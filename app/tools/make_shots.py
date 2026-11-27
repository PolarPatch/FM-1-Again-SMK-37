#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PolarPatch
"""FM-1 Again's pictures for the README and the Firmware Hub, from the host simulator's screens (app/docs/shots.txt)
and the screensaver's film as the FM-1 decodes it (app/media/again_saver.bin):

    app/docs/img/screens.png   the boot screen (the logo), the screensaver, then the screens, 3 x 3 at 240 x 240 (loaf-breaker's way)
    app/docs/img/screen-*.png  each screen alone
    docs/card.png              the hub's card: 1024 x 1024, black and white (fmhub.json image)

    /usr/bin/python3 base/tools/build.py app --host
    app/build/host/again_host app/docs/shots.txt app/build/shots
    /usr/bin/python3 app/tools/make_shots.py"""
import random, struct, sys, zlib
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
SHOTS, IMG = ROOT / "app/build/shots", ROOT / "app/docs/img"
HEROS = str(ROOT / "app/assets/fonts/texgyreheros-bold.otf")
BLUE, BLUE_HI, GAP, EDGE = (50, 50, 200), (100, 100, 218), 10, (40, 40, 40)
FILM_FRAME = 66                                  # the crowd, hands up, under the lights (not the stage)

sys.path.insert(0, str(Path(__file__).parent))
from again_saver import run_ops, FB, N           # noqa: E402


def film_frame(k):
    """frame k of the film, decoded as saver.h decodes it"""
    b = (ROOT / "app/media/again_saver.bin").read_bytes()
    n = struct.unpack_from("<H", b, 4)[0]
    offs = struct.unpack_from(f"<{n + 1}I", b, 8)
    f = bytes(FB)
    for i in range(k + 1):
        f = run_ops(f, zlib.decompress(b[offs[i]:offs[i + 1]], -15))
    return Image.frombytes("1", (N, N), f).convert("RGB")


def wave(d, x0, y0, w, h, seed, gap=2):
    """a strip of the waveform's bars, the beats louder"""
    r, v = random.Random(seed), 0.45
    for i, x in enumerate(range(x0, x0 + w, gap + 1)):
        v = min(1.0, max(0.1, v + (r.random() - 0.5) * 0.4))
        a = 1.0 if i % 8 == 0 else v
        d.rectangle([x, y0 + h - int(a * h), x + gap - 1, y0 + h], fill=BLUE if i % 8 else BLUE_HI)


def fit(text, width, top=300):
    """the font size at which text is width pixels wide"""
    size = top
    while size > 8 and ImageFont.truetype(HEROS, size).getlength(text) > width:
        size -= 1
    return ImageFont.truetype(HEROS, size)


def logo(size=240):
    """the name, set as the posters set it: big, bold, white on black"""
    im = Image.new("RGB", (size, size), (0, 0, 0))
    d, s = ImageDraw.Draw(im), size / 240
    big = fit("AGAIN", int(212 * s))
    d.text((int(14 * s), int(30 * s)), "FM-1", font=big, fill=(255, 255, 255))
    d.text((int(12 * s), int(98 * s)), "AGAIN", font=big, fill=(255, 255, 255))
    wave(d, int(14 * s), int(186 * s), int(212 * s), int(16 * s), 7, gap=max(1, int(2 * s)))
    small = ImageFont.truetype(HEROS, int(11 * s))
    d.text((int(14 * s), int(214 * s)), "P O L A R P A T C H", font=small, fill=(140, 140, 140))
    return im


def grid(tiles, cols):
    rows = (len(tiles) + cols - 1) // cols
    out = Image.new("RGB", (cols * 240 + (cols + 1) * GAP, rows * 240 + (rows + 1) * GAP), EDGE)
    for k, t in enumerate(tiles):
        out.paste(t, (GAP + (k % cols) * (240 + GAP), GAP + (k // cols) * (240 + GAP)))
    return out


def card():
    """1024 x 1024: the film's crowd at four pixels a dot, the waveform, the name under it"""
    im = Image.new("RGB", (1024, 1024), (0, 0, 0))
    f = film_frame(FILM_FRAME).resize((960, 960), Image.NEAREST).crop((0, 200, 960, 800))
    im.paste(f, (32, 32))
    d = ImageDraw.Draw(im)
    wave(d, 32, 660, 960, 40, 11, gap=6)
    big = fit("FM-1 AGAIN", 960)
    d.text((28, 720), "FM-1 AGAIN", font=big, fill=(255, 255, 255))
    small = ImageFont.truetype(HEROS, 30)
    d.text((34, 950), "G R O O V E B O X   F I R M W A R E   ·   P O L A R P A T C H", font=small, fill=(150, 150, 150))
    return im


def main():
    IMG.mkdir(parents=True, exist_ok=True)
    order = [("drop", "Drop!"), ("build", "Drop in 4"), ("pad", "a pad held"), ("chop", "CHOP, a slice reversed"),
             ("keys", "KEYS"), ("home2", "playing"), ("tape", "TAPE")]
    tiles = [Image.open(SHOTS / "boot.png").convert("RGB"), film_frame(FILM_FRAME)] + [Image.open(SHOTS / f"{n}.png").convert("RGB") for n, _ in order]
    grid(tiles, 3).save(IMG / "screens.png", optimize=True)
    Image.open(SHOTS / "boot.png").convert("RGB").save(IMG / "screen-logo.png", optimize=True)
    film_frame(FILM_FRAME).convert("1").save(IMG / "screen-saver.png", optimize=True)
    for n, _ in order:
        Image.open(SHOTS / f"{n}.png").convert("RGB").save(IMG / f"screen-{n}.png", optimize=True)
    (ROOT / "docs").mkdir(exist_ok=True)
    c = card()
    c.save(ROOT / "docs/card.png", optimize=True)
    print("screens.png", grid(tiles, 3).size, "card.png", (ROOT / "docs/card.png").stat().st_size, "B")


if __name__ == "__main__":
    main()
