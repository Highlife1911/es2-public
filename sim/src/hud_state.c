/* The per-frame HUD control flow with its coupling calls (es2/sim/hud.h): the
 * state that decides when the HUD reads the clock 0x467828, draws stream B
 * (0x492ee0/0x492f24 on 0x4d260b) and calls the sound API, as own code without
 * the drawing. Contract: hud.md, "the HUD line contract — coupling set and
 * frame writes" (the coupling table and the per-frame order) and "the HUD line
 * in the own process"; every
 * function names its listing range of the reference DBSIM.EXE (llvm-objdump -d
 * -M intel, SHA-256 8561aa47...c4ad). Coupling calls go through hud_clock,
 * hud_rng_next, hud_rng_bounded and hud_sound (hud_internal.h), which call the
 * dispatch (the clock service, the RNG and sound hooks) and record the
 * original's call site in the trace. The MFD buttons' draw slots and the
 * panel copy draw through the dispatch (the graphics rows, the label and
 * bitmap rows); nothing here is counted in `headless`. */
#include "hud_internal.h"

#define VA_PANEL 0x0049b088u
#define VA_BUTTON_PRESS 0x00438d9cu

/* A widget's method slot +4 ([[W+0x17]+4], the redraw request of the
 * button classes), with the registers it leaves. */
static int widget_slot4(hud_ctx *c, uint32_t w, es2_sim_result *r) {
    return hud_call_regs(c, hr32(c, hr32(c, w + 0x17u) + 4u), (const uint32_t[]){w}, 1, NULL, r);
}

/* ======================================================================
 * 0x446e60(G, p) (listing 0x446e60..0x446fac): the MFD page switch.
 * p = G+0xc1 (the current page): nothing. Else button p on (byte +0x40 <-
 * 1 and its slot +4 when the byte was clear); the old page's button off
 * (the byte cleared when set, its slot +4 in any case) unless the old page
 * is -1; G+0xc1 <- p; word 0x49cbc6 != -1: byte 0x49cbc8 <- 0, the word <-
 * -1; buttons 6..12 by the page's row of 0x49cbde (13 bytes per page, one
 * per button from 6): a set byte turns a button in state 2 (word +0x1b) to
 * 0, a clear byte one in state 0 to 2, each with its slot +4; byte G+0xb0
 * <- 2 (redraw); page 0: G+0xb9 <- [G + 0x308 + 4 [G+0x318]]; page 3 (the
 * radar): button_press(G, 0xb when the player's byte +0x96 is 0, else 0xc,
 * 1) (hook; its button_release reads the clock); page 4: G+0xb9 <- the
 * cockpit target [[0x49b088]+0x210]. EAX, EDX, ECX as the original's last
 * instructions leave them on each path. */
static int mfd_page(hud_ctx *c, uint32_t g, uint32_t p, es2_sim_result *out) {
    es2_sim_result r = *out;
    if (p == hr32(c, g + 0xc1u)) return 0;
    uint32_t b = hr32(c, g + 0x18u + 4u * p);
    if (!hr8(c, b + 0x40u)) {
        hw8(c, b + 0x40u, 1);
        if (widget_slot4(c, b, &r)) return 1;
        r.ecx = b;
    }
    uint32_t old = hr32(c, g + 0xc1u);
    r.eax = old;
    if (old != 0xffffffffu) {
        uint32_t o = hr32(c, g + 0x18u + 4u * old);
        if (hr8(c, o + 0x40u)) hw8(c, o + 0x40u, 0);
        if (widget_slot4(c, o, &r)) return 1;
        r.ecx = o;
    }
    hw32(c, g + 0xc1u, p);
    if (hr16(c, 0x0049cbc6u) != 0xffffu) {
        hw8(c, 0x0049cbc8u, 0);
        hw16(c, 0x0049cbc6u, 0xffffu);
    }
    uint32_t row = 0x0049cbdeu + 13u * p;
    for (uint32_t k = 6; k < 13u; ++k, ++row) {
        uint32_t w = hr32(c, g + 0x18u + 4u * k);
        r.eax = hsx16(hr16(c, w + 0x1bu));
        if (hr8(c, row) && r.eax == 2u) {
            hw16(c, w + 0x1bu, 0);
            if (widget_slot4(c, w, &r)) return 1;
            r.ecx = w;
        } else if (!hr8(c, row) && r.eax == 0) {
            hw16(c, w + 0x1bu, 2);
            if (widget_slot4(c, w, &r)) return 1;
            r.ecx = w;
        }
    }
    hw8(c, g + 0xb0u, 2);
    uint32_t page = hr32(c, g + 0xc1u);
    if (page == 0) {
        r.eax = hr32(c, g + 0x318u);
        r.edx = hr32(c, g + 4u * r.eax + 0x308u);
        hw32(c, g + 0xb9u, r.edx);
        r.ecx = 0xffffffffu;
    } else if (page == 3) {
        uint32_t player = hr32(c, hr32(c, VA_PANEL) + 0x203u);
        uint32_t button = hr8(c, player + 0x92u + 4u) ? 0xcu : 0xbu;
        if (hud_call_regs(c, VA_BUTTON_PRESS, (const uint32_t[]){g, button, 1u}, 3, NULL, &r)) return 1;
    } else if (page == 4) {
        r.ecx = hr32(c, VA_PANEL);
        r.eax = hr32(c, r.ecx + 0x210u);
        hw32(c, g + 0xb9u, r.eax);
    } else {
        r.ecx = page - 4u;
    }
    *out = r;
    return 0;
}
void es2_sim_service_mfd_page(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    es2_sim_result r;
    memset(&r, 0, sizeof(r));
    r.eax = sc->eax; r.edx = sc->edx; r.ecx = sc->ecx;
    if (mfd_page(&c, sc->slots[0], sc->slots[1], &r)) { hud_failed(sc); return; }
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
}

