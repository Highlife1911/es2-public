/* DBSIM's start-up pieces, original machine code against sim/src/startup.c and
 * sim/src/volumes.c (--startup; target :differential_startup_test). Included
 * after init_records_cases.h, whose emulated heap region (ir_region, ir_heap,
 * ir_heap_reset), identity view (ir_memory), call log (ir_note, ir_log) and
 * import patcher it reuses.
 *
 * - parsers: random command lines (up to 26 arguments of every option the
 *   two parsers know, numbers, separators) through the split 0x4652f8, the
 *   early parser 0x45e6cc and the main parser 0x45e758 on both sides; the
 *   tape options (-r, -p, -D, -d) are excluded (file I/O and the demo pick).
 *   prefs_video 0x45e510 (-v) runs as the original on both sides (the own
 *   side's dispatch passes the call through), _fopen answers NULL.
 * - strings: random STR-like buffers in a holder, 0x4539a0 / 0x453918 twice
 *   (two groups) against es2_sim_startup_read_group.
 * - volumes: the VOL registration 0x473260 on the CD's VOL\ (masks 1,
 *   0x100, 0x101; the CD root as the second root, so pass 2 meets every name
 *   again) and on synthetic VOL files in a temporary directory (priorities
 *   with ties and the carry, a foreign mask, a wrong signature, tables larger
 *   than the 0x400-byte window, no directories, a second root with a new
 *   and a duplicate name, both pass orders).
 *
 * Compared after each case: DATA 0x497000..0x6cc000 (the construction
 * counter lives there, at a word this file points [0x6cbee4] at), the used
 * part of the emulated heap region, argc and the argv array (parsers), the
 * output slots (strings), CW3220's file tables except the OS handles
 * (volumes), and the import-level call logs. Every import the path reaches
 * points at a stub over the same models the own side's services use: the
 * heap model, the rtl_cases.h string models, the rtl_io_cases.h file model. */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "es2/sim/startup.h"
#include "es2/sim/files.h"
#include <ctype.h>

#define SU_DATA_RVA 0x97000u
#define SU_DATA_SIZE 0x235000u
#define SU_COUNTER 0x004d1000u          /* an unused BSS dword of DBSIM (checked zero) for the construction counter */
#define SU_FRAME_BYTES 0x4000u

static BYTE *su_data_pristine, *su_snap[2], *su_frame, *su_files_region;
static es2_sim_files su_files;
static es2_sim_entry su_entries[4096];
static es2_sim_dispatch su_dispatch;
static es2_sim_services su_services;
static int su_bad;
static uint32_t su_fopen_result;

/* ---- the call logs: ir_log keeps the first 63 letters of a pass, su_calls
 * counts every call and hashes the whole sequence. */
static uint64_t su_calls_hash[2];
static unsigned su_calls[2];
static void su_mix(char letter) {
    ++su_calls[ir_side];
    su_calls_hash[ir_side] = (su_calls_hash[ir_side] ^ (uint8_t)letter) * 0x100000001b3ull;
}
static void su_note(char letter) { ir_note(letter); su_mix(letter); }
/* The original's _calloc and _free: init_records_cases.h's stubs (they log
 * C and F themselves). */
static void *__cdecl su_calloc(size_t count, size_t n) { su_mix('C'); return ir_calloc(count, n); }
static void __cdecl su_free(void *p) { su_mix('F'); ir_free(p); }

/* ---- import stubs of the original side */
static es2_rtl_memory su_rtl;
static uint32_t su_rtl_call(es2_rtl_function f, const uint32_t *first) {
    es2_rtl_regs r = {0, 0, 0};
    if (es2_rtl_call(f, &su_rtl, (uint32_t)(uintptr_t)first, &r) != ES2_RTL_DONE) su_bad = 1;
    return r.eax;
}
/* The arguments as an array (the harness is built with -mstackrealign, so a
 * parameter's address is a copy, not the caller's slot). */
