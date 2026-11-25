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

## Tried 2026-11-25: FM-1 Again on the branch

FM-1 Again's app (synced into app/, board = "smk37") does not build on `smk37-inhouse`: the branch (2026-11-06) is
older than fm1-base main's companion files and parameters (plat_file_t, plat_file_info_t, plat_param_t,
PLAT_FILE_HEAD, PLAT_FK_* / PLAT_FF_*), which FM-1 Again uses for CHOP's sample, the editor and backup. So the port
waits for the SMK-37 board to come to fm1-base main (or the branch to be brought up to it): Charles Vestal's. A
backport here would fork fm1-base. Locally, the branch's gen_font also needs Homebrew's FriBiDi on the path (main's
build.py sets DYLD_FALLBACK_LIBRARY_PATH; the branch's does not).
