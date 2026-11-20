/* SPDX-License-Identifier: GPL-3.0-only */
/* KRIECHSTROM: the screen reader (src/krst_speak.c with announce.c), on the real UI:
 *   build/host/krst_speak_test            the paths a person takes, each checked by what is said
 *   build/host/krst_speak_test --dump     every screen, card value, list row, dialog and layer as the device would say
 *                                         it (KIND <TAB> text), for fm1-reader's audit (tools/krst_speak_audit.sh)
 * The checks: silent until a reader says HELLO; HELLO says where we are (HOME, the track, its sound, the cards); a
 * knob says its card; a page says its title; the menu its tab and row with its place; PLAY and STOP; a message at once;
 * WHERE says it all again; BYE and nothing more. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int bad;
static void ck(const char *what, int ok)
{
    printf("speak: %-86s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static void sr_send(uint32_t cmd)                       /* a reader's frame: F0 7D 53 52 cmd [1] F7 (the bytes between) */
{
    uint8_t f[5] = {ANN_H0, ANN_H1, ANN_H2, (uint8_t)cmd, ANN_VER};
    ann_frame(f, cmd == ANN_HELLO ? 5u : 4u, fm1_ms);
}
static uint32_t said_from;
static void said_mark(void) { said_from = host_said_n; }
/* something said since the mark contains s (ignoring case) */
static int said(const char *s)
{
    uint32_t k;
    for (k = said_from; k < host_said_n; k++) {
        const char *h = host_said[k % 64u];
        uint32_t i, j, n = str_len(s);
        for (i = 0; h[i]; i++) {
            for (j = 0; j < n && h[i + j]; j++) {
                char a = h[i + j], b = s[j];
                if (a >= 'a' && a <= 'z') a = (char)(a - 32);
                if (b >= 'a' && b <= 'z') b = (char)(b - 32);
                if (a != b)
                    break;
            }
            if (j == n)
                return 1;
        }
    }
    return 0;
}
static void show_said(void)
{
    uint32_t k;
    for (k = said_from; k < host_said_n; k++)
        printf("speak:   said: %s\n", host_said[k % 64u]);
}

/* ------------------------------------------------------------------------------- the dump --- */
static void dump_last(const char *kind)
{
    if (host_said_n)
        printf("%s\t%s\n", kind, host_said[(host_said_n - 1u) % 64u]);
}
static void dump_where(const char *kind)
{
    uint32_t n = host_said_n;
    sr_send(ANN_HELLO);                                  /* (a reader still there: it says HELLO every 2 s) */
    ui.force = 1;
    frame();
    sr_send(ANN_WHERE);
    frame();
    frames(64);
    if (host_said_n != n)
        dump_last(kind);
}
static void dump(void)
{
    uint32_t e, i, c, v, row;
    ui_power_on();
    sr_send(ANN_HELLO);
    frames(400);
    for (e = 0; e < NENG_SHOWN; e++) {                   /* every engine's pages, their cards and values */
        track_t *t = &trk[0];
        set_engine_of(t, eng_vis(e));
        apply_preset_to(t, 0);
        t->engine = t->eng_req;
        song.sel = 0;
        ui.home = 1;
        dump_where("SCREEN");
        for (i = 0; i < NPAGES; i++) {
            if (!page_visible(i))
                continue;
            ui.home = 0;
            ui.page = (uint8_t)i;
            dump_where("SCREEN");
            for (c = 0; c < 4u; c++) {
                uint32_t id = PAGES[i].id[c];
                const param_desc_t *d;
                char t2[40];
                if (id == 0xFFu || id >= P_COUNT)
                    continue;
                d = PAGES[i].scope == SC_GLOBAL ? &GP[id % G_COUNT] : param_desc_of(t->eng_req, id);
                if (!d)
                    continue;
                for (v = 0; v < 3u || (d->names && v <= (uint32_t)(d->max - d->min)); v++) {
                    int32_t val = d->names ? d->min + (int32_t)v : v == 0 ? d->min : v == 1 ? (d->min + d->max) / 2 : d->max;
                    if (d->names && val > d->max)
                        break;
                    if (PAGES[i].scope == SC_GLOBAL)
                        song.g[id % G_COUNT] = (int16_t)val;
                    else
                        t->p[id] = (int16_t)val;
                    frame();
                    sp_card_text(c, t2, sizeof t2);
                    if (t2[0])
                        printf("READOUT\t%s\n", t2);
                    if (!d->names && v >= 2u)
                        break;
                }
                if (PAGES[i].scope == SC_GLOBAL)
                    song.g[id % G_COUNT] = d->def;
                else
                    t->p[id] = d->def;
            }
        }
        for (i = 0; i < ENGINES[t->eng_req]->npresets; i++) {   /* its sounds, as HOME names them */
            apply_preset_to(t, i);
            ui.home = 1;
            frame();
            {
                char pn[16];
                sound_name(t, pn);
                printf("READOUT\t%s\n", pn);
            }
        }
    }
    ui.home = 1;
    for (i = 1; i < LAYER_N; i++) {                      /* the layers */
        ui.layer = (uint8_t)i;
        dump_where("SCREEN");
    }
    ui.layer = 0;
    for (row = 0; row < MI_COUNT; row++) {               /* the menu: every row and value */
        uint32_t tab = MI_TAB[row], first = mtab_first(tab), n = mtab_rows(tab);
        char b[64], k[16];
        if (MI_TAB[row] >= MTAB_COUNT)
            continue;
        ui.menu = 1;
        ui.menu_sel = (uint8_t)row;
        dump_where("SCREEN");
        for (v = 0; v < (menu_valued(row) ? menu_n(row) : 1u); v++) {
            b[0] = 0;
            sp_add(b, MI_NAME[row]);
            if (menu_valued(row))
                sp_add(b, menu_vname(row, v));
            sp_end(b, ",");
            fmt_int(k, (int32_t)(row - first + 1u));
            str_cpy(k + str_len(k), " OF ", sizeof k - str_len(k));
            fmt_int(k + str_len(k), (int32_t)n);
            sp_add(b, k);
            printf("LIST\t%s\n", b);
        }
    }
    ui.menu = 0;
    for (i = CF_CLEAR_SEQ; i <= CF_LEARN_CLEAR; i++) {  /* the dialogs */
        ui.confirm = (uint8_t)i;
        dump_where("DIALOG");
    }
    ui.confirm = 0;
    nm.kind = NK_USER_SAVE;                              /* NAME */
    str_cpy(nm.s, "BASS", sizeof nm.s);
    dump_where("SCREEN");
    nm.kind = NK_PROJ_SAVE;
    dump_where("SCREEN");
    nm.kind = NK_NONE;
}