/* ======================================================================
 * The widget button methods (the tables at W+0x17 and the second base at
 * W+0x20; hud.md, "the HUD line in the own process").
 *
 * 0x438e2c(E) (listing 0x438e2c..0x438e38): the UI click sound
 * sfx_play(0x11) (coupling: one sound call, site 0x438e31). EAX the
 * hook's. */
#define VA_SFX_PLAY 0x00462830u
void es2_sim_service_ui_click(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    es2_sim_result r;
    if (HSOUND(&c, 0x00438e31u, VA_SFX_PLAY, &r, 0x11u)) { hud_failed(sc); return; }
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = 0x11u;
}
/* The press: the owner's slot 0 [[B+0x24]](owner, B, flags), B's slot +4,
 * the second base's slot +8 [[B+0x20]+8](B+0x20) (the click sound for the
 * button classes). 0x438e3c(B, flags) (listing 0x438e3c..0x438e6c) does it
 * when flags & 1; 0x4474d0(B, flags) (0x4474d0..0x44750a), the toggle,
 * when byte B+0x40 is 0 and flags & 1, after B+0x40 ^= 1. EAX, EDX, ECX
 * the last call's (ECX the popped argument). */
static int button_fire(hud_ctx *c, uint32_t b, uint32_t flags, es2_sim_result *r) {
    uint32_t owner = hr32(c, b + 0x24u);
    if (hud_call_regs(c, hr32(c, hr32(c, owner)), (const uint32_t[]){owner, b, flags}, 3, NULL, r)) return 1;
    if (widget_slot4(c, b, r)) return 1;
    if (hud_call_regs(c, hr32(c, hr32(c, b + 0x20u) + 8u), (const uint32_t[]){b + 0x20u}, 1, NULL, r)) return 1;
    r->ecx = b + 0x20u;
    return 0;
}
void es2_sim_service_push_press(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    es2_sim_result r;
    memset(&r, 0, sizeof(r));
    r.eax = sc->slots[1]; r.edx = sc->edx; r.ecx = sc->ecx;
    if ((sc->slots[1] & 1u) && button_fire(&c, sc->slots[0], sc->slots[1], &r)) { hud_failed(sc); return; }
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
}
void es2_sim_service_toggle_press(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t b = sc->slots[0], flags = sc->slots[1];
    es2_sim_result r;
    memset(&r, 0, sizeof(r));
    r.eax = flags; r.edx = sc->edx; r.ecx = sc->ecx;
    if (!hr8(&c, b + 0x40u) && (flags & 1u)) {
        hw8(&c, b + 0x40u, (uint8_t)(hr8(&c, b + 0x40u) ^ 1u));
        if (button_fire(&c, b, flags, &r)) { hud_failed(sc); return; }
    }
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
}
/* The widget frame 0x4475c8(W) (listing 0x4475c8..0x4475f9; 0x447410 has
 * the same bytes): byte W+0x1f = 2: the draw slot [[W+0x17]](W) and the
 * byte - 1; = 1: 0x43122c(0, W, 0) (the copy to the screen) and the byte -
 * 1; else nothing. */