static uint32_t __cdecl su_atol(uint32_t p) { su_note('A'); uint32_t a[1] = {p}; return su_rtl_call(ES2_RTL_ATOL, a); }
static uint32_t __cdecl su_strchr(uint32_t p, uint32_t c) { uint32_t a[2] = {p, c}; return su_rtl_call(ES2_RTL_STRCHR, a); }
static uint32_t __cdecl su_strlen(uint32_t p) { uint32_t a[1] = {p}; return su_rtl_call(ES2_RTL_STRLEN, a); }
static uint32_t __cdecl su_strcat(uint32_t d, uint32_t s) { uint32_t a[2] = {d, s}; return su_rtl_call(ES2_RTL_STRCAT, a); }
static uint32_t __cdecl su_stricmp(uint32_t x, uint32_t y) { su_note('I'); uint32_t a[2] = {x, y}; return su_rtl_call(ES2_RTL_STRICMP, a); }
static uint32_t __cdecl su_strncmp(uint32_t x, uint32_t y, uint32_t n) { uint32_t a[3] = {x, y, n}; return su_rtl_call(ES2_RTL_STRNCMP, a); }
static uint32_t __cdecl su_memcpy(uint32_t d, uint32_t s, uint32_t n) { uint32_t a[3] = {d, s, n}; return su_rtl_call(ES2_RTL_MEMCPY, a); }
static uint32_t __cdecl su_fopen(uint32_t p, uint32_t m) { (void)p; (void)m; su_note('f'); return su_fopen_result; }
static uint32_t su_io(es2_io_function f, const uint32_t *a, unsigned n, char letter) {
    su_note(letter);
    uint32_t eax = 0;
    if (es2_sim_files_call(&su_files, f, a, n, &eax)) su_bad = 1;
    return eax;
}
static uint32_t __cdecl su_open(uint32_t p, uint32_t f, uint32_t m) { uint32_t a[3] = {p, f, m}; return su_io(ES2_IO_OPEN, a, 3, 'O'); }
static uint32_t __cdecl su_close(uint32_t h) { uint32_t a[1] = {h}; return su_io(ES2_IO_CLOSE, a, 1, 'K'); }
static uint32_t __cdecl su_read(uint32_t h, uint32_t b, uint32_t n) { uint32_t a[3] = {h, b, n}; return su_io(ES2_IO_READ, a, 3, 'R'); }
static uint32_t __cdecl su_lseek(uint32_t h, uint32_t o, uint32_t w) { uint32_t a[3] = {h, o, w}; return su_io(ES2_IO_LSEEK, a, 3, 'L'); }
static uint32_t __cdecl su_findfirst(uint32_t p, uint32_t b, uint32_t at) { uint32_t a[3] = {p, b, at}; return su_io(ES2_IO_FINDFIRST, a, 3, 'Q'); }
static uint32_t __cdecl su_findnext(uint32_t b) { uint32_t a[1] = {b}; return su_io(ES2_IO_FINDNEXT, a, 1, 'N'); }

/* ---- own side: the sim services; prefs_video passes through to the original */
static void su_prefs_video(es2_sim_service_call *call, void *user) {
    (void)user;
    es2_hook_result r;
    uint32_t a[1] = {call->slots[0]};
    es2_hook_call c = {mapped + 0x5e510u, ES2_ABI_CDECL, 1, a, initial_fpu, &r, NULL};
    es2_hook_machine_call(&c);
    call->eax = r.eax; call->edx = r.edx;
}
/* Import-level log letters of the own side, as the original's stubs log
 * them: the game allocator reaches _calloc (C), delete reaches _free (F)
 * for a nonzero pointer. */
static struct { uint32_t va; char letter; es2_sim_service_fn real; } su_wrapped[] = {
    {0x0047749cu, 'C', NULL}, {0x004774f0u, 'C', NULL}, {0x0047747cu, 'C', NULL}, {0x00477548u, 'F', NULL},
    {0x0047758cu, 'F', NULL}, {0x00496880u, 'A', NULL}, {0x00496868u, 'I', NULL}, {0x00496802u, 'f', NULL},
    {0x004967c6u, 'O', NULL}, {0x00496874u, 'K', NULL}, {0x00496808u, 'R', NULL}, {0x00496826u, 'L', NULL},
    {0x0049685cu, 'Q', NULL}, {0x00496862u, 'N', NULL},
};
static void su_wrapper(es2_sim_service_call *call, void *user) {
    for (unsigned i = 0; i < sizeof(su_wrapped) / sizeof(su_wrapped[0]); ++i) {
        if (su_wrapped[i].va != call->va) continue;
        if (su_wrapped[i].letter != 'F' || call->slots[0]) su_note(su_wrapped[i].letter);
        if (su_wrapped[i].letter == 'f') { call->eax = su_fopen_result; return; }
        su_wrapped[i].real(call, user);
        return;
    }
}
static void su_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own start-up: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    su_bad = 1;
}
static int su_dispatch_setup(void) {
    es2_sim_dispatch_init(&su_dispatch, su_entries, 4096, su_refuse, NULL);
    su_services.memory = &ir_memory;
    su_services.heap = &ir_heap;
    su_services.vt = NULL;
    su_services.fs0 = NULL;
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_register_services(&su_dispatch, &su_services) ||
        es2_sim_dispatch_add_service(&su_dispatch, 0x0045e510u, "prefs_video (original)", su_prefs_video, NULL, 0, 1) ||
        es2_sim_dispatch_seal(&su_dispatch, &error, &bad))
        return fail("startup: dispatch");
    for (unsigned i = 0; i < sizeof(su_wrapped) / sizeof(su_wrapped[0]); ++i) {
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&su_dispatch, su_wrapped[i].va);
        if (!e || e->kind != ES2_SIM_SERVICE) return fail("startup: service missing");
        su_wrapped[i].real = e->service;
        e->service = su_wrapper;
    }
    return 0;
}

/* ---- passes
 *
 * DATA cannot be write-watched (the image range lies in the start thread's
 * stack reservation, startup_reservation.h), so su_reset and su_take find the
 * DATA pages that differ from the pristine image by comparing: a pristine page
 * that is all zero (most of DATA) is compared against zero, which reads DATA
 * alone. Only differing pages are restored or copied. su_pristine_take is the
 * only writer of su_data_pristine. */
