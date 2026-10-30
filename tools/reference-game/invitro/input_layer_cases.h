/* DBSIM's input layer, original machine code against sim/src/input.c
 * (--input-layer; target :differential_input_layer_test). Included after
 * loop_cases.h, whose loop record (lc_reset: DATA, the counter, the heap,
 * virtual time), GetTickCount stub and machine call, and startup_cases.h's
 * snapshots (su_take, su_compare), call log (su_note), file model and import
 * stubs it reuses.
 *
 * - setup: the input set-up 0x45a5dc from random states: prefs.cfg missing,
 *   short or 54 random bytes (the joystick byte 0xc 0, 1, 2 or random), the
 *   voice archive present or not, keyjoy.cfg values, joystick journals
 *   (joyGetDevCapsA and joyGetPosEx answered or failing per device, blobs of
 *   808 and 52 bytes), the flight-key capture byte and its skip byte, the
 *   language letter, partly used hot-key, handler and graphics-slot tables,
 *   the key ring and the mouse block. prefs_load, keyjoy_load and
 *   prefs_store run as the original's machine code on both sides (they are
 *   hooks; the own side's dispatch passes them through).
 * - poll: after a set-up, 1..6 device polls 0x45baa8 over random key rings,
 *   mouse blocks and joystick journals per frame, with the readers 0x45bbc4,
 *   0x45c524 and the summary 0x477904 called directly between them (EAX and,
 *   where no import is the last writer, EDX compared), the prefs setter
 *   0x459958 with random callback tables of the five set-up callbacks, the
 *   key flush 0x477abc, the mouse bounds 0x45b758 over a fake surface, the
 *   array constructor 0x49667c and the surface attribute 0x481350.
 * - mouse: after a set-up and the mouse queue set-up 0x452ae4, random mouse
 *   messages through the handler 0x4809f8 (the live-mouse byte, the scales
 *   from GetClientRect or preset, the queue callback 0x452cdc with the clock
 *   and mouse_queue as the original on both sides).
 * - streams: the read stream 0x47ad40 / 0x47af98 / 0x47ad78 on random files.
 *
 * Compared after each case: DATA 0x497000..0x6cc000, the used heap, the
 * import-level call log, the journals' state, virtual time, the files the
 * case writes, and the registers named above. Every import the path reaches
 * points at a stub over the same model the own side's service uses. */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "es2/sim/input.h"
#include "es2/sim/audio.h"
#include "es2/formats/cfg.h"

static es2_sim_entry ic_entries[4096];
static es2_sim_dispatch ic_dispatch;
static es2_sim_services ic_services;
static uint32_t ic_fs0;
static es2_sim_input ic_input;
static es2_journal ic_journal[2];
static char ic_ini[512];
static size_t ic_ini_size;
static int32_t ic_client[2];
static char ic_root[MAX_PATH], ic_prefs_path[MAX_PATH];
static BYTE ic_prefs[54];
static unsigned ic_prefs_length;   /* 0: no file */
static BYTE *ic_surface, *ic_scratch;
static int ic_bad;

/* ---- the original side's stubs (the proxy's replay semantics for the
 * journal, input_journal.h) */
static uint32_t ic_frame(void) { return get32(mapped + 0xd257bu); }
static uint32_t __stdcall ic_key_state(int32_t vk) {
    su_note('y');
    es2_journal *j = &ic_journal[ir_side];
    es2_journal_entry *e = es2_journal_take(j, ES2_JOURNAL_KEY, ic_frame(), es2_journal_match_first, vk);
    return (uint32_t)(int32_t)(e ? (int16_t)e->b : es2_journal_key_fallback(j, vk));
}
static uint32_t __stdcall ic_joy_caps(uint32_t id, BYTE *caps, uint32_t size) {
    su_note('J');
    es2_journal *j = &ic_journal[ir_side];
    es2_journal_entry *e = es2_journal_take(j, ES2_JOURNAL_JOYCAPS, ic_frame(), es2_journal_match_first, (int32_t)id);
    if (!e) { ++j->fallbacks; return 165u; }
    if (caps && e->blob && e->blob_size == size) memcpy(caps, e->blob, size);
    return (uint32_t)e->b;
}
static uint32_t __stdcall ic_joy_pos(uint32_t id, BYTE *info) {
    su_note('j');
    es2_journal *j = &ic_journal[ir_side];
    es2_journal_entry *e = es2_journal_take(j, ES2_JOURNAL_JOYPOS, ic_frame(), es2_journal_match_first, (int32_t)id);
    if (!e) { ++j->fallbacks; return 167u; }
    if (info && e->blob && e->blob_size == get32(info)) memcpy(info, e->blob, e->blob_size);
    return (uint32_t)e->b;
}
static uint32_t __stdcall ic_client_rect(uint32_t window, uint32_t *rect) {
    (void)window;
    su_note('r');
    rect[0] = 0; rect[1] = 0; rect[2] = (uint32_t)ic_client[0]; rect[3] = (uint32_t)ic_client[1];
    return 1;
}
static uint32_t __stdcall ic_profile(const char *section, const char *key, const char *fallback, char *buffer, uint32_t size,
                                     const char *file) {
    (void)file;
    su_note('G');
    char value[256];
    int found = 0;
    size_t capacity = size < sizeof(value) ? size : sizeof(value);
    es2_span span = {(const uint8_t *)ic_ini, ic_ini_size};
    es2_error error;
    if (!capacity) return 0;
    if (es2_cfg_ini_get(span, section, key, value, capacity, &found, &error) != ES2_OK) found = 0;
    if (!found) snprintf(value, capacity, "%s", fallback);
    strcpy(buffer, value);
    return (uint32_t)strlen(value);
}
static uint32_t __cdecl ic_memset(uint32_t d, uint32_t v, uint32_t n) { su_note('S'); uint32_t a[3] = {d, v, n}; return su_rtl_call(ES2_RTL_MEMSET, a); }
static uint32_t __cdecl ic_memcpy(uint32_t d, uint32_t s, uint32_t n) { su_note('M'); uint32_t a[3] = {d, s, n}; return su_rtl_call(ES2_RTL_MEMCPY, a); }
static uint32_t __cdecl ic_memcmp(uint32_t d, uint32_t s, uint32_t n) { su_note('P'); uint32_t a[3] = {d, s, n}; return su_rtl_call(ES2_RTL_MEMCMP, a); }
static uint32_t __cdecl ic_strcat(uint32_t d, uint32_t s) { su_note('T'); uint32_t a[2] = {d, s}; return su_rtl_call(ES2_RTL_STRCAT, a); }
static uint32_t __cdecl ic_strncpy(uint32_t d, uint32_t s, uint32_t n) { su_note('N'); uint32_t a[3] = {d, s, n}; return su_rtl_call(ES2_RTL_STRNCPY, a); }
static uint32_t __cdecl ic_fclose(uint32_t f) { (void)f; su_note('k'); return 0; }
static uint32_t __cdecl ic_write(uint32_t h, uint32_t b, uint32_t n) { uint32_t a[3] = {h, b, n}; return su_io(ES2_IO_WRITE, a, 3, 'W'); }

