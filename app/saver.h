/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 PolarPatch */
/* The screensaver: after a minute untouched, a short film of a crowd at a concert, dithered black and white, plays
 * over the whole screen, round and round (as Arctic Scream's northern lights; the first touch only wakes the screen).
 * The film is the resource region's (fm1.toml resources: media/again_saver.bin, made by tools/again_saver.py from the
 * USB002 tour's creative drop: not GPL, see LICENSING.md); without it, no screensaver.
 * Each frame is a raw DEFLATE stream of ops that rebuild it over the frame before (the format: tools/again_saver.py).
 * The film and its decoder: film.h.
 * Part of app.c (the unity build). Main loop only. */

#include "film.h"

#if PLAT_SMK37   /* the SMK-37: no resource region (the board's flash map has none yet), so no film */
static void sv_init(void) { sv_film(0, 0); }
#else
static void sv_init(void) { uint32_t len; const uint8_t *p = plat_res(&len); sv_film(p, len); }
#endif   /* (the resource region) */

/* sv_fb on the screen, a band of the canvas at a time */
static void sv_draw(void)
{
    uint32_t y0, rows = CV_MAX / 240u;
    for (y0 = 0; y0 < 240u; y0 += rows) {
        uint32_t h = 240u - y0 < rows ? 240u - y0 : rows, y, x;
        cv_begin(240, h, C_BLACK);
        for (y = 0; y < h; y++) {
            const uint8_t *r = sv_fb + (y0 + y) * 30u;
            uint16_t *px = cv_px + y * 240u;
            for (x = 0; x < 240u; x++)
                px[x] = r[x >> 3] & 0x80u >> (x & 7u) ? 0xFFFFu : 0x0000u;
        }
        cv_blit(0, y0);
    }
}

static void sv_frame(uint32_t now)
{
    if (now - sv.t < sv.ms)
        return;
    sv.t = now;
    if (sv_decode(sv.at)) {                      /* (a bad frame: the film from its start) */
        sv.at = 0;
        return;
    }
    sv_draw();
    sv.at = (sv.at + 1u) % sv.nf;
}