#define SU_DATA_PAGES (SU_DATA_SIZE / WW_PAGE)
static unsigned char su_pristine_zero[SU_DATA_PAGES];   /* the pristine page is all zero */
static unsigned su_pristine_generation;
static void su_pristine_take(void) {
    memcpy(su_data_pristine, mapped + SU_DATA_RVA, SU_DATA_SIZE);
    for (uint32_t page = 0; page < SU_DATA_PAGES; ++page)
        su_pristine_zero[page] = !ww_page_differs(su_data_pristine + page * WW_PAGE, NULL);
    ++su_pristine_generation;
}
static int su_data_differs(uint32_t page) {
    return ww_page_differs(mapped + SU_DATA_RVA + page * WW_PAGE, su_pristine_zero[page] ? NULL : su_data_pristine + page * WW_PAGE);
}
static void su_reset(unsigned side) {
    for (uint32_t page = 0; page < SU_DATA_PAGES; ++page)
        if (su_data_differs(page)) memcpy(mapped + SU_DATA_RVA + page * WW_PAGE, su_data_pristine + page * WW_PAGE, WW_PAGE);
    if (ww_checking() && memcmp(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE)) {
        fprintf(stderr, "FAIL: startup reset: DATA differs from the pristine image\n");
        ExitProcess(1);
    }
    put32(mapped + 0x2cbee4u, SU_COUNTER);   /* [0x6cbee4] -> the counter word */
    ir_heap_reset();
    ir_side = side;
    ir_log_count[side] = 0;
    su_calls[side] = 0;
    su_calls_hash[side] = 0xcbf29ce484222325ull;
    memset(ir_log[side], 0, IR_LOG);
    su_fopen_result = 0;
}
static int su_files_reset(void) {
    /* CW3220's own blocks (find nodes, stream buffers) on the emulated heap,
     * as both sides' file calls reach the same model. */
    if (es2_sim_files_init(&su_files, &ir_memory, (uint32_t)(uintptr_t)su_files_region, &ir_heap)) return fail("startup: files");
    es2_sim_files_bind(&su_files);
    return 0;
}
static void su_own(es2_sim_startup *s) {
    memset(s, 0, sizeof(*s));
    es2_sim_startup_defaults(s);
    s->memory = &ir_memory;
    s->dispatch = &su_dispatch;
    s->frame_lo = (uint32_t)(uintptr_t)su_frame;
    s->frame_hi = (uint32_t)(uintptr_t)su_frame + SU_FRAME_BYTES;
}
static uint32_t su_machine(uint32_t va, const uint32_t *a, unsigned n) {
    es2_hook_result r;
    es2_hook_call c = {mapped + (va - 0x400000u), ES2_ABI_CDECL, n, a, initial_fpu, &r, NULL};
    es2_hook_machine_call(&c);
    return r.eax;
}
/* The state of a pass: DATA and the used heap. su_snap[side] is the full
 * image the compare reads: DATA, then the region's first su_used[side] bytes.
 * su_snap_set[side] marks the snapshot pages that may differ from their
 * base, the pristine DATA page or a zero heap page; every unmarked page
 * equals its base. su_take copies only the DATA pages that differ from the
 * pristine image and the heap pages written since the clear (ww_written),
 * and puts the base back into the pages it unmarks; su_compare reads only
 * the pages marked on either side. Without write tracking the used heap is
 * copied and marked in full. Code that writes into a snapshot uses
 * su_snap_same, which marks the page. */
#define SU_SNAP_PAGES (SU_DATA_PAGES + IR_PAGES)
static uint32_t su_heap_used(void) { return ir_cursor; }
static uint32_t su_used[2];
static uint32_t su_snap_set[2][(SU_SNAP_PAGES + 31) / 32];
static BYTE *su_snap_owner[2];               /* the buffer su_snap_set[side] describes */
static unsigned su_snap_generation[2];       /* the pristine image its unmarked DATA pages equal */
static int su_marked(unsigned side, uint32_t page) { return su_snap_set[side][page / 32] >> (page % 32) & 1u; }
static void su_mark(unsigned side, uint32_t page, int on) {
    if (on) su_snap_set[side][page / 32] |= 1u << (page % 32);
    else su_snap_set[side][page / 32] &= ~(1u << (page % 32));
}
static void su_take(unsigned side) {
    BYTE *snap = su_snap[side], *heap = snap + SU_DATA_SIZE;
    if (su_snap_owner[side] != snap) {
        memset(heap, 0, IR_REGION_SIZE);
        memset(su_snap_set[side], 0, sizeof(su_snap_set[side]));
        su_snap_owner[side] = snap;
        su_snap_generation[side] = su_pristine_generation - 1u;
    }
    if (su_snap_generation[side] != su_pristine_generation) {
        memcpy(snap, su_data_pristine, SU_DATA_SIZE);
        for (uint32_t page = 0; page < SU_DATA_PAGES; ++page) su_mark(side, page, 0);
        su_snap_generation[side] = su_pristine_generation;
    }
    for (uint32_t page = 0; page < SU_DATA_PAGES; ++page) {
        int differs = su_data_differs(page);
        if (differs || su_marked(side, page)) memcpy(snap + page * WW_PAGE, mapped + SU_DATA_RVA + page * WW_PAGE, WW_PAGE);
        su_mark(side, page, differs);
    }
    su_used[side] = ir_cursor;
    long count = ww_written(ir_region, IR_REGION_SIZE);
    if (count < 0) {
        memcpy(heap, ir_region, ir_cursor);
        for (uint32_t page = 0; page < ir_cursor / WW_PAGE; ++page) su_mark(side, SU_DATA_PAGES + page, 1);
    } else {
        uint32_t written[IR_PAGES / 32] = {0}, used = ir_cursor / WW_PAGE;
        for (long i = 0; i < count; ++i) {
            uint32_t page = (uint32_t)(((BYTE *)ww_dirty[i] - ir_region) / WW_PAGE);
            if (page < used) written[page / 32] |= 1u << (page % 32);
        }
        for (uint32_t page = 0; page < IR_PAGES; ++page) {
            int now = written[page / 32] >> (page % 32) & 1u;
            if (now) memcpy(heap + page * WW_PAGE, ir_region + page * WW_PAGE, WW_PAGE);
            else if (su_marked(side, SU_DATA_PAGES + page)) memset(heap + page * WW_PAGE, 0, WW_PAGE);
            su_mark(side, SU_DATA_PAGES + page, now);
        }
    }
    if (ww_checking() && (memcmp(snap, mapped + SU_DATA_RVA, SU_DATA_SIZE) || memcmp(heap, ir_region, ir_cursor))) {
        fprintf(stderr, "FAIL: startup snapshot %u differs from DATA and the heap\n", side);
        ExitProcess(1);
    }
}
/* Makes the own snapshot hold the original's n bytes at DATA address va (a
 * host address the two passes store differently). */
