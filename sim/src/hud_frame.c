/* The per-frame walk of the panel's row-1 children (hud.md, "the HUD line contract —
 * coupling set and frame writes" rows 1a, 1b, 1c, 1i; "the map draw's tail, the load root's
 * last steps, the radar draw and the per-frame row-1 walk"), called by panel_frame
 * (core/src/panel_frame.c) through the dispatch, and the small leaves they reach. Every
 * function names its listing range of the reference DBSIM.EXE (llvm-objdump -d -M intel,
 * SHA-256 8561aa47...c4ad).
 *
 * Kept: every gate, every clock read (0x467828) and every stream-B draw
 * (0x492ee0 on 0x4d260b) at its site and in its order, the globals and
 * object fields the functions write, the EventPort posts through the
 * port's vtable. Through the dispatch (no own row yet; open in hud.md):
 * the jolt step 0x42d4a8 (its end 0x42d624 is hud_load.h's jolt_end row), the distance 0x4928d0
 * and the EventPort post 0x436a3c. No pixels are drawn by these. At the end:
 * the HUD slot 2 draw 0x43d704, the slot 1 thunks and the
 * altitude ladder 0x43dd98 with its blits, lines and fill. */
#include "hud_internal.h"

#define VA_GC 0x006c5f70u
#define VA_PANEL 0x0049b088u
#define VA_DISPLAY_CURRENT 0x00429820u
#define VA_POWER_ON_START 0x00438ddcu
#define VA_FLASH_TOGGLE 0x0042ea70u  /* src/scene.c's rows */
#define VA_FLASH_END 0x0042ea94u
#define VA_JOLT_STEP 0x0042d4a8u
#define VA_JOLT_END 0x0042d624u
#define VA_DISTANCE 0x004928d0u
#define VA_CONDITION_RATIO 0x004152ecu

/* 0x438700-0x438748: the damage class of a condition ratio r (0..256):
 * p = ((256 - r) * 100) sar 8; 0 at p >= 90, 1 at >= 74, 2 at >= 51,
 * 3 at >= 1, else 4. */
static uint32_t damage_class(uint32_t r) {
    int32_t p = (int32_t)((0x100u - r) * 100u) >> 8;
    if (p >= 0x5a) return 0;
    if (p >= 0x4a) return 1;
    if (p >= 0x33) return 2;
    if (p >= 1) return 3;
    return 4;
}

/* 0x41b514-0x41b533: condition_ratio(o + 0x206, (word)a, (word)b). */
static int part_condition(hud_ctx *c, uint32_t o, uint32_t a, uint32_t b, uint32_t *eax) {
    return HCALL(c, VA_CONDITION_RATIO, eax, o + 0x206u, a & 0xffffu, b & 0xffffu);
}

/* 0x42d82c-0x42d853: GC+0x224 -= [0x4cfcb8], += the argument's low word
 * (zero-extended), which [0x4cfcb8] keeps. EAX = [0x6c5f70]. */
static uint32_t gc_bias_set(hud_ctx *c, uint32_t v) {
    uint32_t gc = hr32(c, VA_GC), w = v & 0xffffu;
    hw32(c, gc + 0x224u, hr32(c, gc + 0x224u) - hr32(c, 0x004cfcb8u));
    hw32(c, gc + 0x224u, hr32(c, gc + 0x224u) + w);
    hw32(c, 0x004cfcb8u, w);
    return gc;
}

/* 0x42d854-0x42d86d: GC+0x224 -= [0x4cfcb8]; [0x4cfcb8] <- 0. */
static uint32_t gc_bias_clear(hud_ctx *c, uint32_t *edx) {
    uint32_t gc = hr32(c, VA_GC), old = hr32(c, 0x004cfcb8u);
    hw32(c, gc + 0x224u, hr32(c, gc + 0x224u) - old);
    hw32(c, 0x004cfcb8u, 0);
    if (edx) *edx = old;
    return gc;
}

/* Post a 6-byte event {word kind, dword 0} through the EventPort at
 * [H+0x20b]: [[port]](port, &event). */
static int port_post(hud_ctx *c, uint32_t h, uint32_t event) {
    uint32_t port = hr32(c, h + 0x20bu);
    return HCALL(c, hr32(c, hr32(c, port)), NULL, port, event);
}

