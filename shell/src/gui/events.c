/* VSHELL's input path and event queue in the own shell.
 * Contracts: vshell_widgets.md sections 3 and 7 and "input and event queue
 * of the own shell", vshell_process.md section 7.
 * The tables below follow the Windows virtual-key set and the US
 * keyboard layout; //shell:gui_tables_real_test compares them with the
 * tables of the reference VSHELL.EXE. */
#include "es2/shell/gui/events.h"

#include <string.h>

/* Key index -> virtual-key code, in PC scan-code order (0x46d4bc). */
static const uint8_t key_vk[ES2_GUI_KEY_COUNT] = {
    0x00, 0x1b, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 0x00, 0x00, 0x08, 0x09,     /* 0..15 */
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', 0x00, 0x00, 0x0d, 0x11, 'A', 'S',     /* 16..31 */
    'D', 'F', 'G', 'H', 'J', 'K', 'L', 0x00, 0x00, 0x00, 0x00, 0x00, 'Z', 'X', 'C', 'V',    /* 32..47 */
    'B', 'N', 'M', 0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x20, 0x14, 0x70, 0x71, 0x72, 0x73, 0x74, /* 48..63 */
    0x75, 0x76, 0x77, 0x78, 0x79, 0x90, 0x91, 0x24, 0x26, 0x21, 0x6d, 0x25, 0x0c, 0x27, 0x6b, 0x23, /* 64..79 */
    0x28, 0x22, 0x2d, 0x2e,                                                                  /* 80..83 */
};

/* Key indices of the hot keys the keyboard object registers (0x46e588,
 * down forms; the list also holds each | 0x80): Tab, Backspace, Ins, Del,
 * PgUp, PgDn, Home, End, Esc, Enter, Up, Down, numpad 5, Left, Right, minus. */
static const uint8_t hot_keys[16] = {15, 14, 82, 83, 73, 81, 71, 79, 1, 28, 72, 80, 76, 75, 77, 12};

/* Characters by key index (0x46e6a9 plain, 0x46e6fd with Shift). */
static const char plain_chars[ES2_GUI_KEY_COUNT] = "\0\0" "1234567890-=" "\0\0" "QWERTYUIOP[]" "\n\0" "ASDFGHJKL;'"
                                  "\0\0\\" "ZXCVBNM,./" "\0\0\0 ";
static const char shift_chars[ES2_GUI_KEY_COUNT] = "\0\0" "!@#$%^&*()_+" "\0\0" "QWERTYUIOP\0\0" "\n\0" "ASDFGHJKL:\""
                                  "\0\0\0" "ZXCVBNM<>?" "\0\0\0 ";

/* Centre the mouse producer measures its motion from ([producer+0x4a],
 * [producer+0x4e] = half the screen object's width and height, 0x408f69;
 * hypothesis: 640 x 480, so (320, 240)). */
#define MOUSE_CENTRE_X 320
#define MOUSE_CENTRE_Y 240

void es2_gui_input_init(es2_gui_input *in) {
    memset(in, 0, sizeof *in);
    in->keyboard = 1;
}

unsigned es2_gui_key_vk(unsigned index) { return index < ES2_GUI_KEY_COUNT ? key_vk[index] : 0; }

/* Original: VSHELL.EXE 0x405215..0x405438 (first match, entry 0 never matches a real key) */
unsigned es2_gui_key_index(unsigned vk) {
    for (unsigned i = 0; i < ES2_GUI_KEY_COUNT; i++)
        if (key_vk[i] == vk && vk != 0) return i;
    return 0;
}

