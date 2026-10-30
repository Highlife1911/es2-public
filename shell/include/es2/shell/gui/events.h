#ifndef ES2_SHELL_GUI_EVENTS_H
#define ES2_SHELL_GUI_EVENTS_H

/* VSHELL's input path and event queue (contracts: vshell_widgets.md
 * sections 3 and 7; vshell_process.md section 7; own choices in
 * vshell_widgets.md, "input and event queue of the own shell"). Portable C17,
 * no platform call.
 *
 * The platform layer (window.c, headless.c) feeds window messages in the
 * form VSHELL's window procedure 0x404fb8 receives them: mouse messages with
 * the position already in frame coordinates and the MK_ button bits, key
 * messages with the Windows virtual-key code and the three modifier states.
 * From there everything is VSHELL's: the button word and the mouse producer
 * ring (0x405bfc, 0x4090b3), the key table, system keys, hot-key queue and
 * key ring (0x4051fa), the device poll that turns them into events (0x4091fe,
 * 0x409544), posting (0x468780) and the drain (0x469ee4). What the handlers
 * see are es2_gui_event values. Hit test, hover, capture and dispatch to
 * windows are the widget toolkit's: the drain hands every event to
 * one callback in queue order. */
#include <stddef.h>
#include <stdint.h>

/* Event types (vshell_widgets.md 3.2). */
enum {
    ES2_GUI_EV_SHOWN = 0x01,
    ES2_GUI_EV_HIDDEN = 0x02,
    ES2_GUI_EV_REDRAW = 0x04,
    ES2_GUI_EV_ENTER = 0x08,
    ES2_GUI_EV_LEAVE = 0x10,
    ES2_GUI_EV_MOUSE = 0x20,
    ES2_GUI_EV_KEY = 0x40,
    ES2_GUI_EV_SCROLL = 0x80,
    ES2_GUI_EV_COMMAND = 0x100,
    ES2_GUI_EV_TIMER = 0x200,
};

/* Mouse codes (+0x24 of a WinMouseEvent). */
enum {
    ES2_GUI_MOUSE_MOVE = 0,
    ES2_GUI_MOUSE_LEFT_UP = 1,
    ES2_GUI_MOUSE_LEFT_DOWN = 2,
    ES2_GUI_MOUSE_RIGHT_UP = 3,
    ES2_GUI_MOUSE_RIGHT_DOWN = 4,
};

/* Key word bits (0x4051fa). */
#define ES2_GUI_KEY_UP 0x80u
#define ES2_GUI_KEY_ALT 0x200u
#define ES2_GUI_KEY_CTRL 0x400u
#define ES2_GUI_KEY_SHIFT 0x800u
#define ES2_GUI_KEY_COUNT 84 /* entries of the key table 0x46d4bc */

typedef struct es2_gui_event {
    uint32_t type;      /* +0x0c */
    int32_t x, y;       /* +0x10, +0x14: the pointer when posted (frame coordinates) */
    uint16_t code;      /* +0x24: mouse code, command code or character word */
    uint8_t synthetic;  /* +0x25: 1 for the posted clicks of 0x413305 */
    uint32_t target;    /* +0x20: 0 = none (goes to the hover window); else the poster's window id */
} es2_gui_event;

#define ES2_GUI_QUEUE_MAX 8192 /* VSHELL's queue grows (0x41da2d); one drain posts up to ~800 */
#define ES2_GUI_MOUSE_RING 100
#define ES2_GUI_KEY_RING 256
#define ES2_GUI_HOT_QUEUE 10
/* The command byte of a hot-key word with Shift, Ctrl or Alt lies past
 * the 256-byte table 0x46e5a9, in unrelated data of the executable; the
 * largest word (Shift+Ctrl+Alt, key-up, index 83) is 0xed3. */
#define ES2_GUI_HOT_BYTES_BASE 0x46e5a9u
#define ES2_GUI_HOT_BYTES_SIZE 0xed4u

/* Window messages of the mouse (0x200..0x205). */
typedef enum es2_gui_mouse_message {
    ES2_GUI_WM_MOUSEMOVE,
    ES2_GUI_WM_LBUTTONDOWN,
    ES2_GUI_WM_LBUTTONUP,
    ES2_GUI_WM_RBUTTONDOWN,
    ES2_GUI_WM_RBUTTONUP,
} es2_gui_mouse_message;

/* What a key message asks the shell to do besides queueing input. */
typedef enum es2_gui_system_action {
    ES2_GUI_SYS_NONE,
    ES2_GUI_SYS_QUIT_BOX,          /* Alt+F4 (0x23e): release the capture, show the Quit box */
    ES2_GUI_SYS_LEAVE_FULLSCREEN,  /* Alt+Tab, Ctrl+Esc, Alt+Esc: leave full screen, prefs byte 6 := 0, write */
    ES2_GUI_SYS_TOGGLE_FULLSCREEN, /* Alt+Enter on key-up: toggle, prefs byte 6 := new state, write */
} es2_gui_system_action;

