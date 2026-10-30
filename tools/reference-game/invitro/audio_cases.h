/* DBSIM's audio, original machine code against sim/src/audio.c (--audio; target
 * :differential_audio_test). Included after input_layer_cases.h; reuses
 * startup_cases.h's DATA reset and snapshots (su_reset, su_take, su_compare)
 * and loop_cases.h's virtual time, clock stub and dispatch helpers.
 *
 * The SOS driver: the original side's LoadLibraryA/GetProcAddress hand out
 * this file's stdcall thunks for the 20 entry points DBSIM reaches; each
 * thunk keeps EDX and ECX (the own code's model of a driver call, audio.h)
 * and forwards to a silent device of its side; the own side's device is the
 * same silent device. A per-case script makes chosen entry points answer a
 * status other than the silent device's (failures), identically on both
 * sides. MCI: the silent device's answers on both sides.
 *
 * - driver: the driver load 0x473708 over random sound.cfg words, device
 *   formats and failure scripts, then the shutdown 0x4737a4 (with a random
 *   uninit status). Compared: EAX, DATA (the 97 entry slots and the module
 *   handle masked: the own process loads no DLL), the device call logs, the
 *   last error.
 * - manager: a synthetic SFX world (context, 2..8 file records, 2..12
 *   slots, cached records with sample structures, stale and free entries)
 *   and 12 random calls of the manager: open, close, cache unload, done,
 *   priority, loops, eviction, stop all (internal functions through the
 *   test seam, EAX compared) and the rows volume, play, stop, rate, pan, is
 *   playing (EAX, EDX, ECX compared), with virtual time moving between
 *   calls. Compared after the case: the world, DATA, the heap, the device
 *   logs, the GlobalFree calls, the last error.
 * - timeline: the voice channels and the lip event queue: the lip pass
 *   0x4632c4 over random queues and scripts, the voice start 0x462b9c without
 *   a lip file (context absent or the voice byte 0), the voice 0x462940 and
 *   the lip value 0x46356c; sfx_lip/sfx_lip_due/sfx_voice run as the
 *   original's machine code on both sides. Compared: EAX/EDX/ECX of every
 *   call, the channels, the queue, the clock reads, DATA. */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "input_layer_cases.h"
#include "es2/sim/audio.h"

#define AU_WORLD_SIZE 0x10000u
#define AU_GLOBAL_SIZE 0x10000u
static BYTE *au_world, *au_global;
static uint32_t au_global_next[2];
static es2_sim_audio_silent au_silent[2];
static es2_sim_audio au_audio;
static es2_sim_audio_device au_device;
static int au_bad;
/* The device call log of a side: count and FNV hash of (entry, args). */
static uint64_t au_log_hash[2];
static unsigned au_log_count[2], au_frees[2];
static uint32_t au_last_error[2], au_status[ES2_SOS_CALL_COUNT];   /* the script: 0xffffffff = the silent answer */
static void au_log(unsigned side, uint32_t k, const uint32_t *a, unsigned n) {
    uint64_t h = au_log_hash[side];
    h = (h ^ k) * 0x100000001b3ull;
    for (unsigned i = 0; i < n; ++i) h = (h ^ a[i]) * 0x100000001b3ull;
    au_log_hash[side] = h;
    ++au_log_count[side];
}
static uint32_t au_answer(unsigned side, es2_sim_sos_call k, const uint32_t *a, unsigned n) {
    au_log(side, (uint32_t)k, a, n);
    es2_sim_audio_device d;
    es2_sim_audio_silent_device(&au_silent[side], &d);
    uint32_t r = d.sos(d.context, &ir_memory, k, a, n);
    return au_status[k] != 0xffffffffu ? au_status[k] : r;
}
/* ---- the original side: thunks in the entry slots */
static const unsigned au_arg_count[ES2_SOS_CALL_COUNT] = {2, 1, 0, 2, 1, 2, 2, 1, 1, 0, 0, 3, 2, 2, 3, 3, 3, 1, 1, 2};
static const char *const au_names[ES2_SOS_CALL_COUNT] = {
    "sosDIGIDetectInit", "sosDIGIDetectFindFirst", "sosDIGIDetectUnInit", "sosDIGIInitSystem", "sosTIMERInitSystem",
    "sosDIGIInitDriver", "sosDIGISetActiveWindow", "sosDIGIStopAllSamples", "sosDIGIUnInitDriver", "sosTIMERUnInitSystem",
    "sosDIGIUnInitSystem", "sosDIGIStartSample", "sosDIGIStopSample", "sosDIGISampleDone", "sosDIGISetSampleVolume",
    "sosDIGISetPanLocation", "sosDIGISetSampleRate", "sosDIGIUserService", "sosDIGIAcquireDriver", "sosDIGIReleaseDriver"};
uint32_t __cdecl au_thunk_c(uint32_t k, const uint32_t *args) { return au_answer(0, (es2_sim_sos_call)k, args, au_arg_count[k]); }
/* Each thunk: push ECX, EDX; au_thunk_c(k, &first argument); pop EDX, ECX;
 * ret 4 * count (the DLL's stdcall). */
