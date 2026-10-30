/* es2shell.exe: the own graphical shell, started
 * by es2launch through ES2_LAUNCH_SHELL with VSHELL's argv appended. GUI
 * subsystem like VSHELL.EXE: no console, also not in headless mode.
 *
 *   es2shell --menu DIR [--headless --script FILE --report FILE] [--fullscreen] <VSHELL argv>
 *       the shell: menu.ini of DIR as es2menu reads it (game_dir,
 *       cd_root, seed, chain_dir, cd_check, online_manual), the screens over
 *       the flow, answers the launcher like VSHELL (2 DBSIM, 5 demo, 0 quit,
 *       1 after an assertion); shell.log in DIR records the flow events
 *   es2shell [--headless --script FILE --report FILE] [--fullscreen] <VSHELL argv>
 *       without --menu: the synthetic test pattern (the frame alone)
 *
 * Own options are removed before VSHELL's parser sees the rest
 * (vshell_process.md, "the own shell process frame"). The headless script of
 * the shell (--menu with --headless): one command per line, '#' a comment:
 *   step [N]                    N main-loop passes (20 ms of the virtual clock each)
 *   click CONTROL [right]       the capture tour's click at the centre of the layout table's control
 *                               "<screen>.<NN>": move, 2 passes, button down,
 *                               2 passes, button up, 1 pass
 *   move X Y | ldown | lup | rdown | rup
 *   key NAME [shift] [alt] [ctrl] | type TEXT
 *   focus 0|1 | close
 *   state                       the flow and presenter state into the report
 *   expect FIELD=VALUE ...      fields of `state`; a mismatch is a report error
 *   snap LABEL                  SHA-256 of the frame indices and the colour table
 *   clock MS                    the virtual clock's step per pass from now on (0: 20 ms)
 *   trace 1|0                   each present's frame SHA-256 into the report
 * The script's end is the window's close (the loop end saves slot 10 when a
 * game is loaded, as the text menu's end of input does).
 * Contract: docs/reverse-engineering/launcher.md, "Graphical shell". */
#define SDL_MAIN_HANDLED
#include "gui/app.h"
#include "gui/app_sound.h"
#include "host.h"
#include "es2/shell/gui/headless.h"
#include "es2/shell/gui/window.h"

#include <SDL3/SDL.h>
#include <windows.h>

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_text(const char *path) {
    size_t size;
    return (char *)es2_host_read(path, &size);   /* NUL-terminated */
}

/* ---- The test pattern without --menu ------------------------------------------------ */
static int pattern_headless(int argc, char **argv, const es2_gui_args *args, const char *script_path,
                            const char *report_path) {
    char *script = script_path ? read_text(script_path) : NULL;
    if (script_path && !script) return 3;
    es2_gui_headless_result result;
    int status = es2_gui_headless_run(script ? script : "pattern\nstep\n", args, NULL, &result);
    free(script);
    if (status) return 3;
    if (report_path) {
        FILE *f = fopen(report_path, "wb");
        if (!f) { free(result.report); return 3; }
        fputs("argv", f);
        for (int i = 1; i < argc; i++) fprintf(f, " %s", argv[i]);
        fprintf(f, "\nargs entry=%d language=%d eggplant=%d movies=%d sound=%d keyboard=%d cheat=%d\n",
                args->entry_mode, args->language, args->eggplant, args->movies, args->sound, args->keyboard,
                args->cheat);
        fwrite(result.report, 1, result.length, f);
        fclose(f);
    }
    free(result.report);
    return result.errors ? 4 : 0;
}

static int pattern_window(const es2_gui_args *args, int fullscreen) {
    char error[256];
    es2_gui_window *window = es2_gui_window_open(fullscreen, error, sizeof error);
    if (!window) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "EarthSiege II", error, NULL);
        return 0; /* VSHELL ends with 0 when its window cannot be created */
    }
    es2_gui_shell *shell = calloc(1, sizeof *shell);
    if (!shell) { es2_gui_window_close(window); return 1; }
    es2_gui_platform platform = es2_gui_window_platform(window);
    es2_gui_shell_init(shell, args, &platform, NULL);
    shell->fullscreen = fullscreen;
    es2_gui_shell_clear(shell);  /* WinMain step 8 (0x4077ee) */
    es2_gui_test_pattern(shell); /* stand-in for the first screen */
    shell->input.interactive = 1; /* 0x46c28c once the first screen is up */
    while (es2_gui_step(shell)) SDL_WaitEventTimeout(NULL, 1); /* own: no busy spin (a deviation) */
    free(shell);
    es2_gui_window_close(window);
    return 0;
}

