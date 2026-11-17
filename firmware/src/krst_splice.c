/* SPDX-License-Identifier: GPL-3.0-only */
/* KRIECHSTROM: SPLICE, the tape cut and glued (FX > SPLICE: AMT). Included by fx.c after the INSERTs; mix_block calls it
 * last on the master (the buses in, after DAMAGE, WALL and HISS: the whole tape is cut).
 * The mix is recorded all the time, mono, at 22.05 kHz, into a ring of 8192 samples (371 ms). While PLAY plays, at each
 * sixteenth (the grid from BPM and PLAY; never on a bar's first) a roll against AMT may glue in a piece instead of the
 * live mix, one, two or four sixteenths long:
 *   REPEAT   the last sixteenth again (a stutter)
 *   REVERSE  the last sixteenth backwards
 *   JUMP     a sixteenth from a beat ago (or as far back as the ring holds): a piece glued in the wrong place
 *   SLOW     the last sixteenth at half speed, an octave down (the tape dragged)
 *   DROP     nothing: the tape cut (a dropout to silence)
 * Low AMT: a cut now and then, REPEAT REVERSE and JUMP; up to AMT 127 one sixteenth in two, longer, with SLOW and DROP.
 * In and out over 64 samples (no click; the cut stays hard). The live mix goes on being recorded (no piece of a piece).
 * The dice are seeded and the tape wiped at PLAY: a project cuts the same way each time. AMT 0: nothing runs. Not with an external clock's
 * tempo (the grid is BPM's) */

#define SPL_LEN 8192u                                    /* the ring, 22.05 kHz samples (a power of 2: masked) */
static int16_t spl_buf[SPL_LEN] __attribute__((section(".pool")));   /* 16 KB */
enum { SPL_REPEAT, SPL_REVERSE, SPL_JUMP, SPL_SLOW, SPL_DROP };
static struct {
    uint32_t w, fill;                                   /* the ring's next write, how much of it holds sound */
    int32_t half;                                       /* the first of a pair (the ring is at half the rate) */
    uint32_t pos, next, step;                           /* samples since PLAY, the next sixteenth's, its number */
    uint32_t left, len;                                 /* the piece: samples left of it, its length */
    int32_t rp, spd, from, seg;                         /* its read position and speed (Q8, ring samples), where
                                                         * it started, its loop (ring samples, 0: none) */
    int32_t g;                                          /* the piece's share now (Q15) */
    uint8_t type, on;
    int32_t rng;
} spl;