static void su_snap_same(uint32_t va, uint32_t n) {
    uint32_t at = va - 0x00497000u;
    memcpy(su_snap[1] + at, su_snap[0] + at, n);
    for (uint32_t page = at / WW_PAGE; page <= (at + n - 1u) / WW_PAGE; ++page) su_mark(1, page, 1);
}
/* The live find nodes (0x14 bytes, CW3220's own heap blocks) in
 * one side's snapshot: +0 is the caller's ffblk, which the two sides keep in
 * different frames (the machine stack, su_frame), +4 the OS find handle;
 * both are set to zero, as the handles of the FILE tables are left out. */
static void su_snap_find_nodes(unsigned side) {
    uint32_t base = (uint32_t)(uintptr_t)ir_region;
    for (uint32_t node = get32((const BYTE *)(uintptr_t)su_files.layout.find_list), n = 0; node && n < 64u;
         node = get32((const BYTE *)(uintptr_t)(node + 0x10u)), ++n) {
        if (node - base >= IR_REGION_SIZE - 8u) break;
        uint32_t at = SU_DATA_SIZE + (node - base);
        memset(su_snap[side] + at, 0, 8u);
        su_mark(side, at / WW_PAGE, 1);
    }
}
static int su_compare(const char *what, unsigned index) {
    if (su_used[0] != su_used[1]) {
        fprintf(stderr, "FAIL: startup %s case %u: heap reservations 0x%lx / 0x%lx\n", what, index, (unsigned long)su_used[0],
                (unsigned long)su_used[1]);
        return 1;
    }
    size_t total = SU_DATA_SIZE + su_used[0];   /* whole pages: ir_cursor grows in 64 KiB steps */
    int same_base = su_snap_generation[0] == su_snap_generation[1], differ = 0;
    for (uint32_t page = 0; !differ && page < total / WW_PAGE; ++page)
        if (su_marked(0, page) || su_marked(1, page) || (!same_base && page < SU_DATA_PAGES))
            differ = memcmp(su_snap[0] + page * WW_PAGE, su_snap[1] + page * WW_PAGE, WW_PAGE) != 0;
    if (ww_checking() && differ != (memcmp(su_snap[0], su_snap[1], total) != 0)) {
        fprintf(stderr, "FAIL: startup %s case %u: the page compare disagrees with the full compare\n", what, index);
        ExitProcess(1);
    }
    if (!differ) return 0;
    unsigned shown = 0, count = 0;
    for (size_t i = 0; i < total; ++i) {
        if (su_snap[0][i] == su_snap[1][i]) continue;
        ++count;
        if (shown++ < 24) {
            uint32_t a = i < SU_DATA_SIZE ? 0x00497000u + (uint32_t)i : (uint32_t)(uintptr_t)ir_region + (uint32_t)(i - SU_DATA_SIZE);
            fprintf(stderr, "  %s %u: 0x%08lx original %02x own %02x\n", what, index, (unsigned long)a, su_snap[0][i], su_snap[1][i]);
        }
    }
    fprintf(stderr, "FAIL: startup %s case %u: %u bytes differ\n", what, index, count);
    return 1;
}

