/* The runtime services of sim/dispatch/services.h: the pure CW3220 imports,
 * the heap imports and the game's allocator over them, the frame-record
 * set-up and GetTickCount. Each states the registers it leaves and where
 * that was read; "listing" = llvm-objdump -d -M intel of the reference
 * DBSIM.EXE (SHA-256 8561aa47...c4ad). */
#include "es2/sim/services.h"
#include "es2/sim/loop.h"
#include "es2/sim/memory.h"
#include <stddef.h>

/* Declarations of every row's function. */
#define ES2_SIM_SERVICE(name, va, abi, result, slots, function) void function(es2_sim_service_call *call, void *user);
#include "sim/dispatch/services.h"
#undef ES2_SIM_SERVICE

#define ES2_SIM_POP_CDECL(slots) 0u
#define ES2_SIM_POP_STDCALL(slots) (4u * (slots))

static const struct {
    const char *name;
    uint32_t va;
    uint16_t pop;
    uint8_t slots;
    es2_sim_service_fn function;
} rows[] = {
#define ES2_SIM_SERVICE(name, va, abi, result, slots, function) {#name, va, ES2_SIM_POP_##abi(slots), slots, function},
#include "sim/dispatch/services.h"
#undef ES2_SIM_SERVICE
};

unsigned es2_sim_service_count(void) { return (unsigned)(sizeof(rows) / sizeof(rows[0])); }

const char *es2_sim_service_row(unsigned i, uint32_t *va) {
    if (i >= es2_sim_service_count()) return NULL;
    if (va) *va = rows[i].va;
    return rows[i].name;
}

int es2_sim_register_services(es2_sim_dispatch *d, es2_sim_services *s) {
    es2_sim_rtl_memory(s->memory, &s->rtl);
    for (unsigned i = 0; i < es2_sim_service_count(); ++i)
        if (es2_sim_dispatch_add_service(d, rows[i].va, rows[i].name, rows[i].function, s, rows[i].pop, rows[i].slots))
            return 1;
    return 0;
}

static void refuse(es2_sim_service_call *call, const char *reason) { call->refused = 1; call->reason = reason; }
static uint32_t rd32(const es2_sim_services *s, uint32_t a) { return s->memory->read32(s->memory->context, a); }
static void wr32(const es2_sim_services *s, uint32_t a, uint32_t v) { s->memory->write32(s->memory->context, a, v); }

/* ---- the 20 pure imports: core/src/rtl.c (confirmed in vitro against CW3220),
 * arguments read at the caller's ESP + 4, registers as CW3220 leaves them.
 * sprintf/vsprintf refuse what the model does not cover. */
static void rtl_service(es2_sim_service_call *call, void *user, es2_rtl_function f) {
    es2_sim_services *s = user;
    es2_rtl_regs r = {call->eax, call->ecx, call->edx};
    if (es2_rtl_call(f, &s->rtl, call->stack, &r) != ES2_RTL_DONE) {
        refuse(call, "format not modelled (floating point, %p, %n or an empty result)");
        return;
    }
    call->eax = r.eax; call->ecx = r.ecx; call->edx = r.edx;
}
#define RTL_SERVICE(lower, upper) \
    void es2_sim_service_##lower(es2_sim_service_call *call, void *user) { rtl_service(call, user, ES2_RTL_##upper); }
RTL_SERVICE(memset, MEMSET)
RTL_SERVICE(memcpy, MEMCPY)
RTL_SERVICE(memmove, MEMMOVE)
RTL_SERVICE(memcmp, MEMCMP)
RTL_SERVICE(strlen, STRLEN)
RTL_SERVICE(strcat, STRCAT)
RTL_SERVICE(strncat, STRNCAT)
RTL_SERVICE(strncpy, STRNCPY)
RTL_SERVICE(strchr, STRCHR)
RTL_SERVICE(strrchr, STRRCHR)
RTL_SERVICE(strstr, STRSTR)
RTL_SERVICE(strncmp, STRNCMP)
RTL_SERVICE(stricmp, STRICMP)
RTL_SERVICE(strnicmp, STRNICMP)
RTL_SERVICE(strupr, STRUPR)
RTL_SERVICE(strlwr, STRLWR)
RTL_SERVICE(itoa, ITOA)
RTL_SERVICE(atol, ATOL)
RTL_SERVICE(sprintf, SPRINTF)
RTL_SERVICE(vsprintf, VSPRINTF)

