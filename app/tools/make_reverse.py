#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PolarPatch
"""REVERSE, a CHOP sample of my own: sounds made to be played backwards. Each is a stab whose pitch falls (a bright
saw through a closing low-pass, with a sub under it) into a room, then turned around: the room becomes a suck
toward the hit, the fall a rise, the hit an abrupt stop at the end. Its lengths are the tempo's (an eighth, a beat,
a bar at 124 BPM), so a slice started on a step ends on the next beat. Then two swells: a chord in a long room,
backwards. All synthesized here; nothing recorded.

  /usr/bin/python3 app/tools/make_reverse.py                 -> app/build/reverse/reverse.wav and reverse.agc
  /usr/bin/python3 app/tools/make_reverse.py --send          (to the FM-1 as CHOP's sample)

Slices: 1-4 rises an eighth long (A, C, E, G), 5-8 a beat long, 9-10 a bar-long swell (Am9, Fmaj9)."""
import argparse
import struct
import subprocess
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import again_chop as AC  # noqa: E402

SR = 44100
BPM = 124.0
BEAT = 60.0 / BPM
rng = np.random.default_rng(1337)


def hz(note):
    return 440.0 * 2 ** ((note - 69) / 12)


def lowpass(x, fc):
    """a 2-pole low-pass whose cutoff follows fc (an array, Hz): a state-variable filter, sample by sample"""
    y = np.zeros_like(x)
    ic1 = ic2 = 0.0
    k = 1 / 1.8
    for i in range(len(x)):
        g = np.tan(np.pi * min(fc[i], 0.45 * SR) / SR)
        a1 = 1 / (1 + g * (g + k)); a2 = g * a1; a3 = g * a2
        v3 = x[i] - ic2; v1 = a1 * ic1 + a2 * v3; v2 = ic2 + a2 * ic1 + a3 * v3
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2
        y[i] = v2
    return y


def room(x, secs, damp=0.4, wet=0.35):
    """a room: x convolved with decaying, darkening noise (its tail secs long to -60 dB), the tail wet of the dry's
    energy"""
    n = int(secs * SR)
    t = np.arange(n) / SR
    ir = rng.standard_normal(n) * np.exp(-6.9 * t / secs)
    # darker as it decays: a one-pole low-pass sliding down
    out = np.zeros(n); lp = 0.0
    for i in range(n):
        c = 0.18 * (1 - damp * t[i] / secs) ** 2 + 0.01
        lp += (ir[i] - lp) * c
        out[i] = lp
    out *= np.sqrt(wet / np.sum(out ** 2))        # the tail's energy: wet of the dry's
    out[0] += 1.0                                 # the dry hit in it
    y = np.convolve(x, out)
    return y / np.max(np.abs(y))


def stab(note, length):
    """a stab: its pitch falling two octaves to the note, the low-pass closing, a sub under it; length seconds"""
    n = int(length * SR)
    t = np.arange(n) / SR
    f = hz(note) * (1 + 3.0 * np.exp(-t / 0.09))             # from two octaves up, falling over ~0.15 s
    ph = np.cumsum(f) / SR
    saw = 2 * (ph % 1) - 1
    saw += 0.5 * (2 * ((ph * 1.007) % 1) - 1)                # a second, a hair sharp: wider
    sub = np.sin(2 * np.pi * np.cumsum(hz(note - 12) * (1 + 1.5 * np.exp(-t / 0.03))) / SR)
    env = np.exp(-t / (length * 0.5))
    x = lowpass(saw, 250 + 5000 * np.exp(-t / 0.12)) * env + 0.6 * sub * env
    return x


def chord(notes, length):
    """a soft chord: sines with a little second harmonic, a slow fall"""
    n = int(length * SR)
    t = np.arange(n) / SR
    x = sum(np.sin(2 * np.pi * hz(m) * t) + 0.25 * np.sin(4 * np.pi * hz(m) * t) for m in notes)
    return x * np.exp(-t / (length * 0.4)) * np.minimum(1, t / 0.005)


def reverse_of(x, length, tail, wet=0.35):
    """x in a room of tail seconds, cut to length seconds from its start, backwards, a 4 ms fade at the cut"""
    y = room(x, tail, wet=wet)[:int(length * SR)]
    y = y[::-1].copy()
    k = int(0.004 * SR)
    y[:k] *= np.linspace(0, 1, k)                 # (the far end of the tail: in from silence)
    y[-k // 4:] *= np.linspace(1, 0, k // 4)      # (the hit: cut, not clicked)
    return y / np.max(np.abs(y))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--send", action="store_true")
    ap.add_argument("--out", default=str(Path(__file__).resolve().parents[1] / "build/reverse"))
    a = ap.parse_args()
    parts = []
    for m in (45, 48, 52, 55):                    # A, C, E, G: an eighth
        parts.append(reverse_of(stab(m, 0.35), BEAT / 2, 0.6))
    for m in (57, 60, 64, 62):                    # a beat
        parts.append(reverse_of(stab(m, 0.6), BEAT, 1.2))
    for notes in ((57, 60, 64, 67, 71), (53, 57, 60, 64, 67)):   # a bar: Am9, Fmaj9
        parts.append(reverse_of(chord(notes, 1.5), 4 * BEAT, 3.0, wet=1.5) * 0.9)
    gap = np.zeros(int(0.01 * SR))
    x, starts = [], []
    pos = 0
    for p in parts:
        starts.append(pos)
        x += [p, gap]
        pos += len(p) + len(gap)
    x = np.concatenate(x) * 10 ** (-1 / 20)
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    pcm = (np.clip(x, -1, 1) * 32767).astype("<i2")
    import wave
    with wave.open(str(out / "reverse.wav"), "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR); w.writeframes(pcm.tobytes())
    # CHOP's file: at 22050 Hz, the slices where the parts start
    y = AC.resample(x, SR)
    blocks, n = AC.encode(y)
    sl = [s * AC.RATE // SR for s in starts]
    head = struct.pack("<4sIIHBB", b"AGC1", AC.RATE, len(y), n // AC.BLOCK, len(sl), 0)
    head += struct.pack(f"<{AC.MAX_SLICES}I", *(sl + [0] * (AC.MAX_SLICES - len(sl))))
    head += b"REVERSE".ljust(16, b"\0")
    data = head + blocks
    (out / "reverse.agc").write_bytes(data)
    print(f"reverse: {len(y) / AC.RATE:.2f} s, {len(sl)} slices, {len(data)} B -> {out}")
    if a.send:
        tool = Path(__file__).resolve().parents[2] / "base/tools/fm1_companion.py"
        sys.exit(subprocess.call([sys.executable, str(tool), "put", str(out / "reverse.agc"), "chop"]))


if __name__ == "__main__":
    main()
