#!/usr/bin/env python3
"""
dao_gfx.py - read the Xbox 360 build's Scaleform movies (.gfx, guiexport.erf).

A .gfx file is a Flash movie: "CFX" (zlib-compressed after the 8-byte header) or "GFX",
with Scaleform's own tags beside Flash's - external images (the atlas cells, named
<movie>_I<id>.dds) and sub-images among them. The UI's logic is ActionScript 2 in
DoAction and DoInitAction tags; `as` disassembles it, enough to follow a value.

Usage:
  python tools/dao_gfx.py tags    <gfx>                 every tag, sprites' nested
  python tools/dao_gfx.py images  <gfx>                 external images and sub-images
  python tools/dao_gfx.py exports <gfx>                 exported symbol names
  python tools/dao_gfx.py as      <gfx> [--find TEXT]   disassembly of the action blocks
                                                        (only those containing TEXT)
"""
import argparse
import struct
import sys
import zlib

TAG_NAMES = {
    0: "End", 1: "ShowFrame", 2: "DefineShape", 9: "SetBackgroundColor", 11: "DefineText",
    12: "DoAction", 22: "DefineShape2", 26: "PlaceObject2", 28: "RemoveObject2",
    32: "DefineShape3", 33: "DefineText2", 34: "DefineButton2", 37: "DefineEditText",
    39: "DefineSprite", 43: "FrameLabel", 46: "DefineMorphShape", 48: "DefineFont2",
    56: "ExportAssets",
    57: "ImportAssets", 59: "DoInitAction", 69: "FileAttributes", 70: "PlaceObject3",
    71: "ImportAssets2", 73: "DefineFontAlignZones", 74: "CSMTextSettings", 75: "DefineFont3",
    77: "Metadata", 78: "DefineScalingGrid", 83: "DefineShape4", 84: "DefineMorphShape2",
    86: "DefineSceneAndFrameLabelData", 88: "DefineFontName",
    1000: "GFx_ExporterInfo", 1001: "GFx_DefineExternalImage", 1002: "GFx_FontTextureInfo",
    1003: "GFx_DefineExternalGradient", 1004: "GFx_DefineGradientMap",
    1005: "GFx_DefineCompactedFont", 1006: "GFx_DefineExternalSound",
    1007: "GFx_DefineExternalStreamSound", 1008: "GFx_DefineSubImage",
    1009: "GFx_DefineExternalImage2",
}


def load(path):
    """The movie's bytes after the header's RECT, frame rate and count: its tags."""
    data = open(path, "rb").read()
    sig = data[:3]
    if sig in (b"CFX", b"CWS"):
        data = data[:8] + zlib.decompress(data[8:])
    elif sig not in (b"GFX", b"FWS"):
        raise ValueError("%s: not a Flash or Scaleform movie (%r)" % (path, sig))
    nbits = data[8] >> 3
    rect_bytes = (5 + 4 * nbits + 7) // 8
    return data, 8 + rect_bytes + 4


def tags(data, pos, end=None, depth=0):
    """(code, depth, offset of the body, body) for every tag, a sprite's inner ones after it."""
    end = len(data) if end is None else min(end, len(data))
    while pos + 2 <= end:
        head = struct.unpack_from("<H", data, pos)[0]
        code, length = head >> 6, head & 0x3F
        pos += 2
        if length == 0x3F:
            if pos + 4 > end:
                return
            length = struct.unpack_from("<I", data, pos)[0]
            pos += 4
        if pos + length > end:
            # Longer than what holds it: a record this reader misjudges; this level ends.
            return
        body = data[pos:pos + length]
        yield code, depth, pos, body
        if code == 39:  # DefineSprite: id, frame count, then its own tags
            yield from tags(data, pos + 4, pos + length, depth + 1)
        pos += length
        if code == 0:
            # The end of a sprite's tags, or the movie's: what follows the movie's End is
            # not tags.
            return


def cstring(b, p):
    """A NUL-terminated string at `p`, and where the next field starts; one running to
    the end of the record without its NUL is taken whole."""
    e = b.find(b"\0", p)
    if e < 0:
        return b[p:].decode("latin-1"), len(b)
    return b[p:e].decode("latin-1"), e + 1


def pstring(b, p):
    n = b[p]
    return b[p + 1:p + 1 + n].decode("latin-1"), p + 1 + n