/* ---- parsers */
static uint32_t su_random(void) { return random32(); }
static void su_number(char *out) {
    static const char *const forms[] = {"%d", "%d", "%u", "+%d", "-%d", "%dx", "0%d", ""};
    unsigned f = su_random() % 8u;
    int v = (int)(su_random() % 1000u);
    if (su_random() % 8u == 0) v = (int)su_random();
    sprintf(out, forms[f], f == 2 ? (unsigned)su_random() : (unsigned)v);
}
static void su_token(char *t) {
    char n[24];
    su_number(n);
    switch (su_random() % 40u) {
    case 0: sprintf(t, "-X%s", n); break;
    case 1: strcpy(t, (su_random() & 1) ? "-Z1" : "-Z0"); break;
    case 2: strcpy(t, "-Zq"); break;
    case 3: strcpy(t, "-c"); break;
    case 4: sprintf(t, "-v%c", (char)('0' + su_random() % 5u)); break;
    case 5: sprintf(t, "-R%s", n); break;
    case 6: strcpy(t, "-b"); break;
    case 7: strcpy(t, "-B"); break;
    case 8: strcpy(t, "-SPRUNKNOWN"); break;
    case 9: strcpy(t, (su_random() & 1) ? "-Sprunknown" : "-SPRUNKNOWNX"); break;
    case 10: strcpy(t, "-m"); break;
    case 11: {
        /* -C with one of the 13 names of the image's table, its case changed, or another name. */
        uint32_t i = su_random() % 14u;
        if (i < 13u) {
            const char *name = (const char *)(uintptr_t)get32(mapped + 0x9ac64u + 4u * i);
            size_t k = 0;
            t[k++] = '-'; t[k++] = 'C';
            for (; name[k - 2] && k < 40; ++k) t[k] = (char)((su_random() & 1) ? tolower((unsigned char)name[k - 2]) : name[k - 2]);
            t[k] = 0;
        } else strcpy(t, "-Cnone");
        break;
    }
    case 12: strcpy(t, "-s"); break;
    case 13: strcpy(t, "-P"); break;
    case 14: sprintf(t, "-t%s", n); break;
    case 15: sprintf(t, "-V%s", n); break;
    case 16: sprintf(t, "-W%s", n); break;
    case 17: sprintf(t, "-T%s", n); break;
    case 18: strcpy(t, "-l"); break;
    case 19: strcpy(t, "-eggplant"); break;
    case 20: strcpy(t, "-EGGPLANT"); break;
    case 21: strcpy(t, (su_random() & 1) ? "-eggplantx" : "-egg"); break;
    case 22: strcpy(t, "-E"); break;
    case 23: strcpy(t, "-F"); break;
    case 24: strcpy(t, "-G"); break;
    case 25: strcpy(t, "-a"); break;
    case 26: strcpy(t, "-"); break;
    case 27: strcpy(t, "dummy"); break;
    case 28: sprintf(t, "x-%s", n); break;
    case 29: {
        /* Any letter of the jump table's range and around it, except the tape options. */
        static const char letters[] = "@ABCEFGHIJKLMNOPQRSTUVWXYZ[_`abcefghijklmnoqstuvwxyz{";
        sprintf(t, "-%c%s", letters[su_random() % (sizeof(letters) - 1u)], n);
        break;
    }
    case 30: sprintf(t, "--%s", n); break;
    default: { static const char *const fixed[] = {"-eggplant", "-X2", "-R0", "-G", "-SPRUNKNOWN", "-v1", "-Z1", "-s", "-C"};
               strcpy(t, fixed[su_random() % 9u]); break; }
    }
}
static int su_parsers(unsigned cases) {
    char line[1024];
    static uint32_t argv[2][27];
    uint32_t argc[2];
    es2_sim_startup s;
    for (unsigned k = 0; k < cases; ++k) {
        current_case = k; current_case_valid = 1;
        size_t n = 0;
        unsigned count = su_random() % 27u;
        if (k < 4u) count = k;   /* empty, one, two, three arguments */
        for (unsigned i = 0; i < count; ++i) {
            char t[64];
            su_token(t);
            unsigned spaces = 1u + (su_random() % 4u == 0 ? su_random() % 3u : 0u);
            if (i == 0 && su_random() % 4u == 0) spaces = 0;
            if (n + spaces + strlen(t) + 2 >= sizeof(line)) break;
            for (unsigned j = 0; j < spaces; ++j) line[n++] = ' ';
            memcpy(line + n, t, strlen(t)); n += strlen(t);
        }
        if (su_random() % 4u == 0) line[n++] = ' ';
        line[n] = 0;
        uint8_t written = (uint8_t)(su_random() % 3u);   /* the first prefs_video call reads data\prefs.cfg (NULL here) */
        for (unsigned side = 0; side < 2u; ++side) {
            su_reset(side);
            mapped[0x9f254u] = written ? 1u : 0u;    /* prefs_video's once flag: both branches */
            memset(argv[side], 0, sizeof(argv[side]));
            uint32_t cmd = (uint32_t)(uintptr_t)line, av = (uint32_t)(uintptr_t)argv[side];
            if (!side) {
                uint32_t a[2] = {cmd, av};
                argc[0] = su_machine(0x004652f8u, a, 2);
                uint32_t b[2] = {argc[0], av};
                su_machine(0x0045e6ccu, b, 2);
                su_machine(0x0045e758u, b, 2);
            } else {
                su_own(&s);
                if (es2_sim_startup_split(&s, cmd, av, &argc[1]) || es2_sim_startup_early_parser(&s, argc[1], av) ||
                    es2_sim_startup_main_parser(&s, argc[1], av))
                    su_bad = 1;
            }
            if (su_bad) { fprintf(stderr, "FAIL: startup parsers case %u: a call failed (line \"%s\")\n", k, line); return 1; }
            su_take(side);
        }
        if (argc[0] != argc[1] || memcmp(argv[0], argv[1], sizeof(argv[0]))) {
            fprintf(stderr, "FAIL: startup parsers case %u: argc %lu/%lu or argv differ (line \"%s\")\n", k, (unsigned long)argc[0],
                    (unsigned long)argc[1], line);
            for (unsigned i = 0; i < 27u; ++i)
                if (argv[0][i] != argv[1][i]) fprintf(stderr, "  argv[%u] 0x%08lx / 0x%08lx\n", i, (unsigned long)argv[0][i], (unsigned long)argv[1][i]);
            return 1;
        }
        if (su_compare("parsers", k)) { fprintf(stderr, "  line \"%s\"\n", line); return 1; }
        if (strcmp(ir_log[0], ir_log[1]) || su_calls[0] != su_calls[1] || su_calls_hash[0] != su_calls_hash[1]) {
            fprintf(stderr, "FAIL: startup parsers case %u: calls %s / %s (line \"%s\")\n", k, ir_log[0], ir_log[1], line);
            return 1;
        }
    }
    current_case_valid = 0;
    return 0;
}

