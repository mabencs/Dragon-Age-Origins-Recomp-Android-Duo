#!/usr/bin/env python3
"""
recompile.py - Phase 1 driver: translate the game's PowerPC code to C++.

Runs XenonRecomp over your disc's executable (optionally patched to Title Update 7)
using DAORecomp's configuration, which holds the function database and switch tables
for this exact binary. Output lands in recomp/generated/, which is gitignored: it is
reproducible from your disc and is far too large to commit.

Two builds exist, matching upstream:

  disc  config/DAO.toml      -> recomp/generated/ppc/       symbols unprefixed
  tu    config/DAO_tu6.toml  -> recomp/generated/ppc_tu6/   symbols prefixed tu6_

The "tu6" naming is upstream's and is kept deliberately even though we feed Title
Update 7: upstream states TU7's code differs from TU6's only by a version number, so
the same configuration and the same generated symbol names serve both. Renaming would
break the runtime sources that reference tu6_ symbols for no benefit.

Where DAORecomp's configuration is wrong for this binary, ADDITIONS below corrects the
copy handed to XenonRecomp (a function's size, a jump table it missed - BUG-051);
tools/find_missed_switches.py checks the output for another such table.

Usage:
  python tools/recompile.py --check          verify inputs, change nothing
  python tools/recompile.py tu               the Title Update build (what we ship)
  python tools/recompile.py disc             the disc build
  python tools/recompile.py both
"""
import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DAO = ROOT / "third_party" / "DAORecomp"
XENON = ROOT / "third_party" / "XenonRecomp"
RECOMPILER = ROOT / "build" / "xenonrecomp" / "XenonRecomp" / "XenonRecomp.exe"
PPC_CONTEXT = XENON / "XenonUtils" / "ppc_context.h"
GENERATED = ROOT / "recomp" / "generated"
ANALYSIS = ROOT / "build" / "recomp-logs"

# From DAORecomp installer/deps.py - the exact binaries its config was built against.
GAME_XEX_SHA1 = "532c0ec164f44bc8784cc0e8f6f67f72c1ed3751"
TU_XEXP_SHA1S = {"4141b608e44c4ed35eb4e24ba2c05844a2f57a54": 6,
                 "dd5b493f4fe2fb4c99e6ef7ed6923ddddeee8291": 7}

BUILDS = {
    "disc": {"config": "DAO", "out": "ppc", "prefix": None, "needs_tu": False},
    "tu": {"config": "DAO_tu6", "out": "ppc_tu6", "prefix": "tu6", "needs_tu": True},
}


def sha1(path):
    h = hashlib.sha1()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def check_inputs(require_tu):
    """Verify everything Phase 1 needs, with specific messages rather than a crash."""
    ok = True
    print("inputs")

    xex = DAO / "game" / "default.xex"
    if not xex.is_file():
        print("  [FAIL] game/default.xex missing")
        print("         python tools/dao_iso.py extract <your>.iso default.xex "
              "third_party/DAORecomp/game/default.xex")
        ok = False
    else:
        digest = sha1(xex)
        if digest == GAME_XEX_SHA1:
            print("  [ OK ] game/default.xex          sha1 %s" % digest)
        else:
            print("  [FAIL] game/default.xex          sha1 %s" % digest)
            print("         expected %s - wrong disc revision; the function" % GAME_XEX_SHA1)
            print("         database in config/ will not match this binary.")
            ok = False

    xexp = DAO / "title_update" / "default.xexp"
    if not xexp.is_file():
        label = "[FAIL]" if require_tu else "[ -- ]"
        print("  %s title_update/default.xexp missing" % label)
        if require_tu:
            print("         python -I third_party/DAORecomp/scripts/stfs.py extract "
                  "<TU package> work/tu7 ")
            ok = False
    else:
        digest = sha1(xexp)
        tu = TU_XEXP_SHA1S.get(digest)
        if tu:
            print("  [ OK ] title_update/default.xexp Title Update %d" % tu)
        else:
            print("  [FAIL] title_update/default.xexp sha1 %s" % digest)
            print("         not Title Update 6 or 7.")
            ok = False

    for label, path in (("XenonRecomp.exe", RECOMPILER), ("ppc_context.h", PPC_CONTEXT)):
        if path.is_file():
            print("  [ OK ] %-24s %s" % (label, path.relative_to(ROOT)))
        else:
            print("  [FAIL] %-24s missing: %s" % (label, path))
            if path is RECOMPILER:
                print("         build it: see docs/TOOLCHAIN.md (Phase 1)")
            ok = False

    # The patch adds the PPC/VMX128 instructions DAO needs; without it the
    # recompiler stops on unimplemented mnemonics.
    recompiler_cpp = XENON / "XenonRecomp" / "recompiler.cpp"
    if recompiler_cpp.is_file():
        patched = "DAO additions" in recompiler_cpp.read_text(encoding="utf-8", errors="replace")
        print("  [%s] XenonRecomp DAO patch    %s"
              % (" OK " if patched else "FAIL", "applied" if patched else "NOT applied"))
        if not patched:
            print("         python -I third_party/DAORecomp/scripts/patch_recompiler.py \\")
            print("                   third_party/XenonRecomp/XenonRecomp/recompiler.cpp")
            ok = False
    return ok


