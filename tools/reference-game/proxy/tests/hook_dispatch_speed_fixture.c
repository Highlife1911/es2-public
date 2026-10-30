/* Side-by-side own dispatcher baseline. No original game code is loaded.
 * Result contracts compare EAX, EDX:EAX or ST0 as appropriate, plus x87 value
 * state, preserved frame slots, LastError and every dispatcher counter. */
#include "hook/hook.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct fixture_frame {
    BYTE fpu[108];
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax, flags, stub_return;
    es2_hook *hook;
    uint32_t caller_return;
} fixture_frame;
typedef struct invocation { fixture_frame frame; uint32_t stack[ES2_HOOK_MAX_ARGS]; } invocation;
_Static_assert(sizeof(fixture_frame) == 156, "dispatcher frame layout");
void __cdecl baseline_dispatch_extended(fixture_frame *);
void __cdecl es2_hook_dispatch_extended(fixture_frame *);
BOOL baseline_install_ex(es2_hook *, const es2_hook_descriptor *, const es2_hook_options *, const es2_hook_extension *);
BOOL baseline_uninstall(es2_hook *);
static void (__cdecl *const dispatchers[])(fixture_frame *) = {baseline_dispatch_extended, es2_hook_dispatch_extended};

#define REQUIRE(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #c); exit(1); } } while (0)
static uint32_t rng = 0x42573132;
static uint32_t random32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static unsigned version, nested, checks;
static uintptr_t caller_sp, lowest_sp;
static BYTE initial_fpu[108];
static uint32_t salt = 0x13579bdf;
static es2_hook *active_hook;

