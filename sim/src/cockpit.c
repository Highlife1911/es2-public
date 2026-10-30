/* The cockpit jolt and bump as own code: the hit entries 0x434010 (jolt) and
 * 0x434144 (bump), the jolt start 0x42d2f8, the jolt step 0x42d4a8 and the bump
 * reset 0x42d820. The per-frame halves (0x43408c, 0x434194), the jolt end
 * 0x42d624 and the GC bias 0x42d82c/0x42d854 are HUD rows (src/hud_frame.c,
 * src/hud_display.c); the palette flashes 0x42ea44/0x42ea94 are src/scene.c's,
 * the strip save 0x488012 and the fill 0x486704 src/gfx.c's, the panel move
 * 0x43271c a HUD row: all of them are called through the dispatch.
 *
 * Contract: display_modes.md, "the jolt and the bump as own code"; every
 * function names its listing range of the reference DBSIM.EXE (llvm-objdump -d
 * -M intel, SHA-256 8561aa47...c4ad). The coupling calls keep their coupling-table sites
 * and order: the clock at 0x434036 and 0x434161, stream B at 0x434053. Checked
 * in vitro against the original's machine code (differential_cockpit_test). */
#include "hud_internal.h"

#define VA_DISPLAY_CURRENT 0x00429820u
#define VA_GC 0x006c5f70u
#define VA_VIEW 0x006c5ed0u
#define VA_PAGED 0x004d2566u        /* word: the page-flipped surface */
#define VA_PAGE_X 0x004d3018u       /* the page origin (0x4649d8) */
#define VA_PAGE_Y 0x004d301cu
#define VA_MODE_X 0x004cf9a0u       /* the mode's origin */
#define VA_MODE_Y 0x004cf9a4u
#define VA_SAVED_X 0x004cfa58u      /* the origin a jolt returns to */
#define VA_SAVED_Y 0x004cfa5cu
#define VA_JOLT_X 0x004cfa60u       /* the shaken origin */
#define VA_JOLT_Y 0x004cfa64u
#define VA_JOLT_LOW 0x004cfa68u     /* the shake bounds */
#define VA_JOLT_HIGH 0x004cfa6cu
#define VA_JOLT_SHIFT 0x004cfa70u   /* word: the accumulated shift */
#define VA_STRIP_RECT 0x004cfa7eu   /* the strip a jolt uncovers: the save rectangle */
#define VA_STRIP_FILL 0x004cfa82u   /* the fill rectangle */
#define VA_STRIP_BUFFER 0x004cfa94u /* the save buffer */
#define VA_JOLT_ON 0x0049ac44u      /* byte: a jolt offset is applied */
#define VA_JOLT_END_TIME 0x0049b0fcu
#define VA_JOLT_FLASH 0x0049b100u
#define VA_BUMP_START 0x0049b634u
#define VA_BUMP_END 0x0049b638u
#define VA_BUMP_VALUE 0x004cfcb8u
#define VA_SCALE 0x004d253cu        /* word: the resolution shift */
#define VA_COLOUR 0x004d3ba2u       /* word: the strip's fill colour */
#define VA_ROWS 0x004a5af8u         /* the row record (+0 base, +4 count) */
#define VA_CUR_PAGE 0x004a368au
#define VA_DRV_ORIGIN 0x004a57e8u   /* driver slot: the page origin */
#define VA_DRV_PAGE 0x004a57f0u     /* driver slot: the page select */
#define VA_JOLT_START 0x0042d2f8u
#define VA_JOLT_END 0x0042d624u
#define VA_BUMP_RESET 0x0042d820u
#define VA_BIAS_CLEAR 0x0042d854u
#define VA_FLASH_ON 0x0042ea44u
#define VA_FLASH_OFF 0x0042ea94u
#define VA_STRIP_SAVE 0x00488012u
#define VA_RECT_FILL 0x00486704u
#define VA_PANEL_MOVE 0x0043271cu

static uint32_t sxw(const hud_ctx *c, uint32_t a) { return hsx16(hr16(c, a)); }

/* 0x434010() (listing 0x434010..0x434088), a hit's jolt: the display in
 * mode 4: EAX 4. A jolt running (end time [0x49b0fc] set): the flash off
 * (0x42ea94) and the jolt end (0x42d624). now = clock (0x434036); the end
 * time <- now + 60. No flash time [0x49b100]: v = stream B (0x434053), the
 * flash time <- now + (v & 0xffff) % 10, the flash on (0x42ea44), the jolt
 * start (0x42d2f8) with 5 << (word [0x4d253c] & 31); EAX its result.
 * Else EAX now + 60. */
