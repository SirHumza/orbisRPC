#!/usr/bin/env python3
"""Convert source artwork into 32-bit BMPs the PS4 build can actually load.

Why BMP at all
--------------
SDL2_image on this platform has no decoders compiled in -- IMG_Init reports
"PNG images are not supported", and the toolchain ships no PNG decoder header
either. The only image loader guaranteed to exist is SDL_LoadBMP_RW, in libSDL2
proper. So the PNGs are converted here, at build time, rather than on the
console.

Backgrounds are left alone
--------------------------
Several sources are RGB with an opaque background baked in, and an earlier
version of this script flooded those to transparent. That was wrong: the artwork
is drawn on tiles that carry their own background -- the YouTube mark belongs on
a white rounded square, not floating bare on a dark card. Keying it away removed
exactly the part that was meant to be seen.

So nothing is keyed. Alpha is added where the source lacks it, and where the
source has it, it is kept.

Pre-scaling
-----------
The menu and settings pages draw these once and cache them, but scaling a
1024x1024 wordmark down to 34px on a CPU rasteriser is pure waste. Baking the
target size in means the runtime blit is 1:1 -- no resampling at all on the frame
path -- and the asset shrinks by roughly two orders of magnitude.

Usage
-----
    gfx2bmp.py --out <dir> <src>=<w>x<h> [<src>=<w>x<h> ...]

A target of 0x0 keeps the source size.
"""

import argparse
import os
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("gfx2bmp.py: PIL is required (pip install pillow)")


def convert(src, out_dir, max_w, max_h):
    with Image.open(src) as im:
        # RGBA throughout. Where the source has no alpha channel this adds a fully
        # opaque one, which is what a BMP with a real alpha byte needs.
        im = im.convert("RGBA")

        if max_w or max_h:
            scale_w = max_w / im.width if max_w else 0
            scale_h = max_h / im.height if max_h else 0
            # Fit inside the box, preserving aspect ratio, never enlarging.
            scale = min(scale_w, scale_h) if scale_w and scale_h else (scale_w or scale_h)
            if scale and scale < 1.0:
                new_w = max(1, int(im.width * scale))
                new_h = max(1, int(im.height * scale))
                im = im.resize((new_w, new_h), Image.Resampling.LANCZOS)

        name = os.path.splitext(os.path.basename(src))[0] + ".bmp"
        dst = os.path.join(out_dir, name)

        # 32-bit BMP so the alpha channel survives into the reader.
        im.save(dst, "BMP")

        size = os.path.getsize(dst)
        print(f"  {os.path.basename(src):28} {im.width:>4}x{im.height:<4} "
              f"-> {name:24} {size / 1024.0:6.1f} KB")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True, help="output directory")
    ap.add_argument("specs", nargs="+",
                    help="<source.png>=<w>x<h>, target 0x0 to keep source size")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)

    for spec in args.specs:
        if "=" not in spec:
            sys.exit(f"gfx2bmp.py: bad spec '{spec}', expected <src>=<w>x<h>")

        src, _, dims = spec.partition("=")
        try:
            w, h = (int(v) for v in dims.lower().split("x"))
        except ValueError:
            sys.exit(f"gfx2bmp.py: bad dimensions in '{spec}'")

        if not os.path.isfile(src):
            sys.exit(f"gfx2bmp.py: no such file: {src}")

        convert(src, args.out, w, h)


if __name__ == "__main__":
    main()