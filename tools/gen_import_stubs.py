#!/usr/bin/env python3
"""
gen_import_stubs.py - emit weak stubs for every kernel import the title needs.

Task 5.1. The recompiled executable calls into the Xbox 360 kernel, XAM, the video
driver and the audio decoder - 251 distinct imports for Dragon Age. Until each one has
a real implementation, something has to resolve the symbol or the library does not
link at all, and nothing can be learned about which of them actually matter.

The list is derived from the recompiled source, not from link errors. XenonRecomp
declares every import in ppc_recomp_shared.h as `PPC_EXTERN_FUNC(__imp__<Name>);`, so
scanning that is deterministic and needs no build. Scraping the linker would be
circular, and worse, lld's output is capped and order-dependent.

Every stub is **weak**, which is what makes this a scaffold rather than a wall: a
hand-written strong definition of the same symbol wins at link time, with no list to
keep in step. Implementing an import is simply writing it in warden_imports.cc.

Each stub logs its first call and counts the rest, so a boot attempt produces an
ordered list of exactly which imports the game reaches and how often - which is the
work list for making it boot, rather than a guess at one.

**A stub fails; it does not succeed.** Stubs used to leave r3 at 0. For an import that
returns an NTSTATUS that is STATUS_SUCCESS, so the title carried on as though the call
had worked and used outputs nobody wrote - which caused both of the first two startup
crashes (BUG-030). So each stub now returns the failure value of the family its import
belongs to, and the title takes its own error path instead:

  NTSTATUS                STATUS_NOT_IMPLEMENTED   0xC0000002
  Win32 error (XAM)       ERROR_FUNCTION_FAILED    0x0000065B
  HRESULT                 E_NOTIMPL                0x80004001
  pointer, handle, value  0 - already null or none, which is the failure
  void                    nothing to return

Which family an import belongs to is read from Xenia's own kernel - the result type of
its `<Name>_entry` function, and for a plain dword the status constants it returns -
rather than guessed from the name: `Xam*` mostly return Win32 errors, `XGetGameRegion`
returns a value, `RtlInitAnsiString` returns nothing. An import Xenia does not
implement falls back to "Nt* returns an NTSTATUS, anything else a value", and `--list`
says which imports that applied to.

Usage:
  python tools/gen_import_stubs.py
  python tools/gen_import_stubs.py --list        # the imports, each with its family
"""
import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
GUEST_BUILD = "ppc_tu6"
SHARED_HEADER = ROOT / "recomp" / "generated" / GUEST_BUILD / "ppc_recomp_shared.h"
DEFAULT_OUT = ROOT / "build" / "gen" / "guest_import_stubs.cc"
# Xenia's kernel - xboxkrnl, xam, xbdm - pinned with the rest of xenia-canary
# (THIRD_PARTY.md). Read only for the result type of each export.
XENIA_KERNEL = ROOT / "third_party" / "xenia-canary" / "src" / "xenia" / "kernel"

# What a stub hands back, by family: (value, name for the log).
FAILURE = {
    "ntstatus": ("0xC0000002u", "STATUS_NOT_IMPLEMENTED"),
    "win32": ("0x0000065Bu", "ERROR_FUNCTION_FAILED"),
    "hresult": ("0x80004001u", "E_NOTIMPL"),
    "pointer": ("0", "null"),
    "value": ("0", "0"),
    "void": ("0", "nothing (void)"),
    # Winsock: SOCKET_ERROR, INVALID_SOCKET and WSA_WAIT_FAILED are all ~0. A 0 from a
    # socket call is success - a connected socket, event 0 signalled - and sends the
    # title's network thread round its loop forever (BUG-035). The exceptions - the
    # BOOL-returning event calls, the XNet status queries - are implemented in
    # warden_net.cc and warden_threads.cc, where the stub is overridden anyway.
    "net": ("0xFFFFFFFFu", "SOCKET_ERROR"),
}