/* ---- the heap imports (es2/sim/heap.h). */
void es2_sim_service_calloc(es2_sim_service_call *call, void *user) {
    es2_sim_services *s = user;
    if (!s->heap) { refuse(call, "no heap"); return; }
    es2_rtl_regs r = {call->eax, call->ecx, call->edx};
    s->heap->caller = call->return_address;   /* the heap trace's caller */
    es2_sim_heap_calloc(s->heap, call->slots[0], call->slots[1], &r);
    call->eax = r.eax; call->ecx = r.ecx; call->edx = r.edx;
}

void es2_sim_service_free(es2_sim_service_call *call, void *user) {
    es2_sim_services *s = user;
    if (!s->heap) { refuse(call, "no heap"); return; }
    es2_rtl_regs r = {call->eax, call->ecx, call->edx};
    s->heap->caller = call->return_address;
    if (es2_sim_heap_free(s->heap, call->slots[0], &r)) { refuse(call, "free of a pointer outside the own heap"); return; }
    call->eax = r.eax; call->ecx = r.ecx; call->edx = r.edx;
}

void es2_sim_service_realloc(es2_sim_service_call *call, void *user) {
    es2_sim_services *s = user;
    if (!s->heap) { refuse(call, "no heap"); return; }
    es2_rtl_regs r = {call->eax, call->ecx, call->edx};
    s->heap->caller = call->return_address;
    if (es2_sim_heap_realloc(s->heap, call->slots[0], call->slots[1], &r)) { refuse(call, "realloc of a pointer outside the own heap"); return; }
    call->eax = r.eax; call->ecx = r.ecx; call->edx = r.edx;
}

/* ---- the game's allocator (rtl_imports.md 4.3; listing 0x47747c..0x4775cc). */

/* 0x47747c(n): EAX = n is pushed with 1 for _calloc(1, n) (0x47747f..0x477485);
 * when p != 0, EDX = [p-4] is added to [0x6bb30c] (0x477491..0x477494).
 * Leaves EAX = p, ECX = calloc's ECX, EDX = [p-4] or calloc's EDX. */
static int game_calloc(es2_sim_services *s, uint32_t n, es2_rtl_regs *r) {
    if (!s->heap) return 1;
    r->eax = n;
    s->heap->caller = 0x0047748au;   /* the return address of the _calloc call 0x477485 */
    uint32_t p = es2_sim_heap_calloc(s->heap, 1, n, r);
    if (p) {
        r->edx = rd32(s, p - 4u);
        wr32(s, ES2_SIM_VA_HEAP_BYTES, rd32(s, ES2_SIM_VA_HEAP_BYTES) + r->edx);
    }
    return 0;
}

void es2_sim_service_game_calloc(es2_sim_service_call *call, void *user) {
    es2_rtl_regs r = {call->eax, call->ecx, call->edx};
    if (game_calloc(user, call->slots[0], &r)) { refuse(call, "no heap"); return; }
    call->eax = r.eax; call->ecx = r.ecx; call->edx = r.edx;
}

/* new (0x47749c) and new[] (0x4774f0, n + 8 at 0x4774f8): an arena block when
 * n < [0x6bb304] and the arena has room (0x4774a4..0x4774dd); with the arena
 * off [0x6bb304] = 0, `jae` is always taken and 0x47747c(n) runs; its
 * `pop ecx` (0x4774e5) leaves ECX = n. The arena (-l, below 12 MB) is not
 * modelled: refused. */
static void new_service(es2_sim_service_call *call, es2_sim_services *s, uint32_t n) {
    if (n < rd32(s, ES2_SIM_VA_ARENA_LIMIT)) { refuse(call, "arena block (-l arena not modelled)"); return; }
    es2_rtl_regs r = {call->eax, call->ecx, call->edx};
    if (game_calloc(s, n, &r)) { refuse(call, "no heap"); return; }
    call->eax = r.eax; call->ecx = n; call->edx = r.edx;
}
void es2_sim_service_new(es2_sim_service_call *call, void *user) { new_service(call, user, call->slots[0]); }
void es2_sim_service_new_array(es2_sim_service_call *call, void *user) { new_service(call, user, call->slots[0] + 8u); }