void es2_sim_service_widget_frame(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t w = sc->slots[0];
    uint8_t s = hr8(&c, w + 0x1fu);
    es2_sim_result r;
    memset(&r, 0, sizeof(r));
    if (s == 2) {
        if (hud_call_regs(&c, hr32(&c, hr32(&c, w + 0x17u)), (const uint32_t[]){w}, 1, NULL, &r)) { hud_failed(sc); return; }
        hw8(&c, w + 0x1fu, (uint8_t)(hr8(&c, w + 0x1fu) - 1u));
    } else if (s == 1) {
        if (hud_call_regs(&c, 0x0043122cu, (const uint32_t[]){0u, w, 0u}, 3, NULL, &r)) { hud_failed(sc); return; }
        hw8(&c, w + 0x1fu, (uint8_t)(hr8(&c, w + 0x1fu) - 1u));
    }
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
}
/* The MFD buttons' draw slots (drawn). 0x44750c(B)
 * (listing 0x44750c..0x4475c6), the toggle button: the canvas push 0x4311e0
 * (EAX the canvas context K); K+0x208 <- 0; the bitmap [B+0x30 + 4 byte B+0x40]
 * of the state through 0x45c8bc(0x4d24bc, bitmap, B); K+0x208 <- 1; the pop
 * 0x431210. For ids (B+0x28) below 6 and 11, 12 (unsigned) the label's font
 * [[B+0x2c]] <- [0x49b0ac + 4 (byte B+0x40 ? 0xc : 0xa)]. Then the label text
 * 0x438920([B+0x2c], s): for ids below 6 (signed) s is a three-byte local at
 * ebp-3: byte [0x49cea6], the digit '1' + id, byte [0x49cea8] (the word
 * [0x49cea6] is written first and its high byte overwritten by the digit); else
 * s = [0x4d135c + 4 id]. EAX the label's; EDX the label's (the push button's id
 * 10: the mark's); ECX the local dword popped at the end (`push ecx` / `pop
 * ecx`): the entry ECX, for ids below 6 with the three text bytes over its
 * upper 24 bits. */
static int mfd_label(hud_ctx *c, uint32_t b, uint32_t text_va, uint32_t *ecx, es2_sim_result *r) {
    uint32_t id = hr32(c, b + 0x28u);
    if ((int32_t)id >= 6) return hud_call_regs(c, 0x00438920u, (const uint32_t[]){hr32(c, b + 0x2cu), hr32(c, 0x004d135cu + 4u * id)}, 2, NULL, r);
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 4u);
    if (!f) return hud_refuse(c, 0x00438920u, "mfd label", "the own code's call area is exhausted");
    uint32_t s = f + 1u;  /* ebp-3 */
    hw32(c, f, *ecx);     /* ebp-4: the pushed ECX */
    hw16(c, s, hr16(c, text_va));
    hw8(c, s + 2u, hr8(c, text_va + 2u));
    hw8(c, s + 1u, (uint8_t)(hr8(c, b + 0x28u) + 0x31u));
    int bad = hud_call_regs(c, 0x00438920u, (const uint32_t[]){hr32(c, b + 0x2cu), s}, 2, NULL, r);
    *ecx = hr32(c, f);
    hud_frame_pop(c, top);
    return bad;
}
void es2_sim_service_mfd_toggle_draw(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t b = sc->slots[0], k = 0;
    es2_sim_result r;
    memset(&r, 0, sizeof(r));
    if (HCALL0(&c, 0x004311e0u, &k)) { hud_failed(sc); return; }
    hw32(&c, k + 0x208u, 0);
    if (HCALL(&c, 0x0045c8bcu, NULL, 0x004d24bcu, hr32(&c, b + 0x30u + 4u * hr8(&c, b + 0x40u)), b)) { hud_failed(sc); return; }
    hw32(&c, k + 0x208u, 1);
    if (HCALL0(&c, 0x00431210u, NULL)) { hud_failed(sc); return; }
    uint32_t id = hr32(&c, b + 0x28u);
    if (id < 6u || id - 11u < 2u)
        hw32(&c, hr32(&c, b + 0x2cu), hr32(&c, 0x0049b0acu + 4u * (hr8(&c, b + 0x40u) ? 0xcu : 0xau)));
    uint32_t ecx = sc->ecx;
    if (mfd_label(&c, b, 0x0049cea6u, &ecx, &r)) { hud_failed(sc); return; }
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = ecx;
}
/* 0x447380(B) (listing 0x447380..0x44740f), the push button: the bitmap
 * [B+0x30 + 4 word B+0x1b (signed)] through 0x45c8bc(0x4d24bc, bitmap, B)
 * (no canvas push); the label as 0x44750c's with the bytes of 0x49cea3 /
 * 0x49cea5; id 10 (the orders button): P = [[B+0x24]+0xd1], the
 * page-tab mark 0x43fa5c(P, menu_code(P, 0), 0) (0x43f9c0 a hook). EAX
 * the last call's. */
