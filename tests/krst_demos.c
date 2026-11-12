/* SPDX-License-Identifier: GPL-3.0-only */
/* KRIECHSTROM: the site's sound demos (tools/krst_demos.sh: build/demos/*.wav, then .m4a for the site), rendered by the
 * real sources: one short piece per feature, written for it (its sounds, tempo and parts), 8 bars: the first 2 dry,
 * then the feature comes in. Each piece is a phrase of 4 bars (64 steps on every track) played twice.
 *   damage    D minor, 84 BPM: a broken piano's arpeggios over a sub bass and a half-time beat; DAMAGE on tape
 *             from bar 3, digital from bar 6
 *   infect    A minor, 120 BPM: a sidechain: a kick on a silent track pumps the pad (DUCK, bar 3), the drums crush
 *             the bass (bar 5)
 *   vox       A minor, 110 BPM: a bright pad and metal drums; from bar 3 the drums speak through the pad (VOX)
 *   howl      C minor, 90 BPM: a slow melody in long notes over a sub and a half-time beat; the melody turns into
 *             GLCH FEEDBACK (bar 3), then SCREAMER (bar 6)
 *   resonate  A minor, 100 BPM: sparse: plucked stabs, a marimba, metal drums and silence between; RESONATE in A minor
 *             from bar 3
 *   dry       the damage piece without DAMAGE (a reference for the measurements)
 * Each file is brought to the same loudness as a whole (one gain: -14 dBFS RMS, its peak at most -1 dBFS), so the
 * demos play at one level and each keeps its before and after as they are.
 *   build/host/krst_demos DIR */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

#define BARS 8u

#define MAXF (8u * 4u * 60u * FS / 80u + CTL)           /* 8 bars at 80 BPM and up, frames */
static int16_t pcm[2u * MAXF];
static int32_t raw[2u * MAXF];

static uint32_t pidx(uint32_t e, const char *name)
{
    uint32_t i;
    for (i = 0; i < ENGINES[e]->npresets; i++)
        if (str_eq(ENGINES[e]->presets[i].name, name))
            return i;
    fprintf(stderr, "demos: no sound %s\n", name);
    exit(1);
}
static void sound(uint32_t k, uint32_t e, const char *name, int level)
{
    host_preset(&trk[k], e, pidx(e, name));
    trk[k].p[P_LEVEL] = (int16_t)level;
}
static void infct(uint32_t k, int src, int mode, int depth, int mix)
{
    track_t *t = &trk[k];
    t->p[P_ITYPE] = IT_INFECT;
    t->p[P_IA] = src;
    t->p[P_IB] = mode;
    t->p[P_IC] = depth;
    t->p[P_IMIX] = mix;
}
/* a note (or chord, n notes) on track k from step s, held len steps */
static void note(uint32_t k, uint32_t s, uint32_t len, uint32_t n, const uint8_t *nt, uint32_t acc)
{
    uint32_t i;
    for (i = 0; i < len && s + i < 64u; i++)
        put_step(&trk[k], s + i, n, nt, i ? ST_TIE : ST_NOTE, !i && acc ? SF_ACCENT : 0u);
}
static void note1(uint32_t k, uint32_t s, uint32_t len, uint8_t nt) { note(k, s, len, 1, &nt, 0); }
/* the drums (track 4): lane bits (1 KICK, 2 SNARE, 4 CLAP, 8 HAT, 16 OPEN HAT, 32 TOM, 64 RIM, 128 BELL) on step s */
static void hit(uint32_t s, uint32_t lanes, uint32_t vel)
{
    if (!trk[3].step[s].hit)
        put_step(&trk[3], s, 0, 0, ST_NOTE, 0);
    trk[3].step[s].hit |= (uint8_t)lanes;
    trk[3].step[s].vel = (uint8_t)(vel > trk[3].step[s].vel ? vel : trk[3].step[s].vel);
}
static void clear_all(void)
{
    uint32_t k;
    for (k = 0; k < NPART; k++) {
        memset(trk[k].step, 0, sizeof trk[k].step);
        trk[k].p[P_SLEN] = 64;
        trk[k].seq_active = 1;
        trk[k].p[P_ITYPE] = 0;
    }
}

