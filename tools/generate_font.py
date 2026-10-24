#!/usr/bin/env python3
"""Generate small anti-aliased Inter raster glyphs for the NOVA framebuffer.

The build depends on the distribution's fonts-inter and python3-pil packages.
Inter typeface copyright belongs to its creators and is licensed under OFL-1.1.
Only bitmap samples are generated, not redistributed font files.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import glob

candidates = (
    glob.glob("/usr/share/fonts/opentype/inter/Inter-Regular.otf")
    + glob.glob("/usr/share/fonts/opentype/inter/InterDisplay-Regular.otf")
    + glob.glob("/usr/share/fonts/truetype/inter/Inter-Regular.ttf")
)
if not candidates:
    raise SystemExit("Inter font unavailable: install fonts-inter")
path = candidates[0]
specs = [
    ("tiny", 13, 18, 18),
    ("regular", 17, 22, 24),
    ("title", 26, 32, 35),
]
out = []
out.append("/* Generated from Inter (OFL-1.1), via tools/generate_font.py. */")
out.append("#ifndef NOVA_RASTER_FONT_H")
out.append("#define NOVA_RASTER_FONT_H")
out.append("#include <stdint.h>")
for name, size, width, height in specs:
    font = ImageFont.truetype(path, size)
    ascent, _ = font.getmetrics()
    stride = width * height // 2
    out.append(f"#define FONT_{name.upper()}_W {width}")
    out.append(f"#define FONT_{name.upper()}_H {height}")
    out.append(f"static const uint8_t font_{name}_advance[95] = {{")
    out.append(",".join(str(min(width, max(2, round(font.getlength(chr(ch))) + 1))) for ch in range(32, 127)))
    out.append("};")
    out.append(f"static const uint8_t font_{name}_data[95][{stride}] = {{")
    for ch in range(32,127):
        im = Image.new("L",(width,height))
        d = ImageDraw.Draw(im)
        d.text((1, ascent),chr(ch), font=font, fill=255, anchor="ls")
        pix = list(im.getdata())
        nibs = [min(15,(v+8)//17) for v in pix]
        data = [(nibs[i]<<4)|nibs[i+1] for i in range(0,len(nibs),2)]
        out.append("  {" + ",".join(str(v) for v in data) + "},")
    out.append("};")
out.append("#endif")
target = Path("kernel/raster_font.h")
target.write_text("\n".join(out)+"\n", encoding="utf-8")
print("Generated", target, "with", len(out), "lines")