/* delete (0x477548) and delete[] (0x47758c): 0x476e2c(0x6bb2f8, p) tests for
 * an arena block; with the arena header zero it returns EAX = 0 with
 * ECX = EDX = p for every p (0x476e2c..0x476e59). p = 0 then returns with
 * EAX = ECX = EDX = 0. Otherwise EAX = [p-4] is subtracted from [0x6bb30c]
 * (0x477576..0x47757a), _free(p) runs with EAX = [p-4], ECX = EDX = p, and
 * its `pop ecx` (0x477585) leaves ECX = p; EAX/EDX are _free's. A nonzero
 * arena header is refused (not modelled). */
static void delete_service(es2_sim_service_call *call, es2_sim_services *s, uint32_t caller) {
    uint32_t p = call->slots[0];
    if (rd32(s, ES2_SIM_VA_ARENA) || rd32(s, ES2_SIM_VA_ARENA + 4u)) { refuse(call, "arena set up (-l arena not modelled)"); return; }
    if (!p) { call->eax = 0; call->ecx = 0; call->edx = 0; return; }
    if (!s->heap) { refuse(call, "no heap"); return; }
    if (!es2_sim_heap_owns(s->heap, p)) { refuse(call, "delete of a pointer outside the own heap"); return; }
    uint32_t header = rd32(s, p - 4u);
    wr32(s, ES2_SIM_VA_HEAP_BYTES, rd32(s, ES2_SIM_VA_HEAP_BYTES) - header);
    es2_rtl_regs r = {header, p, p};
    s->heap->caller = caller;
    es2_sim_heap_free(s->heap, p, &r);
    call->eax = r.eax; call->ecx = p; call->edx = r.edx;
}
/* The return addresses of their _free calls (0x477580, 0x4775c4). */
void es2_sim_service_delete(es2_sim_service_call *call, void *user) { delete_service(call, user, 0x00477585u); }
void es2_sim_service_delete_array(es2_sim_service_call *call, void *user) { delete_service(call, user, 0x004775c9u); }

/* ---- 0x49630c, the frame-record set-up (listing 0x49630c..0x496349;
 * rtl_imports.md 4.2): EAX = the caller's descriptor table, the record r =
 * EBP + [EAX+4]: [r+8] = EAX, [r+0xc] = the caller's ESP after the call,
 * [r+4] = 0x4967f0, dwords [r+0x10] and [r+0x12] = 0, [r+0x1c] = the
 * construction counter [[0x6cbee4]], [r] = FS:0, FS:0 = r. Leaves EAX = the
 * previous FS:0, ECX and EDX as they were (EBX is pushed and popped). The
 * record goes on the machine interface's FS:0 head (es2_machine_fs0): in
 * es2sim a variable, never the real FS:0 (web_build.md section 4.5). */
static uint32_t fs0_read(const es2_sim_services *s) {
    return s->fs0 ? *s->fs0 : es2_machine_fs0();
}
static void fs0_write(const es2_sim_services *s, uint32_t v) {
    if (s->fs0) *s->fs0 = v;
    else es2_machine_set_fs0(v);
}
void es2_sim_service_frame_record(es2_sim_service_call *call, void *user) {
    es2_sim_services *s = user;
    if (!call->ebp) { refuse(call, "frame record without the caller's EBP"); return; }
    uint32_t counter = rd32(s, ES2_SIM_VA_COUNTER_POINTER);
    if (!counter) {
        /* 0x496336 `mov eax, [eax]` reads [0]. */
        if (es2_sim_loop_fault(es2_sim_loop_bound(), ES2_SIM_ACCESS_VIOLATION, 0x00496336u))
            refuse(call, "the process ended at the original's access violation");
        else
            refuse(call, "construction counter pointer [0x6cbee4] not set (c0w32 entry)");
        return;
    }
    uint32_t r = call->ebp + rd32(s, call->eax + 4u);
    wr32(s, r + 8u, call->eax);
    wr32(s, r + 0xcu, call->stack);
    wr32(s, r + 4u, ES2_SIM_VA_RECORD_HANDLER);
    wr32(s, r + 0x10u, 0);
    wr32(s, r + 0x12u, 0);
    wr32(s, r + 0x1cu, rd32(s, counter));
    uint32_t previous = fs0_read(s);
    wr32(s, r, previous);
    fs0_write(s, r);
    call->eax = previous;
}

