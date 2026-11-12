/* SPDX-License-Identifier: GPL-3.0-only */
/* KRIECHSTROM: RESONATE, the mix rings four strings tuned to a chord (FX > RESONATE: AMT, KEY, CHORD). Included by fx.c
 * after the INSERTs; mix_block calls it on the master, the buses in, before DAMAGE (the strings wear with the mix).
 * Each string a comb: a line as long as its period (the KEY's root in the third octave, C3 131 Hz .. B3 247 Hz, and the
 * CHORD's intervals over it, up to two octaves), fed with the mix, a one-pole damping in its loop, fed back (0.90 ..
 * 0.997 by AMT: from a short buzz to a ring of seconds). Its harmonics are all of the root's: the kick rings the root, a snare the whole chord,
 * a held note in the key makes the strings sing and one out of it beats against them. Strings 1 and 3 to the left,
 * 2 and 4 to the right. The strings' sum, scaled by 4 (1 - feedback) (a comb peaks at 1 / (1 - fb): the notes in the key
 * come out 12 dB up, into a tanh), comes in up to the dry's level by AMT. AMT 0: nothing runs (faded out over 2.9 ms, then the lines cleared at the next start). */

#define RS_N 4u
#define RS_LEN 512u                                     /* a line (a power of 2: masked; C3's period is 337 samples) */
static int16_t rs_line[RS_N][RS_LEN] __attribute__((section(".pool")));   /* 4 KB */
static int32_t rs_lp[RS_N], rs_w;                      /* the dampings, the wet now (Q15) */
static uint32_t rs_wp;                                 /* the lines' write */
static const int8_t RS_CHORD[8][RS_N] = {               /* N_RCHD: semitones over the root */
    {0, 12, 19, 24}, {0, 7, 12, 19}, {0, 7, 12, 15}, {0, 7, 12, 16},   /* OCT 5TH MIN MAJ */
    {0, 7, 12, 17}, {0, 7, 10, 15}, {0, 6, 12, 15}, {0, 1, 12, 13},    /* SUS MIN7 DIM CLST */
};
static inline int res_on(void) { return song.g[G_RES] > 0 || rs_w; }
static const uint32_t RS_PERIOD[48] = {   /* C2 .. B5's periods, Q8 (44.1 kHz) */
    172607u, 162919u, 153775u, 145145u, 136998u, 129309u, 122052u, 115201u, 108736u,
    102633u, 96872u, 91435u, 86303u, 81460u, 76888u, 72572u, 68499u, 64655u,
    61026u, 57601u, 54368u, 51316u, 48436u, 45718u, 43152u, 40730u, 38444u,
    36286u, 34250u, 32327u, 30513u, 28800u, 27184u, 25658u, 24218u, 22859u,
    21576u, 20365u, 19222u, 18143u, 17125u, 16164u, 15256u, 14400u, 13592u,
    12829u, 12109u, 11429u,
};
static __attribute__((noinline)) void res_master(int32_t *l, int32_t *r, uint32_t n)
{
    int32_t a = clamp(song.g[G_RES], 0, 127), tw = a ? 8192 + a * 192 : 0;  /* the wet: up to ~1 */
    int32_t fb = 29491 + a * 25, norm = (32768 - fb) * 8;                    /* Q15: 0.90 .. 0.997 */
    uint32_t key = (uint32_t)clamp(song.g[G_RKEY], 0, 11), ch = (uint32_t)clamp(song.g[G_RCHD], 0, 7), k, i;
    uint32_t len[RS_N];
    if (!a && !rs_w)
        return;
    if (!rs_w) {                                        /* starting: no stale ring */
        memset(rs_line, 0, sizeof rs_line);
        memset(rs_lp, 0, sizeof rs_lp);
    }
    for (k = 0; k < RS_N; k++)
        len[k] = RS_PERIOD[12u + key + (uint32_t)RS_CHORD[ch][k]] - 256u;   /* (C3 up; 12 + 11 + 24: 47 at most; less the
                                                         * damping's one sample of delay, so it rings in tune) */
    for (i = 0; i < n; i++) {
        int32_t x = (clamp(l[i] + r[i], -262143, 262143)) >> 3, sl = 0, sr = 0;   /* the mix, mono, at a quarter */
        rs_w += clamp(tw - rs_w, -SL_SLOPE, SL_SLOPE);
        for (k = 0; k < RS_N; k++) {
            uint32_t li = len[k] >> 8, fr = len[k] & 255u, ri = rs_wp - li;
            int32_t a0 = rs_line[k][ri & (RS_LEN - 1u)], a1 = rs_line[k][(ri - 1u) & (RS_LEN - 1u)];
            int32_t y = a0 + (((a1 - a0) * (int32_t)fr) >> 8);
            int32_t d = y - rs_lp[k], f;
            rs_lp[k] += (d + (d < 0)) >> 1;             /* the damping: highs die first, as on a string */
            f = rs_lp[k] * fb;
            y = x + ((f + (f < 0 ? 32767 : 0)) >> 15);   /* (both towards zero: no offset rings on by itself) */
            y = clamp(y, -32767, 32767);
            rs_line[k][rs_wp & (RS_LEN - 1u)] = (int16_t)y;
            if (k & 1u)
                sr += y;
            else
                sl += y;
        }
        rs_wp++;
        sl = softclip((sl * norm) >> 15);               /* (x 4 over the comb's peak: they ring out; the tanh keeps it) */
        sr = softclip((sr * norm) >> 15);
        l[i] += ((sl << 2) * (rs_w >> 3)) >> 12;        /* (x 4: back from the quarter; the wet Q15) */
        r[i] += ((sr << 2) * (rs_w >> 3)) >> 12;
    }
}
