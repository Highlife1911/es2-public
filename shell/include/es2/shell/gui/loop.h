#ifndef ES2_SHELL_GUI_LOOP_H
#define ES2_SHELL_GUI_LOOP_H

/* The own shell's process frame: VSHELL's command line, the window
 * procedure's message branches and the main loop (contract:
 * vshell_process.md sections 3, 7 and 8; own choices: vshell_process.md,
 * "the own shell process frame").
 * Portable: the platform (window.c with SDL3, or headless.c) delivers the
 * messages through the es2_gui_on_* functions inside its pump and shows the
 * screen when es2_gui_platform.present is called. */
#include "es2/shell/gui/events.h"
#include "es2/shell/gui/frame.h"

/* VSHELL's options (0x4015d8, defaults 0x40796c(0), entry mode 0x401bc1). */
typedef struct es2_gui_args {
    int movies;      /* 0x482359: -a clears */
    int cheat;       /* 0x482368: -@ */
    int language;    /* 0x48235e: -1 = the preference byte +0x2b; -f 1, -g 2, -e... 3 */
    int eggplant;    /* 0x46c278: the exact token -eggplant */
    int keyboard;    /* 0x482355: -k clears */
    int sound;       /* 0x482356: -s, -v, -? clear */
    int m_flag;      /* 0x482354: -m (no effect in VSHELL) */
    int l_word;      /* 0x482364: -l clears (no effect in VSHELL) */
    int version;     /* -v seen (VSHELL prints to no console) */
    int usage;       /* -? seen */
    int entry_mode;  /* 0x482362 := the -X value; -r's 3 is overwritten */
} es2_gui_args;

/* argv[0] is skipped (VSHELL's own argv[0] is the literal "dbsim"). */
void es2_gui_parse_args(int argc, char *const *argv, es2_gui_args *out);

typedef enum es2_gui_present_kind { ES2_GUI_PRESENT_DIRTY, ES2_GUI_PRESENT_FULL, ES2_GUI_PRESENT_CLEAR } es2_gui_present_kind;

struct es2_gui_shell;
typedef struct es2_gui_platform {
    void *context;
    /* Deliver the pending window messages (es2_gui_on_*); return 1 on WM_QUIT. */
    int (*pump)(void *context, struct es2_gui_shell *shell);
    /* The screen changed (rects = the presented rectangles of a dirty flush). */
    void (*present)(void *context, struct es2_gui_shell *shell, es2_gui_present_kind kind, const es2_gui_rect *rects,
                    int count);
    void (*cursor)(void *context, int busy);
    void (*fullscreen)(void *context, int on);
} es2_gui_platform;

/* What the screens and the other packages plug in (all optional). */
typedef struct es2_gui_hooks {
    void *context;
    es2_gui_handler handle;                                    /* every drained event */
    void (*timer)(void *context, struct es2_gui_shell *shell); /* 0x46a508 before the drain */
    void (*movie)(void *context, struct es2_gui_shell *shell); /* 0x41e98c(1) */
    void (*map)(void *context, struct es2_gui_shell *shell);   /* 0x4019c6 */
    /* Quit box, prefs byte 6 store/write, sound focus: the shell's part of a system action. */
    void (*system)(void *context, struct es2_gui_shell *shell, es2_gui_system_action action);
    void (*focus)(void *context, struct es2_gui_shell *shell, int gained); /* sound focus (audio.md, "the VSHELL shell audio contract") */
} es2_gui_hooks;

typedef struct es2_gui_shell {
    es2_gui_frame frame;
    es2_gui_screen screen;
    es2_gui_input input;
    int focused;        /* 0x46c288 (1 at start) */
    int window_ready;   /* 0x46d87c */
    int quit;           /* 0x46c268 */
    int fullscreen;     /* 0x481f4c, from preference byte 6 at start */
    int busy;           /* the cursor shape last set by 0x408d2f */
    uint32_t iterations, presents;
    es2_gui_platform platform;
    es2_gui_hooks hooks;
} es2_gui_shell;

/* Fresh shell state (focused, not interactive, gate 0, keyboard per args). */
void es2_gui_shell_init(es2_gui_shell *shell, const es2_gui_args *args, const es2_gui_platform *platform,
                        const es2_gui_hooks *hooks);

/* Window procedure branches (0x404fb8). */
void es2_gui_on_focus(es2_gui_shell *shell, int gained);  /* WM_SETFOCUS / WM_KILLFOCUS */
void es2_gui_on_paint(es2_gui_shell *shell);              /* WM_PAINT */
void es2_gui_on_close(es2_gui_shell *shell);              /* WM_CLOSE */
void es2_gui_on_mouse(es2_gui_shell *shell, es2_gui_mouse_message msg, int32_t x, int32_t y, int mk_left, int mk_right);
void es2_gui_on_key(es2_gui_shell *shell, unsigned vk, int up, int shift, int alt, int ctrl);

/* One main-loop pass from its pump (0x402181 -> 0x402018..0x4020d5):
 * pump; unless focused nothing else; input poll, timer and event drain,
 * palette apply, dirty flush, movie step, map step, each followed by the
 * quit-flag test. Returns 0 when the loop ends (WM_QUIT or quit flag). */
int es2_gui_step(es2_gui_shell *shell);

/* 0x4077ee and 0x406e87 with their present calls. */
void es2_gui_shell_clear(es2_gui_shell *shell);
void es2_gui_shell_present_full(es2_gui_shell *shell);
/* 0x408d2f: busy != 0 -> wait cursor, else arrow. */
void es2_gui_set_busy(es2_gui_shell *shell, int busy);

/* A synthetic picture for the shell without screens: a 32 x 32 grid of
 * 20 x 15 cells in all 256 indices (index = row * 16 + column, both mod 16)
 * with an own palette, the realization gate set and the frame invalidated.
 * No game data. */
void es2_gui_test_pattern(es2_gui_shell *shell);

#endif
