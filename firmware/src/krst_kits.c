/* SPDX-License-Identifier: GPL-3.0-only */
/* KRIECHSTROM's own drum kits (KIT ROOM GATE MARCH ARC ROT SLAB in eng_drum.c): short impacts, not notes.
 *
 * A lane is up to four layers summed, then the lane's post: a drive (half tanh, half a hard clip), a sample hold
 * (fixed, or a rate that falls through the hit: ROT), a crush, a hard cut with a 4 ms fade. A layer is
 *   TONE   a sine swept from f1 to f0 with its own tau
 *   NOISE  white noise, SPARK (a crackle: random hits, ever sparser), HELD (sample-held noise) or METAL (six
 *          squares, the classic analog cluster, with a little noise), through a filter (the SVF's LP / BP / HP, a
 *          one-pole LP / HP, or two one-pole HPs) whose cutoff may sweep from f1 to f0
 * its level held for HOLD, then decaying with TAU (or a cubic rise over HOLD to the cut: a reverse swell). TEETH
 * strike the marked layers again (claps, drags). Every envelope runs per block (CTL samples), ramped linearly
 * inside it: the sample loop has no exponentials, no divisions and only 32-bit products.
 *
 * The part's bus (kv_post, after the voices): a room (four delays feeding each other through a Hadamard mix, at
 * half rate in 16 bits, dark), squashed by a fast compressor and mixed back; the bus driven and crushed in
 * parallel; then the bus compressor. GATE: the kick and the snare ring through their own short comb rooms, cut
 * dead (the gated sound), and the whole part goes through a 12-bit sampler at 26 kHz.
 *
 * The designs are the prototypes the kits were chosen from by ear, in fixed point; tests/krst_kits_test.c holds
 * each hit against them. */

/* ----------------------------------------------------------------------------------------- the design --- */
#define KV_NL 5                                          /* layers per lane */
enum { KL_NONE, KL_TONE, KL_NOISE, KL_SPARK, KL_HELD, KL_METAL };
enum { KF_NONE, KF_LP, KF_BP, KF_HP, KF_LP1, KF_HP1, KF_HP2 };
#define KV_RISE 0xFFFFu                                  /* tau: a cubic rise over HOLD, held to the cut */
typedef struct {
    uint8_t kind, filt, teeth, arg;                      /* arg: HELD the hold (samples), SPARK the density (%),
                                                          * METAL the squares' base (x 50 Hz; 0: 410 Hz) */
    uint16_t f0, f1;                                     /* TONE: end, start (Hz); filters: the cutoff's (0: f0) */
    uint16_t tsw;                                        /* the sweep's tau (0.1 ms); SPARK: the density's */
    uint16_t hold, tau;                                  /* the level: held (0.1 ms), then its tau (0.1 ms, 0: kept) */
    uint16_t q;                                          /* the SVF's Q x 100 */
    int16_t g;                                           /* gain, Q8 */
} kv_layer_t;
typedef struct {
    kv_layer_t l[KV_NL];
    uint16_t len;                                        /* the cut (0.1 ms) */
    uint16_t drive;                                      /* x 1/16, 0 = none */
    uint8_t bits, hold;                                  /* the crush (0 = none), a fixed hold (samples, 0/1 = none) */
    uint16_t rot_lo, rot_span;                           /* ROT: the rate falls to lo / 1000 over span (0.1 ms) */
    uint8_t nteeth, first;                               /* teeth, the earlier ones' level (%) */
    uint16_t gap;                                        /* their spacing (0.1 ms) */
    uint8_t envafter, comb;                              /* layer 0's level after the crush (hats); GATE: 1 the
                                                          * kick's comb room, 2 the snare's */
    uint16_t gain;                                       /* out, Q8 (sets the peak) */
    uint8_t send;                                        /* to the part's room (%) */
} kv_lane_def_t;

/* ------------------------------------------------------------------------------------ the state --- */
typedef struct {                                         /* a layer, set up */
    uint32_t inc0, inc1;                                 /* TONE: the end and start increments; filters: cut Hz */
    uint32_t kbs, kba;                                   /* the sweep's and the level's decay per block (Q16) */
    uint32_t rise;                                       /* a rise's length (samples) */
    uint32_t thr;                                        /* SPARK: the density at full (Q31 of a sample) */
    uint16_t hold;                                       /* blocks */
    int16_t g;
    int32_t kq;                                          /* the SVF damping (Q12) */
    int32_t a1;                                          /* a one-pole's coefficient (Q15) */
} kv_lc_t;
typedef struct {                                         /* a layer, running */
    uint32_t ph;
    int32_t es, ea;                                      /* the sweep's and the level's envelope (Q30) */
    int32_t s1, s2, z1, z2;                              /* filter states */
    int32_t sp;                                          /* SPARK's spark, HELD's held sample */
    uint16_t hc, cnt;                                    /* blocks held so far; HELD's countdown */
} kv_ls_t;
typedef struct {
    const kv_lane_def_t *d;
    kv_lc_t c[KV_NL];
    uint32_t rkb;                                        /* ROT: the rate's decay per block (0: none) */
    int32_t rlo;                                         /* its floor (Q30) */
    uint32_t len, gap, step;                             /* samples; the fixed hold's step (Q16) */
    uint32_t mi[6];                                      /* METAL (one layer a lane): the squares' increments */
} kv_coef_t;
typedef struct {
    kv_ls_t l[KV_NL];
    uint32_t n;                                          /* samples since the strike */
    uint8_t trig, live, tooth, spare;
    int32_t rng;
    int32_t rate;                                        /* ROT: the hold's rate (Q30 of the full) */
    uint32_t acc;                                        /* the hold's accumulator (Q16) */
    int32_t held;
    uint16_t cpos[4];                                    /* GATE: the comb lines' positions */
    uint32_t mph[6];                                     /* METAL's phases */
} kv_voice_t;

