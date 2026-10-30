/* The weapon gauges of the cockpit panel (es2/sim/hud.h): their factories (each
 * weapon's slot +0x64 during the construction), the gauge and widget
 * constructors, and the sync and slider methods the factories and the per-frame
 * weapon syncs call. State yes, pixels no. Every function names its listing
 * range of the reference DBSIM.EXE (llvm-objdump -d -M intel, SHA-256
 * 8561aa47...c4ad); hud.md, "the HUD line in the own process" has the contract. */
#include "hud_internal.h"

#define VA_PANEL 0x0049b088u
#define VA_LAYOUT 0x0049b0a8u

static unsigned shx(const hud_ctx *c) { return hr16(c, 0x004d253au) & 31u; }
static unsigned shy(const hud_ctx *c) { return hr16(c, 0x004d253cu) & 31u; }
static uint16_t shl16(uint16_t v, unsigned s) { return (uint16_t)((uint32_t)v << s); }
static void widget_init(hud_ctx *c, uint32_t w, uint32_t rect) {
    hw8(c, w + 0x10u, 0);
    hw32(c, w + 0x17u, 0x0049dbb6u);
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, w + i, hr32(c, rect + i));
    hw8(c, w + 0x1fu, 0);
}
static void widget_add(hud_ctx *c, uint32_t root, uint32_t w) {
    uint16_t n = hr16(c, root + 0x254u);
    hw32(c, root + 0x256u + 4u * hsx16(n), w);
    hw16(c, root + 0x254u, (uint16_t)(n + 1u));
}
static void region_ctor(hud_ctx *c, uint32_t o) {
    hw32(c, o, 0x004a69f0u);
    hw32(c, o, 0x004a3368u);
    hud_counter(c, 1);
    hw32(c, o, 0x004a3390u);
    hud_counter(c, 1);
    hw32(c, o, 0x004a3264u);
    hw32(c, o + 4u, 0);
    hud_counter(c, 1);
}
static int load_bitmap(hud_ctx *c, uint32_t name, uint32_t ext_h, uint32_t ext_d, uint32_t out) {
    uint32_t path = 0, x = 0;
    if (HCALL(c, HUD_VA_PATH_DIR, &path, name, hr8(c, 0x004d256cu) ? ext_h : ext_d) || HCALL(c, HUD_VA_LOAD_BY_NAME, &x, path)) return 1;
    hw32(c, out, x);
    return 0;
}
/* The label helpers of hud_cockpit.c (0x4387ac, 0x438884) through the
 * dispatch would need rows; they are repeated here as calls of the same own
 * code through two small rows instead: see es2_sim_service_label_init and
 * es2_sim_service_label_in_rect below. */
static int label_new(hud_ctx *c, uint32_t font, uint32_t off, uint32_t rect, uint32_t flags, uint32_t *out) {
    uint32_t l = 0;
    if (HCALL(c, HUD_VA_NEW, &l, 0x21u)) return 1;
    if (!l) return hud_null_refuse(c, 0x004387c3u, "label", "operator new returned NULL (the original writes through it)");
    if (HCALL(c, 0x004387acu, NULL, l, font, 1u) || HCALL(c, 0x00438884u, NULL, l, off, rect, flags)) return 1;
    *out = l;
    return 0;
}

/* ======================================================================
 * 0x40e18c(W) (listing 0x40e18c..0x40e208): the weapon's name into
 * 0x4a98d0: the table name [0x4989c8 + 4 word [[W+0x20]+2]] when W+0x20 is
 * set and its word +0 is 0, else the type's name [[W+0x1c]+0x52]. EAX
 * 0x4a98d0. */
static uint32_t weapon_name(hud_ctx *c, uint32_t w) {
    uint32_t a = hr32(c, w + 0x20u), src;
    if (a && !hr16(c, a)) src = hr32(c, 0x004989c8u + 4u * hsx16(hr16(c, a + 2u)));
    else src = hr32(c, hr32(c, w + 0x1cu) + 0x52u);
    hud_gstrcpy(c, 0x004a98d0u, src);
    return 0x004a98d0u;
}
void es2_sim_service_weapon_name(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    sc->eax = weapon_name(&c, sc->slots[0]);
}

/* 0x440834(G, rect, H, W) (listing 0x440834..0x440a39): the weapon gauge
 * base: 0x438b20 (as hud_cockpit.c), [G] <- 0x49c95c; the bitmaps
 * "pweapons" ([0x49c4d0]) and "wpn_dmg" ([0x49c4d4]) once, with the damage
 * animation's two frame lists (the word pairs at 0x49c4c8/0x49c4cc: |hi -
 * lo| + 1 frames from lo stepping toward hi, each {frame, 1}) and the list
 * header 0x4d1a54 = {2, lists}; G+0xbd <- W, bytes G+0xb1, G+0xc1 <- 0; the
 * damage animation new(0x2a) -> 0x471dac(A, [0x49c4d4], 0x4d1a54) into
 * G+0x6c; word G+0x74 <- 0, the blink periods G+0x7c <- 0x78, G+0x80 <-
 * 0x168, G+0x84 <- 0xb4, G+0x88 <- 0x708; byte G+0x8e <- 0. */
static void gauge_base(hud_ctx *c, uint32_t g, uint32_t rect, uint32_t h) {
    hw32(c, g, 0x0049bc6cu);
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, g + 4u + i, hr32(c, rect + i));
    hw32(c, g + 0x14u, h);
    hw8(c, g + 0xb0u, 0);
    hw16(c, g + 0x74u, 0);
    hw8(c, g + 0x76u, 0);
    hw8(c, g + 0x77u, 0);
    hw32(c, g + 0x6cu, 0);
    hw32(c, g + 0x78u, 0);
    hw8(c, g + 0x8cu, 0);
    hw8(c, g + 0x8du, 0);
    hw32(c, g + 0x90u, 0);
    hw32(c, g + 0x7cu, 0xb4u);
    hw32(c, g + 0x80u, 0x168u);
    hw32(c, g + 0x84u, 0xb4u);
    hw32(c, g + 0x88u, 0x168u);
    hw16(c, g + 0x70u, 0);
    hw16(c, g + 0x72u, 1);
}
static int weapon_gauge_base(hud_ctx *c, uint32_t g, uint32_t rect, uint32_t h, uint32_t w) {
    gauge_base(c, g, rect, h);
    hw32(c, g, 0x0049c95cu);
    if (!hr32(c, 0x0049c4d0u) && load_bitmap(c, 0x0049c728u, 0x0049c731u, 0x0049c735u, 0x0049c4d0u)) return 1;
    hw32(c, g + 0xbdu, w);
    hw8(c, g + 0xb1u, 0);
    hw8(c, g + 0xc1u, 0);
    if (!hr32(c, 0x0049c4d4u)) {
        if (load_bitmap(c, 0x0049c739u, 0x0049c741u, 0x0049c745u, 0x0049c4d4u)) return 1;
        uint32_t lists = 0, frames = 0, total = 0;
        if (HCALL(c, HUD_VA_NEW_ARRAY, &lists, 0x10u)) return 1;
        for (uint32_t k = 0; k < 2u; ++k) {
            int32_t d = (int32_t)hsx16(hr16(c, 0x0049c4ccu + 2u * k)) - (int32_t)hsx16(hr16(c, 0x0049c4c8u + 2u * k));
            hw32(c, lists + 8u * k, hr32(c, lists + 8u * k) + (uint32_t)((d < 0 ? -d : d) + 1));
            total += hr32(c, lists + 8u * k);
        }
        if (HCALL(c, HUD_VA_NEW_ARRAY, &frames, total << 3)) return 1;
        uint32_t e = frames;
        for (uint32_t k = 0; k < 2u; ++k) {
            int32_t lo = (int32_t)hsx16(hr16(c, 0x0049c4c8u + 2u * k)), hi = (int32_t)hsx16(hr16(c, 0x0049c4ccu + 2u * k));
            int32_t step = lo >= hi ? -1 : 1, v = lo;
            hw32(c, lists + 8u * k + 4u, e);
            for (uint32_t j = 0; j < hr32(c, lists + 8u * k); ++j, e += 8u, v += step) {
                hw32(c, e, (uint32_t)v);
                hw32(c, e + 4u, 1);
            }
        }
        hw32(c, 0x004d1a54u, 2);
        hw32(c, 0x004d1a58u, lists);
    }
    uint32_t a = 0;
    if (HCALL(c, HUD_VA_NEW, &a, 0x2au)) return 1;
    if (a) {
        uint32_t frames = 0x004d1a54u;
        hw32(c, a + 0x1eu, hr32(c, 0x0049c4d4u));
        hw32(c, a + 0x16u, frames);
        hw32(c, a + 0x1au, 0);
        hw16(c, a, 0);
        hw32(c, a + 0x12u, hr32(c, frames + 4u));
        hw16(c, a + 4u, 0);
        hw16(c, a + 2u, 0);
        hw32(c, a + 6u, 0);
    }
    hw32(c, g + 0x6cu, a);
    hw16(c, g + 0x74u, 0);
    hw32(c, g + 0x7cu, 0x78u);
    hw32(c, g + 0x80u, 0x168u);
    hw32(c, g + 0x84u, 0xb4u);
    hw32(c, g + 0x88u, 0x708u);
    hw8(c, g + 0x8eu, 0);
    return 0;
}

/* The weapon toggle: 0x442204(B, rect, G, slot, flag) (listing
 * 0x442204..0x4423b9): 0x4524a0; B+0x17 <- 0x49defc, word B+0x1d <- 0, B+0x20
 * <- 0x49dea0, B+0x24 <- G, B+0x17 <- 0x49debc, B+0x20 <- 0x49dee0, B+0x28
 * <- slot (sign-extended), word B+0x1b <- 0, counter + 1, B+0x17 <-
 * 0x49c81c, B+0x20 <- 0x49c840; the number label (font [0x49b0d4]) into
 * B+0x40 at (x1 - (5 << sx), y0, x1 - (2 << sx), y0 + (5 << sy)) with flag,
 * else (x0 + (2 << sx), y0, x0 + (5 << sx), y0 + (5 << sy)), 0x438884 flags
 * 2; byte B+0x44 <- '0' + (slot + 1) % 10, byte B+0x46 <- 0; counter + 1.
 * 0x4424b0(B, rect, G, slot, flag) (0x4424b0..0x4426e4): 0x442204; B+0x17
 * <- 0x49c7dc, B+0x20 <- 0x49c800; the two rectangles B+0x46 (the button
 * face) and B+0x56 (the name) right-aligned with flag, else left-aligned
 * (offsets 6/9 and 0xb/0x23 << sx, heights 7 and 5 << sy); the name label
 * (font [0x49b0d8]) into B+0x2c in B+0x56 (flags 1), L+0x1d <- 0x2e; byte
 * B+0x66 <- flag; counter + 1. */