/* ---- damage: D minor, Dm Bb F C ---- */
static void damage_song(void)
{
    static const uint8_t ROOT[4] = {38, 34, 41, 36};
    static const uint8_t ARP[4][4] = {{62, 65, 69, 65}, {58, 62, 65, 62}, {57, 60, 65, 60}, {55, 60, 64, 60}};
    static const uint8_t PAD[4][3] = {{50, 57, 62}, {46, 53, 58}, {48, 53, 57}, {48, 52, 55}};
    uint32_t b, q, s;
    song.g[G_BPM] = 84;
    sound(0, 0, "SUB BASS", 100);
    sound(1, 12, "BROKEN PIANO", 105);
    sound(2, 2, "HYMN", 70);
    sound(3, 10, "DRY BONES", 100);
    clear_all();
    for (b = 0; b < 4u; b++) {
        note1(0, b * 16u, 16, ROOT[b]);
        note(2, b * 16u, 16, 3, PAD[b], 0);
        for (q = 0; q < 4u; q++)
            note1(1, b * 16u + q * 4u, 3, ARP[b][q]);
        hit(b * 16u, 1, 120);
        hit(b * 16u + 10u, 1, 100);
        hit(b * 16u + 8u, 2, 115);
        for (s = 0; s < 16u; s += 2u)
            hit(b * 16u + s, 8, s % 4u ? 70 : 90);
    }
}
static void damage_at(uint32_t b)
{
    if (b == 2u) { song.g[G_DMG] = 100; song.g[G_DMGC] = 0; }
    if (b == 5u) { song.g[G_DMG] = 110; song.g[G_DMGC] = 127; }
}
static void dry_at(uint32_t b) { (void)b; }

/* ---- infect: A minor; a sidechain: a silent kick on track 2 pumps the pad, the drums crush the bass ---- */
static void infect_song(void)
{
    static const uint8_t ROOT[4] = {33, 33, 29, 31};             /* A1 A1 F1 G1 */
    static const uint8_t PAD[4][3] = {{57, 60, 64}, {57, 60, 64}, {53, 57, 60}, {55, 59, 62}};
    uint32_t b, s;
    song.g[G_BPM] = 120;
    sound(0, 7, "REESE", 90);
    sound(1, 10, "DRUM KIT", 0);                                 /* (the trigger: a kick nobody hears) */
    sound(2, 2, "SICK PAD", 110);
    sound(3, 10, "FACTORY", 100);
    clear_all();
    for (b = 0; b < 4u; b++) {
        note1(0, b * 16u, 16, ROOT[b]);
        note(2, b * 16u, 16, 3, PAD[b], 0);
        for (s = 0; s < 16u; s += 4u) {
            put_step(&trk[1], b * 16u + s, 0, 0, ST_NOTE, 0);
            trk[1].step[b * 16u + s].hit = 1u;
            trk[1].step[b * 16u + s].vel = 120;
            hit(b * 16u + s, 1, 120);
            hit(b * 16u + s + 2u, 16, 80);
        }
        hit(b * 16u + 4u, 2, 115);
        hit(b * 16u + 12u, 2, 115);
    }
}
static void infect_at(uint32_t b)
{
    if (b == 2u) infct(2, 1, IFX_DUCK, 120, 127);               /* the pad pumps with the silent kick */
    if (b == 4u) infct(0, 3, IFX_CRUSH, 115, 127);              /* the drums crush the bass */
}

/* ---- vox: A minor; the drums speak through a bright pad (INFECT VOX) ---- */
static void vox_song(void)
{
    static const uint8_t PAD[4][3] = {{57, 64, 69}, {57, 64, 69}, {53, 60, 65}, {55, 62, 67}};
    static const uint8_t ROOT[4] = {33, 33, 29, 31};
    uint32_t b, s;
    song.g[G_BPM] = 110;
    sound(0, 0, "SUB BASS", 95);
    sound(1, 0, "SAW LEAD", 0);
    sound(2, 0, "RAVE", 105);
    sound(3, 10, "METAL SHOP", 95);
    clear_all();
    for (b = 0; b < 4u; b++) {
        note1(0, b * 16u, 16, ROOT[b]);
        note(2, b * 16u, 16, 3, PAD[b], 0);
        for (s = 0; s < 16u; s++) {
            uint32_t h = 0;
            if (s == 0u || s == 7u || s == 10u) h |= 1u;
            if (s == 4u || s == 12u) h |= 2u;
            if (s % 2u == 0u) h |= 8u;
            if (s == 14u || (b == 3u && s >= 12u)) h |= 64u;
            if (h)
                hit(b * 16u + s, h, (h & 3u) ? 118 : 80);
        }
    }
}
static void vox_at(uint32_t b) { if (b == 2u) infct(2, 3, IFX_VOX, 127, 127); }

/* ---- howl: C minor, a slow melody ---- */
static void howl_song(void)
{
    static const uint8_t ROOT[4] = {36, 32, 39, 31};             /* C2 Ab1 Eb2 G1 */
    static const struct { uint8_t s, len, n; } MEL[] = {
        {0, 8, 60}, {8, 8, 63},                                  /* C4 Eb4 */
        {16, 12, 67}, {28, 4, 65},                               /* G4 F4 */
        {32, 8, 63}, {40, 8, 62},                                /* Eb4 D4 */
        {48, 16, 60},                                            /* C4 */
    };
    uint32_t b, i;
    song.g[G_BPM] = 90;
    sound(0, 0, "SUB FLOOR", 100);
    sound(1, 2, "LAST LIGHT", 95);
    sound(2, 2, "COMA", 55);
    sound(3, 10, "BUNKER", 100);
    clear_all();
    for (b = 0; b < 4u; b++) {
        note1(0, b * 16u, 16, ROOT[b]);
        hit(b * 16u, 1, 120);
        hit(b * 16u + 8u, 2, 115);
        hit(b * 16u + 4u, 8, 70);
        hit(b * 16u + 12u, 8, 70);
        if (b == 3u)
            hit(b * 16u + 14u, 1, 100);
    }
    note1(2, 0, 64, 48);
    for (i = 0; i < NELEM(MEL); i++)
        note1(1, MEL[i].s, MEL[i].len, MEL[i].n);
}
static void howl_at(uint32_t b)
{
    if (b == 2u) sound(1, 7, "FEEDBACK", 95);
    if (b == 5u) sound(1, 7, "SCREAMER", 90);
}