/* ---- error.str groups */
static int su_strings(unsigned cases) {
    BYTE *buffer = (BYTE *)su_frame;   /* the holder's text: the frame area's start */
    uint32_t holder = (uint32_t)(uintptr_t)su_frame + 0x3000u;
    /* Per side: the string slots of both groups, the second outputs, the holder after. */
    static uint32_t out[2][2][2][80], state[2][4];
    es2_sim_startup s;
    for (unsigned k = 0; k < cases; ++k) {
        current_case = k; current_case_valid = 1;
        /* Two groups: a count word, then per string a length word, the
         * bytes, a byte e and (read only with a second output) e further
         * bytes; partly truncated. Without the second output the original
         * does not skip the e bytes, so e is 0 then. */
        int second = (int)(su_random() & 1u);
        uint32_t size = 0;
        memset(buffer, 0, 0x2800u);   /* bytes past the end read as zero lengths, never as stale text */
        for (unsigned g = 0; g < 2u; ++g) {
            unsigned count = su_random() % 70u;
            if (su_random() % 4u == 0) count = su_random() % 5u;
            put16(buffer + size, (uint16_t)count); size += 2u;
            for (unsigned i = 0; i < count && size < 0x2000u; ++i) {
                unsigned length = su_random() % 12u, extra = second ? su_random() % 6u : 0u;
                put16(buffer + size, (uint16_t)length); size += 2u;
                for (unsigned j = 0; j < length; ++j) buffer[size++] = (BYTE)('a' + su_random() % 26u);
                buffer[size++] = (BYTE)extra;
                for (unsigned j = 0; j < extra; ++j) buffer[size++] = (BYTE)('A' + su_random() % 26u);
            }
        }
        if (su_random() % 6u == 0) size = su_random() % (size + 1u);   /* a buffer that ends inside */
        for (unsigned side = 0; side < 2u; ++side) {
            uint32_t *h = (uint32_t *)(uintptr_t)holder;
            h[0] = (uint32_t)(uintptr_t)buffer; h[1] = h[0]; h[2] = size; h[3] = 0;
            memset(out[side], 0, sizeof(out[side]));
            for (unsigned g = 0; g < 2u; ++g) {
                uint32_t array = (uint32_t)(uintptr_t)out[side][g][0];
                uint32_t two = second ? (uint32_t)(uintptr_t)out[side][g][1] : 0u;
                if (!side) {
                    uint32_t a[4] = {holder, array, 12u, two};
                    su_machine(0x004539a0u, a, 4);
                } else {
                    su_own(&s);
                    if (es2_sim_startup_read_group(&s, holder, array, array + sizeof(out[side][g][0]), two)) su_bad = 1;
                }
            }
            if (su_bad) { fprintf(stderr, "FAIL: startup strings case %u: refused\n", k); return 1; }
            memcpy(state[side], h, 16);
        }
        if (memcmp(out[0], out[1], sizeof(out[0])) || memcmp(state[0], state[1], sizeof(state[0]))) {
            fprintf(stderr, "FAIL: startup strings case %u: output or holder differs\n", k);
            return 1;
        }
    }
    current_case_valid = 0;
    return 0;
}

/* ---- volumes */
static void su_write(const char *path, const BYTE *p, size_t n) {
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(p, 1, n, f); fclose(f); }
}
/* A synthetic VOLN file (vol.md layout): signature, use mask, priority,
 * `dirs` directory strings, `members` records, member blocks. */
