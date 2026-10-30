/* Helpers shared by src/hud_load.c and src/hud_state.c (the guest view, calls
 * through the dispatch over the bound loop's call area, the coupling calls with
 * their trace). Private to the HUD files and src/dialogs.c. */
#ifndef ES2_SIM_HUD_INTERNAL_H
#define ES2_SIM_HUD_INTERNAL_H
#include <string.h>
#include "es2/sim/hud.h"
#include "es2/sim/memory.h"
#include "es2/sim/services.h"

#define HUD_VA_COUNTER_POINTER 0x006cbee4u
#define HUD_VA_CLOCK 0x00467828u
#define HUD_VA_RNG_NEXT 0x00492ee0u
#define HUD_VA_RNG_BOUNDED 0x00492f24u
#define HUD_VA_STREAM_B 0x004d260bu
#define HUD_VA_NEW 0x0047749cu
#define HUD_VA_NEW_ARRAY 0x004774f0u
#define HUD_VA_DELETE 0x00477548u
#define HUD_VA_DELETE_ARRAY 0x0047758cu
#define HUD_VA_MEMSET 0x0049681au
#define HUD_VA_STRNCPY 0x00496760u
#define HUD_VA_VECTOR_NEW 0x0049667cu
#define HUD_VA_EMPTY_CTOR 0x0049545bu

typedef struct hud_ctx { es2_sim_hud *h; es2_sim_loop *l; const es2_guest_memory *m; } hud_ctx;

static inline uint32_t hr32(const hud_ctx *c, uint32_t a) { return c->m->read32(c->m->context, a); }
static inline uint16_t hr16(const hud_ctx *c, uint32_t a) { return c->m->read16(c->m->context, a); }
static inline uint8_t hr8(const hud_ctx *c, uint32_t a) { return es2_sim_read8(c->m, a); }
static inline void hw32(const hud_ctx *c, uint32_t a, uint32_t v) { c->m->write32(c->m->context, a, v); }
static inline void hw16(const hud_ctx *c, uint32_t a, uint16_t v) { c->m->write16(c->m->context, a, v); }
static inline void hw8(const hud_ctx *c, uint32_t a, uint8_t v) { es2_sim_write8(c->m, a, v); }
static inline uint32_t hsx16(uint16_t v) { return (uint32_t)(int32_t)(int16_t)v; }
static inline uint32_t hsx8(uint8_t v) { return (uint32_t)(int32_t)(int8_t)v; }
static inline void hud_counter(const hud_ctx *c, int32_t n) {
    uint32_t p = hr32(c, HUD_VA_COUNTER_POINTER);
    hw32(c, p, hr32(c, p) + (uint32_t)n);
}
static inline int hud_refuse(hud_ctx *c, uint32_t va, const char *name, const char *reason) {
    c->h->refused_va = va;
    return es2_sim_loop_refuse(c->l, va, name, reason);
}
/* D: the original's IDIV of EDX:EAX = dividend by the divisor faults at va
 * (es2_sim_loop_divide_refuse). */
static inline int hud_divide_refuse(hud_ctx *c, uint32_t va, uint64_t dividend, uint32_t divisor, const char *name,
                                    const char *reason) {
    c->h->refused_va = va;
    return es2_sim_loop_divide_refuse(c->l, va, dividend, divisor, 1, name, reason);
}
/* The original reads, writes or calls through NULL at va
 * (es2_sim_loop_null_refuse). */
static inline int hud_null_refuse(hud_ctx *c, uint32_t va, const char *name, const char *reason) {
    c->h->refused_va = va;
    return es2_sim_loop_null_refuse(c->l, va, name, reason);
}