void es2_sim_service_mfd_push_draw(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t b = sc->slots[0], ecx = sc->ecx;
    es2_sim_result r;
    memset(&r, 0, sizeof(r));
    if (HCALL(&c, 0x0045c8bcu, NULL, 0x004d24bcu, hr32(&c, b + 0x30u + 4u * hsx16(hr16(&c, b + 0x1bu))), b) ||
        mfd_label(&c, b, 0x0049cea3u, &ecx, &r)) { hud_failed(sc); return; }
    if (hr32(&c, b + 0x28u) == 0xau) {
        uint32_t page = hr32(&c, hr32(&c, b + 0x24u) + 0xd1u), code = 0;
        if (HCALL(&c, 0x0043f9c0u, &code, page, 0u) ||
            hud_call_regs(&c, 0x0043fa5cu, (const uint32_t[]){page, code, 0u}, 3, NULL, &r)) { hud_failed(sc); return; }
    }
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = ecx;
}
/* The second-base thunks (listing 0x447b0d..0x447b44): the this pointer -
 * 0x20, then a jump to 0x4474d0, 0x4475c8, 0x44750c, 0x438e3c, 0x447410,
 * 0x447380. */
void es2_sim_service_base2_thunk(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t target;
    switch (sc->va) {
    case 0x00447b0du: target = 0x004474d0u; break;
    case 0x00447b17u: target = 0x004475c8u; break;
    case 0x00447b21u: target = 0x0044750cu; break;
    case 0x00447b2bu: target = 0x00438e3cu; break;
    case 0x00447b35u: target = 0x00447410u; break;
    default: target = 0x00447380u; break;
    }
    uint32_t slots[2] = {sc->slots[0] - 0x20u, sc->slots[1]};
    es2_sim_result r;
    if (hud_call_regs(&c, target, slots, 2, (const uint32_t[]){sc->eax, sc->edx, sc->ecx}, &r)) { hud_failed(sc); return; }
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
}
/* 0x43122c(mode, rect, point) (listing 0x43122c..0x43140f): the copy of a
 * panel rectangle between the view's pages. The gate (EAX 0): word
 * [0x4d2566] set, panel_visible(rect) (hook) AL clear, the transition byte
 * [[0x4cf99c]+0x1c] set, or mode 1 with word [0x4d252a] set. Else, with V
 * = [0x6c5ed0] and its pages A = V+0x88, B = V+0x8c (mode 1: the two
 * swapped) and the old show page S = V+0x3c: the canvas push 0x4311e0;
 * the draw page V+0x38 <- A and the show page V+0x3c <- B, each with its
 * current copy (0x4a3686 / 0x4a368a) and driver slot ([0x4a57ec] /
 * [0x4a57f0], argument the page) when V is current; the construction
 * counter + 3 (a local region object, vtables only on the stack); the
 * region {kind 0, rect} at ebp-0x14 with the right and bottom edge + 1
 * where the shift word [0x4d253a] / [0x4d253c] (low five bits) is 1..30,
 * the whole rectangle less (dword 0x4d2556, 0x4d255a); the driver's lock
 * [0x4a5828], the copy 0x487e60(region, point or 0x49b07e when 0) (gfx.c's
 * row), the unlock [0x4a5824]; the show page back to S (with its copy and
 * slot); the pop 0x431210; AL 1 over the pop's EAX; the counter - 3. The
 * frame record of 0x49630c is not linked. The copy is done; EDX and ECX as
 * the listing leaves them: on the
 * gates EDX the saved head (FS:0 at entry) and ECX the last value loaded,
 * on the copy ECX the saved head and EDX the counter pointer [0x6cbee4]. */