#define AU_THUNK(k, bytes) \
    extern void au_thunk_##k(void); \
    __asm__(".text\n.globl _au_thunk_" #k "\n_au_thunk_" #k ":\n\tpushl %ecx\n\tpushl %edx\n\tleal 12(%esp), %eax\n" \
            "\tpushl %eax\n\tpushl $" #k "\n\tcall _au_thunk_c\n\taddl $8, %esp\n\tpopl %edx\n\tpopl %ecx\n\tret $" #bytes "\n");
AU_THUNK(0, 8) AU_THUNK(1, 4) AU_THUNK(2, 0) AU_THUNK(3, 8) AU_THUNK(4, 4) AU_THUNK(5, 8) AU_THUNK(6, 8) AU_THUNK(7, 4)
AU_THUNK(8, 4) AU_THUNK(9, 0) AU_THUNK(10, 0) AU_THUNK(11, 12) AU_THUNK(12, 8) AU_THUNK(13, 8) AU_THUNK(14, 12)
AU_THUNK(15, 12) AU_THUNK(16, 12) AU_THUNK(17, 4) AU_THUNK(18, 4) AU_THUNK(19, 8)
static void (*const au_thunks[ES2_SOS_CALL_COUNT])(void) = {
    au_thunk_0, au_thunk_1, au_thunk_2, au_thunk_3, au_thunk_4, au_thunk_5, au_thunk_6, au_thunk_7, au_thunk_8, au_thunk_9,
    au_thunk_10, au_thunk_11, au_thunk_12, au_thunk_13, au_thunk_14, au_thunk_15, au_thunk_16, au_thunk_17, au_thunk_18, au_thunk_19};
static void __stdcall au_unexpected(void) { au_bad = 1; }
/* The entry slots the stubs call through (the name table 0x4a6a9c). */
static const uint32_t au_slots[ES2_SOS_CALL_COUNT] = {0x6cbda8u, 0x6cbdb0u, 0x6cbdacu, 0x6cbd60u, 0x6cbd70u, 0x6cbd68u, 0x6cbda0u, 0x6cbe30u,
                                                      0x6cbd6cu, 0x6cbd74u, 0x6cbd64u, 0x6cbe28u, 0x6cbe2cu, 0x6cbe24u, 0x6cbdecu, 0x6cbe00u,
                                                      0x6cbe08u, 0x6cbd90u, 0x6cbdb8u, 0x6cbdbcu};
static uint32_t __stdcall au_get_version(void) { return 0x23f00206u; }
static uint32_t __stdcall au_load_library(const char *name) { (void)name; return 0x50500000u; }
static uint32_t __stdcall au_proc(uint32_t module, const char *name) {
    (void)module;
    for (unsigned k = 0; k < ES2_SOS_CALL_COUNT; ++k) if (!strcmp(name, au_names[k])) return (uint32_t)(uintptr_t)au_thunks[k];
    return (uint32_t)(uintptr_t)au_unexpected;
}
static uint32_t __stdcall au_free_library(uint32_t module) { (void)module; return 1; }
static void __stdcall au_set_last_error(uint32_t e) { au_last_error[0] = e; }
static uint32_t au_g_alloc(unsigned side, uint32_t size) {
    uint32_t a = (uint32_t)(uintptr_t)au_global + au_global_next[side];
    if (au_global_next[side] + size > AU_GLOBAL_SIZE) return 0;
    au_global_next[side] += (size + 15u) & ~15u;
    return a;
}
static uint32_t __stdcall au_global_alloc(uint32_t flags, uint32_t size) { (void)flags; return au_g_alloc(0, size); }
static uint32_t __stdcall au_global_lock(uint32_t h) { return h; }
static uint32_t __stdcall au_global_unlock(uint32_t h) { (void)h; return 0; }
static uint32_t __stdcall au_global_free(uint32_t h) { (void)h; ++au_frees[0]; return 0; }
static uint32_t __stdcall au_mci(uint32_t d, uint32_t m, uint32_t f, uint32_t p) {
    uint32_t a[4] = {d, m, f, p};
    au_log(0, 0x200u, a, 4);
    es2_sim_audio_device dev;
    es2_sim_audio_silent_device(&au_silent[0], &dev);
    return dev.mci(dev.context, &ir_memory, d, m, f, p);
}
/* ---- the own side's device and platform */
static uint32_t au_own_sos(void *c, const es2_guest_memory *m, es2_sim_sos_call k, const uint32_t *a, unsigned n) {
    (void)c; (void)m;
    return au_answer(1, k, a, n);
}
static uint32_t au_own_mci(void *c, const es2_guest_memory *m, uint32_t d, uint32_t msg, uint32_t f, uint32_t p) {
    (void)c;
    uint32_t a[4] = {d, msg, f, p};
    au_log(1, 0x200u, a, 4);
    es2_sim_audio_device dev;
    es2_sim_audio_silent_device(&au_silent[1], &dev);
    return dev.mci(dev.context, m, d, msg, f, p);
}
static uint32_t au_own_alloc(void *u, uint32_t flags, uint32_t size) { (void)u; (void)flags; return au_g_alloc(1, size); }
static uint32_t au_own_free(void *u, uint32_t h) { (void)u; (void)h; ++au_frees[1]; return 0; }

/* The hooks the audio code calls, as the original's machine code on the own side. */
static void au_original(es2_sim_service_call *call, void *user) {
    unsigned n = (unsigned)(uintptr_t)user;
    es2_hook_result r;
    uint32_t a[4] = {0, 0, 0, 0};
    for (unsigned i = 0; i < n; ++i) a[i] = call->slots[i];
    uint32_t regs[3] = {call->eax, call->edx, call->ecx};
    es2_hook_call c = {mapped + (call->va - 0x400000u), ES2_ABI_CDECL, n, a, initial_fpu, &r, regs};
    es2_hook_machine_call(&c);
    call->eax = r.eax; call->edx = r.edx;
}
/* A machine call with entry registers and EAX/EDX/ECX after it. */
extern void au_call(uint32_t target, const uint32_t *args, uint32_t count, const uint32_t *in, uint32_t *out);
__asm__(".text\n.globl _au_call\n_au_call:\n\tpushl %ebp\n\tmovl %esp, %ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n"
        "\tmovl 16(%ebp), %ecx\n\tmovl 12(%ebp), %esi\n"
        "1:\n\ttestl %ecx, %ecx\n\tjz 2f\n\tdecl %ecx\n\tpushl (%esi,%ecx,4)\n\tjmp 1b\n"
        "2:\n\tmovl 20(%ebp), %ebx\n\tmovl (%ebx), %eax\n\tmovl 4(%ebx), %edx\n\tmovl 8(%ebx), %ecx\n"
        "\tcall *8(%ebp)\n"
        "\tmovl 24(%ebp), %ebx\n\tmovl %eax, (%ebx)\n\tmovl %edx, 4(%ebx)\n\tmovl %ecx, 8(%ebx)\n"
        "\tleal -12(%ebp), %esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n");
static es2_sim_entry au_entries[4096];
static es2_sim_dispatch au_dispatch;
static void au_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own audio: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    au_bad = 1;
}
static int au_dispatch_setup(void) {
    es2_sim_dispatch_init(&au_dispatch, au_entries, 4096, au_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    static const struct { uint32_t va; unsigned slots; const char *name; } passed[] = {
        {0x00463400u, 1, "sfx_voice (original)"}, {0x004634a0u, 1, "sfx_lip_due (original)"}, {0x004634b0u, 1, "sfx_lip (original)"},
    };
    if (es2_sim_register_services(&au_dispatch, &s)) return fail("audio: dispatch");
    for (unsigned i = 0; i < sizeof(passed) / sizeof(passed[0]); ++i)
        if (es2_sim_dispatch_add_service(&au_dispatch, passed[i].va, passed[i].name, au_original, (void *)(uintptr_t)passed[i].slots, 0,
                                         (uint8_t)passed[i].slots))
            return fail("audio: dispatch");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&au_dispatch, &error, &bad)) return fail("audio: dispatch seal");
    return 0;
}
static uint32_t au_rand(uint32_t n) { return random32() % n; }
static BYTE *au_world_start;   /* the world as both sides start it */
static void au_side(unsigned side, uint32_t clock) {
    lc_reset(side, 47, clock, 1);
    lc_loop.dispatch = &au_dispatch;
    au_log_hash[side] = 0xcbf29ce484222325ull;
    au_log_count[side] = 0;
    au_frees[side] = 0;
    au_last_error[side] = 0;
    au_global_next[side] = 0;
    es2_sim_audio_silent_init(&au_silent[side], &lc_vt);
    au_device.context = NULL;
    au_device.sos = au_own_sos;
    au_device.mci = au_own_mci;
    es2_sim_audio_headless(&au_audio, &lc_loop, &au_device);
    au_audio.platform.global_alloc = au_own_alloc;
    au_audio.platform.global_free = au_own_free;
    es2_sim_audio_bind(&au_audio);
    memcpy(au_world, au_world_start, AU_WORLD_SIZE);
    memset(au_global, 0, AU_GLOBAL_SIZE);
    /* As after a driver load: the original's stubs find the thunks in the slots. */
    for (unsigned k = 0; k < ES2_SOS_CALL_COUNT; ++k) put32(mapped + (au_slots[k] - 0x400000u), (uint32_t)(uintptr_t)au_thunks[k]);
}
static void au_end(unsigned side) {
    if (side) au_last_error[1] = au_audio.platform.last_error;
    /* The original's DLL load writes the module handle and the 97 entry slots,
     * the own process neither (audio.md, "audio in the own process"):
     * masked. */
    memset(mapped + 0x2cbd60u, 0, 0x2cbee4u - 0x2cbd60u);
    su_take(side);
}
static int au_compare(const char *what, unsigned k) {
    if (su_compare(what, k)) return 1;
    if (au_log_hash[0] != au_log_hash[1] || au_log_count[0] != au_log_count[1]) {
        fprintf(stderr, "FAIL: audio %s case %u: device calls %u / %u differ\n", what, k, au_log_count[0], au_log_count[1]);
        return 1;
    }
    if (au_frees[0] != au_frees[1] || au_last_error[0] != au_last_error[1]) {
        fprintf(stderr, "FAIL: audio %s case %u: GlobalFree %u / %u, last error %lu / %lu\n", what, k, au_frees[0], au_frees[1],
                (unsigned long)au_last_error[0], (unsigned long)au_last_error[1]);
        return 1;
    }
    if (memcmp(au_world, au_world_start + AU_WORLD_SIZE, AU_WORLD_SIZE)) {
        for (unsigned i = 0, shown = 0; i < AU_WORLD_SIZE && shown < 16u; ++i)
            if (au_world[i] != au_world_start[AU_WORLD_SIZE + i]) {
                fprintf(stderr, "  world +0x%04x original %02x own %02x\n", i, au_world_start[AU_WORLD_SIZE + i], au_world[i]);
                ++shown;
            }
        fprintf(stderr, "FAIL: audio %s case %u: the world differs\n", what, k);
        return 1;
    }
    return 0;
}
/* After side 0: keep its world for the compare. */
static void au_keep_world(void) { memcpy(au_world_start + AU_WORLD_SIZE, au_world, AU_WORLD_SIZE); }