/* ---- the own side: the sim services with the logging wrappers, the hooks
 * and the other packages' callees as the original's machine code */
static void ic_original(es2_sim_service_call *call, void *user) {
    unsigned n = (unsigned)(uintptr_t)user;
    es2_hook_result r;
    uint32_t a[4] = {0, 0, 0, 0};
    for (unsigned i = 0; i < n; ++i) a[i] = call->slots[i];
    uint32_t regs[3] = {call->eax, call->edx, call->ecx};   /* the entry registers the own code names */
    es2_hook_call c = {mapped + (call->va - 0x400000u), ES2_ABI_CDECL, n, a, initial_fpu, &r, regs};
    es2_hook_machine_call(&c);
    call->eax = r.eax; call->edx = r.edx;
}
static const struct { uint32_t va; unsigned slots; const char *name; } ic_passed[] = {
    {0x00459770u, 1, "prefs_load (original)"}, {0x0045b7a8u, 0, "keyjoy_load (original)"},
    {0x00459b94u, 2, "prefs_store (original)"}, {0x00453078u, 2, "mouse_queue (original)"},
    /* The sound groups 0x462e58, 0x462d78, 0x462efc, 0x462ddc are audio.h's
     * rows (sim/src/audio.c). */
};
static struct { uint32_t va; char letter; es2_sim_service_fn real; } ic_wrapped[] = {
    {0x0047749cu, 'C', NULL}, {0x0047747cu, 'C', NULL}, {0x004774f0u, 'C', NULL}, {0x00477548u, 'F', NULL},
    {0x0047758cu, 'F', NULL}, {0x004967a8u, 'F', NULL}, {0x0049681au, 'S', NULL}, {0x0049680eu, 'M', NULL},
    {0x004967bau, 'P', NULL}, {0x004967eau, 'T', NULL}, {0x00496760u, 'N', NULL}, {0x00496868u, 'I', NULL},
    {0x00496802u, 'f', NULL}, {0x00496838u, 'k', NULL}, {0x004967c6u, 'O', NULL}, {0x00496874u, 'K', NULL},
    {0x00496808u, 'R', NULL}, {0x00496826u, 'L', NULL}, {0x0049677eu, 'W', NULL}, {0x00496a66u, 'J', NULL},
    {0x00496a6cu, 'j', NULL}, {0x00496970u, 'r', NULL}, {0x00496910u, 'G', NULL}, {0x0049695eu, 'y', NULL},
    /* 0x45ee00 appends the path with _strcat; the file_imports.h row does it in
     * place. */
    {0x0045ee00u, 'T', NULL},
};
static void ic_wrapper(es2_sim_service_call *call, void *user) {
    for (unsigned i = 0; i < sizeof(ic_wrapped) / sizeof(ic_wrapped[0]); ++i) {
        if (ic_wrapped[i].va != call->va) continue;
        if (ic_wrapped[i].letter != 'F' || call->slots[0] || call->va == 0x004967a8u) su_note(ic_wrapped[i].letter);
        if (ic_wrapped[i].letter == 'f') { call->eax = su_fopen_result; return; }
        if (ic_wrapped[i].letter == 'k') { call->eax = 0; return; }
        ic_wrapped[i].real(call, user);
        return;
    }
}
static void ic_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own input: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    ic_bad = 1;
}
static int ic_dispatch_setup(void) {
    es2_sim_dispatch_init(&ic_dispatch, ic_entries, 4096, ic_refuse, NULL);
    ic_services.memory = &ir_memory;
    ic_services.heap = &ir_heap;
    ic_services.vt = &lc_vt;
    ic_services.fs0 = &ic_fs0;
    if (es2_sim_register_services(&ic_dispatch, &ic_services)) return fail("input: dispatch");
    for (unsigned i = 0; i < sizeof(ic_passed) / sizeof(ic_passed[0]); ++i)
        if (es2_sim_dispatch_add_service(&ic_dispatch, ic_passed[i].va, ic_passed[i].name, ic_original,
                                         (void *)(uintptr_t)ic_passed[i].slots, 0, (uint8_t)ic_passed[i].slots))
            return fail("input: dispatch");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&ic_dispatch, &error, &bad)) return fail("input: dispatch seal");
    /* The cursor follow 0x486e7c is gfx_blit.h's row: passed to the original. */
    es2_sim_entry *follow = (es2_sim_entry *)es2_sim_dispatch_find(&ic_dispatch, 0x00486e7cu);
    if (!follow || follow->kind != ES2_SIM_SERVICE) return fail("input: cursor follow missing");
    follow->service = ic_original;
    follow->service_user = (void *)(uintptr_t)1u;
    for (unsigned i = 0; i < sizeof(ic_wrapped) / sizeof(ic_wrapped[0]); ++i) {
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&ic_dispatch, ic_wrapped[i].va);
        if (!e || e->kind != ES2_SIM_SERVICE) return fail("input: service missing");
        ic_wrapped[i].real = e->service;
        e->service = ic_wrapper;
    }
    return 0;
}