/* A call through the dispatch with the entry registers (NULL: zero). */
static inline int hud_call_regs(hud_ctx *c, uint32_t va, const uint32_t *slots, unsigned count, const uint32_t regs[3],
                                es2_sim_result *out) {
    es2_sim_loop *l = c->l;
    uint32_t bytes = 4u * (count ? count : 1u), top = l->stack_hi;
    if (top - l->stack_lo < bytes) return hud_refuse(c, va, NULL, "the own code's call area is exhausted");
    l->stack_hi -= bytes;
    for (unsigned i = 0; i < count; ++i) hw32(c, l->stack_hi + 4u * i, slots[i]);
    uint32_t zero[3] = {0, 0, 0};
    es2_sim_result r;
    memset(&r, 0, sizeof(r));
    int refused = es2_sim_call(l->dispatch, va, c->m->host(c->m->context, l->stack_hi), count, regs ? regs : zero, &r);
    l->stack_hi = top;
    if (refused) {
        c->h->refused_va = va;
        if (l->outcome == ES2_SIM_LOOP_EXITED) return 1;   /* the process ended inside */
        l->outcome = ES2_SIM_LOOP_REFUSED;
        l->refused_va = va;
        return 1;
    }
    if (out) *out = r;
    return 0;
}
static inline int hud_call(hud_ctx *c, uint32_t va, const uint32_t *slots, unsigned count, uint32_t *eax) {
    es2_sim_result r;
    if (hud_call_regs(c, va, slots, count, NULL, &r)) return 1;
    if (eax) *eax = r.eax;
    return 0;
}
#define HCALL(c, va, eax, ...) hud_call((c), (va), (const uint32_t[]){__VA_ARGS__}, \
    (unsigned)(sizeof((const uint32_t[]){__VA_ARGS__}) / sizeof(uint32_t)), (eax))
#define HCALL0(c, va, eax) hud_call((c), (va), NULL, 0, (eax))
/* A virtual call through slot `offset` of the object's vtable. */
static inline int hud_vcall(hud_ctx *c, uint32_t object, uint32_t offset, const uint32_t *slots, unsigned count,
                            uint32_t *eax) {
    return hud_call(c, hr32(c, hr32(c, object) + offset), slots, count, eax);
}
#define HVCALL(c, obj, off, eax, ...) hud_vcall((c), (obj), (off), (const uint32_t[]){__VA_ARGS__}, \
    (unsigned)(sizeof((const uint32_t[]){__VA_ARGS__}) / sizeof(uint32_t)), (eax))

/* A zero-filled block of the call area for the original's stack locals. */
static inline uint32_t hud_frame_push(hud_ctx *c, uint32_t bytes) {
    bytes = (bytes + 3u) & ~3u;
    if (c->l->stack_hi - c->l->stack_lo < bytes + 0x400u) return 0;
    c->l->stack_hi -= bytes;
    for (uint32_t i = 0; i < bytes; i += 4u) hw32(c, c->l->stack_hi + i, 0);
    return c->l->stack_hi;
}
static inline void hud_frame_pop(hud_ctx *c, uint32_t top) { c->l->stack_hi = top; }

/* ---- the coupling calls (hud.md, "the HUD line contract — coupling set and frame writes"), traced */
static inline void hud_trace(hud_ctx *c, uint8_t kind, uint32_t site, uint32_t target, uint32_t argument, uint32_t result) {
    es2_sim_hud *h = c->h;
    if (h->trace && h->trace_count < h->trace_capacity) {
        es2_sim_hud_event *e = &h->trace[h->trace_count];
        e->kind = kind;
        e->site = site;
        e->target = target;
        e->argument = argument;
        e->result = result;
    }
    ++h->trace_count;
}
/* The clock wrapper 0x467828 called at `site`. */
static inline int hud_clock(hud_ctx *c, uint32_t site, uint32_t *now) {
    uint32_t v = 0;
    if (HCALL0(c, HUD_VA_CLOCK, &v)) return 1;
    hud_trace(c, ES2_SIM_HUD_CLOCK, site, HUD_VA_CLOCK, 0, v);
    if (now) *now = v;
    return 0;
}
/* rng_next(stream B) at `site`. */
static inline int hud_rng_next(hud_ctx *c, uint32_t site, uint32_t *value) {
    uint32_t v = 0;
    if (HCALL(c, HUD_VA_RNG_NEXT, &v, HUD_VA_STREAM_B)) return 1;
    hud_trace(c, ES2_SIM_HUD_STREAM_B, site, HUD_VA_RNG_NEXT, 0, v);
    if (value) *value = v;
    return 0;
}
/* rng_bounded(stream B, n) at `site`. */
static inline int hud_rng_bounded(hud_ctx *c, uint32_t site, uint32_t n, uint32_t *value) {
    uint32_t v = 0;
    if (HCALL(c, HUD_VA_RNG_BOUNDED, &v, HUD_VA_STREAM_B, n)) return 1;
    hud_trace(c, ES2_SIM_HUD_STREAM_B, site, HUD_VA_RNG_BOUNDED, n, v);
    if (value) *value = v;
    return 0;
}
/* A sound-API call (a sound hook or an AUD-area function) at `site`. */
static inline int hud_sound(hud_ctx *c, uint32_t site, uint32_t va, const uint32_t *slots, unsigned count, es2_sim_result *out) {
    es2_sim_result r;
    if (hud_call_regs(c, va, slots, count, NULL, &r)) return 1;
    hud_trace(c, ES2_SIM_HUD_SOUND, site, va, count ? slots[0] : 0, r.eax);
    if (out) *out = r;
    return 0;
}
#define HSOUND(c, site, va, out, ...) hud_sound((c), (site), (va), (const uint32_t[]){__VA_ARGS__}, \
    (unsigned)(sizeof((const uint32_t[]){__VA_ARGS__}) / sizeof(uint32_t)), (out))

