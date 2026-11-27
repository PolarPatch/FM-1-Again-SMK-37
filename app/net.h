/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 PolarPatch */
/* Wi-Fi and Ableton Link (fm1.toml wifi, wifi_ap, link; fm1-base's own stack and base/link/, GPL-3.0-only). The LINK
 * page: WI-FI (OFF, AUTO: the networks kept, given over USB with base/tools/fm1_remote.py wifi join SSID PASSWORD; AP:
 * the FM-1 is the network, its name and password on the page) and LINK (on: a peer of the session whenever there is
 * an address).
 *
 * With a peer in the session (and no MIDI clock coming in, which leads first), the session leads: its tempo is the
 * clock's (BUILD's RUSH waits), the clock is pulled onto its beat a little each frame, and its start and stop are
 * PLAY's: started, the groove begins at the next bar line, as Link's apps do. Alone, the FM-1 gives the session its
 * own tempo and PLAY. Part of app.c (before play_toggle). Main loop only. */

static int nm_up(void);                          /* (MIDI over Wi-Fi: below) */
#define LK_Q 4000000                             /* a bar: four beats, in micro-beats */
static struct {
    uint8_t was_playing;                         /* the session's start / stop last seen */
    int64_t arm_ub;                              /* a start waiting for this bar line (micro-beats); -1 none */
    int32_t mpb;                                 /* the session's tempo last seen */
    uint32_t peers, state;                       /* (the page: drawn again when they change) */
} lk = {0, -1, 0, 0, 0};

/* WI-FI and LINK into the platform: at the start, and when they are turned */
static void net_apply(void)
{
    static const uint8_t MODE[3] = {WIFI_MODE_OFF, WIFI_MODE_AUTO, WIFI_MODE_AP};
    int m = MODE[set.p[PA_WIFI] % 3u];
    if (plat_wifi_get_mode() != m) {
        plat_link_enable(0);                     /* (Link opens its port and joins its group again on the new mode) */
        plat_wifi_mode(m);
    }
    plat_link_enable(set.p[PA_LINK] != 0u);
}

static int lk_with_peers(void) { return plat_link_on() && plat_link_peers() > 0u && plat_link_mpb() > 0; }

/* PLAY: with peers, the session starts and the groove waits for the bar line; alone, at once (and the session too) */
static int lk_play(int start)
{
    if (!plat_link_on())
        return 0;
    if (start && lk_with_peers()) {
        int64_t ub = plat_link_beat_at(plat_link_now());
        if (ub < 0)
            return 0;
        if (lk.arm_ub >= 0) {                    /* PLAY again while waiting: not after all */
            lk.arm_ub = -1;
            return 1;
        }
        lk.arm_ub = ub - plat_link_phase(ub, LK_Q) + LK_Q;
        if (!plat_link_playing())
            plat_link_set_playing(1);
        lk.was_playing = 1;
        return 1;                                /* (the groove starts at the bar line: lk_frame) */
    }
    plat_link_set_playing(start);
    lk.was_playing = (uint8_t)start;
    return 0;
}

/* the TEMPO knob turned (or a MIDI clock's tempo): the session's too */
static void lk_tempo(void)
{
    if (plat_link_on() && bpm_base > 0.0f)
        plat_link_set_tempo((int32_t)(60000000.0f / bpm_base + 0.5f));
}

/* where the clock is in its bar, in sixteenths 0 .. 16, as if without SWING (a pair of sixteenths keeps its length) */
static float lk_ours(void)
{
    uint32_t step = ck.step;
    float l0 = SR * 15.0f / ck.bpm, in = ck.len - ck.left;
    if (step == 0xFFFFFFFFu)
        return -1.0f;
    if (step & 1u)
        in += l0 * (1.0f + ck.swing);
    return (float)(step & 14u) + in / l0;
}

