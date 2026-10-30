/* The radio panel's message callbacks and voice start, the radio records'
 * helpers, the HDD legend and button slots, the MFD select gadget's click and
 * the throttle sliders' redraw slots as own code (the catalogue names
 * src/mfd.c). Contract: hud.md, "the radio panel and the HDD leftovers". Every
 * callee is reached through the dispatch (rows, hooks or, where none exists
 * yet, the original). */
#include "hud_internal.h"

#define VA_GC 0x006c5f70u
#define VA_VIEW 0x006c5ed0u
#define VA_HDD 0x0049d230u            /* the HDD gauge (its radio records at +0x12d, 0x14e each) */
#define VA_RADIO_GC 0x004d1ccc
#define VA_RADIO_SAVED_GC 0x004d1cc8u
#define VA_OFF_X 0x004d2556u          /* the 640x480 offsets */
#define VA_OFF_Y 0x004d255au
#define VA_SHIFT_X 0x004d253au
#define VA_DRIVER_SLOT 0x004a57ecu
#define VA_DRIVER_PAGE 0x004a3686u
#define VA_VISIBLE 0x00431410u        /* panel_visible (hook) */
#define VA_CANVAS_PUSH 0x004311e0u
#define VA_CANVAS_POP 0x00431210u
#define VA_PANEL_COPY 0x0043122cu
#define VA_LABEL 0x00438920u
#define VA_GC_SELECT 0x00480d44u
#define VA_CONDITION_RECORD 0x0041b4ecu   /* condition_record (hook) */
#define VA_WINGMAN_INDEX 0x00433134u      /* wingman_index (hook) */
#define VA_PORT_A 0x00433158u
#define VA_PORT_DROP 0x00435b14u
#define VA_PORT_NEXT 0x00435b38u
#define VA_SFX_PLAYING 0x00462af0u
#define VA_SFX_PLAY 0x00462830u           /* sfx_play (hook) */
#define VA_HDD_RADIO 0x0044b620u
#define VA_VOICE_START 0x0044aff0u
#define VA_CALLSIGN 0x00434260u           /* (panel.c's leaves) */
#define VA_PATH_DIR 0x00492becu
#define VA_WAV_PATH 0x0045ef9cu
#define VA_VOICE_PLAY 0x00462b9cu
#define VA_MFD_CLICK 0x004470c0u          /* mfd_click (hook) */
#define VA_LEGEND_COLOUR 0x00434280u
#define VA_SLIDER_RECT 0x004390b8u
#define VA_SAVE_UNDER 0x00488143u
#define VA_SAVE_BACK 0x00488012u
#define VA_KNOB_DRAW 0x004816a8u

static int visible(hud_ctx *c, uint32_t rect, int *yes) {
    uint32_t al = 0;
    if (HCALL(c, VA_VISIBLE, &al, rect)) return 1;
    *yes = (al & 0xffu) != 0;
    return 0;
}
/* The radio record of wingman w: [0x49d230] + 0x14e w + 0x12d (mod 2^32). */
static uint32_t radio_record(hud_ctx *c, uint32_t w) { return hr32(c, VA_HDD) + 0x14eu * w + 0x12du; }
static void dec8(hud_ctx *c, uint32_t a) { hw8(c, a, (uint8_t)(hr8(c, a) - 1u)); }

/* Original: DBSIM.EXE 0x44ae1c(H, V) (listing 0x44ae1c..0x44ae9f): V's
 * condition class. condition_record(V, buf) (hook) into the 0x54-byte
 * local buf; the 19 words buf+2.. summed signed, idiv 19 (truncating);
 * x = ((256 - mean) * 100) sar 8; x >= 90: 0, >= 74: 1, >= 51: 2, >= 1:
 * 3, else 4 (EAX). H is not read. */