/* Shared pieces of src/hud_load.c (each 0, or 1 when refused). */
void hud_bitmap_init(hud_ctx *c, uint32_t o);                  /* 0x48156c */
int hud_gc_construct(hud_ctx *c, uint32_t o);                   /* 0x480c34 */
void hud_gc_select(hud_ctx *c, uint32_t o);                     /* 0x480d44 */
int hud_shape_one(hud_ctx *c, uint32_t out, uint32_t path);     /* 0x453630: one dts_load from a VOL stream */
int hud_sread(hud_ctx *c, uint32_t s, uint32_t n, uint32_t buf, uint32_t *eax);  /* S.read(n, buf): slot +0x18 */
uint32_t hud_text_width(hud_ctx *c, uint32_t font, uint32_t text);  /* 0x4824ec (src/hud_cockpit.c) */
#define HUD_VA_PATH_DIR 0x00492becu
#define HUD_VA_PATH_EXT 0x00492b90u
#define HUD_VA_VOL_STREAM 0x00472f14u
#define HUD_VA_STREAM_OPEN 0x004730e8u
#define HUD_VA_STREAM_READ 0x004735b8u
#define HUD_VA_STREAM_DESTROY 0x00472f64u
#define HUD_VA_DTS_LOAD 0x0047a144u
#define HUD_VA_SHAPE_BY_NAME 0x00474cb0u
#define HUD_VA_LOAD_BY_NAME 0x0045cdf4u
#define HUD_VA_ITOA 0x00496820u
#define HUD_VA_STRCAT 0x004967eau
#define HUD_VA_RTTI_COUNT 0x00496406u
static inline void hud_gstrcpy(const hud_ctx *c, uint32_t d, uint32_t s) {
    uint32_t i = 0;
    uint8_t b;
    do { b = hr8(c, s + i); hw8(c, d + i, b); ++i; } while (b);
}

/* The context of a service row; 1 (and the call refused) without a loop. */
int es2_sim_hud_serving(es2_sim_service_call *sc, hud_ctx *c);
#define HSERVE(sc, c) hud_ctx c; if (es2_sim_hud_serving((sc), &c)) return
static inline void hud_failed(es2_sim_service_call *sc) { sc->refused = 1; sc->reason = "a call inside the service was refused"; }

/* FS:0 of the calling thread (the head a frame record of 0x49630c saves). */
static inline uint32_t hud_fs0(void) {
#if defined(__i386__)
    uint32_t v;
    __asm__ volatile("movl %%fs:0, %0" : "=r"(v));
    return v;
#else
    return 0;
#endif
}

/* src/hud_frame.c: the altitude ladder 0x43dd98(h) with its
 * pixels; `ecx` is the entry ECX (left on the early return), `out` gets
 * EAX, EDX and ECX as the original leaves them. 0, or 1 when refused. */
int es2_sim_hud_altitude_ladder(hud_ctx *c, uint32_t h, uint32_t ecx, es2_sim_result *out);

/* The error report 0x479c78(0x6c56c8, 5, a, file, line, text) as the original
 * calls it at the HUD's sites (own_process.md, "the error report 0x479c78 at
 * the sites that refused"). Severity 5 ends the process inside the report
 * (windowed ExitProcess(1); headless its __exit is refused at 0x49679c); when
 * it returns (a test's recorder) the caller goes on after the call as the
 * original's bytes do. */
#define HUD_VA_FATAL_REPORT 0x00479c78u
#define HUD_VA_REPORT_RECORD 0x006c56c8u
static inline int hud_fatal_report(hud_ctx *c, uint32_t a, uint32_t file, uint32_t line, uint32_t text) {
    return HCALL(c, HUD_VA_FATAL_REPORT, NULL, HUD_VA_REPORT_RECORD, 5u, a, file, line, text);
}

#endif