static int jolt_hit(hud_ctx *c, uint32_t *eax) {
    uint32_t d = 0, now = 0, v = 0;
    if (HCALL0(c, VA_DISPLAY_CURRENT, &d)) return 1;
    if (hr32(c, d + 0x14u) == 4u) { *eax = 4u; return 0; }
    if (hr32(c, VA_JOLT_END_TIME) != 0) {
        if (HCALL0(c, VA_FLASH_OFF, NULL) || HCALL0(c, VA_JOLT_END, NULL)) return 1;
    }
    if (hud_clock(c, 0x00434036u, &now)) return 1;
    hw32(c, VA_JOLT_END_TIME, now + 0x3cu);
    if (hr32(c, VA_JOLT_FLASH) != 0) { *eax = now + 0x3cu; return 0; }
    if (hud_rng_next(c, 0x00434053u, &v)) return 1;
    hw32(c, VA_JOLT_FLASH, now + (v & 0xffffu) % 10u);
    if (HCALL0(c, VA_FLASH_ON, NULL)) return 1;
    return HCALL(c, VA_JOLT_START, eax, 5u << (hr16(c, VA_SCALE) & 31u));
}

/* 0x434144() (listing 0x434144..0x434191), a hit's bump: the display in
 * mode 4: EAX 4. The GC bias cleared (0x42d854), the end time [0x49b638]
 * <- 0, now = clock (0x434161); now below the end time (never: it was
 * just cleared) returns with EAX now; else start [0x49b634] <- now, end
 * <- now + 60, the bump reset (0x42d820) with 10 << (word [0x4d253c] &
 * 31); EAX its result. */
static int bump_hit(hud_ctx *c, uint32_t *eax) {
    uint32_t d = 0, now = 0;
    if (HCALL0(c, VA_DISPLAY_CURRENT, &d)) return 1;
    if (hr32(c, d + 0x14u) == 4u) { *eax = 4u; return 0; }
    if (HCALL0(c, VA_BIAS_CLEAR, NULL)) return 1;
    hw32(c, VA_BUMP_END, 0);
    if (hud_clock(c, 0x00434161u, &now)) return 1;
    if (now < hr32(c, VA_BUMP_END)) { *eax = now; return 0; }
    hw32(c, VA_BUMP_START, now);
    hw32(c, VA_BUMP_END, now + 0x3cu);
    return HCALL(c, VA_BUMP_RESET, eax, 10u << (hr16(c, VA_SCALE) & 31u));
}

/* The page select of 0x42d3fe..0x42d41e: V+0x3c <- p = V+0x88; V still
 * current: [0x4a368a] <- V+0x3c and the driver's page slot with p. */
static int page_front(hud_ctx *c, uint32_t v) {
    uint32_t p = hr32(c, v + 0x88u);
    hw32(c, v + 0x3cu, p);
    if (v != hr32(c, VA_VIEW)) return 0;
    hw32(c, VA_CUR_PAGE, hr32(c, v + 0x3cu));
    return hud_call(c, hr32(c, VA_DRV_PAGE), (const uint32_t[]){p}, 1u, NULL);
}

/* 0x42d2f8(amp) (listing 0x42d2f8..0x42d4a4), the jolt start. D = 0x429820;
 * mode 4 or the transition byte D+0x1c: nothing (EAX D). The origin
 * (0x4d3018, 0x4d301c) on a paged surface, else (0x4cf9a0, 0x4cf9a4), to
 * the shaken origin 0x4cfa60/64 and the saved origin 0x4cfa58/5c. By the
 * mode (D re-read):
 * - 2, 3: unpaged, [0x4cfa5c] and [0x4cfa60] <- 0; GC+0x208/+0x20c saved
 *   and cleared, the strip saved (0x488012(0x4cfa7e, [0x4cfa94])), GC+0x22c
 *   <- a local colour record (0, word [0x4d3ba2]) (left dangling, as in
 *   the original), the strip filled (0x486704(0x4cfa82)), the front page
 *   selected (V+0x88), GC+0x208/+0x20c restored; then as mode 0;
 * - 0: low [0x4cfa68] <- [0x4cf9a4] (+ [0x4d301c] paged), high
 *   [0x4cfa6c] <- low + (amp & 0xffff); EAX [0x4cf9a4];
 * - 1: high <- [0x4cf9a4]; paged, low += [0x4d301c] (overwritten next);
 *   low <- [0x4cf9a4] - (amp & 0xffff); EAX that;
 * - other: EAX D.
 * Then word [0x4cfa70] <- 0 and byte 0x49ac44 <- 1. */
