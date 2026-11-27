/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 PolarPatch */
/* The waveform: the sound as an editor draws a track, peaks dark blue and RMS lighter inside, left over right, on
 * grey. One column for each 1/232 of 16 bars of the clock, drawn as it plays: a column is written over last time's,
 * so the screen always holds 16 bars. The audio interrupt measures (wv_feed), the frame draws (wv_draw_*).
 * Part of app.c (the unity build). */

#define WV_W 232                                /* columns, x 4 .. 235 */
#define WV_X 4
#define WV_TOP 72                                /* the panel: title bar, lane L, a rule, lane R (then the pads' strip) */
#define WV_TITLE_H 16
#define WV_LANE_H 44
#define WV_HALF 21                               /* the most a column reaches from its lane's middle */
#define WV_L_Y (WV_TOP + WV_TITLE_H)             /* 88 */
#define WV_RULE_Y (WV_L_Y + WV_LANE_H)           /* 132 */
#define WV_R_Y (WV_RULE_Y + 1)                   /* 133 */

#define WV_BG RGB(192, 192, 192)
#define WV_PEAK RGB(50, 50, 200)
#define WV_RMS RGB(100, 100, 218)
#define WV_BAR RGB(228, 228, 228)
#define WV_FRAME RGB(150, 150, 150)
#define WV_INK RGB(24, 24, 24)

static uint8_t wv_pk[2][WV_W], wv_rms[2][WV_W];  /* each column's peak and RMS, 0 .. WV_HALF */
static volatile uint32_t wv_head;                /* the column being measured */
static float wv_peak[2], wv_sum[2];
static uint32_t wv_n;

/* audio interrupt: n frames of the mix, -1 .. 1; the clock at pos (sixteenths, 0 .. 256), dpos more each frame */
static void wv_feed(const float *l, const float *r, uint32_t n, float pos, float dpos)
{
    uint32_t i, c;
    for (i = 0; i < n; i++) {
        float a = fm_fabsf(l[i]), b = fm_fabsf(r[i]);
        if (a > wv_peak[0])
            wv_peak[0] = a;
        if (b > wv_peak[1])
            wv_peak[1] = b;
        wv_sum[0] += l[i] * l[i];
        wv_sum[1] += r[i] * r[i];
        wv_n++;
        c = (uint32_t)(pos * (float)WV_W / 256.0f) % WV_W;
        pos += dpos;
        if (c != wv_head) {                      /* the column is done: keep it, start the next */
            uint32_t h = wv_head, k;
            for (k = 0; k < 2u; k++) {
                float p = fm_minf(wv_peak[k], 1.0f), q = fm_sqrtf(wv_sum[k] / (float)(wv_n ? wv_n : 1u));
                wv_pk[k][h] = (uint8_t)(p * (float)WV_HALF + 0.5f);
                wv_rms[k][h] = (uint8_t)(fm_minf(q, 1.0f) * (float)WV_HALF + 0.5f);
                wv_peak[k] = wv_sum[k] = 0.0f;
            }
            wv_n = 0;
            wv_head = c;
        }
    }
}

/* column c begins a beat (1) or a bar (2) of the 16 bars (0: neither): the ticks along the lanes' edges */
static uint32_t wv_tick(uint32_t c)
{
    if (!c || c * 16u / WV_W != (c - 1u) * 16u / WV_W)
        return 2;
    return c * 64u / WV_W != (c - 1u) * 64u / WV_W;
}

/* one lane of column c at canvas x, canvas row y */
static void wv_lane(int32_t x, int32_t y, uint32_t ch, uint32_t c)
{
    int32_t mid = y + WV_LANE_H / 2, p = wv_pk[ch][c], q = wv_rms[ch][c], t = (int32_t)wv_tick(c) * 3 - 1;
    cv_rect(x, y, 1, WV_LANE_H, WV_BG);
    if (!p)
        cv_pset(x, mid, WV_PEAK);               /* silence: the line through the middle */
    else {
        cv_rect(x, mid - p, 1, 2 * p + 1, WV_PEAK);
        if (q)
            cv_rect(x, mid - q, 1, 2 * q + 1, WV_RMS);
    }
    if (t > 0) {                                 /* the beats ticked, the bars' longer (as a CDJ counts them) */
        cv_rect(x, y, 1, t, WV_INK);
        cv_rect(x, y + WV_LANE_H - t, 1, t, WV_INK);
    }
}

