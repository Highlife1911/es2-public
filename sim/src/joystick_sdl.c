/* The window's joystick over SDL3 (include/es2/sim/joystick_sdl.h; input.md,
 * "the joystick page and the SDL3 joystick"). A documented platform deviation,
 * opt-in; winmm stays the default. Built with ES2SIM_JOYSTICK_PROBE this file
 * is also //sim:joystick_probe, which prints what the game would be answered. */
#include "es2/sim/joystick_sdl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ---- winmm's answers over a backend */
/* JOYCAPSA (mmsystem.h): +0 wMid, +2 wPid, +4 szPname[32], +0x24..+0x38
 * wXmin wXmax wYmin wYmax wZmin wZmax, +0x3c wNumButtons, +0x40 wPeriodMin,
 * +0x44 wPeriodMax, +0x48..+0x5c wRmin wRmax wUmin wUmax wVmin wVmax,
 * +0x60 wCaps, +0x64 wMaxAxes, +0x68 wNumAxes, +0x6c wMaxButtons,
 * +0x70 szRegKey[32], +0x90 szOEMVxD[260]. The game reads wCaps and device
 * 0's wMaxButtons (input.c joystick_read). The A call fills the A layout
 * only, so the rest of the game's 0x328-byte buffer stays as it was. */
uint32_t es2_sim_joystick_caps(const es2_sim_joystick_backend *b, uint32_t id, uint8_t *caps, uint32_t size) {
    es2_sim_joystick_state s;
    if (!caps || !size) return ES2_JOYERR_PARMS;
    memset(&s, 0, sizeof s);
    if (!b || !b->read || b->read(b->user, id, 1, &s)) return ES2_JOYERR_PARMS;
    uint8_t c[ES2_JOYCAPSA_SIZE];
    memset(c, 0, sizeof c);
    put16(c + 0x00, s.vendor);
    put16(c + 0x02, s.product);
    memcpy(c + 0x04, s.name, sizeof s.name);
    c[0x04 + 31] = 0;
    for (unsigned k = 0; k < 6u; ++k) {
        static const uint32_t off[6] = {0x24u, 0x2cu, 0x34u, 0x48u, 0x50u, 0x58u};   /* the min of X Y Z R U V */
        put32(c + off[k], 0);
        put32(c + off[k] + 4u, 0xffffu);
    }
    put32(c + 0x3c, s.buttons);
    put32(c + 0x40, 10u);                 /* stand-ins: the game does not read the periods */
    put32(c + 0x44, 1000u);
    uint32_t has = 0;
    if (s.axes >= 3u) has |= 0x01u;       /* JOYCAPS_HASZ */
    if (s.axes >= 4u) has |= 0x02u;       /* JOYCAPS_HASR */
    if (s.axes >= 5u) has |= 0x04u;       /* JOYCAPS_HASU */
    if (s.axes >= 6u) has |= 0x08u;       /* JOYCAPS_HASV */
    if (s.hat) has |= 0x10u | 0x40u;      /* JOYCAPS_HASPOV | JOYCAPS_POVCTS */
    put32(c + 0x60, has);
    put32(c + 0x64, 6u);
    put32(c + 0x68, s.axes);
    put32(c + 0x6c, 32u);
    memcpy(caps, c, size < sizeof c ? size : sizeof c);
    return 0;
}

/* JOYINFOEX: +0 dwSize (must be 0x34), +4 dwFlags, +8..+0x1c X Y Z R U V,
 * +0x20 dwButtons, +0x24 dwButtonNumber, +0x28 dwPOV, +0x2c/+0x30
 * reserved. Only the fields dwFlags asks for are written (JOY_RETURNX 1 ..
 * V 0x20, POV 0x40, BUTTONS 0x80, POVCTS 0x200); the game asks 0xccf.
 * JOY_RETURNCENTERED and JOY_USEDEADZONE (0x400, 0x800: calibration in
 * winmm's driver) have no counterpart here: SDL's readings are passed on.
 * No device bound to the id answers JOYERR_PARMS (as this machine's winmm
 * answers joyGetPosEx(1) without a controller, measured with joystick_probe),
 * a bound device that went away JOYERR_UNPLUGGED. */