static void lk_frame(void)
{
    int64_t ub;
    int playing;
    uint32_t st = (uint32_t)plat_wifi_state() | (plat_link_on() ? 0x100u : 0u);
    st |= nm_up() ? 0x200u : 0u;
    if (lk.state != st || lk.peers != plat_link_peers()) {
        lk.state = st;
        lk.peers = plat_link_peers();
        if (set.page == PG_LINK)
            ui.redraw = ui.redraw_title = 1;
    }
    lk_lead = (uint8_t)(lk_with_peers() && !sy.here);
    if (!plat_link_on() || (ub = plat_link_beat_at(plat_link_now())) < 0) {
        lk.arm_ub = -1;
        return;
    }
    if (lk_lead) {                               /* the session's tempo: the clock's, and TEMPO's knob shows it */
        int32_t mpb = plat_link_mpb();
        bpm_base = 60000000.0f / (float)mpb;
        if (mpb != lk.mpb) {
            int32_t t = (int32_t)(bpm_base + 0.5f) - 80;
            lk.mpb = mpb;
            if (t >= 0 && t <= PA[PA_TEMPO].top && set.p[PA_TEMPO] != (uint8_t)t) {
                set.p[PA_TEMPO] = (uint8_t)t;    /* (not unsaved: the session's, not a turn) */
                ui.redraw_row = ui.redraw_title = 1;
            }
        }
    } else if (plat_link_mpb() > 0 && (int32_t)(60000000.0f / bpm_base + 0.5f) != plat_link_mpb()) {
        lk_tempo();                              /* alone, or a MIDI clock leads: ours is the session's */
        lk.mpb = plat_link_mpb();
    }
    playing = plat_link_playing();
    if (playing != lk.was_playing) {             /* a peer started or stopped the session */
        lk.was_playing = (uint8_t)playing;
        if (lk_lead && playing && !ck.run && lk.arm_ub < 0)
            lk.arm_ub = ub - plat_link_phase(ub, LK_Q) + LK_Q;
        else if (lk_lead && !playing && (ck.run || lk.arm_ub >= 0)) {
            lk.arm_ub = -1;
            if (ck.run) {
                ck.run = 0;
                want_stop = 1;
                app_say("STOPPED", "LINK");
            }
        }
    }
    if (lk.arm_ub >= 0) {                        /* a start at the bar line */
        if (!lk_lead || !plat_link_playing())
            lk.arm_ub = -1;
        else if (ub >= lk.arm_ub) {
            lk.arm_ub = -1;
            if (!ck.run) {
                ck_start();
                app_say("PLAYING", "LINK");
            }
        }
        return;
    }
    if (lk_lead && ck.run) {                     /* the clock onto the session's beat: a part of the way each frame */
        float theirs = (float)(int32_t)plat_link_phase(ub, LK_Q) * (16.0f / (float)LK_Q), ours = lk_ours(), err;
        if (ours < 0.0f)
            return;
        err = ours - theirs;
        if (err >= 8.0f)
            err -= 16.0f;
        else if (err < -8.0f)
            err += 16.0f;
        lk_nudge = err * (SR * 15.0f / ck.bpm) * (err > 1.0f || err < -1.0f ? 1.0f : 0.1f);
    }
}

/* the page's second line: how far the Wi-Fi got, the session; in MODE AP the network's name, its password and the
 * session by turns */
static void lk_status(char *t)
{
    static const char *const ST[] = {"Wi-Fi off", "Starting", "No network", "Joining", "Address", "Connected",
                                     "Failed"};
    char n[16];
    t[0] = 0;
    if (set.p[PA_WIFI] == 2u) {
        char nm[33], key[PLAT_WIFI_AP_KEY_MAX];
        nm[0] = key[0] = 0;
        plat_wifi_ap_name(nm);
        if (plat_wifi_ap_state() != WIFI_AP_UP)
            text_cat(t, plat_wifi_ap_state() == WIFI_AP_FAILED ? "AP failed" : "AP starting");
        else {
            uint32_t k = plat_ms() / 2000u % 3u;  /* its name, its password, the session: by turns */
            if (k == 1u && plat_wifi_ap_default_key(key) && key[0])
                text_cat(t, key);
            else if (k == 2u && plat_link_on()) {
                int_text((int32_t)plat_link_peers(), n);
                text_cat(t, n);
                text_cat(t, plat_link_peers() == 1u ? " peer" : " peers");
                if (nm_up())
                    text_cat(t, ", MIDI");
            } else
                text_cat(t, nm);
        }
        return;
    }
    if (plat_wifi_state() != WIFI_CONNECTED) {
        uint32_t s = (uint32_t)plat_wifi_state();
        text_cat(t, s == WIFI_FAILED && plat_wifi_why()[0] ? plat_wifi_why() : ST[s < 7u ? s : 0u]);
        return;
    }
    if (!plat_link_on()) {
        text_cat(t, set.p[PA_LINK] ? "Link starting" : nm_up() ? "MIDI" : "Link off");
        return;
    }
    int_text((int32_t)plat_link_peers(), n);
    text_cat(t, n);
    text_cat(t, plat_link_peers() == 1u ? " peer" : " peers");
    if (nm_up())
        text_cat(t, ", MIDI");
}