/* ---- driver */
static unsigned au_driver_ok, au_driver_fallback;
static int au_driver_case(unsigned k) {
    memset(au_world_start, 0, AU_WORLD_SIZE);
    uint16_t words[8];
    static const uint16_t rates[] = {0x10, 0x20}, widths[] = {4, 8}, devices[] = {1, 2, 0xffff};
    words[0] = au_rand(5) ? rates[au_rand(2)] : (uint16_t)au_rand(0x80);
    words[1] = au_rand(5) ? 1 : (uint16_t)au_rand(0x80);
    words[2] = au_rand(5) ? widths[au_rand(2)] : (uint16_t)au_rand(0x80);
    words[3] = au_rand(4) ? devices[au_rand(3)] : (uint16_t)au_rand(4);
    words[4] = 0; words[5] = 0;
    words[6] = (uint16_t)(1u + au_rand(64));
    words[7] = 0x200;
    uint32_t formats = au_rand(3) ? 0xfffu : au_rand(0x1000);
    for (unsigned i = 0; i < ES2_SOS_CALL_COUNT; ++i) au_status[i] = 0xffffffffu;
    static const es2_sim_sos_call failing[] = {ES2_SOS_DETECT_INIT, ES2_SOS_DETECT_FIND_FIRST, ES2_SOS_DETECT_UNINIT, ES2_SOS_TIMER_INIT,
                                               ES2_SOS_INIT_DRIVER, ES2_SOS_UNINIT_DRIVER};
    if (au_rand(3) == 0) {
        es2_sim_sos_call f = failing[au_rand(6)];
        au_status[f] = f == ES2_SOS_TIMER_INIT && au_rand(2) ? 0x00010000u | au_rand(8) : 0x00020000u | au_rand(8);
    }
    int shut = au_rand(4) != 0;
    uint32_t window = 0x00010000u | au_rand(0x100), result[2][3], shutdown[2] = {0, 0};
    for (unsigned side = 0; side < 2u; ++side) {
        au_side(side, 0x01000000u);
        au_silent[side].formats = formats;
        BYTE *cfg = au_world;
        for (unsigned i = 0; i < 8u; ++i) put16(cfg + 2u * i, words[i]);
        uint32_t a[2] = {window, (uint32_t)(uintptr_t)cfg}, in[3] = {1, 2, 3};
        es2_sim_result r;
        if (!side) au_call((uint32_t)(uintptr_t)(mapped + 0x73708u), a, 2, in, result[0]);
        else {
            uint32_t at[2] = {a[0], a[1]};
            if (es2_sim_call(&au_dispatch, 0x00473708u, at, 2, in, &r)) au_bad = 1;
            result[1][0] = r.eax;
        }
        if (shut) {
            if (!side) { uint32_t o[3]; au_call((uint32_t)(uintptr_t)(mapped + 0x737a4u), NULL, 0, in, o); }
            else if (es2_sim_audio_internal(0x004737a4u, NULL, 0, in, &r)) au_bad = 1;
        }
        shutdown[side] = get16(mapped + 0x2a6a98u);
        if (au_bad) { fprintf(stderr, "FAIL: audio driver case %u: a call failed\n", k); return 1; }
        if (!side) au_keep_world();
        au_end(side);
    }
    if (result[0][0] != result[1][0] || shutdown[0] != shutdown[1]) {
        fprintf(stderr, "FAIL: audio driver case %u: EAX %08lx / %08lx\n", k, (unsigned long)result[0][0], (unsigned long)result[1][0]);
        return 1;
    }
    if (result[0][0]) { ++au_driver_ok; if (get16(mapped + 0x2b5590u + 0x4eu) == 0x15u) ++au_driver_fallback; }
    return au_compare("driver", k);
}