static int jolt_start(hud_ctx *c, uint32_t amp, uint32_t *eax) {
    uint32_t d = 0;
    if (HCALL0(c, VA_DISPLAY_CURRENT, &d)) return 1;
    *eax = d;
    if (hr32(c, d + 0x14u) == 4u || hr8(c, d + 0x1cu) != 0) return 0;
    int paged = hr16(c, VA_PAGED) != 0;
    hw32(c, VA_JOLT_X, hr32(c, paged ? VA_PAGE_X : VA_MODE_X));
    hw32(c, VA_JOLT_Y, hr32(c, paged ? VA_PAGE_Y : VA_MODE_Y));
    hw32(c, VA_SAVED_X, hr32(c, VA_JOLT_X));
    hw32(c, VA_SAVED_Y, hr32(c, VA_JOLT_Y));
    if (HCALL0(c, VA_DISPLAY_CURRENT, &d)) return 1;
    *eax = d;
    uint32_t mode = hr32(c, d + 0x14u), w = amp & 0xffffu;
    if (mode == 2u || mode == 3u) {
        if (!paged) {
            hw32(c, VA_SAVED_Y, 0);
            hw32(c, VA_JOLT_X, 0);
        }
        uint32_t gc = hr32(c, VA_GC), v = hr32(c, VA_VIEW);
        uint32_t a = hr32(c, gc + 0x208u);
        hw32(c, gc + 0x208u, 0);
        uint32_t b = hr32(c, gc + 0x20cu);
        hw32(c, gc + 0x20cu, 0);
        if (HCALL(c, VA_STRIP_SAVE, NULL, VA_STRIP_RECT, hr32(c, VA_STRIP_BUFFER))) return 1;
        uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 16u);
        if (!f) return hud_refuse(c, VA_JOLT_START, "jolt start", "the own code's call area is exhausted");
        hw32(c, f, 0);
        hw32(c, gc + 0x22cu, f);
        hw32(c, f + 4u, sxw(c, VA_COLOUR));
        if (HCALL(c, VA_RECT_FILL, NULL, VA_STRIP_FILL) || page_front(c, v)) { hud_frame_pop(c, top); return 1; }
        hud_frame_pop(c, top);
        hw32(c, gc + 0x208u, a);
        hw32(c, gc + 0x20cu, b);
        mode = 0;
    }
    if (mode == 0u) {
        hw32(c, VA_JOLT_LOW, hr32(c, VA_MODE_Y));
        if (paged) hw32(c, VA_JOLT_LOW, hr32(c, VA_JOLT_LOW) + hr32(c, VA_PAGE_Y));
        hw32(c, VA_JOLT_HIGH, w + hr32(c, VA_JOLT_LOW));
        *eax = hr32(c, VA_MODE_Y);
    } else if (mode == 1u) {
        uint32_t y = hr32(c, VA_MODE_Y);
        hw32(c, VA_JOLT_HIGH, y);
        if (paged) hw32(c, VA_JOLT_LOW, hr32(c, VA_JOLT_LOW) + hr32(c, VA_PAGE_Y));
        *eax = y - w;
        hw32(c, VA_JOLT_LOW, *eax);
    }
    hw16(c, VA_JOLT_SHIFT, 0);
    hw8(c, VA_JOLT_ON, 1);
    return 0;
}

/* 0x42d4a8(step) (listing 0x42d4a8..0x42d623), one shake step. Byte
 * 0x49ac44 clear: nothing (EAX as entered); mode 4: EAX 4. With the
 * words y = [0x4cfa64], down = y - [0x4cfa68], up = [0x4cfa6c] - y and
 * the step's low word s: down < up (signed words): d = up > s (up signed,
 * s unsigned) ? s : up; else d = -(down > s ? s : down). Then, with d a
 * signed word:
 * - [0x4cfa64] += d; the page origin of the view's page [V+0x8c]:
 *   driver slot [0x4a57e8](page, &(page point + (0x4cfa60, 0x4cfa64)));
 * - each entry e of the mode's list [D+0xc] + 0x204 mode (count, then the
 *   entries): kind [e] 0: dword e - 4 + 0x14 -= d; kind 2: the word at
 *   [e - 4 + 8], when set, -= d (e 0: the bases at 0, as the original);
 * - word [0x4cfa70] += d; the row record [0x4a5af8], when set: +0 += 8d,
 *   +4 -= d; GC+0x218 and +0x21c -= d;
 * - the panel [D+0x1f] moved by (0, -d) (0x43271c); EAX its result. */