static int edge_shift(uint16_t s) { uint32_t n = s & 31u; return n && n != 31u; }
static int page_field(hud_ctx *c, uint32_t v, uint32_t off, uint32_t cur, uint32_t slot, uint32_t page) {
    hw32(c, v + off, page);
    if (v != hr32(c, 0x006c5ed0u)) return 0;
    hw32(c, cur, hr32(c, v + off));
    return hud_call(c, hr32(c, slot), &page, 1, NULL);
}
void es2_sim_service_panel_copy(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t visible = 0, mode = sc->slots[0], rect = sc->slots[1], point = sc->slots[2];
    /* The gates (listing 0x431242..0x4312ab): EAX 0, EDX the saved head;
     * ECX the entry's, the popped rect, the display pointer, the old show
     * page. */
    sc->eax = 0;
    sc->edx = hud_fs0();
    if (hr16(&c, 0x004d2566u)) return;
    if (HCALL(&c, 0x00431410u, &visible, rect)) { hud_failed(sc); return; }
    sc->ecx = rect;
    if (!(uint8_t)visible) return;
    sc->ecx = hr32(&c, 0x004cf99cu);
    if (hr8(&c, sc->ecx + 0x1cu)) return;
    uint32_t v = hr32(&c, 0x006c5ed0u), a = hr32(&c, v + 0x88u), b = hr32(&c, v + 0x8cu), shown = hr32(&c, v + 0x3cu);
    if (mode == 1u) {
        sc->ecx = shown;
        if (hr16(&c, 0x004d252au)) return;
        uint32_t t = a; a = b; b = t;
    }
    uint32_t top = c.l->stack_hi, f = hud_frame_push(&c, 0x14u);
    if (!f) { hud_refuse(&c, 0x0043122cu, "panel copy", "the own code's call area is exhausted"); hud_failed(sc); return; }
    es2_sim_result r;
    memset(&r, 0, sizeof(r));
    if (HCALL0(&c, 0x004311e0u, NULL) || page_field(&c, v, 0x38u, 0x004a3686u, 0x004a57ecu, a) ||
        page_field(&c, v, 0x3cu, 0x004a368au, 0x004a57f0u, b)) goto failed;
    hud_counter(&c, 3);
    hw32(&c, f, 0);
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(&c, f + 4u + i, hr32(&c, rect + i));
    /* (1 << (cl & 31)) - 1 through `dec; jle` (0x43135c..0x431384): the edge
     * grows for shifts 1..30; 0 gives 0 and 31 overflows (OF set, SF clear). */
    if (edge_shift(hr16(&c, 0x004d253au))) hw32(&c, f + 0xcu, hr32(&c, f + 0xcu) + 1u);
    if (edge_shift(hr16(&c, 0x004d253cu))) hw32(&c, f + 0x10u, hr32(&c, f + 0x10u) + 1u);
    uint32_t dx = hr32(&c, 0x004d2556u), dy = hr32(&c, 0x004d255au);
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(&c, f + 4u + i, hr32(&c, f + 4u + i) - ((i & 4u) ? dy : dx));
    if (!point) point = 0x0049b07eu;
    if (HCALL0(&c, hr32(&c, 0x004a5828u), NULL) || HCALL(&c, 0x00487e60u, NULL, f, point) ||
        HCALL0(&c, hr32(&c, 0x004a5824u), NULL) || page_field(&c, v, 0x3cu, 0x004a368au, 0x004a57f0u, shown) ||
        hud_call_regs(&c, 0x00431210u, NULL, 0, NULL, &r)) goto failed;
    hud_counter(&c, -3);
    hud_frame_pop(&c, top);
    sc->eax = (r.eax & 0xffffff00u) | 1u;
    sc->edx = hr32(&c, 0x006cbee4u);   /* the counter pointer (0x4313f7) */
    sc->ecx = hud_fs0();
    return;
failed:
    hud_frame_pop(&c, top);
    hud_failed(sc);
}
/* ======================================================================
 * The HDD page switch and its pages' activation slots.
 *
 * 0x44a60c(H, p) (listing 0x44a60c..0x44a701): unless p is the current
 * page H+0x109: the old page's button off (byte +0x40 cleared when set,
 * its slot +4) and the old page's slot +8, H+0x109 <- p, the new page's
 * slot +0xc ([H+0x101+4p]), its button on (byte +0x40 <- 1 and slot +4
 * when it was clear), the H+0x68 buttons by the page's row of 0x49d24c (15
 * bytes per page) as 0x446e60 does; in any case byte H+0x10d[H+0x109] <- 1
 * and byte H+0xb0 <- 2. */
static int hdd_page(hud_ctx *c, uint32_t h, uint32_t p) {
    es2_sim_result r;
    memset(&r, 0, sizeof(r));
    if (p != hr32(c, h + 0x109u)) {
        uint32_t old = hr32(c, h + 0x109u);
        if (old != 0xffffffffu) {
            uint32_t b = hr32(c, h + 0x18u + 4u * old);
            if (hr8(c, b + 0x40u)) hw8(c, b + 0x40u, 0);
            if (widget_slot4(c, b, &r)) return 1;
            uint32_t page = hr32(c, h + 4u * hr32(c, h + 0x109u) + 0x101u);
            if (HVCALL(c, page, 8u, NULL, page)) return 1;
        }
        hw32(c, h + 0x109u, p);
        uint32_t page = hr32(c, h + 4u * p + 0x101u);
        if (HVCALL(c, page, 0xcu, NULL, page)) return 1;
        uint32_t b = hr32(c, h + 0x18u + 4u * hr32(c, h + 0x109u));
        if (!hr8(c, b + 0x40u)) {
            hw8(c, b + 0x40u, 1);
            if (widget_slot4(c, b, &r)) return 1;
        }
        uint32_t row = 0x0049d24cu + 15u * p;
        for (int32_t k = 0; (int32_t)hr32(c, h + 0x68u) > k; ++k, ++row) {
            uint32_t w = hr32(c, h + 0x18u + 4u * (uint32_t)k);
            uint32_t state = hsx16(hr16(c, w + 0x1bu));
            if (hr8(c, row) && state == 2u) {
                hw16(c, w + 0x1bu, 0);
                if (widget_slot4(c, w, &r)) return 1;
            } else if (!hr8(c, row) && state == 0) {
                hw16(c, w + 0x1bu, 2);
                if (widget_slot4(c, w, &r)) return 1;
            }
        }
    }
    hw8(c, h + hr32(c, h + 0x109u) + 0x10du, 1);
    hw8(c, h + 0xb0u, 2);
    return 0;
}
void es2_sim_service_hdd_page(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (hdd_page(&c, sc->slots[0], sc->slots[1])) { hud_failed(sc); return; }
}
/* 0x44cf10(M) / 0x44cf50(M) (listings 0x44cf10..0x44cf4c,
 * 0x44cf50..0x44cf8c): the map page's activate / deactivate: with H =
 * [[0x49d6b0]+8], the word +0x1b of [M+0x35], [H+0x4c] and [H+0x50] <- 0
 * (activate) or 2; with byte M+0x380 set the cursor 0x433e3c(2) or (0). */
