#ifndef ES2_ADAPTER_ENTRY_SHIM_H
#define ES2_ADAPTER_ENTRY_SHIM_H
/* The adapters' entry shims as C entries (docs/web_build.md section 4.4).
 * A shim runs before a hook's replacement: it reads entry registers and the
 * caller's stack residue (uninitialized locals the original reads) and either
 * hands them on in registers (the call then continues into the replacement)
 * or runs the replacement itself at the original's stack coordinate, below a
 * guard-probed gap, with a context record (es2_machine_shim,
 * es2/hook/machine.h). The same C entry serves both backends: the proxy's
 * capture (entry_shims_x86.h) fills an es2_machine from the real registers;
 * es2sim's dispatch calls it with the machine of the call
 * (sim/src/machine_sim.c). Guest memory is the identity view in both. */
#include <stdint.h>
#include "es2/hook/machine.h"

typedef struct es2_shim {
    uint32_t rva;            /* the hook's RVA */
    es2_machine_shim entry;
    uint32_t pop;            /* the bytes above the return address the shim's return removes (its RET N) */
    uint32_t bridge;         /* x86: the framework's entry of the hook, set by the capture */
} es2_shim;
#define ES2_SHIM(name, rva, entry, pop) __attribute__((used)) static es2_shim name = {rva, entry, pop, 0}

