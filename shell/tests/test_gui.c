/* The own shell's frame, palette, dirty queue, input path, event queue,
 * main loop and headless mode on synthetic state. No window, no console, no
 * game data (the tables against the reference VSHELL.EXE: gui_tables_real_test). */
#include "es2/shell/gui/headless.h"

#include "es2/formats/sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; fprintf(stderr, "line %d: %s\n", __LINE__, #x); } } while (0)

static es2_gui_rect R(int l, int t, int r, int b) { es2_gui_rect x = {l, t, r, b}; return x; }
static int same(es2_gui_rect a, es2_gui_rect b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

static void test_sha256(void) {
    char hex[65];
    es2f_sha256_of("abc", 3, hex);
    CHECK(!strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    es2f_sha256 s;
    es2f_sha256_init(&s);
    static char block[1001];
    memset(block, 'a', sizeof block);
    for (int i = 0; i < 1000; i++) es2f_sha256_update(&s, block, i % 2 ? 999 : 1001); /* uneven chunks */
    es2f_sha256_hex(&s, hex);
    CHECK(!strcmp(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}

static void test_dirty_queue(void) {
    static es2_gui_frame f;
    es2_gui_frame_init(&f);
    CHECK(es2_gui_invalidate_window(&f, R(10, 10, 20, 20)) == 1);
    CHECK(es2_gui_invalidate_window(&f, R(10, 10, 20, 20)) == 0);  /* equal: contained, borders included */
    CHECK(es2_gui_invalidate_window(&f, R(12, 12, 20, 18)) == 0);  /* touching borders: contained */
    CHECK(f.dirty_count == 1);
    CHECK(es2_gui_invalidate_window(&f, R(100, 100, 110, 110)) == 1);
    CHECK(es2_gui_invalidate_window(&f, R(200, 200, 210, 210)) == 1);
    CHECK(es2_gui_invalidate_window(&f, R(300, 300, 310, 310)) == 1);
    /* strictly encloses the first two: the first is replaced by the last (300..), which is
     * checked again at index 0 and kept; the second by 200.., which is not strictly inside */
    CHECK(es2_gui_invalidate_window(&f, R(9, 9, 201, 211)) == 1);
    CHECK(f.dirty_count == 3);
    CHECK(same(f.dirty[0], R(300, 300, 310, 310)));
    CHECK(same(f.dirty[1], R(200, 200, 210, 210)));
    CHECK(same(f.dirty[2], R(9, 9, 201, 211)));
    /* sharing a border is not strictly inside: kept */
    es2_gui_frame_init(&f);
    es2_gui_invalidate_window(&f, R(10, 10, 20, 20));
    CHECK(es2_gui_invalidate_window(&f, R(10, 5, 30, 30)) == 1);
    CHECK(f.dirty_count == 2);
    /* slot 1 appends without merging */
    CHECK(es2_gui_invalidate_rect(&f, R(10, 10, 20, 20)) == 1);
    CHECK(f.dirty_count == 3);
    /* overflow: own full-frame rectangle */
    es2_gui_frame_init(&f);
    for (int i = 0; i < ES2_GUI_DIRTY_MAX; i++) CHECK(es2_gui_invalidate_window(&f, R(i * 6, 0, i * 6 + 2, 2)) == 1);
    CHECK(f.dirty_count == ES2_GUI_DIRTY_MAX && f.dirty_overflows == 0);
    CHECK(es2_gui_invalidate_window(&f, R(0, 10, 2, 12)) == 0);
    CHECK(f.dirty_count == 1 && f.dirty_overflows == 1 && same(f.dirty[0], R(0, 0, 639, 479)));
}

static void test_palette(void) {
    static es2_gui_frame f;
    uint8_t rec[256][4];
    es2_gui_frame_init(&f);
    memset(rec, 0, sizeof rec);
    rec[1][0] = 63; rec[1][1] = 1; rec[1][2] = 2; rec[1][3] = 1;   /* bit 0: x 4 */
    rec[2][0] = 200; rec[2][1] = 7; rec[2][2] = 9; rec[2][3] = 2;  /* bit 1: unscaled */
    rec[3][0] = 5; rec[3][1] = 5; rec[3][2] = 5; rec[3][3] = 3;    /* bit 0 wins */
    rec[4][0] = 9; rec[4][3] = 0;                                  /* neither: entry kept */
    es2_gui_palette_select(&f, (const uint8_t(*)[4])rec);
    CHECK(f.palette_pending == 1);
    CHECK(es2_gui_palette_apply(&f) == 0);  /* gate 0: nothing reaches the table, pending cleared */
    CHECK(f.palette_pending == 0 && f.uploads == 0 && f.table[1][0] == 0);
    CHECK(es2_gui_palette_apply(&f) == 0);
    f.realize_gate = 1;
    f.table[4][0] = 77;
    es2_gui_palette_select(&f, (const uint8_t(*)[4])rec);
    CHECK(es2_gui_palette_apply(&f) == 1 && f.uploads == 1 && f.recolour == 1);
    CHECK(f.table[1][0] == 252 && f.table[1][1] == 4 && f.table[1][2] == 8);
    CHECK(f.table[2][0] == 200 && f.table[2][1] == 7 && f.table[2][2] == 9);
    CHECK(f.table[3][0] == 20);
    CHECK(f.table[4][0] == 77);
    uint32_t lookup[256];
    es2_gui_palette_lookup(&f, lookup);
    CHECK(lookup[1] == 0xfc0408u);
    /* the same table again: no recolour */
    f.recolour = 0;
    es2_gui_palette_select(&f, (const uint8_t(*)[4])rec);
    CHECK(es2_gui_palette_apply(&f) == 1 && f.uploads == 2 && f.recolour == 0);
}

static void test_recolour(void) {
    static es2_gui_frame f;
    static es2_gui_screen s;
    uint8_t rec[256][4];
    es2_gui_frame_init(&f);
    memset(&s, 0xee, sizeof s);
    memset(rec, 0, sizeof rec);
    f.realize_gate = 1;
    f.pixels[0] = 1;
    rec[1][0] = 10; rec[1][3] = 1;
    CHECK(es2_gui_recolour(&s, &f) == 0 && s.rgb[0] == 0xeeeeeeeeu);
    es2_gui_palette_select(&f, (const uint8_t(*)[4])rec);
    CHECK(es2_gui_palette_apply(&f) == 1 && f.dirty_count == 0);
    /* not dirty, yet converted whole through the new table */
    CHECK(es2_gui_recolour(&s, &f) == 1 && f.recolour == 0 && s.rgb[0] == 0x280000u && s.rgb[639] == 0);
    CHECK(es2_gui_recolour(&s, &f) == 0);
}

static void test_screen(void) {
    static es2_gui_frame f;
    static es2_gui_screen s;
    es2_gui_frame_init(&f);
    memset(&s, 0xee, sizeof s);
    f.table[5][0] = 0x10; f.table[5][1] = 0x20; f.table[5][2] = 0x30;
    memset(f.pixels, 5, sizeof f.pixels);
    /* BitBlt(x, y, w + x, h + y): (10,20)-(19,24) shows (10,20) to (2*10+10, 2*20+5) = (30, 45) exclusive */
    CHECK(es2_gui_screen_present_rect(&s, &f, R(10, 20, 19, 24)) == 20u * 25u);
    CHECK(s.rgb[20 * 640 + 10] == 0x102030u && s.rgb[44 * 640 + 29] == 0x102030u);
    CHECK(s.rgb[45 * 640 + 29] == 0xeeeeeeeeu && s.rgb[44 * 640 + 30] == 0xeeeeeeeeu && s.rgb[20 * 640 + 9] == 0xeeeeeeeeu);
    /* clipped at the client */
    CHECK(es2_gui_screen_present_rect(&s, &f, R(600, 470, 639, 479)) == 40u * 10u);
    CHECK(es2_gui_screen_present_rect(&s, &f, R(5, 5, 4, 9)) == 0);
    /* the colour comes from the table of the moment of the present */
    f.table[5][0] = 0xff;
    es2_gui_invalidate_window(&f, R(0, 0, 0, 0));
    CHECK(es2_gui_flush(&s, &f) == 1 && f.dirty_count == 0);
    CHECK(s.rgb[0] == 0xff2030u && s.rgb[20 * 640 + 10] == 0x102030u);
    es2_gui_screen_present_full(&s, &f);
    CHECK(s.rgb[479 * 640 + 639] == 0xff2030u);
    /* clear: frame zeroed, the whole screen in colour 0 */
    f.table[0][2] = 0x40;
    es2_gui_clear(&s, &f);
    CHECK(f.pixels[1000] == 0 && s.rgb[0] == 0x40u && s.rgb[ES2_GUI_PIXELS - 1] == 0x40u);
    /* presenter rows */
    static uint8_t rows[480 * 2600];
    s.rgb[0] = 0x00a1b2c3u;
    s.rgb[479 * 640 + 639] = 0x00010203u;
    memset(rows, 0x77, sizeof rows);
    CHECK(es2_gui_screen_pixels(&s, ES2_GUI_PIXELS_XRGB8888, rows, 2600));
    CHECK(rows[0] == 0xc3 && rows[1] == 0xb2 && rows[2] == 0xa1 && rows[3] == 0 && rows[4] == 0x40);
    CHECK(rows[479 * 2600 + 639 * 4] == 0x03 && rows[479 * 2600 + 639 * 4 + 2] == 0x01);
    CHECK(rows[2560] == 0x77 && rows[2600] == 0x40);
    CHECK(es2_gui_screen_pixels(&s, ES2_GUI_PIXELS_RGBA, rows, 2560));
    CHECK(rows[0] == 0xa1 && rows[1] == 0xb2 && rows[2] == 0xc3 && rows[3] == 0xff && rows[6] == 0x40);
    CHECK(rows[2560 + 3] == 0xff);
    CHECK(!es2_gui_screen_pixels(&s, ES2_GUI_PIXELS_RGBA, rows, 2559));
}

static void test_layout(void) {
    es2_gui_layout l;
    int x, y;
    CHECK(es2_gui_layout_compute(640, 480, &l) && l.scale == 1 && l.x == 0 && l.y == 0);
    es2_gui_layout_to_frame(&l, 639, 0, &x, &y);
    CHECK(x == 639 && y == 0);
    CHECK(es2_gui_layout_compute(1920, 1080, &l) && l.scale == 2 && l.width == 1280 && l.height == 960 && l.x == 320 &&
          l.y == 60);
    es2_gui_layout_to_frame(&l, 320, 60, &x, &y);
    CHECK(x == 0 && y == 0);
    es2_gui_layout_to_frame(&l, 321, 61, &x, &y);
    CHECK(x == 0 && y == 0);
    es2_gui_layout_to_frame(&l, 1599, 1019, &x, &y);
    CHECK(x == 639 && y == 479);
    es2_gui_layout_to_frame(&l, 319, 59, &x, &y);
    CHECK(x == -1 && y == -1);
    CHECK(es2_gui_layout_compute(2561, 1441, &l) && l.scale == 3 && l.x == 320 && l.y == 0);
    CHECK(es2_gui_layout_compute(600, 300, &l) && l.scale == 0 && l.height == 300 && l.width == 400 && l.x == 100);
    CHECK(!es2_gui_layout_compute(0, 10, &l));
}

typedef struct log { es2_gui_event ev[64]; int n; } log;
static void record(void *context, const es2_gui_event *ev) {
    log *l = context;
    if (l->n < 64) l->ev[l->n++] = *ev;
}
static int drain(es2_gui_input *in, log *l) {
    l->n = 0;
    es2_gui_poll(in);
    es2_gui_drain(in, record, l);
    return l->n;
}
static int mouse_is(const es2_gui_event *e, unsigned code, int x, int y) {
    return e->type == ES2_GUI_EV_MOUSE && e->code == code && e->x == x && e->y == y && !e->synthetic && !e->target;
}

static void test_mouse(void) {
    static es2_gui_input in;
    log l;
    es2_gui_input_init(&in);
    es2_gui_mouse_message_in(&in, ES2_GUI_WM_MOUSEMOVE, 100, 50, 0, 0);
    es2_gui_mouse_message_in(&in, ES2_GUI_WM_LBUTTONDOWN, 100, 50, 1, 0);
    es2_gui_mouse_message_in(&in, ES2_GUI_WM_LBUTTONUP, 100, 50, 0, 0);
    CHECK(drain(&in, &l) == 3);
    CHECK(mouse_is(&l.ev[0], 0, 100, 50) && mouse_is(&l.ev[1], 2, 100, 50) && mouse_is(&l.ev[2], 1, 100, 50));
    /* the position is the pointer at posting time, not at the message */
    es2_gui_mouse_message_in(&in, ES2_GUI_WM_LBUTTONDOWN, 10, 10, 1, 0);
    es2_gui_mouse_message_in(&in, ES2_GUI_WM_MOUSEMOVE, 20, 30, 1, 0);
    CHECK(drain(&in, &l) == 2 && mouse_is(&l.ev[0], 2, 20, 30) && mouse_is(&l.ev[1], 0, 20, 30));
    es2_gui_mouse_message_in(&in, ES2_GUI_WM_LBUTTONUP, 20, 30, 0, 0);
    drain(&in, &l);
    /* a move to the producer's centre has no motion and is deleted */
    es2_gui_mouse_message_in(&in, ES2_GUI_WM_MOUSEMOVE, 320, 240, 0, 0);
    CHECK(drain(&in, &l) == 0);
    /* right button: down 4; a move while it is held reads as released (3, quirk); its up is a move */
    es2_gui_mouse_message_in(&in, ES2_GUI_WM_RBUTTONDOWN, 5, 5, 0, 1);
    es2_gui_mouse_message_in(&in, ES2_GUI_WM_MOUSEMOVE, 6, 5, 0, 1);
    es2_gui_mouse_message_in(&in, ES2_GUI_WM_RBUTTONUP, 6, 5, 0, 0);
    CHECK(drain(&in, &l) == 3 && mouse_is(&l.ev[0], 4, 6, 5) && mouse_is(&l.ev[1], 3, 6, 5) && mouse_is(&l.ev[2], 0, 6, 5));
    /* left down while the right is held: left down, then right up (two records) */
    es2_gui_mouse_message_in(&in, ES2_GUI_WM_RBUTTONDOWN, 6, 5, 0, 1);
    es2_gui_mouse_message_in(&in, ES2_GUI_WM_LBUTTONDOWN, 6, 5, 1, 1);
    CHECK(drain(&in, &l) == 3 && l.ev[0].code == 4 && l.ev[1].code == 2 && l.ev[2].code == 3);
    es2_gui_mouse_message_in(&in, ES2_GUI_WM_LBUTTONUP, 6, 5, 0, 1);
    es2_gui_mouse_message_in(&in, ES2_GUI_WM_RBUTTONUP, 6, 5, 0, 0);
    CHECK(drain(&in, &l) == 2 && l.ev[0].code == 1 && l.ev[1].code == 0);
    /* a movie: button messages only set the skip flag, moves still reach the producer */
    in.movie_open = 1;
    es2_gui_mouse_message_in(&in, ES2_GUI_WM_LBUTTONDOWN, 7, 7, 1, 0);
    CHECK(in.movie_skip == 1);
    es2_gui_mouse_message_in(&in, ES2_GUI_WM_LBUTTONUP, 7, 7, 0, 0);
    es2_gui_mouse_message_in(&in, ES2_GUI_WM_MOUSEMOVE, 8, 7, 0, 0);
    CHECK(drain(&in, &l) == 1 && mouse_is(&l.ev[0], 0, 8, 7));
    in.movie_open = 0;
    in.map_animation = 1;
    es2_gui_mouse_message_in(&in, ES2_GUI_WM_RBUTTONDOWN, 7, 7, 0, 1);
    CHECK(in.map_skip == 1);
    es2_gui_mouse_message_in(&in, ES2_GUI_WM_RBUTTONUP, 7, 7, 0, 0);
    drain(&in, &l);
    /* the producer ring keeps the newest 99 of 150 records */
    for (int i = 0; i < 150; i++) es2_gui_mouse_message_in(&in, ES2_GUI_WM_MOUSEMOVE, i, 1, 0, 0);
    es2_gui_poll(&in);
    CHECK(in.queue_count == 99);
    es2_gui_drain(&in, NULL, NULL);
}

static int command_is(const es2_gui_event *e, unsigned code) { return e->type == ES2_GUI_EV_COMMAND && e->code == code; }
static int key_is(const es2_gui_event *e, unsigned c) { return e->type == ES2_GUI_EV_KEY && e->code == c; }
static void press(es2_gui_input *in, unsigned vk, int shift, int alt, int ctrl) {
    es2_gui_key_message_in(in, vk, 0, shift, alt, ctrl);
    es2_gui_key_message_in(in, vk, 1, shift, alt, ctrl);
}

static void test_keys(void) {
    static es2_gui_input in;
    log l;
    es2_gui_input_init(&in);
    in.interactive = 1;
    CHECK(es2_gui_key_index(0x1b) == 1 && es2_gui_key_index('A') == 30 && es2_gui_key_index(0x2e) == 83);
    CHECK(es2_gui_key_index(0xbd) == 0 && es2_gui_key_index(0x7a) == 0 && es2_gui_key_index(0x10) == 0);
    CHECK(es2_gui_key_vk(72) == 0x26 && es2_gui_key_vk(12) == 0 && es2_gui_key_vk(84) == 0);
    in.cursor_x = 3;
    in.cursor_y = 4;
    press(&in, 'A', 0, 0, 0);
    press(&in, '1', 1, 0, 0);
    press(&in, 'Q', 0, 0, 1);  /* Ctrl+Q: command 0x11 and the poll ends */
    press(&in, 'B', 0, 0, 0);
    CHECK(drain(&in, &l) == 3);
    CHECK(key_is(&l.ev[0], 'A') && l.ev[0].x == 3 && l.ev[0].y == 4 && key_is(&l.ev[1], '!') && command_is(&l.ev[2], 0x11));
    CHECK(drain(&in, &l) == 1 && key_is(&l.ev[0], 'B'));
    /* hot keys: commands for down and up, before the ring's characters */
    press(&in, 'C', 0, 0, 0);
    press(&in, 0x1b, 0, 0, 0);
    press(&in, 0x0d, 0, 0, 0);
    press(&in, 0x09, 0, 0, 0);
    press(&in, 0x26, 0, 0, 0);
    CHECK(drain(&in, &l) == 9);
    CHECK(command_is(&l.ev[0], 0x00) && command_is(&l.ev[1], 0x13) && command_is(&l.ev[2], 0x0a) &&
          command_is(&l.ev[3], 0x1d) && command_is(&l.ev[4], 0x12) && command_is(&l.ev[5], 0x25) &&
          command_is(&l.ev[6], 0x05) && command_is(&l.ev[7], 0x18) && key_is(&l.ev[8], 'C'));
    /* numpad 5 is a hot key with no command; Space and Ctrl+A give characters, F1 nothing */
    press(&in, 0x0c, 0, 0, 0);
    press(&in, ' ', 0, 0, 0);
    press(&in, 'A', 0, 0, 1);
    press(&in, 0x70, 0, 0, 0);
    press(&in, 0x6d, 0, 0, 0);
    CHECK(drain(&in, &l) == 2 && key_is(&l.ev[0], ' ') && key_is(&l.ev[1], 'A'));
    /* a hot key with Shift: VSHELL reads outside its table; the own shell posts nothing */
    press(&in, 0x1b, 1, 0, 0);
    CHECK(drain(&in, &l) == 0);
    /* system keys */
    CHECK(es2_gui_key_message_in(&in, 0x73, 0, 0, 1, 0) == ES2_GUI_SYS_QUIT_BOX); /* Alt+F4 down */
    CHECK(es2_gui_key_message_in(&in, 0x73, 1, 0, 1, 0) == ES2_GUI_SYS_QUIT_BOX); /* and up */
    CHECK(((in.ring_write - in.ring_read) & 0xff) == 2);                          /* both reach the ring */
    CHECK(drain(&in, &l) == 0);
    CHECK(es2_gui_key_message_in(&in, 0x09, 0, 0, 1, 0) == ES2_GUI_SYS_LEAVE_FULLSCREEN);
    CHECK(es2_gui_key_message_in(&in, 0x09, 1, 0, 1, 0) == ES2_GUI_SYS_LEAVE_FULLSCREEN);
    CHECK(es2_gui_key_message_in(&in, 0x1b, 0, 0, 0, 1) == ES2_GUI_SYS_LEAVE_FULLSCREEN);
    CHECK(es2_gui_key_message_in(&in, 0x1b, 0, 0, 1, 0) == ES2_GUI_SYS_LEAVE_FULLSCREEN);
    CHECK(es2_gui_key_message_in(&in, 0x0d, 0, 0, 1, 0) == ES2_GUI_SYS_NONE);
    CHECK(es2_gui_key_message_in(&in, 0x0d, 1, 0, 1, 0) == ES2_GUI_SYS_TOGGLE_FULLSCREEN);
    CHECK(in.hot_read == in.hot_write && in.ring_read == in.ring_write);           /* none of them queued */
    /* the system keys act only while interactive and without a movie; still never queued */
    in.interactive = 0;
    CHECK(es2_gui_key_message_in(&in, 0x09, 0, 0, 1, 0) == ES2_GUI_SYS_NONE);
    CHECK(es2_gui_key_message_in(&in, 0x73, 0, 0, 1, 0) == ES2_GUI_SYS_NONE);
    in.interactive = 1;
    in.movie_open = 1;
    CHECK(es2_gui_key_message_in(&in, 0x0d, 1, 0, 1, 0) == ES2_GUI_SYS_NONE);
    in.movie_open = 0;
    CHECK(drain(&in, &l) == 0);
    /* the hot queue keeps the newest nine of twelve */
    for (int i = 0; i < 12; i++) es2_gui_key_message_in(&in, i % 2 ? 0x25 : 0x27, 0, 0, 0, 0);
    CHECK(drain(&in, &l) == 9 && command_is(&l.ev[0], 0x04) && command_is(&l.ev[8], 0x04));
    /* a full ring (255 words) drops the key */
    for (int i = 0; i < 300; i++) es2_gui_key_message_in(&in, 'D', 0, 0, 0, 0);
    CHECK(((in.ring_write - in.ring_read) & 0xff) == 255);
    es2_gui_poll(&in);
    CHECK(in.queue_count == 255);
    es2_gui_drain(&in, NULL, NULL);
    /* -k: no keyboard device: hot keys land in the ring and nothing is posted */
    es2_gui_input_init(&in);
    in.keyboard = 0;
    press(&in, 0x1b, 0, 0, 0);
    press(&in, 'E', 0, 0, 0);
    CHECK(drain(&in, &l) == 0 && in.ring_write == 4);
    /* characters */
    CHECK(es2_gui_key_character(43) == '\\' && es2_gui_key_character(43 | ES2_GUI_KEY_SHIFT) == 0);
    CHECK(es2_gui_key_character(26) == '[' && es2_gui_key_character(26 | ES2_GUI_KEY_SHIFT) == 0);
    CHECK(es2_gui_key_character(40 | ES2_GUI_KEY_SHIFT) == '"' && es2_gui_key_character(11) == '0');
    CHECK(es2_gui_key_character(57 | ES2_GUI_KEY_ALT) == ' ' && es2_gui_key_character(83) == 0);
    CHECK(es2_gui_hot_command(0x81) == 0x13 && es2_gui_hot_command(12) == 0xff && es2_gui_hot_command(0x801) == 0xff);
}

/* Hot keys with Shift, Ctrl or Alt read the command byte past the 256-byte
 * table (0x409587, 0x409605). A synthetic stand-in for the executable's bytes. */
static void test_modifier_hot_keys(void) {
    static es2_gui_input in;
    static uint8_t bytes[ES2_GUI_HOT_BYTES_SIZE];
    log l;
    memset(bytes, 0xff, sizeof bytes);
    bytes[0x801] = 5;     /* Shift+Esc down */
    bytes[0xcc9] = 5;     /* Shift+Ctrl+PgUp up */
    bytes[0xec7] = 4;     /* Shift+Ctrl+Alt+Home up */
    bytes[0x80f] = 0;     /* Shift+Tab down: the byte 0 is an event too (Esc) */
    bytes[0x40e] = 0xfe;  /* Ctrl+Backspace down: a byte above 0x7f is posted as it is */
    bytes[0x201] = bytes[0x281] = 0x33; /* Alt+Esc, both forms: never reach the hook */
    bytes[0x401] = bytes[0x481] = 0x33; /* Ctrl+Esc */
    bytes[0x20f] = bytes[0x28f] = 0x33; /* Alt+Tab */
    bytes[0x21c] = bytes[0x29c] = 0x33; /* Alt+Enter */
    bytes[0x24b] = 0x0a;                /* Alt+Left down */
    es2_gui_input_init(&in);
    in.interactive = 1;
    in.cursor_x = 7;
    in.cursor_y = 9;
    /* without the executable's bytes: nothing (the old deviation) */
    CHECK(es2_gui_input_hot_command(&in, 0x801) == 0xff && es2_gui_input_hot_command(&in, 0x81) == 0x13);
    press(&in, 0x1b, 1, 0, 0);
    CHECK(drain(&in, &l) == 0);
    /* a short block is refused */
    CHECK(!es2_gui_input_set_hot_bytes(&in, bytes, ES2_GUI_HOT_BYTES_SIZE - 1) && !in.hot_bytes_loaded);
    CHECK(!es2_gui_input_set_hot_bytes(&in, NULL, ES2_GUI_HOT_BYTES_SIZE) && !es2_gui_input_set_hot_bytes(NULL, bytes, sizeof bytes));
    CHECK(es2_gui_input_set_hot_bytes(&in, bytes, sizeof bytes + 8) && in.hot_bytes_loaded);
    CHECK(es2_gui_input_hot_command(&in, 0x801) == 5 && es2_gui_input_hot_command(&in, 0x881) == 0xff);
    CHECK(es2_gui_input_hot_command(&in, 0xed3) == 0xff && es2_gui_input_hot_command(&in, 0x1000) == 0xff);
    /* words below 0x100 stay the built-in table, whatever the block says */
    bytes[0x01] = 0x77;
    CHECK(es2_gui_input_set_hot_bytes(&in, bytes, sizeof bytes) && es2_gui_input_hot_command(&in, 1) == 0x00);
    /* Shift+Esc: the down word reads 5 ("Up"), the up word nothing */
    press(&in, 0x1b, 1, 0, 0);
    CHECK(drain(&in, &l) == 1 && command_is(&l.ev[0], 0x05) && l.ev[0].x == 7 && l.ev[0].y == 9);
    /* Shift+Ctrl+PgUp: only the up word; Shift+Ctrl+Alt+Home: only the up word, command 4 */
    press(&in, 0x21, 1, 0, 1);
    press(&in, 0x24, 1, 1, 1);
    CHECK(drain(&in, &l) == 2 && command_is(&l.ev[0], 0x05) && command_is(&l.ev[1], 0x04));
    /* byte 0 and bytes above 0x7f are events like the others; the order of the hot queue holds */
    press(&in, 0x09, 1, 0, 0);
    press(&in, 0x08, 0, 0, 1);
    press(&in, 0x25, 0, 1, 0);
    CHECK(drain(&in, &l) == 3 && command_is(&l.ev[0], 0x00) && command_is(&l.ev[1], 0xfe) && command_is(&l.ev[2], 0x0a));
    /* the system words never reach the hook, so their bytes are never read */
    CHECK(es2_gui_key_message_in(&in, 0x1b, 0, 0, 1, 0) == ES2_GUI_SYS_LEAVE_FULLSCREEN);
    CHECK(es2_gui_key_message_in(&in, 0x1b, 1, 0, 1, 0) == ES2_GUI_SYS_LEAVE_FULLSCREEN);
    CHECK(es2_gui_key_message_in(&in, 0x1b, 0, 0, 0, 1) == ES2_GUI_SYS_LEAVE_FULLSCREEN);
    CHECK(es2_gui_key_message_in(&in, 0x1b, 1, 0, 0, 1) == ES2_GUI_SYS_LEAVE_FULLSCREEN);
    CHECK(es2_gui_key_message_in(&in, 0x09, 0, 0, 1, 0) == ES2_GUI_SYS_LEAVE_FULLSCREEN);
    CHECK(es2_gui_key_message_in(&in, 0x09, 1, 0, 1, 0) == ES2_GUI_SYS_LEAVE_FULLSCREEN);
    es2_gui_key_message_in(&in, 0x0d, 0, 0, 1, 0);
    es2_gui_key_message_in(&in, 0x0d, 1, 0, 1, 0);
    CHECK(drain(&in, &l) == 0 && in.hot_read == in.hot_write);
    /* a key without a hot-key entry (Alt+A, Ctrl+Q) takes its old path */
    press(&in, 'A', 0, 1, 0);
    press(&in, 'Q', 0, 0, 1);
    CHECK(drain(&in, &l) == 2 && key_is(&l.ev[0], 'A') && command_is(&l.ev[1], 0x11));
    /* -k: with no keyboard device nothing is posted, bytes or not */
    in.keyboard = 0;
    press(&in, 0x1b, 1, 0, 0);
    CHECK(drain(&in, &l) == 0);
}

typedef struct poster { es2_gui_input *in; log l; } poster;
static void post_once(void *context, const es2_gui_event *ev) {
    poster *p = context;
    if (p->l.n < 64) p->l.ev[p->l.n++] = *ev;
    if (ev->type == ES2_GUI_EV_COMMAND) {
        es2_gui_event redraw = {ES2_GUI_EV_REDRAW, 0, 0, 0, 0, 9};
        es2_gui_post(p->in, redraw);
    }
}

static void test_queue(void) {
    static es2_gui_input in;
    poster p = {&in, {.n = 0}};
    es2_gui_input_init(&in);
    in.cursor_x = 11;
    in.cursor_y = 12;
    es2_gui_post_synthetic_click(&in, 77);
    es2_gui_event command = {ES2_GUI_EV_COMMAND, 0, 0, 0x0a, 0, 0};
    es2_gui_post(&in, command);
    CHECK(es2_gui_drain(&in, post_once, &p) == 4); /* the redraw posted by the handler runs in the same drain */
    CHECK(p.l.n == 4);
    CHECK(p.l.ev[0].code == 2 && p.l.ev[0].synthetic == 1 && p.l.ev[0].target == 77 && p.l.ev[0].x == 11);
    CHECK(p.l.ev[1].code == 1 && p.l.ev[1].synthetic == 1 && p.l.ev[1].target == 77 && p.l.ev[1].y == 12);
    CHECK(p.l.ev[3].type == ES2_GUI_EV_REDRAW && p.l.ev[3].target == 9);
    for (int i = 0; i < ES2_GUI_QUEUE_MAX + 5; i++) es2_gui_post(&in, command);
    CHECK(in.queue_count == ES2_GUI_QUEUE_MAX && in.queue_overflows == 5);
}

static void test_args(void) {
    es2_gui_args a;
    char *v1[] = {"dbsim", "dummy", "-eggplant", "-X3", "-s", "-a", "-@", "-G"};
    es2_gui_parse_args(8, v1, &a);
    CHECK(a.entry_mode == 3 && a.eggplant && !a.sound && !a.movies && a.cheat && a.language == 2 && a.keyboard);
    char *v2[] = {"dbsim", "-x5", "/eggplant", "-X", "-k", "-F", "/r", "-m", "-L", "-V"};
    es2_gui_parse_args(10, v2, &a);
    CHECK(a.entry_mode == 0 && !a.eggplant && a.language == 1 && !a.keyboard && a.m_flag && !a.l_word && a.version);
    char *v3[] = {"dbsim", "-e", "-X70000", "/?", "-", "dummy", "-X6"};
    es2_gui_parse_args(7, v3, &a);
    CHECK(a.language == 3 && a.usage && !a.sound && a.entry_mode == 6 && a.movies);
    char *v4[] = {"dbsim", "-X70000"};
    es2_gui_parse_args(2, v4, &a);
    CHECK(a.entry_mode == (short)70000 && a.language == -1);
    /* CW3220's _atol: 32-bit wrap (the host CRT saturates) and the even bytes 0x80..0xfe as spaces */
    char *v5[] = {"dbsim", "-X4294967299"};
    es2_gui_parse_args(2, v5, &a);
    CHECK(a.entry_mode == 3);
    char *v6[] = {"dbsim", "-X\x82" "4"};
    es2_gui_parse_args(2, v6, &a);
    CHECK(a.entry_mode == 4);
    /* 0x401bd4..0x401bed: entry modes 3 and 4 set the gate at start, the others leave it 0 */
    static es2_gui_shell shell;
    for (int mode = 0; mode <= 6; mode++) {
        a.entry_mode = mode;
        es2_gui_shell_init(&shell, &a, NULL, NULL);
        CHECK(shell.frame.realize_gate == (mode == 3 || mode == 4));
    }
}

static const char *find_line(const char *report, const char *prefix, int nth) {
    const char *p = report;
    while (p && *p) {
        if (!strncmp(p, prefix, strlen(prefix)) && nth-- == 0) return p;
        p = strchr(p, '\n');
        if (p) p++;
    }
    return NULL;
}
static int line_has(const char *line, const char *text) {
    const char *end = line ? strchr(line, '\n') : NULL;
    const char *hit = line ? strstr(line, text) : NULL;
    return hit && (!end || hit < end);
}

static void test_headless(void) {
    static const char script[] =
        "# no window: the clear, a pattern, two passes\n"
        "clear\n"
        "pattern\n"
        "step\n"
        "interactive 1\n"
        "move 100 50\n"
        "ldown\n"
        "lup\n"
        "key esc\n"
        "key a shift\n"
        "step\n"
        "fill 10 10 19 19 7\n"
        "fill 12 12 15 15 9\n"
        "step\n"
        "focus 0\n"
        "fill 0 0 0 0 1\n"
        "move 1 1\n"
        "step 2\n"
        "focus 1\n"
        "paint\n"
        "step\n"
        "palette 3\n"
        "invalidate 0 0 9 9\n"
        "click 5\n"
        "key enter alt\n"
        "key tab alt\n"
        "key f4 alt\n"
        "busy 1\n"
        "step\n"
        "bogus 1\n"
        "close\n"
        "step\n"
        "key a\n";
    es2_gui_headless_result r, r2;
    CHECK(es2_gui_headless_run(script, NULL, NULL, &r) == 0);
    CHECK(es2_gui_headless_run(script, NULL, NULL, &r2) == 0);
    CHECK(r.length == r2.length && !memcmp(r.report, r2.report, r.length)); /* deterministic */
    const char *clear = find_line(r.report, "present 1 ", 0);
    CHECK(line_has(clear, "kind=clear rects=0 frame=7818f5542a0404157573be6cffc0e0c8e68ce3c0f5d17d07ccdd9313fb700baf"));
    CHECK(line_has(clear, "table=ef115a0e0c15cdc41958ca46b5b14b456115f4baec5e3ca68599d2a8f435e3b8"));
    const char *first = find_line(r.report, "present 2 ", 0);
    CHECK(line_has(first, "pass=1 kind=dirty rects=1"));
    CHECK(!line_has(first, "table=ef115a0e")); /* the pattern's palette was applied before the flush */
    CHECK(line_has(find_line(r.report, "event 2 ", 0), "type=0x20 code=0x0 x=100 y=50"));
    CHECK(line_has(find_line(r.report, "event 2 ", 1), "type=0x20 code=0x2 x=100 y=50"));
    CHECK(line_has(find_line(r.report, "event 2 ", 2), "type=0x20 code=0x1"));
    CHECK(line_has(find_line(r.report, "event 2 ", 3), "type=0x100 code=0x0 "));
    CHECK(line_has(find_line(r.report, "event 2 ", 4), "type=0x100 code=0x13"));
    CHECK(line_has(find_line(r.report, "event 2 ", 5), "type=0x40 code=0x41"));
    CHECK(find_line(r.report, "event 2 ", 6) == NULL);
    CHECK(line_has(find_line(r.report, "present 3 ", 0), "pass=3 kind=dirty rects=1 ")); /* the inner fill merged */
    /* unfocused passes 4 and 5: nothing; focus back: the queued move and the paint */
    CHECK(find_line(r.report, "present 4 ", 0) && line_has(find_line(r.report, "present 4 ", 0), "pass=6 kind=full"));
    CHECK(line_has(find_line(r.report, "present 5 ", 0), "pass=6 kind=dirty rects=1"));
    CHECK(line_has(find_line(r.report, "event 6 ", 0), "type=0x20 code=0x0 x=1 y=1"));
    CHECK(find_line(r.report, "event 4 ", 0) == NULL && find_line(r.report, "event 5 ", 0) == NULL);
    CHECK(line_has(find_line(r.report, "system 7 ", 0), "toggle_fullscreen"));
    CHECK(line_has(find_line(r.report, "system 7 ", 1), "leave_fullscreen"));
    CHECK(line_has(find_line(r.report, "system 7 ", 2), "leave_fullscreen"));
    CHECK(line_has(find_line(r.report, "system 7 ", 3), "quit_box"));
    CHECK(line_has(find_line(r.report, "system 7 ", 4), "quit_box"));
    CHECK(find_line(r.report, "fullscreen 1", 0) && find_line(r.report, "fullscreen 0", 0));
    CHECK(find_line(r.report, "cursor busy", 0) != NULL);
    CHECK(line_has(find_line(r.report, "event 7 ", 0), "code=0x2 x=1 y=1 synthetic=1 target=5"));
    CHECK(line_has(find_line(r.report, "event 7 ", 1), "code=0x1 x=1 y=1 synthetic=1 target=5"));
    CHECK(find_line(r.report, "event 7 ", 2) == NULL);
    CHECK(line_has(find_line(r.report, "present 6 ", 0), "pass=7 kind=dirty rects=1"));
    CHECK(r.errors == 1 && find_line(r.report, "error 30 bogus 1", 0));
    CHECK(line_has(find_line(r.report, "end ", 0), "passes=8 presents=6 quit=1")); /* close ends; the last key never runs */
    free(r.report);
    free(r2.report);
    /* not interactive: close does nothing; quit (WM_QUIT) ends */
    CHECK(es2_gui_headless_run("close\nstep 3\nquit\nstep\nstep\n", NULL, NULL, &r) == 0);
    CHECK(line_has(find_line(r.report, "end ", 0), "passes=4 presents=0 quit=1"));
    free(r.report);
    /* WM_PAINT before the realization gate presents nothing */
    CHECK(es2_gui_headless_run("paint\nstep\ngate 1\npaint\nstep\n", NULL, NULL, &r) == 0);
    CHECK(line_has(find_line(r.report, "present 1 ", 0), "pass=2 kind=full"));
    CHECK(find_line(r.report, "present 2 ", 0) == NULL);
    free(r.report);
}

int main(void) {
    test_sha256();
    test_dirty_queue();
    test_palette();
    test_recolour();
    test_screen();
    test_layout();
    test_mouse();
    test_keys();
    test_modifier_hot_keys();
    test_queue();
    test_args();
    test_headless();
    printf("%u checks, %u failures\n", checks, failures);
    return failures != 0;
}
