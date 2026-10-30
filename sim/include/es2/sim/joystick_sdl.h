/* The window's joystick over SDL3, a documented platform deviation (input.md,
 * "the joystick page and the SDL3 joystick"). The original asks winmm
 * (joyGetDevCapsA 0x496a66, joyGetPosEx 0x496a6c) through the input seam, and the
 * window layer (platform_sdl.c) answers with winmm itself by default. A modern
 * controller that winmm does not see (an XInput gamepad has no winmm driver on
 * some systems, a gamepad's axes are laid out differently) can instead be
 * answered from SDL3: the seam es2_sim_window_joystick then gets the caps and
 * pos below, which answer in winmm's layouts (JOYCAPSA 0x194 bytes, JOYINFOEX
 * 0x34 bytes) and with winmm's error numbers. Opt-in only
 * (ES2SIM_JOYSTICK=sdl); winmm stays the default.
 *
 * The mapping (input.md, "the joystick page and the SDL3 joystick", the table):
 * winmm devices 0 and 1 are the first two SDL joysticks in SDL's enumeration
 * order at the caps call; a device SDL knows as a gamepad is laid out X/Y left
 * stick, Z right stick vertical, R right stick horizontal, U/V left/right
 * trigger, buttons south, east, west, north, left/right shoulder, back, start,
 * left/right stick (bits 0..9), the d-pad as the POV in hundredths of degrees;
 * any other joystick reports its first six axes as X, Y, Z, R, U, V, its first
 * 32 buttons and its first hat. Axes go from SDL's -32768..32767 to winmm's
 * 0..65535 (the triggers' 0..32767 to 0..65535). */
#ifndef ES2_SIM_JOYSTICK_SDL_H
#define ES2_SIM_JOYSTICK_SDL_H
#include <stdint.h>
#include "es2/sim/platform_sdl.h"

/* winmm's numbers (mmsystem.h). */
#define ES2_JOYERR_PARMS 165u
#define ES2_JOYERR_UNPLUGGED 167u
#define ES2_JOYCAPSA_SIZE 0x194u
#define ES2_JOYINFOEX_SIZE 0x34u
#define ES2_JOY_POVCENTERED 0xffffu

/* One device as the backend sees it, in winmm's units. */
typedef struct es2_sim_joystick_state {
    uint16_t vendor, product;
    char name[32];                       /* szPname, NUL-terminated */
    uint32_t axes;                       /* axes reported, 0..6 (X Y Z R U V) */
    uint32_t buttons;                    /* buttons reported, 0..32 */
    int hat;                             /* a POV (hat or d-pad) */
    uint32_t axis[6];                    /* 0..65535 */
    uint32_t pressed;                    /* button bits */
    uint32_t pov;                        /* hundredths of degrees, or ES2_JOY_POVCENTERED */
} es2_sim_joystick_state;

/* Where the states come from: SDL3 below, or a test's fake. `rebind` is 1
 * at the caps call (devices are bound to ids again), 0 at the pos call.
 * 0 with *out filled, 1 when no device is bound to `id`, 2 when the bound
 * device went away (joyGetPosEx: JOYERR_PARMS, JOYERR_UNPLUGGED). */
typedef struct es2_sim_joystick_backend {
    int (*read)(void *user, uint32_t id, int rebind, es2_sim_joystick_state *out);
    void *user;
} es2_sim_joystick_backend;

/* joyGetDevCapsA(id, caps, size) and joyGetPosEx(id, info) over a backend,
 * with `size` the bytes the guest buffer has. */
uint32_t es2_sim_joystick_caps(const es2_sim_joystick_backend *b, uint32_t id, uint8_t *caps, uint32_t size);
uint32_t es2_sim_joystick_pos(const es2_sim_joystick_backend *b, uint32_t id, uint8_t *info, uint32_t size);

/* The pure halves of the SDL backend (tests call them). A gamepad's
 * readings: the six SDL gamepad axes in SDL's order (left x, left y, right
 * x, right y, left trigger, right trigger), the button bits in the order of
 * the header comment, the d-pad as SDL hat bits (up 1, right 2, down 4,
 * left 8). A joystick's: its first `naxes` axes, `nbuttons` buttons and the
 * first hat's bits when `nhats`. */
void es2_sim_joystick_map_gamepad(const int16_t axis[6], uint32_t pressed, uint8_t dpad, es2_sim_joystick_state *out);
void es2_sim_joystick_map_plain(const int16_t *axis, uint32_t naxes, uint32_t pressed, uint32_t nbuttons,
                                uint32_t nhats, uint8_t hat, es2_sim_joystick_state *out);
uint32_t es2_sim_joystick_pov(uint8_t hat_bits);

/* The SDL3 backend: up to two opened devices. */
struct SDL_Joystick;
struct SDL_Gamepad;
typedef struct es2_sim_joystick_sdl {
    es2_sim_joystick_backend backend;
    struct {
        uint32_t instance;                /* SDL_JoystickID, 0 when unbound */
        struct SDL_Joystick *joystick;
        struct SDL_Gamepad *gamepad;
    } dev[2];
    int open;
    unsigned binds, reads, unplugged;
    char error[160];
} es2_sim_joystick_sdl;

/* SDL's gamepad (and joystick) subsystem; 0, or 1 with s->error. */
int es2_sim_joystick_sdl_open(es2_sim_joystick_sdl *s);
void es2_sim_joystick_sdl_close(es2_sim_joystick_sdl *s);

/* The window's joystick by `mode` (the environment ES2SIM_JOYSTICK):
 * NULL, "" or "winmm" leaves the seam as it is (winmm, the default); "sdl"
 * opens `s` and points the seam at it. 0, or 1 with s->error (an unknown
 * mode, SDL failed). The window binary calls this once after
 * es2_sim_window_init:
 *     es2_sim_joystick_select(&window_layer.joystick, &joystick_sdl, getenv("ES2SIM_JOYSTICK"));
 */
int es2_sim_joystick_select(es2_sim_window_joystick *seam, es2_sim_joystick_sdl *s, const char *mode);

#endif