/* the title bar's right: the address (MODE AP: the clients joined) */
static void lk_where(char *t)
{
    char n[8];
    uint32_t ip = plat_wifi_addr(), i;
    t[0] = 0;
    if (set.p[PA_WIFI] == 2u) {
        int_text((int32_t)plat_wifi_ap_clients(), n);
        text_cat(t, n);
        text_cat(t, " JOINED");
        return;
    }
    for (i = 0; ip && i < 4u; i++) {
        int_text((int32_t)(ip >> (24u - 8u * i) & 255u), n);
        text_cat(t, n);
        if (i < 3u)
            text_cat(t, ".");
    }
}

/* ---- MIDI over Wi-Fi (RTP-MIDI, Apple's "Network Session"): NETMIDI on the page. The FM-1 looks for a session on
 * its network (an mDNS question for _apple-midi._udp, sent from its own port so the answer comes back to it: no group
 * joined, Link keeps its own) and invites the first it finds: a phone's (iOS: an app's Network Session on), a
 * computer's (macOS: Audio MIDI Setup's network session). A computer may invite it too (192.168.4.1 or its address,
 * port 5004). Then the keys and the groove's notes go there as they go over USB and Bluetooth, and what comes from it
 * plays as MIDI in does. No journal (the network is a few metres of Wi-Fi). */

#define NM_CTL 5004u                             /* the session's two ports: control, then data */
#define NM_DATA 5005u
#define NM_MDNS 0xE00000FBu                      /* 224.0.0.251:5353 */
enum { NM_OFF, NM_SEARCH, NM_INV_CTL, NM_INV_DATA, NM_UP };
static struct {
    uint8_t st, tries, open;
    uint32_t ip, addr;                           /* the peer's address; ours when the ports were opened */
    uint16_t pctl, pdata, seq;                   /* its ports; our RTP sequence */
    uint32_t token, ssrc, at, heard, ck_at;      /* (ms: the last thing sent, heard from it, the last sync) */
    uint16_t qid;
} nm;
static const char NM_NAME[] = "FM-1 Again";

/* the notes out: the main loop's straight into the packet; the render's through a ring (the audio interrupt) */
#define NM_RING 64u
static volatile uint32_t nm_r_buf[NM_RING], nm_r_w, nm_r_r;   /* the render's out */
static volatile uint32_t nm_i_buf[NM_RING], nm_i_w, nm_i_r;   /* in, to the render */
static uint32_t nm_m_buf[NM_RING], nm_m_n;                    /* the main loop's out */

static void nm_put(uint32_t pkt, int render)
{
    if (nm.st != NM_UP)
        return;
    if (render) {
        if (nm_r_w - nm_r_r < NM_RING) {
            nm_r_buf[nm_r_w % NM_RING] = pkt;
            nm_r_w++;
        }
    } else if (nm_m_n < NM_RING)
        nm_m_buf[nm_m_n++] = pkt;
}

static int nm_get(uint32_t *pkt)                  /* the render: one that came in */
{
    if (nm_i_r == nm_i_w)
        return 0;
    *pkt = nm_i_buf[nm_i_r % NM_RING];
    nm_i_r++;
    return 1;
}

static uint8_t *nm_u16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; return p + 2; }
static uint8_t *nm_u32(uint8_t *p, uint32_t v) { p = nm_u16(p, v >> 16); return nm_u16(p, v & 0xFFFFu); }
static uint32_t nm_g16(const uint8_t *p) { return (uint32_t)p[0] << 8 | p[1]; }
static uint32_t nm_g32(const uint8_t *p) { return nm_g16(p) << 16 | nm_g16(p + 2); }
static uint64_t nm_now(void) { return (uint64_t)plat_ms() * 10u; }   /* the session's clock: 100 us steps */

/* IN, OK, BY: the session's words (version 2, the token, our SSRC, our name) */
static void nm_word(const char *w, uint16_t ours, uint16_t port, uint32_t token)
{
    uint8_t b[16 + sizeof NM_NAME], *p = b;
    p = nm_u16(p, 0xFFFFu);
    *p++ = (uint8_t)w[0];
    *p++ = (uint8_t)w[1];
    p = nm_u32(p, 2u);
    p = nm_u32(p, token);
    p = nm_u32(p, nm.ssrc);
    if (w[0] != 'B') {
        memcpy(p, NM_NAME, sizeof NM_NAME);
        p += sizeof NM_NAME;
    }
    plat_net_udp_send(ours, nm.ip, port, b, (uint32_t)(p - b));
}

