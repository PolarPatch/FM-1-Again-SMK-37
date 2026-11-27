/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * FM-1 Again changes: Copyright (C) 2026 PolarPatch */
/* FM-1 Again: the app. Started from fm1-base's template (its MIDI in, settings, Bluetooth, screen reader and
 * velocity handling kept); the sound is FM-1 Again's own engine (engine/, dsp/: GPL-3.0, so the firmware as a whole
 * is GPL-3.0): a groove of synthesized drums with BUILD / DROP, chords on KEYS, VOX's chopped syllables, the PUMP
 * over all of it and SPACE after it. The keys play KEYS, VOX or the DRUMS (ALGORITHM chooses). Pages (black and
 * white, big type): HOME, GROOVE (SEQ), KEYS (EDIT), VOX (ENV), PUMP (LFO), SPACE (FX), SETUP (GLO); K1-K4 their
 * four values along the bottom, PRESET the page's choice. PLAY starts and stops; ARP builds and drops.
 * Part of the unity build (device/unity.c, host/plat_host.c), after the platform: gfx.c and announce.c are in
 * scope. */
#include "fastmath.h"
#include "gen/again_font.h"   /* TeX Gyre Heros Bold, plain and stamped (tools/gen_again_font.py) */
/* the groove's notes out (engine/groove.h), over USB and Bluetooth, when NOTES OUT is on (app.c below) */
static void gr_out(uint32_t ch, uint32_t st, uint32_t d1, uint32_t d2);
static void nm_put(uint32_t pkt, int render);   /* MIDI over Wi-Fi (net.h) */
static int nm_get(uint32_t *pkt);
#define GR_NOTE_OUT(ch, note, vel) gr_out((ch), (vel) ? 0x90u : 0x80u, (note), (vel))
#define GR_CC_OUT(ch, cc, val) gr_out((ch), 0xB0u, (cc), (val))
#include "core.c"             /* the sound, apart from the FM-1 (shared with FM-1 Again VIBE) */
static void gr_out(uint32_t ch, uint32_t st, uint32_t d1, uint32_t d2)   /* (the audio interrupt: the render may send) */
{
    if (!set.p[PA_NOTESOUT])
        return;
    plat_midi_send((st >> 4) | (st | ch) << 8 | (d1 & 127u) << 16 | (d2 & 127u) << 24, TO_USB | TO_BLE);
    nm_put((st >> 4) | (st | ch) << 8 | (d1 & 127u) << 16 | (d2 & 127u) << 24, 1);
}
#include "wave.h"
#define APP_REDRAW_FAST_FOR 3000u
static uint32_t redraw_last;                     /* (the mirror's last whole screen, app_redraw; 0: the next at once) */
static uint32_t redraw_fast_until;               /* (the screensaver just gone: the mirror's mends come quickly until then) */
#include "saver.h"
#include "boot.h"

/* MIDI clock in (SYNC AUTO: a clock that comes in leads), after Dinghy 0.6's kit_clock.h (zvenson, GPL-3.0) and
 * OberVave's: each tick taken where it landed (plat_midi_in_t24, fm1.toml midi_stamp), the tempo measured over a
 * quarter (24 ticks) in the FM-1's 24 MHz clock and smoothed, our sixteenths held on the master's by a proportional
 * pull once a sixteenth (6 ticks), counted from where the master was when the tick came (its age at this block's
 * start). START: the first bar's first step; CONTINUE: on; STOP: off. CLOCK OUT: 24 ticks a quarter at our own tempo,
 * START and STOP, to USB (plat_midi_send from the render: within the 2 kHz poll). Audio interrupt, but sy_check. */
#define SY_T24 24000000.0f
#ifdef PLAT_HOST                                 /* (the host's ticks are the computer's real time, not the simulation's:
                                                  * its milliseconds instead, so a script's clock can be followed) */
static uint32_t sy_now(void) { return plat_ms() * 24000u; }
#define sy_stamp() sy_now()
#else
#define sy_now() plat_ticks24()
#define sy_stamp() plat_midi_in_t24()
#endif
static struct {
    uint32_t ticks, q0, last, n;                 /* ticks since START; the quarter's first tick's time; the last's; ticks
                                                  * timed (0: no quarter measured yet) */
    float bpm;                                   /* the master's tempo, smoothed */
    uint8_t here;                                /* a clock is coming in (a tick in the last half second) */
    float out_acc;                               /* CLOCK OUT: ticks owed */
    uint8_t out_run;                             /* CLOCK OUT: the run sent last (START / STOP) */
} sy;
static void sy_tick(uint32_t t)
{
    if (sy.n && t - sy.last > (uint32_t)(SY_T24 / 2.0f))
        sy.n = 0;                                /* (a gap: start the measuring again) */
    sy.last = t, sy.here = 1;
    if (!sy.n)
        sy.q0 = t;
    sy.n++;
    if (sy.n > 24u) {                            /* a quarter: the tempo */
        float b = 60.0f * SY_T24 / (float)(t - sy.q0);
        if (b > 30.0f && b < 300.0f)
            sy.bpm = sy.bpm > 0.0f ? sy.bpm + (b - sy.bpm) * 0.3f : b;
        sy.q0 = t, sy.n = 1;
    }
    sy.ticks++;
    if (sy.bpm > 0.0f)
        bpm_base = sy.bpm - gr.bpm_add;          /* (the master's tempo: BUILD's RUSH does not move it) */
    if (ck.run && sy.ticks % 6u == 0u && sy.bpm > 0.0f) {   /* a sixteenth of the master's: pull ours onto it */
        float age = (float)(sy_now() - t) * (SR / SY_T24);   /* (frames since it came) */
        float ours = (float)(ck.step + 1u) - ck.left / ck.len, theirs = (float)(sy.ticks / 6u) + age / ck.len;
        float err = ours - theirs;
        if (err > -2.0f && err < 2.0f)
            ck.left += err * ck.len * 0.25f;
    }
}
static void sy_check(void)                       /* (the main loop) the clock gone: our own tempo again */
{
    static uint8_t was;
    if (sy.here && sy_now() - sy.last > (uint32_t)(SY_T24 / 2.0f))
        sy.here = 0, sy.n = 0, sy.bpm = 0.0f;
    if (was && !sy.here)
        apply();
    was = sy.here;
}
static void sy_out(uint32_t n)                   /* (the render) CLOCK OUT for a block of n frames */
{
    uint32_t i, k;
    if (!set.p[PA_CLKOUT] || sy.here) {          /* (off, or a master leads: one master) */
        sy.out_run = ck.run;
        return;
    }
    if (ck.run != sy.out_run) {
        plat_midi_send(0x0Fu | (ck.run ? 0xFAu : 0xFCu) << 8, TO_USB);
        sy.out_run = ck.run, sy.out_acc = 0.0f;
    }
    sy.out_acc += (float)n * ck.bpm * 24.0f / (60.0f * SR);
    k = (uint32_t)sy.out_acc;
    sy.out_acc -= (float)k;
    for (i = 0; i < k; i++)
        plat_midi_send(0x0Fu | 0xF8u << 8, TO_USB);
}

