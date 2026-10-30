/* DBSIM's audio as own code (es2/sim/audio.h). Contract: audio.md, "audio in
 * the own process" (every store and call below with its listing address range);
 * "listing" = llvm-objdump -d -M intel of the reference DBSIM.EXE (SHA-256
 * 8561aa47...c4ad). The sfx hooks (core/src/sfx.c) stay registry hooks; what
 * they and other code call of the audio code is a service row here. The
 * original's internal callees are own functions; hooks, other files'
 * services and the imports with a row are called through the dispatch.
 * SOS9503.DLL's entry points and mciSendCommandA go to the bound device.
 *
 * Layouts (audio.md "Sound data" and "audio in the own process"): the SFX
 * context X ([0x49f8ec], 0x4c bytes): +0x10 record capacity, +0x14 slot
 * capacity, +0x18 cache limit, +0x1c cached bytes, +0x20 handle generation,
 * +0x24 the sound.cfg settings, +0x34 records (540 bytes), +0x38 slots (40 bytes), +0x3c / +0x40
 * digital / MIDI playing counts, +0x44 the window. A record's body R =
 * record + 4: R+0 the name (512 bytes), +0x200 open argument, +0x204
 * reference count, +0x208 cached, +0x20c size, +0x210 the sample structure.
 * A slot's body S = slot + 4: S+0 file handle, +4 flags (1 MIDI, 0x80 loop
 * count set, 0x100 playing, 0x400 rate set, 0x800 pan set, 0x1000 stream),
 * +8 priority, +0xc callback, +0x10 volume, +0x14 loop count, +0x18 pan,
 * +0x1c rate, +0x20 the driver's sample handle. Handles are generation << 16
 * | index. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/sim/audio.h"
#include "es2/sim/loop.h"
#include "es2/sim/memory.h"
#include "es2/sim/services.h"

/* ---- guest VAs */
#define VA_COUNTER_POINTER 0x006cbee4u
#define VA_CONTEXT 0x0049f8ecu          /* the SFX context X (0: no sound) */
#define VA_GATE_LOW 0x0049f8f4u         /* rows 0..9 (and CD music) */
#define VA_CD_TRACK 0x0049f8fcu
#define VA_CD_LOOP 0x0049f900u          /* byte */
#define VA_CD_SAVED 0x0049f904u
#define VA_QUEUE_COUNT 0x0049f964u      /* word: the lip event queue */
#define VA_VOICE_ON 0x0049f966u         /* byte */
#define VA_NAMES 0x004d2a88u            /* row names */
#define VA_PARAMS 0x004d2b78u           /* row parameter pointers */
#define VA_STR_HOLDER 0x004d2c68u       /* the sounds.str holder */
#define VA_HANDLES 0x004d2c78u          /* row sample handles */
#define VA_ROW_COUNT 0x004d2d68u        /* word */
#define VA_VOICES 0x004d2d6cu
#define VA_LIPS 0x004d2d70u
#define VA_VOICE_COUNT 0x004d2d74u      /* word */
#define VA_HMI_PATH 0x004d2d76u         /* 0x46233c's buffer */
#define VA_QUEUE 0x004d2ec8u            /* record pointers */
#define VA_REPORT 0x004d2ef0u           /* 0x4649b8's text */
#define VA_LANGUAGE 0x004d2536u         /* byte: the language letter */
#define VA_HMX_OPTION 0x004d2680u       /* word: -l */
#define VA_CD_DEVICE 0x004a0e2cu        /* the MCI device (-1: none) */
#define VA_MASTER_VOLUME 0x004a0e30u
#define VA_CFG_RATE 0x004a0e44u         /* words: rate flag, 1, width flag, 0x200, buffers */
#define VA_CFG_BITS 0x004a0e46u
#define VA_CFG_WIDTH 0x004a0e48u
#define VA_CFG_BLOCK 0x004a0e4au
#define VA_CFG_BUFFERS 0x004a0e4cu
#define VA_STATUS 0x004a0e50u           /* the last driver status */
#define VA_DIGI_ON 0x004a0e54u          /* words: digital driver up, MIDI driver up, digital wanted, MIDI wanted */
#define VA_MIDI_ON 0x004a0e56u
#define VA_DIGI_WANTED 0x004a0e58u
#define VA_MIDI_WANTED 0x004a0e5au
#define VA_WINDOW 0x006b5588u
#define VA_CD_LENGTH 0x006b558cu
#define VA_CAPS 0x006b5590u             /* the digital driver's capability record */
#define VA_DRIVER 0x006b566eu           /* word: the digital driver handle */
#define VA_DRIVER_WINDOW 0x006b5674u
#define VA_DLL_COUNT 0x004a6a98u        /* word: the driver DLL's use count */
#define VA_DLL_HANDLE 0x006cbd58u
#define VA_DLL_WIN32 0x006cbd5cu        /* word: 1 = sos9503.dll (Win32), 0 = sos32s03.dll (Win32s) */
#define VA_COCKPIT 0x0049b088u
/* Callees (dispatch). */
#define VA_NEW_ARRAY 0x004774f0u
#define VA_DELETE 0x00477548u
#define VA_MEMSET 0x0049681au
#define VA_STRNCMP 0x00496778u
#define VA_STRLWR 0x0049678au
#define VA_STRSTR 0x00496784u
#define VA_STRCAT 0x004967eau
#define VA_VSPRINTF 0x0049683eu
#define VA_CLOCK 0x00467828u
#define VA_PATH_NAME 0x00492b90u        /* name.ext -> 0x6cbc0c */
#define VA_STR_OPEN 0x00453878u         /* the string holder (load.c's rows) */
#define VA_STR_NEXT 0x00453918u
#define VA_VOL_STREAM 0x00472f14u       /* the VOL stream constructor with the open (load.c's row) */
#define VA_STREAM_BASE 0x0047a888u      /* the stream base constructor (row) */
#define VA_STREAM_OPEN 0x004730e8u      /* hooks of the VOL stream */
#define VA_STREAM_CLOSE 0x0047307cu
#define VA_STREAM_SIZE 0x00473334u
#define VA_STREAM_DESTROY 0x00472f64u
#define VA_HANDLER_REMOVE 0x00466010u
#define VA_SFX_PLAY 0x00462830u         /* hooks of the sfx group */
#define VA_SFX_RATE 0x00463114u
#define VA_SFX_VOICE 0x00463400u
#define VA_SFX_LIP_DUE 0x004634a0u
#define VT_VOL_STREAM 0x004a0e10u

static es2_sim_audio *bound_audio;
void es2_sim_audio_bind(es2_sim_audio *audio) { bound_audio = audio; }

/* ---- the headless platform: GMEM_FIXED blocks in host memory */
static uint32_t host_global_alloc(void *user, uint32_t flags, uint32_t size) {
    (void)user; (void)flags;
    void *p = calloc(1, size ? size : 1u);
    return (uint32_t)(uintptr_t)p;
}
static uint32_t host_global_free(void *user, uint32_t handle) {
    (void)user;
    free((void *)(uintptr_t)handle);
    return 0;
}
void es2_sim_audio_headless(es2_sim_audio *audio, es2_sim_loop *loop, const es2_sim_audio_device *device) {
    memset(audio, 0, sizeof(*audio));
    audio->loop = loop;
    if (device) audio->device = *device;
    audio->platform.global_alloc = host_global_alloc;
    audio->platform.global_free = host_global_free;
    /* A Windows NT answer (bit 31 clear): 0x4958fd takes the sos9503.dll
     * branch and sets the Win32 word 0x6cbd5c, as on the recording machine. */
    audio->platform.version = 0x23f00206u;
}

/* ---- guest view and calls */
typedef struct ctx { es2_sim_audio *au; es2_sim_loop *l; const es2_guest_memory *m; } ctx;
static uint32_t r32(const ctx *c, uint32_t a) { return c->m->read32(c->m->context, a); }
static uint16_t r16(const ctx *c, uint32_t a) { return c->m->read16(c->m->context, a); }
static uint8_t r8(const ctx *c, uint32_t a) { return es2_sim_read8(c->m, a); }
static void w32(const ctx *c, uint32_t a, uint32_t v) { c->m->write32(c->m->context, a, v); }
static void w16(const ctx *c, uint32_t a, uint16_t v) { c->m->write16(c->m->context, a, v); }
static void w8(const ctx *c, uint32_t a, uint8_t v) { es2_sim_write8(c->m, a, v); }
static uint32_t sx16(uint32_t v) { return (uint32_t)(int32_t)(int16_t)(uint16_t)v; }
static void counter(const ctx *c, int32_t n) {
    uint32_t p = r32(c, VA_COUNTER_POINTER);
    w32(c, p, r32(c, p) + (uint32_t)n);
}
static int refuse(ctx *c, uint32_t va, const char *name, const char *reason) {
    c->au->refused_va = va;
    return es2_sim_loop_refuse(c->l, va, name, reason);
}
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
        c->au->refused_va = va;
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
/* A zero-filled block of the call area for the original's stack locals. */
static uint32_t frame_push(ctx *c, uint32_t bytes) {
    bytes = (bytes + 3u) & ~3u;
    if (c->l->stack_hi - c->l->stack_lo < bytes + 0x400u) return 0;
    c->l->stack_hi -= bytes;
    for (uint32_t i = 0; i < bytes; i += 4u) w32(c, c->l->stack_hi + i, 0);
    return c->l->stack_hi;
}
static void frame_pop(ctx *c, uint32_t top) { c->l->stack_hi = top; }
static uint32_t fs0_head(void) {
#if defined(__i386__)
    uint32_t v;
    __asm__ volatile("movl %%fs:0, %0" : "=r"(v));
    return v;
#else
    return 0;
#endif
}
static int serving(es2_sim_service_call *sc, ctx *c) {
    c->au = bound_audio;
    if (!c->au || !c->au->loop) { sc->refused = 1; sc->reason = "no audio layer bound"; return 1; }
    c->l = c->au->loop;
    c->m = c->l->memory;
    return 0;
}
static void failed(es2_sim_service_call *sc) { sc->refused = 1; sc->reason = "a call inside the service was refused"; }
#define SERVE(sc, c) ctx c; if (serving((sc), &c)) return

/* The device and the platform. A driver call returns EAX; EDX and ECX keep
 * the values they had at the stub's call (not modelled, audio.h). */
static uint32_t sos(ctx *c, es2_sim_sos_call k, const uint32_t *args, unsigned n) {
    const es2_sim_audio_device *d = &c->au->device;
    return d->sos ? d->sos(d->context, c->m, k, args, n) : 0u;
}
#define SOS(c, k, ...) sos((c), (k), (const uint32_t[]){__VA_ARGS__}, \
    (unsigned)(sizeof((const uint32_t[]){__VA_ARGS__}) / sizeof(uint32_t)))
static uint32_t mci(ctx *c, uint32_t device, uint32_t message, uint32_t flags, uint32_t params) {
    const es2_sim_audio_device *d = &c->au->device;
    return d->mci ? d->mci(d->context, c->m, device, message, flags, params) : 306u;
}
static void set_last_error(ctx *c, uint32_t e) { c->au->platform.last_error = e; ++c->au->platform.set_last_error; }
static uint32_t global_alloc(ctx *c, uint32_t flags, uint32_t size) {
    es2_sim_audio_platform *p = &c->au->platform;
    ++p->global_allocs;
    return p->global_alloc ? p->global_alloc(p->user, flags, size) : 0u;
}
static void global_free(ctx *c, uint32_t h) {
    es2_sim_audio_platform *p = &c->au->platform;
    ++p->global_frees;
    if (p->global_free && h) p->global_free(p->user, h);
}
/* The driver handle word as 0x474350 leaves it in AX. */
static uint32_t driver(const ctx *c) { return r16(c, VA_DRIVER); }

/* Slot and record addresses (the original's 40- and 540-byte strides). */
static uint32_t slot_of(const ctx *c, uint32_t x, uint32_t h) { return r32(c, x + 0x38u) + 40u * (h & 0xffffu); }
static uint32_t record_of(const ctx *c, uint32_t x, uint32_t f) { return r32(c, x + 0x34u) + 540u * (f & 0xffffu); }
/* A valid slot handle: the slot's body S, else 0 (the -1 and stale tests). */
static uint32_t slot_body(const ctx *c, uint32_t x, uint32_t h) {
    if (h == 0xffffffffu) return 0;
    uint32_t s = slot_of(c, x, h);
    return r32(c, s) == h ? s + 4u : 0u;
}
/* S's file record body R, else 0. */
static uint32_t record_body(const ctx *c, uint32_t x, uint32_t S) {
    uint32_t f = r32(c, S);
    if (f == 0xffffffffu) return 0;
    uint32_t rec = record_of(c, x, f);
    return r32(c, rec) == f ? rec + 4u : 0u;
}

/* ======================================================================
 * The driver DLL 0x4958fd (listing 0x4958fd..0x495a1e) and its release
 * 0x495a1f (0x495a1f..0x495a42). With the use count 0x4a6a98 nonzero only
 * the count + 1. Otherwise GetVersion: below 80000000h (NT) or a Windows 9x
 * major version of at least 4 takes sos9503.dll and sets the word 0x6cbd5c
 * to 1, else sos32s03.dll and 0; LoadLibraryA; failure 0x390000; the 97
 * GetProcAddress results into the slots of the name table 0x4a6a9c (36-byte
 * records); the count + 1; 0.
 * Own process: no DLL is loaded (the device stands for it): the module
 * handle 0x6cbd58 holds a stand-in and the 97 slots stay as they are (only
 * the stubs read them, and the own code calls the device instead). */
#define DLL_STAND_IN 0x50500000u
static uint32_t dll_load(ctx *c) {
    if (r16(c, VA_DLL_COUNT)) { w16(c, VA_DLL_COUNT, (uint16_t)(r16(c, VA_DLL_COUNT) + 1u)); return 0; }
    uint32_t v = c->au->platform.version;
    w16(c, VA_DLL_WIN32, (v < 0x80000000u || (uint8_t)v >= 4u) ? 1u : 0u);
    w32(c, VA_DLL_HANDLE, DLL_STAND_IN);
    w16(c, VA_DLL_COUNT, (uint16_t)(r16(c, VA_DLL_COUNT) + 1u));
    return 0;
}
static void dll_free(ctx *c) {
    w16(c, VA_DLL_COUNT, (uint16_t)(r16(c, VA_DLL_COUNT) - 1u));
    /* FreeLibrary at a count of 0: nothing is loaded. */
}

/* The driver init 0x473eac(device, midi, window, window2) (listing
 * 0x473eac..0x4741f8). Words 0x4a0e58 = (device != FFFFh), 0x4a0e5a = (midi
 * != FFFFh). Both callers pass midi FFFFh, so the MIDI halves
 * (0x473ef4..0x473f5c, 0x4740df..0x4741f2) never run: refused by name. The
 * digital half when 0x4a0e54 is 0 and 0x4a0e58 set: detect init (device,
 * 0), find first (0x6b5590), detect uninit (also after a failed find), each
 * status into 0x4a0e50 and nonzero = 1; init system (0, 0, not stored); timer
 * init (0) = status, nonzero with a high word other than 1 = 1. Then word
 * caps+0 = device, +70h = 1; ax = [0x4a0e46] | [0x4a0e48] | [0x4a0e44]
 * against caps+4Ch: all bits present: +4Eh = ax; else any of 15h: +4Eh =
 * 15h; else status = caps+4Ch | 1C0000h, returned. +56h = window, +5Ah =
 * window2, words +68h = 78h, +6Eh = [0x4a0e4a], +6Ah = 40h, +6Ch =
 * [0x4a0e4c]; init driver (caps, 0x6b566e) = status, nonzero: 0x4a0e54 = 0
 * and 1; [0x6b5674] = window, set active window (driver, window), 0x4a0e54 =
 * 1. EAX 0. */