static int jolt_step(hud_ctx *c, uint32_t step, uint32_t *eax) {
    if (hr8(c, VA_JOLT_ON) == 0) return 0;
    uint32_t d = 0;
    if (HCALL0(c, VA_DISPLAY_CURRENT, &d)) return 1;
    uint32_t mode = hr32(c, d + 0x14u);
    if (mode == 4u) { *eax = 4u; return 0; }
    uint16_t y = hr16(c, VA_JOLT_Y);
    int16_t down = (int16_t)(uint16_t)(y - hr16(c, VA_JOLT_LOW));
    int16_t up = (int16_t)(uint16_t)(hr16(c, VA_JOLT_HIGH) - y);
    int32_t s = (int32_t)(step & 0xffffu);
    uint16_t si;
    if (down < up) si = (int32_t)up > s ? (uint16_t)step : (uint16_t)up;
    else si = (uint16_t)(0u - ((int32_t)down > s ? step : (uint32_t)(uint16_t)down));
    uint32_t dd = hsx16(si);
    hw32(c, VA_JOLT_Y, hr32(c, VA_JOLT_Y) + dd);
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x18u);
    if (!f) return hud_refuse(c, 0x0042d4a8u, "jolt step", "the own code's call area is exhausted");
    /* f + 0xc: the page point (ebp - 0xc), f: the panel move (ebp - 0x18) */
    uint32_t v = hr32(c, VA_VIEW), page = hr32(c, v + 0x8cu), q = v + 8u * page + 0x48u;
    hw32(c, f + 0xcu, hr32(c, q));
    hw32(c, f + 0x10u, hr32(c, q + 4u));
    hw32(c, f + 0xcu, hr32(c, f + 0xcu) + hr32(c, VA_JOLT_X));
    hw32(c, f + 0x10u, hr32(c, f + 0x10u) + hr32(c, VA_JOLT_Y));
    if (hud_call(c, hr32(c, VA_DRV_ORIGIN), (const uint32_t[]){page, f + 0xcu}, 2u, NULL)) { hud_frame_pop(c, top); return 1; }
    if (HCALL0(c, VA_DISPLAY_CURRENT, &d)) { hud_frame_pop(c, top); return 1; }
    uint32_t list = hr32(c, d + 0xcu) + mode * 0x204u;
    for (uint32_t i = 0; (int32_t)i < (int32_t)hr32(c, list); ++i) {
        uint32_t e = hr32(c, list + 4u + 4u * i), kind = hr32(c, e), base = e ? e - 4u : e;
        if (kind == 0u) hw32(c, base + 0x14u, hr32(c, base + 0x14u) - dd);
        else if (kind == 2u) {
            uint32_t p = hr32(c, base + 8u);
            if (p) hw16(c, p, (uint16_t)(hr16(c, p) - si));
        }
    }
    hw16(c, VA_JOLT_SHIFT, (uint16_t)(hr16(c, VA_JOLT_SHIFT) + si));
    uint32_t rows = hr32(c, VA_ROWS);
    if (rows) {
        hw32(c, rows, hr32(c, rows) + (dd << 3));
        rows = hr32(c, VA_ROWS);
        hw32(c, rows + 4u, hr32(c, rows + 4u) - dd);
    }
    uint32_t gc = hr32(c, VA_GC);
    hw32(c, gc + 0x218u, hr32(c, gc + 0x218u) - dd);
    hw32(c, gc + 0x21cu, hr32(c, gc + 0x21cu) - dd);
    hw32(c, f, 0);
    hw32(c, f + 4u, 0u - dd);
    if (HCALL0(c, VA_DISPLAY_CURRENT, &d)) { hud_frame_pop(c, top); return 1; }
    int bad = HCALL(c, VA_PANEL_MOVE, eax, hr32(c, d + 0x1fu), f);
    hud_frame_pop(c, top);
    return bad;
}

/* ---- the service rows */
void es2_sim_service_cockpit_jolt_hit(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (jolt_hit(&c, &sc->eax)) hud_failed(sc);
}
void es2_sim_service_cockpit_bump_hit(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (bump_hit(&c, &sc->eax)) hud_failed(sc);
}
void es2_sim_service_cockpit_jolt_start(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (jolt_start(&c, sc->slots[0], &sc->eax)) hud_failed(sc);
}
void es2_sim_service_cockpit_jolt_step(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (jolt_step(&c, sc->slots[0], &sc->eax)) hud_failed(sc);
}
/* 0x42d820(v) (listing 0x42d820..0x42d82b): [0x4cfcb8] <- 0; EAX 0. The
 * argument is not read. */
void es2_sim_service_cockpit_bump_reset(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    hw32(&c, VA_BUMP_VALUE, 0);
    sc->eax = 0;
}

/* ---- the intro set-up (display_modes.md, "the capture switch, the done step,
 * the surface frame and the intro set-up"): the intro set-up 0x45d548() (listing
 * 0x45d548..0x45d859) that the mission load 0x461608 runs before the drop
 * sequence 0x45d85c (word 0x4d2510 set). The frame record 0x49630c is not
 * linked (own code; FS:0 is the same at the return, as in the original). Two
 * palette objects of 0x14 bytes (operator new 0x47749c; the inlined
 * constructors leave the vtable 0x4a30f8 after 0x4a69f0 and 0x4a3368, the words
 * +4 and +0xc zero and the construction counter +1 +1 -2) into [0x4d2744] and
 * [0x4d2748]; two arrays of 0x400 bytes (0x4774f0) into [0x4d274c] and
 * [0x4d2750]; the fields +4, +6, +8, +0xc, +0xe and +0x10 of the palette P =
 * [0x49b020] (read once, before the call below) copied into both objects, then
 * +8 of each pointed at its array; word [0x49aef6] <- 8; 0x42eb40([0x4d24ea])
 * (an empty body, gadgets.c's bare row); the 256 entries of [P+8] copied into both
 * arrays; the 16 entries at +0x80 and the 16 at +0x100 of the first array
 * rebuilt from the second: bytes b0, b1, b2 halved into 0x01000000 | b2/2 << 16
 * | b0/2 << 8 | b1/2 (the original's order); 0x4303c4(0x20, 0x10, array + 0x80)
 * and 0x4303c4(0x40, 0x10, array + 0x100) with the array read again through
 * [[0x4d2744]+8]; 0x4308fc([0x4d2744], [0x4d2748], 0x78); byte [0x49aef4] <- 0.
 * EAX is 0x4308fc's (no caller reads it); EDX in the original is the saved FS:0
 * head. Refused: operator new returning NULL (the original then writes through
 * NULL). */