/* ---- a case's random environment */
static uint32_t ic_rand(uint32_t n) { return random32() % n; }
static void ic_write_file(const char *path, const BYTE *bytes, unsigned n) {
    FILE *f = fopen(path, "wb");
    if (!f) { ic_bad = 1; return; }
    if (n && fwrite(bytes, 1, n, f) != n) ic_bad = 1;
    fclose(f);
}
static void ic_read_file(const char *path, BYTE *out, unsigned capacity, unsigned *n) {
    FILE *f = fopen(path, "rb");
    if (!f) { *n = 0; return; }
    *n = (unsigned)fread(out, 1, capacity, f);
    fclose(f);
}
/* A journal of joystick answers for frames frame..frame + frames - 1. */
static size_t ic_joystick_journal(char *text, size_t capacity, uint32_t frame, unsigned frames) {
    size_t n = 0;
    static char hex[2 * 808 + 1];
    for (unsigned f = 0; f < frames; ++f)
        for (uint32_t id = 0; id < 2u; ++id) {
            if (f == 0 && ic_rand(4)) {
                int ok = ic_rand(2);
                unsigned k = 0;
                for (unsigned i = 0; i < 808u; ++i) k += (unsigned)sprintf(hex + k, "%02x", (unsigned)(i < 0x80u || ic_rand(8) == 0 ? ic_rand(256) : 0));
                n += (size_t)snprintf(text + n, capacity - n, "{\"f\":%lu,\"api\":\"joycaps\",\"a\":%lu,\"b\":%u,\"c\":808,\"hex\":\"%s\"}\n",
                                      (unsigned long)(frame + f), (unsigned long)id, ok ? 0u : 165u, ok ? hex : "");
            }
            if (ic_rand(3)) {
                int ok = ic_rand(3) != 0;
                unsigned k = 0;
                uint32_t v[13];
                v[0] = ic_rand(8) ? 0x34u : random32();
                for (unsigned i = 1; i < 13u; ++i) v[i] = ic_rand(4) ? ic_rand(0x10000) : random32();
                v[10] = ic_rand(2) ? 0u : ic_rand(2) ? 0x4650u : ic_rand(2) ? 0x6978u : ic_rand(2) ? 0x2328u : 0xffffu;
                for (unsigned i = 0; i < 13u; ++i)
                    for (unsigned b = 0; b < 4u; ++b) k += (unsigned)sprintf(hex + k, "%02x", (unsigned)((v[i] >> (8u * b)) & 0xffu));
                n += (size_t)snprintf(text + n, capacity - n, "{\"f\":%lu,\"api\":\"joypos\",\"a\":%lu,\"b\":%u,\"hex\":\"%s\"}\n",
                                      (unsigned long)(frame + f), (unsigned long)id, ok ? 0u : 167u, ok ? hex : "");
            }
        }
    return n;
}
static int ic_load_journals(const char *text, size_t n) {
    for (unsigned s = 0; s < 2u; ++s) {
        es2_journal_free(&ic_journal[s]);
        es2_journal_init(&ic_journal[s]);
        if (es2_journal_load_text(&ic_journal[s], text, n, NULL)) return fail("input: journal");
    }
    return 0;
}
/* The environment both sides start from (DATA after the reset). */
typedef struct ic_env {
    uint32_t frame, fopen_result;
    uint8_t capture, skip, language, prefs_path_set, mouse_live;
    uint32_t hotkeys[30], handlers[10], slots[3], ring_write, ring_read, ring[256], dib[2];
    BYTE mouse[10];
} ic_env;
static void ic_random_env(ic_env *e) {
    memset(e, 0, sizeof(*e));
    e->frame = ic_rand(4) ? 0 : ic_rand(100);
    e->fopen_result = ic_rand(2) ? 0x00123450u : 0;
    e->capture = (uint8_t)(ic_rand(4) == 0);
    e->skip = (uint8_t)(ic_rand(4) == 0);
    static const char letters[] = "egfrsx";
    e->language = (uint8_t)(ic_rand(8) ? letters[ic_rand(6)] : ic_rand(256));
    e->prefs_path_set = (uint8_t)(ic_rand(5) == 0);
    for (unsigned i = 0; i < 10u; ++i)
        if (ic_rand(4) == 0) { e->hotkeys[3 * i] = 1; e->hotkeys[3 * i + 1] = 0x0049e000u + ic_rand(0x100); e->hotkeys[3 * i + 2] = 0x00477a7cu; }
    for (unsigned i = 0; i < 10u; ++i) e->handlers[i] = ic_rand(5) == 0 ? 0x00470000u + 4u * ic_rand(64) : 0;
    for (unsigned i = 0; i < 3u; ++i) e->slots[i] = ic_rand(6) == 0 ? 0x00010000u + 0x100u * i : 0;
    e->ring_write = ic_rand(4) ? ic_rand(256) : 0;
    e->ring_read = ic_rand(4) ? ic_rand(256) : e->ring_write;
    for (unsigned i = 0; i < 256u; ++i) e->ring[i] = ic_rand(0x10000);
    for (unsigned i = 0; i < 10u; ++i) e->mouse[i] = (BYTE)ic_rand(256);
    e->mouse_live = (uint8_t)ic_rand(2);
    e->dib[0] = 320u << ic_rand(3);
    e->dib[1] = 240u << ic_rand(3);
}
static void ic_apply_env(const ic_env *e) {
    put32(mapped + 0xd257bu, e->frame);
    memcpy(mapped + 0xd27bcu, ic_root, strlen(ic_root) + 1);
    mapped[0x9eaa5u] = e->capture;
    mapped[0x9eb61u] = e->skip;
    mapped[0xd2536u] = e->language;
    if (e->prefs_path_set) memcpy(mapped + 0x9e81cu, ic_prefs_path, strlen(ic_prefs_path) + 1);
    for (unsigned i = 0; i < 30u; ++i) put32(mapped + 0x2bb8b8u + 4u * i, e->hotkeys[i]);
    for (unsigned i = 0; i < 10u; ++i) put32(mapped + 0xd3b30u + 4u * i, e->handlers[i]);
    for (unsigned i = 0; i < 3u; ++i) put32(mapped + 0x2c5eb8u + 4u * i, e->slots[i]);
    put32(mapped + 0x2bb8b0u, e->ring_write);
    put32(mapped + 0x2bb8b4u, e->ring_read);
    for (unsigned i = 0; i < 256u; ++i) put16(mapped + 0x2bb6b0u + 2u * i, (uint16_t)e->ring[i]);
    memcpy(mapped + 0x2c5f50u, e->mouse, 10);
    mapped[0xd1dd6u] = e->mouse_live;
    put32(mapped + 0xd3610u, 0x00010001u);   /* the window */
    put32(mapped + 0xd3040u, e->dib[0]);   /* the DIB size the mouse scales use */
    put32(mapped + 0xd3044u, e->dib[1]);
    su_fopen_result = e->fopen_result;
}
static void ic_side(unsigned side) {
    lc_reset(side, 47, 0x01000000u, 1);
    lc_loop.dispatch = &ic_dispatch;
    lc_side = side;
    es2_sim_input_headless(&ic_input, &lc_loop, &ic_journal[1], ic_client[0], ic_client[1]);
    es2_sim_input_bind(&ic_input);
    /* The prefs callbacks reach the sound groups, rows over the audio layer. */
    static es2_sim_audio audio;
    es2_sim_audio_headless(&audio, &lc_loop, NULL);
    es2_sim_audio_bind(&audio);
    ic_fs0 = 0;
    if (su_files_reset()) ic_bad = 1;
    if (ic_prefs_length) ic_write_file(ic_prefs_path, ic_prefs, ic_prefs_length);
    else DeleteFileA(ic_prefs_path);
}
/* A prefs file, a keyjoy text and the client size. */
static void ic_random_files(void) {
    unsigned kind = ic_rand(6);
    ic_prefs_length = kind == 0 ? 0 : kind == 1 ? 1u + ic_rand(53) : 54u;
    for (unsigned i = 0; i < 54u; ++i) ic_prefs[i] = (BYTE)(ic_rand(3) ? ic_rand(3) : ic_rand(256));
    unsigned joy = ic_rand(4);
    ic_prefs[0xc] = (BYTE)(joy < 3u ? joy : ic_rand(256));
    static const char *const values[] = {"Reverse", "Default", "reverse", "", "Normal"};
    ic_ini_size = (size_t)snprintf(ic_ini, sizeof(ic_ini), "[Keyjoy]\r\nTilt = %s\r\nBacktrun = x\r\nBackturn = %s\r\nMissile = %s\r\n%sRudder = %s\r\n",
                                   values[ic_rand(5)], values[ic_rand(5)], values[ic_rand(5)], ic_rand(2) ? ";;\r\n" : "",
                                   values[ic_rand(5)]);
    if (ic_rand(8) == 0) ic_ini_size = 0;
    ic_client[0] = ic_rand(4) ? 320 : (int32_t)ic_rand(1000);
    ic_client[1] = ic_rand(4) ? 240 : 1 + (int32_t)ic_rand(1000);
}
static unsigned ic_files_after[2];
static BYTE ic_file_after[2][64];
static void ic_take(unsigned side) {
    su_take(side);
    ic_read_file(ic_prefs_path, ic_file_after[side], sizeof(ic_file_after[side]), &ic_files_after[side]);
}
static int ic_compare(const char *what, unsigned k) {
    if (su_compare(what, k)) return 1;
    const es2_journal *a = &ic_journal[0], *b = &ic_journal[1];
    if (strcmp(ir_log[0], ir_log[1]) || su_calls[0] != su_calls[1] || su_calls_hash[0] != su_calls_hash[1]) {
        fprintf(stderr, "FAIL: input %s case %u: calls\n  %s\n  %s\n", what, k, ir_log[0], ir_log[1]);
        return 1;
    }
    if (a->fallbacks != b->fallbacks || memcmp(a->cursor, b->cursor, sizeof(a->cursor)) || memcmp(a->keys, b->keys, sizeof(a->keys))) {
        fprintf(stderr, "FAIL: input %s case %u: journal state (fallbacks %lu / %lu)\n", what, k, (unsigned long)a->fallbacks,
                (unsigned long)b->fallbacks);
        return 1;
    }
    /* prefs_store (0x459b94, a hook, the original on both sides) writes stack
     * residue when prefs.cfg was shorter than 54 bytes (config.h,
     * ES2_CONFIG_RESIDUE): the file is compared only after a full read. */
    if (ic_prefs_length == 54u && (ic_files_after[0] != ic_files_after[1] || memcmp(ic_file_after[0], ic_file_after[1], ic_files_after[0]))) {
        fprintf(stderr, "FAIL: input %s case %u: prefs.cfg after the case differs\n", what, k);
        return 1;
    }
    return 0;
}
static uint32_t ic_machine(uint32_t va, const uint32_t *a, unsigned n, uint32_t *edx) { return lc_machine(va, a, n, edx); }
static int ic_row(uint32_t va, const uint32_t *a, unsigned n, es2_sim_result *r) {
    uint32_t regs[3] = {0, 0, 0};
    return es2_sim_call(&ic_dispatch, va, a, n, regs, r);
}