static int weapon_toggle(hud_ctx *c, uint32_t b, uint32_t rect, uint32_t g, uint16_t slot, uint8_t flag) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x48u);
    if (!f) return hud_refuse(c, 0x00442204u, "weapon toggle", "the own code's call area is exhausted");
    f += 0x48u;
    unsigned sx = shx(c), sy = shy(c);
    widget_init(c, b, rect);
    hw32(c, b + 0x17u, 0x0049defcu);
    hw16(c, b + 0x1du, 0);
    hw32(c, b + 0x20u, 0x0049dea0u);
    hw32(c, b + 0x24u, g);
    hw32(c, b + 0x17u, 0x0049debcu);
    hw32(c, b + 0x20u, 0x0049dee0u);
    hw32(c, b + 0x28u, hsx16(slot));
    hw16(c, b + 0x1bu, 0);
    hud_counter(c, 1);
    hw32(c, b + 0x17u, 0x0049c81cu);
    hw32(c, b + 0x20u, 0x0049c840u);
    uint32_t r = f - 0x48u, x0 = hr32(c, rect), y0 = hr32(c, rect + 4u), x1 = hr32(c, rect + 8u), l = 0;
    if (flag) {
        hw32(c, r, x1 - (5u << sx));
        hw32(c, r + 8u, x1 - (2u << sx));
    } else {
        hw32(c, r, x0 + (2u << sx));
        hw32(c, r + 8u, x0 + (5u << sx));
    }
    hw32(c, r + 4u, y0 + (0u << sy));
    hw32(c, r + 12u, y0 + (5u << sy));
    hw32(c, f - 0x38u, 0);
    hw32(c, f - 0x34u, 0);
    if (label_new(c, hr32(c, 0x0049b0d4u), f - 0x38u, r, 2u, &l)) return 1;
    hw32(c, b + 0x40u, l);
    hw8(c, b + 0x44u, (uint8_t)(((int32_t)hsx16(slot) + 1) % 10 + 0x30));
    hw8(c, b + 0x46u, 0);
    hud_counter(c, 1);
    hud_frame_pop(c, top);
    return 0;
}
static int weapon_button(hud_ctx *c, uint32_t b, uint32_t rect, uint32_t g, uint16_t slot, uint8_t flag) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x2cu);
    if (!f) return hud_refuse(c, 0x004424b0u, "weapon button", "the own code's call area is exhausted");
    f += 0x2cu;
    unsigned sx = shx(c), sy = shy(c);
    if (weapon_toggle(c, b, rect, g, slot, flag)) return 1;
    hw32(c, b + 0x17u, 0x0049c7dcu);
    hw32(c, b + 0x20u, 0x0049c800u);
    uint32_t x0 = hr32(c, rect), y0 = hr32(c, rect + 4u), x1 = hr32(c, rect + 8u);
    if (flag) {
        hw32(c, b + 0x46u, x1 - (9u << sx));
        hw32(c, b + 0x4eu, x1 - (6u << sx));
        hw32(c, b + 0x56u, x1 - (0x23u << sx));
        hw32(c, b + 0x5eu, x1 - (0xbu << sx));
    } else {
        hw32(c, b + 0x46u, x0 + (6u << sx));
        hw32(c, b + 0x4eu, x0 + (9u << sx));
        hw32(c, b + 0x56u, x0 + (0xbu << sx));
        hw32(c, b + 0x5eu, x0 + (0x23u << sx));
    }
    hw32(c, b + 0x4au, y0 + (0u << sy));
    hw32(c, b + 0x52u, y0 + (7u << sy));
    hw32(c, b + 0x5au, y0 + (0u << sy));
    hw32(c, b + 0x62u, y0 + (5u << sy));
    uint32_t l = 0;
    hw32(c, f - 0x2cu, 0);
    hw32(c, f - 0x28u, 0);
    if (label_new(c, hr32(c, 0x0049b0d8u), f - 0x2cu, b + 0x56u, 1u, &l)) return 1;
    hw32(c, b + 0x2cu, l);
    hw32(c, hr32(c, b + 0x2cu) + 0x1du, 0x2eu);
    hw8(c, b + 0x66u, flag);
    hud_counter(c, 1);
    hud_frame_pop(c, top);
    return 0;
}

/* 0x442978(S, rect, G, kind) (listing 0x442978..0x442b5e): the power
 * slider: 0x4524d0(S, rect, 0, 0x100, 0, 1); S+0x17 <- 0x49df40 and its
 * slot +0x1c; the event base S+0x3e (0x49dea0, S+0x42 <- G, 0x49df24) with
 * the region at S+0x56, counter + 1; S+0x17/S+0x3e <- 0x49df6c/0x49df94, word
 * S+0x1b <- 0, counter + 1, then 0x49c798/0x49c7c0; the knob word S+0x2c <-
 * 1 << sx, S+0x2e = (x0, y0 + (1 << sy)), S+0x36 = (x0 + (1 << sx), y1), S+0x6e
 * <- the corner, the region rectangle S+0x5e <- the knob; the bar new(0x34)
 * -> 0x4395c4(B, the rect with y0 + (1 << sy), 0x400) and, kind 0, B+0x2c <-
 * 0x20, B+0x30 <- 0x22, B+0x24 <- 0x2e; S+0x46 <- B; S+0x4a, S+0x7a <- 0;
 * words S+0x2a, S+0x28 <- the rect's x0, S+0x26 <- 0x400; the slot +0x1c;
 * counter + 1. A NULL bar with kind 0 (written through) is refused. */
static int power_slider(hud_ctx *c, uint32_t s, uint32_t rect, uint32_t g, uint32_t kind) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x34u);
    if (!f) return hud_refuse(c, 0x00442978u, "power slider", "the own code's call area is exhausted");
    f += 0x34u;
    unsigned sx = shx(c), sy = shy(c);
    widget_init(c, s, rect);
    hw32(c, s + 0x17u, 0x0049db8au);
    hw32(c, s + 0x20u, 0);
    hw16(c, s + 0x24u, 0);
    hw16(c, s + 0x26u, 0x100u);
    hw16(c, s + 0x2au, 0);
    hw16(c, s + 0x28u, 0);
    hw16(c, s + 0x2cu, 1);
    hw16(c, s + 0x1du, 1);
    hw32(c, s + 0x17u, 0x0049df40u);
    if (HVCALL(c, s + 0x17u, 0x1cu, NULL, s)) return 1;
    uint32_t e = s + 0x3eu;
    hw32(c, e, 0x0049dea0u);
    hw32(c, e + 4u, g);
    hw32(c, e, 0x0049df24u);
    region_ctor(c, e + 0x18u);
    hud_counter(c, 1);
    hw32(c, s + 0x17u, 0x0049df6cu);
    hw32(c, s + 0x3eu, 0x0049df94u);
    hw16(c, s + 0x1bu, 0);
    hud_counter(c, 1);
    hw32(c, s + 0x17u, 0x0049c798u);
    hw32(c, s + 0x3eu, 0x0049c7c0u);
    uint32_t r = f - 0x34u;
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, r + i, hr32(c, rect + i));
    hw32(c, r + 4u, hr32(c, r + 4u) + (1u << sy));
    hw16(c, s + 0x2cu, (uint16_t)(1u << sx));
    hw32(c, s + 0x36u, hr32(c, r));
    hw32(c, s + 0x3au, hr32(c, r + 4u));
    hw32(c, s + 0x2eu, hr32(c, s + 0x36u));
    hw32(c, s + 0x32u, hr32(c, s + 0x3au));
    hw32(c, s + 0x36u, hr32(c, s + 0x36u) + hsx16(hr16(c, s + 0x2cu)));
    hw32(c, s + 0x3au, hr32(c, r + 12u));
    hw32(c, s + 0x6eu, hr32(c, s + 0x2eu));
    hw32(c, s + 0x72u, hr32(c, s + 0x32u));
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, s + 0x5eu + i, hr32(c, s + 0x2eu + i));
    uint32_t b = 0;
    if (HCALL(c, HUD_VA_NEW, &b, 0x34u)) return 1;
    if (b) {
        if (HCALL(c, 0x004395c4u, NULL, b, r, 0x400u)) return 1;
    }
    if (kind < 1u) {
        if (!b) return hud_null_refuse(c, 0x00442b0bu, "power slider", "operator new returned NULL (the original writes through it)");
        hw32(c, b + 0x2cu, 0x20u);
        hw32(c, b + 0x30u, 0x22u);
        hw32(c, b + 0x24u, 0x2eu);
    }
    hw32(c, s + 0x46u, b);
    hw32(c, s + 0x4au, 0);
    hw32(c, s + 0x7au, 0);
    uint16_t v = hr16(c, r);
    hw16(c, s + 0x2au, v);
    hw16(c, s + 0x28u, v);
    hw16(c, s + 0x26u, 0x400u);
    if (HVCALL(c, s + 0x17u, 0x1cu, NULL, s)) return 1;
    hud_counter(c, 1);
    hud_frame_pop(c, top);
    return 0;
}

/* 0x440a90(G, rect, H, slot, W) (listing 0x440a90..0x440c8e): the energy
 * weapon gauge: 0x440834; [G] <- 0x49c93c, word G+0xd9 <- slot, [G] <-
 * 0x49c91c; flag = (1 << slot) & byte [rect+0x40] (right-aligned); the
 * button new(0x67) -> 0x4424b0, counter - 3, added to the root, G+0x18;
 * _strncpy(G+0xb1, 0x40e18c(W), 0xc); the power slider new(0x7e) ->
 * 0x442978(S, rect', G, [rect+0x3c]) with rect' = (x1 - (0x35 << sx), y0,
 * x1 - (0x24 << sx), y0 + (5 << sy)) (flag) or (x0 + (0x24 << sx), y0, x0 +
 * (0x35 << sx), y0 + (5 << sy)), counter - 6, G+0x1c, added; G+0x68 <- 2;
 * G+0xc6 <- 0x400, then _memset(G+0xc2, 0, 0x17) (which clears it again, as
 * the original). EAX G. 0x440fa0(G, rect, H, slot, W) (0x440fa0..0x4411d9):
 * the projectile gauge: 0x440834; [G] <- 0x49c93c, word G+0xd9 <- slot, [G]
 * <- 0x49c8fc; flag from byte [rect+0x30]; the ammunition rectangle G+0xdf
 * (the same two layouts); the button as above (G+0x18); the name; the
 * ammunition label (font [0x49b0d8], flags 4 with flag else 1) into G+0xdb,
 * L+0x1d <- 0x2e; G+0x68 <- 1; _memset(G+0xc2, 0, 0x17). EAX G. NULL
 * objects written through are refused. */