/* the part's bus: the room's four lines at half rate (16 bits), or GATE's kick and snare combs (full rate) */
#define KB_D0 515u
#define KB_D1 663u
#define KB_D2 846u
#define KB_D3 1055u
#define KB_ROOM (KB_D0 + KB_D1 + KB_D2 + KB_D3)
static const uint16_t KB_GK[3] = {431, 523, 607};       /* GATE: the kick's three combs (0.8 feedback) */
static const uint16_t KB_GS[4] = {557, 617, 691, 757};  /* the snare's four (0.86) */
#define KB_GKN (431u + 523u + 607u)
#define KB_GATE (KB_GKN + 557u + 617u + 691u + 757u)
#define KB_BUF (KB_GATE > KB_ROOM ? KB_GATE : KB_ROOM)
typedef struct {
    int32_t send[CTL];                                   /* the lanes' sends this block */
    int32_t lp[4];
    uint16_t w[4];                                       /* the room lines' write positions */
    int32_t ce, be, cg, bg;                              /* the room's and the bus's compressor envelopes, gains */
    int32_t hz, held, half, wet0, wet1;
    uint32_t sacc;                                       /* GATE's sampler */
    int32_t sh;
    uint8_t kit, dirty;
    int16_t buf[KB_BUF];
} kv_bus_t;

/* --------------------------------------------------------------------------------------- helpers --- */
/* log2(x) x 4096 for x >= 1 (setup only) */
static int32_t kv_log2(uint32_t x)
{
    int32_t e = 0;
    uint32_t m, f;
    if (!x)
        return 0;
    while (e < 31 && x >= (2u << e))
        e++;
    m = e >= 16 ? x >> (e - 16) : x << (16 - e);         /* 1.0 .. 2.0, Q16 */
    f = m - 65536u;
    return e * 4096 + (int32_t)((f * (5909u - ((1813u * f) >> 16))) >> 16);   /* log2(1 + f) ~ f (1.4425 - 0.4425 f) */
}
/* Hz -> the SVF's cutoff index (dsp.c: 0..127 << 8), through the pitch (p16 = 16 x MIDI note) */
static int32_t kv_cut(uint32_t hz)
{
    return dv_cut(1104 + (((kv_log2(hz < 20u ? 20u : hz) - kv_log2(440u)) * 192) >> 12));
}
static uint32_t kv_inc(uint32_t hz) { return hz * (uint32_t)((0x100000000ull + FS / 2u) / FS); }   /* (hz < 44 k) */
static uint32_t kv_smp(uint32_t t01) { return t01 * (FS / 100u) / 100u; }   /* 0.1 ms -> samples */
/* a one-pole's coefficient for a corner (Hz): 1 - e^(-2 pi f / fs), Q15 */
static int32_t kv_pole(uint32_t hz)
{
    uint32_t w = hz * 25736u / FS;                       /* 2 pi f / fs, Q12 (hz < 80 k) */
    uint32_t x = (w * 5909u) >> 12;                      /* x log2 e */
    if (x > 15u * 4096u)
        return 32767;
    return 32768 - (int32_t)(dv_exp2(15u * 4096u - x) >> 16);
}
static inline int32_t kv_dist(int32_t x, int32_t d16)    /* d16: the drive x 1/16; half tanh, half a hard clip */
{
    int32_t u = (clamp(x, -131072, 131072) * d16) >> 4;
    return (softclip(u) + clamp(u, -32767, 32767)) >> 1;
}

/* the coefficients of a lane, from its design and the knobs (TUNE 1/16 semitone; DECY TONE 64: as designed) */
static __attribute__((noinline)) void kv_setup(kv_coef_t *c, const kv_lane_def_t *d, int32_t tune16, int32_t decay, int32_t tone)
{
    uint32_t j, k;
    uint32_t tf = dv_exp2((uint32_t)(4 * 4096 + clamp(tune16, -700, 700) * 4096 / 192)) >> 4;   /* 2^(t / 12), Q16 */
    uint32_t dm = dv_exp2((uint32_t)(4 * 4096 + (clamp(decay, 0, 127) - 64) * 4096 / 64)) >> 4;   /* x0.5 .. x2 */
    uint32_t cm = dv_exp2((uint32_t)(4 * 4096 + (clamp(tone, 0, 127) - 64) * 4096 / 64)) >> 4;
    c->d = d;
    for (j = 0; j < KV_NL; j++) {
        const kv_layer_t *L = &d->l[j];
        kv_lc_t *C = &c->c[j];
        uint32_t tau = L->tau == KV_RISE ? 0u : (uint32_t)(((uint64_t)L->tau * 100u * dm) >> 16);   /* us */
        if (L->kind == KL_TONE) {
            C->inc0 = (uint32_t)(((uint64_t)kv_inc(L->f0) * tf) >> 16);
            C->inc1 = (uint32_t)(((uint64_t)kv_inc(L->f1 ? L->f1 : L->f0) * tf) >> 16);
        } else {
            C->inc0 = (uint32_t)(((uint64_t)L->f0 * cm) >> 16);
            C->inc1 = (uint32_t)(((uint64_t)(L->f1 ? L->f1 : L->f0) * cm) >> 16);
        }
        C->kbs = L->tsw ? dv_kb(L->tsw * 100u) : 0u;
        C->kba = L->tau == KV_RISE || !tau ? 65536u : dv_kb(tau);
        C->rise = L->tau == KV_RISE ? kv_smp(L->hold) : 0u;
        C->hold = (uint16_t)((kv_smp(L->hold) + CTL - 1u) / CTL);
        C->thr = ((1u << 30) / 100u) * L->arg;            /* P(spark) = density x the envelope */
        C->g = L->g;
        C->kq = (int32_t)(409600u / (L->q ? L->q : 70u));  /* 1 / Q, Q12 */
        C->a1 = L->filt >= KF_LP1 ? kv_pole(C->inc0) : 0;
        if (L->kind == KL_METAL)                         /* METAL: the classic cluster's ratios (DV_METAL_R row 0) on its base */
            for (k = 0; k < 6u; k++)
                c->mi[k] = (uint32_t)(((uint64_t)kv_inc((uint32_t)DV_METAL_R[0][k] * (L->arg ? L->arg * 50u : 410u) / 4096u) * tf) >> 16);
    }
    c->len = kv_smp(d->len);
    c->gap = kv_smp(d->gap);
    c->step = d->hold > 1u ? 65536u / d->hold : 65536u;
    if (d->rot_span) {                                   /* the rate falls to lo over span: tau = span / ln(1000 / lo) */
        uint32_t lnq = (uint32_t)(kv_log2(64000u / (d->rot_lo ? d->rot_lo : 1u)) - kv_log2(64u)) * 2839u >> 12;   /* Q12 */
        c->rkb = dv_kb(d->rot_span * 100u * 4096u / (lnq ? lnq : 1u));   /* (span under 1 s) */
        c->rlo = (int32_t)(((1u << 30) / 1000u) * d->rot_lo);
    } else {
        c->rkb = 0;
        c->rlo = 1 << 30;
    }
}

