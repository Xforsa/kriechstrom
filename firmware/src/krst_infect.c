/* SPDX-License-Identifier: GPL-3.0-only */
/* KRIECHSTROM: INFECT, a sidechain: one track's sound driven by another's. The INSERT INFCT (A SRC: the track it listens
 * to, T1..T4, itself too; B MODE; C DEPTH; MIX as the others). Included by fx.c after the INSERTs and DAMAGE.
 *   GATE   the track sounds only while the source does (the kick chops the pad; its tail closes it: ~25 ms)
 *   DUCK   the track falls away under the source and comes back over ~95 ms (the pad pumps with the kick)
 *   CRUSH  the source's hits crush the track: fewer bits and a held sample, by how loud the source is
 *   FOLD   the source's hits fold the track: a wavefolder driven up to ~8x by them
 *   RING   the track times the source: a ring modulator (the drums through the synth)
 *   VOX    a vocoder: the source's spectrum, in 8 bands (180 Hz .. 5.5 kHz), opens the same bands of the track (the drums
 *          or a voice speak through the pad); DEPTH its level, MIX as on every INSERT (ins_vox)
 * The source is heard before its LEVEL, PAN and sends (a track at LEVEL 0 still drives: a silent trigger) and one block
 * late (32 samples, 0.7 ms): every part writes its block into one half of ifx_buf, reads the other (the second core may
 * run a part beside this one). Only a track something listens to is copied (ifx_listen); with no INFCT nothing runs. */

enum { IFX_GATE, IFX_DUCK, IFX_CRUSH, IFX_FOLD, IFX_RING, IFX_VOX };
static int32_t ifx_buf[2][NTRK][CTL] __attribute__((section(".pool")));   /* each track's last block, two halves (1 KB) */
static int32_t ifx_pk[2][NTRK];                          /* .. its peak */
static uint32_t ifx_half;                                /* the half written this block */
static uint8_t ifx_listen;                               /* bit k: track k is some INFCT's source */

/* mix_block, before the parts: the halves swap, who is listened to */
static void ifx_begin(void)
{
    uint32_t k, m = 0;
    ifx_half ^= 1u;
    for (k = 0; k < NTRK; k++)
        if (trk[k].p[P_ITYPE] == IT_INFECT || ins[k].type == IT_INFECT)
            m |= 1u << clamp(trk[k].p[P_IA], 0, NTRK - 1);
    ifx_listen = (uint8_t)m;
}
/* mix_part: the track's block b as it sounds before its LEVEL (b 0: silent) */
static void ifx_feed(const track_t *t, const int32_t *b, uint32_t n)
{
    uint32_t k = (uint32_t)(t - trk) % NTRK, i;
    int32_t *d = ifx_buf[ifx_half][k], pk = 0;
    if (!(ifx_listen >> k & 1u))
        return;
    if (!b) {
        memset(d, 0, n * sizeof *d);
        ifx_pk[ifx_half][k] = 0;
        return;
    }
    for (i = 0; i < n; i++) {
        int32_t v = clamp(b[i], -32767, 32767), a = v < 0 ? -v : v;
        d[i] = v;
        if (a > pk)
            pk = a;
    }
    ifx_pk[ifx_half][k] = pk;
}

/* the INSERT INFCT on track t's block b */
/* VOX: 8 bands, each the SVF's band (tsvf_lp's v1) at Q 4 on the source (analysis) and on the track (synthesis); the
 * source band's level (a follower, ~6 ms) scales the track's. At half the rate (the coefficients are a 22 kHz SVF's:
 * the cut indices one octave up). States in s->v32: per band the analysis' two, the synthesis' two, the level; then
 * the last output (41 of 128). The states are not clamped: at Q 4 a band rings to
 * at most ~4x its input (32767), under the SVF's own bound (150000) */
#define VX_N 8u
static const int32_t VX_CUT[VX_N] = {12866, 15511, 18156, 20590, 23104, 25587, 28082, 30572};   /* 180 300 500 800
                                                         * 1300 2100 3400 5500 Hz: cut indices, Q8 (30 Hz .. 16 kHz), an
                                                         * octave up (3589): the SVF runs at 22 kHz */