static void nm_ck(uint32_t count, const uint8_t *in)   /* a clock sync: count 0 starts it, 1 and 2 answer */
{
    uint8_t b[36], *p = b;
    uint64_t t = nm_now();
    int i;
    p = nm_u16(p, 0xFFFFu);
    *p++ = 'C';
    *p++ = 'K';
    p = nm_u32(p, nm.ssrc);
    *p++ = (uint8_t)count;
    *p++ = 0; *p++ = 0; *p++ = 0;
    if (in)
        memcpy(p, in, 24);                       /* (the times so far, as they came) */
    else
        memset(p, 0, 24);
    for (i = 0; i < 8; i++)                      /* ours in its slot */
        p[8 * count + (uint32_t)i] = (uint8_t)(t >> (56 - 8 * i));
    plat_net_udp_send(NM_DATA, nm.ip, nm.pdata, b, sizeof b);
}

static void nm_lost(void)
{
    nm.st = set.p[PA_NETMIDI] && nm.open ? NM_SEARCH : NM_OFF;
    nm.at = 0;
    nm_m_n = 0;
}

/* a name in an mDNS message, read through its pointers into o as it goes on the wire; its length, 0 bad */
static uint32_t nm_name(const uint8_t *m, uint32_t n, uint32_t at, uint8_t *o, uint32_t room, uint32_t *after)
{
    uint32_t len = 0, hops = 0;
    int jumped = 0;
    while (at < n) {
        uint32_t l = m[at];
        if ((l & 0xC0u) == 0xC0u) {
            if (at + 1 >= n || ++hops > 8)
                return 0;
            if (!jumped && after)
                *after = at + 2;
            jumped = 1;
            at = (l & 0x3Fu) << 8 | m[at + 1];
            continue;
        }
        if (l > 63u || at + 1 + l > n || len + 1 + l + 1 > room)
            return 0;
        memcpy(o + len, m + at, 1 + l);
        len += 1 + l;
        at += 1 + l;
        if (!l) {
            if (!jumped && after)
                *after = at;
            return len;
        }
    }
    return 0;
}

static void nm_ask(const uint8_t *name, uint32_t nlen, uint32_t type)   /* an mDNS question, answered to our port */
{
    uint8_t b[300], *p = b;
    if (nlen + 16 > sizeof b)
        return;
    p = nm_u16(p, nm.qid);
    p = nm_u16(p, 0);
    p = nm_u16(p, 1);
    p = nm_u32(p, 0);
    p = nm_u16(p, 0);
    memcpy(p, name, nlen);
    p += nlen;
    p = nm_u16(p, type);
    p = nm_u16(p, 0x8001u);                      /* IN, an answer to us alone */
    plat_net_udp_send(NM_CTL, NM_MDNS, 5353, b, (uint32_t)(p - b));
}

static void nm_mdns(uint32_t src, const uint8_t *m, uint32_t n)   /* an answer: the session's port (SRV), or ask it */
{
    uint8_t inst[256];
    uint32_t i, at = 12, cnt, ilen = 0;
    if (n < 12 || nm_g16(m) != nm.qid || !(m[2] & 0x80u) || nm.st != NM_SEARCH)
        return;
    for (i = nm_g16(m + 4); i; i--) {            /* the questions again */
        uint8_t t[256];
        if (!nm_name(m, n, at, t, sizeof t, &at))
            return;
        at += 4;
    }
    cnt = nm_g16(m + 6) + nm_g16(m + 8) + nm_g16(m + 10);
    for (i = 0; i < cnt && at < n; i++) {
        uint8_t t[256];
        uint32_t type, rl;
        if (!nm_name(m, n, at, t, sizeof t, &at) || at + 10 > n)
            return;
        type = nm_g16(m + at);
        rl = nm_g16(m + at + 8);
        at += 10;
        if (at + rl > n)
            return;
        if (type == 33u && rl >= 6u) {           /* SRV: the control port; the address is the one that answered */
            nm.ip = src;
            nm.pctl = (uint16_t)nm_g16(m + at + 4);
            nm.pdata = (uint16_t)(nm.pctl + 1u);
            nm.st = NM_INV_CTL;
            nm.tries = 0;
            nm.at = 0;
            return;
        }
        if (type == 12u && !ilen)                /* PTR: the session's own name, to ask its SRV */
            ilen = nm_name(m, n, at, inst, sizeof inst, 0);
        at += rl;
    }
    if (ilen)
        nm_ask(inst, ilen, 33u);
}