/* Original: VSHELL.EXE byte table 0x46e5a9 for words below 0x100 */
unsigned es2_gui_hot_command(unsigned word) {
    static const struct { uint8_t index, down, up; } map[] = {
        {1, 0x00, 0x13},  {14, 0x01, 0x14}, {82, 0x02, 0x15}, {83, 0x03, 0x16}, {75, 0x04, 0x17},
        {72, 0x05, 0x18}, {77, 0x06, 0x19}, {80, 0x07, 0x1a}, {73, 0x08, 0x1b}, {81, 0x09, 0x1c},
        {28, 0x0a, 0x1d}, {71, 0x0b, 0x1e}, {79, 0x0c, 0x1f}, {15, 0x12, 0x25},
    };
    if (word >= 0x100) return 0xff;
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
        if ((word & 0x7f) == map[i].index) return word & ES2_GUI_KEY_UP ? map[i].up : map[i].down;
    return 0xff;
}

/* Original: VSHELL.EXE 0x409587..0x409592 and 0x409602..0x40960b: the byte read is
 * 0x46e5a9[word] for the whole 16-bit word, modifier bits included. */
int es2_gui_input_set_hot_bytes(es2_gui_input *in, const uint8_t *bytes, size_t n) {
    if (!in || !bytes || n < ES2_GUI_HOT_BYTES_SIZE) return 0;
    memcpy(in->hot_bytes, bytes, ES2_GUI_HOT_BYTES_SIZE);
    in->hot_bytes_loaded = 1;
    return 1;
}

unsigned es2_gui_input_hot_command(const es2_gui_input *in, unsigned word) {
    if (word < 0x100) return es2_gui_hot_command(word);
    if (!in || !in->hot_bytes_loaded || word >= ES2_GUI_HOT_BYTES_SIZE) return 0xff;
    return in->hot_bytes[word];
}

/* Original: VSHELL.EXE 0x4096e6..0x409725 */
unsigned es2_gui_key_character(unsigned word) {
    unsigned index = word & 0x7f;
    if (index >= ES2_GUI_KEY_COUNT) return 0;
    unsigned c = (unsigned char)(word & ES2_GUI_KEY_SHIFT ? shift_chars[index] : plain_chars[index]);
    if (c >= 'a' && c <= 'z') c -= 'a' - 'A'; /* 0x46b9a0; both tables are upper case already */
    return c;
}

/* Original: VSHELL.EXE 0x408ffa (ring of 100, the oldest record dropped when full) */
static void mouse_record(es2_gui_input *in, int32_t dx, int32_t dy, uint8_t code) {
    es2_gui_mouse_record *r = &in->mouse[in->mouse_write];
    r->dx = dx;
    r->dy = dy;
    r->code = code;
    in->mouse_write = (uint16_t)(in->mouse_write + 1 == ES2_GUI_MOUSE_RING ? 0 : in->mouse_write + 1);
    if (in->mouse_write == in->mouse_read)
        in->mouse_read = (uint16_t)(in->mouse_read == ES2_GUI_MOUSE_RING - 1 ? 0 : in->mouse_read + 1);
}

/* Original: VSHELL.EXE 0x4090b3 (the producer's listener, mask 0x1f) */
static void mouse_producer(es2_gui_input *in, int32_t x, int32_t y, uint16_t buttons) {
    int32_t dx = x - MOUSE_CENTRE_X, dy = y - MOUSE_CENTRE_Y;
    uint16_t changed = (uint16_t)(in->buttons ^ buttons);
    if (!changed) {
        mouse_record(in, dx, dy, ES2_GUI_MOUSE_MOVE);
    } else {
        if (changed & 1) mouse_record(in, dx, dy, buttons & 1 ? ES2_GUI_MOUSE_LEFT_DOWN : ES2_GUI_MOUSE_LEFT_UP);
        if (changed & 2) mouse_record(in, dx, dy, buttons & 2 ? ES2_GUI_MOUSE_RIGHT_DOWN : ES2_GUI_MOUSE_RIGHT_UP);
    }
    in->buttons = buttons;
}