/* ---- setup */
static int ic_setup_side(unsigned side, const ic_env *e) {
    ic_side(side);
    ic_apply_env(e);
    if (!side) {
        ic_machine(0x0045a5dcu, NULL, 0, NULL);
    } else {
        es2_sim_result r;
        if (es2_sim_input_setup(&ic_input, &r)) ic_bad = 1;
    }
    return ic_bad;
}
static unsigned ic_joystick_cases, ic_joystick_ok, ic_keys_read, ic_joystick_polls, ic_mouse_events, ic_mouse_queued;
static int ic_setup_case(unsigned k) {
    static char text[1 << 16];
    ic_env e;
    ic_random_env(&e);
    ic_random_files();
    size_t n = ic_joystick_journal(text, sizeof(text), e.frame, 1);
    if (ic_load_journals(text, n)) return 1;
    for (unsigned side = 0; side < 2u; ++side) {
        if (ic_setup_side(side, &e)) { fprintf(stderr, "FAIL: input setup case %u: a call failed\n", k); return 1; }
        ic_take(side);
    }
    if (ic_prefs_length >= 13u && ic_prefs[0xc] < 2u) {
        ++ic_joystick_cases;
        if (get32(mapped + 0xd24a4u)) ++ic_joystick_ok;
    }
    if (ic_compare("setup", k)) return 1;
    if (k == 0) printf("SETUP case 0: calls %s, heap 0x%lx bytes; identical\n", ir_log[0], (unsigned long)su_used[0]);
    return 0;
}