static int condition_class(hud_ctx *c, uint32_t v, uint32_t *eax) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x54u);
    if (!f) return hud_refuse(c, 0x0044ae1cu, "condition class", "the own code's call area is exhausted");
    if (HCALL(c, VA_CONDITION_RECORD, NULL, v, f)) { hud_frame_pop(c, top); return 1; }
    int32_t sum = 0;
    for (uint32_t i = 0; i < 19u; ++i) sum += (int16_t)hr16(c, f + 2u + 2u * i);
    hud_frame_pop(c, top);
    int32_t x = ((256 - sum / 19) * 100) >> 8;
    *eax = x >= 0x5a ? 0u : x >= 0x4a ? 1u : x >= 0x33 ? 2u : x >= 1 ? 3u : 4u;
    return 0;
}
void es2_sim_service_radio_class(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t eax = 0;
    if (condition_class(&c, sc->slots[1], &eax)) hud_failed(sc);
    else sc->eax = eax;
}

/* Original: DBSIM.EXE 0x44b864(S) (listing 0x44b864..0x44b91e): the radio
 * record's graphic context. The canvas pushed (0x4311e0); [0x4d1cc8] <-
 * the current context [0x6c5f70]; the radio context R = [0x4d1ccc] gets
 * R+0x210..+0x21f <- S+0x1d..+0x2c, then both corners less [0x4d1cc8]
 * +0x220/+0x224 and less the 640x480 offsets [0x4d2556]/[0x4d255a];
 * 0x480d44(R) (EAX its). */
static int radio_context(hud_ctx *c, uint32_t s, uint32_t *eax) {
    if (HCALL0(c, VA_CANVAS_PUSH, NULL)) return 1;
    hw32(c, VA_RADIO_SAVED_GC, hr32(c, VA_GC));
    uint32_t r = hr32(c, VA_RADIO_GC);
    for (uint32_t k = 0; k < 4u; ++k) hw32(c, r + 0x210u + 4u * k, hr32(c, s + 0x1du + 4u * k));
    for (uint32_t corner = 0; corner < 2u; ++corner)
        for (uint32_t k = 0; k < 2u; ++k) {
            uint32_t at = hr32(c, VA_RADIO_GC) + 0x210u + 8u * corner + 4u * k;
            hw32(c, at, hr32(c, at) - hr32(c, hr32(c, VA_RADIO_SAVED_GC) + 0x220u + 4u * k));
        }
    for (uint32_t corner = 0; corner < 2u; ++corner)
        for (uint32_t k = 0; k < 2u; ++k) {
            uint32_t at = hr32(c, VA_RADIO_GC) + 0x210u + 8u * corner + 4u * k;
            hw32(c, at, hr32(c, at) - hr32(c, k ? VA_OFF_Y : VA_OFF_X));
        }
    return HCALL(c, VA_GC_SELECT, eax, hr32(c, VA_RADIO_GC));
}
void es2_sim_service_radio_context(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t eax = 0;
    if (radio_context(&c, sc->slots[0], &eax)) hud_failed(sc);
    else sc->eax = eax;
}
/* Original: DBSIM.EXE 0x44b920() (listing 0x44b920..0x44b925): the canvas
 * popped (0x431210; EAX its). */
void es2_sim_service_radio_context_end(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t eax = 0;
    if (HCALL0(&c, VA_CANVAS_POP, &eax)) hud_failed(sc);
    else sc->eax = eax;
}
/* Original: DBSIM.EXE 0x44b928(x, w) (listing 0x44b928..0x44b94f): the
 * actor of wingman w's radio record, [[0x49d230] + 0x14e w + 0x12d +
 * 0x137] (EAX); x is not read. */
void es2_sim_service_radio_actor(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    sc->eax = hr32(&c, radio_record(&c, sc->slots[1]) + 0x137u);
}

