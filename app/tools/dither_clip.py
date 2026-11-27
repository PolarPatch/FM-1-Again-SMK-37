#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PolarPatch
"""Dither a video clip to 1-bit frames for the FM-1's screen, as Arctic Scream's aurora: a 4x4 Bayer matrix over
the whole tonal range (Arctic's LO/HI window is for menu colours and burns out a photo). The clip is cut to a centred square, its levels stretched (2nd .. 98th
percentile over the whole clip), and written as a preview GIF and a raw file of packed frames (1 bit a pixel, MSB
first, rows top down).

    /usr/bin/python3 app/tools/dither_clip.py clip.mov out/name [--fps 12.5] [--px 1] [--size 240]

--px 2 draws each dot as 2x2 pixels (a coarser, poster-like grain, a quarter of the bytes)."""
import argparse, subprocess, zlib
from PIL import Image, ImageFilter

BAYER = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5]


def frames(path, fps, n, start=0.0, dur=0.0):
    cmd = ['ffmpeg', '-v', 'error'] + (['-ss', str(start)] if start else []) + (['-t', str(dur)] if dur else []) + [
           '-i', path, '-vf',
           f'fps={fps},crop=min(iw\\,ih):min(iw\\,ih),scale={n}:{n}:flags=area,format=gray',
           '-f', 'rawvideo', '-']
    raw = subprocess.run(cmd, capture_output=True, check=True).stdout
    return [raw[i:i + n * n] for i in range(0, len(raw) - n * n + 1, n * n)]


def bayer8():
    m = [[0]]
    for _ in range(3):
        k = len(m)
        m = [[4 * m[y % k][x % k] + [[0, 2], [3, 1]][y // k][x // k] for x in range(2 * k)] for y in range(2 * k)]
    return m


B8 = bayer8()


def dither(v, n, method, grays):
    """v: n*n values 0 .. 1; the dithered pixels, `grays` levels spread over 0 .. 255"""
    g = grays - 1
    out = bytearray(n * n)
    if method in ('bayer4', 'bayer8'):
        for i, x in enumerate(v):
            t = (BAYER[(i // n & 3) * 4 + (i % n & 3)] + 0.5) / 16 if method == 'bayer4' else \
                (B8[i // n & 7][i % n & 7] + 0.5) / 64
            out[i] = min(int(x * g + t), g) * 255 // g
        return out
    e = list(v)
    taps = {'atkinson': [(1, 0, 1/8), (2, 0, 1/8), (-1, 1, 1/8), (0, 1, 1/8), (1, 1, 1/8), (0, 2, 1/8)],
            'fs': [(1, 0, 7/16), (-1, 1, 3/16), (0, 1, 5/16), (1, 1, 1/16)]}[method]
    for i in range(n * n):
        x = i % n
        q = min(max(round(e[i] * g), 0), g)
        out[i] = q * 255 // g
        err = e[i] - q / g
        for dx, dy, w in taps:
            if 0 <= x + dx < n and i + dy * n + dx < n * n:
                e[i + dy * n + dx] += err * w
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('clip'); ap.add_argument('out')
    ap.add_argument('--fps', type=float, default=12.5)
    ap.add_argument('--px', type=int, default=1)
    ap.add_argument('--size', type=int, default=240)
    ap.add_argument('--gamma', type=float, default=1.0)
    ap.add_argument('--method', default='bayer4', choices=['bayer4', 'bayer8', 'atkinson', 'fs'])
    ap.add_argument('--grays', type=int, default=2, help='levels: 2 (black/white) or 4 (two greys between)')
    ap.add_argument('--hold', type=int, default=0, help='grey steps (0 .. 255) a pixel must move to change')
    ap.add_argument('--sharpen', type=int, default=0, help='unsharp mask amount in percent before dithering')
    a = ap.parse_args()
    n = a.size // a.px
    fs = frames(a.clip, a.fps, n)
    hist = sorted(b for f in fs[::4] for b in f[::7])
    lo, hi = hist[len(hist) * 2 // 100], hist[len(hist) * 98 // 100]
    hi = max(hi, lo + 1)
    ims, packed, held = [], bytearray(), None
    for f in fs:
        if a.hold:                       # a pixel keeps its last grey until it moves more than HOLD: still parts stay still
            held = bytearray(f) if held is None else held
            for i, b in enumerate(f):
                if abs(b - held[i]) > a.hold:
                    held[i] = b
            f = bytes(held)
        if a.sharpen:
            f = Image.frombytes('L', (n, n), f).filter(ImageFilter.UnsharpMask(1.2, a.sharpen, 2)).tobytes()
        v = [min(max((b - lo) / (hi - lo), 0.0), 1.0) ** a.gamma for b in f]
        im = Image.frombytes('L', (n, n), bytes(dither(v, n, a.method, a.grays)))
        if a.px > 1:
            im = im.resize((a.size, a.size), Image.NEAREST)
        ims.append(im)
        small = im.resize((n, n), Image.NEAREST).tobytes() if a.px > 1 else im.tobytes()
        if a.grays == 2:
            packed += Image.frombytes('L', (n, n), small).convert('1', dither=Image.NONE).tobytes()
        else:                                   # 2 bits a pixel, 4 to a byte
            packed += bytes((small[i] >> 6) << 6 | (small[i+1] >> 6) << 4 | (small[i+2] >> 6) << 2 | small[i+3] >> 6
                            for i in range(0, len(small), 4))
    ims[0].save(a.out + '.gif', save_all=True, append_images=ims[1:], duration=round(1000 / a.fps), loop=0)
    open(a.out + '.bin', 'wb').write(packed)
    # an estimate of what flash would hold: each frame XORed with the last, deflated
    prev, xor = bytes(len(packed) // len(fs)), bytearray()
    step = len(prev)
    for i in range(0, len(packed), step):
        cur = packed[i:i + step]
        xor += bytes(p ^ q for p, q in zip(cur, prev)); prev = cur
    print(f'{a.out}: {len(fs)} frames {n}x{n}, raw {len(packed)//1024} KB, '
          f'deflated {len(zlib.compress(bytes(packed), 9))//1024} KB, xor+deflated {len(zlib.compress(bytes(xor), 9))//1024} KB')


if __name__ == '__main__':
    main()
