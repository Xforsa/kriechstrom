/* SPDX-License-Identifier: GPL-3.0-only */
/* KRIECHSTROM: each of its own drum kits on the beat it was chosen on (KIT ROOM GATE MARCH ARC ROT SLAB), rendered
 * by the real sources: 4 bars, a 2-bar phrase twice, the second time with its fill. One drum track, the kit at its
 * design (the knobs at 64); a lane's '9' is an accented hit (velocity 127), the step's other hits share its
 * velocity (the largest of theirs). Each file at -14 dBFS RMS, its peak at most -1 dBFS.
 *   build/host/krst_kits_demo DIR [kit] [beat|track|tour]
 *     beat   the beat, 4 bars (DIR/kit_<name>.wav)
 *     track  8 bars: the beat alone for 2, then a bass and a synth from KRIECHSTROM's sounds (DIR/kit_<name>_track.wav)
 *     tour   every lane once on the quarters (KICK SNARE CLAP HAT CL HAT OP TOM RIM BELL), then 2 bars of the beat
 *            (DIR/kit_<name>_tour.wav) */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

typedef struct {
    const char *name;
    uint32_t kit, bpm, swing;                            /* KIT, BPM, SWG */
    const char *p[DV_NLANE], *f[DV_NLANE];               /* per lane: the phrase (32 steps), the fill (16) */
} kbeat_t;
/* lanes: KICK SNARE CLAP HAT CL HAT OP TOM RIM BELL */
static const kbeat_t KB[] = {
    {"room", 13, 100, 0,   /* a heavy rock groove with ghost notes, the toms falling in at the end */
     {"9.....6.9.9.....9.....6.9...7...", "....9..2....9..3....9..2....9.2.", "................................",
      "7.5.7.5.7.5.7.5.7.5.7.5.7.5.7...", "..............................9.", "................................",
      "................................", "................................"},
     {"9.....6.9.......", "....9..2....9999", "................", "7.5.7.5.7.5.....", "................", "........9.7.9.7.",
      "................", "................"}},
    {"gate", 14, 128, 0,   /* half time: the gated snare on 3, the kick dragging around it, closed hats in sixteenths */
     {"9.....9...7.....9..7......9.....", "........9...............9.......", "................................",
      "7575757575757575757575757575757.", "...............................9", "................................",
      "................................", "................................"},
     {"9.....9...7.....", "........9...9.99", "................", "7575757575757575", "................", "............7.99",
      "................", "................"}},
    {"march", 15, 112, 0,   /* kick and boots stamping in step, the crack on 2 and 4, war drums answering */
     {"9...9...9...9...9.9.9...9...9.9.", "....9.......9.......9.......9...", "9...9...9...9...9...9...9...9...",
      "..5...5...5...5...5...5...5...5.", "................................", "..........7...............7.....",
      "................................", ".............7...............77."},
     {"9...9...99999999", "....9.......9...", "9...9...........", "..5...5.........", "................", "........9.9.....",
      "....9...9.......", "............9.99"}},
    {"arc", 16, 120, 10,   /* electro-industrial: a syncopated kick, the snare on 2 and 4 */
     {"9.....9...9.....9..9..9...9.....", "....9.......9.......9.......9..5", "................................",
      "7.5.7.5.7.5.7.5.7.5.7.5.7.5.7.5.", "..............9.................", "................................",
      "...4.......4.......4.......4....", "................................"},
     {"9.....9...9..9..", "....9.......9.99", "................", "7.5.7.5.7.5.....", "................", "............7899",
      "...4............", "................"}},
    {"rot", 17, 108, 20,   /* broken: a syncopated kick, ghost snares, hats that stutter, a swell into the turn */
     {"9.....7.9.....5.9..7....9....6..", "....9..3....9.3.....9..3...39..3", "........................9.......",
      "7.5.7.5.7.5.7557.5.7.5.7.5.7.5..", "..............................9.", "................................",
      "...........5...............5....", "..............................9."},
     {"9.....7.9...9.9.", "....9..3....9999", "................", "7.5.7.5.75757575", "................", "........9.7.9...",
      "...........5....", "..............9."}},
    {"slab", 18, 140, 0,   /* power electronics: relentless sixteenth noise, the kick doubled, toms crashing in */
     {"9.9.9.9.9.9.9.9.9.9.9.9.9.99.9.9", "....9.......9.......9.......9...", "....9.......9.......9.......9...",
      "75757575757575757575757575757575", "......9.......9.......9.......9.", "...........7..............7.....",
      ".........................5.5.5.5", "................................"},
     {"9.9.9.9.9.9.9.99", "....9.......9999", "....9.......9...", "7575757575757575", "......9.........", "........9799.9.9",
      "................", "................"}},
};