def describe_image(code, body):
    if code == 1001:
        # Id, format, size, then the export name and the file name (atlas cell).
        cid, fmt, w, h = struct.unpack_from("<HHHH", body, 0)
        export, p = pstring(body, 8)
        name, _ = pstring(body, p)
        return "image %d: %s (%dx%d, format %d%s)" % (
            cid, name, w, h, fmt, ", export %r" % export if export else "")
    if code == 1009:
        cid, fmt, w, h = struct.unpack_from("<IHHH", body, 0)
        export, p = pstring(body, 10)
        name, _ = pstring(body, p)
        return "image %d: %s (%dx%d, format %d, export %r)" % (cid, name, w, h, fmt, export)
    if code == 1008:
        cid, image, x1, y1, x2, y2 = struct.unpack_from("<HHHHHH", body, 0)
        return "sub-image %d of image %d: (%d,%d)-(%d,%d)" % (cid, image, x1, y1, x2, y2)
    return None


# ---- ActionScript 2 ----

OPS = {
    0x04: "NextFrame", 0x05: "PrevFrame", 0x06: "Play", 0x07: "Stop", 0x0A: "Add",
    0x0B: "Subtract", 0x0C: "Multiply", 0x0D: "Divide", 0x0E: "Equals", 0x0F: "Less",
    0x10: "And", 0x11: "Or", 0x12: "Not", 0x13: "StringEquals", 0x14: "StringLength",
    0x17: "Pop", 0x18: "ToInteger", 0x1C: "GetVariable", 0x1D: "SetVariable",
    0x20: "SetTarget2", 0x21: "StringAdd", 0x22: "GetProperty", 0x23: "SetProperty",
    0x24: "CloneSprite", 0x25: "RemoveSprite", 0x26: "Trace", 0x2B: "CastOp",
    0x2C: "ImplementsOp", 0x34: "GetTime", 0x3A: "Delete", 0x3B: "Delete2",
    0x3C: "DefineLocal", 0x3D: "CallFunction", 0x3E: "Return", 0x3F: "Modulo",
    0x40: "NewObject", 0x41: "DefineLocal2", 0x42: "InitArray", 0x43: "InitObject",
    0x44: "TypeOf", 0x45: "TargetPath", 0x46: "Enumerate", 0x47: "Add2", 0x48: "Less2",
    0x49: "Equals2", 0x4A: "ToNumber", 0x4B: "ToString", 0x4C: "PushDuplicate",
    0x4D: "StackSwap", 0x4E: "GetMember", 0x4F: "SetMember", 0x50: "Increment",
    0x51: "Decrement", 0x52: "CallMethod", 0x53: "NewMethod", 0x54: "InstanceOf",
    0x55: "Enumerate2", 0x60: "BitAnd", 0x61: "BitOr", 0x62: "BitXor", 0x63: "BitLShift",
    0x64: "BitRShift", 0x65: "BitURShift", 0x66: "StrictEquals", 0x67: "Greater",
    0x68: "StringGreater", 0x69: "Extends", 0x81: "GotoFrame", 0x83: "GetURL",
    0x87: "StoreRegister", 0x88: "ConstantPool", 0x8A: "WaitForFrame", 0x8B: "SetTarget",
    0x8C: "GoToLabel", 0x8D: "WaitForFrame2", 0x8E: "DefineFunction2", 0x8F: "Try",
    0x94: "With", 0x96: "Push", 0x99: "Jump", 0x9A: "GetURL2", 0x9B: "DefineFunction",
    0x9D: "If", 0x9E: "Call", 0x9F: "GotoFrame2",
}


def push_values(body, pool):
    out, p = [], 0
    while p < len(body):
        t = body[p]
        p += 1
        if t == 0:
            s, p = cstring(body, p)
            out.append(repr(s))
        elif t == 1:
            out.append("%g" % struct.unpack_from("<f", body, p)[0])
            p += 4
        elif t == 2:
            out.append("null")
        elif t == 3:
            out.append("undefined")
        elif t == 4:
            out.append("r%d" % body[p])
            p += 1
        elif t == 5:
            out.append("true" if body[p] else "false")
            p += 1
        elif t == 6:
            hi, lo = struct.unpack_from("<II", body, p)
            out.append("%g" % struct.unpack("<d", struct.pack("<II", lo, hi))[0])
            p += 8
        elif t == 7:
            out.append("%d" % struct.unpack_from("<i", body, p)[0])
            p += 4
        elif t == 8:
            i = body[p]
            out.append(repr(pool[i]) if i < len(pool) else "pool[%d]" % i)
            p += 1
        elif t == 9:
            i = struct.unpack_from("<H", body, p)[0]
            out.append(repr(pool[i]) if i < len(pool) else "pool[%d]" % i)
            p += 2
        else:
            out.append("?type%d" % t)
            break
    return ", ".join(out)


