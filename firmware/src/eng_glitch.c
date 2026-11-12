/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Glitchbox contributors */
/* GLITCH (Glitchbox): a harsh mono/duo synth voice that chops itself up. Engine 7 (WHEEL's slot: the stored engine
 * numbers stay below 14, favorites.c / midi_learn.c keep 14 and 15 for themselves).
 *
 * Signal per voice: oscillator (WAVE) -> DRV (soft clip x1..x4 below 64, a triangle wavefolder x1..x8 above, into the
 * soft clip) -> the trapezoidal SVF low-pass (CUT, RES; RES 127 nearly self-oscillates; the ADSR / ENV DEST / LFO /
 * matrix move CUT as on ANALOG) -> CRSH (sample-and-hold 1..13 samples and 16..2 bits together) -> the micro-stutter
 * -> a DC block. From the filter on the signal runs at half scale (its soft knee: +-32383 at most), as ANALOG's.
 *   WAVE  SAW, SQR, DUAL (two saws 13 cents apart), RING (a saw ring-modulated by a sine at 2.375 x: metal), NOIS,
 *         FAT (two saws +-13 cents and a square an octave below), FM (a sine phase-modulated 1:1, index 2..8 following
 *         the envelope: a bright bark that closes), FMX (3.5:1, the modulator fed back on itself: clangy, nasty)
 * The filter is driven as an MS-20's: its band state saturates (the resonance screams instead of whistling), and above
 * DRV 0 the filtered signal goes through a fuzz again (x1..x6 into the soft clip). CRSH takes bits first (0..63) and
 * holds samples only above 64 (the rate reduction is what sounds like a toy).
 *
 * The micro-stutter (GLCH, SIZE, PJMP): each voice records its own output into a 3072-sample ring (70 ms) in the
 * part's engine memory. Time runs in segments of SIZE (256..3072 samples, 6..70 ms). At the end of each segment a
 * dice roll against GLCH decides: play live, or loop the last segment (recording pauses while it loops). A loop picks
 * a variation each segment: the whole segment, its half or its quarter (rolls), forwards or reversed, at the pitch or
 * jumped (PJMP: 0 none .. 127 any of -12 -7 -5 +5 +7 +12 semitones, by tape speed). Changes cross-fade over 32
 * samples (no clicks; the cuts stay hard). The first segment after a note-on always plays live (the attack). GLCH 0
 * costs nothing (no recording). Deterministic: the dice are seeded by the note and its velocity.
 *
 * Voices: at most 2 (poly), each with its own ring; MONO / LEGATO use the first. The filter state is kept on a
 * retrigger (keep), the ring is not. */
/* THE BASS FAMILY (FUZZ REES 808 GRIT GROW): the same engine, a different voice built to be low and big, not the
 * chopped lead above. Measured on a 55 Hz note, the lead chain at DRV 127 had no fundamental left (the wavefolder
 * above DRV 64 scatters a bass into 1 kHz fizz: -47 dB under it), so a "fuzz bass" there was a mid-range buzz. These
 * waves keep the bottom end by construction:
 *   osc -> CUT / RES (the SVF, before the fuzz: an acid line squeals into it) -> OCTV (full-wave rectified blend before
 *   the clip: the octave up) -> FUZZ (a Big-Muff's two stages: up to 56x into a tanh, a coupling high-pass that leaves
 *   the bass, a second stage with a little asymmetry) -> TONE (the Muff's stack: a low-pass at ~400 Hz against a high-pass
 *   at ~1.6 kHz; 64 scoops the mids, 0 is all low end, 127 all fizz) -> CRSH (bits, then sample hold above 64) -> + SUB,
 *   a clean sine at the pitch and one an octave below, AFTER everything, so no fuzz setting can eat it -> a slow DC block
 *   (11 Hz: it must not touch a 41 Hz bass).
 *   FUZZ  a saw and a square an octave down        REES  three saws +-7 cents (the beating wall of a reese)
 *   808   a sine that falls from +24 semitones in 25 ms (a kick that is a bass); the SUB octave stays steady
 *   GRIT  a saw and noise                          GROW  1:1 FM with feedback, its index follows the envelope
 * In this family EDIT 6 and 7 are TONE and OCTV (SIZE and PJMP in the lead waves), EDIT 5 is SUB (GLCH: the stutter
 * is not part of a bass: it would chop the sub) and EDIT 3 is called FUZZ. The lead waves SYNC (a hard-synced saw, its
 * slave swept by the envelope) and SUPR (three saws +-22 cents: Tron's wall) are in the chopped family. */
static const char *const N_GLITCH_WAVE[] = {"SAW", "SQR", "DUAL", "RING", "NOIS", "FAT", "FM", "FMX",
                                            "FUZZ", "REES", "808", "GRIT", "GROW", "SYNC", "SUPR", "HOWL", "MIC"};
enum { GW_SAW, GW_SQR, GW_DUAL, GW_RING, GW_NOIS, GW_FAT, GW_FM, GW_FMX,
       GW_FUZZ, GW_REES, GW_808, GW_GRIT, GW_GROW, GW_SYNC, GW_SUPR, GW_HOWL, GW_MIC };
#define GL_BASS(w) ((w) >= GW_FUZZ && (w) <= GW_GROW)
#define GL_HOWL(w) ((w) >= GW_HOWL)                  /* KRIECHSTROM: the feedback family (howl_render) */

#define GL_POLY 2u
#define GL_BUF 3072u
#define GL_XF 32                 /* cross-fade, samples */
typedef struct {
    union {
        int16_t buf[GL_BUF];     /* the voice's output, recorded while it plays live */
        int32_t bs[8];           /* the bass family's states in the same bytes (it does not chop): bass_render */
    };
    uint16_t w;                  /* next write index */
    uint16_t filled;             /* samples recorded since the note-on (to GL_BUF) */
    uint16_t cnt;                /* samples left in the segment */
    uint16_t start;              /* the loop's first sample (index into buf) */
    uint16_t llen;               /* the loop's length, samples */
    uint8_t loop;                /* 1 = playing the loop, 0 = live */
    uint8_t xf;                  /* cross-fade samples left */
    int32_t rpos;                /* read position in the loop, Q8 */
    int32_t spd;                 /* read speed, Q8 (256 = 1x, negative = reversed) */
    int32_t xfrom;               /* the sample the cross-fade starts from */
    int32_t last;                /* the last sample out */
    int32_t rng;
} gl_slot_t;
ENG_MEM_FITS("GLCH", sizeof(gl_slot_t) * GL_POLY);

