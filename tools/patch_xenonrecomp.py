#!/usr/bin/env python3
"""
patch_xenonrecomp.py - XenonRecomp taught the instructions it left as traps in this title.

PLAN 2.4. Where XenonRecomp meets a form of an instruction it cannot translate, it writes
`__builtin_debugtrap()` in its place: running it kills the game. Six such places are in
Dragon Age: Origins' Title Update 7 code (recomp/generated/ppc_tu6):
  82AADBE0  blrl                     - a call through the link register (`mtlr r3; blrl`)
  82D9F030  vpkd3d128 type 3, pack 1 - two floats packed as two Xenos halves (D3D's)
  82DB4D58, 82DB4E60  vupkd3d128 type 3 - two Xenos halves unpacked (x, y; z 0, w 1)
  82EC2F60, 82EC3080  vupkd3d128 type 5 - four Xenos halves unpacked
The tour of every area never ran one, but a rare path would end the game without a word.
These edits translate them as Xenia's CPU does (xenia-canary, ppc_emit_altivec.cc and the
x64 backend's pack/unpack sequences, math.h's float_to_xenos_half/xenos_half_to_float):
  - Xenos halves have no infinity or NaN: exponent 31 is a number (up to 131008); a
    float that large or a NaN packs to 0x7FFF with its sign; packing truncates (no
    rounding) and flushes what would be a denormal half to zero; unpacking flushes
    denormal halves to zero.
  - vpkd3d128's pack and shift fields choose which words of the destination take the
    packed word, by Xenia's permute table; the others keep their value.
XenonRecomp keeps each vector byte-reversed (the PowerPC's element i is the host's
u32/f32[3 - i], halfword k the host's u16[7 - k]), as its own D3DCOLOR and NORMSHORT2
cases do.

PLAN 12.4 adds the build's symbol prefix to XenonRecomp itself (recompiler.h, main.cpp,
recompiler.cpp): DAORecomp's scripts/prefix_symbols.py, which renames the generated
code's symbols for the TU build (tu6_sub_...), done as each file is written, so the
device that makes the game's code (PLAN 12) needs no Python.

Applied in place to third_party/XenonRecomp after DAORecomp's patch_recompiler.py
(tools/bootstrap.py, step 4), as that script patches it; each edit a find/replace that
must match once, in groups that each carry a marker, and a file already holding a
group's marker is left as it is. The recompiler must then be built again and the title
recompiled (tools/recompile.py tu).

Usage:
  python tools/patch_xenonrecomp.py            patch (if not yet)
  python tools/patch_xenonrecomp.py --check    report only
"""
import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RECOMPILER_CPP = ROOT / "third_party" / "XenonRecomp" / "XenonRecomp" / "recompiler.cpp"
MARKER = "DAO/Warden (PLAN 2.4)"