/* The decimal length of itoa(v, buf, 10) (a minus sign included). */
static uint32_t decimal_length(int32_t v) {
    uint32_t n = v < 0 ? 1u : 0u, u = v < 0 ? 0u - (uint32_t)v : (uint32_t)v;
    do { ++n; u /= 10u; } while (u);
    return n;
}
/* Original: DBSIM.EXE 0x44aff0(H, M, S) (listing 0x44aff0..0x44b147): the
 * voice of radio message M for record S. Byte S+0x12d <- byte [M]. Locals
 * (frame f = EBP-0x30): the number buffer N = f, the names C = f+0xc, B =
 * f+0x18, A = f+0x24 (12 bytes each), A and B start as the two-byte
 * strings at 0x49d588 / 0x49d58a, C as 0x49d58c. itoa(0x434260(sx16 word
 * S+0x135), N, 10), strcat(A, N); N <- the byte S+0x135 + 0x41 and a NUL,
 * strcat(B, N); N <- "0" when the word [M] is below 10 (unsigned), then
 * itoa(word [M]); strcat(C, N); N <- "0" when byte M+0x16 is below 100
 * and another "0" below 10, then itoa(byte M+0x16); strcat(C, N);
 * strcat(A, C), strcat(B, C). p = 0x492bec(B, 0x49d592); w =
 * 0x45ef9c(A, 0x49d58e); S+0x12f <- 0x462b9c(w, p) (the voice); EAX =
 * that with AL 1 when nonzero, else 0. H is not read. The original
 * overruns its 12-byte locals when a name grows past 11 characters (its
 * frame and return address are next); the own code refuses that case
 * before the first write. */
static int voice_start(hud_ctx *c, uint32_t m, uint32_t s, uint32_t *eax) {
    hw8(c, s + 0x12du, hr8(c, m));
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x30u);
    if (!f) return hud_refuse(c, VA_VOICE_START, "voice start", "the own code's call area is exhausted");
    uint32_t n = f, cn = f + 0xcu, bn = f + 0x18u, an = f + 0x24u, sign = 0;
    hw16(c, an, hr16(c, 0x0049d588u));
    hw16(c, bn, hr16(c, 0x0049d58au));
    int bad = 1;
    if (HCALL(c, VA_CALLSIGN, &sign, hsx16(hr16(c, s + 0x135u)))) goto done;
    {
        /* The lengths the original's strings reach (strlen of the copied
         * starts, the digits, the zero padding). */
        uint32_t la = 0, lb = 0, lc = 0, word = hr16(c, m), byte = hr8(c, m + 0x16u);
        while (la < 2u && hr8(c, an + la)) ++la;
        while (lb < 2u && hr8(c, bn + lb)) ++lb;
        while (lc < 2u && hr8(c, 0x0049d58cu + lc)) ++lc;
        lc += (word < 10u ? 1u : 0u) + decimal_length((int32_t)word) + (byte < 100u ? 1u : 0u) + (byte < 10u ? 1u : 0u) +
              decimal_length((int32_t)byte);
        la += decimal_length((int32_t)sign) + lc;
        lb += 1u + lc;
        if (la > 11u || lb > 11u || lc > 11u) {
            hud_frame_pop(c, top);
            return hud_refuse(c, VA_VOICE_START, "voice start", "a name exceeds the original's 12-byte locals");
        }
    }
    if (HCALL(c, HUD_VA_ITOA, NULL, sign, n, 10u) || HCALL(c, HUD_VA_STRCAT, NULL, an, n)) goto done;
    hw8(c, n, (uint8_t)(hr8(c, s + 0x135u) + 0x41u));
    hw8(c, n + 1u, 0);
    if (HCALL(c, HUD_VA_STRCAT, NULL, bn, n)) goto done;
    hw16(c, cn, hr16(c, 0x0049d58cu));
    uint32_t at = n;
    if (hr16(c, m) < 10u) hw8(c, at++, 0x30u);
    if (HCALL(c, HUD_VA_ITOA, NULL, hr16(c, m), at, 10u) || HCALL(c, HUD_VA_STRCAT, NULL, cn, n)) goto done;
    at = n;
    if (hr8(c, m + 0x16u) < 100u) hw8(c, at++, 0x30u);
    if (hr8(c, m + 0x16u) < 10u) hw8(c, at++, 0x30u);
    uint32_t p = 0, w = 0, v = 0;
    if (HCALL(c, HUD_VA_ITOA, NULL, hr8(c, m + 0x16u), at, 10u) || HCALL(c, HUD_VA_STRCAT, NULL, cn, n) ||
        HCALL(c, HUD_VA_STRCAT, NULL, an, cn) || HCALL(c, HUD_VA_STRCAT, NULL, bn, cn) ||
        HCALL(c, VA_PATH_DIR, &p, bn, 0x0049d592u) || HCALL(c, VA_WAV_PATH, &w, an, 0x0049d58eu) ||
        HCALL(c, VA_VOICE_PLAY, &v, w, p))
        goto done;
    hw32(c, s + 0x12fu, v);
    *eax = v ? (v & 0xffffff00u) | 1u : 0u;
    bad = 0;