/* ---- GetTickCount through 0x4968ec: virtual time (own_process.md 7). The
 * limiter's call (return address 0x4678d5) is a poll with its previous clock
 * [0x4d3b74]; every other caller reads. ECX/EDX: left as at the call (the
 * three call sites, listing 0x467828..0x4678f5, overwrite EDX and the low
 * word of ECX before reading them; 0x467828 and 0x467883 return to their
 * callers with them, not traced: hypothesis that they are dead there). */
void es2_sim_service_get_tick_count(es2_sim_service_call *call, void *user) {
    es2_sim_services *s = user;
    if (!s->vt || !s->vt->enabled) { refuse(call, "virtual time disabled (the host has no real clock)"); return; }
    if (call->return_address == ES2_SIM_VA_LIMITER_RETURN)
        call->eax = es2_vt_limiter_poll(s->vt, rd32(s, ES2_SIM_VA_LIMITER_PREVIOUS), NULL, NULL);
    else
        call->eax = es2_vt_read(s->vt);
}

/* ======================================================================
 * Runtime and math leftovers (own_process.md, "files, streams, math and runtime
 * leftovers"; rows in sim/dispatch/services/runtime_leaves.h). Every row sets EAX, EDX
 * and ECX as the original leaves them; where a register is written and then
 * restored by the original's pops, the entry value stays. Reads and writes
 * follow the listing's order, so overlapping operands behave as in the
 * original. */
static uint16_t rd16(const es2_sim_services *s, uint32_t a) { return s->memory->read16(s->memory->context, a); }
static int32_t sx16(uint32_t w) { return (int16_t)(uint16_t)w; }
static int32_t sx8(uint32_t b) { return (int8_t)(uint8_t)b; }
static int32_t sd(const es2_sim_services *s, uint32_t a) { return (int32_t)rd32(s, a); }
static uint32_t lo32(int64_t v) { return (uint32_t)(uint64_t)v; }
static uint32_t hi32(int64_t v) { return (uint32_t)((uint64_t)v >> 32); }

/* ---- 0x476df4(hdr, size, base), the arena set-up (listing 0x476df4..
 * 0x476e29): [hdr] = base, [hdr+4] = size, [hdr+8] = base, [base] = 0,
 * [base+4] = size & ~7, [hdr+0x10] = [hdr+0xc] = size & ~7, then the block
 * tag 0x46424c4b at [[hdr+8] + 8]. Leaves EAX = EDX = base, ECX = size. */
void es2_sim_service_arena_init_476df4(es2_sim_service_call *call, void *user) {
    es2_sim_services *s = user;
    uint32_t hdr = call->slots[0], size = call->slots[1], base = call->slots[2];
    wr32(s, hdr, base);
    wr32(s, hdr + 4u, size);
    wr32(s, hdr + 8u, base);
    wr32(s, base, 0);
    wr32(s, base + 4u, size & ~7u);
    wr32(s, hdr + 0x10u, size & ~7u);
    wr32(s, hdr + 0xcu, size & ~7u);
    wr32(s, rd32(s, hdr + 8u) + 8u, 0x46424c4bu);
    call->eax = base;
    call->edx = base;
    call->ecx = size;
}

/* ---- 0x476e2c(hdr, p), the arena block test (listing 0x476e2c..0x476e59):
 * 1 when [hdr] <= p < [hdr] + [hdr+4] (signed) and the dword at p-8 is the
 * tag 0x41424c4b; else 0. EDX = ECX = p. */