/* 0x432924-0x432a4e (row 1a): the power-on sequence of panel H. With
 * [H+0x241] set: e = clock - [H+0x241]; the ten elements [H+0x70+4i]
 * whose start delay (signed word 0x49b05a[i]) is below e (unsigned) and
 * whose byte +0x8c is clear start (0x438ddc); then [H+0x1e9], [H+0x1f5],
 * [H+0x1ed] when e > 0. Past e > 200: byte H+0x245 <- 1, the first of
 * the ten parts of [H+0x203] with a nonzero condition whose damage class
 * is below 90 selects event 0x22, else 0x21, posted to the port. */
static int power_on_walk(hud_ctx *c, uint32_t h) {
    if (hr32(c, h + 0x241u) == 0) return 0;
    uint32_t now = 0;
    if (hud_clock(c, 0x0043293du, &now)) return 1;
    uint32_t e = now - hr32(c, h + 0x241u);
    for (uint32_t i = 0; i < 10u; ++i) {
        uint32_t el = hr32(c, h + 0x70u + 4u * i);
        if (el == 0) continue;
        if (hsx16(hr16(c, 0x0049b05au + 2u * i)) >= e) continue;
        if (hr8(c, el + 0x8cu) != 0) continue;
        if (HCALL(c, VA_POWER_ON_START, NULL, el)) return 1;
    }
    static const uint32_t extra[3] = {0x1e9u, 0x1f5u, 0x1edu};
    for (unsigned k = 0; k < 3u; ++k) {
        uint32_t el = hr32(c, h + extra[k]);
        if (el == 0 || e == 0 || hr8(c, el + 0x8cu) != 0) continue;
        if (HCALL(c, VA_POWER_ON_START, NULL, el)) return 1;
    }
    if (e <= 200u) return 0;
    hw8(c, h + 0x245u, 1);
    uint32_t damaged = 0;
    for (uint32_t i = 0; i < 10u; ++i) {
        uint32_t r = 0;
        if (part_condition(c, hr32(c, h + 0x203u), 0, i, &r)) return 1;
        if ((r & 0xffffu) == 0) continue;
        if ((int32_t)damage_class(hsx16((uint16_t)r)) < 0x5a) { damaged = 1; break; }
    }
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x20u);
    if (!f) return hud_refuse(c, 0x00432924u, "power_on_walk", "the own code's call area is exhausted");
    hw16(c, f, (uint16_t)(damaged ? 0x22u : 0x21u));
    int bad = port_post(c, h, f);
    hud_frame_pop(c, top);
    return bad;
}

/* 0x43408c-0x434143 (row 1b): the jolt (hit flash and shake). A display in mode 4 clears
 * the end time [0x49b0fc]. With an end time: now = clock; now >= end
 * clears it (EAX 1). Before it: past the next toggle [0x49b100] the
 * flash toggles (0x42ea70) and the next toggle is now + draw % 10; then
 * a jolt step of draw % 5 (0x42d4a8); AL = 1. Without an end time but with a
 * toggle time: the flash ends (0x42ea94), the toggle time and the end
 * time clear, the jolt ends (0x42d624); AL = 1. Else EAX 0. */
static int jolt_frame(hud_ctx *c, uint32_t *eax) {
    uint32_t d = 0, r = 0;
    if (HCALL0(c, VA_DISPLAY_CURRENT, &d)) return 1;
    if (hr32(c, d + 0x14u) == 4u) hw32(c, 0x0049b0fcu, 0);
    if (hr32(c, 0x0049b0fcu) != 0) {
        uint32_t now = 0, v = 0;
        if (hud_clock(c, 0x004340b0u, &now)) return 1;
        if (now >= hr32(c, 0x0049b0fcu)) {
            hw32(c, 0x0049b0fcu, 0);
            *eax = 1;
            return 0;
        }
        if (now > hr32(c, 0x0049b100u)) {
            if (HCALL0(c, VA_FLASH_TOGGLE, NULL)) return 1;
            if (hud_rng_next(c, 0x004340d1u, &v)) return 1;
            hw32(c, 0x0049b100u, now + (v & 0xffffu) % 10u);
        }
        if (hud_rng_next(c, 0x004340efu, &v)) return 1;
        if (HCALL(c, VA_JOLT_STEP, &r, (v & 0xffffu) % 5u)) return 1;
        *eax = (r & ~0xffu) | 1u;
        return 0;
    }
    if (hr32(c, 0x0049b100u) == 0) { *eax = 0; return 0; }
    if (HCALL0(c, VA_FLASH_END, NULL)) return 1;
    hw32(c, 0x0049b100u, 0);
    if (HCALL0(c, VA_JOLT_END, &r)) return 1;
    hw32(c, 0x0049b0fcu, 0);
    *eax = (r & ~0xffu) | 1u;
    return 0;
}