static void side_rect(hud_ctx *c, uint32_t dst, uint32_t rect, uint8_t flag) {
    unsigned sx = shx(c), sy = shy(c);
    uint32_t x0 = hr32(c, rect), y0 = hr32(c, rect + 4u), x1 = hr32(c, rect + 8u);
    hw32(c, dst, flag ? x1 - (0x35u << sx) : x0 + (0x24u << sx));
    hw32(c, dst + 4u, y0 + (0u << sy));
    hw32(c, dst + 8u, flag ? x1 - (0x24u << sx) : x0 + (0x35u << sx));
    hw32(c, dst + 12u, y0 + (5u << sy));
}
static int gauge_button(hud_ctx *c, uint32_t g, uint32_t rect, uint16_t slot, uint8_t flag, uint32_t root) {
    uint32_t b = 0;
    if (HCALL(c, HUD_VA_NEW, &b, 0x67u)) return 1;
    if (b) {
        if (weapon_button(c, b, rect, g, slot, flag)) return 1;
        hud_counter(c, -3);
    }
    widget_add(c, root, b);
    hw32(c, g + 0x18u, b);
    return 0;
}
static int energy_gauge(hud_ctx *c, uint32_t g, uint32_t rect, uint32_t h, uint16_t slot, uint32_t w) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x48u);
    if (!f) return hud_refuse(c, 0x00440a90u, "energy gauge", "the own code's call area is exhausted");
    f += 0x48u;
    if (weapon_gauge_base(c, g, rect, h, w)) return 1;
    hw32(c, g, 0x0049c93cu);
    hw16(c, g + 0xd9u, slot);
    hw32(c, g, 0x0049c91cu);
    uint32_t root = hr32(c, hr32(c, VA_PANEL));
    uint8_t flag = (uint8_t)((uint8_t)(1u << (slot & 31u)) & hr8(c, rect + 0x40u));
    if (gauge_button(c, g, rect, slot, flag, root)) return 1;
    if (HCALL(c, HUD_VA_STRNCPY, NULL, g + 0xb1u, weapon_name(c, w), 0xcu)) return 1;
    side_rect(c, f - 0x48u, rect, flag);
    uint32_t s = 0;
    if (HCALL(c, HUD_VA_NEW, &s, 0x7eu)) return 1;
    if (s) {
        if (power_slider(c, s, f - 0x48u, g, hr32(c, rect + 0x3cu))) return 1;
        hud_counter(c, -6);
    }
    hw32(c, g + 0x1cu, s);
    widget_add(c, root, s);
    hw32(c, g + 0x68u, 2);
    hw32(c, g + 0xc6u, 0x400u);
    if (HCALL(c, HUD_VA_MEMSET, NULL, g + 0xc2u, 0u, 0x17u)) return 1;
    hud_frame_pop(c, top);
    return 0;
}
static int projectile_gauge(hud_ctx *c, uint32_t g, uint32_t rect, uint32_t h, uint16_t slot, uint32_t w) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x34u);
    if (!f) return hud_refuse(c, 0x00440fa0u, "projectile gauge", "the own code's call area is exhausted");
    f += 0x34u;
    if (weapon_gauge_base(c, g, rect, h, w)) return 1;
    hw32(c, g, 0x0049c93cu);
    hw16(c, g + 0xd9u, slot);
    hw32(c, g, 0x0049c8fcu);
    uint32_t root = hr32(c, hr32(c, VA_PANEL)), font = hr32(c, 0x0049b0d8u);
    uint8_t flag = (uint8_t)((uint8_t)(1u << (slot & 31u)) & hr8(c, rect + 0x30u));
    side_rect(c, g + 0xdfu, rect, flag);
    if (gauge_button(c, g, rect, slot, flag, root)) return 1;
    if (HCALL(c, HUD_VA_STRNCPY, NULL, g + 0xb1u, weapon_name(c, w), 0xcu)) return 1;
    hw32(c, f - 0x30u, 0);
    hw32(c, f - 0x34u, 0);
    uint32_t l = 0;
    if (label_new(c, font, f - 0x34u, g + 0xdfu, flag ? 4u : 1u, &l)) return 1;
    hw32(c, l + 0x1du, 0x2eu);
    hw32(c, g + 0xdbu, l);
    hw32(c, g + 0x68u, 1);
    if (HCALL(c, HUD_VA_MEMSET, NULL, g + 0xc2u, 0u, 0x17u)) return 1;
    hud_frame_pop(c, top);
    return 0;
}

/* 0x432018(H, G, slot) (listing 0x432018..0x432070): the gauge filed into
 * the panel: slot -1 (word) takes the first free entry of H+0x70[10] (none
 * free: it stays -1 and the store lands on H+0x6c, as the original); H+0x1d8
 * <- G when 0; the element list [H + 8 + 4 [H+0x6c]] <- G; H+0x70[slot] <- G;
 * H+0x6c + 1, H+0x98 + 1. */
static void gauge_file(hud_ctx *c, uint32_t h, uint32_t g, uint16_t slot) {
    if (slot == 0xffffu)
        for (uint32_t i = 0; i < 10u; ++i)
            if (!hr32(c, h + 0x70u + 4u * i)) { slot = (uint16_t)i; break; }
    if (!hr32(c, h + 0x1d8u)) hw32(c, h + 0x1d8u, g);
    hw32(c, h + 8u + 4u * hr32(c, h + 0x6cu), g);
    hw32(c, h + 0x70u + 4u * hsx16(slot), g);
    hw32(c, h + 0x6cu, hr32(c, h + 0x6cu) + 1u);
    hw32(c, h + 0x98u, hr32(c, h + 0x98u) + 1u);
}
/* 0x432074 / 0x432124 (H, slot, W) (listings 0x432074..0x432122,
 * 0x432124..0x4321d2): the layout rectangle L+0x14+16 slot (L = [0x49b0a8])
 * to L+0x104 / L+0xb4; new(0xdf) -> 0x440a90 or new(0xef) -> 0x440fa0 (G, that
 * rect, H, slot, W) when not NULL; 0x432018(H, G, slot). EAX G. */
static int gauge_factory(hud_ctx *c, uint32_t h, uint16_t slot, uint32_t w, int energy, uint32_t *out) {
    uint32_t l = hr32(c, VA_LAYOUT), dst = l + (energy ? 0x104u : 0xb4u), src = l + 0x14u + (hsx16(slot) << 4), g = 0;
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, dst + i, hr32(c, src + i));
    if (HCALL(c, HUD_VA_NEW, &g, energy ? 0xdfu : 0xefu)) return 1;
    if (g && (energy ? energy_gauge(c, g, dst, h, slot, w) : projectile_gauge(c, g, dst, h, slot, w))) return 1;
    gauge_file(c, h, g, slot);
    *out = g;
    return 0;
}
/* 0x440e9c(G) (listing 0x440e9c..0x440ea1): EAX G+0xc2 (the sync record).
 * 0x40e0e0(W) (listing 0x40e0e0..0x40e13e), the ENERGY_WPN/ELF_WPN slot
 * +0x64: G = 0x432074([0x49b088], byte [[W+0x18]+7], W), W+0x77 <- G; the
 * record copied (23 bytes) from G+0xc2 with dword +4 <- word [W+0x7b]; G's
 * slot +0x10 (G, record) (the sync 0x440d74). 0x40e20c(W)
 * (0x40e20c..0x40e232): W+0x77 <- 0x432124(...). */
void es2_sim_service_energy_gauge_new(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t w = sc->slots[0], g = 0;
    uint32_t top = c.l->stack_hi, f = hud_frame_push(&c, 0x18u);
    if (!f) { hud_refuse(&c, 0x0040e0e0u, "energy gauge", "the own code's call area is exhausted"); hud_failed(sc); return; }
    f += 0x18u;
    if (gauge_factory(&c, hr32(&c, VA_PANEL), (uint16_t)hsx8(hr8(&c, hr32(&c, w + 0x18u) + 7u)), w, 1, &g)) { hud_failed(sc); return; }
    hw32(&c, w + 0x77u, g);
    for (uint32_t i = 0; i < 0x17u; ++i) hw8(&c, f - 0x18u + i, hr8(&c, g + 0xc2u + i));
    hw32(&c, f - 0x14u, hsx16(hr16(&c, w + 0x7bu)));
    if (HVCALL(&c, hr32(&c, w + 0x77u), 0x10u, NULL, hr32(&c, w + 0x77u), f - 0x18u)) { hud_failed(sc); return; }
    hud_frame_pop(&c, top);
}
void es2_sim_service_projectile_gauge_new(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t w = sc->slots[0], g = 0;
    if (gauge_factory(&c, hr32(&c, VA_PANEL), (uint16_t)hsx8(hr8(&c, hr32(&c, w + 0x18u) + 7u)), w, 0, &g)) { hud_failed(sc); return; }
    hw32(&c, w + 0x77u, g);
    sc->eax = g;
}

/* ======================================================================
 * The gauges' per-frame sync and the slider methods.
 *
 * 0x440d74(G, rec) (listing 0x440d74..0x440e99), the energy gauge's slot
 * +0x10: rec+4 differs from G+0xc6: G+0xc6 <- it and the slider's slot +0x14
 * ([G+0x1c], value), redraw-the-slider; rec+0 differs from G+0xc2: G+0xdb <-
 * it unless the sweep byte G+0x8d is set, G+0xc2 <- it, [G+0x1c]+0x7a <- it,
 * redraw-the-slider; byte rec+0x11 differs from G+0xd3 or byte G+0xc1 is
 * set: with 0x42d870's record word +4 nonzero byte G+0xc1 <- 1, else G+0xd3
 * <- the byte, the state words +0x1b of the button [G+0x18] and the slider
 * <- 2 (byte set) or 0, byte G+0xc1 <- 0, redraw-the-button; bytes rec+0x14,
 * +0x15, +0x16 into G+0xd6..0xd8 (redraw-the-button on a change); then the
 * button's slot +4 when redraw-the-button, the slider's slot +4 when
 * redraw-the-slider. 0x42d870() (0x42d870..0x42d898): byte 0x49add5 + 1
 * when 0, 0x4cfcbc <- 0, 0x4cfcc0 <- word [0x4cfa70]; EAX 0x4cfcbc. */
static uint32_t jolt_record(hud_ctx *c) {
    if (!hr8(c, 0x0049add5u)) hw8(c, 0x0049add5u, (uint8_t)(hr8(c, 0x0049add5u) + 1u));
    hw32(c, 0x004cfcbcu, 0);
    hw32(c, 0x004cfcc0u, hsx16(hr16(c, 0x004cfa70u)));
    return 0x004cfcbcu;
}
void es2_sim_service_jolt_record(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    sc->eax = jolt_record(&c);
    sc->edx = hr32(&c, 0x004cfcc0u);
}
static int slot4(hud_ctx *c, uint32_t w) {
    return hud_call(c, hr32(c, hr32(c, w + 0x17u) + 4u), (const uint32_t[]){w}, 1, NULL);
}
static int energy_sync(hud_ctx *c, uint32_t g, uint32_t rec) {
    int slider = 0, button = 0;
    uint32_t v = hr32(c, rec + 4u);
    if (v != hr32(c, g + 0xc6u)) {
        hw32(c, g + 0xc6u, v);
        uint32_t s = hr32(c, g + 0x1cu);
        if (hud_call(c, hr32(c, hr32(c, s + 0x17u) + 0x14u), (const uint32_t[]){s, v}, 2, NULL)) return 1;
        slider = 1;
    }
    v = hr32(c, rec);
    if (v != hr32(c, g + 0xc2u)) {
        if (!hr8(c, g + 0x8du)) hw32(c, g + 0xdbu, v);
        hw32(c, g + 0xc2u, hr32(c, rec));
        hw32(c, hr32(c, g + 0x1cu) + 0x7au, hr32(c, rec));
        slider = 1;
    }
    if (hr8(c, rec + 0x11u) != hr8(c, g + 0xd3u) || hr8(c, g + 0xc1u)) {
        if (hr32(c, jolt_record(c) + 4u)) {
            hw8(c, g + 0xc1u, 1);
        } else {
            hw8(c, g + 0xd3u, hr8(c, rec + 0x11u));
            uint16_t state = hr8(c, rec + 0x11u) ? 2 : 0;
            hw16(c, hr32(c, g + 0x18u) + 0x1bu, state);
            hw16(c, hr32(c, g + 0x1cu) + 0x1bu, state);
            hw8(c, g + 0xc1u, 0);
            button = 1;
        }
    }
    for (uint32_t k = 0; k < 3u; ++k)
        if (hr8(c, rec + 0x14u + k) != hr8(c, g + 0xd6u + k)) {
            hw8(c, g + 0xd6u + k, hr8(c, rec + 0x14u + k));
            button = 1;
        }
    if (button && slot4(c, hr32(c, g + 0x18u))) return 1;
    if (slider && slot4(c, hr32(c, g + 0x1cu))) return 1;
    return 0;
}
void es2_sim_service_energy_sync(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (energy_sync(&c, sc->slots[0], sc->slots[1])) { hud_failed(sc); return; }
}
/* 0x452584(S, v) / 0x45266c(S, v) (listings 0x452584..0x4525ce,
 * 0x45266c..0x4526ba), the sliders' slot +0x14: v clamped to the words
 * [S+0x24, S+0x26] (signed); word S+0x28 <- ((v - S+0x24) * S+0x20 >> 16) +
 * word S+0 (horizontal) or word S+0xc - that (vertical); the slot +8 (S,
 * v). 0x45256c(S) / 0x452650(S) (0x45256c..0x452583, 0x452650..0x452668), the
 * slot +0x10: AX <- ((S+0x2e - S+0) << 16) / S+0x20 + word S+0x24 (the
 * vertical one with S+0x32 and S+4); a zero divisor is refused. */