/* ---- The headless platform of the shell ---------------------------------------------- */
typedef struct message {
    int kind;      /* 0 mouse, 1 key, 2 focus, 3 close */
    es2_gui_mouse_message mouse;
    int32_t x, y;
    unsigned vk;
    int up, shift, alt, ctrl, gained;
} message;

typedef struct headless {
    message pending[256];
    int count;
    int held_left, held_right;
    int32_t x, y;
    uint32_t now;
    uint32_t pass_ms;     /* the virtual clock's step per pump pass (`clock`; 0 = 20 ms) */
    int trace;            /* `trace 1`: every present's frame hash into the report */
    unsigned presents;
    FILE *report;
    int errors;
} headless;

static void queue(headless *h, message m) {
    if (h->count < (int)(sizeof h->pending / sizeof h->pending[0])) h->pending[h->count++] = m;
    else h->errors++;
}
static int headless_pump(void *context, es2_gui_shell *shell) {
    headless *h = context;
    for (int i = 0; i < h->count; i++) {
        message *m = &h->pending[i];
        if (m->kind == 0) es2_gui_on_mouse(shell, m->mouse, m->x, m->y, h->held_left, h->held_right);
        else if (m->kind == 1) es2_gui_on_key(shell, m->vk, m->up, m->shift, m->alt, m->ctrl);
        else if (m->kind == 2) es2_gui_on_focus(shell, m->gained);
        else es2_gui_on_close(shell);
    }
    h->count = 0;
    h->now += h->pass_ms ? h->pass_ms : 20;
    return 0;
}
static void headless_present(void *context, es2_gui_shell *shell, es2_gui_present_kind kind, const es2_gui_rect *rects,
                             int count) {
    headless *h = context;
    h->presents++;
    if (h->trace && h->report) {   /* the frame after each present, as the capture tour records it */
        char frame[65];
        es2_gui_rect r = count > 0 && rects ? rects[0] : (es2_gui_rect){0, 0, ES2_GUI_WIDTH, ES2_GUI_HEIGHT};
        es2_gui_hash_frame(&shell->frame, frame);
        fprintf(h->report, "present %u kind=%d rect=%d,%d,%d,%d tick=%u frame=%s\n", h->presents, (int)kind,
                (int)r.left, (int)r.top, (int)r.right, (int)r.bottom, (unsigned)(h->now / 16u), frame);
    }
}
static void headless_cursor(void *context, int busy) { (void)context; (void)busy; }
static void headless_fullscreen(void *context, int on) { (void)context; (void)on; }
static uint32_t headless_clock(void *context) { return ((headless *)context)->now; }

static void mouse(headless *h, es2_gui_mouse_message msg, int32_t x, int32_t y) {
    message m;
    memset(&m, 0, sizeof m);
    m.kind = 0;
    m.mouse = msg;
    m.x = x;
    m.y = y;
    if (msg == ES2_GUI_WM_LBUTTONDOWN) h->held_left = 1;
    if (msg == ES2_GUI_WM_LBUTTONUP) h->held_left = 0;
    if (msg == ES2_GUI_WM_RBUTTONDOWN) h->held_right = 1;
    if (msg == ES2_GUI_WM_RBUTTONUP) h->held_right = 0;
    h->x = x;
    h->y = y;
    queue(h, m);
}
static void key(headless *h, unsigned vk, int up, int shift, int alt, int ctrl) {
    message m;
    memset(&m, 0, sizeof m);
    m.kind = 1;
    m.vk = vk;
    m.up = up;
    m.shift = shift;
    m.alt = alt;
    m.ctrl = ctrl;
    queue(h, m);
}
static int steps(es2_gui_shell *shell, int n) {
    for (int i = 0; i < n; i++)
        if (!es2_gui_step(shell)) return 0;
    return 1;
}
/* The key and Shift state that give character c in VSHELL's tables. */
static int key_for(unsigned c, unsigned *vk, int *shift) {
    for (int sh = 0; sh < 2; sh++)
        for (unsigned v = 1; v < 256; v++) {
            unsigned index = es2_gui_key_index(v);
            if (index && es2_gui_key_character(index | (sh ? ES2_GUI_KEY_SHIFT : 0)) == c) {
                *vk = v;
                *shift = sh;
                return 1;
            }
        }
    return 0;
}
static void error(headless *h, int line, const char *format, ...) {
    va_list args;
    h->errors++;
    fprintf(h->report, "error %d ", line);
    va_start(args, format);
    vfprintf(h->report, format, args);
    va_end(args);
    fputc('\n', h->report);
}

