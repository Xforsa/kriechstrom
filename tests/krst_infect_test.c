/* SPDX-License-Identifier: GPL-3.0-only */
/* KRIECHSTROM: INFECT, the INSERT INFCT (src/krst_infect.c), on the real sources:
 *   build/host/krst_infect_test [DEMO_DIR]
 * T1 a held SOFT PAD chord with INFCT listening to T2, a DRUM loop (kick, snare, hats) at LEVEL 0: the mix is the pad
 * alone, driven by drums nobody hears (a silent trigger).
 * 1. off: INFCT at MIX 0 renders bit for bit as no INSERT; with no INFCT nothing is copied.
 * 2. GATE: between the hits the pad is near silent, on them it sounds; DUCK the other way round (and stays down a
 *    while after: a sidechain's pump).
 * 3. CRUSH: on the hits the pad holds samples (few distinct values in a block), between them not.
 * 4. FOLD and RING: the pad changes, stays bounded; RING is silent where the source is; VOX: the pad speaks only with
 *    the drums (the vocoder: their spectrum opens its bands).
 * 5. the page: INSRT lists INFCT, SRC T1..T4, MODE GATE..RING, DEPTH.
 * 6. demos (DEMO_DIR): the drums heard, the pad two bars dry, two bars infected, one per MODE. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int bad;
static void ck(const char *what, int ok)
{
    printf("infect: %-82s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static int32_t out_buf[2 * CTL];
static uint64_t hash;
static double blk_rms;
static uint32_t blk_distinct;
static void mixb(void)
{
    uint32_t i, k;
    double s = 0;
    mix_block(out_buf, CTL);
    for (i = 0; i < 2u * CTL; i++) {
        s += (double)out_buf[i] * out_buf[i];
        for (k = 0; k < 4u; k++) {
            hash ^= ((uint32_t)out_buf[i] >> (8u * k)) & 0xFFu;
            hash *= 0x100000001B3ull;
        }
    }
    blk_rms = sqrt(s / (2.0 * CTL));
    for (blk_distinct = 0, i = 0; i < CTL; i++)
        blk_distinct += !i || out_buf[2u * i] != out_buf[2u * (i - 1u)];
}

/* T1 SOFT PAD, one chord held over the bar (ties); T2 DRUM: kick on the beats, snare on 2 and 4, hats between */
static void loop_fresh(int drums_heard)
{
    static const uint8_t CH[3] = {48, 55, 63};
    uint32_t k, i;
    memset(dly_buf, 0, sizeof dly_buf);
    memset(cho_buf, 0, sizeof cho_buf);
    memset(rev_comb, 0, sizeof rev_comb);
    memset(rev_ap, 0, sizeof rev_ap);
    memset(&fx, 0, sizeof fx);
    memset(&pf, 0, sizeof pf);
    memset(ins, 0, sizeof ins);
    memset(ifx_buf, 0, sizeof ifx_buf);
    memset(ifx_pk, 0, sizeof ifx_pk);
    for (k = 0; k < NTRK; k++)
        pf.mg[k] = 32768;
    perf_held = perf_latched = 0;
    lim_env = LIM_T;
    dc_l = dc_r = dce_l = dce_r = 0;
    vage = 0;
    rng_state = 0x1234567u;
    mod_seed = 0x2545F491u;
    song.playing = 0;
    memset(trk, 0, sizeof trk);
    MOD_W_CLEAR();
    memset(sl, 0, sizeof sl);
    memset(&motion, 0, sizeof motion);
    host_tracks_init();
    for (k = 0; k < NPART; k++)
        host_preset(&trk[k], 0, 0);
    host_preset(&trk[0], 0, 1);                        /* SOFT PAD */
    host_preset(&trk[1], ENGI_DRUM, 0);
    song.g[G_DMG] = song.g[G_WALL] = song.g[G_HISS] = 0;
    for (i = 0; i < 16u; i++) {
        put_step(&trk[0], i, 3, CH, i ? ST_TIE : ST_NOTE, 0);
        put_step(&trk[1], i, 0, 0, ST_NOTE, 0);
        trk[1].step[i].hit = (uint8_t)((i % 4u == 0u ? 1u : 0u) | (i % 8u == 4u ? 2u : 0u) | (i % 4u == 2u ? 8u : 0u));
        trk[1].step[i].vel = 110;
    }
    trk[0].p[P_SLEN] = trk[1].p[P_SLEN] = 16;
    trk[0].seq_active = trk[1].seq_active = 1;
    trk[0].p[P_REV] = trk[1].p[P_REV] = 0;
    trk[0].p[P_DLY] = trk[0].p[P_CHOR] = 0;
    trk[1].p[P_LEVEL] = drums_heard ? trk[1].p[P_LEVEL] : 0;   /* (a silent trigger: the mix is the pad) */
    hash = 0xCBF29CE484222325ull;
}
static void set_infect(int32_t mode, int32_t mix)
{
    trk[0].p[P_ITYPE] = IT_INFECT;
    trk[0].p[P_IA] = 1;                                /* SRC T2 */
    trk[0].p[P_IB] = mode;
    trk[0].p[P_IC] = 127;
    trk[0].p[P_IMIX] = mix;
}