static void midi_msg(uint32_t pkt, int port)  /* audio interrupt: a packet that came in */
{
    uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = pkt >> 24;
    if (set.p[PA_SYNC] == 0u && st >= 0xF8u && st <= 0xFCu) {   /* the clock and the transport (SYNC AUTO) */
        if (st == 0xF8u)
            sy_tick(sy_stamp());
        else if (st == 0xFAu) {
            sy.ticks = 0;
            ck_start();
        } else if (st == 0xFBu)
            ck.run = 1;
        else if (st == 0xFCu && ck.run) {
            ck.run = 0;
            gr_stop();
        }
    }
    if (st != 0xF8u && st != 0xFEu) {    /* (clock and active sensing would flood the screen) */
        midi_last = pkt;
        midi_last_port = (uint32_t)port;
        midi_count++;
    }
    if (st < 0xF0u && set.p[PA_MIDICH] && (st & 15u) != set.p[PA_MIDICH] - 1u)
        return;                              /* (CHANNEL: another channel's message) */
    if ((st & 0xF0u) == 0xB0u)              /* the mod wheel, volume, the pedal, all notes off */
        mb_cc(d1, d2);
    else if ((st & 0xF0u) == 0xE0u)         /* pitch bend */
        mb_bend(d1 | (d2 & 0x7Fu) << 7);
    else if ((st & 0xF0u) == 0x90u && d2)
        play_note((int32_t)d1, d2, 1, 1);
    else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u)
        play_note((int32_t)d1, 0, 0, 1);
}

static void midi_in(void)                    /* audio interrupt: the packets that came in */
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port))
        midi_msg(pkt, port);
    while (nm_get(&pkt))                         /* (MIDI over Wi-Fi: shown as port 3) */
        midi_msg(pkt, 3);
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    static float gain_now;                       /* glides to MASTER: no step at a block's edge */
    float gain = (float)master_q12 / 4096.0f * 8300000.0f;   /* MASTER, read once a block */
    float pos0, pos1;
    uint32_t i;
    if (n > APP_BLOCK)
        n = APP_BLOCK;
    midi_in();
    core_render(n, &pos0, &pos1);
    sy_out(n);
    wv_feed(b_kl, b_kr, n, pos0, (pos1 - pos0) / (float)n);
    for (i = 0; i < n; i++) {
        gain_now += (gain - gain_now) * 0.004f;                  /* ~5 ms: turning MASTER does not click */
        lr[2 * i] = (int32_t)(b_kl[i] * gain_now);
        lr[2 * i + 1] = (int32_t)(b_kr[i] * gain_now);
    }
}

void app_panic(void) { core_panic(); }

/* the pages: their names, their four knobs, PRESET's value */
enum { PG_HOME, PG_GROOVE, PG_RISE, PG_BUILD, PG_KEYS, PG_VOX, PG_CHOP, PG_PUMP, PG_SPACE, PG_ECHO, PG_SETUP, PG_TAPE,
       PG_MIX, PG_MIDI, PG_LINK, NPG };
static const struct {
    const char *title;                           /* the header's big word, as the posters set it */
    int8_t stamp;                                /* its stamped letter */
    uint8_t k[4], preset;
} PG[NPG] = {
    [PG_HOME] = {"FM-1 Again", 5, {PA_DEPTH, PA_MIX, PA_FILTER, PA_TEMPO}, PA_PAT},
    [PG_GROOVE] = {"Groove", 0, {PA_SWING, PA_VARY, PA_KICK, PA_HATS}, PA_PAT},
    [PG_RISE] = {"Rise", 0, {PA_RLEN, PA_RBRIGHT, PA_RSUCK, PA_RAUTO}, PA_RAUTO},
    [PG_BUILD] = {"Build", 0, {PA_BUILD, PA_BSTYLE, PA_TEMPOUP, PA_PLAYMODE}, PA_BSTYLE},
    [PG_KEYS] = {"Keys", 0, {PA_TONE, PA_DECAY, PA_PROG, PA_RHYTHM}, PA_CHORD},
    [PG_VOX] = {"Vox", 0, {PA_VOWELS, PA_GATE, PA_FORMANT, PA_BREATH}, PA_VOWELS},
    [PG_CHOP] = {"Chop", 0, {PA_SLICE, PA_LENGTH, PA_PITCH, PA_CMODE}, PA_CREV},
    [PG_PUMP] = {"Pump", 0, {PA_DEPTH, PA_RELEASE, PA_SHAPE, PA_SOURCE}, PA_SOURCE},
    [PG_SPACE] = {"Space", 0, {PA_SIZE, PA_DAMP, PA_MIX, PA_SHIMMER}, PA_SIZE},
    [PG_ECHO] = {"Echo", 0, {PA_ETIME, PA_EFB, PA_EWEAR, PA_EMIX}, PA_EPING},
    [PG_SETUP] = {"Setup", 0, {PA_KEY, PA_PADS, PA_KEYS_LV, PA_VOX_LV}, PA_SAVER},
    [PG_TAPE] = {"Tape", 0, {PA_TWOW, PA_TSAT, PA_TTONE, PA_THISS}, PA_TAPE},
    [PG_MIX] = {"Mix", 0, {PA_KICK, PA_HATS, PA_CLAP, PA_RISE_LV}, PA_PLAYMODE},
    [PG_MIDI] = {"Midi", 0, {PA_SYNC, PA_CLKOUT, PA_NOTESOUT, PA_MIDICH}, PA_SYNC},
    [PG_LINK] = {"Link", 0, {PA_WIFI, PA_LINK, PA_TEMPO, PA_NETMIDI}, PA_LINK},   /* (its second line: net.h lk_status) */
};
static const uint8_t PG_BUTTON[NPG] = {B_HOME, B_SEQ, B_SEQ, B_SEQ, B_EDIT, B_ENV, B_ENV, B_LFO, B_FX, B_FX, B_GLO, B_GLO,
                                       B_GLO, B_GLO, B_GLO};   /* (the same button again: the next of its group) */

/* the tutor's tips (draw_title), in order: each gone once done (set.tips) */
enum { TIP_PLAY, TIP_BUILD, TIP_ALGO, TIP_PADS, TIP_PAGES, TIP_SAVE, NTIP };

/* the controls by their names in plat.h, in its order */
static const char *const BTN_NAME[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                         "PLAY", "REC", "OCT-", "OCT+"};

/* --------------------------------------------------------------- settings --- */
static void settings_load(void)          /* the saved settings (what of them is right), or the defaults */
{
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC)
        settings_defaults();
    else
        settings_fix();
    if (set.page >= NPG)
        set.page = PG_HOME;
    apply();
}

/* CHOP's sample: the one sent over USB (the companion's file "chop"), else the factory's. One copy (A is B), in
 * XDATA1 (0xE5000 .. 0xFBFFF, 92 KiB: 8.2 s): the data area is the screensaver's film. A cut mid-write: the factory
 * phrase until it is sent again. (XDATA1's rule, plat.h: tools/again_chop.py keeps "AT" from each sector's 0xF06) */
#if PLAT_SMK37   /* the SMK-37: no film, so the data area's foot, 0xC2000 .. 0xD8FFF (the same 92 KiB; no 0xF00 rule) */
static const plat_file_t CH_FILE = {PLAT_DATA_LO, PLAT_DATA_LO, 0x17000u};
_Static_assert(PLAT_DATA_LO == 0xC2000u && PLAT_DATA_LO + 0x17000u <= 0xE2000u, "CHOP's file: below the store's pairs");
#else
static const plat_file_t CH_FILE = {PLAT_XDATA1_LO, PLAT_XDATA1_LO, PLAT_XDATA1_HI - PLAT_XDATA1_LO};
_Static_assert(PLAT_XDATA1_LO == 0xE5000u && PLAT_XDATA1_HI - PLAT_XDATA1_LO == 0x17000u, "CHOP's file: XDATA1");
#endif
static char ch_name[16];
static uint32_t ch_gen;                      /* a new sample each time (the screen draws it again) */
static void chop_load(void)
{
    ch_sample_t s;
    int n = plat_file_size(&CH_FILE);
    if (!(n > 0 && ch_parse(plat_file_data(&CH_FILE), (uint32_t)n, &s)))
        ch_parse(CHOP_FACTORY, sizeof CHOP_FACTORY, &s);
    memcpy(ch_name, s.name, sizeof ch_name);
    wv_overview(&s);
    ch_next = s;
    ch_gen++;
    ch_swap = 1;
}