/* 0x434194-0x434225 (row 1c): the bump (the GC bias curve). A display in mode 4 clears
 * the end time [0x49b638]; now = clock; now past the end clears it. With
 * an end time after now: i = ((now - [0x49b634]) * 10) / 60 (unsigned),
 * v = signed word 0x49b046[i], halved (sar 1) when word 0x4d253c is 0,
 * else shifted left by that word - 1; the GC bias takes v (0x42d82c) and
 * AL = 1. At the end time itself the bias clears (0x42d854) and so does
 * the end time; EAX 0. */
static int bump_frame(hud_ctx *c, uint32_t *eax) {
    uint32_t d = 0;
    if (HCALL0(c, VA_DISPLAY_CURRENT, &d)) return 1;
    if (hr32(c, d + 0x14u) == 4u) hw32(c, 0x0049b638u, 0);
    uint32_t now = 0;
    if (hud_clock(c, 0x004341afu, &now)) return 1;
    if (now > hr32(c, 0x0049b638u)) hw32(c, 0x0049b638u, 0);
    uint32_t end = hr32(c, 0x0049b638u);
    if (end == 0) { *eax = 0; return 0; }
    if (end > now) {
        uint32_t i = ((now - hr32(c, 0x0049b634u)) * 10u) / 0x3cu;
        int32_t v = (int32_t)hsx16(hr16(c, 0x0049b046u + 2u * i));
        uint16_t s = hr16(c, 0x004d253cu);
        if (s == 0) v >>= 1;
        else v = (int32_t)((uint32_t)v << (((uint32_t)(int32_t)(int16_t)s - 1u) & 31u));
        *eax = (gc_bias_set(c, (uint32_t)v) & ~0xffu) | 1u;
        return 0;
    }
    gc_bias_clear(c, NULL);
    hw32(c, 0x0049b638u, 0);
    *eax = 0;
    return 0;
}

/* 0x4349ac-0x434a20 (row 1i): the nav marker of the panel [0x49b088].
 * With byte H+0x26a set: d = distance([H+0x203] + 0x26, H+0x25e)
 * (0x4928d0). Armed (byte H+0x26b): d < 10000 clears H+0x26a and posts
 * event 0x1d; unarmed: d > 10000 arms it. */
static int nav_marker_check(hud_ctx *c) {
    uint32_t h = hr32(c, VA_PANEL), d = 0;
    if (hr8(c, h + 0x26au) == 0) return 0;
    if (HCALL(c, VA_DISTANCE, &d, hr32(c, h + 0x203u) + 0x26u, h + 0x25eu)) return 1;
    if (hr8(c, h + 0x26bu) != 0) {
        if ((int32_t)d >= 0x2710) return 0;
        hw8(c, h + 0x26au, 0);
        uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x18u);
        if (!f) return hud_refuse(c, 0x004349acu, "nav_marker_check", "the own code's call area is exhausted");
        hw16(c, f, 0x1du);
        hw32(c, f + 2u, 0);
        int bad = port_post(c, h, f);
        hud_frame_pop(c, top);
        return bad;
    }
    if ((int32_t)d > 0x2710) hw8(c, h + 0x26bu, 1);
    return 0;
}

