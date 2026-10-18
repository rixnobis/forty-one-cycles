#!/usr/bin/env python3
"""raw2png.py log.txt outdir: convert every SHOT line's .raw (15bpp) to PNG, 2x nearest."""
import re, sys
import numpy as np
from PIL import Image
log, out = sys.argv[1], sys.argv[2]
n = 0
for line in open(log, errors='replace'):
    m = re.search(r'SHOT (\S+) (\d+) (\d+) (\S+)', line)
    if not m: continue
    name, w, h = m.group(1), int(m.group(2)), int(m.group(3))
    raw = open(f'{out}/{name}.raw', 'rb').read()
    if len(raw) == w * h * 2:
        d = np.frombuffer(raw, dtype='<u2').reshape(h, w)
        rgb = (np.stack([d & 31, (d >> 5) & 31, (d >> 10) & 31], -1).astype(np.uint16) * 255 // 31).astype(np.uint8)
    else:
        rgb = np.frombuffer(raw, dtype=np.uint8)[:w * h * 3].reshape(h, w, 3)
    Image.fromarray(rgb).save(f'{out}/{name}.png'); n += 1
    print(f'{name}.png {w}x{h}')
print('converted', n)