static int driver_init(ctx *c, uint32_t device, uint32_t midi, uint32_t window, uint32_t window2, uint32_t *result) {
    const uint32_t b = VA_CAPS;
    uint32_t st;
    w16(c, VA_DIGI_WANTED, (uint16_t)device != 0xffffu ? 1u : 0u);
    w16(c, VA_MIDI_WANTED, (uint16_t)midi != 0xffffu ? 1u : 0u);
    if (!r16(c, VA_MIDI_ON) && r16(c, VA_MIDI_WANTED))
        return refuse(c, 0x00473f08u, "SOS MIDI driver init", "the MIDI half (both callers pass device FFFFh)");
    if (!r16(c, VA_DIGI_ON) && r16(c, VA_DIGI_WANTED)) {
        st = SOS(c, ES2_SOS_DETECT_INIT, device & 0xffffu, 0u);
        w32(c, VA_STATUS, st);
        if (st) { *result = 1; return 0; }
        st = SOS(c, ES2_SOS_DETECT_FIND_FIRST, b);
        w32(c, VA_STATUS, st);
        if (st) { sos(c, ES2_SOS_DETECT_UNINIT, NULL, 0); *result = 1; return 0; }
        st = sos(c, ES2_SOS_DETECT_UNINIT, NULL, 0);
        w32(c, VA_STATUS, st);
        if (st) { *result = 1; return 0; }
        SOS(c, ES2_SOS_INIT_SYSTEM, 0u, 0u);
        st = SOS(c, ES2_SOS_TIMER_INIT, 0u);
        w32(c, VA_STATUS, st);
        if (st && (st >> 16) != 1u) { *result = 1; return 0; }
        w16(c, b, (uint16_t)device);
        w16(c, b + 0x70u, 1);
        uint16_t want = (uint16_t)(r16(c, VA_CFG_BITS) | r16(c, VA_CFG_WIDTH) | r16(c, VA_CFG_RATE)), caps = r16(c, b + 0x4cu);
        if ((uint16_t)(want & caps) == want) w16(c, b + 0x4eu, want);
        else if (caps & 0x15u) w16(c, b + 0x4eu, 0x15u);
        else {
            st = (uint32_t)r16(c, b + 0x4cu) | 0x1c0000u;
            w32(c, VA_STATUS, st);
            *result = st;
            return 0;
        }
        w32(c, b + 0x56u, window);
        w32(c, b + 0x5au, window2);
        w16(c, b + 0x68u, 0x78u);
        w16(c, b + 0x6eu, r16(c, VA_CFG_BLOCK));
        w16(c, b + 0x6au, 0x40u);
        w16(c, b + 0x6cu, r16(c, VA_CFG_BUFFERS));
        st = SOS(c, ES2_SOS_INIT_DRIVER, b, VA_DRIVER);
        w32(c, VA_STATUS, st);
        if (st) { w16(c, VA_DIGI_ON, 0); *result = 1; return 0; }
        w32(c, VA_DRIVER_WINDOW, window);
        SOS(c, ES2_SOS_SET_ACTIVE_WINDOW, driver(c), window);
        w16(c, VA_DIGI_ON, 1);
    }
    if (!r16(c, VA_MIDI_ON) && r16(c, VA_MIDI_WANTED))
        return refuse(c, 0x004740fbu, "SOS MIDI driver init", "the MIDI half (both callers pass device FFFFh)");
    *result = 0;
    return 0;
}

/* The driver load 0x473708(window, settings) (listing 0x473708..0x4737a0;
 * audio.md, "audio in the own process"): GetVersion (result unused), [0x6b5588]
 * = window, the DLL 0x4958fd; on 0 the five settings words +0, +2, +4, +0Ch,
 * +0Eh to 0x4a0e44, 0x4a0e46, 0x4a0e48, 0x4a0e4c, 0x4a0e4a, then 0x473eac(word
 * +6, FFFFh, window, window); nonzero: once more with device 1; nonzero again:
 * the DLL released (0x495a1f). EAX = (the last result == 0). */
static int driver_load(ctx *c, uint32_t window, uint32_t cfg, uint32_t *ok) {
    w32(c, VA_WINDOW, window);
    uint32_t r = dll_load(c);
    if (!r) {
        w16(c, VA_CFG_RATE, r16(c, cfg));
        w16(c, VA_CFG_BITS, r16(c, cfg + 2u));
        w16(c, VA_CFG_WIDTH, r16(c, cfg + 4u));
        w16(c, VA_CFG_BUFFERS, r16(c, cfg + 0xcu));
        w16(c, VA_CFG_BLOCK, r16(c, cfg + 0xeu));
        if (driver_init(c, r16(c, cfg + 6u), 0xffffu, window, window, &r)) return 1;
        if (r) {
            if (driver_init(c, 1u, 0xffffu, window, window, &r)) return 1;
            if (r) dll_free(c);
        }
    }
    *ok = r == 0;
    return 0;
}
void es2_sim_service_sound_driver(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    uint32_t ok = 0;
    if (driver_load(&c, sc->slots[0], sc->slots[1], &ok)) { failed(sc); return; }
    /* EDX/ECX: the driver init's (only the own 0x463694 calls this row and
     * reads EAX alone). */
    sc->eax = ok; sc->edx = 0; sc->ecx = 0;
}

/* The driver shutdown 0x4737a4 (listing 0x4737a4..0x4737ae): 0x4741fc
 * (0x4741fc..0x474300): with 0x4a0e54 set: 0x4a0e54 = 0, stop all (driver),
 * set active window (driver, 0), uninit driver (driver) = status, nonzero:
 * return (1); timer uninit, uninit system; the MIDI half with 0x4a0e56 set
 * (never: refused). Then the DLL release 0x495a1f. */
static int driver_shutdown(ctx *c) {
    if (r16(c, VA_DIGI_ON)) {
        w16(c, VA_DIGI_ON, 0);
        SOS(c, ES2_SOS_STOP_ALL, driver(c));
        SOS(c, ES2_SOS_SET_ACTIVE_WINDOW, driver(c), 0u);
        uint32_t st = SOS(c, ES2_SOS_UNINIT_DRIVER, driver(c));
        w32(c, VA_STATUS, st);
        if (st) { dll_free(c); return 0; }
        sos(c, ES2_SOS_TIMER_UNINIT, NULL, 0);
        sos(c, ES2_SOS_UNINIT_SYSTEM, NULL, 0);
    }
    if (r16(c, VA_MIDI_ON)) return refuse(c, 0x0047425eu, "SOS MIDI driver shutdown", "the MIDI driver is never started");
    dll_free(c);
    return 0;
}
/* ======================================================================
 * The SOS glue below the SFX manager (listing 0x473828..0x473ba4). R is a file
 * record's body, S a slot's body. MIDI (flag 1) and stream (flag 0x1000) slots
 * never exist: 0x463a14 sets flag 1 only for open mode 0 and 0x1000 only for
 * mode 2, and the callers pass 1 (voices) or, from the table init, the residue
 * mode of 0x46254c, never 0 or 2 (audio.md, "audio in the own process"). Their
 * glue paths are refused by name. */
typedef struct rg { uint32_t eax, edx, ecx; } rg;
static int midi_refused(ctx *c, uint32_t va) { return refuse(c, va, "SOS MIDI glue", "a MIDI slot (flag 1; never opened, audio.md \"Audio in the own process\")"); }

/* 0x473898(R, S) (0x473898..0x4739aa): digital: the volume v = S+10h *
 * [0x4a0e30] (IMUL, 32 bits), * 32767 (shl 15, sub), DIV 10000; the sample
 * structure P = R+210h: P+30h = (MOVSX v) << 16 | MOVSX v, P+48h = S+18h (the
 * pan), P+34h = S+14h - 1 (or -1 for a loop count of 0), P+64h = S+0Ch (the
 * callback), dword S+20h = 0; start sample (driver, P, S+20h) = st; st
 * nonzero: EDX = ([0x4a0e50] != 0), failure only then. EAX = success, EDX
 * the callback (st 0) or that test, ECX 0. */
static int glue_start(ctx *c, uint32_t R, uint32_t S, rg *out) {
    uint32_t f = r32(c, S + 4u);
    if (f & 1u) return midi_refused(c, 0x004738a9u);
    if (f & 0x1000u) return refuse(c, 0x004738f9u, "SOS stream start 0x47473c", "a stream slot (flag 0x1000; never opened)");
    uint32_t v = r32(c, S + 0x10u) * r32(c, VA_MASTER_VOLUME);
    v = ((v << 15) - v) / 10000u;
    uint32_t p = r32(c, R + 0x210u), lo = sx16(v);
    w32(c, p + 0x30u, (lo << 16) | lo);
    w32(c, p + 0x48u, r32(c, S + 0x18u));
    w32(c, p + 0x34u, r32(c, S + 0x14u) ? r32(c, S + 0x14u) - 1u : 0xffffffffu);
    uint32_t cb = r32(c, S + 0xcu);
    w32(c, p + 0x64u, cb);
    w32(c, S + 0x20u, 0);
    uint32_t st = SOS(c, ES2_SOS_START_SAMPLE, driver(c), p, S + 0x20u), edx = cb;
    int bad = st != 0;
    if (bad) { edx = r32(c, VA_STATUS) != 0; bad = (int)edx; }
    out->eax = !bad; out->edx = edx; out->ecx = 0;
    return 0;
}
/* 0x4739ac(R, S) (0x4739ac..0x473a05): digital: stop sample (driver, word
 * S+20h). EAX 1; EDX/ECX the caller's. */
static int glue_stop(ctx *c, uint32_t S) {
    uint32_t f = r32(c, S + 4u);
    if (f & 1u) return midi_refused(c, 0x004739bau);
    if (f & 0x1000u) return refuse(c, 0x004739e0u, "SOS stream stop 0x4749d0", "a stream slot (flag 0x1000; never opened)");
    SOS(c, ES2_SOS_STOP_SAMPLE, driver(c), r16(c, S + 0x20u));
    return 0;
}
/* 0x473a5c(S) (0x473a5c..0x473a88): digital: sample done (driver, word
 * S+20h); EAX its answer. */
static int glue_done(ctx *c, uint32_t S, uint32_t *eax) {
    if (r32(c, S + 4u) & 1u) return midi_refused(c, 0x00473a69u);
    *eax = SOS(c, ES2_SOS_SAMPLE_DONE, driver(c), r16(c, S + 0x20u));
    return 0;
}
/* 0x473aec(S) (0x473aec..0x473b50): digital: EDX:EAX = S+10h * [0x4a0e30]
 * (one-operand IMUL), v = ((low << 15) - low) DIV 10000 (EDX the remainder),
 * set sample volume (driver, word S+20h, (MOVSX v) << 16 | MOVSX v). EAX its
 * answer, EDX = remainder high word | S+20h, ECX 10000. */
static int glue_volume(ctx *c, uint32_t S, rg *out) {
    if (r32(c, S + 4u) & 1u) return midi_refused(c, 0x00473afbu);
    uint32_t lo = r32(c, S + 0x10u) * r32(c, VA_MASTER_VOLUME);
    lo = (lo << 15) - lo;
    uint32_t q = lo / 10000u, rem = lo % 10000u, e = sx16(q);
    uint16_t sh = r16(c, S + 0x20u);
    out->eax = SOS(c, ES2_SOS_SET_VOLUME, driver(c), sh, (e << 16) | e);
    out->edx = (rem & 0xffff0000u) | sh;
    out->ecx = 10000u;
    return 0;
}
/* 0x473b54(S) (0x473b54..0x473b7e): digital: set sample rate (driver, word
 * S+20h, word (S+1Ch * 2B11h) >> 16); MIDI: nothing. EDX = the entry EDX's
 * high word | S+20h. */
static void glue_rate(ctx *c, uint32_t S, uint32_t edx_in, rg *out) {
    if (r32(c, S + 4u) & 1u) return;
    uint16_t sh = r16(c, S + 0x20u);
    out->eax = SOS(c, ES2_SOS_SET_RATE, driver(c), sh, ((r32(c, S + 0x1cu) * 0x2b11u) >> 16) & 0xffffu);
    out->edx = (edx_in & 0xffff0000u) | sh;
}
/* 0x473b80(S) (0x473b80..0x473ba4): digital: set pan location (driver, word
 * S+20h, word S+18h); MIDI: nothing. */
static void glue_pan(ctx *c, uint32_t S, uint32_t edx_in, rg *out) {
    if (r32(c, S + 4u) & 1u) return;
    uint16_t sh = r16(c, S + 0x20u);
    out->eax = SOS(c, ES2_SOS_SET_PAN, driver(c), sh, r16(c, S + 0x18u));
    out->edx = (edx_in & 0xffff0000u) | sh;
}

/* A VOL stream on the original's frame without the open: 0x472e6c(S)
 * (0x472e6c..0x472eb1): the base 0x47a888 (row), [S] = 0x4a0e10, +27h, +2Bh,
 * +0Ah, +0Eh, +16h = 0, word +8 = FFFFh, [S+4] = 5, counter + 1. */
static int stream_ctor(ctx *c, uint32_t s) {
    if (CALL(c, VA_STREAM_BASE, NULL, s)) return 1;
    w32(c, s, VT_VOL_STREAM);
    w32(c, s + 0x27u, 0); w32(c, s + 0x2bu, 0);
    w32(c, s + 0xau, 0); w32(c, s + 0xeu, 0); w32(c, s + 0x16u, 0);
    w16(c, s + 8u, 0xffffu);
    w32(c, s + 4u, 5);
    counter(c, 1);
    return 0;
}
/* The stream's read virtual (vtable slot +18h; 0x4735b8, hook stream_read). */
static int stream_read(ctx *c, uint32_t s, uint32_t n, uint32_t dst) {
    return CALL(c, r32(c, r32(c, s) + 0x18u), NULL, s, n, dst);
}

/* The digital sample load 0x474360(name, out) (0x474360..0x474506), inside
 * an exception frame: a VOL stream T on f-30h (0x472e6c), the hook open
 * (T, name): AL 0: destroy (T, 2), 1. n = size (hook); B = GlobalAlloc(40h,
 * n + 100h): 0: destroy, 1. P = GlobalLock(B) (= B, GMEM_FIXED); the read
 * virtual (T, n, P+100h); close (hook); _memset(P, 0, FCh); P+FCh = B.
 * _strncmp(P+100h, "RIFF", 4) = 0: the WAV header Q = P+100h: P+10h =
 * [Q+28h] - 2Ch, P+3Ch = MOVSX word Q+22h, P+40h = MOVSX word Q+16h, P+44h
 * = 8000h for 8 bits else 0, P+38h = [Q+18h], [P] = P+12Ch; otherwise raw:
 * P+10h = n, +3Ch = 8, +44h = 8000h, +40h = 1, +38h = 2B11h, [P] = Q. P+48h
 * = 8000h, P+30h = 7FFF7FFFh; the prepare stub 0x496271([P], n, P+4): with
 * the Win32 word 0x6cbd5c set it stores [P] into P+4 without the DLL (the
 * other branch would call it: refused). *out = P; destroy (T, 2); 0. */
static int sample_load(ctx *c, uint32_t name, uint32_t out, uint32_t *result) {
    uint32_t top = c->l->stack_hi, f = frame_push(c, 0x50u);
    if (!f) return refuse(c, 0x00474360u, "sample load", "the own code's call area is exhausted");
    uint32_t t = f + 0x50u - 0x30u, ok = 0, n = 0;
    int bad = stream_ctor(c, t) || CALL(c, VA_STREAM_OPEN, &ok, t, name);
    if (!bad && (ok & 0xffu)) bad = CALL(c, VA_STREAM_SIZE, &n, t);
    if (!bad && (ok & 0xffu)) {
        uint32_t b = global_alloc(c, 0x40u, n + 0x100u);
        if (!b) *result = 1;
        else {
            uint32_t p = b, q = p + 0x100u, cmp = 0;
            bad = stream_read(c, t, n, q) || CALL(c, VA_STREAM_CLOSE, NULL, t) || CALL(c, VA_MEMSET, NULL, p, 0u, 0xfcu) ||
                  CALL(c, VA_STRNCMP, &cmp, q, 0x004a0eb9u, 4u);
            if (!bad) {
                w32(c, p + 0xfcu, b);
                if (!cmp) {
                    w32(c, p + 0x10u, r32(c, q + 0x28u) - 0x2cu);
                    w32(c, p + 0x3cu, sx16(r16(c, q + 0x22u)));
                    w32(c, p + 0x40u, sx16(r16(c, q + 0x16u)));
                    w32(c, p + 0x44u, r16(c, q + 0x22u) == 8u ? 0x8000u : 0u);
                    w32(c, p + 0x38u, r32(c, q + 0x18u));
                    w32(c, p, p + 0x12cu);
                } else {
                    w32(c, p + 0x10u, n);
                    w32(c, p + 0x3cu, 8); w32(c, p + 0x44u, 0x8000u); w32(c, p + 0x40u, 1); w32(c, p + 0x38u, 0x2b11u);
                    w32(c, p, q);
                }
                w32(c, p + 0x48u, 0x8000u);
                w32(c, p + 0x30u, 0x7fff7fffu);
                if (!r16(c, VA_DLL_WIN32)) bad = refuse(c, 0x0049628eu, "sosPrepare32Memory", "the Win32s driver branch (the word 0x6cbd5c is 0)");
                else w32(c, p + 4u, r32(c, p));
                if (!bad) { w32(c, out, p); *result = 0; }
            }
        }
    } else if (!bad) *result = 1;
    if (!bad) bad = CALL(c, VA_STREAM_DESTROY, NULL, t, 2u);
    frame_pop(c, top);
    return bad;
}
/* 0x474508(P) (0x474508..0x474538): P nonzero: the unprepare stub (0 without
 * the DLL), GlobalUnlock and GlobalFree of P+FCh. */