/* the MIDI in a data packet, to the render */
static void nm_midi_in(const uint8_t *b, uint32_t n)
{
    uint32_t at, len, end, st = 0;
    int first = 1, z;
    if (n < 13 || (b[0] & 0xC0u) != 0x80u)
        return;
    at = 12u + 4u * (b[0] & 15u);
    if (at >= n)
        return;
    z = b[at] & 0x20;
    if (b[at] & 0x80u) {
        if (at + 1 >= n)
            return;
        len = (b[at] & 15u) << 8 | b[at + 1];
        at += 2;
    } else
        len = b[at++] & 15u;
    end = at + len < n ? at + len : n;
    while (at < end) {
        uint32_t d = 0, c, k;
        if (!first || z)                         /* a delta time: up to four bytes */
            for (k = 0; k < 4u && at < end; k++)
                if (!(b[at++] & 0x80u))
                    break;
        first = 0;
        if (at >= end)
            break;
        if (b[at] & 0x80u)
            st = b[at++];
        if (st == 0xF0u || st == 0xF7u) {        /* SysEx: past it */
            while (at < end && b[at] != 0xF7u && b[at] != 0xF0u && b[at] != 0xF4u)
                at++;
            at++;
            st = 0;
            continue;
        }
        if (!st)
            break;
        c = st >= 0xF8u || st == 0xF6u ? 0u : st == 0xF2u || st < 0xC0u || (st & 0xF0u) == 0xE0u ? 2u : 1u;
        if (at + c > end)
            break;
        if (st < 0xF0u)
            d = (st >> 4) | st << 8;
        else
            d = (c == 0u ? 0x0Fu : c == 1u ? 2u : 3u) | st << 8;
        if (c > 0u)
            d |= (uint32_t)b[at] << 16;
        if (c > 1u)
            d |= (uint32_t)b[at + 1] << 24;
        at += c;
        if (st >= 0xF0u && st < 0xF8u)
            st = 0;
        if (nm_i_w - nm_i_r < NM_RING) {
            nm_i_buf[nm_i_w % NM_RING] = d;
            nm_i_w++;
        }
    }
}

static void nm_rx(void *ctx, uint32_t src, uint16_t sport, const uint8_t *p, uint32_t n, int64_t at_us)
{
    uint32_t data = (uint32_t)(uintptr_t)ctx;
    (void)at_us;
    if (!data && sport == 5353u) {
        nm_mdns(src, p, n);
        return;
    }
    if (n >= 4 && p[0] == 0xFF && p[1] == 0xFF) {   /* the session's words */
        if (p[2] == 'I' && p[3] == 'N' && n >= 16) {   /* invited: yes (one session: a new one takes its place) */
            if (!data || src != nm.ip) {
                nm.ip = src;
                nm.pctl = sport;
                nm.pdata = (uint16_t)(sport + 1u);
                nm.st = NM_INV_DATA;
                nm.tries = 0;
                nm.at = plat_ms();
                nm.heard = plat_ms();
            } else {
                nm.pdata = sport;
                nm.st = NM_UP;
                nm.ck_at = 0;
            }
            nm_word("OK", data ? NM_DATA : NM_CTL, sport, nm_g32(p + 8));
            return;
        }
        if (src != nm.ip)
            return;
        nm.heard = plat_ms();
        if (p[2] == 'O' && p[3] == 'K' && n >= 16 && nm_g32(p + 8) == nm.token) {
            if (nm.st == NM_INV_CTL) {
                nm.st = NM_INV_DATA;
                nm.tries = 0;
                nm.at = 0;
            } else if (nm.st == NM_INV_DATA && data) {
                nm.st = NM_UP;
                nm.ck_at = 0;
            }
        } else if ((p[2] == 'N' && p[3] == 'O') || (p[2] == 'B' && p[3] == 'Y'))
            nm_lost();
        else if (p[2] == 'C' && p[3] == 'K' && n >= 36 && data && p[8] < 2u)
            nm_ck(p[8] + 1u, p + 12);
        return;
    }
    if (data && src == nm.ip && nm.st == NM_UP) {
        nm.heard = plat_ms();
        nm_midi_in(p, n);
    }
}

