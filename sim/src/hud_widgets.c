/* The HUD message queue and ports, and the gauge blink of the widget library,
 * as own code (hud.md, "the message queue, the ports and the gauge blink";
 * coupling sites hud.md, "the HUD line contract — coupling set and frame
 * writes"). Every function names its listing range of the reference DBSIM.EXE
 * (llvm-objdump -d -M intel, SHA-256 8561aa47...c4ad).
 *
 * The queue (base b = port + 4): ten entries of 0x31 bytes at b, the order
 * list (dwords, entry indices) at b+0x1ea, the used bytes at b+0x212 and
 * the count word at b+0x21c. A port holds the current entry at +0x49a, the
 * shown byte +0x49e, the bitmap +0x4a3, the box +0x4a7 (four dwords), the
 * start callbacks +0x4b9 (count byte +0x4b7), the hide callbacks +0x4c1
 * (count byte +0x4b8), the state bytes +0x4c9 (started), +0x4ca (done),
 * +0x4cb (cancelled), the pause clock +0x4cc and the bitmaps' pixels
 * +0x4d3/+0x4d7.
 *
 * Kept: every clock read (0x467828) and every stream-B draw (0x492ee0 on
 * 0x4d260b) at its site and in its order (0x434ecd, 0x4351f6, 0x435635,
 * 0x436f78, 0x438bde; 0x435cba, 0x436a6c, 0x438d84), the read-back
 * arithmetic of the entry times, the original's slips (the shift of
 * 0x434e8c's priority insert loses the last order entry; 0x4351c0 clears
 * the used byte of list[list[9]]; 0x4351ec never expires order entry 0;
 * 0x435c48's index compare with -1 never matches). The debug sprintf
 * blocks of 0x434e8c, 0x435074, 0x4350d4, 0x435140 and 0x435610 are jumped
 * over in the original and are not reproduced. Every call of other DBSIM
 * code (the wingman index 0x433134, the phrase table 0x453a10/0x453a4c,
 * the canvas 0x4311e0/0x431210, the restore and scroll blits 0x488143/
 * 0x487e60, the display 0x429820, the driver slots, the vtable slots and
 * the callbacks, the voice tables' 0x471de0..0x471f24) goes through the
 * dispatch. No pixels are drawn here. */
#include "hud_internal.h"

#define VA_PAUSED_TOTAL 0x004d1294u   /* the clock spent paused */
#define VA_EVENT_CLOCK 0x004d1290u
#define VA_SCALE 0x004d253au          /* word: the resolution shift */
#define VA_GLOBALS 0x004d24e6u        /* -> the globals; +0x99: the end-all byte */
#define VA_HOLD 0x004cf99cu           /* -> a record; +0x1c: no port redraw */
#define VA_SURFACE 0x006c5ed0u
#define VA_PAGE_A 0x004a3686u
#define VA_PAGE_B 0x004a368au
#define VA_DRV_EC 0x004a57ecu
#define VA_DRV_F0 0x004a57f0u
#define VA_DRV_28 0x004a5828u
#define VA_DRV_24 0x004a5824u
#define VA_DEPTH 0x0049dd8cu          /* word: open dialogs */
#define VA_PICK 0x004d0ce6u           /* the picked descriptor: dword, byte, dword */
#define VA_WINGMAN_INDEX 0x00433134u
#define VA_PHRASE_NEXT 0x00453a10u
#define VA_PHRASE_ROW 0x00453a4cu
#define VA_CANVAS_PUSH 0x004311e0u
#define VA_CANVAS_POP 0x00431210u
#define VA_RESTORE 0x00488143u
#define VA_SCROLL 0x00487e60u
#define VA_DISPLAY_CURRENT 0x00429820u
#define VA_REDRAW 0x00437164u
#define VA_VOICE_BUSY 0x00471f24u
#define VA_VOICE_SELECT 0x00471de0u
#define VA_VOICE_START 0x00471e10u
#define VA_VOICE_STEP 0x00471e88u

#define Q_LIST 0x1eau
#define Q_USED 0x212u
#define Q_COUNT 0x21cu
#define Q_ENTRY 0x31u

static int32_t q_count(hud_ctx *c, uint32_t b) { return (int32_t)hsx16(hr16(c, b + Q_COUNT)); }
static uint32_t q_list(hud_ctx *c, uint32_t b, uint32_t j) { return hr32(c, b + Q_LIST + 4u * j); }
static uint32_t q_at(uint32_t b, uint32_t slot) { return b + Q_ENTRY * slot; }
static void add32(hud_ctx *c, uint32_t a, uint32_t v) { hw32(c, a, hr32(c, a) + v); }

/* ---- the queue */
/* 0x435140(b, i) (listing 0x435140..0x4351be): with entries, the used byte
 * of list[i] cleared, the list moved down over i, the count decremented.
 * EAX: b+0x1ee+4k at the loop's end (with no entries: unchanged). */
static void q_remove(hud_ctx *c, uint32_t b, uint32_t i, uint32_t *eax) {
    if (!hr16(c, b + Q_COUNT)) return;
    hw8(c, b + q_list(c, b, i) + Q_USED, 0);
    uint32_t k = i;
    while ((int32_t)k < q_count(c, b) - 1) {
        hw32(c, b + Q_LIST + 4u * k, hr32(c, b + Q_LIST + 4u + 4u * k));
        ++k;
    }
    hw16(c, b + Q_COUNT, (uint16_t)(hr16(c, b + Q_COUNT) - 1u));
    if (eax) *eax = b + 0x1eeu + 4u * k;
}
/* 0x4351c0(b) (listing 0x4351c0..0x4351e3): the used byte of
 * list[list[9]] cleared (the original's slip: list[9] is an entry index,
 * not an order position), the count decremented. */
static void q_drop(hud_ctx *c, uint32_t b) {
    uint32_t k = hr32(c, b + 0x20eu);
    hw8(c, b + q_list(c, b, k) + Q_USED, 0);
    hw16(c, b + Q_COUNT, (uint16_t)(hr16(c, b + Q_COUNT) - 1u));
}
/* 0x434e8c(b, rec, desc) (listing 0x434e8c..0x435072): a full queue drops
 * one entry first; the first free entry takes the record and the row
 * desc+5 points to, its times in 1/60 s from the row (the two last added
 * to the clock read at 0x434ecd); then the priority insert: before the
 * first order entry whose priority byte (+0x17) is greater than the row's,
 * the list moved up (from count-1 down to j+1: the old last entry is lost,
 * the original's slip). No free entry: nothing. */
static int q_insert(hud_ctx *c, uint32_t b, uint32_t rec, uint32_t desc) {
    if (hr16(c, b + Q_COUNT) == 10u) q_drop(c, b);
    uint32_t row = hr32(c, desc + 5u), found = 0xffffffffu, i;
    for (i = 0; i < 10u; ++i) {
        if (hr8(c, b + Q_USED + i)) continue;
        uint32_t now = 0;
        if (hud_clock(c, 0x00434ecdu, &now)) return 1;
        found = i;
        hw8(c, b + Q_USED + i, 1);
        uint32_t e = q_at(b, i);
        hw32(c, e + 2u, hr32(c, rec + 2u));
        hw32(c, e + 0xeu, hr32(c, rec + 0xeu));
        hw32(c, e + 6u, hr32(c, rec + 6u));
        hw32(c, e + 0xau, hr32(c, rec + 0xau));
        hw32(c, e + 0x12u, hr32(c, rec + 0x12u));
        hw32(c, e + 0x18u, hr32(c, desc));
        hw16(c, e, hr8(c, row));
        hw8(c, e + 0x16u, hr8(c, row + 1u));
        hw8(c, e + 0x17u, hr8(c, row + 2u));
        static const uint8_t field[4] = {0x1cu, 0x20u, 0x24u, 0x28u};
        for (unsigned f = 0; f < 4u; ++f) {
            hw32(c, e + field[f], hr8(c, row + 3u + f));
            hw32(c, e + field[f], hr32(c, e + field[f]) * 60u);
            if (f >= 2u) add32(c, e + field[f], now);
        }
        for (unsigned f = 0; f < 5u; ++f) hw8(c, e + 0x2cu + f, hr8(c, row + 7u + f));
        break;
    }
    if (found == 0xffffffffu) return 0;
    int32_t priority = hr8(c, row + 2u);
    uint32_t j = 0;
    for (; (int32_t)j < q_count(c, b); ++j) {
        if (priority >= (int32_t)hr8(c, b + Q_ENTRY * q_list(c, b, j) + 0x17u)) continue;
        int32_t k = q_count(c, b) - 1;
        while ((int32_t)j < k) {
            hw32(c, b + Q_LIST + 4u * (uint32_t)k, hr32(c, b + 0x1e6u + 4u * (uint32_t)k));
            --k;
        }
        break;
    }
    hw32(c, b + Q_LIST + 4u * j, found);
    hw16(c, b + Q_COUNT, (uint16_t)(hr16(c, b + Q_COUNT) + 1u));
    return 0;
}
/* 0x435074(b) (listing 0x435074..0x4350d0): the first ordered entry, or 0. */
static uint32_t q_head(hud_ctx *c, uint32_t b) {
    return hr16(c, b + Q_COUNT) ? b + Q_ENTRY * hr32(c, b + Q_LIST) : 0u;
}
/* 0x4350d4(b, i) (listing 0x4350d4..0x43513e): ordered entry i (signed
 * compare with the count), or 0. */
static uint32_t q_entry(hud_ctx *c, uint32_t b, uint32_t i) {
    return (int32_t)i < q_count(c, b) ? b + Q_ENTRY * q_list(c, b, i) : 0u;
}
/* 0x4351ec(b) (listing 0x4351ec..0x43523d): the clock (site 0x4351f6);
 * from the last order position down to 1 (not 0) every entry whose end
 * time (+0x28) is below it (unsigned) is removed. */