static int sample_unload(ctx *c, uint32_t p) {
    if (!p) return 0;
    if (!r16(c, VA_DLL_WIN32)) return refuse(c, 0x004962b5u, "sosUnPrepare32Memory", "the Win32s driver branch");
    global_free(c, r32(c, p + 0xfcu));
    return 0;
}
/* 0x473828(R, index, load, midi) (0x473828..0x473894): load: the sample
 * load into R+210h (MIDI: 0x47465c, refused); unload: 0x474508([R+210h])
 * (MIDI 0x474708, refused), R+210h = 0. EAX = (the result == 0). */
static int glue_cache(ctx *c, uint32_t R, uint32_t load, uint32_t midi, uint32_t *ok) {
    if (midi) return refuse(c, load ? 0x0047383cu : 0x00473864u, "SOS MIDI bank", "a MIDI record (never opened)");
    uint32_t r = 0;
    if (load) { if (sample_load(c, R, R + 0x210u, &r)) return 1; }
    else { if (sample_unload(c, r32(c, R + 0x210u))) return 1; w32(c, R + 0x210u, 0); }
    *ok = r == 0;
    return 0;
}
/* ======================================================================
 * The SFX manager (listing 0x463a14..0x4649b7). */

/* The error report 0x4649b8(format, ...) (0x4649b8..0x4649cf): _vsprintf
 * into 0x4d2ef0 with the arguments after the format (no other effect). */
static int report(ctx *c, uint32_t format, uint32_t arg) {
    uint32_t top = c->l->stack_hi, f = frame_push(c, 4u);
    if (!f) return refuse(c, 0x004649b8u, "sfx report", "the own code's call area is exhausted");
    w32(c, f, arg);
    int bad = CALL(c, VA_VSPRINTF, NULL, VA_REPORT, format, f);
    frame_pop(c, top);
    es2_sim_audio_platform *p = &c->au->platform;
    ++p->error_reports;
    unsigned i = 0;
    for (; i + 1 < sizeof(p->last_report) && r8(c, VA_REPORT + i); ++i) p->last_report[i] = (char)r8(c, VA_REPORT + i);
    p->last_report[i] = 0;
    return bad;
}

/* open 0x463a14(X, name, mode, a, b) (0x463a14..0x463c4d): the first slot
 * with handle -1 (unsigned walk to X+14h) gets h = generation++ << 16 |
 * index; none: SetLastError(0Eh), -1. S: +4 = 0, +8 = 5, +0Ch = 0, +10h =
 * 100, +14h = 1, +18h = 8000h, +1Ch = 10000h, +0 = -1. A used record (to
 * X+10h) whose name equals: S+0 = its handle, mode 0 sets flag 1, its
 * reference count + 1, h. Otherwise the first free record gets f =
 * generation++ << 16 | index (none: the slot freed again, SetLastError(0Eh),
 * -1), S+0 = f; mode 0: flag 1, mode 2: flag 1000h; the name copied, R+200h
 * = a, +204h = 1, +208h = 0, +20Ch = b, +210h = 0. h. */
static int sfx_open(ctx *c, uint32_t x, uint32_t name, uint32_t mode, uint32_t a, uint32_t b, uint32_t *handle) {
    uint32_t h = 0xffffffffu, S = 0;
    for (uint32_t i = 0; i < r32(c, x + 0x14u); ++i) {
        uint32_t s = r32(c, x + 0x38u) + 40u * i;
        if (r32(c, s) != 0xffffffffu) continue;
        uint32_t g = r32(c, x + 0x20u);
        w32(c, x + 0x20u, g + 1u);
        h = (g << 16) + i;
        w32(c, s, h);
        S = r32(c, x + 0x38u) + 40u * i + 4u;
        break;
    }
    if (h == 0xffffffffu) { set_last_error(c, 0xe); *handle = 0xffffffffu; return 0; }
    w32(c, S + 4u, 0); w32(c, S + 8u, 5); w32(c, S + 0xcu, 0); w32(c, S + 0x10u, 100); w32(c, S + 0x14u, 1);
    w32(c, S + 0x18u, 0x8000u); w32(c, S + 0x1cu, 0x10000u); w32(c, S, 0xffffffffu);
    uint32_t R = 0;
    for (uint32_t j = 0; j < r32(c, x + 0x10u); ++j) {
        uint32_t rec = r32(c, x + 0x34u) + 540u * j;
        if (r32(c, rec) == 0xffffffffu) continue;
        uint32_t k = 0;
        uint8_t p, q;
        do { p = r8(c, rec + 4u + k); q = r8(c, name + k); ++k; } while (p == q && p);
        if (p != q) continue;
        w32(c, S, r32(c, rec));
        R = rec + 4u;
        if (!mode) w32(c, S + 4u, r32(c, S + 4u) | 1u);
        break;
    }
    if (r32(c, S) != 0xffffffffu) {
        w32(c, R + 0x204u, r32(c, R + 0x204u) + 1u);
        *handle = h;
        return 0;
    }
    uint32_t f = 0xffffffffu;
    for (uint32_t j = 0; j < r32(c, x + 0x10u); ++j) {
        uint32_t rec = r32(c, x + 0x34u) + 540u * j;
        if (r32(c, rec) != 0xffffffffu) continue;
        uint32_t g = r32(c, x + 0x20u);
        w32(c, x + 0x20u, g + 1u);
        f = (g << 16) + j;
        w32(c, rec, f);
        w32(c, S, f);
        R = rec + 4u;
        break;
    }
    if (f == 0xffffffffu) {
        w32(c, slot_of(c, x, h), 0xffffffffu);
        set_last_error(c, 0xe);
        *handle = 0xffffffffu;
        return 0;
    }
    if (mode == 0) w32(c, S + 4u, r32(c, S + 4u) | 1u);
    else if (mode == 2) w32(c, S + 4u, r32(c, S + 4u) | 0x1000u);
    uint32_t k = 0;
    uint8_t v;
    do { v = r8(c, name + k); w8(c, R + k, v); ++k; } while (v);
    w32(c, R + 0x200u, a); w32(c, R + 0x204u, 1); w32(c, R + 0x208u, 0); w32(c, R + 0x20cu, b); w32(c, R + 0x210u, 0);
    *handle = h;
    return 0;
}

static int sfx_cache(ctx *c, uint32_t x, uint32_t h, uint32_t load, uint32_t *result);
static int sfx_stop(ctx *c, uint32_t x, uint32_t h, rg *out);

/* close 0x463c50(X, h) (0x463c50..0x463d48): a valid slot whose record is
 * valid: playing (flag 100h): stop; the record's reference count nonzero:
 * - 1, reaching 0: uncache (X, h, 0) and the record freed; the slot's file
 * handle and the slot freed. */
static int sfx_close(ctx *c, uint32_t x, uint32_t h) {
    uint32_t S = slot_body(c, x, h);
    if (!S || r32(c, S) == 0xffffffffu) return 0;
    uint32_t f = r32(c, S), rec = record_of(c, x, f);
    if (r32(c, rec) != f) return 0;
    uint32_t R = rec + 4u, r;
    rg g;
    if ((r32(c, S + 4u) & 0x100u) && sfx_stop(c, x, h, &g)) return 1;
    if (r32(c, R + 0x204u)) {
        w32(c, R + 0x204u, r32(c, R + 0x204u) - 1u);
        if (!r32(c, R + 0x204u)) {
            if (sfx_cache(c, x, h, 0, &r)) return 1;
            w32(c, record_of(c, x, f), 0xffffffffu);
        }
    }
    w32(c, S, 0xffffffffu);
    w32(c, slot_of(c, x, h), 0xffffffffu);
    return 0;
}

/* The original reads, writes or calls through NULL at va (es2_sim_loop_null_refuse). */
static int null_refuse(ctx *c, uint32_t va, const char *name, const char *reason) {
    c->au->refused_va = va;
    return es2_sim_loop_null_refuse(c->l, va, name, reason);
}

/* The eviction 0x464390(X, need) (0x464390..0x4645da): a score per record
 * (new[](4 X+10h)): a used, cached record: 10 * references + 1, plus for
 * each slot on it priority + 1000 (playing, byte S+5 bit 0) + 10000 (loop
 * count set and playing); a stream slot makes it 0. Then, while less than `need` is
 * freed, the lowest nonzero score below 100000 (unsigned, the first on a
 * tie) is unloaded: score 0; a score of 1000 or more first stops every slot
 * on the record; the unload (the MIDI flag of the scoring loop's last slot,
 * as the original reads its stale local), R+208h = 0, X+1Ch -= size, freed
 * += size. The scores deleted. EAX = (freed >= need). */
static int sfx_evict(ctx *c, uint32_t x, uint32_t need, uint32_t *result) {
    uint32_t scores = 0, last_slot = 0;
    if (CALL(c, VA_NEW_ARRAY, &scores, r32(c, x + 0x10u) << 2)) return 1;
    if (!scores) return null_refuse(c, 0x004643a9u, "sfx evict", "new[] returned NULL (the original faults)");
    for (uint32_t j = 0; j < r32(c, x + 0x10u); ++j) {
        uint32_t e = scores + 4u * j;
        w32(c, e, 0);
        uint32_t rec = r32(c, x + 0x34u) + 540u * j, R = rec + 4u;
        if (r32(c, rec) == 0xffffffffu || !r32(c, R + 0x208u)) continue;
        w32(c, e, 10u * r32(c, R + 0x204u) + 1u);
        for (uint32_t i = 0; i < r32(c, x + 0x14u); ++i) {
            uint32_t s = r32(c, x + 0x38u) + 40u * i;
            if (r32(c, s) == 0xffffffffu) continue;
            uint32_t S = s + 4u;
            last_slot = S;
            if (r32(c, rec) != r32(c, S)) continue;
            uint32_t fl = r32(c, S + 4u);
            if (fl & 0x1000u) { w32(c, e, 0); break; }
            uint32_t add = r32(c, S + 8u) + ((fl & 0x100u) ? 1000u : 0u) + (((fl & 0x80u) && (fl & 0x100u)) ? 10000u : 0u);
            w32(c, e, r32(c, e) + add);
        }
    }
    uint32_t freed = 0;
    while (freed < need) {
        uint32_t best = 0xffffffffu, low = 100000u;
        for (uint32_t j = 0; j < r32(c, x + 0x10u); ++j) {
            uint32_t v = r32(c, scores + 4u * j);
            if (v && low > v) { low = v; best = j; }
        }
        if (best == 0xffffffffu) break;
        w32(c, scores + 4u * best, 0);
        uint32_t rec = r32(c, x + 0x34u) + 540u * best, R = rec + 4u;
        if (low >= 1000u)
            for (uint32_t i = 0; i < r32(c, x + 0x14u); ++i) {
                uint32_t s = r32(c, x + 0x38u) + 40u * i;
                if (r32(c, s) == 0xffffffffu || r32(c, s + 4u) != r32(c, rec)) continue;
                rg g;
                if (sfx_stop(c, x, r32(c, s), &g)) return 1;
            }
        uint32_t ok;
        if (glue_cache(c, R, 0, last_slot ? (r32(c, last_slot + 4u) & 1u) : 0u, &ok)) return 1;
        w32(c, R + 0x208u, 0);
        w32(c, x + 0x1cu, r32(c, x + 0x1cu) - r32(c, R + 0x20cu));
        freed += r32(c, R + 0x20cu);
    }
    if (CALL(c, VA_DELETE, NULL, scores)) return 1;
    *result = freed >= need;
    return 0;
}

/* cache 0x463d4c(X, h, load) (0x463d4c..0x464037), inside an exception
 * frame (EDX at every return: the head at entry). Invalid slot or record:
 * 0; a stream slot: 1; R+208h equal to `load`: 1. Unload: references above
 * 1: SetLastError(20h), 0; else the glue unload, R+208h = 0, X+1Ch -=
 * size, size = 0, 1. Load: the VOL stream T on f-30h (0x472f14 with the
 * open of R); [T+4] nonzero: report "SFX::cache: Unable to open file %s",
 * SetLastError(2), destroy (T, 2), 0. Size 0: the hook's size; still 0:
 * SetLastError(26h), CloseHandle of the uninitialised local f-54h (refused
 * by name: residue), destroy, 0. Close (hook); with a limit X+18h and need =
 * size + X+1Ch - limit > 0 (signed): the eviction, 0: SetLastError(0Eh),
 * destroy, 0. R+208h = 0, the glue load: success: R+208h = 1, X+1Ch +=
 * size; R+208h still 0: report "SFX::cache: Unable to read sample %s.",
 * SetLastError(26h), size = 0. EAX R+208h; destroy (T, 2). */
static int sfx_cache(ctx *c, uint32_t x, uint32_t h, uint32_t load, uint32_t *result) {
    *result = 0;
    uint32_t S = slot_body(c, x, h);
    if (!S) return 0;
    if (r32(c, S + 4u) & 0x1000u) { *result = 1; return 0; }
    uint32_t R = record_body(c, x, S);
    if (!R) return 0;
    if (r32(c, R + 0x208u) == load) { *result = 1; return 0; }
    uint32_t ok = 0;
    if (!load) {
        if (r32(c, R + 0x204u) > 1u) { set_last_error(c, 0x20); return 0; }
        if (glue_cache(c, R, 0, r32(c, S + 4u) & 1u, &ok)) return 1;
        w32(c, R + 0x208u, 0);
        w32(c, x + 0x1cu, r32(c, x + 0x1cu) - r32(c, R + 0x20cu));
        w32(c, R + 0x20cu, 0);
        *result = 1;
        return 0;
    }
    uint32_t top = c->l->stack_hi, f = frame_push(c, 0x58u);
    if (!f) return refuse(c, 0x00463d4cu, "sfx cache", "the own code's call area is exhausted");
    uint32_t t = f + 0x58u - 0x30u, n = 0;
    int bad = CALL(c, VA_VOL_STREAM, NULL, t, R);
    if (!bad && r32(c, t + 4u)) {
        bad = report(c, 0x0049fc8bu, R);
        set_last_error(c, 2);
    } else if (!bad) {
        if (!r32(c, R + 0x20cu)) {
            bad = CALL(c, VA_STREAM_SIZE, &n, t);
            w32(c, R + 0x20cu, n);
            if (!bad && !n) {
                set_last_error(c, 0x26);
                bad = refuse(c, 0x00463f2fu, "sfx cache", "CloseHandle of the uninitialised local f-54h (an empty sample file)");
            }
        }
        if (!bad && r32(c, R + 0x20cu)) bad = CALL(c, VA_STREAM_CLOSE, NULL, t);
        int go = !bad && r32(c, R + 0x20cu);
        uint32_t limit = r32(c, x + 0x18u);
        if (go && limit) {
            int32_t over = (int32_t)(r32(c, R + 0x20cu) + r32(c, x + 0x1cu) - limit);
            if (over > 0) {
                uint32_t room = 0;
                bad = sfx_evict(c, x, (uint32_t)over, &room);
                if (!bad && !room) { set_last_error(c, 0xe); go = 0; }
            }
        }
        if (go && !bad) {
            w32(c, R + 0x208u, 0);
            bad = glue_cache(c, R, 1, r32(c, S + 4u) & 1u, &ok);
            if (!bad && ok) { w32(c, R + 0x208u, 1); w32(c, x + 0x1cu, r32(c, x + 0x1cu) + r32(c, R + 0x20cu)); }
            if (!bad && !r32(c, R + 0x208u)) {
                bad = report(c, 0x0049fccau, R);
                set_last_error(c, 0x26);
                w32(c, R + 0x20cu, 0);
            }
            if (!bad) *result = r32(c, R + 0x208u);
        }
    }
    if (!bad) bad = CALL(c, VA_STREAM_DESTROY, NULL, t, 2u);
    frame_pop(c, top);
    return bad;
}

/* play 0x464038(X, h) (0x464038..0x464115; ECX at return: the entry ECX,
 * or the record index once read): invalid slot, file handle or record: 0;
 * not cached: cache (X, h, 1), 0: 0 (EDX the cache's); the glue start;
 * success: the MIDI or digital playing count + 1, flag 100h, 1. */
