/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 PolarPatch */
/* The boot screen, after Arctic Scream p0larpatch's (its ui.c ui_boot: the storm, the light, the timing) and its site's
 * glitching titles: FM-1 and AGAIN in the pixfont at four times its size come out of the dark in a storm of torn rows
 * that settles; then the letters stand whole and white, and ghosts of them in ice, plasma and violet are torn into view
 * beside them in thin bands, a few at a time at random moments (the glitch around the letters, never in them). Under
 * the title the four colour lines draw out from their middles, and p0larpatch fades in under them in ice. BOOT_MS
 * long, or until the first touch. The pixfont: Press Start 2P (SIL OFL 1.1), app/gen/again_pixfont.h.
 * Part of app.c (the unity build). Main loop only. */
#include "gen/again_pixfont.h"

#define BOOT_MS 5600u
#define BOOT_FRAME_MS 55u                       /* a frame: ~18 a second */
#define BT_Y1 50                                /* FM-1's top, */
#define BT_Y2 92                                /* AGAIN's, */
#define BT_LY 140                               /* the lines', */
#define BT_PY 158                               /* p0larpatch's */
#define BT_TY (BT_Y1 - 2)                       /* the band the title is drawn in */
#define BT_TH (BT_Y2 + 34 - BT_TY)
#define BT_ICE RGB(0x22, 0xd3, 0xee)
#define BT_PLASMA RGB(0xff, 0x1a, 0x8c)
#define BT_VIOLET RGB(0xa7, 0x8b, 0xfa)
#define BT_GREEN RGB(0x3f, 0xb9, 0x50)
static const uint16_t BT_K[] = {RGB(0xea, 0xf6, 0xfa), BT_ICE, BT_PLASMA, BT_VIOLET};   /* white, then the ghosts */
static const uint16_t BT_LINES[4] = {BT_ICE, BT_GREEN, BT_VIOLET, BT_PLASMA};
static const char *const BT_WORD[2] = {"FM-1", "AGAIN"};
/* The three ghosts, as PolarPatch BlueWatch's CONFIG title: each a copy of the whole title in its colour, shifted a
 * little sideways (so it shows beside the letters, never in them), cut into view as one band of the title for a few
 * hundredths of its own cycle, the three cycles apart so the tears never land together. A band: from, to (% of the
 * cycle), top, bottom (% of the title's height). */
#define BT_NG 3
typedef struct { uint8_t from, to, top, bot; } bt_band;
static const struct { uint16_t ms; int8_t dx; uint8_t k, n; bt_band b[3]; } BT_GHOST[BT_NG] = {
    {2600, -7, 1, 3, {{15, 21, 8, 26}, {53, 59, 46, 68}, {86, 92, 70, 88}}},   /* ice */
    {3100, 7, 2, 3, {{23, 29, 32, 48}, {62, 68, 76, 92}, {94, 99, 12, 32}}},   /* plasma */
    {4300, 4, 3, 2, {{41, 47, 58, 74}, {79, 85, 20, 38}, {0, 0, 0, 0}}},       /* violet */
};
static struct {
    uint8_t on;
    uint32_t t0, at, rnd, k, pk, ln;            /* since; the last frame; the dice; the title's light (0 .. 64);
                                                 * p0larpatch's (0 .. 8); the lines' length */
    int8_t tear[2][16];                         /* the storm: each word's 2 px bands, shifted */
    uint8_t force;                              /* (everything drawn again at the next frame: the mirror asked) */
    struct { int16_t top, bot; int8_t dx; } g[BT_NG];   /* each ghost now: title rows top .. bot (none: top >= bot) */
} bt;

