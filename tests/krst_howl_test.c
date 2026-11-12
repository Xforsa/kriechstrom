/* SPDX-License-Identifier: GPL-3.0-only */
/* KRIECHSTROM: GLCH's feedback family, HOWL and MIC (src/eng_glitch.c howl_render), on the real sources:
 *   build/host/krst_howl_test [DEMO_DIR]
 * One note held 3 s on track 1 (GLCH, the sends off), measured from 2 s on:
 * 1. HOWL: under its threshold (GAIN 40) a note dies away, over it (GAIN 90) it sustains; sustaining it is in tune
 *    (the autocorrelation's period within 1 % of the note, three octaves); BLOOM swells it in; bounded, no DC.
 * 2. MIC: GAIN 0 silent, GAIN 100 howls; the howl's strongest frequency near the note (within 25 Hz, three notes).
 * 3. a note let go: silence after its release (the loop does not ring on in a voice that ended).
 * 4. the sounds: GLCH's WAVE lists HOWL and MIC; the eight feedback sounds are there, each sounds, bounded.
 * 5. demos (DEMO_DIR): each of the eight sounds, a few held notes. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int bad;
static void ck(const char *what, int ok)
{
    printf("howl: %-84s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static int32_t ob[2 * CTL];
#define WIN 8820
static float win[WIN];
static int wn;
typedef struct { double early, late, mean, ac, spec; int32_t peak; } hm_t;

static void fresh(const int16_t e[8])
{
    uint32_t k;
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    host_preset(&trk[0], 7, 0);
    for (k = 0; k < 8u; k++)
        trk[0].p[P_E0 + k] = e[k];
    trk[0].p[P_ITYPE] = 0;
    trk[0].p[P_REV] = trk[0].p[P_DLY] = trk[0].p[P_CHOR] = trk[0].p[P_DIST] = 0;
    trk[0].p[P_ATK] = 0;
    trk[0].p[P_SUS] = 127;
    trk[0].p[P_REL] = 30;
    trk[0].p[P_ED_FLT] = 0;
    song.g[G_DMG] = song.g[G_WALL] = song.g[G_HISS] = 0;
}
static double acpitch(void)
{
    int L, i, best = 0;
    double bv = 0.5;
    for (L = 20; L < 2205; L++) {
        double a = 0, b = 0, c = 0, r;
        for (i = 0; i + L < wn; i++) {
            a += win[i] * win[i + L];
            b += win[i] * win[i];
            c += win[i + L] * win[i + L];
        }
        r = a / sqrt(b * c + 1e-9);
        if (r > bv * 1.02) {
            bv = r;
            best = L;
        }
    }
    return best ? (double)FS / best : 0;
}
static double specpk(void)
{
    double f, bf = 0, bv = 0;
    for (f = 20; f < 2500; f += 1) {
        double c = 0, s = 0, w = 2 * M_PI * f / FS, e;
        int i;
        for (i = 0; i < wn; i += 2) {
            c += win[i] * cos(w * i);
            s += win[i] * sin(w * i);
        }
        e = c * c + s * s;
        if (e > bv) {
            bv = e;
            bf = f;
        }
    }
    return bf;
}
/* a note held 3 s: RMS of its first 0.15 s and from 2 s on, the peak, the mean, its pitch (spec: also the spectrum) */
static hm_t held(const int16_t e[8], uint32_t note, int spec)
{
    hm_t m = {0, 0, 0, 0, 0, 0};
    uint32_t k, i, n1 = 0, n2 = 0, blocks = 3u * FS / CTL;
    wn = 0;
    fresh(e);
    trk_note_on(&trk[0], note, 100);
    for (k = 0; k < blocks; k++) {
        mix_block(ob, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t v = ob[2u * i];
            double t = (k * CTL + i) / (double)FS;
            if ((v < 0 ? -v : v) > m.peak)
                m.peak = v < 0 ? -v : v;
            if (t < 0.15) {
                m.early += (double)v * v;
                n1++;
            } else if (t > 2.0) {
                m.late += (double)v * v;
                m.mean += v;
                n2++;
                if (wn < WIN)
                    win[wn++] = (float)v;
            }
        }
    }
    trk_note_off(&trk[0], note);
    m.early = sqrt(m.early / n1);
    m.late = sqrt(m.late / n2);
    m.mean /= n2;
    m.ac = acpitch();
    m.spec = spec ? specpk() : 0;
    return m;
}
static double hz(uint32_t note) { return 440.0 * pow(2, ((int)note - 69) / 12.0); }