void app_init(void)
{
    dr_init();
    settings_load();
    chop_load();
    ch.s = ch_next;
    ch_swap = 0;
}

/* --------------------------------------------------------------------- UI --- */
static struct {
    char msg[2][24];                     /* a short message over the header */
    uint32_t msg_until;
    uint32_t keys, btn;                  /* held at the last input */
    uint8_t vel[NKEYS], prs[NKEYS];      /* each key's last velocity, and its pressure while held */
    int32_t last_key;
    uint8_t looking;                     /* Bluetooth MIDI devices: REC pressed, the scan's list up (lib/btdev) */
    uint32_t rec_t;                      /* REC went down (0: not held, or its hold already acted) */
    int32_t turns[NE];                   /* detents since boot, per encoder */
    uint32_t lit_until[4];               /* a knob just turned: its cell lit */
    uint32_t trick_keys;                 /* the keys holding a PERFORM trick */
    uint8_t trick_of[NKEYS];             /* .. which */
    uint8_t sel_used;                    /* SEL held for a trick (not a tap) */
    uint8_t redraw, redraw_row, redraw_title;
    uint8_t full;                        /* everything drawn again (the screensaver gone) */
} ui;

/* key k (0: F3) among the white keys: 0, 1, .. (-1: a black key) */
static int32_t white_index(uint32_t k)
{
    static const int8_t W[12] = {0, -1, 1, -1, 2, -1, 3, 4, -1, 5, -1, 6};   /* F F# G G# A A# B C C# D D# E */
    return W[k % 12u] < 0 ? -1 : (int32_t)(k / 12u) * 7 + W[k % 12u];
}
/* .. among the black keys: 0 (F#3), 1, .. (-1: a white key) */
static int32_t black_index(uint32_t k)
{
    static const int8_t B[12] = {-1, 0, -1, 1, -1, 2, -1, -1, 3, -1, 4, -1};
    return B[k % 12u] < 0 ? -1 : (int32_t)(k / 12u) * 5 + B[k % 12u];
}
/* the trick key k holds (-1: none): a pad (a black key, PADS on) or, SEL held, a white key */
static int32_t key_trick(uint32_t k, int sel)
{
    int32_t b = black_index(k), w = white_index(k);
    if (set.p[PA_PADS] && b >= 0 && b < NPAD)
        return PAD_TRICK[b];
    if (sel && w >= 0 && w < NPF)
        return w;
    return -1;
}

static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}

static void int_text(int32_t v, char *b)     /* signed, no padding */
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}