static void su_volume(const char *path, const char *signature, uint32_t mask, BYTE priority, unsigned dirs, unsigned members,
                      int truncate) {
    static BYTE v[0x20000];
    size_t n = 0;
    memcpy(v, signature, 4); n = 4;
    put32(v + n, mask); n += 4;
    v[n++] = priority;
    v[n++] = (BYTE)dirs;
    size_t at_bytes = n; n += 2;
    size_t first = n;
    for (unsigned d = 0; d < dirs; ++d) n += (size_t)sprintf((char *)v + n, "d%u\\", d) + 1u;
    put16(v + at_bytes, (uint16_t)(n - first));
    put16(v + n, (uint16_t)members); n += 2;
    put32(v + n, 18u * members); n += 4;
    size_t records = n;
    n += 18u * members;
    for (unsigned m = 0; m < members; ++m) {
        BYTE *r = v + records + 18u * m;
        memset(r, 0, 18);
        sprintf((char *)r, "F%u.DAT", m);
        r[13] = (BYTE)(dirs ? m % dirs : 0);
        put32(r + 14, (uint32_t)n);
        v[n++] = 2; put32(v + n, 3); n += 4; put32(v + n, m); n += 4;
        v[n++] = 'a'; v[n++] = 'b'; v[n++] = 'c'; v[n++] = 'c';
    }
    if (truncate) n = records + 5u;
    su_write(path, v, n);
}
static BYTE su_files_state[2][0xc8u + 0x4b0u];
static int su_volume_run(const char *what, unsigned index, uint32_t mask, const char *root, uint32_t alternate_first) {
    static char root_text[MAX_PATH];
    static const char name[] = ".vol";
    strcpy(root_text, root);
    for (unsigned side = 0; side < 2u; ++side) {
        su_reset(side);
        if (su_files_reset()) return 1;
        uint32_t r = root[0] ? (uint32_t)(uintptr_t)root_text : 0u;
        if (!side) {
            uint32_t a[4] = {mask, (uint32_t)(uintptr_t)name, r, alternate_first};
            su_machine(0x00473260u, a, 4);
        } else {
            es2_sim_startup s;
            su_own(&s);
            if (es2_sim_startup_register_volumes(&s, mask, (uint32_t)(uintptr_t)name, r, alternate_first)) su_bad = 1;
        }
        if (su_bad) { fprintf(stderr, "FAIL: startup volumes %s %u: refused or a model call failed\n", what, index); return 1; }
        su_take(side);
        su_snap_find_nodes(side);
        /* CW3220's tables except the OS handles: the handle flags and the FILE array. */
        memcpy(su_files_state[side], su_files_region + 0x10u, 0xc8u);
        memcpy(su_files_state[side] + 0xc8u, su_files_region + 0x1b0u, 0x4b0u);
    }
    if (su_compare(what, index)) return 1;
    if (memcmp(su_files_state[0], su_files_state[1], sizeof(su_files_state[0]))) {
        fprintf(stderr, "FAIL: startup volumes %s %u: CW3220's handle flags or FILE array differ\n", what, index);
        return 1;
    }
    if (strcmp(ir_log[0], ir_log[1]) || su_calls[0] != su_calls[1] || su_calls_hash[0] != su_calls_hash[1]) {
        fprintf(stderr, "FAIL: startup volumes %s %u: calls\n  %s\n  %s\n", what, index, ir_log[0], ir_log[1]);
        return 1;
    }
    uint32_t manager = get32(mapped + 0xa0d18u);
    printf("VOLUMES %s %u: mask 0x%lx root \"%s\" alternate-first %lu: %u archives, counter %ld, heap 0x%lx bytes, calls %u; identical\n",
           what, index, (unsigned long)mask, root, (unsigned long)alternate_first, manager ? ((BYTE *)(uintptr_t)manager)[0x78] : 0u,
           (long)(int32_t)get32(mapped + (SU_COUNTER - 0x400000u)), (unsigned long)su_heap_used(), su_calls[0]);
    return 0;
}
static int su_volumes(const char *cd) {
    char here[MAX_PATH], temp[MAX_PATH], path[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, here);
    /* The CD's VOL\ from the CD root; the second root is the CD root itself. */
    char cd_root[MAX_PATH];
    snprintf(cd_root, sizeof(cd_root), "%s\\", cd);
    if (!SetCurrentDirectoryA(cd)) return fail("startup volumes: the CD root");
    int bad = su_volume_run("cd", 1, 1u, cd_root, 0) || su_volume_run("cd", 2, 0x100u, cd_root, 0) ||
              su_volume_run("cd", 3, 0x101u, cd_root, 0) || su_volume_run("cd", 4, 1u, "", 0);
    SetCurrentDirectoryA(here);
    if (bad) return 1;
    /* Synthetic archives in a temporary directory. */
    GetTempPathA(MAX_PATH, temp);
    snprintf(path, sizeof(path), "%ses2-startup-%lu", temp, (unsigned long)GetCurrentProcessId());
    CreateDirectoryA(path, NULL);
    if (!SetCurrentDirectoryA(path)) return fail("startup volumes: temporary directory");
    CreateDirectoryA("vol", NULL);
    CreateDirectoryA("r2", NULL);
    CreateDirectoryA("r2\\vol", NULL);
    su_volume("vol\\a5.vol", "VOLN", 1u, 5, 3, 4, 0);
    su_volume("vol\\b5.vol", "VOLN", 1u, 5, 1, 2, 0);
    su_volume("vol\\c10.vol", "VOLN", 3u, 10, 41, 10, 0);
    su_volume("vol\\d10.vol", "VOLN", 1u, 10, 2, 3, 0);
    su_volume("vol\\e.vol", "VOLN", 0x100u, 9, 1, 1, 0);
    su_volume("vol\\f.vol", "VOLX", 1u, 9, 1, 1, 0);
    su_volume("vol\\g.vol", "XOLN", 1u, 9, 1, 1, 0);
    su_volume("vol\\h.vol", "VOLN", 1u, 7, 30, 400, 0);   /* tables over several 0x400 windows */
    su_volume("vol\\i.vol", "VOLN", 1u, 6, 0, 0, 0);      /* no directories, no members */
    su_volume("r2\\vol\\a5.vol", "VOLN", 1u, 99, 1, 1, 0); /* a name of pass 1: skipped */
    su_volume("r2\\vol\\z.vol", "VOLN", 1u, 5, 2, 2, 0);
    bad = su_volume_run("synthetic", 1, 1u, "r2\\", 0) || su_volume_run("synthetic", 2, 1u, "r2\\", 1) ||
          su_volume_run("synthetic", 3, 0x100u, "r2\\", 0) || su_volume_run("synthetic", 4, 2u, "", 0) ||
          su_volume_run("synthetic", 5, 0xffffffffu, "r2\\", 0);
    SetCurrentDirectoryA(here);
    const char *names[] = {"vol\\a5.vol", "vol\\b5.vol", "vol\\c10.vol", "vol\\d10.vol", "vol\\e.vol", "vol\\f.vol", "vol\\g.vol",
                           "vol\\h.vol", "vol\\i.vol", "r2\\vol\\a5.vol", "r2\\vol\\z.vol"};
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        char f[MAX_PATH];
        snprintf(f, sizeof(f), "%s\\%s", path, names[i]);
        DeleteFileA(f);
    }
    snprintf(temp, sizeof(temp), "%s\\r2\\vol", path); RemoveDirectoryA(temp);
    snprintf(temp, sizeof(temp), "%s\\r2", path); RemoveDirectoryA(temp);
    snprintf(temp, sizeof(temp), "%s\\vol", path); RemoveDirectoryA(temp);
    RemoveDirectoryA(path);
    return bad;
}