/* ---- manager */
#define AU_X 0x0000u
#define AU_RECORDS 0x0100u
#define AU_SLOTS 0x1400u
#define AU_SAMPLES 0x1800u
#define AU_NAMES 0x3000u
static const char *const au_sample_names[] = {"HMI\\a.wav", "HMI\\b.wav", "HMI\\c.wav", "HMI\\d.wav", "simvoc\\e.wav"};
static uint32_t au_va(uint32_t off) { return (uint32_t)(uintptr_t)au_world + off; }
static void au_build_world(uint32_t *records_out, uint32_t *slots_out) {
    memset(au_world, 0, AU_WORLD_SIZE);
    uint32_t records = 2u + au_rand(7), slots = 2u + au_rand(11), x = au_va(AU_X), cached = 0;
    for (unsigned i = 0; i < 5u; ++i) strcpy((char *)au_world + AU_NAMES + 0x20u * i, au_sample_names[i]);
    put32(au_world + 0x10u, records); put32(au_world + 0x14u, slots);
    put32(au_world + 0x18u, au_rand(3) ? 0 : au_rand(3) ? 2000000u : 1000u + au_rand(100000));
    put32(au_world + 0x20u, au_rand(0x100));
    put32(au_world + 0x34u, au_va(AU_RECORDS)); put32(au_world + 0x38u, au_va(AU_SLOTS));
    uint32_t handles[8];
    for (uint32_t j = 0; j < records; ++j) {
        BYTE *rec = au_world + AU_RECORDS + 540u * j, *R = rec + 4;
        handles[j] = 0xffffffffu;
        if (au_rand(4) == 0) { put32(rec, 0xffffffffu); continue; }
        handles[j] = (au_rand(0x100) << 16) | j;
        put32(rec, handles[j]);
        strcpy((char *)R, au_sample_names[au_rand(5)]);
        put32(R + 0x204, au_rand(4) ? 1u + au_rand(3) : 0);
        if (au_rand(3)) {
            uint32_t P = au_va(AU_SAMPLES + 0x100u * j), size = 1000u + au_rand(60000);
            put32(R + 0x208, 1); put32(R + 0x20c, size); put32(R + 0x210, P);
            BYTE *p = au_world + AU_SAMPLES + 0x100u * j;
            put32(p + 0x10, size); put32(p + 0x38, au_rand(2) ? 11025u : 22050u); put32(p + 0x3c, au_rand(2) ? 8u : 16u);
            put32(p + 0x40, 1u + au_rand(2)); put32(p + 0xfc, P);
            cached += size;
        } else put32(R + 0x20c, au_rand(2) ? 0 : 1000u + au_rand(9000));
    }
    put32(au_world + 0x1cu, cached);
    for (uint32_t i = 0; i < slots; ++i) {
        BYTE *s = au_world + AU_SLOTS + 40u * i, *S = s + 4;
        if (au_rand(3) == 0) { put32(s, 0xffffffffu); continue; }
        put32(s, (au_rand(0x100) << 16) | i);
        uint32_t r = au_rand(records + 1u);
        put32(S, r < records ? (au_rand(8) || handles[r] == 0xffffffffu ? handles[r] : (handles[r] ^ 0x10000u)) : 0xffffffffu);
        static const uint32_t flags[] = {0, 0x80u, 0x400u, 0x800u, 0x100u, 0x180u};
        put32(S + 4, flags[au_rand(6)]);
        put32(S + 8, au_rand(4) ? 5 : au_rand(300)); put32(S + 0xc, au_rand(4) ? 0 : au_rand(0x10000));
        put32(S + 0x10, au_rand(65001)); put32(S + 0x14, au_rand(4)); put32(S + 0x18, au_rand(0x10000)); put32(S + 0x1c, au_rand(0x20000));
        put32(S + 0x20, au_rand(32));
    }
    (void)x;
    *records_out = records; *slots_out = slots;
}
/* A handle for an operation: a used slot's, -1 or a stale one. */
static uint32_t au_pick_handle(uint32_t slots, uint32_t pick, uint32_t salt) {
    if (pick % 8u == 0) return 0xffffffffu;
    uint32_t i = pick % slots, h = get32(au_world + AU_SLOTS + 40u * i);
    if (pick % 8u == 1 && h != 0xffffffffu) return h ^ (salt << 16);
    return h;
}
#define AU_OPS 12u
static unsigned au_ops_seen[24], au_plays, au_evictions, au_opens, au_stops;
static int au_manager_case(unsigned k) {
    uint32_t records, slots;
    au_build_world(&records, &slots);
    memcpy(au_world_start, au_world, AU_WORLD_SIZE);
    for (unsigned i = 0; i < ES2_SOS_CALL_COUNT; ++i) au_status[i] = 0xffffffffu;
    uint32_t op[AU_OPS], pick[AU_OPS], value[AU_OPS], advance[AU_OPS], results[2][AU_OPS][3];
    for (unsigned i = 0; i < AU_OPS; ++i) {
        op[i] = au_rand(17); pick[i] = random32(); value[i] = random32(); advance[i] = au_rand(3) ? au_rand(200) : au_rand(100000);
    }
    memset(results, 0, sizeof(results));
    for (unsigned side = 0; side < 2u; ++side) {
        au_side(side, 0x01000000u);
        put16(mapped + 0x2cbd5cu, 1);   /* the Win32 driver word (as after the load) */
        uint32_t x = au_va(AU_X);
        put32(mapped + 0x9f8ecu, x);
        for (unsigned i = 0; i < 4u; ++i) put32(mapped + 0xd2c78u + 4u * i, au_pick_handle(slots, i + 2u, 0));
        for (unsigned i = 0; i < AU_OPS; ++i) {
            lc_vt.clock_ms += advance[i];
            uint32_t h = au_pick_handle(slots, pick[i], value[i] & 0xffu), in[3] = {value[i], value[i] ^ 0x5a5a5a5au, value[i] >> 3};
            uint32_t *out = results[side][i];
            es2_sim_result r;
            uint32_t va = 0, a[5] = {x, h, 0, 0, 0}, n = 2;
            int row_call = 1;
            switch (op[i]) {
            case 0: va = 0x00464618u; a[2] = value[i] % 70000u; n = 3; break;
            case 1: case 2:
                /* play only on a cached record (no sample load in vitro); else stop */
                va = 0x00464118u;
                if (h != 0xffffffffu) {
                    BYTE *s = au_world + AU_SLOTS + 40u * (h & 0xffffu);
                    uint32_t f = (h & 0xffffu) < 0x400u ? get32(s + 4) : 0xffffffffu;
                    if (f != 0xffffffffu && (f & 0xffffu) < records && get32(au_world + AU_RECORDS + 540u * (f & 0xffffu)) == f &&
                        get32(au_world + AU_RECORDS + 540u * (f & 0xffffu) + 4u + 0x208u))
                        va = 0x00464038u;
                }
                break;
            case 3: va = 0x00464118u; break;
            case 4: va = 0x0046466cu; a[2] = (value[i] >> 28) % 3u ? 0x10000u : value[i] % 0x30000u; n = 3; break;
            case 5: va = 0x004646dcu; a[2] = (value[i] >> 28) % 3u ? 0x8000u : value[i] & 0xffffu; n = 3; break;
            case 6: va = 0x00462af0u; a[0] = value[i] % 4u; n = 1; break;
            case 7: row_call = 0; va = 0x00463a14u; n = 5;
                a[1] = au_va(AU_NAMES + 0x20u * (value[i] % 5u)); a[2] = (value[i] >> 8) % 2u ? 1u : 3u; a[3] = value[i] >> 12; a[4] = value[i] >> 20;
                break;
            case 8: row_call = 0; va = 0x00463c50u; break;
            case 9: row_call = 0; va = 0x00463d4cu; a[2] = 0; n = 3; break;
            case 10: row_call = 0; va = 0x004641e8u; break;
            case 11: row_call = 0; va = 0x004647b8u; a[2] = value[i] % 400u; n = 3; break;
            case 12: row_call = 0; va = 0x0046474cu; a[2] = value[i] % 4u; n = 3; break;
            case 13: {
                /* The eviction reads its scoring loop's last slot pointer (f-0Ch) for the
                 * unload; with cached records but no used slot that local is never
                 * written (residue, the original faults): stop all instead. */
                int cached_any = 0, used_any = 0;
                for (uint32_t j = 0; j < records; ++j)
                    if (get32(au_world + AU_RECORDS + 540u * j) != 0xffffffffu && get32(au_world + AU_RECORDS + 540u * j + 4u + 0x208u)) cached_any = 1;
                for (uint32_t j = 0; j < slots; ++j) if (get32(au_world + AU_SLOTS + 40u * j) != 0xffffffffu) used_any = 1;
                row_call = 0;
                if (cached_any && !used_any) { va = 0x004648e0u; n = 1; }
                else { va = 0x00464390u; a[1] = value[i] % 80000u; }
                break;
            }
            case 14: row_call = 0; va = 0x004648e0u; n = 1; break;
            case 15: va = 0x00464038u; a[1] = ((value[i] >> 30) & 1u) || h == 0xffffffffu ? 0xffffffffu : (h ^ 0x10000u); break;   /* invalid play handles only */
            default: va = 0x00464618u; a[1] = 0xffffffffu; n = 3; break;
            }
            if (side) ++au_ops_seen[op[i]];
            if (!side) au_call((uint32_t)(uintptr_t)(mapped + (va - 0x400000u)), a, n, in, out);
            else if (row_call) {
                if (es2_sim_call(&au_dispatch, va, a, n, in, &r)) au_bad = 1;
                out[0] = r.eax; out[1] = r.edx; out[2] = r.ecx;
            } else {
                if (es2_sim_audio_internal(va, a, n, in, &r)) au_bad = 1;
                out[0] = r.eax;
            }
            if (!row_call && !side) { out[1] = out[2] = 0; }
            if (au_bad) { fprintf(stderr, "FAIL: audio manager case %u op %u (0x%08lx): a call failed\n", k, i, (unsigned long)va); return 1; }
            if (side && out[0] == 1u) { if (va == 0x00464038u) ++au_plays; if (va == 0x00464390u) ++au_evictions; if (va == 0x00464118u) ++au_stops; }
            if (side && va == 0x00463a14u && out[0] != 0xffffffffu) ++au_opens;
            if (op[i] == 6 && !side) out[1] = out[2] = 0;   /* is playing: EAX only */
            if (op[i] == 6 && side) out[1] = out[2] = 0;
            if (!row_call) out[0] = va == 0x004647b8u || va == 0x0046474cu || va == 0x00463c50u || va == 0x004648e0u ? 0 : out[0];
        }
        if (!side) au_keep_world();
        au_end(side);
    }
    for (unsigned i = 0; i < AU_OPS; ++i)
        if (memcmp(results[0][i], results[1][i], sizeof(results[0][i]))) {
            fprintf(stderr, "FAIL: audio manager case %u op %u (kind %lu): EAX %08lx / %08lx EDX %08lx / %08lx ECX %08lx / %08lx\n", k, i,
                    (unsigned long)op[i], (unsigned long)results[0][i][0], (unsigned long)results[1][i][0], (unsigned long)results[0][i][1],
                    (unsigned long)results[1][i][1], (unsigned long)results[0][i][2], (unsigned long)results[1][i][2]);
            return 1;
        }
    return au_compare("manager", k);
}