# (what, find, replace)
EDITS = [
    ("blrl: a call through the link register",
     '    case PPC_INST_BLRL:\n'
     '        println("__builtin_debugtrap();");\n'
     '        break;\n',
     '    case PPC_INST_BLRL:\n'
     '        // DAO/Warden (PLAN 2.4): a call through the link register - the target read\n'
     '        // before the link register takes the return address, then as bctrl calls.\n'
     '        println("\\t{}.u64 = ctx.lr;", temp());\n'
     '        if (!config.skipLr)\n'
     '            println("\\tctx.lr = 0x{:X};", base + 4);\n'
     '        println("\\tPPC_CALL_INDIRECT_FUNC({}.u32);", temp());\n'
     '        csrState = CSRState::Unknown; // the call could change it\n'
     '        break;\n'),

    ("vpkd3d128 type 3: two floats packed as Xenos halves (FLOAT16_2)",
     '        default:\n'
     '            println("\\t__builtin_debugtrap();");\n'
     '            break;\n'
     '        }\n'
     '        break;\n'
     '\n'
     '    case PPC_INST_VPKSHUS:\n',
     '        case 3: // float16_2\n'
     '        {\n'
     '            // DAO/Warden (PLAN 2.4): x and y (the host\'s f32[3] and [2]) as Xenos\n'
     '            // halves, truncated - (x << 16) | y, the packed word - then placed as\n'
     '            // Xenia\'s permute table for the pack and shift says.\n'
     '            for (size_t i = 0; i < 2; i++)\n'
     '            {\n'
     '                println("\\t{}.u32 = {}.u32[{}] & 0x7FFFFFFF;", temp(), v(insn.operands[1]), 3 - i);\n'
     '                println("\\t{0}.u32 = {0}.u32 >= 0x47FFE000 ? 0x7FFF : ({0}.u32 < 0x38800000 ? 0 : ((({0}.u32 + 0xC8000000) >> 13) & 0x7FFF));", temp());\n'
     '                println("\\t{}.u32[{}] = {}.u32 | (({}.u32[{}] & 0x80000000) >> 16);", vTemp(), i, temp(), v(insn.operands[1]), 3 - i);\n'
     '            }\n'
     '            println("\\t{0}.u32 = ({1}.u32[0] << 16) | {1}.u32[1];", temp(), vTemp());\n'
     '            // Per pack (1: 32-bit, 2 and 3: 64-bit) and shift, each result word\n'
     '            // (x, y, z, w) as {from the packed value?, which word}; the packed\n'
     '            // value\'s words are 0, 0, 0 and the packed word.\n'
     '            static constexpr uint8_t kPermute[3][4][8] = {\n'
     '                {{0, 0, 0, 1, 0, 2, 1, 3}, {0, 0, 0, 1, 1, 3, 0, 3}, {0, 0, 1, 3, 0, 2, 0, 3}, {1, 3, 0, 1, 0, 2, 0, 3}},\n'
     '                {{0, 0, 0, 1, 1, 2, 1, 3}, {0, 0, 1, 2, 1, 3, 0, 3}, {1, 2, 1, 3, 0, 2, 0, 3}, {1, 3, 0, 1, 0, 2, 0, 3}},\n'
     '                {{0, 0, 0, 1, 1, 2, 1, 3}, {0, 0, 1, 2, 1, 3, 0, 3}, {1, 2, 1, 3, 0, 2, 0, 3}, {0, 0, 0, 1, 0, 2, 1, 2}},\n'
     '            };\n'
     '            const size_t pack = insn.operands[3], shift = insn.operands[4];\n'
     '            if (pack < 1 || pack > 3 || shift > 3)\n'
     '            {\n'
     '                fmt::println("Unexpected float16_2 pack instruction at {:X}", base);\n'
     '                println("\\t__builtin_debugtrap();");\n'
     '                break;\n'
     '            }\n'
     '            const uint8_t* control = kPermute[pack - 1][shift];\n'
     '            for (size_t word = 0; word < 4; word++)\n'
     '            {\n'
     '                const size_t from = control[word * 2], index = control[word * 2 + 1];\n'
     '                if (from)\n'
     '                    println("\\t{}.u32[{}] = {};", vTemp(), 3 - word, index == 3 ? fmt::format("{}.u32", temp()) : std::string("0"));\n'
     '                else\n'
     '                    println("\\t{}.u32[{}] = {}.u32[{}];", vTemp(), 3 - word, v(insn.operands[0]), 3 - index);\n'
     '            }\n'
     '            println("\\t{} = {};", v(insn.operands[0]), vTemp());\n'
     '            break;\n'
     '        }\n'
     '\n'
     '        default:\n'
     '            println("\\t__builtin_debugtrap();");\n'
     '            break;\n'
     '        }\n'
     '        break;\n'
     '\n'
     '    case PPC_INST_VPKSHUS:\n'),

    ("vupkd3d128 types 3 and 5: Xenos halves unpacked (FLOAT16_2, FLOAT16_4)",
     '        default:\n'
     '            println("\\t__builtin_debugtrap();");\n'
     '            break;\n'
     '        }\n'
     '        break;\n'
     '\n'
     '    case PPC_INST_VUPKHSB:\n',
     '        case 3: // float16_2\n'
     '        case 5: // float16_4\n'
     '        {\n'
     '            // DAO/Warden (PLAN 2.4): Xenos halves to floats, a denormal half to zero:\n'
     '            // float16_2 x and y from halfwords 6 and 7 (the host\'s u16[1] and [0]),\n'
     '            // z 0 and w 1; float16_4 each element from halfwords 4-7 (the host\'s\n'
     '            // f32[j] from u16[j]). Into the scratch vector first: the source may be\n'
     '            // the destination.\n'
     '            const bool four = (insn.operands[2] >> 2) == 5;\n'
     '            for (size_t j = 0; j < (four ? 4 : 2); j++)\n'
     '            {\n'
     '                const size_t from = four ? j : 1 - j, to = four ? j : 3 - j;\n'
     '                println("\\t{}.u32 = {}.u16[{}];", temp(), v(insn.operands[1]), from);\n'
     '                println("\\t{0}.u32[{1}] = ({2}.u32 & 0x7C00) ? ((({2}.u32 & 0x8000) << 16) | (((({2}.u32 >> 10) & 0x1F) + 112) << 23) | (({2}.u32 & 0x3FF) << 13)) : (({2}.u32 & 0x8000) << 16);", vTemp(), to, temp());\n'
     '            }\n'
     '            if (!four)\n'
     '            {\n'
     '                println("\\t{}.u32[1] = 0x00000000;", vTemp());\n'
     '                println("\\t{}.u32[0] = 0x3F800000;", vTemp());\n'
     '            }\n'
     '            println("\\t{} = {};", v(insn.operands[0]), vTemp());\n'
     '            break;\n'
     '        }\n'
     '\n'
     '        default:\n'
     '            println("\\t__builtin_debugtrap();");\n'
     '            break;\n'
     '        }\n'
     '        break;\n'
     '\n'
     '    case PPC_INST_VUPKHSB:\n'),
]