#define VA_INTRO_PAL_A 0x004d2744u
#define VA_INTRO_PAL_B 0x004d2748u
#define VA_INTRO_ARRAY_A 0x004d274cu
#define VA_INTRO_ARRAY_B 0x004d2750u
#define VA_PALETTE 0x0049b020u
static int intro_palette(hud_ctx *c, uint32_t *o) {
    if (HCALL(c, HUD_VA_NEW, o, 0x14u)) return 1;
    if (!*o) return hud_null_refuse(c, 0x0045d56du, "intro palette", "operator new returned NULL (the original writes through it)");
    hw32(c, *o, 0x004a69f0u);
    hw32(c, *o, 0x004a3368u);
    hud_counter(c, 1);
    hw32(c, *o, 0x004a30f8u);
    hw16(c, *o + 4u, 0);
    hw16(c, *o + 0xcu, 0);
    hud_counter(c, 1);
    hud_counter(c, -2);
    return 0;
}
static int intro_setup(hud_ctx *c, uint32_t *eax) {
    uint32_t a = 0, b = 0, x = 0, y = 0;
    if (intro_palette(c, &a)) return 1;
    hw32(c, VA_INTRO_PAL_A, a);
    if (intro_palette(c, &b)) return 1;
    hw32(c, VA_INTRO_PAL_B, b);
    if (HCALL(c, HUD_VA_NEW_ARRAY, &x, 0x400u)) return 1;
    hw32(c, VA_INTRO_ARRAY_A, x);
    if (HCALL(c, HUD_VA_NEW_ARRAY, &y, 0x400u)) return 1;
    hw32(c, VA_INTRO_ARRAY_B, y);
    uint32_t p = hr32(c, VA_PALETTE);
    for (unsigned k = 0; k < 2u; ++k) {
        uint32_t o = hr32(c, k ? VA_INTRO_PAL_B : VA_INTRO_PAL_A);
        hw16(c, o + 4u, hr16(c, p + 4u));
        hw16(c, o + 6u, hr16(c, p + 6u));
        hw32(c, o + 8u, hr32(c, p + 8u));
        hw16(c, o + 0xcu, hr16(c, p + 0xcu));
        hw16(c, o + 0xeu, hr16(c, p + 0xeu));
        hw32(c, o + 0x10u, hr32(c, p + 0x10u));
    }
    hw32(c, hr32(c, VA_INTRO_PAL_A) + 8u, hr32(c, VA_INTRO_ARRAY_A));
    hw32(c, hr32(c, VA_INTRO_PAL_B) + 8u, hr32(c, VA_INTRO_ARRAY_B));
    hw16(c, 0x0049aef6u, 8);
    if (HCALL(c, 0x0042eb40u, NULL, hr32(c, 0x004d24eau))) return 1;
    /* x and y: the arrays as the original holds them in ESI and EBX */
    for (uint32_t i = 0, src = hr32(c, p + 8u); i < 0x100u; ++i) {
        uint32_t v = hr32(c, src + 4u * i);
        hw32(c, y + 4u * i, v);
        hw32(c, x + 4u * i, v);
    }
    for (uint32_t base = 0x80u; base <= 0x100u; base += 0x80u) {
        uint32_t s = hr32(c, VA_INTRO_ARRAY_B) + base, d = hr32(c, VA_INTRO_ARRAY_A) + base;
        for (uint32_t i = 0; i < 0x10u; ++i, s += 4u, d += 4u) {
            uint32_t b0 = hr8(c, s) >> 1, b1 = hr8(c, s + 1u) >> 1, b2 = hr8(c, s + 2u) >> 1;
            hw32(c, d, 0x01000000u | b2 << 16 | b0 << 8 | b1);
        }
    }
    if (HCALL(c, 0x004303c4u, NULL, 0x20u, 0x10u, hr32(c, hr32(c, VA_INTRO_PAL_A) + 8u) + 0x80u) ||
        HCALL(c, 0x004303c4u, NULL, 0x40u, 0x10u, hr32(c, hr32(c, VA_INTRO_PAL_A) + 8u) + 0x100u) ||
        HCALL(c, 0x004308fcu, eax, hr32(c, VA_INTRO_PAL_A), hr32(c, VA_INTRO_PAL_B), 0x78u))
        return 1;
    hw8(c, 0x0049aef4u, 0);
    return 0;
}
void es2_sim_service_intro_setup(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (intro_setup(&c, &sc->eax)) hud_failed(sc);
}

