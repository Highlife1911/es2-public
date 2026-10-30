/* The guest callees of the mission-load root 0x461608 as own code
 * (es2/sim/load.h). Contract: mission_load.md, "the mission load in the own
 * process" (every store and call below with its listing address range);
 * "listing" = llvm-objdump -d -M intel of the reference DBSIM.EXE (SHA-256
 * 8561aa47...c4ad). The root and the load pieces stay registry hooks; what they
 * call that is not a hook is a service row here. The original's internal
 * callees are own functions; hooks, other packages' services and the imports
 * are called through the dispatch over the bound main loop's call area. Guest
 * state is read and written only through the guest view.
 *
 * Headless entries (pixels, presents, GDI objects) are marked "headless:"
 * with the reason read from the bytes: they write nothing in guest memory
 * outside the frame bits, the stack and platform objects. */
#include <string.h>
#include "es2/sim/load.h"
#include "es2/sim/memory.h"
#include "es2/sim/services.h"
#include "es2/sim/present.h"
#include "es2/sim/image_data.h"

/* ---- guest VAs */
#define VA_COUNTER_POINTER 0x006cbee4u
#define VA_DEVICES 0x006c5eb4u          /* the graphics library's current object per kind (0x480590) */
#define VA_CURRENT_SURFACE 0x006c5ed0u  /* kind 7 */
#define VA_GC 0x004d24ceu               /* G+0x12: the graphics context object */
#define VA_GC_CURRENT 0x006c5f70u
#define VA_CLIP_CURRENT 0x004a3614u
#define VA_DD_ON 0x004d255eu            /* G+0xa2: DirectDraw */
#define VA_MEMORY_DC 0x004d3034u        /* D+0x14 */
#define VA_PALETTE_HANDLE 0x004d302cu   /* D+0xc */
#define VA_DISPLAY 0x004d3020u
#define VA_BITS 0x004d3028u             /* D+8 */
#define VA_PATH_BUFFER 0x006cbc0cu      /* 0x492b90's result */
#define VA_DIR_BUFFER 0x006cbc2au       /* 0x492bec's result */
#define VA_ARENA_CURRENT 0x006c5830u
#define VA_PALETTE_CURRENT 0x004d3b2cu
#define VA_LOGPALETTE 0x004d3728u       /* version, count, 256 PALETTEENTRY at 0x4d372c */
#define VA_PAL_ENTRIES 0x004d372cu
#define VA_RANGES 0x004a4fa5u           /* 0x486e10's table: 8 bytes per palette range */
/* Callees (dispatch). */
#define VA_NEW 0x0047749cu
#define VA_NEW_ARRAY 0x004774f0u
#define VA_DELETE 0x00477548u
#define VA_DELETE_ARRAY 0x0047758cu
#define VA_MEMSET 0x0049681au
#define VA_STRCAT 0x004967eau
#define VA_STRCHR 0x004967a2u
#define VA_OPEN 0x004967c6u
#define VA_CLOSE 0x00496874u
#define VA_PATH_ROOT 0x0045ee00u
#define VA_STREAM_OPEN 0x0047ad40u      /* the read stream R (rows of input.c) */
#define VA_STREAM_READ 0x0047af98u
#define VA_STREAM_DESTROY 0x0047ad78u
#define VA_VOL_OPEN 0x004730e8u         /* hook stream_open (the VOL-aware stream S) */
#define VA_VOL_DESTROY 0x00472f64u      /* hook stream_destroy */
#define VA_DTS_LOAD 0x0047a144u         /* hook dts_load */
#define VT_VOL_STREAM 0x004a0e10u
#define VT_STREAM_BASE 0x004a16e8u

static es2_sim_load *bound_load;
void es2_sim_load_bind(es2_sim_load *load) { bound_load = load; }
es2_sim_load *es2_sim_load_bound(void) { return bound_load; }
void es2_sim_load_headless(es2_sim_load *load, es2_sim_loop *loop) {
    memset(load, 0, sizeof(*load));
    load->loop = loop;
    /* What GDI answers on a 32-bit desktop (measured on the build host:
     * GetSystemPaletteUse 0, GetDeviceCaps(NUMCOLORS) -1,
     * GetSystemPaletteEntries 0 entries; mission_load.md, "the mission load in
     * the own process"). */
    load->platform.palette_use = 0;
    load->platform.num_colors = 0xffffffffu;
    load->platform.palette_handle = 0x0000c0deu;
    load->platform.paint_dc = 0x0000dc01u;
}

/* ---- guest view and calls */
typedef struct ctx { es2_sim_load *ld; es2_sim_loop *l; const es2_guest_memory *m; } ctx;
static uint32_t r32(const ctx *c, uint32_t a) { return c->m->read32(c->m->context, a); }
static uint16_t r16(const ctx *c, uint32_t a) { return c->m->read16(c->m->context, a); }
static uint8_t r8(const ctx *c, uint32_t a) { return es2_sim_read8(c->m, a); }
static void w32(const ctx *c, uint32_t a, uint32_t v) { c->m->write32(c->m->context, a, v); }
static void w16(const ctx *c, uint32_t a, uint16_t v) { c->m->write16(c->m->context, a, v); }
static void w8(const ctx *c, uint32_t a, uint8_t v) { es2_sim_write8(c->m, a, v); }
static uint32_t sx16(uint16_t v) { return (uint32_t)(int32_t)(int16_t)v; }
static void counter(const ctx *c, int32_t n) {
    uint32_t p = r32(c, VA_COUNTER_POINTER);
    w32(c, p, r32(c, p) + (uint32_t)n);
}
static int refuse(ctx *c, uint32_t va, const char *name, const char *reason) {
    c->ld->refused_va = va;
    return es2_sim_loop_refuse(c->l, va, name, reason);
}
/* The original reads, writes or calls through NULL at va (es2_sim_loop_null_refuse). */
static int null_refuse(ctx *c, uint32_t va, const char *name, const char *reason) {
    c->ld->refused_va = va;
    return es2_sim_loop_null_refuse(c->l, va, name, reason);
}
/* D: the original's IDIV of EDX:EAX = dividend by the divisor faults at va
 * (es2_sim_loop_divide_refuse). */
static int divide_refuse(ctx *c, uint32_t va, uint64_t dividend, uint32_t divisor, const char *name, const char *reason) {
    c->ld->refused_va = va;
    return es2_sim_loop_divide_refuse(c->l, va, dividend, divisor, 1, name, reason);
}
/* A call through the dispatch with the given entry registers (NULL: zero). */
static int call_regs(ctx *c, uint32_t va, const uint32_t *slots, unsigned count, const uint32_t regs[3], es2_sim_result *out) {
    es2_sim_loop *l = c->l;
    uint32_t bytes = 4u * (count ? count : 1u), top = l->stack_hi;
    if (top - l->stack_lo < bytes) return refuse(c, va, NULL, "the own code's call area is exhausted");
    l->stack_hi -= bytes;
    for (unsigned i = 0; i < count; ++i) w32(c, l->stack_hi + 4u * i, slots[i]);
    uint32_t zero[3] = {0, 0, 0};
    es2_sim_result r;
    memset(&r, 0, sizeof(r));
    int refused = es2_sim_call(l->dispatch, va, c->m->host(c->m->context, l->stack_hi), count, regs ? regs : zero, &r);
    l->stack_hi = top;
    if (refused) {
        c->ld->refused_va = va;
        if (l->outcome == ES2_SIM_LOOP_EXITED) return 1;   /* the process ended inside */
        l->outcome = ES2_SIM_LOOP_REFUSED;
        l->refused_va = va;
        return 1;
    }
    if (out) *out = r;
    return 0;
}
static int call(ctx *c, uint32_t va, const uint32_t *slots, unsigned count, uint32_t *eax) {
    es2_sim_result r;
    if (call_regs(c, va, slots, count, NULL, &r)) return 1;
    if (eax) *eax = r.eax;
    return 0;
}
#define CALL(c, va, eax, ...) call((c), (va), (const uint32_t[]){__VA_ARGS__}, \
    (unsigned)(sizeof((const uint32_t[]){__VA_ARGS__}) / sizeof(uint32_t)), (eax))
/* The error report 0x479c78(0x6c56c8, 5, a, file, line, text) as the original
 * calls it at its sites in this file (own_process.md, "the error report
 * 0x479c78 at the sites that refused"). Severity 5 ends the process inside the
 * report (windowed ExitProcess(1); headless its __exit is refused at 0x49679c);
 * when it returns (a test's recorder) the caller goes on with the instructions
 * after the call, as the original's bytes do. The third argument is the
 * original's `sete` of the failed test, 0 at every site here. */
#define VA_FATAL_REPORT 0x00479c78u
#define VA_REPORT_RECORD 0x006c56c8u
static int fatal_report(ctx *c, uint32_t file, uint32_t line, uint32_t text) {
    return CALL(c, VA_FATAL_REPORT, NULL, VA_REPORT_RECORD, 5u, 0u, file, line, text);
}
/* A virtual call through slot `offset` of the object's vtable. */
static int vcall(ctx *c, uint32_t object, uint32_t offset, const uint32_t *slots, unsigned count, uint32_t *eax) {
    return call(c, r32(c, r32(c, object) + offset), slots, count, eax);
}
/* A zero-filled block of the call area for the original's stack locals. */
static uint32_t frame_push(ctx *c, uint32_t bytes) {
    bytes = (bytes + 3u) & ~3u;
    if (c->l->stack_hi - c->l->stack_lo < bytes + 0x400u) return 0;
    c->l->stack_hi -= bytes;
    for (uint32_t i = 0; i < bytes; i += 4u) w32(c, c->l->stack_hi + i, 0);
    return c->l->stack_hi;
}
static void frame_pop(ctx *c, uint32_t top) { c->l->stack_hi = top; }

static int serving(es2_sim_service_call *sc, ctx *c) {
    c->ld = bound_load;
    if (!c->ld || !c->ld->loop) { sc->refused = 1; sc->reason = "no load layer bound (H5-05)"; return 1; }
    c->l = c->ld->loop;
    c->m = c->l->memory;
    return 0;
}
static void failed(es2_sim_service_call *sc) { sc->refused = 1; sc->reason = "a call inside the service was refused"; }
#define SERVE(sc, c) ctx c; if (serving((sc), &c)) return

/* String helpers on guest memory (the original's inline rep movs/scas). */
static void gstrcpy(const ctx *c, uint32_t d, uint32_t s) { uint32_t i = 0; uint8_t b; do { b = r8(c, s + i); w8(c, d + i, b); ++i; } while (b); }

/* ======================================================================
 * Step 3 of the root: the graphics context 0x461294 (listing
 * 0x461294..0x461302), its constructor 0x480c34 (0x480c34..0x480d43) and
 * 0x480d44 (0x480d44..0x480d6e) with 0x480ecf (0x480ecf..0x480ef0).
 *
 * new(0x239) = O; the constructor: [O] <- 0x4a69f0, 0x4a3368, counter + 1
 * (the element constructors 0x48de2f/0x49545b on O+0x210 and O+0x220 store
 * nothing), [O] <- 0x4a2e10; the current surface s = [0x6c5ed0] and its
 * first clip record p = [s+0x40]: 0x81 dwords p -> O+4, then O+4+{0x204,
 * 0x208, 0x224, 0x228, 0x20c, 0x210, 0x214, 0x218, 0x21c, 0x220} from p,
 * byte O+0x230 <- 0, dwords O+0x231, O+0x235 <- 0, counter + 1 (EAX O, EDX
 * 0, ECX [p+0x220]); the caller's counter - 2; [0x4d24ce] <- O;
 * [O+0x208] <- 1; 0x480d44(O): [0x6c5f70] <- O, [s+0x44] <- O+4 and,
 * when s is the current surface (always: s is read from the same slot),
 * [0x4a3614] <- O+4, then 0x489558(O+4): [0x4a3614] <- O+4 again. EAX the
 * frame record's saved head (the head at entry: FS:0 is unchanged), EDX 0,
 * ECX O (the POP ECX of the argument slot). A NULL O (the original then
 * goes on with O = 0 and faults at 0x4612e4) is refused. */
static int graphics_context(ctx *c, uint32_t *obj) {
    uint32_t o = 0;
    if (CALL(c, VA_NEW, &o, 0x239u)) return 1;
    if (!o) return null_refuse(c, 0x004612e4u, "graphics context", "operator new returned NULL (the original faults)");
    w32(c, o, 0x004a69f0u);
    w32(c, o, 0x004a3368u);
    counter(c, 1);
    w32(c, o, 0x004a2e10u);
    uint32_t s = r32(c, VA_CURRENT_SURFACE);
    if (!s) return null_refuse(c, 0x00480ec4u, "graphics context", "no current surface (the original reads through NULL)");
    uint32_t p = r32(c, s + 0x40u), d = o + 4u;
    if (!p) return null_refuse(c, 0x00480c93u, "graphics context", "no clip record (the original reads through NULL)");
    for (uint32_t i = 0; i < 0x81u; ++i) w32(c, d + 4u * i, r32(c, p + 4u * i));
    static const uint32_t tail[] = {0x204u, 0x208u, 0x224u, 0x228u, 0x20cu, 0x210u, 0x214u, 0x218u, 0x21cu, 0x220u};
    for (unsigned i = 0; i < sizeof(tail) / sizeof(tail[0]); ++i) w32(c, d + tail[i], r32(c, p + tail[i]));
    w8(c, o + 0x230u, 0);
    w32(c, o + 0x231u, 0);
    w32(c, o + 0x235u, 0);
    counter(c, 1);
    counter(c, -2);
    w32(c, VA_GC, o);
    w32(c, o + 0x208u, 1);
    w32(c, VA_GC_CURRENT, o);
    w32(c, s + 0x44u, o + 4u);
    if (s == r32(c, VA_CURRENT_SURFACE)) w32(c, VA_CLIP_CURRENT, o + 4u);
    w32(c, VA_CLIP_CURRENT, o + 4u);
    *obj = o;
    return 0;
}
static uint32_t fs0_head(void) {
#if defined(__i386__)
    uint32_t v;
    __asm__ volatile("movl %%fs:0, %0" : "=r"(v));
    return v;
#else
    return 0;
#endif
}
void es2_sim_service_graphics_context(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    uint32_t o;
    if (graphics_context(&c, &o)) { failed(sc); return; }
    sc->eax = fs0_head(); sc->edx = 0; sc->ecx = o;
}

/* The frame clear 0x46548c (listing 0x46548c..0x465616). DirectDraw
 * (G+0xa2 nonzero): the modelled surfaces (es2_sim_present_dd_clear), EAX
 * the last DirectDraw call's. GDI with a memory DC [0x4d3034]:
 * GetClientRect, InvalidateRect(hwnd, rect, 1), BeginPaint, memset(the
 * bits [0x4d3028], 0, the descriptor's size [[0x4d3020]+6]), StretchBlt to
 * the window (10 x 10 of the source, pixels only), with a palette handle
 * [0x4d302c] SelectPalette(dc, ESI, 1) and RealizePalette, EndPaint.
 * Headless: the memset (the frame's bytes) and the counts; the window side
 * writes no guest memory (the PAINTSTRUCT and the rectangle are stack
 * locals). EAX EndPaint's TRUE. */
static int frame_clear(ctx *c, uint32_t *eax) {
    es2_sim_load_platform *p = &c->ld->platform;
    *eax = 1;
    if (r32(c, VA_DD_ON)) return es2_sim_present_dd_clear(eax);
    if (!r32(c, VA_MEMORY_DC)) return 0;
    ++p->invalidate;
    ++p->begin_paint;
    if (CALL(c, VA_MEMSET, NULL, r32(c, VA_BITS), 0u, r32(c, r32(c, VA_DISPLAY) + 6u))) return 1;
    ++p->stretch_blt;
    if (r32(c, VA_PALETTE_HANDLE)) { ++p->select_palette; ++p->realize_palette; }
    ++p->end_paint;
    return 0;
}
void es2_sim_service_frame_clear(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    uint32_t eax;
    if (frame_clear(&c, &eax)) { failed(sc); return; }
    sc->eax = eax;
}

/* ======================================================================
 * The path builders 0x492b90 (0x492b90..0x492beb) and 0x492bec
 * (0x492bec..0x492c4a). 0x492b90(name, ext): name copied to 0x6cbc0c,
 * 0x492b68(buffer, '.', 0) (_strchr; a hit is cut), _strcat(".") and
 * _strcat(ext); EAX the buffer. 0x492bec(name, ext): b = 0x492b90(name,
 * ext); ext copied to 0x6cbc2a, _strcat("\\"), _strcat(b); EAX 0x6cbc2a. */
static int path_ext(ctx *c, uint32_t name, uint32_t ext, uint32_t *out) {
    gstrcpy(c, VA_PATH_BUFFER, name);
    uint32_t hit = 0;
    if (CALL(c, VA_STRCHR, &hit, VA_PATH_BUFFER, 0x2eu)) return 1;
    if (hit) w8(c, hit, 0);
    uint32_t r = 0;
    if (CALL(c, VA_STRCAT, &r, VA_PATH_BUFFER, 0x004a6939u)) return 1;
    if (CALL(c, VA_STRCAT, &r, r, ext)) return 1;
    *out = r;
    return 0;
}
static int path_dir(ctx *c, uint32_t name, uint32_t ext, uint32_t *out) {
    uint32_t b = 0, r = 0;
    if (path_ext(c, name, ext, &b)) return 1;
    gstrcpy(c, VA_DIR_BUFFER, ext);
    if (CALL(c, VA_STRCAT, &r, VA_DIR_BUFFER, 0x004a693bu)) return 1;
    if (CALL(c, VA_STRCAT, &r, r, b)) return 1;
    *out = r;
    return 0;
}
void es2_sim_service_path_dir(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    uint32_t r;
    if (path_dir(&c, sc->slots[0], sc->slots[1], &r)) { failed(sc); return; }
    sc->eax = r;
}

/* ======================================================================
 * The launch peek 0x4614c0(world, flyer) (listing 0x4614c0..0x461605): the
 * read stream R at the original's f-0Ch (0xb bytes), opened on the path
 * 0x45ee00("data\\script") + 0x492b90 with [0x4a09f0] ("dat"); a status
 * after the open is the fatal report 0x479c78 (0x461524; called, the
 * original ends the process there); R.read(0x14, f-44h) (0x47af98); *world <- the word
 * f-44h; close 0x47ae28; reopen 0x47ae68 on 0x45ee00("data\\player.mec"),
 * a status again fatal; five R.read(2, f-2Eh) through the vtable slot
 * +0x18 (= 0x47af98); *flyer <- (word f-2Eh = 8); the destructor
 * 0x47ad78(R, 2). The words read are the script header's first word (the
 * world) and the fifth word of player.mec (8: a flyer chassis, mission_load.md
 * section 1). */
static int stream_close(ctx *c, uint32_t r) {
    if (r8(c, r + 0xau) && r16(c, r + 8u) != 0xffffu) {
        uint32_t x = 0;
        if (CALL(c, VA_CLOSE, &x, sx16(r16(c, r + 8u)))) return 1;
        if (x == 0xffffffffu) { w32(c, r + 4u, 3); return 0; }
    }
    w16(c, r + 8u, 0xffffu);
    w32(c, r + 4u, 5);
    return 0;
}
static int stream_reopen(ctx *c, uint32_t r, uint32_t path) {
    if (stream_close(c, r)) return 1;
    uint32_t h = 0;
    /* The original pushes two slots; the third (mode) is read by _open only with O_CREAT. */
    if (CALL(c, VA_OPEN, &h, path, 0x8000u, 0u)) return 1;
    w16(c, r + 8u, (uint16_t)h);
    if ((uint16_t)h == 0xffffu) { w32(c, r + 4u, 1); return 0; }
    w32(c, r + 4u, 0);
    w8(c, r + 0xau, 1);
    return 0;
}
static int launch_peek(ctx *c, uint32_t world, uint32_t flyer) {
    uint32_t top = c->l->stack_hi, f = frame_push(c, 0x44u);
    if (!f) return refuse(c, 0x004614c0u, "launch peek", "the own code's call area is exhausted");
    f += 0x44u;   /* the original's EBP */
    uint32_t r = f - 0xcu, p = 0, q = 0;
    int bad = CALL(c, VA_PATH_ROOT, &p, 0x0049f861u) || path_ext(c, p, r32(c, 0x004a09f0u), &q) ||
              CALL(c, VA_STREAM_OPEN, NULL, r, q);
    if (!bad && r32(c, r + 4u)) bad = fatal_report(c, 0x0049f86du, 0xdeu, q);   /* 0x461524 */
    if (!bad) bad = CALL(c, VA_STREAM_READ, NULL, r, 0x14u, f - 0x44u);
    if (!bad) {
        w16(c, world, r16(c, f - 0x44u));
        bad = stream_close(c, r) || CALL(c, VA_PATH_ROOT, &p, 0x0049f876u) || stream_reopen(c, r, p);
    }
    if (!bad && r32(c, r + 4u)) bad = fatal_report(c, 0x0049f886u, 0xe4u, 0x0049f88fu);   /* 0x461587 */
    for (int i = 0; !bad && i < 5; ++i) bad = vcall(c, r, 0x18u, (const uint32_t[]){r, 2u, f - 0x2eu}, 3, NULL);
    if (!bad) {
        w16(c, flyer, r16(c, f - 0x2eu) == 8u ? 1u : 0u);
        bad = CALL(c, VA_STREAM_DESTROY, NULL, r, 2u);
    }
    frame_pop(c, top);
    return bad;
}
void es2_sim_service_launch_peek(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    if (launch_peek(&c, sc->slots[0], sc->slots[1])) { failed(sc); return; }
    sc->eax = fs0_head();
}

/* ======================================================================
 * Load by name 0x45cdf4(path) (listing 0x45cdf4..0x45ce4b) and 0x465f7c
 * (0x465f7c..0x465fd3, the same code with the descriptor 0x4a0818): the
 * VOL-aware stream S on the original's f-30h (0x472f14: the base class
 * 0x47a888 = [S] 0x4a16e8, [S+4] 0, counter + 1; [S] 0x4a0e10, +0x27,
 * +0x2b, +0xa, +0xe, +0x16 <- 0, word +8 <- 0xffff, byte +0x26 <- 0; the
 * hook stream_open(S, path); counter + 1); dts_load(S, 0) (hook) = X;
 * stream_destroy(S, 2) (hook). EAX X, EDX the head at entry. */
static int vol_stream(ctx *c, uint32_t s, uint32_t path, es2_sim_result *open) {
    w32(c, s, VT_STREAM_BASE);
    w32(c, s + 4u, 0);
    counter(c, 1);
    w32(c, s, VT_VOL_STREAM);
    w32(c, s + 0x27u, 0);
    w32(c, s + 0x2bu, 0);
    w32(c, s + 0xau, 0);
    w32(c, s + 0xeu, 0);
    w32(c, s + 0x16u, 0);
    w16(c, s + 8u, 0xffffu);
    w8(c, s + 0x26u, 0);
    es2_sim_result r;
    if (call_regs(c, VA_VOL_OPEN, (const uint32_t[]){s, path}, 2, NULL, &r)) return 1;
    if (open) *open = r;
    counter(c, 1);
    return 0;
}
/* The constructor 0x472f14(S, path) as a row (listing 0x472f14..0x472f63):
 * EAX S, EDX the open's, ECX the open's (the hook's registers). */
void es2_sim_service_vol_stream(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    es2_sim_result r;
    if (vol_stream(&c, sc->slots[0], sc->slots[1], &r)) { failed(sc); return; }
    sc->eax = sc->slots[0]; sc->edx = r.edx; sc->ecx = r.ecx;
}
static int load_by_name(ctx *c, uint32_t path, uint32_t *result) {
    uint32_t top = c->l->stack_hi, f = frame_push(c, 0x50u);
    if (!f) return refuse(c, 0x0045cdf4u, "load by name", "the own code's call area is exhausted");
    uint32_t s = f + 0x50u - 0x30u, x = 0;
    int bad = vol_stream(c, s, path, NULL);
    if (!bad) bad = CALL(c, VA_DTS_LOAD, &x, s, 0u);
    if (!bad) bad = CALL(c, VA_VOL_DESTROY, NULL, s, 2u);
    frame_pop(c, top);
    *result = x;
    return bad;
}
void es2_sim_service_load_by_name(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    uint32_t head = fs0_head(), x;
    if (load_by_name(&c, sc->slots[0], &x)) { failed(sc); return; }
    sc->eax = x; sc->edx = head;
}