void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[48];
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    ui.redraw = 1;
    if (ann_on()) {                          /* and to a screen reader (announce.c, fm1-reader) */
        t[0] = 0;
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            text_cat(t, " ");
            text_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

/* a parameter's value as the screen shows it (and a screen reader says it) */
static void value_text(uint32_t i, char *t)
{
    int32_t v = set.p[i];
    t[0] = 0;
    switch (PA[i].fmt) {
    case FMT_PCT:
        int_text(v * 100 / PA[i].top, t);
        text_cat(t, "%");
        break;
    case FMT_BPM:
        int_text(80 + v, t);
        break;
    case FMT_LIST:
        text_cat(t, PA[i].list[v]);
        break;
    case FMT_MS:
        int_text(i == PA_GATE ? 40 + v * 6 : 60 + v * 6, t);
        text_cat(t, "MS");
        break;
    case FMT_KEY:
        text_cat(t, KEY_NAME[v]);
        break;
    case FMT_BARS:
        text_cat(t, BARS_NAME[v]);
        text_cat(t, " BARS");
        break;
    case FMT_FILTER:
        if (v > 52)
            text_cat(t, "HP"), int_text((v - 52) * 100 / 48, t + 2);   /* ("HP33": one word, "FILTER HP33" read right) */
        else if (v < 48)
            text_cat(t, "LP"), int_text((48 - v) * 100 / 48, t + 2);
        else
            text_cat(t, "OPEN");
        break;
    case FMT_SIZE:
        if (v >= 127)
            text_cat(t, "INF");
        else {
            int32_t d = RV_RT60_X10[v];
            int_text(d / 10, t);
            text_cat(t, ".");
            int_text(d % 10, t + 0 + (d / 10 >= 10 ? 3 : 2));
            text_cat(t, "S");
        }
        break;
    case FMT_SEMI:
        if (v > 12)
            text_cat(t, "+");
        int_text(v - 12, t + (v > 12));
        break;
    case FMT_ONE:
        int_text(v + 1, t);
        break;
    case FMT_UP:                                 /* OFF, or +N BPM */
        if (!v) {
            text_cat(t, "OFF");
            break;
        }
        text_cat(t, "+");
        int_text(v, t + 1);
        text_cat(t, " BPM");
        break;
    default:
        int_text(v, t);
    }
}

static void octave_text(char *t)
{
    t[0] = set.octave < 0 ? '-' : '+';
    t[1] = (char)('0' + (set.octave < 0 ? -set.octave : set.octave));
    t[2] = 0;
}

/* a screen reader that has just started listening (or asks) hears the whole screen */
static void speak(void)
{
    char t[160], v[24];
    uint32_t k;
    if (ann_on() && ann_where()) {
        t[0] = 0;
        text_cat(t, set.page == PG_HOME ? "HOME" : PG[set.page].title);   /* (as a page change says it) */
        text_cat(t, ". ");
        for (k = 0; k < 4u; k++) {
            uint32_t p = PG[set.page].k[k];
            value_text(p, v);
            text_cat(t, PA[p].name);
            text_cat(t, " ");
            text_cat(t, v);
            text_cat(t, ". ");
        }
        text_cat(t, "KEYS PLAY ");
        text_cat(t, IN_NAME[set.inst]);
        if (ck.run)                              /* (after the knobs: a reader takes the first sentences as them) */
            text_cat(t, ". PLAYING");
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(plat_ms());
}

static void octave_step(int32_t d)
{
    int32_t o = clamp(set.octave + d, -2, 2);
    char t[4];
    if (o == set.octave)
        return;
    set.octave = (int8_t)o;
    set_dirty = 1;
    octave_text(t);
    app_say("OCTAVE", t);
}

int again_ble_off(void) { return set.ble_off; }

/* REC held a second: Bluetooth off or on (kept with the settings, so it stays so at the next power-on) */
static void ble_toggle(void)
{
    set.ble_off = (uint8_t)!set.ble_off;
    set_dirty = 1;
    plat_ble_enable(!set.ble_off);
    app_say("BLUETOOTH", set.ble_off ? "OFF" : "ON");
}

/* REC: a Bluetooth MIDI device (FM1B_BLE_CENTRAL, lib/btdev): look for devices, then connect to the one ALGORITHM
 * picks, then let it go */
static void ble_device_say(void)
{
    char name[21];
    if (btdev_found(btdev_pick(), name, 0))
        app_say("DEVICE", name);
    else
        app_say("BLE", "SCANNING");
}
static void ble_device_button(void)
{
    int ev;
    if (!btdev_busy() && !ui.looking) {
        ui.looking = 1;
        app_say("BLE", "SCANNING");
        return;
    }
    ev = btdev_save();
    ui.looking = 0;
    app_say(ev == BTDEV_CONNECTING ? btdev_word(ev) : "BLE", ev == BTDEV_CONNECTING ? btdev_name() : btdev_word(ev));
}

/* a parameter turned by d detents */
/* CHOP: the slice in hand (PITCHED: SLICE; SLICES: the one played last) */
static uint32_t chop_cur(void)
{
    uint32_t n = ch_next.nslices ? ch_next.nslices : 1u;
    return (set.p[PA_CMODE] == CM_PITCHED ? set.p[PA_SLICE] : ch.last_slice) % n;
}

/* parameter i to v (0 .. its top): what goes with it, the engine, the screen and a screen reader */
#include "net.h"                         /* Wi-Fi and Ableton Link */

static void param_set(uint32_t i, int32_t v)
{
    char t[24];
    v = clamp(v, 0, PA[i].top);
    if (v == set.p[i])
        return;
    set.p[i] = (uint8_t)v;
    if (i == PA_PAT)                             /* a new groove brings its own swing */
        set.p[PA_SWING] = (uint8_t)(GR_PAT[v].swing / 0.33f * 100.0f + 0.5f);
    if (i == PA_TAPE) {                          /* a tape: its four */
        set.p[PA_TWOW] = TP_SET[v][0];
        set.p[PA_TSAT] = TP_SET[v][1];
        set.p[PA_TTONE] = TP_SET[v][2];
        set.p[PA_THISS] = TP_SET[v][3];
    }
    if (i == PA_CREV)                            /* REVERSE: the slice in hand's bit */
        set.crev = v ? set.crev | 1u << chop_cur() : set.crev & ~(1u << chop_cur());
    set_dirty = 1;
    apply();
    if (i == PA_WIFI || i == PA_LINK)
        net_apply();
    else if (i == PA_TEMPO)
        lk_tempo();                              /* (Link: the session's tempo too) */
    value_text(i, t);
    if (i == PA_CREV) {                          /* "SLICE 3" "REVERSE" */
        char nm[12] = "SLICE ";
        int_text((int32_t)chop_cur() + 1, nm + 6);
        app_say(nm, t);
    } else {
        app_say(PA[i].name, t);
    }
    ui.redraw_row = ui.redraw_title = 1;
}
static void param_turn(uint32_t i, int32_t d) { param_set(i, (int32_t)set.p[i] + d); }

static void play_toggle(void)
{
    if (ck.run) {
        ck.run = 0;
        want_stop = 1;
        lk_play(0);
        app_say("STOPPED", "");
    } else if (lk_play(1)) {                 /* (Link with peers: at the next bar line, or not after all) */
        app_say(lk.arm_ub >= 0 ? "NEXT BAR" : "NOT STARTED", "");
    } else {
        ck_start();
        app_say("PLAYING", PAT_NAME[set.p[PA_PAT]]);
    }
}

static uint32_t key_ch(void) { return set.p[PA_MIDICH] ? set.p[PA_MIDICH] - 1u : 0u; }   /* the keys' channel out */

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), k, pg;
    int32_t e;
    touch_poll();                            /* the sensed keys' presses, velocities and pressure */
    keys = touch_keys();
    if (keys || btn || keys != ui.keys || btn != ui.btn)
        sv.touch = plat_ms();
    if (sv.on) {                             /* the screensaver: the first touch only wakes the screen */
        int any = keys != ui.keys || btn != ui.btn || keys || btn;
        for (k = 0; k < NE; k++)
            any |= plat_enc((int)k) != 0;
        if (any) {
            sv.on = 0;
            redraw_last = 0;                     /* (the mirror: the film's leftovers mended at its next ask) */
            redraw_fast_until = plat_ms() + APP_REDRAW_FAST_FOR;
            sv.touch = plat_ms();
            ui.full = 1;
            ui.keys = keys;                      /* (what is held now plays nothing; let go, nothing either) */
            ui.trick_keys &= keys;
            ui.btn = btn;
        }
        return;
    }
    for (k = 0; k < NKEYS; k++) {            /* key 0 is F3 (MIDI 53) */
        uint32_t bit = 1u << k, v;
        int32_t note = 53 + (int32_t)k + 12 * set.octave, w;
        if ((keys & bit) && !(ui.keys & bit) && (w = key_trick(k, btn >> B_SEL & 1u)) >= 0) {
            ui.trick_keys |= bit;                /* a pad, or SEL and a white key: a trick while held */
            ui.trick_of[k] = (uint8_t)w;
            ui.sel_used |= (uint8_t)(btn >> B_SEL & 1u);
            pfx.held |= 1u << w;
            set.tips |= 1u << TIP_PADS;
            app_say(PF_NAME[w], "");
            continue;
        }
        if (ui.trick_keys & bit) {               /* a trick's key: let go, the trick ends */
            if (!(keys & bit)) {
                ui.trick_keys &= ~bit;
                pfx.held &= ~(1u << ui.trick_of[k]);
            }
            continue;
        }
        if ((keys & bit) && !(ui.keys & bit) && (btn >> B_SEL & 1u))
            continue;                            /* (SEL and a key that is no trick: nothing) */
        if ((keys & bit) && !(ui.keys & bit)) {
            v = touch_vel(k);
            ui.vel[k] = (uint8_t)v;
            ui.prs[k] = 0;
            ui.last_key = (int32_t)k;
            play_note(note, v, 1, 0);
            plat_midi_send(0x09u | (0x90u | key_ch()) << 8 | (uint32_t)note << 16 | v << 24, TO_USB | TO_BLE);
            nm_put(0x09u | (0x90u | key_ch()) << 8 | (uint32_t)note << 16 | v << 24, 0);
        } else if (!(keys & bit) && (ui.keys & bit)) {
            ui.prs[k] = 0;
            play_note(note, 0, 0, 0);
            plat_midi_send(0x08u | (0x80u | key_ch()) << 8 | (uint32_t)note << 16, TO_USB | TO_BLE);
            nm_put(0x08u | (0x80u | key_ch()) << 8 | (uint32_t)note << 16, 0);
        } else if ((keys & bit) && touch_pressure(k) != ui.prs[k]) {   /* pressure: poly aftertouch */
            ui.prs[k] = touch_pressure(k);
            plat_midi_send(0x0Au | (0xA0u | key_ch()) << 8 | (uint32_t)note << 16 | (uint32_t)ui.prs[k] << 24, TO_USB | TO_BLE);
            nm_put(0x0Au | (0xA0u | key_ch()) << 8 | (uint32_t)note << 16 | (uint32_t)ui.prs[k] << 24, 0);
        }
    }
    for (k = 0; k < NB; k++) {               /* a button pressed */
        uint32_t bit = 1u << k;
        if (!(btn & bit) || (ui.btn & bit))
            continue;
        for (pg = 0; pg < NPG && PG_BUTTON[pg] != k; pg++)
            ;
        if (pg < NPG && PG_BUTTON[set.page] == k && set.page + 1u < NPG && PG_BUTTON[set.page + 1u] == k)
            pg = set.page + 1u;                  /* the button again: its next page */
        if (pg < NPG) {                      /* a page's button: that page */
            if (set.page != pg) {
                set.page = (uint8_t)pg;
                ui.redraw_row = ui.redraw_title = 1;
                set.tips |= 1u << TIP_PAGES;
            }
            app_say(pg == PG_HOME ? "HOME" : PG[pg].title, "");
            ui.msg_until = 0;                    /* (heard, not shown: the header names the page) */
        } else if (k == B_OCTDN)
            octave_step(-1);
        else if (k == B_OCTUP)
            octave_step(1);
        else if (k == B_REC && FM1B_BLE)
            ui.rec_t = plat_ms();                /* a press: on release (a hold switches Bluetooth) */
        else if (k == B_SAVE) {
            set.tips |= 1u << TIP_SAVE;
            if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0) {
                set_dirty = 0;
                app_say("SAVED", "");
            } else {
                app_say("SAVE", "FAILED");
            }
        } else if (k == B_PLAY)
            play_toggle();
        else if (k == B_ARP) {                   /* BUILD, then DROP */
            if (!ck.run)
                play_toggle();
            if (gr.bd == BD_BUILD) {
                gr_build_button();
                app_say("DROP", "AT THE BAR");
            } else {
                gr_build_button();
                app_say("BUILD", BARS_NAME[set.p[PA_BUILD]]);
                set.tips |= 1u << TIP_BUILD;
            }
        } else if (k == B_SEL) {
            ui.sel_used = 0;                     /* (said on its release, unless a trick was played) */
        }
    }
    if (!(btn >> B_SEL & 1u) && (ui.btn >> B_SEL & 1u) && !ui.sel_used)
        app_say("KEYS PLAY", IN_NAME[set.inst]);  /* SEL tapped: what the keys play */
    if (FM1B_BLE && ui.rec_t) {             /* REC: held a second, Bluetooth on / off; a press, a device */
        if (btn >> B_REC & 1u) {
            if (plat_ms() - ui.rec_t >= 1000u) {
                ui.rec_t = 0;
                ble_toggle();
            }
        } else {
            ui.rec_t = 0;
            if (FM1B_BLE_CENTRAL && !set.ble_off)
                ble_device_button();
            else
                app_say("BUTTON", "REC");
        }
    }
    for (k = 0; k < NE; k++) {               /* an encoder turned */
        if ((e = plat_enc((int)k)) == 0)
            continue;
        sv.touch = plat_ms();
        ui.turns[k] += e;
        if (k == EN_ALGO && FM1B_BLE_CENTRAL && ui.looking) {
            btdev_choose(btdev_pick() + e);
            ble_device_say();
        } else if (k == EN_ALGO) {           /* what the keys play */
            set.inst = (uint8_t)clamp((int32_t)set.inst + e, 0, NIN - 1);
            set_dirty = 1;
            set.tips |= 1u << TIP_ALGO;
            app_say("KEYS PLAY", IN_NAME[set.inst]);
        } else if (k == EN_SELECT) {         /* the pages in turn */
            set.page = (uint8_t)clamp((int32_t)set.page + e, 0, NPG - 1);
            ui.redraw_row = ui.redraw_title = 1;
            app_say(set.page == PG_HOME ? "HOME" : PG[set.page].title, "");
            ui.msg_until = 0;
        } else if (k == EN_PRESET) {
            param_turn(PG[set.page].preset, e);
        } else {
            uint32_t c = k - EN_K1;
            param_turn(PG[set.page].k[c], e);
            ui.lit_until[c] = plat_ms() + 900u;
        }
    }
    ui.keys = keys;
    ui.btn = btn;
    plat_glow(1);                            /* the unlit buttons glow, readable in the dark */
    plat_leds(btn | (set_dirty ? 1u << B_SAVE : 0u) | (ck.run ? 1u << B_PLAY : 0u) |
                  (gr.bd != BD_NONE ? 1u << B_ARP : 0u) | 1u << PG_BUTTON[set.page],
              keys);
}

