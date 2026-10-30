/* The rows of sim/dispatch/services/gap_roots_shell.h, the error report 0x479c78 among them
 * (sim/src/services_files.c and sim/src/loop.c, own_process.md, "SHELL, RTL, FILE
 * and INP gap roots as rows and verdicts"), original machine code against the own code
 * (target :differential_gap_roots_shell_test): the help file's language 0x45effc and name 0x45f070,
 * the -D demo pick 0x45ceb8, the notify list 0x479c54, the report text 0x479dd0, the log
 * path 0x479e48, the log 0x479e8c, the report body 0x479f24 and the report 0x479c78. It
 * reuses startup_cases.h's snapshots and loop_cases.h's loop, as hud_mfd_leaves_cases.h does.
 * Identifiers use h25_/H25_.
 *
 * Both sides start from the pristine DATA and build the same seeded state:
 * the roots 0x4d27bc, 0x4d280c and 0x6c56dc (random short texts), the log's
 * separator flag 0x4a1610, the warning count 0x6c582c, the notify list
 * 0x6c56d8 (0..3 nodes whose function is a stub), the demo bytes
 * 0x49f032/0x49f033 with the count word 0x4d26f0 (positive when the list is
 * marked read), the name table 0x4d26f4 and the last pick 0x49f034. Then
 * 1..6 calls of the nine functions with random entry EAX, EDX and ECX and
 * arguments: severities 0..7 or any dword (0..6 for 0x479dd0, which indexes
 * the name table; a text for severity 2, whose log has no null check), a
 * file text, any line, a text or NULL.
 *
 * The callees are recording stubs on both sides: _fopen, _fread, _fclose,
 * _open, _write, _close, _time, __exit and _abort (the original's import
 * slots; the own side's file and _time rows replaced after the seal, __exit
 * and _abort stub services: no rows), the display guard 0x4669b4, the timer
 * 0x4650d0, the holder constructor 0x453824, the stream 0x47ad40/0x47ad78,
 * new[] 0x4774f0 and the group 0x4539a0 (code patches; rows replaced), the
 * stream's read slot +0x18 and the notify functions (the original side's
 * stubs; the own side serves their addresses). MessageBoxA and
 * PostQuitMessage are import stubs on the original side; the own side's
 * headless platform counts them (message_box, post_quit,
 * es2_sim_fatal_box_last), and those counts are written into the same log
 * in order. _strcat and _sprintf are not logged: the original's import slots
 * do the work through the own runtime, the own side runs its rows. A stub
 * logs its letter and arguments (texts and buffers as a hash of their bytes,
 * stack pointers not at all) and leaves seeded EAX, EDX and ECX.
 *
 * Compared: EAX of every call; EDX and ECX of 0x45effc and of a demo pick that
 * answers a name; ECX of 0x45f070 (its EDX is _strcat's, not reproduced; the
 * report's EDX and ECX neither: own_process.md, "SHELL, RTL, FILE and INP gap
 * roots as rows and verdicts"); DATA 0x497000..0x6cc000 and the heap; the stub
 * log. Coverage: counters from the original side, every one must be
 * non-zero. */
#pragma once
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"

void es2_sim_service_fatal_report_479c78(es2_sim_service_call *call, void *user);
void es2_sim_fatal_box_last(uint32_t *caption, uint64_t *hash);

static es2_sim_entry h25_entries[4096];
static es2_sim_dispatch h25_dispatch;
static int h25_bad;
static unsigned h25_side;

extern void h25_call(uint32_t target, const uint32_t *args, uint32_t count, const uint32_t *in, uint32_t *out);
__asm__(".text\n.globl _h25_call\n_h25_call:\n\tpushl %ebp\n\tmovl %esp, %ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n"
        "\tmovl 16(%ebp), %ecx\n\tmovl 12(%ebp), %esi\n"
        "1:\n\ttestl %ecx, %ecx\n\tjz 2f\n\tdecl %ecx\n\tpushl (%esi,%ecx,4)\n\tjmp 1b\n"
        "2:\n\tmovl 20(%ebp), %ebx\n\tmovl (%ebx), %eax\n\tmovl 4(%ebx), %edx\n\tmovl 8(%ebx), %ecx\n"
        "\tcall *8(%ebp)\n"
        "\tmovl 24(%ebp), %ebx\n\tmovl %eax, (%ebx)\n\tmovl %edx, 4(%ebx)\n\tmovl %ecx, 8(%ebx)\n"
        "\tleal -12(%ebp), %esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n");

/* ---- the scratch area in DATA */
#define H25_X 0x006b0000u
#define H25_FILES (H25_X + 0x000u)    /* 4 file texts, 0x40 apart */
#define H25_TEXTS (H25_X + 0x100u)    /* 4 texts, 0x80 apart */
#define H25_NODES (H25_X + 0x400u)    /* 3 notify nodes, 8 apart */
#define H25_VT (H25_X + 0x500u)       /* the stream's vtable */
#define H25_NEW (H25_X + 0x600u)      /* new[]'s answer, 0x200 */
#define H25_BUF (H25_X + 0x800u)      /* 0x479dd0's buffer, 0x400 */
#define H25_NAMES (H25_X + 0xc00u)    /* demo names, 0x10 apart */
#define H25_FILE 0x006b0f00u          /* the FILE value fopen answers */

