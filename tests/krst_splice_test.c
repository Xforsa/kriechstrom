/* SPDX-License-Identifier: GPL-3.0-only */
/* KRIECHSTROM: SPLICE (src/krst_splice.c), the tape cut and glued, on the real sources:
 *   build/host/krst_splice_test
 * On the power-on project (120 BPM), 16 bars after PLAY:
 * 1. AMT 0: nothing runs, the mix as without it (bit for bit).
 * 2. AMT 127: pieces glued in, about one sixteenth in two (between 1 in 4 and 3 in 4 of the rolls), never on a bar's
 *    first; every kind (REPEAT REVERSE JUMP SLOW DROP); bounded; at AMT 30 far fewer, and no SLOW or DROP to speak of.
 * 3. the same project cut the same way twice (the dice seeded at PLAY).
 * 4. stored: AMT back from the project (G_SAVE's word, marked); a project before (G_SAVE 0 or 1) has it off.
 * 5. the page: FX > SPLICE: AMT. */
#define KRST_DEMO_DEF 1
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int bad;
static void ck(const char *what, int ok)
{
    printf("splice: %-82s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

typedef struct { uint64_t h; uint32_t pieces, kinds[5], on_one, sixteenths; int32_t peak; } run_t;
static run_t run_in(int amt, uint32_t bars)
{
    static int32_t out[2 * CTL];
    run_t m;
    uint32_t k, i, was = 0, blocks = bars * 4u * FS / 2u / CTL;
    memset(&m, 0, sizeof m);
    m.h = 0xCBF29CE484222325ull;
    ui_power_on();
    song.master_q12 = 3200;                             /* (MASTER at about three quarters) */
    song.g[G_SPLICE] = (int16_t)amt;
    seq_start();
    for (k = 0; k < blocks; k++) {
        mix_block(out, CTL);
        if (spl.left && spl.left > was) {                 /* (a piece started in this block) */
            m.pieces++;
            m.kinds[spl.type]++;
            m.on_one += ((spl.step - 1u) & 15u) == 0u;
        }
        was = spl.left;
        for (i = 0; i < 2u * CTL; i++) {
            int32_t a = out[i] < 0 ? -out[i] : out[i];
            m.peak = a > m.peak ? a : m.peak;
            m.h = (m.h ^ (uint32_t)out[i]) * 0x100000001B3ull;
        }
    }
    m.sixteenths = spl.step;
    seq_stop();
    return m;
}
/* in a fork()ed child: a run never starts from an earlier one's buses, voices or dice */
static run_t run(int amt, uint32_t bars)
{
    int fd[2];
    run_t m;
    pid_t pid;
    memset(&m, 0, sizeof m);
    if (pipe(fd))
        return m;
    fflush(stdout);
    if (!(pid = fork())) {
        m = run_in(amt, bars);
        if (write(fd[1], &m, sizeof m) != sizeof m)
            _exit(1);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], &m, sizeof m) != sizeof m)
        memset(&m, 0, sizeof m);
    close(fd[0]);
    waitpid(pid, 0, 0);
    return m;
}

int main(void)
{
    run_t off = run(0, 4), base, hi, hi2, lo;
    base = off;
    ck("AMT 0: nothing runs (no piece glued)", off.pieces == 0);
    {   /* bit for bit: the same loop never touched by SPLICE, rendered twice */
        run_t again = run(0, 4);
        song.g[G_SPLICE] = 0;
        ck("  .. SPLICE off and the loop the same each time (bit for bit)", again.h == base.h && !splice_on());
    }
    hi = run(127, 16);
    hi2 = run(127, 16);
    lo = run(30, 16);
    printf("splice:   AMT 127: %u pieces of %u sixteenths (REPEAT %u REVERSE %u JUMP %u SLOW %u DROP %u), on a bar's first %u, "
           "peak %d\n", (unsigned)hi.pieces, (unsigned)hi.sixteenths, (unsigned)hi.kinds[0], (unsigned)hi.kinds[1],
           (unsigned)hi.kinds[2], (unsigned)hi.kinds[3], (unsigned)hi.kinds[4], (unsigned)hi.on_one, (int)hi.peak);
    printf("splice:   AMT 30: %u pieces (SLOW %u DROP %u)\n", (unsigned)lo.pieces, (unsigned)lo.kinds[3], (unsigned)lo.kinds[4]);
    ck("AMT 127: pieces now and then (the rolls: 1 in 4 .. 3 in 4 of the free sixteenths), never on a bar's first",
       hi.pieces > hi.sixteenths / 12u && hi.pieces < hi.sixteenths / 2u && hi.on_one == 0);
    ck("  every kind: REPEAT, REVERSE, JUMP, SLOW, DROP", hi.kinds[0] && hi.kinds[1] && hi.kinds[2] && hi.kinds[3] && hi.kinds[4]);
    ck("  bounded, the mix changed", hi.peak < 32768 && hi.h != base.h);
    ck("AMT 30: far fewer (under a third of 127's), SLOW and DROP rare", lo.pieces * 3u < hi.pieces &&
       lo.kinds[3] + lo.kinds[4] <= lo.pieces / 4u + 1u);
    ck("the same project cut the same way twice (the dice from PLAY)", hi.h == hi2.h && hi.pieces == hi2.pieces);
    {
        static project_store_t st;
        project_t q, r;
        int ok;
        ui_power_on();
        song.g[G_SPLICE] = 77;
        project_capture(&q);
        ok = proj_pack(&st, &q);
        memset(&r, 0, sizeof r);
        ok = ok && proj_unpack(&r, st.raw, sizeof st.raw);
        song.g[G_SPLICE] = 0;
        project_restore_runtime(&r);
        ok = ok && song.g[G_SPLICE] == 77;
        r.g[G_SAVE] = 1;                                /* (a project before: G_SAVE a button's 0 or 1) */
        r.sum = proj_sum(&r);
        project_restore_runtime(&r);
        ok = ok && song.g[G_SPLICE] == 0;
        ck("stored: AMT 77 back (G_SAVE's word, marked); a project before has it off", ok);
    }
    {
        uint32_t i;
        int ok = 0;
        for (i = 0; i < NELEM(PAGES); i++)
            if (str_eq(PAGES[i].title, "SPLICE"))
                ok = PAGES[i].fam == FAM_FX && PAGES[i].id[0] == G_SPLICE && PAGES[i].id[1] == 0xFF;
        ck("the page: FX > SPLICE: AMT", ok && str_eq(GP[G_SPLICE].label, "AMT"));
    }
    printf("%s\n", bad ? "SPLICE TEST FAILED" : "splice: all ok");
    return bad != 0;
}