/* The arena allocator 0x47a288(n, A) (listing 0x47a288..0x47a2c7; 0x47a2c8
 * is the same code): A non-NULL and used + n (32-bit) <= the limit [A+4]:
 * [A+8] += n, EAX = [A] + old used; else new[](n). EDX the result, ECX n
 * (new[]'s POP ECX) on the new[] path, else n. */
void es2_sim_service_arena_alloc(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    uint32_t n = sc->slots[0], a = sc->slots[1], r = 0;
    if (a && r32(&c, a + 8u) + n <= r32(&c, a + 4u)) {
        uint32_t used = r32(&c, a + 8u);
        w32(&c, a + 8u, used + n);
        r = r32(&c, a) + used;
    } else if (CALL(&c, VA_NEW_ARRAY, &r, n)) { failed(sc); return; }
    sc->eax = r; sc->edx = r; sc->ecx = n;
}

/* The GLPalette factory 0x4820d4(S, arg) (listing 0x4820d4..0x482145):
 * P = 0x47a2c8(0x14, [0x6c5830]); the constructor 0x4823da
 * (0x4823da..0x48240f): [P] <- 0x4a69f0, 0x4a3368, counter + 1, 0x4a30f8,
 * words +4 and +0xc <- 0, counter + 1; counter - 2; the read slot +0xc
 * (P, S, arg). EAX P, EDX the head at entry. A NULL P faults (refused).
 * The read 0x4821cc(P, S) (0x4821cc..0x4822c9): S.read(2, P+4),
 * S.read(2, P+6) (0x492430: the slot +0x18 of S); n = word P+4 nonzero:
 * P+8 <- 0x47a288(4n, arena), S.read(4n, P+8); S.read(2, P+0xc),
 * S.read(2, P+0xe); m = word P+0xc nonzero: P+0x10 <- 0x47a288(8m, arena)
 * and for i < m (signed words): S.read(2, w), b = 0x47a288(2w, arena),
 * S.read(2w, b), word [P+0x10]+8i+4 <- w, [P+0x10]+8i <- b. */
static int sread(ctx *c, uint32_t s, uint32_t n, uint32_t buf) { return vcall(c, s, 0x18u, (const uint32_t[]){s, n, buf}, 3, NULL); }
static int arena(ctx *c, uint32_t n, uint32_t *out) {
    return CALL(c, 0x0047a288u, out, n, r32(c, VA_ARENA_CURRENT));
}
static int palette_read(ctx *c, uint32_t p, uint32_t s) {
    if (sread(c, s, 2, p + 4u) || sread(c, s, 2, p + 6u)) return 1;
    uint16_t n = r16(c, p + 4u);
    if (n) {
        uint32_t b = 0;
        if (arena(c, sx16(n) << 2, &b)) return 1;
        w32(c, p + 8u, b);
        if (sread(c, s, sx16(r16(c, p + 4u)) << 2, r32(c, p + 8u))) return 1;
    }
    if (sread(c, s, 2, p + 0xcu) || sread(c, s, 2, p + 0xeu)) return 1;
    uint16_t m = r16(c, p + 0xcu);
    if (m) {
        uint32_t b = 0;
        if (arena(c, sx16(m) << 3, &b)) return 1;
        w32(c, p + 0x10u, b);
        uint32_t top = c->l->stack_hi, f = frame_push(c, 8u);
        if (!f) return refuse(c, 0x004821ccu, "palette read", "the own code's call area is exhausted");
        f += 8u;
        int bad = 0;
        for (int16_t i = 0; !bad && i < (int16_t)r16(c, p + 0xcu); ++i) {
            bad = sread(c, s, 2, f - 2u);
            uint32_t w = sx16(r16(c, f - 2u)), q = 0;
            if (!bad) bad = arena(c, w * 2u, &q);
            if (!bad) { w32(c, f - 8u, q); bad = sread(c, s, w * 2u, q); }
            if (!bad) {
                w16(c, r32(c, p + 0x10u) + 8u * (uint32_t)(int32_t)i + 4u, r16(c, f - 2u));
                w32(c, r32(c, p + 0x10u) + 8u * (uint32_t)(int32_t)i, r32(c, f - 8u));
            }
        }
        frame_pop(c, top);
        if (bad) return 1;
    }
    return 0;
}
void es2_sim_service_palette_new(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    uint32_t head = fs0_head(), p = 0;
    if (CALL(&c, 0x0047a2c8u, &p, 0x14u, r32(&c, VA_ARENA_CURRENT))) { failed(sc); return; }
    if (!p) { null_refuse(&c, 0x0048212du, "palette", "the allocation returned NULL (the original faults)"); failed(sc); return; }
    w32(&c, p, 0x004a69f0u);
    w32(&c, p, 0x004a3368u);
    counter(&c, 1);
    w32(&c, p, 0x004a30f8u);
    w16(&c, p + 4u, 0);
    w16(&c, p + 0xcu, 0);
    counter(&c, 1);
    counter(&c, -2);
    if (vcall(&c, p, 0xcu, (const uint32_t[]){p, sc->slots[0], sc->slots[1]}, 3, NULL)) { failed(sc); return; }
    sc->eax = p; sc->edx = head;
}
void es2_sim_service_palette_read(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    if (palette_read(&c, sc->slots[0], sc->slots[1])) { failed(sc); return; }
}

/* ======================================================================
 * The palette set 0x481fe0(P) (listing 0x481fe0..0x482011): 0x4820b7(the
 * current kind-0 object 0x48d99e, sx [P+0xe], sx [P+0xc], [P+0x10], sx
 * [P+6]) -> 0x486e10(start, count, src, d) (0x486e10..0x486e3e): count
 * pairs of dwords from src to 0x4a4fa5 + 8 start; then the apply 0x465e28(P)
 * (0x465e28..0x465f7a): with P nonzero [0x4d3b2c] <- P; a local colour
 * table from P's colours (stack); 0x465c40 (GetDC, CreatePalette of a
 * black palette, select, realize, DeleteObject, ReleaseDC: no guest
 * write); [0x4d302c] <- 0x465a24(local, 0x100, [P+8]); GetDC(0),
 * SelectPalette, RealizePalette, SetDIBColorTable(memory DC, 0, 256,
 * local), ReleaseDC; DirectDraw (G+0xa2) refused.
 * 0x465a24(colours, n, src) (0x465a24..0x465c3c): word 0x4d3728 <- 0x300,
 * word 0x4d372a <- 0x100; GetSystemPaletteUse(dc) = 2: the n source entries
 * into 0x4d372c (bytes 2, 1, 0, flags 1), the rest's flags 0, the end
 * colours 0x4d3b28.. <- ff ff ff 00 and entry 0 <- 0; otherwise r =
 * GetDeviceCaps(dc, 0x18 = NUMCOLORS) / 2 (sar with the sign fix-up),
 * GetSystemPaletteEntries(dc, 0, 256, 0x4d372c), the first r flags <- 0,
 * entries r .. 255-r from src (bytes 2, 1, 0) each nudged by 3 (+3 below
 * 0xfc, else -3) on the blue byte when it equals one of the first r or last
 * r entries, flags 5; the tail's flags 0; ReleaseDC; CreatePalette(0x4d3728). */
static void palette_entries(ctx *c, const uint8_t *src) {
    es2_sim_load_platform *pl = &c->ld->platform;
    w16(c, VA_LOGPALETTE, 0x300u);
    w16(c, VA_LOGPALETTE + 2u, 0x100u);
    ++pl->get_dc;
    ++pl->palette_use_calls;
    if (pl->palette_use == 2u) {
        uint32_t i = 0, e = VA_PAL_ENTRIES;
        for (; (int32_t)i < 0x100; ++i, e += 4u) {
            w8(c, e, src[4u * i + 2u]);
            w8(c, e + 1u, src[4u * i + 1u]);
            w8(c, e + 2u, src[4u * i]);
            w8(c, e + 3u, 1);
        }
        for (uint8_t k = 0; k < 4; ++k) w8(c, 0x004d3b28u + k, k < 3 ? 0xffu : 0);
        for (uint8_t k = 0; k < 4; ++k) w8(c, VA_PAL_ENTRIES + k, 0);
    } else {
        ++pl->device_caps;
        int32_t r = (int32_t)pl->num_colors;
        r = r / 2;   /* sar 1 with the sign fix-up: rounds toward zero */
        ++pl->palette_entries;   /* headless: no system palette, nothing written */
        for (int32_t i = 0; i < r; ++i) w8(c, VA_PAL_ENTRIES + 4u * (uint32_t)i + 3u, 0);
        int32_t i = r;
        for (; i < 0x100 - r; ++i) {
            uint32_t e = VA_PAL_ENTRIES + 4u * (uint32_t)i;
            w8(c, e, src[4u * (uint32_t)i + 2u]);
            w8(c, e + 1u, src[4u * (uint32_t)i + 1u]);
            w8(c, e + 2u, src[4u * (uint32_t)i]);
            for (int32_t j = 0; j < r; ++j) {
                uint32_t q = VA_PAL_ENTRIES + 4u * (uint32_t)j;
                if (r8(c, q + 2u) == r8(c, e + 2u) && r8(c, q + 1u) == r8(c, e + 1u) && r8(c, q) == r8(c, e))
                    w8(c, e + 2u, (uint8_t)(r8(c, e + 2u) < 0xfcu ? r8(c, e + 2u) + 3u : r8(c, e + 2u) - 3u));
            }
            for (int32_t j = 0x100 - r; j < 0x100; ++j) {
                uint32_t q = VA_PAL_ENTRIES + 4u * (uint32_t)j;
                if (r8(c, q + 2u) == r8(c, e + 2u) && r8(c, q + 1u) == r8(c, e + 1u) && r8(c, q) == r8(c, e))
                    w8(c, e + 2u, (uint8_t)(r8(c, e + 2u) < 0xfcu ? r8(c, e + 2u) + 3u : r8(c, e + 2u) - 3u));
            }
            w8(c, e + 3u, 5);
        }
        for (; i < 0x100; ++i) w8(c, VA_PAL_ENTRIES + 4u * (uint32_t)i + 3u, 0);
    }
    ++pl->release_dc;
    ++pl->create_palette;
}
static int palette_apply(ctx *c, uint32_t p) {
    es2_sim_load_platform *pl = &c->ld->platform;
    if (!p) return 0;
    w32(c, VA_PALETTE_CURRENT, p);
    /* The local table: per entry i the source's bytes 2, 1, 0 when its byte
     * 3 has bit 0, else, when bit 1 of the matching dword of P+8's ...
     * (the stack copy; only SetDIBColorTable reads it). */
    uint8_t local[1024];
    memset(local, 0, sizeof(local));
    uint32_t colours = r32(c, p + 8u);
    for (uint32_t i = 0; i < 0x100u; ++i) {
        uint32_t e = colours + 4u * i;
        if (r8(c, e + 3u) & 1u) {
            local[4u * i + 2u] = (uint8_t)(r8(c, e) << 2);
            local[4u * i + 1u] = (uint8_t)(r8(c, e + 1u) << 2);
            local[4u * i] = (uint8_t)(r8(c, e + 2u) << 2);
        } else if ((int32_t)r32(c, colours + 4u * i) >> 24 & 2) {
            local[4u * i + 2u] = r8(c, e);
            local[4u * i + 1u] = r8(c, e + 1u);
            local[4u * i] = r8(c, e + 2u);
        }
    }
    /* 0x465c40: the black palette in and out (no guest write). */
    pl->get_dc += 1; pl->create_palette += 1; pl->select_palette += 2; pl->realize_palette += 1;
    pl->delete_object += 1; pl->release_dc += 1;
    palette_entries(c, local);
    w32(c, VA_PALETTE_HANDLE, pl->palette_handle);
    ++pl->get_dc; ++pl->select_palette; ++pl->realize_palette;
    ++pl->color_table_uploads;
    memcpy(pl->color_table, local, sizeof(local));
    ++pl->release_dc;
    if (r32(c, VA_DD_ON)) return es2_sim_present_dd_palette(local);
    return 0;
}
static int palette_set(ctx *c, uint32_t p) {
    int32_t start = (int32_t)sx16(r16(c, p + 0xeu)), count = (int32_t)sx16(r16(c, p + 0xcu));
    uint32_t src = r32(c, p + 0x10u);
    for (int32_t i = 0; count && i < count; ++i) {
        w32(c, VA_RANGES + 8u * (uint32_t)(start + i), r32(c, src + 8u * (uint32_t)i));
        w32(c, VA_RANGES + 8u * (uint32_t)(start + i) + 4u, r32(c, src + 8u * (uint32_t)i + 4u));
    }
    return palette_apply(c, p);
}
void es2_sim_service_palette_set(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    if (palette_set(&c, sc->slots[0])) { failed(sc); return; }
}

/* ======================================================================
 * The loading screen 0x461450(name) (listing 0x461450..0x4614be): ext =
 * [0x4a0a28] ("hba") when the signed byte [0x4d2537] is 3, else [0x4a0a10]
 * ("dba"); A = 0x45cdf4(0x492bec(name, ext)) (a GLBitmapArray); the blit
 * 0x4816a8([[A+8]], point (0, 0), 0); the present 0x464a14; counter + 1;
 * 0x48349c(A, 3). The point is the local pair f-8/f-4 (zero). */
static int bitmap_array_destroy(ctx *c, uint32_t a, uint32_t flags, uint32_t *eax);
static int loading_screen(ctx *c, uint32_t name, uint32_t *eax) {
    uint32_t ext = (int8_t)r8(c, 0x004d2537u) == 3 ? r32(c, 0x004a0a28u) : r32(c, 0x004a0a10u), path = 0, a = 0;
    if (path_dir(c, name, ext, &path)) return 1;
    if (CALL(c, 0x0045cdf4u, &a, path)) return 1;
    if (!a) return null_refuse(c, 0x00461495u, "loading screen", "the bitmap array did not load (the original reads through NULL)");
    uint32_t top = c->l->stack_hi, f = frame_push(c, 8u);
    if (!f) return refuse(c, 0x00461450u, "loading screen", "the own code's call area is exhausted");
    es2_sim_present_site(0x004614a8u);   /* the present's call at 0x4614a3 */
    int bad = CALL(c, 0x004816a8u, NULL, r32(c, r32(c, a + 8u)), f, 0u) || CALL(c, 0x00464a14u, NULL, 0u);
    frame_pop(c, top);
    if (bad) return 1;
    counter(c, 1);
    return bitmap_array_destroy(c, a, 3, eax);
}
void es2_sim_service_loading_screen(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    uint32_t x = 0;
    if (loading_screen(&c, sc->slots[0], &x)) { failed(sc); return; }
    sc->eax = x;
}

/* 0x496406(p, offset) (listing 0x496406..0x496424): 0 for NULL, else the
 * RTTI dword [[[p+offset] - [[p+offset]-4]] - 0xc] + 0x20 (the count the
 * caller adds to the construction counter). The dword lies in CODE,
 * which is no-access in the guest; it comes from the own RTTI_COUNT runs
 * (es2_sim_dbsim_rtti_count), and only a descriptor outside them (synthetic
 * test images) is read through guest memory. */
static uint32_t rtti_count(const ctx *c, uint32_t p, uint32_t offset) {
    if (!p) return 0;
    uint32_t v = r32(c, p + offset);
    v -= r32(c, v - 4u);
    uint32_t t = r32(c, v - 0xcu), n;
    if (!es2_sim_dbsim_rtti_count(t + 0x20u, &n)) return n;
    return r32(c, t + 0x20u);
}
/* The GLBitmapArray destructor 0x48349c(A, flags) (listing
 * 0x48349c..0x483532): counter - 1; A nonzero: [A] <- 0x4a32ec; for i <
 * [A+4] (unsigned): e = [[A+8]+4i] nonzero: counter += 0x496406(e, 0), e's
 * slot +0x18 (e, 3); delete[]([A+8]); flags bit 0: delete(A). */
static int bitmap_array_destroy(ctx *c, uint32_t a, uint32_t flags, uint32_t *eax) {
    counter(c, -1);
    if (!a) return 0;
    w32(c, a, 0x004a32ecu);
    for (uint32_t i = 0; i < r32(c, a + 4u); ++i) {
        uint32_t e = r32(c, r32(c, a + 8u) + 4u * i);
        if (!e) continue;
        counter(c, (int32_t)rtti_count(c, e, 0));
        if (vcall(c, e, 0x18u, (const uint32_t[]){e, 3u}, 2, NULL)) return 1;
    }
    if (CALL(c, VA_DELETE_ARRAY, eax, r32(c, a + 8u))) return 1;
    if (flags & 1u) return CALL(c, VA_DELETE, eax, a);
    return 0;
}
void es2_sim_service_bitmap_array_destroy(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    uint32_t x = 0;
    if (bitmap_array_destroy(&c, sc->slots[0], sc->slots[1], &x)) { failed(sc); return; }
    sc->eax = x;
}
/* The GLBitmap destructor 0x4815b8(B, flags) (listing 0x4815b8..0x481612):
 * counter - 1; B nonzero: [B] <- 0x4a306c; word B+0x14 nonzero:
 * delete[]([B+0x16]); [B+0xa] nonzero: delete[]([B+0xe]); counter - 1; the
 * base destructor 0x4838fd(B, 0) (0x4838fd..0x483923: counter - 1, [B] <-
 * 0x4a3368, flag bit 0 delete); flags bit 0: delete(B). */
static int base_destroy(ctx *c, uint32_t b, uint32_t flags) {
    counter(c, -1);
    if (!b) return 0;
    w32(c, b, 0x004a3368u);
    if (flags & 1u) return CALL(c, VA_DELETE, NULL, b);
    return 0;
}
void es2_sim_service_bitmap_destroy(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    uint32_t b = sc->slots[0], flags = sc->slots[1];
    counter(&c, -1);
    if (b) {
        w32(&c, b, 0x004a306cu);
        if (r16(&c, b + 0x14u) && CALL(&c, VA_DELETE_ARRAY, NULL, r32(&c, b + 0x16u))) { failed(sc); return; }
        if (r32(&c, b + 0xau) && CALL(&c, VA_DELETE_ARRAY, NULL, r32(&c, b + 0xeu))) { failed(sc); return; }
        counter(&c, -1);
        if (base_destroy(&c, b, 0)) { failed(sc); return; }
        if ((flags & 1u) && CALL(&c, VA_DELETE, NULL, b)) { failed(sc); return; }
    }
}

/* ======================================================================
 * Step 5: 0x480af0(O, word) (listing 0x480af0..0x480b35): 0x480590(O, 0,
 * O): [O] <- 0, kind 0's slot [0x6c5eb4] <- O when free; when the slot is
 * O: 0x480b78(O, word): word O+4 <- word, EAX [O] & 1 (0 here: the
 * abort-notify registration 0x479cfc(0x480bc3) is not reached); counter +
 * 1; EAX O. */
void es2_sim_service_device_object(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    uint32_t o = sc->slots[0];
    w32(&c, o, 0);
    if (!r32(&c, VA_DEVICES)) w32(&c, VA_DEVICES, o);
    if (r32(&c, VA_DEVICES) == o) {
        w16(&c, o + 4u, (uint16_t)sc->slots[1]);
        if (r32(&c, o) & 1u && CALL(&c, 0x00479cfcu, NULL, 0x00480bc3u)) { failed(sc); return; }
    }
    counter(&c, 1);
    sc->eax = o; sc->edx = r32(&c, VA_COUNTER_POINTER); sc->ecx = 0;   /* 0x480bba's POP ECX of its 0 */
}
/* ======================================================================
 * Step 6: the sound init 0x462410(on, five unread words) (listing
 * 0x462410..0x462548; audio.md "Sound data"): the window-procedure handler
 * 0x462398 added (0x465fec); word [0x4d2d74] <- 5 voice channels; the
 * cache limit 1,000,000 with the word [0x4d2680] (-l) else 2,000,000; the
 * channel array 0x49667c(0, 0x42, 5, 1, 0x463624) -> [0x4d2d6c], cleared
 * (_memset 0x42 * n), each channel's dword +0 <- -1; the lip buffers
 * new[](100 n) -> [0x4d2d70]; word [0x4d2d68] (the sound table count) <-
 * 0. With the word `on` nonzero: new(0x4c) = X, its constructor 0x46379c
 * (data\sound.cfg) and counter - 1, [0x49f8ec] <- X, [X+0x44] <- the window
 * [0x4d3610], 0x463694(X, limit, 60, 90) (the driver); 0: [0x49f8ec] <- 0,
 * else the table and preloads 0x46254c. The context's functions are
 * audio.c's rows, called through the dispatch. A NULL X (the original
 * faults at 0x462514) is refused. EAX the head at entry. */
#define VA_VOICES 0x004d2d6cu
#define VA_VOICE_COUNT 0x004d2d74u
#define VA_LIPS 0x004d2d70u
#define VA_SOUND_COUNT 0x004d2d68u
#define VA_SOUND_CONTEXT 0x0049f8ecu
static int sound_init(ctx *c, uint32_t on) {
    if (CALL(c, 0x00465fecu, NULL, 0x00462398u)) return 1;
    w16(c, VA_VOICE_COUNT, 5);
    uint32_t limit = r16(c, 0x004d2680u) ? 1000000u : 2000000u, v = 0, lips = 0;
    if (CALL(c, 0x0049667cu, &v, 0u, 0x42u, sx16(r16(c, VA_VOICE_COUNT)), 1u, 0x00463624u)) return 1;
    w32(c, VA_VOICES, v);
    uint32_t n = sx16(r16(c, VA_VOICE_COUNT));
    if (CALL(c, VA_MEMSET, NULL, v, 0u, n * 0x42u)) return 1;
    for (uint32_t i = 0; (int32_t)i < (int32_t)sx16(r16(c, VA_VOICE_COUNT)); ++i) w32(c, r32(c, VA_VOICES) + 0x42u * i, 0xffffffffu);
    if (CALL(c, VA_NEW_ARRAY, &lips, sx16(r16(c, VA_VOICE_COUNT)) * 100u)) return 1;
    w32(c, VA_LIPS, lips);
    w16(c, VA_SOUND_COUNT, 0);
    if (!(uint16_t)on) return 0;
    uint32_t x = 0, ok = 0;
    if (CALL(c, VA_NEW, &x, 0x4cu)) return 1;
    if (!x) return null_refuse(c, 0x00462514u, "sound init", "operator new returned NULL (the original faults)");
    if (CALL(c, 0x0046379cu, NULL, x)) return 1;
    counter(c, -1);
    w32(c, VA_SOUND_CONTEXT, x);
    w32(c, x + 0x44u, r32(c, 0x004d3610u));
    if (CALL(c, 0x00463694u, &ok, x, limit, 0x3cu, 0x5au)) return 1;
    if (!ok) { w32(c, VA_SOUND_CONTEXT, 0); return 0; }
    return CALL(c, 0x0046254cu, NULL, 0u);
}
void es2_sim_service_sound_init(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    uint32_t head = fs0_head();
    if (sound_init(&c, sc->slots[0])) { failed(sc); return; }
    sc->eax = head;
}
/* The voice channel constructor 0x463624(ch) (listing 0x463624..0x46363f
 * with 0x46333c): dwords +0x2f and +0 <- 0, byte +0x3b <- 0, dword +0x37 <-
 * 0. EAX ch, EDX 0, ECX ch (the POP ECX of the pushed argument). */
void es2_sim_service_voice_channel(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    uint32_t e = sc->slots[0];
    w32(&c, e + 0x2fu, 0);
    w32(&c, e, 0);
    w8(&c, e + 0x3bu, 0);
    w32(&c, e + 0x37u, 0);
    sc->eax = e; sc->edx = 0; sc->ecx = e;
}
/* The context sizes 0x463694(X, limit, records, slots) (listing
 * 0x463694..0x463799): [X+0x18] <- limit, [X+0x10] <- records, [X+0x34] <-
 * new[](540 records), every record's dword +0 <- -1 (stride 540); [X+0x14]
 * <- slots, [X+0x38] <- new[](40 slots), every slot's dword +0 <- -1; then
 * the driver load 0x473708([X+0x44], X+0x24) (the SOS driver, audio.c,
 * through the dispatch); nonzero: EAX 1, else SetLastError(0x3eb) and 0.
 * A failed allocation (SetLastError(0xe), the frees) is refused. */
