/* The method-table bodies of the cockpit's panel gadgets as own code (hud.md,
 * "gauge and panel gadget constructors"). The gadgets themselves are built by
 * the own constructors (src/hud_gauges.c: the weapon toggle 0x442204,
 * the weapon button 0x4424b0, the power slider 0x442978; src/hud_cockpit.c: the
 * range gauge 0x441df8 and the range toggle 0x442c28), which store the original
 * method tables; the slots of those tables that had no own code are the rows
 * here, together with the twelve this-adjusting entries of the gadgets' event
 * bases.
 *
 * Every function names its listing range of the reference DBSIM.EXE
 * (llvm-objdump -d -M intel, SHA-256 8561aa47...c4ad). The gadget layout
 * used here: the method
 * table at +0x17, the redraw byte +0x1f (1: copy the panel rectangle
 * again, 2: draw, then copy; each pass counts it down by one), the event
 * base at +0x20 (the power slider: +0x3e) with its own table, the owner at
 * +0x24 (the power slider: +0x42), whose slot 0 takes (owner, gadget,
 * value). Every call (the panel copy 0x43122c, the press 0x438d9c, the
 * toggle draw 0x4423bc and every method-table slot) goes through the
 * dispatch in the listing's order; each table pointer is read where the
 * original reads it, after the call before it. */
#include "hud_internal.h"

#define VA_PANEL_COPY 0x0043122cu
#define VA_BUTTON_PRESS 0x00438d9cu
#define VA_TOGGLE_DRAW 0x004423bcu

#define ROW(name) void es2_sim_service_##name(es2_sim_service_call *sc, void *user)
#define ROW_BEGIN (void)user; HSERVE(sc, c); uint32_t eax = sc->eax
#define ROW_END(expr) do { if (expr) { hud_failed(sc); return; } sc->eax = eax; } while (0)

/* A call through slot `offset` of the gadget's method table at G+0x17. */
static int own_slot(hud_ctx *c, uint32_t g, uint32_t offset, uint32_t *eax) {
    return HCALL(c, hr32(c, hr32(c, g + 0x17u) + offset), eax, g);
}
/* The event base's slot +8 [[G+base]+8](G+base). */
static int base_slot8(hud_ctx *c, uint32_t g, uint32_t base, uint32_t *eax) {
    return HCALL(c, hr32(c, hr32(c, g + base) + 8u), eax, g + base);
}
/* The owner's slot 0 [[O]](O, G, v), O = [G+0x24]. */
static int owner_slot0(hud_ctx *c, uint32_t g, uint32_t v, uint32_t *eax) {
    uint32_t owner = hr32(c, g + 0x24u);
    return HCALL(c, hr32(c, hr32(c, owner)), eax, owner, g, v);
}
/* The count-down of the redraw byte after a pass (a read-modify-write of
 * memory after the calls, as the original's dec byte [G+0x1f]). */
static void redraw_step(hud_ctx *c, uint32_t g) { hw8(c, g + 0x1fu, (uint8_t)(hr8(c, g + 0x1fu) - 1u)); }

/* 0x4420a4(G) (listing 0x4420a4..0x4420c7), the range gauge's slot +8:
 * slot +0xc of each of the four items at G+0x18, in order. */
ROW(gadget_range_items) {
    ROW_BEGIN;
    uint32_t g = sc->slots[0];
    for (uint32_t i = 0; i < 4u; ++i)
        if (own_slot(&c, hr32(&c, g + 0x18u + 4u * i), 0xcu, &eax)) { hud_failed(sc); return; }
    sc->eax = eax;
}
/* 0x4421c8(G, key) (listing 0x4421c8..0x442203), the range gauge's slot
 * +0xc: the key word 0x26 presses (G, 1, 1), 0x29 presses (G, 0, 1)
 * (0x438d9c, a registry hook); EAX then the press's answer with AL 1; any
 * other key: EAX 0. */
ROW(gadget_range_key) {
    ROW_BEGIN;
    uint32_t g = sc->slots[0];
    uint16_t key = (uint16_t)sc->slots[1];
    if (key != 0x26u && key != 0x29u) { sc->eax = 0; return; }
    if (HCALL(&c, VA_BUTTON_PRESS, &eax, g, key == 0x26u ? 1u : 0u, 1u)) { hud_failed(sc); return; }
    sc->eax = (eax & ~0xffu) | 1u;
}
/* 0x44244c(B) (listing 0x44244c..0x44247d), the weapon toggle's slot +0xc,
 * and 0x442dbc(B) (listing 0x442dbc..0x442ded), the range toggle's: the
 * redraw byte 1: the panel copy (0, B, 0), 2: B's slot 0; either then
 * counts the byte down; any other value: nothing. */