static int q_expire(hud_ctx *c, uint32_t b) {
    uint32_t now = 0;
    if (hud_clock(c, 0x004351f6u, &now)) return 1;
    for (int32_t k = q_count(c, b) - 1; k > 0; --k)
        if (hr32(c, q_at(b, q_list(c, b, (uint32_t)k)) + 0x28u) < now) q_remove(c, b, (uint32_t)k, NULL);
    return 0;
}
/* 0x435240(b, key, code) (listing 0x435240..0x435295): the first ordered
 * entry with word code and key +2 is removed: AL 1 (the upper bytes from
 * 0x435140), else EAX 0. */
static uint32_t q_remove_key(hud_ctx *c, uint32_t b, uint32_t key, uint32_t code) {
    for (uint32_t j = 0; (int32_t)j < q_count(c, b); ++j) {
        uint32_t e = q_at(b, q_list(c, b, j));
        if (hr16(c, e) != (uint16_t)code || hr32(c, e + 2u) != key) continue;
        uint32_t eax = b;
        q_remove(c, b, j, &eax);
        return (eax & ~0xffu) | 1u;
    }
    return 0;
}

/* ---- the ports */
/* 0x435ac8(p, key, code) (listing 0x435ac8..0x435b13): the current entry
 * matching: cancelled (+0x4cb) and AL 1 unless started, then EAX 0; else
 * the queue's 0x435240. */
static uint32_t port_cancel(hud_ctx *c, uint32_t p, uint32_t key, uint32_t code) {
    uint32_t e = hr32(c, p + 0x49au);
    if (e && hr16(c, e) == (uint16_t)code && hr32(c, e + 2u) == key) {
        if (hr8(c, p + 0x4c9u)) return 0;
        hw8(c, p + 0x4cbu, 1);
        return (e & ~0xffu) | 1u;
    }
    return q_remove_key(c, p + 4u, key, code);
}
/* 0x435b14(p, e) (listing 0x435b14..0x435b36): e current and started:
 * done (+0x4ca). */
static void port_done(hud_ctx *c, uint32_t p, uint32_t e) {
    if (e == hr32(c, p + 0x49au) && hr8(c, p + 0x4c9u)) hw8(c, p + 0x4cau, 1);
}
/* 0x435610(p) (listing 0x435610..0x43591f): the port's step. Paused
 * (+0x4cc): nothing. Else the queue expires, the clock (site 0x435635);
 * no current entry: the head becomes it. Not done and not cancelled: past
 * the end time the entry leaves (the shown and hidden bytes cleared), past
 * the start time the port starts once (the start callbacks with the
 * entry). Done or cancelled: a started, not cancelled entry asks slot +4
 * and on yes moves its show times by the clock; else the end is due when
 * cancelled, past the show end (+0x20) while shown, past +0x1c while shown
 * with more than one entry, or with the globals' end-all byte; slot +0xc
 * agrees and the entry leaves. */
static int port_frame(hud_ctx *c, uint32_t p) {
    if (hr32(c, p + 0x4ccu)) return 0;
    uint32_t b = p + 4u, now = 0, al = 0;
    if (q_expire(c, b)) return 1;
    if (hud_clock(c, 0x00435635u, &now)) return 1;
    if (!hr32(c, p + 0x49au)) hw32(c, p + 0x49au, q_head(c, b));
    if (!hr32(c, p + 0x49au)) return 0;
    if (!hr8(c, p + 0x4cau) && !hr8(c, p + 0x4cbu)) {
        if (now > hr32(c, hr32(c, p + 0x49au) + 0x28u)) {
            q_remove(c, b, 0, NULL);
            hw32(c, p + 0x49au, 0);
            hw8(c, p + 0x49eu, 0);
            hw8(c, p + 0x4d2u, 0);
            hw8(c, p + 0x4cbu, 0);
            hw8(c, p + 0x4cau, 0);
            return 0;
        }
        if (now <= hr32(c, hr32(c, p + 0x49au) + 0x24u) || hr8(c, p + 0x4c9u)) return 0;
        hw8(c, p + 0x4c9u, 1);
        hw8(c, p + 0x4cau, 0);
        for (uint32_t i = 0; i < hr8(c, p + 0x4b7u); ++i)
            if (HCALL(c, hr32(c, p + 0x4b9u + 4u * i), NULL, hr32(c, p + 0x49au))) return 1;
        return 0;
    }
    if (hr8(c, p + 0x4c9u) && !hr8(c, p + 0x4cbu)) {
        if (HVCALL(c, p, 4u, &al, p)) return 1;
        if (!(al & 0xffu)) return 0;
        add32(c, hr32(c, p + 0x49au) + 0x1cu, now);
        add32(c, hr32(c, p + 0x49au) + 0x20u, now);
        hw8(c, p + 0x4c9u, 0);
        return 0;
    }
    if (!hr8(c, p + 0x4cbu)) {
        int end = hr8(c, p + 0x49eu) && now > hr32(c, hr32(c, p + 0x49au) + 0x20u);
        if (!end)
            end = hr8(c, p + 0x49eu) && now > hr32(c, hr32(c, p + 0x49au) + 0x1cu) && (int32_t)hsx16(hr16(c, p + 0x220u)) > 1;
        if (!end && !hr8(c, hr32(c, VA_GLOBALS) + 0x99u)) return 0;
    }
    if (HVCALL(c, p, 0xcu, &al, p)) return 1;
    if (!(al & 0xffu)) return 0;
    q_remove(c, b, 0, NULL);
    hw32(c, p + 0x49au, 0);
    hw8(c, p + 0x4cbu, 0);
    hw8(c, p + 0x4cau, 0);
    hw8(c, p + 0x4c9u, 0);
    return 0;
}
/* 0x435a74(p, table, desc, n) (listing 0x435a74..0x435ac4): n (a byte)
 * zero: desc. Else 0x453a10(table, [desc]) n times (the loop runs while
 * the decremented byte is positive, signed); the last answer and
 * 0x453a4c(table, it) form the picked descriptor 0x4d0ce6, returned. */
static int port_pick(hud_ctx *c, uint32_t table, uint32_t desc, uint32_t n, uint32_t *eax) {
    uint8_t k = (uint8_t)n;
    if (!k) { *eax = desc; return 0; }
    uint32_t v = 0, row = 0;
    do {
        if (HCALL(c, VA_PHRASE_NEXT, &v, table, hr32(c, desc))) return 1;
        --k;
    } while ((int8_t)k > 0);
    hw32(c, VA_PICK, v);
    hw8(c, VA_PICK + 4u, 0);
    if (HCALL(c, VA_PHRASE_ROW, &row, table, v)) return 1;
    hw32(c, VA_PICK + 5u, row);
    *eax = VA_PICK;
    return 0;
}
/* The common tail of the receives: with more than one variant a stream-B
 * draw (at `site`) picks one; a descriptor with a value and a row is
 * queued. */
static int port_take(hud_ctx *c, uint32_t p, uint32_t rec, uint32_t table, uint32_t desc, uint32_t site) {
    uint8_t variants = hr8(c, desc + 4u);
    uint32_t pick = 0;
    if (variants > 1u) {
        uint32_t v = 0;
        if (hud_rng_next(c, site, &v)) return 1;
        pick = (v & 0xffffu) % variants;
    }
    if (pick & 0xffu)
        if (port_pick(c, table, desc, pick, &desc)) return 1;
    if (hr32(c, desc) && hr32(c, desc + 5u)) return q_insert(c, p + 4u, rec, desc);
    return 0;
}
/* 0x435c48(p, rec) (listing 0x435c48..0x435d0b): the DialogPort's
 * receive. Without a sender (+2) the descriptor row 0x4d08ed + 9*code
 * with the table 0x4d0444; else the sender's wingman index w (0x433134,
 * its low byte; the compare with -1 never matches) selects 0x4d0464 +
 * 387*w + 9*code and the table 0x4d0414 + 16*w. Stream B at 0x435cba. */
static int dialog_receive(hud_ctx *c, uint32_t p, uint32_t rec) {
    uint32_t code = hr8(c, rec), key = hr32(c, rec + 2u), table, desc;
    if (!key) {
        table = 0x004d0444u;
        desc = 0x004d08edu + 9u * code;
    } else {
        uint32_t w = 0;
        if (HCALL(c, VA_WINGMAN_INDEX, &w, key)) return 1;
        w &= 0xffu;
        table = 0x004d0414u + 16u * w;
        desc = 0x004d0464u + 387u * w + 9u * code;
    }
    return port_take(c, p, rec, table, desc, 0x00435cbau);
}
/* 0x436a3c(p, rec) (listing 0x436a3c..0x436aba): the EventPort's receive:
 * the row 0x4d0aaf + 9*code, the table 0x4d0454; stream B at 0x436a6c. */
static int event_receive(hud_ctx *c, uint32_t p, uint32_t rec) {
    uint32_t code = hr8(c, rec);
    return port_take(c, p, rec, 0x004d0454u, 0x004d0aafu + 9u * code, 0x00436a6cu);
}
/* The page select of 0x4361cc and 0x437164: S+slot <- page; S current:
 * the global <- S+slot and the driver's page call. */
static int page_select(hud_ctx *c, uint32_t s, uint32_t slot, uint32_t page, uint32_t global, uint32_t driver, int reread) {
    hw32(c, s + slot, page);
    if (s != hr32(c, VA_SURFACE)) return 0;
    hw32(c, global, reread ? hr32(c, s + slot) : page);
    return HCALL(c, hr32(c, driver), NULL, page);
}
/* 0x437164(p, mode) (listing 0x437164..0x43726c): a shown, not hidden
 * port's redraw: the canvas pushed, S's pages +0x38/+0x3c set to the back
 * and front page (swapped for mode 1), the driver's sync [0x4a5828], the
 * scroll 0x487e60(p+0x4a3, {0,0}), [0x4a5824] unless the display is mode
 * 4 with no dialog open, S+0x3c restored, the canvas popped. */