done:
    hud_frame_pop(c, top);
    return bad;
}
void es2_sim_service_radio_voice(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t eax = 0;
    if (voice_start(&c, sc->slots[1], sc->slots[2], &eax)) hud_failed(sc);
    else sc->eax = eax;
}

/* Original: DBSIM.EXE 0x44b514(M) (listing 0x44b514..0x44b5e7): the radio
 * port's start callback (installed by the HDD constructor through
 * 0x4355a8). w = wingman_index([M+2]) (hook); -1: done (EAX -1). S = the
 * record of w (with H = [0x49d230] read once); 0x44aff0(H, M, S) with AL
 * set: S+0x143 <- the clock (site 0x44b559) + 0x14, S+0x13b <- 1; unless
 * 0x462af0(0x1c) the sound 0x462830(0x1c) (hook); 0x44b620([0x49d230]);
 * with [H'+0x766] 0 (H' = [0x49d230] again): H'+0x766 <- [S+0x137], word
 * H'+0x786 <- word [[S]+0x1d], H'+0x782 <- 0; S+0x14a <- M; EAX H'. AL
 * clear: the port P = 0x433158(), 0x435b14(P, M), 0x435b38(P, M) (EAX
 * the last). */
static int radio_start(hud_ctx *c, uint32_t m, uint32_t *eax) {
    uint32_t w = 0, r = 0;
    if (HCALL(c, VA_WINGMAN_INDEX, &w, hr32(c, m + 2u))) return 1;
    if (w == 0xffffffffu) { *eax = w; return 0; }
    uint32_t h = hr32(c, VA_HDD), s = h + 0x14eu * w + 0x12du;
    if (HCALL(c, VA_VOICE_START, &r, h, m, s)) return 1;
    if (r & 0xffu) {
        uint32_t now = 0;
        if (hud_clock(c, 0x0044b559u, &now)) return 1;
        hw32(c, s + 0x143u, now + 0x14u);
        hw32(c, s + 0x13bu, 1u);
        if (HCALL(c, VA_SFX_PLAYING, &r, 0x1cu)) return 1;
        if (!r && HCALL(c, VA_SFX_PLAY, NULL, 0x1cu)) return 1;
        if (HCALL(c, VA_HDD_RADIO, NULL, hr32(c, VA_HDD))) return 1;
        uint32_t g = hr32(c, VA_HDD);
        if (!hr32(c, g + 0x766u)) {
            hw32(c, g + 0x766u, hr32(c, s + 0x137u));
            hw16(c, g + 0x786u, hr16(c, hr32(c, s) + 0x1du));
            hw32(c, g + 0x782u, 0);
        }
        hw32(c, s + 0x14au, m);
        *eax = g;
        return 0;
    }
    uint32_t port = 0;
    return HCALL0(c, VA_PORT_A, &port) || HCALL(c, VA_PORT_DROP, NULL, port, m) || HCALL(c, VA_PORT_NEXT, eax, port, m);
}
void es2_sim_service_radio_start(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t eax = 0;
    if (radio_start(&c, sc->slots[0], &eax)) hud_failed(sc);
    else sc->eax = eax;
}
/* Original: DBSIM.EXE 0x44b5e8(M) (listing 0x44b5e8..0x44b61d): the port's
 * end callback (0x4355dc): w = wingman_index([M+2]) (hook); unless -1
 * (EAX -1) byte S+0x1c <- 0 of w's record S (EAX S). */
