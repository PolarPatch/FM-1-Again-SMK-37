# FM-1 Again SMK-37: developer notes

- `base/` is fm1-base at its `smk37-inhouse` branch (eafac16, 2026-11-06): `board = "smk37"`, `model = pro|elite|mke`.
  Not in fm1-base's main yet (Charles Vestal merges it); FM-1 Again itself stays on main.
- The SMK's API (base/include/plat.h, PLAT_SMK37): the FM-1's controls as on the FM-1 (27 middle keys, 14 buttons,
  seven encoders K6 K7 K8 K1..K4, a virtual MASTER on K5) plus B_KBANK, B_FBANK, B_LEFT, B_RIGHT; plat_keys37 (C3..C6),
  plat_pads / plat_pad_vel / plat_pad_rgb (16 RGB pads), plat_fader (4), plat_wheel (pitch, mod), plat_pedal; TO_TRS.
- Its flash differs: data area 0xC2000..0xE9FFF, XDATA1 0xA1000..0xBAFFF with PLAT_OWNER 0xAF000 refused, the
  settings pair at PLAT_GLOB 0xEB000: CHOP's sample and the screensaver's film need new places.
- Plan: FM-1 Again's app with `#if PLAT_SMK37` parts (pads: PERFORM's tricks and CHOP's slices, lit; faders: KEYS,
  VOICE, DRUMS, SPACE levels, taking over once moved; wheels: pitch and FILTER; the outer keys below and above), the
  control layout of Arctic Scream SMK-37 (PolarPatch), its browser panel (web/smk_panel.js) for a demo.

## The port, 2026-11-27

- `base/` is fm1-base's `smk37-inhouse` at 1842e0b (it merged main 8820d6f on 2026-11-25, so the companion files and
  parameters are there now, and the own Wi-Fi stack and Ableton Link). FM-1 Again's `app/` is synced from the FM-1 repo
  (1.0.7: Link, NETMIDI / RTP-MIDI, Bluetooth MIDI) and committed here; the SMK's differences are `#if PLAT_SMK37`:
  - `app.h` APP_STORE_MAP: the settings at PLAT_GLOB 0xEB000, the panel / Bluetooth / Wi-Fi pairs at 0xE4000, 0xE6000,
    0xE2000 (the data area's top).
  - `app.c` CH_FILE: CHOP's sample at the data area's foot, 0xC2000 .. 0xD8FFF (92 KiB, as the FM-1's XDATA1).
  - `saver.h`: no film (no resource region on the SMK-37: the build refuses `resources` there), `fm1.toml`
    `resources` commented out; `tests/saver.txt` fails here for that reason.
  - `fm1.toml` board = "smk37", model = "pro".
- The SMK's flash, never written by this app: 0x0..0x3FFF, 0xC0000..0xC1FFF (calibration), 0xF4000..0xFDFFF,
  0xFF000 on; PLAT_OWNER 0xAF000 is refused by the platform.
- The boot raises VDC14 / SYSVDD and runs fm1_power_stock / fm1_clock_stock (main_fm1.c, both boards): the
  Wi-Fi + Bluetooth crash Arctic Scream SMK-37 had without them.
- Build: `/usr/bin/python3 base/tools/build.py app` -> `app/build/again-smk37.fwsc`; host:
  `--host` -> `app/build/host/again-smk37_host`. Before a flash: a full backup
  (`sudo -n /usr/local/smk37-backup/smk37-read backup`).
- Next: the pads (PERFORM's tricks, CHOP's slices), faders (KEYS / VOICE / DRUMS / SPACE), wheels (pitch, FILTER),
  the outer keys; packages for elite and mke; an `smk37` block for the hub.