/* tape speeds of the jumps, Q8, mildest first (PJMP opens them in this order): +7 -5 +12 -12 +5 -7 semitones */
static const int16_t GL_JUMP[6] = {384, 192, 512, 128, 342, 171};

static gl_slot_t *gl_slot_of(track_t *t, voice_t *v)
{
    uint32_t i;
    if (t < &trk[0] || t >= &trk[NPART])
        return 0;
    i = (uint32_t)(v - t->v);
    return i < GL_POLY ? (gl_slot_t *)ENG_MEM(t - trk) + i : 0;
}

/* the segment length of SIZE: 256..3072 samples */
static uint32_t gl_seg(int32_t size) { return 256u + (uint32_t)clamp(size, 0, 127) * (GL_BUF - 256u) / 127u; }

static void howl_note_on(track_t *t, voice_t *v, gl_slot_t *S);
static void glitch_note_on(track_t *t, voice_t *v)
{
    gl_slot_t *S = gl_slot_of(t, v);
    v->ph[1] = v->ph[0] + 0x40000000u;
    v->s[0] = v->s[1] = 0;                            /* filter (kept on a retrigger: keep) */
    if (!v->s[2])
        v->s[2] = 0x2545F491 ^ (int32_t)v->note;      /* noise */
    v->s[3] = 0;                                      /* CRSH: the held sample */
    v->s[4] = 0;                                      /* CRSH: the hold counter */
    v->s[5] = 0;                                      /* DC block, Q8 */
    v->s[6] = 0;                                      /* FMX feedback */
    v->s[7] = 0;                                      /* bass family: the SUB octave's phase */
    if (GL_HOWL(t->p[P_E0])) {                        /* KRIECHSTROM: the feedback family: an empty loop */
        howl_note_on(t, v, S);
        return;
    }
    if (GL_BASS(t->p[P_E0])) {                        /* the bass family starts at phase 0 (the sub, the 808's */
        v->ph[0] = 0;                                 /* sine and the fuzz's fundamental add, not fight) */
        v->ph[1] = t->p[P_E0] == GW_808 ? 163840u : 0x40000000u;   /* 808: the pitch drop left, Q16 (2.5 x the pitch) */
        v->ph[2] = 0;
        if (S) {
            uint32_t k;
            for (k = 0; k < 8u; k++)
                S->bs[k] = 0;
            S->loop = 0;
            S->xf = 0;
            S->filled = 0;
        }
        return;
    }
    if (S) {
        S->filled = 0;
        S->loop = 0;
        S->xf = 0;
        S->last = 0;
        S->cnt = (uint16_t)gl_seg(t->p[P_E6]);
        S->rng = (int32_t)(0x9E3779B9u ^ ((uint32_t)v->note << 8) ^ ((uint32_t)v->vel << 20));
        if (!S->rng)
            S->rng = 1;
    }
}

/* a triangle wavefolder: continuous, +-32767 out for any x (two's complement wrap of x + 32768 into 0..131071) */
static inline int32_t gl_fold(int32_t x)
{
    uint32_t u = (uint32_t)(x + 32768) & 0x1FFFFu;
    return u < 65536u ? (int32_t)u - 32768 : 98303 - (int32_t)u;
}

/* the end of a segment: live or a loop, and the loop's variation */
static void gl_segment(gl_slot_t *S, const int16_t *p, uint32_t seg)
{
    uint32_t r = noise32(&S->rng), was = S->loop;
    int32_t chance = p[P_E5] * 258;                   /* Q15 */
    S->cnt = (uint16_t)seg;
    if ((int32_t)(r >> 17) >= chance || S->filled < seg) {
        S->loop = 0;
    } else {
        uint32_t q = noise32(&S->rng), span = ((uint32_t)p[P_E7] * 6u + 126u) / 127u;   /* jumps to pick from: 0..6 */
        if (!was)                                     /* loop the last segment */
            S->start = (uint16_t)((S->w + GL_BUF - seg) % GL_BUF);
        S->loop = 1;
        S->llen = (uint16_t)(seg >> ((q >> 4) % 3u == 2u ? (q >> 6) % 2u + 1u : 0u));   /* rolls: 1/2, 1/4 */
        if (S->llen < 32u)
            S->llen = 32u;
        S->spd = 256;
        if (span && (q & 3u) != 0u)                   /* 3 in 4 loops jump when PJMP allows */
            S->spd = GL_JUMP[(q >> 8) % span];
        if ((q & 0x30u) == 0x30u)                     /* 1 in 4 reversed */
            S->spd = -S->spd;
        S->rpos = S->spd < 0 ? ((int32_t)S->llen << 8) - 1 : 0;
    }
    if (S->loop || was) {                             /* a new loop or back to live: cross-fade from here */
        S->xf = GL_XF;
        S->xfrom = S->last;
    }
}

/* a one-pole low-pass's coefficient for cutoff index c (SVF_G), Q16, as fx.c ins_lpk (that comes later in the build) */
static int32_t bs_lpk(int32_t c)
{
    uint32_t g = SVF_G[clamp(c, 0, 127)];
    return (int32_t)((g << 16) / (4096u + g));
}

/* the bass family (see the top). States in S->bs: 0 the coupling high-pass, 1 2 the tone stack's two low-passes (x16),
 * 3 the octave's rectifier average (Q8); the voice's: s[0] s[1] the SVF, s[2] noise, s[3] s[4] CRSH, s[5] the DC block
 * (Q10), s[6] GROW's feedback, s[7] the SUB octave's phase; ph[1] of the 808 its pitch drop left */
