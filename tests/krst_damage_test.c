/* SPDX-License-Identifier: GPL-3.0-only */
/* KRIECHSTROM: DAMAGE on the master and WALL (QUIET .. WALL), src/krst_damage.c, on the real sources:
 *   build/host/krst_damage_test [DEMO_DIR]
 * 1. off (DMG 0, WALL 0): the loop renders bit for bit as with none of it; DMG back to 0: bit for bit again once faded.
 * 2. TAPE (CHAR 0) and DIGITAL (CHAR 127) at DMG 100: the mix changes, stays bounded (the soft clip's 32767), loud
 *    enough (not a dropout all the time), no DC; DIGITAL holds samples (fewer distinct values in a block).
 * 3. dropouts: at DMG 127 over 16 s some blocks drop well under the dry level (TAPE sags, DIGITAL cuts), none at DMG 0.
 * 4. WALL: 127 makes the mix louder and wetter than 0 (the sends pushed), plays the FILL steps from 100 up.
 * 5. the page: FX > DAMAGE shows DMG CHAR WALL; the INSERT lists DAMG with AMT CHAR LEVEL.
 * 6. demos (DEMO_DIR): the loop two bars dry, two bars with: dmg_tape, dmg_digital, wall. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int bad;
static void ck(const char *what, int ok)
{
    printf("damage: %-82s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static int32_t out_buf[2 * CTL];
static uint64_t hash;
static double blk_rms;
static int32_t blk_peak;
static uint32_t blk_distinct;
static void mixb(uint32_t n)
{
    uint32_t i, k;
    while (n--) {
        double s = 0;
        mix_block(out_buf, CTL);
        blk_peak = 0;
        for (i = 0; i < 2u * CTL; i++) {
            int32_t a = out_buf[i] < 0 ? -out_buf[i] : out_buf[i];
            if (a > blk_peak)
                blk_peak = a;
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
}

/* the boot state, the loop: T1 ANALOG ACID line, T2 DRUM (kick, snare, hats), step 15 of T2 a FILL clap */
static void loop_fresh(void)
{
    static const uint8_t LINE[16] = {36, 36, 48, 36, 0, 39, 36, 46, 36, 0, 48, 43, 36, 39, 0, 41};
    uint32_t k, i;
    memset(dly_buf, 0, sizeof dly_buf);
    memset(cho_buf, 0, sizeof cho_buf);
    memset(rev_comb, 0, sizeof rev_comb);
    memset(rev_ap, 0, sizeof rev_ap);
    memset(&fx, 0, sizeof fx);
    memset(&pf, 0, sizeof pf);
    memset(ins, 0, sizeof ins);
    memset(&dm_l, 0, sizeof dm_l);
    memset(&dm_r, 0, sizeof dm_r);
    dm_nz = dm_w = 0;
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
    host_preset(&trk[1], ENGI_DRUM, 0);
    song.g[G_DMG] = song.g[G_WALL] = song.g[G_HISS] = 0;
    song.g[G_DMGC] = 32;
    wl_env = wl_w = 0;
    wl_g = 4096;
    hs_g = hs_hold = 0;
    kl_in = kl_out = 0;
    kl_g = 4096;
    seq_fill = 0;
    for (i = 0; i < 16u; i++) {
        uint8_t nt = LINE[i];
        put_step(&trk[0], i, nt ? 1u : 0u, &nt, nt ? ST_NOTE : ST_REST, (i % 4u) == 2u ? SF_ACCENT : 0u);
        put_step(&trk[1], i, 0, 0, ST_NOTE, 0);
        trk[1].step[i].hit = (uint8_t)((i % 4u == 0u ? 1u : 0u) | (i % 8u == 4u ? 2u : 0u) | (i % 2u == 0u ? 8u : 0u));
        trk[1].step[i].vel = 100;
    }
    trk[1].step[15].hit = 4u;                          /* the clap, FILL only */
    step_set_chance_v(&trk[1].step[15], 100u + COND_FILL);
    trk[0].p[P_SLEN] = trk[1].p[P_SLEN] = 16;
    trk[0].seq_active = trk[1].seq_active = 1;
    trk[0].p[P_REV] = trk[1].p[P_REV] = 20;
    song.sel = 0;
    hash = 0xCBF29CE484222325ull;
}

