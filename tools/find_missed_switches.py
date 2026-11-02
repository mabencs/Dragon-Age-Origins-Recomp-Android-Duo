#!/usr/bin/env python3
"""Find jump tables XenonRecomp translated as indirect calls (BUG-051).

A jump table the recompiler knows about becomes a C `switch`; one it does not know about
ends the function at its `bctr`, which is then emitted as PPC_CALL_INDIRECT_FUNC - a call
through the function table to a case label that is usually not a function, so the lookup
returns null and the title calls host address 0 when that case is taken. DAORecomp's
switch tables come from scripts/find_switches.py, which starts from a `cmplwi` bound
check, so a table whose index is bounded some other way is missed.

This scans the generated code for that shape: `lis`/`addi` of a constant base, an index
scaled by 4 (`rlwinm rI,rX,2,0,29`), `lwzx`, `mtctr`, then a `bctr` emitted as an
indirect call. Addresses are counted from each function's start, one per instruction
comment. A hit needs a function size and a [[switch]] entry in tools/recompile.py's
ADDITIONS.

  python tools/find_missed_switches.py [ppc_tu6|ppc]

Exit status 1 when a candidate is found.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
FUNC = re.compile(r"^PPC_FUNC_IMPL\(__imp__(?:tu6_)?sub_([0-9A-F]{8})\)")
INSN = re.compile(r"^\t+// (?!ERROR)(\S.*)$")


def scan(gen):
    hits = []
    for path in sorted(gen.glob("ppc_recomp.*.cpp")):
        address = None
        recent = []  # (address, text) of the last instructions
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            m = FUNC.match(line)
            if m:
                address = int(m.group(1), 16)
                recent = []
                continue
            if address is None:
                continue
            m = INSN.match(line)
            if m:
                recent.append((address, m.group(1).strip()))
                recent = recent[-8:]
                address += 4
                continue
            if ("PPC_CALL_INDIRECT_FUNC" in line and len(recent) >= 5
                    and recent[-1][1].startswith("bctr")):
                window = [t for _, t in recent[:-1]]
                mt = re.match(r"mtctr (r\d+)$", window[-1])
                lx = re.match(r"lwzx (r\d+),(r\d+),(r\d+)$", window[-2])
                if not (mt and lx and lx.group(1) == mt.group(1)):
                    continue
                base, index = lx.group(2), lx.group(3)
                before = window[:-2]
                if (any(re.match(r"lis %s," % base, t) for t in before)
                        and any(re.match(r"addi %s,%s," % (base, base), t) for t in before)
                        and any(re.match(r"rlwinm %s,r\d+,2,0,29" % index, t) for t in before)):
                    hits.append((recent[-1][0], path.name, " ; ".join(window[-6:])))
    return hits


def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else "ppc_tu6"
    gen = ROOT / "recomp" / "generated" / folder
    if not any(gen.glob("ppc_recomp.*.cpp")):
        raise SystemExit("error: no generated code in %s - run tools/recompile.py first" % gen)
    switches = sum(path.read_text(encoding="utf-8", errors="replace").count("\tswitch (")
                   for path in gen.glob("ppc_recomp.*.cpp"))
    hits = scan(gen)
    print("%s: %d switches; %d candidate jump table(s) translated as indirect calls"
          % (folder, switches, len(hits)))
    for bctr, name, window in hits:
        print("  bctr %08X  %s  %s" % (bctr, name, window[:150]))
    return 1 if hits else 0


if __name__ == "__main__":
    sys.exit(main())