/* ---- poll */
#define IC_CALLS 8u
static int ic_poll_case(unsigned k) {
    static char text[1 << 17];
    ic_env e;
    ic_random_env(&e);
    ic_random_files();
    ic_prefs_length = 54u;
    ic_prefs[0xc] = (BYTE)ic_rand(3);
    unsigned polls = 1u + ic_rand(6);
    size_t n = ic_joystick_journal(text, sizeof(text), e.frame, polls + 1u);
    if (ic_load_journals(text, n)) return 1;
    /* The per-poll changes: ring words written at the write index with the
     * device's bit 1, the mouse block, the frame, the readers' arguments. */
    uint32_t adds[8], words[8][4], mouse[8][3], frames[8], extra[8][4];
    for (unsigned p = 0; p < polls; ++p) {
        adds[p] = ic_rand(5);
        for (unsigned i = 0; i < 4u; ++i) words[p][i] = ic_rand(0x10000);
        mouse[p][0] = random32(); mouse[p][1] = random32(); mouse[p][2] = ic_rand(8);
        frames[p] = e.frame + (p ? ic_rand(2) : 0);
        for (unsigned i = 0; i < 4u; ++i) extra[p][i] = random32();
    }
    static uint32_t eax[2][8][IC_CALLS], edx[2][8][IC_CALLS];
    memset(eax, 0, sizeof(eax));
    memset(edx, 0, sizeof(edx));
    for (unsigned side = 0; side < 2u; ++side) {
        if (ic_setup_side(side, &e)) { fprintf(stderr, "FAIL: input poll case %u: set-up failed\n", k); return 1; }
        uint32_t keyboard = get32(mapped + 0xd246cu) ? get32(mapped + 0xd246cu) - 4u : 0;
        for (unsigned p = 0; p < polls; ++p) {
            put32(mapped + 0xd257bu, frames[p]);
            for (unsigned i = 0; i < adds[p]; ++i) {
                uint32_t w = get32(mapped + 0x2bb8b0u);
                put16(mapped + 0x2bb6b0u + 2u * (w & 0xffu), (uint16_t)words[p][i & 3u]);
                put32(mapped + 0x2bb8b0u, (w + 1u) & 0xffu);
                if (keyboard) put32((BYTE *)(uintptr_t)keyboard, get32((BYTE *)(uintptr_t)keyboard) | 2u);
            }
            put32(mapped + 0x2c5f50u, mouse[p][0] & 0x3ffu);
            put32(mapped + 0x2c5f54u, mouse[p][1] & 0x3ffu);
            put16(mapped + 0x2c5f58u, (uint16_t)mouse[p][2]);
            es2_sim_result r;
            unsigned c = 0;
            if (!side) eax[0][p][c] = ic_machine(0x0045baa8u, NULL, 0, NULL);
            else {
                if (es2_sim_device_poll(&ic_input, &r)) ic_bad = 1;
                eax[1][p][c] = r.eax;
                if (get16(mapped + 0xd23f6u)) ++ic_keys_read;
                if (get32(mapped + 0xd24a4u)) ++ic_joystick_polls;
            }
            ++c;
            /* The key reader (EDX the keyboard object for id 1, else the caller's). */
            uint32_t id = extra[p][0] % 4u, a[2] = {0x004d23f6u + (extra[p][1] % 8u), id};
            if (!side) eax[0][p][c] = ic_machine(0x0045bbc4u, a, 2, &edx[0][p][c]);
            else { r.edx = 0; if (es2_sim_key_reader(&ic_input, a[0], a[1], &r)) ic_bad = 1; eax[1][p][c] = r.eax; edx[1][p][c] = r.edx; }
            if (id != 1u) edx[0][p][c] = edx[1][p][c] = 0;
            ++c;
            uint32_t b[1] = {extra[p][2] % 5u};
            if (!side) eax[0][p][c] = ic_machine(0x0045c524u, b, 1, NULL);
            else { if (ic_row(0x0045c524u, b, 1, &r)) ic_bad = 1; eax[1][p][c] = r.eax; }
            ++c;
            if (!side) eax[0][p][c] = ic_machine(0x00477904u, NULL, 0, &edx[0][p][c]);
            else { if (es2_sim_joystick_summary(&ic_input, &r)) ic_bad = 1; eax[1][p][c] = r.eax; edx[1][p][c] = r.edx; }
            ++c;
            /* The prefs setter with a random callback table entry. */
            static const uint32_t callbacks[6] = {0, 0x00459cb4u, 0x00459c88u, 0x00459ce0u, 0x00459d3cu, 0x00459d68u};
            uint32_t index = extra[p][3] % 0x36u;
            put32(mapped + 0xd1fdcu + 4u * index, callbacks[(extra[p][3] >> 8) % 6u]);
            mapped[0xd20b4u] = (BYTE)((extra[p][3] >> 12) & 1u);
            uint32_t s[3] = {index, (extra[p][3] >> 16) & 0x1ffu, (extra[p][3] >> 25) & 3u};
            if (!side) eax[0][p][c] = ic_machine(0x00459958u, s, 3, NULL);
            else { if (es2_sim_prefs_set(&ic_input, s[0], s[1], s[2], &r)) ic_bad = 1; eax[1][p][c] = r.eax; }
            ++c;
            /* The key flush, the surface attribute, the bounds, an array. */
            if (keyboard && extra[p][1] % 4u == 0) {
                uint32_t f[1] = {keyboard};
                if (!side) eax[0][p][c] = ic_machine(0x00477abcu, f, 1, &edx[0][p][c]);
                else { if (ic_row(0x00477abcu, f, 1, &r)) ic_bad = 1; eax[1][p][c] = r.eax; edx[1][p][c] = r.edx; }
            }
            ++c;
            uint32_t g[2] = {(uint32_t)(uintptr_t)ic_surface, extra[p][2] % 20u};
            if (!side) eax[0][p][c] = ic_machine(0x00481350u, g, 2, &edx[0][p][c]);
            else { if (ic_row(0x00481350u, g, 2, &r)) ic_bad = 1; eax[1][p][c] = r.eax; edx[1][p][c] = r.edx; }
            ++c;
            put32(mapped + 0x2c5ed0u, (uint32_t)(uintptr_t)ic_surface);
            if (!side) ic_machine(0x0045b758u, NULL, 0, NULL);
            else if (es2_sim_mouse_bounds(&ic_input)) ic_bad = 1;
            uint32_t v[5] = {extra[p][0] % 3u ? 0u : (uint32_t)(uintptr_t)ic_scratch, 1u + extra[p][1] % 20u, extra[p][2] % 8u,
                             (extra[p][3] & 1u) ? 0x11u : 1u, (extra[p][3] & 2u) ? 0x004531b5u : 0u};
            if (!side) eax[0][p][c] = ic_machine(0x0049667cu, v, 5, NULL);
            else { if (ic_row(0x0049667cu, v, 5, &r)) ic_bad = 1; eax[1][p][c] = r.eax; }
        }
        if (ic_bad) { fprintf(stderr, "FAIL: input poll case %u: a call failed\n", k); return 1; }
        /* 0x45b758 copies three words of its own uninitialised local into
         * M+0x1e..+0x23 (residue; the own code's model is 0): masked. */
        if (get32(mapped + 0xd2470u)) memset((BYTE *)(uintptr_t)(get32(mapped + 0xd2470u) - 0x3au + 0x1eu), 0, 6);
        ic_take(side);
    }
    for (unsigned p = 0; p < polls; ++p)
        for (unsigned c = 0; c < IC_CALLS; ++c)
            if (eax[0][p][c] != eax[1][p][c] || edx[0][p][c] != edx[1][p][c]) {
                fprintf(stderr, "FAIL: input poll case %u poll %u call %u: EAX %08lx / %08lx EDX %08lx / %08lx\n", k, p, c,
                        (unsigned long)eax[0][p][c], (unsigned long)eax[1][p][c], (unsigned long)edx[0][p][c], (unsigned long)edx[1][p][c]);
                return 1;
            }
    return ic_compare("poll", k);
}