#define KD_BARS 4u
#define KD_MAXF (8u * 4u * 60u * FS / 84u + CTL)
static int32_t raw[2u * KD_MAXF];
static int16_t pcm[2u * KD_MAXF];

static uint32_t drum_idx(void)
{
    uint32_t e;
    for (e = 0; e < NENGINES; e++)
        if (ENGINES[e] == &ENG_DRUM)
            return e;
    return 0;
}
static void kd_song(const kbeat_t *b)
{
    track_t *t = &trk[3];
    uint32_t k, s, l, e = drum_idx();
    song.g[G_BPM] = (int16_t)b->bpm;
    for (k = 0; k < NPART; k++) {
        memset(trk[k].step, 0, sizeof trk[k].step);
        trk[k].p[P_SLEN] = 64;
        trk[k].seq_active = 1;
        trk[k].p[P_LEVEL] = 0;
        trk[k].p[P_ITYPE] = 0;
    }
    host_preset(t, e, 0);
    t->p[P_E0] = (int16_t)b->kit;
    t->p[P_LEVEL] = 100;
    t->p[P_SSWING] = (int16_t)b->swing;
    t->p[P_DLY] = t->p[P_REV] = t->p[P_CHOR] = t->p[P_DIST] = 0;
    for (s = 0; s < 64u; s++) {
        uint32_t bar = s / 16u, pos = s % 16u, hit = 0, acc = 0, vel = 0;
        for (l = 0; l < DV_NLANE; l++) {
            char c = bar == 3u ? b->f[l][pos] : b->p[l][(bar % 2u) * 16u + pos];
            if (c < '1' || c > '9')
                continue;
            hit |= 1u << l;
            if (c == '9')
                acc |= 1u << l;
            else if ((uint32_t)(c - '0') > vel)
                vel = (uint32_t)(c - '0');
        }
        if (!hit)
            continue;
        put_step(t, s, 0, 0, ST_NOTE, 0);
        t->step[s].hit = (uint8_t)hit;
        t->step[s].acc = (uint8_t)acc;
        t->step[s].vel = (uint8_t)(vel ? vel * 127u / 9u : 127u);
    }
}

