/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 KRIECHSTROM */
/* KRIECHSTROM: DICE, a new part for the selected track in a MOOD, in the track's key (ROOT, SCL). In SEQ TOOLS (SEQ
 * held): F5 DICE, G5 the next MOOD. Included by ui_tools.c (its helpers: tl_rnd, tl_neigh, tl_in_scale, step_clear,
 * grid_hit, grid_acc); one undo, as its other tools.
 *   MACHINE   the sequencer drives it: sixteenths of bass, a straight beat, a stab
 *   GRIND     slow and heavy, half time: one huge snare, a late bass sliding down
 *   MARCH 7/8 fast, 14 steps, pounding
 *   GLITCH    broken beats, crushed bleeps, hits anywhere
 *   SPIRAL    falling a semitone at a time, toms
 *   GHOST     a piano alone, a held drone, almost no drums
 *   SURGE     fast and all out
 *   HUSH      the quiet verse: a soft kick, a chord held, nothing else
 * What a track gets: a DRUM track a beat on the grid's lanes; the others by their number, T1 the bass, T2 the keys
 * (stabs, chords, a held chord), T3 and T4 a high line (hits, a riser, a drone). The MOOD is not saved: MACHINE at
 * power-on. */
enum { MOOD_MACHINE, MOOD_GRIND, MOOD_MARCH, MOOD_GLITCH, MOOD_SPIRAL, MOOD_GHOST, MOOD_SURGE, MOOD_HUSH, MOOD_N };
static const char *const MOOD_NAME[MOOD_N] = {"MACHINE", "GRIND", "MARCH 7/8", "GLITCH", "SPIRAL", "GHOST", "SURGE",
                                              "HUSH"};
static uint8_t dice_mood;

static uint32_t dc_bits(const char *s)          /* "x...x..." -> step bits */
{
    uint32_t b = 0, i;
    for (i = 0; i < 16u && s[i]; i++)
        if (s[i] != '.') b |= 1u << i;
    return b;
}
static int dc_ch(uint32_t pct) { return tl_rnd(100) < pct; }
static const char *dc_pick(const char *const *a) { return a[tl_rnd(3)]; }

/* the note d scale steps from n (n in the scale; d any sign), in the track's scale */
static int32_t dc_deg(const track_t *t, int32_t n, int32_t d)
{
    for (; d > 0; d--) n = tl_neigh(t, n, 1);
    for (; d < 0; d++) n = tl_neigh(t, n, -1);
    return n;
}
/* the track's root near a register: lo .. lo + 11 */
static int32_t dc_root(const track_t *t, int32_t lo) { return lo + (t->p[P_ROOT] - lo % 12 + 120) % 12; }

static void dc_note(step_t *s, int32_t n, uint32_t f)       /* a NOTE step; f: SF_ACCENT, SF_SLIDE */
{
    step_clear(s);
    s->time = ST_NOTE;
    s->n = 1;
    s->note[0] = (uint8_t)clamp(n, 12, 115);
    s->vel = (uint8_t)(f & SF_ACCENT ? 120 : 92);
    s->flags = (uint8_t)(f & (SF_ACCENT | SF_SLIDE));
}
/* a chord on n: 1 the fifth, 2 the scale's triad (in key: minor or major as the scale has it), 3 its seventh too */
static void dc_chord(step_t *s, const track_t *t, int32_t n, uint32_t kind, uint32_t f)
{
    dc_note(s, n, f);
    if (kind == 1u) {
        s->note[1] = (uint8_t)clamp(n + 7, 12, 115);
        s->n = 2;
    } else if (kind >= 2u) {
        s->note[1] = (uint8_t)dc_deg(t, n, 2);
        s->note[2] = (uint8_t)dc_deg(t, n, 4);
        s->n = 3;
        if (kind == 3u) {
            s->note[3] = (uint8_t)dc_deg(t, n, 6);
            s->n = 4;
        }
    }
}
static void dc_tie(step_t *s)                   /* held on from the step before */
{
    step_clear(s);
    s->time = ST_TIE;
}
/* steps a .. b - 1 held from a NOTE at a (a chord or a note set there already) */
static void dc_hold(track_t *t, uint32_t a, uint32_t b)
{
    uint32_t i;
    for (i = a + 1u; i < b; i++) dc_tie(&t->step[i]);
}

