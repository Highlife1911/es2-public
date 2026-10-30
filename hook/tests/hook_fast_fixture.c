/* Side-by-side own dispatcher baseline. No original game code is loaded.
 * Result contracts compare EAX, EDX:EAX or ST0 as appropriate, plus x87 value
 * state, preserved frame slots, LastError and every dispatcher counter. */
#include "hook/hook.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tools/reference-game/proxy/oracle_perf.h"
#include "counter_candidate.h"

typedef struct fixture_frame {
    BYTE fpu[108];
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax, flags, stub_return;
    es2_hook *hook;
    uint32_t caller_return;
} fixture_frame;
typedef struct invocation { fixture_frame frame; uint32_t stack[ES2_HOOK_MAX_ARGS]; } invocation;
_Static_assert(sizeof(fixture_frame) == 156, "dispatcher frame layout");
BOOL __cdecl es2_hook_dispatch_fast(fixture_frame *);
void __cdecl es2_hook_dispatch_extended(fixture_frame *);
BOOL baseline_install_ex(es2_hook *, const es2_hook_descriptor *, const es2_hook_options *, const es2_hook_extension *);
BOOL baseline_uninstall(es2_hook *);
static void __cdecl fast_model(fixture_frame *f) {
    __asm__ volatile("frstor %0" : : "m"(f->fpu));
    if (es2_hook_dispatch_fast(f)) es2_hook_fpu_save(f->fpu);
    else es2_hook_dispatch_extended(f);
}
static void (__cdecl *const dispatchers[])(fixture_frame *) = {es2_hook_dispatch_extended, fast_model};
BOOL baseline_install_ex(es2_hook *h, const es2_hook_descriptor *d, const es2_hook_options *o, const es2_hook_extension *e) {
    es2_hook_set_fast_path(FALSE);
    BOOL result = es2_hook_install_ex(h, d, o, e);
    es2_hook_set_fast_path(TRUE); return result;
}
BOOL baseline_uninstall(es2_hook *h) { return es2_hook_uninstall(h); }