static int32_t kv_tooth_level(const kv_lane_def_t *d, uint32_t tooth)
{
    return d->nteeth > 1u && tooth + 1u < d->nteeth ? (int32_t)(((1u << 30) / 100u) * d->first) : 1 << 30;
}
static void kv_strike(kv_voice_t *v, const kv_coef_t *c)
{
    uint32_t j, k;
    for (j = 0; j < KV_NL; j++) {
        kv_ls_t *s = &v->l[j];
        s->s1 = s->s2 = s->z1 = s->z2 = s->sp = 0;
        s->ph = 0;
        s->es = 1 << 30;
        s->ea = c->d->l[j].tau == KV_RISE ? 0 : kv_tooth_level(c->d, 0);
        s->hc = s->cnt = 0;
    }
    for (k = 0; k < 6u; k++)
        v->mph[k] = 0;
    if (!v->rng)
        v->rng = 0x2545F491;
    v->n = 0;
    v->tooth = 0;
    v->rate = 1 << 30;
    v->acc = 65536u;
    v->held = 0;
    for (k = 0; k < 4u; k++)
        v->cpos[k] = 0;
    v->live = 1;
}

/* one block of a lane into y (Q15, near -7 dBFS at its peak); comb: GATE's lines for this lane (0: none) */
static __attribute__((noinline)) void kv_run(const kv_coef_t *c, kv_voice_t *v, int32_t *y, uint32_t n, int16_t *comb)
{
    const kv_lane_def_t *d = c->d;
    int32_t acc[CTL], lv0[CTL];
    uint32_t i, j;
    if (v->trig) {
        kv_strike(v, c);
        v->trig = 0;
    }
    if (!v->live || n > CTL) {
        for (i = 0; i < n; i++)
            y[i] = 0;
        return;
    }
    for (i = 0; i < n; i++)
        acc[i] = 0, lv0[i] = 32767;
    if (d->nteeth > 1u && v->tooth + 1u < d->nteeth && v->n >= (v->tooth + 1u) * c->gap) {   /* a tooth */
        v->tooth++;
        for (j = 0; j < KV_NL; j++)
            if (d->l[j].teeth) {
                v->l[j].ea = kv_tooth_level(d, v->tooth);
                v->l[j].hc = 0;
            }
    }
    for (j = 0; j < KV_NL; j++) {
        const kv_layer_t *L = &d->l[j];
        const kv_lc_t *C = &c->c[j];
        kv_ls_t *s = &v->l[j];
        int32_t a0, a1, da, a, es0;
        if (L->kind == KL_NONE)
            continue;
        if (C->rise) {                                   /* the cubic rise, held at full after it */
            uint32_t r0 = v->n >= C->rise ? 65536u : (v->n << 15) / (C->rise >> 1);   /* (n < 2^16 here) */
            uint32_t r1 = v->n + n >= C->rise ? 65536u : ((v->n + n) << 15) / (C->rise >> 1);
            a0 = (int32_t)((((r0 * r0) >> 16) * r0) >> 17);
            a1 = (int32_t)((((r1 * r1) >> 16) * r1) >> 17);
        } else {
            a0 = s->ea >> 15;
            if (s->hc < C->hold)
                s->hc++;
            else
                s->ea = (int32_t)mulq16(s->ea, C->kba);
            a1 = s->ea >> 15;
        }
        es0 = s->es;
        s->es = C->kbs ? (int32_t)mulq16(s->es, C->kbs) : 0;
        if (!C->rise && a0 < 3 && a1 < 3 && !(j == 0 && d->envafter))
            continue;                                    /* (died away: -80 dB; a tooth brings it back) */
        da = (a1 - a0) / (int32_t)CTL;
        a = a0;
        if (L->kind == KL_TONE) {                        /* the swept sine */
            uint32_t span = C->inc1 > C->inc0 ? C->inc1 - C->inc0 : 0u, ph = s->ph;
            uint32_t i0 = C->inc0 + (uint32_t)(((uint64_t)span * (uint32_t)(es0 >> 14)) >> 16);
            uint32_t i1 = C->inc0 + (uint32_t)(((uint64_t)span * (uint32_t)(s->es >> 14)) >> 16);
            int32_t di = ((int32_t)(i1 >> 5) - (int32_t)(i0 >> 5)) / (int32_t)CTL;   /* (in 1/32: no overflow) */
            uint32_t inc = i0;
            for (i = 0; i < n; i++, a += da, inc += (uint32_t)(di << 5)) {
                ph += inc;
                acc[i] += (((sine_i(ph) * a) >> 15) * C->g) >> 8;
            }
            s->ph = ph;
        } else {                                         /* the noises: the source, the filter, the level, each a loop */
            tsvf_t f = {0, 0, 0};
            int32_t w[CTL], kq = C->kq, ap = C->a1, s1 = s->s1, s2 = s->s2, z1 = s->z1, z2 = s->z2, sp = s->sp;
            uint32_t hz = C->inc0, cnt = s->cnt, hn = L->arg ? L->arg : 1u, g = (uint32_t)C->g;
            if (C->inc1 != C->inc0)                      /* the cutoff's sweep, once a block */
                hz = (uint32_t)((int32_t)C->inc0 + (int32_t)(((int64_t)((int32_t)C->inc1 - (int32_t)C->inc0) * es0) >> 30));
            switch (L->kind) {
            case KL_SPARK: {                             /* the density falls with the sweep's envelope */
                int32_t thr = (int32_t)(((uint64_t)C->thr * (uint32_t)(es0 >> 14)) >> 16);
                for (i = 0; i < n; i++) {
                    if ((int32_t)(noise32(&v->rng) >> 1) < thr)
                        sp = (noise32(&v->rng) & 1u) ? 32767 : -32767;
                    else
                        sp = (sp * 26870) >> 15;         /* x 0.82 */
                    w[i] = sp;
                }
                break;
            }
            case KL_HELD:
                for (i = 0; i < n; i++) {
                    if (!cnt) {
                        cnt = hn;
                        sp = DV_NOISE(v);
                    }
                    cnt--;
                    w[i] = sp;
                }
                break;
            case KL_METAL:
                for (i = 0; i < n; i++) {
                    int32_t q = 0;
                    uint32_t k;
                    for (k = 0; k < 6u; k++) {
                        v->mph[k] += c->mi[k];
                        q += (int32_t)(v->mph[k] >> 31);
                    }
                    w[i] = (2 * q - 6) * 5461 + ((DV_NOISE(v) * 9830) >> 15);   /* the squares' mean + 0.3 noise */
                }
                break;
            default:
                for (i = 0; i < n; i++)
                    w[i] = DV_NOISE(v);
            }
            switch (L->filt) {
            case KF_LP:
            case KF_BP:
            case KF_HP: {
                int mode = L->filt;
                tsvf_coef_k(&f, kv_cut(hz), kq);
                for (i = 0; i < n; i++) {
                    int32_t x = w[i], v3 = x - s2;
                    int32_t v1 = (f.a1 * s1 + f.a2 * v3) >> 13;
                    int32_t v2 = s2 + ((f.a2 * s1 + f.a3 * v3) >> 13);
                    s1 = clamp(2 * v1 - s1, -150000, 150000);
                    s2 = clamp(2 * v2 - s2, -150000, 150000);
                    w[i] = mode == KF_LP ? v2 : mode == KF_BP ? (v1 * kq) >> 12 : x - ((v1 * kq) >> 12) - v2;
                }
                break;
            }
            case KF_LP1:
                for (i = 0; i < n; i++) {
                    z1 += ((w[i] - z1) * ap) >> 15;
                    w[i] = z1;
                }
                break;
            case KF_HP1:                                 /* (the output before the state moves, as designed) */
                for (i = 0; i < n; i++) {
                    int32_t x = w[i];
                    w[i] = x - z1;
                    z1 += ((x - z1) * ap) >> 15;
                }
                break;
            case KF_HP2:
                for (i = 0; i < n; i++) {
                    int32_t x = w[i], y = x - z1;
                    z1 += ((x - z1) * ap) >> 15;
                    w[i] = y - z2;
                    z2 += ((y - z2) * ap) >> 15;
                }
                break;
            default:
                break;
            }
            if (j == 0 && d->envafter)                   /* (hats: layer 0's level after the crush) */
                for (i = 0; i < n; i++, a += da) {
                    lv0[i] = a;
                    acc[i] += (clamp(w[i], -65535, 65535) * (int32_t)g) >> 8;
                }
            else
                for (i = 0; i < n; i++, a += da)
                    acc[i] += (((clamp(w[i], -65535, 65535) * a) >> 15) * (int32_t)g) >> 8;
            s->s1 = s1, s->s2 = s2, s->z1 = z1, s->z2 = z2, s->sp = sp;
            s->cnt = (uint16_t)cnt;
        }
    }
    {   /* the post: drive, hold (fixed or ROT's falling rate), crush, layer 0's level (hats), GATE's comb, the cut */
        int32_t r0 = v->rate, r1 = r0, rq, dr, held = v->held, sh = d->bits ? 16 - d->bits : 0;
        uint32_t ac = v->acc, step = c->step, fade0 = FS * 4u / 1000u;
        int hold = c->rkb || step != 65536u;
        if (c->rkb) {
            r1 = (int32_t)mulq16(r0, c->rkb);
            if (r1 < c->rlo)
                r1 = c->rlo;
            v->rate = r1;
        }
        rq = r0 >> 14;                                   /* Q16 */
        dr = ((r1 >> 14) - rq) / (int32_t)CTL;
        for (i = 0; i < n; i++, rq += dr) {
            int32_t x = acc[i];
            uint32_t k = v->n + i;
            if (d->drive)
                x = kv_dist(x, d->drive);
            if (hold) {
                ac += c->rkb ? (uint32_t)rq : step;
                if (ac >= 65536u) {
                    ac -= 65536u;
                    held = sh ? ((x + (1 << (sh - 1))) >> sh) << sh : x;
                }
                x = held;
            } else if (sh) {
                x = ((x + (1 << (sh - 1))) >> sh) << sh;
            }
            if (d->envafter)
                x = (x * lv0[i]) >> 15;
            if (comb) {                                  /* GATE: the lane's own short room, then the gate */
                const uint16_t *D = d->comb == 1 ? KB_GK : KB_GS;
                uint32_t nl = d->comb == 1 ? 3u : 4u;
                int32_t ws = 0, fb = d->comb == 1 ? 26214 : 28180;   /* 0.8, 0.86 */
                int16_t *b = comb;
                for (uint32_t q = 0; q < nl; q++) {
                    uint32_t pos = v->cpos[q];
                    int32_t yd = b[pos];
                    b[pos] = (int16_t)clamp(x + ((yd * fb) >> 15), -32767, 32767);
                    ws += yd;
                    if (++pos >= D[q])
                        pos = 0;
                    v->cpos[q] = (uint16_t)pos;
                    b += D[q];
                }
                x = d->comb == 1 ? kv_dist(x + ((ws * 230) >> 8), 24) : kv_dist(x + ((ws * 282) >> 8), 21);   /* x0.9 x1.5, x1.1 x1.3 */
            }
            x = clamp(x, -131071, 131071);
            if (k + fade0 >= c->len)                     /* the cut: a 4 ms fade to the length */
                x = k >= c->len ? 0 : ((x >> 2) * (int32_t)(((c->len - k) * 372u) >> 1)) >> 13;   /* (372: 2^16 / 176) */
            y[i] = (x * (int32_t)d->gain) >> 8;
        }
        v->acc = ac;
        v->held = held;
    }
    v->n += n;
    if (v->n >= c->len)
        v->live = 0;
}