static int sfx_play(ctx *c, uint32_t x, uint32_t h, uint32_t ecx_in, rg *out) {
    out->eax = 0; out->edx = h; out->ecx = ecx_in;
    if (h == 0xffffffffu) return 0;
    uint32_t s = slot_of(c, x, h);
    if (r32(c, s) != h) return 0;
    uint32_t S = s + 4u, f = r32(c, S);
    if (f == 0xffffffffu) return 0;
    uint32_t rec = record_of(c, x, f);
    out->ecx = f & 0xffffu;
    if (r32(c, rec) != f) return 0;
    uint32_t R = rec + 4u, r;
    if (!r32(c, R + 0x208u)) {
        if (sfx_cache(c, x, h, 1, &r)) return 1;
        out->edx = fs0_head();
        if (!r) return 0;
    }
    rg g;
    if (glue_start(c, R, S, &g)) return 1;
    out->edx = g.edx;
    if (!g.eax) return 0;
    if (r32(c, S + 4u) & 1u) w32(c, x + 0x40u, r32(c, x + 0x40u) + 1u);
    else w32(c, x + 0x3cu, r32(c, x + 0x3cu) + 1u);
    w32(c, S + 4u, r32(c, S + 4u) | 0x100u);
    out->eax = 1;
    return 0;
}

/* stop 0x464118(X, h) (0x464118..0x4641e4): -1: 0 (the entry EDX/ECX);
 * stale: 0 (EDX the index, ECX the slot table); not playing: 1; invalid
 * file handle or record: 0 (EDX/ECX the record index and 135 times it);
 * the glue stop, the playing count - 1, flag 100h cleared, 1. */
static int sfx_stop_regs(ctx *c, uint32_t x, uint32_t h, uint32_t edx_in, uint32_t ecx_in, rg *out) {
    out->eax = 0; out->edx = edx_in; out->ecx = ecx_in;
    if (h == 0xffffffffu) return 0;
    uint32_t i = h & 0xffffu, base = r32(c, x + 0x38u);
    out->edx = i; out->ecx = base;
    if (r32(c, base + 40u * i) != h) return 0;
    uint32_t S = base + 40u * i + 4u;
    if (!(r32(c, S + 4u) & 0x100u)) { out->eax = 1; return 0; }
    uint32_t f = r32(c, S);
    if (f == 0xffffffffu) return 0;
    uint32_t fi = f & 0xffffu;
    out->edx = fi; out->ecx = 135u * fi;
    if (r32(c, record_of(c, x, f)) != f) return 0;
    if (glue_stop(c, S)) return 1;
    if (r32(c, S + 4u) & 1u) w32(c, x + 0x40u, r32(c, x + 0x40u) - 1u);
    else w32(c, x + 0x3cu, r32(c, x + 0x3cu) - 1u);
    w32(c, S + 4u, r32(c, S + 4u) & 0xfffffeffu);
    out->eax = 1;
    return 0;
}
static int sfx_stop(ctx *c, uint32_t x, uint32_t h, rg *out) { return sfx_stop_regs(c, x, h, 0, 0, out); }

/* done 0x4641e8(X, h) (0x4641e8..0x46425d): invalid: 1; a playing slot:
 * the glue's done answer nonzero: stop, 1; zero: 0; not playing: 1. */
static int sfx_done(ctx *c, uint32_t x, uint32_t h, uint32_t *result) {
    uint32_t S = slot_body(c, x, h);
    *result = 1;
    if (!S || !(r32(c, S + 4u) & 0x100u)) return 0;
    uint32_t d = 0;
    if (glue_done(c, S, &d)) return 1;
    if (!d) { *result = 0; return 0; }
    rg g;
    return sfx_stop(c, x, h, &g);
}

/* volume 0x464618(X, h, v) (0x464618..0x464669), rate 0x46466c(X, h, r)
 * (0x46466c..0x4646d8), pan 0x4646dc(X, h, p) (0x4646dc..0x464748): -1:
 * EAX X, EDX -1, ECX the entry's; stale: EAX X, EDX h, ECX the index. The
 * field (S+10h; S+1Ch with flag 400h unless 10000h; S+18h with flag 800h
 * unless 8000h); not playing: EAX = EDX = S, ECX v (volume) or the index;
 * playing: the glue, ECX = S (the POP ECX of its argument). */
static int sfx_volume(ctx *c, uint32_t x, uint32_t h, uint32_t v, uint32_t ecx_in, rg *out) {
    *out = (rg){x, h, ecx_in};
    if (h == 0xffffffffu) return 0;
    uint32_t i = h & 0xffffu, s = r32(c, x + 0x38u) + 40u * i;
    out->ecx = i;
    if (r32(c, s) != h) return 0;
    uint32_t S = s + 4u;
    w32(c, S + 0x10u, v);
    *out = (rg){S, S, v};
    if (!(r32(c, S + 4u) & 0x100u)) return 0;
    rg g;
    if (glue_volume(c, S, &g)) return 1;
    *out = (rg){g.eax, g.edx, S};
    return 0;
}
static void sfx_field(ctx *c, uint32_t x, uint32_t h, uint32_t v, uint32_t ecx_in, int pan, rg *out) {
    *out = (rg){x, h, ecx_in};
    if (h == 0xffffffffu) return;
    uint32_t i = h & 0xffffu, s = r32(c, x + 0x38u) + 40u * i;
    out->ecx = i;
    if (r32(c, s) != h) return;
    uint32_t S = s + 4u, flag = pan ? 0x800u : 0x400u, neutral = pan ? 0x8000u : 0x10000u;
    w32(c, S + 4u, v == neutral ? (r32(c, S + 4u) & ~flag) : (r32(c, S + 4u) | flag));
    w32(c, S + (pan ? 0x18u : 0x1cu), v);
    *out = (rg){S, S, i};
    if (!(r32(c, S + 4u) & 0x100u)) return;
    rg g = {S, S, 0};
    if (pan) glue_pan(c, S, S, &g); else glue_rate(c, S, S, &g);
    *out = (rg){g.eax, g.edx, S};
}

/* loops 0x46474c(X, h, n) (0x46474c..0x4647b5): flag 80h unless n is 1,
 * S+14h = n; playing: 0x473ba8 (an empty body). priority 0x4647b8(X, h, p)
 * (0x4647b8..0x4647e8): S+8 = p. */
static void sfx_loops(ctx *c, uint32_t x, uint32_t h, uint32_t n) {
    uint32_t S = slot_body(c, x, h);
    if (!S) return;
    w32(c, S + 4u, n == 1u ? (r32(c, S + 4u) & 0xffffff7fu) : (r32(c, S + 4u) | 0x80u));
    w32(c, S + 0x14u, n);
}
static void sfx_priority(ctx *c, uint32_t x, uint32_t h, uint32_t p) {
    uint32_t S = slot_body(c, x, h);
    if (S) w32(c, S + 8u, p);
}
/* stop all 0x4648e0(X) (0x4648e0..0x464985): for index i below X+10h (the
 * record capacity, as the original bounds its slot walk): a playing slot
 * with a valid record: the glue stop, the count - 1, flag 100h cleared. */
static int sfx_stop_all(ctx *c, uint32_t x) {
    for (uint32_t i = 0; i < r32(c, x + 0x10u); ++i) {
        uint32_t S = r32(c, x + 0x38u) + 40u * i + 4u;
        if (!(r32(c, S + 4u) & 0x100u)) continue;
        uint32_t f = r32(c, S);
        if (f == 0xffffffffu || r32(c, record_of(c, x, f)) != f) continue;
        if (glue_stop(c, S)) return 1;
        if (r32(c, S + 4u) & 1u) w32(c, x + 0x40u, r32(c, x + 0x40u) - 1u);
        else w32(c, x + 0x3cu, r32(c, x + 0x3cu) - 1u);
        w32(c, S + 4u, r32(c, S + 4u) & 0xfffffeffu);
    }
    return 0;
}

/* The rows the sfx hooks bridge to. */
void es2_sim_service_sfx_volume(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    rg r;
    if (sfx_volume(&c, sc->slots[0], sc->slots[1], sc->slots[2], sc->ecx, &r)) { failed(sc); return; }
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
}
void es2_sim_service_sfx_rate(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    rg r;
    sfx_field(&c, sc->slots[0], sc->slots[1], sc->slots[2], sc->ecx, 0, &r);
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
}
void es2_sim_service_sfx_pan(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    rg r;
    sfx_field(&c, sc->slots[0], sc->slots[1], sc->slots[2], sc->ecx, 1, &r);
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
}
void es2_sim_service_sfx_play(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    rg r;
    if (sfx_play(&c, sc->slots[0], sc->slots[1], sc->ecx, &r)) { failed(sc); return; }
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
}
void es2_sim_service_sfx_stop(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    rg r;
    if (sfx_stop_regs(&c, sc->slots[0], sc->slots[1], sc->edx, sc->ecx, &r)) { failed(sc); return; }
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
}
/* ======================================================================
 * The sound API above the manager (listing 0x46233c..0x463694). */

/* 0x45ef1c(name, ext) (0x45ef1c..0x45ef81): byte 0x49f257 = the language
 * letter, then 0x4d291c = "st<letter>" + "\" + 0x492b90(name, ext) (the
 * copy inline, the two _strcat). EAX 0x4d291c. */
static int language_path(ctx *c, uint32_t name, uint32_t ext, uint32_t *out) {
    w8(c, 0x0049f257u, r8(c, VA_LANGUAGE));
    uint32_t p = 0, k = 0;
    if (CALL(c, VA_PATH_NAME, &p, name, ext)) return 1;
    uint8_t v;
    do { v = r8(c, 0x0049f255u + k); w8(c, 0x004d291cu + k, v); ++k; } while (v);
    if (CALL(c, VA_STRCAT, NULL, 0x004d291cu, 0x0049f3fcu) || CALL(c, VA_STRCAT, NULL, 0x004d291cu, p)) return 1;
    *out = 0x004d291cu;
    return 0;
}
/* 0x46233c(name) (0x46233c..0x462394): "HMX\" with the word 0x4d2680 (-l)
 * else "HMI\" into 0x4d2d76, then _strcat(0x4d2d76, name). EAX 0x4d2d76. */
static int sample_path(ctx *c, uint32_t name, uint32_t *out) {
    uint32_t s = r16(c, VA_HMX_OPTION) ? 0x0049f9afu : 0x0049f9b4u, k = 0;
    uint8_t v;
    do { v = r8(c, s + k); w8(c, VA_HMI_PATH + k, v); ++k; } while (v);
    if (CALL(c, VA_STRCAT, NULL, VA_HMI_PATH, name)) return 1;
    *out = VA_HMI_PATH;
    return 0;
}

/* The table init 0x46254c (0x46254c..0x462707): the holder 0x4d2c68 reads
 * st<letter>\sounds.str (0x453878), 0x453918 fills names 0x4d2a88[] and
 * parameters 0x4d2b78[] until it answers 0. For each row (word 0x4d2d68
 * from 0 while the name is not empty): _strlwr(name); the open mode from
 * _strstr(L, ".hmp") (0) and _strstr(L, ".wav") (1), where L is the
 * function's own 20-byte local that it never writes (stack residue: modelled
 * as zeroes, so neither is found), else the EDI it was entered with, which
 * 0x462410 left as the channel array [0x4d2d6c]; the open (X, 0x46233c(name),
 * mode, 0, 0) = h; h != -1: handle[i] = h, priority 5, loops p[0], volume
 * 0x47e08d(p[1], 65000), p[2]: cache (X, h, 1); bytes p[4] FFh -> 5, p[5] FFh
 * -> 100, p[8] = 100 (all skipped for h = -1). Then every channel's word
 * +3Ch = the row count and +3Eh = -1. */
static uint32_t q16(uint32_t a, uint32_t b) { return (uint32_t)((uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b) >> 16); }
static int table_init(ctx *c) {
    uint32_t mode = r32(c, VA_VOICES), path = 0, more = 0;
    if (language_path(c, 0x0049f9b9u, 0x0049f9c0u, &path) || CALL(c, VA_STR_OPEN, NULL, VA_STR_HOLDER, path)) return 1;
    uint32_t names = VA_NAMES, params = VA_PARAMS;
    do {
        if (CALL(c, VA_STR_NEXT, &more, VA_STR_HOLDER, names, params)) return 1;
        names += 4u; params += 4u;
    } while (more & 0xffu);
    uint32_t top = c->l->stack_hi, local = frame_push(c, 0x14u);
    if (!local) return refuse(c, 0x0046254cu, "sound table", "the own code's call area is exhausted");
    int bad = 0;
    w16(c, VA_ROW_COUNT, 0);
    for (uint32_t pp = VA_PARAMS; !bad; pp += 4u, w16(c, VA_ROW_COUNT, (uint16_t)(r16(c, VA_ROW_COUNT) + 1u))) {
        uint32_t i = r16(c, VA_ROW_COUNT), name = r32(c, VA_NAMES + 4u * i), found = 0, h = 0;
        if (!r8(c, name)) break;
        if ((bad = CALL(c, VA_STRLWR, NULL, name) || CALL(c, VA_STRSTR, &found, local, r32(c, 0x0049f938u)))) break;
        if (found) mode = 0;
        else {
            if ((bad = CALL(c, VA_STRSTR, &found, local, r32(c, 0x0049f93cu)))) break;
            if (found) mode = 1;
        }
        uint32_t x = r32(c, VA_CONTEXT);
        if ((bad = sample_path(c, r32(c, VA_NAMES + 4u * i), &path) || sfx_open(c, x, path, mode, 0, 0, &h))) break;
        if (h == 0xffffffffu) continue;
        w32(c, VA_HANDLES + 4u * i, h);
        sfx_priority(c, r32(c, VA_CONTEXT), h, 5);
        sfx_loops(c, r32(c, VA_CONTEXT), h, r8(c, r32(c, pp)));
        rg g;
        if ((bad = sfx_volume(c, r32(c, VA_CONTEXT), h, q16(r8(c, r32(c, pp) + 1u), 0xfde8u), 0, &g))) break;
        if (r8(c, r32(c, pp) + 2u)) {
            uint32_t r;
            if ((bad = sfx_cache(c, r32(c, VA_CONTEXT), h, 1, &r))) break;
        }
        uint32_t p = r32(c, pp);
        if (r8(c, p + 4u) == 0xffu) w8(c, p + 4u, 5);
        p = r32(c, pp);
        if (r8(c, p + 5u) == 0xffu) w8(c, p + 5u, 100);
        w8(c, r32(c, pp) + 8u, 100);
    }
    frame_pop(c, top);
    if (bad) return 1;
    uint32_t n = r16(c, VA_ROW_COUNT), ch = r32(c, VA_VOICES);
    for (int32_t k = 0; k < (int32_t)sx16(r16(c, VA_VOICE_COUNT)); ++k, ch += 0x42u) {
        w16(c, ch + 0x3cu, (uint16_t)n);
        w32(c, ch + 0x3eu, 0xffffffffu);
    }
    return 0;
}
void es2_sim_service_sound_table(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    if (table_init(&c)) { failed(sc); return; }
    /* Registers: only the own 0x462410 calls this row and reads none. */
    sc->eax = r32(&c, VA_VOICES) + 0x42u * (uint32_t)(int32_t)sx16(r16(&c, VA_VOICE_COUNT));
    sc->edx = (uint32_t)(int32_t)sx16(r16(&c, VA_VOICE_COUNT));
    sc->ecx = r16(&c, VA_ROW_COUNT);
}

/* is playing 0x462af0(i) (0x462af0..0x462b2d): no context or handle -1: 0;
 * else (done (X, handle[i]) == 0). */
void es2_sim_service_sfx_playing(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    uint32_t x = r32(&c, VA_CONTEXT), h = 0, d = 1;
    sc->eax = 0;
    if (!x) return;
    h = r32(&c, VA_HANDLES + 4u * sc->slots[0]);
    if (h == 0xffffffffu) return;
    if (sfx_done(&c, x, h, &d)) { failed(sc); return; }
    sc->eax = d == 0;
}

/* ---- the voice channels (5 records of 42h bytes at [0x4d2d6c]) */

/* The LRU channel 0x462b30 (0x462b30..0x462b99): the lowest start time
 * +37h (unsigned, strictly below the running minimum from FFFFFFFFh); its
 * lip buffer +2Fh = [0x4d2d70] + 100 * its index (MOVSX of the index word);
 * a handle +0 other than -1 is closed (with a context) and set to -1. None
 * found (every start time FFFFFFFFh): the original writes through NULL
 * (refused). */
