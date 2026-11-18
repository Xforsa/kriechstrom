/* SPDX-License-Identifier: GPL-3.0-only */
/* KRIECHSTROM: its own drum kits (src/krst_kits.c, KIT ROOM GATE MARCH ARC ROT SLAB), on the real sources:
 *   build/host/krst_kits_test [--print]
 * 1. Each lane's hit holds its design: its energy in 6 bands (60 Hz .. 9 kHz) in 3 windows (the attack 0-20 ms,
 *    the body 20-100 ms, the tail 100-300 ms) within 1.5 dB (rms) of the profile below, taken when the firmware's
 *    hits were checked against the prototypes the kits were chosen from by ear (--print prints it again).
 * 2. Its peak at its level (LVL x -7 dBFS) within 1 dB; it ends at its length, from silence to silence.
 * 3. TUNE, DECY, TONE at their ends: every hit bounded, and it ends.
 * 4. Through the engine and the sequencer, a beat per kit: it sounds, nothing clips, the part's room rings out to
 *    silence after the last hit; the closed hat chokes the open one; KIT moved while a hit rings. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int bad;
static void ck(const char *what, int ok)
{
    printf("kits: %-88s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* --------------------------------------------------------------- the hits, straight from kv_run --- */
#define HMAX (44100u * 2u)
static int32_t hbuf[HMAX];
static uint32_t hit(uint32_t kk, uint32_t lane, int32_t tune, int32_t decay, int32_t tone, int32_t *peak, uint32_t *end)
{
    static int16_t comb[KB_BUF];
    static int32_t y[CTL];
    kv_coef_t c;
    kv_voice_t v;
    const kv_lane_def_t *d = &KV_KIT[kk][lane];
    uint32_t n = 0, i;
    int16_t *cb = KV_BUS[kk].room == 2u && d->comb ? (d->comb == 1u ? comb : comb + KB_GKN) : 0;
    memset(&v, 0, sizeof v);
    memset(comb, 0, sizeof comb);
    kv_setup(&c, d, tune, decay, tone);
    v.trig = 1;
    *peak = 0;
    *end = 0;
    while (n + CTL <= HMAX) {
        kv_run(&c, &v, y, CTL, cb);
        for (i = 0; i < CTL; i++, n++) {
            int32_t a = y[i] < 0 ? -y[i] : y[i];
            hbuf[n] = y[i];
            if (a > *peak)
                *peak = a;
            if (a)
                *end = n + 1u;
        }
        if (!v.live && n > c.len + CTL)
            break;
    }
    if (KV_BUS[kk].sampler) {                            /* (GATE: the bus's sampler, as the prototypes had it on
                                                          * every hit) */
        uint32_t acc = 0;
        int32_t h = 0;
        for (i = 0; i < n; i++) {
            if ((acc += 38700u) >= 65536u) {
                acc -= 65536u;
                h = ((hbuf[i] + 8) >> 4) << 4;
            }
            hbuf[i] = h;
        }
    }
    return n;
}
/* the profile: dB of the hit's energy, per window and band */
typedef struct { float ic1, ic2; } fsvf_t;
static float fbp(fsvf_t *s, float x, float f, float q)
{
    float g = tanf(3.14159265f * f / FS), k = 1 / q, a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2;
    float v3 = x - s->ic2, v1 = a1 * s->ic1 + a2 * v3, v2 = s->ic2 + a2 * s->ic1 + a3 * v3;
    s->ic1 = 2 * v1 - s->ic1;
    s->ic2 = 2 * v2 - s->ic2;
    return v1 * k;
}
static void profile(uint32_t n, float *p)
{
    static const float BF[6] = {60, 150, 400, 1200, 3500, 9000};
    static const uint32_t WE[4] = {0, 882, 4410, 13230};
    fsvf_t b[6];
    double e[18], tot = 1e-12;
    uint32_t i, k, w = 0;
    memset(b, 0, sizeof b);
    for (k = 0; k < 18u; k++)
        e[k] = 1e-9;
    for (i = 0; i < WE[3]; i++) {
        float x = i < n ? hbuf[i] / 32768.0f : 0;
        while (w < 2u && i >= WE[w + 1u])
            w++;
        for (k = 0; k < 6u; k++) {
            float y = fbp(&b[k], x, BF[k], 1.2f);
            e[w * 6u + k] += y * y;
            tot += y * y;
        }
    }
    for (k = 0; k < 18u; k++)
        p[k] = fmaxf(-40.0f, 10.0f * log10f((float)(e[k] / tot)));
}
/* the profiles at the design (all knobs 64), dB; lanes KICK SNARE CLAP HAT CL HAT OP TOM RIM BELL */
static const int8_t PROF[KK_COUNT][DV_NLANE][18] = {
#include "krst_kits_prof.h"
};
static const float LVL_ROOM[8] = {1.0f, 0.85f, 0.55f, 0.28f, 0.3f, 0.7f, 0.4f, 0.35f};
static const float LVL[8] = {1.0f, 0.8f, 0.55f, 0.22f, 0.3f, 0.6f, 0.35f, 0.4f};