#define BAR (4u * FS / 2u / CTL)                        /* 120 BPM: a bar 2 s, in blocks */
#define NB (2u * BAR)
typedef struct { double on, off; uint32_t dist_on, dist_off; uint64_t h; int32_t peak; } meas_t;
/* bars 2..3 of a run: the pad's RMS on the hits (blocks within ~12 ms after a kick or snare step starts) and between
 * them (blocks 120 .. 250 ms after one), its distinct values there; mode < 0: no INSERT */
static meas_t run(int32_t mode, int32_t mix)
{
    meas_t m = {0, 0, 0, 0, 0, 0};
    uint32_t k, non = 0, noff = 0, step_blocks = BAR / 16u;
    loop_fresh(0);
    if (mode >= 0)
        set_infect(mode, mix);
    seq_start();
    for (k = 0; k < BAR; k++)
        mixb();
    for (k = 0; k < NB; k++) {
        uint32_t i, pos = k % (4u * step_blocks);       /* (blocks into the beat: a kick, or the snare, starts each) */
        mixb();
        for (i = 0; i < 2u * CTL; i++) {
            int32_t a = out_buf[i] < 0 ? -out_buf[i] : out_buf[i];
            if (a > m.peak)
                m.peak = a;
        }
        if (pos >= 1u && pos < 1u + step_blocks / 8u) {
            m.on += blk_rms * blk_rms;
            m.dist_on += blk_distinct;
            non++;
        } else if (pos >= step_blocks + step_blocks / 2u && pos < 2u * step_blocks) {   /* (after the kick, before the hat) */
            m.off += blk_rms * blk_rms;
            m.dist_off += blk_distinct;
            noff++;
        }
    }
    seq_stop();
    m.on = sqrt(m.on / (non ? non : 1u));
    m.off = sqrt(m.off / (noff ? noff : 1u));
    m.dist_on /= non ? non : 1u;
    m.dist_off /= noff ? noff : 1u;
    m.h = hash;
    return m;
}