void es2_sim_service_sound_sizes(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    uint32_t x = sc->slots[0], records = sc->slots[2], slots = sc->slots[3], p = 0, ok = 0;
    w32(&c, x + 0x18u, sc->slots[1]);
    w32(&c, x + 0x10u, records);
    if (CALL(&c, VA_NEW_ARRAY, &p, records * 540u)) { failed(sc); return; }
    w32(&c, x + 0x34u, p);
    if (!p) { refuse(&c, 0x00463765u, "sound context", "new[] returned NULL (SetLastError and the frees are not modelled)"); failed(sc); return; }
    for (uint32_t i = 0; i < r32(&c, x + 0x10u); ++i) w32(&c, r32(&c, x + 0x34u) + 540u * i, 0xffffffffu);
    w32(&c, x + 0x14u, slots);
    if (CALL(&c, VA_NEW_ARRAY, &p, slots * 40u)) { failed(sc); return; }
    w32(&c, x + 0x38u, p);
    if (!p) { refuse(&c, 0x0046375cu, "sound context", "new[] returned NULL (SetLastError and the frees are not modelled)"); failed(sc); return; }
    for (uint32_t i = 0; i < r32(&c, x + 0x14u); ++i) w32(&c, r32(&c, x + 0x38u) + 40u * i, 0xffffffffu);
    if (CALL(&c, 0x00473708u, &ok, r32(&c, x + 0x44u), x + 0x24u)) { failed(sc); return; }
    if (!ok) { refuse(&c, 0x00463740u, "sound context", "the driver load failed (SetLastError(0x3eb) is not modelled)"); failed(sc); return; }
    sc->eax = 1;
}
/* ======================================================================
 * Step 7: 0x463370 (a bare RET: the registers stay), the renderer reset
 * 0x48dd08 (listing 0x48dd08..0x48dd85) and the AN class registration
 * 0x4913dc (0x4913dc..0x4913ef).
 * 0x48dd08: [0x6c604c], [0x6c6054] <- 0; the ten light-callback dwords
 * 0x6c60ac.. <- 0; byte [0x6cbc04] <- 1 (0x48de70); the 32 words 0x6c6068..
 * <- 0, 1, .., 31 (the default index list: word i holds i, AX counting up
 * at 0x48dd30..0x48dd41; 0 there would leave every polygon drawn through
 * the default list at its first point); _memset(0x6c60d4, 0, 0x5b30);
 * 0x47a0b8(0x4a63b0, [0x4a6488]) (the TS class table); 0x48de78(0x6c5ffc): _memset 0x20 bytes, words +0x10, +6,
 * +0 <- 0x4000 (an identity matrix); [0x6c5ff4], [0x6c5ff8] <- 0x6c5ffc.
 * EAX 0x6c5ffc. 0x47a0b8(table, n) (0x47a0b8..0x47a0e5): n nonzero: the
 * pair (table, n) at 0x6c5834 + 8 [0x4a1690], the count + 1. */
static void register_classes(ctx *c, uint32_t table, uint32_t n) {
    if (!n) return;
    uint32_t k = r32(c, 0x004a1690u);
    w32(c, 0x006c5838u + 8u * k, n);
    w32(c, 0x006c5834u + 8u * r32(c, 0x004a1690u), table);
    w32(c, 0x004a1690u, r32(c, 0x004a1690u) + 1u);
}
void es2_sim_service_bare_return(es2_sim_service_call *sc, void *user) { (void)sc; (void)user; }
void es2_sim_service_renderer_reset(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    w32(&c, 0x006c604cu, 0);
    w32(&c, 0x006c6054u, 0);
    for (uint32_t i = 0; i < 10u; ++i) w32(&c, 0x006c60acu + 4u * i, 0);
    w8(&c, 0x006cbc04u, 1);
    for (uint32_t i = 0; i < 0x20u; ++i) w16(&c, 0x006c6068u + 2u * i, (uint16_t)i);
    if (CALL(&c, VA_MEMSET, NULL, 0x006c60d4u, 0u, 0x5b30u)) { failed(sc); return; }
    register_classes(&c, 0x004a63b0u, r32(&c, 0x004a6488u));
    if (CALL(&c, VA_MEMSET, NULL, 0x006c5ffcu, 0u, 0x20u)) { failed(sc); return; }
    w16(&c, 0x006c5ffcu + 0x10u, 0x4000u);
    w16(&c, 0x006c5ffcu + 6u, 0x4000u);
    w16(&c, 0x006c5ffcu, 0x4000u);
    w32(&c, 0x006c5ff4u, 0x006c5ffcu);
    w32(&c, 0x006c5ff8u, 0x006c5ffcu);
    sc->eax = 0x006c5ffcu;
}
void es2_sim_service_an_classes(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    register_classes(&c, 0x004a687cu, r32(&c, 0x004a68acu));
}
/* ======================================================================
 * Element constructors and member-address leaves: bodies 55 8b ec 8b 45 08 (optionally inside PUSH EBX ... POP EBX, with LEA EBX)
 * followed by LEA\n * EDX/ECX, [EAX+d] and 5d c3 (the byte scan of CODE that found them is mission_load.md, "the mission load in
 * the own process"): EAX the argument, EDX/ECX the argument plus d where the body sets them, else as at the call. */
#define NONE 0x7fffffff
static const struct { uint32_t va; int32_t edx, ecx; } member_bodies[] = {
    {0x00402c36u, 46, NONE},
    {0x00406a6cu, 8, NONE},
    {0x00406a77u, 10, 24},
    {0x00407548u, 1, NONE},
    {0x0040cf04u, 8, NONE},
    {0x004117a5u, 16, NONE},
    {0x00423098u, 58, NONE},
    {0x00429655u, 17, NONE},
    {0x00437558u, 6, NONE},
    {0x00438785u, 4, NONE},
    {0x00438798u, 12, NONE},
    {0x00450617u, 12, NONE},
    {0x00469889u, 28, NONE},
    {0x004715e1u, 1, 7},
    {0x00406a85u, 10, 16},   /* with PUSH EBX / LEA EBX, [EAX+0x16] / POP EBX */
    {0x0041178du, 18, 26},   /* with PUSH EBX, ESI; ECX = EDX + 8 */
};
void es2_sim_service_member_address(es2_sim_service_call *sc, void *user) {
    (void)user;
    for (unsigned i = 0; i < sizeof(member_bodies) / sizeof(member_bodies[0]); ++i) {
        if (member_bodies[i].va != sc->va) continue;
        uint32_t a = sc->slots[0];
        sc->eax = a;
        if (member_bodies[i].edx != NONE) sc->edx = a + (uint32_t)member_bodies[i].edx;
        if (member_bodies[i].ecx != NONE) sc->ecx = a + (uint32_t)member_bodies[i].ecx;
        return;
    }
    sc->refused = 1; sc->reason = "no member body for this VA";
}
#undef NONE
/* ======================================================================
 * The shape loaders (asset_loading.md "Callers and reach"; listings
 * 0x474b30..0x474bbb, 0x474bbc..0x474be2, 0x474be4..0x474c0f,
 * 0x474cd8..0x474de2, 0x474de4..0x474eac) and their stream helpers
 * 0x47a308 (0x47a308..0x47a3b1), 0x47a3b4 (0x47a3b4..0x47a3f8), the arena
 * push 0x47a3fc (0x47a3fc..0x47a42f), pop 0x47a474 (0x47a474..0x47a49f)
 * and the class lookup 0x47a4a0 (0x47a4a0..0x47a4e3).
 * S's slots: +4 tell, +8 seek(pos), +0x18 read(n, buf); its status is the
 * dword S+4 (0x4930ab, 0x47a874). */
#define VA_ARENA_INDEX 0x004a16c0u
#define VA_ARENA_TABLE 0x006c5968u
#define VA_ARENA_SAVED 0x006b7a14u
static uint32_t class_lookup(const ctx *c, uint32_t tag) {
    for (uint32_t i = 0; i < r32(c, 0x004a1690u); ++i) {
        uint32_t table = r32(c, 0x006c5834u + 8u * i);
        for (uint32_t k = 0; k < r32(c, 0x006c5838u + 8u * i); ++k)
            if (r32(c, table + 12u * k) == tag) return table + 12u * k;
    }
    return 0;
}
/* 0x47a308(S, header): pos = S.tell; header <- (-1, 0); S.read(8, header);
 * an unknown tag with a nonempty fallback registry is refused (the
 * registry [0x4a1694] stays empty in DBSIM: asset_loading.md); unless the
 * status is 2: S.seek(pos); AL = (status = 0). */
static int header_peek(ctx *c, uint32_t s, uint32_t header, int *ok) {
    uint32_t pos = 0;
    if (vcall(c, s, 4u, (const uint32_t[]){s}, 1, &pos)) return 1;
    w32(c, header, 0xffffffffu);
    w32(c, header + 4u, 0);
    if (sread(c, s, 8u, header)) return 1;
    if (!class_lookup(c, r32(c, header)) && r32(c, 0x004a1694u))
        return refuse(c, 0x0047a34au, "header peek", "the fallback class registry (never filled in DBSIM)");
    if (r32(c, s + 4u) != 2u && vcall(c, s, 8u, (const uint32_t[]){s, pos}, 2, NULL)) return 1;
    *ok = r32(c, s + 4u) == 0;
    return 0;
}
/* 0x47a3b4(S): S.read(8, local); S.seek((S.tell + length + 1) & -2); AL =
 * (status = 0). */
static int object_skip(ctx *c, uint32_t s, int *ok) {
    uint32_t top = c->l->stack_hi, f = frame_push(c, 8u), pos = 0;
    if (!f) return refuse(c, 0x0047a3b4u, "object skip", "the own code's call area is exhausted");
    int bad = sread(c, s, 8u, f) || vcall(c, s, 4u, (const uint32_t[]){s}, 1, &pos);
    if (!bad) bad = vcall(c, s, 8u, (const uint32_t[]){s, (pos + r32(c, f + 4u) + 1u) & ~1u}, 2, NULL);
    frame_pop(c, top);
    if (!bad) *ok = r32(c, s + 4u) == 0;
    return bad;
}
/* 0x474bbc(n): [0x6b7a14] <- [0x6bb30c]; p = 0x47747c(n); the arena push
 * 0x47a3fc(p, n, 0): the index [0x4a16c0] + 1, the record 0x6c5968 + 12
 * index = {p, n, 0} becomes [0x6c5830]. */
static int arena_push(ctx *c, uint32_t n) {
    w32(c, VA_ARENA_SAVED, r32(c, 0x006bb30cu));
    uint32_t p = 0;
    if (CALL(c, 0x0047747cu, &p, n)) return 1;
    w32(c, VA_ARENA_INDEX, r32(c, VA_ARENA_INDEX) + 1u);
    uint32_t rec = VA_ARENA_TABLE + 12u * r32(c, VA_ARENA_INDEX);
    w32(c, VA_ARENA_CURRENT, rec);
    w32(c, rec, p);
    w32(c, rec + 4u, n);
    w32(c, rec + 8u, 0);
    return 0;
}
/* 0x474be4: rec = [0x6c5830]; _realloc(rec base, rec used) (its result is
 * not stored); [0x6bb30c] <- [0x6b7a14] + used; the pop 0x47a474: index -
 * 1 and [0x6c5830] <- 0x6c5968 + 12 index, also for index -1 (the "jae"
 * after TEST is always taken: the record 0x6c595c). */
static int arena_pop(ctx *c) {
    uint32_t rec = r32(c, VA_ARENA_CURRENT);
    if (CALL(c, 0x0049687au, NULL, r32(c, rec), r32(c, rec + 8u))) return 1;
    w32(c, 0x006bb30cu, r32(c, VA_ARENA_SAVED) + r32(c, rec + 8u));
    w32(c, VA_ARENA_INDEX, r32(c, VA_ARENA_INDEX) - 1u);
    w32(c, VA_ARENA_CURRENT, VA_ARENA_TABLE + 12u * r32(c, VA_ARENA_INDEX));
    return 0;
}
/* 0x474cd8(out, path, tag, skip): S on the frame; arena push 500,000; per
 * object while the header peek succeeds: a tag other than `tag` is the
 * fatal report (0x474d3e, then on); skip (the word, post-decremented, > 0) the object,
 * else the local table f-1e8h gets dts_load(S, 0); then [out] <-
 * 0x47a288(4 n, arena), the table copied (_memcpy); the arena pop; the
 * destructor. EAX n. The local table holds 0x6c entries: more objects are
 * refused (the original overruns its frame). */
static int shape_list(ctx *c, uint32_t out, uint32_t path, uint32_t tag, uint32_t skip, uint32_t *count) {
    uint32_t top = c->l->stack_hi, f = frame_push(c, 0x1e8u);
    if (!f) return refuse(c, 0x00474cd8u, "shape list", "the own code's call area is exhausted");
    f += 0x1e8u;
    uint32_t s = f - 0x30u, h = f - 0x58u, table = f - 0x1e8u, n = 0;
    uint16_t left = (uint16_t)skip;
    int bad = vol_stream(c, s, path, NULL) || arena_push(c, 0x7a120u);
    for (int ok = 1; !bad;) {
        if ((bad = header_peek(c, s, h, &ok)) || !ok) break;
        if (r32(c, h) != tag && (bad = fatal_report(c, 0x004a0f38u, 0x7eu, path))) break;   /* 0x474d3e */
        uint16_t was = left;
        left = (uint16_t)(left - 1u);
        if ((int16_t)was > 0) { int skipped; bad = object_skip(c, s, &skipped); continue; }
        if (table + 4u * n >= f - 0x58u) { bad = refuse(c, 0x00474d6eu, "shape list", "more objects than the local table holds"); break; }
        uint32_t x = 0;
        bad = CALL(c, VA_DTS_LOAD, &x, s, 0u);
        if (!bad) { w32(c, table + 4u * n, x); ++n; }
    }
    if (!bad) {
        uint32_t m = (uint32_t)(int32_t)(int16_t)n, b = 0;
        bad = arena(c, m << 2, &b);
        if (!bad) { w32(c, out, b); bad = CALL(c, 0x0049680eu, NULL, b, table, m << 2); }
    }
    if (!bad) bad = arena_pop(c) || CALL(c, VA_VOL_DESTROY, NULL, s, 2u);
    frame_pop(c, top);
    *count = n;
    return bad;
}
void es2_sim_service_shape_list(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    uint32_t head = fs0_head(), n = 0;
    if (shape_list(&c, sc->slots[0], sc->slots[1], sc->slots[2], sc->slots[3], &n)) { failed(sc); return; }
    sc->eax = n; sc->edx = head;
}
/* 0x474b30(path, tag out): S; the header peek; for the tags 0x1e0003
 * (ANShape), 0x140008 (TSShape) and 0x28000f (GLPalette) X = dts_load(S,
 * 0) and, when X is nonzero, *out <- the tag; the destructor; EAX X. Any
 * other tag returns the caller's EBX (residue; refused). */
static int shape_root(ctx *c, uint32_t path, uint32_t out, uint32_t *result) {
    uint32_t top = c->l->stack_hi, f = frame_push(c, 0x58u);
    if (!f) return refuse(c, 0x00474b30u, "shape root", "the own code's call area is exhausted");
    f += 0x58u;
    uint32_t s = f - 0x30u, h = f - 0x58u, x = 0;
    int ok = 0, bad = vol_stream(c, s, path, NULL) || header_peek(c, s, h, &ok);
    if (!bad) {
        uint32_t tag = r32(c, h);
        if (tag == 0x001e0003u || tag == 0x00140008u || tag == 0x0028000fu) {
            bad = CALL(c, VA_DTS_LOAD, &x, s, 0u);
            if (!bad && x) w32(c, out, r32(c, h));
        } else bad = refuse(c, 0x00474b9bu, "shape root", "an unknown root class returns the caller's EBX (residue)");
    }
    if (!bad) bad = CALL(c, VA_VOL_DESTROY, NULL, s, 2u);
    frame_pop(c, top);
    *result = x;
    return bad;
}
void es2_sim_service_shape_root(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    uint32_t head = fs0_head(), x = 0;
    if (shape_root(&c, sc->slots[0], sc->slots[1], &x)) { failed(sc); return; }
    sc->eax = x; sc->edx = head;
}
/* 0x474de4(path, index, tag): S; while the header peek succeeds and the
 * word index > the count so far (signed words): a tag other than `tag` is
 * the fatal report (0x474e3c, then on), the object skipped; then the arena push
 * 200,000, X = dts_load(S, 0), the arena pop, the destructor; EAX X. */
static int shape_at(ctx *c, uint32_t path, uint32_t index, uint32_t tag, uint32_t *result) {
    uint32_t top = c->l->stack_hi, f = frame_push(c, 0x58u);
    if (!f) return refuse(c, 0x00474de4u, "shape at", "the own code's call area is exhausted");
    f += 0x58u;
    uint32_t s = f - 0x30u, h = f - 0x58u, x = 0, k = 0;
    int bad = vol_stream(c, s, path, NULL);
    for (int ok = 1; !bad;) {
        if ((bad = header_peek(c, s, h, &ok)) || !ok) break;
        uint32_t was = k++;
        if (!((int16_t)index > (int16_t)was)) break;
        if (r32(c, h) != tag && (bad = fatal_report(c, 0x004a0f42u, 0x9eu, path))) break;   /* 0x474e3c */
        int skipped;
        bad = object_skip(c, s, &skipped);
    }
    if (!bad) bad = arena_push(c, 0x30d40u) || CALL(c, VA_DTS_LOAD, &x, s, 0u) || arena_pop(c) ||
                    CALL(c, VA_VOL_DESTROY, NULL, s, 2u);
    frame_pop(c, top);
    *result = x;
    return bad;
}
void es2_sim_service_shape_at(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    uint32_t head = fs0_head(), x = 0;
    if (shape_at(&c, sc->slots[0], sc->slots[1], sc->slots[2], &x)) { failed(sc); return; }
    sc->eax = x; sc->edx = head;
}
/* ======================================================================
 * The TS object model's reads that the grid and the shapes share
 * (asset_loading.md; listings 0x48f9a0..0x48f9e4, 0x48fea0..0x48ff0a,
 * 0x490e68..0x490f3c) and the GridShape class (factory 0x427568
 * (0x427568..0x4275ec), constructors 0x490a34 (0x490a34..0x490a62) and
 * 0x476710 (0x476710..0x476752), read 0x42762c (0x42762c..0x427712)).
 * 0x48f9a0(P, S): words +4, +6, +8, 6 bytes at +0xa.
 * 0x48fea0(P, S, arg): 0x48f9a0; word +0x10 = n; +0x12 <- 0x47a288(4n,
 * arena); n objects dts_load(S, 0) into it.
 * 0x490e68(P, S, arg): 0x48fea0; words +0x16 = m, +0x24 = k; +0x20 and
 * +0x1c <- 0x47a288(2k, arena) each; +0x18 <- 0x49667c(0x47a288(32m,
 * arena), 0x20, m, 1, 0x495132); S.read(2k, [+0x20]) (0x491171);
 * S.read(32m, [+0x18]); _memset([+0x1c], 0, 2k). */
static int part_read(ctx *c, uint32_t p, uint32_t s) {
    return sread(c, s, 2, p + 4u) || sread(c, s, 2, p + 6u) || sread(c, s, 2, p + 8u) || sread(c, s, 6, p + 0xau);
}
static int part_list_read(ctx *c, uint32_t p, uint32_t s) {
    if (part_read(c, p, s) || sread(c, s, 2, p + 0x10u)) return 1;
    uint32_t q = 0;
    if (arena(c, (uint32_t)r16(c, p + 0x10u) << 2, &q)) return 1;
    w32(c, p + 0x12u, q);
    for (uint32_t i = 0; (int32_t)i < (int32_t)r16(c, p + 0x10u); ++i, q += 4u) {
        uint32_t x = 0;
        if (CALL(c, VA_DTS_LOAD, &x, s, 0u)) return 1;
        w32(c, q, x);
    }
    return 0;
}
static int shape_read(ctx *c, uint32_t p, uint32_t s) {
    if (part_list_read(c, p, s) || sread(c, s, 2, p + 0x16u) || sread(c, s, 2, p + 0x24u)) return 1;
    uint32_t b = 0;
    if (arena(c, (uint32_t)r16(c, p + 0x24u) * 2u, &b)) return 1;
    w32(c, p + 0x20u, b);
    if (arena(c, (uint32_t)r16(c, p + 0x24u) * 2u, &b)) return 1;
    w32(c, p + 0x1cu, b);
    if (arena(c, (uint32_t)r16(c, p + 0x16u) << 5, &b)) return 1;
    if (CALL(c, 0x0049667cu, &b, b, 0x20u, (uint32_t)r16(c, p + 0x16u), 1u, 0x00495132u)) return 1;
    w32(c, p + 0x18u, b);
    return sread(c, s, (uint32_t)r16(c, p + 0x24u) * 2u, r32(c, p + 0x20u)) ||
           sread(c, s, (uint32_t)r16(c, p + 0x16u) << 5, r32(c, p + 0x18u)) ||
           CALL(c, VA_MEMSET, NULL, r32(c, p + 0x1cu), 0u, (uint32_t)r16(c, p + 0x24u) * 2u);
}
void es2_sim_service_part_read(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    if (part_read(&c, sc->slots[0], sc->slots[1])) failed(sc);
}
void es2_sim_service_part_list_read(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    if (part_list_read(&c, sc->slots[0], sc->slots[1])) failed(sc);
}
void es2_sim_service_shape_read(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    if (shape_read(&c, sc->slots[0], sc->slots[1])) failed(sc);
}
/* 0x476710(P): [P] <- 0x4a69f0, 0x4a6a14, counter + 1, 0x4a6250, counter +
 * 1, 0x4a10bc, word +0x10 <- 0, dword +0x12 <- 0, counter + 1. */
static void part_list_ctor(ctx *c, uint32_t p) {
    w32(c, p, 0x004a69f0u);
    w32(c, p, 0x004a6a14u);
    counter(c, 1);
    w32(c, p, 0x004a6250u);
    counter(c, 1);
    w32(c, p, 0x004a10bcu);
    w16(c, p + 0x10u, 0);
    w32(c, p + 0x12u, 0);
    counter(c, 1);
}
/* 0x490a34(P): 0x476710; [P] <- 0x4a6319; +0x20, +0x1c, +0x18 <- 0;
 * counter + 1. */
static void shape_ctor(ctx *c, uint32_t p) {
    part_list_ctor(c, p);
    w32(c, p, 0x004a6319u);
    w32(c, p + 0x20u, 0);
    w32(c, p + 0x1cu, 0);
    w32(c, p + 0x18u, 0);
    counter(c, 1);
}
void es2_sim_service_part_list_ctor(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    part_list_ctor(&c, sc->slots[0]);
    sc->eax = sc->slots[0]; sc->edx = 0; sc->ecx = r32(&c, VA_COUNTER_POINTER);
}
void es2_sim_service_shape_ctor(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    shape_ctor(&c, sc->slots[0]);
    sc->eax = sc->slots[0]; sc->edx = 0; sc->ecx = 0;
}
/* The GridShape factory 0x427568(S, arg): G = 0x47a2c8(0x438, arena);
 * 0x490a34(G); [G] <- 0x49aaa0; counter + 1, - 5; G's slot +0xc (G, S,
 * arg) = 0x42762c. EAX G, EDX the head at entry. A NULL G faults (refused).
 * 0x42762c(G, S, arg): 0x490e68; words +0x2a (w), +0x2c (h), +0x2e,
 * +0x30, +0x32; 0x400 bytes at +0x34; h nonzero: +0x434 <- 0x47a288(4h,
 * arena) and per row (signed words) a row of w bytes from 0x47a288(w,
 * arena), read from S. */