uint32_t es2_sim_joystick_pos(const es2_sim_joystick_backend *b, uint32_t id, uint8_t *info, uint32_t size) {
    es2_sim_joystick_state s;
    if (!info || size < ES2_JOYINFOEX_SIZE || get32(info) != ES2_JOYINFOEX_SIZE) return ES2_JOYERR_PARMS;
    memset(&s, 0, sizeof s);
    int r = b && b->read ? b->read(b->user, id, 0, &s) : 1;
    if (r) return r == 2 ? ES2_JOYERR_UNPLUGGED : ES2_JOYERR_PARMS;
    uint32_t flags = get32(info + 4);
    for (unsigned k = 0; k < 6u; ++k)
        if (flags & (1u << k)) put32(info + 8u + 4u * k, k < s.axes ? s.axis[k] : 0x8000u);
    if (flags & 0x80u) {
        uint32_t n = 0;
        for (uint32_t v = s.pressed; v; v &= v - 1u) ++n;
        put32(info + 0x20, s.pressed);
        put32(info + 0x24, n);
    }
    if (flags & (0x40u | 0x200u)) put32(info + 0x28, s.hat ? s.pov : ES2_JOY_POVCENTERED);
    return 0;
}

/* ---- the mapping */
static uint32_t stick(int16_t v) { return (uint32_t)((int32_t)v + 32768); }
static uint32_t trigger(int16_t v) {
    int32_t t = v < 0 ? 0 : v;
    return (uint32_t)(t * 65535 / 32767);
}
uint32_t es2_sim_joystick_pov(uint8_t hat) {
    switch (hat & 0x0fu) {
    case 0x1: return 0;                   /* up */
    case 0x3: return 4500u;               /* up right */
    case 0x2: return 9000u;               /* right */
    case 0x6: return 13500u;              /* down right */
    case 0x4: return 18000u;              /* down */
    case 0xc: return 22500u;              /* down left */
    case 0x8: return 27000u;              /* left */
    case 0x9: return 31500u;              /* up left */
    default: return ES2_JOY_POVCENTERED;  /* centred, or opposite directions */
    }
}
void es2_sim_joystick_map_gamepad(const int16_t axis[6], uint32_t pressed, uint8_t dpad, es2_sim_joystick_state *out) {
    out->axes = 6;
    out->buttons = 10;
    out->hat = 1;
    out->axis[0] = stick(axis[0]);        /* X: left stick x */
    out->axis[1] = stick(axis[1]);        /* Y: left stick y */
    out->axis[2] = stick(axis[3]);        /* Z: right stick y */
    out->axis[3] = stick(axis[2]);        /* R: right stick x */
    out->axis[4] = trigger(axis[4]);      /* U: left trigger */
    out->axis[5] = trigger(axis[5]);      /* V: right trigger */
    out->pressed = pressed & 0x3ffu;
    out->pov = es2_sim_joystick_pov(dpad);
}
void es2_sim_joystick_map_plain(const int16_t *axis, uint32_t naxes, uint32_t pressed, uint32_t nbuttons,
                                uint32_t nhats, uint8_t hat, es2_sim_joystick_state *out) {
    out->axes = naxes < 6u ? naxes : 6u;
    out->buttons = nbuttons < 32u ? nbuttons : 32u;
    for (uint32_t k = 0; k < 6u; ++k) out->axis[k] = k < out->axes ? stick(axis[k]) : 0x8000u;
    out->pressed = out->buttons < 32u ? pressed & ((1u << out->buttons) - 1u) : pressed;
    out->hat = nhats != 0;
    out->pov = nhats ? es2_sim_joystick_pov(hat) : ES2_JOY_POVCENTERED;
}