static void tests(void)
{
    meas_t dry = run(-1, 0), z = run(IFX_GATE, 0), g = run(IFX_GATE, 127), d = run(IFX_DUCK, 127), c = run(IFX_CRUSH, 127);
    meas_t f = run(IFX_FOLD, 127), r = run(IFX_RING, 127), v = run(IFX_VOX, 127);
    printf("infect:   pad rms on the hits / between: dry %.0f / %.0f | GATE %.0f / %.0f | DUCK %.0f / %.0f | RING %.0f / %.0f\n",
           dry.on, dry.off, g.on, g.off, d.on, d.off, r.on, r.off);
    printf("infect:   distinct values a block on the hits / between: dry %u / %u | CRUSH %u / %u; peaks: FOLD %d RING %d\n",
           (unsigned)dry.dist_on, (unsigned)dry.dist_off, (unsigned)c.dist_on, (unsigned)c.dist_off, (int)f.peak, (int)r.peak);
    ck("off: INFCT at MIX 0 renders bit for bit as no INSERT", z.h == dry.h);
    loop_fresh(0);
    ifx_begin();
    ck("off: with no INFCT no track is listened to (nothing copied)", ifx_listen == 0);
    ck("GATE: the pad near silent between the hits (under a tenth of dry), sounding on them (over a third)",
       g.off < dry.off * 0.1 && g.on > dry.on * 0.33);
    ck("DUCK: the pad down on the hits (under half of dry), back between them (over 0.8)",
       d.on < dry.on * 0.5 && d.off > dry.off * 0.8);
    {   /* CRUSH on a block of its own (the master's DC blocker makes every sample of the mix differ): a sine, the
         * source hitting (its peak 30000) and quiet */
        static ins_t st;
        track_t *t = &trk[0];
        int32_t blk[CTL];
        uint32_t i, rep, hit = 0, quiet = 0;
        loop_fresh(0);
        set_infect(IFX_CRUSH, 127);
        for (rep = 0; rep < 2u; rep++) {
            memset(&st, 0, sizeof st);
            st.type = IT_INFECT;
            st.w = 32768;
            ifx_pk[ifx_half ^ 1u][1] = rep ? 0 : 30000;
            for (i = 0; i < CTL; i++)
                blk[i] = (int32_t)(20000.0 * sin(i * 0.3));
            ins_infect(&st, t, blk, CTL, 32768);
            for (i = 1; i < CTL; i++)
                *(rep ? &quiet : &hit) += blk[i] != blk[i - 1u];
        }
        printf("infect:   CRUSH on a sine block: %u of %u samples change with the source hitting, %u quiet\n",
               (unsigned)hit, (unsigned)CTL - 1u, (unsigned)quiet);
        ck("CRUSH: the source hitting holds the samples (a few changes a block), quiet it passes them all",
           hit <= 4u && quiet == CTL - 1u);
    }
    (void)c;
    ck("FOLD: changed on the hits, bounded", f.h != dry.h && f.peak < 32768);
    printf("infect:   VOX: pad rms on the hits / between %.0f / %.0f, peak %d\n", v.on, v.off, (int)v.peak);
    ck("VOX: the pad speaks with the drums: near silent between the hits, sounding on them, bounded",
       v.off < dry.off * 0.15 && v.on > dry.on * 0.05 && v.peak < 32768 && v.h != dry.h);
    ck("RING: silent where the source is (between the hits), sounding on them, bounded",
       r.off < dry.off * 0.1 && r.on > dry.on * 0.1 && r.peak < 32768);
    {
        int ok = str_eq(N_ITYPE[IT_INFECT], "INFCT") && str_eq(ins_desc(IT_INFECT, 0)->label, "SRC") &&
                 str_eq(ins_desc(IT_INFECT, 1)->label, "MODE") && str_eq(ins_desc(IT_INFECT, 2)->label, "DEPTH") &&
                 ins_desc(IT_INFECT, 0)->max == 3 && ins_desc(IT_INFECT, 1)->max == 5 &&
                 str_eq(N_IMODE[IFX_GATE], "GATE") && str_eq(N_IMODE[IFX_RING], "RING") && str_eq(N_IMODE[IFX_VOX], "VOX");
        ck("the page: INSRT INFCT, SRC T1..T4, MODE GATE DUCK CRUSH FOLD RING VOX, DEPTH", ok);
    }
}

static int demos(const char *dir)
{
    static const char *const NAME[] = {"infect_gate", "infect_duck", "infect_crush", "infect_fold", "infect_ring"};
    uint32_t d, k, i, frames = 4u * BAR * CTL, n = 0;
    for (d = 0; d < NELEM(NAME); d++) {
        char path[512];
        FILE *f;
        snprintf(path, sizeof path, "%s/%s.wav", dir, NAME[d]);
        if (!(f = fopen(path, "wb")))
            continue;
        wav_hdr(f, frames);
        loop_fresh(1);
        seq_start();
        for (k = 0; k < 4u * BAR; k++) {
            if (k == 2u * BAR)
                set_infect((int32_t)d, 127);
            mixb();
            for (i = 0; i < CTL; i++)
                wav_put(f, out_buf[2u * i], out_buf[2u * i + 1u]);
        }
        seq_stop();
        fclose(f);
        n++;
    }
    return (int)n;
}

int main(int argc, char **argv)
{
    tests();
    if (argc > 1)
        printf("infect: %d demos in %s (two bars dry, two infected)\n", demos(argv[1]), argv[1]);
    if (bad) {
        printf("INFECT TEST FAILED (%d)\n", bad);
        return 1;
    }
    printf("infect: all ok\n");
    return 0;
}