static void bass_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    gl_slot_t *S = gl_slot_of(t, v);
    uint32_t wave = (uint32_t)p[P_E0], i, ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2], inc = m->inc, inc2, inc3;
    uint32_t sph = (uint32_t)v->s[7];
    int32_t ic1 = v->s[0], ic2 = v->s[1], nst = v->s[2], held = v->s[3], hc = v->s[4], dc = v->s[5], fb = v->s[6];
    int32_t fz = p[P_E3], crsh = p[P_E4], sub = p[P_E5], tone = p[P_E6], octv = p[P_E7];
    int32_t g1 = 4096 + fz * fz * 14, hold = 1 + crsh / 16, sh = crsh * 13 / 127, subg = sub * 40;
    int32_t ka = bs_lpk(52), kb = bs_lpk(80), b0 = softclip(2400);
    uint32_t fmi = 393216u + (uint32_t)(m->envq15 > 0 ? m->envq15 : 0) * 24u;
    tsvf_t flt;
    if (!S)
        return;
    tsvf_coef(&flt, (p[P_E1] << 8) + m->cutoff, p[P_E2]);
    inc2 = inc + (inc >> 7);                              /* REES: +-0.78 % = +-13 cents ... */
    inc3 = inc - (inc >> 7);
    if (wave == GW_REES) {
        inc2 = inc + (inc >> 8);                          /* ... halved: +-7 cents */
        inc3 = inc - (inc >> 8);
    }
    for (i = 0; i < n; i++) {
        int32_t s, y, u, x16, o1, o2, hi, r;
        uint32_t ie = inc;
        switch (wave) {
        case GW_REES:
            s = (osc_saw(ph0, inc) + osc_saw(ph1, inc2) + osc_saw(ph2, inc3)) * 11 >> 5;
            ph1 += inc2;
            ph2 += inc3;
            break;
        case GW_808:
            ie = inc + (inc >> 8) * (ph1 >> 8);           /* the drop: inc x (1 + k), k falling to 0 in ~25 ms */
            s = osc_sine(ph0);
            ph1 -= (ph1 >> 10) + (ph1 ? 1u : 0u);
            break;
        case GW_GRIT:
            s = ((osc_saw(ph0, inc) * 5) >> 3) + ((int32_t)(noise32(&nst) >> 17) - 16384);
            break;
        case GW_GROW: {
            int32_t md = osc_sine(ph1 + (uint32_t)fb * 65536u);
            fb = md;
            s = osc_sine(ph0 + (uint32_t)md * fmi);
            ph1 += inc;
            break;
        }
        default:                                          /* FUZZ: a saw and a square an octave down */
            s = ((osc_saw(ph0, inc) * 5) >> 3) + ((osc_pulse(ph2, inc >> 1, 0x80000000u) * 3) >> 3);
            ph2 += inc >> 1;
            break;
        }
        ph0 += ie;
        y = tsvf_lp(&flt, s >> 1, &ic1, &ic2);            /* the filter at half scale ... */
        ic1 = softclip(ic1 >> 2) << 2;                    /* (the band state saturates: the resonance screams) */
        y = clamp(y << 1, -65535, 65535);                 /* ... and back */
        if (octv) {                                       /* OCTV: the rectified signal, its mean taken away */
            r = y < 0 ? -y : y;
            S->bs[3] += ((r << 8) - S->bs[3]) >> 10;
            y += ((r - (S->bs[3] >> 8)) * octv) >> 6;
            y = clamp(y, -65535, 65535);
        }
        u = softclip(((y >> 3) * g1) >> 9);               /* FUZZ: stage 1, 4x .. 56x into a tanh */
        S->bs[0] += ((u << 8) - S->bs[0]) >> 7;           /* the coupling high-pass, ~55 Hz */
        u -= S->bs[0] >> 8;
        u = softclip(u * 2 + 2400) - b0;                  /* stage 2: harder, a little lopsided (even harmonics) */
        x16 = u << 4;
        r = mulq16(x16 - S->bs[1], (uint32_t)ka);         /* TONE: a low-pass at ~400 Hz ... */
        o1 = r + S->bs[1];
        S->bs[1] = o1 + r;
        r = mulq16(x16 - S->bs[2], (uint32_t)kb);         /* ... against a high-pass at ~1.6 kHz */
        o2 = r + S->bs[2];
        S->bs[2] = o2 + r;
        hi = x16 - o2;
        s = ((((o1 * (127 - tone) + hi * tone) >> 7) * 11 >> 3) + 8) >> 4;   /* x1.38 (the leads' level; the high-pass rings to 2x) */
        if (crsh) {                                       /* CRSH: bits first, the hold only above 64 */
            if (--hc <= 0) {
                hc = hold;
                held = (s >> sh) << sh;
            }
            s = held;
        }
        if (subg) {                                       /* SUB: sines at the pitch and an octave down, clean */
            u = osc_sine(sph);
            s += ((osc_sine(ph0 - ie) + u) * subg) >> 14;
            sph += inc >> 1;
        }
        s = soft_knee(s, 24000);
        dc += s - (dc >> 10);                             /* a slow DC block (11 Hz): a 41 Hz bass stays whole */
        s -= dc >> 10;
        out[i] += voice_amp(s, m, i) << 1;
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ph[2] = ph2;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = nst;
    v->s[3] = held;
    v->s[4] = hc;
    v->s[5] = dc;
    v->s[6] = fb;
    v->s[7] = (int32_t)sph;
}

/* KRIECHSTROM: THE FEEDBACK FAMILY (HOWL MIC): no oscillator, a loop that feeds itself. The voice's ring (S->buf) is
 * the loop; what comes round goes through the filter (CUT RES, the envelopes and the LFO move CUT as on the leads), a
 * low cut, GAIN and a tanh, and back in. Under the loop's loss (GAIN under ~56: x1) a note is struck and dies away;
 * above, it sustains, and higher it screams into the tanh. Noise starts it: BITE a burst at the note-on (10 ms) and, under
 * it, from GAIN x0.9 up, a floor that never stops (so a loop over its loss grows out of nothing, as feedback does). BLOOM: GAIN climbs
 * from half to the knob over 0 .. ~3 s (the howl swells in). CHAOS: the loop's length wanders (a random walk, up to
 * +-6 %), the pitch with it. CRSH takes bits inside the loop (the grit goes round and round).
 *   HOWL  the loop as long as the note's period: a string in overdrive, in tune with the keys (a filter's delay
 *         flattens it a little)
 *   MIC   a microphone in front of a speaker: the loop as long as a room (54 ms), a resonator tuned to the note in it
 *         (RES: how narrow); the howl locks onto the room's mode at the note (its modes 18 Hz apart: on the key at
 *         RES 110, a wide RES lets it slip to a neighbour) and CHAOS, moving the microphone, makes it jump between modes
 * States: S->w the loop's write, S->rpos the gain now (Q12), S->xfrom the walk, S->spd its target, S->cnt the burst
 * left, S->rng the noise; the voice's s[0] s[1] the SVF, s[2] s[3] MIC's resonator (a second SVF), s[5] the DC block
 * (Q10), s[6] the low cut (Q8) */