static int port_redraw(hud_ctx *c, uint32_t p, uint32_t mode) {
    if (!hr8(c, p + 0x49eu) || hr8(c, p + 0x4d2u)) return 0;
    uint32_t s = hr32(c, VA_SURFACE), back = hr32(c, s + 0x88u), front = hr32(c, s + 0x8cu), saved = hr32(c, s + 0x3cu);
    uint32_t first = mode == 1u ? front : back, second = mode == 1u ? back : front;
    if (HCALL0(c, VA_CANVAS_PUSH, NULL)) return 1;
    if (page_select(c, s, 0x38u, first, VA_PAGE_A, VA_DRV_EC, 1)) return 1;
    if (page_select(c, s, 0x3cu, second, VA_PAGE_B, VA_DRV_F0, 1)) return 1;
    uint32_t top = c->l->stack_hi, origin = hud_frame_push(c, 8u);
    if (!origin) return hud_refuse(c, VA_REDRAW, NULL, "the own code's call area is exhausted");
    int bad = HCALL0(c, hr32(c, VA_DRV_28), NULL) || HCALL(c, VA_SCROLL, NULL, p + 0x4a3u, origin);
    hud_frame_pop(c, top);
    if (bad) return 1;
    uint32_t d = 0;
    if (HCALL0(c, VA_DISPLAY_CURRENT, &d)) return 1;
    if ((hr32(c, d + 0x14u) != 4u || hr16(c, VA_DEPTH)) && HCALL0(c, hr32(c, VA_DRV_24), NULL)) return 1;
    if (page_select(c, s, 0x3cu, saved, VA_PAGE_B, VA_DRV_F0, 0)) return 1;
    return HCALL0(c, VA_CANVAS_POP, NULL);
}
/* 0x436130(p) (listing 0x436130..0x4361c9): the DialogPort's hide. Not
 * shown or hidden: AL 1. With [0x4cf99c]+0x1c set: EAX 0. Else the hide
 * callbacks with the entry, the canvas pushed, the save-under restored
 * (0x488143(p+0x4a3, box, [p+0x4d3])), the canvas popped, the redraw
 * 0x437164(p, 1), shown and hidden cleared, AL 1. */
static int dialog_hide(hud_ctx *c, uint32_t p, uint32_t *eax) {
    if (!hr8(c, p + 0x49eu) || hr8(c, p + 0x4d2u)) { *eax = 1; return 0; }
    if (hr8(c, hr32(c, VA_HOLD) + 0x1cu)) { *eax = 0; return 0; }
    for (uint32_t i = 0; i < hr8(c, p + 0x4b8u); ++i)
        if (HCALL(c, hr32(c, p + 0x4c1u + 4u * i), NULL, hr32(c, p + 0x49au))) return 1;
    if (HCALL0(c, VA_CANVAS_PUSH, NULL)) return 1;
    if (HCALL(c, VA_RESTORE, NULL, p + 0x4a3u, p + 0x4a7u, hr32(c, p + 0x4d3u))) return 1;
    if (HCALL0(c, VA_CANVAS_POP, NULL)) return 1;
    uint32_t r = 0;
    if (HCALL(c, VA_REDRAW, &r, p, 1)) return 1;
    hw8(c, p + 0x49eu, 0);
    hw8(c, p + 0x4d2u, 0);
    *eax = (r & ~0xffu) | 1u;
    return 0;
}
/* 0x4361cc(p) (listing 0x4361cc..0x436240): the DialogPort's frame: a
 * started current entry with the auto byte (+0x2c) is done; a shown, not
 * hidden port selects S's front page into +0x38 and draws (slot +0x18);
 * then the step 0x435610. */
static int dialog_frame(hud_ctx *c, uint32_t p) {
    uint32_t e = hr32(c, p + 0x49au);
    if (e && hr8(c, e + 0x2cu) && hr8(c, p + 0x4c9u)) hw8(c, p + 0x4cau, 1);
    if (hr8(c, p + 0x49eu) && !hr8(c, p + 0x4d2u)) {
        uint32_t s = hr32(c, VA_SURFACE);
        if (page_select(c, s, 0x38u, hr32(c, s + 0x8cu), VA_PAGE_A, VA_DRV_EC, 1)) return 1;
        if (HVCALL(c, p, 0x18u, NULL, p)) return 1;
    }
    return port_frame(c, p);
}
/* 0x436f70(p) (listing 0x436f70..0x436fcd): the EventPort's frame: the
 * clock (site 0x436f78) less the paused total, times 35 << scale (IMUL ESI
 * into EDX:EAX); XOR EDX, EDX at 0x436f9a drops the high dword before the
 * DIV 0x436f9c, so the low dword is divided by 60 unsigned and the DIV never
 * faults; [0x4d1290] <- [p+0x4af] less the quotient; the draw slot +0x18; a
 * started entry is done; the step 0x435610. */
static int event_frame(hud_ctx *c, uint32_t p) {
    uint32_t now = 0;
    if (hud_clock(c, 0x00436f78u, &now)) return 1;
    uint32_t elapsed = now - hr32(c, VA_PAUSED_TOTAL), factor = 0x23u << (hr16(c, VA_SCALE) & 0x1fu);
    hw32(c, VA_EVENT_CLOCK, hr32(c, p + 0x4afu) - factor * elapsed / 60u);
    if (HVCALL(c, p, 0x18u, NULL, p)) return 1;
    if (hr8(c, p + 0x4c9u)) hw8(c, p + 0x4cau, 1);
    return port_frame(c, p);
}

/* ---- the widget library: the gauge blink */
/* 0x438d6c(g, lo, hi) (listing 0x438d6c..0x438d9a): lo when hi == lo, else
 * lo + (stream-B draw at 0x438d84, low word) mod (hi - lo), unsigned. */
static int blink_random(hud_ctx *c, uint32_t lo, uint32_t hi, uint32_t *eax) {
    if (hi == lo) { *eax = lo; return 0; }
    uint32_t v = 0;
    if (hud_rng_next(c, 0x00438d84u, &v)) return 1;
    *eax = (v & 0xffffu) % (hi - lo) + lo;
    return 0;
}
/* One half of the blink with a voice object (+0x6c): the next switch time
 * (+0x78) drawn from [lo_at, hi_at] when unset; past it, an idle voice
 * (0x471f24 zero) is given the phrase word (sign-extended) and started
 * at g+4, a busy one stepped (0x471e88(v, 0)) and, when that answers 0,
 * the state byte goes to `next` and the time is cleared. */
static int blink_voice(hud_ctx *c, uint32_t g, uint32_t now, uint32_t lo_at, uint32_t word_at, uint8_t next) {
    if (!hr32(c, g + 0x78u)) {
        uint32_t r = 0;
        if (blink_random(c, hr32(c, g + lo_at), hr32(c, g + lo_at + 4u), &r)) return 1;
        hw32(c, g + 0x78u, now + r);
        return 0;
    }
    if (now <= hr32(c, g + 0x78u)) return 0;
    uint32_t busy = 0;
    if (HCALL(c, VA_VOICE_BUSY, &busy, hr32(c, g + 0x6cu))) return 1;
    if (!busy) {
        if (HCALL(c, VA_VOICE_SELECT, NULL, hr32(c, g + 0x6cu), hsx16(hr16(c, g + word_at)))) return 1;
        return HCALL(c, VA_VOICE_START, NULL, hr32(c, g + 0x6cu), g + 4u);
    }
    uint32_t more = 0;
    if (HCALL(c, VA_VOICE_STEP, &more, hr32(c, g + 0x6cu), 0)) return 1;
    if (!(more & 0xffu)) {
        hw8(c, g + 0x76u, next);
        hw32(c, g + 0x78u, 0);
    }
    return 0;
}
/* The half without a voice: the time drawn when unset, past it the state
 * byte goes to `next`. */
static int blink_plain(hud_ctx *c, uint32_t g, uint32_t now, uint32_t lo_at, uint8_t next) {
    if (!hr32(c, g + 0x78u)) {
        uint32_t r = 0;
        if (blink_random(c, hr32(c, g + lo_at), hr32(c, g + lo_at + 4u), &r)) return 1;
        hw32(c, g + 0x78u, now + r);
        return 0;
    }
    if (now > hr32(c, g + 0x78u)) {
        hw8(c, g + 0x76u, next);
        hw32(c, g + 0x78u, 0);
    }
    return 0;
}
/* 0x438bc0(g) (listing 0x438bc0..0x438d6b): the gauge blink: the previous
 * state (+0x77) <- the state (+0x76); with the blink word +0x74 clear the
 * state is 1; else the clock (site 0x438bde) and the on half (times
 * +0x7c/+0x80, phrase +0x72, then off) or the off half (+0x84/+0x88,
 * phrase +0x70, then on). */
static int gauge_blink(hud_ctx *c, uint32_t g) {
    hw8(c, g + 0x77u, hr8(c, g + 0x76u));
    if (!hr16(c, g + 0x74u)) { hw8(c, g + 0x76u, 1); return 0; }
    uint32_t now = 0;
    if (hud_clock(c, 0x00438bdeu, &now)) return 1;
    int on = hr8(c, g + 0x76u) != 0;
    if (hr32(c, g + 0x6cu)) return blink_voice(c, g, now, on ? 0x7cu : 0x84u, on ? 0x72u : 0x70u, on ? 0u : 1u);
    return blink_plain(c, g, now, on ? 0x7cu : 0x84u, on ? 0u : 1u);
}

