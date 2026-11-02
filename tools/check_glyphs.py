#!/usr/bin/env python3
"""
check_glyphs.py - check the glyph sets (PLAN 7.5) on the computer, before the Thor.

Builds tools/glyphs_host.cc with app/src/main/cpp/warden_glyphs.cc, runs it on the atlas
descriptions of the disc's guiexport.erf for each set, and draws one sheet per set:
every redirected image as the game would then draw it - its rectangle on its page by
the patched descriptions - beside what it is on the disc. Decoding the pages takes
tools/dao_xds.py and Pillow. Everything it writes is under work/glyphs/ (git-ignored).

Usage:
  python tools/check_glyphs.py [--iso <disc image>]

The disc image defaults to the one in the repository root. Its guiexport.erf is
extracted to work/glyphs/ the first time.
"""
import argparse
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import dao_erf  # noqa: E402
import dao_iso  # noqa: E402
import dao_xds  # noqa: E402

WORK = ROOT / "work" / "glyphs"
BUILD = ROOT / "build" / "glyphs-check"
WINSYSROOT = ROOT / "toolchain" / "winsysroot"
CPP = ROOT / "app" / "src" / "main" / "cpp"
SETS = ("nintendo", "playstation")


def host_compiler():
    candidate = pathlib.Path(os.environ.get("ProgramFiles", r"C:\Program Files"))
    candidate = candidate / "LLVM" / "bin" / ("clang-cl.exe" if os.name == "nt" else "clang++")
    if candidate.is_file():
        return candidate
    import shutil
    found = shutil.which("clang-cl") or shutil.which("clang++")
    return pathlib.Path(found) if found else None


def build():
    compiler = host_compiler()
    if not compiler:
        raise SystemExit("error: no clang-cl or clang++ found; see docs/TOOLCHAIN.md")
    BUILD.mkdir(parents=True, exist_ok=True)
    exe = BUILD / ("glyphs_host.exe" if os.name == "nt" else "glyphs_host")
    sources = [CPP / "warden_glyphs.cc", ROOT / "tools" / "glyphs_host.cc"]
    if compiler.name.startswith("clang-cl"):
        args = [str(compiler), "/std:c++20", "/O2", "/EHsc", "/MT", "/W3",
                "-winsysroot", str(WINSYSROOT), f"/I{CPP}", f"/Fe{exe}", f"/Fo{BUILD}\\"]
    else:
        args = [str(compiler), "-std=c++20", "-O2", f"-I{CPP}", "-o", str(exe)]
    args += [str(s) for s in sources]
    result = subprocess.run(args, cwd=BUILD, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout[-4000:], result.stderr[-4000:], file=sys.stderr)
        raise SystemExit("error: glyphs_host failed to build")
    return exe


def extract(iso):
    erf_path = WORK / "guiexport.erf"
    if not erf_path.is_file():
        WORK.mkdir(parents=True, exist_ok=True)
        data = dao_iso.Xdvdfs(str(iso)).read_file("packages/core/data/guiexport.erf")
        with open(erf_path, "wb") as out:
            out.write(data)
    files = WORK / "guiexport"
    if not (files / "atl_shared_dxt5_dat.xml").is_file():
        files.mkdir(parents=True, exist_ok=True)
        erf = dao_erf.Erf(str(erf_path))
        for entry in erf.entries:
            with open(files / entry[0], "wb") as out:
                out.write(erf.read(entry))
    return files


def sheet(set_name, files, patched, report):
    from PIL import Image, ImageDraw, ImageFont
    pages_after = {}
    for xml in files.glob("atl_*_dat.xml"):
        source = patched / xml.name if (patched / xml.name).is_file() else xml
        for page, cells in dao_xds.atlas_pages(str(source)).items():
            pages_after.setdefault(page, []).extend(cells)
    before = {}
    for xml in files.glob("atl_*_dat.xml"):
        for page, cells in dao_xds.atlas_pages(str(xml)).items():
            for cell in cells:
                before.setdefault(cell[0].lower(), (page,) + cell)
    after = {}
    for page, cells in pages_after.items():
        for cell in cells:
            after.setdefault(cell[0].lower(), []).append((page,) + cell)
    images = {}

    def crop(page, x, y, w, h):
        if page not in images:
            xds = files / (page + ".xds")
            images[page] = dao_xds.decode(str(xds))[0] if xds.is_file() else None
        im = images[page]
        if im is None:
            return None
        bg = Image.new("RGBA", (w, h), (34, 34, 40, 255))
        bg.alpha_composite(im.crop((x, y, x + w, y + h)))
        return bg.convert("RGB")

    names = []
    for line in report.splitlines():
        line = line.strip()
        if " <- " in line and ": no such" not in line:
            names.append(line.split(" ")[0])
    names = list(dict.fromkeys(names))
    font = ImageFont.truetype("arial.ttf", 11) if os.name == "nt" else ImageFont.load_default()
    cw, ch, cols = 260, 70, 4
    rows = (len(names) + cols - 1) // cols
    out = Image.new("RGB", (cw * cols, ch * max(rows, 1)), (34, 34, 40))
    draw = ImageDraw.Draw(out)
    for i, name in enumerate(names):
        cx, cy = (i % cols) * cw, (i // cols) * ch
        b = before.get(name.lower())
        if b:
            im = crop(b[0], b[2], b[3], b[4], b[5])
            if im:
                out.paste(im, (cx + 4, cy + 4))
        for k, a in enumerate(after.get(name.lower(), [])[:2]):
            im = crop(a[0], a[2], a[3], a[4], a[5])
            if im:
                out.paste(im, (cx + 70 + k * 60, cy + 4))
        draw.text((cx + 4, cy + ch - 16), name.replace(".dds", ""), fill=(230, 220, 160), font=font)
    path = WORK / ("check_%s.png" % set_name)
    out.save(path)
    return path


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--iso", default=str(ROOT / "Dragon Age - Origins (World) (En,Es,It).iso"))
    a = ap.parse_args()
    files = extract(a.iso)
    exe = build()
    for set_name in SETS:
        out = WORK / ("patched_" + set_name)
        result = subprocess.run([str(exe), str(files), set_name, str(out)],
                                capture_output=True, text=True)
        print(result.stdout, end="")
        if result.returncode != 0:
            print(result.stderr, file=sys.stderr)
            return 1
        print("  sheet: %s (left: the disc's image; right: what it shows now)" %
              sheet(set_name, files, out, result.stdout))
    return 0


if __name__ == "__main__":
    sys.exit(main())