void es2_sim_service_map_page_switch(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t m = sc->slots[0], h = hr32(&c, hr32(&c, 0x0049d6b0u) + 8u);
    uint16_t state = sc->va == 0x0044cf10u ? 0 : 2;
    hw16(&c, hr32(&c, m + 0x35u) + 0x1bu, state);
    hw16(&c, hr32(&c, h + 0x4cu) + 0x1bu, state);
    hw16(&c, hr32(&c, h + 0x50u) + 0x1bu, state);
    if (hr8(&c, m + 0x380u) && HCALL(&c, 0x00433e3cu, NULL, state ? 0u : 2u)) { hud_failed(sc); return; }
}
/* 0x450b88(S, k) (listing 0x450b88..0x450bf1): the status page's mode: S+0x7c
 * <- k, S+0x84 <- 0, S+0x88 <- -1, S+0x90 <- 0x13 (k 0), 0xc (k 1) or, k 2,
 * word [[S+0xec]+0x202]+8 (0 without S+0xec). 0x451dbc(S) (0x451dbc..0x451ddc),
 * the activate slot: 0x450b88(S, S+0x7c), S+0x80 <- -1. (The deactivate
 * slot 0x451eae is an empty body.) */
static void status_mode(hud_ctx *c, uint32_t s, uint32_t k) {
    hw32(c, s + 0x7cu, k);
    hw32(c, s + 0x84u, 0);
    hw32(c, s + 0x88u, 0xffffffffu);
    if (k == 0) hw32(c, s + 0x90u, 0x13u);
    else if (k == 1) hw32(c, s + 0x90u, 0xcu);
    else if (k == 2) {
        uint32_t t = hr32(c, s + 0xecu);
        hw32(c, s + 0x90u, t ? hsx16(hr16(c, hr32(c, t + 0x202u) + 8u)) : 0);
    }
}
void es2_sim_service_status_mode(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    status_mode(&c, sc->slots[0], sc->slots[1]);
    sc->eax = sc->slots[0];
}
void es2_sim_service_status_activate(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t s = sc->slots[0];
    status_mode(&c, s, hr32(&c, s + 0x7cu));
    hw32(&c, s + 0x80u, 0xffffffffu);
}
/* 0x4355a8(P, f) / 0x4355dc(P, f) (listings 0x4355a8..0x4355d7,
 * 0x4355dc..0x43560b): a port's start / end callback into P+0x4b9 / P+0x4c1
 * while its byte count P+0x4b7 / P+0x4b8 is below 2 (AL 1), else EAX 0. */
void es2_sim_service_port_callback(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t p = sc->slots[0], end = sc->va == 0x004355dcu;
    uint32_t count = p + (end ? 0x4b8u : 0x4b7u), table = p + (end ? 0x4c1u : 0x4b9u);
    uint8_t n = hr8(&c, count);
    if (n >= 2u) { sc->eax = 0; return; }
    hw8(&c, count, (uint8_t)(n + 1u));
    hw32(&c, table + 4u * n, sc->slots[1]);
    sc->eax = (p & 0xffffff00u) | 1u;
    sc->ecx = n;
    sc->edx = sc->slots[1];
}
/* 0x4311e0() / 0x431210() (listings 0x4311e0..0x43120e, 0x431210..0x43122b):
 * the panel's context stack: push: byte 0x49b106 + 1, 0x4d03ec[it] <- the
 * current context [0x6c5f70], 0x480d44 of the panel canvas [[0x49b088]+4]
 * (EAX it); pop: 0x480d44(0x4d03ec[byte 0x49b106]), the byte - 1. */