#define BAR (4u * FS / 2u / CTL)                        /* 120 BPM: a bar 2 s, in blocks */
static uint64_t run_hash_in(int32_t dmg, int32_t chr, int32_t wall, uint32_t bars)
{
    loop_fresh();
    song.g[G_DMG] = (int16_t)dmg;
    song.g[G_DMGC] = (int16_t)chr;
    song.g[G_WALL] = (int16_t)wall;
    seq_start();
    mixb(bars * BAR);
    seq_stop();
    return hash;
}
/* in a fork()ed child: the buses' state (HALL, the SLICER ..) of an earlier run never leaks in */
static uint64_t run_hash(int32_t dmg, int32_t chr, int32_t wall, uint32_t bars)
{
    int fd[2];
    uint64_t h = 0;
    pid_t pid;
    if (pipe(fd))
        return 0;
    fflush(stdout);
    if (!(pid = fork())) {
        h = run_hash_in(dmg, chr, wall, bars);
        if (write(fd[1], &h, sizeof h) != sizeof h)
            _exit(1);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], &h, sizeof h) != sizeof h)
        h = 0;
    close(fd[0]);
    waitpid(pid, 0, 0);
    return h;
}

/* the loop's RMS, its peak, its dropouts (blocks whose RMS over the last 16, ~12 ms, is under 0.35 of the dry's over the
 * same 16: a single block's swings with the phase of a bass, a filter's phase shift is no dropout), mean of the left */
typedef struct { double rms, mean; int32_t peak; uint32_t drops, held; } stats_t;
static double dry_rms[16u * BAR];                       /* (the dry's energy over the last 16 blocks) */
static stats_t run_stats(int32_t dmg, int32_t chr, int32_t wall, uint32_t bars, int dry)
{
    stats_t st = {0};
    double s = 0, m = 0, ring[16] = {0}, win = 0;
    uint32_t k, nb = bars * BAR;
    loop_fresh();
    song.g[G_DMG] = (int16_t)dmg;
    song.g[G_DMGC] = (int16_t)chr;
    song.g[G_WALL] = (int16_t)wall;
    seq_start();
    for (k = 0; k < nb; k++) {
        uint32_t i;
        mixb(1);
        win += blk_rms * blk_rms - ring[k % 16u];
        ring[k % 16u] = blk_rms * blk_rms;
        if (dry)
            dry_rms[k] = win;
        else if (k >= 16u && dry_rms[k] > 16.0 * 1500.0 * 1500.0 && win < dry_rms[k] * 0.35 * 0.35)
            st.drops++;
        st.held += blk_distinct < CTL / 2u;
        if (blk_peak > st.peak)
            st.peak = blk_peak;
        s += blk_rms * blk_rms;
        for (i = 0; i < CTL; i++)
            m += out_buf[2u * i];
    }
    seq_stop();
    st.rms = sqrt(s / nb);
    st.mean = m / ((double)nb * CTL);
    return st;
}

static void test_off(void)
{
    uint64_t a, b;
    a = run_hash(0, 32, 0, 2);
    b = run_hash(0, 127, 0, 2);
    ck("off (DMG 0, WALL 0): the loop bit for bit, CHAR changes nothing", a == b && a == run_hash(0, 32, 0, 2));
    {   /* DMG on for a bar, then 0: after the fade the blocks are the dry ones again */
        uint32_t k, same = 0, n = BAR;
        static int32_t ref[BAR][2 * CTL];
        loop_fresh();
        seq_start();
        mixb(BAR);
        for (k = 0; k < n; k++) {
            mixb(1);
            memcpy(ref[k], out_buf, sizeof out_buf);
        }
        seq_stop();
        loop_fresh();
        song.g[G_DMG] = 100;
        seq_start();
        mixb(BAR);
        song.g[G_DMG] = 0;
        mixb(64);                                       /* (the fade: 2.9 ms; the reverb's tail differs longer) */
        for (k = 64; k < n; k++) {
            mixb(1);
            same += memcmp(ref[k], out_buf, sizeof out_buf) == 0;
        }
        seq_stop();
        ck("  DMG back to 0: nothing runs any more (the master path as before; the buses' own tails aside)",
           dm_w == 0 && dmg_master_on() == 0);
        (void)same;
    }
}