/* ---- mouse */
static int ic_mouse_case(unsigned k) {
    ic_env e;
    ic_random_env(&e);
    ic_random_files();
    if (ic_load_journals("", 0)) return 1;
    unsigned count = 1u + ic_rand(12);
    static const uint32_t messages[] = {0x200u, 0x201u, 0x202u, 0x204u, 0x205u, 0x0005u, 0x203u, 0x100u, 0x206u};
    uint32_t m[16], w[16], l[16], live[16];
    uint32_t scale_x = ic_rand(3) == 0 ? ic_rand(0x40000) : 0, scale_y = scale_x ? ic_rand(0x40000) : 0;
    for (unsigned i = 0; i < count; ++i) {
        m[i] = messages[ic_rand(9)]; w[i] = ic_rand(4) ? ic_rand(4) : random32();
        l[i] = ic_rand(4) ? (ic_rand(640) | ic_rand(480) << 16) : random32();
        live[i] = ic_rand(4) != 0;
    }
    if (ic_client[1] == 0) ic_client[1] = 1;
    uint32_t results[2][16], setup_eax[2], setup_edx[2];
    for (unsigned side = 0; side < 2u; ++side) {
        if (ic_setup_side(side, &e)) { fprintf(stderr, "FAIL: input mouse case %u: set-up failed\n", k); return 1; }
        if (!side) setup_eax[0] = ic_machine(0x00452ae4u, NULL, 0, &setup_edx[0]);
        else { es2_sim_result r; if (es2_sim_mouse_queue_setup(&ic_input, &r)) ic_bad = 1; setup_eax[1] = r.eax; setup_edx[1] = r.edx; }
        put32(mapped + 0xa2dccu, scale_x);
        put32(mapped + 0xa2dd0u, scale_y);
        mapped[0x9dbdfu] = 0;
        for (unsigned i = 0; i < count; ++i) {
            mapped[0xd1dd6u] = (BYTE)live[i];
            if (!side) {
                es2_hook_result r;
                uint32_t a[4] = {0x00010001u, m[i], w[i], l[i]};
                es2_hook_call c = {mapped + 0x809f8u, ES2_ABI_STDCALL, 4, a, initial_fpu, &r, NULL};
                es2_hook_machine_call(&c);
                results[0][i] = r.eax;
            } else if (es2_sim_mouse_handler(&ic_input, m[i], w[i], l[i], &results[1][i])) {
                ic_bad = 1;
            }
        }
        if (ic_bad) { fprintf(stderr, "FAIL: input mouse case %u: a call failed\n", k); return 1; }
        if (side) {
            ic_mouse_events += count;
            uint32_t w = get32(mapped + 0xd1e04u);
            if (w) ic_mouse_queued += get32((BYTE *)(uintptr_t)w);
        }
        ic_take(side);
    }
    if (setup_eax[0] != setup_eax[1] || setup_edx[0] != setup_edx[1] || memcmp(results[0], results[1], 4u * count)) {
        fprintf(stderr, "FAIL: input mouse case %u: results (queue set-up EAX %08lx / %08lx)\n", k, (unsigned long)setup_eax[0],
                (unsigned long)setup_eax[1]);
        return 1;
    }
    return ic_compare("mouse", k);
}