static void wv_draw_title(const char *name)
{
    cv_begin(240, WV_TITLE_H, WV_FRAME);
    cv_rect(1, 1, 238, WV_TITLE_H - 1, WV_BAR);
    cv_text(6, 1, &AG_S, name, WV_INK);
    cv_text(222, 1, &AG_S, "...", WV_INK);
    cv_blit(0, WV_TOP);
}

/* the whole panel's lanes */
static void wv_draw_all(void)
{
    uint32_t c;
    cv_begin(240, WV_LANE_H, WV_FRAME);          /* lane L */
    cv_rect(1, 0, 238, WV_LANE_H, WV_BG);
    for (c = 0; c < WV_W; c++)
        wv_lane(WV_X + (int32_t)c, 0, 0, c);
    cv_blit(0, WV_L_Y);
    cv_begin(240, WV_LANE_H + 1, WV_FRAME);      /* the rule, lane R */
    cv_rect(1, 0, 238, 1, C_BLACK);
    cv_rect(1, 1, 238, WV_LANE_H, WV_BG);
    for (c = 0; c < WV_W; c++)
        wv_lane(WV_X + (int32_t)c, 1, 1, c);
    cv_blit(0, WV_RULE_Y);
}

/* CHOP's page: the sample in the panel's place (wv_held: the columns wait, drawn whole again after) */
static uint8_t wv_held;
static uint8_t wv_ov[WV_W];                      /* the sample's peaks, a column each, 0 .. WV_LANE_H - 2 */

/* the sample s's overview (the main loop: decodes it once) */
static void wv_overview(const ch_sample_t *s)
{
    adp_t d;
    uint32_t c, i = 0;
    int32_t pk = 0;
    d.at = 0;
    for (c = 0; c < WV_W; c++) {
        uint32_t to = s->n * (c + 1u) / WV_W;
        pk = 0;
        for (; i < to; i++) {
            int32_t v = adp_next(s->blocks, &d);
            v = v < 0 ? -v : v;
            pk = v > pk ? v : pk;
        }
        wv_ov[c] = (uint8_t)(pk * (WV_LANE_H - 2) / 32768);
    }
}

/* the sample, its slices (their numbers over them; "<" before a slice played backwards), the one played last lit */
static void wv_draw_sample(const ch_sample_t *s, uint32_t lit, uint32_t rev)
{
    uint32_t c, k, h = 2u * WV_LANE_H + 1u;
    int32_t mid = (int32_t)h / 2;
    cv_begin(240, h, WV_FRAME);
    cv_rect(1, 0, 238, h, WV_BG);
    if (s->blocks && s->n) {
        for (k = 0; k < s->nslices; k++) {
            int32_t x0 = WV_X + (int32_t)(s->slice[k] * WV_W / s->n);
            int32_t x1 = WV_X + (int32_t)(s->slice[k + 1u] * WV_W / s->n);
            char t[5];
            if (k == lit)
                cv_rect(x0, 0, x1 - x0, h, WV_BAR);
            cv_rect(x0, 0, 1, h, WV_INK);
            if (x1 - x0 >= 14) {
                char *u = t + 1;
                t[1] = (char)('0' + (k + 1u) / 10u);
                t[2] = (char)('0' + (k + 1u) % 10u);
                t[3] = 0;
                if (k + 1u < 10u)
                    u++;
                if (rev >> k & 1u)
                    *--u = '<';
                cv_text(x0 + 2, 1, &AG_S, u, WV_INK);
            }
        }
        for (c = 0; c < WV_W; c++) {
            int32_t p = wv_ov[c];
            cv_rect(WV_X + (int32_t)c, mid - p, 1, 2 * p + 1, WV_PEAK);
        }
    }
    cv_blit(0, WV_L_Y);
}

/* the columns finished since the last call, and the playhead after them */
static void wv_draw_new(void)
{
    static uint32_t done;                        /* the next column to draw */
    uint32_t head = wv_head, c;
    if (done == head)
        return;
    if (wv_held) {                               /* (CHOP's page: not now) */
        done = head;
        return;
    }
    while (done != head) {
        c = done;
        cv_begin(2, WV_LANE_H * 2 + 1, C_BLACK);
        wv_lane(0, 0, 0, c);
        wv_lane(0, WV_LANE_H + 1, 1, c);
        if (c + 1u < WV_W) {                     /* the playhead: a dark line where the next column comes */
            cv_rect(1, 0, 1, WV_LANE_H, WV_INK);
            cv_rect(1, WV_LANE_H + 1, 1, WV_LANE_H, WV_INK);
        }
        cv_blit(WV_X + c, WV_L_Y);
        done = (done + 1u) % WV_W;
    }
}