static inline int splice_on(void) { return song.g[G_SPLICE] > 0 || spl.on; }
static void splice_start(void)                          /* (seq.c seq_start: PLAY) */
{
    spl.pos = spl.step = 0;
    spl.next = 0;
    spl.left = 0;
    spl.g = 0;
    spl.fill = 0;                                       /* (a fresh tape: nothing glued before it holds a sixteenth) */
    spl.half = 0;
    spl.rng = 0x5EED1234;
}
static inline int32_t spl_read(int32_t rp)               /* the ring at rp (Q8), between two samples */
{
    uint32_t i = (uint32_t)(rp >> 8) & (SPL_LEN - 1u), j = (i + 1u) & (SPL_LEN - 1u);
    int32_t a = spl_buf[i], b = spl_buf[j];
    return a + (((b - a) * (rp & 255)) >> 8);
}
/* a sixteenth starts: maybe a piece (st: its length in 44.1 kHz samples) */
static void splice_roll(int32_t amt, uint32_t st)
{
    uint32_t r = noise32(&spl.rng), seg = st >> 1, k;      /* (a sixteenth in ring samples) */
    uint32_t thr = (uint32_t)(amt * amt) * 66u;            /* (of 2^21: one in two at 127) */
    if ((r >> 11) >= thr || (spl.step & 15u) == 0u || spl.fill < seg + 64u || seg + 64u > SPL_LEN)
        return;
    r = noise32(&spl.rng);
    k = (r & 255u) * (uint32_t)(64 + amt) >> 8;            /* (the type: 0 .. ~190, the far end only at a high AMT) */
    spl.type = k < 60u ? SPL_REPEAT : k < 100u ? SPL_REVERSE : k < 135u ? SPL_JUMP : k < 165u ? SPL_SLOW : SPL_DROP;
    k = (r >> 8) & 255u;                                  /* (its length: 1, 2 or 4 sixteenths) */
    spl.len = st * (k < (uint32_t)(amt * 2) ? (k < (uint32_t)amt / 2u ? 4u : 2u) : 1u);
    spl.left = spl.len;
    spl.seg = 0;
    switch (spl.type) {
    case SPL_REPEAT:                                       /* the last sixteenth, looped */
        spl.from = (int32_t)((spl.w - seg) & (SPL_LEN - 1u)) << 8;
        spl.seg = (int32_t)seg;
        spl.spd = 128;
        break;
    case SPL_REVERSE:                                      /* .. backwards, looped */
        spl.from = (int32_t)((spl.w - 1u) & (SPL_LEN - 1u)) << 8;
        spl.seg = (int32_t)seg;
        spl.spd = -128;
        break;
    case SPL_JUMP: {                                       /* a beat ago, as far as the ring holds */
        uint32_t back = 4u * seg, most = spl.fill - seg - 8u;   /* (fill > seg + 64: most > 0) */
        back = back < most ? back : most;
        spl.from = (int32_t)((spl.w - back) & (SPL_LEN - 1u)) << 8;
        spl.spd = 128;
        break;
    }
    case SPL_SLOW:                                         /* the last sixteenth at half speed */
        spl.from = (int32_t)((spl.w - seg) & (SPL_LEN - 1u)) << 8;
        spl.spd = 64;
        break;
    default:
        spl.from = 0;
        spl.spd = 0;
        break;
    }
    spl.rp = spl.from;
}
static __attribute__((noinline)) void splice_master(int32_t *l, int32_t *r, uint32_t n)
{
    int32_t amt = clamp(song.g[G_SPLICE], 0, 127);
    uint32_t i, st = (uint32_t)(FS * 15u) / (uint32_t)clamp(song.g[G_BPM], 40, 240);   /* a sixteenth, samples */
    for (i = 0; i < n; i++) {
        int32_t x = l[i] + r[i];
        if (spl.half & 1) {                              /* record: the pair's mean, at half the rate */
            spl_buf[spl.w] = (int16_t)clamp((x + spl.half) >> 3, -32767, 32767);   /* (half holds the first's, x 4) */
            spl.w = (spl.w + 1u) & (SPL_LEN - 1u);
            spl.fill += spl.fill < SPL_LEN;
            spl.half = 0;
        } else {
            spl.half = (x & ~1) | 1;                     /* (bit 0 marks it held) */
        }
        if (song.playing && amt) {
            if (spl.pos >= spl.next) {
                if (!spl.left)
                    splice_roll(amt, st);
                spl.next += st;
                spl.step++;
            }
            spl.pos++;
        }
        {   /* the piece over the live mix */
            int32_t tg = spl.left ? 32768 : 0, y;
            if (spl.left && spl.left < 64u)
                tg = (int32_t)spl.left << 9;             /* (out over its last 64 samples) */
            spl.g += clamp(tg - spl.g, -512, 512);        /* (in over 64 samples) */
            if (!spl.g)
                continue;
            y = spl.type == SPL_DROP ? 0 : spl_read(spl.rp) << 1;
            l[i] += ((clamp(y - l[i], -524287, 524287) >> 4) * spl.g) >> 11;   /* (2^15 x 2^15: fits) */
            r[i] += ((clamp(y - r[i], -524287, 524287) >> 4) * spl.g) >> 11;
            if (spl.left) {
                spl.left--;
                spl.rp += spl.spd;
                if (spl.seg && (spl.spd > 0 ? spl.rp - spl.from >= spl.seg << 8 : spl.from - spl.rp >= spl.seg << 8))
                    spl.rp = spl.from;                    /* (REPEAT, REVERSE: round the sixteenth again; rp is
                                                         * not wrapped: spl_read masks it) */
            }
        }
    }
    spl.on = amt > 0 || spl.g || spl.left;
}