void es2_sim_service_grid_new(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t head = fs0_head(), g = 0;
    if (CALL(&c, 0x0047a2c8u, &g, 0x438u, r32(&c, VA_ARENA_CURRENT))) { failed(sc); return; }
    if (!g) { null_refuse(&c, 0x004275d4u, "grid shape", "the allocation returned NULL (the original faults)"); failed(sc); return; }
    shape_ctor(&c, g);
    w32(&c, g, 0x0049aaa0u);
    counter(&c, 1);
    counter(&c, -5);
    if (vcall(&c, g, 0xcu, (const uint32_t[]){g, sc->slots[0], sc->slots[1]}, 3, NULL)) { failed(sc); return; }
    sc->eax = g; sc->edx = head;
}
void es2_sim_service_grid_read(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t g = sc->slots[0], s = sc->slots[1];
    if (shape_read(&c, g, s)) { failed(sc); return; }
    static const uint32_t words[] = {0x2au, 0x2cu, 0x2eu, 0x30u, 0x32u};
    for (unsigned i = 0; i < 5u; ++i) if (sread(&c, s, 2, g + words[i])) { failed(sc); return; }
    if (sread(&c, s, 0x400u, g + 0x34u)) { failed(sc); return; }
    uint16_t h = r16(&c, g + 0x2cu);
    if (!h) return;
    uint32_t rows = 0;
    if (arena(&c, sx16(h) << 2, &rows)) { failed(sc); return; }
    w32(&c, g + 0x434u, rows);
    for (int16_t k = 0; k < (int16_t)r16(&c, g + 0x2cu); ++k, rows += 4u) {
        uint32_t b = 0;
        if (arena(&c, sx16(r16(&c, g + 0x2au)), &b)) { failed(sc); return; }
        w32(&c, rows, b);
        if (sread(&c, s, sx16(r16(&c, g + 0x2au)), r32(&c, rows))) { failed(sc); return; }
    }
}
/* ======================================================================
 * The TS and AN persistent classes (the registry tables 0x4a63b0 and
 * 0x4a687c; asset_loading.md "Objects and globals"): factories, the
 * constructors they call and the reads of their vtable slot +0xc. Every
 * factory (listing at its VA, 0x48dea0..0x4921e5) allocates its size from
 * 0x47a2c8(size, [0x6c5830]), runs the construction (the vtable chain with
 * the counter + 1 per level, then the factory's counter - n: net 0), calls
 * the read slot +0xc (P, S, arg) and returns EAX P, EDX the head at entry.
 * A NULL allocation faults in the original (refused). */
static void chain(ctx *c, uint32_t p, const uint32_t *vt, unsigned n) {
    w32(c, p, 0x004a69f0u);
    for (unsigned i = 0; i < n; ++i) { w32(c, p, vt[i]); counter(c, 1); }
}
/* 0x4758b4(P) (0x4758b4..0x4758ff): the chain 0x4a6a14, 0x4a6250,
 * 0x4a0fbf; +0x10, +0x14, +0x18, +0x1c <- 0. 0x475ac0(P)
 * (0x475ac0..0x475aeb): 0x4758b4, [P] <- 0x4a0f8b, word +0x28 <- 0, dword
 * +0x2a <- 0, counter + 1. 0x476940(P) (0x476940..0x476965): 0x476710,
 * [P] <- 0x4a1088, +0x16 <- 0, counter + 1. 0x476aac(P)
 * (0x476aac..0x476adc): 0x476710, [P] <- 0x4a1054, word +0x16 <- 0, +0x18,
 * +0x1c <- 0, counter + 1. */
static void group_ctor(ctx *c, uint32_t p) {
    w32(c, p, 0x004a69f0u);
    w32(c, p, 0x004a6a14u); counter(c, 1);
    w32(c, p, 0x004a6250u); counter(c, 1);
    w32(c, p, 0x004a0fbfu);
    w32(c, p + 0x10u, 0); w32(c, p + 0x14u, 0); w32(c, p + 0x18u, 0); w32(c, p + 0x1cu, 0);
    counter(c, 1);
}
static void class_construct(ctx *c, uint32_t va, uint32_t p) {
    static const uint32_t poly[] = {0x004a6a14u, 0x004a5e88u, 0x004a5eb4u, 0x004a5ee0u};
    switch (va) {
    case 0x0048dea0u: chain(c, p, (const uint32_t[]){0x004a6a14u, 0x004a6014u}, 2); counter(c, -2); break;
    case 0x0048df64u: chain(c, p, poly, 3); counter(c, -3); break;
    case 0x0048e178u: chain(c, p, poly, 4); counter(c, -4); break;
    case 0x0048e31cu: chain(c, p, poly, 4); w32(c, p, 0x004a5f0cu); counter(c, 1); counter(c, -5); break;
    case 0x0048e3e0u: chain(c, p, poly, 4); w32(c, p, 0x004a5fe8u); counter(c, 1); counter(c, -5); break;
    case 0x0048e4a4u: chain(c, p, poly, 4); w32(c, p, 0x004a5fbcu); counter(c, 1); counter(c, -5); break;
    case 0x0048e64cu: chain(c, p, poly, 4); w32(c, p, 0x004a5f90u); counter(c, 1); counter(c, -5); break;
    case 0x0048e710u:
        chain(c, p, poly, 4); w32(c, p, 0x004a5fe8u); counter(c, 1); w32(c, p, 0x004a5f64u); counter(c, 1); counter(c, -6); break;
    case 0x0048e7e0u:
        chain(c, p, poly, 4); w32(c, p, 0x004a5fbcu); counter(c, 1); w32(c, p, 0x004a5f38u); counter(c, 1); counter(c, -6); break;
    case 0x0048e8b0u: group_ctor(c, p); counter(c, -3); break;
    case 0x0048ee10u:
        group_ctor(c, p); w32(c, p, 0x004a0f8bu); w16(c, p + 0x28u, 0); w32(c, p + 0x2au, 0); counter(c, 1); counter(c, -4); break;
    case 0x0048f8dcu:   /* 0x4922d3(P+0xa) stores nothing */
        chain(c, p, (const uint32_t[]){0x004a6a14u, 0x004a6250u}, 2); counter(c, -2); break;
    case 0x0048fb74u: chain(c, p, (const uint32_t[]){0x004a6a14u, 0x004a6250u, 0x004a62b8u}, 3); counter(c, -3); break;
    case 0x0048fd98u: part_list_ctor(c, p); counter(c, -3); break;
    case 0x0048fff4u: part_list_ctor(c, p); w32(c, p, 0x004a6284u); counter(c, 1); counter(c, -4); break;
    case 0x004901a0u: part_list_ctor(c, p); w32(c, p, 0x004a1088u); w32(c, p + 0x16u, 0); counter(c, 1); counter(c, -4); break;
    case 0x00490388u:
        part_list_ctor(c, p); w32(c, p, 0x004a1054u); w16(c, p + 0x16u, 0); w32(c, p + 0x18u, 0); w32(c, p + 0x1cu, 0);
        counter(c, 1); counter(c, -4); break;
    case 0x00490d68u: shape_ctor(c, p); counter(c, -4); break;
    case 0x004913f0u: chain(c, p, (const uint32_t[]){0x004a6a14u, 0x004a684cu}, 2); counter(c, -2); break;
    case 0x004919a0u: chain(c, p, (const uint32_t[]){0x004a6a14u, 0x004a684cu, 0x004a6810u}, 3); counter(c, -3); break;
    case 0x00491a50u: chain(c, p, (const uint32_t[]){0x004a6a14u, 0x004a67e4u}, 2); counter(c, -2); break;
    case 0x00492164u: shape_ctor(c, p); w32(c, p, 0x004a67b0u); counter(c, 1); counter(c, -5); break;
    default: break;
    }
}
static const struct { uint32_t va, size; } class_sizes[] = {
    {0x0048dea0u, 0x04u}, {0x0048df64u, 0x0cu}, {0x0048e178u, 0x0eu}, {0x0048e31cu, 0x0eu}, {0x0048e3e0u, 0x0eu},
    {0x0048e4a4u, 0x10u}, {0x0048e64cu, 0x0eu}, {0x0048e710u, 0x0eu}, {0x0048e7e0u, 0x10u}, {0x0048e8b0u, 0x28u},
    {0x0048ee10u, 0x2eu}, {0x0048f8dcu, 0x10u}, {0x0048fb74u, 0x14u}, {0x0048fd98u, 0x16u}, {0x0048fff4u, 0x18u},
    {0x004901a0u, 0x1au}, {0x00490388u, 0x20u}, {0x00490d68u, 0x2au}, {0x004913f0u, 0x1au}, {0x004919a0u, 0x1au},
    {0x00491a50u, 0x22u}, {0x00492164u, 0x2eu},
};
void es2_sim_service_class_new(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t size = 0, head = fs0_head(), p = 0;
    for (unsigned i = 0; i < sizeof(class_sizes) / sizeof(class_sizes[0]); ++i) if (class_sizes[i].va == sc->va) size = class_sizes[i].size;
    if (!size) { sc->refused = 1; sc->reason = "no class for this factory VA"; return; }
    if (CALL(&c, 0x0047a2c8u, &p, size, r32(&c, VA_ARENA_CURRENT))) { failed(sc); return; }
    if (!p) { null_refuse(&c, sc->va, "persistent class", "the allocation returned NULL (the original faults)"); failed(sc); return; }
    class_construct(&c, sc->va, p);
    if (vcall(&c, p, 0xcu, (const uint32_t[]){p, sc->slots[0], sc->slots[1]}, 3, NULL)) { failed(sc); return; }
    sc->eax = p; sc->edx = head;
}

/* The reads (slot +0xc; listings at each VA): w(off) = S.read(2, P+off)
 * (0x49118c / 0x492430), b(off) = S.read(1, P+off) (0x490874). */
static int rw(ctx *c, uint32_t s, uint32_t p, uint32_t off) { return sread(c, s, 2, p + off); }
static int arena_store(ctx *c, uint32_t p, uint32_t off, uint32_t n) {
    uint32_t b = 0;
    if (arena(c, n, &b)) return 1;
    w32(c, p + off, b);
    return 0;
}
static int load_objects(ctx *c, uint32_t s, uint32_t dst, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i, dst += 4u) {
        uint32_t x = 0;
        if (CALL(c, VA_DTS_LOAD, &x, s, 0u)) return 1;
        w32(c, dst, x);
    }
    return 0;
}
/* 0x48e02c TSPoly: w(4), w(6), w(8), w(0xa). 0x48e258: 0x48e02c, w(0xc).
 * 0x48e588: 0x48e258, w(0xe). */
static int poly_read(ctx *c, uint32_t p, uint32_t s, unsigned words) {
    for (unsigned i = 0; i < words; ++i) if (rw(c, s, p, 4u + 2u * i)) return 1;
    return 0;
}
/* 0x48fce0 TSBitmapPart: part, w(0x10), b(0x12), b(0x13). */
static int bitmap_part_read(ctx *c, uint32_t p, uint32_t s) {
    return part_read(c, p, s) || rw(c, s, p, 0x10u) || sread(c, s, 1, p + 0x12u) || sread(c, s, 1, p + 0x13u);
}
/* 0x4900dc TSCellAnimPart: part list, w(0x16). */
static int cell_anim_read(ctx *c, uint32_t p, uint32_t s) { return part_list_read(c, p, s) || rw(c, s, p, 0x16u); }
/* 0x492244 ANShape: TSShape, [P+0x2a] <- dts_load(S, 0). */
static int an_shape_read(ctx *c, uint32_t p, uint32_t s) {
    uint32_t x = 0;
    if (shape_read(c, p, s) || CALL(c, VA_DTS_LOAD, &x, s, 0u)) return 1;
    w32(c, p + 0x2au, x);
    return 0;
}
/* 0x4914f0 ANSequence (and ANCyclicSequence): w(4), w(6), w(8), w(0xa) =
 * a; +0xc <- arena(6a), S.read(6a); w(0x10) = b; +0x12 <- arena(2b),
 * S.read(2b) (0x49245e); +0x16 <- arena(2ab), S.read(2ab) (sign-extended
 * words, low dword of the products). */
static int sequence_read(ctx *c, uint32_t p, uint32_t s) {
    if (poly_read(c, p, s, 4)) return 1;
    uint32_t a = sx16(r16(c, p + 0xau));
    if (arena_store(c, p, 0xcu, a * 6u) || sread(c, s, sx16(r16(c, p + 0xau)) * 6u, r32(c, p + 0xcu)) || rw(c, s, p, 0x10u)) return 1;
    uint32_t b = sx16(r16(c, p + 0x10u));
    if (arena_store(c, p, 0x12u, b * 2u) || sread(c, s, sx16(r16(c, p + 0x10u)) * 2u, r32(c, p + 0x12u))) return 1;
    a = sx16(r16(c, p + 0xau)); b = sx16(r16(c, p + 0x10u));
    if (arena_store(c, p, 0x16u, a * b * 2u)) return 1;
    a = sx16(r16(c, p + 0xau)); b = sx16(r16(c, p + 0x10u));
    return sread(c, s, a * b * 2u, r32(c, p + 0x16u));
}
/* 0x491bb0 ANAnimList: w(4) = n; +6 <- arena(4n) with n objects (signed
 * count); w(0x1c) = k: +0x1e <- arena(8k), S.read(8k); w(0x10) = m: +0x12
 * <- 0x49667c(arena(12m), 0xc, m, 1, 0x4922e6), S.read(12m); w(0x16) = q:
 * +0x18 <- arena(2q), S.read(2q); w(0xa) = r: +0xc <- arena(4r), S.read(4r). */
static int anim_list_read(ctx *c, uint32_t p, uint32_t s) {
    if (rw(c, s, p, 4u) || arena_store(c, p, 6u, sx16(r16(c, p + 4u)) << 2)) return 1;
    uint32_t dst = r32(c, p + 6u);
    for (int32_t i = 0; (int32_t)sx16(r16(c, p + 4u)) > i; ++i, dst += 4u) {
        uint32_t x = 0;
        if (CALL(c, VA_DTS_LOAD, &x, s, 0u)) return 1;
        w32(c, dst, x);
    }
    if (rw(c, s, p, 0x1cu) || arena_store(c, p, 0x1eu, sx16(r16(c, p + 0x1cu)) << 3) ||
        sread(c, s, sx16(r16(c, p + 0x1cu)) << 3, r32(c, p + 0x1eu)) || rw(c, s, p, 0x10u)) return 1;
    uint32_t b = 0, m = sx16(r16(c, p + 0x10u));
    if (arena(c, m * 12u, &b) || CALL(c, 0x0049667cu, &b, b, 0xcu, sx16(r16(c, p + 0x10u)), 1u, 0x004922e6u)) return 1;
    w32(c, p + 0x12u, b);
    if (sread(c, s, sx16(r16(c, p + 0x10u)) * 12u, r32(c, p + 0x12u)) || rw(c, s, p, 0x16u) ||
        arena_store(c, p, 0x18u, sx16(r16(c, p + 0x16u)) * 2u) || sread(c, s, sx16(r16(c, p + 0x16u)) * 2u, r32(c, p + 0x18u)) ||
        rw(c, s, p, 0xau) || arena_store(c, p, 0xcu, sx16(r16(c, p + 0xau)) << 2))
        return 1;
    return sread(c, s, sx16(r16(c, p + 0xau)) << 2, r32(c, p + 0xcu));
}
/* 0x48e9f0 TSGroup: part, w(0x20) = a, w(0x22) = b, w(0x24) = d, w(0x26)
 * = e (zero-extended); +0x10 <- arena(2a); +0x14 <- 0x49667c(arena(6b), 6,
 * b, 1, 0x4922d3); +0x18 <- arena(4d); +0x1c <- arena(4e); S.read(2a),
 * S.read(6b), S.read(4d) (0x48f3ea); e objects into +0x1c. */
static int group_read(ctx *c, uint32_t p, uint32_t s) {
    if (part_read(c, p, s) || rw(c, s, p, 0x20u) || rw(c, s, p, 0x22u) || rw(c, s, p, 0x24u) || rw(c, s, p, 0x26u) ||
        arena_store(c, p, 0x10u, (uint32_t)r16(c, p + 0x20u) * 2u)) return 1;
    uint32_t b = 0;
    if (arena(c, (uint32_t)r16(c, p + 0x22u) * 6u, &b) || CALL(c, 0x0049667cu, &b, b, 6u, (uint32_t)r16(c, p + 0x22u), 1u, 0x004922d3u))
        return 1;
    w32(c, p + 0x14u, b);
    if (arena_store(c, p, 0x18u, (uint32_t)r16(c, p + 0x24u) << 2) || arena_store(c, p, 0x1cu, (uint32_t)r16(c, p + 0x26u) << 2) ||
        sread(c, s, (uint32_t)r16(c, p + 0x20u) * 2u, r32(c, p + 0x10u)) ||
        sread(c, s, (uint32_t)r16(c, p + 0x22u) * 6u, r32(c, p + 0x14u)) ||
        sread(c, s, (uint32_t)r16(c, p + 0x24u) << 2, r32(c, p + 0x18u)))
        return 1;
    return load_objects(c, s, r32(c, p + 0x1cu), r16(c, p + 0x26u));
}
/* 0x48ef20 TSBSPGroup: group, w(0x28) = n; +0x2a <- arena(10n), S.read(10n). */
static int bsp_group_read(ctx *c, uint32_t p, uint32_t s) {
    return group_read(c, p, s) || rw(c, s, p, 0x28u) || arena_store(c, p, 0x2au, (uint32_t)r16(c, p + 0x28u) * 10u) ||
           sread(c, s, (uint32_t)r16(c, p + 0x28u) * 10u, r32(c, p + 0x2au));
}
/* 0x490288 TSDetailPart: part list; +0x16 <- arena(2n) (n the list's word
 * +0x10), S.read(2n). */
static int detail_read(ctx *c, uint32_t p, uint32_t s) {
    return part_list_read(c, p, s) || arena_store(c, p, 0x16u, (uint32_t)r16(c, p + 0x10u) * 2u) ||
           sread(c, s, (uint32_t)r16(c, p + 0x10u) * 2u, r32(c, p + 0x16u));
}
/* 0x490488 TSBSPPart: part list, w(0x16) = n; +0x18 <- 0x49667c(arena(14n),
 * 0xe, n, 1, 0x49090f); +0x1c <- arena(2n); S.read(14n, +0x18), S.read(2n, +0x1c). */
static int bsp_part_read(ctx *c, uint32_t p, uint32_t s) {
    if (part_list_read(c, p, s) || rw(c, s, p, 0x16u)) return 1;
    uint32_t b = 0, n = r16(c, p + 0x16u);
    if (arena(c, n * 14u, &b) || CALL(c, 0x0049667cu, &b, b, 0xeu, (uint32_t)r16(c, p + 0x16u), 1u, 0x0049090fu)) return 1;
    w32(c, p + 0x18u, b);
    return arena_store(c, p, 0x1cu, (uint32_t)r16(c, p + 0x16u) * 2u) ||
           sread(c, s, (uint32_t)r16(c, p + 0x16u) * 14u, r32(c, p + 0x18u)) ||
           sread(c, s, (uint32_t)r16(c, p + 0x16u) * 2u, r32(c, p + 0x1cu));
}
void es2_sim_service_class_read(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t p = sc->slots[0], s = sc->slots[1];
    int bad;
    switch (sc->va) {
    case 0x0048e02cu: bad = poly_read(&c, p, s, 4); break;
    case 0x0048e258u: bad = poly_read(&c, p, s, 5); break;
    case 0x0048e588u: bad = poly_read(&c, p, s, 6); break;
    case 0x0048fce0u: bad = bitmap_part_read(&c, p, s); break;
    case 0x004900dcu: bad = cell_anim_read(&c, p, s); break;
    case 0x00492244u: bad = an_shape_read(&c, p, s); break;
    case 0x004914f0u: bad = sequence_read(&c, p, s); break;
    case 0x00491bb0u: bad = anim_list_read(&c, p, s); break;
    case 0x0048e9f0u: bad = group_read(&c, p, s); break;
    case 0x0048ef20u: bad = bsp_group_read(&c, p, s); break;
    case 0x00490288u: bad = detail_read(&c, p, s); break;
    case 0x00490488u: bad = bsp_part_read(&c, p, s); break;
    default: sc->refused = 1; sc->reason = "no read for this VA"; return;
    }
    if (bad) failed(sc);
}
/* ======================================================================
 * The texture pages: the table builder 0x46a03c(A) (listing
 * 0x46a03c..0x46a109), the slot search 0x469e14(T, B, out) (0x469e14..
 * 0x46a03a) and the next-page constructor 0x469ddc(T, page)
 * (0x469ddc..0x469e13). T (0x808 bytes): 256 row pairs {x, free}, +0x800
 * the next page's table, +0x804 the page index; the page's pixels are the
 * 64 KiB at 0x4d4f04 + 0x10000 page (BSS). B is a GLBitmap (height word
 * +4, width word +6, pixels +0xe). The record out (0x14 bytes): x, row,
 * x + w - 1, row + h - 1, word page, word "has a zero pixel". */
#define VA_TEXTURE_PAGES 0x004d4f04u
static int texture_place(ctx *c, uint32_t t, uint32_t b, uint32_t out) {
    for (;;) {
        int32_t w = (int32_t)sx16(r16(c, b + 6u)), h = (int32_t)sx16(r16(c, b + 4u));
        for (int32_t row = 0; 0x100 - h + 1 > row; ++row) {
            uint32_t pr = t + 8u * (uint32_t)row;
            if (w > (int32_t)r32(c, pr + 4u)) continue;
            int32_t k = 0;
            while (k < h && r32(c, pr) == r32(c, pr + 8u * (uint32_t)k) && !(w > (int32_t)r32(c, pr + 8u * (uint32_t)k + 4u))) ++k;
            uint32_t end = r32(c, pr) + r32(c, pr + 4u);
            int right = 0;
            if (k < h) {
                right = 1;
                k = 0;
                while (k < h && r32(c, pr + 8u * (uint32_t)k) + r32(c, pr + 8u * (uint32_t)k + 4u) == end &&
                       !(w > (int32_t)r32(c, pr + 8u * (uint32_t)k + 4u)))
                    ++k;
            }
            if (k != h) continue;
            uint32_t x = r32(c, pr);
            if (right) x += r32(c, pr + 4u) - (uint32_t)w;
            uint32_t src = r32(c, b + 0xeu);
            uint32_t dst = (r32(c, t + 0x804u) << 16) + ((uint32_t)row << 8) + x + VA_TEXTURE_PAGES;
            uint16_t zero = 0;
            for (int32_t i = 0; i < h; ++i) {
                uint32_t q = pr + 8u * (uint32_t)i;
                if (!right) w32(c, q, r32(c, q) + (uint32_t)w);
                w32(c, q + 4u, r32(c, q + 4u) - (uint32_t)w);
                for (int32_t j = 0; w > j; ++j) {
                    uint8_t v = r8(c, src + (uint32_t)j);
                    w8(c, dst + (uint32_t)j, v);
                    if (!v) zero = 1;
                }
                dst += 0x100u;
                src += (uint32_t)w;
            }
            w32(c, out, x);
            w32(c, out + 4u, (uint32_t)row);
            w32(c, out + 8u, (uint32_t)(int32_t)sx16(r16(c, b + 6u)) + x - 1u);
            w32(c, out + 0xcu, (uint32_t)(int32_t)sx16(r16(c, b + 4u)) + (uint32_t)row - 1u);
            w16(c, out + 0x10u, r16(c, t + 0x804u));
            w16(c, out + 0x12u, zero);
            return 0;
        }
        if (!r32(c, t + 0x800u)) {
            uint32_t n = 0;
            if (CALL(c, VA_NEW, &n, 0x808u)) return 1;
            if (n) {
                uint32_t page = r32(c, t + 0x804u) + 1u;
                w32(c, n + 0x800u, 0);
                w32(c, n + 0x804u, page);
                for (uint32_t i = 0; i < 0x100u; ++i) { w32(c, n + 8u * i, 0); w32(c, n + 8u * i + 4u, 0x100u); }
            }
            w32(c, t + 0x800u, n);
        }
        t = r32(c, t + 0x800u);
        if (!t) return null_refuse(c, 0x0046a023u, "texture pages", "operator new returned NULL (the original reads through NULL)");
    }
}
static int texture_table(ctx *c, uint32_t a, uint32_t *result) {
    uint32_t n = r32(c, a + 4u), recs = 0, flags = 0;
    if (CALL(c, 0x0049667cu, &recs, 0u, 0x14u, n, 1u, 0x0046a42bu)) return 1;
    if (CALL(c, VA_NEW_ARRAY, &flags, r32(c, a + 4u))) return 1;
    if (CALL(c, VA_MEMSET, NULL, flags, 0u, r32(c, a + 4u))) return 1;
    uint32_t rec = recs;
    for (int32_t i = 0; (int32_t)r32(c, a + 4u) > i; ++i, rec += 0x14u) {
        /* The size scan over the unplaced bitmaps (EBX, 0x46a08e..0x46a0cb)
         * has no consumer: not modelled (it reads and writes nothing else). */
        w8(c, flags + (uint32_t)i, 1);
        if (texture_place(c, r32(c, 0x004a08b4u), r32(c, r32(c, a + 8u) + 4u * (uint32_t)i), rec)) return 1;
    }
    *result = recs;
    return 0;
}
void es2_sim_service_texture_table(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t x = 0;
    if (texture_table(&c, sc->slots[0], &x)) { failed(sc); return; }
    sc->eax = x;
}
void es2_sim_service_texture_place(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t head = fs0_head();
    if (texture_place(&c, sc->slots[0], sc->slots[1], sc->slots[2])) { failed(sc); return; }
    sc->eax = head;
}
/* ======================================================================
 * The graphics library's light object and the scene lights.
 * 0x48bc40(L) (listing 0x48bc40..0x48bc6d): [L] <- 0x4a5b39 (the element
 * constructors 0x4953c4 on L+4 and 0x4922d3 on L+0x10 store nothing),
 * counter + 1; EAX L. The light table 0x406e44(T) (0x406e44..0x406ee2):
 * 0x49667c(T+0x6c, 0x23, 20, 1, 0x407548); the light at T+0x328 (0x48bc40,
 * [.] <- 0x49f8d4, 0x48bc40 on +0x1e, counter + 1, [.] <- 0x497adc, +0x16
 * <- 0, counter + 1); [T+0x18], [T+0x3b0], [T+0x35c] <- 0; 0x406ee4(T,
 * 2000, 3000): [T+0x10] <- 2000 >> 5, [T+0x14] <- 3000 >> 5; per entry i
 * < 20 byte +0 <- 1 and dword +0x1f <- 0; counter + 1; EAX T. */