/* Runs the script; returns 0 when the loop ended (quit), 1 when the script ran out. */
static int run_script(headless *h, es2_gui_shell *shell, es2_app *app, char *script) {
    int line = 0;
    for (char *next, *at = script; at && *at; at = next) {
        char verb[32], rest[512], *args;
        next = strchr(at, '\n');
        if (next) *next++ = 0;
        line++;
        size_t n = strlen(at);
        while (n && (at[n - 1] == '\r' || at[n - 1] == ' ')) at[--n] = 0;
        while (*at == ' ') at++;
        if (!*at || *at == '#') continue;
        if (sscanf(at, "%31s", verb) != 1) continue;
        args = at + strlen(verb);
        while (*args == ' ') args++;
        snprintf(rest, sizeof rest, "%s", args);
        int alive = 1;
        if (!strcmp(verb, "step")) {
            int count = atoi(rest);
            alive = steps(shell, count > 0 ? count : 1);
        } else if (!strcmp(verb, "click")) {
            char name[64], side[16] = "";
            es2_ui_widget *w;
            if (sscanf(rest, "%63s %15s", name, side) < 1 || !(w = es2_ui_get(es2_app_ui(app), es2_app_control(app, name)))) {
                error(h, line, "no control %s", rest);
                continue;
            }
            int32_t x = (w->abs.left + w->abs.right) / 2, y = (w->abs.top + w->abs.bottom) / 2;
            int right = !strcmp(side, "right");
            mouse(h, ES2_GUI_WM_MOUSEMOVE, 639, 479);
            mouse(h, ES2_GUI_WM_MOUSEMOVE, 0, 0);
            mouse(h, ES2_GUI_WM_MOUSEMOVE, x, y);
            alive = steps(shell, 2);
            if (alive) {
                mouse(h, right ? ES2_GUI_WM_RBUTTONDOWN : ES2_GUI_WM_LBUTTONDOWN, x, y);
                alive = steps(shell, 2);
            }
            if (alive) {
                mouse(h, right ? ES2_GUI_WM_RBUTTONUP : ES2_GUI_WM_LBUTTONUP, x, y);
                alive = steps(shell, 1);
            }
        } else if (!strcmp(verb, "move")) {
            int x, y;
            if (sscanf(rest, "%d %d", &x, &y) != 2) { error(h, line, "move X Y"); continue; }
            mouse(h, ES2_GUI_WM_MOUSEMOVE, x, y);
        } else if (!strcmp(verb, "ldown")) mouse(h, ES2_GUI_WM_LBUTTONDOWN, h->x, h->y);
        else if (!strcmp(verb, "lup")) mouse(h, ES2_GUI_WM_LBUTTONUP, h->x, h->y);
        else if (!strcmp(verb, "rdown")) mouse(h, ES2_GUI_WM_RBUTTONDOWN, h->x, h->y);
        else if (!strcmp(verb, "rup")) mouse(h, ES2_GUI_WM_RBUTTONUP, h->x, h->y);
        else if (!strcmp(verb, "key")) {
            char name[32];
            unsigned vk;
            if (sscanf(rest, "%31s", name) != 1 || !(vk = es2_gui_key_by_name(name))) { error(h, line, "key %s", rest); continue; }
            int shift = strstr(rest, " shift") != NULL, alt = strstr(rest, " alt") != NULL, ctrl = strstr(rest, " ctrl") != NULL;
            key(h, vk, 0, shift, alt, ctrl);
            alive = steps(shell, 2);
            key(h, vk, 1, shift, alt, ctrl);
            if (alive) alive = steps(shell, 1);
        } else if (!strcmp(verb, "type")) {
            for (const char *c = args; alive && *c; c++) {
                unsigned vk;
                int shift;
                if (!key_for((unsigned char)*c, &vk, &shift)) { error(h, line, "no key for '%c'", *c); break; }
                key(h, vk, 0, shift, 0, 0);
                alive = steps(shell, 2);
                key(h, vk, 1, shift, 0, 0);
                if (alive) alive = steps(shell, 1);
            }
        } else if (!strcmp(verb, "clock")) {
            h->pass_ms = (uint32_t)strtoul(rest, NULL, 10);
        } else if (!strcmp(verb, "trace")) {
            h->trace = atoi(rest) != 0;
        } else if (!strcmp(verb, "focus")) {
            message m;
            memset(&m, 0, sizeof m);
            m.kind = 2;
            m.gained = atoi(rest) != 0;
            queue(h, m);
        } else if (!strcmp(verb, "close")) {
            message m;
            memset(&m, 0, sizeof m);
            m.kind = 3;
            queue(h, m);
        } else if (!strcmp(verb, "dump")) {
            /* the frame as an 8-bit BMP with the uploaded colour table (for a
             * look in a scratch directory; never for the repository) */
            FILE *f = fopen(rest, "wb");
            uint8_t head[54 + 1024];
            uint32_t size = (uint32_t)(sizeof head + ES2_GUI_PIXELS);
            if (!f) { error(h, line, "cannot write %s", rest); continue; }
            memset(head, 0, sizeof head);
            head[0] = 'B';
            head[1] = 'M';
            for (int b = 0; b < 4; b++) {
                head[2 + b] = (uint8_t)(size >> (8 * b));
                head[10 + b] = (uint8_t)((uint32_t)sizeof head >> (8 * b));
                head[18 + b] = (uint8_t)((uint32_t)ES2_GUI_WIDTH >> (8 * b));
                head[22 + b] = (uint8_t)((uint32_t)-ES2_GUI_HEIGHT >> (8 * b));
            }
            head[14] = 40;
            head[26] = 1;
            head[28] = 8;
            for (int k = 0; k < 256; k++) {
                head[54 + 4 * k] = shell->frame.table[k][2];
                head[55 + 4 * k] = shell->frame.table[k][1];
                head[56 + 4 * k] = shell->frame.table[k][0];
            }
            fwrite(head, 1, sizeof head, f);
            fwrite(shell->frame.pixels, 1, ES2_GUI_PIXELS, f);
            fclose(f);
        } else if (!strcmp(verb, "state")) {
            char d[256];
            es2_app_describe(app, d, sizeof d);
            fprintf(h->report, "state %d %s\n", line, d);
        } else if (!strcmp(verb, "expect")) {
            char d[256], *field, *save = NULL;
            es2_app_describe(app, d, sizeof d);
            for (field = strtok_r(rest, " ", &save); field; field = strtok_r(NULL, " ", &save)) {
                char want[64];
                snprintf(want, sizeof want, " %s ", field);
                char padded[260];
                snprintf(padded, sizeof padded, " %s ", d);
                if (!strstr(padded, want)) error(h, line, "expected %s, state %s", field, d);
            }
        } else if (!strcmp(verb, "snap")) {
            char frame[65], table[65];
            es2_gui_hash_frame(&shell->frame, frame);
            es2_gui_hash_table(&shell->frame, table);
            fprintf(h->report, "snap %s frame=%s table=%s\n", rest, frame, table);
        } else {
            error(h, line, "unknown command %s", verb);
        }
        fflush(h->report);
        if (!alive) return 0;
    }
    return 1;
}