void es2_sim_service_radio_end(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t w = 0;
    if (HCALL(&c, VA_WINGMAN_INDEX, &w, hr32(&c, sc->slots[0] + 2u))) { hud_failed(sc); return; }
    if (w == 0xffffffffu) { sc->eax = w; return; }
    uint32_t s = radio_record(&c, w);
    hw8(&c, s + 0x1cu, 0);
    sc->eax = s;
}

/* Original: DBSIM.EXE 0x44ba54(H, L) (listing 0x44ba54..0x44bac3): the
 * HDD's legend label. m = word H+0x55c: 0: colour word [0x4d3b88], text
 * [0x49b0b8]; 4: [0x4d3b9a], [0x49b0b4]; else the colour 0x434280(sx16 m
 * - 1) (its AX) and [0x49b0b4] (read after the call). [L] <- the text,
 * L+0x1d <- sx16 colour; 0x438920(L, [H + 0x548 + 4 sx16 word H+0x55c])
 * (EAX its). */
static int hdd_legend(hud_ctx *c, uint32_t h, uint32_t l, uint32_t *eax) {
    uint16_t m = hr16(c, h + 0x55cu);
    uint32_t colour, text;
    if (m == 0u) {
        colour = hr16(c, 0x004d3b88u);
        text = hr32(c, 0x0049b0b8u);
    } else if (m == 4u) {
        colour = hr16(c, 0x004d3b9au);
        text = hr32(c, 0x0049b0b4u);
    } else {
        if (HCALL(c, VA_LEGEND_COLOUR, &colour, hsx16(m) - 1u)) return 1;
        text = hr32(c, 0x0049b0b4u);
    }
    hw32(c, l, text);
    hw32(c, l + 0x1du, hsx16((uint16_t)colour));
    return HCALL(c, VA_LABEL, eax, l, hr32(c, h + 0x548u + 4u * hsx16(hr16(c, h + 0x55cu))));
}
void es2_sim_service_hdd_legend(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t eax = 0;
    if (hdd_legend(&c, sc->slots[0], sc->slots[1], &eax)) hud_failed(sc);
    else sc->eax = eax;
}

/* Original: DBSIM.EXE 0x44be18(B) (listing 0x44be18..0x44be74), slot +0xc
 * of the HDD button table 0x49d60c: with B visible (panel_visible, AL),
 * by the state byte B+0x1f: 1: the kinds [B+0x28] 10..12 (unsigned
 * [B+0x28] - 10 below 3) draw through slot 0 of [B+0x17] (B), the others
 * copy B's rectangle (0x43122c(0, B, 0)); 2: slot 0 (B); after either the
 * byte B+0x1f is decremented; other states nothing. EAX is the last
 * callee's and not part of the contract (the slot's callers ignore it). */
static int hdd_button_redraw(hud_ctx *c, uint32_t b) {
    int yes;
    if (visible(c, b, &yes)) return 1;
    if (!yes) return 0;
    uint8_t st = hr8(c, b + 0x1fu);
    if (st == 1u) {
        if (hr32(c, b + 0x28u) - 0xau < 3u) {
            if (hud_vcall(c, b + 0x17u, 0u, (const uint32_t[]){b}, 1, NULL)) return 1;
        } else if (HCALL(c, VA_PANEL_COPY, NULL, 0u, b, 0u))
            return 1;
    } else if (st == 2u) {
        if (hud_vcall(c, b + 0x17u, 0u, (const uint32_t[]){b}, 1, NULL)) return 1;
    } else
        return 0;
    dec8(c, b + 0x1fu);
    return 0;
}
void es2_sim_service_hdd_button_redraw(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (hdd_button_redraw(&c, sc->slots[0])) hud_failed(sc);
}
/* Original: DBSIM.EXE 0x44be78(B, x) (listing 0x44be78..0x44beb8), slot
 * +8 of the HDD button table: with byte B+0x40 set the kinds [B+0x28] 0
 * and 1 do nothing; else byte B+0x40 ^= 1, the owner O = [B+0x24] gets
 * slot 0 of [O] (O, B, x), then slot +4 of [B+0x17] (B), then slot +8 of
 * [B+0x20] (B+0x20), each table read just before its call. EAX not part
 * of the contract. */