/* ---- the service rows */
#define ROW(name) void es2_sim_service_##name(es2_sim_service_call *sc, void *user)
#define ROW_BEGIN (void)user; HSERVE(sc, c); uint32_t eax = 0; (void)eax
#define ROW_END(expr) do { if (expr) { hud_failed(sc); return; } sc->eax = eax; } while (0)

ROW(hud_queue_insert) { ROW_BEGIN; ROW_END(q_insert(&c, sc->slots[0], sc->slots[1], sc->slots[2])); }
ROW(hud_queue_head) { ROW_BEGIN; eax = q_head(&c, sc->slots[0]); ROW_END(0); }
ROW(hud_queue_entry) { ROW_BEGIN; eax = q_entry(&c, sc->slots[0], sc->slots[1]); ROW_END(0); }
ROW(hud_queue_remove) {
    ROW_BEGIN;
    eax = sc->eax;
    q_remove(&c, sc->slots[0], sc->slots[1], &eax);
    ROW_END(0);
}
ROW(hud_queue_drop) { ROW_BEGIN; q_drop(&c, sc->slots[0]); eax = sc->slots[0]; ROW_END(0); }
ROW(hud_queue_expire) { ROW_BEGIN; ROW_END(q_expire(&c, sc->slots[0])); }
ROW(hud_queue_remove_key) { ROW_BEGIN; eax = q_remove_key(&c, sc->slots[0], sc->slots[1], sc->slots[2]); ROW_END(0); }
ROW(hud_port_step) { ROW_BEGIN; ROW_END(port_frame(&c, sc->slots[0])); }
ROW(hud_port_pick) { ROW_BEGIN; ROW_END(port_pick(&c, sc->slots[1], sc->slots[2], sc->slots[3], &eax)); }
ROW(hud_port_cancel) { ROW_BEGIN; eax = port_cancel(&c, sc->slots[0], sc->slots[1], sc->slots[2]); ROW_END(0); }
ROW(hud_port_done) {
    ROW_BEGIN;
    port_done(&c, sc->slots[0], sc->slots[1]);
    eax = sc->slots[0];
    sc->edx = sc->slots[1];
    ROW_END(0);
}
ROW(hud_dialog_receive) { ROW_BEGIN; ROW_END(dialog_receive(&c, sc->slots[0], sc->slots[1])); }
ROW(hud_dialog_hide) { ROW_BEGIN; ROW_END(dialog_hide(&c, sc->slots[0], &eax)); }
ROW(hud_dialog_frame) { ROW_BEGIN; ROW_END(dialog_frame(&c, sc->slots[0])); }
ROW(hud_event_receive) { ROW_BEGIN; ROW_END(event_receive(&c, sc->slots[0], sc->slots[1])); }
ROW(hud_event_frame) { ROW_BEGIN; ROW_END(event_frame(&c, sc->slots[0])); }
ROW(hud_port_redraw) { ROW_BEGIN; ROW_END(port_redraw(&c, sc->slots[0], sc->slots[1])); }
ROW(hud_gauge_blink) { ROW_BEGIN; ROW_END(gauge_blink(&c, sc->slots[0])); }
ROW(hud_blink_random) { ROW_BEGIN; ROW_END(blink_random(&c, sc->slots[1], sc->slots[2], &eax)); }

/* ======================================================================
 * The ports' show, text, draw and hide slots and the voice path (hud.md, "the
 * DialogPort and EventPort show, text, draw and restore"). Reached through the
 * vtables DialogPort 0x49bad4 and EventPort 0x49ba7c (no direct calls), except
 * 0x45ef9c. */
#define VA_GC_CURRENT 0x006c5f70u
#define VA_FONT 0x0049b0b4u
#define VA_HALF_WIDTH 0x004d253eu     /* dword: the screen width (halved by sar) */
#define VA_DIALOG_TEXT 0x004d0cefu
#define VA_EVENT_TEXT 0x004d0d3fu
#define VA_NO_BOX 0x004d1f3au         /* byte 1: the dialog port shows no box */
#define VA_EVENT_DIRECT 0x004d1f3bu   /* byte 1: events skip the box; non-zero: voiced */
#define VA_COLOUR_BACK 0x004d3ba2u    /* word */
#define VA_COLOUR_EDGE 0x004d3b8eu    /* word */
#define VA_WIDTH 0x004824ecu          /* gfx_string_width */
#define VA_STRING 0x00482634u         /* gfx_string */
#define VA_SAVE 0x00488012u           /* gfx_save */
#define VA_FILL 0x00486704u           /* rect_fill */
#define VA_WING_COLOUR 0x00434280u
#define VA_PANEL_VISIBLE 0x00431410u  /* panel_visible (hook) */
#define VA_VOICE 0x00462940u          /* voice */
#define VA_SFX 0x00462830u            /* sfx_play (hook) */
#define VA_VOICE_PATH 0x0045ef9cu
#define VA_STRNCAT 0x004967c0u
#define VA_LANGUAGE 0x004d2536u
#define VA_VOICE_DIR 0x0049f259u      /* the voice directory name; its byte 7 at 0x49f260 */
#define VA_PATH_OUT 0x004d293au

/* 0x45ef9c(name, ext) (listing 0x45ef9c..0x45eff9, with 0x45ef84 at
 * 0x45ef84..0x45ef9b): r = path_ext(name, ext); the language byte
 * [0x4d2536] replaces byte 7 of the directory name 0x49f259 unless it is
 * 0x72 (signed compare); the directory copied to 0x4d293a, then strcat the
 * separator 0x49f3fe and r. EAX: the last strcat's answer. */
static int voice_path(hud_ctx *c, uint32_t name, uint32_t ext, uint32_t *eax) {
    uint32_t r = 0;
    if (HCALL(c, HUD_VA_PATH_EXT, &r, name, ext)) return 1;
    uint8_t language = hr8(c, VA_LANGUAGE);
    if ((int8_t)language != 0x72) hw8(c, VA_VOICE_DIR + 7u, language);
    hud_gstrcpy(c, VA_PATH_OUT, VA_VOICE_DIR);
    if (HCALL(c, HUD_VA_STRCAT, NULL, VA_PATH_OUT, 0x0049f3feu)) return 1;
    return HCALL(c, HUD_VA_STRCAT, eax, VA_PATH_OUT, r);
}
/* A 40-byte name template copied into a call-area block (the original's
 * rep movsd into its frame); 0 when the call area is exhausted. */
static uint32_t name_block(hud_ctx *c, uint32_t from) {
    uint32_t b = hud_frame_push(c, 40u);
    if (b)
        for (uint32_t i = 0; i < 40u; i += 4u) hw32(c, b + i, hr32(c, from + i));
    return b;
}
/* The box moved by the canvas origin gc+0x220/+0x224 (sign +1) or back
 * (-1), in the original's order x0, y0, x1, y1. */
static void box_shift(hud_ctx *c, uint32_t b, uint32_t gc, int sign) {
    for (uint32_t k = 0; k < 4u; ++k) {
        uint32_t v = hr32(c, gc + 0x220u + 4u * (k & 1u));
        hw32(c, b + 4u * k, sign > 0 ? hr32(c, b + 4u * k) + v : hr32(c, b + 4u * k) - v);
    }
}
/* 0x435d0c(p, e) (listing 0x435d0c..0x435d98): the DialogPort's text. No
 * entry: the text 0x4d0cef emptied. Else the speaker's name (no sender:
 * [0x4d13ac], i.e. 0x4342b8; else 0x434298(0x433134(sender)), i.e.
 * 0x44b928([[0x49b088]+0x1f9], w) = [[0x49d230] + 334*w + 0x264], whose
 * first argument is unused) copied there, then strcat ": " (0x49ba5d) and
 * strncat the entry's text [e+0x18], 0x4a. EAX 0x4d0cef. */
static int dialog_text(hud_ctx *c, uint32_t e, uint32_t *eax) {
    *eax = VA_DIALOG_TEXT;
    if (!e) { hw8(c, VA_DIALOG_TEXT, 0); return 0; }
    uint32_t name;
    if (!hr32(c, e + 2u)) name = hr32(c, 0x004d13acu);
    else {
        uint32_t w = 0;
        if (HCALL(c, VA_WINGMAN_INDEX, &w, hr32(c, e + 2u))) return 1;
        uint32_t roster = hr32(c, 0x0049b088u);
        if (!roster) return hud_null_refuse(c, 0x004342a5u, "dialog text", "no roster record (the original reads through NULL)");
        (void)hr32(c, roster + 0x1f9u);
        name = hr32(c, hr32(c, 0x0049d230u) + 334u * w + 0x264u);
    }
    if (!name) return hud_null_refuse(c, 0x00435d43u, "dialog text", "no speaker name (the original's strcpy reads NULL)");
    hud_gstrcpy(c, VA_DIALOG_TEXT, name);
    if (HCALL(c, HUD_VA_STRCAT, NULL, VA_DIALOG_TEXT, 0x0049ba5du)) return 1;
    return HCALL(c, VA_STRNCAT, NULL, VA_DIALOG_TEXT, hr32(c, e + 0x18u), 0x4au);
}
/* 0x436a0c(p, e) (listing 0x436a0c..0x436a38): the EventPort's text:
 * strncpy(0x4d0d3f, [e+0x18], 0x50), or emptied without an entry. EAX
 * 0x4d0d3f. */
