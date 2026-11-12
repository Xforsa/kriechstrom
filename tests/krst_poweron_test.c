/* SPDX-License-Identifier: GPL-3.0-only */
/* KRIECHSTROM's power-on (built with KRST_DEMO_DEF=1, engines.c TRK_DEF): the four tracks get GLCH SICK ENGINE on PISTON,
 * FM6 BROKEN PIANO on DREAD, PHASE SICK PAD on DRONE, DRUM FACTORY on WRECK; PLAY sounds at once, every track is heard,
 * nothing clips. build/host/krst_poweron_test [WAV]: the first four bars after PLAY */
#define KRST_DEMO_DEF 1
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int bad;
static void ck(const char *what, int ok)
{
    printf("poweron: %-80s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

int main(int argc, char **argv)
{
    static int32_t out[2 * CTL];
    uint32_t k, i, frames = 4u * 4u * FS / 2u / CTL * CTL, heard = 0;
    int32_t pk = 0;
    double s = 0;
    FILE *f = argc > 1 ? fopen(argv[1], "wb") : 0;
    ui_power_on();
    song.master_q12 = 3200;                             /* (the host: MASTER at about three quarters) */
    ck("the four sounds: GLCH SICK ENGINE, FM6 BROKEN PIANO, PHASE SICK PAD, DRUM FACTORY",
       str_eq(ENGINES[trk[0].eng_req]->presets[trk[0].preset].name, "SICK ENGINE") &&
       str_eq(ENGINES[trk[1].eng_req]->presets[trk[1].preset].name, "BROKEN PIANO") &&
       str_eq(ENGINES[trk[2].eng_req]->presets[trk[2].preset].name, "SICK PAD") &&
       str_eq(ENGINES[trk[3].eng_req]->presets[trk[3].preset].name, "FACTORY"));
    ck("  each with its pattern (PISTON, DREAD, DRONE, WRECK): no track empty",
       !seq_is_empty(&trk[0]) && !seq_is_empty(&trk[1]) && !seq_is_empty(&trk[2]) && !seq_is_empty(&trk[3]));
    if (f)
        wav_hdr(f, frames);
    seq_start();
    for (k = 0; k < frames / CTL; k++) {
        uint32_t v0 = vage;
        mix_block(out, CTL);
        for (; v0 < vage; v0++)
            for (i = 0; i < NPART; i++) {
                uint32_t j;
                for (j = 0; j < NVOICE; j++)
                    if (trk[i].v[j].age == v0 + 1u)
                        heard |= 1u << i;
            }
        for (i = 0; i < 2u * CTL; i++) {
            int32_t a = out[i] < 0 ? -out[i] : out[i];
            pk = a > pk ? a : pk;
            s += (double)out[i] * out[i];
        }
        if (f)
            for (i = 0; i < CTL; i++)
                wav_put(f, out[2u * i], out[2u * i + 1u]);
    }
    seq_stop();
    if (f)
        fclose(f);
    s = sqrt(s / (2.0 * frames));
    printf("poweron:   four bars after PLAY: rms %.0f, peak %d, tracks heard %x\n", s, pk, heard);
    ck("PLAY at power-on: all four tracks play, loud (rms > 2000), no clip past the soft clip", heard == 15u && s > 2000.0 &&
       pk <= 32767);
    printf("%s\n", bad ? "POWERON TEST FAILED" : "poweron: all ok");
    return bad != 0;
}