void es2_sim_service_arena_test_476e2c(es2_sim_service_call *call, void *user) {
    es2_sim_services *s = user;
    int32_t p = (int32_t)call->slots[1];
    int32_t base = sd(s, call->slots[0]);
    int hit = 0;
    if (!(base > p) && !(p >= (int32_t)((uint32_t)base + rd32(s, call->slots[0] + 4u))))
        hit = rd32(s, (uint32_t)p - 8u) == 0x41424c4bu;
    call->eax = hit ? 1u : 0u;
    call->edx = (uint32_t)p;
    call->ecx = (uint32_t)p;
}

/* ---- 0x46b850(obj): [obj+0x4a] + 0x1e8 (listing 0x46b850..0x46b85f). */
void es2_sim_service_object_field_46b850(es2_sim_service_call *call, void *user) {
    es2_sim_services *s = user;
    call->eax = rd32(s, call->slots[0] + 0x4au) + 0x1e8u;
}

/* ---- 0x47dbca(a, b): the dot product of two word pairs (a[0]*b[0] +
 * a[1]*b[1], 32-bit products). ECX = the first product, EDX = b[1]
 * sign-extended (listing 0x47dbca..0x47dbef). */
void es2_sim_service_dot16_47dbca(es2_sim_service_call *call, void *user) {
    es2_sim_services *s = user;
    uint32_t a = call->slots[0], b = call->slots[1];
    uint32_t first = (uint32_t)sx16(rd16(s, a)) * (uint32_t)sx16(rd16(s, b));
    uint32_t edx = (uint32_t)sx16(rd16(s, b + 2u));
    call->eax = (uint32_t)sx16(rd16(s, a + 2u)) * edx + first;
    call->edx = edx;
    call->ecx = first;
}

/* ---- 0x47dba4(a, b): the same for byte pairs (listing 0x47dba4..0x47dbc9). */
void es2_sim_service_dot8_47dba4(es2_sim_service_call *call, void *user) {
    es2_sim_services *s = user;
    uint32_t a = call->slots[0], b = call->slots[1];
    uint32_t first = (uint32_t)sx8(es2_sim_read8(s->memory, a)) * (uint32_t)sx8(es2_sim_read8(s->memory, b));
    uint32_t edx = (uint32_t)sx8(es2_sim_read8(s->memory, b + 1u));
    call->eax = (uint32_t)sx8(es2_sim_read8(s->memory, a + 1u)) * edx + first;
    call->edx = edx;
    call->ecx = first;
}

/* ---- 0x47db4d(a, b, out): the cross product of two dword triples, each
 * component the low 32 bits of the difference of two signed 64-bit products;
 * returns out. EDX = the high half of the last component's second product
 * (b[1] * a[0]), ECX = a (listing 0x47db4d..0x47dba3). */
void es2_sim_service_cross_47db4d(es2_sim_service_call *call, void *user) {
    es2_sim_services *s = user;
    uint32_t a = call->slots[0], b = call->slots[1], out = call->slots[2];
    int64_t p, q;
    p = (int64_t)sd(s, b + 4u) * sd(s, a + 8u);
    uint32_t esi = lo32(p);
    q = (int64_t)sd(s, b + 8u) * sd(s, a + 4u);
    wr32(s, out, lo32(q) - esi);
    p = (int64_t)sd(s, b + 8u) * sd(s, a);
    esi = lo32(p);
    q = (int64_t)sd(s, b) * sd(s, a + 8u);
    wr32(s, out + 4u, lo32(q) - esi);
    p = (int64_t)sd(s, b) * sd(s, a + 4u);
    esi = lo32(p);
    q = (int64_t)sd(s, b + 4u) * sd(s, a);
    wr32(s, out + 8u, lo32(q) - esi);
    call->eax = out;
    call->edx = hi32(q);
    call->ecx = a;
}

/* ---- 0x47ddcd(a, b): the sign of the dot product of two dword triples
 * (64-bit sum): -1 when the signed sum is negative, else 1 (zero gives 1).
 * The test is the flags of the last ADC (SF != OF), so the 64-bit sum of the
 * first two products wraps as the original's does. ECX = the low half of the
 * first two products' sum, EDX = the high half of the full sum (listing
 * 0x47ddcd..0x47de0b). */