static void designs(int print)
{
    uint32_t kk, l, k, worst_l = 0, worst_k = 0, ends = 1, lvl = 1;
    float worst = 0;
    for (kk = 0; kk < KK_COUNT; kk++) {
        if (print)
            printf("    {\n");
        for (l = 0; l < DV_NLANE; l++) {
            int32_t pk;
            uint32_t end, n = hit(kk, l, 0, 64, 64, &pk, &end), len;
            float p[18], d = 0, want = (kk == KK_ROOM ? LVL_ROOM[l] : LVL[l]) * 14746.0f;
            kv_coef_t c;
            kv_setup(&c, &KV_KIT[kk][l], 0, 64, 64);
            len = c.len;
            profile(n, p);
            if (print) {
                printf("        {");
                for (k = 0; k < 18u; k++)
                    printf("%d%s", (int)lrintf(p[k]), k < 17u ? ", " : "},\n");
                continue;
            }
            for (k = 0; k < 18u; k++)
                d += (p[k] - PROF[kk][l][k]) * (p[k] - PROF[kk][l][k]);
            d = sqrtf(d / 18.0f);
            if (d > worst)
                worst = d, worst_k = kk, worst_l = l;
            ends &= end <= len && end > 0u;              /* (silent at its length) */
            lvl &= fabsf(20.0f * log10f(pk / want)) < 1.0f;
            if (fabsf(20.0f * log10f(pk / want)) >= 1.0f || end > len)
                printf("kits:   kit %u lane %u: peak %d (want %.0f), end %u of %u\n", kk, l, pk, want, end, len);
        }
        if (print)
            printf("    },\n");
    }
    if (print)
        return;
    printf("kits:   the farthest from its profile: kit %u lane %u, %.2f dB\n", worst_k, worst_l, worst);
    ck("every lane of the six kits holds its design (6 bands x 3 windows, within 1.5 dB)", worst < 1.5f);
    ck("  each at its level (LVL x -7 dBFS, within 1 dB)", lvl);
    ck("  each silent at its length", ends);
}

static void knobs(void)
{
    static const int32_t TU[3] = {-12 * 16, 0, 12 * 16}, KN[3] = {0, 64, 127};
    uint32_t kk, l, a, b, c, ok = 1;
    for (kk = 0; kk < KK_COUNT; kk++)
        for (l = 0; l < DV_NLANE; l++)
            for (a = 0; a < 3u; a++)
                for (b = 0; b < 3u; b++)
                    for (c = 0; c < 3u; c++) {
                        int32_t pk;
                        uint32_t end, n = hit(kk, l, TU[a], KN[b], KN[c], &pk, &end);
                        kv_coef_t cf;
                        kv_setup(&cf, &KV_KIT[kk][l], TU[a], KN[b], KN[c]);
                        if (pk > 32767 || end > cf.len || n >= HMAX) {
                            ok = 0;
                            printf("kits:   kit %u lane %u TUNE %d DECY %d TONE %d: peak %d end %u len %u\n", kk, l,
                                   TU[a], KN[b], KN[c], pk, end, cf.len);
                        }
                    }
    ck("TUNE (-12 .. +12 st), DECY, TONE at their ends and middles: every hit bounded, it ends", ok);
}