/* ---------------------------------------------------------------------------- the paths --- */
static void paths(void)
{
    uint32_t n;
    ui_power_on();
    host_said_n = 0;
    frames(500);
    ck("silent until a reader says HELLO (nothing sent)", host_said_n == 0u);

    said_mark();
    sr_send(ANN_HELLO);
    frames(300);
    ck("HELLO: where we are (HOME, track 1, its engine and sound, the cards)", said("HOME") && said("TRACK 1") &&
       said(ENGINES[trk[0].eng_req]->name));
    if (!said("HOME"))
        show_said();

    said_mark();
    turn(EN_K1, 3);
    frames(300);
    ck("a knob turned: its card, label and value, once it settles", host_said_n > said_from && said(spc.l[0]));
    if (!(host_said_n > said_from))
        show_said();

    said_mark();
    press(B_EDIT);
    frames(400);
    ck("a page: its title and its cards", host_said_n > said_from);
    show_said();

    said_mark();
    transport_req = 1;
    song.playing = 1;
    frames(100);
    ck("PLAY said", said("PLAY"));
    said_mark();
    song.playing = 0;
    frames(100);
    ck("STOP said", said("STOP"));

    said_mark();
    press(B_OCTUP);
    frames(250);
    ck("OCT+: the octave (OCTAVE PLUS 1)", said("OCTAVE PLUS 1"));
    said_mark();
    press(B_OCTDN);
    frames(250);
    press(B_OCTDN);
    frames(250);
    ck("OCT- twice: OCTAVE MINUS 1", said("OCTAVE MINUS 1"));
    said_mark();
    press(B_OCTUP);
    frames(250);
    ck("back to the middle: OCTAVE 0", said("OCTAVE 0"));
    if (song.octave)
        show_said();

    said_mark();
    ui_say("SAVED ", "A");
    frames(50);
    ck("a message in the header: at once", said("SAVED A"));

    said_mark();
    ui.menu = 1;
    ui.menu_sel = 0;
    ui.force = 1;
    frames(400);
    ck("the menu: its tab, the row with its value and its place (1 of n)", said("MENU") && said("1 OF"));
    show_said();
    ui.menu = 0;
    ui.force = 1;
    frames(400);

    said_mark();
    sr_send(ANN_WHERE);
    frames(100);
    ck("WHERE: it all again", host_said_n > said_from);

    {   /* the KIT of a drum track */
        track_t *t = &trk[3];
        said_mark();
        song.sel = 3;
        ui.home = 0;
        ui.page = (uint8_t)page_first(FAM_EDIT);
        t->p[P_E0] = 13;
        ui.force = 1;
        frames(400);
        ck("a drum track's EDIT page: KIT ROOM", said("ROOM"));
        if (!said("ROOM"))
            show_said();
    }

    sr_send(ANN_BYE);
    frames(50);
    n = host_said_n;
    turn(EN_K2, 4);
    frames(400);
    ck("BYE: nothing more", host_said_n == n);

    {   /* a reader that stops saying HELLO: gone after 5 s */
        sr_send(ANN_HELLO);
        frames(300);
        frames(6000);
        n = host_said_n;
        turn(EN_K2, -4);
        frames(400);
        ck("a reader not heard for 5 s: nothing more", host_said_n == n);
    }
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--dump")) {
        dump();
        return 0;
    }
    paths();
    printf("speak: %s\n", bad ? "FAIL" : "all ok");
    return bad != 0;
}