/* ---- timeline */
#define AU_VOICES 0x4000u
#define AU_LIPS 0x4200u
static unsigned au_voices_started;
static int au_timeline_case(unsigned k) {
    /* Five channels (42h bytes) with scripts in their lip buffers, a queue. */
    memset(au_world, 0, AU_WORLD_SIZE);
    for (unsigned i = 0; i < 5u; ++i) strcpy((char *)au_world + AU_NAMES + 0x20u * i, au_sample_names[i]);
    for (unsigned c = 0; c < 5u; ++c) {
        BYTE *ch = au_world + AU_VOICES + 0x42u * c, *lip = au_world + AU_LIPS + 100u * c;
        put32(ch, au_rand(3) ? 0xffffffffu : au_rand(2));   /* index within the 2-slot table (stale: the close returns) */
        put32(ch + 4, 0x00100000u + au_rand(0x100));
        put16(ch + 8, (uint16_t)au_rand(4));
        put32(ch + 0xa, au_va(AU_LIPS + 100u * c + au_rand(4)));
        put32(ch + 0x2f, au_va(AU_LIPS + 100u * c));
        if (au_rand(3) == 0) { strcpy((char *)ch + 0xe, au_sample_names[au_rand(5)]); put32(ch + 0x33, au_va(AU_VOICES + 0x42u * c + 0xeu)); }
        put32(ch + 0x37, au_rand(4) || !c ? au_rand(0x200000) : 0xffffffffu);   /* channel 0 below FFFFFFFFh: the LRU finds one (else the original writes through NULL) */
        put32(ch + 0x3e, au_rand(2) ? 0xffffffffu : au_rand(2));
        for (unsigned b = 0; b < 99u; ++b) lip[b] = (BYTE)(au_rand(6) == 0 ? 0xff : au_rand(5) == 0 ? au_rand(256) : au_rand(16));
        lip[99] = 0xff;
    }
    /* An empty context (2 records, 2 free slots) for the LRU channel's close. */
    put32(au_world + 0x10u, 2); put32(au_world + 0x14u, 2); put32(au_world + 0x34u, au_va(AU_RECORDS)); put32(au_world + 0x38u, au_va(AU_SLOTS));
    for (unsigned j = 0; j < 2u; ++j) { put32(au_world + AU_RECORDS + 540u * j, 0xffffffffu); put32(au_world + AU_SLOTS + 40u * j, 0xffffffffu); }
    uint32_t queued = au_rand(7), qtime[6], qch[6];
    for (uint32_t q = 0; q < 6u; ++q) { qtime[q] = 0x00100000u + au_rand(0x40); qch[q] = au_va(AU_VOICES + 0x42u * au_rand(5)); }
    memcpy(au_world_start, au_world, AU_WORLD_SIZE);
    uint32_t calls = 2u + au_rand(6), kind[8], arg[8], results[2][8][3];
    for (unsigned i = 0; i < calls; ++i) { kind[i] = au_rand(5); arg[i] = random32(); }
    uint32_t start = 0x01000000u + au_rand(0x1000);
    int with_context = au_rand(2);
    for (unsigned side = 0; side < 2u; ++side) {
        au_side(side, start);
        put32(mapped + 0xd2d6cu, au_va(AU_VOICES));
        put32(mapped + 0xd2d70u, au_va(AU_LIPS));
        put16(mapped + 0xd2d74u, 5);
        put32(mapped + 0x9f8ecu, with_context ? au_va(AU_X) : 0);
        mapped[0x9f966u] = 0;   /* the voice byte off: no sample file in vitro */
        put32(mapped + 0xd3b78u, 0);
        put16(mapped + 0x9f964u, (uint16_t)queued);
        for (uint32_t q = 0; q < queued; ++q) {
            put32(mapped + 0xd2e76u + 8u * q, qtime[q]);
            put32(mapped + 0xd2e7au + 8u * q, qch[q]);
            put32(mapped + 0xd2ec8u + 4u * q, 0x004d2e76u + 8u * q);
        }
        for (unsigned i = 0; i < calls; ++i) {
            uint32_t in[3] = {arg[i], arg[i] ^ 0x77u, arg[i] >> 5}, *out = results[side][i];
            uint32_t va, a[2] = {0, 0}, n = 0;
            switch (kind[i]) {
            case 0: case 1: va = 0x004632c4u; break;
            case 2: va = 0x00462b9cu; a[0] = au_va(AU_NAMES + 0x20u * (arg[i] % 5u)); n = 2; break;
            case 3: va = 0x00462940u; a[0] = au_va(AU_NAMES + 0x20u * (arg[i] % 5u)); a[1] = arg[i] >> 16; n = 2;
                if (with_context) va = 0x004632c4u, n = 0;   /* 0x462940 with a context calls sfx_voice -> play: covered by the manager */
                break;
            default: va = 0x0046356cu; a[0] = au_va(AU_VOICES + 0x42u * (arg[i] % 5u)); n = 1; break;
            }
            lc_vt.clock_ms += arg[i] % 64u;
            uint32_t queue_before = get16(mapped + 0x9f964u);
            if (!side) au_call((uint32_t)(uintptr_t)(mapped + (va - 0x400000u)), a, n, in, out);
            else {
                es2_sim_result r;
                if (es2_sim_call(&au_dispatch, va, a, n, in, &r)) au_bad = 1;
                out[0] = r.eax; out[1] = r.edx; out[2] = r.ecx;
            }
            if (va == 0x00462b9cu) { out[2] = 0; if (side) ++au_voices_started; }   /* ECX: the original's string copy; EDX the head */
            if (va == 0x00462b9cu || va == 0x00462940u) out[1] = 0;
            if (va == 0x0046356cu) out[1] = out[2] = 0;
            /* An empty queue: ECX is what GetTickCount left (the host's). */
            if (va == 0x004632c4u && !queue_before) out[2] = 0;
            if (au_bad) { fprintf(stderr, "FAIL: audio timeline case %u call %u (0x%08lx): a call failed\n", k, i, (unsigned long)va); return 1; }
        }
        if (!side) au_keep_world();
        au_end(side);
    }
    for (unsigned i = 0; i < calls; ++i)
        if (memcmp(results[0][i], results[1][i], sizeof(results[0][i]))) {
            fprintf(stderr, "FAIL: audio timeline case %u call %u (kind %lu): EAX %08lx / %08lx EDX %08lx / %08lx ECX %08lx / %08lx\n", k, i,
                    (unsigned long)kind[i], (unsigned long)results[0][i][0], (unsigned long)results[1][i][0], (unsigned long)results[0][i][1],
                    (unsigned long)results[1][i][1], (unsigned long)results[0][i][2], (unsigned long)results[1][i][2]);
            return 1;
        }
    return au_compare("timeline", k);
}

