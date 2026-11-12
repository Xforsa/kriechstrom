/* SPDX-License-Identifier: GPL-3.0-only */
/* KRIECHSTROM: INFECT, one track's sound driven by another's. The INSERT INFCT (A SRC: the track it listens to, T1..T4,
 * itself too; B MODE; C DEPTH; MIX as the others). Included by fx.c after the INSERTs and DAMAGE.
 *   GATE   the track sounds only while the source does (the kick chops the pad; its tail closes it)
 *   DUCK   the track falls away under the source (the pad breathes with the kick)
 *   CRUSH  the source's hits crush the track: fewer bits and a held sample, by how loud the source is
 *   FOLD   the source's hits fold the track: a wavefolder driven up to ~8x by them
 *   RING   the track times the source: a ring modulator (the drums through the synth)
 * The source is heard before its LEVEL, PAN and sends (a track at LEVEL 0 still drives: a silent trigger) and one block
 * late (32 samples, 0.7 ms): every part writes its block into one half of ifx_buf, reads the other (the second core may
 * run a part beside this one). Only a track something listens to is copied (ifx_listen); with no INFCT nothing runs. */

enum { IFX_GATE, IFX_DUCK, IFX_CRUSH, IFX_FOLD, IFX_RING };
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
static __attribute__((noinline)) void ins_infect(ins_t *s, const track_t *t, int32_t *b, uint32_t n, int32_t tw)
{
    uint32_t src = (uint32_t)clamp(t->p[P_IA], 0, NTRK - 1), mode = (uint32_t)clamp(t->p[P_IB], 0, IFX_RING), i;
    const int32_t *a = ifx_buf[ifx_half ^ 1u][src];
    int32_t dpt = clamp(t->p[P_IC], 0, 127) * 258, e0 = s->x[0], e1, pk = ifx_pk[ifx_half ^ 1u][src];
    e1 = pk > e0 ? pk : e0 - (e0 >> 4) - 1;             /* the source's level: up at once, down over ~12 ms */
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