void es2_sim_service_canvas_push(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint8_t n = (uint8_t)(hr8(&c, 0x0049b106u) + 1u);
    hw8(&c, 0x0049b106u, n);
    hw32(&c, 0x004d03ecu + 4u * hsx8(n), hr32(&c, 0x006c5f70u));
    uint32_t canvas = hr32(&c, hr32(&c, VA_PANEL) + 4u);
    hud_gc_select(&c, canvas);
    sc->eax = canvas;
}
void es2_sim_service_canvas_pop(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    hud_gc_select(&c, hr32(&c, 0x004d03ecu + 4u * hsx8(hr8(&c, 0x0049b106u))));
    hw8(&c, 0x0049b106u, (uint8_t)(hr8(&c, 0x0049b106u) - 1u));
}
/* ======================================================================
 * The panel syncs cockpit_sync (hook) calls every logic frame.
 *
 * 0x4450f8(G, rec) (listing 0x4450f8..0x445126): G+0xb1 <- [rec], byte G+0xb0
 * <- 2 (redraw) when it changed. EAX G, EDX and ECX [rec]. */
void es2_sim_service_gauge_value(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t g = sc->slots[0], v = hr32(&c, sc->slots[1]);
    int changed = v != hr32(&c, g + 0xb1u);
    hw32(&c, g + 0xb1u, v);
    if (changed) hw8(&c, g + 0xb0u, 2);
    sc->eax = g; sc->edx = v; sc->ecx = v;
}
/* 0x434a24(a, v, w1, w2) (listing 0x434a24..0x434a68): the panel's nav
 * marker: H+0x26c <- a, H+0x270..+0x278 <- the three dwords of v (stored
 * +8, +0, +4), words H+0x27c <- w1, H+0x27e <- w2 (H = [0x49b088]). EAX
 * (H+0x270) with AX = w2, EDX H, ECX [v+4]. */
void es2_sim_service_nav_marker(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t h = hr32(&c, VA_PANEL), v = sc->slots[1];
    hw32(&c, h + 0x26cu, sc->slots[0]);
    hw32(&c, h + 0x278u, hr32(&c, v + 8u));
    hw32(&c, h + 0x270u, hr32(&c, v));
    hw32(&c, h + 0x274u, hr32(&c, v + 4u));
    hw16(&c, h + 0x27cu, (uint16_t)sc->slots[2]);
    hw16(&c, h + 0x27eu, (uint16_t)sc->slots[3]);
    sc->eax = ((h + 0x270u) & 0xffff0000u) | (uint16_t)sc->slots[3];
    sc->edx = h;
    sc->ecx = hr32(&c, v + 4u);
}
/* The HUD gadgets' slot +0xc: 0x43b420(D, d) (listing 0x43b420..0x43b45e):
 * word D+0x3a - d clamped to the words [D+0x24, D+0x26] (signed) into
 * D+0x3a; 0x43c3f8(D, d) (0x43c3f8..0x43c40a): words D+0x24 and D+0x26 + d. */
void es2_sim_service_gadget_step(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t d = sc->slots[0];
    uint16_t delta = (uint16_t)sc->slots[1];
    sc->eax = d;
    if (sc->va == 0x0043c3f8u) {
        hw16(&c, d + 0x24u, (uint16_t)(hr16(&c, d + 0x24u) + delta));
        hw16(&c, d + 0x26u, (uint16_t)(hr16(&c, d + 0x26u) + delta));
        sc->edx = sc->slots[1];
        return;
    }
    uint16_t v = (uint16_t)(hr16(&c, d + 0x3au) - delta);
    int16_t lo = (int16_t)hr16(&c, d + 0x24u), hi = (int16_t)hr16(&c, d + 0x26u);
    sc->ecx = (sc->ecx & 0xffff0000u) | v;
    if ((int16_t)v >= lo && (int16_t)v <= hi) hw16(&c, d + 0x3au, v);
    else if ((int16_t)v < lo) hw16(&c, d + 0x3au, (uint16_t)lo);
    else hw16(&c, d + 0x3au, (uint16_t)hi);
}
/* 0x43d9b4(G, rec) (listing 0x43d9b4..0x43dbae): the HUD record sync: word
 * rec+0 against G+0xb1 (the speed tape [G+0xf3]'s slot +0xc with new - old),
 * rec+2 against G+0xb3 (the heading [G+0xdf], old - new), rec+4 against
 * G+0xb5 (the pitch [G+0xe3], old - new), each stored first; then the 0x26
 * bytes of rec (the record's fields, in the original's order) into [G+0xe7]+0x14,
 * [G+0xeb]+0x14 and G+0xb1. */