/* ------------------------------------------------------------------------------------ the kits --- */
/* times in 0.1 ms, gains Q8 (256 = 1), the drive x 1/16. Per kit: KICK SNARE CLAP HAT CL HAT OP TOM RIM BELL
 * (the last lane: the kit's extra: ROOM a crash, GATE a gated noise, MARCH a low war drum, ARC a burst of the arc,
 * ROT a reverse swell, SLAB a noise crash) */
#define T_(f0, f1, tsw, hold, tau, g) {KL_TONE, KF_NONE, 0, 0, f0, f1, tsw, hold, tau, 0, g}
#define TT(f0, f1, tsw, hold, tau, g) {KL_TONE, KF_NONE, 1, 0, f0, f1, tsw, hold, tau, 0, g}
#define N_(flt, f0, f1, tsw, q, hold, tau, g) {KL_NOISE, flt, 0, 0, f0, f1, tsw, hold, tau, q, g}
#define NT(flt, f0, f1, tsw, q, hold, tau, g) {KL_NOISE, flt, 1, 0, f0, f1, tsw, hold, tau, q, g}
#define SP(f0, dens, dtau, g) {KL_SPARK, KF_HP1, 0, dens, f0, 0, dtau, 0, 0, 0, g}
#define HE(n, f0, tau, g) {KL_HELD, KF_HP1, 0, n, f0, 0, 0, 0, tau, 0, g}
#define ME(base, f0, tau, g) {KL_METAL, KF_HP2, 0, base, f0, 0, 0, 0, tau, 0, g}
#define NO {KL_NONE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
/*                 len   drv bits hold rot_lo span teeth first gap envafter comb gain send */
#define P_(len, drv, bits, hold, lo, span, nt, first, gap, ea, comb, gain, send) len, drv, bits, hold, lo, span, nt, first, gap, ea, comb, gain, send