static inline uint32_t shim_r32(uint32_t a) {
    const volatile uint8_t *p = (const volatile uint8_t *)(uintptr_t)a;
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static inline uint32_t shim_r8(uint32_t a) { return *(const volatile uint8_t *)(uintptr_t)a; }
static inline void shim_w32(uint32_t a, uint32_t v) {
    volatile uint8_t *p = (volatile uint8_t *)(uintptr_t)a;
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
/* The low byte (CL) or the second byte (CH) of a register replaced. */
static inline uint32_t shim_low8(uint32_t r, uint32_t b) { return (r & 0xffffff00u) | (b & 0xffu); }
static inline uint32_t shim_high8(uint32_t r, uint32_t b) { return (r & 0xffff00ffu) | (b & 0xffu) << 8; }
/* MOVB low(%esp),%cl; MOVB high(%esp),%ch at the entry ESP (offsets
 * below it). */
static inline uint32_t shim_cx(const es2_machine *m, uint32_t r, uint32_t low, uint32_t high) {
    return shim_high8(shim_low8(r, shim_r8(m->esp - low)), shim_r8(m->esp - high));
}

/* The return of a shim that ran its replacement: the replacement's
 * registers, EBP from the slot the shim's PUSH EBP saved it in (`saved`; 0:
 * the replacement's), ESP the entry ESP. Returns 1 (es2_machine_shim). */
static inline int shim_return(es2_machine *m, const es2_machine *c, uint32_t entry, uint32_t saved) {
    uint32_t ebp = saved ? shim_r32(saved) : c->ebp;
    *m = *c;
    m->ebp = ebp;
    m->esp = entry;
    return 1;
}

/* A shim's PUSH EBP; MOV EBP,ESP: the caller's EBP saved below the return
 * address; returns the new EBP. */
static inline uint32_t shim_frame(const es2_machine *m) {
    uint32_t ebp = m->esp - 4u;
    shim_w32(ebp, m->ebp);
    return ebp;
}
/* A guard-probed gap: `pages` pages below `top` touched one by one
 * (es2_machine_probe), then the floor (es2_machine_floor); returns the ESP
 * below the gap. */
static inline uint32_t shim_gap(uint32_t bridge, uint32_t top, uint32_t pages) {
    (void)es2_machine_probe(bridge, top, pages * 4096u);
    return es2_machine_floor(top - pages * 4096u);
}
/* The gapped shims of the stack-coordinate adapters: shim_frame, the
 * caller's EBX/ESI/EDI into the original's save slots EBP+save, +save-4,
 * +save-8 (save negative; `saved` of them, from EBX on), the gap below EBP
 * and a context of `size` bytes right below it, starting {EBP, outgoing
 * ESP = the last save slot}. *ebp gets EBP; returns the context's address. */
static inline uint32_t shim_saved_gap_n(const es2_machine *m, uint32_t bridge, uint32_t *ebp, int32_t save,
                                        unsigned saved, uint32_t pages, uint32_t size) {
    const uint32_t frame = shim_frame(m), slot = frame + (uint32_t)save, value[3] = {m->ebx, m->esi, m->edi};
    for (unsigned i = 0; i < saved; ++i) shim_w32(slot - 4u * i, value[i]);
    const uint32_t context = shim_gap(bridge, frame, pages) - size;
    shim_w32(context, frame);
    shim_w32(context + 4u, saved ? slot - 4u * (saved - 1u) : frame);
    *ebp = frame;
    return context;
}
/* shim_saved_gap_n with the context {EBP, outgoing ESP = EBP-top, the
 * caller's EBX, ESI, EDI}. */
static inline uint32_t shim_record_gap(const es2_machine *m, uint32_t bridge, uint32_t *ebp, int32_t save, unsigned saved,
                                       uint32_t pages, uint32_t top) {
    const uint32_t context = shim_saved_gap_n(m, bridge, ebp, save, saved, pages, 20u);
    shim_w32(context + 4u, *ebp - top);
    shim_w32(context + 8u, m->ebx);
    shim_w32(context + 12u, m->esi);
    shim_w32(context + 16u, m->edi);
    return context;
}
/* All three saved, the context {EBP, outgoing ESP}. */
static inline uint32_t shim_saved_gap(const es2_machine *m, uint32_t bridge, uint32_t *ebp, int32_t save, uint32_t pages) {
    return shim_saved_gap_n(m, bridge, ebp, save, 3u, pages, 8u);
}
/* A gapped shim whose adapter takes its context from `pending` while the
 * replacement runs: shim_saved_gap and shim_gap_call with `count` slots. */
#define ES2_SHIM_GAPPED(name, rva, pending, save, pages, count)                                        \
    static int name##_entry(es2_machine *m, uint32_t bridge) {                                        \
        uint32_t ebp;                                                                                  \
        const uint32_t context = shim_saved_gap(m, bridge, &ebp, save, pages);                        \
        pending = (void *)(uintptr_t)context;                                                          \
        const int ran = shim_gap_call(m, bridge, ebp, context, count);                                 \
        pending = NULL;                                                                                \
        return ran;                                                                                    \
    }                                                                                                  \
    ES2_SHIM(name##_shim, rva, name##_entry, 0)
/* The shim's CALL of the replacement with ESP `esp` (the slots `args`
 * pushed below it, args[0] lowest), EBP `ebp` (shim_frame), ECX `ecx` and
 * the other registers of the entry, then MOV ESP,EBP; POP EBP and the return
 * (shim_return). */
static inline int shim_call(es2_machine *m, uint32_t bridge, es2_machine_abi abi, uint32_t ebp, uint32_t esp,
                            uint32_t ecx, const uint32_t *args, unsigned count) {
    es2_machine c = *m;
    c.ecx = ecx;
    c.ebp = ebp;
    c.esp = esp;
    (void)es2_machine_call(&c, bridge, abi, args, count);
    return shim_return(m, &c, ebp + 4u, ebp);
}
/* shim_saved_gap's call: the `count` slots above the return address copied
 * for the replacement below the context; EBX reloaded from its save slot
 * (the caller's), EAX, EDX and ECX as in m. */
static inline int shim_gap_call(es2_machine *m, uint32_t bridge, uint32_t ebp, uint32_t context, unsigned count) {
    uint32_t args[ES2_MACHINE_MAX_ARGS];
    for (unsigned i = 0; i < count; ++i) args[i] = shim_r32(ebp + 8u + 4u * i);
    return shim_call(m, bridge, ES2_MACHINE_CDECL, ebp, context, m->ecx, args, count);
}
/* The exit shims: shim_frame, the `count` slots above the return address
 * copied, the replacement called at ESP = EBP, then the ECX the adapter left
 * in *ecx if it ran (*valid; a fallback to the original keeps its own). */
static inline int shim_exit_call(es2_machine *m, uint32_t bridge, unsigned count, const uint32_t *ecx, uint32_t *valid) {
    const uint32_t ebp = shim_frame(m);
    uint32_t args[ES2_MACHINE_MAX_ARGS];
    for (unsigned i = 0; i < count; ++i) args[i] = shim_r32(ebp + 8u + 4u * i);
    es2_machine c = *m;
    c.ebp = ebp;
    c.esp = ebp;
    *valid = 0;
    (void)es2_machine_call(&c, bridge, ES2_MACHINE_CDECL, args, count);
    if (*valid) { c.ecx = *ecx; *valid = 0; }
    return shim_return(m, &c, ebp + 4u, ebp);
}
#endif