static uint32_t bt_rnd(uint32_t n)
{
    bt.rnd ^= bt.rnd << 13, bt.rnd ^= bt.rnd >> 17, bt.rnd ^= bt.rnd << 5;
    return bt.rnd % n;
}
static uint16_t bt_mix(uint16_t a, uint16_t b, uint32_t t, uint32_t n)   /* RGB565 a .. b, t of n */
{
    uint32_t r = ((a >> 11) * (n - t) + (b >> 11) * t) / n, g = ((a >> 5 & 63u) * (n - t) + (b >> 5 & 63u) * t) / n,
             bl = ((a & 31u) * (n - t) + (b & 31u) * t) / n;
    return (uint16_t)(r << 11 | g << 5 | bl);
}
static uint16_t bt_c(uint32_t k) { return bt_mix(C_BLACK, BT_K[k], bt.k, 64u); }
/* a word's rows ya .. yb (px of its 32) at canvas row y, shifted dx */
static void bt_rows(int32_t y, const char *s, int32_t ya, int32_t yb, int32_t dx, uint16_t c)
{
    int32_t n = 0, x, r, b, b0, t, u;
    while (s[n])
        n++;
    for (x = (240 - n * 32) / 2 + dx; *s; s++, x += 32) {
        const uint8_t *g = PIXFONT[(uint8_t)*s - 32u];
        for (r = ya / 4; r < 8 && r * 4 < yb; r++) {
            t = r * 4 > ya ? r * 4 : ya, u = r * 4 + 4 < yb ? r * 4 + 4 : yb;
            for (b = 0; b < 8; b = b0) {        /* (a run of pixels: one rect) */
                for (b0 = b; b0 < 8 && g[r] & (0x80u >> b0); b0++)
                    ;
                if (b0 > b)
                    cv_rect(x + b * 4, y + t, (b0 - b) * 4, u - t, c);
                else
                    b0++;
            }
        }
    }
}
static void bt_draw_title(void)
{
    uint32_t w, b, i;
    int32_t y[2] = {BT_Y1 - BT_TY, BT_Y2 - BT_TY};
    cv_begin(240, BT_TH, C_BLACK);
    for (i = 0; i < BT_NG; i++)                 /* the ghosts behind: only their edges show beside the letters */
        for (w = 0; w < 2u && bt.g[i].top < bt.g[i].bot; w++) {
            int32_t a = bt.g[i].top - (y[w] - y[0]), z = bt.g[i].bot - (y[w] - y[0]);   /* (in the word's rows) */
            a = a < 0 ? 0 : a, z = z > 32 ? 32 : z;
            if (a < z)
                bt_rows(y[w], BT_WORD[w], a, z, bt.g[i].dx, bt_c(BT_GHOST[i].k));
        }
    for (w = 0; w < 2u; w++)                    /* the letters, whole but for the storm's tears */
        for (b = 0; b < 16u; b++)
            bt_rows(y[w], BT_WORD[w], (int32_t)b * 2, (int32_t)b * 2 + 2, bt.tear[w][b], bt_c(0));
    cv_blit(0, BT_TY);
}
static void bt_draw_name(void)                  /* p0larpatch: the pixfont at twice its size */
{
    const char *s = "p0larpatch";
    int32_t x = 40, r, b;
    uint16_t c = bt_mix(C_BLACK, BT_ICE, bt.pk, 8u);
    cv_begin(240, 16, C_BLACK);
    for (; *s; s++, x += 16)
        for (r = 0; r < 8; r++)
            for (b = 0; b < 8; b++)
                if (PIXFONT[(uint8_t)*s - 32u][r] & (0x80u >> b))
                    cv_rect(x + b * 2, r * 2, 2, 2, c);
    cv_blit(0, BT_PY);
}
static void bt_draw_lines(void)                 /* four lines, 44 px, growing from their middles */
{
    uint32_t t;
    cv_begin(240, 2, C_BLACK);
    for (t = 0; t < 4u && bt.ln; t++)
        cv_rect(23 + (int32_t)t * 50 + 22 - (int32_t)bt.ln / 2, 0, (int32_t)bt.ln, 2, BT_LINES[t]);
    cv_blit(0, BT_LY);
}
/* the boot screen at e ms: the storm dying down, the ghosts on their cycles; 1: the title changed */
static int bt_state(uint32_t e)
{
    uint32_t g = 0, i, j, w, b, k0 = bt.k;
    int32_t th = BT_Y2 + 32 - BT_Y1;            /* the title's height: FM-1, the gap, AGAIN */
    int changed = 0;
    if (e < 1700u)
        g = e < 300u ? 64u : 64u - (e - 300u) * 64u / 1400u;   /* the storm */
    bt.k = e < 250u ? 0u : e < 1100u ? (e - 250u) * 64u / 850u : 64u;
    if (g && bt_rnd(80) < g)
        bt.k = bt.k * (1u + bt_rnd(3)) / 4u;    /* (it flickers in the storm) */
    changed |= bt.k != k0;
    for (w = 0; w < 2u; w++)                    /* the storm's tears in the letters: gone once it settled */
        for (b = 0; b < 16u; b++) {
            int8_t t = 0;
            if (g && bt_rnd(64) < g / 3u)
                t = (int8_t)((int32_t)bt_rnd(2u * (2u + g / 4u) + 1u) - (int32_t)(2u + g / 4u));
            changed |= t != bt.tear[w][b];
            bt.tear[w][b] = t;
        }
    for (i = 0; i < BT_NG; i++) {               /* the ghosts: where each one's cycle is */
        uint32_t pc = (e % BT_GHOST[i].ms) * 100u / BT_GHOST[i].ms;
        int16_t top = 0, bot = 0;
        int8_t dx = (int8_t)(BT_GHOST[i].dx * (g ? 3 : 1));   /* (thrown further in the storm) */
        for (j = 0; j < BT_GHOST[i].n; j++)
            if (pc >= BT_GHOST[i].b[j].from && pc < BT_GHOST[i].b[j].to)
                top = (int16_t)(th * BT_GHOST[i].b[j].top / 100), bot = (int16_t)(th * BT_GHOST[i].b[j].bot / 100);
        if (g && bt_rnd(3) == 0) {              /* (the storm tears them in at random too) */
            top = (int16_t)bt_rnd((uint32_t)th - 6u);
            bot = (int16_t)(top + 4 + (int16_t)bt_rnd(10));
        }
        changed |= top != bt.g[i].top || bot != bt.g[i].bot || dx != bt.g[i].dx;
        bt.g[i].top = top, bt.g[i].bot = bot, bt.g[i].dx = dx;
    }
    bt.pk = e < 1300u ? 0u : e < 2100u ? (e - 1300u) * 8u / 800u : 8u;
    bt.ln = e < 1000u ? 0u : e < 1800u ? (e - 1000u) * 44u / 800u : 44u;
    return changed;
}
static void boot_start(uint32_t now)
{
    bt.on = 1;
    bt.t0 = bt.at = now;
    bt.rnd = 0x9E3779B9u ^ now;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    bt_state(0);
}
/* a frame of it; 0 once it is over */
static int boot_frame(uint32_t now)
{
    uint32_t k = bt.k, pk = bt.pk, ln = bt.ln;
    if (now - bt.t0 >= BOOT_MS) {
        bt.on = 0;
        return 0;
    }
    if (now - bt.at < BOOT_FRAME_MS)
        return 1;
    bt.at = now;
    if (bt.force) {                              /* (the mirror asked for all of it) */
        bt.force = 0;
        lcd_fill(0, 0, 240, 240, C_BLACK);
        bt_state(now - bt.t0);
        bt_draw_title();
        bt_draw_name();
        bt_draw_lines();
        return 1;
    }
    if (bt_state(now - bt.t0) || bt.k != k)
        bt_draw_title();
    if (bt.pk != pk)
        bt_draw_name();
    if (bt.ln != ln)
        bt_draw_lines();
    return 1;
}