typedef struct es2_gui_mouse_record { int32_t dx, dy; uint8_t code; } es2_gui_mouse_record;

typedef struct es2_gui_input {
    es2_gui_event queue[ES2_GUI_QUEUE_MAX];
    unsigned queue_head, queue_count;
    uint32_t queue_overflows;
    int32_t cursor_x, cursor_y;          /* the system pointer in frame coordinates */
    /* mouse producer (0x408e08) */
    es2_gui_mouse_record mouse[ES2_GUI_MOUSE_RING];
    uint16_t mouse_read, mouse_write;    /* +0x570, +0x572 */
    uint16_t buttons;                    /* +0x5a: last button word */
    /* keyboard */
    uint16_t ring[ES2_GUI_KEY_RING];     /* 0x4814a0 */
    unsigned ring_write, ring_read;      /* 0x4816a0, 0x4816a4 */
    uint16_t hot[ES2_GUI_HOT_QUEUE];     /* keyboard object +0x14 */
    uint16_t hot_read, hot_write;        /* +0x50, +0x52 */
    /* The bytes at 0x46e5a9.. as the host read them from VSHELL.EXE (not
     * committed, never compiled in); loaded = 0: words with modifiers give no event */
    uint8_t hot_bytes[ES2_GUI_HOT_BYTES_SIZE];
    int hot_bytes_loaded;
    int keyboard;                        /* 0x482355 (-k clears it): the keyboard device exists */
    /* flags the window procedure reads or sets */
    int interactive;                     /* 0x46c28c */
    int movie_open;                      /* 0x470eb0 */
    int intro_playing, skip_intro;       /* 0x471124, 0x471120 */
    int movie_skip;                      /* 0x470ea0 */
    int map_animation, map_skip;         /* 0x46c26c, 0x46c270 */
} es2_gui_input;

/* Empty state with the keyboard device present. */
void es2_gui_input_init(es2_gui_input *in);

/* The window procedure's mouse branches (0x4050bd..0x4051f5) and 0x405bfc:
 * x, y are the logical (frame) position the platform computed from the
 * client position; mk_left/mk_right the MK_LBUTTON/MK_RBUTTON bits of
 * wParam. Also moves the system pointer to (x, y). */
void es2_gui_mouse_message_in(es2_gui_input *in, es2_gui_mouse_message msg, int32_t x, int32_t y, int mk_left,
                              int mk_right);

/* The key branch 0x4051fa..0x40543e: vk = the Windows virtual-key code, up
 * for WM_KEYUP/WM_SYSKEYUP, the three modifier states as GetKeyState reports
 * them while the message is handled. Returns the system action to perform. */
es2_gui_system_action es2_gui_key_message_in(es2_gui_input *in, unsigned vk, int up, int shift, int alt, int ctrl);

/* Key index of a virtual-key code (first match in 0x46d4bc), 0 = not in the table. */
unsigned es2_gui_key_index(unsigned vk);
/* Virtual-key code of a key index (the table 0x46d4bc; 0 for unmapped indices). */
unsigned es2_gui_key_vk(unsigned index);
/* The hot-key command byte (0x46e5a9) of a key word below 0x100, 0xff = none. */
unsigned es2_gui_hot_command(unsigned word);
/* Hand the input the executable's bytes at ES2_GUI_HOT_BYTES_BASE
 * (ES2_GUI_HOT_BYTES_SIZE of them, VSHELL.EXE's bytes read from the span store
 * at run time). Returns 1 when taken, 0 when n is too short (nothing changes). */
int es2_gui_input_set_hot_bytes(es2_gui_input *in, const uint8_t *bytes, size_t n);
/* The command byte VSHELL reads for a key word (0x409587, 0x409605), 0xff = no
 * event: words below 0x100 from the built-in table, words with modifier bits
 * from the executable's bytes when set, else 0xff. */
unsigned es2_gui_input_hot_command(const es2_gui_input *in, unsigned word);
/* The character of a ring word (tables 0x46e6a9 plain, 0x46e6fd Shift), 0 = none. */
unsigned es2_gui_key_character(unsigned word);

/* Device poll 0x41e0bf: the mouse producer's records (0x4091fe), then the
 * keyboard's hot-key commands and ring characters (0x409544). */
void es2_gui_poll(es2_gui_input *in);

/* 0x468780: a mouse move whose motion is (0, 0) is dropped; every other
 * event gets the system pointer as its position and is appended. Returns 1
 * when queued. */
int es2_gui_post(es2_gui_input *in, es2_gui_event ev);
/* 0x413305: left down then left up to target, marked synthetic. */
void es2_gui_post_synthetic_click(es2_gui_input *in, uint32_t target);

/* 0x469ee4: pops until the queue is empty; events a handler posts run in
 * the same drain. Returns the number of events handled. */
typedef void (*es2_gui_handler)(void *context, const es2_gui_event *ev);
unsigned es2_gui_drain(es2_gui_input *in, es2_gui_handler handler, void *context);

#endif