void es2_sim_service_slider_set(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t s = sc->slots[0];
    int16_t v = (int16_t)sc->slots[1], lo = (int16_t)hr16(&c, s + 0x24u), hi = (int16_t)hr16(&c, s + 0x26u);
    if (v < lo) v = lo;
    else if (v > hi) v = hi;
    int32_t d = (int32_t)(((int32_t)v - (int32_t)lo) * (int32_t)hr32(&c, s + 0x20u)) >> 16;
    if (sc->va == 0x00452584u) hw16(&c, s + 0x28u, (uint16_t)((uint16_t)d + hr16(&c, s)));
    else hw16(&c, s + 0x28u, (uint16_t)(hr16(&c, s + 0xcu) - (uint16_t)d));
    if (hud_call(&c, hr32(&c, hr32(&c, s + 0x17u) + 8u), (const uint32_t[]){s, (uint32_t)(int32_t)v}, 2, NULL)) { hud_failed(sc); return; }
}
void es2_sim_service_slider_value(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t s = sc->slots[0];
    int vertical = sc->va == 0x00452650u;
    int32_t div = (int32_t)hr32(&c, s + 0x20u);
    if (!div) { hud_divide_refuse(&c, vertical ? 0x00452660u : 0x0045257bu, 0, 0, "slider value", "a zero scale (the original's idiv faults)"); hud_failed(sc); return; }
    uint32_t num = (vertical ? hr32(&c, s + 0x32u) - hr32(&c, s + 4u) : hr32(&c, s + 0x2eu) - hr32(&c, s)) << 16;
    int32_t q = (int32_t)num / div;
    sc->eax = ((uint32_t)q & 0xffff0000u) | (uint16_t)((uint16_t)q + hr16(&c, s + 0x24u));
    sc->edx = (uint32_t)((int32_t)num % div);
    sc->ecx = s;
}
/* 0x43901c(S, v) (listing 0x43901c..0x439045), the bar sliders' slot +8:
 * the owner's slot 0 [[S+0x42]](owner, S, v), S's slot +4, the event base's
 * slot +8 [[S+0x3e]+8](S+0x3e). (0x439014 is an empty body.) */
void es2_sim_service_slider_moved(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t s = sc->slots[0], owner = hr32(&c, s + 0x42u);
    if (hud_call(&c, hr32(&c, hr32(&c, owner)), (const uint32_t[]){owner, s, sc->slots[1]}, 3, NULL) || slot4(&c, s) ||
        hud_call(&c, hr32(&c, hr32(&c, s + 0x3eu) + 8u), (const uint32_t[]){s + 0x3eu}, 1, NULL))
        hud_failed(sc);
}
/* 0x440f18(G, W, v) (listing 0x440f18..0x440f9d), the weapon gauges' slot 0:
 * on the first match of W in G+0x18[G+0x68] (the count reread and compared
 * signed each round), index 0 (the button): v & 2 group_toggle([[[0x49b088]
 * +0x203]+0x202], the word G+0xd9 in CX over G's high word) (hook), else
 * panel_weapon([G+0x14], G) (hook), EAX, EDX and ECX the callee's; index 1
 * (the slider): G+0xc6 <- v clamped signed to [0, 0x400], EAX the clamped
 * value; a later index: EAX = index - 1. Without a match EAX is the index
 * reached. Outside the calls EDX is the list cursor and ECX = G. */
void es2_sim_service_weapon_gauge_event(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t g = sc->slots[0], w = sc->slots[1], v = sc->slots[2], i = 0;
    sc->ecx = g;
    for (; (int32_t)i < (int32_t)hr32(&c, g + 0x68u); ++i) {
        if (w != hr32(&c, g + 0x18u + 4u * i)) continue;
        sc->edx = g + 0x18u + 4u * i;
        sc->eax = i - 1u;
        if (i == 0) {
            es2_sim_result r;
            int refused;
            if (v & 2u) {
                uint32_t n = (g & 0xffff0000u) | hr16(&c, g + 0xd9u);
                uint32_t list = hr32(&c, hr32(&c, hr32(&c, VA_PANEL) + 0x203u) + 0x202u);
                refused = hud_call_regs(&c, 0x004110acu, (const uint32_t[]){list, n}, 2, NULL, &r);
            } else {
                refused = hud_call_regs(&c, 0x00432a50u, (const uint32_t[]){hr32(&c, g + 0x14u), g}, 2, NULL, &r);
            }
            if (refused) { hud_failed(sc); return; }
            sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
        } else if (i == 1) {
            int32_t x = (int32_t)v >= 0x400 ? 0x400 : (int32_t)v;
            if (x <= 0) x = 0;
            hw32(&c, g + 0xc6u, (uint32_t)x);
            sc->eax = (uint32_t)x;
        }
        return;
    }
    sc->eax = i;
    sc->edx = g + 0x18u + 4u * i;
}
/* 0x4395c4(B, rect, n) (listing 0x4395c4..0x4395e7) with 0x4390c4: the
 * horizontal bar, as hud_cockpit.c's own copy (a row for the gauge files). */
void es2_sim_service_bar_horizontal(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t b = sc->slots[0], rect = sc->slots[1];
    hw32(&c, b, 0x0049bd78u);
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(&c, b + 4u + i, hr32(&c, rect + i));
    hw32(&c, b, 0x0049bd60u);
    int32_t d = (int32_t)hsx16((uint16_t)sc->slots[2]);
    if (!d) { hud_divide_refuse(&c, 0x004390eeu, 0, 0, "bar", "a zero step count (the original's idiv faults)"); hud_failed(sc); return; }
    hw32(&c, b + 0x14u, (uint32_t)((int32_t)((hr32(&c, rect + 8u) - hr32(&c, rect)) << 16) / d));
    hw32(&c, b + 0x18u, hr32(&c, rect));
    hw32(&c, b + 0x1cu, hr32(&c, rect + 8u));
    hw32(&c, b + 0x20u, (int32_t)hr32(&c, b + 0x14u) > 0 ? hr32(&c, b + 0x18u) : hr32(&c, b + 0x1cu));
    hw32(&c, b, 0x0049bd30u);
    sc->eax = b;
}

/* ======================================================================
 * The wingman radio records of the HDD (0x44a7e8, called by order_setup
 * per wingman).
 *
 * 0x4539f4(T) (listing 0x4539f4..0x453a0d): the first record of a string
 * table [T] + 4, 0 without [T]. 0x453a10(T, p) (0x453a10..0x453a4a): the next
 * record after p while p is below [T] + [T+8] (unsigned): p + word [p-2] + 1
 * + byte [p + word [p-2]] + 2 (sign-extended), else 0. 0x434240(x)
 * (0x434240..0x43425f): x / 3. 0x434260(x) (0x434260..0x43427c): x / 4 + 1
 * (toward zero), 3 -> 4. 0x434280(i) (0x434280..0x434297): the colour word
 * [0x4d3b7c + 2 word [0x49b040 + 2i]]. */
static uint32_t strings_first(hud_ctx *c, uint32_t t) {
    uint32_t p = hr32(c, t);
    return p ? p + 4u : 0;
}
static uint32_t strings_after(hud_ctx *c, uint32_t t, uint32_t p) {
    if (!(hr32(c, t) + hr32(c, t + 8u) > p)) return 0;
    uint32_t q = p + hsx16(hr16(c, p - 2u));
    return q + 1u + hsx8(hr8(c, q)) + 2u;
}
/* 0x44a7e8(H, i, x) (listing 0x44a7e8..0x44ae18): wingman i's radio
 * record R = H+0x12d+0x14e i. i = 0 with word [0x49d248] >= 0: x <- it; R+0x137
 * <- the x-th record of the pilot names [0x49d238]; word R+0x135 <- x / 3
 * (the pilot) into x; the pilot's texts 0x435970(DialogPort, 2, i, x / 4 +
 * 1 (3 -> 4)); the 27 animation offsets of "pilot<x>.ofs" (each a dword k
 * and 8 bytes into R+0x3d+8k; a missing file is the fatal report 0x44a905); the
 * pilot's portrait 0x45cdf4("pilot<x>" with [0x4a0a10]) into R+0x18 (NULL:
 * the fatal report 0x44a992 after the byte), byte R+0x133 <- 1; six labels in the radio button
 * [H+0x40+4i]'s rectangle (fonts [0x49b0b4]/[0x49b0ac] alternating, height
 * word font+0xa, rows at 4, 0x10, 0x18, 0x20, 0x28 << sy and the corner label
 * at the bottom left (0xa << sx by 0xa << sy) with word [0x4d3b9a]) into R+0
 * .. R+0x14, L0+0x1d <- 0x434280(i); R+0x13f, R+0x13b <- 0, bytes R+0x12e,
 * +0x147, +0x149, +0x148 <- 0; H+0x538+4i <- [0x4d03c8+4i], H+0x54c+4i <-
 * R+0x137; the page bytes 0x49d24c[i+10], 0x49d25b[i+10] <- 1; word
 * [H+0x18+4(i+10)]+0x1b <- 0; stream_destroy. */