def disassemble(actions, base=0, indent="", pool=None):
    """Lines of the action records in `actions`, a DefineFunction's body indented under
    it. A block ends at its End action; a function's body, which has none, at its size,
    and it uses the enclosing block's constant pool until it sets its own."""
    top = pool is None
    pool = [] if top else list(pool)
    lines, p = [], 0
    while p < len(actions):
        at = p
        code = actions[p]
        p += 1
        if code == 0 and top:
            lines.append("%s%05X  End" % (indent, base + at))
            break
        body = b""
        if code >= 0x80:
            length = struct.unpack_from("<H", actions, p)[0]
            p += 2
            body = actions[p:p + length]
            p += length
        name = OPS.get(code, "op_%02X" % code)
        arg = ""
        if code == 0x88:
            count = struct.unpack_from("<H", body, 0)[0]
            q, pool = 2, []
            for _ in range(count):
                s, q = cstring(body, q)
                pool.append(s)
            arg = "%d strings" % count
        elif code == 0x96:
            arg = push_values(body, pool)
        elif code in (0x99, 0x9D):
            off = struct.unpack_from("<h", body, 0)[0]
            arg = "-> %05X" % (base + p + off)
        elif code == 0x87:
            arg = "r%d" % body[0]
        elif code == 0x8C:
            arg = cstring(body, 0)[0]
        elif code in (0x8E, 0x9B):
            fname, q = cstring(body, 0)
            nparams = struct.unpack_from("<H", body, q)[0]
            q += 2
            params = []
            if code == 0x8E:
                q += 3  # register count, flags
                for _ in range(nparams):
                    reg = body[q]
                    pname, q = cstring(body, q + 1)
                    params.append("r%d=%s" % (reg, pname) if reg else pname)
            else:
                for _ in range(nparams):
                    pname, q = cstring(body, q)
                    params.append(pname)
            size = struct.unpack_from("<H", body, q)[0]
            lines.append("%s%05X  %s function %s(%s), %d bytes" % (
                indent, base + at, name, fname or "<anonymous>", ", ".join(params), size))
            # The function's body follows the record.
            lines.extend(disassemble(actions[p:p + size], base + p, indent + "    ", pool))
            p += size
            continue
        lines.append("%s%05X  %s %s" % (indent, base + at, name, arg))
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=("tags", "images", "exports", "as"))
    ap.add_argument("gfx")
    ap.add_argument("--find", default="")
    a = ap.parse_args()
    data, start = load(a.gfx)
    for code, depth, offset, body in tags(data, start):
        name = TAG_NAMES.get(code, "tag%d" % code)
        if a.cmd == "tags":
            extra = ""
            if code == 39:
                extra = " id %d" % struct.unpack_from("<H", body, 0)[0]
            elif code == 59:
                extra = " sprite %d" % struct.unpack_from("<H", body, 0)[0]
            print("%s%-28s %6d bytes @%06X%s" % ("  " * depth, name, len(body), offset, extra))
        elif a.cmd == "images":
            text = describe_image(code, body)
            if text:
                print(text)
        elif a.cmd == "exports" and code == 56:
            count = struct.unpack_from("<H", body, 0)[0]
            p = 2
            for _ in range(count):
                cid = struct.unpack_from("<H", body, p)[0]
                sym, p = cstring(body, p + 2)
                print("%5d  %s" % (cid, sym))
        elif a.cmd == "as" and code in (12, 59):
            actions = body if code == 12 else body[2:]
            if a.find and a.find.encode() not in actions:
                continue
            head = "sprite %d" % struct.unpack_from("<H", body, 0)[0] if code == 59 else "frame"
            print("==== %s %s @%06X, %d bytes" % (name, head, offset, len(body)))
            try:
                for line in disassemble(actions, offset + (0 if code == 12 else 2)):
                    print(line)
            except (struct.error, IndexError) as e:
                # A record this reader misjudges: say so, and go on with the next block.
                print("     (the rest of this block not decoded: %s)" % e)


if __name__ == "__main__":
    sys.exit(main())