static void nm_send(void)                        /* what was played since the last frame: one RTP-MIDI packet */
{
    uint8_t b[512], *p = b + 14;
    uint32_t i, len;
    uint32_t pk[NM_RING];
    uint32_t np = 0;
    for (i = 0; i < nm_m_n; i++)
        pk[np++] = nm_m_buf[i];
    nm_m_n = 0;
    while (nm_r_r != nm_r_w && np < NM_RING) {
        pk[np++] = nm_r_buf[nm_r_r % NM_RING];
        nm_r_r++;
    }
    if (!np)
        return;
    for (i = 0; i < np; i++) {
        uint32_t st = pk[i] >> 8 & 0xFFu, c;
        if (st < 0x80u || (st >= 0xF0u && st < 0xF8u && st != 0xF2u && st != 0xF3u && st != 0xF1u))
            continue;
        c = st >= 0xF8u ? 0u : (st & 0xF0u) == 0xC0u || (st & 0xF0u) == 0xD0u || st == 0xF1u || st == 0xF3u ? 1u : 2u;
        if (p + 1 + 1 + c > b + sizeof b)
            break;
        if (p != b + 14)
            *p++ = 0;                            /* (a delta time of none) */
        *p++ = (uint8_t)st;
        if (c > 0u)
            *p++ = (uint8_t)(pk[i] >> 16 & 0x7Fu);
        if (c > 1u)
            *p++ = (uint8_t)(pk[i] >> 24 & 0x7Fu);
    }
    len = (uint32_t)(p - (b + 14));
    if (!len)
        return;
    b[0] = 0x80;
    b[1] = 0x61;
    nm_u16(b + 2, nm.seq++);
    nm_u32(b + 4, (uint32_t)nm_now());
    nm_u32(b + 8, nm.ssrc);
    b[12] = (uint8_t)(0x80u | len >> 8);         /* the long form always: B set, J, Z, P clear */
    b[13] = (uint8_t)len;
    plat_net_udp_send(NM_DATA, nm.ip, nm.pdata, b, 14u + len);
}

static void nm_frame(void)
{
    uint32_t now = plat_ms(), addr = plat_wifi_addr();
    int want = set.p[PA_NETMIDI] && addr;
    if (!nm.ssrc)
        nm.ssrc = 0x46310000u ^ plat_ms() * 2654435761u;
    if (nm.open && (!want || addr != nm.addr)) {   /* the network gone or changed: the ports go too */
        if (nm.st == NM_UP)
            nm_word("BY", NM_CTL, nm.pctl, nm.token);
        plat_net_udp_close(NM_CTL);
        plat_net_udp_close(NM_DATA);
        nm.open = 0;
        nm_lost();
    }
    if (want && !nm.open) {
        if (plat_net_udp_open(NM_CTL, nm_rx, (void *)0) == 0 &&
            plat_net_udp_open(NM_DATA, nm_rx, (void *)1) == 0) {
            nm.open = 1;
            nm.addr = addr;
            nm.st = NM_SEARCH;
            nm.at = 0;
        } else {
            plat_net_udp_close(NM_CTL);
            return;
        }
    }
    switch (nm.st) {
    case NM_SEARCH:                              /* ask every 2 s */
        if (!nm.at || now - nm.at >= 2000u) {
            static const uint8_t Q[] = "\013_apple-midi\004_udp\005local";
            nm.at = now;
            nm.qid = (uint16_t)(now | 1u);
            nm_ask(Q, sizeof Q, 12u);
        }
        break;
    case NM_INV_CTL:
    case NM_INV_DATA:                            /* invite, once a second, five times */
        if (!nm.at || now - nm.at >= 1000u) {
            if (nm.tries++ >= 5u) {
                nm_lost();
                break;
            }
            nm.at = now;
            if (nm.tries == 1u && nm.st == NM_INV_CTL)
                nm.token = now * 2246822519u + 1u;
            if (nm.st == NM_INV_CTL)
                nm_word("IN", NM_CTL, nm.pctl, nm.token);
            else
                nm_word("IN", NM_DATA, nm.pdata, nm.token);
        }
        break;
    case NM_UP:
        nm_send();
        if (!nm.ck_at || now - nm.ck_at >= 5000u) {   /* a sync every 5 s: its answers say it is there */
            nm.ck_at = now;
            nm_ck(0, 0);
        }
        if (now - nm.heard > 30000u)             /* nothing heard for 30 s: gone */
            nm_lost();
        break;
    default:
        break;
    }
}

static int nm_up(void) { return nm.st == NM_UP; }