static int radio_record(hud_ctx *c, uint32_t h, uint32_t i, uint16_t x) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0xacu);
    if (!f) return hud_refuse(c, 0x0044a7e8u, "radio record", "the own code's call area is exhausted");
    f += 0xacu;
    unsigned sx = shx(c), sy = shy(c);
    uint32_t r = h + 0x12du + 0x14eu * i;
    if (i == 0 && (int32_t)hr32(c, 0x0049d248u) >= 0) x = hr16(c, 0x0049d248u);
    uint32_t t = hr32(c, 0x0049d238u), p = strings_first(c, t);
    for (int32_t k = 0; k < (int32_t)hsx16(x); ++k) p = strings_after(c, t, p);
    hw32(c, r + 0x137u, p);
    uint16_t pilot = (uint16_t)((int32_t)hsx16(x) / 3);
    hw16(c, r + 0x135u, pilot);
    x = pilot;
    uint32_t panel = hr32(c, VA_PANEL), port = panel ? hr32(c, panel + 0x207u) : 0;
    int32_t q = (int32_t)hsx16(hr16(c, r + 0x135u)) / 4 + 1;
    if (q == 3) q = 4;
    if (HCALL(c, 0x00435970u, NULL, port, 2u, i, (uint32_t)q)) return 1;
    uint32_t name = f - 0x74u, s = f - 0x30u, path = 0;
    hud_gstrcpy(c, name, 0x0049d523u);
    if (HCALL(c, HUD_VA_ITOA, NULL, hsx16(x), f - 0x6fu, 10u)) return 1;
    if (HCALL(c, HUD_VA_PATH_DIR, &path, name, 0x0049d529u) || HCALL(c, HUD_VA_VOL_STREAM, NULL, s, path)) return 1;
    if (hr32(c, f - 0x2cu) && hud_fatal_report(c, 0, 0x0049d52du, 0x3eeu, 0x0049d536u)) return 1;   /* 0x44a905 */
    for (int32_t k = 0; k < 0x1b; ++k) {
        if (hud_sread(c, s, 4u, f - 0x7cu, NULL)) return 1;
        if (HCALL(c, HUD_VA_STREAM_READ, NULL, s, 8u, (hr32(c, f - 0x7cu) << 3) + r + 0x3du)) return 1;
    }
    uint32_t pic = 0;
    if (HCALL(c, HUD_VA_PATH_DIR, &path, name, hr32(c, 0x004a0a10u)) || HCALL(c, HUD_VA_LOAD_BY_NAME, &pic, path)) return 1;
    hw32(c, r + 0x18u, pic);
    hw8(c, r + 0x133u, 1);
    if (!hr32(c, r + 0x18u) && hud_fatal_report(c, hr32(c, r + 0x18u), 0x0049d55au, 0x406u, 0x0049d563u)) return 1;   /* 0x44a992 */
    uint32_t fa = hr32(c, 0x0049b0b4u), fb = hr32(c, 0x0049b0acu), hgt = hsx16(hr16(c, fa + 0xau)), off = f - 0x90u;
    hw32(c, f - 0x8cu, 0);
    hw32(c, f - 0x90u, 0);
    uint32_t b = hr32(c, h + 0x40u + 4u * i), rect = f - 0xacu;
    uint32_t x0 = hr32(c, b), y0 = hr32(c, b + 4u), x1 = hr32(c, b + 8u), y1 = hr32(c, b + 0xcu);
    hw32(c, rect, hsx16(shl16(2u, sx)) + x0);
    hw32(c, rect + 4u, hsx16(shl16(4u, sy)) + y0);
    hw32(c, rect + 8u, x1 - hsx16(shl16(2u, sx)));
    hw32(c, rect + 12u, hr32(c, rect + 4u) + hgt);
    uint32_t l = 0;
    if (label_new(c, fa, off, rect, 2u, &l)) return 1;
    hw32(c, r, l);
    hw32(c, l + 0x1du, hsx16(hr16(c, 0x004d3b7cu + 2u * hsx16(hr16(c, 0x0049b040u + 2u * i)))));
    static const uint8_t rows[4] = {0x10, 0x18, 0x20, 0x28};
    for (uint32_t k = 0; k < 4u; ++k) {
        hw32(c, rect, x0);
        hw32(c, rect + 8u, x1);
        hw32(c, rect + 4u, hsx16(shl16(rows[k], sy)) + y0);
        hw32(c, rect + 12u, hr32(c, rect + 4u) + hgt);
        if (label_new(c, (k & 1u) ? fa : fb, off, rect, 2u, &l)) return 1;
        hw32(c, r + 4u + 4u * k, l);
    }
    hw32(c, rect, x0);
    hw32(c, rect + 8u, hsx16(shl16(0xau, sx)) + x0);
    hw32(c, rect + 12u, y1);
    hw32(c, rect + 4u, y1 - hsx16(shl16(0xau, sy)));
    if (label_new(c, fa, off, rect, 2u, &l)) return 1;
    hw32(c, r + 0x14u, l);
    hw32(c, l + 0x1du, hsx16(hr16(c, 0x004d3b9au)));
    hw32(c, r + 0x13fu, 0);
    hw32(c, r + 0x13bu, 0);
    hw8(c, r + 0x12eu, 0);
    hw8(c, r + 0x147u, 0);
    hw8(c, r + 0x149u, 0);
    hw8(c, r + 0x148u, 0);
    hw32(c, h + 0x538u + 4u * i, hr32(c, 0x004d03c8u + 4u * i));
    hw32(c, h + 0x54cu + 4u * i, hr32(c, r + 0x137u));
    hw8(c, 0x0049d24cu + i + 0xau, 1);
    hw8(c, 0x0049d25bu + i + 0xau, 1);
    hw16(c, hr32(c, h + 0x18u + 4u * (i + 0xau)) + 0x1bu, 0);
    if (HCALL(c, HUD_VA_STREAM_DESTROY, NULL, s, 2u)) return 1;
    hud_frame_pop(c, top);
    return 0;
}
void es2_sim_service_radio_record(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (radio_record(&c, sc->slots[0], sc->slots[1], (uint16_t)sc->slots[2])) { hud_failed(sc); return; }
    sc->eax = hud_fs0();
}
/* 0x4472bc(G, n) (listing 0x4472bc..0x447308): the MFD status view list:
 * G+0x308 <- the player [[0x49b088]+0x203], G+0x30c+4i <- [0x4d03c8+4i] for i
 * < n, G+0x31c <- n + 1. EAX G, ECX the loop's count, EDX the next slot. */
void es2_sim_service_mfd_view_list(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t g = sc->slots[0], n = sc->slots[1], i = 0;
    hw32(&c, g + 0x308u, hr32(&c, hr32(&c, VA_PANEL) + 0x203u));
    for (; (int32_t)n > (int32_t)i; ++i) hw32(&c, g + 0x30cu + 4u * i, hr32(&c, 0x004d03c8u + 4u * i));
    hw32(&c, g + 0x31cu, n + 1u);
    sc->eax = g; sc->ecx = i; sc->edx = g + 0x30cu + 4u * i;
}
/* ======================================================================
 * 0x437c8c(A) (listing 0x437c8c..0x437e90), root step 20 per MECH: the
 * weapon display records of A's damage view. T = [[A+0x1f2]+0xda], n = [T+0x54],
 * the source entries [T+0x58] (20 bytes each); E = 0x49667c(0, 0x22, n, 1,
 * 0x438798) (34 bytes each). Per i < n: the mount j of mount_table(A) (hook)
 * whose byte +0x17 is i (the first of word [m] entries of 0x1a bytes at
 * [m+2], or the count); word E+0x20 <- j; the weapon [[A+0x202]+4][j]: none
 * (or its word [[w+0x1c]+0x50] = -1): [E] <- 0; else k = that + word
 * src+8, [E] <- [[0x4d16d0]+8][k] (the bitmap), the size pair 0x4d16d4+8k;
 * E+4 <- src+0 (x, y) aligned by the low bits of src+0xc (2: x - w, 4: x - w/2)
 * and its bits 3.. (0x10: y - h, 0x20: y - h/2); E+0xc <- E+4, E+0x14 <- E+4
 * + (w, h); E+0x1c <- src+0x10 and, = 2, x - (the bitmap's word +6, scaled
 * by sx for byte [0x4d2537] = 3 and byte [0x4d256c] = 0, minus w). R =
 * new(8) = {n, E}. EAX R (NULL refused: the original writes through it). */
void es2_sim_service_damage_view(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t a = sc->slots[0], t = hr32(&c, hr32(&c, a + 0x1f2u) + 0xdau), n = hr32(&c, t + 0x54u), src = hr32(&c, t + 0x58u), arr = 0;
    if (HCALL(&c, HUD_VA_VECTOR_NEW, &arr, 0u, 0x22u, n, 1u, 0x00438798u)) { hud_failed(sc); return; }
    uint32_t e = arr;
    for (int32_t i = 0; (int32_t)n > i; ++i, e += 0x22u, src += 0x14u) {
        uint32_t m = 0;
        if (HCALL(&c, 0x00420634u, &m, a)) { hud_failed(sc); return; }
        uint32_t j = 0, p = hr32(&c, m + 2u);
        while ((int32_t)j < (int32_t)hsx16(hr16(&c, m)) && (int32_t)hsx8(hr8(&c, p + 0x17u)) != i) { ++j; p += 0x1au; }
        uint32_t w = hr32(&c, hr32(&c, hr32(&c, a + 0x202u) + 4u) + 4u * hsx16((uint16_t)j));
        uint16_t kind = w ? hr16(&c, hr32(&c, w + 0x1cu) + 0x50u) : 0xffffu;
        hw16(&c, e + 0x20u, (uint16_t)j);
        if (kind == 0xffffu) { hw32(&c, e, 0); continue; }
        int32_t k = (int32_t)(int16_t)(uint16_t)(kind + hr16(&c, src + 8u));
        hw32(&c, e, hr32(&c, hr32(&c, hr32(&c, 0x004d16d0u) + 8u) + 4u * (uint32_t)k));
        uint32_t size = 0x004d16d4u + 8u * (uint32_t)k, mode = hr32(&c, src + 0xcu);
        hw32(&c, e + 4u, hr32(&c, src));
        hw32(&c, e + 8u, hr32(&c, src + 4u));
        if ((mode & 7u) == 2u) hw32(&c, e + 4u, hr32(&c, e + 4u) - hr32(&c, size));
        else if ((mode & 7u) == 4u) hw32(&c, e + 4u, hr32(&c, e + 4u) - (uint32_t)((int32_t)hr32(&c, size) >> 1));
        if ((mode & ~7u) == 0x10u) hw32(&c, e + 8u, hr32(&c, e + 8u) - hr32(&c, size + 4u));
        else if ((mode & ~7u) == 0x20u) hw32(&c, e + 8u, hr32(&c, e + 8u) - (uint32_t)((int32_t)hr32(&c, size + 4u) >> 1));
        hw32(&c, e + 0x14u, hr32(&c, e + 4u));
        hw32(&c, e + 0x18u, hr32(&c, e + 8u));
        hw32(&c, e + 0xcu, hr32(&c, e + 0x14u));
        hw32(&c, e + 0x10u, hr32(&c, e + 0x18u));
        hw32(&c, e + 0x14u, hr32(&c, e + 0x14u) + hr32(&c, size));
        hw32(&c, e + 0x18u, hr32(&c, e + 0x18u) + hr32(&c, size + 4u));
        hw32(&c, e + 0x1cu, hr32(&c, src + 0x10u));
        if (hr32(&c, e + 0x1cu) == 2u) {
            uint16_t dx = hr16(&c, hr32(&c, e) + 6u);
            if ((int8_t)hr8(&c, 0x004d2537u) == 3 && !hr8(&c, 0x004d256cu)) dx = shl16(dx, shx(&c));
            hw32(&c, e + 4u, hr32(&c, e + 4u) - (hsx16(dx) - hr32(&c, size)));
        }
    }
    uint32_t r = 0;
    if (HCALL(&c, HUD_VA_NEW, &r, 8u)) { hud_failed(sc); return; }
    if (!r) { hud_null_refuse(&c, 0x00437e82u, "damage view", "operator new returned NULL (the original writes through it)"); hud_failed(sc); return; }
    hw32(&c, r, n);
    hw32(&c, r + 4u, arr);
    sc->eax = r;
    sc->edx = arr;
}
/* 0x44141c(G, rec) (listing 0x44141c..0x4414c8), the projectile gauge's
 * slot +0x10 (the sync): rec+0 into G+0xc2 (byte G+0xb0 <- 2 on a change);
 * byte rec+0x11 into G+0xd3 with the button's state word +0x1b <- 2 (set)
 * or 0; bytes rec+0x14..0x16 into G+0xd6..0xd8; on any of those the
 * button's slot +4 and byte G+0xb0 <- 2. */