/* ---- the drop sequence (cockpit.md, "drop sequence 45d85c and panel
 * constructor 431bf8"): the mission-start drop sequence 0x45d85c() (listing
 * 0x45d85c..0x45dc4e) that the mission load 0x461608 runs after the intro
 * set-up when word 0x4d2510 is set. As in 0x45d548 the frame record 0x49630c is
 * not linked (it writes only the caller's frame and FS:0, which is the same at
 * the return). The original's locals ebp-0x88..ebp-0x2c are a block of the call
 * area (DROP_L): the colour record (ebp-0x30, ebp-0x2c) that GC+0x22c is left
 * pointing at, the 2D point (ebp-0x60, ebp-0x5c) handed to the blit, the 3D
 * point (ebp-0x78..ebp-0x70), the projection (ebp-0x58, ebp-0x54) and the fill
 * rectangle (ebp-0x88..ebp-0x7c). Four pieces: drop_begin, drop_descent (one
 * pass of the loop), drop_land and drop_release. */
#define VA_PLAYER 0x004d24e6u        /* P: the player object */
#define VA_CAMERA 0x004d24eau        /* C: the camera object */
#define VA_SCREEN 0x004d24bcu        /* the screen record the blit draws on */
#define VA_HUD_PANEL 0x0049b088u
#define VA_FADE_ON 0x0049f138u
#define VA_PATH_DIR 0x00492becu
#define VA_LOAD_BY_NAME 0x0045cdf4u
#define VA_SFX_PLAY 0x00462830u      /* hooks of the sfx group */
#define VA_SFX_STOP 0x00462ac4u
#define VA_CAMERA_UPDATE 0x004011a0u /* hook camera_update */
#define VA_RENDER_ROOT 0x0045fca8u   /* hook render_root */
#define VA_VIEW_SETUP 0x0048c2e4u
#define VA_OBJECT_SELECT 0x0048c444u
#define VA_PROJECT 0x0048c5fcu
#define VA_TO_SCREEN 0x0048c6d0u
#define VA_PUMP 0x00464c78u
#define VA_BLIT_AT 0x0045c8bcu
#define VA_FADE_STEP 0x00430b34u
#define VA_PRESENT 0x0045fba4u
#define VA_JOLT_STEP 0x0042d4a8u
#define DROP_FRAME 0x88u
#define DROP_L(d, x) ((d)->ebp - (x))
typedef struct drop_state {
    uint32_t ebp;            /* the top of the locals block (the original's EBP) */
    uint32_t gc, bitmap;     /* EBX (GC [0x6c5f70], read once) and ESI */
    uint32_t saved220, saved224, saved21c;
    uint32_t x, y, z;        /* P's saved position */
} drop_state;

/* 0x45d85c..0x45d91b: the intro bitmap B = [[0x45cdf4(0x492bec(0x49f225,
 * [0x4a0a10]))+8]] (a NULL record or list is refused: the original reads
 * through it), GC read after the load; the saves of GC+0x220/+0x224/+0x21c
 * and of P's position +0x26/+0x2a/+0x2e (the angle words +0xc..+0x10 go
 * into locals that nothing reads); the 3D point (0, 1000, 0);
 * sfx_play(0x21). */
static int drop_begin(hud_ctx *c, drop_state *d) {
    uint32_t path = 0, r = 0;
    if (HCALL(c, VA_PATH_DIR, &path, 0x0049f225u, hr32(c, 0x004a0a10u)) || HCALL(c, VA_LOAD_BY_NAME, &r, path)) return 1;
    d->gc = hr32(c, VA_GC);
    if (!r) return hud_null_refuse(c, 0x0045d893u, "drop sequence", "the intro record is NULL (the original reads through it)");
    uint32_t list = hr32(c, r + 8u);
    if (!list) return hud_null_refuse(c, 0x0045d898u, "drop sequence", "the intro bitmap list is NULL (the original reads through it)");
    d->bitmap = hr32(c, list);
    hw32(c, DROP_L(d, 0x30u), 0);
    d->saved220 = hr32(c, d->gc + 0x220u);
    d->saved224 = hr32(c, d->gc + 0x224u);
    d->saved21c = hr32(c, d->gc + 0x21cu);
    uint32_t p = hr32(c, VA_PLAYER);
    d->x = hr32(c, p + 0x26u);
    d->y = hr32(c, p + 0x2au);
    d->z = hr32(c, p + 0x2eu);
    hw32(c, DROP_L(d, 0x78u), 0);
    hw32(c, DROP_L(d, 0x74u), 0x3e8u);
    hw32(c, DROP_L(d, 0x70u), 0);
    return HSOUND(c, 0x0045d91bu, VA_SFX_PLAY, NULL, 0x21u);
}