static void gl_light(ctx *c, uint32_t l) { w32(c, l, 0x004a5b39u); counter(c, 1); }
void es2_sim_service_gl_light(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    gl_light(&c, sc->slots[0]);
    sc->eax = sc->slots[0]; sc->ecx = r32(&c, VA_COUNTER_POINTER);
}
void es2_sim_service_light_table(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t t = sc->slots[0];
    if (CALL(&c, 0x0049667cu, NULL, t + 0x6cu, 0x23u, 0x14u, 1u, 0x00407548u)) { failed(sc); return; }
    uint32_t l = t + 0x328u;
    gl_light(&c, l);
    w32(&c, l, 0x0049f8d4u);
    gl_light(&c, l + 0x1eu);
    counter(&c, 1);
    w32(&c, l, 0x00497adcu);
    w32(&c, l + 0x16u, 0);
    counter(&c, 1);
    w32(&c, t + 0x18u, 0);
    w32(&c, t + 0x3b0u, 0);
    w32(&c, t + 0x35cu, 0);
    w32(&c, t + 0x10u, (uint32_t)(2000 >> 5));
    w32(&c, t + 0x14u, (uint32_t)(3000 >> 5));
    for (uint32_t i = 0; i < 0x14u; ++i) { w8(&c, t + 0x6cu + 0x23u * i, 1); w32(&c, t + 0x6cu + 0x23u * i + 0x1fu, 0); }
    counter(&c, 1);
    sc->eax = t; sc->edx = 0x14u; sc->ecx = 0;   /* the loop's EDX, ECX zeroed in it */
}
/* 0x4800c0(v, M, out) (listing 0x4800c0..0x4801d3): the word vector v
 * through the word matrix M (9 words, kind byte +0x12): kind < 1 copies
 * the six bytes; kind 1 rotates x and y with the upper 2 x 2 (z copied);
 * otherwise the full product; each sum + 0x2000 >> 14 (arithmetic) into a
 * word. The original re-reads v for every output word after writing the
 * previous one, so a call with out == v (the leaves part of
 * differential_view_test) feeds the new x into y and z. EAX out, except
 * that the copy (movsd + movsw) leaves EAX = out + 6. */
static int32_t mw(const ctx *c, uint32_t a) { return (int32_t)(int16_t)r16(c, a); }
static void matrix_apply_word(ctx *c, uint32_t v, uint32_t m, uint32_t out, uint32_t column, int full) {
    uint32_t sum = (uint32_t)mw(c, v) * (uint32_t)mw(c, m + column) + (uint32_t)mw(c, v + 2u) * (uint32_t)mw(c, m + column + 4u);
    if (full) sum += (uint32_t)mw(c, v + 4u) * (uint32_t)mw(c, m + column + 0xcu);
    w16(c, out, (uint16_t)((int32_t)(sum + 0x2000u) >> 14));
}
static uint32_t matrix_apply(ctx *c, uint32_t v, uint32_t m, uint32_t out) {
    int8_t kind = (int8_t)r8(c, m + 0x12u);
    if (kind < 1) {
        for (uint32_t i = 0; i < 6u; ++i) w8(c, out + i, r8(c, v + i));
        return out + 6u;
    }
    if (kind == 1) {
        matrix_apply_word(c, v, m, out, 0u, 0);
        matrix_apply_word(c, v, m, out + 2u, 2u, 0);
        w16(c, out + 4u, r16(c, v + 4u));
        return out;
    }
    matrix_apply_word(c, v, m, out, 0u, 1);
    matrix_apply_word(c, v, m, out + 2u, 2u, 1);
    /* The third column: M+8, M+0xa, M+0x10 (not M+8+0xc). */
    uint32_t sum = (uint32_t)mw(c, v) * (uint32_t)mw(c, m + 8u) + (uint32_t)mw(c, v + 2u) * (uint32_t)mw(c, m + 0xau) +
                   (uint32_t)mw(c, v + 4u) * (uint32_t)mw(c, m + 0x10u);
    w16(c, out + 4u, (uint16_t)((int32_t)(sum + 0x2000u) >> 14));
    return out;
}
void es2_sim_service_matrix_apply(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    sc->eax = matrix_apply(&c, sc->slots[0], sc->slots[1], sc->slots[2]);
}
/* 0x48be50(L) (0x48be50..0x48be94): the light list 0x6c60ac (count
 * [0x6c60a8], at most 10): appended unless full or present; AL 1 when
 * appended. 0x407800(L) (0x407800..0x40781c): [0x4a960c + 4 n] <- L, the
 * word n [0x497ae4] + 1. */
static int light_list_add(ctx *c, uint32_t l) {
    int32_t n = (int32_t)r32(c, 0x006c60a8u);
    if (n >= 10) return 0;
    for (int32_t k = 0; k < n; ++k) if (r32(c, 0x006c60acu + 4u * (uint32_t)k) == l) return 0;
    w32(c, 0x006c60acu + 4u * r32(c, 0x006c60a8u), l);
    w32(c, 0x006c60a8u, r32(c, 0x006c60a8u) + 1u);
    return 1;
}
static void scene_light_add(ctx *c, uint32_t l) {
    w32(c, 0x004a960cu + 4u * sx16(r16(c, 0x00497ae4u)), l);
    w16(c, 0x00497ae4u, (uint16_t)(r16(c, 0x00497ae4u) + 1u));
}
/* The scene light 0x46134c (listing 0x46134c..0x46144c, step 7 of the
 * root): new(0x34) = L; 0x48bc40(L), [L] <- 0x49f8d4, 0x48bc40(L+0x1e),
 * counter + 1, [L] <- 0x49f8c0, [L+0x16] <- 1, counter + 1, counter - 4;
 * L's slot +4 (L, 0x100) (0x48bcec: [L+0x1a] <- 0x100); the vectors f-34h
 * = (0, 0x1000, 0) and f-3ch = (0xe890, 0, 0x5208); euler_matrix(f-3ch,
 * f-50h) (hook); 0x4800c0(f-34h, f-50h, f-2ch); [L+0x10] <- dword f-2ch,
 * word [L+0x14] <- word f-28h; 0x48be50(L); 0x407800(L). EAX the head. A
 * NULL L (the original faults at the slot call) is refused. */
static int scene_light(ctx *c) {
    uint32_t l = 0;
    if (CALL(c, VA_NEW, &l, 0x34u)) return 1;
    if (!l) return null_refuse(c, 0x004613d2u, "scene light", "operator new returned NULL (the original faults)");
    gl_light(c, l);
    w32(c, l, 0x0049f8d4u);
    gl_light(c, l + 0x1eu);
    counter(c, 1);
    w32(c, l, 0x0049f8c0u);
    w32(c, l + 0x16u, 1);
    counter(c, 1);
    counter(c, -4);
    if (vcall(c, l, 4u, (const uint32_t[]){l, 0x100u}, 2, NULL)) return 1;
    uint32_t top = c->l->stack_hi, f = frame_push(c, 0x50u);
    if (!f) return refuse(c, 0x0046134cu, "scene light", "the own code's call area is exhausted");
    f += 0x50u;
    w16(c, f - 0x34u, 0); w16(c, f - 0x32u, 0x1000u); w16(c, f - 0x30u, 0);
    w16(c, f - 0x3cu, 0xe890u); w16(c, f - 0x3au, 0); w16(c, f - 0x38u, 0x5208u);
    int bad = CALL(c, 0x0047ebb8u, NULL, f - 0x3cu, f - 0x50u);
    if (!bad) {
        matrix_apply(c, f - 0x34u, f - 0x50u, f - 0x2cu);
        w32(c, l + 0x10u, r32(c, f - 0x2cu));
        w16(c, l + 0x14u, r16(c, f - 0x28u));
        light_list_add(c, l);
        scene_light_add(c, l);
    }
    frame_pop(c, top);
    return bad;
}
void es2_sim_service_scene_light(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t head = fs0_head();
    if (scene_light(&c)) { failed(sc); return; }
    sc->eax = head;
}
void es2_sim_service_light_intensity(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    w32(&c, sc->slots[0] + 0x1au, sc->slots[1]);
    sc->eax = sc->slots[0]; sc->edx = sc->slots[1];
}
void es2_sim_service_light_list_add(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    int added = light_list_add(&c, sc->slots[0]);
    sc->eax = added ? (sc->eax & 0xffffff00u) | 1u : 0;
    sc->ecx = sc->slots[0];
}
void es2_sim_service_scene_light_add(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    scene_light_add(&c, sc->slots[0]);
    sc->edx = sc->slots[0];
}
/* Constants 0x415464 (0x415464..0x415487): word [0x4a9b82] <- 1, words
 * [0x4a9ccc] <- [0x49a026], [0x4a9cce] <- [0x49a038]. EAX/EDX the words. */
void es2_sim_service_stage2_constants(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    w16(&c, 0x004a9b82u, 1);
    uint16_t a = r16(&c, 0x0049a026u), d = r16(&c, 0x0049a038u);
    w16(&c, 0x004a9cccu, a);
    w16(&c, 0x004a9cceu, d);
    sc->eax = (sc->eax & 0xffff0000u) | a; sc->edx = (sc->edx & 0xffff0000u) | d;
}
/* The binning tables 0x428230 (0x428230..0x42829c): [0x4cf878],
 * [0x4cf87c] <- new[](0x800) each; [0x4cf884] <- 0x49667c(0, 0x36, 0x200,
 * 1, 0x428e10); [0x4cf888] <- new[](0x800); 0x428efc(0x200): [0x49ac40] <-
 * 0x49667c(0, 0x1d, 0x200, 1, 0x429655); _memset(0x4aab00, 0, 0x249f0).
 * The element constructor 0x428e10 (0x428e10..0x428e34): [e] <- 0x49ac18,
 * 0x49ac28, 0x49ac38; EAX e, EDX e+4, ECX e+0x18. */
void es2_sim_service_binning_tables(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t x = 0;
    if (CALL(&c, VA_NEW_ARRAY, &x, 0x800u)) { failed(sc); return; }
    w32(&c, 0x004cf878u, x);
    if (CALL(&c, VA_NEW_ARRAY, &x, 0x800u)) { failed(sc); return; }
    w32(&c, 0x004cf87cu, x);
    if (CALL(&c, 0x0049667cu, &x, 0u, 0x36u, 0x200u, 1u, 0x00428e10u)) { failed(sc); return; }
    w32(&c, 0x004cf884u, x);
    if (CALL(&c, VA_NEW_ARRAY, &x, 0x800u)) { failed(sc); return; }
    w32(&c, 0x004cf888u, x);
    if (CALL(&c, 0x0049667cu, &x, 0u, 0x1du, 0x200u, 1u, 0x00429655u)) { failed(sc); return; }
    w32(&c, 0x0049ac40u, x);
    if (CALL(&c, VA_MEMSET, &x, 0x004aab00u, 0u, 0x249f0u)) { failed(sc); return; }
    sc->eax = x;
}
void es2_sim_service_bin_cell_ctor(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t e = sc->slots[0];
    w32(&c, e, 0x0049ac18u);
    w32(&c, e, 0x0049ac28u);
    w32(&c, e, 0x0049ac38u);
    sc->eax = e; sc->edx = e + 4u; sc->ecx = e + 0x18u;
}
/* 0x43034c (0x43034c..0x430391): [0x49b034] <- new[](0x408); 0x81 rows
 * [[0x49b034] + 8i] <- new[](0x102); dwords 0x4cfd64[0..4] <- 0. */
void es2_sim_service_row_buffers(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t x = 0;
    if (CALL(&c, VA_NEW_ARRAY, &x, 0x408u)) { failed(sc); return; }
    w32(&c, 0x0049b034u, x);
    for (uint32_t i = 0; i < 0x81u; ++i) {
        if (CALL(&c, VA_NEW_ARRAY, &x, 0x102u)) { failed(sc); return; }
        w32(&c, r32(&c, 0x0049b034u) + 8u * i, x);
    }
    for (uint32_t i = 0; i < 5u; ++i) w32(&c, 0x004cfd64u + 4u * i, 0);
    sc->eax = 0x004cfd78u; sc->edx = 0;
}
/* The table reader 0x492f54(dst, size, count, path) (0x492f54..0x49300e):
 * S; a status is the fatal report (0x492fab); stream_read(S, count * size
 * (signed words), dst) (hook); AL 0 is the fatal report (0x492feb); the
 * destructor. The colours 0x467b64 (0x467b64..0x467b89): 0x492f54(0x4d3b7c,
 * 0x36, 1, "colors".dat). */
static int table_read(ctx *c, uint32_t dst, uint32_t size, uint32_t count, uint32_t path) {
    uint32_t top = c->l->stack_hi, f = frame_push(c, 0x50u);
    if (!f) return refuse(c, 0x00492f54u, "table read", "the own code's call area is exhausted");
    uint32_t s = f + 0x50u - 0x30u, ok = 0;
    int bad = vol_stream(c, s, path, NULL);
    if (!bad && r32(c, s + 4u)) bad = fatal_report(c, 0x004a69d4u, 0x15u, path);   /* 0x492fab */
    if (!bad) bad = CALL(c, 0x004735b8u, &ok, s, sx16((uint16_t)count) * sx16((uint16_t)size), dst);
    if (!bad && !(ok & 0xffu)) bad = fatal_report(c, 0x004a69dcu, 0x17u, path);   /* 0x492feb */
    if (!bad) bad = CALL(c, VA_VOL_DESTROY, NULL, s, 2u);
    frame_pop(c, top);
    return bad;
}
void es2_sim_service_table_read(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t head = fs0_head();
    if (table_read(&c, sc->slots[0], sc->slots[1], sc->slots[2], sc->slots[3])) { failed(sc); return; }
    sc->eax = head;
}
void es2_sim_service_colours(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t path = 0;
    if (path_dir(&c, 0x004a0884u, r32(&c, 0x004a09f0u), &path) || table_read(&c, 0x004d3b7cu, 0x36u, 1u, path)) { failed(sc); return; }
}
/* Element constructors that construct an inner array:
 * 0x4207bb(e) (listing 0x4207bb..0x4207da): 0x49667c(e, 4, 7, 1, 0x492428);
 * EAX e, EDX/ECX the array constructor's. */
static const struct { uint32_t va, size, count, ctor, offset; int32_t counter; } inner_arrays[] = {
    {0x004207bbu, 4u, 7u, 0x00492428u, 0, 0},
    /* The same bytes at 0x4069c5, 0x423078, 0x438765 (the byte scan of CODE). */
    {0x004069c5u, 8u, 3u, 0x00406a98u, 0, 0},
    {0x00423078u, 6u, 3u, 0x004922d3u, 0, 0},
    {0x00438765u, 28u, 3u, 0x004387a3u, 0, 0},
    /* 0x420817(e) (0x420817..0x420844): the array at e+0xec (10 of 0x18,
     * 0x42067c), then counter + 1. */
    {0x00420817u, 0x18u, 10u, 0x0042067cu, 0xecu, 1},
};
void es2_sim_service_inner_array(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    for (unsigned i = 0; i < sizeof(inner_arrays) / sizeof(inner_arrays[0]); ++i) {
        if (inner_arrays[i].va != sc->va) continue;
        es2_sim_result r;
        if (call_regs(&c, 0x0049667cu, (const uint32_t[]){sc->slots[0] + inner_arrays[i].offset, inner_arrays[i].size, inner_arrays[i].count, 1u,
                                                            inner_arrays[i].ctor}, 5, NULL, &r)) { failed(sc); return; }
        sc->eax = sc->slots[0]; sc->edx = r.edx; sc->ecx = r.ecx;
        if (inner_arrays[i].counter) { counter(&c, inner_arrays[i].counter); sc->edx = r32(&c, VA_COUNTER_POINTER); }
        return;
    }
    sc->refused = 1; sc->reason = "no inner array for this VA";
}
/* ======================================================================
 * The string holder (error.str, sounds.str, mission strings): 0x453878(H, path)
 * (listing 0x453878..0x4538cd) with 0x4538d0 (0x4538d0..0x453914): S; S.read(4,
 * H+8); [H] <- new[]([H+8]); S.read([H+8], [H]); [H+4] <- [H]; AL 1; the
 * destructor; EAX 1-in-AL (the pushed result), EDX the head. 0x453918(H, out,
 * out2) (0x453918..0x45399c): the next string of the current group
 * (launcher.md, "DBSIM's start-up as own code"; the same contract as
 * startup.c's own copy): EAX 0, or H with AL 1. */
static int strings_load(ctx *c, uint32_t h, uint32_t path, uint32_t *al) {
    uint32_t top = c->l->stack_hi, f = frame_push(c, 0x50u);
    if (!f) return refuse(c, 0x00453878u, "string holder", "the own code's call area is exhausted");
    uint32_t s = f + 0x50u - 0x30u, b = 0;
    int bad = vol_stream(c, s, path, NULL) || sread(c, s, 4u, h + 8u) || CALL(c, VA_NEW_ARRAY, &b, r32(c, h + 8u));
    if (!bad) { w32(c, h, b); bad = sread(c, s, r32(c, h + 8u), b); }
    if (!bad) { w32(c, h + 4u, r32(c, h)); *al = 1; bad = CALL(c, VA_VOL_DESTROY, NULL, s, 2u); }
    frame_pop(c, top);
    return bad;
}
void es2_sim_service_strings_load(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t head = fs0_head(), al = 0;
    if (strings_load(&c, sc->slots[0], sc->slots[1], &al)) { failed(sc); return; }
    sc->eax = (sc->slots[0] & 0xffffff00u) | al; sc->edx = head;
}
void es2_sim_service_strings_next(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t h = sc->slots[0], out = sc->slots[1], out2 = sc->slots[2], cursor = r32(&c, h + 4u);
    if (!(r32(&c, h) + r32(&c, h + 8u) > cursor)) { sc->eax = 0; sc->ecx = 0; sc->edx = cursor; return; }
    if (!r16(&c, h + 0xcu)) {
        w16(&c, h + 0xcu, r16(&c, cursor));
        cursor += 2u;
        w32(&c, h + 4u, cursor);
        w16(&c, h + 0xeu, 0);
    }
    if ((int16_t)r16(&c, h + 0xeu) >= (int16_t)r16(&c, h + 0xcu)) {
        w16(&c, h + 0xeu, 0);
        w16(&c, h + 0xcu, 0);
        sc->eax = 0; sc->ecx = 0; sc->edx = cursor;
        return;
    }
    int16_t length = (int16_t)r16(&c, cursor);
    cursor += 2u;
    w32(&c, out, cursor);
    uint32_t p = cursor + (uint32_t)(int32_t)length;
    int8_t extra = (int8_t)r8(&c, p);
    ++p;
    sc->edx = ((uint32_t)(int32_t)length & 0xffffff00u) | (uint8_t)extra;
    if (out2) { w32(&c, out2, p); p += (uint32_t)(int32_t)extra; sc->edx = (uint32_t)(int32_t)extra; }
    w32(&c, h + 4u, p);
    w16(&c, h + 0xeu, (uint16_t)(r16(&c, h + 0xeu) + 1u));
    sc->eax = (h & 0xffffff00u) | 1u; sc->ecx = p;
}
/* 0x4539a0(H, array, n, out2) (listing 0x4539a0..0x4539f1): count = the
 * word at [H+4]; that many 0x453918(H, array + 4i, out2 ? out2 + 4i : 0)
 * (n not read); word H+0xc <- 0; AX the count, ECX H, EDX the count. */
void es2_sim_service_strings_group(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t h = sc->slots[0], array = sc->slots[1], out2 = sc->slots[3];
    uint16_t count = r16(&c, r32(&c, h + 4u));
    es2_sim_result r;
    memset(&r, 0, sizeof(r));
    for (int32_t i = 0; i < (int32_t)(int16_t)count; ++i) {
        if (call_regs(&c, 0x00453918u, (const uint32_t[]){h, array + 4u * (uint32_t)i, out2 ? out2 + 4u * (uint32_t)i : 0u}, 3, NULL, &r)) {
            failed(sc); return;
        }
    }
    w16(&c, h + 0xcu, 0);
    sc->eax = (r.eax & 0xffff0000u) | count; sc->ecx = h; sc->edx = (uint32_t)(int32_t)(int16_t)count;
}
void es2_sim_service_path_ext(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    uint32_t r;
    if (path_ext(&c, sc->slots[0], sc->slots[1], &r)) { failed(sc); return; }
    sc->eax = r;
}
/* ======================================================================
 * The world loader 0x42e010(W) (listing 0x42e010..0x42e6ff; mission_load.md
 * section 29: the guest under world_load) with its callees: the palette
 * load 0x430394, the sky constructor 0x42eb5c, its band set 0x42ebbc, the
 * range tables 0x430d08, the shade tables 0x42e760, the palette-cycle
 * registration 0x430638 (one clock read per cycle), the remap arena
 * 0x46a464, the world texture table 0x46bd9c, the reciprocal table
 * 0x4680a0 and the flat objects 0x4097a8. S's slot +0x18 reads; the hook
 * stream_read 0x4735b8 is called directly where the original does. */
static int rd(ctx *c, uint32_t s, uint32_t n, uint32_t buf) { return sread(c, s, n, buf); }
static int hread(ctx *c, uint32_t s, uint32_t n, uint32_t buf) { return CALL(c, 0x004735b8u, NULL, s, n, buf); }
/* 0x430394(name) (0x430394..0x4303ae): [0x49b020] <- 0x465f7c(name); EAX it. */
static int world_palette(ctx *c, uint32_t name, uint32_t *p) {
    if (CALL(c, 0x00465f7cu, p, name)) return 1;
    w32(c, 0x0049b020u, *p);
    return 0;
}
/* 0x42eb5c(S) (0x42eb5c..0x42ebba): 0x4758b4; [S] <- 0x49afb0; +0x6c 1,
 * +0x54 3, +0x58 0x10, +0x5c 1, +0x60 8, +0x64 1, +0x68 1, +0x50 0, bytes
 * +0x70 0, +0x71 1; counter + 1. */
static void sky_ctor(ctx *c, uint32_t s) {
    group_ctor(c, s);
    w32(c, s, 0x0049afb0u);
    w32(c, s + 0x6cu, 1); w32(c, s + 0x54u, 3); w32(c, s + 0x58u, 0x10u); w32(c, s + 0x5cu, 1);
    w32(c, s + 0x60u, 8); w32(c, s + 0x64u, 1); w32(c, s + 0x68u, 1); w32(c, s + 0x50u, 0);
    w8(c, s + 0x70u, 0); w8(c, s + 0x71u, 1);
    counter(c, 1);
}
/* 0x430d08(a, b, n, m, out) (0x430d08..0x430dff): per pair k < n (signed
 * words) of the word ranges a[k] = (start, count), b[k]: m - 1 steps from a
 * to b in 8.8 fixed point (the start and end slopes idiv by m - 1, kept as
 * words), each a (start, count) word pair; then b[k] itself. */