void app_input_only(void) { input(); }

/* ----------------------------------------------------------------- screen --- */
/* the poster's big type: s in the big face, its letter at "stamp" (-1: none) in the stamped one */
static int32_t poster_text(int32_t x, int32_t y, const char *s, int32_t stamp)
{
    char one[2] = {0, 0};
    int32_t i;
    for (i = 0; s[i]; i++) {
        one[0] = s[i];
        x = cv_text(x, y, i == stamp ? &AG_LS : &AG_L, one, C_WHITE);
    }
    return x;
}

/* a name in capitals as the poster sets it: "2-STEP" -> "2-Step" */
static void title_case(const char *s, char *t)
{
    uint32_t i, first = 1;
    for (i = 0; s[i] && i < 23u; i++) {
        char c = s[i];
        t[i] = !first && c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
        first = c == ' ' || c == '-';
    }
    t[i] = 0;
}

/* HOME's second line: the groove's chord now as a name ("Am9"; stopped: its first), or the key when it plays none */
static void chord_now(char *t)
{
    static const char *const SUF[NCH] = {"", "m7", "maj7", "m9", "maj9", "sus2", "add9", ""};
    t[0] = 0;
    if (!set.p[PA_PROG]) {
        text_cat(t, "Key ");
        text_cat(t, KEY_NAME[set.p[PA_KEY]]);
        return;
    }
    {
        const uint8_t *c = GR_PROG[set.p[PA_PROG]][ck.run ? (ck.step >> 5) & 3u : 0u];
        text_cat(t, KEY_NAME[(set.p[PA_KEY] + c[0]) % 12u]);
        text_cat(t, SUF[c[1] % NCH]);
    }
}

static void draw_header(void)
{
    char t[32], n[24];
    uint32_t step = ck.step & 255u, bar = step / 16u;
    cv_begin(240, WV_TOP, C_BLACK);
    /* line 1: the page; HOME: the bar, the build, the drop */
    uint32_t held = pfx.held | gr.pf_auto;
    if (held) {                                  /* a trick held: its name, big (loaf-breaker's pads show theirs) */
        uint32_t k;
        for (k = 0; k < NPF && !(held >> k & 1u); k++)
            ;
        title_case(PF_NAME[k], t);              /* (no stamped letter: its grain looked like the screensaver left over) */
        poster_text(8, 2, t, -1);
    } else if (set.page != PG_HOME)
        poster_text(8, 2, PG[set.page].title, PG[set.page].stamp);
    else if (ck.run && gr.drops && ck.step - gr.drop_step < 8u)
        poster_text(8, 2, "Drop!", -1);
    else if (ck.run && gr.bd == BD_BUILD) {
        uint32_t left = gr.bd_bars - (ck.step - gr.bd_from) / 16u;
        t[0] = 0;
        text_cat(t, "Drop in ");
        int_text((int32_t)left, n);
        text_cat(t, n);
        poster_text(8, 2, t, -1);
    } else if (ck.run && gr.bd == BD_ARMED)
        poster_text(8, 2, "Build...", -1);
    else if (ck.run && gr.song) {                /* SONG: the section and its bar */
        t[0] = 0;
        text_cat(t, GR_SONG[gr.sec % GR_NSEC].name);
        text_cat(t, " ");
        int_text((int32_t)gr.sec_bar + 1, n);
        text_cat(t, n);
        text_cat(t, "/");
        int_text((int32_t)GR_SONG[gr.sec % GR_NSEC].bars, n);
        text_cat(t, n);
        poster_text(8, 2, t, -1);
    } else if (ck.run) {
        t[0] = 0;
        text_cat(t, "Bar ");
        int_text((int32_t)bar + 1, n);
        text_cat(t, n);
        text_cat(t, ".");
        int_text((int32_t)(step % 16u) / 4 + 1, n);
        text_cat(t, n);
        poster_text(8, 2, t, -1);
    } else {                                     /* stopped: the groove's name */
        title_case(PAT_NAME[set.p[PA_PAT]], t);
        poster_text(8, 2, t, -1);
    }
    /* line 2: a message for a moment; else the page's choice (HOME: the tempo) */
    if (plat_ms() < ui.msg_until) {
        int32_t x = cv_text(8, 42, &AG_M, ui.msg[0], C_WHITE);
        cv_text(x + 8, 42, &AG_M, ui.msg[1], C_WHITE);
    } else if (set.page == PG_HOME) {            /* the chord now (stopped: the first), or the key */
        chord_now(t);
        poster_text(8, 36, t, -1);
    } else if (set.page == PG_LINK) {            /* how far the Wi-Fi got, the session (net.h) */
        char w[40];
        lk_status(w);
        poster_text(8, 36, w, -1);
    } else {
        char w[32];
        uint32_t pr = PG[set.page].preset;
        w[0] = 0;
        value_text(pr, n);
        text_cat(w, n);
        title_case(w, t);
        poster_text(8, 36, t, -1);
    }
    cv_blit(0, 0);
}

