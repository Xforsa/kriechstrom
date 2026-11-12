/* SPDX-License-Identifier: GPL-3.0-only */
/* KRIECHSTROM: DAMAGE, the machine wearing out. Included by fx.c after the INSERTs (ins_t, pf_mix, softclip, noise32).
 *
 * One damage, two places: on the master (FX > DAMAGE: G_DMG, its CHAR G_DMGC; WALL pushes it up) and as a track's
 * INSERT DAMG (A AMOUNT, B CHAR, C LEVEL, MIX). AMOUNT 0 is off; CHAR moves it from TAPE (0) to DIGITAL (127):
 *   drive     a tanh after up to x16 (both); the master's loudness then kept where it was (krst_level): DAMAGE and
 *             WALL change the sound, not the level
 *   tape      a darkening low-pass (down to ~800 Hz), wow and flutter (a line read at a wobbling distance: up to
 *             ~3.6 ms at 0.6 Hz with a 6 Hz flutter on it), dropouts that sag (to -24 dB over ~20 .. 80 ms)
 *   digital   fewer bits (up to 11 gone), a held sample (1 .. 16: down to ~2.8 kHz), dropouts that cut (silence)
 * The dropouts are rolled once a block (32 samples): about one in 1.5 s at AMOUNT 64, 5 a second at 127.
 * The hiss is a knob of its own (HISS, G_HISS: hiss_master), WALL another thing (wall_master): what DAMAGE does stays
 * DAMAGE's. With DMG, WALL and HISS at 0 nothing runs: the mix is bit for bit as before. */

typedef struct {
    int32_t lp, hold, loss, nz;          /* the tape's low-pass, the held sample, the dropout's loss (Q15: 0 none, 32768
                                          * silence; 0 is the state from rest), the hiss' state */
    uint16_t dropt, cnt;                 /* blocks of dropout left, samples of the held one left */
    uint32_t ph;                         /* the wow's phase */
    uint16_t wp;                         /* the line's next write */
} dmg_t;

typedef struct {                         /* a block's values of one AMOUNT / CHAR */
    int32_t g, k, mk, tgt, slope, wow;        /* wow: its depth, samples Q8 */
    int32_t dq, ddq;                           /* the wow's distance back now and its step a sample, Q8 */
    int32_t gn, dgn;                           /* the make-up times the dropout's gain now, its step a sample (Q15) */
    uint32_t sh, hold;
} dmg_par_t;
#define DMG_WOW_INC 58435u                      /* 0.6 Hz at 44.1 kHz, of 2^32 */

static inline uint32_t dmg_noise(int32_t *st)
{
    if (!*st)
        *st = 0x2545F491;
    return noise32(st);
}

/* the block's values; a roll for a new dropout on d (st: the generator) */
static inline int32_t dmg_tri(uint32_t ph);
static void dmg_begin(dmg_par_t *q, dmg_t *d, int32_t a, int32_t c, int32_t *st, int32_t wmax, uint32_t len, uint32_t n)
{
    int32_t tape = 127 - c, at = a * tape / 127;
    q->g = 4096 + a * a * 4;                            /* Q12: 1x .. 16x (the level is kept after: krst_level) */
    q->k = 32767 - (a * tape * 230) / 127;              /* the tape's darkening, Q15: down to ~800 Hz */
    q->mk = 32768 - a * 160;                            /* make-up: the drive packs it denser (louder by RMS) */
    q->wow = (at * at * wmax) >> 6;                     /* (Q8: wmax samples at the top, gentle at first) */
    if (q->wow) {                                       /* the block's start and end: a line between (0.6 Hz is slow) */
        uint32_t pe = d->ph + DMG_WOW_INC * n;
        int32_t m0 = dmg_tri(d->ph) + (dmg_tri(d->ph * 10u) >> 3), m1 = dmg_tri(pe) + (dmg_tri(pe * 10u) >> 3);
        q->dq = ((int32_t)(len >> 1) << 8) + ((m0 * q->wow) >> 15);   /* (|m| < 36864, wow < 40323: fits) */
        q->ddq = (((m1 * q->wow) >> 15) - ((m0 * q->wow) >> 15)) / (int32_t)n;
        d->ph = pe;
    }
    q->sh = (uint32_t)clamp((a * c) >> 10, 0, 11);      /* bits gone: 0 .. 11 */
    q->hold = 1u + (uint32_t)((a * c) >> 10);           /* held samples: 1 .. 16 */
    if (!d->dropt) {
        uint32_t r = dmg_noise(st) & 0xFFFFFFu, thr = (uint32_t)(a * a * a) >> 6;
        if (r < thr && a > 24) {
            uint32_t len = dmg_noise(st) & 63u;
            d->dropt = (uint16_t)(c > 64 ? 5u + (len >> 1) : 24u + (len >> 1) + (len >> 2));
        }
        q->tgt = 32768;
    } else {
        d->dropt--;
        q->tgt = c > 64 ? 0 : 2048;                     /* (TAPE: down to a sixteenth, -24 dB) */
    }
    q->slope = c > 64 ? 2 : 7;                          /* (cut hard, sag slowly) */
    {   /* the dropout's gain over the block: a line to where it glides (DIGITAL: there at once) */
        int32_t d0 = 32768 - d->loss, d1 = c > 64 ? q->tgt : d0 + (((q->tgt - d0) * (int32_t)n) >> 7);
        d1 = clamp(d1, 0, 32768);
        q->gn = (q->mk * (d0 >> 1)) >> 14;
        q->dgn = (((q->mk * (d1 >> 1)) >> 14) - q->gn) / (int32_t)n;
        d->loss = 32768 - d1;
    }
}