static void range_table(ctx *c, uint32_t a, uint32_t b, int16_t n, int16_t m, uint32_t out) {
    for (int32_t k = 0; (int32_t)n > k; ++k, a += 4u, b += 4u) {
        uint16_t a0 = r16(c, a), a1 = (uint16_t)(r16(c, a + 2u) + a0 - 1u);
        uint16_t b0 = r16(c, b), b1 = (uint16_t)(r16(c, b + 2u) + b0 - 1u);
        int32_t div = (int32_t)m - 1;
        int16_t s0 = (int16_t)((((int32_t)(int16_t)b0 - (int16_t)a0 + 1) << 8) / div);
        int16_t s1 = (int16_t)((((int32_t)(int16_t)b1 - (int16_t)a1 + 1) << 8) / div);
        int32_t e = 0, d = 0;
        for (int32_t j = 0; j < (int32_t)m - 1; ++j, out += 4u) {
            uint16_t hi = (uint16_t)((d >> 8) + (int16_t)a1);
            uint16_t lo = (uint16_t)((uint16_t)(e >> 8) + a0);
            w16(c, out, lo);
            w16(c, out + 2u, (uint16_t)(hi - lo + 1u));
            e += s0;
            d += s1;
        }
        w16(c, out, b0);
        w16(c, out + 2u, (uint16_t)(b1 - b0 + 1u));
        out += 4u;
    }
}
/* 0x42e760(name) (0x42e760..0x42e874): S; S.read(1, 0x4cfce4) = k;
 * [0x4cfce0] <- new[](5k), each record's byte 0 <- 0; S.read(1, f-51h) =
 * j; j times: S.read(1, f-52h) = r, R = [0x4cfce0] + 5r, S.read(1, R),
 * [R+1] <- new[](2 [R]), stream_read(S, 2 [R], [R+1]); the destructor. */
static int shade_tables(ctx *c, uint32_t name) {
    uint32_t top = c->l->stack_hi, f = frame_push(c, 0x54u), path = 0, x = 0;
    if (!f) return refuse(c, 0x0042e760u, "shade tables", "the own code's call area is exhausted");
    f += 0x54u;
    uint32_t s = f - 0x30u;
    int bad = path_dir(c, name, r32(c, 0x004a09f0u), &path) || vol_stream(c, s, path, NULL) || rd(c, s, 1u, 0x004cfce4u) ||
              CALL(c, VA_NEW_ARRAY, &x, (uint32_t)r8(c, 0x004cfce4u) * 5u);
    if (!bad) {
        w32(c, 0x004cfce0u, x);
        for (uint32_t i = 0; i < r8(c, 0x004cfce4u); ++i) w8(c, x + 5u * i, 0);
        bad = rd(c, s, 1u, f - 0x51u);
    }
    for (uint32_t i = 0; !bad && i < r8(c, f - 0x51u); ++i) {
        bad = rd(c, s, 1u, f - 0x52u);
        uint32_t rec = (uint32_t)r8(c, f - 0x52u) * 5u + r32(c, 0x004cfce0u), q = 0;
        if (!bad) bad = rd(c, s, 1u, rec) || CALL(c, VA_NEW_ARRAY, &q, (uint32_t)r8(c, rec) * 2u);
        if (!bad) { w32(c, rec + 1u, q); bad = hread(c, s, (uint32_t)r8(c, rec) * 2u, q); }
    }
    if (!bad) bad = CALL(c, VA_VOL_DESTROY, NULL, s, 2u);
    frame_pop(c, top);
    return bad;
}
/* 0x430638(R) (0x430638..0x430685): the first free of the five cycle slots
 * 0x4cfd64: [slot] <- R, [0x4cfd78 + 4k] <- clock (0x467828) + the signed
 * word R+4, word [0x4cfd8c + 2k] <- 0, AL 1; all used: EAX 0. */
static int cycle_register(ctx *c, uint32_t r, uint32_t *al) {
    for (uint32_t k = 0; k < 5u; ++k) {
        if (r32(c, 0x004cfd64u + 4u * k)) continue;
        w32(c, 0x004cfd64u + 4u * k, r);
        uint32_t now = 0;
        if (CALL(c, ES2_SIM_VA_CLOCK, &now, 0u)) return 1;
        w32(c, 0x004cfd78u + 4u * k, now + sx16(r16(c, r + 4u)));
        w16(c, 0x004cfd8cu + 2u * k, 0);
        *al = 1;
        return 0;
    }
    *al = 0;
    return 0;
}
/* 0x46a464(a, b, n) (0x46a464..0x46a4a8): with the word [0x4a0918] = 0:
 * p = new[]((n + 1) << 8), [0x6b4f20] <- p, [0x4a0998] <- p + 0x100 - (p
 * & 0xff) (the next 256-byte boundary). a and b are not read. */
static int remap_arena(ctx *c, uint32_t n) {
    if (r16(c, 0x004a0918u)) return 0;
    uint32_t p = 0;
    if (CALL(c, VA_NEW_ARRAY, &p, (n + 1u) << 8)) return 1;
    w32(c, 0x006b4f20u, p);
    w32(c, 0x004a0998u, 0x100u - (r32(c, 0x006b4f20u) & 0xffu) + p);
    return 0;
}
/* 0x46bd9c(name) (0x46bd9c..0x46bdf4): A = 0x45cdf4(name.dba); with -l the
 * count [A+4] is 2 while 0x46a03c(A) builds [0x6b4f40]; the count back;
 * counter + 1; 0x48349c(A, 3). */
static int world_textures(ctx *c, uint32_t name) {
    uint32_t path = 0, a = 0, t = 0;
    if (path_dir(c, name, r32(c, 0x004a0a10u), &path) || CALL(c, 0x0045cdf4u, &a, path)) return 1;
    if (!a) return null_refuse(c, 0x0046bdbcu, "world textures", "the bitmap array did not load (the original reads through NULL)");
    uint32_t saved = r32(c, a + 4u);
    if (r16(c, 0x004d2680u)) w32(c, a + 4u, 2);
    if (texture_table(c, a, &t)) return 1;
    w32(c, 0x006b4f40u, t);
    w32(c, a + 4u, saved);
    counter(c, 1);
    uint32_t x;
    return bitmap_array_destroy(c, a, 3, &x);
}
/* 0x4680a0(a, b) (0x4680a0..0x4680dc): [0x4d3cb4] <- a, [0x4d3cb8] <- b;
 * for i = 1 .. 0x289: [0x4d3cc0 + 4(i-1)] <- 0x47e068(1, i) = (1 << 16) / i
 * (idiv of the 64-bit value). */
static void reciprocals(ctx *c, uint32_t a, uint32_t b) {
    w32(c, 0x004d3cb4u, a);
    w32(c, 0x004d3cb8u, b);
    for (uint32_t i = 1; i < 0x28au; ++i) w32(c, 0x004d3cc0u + 4u * (i - 1u), (uint32_t)(0x10000 / (int32_t)i));
}
/* Reads NUL-terminated bytes one at a time into dst (the loops at
 * 0x42e470, 0x42e48b, 0x42e4ad, 0x42e4f2, 0x42e50d). */
static int read_text(ctx *c, uint32_t s, uint32_t dst) {
    for (;; ++dst) {
        if (rd(c, s, 1u, dst)) return 1;
        if (!r8(c, dst)) return 0;
    }
}
static int world_load_body(ctx *c, uint32_t w) {
    uint32_t top = c->l->stack_hi, f = frame_push(c, 0x15cu);
    if (!f) return refuse(c, 0x0042e010u, "world", "the own code's call area is exhausted");
    f += 0x15cu;
    uint32_t s = f - 0x30u, s2 = f - 0x64u, name = f - 0xc4u, text = f - 0x150u, path = 0, x = 0;
    int bad = 0;
    for (uint32_t i = 0; i < 0x14u; ++i) w8(c, 0x004cfcc4u + i, r8(c, w + i));
    uint16_t number = (uint16_t)(r16(c, 0x004cfcc4u) * 2u + r16(c, 0x004cfcd6u));
    for (uint32_t i = 0; i < 6u; ++i) w8(c, name + i, r8(c, 0x0049af88u + i));
    bad = CALL(c, 0x00496820u, NULL, sx16(number), name + 5u, 10u) || path_dir(c, name, r32(c, 0x004a09fcu), &path) ||
          world_palette(c, path, &x) || path_dir(c, name, 0x0049af8eu, &path) || vol_stream(c, s, path, NULL) ||
          CALL(c, VA_NEW, &x, 0x7au);
    if (bad) goto out;
    if (x) { sky_ctor(c, x); counter(c, -4); }
    w32(c, 0x0049aee0u, x);
    for (uint32_t off = 0x86u; off <= 0x94u; off += 2u) if ((bad = rd(c, s, 2u, f - off))) goto out;
    {
        uint32_t sky = r32(c, 0x0049aee0u);
        w32(c, sky + 0x6cu, sx16(r16(c, f - 0x86u)));
        w32(c, sky + 0x54u, sx16(r16(c, f - 0x8au)));
        w32(c, sky + 0x58u, sx16(r16(c, f - 0x8cu)));
        w32(c, sky + 0x5cu, sx16(r16(c, f - 0x90u)));
        w32(c, sky + 0x60u, sx16(r16(c, f - 0x92u)));
        w16(c, 0x0049aee8u, (uint16_t)(r16(c, f - 0x8eu) + r16(c, f - 0x92u) - 1u));
        /* 0x42ebbc(sky, a, b, d) (0x42ebbc..0x42ebe5): +0x28 <- (a + [+0x58] -
         * 1) & 0xffffff, +0x2c <- b & 0xffffff, +0x30 <- d. */
        w32(c, sky + 0x28u, (sx16(r16(c, f - 0x88u)) + r32(c, sky + 0x58u) - 1u) & 0xffffffu);
        w32(c, sky + 0x2cu, sx16(r16(c, f - 0x8eu)) & 0xffffffu);
        w32(c, sky + 0x30u, sx16(r16(c, f - 0x94u)));
    }
    bad = rd(c, s, 2u, f - 0x96u) || rd(c, s, 2u, 0x0049aeeau) || rd(c, s, 2u, 0x004cfcf2u) || rd(c, s, 2u, 0x004cfcf4u) ||
          rd(c, s, 2u, f - 0x98u) || rd(c, s, 2u, f - 0x9au);
    if (bad) goto out;
    w16(c, 0x004cfcf0u, 0x40u);
    w16(c, 0x004cfcf6u, 0x62u);
    static const uint32_t counts[2] = {0x004cfcd8u, 0x004cfcdau}, tables[2] = {0x004cfd00u, 0x004cfd04u};
    for (unsigned t = 0; t < 2u; ++t) {
        if ((bad = rd(c, s, 4u, f - 0xa0u))) goto out;
        w16(c, counts[t], r16(c, f - 0xa0u));
        if ((bad = CALL(c, VA_NEW_ARRAY, &x, sx16(r16(c, f - 0xa0u)) << 2))) goto out;
        w32(c, tables[t], x);
        for (int32_t i = 0; i < (int32_t)sx16(r16(c, counts[t])); ++i, x += 4u) if ((bad = rd(c, s, 4u, x))) goto out;
    }
    bad = rd(c, s, 2u, 0x004cfcdeu) || rd(c, s, 2u, 0x004cfcdcu) || hread(c, s, sx16(r16(c, 0x004cfcdcu)) << 2, f - 0x104u) ||
          rd(c, s, 2u, f - 0xa2u) || hread(c, s, sx16(r16(c, 0x004cfcdcu)) << 2, f - 0x144u) ||
          CALL(c, VA_NEW_ARRAY, &x, (sx16(r16(c, 0x004cfcdeu)) * sx16(r16(c, 0x004cfcdcu))) << 2);
    if (bad) goto out;
    w32(c, 0x004cfcf8u, x);
    range_table(c, f - 0x104u, f - 0x144u, (int16_t)r16(c, 0x004cfcdcu), (int16_t)r16(c, 0x004cfcdeu), x);
    bad = hread(c, s, 4u, f - 0xa8u) || hread(c, s, 4u, f - 0xacu) || CALL(c, VA_NEW_ARRAY, &x, sx16(r16(c, 0x004cfcdeu)) << 2);
    if (bad) goto out;
    w32(c, 0x004cfcfcu, x);
    range_table(c, f - 0xa8u, f - 0xacu, 1, (int16_t)r16(c, 0x004cfcdeu), x);
    bad = rd(c, s, 2u, f - 0xaeu) || rd(c, s, 2u, f - 0xb0u) || rd(c, s, 4u, 0x004cfce8u) || rd(c, s, 4u, 0x004cfcecu) ||
          read_text(c, s, text) || read_text(c, s, text) || read_text(c, s, text);
    if (bad) goto out;
    if (r8(c, text)) {
        uint32_t root = 0;
        if ((bad = path_dir(c, text, r32(c, 0x004a09fcu), &path) || CALL(c, 0x00474cb0u, &root, path))) goto out;
        w32(c, 0x0049aef8u, root);
    }
    bad = read_text(c, s, text) || read_text(c, s, f - 0x15cu) || CALL(c, 0x0047307cu, NULL, s) ||
          path_dir(c, name, 0x0049af92u, &path) || CALL(c, VA_VOL_OPEN, NULL, s, path);
    if (bad) goto out;
    if (!r32(c, s + 4u)) {
        if ((bad = rd(c, s, 2u, 0x0049aef0u))) goto out;
        if (r16(c, 0x0049aef0u)) {
            if ((bad = CALL(c, VA_NEW_ARRAY, &x, sx16(r16(c, 0x0049aef0u)) * 6u))) goto out;
            w32(c, 0x0049aeecu, x);
            /* The pointer advances 12 bytes per 6-byte entry (0x42e5bd and
             * 0x42e5c8): kept, it writes past the array as the original does. */
            for (int32_t i = 0; i < (int32_t)sx16(r16(c, 0x0049aef0u)); ++i, x += 12u) {
                uint32_t al = 0;
                if ((bad = rd(c, s, 2u, x) || rd(c, s, 2u, x + 2u) || rd(c, s, 2u, x + 4u) || cycle_register(c, x, &al))) goto out;
            }
        }
    }
    if ((bad = shade_tables(c, 0x0049af96u))) goto out;
    {
        uint32_t cosv = 0;
        /* heading_cos(0xe000) (hook) times [0x4d253e] + [0x4d2542], >> 14:
         * the first two arguments of 0x46a464, which it does not read. */
        if ((bad = CALL(c, 0x0047dff0u, &cosv, 0xe000u))) goto out;
        if ((bad = path_dir(c, name, 0x0049af9du, &path) || vol_stream(c, s2, path, NULL) || rd(c, s2, 4u, f - 0xb8u) ||
                   rd(c, s2, 4u, f - 0xb4u)))
            goto out;
        uint32_t cells = r32(c, f - 0xb8u) * r32(c, f - 0xb4u);
        if ((bad = remap_arena(c, cells) || hread(c, s2, cells << 8, r32(c, 0x004a0998u)) || CALL(c, VA_VOL_DESTROY, NULL, s2, 2u)))
            goto out;
    }
    if ((bad = world_textures(c, text))) goto out;
    reciprocals(c, r32(c, f - 0xb8u), r32(c, f - 0xb4u));
    w32(c, 0x004a5b00u, r32(c, 0x004a0998u));
    w32(c, 0x004a5b04u, r32(c, f - 0xb8u));
    w32(c, 0x004a5b08u, r32(c, f - 0xb8u));
    bad = CALL(c, 0x004097a8u, NULL, (uint32_t)r16(c, 0x0049aeeau)) || CALL(c, VA_VOL_DESTROY, NULL, s, 2u);
out:
    frame_pop(c, top);
    return bad;
}
void es2_sim_service_world_body(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t head = fs0_head();
    if (world_load_body(&c, sc->slots[0])) { failed(sc); return; }
    sc->eax = head;
}
/* 0x474cb0(name) (0x474cb0..0x474cd4): 0x474b30(0x492b90(name, "dpl"),
 * local); EAX its result. */
void es2_sim_service_shape_by_name(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t top = c.l->stack_hi, f = frame_push(&c, 4u), p = 0, x = 0;
    if (!f) { refuse(&c, 0x00474cb0u, "shape by name", "the own code's call area is exhausted"); failed(sc); return; }
    int bad = path_ext(&c, sc->slots[0], 0x004a0f34u, &p) || CALL(&c, 0x00474b30u, &x, p, f);
    frame_pop(&c, top);
    if (bad) { failed(sc); return; }
    sc->eax = x;
}

/* Element constructors that store constants (listing at each VA):
 * 0x4240e4(e) (0x4240e4..0x4240f3): EDX e+4, word [e+4] <- 0; EAX e. */
void es2_sim_service_element_store(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t e = sc->slots[0];
    switch (sc->va) {
    case 0x004240e4u: w16(&c, e + 4u, 0); sc->edx = e + 4u; break;
    /* 0x4207db (0x4207db..0x4207f6): [e] <- 0, counter + 2; ECX, EDX the counter pointer. */
    case 0x004207dbu: w32(&c, e, 0); counter(&c, 2); sc->ecx = sc->edx = r32(&c, VA_COUNTER_POINTER); break;
    /* 0x411892 (0x411892..0x4118a5): [e] <- 0, counter + 1; EDX 0, ECX the
     * counter pointer. 0x48de4c (0x48de4c..0x48de55): [e] <- 0; EDX 0. */
    case 0x00411892u: w32(&c, e, 0); counter(&c, 1); sc->edx = 0; sc->ecx = r32(&c, VA_COUNTER_POINTER); break;
    case 0x0048de4cu: w32(&c, e, 0); sc->edx = 0; break;
    default: sc->refused = 1; sc->reason = "no element constructor for this VA"; return;
    }
    sc->eax = e;
}

/* The shape of a building type 0x405ebc(t) (listing 0x405ebc..0x405fa8):
 * T = [0x4a959c] + 0x3c t (signed word t); with word T+6 set: slot
 * [0x4a9574] + 4 (word T+2); empty: 0x474de4(0x492bec("bases_an",
 * "dts"), T+2's word, 0x1e0003); +0x26 <- 0x4a9590 when word T+0x32 is
 * set, else 0x4a9588. Without: slot [0x4a957c] + 4 (word T+2); empty:
 * 0x474de4(0x492bec("bases", "dgs"), the word, 0x2bc0001); +0x26 <-
 * 0x4a9d9a for t = 0x34 with word T+0x32 set, else 0x4a9590 with it set,
 * else 0x4a9588. The index slot carries the upper half of the string
 * pointer in EAX (only its low word is read). */
void es2_sim_service_building_shape(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    int16_t t = (int16_t)sc->slots[0];
    uint32_t rec = (uint32_t)(int32_t)t * 0x3cu + r32(&c, 0x004a959cu), x = 0, path = 0;
    int animated = r16(&c, rec + 6u) != 0;
    uint32_t slot = sx16(r16(&c, rec + 2u)) * 4u + r32(&c, animated ? 0x004a9574u : 0x004a957cu);
    uint32_t name = animated ? 0x004976fbu : 0x00497708u;
    if (!r32(&c, slot)) {
        if (path_dir(&c, name, animated ? 0x00497704u : 0x0049770eu, &path) ||
            CALL(&c, 0x00474de4u, &x, path, (name & 0xffff0000u) | r16(&c, rec + 2u), animated ? 0x001e0003u : 0x02bc0001u)) {
            failed(sc); return;
        }
        w32(&c, slot, x);
        uint32_t value = !r16(&c, rec + 0x32u) ? 0x004a9588u : (!animated && t == 0x34) ? 0x004a9d9au : 0x004a9590u;
        w32(&c, r32(&c, slot) + 0x26u, value);
    }
}

/* The name list of a type file 0x492d00(name, n) (listing
 * 0x492d00..0x492e7e; mission_load.md section 26): the index BX is 0 and
 * never changes, so one file: the local copy f-5ch of name, E =
 * _strchr(copy, 0) (its end); L = new[](4n), zeroed; word f-52h <- 0; the
 * 18 bytes f-78h zeroed; [E] <- 0; the size 0x492c84(name.<[0x4a09ec]>)
 * into word f-78h and added to f-52h; B = new[](f-52h); with a nonzero
 * size: S on the file, stream_read(S, size, B), 0x492c4c(B, L), the
 * destructor; EAX L. 0x492c84(path) (0x492c84..0x492cfd): S; a status
 * gives 0, else stream_size(S) (hook); the destructor. 0x492c4c(B, L)
 * (0x492c4c..0x492c80): until a byte 0xa, each string's pointer into L
 * (when L is nonzero) and the count; EAX the count. */
static int file_size(ctx *c, uint32_t path, uint32_t *size) {
    uint32_t top = c->l->stack_hi, f = frame_push(c, 0x50u);
    if (!f) return refuse(c, 0x00492c84u, "file size", "the own code's call area is exhausted");
    uint32_t s = f + 0x50u - 0x30u;
    *size = 0;
    int bad = vol_stream(c, s, path, NULL);
    if (!bad && !r32(c, s + 4u)) bad = CALL(c, 0x00473334u, size, s);
    if (!bad) bad = CALL(c, VA_VOL_DESTROY, NULL, s, 2u);
    frame_pop(c, top);
    return bad;
}
static uint32_t split_lines(ctx *c, uint32_t b, uint32_t l) {
    uint32_t n = 0;
    while (r8(c, b) != 0xau) {
        ++n;
        if (l) { w32(c, l, b); l += 4u; }
        uint32_t k = 0;
        while (r8(c, b + k)) ++k;
        b += k + 1u;
    }
    return n;
}
static int name_list(ctx *c, uint32_t name, uint16_t n, uint32_t *result) {
    uint32_t top = c->l->stack_hi, f = frame_push(c, 0x78u);
    if (!f) return refuse(c, 0x00492d00u, "name list", "the own code's call area is exhausted");
    f += 0x78u;
    uint32_t copy = f - 0x5cu, end = 0, l = 0, b = 0, path = 0, size = 0;
    gstrcpy(c, copy, name);
    int bad = CALL(c, VA_STRCHR, &end, copy, 0u) || CALL(c, VA_NEW_ARRAY, &l, sx16(n) << 2) ||
              CALL(c, VA_MEMSET, NULL, l, 0u, sx16(n) << 2);
    if (!bad) {
        w16(c, f - 0x52u, 0);
        bad = CALL(c, VA_MEMSET, NULL, f - 0x78u, 0u, 0x12u);
    }
    if (!bad) {
        w8(c, end, 0);
        bad = path_dir(c, copy, r32(c, 0x004a09ecu), &path) || file_size(c, path, &size);
    }
    if (!bad) {
        w16(c, f - 0x78u, (uint16_t)size);
        w16(c, f - 0x52u, (uint16_t)(r16(c, f - 0x52u) + (uint16_t)size));
        bad = CALL(c, VA_NEW_ARRAY, &b, sx16(r16(c, f - 0x52u)));
    }
    if (!bad && r16(c, f - 0x78u)) {
        w8(c, end, 0);
        uint32_t s = f - 0x30u;
        bad = path_dir(c, copy, r32(c, 0x004a09ecu), &path) || vol_stream(c, s, path, NULL) ||
              hread(c, s, sx16(r16(c, f - 0x78u)), b);
        if (!bad) { split_lines(c, b, l); bad = CALL(c, VA_VOL_DESTROY, NULL, s, 2u); }
    }
    frame_pop(c, top);
    *result = l;
    return bad;
}
void es2_sim_service_name_list(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t head = fs0_head(), l = 0;
    if (name_list(&c, sc->slots[0], (uint16_t)sc->slots[1], &l)) { failed(sc); return; }
    sc->eax = l; sc->edx = head;
}
void es2_sim_service_file_size(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t head = fs0_head(), size = 0;
    if (file_size(&c, sc->slots[0], &size)) { failed(sc); return; }
    sc->eax = size; sc->edx = head;
}
void es2_sim_service_split_lines(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    sc->eax = split_lines(&c, sc->slots[0], sc->slots[1]);
}

/* The weapon-mount element 0x42067c(e) (listing 0x42067c..0x4206c8):
 * [e] <- 0x4a69f0, 0x4a6a14, counter + 1, 0x4a6250, counter + 1; the hook
 * shape_word_block(e+0x10, 0); [e] <- 0x49a494; counter + 1; EAX e. */
void es2_sim_service_mount_ctor(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t e = sc->slots[0];
    w32(&c, e, 0x004a69f0u);
    w32(&c, e, 0x004a6a14u); counter(&c, 1);
    w32(&c, e, 0x004a6250u); counter(&c, 1);
    if (CALL(&c, 0x00402fc0u, NULL, e + 0x10u, 0u)) { failed(sc); return; }
    w32(&c, e, 0x0049a494u);
    counter(&c, 1);
    sc->eax = e; sc->ecx = r32(&c, VA_COUNTER_POINTER);
}

/* The weapon displays 0x4378d8(n) (listing 0x4378d8..0x4379cb; from
 * mech_types): [0x49bbe8] <- 0x49667c(0, 0x5c, n, 1, 0x438765) (signed
 * word n); [0x4d16d0] <- 0x45cdf4("weapons".hba when byte [0x4d256c] is
 * set, else .dba); S on "weapons".pdg; S.read(4, f-54h) = k;
 * stream_read(S, 8k, 0x4d16d4); per pair i < k: dword +0 <<= the word
 * [0x4d253a], +4 <<= the word [0x4d253c] (shift counts mod 32); the
 * destructor. */