/* the bottom row: K1-K4, each its name small and its value bigger; one just turned is lit (white, black type) */
#define ROW_Y 190
static void draw_row(void)
{
    uint32_t k, now = plat_ms();
    char v[24];
    for (k = 0; k < 4u; k++) {                   /* a canvas each: a long word is cut at its cell's edge */
        uint32_t p = PG[set.page].k[k];
        int32_t lit = now < ui.lit_until[k];
        uint16_t ink = lit ? C_BLACK : C_WHITE;
        cv_begin(60, 240 - ROW_Y, lit ? C_WHITE : C_BLACK);
        cv_rect(0, 0, 60, 1, C_WHITE);
        if (k && !lit)
            cv_rect(0, 1, 1, 49, RGB(90, 90, 90));
        cv_text(4, 4, &AG_S, PA[p].name, ink);
        value_text(p, v);
        if (text_w(&AG_M, v) > 55)               /* a long word: the small face */
            cv_text(4, 27, &AG_S, v, ink);
        else
            cv_text(4, 22, &AG_M, v, ink);
        cv_blit((int32_t)k * 60, ROW_Y);
    }
}

/* the tutor (loaf-breaker's idea): the first thing not done yet, on HOME's title bar, until it is done */
static const char *const TIP_TEXT[NTIP] = {"TIP: PRESS PLAY", "TIP: ARP BUILDS TO THE DROP", "TIP: ALGO - WHAT THE KEYS PLAY",
                                           "TIP: HOLD A BLACK KEY - A TRICK", "TIP: SEQ EDIT ENV FX - THE PAGES",
                                           "TIP: SAVE KEEPS IT ALL"};
static int32_t tip_now(void)
{
    uint32_t i;
    if (ck.run)
        set.tips |= 1u << TIP_PLAY;
    for (i = 0; i < NTIP; i++)
        if (!(set.tips >> i & 1u))
            return (int32_t)i;
    return -1;
}

/* the panel's title bar: off HOME the page's group as tabs (loaf-breaker's), the one shown inverted, and a word on
 * the right; on HOME the groove, or a tip */
static void draw_title(void)
{
    char t[48], n[12];
    int32_t tip = set.page == PG_HOME ? tip_now() : -1;
    t[0] = 0;
    if (tip >= 0) {
        cv_begin(240, WV_TITLE_H, WV_INK);
        cv_text(6, 1, &AG_S, TIP_TEXT[tip], WV_BAR);
        cv_blit(0, WV_TOP);
        return;
    }
    if (set.page != PG_HOME) {
        uint32_t g, x = 2;
        cv_begin(240, WV_TITLE_H, WV_FRAME);
        cv_rect(1, 1, 238, WV_TITLE_H - 1, WV_BAR);
        for (g = 0; g < NPG; g++) {
            if (g == PG_HOME || PG_BUTTON[g] != PG_BUTTON[set.page])
                continue;
            n[0] = 0;
            text_cat(n, PG[g].title);
            {
                uint32_t i;
                for (i = 0; n[i]; i++)
                    n[i] = n[i] >= 'a' && n[i] <= 'z' ? (char)(n[i] - 32) : n[i];
            }
            if (g == set.page) {
                cv_rect((int32_t)x, 2, text_w(&AG_T, n) + 6, WV_TITLE_H - 3, WV_INK);
                cv_text((int32_t)x + 3, 2, &AG_T, n, WV_BAR);
            } else
                cv_text((int32_t)x + 3, 2, &AG_T, n, WV_INK);
            x += (uint32_t)text_w(&AG_T, n) + 8u;
        }
        t[0] = 0;
        if (set.page == PG_CHOP) {               /* on the right: CHOP its sample, SETUP the load, else the groove */
            title_case(ch_name, t);
        } else if (set.page == PG_LINK) {
            lk_where(t);
        } else if (set.page == PG_SETUP) {
            text_cat(t, "CPU ");
            int_text((int32_t)plat_cpu_pct(), n);
            text_cat(t, n);
            text_cat(t, "%");
        } else {
            title_case(PAT_NAME[set.p[PA_PAT]], t);
            text_cat(t, " ");
            int_text(80 + set.p[PA_TEMPO], n);
            text_cat(t, n);
        }
        if (text_w(&AG_T, t) < 236 - (int32_t)x)
            cv_text(236 - text_w(&AG_T, t), 2, &AG_T, t, WV_INK);
        cv_blit(0, WV_TOP);
        return;
    }
    title_case(PAT_NAME[set.p[PA_PAT]], t);
    text_cat(t, " - ");
    int_text(80 + set.p[PA_TEMPO], n);
    text_cat(t, n);
    text_cat(t, " - ");
    text_cat(t, KEY_NAME[set.p[PA_KEY]]);
    text_cat(t, " - ");
    title_case(IN_NAME[set.inst], n);
    text_cat(t, n);
    if (gr.song)
        text_cat(t, " - Song");
    wv_draw_title(t);
}

/* the pads' strip (loaf-breaker's): the black keys' tricks, the ones playing inverted; SEL's when PADS are off */
#define PADS_Y (WV_R_Y + WV_LANE_H)
static void draw_pads(void)
{
    uint32_t k, held = pfx.held | gr.pf_auto;
    cv_begin(240, ROW_Y - PADS_Y, C_BLACK);
    if (!set.p[PA_PADS])
        cv_text(4, 0, &AG_T, "SEL + A WHITE KEY: A TRICK", RGB(150, 150, 150));
    else
        for (k = 0; k < NPAD; k++) {
            int32_t x = (int32_t)(k * 240u / NPAD), w = (int32_t)((k + 1u) * 240u / NPAD) - x - 1;
            int on = held >> PAD_TRICK[k] & 1u;
            if (on)
                cv_rect(x, 0, w, ROW_Y - PADS_Y, C_WHITE);
            else if (k)
                cv_rect(x - 1, 2, 1, ROW_Y - PADS_Y - 4, RGB(70, 70, 70));
            cv_text(x + (w - text_w(&AG_T, PAD_SHORT[k])) / 2, 0, &AG_T, PAD_SHORT[k], on ? C_BLACK : RGB(170, 170, 170));
        }
    cv_blit(0, PADS_Y);
}

void app_ui_init(void)
{
    touch_on(1);                             /* velocity and pressure: the keys' readings on */
    sv_init();                               /* the screensaver's film, if the resources hold it */
    sv.touch = plat_ms();
    boot_start(plat_ms());                   /* the boot screen: FM-1 AGAIN in the storm (boot.h) */
#if !PLAT_SMK37
    net_apply();                             /* WI-FI and LINK as kept (the SMK-37: in app_frame, 3 s on) */
#endif
    ui.redraw = ui.redraw_row = ui.redraw_title = 1;
}