static int redraw_pass(hud_ctx *c, uint32_t b, uint32_t *eax) {
    uint8_t s = hr8(c, b + 0x1fu);
    if (s == 1u) {
        if (HCALL(c, VA_PANEL_COPY, eax, 0u, b, 0u)) return 1;
    } else if (s == 2u) {
        if (own_slot(c, b, 0, eax)) return 1;
    } else {
        *eax = (*eax & ~0xffu) | (uint8_t)(s - 2u);
        return 0;
    }
    redraw_step(c, b);
    return 0;
}
ROW(gadget_toggle_redraw) { ROW_BEGIN; ROW_END(redraw_pass(&c, sc->slots[0], &eax)); }
/* 0x442480(B, v) (listing 0x442480..0x4424ac), the weapon toggle's slot
 * +8, and 0x442948(B, v) (listing 0x442948..0x442974), the weapon
 * button's: the owner's slot 0 (owner, B, v), B's slot +4, the event
 * base's slot +8. */
ROW(gadget_changed) {
    ROW_BEGIN;
    uint32_t b = sc->slots[0];
    ROW_END(owner_slot0(&c, b, sc->slots[1], &eax) || own_slot(&c, b, 4u, &eax) || base_slot8(&c, b, 0x20u, &eax));
}
/* 0x4428fc(B) (listing 0x4428fc..0x442944), the weapon button's slot
 * +0xc: the redraw byte 1: the panel copies (0, B, 0) and (0, B+0x56, 0);
 * 2: the toggle draw 0x4423bc(B), then B's slot 0; either then counts the
 * byte down; any other value: nothing. */
static int button_pass(hud_ctx *c, uint32_t b, uint32_t *eax) {
    uint8_t s = hr8(c, b + 0x1fu);
    if (s == 1u) {
        if (HCALL(c, VA_PANEL_COPY, eax, 0u, b, 0u) || HCALL(c, VA_PANEL_COPY, eax, 0u, b + 0x56u, 0u)) return 1;
    } else if (s == 2u) {
        if (HCALL(c, VA_TOGGLE_DRAW, eax, b) || own_slot(c, b, 0, eax)) return 1;
    } else {
        *eax = (*eax & ~0xffu) | (uint8_t)(s - 2u);
        return 0;
    }
    redraw_step(c, b);
    return 0;
}
ROW(gadget_button_redraw) { ROW_BEGIN; ROW_END(button_pass(&c, sc->slots[0], &eax)); }
/* 0x442c10(S) (listing 0x442c10..0x442c26), the power slider's slot +0xc:
 * the redraw byte 2: S's slot 0 (the byte is not counted down); else
 * nothing (EAX S). */
ROW(gadget_slider_redraw) {
    ROW_BEGIN;
    uint32_t s = sc->slots[0];
    eax = s;
    ROW_END(hr8(&c, s + 0x1fu) == 2u && own_slot(&c, s, 0, &eax));
}
/* 0x442df0(B, v) (listing 0x442df0..0x442e1e), the range toggle's slot +8:
 * only for v & 1: the event base's slot +8, then the owner's slot 0
 * (owner, B, v). */
ROW(gadget_range_toggle_changed) {
    ROW_BEGIN;
    uint32_t b = sc->slots[0], v = sc->slots[1];
    ROW_END((v & 1u) && (base_slot8(&c, b, 0x20u, &eax) || owner_slot0(&c, b, v, &eax)));
}
/* The event bases' entries 0x4433c7..0x443435 (listing 0x4433c7..0x44343e,
 * each `add dword [esp+4], -n; jmp target`): the gadget's method for the
 * event base's this, the first argument moved back by the base's offset
 * (0x20; the power slider's 0x3e), the registers passed through. */
ROW(gadget_base_thunk) {
    (void)user;
    HSERVE(sc, c);
    static const struct { uint32_t va, target, adjust; } entries[] = {
        {0x004433c7u, 0x00442df0u, 0x20u}, {0x004433d1u, 0x00442dbcu, 0x20u}, {0x004433dbu, 0x00442cb0u, 0x20u},
        {0x004433e5u, 0x0043901cu, 0x3eu}, {0x004433efu, 0x00442c10u, 0x3eu}, {0x004433f9u, 0x00442b60u, 0x3eu},
        {0x00443403u, 0x00442948u, 0x20u}, {0x0044340du, 0x004428fcu, 0x20u}, {0x00443417u, 0x004426e8u, 0x20u},
        {0x00443421u, 0x00442480u, 0x20u}, {0x0044342bu, 0x0044244cu, 0x20u}, {0x00443435u, 0x004423bcu, 0x20u},
    };
    unsigned i = 0;
    while (i < sizeof(entries) / sizeof(entries[0]) && entries[i].va != sc->va) ++i;
    if (i == sizeof(entries) / sizeof(entries[0])) {
        sc->refused = 1;
        sc->reason = "not an event-base entry of the gadgets";
        return;
    }
    uint32_t slots[2] = {sc->slots[0] - entries[i].adjust, sc->slots[1]};
    es2_sim_result r;
    if (hud_call_regs(&c, entries[i].target, slots, 2, (const uint32_t[]){sc->eax, sc->edx, sc->ecx}, &r)) { hud_failed(sc); return; }
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
}