static int voice_lru(ctx *c, uint32_t *out) {
    uint32_t best = 0xffffffffu, ch = r32(c, VA_VOICES), pick = 0, index = 0;
    for (int32_t k = 0; k < (int32_t)sx16(r16(c, VA_VOICE_COUNT)); ++k, ch += 0x42u) {
        uint32_t t = r32(c, ch + 0x37u);
        if (best > t) { best = t; index = (uint32_t)k; pick = ch; }
    }
    if (!pick) return null_refuse(c, 0x00462b71u, "voice channel", "no channel has a start time below FFFFFFFFh (the original writes through NULL)");
    w32(c, pick + 0x2fu, r32(c, VA_LIPS) + 100u * (uint32_t)(int32_t)(int16_t)index);
    uint32_t h = r32(c, pick);
    if (h != 0xffffffffu) {
        uint32_t x = r32(c, VA_CONTEXT);
        if (x && sfx_close(c, x, h)) return 1;
        w32(c, pick, 0xffffffffu);
    }
    *out = pick;
    return 0;
}
/* The lip file 0x463374(ch, lip, h) (0x463374..0x4633fe), inside an
 * exception frame: with a lip name: the VOL stream T (0x472f14), the read
 * virtual (T, 4, &n) and (T, n, [ch+2Fh]), the byte [ch+2Fh][n] = FFh,
 * destroy (T, 2); without: [ch+2Fh][0] = FFh. ch+0 = h. A failed length
 * read leaves the original's local f-54h as it was (residue): modelled as
 * 0. */
static int lip_read(ctx *c, uint32_t ch, uint32_t lip, uint32_t h) {
    if (lip) {
        uint32_t top = c->l->stack_hi, f = frame_push(c, 0x54u);
        if (!f) return refuse(c, 0x00463374u, "lip file", "the own code's call area is exhausted");
        uint32_t t = f + 0x54u - 0x30u, n = f;
        int bad = CALL(c, VA_VOL_STREAM, NULL, t, lip) || stream_read(c, t, 4u, n) ||
                  stream_read(c, t, r32(c, n), r32(c, ch + 0x2fu));
        if (!bad) { w8(c, r32(c, ch + 0x2fu) + r32(c, n), 0xffu); bad = CALL(c, VA_STREAM_DESTROY, NULL, t, 2u); }
        frame_pop(c, top);
        if (bad) return 1;
    } else w8(c, r32(c, ch + 0x2fu), 0xffu);
    w32(c, ch, h);
    return 0;
}
/* The voice start 0x462b9c(wav, lip) (0x462b9c..0x462d3d), inside an
 * exception frame: a lip name whose VOL stream reports an error ([T+4]):
 * destroy, 0 (before the clock). Otherwise destroy, t = the clock 0x467828
 * (one read); a channel whose name +33h equals wav (strcmp) is reused;
 * else the LRU channel: wav copied to +0Eh, +33h = +0Eh, byte +3Bh = 1;
 * with a context and the voice byte 0x49f966: +3Eh = open (X, +0Eh, 1, 0, 0),
 * -1 or a failed cache (X, h, 1): 0; priority FFh; then the lip file
 * 0x463374(ch, lip, +3Eh). +37h = t. EAX ch, EDX the head at entry. */
static int voice_start(ctx *c, uint32_t wav, uint32_t lip, uint32_t *out) {
    *out = 0;
    if (lip) {
        uint32_t top = c->l->stack_hi, f = frame_push(c, 0x54u);
        if (!f) return refuse(c, 0x00462b9cu, "voice start", "the own code's call area is exhausted");
        uint32_t t = f + 0x54u - 0x30u;
        int bad = CALL(c, VA_VOL_STREAM, NULL, t, lip);
        int error = !bad && r32(c, t + 4u) != 0;
        if (!bad) bad = CALL(c, VA_STREAM_DESTROY, NULL, t, 2u);
        frame_pop(c, top);
        if (bad) return 1;
        if (error) return 0;
    }
    uint32_t now = 0, ch = 0, at = r32(c, VA_VOICES);
    if (CALL(c, VA_CLOCK, &now)) return 1;
    for (int32_t k = 0; k < (int32_t)sx16(r16(c, VA_VOICE_COUNT)); ++k, at += 0x42u) {
        uint32_t name = r32(c, at + 0x33u);
        if (!name) continue;
        uint32_t i = 0;
        uint8_t p, q;
        do { p = r8(c, name + i); q = r8(c, wav + i); ++i; } while (p == q && p);
        if (p == q) { ch = at; break; }
    }
    if (!ch) {
        if (voice_lru(c, &ch)) return 1;
        uint32_t i = 0;
        uint8_t v;
        do { v = r8(c, wav + i); w8(c, ch + 0xeu + i, v); ++i; } while (v);
        w32(c, ch + 0x33u, ch + 0xeu);
        w8(c, ch + 0x3bu, 1);
        uint32_t x = r32(c, VA_CONTEXT);
        if (x && r8(c, VA_VOICE_ON)) {
            uint32_t h = 0, r = 0;
            if (sfx_open(c, x, ch + 0xeu, 1, 0, 0, &h)) return 1;
            w32(c, ch + 0x3eu, h);
            if (h == 0xffffffffu) return 0;
            if (sfx_cache(c, r32(c, VA_CONTEXT), h, 1, &r)) return 1;
            if (!r) return 0;
            sfx_priority(c, r32(c, VA_CONTEXT), r32(c, ch + 0x3eu), 0xffu);
        }
        if (lip_read(c, ch, lip, r32(c, ch + 0x3eu))) return 1;
    }
    w32(c, ch + 0x37u, now);
    *out = ch;
    return 0;
}
void es2_sim_service_voice_start(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    uint32_t head = fs0_head(), ch = 0;
    if (voice_start(&c, sc->slots[0], sc->slots[1], &ch)) { failed(sc); return; }
    sc->eax = ch; sc->edx = head;
}
/* The voice 0x462940(wav, volume) (0x462940..0x462978): with a context: ch =
 * the voice start (wav, 0); nonzero: the channel volume 0x463578(ch, MOVSX
 * volume) (with a context and a handle +0 other than -1: volume (X, +0, v))
 * and the hook sfx_voice 0x463400(ch), whose registers it returns. No
 * context: the entry registers; no channel: EAX 0. */
void es2_sim_service_voice(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    if (!r32(&c, VA_CONTEXT)) return;   /* the entry registers stay */
    uint32_t ch = 0;
    if (voice_start(&c, sc->slots[0], 0, &ch)) { failed(sc); return; }
    sc->eax = 0;
    sc->edx = fs0_head();
    if (!ch) return;
    uint32_t x = r32(&c, VA_CONTEXT);
    rg g;
    if (x && r32(&c, ch) != 0xffffffffu && sfx_volume(&c, x, r32(&c, ch), sx16(sc->slots[1]), 0, &g)) { failed(sc); return; }
    es2_sim_result r;
    if (call_regs(&c, VA_SFX_VOICE, (const uint32_t[]){ch}, 1, NULL, &r)) { failed(sc); return; }
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
}
/* The lip value 0x46356c(ch) (0x46356c..0x463577): AX = word ch+8 (the
 * upper half of EAX is ch's). */
void es2_sim_service_lip_value(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    uint32_t ch = sc->slots[0];
    sc->eax = (ch & 0xffff0000u) | r16(&c, ch + 8u);
}
/* The lip pass 0x4632c4 (0x4632c4..0x463334, every frame from the radio
 * panel): t = the clock 0x467828 (one read); from n = count - 1 down to 0
 * the first queued record whose time is not above t (unsigned): the entries
 * n .. count - 1 move to 0 .., the word count -= n + 1, and the hook
 * sfx_lip_due 0x4634a0 runs for the channel of the new first entry. EAX,
 * EDX, ECX: the hook's, else the walk's (EAX the last entry pointer looked
 * at, EDX t, ECX the clock's or the last record pointer read). */
void es2_sim_service_lip_pass(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    es2_sim_result t;
    if (call_regs(&c, VA_CLOCK, NULL, 0, NULL, &t)) { failed(sc); return; }
    int32_t n = (int32_t)sx16(r16(&c, VA_QUEUE_COUNT)) - 1;
    uint32_t ecx = t.ecx;
    for (; n >= 0; --n) {
        ecx = r32(&c, VA_QUEUE + 4u * (uint32_t)n);
        if (t.eax < r32(&c, ecx)) continue;
        int32_t count = (int32_t)sx16(r16(&c, VA_QUEUE_COUNT));
        uint32_t d = 0;
        for (int32_t k = n; k < count; ++k, ++d) w32(&c, VA_QUEUE + 4u * d, r32(&c, VA_QUEUE + 4u * (uint32_t)k));
        w16(&c, VA_QUEUE_COUNT, (uint16_t)(r16(&c, VA_QUEUE_COUNT) - (uint16_t)(n + 1)));
        uint32_t ch = r32(&c, r32(&c, VA_QUEUE) + 4u);
        es2_sim_result r;
        if (call_regs(&c, VA_SFX_LIP_DUE, (const uint32_t[]){ch}, 1, (const uint32_t[]){r32(&c, VA_QUEUE), ch, VA_QUEUE + 4u * d}, &r)) { failed(sc); return; }
        sc->eax = r.eax; sc->edx = r.edx; sc->ecx = ch;   /* POP ECX of the pushed channel */
        return;
    }
    sc->eax = VA_QUEUE + 4u * (uint32_t)n; sc->edx = t.eax; sc->ecx = ecx;
}
/* ======================================================================
 * CD audio over MCI (listing 0x473c00..0x473ea8; the manager's wrappers
 * 0x464858, 0x464874, 0x464888, 0x464988, 0x46499c call these directly).
 * The device word [0x4a0e2c] is -1 without an open device. */
#define MCI_OPEN 0x0803u
#define MCI_CLOSE 0x0804u
#define MCI_PLAY 0x0806u
#define MCI_STOP 0x0808u
#define MCI_SET 0x080du
#define MCI_STATUS 0x0814u
/* 0x473c00: an open device (signed >= 0): MCI_STOP and MCI_CLOSE (flags 0,
 * no block); the word -1. */
static void cd_stop(ctx *c) {
    uint32_t d = r32(c, VA_CD_DEVICE);
    if ((int32_t)d >= 0) { mci(c, d, MCI_STOP, 0, 0); mci(c, r32(c, VA_CD_DEVICE), MCI_CLOSE, 0, 0); }
    w32(c, VA_CD_DEVICE, 0xffffffffu);
}
/* 0x473d84(track): MCI_STATUS (ITEM | TRACK | WAIT 112h) of item 1 (the
 * length) on the open device; the answer or 0. 0x473d44: the same with
 * flags 102h and item 2 (the position). */
static int cd_status(ctx *c, uint32_t flags, uint32_t item, uint32_t track, uint32_t *out) {
    *out = 0;
    uint32_t d = r32(c, VA_CD_DEVICE);
    if ((int32_t)d < 0) return 0;
    uint32_t top = c->l->stack_hi, f = frame_push(c, 0x10u);
    if (!f) return refuse(c, 0x00473d84u, "CD status", "the own code's call area is exhausted");
    w32(c, f + 8u, item);
    w32(c, f + 0xcu, track);
    if (!mci(c, d, MCI_STATUS, flags, f)) *out = r32(c, f + 4u);
    frame_pop(c, top);
    return 0;
}
/* 0x473c48(track) and 0x473dcc(position): MCI_OPEN (TYPE 2000h) of
 * "cdaudio" (0x4a0e34 / 0x4a0e3c), failure 0; the device to [0x4a0e2c];
 * MCI_SET (TIME_FORMAT 400h) to TMSF (10), failure: stop, 0. The track
 * start reads the track's length (0x473d84) into [0x6b558c] (0: stop, 0).
 * MCI_PLAY (NOTIFY | FROM | TO, 0Dh) with the window [0x6b5588] as the
 * callback, from = the track (byte) or the position, to = that byte | the
 * three low bytes of [0x6b558c] << 8; failure: stop. EAX = (the last answer
 * == 0). */
static int cd_play(ctx *c, uint32_t arg, int resume, uint32_t *ok) {
    *ok = 0;
    uint32_t top = c->l->stack_hi, f = frame_push(c, 0x2cu);
    if (!f) return refuse(c, resume ? 0x00473dccu : 0x00473c48u, "CD play", "the own code's call area is exhausted");
    uint32_t open = f + 0x2cu - 0x14u, set = f + 0x2cu - 0x20u, play = f;
    w32(c, open + 8u, resume ? 0x004a0e3cu : 0x004a0e34u);
    uint32_t e = mci(c, 0, MCI_OPEN, 0x2000u, open);
    int bad = 0;
    if (!e) {
        uint32_t d = r32(c, open + 4u);
        w32(c, VA_CD_DEVICE, d);
        w32(c, set + 4u, 10u);
        e = mci(c, d, MCI_SET, 0x400u, set);
        if (e) cd_stop(c);
        else {
            uint32_t len = 0;
            if (!resume) {
                bad = cd_status(c, 0x112u, 1u, arg, &len);
                w32(c, VA_CD_LENGTH, len);
            }
            if (!bad && !resume && !len) { cd_stop(c); frame_pop(c, top); return 0; }
            if (!bad) {
                len = r32(c, VA_CD_LENGTH);
                w32(c, play, r32(c, VA_WINDOW));
                w32(c, play + 4u, resume ? arg : (arg & 0xffu));
                w32(c, play + 8u, (arg & 0xffu) | (len & 0xffu) << 8 | ((len >> 8) & 0xffu) << 16 | ((len >> 16) & 0xffu) << 24);
                e = mci(c, r32(c, VA_CD_DEVICE), MCI_PLAY, 0xdu, play);
                if (e) cd_stop(c);
            }
        }
    }
    frame_pop(c, top);
    *ok = e == 0;
    return bad;
}

/* The CD start of the mission load 0x46313c (0x46313c..0x463167): with a
 * context, a track [0x49f8fc] and the gate [0x49f8f4]: 0x464858(X, track). */
void es2_sim_service_cd_start(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    uint32_t x = r32(&c, VA_CONTEXT), ok = 0;
    sc->eax = x;
    if (!x || !r32(&c, VA_CD_TRACK) || !r32(&c, VA_GATE_LOW)) return;
    if (cd_play(&c, r32(&c, VA_CD_TRACK), 0, &ok)) { failed(sc); return; }
    sc->eax = ok;
}
/* 0x464874(X) as a row (also called from 0x45f4cf): the CD stop. */
void es2_sim_service_cd_stop(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    cd_stop(&c);
    sc->eax = 0x00473c00u;   /* MOV EAX,473C00h before the call; 0x473c00 leaves it */
}

/* ======================================================================
 * The window handler 0x462398(window, message, w, l) (0x462398..0x46240c,
 * stdcall): WM_TIMER (113h) with a context: 0x4639e8 -> 0x4737d8(window,
 * message, w, l): user service (driver); a window: 0x474aa0, whose WM_TIMER
 * branch runs the user service again when 0x47432c ((54h & 58h) | (56h &
 * 5Ah)) is set (its other branches need messages this path never passes:
 * refused). MM_MCINOTIFY (3B9h) with w = MCI_NOTIFY_SUCCESSFUL, a context
 * and the loop byte 0x49f900: the CD stop and the track [0x49f8fc] again.
 * EAX 0; EDX the message - 3B9h on the other messages (the handler list in
 * loop.c reads neither EDX nor ECX). */
static int user_service(ctx *c, uint32_t window, uint32_t message) {
    SOS(c, ES2_SOS_USER_SERVICE, driver(c));
    if (!window) return 0;
    if (message != 0x113u) return refuse(c, 0x00474aa0u, "SOS window messages", "a message other than WM_TIMER (never passed)");
    if ((r16(c, VA_DIGI_ON) & r16(c, VA_DIGI_WANTED)) | (r16(c, VA_MIDI_ON) & r16(c, VA_MIDI_WANTED))) SOS(c, ES2_SOS_USER_SERVICE, driver(c));
    return 0;
}
void es2_sim_service_sound_window(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    uint32_t message = sc->slots[1];
    sc->eax = 0;
    sc->edx = message - 0x3b9u;
    if (message == 0x113u) {
        sc->edx = r32(&c, VA_CONTEXT);
        if (sc->edx && user_service(&c, sc->slots[0], message)) failed(sc);
    } else if (message == 0x3b9u && sc->slots[2] == 1u && r32(&c, VA_CONTEXT) && r8(&c, VA_CD_LOOP)) {
        uint32_t ok;
        cd_stop(&c);
        if (cd_play(&c, r32(&c, VA_CD_TRACK), 0, &ok)) failed(sc);
    }
}
/* 0x464260 (0x464260..0x46427d, from the UI loop): 0x4737d8(0, 0, 0, 0):
 * the user service. EAX 0. */
void es2_sim_service_sound_service(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    if (user_service(&c, 0, 0)) { failed(sc); return; }
    sc->eax = 0;
}