/* the triangle of a phase, -32768 .. 32767 */
static inline int32_t dmg_tri(uint32_t ph) { int32_t v = (int32_t)(ph >> 15); return (v < 65536 ? v : 131071 - v) - 32768; }
/* one sample x (a voice's full scale 32768); line: the wow's (a power of 2, mask), its centre half of it */
static inline int32_t dmg_sample(dmg_par_t *q, dmg_t *d, int32_t x, int32_t *st, int16_t *line, uint32_t mask)
{
    int32_t u = softclip(((clamp(x, -262143, 262143) >> 2) * q->g) >> 10);
    d->lp += ((u - d->lp) * q->k) >> 15;              /* (|u - lp| < 2^16, k < 2^15: fits) */
    u = d->lp;
    if (q->wow) {                                     /* wow and flutter: the line read at a wobbling distance */
        int32_t dq = q->dq, a0, a1;
        uint32_t ri, fr;
        q->dq += q->ddq;
        line[d->wp & mask] = (int16_t)clamp(u, -32767, 32767);
        ri = d->wp - (uint32_t)(dq >> 8);
        fr = (uint32_t)dq & 255u;
        a0 = line[ri & mask];
        a1 = line[(ri - 1u) & mask];
        u = a0 + (((a1 - a0) * (int32_t)fr) >> 8);
        d->wp++;
    }
    if (q->hold > 1u) {
        if (!d->cnt) {
            d->hold = u;
            d->cnt = (uint16_t)q->hold;
        }
        d->cnt--;
        u = d->hold;
    }
    if (q->sh)
        u = ((u + (1 << (q->sh - 1u))) >> q->sh) << q->sh;
    u = (u * q->gn) >> 15;
    q->gn += q->dgn;
    (void)st;
    return u;
}

/* the INSERT DAMG (track_insert): A AMOUNT, B CHAR, C LEVEL, MIX as the others */
static __attribute__((noinline)) void ins_damage(ins_t *s, const track_t *t, int32_t *b, uint32_t n, int32_t tw)
{
    dmg_t d;
    dmg_par_t q;
    int32_t a = clamp(t->p[P_IA], 0, 127), c = clamp(t->p[P_IB], 0, 127), lvl = LEVEL_Q12[clamp(t->p[P_IC], 0, 127)];
    uint32_t i;
    d.ph = s->ph;
    d.wp = s->wp;
    d.lp = s->lp1;
    d.hold = s->hold;
    d.loss = s->x[0];
    d.nz = s->x[1];
    d.dropt = (uint16_t)s->x[2];
    d.cnt = s->cnt;
    dmg_begin(&q, &d, a, c, &d.nz, 100, IL_LEN, n);    /* (the INSERT's line: 256, its wow up to 100 samples) */
    for (i = 0; i < n; i++) {
        int32_t x = b[i], y;
        s->w += clamp(tw - s->w, -SL_SLOPE, SL_SLOPE);
        y = (dmg_sample(&q, &d, x, &d.nz, s->line, IL_LEN - 1u) * lvl) >> 12;
        s->dc += ((y << 8) - s->dc) >> 9;               /* DC blocker, ~14 Hz */
        y -= s->dc >> 8;
        b[i] = pf_mix(x, y, s->w);
    }
    s->lp1 = d.lp;
    s->hold = d.hold;
    s->x[0] = d.loss;
    s->x[1] = d.nz;
    s->x[2] = d.dropt;
    s->cnt = (uint8_t)d.cnt;
    s->ph = d.ph;
    s->wp = d.wp;
}