# PLAN 12.4: the build's own names, given as XenonRecomp writes each file - DAORecomp's
# scripts/prefix_symbols.py in C++, because the device that makes the game's code (the
# published APK does, PLAN 12) has no Python. tools/recompile.py passes the prefix (tu6)
# as XenonRecomp's third argument; the files come out byte for byte as the script made
# them (checked against the script's output when this was written).
PREFIX_MARKER = "DAO/Warden (PLAN 12.4)"

PREFIX_FUNCTION = r'''// DAO/Warden (PLAN 12.4): DAORecomp's scripts/prefix_symbols.py as each file is written,
// its four rules in one pass - sub_XXXXXXXX (after __imp__ too) unless prefixed already;
// the register save and restore helpers (__restgprlr_14 to __tu6_restgprlr_14); the entry
// point (_xstart); PPCFuncMappings. Imports (__imp__<export>) keep their names.
static std::string ApplySymbolPrefix(const std::string& in, const std::string& prefix)
{
    auto alnum = [](char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
    auto word = [&](char c) { return alnum(c) || c == '_'; };
    auto upperHex = [](char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'); };
    const std::string mine = prefix + "_";
    const size_t n = in.size();
    std::string result;
    result.reserve(n + n / 8);
    size_t i = 0;
    while (i < n)
    {
        const char c = in[i];
        const bool afterAlnum = i > 0 && alnum(in[i - 1]);
        if (c == 's' && !afterAlnum && i + 12 <= n && in.compare(i, 4, "sub_") == 0 &&
            !(i >= mine.size() && in.compare(i - mine.size(), mine.size(), mine) == 0))
        {
            bool hex = true;
            for (size_t k = 4; k < 12; k++)
                hex = hex && upperHex(in[i + k]);
            if (hex && (i + 12 == n || !word(in[i + 12])))
            {
                result += mine;
                result += "sub_";
                i += 4;
                continue;
            }
        }
        if (c == '_' && !afterAlnum && in.compare(i, 2, "__") == 0)
        {
            size_t j = i + 2;
            std::string_view kind, set;
            for (std::string_view k : {std::string_view("rest"), std::string_view("save")})
                if (in.compare(j, k.size(), k) == 0)
                    kind = k;
            if (!kind.empty())
            {
                j += kind.size();
                for (std::string_view s : {std::string_view("gprlr"), std::string_view("fpr"), std::string_view("vmx")})
                    if (in.compare(j, s.size(), s) == 0)
                        set = s;
            }
            if (!set.empty() && j + set.size() < n && in[j + set.size()] == '_')
            {
                const size_t digits = j + set.size() + 1;
                size_t end = digits;
                while (end < n && in[end] >= '0' && in[end] <= '9')
                    end++;
                if (end > digits && (end == n || !word(in[end])))
                {
                    result += "__";
                    result += mine;
                    result += kind;
                    result += set;
                    result += '_';
                    result.append(in, digits, end - digits);
                    i = end;
                    continue;
                }
            }
        }
        if (c == '_' && !afterAlnum && in.compare(i, 7, "_xstart") == 0 && (i + 7 == n || !word(in[i + 7])))
        {
            result += '_';
            result += mine;
            result += "xstart";
            i += 7;
            continue;
        }
        if (c == 'P' && !(i > 0 && word(in[i - 1])) && in.compare(i, 15, "PPCFuncMappings") == 0 &&
            (i + 15 == n || !word(in[i + 15])))
        {
            result += "PPCFuncMappings_";
            result += prefix;
            i += 15;
            continue;
        }
        result += c;
        i++;
    }
    return result;
}

'''

