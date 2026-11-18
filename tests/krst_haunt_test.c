/* SPDX-License-Identifier: GPL-3.0-only */
/* KRIECHSTROM: HAUNT (src/krst_haunt.c: PRINT, SUCK, DRAG), on the real sources:
 *   build/host/krst_haunt_test
 * On the power-on project (its drums, 120 BPM), 8 bars after PLAY:
 * 1. MODE OFF or AMT 0: nothing runs, the mix bit for bit as without it.
 * 2. Each mode changes the mix, bounded; the same each time (the grid and the tape from PLAY).
 *    PRINT: the ghost is the mix three beats back (the change correlates with the mix delayed so, not with it now).
 *    SUCK: what it adds comes before the snares and claps (more in the beat before them than in the beat after).
 *    DRAG: the change lies in the bars' last beats and the first moment of the one.
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
    song.g[G_HAUNT] = (int16_t)mode;
    song.g[G_HAMT] = (int16_t)amt;
    seq_start();
    for (k = 0; k < nb; k++) {
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
    uint32_t beat = 60u * FS / 120u, bar = 4u * beat, n = HB * bar;
    hrun_t off = run(0, 0, mixes[0]), off2 = run(0, 110, 0), none = run(1, 0, 0), pr, sk, dr, pr2;
    ck("MODE OFF or AMT 0: nothing runs, the mix bit for bit as without it", off.h == off2.h && off.h == none.h);
    pr = run(1, 110, mixes[1]);
    sk = run(2, 110, mixes[2]);
    dr = run(3, 110, mixes[3]);
    pr2 = run(1, 110, 0);
    printf("haunt:   peaks: off %d, PRINT %d, SUCK %d, DRAG %d\n", off.peak, pr.peak, sk.peak, dr.peak);
    ck("every mode changes the mix, bounded", pr.h != off.h && sk.h != off.h && dr.h != off.h &&
       pr.peak < 32768 && sk.peak < 32768 && dr.peak < 32768);
    ck("  the same each time (PLAY wipes the tape, starts the grid)", pr.h == pr2.h);
    {   /* PRINT: the change's loudness (10 ms) follows the mix's of three beats back more than the mix's now */
        static double ed[4096], e3[4096], e0[4096];
        uint32_t w = 441u, nw = 0, i, j;
        double m_d = 0, m3 = 0, m0 = 0, c3 = 0, c0 = 0, v_d = 0, v3 = 0, v0 = 0;
        for (i = 4u * bar; i + w <= n && nw < 4096u; i += w, nw++) {
            double a = 0, b3 = 0, b0 = 0;
            for (j = i; j < i + w; j++) {
                double d = (double)mixes[1][j] - mixes[0][j];
                a += d * d, b3 += (double)mixes[0][j - 3u * beat] * mixes[0][j - 3u * beat], b0 += (double)mixes[0][j] * mixes[0][j];
            }
            ed[nw] = sqrt(a), e3[nw] = sqrt(b3), e0[nw] = sqrt(b0);
            m_d += ed[nw], m3 += e3[nw], m0 += e0[nw];
        }
        m_d /= nw, m3 /= nw, m0 /= nw;
        for (i = 0; i < nw; i++) {
            c3 += (ed[i] - m_d) * (e3[i] - m3), c0 += (ed[i] - m_d) * (e0[i] - m0);
            v_d += (ed[i] - m_d) * (ed[i] - m_d), v3 += (e3[i] - m3) * (e3[i] - m3), v0 += (e0[i] - m0) * (e0[i] - m0);
        }
        c3 /= sqrt(v_d * v3 + 1e-9), c0 /= sqrt(v_d * v0 + 1e-9);
        printf("haunt:   PRINT: the change's loudness against the mix 3 beats back %.2f, the mix now %.2f\n", c3, c0);
        ck("PRINT: the ghost follows the mix of three beats back", c3 > 0.5 && pr.h != off.h);
    }
    {   /* SUCK: what it adds just before each snare or clap (the swell's top, a quarter beat) against just after */
        double before = 0, after = 0;
        uint32_t k, st = beat / 4u;
        for (k = 16u; k + 1u < HB * 16u; k++)
            if (hn_hit_at(k)) {
                uint32_t h = k * st;
                before += e_diff(mixes[2], mixes[0], h - st, h - 64u, 0);
                after += e_diff(mixes[2], mixes[0], h + 64u, h + st, 0);
            }
        printf("haunt:   SUCK: added energy in the sixteenth before the hits %.3g, after %.3g\n", before, after);
        ck("SUCK: the swell rises into the hits (more just before them than just after)", before > 2.0 * after);
    }
    {   /* DRAG: the change lies in each bar's last beat (and the one's first 1/80 of the bar) */
        double in = 0, out = 0;
        uint32_t b;
        for (b = 1; b + 1u < HB; b++) {
            in += e_diff(mixes[3], mixes[0], b * bar + 3u * beat, b * bar + bar, 0) +
                  e_diff(mixes[3], mixes[0], (b + 1u) * bar, (b + 1u) * bar + bar / 80u, 0);
            out += e_diff(mixes[3], mixes[0], b * bar + bar / 80u + 64u, b * bar + 3u * beat - 64u, 0);
        }
        printf("haunt:   DRAG: the change in the last beats %.3g, elsewhere %.3g\n", in, out);
        ck("DRAG: the mix drags in the bars' last beats only", in > 0 && out < in * 0.01);
    }
    {
        static project_store_t st;
        project_t q, r;
        int ok, m;
        for (m = 1, ok = 1; m <= 3; m++) {
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
        ck("the page: FX > HAUNT: MODE (OFF PRINT SUCK DRAG), AMT", ok && str_eq(GP[G_HAUNT].label, "MODE") &&
           str_eq(GP[G_HAUNT].names[3], "DRAG"));
    }
    printf("%s\n", bad ? "HAUNT TEST FAILED" : "haunt: all ok");
    return bad != 0;
}