/* ---- the SDL3 backend */
static void sdl_unbind(es2_sim_joystick_sdl *s, unsigned i) {
    if (s->dev[i].gamepad) SDL_CloseGamepad(s->dev[i].gamepad);
    else if (s->dev[i].joystick) SDL_CloseJoystick(s->dev[i].joystick);
    s->dev[i].gamepad = NULL;
    s->dev[i].joystick = NULL;
    s->dev[i].instance = 0;
}
/* The caps call binds ids 0 and 1 to the first two joysticks SDL lists. */
static void sdl_rebind(es2_sim_joystick_sdl *s) {
    int n = 0;
    SDL_JoystickID *ids = SDL_GetJoysticks(&n);
    ++s->binds;
    for (unsigned i = 0; i < 2u; ++i) {
        SDL_JoystickID want = ids && (int)i < n ? ids[i] : 0;
        if (s->dev[i].instance == want && (s->dev[i].gamepad || s->dev[i].joystick)) continue;
        sdl_unbind(s, i);
        if (!want) continue;
        if (SDL_IsGamepad(want)) s->dev[i].gamepad = SDL_OpenGamepad(want);
        if (!s->dev[i].gamepad) s->dev[i].joystick = SDL_OpenJoystick(want);
        if (s->dev[i].gamepad || s->dev[i].joystick) s->dev[i].instance = want;
    }
    SDL_free(ids);
}
static void copy_name(char *dst, const char *src) {
    size_t n = src ? strlen(src) : 0;
    if (n > 31u) n = 31u;
    memset(dst, 0, 32);
    if (n) memcpy(dst, src, n);
}
static int sdl_read(void *user, uint32_t id, int rebind, es2_sim_joystick_state *out) {
    es2_sim_joystick_sdl *s = user;
    if (!s->open || id > 1u) return 1;
    if (rebind) sdl_rebind(s);
    SDL_UpdateJoysticks();
    ++s->reads;
    SDL_Gamepad *g = s->dev[id].gamepad;
    SDL_Joystick *j = s->dev[id].joystick;
    if ((g && !SDL_GamepadConnected(g)) || (j && !SDL_JoystickConnected(j))) {
        ++s->unplugged;
        sdl_unbind(s, id);
        return 2;
    }
    if (g) {
        static const SDL_GamepadAxis axes[6] = {SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY,
                                                SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY,
                                                SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER};
        static const SDL_GamepadButton buttons[10] = {
            SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST,
            SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
            SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_LEFT_STICK,
            SDL_GAMEPAD_BUTTON_RIGHT_STICK};
        int16_t a[6];
        uint32_t pressed = 0;
        uint8_t dpad = 0;
        for (unsigned k = 0; k < 6u; ++k) a[k] = SDL_GetGamepadAxis(g, axes[k]);
        for (unsigned k = 0; k < 10u; ++k)
            if (SDL_GetGamepadButton(g, buttons[k])) pressed |= 1u << k;
        if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_UP)) dpad |= SDL_HAT_UP;
        if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) dpad |= SDL_HAT_RIGHT;
        if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) dpad |= SDL_HAT_DOWN;
        if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) dpad |= SDL_HAT_LEFT;
        es2_sim_joystick_map_gamepad(a, pressed, dpad, out);
        out->vendor = SDL_GetGamepadVendor(g);
        out->product = SDL_GetGamepadProduct(g);
        copy_name(out->name, SDL_GetGamepadName(g));
        return 0;
    }
    if (j) {
        int na = SDL_GetNumJoystickAxes(j), nb = SDL_GetNumJoystickButtons(j), nh = SDL_GetNumJoystickHats(j);
        int16_t a[6] = {0, 0, 0, 0, 0, 0};
        uint32_t pressed = 0;
        if (na < 0) na = 0;
        if (nb < 0) nb = 0;
        if (nh < 0) nh = 0;
        for (int k = 0; k < na && k < 6; ++k) a[k] = SDL_GetJoystickAxis(j, k);
        for (int k = 0; k < nb && k < 32; ++k)
            if (SDL_GetJoystickButton(j, k)) pressed |= 1u << k;
        es2_sim_joystick_map_plain(a, (uint32_t)na, pressed, (uint32_t)nb, (uint32_t)nh,
                                   nh ? SDL_GetJoystickHat(j, 0) : 0, out);
        out->vendor = SDL_GetJoystickVendor(j);
        out->product = SDL_GetJoystickProduct(j);
        copy_name(out->name, SDL_GetJoystickName(j));
        return 0;
    }
    return 1;
}

int es2_sim_joystick_sdl_open(es2_sim_joystick_sdl *s) {
    memset(s, 0, sizeof *s);
    s->backend.read = sdl_read;
    s->backend.user = s;
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        snprintf(s->error, sizeof s->error, "SDL_InitSubSystem(GAMEPAD): %s", SDL_GetError());
        return 1;
    }
    s->open = 1;
    return 0;
}
void es2_sim_joystick_sdl_close(es2_sim_joystick_sdl *s) {
    if (!s->open) return;
    sdl_unbind(s, 0);
    sdl_unbind(s, 1);
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    s->open = 0;
}

/* ---- the seam */
static uint32_t seam_caps(void *user, uint32_t id, uint8_t *caps, uint32_t size) {
    const es2_sim_joystick_sdl *s = user;
    return es2_sim_joystick_caps(&s->backend, id, caps, size);
}
static uint32_t seam_pos(void *user, uint32_t id, uint8_t *info, uint32_t size) {
    const es2_sim_joystick_sdl *s = user;
    return es2_sim_joystick_pos(&s->backend, id, info, size);
}
int es2_sim_joystick_select(es2_sim_window_joystick *seam, es2_sim_joystick_sdl *s, const char *mode) {
    if (!mode || !*mode || !strcmp(mode, "winmm")) return 0;
    if (strcmp(mode, "sdl")) {
        memset(s, 0, sizeof *s);
        snprintf(s->error, sizeof s->error, "ES2SIM_JOYSTICK=%.40s: expected winmm or sdl", mode);
        return 1;
    }
    if (es2_sim_joystick_sdl_open(s)) return 1;
    seam->caps = seam_caps;
    seam->pos = seam_pos;
    seam->user = s;
    return 0;
}