/* ---- track: a bass (track 1) and a synth (track 2) for each kit, E minor; notes per sixteenth over 2 bars, 0 rest ---- */
typedef struct { const char *beng, *bpre, *seng, *spre; uint8_t bass[32], syn[32]; uint8_t slen; } kctx_t;
static const kctx_t KCTX[] = {
    {"GLCH", "FUZZ BASS", "PHASE", "SICK PAD",          /* room: a fuzz riff in eighths, a pad */
     {28, 0, 28, 0, 31, 0, 28, 0, 33, 0, 31, 0, 28, 0, 26, 0, 28, 0, 28, 0, 31, 0, 35, 0, 33, 0, 31, 0, 28, 0, 26, 0},
     {52, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 48, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 16},
    {"ANALOG", "IRON LUNG", "PHASE", "SCREAM LEAD",     /* gate: a long low bass, a scream on 3 */
     {28, 0, 0, 0, 0, 0, 0, 0, 28, 0, 0, 0, 31, 0, 0, 0, 26, 0, 0, 0, 0, 0, 0, 0, 26, 0, 0, 0, 24, 0, 0, 0},
     {0, 0, 0, 0, 0, 0, 0, 0, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 62, 0, 0, 0, 0, 0, 0, 0}, 6},
    {"ANALOG", "HAMMER", "ANALOG", "COLD SQUARE",       /* march: the bass on the beats, a cold line */
     {28, 0, 0, 0, 28, 0, 0, 0, 28, 0, 0, 0, 31, 0, 0, 0, 28, 0, 0, 0, 28, 0, 0, 0, 26, 0, 0, 0, 23, 0, 0, 0},
     {64, 0, 0, 0, 0, 0, 0, 0, 67, 0, 0, 0, 0, 0, 0, 0, 66, 0, 0, 0, 0, 0, 0, 0, 62, 0, 0, 0, 0, 0, 0, 0}, 6},
    {"GLCH", "GRIT BASS", "PHASE", "SCREAM LEAD",       /* arc: a grit bass in sixteenths, stabs */
     {28, 28, 40, 28, 28, 31, 28, 40, 28, 28, 40, 28, 33, 31, 28, 26, 28, 28, 40, 28, 28, 31, 28, 40, 35, 33, 31, 28, 26, 28, 31, 33},
     {0, 0, 64, 0, 0, 0, 0, 0, 0, 0, 67, 0, 0, 0, 0, 0, 0, 0, 64, 0, 0, 0, 0, 0, 0, 0, 62, 0, 0, 0, 0, 0}, 2},
    {"ANALOG", "PISTON", "PHASE", "SICK PAD",           /* rot: a broken bass, a pad */
     {28, 0, 0, 28, 0, 0, 40, 0, 28, 0, 31, 0, 0, 33, 0, 0, 28, 0, 0, 28, 0, 0, 40, 0, 26, 0, 0, 0, 23, 0, 26, 0},
     {55, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 52, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 16},
    {"GLCH", "FUZZ BASS", "GLCH", "SCREAM",             /* slab: a drone in sixteenths, a scream */
     {28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 29, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 31, 29, 28, 26},
     {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 52, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 8},
};
static uint32_t eng_named(const char *n)
{
    uint32_t e;
    for (e = 0; e < NENGINES; e++)
        if (ENGINES[e] && str_eq(ENGINES[e]->name, n))
            return e;
    fprintf(stderr, "kits: no engine %s\n", n);
    exit(1);
}
static uint32_t pre_named(uint32_t e, const char *n)
{
    uint32_t i;
    for (i = 0; i < ENGINES[e]->npresets; i++)
        if (str_eq(ENGINES[e]->presets[i].name, n))
            return i;
    fprintf(stderr, "kits: no sound %s\n", n);
    exit(1);
}
static void kd_track(uint32_t d)
{
    const kctx_t *c = &KCTX[d];
    uint32_t eb = eng_named(c->beng), es = eng_named(c->seng), s, k;
    host_preset(&trk[0], eb, pre_named(eb, c->bpre));
    host_preset(&trk[1], es, pre_named(es, c->spre));
    for (k = 0; k < 2u; k++) {
        memset(trk[k].step, 0, sizeof trk[k].step);
        trk[k].p[P_SLEN] = 64;
        trk[k].seq_active = 1;
        trk[k].p[P_LEVEL] = 0;                           /* (in from bar 3: kd_at) */
    }
    for (s = 0; s < 64u; s++) {
        uint8_t b = c->bass[s % 32u], y = c->syn[s % 32u];
        uint32_t i;
        if (b)
            put_step(&trk[0], s, 1, &b, ST_NOTE, s % 4u == 0u ? SF_ACCENT : 0u);
        if (y)
            for (i = 0; i < c->slen && s + i < 64u; i++)
                put_step(&trk[1], s + i, 1, &y, i ? ST_TIE : ST_NOTE, 0);
    }
}
/* tour: lane l on the quarter l (bars 1 and 2), then the beat's phrase (bars 3, 4) */
static void kd_tour(const kbeat_t *b)
{
    track_t *t = &trk[3];
    uint32_t s, l;
    memset(t->step, 0, sizeof t->step);
    for (l = 0; l < DV_NLANE; l++) {
        put_step(t, l * 4u, 0, 0, ST_NOTE, 0);
        t->step[l * 4u].hit = (uint8_t)(1u << l);
        t->step[l * 4u].acc = (uint8_t)(1u << l);
        t->step[l * 4u].vel = 127;
    }
    for (s = 32; s < 64u; s++) {
        uint32_t pos = s % 16u, bar = (s - 32u) / 16u, hit = 0, acc = 0, vel = 0;
        for (l = 0; l < DV_NLANE; l++) {
            char c = b->p[l][bar * 16u + pos];
            if (c < '1' || c > '9')
                continue;
            hit |= 1u << l;
            if (c == '9')
                acc |= 1u << l;
            else if ((uint32_t)(c - '0') > vel)
                vel = (uint32_t)(c - '0');
        }
        if (!hit)
            continue;
        put_step(t, s, 0, 0, ST_NOTE, 0);
        t->step[s].hit = (uint8_t)hit;
        t->step[s].acc = (uint8_t)acc;
        t->step[s].vel = (uint8_t)(vel ? vel * 127u / 9u : 127u);
    }
}

int main(int argc, char **argv)
{
    static int32_t out[2 * CTL];
    uint32_t d, k, i;
    const char *mode = argc > 3 ? argv[3] : "beat";
    int track = !strcmp(mode, "track"), tour = !strcmp(mode, "tour");
    uint32_t bars = track ? 8u : 4u;
    if (argc < 2) {
        fprintf(stderr, "usage: krst_kits_demo DIR [kit|all] [beat|track|tour]\n");
        return 2;
    }
    for (d = 0; d < NELEM(KB); d++) {
        char path[512];
        FILE *f;
        int32_t pk = 0;
        double e = 0, g;
        uint32_t nf = 0, barb;
        if (argc > 2 && strcmp(argv[2], "all") && strcmp(argv[2], KB[d].name))
            continue;
        snprintf(path, sizeof path, "%s/kit_%s%s.wav", argv[1], KB[d].name, track ? "_track" : tour ? "_tour" : "");
        ui_power_on();
        song.master_q12 = 3200;
        kd_song(&KB[d]);
        if (track)
            kd_track(d);
        if (tour)
            kd_tour(&KB[d]);
        barb = 4u * 60u * FS / KB[d].bpm / CTL;
        seq_start();
        for (k = 0; k < bars * barb && nf + CTL <= KD_MAXF; k++) {
            if (track && k == 2u * barb)                 /* bar 3: the bass and the synth come in */
                trk[0].p[P_LEVEL] = 100, trk[1].p[P_LEVEL] = 80;
            mix_block(out, CTL);
            for (i = 0; i < 2u * CTL; i++) {
                raw[2u * nf + i] = out[i];
                pk = out[i] > pk ? out[i] : -out[i] > pk ? -out[i] : pk;
                e += (double)out[i] * out[i];
            }
            nf += CTL;
        }
        seq_stop();
        g = 0.2 * 32768.0 / sqrt(e / (2.0 * nf) + 1.0);
        if (g * pk > 0.89 * 32767.0)
            g = 0.89 * 32767.0 / pk;
        for (i = 0; i < 2u * nf; i++)
            pcm[i] = (int16_t)clamp((int32_t)lrint(raw[i] * g), -32767, 32767);
        if (!(f = fopen(path, "wb"))) {
            fprintf(stderr, "kits: cannot write %s\n", path);
            return 1;
        }
        wav_hdr(f, nf);
        for (i = 0; i < nf; i++)
            wav_put(f, pcm[2u * i], pcm[2u * i + 1u]);
        fclose(f);
        printf("kits: %s (%u BPM, peak %.1f dBFS raw, gain %+.1f dB)\n", path, (unsigned)KB[d].bpm,
               20 * log10(pk / 32768.0 + 1e-9), 20 * log10(g));
    }
    return 0;
}