void app_frame(void)
{
    static uint32_t last_step = 0xFFFFFFFFu, last_run, last_inst = 99u, lit_any, last_bd;
    uint32_t i, now = plat_ms(), lit = 0;
    input();
    sy_check();
#if PLAT_SMK37
    {   /* the SMK-37: the radio (Bluetooth, then WI-FI and LINK) 3 s after the start, not in the boot (Arctic Scream
         * SMK-37's lesson, 2026-11-27); not after a Bluetooth run that crashed (radio.c), as main_fm1.c's boot */
        static uint8_t radio_go;
        if (!radio_go && now >= 3000u) {
            radio_go = 1;
#ifdef PLAT_HOST
            if (!set.ble_off)
#else
            if (!radio_last_bad && !set.ble_off)
#endif
                plat_ble_start();
            net_apply();
        }
    }
#endif
    lk_frame();                                  /* Ableton Link: the session's tempo, beat and start (net.h) */
    nm_frame();                                  /* MIDI over Wi-Fi (net.h) */
    speak();
    tapefx_poll();                               /* TAPE's saturation curve, made here when it changes */
    if (FM1B_BLE_CENTRAL) {                  /* looking: a device found is named at once (REC connects to it) */
        static int found;
        int n = ui.looking ? btdev_count() : 0, ev;
        btdev_page(ui.looking && !set.ble_off);
        if (n > found && btdev_pick() < n)
            ble_device_say();
        found = n;
        while ((ev = btdev_event()) != BTDEV_NONE)   /* a device connected, given up or lost */
            if (ev == BTDEV_CONNECTED || ev == BTDEV_NO_ANSWER || ev == BTDEV_LOST)
                app_say("DEVICE", btdev_word(ev));
    }
    if (bt.on) {                                 /* the boot screen, until it is over or the first touch */
        if (sv.touch - bt.t0 < 0x80000000u && sv.touch != bt.t0 && sv.touch - bt.t0 > 150u)
            bt.on = 0;
        else if (boot_frame(now))
            return;
        ui.full = 1;
        ui.redraw = ui.redraw_row = ui.redraw_title = 1;
    }
    /* (signed: input() above set sv.touch to plat_ms(), which may be past this frame's now; unsigned, that came out
     * as ~49 days untouched and the film started on any press, a frame at a time, between the page's) */
    if (!sv.on && sv.p && set.p[PA_SAVER] && (int32_t)(now - sv.touch) >= (int32_t)SV_IDLE_MS) {
        sv.on = 1;                               /* untouched a minute: the film */
        sv.at = 0;
        sv.t = now - sv.ms;
    }
    if (sv.on) {
        wv_held = 1;                             /* (the waveform's columns wait meanwhile) */
        sv_frame(now);
        return;
    }
    if (ui.full)                                 /* everything again (back from the screensaver, the boot screen; the
                                                  * mirror asking, which while the waveform runs is often): drawn over
                                                  * what is there, never cleared first, so nothing flickers. The
                                                  * header, the title bar, the panel, the pads and the row cover all
                                                  * 240 x 240 */
        ui.redraw = ui.redraw_row = ui.redraw_title = 1;
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (set.page == PG_HOME && ck.run && (ck.step / 4u != last_step || gr.bd != last_bd)) {
        last_step = ck.step / 4u;                /* HOME counts the beats */
        last_bd = gr.bd;
        ui.redraw = 1;
    }
    if (ck.run != last_run || set.inst != last_inst) {
        last_run = ck.run;
        last_inst = set.inst;
        ui.redraw = ui.redraw_title = 1;
    }
    for (i = 0; i < 4u; i++)
        lit |= (uint32_t)(now < ui.lit_until[i]) << i;
    {                                            /* the CPU short (the audio past 85 % of its budget): KEYS plays fewer voices */
        uint32_t cpu = plat_cpu_pct();
        kp.nv = cpu > 85u ? 6u : cpu < 70u ? KP_NV : kp.nv;
    }
    if (set.page == PG_LINK && set.p[PA_WIFI] == 2u) {   /* MODE AP: its name and password by turns */
        static uint32_t ap_at;
        if (now - ap_at >= 1000u) {
            ap_at = now;
            ui.redraw = ui.redraw_title = 1;
        }
    }
    if (set.page == PG_SETUP) {                  /* the load, once a second */
        static uint32_t cpu_at;
        if (now - cpu_at >= 1000u) {
            cpu_at = now;
            ui.redraw_title = 1;
        }
    }
    if (lit != lit_any) {                        /* a cell's light goes out */
        lit_any = lit;
        ui.redraw_row = 1;
    }
    {                                            /* the pads' strip, a trick's name in the header, the tutor */
        static uint32_t last_held = 0xFFFFFFFFu, last_pads = 99u, last_tip = 99u, last_song;
        uint32_t held = pfx.held | gr.pf_auto, tip = (uint32_t)(tip_now() + 1);
        if (held != last_held || set.p[PA_PADS] != last_pads || ui.full) {
            if ((held != 0) != (last_held != 0) || held != last_held)
                ui.redraw = 1;
            last_held = held;
            last_pads = set.p[PA_PADS];
            draw_pads();
        }
        if (tip != last_tip || (uint32_t)gr.song != last_song) {
            last_tip = tip;
            last_song = gr.song;
            ui.redraw_title = 1;
        }
        if (gr.song && ck.run && set.page == PG_HOME) {   /* SONG: the section changes */
            static uint32_t last_sec = 99u;
            if (gr.sec != last_sec) {
                last_sec = gr.sec;
                ui.redraw = 1;
            }
        }
    }
    if (ui.redraw_title) {
        static uint8_t panel;
        ui.redraw_title = 0;
        ui.redraw = 1;
        draw_title();
        if (!panel) {                            /* the panel once; then its columns as they come */
            panel = 1;
            wv_draw_all();
        }
    }
    {                                            /* CHOP's REVERSE shows the slice in hand's */
        uint8_t r = (uint8_t)(set.crev >> chop_cur() & 1u);
        if (set.p[PA_CREV] != r) {
            set.p[PA_CREV] = r;
            ui.redraw = 1;
        }
    }
    {                                            /* CHOP's page: the sample in the panel; leaving it, the mix again */
        static uint32_t shown = 0xFFFFFFFFu;
        uint32_t lit = set.p[PA_CMODE] == CM_PITCHED ? set.p[PA_SLICE] % ch_next.nslices : ch.last_slice;
        uint32_t want = set.page == PG_CHOP ? (lit | ch_gen << 8) ^ set.crev * 2654435761u : 0xFFFFFFFFu;
        if (want != shown || ui.full) {
            if (set.page == PG_CHOP) {
                wv_held = 1;
                wv_draw_sample(&ch_next, lit, set.crev);
            } else {
                wv_held = 0;
                wv_draw_all();
            }
            shown = want;
        }
    }
    if (ui.redraw) {
        ui.redraw = 0;
        draw_header();
    }
    if (ui.redraw_row) {
        ui.redraw_row = 0;
        draw_row();
    }
    wv_draw_new();
    ui.full = 0;
}

/* ------------------------------------------------------------ companion --- */
/* the computer's side (fm1.toml companion = true: docs/COMPANION.md): the saved settings are a file a page can
 * download and send back (app.h APP_FILE_OBJS); the platform checks a file whole before it saves it */
int app_file_busy(void) { return 0; }   /* (a sample may come while it plays: the copy written is not the one played) */
int app_file(uint32_t i, plat_file_info_t *f)
{
    int n;
    if (i)
        return -1;
    n = plat_file_size(&CH_FILE);
    f->name = "chop";
    f->kind = PLAT_FK_SAMPLE;
    f->flags = PLAT_FF_READ | PLAT_FF_WRITE | PLAT_FF_DELETE | PLAT_FF_PLAYING;
    f->size = n > 0 ? (uint32_t)n : 0u;
    f->room = CH_FILE.n - PLAT_FILE_HEAD;
    return 0;
}
int app_file_read(uint32_t i, uint32_t off, uint8_t *dst, uint32_t n) { return i ? -1 : plat_file_read(&CH_FILE, off, dst, n); }
int app_file_begin(uint32_t i, uint32_t size)
{
    if (i)
        return -1;
    chop_factory();                          /* one copy: nothing plays from it while it is written */
    return plat_file_begin(&CH_FILE, size);
}
int app_file_write(uint32_t i, uint32_t off, const uint8_t *src, uint32_t n)
{
    return i ? -1 : plat_file_write(&CH_FILE, off, src, n);
}
int app_file_commit(uint32_t i, uint32_t size, uint32_t crc)
{
    int rc = i ? -1 : plat_file_commit(&CH_FILE, size, crc);
    if (!rc) {
        chop_load();
        set.crev = 0;                            /* (new slices: none backwards) */
        apply();
        app_say("CHOP", ch_name);
        ui.redraw_title = 1;
    }
    return rc;
}
int app_file_delete(uint32_t i)
{
    int rc = i ? -1 : plat_file_delete(&CH_FILE);
    if (!rc) {
        chop_load();
        app_say("CHOP", "FACTORY");
    }
    return rc;
}
/* the parameters for a computer (APP_PARAMS: the editor page, fm1_companion.py): every PA_*, its id its index (kept
 * from one release to the next: PA_* only grows at its end), grouped by the page that shows it, in the values the
 * screen shows (TEMPO in BPM, PITCH in semitones, SLICE from 1) */