/* ---- resonate: A minor, sparse ---- */
static void resonate_song(void)
{
    static const struct { uint8_t s, n; } STAB[] = {{0, 45}, {3, 45}, {16, 45}, {22, 48}, {32, 41}, {35, 41}, {48, 40}, {54, 43}};
    static const struct { uint8_t s, n; } MAR[] = {{6, 69}, {10, 72}, {26, 76}, {38, 72}, {42, 69}, {58, 71}};
    uint32_t b, i;
    song.g[G_BPM] = 100;
    sound(0, 0, "PLUCK", 105);
    sound(1, 12, "MARIMBA", 95);
    sound(2, 0, "SOFT PAD", 0);
    sound(3, 10, "METAL SHOP", 105);
    clear_all();
    for (i = 0; i < NELEM(STAB); i++)
        note1(0, STAB[i].s, 1, STAB[i].n);
    for (i = 0; i < NELEM(MAR); i++)
        note1(1, MAR[i].s, 1, MAR[i].n);
    for (b = 0; b < 4u; b++) {
        hit(b * 16u, 1, 120);
        hit(b * 16u + 8u, b % 2u ? 4u : 64u, 105);
        if (b == 1u || b == 3u)
            hit(b * 16u + 11u, 1, 95);
    }
}
static void resonate_at(uint32_t b)
{
    if (b == 2u) { song.g[G_RES] = 110; song.g[G_RKEY] = 9; song.g[G_RCHD] = 2; }   /* A MIN */
}

static const struct { const char *name; void (*song)(void); void (*at)(uint32_t); } DEMO[] = {
    {"dry", damage_song, dry_at}, {"damage", damage_song, damage_at},
    {"infect", infect_song, infect_at}, {"vox", vox_song, vox_at}, {"howl", howl_song, howl_at},
    {"resonate", resonate_song, resonate_at},
};

int main(int argc, char **argv)
{
    static int32_t out[2 * CTL];
    uint32_t d, k, i;
    if (argc < 2) {
        fprintf(stderr, "usage: krst_demos DIR\n");
        return 2;
    }
    for (d = 0; d < NELEM(DEMO); d++) {
        char path[512];
        FILE *f;
        int32_t pk = 0;
        double e = 0, g;
        uint32_t barb, nf = 0;
        snprintf(path, sizeof path, "%s/%s.wav", argv[1], DEMO[d].name);
        ui_power_on();
        song.master_q12 = 3200;                         /* (MASTER at about three quarters, as the power-on test) */
        DEMO[d].song();
        barb = 4u * 60u * FS / (uint32_t)song.g[G_BPM] / CTL;   /* a bar, in blocks */
        seq_start();
        for (k = 0; k < BARS * barb && nf + CTL <= MAXF; k++) {
            if (k % barb == 0u && !getenv("KRST_DEMO_DRY"))   /* (KRST_DEMO_DRY: none of it, a reference) */
                DEMO[d].at(k / barb);
            mix_block(out, CTL);
            for (i = 0; i < 2u * CTL; i++) {
                raw[2u * nf + i] = out[i];
                pk = out[i] > pk ? out[i] : -out[i] > pk ? -out[i] : pk;
                e += (double)out[i] * out[i];
            }
            nf += CTL;
        }
        seq_stop();
        g = 0.2 * 32768.0 / sqrt(e / (2.0 * nf) + 1.0);   /* (-14 dBFS RMS) */
        if (g * pk > 0.89 * 32767.0)                     /* (.. its peak at most -1 dBFS) */
            g = 0.89 * 32767.0 / pk;
        for (i = 0; i < 2u * nf; i++)
            pcm[i] = (int16_t)clamp((int32_t)lrint(raw[i] * g), -32767, 32767);
        if (!(f = fopen(path, "wb"))) {
            fprintf(stderr, "demos: cannot write %s\n", path);
            return 1;
        }
        wav_hdr(f, nf);
        for (i = 0; i < nf; i++)
            wav_put(f, pcm[2u * i], pcm[2u * i + 1u]);
        fclose(f);
        printf("demos: %s (%u bars at %d BPM, gain %+.1f dB)\n", path, (unsigned)BARS, song.g[G_BPM], 20 * log10(g));
    }
    return 0;
}
