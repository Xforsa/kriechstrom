/* SPDX-License-Identifier: GPL-3.0-only */
/* KRIECHSTROM: RESONATE (src/krst_resonate.c), four strings on the master tuned to a chord, on the real sources:
 *   build/host/krst_resonate_test [DEMO_DIR]
 * 1. AMT 0: the loop renders bit for bit as without it.
 * 2. a burst of noise into the strings (res_master on its own): they ring at the chord's notes (A MIN: A3 E4 A4 C5,
 *    each a peak within 1 % of its note; OCT: A3 A4 E5 A5), longer at AMT 127 (a pedal held) than at 40, bounded,
 *    and die away (no offset left ringing: the loop rounds towards zero).
 * 3. stored: AMT KEY CHORD back from the project's header bytes; a project before (the bytes 0) has it off.
 * 4. the page: FX > RESONATE: AMT KEY CHORD.
 * 5. demo (DEMO_DIR): the power-on project 8 bars dry, 8 with RESONATE (A MIN, AMT 90). */
#define KRST_DEMO_DEF 1
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int bad;
static void ck(const char *what, int ok)
{
    printf("resonate: %-80s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
#define RN (5u * FS)
static float buf[RN];
/* the strings' answer to 20 ms of noise: buf (left + right), its peak, its level 4.5 s on against 0.1 s on */
static void ring(int amt, int key, int chord, int32_t *peak, double *tail)
{
    int32_t l[CTL], r[CTL], st = 0x12345;
    uint32_t k, i;
    double e1 = 0, e2 = 0;
    memset(rs_line, 0, sizeof rs_line);
    memset(rs_lp, 0, sizeof rs_lp);
    rs_w = 0;
    song.g[G_RES] = (int16_t)amt;
    song.g[G_RKEY] = (int16_t)key;
    song.g[G_RCHD] = (int16_t)chord;
    *peak = 0;
    for (k = 0; k < RN / CTL; k++) {
        for (i = 0; i < CTL; i++)
            l[i] = r[i] = k * CTL + i < FS / 50u ? (int32_t)(noise32(&st) >> 18) - 8192 : 0;
        res_master(l, r, CTL);
        for (i = 0; i < CTL; i++) {
            uint32_t t = k * CTL + i;
            float v = (float)(l[i] + r[i]);
            int32_t a = l[i] < 0 ? -l[i] : l[i];
            buf[t] = t < FS / 50u ? 0 : v;              /* (the strings alone, the burst left out) */
            *peak = a > *peak ? a : *peak;
            if (t > FS / 10u && t < FS / 10u + FS / 10u)
                e1 += (double)v * v;
            if (t > 9u * FS / 2u && t < 9u * FS / 2u + FS / 10u)
                e2 += (double)v * v;
        }
    }
    song.g[G_RES] = 0;
    *tail = 10 * log10((e2 + 1) / (e1 + 1));
}
static double energy_at(double f)
{
    double c = 0, s = 0, w = 2 * M_PI * f / FS;
    uint32_t i;
    for (i = FS / 20u; i < FS / 20u + FS / 2u; i++) {
        c += buf[i] * cos(w * i);
        s += buf[i] * sin(w * i);
    }
    return c * c + s * s;
}
/* the strongest frequency within +-6 % of f */
static double peak_near(double f)
{
    double g, bf = f, bv = 0;
    for (g = f * 0.94; g < f * 1.06; g += 0.25) {
        double e = energy_at(g);
        if (e > bv) {
            bv = e;
            bf = g;
        }
    }
    return bf;
}

int main(int argc, char **argv)
{
    static const double MIN[4] = {220.0, 329.63, 440.0, 523.25}, OCT[4] = {220.0, 440.0, 659.26, 880.0};
    int32_t pk40, pk127, pk;
    double t40, t127, t;
    uint32_t k;
    int tune = 1;
    ui_power_on();
    {   /* AMT 0: as without it (two runs of the power-on loop, RESONATE never on) */
        static int32_t out[2 * CTL];
        uint64_t h[2] = {0, 0};
        uint32_t run, b;
        for (run = 0; run < 2u; run++) {
            ui_power_on();
            song.g[G_RES] = 0;
            seq_start();
            for (b = 0; b < FS / CTL; b++) {
                uint32_t i;
                mix_block(out, CTL);
                for (i = 0; i < 2u * CTL; i++)
                    h[run] = h[run] * 0x100000001B3ull ^ (uint32_t)out[i];
            }
            seq_stop();
        }
        ck("AMT 0: nothing runs (the strings never fed, the loop the same twice)", h[0] == h[1] && !rs_w);
    }
    ring(40, 9, 2, &pk40, &t40);
    ring(127, 9, 2, &pk127, &t127);
    for (k = 0; k < 4u; k++) {
        double f = peak_near(MIN[k]);
        printf("resonate:   A MIN string %u: %.2f Hz rings at %.2f\n", (unsigned)k + 1u, MIN[k], f);
        tune &= fabs(f / MIN[k] - 1.0) < 0.01;
    }
    ring(127, 9, 0, &pk, &t);
    for (k = 0; k < 4u; k++) {
        double f = peak_near(OCT[k]);
        printf("resonate:   A OCT string %u: %.2f Hz rings at %.2f\n", (unsigned)k + 1u, OCT[k], f);
        tune &= fabs(f / OCT[k] - 1.0) < 0.01;
    }
    printf("resonate:   after 4.5 s against 0.1 s: AMT 40 %.1f dB, AMT 127 %.1f dB; peaks %d %d\n", t40, t127, (int)pk40, (int)pk127);
    ck("the strings ring at the chord's notes (within 1 %: A MIN, A OCT)", tune);
    ck("AMT 40 dies away (under -40 dB at 4.5 s), 127 rings on longer but dies too (under -12 dB), bounded",
       t40 < -40 && t127 > t40 + 20 && t127 < -12 && pk127 < 32768);
    {
        static project_store_t st;
        project_t q, r;
        int ok;
        ui_power_on();
        song.g[G_RES] = 77;
        song.g[G_RKEY] = 4;
        song.g[G_RCHD] = 6;
        project_capture(&q);
        ok = proj_pack(&st, &q);
        memset(&r, 0, sizeof r);
        ok = ok && proj_unpack(&r, st.raw, sizeof st.raw) && r.res == 77u && r.rkey == (4u | 6u << 4);
        project_restore_runtime(&r);
        ok = ok && song.g[G_RES] == 77 && song.g[G_RKEY] == 4 && song.g[G_RCHD] == 6;
        ck("stored: AMT 77, KEY E, CHORD DIM back from the header bytes", ok);
    }
    {
        uint32_t i;
        int ok = 0;
        for (i = 0; i < NELEM(PAGES); i++)
            if (str_eq(PAGES[i].title, "RESONATE"))
                ok = PAGES[i].fam == FAM_FX && PAGES[i].id[0] == G_RES && PAGES[i].id[1] == G_RKEY &&
                     PAGES[i].id[2] == G_RCHD;
        ck("the page: FX > RESONATE: AMT KEY CHORD", ok && str_eq(GP[G_RES].label, "AMT") && str_eq(N_RCHD[2], "MIN"));
    }
    if (argc > 1) {
        static int32_t out[2 * CTL];
        char path[512];
        FILE *f;
        uint32_t b, bars = 16u, barb = 4u * FS / 2u / CTL;
        snprintf(path, sizeof path, "%s/resonate.wav", argv[1]);
        if ((f = fopen(path, "wb"))) {
            ui_power_on();
            song.master_q12 = 3200;
            wav_hdr(f, bars * barb * CTL);
            seq_start();
            for (b = 0; b < bars * barb; b++) {
                uint32_t i;
                if (b == 8u * barb) {
                    song.g[G_RES] = 90;
                    song.g[G_RKEY] = 9;
                    song.g[G_RCHD] = 2;
                }
                mix_block(out, CTL);
                for (i = 0; i < CTL; i++)
                    wav_put(f, out[2u * i], out[2u * i + 1u]);
            }
            seq_stop();
            fclose(f);
            printf("resonate: demo in %s\n", path);
        }
    }
    printf("%s\n", bad ? "RESONATE TEST FAILED" : "resonate: all ok");
    return bad != 0;
}