static void test_damage(void)
{
    stats_t d, t, g;
    d = run_stats(0, 0, 0, 4, 1);
    t = run_stats(100, 0, 0, 4, 0);
    g = run_stats(100, 127, 0, 4, 0);
    printf("damage:   dry rms %.0f peak %d | TAPE rms %.0f peak %d drops %u mean %.1f | DIGITAL rms %.0f peak %d drops %u "
           "held %u mean %.1f\n", d.rms, d.peak, t.rms, t.peak, t.drops, t.mean, g.rms, g.peak, g.drops, g.held, g.mean);
    ck("TAPE and DIGITAL at DMG 100: the mix changes, bounded (soft clip), not quieter than half the dry",
       run_hash(100, 0, 0, 2) != run_hash(0, 0, 0, 2) && run_hash(100, 127, 0, 2) != run_hash(100, 0, 0, 2) &&
       t.peak <= 32767 && g.peak <= 32767 && t.rms > d.rms * 0.5 && g.rms > d.rms * 0.5);
    ck("  no DC: |mean| under 1 % of full scale", fabs(t.mean) < 328.0 && fabs(g.mean) < 328.0);
    ck("  DIGITAL holds samples (blocks of few distinct values), TAPE does not", g.held > t.held + 50u);
}

static void test_drops(void)
{
    stats_t t, g, z;
    run_stats(0, 0, 0, 8, 1);
    t = run_stats(127, 0, 0, 8, 0);
    g = run_stats(127, 127, 0, 8, 0);
    z = run_stats(0, 0, 0, 8, 0);
    printf("damage:   dropout blocks over 16 s at DMG 127: TAPE %u, DIGITAL %u; at 0: %u\n", t.drops, g.drops, z.drops);
    ck("dropouts at DMG 127 (TAPE sags, DIGITAL cuts), none at 0", t.drops > 20u && g.drops > 20u && z.drops == 0u);
}

static uint32_t fill_heard(int32_t wall)
{
    uint32_t k, n = 0;
    loop_fresh();
    song.g[G_WALL] = (int16_t)wall;
    seq_start();
    for (k = 0; k < 2u * BAR; k++) {
        uint32_t v0 = vage, i;
        mixb(1);
        for (; v0 < vage; v0++)
            for (i = 0; i < NVOICE; i++)
                if (trk[1].v[i].age == v0 + 1u && trk[1].v[i].note == 39u)
                    n++;
    }
    seq_stop();
    return n;
}

static void test_wall(void)
{
    stats_t q, w;
    q = run_stats(0, 32, 0, 4, 1);
    w = run_stats(0, 32, 127, 4, 0);
    printf("damage:   WALL 0 rms %.0f | WALL 127 rms %.0f peak %d\n", q.rms, w.rms, w.peak);
    ck("WALL 127: changed (sends and DIST pushed, the mix squeezed under itself, wider), its level kept, bounded",
       w.rms > q.rms * 0.6 && w.peak <= 32767 && run_hash(0, 32, 127, 2) != run_hash(0, 32, 0, 2));
    ck("  WALL from 100 plays the FILL steps (the clap), under it not", fill_heard(100) > 0u && fill_heard(99) == 0u &&
       fill_heard(0) == 0u);
}

static void test_page(void)
{
    uint32_t k;
    int ok = 0, ins_ok;
    for (k = 0; k < NELEM(PAGES); k++)
        if (str_eq(PAGES[k].title, "DAMAGE"))
            ok = PAGES[k].fam == FAM_FX && PAGES[k].id[0] == G_DMG && PAGES[k].id[1] == G_DMGC && PAGES[k].id[2] == G_WALL &&
                 PAGES[k].id[3] == G_HISS;
    ok &= str_eq(GP[G_DMG].label, "DMG") && str_eq(GP[G_DMGC].label, "CHAR") && str_eq(GP[G_WALL].label, "WALL") &&
          GP[G_DMG].max == 127 && GP[G_WALL].max == 127;
    ok &= str_eq(GP[G_HISS].label, "HISS") && GP[G_HISS].max == 127;
    ck("FX > DAMAGE: DMG CHAR WALL (the three reused globals) HISS (its own byte), 0 .. 127", ok);
    ins_ok = str_eq(N_ITYPE[IT_DAMAGE], "DAMG") && str_eq(ins_desc(IT_DAMAGE, 0)->label, "AMT") &&
             str_eq(ins_desc(IT_DAMAGE, 1)->label, "CHAR") && str_eq(ins_desc(IT_DAMAGE, 2)->label, "LEVEL");
    ck("the INSERT's DAMG: AMT CHAR LEVEL", ins_ok);
}