/* ---- service rows */
void es2_sim_service_power_on_walk(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (power_on_walk(&c, sc->slots[0])) hud_failed(sc);
}
void es2_sim_service_jolt_frame(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t eax = 0;
    if (jolt_frame(&c, &eax)) { hud_failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_bump_frame(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t eax = 0;
    if (bump_frame(&c, &eax)) { hud_failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_nav_marker_check(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (nav_marker_check(&c)) hud_failed(sc);
}
void es2_sim_service_damage_class(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    sc->eax = damage_class(sc->slots[0]);
}
void es2_sim_service_part_condition(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t eax = 0;
    if (part_condition(&c, sc->slots[0], sc->slots[1], sc->slots[2], &eax)) { hud_failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_gc_bias_set(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    sc->eax = gc_bias_set(&c, sc->slots[0]);
    sc->ecx = sc->slots[0] & 0xffffu;
    sc->edx = sc->slots[0];
}
void es2_sim_service_gc_bias_clear(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t edx = 0;
    sc->eax = gc_bias_clear(&c, &edx);
    sc->edx = edx;
    sc->ecx = 0;
}

/* ---- the HUD's slot 2 draw 0x43d704, the gadgets' slot 1 thunks and
 * the altitude ladder 0x43dd98 with pixels (hud.md, "the HUD's slot 2 draw, the
 * gadgets' slot 1 thunks and the altitude ladder with pixels"). Every guest
 * call goes through the dispatch, with the registers the original leaves where
 * a callee or the return can see them. */
#define VA_DAMAGE_ALERT 0x004342e0u
#define VA_GAUGE_BLINK 0x00438bc0u
#define VA_VISIBLE 0x00431410u
#define VA_CANVAS_PUSH 0x004311e0u
#define VA_CANVAS_POP 0x00431210u
#define VA_PICK_IDLE 0x004282c8u
#define VA_PICK_OBJECT 0x004282c0u
#define VA_PICK_SELECT 0x004332dcu
#define VA_BLIT 0x004816a8u
#define VA_LABEL 0x00438920u
#define VA_MISSION_TIME 0x0043dcd4u
#define VA_SPEED_TEXT 0x0043dca0u
#define VA_RADAR_FORWARD 0x0043e114u
#define VA_LADDER 0x0043dd98u
#define VA_LINE 0x00483a04u
#define VA_RECT_FILL 0x00486704u
#define VA_TERRAIN_HEIGHT 0x0046e180u
#define VA_PLAYER 0x004d24e6u
#define VA_TERRAIN 0x004a0be0u
#define VA_HUD_BITMAPS 0x0049be78u

/* A call with explicit entry registers; `r` gets the registers it leaves. */
#define HREGS(c, va, in, r, ...) hud_call_regs((c), (va), (const uint32_t[]){__VA_ARGS__}, \
    (unsigned)(sizeof((const uint32_t[]){__VA_ARGS__}) / sizeof(uint32_t)), (in), (r))
static int call0_regs(hud_ctx *c, uint32_t va, const uint32_t in[3], es2_sim_result *r) {
    return hud_call_regs(c, va, NULL, 0, in, r);
}
static void set_out(es2_sim_result *out, uint32_t eax, uint32_t edx, uint32_t ecx) {
    out->eax = eax;
    out->edx = edx;
    out->ecx = ecx;
}

/* 0x43d704(H) (listing 0x43d704..0x43d9b0), the HUD's slot 2 (the per-frame
 * draw the render root 0x45fca8 and the drop descent call through [H]+8):
 * - the display record 0x429820's mode +0x14 is 4 and the byte +0x1c of
 *   [0x4cf99c] is 0: nothing (EAX 4);
 * - word H+0x74 <- the damage alert 0x4342e0(2); nonzero: H+0x7c, +0x80,
 *   +0x84, +0x88 <- the signed words 0x49be48, 0x49be52, 0x49be5c, 0x49be66
 *   indexed by it, then the blink 0x438bc0(H);
 * - blinking off (byte H+0x76) or [H+0xd7]'s rectangle not visible
 *   (0x431410): no more;
 * - the canvas pushed; with byte 0x49be74 set and 0x4282c8() true, the
 *   picked object 0x4282c0() goes to 0x4332dc([H+0x14], it) and the byte is
 *   cleared (dead in the reference binary: nothing writes 0x49be74);
 * - the target [[0x49b088]+0x26c] copied to H+0xb7.. (with the signed word
 *   +8 of its shape record [[+0x34]+4]; without one the original reads
 *   address 8, a fault: refused);
 * - the speed word [[[0x49b088]+0x203]+0x10] (while the power-on ramp
 *   H+0x8c runs and H+0x8d is clear, limited to (clock - [H+0x90]) * 50 in
 *   16 bits, the ramp ended when reached), negated in 16 bits, to slot 3 of
 *   [H+0xdb];
 * - slot 1 of each of the [H+0xff] gadgets [H+0xd7..];
 * - the reverse bitmap and label as in 0x43d5f0, the mission time, the
 *   speed text, the four labels, the radar 0x43e114, the altitude ladder
 *   0x43dd98, the canvas popped (the registers it leaves). */
static int hud_frame_draw(hud_ctx *c, uint32_t h, es2_sim_result *out) {
    es2_sim_result r;
    memset(&r, 0, sizeof(r));
    if (call0_regs(c, VA_DISPLAY_CURRENT, NULL, &r)) return 1;
    uint32_t esi = r.eax;
    if (hr32(c, esi + 0x14u) == 4u) {
        uint32_t edx = hr32(c, 0x004cf99cu);
        if (!hr8(c, edx + 0x1cu)) { set_out(out, 4u, edx, r.ecx); return 0; }
    }
    if (HREGS(c, VA_DAMAGE_ALERT, NULL, &r, 2u)) return 1;
    uint32_t eax = r.eax, edx = r.edx, ecx = 2u;
    esi = r.eax;
    hw16(c, h + 0x74u, (uint16_t)esi);
    if ((uint16_t)esi) {
        hw32(c, h + 0x7cu, hsx16(hr16(c, 0x0049be48u + 2u * hsx16((uint16_t)esi))));
        hw32(c, h + 0x80u, hsx16(hr16(c, 0x0049be52u + 2u * hsx16(hr16(c, h + 0x74u)))));
        hw32(c, h + 0x84u, hsx16(hr16(c, 0x0049be5cu + 2u * hsx16(hr16(c, h + 0x74u)))));
        hw32(c, h + 0x88u, hsx16(hr16(c, 0x0049be66u + 2u * hsx16(hr16(c, h + 0x74u)))));
        if (HREGS(c, VA_GAUGE_BLINK, NULL, &r, h)) return 1;
        eax = r.eax;
        edx = r.edx;
        ecx = h;
    }
    if (hr8(c, h + 0x76u)) { set_out(out, eax, edx, ecx); return 0; }
    uint32_t rect = hr32(c, h + 0xd7u) + 4u;
    if (HREGS(c, VA_VISIBLE, NULL, &r, rect)) return 1;
    if (!(r.eax & 0xffu)) { set_out(out, r.eax, r.edx, rect); return 0; }
    if (call0_regs(c, VA_CANVAS_PUSH, NULL, &r)) return 1;
    if (hr8(c, 0x0049be74u)) {
        if (call0_regs(c, VA_PICK_IDLE, NULL, &r)) return 1;
        if (r.eax & 0xffu) {
            if (call0_regs(c, VA_PICK_OBJECT, NULL, &r)) return 1;
            if (r.eax && HCALL(c, VA_PICK_SELECT, NULL, hr32(c, h + 0x14u), r.eax)) return 1;
            hw8(c, 0x0049be74u, 0);
        }
    }
    uint32_t t = hr32(c, VA_PANEL) + 0x26cu, obj = hr32(c, t);
    hw32(c, h + 0xb7u, obj);
    if (obj) {
        hw32(c, h + 0xbbu + 8u, hr32(c, t + 0xcu));
        hw32(c, h + 0xbbu, hr32(c, t + 4u));
        esi = hr32(c, t + 8u);
        hw32(c, h + 0xbbu + 4u, esi);
        hw16(c, h + 0xc9u, hr16(c, t + 0x10u));
        uint32_t shape = hr32(c, obj + 0x34u), rec = shape ? hr32(c, shape + 4u) : 0u;
        if (!rec) return hud_null_refuse(c, 0x0043d83bu, "HUD draw", "the target has no shape record: the original reads address 8");
        hw32(c, h + 0xcdu, hsx16(hr16(c, rec + 8u)));
        hw32(c, h + 0xd1u, 2000u);
        hw16(c, h + 0xc7u, hr16(c, obj + 0x10u));
    }
    uint16_t si = hr16(c, hr32(c, hr32(c, VA_PANEL) + 0x203u) + 0x10u);
    esi = (esi & 0xffff0000u) | si;
    if (!hr8(c, h + 0x8du) && hr8(c, h + 0x8cu)) {
        uint32_t now = 0;
        if (hud_clock(c, 0x0043d87bu, &now)) return 1;
        uint32_t ramp = (now - hr32(c, h + 0x90u)) * 50u;
        if (si > 0x8000u) {
            ramp = (ramp & 0xffff0000u) | (uint16_t)(0u - (ramp & 0xffffu));
            if (si < (uint16_t)ramp) esi = ramp;
            else hw8(c, h + 0x8du, 1);
        } else {
            if (si > (uint16_t)ramp) esi = ramp;
            else hw8(c, h + 0x8du, 1);
        }
    }
    esi = (esi & 0xffff0000u) | (uint16_t)(0u - (esi & 0xffffu));
    uint32_t speed = hr32(c, h + 0xdbu);
    if (HVCALL(c, speed, 0xcu, NULL, speed, esi)) return 1;
    for (uint32_t i = 0; (int32_t)i < (int32_t)hr32(c, h + 0xffu); ++i) {
        uint32_t g = hr32(c, h + 0xd7u + 4u * i);
        if (HVCALL(c, g, 4u, NULL, g)) return 1;
    }
    if (hr8(c, hr32(c, hr32(c, VA_PANEL) + 0x1e1u) + 0xb3u)) {
        uint32_t at = hr32(c, h + 0x103u) + 4u;
        if (HCALL(c, VA_BLIT, NULL, hr32(c, hr32(c, hr32(c, VA_HUD_BITMAPS) + 8u) + 0x38u), at, 0u)) return 1;
        if (HCALL(c, VA_LABEL, NULL, hr32(c, h + 0x103u), hr32(c, 0x004d1650u))) return 1;
    }
    if (HCALL(c, VA_MISSION_TIME, NULL, h) || HCALL(c, VA_SPEED_TEXT, NULL, h)) return 1;
    if (HCALL(c, VA_LABEL, NULL, hr32(c, 0x004d17ccu), hr32(c, 0x004d1658u))) return 1;
    if (HCALL(c, VA_LABEL, NULL, hr32(c, 0x004d17d0u), 0x004d17e4u)) return 1;
    if (HCALL(c, VA_LABEL, NULL, hr32(c, 0x004d17d4u), hr32(c, 0x004d165cu))) return 1;
    if (HCALL(c, VA_LABEL, NULL, hr32(c, 0x004d17d8u), 0x004d17eeu)) return 1;
    if (HREGS(c, VA_RADAR_FORWARD, NULL, &r, h)) return 1;
    uint32_t in[3] = {r.eax, r.edx, h};
    if (HREGS(c, VA_LADDER, in, &r, h)) return 1;
    in[0] = r.eax;
    in[1] = r.edx;
    in[2] = h;
    if (call0_regs(c, VA_CANVAS_POP, in, &r)) return 1;
    set_out(out, r.eax, r.edx, r.ecx);
    return 0;
}

/* 0x43e60f, 0x43e61d, 0x43e62b, 0x43e643, 0x43e651, 0x43e69f, 0x43e6ad
 * (the same body as gadgets.c's 0x43b7cc): slot 1 of the gunsights, the tapes
 * and the pip gadget's neighbours, calling slot 0 [[D]](D) with EAX = [D].
 * EAX and EDX are slot 0's, ECX the argument slot as read back after the
 * call (pop ecx). */
static int slot0_thunk(hud_ctx *c, uint32_t d, const uint32_t in[3], es2_sim_result *out) {
    es2_sim_loop *l = c->l;
    uint32_t top = l->stack_hi, va = hr32(c, hr32(c, d));
    if (top - l->stack_lo < 4u) return hud_refuse(c, va, NULL, "the own code's call area is exhausted");
    l->stack_hi -= 4u;
    hw32(c, l->stack_hi, d);
    es2_sim_result r;
    memset(&r, 0, sizeof(r));
    uint32_t regs[3] = {hr32(c, d), in[1], in[2]};
    int refused = es2_sim_call(l->dispatch, va, c->m->host(c->m->context, l->stack_hi), 1u, regs, &r);
    uint32_t back = hr32(c, l->stack_hi);
    l->stack_hi = top;
    if (refused) {
        c->h->refused_va = va;
        if (l->outcome == ES2_SIM_LOOP_EXITED) return 1;   /* the process ended inside */
        l->outcome = ES2_SIM_LOOP_REFUSED;
        l->refused_va = va;
        return 1;
    }
    set_out(out, r.eax, r.edx, back);
    return 0;
}

/* 0x43dd98(H) (listing 0x43dd98..0x43e110, frame 0x78), the altitude
 * ladder, for a flyer chassis only (word [[[0x4d24e6]+0x1f2]+0x50]; else
 * EAX [0x4d24e6], EDX its +0x1f2, ECX the entry's). L(x) is the local at
 * ebp - x; the points and rectangles the callees get live in the frame and
 * are read back from it where the original reads them.
 * - GC+0x22c <- &L0c, GC+0x228 <- &L14 (the colour records {0, 0x4b} and
 *   {0, 0x49}; they stay pointing into the dead frame);
 * - the bitmaps [[0x49be78]+8]+0x3c/+0x40/+0x44/+0x48 (scale top, scale
 *   bottom, marker, the scrolling tape); the terrain [0x4a0be0] floor
 *   +0x110 and ceiling +0x114 + 5000; the ground under the player
 *   0x46e180(terrain, &{P+0x26, P+0x2a}) and the player's height P+0x2e;
 * - top edi = [H+0x117] + the top bitmap's height, bottom esi =
 *   (0x24 << sy) + [H+0x117] - the bottom bitmap's height;
 * - both scale bitmaps blitted; the ground bar: two lines 0x483a04 and the
 *   fill 0x486704 from the clamped ground position down to esi; the marker
 *   at the clamped height position; with the clip rectangle and origin of
 *   GC (+0x210..+0x224, +0x208) set to the tape column, the tape bitmap
 *   twice ((height / 50) mod (0x24 << sy), then one bitmap height up); the
 *   GC fields restored.
 * The products are truncated to 32 bits (imul; cdq); a zero divisor or
 * INT_MIN / -1 faults in the original: refused. EAX: the old GC+0x208,
 * EDX: the old GC+0x224, ECX: the old GC+0x21c. */
static int ladder_div(hud_ctx *c, int32_t a, int32_t b, int32_t *q) {
    if (b == 0 || (a == INT32_MIN && b == -1))
        return hud_divide_refuse(c, VA_LADDER, (uint64_t)(int64_t)a, (uint32_t)b, "altitude ladder", "the original's idiv faults");
    *q = a / b;
    return 0;
}
static int32_t ladder_clamp(int32_t v, int32_t lo, int32_t hi) {
    if (lo > v) return lo;
    if (hi < v) return hi;
    return v;
}
int es2_sim_hud_altitude_ladder(hud_ctx *c, uint32_t h, uint32_t ecx, es2_sim_result *out) {
    uint32_t p = hr32(c, VA_PLAYER), chassis = hr32(c, p + 0x1f2u);
    if (!hr16(c, chassis + 0x50u)) { set_out(out, p, chassis, ecx); return 0; }
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x78u);
    if (!f) return hud_refuse(c, VA_LADDER, "altitude ladder", "the own code's call area is exhausted");
    uint32_t bp = f + 0x78u;
#define L(x) (bp - (x##u))
    uint32_t gc = hr32(c, VA_GC);
    hw32(c, L(0x4), hr32(c, 0x006c5ed0u));
    hw32(c, L(0xc), 0);
    uint32_t terr = hr32(c, VA_TERRAIN);
    hw32(c, L(0x14), 0);
    hw32(c, gc + 0x22cu, L(0xc));
    hw32(c, gc + 0x228u, L(0x14));
    uint32_t bm = hr32(c, hr32(c, VA_HUD_BITMAPS) + 8u);
    p = hr32(c, VA_PLAYER);
    uint32_t b18 = hr32(c, bm + 0x3cu), b1c = hr32(c, bm + 0x40u), b20 = hr32(c, bm + 0x44u), b24 = hr32(c, bm + 0x48u);
    uint32_t floor_ = hr32(c, terr + 0x110u), ceil_ = hr32(c, terr + 0x114u) + 5000u;
    hw32(c, L(0x38), hr32(c, p + 0x26u));
    hw32(c, L(0x34), hr32(c, p + 0x2au));
    uint32_t ground = 0;
    int bad = HCALL(c, VA_TERRAIN_HEIGHT, &ground, terr, L(0x38));
    if (bad) goto done;
    uint32_t height = hr32(c, p + 0x2eu);
    unsigned sy = hr8(c, 0x004d253cu) & 31u, sx = hr8(c, 0x004d253au) & 31u;
    uint32_t edi = hr32(c, h + 0x117u) + hsx16(hr16(c, b18 + 4u));
    uint32_t esi = (0x24u << sy) + hr32(c, h + 0x117u) - hsx16(hr16(c, b1c + 4u));
    uint32_t span = esi - edi;
    int32_t range = (int32_t)(ceil_ - floor_), q = 0;
    if ((bad = HCALL(c, VA_BLIT, NULL, b18, h + 0x113u, 0u))) goto done;
    hw32(c, L(0x44), hr32(c, h + 0x113u));
    hw32(c, L(0x40), esi);
    if ((bad = HCALL(c, VA_BLIT, NULL, b1c, L(0x44), 0u))) goto done;
    if ((bad = ladder_div(c, (int32_t)((ground - floor_) * span), range, &q))) goto done;
    hw32(c, L(0x40), (uint32_t)ladder_clamp((int32_t)(esi - (uint32_t)q), (int32_t)edi, (int32_t)esi));
    hw32(c, L(0x60), hr32(c, h + 0x113u) + hsx16(hr16(c, b18 + 6u)));
    hw32(c, L(0x68), hr32(c, L(0x60)) - (1u << sx));
    hw32(c, L(0x64), edi);
    hw32(c, L(0x5c), hr32(c, L(0x40)));
    hw32(c, L(0x10), 0x49u);
    hw32(c, L(0x44), hr32(c, L(0x68)));
    hw32(c, L(0x40), hr32(c, L(0x5c)));
    if ((bad = HCALL(c, VA_LINE, NULL, L(0x68), L(0x44)))) goto done;
    hw32(c, L(0x44), hr32(c, L(0x60)));
    hw32(c, L(0x40), hr32(c, L(0x64)));
    if ((bad = HCALL(c, VA_LINE, NULL, L(0x60), L(0x44)))) goto done;
    hw32(c, L(0x64), hr32(c, L(0x5c)));
    hw32(c, L(0x5c), esi);
    hw32(c, L(0x8), 0x4bu);
    if ((bad = HCALL(c, VA_RECT_FILL, NULL, L(0x68)))) goto done;
    if ((bad = ladder_div(c, (int32_t)((height - floor_) * span), range, &q))) goto done;
    hw32(c, L(0x40), (uint32_t)ladder_clamp((int32_t)(esi - (uint32_t)q), (int32_t)edi, (int32_t)esi));
    hw32(c, L(0x40), hr32(c, L(0x40)) - (uint32_t)((int32_t)hsx16(hr16(c, b20 + 4u)) >> 1));
    hw32(c, L(0x44), (uint32_t)((int32_t)(hr32(c, L(0x60)) - hr32(c, L(0x68))) >> 1) + hr32(c, L(0x68))
                         - (uint32_t)((int32_t)hsx16(hr16(c, b20 + 6u)) >> 1) + 1u);
    if ((bad = HCALL(c, VA_BLIT, NULL, b20, L(0x44), 0u))) goto done;
    uint32_t s210 = hr32(c, gc + 0x210u), s214 = hr32(c, gc + 0x214u), s218 = hr32(c, gc + 0x218u);
    uint32_t s21c = hr32(c, gc + 0x21cu), s220 = hr32(c, gc + 0x220u), s224 = hr32(c, gc + 0x224u);
    uint32_t s208 = hr32(c, gc + 0x208u);
    hw32(c, L(0x68), (2u << sx) + hr32(c, h + 0x113u));
    hw32(c, L(0x64), edi);
    hw32(c, L(0x60), hsx16(hr16(c, b24 + 6u)) + hr32(c, L(0x68)));
    hw32(c, L(0x5c), esi);
    hw32(c, gc + 0x210u, hr32(c, L(0x68)));
    hw32(c, gc + 0x214u, hr32(c, L(0x64)));
    hw32(c, gc + 0x218u, hr32(c, L(0x60)));
    hw32(c, gc + 0x21cu, hr32(c, L(0x5c)));
    hw32(c, gc + 0x224u, 0);
    hw32(c, gc + 0x220u, 0);
    hw32(c, gc + 0x208u, 1u);
    int32_t period = (int32_t)(0x24u << sy);
    hw32(c, L(0x44), hr32(c, L(0x68)));
    if (period == 0) { bad = hud_divide_refuse(c, VA_LADDER, 0, 0, "altitude ladder", "the original's idiv faults"); goto done; }
    hw32(c, L(0x40), (uint32_t)(((int32_t)height / 50) % period) + hr32(c, L(0x64)));
    if ((bad = HCALL(c, VA_BLIT, NULL, b24, L(0x44), 0u))) goto done;
    hw32(c, L(0x40), hr32(c, L(0x40)) - hsx16(hr16(c, b24 + 4u)));
    if ((bad = HCALL(c, VA_BLIT, NULL, b24, L(0x44), 0u))) goto done;
    hw32(c, gc + 0x210u, s210);
    hw32(c, gc + 0x214u, s214);
    hw32(c, gc + 0x218u, s218);
    hw32(c, gc + 0x21cu, s21c);
    hw32(c, gc + 0x220u, s220);
    hw32(c, gc + 0x224u, s224);
    hw32(c, gc + 0x208u, s208);
    set_out(out, s208, s224, s21c);
#undef L
done:
    hud_frame_pop(c, top);
    return bad;
}

void es2_sim_service_hud_frame_draw(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    es2_sim_result r;
    memset(&r, 0, sizeof(r));
    if (hud_frame_draw(&c, sc->slots[0], &r)) { hud_failed(sc); return; }
    sc->eax = r.eax;
    sc->edx = r.edx;
    sc->ecx = r.ecx;
}
void es2_sim_service_hud_slot0_thunk(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    es2_sim_result r;
    memset(&r, 0, sizeof(r));
    uint32_t in[3] = {sc->eax, sc->edx, sc->ecx};
    if (slot0_thunk(&c, sc->slots[0], in, &r)) { hud_failed(sc); return; }
    sc->eax = r.eax;
    sc->edx = r.edx;
    sc->ecx = r.ecx;
}