/* Original: VSHELL.EXE 0x4050bd..0x4051f5 and 0x405bfc */
void es2_gui_mouse_message_in(es2_gui_input *in, es2_gui_mouse_message msg, int32_t x, int32_t y, int mk_left,
                              int mk_right) {
    unsigned l = mk_left ? 1u : 0u, r = mk_right ? 2u : 0u, value;
    in->cursor_x = x;
    in->cursor_y = y;
    if (msg == ES2_GUI_WM_LBUTTONDOWN || msg == ES2_GUI_WM_RBUTTONDOWN) {
        if (in->intro_playing) in->skip_intro = 1;
        if (in->movie_open) in->movie_skip = 1;
        if (in->map_animation) in->map_skip = 1;
    }
    switch (msg) {
    case ES2_GUI_WM_MOUSEMOVE: value = l + 2 * r; break;   /* the right button lands in bit 2 */
    case ES2_GUI_WM_LBUTTONDOWN: value = 1 + 2 * r; break;
    case ES2_GUI_WM_LBUTTONUP: value = 2 * r; break;
    case ES2_GUI_WM_RBUTTONDOWN: value = l + 2; break;
    default: value = l; break;                             /* WM_RBUTTONUP */
    }
    if (msg != ES2_GUI_WM_MOUSEMOVE && in->movie_open) return;
    mouse_producer(in, x, y, (uint16_t)(value & 3));
}

static int hot_key_listed(unsigned low_byte) {
    for (size_t i = 0; i < sizeof hot_keys; i++)
        if (low_byte == hot_keys[i] || low_byte == (hot_keys[i] | ES2_GUI_KEY_UP)) return 1;
    return 0;
}

/* Original: VSHELL.EXE 0x409491 (ten entries, the oldest dropped when full) */
static void hot_store(es2_gui_input *in, uint16_t word) {
    in->hot[in->hot_write] = word;
    in->hot_write = (uint16_t)(in->hot_write + 1 == ES2_GUI_HOT_QUEUE ? 0 : in->hot_write + 1);
    if (in->hot_read == in->hot_write)
        in->hot_read = (uint16_t)(in->hot_read == ES2_GUI_HOT_QUEUE - 1 ? 0 : in->hot_read + 1);
}

/* Original: VSHELL.EXE 0x4051fa..0x40543e */
es2_gui_system_action es2_gui_key_message_in(es2_gui_input *in, unsigned vk, int up, int shift, int alt, int ctrl) {
    unsigned index = es2_gui_key_index(vk);
    if (!index) return ES2_GUI_SYS_NONE;
    unsigned word = index | (shift ? ES2_GUI_KEY_SHIFT : 0) | (alt ? ES2_GUI_KEY_ALT : 0) | (ctrl ? ES2_GUI_KEY_CTRL : 0);
    int active = !in->movie_open && in->interactive;
    es2_gui_system_action action = ES2_GUI_SYS_NONE;
    if (word == 0x23e && active) action = ES2_GUI_SYS_QUIT_BOX; /* and the word goes on to the hooks */
    if (word == 0x20f || word == 0x401 || word == 0x201) return active ? ES2_GUI_SYS_LEAVE_FULLSCREEN : action;
    if (word == 0x21c) return active && up ? ES2_GUI_SYS_TOGGLE_FULLSCREEN : action;
    if (up) word |= ES2_GUI_KEY_UP;
    if (in->keyboard && hot_key_listed(word & 0xff)) { /* hook 0x4094ee consumes it */
        hot_store(in, (uint16_t)word);
        return action;
    }
    unsigned next = (in->ring_write + 1) & 0xff;
    if (next == in->ring_read) return action; /* full ring: the key is lost */
    in->ring[in->ring_write] = (uint16_t)word;
    in->ring_write = next;
    return action;
}

static uint16_t ring_pop(es2_gui_input *in) {
    if (in->ring_read == in->ring_write) return 0;
    uint16_t word = in->ring[in->ring_read];
    in->ring_read = (in->ring_read + 1) & 0xff;
    return word;
}

static void post_simple(es2_gui_input *in, uint32_t type, uint16_t code) {
    es2_gui_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = type;
    ev.code = code;
    es2_gui_post(in, ev);
}

