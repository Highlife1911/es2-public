/* screen_test. First the harness's own checks (layout table reader,
 * click by control name, text, frames.json reader and locator, difference
 * classes, the reference comparison with and without a dumped frame), then
 * every registered screen suite (shell/tests/screen_<name>.c, one
 * ES2_SCREEN_SUITE line each). With ES2_DATA_ROOT and a capture-tour
 * directory the real references are compared; without one that part prints
 * SKIP. Arguments: <layout json> [--suite NAME] [--case PREFIX]. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L   /* setenv, unsetenv and mkdir under -std=c17 */
#endif
#include "screen_harness.h"

#include "es2/shell/gui/headless.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define make_dir(p) mkdir(p, 0777)
#endif

void es2_screen_fixture_set_layout(const es2_screen_layout *layout);
extern const es2_screen_case es2_screen_fixture_cases[];

static unsigned checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; fprintf(stderr, "line %d: %s\n", __LINE__, #x); } } while (0)

static int set_env(const char *name, const char *value) {
#ifdef _WIN32
    char buffer[1200];
    snprintf(buffer, sizeof buffer, "%s=%s", name, value ? value : "");
    return _putenv(buffer);
#else
    return value ? setenv(name, value, 1) : unsetenv(name);
#endif
}

static int write_file(const char *path, const void *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t n = fwrite(data, 1, size, f);
    return fclose(f) || n != size ? -1 : 0;
}

/* ---- layout -------------------------------------------------------------- */

static void test_layout_parser(void) {
    static const char good[] =
        "{\"schema\":1,\"screens\":[{\"name\":\"demo\",\"controls\":["
        "{\"id\":\"demo.00\",\"class\":\"ESWindow\",\"screen_rect\":null,\"args\":{},\"handler\":0},"
        "{\"id\":\"demo.01\",\"class\":\"ESButtonFont\",\"screen_rect\":[10,20,31,41],"
        "\"args\":{\"font\":{\"global\":\"0x46ddfc\"},\"label\":\"\\u00e9\\n\"},\"handler\":\"0x431935\"},"
        "{\"id\":\"demo.02\",\"class\":\"ESMessage\",\"owner\":\"demo.01\",\"screen_rect\":[-3,-3,0,0],"
        "\"args\":{\"font\":{\"global\":\"0x46ddfc\"}},\"handler\":0}]}]}";
    es2_screen_layout l;
    char err[200] = "";
    CHECK(es2_screen_layout_parse(good, sizeof good - 1, &l, err, sizeof err) == 0);
    CHECK(l.count == 3);
    const es2_screen_control *c = es2_screen_layout_find(&l, "demo.01");
    int32_t x = 0, y = 0;
    CHECK(c && c->has_rect && c->clickable && c->text && !c->owner[0]);
    CHECK(es2_screen_control_centre(c, &x, &y) && x == 20 && y == 30); /* floor(41/2), floor(61/2) */
    c = es2_screen_layout_find(&l, "demo.02");
    CHECK(c && !c->clickable && c->text && !strcmp(c->owner, "demo.01"));
    CHECK(es2_screen_control_centre(c, &x, &y) && x == -2 && y == -2); /* floor(-3/2) as PowerShell's Floor */
    c = es2_screen_layout_find(&l, "demo.00");
    CHECK(c && !c->has_rect && !es2_screen_control_centre(c, &x, &y) && !c->clickable);
    CHECK(!es2_screen_layout_find(&l, "demo.03"));
    es2_screen_layout_free(&l);

    static const char *const bad[] = {
        "{\"schema\":2,\"screens\":[]}",
        "{\"schema\":1}",
        "{\"schema\":1,\"screens\":[{\"name\":\"x\",\"controls\":[{\"id\":\"x.0\",\"class\":\"A\",\"screen_rect\":[1,2,3]}]}]}",
        "{\"schema\":1,\"screens\":[{\"name\":\"x\",\"controls\":[{\"id\":\"x.0\",\"class\":\"A\",\"screen_rect\":[1,2,3,4.5]}]}]}",
        "{\"schema\":1,\"screens\":[{\"name\":\"x\",\"controls\":[{\"class\":\"A\"}]}]}",
        "{\"schema\":1,\"screens\":[{\"controls\":[]}]}",
        "{\"schema\":1,\"screens\":[]",
        "{\"schema\":1,\"screens\":[]} x",
        "{\"schema\":1,\"s\\q\":[]}",
        "{\"schema\":1,\"s\":\"\\u12\"}",
        "{\"schema\":1,\"s\":\"\\u0000\"}",
        "{\"schema\":1,\"s\":[1,]}",
        "{\"schema\":01e}",
        "",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        err[0] = 0;
        int r = es2_screen_layout_parse(bad[i], strlen(bad[i]), &l, err, sizeof err);
        CHECK(r == -1 && err[0]);
        if (r == 0) es2_screen_layout_free(&l);
    }
    /* nesting deeper than the limit, and a truncated string */
    char deep[200];
    memset(deep, '[', 100);
    deep[100] = 0;
    CHECK(es2_screen_layout_parse(deep, 100, &l, err, sizeof err) == -1 && strstr(err, "deep"));
    CHECK(es2_screen_layout_parse("\"abc", 4, &l, err, sizeof err) == -1);
    CHECK(es2_screen_layout_load("no/such/layout.json", &l, err, sizeof err) == -1);
}

static void test_layout_table(const es2_screen_layout *l) {
    CHECK(l->count == 1051); /* the layout table: 22 screens, 1,051 controls */
    const es2_screen_control *c = es2_screen_layout_find(l, "main_menu.05");
    int32_t x, y;
    CHECK(c && !strcmp(c->cls, "ESButtonFont") && c->clickable && c->text);
    CHECK(c && c->rect.left == 155 && c->rect.top == 201 && c->rect.right == 308 && c->rect.bottom == 216);
    CHECK(es2_screen_control_centre(c, &x, &y) && x == 231 && y == 208);
    c = es2_screen_layout_find(l, "main_menu.06"); /* the button's label */
    CHECK(c && !c->clickable && c->text && !strcmp(c->owner, "main_menu.05"));
    c = es2_screen_layout_find(l, "main_menu.03"); /* ESTitle: a panel, not glyphs */
    CHECK(c && !c->text);
    c = es2_screen_layout_find(l, "main_menu.02");
    CHECK(c && !c->has_rect);
    c = es2_screen_layout_find(l, "quit_box.03");
    CHECK(c && c->clickable && es2_screen_control_centre(c, &x, &y) && x == 321 && y == 247);
}

/* ---- session: clicks, keys, text ----------------------------------------- */

static const es2_gui_event *nth(const es2_screen_session *s, uint32_t type, size_t n) {
    for (size_t i = 0; i < s->event_count; i++)
        if (s->events[i].type == type && n-- == 0) return &s->events[i];
    return NULL;
}

static size_t count_type(const es2_screen_session *s, uint32_t type) {
    size_t n = 0;
    for (size_t i = 0; i < s->event_count; i++) n += s->events[i].type == type;
    return n;
}

static void test_click(const es2_screen_layout *l) {
    es2_screen_session s;
    CHECK(es2_screen_session_open(&s, l, NULL, NULL) == 0);
    CHECK(es2_screen_run(&s, "click main_menu.05\n") == 0);
    CHECK(s.pass == 5);
    /* moves to (639,479), (0,0) and the centre: each posted at its pointer;
     * then the button records at the pointer of their poll */
    const es2_gui_event *e;
    CHECK((e = nth(&s, ES2_GUI_EV_MOUSE, 0)) && e->code == ES2_GUI_MOUSE_MOVE);
    CHECK(count_type(&s, ES2_GUI_EV_MOUSE) == 5);
    CHECK((e = nth(&s, ES2_GUI_EV_MOUSE, 2)) && e->code == ES2_GUI_MOUSE_MOVE && e->x == 231 && e->y == 208);
    CHECK((e = nth(&s, ES2_GUI_EV_MOUSE, 3)) && e->code == ES2_GUI_MOUSE_LEFT_DOWN && e->x == 231 && e->y == 208 &&
          !e->synthetic);
    CHECK((e = nth(&s, ES2_GUI_EV_MOUSE, 4)) && e->code == ES2_GUI_MOUSE_LEFT_UP && e->x == 231 && e->y == 208);
    CHECK(s.shell->input.buttons == 0);
    es2_screen_session_close(&s);

    CHECK(es2_screen_session_open(&s, l, NULL, NULL) == 0);
    CHECK(es2_screen_run(&s, "click quit_box.05 right\n") == 0);
    CHECK((e = nth(&s, ES2_GUI_EV_MOUSE, 3)) && e->code == ES2_GUI_MOUSE_RIGHT_DOWN && e->x == 321 && e->y == 269);
    CHECK((e = nth(&s, ES2_GUI_EV_MOUSE, 4)) && e->code == ES2_GUI_MOUSE_RIGHT_UP);
    es2_screen_session_close(&s);

    /* refusals: unknown control, control without screen_rect, bad side, no layout */
    CHECK(es2_screen_session_open(&s, l, NULL, NULL) == 0);
    CHECK(es2_screen_run(&s, "click main_menu.99\nclick main_menu.02\nclick main_menu.05 middle\nbogus 1\n") == 4);
    CHECK(s.event_count == 0 && strstr(s.error, "line 1") && strstr(s.error, "layout table"));
    es2_screen_session_close(&s);
    CHECK(es2_screen_session_open(&s, NULL, NULL, NULL) == 0);
    CHECK(es2_screen_run(&s, "click main_menu.05\n") == 1);
    es2_screen_session_close(&s);

    /* a click at the motion centre (320, 240): the move is dropped, the buttons are not */
    static const char centre_layout[] =
        "{\"schema\":1,\"screens\":[{\"name\":\"c\",\"controls\":[{\"id\":\"c.00\",\"class\":\"ESButtonFont\","
        "\"screen_rect\":[310,230,330,250],\"args\":{},\"handler\":\"0x1\"}]}]}";
    es2_screen_layout cl;
    char err[100];
    CHECK(es2_screen_layout_parse(centre_layout, sizeof centre_layout - 1, &cl, err, sizeof err) == 0);
    CHECK(es2_screen_session_open(&s, &cl, NULL, NULL) == 0);
    CHECK(es2_screen_run(&s, "click c.00\n") == 0);
    CHECK(count_type(&s, ES2_GUI_EV_MOUSE) == 4);
    CHECK((e = nth(&s, ES2_GUI_EV_MOUSE, 2)) && e->code == ES2_GUI_MOUSE_LEFT_DOWN && e->x == 320 && e->y == 240);
    es2_screen_session_close(&s);
    es2_screen_layout_free(&cl);
}

static void test_keys(void) {
    es2_screen_session s;
    CHECK(es2_screen_session_open(&s, NULL, NULL, NULL) == 0);
    CHECK(es2_screen_run(&s, "type Ab 1\n") == 0);
    CHECK(s.pass == 12); /* three passes per character */
    const es2_gui_event *e;
    CHECK(count_type(&s, ES2_GUI_EV_KEY) == 4);
    CHECK((e = nth(&s, ES2_GUI_EV_KEY, 0)) && e->code == 'A');
    CHECK((e = nth(&s, ES2_GUI_EV_KEY, 1)) && e->code == 'B');
    CHECK((e = nth(&s, ES2_GUI_EV_KEY, 2)) && e->code == ' ');
    CHECK((e = nth(&s, ES2_GUI_EV_KEY, 3)) && e->code == '1');
    /* punctuation has no key in VSHELL's key table: refused, nothing sent */
    unsigned before = s.pass;
    CHECK(es2_screen_run(&s, "type a;\n") == 1 && s.pass == before && strstr(s.error, "no key"));
    CHECK(es2_screen_run(&s, "type\n") == 1);
    /* press: hot keys become commands (Esc down 0x00, up 0x13) */
    CHECK(es2_screen_run(&s, "press esc\n") == 0);
    CHECK(count_type(&s, ES2_GUI_EV_COMMAND) == 2);
    CHECK((e = nth(&s, ES2_GUI_EV_COMMAND, 0)) && e->code == 0x00);
    CHECK((e = nth(&s, ES2_GUI_EV_COMMAND, 1)) && e->code == 0x13);
    CHECK(es2_screen_run(&s, "press nosuchkey\npress esc hyper\n") == 2);
    /* the loop ends: later lines are errors */
    CHECK(es2_screen_run(&s, "quit\nstep\nstep\n") == 1);
    es2_screen_session_close(&s);
}

static void test_snaps(void) {
    es2_screen_session s;
    CHECK(es2_screen_session_open(&s, NULL, NULL, NULL) == 0);
    CHECK(es2_screen_run(&s, "snap empty\n") == 0);
    es2_gui_test_pattern(s.shell);
    CHECK(es2_screen_run(&s, "step\nsnap pattern\n") == 0);
    const es2_screen_snap *a = es2_screen_find_snap(&s, "empty"), *b = es2_screen_find_snap(&s, "pattern");
    CHECK(a && b && strcmp(a->frame, b->frame) && strcmp(a->table, b->table));
    char hex[65];
    es2_gui_hash_frame(&s.shell->frame, hex);
    CHECK(b && !strcmp(b->frame, hex));
    CHECK(s.present_count == 1 && !strcmp(s.presents[0].frame, hex) && !strcmp(s.presents[0].table, b->table));
    /* the snap's table hash is the RGB form, the headless report's is BGR */
    es2_gui_hash_table(&s.shell->frame, hex);
    CHECK(b && strcmp(b->table, hex));
    CHECK(!es2_screen_find_snap(&s, "none"));
    es2_screen_session_close(&s);
}

/* ---- differences ---------------------------------------------------------- */

static void test_compare(const es2_screen_layout *l) {
    static uint8_t a[ES2_GUI_PIXELS], b[ES2_GUI_PIXELS];
    static uint8_t ta[256][3], tb[256][3];
    for (size_t i = 0; i < ES2_GUI_PIXELS; i++) a[i] = (uint8_t)((i * 31) % 251);
    memset(ta, 9, sizeof ta);
    memcpy(b, a, sizeof b);
    memcpy(tb, ta, sizeof tb);
    es2_screen_diff d;
    char line[256];
    es2_screen_compare(a, (const uint8_t(*)[3])ta, b, (const uint8_t(*)[3])tb, l, "main_menu", &d);
    CHECK(d.cls == ES2_SCREEN_SAME && d.pixels == 0);

    tb[17][1] = 1;
    tb[40][2] = 1;
    es2_screen_compare(a, (const uint8_t(*)[3])ta, b, (const uint8_t(*)[3])tb, l, "main_menu", &d);
    CHECK(d.cls == ES2_SCREEN_PALETTE && d.first_entry == 17 && d.last_entry == 40);
    es2_screen_diff_format(&d, line, sizeof line);
    CHECK(!strcmp(line, "class=palette entries=17..40"));
    memcpy(tb, ta, sizeof tb);

    /* state: a block outside every control of the screen */
    for (int y = 10; y <= 14; y++)
        for (int x = 20; x <= 29; x++) b[y * ES2_GUI_WIDTH + x] ^= 0x55;
    es2_screen_compare(a, (const uint8_t(*)[3])ta, b, (const uint8_t(*)[3])tb, l, "main_menu", &d);
    CHECK(d.cls == ES2_SCREEN_STATE && d.rect.left == 20 && d.rect.top == 10 && d.rect.right == 29 &&
          d.rect.bottom == 14 && d.pixels == 50 && !d.control[0] && !d.table_differs);
    es2_screen_diff_format(&d, line, sizeof line);
    CHECK(!strcmp(line, "class=state rect=(20,10,29,14) control=- pixels=50"));
    /* two regions: the first in row order is reported */
    b[400 * ES2_GUI_WIDTH + 600] ^= 1;
    es2_screen_compare(a, (const uint8_t(*)[3])ta, b, (const uint8_t(*)[3])tb, l, "main_menu", &d);
    CHECK(d.rect.left == 20 && d.rect.bottom == 14 && d.pixels == 51);
    memcpy(b, a, sizeof b);

    /* font: inside the label main_menu.06 (an ESMessage with a font) */
    b[205 * ES2_GUI_WIDTH + 200] ^= 0x0f;
    es2_screen_compare(a, (const uint8_t(*)[3])ta, b, (const uint8_t(*)[3])tb, l, "main_menu", &d);
    CHECK(d.cls == ES2_SCREEN_FONT && !strcmp(d.control, "main_menu.06"));
    /* the same pixel without the layout: state; with the table changed too: flagged */
    tb[3][0] = 77;
    es2_screen_compare(a, (const uint8_t(*)[3])ta, b, (const uint8_t(*)[3])tb, NULL, NULL, &d);
    CHECK(d.cls == ES2_SCREEN_STATE && d.table_differs);
    es2_screen_diff_format(&d, line, sizeof line);
    CHECK(strstr(line, "table=differs") != NULL);
    memcpy(tb, ta, sizeof tb);
    memcpy(b, a, sizeof b);

    /* geometry: a 30 x 12 block of a uniform background drawn 3 right, 2 down */
    memset(a, 5, sizeof a);
    for (int y = 100; y < 112; y++)
        for (int x = 400; x < 430; x++) a[y * ES2_GUI_WIDTH + x] = (uint8_t)(x * 3 + y);
    memset(b, 5, sizeof b);
    for (int y = 100; y < 112; y++)
        for (int x = 400; x < 430; x++) b[(y + 2) * ES2_GUI_WIDTH + x + 3] = (uint8_t)(x * 3 + y);
    es2_screen_compare(a, (const uint8_t(*)[3])ta, b, (const uint8_t(*)[3])tb, l, "main_menu", &d);
    CHECK(d.cls == ES2_SCREEN_GEOMETRY);
    es2_screen_diff_format(&d, line, sizeof line);
    CHECK(!strncmp(line, "class=geometry rect=(", 21));
    CHECK(!strcmp(es2_screen_class_name(ES2_SCREEN_UNKNOWN), "unknown"));
}

/* ---- references ------------------------------------------------------------ */

static const char fixture_frames[] =
    "{\"schema\":1,\"kind\":\"es2-vshell-screens\",\"session\":\"fixture\",\"runs\":[],\"screens\":["
    "{\"run\":\"game_r\",\"screen\":\"main_menu\",\"state\":\"start\",\"step\":2,"
    "\"index_sha256\":\"%s\",\"palette_sha256\":\"%s\"},"
    "{\"run\":\"game_r\",\"screen\":\"main_menu\",\"state\":\"start\",\"step\":9,"
    "\"index_sha256\":\"%s\",\"palette_sha256\":null},"
    "{\"run\":\"game_r\",\"screen\":\"preferences\",\"state\":\"from_main_menu\",\"step\":6,"
    "\"index_sha256\":\"%s\",\"palette_sha256\":null}]}";

static void test_references_parser(void) {
    static const char z[] = "0000000000000000000000000000000000000000000000000000000000000000";
    static const char f[] = "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF";
    char text[2048], err[200];
    snprintf(text, sizeof text, fixture_frames, f, z, z, z);
    es2_screen_references r;
    CHECK(es2_screen_references_parse(text, strlen(text), &r, err, sizeof err) == 0);
    CHECK(r.count == 3);
    const es2_screen_reference *x = es2_screen_references_find(&r, "game_r:main_menu:start");
    CHECK(x && x->step == 2 && x->index_sha256[0] == 'f' && !strcmp(x->palette_sha256, z)); /* lower case; first kept */
    x = es2_screen_references_find(&r, "game_r:preferences:from_main_menu");
    CHECK(x && !x->palette_sha256[0] && !strcmp(x->run, "game_r"));
    CHECK(!es2_screen_references_find(&r, "game_t:main_menu:start"));
    es2_screen_references_free(&r);

    /* A frames.json built from a version 2 capture-tour log (the run's capture_start has schema 2 and
     * qpc_frequency, every frame record a qpc value) reads like a version 1 one; the screens are the same. */
    static const char frames_v2[] =
        "{\"schema\":1,\"kind\":\"es2-vshell-screens\",\"session\":\"fixture\",\"runs\":[{\"name\":\"game_r\","
        "\"capture_start\":{\"kind\":\"start\",\"seq\":0,\"qpc\":5000000000,\"schema\":2,\"pid\":7,\"qpc_frequency\":10000000},"
        "\"frames\":[{\"seq\":1,\"kind\":\"present\",\"step\":2,\"qpc\":5012345678901,"
        "\"index_sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\",\"palette_sha256\":null}]}],"
        "\"screens\":[{\"run\":\"game_r\",\"screen\":\"main_menu\",\"state\":\"start\",\"step\":2,\"qpc\":5012345678901,"
        "\"index_sha256\":\"FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF\",\"palette_sha256\":null}]}";
    CHECK(es2_screen_references_parse(frames_v2, strlen(frames_v2), &r, err, sizeof err) == 0);
    CHECK(r.count == 1);
    x = es2_screen_references_find(&r, "game_r:main_menu:start");
    CHECK(x && x->step == 2 && x->index_sha256[0] == 'f');
    es2_screen_references_free(&r);

    snprintf(text, sizeof text, fixture_frames, "abc", z, z, z); /* not a hash */
    CHECK(es2_screen_references_parse(text, strlen(text), &r, err, sizeof err) == -1);
    static const char *const bad[] = {
        "{\"schema\":1,\"kind\":\"other\",\"screens\":[]}",
        "{\"schema\":2,\"kind\":\"es2-vshell-screens\",\"screens\":[]}",
        "{\"schema\":1,\"kind\":\"es2-vshell-screens\"}",
        ("{\"schema\":1,\"kind\":\"es2-vshell-screens\",\"screens\":[{\"run\":\"a\",\"screen\":\"b\",\"state\":\"c\","
        "\"index_sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\"}]}"),
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        CHECK(es2_screen_references_parse(bad[i], strlen(bad[i]), &r, err, sizeof err) == -1);
    CHECK(es2_screen_references_load("no/such/frames.json", &r, err, sizeof err) == -1);
}

static void test_locate(const char *tmp) {
    char saved_screens[1024] = "", saved_root[1024] = "", saved_repo[1024] = "";
    const char *v = getenv("ES2_VSHELL_SCREENS");
    int had_screens = v != NULL;
    if (v) snprintf(saved_screens, sizeof saved_screens, "%s", v);
    v = getenv("ES2_DATA_ROOT");
    int had_root = v != NULL;
    if (v) snprintf(saved_root, sizeof saved_root, "%s", v);
    v = getenv("ES2_DATA_REPO");
    int had_repo = v != NULL;
    if (v) snprintf(saved_repo, sizeof saved_repo, "%s", v);

    char path[1024], dir[1024], found[1024];
    static const char *const dirs[] = {"", "/data", "/data/extracted", "/data/extracted/cd", "/data/logs",
                                       "/data/logs/vshell-screens-20261001-0900", "/data/logs/vshell-screens-20261002-0900",
                                       "/data/logs/vshell-screens-20261003-0900"};
    for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
        snprintf(dir, sizeof dir, "%s/locate%s", tmp, dirs[i]);
        make_dir(dir);
    }
    /* 03 has no frames.json: the newest complete one is 02 */
    snprintf(path, sizeof path, "%s/locate/data/logs/vshell-screens-20261001-0900/frames.json", tmp);
    CHECK(write_file(path, "{}", 2) == 0);
    snprintf(path, sizeof path, "%s/locate/data/logs/vshell-screens-20261002-0900/frames.json", tmp);
    CHECK(write_file(path, "{}", 2) == 0);

    set_env("ES2_VSHELL_SCREENS", "");
    set_env("ES2_DATA_ROOT", "");
    set_env("ES2_DATA_REPO", "");
    CHECK(es2_screen_references_locate(found, sizeof found) == 0);
    snprintf(dir, sizeof dir, "%s/locate/data", tmp);
    set_env("ES2_DATA_REPO", dir); /* the es2-data checkout itself */
    CHECK(es2_screen_references_locate(found, sizeof found) == 1 && strstr(found, "vshell-screens-20261002-0900/frames.json"));
    set_env("ES2_DATA_REPO", "");
    snprintf(dir, sizeof dir, "%s/locate/data/extracted/cd", tmp);
    set_env("ES2_DATA_ROOT", dir);
    CHECK(es2_screen_references_locate(found, sizeof found) == 1 && strstr(found, "vshell-screens-20261002-0900/frames.json"));
    snprintf(dir, sizeof dir, "%s/locate/data/logs/vshell-screens-20261001-0900", tmp);
    set_env("ES2_VSHELL_SCREENS", dir);
    CHECK(es2_screen_references_locate(found, sizeof found) == 1 && strstr(found, "20261001-0900/frames.json"));
    set_env("ES2_VSHELL_SCREENS", path);
    CHECK(es2_screen_references_locate(found, sizeof found) == 1 && !strcmp(found, path));
    snprintf(dir, sizeof dir, "%s/locate/data/extracted", tmp); /* no logs two levels up */
    set_env("ES2_VSHELL_SCREENS", "");
    set_env("ES2_DATA_ROOT", dir);
    CHECK(es2_screen_references_locate(found, sizeof found) == 0);

    set_env("ES2_VSHELL_SCREENS", had_screens ? saved_screens : "");
    set_env("ES2_DATA_ROOT", had_root ? saved_root : "");
    set_env("ES2_DATA_REPO", had_repo ? saved_repo : "");
}

/* The comparison with the capture tour's references, on the fixture screen:
 * equal, palette only, index differing without and with a dumped reference
 * frame. */
static void test_reference_compare(const es2_screen_layout *l, const char *tmp) {
    es2_screen_case c = es2_screen_fixture_cases[0];
    es2_screen_expect e = {"start", NULL, NULL, "game_r:main_menu:start"};
    c.expect = &e;
    c.expect_count = 1;
    c.expected_picture = NULL;

    /* the actual hashes of the fixture's start snap */
    es2_screen_session s;
    void *state = calloc(1, c.state_size);
    CHECK(state && es2_screen_session_open(&s, l, NULL, NULL) == 0);
    CHECK(c.setup(&s, state, NULL) == 0 && es2_screen_run(&s, c.script) == 0);
    const es2_screen_snap *snap = es2_screen_find_snap(&s, "start");
    CHECK(snap != NULL);
    if (!snap) return;
    char frame[65], table[65];
    snprintf(frame, sizeof frame, "%s", snap->frame);
    snprintf(table, sizeof table, "%s", snap->table);
    /* a reference frame differing in one label (main_menu.08) and the frame of a geometry change */
    static uint8_t changed[ES2_GUI_PIXELS];
    memcpy(changed, snap->pixels, sizeof changed);
    for (int x = 160; x < 170; x++) changed[230 * ES2_GUI_WIDTH + x] ^= 0x33;
    char changed_hash[65] = "";
    {
        es2_gui_frame *f = malloc(sizeof *f);
        CHECK(f != NULL);
        if (f) {
            memcpy(f->pixels, changed, sizeof changed);
            es2_gui_hash_frame(f, changed_hash);
            free(f);
        }
    }
    es2_screen_session_close(&s);
    free(state);

    static const char z[] = "0000000000000000000000000000000000000000000000000000000000000000";
    char text[2048], err[200];
    es2_screen_references r;
    es2_screen_totals t = {0, 0, 0};

    snprintf(text, sizeof text, fixture_frames, frame, table, z, z);
    CHECK(es2_screen_references_parse(text, strlen(text), &r, err, sizeof err) == 0);
    es2_screen_run_case(&c, l, &r, NULL, &t);
    CHECK(t.passed == 1 && t.failed == 0);
    es2_screen_references_free(&r);

    printf("(the next five failing cases are expected: the harness's own comparison checks)\n");
    snprintf(text, sizeof text, fixture_frames, frame, z, z, z); /* palette differs */
    CHECK(es2_screen_references_parse(text, strlen(text), &r, err, sizeof err) == 0);
    es2_screen_run_case(&c, l, &r, NULL, &t);
    CHECK(t.failed == 1);
    es2_screen_references_free(&r);

    /* index differs, no session directory: class unknown */
    snprintf(text, sizeof text, fixture_frames, changed_hash, table, z, z);
    CHECK(es2_screen_references_parse(text, strlen(text), &r, err, sizeof err) == 0);
    es2_screen_run_case(&c, l, &r, NULL, &t);
    CHECK(t.failed == 2);

    /* with the dumped reference frame (capture-tour -Dump layout): font class
     * in main_menu.08 */
    char dir[1024], path[1200];
    snprintf(dir, sizeof dir, "%s/session", tmp);
    make_dir(dir);
    snprintf(dir, sizeof dir, "%s/session/private", tmp);
    make_dir(dir);
    snprintf(dir, sizeof dir, "%s/session/private/dumps", tmp);
    make_dir(dir);
    snprintf(dir, sizeof dir, "%s/session/private/dumps/game_r", tmp);
    make_dir(dir);
    snprintf(path, sizeof path, "%s/%s.idx", dir, changed_hash);
    CHECK(write_file(path, changed, sizeof changed) == 0);
    snprintf(r.session, sizeof r.session, "%s/session", tmp);
    fflush(stdout);
    es2_screen_run_case(&c, l, &r, NULL, &t);
    CHECK(t.failed == 3);
    /* the same difference recorded as known: passes; recorded as another class: fails */
    static const es2_screen_known known_font[] = {{"fixture.start", "start", "game_r:main_menu:start", "font"}};
    static const es2_screen_known known_state[] = {{"fixture.start", "start", "game_r:main_menu:start", "state"}};
    es2_screen_known_use(known_font, 1);
    es2_screen_run_case(&c, l, &r, NULL, &t);
    CHECK(t.passed == 2 && t.failed == 3);
    es2_screen_known_use(known_state, 1);
    es2_screen_run_case(&c, l, &r, NULL, &t);
    CHECK(t.failed == 4);
    /* known without a dumped frame (class unknown): passes */
    es2_screen_known_use(known_font, 1);
    r.session[0] = 0;
    es2_screen_run_case(&c, l, &r, NULL, &t);
    CHECK(t.passed == 3 && t.failed == 4);
    es2_screen_references_free(&r);
    /* a known difference that is gone fails */
    snprintf(text, sizeof text, fixture_frames, frame, table, z, z);
    CHECK(es2_screen_references_parse(text, strlen(text), &r, err, sizeof err) == 0);
    es2_screen_run_case(&c, l, &r, NULL, &t);
    CHECK(t.failed == 5);
    es2_screen_references_free(&r);
    es2_screen_known_use(NULL, 0);

    /* a reference key that is not in frames.json: SKIP, the case passes */
    e.reference = "game_t:main_menu:start";
    snprintf(text, sizeof text, fixture_frames, frame, table, z, z);
    CHECK(es2_screen_references_parse(text, strlen(text), &r, err, sizeof err) == 0);
    es2_screen_run_case(&c, l, &r, NULL, &t);
    CHECK(t.passed == 4);
    es2_screen_references_free(&r);
    /* no references at all: SKIP */
    es2_screen_run_case(&c, l, NULL, NULL, &t);
    CHECK(t.passed == 5);
    /* a real case without ES2_DATA_ROOT: SKIP */
    c.needs_data = 1;
    es2_screen_run_case(&c, l, NULL, NULL, &t);
    CHECK(t.skipped == 1);
}

static void test_fixture_helpers(void) {
    static uint8_t img[64 * 32], img2[64 * 32];
    es2_screen_fixture_image(img, 64, 32, 3);
    es2_screen_fixture_image(img2, 64, 32, 3);
    CHECK(!memcmp(img, img2, sizeof img) && memchr(img, 0, sizeof img));
    es2_screen_fixture_image(img2, 64, 32, 4);
    CHECK(memcmp(img, img2, sizeof img));
    uint8_t p[256][4];
    es2_screen_fixture_palette(p, 1);
    CHECK(p[0][3] == 1 && p[255][3] == 1 && p[17][0] < 64);
    static uint8_t frame[ES2_GUI_PIXELS];
    CHECK(es2_screen_fixture_text(frame, 630, 475, "AB", 9) == 12); /* clipped, no write outside */
    CHECK(frame[479 * ES2_GUI_WIDTH + 639] == 0 || frame[479 * ES2_GUI_WIDTH + 639] == 9);
}

/* ---- main ------------------------------------------------------------------ */

/* Runfiles: the path itself, else the manifest's "_main/<rootpath> <path>" line. */
static int resolve_runfile(const char *rootpath, char *out, size_t size) {
    FILE *f = fopen(rootpath, "rb");
    if (f) {
        fclose(f);
        snprintf(out, size, "%s", rootpath);
        return 1;
    }
    const char *manifest = getenv("RUNFILES_MANIFEST_FILE");
    f = manifest ? fopen(manifest, "r") : NULL;
    char line[2048], key[600];
    snprintf(key, sizeof key, "_main/%s ", rootpath);
    int found = 0;
    while (f && !found && fgets(line, sizeof line, f)) {
        if (strncmp(line, key, strlen(key))) continue;
        char *path = line + strlen(key);
        path[strcspn(path, "\r\n")] = 0;
        snprintf(out, size, "%s", path);
        found = 1;
    }
    if (f) fclose(f);
    return found;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: screen_test <vshell_layout.json> [--suite NAME] [--case PREFIX]\n");
        return 2;
    }
    const char *only_suite = NULL, *only_case = NULL;
    for (int i = 2; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--suite")) only_suite = argv[i + 1];
        else if (!strcmp(argv[i], "--case")) only_case = argv[i + 1];
    }
    static es2_screen_layout layout;
    char err[300];
    char layout_path[1024];
    if (!resolve_runfile(argv[1], layout_path, sizeof layout_path)) {
        fprintf(stderr, "cannot find %s (runfiles)\n", argv[1]);
        return 1;
    }
    if (es2_screen_layout_load(layout_path, &layout, err, sizeof err)) {
        fprintf(stderr, "%s\n", err);
        return 1;
    }
    es2_screen_fixture_set_layout(&layout);
    const char *tmp = getenv("TEST_TMPDIR");
    char tmpdir[1024];
    snprintf(tmpdir, sizeof tmpdir, "%s", tmp && *tmp ? tmp : ".");

    test_layout_parser();
    test_layout_table(&layout);
    test_click(&layout);
    test_keys();
    test_snaps();
    test_compare(&layout);
    test_references_parser();
    test_locate(tmpdir);
    test_reference_compare(&layout, tmpdir);
    test_fixture_helpers();
    printf("harness: %u checks, %u failures\n", checks, failures);

    /* real references */
    const char *data_root = getenv("ES2_DATA_ROOT");
    if (data_root && !*data_root) data_root = NULL;
    es2_screen_references refs;
    int have_refs = 0, refs_failed = 0;
    char frames[1024];
    if (es2_screen_references_locate(frames, sizeof frames)) {
        if (es2_screen_references_load(frames, &refs, err, sizeof err)) {
            printf("FAIL references: %s\n", err);
            refs_failed = 1;
        } else {
            have_refs = 1;
            printf("references: %s (%zu snaps)\n", frames, refs.count);
        }
    } else {
        printf("SKIP real comparison: no capture directory (ES2_VSHELL_SCREENS unset and no "
               "logs/vshell-screens-*/frames.json beside ES2_DATA_ROOT)\n");
    }

    const es2_screen_suite *suites[256];
    size_t n = es2_screen_suites(suites, 256);
    if (n > 256) n = 256;
    es2_screen_totals totals = {0, 0, 0};
    for (size_t i = 0; i < n; i++) {
        if (only_suite && strcmp(suites[i]->name, only_suite)) continue;
        for (size_t k = 0; k < suites[i]->count; k++) {
            const es2_screen_case *c = &suites[i]->cases[k];
            if (only_case && strncmp(c->name, only_case, strlen(only_case))) continue;
            es2_screen_run_case(c, &layout, have_refs ? &refs : NULL, data_root, &totals);
        }
    }
    printf("screens: %zu suites, %u passed, %u failed, %u skipped\n", n, totals.passed, totals.failed, totals.skipped);
    if (have_refs) es2_screen_references_free(&refs);
    es2_screen_layout_free(&layout);
    return failures || totals.failed || refs_failed ? 1 : 0;
}