# Corrections where Xenia's code cannot say: import name -> family, each with how it
# was established.
OVERRIDES = {
    # NTSTATUS, as on Windows; Xenia's implementations return a bare 0, which reads
    # as a value.
    "RtlMultiByteToUnicodeN": "ntstatus",
    "RtlUnicodeToMultiByteN": "ntstatus",
}

RETURN = re.compile(r"\breturn\s+([^;]+);")
STATUS_TOKENS = (("X_STATUS_", "ntstatus"), ("X_ERROR_", "win32"), ("X_E_", "hresult"))
TYPE_FAMILY = {"X_STATUS": "ntstatus", "X_RESULT": "win32", "X_HRESULT": "hresult"}

# `PPC_EXTERN_FUNC(__imp__KeQuerySystemTime);`. The guest's own functions are declared
# the same way but as tu6_sub_<address> / sub_<address>, which is how the two are told
# apart - an import is anything that is not a translated address.
DECLARATION = re.compile(r"^PPC_EXTERN_FUNC\((__imp__(\w+))\);", re.M)
GUEST_FUNCTION = re.compile(r"^(tu6_)?sub_[0-9A-Fa-f]{8}$")

# Rough grouping, for the report and for docs/IMPORTS.md. Order matters: the first
# match wins, so the longer prefixes come first.
GROUPS = [
    ("XMA", "XMA audio decoder"),
    ("XAudio", "XAudio2"),
    ("XNotify", "notification listener"),
    ("XGet", "system info"),
    ("XInput", "controllers"),
    ("Xam", "XAM - user, content, UI"),
    ("Net", "networking"),
    ("Nt", "NT syscalls - files, events, threads"),
    ("Ke", "kernel - threads, sync, timing"),
    ("Ex", "executive"),
    ("Rtl", "runtime library - strings, locks"),
    ("Vd", "video driver"),
    ("Mm", "memory manager"),
    ("Ob", "object manager"),
    ("Io", "I/O manager"),
    ("Hal", "hardware abstraction"),
    ("Dbg", "debug output"),
    ("Interlocked", "interlocked lists"),
    ("Xe", "misc Xenon"),
]


def group_of(name):
    bare = name.lstrip("_")
    for prefix, description in GROUPS:
        if bare.startswith(prefix):
            return prefix, description
    return "other", "uncategorised"


def read_imports():
    if not SHARED_HEADER.is_file():
        raise SystemExit(
            f"error: {SHARED_HEADER} not found.\n"
            f"       The title has not been recompiled - see tools/recompile.py.")
    text = SHARED_HEADER.read_text(encoding="utf-8", errors="replace")
    names = []
    for symbol, bare in DECLARATION.findall(text):
        if GUEST_FUNCTION.match(bare):
            continue
        names.append(symbol)
    return sorted(set(names))


# Any function defined at the top level of a kernel source file - exports return
# through `xe*` helpers and through plain ones alike (NtClose_entry returns NtClose(),
# ExCreateThread_entry returns ExCreateThread()), so the follow has to know them all.
FUNCTION_DEF = re.compile(
    r"^(?:static\s+|inline\s+)*([A-Za-z_][\w:<>]*)\s+(\w+)\s*\(", re.M)
TYPED_LOCAL = re.compile(r"\b(X_STATUS|X_RESULT|X_HRESULT)\s+(\w+)\s*[=;]")
# Functions and methods declared to return a status, declarations included - an
# export can return through a method (NtClose -> object_table()->ReleaseHandle()).
STATUS_DECL = re.compile(
    r"^\s*(?:virtual\s+|static\s+)*(X_STATUS|X_RESULT|X_HRESULT)\s+(?:\w+::)?(\w+)\s*\(",
    re.M)
# The outermost call a return expression ends in: `ReleaseHandle` in
# `kernel_state()->object_table()->ReleaseHandle(handle)`.
OUTER_CALL = re.compile(r"(\w+)\s*\((?:[^()]|\([^()]*\))*\)\s*$")
# Exports that return through another (NtOpenFile through NtCreateFile_entry,
# MmQueryStatistics through xeMmQueryStatistics) are followed this many calls deep.
MAX_DEPTH = 3