#define REQUIRE(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #c); exit(1); } } while (0)
static uint32_t rng = 0x42573132;
static uint32_t random32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static unsigned version, nested, compare_tail, checks;
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
        // compare_tail: the innermost level runs in compare mode.
        if (compare_tail && a[0] == 1) active_hook->options.mode = ES2_HOOK_COMPARE;
        dispatchers[version](&inner.frame);
        if (compare_tail) active_hook->options.mode = ES2_HOOK_REPLACED;
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
static void varied_state(unsigned variant) {
    unsigned short cw=(unsigned short)(0x037f|((variant&3)<<10));
    __asm__ volatile("fninit; fldcw %0"::"m"(cw));
    for(unsigned i=0;i<(variant%9);++i) {
        if(i&1)__asm__ volatile("fldz; fchs");else __asm__ volatile("fldpi");
    }
    if(variant%9)__asm__ volatile("ftst");
    es2_hook_fpu_save(initial_fpu);__asm__ volatile("fninit");
}
static unsigned __cdecl observer_begin(es2_hook *h) {
    DWORD error=GetLastError();unsigned token=perf_hook_begin(h,h->descriptor->name);SetLastError(error);return token;
}
static void __cdecl observer_end(unsigned token) { DWORD error=GetLastError();perf_hook_end(token);SetLastError(error); }
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
        REQUIRE(sp - lowest_sp < 4096);
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
static DWORD WINAPI foreign_call(void *arg) {
    SetLastError(0x1234);es2_hook_machine_call(arg);DWORD error=GetLastError();__asm__ volatile("fninit");return error;
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
            HANDLE thread=CreateThread(NULL,0,foreign_call,&call,0,NULL);REQUIRE(thread);
            REQUIRE(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0);DWORD exit_code;
            REQUIRE(GetExitCodeThread(thread,&exit_code)&&exit_code==0x1234);CloseHandle(thread);
            REQUIRE(h.calls==o->calls&&h.skipped==o->skipped&&h.bypassed==o->bypassed&&!h.in_flight);
            if(e.result_kind!=ES2_RESULT_ST0)REQUIRE(result.eax==0x76543210);
            if(e.result_kind==ES2_RESULT_PAIR)REQUIRE(result.edx==0xfedcba98);
            REQUIRE(version ? es2_hook_uninstall(&h) : baseline_uninstall(&h));
            REQUIRE(!memcmp(image + 0x1000, signature, 5));
        }
        REQUIRE(!memcmp(&observations[0], &observations[1], sizeof(observation))); ++checks;
    }
    REQUIRE(VirtualFree(image, 0, MEM_RELEASE));
}
static void differential_fixture(void) {
    InitializeCriticalSection(&perf_lock);perf_lock_ready=TRUE;perf_thread=GetCurrentThreadId();perf_last=perf_now();
    es2_hook_set_fast_observer(observer_begin,observer_end);
    es2_hook h; es2_hook_descriptor d; es2_hook_extension e;
    for (unsigned abi = 0; abi < 3; ++abi) for (unsigned argc = 0; argc <= ES2_HOOK_MAX_ARGS; ++argc)
    for (unsigned kind = 0; kind < 4; ++kind) {
        configure(&h, &d, &e, argc, abi, kind);
        BYTE *original = original_stub(argc, e.abi, e.result_kind); h.trampoline = original;
        for (unsigned i = 0; i < 96; ++i) {
            varied_state(i);perf_enabled=(i&1)!=0;
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
    for (unsigned depth = 0; depth <= ES2_HOOK_MAX_DEPTH + 4; ++depth) {
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
    // A compare level below replaced levels starts only below the compare
    // depth; deeper it runs the original and counts as skipped.
    compare_tail = 1;
    for (unsigned depth = 1; depth <= ES2_HOOK_MAX_COMPARE_DEPTH + 2; ++depth) {
        invocation input = {0}; input.frame.hook = &h; input.stack[0] = depth;
        h.options.mode = ES2_HOOK_REPLACED; version = 0;
        h.calls = h.skipped = h.bypassed = h.mismatches = h.in_flight = 0;
        (void)observe(&h, &input, e.result_kind); ++checks;
        REQUIRE(h.calls == depth + 1 && !h.bypassed);
        REQUIRE(h.skipped == (depth >= ES2_HOOK_MAX_COMPARE_DEPTH));
    }
    compare_tail = 0; h.options.mode = ES2_HOOK_REPLACED;
    nested = 0; REQUIRE(VirtualFree(h.trampoline, 0, MEM_RELEASE));
    initial_state();perf_enabled=FALSE;detour_fixture();
    REQUIRE(perf_counts[PERF_HOOK]);es2_hook_set_fast_observer(NULL,NULL);
    DeleteCriticalSection(&perf_lock);perf_lock_ready=FALSE;
    printf("PASS fast/generic: %u old/new observations; all ABIs, 0..6 args, EAX/pair/ST0/context, compare/owner/bypass/depth, counter carry, generated detours and replaced stack bound\n", checks);
}
static LONGLONG clock_now(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
static volatile uint64_t component_counter;
static volatile unsigned component_depth, component_flight;
static volatile uint32_t component_sink;
static __attribute__((noinline)) void component_empty(void) { __asm__ volatile("" ::: "memory"); }
static __attribute__((noinline)) void component_thread(void) { component_sink=GetCurrentThreadId(); }
static __attribute__((noinline)) void component_counter_only(void) { ++component_counter; }
static __attribute__((noinline)) void component_counter_candidate(void) { counter_candidate(&component_counter); }
static __attribute__((noinline)) void component_bookkeeping(void) {
    if(component_depth<8){++component_depth;++component_flight;++component_counter;--component_flight;--component_depth;}
}
static __attribute__((noinline)) void component_unpack(void) {
    uint32_t args[6]={0};for(unsigned i=0;i<6;++i)args[i]=component_sink+i;
    __asm__ volatile("" :: "r"(args) : "memory");
}
static __attribute__((noinline)) void component_x87(void) { BYTE fpu[108];es2_hook_fpu_save(fpu); }
static __attribute__((noinline)) void component_restore(void) { __asm__ volatile("frstor %0"::"m"(initial_fpu)); }
static __attribute__((noinline)) void component_bridge(void) {
    uint32_t args[6]={0};uint32_t callback_args[3]={(uint32_t)(uintptr_t)args,(uint32_t)(uintptr_t)&salt,0};
    es2_hook_result result;
    es2_hook_call c={(void *)replacement64,ES2_ABI_CDECL,2,callback_args,initial_fpu,&result,NULL};
    es2_hook_machine_call(&c);
}
static __attribute__((noinline)) void component_perf(void) {
    DWORD error=GetLastError();unsigned phase=perf_begin(PERF_HOOK);SetLastError(error);
    error=GetLastError();perf_end(phase);SetLastError(error);
}
static __attribute__((noinline)) void component_hook_perf(void) {
    DWORD error=GetLastError();unsigned token=perf_hook_begin(&salt,"fixture");SetLastError(error);
    error=GetLastError();perf_hook_end(token);SetLastError(error);
}
static void component_benchmark(void) {
    void (*f[])(void)={component_empty,component_thread,component_counter_only,component_counter_candidate,component_bookkeeping,component_unpack,component_x87,component_restore,component_bridge,component_perf,component_hook_perf};
    const char *names[]={"empty","thread_gate","counter","counter_candidate","depth_and_counters","unpack_six","fnsave_frstor","frstor","machine_bridge_body","perf_wrapper","perf_hook_names"};
    LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);
    InitializeCriticalSection(&perf_lock);perf_lock_ready=TRUE;perf_enabled=TRUE;perf_thread=GetCurrentThreadId();perf_last=perf_now();
    perf_started=TRUE;
    for(unsigned trial=0;trial<5;++trial)for(unsigned j=0;j<11;++j) {
        unsigned i=trial&1?10-j:j;LONGLONG start=clock_now();
        for(unsigned k=0;k<300000;++k)f[i]();LONGLONG elapsed=clock_now()-start;
        printf("component name=%s trial=%u ns=%.3f\n",names[i],trial,(double)elapsed*1e9/frequency.QuadPart/300000);
    }
    perf_enabled=FALSE;DeleteCriticalSection(&perf_lock);perf_lock_ready=FALSE;
    for(unsigned i=0;i<100;++i) {
        uint64_t reference=UINT64_C(0x12345678ffffffd0)+i,candidate=reference;
        ++reference;counter_candidate(&candidate);REQUIRE(reference==candidate);
    }
}
void __cdecl es2_hook_bench_run(void *, unsigned, unsigned, unsigned);
static void installed_benchmark(void) {
    LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);
    BYTE *image=VirtualAlloc(NULL,0x4000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);REQUIRE(image);
    code32(image,0x5a4d);code32(image+0x3c,0x80);code32(image+0x80,0x4550);
    image[0x84]=0x4c;image[0x85]=1;image[0x98]=0x0b;image[0x99]=1;
    code32(image+0xb4,(uint32_t)(uintptr_t)image);code32(image+0xd0,0x4000);
    static const BYTE signature[]={0x90,0x90,0x90,0x90,0x90},lengths[]={1,1,1,1,1};
    for(unsigned abi=0;abi<3;++abi)for(unsigned argc=0;argc<=6;argc+=3) {
        es2_hook h;es2_hook_descriptor d;es2_hook_extension e;configure(&h,&d,&e,argc,abi,ES2_RESULT_PAIR);
        d.rva=0x1000;d.expected=signature;d.expected_size=d.relocate_size=5;d.instruction_lengths=lengths;d.instruction_count=5;
        BYTE *stub=original_stub(argc,e.abi,e.result_kind);memcpy(image+0x1000,signature,5);memcpy(image+0x1005,stub,32);VirtualFree(stub,0,MEM_RELEASE);
        es2_hook_options o={0};o.image=image;o.expected_base=(uintptr_t)image;o.expected_image_size=0x4000;o.mode=ES2_HOOK_REPLACED;
        for(unsigned trial=0;trial<5;++trial)for(unsigned v=0;v<2;++v) {
            version=v^(trial&1);memset(&h,0,sizeof(h));es2_hook_set_fast_path(version!=0);
            REQUIRE(es2_hook_install_ex(&h,&d,&o,&e));lowest_sp=0;
            __asm__ volatile("frstor %0"::"m"(initial_fpu));
            LONGLONG start=clock_now();es2_hook_bench_run(image+0x1000,abi,argc,300000);LONGLONG elapsed=clock_now()-start;
            __asm__ volatile("fninit");REQUIRE(h.calls==300000&&!h.in_flight);
            REQUIRE(es2_hook_uninstall(&h));
            printf("installed abi=%u args=%u trial=%u version=%s ns=%.3f\n",abi,argc,trial,version?"fast":"generic",(double)elapsed*1e9/frequency.QuadPart/300000);
        }
    }
    es2_hook_set_fast_path(TRUE);VirtualFree(image,0,MEM_RELEASE);
}
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
static uint32_t __cdecl equal32(const uint32_t *a,void *u) {(void)a;(void)u;return 0x76543210;}
static uint64_t __cdecl equal64(const uint32_t *a,void *u) {(void)a;(void)u;return UINT64_C(0xfedcba9876543210);}
static long double __cdecl equal80(const uint32_t *a,void *u) {(void)a;(void)u;return 1.0L;}
static void compare_benchmark(void) {
    const char *names[]={"eax_leaf","pair_leaf","st0_leaf","span64","span4096","region513","region2050","region10634"};
    unsigned sizes[]={0,0,0,64,4096,513,2050,10634};
    BYTE *data=VirtualAlloc(NULL,0x5000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);REQUIRE(data);
    LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);
    for(unsigned trial=0;trial<5;++trial)for(unsigned step=0;step<8;++step) {
        unsigned index=trial&1?7-step:step;
        unsigned kind=index<3?index:ES2_RESULT_PAIR;
        es2_hook h;es2_hook_descriptor d;es2_hook_extension e;configure(&h,&d,&e,1,ES2_ABI_CDECL,kind);
        d.replacement=equal32;e.replacement64=equal64;e.replacement80=equal80;
        es2_hook_span span={0,0,sizes[index]};
        es2_hook_region region={ES2_REGION_RVA,0,0,0,0,sizes[index],TRUE,0,{0}};
        if(index==3||index==4){d.spans=&span;d.span_count=1;}
        if(index>=5){e.regions=&region;e.region_count=1;h.options.image=data;}
        h.options.mode=ES2_HOOK_COMPARE;h.trampoline=original_stub(1,e.abi,e.result_kind);
        invocation input={0};input.frame.hook=&h;memcpy(input.frame.fpu,initial_fpu,sizeof(initial_fpu));input.stack[0]=(uint32_t)(uintptr_t)data;
        LONGLONG start=clock_now();for(unsigned i=0;i<2000;++i) {
            /* ST0 pushes once in each call. Each logical invocation begins
             * with the same caller x87 state, as in the differential fixture. */
            memcpy(input.frame.fpu,initial_fpu,sizeof(initial_fpu));
            es2_hook_dispatch_extended(&input.frame);
        }
        LONGLONG elapsed=clock_now()-start;__asm__ volatile("fninit");
        REQUIRE(h.calls==2000&&!h.mismatches&&!h.skipped&&!h.in_flight);
        printf("compare_class name=%s bytes=%u trial=%u ns=%.3f\n",names[index],sizes[index],trial,(double)elapsed*1e9/frequency.QuadPart/2000);
        VirtualFree(h.trampoline,0,MEM_RELEASE);
    }
    VirtualFree(data,0,MEM_RELEASE);
}
int main(int argc, char **argv) {
    if (argc != 2) return 2; initial_state();
    if (!strcmp(argv[1], "differential")) differential_fixture();
    else if (!strcmp(argv[1], "benchmark")) { benchmark_fixture(); installed_benchmark(); component_benchmark(); compare_benchmark(); }
    else return 2;
    return 0;
}