static int event_text(hud_ctx *c, uint32_t e, uint32_t *eax) {
    *eax = VA_EVENT_TEXT;
    if (!e) { hw8(c, VA_EVENT_TEXT, 0); return 0; }
    return HCALL(c, HUD_VA_STRNCPY, NULL, VA_EVENT_TEXT, hr32(c, e + 0x18u), 0x50u);
}
/* 0x435d9c(p) (listing 0x435d9c..0x43612f): the DialogPort's show. The
 * globals' end-all byte or [0x4cf99c]+0x1c: EAX 0. An entry with the voice
 * byte (+0x2c): the name template 0x49b7d0 gets the code word + 1 as two
 * decimal digits at 3/4 and the byte +0x16 + '0' at 7; its path
 * 0x45ef9c(name, 0x49ba60) is spoken (0x462940(path, 100), site 0x435e50)
 * unless name[2] is '0'. [0x4d1f3a] == 1: AL 1. Else the text slot +8,
 * shown; S's front page into +0x38; the canvas pushed; the box centred on
 * half of [0x4d253e] around the text width, 10 << scale margin each side;
 * the save-under of the box (moved to the canvas origin) into [p+0x4d3];
 * the speaker's colour (0x434280 of the wingman index, else the words
 * 0x4d3ba2/0x4d3b8e) filled as {0, colour} and {4, colour - 1} through
 * GC+0x22c; the text at the centre; the redraw 0x437164(p, 1); the box
 * saved again into [p+0x4d7]; the driver's [0x4a5824] unless the display
 * is mode 4; S+0x3c <- the front page; the canvas popped; hidden cleared;
 * AL 1. */
static int dialog_show(hud_ctx *c, uint32_t p, uint32_t *eax) {
    *eax = 0;
    if (hr8(c, hr32(c, VA_GLOBALS) + 0x99u) || hr8(c, hr32(c, VA_HOLD) + 0x1cu)) return 0;
    uint32_t top = c->l->stack_hi;
    if (hr8(c, hr32(c, p + 0x49au) + 0x2cu)) {
        uint32_t name = name_block(c, 0x0049b7d0u), path = 0;
        if (!name) return hud_refuse(c, 0x00435d9cu, NULL, "the own code's call area is exhausted");
        uint32_t n = (uint32_t)hr16(c, hr32(c, p + 0x49au)) + 1u;
        hw8(c, name + 3u, (uint8_t)(n / 10u + 0x30u));
        n = (uint32_t)hr16(c, hr32(c, p + 0x49au)) + 1u;
        hw8(c, name + 4u, (uint8_t)(n % 10u + 0x30u));
        hw8(c, name + 7u, (uint8_t)(hr8(c, hr32(c, p + 0x49au) + 0x16u) + 0x30u));
        int bad = HCALL(c, VA_VOICE_PATH, &path, name, 0x0049ba60u);
        if (!bad && (int8_t)hr8(c, name + 2u) != 0x30) bad = HSOUND(c, 0x00435e50u, VA_VOICE, NULL, path, 0x64u);
        hud_frame_pop(c, top);
        if (bad) return 1;
    }
    if (hr8(c, VA_NO_BOX) == 1u) { *eax = 1; return 0; }
    if (HVCALL(c, p, 8u, NULL, p, hr32(c, p + 0x49au))) return 1;
    hw8(c, p + 0x49eu, 1);
    uint32_t s = hr32(c, VA_SURFACE), front = hr32(c, s + 0x8cu);
    if (page_select(c, s, 0x38u, front, VA_PAGE_A, VA_DRV_EC, 1)) return 1;
    uint32_t gc = 0, font = hr32(c, VA_FONT), w = 0, b = p + 0x4a7u;
    if (HCALL0(c, VA_CANVAS_PUSH, &gc)) return 1;
    if (HCALL(c, VA_WIDTH, &w, font, VA_DIALOG_TEXT)) return 1;
    uint32_t sh = hr16(c, VA_SCALE) & 0x1fu;
    uint32_t half = (uint32_t)((int32_t)hr32(c, VA_HALF_WIDTH) >> 1), hw = (uint32_t)((int32_t)w >> 1);
    hw32(c, b, hr32(c, gc + 0x220u) + half - hw - (0xau << sh));
    hw32(c, b + 8u, hr32(c, b) + w + (0x14u << sh));
    box_shift(c, b, gc, 1);
    if (HCALL(c, VA_SAVE, NULL, p + 0x4a3u, hr32(c, p + 0x4d3u))) return 1;
    box_shift(c, b, gc, -1);
    uint32_t f = hud_frame_push(c, 0x18u);
    if (!f) return hud_refuse(c, 0x00435d9cu, NULL, "the own code's call area is exhausted");
    uint32_t pt = f, fill_a = f + 8u, fill_b = f + 0x10u;
    hw32(c, pt, hr32(c, gc + 0x220u) + half - hw);
    uint32_t b3 = hr32(c, b + 0xcu);
    hw32(c, pt + 4u, b3 - (uint32_t)((int32_t)(b3 - hr32(c, b + 4u) - hsx16(hr16(c, font + 0x16u))) >> 1));
    uint32_t wi = 0xffffffffu, colour = 0;
    int bad = 0;
    if (hr32(c, hr32(c, p + 0x49au) + 2u)) bad = HCALL(c, VA_WINGMAN_INDEX, &wi, hr32(c, hr32(c, p + 0x49au) + 2u));
    hw32(c, fill_a, 0);
    hw32(c, fill_b, 4);
    if (!bad && wi != 0xffffffffu) {
        bad = HCALL(c, VA_WING_COLOUR, &colour, wi);
        hw32(c, fill_a + 4u, colour);
        hw32(c, fill_b + 4u, colour - 1u);
    } else if (!bad) {
        hw32(c, fill_a + 4u, hsx16(hr16(c, VA_COLOUR_BACK)));
        hw32(c, fill_b + 4u, hsx16(hr16(c, VA_COLOUR_EDGE)));
    }
    if (!bad) { hw32(c, gc + 0x22cu, fill_a); bad = HCALL(c, VA_FILL, NULL, b); }
    if (!bad) { hw32(c, gc + 0x22cu, fill_b); bad = HCALL(c, VA_FILL, NULL, b); }
    if (!bad) bad = HCALL(c, VA_STRING, NULL, font, pt, VA_DIALOG_TEXT);
    hud_frame_pop(c, top);
    if (bad) return 1;
    if (HCALL(c, VA_REDRAW, NULL, p, 1)) return 1;
    box_shift(c, b, gc, 1);
    if (HCALL(c, VA_SAVE, NULL, p + 0x4a3u, hr32(c, p + 0x4d7u))) return 1;
    box_shift(c, b, gc, -1);
    uint32_t d = 0;
    if (HCALL0(c, VA_DISPLAY_CURRENT, &d)) return 1;
    if (hr32(c, d + 0x14u) != 4u && HCALL0(c, hr32(c, VA_DRV_24), NULL)) return 1;
    if (page_select(c, s, 0x3cu, front, VA_PAGE_B, VA_DRV_F0, 1)) return 1;
    if (HCALL0(c, VA_CANVAS_POP, NULL)) return 1;
    hw8(c, p + 0x4d2u, 0);
    *eax = 1;
    return 0;
}
/* 0x436abc(p) (listing 0x436abc..0x436cea): the EventPort's show. The
 * end-all byte: EAX 0. The clock (site 0x436adf). [0x4d1f3b] == 1 or the
 * display in mode 4: shown and hidden set, then the voice. Else the same
 * code word as the last shown (+0x4e8, sign-extended, against the
 * zero-extended word) within 300 ticks of +0x4e4 (unsigned): not shown,
 * cancelled, +0x4e4 <- now, EAX 0. Else +0x4e4 <- now, +0x4e8 <- the
 * code, the text slot +8, [0x4d1290] <- [p+0x4af], [0x4d1294] <- now, the
 * box <- the rest box +0x4d4, shown, not hidden, the draw slot +0x18, the
 * redraw 0x437164(p, 1) and the sound of the code: the original's switch
 * (index bytes 0x436be2 for codes 0..0x38, read from the image; above:
 * index 0) plays 0x18 (index 4, site 0x436c35), 0x19 (5, 0x436c3f) or
 * 0x1a (3: 0x436c49; 2: 0x436c53 and the blink byte +0x4d3 set; 1:
 * 0x436c64; 0: 0x436c6e). The voice ([0x4d1f3b] non-zero): the template
 * 0x49b820 with the entry's byte +0x2c as two digits at 6/7, spoken
 * through 0x45ef9c(name, 0x49ba6c) and 0x462940(path, 100) at 0x436cda.
 * AL 1. */
