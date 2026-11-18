/* SPDX-License-Identifier: GPL-3.0-only */
/* KRIECHSTROM: what the screen says, for a screen reader (fm1-reader: https://charlesvestal.github.io/fm1-reader/).
 * announce.c (fm1-base's, MPL-2.0) keeps the protocol; this says what changed on the screen, once a frame, from the
 * same state the screen is drawn from: the four cards (ui_draw.c draw_column hands them over as they are drawn), the
 * footer's engine, track, sound and page, the header's transport, tempo and messages, the menu's row, a dialog, a
 * name being typed, a layer. The words file (firmware/words.tsv, published with the site) says the screen's short
 * labels in full ("ATK" attack).
 *   everything (a WHERE, a reader that starts listening): where we are, then the four cards
 *   the context changed (page, track, engine, sound, layer, menu tab, a dialog): the same, once it settles (200 ms)
 *   a card's value changed: its label and value (120 ms: the knob settled)
 *   a message in the header: at once; PLAY and STOP; the tempo; the step under the cursor in SEQ
 * Text is built in the pool (sp_buf), never on the system stack, and only while a reader listens. */

/* the screen's own text, a card at a time, as ui_draw.c draws it (the frame it was drawn in) */
static void spk_card(uint32_t c, const char *label, const char *val, const char *unit);

#ifndef KRST_SPEAK_HOST
/* the send: never waits, nothing while the queue cannot take a whole SAY (announce.c drops it) */
static int ann_wire_send(const uint8_t *p, uint32_t n)
{
    if (!usb.config || usb.suspended || SXQ - (so_w - so_r) < (n + 2u) / 3u)
        return -1;
    return ota_wire_send(p, n);
}
#endif

static const char *const SP_LAYER[LAYER_N] = {"", "FX LAYER", "GLOBAL LAYER", "SCALE LAYER", "EDIT LAYER", "SEQ LAYER",
                                              "REC LAYER"};
static struct {
    char l[4][14], v[4][16], u[4][8];
    uint32_t frame[4];
} spc __attribute__((section(".pool")));
static struct {
    uint32_t key;                                       /* the context's hash */
    char val[4][32];                                    /* each card's "label value unit", as last said */
    char msg[24];
    char nm[NM_LEN + 1u];
    uint8_t started, playing, menu_sel, cursor, step_on;
    int16_t bpm;
} spk __attribute__((section(".pool")));
static char sp_buf[ANN_MAX + 1u] __attribute__((section(".pool")));

static void spk_card(uint32_t c, const char *label, const char *val, const char *unit)
{
    if (c >= 4u)
        return;
    if (val && str_eq(val, "--"))                       /* an action card: not picked, picked */
        val = "";
    else if (val && str_eq(val, "[OCT+]"))
        val = "PICKED";
    str_cpy(spc.l[c], label ? label : "", sizeof spc.l[c]);
    str_cpy(spc.v[c], val ? val : "", sizeof spc.v[c]);
    str_cpy(spc.u[c], unit ? unit : "", sizeof spc.u[c]);
    spc.frame[c] = ui.frame;
}

/* append s (printable ASCII only; a leading icon byte and the like dropped) after a space, within ANN_MAX */
static void sp_add(char *b, const char *s)
{
    uint32_t n = str_len(b);
    if (!s || !*s)
        return;
    if (n && b[n - 1u] != ' ' && n < ANN_MAX)
        b[n++] = ' ';
    for (; *s && n < ANN_MAX; s++) {
        if (*s == '[' || *s == ']')                     /* a keycap ("[EDIT]"): the button's name */
            continue;
        if (*s == '>' || (*s == '-' && s[1] == '>')) {  /* "SEQ>AUTO", "MIDI IN ->": to */
            if (*s == '-')
                s++;
            if (n + 4u < ANN_MAX) {
                if (n && b[n - 1u] != ' ')
                    b[n++] = ' ';
                b[n++] = 'T', b[n++] = 'O', b[n++] = ' ';
            }
            continue;
        }
        if ((uint8_t)*s >= 32u && (uint8_t)*s < 127u)
            b[n++] = *s;
    }
    while (n && b[n - 1u] == ' ')
        n--;
    b[n] = 0;
}
static void sp_end(char *b, const char *mark)           /* a full stop or a comma after what is there */
{
    uint32_t n = str_len(b);
    if (n && n + 1u < ANN_MAX && b[n - 1u] != '.' && b[n - 1u] != ',' && b[n - 1u] != '?' && b[n - 1u] != ':')
        str_cpy(b + n, mark, 2);
}
/* a card as said: "label value unit" (an empty value: the label alone, an action) */
static void sp_card_text(uint32_t c, char *b, uint32_t max)
{
    b[0] = 0;
    if (spc.frame[c] != ui.frame || !spc.l[c][0])
        return;
    str_cpy(b, spc.l[c], max);
    if (spc.v[c][0]) {
        str_cpy(b + str_len(b), spc.v[c][0] == '.' ? " 0" : " ", max - str_len(b));   /* (".41": 0.41) */
        str_cpy(b + str_len(b), spc.v[c], max - str_len(b));
    }
    if (spc.u[c][0] && !str_eq(spc.u[c], spc.l[c])) {
        str_cpy(b + str_len(b), " ", max - str_len(b));
        str_cpy(b + str_len(b), spc.u[c], max - str_len(b));
    }
}
static void sp_cards(char *b)
{
    uint32_t c;
    char t[32];
    for (c = 0; c < 4u; c++) {
        sp_card_text(c, t, sizeof t);
        if (t[0]) {
            sp_add(b, t);
            sp_end(b, ",");
        }
    }
    {
        uint32_t n = str_len(b);
        if (n && b[n - 1u] == ',')
            b[n - 1u] = '.';
    }
}
static uint32_t sp_hash(uint32_t h, const char *s) { return str_hash(h, s); }