#ifdef ES2SIM_JOYSTICK_PROBE
/* joystick_probe [seconds] [winmm|sdl]: asks the chosen joystick as the
 * game's device open and poll do (joyGetDevCapsA(i, buf, 0x328), then
 * joyGetPosEx with dwSize 0x34 and dwFlags 0xccf every 100 ms) and prints
 * the answers and the four axes as the game scales them (0x47785c:
 * ((v << 8) / 0xffff) - 0x80), the POV direction bit and the button byte. */
#include <windows.h>
#include <mmsystem.h>
static uint32_t probe_winmm_caps(void *user, uint32_t id, uint8_t *caps, uint32_t size) {
    (void)user;
    return joyGetDevCapsA((UINT_PTR)id, (LPJOYCAPSA)caps, size);
}
static uint32_t probe_winmm_pos(void *user, uint32_t id, uint8_t *info, uint32_t size) {
    (void)user;
    if (size < sizeof(JOYINFOEX)) return ES2_JOYERR_PARMS;
    return joyGetPosEx(id, (LPJOYINFOEX)info);
}
static int32_t scaled(uint32_t v) { return (int32_t)(v << 8) / 0xffff - 0x80; }
int main(int argc, char **argv) {
    int seconds = argc > 1 ? atoi(argv[1]) : 10;
    const char *mode = argc > 2 ? argv[2] : "sdl";
    static es2_sim_joystick_sdl s;
    es2_sim_window_joystick seam = {probe_winmm_caps, probe_winmm_pos, NULL};
    if (es2_sim_joystick_select(&seam, &s, mode)) { fprintf(stderr, "joystick_probe: %s\n", s.error); return 1; }
    printf("joystick_probe: %s, %d s\n", seam.user ? "SDL3 (ES2SIM_JOYSTICK=sdl)" : "winmm", seconds);
    static uint8_t caps[2][0x328];
    uint32_t has[2] = {0, 0};
    for (uint32_t i = 0; i < 2u; ++i) {
        uint32_t r = seam.caps(seam.user, i, caps[i], 0x328u);
        has[i] = get32(caps[i] + 0x60);
        printf("device %u: joyGetDevCapsA %u, name \"%.31s\", wCaps 0x%x, wNumAxes %u, wNumButtons %u, wMaxButtons %u\n",
               i, r, (const char *)caps[i] + 4, has[i], get32(caps[i] + 0x68), get32(caps[i] + 0x3c),
               get32(caps[i] + 0x6c));
    }
    for (int t = 0; t < seconds * 10; ++t) {
        uint8_t info[2][ES2_JOYINFOEX_SIZE];
        uint32_t r[2];
        for (uint32_t i = 0; i < 2u; ++i) {
            memset(info[i], 0, sizeof info[i]);
            put32(info[i], ES2_JOYINFOEX_SIZE);
            put32(info[i] + 4, 0xccfu);
            r[i] = seam.pos(seam.user, i, info[i], ES2_JOYINFOEX_SIZE);
        }
        const uint8_t *p = info[0];
        uint32_t pov = get32(p + 0x28);
        unsigned bit = pov == 0 ? 1 : pov == 18000u ? 2 : pov == 27000u ? 4 : pov == 9000u ? 8 : 0;
        printf("\rpos %u/%u  X %6u Y %6u Z %6u R %6u  buttons 0x%08x POV %5u | game: x %4d y %4d z %4d r %4d pov bit %u buttons 0x%02x   ",
               r[0], r[1], get32(p + 8), get32(p + 0xc), get32(p + 0x10), get32(p + 0x14), get32(p + 0x20), pov,
               scaled(get32(p + 8)), scaled(get32(p + 0xc)),
               scaled(has[0] & 1u ? get32(p + 0x10) : get32(info[1] + 8)),
               scaled(has[0] & 2u ? get32(p + 0x14) : get32(info[1] + 0xc)), bit, get32(p + 0x20) & 0xffu);
        fflush(stdout);
        SDL_Delay(100);
    }
    printf("\n");
    if (seam.user) es2_sim_joystick_sdl_close(&s);
    return 0;
}
#endif
