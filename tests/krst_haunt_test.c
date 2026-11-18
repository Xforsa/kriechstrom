/* SPDX-License-Identifier: GPL-3.0-only */
/* KRIECHSTROM: HAUNT (src/krst_haunt.c: SUCK, DRAG), on the real sources:
 *   build/host/krst_haunt_test
 * On the power-on project (a kick on the beats, a snare on 2 and 4, 120 BPM), 8 bars after PLAY:
 * 1. MODE OFF or AMT 0: nothing runs, the mix bit for bit as without it.
 * 2. Each mode changes the mix, bounded; the same each time (the grid and the tape from PLAY).
 *    SUCK: what it adds rises into the snares (more just before them than just after).
 *    DRAG: the change lies in the bars' last beats and the first 3 ms of the one; turned on mid-song too (its
 *    grid is the song's from PLAY).
 * 3. Stored: MODE and AMT back from a project (the high bits); a project before has HAUNT off.
 * 4. The page: FX > HAUNT: MODE, AMT. */
#define KRST_DEMO_DEF 1
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int bad;
static void ck(const char *what, int ok)
{
    printf("haunt: %-86s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

#define HB 8u
#define HMAXF (HB * 4u * 60u * FS / 120u + CTL)
typedef struct { uint64_t h; int32_t peak; } hrun_t;
static int32_t hm[HMAXF];                                /* the mix (left), in the child; the parent gets it by pipe */
static int late;                                         /* run_in: HAUNT turned on at bar 2 and a half, not before PLAY */
static hrun_t run_in(int mode, int amt)
{
    static int32_t out[2 * CTL];
    hrun_t m;
    uint32_t k, i, nb = HB * 4u * 60u * FS / 120u / CTL, nf = 0;
    memset(&m, 0, sizeof m);
    m.h = 0xCBF29CE484222325ull;
    ui_power_on();
    song.master_q12 = 3200;
    song.g[G_BPM] = 120;
    song.g[G_HAUNT] = (int16_t)(late ? 0 : mode);
    song.g[G_HAMT] = (int16_t)amt;
    for (k = 0; k < 16u; k++) {                         /* the drums: a kick on the beats, a snare on 2 and 4 (no
                                                         * other hits: the swells stand apart) */
        step_t *sp = &trk[3].step[k];
        sp->hit = (uint8_t)((k % 4u == 0u ? 1u << DV_KICK : 0u) | (k == 4u || k == 12u ? 1u << DV_SNARE : 0u));
        sp->n = 0;
        sp->time = sp->hit ? ST_NOTE : ST_REST;
    }
    trk[3].p[P_SLEN] = 16;
    seq_start();
    for (k = 0; k < nb; k++) {
        if (late && k == nb * 5u / 16u)
            song.g[G_HAUNT] = (int16_t)mode;
        mix_block(out, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t a = abs(out[2u * i]);
            m.peak = a > m.peak ? a : m.peak;
            hm[nf++] = out[2u * i];
            m.h = (m.h ^ (uint32_t)out[2u * i]) * 0x100000001B3ull;
            m.h = (m.h ^ (uint32_t)out[2u * i + 1u]) * 0x100000001B3ull;
        }
    }
    seq_stop();
    return m;
}
static int32_t mixes[4][HMAXF];
static hrun_t run(int mode, int amt, int32_t *keep)
{
    int fd[2];
    hrun_t m;
    pid_t pid;
    memset(&m, 0, sizeof m);
    if (pipe(fd))
        return m;
    fflush(stdout);
    if (!(pid = fork())) {
        m = run_in(mode, amt);
        if (write(fd[1], &m, sizeof m) != sizeof m)
            _exit(1);
        {
            uint8_t *p = (uint8_t *)hm;
            size_t left = sizeof hm;
            while (left) {
                ssize_t w = write(fd[1], p, left);
                if (w <= 0)
                    _exit(1);
                p += w, left -= (size_t)w;
            }
        }
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], &m, sizeof m) != sizeof m)
        memset(&m, 0, sizeof m);
    {
        uint8_t *p = (uint8_t *)(keep ? keep : hm);
        size_t left = sizeof hm;
        while (left) {
            ssize_t r = read(fd[0], p, left);
            if (r <= 0)
                break;
            p += r, left -= (size_t)r;
        }
    }
    close(fd[0]);
    waitpid(pid, 0, 0);
    return m;
}
/* the energy of a - b over [i0, i1) */
static double e_diff(const int32_t *a, const int32_t *b, uint32_t i0, uint32_t i1, int32_t lag)
{
    double e = 0;
    uint32_t i;
    for (i = i0; i < i1; i++) {
        int64_t j = (int64_t)i - lag;
        double d = (double)a[i] - (j >= 0 ? (double)b[j] : 0);
        e += d * d;
    }
    return e;
}

