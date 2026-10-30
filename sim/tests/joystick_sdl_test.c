/* The SDL joystick's winmm answers over a fake backend (no device, no SDL
 * subsystem): the JOYCAPSA and JOYINFOEX layouts, the error numbers, the
 * gamepad and plain mappings, the mode selection. */
#include "es2/sim/joystick_sdl.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x); ++failures; } } while (0)

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

typedef struct fake {
    int present[2];
    es2_sim_joystick_state state[2];
    unsigned rebinds, reads;
} fake;
static int fake_read(void *user, uint32_t id, int rebind, es2_sim_joystick_state *out) {
    fake *f = user;
    ++f->reads;
    if (rebind) ++f->rebinds;
    if (id > 1u || !f->present[id]) return 1;
    if (f->present[id] == 2) return 2;
    *out = f->state[id];
    return 0;
}

static void test_caps(void) {
    fake f;
    memset(&f, 0, sizeof f);
    es2_sim_joystick_backend b = {fake_read, &f};
    int16_t a[6] = {0, 0, 0, 0, 0, 0};
    es2_sim_joystick_map_gamepad(a, 0, 0, &f.state[0]);
    f.state[0].vendor = 0x045e;
    f.state[0].product = 0x028e;
    memset(f.state[0].name, 'x', sizeof f.state[0].name);              /* no NUL: caps cuts at 31 */
    memcpy(f.state[0].name, "A pad with a name", 17);
    f.present[0] = 1;
    uint8_t buf[0x328];
    memset(buf, 0xcd, sizeof buf);
    CHECK(es2_sim_joystick_caps(&b, 0, buf, sizeof buf) == 0);
    CHECK(f.rebinds == 1);
    CHECK(buf[0] == 0x5e && buf[1] == 0x04 && buf[2] == 0x8e && buf[3] == 0x02);
    CHECK(buf[4 + 31] == 0 && !memcmp(buf + 4, "A pad with a name", 17));
    CHECK(get32(buf + 0x24) == 0 && get32(buf + 0x28) == 0xffffu);       /* wXmin, wXmax */
    CHECK(get32(buf + 0x5c) == 0xffffu);                                 /* wVmax */
    CHECK(get32(buf + 0x3c) == 10u);                                     /* wNumButtons */
    CHECK(get32(buf + 0x60) == 0x5fu);                                   /* HASZ R U V POV POVCTS */
    CHECK(get32(buf + 0x64) == 6u && get32(buf + 0x68) == 6u && get32(buf + 0x6c) == 32u);
    CHECK(buf[0x193] == 0 && buf[0x194] == 0xcd);                        /* the A layout only */
    /* A short buffer gets its prefix; no buffer, no device: JOYERR_PARMS. */
    memset(buf, 0xcd, sizeof buf);
    CHECK(es2_sim_joystick_caps(&b, 0, buf, 8) == 0);
    CHECK(buf[7] != 0xcd && buf[8] == 0xcd);
    CHECK(es2_sim_joystick_caps(&b, 0, NULL, 0x328) == ES2_JOYERR_PARMS);
    CHECK(es2_sim_joystick_caps(&b, 1, buf, 0x328) == ES2_JOYERR_PARMS);
    CHECK(es2_sim_joystick_caps(&b, 7, buf, 0x328) == ES2_JOYERR_PARMS);
    /* A plain two-axis stick: no Z, R, POV. */
    int16_t p[2] = {-32768, 32767};
    es2_sim_joystick_map_plain(p, 2, 0x5, 4, 0, 0, &f.state[1]);
    f.present[1] = 1;
    CHECK(es2_sim_joystick_caps(&b, 1, buf, 0x328) == 0);
    CHECK(get32(buf + 0x60) == 0 && get32(buf + 0x68) == 2u && get32(buf + 0x3c) == 4u);
}

