"""resolve_to_png.py - turn a resolve dump (files/resolve-<n>-<address>.raw, written by the
resolve-dump experiment, docs/TESTING.md) into PNGs.

    python tools/resolve_to_png.py <dump.raw> [width] [--linear] [--endian N]

The dump holds a resolve's destination bytes in the guest layout: 32 bits per pixel,
tiled in the Xenos 2D layout (32x32 macro tiles, the pitch aligned to 32) unless
--linear. A k_8_8_8_8 colour resolve (dest endian 2, as this title's are) reads B G R A
byte by byte in the dump - checked against the loading screen's front buffer - so no
swap is applied by default; --endian N applies the copy's swap (1 8-in-16, 2 8-in-32,
3 16-in-32) first. Width defaults to 1280; the height follows from the length. Writes
<dump>.png (the colour as stored, alpha ignored) and <dump>-alpha.png (the fourth byte as
grey) next to the dump - for a depth/stencil resolve the colour image shows the 24-bit
depth's bytes and the alpha image the stencil (or the depth's low byte, depending on
the packing). Needs Pillow.
"""
import sys

from PIL import Image


def tiled_offset(x, y, pitch_aligned, bytes_per_block_log2=2):
    # texture_address::Tiled2D and TiledCombine (xenia/gpu/texture_address.h).
    outer_blocks = ((y >> 5) * (pitch_aligned >> 5) + (x >> 5)) << 6
    inner_blocks = (((y >> 1) & 0b111) << 3) | (x & 0b111)
    outer_inner = (outer_blocks | inner_blocks) << bytes_per_block_log2
    bank = (y >> 4) & 1
    pipe = ((x >> 3) & 0b11) ^ (((y >> 3) & 1) << 1)
    return (((y & 1) << 4) | (pipe << 6) | (bank << 11) | (outer_inner & 0b1111)
            | (((outer_inner >> 4) & 1) << 5) | (((outer_inner >> 5) & 0b111) << 8)
            | (outer_inner >> 8 << 12))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    linear = '--linear' in sys.argv
    endian = 0
    if '--endian' in sys.argv:
        endian = int(sys.argv[sys.argv.index('--endian') + 1])
        args = [a for a in args if a != str(endian)]
    path = args[0]
    width = int(args[1]) if len(args) > 1 else 1280
    data = open(path, 'rb').read()
    pitch = (width + 31) & ~31
    height = len(data) // (pitch * 4)
    colour = Image.new('RGB', (width, height))
    alpha = Image.new('L', (width, height))
    cpix = colour.load()
    apix = alpha.load()
    for y in range(height):
        for x in range(width):
            o = (y * pitch + x) * 4 if linear else tiled_offset(x, y, pitch)
            if o + 4 > len(data):
                continue
            b = data[o:o + 4]
            if endian == 2:  # 8-in-32
                b = b[::-1]
            elif endian == 1:  # 8-in-16
                b = bytes((b[1], b[0], b[3], b[2]))
            elif endian == 3:  # 16-in-32
                b = b[2:4] + b[0:2]
            cpix[x, y] = (b[2], b[1], b[0])
            apix[x, y] = b[3]
    base = path[:-4] if path.endswith('.raw') else path
    colour.save(base + '.png')
    alpha.save(base + '-alpha.png')
    print(f'{path}: {width}x{height} -> {base}.png, {base}-alpha.png')


if __name__ == '__main__':
    main()