/* ---- the master: G_DMG, G_DMGC, and WALL (QUIET .. WALL, G_WALL) on top ---- */
static dmg_t dm_l, dm_r;
static int32_t dm_nz, dm_w;                             /* the shared generator (one dropout for both sides); the wet */
#define DM_LINE 512u
static int16_t dm_line[2][DM_LINE] __attribute__((section(".pool")));   /* the wow's lines (2 KB) */

static inline int32_t dmg_master_amount(void) { return clamp(song.g[G_DMG], 0, 127); }
static inline int dmg_master_on(void) { return dmg_master_amount() > 0 || dm_w; }

/* l, r: the mix (with the buses) before MASTER, in place */
static __attribute__((noinline)) void dmg_master(int32_t *l, int32_t *r, uint32_t n)
{
    dmg_par_t q;
    int32_t a = dmg_master_amount(), c = clamp(song.g[G_DMGC], 0, 127), tw = a ? 32768 : 0;
    uint32_t i, dt;
    if (!a && !dm_w) {
        memset(&dm_l, 0, sizeof dm_l);
        memset(&dm_r, 0, sizeof dm_r);
        return;
    }
    dmg_begin(&q, &dm_l, a, c, &dm_nz, 160, DM_LINE, n);   /* (wow up to 160 samples, 3.6 ms) */
    dt = dm_l.dropt;
    dm_r.dropt = (uint16_t)dt;                          /* (the right follows the left's dropout) */
    {
    dmg_par_t qr = q;                                   /* (the right: the same wow line, stepping on its own) */
    for (i = 0; i < n; i++) {
        int32_t xl = l[i], xr = r[i], yl, yr;
        dm_w += clamp(tw - dm_w, -SL_SLOPE, SL_SLOPE);
        yl = dmg_sample(&q, &dm_l, xl, &dm_nz, dm_line[0], DM_LINE - 1u) << 1;
        yr = dmg_sample(&qr, &dm_r, xr, &dm_nz, dm_line[1], DM_LINE - 1u) << 1;
        l[i] = pf_mix(xl, yl, dm_w);
        r[i] = pf_mix(xr, yr, dm_w);
    }
    }
}

/* ---- WALL (QUIET .. WALL) on the master: the New York trick. The mix squeezed hard (a peak compressor, ~90 ms release,
 * up to x8 of make-up) and into a soft clip, then mixed in with the dry a little under it (at the top -2.6 dB, so the
 * limiter after it has nothing to pump): the body comes up, the hits stay. With the
 * sends and DIST pushed (fx.c wall_push) and the FILL steps from 100 (seq.c) it is the wall; no wear, no tape ---- */
static int32_t wl_env, wl_g = 4096, wl_w;
static inline int wall_master_on(void) { return song.g[G_WALL] > 0 || wl_w; }
static __attribute__((noinline)) void wall_master(int32_t *l, int32_t *r, uint32_t n)
{
    int32_t w = clamp(song.g[G_WALL], 0, 127), tw = w * 200, pk = 0, gt, dg;   /* (the wet: up to ~0.78) */
    int32_t trim = 32768 - w * 112;                     /* (-4.9 dB at the top: what the pushed sends and DIST add before
                                                         * the level keeper sees the mix) */
    uint32_t i;
    if (!w && !wl_w) {
        wl_env = 0;
        wl_g = 4096;
        return;
    }
    for (i = 0; i < n; i++) {
        int32_t a = l[i] < 0 ? -l[i] : l[i], b = r[i] < 0 ? -r[i] : r[i];
        a = a > b ? a : b;
        if (a > pk)
            pk = a;
    }
    pk = pk > 1048575 ? 1048575 : pk;
    if (pk > wl_env)
        wl_env = pk;
    else
        wl_env -= wl_env >> 7;
    gt = wl_env > 1536 ? (int32_t)((9000u << 12) / (uint32_t)wl_env) : 32768;
    gt = gt > 32768 ? 32768 : gt;                       /* (Q12: up to x8) */
    dg = (gt - wl_g) / (int32_t)n;
    for (i = 0; i < n; i++) {
        int32_t yl, yr;
        wl_w += clamp(tw - wl_w, -SL_SLOPE, SL_SLOPE);
        wl_g += dg;
        yl = softclip(clamp(((l[i] >> 2) * wl_g) >> 10, -262143, 262143));
        yr = softclip(clamp(((r[i] >> 2) * wl_g) >> 10, -262143, 262143));
        yl = ((l[i] >> 1) * (65536 - wl_w) >> 15) + ((yl * wl_w) >> 15);   /* (the dry a little under: no pumping) */
        yr = ((r[i] >> 1) * (65536 - wl_w) >> 15) + ((yr * wl_w) >> 15);
        {   /* wider: the side up to x2 at the top (the reverb and the delay it pushed live there) */
            int32_t m = (yl + yr) >> 1, s = ((yl - yr) >> 1) + ((((yl - yr) >> 1) * wl_w) >> 15);
            l[i] = ((m + s) >> 1) * trim >> 14;
            r[i] = ((m - s) >> 1) * trim >> 14;
        }
    }
}

