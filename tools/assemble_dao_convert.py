#!/usr/bin/env python3
"""
assemble_dao_convert.py - DAORecomp's PC-to-360 mod converter, made to build on Android.

PLAN 10.13. Upstream's converter (third_party/DAORecomp/DAORecomp/mods/convert/ and what
it reaches: mods/game_files, mods/mod_files, ui/erf, files) is portable C++ but for a few
Windows calls. Four of its files build as they are, straight from the pinned checkout
(CMakeLists.txt); these four need the edits below (gff.cpp's fix an upstream bug, PLAN
10.19), and are written to build/dao-convert/ with the same relative paths. The stand-ins for the rest (text.h, mods/mods.h,
mods/mod_install.h, stb's zlib decoders) are app/src/main/cpp/dao_compat/ and
dao_convert_android.cc.

As tools/assemble_runtime.py does: each edit is a find/replace that must match exactly the
number of times given, so a moved pin fails loudly and names the edit. Nothing upstream
is committed; this script and its edits are.

Usage:
  python tools/assemble_dao_convert.py
  python tools/assemble_dao_convert.py --check     report, write nothing
"""
import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DAO = ROOT / "third_party" / "DAORecomp" / "DAORecomp"
OUT = ROOT / "build" / "dao-convert"

CONVERT = "mods/convert/convert.cpp"
MOD_FILES = "mods/mod_files.cpp"
FILES = "files.cpp"
GFF = "mods/convert/gff.cpp"

# (file, what, find, replace, times)
EDITS = [
    (CONVERT, "POSIX: getpid",
     '#include "mods/convert/convert.h"\n',
     '#include "mods/convert/convert.h"\n\n#include <unistd.h>  // getpid (Android)\n', 1),
    (CONVERT, "the work folder's name: narrow strings, the process id from getpid",
     "(modFolder.filename().native() + L\"-\" + std::to_wstring(GetCurrentProcessId()))",
     "(modFolder.filename().string() + \"-\" + std::to_string(getpid()))", 1),
    (CONVERT, "relative paths as UTF-8 directly (paths are UTF-8 on Android)",
     "WideToUtf8(path.lexically_relative(base).generic_wstring())",
     "path.lexically_relative(base).generic_string()", 1),
    (CONVERT, "a package's entries' labels as UTF-8 directly",
     "WideToUtf8(relative.generic_wstring())",
     "relative.generic_string()", 2),
    (CONVERT, "narrow folder names",
     "work_.Dir() / L\"packages\" / std::to_wstring(++unpacked_)",
     "work_.Dir() / \"packages\" / std::to_string(++unpacked_)", 1),
    (CONVERT, "narrow folder names (out)",
     "work_.Dir() / L\"out\"",
     "work_.Dir() / \"out\"", 1),
    (CONVERT, "narrow temporary names",
     "out / std::to_wstring(i)",
     "out / std::to_string(i)", 1),

    (MOD_FILES, "the lock file's name, narrow",
     "constexpr const wchar_t* kLockFile = L\"lock\";",
     "constexpr const char* kLockFile = \"lock\";", 1),
    (MOD_FILES, "no Windows file lock: one conversion at a time in the app",
     "        lock_ = CreateFileW((dir_ / kLockFile).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,\n"
     "            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);\n",
     "        // Android: the app converts one mod at a time (ModsPage), so a marker file\n"
     "        // stands for Windows' exclusive handle.\n"
     "        std::ofstream((dir_ / kLockFile)).put('\\n');\n"
     "        lock_ = nullptr;\n", 1),
    (MOD_FILES, "no handle to close",
     "        if (lock_ != INVALID_HANDLE_VALUE)\n            CloseHandle(lock_);\n",
     "        (void)lock_;\n", 1),
    (MOD_FILES, "no junctions on Android",
     " || type == fs::file_type::junction", "", 1),

    (FILES, "a plain folder, POSIX",
     "        const DWORD attributes = GetFileAttributesW(path.c_str());\n"
     "        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) &&\n"
     "            !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);\n",
     "        std::error_code ec;\n"
     "        return std::filesystem::is_directory(std::filesystem::symlink_status(path, ec));\n", 1),
    (FILES, "a narrow .part suffix",
     "concat(L\".part\")", "concat(\".part\")", 1),

    # PLAN 10.19, an upstream bug: a generic field holding a string (a module's or an
    # area's string variable) points at the string itself - its count, then its
    # characters - in the PC's files and the 360's alike (the 360's singleplayer.cif:
    # PARTY_BANTER_DIALOG_FILE), not at a slot pointing to it. Upstream read and wrote
    # one pointer too many, so a PC add-in's module (.cif) with such a variable was "not
    # a well-formed GFF file" (No Helmet Hack's).
    (GFF, "a generic string read where it is",
     "                    if (ref != kNull)\n"
     "                        value.items.push_back(Read(value.type, value.flags, uint64_t(dataOffset_) + ref, depth + 1));\n",
     "                    if (ref != kNull && value.type == kString && !(value.flags & (kList | kStruct | kRef)))\n"
     "                    {\n"
     "                        // A generic string's data is the string itself (PLAN 10.19).\n"
     "                        Value text;\n"
     "                        text.kind = Value::Kind::String;\n"
     "                        text.text = String(ref);\n"
     "                        value.items.push_back(std::move(text));\n"
     "                    }\n"
     "                    else if (ref != kNull)\n"
     "                        value.items.push_back(Read(value.type, value.flags, uint64_t(dataOffset_) + ref, depth + 1));\n",
     1),
    (GFF, "a generic string written where its field points",
     "                    if (v.items.empty())\n"
     "                        return Put32(at + 4, kNull);\n"
     "                    const size_t base = Alloc(SlotSize(v.type, v.flags, templates_), BlockAlign(v.type, v.flags));\n",
     "                    if (v.items.empty())\n"
     "                        return Put32(at + 4, kNull);\n"
     "                    if (v.type == kString && !(v.flags & (kList | kStruct | kRef)))\n"
     "                        // A generic string's data is the string itself (PLAN 10.19).\n"
     "                        return WriteString(v.items[0].kind == Value::Kind::String ? &v.items[0].text : nullptr,\n"
     "                            at + 4);\n"
     "                    const size_t base = Alloc(SlotSize(v.type, v.flags, templates_), BlockAlign(v.type, v.flags));\n",
     1),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    texts = {}
    failures = 0
    for rel, what, find, repl, times in EDITS:
        if rel not in texts:
            src = DAO / rel
            if not src.is_file():
                print(f"  [FAIL] {rel}: not in {DAO}")
                failures += 1
                continue
            texts[rel] = src.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")
        n = texts[rel].count(find)
        ok = n == times
        failures += 0 if ok else 1
        print(f"  [{' OK ' if ok else 'FAIL'}] {Path(rel).name:<16} {what}"
              + ("" if ok else f"  (matched {n}x, wanted {times})"))
        if ok:
            texts[rel] = texts[rel].replace(find, repl)
    if failures:
        print(f"\n{failures} edit(s) did not match: the DAORecomp pin moved? (THIRD_PARTY.md)")
        return 1
    if args.check:
        print("\nall edits apply; nothing written (--check)")
        return 0
    for rel, text in texts.items():
        dst = OUT / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_text(text, encoding="utf-8", newline="\n")
        print(f"  wrote {dst.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