/* ------------------------------------------------------------------- through the engine --- */
static uint32_t drum_engine(void)
{
    uint32_t e;
    for (e = 0; e < NENGINES; e++)
        if (ENGINES[e] == &ENG_DRUM)
            return e;
    return 0;
}
static void beat(uint32_t kit, uint32_t lanes_each_beat, uint32_t extra_step, uint32_t extra_lanes)
{
    track_t *t = &trk[3];
    uint32_t k, s;
    for (k = 0; k < NPART; k++) {
        memset(trk[k].step, 0, sizeof trk[k].step);
        trk[k].p[P_SLEN] = 16;
        trk[k].seq_active = 1;
        trk[k].p[P_LEVEL] = 0;
    }
    host_preset(t, drum_engine(), 13u + kit);
    t->p[P_LEVEL] = 100;
    for (s = 0; s < 16u; s++)
        if (lanes_each_beat && !(s & 3u)) {
            put_step(t, s, 0, 0, ST_NOTE, 0);
            t->step[s].hit = (uint8_t)lanes_each_beat;
            t->step[s].vel = 127;
        }
    if (extra_lanes) {
        put_step(t, extra_step, 0, 0, ST_NOTE, 0);
        t->step[extra_step].hit |= (uint8_t)extra_lanes;
        t->step[extra_step].vel = 127;
    }
}
/* run the beat bars, then stop and listen on for tail seconds: the peak while playing, the last sound (s) after */
static void engine_run(uint32_t bars, float tail, int32_t *peak, float *last, int32_t *tail_peak)
{
    static int32_t out[2 * CTL];
    uint32_t k, i, nb = bars * 4u * 60u * FS / 120u / CTL, nt = (uint32_t)(tail * FS) / CTL;
    *peak = 0;
    *last = 0;
    *tail_peak = 0;
    song.g[G_BPM] = 120;
    seq_start();
    for (k = 0; k < nb; k++) {
        mix_block(out, CTL);
        for (i = 0; i < 2u * CTL; i++)
            *peak = abs(out[i]) > *peak ? abs(out[i]) : *peak;
    }
    seq_stop();
    for (k = 0; k < nt; k++) {
        mix_block(out, CTL);
        for (i = 0; i < 2u * CTL; i++)
            if (abs(out[i]) > 8) {
                *last = (float)(k * CTL) / FS;
                *tail_peak = abs(out[i]) > *tail_peak ? abs(out[i]) : *tail_peak;
            }
    }
}
static void engine(void)
{
    uint32_t kk, ok_sound = 1, ok_tail = 1;
    for (kk = 0; kk < KK_COUNT; kk++) {
        int32_t pk, tp;
        float last;
        ui_power_on();
        song.master_q12 = 3200;
        beat(kk, 0xFFu, 2u, 0u);                         /* every lane on every beat: the loudest it gets */
        engine_run(2, 3.0f, &pk, &last, &tp);
        printf("kits:   %s: peak %d, the last sound %.2f s after the stop\n", ENG_DRUM.edit[0].names[13u + kk], pk, last);
        ok_sound &= pk > 3000 && pk < 32767;
        ok_tail &= last < 2.0f;
    }
    ck("a beat per kit through the engine and the sequencer: it sounds, nothing clips", ok_sound);
    ck("  the part's room and the voices ring out to silence (under 2 s after the stop)", ok_tail);

    {   /* the choke: an open hat, a closed one 2 steps later; the open one gone at once */
        kv_voice_t *ov;
        drum_lane_t *K;
        int32_t pk, tp;
        float last;
        uint32_t live_after = 0, k;
        static int32_t out[2 * CTL];
        ui_power_on();
        beat(KK_ROT, 0u, 0u, 1u << DV_HATO);
        put_step(&trk[3], 2, 0, 0, ST_NOTE, 0);
        trk[3].step[2].hit = 1u << DV_HATC;
        trk[3].step[2].vel = 127;
        song.g[G_BPM] = 120;
        seq_start();
        K = drum_kit_of(&trk[3]);
        ov = &K[DV_HATO].kv;
        for (k = 0; k < (FS / 8u * 2u + FS / 50u) / CTL; k++)   /* 2 sixteenths and 20 ms */
            mix_block(out, CTL);
        live_after = ov->live;
        seq_stop();
        (void)pk, (void)tp, (void)last;
        ck("the closed hat chokes the open one (gone within 20 ms)", !live_after);
    }
    {   /* KIT moved while a hit rings: from ROOM to STD and back, nothing stuck, nothing out of bounds */
        int32_t pk, tp;
        float last;
        static int32_t out[2 * CTL];
        uint32_t k, i, peak = 0;
        ui_power_on();
        beat(KK_ROOM, 0x23u, 0u, 0u);
        song.g[G_BPM] = 120;
        seq_start();
        for (k = 0; k < 600u; k++) {
            trk[3].p[P_E0] = (int16_t)(k % 200u < 100u ? DK_ROOM : (k % 400u < 200u ? DK_STD : DK_SLAB));
            mix_block(out, CTL);
            for (i = 0; i < 2u * CTL; i++)
                peak = (uint32_t)abs(out[i]) > peak ? (uint32_t)abs(out[i]) : peak;
        }
        seq_stop();
        trk[3].p[P_E0] = DK_STD;
        engine_run(0, 3.0f, &pk, &last, &tp);
        ck("KIT moved while hits ring (ROOM, STD, SLAB): bounded, and it falls silent", peak < 32767u && last < 2.0f);
    }
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--print")) {
        designs(1);
        return 0;
    }
    designs(0);
    knobs();
    engine();
    printf("kits: %s\n", bad ? "FAIL" : "all ok");
    return bad != 0;
}