void es2_sim_service_projectile_sync(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t g = sc->slots[0], rec = sc->slots[1];
    int redraw = 0;
    if (hr32(&c, rec) != hr32(&c, g + 0xc2u)) {
        hw32(&c, g + 0xc2u, hr32(&c, rec));
        hw8(&c, g + 0xb0u, 2);
    }
    if (hr8(&c, rec + 0x11u) != hr8(&c, g + 0xd3u)) {
        hw8(&c, g + 0xd3u, hr8(&c, rec + 0x11u));
        hw16(&c, hr32(&c, g + 0x18u) + 0x1bu, hr8(&c, rec + 0x11u) ? 2 : 0);
        redraw = 1;
    }
    for (uint32_t k = 0; k < 3u; ++k)
        if (hr8(&c, rec + 0x14u + k) != hr8(&c, g + 0xd6u + k)) {
            hw8(&c, g + 0xd6u + k, hr8(&c, rec + 0x14u + k));
            redraw = 1;
        }
    if (redraw) {
        if (slot4(&c, hr32(&c, g + 0x18u))) { hud_failed(sc); return; }
        hw8(&c, g + 0xb0u, 2);
    }
}
/* ======================================================================
 * The pod weapon gauges: weapon slot +0x64 of ECM 0x40e3bc, TURBO 0x40e3e4,
 * ENERGY 0x40e40c, SHIELD 0x40e434, TARG 0x40e45c (five identical bodies).
 *
 * 0x44154c(G, rect, H, slot, W) (listing 0x44154c..0x441740), the pod
 * gauge: 0x440834; [G] <- 0x49c8d0; flag = (1 << slot) & byte [rect+0x50];
 * the toggle new(0x46) -> 0x442204(B, rect, G, slot, flag), counter - 2,
 * G+0x18, added to the root; the name G+0xb1 <- the word at 0x49c749, then
 * _strncat(name 0x40e18c(W), 0xb) and _strncat([0x4d1348], 0xc - strlen -
 * 1); the count rectangle G+0xce (x0 + (0xb << sx), y0, x0 + (0x35 << sx),
 * y0 + (5 << sy)); the count label new(0x21) (0x4387ac font [0x49b0d8], 1)
 * into G+0xca placed by 0x438884 (flags flag ? 4 : 1), +0x1d <- 0x2e;
 * G+0x68 <- 1; _memset(G+0xc2, 0, 8). EAX G. */
static int pod_gauge(hud_ctx *c, uint32_t g, uint32_t rect, uint32_t h, uint16_t slot, uint32_t w) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x10u);
    if (!f) return hud_refuse(c, 0x0044154cu, "pod gauge", "the own code's call area is exhausted");
    if (weapon_gauge_base(c, g, rect, h, w)) return 1;
    hw32(c, g, 0x0049c8d0u);
    uint32_t root = hr32(c, hr32(c, VA_PANEL)), font = hr32(c, 0x0049b0d8u);
    uint8_t flag = (uint8_t)((uint8_t)(1u << (slot & 31u)) & hr8(c, rect + 0x50u));
    uint32_t b = 0;
    if (HCALL(c, HUD_VA_NEW, &b, 0x46u)) return 1;
    if (b) {
        if (weapon_toggle(c, b, rect, g, slot, flag)) return 1;
        hud_counter(c, -2);
    }
    hw32(c, g + 0x18u, b);
    widget_add(c, root, hr32(c, g + 0x18u));
    hw16(c, g + 0xb1u, hr16(c, 0x0049c749u));
    uint32_t len = 0;
    if (HCALL(c, 0x004967c0u, NULL, g + 0xb1u, weapon_name(c, w), 0xbu) || HCALL(c, 0x00496766u, &len, g + 0xb1u) ||
        HCALL(c, 0x004967c0u, NULL, g + 0xb1u, hr32(c, 0x004d1348u), 0xcu - len - 1u)) return 1;
    unsigned sx = shx(c), sy = shy(c);
    uint32_t r = g + 0xceu;
    hw32(c, r + 8u, hr32(c, rect));
    hw32(c, r + 0xcu, hr32(c, rect + 4u));
    hw32(c, r, hr32(c, r + 8u));
    hw32(c, r + 4u, hr32(c, r + 0xcu));
    hw32(c, r, hr32(c, r) + (0xbu << sx));
    hw32(c, r + 4u, hr32(c, r + 4u) + (0u << sy));
    hw32(c, r + 8u, hr32(c, r + 8u) + (0x35u << sx));
    hw32(c, r + 0xcu, hr32(c, r + 0xcu) + (5u << sy));
    uint32_t l = 0;
    if (HCALL(c, HUD_VA_NEW, &l, 0x21u)) return 1;
    if (l && HCALL(c, 0x004387acu, NULL, l, font, 1u)) return 1;
    hw32(c, g + 0xcau, l);
    hw32(c, f, 0);
    hw32(c, f + 4u, 0);
    if (!l) return hud_null_refuse(c, 0x004416ffu, "pod gauge", "the label allocation returned NULL (the original writes through it)");
    if (HCALL(c, 0x00438884u, NULL, l, f, r, flag ? 4u : 1u)) return 1;
    hw32(c, hr32(c, g + 0xcau) + 0x1du, 0x2eu);
    hw32(c, g + 0x68u, 1);
    if (HCALL(c, HUD_VA_MEMSET, NULL, g + 0xc2u, 0u, 8u)) return 1;
    hud_frame_pop(c, top);
    return 0;
}
/* 0x4419c0(G, rect, H, slot, W) (listing 0x4419c0..0x4419eb): 0x44154c,
 * [G] <- 0x49c8a4. 0x441a5c(G, rect, H, slot, W) (0x441a5c..0x441bab), the
 * energy pod gauge: 0x4419c0, [G] <- 0x49c878; the charge bar new(0x34)
 * -> 0x4395c4(bar, (x0 + (0x24 << sx), y0 + (1 << sy), x0 + (0x35 << sx),
 * y0 + (5 << sy)), 0x9c4), bar+0x2c <- 0x20, +0x30 <- 0x22, +0x24 <- 0x2e,
 * G+0xde; _strncpy(G+0xb1, 0x40e18c(W), 0xb); the count label [G+0xca]:
 * +4 <- x0 + (int16)(6 << sx), +0xc <- the bar's x0 - (2 << sx), +8 + (1 <<
 * sy). EAX G. */
static int pod_gauge_plain(hud_ctx *c, uint32_t g, uint32_t rect, uint32_t h, uint16_t slot, uint32_t w) {
    if (pod_gauge(c, g, rect, h, slot, w)) return 1;
    hw32(c, g, 0x0049c8a4u);
    return 0;
}
static int pod_gauge_energy(hud_ctx *c, uint32_t g, uint32_t rect, uint32_t h, uint16_t slot, uint32_t w) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x10u);
    if (!f) return hud_refuse(c, 0x00441a5cu, "energy pod gauge", "the own code's call area is exhausted");
    if (pod_gauge_plain(c, g, rect, h, slot, w)) return 1;
    hw32(c, g, 0x0049c878u);
    unsigned sx = shx(c), sy = shy(c);
    uint32_t x0 = hr32(c, rect), y0 = hr32(c, rect + 4u);
    hw32(c, f, x0 + (0x24u << sx));
    hw32(c, f + 4u, y0 + (1u << sy));
    hw32(c, f + 8u, x0 + (0x35u << sx));
    hw32(c, f + 0xcu, y0 + (5u << sy));
    uint32_t bar = 0;
    if (HCALL(c, HUD_VA_NEW, &bar, 0x34u)) return 1;
    if (bar && HCALL(c, 0x004395c4u, NULL, bar, f, 0x9c4u)) return 1;
    if (!bar) return hud_null_refuse(c, 0x00441b23u, "energy pod gauge", "the bar allocation returned NULL (the original writes through it)");
    hw32(c, bar + 0x2cu, 0x20u);
    hw32(c, bar + 0x30u, 0x22u);
    hw32(c, bar + 0x24u, 0x2eu);
    hw32(c, g + 0xdeu, bar);
    if (HCALL(c, HUD_VA_STRNCPY, NULL, g + 0xb1u, weapon_name(c, w), 0xbu)) return 1;
    uint32_t l = hr32(c, g + 0xcau);
    hw32(c, l + 4u, (uint32_t)(int32_t)(int16_t)shl16(6u, sx) + hr32(c, rect));
    hw32(c, l + 0xcu, hr32(c, f) - (2u << sx));
    hw32(c, l + 8u, hr32(c, l + 8u) + (1u << sy));
    hud_frame_pop(c, top);
    return 0;
}
/* 0x4321d4(H, slot, W) (listing 0x4321d4..0x43233e): the layout rectangle
 * L+0x14+16 slot (L = [0x49b0a8]) to L+0x164; by the weapon kind word
 * W+0x77: 0x12 new(0xde) -> 0x4419c0, 0x1d, 0x1e and 0x20 new(0xde) ->
 * 0x44154c, 0x1f new(0xe6) -> 0x441a5c (each when not NULL); other kinds
 * file an unset local (refused); 0x432018(H, G, slot). EAX G. Rows
 * 0x40e3bc, 0x40e3e4, 0x40e40c, 0x40e434, 0x40e45c (W) (each 0x28 bytes):
 * W+0x79 <- 0x4321d4([0x49b088], byte [[W+0x18]+7], W). */
static int pod_factory(hud_ctx *c, uint32_t h, uint16_t slot, uint32_t w, uint32_t *out) {
    uint32_t l = hr32(c, VA_LAYOUT), dst = l + 0x164u, src = l + 0x14u + (hsx16(slot) << 4), g = 0;
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, dst + i, hr32(c, src + i));
    int32_t kind = (int32_t)(int16_t)hr16(c, w + 0x77u);
    int (*ctor)(hud_ctx *, uint32_t, uint32_t, uint32_t, uint16_t, uint32_t) = NULL;
    uint32_t size = 0xdeu;
    if (kind == 0x12) ctor = pod_gauge_plain;
    else if (kind == 0x1d || kind == 0x1e || kind == 0x20) ctor = pod_gauge;
    else if (kind == 0x1f) { ctor = pod_gauge_energy; size = 0xe6u; }
    else return hud_refuse(c, 0x0043231au, "pod gauge factory", "a weapon kind without a pod gauge files an unset local in the original");
    if (HCALL(c, HUD_VA_NEW, &g, size)) return 1;
    if (g && ctor(c, g, dst, h, slot, w)) return 1;
    gauge_file(c, h, g, slot);
    *out = g;
    return 0;
}
void es2_sim_service_pod_gauge_new(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t w = sc->slots[0], g = 0;
    if (pod_factory(&c, hr32(&c, VA_PANEL), (uint16_t)hsx8(hr8(&c, hr32(&c, w + 0x18u) + 7u)), w, &g)) { hud_failed(sc); return; }
    hw32(&c, w + 0x79u, g);
    sc->eax = g;
}
void es2_sim_service_pod_factory(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (pod_factory(&c, sc->slots[0], (uint16_t)sc->slots[1], sc->slots[2], &sc->eax)) hud_failed(sc);
}