static uint32_t h25_get32(uint32_t a) { return get32((BYTE *)(uintptr_t)a); }
static void h25_put32(uint32_t a, uint32_t v) { put32((BYTE *)(uintptr_t)a, v); }
static void h25_put16(uint32_t a, uint16_t v) { put16((BYTE *)(uintptr_t)a, v); }
static void h25_put8(uint32_t a, uint8_t v) { *(BYTE *)(uintptr_t)a = v; }
static uint8_t h25_get8(uint32_t a) { return *(const BYTE *)(uintptr_t)a; }
static uint32_t h25_hash_text(uint32_t p) {
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < 0x1000u; ++i) {
        uint8_t b = h25_get8(p + i);
        if (!b) break;
        h = (h ^ b) * 16777619u;
    }
    return h;
}
static uint32_t h25_hash_bytes(uint32_t p, uint32_t n) {
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < n && i < 0x1000u; ++i) h = (h ^ h25_get8(p + i)) * 16777619u;
    return h;
}

/* ---- the stub log of a side */
static uint64_t h25_hash[2];
static unsigned h25_count[2];
static char h25_text[2][4096];
static size_t h25_len[2];
static void h25_raw(char letter, const uint32_t *s, unsigned n) {
    uint64_t h = h25_hash[h25_side];
    uint32_t v[6] = {(uint32_t)letter, 0, 0, 0, 0, 0};
    for (unsigned i = 0; i < n && i < 5u; ++i) v[1 + i] = s[i];
    for (unsigned i = 0; i < 6u; ++i) { h ^= v[i]; h *= 0x100000001b3ull; }
    h25_hash[h25_side] = h;
    ++h25_count[h25_side];
    if (h25_len[h25_side] + 90u < sizeof(h25_text[0]))
        h25_len[h25_side] += (size_t)sprintf(h25_text[h25_side] + h25_len[h25_side], "%c(%lx,%lx,%lx,%lx,%lx) ", letter,
                                             (unsigned long)v[1], (unsigned long)v[2], (unsigned long)v[3], (unsigned long)v[4],
                                             (unsigned long)v[5]);
}
/* The own platform's MessageBoxA and PostQuitMessage since the last look,
 * into the log (the own side only). */
static unsigned h25_seen_box, h25_seen_quit;
static void h25_flush(void) {
    if (h25_side != 1u) return;
    if (lc_loop.platform.message_box != h25_seen_box) {
        h25_seen_box = lc_loop.platform.message_box;
        uint32_t caption = 0;
        uint64_t h = 0;
        es2_sim_fatal_box_last(&caption, &h);
        uint32_t v[5] = {0, (uint32_t)h, (uint32_t)(h >> 32), caption, 0};
        h25_raw('M', v, 5);
    }
    if (lc_loop.platform.post_quit != h25_seen_quit) {
        h25_seen_quit = lc_loop.platform.post_quit;
        uint32_t v[1] = {0};
        h25_raw('Q', v, 1);
    }
}
static void h25_note(char letter, const uint32_t *s, unsigned n) {
    h25_flush();
    h25_raw(letter, s, n);
}

/* ---- coverage */
enum { H25C_LANG_NONE, H25C_LANG_F, H25C_LANG_G, H25C_LANG_S, H25C_LANG_OTHER, H25C_NAME, H25C_DEMO_BUILD, H25C_DEMO_BUILT,
       H25C_DEMO_READ, H25C_DEMO_STATUS, H25C_DEMO_EMPTY, H25C_DEMO_FILLED, H25C_DEMO_NONE, H25C_DEMO_ONE, H25C_DEMO_TIME,
       H25C_DEMO_NEGATIVE, H25C_NOTIFY_EMPTY, H25C_NOTIFY_NODES, H25C_TEXT_NULL, H25C_TEXT, H25C_PATH, H25C_OPEN_FAIL,
       H25C_RULE, H25C_NO_RULE, H25C_SEV_LOW, H25C_SEV_2, H25C_SEV_3, H25C_SEV_4, H25C_SEV_5, H25C_SEV_6, H25C_SEV_HIGH,
       H25C_FATAL_END, H25C_FATAL_PLAIN, H25C_COUNT };
static const char *const h25_cv_names[H25C_COUNT] = {
    "language no file", "language F", "language G", "language S", "language other", "help name", "demo holder built",
    "demo holder there", "demo list read", "demo stream status", "demo list empty", "demo list filled", "demo no list",
    "demo one", "demo time", "demo negative time", "notify empty", "notify nodes", "text without text", "text with text",
    "log path", "log open fails", "log separator", "log no separator", "report 0/1", "report 2", "report 3", "report 4",
    "report 5", "report 6", "report above 6", "fatal ends", "fatal reports only"};