/* where we are (the context) into b, its key: the mode, the page, the track, the sound, the cards' labels */
static uint32_t sp_where(char *b)
{
    uint32_t h = 0x811C9DC5u, c;
    char t[24];
    b[0] = 0;
    if (ui.uboot) {
        str_cpy(t, "UPDATE MODE IN 5", sizeof t);
        t[15] = (char)('0' + ui.uboot % 10u);
        sp_add(b, t);
        return sp_hash(1u, t);
    }
    if (ui.menu) {
        uint32_t row = ui.menu_sel % MI_COUNT, tab = MI_TAB[row], first = mtab_first(tab), n = mtab_rows(tab);
        if (ui.menu == 2u) {
            sp_add(b, "ABOUT. KRIECHSTROM " FELUCCA_VERSION ", BUILT ON FELUCCA.");
            return 2u;
        }
        if (ui.menu == 3u) {
            sp_add(b, "INFO. KRIECHSTROM " FELUCCA_VERSION ".");
            return 3u;
        }
        sp_add(b, "MENU,");
        sp_add(b, MTAB_NAME[tab % MTAB_COUNT]);
        sp_end(b, ".");
        (void)first;
        (void)n;
        return sp_hash(4u, MTAB_NAME[tab % MTAB_COUNT]);
    }
    if (ui.confirm) {
        char a[24], d[24];
        confirm_text(a, d);
        sp_add(b, a);
        if (d[0]) {
            sp_add(b, d);
            sp_end(b, ".");
        }
        sp_add(b, "OCT+: YES. OCT-: NO.");
        return sp_hash(5u + ui.confirm, a);
    }
    if (name_on()) {
        sp_add(b, nm.kind == NK_PROJ_SAVE || nm.kind == NK_PROJ_RENAME ? "PROJECT NAME:" : "SOUND NAME:");
        sp_add(b, nm.s[0] ? nm.s : "EMPTY");
        sp_end(b, ".");
        sp_add(b, "OCT+: SAVE. OCT-: CANCEL.");
        return 6u + nm.kind;
    }
    if (ui.layer) {
        sp_add(b, SP_LAYER[ui.layer % LAYER_N]);
        sp_end(b, ".");
        h = sp_hash(7u + ui.layer, "L");
    } else {
        const engine_t *e = ENGINES[TSEL->eng_req % NENGINES];
        char pn[16];
        if (ui.home)
            str_cpy(t, "HOME", sizeof t);
        else
            page_title(t);
        for (c = 0; t[c]; c++)                           /* "ENV 1/2": page 1 of 2 (not a half) */
            if (t[c] == '/') {
                char r[24];
                str_cpy(r, t + c + 1u, sizeof r);
                str_cpy(t + c, " OF ", sizeof t - c);
                str_cpy(t + str_len(t), r, sizeof t - str_len(t));
                break;
            }
        sound_name(TSEL, pn);
        sp_add(b, t);
        sp_end(b, ".");
        str_cpy(t, "TRACK 1,", sizeof t);
        t[6] = (char)('1' + song.sel % NTRK);
        sp_add(b, t);
        sp_add(b, e->name);
        sp_add(b, pn);
        sp_end(b, ".");
        h = sp_hash(sp_hash(sp_hash(h, b), "|"), pn);
    }
    for (c = 0; c < 4u; c++)
        h = sp_hash(h + c, spc.frame[c] == ui.frame ? spc.l[c] : "");
    return h;
}