#define HW_ROOM (2400u << 8)                          /* MIC's loop: 2400 samples, 54 ms (Q8) */
static int32_t howl_gain(const int16_t *p)           /* GAIN, Q12: x0.25 .. x2 (MIC x2.5: a room loses more) */
{
    return 1024 + p[P_E3] * (p[P_E0] == GW_MIC ? 72 : 56);
}
static void howl_note_on(track_t *t, voice_t *v, gl_slot_t *S)
{
    uint32_t k;
    v->s[0] = v->s[1] = v->s[2] = v->s[3] = v->s[5] = v->s[6] = 0;
    if (!S)
        return;
    for (k = 0; k < GL_BUF; k++)
        S->buf[k] = 0;
    S->w = 0;
    S->rpos = t->p[P_E7] ? howl_gain(t->p) / 2 : howl_gain(t->p);
    S->xfrom = S->spd = 0;
    S->cnt = 441;
    S->rng = (int32_t)(0x9E3779B9u ^ ((uint32_t)v->note << 8) ^ ((uint32_t)v->vel << 20));
    if (!S->rng)
        S->rng = 1;
    S->loop = S->xf = 0;
    S->filled = 0;
}
static void howl_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    gl_slot_t *S = gl_slot_of(t, v);
    uint32_t i, mic = p[P_E0] == GW_MIC, inc = m->inc, w, len;
    int32_t ic1 = v->s[0], ic2 = v->s[1], r1 = v->s[2], r2 = v->s[3], dc = v->s[5], lc = v->s[6];
    int32_t gt = howl_gain(p), g, bloom = p[P_E7], chaos = p[P_E5], bite = p[P_E6], crsh = p[P_E4];
    int32_t sh = crsh * 13 / 127, rk = 0;
    tsvf_t flt, rf;
    if (!S)
        return;
    {   /* BLOOM: the gain towards the knob (a line: 0 at once .. 127 ~3 s from half) */
        int32_t st = (gt * 32) / (1 + bloom * bloom * 10);
        st = st < 1 ? 1 : st;
        S->rpos = S->rpos < gt ? (S->rpos + st < gt ? S->rpos + st : gt) : gt;
        g = S->rpos;
    }
    {   /* CHAOS: a random walk of the length, a new target every 16 blocks */
        if (!(S->filled++ & 15u))
            S->spd = (int32_t)(noise32(&S->rng) >> 16) - 32768;
        S->xfrom += (S->spd - S->xfrom) >> 4;
    }
    if (mic) {
        len = HW_ROOM;
        /* the resonator: the SVF's band at the note (cut index: 30 Hz .. 16 kHz in 127 steps, Q8), RES its width;
         * the band peaks at 1 / k, so k (Q12) back down to 1 at the note */
        rk = 8192 - p[P_E2] * 7600 / 127;
        tsvf_coef_k(&rf, 13904 + 299 * ((int32_t)v->note - 69), rk);
    } else {
        len = (inc >> 8) ? 0xFFFFFFFFu / (inc >> 8) : (GL_BUF - 2u) << 8;   /* the period, Q8 */
    }
    len += (uint32_t)(((int32_t)(len >> 8) * ((S->xfrom * chaos) >> 11)) >> 8);   /* (+-1/16 at the top) */
    len = len < (2u << 8) ? 2u << 8 : len > ((GL_BUF - 2u) << 8) ? (GL_BUF - 2u) << 8 : len;
    tsvf_coef(&flt, (p[P_E1] << 8) + m->cutoff, mic ? 40 : p[P_E2]);
    w = S->w;
    for (i = 0; i < n; i++) {
        int32_t x, y, e, a0, a1;
        uint32_t li = len >> 8, fr = len & 255u, ri = w >= li ? w - li : w + GL_BUF - li;
        a0 = S->buf[ri];
        a1 = S->buf[ri ? ri - 1u : GL_BUF - 1u];
        x = a0 + (((a1 - a0) * (int32_t)fr) >> 8);
        if (mic) {                                    /* the resonator at the note: the SVF's band (tsvf_lp's v1) */
            int32_t v3 = x - r2, v1 = (rf.a1 * r1 + rf.a2 * v3) >> 13, v2 = r2 + ((rf.a2 * r1 + rf.a3 * v3) >> 13);
            r1 = clamp(2 * v1 - r1, -150000, 150000);
            r2 = clamp(2 * v2 - r2, -150000, 150000);
            x = clamp((v1 * rk) >> 12, -65535, 65535);
        }
        y = tsvf_lp(&flt, x >> 1, &ic1, &ic2) << 1;   /* the filter (MIC: a gentle one, the room's air) */
        lc += ((y << 8) - lc) >> 8;                   /* the low cut, ~27 Hz */
        y -= lc >> 8;
        y = softclip(((y >> 2) * g) >> 10);           /* GAIN into the tanh */
        if (sh)
            y = (y >> sh) << sh;
        e = (int32_t)(noise32(&S->rng) >> 17) - 16384;   /* the noise: the burst, then the floor */
        if (S->cnt) {
            S->cnt--;
            e = (e * bite) >> 6;
        } else {
            e = gt >= 3686 ? (e * (bite + 8)) >> 13 : 0;   /* (the floor only near the threshold and over it) */
        }
        S->buf[w] = (int16_t)clamp(y + e, -32767, 32767);
        w = w + 1u < GL_BUF ? w + 1u : 0u;
        dc += y - (dc >> 10);
        y -= dc >> 10;
        out[i] += voice_amp(soft_knee(y, 24000), m, i) << 1;
    }
    S->w = (uint16_t)w;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = r1;
    v->s[3] = r2;
    v->s[5] = dc;
    v->s[6] = lc;
}

/* EDIT 3 .. 7 of the bass family as the bass family names them (the same ranges and defaults) */
static const param_desc_t *glitch_desc(const track_t *t, uint32_t k)
{
    static const param_desc_t H[8] = {                /* KRIECHSTROM: the feedback family's */
        {0}, {0}, {0},
        {"GAIN", F_PCT, 0, 127, 80, 0, 0}, {0},
        {"CHAOS", F_PCT, 0, 127, 30, 0, 0}, {"BITE", F_PCT, 0, 127, 64, 0, 0}, {"BLOOM", F_PCT, 0, 127, 40, 0, 0},
    };
    if (GL_HOWL(t->p[P_E0]))
        return k >= 3u && k != 4u ? &H[k] : 0;
    static const param_desc_t B[8] = {
        {0}, {0}, {0},
        {"FUZZ", F_PCT, 0, 127, 110, 0, 0}, {0},
        {"SUB", F_PCT, 0, 127, 40, 0, 0}, {"TONE", F_PCT, 0, 127, 48, 0, 0}, {"OCTV", F_PCT, 0, 127, 0, 0, 0},
    };
    return GL_BASS(t->p[P_E0]) && k >= 3u && k != 4u ? &B[k] : 0;
}