#define VX_K 1024                                       /* the bands' damping, Q12: Q 4 */
static tsvf_t vx_c[VX_N];
static uint8_t vx_ready;
static __attribute__((noinline)) void ins_vox(ins_t *s, int32_t *b, const int32_t *a, uint32_t n, int32_t dpt, int32_t tw)
{
    uint32_t i, k;
    int32_t *st = s->v32, yp = st[5u * VX_N];               /* (the last output, x 8: the line to the next pair's) */
    if (!vx_ready) {                                    /* (the bands are fixed: their coefficients once) */
        for (k = 0; k < VX_N; k++)
            tsvf_coef_k(&vx_c[k], VX_CUT[k], VX_K);
        vx_ready = 1;
    }
    for (i = 0; i + 1u < n; i += 2u) {                  /* at half the rate (22 kHz: the top band is 5.5 kHz), a pair
                                                         * averaged in, the output a line between the pairs */
        int32_t x = clamp((b[i] + b[i + 1u]) >> 4, -32767, 32767);   /* (the band states stay within 2^18) */
        int32_t m = (a[i] + a[i + 1u]) >> 1, y = 0;   /* (ifx_feed clamped the source) */
        for (k = 0; k < VX_N; k++) {
            const tsvf_t *c = &vx_c[k];
            int32_t *q = st + 5u * k, v3, v1, v2, e;
            v3 = m - q[1];                              /* the source's band */
            v1 = (c->a1 * q[0] + c->a2 * v3) >> 13;
            v2 = q[1] + ((c->a2 * q[0] + c->a3 * v3) >> 13);
            q[0] = 2 * v1 - q[0];
            q[1] = 2 * v2 - q[1];
            e = (v1 * VX_K) >> 12;                      /* (back to 1 at the centre) */
            e = e < 0 ? -e : e;
            q[4] += (e - q[4]) >> 7;                    /* its level (~6 ms) */
            v3 = x - q[3];                              /* the track's band */
            v1 = (c->a1 * q[2] + c->a2 * v3) >> 13;
            v2 = q[3] + ((c->a2 * q[2] + c->a3 * v3) >> 13);
            q[2] = 2 * v1 - q[2];
            q[3] = 2 * v2 - q[3];
            y += (((v1 * VX_K) >> 12) * q[4]) >> 13;    /* (x 4: a band's level is a fraction of the whole; the band
                                                         * and its level within 15 bits: Q 4 rings to 4x, k takes it back) */
        }
        y = ((clamp(y, -32767, 32767) >> 1) * dpt) >> 11;   /* DEPTH: the vocoder's level (x 8: the track's scale) */
        s->w += clamp(tw - s->w, -2 * SL_SLOPE, 2 * SL_SLOPE);   /* (MIX's glide a pair at a time) */
        b[i] = pf_mix(b[i], (yp + y) >> 1, s->w);       /* MIX: the track as it is .. all vocoded, as the others */
        b[i + 1u] = pf_mix(b[i + 1u], y, s->w);
        yp = y;
    }
    st[5u * VX_N] = yp;
}

static __attribute__((noinline)) void ins_infect(ins_t *s, const track_t *t, int32_t *b, uint32_t n, int32_t tw)
{
    uint32_t src = (uint32_t)clamp(t->p[P_IA], 0, NTRK - 1), mode = (uint32_t)clamp(t->p[P_IB], 0, IFX_VOX), i;
    const int32_t *a = ifx_buf[ifx_half ^ 1u][src];
    int32_t dpt = clamp(t->p[P_IC], 0, 127) * 258, e0 = s->x[0], e1, pk = ifx_pk[ifx_half ^ 1u][src];
    if (mode == IFX_VOX) {
        ins_vox(s, b, a, n, dpt, tw);
        return;
    }
    /* the source's level: up at once, down by the mode (GATE ~25 ms, DUCK ~95 ms, the others ~12 ms) */
    e1 = pk > e0 ? pk : e0 - (e0 >> (mode == IFX_DUCK ? 7 : mode == IFX_GATE ? 5 : 4)) - 1;
    e1 = e1 < 0 ? 0 : e1;
    s->x[0] = e1;
    {
    int32_t e = e0, de = (e1 - e0) / (int32_t)n;      /* (the level a line over the block) */
    for (i = 0; i < n; i++) {
        int32_t x = clamp(b[i], -262143, 262143), y, g;   /* (x >> 3 times a Q15 gain fits 32 bits) */
        int32_t es = e * 3 > 32768 ? 32768 : e * 3;        /* (a drum's hit opens it fully, a hat a little) */
        int32_t amt = (int32_t)(((uint32_t)dpt * (uint32_t)es) >> 22);   /* depth x level, 0 .. 255 */
        e += de;
        s->w += clamp(tw - s->w, -SL_SLOPE, SL_SLOPE);
        switch (mode) {
        case IFX_GATE:                                   /* (the level squared: an expander, a tail closes it) */
            g = 32768 - dpt + ((dpt * ((es * es) >> 15)) >> 15);
            y = ((x >> 3) * g) >> 12;
            break;
        case IFX_DUCK:
            g = 32768 - ((dpt * es) >> 15);
            y = ((x >> 3) * g) >> 12;
            break;
        case IFX_CRUSH:
            if (!s->cnt) {
                s->hold = ins_shape(IT_CRUSH, x, (uint32_t)(amt * 12) >> 8);
                s->cnt = (uint8_t)(1 + ((amt * 23) >> 8));
            }
            s->cnt--;
            y = s->hold;
            break;
        case IFX_FOLD:
            g = 4096 + ((amt * amt * 7) >> 4);            /* Q12: 1x .. ~8x */
            y = ins_shape(IT_FOLD, ((x >> 3) * g) >> 9, 0);
            break;
        default:                                         /* RING: x times the source (Q15), DEPTH from dry to all */
            g = clamp(a[i] * 2, -32768, 32767);
            y = ((x >> 3) * g) >> 12;
            y = x + ((((y - x) >> 4) * dpt) >> 11);
            break;
        }
        b[i] = pf_mix(b[i], y, s->w);
    }
    }
}