static int weapon_displays(ctx *c, uint16_t n) {
    uint32_t x = 0, path = 0;
    if (CALL(c, 0x0049667cu, &x, 0u, 0x5cu, sx16(n), 1u, 0x00438765u)) return 1;
    w32(c, 0x0049bbe8u, x);
    if (path_dir(c, 0x0049bc38u, r8(c, 0x004d256cu) ? 0x0049bc40u : 0x0049bc44u, &path) || CALL(c, 0x0045cdf4u, &x, path)) return 1;
    w32(c, 0x004d16d0u, x);
    uint32_t top = c->l->stack_hi, f = frame_push(c, 0x54u);
    if (!f) return refuse(c, 0x004378d8u, "weapon displays", "the own code's call area is exhausted");
    f += 0x54u;
    uint32_t s = f - 0x30u;
    int bad = path_dir(c, 0x0049bc38u, 0x0049bc48u, &path) || vol_stream(c, s, path, NULL) || sread(c, s, 4u, f - 0x54u) ||
              hread(c, s, r32(c, f - 0x54u) << 3, 0x004d16d4u);
    if (!bad) {
        for (int32_t i = 0; i < (int32_t)r32(c, f - 0x54u); ++i) {
            uint32_t e = 0x004d16d4u + 8u * (uint32_t)i;
            w32(c, e, r32(c, e) << (r16(c, 0x004d253au) & 31u));
            w32(c, e + 4u, r32(c, e + 4u) << (r16(c, 0x004d253cu) & 31u));
        }
        bad = CALL(c, VA_VOL_DESTROY, NULL, s, 2u);
    }
    frame_pop(c, top);
    return bad;
}
void es2_sim_service_weapon_displays(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t head = fs0_head();
    if (weapon_displays(&c, (uint16_t)sc->slots[0])) { failed(sc); return; }
    sc->eax = head;
}


/* ======================================================================
 * The TS part lookups (vtable slots +0x20 and +0x24; listings
 * 0x47622c..0x47623f, 0x476240..0x476246, 0x476850..0x47688d,
 * 0x476890..0x4768ef). 0x47622c(P, id): EAX P when the word id equals word
 * P+6, else 0. 0x476850(P, id): the same test, else each child of the
 * list (word count +0x10, array +0x12) through its slot +0x20 (P, id)
 * until one answers. 0x476890(P, id): the address of the first child slot
 * whose child's word +6 is id, else each child's slot +0x24 until one
 * answers; EAX the result (0 for none). */
static int part_find(ctx *c, uint32_t p, uint32_t id, uint32_t *out) {
    if ((uint16_t)id == r16(c, p + 6u)) { *out = p; return 0; }
    uint32_t r = 0;
    for (uint32_t i = 0; i < r16(c, p + 0x10u) && !r; ++i) {
        uint32_t ch = r32(c, r32(c, p + 0x12u) + 4u * i);
        if (vcall(c, ch, 0x20u, (const uint32_t[]){ch, id}, 2, &r)) return 1;
    }
    *out = r;
    return 0;
}
static int part_slot_find(ctx *c, uint32_t p, uint32_t id, uint32_t *out) {
    uint32_t r = 0;
    for (uint32_t i = 0; i < r16(c, p + 0x10u) && !r; ++i)
        if (r16(c, r32(c, r32(c, p + 0x12u) + 4u * i) + 6u) == (uint16_t)id) r = r32(c, p + 0x12u) + 4u * i;
    for (uint32_t i = 0; i < r16(c, p + 0x10u) && !r; ++i) {
        uint32_t ch = r32(c, r32(c, p + 0x12u) + 4u * i);
        if (vcall(c, ch, 0x24u, (const uint32_t[]){ch, (uint32_t)(uint16_t)id}, 2, &r)) return 1;
    }
    *out = r;
    return 0;
}
void es2_sim_service_part_lookup(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t p = sc->slots[0], id = sc->slots[1], r = 0;
    switch (sc->va) {
    case 0x0047622cu:
        sc->edx = (sc->edx & 0xffff0000u) | (uint16_t)id;
        sc->eax = (uint16_t)id == r16(&c, p + 6u) ? p : 0;
        return;
    case 0x00476850u: if (part_find(&c, p, id, &r)) { failed(sc); return; } break;
    case 0x00476890u: if (part_slot_find(&c, p, id, &r)) { failed(sc); return; } break;
    default: sc->refused = 1; sc->reason = "no part lookup for this VA"; return;
    }
    sc->eax = r;
}
/* Constant bodies (the byte scan named with their rows): EAX the constant. */
static const struct { uint32_t va, value; } constant_bodies[] = {
    {0x00407ac6u, 0x00000000u},
    {0x004096dcu, 0x00000000u},
    {0x0040acb4u, 0x00000000u},
    {0x00420729u, 0x00000000u},
    {0x00427624u, 0x00000000u},
    {0x00451eb3u, 0x00000000u},
    {0x0045236cu, 0x00000000u},
    {0x0047161eu, 0x00000000u},
    {0x00476240u, 0x00000000u},
    {0x00477a84u, 0x00000000u},
    {0x004786d2u, 0x00000000u},
    {0x0047a8f4u, 0x00000000u},
    {0x00480814u, 0x00000000u},
    {0x00480840u, 0x00000000u},
    {0x00480f10u, 0x00000000u},
    {0x00480f18u, 0x00000000u},
    {0x00481d48u, 0x00000000u},
    {0x00482868u, 0x00000000u},
    {0x0048a1ccu, 0x00000000u},
    {0x0048dce8u, 0x00000000u},
    {0x0048dcf0u, 0x00000000u},
    {0x0048df40u, 0x00000000u},
    {0x004275f0u, 0x02bc0001u},
    {0x00480834u, 0x006c5f50u},
    {0x00481d50u, 0x0028000eu},
    {0x00482148u, 0x0028000fu},
    {0x00482870u, 0x00280005u},
    {0x00482dfcu, 0x00280001u},
    {0x00483008u, 0x0028000cu},
    {0x00483014u, 0x00000010u},
    {0x00483020u, 0x00000018u},
    {0x004830d8u, 0x0028000du},
    {0x004833e8u, 0x0028000bu},
    {0x0048df34u, 0x0014000eu},
    {0x0048df48u, 0x00000004u},
    {0x0048e008u, 0x00140001u},
    {0x0048e014u, 0x00000008u},
    {0x0048e020u, 0x0000000cu},
    {0x0048e22cu, 0x00140002u},
    {0x0048e24cu, 0x0000000eu},
    {0x0048e3d4u, 0x0014000fu},
    {0x0048e498u, 0x00140003u},
    {0x0048e55cu, 0x00140009u},
    {0x0048e57cu, 0x00000010u},
    {0x0048e704u, 0x00140010u},
    {0x0048e7d4u, 0x00140011u},
    {0x0048e8a4u, 0x00140012u},
    {0x0048e924u, 0x00140014u},
    {0x0048ee84u, 0x0014000au},
    {0x0048f97cu, 0x00140005u},
    {0x0048f988u, 0x0000000cu},
    {0x0048f994u, 0x00000010u},
    {0x0048fd6cu, 0x00000014u},
    {0x0048fd78u, 0x00140013u},
    {0x0048fe0cu, 0x00140007u},
    {0x00490078u, 0x0014000bu},
    {0x00490214u, 0x0014000cu},
    {0x004903fcu, 0x00140015u},
    {0x00490ddcu, 0x00140008u},
    {0x00491484u, 0x001e0001u},
    {0x00491a44u, 0x001e0004u},
    {0x00491ae4u, 0x001e0002u},
    {0x004921e8u, 0x001e0003u},
};
void es2_sim_service_return_constant(es2_sim_service_call *sc, void *user) {
    (void)user;
    for (unsigned i = 0; i < sizeof(constant_bodies) / sizeof(constant_bodies[0]); ++i)
        if (constant_bodies[i].va == sc->va) { sc->eax = constant_bodies[i].value; return; }
    sc->refused = 1; sc->reason = "no constant body for this VA";
}

/* The damage display of a type 0x4379cc(name, t) (listing
 * 0x4379cc..0x437c65; from mech_type_load): D = [0x49bbe8] + 0x5c t; A =
 * 0x45cdf4(name.hba when byte [0x4d256c] is set, else .dba); S on
 * name.pdg (a status: the fatal report 0x437a62 with the name); S.read(4) = k; per entry
 * i < k (0x1c bytes at D + 0x1c i): +0 <- [[A+8] + 4i]; stream_read(S, 8,
 * +4), the pair shifted left by the words [0x4d253a]/[0x4d253c];
 * stream_read(S, 8, +0xc), shifted, + 1 each with the byte flag; S.read(4)
 * = m; +0x14 <- m, +0x18 <- 0x49667c(0, 0x1c, m, 1, 0x438785),
 * stream_read(S, 0x1c m); per element: +0x14 <- the signed word
 * [0x4d3b7c + 2 (its word +0x14)], the pairs +4 and +0xc shifted, +0xc/+0x10
 * + 1 with the flag. Then S.read(4) = q into D+0x54; D+0x58 <-
 * 0x49667c(0, 0x14, q, 1, 0x438790), stream_read(S, 0x14 q), each
 * element's pair +0 shifted; the destructor; EAX D. */
static void shift_pair(ctx *c, uint32_t p) {
    w32(c, p, r32(c, p) << (r16(c, 0x004d253au) & 31u));
    w32(c, p + 4u, r32(c, p + 4u) << (r16(c, 0x004d253cu) & 31u));
}
static int damage_display(ctx *c, uint32_t name, uint16_t t, uint32_t *result) {
    uint32_t d = r32(c, 0x0049bbe8u) + 0x5cu * sx16(t), a = 0, path = 0;
    *result = d;
    if (path_dir(c, name, r8(c, 0x004d256cu) ? 0x0049bc4cu : 0x0049bc50u, &path) || CALL(c, 0x0045cdf4u, &a, path)) return 1;
    uint32_t top = c->l->stack_hi, f = frame_push(c, 0x68u);
    if (!f) return refuse(c, 0x004379ccu, "damage display", "the own code's call area is exhausted");
    f += 0x68u;
    uint32_t s = f - 0x30u;
    int bad = path_dir(c, name, 0x0049bc10u, &path) || vol_stream(c, s, path, NULL);
    if (!bad && r32(c, s + 4u)) bad = fatal_report(c, 0x0049bc54u, 0x48u, name);   /* 0x437a62 */
    if (!bad) bad = sread(c, s, 4u, f - 0x5cu);
    uint8_t flag = r8(c, 0x004d256cu);
    uint32_t e = d;
    for (int32_t i = 0; !bad && i < (int32_t)r32(c, f - 0x5cu); ++i, e += 0x1cu) {
        w32(c, e, r32(c, r32(c, a + 8u) + 4u * (uint32_t)i));
        bad = hread(c, s, 8u, e + 4u);
        if (bad) break;
        shift_pair(c, e + 4u);
        if ((bad = hread(c, s, 8u, e + 0xcu))) break;
        shift_pair(c, e + 0xcu);
        if (flag) { w32(c, e + 0xcu, r32(c, e + 0xcu) + 1u); w32(c, e + 0x10u, r32(c, e + 0x10u) + 1u); }
        uint32_t arr = 0;
        if ((bad = sread(c, s, 4u, f - 0x60u) || CALL(c, 0x0049667cu, &arr, 0u, 0x1cu, r32(c, f - 0x60u), 1u, 0x00438785u))) break;
        w32(c, e + 0x14u, r32(c, f - 0x60u));
        w32(c, e + 0x18u, arr);
        if ((bad = hread(c, s, r32(c, f - 0x60u) * 0x1cu, arr))) break;
        for (int32_t j = 0; j < (int32_t)r32(c, f - 0x60u); ++j, arr += 0x1cu) {
            w32(c, arr + 0x14u, sx16(r16(c, 0x004d3b7cu + 2u * sx16(r16(c, arr + 0x14u)))));
            shift_pair(c, arr + 4u);
            shift_pair(c, arr + 0xcu);
            if (flag) { w32(c, arr + 0xcu, r32(c, arr + 0xcu) + 1u); w32(c, arr + 0x10u, r32(c, arr + 0x10u) + 1u); }
        }
    }
    if (!bad) {
        uint32_t tail = d + 0x54u, arr = 0;
        bad = sread(c, s, 4u, f - 0x60u);
        if (!bad) { w32(c, tail, r32(c, f - 0x60u)); bad = CALL(c, 0x0049667cu, &arr, 0u, 0x14u, r32(c, f - 0x60u), 1u, 0x00438790u); }
        if (!bad) { w32(c, tail + 4u, arr); bad = hread(c, s, r32(c, f - 0x60u) * 0x14u, arr); }
        for (int32_t j = 0; !bad && j < (int32_t)r32(c, f - 0x60u); ++j) shift_pair(c, r32(c, tail + 4u) + 0x14u * (uint32_t)j);
    }
    if (!bad) bad = CALL(c, VA_VOL_DESTROY, NULL, s, 2u);
    frame_pop(c, top);
    return bad;
}
void es2_sim_service_damage_display(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t head = fs0_head(), d = 0;
    if (damage_display(&c, sc->slots[0], (uint16_t)sc->slots[1], &d)) { failed(sc); return; }
    sc->eax = d; sc->edx = head;
}

/* 0x474c38(name) (listing 0x474c38..0x474c72): the arena push 200,000; X =
 * 0x474b30(0x492b90(name, the extension 0x4a0f2c), local); the arena pop;
 * EAX X. (Its twin 0x474c74 with the extension 0x4a0f30 has no static
 * reference, //sim:table_test, and gets no row.)
 * 0x474c10(S) (0x474c10..0x474c36): the header peek into a local, then
 * dts_load(S, 0); EAX its result. */
static int shape_in_arena(ctx *c, uint32_t name, uint32_t ext, uint32_t *x) {
    uint32_t top = c->l->stack_hi, f = frame_push(c, 4u), p = 0;
    if (!f) return refuse(c, 0x00474c38u, "shape", "the own code's call area is exhausted");
    int bad = arena_push(c, 0x30d40u) || path_ext(c, name, ext, &p) || CALL(c, 0x00474b30u, x, p, f) || arena_pop(c);
    frame_pop(c, top);
    return bad;
}
void es2_sim_service_shape_in_arena(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t x = 0;
    if (shape_in_arena(&c, sc->slots[0], 0x004a0f2cu, &x)) { failed(sc); return; }
    sc->eax = x;
}
void es2_sim_service_peek_load(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t top = c.l->stack_hi, f = frame_push(&c, 8u), x = 0, s = sc->slots[0];
    if (!f) { refuse(&c, 0x00474c10u, "peek load", "the own code's call area is exhausted"); failed(sc); return; }
    int ok = 0, bad = header_peek(&c, s, f, &ok) || CALL(&c, VA_DTS_LOAD, &x, s, 0u);
    frame_pop(&c, top);
    if (bad) { failed(sc); return; }
    sc->eax = x;
}

/* The terrain height cache (terrain.md; listings 0x4952e8..0x4952fb,
 * 0x4952fc..0x49534a, 0x4954e4..0x4954fc). 0x4952e8(H, a, b): [H+8] <- a,
 * [H+4] <- b. 0x4952fc(H): _memset([H+0x10], 0, [H+0xc]); the local
 * vector (0, 0, 0x10000) through 0x4954e4(v, 0x6c5fb4, [H+0x18]) =
 * rotate_point(v, 0x6c5fb4, [H+0x18]) (hook). */
void es2_sim_service_height_cache_set(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    w32(&c, sc->slots[0] + 8u, sc->slots[1]);
    w32(&c, sc->slots[0] + 4u, sc->slots[2]);
    sc->eax = sc->slots[0]; sc->edx = sc->slots[1]; sc->ecx = sc->slots[2];
}
void es2_sim_service_height_cache_reset(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t h = sc->slots[0], top = c.l->stack_hi, v = frame_push(&c, 0xcu);
    if (!v) { refuse(&c, 0x004952fcu, "height cache", "the own code's call area is exhausted"); failed(sc); return; }
    es2_sim_result r;
    int bad = CALL(&c, VA_MEMSET, NULL, r32(&c, h + 0x10u), 0u, r32(&c, h + 0xcu));
    if (!bad) {
        w32(&c, v, 0); w32(&c, v + 4u, 0); w32(&c, v + 8u, 0x10000u);
        bad = call_regs(&c, 0x00480304u, (const uint32_t[]){v, 0x006c5fb4u, r32(&c, h + 0x18u)}, 3, NULL, &r);
    }
    frame_pop(&c, top);
    if (bad) { failed(sc); return; }
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
}
void es2_sim_service_rotate_thunk(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    es2_sim_result r;
    if (call_regs(&c, 0x00480304u, (const uint32_t[]){sc->slots[0], sc->slots[1], sc->slots[2]}, 3, NULL, &r)) { failed(sc); return; }
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
}

/* 0x496406(p, offset) as a row (see rtti_count): EDX the offset when p is
 * nonzero. */
void es2_sim_service_rtti_count(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    sc->eax = rtti_count(&c, sc->slots[0], sc->slots[1]);
    if (sc->slots[0]) sc->edx = sc->slots[1];
}

/* The weapon shape cache 0x40f998 (listing 0x40f998..0x40fa86; from
 * actor_load): S on "mechwpns".dts; [0x4a9ae4] <- 0; while the header
 * peek succeeds: a tag other than 0x140008 is the fatal report (0x40fa18);
 * n = 0x47a540(header) (0x47a540..0x47a55b: the length when byte +3 has a
 * high nibble, else (length + 9) & -2); [0x4a99f4 + 4k] <- n, [0x4a9904 +
 * 4k] <- new[](n), stream_read(S, n, it) (the object's raw bytes, header
 * included); [0x4a9ae4] + 1; the destructor. */
static uint32_t object_size(const ctx *c, uint32_t h) {
    return (r8(c, h + 3u) & 0xf0u) ? r32(c, h + 4u) : (r32(c, h + 4u) + 9u) & ~1u;
}
void es2_sim_service_object_size(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    sc->eax = object_size(&c, sc->slots[0]);
}
static int weapon_shapes(ctx *c) {
    uint32_t top = c->l->stack_hi, f = frame_push(c, 0x5cu), path = 0;
    if (!f) return refuse(c, 0x0040f998u, "weapon shapes", "the own code's call area is exhausted");
    f += 0x5cu;
    uint32_t s = f - 0x30u, h = f - 0x5cu, sizes = 0x004a99f4u, bufs = 0x004a9904u;
    int bad = path_dir(c, 0x004991e0u, r32(c, 0x004a09f8u), &path) || vol_stream(c, s, path, NULL);
    if (!bad) w32(c, 0x004a9ae4u, 0);
    for (int ok = 1; !bad;) {
        if ((bad = header_peek(c, s, h, &ok)) || !ok) break;
        if (r32(c, h) != 0x00140008u && (bad = fatal_report(c, 0x004991e9u, 0x90u, path))) break;   /* 0x40fa18 */
        uint32_t n = object_size(c, h), b = 0;
        w32(c, sizes, n);
        if ((bad = CALL(c, VA_NEW_ARRAY, &b, n))) break;
        w32(c, bufs, b);
        bad = hread(c, s, r32(c, sizes), r32(c, bufs));
        sizes += 4u; bufs += 4u;
        w32(c, 0x004a9ae4u, r32(c, 0x004a9ae4u) + 1u);
    }
    if (!bad) bad = CALL(c, VA_VOL_DESTROY, NULL, s, 2u);
    frame_pop(c, top);
    return bad;
}
void es2_sim_service_weapon_shapes(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t head = fs0_head();
    if (weapon_shapes(&c)) { failed(sc); return; }
    sc->eax = head;
}


/* The arena push and pop and the header helpers as rows (see arena_push,
 * arena_pop, header_peek, object_skip): 0x474bbc EAX the block; 0x474be4
 * EAX the index, EDX the record; 0x47a308 and 0x47a3b4 AL the result. */
void es2_sim_service_arena_push(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    if (arena_push(&c, sc->slots[0])) { failed(sc); return; }
    sc->eax = r32(&c, r32(&c, VA_ARENA_CURRENT)); sc->edx = sc->eax;
}
void es2_sim_service_arena_pop(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    if (arena_pop(&c)) { failed(sc); return; }
    sc->eax = r32(&c, VA_ARENA_INDEX); sc->edx = r32(&c, VA_ARENA_CURRENT);
}
void es2_sim_service_header_peek(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    int ok = 0;
    if (header_peek(&c, sc->slots[0], sc->slots[1], &ok)) { failed(sc); return; }
    sc->eax = ok ? 1u : 0u;
}
void es2_sim_service_object_skip(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    int ok = 0;
    if (object_skip(&c, sc->slots[0], &ok)) { failed(sc); return; }
    sc->eax = ok ? 1u : 0u;
}

/* ======================================================================
 * The memory stream M (0x14 bytes, vtable 0x4a1788; listings
 * 0x47b36c..0x47b50b, 0x481aea..0x481b16) and the stream base class
 * (0x47a888..0x47a8d2): +4 status, +8 base, +0xc end, +0x10 cursor.
 * 0x47a888(S): [S] <- 0x4a16e8, [S+4] <- 0, counter + 1. 0x47a8a4(S, f):
 * counter - 1; S nonzero: [S] <- 0x4a16e8, its slot +0x10 (S) (0x47a904:
 * AL 1), f bit 0: delete(S). 0x47b36c(M, n, buf): 0x47a888; [M] <-
 * 0x4a1788; base buf, end buf + n, cursor buf; counter + 1; EAX M.
 * 0x481aea(M, f): M nonzero: counter - 1, 0x47a8a4(M, 0), f bit 0: delete.
 * 0x47b3a0(M, flag): status <- 1, or at the end 1 with flag else 2; EAX 0.
 * 0x47b400 tell: cursor - base. 0x47b410 seek(M, pos): status 2 becomes 0;
 * a status: EAX 0; pos outside [0, end - base]: 0x47b3a0(M, 0); else cursor
 * <- base + pos, AL 1. 0x47b4b4 read(M, n, dst) / 0x47b45c write(M, n,
 * src): a status: EAX 0; fewer than n left: _memcpy of the rest, cursor to
 * the end, 0x47b3a0(M, 0 for read, 1 for write); else _memcpy of n, cursor
 * + n, AL 1 (EAX's upper bytes the memcpy's result). 0x47b454: AL 1. */