static void test_pos(void) {
    fake f;
    memset(&f, 0, sizeof f);
    es2_sim_joystick_backend b = {fake_read, &f};
    int16_t a[6] = {-32768, 32767, 100, -100, 32767, -5};
    es2_sim_joystick_map_gamepad(a, 0x481u | 0xfc00u, 0x1, &f.state[0]);
    f.present[0] = 1;
    uint8_t info[0x34];
    memset(info, 0xcd, sizeof info);
    put32(info, 0x34);
    put32(info + 4, 0xccfu);
    CHECK(es2_sim_joystick_pos(&b, 0, info, sizeof info) == 0);
    CHECK(f.rebinds == 0);
    CHECK(get32(info + 0x08) == 0u);                  /* X: left x -32768 */
    CHECK(get32(info + 0x0c) == 65535u);              /* Y: left y 32767 */
    CHECK(get32(info + 0x10) == 32768u - 100u);       /* Z: right y */
    CHECK(get32(info + 0x14) == 32868u);              /* R: right x */
    CHECK(get32(info + 0x18) == 0xcdcdcdcdu);         /* U, V not asked by 0xccf */
    CHECK(get32(info + 0x1c) == 0xcdcdcdcdu);
    CHECK(get32(info + 0x20) == 0x81u);               /* buttons 0..9 only */
    CHECK(get32(info + 0x24) == 2u);
    CHECK(get32(info + 0x28) == 0u);                  /* d-pad up */
    CHECK(get32(info + 0x2c) == 0xcdcdcdcdu);
    put32(info + 4, 0x30u);                           /* U, V */
    CHECK(es2_sim_joystick_pos(&b, 0, info, sizeof info) == 0);
    CHECK(get32(info + 0x18) == 65535u && get32(info + 0x1c) == 0u);
    /* dwSize wrong, buffer short: PARMS; no device: UNPLUGGED. */
    put32(info, 0x30);
    CHECK(es2_sim_joystick_pos(&b, 0, info, sizeof info) == ES2_JOYERR_PARMS);
    put32(info, 0x34);
    CHECK(es2_sim_joystick_pos(&b, 0, info, 0x30) == ES2_JOYERR_PARMS);
    CHECK(es2_sim_joystick_pos(&b, 1, info, sizeof info) == ES2_JOYERR_PARMS);
    CHECK(es2_sim_joystick_pos(NULL, 0, info, sizeof info) == ES2_JOYERR_PARMS);
    f.present[1] = 2;
    CHECK(es2_sim_joystick_pos(&b, 1, info, sizeof info) == ES2_JOYERR_UNPLUGGED);
    /* A plain stick without hat: absent axes centred, POV centred. */
    int16_t p[3] = {0, 1, 2};
    es2_sim_joystick_map_plain(p, 3, 0xffffffffu, 40, 0, 0, &f.state[0]);
    put32(info + 4, 0xffu);
    CHECK(es2_sim_joystick_pos(&b, 0, info, sizeof info) == 0);
    CHECK(get32(info + 0x10) == 32770u && get32(info + 0x14) == 0x8000u);
    CHECK(get32(info + 0x20) == 0xffffffffu && get32(info + 0x24) == 32u);
    CHECK(get32(info + 0x28) == ES2_JOY_POVCENTERED);
}

static void test_pov(void) {
    CHECK(es2_sim_joystick_pov(0) == ES2_JOY_POVCENTERED);
    CHECK(es2_sim_joystick_pov(1) == 0);
    CHECK(es2_sim_joystick_pov(2) == 9000u);
    CHECK(es2_sim_joystick_pov(4) == 18000u);
    CHECK(es2_sim_joystick_pov(8) == 27000u);
    CHECK(es2_sim_joystick_pov(3) == 4500u);
    CHECK(es2_sim_joystick_pov(9) == 31500u);
    CHECK(es2_sim_joystick_pov(5) == ES2_JOY_POVCENTERED);
    int16_t a[2] = {0, 0};
    es2_sim_joystick_state s;
    memset(&s, 0, sizeof s);
    es2_sim_joystick_map_plain(a, 2, 0, 2, 1, 0x6, &s);
    CHECK(s.hat && s.pov == 13500u);
}

static uint32_t keep_caps(void *user, uint32_t id, uint8_t *caps, uint32_t size) { (void)user; (void)id; (void)caps; (void)size; return 6; }
static uint32_t keep_pos(void *user, uint32_t id, uint8_t *info, uint32_t size) { (void)user; (void)id; (void)info; (void)size; return 6; }
static void test_select(void) {
    static es2_sim_joystick_sdl s;
    int user = 0;
    es2_sim_window_joystick seam = {keep_caps, keep_pos, &user};
    CHECK(es2_sim_joystick_select(&seam, &s, NULL) == 0);
    CHECK(es2_sim_joystick_select(&seam, &s, "") == 0);
    CHECK(es2_sim_joystick_select(&seam, &s, "winmm") == 0);
    CHECK(seam.caps == keep_caps && seam.pos == keep_pos && seam.user == &user);
    CHECK(es2_sim_joystick_select(&seam, &s, "xinput") == 1);
    CHECK(strstr(s.error, "expected winmm or sdl") != NULL);
    CHECK(seam.caps == keep_caps && seam.pos == keep_pos && seam.user == &user);
}

int main(void) {
    test_caps();
    test_pos();
    test_pov();
    test_select();
    if (failures) { fprintf(stderr, "joystick_sdl_test: %d failures\n", failures); return 1; }
    printf("joystick_sdl_test: passed\n");
    return 0;
}