/* Original: VSHELL.EXE 0x4091fe (mouse) and 0x409545 (keyboard) */
void es2_gui_poll(es2_gui_input *in) {
    while (in->mouse_read != in->mouse_write) {
        const es2_gui_mouse_record *r = &in->mouse[in->mouse_read];
        es2_gui_event ev;
        memset(&ev, 0, sizeof ev);
        ev.type = ES2_GUI_EV_MOUSE;
        ev.code = r->code;
        if (r->code == ES2_GUI_MOUSE_MOVE) {
            ev.x = r->dx;
            ev.y = r->dy;
        }
        in->mouse_read = (uint16_t)(in->mouse_read + 1 == ES2_GUI_MOUSE_RING ? 0 : in->mouse_read + 1);
        es2_gui_post(in, ev);
    }
    if (!in->keyboard) return; /* -k: no keyboard device, nothing drains the ring */
    while (in->hot_read != in->hot_write) {
        uint16_t word = in->hot[in->hot_read];
        in->hot_read = (uint16_t)(in->hot_read + 1 == ES2_GUI_HOT_QUEUE ? 0 : in->hot_read + 1);
        /* Words with modifier bits index past VSHELL's 256-byte table into
         * unrelated bytes of the executable: they are read from the image
         * the host gave (es2_gui_input_set_hot_bytes); without it nothing is posted. */
        unsigned command = es2_gui_input_hot_command(in, word);
        if (command != 0xff) post_simple(in, ES2_GUI_EV_COMMAND, (uint16_t)command);
    }
    for (uint16_t word; (word = ring_pop(in)) != 0;) {
        if (word & ES2_GUI_KEY_UP) continue;
        if ((word & ES2_GUI_KEY_CTRL) && (word & 0xff) == 0x10) { /* Ctrl+Q: command 0x11, poll ends */
            post_simple(in, ES2_GUI_EV_COMMAND, 0x11);
            return;
        }
        unsigned c = es2_gui_key_character(word);
        if (c) post_simple(in, ES2_GUI_EV_KEY, (uint16_t)c);
    }
}

/* Original: VSHELL.EXE 0x468780 */
int es2_gui_post(es2_gui_input *in, es2_gui_event ev) {
    if (ev.type == ES2_GUI_EV_MOUSE && ev.code == ES2_GUI_MOUSE_MOVE && ev.x == 0 && ev.y == 0)
        return 0; /* the motion leaves the pointer where it was: the event is deleted */
    ev.x = in->cursor_x; /* 0x4689c1: the pointer position (display slot +0x14, GetCursorPos) */
    ev.y = in->cursor_y;
    if (in->queue_count == ES2_GUI_QUEUE_MAX) {
        in->queue_overflows++;
        return 0;
    }
    in->queue[(in->queue_head + in->queue_count) % ES2_GUI_QUEUE_MAX] = ev;
    in->queue_count++;
    return 1;
}

/* Original: VSHELL.EXE 0x41335d..0x4133d0 */
void es2_gui_post_synthetic_click(es2_gui_input *in, uint32_t target) {
    es2_gui_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = ES2_GUI_EV_MOUSE;
    ev.synthetic = 1;
    ev.target = target;
    ev.code = ES2_GUI_MOUSE_LEFT_DOWN;
    es2_gui_post(in, ev);
    ev.code = ES2_GUI_MOUSE_LEFT_UP;
    es2_gui_post(in, ev);
}

/* Original: VSHELL.EXE 0x469ee4 (0x469fd7..0x469fe5: until the queue is empty) */
unsigned es2_gui_drain(es2_gui_input *in, es2_gui_handler handler, void *context) {
    unsigned n = 0;
    while (in->queue_count) {
        es2_gui_event ev = in->queue[in->queue_head];
        in->queue_head = (in->queue_head + 1) % ES2_GUI_QUEUE_MAX;
        in->queue_count--;
        n++;
        if (handler) handler(context, &ev);
    }
    return n;
}