static void speak(uint32_t now)
{
    uint32_t key, c, all;
    char t[40];
    if (!ann_on()) {
        spk.started = 0;
        return;
    }
    all = ann_where() || !spk.started;
    key = sp_where(sp_buf);
    if (!ui.menu && !ui.confirm && !name_on() && !ui.uboot) {
        if (all || key != spk.key) {
            sp_cards(sp_buf);
            if (act_cols()) {                            /* an action page: what OCT+ and OCT- do */
                char ha[16], hb[16];
                foot_hint(ha, hb);
                sp_add(sp_buf, ha);
                sp_end(sp_buf, ".");
                sp_add(sp_buf, hb);
                sp_end(sp_buf, ".");
            }
        }
    } else if (ui.menu == 1u) {                          /* the menu: the row, its value, its place */
        uint32_t row = ui.menu_sel % MI_COUNT, tab = MI_TAB[row], first = mtab_first(tab), n = mtab_rows(tab);
        char r[48];
        r[0] = 0;
        sp_add(r, MI_NAME[row]);
        if (menu_valued(row))
            sp_add(r, menu_vname(row, menu_get(row)));
        sp_end(r, ",");
        fmt_int(t, (int32_t)(row - first + 1u));
        str_cpy(t + str_len(t), " OF ", sizeof t - str_len(t));
        fmt_int(t + str_len(t), (int32_t)n);
        sp_add(r, t);
        if (all || key != spk.key) {
            sp_add(sp_buf, r);
            spk.menu_sel = (uint8_t)row;
            str_cpy(spk.val[0], r, sizeof spk.val[0]);
        } else if (!str_eq(r, spk.val[0])) {
            str_cpy(spk.val[0], r, sizeof spk.val[0]);
            ann_later(r, ANN_NOW, 120, now);
        }
    } else if (name_on() && !all && key == spk.key && !str_eq(nm.s, spk.nm)) {   /* a letter typed or taken away */
        str_cpy(spk.nm, nm.s, sizeof spk.nm);
        str_cpy(t, "NAME: ", sizeof t);
        str_cpy(t + 6, nm.s[0] ? nm.s : "EMPTY", sizeof t - 6);
        ann_later(t, ANN_NOW, 200, now);
    }
    if (all || key != spk.key) {                          /* where we are, once it settles */
        if (sp_buf[0])
            ann_later(sp_buf, ANN_NOW, all ? 0u : 200u, now);
        spk.key = key;
        if (ui.menu != 1u)                               /* (the menu keeps its row in val[0]) */
            for (c = 0; c < 4u; c++)
                sp_card_text(c, spk.val[c], sizeof spk.val[c]);
        str_cpy(spk.nm, nm.s, sizeof spk.nm);
    } else if (!ui.menu && !ui.confirm && !name_on()) {  /* a card's value: its label and value, when it settles */
        for (c = 0; c < 4u; c++) {
            sp_card_text(c, t, sizeof t);
            if (t[0] && !str_eq(t, spk.val[c])) {
                str_cpy(spk.val[c], t, sizeof spk.val[c]);
                ann_later(t, ANN_NOW, 120, now);
            }
        }
    }
    if (ui.msg_t && ui.msg[0] && !str_eq(ui.msg, spk.msg)) {   /* a message in the header: at once */
        str_cpy(spk.msg, ui.msg, sizeof spk.msg);
        sp_buf[0] = 0;
        sp_add(sp_buf, ui.msg);
        ann_say(sp_buf, 0, ANN_NOW);
    } else if (!ui.msg_t) {
        spk.msg[0] = 0;
    }
    if (spk.started && (uint8_t)song.playing != spk.playing)
        ann_say(song.playing ? "PLAY" : "STOP", 0, ANN_NOW);
    spk.playing = (uint8_t)song.playing;
    if (spk.started && song.g[G_BPM] != spk.bpm) {         /* the tempo (the header's) */
        str_cpy(t, "TEMPO ", sizeof t);
        fmt_int(t + 6, song.g[G_BPM]);
        ann_later(t, ANN_NOW, 120, now);
    }
    spk.bpm = song.g[G_BPM];
    if (song.seq_mode && !ui.menu && !ui.confirm) {      /* SEQ: the step under the cursor */
        const step_t *st = &seq_steps(TSEL)[ui.cursor % 64u];
        uint8_t on = (uint8_t)step_on(st);
        if (spk.started && (ui.cursor != spk.cursor || on != spk.step_on)) {
            str_cpy(t, "STEP ", sizeof t);
            fmt_int(t + 5, ui.cursor + 1);
            str_cpy(t + str_len(t), on ? " ON" : " OFF", sizeof t - str_len(t));
            ann_later(t, ANN_NOW, 120, now);
        }
        spk.cursor = ui.cursor;
        spk.step_on = on;
    }
    spk.started = 1;
}