static int startup_cases(const char *cd) {
    current_function = "startup";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_frame = VirtualAlloc(NULL, SU_FRAME_BYTES, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    su_files_region = VirtualAlloc(NULL, ES2_SIM_FILES_REGION_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    ir_region = ww_alloc(IR_REGION_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !su_frame || !su_files_region || !ir_region)
        return fail("startup: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("startup: the counter word is not free");
    su_pristine_take();
    static const struct { const char *name; void *stub; } stubs[] = {
        {"_calloc", (void *)su_calloc}, {"_free", (void *)su_free}, {"_atol", (void *)su_atol}, {"_strchr", (void *)su_strchr},
        {"_strlen", (void *)su_strlen}, {"_strcat", (void *)su_strcat}, {"_stricmp", (void *)su_stricmp},
        {"_strncmp", (void *)su_strncmp}, {"_memcpy", (void *)su_memcpy}, {"_fopen", (void *)su_fopen}, {"_open", (void *)su_open},
        {"_close", (void *)su_close}, {"_read", (void *)su_read}, {"_lseek", (void *)su_lseek},
        {"_findfirst", (void *)su_findfirst}, {"_findnext", (void *)su_findnext},
    };
    for (unsigned i = 0; i < sizeof(stubs) / sizeof(stubs[0]); ++i)
        if (!ir_patch_import("cw3220.dll", stubs[i].name, stubs[i].stub)) return fail("startup: import slot not found");
    if (su_dispatch_setup()) return 1;
    ir_heap_reset();   /* the file model's stdin/stdout buffers (407248) come from the heap */
    if (su_files_reset()) return 1;
    int bad = su_parsers(4000);
    if (!bad) printf("PASS: startup parsers: 4000 command lines through 0x4652f8, 0x45e6cc, 0x45e758 identical (DATA, heap, argc, argv, calls)\n");
    if (!bad) bad = su_strings(4000);
    if (!bad) printf("PASS: startup strings: 4000 holders through 0x4539a0/0x453918 (two groups) identical\n");
    if (!bad) bad = su_volumes(cd);
    if (!bad) printf("PASS: startup volumes: the registration 0x473260 identical on the CD's VOL\\ and on synthetic archives\n");
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}

/* The suite entry (sim_main.c): the CD root is ES2_DATA_ROOT itself when it
 * holds VOL\, else its extracted\cd. */
static int startup_suite(void) {
    const char *root=getenv("ES2_DATA_ROOT");
    char cd[MAX_PATH]; snprintf(cd,sizeof(cd),"%s\\VOL",root);
    if(GetFileAttributesA(cd)!=INVALID_FILE_ATTRIBUTES) snprintf(cd,sizeof(cd),"%s",root); else snprintf(cd,sizeof(cd),"%s\\extracted\\cd",root);
    return startup_cases(cd);
}