static unsigned h25_cv[H25C_COUNT];
static void h25_cover(unsigned c) { if (!h25_side) ++h25_cv[c]; }

/* ---- the stubs' bodies, one for both sides */
static uint32_t h25_answer_seed;
static uint32_t h25_answer(void) {
    h25_answer_seed = h25_answer_seed * 1103515245u + 12345u;
    return h25_answer_seed ^ (h25_answer_seed >> 13);
}
static void h25_stub(char letter, const uint32_t *s, uint32_t out[3]) {
    uint32_t v[5] = {0, 0, 0, 0, 0};
    /* MessageBoxA and PostQuitMessage take no answers: the own side has no
     * stub for them. */
    int plain = letter == 'M' || letter == 'Q';
    out[0] = plain ? 0 : h25_answer();
    out[1] = plain ? 0 : h25_answer();
    out[2] = plain ? 0 : h25_answer();
    switch (letter) {
    case 'o':   /* _fopen(path, mode): NULL in a quarter */
        v[0] = h25_hash_text(s[0]); v[1] = h25_hash_text(s[1]);
        h25_note(letter, v, 2);
        out[0] = h25_answer() % 4u ? H25_FILE : 0;
        if (!out[0]) h25_cover(H25C_LANG_NONE);
        break;
    case 'r': { /* _fread(dst, size, n, f): one byte F, G, S or other */
        v[0] = s[1]; v[1] = s[2]; v[2] = s[3];
        h25_note(letter, v, 3);
        uint32_t k = h25_answer() % 4u;
        uint8_t b = k == 0 ? 'F' : k == 1 ? 'G' : k == 2 ? 'S' : (uint8_t)h25_answer();
        if (s[1] * s[2] == 1u) h25_put8(s[0], b);
        h25_cover(b == 'F' ? H25C_LANG_F : b == 'G' ? H25C_LANG_G : b == 'S' ? H25C_LANG_S : H25C_LANG_OTHER);
        out[0] = 1;
        break;
    }
    case 'c': case 'K': case 'N': case 'X': case 'h': case 'n':   /* one plain argument */
        v[0] = s[0];
        h25_note(letter, v, 1);
        if (letter == 'n') out[0] = H25_NEW;
        break;
    case 'O':   /* _open(path, flags, mode): negative in a quarter */
        v[0] = h25_hash_text(s[0]); v[1] = s[1]; v[2] = s[2];
        h25_note(letter, v, 3);
        out[0] = h25_answer() % 4u ? 3u + h25_answer() % 20u : 0x80000000u | h25_answer();
        if ((int32_t)out[0] < 0) h25_cover(H25C_OPEN_FAIL);
        break;
    case 'W':   /* _write(h, buf, n) */
        v[0] = s[0]; v[1] = s[2]; v[2] = h25_hash_bytes(s[1], s[2]);
        h25_note(letter, v, 3);
        out[0] = s[2];
        break;
    case 'T': { /* _time(&t) */
        h25_note(letter, v, 0);
        uint32_t t = h25_answer() % 3u ? h25_answer() & 0x7fffffffu : 0x80000000u | h25_answer();
        if (s[0]) h25_put32(s[0], t);
        out[0] = t;
        if ((int32_t)t < 0) h25_cover(H25C_DEMO_NEGATIVE);
        break;
    }
    case 'M':   /* MessageBoxA(w, text, caption, type): IDOK */
        v[0] = s[0];
        {
            uint64_t h = 0xcbf29ce484222325ull;
            for (uint32_t i = 0; i < 0x1000u; ++i) {
                uint8_t b = h25_get8(s[1] + i);
                if (!b) break;
                h = (h ^ b) * 0x100000001b3ull;
            }
            v[1] = (uint32_t)h; v[2] = (uint32_t)(h >> 32);
        }
        v[3] = s[2]; v[4] = s[3];
        h25_note(letter, v, 5);
        out[0] = 1;
        break;
    case 'Q':   /* PostQuitMessage(code) */
        v[0] = s[0];
        h25_note(letter, v, 1);
        break;
    case 's': { /* stream open(S, path): S's vtable and status */
        v[0] = h25_hash_text(s[1]);
        h25_note(letter, v, 1);
        h25_put32(s[0], H25_VT);
        uint32_t status = h25_answer() % 3u ? 0u : 1u + h25_answer() % 0xffffu;
        h25_put32(s[0] + 4u, status);
        h25_put32(s[0] + 8u, h25_answer());
        if (status) h25_cover(H25C_DEMO_STATUS);
        break;
    }
    case 'R': { /* read slot(S, n, dst): the size at 0x4d26e8, the bytes into new[]'s block */
        v[0] = s[1]; v[1] = s[2];
        h25_note(letter, v, 2);
        if (s[2] == 0x004d26e8u && s[1] == 4u) {
            h25_put32(s[2], h25_answer() % 5u ? h25_answer() % 0x180u : 0u);
        } else if (s[2] == H25_NEW) {
            for (uint32_t i = 0; i < s[1] && i < 0x200u; ++i) h25_put8(H25_NEW + i, (uint8_t)h25_answer());
        }
        break;
    }
    case 'g': { /* strings group(holder, names, n, x): names and a count */
        v[0] = s[0]; v[1] = s[1]; v[2] = s[2]; v[3] = s[3];
        h25_note(letter, v, 4);
        for (uint32_t i = 0; i < 20u; ++i) h25_put32(s[1] + 4u * i, H25_NAMES + 0x10u * (h25_answer() % 16u));
        uint32_t k = h25_answer() % 5u;
        uint32_t count = k == 0 ? (uint32_t)-(int32_t)(h25_answer() % 4u) : k == 1 ? 1u : 2u + h25_answer() % 19u;
        out[0] = (out[0] & 0xffff0000u) | (count & 0xffffu);
        h25_cover((int16_t)(uint16_t)count > 0 ? H25C_DEMO_FILLED : H25C_DEMO_EMPTY);
        break;
    }
    case 'x':   /* stream destroy(S, flags) */
        v[0] = s[1];
        h25_note(letter, v, 1);
        break;
    default:    /* no arguments: d (display guard), k (timer), A (_abort) */
        h25_note(letter, v, 0);
        break;
    }
    if (getenv("H25_TRACE") && h25_len[h25_side] + 12u < sizeof(h25_text[0])) h25_len[h25_side] += (size_t)sprintf(h25_text[h25_side] + h25_len[h25_side], "=%lx ", (unsigned long)out[0]);
}