static void tests(void)
{
    static const int16_t DIE[8] = {GW_HOWL, 110, 20, 40, 0, 0, 64, 0}, SUS[8] = {GW_HOWL, 110, 20, 90, 0, 0, 64, 0};
    static const int16_t BLM[8] = {GW_HOWL, 110, 20, 90, 0, 0, 64, 100}, M0[8] = {GW_MIC, 110, 110, 0, 0, 0, 20, 0};
    static const int16_t M1[8] = {GW_MIC, 110, 110, 100, 0, 0, 20, 0};
    static const uint32_t NOTES[3] = {33, 45, 57}, MNOTES[3] = {57, 64, 69};
    hm_t d = held(DIE, 45, 0), s = held(SUS, 45, 0), b = held(BLM, 45, 0), m0 = held(M0, 69, 0);
    uint32_t k;
    int tune = 1, mic = 1, peak = s.peak < 32768 && b.peak < 32768;
    printf("howl:   HOWL A2: GAIN 40 rms %.0f -> %.0f, GAIN 90 %.0f -> %.0f (peak %d, mean %.0f), BLOOM 100 starts at %.0f\n",
           d.early, d.late, s.early, s.late, (int)s.peak, s.mean, b.early);
    ck("HOWL: under the threshold (GAIN 40) a note dies away, over it (GAIN 90) it sustains",
       d.late < d.early * 0.05 && s.late > 5000);
    ck("HOWL: BLOOM swells it in (its first 0.15 s under a quarter of BLOOM 0's), the same in the end",
       b.early < s.early * 0.25 && b.late > s.late * 0.8);
    ck("HOWL: bounded, no DC (|mean| under 0.5 % of full scale)", peak && fabs(s.mean) < 164);
    for (k = 0; k < 3u; k++) {
        hm_t h = held(SUS, NOTES[k], 0);
        printf("howl:   HOWL note %u (%.1f Hz): %.1f Hz\n", (unsigned)NOTES[k], hz(NOTES[k]), h.ac);
        tune &= fabs(h.ac / hz(NOTES[k]) - 1.0) < 0.01;
    }
    ck("HOWL: in tune with the keys (within 1 %, 55 110 220 Hz)", tune);
    for (k = 0; k < 3u; k++) {
        hm_t h = held(M1, MNOTES[k], 1);
        printf("howl:   MIC note %u (%.1f Hz): howls at %.0f Hz, rms %.0f peak %d\n", (unsigned)MNOTES[k], hz(MNOTES[k]), h.spec,
               h.late, (int)h.peak);
        mic &= fabs(h.spec - hz(MNOTES[k])) < 25 && h.late > 5000 && h.peak < 32768;
    }
    printf("howl:   MIC GAIN 0: rms %.0f -> %.0f\n", m0.early, m0.late);
    ck("MIC: GAIN 0 next to silent, GAIN 100 howls near the note (within 25 Hz), bounded", m0.late < 50 && mic);
    {   /* let go: the release, then nothing */
        uint32_t i, blocks = FS / CTL;
        int32_t pk = 0;
        for (i = 0; i < blocks; i++)
            mix_block(ob, CTL);
        for (i = 0; i < 16u; i++) {
            uint32_t j;
            mix_block(ob, CTL);
            for (j = 0; j < 2u * CTL; j++)
                pk = ob[j] > pk ? ob[j] : -ob[j] > pk ? -ob[j] : pk;
        }
        ck("a howl let go: silent a second after its release", pk < 8);
    }
    {
        const engine_t *e = ENGINES[7];
        uint32_t i, found = 0, sounds = 1;
        static const char *const NAMES[8] = {"FEEDBACK", "STRUNG OUT", "SCREAMER", "SWELL", "WOUNDED", "PA HOWL",
                                             "WRONG ROOM", "STAGE FRIGHT"};
        for (i = 0; i < e->npresets; i++) {
            uint32_t j;
            for (j = 0; j < 8u; j++)
                if (str_eq(e->presets[i].name, NAMES[j])) {
                    int16_t v[8];
                    hm_t h;
                    uint32_t q;
                    for (q = 0; q < 8u; q++)
                        v[q] = e->presets[i].e[q];
                    h = held(v, 52, 0);
                    found++;
                    sounds &= GL_HOWL(v[0]) && (h.late > 500 || h.early > 500) && h.peak < 32768;
                }
        }
        ck("GLCH's WAVE: HOWL and MIC; the eight feedback sounds there, each sounds, bounded",
           str_eq(N_GLITCH_WAVE[GW_HOWL], "HOWL") && str_eq(N_GLITCH_WAVE[GW_MIC], "MIC") && found == 8u && sounds);
    }
}

static int demos(const char *dir)
{
    static const uint8_t PH[4] = {40, 43, 47, 35};
    const engine_t *e = ENGINES[7];
    uint32_t i, n = 0;
    for (i = 0; i < e->npresets; i++) {
        char path[512], name[32];
        uint32_t j, k, q, frames = 8u * FS;
        FILE *f;
        if (!GL_HOWL(e->presets[i].e[0]))
            continue;
        for (j = 0; e->presets[i].name[j] && j < 31u; j++)
            name[j] = e->presets[i].name[j] == ' ' ? '_' : (char)(e->presets[i].name[j] | 0x20);
        name[j] = 0;
        snprintf(path, sizeof path, "%s/howl_%s.wav", dir, name);
        if (!(f = fopen(path, "wb")))
            continue;
        wav_hdr(f, frames);
        memset(trk, 0, sizeof trk);
        host_tracks_init();
        host_preset(&trk[0], 7, i);
        song.g[G_DMG] = song.g[G_WALL] = song.g[G_HISS] = 0;
        for (q = 0; q < 4u; q++) {                        /* four notes of 2 s, held 1.6 s */
            uint32_t nt = PH[q] + (e->presets[i].e[0] == GW_MIC ? 24u : 12u);
            trk_note_on(&trk[0], nt, 100);
            for (k = 0; k < 2u * FS / CTL; k++) {
                if (k == 16u * FS / 10u / CTL)
                    trk_note_off(&trk[0], nt);
                mix_block(ob, CTL);
                for (j = 0; j < CTL; j++)
                    wav_put(f, ob[2u * j], ob[2u * j + 1u]);
            }
        }
        fclose(f);
        n++;
    }
    return (int)n;
}

int main(int argc, char **argv)
{
    tests();
    if (argc > 1)
        printf("howl: %d demos in %s\n", demos(argv[1]), argv[1]);
    if (bad) {
        printf("HOWL TEST FAILED (%d)\n", bad);
        return 1;
    }
    printf("howl: all ok\n");
    return 0;
}
