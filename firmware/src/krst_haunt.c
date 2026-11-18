/* SPDX-License-Identifier: GPL-3.0-only */
/* KRIECHSTROM: HAUNT, the tape haunted (FX > HAUNT: MODE, AMT). Included by fx.c after SPLICE; mix_block calls it last on
 * the master. One of two at a time:
 *   SUCK   a reverse swell into each snare and clap, as a reversed cymbal: noise whose band opens from 500 Hz to 7 kHz
 *          as it rises over the beat before the hit, cut dead on it. The drum tracks' steps say where the next one falls
 *          (without a drum track: beats 2 and 4)
 *   DRAG   a tape brake on the last beat of every bar: the tape slows (to half speed at the top of AMT: an octave down
 *          at the bar line) and its level sags; on the one it is back at once
 * AMT 0 or MODE OFF: nothing runs, the output as without it. The grid is BPM's from PLAY (as SPLICE's); both run only
 * while PLAY plays. DRAG's tape is wiped at PLAY and when MODE changes. */

#define HN_BYTES 7600u                                   /* (the pool keeps its 8 KB spare: build.py) */
static union {
    int16_t b16[HN_BYTES / 2u];
} hn_buf __attribute__((section(".pool")));
enum { HN_OFF, HN_SUCK, HN_DRAG };                     /* (G_HAUNT) */
#define HN_DR_LEN (HN_BYTES / 2u)                         /* DRAG: 3800 mono samples at 22 kHz (16 bits), 172 ms */
static struct {
    uint8_t mode, live;
    uint32_t w, cnt;
    uint32_t pos;                                       /* samples since PLAY (haunt_tick: kept while HAUNT is off too,
                                                         * so a HAUNT turned on mid-song sits on the song's grid) */                   /* the write position, the decimation's count; the grid */
    int32_t half, rp, last;                             /* DRAG: the pair being recorded, the reader (Q8), its last
                                                         * sample */
    int32_t sw_left, sw_len, sw_rp;                     /* SUCK: the swell: samples left, its length */
    int32_t s1, s2, rng;                                /* .. its band-pass, its noise */
    tsvf_t f;
} hn;

static inline void haunt_tick(uint32_t n) { if (song.playing) hn.pos += n; }   /* (mix_block, every block, after HAUNT) */
static inline int haunt_on(void) { return (song.g[G_HAUNT] > 0 && song.g[G_HAMT] > 0) || hn.live; }
static void haunt_clear(uint32_t mode)
{
    uint32_t i;
    for (i = 0; i < HN_BYTES / 2u; i++)
        hn_buf.b16[i] = 0;
    hn.mode = (uint8_t)mode;
    hn.w = hn.cnt = 0;
    hn.half = hn.rp = hn.last = 0;
    hn.sw_left = 0;
    hn.rng = 0x5EED4A17;
}
static void haunt_start(void)                           /* (seq.c seq_start: PLAY) */
{
    haunt_clear((uint32_t)clamp(song.g[G_HAUNT], 0, 2));
    hn.pos = 0;
}

static const step_t *seq_steps(const track_t *t);      /* (song_chain.c: the steps the track plays, a song's too) */
/* does a snare or a clap fall on sixteenth k (since PLAY)? any DRUM track, unmuted; none: beats 2 and 4 */
static int hn_hit_at(uint32_t k)
{
    uint32_t t, drums = 0;
    for (t = 0; t < NTRK; t++) {
        const track_t *tr = &trk[t];
        uint32_t len = (uint32_t)clamp(tr->p[P_SLEN], 1, 64);
        if (tr->eng_req != ENGI_DRUM)
            continue;
        drums = 1;
        if (tr->p[P_MUTE])
            continue;
        {
            const step_t *sp = &seq_steps(tr)[k % len];
            uint32_t q;
            if (sp->hit & ((1u << DV_SNARE) | (1u << DV_CLAP)))   /* (the grid's hits, or notes: GM snares, claps) */
                return 1;
            for (q = 0; q < sp->n && q < 4u; q++)
                if (sp->time == ST_NOTE && (drum_lane(sp->note[q]) == DV_SNARE || drum_lane(sp->note[q]) == DV_CLAP))
                    return 1;
        }
    }
    return !drums && (k & 15u) % 8u == 4u;
}