static void glitch_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    gl_slot_t *S = gl_slot_of(t, v);
    if (GL_BASS(p[P_E0])) {
        bass_render(t, v, out, n, m);
        return;
    }
    if (GL_HOWL(p[P_E0])) {
        howl_render(t, v, out, n, m);
        return;
    }
    uint32_t wave = (uint32_t)p[P_E0], i, seg = gl_seg(p[P_E6]);
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2], inc = m->inc, inc2;
    int32_t ic1 = v->s[0], ic2 = v->s[1], nst = v->s[2], held = v->s[3], hc = v->s[4], dc = v->s[5];
    int32_t drv = p[P_E3], crsh = p[P_E4], g = 8192 + drv * 30 * 4096 / 127;  /* drive Q12: x2..x32 */
    int32_t hold = 1 + crsh / 16, sh = crsh * 13 / 127;   /* CRSH: 0..13 bits off and a 1..8 sample hold together */
    int32_t gp = 8192 + drv * 14 * 4096 / 127;                                 /* the clipper after the filter: x2..x16 */
    uint32_t fmi = 393216u + (uint32_t)(m->envq15 > 0 ? m->envq15 : 0) * 24u   /* FM index 3..9 by the envelope, */
                 + (uint32_t)drv * 6192u;                                       /* +0..6 by DRV (Q32 cycles per Q15) */
    int32_t fb = v->s[6];                             /* FMX: the modulator's last sample (feedback) */
    int glitch = S && p[P_E5] > 0;
    tsvf_t flt;
    tsvf_coef(&flt, (p[P_E1] << 8) + m->cutoff, p[P_E2]);
    inc2 = wave == GW_RING ? (inc >> 3) * 19u : wave == GW_FM ? inc : wave == GW_FMX ? (inc >> 1) * 7u
         : inc + (inc >> 7);                          /* RING 2.375 x, FM 1 x, FMX 3.5 x, DUAL / FAT +13 cents */
    for (i = 0; i < n; i++) {
        int32_t s, y;
        switch (wave) {
        case GW_SQR:
            s = osc_pulse(ph0, inc, 0x80000000u) << 1;
            break;
        case GW_DUAL:
            s = (osc_saw(ph0, inc) + osc_saw(ph1, inc2)) >> 1;
            break;
        case GW_RING:
            s = mulq15(osc_saw(ph0, inc), osc_sine(ph1));
            s += s >> 1;
            break;
        case GW_NOIS:
            s = (int32_t)(noise32(&nst) >> 17) - 16384;
            s <<= 1;
            break;
        case GW_SYNC: {                               /* a hard-synced saw: the slave 1.5x .. 5.5x, swept by the envelope */
            uint32_t incs = inc + (inc >> 1) + (((inc >> 15) * (uint32_t)(m->envq15 > 0 ? m->envq15 : 0)) << 2);
            if ((uint32_t)(ph0 + inc) < ph0)
                ph1 = 0;
            s = osc_saw(ph1, incs);
            ph1 += incs;
            break;
        }
        case GW_SUPR:                                 /* three saws +-22 cents */
            s = (osc_saw(ph0, inc) + osc_saw(ph1, inc + (inc >> 6) / 2 * 1) + osc_saw(ph2, inc - (inc >> 6) / 2 * 1)) * 11 >> 5;
            ph2 += inc - (inc >> 6) / 2;
            break;
        case GW_FAT:
            s = ((osc_saw(ph0, inc) + osc_saw(ph1, inc2)) * 3 >> 3) + (osc_pulse(ph2, inc >> 1, 0x80000000u) >> 1);
            ph2 += inc >> 1;                          /* (the sub square an octave below) */
            break;
        case GW_FM:
        case GW_FMX: {
            int32_t md = osc_sine(ph1 + (wave == GW_FMX ? (uint32_t)fb * 65536u : 0u));
            fb = md;
            s = osc_sine(ph0 + (uint32_t)md * fmi);
            break;
        }
        default:
            s = osc_saw(ph0, inc);
            break;
        }
        ph0 += inc;
        ph1 += inc2;
        if (drv) {                                    /* asymmetric hard clip (even harmonics), the folder above 64 */
            int32_t x = (clamp(s, -32767, 32767) * (g >> 2)) >> 10;
            if (drv >= 64)
                x = gl_fold(x) + (gl_fold(x >> 1) >> 1);
            s = clamp(x, -22000, 32767);
        }
        y = tsvf_lp(&flt, s >> 1, &ic1, &ic2);        /* the filter at half scale, then a soft knee */
        ic1 = softclip(ic1 >> 2) << 2;                /* MS-20: the band state saturates (+-131000) */
        {
            int32_t a = y < 0 ? -y : y;
            if (a > 16000) {
                a = 16000 + (softclip((a - 16000) * 2) >> 1);
                y = y < 0 ? -a : a;
            }
        }
        s = drv ? clamp((y * (gp >> 4)) >> 8, -26000, 26000) : y;   /* hard clipped after the filter */
        if (crsh) {                                   /* rate and bits */
            if (--hc <= 0) {
                hc = hold;
                held = (s >> sh) << sh;
            }
            s = held;
        }
        if (glitch) {
            if (!S->loop) {
                S->buf[S->w] = (int16_t)s;
                if (++S->w >= GL_BUF)
                    S->w = 0;
                if (S->filled < GL_BUF)
                    S->filled++;
            } else {                                  /* the loop, read between samples */
                int32_t L8 = (int32_t)S->llen << 8, k = S->rpos >> 8, fr = S->rpos & 255, a, b;
                uint32_t ia = S->start + (uint32_t)k, ib = (uint32_t)k + 1u < S->llen ? ia + 1u : S->start;
                if (ia >= GL_BUF)                     /* (the next sample: the loop's own, wrapping inside it) */
                    ia -= GL_BUF;
                if (ib >= GL_BUF)
                    ib -= GL_BUF;
                a = S->buf[ia];
                b = S->buf[ib];
                s = a + (((b - a) * fr) >> 8);
                S->rpos += S->spd;
                if (S->rpos >= L8)
                    S->rpos -= L8;
                else if (S->rpos < 0)
                    S->rpos += L8;
            }
            if (S->xf) {
                s += ((S->xfrom - s) * S->xf) / GL_XF;
                S->xf--;
            }
            S->last = s;
            if (!--S->cnt)
                gl_segment(S, p, seg);
        }
        dc += s - (dc >> 8);                          /* DC block, ~27 Hz (the folder, the bits, the loops); dc Q8: */
        s -= dc >> 8;                                 /* the shift's floor costs 1/256 of a step, not 255 steps */
        out[i] += voice_amp(s, m, i) << 1;
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ph[2] = ph2;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = nst;
    v->s[3] = held;
    v->s[4] = hc;
    v->s[5] = dc;
    v->s[6] = fb;
}

