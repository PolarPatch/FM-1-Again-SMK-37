#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PolarPatch
"""CHOP's samples: a WAV made into FM-1 Again's sample file ("AGC1": mono, 22050 Hz, IMA ADPCM in blocks of 256
samples, each block with its decoder's state so a slice can start anywhere), its slices found where the sound
starts again (onsets) or cut evenly, then sent to the FM-1 or kept as a file or a C header.

  /usr/bin/python3 app/tools/again_chop.py voice.wav                      -> voice.agc (beside it)
  /usr/bin/python3 app/tools/again_chop.py voice.wav --even 16            (16 even slices)
  /usr/bin/python3 app/tools/again_chop.py voice.wav --send               (to the FM-1: fm1_companion's put, as "chop")
  /usr/bin/python3 app/tools/again_chop.py voice.wav --header app/gen/again_chop.h   (the factory sample)

--max-seconds cuts it (default: what the FM-1 holds, 8.2 s). --gain-db, --normalize (default on: peaks at -1 dB).
Needs numpy (and mido with python-rtmidi for --send)."""
import argparse
import struct
import subprocess
import sys
import wave
from pathlib import Path

import numpy as np

RATE = 22050
BLOCK = 256
BLOCK_BYTES = 4 + BLOCK // 2
HEAD = 160
MAX_SLICES = 32
ROOM = 0x17000 - 32                               # app.c CH_FILE (XDATA1): its bytes, less plat_file's head

IMA_STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88,
            97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
            724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660,
            4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
            18500, 20350, 22385, 24623, 27086, 29794, 32767]
IMA_INDEX = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]


def read_wav(path):
    with wave.open(str(path)) as w:
        n, ch, sw, sr = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
        raw = w.readframes(n)
    if sw == 2:
        x = np.frombuffer(raw, "<i2").astype(np.float64) / 32768
    elif sw == 3:
        b = np.frombuffer(raw, np.uint8).reshape(-1, 3)
        x = ((b[:, 0].astype(np.int32) | b[:, 1].astype(np.int32) << 8 | b[:, 2].astype(np.int32) << 16) << 8 >> 8) / 8388608
    elif sw == 4:
        x = np.frombuffer(raw, "<i4").astype(np.float64) / 2147483648
    else:
        x = (np.frombuffer(raw, np.uint8).astype(np.float64) - 128) / 128
    return x.reshape(-1, ch).mean(axis=1), sr


def resample(x, sr):
    if sr == RATE:
        return x
    n = int(len(x) * RATE / sr)
    # a windowed-sinc low-pass before going down (the band above 10 kHz out), then linear between samples
    if sr > RATE:
        fc = 0.45 * RATE / sr
        k = np.arange(-64, 65)
        h = np.sinc(2 * fc * k) * np.hanning(len(k))
        x = np.convolve(x, h / h.sum(), mode="same")
    t = np.arange(n) * sr / RATE
    return np.interp(t, np.arange(len(x)), x)


def encode(x):
    """x: floats -1..1 -> the blocks (bytes), each its state then its nibbles; the encoder follows its own decoder"""
    s = np.clip(np.round(x * 32767), -32768, 32767).astype(np.int32)
    s = np.concatenate([s, np.zeros((-len(s)) % BLOCK, np.int32)])
    out, pred, idx = bytearray(), 0, 0
    for b in range(0, len(s), BLOCK):
        out += struct.pack("<hBB", pred, idx, 0)
        nib = []
        for v in s[b:b + BLOCK]:
            step = IMA_STEP[idx]
            d = int(v) - pred
            code = 8 if d < 0 else 0
            d = abs(d)
            diff = step >> 3
            if d >= step:
                code |= 4; d -= step; diff += step
            step >>= 1
            if d >= step:
                code |= 2; d -= step; diff += step
            step >>= 1
            if d >= step:
                code |= 1; diff += step
            pred = max(-32768, min(32767, pred - diff if code & 8 else pred + diff))
            idx = max(0, min(88, idx + IMA_INDEX[code]))
            nib.append(code)
        out += bytes(nib[i] | nib[i + 1] << 4 for i in range(0, BLOCK, 2))
    return bytes(out), len(s)