/* ---- the level keeper: DAMAGE and WALL may make the mix denser, never louder. The energy going into them and coming
 * out, each over ~46 ms (a block's: the sum of (x >> 7) squared, both sides), and a gain that brings the out back to the
 * in, only ever down (x1 .. x0.2: a tape's dropout is not filled up), gliding over each block ---- */
static int32_t kl_in, kl_out;                            /* (a block's energy: < 2^25) */
static int32_t kl_g = 4096;                              /* Q12 */
static uint32_t kl_energy(const int32_t *l, const int32_t *r, uint32_t n)
{
    uint32_t i, e = 0;
    for (i = 0; i < n; i++) {
        int32_t a = clamp(l[i] >> 7, -2047, 2047), b = clamp(r[i] >> 7, -2047, 2047);
        e += (uint32_t)(a * a + b * b) >> 4;
    }
    return e;
}
static uint32_t kl_isqrt(uint32_t v)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > v)
        b >>= 2;
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return r;
}
static inline int krst_level_on(void) { return kl_g != 4096; }
/* ein: the block's energy before DAMAGE and WALL (kl_energy); l, r after them, in place */
static __attribute__((noinline)) void krst_level(int32_t *l, int32_t *r, uint32_t n, uint32_t ein, int active)
{
    uint32_t i, a, b;
    int32_t eo = (int32_t)kl_energy(l, r, n), gt = 4096, dg;
    kl_in += ((int32_t)ein - kl_in) >> 6;
    kl_out += (eo - kl_out) >> 6;
    if (active && kl_out > kl_in * 2 / 5 && kl_out > 64) {
        a = (uint32_t)(kl_in > 0 ? kl_in : 0) * 2u / 5u;   /* (two fifths of the energy: a dense mix meets the limiter
                                                         * less than a peaky one and comes out ~4 dB louder at the same
                                                         * energy; this puts it back level with the dry) */
        b = (uint32_t)kl_out;
        while (b > (1u << 20)) {
            a >>= 1;
            b >>= 1;
        }
        gt = (int32_t)kl_isqrt(((a << 10) / (b ? b : 1u)) << 14);   /* sqrt(in / out), Q12 */
        gt = clamp(gt, 820, 4096);
    }
    dg = (gt - kl_g) / (int32_t)n;
    for (i = 0; i < n; i++) {
        kl_g += dg;
        l[i] = (clamp(l[i], -524287, 524287) * kl_g) >> 12;   /* (|x| < 2^19, g <= 2^12: fits) */
        r[i] = (clamp(r[i], -524287, 524287) * kl_g) >> 12;
    }
    kl_g = gt;
    if (!active && kl_g == 4096)
        kl_in = kl_out = 0;
}

/* ---- HISS: a tape's noise floor on the master, on its own knob (it runs while stopped too, as a tape would) ---- */
static uint32_t hs_st = 0x9E3779B9u;
static int32_t hs_lp[2];
static __attribute__((noinline)) void hiss_master(int32_t *l, int32_t *r, uint32_t n)
{
    int32_t h = clamp(song.g[G_HISS], 0, 127), lv = (h * h) >> 4;   /* (up to ~1000: about -30 dB under the mix) */
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t a, b;
        hs_st = hs_st * 1664525u + 1013904223u;
        a = (int32_t)hs_st >> 20;
        hs_st = hs_st * 1664525u + 1013904223u;
        b = (int32_t)hs_st >> 20;
        hs_lp[0] += (a - hs_lp[0]) >> 2;                /* (a little darker than white, as tape is) */
        hs_lp[1] += (b - hs_lp[1]) >> 2;
        l[i] += (hs_lp[0] * lv) >> 10;
        r[i] += (hs_lp[1] * lv) >> 10;
    }
}