enum { KK_ROOM, KK_GATE, KK_MARCH, KK_ARC, KK_ROT, KK_SLAB, KK_COUNT };
static const kv_lane_def_t KV_KIT[KK_COUNT][DV_NLANE] = {
    {   /* ROOM: acoustic drums from modes in a big squashed room (the Downward Spiral's way) */
        {{T_(62, 105, 120, 0, 1100, 256), T_(98, 167, 120, 0, 500, 128), T_(151, 257, 120, 0, 250, 64),
          N_(KF_LP1, 400, 0, 0, 0, 0, 200, 614), N_(KF_HP1, 2500, 0, 0, 0, 0, 20, 179)},
         P_(3000, 35, 0, 0, 0, 0, 0, 0, 0, 0, 0, 117, 25)},
        {{T_(182, 209, 100, 0, 700, 230), T_(331, 381, 100, 0, 450, 138), N_(KF_BP, 4200, 0, 0, 50, 0, 1600, 333),
          N_(KF_BP, 7500, 0, 0, 70, 0, 900, 166), N_(KF_HP1, 3000, 0, 0, 0, 0, 15, 205)},
         P_(4000, 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 90)},
        {{NT(KF_BP, 1100, 0, 0, 130, 0, 40, 640), N_(KF_BP, 1100, 0, 0, 130, 0, 750, 320), NO, NO, NO},
         P_(2500, 32, 0, 0, 0, 0, 4, 100, 100, 0, 0, 65, 50)},
        {{ME(62, 6000, 180, 256), N_(KF_HP2, 6000, 0, 0, 0, 0, 180, 154), NO, NO, NO},
         P_(800, 26, 0, 0, 0, 0, 0, 0, 0, 0, 0, 33, 15)},
        {{ME(62, 6000, 2200, 256), N_(KF_HP2, 6000, 0, 0, 0, 0, 2200, 154), NO, NO, NO},
         P_(6000, 26, 0, 0, 0, 0, 0, 0, 0, 0, 0, 35, 20)},
        {{T_(118, 147, 300, 0, 3000, 256), T_(186, 233, 300, 0, 1200, 102), N_(KF_HP1, 2000, 0, 0, 0, 0, 30, 102), NO, NO},
         P_(7000, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 82, 70)},
        {{T_(1700, 0, 0, 0, 120, 256), T_(620, 0, 0, 0, 120, 205), N_(KF_NONE, 0, 0, 0, 0, 0, 10, 128), NO, NO},
         P_(700, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 47, 30)},
        {{ME(50, 3000, 6000, 256), N_(KF_HP2, 3000, 0, 0, 0, 0, 6000, 154), NO, NO, NO},
         P_(14000, 26, 0, 0, 0, 0, 0, 0, 0, 0, 0, 41, 30)},
    },
    {   /* GATE: the 80s gated snare, half time; the kick gated too; the part through a 12-bit sampler */
        {{T_(41, 650, 120, 350, 600, 256), N_(KF_BP, 2500, 0, 0, 70, 0, 600, 128), N_(KF_HP1, 3000, 0, 0, 0, 0, 15, 205), NO, NO},
         P_(2040, 80, 0, 0, 0, 0, 0, 0, 0, 0, 1, 117, 0)},
        {{T_(190, 247, 60, 60, 300, 256), N_(KF_BP, 2600, 0, 0, 60, 0, 450, 512), N_(KF_HP1, 5000, 0, 0, 0, 0, 200, 154), NO, NO},
         P_(2640, 48, 0, 0, 0, 0, 0, 0, 0, 0, 2, 94, 0)},
        {{NT(KF_BP, 1200, 0, 0, 130, 0, 40, 640), N_(KF_BP, 1200, 0, 0, 130, 0, 600, 320), NO, NO, NO},
         P_(2000, 48, 0, 0, 0, 0, 4, 100, 90, 0, 0, 65, 10)},
        {{ME(0, 7000, 90, 256), NO, NO, NO, NO}, P_(400, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 26, 0)},
        {{ME(0, 7000, 1100, 256), NO, NO, NO, NO}, P_(3000, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 35, 0)},
        {{T_(120, 600, 120, 100, 800, 256), N_(KF_BP, 2500, 0, 0, 70, 0, 600, 77), N_(KF_HP1, 3000, 0, 0, 0, 0, 15, 205), NO, NO},
         P_(3000, 48, 0, 0, 0, 0, 0, 0, 0, 0, 0, 70, 5)},
        {{N_(KF_HP1, 2000, 0, 0, 0, 0, 40, 768), NO, NO, NO, NO}, P_(400, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 41, 0)},
        {{N_(KF_HP1, 3000, 0, 0, 0, 0, 2000, 256), NO, NO, NO, NO}, P_(2600, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 47, 0)},
    },
    {   /* MARCH: only impacts, nothing rings: the kick with a beater click, a dry crack, boots, war drums, a drag */
        {{T_(52, 1300, 90, 200, 400, 256), T_(3800, 0, 0, 0, 20, 282), N_(KF_HP1, 4000, 0, 0, 0, 0, 15, 179), N_(KF_BP, 1800, 0, 0, 70, 0, 300, 128), NO},
         P_(1700, 112, 0, 0, 0, 0, 0, 0, 0, 0, 0, 117, 0)},
        {{T_(200, 400, 40, 60, 200, 205), N_(KF_BP, 2600, 0, 0, 60, 300, 200, 410), N_(KF_HP1, 3000, 0, 0, 0, 0, 20, 307), NO, NO},
         P_(1400, 96, 0, 0, 0, 0, 0, 0, 0, 0, 0, 94, 15)},
        {{NT(KF_LP1, 700, 0, 0, 0, 0, 80, 640), T_(110, 300, 50, 50, 200, 154), NO, NO, NO},
         P_(1200, 80, 0, 0, 0, 0, 2, 100, 120, 0, 0, 65, 5)},
        {{N_(KF_HP1, 7000, 0, 0, 0, 0, 50, 512), NO, NO, NO, NO}, P_(300, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 26, 0)},
        {{N_(KF_HP1, 6000, 0, 0, 0, 0, 300, 400), NO, NO, NO, NO}, P_(1200, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 35, 0)},
        {{T_(105, 630, 100, 250, 350, 256), N_(KF_LP1, 900, 0, 0, 0, 0, 200, 614), NO, NO, NO},
         P_(1500, 96, 0, 0, 0, 0, 0, 0, 0, 0, 0, 70, 10)},
        {{TT(262, 0, 0, 0, 400, 179), TT(455, 0, 0, 0, 250, 102), NT(KF_HP1, 3500, 0, 0, 0, 0, 12, 205), N_(KF_BP, 5200, 0, 0, 50, 560, 800, 500), NO},
         P_(1200, 83, 0, 0, 0, 0, 3, 50, 280, 0, 0, 41, 10)},
        {{T_(70, 420, 100, 250, 350, 256), N_(KF_LP1, 900, 0, 0, 0, 0, 200, 614), NO, NO, NO},
         P_(1500, 96, 0, 0, 0, 0, 0, 0, 0, 0, 0, 47, 10)},
    },
    {   /* ARC: electricity: the kick with an arc of sparks, the arc through the snare, a hiss of current */
        {{T_(50, 1100, 120, 180, 500, 256), SP(1500, 25, 500, 307), NO, NO, NO},
         P_(2200, 80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 117, 5)},
        {{T_(195, 273, 80, 80, 350, 256), N_(KF_BP, 2600, 0, 0, 60, 0, 500, 358), N_(KF_HP1, 6000, 0, 0, 0, 0, 300, 128), SP(1200, 35, 600, 230), NO},
         P_(1800, 96, 0, 0, 0, 0, 0, 0, 0, 0, 0, 94, 25)},
        {{NT(KF_NONE, 0, 0, 0, 0, 0, 40, 768), N_(KF_NONE, 0, 0, 0, 0, 0, 500, 307), NO, NO, NO},
         P_(1800, 48, 5, 0, 0, 0, 4, 100, 90, 0, 0, 63, 20)},
        {{N_(KF_BP, 10000, 0, 0, 250, 0, 100, 256), NO, NO, NO, NO}, P_(400, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 64, 0)},
        {{N_(KF_BP, 10000, 0, 0, 250, 0, 800, 256), NO, NO, NO, NO}, P_(2500, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 54, 5)},
        {{T_(110, 500, 120, 200, 700, 256), N_(KF_BP, 2500, 0, 0, 70, 0, 600, 77), N_(KF_HP1, 3000, 0, 0, 0, 0, 15, 205), NO, NO},
         P_(2200, 64, 7, 2, 0, 0, 0, 0, 0, 0, 0, 70, 20)},
        {{HE(5, 6500, 40, 256), NO, NO, NO, NO}, P_(300, 48, 4, 0, 0, 0, 0, 0, 0, 1, 0, 41, 5)},
        {{SP(1500, 40, 800, 300), NO, NO, NO, NO}, P_(1500, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 47, 10)},
    },
    {   /* ROT: digital decay: the sample rate falls through every hit */
        {{T_(48, 1200, 120, 200, 500, 256), N_(KF_BP, 2500, 0, 0, 70, 0, 500, 154), NO, NO, NO},
         P_(2000, 80, 6, 0, 40, 1600, 0, 0, 0, 0, 0, 119, 0)},
        {{T_(190, 266, 80, 80, 350, 256), N_(KF_BP, 2200, 0, 0, 60, 0, 800, 358), N_(KF_HP1, 6000, 0, 0, 0, 0, 480, 128), NO, NO},
         P_(2200, 96, 6, 0, 80, 2000, 0, 0, 0, 0, 0, 95, 0)},
        {{NT(KF_BP, 1200, 0, 0, 130, 0, 40, 640), N_(KF_BP, 1200, 0, 0, 130, 0, 540, 320), NO, NO, NO},
         P_(1800, 80, 6, 0, 100, 1500, 4, 100, 90, 0, 0, 65, 0)},
        {{HE(3, 6500, 120, 256), NO, NO, NO, NO}, P_(450, 48, 4, 0, 0, 0, 0, 0, 0, 1, 0, 25, 0)},
        {{HE(3, 6500, 800, 256), NO, NO, NO, NO}, P_(2500, 48, 4, 0, 0, 0, 0, 0, 0, 1, 0, 35, 0)},
        {{T_(70, 260, 300, 100, 700, 256), N_(KF_NONE, 0, 0, 0, 0, 0, 40, 77), NO, NO, NO},
         P_(2500, 80, 6, 0, 50, 2000, 0, 0, 0, 0, 0, 71, 0)},
        {{HE(7, 6500, 40, 256), NO, NO, NO, NO}, P_(300, 48, 3, 0, 0, 0, 0, 0, 0, 1, 0, 42, 0)},
        {{N_(KF_BP, 7500, 1500, 1200, 80, 3600, KV_RISE, 256), NO, NO, NO, NO}, P_(3640, 48, 0, 0, 0, 0, 0, 0, 0, 0, 0, 47, 0)},
    },
    {   /* SLAB: power electronics: everything from noise, a real punch under the kick */
        {{N_(KF_LP, 60, 5060, 120, 180, 200, 450, 2304), T_(50, 900, 100, 150, 400, 230), N_(KF_BP, 3000, 0, 0, 70, 0, 300, 128), NO, NO},
         P_(1800, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 117, 10)},
        {{N_(KF_BP, 700, 3700, 200, 120, 300, 500, 1024), T_(200, 0, 0, 60, 300, 205), NO, NO, NO},
         P_(2000, 80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 94, 50)},
        {{NT(KF_NONE, 0, 0, 0, 0, 0, 40, 768), NO, NO, NO, NO}, P_(2000, 48, 4, 0, 0, 0, 5, 100, 100, 0, 0, 63, 40)},
        {{HE(2, 6500, 100, 256), NO, NO, NO, NO}, P_(400, 48, 3, 0, 0, 0, 0, 0, 0, 1, 0, 25, 0)},
        {{HE(2, 6500, 1000, 256), NO, NO, NO, NO}, P_(3000, 48, 3, 0, 0, 0, 0, 0, 0, 1, 0, 35, 5)},
        {{N_(KF_BP, 150, 750, 400, 300, 0, 1000, 1536), NO, NO, NO, NO}, P_(3000, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 70, 40)},
        {{HE(9, 6500, 30, 256), NO, NO, NO, NO}, P_(200, 48, 2, 0, 0, 0, 0, 0, 0, 1, 0, 43, 0)},
        {{N_(KF_BP, 1000, 6000, 1500, 70, 0, 1500, 512), NO, NO, NO, NO}, P_(4000, 48, 5, 0, 0, 0, 0, 0, 0, 0, 0, 46, 20)},
    },
};
/* the buses: room, its feedback and level back (%), the parallel crush's share and the dry's (%), its bits, the
 * parallel drive (x 1/16), GATE's sampler */
