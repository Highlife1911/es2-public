/* DBSIM's input layer as own code (input.md, "the input layer as own code"):
 * the input set-up 0x45a5dc that the mission-load root calls first, with the
 * device objects it builds (keyboard 0x45c01c/0x4779a8, mouse
 * 0x45c1d4/0x4805e0, the device registry 0x4d2468..0x4d24a4), the hot-key
 * registrations, the flight-key capture 0x45b68c, the prefs read
 * 0x45972c/0x45a1b8 (callbacks, the voice-archive test, the joystick device
 * 0x459df0 with its constructor 0x45c298 and destructor 0x45c2e0) and
 * keyjoy.cfg (the hook keyjoy_load through the dispatch); the device poll
 * 0x45baa8 with its readers (the key ring reader 0x45bbc4, the joystick object
 * 0x45c524, the joystick poll 0x477720/0x45c330, the mouse reader 0x45bc74),
 * the joystick summary 0x477904; the mouse window handler 0x4809f8 with the
 * mouse event 0x480948 and the queue callback 0x452cdc; the mouse queue set-up
 * 0x452ae4, the mouse bounds 0x45b758 and the key flush 0x477abc that the load
 * root calls later. Every call of other DBSIM code (hooks, other packages'
 * services, imports) goes through the dispatch; the original's internal callees
 * of these functions are own code here.
 *
 * The provider seam. What the original reads from the platform below these
 * functions is: GetKeyState (in the keyboard handler, a hook), joyGetDevCapsA
 * and joyGetPosEx (the joystick device), GetCursorPos, GetClientRect and
 * GetWindowRect (the mouse scale, the load root) and GetPrivateProfileStringA
 * (keyjoy.cfg, sound.cfg). Each is a service row over `es2_sim_input_platform`:
 * headless it answers from the oracle's input journal exactly as the proxy's
 * replay does (own_process.md section 8); a windowed host fills the same
 * structure from SDL3 events and devices. Window messages stay the pump's (the
 * journal is its message source). */
#ifndef ES2_SIM_INPUT_H
#define ES2_SIM_INPUT_H
#include <stdint.h>
#include "es2/core/guest.h"
#include "es2/sim/dispatch.h"
#include "es2/sim/loop.h"
#include "sim/oracle/journal.h"

/* Guest VAs (DBSIM.EXE). */
#define ES2_SIM_VA_DEVICE_POLL 0x0045baa8u
#define ES2_SIM_VA_DEVICE_BLOCK 0x004d23f6u   /* D: the poll's 0x29-byte result */
#define ES2_SIM_VA_DEVICE_SLOTS 0x004d2468u   /* device registry: object per id */
#define ES2_SIM_VA_KEY_RING 0x006bb6b0u       /* 256 words; write 0x6bb8b0, read 0x6bb8b4 */
#define ES2_SIM_VA_MOUSE_BLOCK 0x006c5f50u    /* x, y dwords, button word */
#define ES2_SIM_VA_JOY_SUMMARY 0x006bb6a8u

/* Win32 result codes the journal answers (mmsystem.h). */
#define ES2_JOYERR_PARMS 165u
#define ES2_JOYERR_UNPLUGGED 167u

/* The platform below the input functions. `frame` is the main loop's frame
 * counter [0x4d257b] at the call (the journal's key). Buffers are host
 * views of the guest's. */
typedef struct es2_sim_input_platform {
    /* GetKeyState(vk): the SHORT result. */
    int16_t (*key_state)(void *user, uint32_t frame, int32_t vk);
    /* joyGetDevCapsA(id, caps, size) / joyGetPosEx(id, info): the MMRESULT;
     * they may fill the buffer (size bytes; info's own dwSize). */
    uint32_t (*joy_caps)(void *user, uint32_t frame, uint32_t id, uint8_t *caps, uint32_t size);
    uint32_t (*joy_pos)(void *user, uint32_t frame, uint32_t id, uint8_t *info, uint32_t size);
    /* GetCursorPos: the BOOL result and the point. */
    uint32_t (*cursor_pos)(void *user, uint32_t frame, int32_t *x, int32_t *y);
    /* The main window's client size (GetClientRect's right and bottom; left
     * and top are 0). */
    void (*client_size)(void *user, int32_t *width, int32_t *height);
    void *user;
} es2_sim_input_platform;

typedef struct es2_sim_input es2_sim_input;
struct es2_sim_input {
    es2_sim_loop *loop;                   /* memory, dispatch, journal, the call area */
    es2_sim_input_platform platform;
    /* GetPrivateProfileStringA's file: the bytes of `name` (as the game
     * passes it, relative to the working directory) into `buffer`; 0 and
     * *size, or 1 when there is no such file. NULL: no file exists. */
    int (*read_file)(void *user, const char *name, uint8_t *buffer, uint32_t capacity, uint32_t *size);
    void *read_user;
    /* The headless journal platform's state (es2_sim_input_headless). */
    es2_journal *journal, empty;
    int32_t client_width, client_height;
    /* Calls, for the log and the tests. */
    unsigned key_states, joy_caps, joy_pos, cursor_reads, client_rects, window_rects, profile_reads;
    /* Out: a refusal inside the own code (also logged through the dispatch). */
    uint32_t refused_va;
};

/* Zeroes the record and sets the headless journal platform over `journal`
 * (NULL: an empty journal, so every answer is the replay's fallback) and the
 * client size `width` x `height` (the game size the window was made for). */
void es2_sim_input_headless(es2_sim_input *in, es2_sim_loop *loop, es2_journal *journal, int32_t width, int32_t height);
/* The instance the service rows use (one per process). */
void es2_sim_input_bind(es2_sim_input *in);

/* The pieces as own code, each 0, or 1 when a call was refused
 * (in->refused_va). `out` gets EAX, EDX, ECX as the original leaves them where
 * they are defined (input.md, "the input layer as own code"). */
int es2_sim_input_setup(es2_sim_input *in, es2_sim_result *out);              /* 0x45a5dc */
int es2_sim_device_poll(es2_sim_input *in, es2_sim_result *out);              /* 0x45baa8 */
int es2_sim_key_reader(es2_sim_input *in, uint32_t p, uint32_t id, es2_sim_result *out);   /* 0x45bbc4 */
int es2_sim_joystick_summary(es2_sim_input *in, es2_sim_result *out);         /* 0x477904 */
int es2_sim_mouse_handler(es2_sim_input *in, uint32_t message, uint32_t wparam, uint32_t lparam, uint32_t *result); /* 0x4809f8 */
int es2_sim_mouse_event(es2_sim_input *in, uint32_t code, uint32_t lparam, uint32_t buttons);   /* 0x480948 */
int es2_sim_mouse_queue_setup(es2_sim_input *in, es2_sim_result *out);        /* 0x452ae4 */
int es2_sim_mouse_bounds(es2_sim_input *in);                                  /* 0x45b758 */
int es2_sim_prefs_set(es2_sim_input *in, uint32_t index, uint32_t value, uint32_t flag, es2_sim_result *out); /* 0x459958 */

#endif