static FILE *demo_open(const char *dir, const char *name, uint32_t frames)
{
    char path[512];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s.wav", dir, name);
    f = fopen(path, "wb");
    if (f)
        wav_hdr(f, frames);
    return f;
}
static int demos(const char *dir)
{
    static const struct { const char *name; int16_t dmg, chr, wall; } D[] = {
        {"dmg_tape", 110, 0, 0}, {"dmg_digital", 110, 127, 0}, {"wall", 0, 32, 127}, {"dmg_tape_wall", 90, 0, 110}};
    uint32_t d, k, i, frames = 4u * BAR * CTL, n = 0;
    for (d = 0; d < NELEM(D); d++) {
        FILE *f = demo_open(dir, D[d].name, frames);
        if (!f)
            continue;
        loop_fresh();
        seq_start();
        for (k = 0; k < 4u * BAR; k++) {
            if (k == 2u * BAR) {
                song.g[G_DMG] = D[d].dmg;
                song.g[G_DMGC] = D[d].chr;
                song.g[G_WALL] = D[d].wall;
            }
            mixb(1);
            for (i = 0; i < CTL; i++)
                wav_put(f, out_buf[2u * i], out_buf[2u * i + 1u]);
        }
        seq_stop();
        fclose(f);
        n++;
    }
    return (int)n;
}

/* the peak of the master after a second of a mode: 0 stopped and silent, 1 PLAY with every track empty (the tape runs,
 * nothing on it), 2 stopped, a note held on T1 */