typedef struct { uint8_t room, fb, wet, crush, dry, bits, drive, sampler; } kv_bus_t2;
static const kv_bus_t2 KV_BUS[KK_COUNT] = {
    {1, 72, 70, 45, 65, 8, 40, 0},                       /* ROOM: the big squashed room */
    {2, 0, 0, 15, 91, 12, 32, 1},                        /* GATE: the combs are in the lanes; the sampler */
    {1, 40, 15, 20, 88, 10, 40, 0},                      /* MARCH: dry, impacts */
    {1, 45, 20, 30, 82, 8, 56, 0},                       /* ARC: nearly dry */
    {0, 0, 0, 0, 100, 12, 19, 0},                        /* ROT: dry */
    {1, 55, 40, 30, 82, 6, 32, 0},                       /* SLAB */
};
#undef T_
#undef TT
#undef N_
#undef NT
#undef SP
#undef HE
#undef ME
#undef NO
#undef P_

/* -------------------------------------------------------------------------------- the part's bus --- */
/* the levels: a lane at its design peaks near 14700 (Q15, -7 dBFS); in the part's sum (drum_render: x 1.25, the
 * voice level, x 2) that is about 27000: the prototypes' 1.0. The room's compressor holds it to 0.05 of that, the
 * bus's to 0.6 */