static int event_show(hud_ctx *c, uint32_t p, uint32_t *eax) {
    *eax = 0;
    if (hr8(c, hr32(c, VA_GLOBALS) + 0x99u)) return 0;
    uint32_t now = 0, d = 0;
    if (hud_clock(c, 0x00436adfu, &now)) return 1;
    int direct = hr8(c, VA_EVENT_DIRECT) == 1u;
    if (!direct) {
        if (HCALL0(c, VA_DISPLAY_CURRENT, &d)) return 1;
        direct = hr32(c, d + 0x14u) == 4u;
    }
    if (direct) {
        hw8(c, p + 0x49eu, 1);
        hw8(c, p + 0x4d2u, 1);
    } else {
        uint32_t e = hr32(c, p + 0x49au);
        if ((uint32_t)hr16(c, e) == hsx16(hr16(c, p + 0x4e8u)) && now < hr32(c, p + 0x4e4u) + 0x12cu) {
            hw8(c, p + 0x49eu, 0);
            hw8(c, p + 0x4cbu, 1);
            hw32(c, p + 0x4e4u, now);
            return 0;
        }
        hw32(c, p + 0x4e4u, now);
        hw16(c, p + 0x4e8u, hr16(c, hr32(c, p + 0x49au)));
        if (HVCALL(c, p, 8u, NULL, p, hr32(c, p + 0x49au))) return 1;
        hw32(c, VA_EVENT_CLOCK, hr32(c, p + 0x4afu));
        hw32(c, VA_PAUSED_TOTAL, now);
        for (uint32_t k = 0; k < 16u; k += 4u) hw32(c, p + 0x4a7u + k, hr32(c, p + 0x4d4u + k));
        hw8(c, p + 0x49eu, 1);
        hw8(c, p + 0x4d2u, 0);
        if (HVCALL(c, p, 0x18u, NULL, p)) return 1;
        if (HCALL(c, VA_REDRAW, NULL, p, 1)) return 1;
        uint32_t code = hr16(c, hr32(c, p + 0x49au)), index = code > 0x38u ? 0u : hr8(c, 0x00436be2u + code);
        static const struct { uint32_t sound, site; } play[6] = {
            {0x1au, 0x00436c6eu}, {0x1au, 0x00436c64u}, {0x1au, 0x00436c53u},
            {0x1au, 0x00436c49u}, {0x18u, 0x00436c35u}, {0x19u, 0x00436c3fu}};
        if (index > 5u) return hud_refuse(c, 0x00436bdbu, NULL, "a switch index outside the original's jump table");
        if (HSOUND(c, play[index].site, VA_SFX, NULL, play[index].sound)) return 1;
        if (index == 2u) hw8(c, p + 0x4d3u, 1);
    }
    if (hr8(c, VA_EVENT_DIRECT)) {
        uint32_t top = c->l->stack_hi, name = name_block(c, 0x0049b820u), path = 0;
        if (!name) return hud_refuse(c, 0x00436abcu, NULL, "the own code's call area is exhausted");
        uint32_t v = hr8(c, hr32(c, p + 0x49au) + 0x2cu);
        hw8(c, name + 6u, (uint8_t)(v / 10u + 0x30u));
        hw8(c, name + 7u, (uint8_t)(v % 10u + 0x30u));
        int bad = HCALL(c, VA_VOICE_PATH, &path, name, 0x0049ba6cu);
        if (!bad) bad = HSOUND(c, 0x00436cdau, VA_VOICE, NULL, path, 0x64u);
        hud_frame_pop(c, top);
        if (bad) return 1;
    }
    *eax = 1;
    return 0;
}
/* 0x436cec(p) (listing 0x436cec..0x436f6e): the EventPort's draw, inside
 * an exception frame of 0x49630c (not linked here; where the original
 * returns the saved FS:0, EAX is the calling thread's FS:0). Held
 * ([0x4cf99c]+0x1c), not shown or hidden, or the box (copied) not visible
 * (0x431410): nothing. Else a local graphics context (0x480c34; [0x6c5f70]
 * read before) clipped to the box, origin 0, +0x208/+0x20c 1, selected
 * (0x480d44); the text 0x4d0d3f placed: x centred in the box for code
 * 0x36, else [0x4d1290], less the box's x0; y the box's height less the
 * font's +0xa word, halved, plus the font's +0x16 word, plus 1; the box
 * moved to 0/0; filled with {0, [0x4d3ba2]} and {4, [0x4d3b8e]} through
 * GC+0x22c; the clip narrowed by 3 << scale left and right; the text drawn
 * unless the blink byte +0x4d3 is set and bit 5 of the clock (site
 * 0x436f27) is clear; the old context selected again, the construction
 * counter down by two; EAX: the counter pointer [0x6cbee4]. */
static int event_draw(hud_ctx *c, uint32_t p, uint32_t *eax) {
    *eax = hud_fs0();
    if (hr8(c, hr32(c, VA_HOLD) + 0x1cu) || !hr8(c, p + 0x49eu) || hr8(c, p + 0x4d2u)) return 0;
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x268u);
    if (!f) return hud_refuse(c, 0x00436cecu, NULL, "the own code's call area is exhausted");
    uint32_t r = f, pt = f + 0x10u, fill_a = f + 0x18u, fill_b = f + 0x20u, gc = f + 0x28u, visible = 0;
    for (uint32_t k = 0; k < 16u; k += 4u) hw32(c, r + k, hr32(c, p + 0x4a7u + k));
    int bad = HCALL(c, VA_PANEL_VISIBLE, &visible, r);
    if (bad || !(visible & 0xffu) || !hr8(c, p + 0x49eu) || hr8(c, p + 0x4d2u)) { hud_frame_pop(c, top); return bad; }
    uint32_t saved = hr32(c, VA_GC_CURRENT);
    if (hud_gc_construct(c, gc)) { hud_frame_pop(c, top); return 1; }
    for (uint32_t k = 0; k < 16u; k += 4u) hw32(c, gc + 0x210u + k, hr32(c, p + 0x4a7u + k));
    hw32(c, gc + 0x224u, 0);
    hw32(c, gc + 0x220u, 0);
    hw32(c, gc + 0x208u, 1);
    hw32(c, gc + 0x20cu, 1);
    hud_gc_select(c, gc);
    uint32_t font = hr32(c, VA_FONT), w = 0;
    bad = HCALL(c, VA_WIDTH, &w, font, VA_EVENT_TEXT);
    if (!bad) {
        uint32_t r0 = hr32(c, r), r1 = hr32(c, r + 4u), r2 = hr32(c, r + 8u), r3 = hr32(c, r + 0xcu);
        uint32_t x = hr16(c, hr32(c, p + 0x49au)) == 0x36u ? (uint32_t)((int32_t)(r2 - r0 - w) >> 1) + r0 : hr32(c, VA_EVENT_CLOCK);
        hw32(c, pt, x - r0);
        hw32(c, pt + 4u, (uint32_t)((int32_t)(r3 - r1 - hsx16(hr16(c, font + 0xau))) >> 1) + hsx16(hr16(c, font + 0x16u)) + 1u);
        hw32(c, r + 8u, r2 - r0);
        hw32(c, r, 0);
        hw32(c, r + 0xcu, r3 - r1);
        hw32(c, r + 4u, 0);
        hw32(c, fill_a, 0);
        hw32(c, fill_a + 4u, hsx16(hr16(c, VA_COLOUR_BACK)));
        hw32(c, fill_b, 4);
        hw32(c, fill_b + 4u, hsx16(hr16(c, VA_COLOUR_EDGE)));
        hw32(c, gc + 0x22cu, fill_a);
        bad = HCALL(c, VA_FILL, NULL, r);
    }
    if (!bad) { hw32(c, gc + 0x22cu, fill_b); bad = HCALL(c, VA_FILL, NULL, r); }
    if (!bad) {
        uint32_t d3 = 3u << (hr16(c, VA_SCALE) & 0x1fu), now = 0;
        hw32(c, gc + 0x210u, hr32(c, gc + 0x210u) + d3);
        hw32(c, gc + 0x218u, hr32(c, gc + 0x218u) - d3);
        int draw = 1;
        if (hr8(c, p + 0x4d3u)) {
            bad = hud_clock(c, 0x00436f27u, &now);
            draw = (now & 0x20u) != 0;
        }
        if (!bad && draw) bad = HCALL(c, VA_STRING, NULL, font, pt, VA_EVENT_TEXT);
    }
    if (!bad) {
        hud_gc_select(c, saved);
        hud_counter(c, -1);
        hud_counter(c, -1);
        *eax = hr32(c, HUD_VA_COUNTER_POINTER);
    }
    hud_frame_pop(c, top);
    return bad;
}
/* 0x436fd0(p) (listing 0x436fd0..0x437041): the EventPort's hide: a
 * shown, not hidden port's box filled with {0, [0x4d3ba2]} on the pushed
 * canvas, the canvas popped, the redraw 0x437164(p, 1), shown, hidden and
 * the blink byte cleared. AL 1. */
static int event_hide(hud_ctx *c, uint32_t p, uint32_t *eax) {
    *eax = 1;
    if (!hr8(c, p + 0x49eu) || hr8(c, p + 0x4d2u)) return 0;
    uint32_t gc = 0, top = c->l->stack_hi;
    if (HCALL0(c, VA_CANVAS_PUSH, &gc)) return 1;
    uint32_t f = hud_frame_push(c, 8u);
    if (!f) return hud_refuse(c, 0x00436fd0u, NULL, "the own code's call area is exhausted");
    hw32(c, f, 0);
    hw32(c, f + 4u, hsx16(hr16(c, VA_COLOUR_BACK)));
    hw32(c, gc + 0x22cu, f);
    int bad = HCALL(c, VA_FILL, NULL, p + 0x4a7u);
    hud_frame_pop(c, top);
    if (bad || HCALL0(c, VA_CANVAS_POP, NULL) || HCALL(c, VA_REDRAW, NULL, p, 1)) return 1;
    hw8(c, p + 0x49eu, 0);
    hw8(c, p + 0x4d2u, 0);
    hw8(c, p + 0x4d3u, 0);
    return 0;
}
/* 0x437044(p) (listing 0x437044..0x437091): the ports' slot +0x14: not
 * held, shown and not hidden: the save-under restored on the pushed
 * canvas (0x488143(p+0x4a3, box, [p+0x4d3])). */
static int port_restore(hud_ctx *c, uint32_t p) {
    if (hr8(c, hr32(c, VA_HOLD) + 0x1cu) || !hr8(c, p + 0x49eu) || hr8(c, p + 0x4d2u)) return 0;
    if (HCALL0(c, VA_CANVAS_PUSH, NULL)) return 1;
    if (HCALL(c, VA_RESTORE, NULL, p + 0x4a3u, p + 0x4a7u, hr32(c, p + 0x4d3u))) return 1;
    return HCALL0(c, VA_CANVAS_POP, NULL);
}
/* 0x437094(p) (listing 0x437094..0x437160): the DialogPort's draw: the
 * same gate; the box (moved to the canvas origin) saved into [p+0x4d3],
 * then the box drawn back from [p+0x4d7] (0x488143). */