/* {WAVE, CUT, RES, DRV, CRSH, GLCH, SIZE, PJMP} */
static const preset_t GLITCH_PRESETS[] = {
    {"DESTROYER", {GW_FAT, 62, 108, 120, 80, 70, 40, 60}, {0, 60, 100, 30}, 40, 1, FX(30, 0, 30, 20), PAT(4)},
    {"FUZZ BASS", {GW_FUZZ, 62, 70, 120, 20, 60, 40, 0}, {0, 60, 110, 20}, 30, 1, FX(20, 0, 0, 5), PAT(2)},
    {"BROKEN", {GW_FMX, 95, 80, 120, 95, 110, 20, 100}, {0, 70, 80, 40}, 20, 0, FX(20, 0, 40, 30), PAT(13)},
    {"SCREAM", {GW_SAW, 55, 124, 110, 65, 20, 60, 30}, {2, 80, 110, 50}, 55, 1, FX(25, 0, 40, 30), PAT(4)},
    {"NOIZ HIT", {GW_NOIS, 75, 120, 127, 100, 90, 10, 0}, {0, 60, 20, 30}, 60, 0, FX(30, 0, 30, 20), PAT(12)},
    {"HUM", {GW_FAT, 40, 105, 105, 70, 20, 127, 20}, {60, 90, 127, 90}, 10, 1, FX(0, 30, 30, 60), PAT(8)},
    {"NASTY FM", {GW_FMX, 105, 70, 120, 75, 30, 30, 40}, {0, 70, 70, 35}, 15, 1, FX(30, 0, 35, 20), PAT(4)},
    {"CRUSH LEAD", {GW_FAT, 90, 95, 127, 105, 25, 40, 50}, {0, 70, 100, 40}, 35, 1, FX(35, 10, 40, 25), PAT(4)},
    {"SQR FUZZ", {GW_SQR, 58, 100, 127, 70, 0, 64, 0}, {0, 50, 100, 20}, 50, 1, FX(45, 0, 0, 10), PAT(2)},
    {"FM BASS", {GW_FM, 70, 75, 115, 60, 0, 64, 0}, {0, 45, 60, 20}, 30, 1, FX(25, 0, 0, 5), PAT(2)},
    {"MELTDOWN", {GW_FMX, 80, 115, 127, 120, 60, 30, 80}, {0, 60, 100, 30}, 30, 1, FX(40, 0, 20, 20), PAT(4)},
    {"RUST SAW", {GW_DUAL, 70, 110, 127, 90, 10, 50, 0}, {0, 60, 100, 30}, 45, 1, FX(40, 0, 20, 20), PAT(2)},
    /* the bass family: {WAVE, CUT, RES, FUZZ, CRSH, SUB, TONE, OCTV} */
    {"REESE", {GW_REES, 58, 75, 100, 30, 50, 55, 20}, {5, 70, 115, 30}, 20, 1, FX(15, 20, 0, 10), PAT(2)},
    {"808 DIST", {GW_808, 100, 30, 115, 20, 70, 35, 0}, {0, 80, 50, 45}, 0, 1, FX(20, 0, 0, 5), PAT(2)},
    {"GRIT BASS", {GW_GRIT, 55, 90, 127, 75, 45, 60, 30}, {0, 55, 100, 20}, 35, 1, FX(25, 0, 0, 5), PAT(2)},
    {"GROWL", {GW_GROW, 78, 85, 110, 40, 75, 72, 40}, {0, 75, 90, 25}, 40, 1, FX(20, 0, 10, 10), PAT(2)},
    {"ACID FUZZ", {GW_FUZZ, 50, 120, 105, 25, 55, 50, 0}, {0, 45, 60, 20}, 60, 1, FX(20, 0, 20, 10), PAT(2)},
    {"OCTA SMASH", {GW_FUZZ, 70, 60, 127, 60, 55, 80, 90}, {0, 60, 110, 20}, 25, 1, FX(30, 0, 0, 5), PAT(2)},

    /* KRIECHSTROM's sounds: appended (the stored preset numbers of the ones above stay) */
    {"GRINDER", {GW_GRIT, 60, 100, 120, 40, 20, 50, 0}, {0, 70, 90, 30}, 40, 1, FX(30, 0, 10, 10), PAT(2)},
    {"FILTH", {GW_SUPR, 55, 80, 127, 60, 10, 60, 0}, {0, 80, 100, 30}, 30, 1, FX(40, 0, 10, 10), PAT(2)},
    {"SCAR TISSUE", {GW_SYNC, 70, 105, 115, 50, 40, 40, 20}, {0, 60, 80, 30}, 45, 1, FX(25, 0, 25, 15), PAT(4)},
    {"DEAD WIRE", {GW_SQR, 50, 110, 100, 100, 60, 30, 0}, {0, 40, 40, 20}, 55, 1, FX(20, 0, 20, 10), PAT(1)},
    {"BITROT", {GW_SAW, 60, 90, 90, 120, 50, 50, 0}, {0, 70, 80, 30}, 35, 1, FX(10, 0, 20, 15), PAT(2)},
    {"BREAKER", {GW_FUZZ, 50, 115, 127, 60, 30, 40, 0}, {0, 45, 40, 20}, 60, 1, FX(45, 0, 5, 10), PAT(2)},
    {"FAULT LINE", {GW_REES, 58, 95, 120, 50, 80, 50, 40}, {0, 80, 90, 30}, 30, 1, FX(30, 10, 20, 15), PAT(4)},
    {"FUZZ GUT", {GW_GROW, 60, 70, 127, 30, 0, 64, 0}, {0, 70, 110, 20}, 25, 1, FX(40, 0, 0, 5), PAT(2)},
    {"VOLTAGE", {GW_FMX, 75, 100, 120, 80, 50, 40, 30}, {0, 50, 60, 25}, 40, 1, FX(30, 0, 20, 15), PAT(4)},
    {"NERVE", {GW_FM, 85, 90, 110, 60, 90, 20, 60}, {0, 35, 20, 20}, 50, 0, FX(20, 0, 30, 20), PAT(13)},
    /* KRIECHSTROM: GLCH, the signature: 20 more, made for it */
    {"WIRE BRUSH", {GW_NOIS, 90, 60, 110, 90, 70, 20, 0}, {0, 60, 30, 40}, 50, 0, FX(20, 0, 25, 25), PAT(12)},
    {"SPLINTER", {GW_FMX, 100, 70, 127, 110, 120, 10, 110}, {0, 60, 30, 40}, 30, 0, FX(25, 0, 35, 25), PAT(13)},
    {"BLOODLINE", {GW_REES, 45, 100, 120, 30, 10, 60, 0}, {0, 60, 100, 30}, 35, 1, FX(25, 0, 10, 10), PAT(2)},
    {"SICK ENGINE", {GW_GRIT, 52, 110, 127, 60, 40, 50, 0}, {0, 60, 100, 30}, 40, 1, FX(30, 0, 10, 10), PAT(2)},
    {"RUST BELT", {GW_DUAL, 60, 90, 120, 70, 20, 64, 0}, {0, 60, 100, 30}, 40, 1, FX(25, 0, 15, 10), PAT(2)},
    {"CONCRETE", {GW_SUPR, 48, 95, 127, 50, 0, 70, 0}, {0, 60, 100, 30}, 30, 1, FX(35, 0, 5, 10), PAT(8)},
    {"STATIC LEAD", {GW_SAW, 85, 115, 110, 90, 30, 40, 20}, {2, 80, 110, 50}, 45, 1, FX(25, 10, 35, 30), PAT(4)},
    {"HOWLER", {GW_SYNC, 95, 124, 120, 40, 20, 50, 40}, {2, 80, 110, 50}, 55, 1, FX(30, 0, 40, 35), PAT(4)},
    {"BURN UNIT", {GW_FAT, 70, 100, 127, 100, 50, 60, 30}, {2, 80, 110, 50}, 40, 1, FX(35, 0, 30, 25), PAT(4)},
    {"CAUTERIZE", {GW_RING, 80, 90, 115, 80, 60, 30, 60}, {0, 50, 0, 40}, 50, 0, FX(20, 0, 40, 40), PAT(13)},
    {"STUTTER BASS", {GW_808, 60, 80, 120, 40, 110, 50, 0}, {0, 60, 100, 30}, 35, 1, FX(25, 0, 10, 10), PAT(1)},
    {"FRACTAL", {GW_FMX, 90, 85, 100, 120, 100, 20, 127}, {0, 60, 30, 40}, 30, 0, FX(20, 0, 45, 35), PAT(13)},
    {"HUSHED FUZZ", {GW_FUZZ, 35, 60, 90, 20, 0, 70, 0}, {80, 100, 120, 90}, 10, 0, FX(10, 40, 20, 70), PAT(5)},
    {"WARHEAD", {GW_GROW, 55, 115, 127, 60, 30, 64, 0}, {0, 60, 100, 30}, 45, 1, FX(35, 0, 10, 10), PAT(2)},
    {"SCREWDRIVER", {GW_SQR, 70, 110, 127, 110, 80, 30, 50}, {2, 80, 110, 50}, 50, 1, FX(30, 0, 30, 20), PAT(4)},
    {"CORRODED PAD", {GW_FAT, 40, 80, 90, 90, 20, 127, 0}, {80, 100, 120, 90}, 15, 0, FX(15, 50, 25, 90), PAT(5)},
    {"TERMINAL HUM", {GW_DUAL, 30, 90, 100, 60, 10, 127, 0}, {80, 100, 120, 90}, 10, 0, FX(20, 40, 20, 100), PAT(8)},
    {"BROKEN RADIO", {GW_NOIS, 80, 100, 90, 120, 120, 20, 90}, {0, 60, 30, 40}, 20, 0, FX(15, 20, 45, 40), PAT(5)},
    {"SPIKE", {GW_FM, 95, 100, 120, 80, 50, 15, 70}, {0, 50, 0, 40}, 55, 0, FX(25, 0, 35, 25), PAT(13)},
    {"MACHINE GUN", {GW_808, 70, 90, 127, 90, 127, 30, 0}, {0, 60, 30, 40}, 60, 0, FX(40, 0, 10, 20), PAT(12)},
    /* KRIECHSTROM: the feedback family {WAVE, CUT, RES, GAIN, CRSH, CHAOS, BITE, BLOOM} */
    {"FEEDBACK", {GW_HOWL, 100, 20, 80, 0, 20, 64, 30}, {0, 60, 127, 60}, 0, 1, FX(0, 0, 20, 30), PAT(18)},
    {"STRUNG OUT", {GW_HOWL, 115, 10, 50, 0, 0, 110, 0}, {0, 80, 0, 40}, 0, 0, FX(10, 0, 30, 20), PAT(0)},
    {"SCREAMER", {GW_HOWL, 95, 60, 127, 30, 40, 80, 10}, {0, 60, 127, 40}, 0, 1, FX(30, 0, 20, 20), PAT(0)},
    {"SWELL", {GW_HOWL, 90, 30, 85, 0, 25, 30, 110}, {0, 60, 127, 80}, 0, 0, FX(0, 0, 30, 50), PAT(18)},
    {"WOUNDED", {GW_HOWL, 70, 40, 75, 20, 100, 60, 40}, {0, 60, 127, 50}, 0, 1, FX(20, 0, 20, 30), PAT(0)},
    {"PA HOWL", {GW_MIC, 110, 110, 90, 0, 20, 20, 60}, {0, 60, 127, 70}, 0, 1, FX(0, 0, 20, 40), PAT(18)},
    {"WRONG ROOM", {GW_MIC, 100, 100, 100, 0, 90, 40, 30}, {0, 60, 127, 60}, 0, 1, FX(0, 0, 25, 40), PAT(0)},
    {"STAGE FRIGHT", {GW_MIC, 105, 115, 120, 50, 50, 50, 10}, {0, 60, 127, 50}, 0, 1, FX(25, 0, 10, 30), PAT(0)},
};