/* ---- the original side's stubs: naked entries that keep the registers */
uint32_t h25_reg_out[3];
void __cdecl h25_stub_c(uint32_t letter, const uint32_t *args) { h25_stub((char)letter, args, h25_reg_out); }
#define H25_STUB(name, letter, pop)                                                                                \
    extern void name(void);                                                                                        \
    __asm__(".text\n.globl _" #name "\n_" #name ":\n\tleal 4(%esp), %eax\n\tpushl %eax\n\tpushl $" #letter "\n"   \
            "\tcall _h25_stub_c\n\taddl $8, %esp\n"                                                                \
            "\tmovl _h25_reg_out, %eax\n\tmovl _h25_reg_out+4, %edx\n\tmovl _h25_reg_out+8, %ecx\n\tret $" #pop "\n");
H25_STUB(h25_o_fopen, 111, 0)     /* 'o' */
H25_STUB(h25_o_fread, 114, 0)     /* 'r' */
H25_STUB(h25_o_fclose, 99, 0)     /* 'c' */
H25_STUB(h25_o_open, 79, 0)       /* 'O' */
H25_STUB(h25_o_write, 87, 0)      /* 'W' */
H25_STUB(h25_o_close, 75, 0)      /* 'K' */
H25_STUB(h25_o_time, 84, 0)       /* 'T' */
H25_STUB(h25_o_exit, 88, 0)       /* 'X' */
H25_STUB(h25_o_abort, 65, 0)      /* 'A' */
H25_STUB(h25_o_box, 77, 16)       /* 'M', stdcall */
H25_STUB(h25_o_quit, 81, 4)       /* 'Q', stdcall */
H25_STUB(h25_o_display, 100, 0)   /* 'd' */
H25_STUB(h25_o_timer, 107, 0)     /* 'k' */
H25_STUB(h25_o_holder, 104, 0)    /* 'h' */
H25_STUB(h25_o_stream, 115, 0)    /* 's' */
H25_STUB(h25_o_destroy, 120, 0)   /* 'x' */
H25_STUB(h25_o_new, 110, 0)       /* 'n' */
H25_STUB(h25_o_group, 103, 0)     /* 'g' */
H25_STUB(h25_o_read, 82, 0)       /* 'R' (the vtable slot) */
H25_STUB(h25_o_notify, 78, 0)     /* 'N' (the nodes' function) */
/* _strcat and _sprintf through the own runtime, not logged */
static uint32_t __cdecl h25_rtl_strcat(const uint32_t *a) { return su_rtl_call(ES2_RTL_STRCAT, a); }
static uint32_t __cdecl h25_rtl_sprintf(const uint32_t *a) { return su_rtl_call(ES2_RTL_SPRINTF, a); }
uint32_t (*h25_rtl_strcat_p)(const uint32_t *) = h25_rtl_strcat;
uint32_t (*h25_rtl_sprintf_p)(const uint32_t *) = h25_rtl_sprintf;
#define H25_RTL(name, target)                                                                         \
    extern void name(void);                                                                           \
    __asm__(".text\n.globl _" #name "\n_" #name ":\n\tleal 4(%esp), %eax\n\tpushl %eax\n"             \
            "\tcall *_" #target "\n\taddl $4, %esp\n\tret\n");
H25_RTL(h25_o_strcat, h25_rtl_strcat_p)
H25_RTL(h25_o_sprintf, h25_rtl_sprintf_p)

#define H25_SITES 7u
static const struct { uint32_t va; void (*stub)(void); } h25_sites[H25_SITES] = {
    {0x004669b4u, h25_o_display}, {0x004650d0u, h25_o_timer},  {0x00453824u, h25_o_holder}, {0x0047ad40u, h25_o_stream},
    {0x0047ad78u, h25_o_destroy}, {0x004774f0u, h25_o_new},    {0x004539a0u, h25_o_group},
};
static BYTE h25_saved[H25_SITES][5];
static int h25_patch(int install) {
    for (unsigned j = 0; j < H25_SITES; ++j) {
        unsigned i = install ? j : H25_SITES - 1u - j;
        BYTE *site = mapped + (h25_sites[i].va - 0x400000u);
        DWORD previous;
        if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) return fail("gap roots shell: stub protection");
        if (install) {
            memcpy(h25_saved[i], site, 5);
            site[0] = 0xe9;
            put32(site + 1, (uint32_t)(uintptr_t)h25_sites[i].stub - (uint32_t)(uintptr_t)(site + 5));
        } else {
            memcpy(site, h25_saved[i], 5);
        }
        if (!VirtualProtect(site, 5, previous, &previous)) return fail("gap roots shell: stub restore protection");
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    return 0;
}

/* ---- the own side */
#define H25_OWN(name, letter)                                        \
    static void name(es2_sim_service_call *call, void *user) {       \
        (void)user;                                                  \
        uint32_t out[3];                                             \
        h25_stub(letter, call->slots, out);                          \
        call->eax = out[0]; call->edx = out[1]; call->ecx = out[2];  \
    }
H25_OWN(h25_s_fopen, 'o')
H25_OWN(h25_s_fread, 'r')
H25_OWN(h25_s_fclose, 'c')
H25_OWN(h25_s_open, 'O')
H25_OWN(h25_s_write, 'W')
H25_OWN(h25_s_close, 'K')
H25_OWN(h25_s_time, 'T')
H25_OWN(h25_s_exit, 'X')
H25_OWN(h25_s_abort, 'A')
H25_OWN(h25_s_display, 'd')
H25_OWN(h25_s_timer, 'k')
H25_OWN(h25_s_holder, 'h')
H25_OWN(h25_s_stream, 's')
H25_OWN(h25_s_destroy, 'x')
H25_OWN(h25_s_new, 'n')
H25_OWN(h25_s_group, 'g')
H25_OWN(h25_s_read, 'R')
H25_OWN(h25_s_notify, 'N')
static void h25_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own gap roots shell: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    h25_bad = 1;
}
static int h25_dispatch_setup(void) {
    es2_sim_dispatch_init(&h25_dispatch, h25_entries, 4096, h25_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&h25_dispatch, &s)) return fail("gap roots shell: dispatch");
    if (es2_sim_dispatch_add_service(&h25_dispatch, 0x0049679cu, "gap roots shell __exit stub", h25_s_exit, NULL, 0, 1) ||
        es2_sim_dispatch_add_service(&h25_dispatch, 0x0049688cu, "gap roots shell _abort stub", h25_s_abort, NULL, 0, 0) ||
        es2_sim_dispatch_add_service(&h25_dispatch, (uint32_t)(uintptr_t)h25_o_read, "gap roots shell read slot stub", h25_s_read, NULL, 0, 3) ||
        es2_sim_dispatch_add_service(&h25_dispatch, (uint32_t)(uintptr_t)h25_o_notify, "gap roots shell notify stub", h25_s_notify, NULL, 0, 1))
        return fail("gap roots shell: dispatch (stubs)");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&h25_dispatch, &error, &bad)) {
        fprintf(stderr, "seal: %s at 0x%08lx\n", error ? error : "?", (unsigned long)bad);
        return fail("gap roots shell: dispatch seal");
    }
    static const struct { uint32_t va; es2_sim_service_fn fn; } rows[14] = {
        {0x00496802u, h25_s_fopen},   {0x0049684au, h25_s_fread},  {0x00496838u, h25_s_fclose}, {0x004967c6u, h25_s_open},
        {0x0049677eu, h25_s_write},   {0x00496874u, h25_s_close},  {0x004669b4u, h25_s_display}, {0x004650d0u, h25_s_timer},
        {0x00453824u, h25_s_holder},  {0x0047ad40u, h25_s_stream}, {0x0047ad78u, h25_s_destroy}, {0x004774f0u, h25_s_new},
        {0x004539a0u, h25_s_group},   {0x004967fcu, h25_s_time},
    };
    for (unsigned i = 0; i < 14u; ++i) {
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&h25_dispatch, rows[i].va);
        if (!e || e->kind != ES2_SIM_SERVICE) {
            fprintf(stderr, "row 0x%08lx\n", (unsigned long)rows[i].va);
            return fail("gap roots shell: a callee row is missing");
        }
        e->service = rows[i].fn;
        e->service_user = NULL;
    }
    const uint32_t mine[8] = {0x0045effcu, 0x0045f070u, 0x0045ceb8u, 0x00479c54u, 0x00479dd0u, 0x00479e48u, 0x00479e8cu, 0x00479f24u};
    for (unsigned i = 0; i < 8u; ++i) {
        const es2_sim_entry *e = es2_sim_dispatch_find(&h25_dispatch, mine[i]);
        if (!e || e->kind != ES2_SIM_SERVICE) return fail("gap roots shell: a row is missing");
    }
    return 0;
}