#define KV_UNIT 27000
#define KV_ROOM_THR (KV_UNIT / 20)
#define KV_BUS_THR (KV_UNIT * 3 / 5)
/* a compressor at its limit: the envelope every sample (attack, release: Q15 factors), the gain (thr / envelope,
 * Q15) every 8, one 32-bit division */
static inline int32_t kv_smash(int32_t x, int32_t *e, int32_t *g, uint32_t i, int32_t thr, int32_t att, int32_t rel)
{
    int32_t a = clamp(x < 0 ? -x : x, 0, 1 << 20);
    *e = a > *e ? *e + (((a - *e) * att) >> 15) : (int32_t)(((uint32_t)*e >> 8) * (uint32_t)rel >> 7);
    if (!(i & 7u) || a > *e)
        *g = *e > thr ? (int32_t)(((uint32_t)thr << 15) / (uint32_t)*e) : 32767;
    return ((clamp(x, -262143, 262143) >> 2) * *g) >> 13;
}
static __attribute__((noinline)) void kv_bus_clear(kv_bus_t *b, uint32_t kit)
{
    uint32_t i;
    for (i = 0; i < KB_BUF; i++)
        b->buf[i] = 0;
    for (i = 0; i < 4u; i++)
        b->lp[i] = 0, b->w[i] = 0;
    for (i = 0; i < CTL; i++)
        b->send[i] = 0;
    b->cg = b->bg = 32767;
    b->ce = b->be = b->hz = b->held = b->half = b->wet0 = b->wet1 = 0;
    b->sacc = 0;
    b->sh = 0;
    b->kit = (uint8_t)kit;
    b->dirty = 0;
}
/* the room, one tick at half rate: in, out at the part's level */
static __attribute__((noinline)) int32_t kv_room(kv_bus_t *b, int32_t in, int32_t fb)
{
    static const uint16_t D[4] = {KB_D0, KB_D1, KB_D2, KB_D3};
    int16_t *line = b->buf;
    int32_t o[4], y = 0, h0, h1, h2, h3, m[4];
    uint32_t j;
    for (j = 0; j < 4u; j++) {                           /* the oldest of each line, through its low-pass */
        int32_t r = line[b->w[j]];
        b->lp[j] += ((r - b->lp[j]) * 22938) >> 15;      /* (0.7: 0.45 a sample at 44.1 kHz) */
        o[j] = b->lp[j];
        y += o[j];
        line += D[j];
    }
    h0 = o[0] + o[1], h1 = o[0] - o[1], h2 = o[2] + o[3], h3 = o[2] - o[3];
    m[0] = h0 + h2, m[1] = h1 + h3, m[2] = h0 - h2, m[3] = h1 - h3;
    for (j = 0, line = b->buf; j < 4u; j++) {            /* (the Hadamard mix x 1/2: orthonormal) x the feedback */
        line[b->w[j]] = (int16_t)clamp((in >> 2) + ((m[j] * fb) >> 8), -32767, 32767);
        if (++b->w[j] >= D[j])
            b->w[j] = 0;
        line += D[j];
    }
    return y << 1;                                       /* (0.5 x the sum, x 4: the line holds a quarter) */
}
/* the part's bus over its voices' sum (out); 1 while the room still rings */
static __attribute__((noinline)) uint32_t kv_post(kv_bus_t *b, uint32_t kit, int32_t *out, uint32_t n, int32_t drv)
{
    const kv_bus_t2 *B = &KV_BUS[kit];
    uint32_t i, any = 0;
    int32_t fb = B->fb * 128 / 100, wet = B->wet * 256 / 100, dry = B->dry * 256 / 100, cr = B->crush * 256 / 100;
    int32_t d16 = B->drive + drv * 3 / 16, sh = 16 - B->bits, a60 = 280;   /* (a one-pole at 60 Hz, Q15) */
    if (b->kit != kit)
        kv_bus_clear(b, kit);
    for (i = 0; i < n; i++)
        any |= (uint32_t)(out[i] != 0) | (uint32_t)(b->send[i] != 0);
    if (!any && !b->dirty)
        return 0;
    for (i = 0; i < n; i++) {
        int32_t x = out[i], g;
        if (B->room == 1) {                              /* the room at half rate, its output ramped between ticks */
            if (!(i & 1u)) {
                b->wet0 = b->wet1;
                b->wet1 = kv_smash(kv_room(b, (b->send[i] + b->send[i + 1u]) >> 1, fb), &b->ce, &b->cg, i, KV_ROOM_THR, 16384, 32735) * 3;
            }
            x += ((i & 1u ? b->wet1 : (b->wet0 + b->wet1) >> 1) * wet) >> 8;
        }
        b->hz += ((x - b->hz) * a60) >> 15;              /* the parallel drive and crush, from 60 Hz up */
        g = kv_dist((x - b->hz) + ((x - b->hz) >> 2), d16);
        if (!(++b->half & 1))
            b->held = ((g + (1 << (sh - 1))) >> sh) << sh;
        x = ((x * dry) >> 8) + ((((b->held * 27) >> 5) * cr) >> 8);   /* (x 0.84: back to the part's level) */
        x = kv_smash(x, &b->be, &b->bg, i, KV_BUS_THR, 9830, 32752);   /* the bus compressor */
        if (B->sampler) {                                /* GATE: 12 bits at 26 kHz */
            if ((b->sacc += 38700u) >= 65536u) {
                b->sacc -= 65536u;
                b->sh = ((x + 8) >> 4) << 4;
            }
            x = b->sh;
        }
        out[i] = x;
        b->send[i] = 0;
    }
    b->dirty = (uint8_t)(B->room == 1 && (b->ce > 8 || b->wet1));
    return b->dirty;
}
