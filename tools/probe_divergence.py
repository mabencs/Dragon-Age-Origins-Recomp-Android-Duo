#!/usr/bin/env python3
"""
probe_divergence.py - how far has DAORecomp diverged from upstream Xenia?

Part of the Q1 risk probe (see docs/PROBE-Q1.md). The whole port plan rests on
dropping Xenia's Vulkan backend underneath DAORecomp's Xenos frontend. That is
only cheap if DAORecomp left the frontend close to stock.

This compares the backend-agnostic frontend files DAORecomp vendors against the
same files in upstream Xenia Canary. Backend-specific files (gpu/d3d12/,
gpu/dxbc*) and DAO-specific files are excluded, since those differ by design.

Low divergence  => graft a Vulkan backend on, port a small delta.
High divergence => DAO's frontend changes must be ported first, at real cost.

Usage:
  python tools/probe_divergence.py [--root third_party] [--top N]
"""
import argparse
import difflib
import os
import sys

VENDORED = os.path.join("DAORecomp", "xenia", "src", "xenia")
UPSTREAM = os.path.join("xenia-canary", "src", "xenia")
CODE_EXT = (".cc", ".h", ".inc", ".inl")


def excluded(rel):
    """Backend-specific or DAO-specific: expected to differ, so not measured."""
    return (rel.startswith("gpu/d3d12/")
            or rel.startswith("gpu/vulkan/")
            or rel.startswith("gpu/dxbc")
            or rel.startswith("gpu/spirv")
            or "dao_" in rel)


def changed_lines(path_a, path_b):
    la = open(path_a, encoding="utf-8", errors="replace").read().splitlines()
    lb = open(path_b, encoding="utf-8", errors="replace").read().splitlines()
    sm = difflib.SequenceMatcher(None, la, lb)
    delta = sum(max(i2 - i1, j2 - j1)
                for tag, i1, i2, j1, j2 in sm.get_opcodes() if tag != "equal")
    return delta, len(la)


def main():
    ap = argparse.ArgumentParser(description="Measure DAORecomp vs upstream Xenia divergence")
    ap.add_argument("--root", default="third_party",
                    help="directory holding the cloned upstream repos")
    ap.add_argument("--top", type=int, default=15, help="how many files to list")
    a = ap.parse_args()

    base_a = os.path.join(a.root, VENDORED)
    base_b = os.path.join(a.root, UPSTREAM)
    for p in (base_a, base_b):
        if not os.path.isdir(p):
            print("error: not found: %s" % p, file=sys.stderr)
            print("       clone the upstream repos first (see docs/TOOLCHAIN.md)",
                  file=sys.stderr)
            return 1

    rows, missing = [], []
    for root, _dirs, files in os.walk(base_a):
        for f in files:
            if not f.endswith(CODE_EXT):
                continue
            pa = os.path.join(root, f)
            rel = os.path.relpath(pa, base_a).replace(os.sep, "/")
            if excluded(rel):
                continue
            pb = os.path.join(base_b, rel.replace("/", os.sep))
            if not os.path.exists(pb):
                missing.append(rel)
                continue
            delta, total = changed_lines(pa, pb)
            rows.append((delta, total, rel))

    rows.sort(reverse=True)
    tot_changed = sum(r[0] for r in rows)
    tot_lines = sum(r[1] for r in rows)
    identical = sum(1 for r in rows if r[0] == 0)
    pct = 100 * tot_changed / max(tot_lines, 1)

    print("Q1 PROBE - DAORecomp vendored Xenia frontend vs upstream Canary")
    print("=" * 66)
    print("  frontend files compared   %d" % len(rows))
    print("  byte-identical            %d of %d" % (identical, len(rows)))
    print("  vendored lines            %s" % "{:,}".format(tot_lines))
    print("  differing lines           %s  (%.1f%%)" % ("{:,}".format(tot_changed), pct))
    print("  files absent upstream     %d" % len(missing))

    # Pre-generated shader bytecode headers are build artefacts, not real gaps.
    bytecode = [m for m in missing if "shaders/bytecode/" in m]
    if bytecode:
        print("    of which pre-generated shader bytecode: %d (build artefacts)"
              % len(bytecode))
    for m in [m for m in missing if "shaders/bytecode/" not in m][:12]:
        print("    %s" % m)

    print()
    print("  most-diverged frontend files:")
    for delta, total, rel in rows[:a.top]:
        print("    %6d / %-6d (%5.1f%%)  %s"
              % (delta, total, 100 * delta / max(total, 1), rel))

    print()
    if pct < 5:
        print("  VERDICT: frontend is close to stock. Grafting a Vulkan backend on")
        print("           is viable; only a small delta needs porting.")
    elif pct < 20:
        print("  VERDICT: moderate divergence. Budget real time to port frontend changes.")
    else:
        print("  VERDICT: heavy divergence. Re-examine the backend-swap approach.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