/* ======================================================================
 * Pause 0x46317c and resume 0x463238 (from the loop's 0x45f0d4/0x45f108).
 * Pause (0x46317c..0x463235): [0x49f904] = 0; with a context: a track and
 * the loop byte: with the gate [0x49f8f4] the position (0x473d44) to
 * 0x49f904, the CD stop; every row's byte p[7] = (its slot loops endlessly
 * (S+14h 0) and plays); stop all; the release 0x4737c4 (release driver
 * (driver, 1000h)). A row whose handle is no valid slot makes the original
 * read through NULL (refused). Resume (0x463238..0x4632c1): with a context:
 * the acquire 0x4737b0 (acquire driver (driver)), play every row with
 * p[7] set; with the gate: the saved position resumed, or the track
 * started. */
void es2_sim_service_sound_pause(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    w32(&c, VA_CD_SAVED, 0);
    uint32_t x = r32(&c, VA_CONTEXT);
    if (!x) return;
    if (r32(&c, VA_CD_TRACK) && r8(&c, VA_CD_LOOP)) {
        if (r32(&c, VA_GATE_LOW)) {
            uint32_t pos = 0;
            if (cd_status(&c, 0x102u, 2u, 0, &pos)) { failed(sc); return; }
            w32(&c, VA_CD_SAVED, pos);
        }
        cd_stop(&c);
    }
    for (uint32_t i = 0; i < r16(&c, VA_ROW_COUNT); ++i) {
        uint32_t S = slot_body(&c, r32(&c, VA_CONTEXT), r32(&c, VA_HANDLES + 4u * i));
        if (!S) { null_refuse(&c, 0x004631edu, "sound pause", "a row without a valid slot (the original reads through NULL)"); failed(sc); return; }
        w8(&c, r32(&c, VA_PARAMS + 4u * i) + 7u, (!r32(&c, S + 0x14u) && (r32(&c, S + 4u) & 0x100u)) ? 1u : 0u);
    }
    if (sfx_stop_all(&c, r32(&c, VA_CONTEXT))) { failed(sc); return; }
    sc->eax = SOS(&c, ES2_SOS_RELEASE_DRIVER, driver(&c), 0x1000u);
}
void es2_sim_service_sound_resume(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    uint32_t x = r32(&c, VA_CONTEXT);
    sc->eax = x;
    if (!x) return;
    SOS(&c, ES2_SOS_ACQUIRE_DRIVER, driver(&c));
    for (uint32_t i = 0; i < r16(&c, VA_ROW_COUNT); ++i) {
        if (!r8(&c, r32(&c, VA_PARAMS + 4u * i) + 7u)) continue;
        rg g;
        if (sfx_play(&c, r32(&c, VA_CONTEXT), r32(&c, VA_HANDLES + 4u * i), 0, &g)) { failed(sc); return; }
        sc->eax = g.eax;
    }
    if (!r32(&c, VA_GATE_LOW)) return;
    uint32_t ok = 0;
    int bad = r32(&c, VA_CD_SAVED) ? cd_play(&c, r32(&c, VA_CD_SAVED), 1, &ok) : cd_play(&c, r32(&c, VA_CD_TRACK), 0, &ok);
    if (bad) { failed(sc); return; }
    sc->eax = ok;
}

/* ======================================================================
 * The prefs sound groups and the option screen's volume (listing
 * 0x462d78..0x463012). The row volume v(i) = (0x47e08d(p[1], 65000) *
 * p[8]) (IMUL, low dword, CDQ) IDIV 100. The flags 0x462d40/0x462d5c are
 * input.c's rows (through the dispatch). */
static uint32_t row_volume(const ctx *c, uint32_t i) {
    uint32_t p = r32(c, VA_PARAMS + 4u * i);
    return (uint32_t)((int32_t)(q16(r8(c, p + 1u), 0xfde8u) * r8(c, p + 8u)) / 100);
}
/* The flag row (input.c's) last: its registers are the group's. */
static int flag(ctx *c, es2_sim_service_call *sc, uint32_t va, uint32_t on) {
    es2_sim_result r;
    if (call_regs(c, va, (const uint32_t[]){on}, 1, NULL, &r)) return 1;
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = on;   /* POP ECX of the pushed argument */
    return 0;
}
static int set_rows(ctx *c, uint32_t from, uint32_t to, int on) {
    for (uint32_t i = from; i < to; ++i) {
        rg g;
        if (sfx_volume(c, r32(c, VA_CONTEXT), r32(c, VA_HANDLES + 4u * i), on ? row_volume(c, i) : 0u, 0, &g)) return 1;
    }
    return 0;
}
/* 0x462d78 (music and effects 0..9 off): [0x49f904] = 0; with a context: a
 * track: the position to 0x49f904 and the CD stop; else rows 0..9 volume 0;
 * then flag A (0). Registers: the flag row's (POP ECX: the argument); without a
 * context EAX 0 (0x462d78) or the context (0x462e58), the entry's for
 * 0x462ddc/0x462efc. */
void es2_sim_service_sound_low_off(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    w32(&c, VA_CD_SAVED, 0);
    sc->eax = 0;   /* XOR EAX,EAX before the store */
    if (!r32(&c, VA_CONTEXT)) return;
    int bad;
    if (r32(&c, VA_CD_TRACK)) {
        uint32_t pos = 0;
        bad = cd_status(&c, 0x102u, 2u, 0, &pos);
        w32(&c, VA_CD_SAVED, pos);
        cd_stop(&c);
    } else bad = set_rows(&c, 0, 10, 0);
    if (bad || flag(&c, sc, 0x00462d40u, 0)) failed(sc);
}
/* 0x462e58 (on): with a context: a track: resume the saved position or
 * start the track; else rows 0..9 at v(i); then flag A (1). */
void es2_sim_service_sound_low_on(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    sc->eax = r32(&c, VA_CONTEXT);   /* MOV EAX,[0x49f8ec] */
    if (!sc->eax) return;
    int bad;
    uint32_t ok;
    if (r32(&c, VA_CD_TRACK)) bad = r32(&c, VA_CD_SAVED) ? cd_play(&c, r32(&c, VA_CD_SAVED), 1, &ok) : cd_play(&c, r32(&c, VA_CD_TRACK), 0, &ok);
    else bad = set_rows(&c, 0, 10, 1);
    if (bad || flag(&c, sc, 0x00462d40u, 1)) failed(sc);
}
/* 0x462ddc / 0x462efc (rows 10 .. count - 1 off / on, then flag B). */
void es2_sim_service_sound_high_off(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    if (!r32(&c, VA_CONTEXT)) return;
    if (set_rows(&c, 10, r16(&c, VA_ROW_COUNT), 0) || flag(&c, sc, 0x00462d5cu, 0)) failed(sc);
}
void es2_sim_service_sound_high_on(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    if (!r32(&c, VA_CONTEXT)) return;
    if (set_rows(&c, 10, r16(&c, VA_ROW_COUNT), 1) || flag(&c, sc, 0x00462d5cu, 1)) failed(sc);
}
/* 0x462fe0(i): with a context (p[1] * p[8]) IDIV 100, else 0. 0x463014(i,
 * v): with a context p[1] = v (byte) and volume (X, handle[i],
 * 0x47e08d(v, 65000)). */
void es2_sim_service_row_level(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    sc->eax = 0;
    if (!r32(&c, VA_CONTEXT)) return;
    uint32_t p = r32(&c, VA_PARAMS + 4u * sc->slots[0]);
    int32_t prod = (int32_t)(r8(&c, p + 1u) * r8(&c, p + 8u));
    sc->eax = (uint32_t)(prod / 100); sc->edx = (uint32_t)(prod % 100); sc->ecx = 100u;
}
void es2_sim_service_row_set_level(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    if (!r32(&c, VA_CONTEXT)) return;
    uint32_t i = sc->slots[0], v = sc->slots[1] & 0xffu;
    w8(&c, r32(&c, VA_PARAMS + 4u * i) + 1u, (uint8_t)v);
    rg g;
    if (sfx_volume(&c, r32(&c, VA_CONTEXT), r32(&c, VA_HANDLES + 4u * i), q16(v, 0xfde8u), 0, &g)) { failed(sc); return; }
    sc->eax = g.eax; sc->edx = g.edx; sc->ecx = g.ecx;
}

/* ======================================================================
 * The sound close 0x462734 (0x462734..0x462780; the abort notify 0x461304
 * and 0x45f4d5): with a context: the CD stop unless the device word is -1
 * (0x464888); the handler removed (0x466010, row); counter + 1; the context
 * destructor 0x4638f0(X, 3): counter - 1, the driver shutdown 0x4737a4, the
 * delete of X+34h and X+38h (each then 0), the delete of X (flag 1);
 * [0x49f8ec] = 0. */
void es2_sim_service_sound_close(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    uint32_t x = r32(&c, VA_CONTEXT);
    sc->eax = x;
    if (!x) return;
    if (r32(&c, VA_CD_DEVICE) != 0xffffffffu) cd_stop(&c);
    if (CALL(&c, VA_HANDLER_REMOVE, NULL, 0x00462398u)) { failed(sc); return; }
    counter(&c, 1);
    x = r32(&c, VA_CONTEXT);
    counter(&c, -1);
    if (x) {
        if (driver_shutdown(&c)) { failed(sc); return; }
        if (r32(&c, x + 0x34u) && CALL(&c, VA_DELETE, NULL, r32(&c, x + 0x34u))) { failed(sc); return; }
        w32(&c, x + 0x34u, 0);
        if (r32(&c, x + 0x38u) && CALL(&c, VA_DELETE, NULL, r32(&c, x + 0x38u))) { failed(sc); return; }
        w32(&c, x + 0x38u, 0);
        if (CALL(&c, VA_DELETE, NULL, x)) { failed(sc); return; }
    }
    w32(&c, VA_CONTEXT, 0);
    sc->eax = 0; sc->edx = 0;
}

/* The cockpit sound step of the mission load 0x4328cc (0x4328cc..0x432923):
 * with byte [C+245h] 0 (C = [0x49b088]): [C+241h] = the clock, sfx_play(13h);
 * a nonzero word [[[C+203h]+1F2h]+50h]: sfx_play(2Dh) and sfx_rate(2Dh,
 * A410h) (hooks). */
void es2_sim_service_cockpit_sound(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    es2_sim_result r;
    memset(&r, 0, sizeof(r));
    uint32_t cp = r32(&c, VA_COCKPIT);
    if (!r8(&c, cp + 0x245u)) {
        uint32_t t = 0;
        if (CALL(&c, VA_CLOCK, &t)) { failed(sc); return; }
        w32(&c, r32(&c, VA_COCKPIT) + 0x241u, t);
        if (call_regs(&c, VA_SFX_PLAY, (const uint32_t[]){0x13u}, 1, NULL, &r)) { failed(sc); return; }
    }
    uint32_t a = r32(&c, r32(&c, VA_COCKPIT) + 0x203u), d = r32(&c, a + 0x1f2u);
    sc->eax = a; sc->edx = d; sc->ecx = r32(&c, VA_COCKPIT);
    if (r16(&c, d + 0x50u)) {
        if (call_regs(&c, VA_SFX_PLAY, (const uint32_t[]){0x2du}, 1, NULL, &r) ||
            call_regs(&c, VA_SFX_RATE, (const uint32_t[]){0x2du, 0xa410u}, 2, NULL, &r)) { failed(sc); return; }
        sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
    }
}

/* ======================================================================
 * The silent device. */
void es2_sim_audio_silent_init(es2_sim_audio_silent *s, const es2_vt *vt) {
    memset(s, 0, sizeof(*s));
    s->vt = vt;
    s->formats = 0xfffu;
    s->mci_open_error = 306u;   /* MCIERR_DEVICE_NOT_INSTALLED-class answer: no CD audio device */
}
static uint32_t silent_now(const es2_sim_audio_silent *s) { return s->vt ? s->vt->clock_ms : 0u; }
/* The playing time of a started sample structure P (0x474360's layout):
 * P+10h bytes at P+38h Hz, P+3Ch bits, P+40h channels, P+34h the repeat
 * count (-1: endless). */
static uint32_t silent_length(const es2_guest_memory *m, uint32_t p) {
    uint32_t loops = m->read32(m->context, p + 0x34u);
    if (loops == 0xffffffffu) return 0xffffffffu;
    uint64_t rate = m->read32(m->context, p + 0x38u), bits = m->read32(m->context, p + 0x3cu), ch = m->read32(m->context, p + 0x40u);
    uint64_t bps = rate * ch * bits / 8u;
    if (!bps) return 0;
    uint64_t ms = (uint64_t)m->read32(m->context, p + 0x10u) * 1000u / bps * ((uint64_t)loops + 1u);
    return ms > 0xfffffffeu ? 0xfffffffeu : (uint32_t)ms;
}
static uint32_t silent_sos(void *context, const es2_guest_memory *m, es2_sim_sos_call k, const uint32_t *a, unsigned n) {
    es2_sim_audio_silent *s = context;
    (void)n;
    if ((unsigned)k < ES2_SOS_CALL_COUNT) ++s->calls[k];
    switch (k) {
    case ES2_SOS_DETECT_FIND_FIRST: {
        /* What SOS9503.DLL's find-first writes for a wave device (its
         * 0x404589 over WAVEOUTCAPS): +2/+4/+6 the ids and version (0), +8
         * the name (empty), +48h the formats, +4Ch |= the format classes
         * (10h 11 kHz, 20h 22 kHz, 40h 44 kHz, 1 8 bits, 2 16 bits, 4 mono,
         * 8 stereo), +4Eh 0, +50h the support flags (0), +54h the device
         * index (0). */
        uint32_t b = a[0], f = s->formats, cls = 0;
        if (f & 0x00fu) cls |= 0x10u;
        if (f & 0x0f0u) cls |= 0x20u;
        if (f & 0xf00u) cls |= 0x40u;
        if (f & 0x333u) cls |= 0x01u;
        if (f & 0xcccu) cls |= 0x02u;
        if (f & 0x555u) cls |= 0x04u;
        if (f & 0xaaau) cls |= 0x08u;
        m->write16(m->context, b + 2u, 0); m->write16(m->context, b + 4u, 0); m->write16(m->context, b + 6u, 0);
        es2_sim_write8(m, b + 8u, 0);
        m->write32(m->context, b + 0x48u, f);
        m->write16(m->context, b + 0x4cu, (uint16_t)(m->read16(m->context, b + 0x4cu) | cls));
        m->write16(m->context, b + 0x4eu, 0);
        m->write32(m->context, b + 0x50u, 0);
        m->write16(m->context, b + 0x54u, 0);
        return 0;
    }
    case ES2_SOS_INIT_DRIVER: m->write16(m->context, a[1], 0); return 0;
    case ES2_SOS_START_SAMPLE: {
        for (unsigned i = 0; i < ES2_SIM_SILENT_SAMPLES; ++i) {
            if (s->sample[i].used) continue;
            s->sample[i].used = 1;
            s->sample[i].start_ms = silent_now(s);
            s->sample[i].length_ms = silent_length(m, a[1]);
            m->write16(m->context, a[2], (uint16_t)i);
            return 0;
        }
        return 0x17u;   /* the DLL's "no free sample" answer (0x405eb2) */
    }
    case ES2_SOS_STOP_SAMPLE:
        if (a[1] < ES2_SIM_SILENT_SAMPLES) s->sample[a[1]].used = 0;
        return 0;
    case ES2_SOS_SAMPLE_DONE: {
        if (a[1] >= ES2_SIM_SILENT_SAMPLES || !s->sample[a[1]].used) return 1;
        uint32_t len = s->sample[a[1]].length_ms;
        return len != 0xffffffffu && silent_now(s) - s->sample[a[1]].start_ms >= len;
    }
    case ES2_SOS_STOP_ALL:
        for (unsigned i = 0; i < ES2_SIM_SILENT_SAMPLES; ++i) s->sample[i].used = 0;
        return 0;
    default: return 0;
    }
}
static uint32_t silent_mci(void *context, const es2_guest_memory *m, uint32_t device, uint32_t message, uint32_t flags, uint32_t params) {
    es2_sim_audio_silent *s = context;
    (void)m; (void)device; (void)flags; (void)params;
    ++s->mci_calls;
    return message == MCI_OPEN ? s->mci_open_error : 257u;   /* MCIERR_INVALID_DEVICE_ID */
}
void es2_sim_audio_silent_device(es2_sim_audio_silent *s, es2_sim_audio_device *device) {
    device->context = s;
    device->sos = silent_sos;
    device->mci = silent_mci;
}
/* ======================================================================
 * The test seam: the internal functions by their original VA (in vitro). */