def body_after(text, start):
    """The brace-balanced body of the function whose signature begins at `start`, or
    "" if it is only a declaration - a `;` before the first `{` - as in a header.

    A plain counter: braces inside string literals or comments would throw it, and
    Xenia's export bodies do not have those in a way that matters here.
    """
    open_at = text.find("{", start)
    semicolon = text.find(";", start)
    if open_at < 0 or 0 <= semicolon < open_at:
        return ""
    depth = 0
    for i in range(open_at, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[open_at:i + 1]
    return text[open_at:]


def load_xenia():
    """Every function definition an export's family can depend on, from Xenia's kernel:
    `<Name>_entry` and `xe*` helpers, as {function name: (result type, body, where)}.
    Declarations are skipped - only a definition has a body to read."""
    if not XENIA_KERNEL.is_dir():
        raise SystemExit(
            f"error: {XENIA_KERNEL} not found.\n"
            f"       Fetch xenia-canary at its pinned commit - see THIRD_PARTY.md.")
    functions = {}
    status_names = {}
    for path in sorted(XENIA_KERNEL.rglob("*")):
        if path.suffix not in (".cc", ".h"):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for match in FUNCTION_DEF.finditer(text):
            name = match.group(2)
            body = body_after(text, match.end())
            if not body or name in functions:
                continue
            line = text.count("\n", 0, match.start()) + 1
            where = f"{path.relative_to(XENIA_KERNEL).as_posix()}:{line}"
            functions[name] = (match.group(1), body, where)
        for match in STATUS_DECL.finditer(text):
            status_names.setdefault(match.group(2), TYPE_FAMILY[match.group(1)])
    return functions, status_names


def family_of(result_type, body, xenia, depth=0):
    """An export's family, from its result type and, for a plain dword, what it returns.

    A dword result is a status only if what the function returns is one. Each `return`
    is looked at: a status constant names the family; so does a local declared as, or
    assigned, one; so does a call to anything declared as returning a status; and any
    other returned call is followed into the function it calls - a helper or another
    export - up to MAX_DEPTH deep. Anything else - a handle, a count, a region - is a
    value.
    """
    functions, status_names = xenia
    if result_type in ("void",):
        return "void"
    if result_type == "pointer_result_t":
        return "pointer"
    if result_type == "X_HRESULT_result_t" or result_type == "X_HRESULT":
        return "hresult"
    if result_type in TYPE_FAMILY:
        return TYPE_FAMILY[result_type]
    if result_type not in ("dword_result_t", "uint32_t"):
        return "value"

    def from_expression(expression):
        for token, family in STATUS_TOKENS:
            if token in expression:
                return family
        call = OUTER_CALL.search(expression)
        if call and call.group(1) in status_names:
            return status_names[call.group(1)]
        if call and call.group(1) in functions and depth < MAX_DEPTH:
            callee_type, callee_body, _ = functions[call.group(1)]
            family = family_of(callee_type, callee_body, xenia, depth + 1)
            if family != "value":
                return family
        return None

    declared = {m.group(2): TYPE_FAMILY[m.group(1)] for m in TYPED_LOCAL.finditer(body)}
    for expression in RETURN.findall(body):
        family = from_expression(expression)
        if family:
            return family
        identifier = expression.strip()
        if identifier in declared:
            return declared[identifier]
        # `auto result = xeXamEnumerate(...); return result;` - judge the variable by
        # what was assigned to it.
        if re.fullmatch(r"\w+", identifier):
            assigned = re.search(rf"\b{identifier}\s*=\s*([^;]+);", body)
            if assigned:
                family = from_expression(assigned.group(1))
                if family:
                    return family
    return "value"


def classify(bare, xenia):
    """(family, where it was established) for one import."""
    if bare in OVERRIDES:
        return OVERRIDES[bare], "override in gen_import_stubs.py"
    found = xenia[0].get(bare + "_entry")
    if not found:
        if bare.startswith("NetDll_"):
            return "net", "not in Xenia - Winsock, by name"
        family = "ntstatus" if bare.startswith("Nt") else "value"
        return family, "not in Xenia - by name"
    result_type, body, where = found
    family = family_of(result_type, body, xenia)
    if family == "value" and bare.startswith("NetDll_"):
        # Xenia returns 0 or a socket here; for a stub that is a lie (see FAILURE).
        return "net", where + " - Winsock, so SOCKET_ERROR"
    return family, where


def classify_all(names):
    xenia = load_xenia()
    return {name: classify(name[len("__imp__"):], xenia) for name in names}


HEADER = """// Generated by tools/gen_import_stubs.py - do not edit.
//
// One weak stub per kernel import the recompiled title references. Weak, so that a
// hand-written definition in warden_imports.cc overrides it at link time without any
// list needing to be kept in step.
//
// A stub logs its first call, counts every call, and returns its import's *failure*
// value - STATUS_NOT_IMPLEMENTED for an NTSTATUS, ERROR_FUNCTION_FAILED for a Win32
// error, E_NOTIMPL for an HRESULT, 0 for a pointer or value. Never success: a stub
// that claims to have worked sends the title on with outputs nobody wrote (BUG-030).
// The family of each import is read from Xenia's kernel and noted above its stub.
#include <atomic>

#include "ppc_recomp_shared.h"
#include "warden_imports.h"

namespace warden {

const char* const kImportNames[] = {
"""


def generate(names, conventions):
    out = [HEADER]
    for name in names:
        out.append(f'    "{name[len("__imp__"):]}",\n')
    out.append("};\n")
    out.append(f"const size_t kImportCount = {len(names)};\n\n")
    out.append("// What each stub returns, for the log - indexed as kImportNames is.\n")
    out.append("const char* const kImportFailures[] = {\n")
    for name in names:
        out.append(f'    "{FAILURE[conventions[name][0]][1]}",\n')
    out.append("};\n\n")
    out.append("}  // namespace warden\n\n")

    for index, name in enumerate(names):
        family, where = conventions[name]
        value, _ = FAILURE[family]
        out.append(f"// {family}, per {where}\n")
        out.append(f"__attribute__((weak)) PPC_FUNC({name}) {{\n")
        out.append("  (void)base;\n")
        out.append(f"  warden::NoteImportCall({index});\n")
        out.append(f"  ctx.r3.u64 = {value};\n")
        out.append("}\n\n")
    return "".join(out)


def main():
    ap = argparse.ArgumentParser(description="Emit weak kernel import stubs")
    ap.add_argument("--out", default=str(DEFAULT_OUT))
    ap.add_argument("--list", action="store_true", help="print the imports and exit")
    a = ap.parse_args()

    names = read_imports()
    conventions = classify_all(names)
    if a.list:
        for name in names:
            bare = name[len("__imp__"):]
            prefix, _ = group_of(bare)
            family, where = conventions[name]
            print(f"{prefix:<12} {bare:<40} {family:<9} {where}")
        families = {}
        for family, _ in conventions.values():
            families[family] = families.get(family, 0) + 1
        by_name = sum(1 for _, where in conventions.values() if where.startswith("not in"))
        print(f"\n{len(names)} imports: " +
              ", ".join(f"{f} {n}" for f, n in sorted(families.items(), key=lambda kv: -kv[1])) +
              f"; {by_name} not in Xenia, classified by name", file=sys.stderr)
        return 0

    text = generate(names, conventions)
    out = pathlib.Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.is_file() and out.read_text(encoding="utf-8") == text:
        print(f"  unchanged: {out.relative_to(ROOT)} ({len(names)} imports)")
        return 0
    out.write_text(text, encoding="utf-8", newline="\n")

    counts = {}
    for name in names:
        prefix, _ = group_of(name[len("__imp__"):])
        counts[prefix] = counts.get(prefix, 0) + 1
    summary = ", ".join(f"{prefix} {count}"
                        for prefix, count in sorted(counts.items(),
                                                    key=lambda kv: -kv[1]))
    print(f"  wrote {out.relative_to(ROOT)}: {len(names)} weak stubs")
    print(f"  {summary}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