static int32_t pa_off(uint32_t i)
{
    return PA[i].fmt == FMT_BPM ? 80 : PA[i].fmt == FMT_SEMI ? -12 : PA[i].fmt == FMT_ONE ? 1 : 0;
}
static uint32_t pa_page(uint32_t i)
{
    uint32_t g, k;
    for (g = 0; g < NPG; g++)
        for (k = 0; k < 5u; k++)
            if ((k < 4u ? PG[g].k[k] : PG[g].preset) == i)
                return g;
    return PG_SETUP;
}
int app_param(uint32_t i, plat_param_t *p)
{
    static const char *const UNIT[] = {[FMT_PCT] = "%", [FMT_BPM] = "BPM", [FMT_MS] = "", [FMT_UP] = "BPM",
                                       [FMT_SEMI] = "st"};
    uint32_t f;
    if (i >= NPA)
        return -1;
    f = PA[i].fmt;
    p->id = (uint16_t)i;
    p->group = (uint8_t)pa_page(i);
    p->flags = 0;
    p->name = PA[i].name;
    p->unit = f < sizeof UNIT / sizeof UNIT[0] && UNIT[f] && !(f == FMT_PCT && PA[i].top != 100) ? UNIT[f] : "";
    p->type = f == FMT_LIST || f == FMT_KEY || f == FMT_BARS ? PLAT_PT_ENUM : PLAT_PT_NUMBER;
    p->labels = f == FMT_LIST ? PA[i].list : f == FMT_KEY ? KEY_NAME : f == FMT_BARS ? BARS_NAME : 0;
    p->min = pa_off(i);
    p->max = PA[i].top + pa_off(i);
    p->def = PA[i].def + pa_off(i);
    return 0;
}
const char *app_param_group(uint32_t g)
{
    return g < NPG ? (g == PG_HOME ? "Home" : PG[g].title) : 0;
}
int32_t app_param_get(uint32_t i) { return i < NPA ? set.p[i] + pa_off(i) : 0; }
int32_t app_param_set(uint32_t i, int32_t v)
{
    if (i >= NPA)
        return 0;
    param_set(i, v - pa_off(i));
    return set.p[i] + pa_off(i);
}

/* the mirror lost a part of the screen (APP_REDRAW): all of it again at the next frame, whatever is up */
/* The mirror asks again every 50 ms while rows it could not send are dirty, and a whole screen fills its ring: while
 * the waveform runs that would ask, flood, drop and ask again for ever, the viewer never catching up. So the whole
 * screen at most every APP_REDRAW_MS; between them only what changes goes out, and the next one mends the rest */
/* Waking from the screensaver, the ring is still full of the film: the page's first whole screen is mostly dropped,
 * and at one mend every APP_REDRAW_MS its bands of film stayed in the viewer up to 2 s. For APP_REDRAW_FAST_FOR after
 * the wake, a mend every APP_REDRAW_FAST_MS (each sends only the rows still wrong: mirror.c skips what the viewer has) */
#define APP_REDRAW_MS 2000u
#define APP_REDRAW_FAST_MS 100u
void app_redraw(void)
{
    uint32_t now = plat_ms();
    uint32_t gap = (int32_t)(redraw_fast_until - now) > 0 ? APP_REDRAW_FAST_MS : APP_REDRAW_MS;
    if (redraw_last && now - redraw_last < gap)
        return;
    redraw_last = now ? now : 1u;
    ui.full = 1;
    ui.redraw = ui.redraw_row = ui.redraw_title = 1;
    bt.force = 1;
    sv.t -= sv.ms;                               /* (the screensaver: its frame now) */
}

/* backup and restore (fm1.toml backup = true) */
int app_backup_playing(void) { return ck.run; }
void app_backup_flush(void) {}           /* (SAVE saves: a backup takes what was saved) */
void app_backup_hold(int on) { (void)on; }   /* (nothing saves by itself) */
void app_backup_restored(void)
{
    settings_load();
    set_dirty = 0;
    ui.full = 1;
}

void app_file_changed(uint32_t obj)      /* the computer replaced the settings: use them */
{
    if (obj != OBJ_SETTINGS)
        return;
    settings_load();
    set_dirty = 0;
    ui.redraw = ui.redraw_row = ui.redraw_title = 1;
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "octave"))
        *got = set.octave;
    else if (!strcmp(what, "note"))
        *got = last_note;
    else if (!strcmp(what, "dirty"))
        *got = set_dirty;
    else if (!strcmp(what, "crev"))          /* CHOP: the slices played backwards */
        *got = (int)set.crev;
    else if (!strcmp(what, "bpm"))           /* the tempo now (a MIDI clock's, when it leads) */
        *got = (int)(ck.bpm + 0.5f);
    else if (!strcmp(what, "step"))          /* the clock's sixteenth since PLAY */
        *got = (int)ck.step;
    else if (!strcmp(what, "saver"))         /* the screensaver on */
        *got = sv.on;
    else if (!strcmp(what, "saver_frames"))  /* its film's frames (0: none) */
        *got = sv.p ? (int)sv.nf : 0;
    else if (!strcmp(what, "saver_bad")) {   /* the film decoded whole: the frames not as tools/again_saver.py made them */
        uint32_t f, bad = 0, i, k, c;
        for (f = 0; sv.p && f < sv.nf; f++) {
            c = 0xFFFFFFFFu;
            if (sv_decode(f))
                c = 0;
            else
                for (i = 0; i < SV_FB; i++)
                    for (c ^= sv_fb[i], k = 0; k < 8u; k++)
                        c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1u)));
            bad += ~c != sv_u32(sv.p + 12u + 4u * sv.nf + 4u * f);
        }
        sv.at = 0;
        *got = (int)bad;
    }
    else if (!strcmp(what, "page"))
        *got = set.page;
    else if (!strcmp(what, "inst"))
        *got = set.inst;
    else if (!strcmp(what, "run"))
        *got = ck.run;
    else if (!strcmp(what, "bd"))            /* BUILD / DROP: 0 none, 1 armed, 2 building */
        *got = gr.bd;
    else if (!strcmp(what, "drops"))
        *got = (int)gr.drops;
    else if (!strcmp(what, "kp_voices")) {   /* KEYS' voices sounding */
        int i, c = 0;
        for (i = 0; i < (int)KP_NV; i++)
            c += kp.v[i].note >= 0;
        *got = c;
    } else if (!strncmp(what, "p", 1) && what[1] >= '0' && what[1] <= '9')   /* pN: parameter N */
        *got = set.p[atoi(what + 1) % NPA];
    else if (!strcmp(what, "vel"))           /* the last key's velocity and pressure */
        *got = ui.vel[ui.last_key];
    else if (!strcmp(what, "prs"))
        *got = ui.prs[ui.last_key];
    else if (!strncmp(what, "turns", 5))    /* turnsN: encoder N's count (plat.h EN_*) */
        *got = ui.turns[atoi(what + 5) % NE];
    else
        return 0;
    return 1;
}
#endif