/* ------------------------------------------------------------------------------------------------- DRUM --- */
static void dc_drums(track_t *t, uint32_t len, uint32_t m)
{
    static const char *const KICK[MOOD_N][3] = {
        {"x...x...x...x...", "x...x...x...x..x", "x...x...x..xx..."},
        {"x......x..x.....", "x.........x.....", "x..x......x..x.."},
        {"x.x...x.x.x...", "x.x...x.xx.x..", "x.x..xx.x.x..."},
        {"x..x.....x..x...", "x....x.x..x....x", "xx.....x..x..x.."},
        {"x.......x.......", "x......x..x.....", "x.......x.....x."},
        {"x...............", "................", "x.......x......."},
        {"x...x...x...x...", "x..xx...x..xx...", "x.x.x...x.x.x..x"},
        {"x.......x.......", "x.......x...x...", "x.........x....."}};
    static const char *const SNARE[MOOD_N][3] = {
        {"....x.......x...", "....x.......x...", "....x.......x..x"},
        {"........x.......", "........x.......", "........x.....x."},
        {"....x.....x...", "....x.....x..x", "....x...x.x..."},
        {"...x.....x....x.", ".....x..x...x...", "....x..x...x.x.."},
        {"....x.......x...", "........x.......", "....x.......x.x."},
        {"................", "................", "................"},
        {"....x.......x...", "....x..x....x...", "....x.......xxxx"},
        {"................", "............x...", "................"}};
    static const char *const TOMS[3] = {"..........x.x.x.", "...x......x.....", "............xxx."};
    uint32_t lane[NLANE] = {0}, all = (1u << len) - 1u, i, l, acc;
    lane[DV_KICK] = dc_bits(KICK[m][tl_rnd(3)]);
    lane[DV_SNARE] = dc_bits(SNARE[m][tl_rnd(3)]);
    switch (m) {
    case MOOD_MACHINE:
        lane[DV_HATC] = dc_bits("..x...x...x...x.");
        if (dc_ch(50)) lane[DV_CLAP] = lane[DV_SNARE];
        break;
    case MOOD_GRIND:
        lane[DV_HATC] = dc_bits("x.x.x.x.x.x.x.x.") & (rng() | dc_bits("x...x...x...x..."));
        if (dc_ch(50)) lane[DV_HATO] = dc_bits("......x.......x.");
        break;
    case MOOD_MARCH:
        lane[DV_HATC] = all;
        if (dc_ch(50)) lane[DV_TOM] = dc_bits("...........xxx");
        break;
    case MOOD_GLITCH:
        lane[DV_HATC] = rng() & rng() & all;
        lane[DV_RIM] = rng() & rng() & rng() & all;
        lane[DV_CLAP] = 1u << tl_rnd(16);
        break;
    case MOOD_SPIRAL:
        lane[DV_HATC] = dc_bits("..x...x...x...x.");
        lane[DV_TOM] = dc_bits(dc_pick(TOMS));
        break;
    case MOOD_GHOST:
        lane[DV_BELL] = 1u << (4u + tl_rnd(3) * 4u);
        break;
    case MOOD_SURGE:
        lane[DV_HATC] = all;
        lane[DV_HATO] = dc_bits("..x...x...x...x.");
        lane[DV_CLAP] = lane[DV_SNARE];
        lane[DV_RIM] = dc_bits("x...............");
        break;
    default:                                    /* HUSH */
        lane[DV_HATC] = dc_bits(dc_ch(50) ? "....x.......x..." : "..x.......x.....");
        break;
    }
    if (m != MOOD_GHOST && m != MOOD_HUSH)      /* the metal (BELL): where the kick and the snare leave room */
        for (i = 0; i < len; i++)
            if (!((lane[DV_KICK] | lane[DV_SNARE]) >> i & 1u) && dc_ch(m == MOOD_GLITCH || m == MOOD_MARCH ? 18 : 10))
                lane[DV_BELL] |= 1u << i;
    acc = (lane[DV_KICK] | lane[DV_SNARE]) & (dc_bits("x...x...x...x...") | rng());
    for (i = 0; i < len; i++) {
        step_clear(&t->step[i]);
        for (l = 0; l < NLANE; l++)
            if ((lane[l] & all) >> i & 1u) {
                grid_hit(t, i, l, 1);
                if ((l == DV_KICK || l == DV_SNARE) && (acc >> i & 1u)) grid_acc(t, i, l, 1);
            }
    }
}

