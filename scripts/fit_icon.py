#!/usr/bin/env python3
"""One-off downscale of a notification icon to 512x512.

Notification icons are capped around 512x512; anything larger is rejected and
the notification falls back to the default system icon. This is a tool for
preparing a source icon, NOT a build step -- gen_icon_header.py embeds whatever
PNG it is pointed at and only warns if that PNG is oversized, so the operator
owns the resolution from here on.

    python3 scripts/fit_icon.py config/icons/logo.png [out.png] [size]
"""
import sys, os

try:
    from PIL import Image
except ImportError:
    sys.exit('Pillow is required for this one-off: pip install Pillow')

SRC = sys.argv[1] if len(sys.argv) > 1 else 'config/icons/logo.png'
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.splitext(SRC)[0] + '.notify.png'
SIZE = int(sys.argv[3]) if len(sys.argv) > 3 else 512

im = Image.open(SRC)
before = im.size

# Fit inside SIZE x SIZE without cropping, preserving aspect. Transparent
# logos stay RGBA; opaque ones become palette PNGs, which are far smaller.
if im.mode in ('RGBA', 'LA'):
    im = im.convert('RGBA')
else:
    im = im.convert('RGB')

im.thumbnail((SIZE, SIZE), Image.LANCZOS)

if im.mode == 'RGBA':
    im.save(OUT, optimize=True)
else:
    im.convert('P', palette=Image.ADAPTIVE, colors=256).save(OUT, optimize=True)

after = Image.open(OUT).size
print('%s  %dx%d  %d bytes' % (SRC, before[0], before[1], os.path.getsize(SRC)))
print('%s  %dx%d  %d bytes' % (OUT, after[0], after[1], os.path.getsize(OUT)))