int main(void)
{
    uint32_t beat = 60u * FS / 120u, bar = 4u * beat;
    hrun_t off = run(0, 0, mixes[0]), off2 = run(0, 110, 0), none = run(1, 0, 0), sk, dr, sk2, drl;
    ck("MODE OFF or AMT 0: nothing runs, the mix bit for bit as without it", off.h == off2.h && off.h == none.h);
    sk = run(1, 110, mixes[1]);
    dr = run(2, 127, mixes[2]);
    sk2 = run(1, 110, 0);
    late = 1;
    drl = run(2, 127, mixes[3]);
    late = 0;
    printf("haunt:   peaks: off %d, SUCK %d, DRAG %d (on mid-song %d)\n", off.peak, sk.peak, dr.peak, drl.peak);
    ck("every mode changes the mix, bounded", sk.h != off.h && dr.h != off.h && sk.peak < 32768 && dr.peak < 32768);
    ck("  the same each time (PLAY starts the grid)", sk.h == sk2.h);
    {   /* SUCK: what it adds just before each snare (the swell's top, a sixteenth) against just after */
        double before = 0, after = 0;
        uint32_t k, st = beat / 4u;
        for (k = 16u; k + 1u < HB * 16u; k++)
            if (k % 16u == 4u || k % 16u == 12u) {
                uint32_t h = k * st;
                before += e_diff(mixes[1], mixes[0], h - st, h - 64u, 0);
                after += e_diff(mixes[1], mixes[0], h + 64u, h + st, 0);
            }
        printf("haunt:   SUCK: added energy in the sixteenth before the snares %.3g, after %.3g\n", before, after);
        ck("SUCK: the swell rises into the snares (more just before them than just after)", before > 2.0 * after);
    }
    {   /* DRAG: the change lies in each bar's last eighth (and the one's first 3 ms); the same turned on mid-song */
        double in = 0, out = 0, in_l = 0, out_l = 0;
        uint32_t b, w = bar / 4u;
        for (b = 1; b + 1u < HB; b++) {
            in += e_diff(mixes[2], mixes[0], b * bar + bar - w, b * bar + bar + 140u, 0);
            out += e_diff(mixes[2], mixes[0], b * bar + 200u, b * bar + bar - w - 64u, 0);
            if (b >= 3u) {
                in_l += e_diff(mixes[3], mixes[0], b * bar + bar - w, b * bar + bar + 140u, 0);
                out_l += e_diff(mixes[3], mixes[0], b * bar + 200u, b * bar + bar - w - 64u, 0);
            }
        }
        printf("haunt:   DRAG: the change in the last beats %.3g, elsewhere %.3g; turned on mid-song %.3g, %.3g\n",
               in, out, in_l, out_l);
        ck("DRAG: the tape brakes in the bars' last beats only", in > 0 && out < in * 0.01);
        ck("  turned on mid-song: on the song's grid all the same", in_l > 0 && out_l < in_l * 0.01);
    }
    {
        static project_store_t st;
        project_t q, r;
        int ok, m;
        for (m = 1, ok = 1; m <= 2; m++) {
            ui_power_on();
            song.g[G_HAUNT] = (int16_t)m;
            song.g[G_HAMT] = 77;
            song.g[G_SPLICE] = 40;
            song.g[G_RES] = 90;
            project_capture(&q);
            ok = ok && proj_pack(&st, &q);
            memset(&r, 0, sizeof r);
            ok = ok && proj_unpack(&r, st.raw, sizeof st.raw);
            song.g[G_HAUNT] = song.g[G_HAMT] = song.g[G_SPLICE] = song.g[G_RES] = 0;
            project_restore_runtime(&r);
            ok = ok && song.g[G_HAUNT] == m && song.g[G_HAMT] == 77 && song.g[G_SPLICE] == 40 && song.g[G_RES] == 90;
        }
        r.g[G_LOAD] = 1;                                /* (a project before: G_LOAD a button's 0 or 1) */
        r.sum = proj_sum(&r);
        project_restore_runtime(&r);
        ok = ok && song.g[G_HAUNT] == 0 && song.g[G_HAMT] == GP[G_HAMT].def && song.g[G_SPLICE] == 40;
        ck("stored: MODE and AMT back (G_LOAD's word, marked; RESONATE and SPLICE as they were); a project before: off", ok);
    }
    {
        uint32_t i;
        int ok = 0;
        for (i = 0; i < NELEM(PAGES); i++)
            if (str_eq(PAGES[i].title, "HAUNT"))
                ok = PAGES[i].fam == FAM_FX && PAGES[i].id[0] == G_HAUNT && PAGES[i].id[1] == G_HAMT;
        ck("the page: FX > HAUNT: MODE (OFF SUCK DRAG), AMT", ok && str_eq(GP[G_HAUNT].label, "MODE") &&
           str_eq(GP[G_HAUNT].names[1], "SUCK") && str_eq(GP[G_HAUNT].names[2], "DRAG") && GP[G_HAUNT].max == 2);
    }
    printf("%s\n", bad ? "HAUNT TEST FAILED" : "haunt: all ok");
    return bad != 0;
}