/* ------------------------------------------------------------------------------------------------- BASS --- */
static void dc_bass(track_t *t, uint32_t len, uint32_t m)
{
    int32_t r = dc_root(t, 36);
    uint32_t i;
    for (i = 0; i < len; i++) step_clear(&t->step[i]);
    switch (m) {
    case MOOD_MACHINE: {                        /* the sequencer: sixteenths on the root, the flat second, the fifth */
        static const char *const ALT[3] = {"...x...x..x....x", ".x.....x.x.....x", "...x..x....x..x."};
        uint32_t a = dc_bits(dc_pick(ALT));
        int32_t other = dc_ch(50) ? r + 1 : r + 7;
        for (i = 0; i < len; i++)
            dc_note(&t->step[i], a >> i & 1u ? (dc_ch(60) ? r + 12 : other) : r, i % 4u == 0u ? SF_ACCENT : 0u);
        break;
    }
    case MOOD_GRIND: {                          /* heavy and late, sliding down into the flat second */
        static const char *const R[3] = {"x..x..x...x.x...", "x.....x.x..x....", "x..x....x.x..x.."};
        uint32_t w = dc_bits(dc_pick(R));
        for (i = 0; i < len; i++)
            if (w >> i & 1u)
                dc_note(&t->step[i], r + (i >= 8u && dc_ch(50) ? 1 : 0) + (dc_ch(15) ? 12 : 0),
                        (dc_ch(30) ? SF_SLIDE : 0u) | (i == 0u ? SF_ACCENT : 0u));
        break;
    }
    case MOOD_MARCH:                            /* pounding, the flat second at the end */
        for (i = 0; i < len; i++)
            if (i % 2u == 0u || dc_ch(35))
                dc_note(&t->step[i], r + (dc_ch(20) ? 12 : 0) + (i >= 10u && dc_ch(40) ? 1 : 0), SF_ACCENT);
        break;
    case MOOD_GLITCH:
        for (i = 0; i < len; i++)
            if (dc_ch(45))
                dc_note(&t->step[i], dc_deg(t, r, (int32_t)tl_rnd(3) * 2) + (dc_ch(20) ? 12 : 0),
                        dc_ch(40) ? SF_ACCENT : 0u);
        break;
    case MOOD_SPIRAL:                           /* down a semitone each beat */
        for (i = 0; i < len; i++)
            if (i % 2u == 0u || dc_ch(30))
                dc_note(&t->step[i], r + 12 - (int32_t)(i / 4u), (i % 4u == 0u ? SF_ACCENT : 0u) |
                        (i % 4u == 3u ? SF_SLIDE : 0u));
        break;
    case MOOD_GHOST:                            /* the root, held all bar */
        dc_note(&t->step[0], r, 0);
        dc_hold(t, 0, len);
        break;
    case MOOD_SURGE:                            /* off the beat, the offbeats accented */
        for (i = 0; i < len; i++)
            if (i % 4u != 0u || dc_ch(30))
                dc_note(&t->step[i], r + (dc_ch(20) ? 7 : 0) + (dc_ch(20) ? 12 : 0), i % 2u ? SF_ACCENT : 0u);
        break;
    default:                                    /* HUSH: eighths, the second half on the sixth degree below */
        for (i = 0; i < len; i += 2u)
            dc_note(&t->step[i], i >= 8u ? dc_deg(t, r, 5) - 12 : r, 0);
        break;
    }
}