/* ---- The shell ------------------------------------------------------------------------- */
typedef struct process {
    es2_host host;
    FILE *log;
    int window;          /* message boxes allowed */
} process;

static void say(void *ctx, const char *data, size_t size) {
    process *p = ctx;
    if (p->log) {
        fwrite(data, 1, size, p->log);
        fflush(p->log);
    }
}
/* VSHELL's severity-4 assertion box (0x44e5b0). */
static void assertion(void *ctx, const char *text) {
    process *p = ctx;
    if (p->window) MessageBoxA(NULL, text, "EarthSiege II", MB_ICONSTOP);
}
static uint32_t sdl_clock(void *context) { (void)context; return (uint32_t)SDL_GetTicks(); }

static int run_shell(char **vshell, int count, const es2_gui_args *args, const char *menu_dir, int headless_mode,
                     const char *script_path, const char *report_path, int fullscreen) {
    static process p;
    es2_host *h = &p.host;
    char cwd[MAX_PATH], log_path[MAX_PATH + 16], message_text[256];
    int code = 0;
    p.window = !headless_mode;
    h->ui.ctx = &p;
    h->ui.say = say;
    h->ui.assertion = assertion;
    /* the text menu's configuration: --menu DIR and the VSHELL argv */
    h->args.menu_dir = menu_dir;
    h->args.state = args->entry_mode;
    h->args.no_movies = !args->movies;
    /* The language word 0x48235e is prefs byte +0x2b (0x4079b7; WinMain
     * loaded prefs.cfg at 0x406b28 before) unless -f, -g or -e... set it */
    {
        int language = args->language;
        if (language < 0) {
            size_t size;
            uint8_t prefs[ES2_SHELL_PREFS_SIZE], *file = es2_host_read("DATA\\PREFS.CFG", &size);
            es2_span span = {file, size};
            es2_shell_prefs_load(file ? &span : NULL, prefs);
            free(file);
            language = prefs[0x2b];
        }
        snprintf(h->args.language, sizeof h->args.language, "%s", es2_host_language_dir(language));
    }
    if (!es2_host_config(h)) {
        snprintf(message_text, sizeof message_text, "%s\\menu.ini is missing or incomplete", menu_dir);
        if (p.window) MessageBoxA(NULL, message_text, "EarthSiege II", MB_ICONSTOP);
        return 0;
    }
    snprintf(log_path, sizeof log_path, "%s\\shell.log", h->dir);
    p.log = fopen(log_path, "ab");
    GetCurrentDirectoryA(sizeof cwd, cwd);
    if (p.log) {
        fprintf(p.log, "\n[start");
        for (int i = 1; i < count; i++) fprintf(p.log, " %s", vshell[i]);
        fprintf(p.log, " in %s]\n", cwd);
        fprintf(p.log, "language %s\n", h->args.language[0] ? h->args.language : "(none)");
    }
    if (!es2_host_check_directory(h, cwd) || !es2_host_cd_check(h)) {   /* 0x401cac */
        if (p.window) MessageBoxA(NULL, "The CD check or the game directory failed (shell.log).", "EarthSiege II", MB_ICONSTOP);
        goto end;
    }
    es2_host_labels_open(h);
    es2_host_start(h, NULL);
    {
        es2_gui_shell *shell = calloc(1, sizeof *shell);
        es2_gui_window *window = NULL;
        static headless hl;
        es2_gui_platform platform;
        es2_app_platform ap;
        es2_app *app;
        int display = fullscreen || h->flow.prefs.current[6] == 1;   /* preference byte 6 */
        if (!shell) goto end;
        memset(&ap, 0, sizeof ap);
        if (headless_mode) {
            platform = (es2_gui_platform){&hl, headless_pump, headless_present, headless_cursor, headless_fullscreen};
            ap.headless = 1;
            ap.clock = headless_clock;
            ap.clock_context = &hl;
            ap.audio_device = es2_app_sound_null_device;   /* samples found and plays logged, no output */
        } else {
            char error_text[256];
            window = es2_gui_window_open(display, error_text, sizeof error_text);
            if (!window) {
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "EarthSiege II", error_text, NULL);
                free(shell);
                goto end;   /* VSHELL ends with 0 when its window cannot be created */
            }
            platform = es2_gui_window_platform(window);
            {   /* VSHELL's class icon "es2icon" (0x40687c), platform
                 * deviation 9; from the span store */
                if (!es2_gui_window_icon(window, error_text, sizeof error_text) && p.log)
                    fprintf(p.log, "no window icon: %s\n", error_text);
            }
            ap.native_window = es2_gui_window_native(window);
            ap.audio_device = es2_app_sound_sdl_device;
            ap.clock = sdl_clock;
        }
        ap.log = p.log;
        ap.sound = args->sound;
        ap.cheat = args->cheat;
        es2_gui_shell_init(shell, args, &platform, NULL);
        shell->fullscreen = display;
        es2_gui_shell_clear(shell);   /* WinMain step 8 (0x4077ee) */
        app = es2_app_create(h, shell, &ap);
        if (app && es2_app_build(app) == 0) {
            es2_app_enter(app, h->args.state);
            if (headless_mode) {
                char *script = script_path ? read_text(script_path) : NULL;
                hl.report = report_path ? fopen(report_path, "wb") : NULL;
                if (!hl.report) hl.report = fopen("NUL", "wb");
                fputs("argv", hl.report);
                for (int i = 1; i < count; i++) fprintf(hl.report, " %s", vshell[i]);
                fputc('\n', hl.report);
                if (!script) error(&hl, 0, "no script");
                else if (!h->flow.quit && !shell->quit) run_script(&hl, shell, app, script);
                free(script);
            } else {
                while (es2_gui_step(shell)) SDL_WaitEventTimeout(NULL, 1);   /* own: no busy spin */
            }
            code = es2_app_finish(app);
            if (hl.report) {
                char d[256];
                es2_app_describe(app, d, sizeof d);
                fprintf(hl.report, "end passes=%u presents=%u errors=%d exit=%d %s\n", (unsigned)shell->iterations,
                        hl.presents, hl.errors, code, d);
                fclose(hl.report);
                if (hl.errors && !code) code = 0;
            }
        } else if (p.window) {
            MessageBoxA(NULL, "The shell could not be built (shell.log).", "EarthSiege II", MB_ICONSTOP);
        }
        es2_app_destroy(app);
        if (!headless_mode) es2_app_sound_sdl_shutdown();
        if (window) es2_gui_window_close(window);
        free(shell);
    }