static void stream_base_ctor(ctx *c, uint32_t s) { w32(c, s, VT_STREAM_BASE); w32(c, s + 4u, 0); counter(c, 1); }
static int stream_base_dtor(ctx *c, uint32_t s, uint32_t flags) {
    counter(c, -1);
    if (!s) return 0;
    w32(c, s, VT_STREAM_BASE);
    uint32_t slot = r32(c, VT_STREAM_BASE + 0x10u);
    if (slot != 0x0047a904u && CALL(c, slot, NULL, s)) return 1;
    if (flags & 1u) return CALL(c, VA_DELETE, NULL, s);
    return 0;
}
static void memory_status(ctx *c, uint32_t m, uint32_t flag) {
    if (r32(c, m + 0x10u) == r32(c, m + 0xcu)) w32(c, m + 4u, flag ? 1u : 2u);
    else w32(c, m + 4u, 1);
}
static void memory_stream(ctx *c, uint32_t m, uint32_t n, uint32_t buf) {
    stream_base_ctor(c, m);
    w32(c, m, 0x004a1788u);
    w32(c, m + 8u, buf);
    w32(c, m + 0xcu, n + buf);
    w32(c, m + 0x10u, buf);
    counter(c, 1);
}
void es2_sim_service_memory_stream(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t m = sc->slots[0], n = sc->slots[1], buf = sc->slots[2];
    switch (sc->va) {
    case 0x0047a888u: stream_base_ctor(&c, m); sc->eax = m; sc->edx = 0; sc->ecx = r32(&c, VA_COUNTER_POINTER); return;
    case 0x0047a8a4u: if (stream_base_dtor(&c, m, n)) { failed(sc); } return;
    case 0x0047b36cu: memory_stream(&c, m, n, buf); sc->eax = m; sc->edx = r32(&c, VA_COUNTER_POINTER); sc->ecx = m; return;
    case 0x00481aeau:
        if (m) { counter(&c, -1); if (stream_base_dtor(&c, m, 0) || ((n & 1u) && CALL(&c, VA_DELETE, NULL, m))) failed(sc); }
        return;
    case 0x0047b3a0u: memory_status(&c, m, n); sc->eax = 0; return;
    case 0x0047b400u: sc->eax = r32(&c, m + 0x10u) - r32(&c, m + 8u); sc->edx = sc->eax; return;
    case 0x0047b410u: {
        int32_t pos = (int32_t)n;
        sc->edx = n;
        if (r32(&c, m + 4u) == 2u) w32(&c, m + 4u, 0);
        if (r32(&c, m + 4u)) { sc->eax = 0; return; }
        if (pos < 0 || pos > (int32_t)(r32(&c, m + 0xcu) - r32(&c, m + 8u))) { memory_status(&c, m, 0); sc->eax = 0; return; }
        w32(&c, m + 0x10u, (uint32_t)pos + r32(&c, m + 8u));
        sc->eax = (m & 0xffffff00u) | 1u;
        return;
    }
    case 0x0047b4b4u: case 0x0047b45cu: {
        int read = sc->va == 0x0047b4b4u;
        if (r32(&c, m + 4u)) { sc->eax = 0; return; }
        uint32_t left = r32(&c, m + 0xcu) - r32(&c, m + 0x10u), r = 0;
        if ((int32_t)left < (int32_t)n) {
            if (CALL(&c, 0x0049680eu, &r, read ? buf : r32(&c, m + 0x10u), read ? r32(&c, m + 0x10u) : buf, left)) { failed(sc); return; }
            w32(&c, m + 0x10u, r32(&c, m + 0x10u) + left);
            memory_status(&c, m, read ? 0u : 1u);
            sc->eax = 0;
            return;
        }
        if (CALL(&c, 0x0049680eu, &r, read ? buf : r32(&c, m + 0x10u), read ? r32(&c, m + 0x10u) : buf, n)) { failed(sc); return; }
        w32(&c, m + 0x10u, r32(&c, m + 0x10u) + n);
        sc->eax = (r & 0xffffff00u) | 1u;
        return;
    }
    case 0x0047b454u: sc->eax = (sc->eax & 0xffffff00u) | 1u; return;
    default: sc->refused = 1; sc->reason = "no memory stream method for this VA"; return;
    }
}
/* The weapon shape of a mount 0x40fab0(W, i) (listing 0x40fab0..0x40fb2a;
 * from weapon_ctor): k = the signed word W+0x22+2i; the memory stream M on
 * the cached bytes ([0x4a99f4 + 4k], [0x4a9904 + 4k]); X = 0x474c10(M);
 * [X+0x26] <- 0x4a9ae8; counter - 1; 0x47a8a4(M, 0); EAX X. */
void es2_sim_service_mount_shape(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t head = fs0_head(), top = c.l->stack_hi, f = frame_push(&c, 0x34u), x = 0;
    if (!f) { refuse(&c, 0x0040fab0u, "mount shape", "the own code's call area is exhausted"); failed(sc); return; }
    f += 0x34u;
    uint32_t m = f - 0x14u, k = sx16(r16(&c, sc->slots[0] + 2u * sx16((uint16_t)sc->slots[1]) + 0x22u));
    memory_stream(&c, m, r32(&c, 0x004a99f4u + 4u * k), r32(&c, 0x004a9904u + 4u * k));
    int bad = CALL(&c, 0x00474c10u, &x, m);
    if (!bad) {
        w32(&c, x + 0x26u, 0x004a9ae8u);
        counter(&c, -1);
        bad = stream_base_dtor(&c, m, 0);
    }
    frame_pop(&c, top);
    if (bad) { failed(sc); return; }
    sc->eax = x; sc->edx = head;
}

/* The mount offset walk 0x40dd4c(P, off) (listing 0x40dd4c..0x40ded7;
 * from weapon_ctor): by P's class tag (slot +0): the lists 0x140007,
 * 0x140008, 0x14000b, 0x14000c, 0x140015 walk their children (word +0x10,
 * array +0x12); the groups 0x14000a and 0x140014 clear the flag bytes
 * 0x4a986c for their zx word +0x22 vertices, mark every vertex the polygons
 * of tags 0x140001..0x140003, 0x140009 and 0x14000f use (word +6 and the
 * word +8 entries of the index list [P+0x10] from word +0xa), and add the
 * three words off[0..2] to each marked vertex of the 6-byte array
 * [P+0x14]; tag 0x140006 recurses on itself (no such class is registered);
 * every other tag returns. */
static int mount_offset(ctx *c, uint32_t p, uint32_t off) {
    uint32_t t = 0;
    if (vcall(c, p, 0u, (const uint32_t[]){p}, 1, &t)) return 1;
    int list = t == 0x00140007u || t == 0x00140008u || t == 0x0014000bu || t == 0x0014000cu || t == 0x00140015u;
    int group = t == 0x0014000au || t == 0x00140014u;
    if (t == 0x00140006u) return refuse(c, 0x0040dea0u, "mount offset", "tag 0x140006 recurses without end");
    if (list) {
        for (int16_t i = 0; (int32_t)i < (int32_t)r16(c, p + 0x10u); ++i)
            if (mount_offset(c, r32(c, r32(c, p + 0x12u) + 4u * (uint32_t)(int32_t)i), off)) return 1;
        return 0;
    }
    if (!group) return 0;
    const uint32_t flags = 0x004a986cu;
    if (CALL(c, VA_MEMSET, NULL, flags, 0u, (uint32_t)r16(c, p + 0x22u))) return 1;
    for (uint32_t i = 0; i < r16(c, p + 0x26u); ++i) {
        uint32_t item = r32(c, r32(c, p + 0x1cu) + 4u * i), tag = 0;
        if (vcall(c, item, 0u, (const uint32_t[]){item}, 1, &tag)) return 1;
        if (!(tag - 0x00140001u < 3u || tag == 0x00140009u || tag == 0x0014000fu)) continue;
        w8(c, flags + r16(c, item + 6u), 1);
        for (int16_t k = 0; (int32_t)k < (int32_t)r16(c, item + 8u); ++k) {
            uint32_t idx = (uint32_t)r16(c, item + 0xau) + (uint32_t)(int32_t)k;
            w8(c, flags + sx16(r16(c, r32(c, p + 0x10u) + 2u * idx)), 1);
        }
    }
    uint32_t v = r32(c, p + 0x14u);
    for (uint32_t i = 0; i < r16(c, p + 0x22u); ++i, v += 6u) {
        if (!r8(c, flags + i)) continue;
        w16(c, v + 4u, (uint16_t)(r16(c, v + 4u) + r16(c, off + 4u)));
        w16(c, v, (uint16_t)(r16(c, v) + r16(c, off)));
        w16(c, v + 2u, (uint16_t)(r16(c, v + 2u) + r16(c, off + 2u)));
    }
    return 0;
}
void es2_sim_service_mount_offset(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    if (mount_offset(&c, sc->slots[0], sc->slots[1])) failed(sc);
}

/* The weapon volume copy 0x40c834(D, S, off) (listing 0x40c834..0x40c8bd)
 * and the volume match 0x40ce28(V, list, offs, n) (0x40ce28..0x40ce75;
 * only caller collision_match): word D <- word S = k; k nonzero: [D+2] <-
 * 0x49667c(0, 8, k, 1, 0x40c8be) and the k 8-byte spheres of [S+2] copied
 * with the offset's three words added to their first three. The match:
 * the first i < n (signed word) whose [list[i]]'s word +6 equals V's word
 * +6 gets 0x40c834(V, list[i], offs[i]); EAX (i < n). */
static int volume_copy(ctx *c, uint32_t d, uint32_t s, uint32_t off) {
    uint16_t k = r16(c, s);
    w16(c, d, k);
    if (!k) return 0;
    uint32_t a = 0;
    if (CALL(c, 0x0049667cu, &a, 0u, 8u, sx16(k), 1u, 0x0040c8beu)) return 1;
    w32(c, d + 2u, a);
    uint32_t src = r32(c, s + 2u);
    for (int16_t i = 0; (int16_t)r16(c, d) > i; ++i, src += 8u, a += 8u) {
        w32(c, a, r32(c, src));
        w32(c, a + 4u, r32(c, src + 4u));
        w16(c, a + 4u, (uint16_t)(r16(c, a + 4u) + r16(c, off + 4u)));
        w16(c, a, (uint16_t)(r16(c, a) + r16(c, off)));
        w16(c, a + 2u, (uint16_t)(r16(c, a + 2u) + r16(c, off + 2u)));
    }
    return 0;
}
void es2_sim_service_volume_copy(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    if (volume_copy(&c, sc->slots[0], sc->slots[1], sc->slots[2])) failed(sc);
}
void es2_sim_service_volume_match(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t v = sc->slots[0], list = sc->slots[1], offs = sc->slots[2];
    int32_t n = (int32_t)sx16((uint16_t)sc->slots[3]), i = 0;
    for (; i < n; ++i, list += 4u, offs += 4u) {
        if (r16(&c, v + 6u) != r16(&c, r32(&c, list) + 6u)) continue;
        if (volume_copy(&c, v, r32(&c, list), r32(&c, offs))) { failed(sc); return; }
        break;
    }
    sc->eax = i < n ? 1u : 0u;
}



/* The rest of weapon_fit's guest callees (listings 0x4205cc..0x420633,
 * 0x411d64..0x411dd1).
 * 0x4205cc(t, k): the static triple 0x49a430 (cleared once, byte
 * 0x49a436); words +0, +2 <- the two words at [0x4a9d6c] + 0x1c t + 4k - 4;
 * EAX 0x49a430.
 * 0x411d64(A, v, out): M = [[[A+0x45]+0xc]] + 0xc; its matrix M+6 built by
 * euler_matrix(M, M+6) once (word M+0x26 <- 1); rotate_2d(v, M+6, local);
 * out += (local x, local y, [v+8]). */
void es2_sim_service_type_pair(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    if (!r8(&c, 0x0049a436u)) {
        w16(&c, 0x0049a430u, 0); w16(&c, 0x0049a432u, 0); w16(&c, 0x0049a434u, 0);
        w8(&c, 0x0049a436u, (uint8_t)(r8(&c, 0x0049a436u) + 1u));
    }
    uint32_t p = sx16((uint16_t)sc->slots[0]) * 0x1cu + r32(&c, 0x004a9d6cu) + sx16((uint16_t)sc->slots[1]) * 4u - 4u;
    w16(&c, 0x0049a430u, r16(&c, p));
    w16(&c, 0x0049a432u, r16(&c, p + 2u));
    sc->eax = 0x0049a430u; sc->edx = (sc->edx & 0xffff0000u) | r16(&c, p); sc->ecx = sx16((uint16_t)sc->slots[1]) * 4u;
}
void es2_sim_service_mount_place(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t a = sc->slots[0], v = sc->slots[1], out = sc->slots[2];
    uint32_t m = r32(&c, r32(&c, r32(&c, a + 0x45u) + 0xcu)) + 0xcu, d = r32(&c, v + 8u);
    if (!r16(&c, m + 0x26u)) {
        if (CALL(&c, 0x0047ebb8u, NULL, m, m + 6u)) { failed(sc); return; }
        w16(&c, m + 0x26u, 1);
    }
    uint32_t top = c.l->stack_hi, f = frame_push(&c, 0x10u);
    if (!f) { refuse(&c, 0x00411d64u, "mount place", "the own code's call area is exhausted"); failed(sc); return; }
    int bad = CALL(&c, 0x0047ffe8u, NULL, v, m + 6u, f);
    if (!bad) {
        w32(&c, out + 8u, r32(&c, out + 8u) + d);
        w32(&c, out, r32(&c, out) + r32(&c, f));
        w32(&c, out + 4u, r32(&c, out + 4u) + r32(&c, f + 4u));
    }
    frame_pop(&c, top);
    if (bad) failed(sc);
}


/* The weapon shape cache release 0x40fa88 (listing 0x40fa88..0x40faae):
 * for i < [0x4a9ae4] (signed): delete([0x4a9904 + 4i]) (operator delete
 * of a new[] block, as the original), the slot <- 0. EDX 0. */
void es2_sim_service_weapon_shapes_free(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    es2_sim_result r;
    memset(&r, 0, sizeof(r));
    for (int32_t i = 0; i < (int32_t)r32(&c, 0x004a9ae4u); ++i) {
        uint32_t slot = 0x004a9904u + 4u * (uint32_t)i;
        if (call_regs(&c, VA_DELETE, (const uint32_t[]){r32(&c, slot)}, 1, NULL, &r)) { failed(sc); return; }
        w32(&c, slot, 0);
    }
    sc->eax = r.eax; sc->edx = 0; sc->ecx = r.ecx;
}

/* ---- the terrain shading pass and the root's last callees */

/* The cell of the terrain grid G at index idx as 0x46bfdc and 0x46c2e0
 * compute it (listing 0x46bff1..0x46c028 and its three repeats): NULL for
 * a negative index or one >= 1 << (([G+0x100] + [G+0x104]) & 31) (signed),
 * else [G+0xec] + 16 idx. */
static uint32_t terrain_cell(const ctx *c, uint32_t g, uint32_t idx) {
    uint32_t n = 1u << ((r32(c, g + 0x100u) + r32(c, g + 0x104u)) & 31u);
    if ((int32_t)idx < 0 || (int32_t)idx >= (int32_t)n) return 0;
    return r32(c, g + 0xecu) + (idx << 4);
}
static void shl_word(const ctx *c, uint32_t a) { w16(c, a, (uint16_t)(r16(c, a) << 1)); }
/* 0x46bfdc(G, x, y) (listing 0x46bfdc..0x46c238): the two triangle normals
 * of cell (x, y) from its four corner heights (the first byte of the cells
 * (x, y) = s, (x, y+1), (x+1, y+1), (x+1, y); shift [G+0x100]):
 *   d = (h00 + h11) - (h01 + h10), the split bits 0..1 of s+0xf:
 *   d = 0: 1; n1 = n2 = (h00 - h10, h00 - h01);
 *   d > 0: 0; n1 = (h00 - h10, h00 - h01), n2 = (h01 - h11, h10 - h11);
 *   d < 0: 2; n1 = (h01 - h11, h00 - h01), n2 = (h00 - h10, h10 - h11);
 * the words s+1/s+3 (n1) and s+7/s+9 (n2) as 16-bit differences; the z
 * words s+5 and s+0xb <- (1 << ([G+0x108] & 31)) idiv [G+0x118]; then
 * every one of the six words shifted left by one. A NULL cell or a
 * faulting idiv crashes the original: refused here. */
static int terrain_slopes(ctx *c, uint32_t g, uint32_t x, uint32_t y) {
    uint32_t sh = r32(c, g + 0x100u) & 31u;
    uint32_t s = terrain_cell(c, g, (y << sh) + x);
    uint32_t p01 = terrain_cell(c, g, ((y + 1u) << sh) + x);
    uint32_t p11 = terrain_cell(c, g, ((y + 1u) << sh) + x + 1u);
    uint32_t p10 = terrain_cell(c, g, (y << sh) + x + 1u);
    if (!s || !p01 || !p11 || !p10) return null_refuse(c, 0x0046bfdcu, "terrain slopes", "a corner outside the grid (the original reads NULL)");
    uint32_t h00 = r8(c, s), h01 = r8(c, p01), h11 = r8(c, p11), h10 = r8(c, p10);
    int32_t d = (int32_t)(h00 + h11) - (int32_t)(h01 + h10);
    uint8_t split = (uint8_t)(r8(c, s + 0xfu) & 0xfcu);
    if (d == 0) {
        w8(c, s + 0xfu, (uint8_t)(split | 1u));
        w16(c, s + 7u, (uint16_t)(h00 - h10)); w16(c, s + 1u, (uint16_t)(h00 - h10));
        w16(c, s + 9u, (uint16_t)(h00 - h01)); w16(c, s + 3u, (uint16_t)(h00 - h01));
    } else if (d > 0) {
        w8(c, s + 0xfu, split);
        w16(c, s + 1u, (uint16_t)(h00 - h10)); w16(c, s + 3u, (uint16_t)(h00 - h01));
        w16(c, s + 7u, (uint16_t)(h01 - h11)); w16(c, s + 9u, (uint16_t)(h10 - h11));
    } else {
        w8(c, s + 0xfu, (uint8_t)(split | 2u));
        w16(c, s + 1u, (uint16_t)(h01 - h11)); w16(c, s + 3u, (uint16_t)(h00 - h01));
        w16(c, s + 7u, (uint16_t)(h00 - h10)); w16(c, s + 9u, (uint16_t)(h10 - h11));
    }
    int32_t num = (int32_t)(1u << (r32(c, g + 0x108u) & 31u)), den = (int32_t)r32(c, g + 0x118u);
    if (den == 0 || (num == INT32_MIN && den == -1))
        return divide_refuse(c, 0x0046c208u, (uint64_t)(int64_t)num, (uint32_t)den, "terrain slopes", "the z division faults in the original");
    uint16_t z = (uint16_t)(num / den);
    w16(c, s + 0xbu, z); w16(c, s + 5u, z);
    shl_word(c, s + 5u); shl_word(c, s + 1u); shl_word(c, s + 3u);
    shl_word(c, s + 0xbu); shl_word(c, s + 7u); shl_word(c, s + 9u);
    return 0;
}
/* 0x46c3f0(G, s, i, j) (listing 0x46c3f0..0x46c49d; 0x46c49f..0x46c4b5 is
 * unreachable): the last row or column (j >= (1 << [G+0x104]) - 1 or i >=
 * (1 << [G+0x100]) - 1, signed) gets the flat normals (0, 0, 0x800) in both
 * triangles (dword s+7 <- dword s+1, word s+0xb <- word s+5); every other
 * cell 0x46bfdc(G, i, j) and the hook normalize(s+1, 0x800), normalize(s+7,
 * 0x800). Then the shade bytes s+0xd <- AL of 0x48c16c(s+1, 0x6b4f5d) and
 * s+0xe <- AL of 0x48c16c(s+7, 0x6b4f5d) (view.c's terrain lighting). */
static int terrain_cell_shade(ctx *c, uint32_t g, uint32_t s, uint32_t i, uint32_t j) {
    uint32_t eax = 0;
    if ((int32_t)j >= (int32_t)((1u << (r32(c, g + 0x104u) & 31u)) - 1u) ||
        (int32_t)i >= (int32_t)((1u << (r32(c, g + 0x100u) & 31u)) - 1u)) {
        w16(c, s + 1u, 0); w16(c, s + 3u, 0); w16(c, s + 5u, 0x800);
        w32(c, s + 7u, r32(c, s + 1u));
        w16(c, s + 0xbu, r16(c, s + 5u));
    } else {
        if (terrain_slopes(c, g, i, j)) return 1;
        for (uint32_t v = s + 1u; v <= s + 7u; v += 6u) {
            /* normalize's idiv 0x46c29e divides 0x800 << 16 by the 0x47de72
             * length of the three words, 0 only for the zero vector (a zone
             * DAT whose fourth dword exceeds 1 << its third, on a flat cell);
             * the registry's normalize divides on the host instead. */
            if (!r16(c, v) && !r16(c, v + 2u) && !r16(c, v + 4u))
                return divide_refuse(c, 0x0046c29eu, 0x08000000u, 0, "terrain normalize", "a zero normal divides by zero in the original");
            if (CALL(c, 0x0046c23cu, NULL, v, 0x800u)) return 1;
        }
    }
    if (CALL(c, 0x0048c16cu, &eax, s + 1u, 0x006b4f5du)) return 1;
    w8(c, s + 0xdu, (uint8_t)eax);
    if (CALL(c, 0x0048c16cu, &eax, s + 7u, 0x006b4f5du)) return 1;
    w8(c, s + 0xeu, (uint8_t)eax);
    return 0;
}
/* The terrain shading pass 0x46c2e0(G) (listing 0x46c2e0..0x46c3ed; from
 * base_flatten): 0x48c444(0) (view.c's object select: the identity object
 * matrix), then for j < 1 << [G+0x104] and i < 1 << [G+0x100] (signed,
 * both bounds re-read every iteration) 0x46c3f0(G, s, i, j) with s the
 * cell of row j (NULL for an index outside the grid; the original then
 * faults in 0x46c3f0, refused here) plus 16 i; then the same loops over
 * the empty 0x46c4bc (no effect, not called). Registers as the original
 * leaves them: EAX 1 << [G+0x104]; ECX the second loop's row counter
 * (popped from its slot at 0x46c3eb): 1 << [G+0x104] when that is
 * positive, else 0; EDX 1 << [G+0x100] when the second loop ran at least
 * one row, else 0. */
void es2_sim_service_terrain_shading(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t g = sc->slots[0];
    if (CALL(&c, 0x0048c444u, NULL, 0u)) { failed(sc); return; }
    for (uint32_t j = 0; (int32_t)(1u << (r32(&c, g + 0x104u) & 31u)) > (int32_t)j; ++j) {
        uint32_t s = terrain_cell(&c, g, j << (r32(&c, g + 0x100u) & 31u)), row = s;
        for (uint32_t i = 0; (int32_t)i < (int32_t)(1u << (r32(&c, g + 0x100u) & 31u)); ++i, s += 16u) {
            if (!row) { null_refuse(&c, 0x0046c3f0u, "terrain shading", "a row outside the grid (the original writes near NULL)"); failed(sc); return; }
            if (terrain_cell_shade(&c, g, s, i, j)) { failed(sc); return; }
        }
    }
    sc->eax = 1u << (r32(&c, g + 0x104u) & 31u);
    int ran = (int32_t)sc->eax > 0;
    sc->ecx = ran ? sc->eax : 0u;
    sc->edx = ran ? 1u << (r32(&c, g + 0x100u) & 31u) : 0u;
}

/* 0x433f7c (listing 0x433f7c..0x43400f; the root at 0x461ecd and the
 * transition 0x42a9c0): one step of the cockpit H = [0x49b088]. H+0x232 ==
 * [H+0x226]: [H+0x22a] <- H+0x236; EAX H, EDX H+0x236. Otherwise [H+0x226]
 * <- H+0x236, P = [H+0x236], and the empty 0x48992e (word [0x4d2566]
 * nonzero) or 0x486e70 (zero) is called with (q, P+8), q = [P+4] + 4 if
 * [P+4] is nonzero, else 0; both preserve every register, so only the
 * registers the original left on the way matter:
 *   0x48992e, [P+4] nonzero: EAX H, EDX P, ECX q;
 *   0x48992e, [P+4] zero:    EAX P, EDX P+8, ECX 0;
 *   0x486e70:                EAX q, EDX P+8, ECX q. */
void es2_sim_service_cockpit_step(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t h = r32(&c, 0x0049b088u);
    if (h + 0x232u == r32(&c, h + 0x226u)) {
        w32(&c, h + 0x22au, h + 0x236u);
        sc->eax = h; sc->edx = h + 0x236u;
        return;
    }
    w32(&c, h + 0x226u, h + 0x236u);
    uint32_t p = r32(&c, h + 0x236u), link = r32(&c, p + 4u), q = link ? link + 4u : 0u;
    if (r16(&c, 0x004d2566u)) {
        if (link) { sc->eax = h; sc->edx = p; }
        else { sc->eax = p; sc->edx = p + 8u; }
        sc->ecx = q;
    } else {
        sc->eax = q; sc->edx = p + 8u; sc->ecx = q;
    }
}

/* 0x465618(sprite) (listing 0x465618..0x465624; the root below 640
 * pixels): [0x4d3724] <- sprite (the cursor sprite the present 0x465628
 * draws); EAX sprite, ECX and EDX unchanged. */
void es2_sim_service_cursor_sprite_set(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    w32(&c, 0x004d3724u, sc->slots[0]);
    sc->eax = sc->slots[0];
}

/* 0x4955c4 (listing 0x4955c4..0x4955d3, 0x49571c..0x495748,
 * 0x4956e4..0x4956f4; the root with -m, byte 0x4d267d): 0x49571c(0x6cbc50,
 * 0x20): with the DARKMONO.VXD handle [0x6cbc50] = -1 (CreateFileA failed,
 * own_process.md record 64) nothing happens: EAX 0 (0x4956e4's answer),
 * ECX 0x6cbc50, EDX unchanged. Another handle would be passed to
 * DeviceIoControl(h, 5, &0x20, 1, 0, 0, 0, 0): the own process opens no
 * VxD, so that path is refused. */
void es2_sim_service_mono_out(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    if (r32(&c, 0x006cbc50u) != 0xffffffffu) { refuse(&c, 0x00495741u, "mono out", "DeviceIoControl on the DARKMONO.VXD handle (no VxD in the own process)"); failed(sc); return; }
    sc->eax = 0; sc->ecx = 0x006cbc50u;
}