/* ---- the weapon gauges' per-frame draws (element slot +8) and the
 * small methods on their path (hud.md, "the panel weapon gauges' frame draws").
 * The panel's per-frame caller (0x4327xx) discards EAX of slot +8. "vis(R)" is
 * AL of 0x431410(R); D is [0x4cf99c]; W18/W1c are the widgets
 * [G+0x18]/[G+0x1c], whose vtable sits at W+0x17 (slot +4 redraw, +0xc frame). */
#define VA_VISIBLE 0x00431410u
#define VA_DISPLAY 0x004cf99cu
#define VA_DAMAGE_ALERT 0x004342e0u
#define VA_GAUGE_BLINK 0x00438bc0u
#define VA_ANIM_STATE 0x00471f24u
#define VA_LABEL_DRAW 0x00438920u
#define VA_PANEL_COPY 0x0043122cu
#define VA_PORT_CANCEL 0x00435ac8u
#define VA_COLOURS 0x0049b0acu

static int gvis(hud_ctx *c, uint32_t rect, int *yes) {
    uint32_t al = 0;
    if (HCALL(c, VA_VISIBLE, &al, rect)) return 1;
    *yes = (al & 0xffu) != 0;
    return 0;
}
/* Widget slot `off`: [[W+0x17]+off](W). */
static int wslot(hud_ctx *c, uint32_t w, uint32_t off, uint32_t *eax) {
    return hud_call(c, hr32(c, hr32(c, w + 0x17u) + off), (const uint32_t[]){w}, 1, eax);
}
/* The common head of the three frames (0x440cb8..0x440ce4, the same at
 * 0x441290..0x4412c0 and 0x441878..0x4418a4): go on only when vis(G+4) or
 * byte D+0x1c, and byte G+0x8c. */
static int frame_gate(hud_ctx *c, uint32_t g, int *go) {
    int yes = 0;
    *go = 0;
    if (gvis(c, g + 4u, &yes)) return 1;
    if (!yes && !hr8(c, hr32(c, VA_DISPLAY) + 0x1cu)) return 0;
    *go = hr8(c, g + 0x8cu) != 0;
    return 0;
}
/* The first full draw (0x440cfc..0x440d1e): when byte G+0x8e is clear and
 * vis(G+4): [[G]+4](G), G+0x8e <- 1, and the frame ends (*done). */
static int first_draw(hud_ctx *c, uint32_t g, int *done) {
    int yes = 0;
    *done = 0;
    if (hr8(c, g + 0x8eu)) return 0;
    if (gvis(c, g + 4u, &yes)) return 1;
    if (!yes) return 0;
    if (HVCALL(c, g, 4u, NULL, g)) return 1;
    hw8(c, g + 0x8eu, 1);
    *done = 1;
    return 0;
}
/* The damage blink step (0x440d20..0x440d37): word G+0x74 <- AX of
 * 0x4342e0(2); when that is nonzero 0x438bc0(G). */
static int blink_step(hud_ctx *c, uint32_t g) {
    uint32_t ax = 0;
    if (HCALL(c, VA_DAMAGE_ALERT, &ax, 2u)) return 1;
    hw16(c, g + 0x74u, (uint16_t)ax);
    if ((uint16_t)ax && HCALL(c, VA_GAUGE_BLINK, NULL, g)) return 1;
    return 0;
}
/* Byte G+0x76 differs from G+0x77 and is clear (0x440d38..0x440d42). */
static int blink_on(hud_ctx *c, uint32_t g) {
    uint8_t a = hr8(c, g + 0x76u);
    return a != hr8(c, g + 0x77u) && a == 0;
}
/* The busy test with the call 0x471f24 (0x44133e..0x441362): byte G+0x76,
 * or an animation [G+0x6c] whose 0x471f24 EAX is not 0. */
static int gbusy(hud_ctx *c, uint32_t g, int *yes) {
    *yes = 1;
    if (hr8(c, g + 0x76u)) return 0;
    uint32_t a = hr32(c, g + 0x6cu), v = 0;
    if (a && HCALL(c, VA_ANIM_STATE, &v, a)) return 1;
    *yes = a && v != 0;
    return 0;
}
static void count_down(hud_ctx *c, uint32_t g) { hw8(c, g + 0xb0u, (uint8_t)(hr8(c, g + 0xb0u) - 1u)); }

/* Original: DBSIM.EXE 0x440eac(G) (listing 0x440eac..0x440f14), the energy
 * gauge's power-on sweep (called from 0x440cf4): t = clock (site 0x440eb3)
 * - [G+0x90]; t > 50 unsigned: [G+0xc2] <- [G+0xdb], byte G+0x8d <- 1;
 * otherwise [G+0xc2] <- 25t when [G+0xdb] > 25t (signed), else [G+0xdb].
 * Then [[G+0x1c]+0x7a] <- [G+0xc2], W18 slot +4, W1c slot +4 (EAX the
 * last). */
static int energy_sweep(hud_ctx *c, uint32_t g, uint32_t *eax) {
    uint32_t now = 0;
    if (hud_clock(c, 0x00440eb3u, &now)) return 1;
    uint32_t t = now - hr32(c, g + 0x90u), full = hr32(c, g + 0xdbu);
    if (t > 0x32u) {
        hw32(c, g + 0xc2u, full);
        hw8(c, g + 0x8du, 1);
    } else {
        uint32_t s = t * 25u;
        hw32(c, g + 0xc2u, (int32_t)full > (int32_t)s ? s : full);
    }
    hw32(c, hr32(c, g + 0x1cu) + 0x7au, hr32(c, g + 0xc2u));
    if (wslot(c, hr32(c, g + 0x18u), 4u, NULL)) return 1;
    return wslot(c, hr32(c, g + 0x1cu), 4u, eax);
}
/* Original: DBSIM.EXE 0x441db0(G) (listing 0x441db0..0x441df7), the energy
 * pod's sweep (slot +0x1c of 0x49c878): as 0x440eac with the clock site
 * 0x441db7, [G+0xe2] -> [G+0xc6] and no redraws. EAX [G+0xe2] on the long
 * path, else t. */
static int pod_sweep(hud_ctx *c, uint32_t g, uint32_t *eax) {
    uint32_t now = 0;
    if (hud_clock(c, 0x00441db7u, &now)) return 1;
    uint32_t t = now - hr32(c, g + 0x90u), full = hr32(c, g + 0xe2u);
    if (t > 0x32u) {
        hw32(c, g + 0xc6u, full);
        hw8(c, g + 0x8du, 1);
        *eax = full;
    } else {
        uint32_t s = t * 25u;
        hw32(c, g + 0xc6u, (int32_t)full > (int32_t)s ? s : full);
        *eax = t;
    }
    return 0;
}

/* Original: DBSIM.EXE 0x440cb8(G) (listing 0x440cb8..0x440d73), the energy
 * gauge's frame (slot +8 of 0x49c91c): the gate; byte G+0x8d clear: the
 * sweep 0x440eac; set: the first full draw (ends the frame); then the blink
 * step; when the blink came on, W18 and W1c slot +4; then W18 and W1c slot
 * +0xc. */
static int energy_frame(hud_ctx *c, uint32_t g) {
    int go = 0, done = 0;
    if (frame_gate(c, g, &go)) return 1;
    if (!go) return 0;
    if (!hr8(c, g + 0x8du)) {
        if (HCALL(c, 0x00440eacu, NULL, g)) return 1;
    } else {
        if (first_draw(c, g, &done)) return 1;
        if (done) return 0;
    }
    if (blink_step(c, g)) return 1;
    if (blink_on(c, g)) {
        if (wslot(c, hr32(c, g + 0x18u), 4u, NULL)) return 1;
        if (wslot(c, hr32(c, g + 0x1cu), 4u, NULL)) return 1;
    }
    if (wslot(c, hr32(c, g + 0x18u), 0xcu, NULL)) return 1;
    return wslot(c, hr32(c, g + 0x1cu), 0xcu, NULL);
}
/* Original: DBSIM.EXE 0x441290(G) (listing 0x441290..0x441419), the
 * projectile gauge's frame (slot +8 of 0x49c8fc): the gate; bytes G+0x8d
 * <- 1, G+0xb0 <- 2; the first full draw (ends the frame); the blink step;
 * when the blink came on, W18 slot +4 and G+0xb0 <- 2; W18 slot +0xc; busy
 * ends the frame; byte [W18+0x1f] set: G+0xb0 <- 2. Then by G+0xb0: 2:
 * vis([G+0xdb]+4) clear ends the frame (no count down); else, when byte
 * G+0xd3 is clear, _itoa([G+0xc2], an 8-byte local, 10), [[G+0xdb]] <- the
 * colour [0x49b0ac + 4 (G+0xd6 ? 10 : 11)] and 0x438920([G+0xdb], local);
 * G+0xb0 - 1. 1: when G+0xd3 is clear 0x43122c(0, [G+0xdb]+4, 0); G+0xb0 -
 * 1. Other values: nothing. */
static int projectile_frame(hud_ctx *c, uint32_t g) {
    int go = 0, done = 0, b = 0;
    if (frame_gate(c, g, &go)) return 1;
    if (!go) return 0;
    hw8(c, g + 0x8du, 1);
    hw8(c, g + 0xb0u, 2);
    if (first_draw(c, g, &done)) return 1;
    if (done) return 0;
    if (blink_step(c, g)) return 1;
    if (blink_on(c, g)) {
        if (wslot(c, hr32(c, g + 0x18u), 4u, NULL)) return 1;
        hw8(c, g + 0xb0u, 2);
    }
    if (wslot(c, hr32(c, g + 0x18u), 0xcu, NULL)) return 1;
    if (gbusy(c, g, &b)) return 1;
    if (b) return 0;
    if (hr8(c, hr32(c, g + 0x18u) + 0x1fu)) hw8(c, g + 0xb0u, 2);
    uint8_t state = hr8(c, g + 0xb0u);
    if (state == 2u) {
        int yes = 0;
        if (gvis(c, hr32(c, g + 0xdbu) + 4u, &yes)) return 1;
        if (!yes) return 0;
        if (!hr8(c, g + 0xd3u)) {
            uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 8u);
            if (!f) return hud_refuse(c, 0x00441290u, "projectile gauge frame", "the own code's call area is exhausted");
            if (HCALL(c, HUD_VA_ITOA, NULL, hr32(c, g + 0xc2u), f, 10u)) return 1;
            uint32_t colour = hr32(c, VA_COLOURS + 4u * (hr8(c, g + 0xd6u) ? 10u : 11u)), l = hr32(c, g + 0xdbu);
            hw32(c, l, colour);
            if (HCALL(c, VA_LABEL_DRAW, NULL, l, f)) return 1;
            hud_frame_pop(c, top);
        }
        count_down(c, g);
    } else if (state == 1u) {
        if (!hr8(c, g + 0xd3u) && HCALL(c, VA_PANEL_COPY, NULL, 0u, hr32(c, g + 0xdbu) + 4u, 0u)) return 1;
        count_down(c, g);
    }
    return 0;
}
/* Original: DBSIM.EXE 0x441878(G) (listing 0x441878..0x44196f), the pod
 * gauges' frame (slot +8 of 0x49c8a4 and 0x49c8d0; 0x441da0 forwards to it
 * for 0x49c878): the gate; byte G+0x8d clear: [[G]+0x1c](G) (the sweep
 * 0x443444 or 0x441db0); set: the first full draw (ends the frame); the
 * blink step; when the blink came on G+0xb0 <- 2; busy ends the frame;
 * G+0xb0 2: [[G]+4](G), - 1; 1: 0x43122c(0, [G+0xca]+4, 0), - 1. */