end:
    if (p.log) {
        fprintf(p.log, "[exit %d]\n", code);
        fclose(p.log);
    }
    es2_host_close(h);
    return code;
}

static const char *per_entry_path(const char *path, int entry_mode, char *buffer, size_t size);

int main(int argc, char **argv) {
    int headless_mode = 0, fullscreen = 0, count = 0;
    const char *script = NULL, *report = NULL, *menu = NULL;
    char **vshell = calloc((size_t)argc + 1, sizeof *vshell);
    if (!vshell) return 1;
    vshell[count++] = "dbsim";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--headless")) headless_mode = 1;
        else if (!strcmp(argv[i], "--fullscreen")) fullscreen = 1;
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) script = argv[++i];
        else if (!strcmp(argv[i], "--report") && i + 1 < argc) report = argv[++i];
        else if (!strcmp(argv[i], "--menu") && i + 1 < argc) menu = argv[++i];
        else vshell[count++] = argv[i];
    }
    es2_gui_args args;
    es2_gui_parse_args(count, vshell, &args);
    char script_buf[1024], report_buf[1024];   /* {X} in --script/--report = the -X entry mode */
    script = per_entry_path(script, args.entry_mode, script_buf, sizeof script_buf);
    report = per_entry_path(report, args.entry_mode, report_buf, sizeof report_buf);
    int code;
    if (menu) code = run_shell(vshell, count, &args, menu, headless_mode, script, report, fullscreen);
    else code = headless_mode ? pattern_headless(count, vshell, &args, script, report) : pattern_window(&args, fullscreen);
    free(vshell);
    return code;
}

/* The unattended chain (start-own-menu.ps1 -Unattended -Shell gui) gives one ES2_LAUNCH_SHELL for every
 * start of the run; "{X}" in --script and --report becomes the -X entry mode of this start (1 at start-up, 3
 * after DBSIM's return), so each start reads its own script and writes its own report. Paths without "{X}" and
 * paths that would not fit stay as given. */
static const char *per_entry_path(const char *path, int entry_mode, char *buffer, size_t size) {
    const char *at = path ? strstr(path, "{X}") : NULL;
    if (!at) return path;
    int n = snprintf(buffer, size, "%.*s%d%s", (int)(at - path), path, entry_mode, at + 3);
    return (n > 0 && (size_t)n < size) ? buffer : path;
}