/* KRIECHSTROM: each sound's INSERT (Glitchbox kept it in preset_t; here a table beside the presets, so the shared
 * Felucca-Engines header stays as it is): TYPE A B C MIX, each + 1, 0 = the track's default (OFF). preset_ins() puts it
 * on a track after a sound load (ui.c, project.c), as Glitchbox's did */
enum { INS_SOFT = 1, INS_HARD, INS_FOLD, INS_FUZZ, INS_CRUSH, INS_MUFF = 9, INS_BASS, INS_SMASH, INS_DAMG };
#define INS_V(ty, a, b, c, m) (ty) + 1, (a) + 1, (b) + 1, (c) + 1, (m) + 1
static const uint8_t GLITCH_INS[][5] = {
    {INS_V(INS_MUFF, 90, 64, 120, 100)},   /* DESTROYER */
    {INS_V(INS_BASS, 100, 96, 96, 127)},   /* FUZZ BASS */
    {0, 0, 0, 0, 0},   /* BROKEN */
    {INS_V(INS_FUZZ, 90, 96, 114, 100)},   /* SCREAM */
    {INS_V(INS_SMASH, 100, 40, 111, 127)},   /* NOIZ HIT */
    {0, 0, 0, 0, 0},   /* HUM */
    {0, 0, 0, 0, 0},   /* NASTY FM */
    {INS_V(INS_CRUSH, 40, 70, 100, 100)},   /* CRUSH LEAD */
    {INS_V(INS_MUFF, 110, 70, 118, 127)},   /* SQR FUZZ */
    {0, 0, 0, 0, 0},   /* FM BASS */
    {INS_V(INS_FOLD, 100, 96, 117, 110)},   /* MELTDOWN */
    {0, 0, 0, 0, 0},   /* RUST SAW */
    {INS_V(INS_MUFF, 80, 50, 114, 90)},   /* REESE */
    {INS_V(INS_BASS, 110, 96, 110, 127)},   /* 808 DIST */
    {INS_V(INS_BASS, 120, 90, 96, 127)},   /* GRIT BASS */
    {0, 0, 0, 0, 0},   /* GROWL */
    {0, 0, 0, 0, 0},   /* ACID FUZZ */
    {INS_V(INS_SMASH, 90, 50, 112, 127)},   /* OCTA SMASH */
    /* KRIECHSTROM's sounds */
    {INS_V(INS_SMASH, 95, 45, 110, 110)},   /* GRINDER */
    {INS_V(INS_FUZZ, 85, 80, 112, 100)},   /* FILTH */
    {0, 0, 0, 0, 0},   /* SCAR TISSUE */
    {0, 0, 0, 0, 0},   /* DEAD WIRE */
    {INS_V(INS_CRUSH, 50, 60, 100, 90)},   /* BITROT */
    {INS_V(INS_MUFF, 100, 60, 118, 120)},   /* BREAKER */
    {0, 0, 0, 0, 0},   /* FAULT LINE */
    {INS_V(INS_BASS, 110, 90, 100, 127)},   /* FUZZ GUT */
    {INS_V(INS_FOLD, 80, 90, 112, 90)},   /* VOLTAGE */
    {0, 0, 0, 0, 0},   /* NERVE */
    /* KRIECHSTROM: GLCH, the signature */
    {0, 0, 0, 0, 0},   /* WIRE BRUSH */
    {0, 0, 0, 0, 0},   /* SPLINTER */
    {INS_V(INS_MUFF, 85, 40, 112, 90)},   /* BLOODLINE */
    {INS_V(INS_SMASH, 100, 50, 110, 120)},   /* SICK ENGINE */
    {INS_V(INS_BASS, 115, 80, 100, 127)},   /* RUST BELT */
    {INS_V(INS_MUFF, 100, 30, 116, 110)},   /* CONCRETE */
    {0, 0, 0, 0, 0},   /* STATIC LEAD */
    {INS_V(INS_FUZZ, 90, 90, 112, 100)},   /* HOWLER */
    {INS_V(INS_FOLD, 95, 90, 114, 100)},   /* BURN UNIT */
    {0, 0, 0, 0, 0},   /* CAUTERIZE */
    {0, 0, 0, 0, 0},   /* STUTTER BASS */
    {0, 0, 0, 0, 0},   /* FRACTAL */
    {INS_V(INS_MUFF, 60, 20, 110, 70)},   /* HUSHED FUZZ */
    {INS_V(INS_SMASH, 110, 40, 112, 127)},   /* WARHEAD */
    {0, 0, 0, 0, 0},   /* SCREWDRIVER */
    {INS_V(INS_CRUSH, 40, 60, 100, 80)},   /* CORRODED PAD */
    {0, 0, 0, 0, 0},   /* TERMINAL HUM */
    {0, 0, 0, 0, 0},   /* BROKEN RADIO */
    {0, 0, 0, 0, 0},   /* SPIKE */
    {INS_V(INS_SMASH, 95, 40, 110, 110)},   /* MACHINE GUN */
    {0, 0, 0, 0, 0},   /* FEEDBACK */
    {0, 0, 0, 0, 0},   /* STRUNG OUT */
    {INS_V(INS_FUZZ, 80, 90, 110, 80)},   /* SCREAMER */
    {0, 0, 0, 0, 0},   /* SWELL */
    {0, 0, 0, 0, 0},   /* WOUNDED */
    {0, 0, 0, 0, 0},   /* PA HOWL */
    {0, 0, 0, 0, 0},   /* WRONG ROOM */
    {INS_V(INS_CRUSH, 60, 70, 100, 70)},   /* STAGE FRIGHT */
};
_Static_assert(sizeof GLITCH_INS / sizeof GLITCH_INS[0] == sizeof GLITCH_PRESETS / sizeof GLITCH_PRESETS[0],
               "GLITCH_INS: one row a preset");

static const engine_t ENG_GLITCH = {
    .name = "GLCH",
    .page_title = {"TONE", "CHOP"},
    .edit = {
        {"WAVE", F_ENUM, 0, 16, GW_FAT, N_GLITCH_WAVE, 0},
        {"CUT", F_CUTOFF, 0, 127, 80, 0, 0},
        {"RES", F_PCT, 0, 127, 95, 0, 0},
        {"DRV", F_PCT, 0, 127, 110, 0, 0},
        {"CRSH", F_PCT, 0, 127, 70, 0, 0},
        {"GLCH", F_PCT, 0, 127, 40, 0, 0},
        {"SIZE", F_PCT, 0, 127, 48, 0, 0},
        {"PJMP", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = GLITCH_PRESETS,
    .npresets = NELEM(GLITCH_PRESETS),
    .note_on = glitch_note_on,
    .render = glitch_render,
    .desc = glitch_desc,
    .knob = {P_E1, P_E2, P_E3, P_E5},
    .poly = GL_POLY,
    .keep = 0x03,                /* the filter */
    .mem = sizeof(gl_slot_t) * GL_POLY,
};