static int hdd_button_press(hud_ctx *c, uint32_t b, uint32_t x) {
    if (hr8(c, b + 0x40u) && hr32(c, b + 0x28u) < 2u) return 0;
    hw8(c, b + 0x40u, (uint8_t)(hr8(c, b + 0x40u) ^ 1u));
    uint32_t o = hr32(c, b + 0x24u);
    if (hud_call(c, hr32(c, hr32(c, o)), (const uint32_t[]){o, b, x}, 3, NULL)) return 1;
    if (hud_call(c, hr32(c, hr32(c, b + 0x17u) + 4u), (const uint32_t[]){b}, 1, NULL)) return 1;
    return hud_call(c, hr32(c, hr32(c, b + 0x20u) + 8u), (const uint32_t[]){b + 0x20u}, 1, NULL);
}
void es2_sim_service_hdd_button_press(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (hdd_button_press(&c, sc->slots[0], sc->slots[1])) hud_failed(sc);
}

/* Original: DBSIM.EXE 0x447658(W, f) (listing 0x447658..0x447677), slot +8
 * of the MFD select gadget table 0x49cee0: with bit 0 of f mfd_click([W+
 * 0x24], W, 0x4d1de3) (hook; EAX its), else EAX W. */
void es2_sim_service_mfd_select_press(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t w = sc->slots[0], eax = w;
    if ((sc->slots[1] & 1u) && HCALL(&c, VA_MFD_CLICK, &eax, hr32(&c, w + 0x24u), w, 0x004d1de3u)) { hud_failed(sc); return; }
    sc->eax = eax;
}
/* Original: DBSIM.EXE 0x448b58(W) (listing 0x448b58..0x448b63), slot +4 of
 * the throttle sliders' tables 0x49d12c (and at 0x49d1a0): byte W+0x1f <-
 * 2 (the redraw request); EAX W. */
void es2_sim_service_throttle_dirty(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    hw8(&c, sc->slots[0] + 0x1fu, 2u);
    sc->eax = sc->slots[0];
}

/* signed min / max of a dword and a value */
static uint32_t smin(uint32_t a, uint32_t b) { return (int32_t)a < (int32_t)b ? a : b; }
static uint32_t smax(uint32_t a, uint32_t b) { return (int32_t)a > (int32_t)b ? a : b; }
/* One edge from the slider's rectangle record 0x4390b8([W+0x7e]) at +k
 * against the dword W+o: the record's value when it passes (lower for a
 * minimum, higher for a maximum; the record is fetched a second time
 * then, as the original does), else W+o. */