/* (word [B+4] << word [0x4d253c]) as a 16-bit shift (count & 31), sign-extended. */
static uint32_t drop_height(hud_ctx *c, uint32_t b) {
    unsigned s = hr16(c, VA_SCALE) & 31u;
    return hsx16((uint16_t)((uint32_t)hr16(c, b + 4u) << s));
}
/* The colour record (ebp-0x30 = 0 since drop_begin, ebp-0x2c the
 * sign-extended word [0x4d3ba2]) into GC+0x22c, then 0x486704(rect). */
static int drop_fill(hud_ctx *c, drop_state *d, uint32_t rect) {
    hw32(c, d->gc + 0x22cu, DROP_L(d, 0x30u));
    hw32(c, DROP_L(d, 0x2cu), sxw(c, VA_COLOUR));
    return HCALL(c, VA_RECT_FILL, NULL, rect);
}

/* One pass of 0x45d92a..0x45db07 with the height z: P's position <- (x, y,
 * z) (+0x2e first); camera_update(C), 0x48c2e4(C), 0x48c444(0) (C re-read
 * for each); the 3D point's third dword <- -[C+0xc]; 0x48c5fc(point, s),
 * 0x48c6d0(s, s); the 2D point <- (0, v = s.y); GC+0x21c <- v when 0 < v <
 * the saved +0x21c (signed), the saved value when v >= it, [GC+0x214] when
 * v <= 0; render_root, the pump; GC+0x220/+0x224 <- 0, GC+0x21c <- the
 * saved value; with h = drop_height (re-read at each use) and v re-read
 * from the 2D point:
 * - h + v < [GC+0x214] (signed): the fill of GC+0x210;
 * - else h + v < [GC+0x21c]: 0x45c8bc(0x4d24bc, B, &point) and the fill of
 *   (point.x, h + v, [GC+0x218], [GC+0x21c]);
 * - else 0x45c8bc(0x4d24bc, B, &point) alone;
 * the HUD draw: the slot +8 of [[0x49b088]+0x1f5]; GC+0x220/+0x224 <- the
 * saved values; [0x49f138] set and v > -200 (signed): [0x49f138] <-
 * 0x430b34(); 0x45fba4(). */
static int drop_descent(hud_ctx *c, drop_state *d, uint32_t z) {
    uint32_t p = hr32(c, VA_PLAYER) + 0x26u;
    hw32(c, p + 8u, z);
    hw32(c, p, d->x);
    hw32(c, p + 4u, d->y);
    if (HCALL(c, VA_CAMERA_UPDATE, NULL, hr32(c, VA_CAMERA)) || HCALL(c, VA_VIEW_SETUP, NULL, hr32(c, VA_CAMERA)) ||
        HCALL(c, VA_OBJECT_SELECT, NULL, 0u))
        return 1;
    hw32(c, DROP_L(d, 0x70u), 0u - hr32(c, hr32(c, VA_CAMERA) + 0xcu));
    if (HCALL(c, VA_PROJECT, NULL, DROP_L(d, 0x78u), DROP_L(d, 0x58u)) ||
        HCALL(c, VA_TO_SCREEN, NULL, DROP_L(d, 0x58u), DROP_L(d, 0x58u)))
        return 1;
    uint32_t pt = DROP_L(d, 0x60u), v = hr32(c, DROP_L(d, 0x54u));
    hw32(c, pt, 0);
    hw32(c, pt + 4u, v);
    uint32_t clip;
    if ((int32_t)v > 0) clip = (int32_t)v < (int32_t)d->saved21c ? v : d->saved21c;
    else clip = hr32(c, d->gc + 0x214u);
    hw32(c, d->gc + 0x21cu, clip);
    if (HCALL0(c, VA_RENDER_ROOT, NULL) || HCALL0(c, VA_PUMP, NULL)) return 1;
    hw32(c, d->gc + 0x220u, 0);
    hw32(c, d->gc + 0x224u, 0);
    hw32(c, d->gc + 0x21cu, d->saved21c);
    if ((int32_t)(drop_height(c, d->bitmap) + hr32(c, pt + 4u)) < (int32_t)hr32(c, d->gc + 0x214u)) {
        if (drop_fill(c, d, d->gc + 0x210u)) return 1;
    } else if ((int32_t)(drop_height(c, d->bitmap) + hr32(c, pt + 4u)) < (int32_t)hr32(c, d->gc + 0x21cu)) {
        if (HCALL(c, VA_BLIT_AT, NULL, VA_SCREEN, d->bitmap, pt)) return 1;
        uint32_t rect = DROP_L(d, 0x88u);
        hw32(c, rect, hr32(c, pt));
        hw32(c, rect + 4u, drop_height(c, d->bitmap) + hr32(c, pt + 4u));
        hw32(c, rect + 8u, hr32(c, d->gc + 0x218u));
        hw32(c, rect + 12u, hr32(c, d->gc + 0x21cu));
        if (drop_fill(c, d, rect)) return 1;
    } else if (HCALL(c, VA_BLIT_AT, NULL, VA_SCREEN, d->bitmap, pt)) return 1;
    uint32_t hud = hr32(c, hr32(c, VA_HUD_PANEL) + 0x1f5u);
    if (HVCALL(c, hud, 8u, NULL, hud)) return 1;
    hw32(c, d->gc + 0x220u, d->saved220);
    hw32(c, d->gc + 0x224u, d->saved224);
    if (hr32(c, VA_FADE_ON) != 0 && (int32_t)hr32(c, pt + 4u) > -200) {
        uint32_t f = 0;
        if (HCALL0(c, VA_FADE_STEP, &f)) return 1;
        hw32(c, VA_FADE_ON, f);
    }
    return HCALL0(c, VA_PRESENT, NULL);
}