void es2_sim_service_dot3_sign_47ddcd(es2_sim_service_call *call, void *user) {
    es2_sim_services *s = user;
    uint32_t a = call->slots[0], b = call->slots[1];
    int64_t p1 = (int64_t)sd(s, a) * sd(s, b);
    int64_t p2 = (int64_t)sd(s, a + 4u) * sd(s, b + 4u);
    uint64_t partial = (uint64_t)p1 + (uint64_t)p2;
    int64_t p3 = (int64_t)sd(s, a + 8u) * sd(s, b + 8u);
    uint64_t total = partial + (uint64_t)p3;
    int sf = (int)(total >> 63);
    int of = (int)(((partial ^ total) & ((uint64_t)p3 ^ total)) >> 63);
    int negative = sf != of;
    call->eax = negative ? 0xffffffffu : 1u;
    call->edx = (uint32_t)(total >> 32);
    call->ecx = (uint32_t)partial;
}

/* ---- 0x47de2d(out, num, den): out[0] = the signed 64-bit [num] / den,
 * out[1] = the remainder (IDIV); returns out with ECX = out, EDX = the
 * remainder. A zero divisor or a quotient beyond 32 bits is the CPU's divide
 * fault at the IDIV `site` (0x47de3c): the process ends there as the
 * original's (D, es2_sim_loop_divide_fault); without a process record the
 * row refuses it (listing 0x47de2d..0x47de49). */
static int idiv64(es2_sim_service_call *call, uint32_t site, int64_t n, int32_t d, uint32_t *q, uint32_t *r) {
    int faults = d == 0 || (n == INT64_MIN && d == -1);
    int64_t quotient = faults ? 0 : n / d;
    if (faults || quotient > INT32_MAX || quotient < INT32_MIN) {
        if (es2_sim_loop_divide_fault(es2_sim_loop_bound(), site, (uint64_t)n, (uint32_t)d, 1))
            refuse(call, "the process ended at the original's divide fault");
        else
            refuse(call, d == 0 ? "divide fault (zero divisor)" : "divide fault (quotient beyond 32 bits)");
        return 1;
    }
    *q = (uint32_t)(int32_t)quotient;
    *r = (uint32_t)(int32_t)(n % d);
    return 0;
}
void es2_sim_service_idiv64_47de2d(es2_sim_service_call *call, void *user) {
    es2_sim_services *s = user;
    uint32_t out = call->slots[0], num = call->slots[1], q, r;
    int64_t n = (int64_t)(((uint64_t)rd32(s, num + 4u) << 32) | rd32(s, num));
    if (idiv64(call, 0x0047de3cu, n, (int32_t)call->slots[2], &q, &r)) return;
    wr32(s, out, q);
    wr32(s, out + 4u, r);
    call->eax = out;
    call->edx = r;
    call->ecx = out;
}
/* 0x47eabe(out, num, den) passes its arguments to 0x47de2d and returns out:
 * the same registers (listing 0x47eabe..0x47eada). */
void es2_sim_service_idiv64_47eabe(es2_sim_service_call *call, void *user) { es2_sim_service_idiv64_47de2d(call, user); }

/* ---- 0x47df04(p): swaps the words at p+2 and p+4. EAX = p, ECX's low word =
 * the old word at p+4 (listing 0x47df04..0x47df18). */
void es2_sim_service_swap_words_47df04(es2_sim_service_call *call, void *user) {
    es2_sim_services *s = user;
    uint32_t p = call->slots[0];
    uint16_t w1 = rd16(s, p + 2u), w2 = rd16(s, p + 4u);
    s->memory->write16(s->memory->context, p + 4u, w1);
    s->memory->write16(s->memory->context, p + 2u, w2);
    call->eax = p;
    call->ecx = (call->ecx & 0xffff0000u) | w2;
}

/* ---- 0x47df84(out, a, b): the signed 64-bit product a * b stored at out
 * (low dword first); returns out with ECX = out and EDX = the high half.
 * 0x47eaa1 passes its three arguments on and returns out: the same
 * registers (listing 0x47df84..0x47df9b, 0x47eaa1..0x47eabd). */