static int audio_cases(void) {
    current_function = "audio";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    au_world = VirtualAlloc(NULL, AU_WORLD_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    au_world_start = malloc(2u * AU_WORLD_SIZE);
    au_global = VirtualAlloc(NULL, AU_GLOBAL_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits || !au_world || !au_world_start || !au_global)
        return fail("audio: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("audio: the counter word is not free");
    su_pristine_take();
    static const struct { const char *dll, *name; void *stub; } stubs[] = {
        {"cw3220.dll", "_calloc", (void *)su_calloc}, {"cw3220.dll", "_free", (void *)su_free},
        {"cw3220.dll", "_memset", (void *)ic_memset}, {"cw3220.dll", "_strcat", (void *)ic_strcat},
        {"KERNEL32.dll", "GetTickCount", (void *)lc_tick}, {"KERNEL32.dll", "GetVersion", (void *)au_get_version},
        {"KERNEL32.dll", "LoadLibraryA", (void *)au_load_library}, {"KERNEL32.dll", "GetProcAddress", (void *)au_proc},
        {"KERNEL32.dll", "FreeLibrary", (void *)au_free_library}, {"KERNEL32.dll", "SetLastError", (void *)au_set_last_error},
        {"KERNEL32.dll", "GlobalAlloc", (void *)au_global_alloc}, {"KERNEL32.dll", "GlobalLock", (void *)au_global_lock},
        {"KERNEL32.dll", "GlobalUnlock", (void *)au_global_unlock}, {"KERNEL32.dll", "GlobalFree", (void *)au_global_free},
        {"WINMM.dll", "mciSendCommandA", (void *)au_mci},
    };
    for (unsigned i = 0; i < sizeof(stubs) / sizeof(stubs[0]); ++i)
        if (!ir_patch_import(stubs[i].dll, stubs[i].name, stubs[i].stub)) {
            fprintf(stderr, "import %s!%s\n", stubs[i].dll, stubs[i].name);
            return fail("audio: import slot not found");
        }
    if (au_dispatch_setup()) return 1;
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"driver", au_driver_case, 2000}, {"manager", au_manager_case, 4000}, {"timeline", au_timeline_case, 4000},
    };
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        for (unsigned k = 0; !bad && k < parts[p].cases; ++k) {
            current_case = k; current_case_valid = 1;
            bad = parts[p].run(k);
        }
        if (!bad) printf("PASS: audio %s: %u cases identical\n", parts[p].name, parts[p].cases);
    }
    if (!bad) printf("AUDIO coverage: %u driver loads succeeded (%u with the fallback format 15h); %u plays started, %u stops, %u opens, "
                     "%u evictions that freed enough; %u voice starts\n", au_driver_ok, au_driver_fallback, au_plays, au_stops, au_opens, au_evictions,
                     au_voices_started);
    current_case_valid = 0;
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
