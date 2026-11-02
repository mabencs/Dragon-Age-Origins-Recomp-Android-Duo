# PLAN 10.11: contact sheets of the tour's screenshots - up to 3 areas a sheet, the six
# shots of each in a row (arrival, walk1, walk2, turn1, turn2, walk3) at 426x240.
#   python sheets.py <label> [<label> ...]   -> build/areas/sheet-<first>.png
import sys
from pathlib import Path
from PIL import Image, ImageDraw

labels = sys.argv[1:]
names = ["arrival", "walk1", "walk2", "turn1", "turn2", "walk3"]
W, H = 426, 240
sheet = Image.new("RGB", (W * 6, (H + 16) * len(labels)), "black")
d = ImageDraw.Draw(sheet)
for row, label in enumerate(labels):
    y = row * (H + 16)
    d.text((4, y + 2), label, fill="white")
    for col, n in enumerate(names):
        f = Path(f"build/areas/{label}-{n}.png")
        if f.exists():
            try:
                sheet.paste(Image.open(f).convert("RGB").resize((W, H)), (col * W, y + 16))
            except Exception as e:  # a truncated screenshot (the computer crashed mid-write)
                print("skipped", f, e)
out = f"build/areas/sheet-{labels[0]}.png"
sheet.save(out)
print(out, sheet.size)