/* ---- streams: the read stream (0x47ad40, 0x47af98, 0x47ad78) or the write
 * stream (0x47b064, 0x47b2b8, 0x47b09c) on a random file */
static int ic_streams_case(unsigned k) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%sstream.bin", ic_root);
    BYTE bytes[96], source[64];
    unsigned length = ic_rand(4) == 0 ? 0xffffffffu : ic_rand(96);
    for (unsigned i = 0; i < sizeof(bytes); ++i) bytes[i] = (BYTE)ic_rand(256);
    for (unsigned i = 0; i < sizeof(source); ++i) source[i] = (BYTE)ic_rand(256);
    uint32_t sizes[3] = {ic_rand(60), ic_rand(4) ? ic_rand(60) : 0, ic_rand(60)};
    unsigned count = 1u + ic_rand(3);
    uint32_t flags = ic_rand(2) ? 2u : 3u;
    int write = ic_rand(2) != 0;
    const uint32_t open = write ? 0x0047b064u : 0x0047ad40u, transfer = write ? 0x0047b2b8u : 0x0047af98u,
                   destroy = write ? 0x0047b09cu : 0x0047ad78u;
    uint32_t eax[2][6], edx[2][6];
    BYTE object[2][0x80], file[2][256];
    unsigned file_size[2];
    memset(eax, 0, sizeof(eax)); memset(edx, 0, sizeof(edx)); memset(object, 0, sizeof(object)); memset(file, 0, sizeof(file));
    ic_prefs_length = 0;
    for (unsigned side = 0; side < 2u; ++side) {
        ic_side(side);
        if (length == 0xffffffffu) DeleteFileA(path); else ic_write_file(path, bytes, length);
        memcpy(mapped + 0xd27bcu, path, strlen(path) + 1);   /* the path, guest-addressable */
        uint32_t p = (uint32_t)(uintptr_t)(mapped + 0xd27bcu), stream = (uint32_t)(uintptr_t)ic_scratch + 0x200u;
        uint32_t buf = (uint32_t)(uintptr_t)ic_scratch + 0x240u;
        memset(ic_scratch + 0x200u, 0xa5, 0x100u);
        if (write) memcpy(ic_scratch + 0x240u, source, sizeof(source));
        es2_sim_result r;
        if (flags == 3u) {
            uint32_t a[1] = {0xbu};
            if (!side) stream = ic_machine(0x0047749cu, a, 1, NULL);
            else { if (ic_row(0x0047749cu, a, 1, &r)) ic_bad = 1; stream = r.eax; }
        }
        uint32_t a[2] = {stream, p};
        if (!side) eax[0][0] = ic_machine(open, a, 2, &edx[0][0]);
        else { if (ic_row(open, a, 2, &r)) ic_bad = 1; eax[1][0] = r.eax; edx[1][0] = r.edx; }
        for (unsigned i = 0; i < count; ++i) {
            uint32_t b[3] = {stream, sizes[i], buf};
            if (!side) eax[0][1 + i] = ic_machine(transfer, b, 3, NULL) & 0xffu;
            else { if (ic_row(transfer, b, 3, &r)) ic_bad = 1; eax[1][1 + i] = r.eax & 0xffu; }
        }
        memcpy(object[side], (BYTE *)(uintptr_t)stream, 0xbu);
        memcpy(object[side] + 0x10u, ic_scratch + 0x240u, 0x60u);
        uint32_t c[2] = {stream, flags};
        if (!side) ic_machine(destroy, c, 2, NULL);
        else if (ic_row(destroy, c, 2, &r)) ic_bad = 1;
        if (flags == 2u) memcpy(object[side] + 0x70u, (BYTE *)(uintptr_t)stream, 0xbu);
        if (ic_bad) { fprintf(stderr, "FAIL: input streams case %u: a call failed\n", k); return 1; }
        su_take(side);
        ic_files_after[side] = 0;
        ic_read_file(path, file[side], sizeof(file[side]), &file_size[side]);
    }
    DeleteFileA(path);
    if (memcmp(eax[0], eax[1], sizeof(eax[0])) || memcmp(edx[0], edx[1], sizeof(edx[0]))) {
        fprintf(stderr, "FAIL: input streams case %u: registers (%s, length %u)\n", k, write ? "write" : "read", length);
        return 1;
    }
    if (memcmp(object[0], object[1], sizeof(object[0]))) {
        for (unsigned i = 0; i < sizeof(object[0]); ++i)
            if (object[0][i] != object[1][i]) fprintf(stderr, "  streams %u: byte 0x%02x original %02x own %02x\n", k, i, object[0][i], object[1][i]);
        fprintf(stderr, "FAIL: input streams case %u: the stream object or buffer (%s, length %u, flags %lu, calls %u)\n", k,
                write ? "write" : "read", length, (unsigned long)flags, count);
        return 1;
    }
    if (file_size[0] != file_size[1] || memcmp(file[0], file[1], file_size[0])) {
        fprintf(stderr, "FAIL: input streams case %u: the file after the case (%u / %u bytes)\n", k, file_size[0], file_size[1]);
        return 1;
    }
    return ic_compare("streams", k);
}
static int input_layer_cases(void) {
    current_function = "input-layer";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_files_region = VirtualAlloc(NULL, ES2_SIM_FILES_REGION_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    ic_surface = VirtualAlloc(NULL, 0x1000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    ic_scratch = VirtualAlloc(NULL, 0x1000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !su_files_region || !ir_region || !lc_stack || !lc_bits ||
        !ic_surface || !ic_scratch)
        return fail("input: memory");
    for (unsigned i = 0; i < 0x40u; ++i) ic_surface[i] = (BYTE)random32();
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("input: the counter word is not free");
    su_pristine_take();
    static const struct { const char *dll, *name; void *stub; } stubs[] = {
        {"cw3220.dll", "_calloc", (void *)su_calloc}, {"cw3220.dll", "_free", (void *)su_free}, {"cw3220.dll", "_atexit", (void *)lc_atexit},
        {"cw3220.dll", "_memset", (void *)ic_memset}, {"cw3220.dll", "_memcpy", (void *)ic_memcpy}, {"cw3220.dll", "_memcmp", (void *)ic_memcmp},
        {"cw3220.dll", "_strcat", (void *)ic_strcat}, {"cw3220.dll", "_strncpy", (void *)ic_strncpy}, {"cw3220.dll", "_stricmp", (void *)su_stricmp},
        {"cw3220.dll", "_fopen", (void *)su_fopen}, {"cw3220.dll", "_fclose", (void *)ic_fclose}, {"cw3220.dll", "_open", (void *)su_open},
        {"cw3220.dll", "_close", (void *)su_close}, {"cw3220.dll", "_read", (void *)su_read}, {"cw3220.dll", "_lseek", (void *)su_lseek},
        {"cw3220.dll", "_write", (void *)ic_write}, {"KERNEL32.dll", "GetTickCount", (void *)lc_tick},
        {"KERNEL32.dll", "GetPrivateProfileStringA", (void *)ic_profile}, {"USER32.dll", "GetKeyState", (void *)ic_key_state},
        {"USER32.dll", "GetClientRect", (void *)ic_client_rect}, {"WINMM.dll", "joyGetDevCapsA", (void *)ic_joy_caps},
        {"WINMM.dll", "joyGetPosEx", (void *)ic_joy_pos},
    };
    for (unsigned i = 0; i < sizeof(stubs) / sizeof(stubs[0]); ++i)
        if (!ir_patch_import(stubs[i].dll, stubs[i].name, stubs[i].stub)) {
            fprintf(stderr, "import %s!%s\n", stubs[i].dll, stubs[i].name);
            return fail("input: import slot not found");
        }
    if (ic_dispatch_setup()) return 1;
    es2_journal_init(&ic_journal[0]);
    es2_journal_init(&ic_journal[1]);
    /* The installation: a temporary directory with data\. */
    char temp[MAX_PATH];
    GetTempPathA(MAX_PATH, temp);
    snprintf(ic_root, sizeof(ic_root), "%ses2-input-%lu\\", temp, (unsigned long)GetCurrentProcessId());
    CreateDirectoryA(ic_root, NULL);
    snprintf(temp, sizeof(temp), "%sdata", ic_root);
    CreateDirectoryA(temp, NULL);
    snprintf(ic_prefs_path, sizeof(ic_prefs_path), "%sdata\\prefs.cfg", ic_root);
    if (strlen(ic_root) > 120u) return fail("input: temporary path too long for the guest's root buffer");
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"setup", ic_setup_case, 2000}, {"poll", ic_poll_case, 2000}, {"mouse", ic_mouse_case, 2000}, {"streams", ic_streams_case, 1000},
    };
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        for (unsigned k = 0; !bad && k < parts[p].cases; ++k) {
            current_case = k; current_case_valid = 1;
            bad = parts[p].run(k);
        }
        if (!bad) printf("PASS: input layer %s: %u cases identical\n", parts[p].name, parts[p].cases);
    }
    if (!bad) printf("INPUT coverage: %u set-up cases with a joystick byte 0 or 1, %u of them with the device active; %u polls with a key, "
                     "%u with the joystick; %u mouse messages, %u button events queued\n", ic_joystick_cases, ic_joystick_ok, ic_keys_read,
                     ic_joystick_polls, ic_mouse_events, ic_mouse_queued);
    current_case_valid = 0;
    DeleteFileA(ic_prefs_path);
    snprintf(temp, sizeof(temp), "%sdata", ic_root);
    RemoveDirectoryA(temp);
    RemoveDirectoryA(ic_root);
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