static int record_edge(hud_ctx *c, uint32_t w, uint32_t k, uint32_t o, int minimum, uint32_t *out) {
    uint32_t p = 0;
    if (HCALL(c, VA_SLIDER_RECT, &p, hr32(c, w + 0x7eu))) return 1;
    int32_t v = (int32_t)hr32(c, p + k), mine = (int32_t)hr32(c, w + o);
    if (minimum ? v < mine : v > mine) {
        if (HCALL(c, VA_SLIDER_RECT, &p, hr32(c, w + 0x7eu))) return 1;
        *out = hr32(c, p + k);
    } else
        *out = hr32(c, w + o);
    return 0;
}
/* Original: DBSIM.EXE 0x4481c4(W) (listing 0x4481c4..0x44839e) and
 * 0x448738(W) (listing 0x448738..0x4488f2): slot +0xc of the vertical
 * (0x49d12c) and the horizontal (0x49d0e8) throttle slider, the redraw.
 * Only with byte W+0x1f 2 and W visible (AL). GC = [0x6c5f70] and the view
 * V = [0x6c5ed0] at entry; 0x488143(W+0x5a, W+0x6e, [W+0x52]) (the old
 * knob's background back); W+0x7a <- [[W+0x42]+0xb1] and W+0x4a <- it
 * when it differs. The damage rectangle r (frame EBP-0x14: x0, y0, x1,
 * y1), all signed:
 *  vertical: y0 = min([[W+0x7e]+0x18], [W+0x32]), y1 = max([[W+0x82]
 *  +0x1c], [W+0x3a]), x0 = min(+0 of 0x4390b8([W+0x7e]), [W+0x2e]), x1 =
 *  max(+8 of it, [W+0x36]); then W+0x32 <- sx16 word W+0x28 - sx16 word
 *  W+0x2c, W+0x3a <- sx16 word W+0x28, y0 = min(y0, [W+0x32]), y1 =
 *  max(y1, [W+0x3a]);
 *  horizontal: x0 = min([[W+0x82]+0x18], [W+0x2e]), x1 = max([[W+0x7e]
 *  +0x1c], [W+0x36]), y0 = min(+4 of the record, [W+0x32]), y1 = max(+0xc,
 *  [W+0x3a]); W+0x2e <- sx16 W+0x28, W+0x36 <- it + sx16 W+0x2c, x0 =
 *  min(x0, [W+0x2e]), x1 = max(x1, [W+0x36]).
 * The record 0x4390b8 is called once per edge and again when it wins.
 * Then V+0x38 <- [V+0x3c] (the page) and, when V is still [0x6c5ed0],
 * [0x4a3686] <- it and the driver slot [0x4a57ec](page); W+0x5e..+0x6d <-
 * W+0x2e..+0x3d, both corners plus GC+0x220/+0x224; 0x488012(W+0x5a,
 * [W+0x52]) (the new knob's background saved); the knob 0x4816a8([W+0x4e],
 * W+0x2e, f) with f = 2 when (0xa0 shl (word [0x4d253a] mod 32)) is below
 * [W+0x2e] (signed; vertical only), 0 for the horizontal; W+0x6e/+0x72 <-
 * W+0x2e/+0x32; 0x43122c(1, r, 0); byte W+0x1f <- 0. EAX the last
 * callee's (the early returns leave the original's AL arithmetic; not
 * part of the contract: the slot's callers ignore it). */