static int dialog_draw(hud_ctx *c, uint32_t p) {
    if (hr8(c, hr32(c, VA_HOLD) + 0x1cu) || !hr8(c, p + 0x49eu) || hr8(c, p + 0x4d2u)) return 0;
    uint32_t gc = 0, b = p + 0x4a7u;
    if (HCALL0(c, VA_CANVAS_PUSH, &gc)) return 1;
    box_shift(c, b, gc, 1);
    if (HCALL(c, VA_SAVE, NULL, p + 0x4a3u, hr32(c, p + 0x4d3u))) return 1;
    box_shift(c, b, gc, -1);
    if (HCALL(c, VA_RESTORE, NULL, p + 0x4a3u, b, hr32(c, p + 0x4d7u))) return 1;
    return HCALL0(c, VA_CANVAS_POP, NULL);
}

ROW(hud_dialog_show) { ROW_BEGIN; ROW_END(dialog_show(&c, sc->slots[0], &eax)); }
ROW(hud_dialog_text) { ROW_BEGIN; ROW_END(dialog_text(&c, sc->slots[1], &eax)); }
ROW(hud_event_show) { ROW_BEGIN; ROW_END(event_show(&c, sc->slots[0], &eax)); }
ROW(hud_event_text) { ROW_BEGIN; ROW_END(event_text(&c, sc->slots[1], &eax)); }
ROW(hud_event_draw) { ROW_BEGIN; ROW_END(event_draw(&c, sc->slots[0], &eax)); }
ROW(hud_event_hide) { ROW_BEGIN; ROW_END(event_hide(&c, sc->slots[0], &eax)); }
ROW(hud_port_restore) { ROW_BEGIN; ROW_END(port_restore(&c, sc->slots[0])); }
ROW(hud_dialog_draw) { ROW_BEGIN; ROW_END(dialog_draw(&c, sc->slots[0])); }
ROW(hud_voice_path) { ROW_BEGIN; ROW_END(voice_path(&c, sc->slots[0], sc->slots[1], &eax)); }

/* ======================================================================
 * The CommandPort (vtable 0x49baa8: receive 0x4362e4, show 0x43660c, text
 * 0x436318, hide 0x436130, frame 0x4365d0, restore 0x437044, draw 0x437094;
 * constructed by 0x436244 when word [0x4aa728] is set), the CD-root path
 * 0x45ee60, the phrase table 0x453a10/0x453a4c and the voice select 0x471de0
 * (hud.md, "the CommandPort, the CD-root path, the phrase table and the voice
 * select"). The CommandPort keeps the font at +0x4df, the text width +0x4e3,
 * the box height +0x4e7 and the line count +0x4eb; its lines are 0x78 bytes
 * each from 0x4d0d8f. */
#define VA_CMD_LINES 0x004d0d8fu
#define VA_CMD_TABLE 0x004d0444u
#define VA_CMD_ROWS 0x004d08edu
#define VA_STRLEN 0x00496766u
#define VA_SCALE_Y 0x004d253cu        /* word: the vertical resolution shift */
#define VA_CMD_VOICE 0x004aa728u      /* word: the CommandPort's voice set */
#define VA_CD_ROOT 0x004d280cu
#define VA_CD_PATH 0x004d28acu
#define VA_CD_PATH_FN 0x0045ee60u

/* 0x453a10(t, v) (listing 0x453a10..0x453a4a): v below [t] + [t+8]
 * (unsigned): a = v + word [v-2] (sign-extended), then a + 1 + byte [a]
 * (sign-extended) + 2; else 0. */
static uint32_t phrase_next(hud_ctx *c, uint32_t t, uint32_t v) {
    if (!(hr32(c, t) + hr32(c, t + 8u) > v)) return 0;
    uint32_t a = v + hsx16(hr16(c, v - 2u));
    return a + 1u + (uint32_t)(int32_t)(int8_t)hr8(c, a) + 2u;
}
/* 0x453a4c(t, v) (listing 0x453a4c..0x453a82): the same bound; a = v +
 * word [v-2] (sign-extended); byte [a] zero: 0, else a + 1. */
static uint32_t phrase_row(hud_ctx *c, uint32_t t, uint32_t v) {
    if (!(hr32(c, t) + hr32(c, t + 8u) > v)) return 0;
    uint32_t a = v + hsx16(hr16(c, v - 2u));
    return hr8(c, a) ? a + 1u : 0u;
}
/* 0x471de0(v, n) (listing 0x471de0..0x471e0d): r = [v+0x16]; [r] >= n
 * (unsigned; n == [r] passes): [v+0x12] <- [r+4] + 8n, word [v+2] <- n,
 * AL 1 (EAX's upper bytes are v's); else EAX 0. */
static uint32_t voice_select(hud_ctx *c, uint32_t v, uint32_t n) {
    uint32_t r = hr32(c, v + 0x16u);
    if (hr32(c, r) < n) return 0;
    hw32(c, v + 0x12u, hr32(c, r + 4u) + 8u * n);
    hw16(c, v + 2u, (uint16_t)n);
    return (v & 0xffffff00u) | 1u;
}
/* 0x45ee60(path) (listing 0x45ee60..0x45eebc): the CD root 0x4d280c
 * copied to 0x4d28ac, then strcat the path without its drive (two bytes
 * skipped when path[1] is ':'). EAX 0x4d28ac. */
static int cd_path(hud_ctx *c, uint32_t path, uint32_t *eax) {
    uint32_t skip = hr8(c, path + 1u) == 0x3au ? 2u : 0u;
    hud_gstrcpy(c, VA_CD_PATH, VA_CD_ROOT);
    if (HCALL(c, HUD_VA_STRCAT, NULL, VA_CD_PATH, path + skip)) return 1;
    *eax = VA_CD_PATH;
    return 0;
}
/* 0x4362e4(p, rec) (listing 0x4362e4..0x436317): the CommandPort's
 * receive: the row d = 0x4d08ed + 9 * byte [rec]; [d] and [d+5] both
 * non-zero: 0x434e8c(p+4, rec, d). No variant draw. */
static int cmd_receive(hud_ctx *c, uint32_t p, uint32_t rec) {
    uint32_t d = VA_CMD_ROWS + 9u * hr8(c, rec);
    if (hr32(c, d) && hr32(c, d + 5u)) return q_insert(c, p + 4u, rec, d);
    return 0;
}
/* 0x436318(p, e) (listing 0x436318..0x4365cf): the CommandPort's text, the
 * entry's phrases word-wrapped into the lines at 0x4d0d8f. No entry: the
 * line count +0x4eb <- 0. Else the row d = 0x4d08ed + 9 * word [e] (zero-
 * extended): n = byte d+4 phrases from [d], each next one 0x453a10(0x4d0444,
 * phrase); the limit 0x50, 0x3c when word [0x4d253a] is zero; count 0, the
 * first line emptied, length l = 0, line k = 0. Per phrase of length len
 * (strlen 0x496766), all compares signed:
 *   len + l < limit: a non-empty line gets " " (0x49ba64) appended, an empty
 *   one counts; the phrase appended; l += len + 1.
 *   else the characters i = 0..len-1: at a space or the last character,
 *   i + l < limit and not the last character remembers the split s =
 *   phrase + i, j = i; otherwise with a split: " " (0x49ba66) and strncat
 *   (phrase, j) onto the line, an empty line (l == 0) counts, l += j, s
 *   past the space, and l above the longest m: m <- l - 1, longest <- the
 *   line; without one: s = the phrase. Then the next line k+1 <- s (strcpy),
 *   l = len - j, count + 1, and the character walk ends.
 *   l above m: m <- l - 1, longest <- line k.
 * The height +0x4e7 <- (8 << [0x4d253c]) * (count + 1); the width +0x4e3 <-
 * 0x4824ec([p+0x4df], longest), 0 without one. EAX 0x4d0d8f. */
static int cmd_text(hud_ctx *c, uint32_t p, uint32_t e, uint32_t *eax) {
    uint32_t longest = 0;
    int32_t m = 0;
    if (!e) hw32(c, p + 0x4ebu, 0);
    else {
        uint32_t d = VA_CMD_ROWS + 9u * hr16(c, e);
        int32_t n = hr8(c, d + 4u), limit = hr16(c, VA_SCALE) ? 0x50 : 0x3c, l = 0;
        uint32_t phrase = hr32(c, d), k = 0;
        hw32(c, p + 0x4ebu, 0);
        hw8(c, VA_CMD_LINES, 0);
        for (int32_t i = 0; i < n; ++i) {
            uint32_t len32 = 0, line = VA_CMD_LINES + 0x78u * k;
            if (HCALL(c, VA_STRLEN, &len32, phrase)) return 1;
            int32_t len = (int32_t)len32;
            if (len + l < limit) {
                if (hr8(c, line)) { if (HCALL(c, HUD_VA_STRCAT, NULL, line, 0x0049ba64u)) return 1; }
                else add32(c, p + 0x4ebu, 1);
                if (HCALL(c, HUD_VA_STRCAT, NULL, line, phrase)) return 1;
                l += len + 1;
            } else {
                uint32_t s = 0;
                int32_t j = 0;
                for (int32_t ch = 0; ch < len; ++ch) {
                    if (hr8(c, phrase + (uint32_t)ch) != 0x20u && ch != len - 1) continue;
                    if (ch + l < limit && ch != len - 1) { s = phrase + (uint32_t)ch; j = ch; continue; }
                    if (s) {
                        if (HCALL(c, HUD_VA_STRCAT, NULL, line, 0x0049ba66u) ||
                            HCALL(c, VA_STRNCAT, NULL, line, phrase, (uint32_t)j)) return 1;
                        if (!l) add32(c, p + 0x4ebu, 1);
                        l += j;
                        ++s;
                        if (l > m) { m = l - 1; longest = line; }
                    } else s = phrase;
                    ++k;
                    hud_gstrcpy(c, VA_CMD_LINES + 0x78u * k, s);
                    l = len - j;
                    add32(c, p + 0x4ebu, 1);
                    break;
                }
            }
            if (l > m) { m = l - 1; longest = VA_CMD_LINES + 0x78u * k; }
            if (HCALL(c, VA_PHRASE_NEXT, &phrase, VA_CMD_TABLE, phrase)) return 1;
        }
    }
    hw32(c, p + 0x4e7u, 8u << (hr16(c, VA_SCALE_Y) & 0x1fu));
    hw32(c, p + 0x4e7u, (hr32(c, p + 0x4ebu) + 1u) * hr32(c, p + 0x4e7u));
    uint32_t w = 0;
    if (longest && HCALL(c, VA_WIDTH, &w, hr32(c, p + 0x4dfu), longest)) return 1;
    hw32(c, p + 0x4e3u, w);
    *eax = VA_CMD_LINES;
    return 0;
}
/* 0x4365d0(p) (listing 0x4365d0..0x436609): shown and not hidden: the
 * draw slot +0x18; started: done <- 1; then the step 0x435610. */