# Corrections to DAORecomp's configuration, applied to the generated copy of its TOML
# (write_redirected_config) - its files in third_party stay as fetched. Keyed by the
# config's name; each says what it fixes and how it was found.
#
# None now. Until 2026-10-30 DAO_tu6 had BUG-051's: sub_82D7D148, ending in a jump table
# (a bctr at 82D7D1EC through seven words, its index r11 set to 0-6 by float comparisons
# rather than bounded by a cmplwi, so DAORecomp's scripts/find_switches.py skipped it and
# the analysis ended the function at the table; the title called a null host function
# when the Korcari Wilds loaded, run 217) - the function (0x82D7D148, 0x108) and the table
# (0x82D7D1EC, r11, 82D7D20C ... 82D7D244). DAORecomp found the same and fixed it upstream
# (7da0e07, "the Aeducan Thaig crash", issue #5), with the very same bounds and table, in
# the pin since eb2360a - and this list refuses a function the config already has.
# Entries take the shape {"DAO_tu6": {"functions": [(address, size)], "switches":
# [(bctr address, index register, [case labels in index order])]}}.
ADDITIONS = {}


def apply_additions(config_name, text):
    """ADDITIONS for this config into its TOML text; a switch-table copy beside the
    original when there are switches. Returns the text and the copy's path (or None).
    Asserted: an addition that upstream has since made itself, or a TOML whose shape
    changed, stops the recompile instead of being applied twice or nowhere."""
    additions = ADDITIONS.get(config_name)
    if not additions:
        return text, None
    for address, _ in additions["functions"]:
        if re.search(r"address = 0x%08X\b" % address, text, flags=re.I):
            raise SystemExit("error: %s already lists a function at %08X - drop it from "
                             "ADDITIONS" % (config_name, address))
    rows = "".join("    { address = 0x%08X, size = 0x%X },\n" % f for f in additions["functions"])
    text, n = re.subn(r"^functions = \[\n", lambda m: m.group(0) + rows, text, count=1,
                      flags=re.M)
    if n != 1:
        raise SystemExit("error: no 'functions = [' list in %s.toml to add to" % config_name)
    switch_copy = None
    if additions["switches"]:
        m = re.search(r'^switch_table_file_path\s*=\s*"([^"]+)"', text, flags=re.M)
        if not m:
            raise SystemExit("error: %s.toml names no switch-table file" % config_name)
        original = DAO / "config" / m.group(1)
        tables = original.read_text(encoding="utf-8")
        for base, _, _ in additions["switches"]:
            if re.search(r"base = 0x%08X\b" % base, tables, flags=re.I):
                raise SystemExit("error: %s already has the switch at %08X - drop it from "
                                 "ADDITIONS" % (original.name, base))
        tables += "".join(
            "\n[[switch]]\nbase = 0x%08X\nr = %d\nlabels = [\n%s]\n"
            % (base, r, "".join("    0x%08X,\n" % label for label in labels))
            for base, r, labels in additions["switches"])
        switch_copy = original.with_name(original.stem + ".gen.toml")
        switch_copy.write_text(tables, encoding="utf-8")
        text = text.replace(m.group(0), 'switch_table_file_path = "%s"' % switch_copy.name)
    print("  ADDITIONS: %d function(s), %d switch table(s) added to %s"
          % (len(additions["functions"]), len(additions["switches"]), config_name))
    return text, switch_copy