static int pod_frame(hud_ctx *c, uint32_t g) {
    int go = 0, done = 0, b = 0;
    if (frame_gate(c, g, &go)) return 1;
    if (!go) return 0;
    if (!hr8(c, g + 0x8du)) {
        if (HVCALL(c, g, 0x1cu, NULL, g)) return 1;
    } else {
        if (first_draw(c, g, &done)) return 1;
        if (done) return 0;
    }
    if (blink_step(c, g)) return 1;
    if (blink_on(c, g)) hw8(c, g + 0xb0u, 2);
    if (gbusy(c, g, &b)) return 1;
    if (b) return 0;
    uint8_t state = hr8(c, g + 0xb0u);
    if (state == 2u) {
        if (HVCALL(c, g, 4u, NULL, g)) return 1;
        count_down(c, g);
    } else if (state == 1u) {
        if (HCALL(c, VA_PANEL_COPY, NULL, 0u, hr32(c, g + 0xcau) + 4u, 0u)) return 1;
        count_down(c, g);
    }
    return 0;
}
/* Original: DBSIM.EXE 0x441fa4(G) (listing 0x441fa4..0x442051), the range
 * lamp toggle (W = [G+0x20]): word W+0x1b == 2: EAX 0. Otherwise byte G+0xb3
 * ^= 1; set: when byte W+0x40 is clear, W+0x40 <- 1 and W slot +4; clear:
 * W+0x40 <- 0 when set, W slot +4 always. P = [[0x49b088]+0x20b]; the
 * 6-byte local event {word G+0xb3 ? 0x26 : 0x27, dword 0}; 0x435ac8(P, 0,
 * 0x26), 0x435ac8(P, 0, 0x27), [[P]](P, &event). EAX: AL 1 over the upper
 * bytes of the last call's EAX. */
static int range_lamp(hud_ctx *c, uint32_t g, uint32_t *eax) {
    uint32_t w = hr32(c, g + 0x20u);
    if (hr16(c, w + 0x1bu) == 2u) {
        *eax = 0;
        return 0;
    }
    hw8(c, g + 0xb3u, (uint8_t)(hr8(c, g + 0xb3u) ^ 1u));
    if (hr8(c, g + 0xb3u)) {
        if (!hr8(c, w + 0x40u)) {
            hw8(c, w + 0x40u, 1);
            if (wslot(c, w, 4u, NULL)) return 1;
        }
    } else {
        if (hr8(c, w + 0x40u)) hw8(c, w + 0x40u, 0);
        if (wslot(c, w, 4u, NULL)) return 1;
    }
    uint32_t p = hr32(c, hr32(c, VA_PANEL) + 0x20bu);
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x18u);
    if (!f) return hud_refuse(c, 0x00441fa4u, "range lamp", "the own code's call area is exhausted");
    hw16(c, f, (uint16_t)(hr8(c, g + 0xb3u) ? 0x26u : 0x27u));
    hw32(c, f + 2u, 0);
    if (HCALL(c, VA_PORT_CANCEL, NULL, p, 0u, 0x26u)) return 1;
    if (HCALL(c, VA_PORT_CANCEL, NULL, p, 0u, 0x27u)) return 1;
    uint32_t r = 0;
    if (HCALL(c, hr32(c, hr32(c, p)), &r, p, f)) return 1;
    hud_frame_pop(c, top);
    *eax = (r & ~0xffu) | 1u;
    return 0;
}

void es2_sim_service_energy_frame(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (energy_frame(&c, sc->slots[0])) hud_failed(sc);
}
void es2_sim_service_projectile_frame(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (projectile_frame(&c, sc->slots[0])) hud_failed(sc);
}
void es2_sim_service_pod_frame(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (pod_frame(&c, sc->slots[0])) hud_failed(sc);
}
/* Original: DBSIM.EXE 0x441da0(G) (listing 0x441da0..0x441dae), the energy
 * pod's frame (slot +8 of 0x49c878): 0x441878(G), through the dispatch. */
void es2_sim_service_energy_pod_frame(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (HCALL(&c, 0x00441878u, NULL, sc->slots[0])) hud_failed(sc);
}
/* Original: DBSIM.EXE 0x441c04(G, w) (listing 0x441c04..0x441c3b), the
 * energy pod's click (slot 0 of 0x49c878): for i from 0 while i < [G+0x68]
 * (signed): when w == [G+0x18+4i] and i == 0 (the `sub 1; jae` at
 * 0x441c1c), byte G+0xc2 ^= 1 and G+0xb0 <- 2. EAX the final i, EDX G, ECX
 * G+0x18+4i. */
void es2_sim_service_energy_pod_click(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t g = sc->slots[0], w = sc->slots[1];
    int32_t n = (int32_t)hr32(&c, g + 0x68u), i = 0;
    for (; i < n; ++i) {
        if (w == hr32(&c, g + 0x18u + 4u * (uint32_t)i) && i == 0) {
            hw8(&c, g + 0xc2u, (uint8_t)(hr8(&c, g + 0xc2u) ^ 1u));
            hw8(&c, g + 0xb0u, 2);
        }
    }
    sc->eax = (uint32_t)i;
    sc->edx = g;
    sc->ecx = g + 0x18u + 4u * (uint32_t)i;
}
void es2_sim_service_range_lamp(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (range_lamp(&c, sc->slots[0], &sc->eax)) hud_failed(sc);
}
/* Original: DBSIM.EXE 0x447d94(G) (listing 0x447d94..0x447da6), the
 * throttle element's frame (slot +8 of 0x49d1e0): [[W+0x17]+0xc](W) with
 * W = [G+0x18]; EAX the callee's. */
void es2_sim_service_throttle_frame(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (wslot(&c, hr32(&c, sc->slots[0] + 0x18u), 0xcu, &sc->eax)) hud_failed(sc);
}
/* 0x440834(G, rect, H, W): weapon_gauge_base above as a row (the gauge
 * constructors 0x440a90, 0x440fa0, 0x44154c reach it). EAX G. */
void es2_sim_service_weapon_gauge_base(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (weapon_gauge_base(&c, sc->slots[0], sc->slots[1], sc->slots[2], sc->slots[3])) {
        hud_failed(sc);
        return;
    }
    sc->eax = sc->slots[0];
}
void es2_sim_service_energy_sweep(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (energy_sweep(&c, sc->slots[0], &sc->eax)) hud_failed(sc);
}
void es2_sim_service_pod_sweep(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (pod_sweep(&c, sc->slots[0], &sc->eax)) hud_failed(sc);
}
/* Original: DBSIM.EXE 0x443444(G) (listing 0x443444..0x443452), the plain
 * pods' sweep (slot +0x1c of 0x49c8a4/0x49c8d0): byte G+0x8d <- 1; EAX G. */
void es2_sim_service_pod_sweep_plain(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    hw8(&c, sc->slots[0] + 0x8du, 1);
    sc->eax = sc->slots[0];
}

/* The slot-0 methods of three gauges (cockpit.md, "The widget slots"). Each walks the button list G+0x18[G+0x68] (the count reread
 * and compared signed each round) for the pressed widget W and leaves EAX the
 * index register, EDX the list cursor and ECX as the listing leaves it. */

/* 0x4414dc(G, W, v) (listing 0x4414dc..0x441548), ProjectileWeaponGauge:
 * on the first match, index 0 (the button): v & 2 group_toggle([[[0x49b088]
 * +0x203]+0x202], the word G+0xd9 in CX over W's high word) (hook), else
 * panel_weapon([G+0x14], G) (hook); then G+0xb0 <- 2; EAX, EDX and ECX the
 * callee's. A later index returns EAX = index - 1. */
void es2_sim_service_projectile_gauge_event(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t g = sc->slots[0], w = sc->slots[1], v = sc->slots[2], i = 0;
    for (; (int32_t)i < (int32_t)hr32(&c, g + 0x68u); ++i) {
        if (w != hr32(&c, g + 0x18u + 4u * i)) continue;
        sc->edx = g + 0x18u + 4u * i;
        sc->ecx = w;
        if (i) { sc->eax = i - 1u; return; }
        es2_sim_result r;
        int refused;
        if (v & 2u) {
            uint32_t n = (w & 0xffff0000u) | hr16(&c, g + 0xd9u);
            uint32_t list = hr32(&c, hr32(&c, hr32(&c, VA_PANEL) + 0x203u) + 0x202u);
            refused = hud_call_regs(&c, 0x004110acu, (const uint32_t[]){list, n}, 2, NULL, &r);
        } else {
            refused = hud_call_regs(&c, 0x00432a50u, (const uint32_t[]){hr32(&c, g + 0x14u), g}, 2, NULL, &r);
        }
        if (refused) { hud_failed(sc); return; }
        hw8(&c, g + 0xb0u, 2);
        sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
        return;
    }
    sc->eax = i;
    sc->edx = g + 0x18u + 4u * i;
    sc->ecx = w;
}
/* 0x441a24(G, W) (listing 0x441a24..0x441a5b), TogglePodGauge: every match
 * at index 0 toggles bit 0 of G+0xc2 and sets G+0xb0 <- 2; the walk goes on
 * to the end. EAX the count reached, EDX = G, ECX the cursor. */
void es2_sim_service_pod_gauge_event(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t g = sc->slots[0], w = sc->slots[1], i = 0;
    for (; (int32_t)i < (int32_t)hr32(&c, g + 0x68u); ++i) {
        if (w != hr32(&c, g + 0x18u + 4u * i) || i) continue;
        hw8(&c, g + 0xc2u, (uint8_t)(hr8(&c, g + 0xc2u) ^ 1u));
        hw8(&c, g + 0xb0u, 2);
    }
    sc->eax = i;
    sc->edx = g;
    sc->ecx = g + 0x18u + 4u * i;
}
/* 0x443834(G, W) (listing 0x443834..0x44387f), ShieldsGauge: on the first
 * match, index 0: G+0xc2 <- 1, G+0xc3 <- 0 (EAX -1); index 1: G+0xc3 <- 1,
 * G+0xc2 <- 0 (EAX 0); a later index: EAX = index - 1. ECX = G. */
void es2_sim_service_shields_gauge_event(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t g = sc->slots[0], w = sc->slots[1], i = 0;
    sc->ecx = g;
    for (; (int32_t)i < (int32_t)hr32(&c, g + 0x68u); ++i) {
        if (w != hr32(&c, g + 0x18u + 4u * i)) continue;
        sc->edx = g + 0x18u + 4u * i;
        sc->eax = i - 1u;
        if (i == 0) {
            hw8(&c, g + 0xc2u, 1);
            hw8(&c, g + 0xc3u, 0);
        } else if (i == 1) {
            hw8(&c, g + 0xc3u, 1);
            hw8(&c, g + 0xc2u, 0);
        }
        return;
    }
    sc->eax = i;
    sc->edx = g + 0x18u + 4u * i;
}