/* ---- the seeded state */
static uint32_t h25_seed;
static uint32_t h25_rand(uint32_t n) {
    h25_seed = h25_seed * 1103515245u + 12345u;
    return (h25_seed >> 8) % n;
}
static void h25_string(uint32_t a, uint32_t max) {
    static const char set[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789\\:._- %";
    uint32_t n = h25_rand(max);
    for (uint32_t i = 0; i < n; ++i) h25_put8(a + i, (uint8_t)set[h25_rand(sizeof(set) - 1u)]);
    h25_put8(a + n, 0);
}
static void h25_build(void) {
    h25_string(0x004d27bcu, 0x30u);
    h25_string(0x004d280cu, 0x30u);
    h25_string(0x006c56dcu, 0x40u);
    h25_put32(0x004a1610u, h25_rand(3u) ? (h25_rand(2u) ? 1u : h25_rand(0x10000u) << 16 | 1u) : 0u);
    h25_put32(0x006c582cu, h25_rand(3u) ? h25_rand(4u) : h25_rand(0x10000u) << 16 | h25_rand(0x10000u));
    for (uint32_t i = 0; i < 4u; ++i) h25_string(H25_FILES + 0x40u * i, 0x3fu);
    for (uint32_t i = 0; i < 4u; ++i) h25_string(H25_TEXTS + 0x80u * i, 0x7fu);
    for (uint32_t i = 0; i < 16u; ++i) h25_string(H25_NAMES + 0x10u * i, 0xfu);
    uint32_t nodes = h25_rand(4u);
    h25_put32(0x006c56d8u, nodes ? H25_NODES : 0u);
    for (uint32_t i = 0; i < 3u; ++i) {
        h25_put32(H25_NODES + 8u * i, i + 1u < nodes ? H25_NODES + 8u * (i + 1u) : 0u);
        h25_put32(H25_NODES + 8u * i + 4u, (uint32_t)(uintptr_t)h25_o_notify);
    }
    h25_put32(H25_VT + 0x18u, (uint32_t)(uintptr_t)h25_o_read);
    h25_put8(0x0049f032u, (uint8_t)(h25_rand(2u) ? 0u : 1u + h25_rand(0xffu)));
    uint32_t read = h25_rand(2u);
    h25_put8(0x0049f033u, (uint8_t)(read ? 1u + h25_rand(0xffu) : 0u));
    uint32_t count = read ? (h25_rand(3u) ? 1u : 2u + h25_rand(19u)) : h25_rand(0x10000u);
    h25_put16(0x004d26f0u, (uint16_t)count);
    for (uint32_t i = 0; i < 20u; ++i) h25_put32(0x004d26f4u + 4u * i, H25_NAMES + 0x10u * h25_rand(16u));
    h25_put16(0x0049f034u, (uint16_t)h25_rand(0x10000u));
    h25_put32(0x004d26e0u, h25_rand(0x10000u) << 16 | h25_rand(0x10000u));
    h25_put32(0x004d26e4u, h25_rand(0x10000u) << 16 | h25_rand(0x10000u));
    h25_put32(0x004d26e8u, h25_rand(0x10000u) << 16 | h25_rand(0x10000u));
    h25_put32(0x004d3608u, 0);
}

/* ---- the calls */
enum { H25_LANG, H25_NAME, H25_DEMO, H25_NOTIFY, H25_TEXT, H25_PATH, H25_LOG, H25_REPORT, H25_FATAL, H25_OPS };
static const struct { uint32_t va; unsigned args; const char *name; } h25_ops[H25_OPS] = {
    {0x0045effcu, 0, "help language"}, {0x0045f070u, 0, "help name"},  {0x0045ceb8u, 0, "demo pick"},
    {0x00479c54u, 2, "notify"},        {0x00479dd0u, 5, "report text"}, {0x00479e48u, 0, "log path"},
    {0x00479e8cu, 1, "log"},           {0x00479f24u, 5, "report"},      {0x00479c78u, 6, "fatal report"},
};
typedef struct h25_op { unsigned op; uint32_t a[6], in[3]; } h25_op;
static uint32_t h25_severity(void) {
    uint32_t r = random32() % 10u;
    return r < 8u ? r : random32();
}
static uint32_t h25_text_arg(void) { return random32() % 3u ? H25_TEXTS + 0x80u * (random32() % 4u) : 0u; }
static void h25_args(h25_op *o) {
    memset(o->a, 0, sizeof(o->a));
    uint32_t file = H25_FILES + 0x40u * (random32() % 4u), line = random32() % 3u ? random32() % 2000u : random32();
    switch (o->op) {
    case H25_NOTIFY:
        o->a[0] = random32();
        o->a[1] = h25_severity();
        break;
    case H25_TEXT:
        o->a[0] = H25_BUF;
        o->a[1] = random32() % 7u;
        o->a[2] = file;
        o->a[3] = line;
        o->a[4] = h25_text_arg();
        break;
    case H25_LOG:
        o->a[0] = random32() % 4u ? H25_TEXTS + 0x80u * (random32() % 4u) : H25_FILES;
        break;
    case H25_REPORT:
        o->a[0] = h25_severity();
        o->a[1] = random32();
        o->a[2] = file;
        o->a[3] = line;
        o->a[4] = o->a[0] == 2u ? H25_TEXTS + 0x80u * (random32() % 4u) : h25_text_arg();
        break;
    case H25_FATAL:
        o->a[0] = random32();
        o->a[1] = h25_severity();
        o->a[2] = random32();
        o->a[3] = file;
        o->a[4] = line;
        o->a[5] = o->a[1] == 2u ? H25_TEXTS + 0x80u * (random32() % 4u) : h25_text_arg();
        break;
    default:
        break;
    }
    for (unsigned k = 0; k < 3u; ++k) o->in[k] = random32();
}
static void h25_cover_severity(uint32_t sev) {
    h25_cover(sev < 2u ? H25C_SEV_LOW : sev > 6u ? H25C_SEV_HIGH : H25C_SEV_2 + (sev - 2u));
}
static void h25_cover_op(const h25_op *o) {
    switch (o->op) {
    case H25_NAME: h25_cover(H25C_NAME); break;
    case H25_DEMO:
        h25_cover(h25_get8(0x0049f032u) ? H25C_DEMO_BUILT : H25C_DEMO_BUILD);
        if (!h25_get8(0x0049f033u)) h25_cover(H25C_DEMO_READ);
        break;
    case H25_NOTIFY: h25_cover(h25_get32(0x006c56d8u) ? H25C_NOTIFY_NODES : H25C_NOTIFY_EMPTY); break;
    case H25_TEXT: h25_cover(o->a[4] ? H25C_TEXT : H25C_TEXT_NULL); break;
    case H25_PATH: h25_cover(H25C_PATH); break;
    case H25_LOG: h25_cover(h25_get32(0x004a1610u) ? H25C_RULE : H25C_NO_RULE); break;
    case H25_REPORT: h25_cover_severity(o->a[0]); break;
    case H25_FATAL:
        h25_cover(o->a[1] >= 4u && o->a[1] <= 6u ? H25C_FATAL_END : H25C_FATAL_PLAIN);
        h25_cover_severity(o->a[1]);
        break;
    default: break;
    }
}
/* After a demo pick on the original side: which answer it gave. */
static void h25_cover_demo(void) {
    if (!h25_get8(0x0049f033u)) { h25_cover(H25C_DEMO_NONE); return; }
    h25_cover((int16_t)(uint16_t)h25_get32(0x004d26f0u) == 1 ? H25C_DEMO_ONE : H25C_DEMO_TIME);
}

static void h25_side_begin(unsigned side, uint32_t seed, uint32_t answers) {
    lc_reset(side, 47, 0x01000000u, 1);
    lc_loop.dispatch = &h25_dispatch;
    h25_side = side;
    h25_hash[side] = 0xcbf29ce484222325ull;
    h25_count[side] = 0;
    h25_len[side] = 0;
    h25_text[side][0] = 0;
    h25_seen_box = lc_loop.platform.message_box;
    h25_seen_quit = lc_loop.platform.post_quit;
    h25_answer_seed = answers;
    h25_seed = seed;
    h25_build();
}
static unsigned h25_calls[H25_OPS];
static int h25_case(unsigned k) {
    unsigned ops = 1u + random32() % 6u;
    static h25_op list[6];
    uint32_t seed = random32(), answers = random32();
    static uint32_t reg[2][6][3];
    static int named[6];
    for (unsigned i = 0; i < ops; ++i) {
        list[i].op = random32() % H25_OPS;
        h25_args(&list[i]);
    }
    for (unsigned side = 0; side < 2u; ++side) {
        h25_side_begin(side, seed, answers);
        for (unsigned i = 0; i < ops && !h25_bad && !lc_bad && !su_bad; ++i) {
            h25_op *o = &list[i];
            unsigned n = h25_ops[o->op].args;
            if (side == 0) {
                h25_cover_op(o);
                h25_call((uint32_t)(uintptr_t)(mapped + (h25_ops[o->op].va - 0x400000u)), o->a, n, o->in, reg[0][i]);
                ++h25_calls[o->op];
                named[i] = o->op == H25_DEMO && h25_get8(0x0049f033u);
                if (o->op == H25_DEMO) h25_cover_demo();
            } else {
                es2_sim_result r;
                memset(&r, 0, sizeof(r));
                if (es2_sim_call(&h25_dispatch, h25_ops[o->op].va, o->a, n ? n : 1u, o->in, &r)) h25_bad = 1;
                h25_flush();
                reg[1][i][0] = r.eax; reg[1][i][1] = r.edx; reg[1][i][2] = r.ecx;
            }
        }
        if (h25_bad || lc_bad || su_bad) { fprintf(stderr, "FAIL: gap roots shell case %u: a call failed (side %u)\n", k, side); return 1; }
        su_take(side);
    }
    for (unsigned i = 0; i < ops; ++i) {
        unsigned op = list[i].op;
        int edx = op == H25_LANG || (op == H25_DEMO && named[i]);
        int ecx = edx || op == H25_NAME;
        if (reg[0][i][0] != reg[1][i][0] || (edx && reg[0][i][1] != reg[1][i][1]) || (ecx && reg[0][i][2] != reg[1][i][2])) {
            fprintf(stderr, "FAIL: gap roots shell case %u: %s (call %u) EAX %08lx / %08lx, EDX %08lx / %08lx, ECX %08lx / %08lx\n", k,
                    h25_ops[op].name, i, (unsigned long)reg[0][i][0], (unsigned long)reg[1][i][0], (unsigned long)reg[0][i][1],
                    (unsigned long)reg[1][i][1], (unsigned long)reg[0][i][2], (unsigned long)reg[1][i][2]);
            fprintf(stderr, "  arguments %lx %lx %lx %lx %lx %lx\n  original: %s\n  own:      %s\n", (unsigned long)list[i].a[0],
                    (unsigned long)list[i].a[1], (unsigned long)list[i].a[2], (unsigned long)list[i].a[3], (unsigned long)list[i].a[4],
                    (unsigned long)list[i].a[5], h25_text[0], h25_text[1]);
            return 1;
        }
    }
    if (su_compare("gap roots shell", k)) {
        fprintf(stderr, "  calls:");
        for (unsigned i = 0; i < ops; ++i) fprintf(stderr, " %s", h25_ops[list[i].op].name);
        fprintf(stderr, "\n");
        return 1;
    }
    if (h25_hash[0] != h25_hash[1] || h25_count[0] != h25_count[1]) {
        fprintf(stderr, "FAIL: gap roots shell case %u: stub calls differ (%u / %u)\n  original: %s\n  own:      %s\n", k, h25_count[0],
                h25_count[1], h25_text[0], h25_text[1]);
        return 1;
    }
    return 0;
}

static int gap_roots_shell_cases(void) {
    current_function = "gap roots shell";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits) return fail("gap roots shell: memory");
    su_pristine_take();
    static const struct { const char *dll, *name; void *stub; } stubs[] = {
        {"cw3220.dll", "_fopen", (void *)h25_o_fopen},   {"cw3220.dll", "_fread", (void *)h25_o_fread},
        {"cw3220.dll", "_fclose", (void *)h25_o_fclose}, {"cw3220.dll", "_open", (void *)h25_o_open},
        {"cw3220.dll", "_write", (void *)h25_o_write},   {"cw3220.dll", "_close", (void *)h25_o_close},
        {"cw3220.dll", "_time", (void *)h25_o_time},     {"cw3220.dll", "__exit", (void *)h25_o_exit},
        {"cw3220.dll", "_abort", (void *)h25_o_abort},   {"cw3220.dll", "_strcat", (void *)h25_o_strcat},
        {"cw3220.dll", "_sprintf", (void *)h25_o_sprintf}, {"USER32.dll", "MessageBoxA", (void *)h25_o_box},
        {"USER32.dll", "PostQuitMessage", (void *)h25_o_quit},
    };
    for (unsigned i = 0; i < sizeof(stubs) / sizeof(stubs[0]); ++i)
        if (!ir_patch_import(stubs[i].dll, stubs[i].name, stubs[i].stub)) {
            fprintf(stderr, "import %s!%s\n", stubs[i].dll, stubs[i].name);
            return fail("gap roots shell: import slot not found");
        }
    if (h25_patch(1)) return 1;
    if (h25_dispatch_setup()) return 1;
    int bad = 0;
    const unsigned cases = 2000;
    for (unsigned k = 0; !bad && k < cases; ++k) {
        current_case = k; current_case_valid = 1;
        bad = h25_case(k);
    }
    if (!bad) {
        printf("PASS: gap roots shell: %u cases identical\n", cases);
        printf("gap roots shell calls:");
        for (unsigned i = 0; i < H25_OPS; ++i) printf(" %s %u%s", h25_ops[i].name, h25_calls[i], i + 1u < H25_OPS ? "," : "\n");
        printf("gap roots shell coverage:");
        for (unsigned i = 0; i < H25C_COUNT; ++i) printf(" %s %u%s", h25_cv_names[i], h25_cv[i], i + 1u < H25C_COUNT ? "," : "\n");
        for (unsigned i = 0; i < H25C_COUNT; ++i)
            if (!h25_cv[i]) { fprintf(stderr, "FAIL: gap roots shell: path '%s' never taken\n", h25_cv_names[i]); bad = 1; }
    }
    current_case_valid = 0;
    h25_patch(0);
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