def write_redirected_config(build):
    """Copy DAORecomp's TOML with out_directory_path pointed at our tree.

    Two constraints from XenonRecomp, both learned the hard way:

    * Every path in the TOML is resolved against the *config file's* directory
      (recompiler.cpp builds "{configDir}{outDirectoryPath}/{file}"), so an absolute
      output path turns into "config/C:/..." and the writes fail. The redirect has to
      be relative to the config directory. The copy must also live beside the
      original, because the switch-table path resolves the same way.
    * The output directory has to exist already. XenonRecomp does not create it, and
      the resulting failure is an uncaught exception that aborts with 0xC0000409 and
      no message at all.
    """
    src = DAO / "config" / ("%s.toml" % build["config"])
    dst = DAO / "config" / ("%s.gen.toml" % build["config"])
    out_dir = GENERATED / build["out"]
    out_dir.mkdir(parents=True, exist_ok=True)

    rel = os.path.relpath(out_dir, DAO / "config").replace(os.sep, "/")
    text = src.read_text(encoding="utf-8")
    text, n = re.subn(r"^out_directory_path\s*=.*$",
                      'out_directory_path = "%s"' % rel, text, count=1, flags=re.M)
    if n != 1:
        raise SystemExit("error: could not redirect out_directory_path in %s" % src)
    text, switch_copy = apply_additions(build["config"], text)
    dst.write_text(text, encoding="utf-8")
    return dst, out_dir, switch_copy


def run_build(name):
    build = BUILDS[name]
    print()
    print("=" * 68)
    print("recompiling: %s build (config %s -> recomp/generated/%s)"
          % (name, build["config"], build["out"]))
    print("=" * 68)

    config, out_dir, switch_copy = write_redirected_config(build)
    ANALYSIS.mkdir(parents=True, exist_ok=True)
    log = ANALYSIS / ("recomp_%s.log" % name)

    started = time.time()
    # PLAN 12.4: the build's prefix (tu6) is XenonRecomp's third argument - its names are
    # given as each file is written (tools/patch_xenonrecomp.py), what DAORecomp's
    # scripts/prefix_symbols.py did to the files after, so the device that makes the
    # game's code runs the same one program.
    command = [str(RECOMPILER), str(config), str(PPC_CONTEXT)]
    if build["prefix"]:
        command.append(build["prefix"])
    try:
        with open(log, "w", encoding="utf-8", errors="replace") as fh:
            proc = subprocess.run(command, stdout=fh, stderr=subprocess.STDOUT, cwd=str(DAO))
    finally:
        config.unlink(missing_ok=True)
        if switch_copy:
            switch_copy.unlink(missing_ok=True)
    elapsed = time.time() - started

    tail = log.read_text(encoding="utf-8", errors="replace").splitlines()
    # "Recompiling functions..." is progress noise; show anything else.
    notable = [l for l in tail if l.strip() and "Recompiling functions" not in l]
    for line in notable[-15:]:
        print("  %s" % line)

    if proc.returncode != 0:
        print("  FAILED (exit %d) - see %s" % (proc.returncode, log.relative_to(ROOT)))
        # 0xC0000409 with an empty log means an uncaught C++ exception aborted the
        # process. In practice that has meant a path problem: XenonRecomp resolves
        # every TOML path against the config directory and does not create the
        # output directory.
        if proc.returncode & 0xFFFFFFFF == 0xC0000409:
            print("  that exit code is a fastfail from an uncaught exception.")
            print("  check: does %s exist, and is out_directory_path relative"
                  % out_dir.relative_to(ROOT))
            print("         to %s ?" % (DAO / 'config').relative_to(ROOT))
        return False

    if build["prefix"]:
        print("  symbols prefixed with %s_ as written" % build["prefix"])

    files = sorted(out_dir.glob("*.cpp")) + sorted(out_dir.glob("*.h"))
    total = sum(f.stat().st_size for f in files)
    print()
    print("  generated   %d files, %s bytes" % (len(files), "{:,}".format(total)))
    print("  elapsed     %.1f s" % elapsed)
    print("  output      %s" % out_dir.relative_to(ROOT))
    print("  log         %s" % log.relative_to(ROOT))
    return True


def main():
    ap = argparse.ArgumentParser(description="Translate the game's PowerPC code to C++")
    ap.add_argument("build", nargs="?", default="tu", choices=["disc", "tu", "both"])
    ap.add_argument("--check", action="store_true", help="verify inputs and exit")
    a = ap.parse_args()

    names = ["disc", "tu"] if a.build == "both" else [a.build]
    require_tu = any(BUILDS[n]["needs_tu"] for n in names)

    if not check_inputs(require_tu):
        print()
        print("inputs are not ready; nothing was run.")
        return 1
    if a.check:
        print()
        print("inputs ready.")
        return 0

    for name in names:
        if not run_build(name):
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
