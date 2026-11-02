#!/usr/bin/env python3
"""
graft_inventory.py - what does Xenia's Vulkan backend need that DAORecomp lacks?

Phase 3.1 grafts three things from upstream Xenia Canary onto DAORecomp's vendored
Xenos frontend:

    src/xenia/gpu/vulkan/              the Vulkan GPU backend
    src/xenia/gpu/spirv_shader_*       the SPIR-V shader translator
    src/xenia/ui/vulkan/               Vulkan device, presenter, VMA integration

DAORecomp vendors only a stripped subset of Xenia - enough for its D3D12 path. This
walks the include graph of the files we want to add and reports which headers are
already present, which must come from upstream, and which are third-party.

The point is to size the graft before starting it, and to produce the file list the
assembly step then works from.

Usage:
  python tools/graft_inventory.py
  python tools/graft_inventory.py --list-missing
"""
import argparse
import collections
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DAO_XENIA = ROOT / "third_party" / "DAORecomp" / "xenia" / "src"
CANARY_SRC = ROOT / "third_party" / "xenia-canary" / "src"
CANARY_TP = ROOT / "third_party" / "xenia-canary" / "third_party"

# What we are grafting in.
GRAFT_ROOTS = [
    ("gpu/vulkan", "Vulkan GPU backend"),
    ("ui/vulkan", "Vulkan device / presenter / VMA"),
]
GRAFT_GLOBS = [
    ("gpu", "spirv_shader_translator*", "SPIR-V shader translator"),
    ("gpu", "spirv_builder*", "SPIR-V builder"),
    ("gpu", "spirv_shader.*", "SPIR-V shader object"),
    ("gpu", "spirv_compatibility.h", "SPIR-V compatibility"),
]

INCLUDE = re.compile(r'^\s*#\s*include\s+[<"]([^">]+)[">]', re.M)
# Files that only exist for the tools we do not build.
SKIP = re.compile(r"trace_viewer_main|trace_dump_main|_main\.cc$")


def collect_graft_files():
    """The set of .cc/.h we intend to add, relative to src/."""
    files = {}
    for rel, label in GRAFT_ROOTS:
        d = CANARY_SRC / "xenia" / rel
        if not d.is_dir():
            continue
        for p in sorted(d.iterdir()):
            if p.suffix in (".cc", ".h") and not SKIP.search(p.name):
                files[f"xenia/{rel}/{p.name}"] = label
    for rel, pattern, label in GRAFT_GLOBS:
        for p in sorted((CANARY_SRC / "xenia" / rel).glob(pattern)):
            if p.suffix in (".cc", ".h") and not SKIP.search(p.name):
                files[f"xenia/{rel}/{p.name}"] = label
    return files


def resolve(inc, from_rel):
    """Xenia includes are rooted at src/, with some relative ones."""
    if inc.startswith("xenia/") or inc.startswith("third_party/"):
        return inc
    # relative to the including file's directory
    return str((Path(from_rel).parent / inc).as_posix())


def walk(seed):
    """Transitively follow includes within the Xenia source tree."""
    seen, queue, external = set(), list(seed), collections.Counter()
    while queue:
        rel = queue.pop()
        if rel in seen:
            continue
        seen.add(rel)
        path = CANARY_SRC / rel
        if not path.is_file():
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for inc in INCLUDE.findall(text):
            target = resolve(inc, rel)
            if target.startswith("xenia/"):
                if target not in seen:
                    queue.append(target)
            elif target.startswith("third_party/"):
                external[target.split("/")[1]] += 1
            elif "/" not in inc and "." not in inc:
                pass            # standard library
            else:
                external[inc] += 1
    return seen, external


def main():
    ap = argparse.ArgumentParser(description="Size the Vulkan backend graft")
    ap.add_argument("--list-missing", action="store_true",
                    help="print every Xenia header that must come from upstream")
    a = ap.parse_args()

    for p in (DAO_XENIA, CANARY_SRC):
        if not p.is_dir():
            print(f"error: not found: {p}", file=sys.stderr)
            print("       see THIRD_PARTY.md for how to fetch the sources", file=sys.stderr)
            return 1

    graft = collect_graft_files()
    print("=" * 72)
    print("GRAFT TARGET - files to add from Xenia Canary")
    print("=" * 72)
    by_label = collections.defaultdict(list)
    for rel, label in graft.items():
        by_label[label].append(rel)
    graft_lines = 0
    for label, rels in sorted(by_label.items()):
        lines = 0
        for rel in rels:
            p = CANARY_SRC / rel
            if p.is_file():
                lines += len(p.read_text(encoding="utf-8", errors="replace").splitlines())
        graft_lines += lines
        print(f"  {label:<34} {len(rels):>3} files  {lines:>7,} lines")
    print(f"  {'TOTAL':<34} {len(graft):>3} files  {graft_lines:>7,} lines")

    reachable, external = walk(list(graft.keys()))
    xenia_headers = sorted(r for r in reachable if r.endswith(".h"))

    present, missing = [], []
    for rel in xenia_headers:
        if (DAO_XENIA / rel).is_file():
            present.append(rel)
        elif (CANARY_SRC / rel).is_file():
            missing.append(rel)

    print()
    print("=" * 72)
    print("DEPENDENCY GRAPH - Xenia headers these files reach")
    print("=" * 72)
    print(f"  already in DAORecomp's subset   {len(present):>4}")
    print(f"  must be added from upstream     {len(missing):>4}")
    missing_lines = sum(
        len((CANARY_SRC / r).read_text(encoding="utf-8", errors="replace").splitlines())
        for r in missing)
    print(f"  ...totalling                    {missing_lines:>4,} lines")

    groups = collections.Counter(r.rsplit("/", 1)[0] for r in missing)
    if groups:
        print()
        print("  missing headers by directory:")
        for d, n in groups.most_common():
            print(f"    {d:<40} {n}")

    if a.list_missing and missing:
        print()
        print("  full list:")
        for r in missing:
            print(f"    {r}")

    print()
    print("=" * 72)
    print("THIRD-PARTY / EXTERNAL INCLUDES")
    print("=" * 72)
    for name, n in external.most_common(24):
        here = (CANARY_TP / name).exists()
        mark = "vendored" if here else "NEEDS FETCH"
        print(f"  {name:<44} {n:>4} refs   {mark}")

    print()
    print("=" * 72)
    print("ASSESSMENT")
    print("=" * 72)
    total = graft_lines + missing_lines
    print(f"  code to bring across            {total:,} lines")
    print(f"    the backend itself            {graft_lines:,}")
    print(f"    supporting headers            {missing_lines:,}")
    print("  DAORecomp frontend deltas to port  ~1,700 lines (see PROBE-Q1.md)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