void es2_sim_service_imul64_47df84(es2_sim_service_call *call, void *user) {
    es2_sim_services *s = user;
    uint32_t out = call->slots[0];
    int64_t p = (int64_t)(int32_t)call->slots[1] * (int32_t)call->slots[2];
    wr32(s, out, lo32(p));
    wr32(s, out + 4u, hi32(p));
    call->eax = out;
    call->edx = hi32(p);
    call->ecx = out;
}
void es2_sim_service_imul64_47eaa1(es2_sim_service_call *call, void *user) { es2_sim_service_imul64_47df84(call, user); }

/* ---- 0x47df9c(n), the integer square root by the shift-and-test loop of
 * listing 0x47df9c..0x47dfed, executed step by step: BSR, halved, the
 * jcxz on CX, then the descent in EBX/EBP/ESI/EDI with ECX counting down
 * (shift counts are CL & 31). Leaves EAX = the root, EDX = n and ECX = the
 * loop's final count; for n = 0 ECX is the entry value. */
static uint32_t isqrt_loop(uint32_t n, uint32_t *ecx_io) {
    uint32_t edx = n, ebx = 0, ecx = *ecx_io;
    if (!edx) return 0;
    ecx = 31u - (uint32_t)__builtin_clz(edx);
    ecx >>= 1;
    ebx = 1u << (ecx & 31u);
    if ((ecx & 0xffffu) == 0) { *ecx_io = ecx; return ebx; }
    uint32_t eax = ebx << (ecx & 31u);
    uint32_t ebp = ebx >> 1;
    ecx -= 1u;
    for (;;) {
        uint32_t esi = (ebx << (ecx & 31u)) << 1;
        for (;;) {
            uint32_t edi = (ebp << (ecx & 31u)) + eax + esi;
            ecx -= 1u;
            if (edi > edx) {
                ebp >>= 1;
                if (!ebp) { *ecx_io = ecx; return ebx; }
                esi >>= 1;
                continue;
            }
            ebx += ebp;
            ebp >>= 1;
            if (!ebp) { *ecx_io = ecx; return ebx; }
            eax = edi;
            break;
        }
    }
}
void es2_sim_service_isqrt_47df9c(es2_sim_service_call *call, void *user) {
    (void)user;
    uint32_t n = call->slots[0], ecx = call->ecx;
    call->eax = isqrt_loop(n, &ecx);
    call->edx = n;
    call->ecx = ecx;
}
/* 0x47ea92(n) calls 0x47df9c and pops its argument: the same EAX and EDX,
 * ECX = n (listing 0x47ea92..0x47eaa0). */
void es2_sim_service_isqrt_47ea92(es2_sim_service_call *call, void *user) {
    es2_sim_service_isqrt_47df9c(call, user);
    call->ecx = call->slots[0];
}
/* 0x47ea64(v): the root of v[0]^2 + v[1]^2 + v[2]^2 (32-bit products),
 * through 0x47ea92. EDX = ECX = the sum (listing 0x47ea64..0x47ea91). */
void es2_sim_service_length_47ea64(es2_sim_service_call *call, void *user) {
    es2_sim_services *s = user;
    uint32_t v = call->slots[0];
    uint32_t x = rd32(s, v), y = rd32(s, v + 4u), z = rd32(s, v + 8u);
    uint32_t sum = x * x + y * y + z * z;
    uint32_t ecx = call->ecx;
    call->eax = isqrt_loop(sum, &ecx);
    call->edx = sum;
    call->ecx = sum;
}

/* ---- 0x47e0c0(a, b): (a * sx16(b)) >> 14 as the low 32 bits of the signed
 * 64-bit product shifted right (SHRD 14). EDX = the product's high half,
 * ECX = sx16(b) (listing 0x47e0c0..0x47e0d2). */
void es2_sim_service_mul_shift_47e0c0(es2_sim_service_call *call, void *user) {
    (void)user;
    int32_t b = sx16(call->slots[1]);
    int64_t p = (int64_t)(int32_t)call->slots[0] * b;
    call->eax = (lo32(p) >> 14) | (hi32(p) << 18);
    call->edx = hi32(p);
    call->ecx = (uint32_t)b;
}