static unsigned word16(const BYTE *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }
static void stack_sample(void) {
    uintptr_t sp; __asm__ volatile("movl %%esp,%0" : "=r"(sp));
    if (!lowest_sp || sp < lowest_sp) lowest_sp = sp;
}
static uint64_t value(const uint32_t *a, void *user) {
    uint32_t lo = *(const uint32_t *)user, hi = ~lo;
    for (unsigned i = 0; i < ES2_HOOK_MAX_ARGS; ++i) { lo = lo * 17 + a[i]; hi = hi * 31 + (a[i] ^ lo); }
    return ((uint64_t)hi << 32) | lo;
}
static uint32_t __cdecl replacement32(const uint32_t *a, void *user) {
    stack_sample(); SetLastError(0x5a21); return (uint32_t)value(a, user);
}
static uint64_t __cdecl replacement64(const uint32_t *a, void *user) {
    stack_sample(); SetLastError(0x5a22);
    if (nested && a[0]) {
        invocation inner = {0}; inner.frame.hook = active_hook; inner.stack[0] = a[0] - 1;
        memcpy(inner.frame.fpu, initial_fpu, sizeof(initial_fpu));
        dispatchers[version](&inner.frame);
        return (((uint64_t)inner.frame.edx << 32) | inner.frame.eax) + a[0];
    }
    return value(a, user);
}
static uint64_t __cdecl replacement_context(const uint32_t *a, void *user, const uint32_t *regs) {
    return replacement64(a, user) ^ ((uint64_t)regs[0] << 32) ^ regs[1] ^ regs[2];
}
static long double __cdecl replacement80(const uint32_t *a, void *user) {
    (void)user; stack_sample(); SetLastError(0x5a23); return (long double)(int32_t)a[0];
}
static void initial_state(void) {
    unsigned short cw = 0x1332;
    __asm__ volatile("fninit; fldcw %0; fld1; fldpi; fldz" : : "m"(cw));
    es2_hook_fpu_save(initial_fpu); __asm__ volatile("fninit");
}
typedef struct observation {
    uint32_t eax, edx, error, in_flight;
    uint64_t calls, skipped, bypassed, mismatches;
    BYTE fpu[108];
} observation;
static void fpu_value_state(BYTE *out, const BYTE *in) {
    // FIP/FDP and reserved/empty bytes are not x87 value state (the existing
    // hook comparison contract). Keep CW, SW, tags and occupied 80-bit values.
    memset(out, 0, 108);
    memcpy(out, in, 2); memcpy(out + 4, in + 4, 2); memcpy(out + 8, in + 8, 2);
    unsigned top = (word16(in + 4) >> 11) & 7, tags = word16(in + 8);
    for (unsigned i = 0; i < 8; ++i)
        if (((tags >> (2 * ((top + i) & 7))) & 3) != 3) memcpy(out + 28 + i * 10, in + 28 + i * 10, 10);
}
static observation observe(es2_hook *h, invocation *input, es2_hook_result_kind kind) {
    observation out; memset(&out, 0, sizeof(out));
    invocation call = *input; es2_hook_fpu_save(call.frame.fpu);
    memcpy(call.frame.fpu, initial_fpu, sizeof(initial_fpu));
    uintptr_t sp; __asm__ volatile("movl %%esp,%0" : "=r"(sp)); lowest_sp = 0;
    SetLastError(0x1234); dispatchers[version](&call.frame);
    out.error = GetLastError(); out.in_flight = h->in_flight;
    if (lowest_sp && !nested && h->options.mode == ES2_HOOK_REPLACED)
        REQUIRE(version ? sp - lowest_sp < 4096 : sp - lowest_sp > 64000);
    out.calls = h->calls; out.skipped = h->skipped; out.bypassed = h->bypassed; out.mismatches = h->mismatches;
    if (kind != ES2_RESULT_ST0) out.eax = call.frame.eax;
    if (kind == ES2_RESULT_PAIR) out.edx = call.frame.edx;
    fpu_value_state(out.fpu, call.frame.fpu);
    REQUIRE(!memcmp(&call.frame.edi, &input->frame.edi, 5 * sizeof(uint32_t)));
    REQUIRE(call.frame.ecx == input->frame.ecx && call.frame.flags == input->frame.flags);
    REQUIRE(call.frame.stub_return == input->frame.stub_return && call.frame.caller_return == input->frame.caller_return);
    REQUIRE(!memcmp(call.stack, input->stack, sizeof(call.stack)));
    __asm__ volatile("fninit");
    return out;
}
static void code32(BYTE *p, uint32_t v) { for (unsigned i = 0; i < 4; ++i) p[i] = (BYTE)(v >> (i * 8)); }
static BYTE *original_stub(unsigned argc, es2_hook_abi abi, es2_hook_result_kind kind) {
    BYTE *code = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE); REQUIRE(code);
    unsigned n = 0;
    if (kind == ES2_RESULT_ST0) { code[n++] = 0xd9; code[n++] = 0xe8; } // fld1
    code[n++] = 0xb8; code32(code + n, 0x76543210); n += 4;
    code[n++] = 0xba; code32(code + n, 0xfedcba98); n += 4;
    unsigned cleanup = abi == ES2_ABI_CDECL ? 0 : (abi == ES2_ABI_BORLAND ? (argc > 3 ? argc - 3 : 0) : argc) * 4;
    code[n++] = 0xc2; code[n++] = (BYTE)cleanup; code[n++] = 0;
    REQUIRE(FlushInstructionCache(GetCurrentProcess(), code, n)); return code;
}
static void configure(es2_hook *h, es2_hook_descriptor *d, es2_hook_extension *e, unsigned argc, unsigned abi, unsigned kind) {
    memset(h, 0, sizeof(*h)); memset(d, 0, sizeof(*d)); memset(e, 0, sizeof(*e));
    d->name = "dispatch_cost"; d->arg_count = argc; d->replacement = replacement32; d->user = &salt;
    e->abi = (es2_hook_abi)abi; e->result_kind = kind == 3 ? ES2_RESULT_PAIR : (es2_hook_result_kind)kind;
    e->replacement64 = replacement64; e->replacement80 = replacement80;
    if (kind == 3) e->replacement_context = replacement_context;
    h->descriptor = d; h->extension = e; h->owner_thread = GetCurrentThreadId();
    h->installed = 1; h->options.mode = ES2_HOOK_REPLACED; active_hook = h;
}
static void detour_fixture(void) {
    BYTE *image = VirtualAlloc(NULL, 0x4000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE); REQUIRE(image);
    code32(image, 0x5a4d); code32(image + 0x3c, 0x80); code32(image + 0x80, 0x4550);
    image[0x84] = 0x4c; image[0x85] = 1; image[0x98] = 0x0b; image[0x99] = 1;
    code32(image + 0xb4, (uint32_t)(uintptr_t)image); code32(image + 0xd0, 0x4000);
    static const BYTE signature[] = {0x90, 0x90, 0x90, 0x90, 0x90}, lengths[] = {1, 1, 1, 1, 1};
    for (unsigned abi = 0; abi < 3; ++abi) for (unsigned argc = 0; argc <= ES2_HOOK_MAX_ARGS; ++argc)
    for (unsigned kind = 0; kind < 4; ++kind) for (unsigned mode = 1; mode <= 2; ++mode) {
        es2_hook h; es2_hook_descriptor d; es2_hook_extension e;
        configure(&h, &d, &e, argc, abi, kind);
        d.rva = 0x1000; d.expected = signature; d.expected_size = d.relocate_size = 5;
        d.instruction_lengths = lengths; d.instruction_count = 5;
        BYTE *stub = original_stub(argc, e.abi, e.result_kind);
        memcpy(image + 0x1000, signature, 5); memcpy(image + 0x1005, stub, 32);
        REQUIRE(VirtualFree(stub, 0, MEM_RELEASE));
        REQUIRE(FlushInstructionCache(GetCurrentProcess(), image + 0x1000, 37));
        es2_hook_options options = {0}; options.image = image; options.expected_base = (uintptr_t)image;
        options.expected_image_size = 0x4000; options.mode = (es2_hook_mode)mode;
        uint32_t args[ES2_HOOK_MAX_ARGS], regs[3];
        for (unsigned i = 0; i < ES2_HOOK_MAX_ARGS; ++i) args[i] = random32();
        for (unsigned i = 0; i < 3; ++i) regs[i] = random32();
        observation observations[2]; memset(observations, 0, sizeof(observations));
        for (version = 0; version < 2; ++version) {
            memset(&h, 0, sizeof(h));
            REQUIRE(version ? es2_hook_install_ex(&h, &d, &options, &e) : baseline_install_ex(&h, &d, &options, &e));
            es2_hook_result result;
            es2_hook_call call = {image + 0x1000, e.abi, argc, args, initial_fpu, &result, regs};
            SetLastError(0x1234); es2_hook_machine_call(&call);
            observation *o = &observations[version]; o->error = GetLastError();
            if (e.result_kind != ES2_RESULT_ST0) o->eax = result.eax;
            if (e.result_kind == ES2_RESULT_PAIR) o->edx = result.edx;
            fpu_value_state(o->fpu, result.fpu); __asm__ volatile("fninit");
            o->calls = h.calls; o->mismatches = h.mismatches; o->bypassed = h.bypassed;
            o->skipped = h.skipped; o->in_flight = h.in_flight;
            REQUIRE(version ? es2_hook_uninstall(&h) : baseline_uninstall(&h));
            REQUIRE(!memcmp(image + 0x1000, signature, 5));
        }
        REQUIRE(!memcmp(&observations[0], &observations[1], sizeof(observation))); ++checks;
    }
    REQUIRE(VirtualFree(image, 0, MEM_RELEASE));
}
static void differential_fixture(void) {
    es2_hook h; es2_hook_descriptor d; es2_hook_extension e;
    for (unsigned abi = 0; abi < 3; ++abi) for (unsigned argc = 0; argc <= ES2_HOOK_MAX_ARGS; ++argc)
    for (unsigned kind = 0; kind < 4; ++kind) {
        configure(&h, &d, &e, argc, abi, kind);
        BYTE *original = original_stub(argc, e.abi, e.result_kind); h.trampoline = original;
        for (unsigned i = 0; i < 96; ++i) {
            invocation input = {0}; input.frame.hook = &h;
            uint32_t *registers = &input.frame.edi;
            for (unsigned j = 0; j < 10; ++j) registers[j] = random32();
            input.frame.caller_return = random32();
            for (unsigned j = 0; j < ES2_HOOK_MAX_ARGS; ++j) input.stack[j] = random32();
            observation observations[2];
            for (version = 0; version < 2; ++version) {
                h.calls = 0xffffffffu; h.skipped = h.bypassed = h.mismatches = h.in_flight = 0;
                h.installed = i % 4 == 1 ? 2 : 1; h.owner_thread = i % 4 == 2 ? 0 : GetCurrentThreadId();
                h.options.mode = i % 8 == 3 ? ES2_HOOK_COMPARE : ES2_HOOK_REPLACED;
                observations[version] = observe(&h, &input, e.result_kind);
            }
            REQUIRE(!memcmp(&observations[0], &observations[1], sizeof(observation))); ++checks;
        }
        REQUIRE(VirtualFree(original, 0, MEM_RELEASE));
    }
    configure(&h, &d, &e, 1, ES2_ABI_CDECL, ES2_RESULT_PAIR);
    h.trampoline = original_stub(1, e.abi, e.result_kind); nested = 1;
    for (unsigned depth = 0; depth <= 12; ++depth) {
        invocation input = {0}; input.frame.hook = &h; input.stack[0] = depth;
        observation observations[2];
        for (version = 0; version < 2; ++version) {
            h.calls = h.skipped = h.bypassed = h.mismatches = h.in_flight = 0;
            observations[version] = observe(&h, &input, e.result_kind);
        }
        REQUIRE(!memcmp(&observations[0], &observations[1], sizeof(observation))); ++checks;
        REQUIRE(h.calls == (depth < ES2_HOOK_MAX_DEPTH ? depth + 1 : ES2_HOOK_MAX_DEPTH + 1));
        REQUIRE(h.skipped == (depth >= ES2_HOOK_MAX_DEPTH));
    }
    nested = 0; REQUIRE(VirtualFree(h.trampoline, 0, MEM_RELEASE));
    detour_fixture();
    printf("PASS dispatcher: %u old/new observations; all ABIs, 0..6 args, EAX/pair/ST0/context, compare/owner/bypass/depth, counter carry, generated detours and replaced stack bound\n", checks);
}
static LONGLONG clock_now(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
static void benchmark_fixture(void) {
    LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency);
    for (unsigned abi = 0; abi < 3; ++abi) for (unsigned argc = 0; argc <= 6; argc += 3) {
        es2_hook h; es2_hook_descriptor d; es2_hook_extension e;
        configure(&h, &d, &e, argc, abi, ES2_RESULT_PAIR);
        invocation input = {0}; input.frame.hook = &h; memcpy(input.frame.fpu, initial_fpu, sizeof(initial_fpu));
        for (unsigned i = 0; i < 6; ++i) input.stack[i] = i + 1;
        invocation initial = input;
        for (unsigned trial = 0; trial < 3; ++trial) for (unsigned v = 0; v < 2; ++v) {
            version = v ^ (trial & 1); lowest_sp = 0; h.calls = 0; input = initial;
            __asm__ volatile("movl %%esp,%0" : "=r"(caller_sp));
            LONGLONG began = clock_now();
            for (unsigned i = 0; i < 300000; ++i) dispatchers[version](&input.frame);
            LONGLONG elapsed = clock_now() - began; REQUIRE(h.calls == 300000 && !h.in_flight);
            __asm__ volatile("fninit");
            printf("dispatch abi=%u args=%u trial=%u version=%s ns=%.3f stack_to_callback=%lu\n", abi, argc, trial,
                version ? "new" : "old", (double)elapsed * 1e9 / frequency.QuadPart / 300000,
                (unsigned long)(caller_sp - lowest_sp));
        }
    }
}
int main(int argc, char **argv) {
    if (argc != 2) return 2; initial_state();
    if (!strcmp(argv[1], "differential")) differential_fixture();
    else if (!strcmp(argv[1], "benchmark")) benchmark_fixture();
    else return 2;
    return 0;
}
