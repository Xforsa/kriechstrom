/* SPDX-License-Identifier: GPL-3.0-only */
/* KRIECHSTROM: HAUNT, the tape haunted (FX > HAUNT: MODE, AMT). Included by fx.c after SPLICE; mix_block calls it last on
 * the master. One of three at a time (they share one buffer):
 *   PRINT  print-through: a dark, wobbling ghost of what plays a beat later, heard before it. A loop's next beat is what
 *          it played three beats ago, so the ghost is the mix of three beats back (8 bits at 2.75 kHz, dark, saturated)
 *   SUCK   a reversed reverb rising into each snare and clap: the drum tracks' steps say where the next one falls; a beat
 *          before it, the last beat of a small reverb of the mix's top (5.5 kHz) plays backwards, swelling into the hit
 *          (without a drum track: into beats 2 and 4)
 *   DRAG   the tape drags: over the last beat of every bar the mix slows (a delay that grows, up to 43 ms: about a
 *          semitone down at its end), and on the one it is back at once
 * AMT 0 or MODE OFF: nothing runs, the output as without it. The grid is BPM's from PLAY (as SPLICE's); PRINT runs
 * stopped too, SUCK and DRAG only while PLAY plays. The buffer is wiped at PLAY and when MODE changes. */

#define HN_BYTES 7600u                                   /* (the pool keeps its 8 KB spare: build.py) */
static union {
    int8_t b8[HN_BYTES];
    int16_t b16[HN_BYTES / 2u];
} hn_buf __attribute__((section(".pool")));
enum { HN_OFF, HN_PRINT, HN_SUCK, HN_DRAG };
#define HN_PR_DEC 16u                                    /* PRINT: 44.1 kHz / 16 = 2756 Hz */
#define HN_SK_DEC 8u                                     /* SUCK: 5512 Hz */
static const uint16_t HN_SK_D[4] = {241, 309, 376, 449};   /* SUCK's reverb lines (5.5 kHz samples) */
#define HN_SK_LINES (241u + 309u + 376u + 449u)
#define HN_SK_RING (HN_BYTES - 2u * HN_SK_LINES)          /* its output, 8 bits: 4850 samples, 0.88 s */
#define HN_DR_FRAMES (HN_BYTES / 4u)                      /* DRAG: 1900 stereo frames (16 bits), 43 ms */
static struct {
    uint8_t mode, live;
    uint32_t w, cnt, pos, next, step, ph;               /* (ph: DRAG's place in the bar) */                   /* the write position, the decimation's count; the grid */
    int32_t acc, lp1, lp2, hz1, hp1;                    /* filters and the decimation's sum */
    uint32_t lfo1, lfo2;                                /* PRINT's wobble */
    uint16_t lw[4];                                     /* SUCK: the lines' positions */
    int32_t lp[4];
    int32_t sw_left, sw_len, sw_rp;                     /* SUCK: the swell: samples left, its length, read (Q8) */
} hn;

static inline int haunt_on(void) { return (song.g[G_HAUNT] > 0 && song.g[G_HAMT] > 0) || hn.live; }
static void haunt_clear(uint32_t mode)
{
    uint32_t i;
    for (i = 0; i < HN_BYTES / 2u; i++)
        hn_buf.b16[i] = 0;
    hn.mode = (uint8_t)mode;
    hn.w = hn.cnt = 0;
    hn.acc = hn.lp1 = hn.lp2 = hn.hz1 = hn.hp1 = 0;
    for (i = 0; i < 4u; i++)
        hn.lw[i] = 0, hn.lp[i] = 0;
    hn.sw_left = 0;
}
static void haunt_start(void)                           /* (seq.c seq_start: PLAY) */
{
    haunt_clear((uint32_t)clamp(song.g[G_HAUNT], 0, 3));
    hn.pos = hn.next = hn.step = hn.ph = 0;
}