/* 0x45db0d..0x45db7b: P's position <- the saved one (+0x2e first);
 * sfx_stop(0x21); end = clock + 30; the jolt start 0x42d2f8(5);
 * sfx_play(0x29); then, at least once and while end > clock (unsigned):
 * 0x42d4a8((stream B & 0xffff) mod 5), render_root; the jolt end 0x42d624. */
static int drop_land(hud_ctx *c, drop_state *d) {
    uint32_t p = hr32(c, VA_PLAYER) + 0x26u, end = 0, now = 0, v = 0;
    hw32(c, p + 8u, d->z);
    hw32(c, p, d->x);
    hw32(c, p + 4u, d->y);
    if (HSOUND(c, 0x0045db2bu, VA_SFX_STOP, NULL, 0x21u) || hud_clock(c, 0x0045db31u, &end)) return 1;
    end += 0x1eu;
    if (HCALL(c, VA_JOLT_START, NULL, 5u) || HSOUND(c, 0x0045db45u, VA_SFX_PLAY, NULL, 0x29u)) return 1;
    do {
        if (hud_rng_next(c, 0x0045db50u, &v) || HCALL(c, VA_JOLT_STEP, NULL, (v & 0xffffu) % 5u) ||
            HCALL0(c, VA_RENDER_ROOT, NULL) || hud_clock(c, 0x0045db6du, &now))
            return 1;
    } while (end > now);
    return HCALL0(c, VA_JOLT_END, NULL);
}

/* 0x45db7b..0x45dc37: delete[] of [0x4d274c] and [0x4d2750] when set; per
 * palette object A of [0x4d2744], [0x4d2748] when set: words A+4, A+0xc <-
 * 0, the counter += 0x496406(A, 0) (the pointer re-read after the call),
 * A's slot +0x18 with (A, 3); byte [0x49aef4] <- 1. EAX: the second
 * object's slot +0x18 result, 0 without that object. */
static int drop_release(hud_ctx *c, uint32_t *eax) {
    for (uint32_t a = VA_INTRO_ARRAY_A; a <= VA_INTRO_ARRAY_B; a += 4u) {
        uint32_t x = hr32(c, a);
        if (x && HCALL(c, HUD_VA_DELETE_ARRAY, NULL, x)) return 1;
    }
    for (uint32_t a = VA_INTRO_PAL_A; a <= VA_INTRO_PAL_B; a += 4u) {
        uint32_t o = hr32(c, a), n = 0;
        *eax = 0;
        if (!o) continue;
        hw16(c, o + 4u, 0);
        hw16(c, o + 0xcu, 0);
        if (HCALL(c, HUD_VA_RTTI_COUNT, &n, o, 0u)) return 1;
        hud_counter(c, (int32_t)n);
        if (HVCALL(c, o, 0x18u, eax, o, 3u)) return 1;
    }
    hw8(c, 0x0049aef4u, 1);
    return 0;
}

/* 0x45d85c(): drop_begin; for z from P.z - 4000 while z < P.z (signed; the
 * sums wrap as 32-bit values), step 35: drop_descent(z); drop_land;
 * drop_release. */
static int drop_sequence(hud_ctx *c, uint32_t *eax) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, DROP_FRAME);
    if (!f) return hud_refuse(c, 0x0045d85cu, "drop sequence", "the own code's call area is exhausted");
    drop_state d;
    memset(&d, 0, sizeof(d));
    d.ebp = f + DROP_FRAME;
    int bad = drop_begin(c, &d);
    for (uint32_t z = d.z - 4000u; !bad && (int32_t)z < (int32_t)d.z; z += 0x23u) bad = drop_descent(c, &d, z);
    if (!bad) bad = drop_land(c, &d) || drop_release(c, eax);
    hud_frame_pop(c, top);
    return bad;
}
void es2_sim_service_drop_sequence(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (drop_sequence(&c, &sc->eax)) hud_failed(sc);
}
