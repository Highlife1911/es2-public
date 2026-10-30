/* es2shell's music fades (gui/app_sound.h) pump as VSHELL's
 * 0x42f894 / 0x42f7f0: the pump 0x406328 before every volume test, a step
 * when the clock advanced by more than 10 ms, WM_QUIT ends the fade; input
 * delivered by the pump is queued, not dispatched, during the fade. */
#include "gui/app_sound.h"
#include "es2/shell/gui/loop.h"

#include <stdio.h>
#include <string.h>

static int failures, checks;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        checks++;                                                                                                      \
        if (!(c)) {                                                                                                    \
            failures++;                                                                                                \
            fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c);                                             \
        }                                                                                                              \
    } while (0)

typedef struct fake {
    uint32_t now, step;   /* virtual clock: + step per read */
    int pumps, quit_at;   /* pump calls; return 1 on call quit_at (0: never) */
    int input_at;         /* on this pump call: a click and a key into `shell` */
    es2_gui_shell *shell;
    int volume;           /* from the log's last "sound music volume" line */
} fake;

static uint32_t clock_(void *ctx) {
    fake *f = ctx;
    f->now += f->step;
    return f->now;
}
static int pump_(void *ctx) {
    fake *f = ctx;
    f->pumps++;
    if (f->shell && f->pumps == f->input_at) {
        es2_gui_on_mouse(f->shell, ES2_GUI_WM_LBUTTONDOWN, 100, 100, 1, 0);
        es2_gui_on_mouse(f->shell, ES2_GUI_WM_LBUTTONUP, 100, 100, 0, 0);
        es2_gui_on_key(f->shell, 'A', 0, 0, 0, 0);
    }
    return f->quit_at && f->pumps == f->quit_at;
}
static void log_(void *ctx, const char *line) {
    fake *f = ctx;
    int v;
    if (sscanf(line, "sound music volume %d", &v) == 1) f->volume = v;
}
static int handled;
static void handler(void *ctx, const es2_gui_event *e) {
    (void)ctx, (void)e;
    handled++;
}

static es2_app_sound *open_sound(fake *f, uint8_t prefs[ES2_SHELL_PREFS_SIZE], int music) {
    es2_shell_prefs_session session;
    uint8_t out[ES2_SHELL_PREFS_SIZE];
    memset(&session, 0, sizeof session);
    es2_app_sound *s = es2_app_sound_create();
    es2_app_sound_set_log(s, log_, f);
    es2_app_sound_startup(s, 1, es2_app_sound_null_device, NULL, &session, out);
    memset(prefs, 0, ES2_SHELL_PREFS_SIZE);
    prefs[0] = (uint8_t)music;   /* byte 0: music on */
    return s;
}

int main(void) {
    uint8_t prefs[ES2_SHELL_PREFS_SIZE];
    es2_app_sound *s;

    /* fade-in 0 -> 100, a step on every pass (11 ms > 10): 100 steps, the
     * pump once more before the final test (0x42f8e9 before 0x42f8f7) */
    fake f = {.step = 11};
    s = open_sound(&f, prefs, 1);
    CHECK(es2_app_sound_ramp(s, 1, prefs, clock_, &f, pump_, &f) == 0);
    CHECK(f.volume == 100);
    CHECK(f.pumps == 101);
    /* fade-out 100 -> 1 (ends above 0, 0x42f878..0x42f87e): 99 steps */
    f.pumps = 0;
    CHECK(es2_app_sound_ramp(s, 0, prefs, clock_, &f, pump_, &f) == 0);
    CHECK(f.volume == 1);
    CHECK(f.pumps == 100);
    /* at the end already: one pump, no step */
    f.pumps = 0;
    f.volume = -1;
    CHECK(es2_app_sound_ramp(s, 0, prefs, clock_, &f, pump_, &f) == 0);
    CHECK(f.volume == 1);
    CHECK(f.pumps == 1);
    es2_app_sound_destroy(s);

    /* exactly 10 ms is not enough (jbe at 0x42f8c0): 5 ms per read steps
     * every third pass (15 ms) */
    f = (fake){.step = 5};
    s = open_sound(&f, prefs, 1);
    CHECK(es2_app_sound_ramp(s, 1, prefs, clock_, &f, pump_, &f) == 0);
    CHECK(f.volume == 100);
    CHECK(f.pumps == 301);
    es2_app_sound_destroy(s);
    f = (fake){.step = 10};
    s = open_sound(&f, prefs, 1);
    f.quit_at = 50;
    CHECK(es2_app_sound_ramp(s, 1, prefs, clock_, &f, pump_, &f) == 1);
    CHECK(f.volume == 24);   /* 10 ms per read: a step every second pass */
    es2_app_sound_destroy(s);

    /* WM_QUIT on the 5th pump: the fade ends at once after 4 steps */
    f = (fake){.step = 11, .quit_at = 5};
    s = open_sound(&f, prefs, 1);
    CHECK(es2_app_sound_ramp(s, 1, prefs, clock_, &f, pump_, &f) == 1);
    CHECK(f.volume == 4);
    CHECK(f.pumps == 5);
    es2_app_sound_destroy(s);

    /* music off (prefs byte 0 = 0): no fade, no pump */
    f = (fake){.step = 11, .volume = -1};
    s = open_sound(&f, prefs, 0);
    CHECK(es2_app_sound_ramp(s, 1, prefs, clock_, &f, pump_, &f) == 0);
    CHECK(f.pumps == 0);
    CHECK(f.volume == -1);
    es2_app_sound_destroy(s);

    /* a click and a key during the fade: the pump only runs the window
     * procedure's path, so both are queued (mouse ring, key ring) and no
     * event is dispatched until the shell's loop polls after the fade */
    static es2_gui_shell shell;
    es2_gui_hooks hooks = {0};
    hooks.handle = handler;
    es2_gui_shell_init(&shell, NULL, NULL, &hooks);
    f = (fake){.step = 11, .input_at = 30, .shell = &shell};
    s = open_sound(&f, prefs, 1);
    handled = 0;
    CHECK(es2_app_sound_ramp(s, 1, prefs, clock_, &f, pump_, &f) == 0);
    CHECK(f.volume == 100);
    CHECK(handled == 0);
    CHECK(shell.input.mouse_write != shell.input.mouse_read);
    CHECK(shell.input.ring_write != shell.input.ring_read);
    es2_gui_poll(&shell.input);
    CHECK(shell.input.queue_count > 0);
    es2_app_sound_destroy(s);

    printf("app_sound_test: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