/* ---- 0x480050(m, w, out): two rows of a fixed-point affine map
 * (listing 0x480050..0x4800bc), m = two dwords, w = four words then two
 * dword offsets: out[0] = ((m[0]*w.w0 + m[1]*w.w2 + 0x2000) >> 14) + [w+8],
 * out[1] = ((m[0]*w.w1 + m[1]*w.w3 + 0x2000) >> 14) + [w+0xc] (word
 * indices; 64-bit sums, SHRD 14, low 32 bits). Returns out; ECX = the high
 * half of the second row's first product, EDX = the high half of its
 * rounded sum. */
void es2_sim_service_affine_480050(es2_sim_service_call *call, void *user) {
    es2_sim_services *s = user;
    uint32_t m = call->slots[0], w = call->slots[1], out = call->slots[2];
    int64_t first = (int64_t)sd(s, m) * sx16(rd16(s, w));
    uint64_t sum = (uint64_t)first + (uint64_t)((int64_t)sd(s, m + 4u) * sx16(rd16(s, w + 4u))) + 0x2000u;
    wr32(s, out, (uint32_t)(sum >> 14) + rd32(s, w + 8u));
    first = (int64_t)sd(s, m) * sx16(rd16(s, w + 2u));
    sum = (uint64_t)first + (uint64_t)((int64_t)sd(s, m + 4u) * sx16(rd16(s, w + 6u))) + 0x2000u;
    wr32(s, out + 4u, (uint32_t)(sum >> 14) + rd32(s, w + 0xcu));
    call->eax = out;
    call->edx = (uint32_t)(sum >> 32);
    call->ecx = hi32(first);
}

/* ---- the table of 32-bit slots at 0x6c5eb4 (DATA) that 0x480590 and
 * 0x4805b4 fill: 0x480590(dst, i, v) stores 0 at [dst] and sets slot i to v
 * only when it is empty; 0x4805b4(i, v) stores v when v is 0 or the slot is
 * empty and returns 0, else returns AL = 1 with EAX's upper bytes = i
 * (listing 0x480590..0x4805b2, 0x4805b4..0x4805db). */
#define VA_SLOT_TABLE 0x006c5eb4u
void es2_sim_service_slot_init_480590(es2_sim_service_call *call, void *user) {
    es2_sim_services *s = user;
    uint32_t dst = call->slots[0], i = call->slots[1], v = call->slots[2];
    uint32_t ecx = 0, at = VA_SLOT_TABLE + 4u * i;
    wr32(s, dst, 0);
    if (rd32(s, at) == 0) { ecx = v; wr32(s, at, v); }
    call->eax = dst;
    call->edx = i;
    call->ecx = ecx;
}
void es2_sim_service_slot_set_4805b4(es2_sim_service_call *call, void *user) {
    es2_sim_services *s = user;
    uint32_t i = call->slots[0], v = call->slots[1], at = VA_SLOT_TABLE + 4u * i;
    call->edx = v;
    if (v == 0 || rd32(s, at) == 0) { wr32(s, at, v); call->eax = 0; }
    else call->eax = (i & 0xffffff00u) | 1u;
}

/* ---- 0x47eadb(p): two empty constructors on p and p+0xc (each returns its
 * argument); returns p, ECX = p + 0xc (listing 0x47eadb..0x47eaf7,
 * 0x4953c4). */
void es2_sim_service_ctor_pair_47eadb(es2_sim_service_call *call, void *user) {
    (void)user;
    call->eax = call->slots[0];
    call->ecx = call->slots[0] + 0xcu;
}

/* ---- 0x492b04(a, b): the signed quotient a / b in EAX, the remainder in
 * EDX (CDQ, IDIV); a zero divisor or INT_MIN / -1 is the divide fault at
 * 0x492b0b, as at 0x47de3c (listing 0x492b04..0x492b0f). */
void es2_sim_service_idiv_492b04(es2_sim_service_call *call, void *user) {
    (void)user;
    uint32_t q, r;
    if (idiv64(call, 0x00492b0bu, (int64_t)(int32_t)call->slots[0], (int32_t)call->slots[1], &q, &r)) return;
    call->eax = q;
    call->edx = r;
}