/* ------------------------------------------------------------------------------------------------- KEYS --- */
static void dc_keys(track_t *t, uint32_t len, uint32_t m)
{
    int32_t r = dc_root(t, 55);
    uint32_t i;
    for (i = 0; i < len; i++) step_clear(&t->step[i]);
    switch (m) {
    case MOOD_MACHINE:
    case MOOD_SURGE: {                          /* a stab off the beat: the fifth, the chord */
        static const char *const W[3] = {"......x.......x.", "...x......x.....", "..x.....x.....x."};
        uint32_t w = dc_bits(dc_pick(W));
        for (i = 0; i < len; i++)
            if (w >> i & 1u)
                dc_chord(&t->step[i], t, r + (dc_ch(30) ? 1 : 0), m == MOOD_SURGE ? 1u : 2u, dc_ch(50) ? SF_ACCENT : 0u);
        break;
    }
    case MOOD_GRIND:
    case MOOD_HUSH: {                           /* a low chord, held to half the bar, then another */
        int32_t second = m == MOOD_HUSH ? dc_deg(t, r, 5) - 12 : r + 1;
        dc_chord(&t->step[0], t, r - (m == MOOD_GRIND ? 12 : 0), 2, 0);
        dc_hold(t, 0, len / 2u);
        dc_chord(&t->step[len / 2u], t, second - (m == MOOD_GRIND ? 12 : 0), 2, 0);
        dc_hold(t, len / 2u, len);
        break;
    }
    case MOOD_MARCH:                            /* the riff: the root and the flat second, high, fifths */
        for (i = 0; i < len; i++)
            if (dc_ch(40))
                dc_chord(&t->step[i], t, r + 12 + (dc_ch(40) ? 1 : 0), 1, dc_ch(40) ? SF_ACCENT : 0u);
        break;
    case MOOD_GLITCH:                           /* bleeps from the scale, anywhere */
        for (i = 0; i < len; i++)
            if (dc_ch(30))
                dc_note(&t->step[i], dc_deg(t, r, (int32_t)tl_rnd(7)) + (dc_ch(30) ? 12 : 0), dc_ch(30) ? SF_ACCENT : 0u);
        break;
    case MOOD_SPIRAL:                           /* a falling line on top */
        for (i = 0; i < len; i += 2u)
            dc_note(&t->step[i], r + 12 - (int32_t)(i / 2u), dc_ch(30) ? SF_ACCENT : 0u);
        break;
    default: {                                  /* GHOST: a piano alone, a minor arpeggio, notes left to ring */
        static const int8_t ARP[4] = {0, 2, 4, 7};
        for (i = 0; i < len; i++)
            if (i % 2u == 0u ? dc_ch(80) : dc_ch(15))
                dc_note(&t->step[i], dc_deg(t, r, ARP[(i / 2u + (uint32_t)(i >= 8u)) % 4u]) - (i >= 8u ? 2 : 0),
                        i % 4u == 0u ? SF_ACCENT : 0u);
        break;
    }
    }
}

/* -------------------------------------------------------------------------------------- the high line --- */
static void dc_line(track_t *t, uint32_t len, uint32_t m)
{
    int32_t r = dc_root(t, 67);
    uint32_t i;
    for (i = 0; i < len; i++) step_clear(&t->step[i]);
    switch (m) {
    case MOOD_GHOST:
    case MOOD_HUSH:                             /* a drone held all bar */
        dc_note(&t->step[0], r, 0);
        dc_hold(t, 0, len);
        break;
    case MOOD_GLITCH:
        for (i = 0; i < len; i++)
            if (dc_ch(50)) dc_note(&t->step[i], r + (int32_t)tl_rnd(24) - 12, dc_ch(30) ? SF_ACCENT : 0u);
        break;
    case MOOD_MARCH:
    case MOOD_SURGE:                            /* on every beat, a riser into the next bar */
        for (i = 0; i + 4u < len; i += 4u) dc_note(&t->step[i], r, SF_ACCENT);
        for (i = len - 4u; i < len - 1u; i++) dc_note(&t->step[i], r + (int32_t)(i - (len - 4u)) * 5, SF_SLIDE);
        break;
    default: {                                  /* hits off the beat, now and then a long scrape */
        static const char *const W[3] = {"..x...x...x...x.", "......x.......x.", "...x.......x...."};
        uint32_t w = dc_bits(dc_pick(W));
        for (i = 0; i < len; i++)
            if (w >> i & 1u) dc_note(&t->step[i], r, 0);
        if (dc_ch(35) && len >= 16u) {
            dc_note(&t->step[12], r, 0);
            dc_hold(t, 12, 16);
        }
        break;
    }
    }
}