def onsets(x, most):
    """where the sound starts again: the rise of its energy (10 ms frames), the strongest, at least 150 ms apart"""
    f = 220
    e = np.array([np.sum(x[i:i + f] ** 2) for i in range(0, len(x) - f, f)]) + 1e-9
    le = np.log10(e)
    flux = np.maximum(0, np.diff(le, prepend=le[0]))
    flux[le < le.max() - 2.5] = 0                 # (not in the quiet: a tail's ripples)
    cand = sorted(range(len(flux)), key=lambda i: -flux[i])
    picked = [0]
    for i in cand:
        if flux[i] < 0.4 or len(picked) >= most:
            break
        if all(abs(i - p) * f >= RATE * 0.15 for p in picked):
            picked.append(i)
    return sorted(p * f for p in picked)


def make(path, even, seconds, gain_db, normalize, name):
    x, sr = read_wav(path)
    x = resample(x, sr)
    x = x[:int(min(seconds, ROOM // BLOCK_BYTES * BLOCK / RATE) * RATE)]
    if normalize and np.max(np.abs(x)) > 0:
        x = x / np.max(np.abs(x)) * 10 ** (-1 / 20)
    x = x * 10 ** (gain_db / 20)
    blocks, n = encode(x)
    sl = [i * len(x) // even for i in range(even)] if even else onsets(x, MAX_SLICES)
    head = struct.pack("<4sIIHBB", b"AGC1", RATE, len(x), n // BLOCK, len(sl), 0)
    head += struct.pack(f"<{MAX_SLICES}I", *(sl + [0] * (MAX_SLICES - len(sl))))
    head += name.encode("ascii", "replace")[:15].ljust(16, b"\0")
    assert len(head) == HEAD
    data = bytearray(head + blocks)
    assert len(data) <= ROOM, "too long for the FM-1"
    # XDATA1 (plat.h): the update loader clears a sector whose bytes 0xF00..0xF07 read as its record, "AT" at 0xF06;
    # the file sits from a sector's start after plat_file's 32-byte head: never "AT" there (a step of one sample's nibble)
    for at in range(0xF06 - 32, len(data) - 1, 0x1000):
        if data[at:at + 2] == b"AT":
            data[at + 1] ^= 1
    data = bytes(data)
    return data, len(x), sl


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("wav")
    ap.add_argument("--even", type=int, default=0, help="even slices (2..32) instead of onsets")
    ap.add_argument("--max-seconds", type=float, default=8.2)
    ap.add_argument("--gain-db", type=float, default=0.0)
    ap.add_argument("--no-normalize", action="store_true")
    ap.add_argument("--name", default=None, help="its name on the screen (15 characters)")
    ap.add_argument("--out", default=None)
    ap.add_argument("--header", default=None, help="a C header instead (the factory sample)")
    ap.add_argument("--send", action="store_true", help="to the FM-1 (fm1_companion.py put)")
    a = ap.parse_args()
    src = Path(a.wav)
    name = (a.name or src.stem).upper()
    data, n, sl = make(src, max(0, min(MAX_SLICES, a.even)), a.max_seconds, a.gain_db, not a.no_normalize, name)
    print(f"{src.name}: {n / RATE:.2f} s, {len(sl)} slices, {len(data)} B")
    if a.header:
        lines = [f"/* generated by app/tools/again_chop.py from {src.name}: CHOP's factory sample (AGC1) */",
                 "#pragma once", "#include <stdint.h>", f"static const uint8_t CHOP_FACTORY[{len(data)}] = {{"]
        for i in range(0, len(data), 24):
            lines.append("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 24]) + ",")
        lines.append("};")
        Path(a.header).write_text("\n".join(lines) + "\n")
        return
    out = Path(a.out) if a.out else src.with_suffix(".agc")
    out.write_bytes(data)
    if a.send:
        tool = Path(__file__).resolve().parents[2] / "base/tools/fm1_companion.py"
        sys.exit(subprocess.call([sys.executable, str(tool), "put", str(out), "chop"]))


if __name__ == "__main__":
    main()
