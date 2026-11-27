#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PolarPatch
"""The screensaver's film: video clips dithered to 1-bit 240 x 240 frames (an 8 x 8 Bayer matrix, as dither_clip.py's
bayer8), coded frame by frame against the frame before, packed for the resource region (fm1.toml resources).

    /usr/bin/python3 app/tools/again_saver.py --out app/media/again_saver.bin --gif preview.gif \\
        clip.mov[:start[:seconds[:gamma]]] ...

The clips play in order, round and round. The format (little-endian), read by app/saver.h:
    "AGS1", u16 frames, u16 ms a frame, then frames + 1 u32 offsets (from the payload's start) of each frame's code,
    then frames u32 CRC-32s of each frame as decoded (the host's test checks them), then the codes.
A frame's code is a raw DEFLATE stream (RFC 1951) of its ops; the ops rebuild the frame (7200 bytes, 30 a row, the
most significant bit the leftmost pixel, 1 white) over the one before it, a byte at a time, each op a byte:
    op >> 6 = 0 SKIP: (op & 63) + 1 bytes kept from the frame before
              1 UP:   .. copied from 240 bytes back in this frame (8 rows up: the same row of the Bayer matrix)
              2 LEFT: .. copied from the byte before (a flat grey along a row)
              3 LIT:  .. bytes that follow the op
The first frame is coded over a black one; going round, the player blacks its frame first.
The footage is the USB002 tour's creative drop (see LICENSING.md): this tool is GPL, the film it makes is not."""
import argparse, struct, sys, zlib
from pathlib import Path
from PIL import Image, ImageFilter

sys.path.insert(0, str(Path(__file__).parent))
from dither_clip import frames, dither   # noqa: E402

N, FB, BACK = 240, 7200, 240
BUDGET = 69 * 4096 - 32                  # app.h APP_RES_SECTORS, less res.c's head


def dither_clip(path, start, dur, gamma, fps, sharpen, hold):
    fs = frames(path, fps, N, start, dur)
    hist = sorted(b for f in fs[::2] for b in f[::7])
    lo, hi = hist[len(hist) * 2 // 100], hist[len(hist) * 98 // 100]
    hi = max(hi, lo + 1)
    out, held = [], None
    for f in fs:
        if hold:
            held = bytearray(f) if held is None else held
            for i, b in enumerate(f):
                if abs(b - held[i]) > hold:
                    held[i] = b
            f = bytes(held)
        if sharpen:
            f = Image.frombytes('L', (N, N), f).filter(ImageFilter.UnsharpMask(1.2, sharpen, 2)).tobytes()
        v = [min(max((b - lo) / (hi - lo), 0.0), 1.0) ** gamma for b in f]
        out.append(Image.frombytes('L', (N, N), bytes(dither(v, N, 'bayer8', 2))))
    return out


def pack(im):
    return im.convert('1', dither=Image.NONE).tobytes()


def ops(prev, cur):
    out, lit, i = bytearray(), bytearray(), 0

    def flush():
        nonlocal lit
        while lit:
            k = min(64, len(lit))
            out.append(0xC0 | (k - 1))
            out.extend(lit[:k])
            lit = lit[k:]
    while i < FB:
        best, bop = 0, 0
        for op in (0, 1, 2):
            n = 0
            while i + n < FB and n < 64:
                j = i + n
                if op == 0:
                    src = prev[j]
                elif op == 1:
                    src = cur[j - BACK] if j >= BACK else -1
                else:
                    src = cur[j - 1] if j >= 1 else -1
                if src != cur[j]:
                    break
                n += 1
            if n > best:
                best, bop = n, op
        if best >= 2 or (best == 1 and not lit):
            flush()
            out.append(bop << 6 | (best - 1))
            i += best
        else:
            lit.append(cur[i])
            i += 1
    flush()
    return bytes(out)


def run_ops(prev, code):
    """the player's side (saver.h sv_ops), to check the coder"""
    f, i, k = bytearray(prev), 0, 0
    while k < len(code):
        op, n = code[k] >> 6, (code[k] & 63) + 1
        k += 1
        for _ in range(n):
            f[i] = f[i] if op == 0 else f[i - BACK] if op == 1 else f[i - 1] if op == 2 else code[k]
            k += op == 3
            i += 1
    assert i == FB
    return bytes(f)


def deflate(b):
    c = zlib.compressobj(9, zlib.DEFLATED, -15, 9)
    return c.compress(b) + c.flush()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('clips', nargs='+', help='file[:start[:seconds[:gamma]]]')
    ap.add_argument('--out', required=True)
    ap.add_argument('--gif')
    ap.add_argument('--fps', type=float, default=6)
    ap.add_argument('--sharpen', type=int, default=80)
    ap.add_argument('--hold', type=int, default=0)
    a = ap.parse_args()
    ims = []
    for c in a.clips:
        p = c.split(':') + ['0', '0', '1']
        n0 = len(ims)
        ims += dither_clip(p[0], float(p[1]), float(p[2]), float(p[3]), a.fps, a.sharpen, a.hold)
        print(f'{p[0]}: {len(ims) - n0} frames', file=sys.stderr)
    raw = [pack(im) for im in ims]
    codes, crcs, prev, biggest = [], [], bytes(FB), 0
    for r in raw:
        o = ops(prev, r)
        assert run_ops(prev, o) == r
        biggest = max(biggest, len(o))
        codes.append(deflate(o))
        crcs.append(zlib.crc32(r))
        prev = r
    n = len(raw)
    head = 8 + 4 * (n + 1) + 4 * n
    offs, at = [], head
    for c in codes:
        offs.append(at)
        at += len(c)
    offs.append(at)
    blob = b'AGS1' + struct.pack('<HH', n, round(1000 / a.fps)) + struct.pack(f'<{n + 1}I', *offs) + \
        struct.pack(f'<{n}I', *crcs) + b''.join(codes)
    print(f'{n} frames, {n / a.fps:.1f} s, {len(blob)} bytes ({len(blob) * 100 // BUDGET} % of the room), '
          f'the longest ops {biggest} bytes', file=sys.stderr)
    if len(blob) > BUDGET:
        raise SystemExit(f'again_saver: {len(blob)} bytes, the region holds {BUDGET}: shorter clips')
    assert biggest <= 4096, 'saver.h SV_OPS'
    Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    Path(a.out).write_bytes(blob)
    if a.gif:
        ims[0].save(a.gif, save_all=True, append_images=ims[1:], duration=round(1000 / a.fps), loop=0)


if __name__ == '__main__':
    main()