int es2_sim_audio_internal(uint32_t va, const uint32_t *a, unsigned n, const uint32_t regs[3], es2_sim_result *out) {
    es2_sim_service_call sc;
    memset(&sc, 0, sizeof(sc));
    ctx c;
    if (serving(&sc, &c)) return 1;
    memset(out, 0, sizeof(*out));
    uint32_t v = 0;
    int bad = 0;
    (void)regs;
    switch (va) {
    case 0x00463a14u: if (n < 5) return 1; bad = sfx_open(&c, a[0], a[1], a[2], a[3], a[4], &v); out->eax = v; break;
    case 0x00463c50u: if (n < 2) return 1; bad = sfx_close(&c, a[0], a[1]); break;
    case 0x00463d4cu: if (n < 3) return 1; bad = sfx_cache(&c, a[0], a[1], a[2], &v); out->eax = v; break;
    case 0x004641e8u: if (n < 2) return 1; bad = sfx_done(&c, a[0], a[1], &v); out->eax = v; break;
    case 0x004647b8u: if (n < 3) return 1; sfx_priority(&c, a[0], a[1], a[2]); break;
    case 0x0046474cu: if (n < 3) return 1; sfx_loops(&c, a[0], a[1], a[2]); break;
    case 0x00464390u: if (n < 2) return 1; bad = sfx_evict(&c, a[0], a[1], &v); out->eax = v; break;
    case 0x004648e0u: if (n < 1) return 1; bad = sfx_stop_all(&c, a[0]); break;
    case 0x004737a4u: bad = driver_shutdown(&c); break;
    case 0x00462b30u: bad = voice_lru(&c, &v); out->eax = v; break;
    default: return 1;
    }
    return bad;
}

/* ======================================================================
 * The SOS software mixer and the mixing device (audio.md, "The mixing device").
 * Every fact below is a static reading of SOS9503.DLL (the CD's file;
 * llvm-objdump -d -M intel, image base 0x400000), with the address range in the
 * comment; the unit test //sim:sos_mixer_test checks the arithmetic on
 * synthetic voices. */
#define SOS_USED 0x8000u
#define SOS_ENDED 0x4000u
#define SOS_NOTICED 0x800u
#define SOS_REMOVED 0x2000u

/* The default voice callbacks that sosDIGIStartSample installs when the
 * sample's own are 0 (0x405f7e +60h = 0x404140, 0x405fbb +64h = 0x4041ae; DBSIM
 * leaves them 0). Both read the window table entry of the voice's driver (+74h;
 * 0x41809e + driver * 0xa58, driver 0 here) and, when it is nonzero,
 * PostMessageA(window, 0xa000, 0xa003, handle (+2Ch word) << 16 | flag): 4000h
 * from the end callback (0x404168..0x4041a3), 2000h from the remove callback
 * (0x4041ae..). */
#define SOS_NOTICE_MESSAGE 0xa000u
#define SOS_NOTICE_WPARAM 0xa003u
static void sos_notice(es2_sim_sos_mixer *x, uint32_t handle, uint32_t flag) {
    if (!x->window) return;
    ++x->posts;
    if (!x->post || x->post(x->post_user, x->window, SOS_NOTICE_MESSAGE, SOS_NOTICE_WPARAM, (handle & 0xffffu) << 16 | flag))
        ++x->posts_lost;
}

static uint32_t sos_b(const es2_guest_memory *m, uint32_t a) { return es2_sim_read8(m, a); }
static uint32_t sos_w(const es2_guest_memory *m, uint32_t a) { return m->read16(m->context, a); }
static uint32_t sos_d(const es2_guest_memory *m, uint32_t a) { return m->read32(m->context, a); }
/* IMUL r/m16 with EDX cleared before it, then ADD EDX, EDX: the high product
 * word, zero-extended and doubled (0x406c7a..0x406c94). */
static uint32_t sos_gain(uint32_t volume, uint32_t factor) {
    int32_t p = (int32_t)(int16_t)(uint16_t)volume * (int32_t)(int16_t)(uint16_t)factor;
    return (uint32_t)(uint16_t)((uint32_t)p >> 16) * 2u;
}
/* The routines' volume product: IMUL word [gain], ADD EDX, EDX, MOVSX EDX, DX. */
static int32_t sos_scaled(uint32_t word, uint32_t gain, uint32_t *ax) {
    int32_t p = (int32_t)(int16_t)(uint16_t)word * (int32_t)(int16_t)(uint16_t)gain;
    *ax = (uint16_t)(uint32_t)p;   /* AX keeps the low product word: the next byte fetch keeps its AL */
    return (int16_t)(uint16_t)(((uint32_t)p >> 16) << 1);
}

typedef struct sos_run { uint32_t fl, gl, gr, step0, step1, frac; } sos_run;

/* The routine 0x40730c[fl] (read: 0x407790, 0x4077b1, 0x4077d7, 0x407820,
 * 0x407841, 0x4078be, 0x4078e5, 0x407917, 0x407a26, 0x407a50, 0x407a7f,
 * 0x407bba, 0x407bef, 0x407ca8, 0x407cd2, 0x407d5c, 0x407d9a, 0x407e60,
 * 0x407e8d, 0x407f2e, 0x407f6d, 0x407fb7, 0x408044, 0x408126, 0x40816d,
 * 0x4081b9, 0x40835e, 0x4083ab; the others by the same pattern). Flags: 1
 * unsigned data (XOR 8000h), 2 16-bit data, 4 stereo output, 8 stereo data,
 * 10h resampled, 20h scaled. Entered with EAX = EBX = 0 and ECX = 80000000h
 * (0x406d5b..0x406d63). Per output frame: leave when pos >= end; fetch (8-bit:
 * MOV AH, [pos] keeps AL, a stereo source MOV BH, [pos+1] with BL always 0;
 * 16-bit: whole words); add (unscaled: the sign-extended word, a mono source
 * to both output channels, a stereo source to mono output its left word only;
 * scaled: the left word times gain L, the right word (a mono source: the same
 * word) times gain R, mono output only the left); next frame; the position
 * advances a frame, or resampled ADD ECX, frac / ADC: the integer step or
 * the one beyond it; stop after the last accumulator. */
static uint32_t sos_routine(es2_sim_sos_mixer *x, const es2_guest_memory *m, const sos_run *r, uint32_t pos, uint32_t end,
                            uint32_t *edi, uint32_t ints) {
    uint32_t ax = 0, bx = 0, ecx = 0x80000000u, i = *edi;
    const uint32_t fl = r->fl, frame = ((fl & 2u) ? 2u : 1u) * ((fl & 8u) ? 2u : 1u), step = (fl & 4u) ? 2u : 1u;
    while (pos < end && i + step <= ints) {
        if (fl & 2u) {
            ax = sos_w(m, pos);
            if (fl & 8u) bx = sos_w(m, pos + 2u);
        } else {
            ax = (ax & 0x00ffu) | (sos_b(m, pos) << 8);
            if (fl & 8u) bx = (bx & 0x00ffu) | (sos_b(m, pos + 1u) << 8);
        }
        if (fl & 1u) { ax ^= 0x8000u; if (fl & 8u) bx ^= 0x8000u; }
        uint32_t wl = ax & 0xffffu, wr = (fl & 8u) ? (bx & 0xffffu) : wl;
        if (fl & 0x20u) {
            x->acc[i] += sos_scaled(wl, r->gl, &ax);
            if (fl & 4u) x->acc[i + 1u] += sos_scaled(wr, r->gr, &ax);
        } else {
            x->acc[i] += (int16_t)(uint16_t)wl;
            if (fl & 4u) x->acc[i + 1u] += (int16_t)(uint16_t)wr;
        }
        i += step;
        if (fl & 0x10u) {
            uint32_t c0 = ecx;
            ecx += r->frac;
            pos += ecx < c0 ? r->step1 : r->step0;
        } else pos += frame;
    }
    *edi = i;
    return pos;
}

/* The flags, gains and steps of one run (0x406c00..0x406d49). Returns 1
 * when the resample DIV faulted and the process ended (a test's recorder). */
static int sos_prepare(es2_sim_sos_mixer *x, const es2_sim_sos_voice *v, sos_run *r) {
    memset(r, 0, sizeof(*r));
    if (v->format & 0x8000u) r->fl |= 1u;
    /* +44h bit 4000h (flag 40h) selects routines DBSIM never reaches: the
     * sample load writes +44h = 8000h or 0 (audio.md, "audit of the audio
     * rest"). */
    if (v->pan == 0x8000u) {
        r->gl = v->volume & 0x7fffu;
        r->gr = (v->volume >> 16) & 0x7fffu;
    } else {
        uint32_t bx = 0x7fffu, cx = 0x7fffu;
        if (v->pan & 0x8000u) bx = 0xffffu - v->pan; else cx = v->pan;
        r->gl = sos_gain(v->volume, bx);
        r->gr = sos_gain(v->volume, cx);
    }
    if (r->gl < 0x7ff0u || r->gr < 0x7ff0u) r->fl |= 0x20u;
    if (v->bits != 8u) r->fl |= 2u;
    if (v->channels != 1u) r->fl |= 8u;
    if (x->channels != 1u) r->fl |= 4u;
    if (v->rate != x->rate && x->rate) {
        /* EDX:EAX = rate:0 DIV ECX = output rate << 16 (0x406cf0). A rate of
         * at least output rate * 10000h faults; only a modified WAV header
         * reaches it (audio.md, "Resampling"): the process ends with the
         * original's code at the DLL's DIV. Without a process record the
         * quotient saturates. */
        uint32_t ecx = x->rate << 16;
        uint64_t edx_eax = (uint64_t)v->rate << 32;
        if ((ecx == 0 || v->rate >= ecx) &&
            es2_sim_loop_divide_fault(es2_sim_loop_bound(), ES2_SIM_SOS_DIV_SITE, edx_eax, ecx, 0)) {
            x->faulted = 1;
            return 1;
        }
        uint64_t q = ecx ? edx_eax / ecx : 0xffffffffu;
        if (q > 0xffffffffu) q = 0xffffffffu;
        uint32_t whole = (uint32_t)q >> 16, scale = ((r->fl & 8u) ? 2u : 1u) * ((r->fl & 2u) ? 2u : 1u);
        r->frac = (uint32_t)q << 16;
        r->step0 = (whole * scale) & 0xffffu;          /* word 0x41cc60 */
        r->step1 = ((whole + 1u) * scale) & 0xffffu;   /* word 0x41cc64 */
        r->fl |= 0x10u;
    }
    return 0;
}

void es2_sim_sos_mix(es2_sim_sos_mixer *x, const es2_guest_memory *m) {
    /* 0x406b90..0x406bd2: the accumulators cover the block (bytes << 2 for
     * 8-bit output, << 1 for 16-bit: one dword per output sample), zeroed;
     * +4Ch = 0. */
    uint32_t ints = x->bits == 16u ? x->block / 2u : x->block;
    if (ints > ES2_SIM_SOS_BLOCK_MAX) ints = ES2_SIM_SOS_BLOCK_MAX;
    if (x->channels != 1u) ints &= ~1u;
    memset(x->acc, 0, (size_t)ints * sizeof(x->acc[0]));
    x->count = 0;
    ++x->mixed;
    /* 0x406bdc..0x406e54: the voice list. */
    for (uint32_t k = 0; k < x->list_n; ++k) {
        es2_sim_sos_voice *v = &x->voice[x->list[k]];
        ++x->count;
        if (v->flags & SOS_ENDED) {
            /* 0x406dfa..0x406e21: once, the notice callback 0x403a71: +70h =
             * [0x417ffe], driver 0's written-buffer count +992h. */
            if (!(v->flags & SOS_NOTICED)) { v->done_at = x->written; v->flags |= SOS_NOTICED; }
            continue;
        }
        uint32_t edi = 0;
        unsigned idle = 0;
        for (;;) {
            sos_run r;
            if (sos_prepare(x, v, &r)) return;
            uint32_t end = v->data + v->length, from = edi;
            v->pos = sos_routine(x, m, &r, v->pos, end, &edi, ints);
            v->samples += edi;   /* (EDI - [+40h]) >> 2: from the buffer start, again after a restart */
            if (v->pos < end) break;
            /* 0x406d9f..0x406df5: the end of the data. */
            if (v->loops == 0) {
                /* +24h |= 4000h; the end callback +60h is the default
                 * 0x404140 (DBSIM's +60h is 0): [+0] = 0 first, it posts a
                 * message and leaves [+0] at 0, so the voice stays ended. */
                v->flags |= SOS_ENDED;
                v->data = 0;
                sos_notice(x, (uint32_t)(v - x->voice), SOS_ENDED);
                break;
            }
            if (v->loops != 0xffffffffu) --v->loops;
            v->pos = v->data;   /* 0x406e26 */
            if (edi >= ints) break;
            /* Guard (not in the original, which would loop forever): an
             * endless voice whose data is empty. */
            if (edi == from && ++idle > 1u) break;
        }
        if (x->count >= ES2_SIM_SOS_VOICES) break;   /* 0x406e3a: +4Ch >= +14h */
    }
    /* 0x406e5a..0x406ecd and the routines of table 0x40924c (0x40964c,
     * 0x40965a, 0x409669, 0x40967c, 0x409690, 0x4096a4, 0x4096ba, 0x4098fa,
     * 0x409953): clipped to [-8000h, 7FFFh] only when the list had more than
     * one voice (+4Ch > 1); 8-bit output: bits 8..15 of the accumulator
     * (XOR 8000h first when unsigned); 16-bit: its low word. */
    const int clip = x->count > 1u, unsig = (x->out_flags & 0x8000u) != 0;
    for (uint32_t i = 0; i < ints; ++i) {
        int32_t s = x->acc[i];
        if (clip) s = s > 0x7fff ? 0x7fff : s < -0x8000 ? -0x8000 : s;
        uint32_t u = (uint32_t)s;
        if (x->bits == 16u) { x->out[2u * i] = (uint8_t)u; x->out[2u * i + 1u] = (uint8_t)(u >> 8); }
        else { if (unsig) u ^= 0x8000u; x->out[i] = (uint8_t)(u >> 8); }
    }
}

static void sos_unlink(es2_sim_sos_mixer *x, uint32_t k) {
    memmove(&x->list[k], &x->list[k + 1u], x->list_n - k - 1u);
    --x->list_n;
}
static uint32_t sos_refill(es2_sim_sos_mixer *x, const es2_guest_memory *m);
/* One buffer back (WOM_DONE, 0x402fa1..0x403123): +87Eh--, +996h++, the
 * service thread resumed (0x403055, caps +70h bit 0; DBSIM sets +70h = 1);
 * then every listed voice with +70h == +996h is removed (+24h = 2000h,
 * unlinked, the remove callback: the default 0x4041ae posts a message). The
 * thread runs at THREAD_PRIORITY_TIME_CRITICAL (0x402b19), so its refill is
 * taken to run before the removals (hypothesis; with one buffer only this
 * order lets a voice be removed at all). Not during a reset (0x402e8a stops
 * the thread first through 0x4039f9: `refill` 0). */
static void sos_buffer_done(es2_sim_sos_mixer *x, const es2_guest_memory *m, int refill) {
    --x->outstanding;
    ++x->done;
    if (refill) sos_refill(x, m);
    for (uint32_t k = 0; k < x->list_n;) {
        es2_sim_sos_voice *v = &x->voice[x->list[k]];
        if (v->done_at == x->done) {
            uint32_t handle = x->list[k];
            v->flags = SOS_REMOVED;
            sos_unlink(x, k);
            sos_notice(x, handle, SOS_REMOVED);   /* the remove callback, called at 0x403101 */
        }
        else ++k;
    }
}
static uint32_t sos_write(es2_sim_sos_mixer *x, const es2_guest_memory *m) {
    if (x->faulted) return 0x280000u;
    es2_sim_sos_mix(x, m);
    if (x->faulted) return 0x280000u;
    uint32_t e = x->sink.write ? x->sink.write(x->sink.context, x->out, x->block) : 1u;
    if (e) { x->sink_error = e; return e | 0x280000u; }
    ++x->written;
    ++x->outstanding;
    /* The ring runs dry (audio.md, "the ring runs dry as waveOut's does"): a buffer mixed
     * with no voice listed (+4Ch 0 after the mix) is the last write until a
     * start primes or refills (the user service 0x40316b and the thread
     * 0x403809 refill only with +4Ch nonzero). waveOut plays every written
     * buffer to its end and returns it; an SDL stream that resamples keeps its
     * last frames until more input follows or it is flushed, so the sink is
     * flushed here. */
    if (!x->count && x->sink.flush) {
        x->sink.flush(x->sink.context);
        ++x->flushes;
    }
    return 0;
}
/* The refill of the service thread 0x403809 and of the user service
 * 0x40316b: nothing when +4Ch is 0 (+880h and +87Ch stay 0 in DBSIM's use);
 * otherwise every free buffer of the ring: mix, waveOutWrite, +992h++,
 * +87Eh++. */
static uint32_t sos_refill(es2_sim_sos_mixer *x, const es2_guest_memory *m) {
    if (!x->opened || !x->count || !x->block) return 0;
    while (x->outstanding < x->buffers) {
        uint32_t e = sos_write(x, m);
        if (e) return e;
    }
    return 0;
}
/* The priming 0x402ca4 (0x402d0b..0x402e36): the ring emptied, every buffer
 * mixed and written. */