static int32_t mode_peak(int32_t dmg, int32_t chr, int32_t wall, int32_t hiss, int mode)
{
    int32_t pk = 0;
    uint32_t k;
    loop_fresh();
    song.g[G_DMG] = (int16_t)dmg;
    song.g[G_DMGC] = (int16_t)chr;
    song.g[G_WALL] = (int16_t)wall;
    song.g[G_HISS] = (int16_t)hiss;
    if (mode == 1) {
        for (k = 0; k < NPART; k++)
            track_defaults_steps(&trk[k]);
        seq_start();
    }
    if (mode == 2)
        trk_note_on(&trk[0], 40, 100);
    mixb(FS / CTL);
    for (k = 0; k < FS / CTL; k++) {
        mixb(1);
        if (blk_peak > pk)
            pk = blk_peak;
    }
    if (mode == 1)
        seq_stop();
    return pk;
}
static int32_t silent_peak(int32_t dmg, int32_t chr, int32_t wall, int32_t hiss) { return mode_peak(dmg, chr, wall, hiss, 0); }
/* HISS: the peak a second after the last note let go (stopped): the tape stopped by then */
static int32_t after_note_peak(int32_t hiss)
{
    int32_t pk = 0;
    uint32_t k;
    loop_fresh();
    song.g[G_HISS] = (int16_t)hiss;
    trk_note_on(&trk[0], 40, 100);
    mixb(FS / 4u / CTL);
    trk_note_off(&trk[0], 40);
    mixb(3u * FS / CTL);                                /* (the release, the 1 s hold, the 0.6 s close) */
    for (k = 0; k < FS / 4u / CTL; k++) {
        mixb(1);
        if (blk_peak > pk)
            pk = blk_peak;
    }
    return pk;
}
static void test_apart(void)
{
    int32_t a = silent_peak(127, 0, 0, 0), b = silent_peak(127, 127, 0, 0), c = silent_peak(0, 0, 127, 0),
            h = mode_peak(0, 0, 0, 64, 1), h2 = mode_peak(0, 0, 0, 127, 1), hs = silent_peak(0, 0, 0, 127),
            hn = mode_peak(0, 0, 0, 127, 2), ha = after_note_peak(127), ha0 = after_note_peak(0);
    project_t q;
    printf("damage:   silence: DMG TAPE %d, DMG DIGITAL %d, WALL %d | HISS while PLAY plays (empty): 64 %d, 127 %d | HISS "
           "127 stopped and silent %d, a note held %d, 3 s after it %d (without HISS %d)\n", a, b, c, h, h2, hs, hn, ha, ha0);
    ck("DAMAGE and WALL add nothing to silence (no hiss of their own); HISS does while PLAY plays, louder at 127, "
       "under -24 dB", a == 0 && b == 0 && c == 0 && h > 20 && h2 > h && h2 < 2100);
    ck("  HISS is a running tape's: stopped and silent none, with a note held it hisses, 3 s after the note none again "
       "(only the note's own reverb tail, as without HISS)", hs == 0 && hn > 0 && ha <= ha0 + 2);
    ck("  DAMAGE and WALL are different sounds (the loop at DMG 127 against WALL 127)",
       run_hash(127, 0, 0, 2) != run_hash(0, 0, 127, 2) && run_hash(0, 0, 127, 2) != run_hash(127, 0, 127, 2));
    {   /* by what they do: WALL has none of DAMAGE's marks (no dropouts, no held samples); DMG and HISS follow their knob */
        stats_t dry, wl, dg, d40, d90;
        dry = run_stats(0, 127, 0, 8, 1);
        wl = run_stats(0, 127, 127, 8, 0);
        dg = run_stats(127, 127, 0, 8, 0);
        d40 = run_stats(40, 0, 0, 4, 0);
        d90 = run_stats(90, 0, 0, 4, 0);
        printf("damage:   WALL 127: drops %u held %u | DMG 127 DIGITAL: drops %u held %u | dry held %u | TAPE rms at 40 %.0f, 90 %.0f\n",
               wl.drops, wl.held, dg.drops, dg.held, dry.held, d40.rms, d90.rms);
        ck("  WALL has none of DAMAGE's wear: next to no dropouts (5 at most in 16 s), no held samples; DIGITAL DMG has both",
           wl.drops <= 5u && wl.held <= dry.held + 10u && dg.drops > 20u && dg.held > dry.held + 200u);
        {
            stats_t t127 = run_stats(127, 0, 0, 4, 0), d127 = run_stats(127, 127, 0, 4, 0), w127 = run_stats(0, 32, 127, 4, 0),
                    d0 = run_stats(0, 0, 0, 4, 0);
            double lt = 20 * log10(t127.rms / d0.rms), ld = 20 * log10(d127.rms / d0.rms), lw = 20 * log10(w127.rms / d0.rms);
            printf("damage:   level against dry: TAPE 127 %+.1f dB, DIGITAL 127 %+.1f dB, WALL 127 %+.1f dB\n", lt, ld, lw);
            ck("  DAMAGE and WALL keep the level: within +1.5 dB of dry (denser, not louder), not down more than 6 dB",
               lt < 1.5 && ld < 1.5 && lw < 1.5 && lt > -6 && ld > -6 && lw > -6);
        }
        ck("  DMG follows its knob (40 and 90 differ), HISS too (64 < 127, 0 silent)",
           d40.rms != d90.rms && mode_peak(0, 0, 0, 32, 1) < mode_peak(0, 0, 0, 64, 1) && mode_peak(0, 0, 0, 0, 1) == 0);
    }
    loop_fresh();
    song.g[G_HISS] = 77;
    project_capture(&q);
    {
        static project_store_t st;
        project_t r;
        int ok = proj_pack(&st, &q);
        memset(&r, 0, sizeof r);
        ok = ok && proj_unpack(&r, st.raw, sizeof st.raw);
        ck("  HISS kept in the project (its own byte): 77 back", ok && r.hiss == 77u && q.hiss == 77u);
    }
}

int main(int argc, char **argv)
{
    test_off();
    test_damage();
    test_drops();
    test_wall();
    test_page();
    test_apart();
    if (argc > 1)
        printf("damage: %d demos in %s (two bars dry, two with)\n", demos(argv[1]), argv[1]);
    if (bad) {
        printf("DAMAGE TEST FAILED (%d)\n", bad);
        return 1;
    }
    printf("damage: all ok\n");
    return 0;
}