/* DICE on t (the caller took the undo copy): its LEN 16 (MARCH: 14), a part by its kind and number */
static const char *dice_do(track_t *t)
{
    static char b[24];
    uint32_t m = dice_mood % MOOD_N, len = m == MOOD_MARCH ? 14u : 16u, i = trk_index(t);
    t->p[P_SLEN] = (int16_t)len;
    if (drum_track(t)) dc_drums(t, len, m);
    else if (i == 0u) dc_bass(t, len, m);
    else if (i == 1u) dc_keys(t, len, m);
    else dc_line(t, len, m);
    str_cpy(b, "DICE: ", sizeof b);
    str_cpy(b + str_len(b), MOOD_NAME[m], sizeof b - str_len(b));
    return b;
}
static const char *mood_next(void)
{
    static char b[24];
    dice_mood = (uint8_t)((dice_mood + 1u) % MOOD_N);
    str_cpy(b, "MOOD ", sizeof b);
    str_cpy(b + str_len(b), MOOD_NAME[dice_mood], sizeof b - str_len(b));
    return b;
}

/* ---------------------------------------------------------------------------------- HAPPY ACCIDENT (OOPS) --- *
 * EDIT held + G5: 2 or 3 of the sound's 8 engine values (not its choices: WAVE, KIT, MODE, SLOT ..) moved 15 .. 45 %
 * of their range up or down (back from an end), half the time one FX send up too. The level, the steps, the engine
 * and the kit stay. A sound load for the undo: SAVE held brings back the sound from before the first OOPS (a run of
 * them is one undo, as browsing PRESETS is). Returns what moved, "OOPS: CUT RES DLY" */
static const char *accident_of(track_t *t)
{
    static char b[28];
    static const char *const SEND[4] = {"DIST", "CHO", "DLY", "REV"};
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    uint32_t want = 2u + tl_rnd(2), done = 0, used = 0, k;
    str_cpy(b, "OOPS:", sizeof b);
    load_begin(t, UNDO_SOUND);
    fm1_irq_off();
    for (k = 0; done < want && k < 32u; k++) {
        uint32_t i = tl_rnd(8);
        const param_desc_t *d = &e->edit[i];
        int32_t span = d->max - d->min, v0 = t->p[P_E0 + i], dv, v;
        if ((used >> i & 1u) || !d->label || d->fmt == F_ENUM || d->names || span < 8)
            continue;
        dv = span * (15 + (int32_t)tl_rnd(31)) / 100;
        if (dv < 1)
            dv = 1;
        if (tl_rnd(2))
            dv = -dv;
        v = v0 + dv;
        if (v > d->max || v < d->min)
            v = v0 - dv;
        v = clamp(v, d->min, d->max);
        if (v == v0)
            continue;
        t->p[P_E0 + i] = (int16_t)v;
        used |= 1u << i;
        done++;
        if (str_len(b) + 1u + str_len(d->label) < sizeof b) {
            str_cpy(b + str_len(b), " ", 2);
            str_cpy(b + str_len(b), d->label, sizeof b - str_len(b));
        }
    }
    if (tl_rnd(2)) {
        uint32_t s = tl_rnd(4), pi = P_DIST + s;
        int32_t v = t->p[pi] + 15 + (int32_t)tl_rnd(30);
        t->p[pi] = (int16_t)clamp(v, TP[pi].min, TP[pi].max);
        if (str_len(b) + 1u + str_len(SEND[s]) < sizeof b) {
            str_cpy(b + str_len(b), " ", 2);
            str_cpy(b + str_len(b), SEND[s], sizeof b - str_len(b));
        }
    }
    fm1_irq_on();
    load_end(t);
    return str_len(b) > 5u ? b : "OOPS: NOTHING TO MOVE";
}