static void sos_prime(es2_sim_sos_mixer *x, const es2_guest_memory *m) {
    if (!x->opened || !x->block) return;
    for (uint32_t i = 0; i < x->buffers; ++i)
        if (sos_write(x, m)) return;
}
void es2_sim_sos_pump(es2_sim_sos_mixer *x, const es2_guest_memory *m) {
    if (!x->opened || !x->outstanding || !x->block || !x->sink.queued) return;
    uint64_t q = x->sink.queued(x->sink.context);
    uint64_t pending = (q + x->block - 1u) / x->block;
    uint32_t back = pending >= x->outstanding ? 0u : x->outstanding - (uint32_t)pending;
    while (back--) sos_buffer_done(x, m, (x->service & 3u) != 0);
}

void es2_sim_sos_mixer_init(es2_sim_sos_mixer *x, const es2_vt *vt, const es2_sim_sos_sink *sink) {
    memset(x, 0, sizeof(*x));
    es2_sim_audio_silent_init(&x->silent, vt);
    if (sink) x->sink = *sink;
    for (unsigned i = 0; i < ES2_SIM_SOS_VOICES; ++i) x->voice[i].done_at = 0xffffffffu;
}
/* waveOutReset (every outstanding buffer comes back) and waveOutClose
 * (0x402e8a, 0x403bbc). */
static void sos_close(es2_sim_sos_mixer *x) {
    if (!x->opened) return;
    if (x->sink.reset) x->sink.reset(x->sink.context);
    while (x->outstanding) sos_buffer_done(x, NULL, 0);
    if (x->sink.close) x->sink.close(x->sink.context);
    x->opened = 0;
}
static uint32_t sos_open(es2_sim_sos_mixer *x) {
    uint32_t e = x->sink.open ? x->sink.open(x->sink.context, x->rate, x->channels, x->bits) : 2u;
    if (e) { x->sink_error = e; return 0x320000u | (e & 0xffffu); }
    x->opened = 1;
    return 0;
}
/* sosDIGIInitDriver 0x401596 with the format decode 0x403547..0x403778 for
 * the MME device 0x402810: +64h = caps +6Eh; caps +4Eh & 70h: 10h 11025 Hz,
 * 20h 22050 Hz (+64h * 2), 40h 44100 Hz (* 4); & 3: 1 8-bit (+10h = 8000h:
 * unsigned output), 2 16-bit (* 2); & 0Ch: 4 mono, 8 stereo (* 2); +62h =
 * caps +6Ch buffers; waveOutOpen, a failure answering MMRESULT | 320000h. */
static uint32_t sos_init_driver(es2_sim_sos_mixer *x, const es2_guest_memory *m, uint32_t caps, uint32_t handle) {
    if (x->initialised) sos_close(x);
    uint32_t f = sos_w(m, caps + 0x4eu), block = sos_w(m, caps + 0x6eu);
    x->rate = x->bits = x->channels = x->out_flags = 0;
    switch (f & 0x70u) {
    case 0x10u: x->rate = 11025u; break;
    case 0x20u: x->rate = 22050u; block *= 2u; break;
    case 0x40u: x->rate = 44100u; block *= 4u; break;
    default: break;
    }
    switch (f & 3u) {
    case 1u: x->bits = 8u; x->out_flags = 0x8000u; break;
    case 2u: x->bits = 16u; block *= 2u; break;
    default: break;
    }
    switch (f & 0xcu) {
    case 4u: x->channels = 1u; break;
    case 8u: x->channels = 2u; block *= 2u; break;
    default: break;
    }
    x->block = block & 0xffffu;
    x->buffers = sos_w(m, caps + 0x6cu);
    x->service = sos_w(m, caps + 0x70u);
    if (x->buffers > ES2_SIM_SOS_RING) x->buffers = ES2_SIM_SOS_RING;   /* the original would overrun its ring */
    /* A format without a rate, width or channel count: waveOutOpen's
     * WAVERR_BADFORMAT (DBSIM asks for 29h or 15h only). */
    if (!x->rate || !x->bits || !x->channels) return 0x320000u | 32u;
    memset(x->voice, 0, sizeof(x->voice));
    for (unsigned i = 0; i < ES2_SIM_SOS_VOICES; ++i) x->voice[i].done_at = 0xffffffffu;
    x->list_n = x->count = x->written = x->done = x->outstanding = 0;
    uint32_t e = sos_open(x);
    if (e) return e;
    x->initialised = 1;
    m->write16(m->context, handle, 0);
    return 0;
}
static es2_sim_sos_voice *sos_voice(es2_sim_sos_mixer *x, uint32_t h) {
    /* The DLL indexes its 32 voices without a range check; a handle beyond
     * them is answered as an unused voice here. */
    h &= 0xffffu;
    return h < ES2_SIM_SOS_VOICES ? &x->voice[h] : NULL;
}
/* sosDIGIStartSample 0x405dc0..0x4061aa. */
static uint32_t sos_start(es2_sim_sos_mixer *x, const es2_guest_memory *m, uint32_t d, uint32_t p, uint32_t handle) {
    if (!x->initialised) return 0x50000u | (d & 0xffffu);
    if (!p) return 0x80000u;
    uint32_t before = x->count, i = 0;
    while (i < ES2_SIM_SOS_VOICES && (x->voice[i].flags & SOS_USED)) ++i;
    if (i == ES2_SIM_SOS_VOICES) return 0x17u;
    es2_sim_sos_voice *v = &x->voice[i];
    v->data = sos_d(m, p);
    v->pos = v->data;
    v->length = sos_d(m, p + 0x10u);
    v->flags = sos_d(m, p + 0x24u) | SOS_USED;
    v->volume = sos_d(m, p + 0x30u);
    v->loops = sos_d(m, p + 0x34u);
    v->rate = sos_d(m, p + 0x38u);
    v->bits = sos_d(m, p + 0x3cu);
    v->channels = sos_d(m, p + 0x40u);
    v->format = sos_d(m, p + 0x44u);
    v->pan = sos_d(m, p + 0x48u);
    v->samples = sos_d(m, p + 0x5cu);
    v->done_at = 0xffffffffu;
    x->list[x->list_n++] = (uint8_t)i;     /* appended at the tail (0x4060b7..) */
    m->write16(m->context, handle, (uint16_t)i);
    if (x->opened && before == 0 && x->outstanding == 0) sos_prime(x, m);
    ++x->count;
    return 0;
}
static uint32_t mixer_sos(void *context, const es2_guest_memory *m, es2_sim_sos_call k, const uint32_t *a, unsigned n) {
    es2_sim_sos_mixer *x = context;
    if ((unsigned)k < ES2_SOS_CALL_COUNT) ++x->calls[k];
    es2_sim_sos_pump(x, m);
    uint32_t a0 = n > 0 ? a[0] : 0u, a1 = n > 1 ? a[1] : 0u, a2 = n > 2 ? a[2] : 0u;
    es2_sim_sos_voice *v;
    switch (k) {
    case ES2_SOS_INIT_DRIVER: return sos_init_driver(x, m, a0, a1);
    case ES2_SOS_UNINIT_DRIVER:
        sos_close(x);
        x->initialised = 0;
        return 0;
    case ES2_SOS_SET_ACTIVE_WINDOW:
        /* sosDIGISetActiveWindow 0x404050..0x404134: a driver >= 1: driver |
         * 180000h; the driver word without 1000h (not initialised): driver |
         * 50000h; window 0: the entry 0 (0x4040db); else IsWindow (0x4040ed),
         * 0: window | 40000h, else the entry = window (0x404122). 0 when
         * stored. */
        if ((a0 & 0xffffu) >= 1u) return 0x180000u | (a0 & 0xffffu);
        if (!x->initialised) return 0x50000u | (a0 & 0xffffu);
        if (a1 && x->is_window && !x->is_window(x->post_user, a1)) return a1 | 0x40000u;
        x->window = a1;
        return 0;
    case ES2_SOS_START_SAMPLE: return sos_start(x, m, a0, a1, a2);
    case ES2_SOS_STOP_SAMPLE:   /* 0x4061ad: used: +24h |= 4000h; 0 either way */
        if ((v = sos_voice(x, a1)) && (v->flags & SOS_USED)) v->flags |= SOS_ENDED;
        return 0;
    case ES2_SOS_STOP_ALL:      /* 0x40623c: every voice +24h |= 4000h */
        if ((a0 & 0xffffu) >= 1u) return 0x180000u | (a0 & 0xffffu);
        if (!x->initialised) return 0x50000u | (a0 & 0xffffu);
        for (unsigned i = 0; i < ES2_SIM_SOS_VOICES; ++i) x->voice[i].flags |= SOS_ENDED;
        return 0;
    case ES2_SOS_SAMPLE_DONE:   /* 0x405cd0: (+24h & 8000h) == 0 */
        return !((v = sos_voice(x, a1)) && (v->flags & SOS_USED));
    case ES2_SOS_SET_VOLUME: {  /* 0x40556b: the old low word; unused: 18h */
        if (!(v = sos_voice(x, a1)) || !(v->flags & SOS_USED)) return 0x18u;
        uint32_t old = v->volume & 0xffffu;
        v->volume = a2;
        return old;
    }
    case ES2_SOS_SET_PAN: {     /* 0x405850: words; unused: handle | 180000h */
        if (!(v = sos_voice(x, a1)) || !(v->flags & SOS_USED)) return (a1 & 0xffffu) | 0x180000u;
        uint32_t old = v->pan & 0xffffu;
        v->pan = a2 & 0xffffu;
        return old;
    }
    case ES2_SOS_SET_RATE: {    /* 0x405996: the same on +38h */
        if (!(v = sos_voice(x, a1)) || !(v->flags & SOS_USED)) return (a1 & 0xffffu) | 0x180000u;
        uint32_t old = v->rate & 0xffffu;
        v->rate = a2 & 0xffffu;
        return old;
    }
    case ES2_SOS_USER_SERVICE:  /* 0x40316b */
        if (!x->initialised) return 0x50000u | (a0 & 0xffffu);
        if (x->service & 1u) return 0;   /* 0x403205: the service thread refills */
        return sos_refill(x, m);
    case ES2_SOS_RELEASE_DRIVER: /* 0x403bbc: waveOutReset, the buffers back, waveOutClose */
        /* Flag 1000h (DBSIM's pause passes it): first a spin until no buffer
         * is outstanding (+87Eh 0, 0x403c73..0x403c9b) while the service
         * thread goes on: stopped voices are noticed and removed and the
         * ring runs dry once the list is empty. Here the buffers come back
         * at once instead of at the device's pace; a voice still playing
         * would make the original spin forever, so the drain is bounded. */
        if ((a1 & 0x1000u) && x->opened)
            for (unsigned n = 0; x->outstanding && n < 4u * ES2_SIM_SOS_RING + 4u; ++n) sos_buffer_done(x, m, (x->service & 3u) != 0);
        sos_close(x);
        return 0;
    case ES2_SOS_ACQUIRE_DRIVER: { /* 0x403d7e: waveOutOpen again; voices listed: the priming */
        if (!x->initialised || x->opened) return 0;
        uint32_t e = sos_open(x);
        if (e) return e;
        if (x->count) sos_prime(x, m);
        return 0;
    }
    default: return silent_sos(&x->silent, m, k, a, n);   /* detection, system and timer calls */
    }
}
static uint32_t mixer_mci(void *context, const es2_guest_memory *m, uint32_t device, uint32_t message, uint32_t flags, uint32_t params) {
    es2_sim_sos_mixer *x = context;
    return silent_mci(&x->silent, m, device, message, flags, params);
}
void es2_sim_sos_mixer_device(es2_sim_sos_mixer *x, es2_sim_audio_device *device) {
    device->context = x;
    device->sos = mixer_sos;
    device->mci = mixer_mci;
}
/* ======================================================================
 * mciSendCommandA through the system (audio.md, "sound and CD music in the
 * window"). The guest blocks of the own CD code (cd_stop, cd_status, cd_play
 * above; listing 0x473c00..0x473ea8) become host blocks of the same field
 * order: field 0 the callback, then MCI_OPEN_PARMSA (wDeviceID,
 * lpstrDeviceType, lpstrElementName, lpstrAlias), MCI_PLAY_PARMS (dwFrom,
 * dwTo), MCI_SET_PARMS (dwTimeFormat, dwAudio), MCI_STATUS_PARMS (dwReturn,
 * dwItem, dwTrack); MCI_STOP and MCI_CLOSE with a block: the callback only
 * (MCI_GENERIC_PARMS). DBSIM sends them without a block (0x473c09..0x473c28). */
#define MCI_NOTIFY_FLAG 0x1u
#define MCI_OPEN_ELEMENT 0x200u
#define MCI_OPEN_ALIAS 0x400u
#define MCI_OPEN_ELEMENT_ID 0x800u
#define MCI_OPEN_TYPE_ID 0x1000u
#define MCI_OPEN_TYPE 0x2000u
#define MCIERR_UNSUPPORTED 274u          /* MCIERR_UNSUPPORTED_FUNCTION */
#define MCIERR_DEVICE_NAME 263u          /* MCIERR_INVALID_DEVICE_NAME (guard: an unterminated string) */
void es2_sim_mci_host_init(es2_sim_mci_host *h, uint32_t (*send)(void *user, uint32_t device, uint32_t message, uint32_t flags, void *params),
                           void *user, uintptr_t notify_window) {
    memset(h, 0, sizeof(*h));
    h->send = send;
    h->user = user;
    h->notify_window = notify_window;
}
static unsigned mci_fields(uint32_t message) {
    switch (message) {
    case MCI_OPEN: return 5u;
    case MCI_STATUS: return 4u;
    case MCI_PLAY: case MCI_SET: return 3u;
    case MCI_CLOSE: case MCI_STOP: return 1u;
    default: return 0;
    }
}
/* A guest string of at most ES2_SIM_MCI_STRING - 1 characters; 1 when it
 * has no terminator within that. */
static int mci_string(const es2_guest_memory *m, uint32_t va, char *out) {
    for (unsigned i = 0; i < ES2_SIM_MCI_STRING; ++i) {
        out[i] = (char)(m->read16(m->context, va + i) & 0xffu);
        if (!out[i]) return 0;
    }
    out[0] = 0;
    return 1;
}
uint32_t es2_sim_mci_send(es2_sim_mci_host *h, const es2_guest_memory *m, uint32_t device, uint32_t message, uint32_t flags,
                          uint32_t params) {
    ++h->calls;
    h->last_message = message;
    h->last_flags = flags;
    if (!h->send) return h->last_error = 306u;
    if (!params) {
        ++h->sent;
        return h->last_error = h->send(h->user, device, message, flags, NULL);
    }
    unsigned n = mci_fields(message);
    if (!n) { ++h->unsupported; return h->last_error = MCIERR_UNSUPPORTED; }
    uint32_t guest[ES2_SIM_MCI_FIELDS] = {0};
    uintptr_t sent[ES2_SIM_MCI_FIELDS] = {0};
    int kept[ES2_SIM_MCI_FIELDS] = {0};   /* a field the host replaced: not written back */
    for (unsigned i = 0; i < n; ++i) sent[i] = guest[i] = m->read32(m->context, params + 4u * i);
    if ((flags & MCI_NOTIFY_FLAG) && guest[0]) {
        ++h->notify_requests;
        h->guest_window = guest[0];
        if (h->notify_window) { sent[0] = h->notify_window; kept[0] = 1; }
    }
    if (message == MCI_OPEN) {
        const struct { uint32_t flag, id_flag; unsigned field; } names[3] = {
            {MCI_OPEN_TYPE, MCI_OPEN_TYPE_ID, 2u}, {MCI_OPEN_ELEMENT, MCI_OPEN_ELEMENT_ID, 3u}, {MCI_OPEN_ALIAS, 0, 4u}};
        for (unsigned s = 0; s < 3u; ++s) {
            if (!(flags & names[s].flag) || (flags & names[s].id_flag)) continue;
            if (mci_string(m, guest[names[s].field], h->strings[s])) return h->last_error = MCIERR_DEVICE_NAME;
            sent[names[s].field] = (uintptr_t)h->strings[s];
            kept[names[s].field] = 1;
        }
    }
    memcpy(h->block, sent, sizeof(h->block));
    ++h->sent;
    uint32_t e = h->send(h->user, device, message, flags, h->block);
    for (unsigned i = 0; i < n; ++i)
        if (!kept[i] && h->block[i] != sent[i]) m->write32(m->context, params + 4u * i, (uint32_t)h->block[i]);
    return h->last_error = e;
}