static int cmd_frame(hud_ctx *c, uint32_t p) {
    if (hr8(c, p + 0x49eu) && !hr8(c, p + 0x4d2u) && HVCALL(c, p, 0x18u, NULL, p)) return 1;
    if (hr8(c, p + 0x4c9u)) hw8(c, p + 0x4cau, 1);
    return port_frame(c, p);
}
/* 0x43660c(p) (listing 0x43660c..0x4369a1): the CommandPort's show. The
 * globals' end-all byte or [0x4cf99c]+0x1c: EAX 0. [0x4d1f3a] == 1: shown
 * and hidden set, straight to the voice. Else the text slot +8 with the
 * current entry, shown; S's front page (S+0x8c; S+0x88 is read too) into
 * +0x38; the canvas pushed; with x = 10 << [0x4d253a] and h = 8 <<
 * [0x4d253c] the box x0 = gc+0x220 + ([0x4d253e] sar 1) - (width sar 1) -
 * x, x1 = width + x0 + 2x, y1 = y0 + the height +0x4e7; the save-under
 * 0x488012(p+0x4a3, [p+0x4d3]) with the box moved to the canvas origin;
 * the fills {0, word [0x4d3ba2]} and {4, word [0x4d3b8e]} (sign-extended)
 * through GC+0x22c; the lines 0x482634([p+0x4df], pt, line i) for i <
 * count (signed) from pt = (x0 + x, y0 + (h sar 1) + h), h apart; the
 * redraw 0x437164(p, 1); the box saved again into [p+0x4d7]; the driver's
 * [0x4a5824] unless the display is mode 4; S+0x3c <- the front page; the
 * canvas popped; hidden cleared. The voice: the byte [0x4d1f3a] (word
 * [0x4aa728] set) or [0x4d1f3b] (not) non-zero: the template 0x49b7f8 with
 * the low byte of [0x4aa728] + '0' at 2 and the entry's code word + 1 as
 * two decimal digits at 6/7, its path 0x45ef9c(name, 0x49ba68), moved
 * under the CD root 0x45ee60 unless name[2] is '0', spoken by
 * 0x462940(path, 100) at site 0x436991. AL 1. */
static int cmd_show(hud_ctx *c, uint32_t p, uint32_t *eax) {
    *eax = 0;
    if (hr8(c, hr32(c, VA_GLOBALS) + 0x99u) || hr8(c, hr32(c, VA_HOLD) + 0x1cu)) return 0;
    uint32_t top = c->l->stack_hi;
    if (hr8(c, VA_NO_BOX) == 1u) {
        hw8(c, p + 0x49eu, 1);
        hw8(c, p + 0x4d2u, 1);
    } else {
        if (HVCALL(c, p, 8u, NULL, p, hr32(c, p + 0x49au))) return 1;
        hw8(c, p + 0x49eu, 1);
        uint32_t s = hr32(c, VA_SURFACE), front = hr32(c, s + 0x8cu);
        (void)hr32(c, s + 0x88u);
        if (page_select(c, s, 0x38u, front, VA_PAGE_A, VA_DRV_EC, 1)) return 1;
        uint32_t gc = 0, b = p + 0x4a7u;
        if (HCALL0(c, VA_CANVAS_PUSH, &gc)) return 1;
        uint32_t x = 0xau << (hr16(c, VA_SCALE) & 0x1fu), h = 8u << (hr16(c, VA_SCALE_Y) & 0x1fu), w = hr32(c, p + 0x4e3u);
        hw32(c, b, hr32(c, gc + 0x220u) + (uint32_t)((int32_t)hr32(c, VA_HALF_WIDTH) >> 1) - (uint32_t)((int32_t)w >> 1) - x);
        hw32(c, b + 8u, w + hr32(c, b));
        add32(c, b + 8u, 2u * x);
        hw32(c, b + 0xcu, hr32(c, b + 4u) + hr32(c, p + 0x4e7u));
        box_shift(c, b, gc, 1);
        if (HCALL(c, VA_SAVE, NULL, p + 0x4a3u, hr32(c, p + 0x4d3u))) return 1;
        box_shift(c, b, gc, -1);
        uint32_t f = hud_frame_push(c, 0x18u);
        if (!f) return hud_refuse(c, 0x0043660cu, "command show", "the own code's call area is exhausted");
        uint32_t pt = f, fill_b = f + 8u, fill_a = f + 0x10u;
        hw32(c, pt, hr32(c, b) + x);
        hw32(c, pt + 4u, hr32(c, b + 4u) + (uint32_t)((int32_t)h >> 1) + h);
        hw32(c, fill_a, 0);
        hw32(c, fill_b, 4);
        hw32(c, fill_a + 4u, hsx16(hr16(c, VA_COLOUR_BACK)));
        hw32(c, fill_b + 4u, hsx16(hr16(c, VA_COLOUR_EDGE)));
        hw32(c, gc + 0x22cu, fill_a);
        int bad = HCALL(c, VA_FILL, NULL, b);
        if (!bad) { hw32(c, gc + 0x22cu, fill_b); bad = HCALL(c, VA_FILL, NULL, b); }
        for (int32_t i = 0; !bad && i < (int32_t)hr32(c, p + 0x4ebu); ++i) {
            bad = HCALL(c, VA_STRING, NULL, hr32(c, p + 0x4dfu), pt, VA_CMD_LINES + 0x78u * (uint32_t)i);
            add32(c, pt + 4u, h);
        }
        hud_frame_pop(c, top);
        if (bad) return 1;
        if (HCALL(c, VA_REDRAW, NULL, p, 1)) return 1;
        box_shift(c, b, gc, 1);
        if (HCALL(c, VA_SAVE, NULL, p + 0x4a3u, hr32(c, p + 0x4d7u))) return 1;
        box_shift(c, b, gc, -1);
        uint32_t d = 0;
        if (HCALL0(c, VA_DISPLAY_CURRENT, &d)) return 1;
        if (hr32(c, d + 0x14u) != 4u && HCALL0(c, hr32(c, VA_DRV_24), NULL)) return 1;
        if (page_select(c, s, 0x3cu, front, VA_PAGE_B, VA_DRV_F0, 1)) return 1;
        if (HCALL0(c, VA_CANVAS_POP, NULL)) return 1;
        hw8(c, p + 0x4d2u, 0);
    }
    uint16_t set = hr16(c, VA_CMD_VOICE);
    if (hr8(c, 0x004d1f38u + (set ? 2u : 3u))) {
        uint32_t name = name_block(c, 0x0049b7f8u), path = 0;
        if (!name) return hud_refuse(c, 0x0043660cu, "command show", "the own code's call area is exhausted");
        hw8(c, name + 2u, (uint8_t)(set + 0x30u));
        uint32_t n = (uint32_t)hr16(c, hr32(c, p + 0x49au)) + 1u;
        hw8(c, name + 6u, (uint8_t)(n / 10u + 0x30u));
        n = (uint32_t)hr16(c, hr32(c, p + 0x49au)) + 1u;
        hw8(c, name + 7u, (uint8_t)(n % 10u + 0x30u));
        int bad = HCALL(c, VA_VOICE_PATH, &path, name, 0x0049ba68u);
        if (!bad && (int8_t)hr8(c, name + 2u) != 0x30) bad = HCALL(c, VA_CD_PATH_FN, &path, path);
        if (!bad) bad = HSOUND(c, 0x00436991u, VA_VOICE, NULL, path, 0x64u);
        hud_frame_pop(c, top);
        if (bad) return 1;
    }
    *eax = 1;
    return 0;
}
ROW(hud_phrase_next) { ROW_BEGIN; eax = phrase_next(&c, sc->slots[0], sc->slots[1]); ROW_END(0); }
ROW(hud_phrase_row) { ROW_BEGIN; eax = phrase_row(&c, sc->slots[0], sc->slots[1]); ROW_END(0); }
ROW(hud_voice_select) { ROW_BEGIN; eax = voice_select(&c, sc->slots[0], sc->slots[1]); ROW_END(0); }
ROW(hud_cd_path) { ROW_BEGIN; ROW_END(cd_path(&c, sc->slots[0], &eax)); }
ROW(hud_command_receive) { ROW_BEGIN; ROW_END(cmd_receive(&c, sc->slots[0], sc->slots[1])); }
ROW(hud_command_text) { ROW_BEGIN; ROW_END(cmd_text(&c, sc->slots[0], sc->slots[1], &eax)); }
ROW(hud_command_frame) { ROW_BEGIN; ROW_END(cmd_frame(&c, sc->slots[0])); }
ROW(hud_command_show) { ROW_BEGIN; ROW_END(cmd_show(&c, sc->slots[0], &eax)); }