static __attribute__((noinline)) void haunt_master(int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t mode = (uint32_t)clamp(song.g[G_HAUNT], 0, 2), i;
    int32_t amt = song.g[G_HAMT] > 0 ? clamp(song.g[G_HAMT], 0, 127) : 0;
    uint32_t bpm = (uint32_t)clamp(song.g[G_BPM], 40, 240), st = (uint32_t)(FS * 15u) / bpm, bar = 16u * st;
    if (mode != hn.mode)
        haunt_clear(mode);
    if (!mode || !amt) {
        hn.live = 0;
        return;
    }
    hn.live = 1;
    if (mode == HN_SUCK) {                              /* a reverse swell into the next snare: noise, its band opening */
        uint32_t ahead = 4u;
        int32_t g = amt * 280;                          /* (Q15 of x 1.1 at the top) */
        if (ahead * st > FS)                            /* (slow: half a beat, a swell up to a second) */
            ahead = 2u;
        uint32_t step = hn.pos / st, rem = hn.pos % st;  /* (the sixteenth we are in, and how far into it) */
        for (i = 0; i < n; i++, rem++) {
            if (rem >= st)
                rem = 0, step++;
            if (song.playing && rem == 0) {             /* a sixteenth: a beat ahead, is there a hit? */
                if (!hn.sw_left && hn_hit_at(step + ahead)) {
                    hn.sw_len = hn.sw_left = (int32_t)(ahead * st);
                    hn.s1 = hn.s2 = 0;
                }
            }
            if (hn.sw_left > 0) {
                int32_t u = hn.sw_len - hn.sw_left, e, y, cut;
                uint32_t q = ((uint32_t)u << 13) / ((uint32_t)hn.sw_len >> 2 | 1u);   /* Q15, 0 .. 1 */
                e = (int32_t)((((q * q) >> 15) * q) >> 15);   /* (u^3: rising, the most at the end) */
                if (hn.sw_left < 32)                    /* (cut at the hit: 32 samples) */
                    e = (e * hn.sw_left) >> 5;
                if (!(hn.sw_left & 31) || u == 0) {     /* the band, once in 32: 500 Hz .. 7 kHz as it rises */
                    cut = kv_cut(500u + ((q * 6500u) >> 15));   /* (krst_kits.c: Hz to dsp.c's index) */
                    tsvf_coef_k(&hn.f, cut, 2900);       /* (Q ~1.4) */
                }
                {
                    int32_t x = (int32_t)noise32(&hn.rng) >> 16, v3 = x - hn.s2;
                    int32_t v1 = (hn.f.a1 * hn.s1 + hn.f.a2 * v3) >> 13;
                    int32_t v2 = hn.s2 + ((hn.f.a2 * hn.s1 + hn.f.a3 * v3) >> 13);
                    hn.s1 = clamp(2 * v1 - hn.s1, -150000, 150000);
                    hn.s2 = clamp(2 * v2 - hn.s2, -150000, 150000);
                    y = clamp((v1 * 2900) >> 12, -65535, 65535);
                }
                y = (((y * e) >> 15) * g) >> 14;        /* (x 2: the mix's level) */
                l[i] += y;
                r[i] += (y * 29) >> 5;
                hn.sw_left--;
            }
        }
        return;
    }
    /* DRAG: a tape brake on the bar's last beat: the reader slows (to half speed at the top: an octave down at the bar
     * line), the mix recorded mono at half rate meanwhile; on the one, back to the live mix in 3 ms */
    {
        int16_t *b = hn_buf.b16;
        uint32_t size = HN_DR_LEN, win = bar / 4u, start = bar - win;   /* (the last beat) */
        int32_t depth = amt * 129;                      /* (Q15: how far the speed falls, 0.5 at the top) */
        {   /* (a slow tempo: no deeper than the ring holds; the lag is depth x window / 2, in half-rate samples) */
            uint32_t lag = ((uint32_t)depth * win) >> 17;   /* (32 bits: depth < 2^14, win < 2^16) */
            if (lag + 64u > size)
                depth = (int32_t)(((size - 64u) << 17) / win);
        }
        uint32_t ph = hn.pos % bar;                     /* (where in the bar, since PLAY) */
        for (i = 0; i < n; i++, ph = ph + 1u == bar ? 0u : ph + 1u) {
            int32_t m = (l[i] + r[i]) >> 2;              /* (as SPLICE's tape: (l + r) / 4) */
            if (hn.half & 1) {                          /* record: the pair's mean, at half the rate */
                b[hn.w] = (int16_t)clamp((m + (hn.half >> 1)) >> 1, -32767, 32767);
                hn.w = hn.w + 1u == size ? 0u : hn.w + 1u;
                hn.half = 0;
            } else {
                hn.half = (m << 1) | 1;
            }
            if (song.playing && hn.pos + i >= bar && ph >= start) {   /* the brake */
                int32_t u = (int32_t)(((ph - start) << 12) / win), rate, y, k0, k1, f;
                if (ph == start || !hn.last)            /* (from the newest sample: the one just written; also
                                                         * when turned on mid-window) */
                    hn.rp = (int32_t)(hn.w ? hn.w - 1u : size - 1u) * 256;
                rate = 32768 - ((depth * u) >> 12);     /* Q15 of the speed */
                hn.rp += rate >> 8;                     /* (half-rate samples, Q8: x 1/2 a sample) */
                if (hn.rp >= (int32_t)size * 256)
                    hn.rp -= (int32_t)size * 256;
                k0 = hn.rp >> 8;
                k1 = k0 + 1 == (int32_t)size ? 0 : k0 + 1;
                f = hn.rp & 255;
                y = (b[k0] + (((b[k1] - b[k0]) * f) >> 8)) << 1;
                y = (y * (4096 - (u >> 2))) >> 12;      /* (and the level sags a quarter) */
                hn.last = y;
                l[i] = y;
                r[i] = y;
            } else if (song.playing && hn.last && ph < 132u) {   /* the one: back in 3 ms */
                int32_t u = (int32_t)ph * 31;
                l[i] = (l[i] * u + hn.last * (4096 - u)) >> 12;
                r[i] = (r[i] * u + hn.last * (4096 - u)) >> 12;
            }
            if (ph == 131u)
                hn.last = 0;
        }
    }
}