static int slider_redraw(hud_ctx *c, uint32_t w, int vertical) {
    if (hr8(c, w + 0x1fu) != 2u) return 0;
    int yes;
    if (visible(c, w, &yes)) return 1;
    if (!yes) return 0;
    uint32_t gc = hr32(c, VA_GC), view = hr32(c, VA_VIEW);
    if (HCALL(c, VA_SAVE_UNDER, NULL, w + 0x5au, w + 0x6eu, hr32(c, w + 0x52u))) return 1;
    uint32_t value = hr32(c, hr32(c, w + 0x42u) + 0xb1u);
    hw32(c, w + 0x7au, value);
    if (value != hr32(c, w + 0x4au)) hw32(c, w + 0x4au, value);
    uint32_t x0, y0, x1, y1;
    int32_t pos = (int16_t)hr16(c, w + 0x28u);
    if (vertical) {
        y0 = smin(hr32(c, hr32(c, w + 0x7eu) + 0x18u), hr32(c, w + 0x32u));
        y1 = smax(hr32(c, hr32(c, w + 0x82u) + 0x1cu), hr32(c, w + 0x3au));
        if (record_edge(c, w, 0u, 0x2eu, 1, &x0) || record_edge(c, w, 8u, 0x36u, 0, &x1)) return 1;
        hw32(c, w + 0x32u, (uint32_t)(pos - (int16_t)hr16(c, w + 0x2cu)));
        hw32(c, w + 0x3au, (uint32_t)pos);
        y0 = smin(y0, hr32(c, w + 0x32u));
        y1 = smax(y1, (uint32_t)pos);
    } else {
        x0 = smin(hr32(c, hr32(c, w + 0x82u) + 0x18u), hr32(c, w + 0x2eu));
        x1 = smax(hr32(c, hr32(c, w + 0x7eu) + 0x1cu), hr32(c, w + 0x36u));
        if (record_edge(c, w, 4u, 0x32u, 1, &y0) || record_edge(c, w, 0xcu, 0x3au, 0, &y1)) return 1;
        hw32(c, w + 0x2eu, (uint32_t)pos);
        uint32_t end = (uint32_t)(pos + (int16_t)hr16(c, w + 0x2cu));
        hw32(c, w + 0x36u, end);
        x0 = smin(x0, hr32(c, w + 0x2eu));
        x1 = smax(x1, end);
    }
    uint32_t page = hr32(c, view + 0x3cu);
    hw32(c, view + 0x38u, page);
    if (view == hr32(c, VA_VIEW)) {
        hw32(c, VA_DRIVER_PAGE, hr32(c, view + 0x38u));
        if (hud_call(c, hr32(c, VA_DRIVER_SLOT), (const uint32_t[]){page}, 1, NULL)) return 1;
    }
    for (uint32_t k = 0; k < 4u; ++k) hw32(c, w + 0x5eu + 4u * k, hr32(c, w + 0x2eu + 4u * k));
    for (uint32_t corner = 0; corner < 2u; ++corner)
        for (uint32_t k = 0; k < 2u; ++k) {
            uint32_t at = w + 0x5eu + 8u * corner + 4u * k;
            hw32(c, at, hr32(c, at) + hr32(c, gc + 0x220u + 4u * k));
        }
    if (HCALL(c, VA_SAVE_BACK, NULL, w + 0x5au, hr32(c, w + 0x52u))) return 1;
    uint32_t flag = 0;
    if (vertical) flag = (int32_t)(0xa0u << (hr16(c, VA_SHIFT_X) & 31u)) < (int32_t)hr32(c, w + 0x2eu) ? 2u : 0u;
    if (HCALL(c, VA_KNOB_DRAW, NULL, hr32(c, w + 0x4eu), w + 0x2eu, flag)) return 1;
    hw32(c, w + 0x6eu, hr32(c, w + 0x2eu));
    hw32(c, w + 0x72u, hr32(c, w + 0x32u));
    uint32_t top = c->l->stack_hi, r = hud_frame_push(c, 0x10u);
    if (!r) return hud_refuse(c, vertical ? 0x004481c4u : 0x00448738u, "throttle slider redraw", "the own code's call area is exhausted");
    hw32(c, r, x0);
    hw32(c, r + 4u, y0);
    hw32(c, r + 8u, x1);
    hw32(c, r + 0xcu, y1);
    int bad = HCALL(c, VA_PANEL_COPY, NULL, 1u, r, 0u);
    hud_frame_pop(c, top);
    if (bad) return 1;
    hw8(c, w + 0x1fu, 0);
    return 0;
}
void es2_sim_service_throttle_redraw_v(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (slider_redraw(&c, sc->slots[0], 1)) hud_failed(sc);
}
void es2_sim_service_throttle_redraw_h(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (slider_redraw(&c, sc->slots[0], 0)) hud_failed(sc);
}

/* 0x434260(n) (listing 0x434260..0x43427c): the callsign digit of a pilot
 * index, n / 4 + 1 (signed, rounded towards zero: add 3 before the shift when
 * negative), 3 replaced by 4. EAX the digit; EDX and ECX as at the call. */
void es2_sim_service_callsign_digit_434260(es2_sim_service_call *sc, void *user) {
    (void)user;
    uint32_t v = sc->slots[0];
    if ((int32_t)v < 0) v += 3u;
    uint32_t d = (uint32_t)((int32_t)v >> 2) + 1u;
    sc->eax = d == 3u ? 4u : d;
}