/* 8 bits: the sign and the square root of the size (so the quiet stays), from and to 16 */
static int8_t hn_enc(int32_t x)
{
    uint32_t a = (uint32_t)(x < 0 ? -x : x), r = 0, b = 1u << 14;
    if (a > 32767u)
        a = 32767u;
    while (b > a)
        b >>= 2;
    while (b) {                                         /* integer square root, 0..181 */
        if (a >= r + b) {
            a -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    r = (r * 127u + 90u) / 181u;
    return (int8_t)(x < 0 ? -(int32_t)r : (int32_t)r);
}
static inline int32_t hn_dec(int8_t q)
{
    int32_t m = ((q < 0 ? -q : q) * 1459) >> 10, v = m * m;   /* (x 181 / 127, squared) */
    return q < 0 ? -v : v;
}
static inline int32_t hn_ring8(uint32_t base, uint32_t size, int32_t rp)   /* an 8-bit ring at rp (Q8, within one turn
                                                                            * either side), between samples */
{
    int32_t k = rp >> 8;
    uint32_t i, j;
    if (k < 0)
        k += (int32_t)size;
    else if (k >= (int32_t)size)
        k -= (int32_t)size;
    i = (uint32_t)k;
    j = i + 1u == size ? 0u : i + 1u;
    int32_t a = hn_dec(hn_buf.b8[base + i]), b = hn_dec(hn_buf.b8[base + j]);
    return a + (((b - a) * (rp & 255)) >> 8);
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
    uint32_t mode = (uint32_t)clamp(song.g[G_HAUNT], 0, 3), i;
    int32_t amt = song.g[G_HAMT] > 0 ? clamp(song.g[G_HAMT], 0, 127) : 0;
    uint32_t bpm = (uint32_t)clamp(song.g[G_BPM], 40, 240), st = (uint32_t)(FS * 15u) / bpm, bar = 16u * st;
    if (mode != hn.mode)
        haunt_clear(mode);
    if (!mode || !amt) {
        hn.live = 0;
        return;
    }
    hn.live = 1;
    if (mode == HN_PRINT) {                             /* the ghost: three beats back, dark, wobbling */
        uint32_t size = HN_BYTES, d = 3u * 60u * (FS / HN_PR_DEC) / bpm;
        int32_t g = amt * 240;                          /* (Q15 of x 0.93 at the top: as loud as the mix) */
        if (d > size - 64u)
            d = size - 64u;
        for (i = 0; i < n; i++) {
            int32_t m = (l[i] + r[i]) >> 2, y, wob;
            hn.lp1 += ((m - hn.lp1) * 4600) >> 15;      /* (two poles at about 1.1 kHz) */
            hn.lp2 += ((hn.lp1 - hn.lp2) * 4600) >> 15;
            hn.acc += hn.lp2;
            if (++hn.cnt >= HN_PR_DEC) {
                hn_buf.b8[hn.w] = hn_enc(hn.acc / (int32_t)HN_PR_DEC);
                hn.w = hn.w + 1u == size ? 0u : hn.w + 1u;
                hn.acc = 0;
                hn.cnt = 0;
            }
            hn.lfo1 += 68168u;                          /* 0.7 Hz */
            hn.lfo2 += 516150u;                         /* 5.3 Hz */
            wob = sine_i(hn.lfo1) * 7 + sine_i(hn.lfo2) * 3;   /* (Q15 x 2.75 kHz samples) */
            {
                uint32_t k = hn.w >= d ? hn.w - d : hn.w + size - d;
                y = hn_ring8(0, size, (int32_t)k * 256 + (int32_t)hn.cnt * 16 - (wob >> 7));   /* (cnt x 256 / 16) */
            }
            hn.hz1 += ((y - hn.hz1) * 700) >> 15;       /* (a high-pass at about 150 Hz) */
            y = softclip((y - hn.hz1) * 4);              /* (x 4, saturated: the grit brings back a top) */
            y = (y * g) >> 14;                          /* (x 2: the mix's level) */
            l[i] += y;
            r[i] += y;
        }
        return;
    }
    if (mode == HN_SUCK) {                              /* a reversed reverb rising into the next snare */
        int16_t *line = hn_buf.b16;
        uint32_t base = 2u * HN_SK_LINES, ring = HN_SK_RING, ahead = 4u;
        int32_t g = amt * 1400;                         /* (Q12: x 43 at the top) */
        if (ahead * st / HN_SK_DEC + 8u > ring)          /* (slow: half a beat, the ring holds 0.88 s) */
            ahead = 2u;
        for (i = 0; i < n; i++) {
            int32_t m = (l[i] + r[i]) >> 2, hp = m - hn.hp1;
            hn.hp1 += ((m - hn.hp1) * 1400) >> 15;       /* (from about 300 Hz) */
            hn.acc += hp;
            if (++hn.cnt >= HN_SK_DEC) {                /* the reverb, at 8.8 kHz */
                int32_t in = hn.acc / (int32_t)HN_SK_DEC, o[4], y = 0, h0, h1, h2, h3, mm[4];
                int16_t *ln = line;
                uint32_t j;
                hn.acc = 0;
                hn.cnt = 0;
                for (j = 0; j < 4u; j++) {
                    o[j] = ln[hn.lw[j]];
                    hn.lp[j] += ((o[j] - hn.lp[j]) * 30800) >> 15;   /* (0.94) */
                    o[j] = hn.lp[j];
                    y += o[j];
                    ln += HN_SK_D[j];
                }
                h0 = o[0] + o[1], h1 = o[0] - o[1], h2 = o[2] + o[3], h3 = o[2] - o[3];
                mm[0] = h0 + h2, mm[1] = h1 + h3, mm[2] = h0 - h2, mm[3] = h1 - h3;
                for (j = 0, ln = line; j < 4u; j++) {   /* (Hadamard x 1/2 x 0.93) */
                    ln[hn.lw[j]] = (int16_t)clamp((in >> 1) + ((mm[j] * 15237) >> 15), -32767, 32767);
                    if (++hn.lw[j] >= HN_SK_D[j])
                        hn.lw[j] = 0;
                    ln += HN_SK_D[j];
                }
                hn_buf.b8[base + hn.w] = hn_enc(clamp(y >> 1, -32767, 32767));
                hn.w = hn.w + 1u == ring ? 0u : hn.w + 1u;
            }
            if (song.playing) {                         /* the grid: a beat ahead, is there a hit? */
                if (hn.pos >= hn.next) {
                    if (!hn.sw_left && hn_hit_at(hn.step + ahead)) {
                        hn.sw_len = hn.sw_left = (int32_t)(ahead * st);
                        hn.sw_rp = (int32_t)hn.w * 256;  /* (from now, backwards) */
                    }
                    hn.next += st;
                    hn.step++;
                }
                hn.pos++;
            }
            if (hn.sw_left > 0) {
                int32_t u = hn.sw_len - hn.sw_left, e, y;   /* rising: (u / len)^3, cut 40 samples before the hit */
                uint32_t q = ((uint32_t)u << 13) / ((uint32_t)hn.sw_len >> 2 | 1u);   /* Q15 */
                e = (int32_t)((q * q) >> 15);            /* (u^2: it is heard sooner) */
                if (hn.sw_left < 40)
                    e = (e * hn.sw_left) / 40;
                hn.sw_rp -= 256 / (int32_t)HN_SK_DEC;
                if (hn.sw_rp < 0)
                    hn.sw_rp += (int32_t)ring * 256;
                y = hn_ring8(base, ring, hn.sw_rp);
                y = (((y * e) >> 15) * g) >> 11;
                l[i] += y;
                r[i] += (y * 29) >> 5;
                hn.sw_left--;
            }
        }
        return;
    }
    /* DRAG: the bar's last beat drags, the one catches up */
    {
        int16_t *b = hn_buf.b16;
        uint32_t size = HN_DR_FRAMES, maxd = (uint32_t)amt * 1880u / 127u;   /* (43 ms at the top) */
        for (i = 0; i < n; i++) {
            uint32_t ph = song.playing ? hn.ph : 0u, dl = 0;
            int32_t xl = l[i], xr = r[i];
            b[2u * hn.w] = (int16_t)clamp(xl >> 2, -32767, 32767);
            b[2u * hn.w + 1u] = (int16_t)clamp(xr >> 2, -32767, 32767);
            if (ph > bar - bar / 4u) {                  /* the last beat: the delay grows (u^2) */
                uint32_t u = ((ph - (bar - bar / 4u)) << 12) / (bar / 4u);   /* Q12 */
                dl = (maxd * ((u * u) >> 12)) >> 4;      /* Q8 samples */
            }
            if (dl) {
                uint32_t rp = (hn.w + size) * 256u - dl, k0 = rp >> 8, k1;
                k0 = k0 >= size ? k0 - size : k0;
                k1 = k0 + 1u == size ? 0u : k0 + 1u;
                int32_t f = (int32_t)(rp & 255u);
                l[i] = (b[2u * k0] + (((b[2u * k1] - b[2u * k0]) * f) >> 8)) << 2;
                r[i] = (b[2u * k0 + 1u] + (((b[2u * k1 + 1u] - b[2u * k0 + 1u]) * f) >> 8)) << 2;
            } else if (song.playing && hn.pos >= bar && ph < bar / 80u) {   /* the one: from where it was, at once */
                uint32_t k0 = hn.w >= maxd ? hn.w - maxd : hn.w + size - maxd;
                int32_t u = (int32_t)((ph << 12) / (bar / 80u));
                l[i] = (xl * u + ((b[2u * k0] << 2) * (4096 - u))) >> 12;
                r[i] = (xr * u + ((b[2u * k0 + 1u] << 2) * (4096 - u))) >> 12;
            }
            hn.w = hn.w + 1u == size ? 0u : hn.w + 1u;
            if (song.playing) {
                hn.pos++;
                if (++hn.ph >= bar)
                    hn.ph = 0;
            }
        }
    }
}