PREFIX_EDITS_H = [
    ("the prefix, a member of the recompiler",
     '    std::string out;\n'
     '    size_t cppFileIndex = 0;\n'
     '    RecompilerConfig config;\n',
     '    std::string out;\n'
     '    size_t cppFileIndex = 0;\n'
     '    RecompilerConfig config;\n'
     '    // DAO/Warden (PLAN 12.4): the names\' prefix (tu6), empty for none.\n'
     '    std::string symbolPrefix;\n'),
]

PREFIX_EDITS_MAIN = [
    ("the prefix from a third argument",
     '        recompiler.Recompile(headerFilePath);\n',
     '        // DAO/Warden (PLAN 12.4): a third argument names the build\'s prefix.\n'
     '        if (argc > 3)\n'
     '            recompiler.symbolPrefix = argv[3];\n'
     '        recompiler.Recompile(headerFilePath);\n'),
]

PREFIX_EDITS_CPP = [
    ("the prefix applied as each file is written",
     'void Recompiler::SaveCurrentOutData(const std::string_view& name)\n'
     '{\n'
     '    if (!out.empty())\n'
     '    {\n',
     PREFIX_FUNCTION +
     'void Recompiler::SaveCurrentOutData(const std::string_view& name)\n'
     '{\n'
     '    if (!out.empty())\n'
     '    {\n'
     '        if (!symbolPrefix.empty())\n'
     '            out = ApplySymbolPrefix(out, symbolPrefix);\n'),
]

# Each group: the file (under third_party/XenonRecomp), the marker a patched file holds,
# the edits. Groups are applied in order, each once.
GROUPS = [
    ("XenonRecomp/recompiler.cpp", MARKER, EDITS),
    ("XenonRecomp/recompiler.h", PREFIX_MARKER, PREFIX_EDITS_H),
    ("XenonRecomp/main.cpp", PREFIX_MARKER, PREFIX_EDITS_MAIN),
    ("XenonRecomp/recompiler.cpp", PREFIX_MARKER, PREFIX_EDITS_CPP),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    xenon = RECOMPILER_CPP.parent.parent
    failures = 0
    written = []
    for relative, marker, edits in GROUPS:
        path = xenon / relative
        if not path.is_file():
            print("  [FAIL] %s is missing (tools/bootstrap.py fetches XenonRecomp)" % path)
            return 1
        raw = path.read_bytes().decode("utf-8")
        crlf = "\r\n" in raw
        text = raw.replace("\r\n", "\n")
        if marker in text:
            print("  [ OK ] %s already patched (%s)" % (relative, marker))
            continue
        group_failures = 0
        for what, find, replace in edits:
            n = text.count(find)
            ok = n == 1
            group_failures += 0 if ok else 1
            print("  [%s] %s: %s%s" % (" OK " if ok else "FAIL", relative, what,
                                       "" if ok else "  (matched %dx, wanted 1)" % n))
            if ok:
                text = text.replace(find, replace)
        failures += group_failures
        if group_failures or args.check:
            continue
        if crlf:
            text = text.replace("\n", "\r\n")
        path.write_bytes(text.encode("utf-8"))
        written.append(relative)
    if failures:
        print("\n%d edit(s) did not match: XenonRecomp's pin moved, or DAORecomp's "
              "patch_recompiler.py was not run first?" % failures)
        return 1
    if args.check:
        print("\nall edits apply; nothing written (--check)")
    elif written:
        print("  patched %s - build XenonRecomp again and recompile (tools/recompile.py tu)"
              % ", ".join(written))
    return 0


if __name__ == "__main__":
    sys.exit(main())