static void record_copy(hud_ctx *c, uint32_t s, uint32_t t) {
    hw16(c, t, hr16(c, s));
    hw16(c, t + 2u, hr16(c, s + 2u));
    hw16(c, t + 4u, hr16(c, s + 4u));
    hw32(c, t + 6u, hr32(c, s + 6u));
    hw16(c, t + 0x16u, hr16(c, s + 0x16u));
    hw16(c, t + 0x18u, hr16(c, s + 0x18u));
    hw16(c, t + 0x1au, hr16(c, s + 0x1au));
    hw32(c, t + 0x1cu, hr32(c, s + 0x1cu));
    hw32(c, t + 0x20u, hr32(c, s + 0x20u));
    hw8(c, t + 0x24u, hr8(c, s + 0x24u));
    hw8(c, t + 0x25u, hr8(c, s + 0x25u));
    hw32(c, t + 0x12u, hr32(c, s + 0x12u));
    hw32(c, t + 0xau, hr32(c, s + 0xau));
    hw32(c, t + 0xeu, hr32(c, s + 0xeu));
}
void es2_sim_service_hud_sync(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t g = sc->slots[0], rec = sc->slots[1];
    static const struct { uint16_t field, gadget; int forward; } steps[3] = {{0xb1, 0xf3, 1}, {0xb3, 0xdf, 0}, {0xb5, 0xe3, 0}};
    for (unsigned k = 0; k < 3u; ++k) {
        uint16_t old = hr16(&c, g + steps[k].field), now = hr16(&c, rec + 2u * k);
        if (old == now) continue;
        hw16(&c, g + steps[k].field, now);
        uint32_t d = hr32(&c, g + steps[k].gadget);
        uint16_t delta = steps[k].forward ? (uint16_t)(now - old) : (uint16_t)(old - now);
        if (HVCALL(&c, d, 0xcu, NULL, d, delta)) { hud_failed(sc); return; }
    }
    record_copy(&c, rec, hr32(&c, g + 0xe7u) + 0x14u);
    record_copy(&c, rec, hr32(&c, g + 0xebu) + 0x14u);
    record_copy(&c, rec, g + 0xb1u);
    sc->eax = g + 0xb1u;
    sc->edx = g + 0xbbu;
    sc->ecx = hr32(&c, rec + 0xeu);
}
/* 0x443880(G, rec) (listing 0x443880..0x443906), the Shields gauge's record
 * set (from shield_balance, hook): changed = any of the dwords rec+0, +4,
 * +8 differs from G+0xb5, G+0xb9, G+0xbd; the 15 bytes of rec into G+0xb5;
 * before the sweep is done (byte G+0x8d = 0) G+0xcc, G+0xd0 <- G+0xb5, G+0xb9
 * and those two <- 0; changed: byte G+0xb0 <- 2. EAX G. */
void es2_sim_service_shields_record(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t g = sc->slots[0], rec = sc->slots[1];
    int changed = hr32(&c, rec) != hr32(&c, g + 0xb5u) || hr32(&c, rec + 4u) != hr32(&c, g + 0xb9u) || hr32(&c, rec + 8u) != hr32(&c, g + 0xbdu);
    for (uint32_t i = 0; i < 15u; ++i) hw8(&c, g + 0xb5u + i, hr8(&c, rec + i));
    sc->eax = g; sc->edx = rec; sc->ecx = 0;
    if (!hr8(&c, g + 0x8du)) {
        hw32(&c, g + 0xccu, hr32(&c, g + 0xb5u));
        sc->ecx = hr32(&c, g + 0xb9u);
        hw32(&c, g + 0xd0u, sc->ecx);
        hw32(&c, g + 0xb9u, 0);
        hw32(&c, g + 0xb5u, 0);
        sc->edx = 0;
    }
    if (changed) hw8(&c, g + 0xb0u, 2);
}/* 0x438ddc(G) (listing 0x438ddc..0x438df7), the power-on start: byte
 * G+0x8c <- 1, G+0x90 <- the clock (site 0x438dea). EAX the clock. */
void es2_sim_service_power_on_start(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t g = sc->slots[0], now = 0;
    hw8(&c, g + 0x8cu, 1);
    if (hud_clock(&c, 0x00438deau, &now)) { hud_failed(sc); return; }
    hw32(&c, g + 0x90u, now);
    sc->eax = now;
}
/* 0x4419b0(G) (listing 0x4419b0..0x4419be): byte G+0xb0 <- 2 (a full
 * redraw); EAX G. */
void es2_sim_service_gauge_redraw(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    hw8(&c, sc->slots[0] + 0xb0u, 2);
    sc->eax = sc->slots[0];
}
