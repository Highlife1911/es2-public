/* The call probe. A diagnosis measure: on entry of one configured
 * function of the original, in a configured range of frames and when a
 * configured condition holds, it writes a byte range relative to a pointer
 * read from memory into a small NDJSON report, once per frame and distinct
 * pointer value. Contract and use: docs/reference-game/own_process.md,
 * "the free camera's shape edge", section "Live request".
 *
 * Off unless the INI has [call_probe] enabled=1 (the INI of the frame
 * capture: ES2_PRESENTATION_CONFIG, else ES2_ORACLE_SPEED_CONFIG, else
 * es2-oracle.ini, as presenter.h). Without the key nothing at all is changed
 * (no allocation, no file, no byte of the image). DBSIM.EXE only.
 *
 * Keys of [call_probe] (every value ASCII):
 *   enabled=0|1
 *   va=0x4911c0           the function entry; an instruction start of the
 *                         hash-bound entry inventory (probe_entry_inventory.h)
 *   frames=652,653,709-715  frames (the counter 0x4d257b, the key of the
 *                         frame capture's records) as numbers or from-to
 *   when=<test> && <test> ...   each test `<expr> == <expr>` or `!=`;
 *                         empty: always
 *   pointer=<expr>        the pointer whose value keys the dumps
 *   offset=-4, length=0x64   the bytes [pointer+offset, +length), 1..256
 *   max_dumps=64          1..1024; the probe ends after this many dumps
 *   report=<path>|beside_tick_log  ("<[trace] tick_log>.call-probe.ndjson")
 * An <expr> is a sum and difference of numbers (decimal or 0x hex), the
 * registers at the entry (eax ecx edx ebx esp ebp esi edi; esp points at the
 * return address) and dword reads [<expr>]. A read that fails (the memory is
 * not readable; ReadProcessMemory, no exception) makes the test false and
 * is counted. A malformed configuration is refused with a reason (log event
 * call_probe_config_refused and, when the report path is usable, a
 * "refused" record with the reason).
 *
 * The hook. The first instructions at va (at least five bytes) are decoded
 * with the entry probe kinds (entry_probe_engine.h, ep_kind_size); every one
 * must be of a kind without a relative target or control transfer, and the
 * first must push (push r32, enter, push imm). They are copied into a tail
 * (the copy plus `jmp va+n`), and the five bytes at va become `jmp
 * cp_entry`. cp_entry saves the flags with one push at [ESP-4], the slot the
 * function's own first push overwrites before anything reads it, switches to
 * a private stack (TEB stack bounds set to it while the step runs), saves
 * every general register and the x87/SSE state, runs the step, restores
 * everything and enters the tail with ESP and all registers as the call left
 * them. Nothing else below ESP is written, and nothing of the original's
 * memory: the step only reads (ReadProcessMemory) and writes its own file.
 * A second thread arriving while the step runs goes straight to the tail.
 * Once the frame counter is past the last configured frame, or max_dumps is
 * reached, the step writes the "end" record and puts the five original
 * bytes back (the thread is in the stub at that point, so the restore is
 * safe), so the probe costs nothing after its range.
 *
 * Report (one JSON object per line, CRLF):
 *   {"event":"start","schema":1,"kind":"es2-call-probe",...}  the configuration
 *   {"event":"dump","frame":F,"call":C,"pointer":"0x..","from":"0x..",
 *    "offset":O,"length":L,"unreadable":U,"stack":[4 words at ESP],"bytes":"hex"}
 *    (C: the call's ordinal in its frame, from 1; unreadable bytes are "??")
 *   {"event":"end","reason":...,"calls":...,"in_range":...,"matched":...,
 *    "dumps":...,"duplicates":...,"seen_overflow":...,"read_faults":...,"restored":...}
 */
#ifndef ES2_CALL_PROBE_H
#define ES2_CALL_PROBE_H
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "entry_probe_engine.h"

#define CP_PATCH_SIZE 5u
#define CP_RELOCATE_MAX 16u
#define CP_MAX_OPS 48u
#define CP_MAX_TESTS 8u
#define CP_MAX_RANGES 16u
#define CP_MAX_LENGTH 256u
#define CP_MAX_DUMPS 1024u
#define CP_DEFAULT_DUMPS 64u
#define CP_SEEN_MAX 64u
#define CP_PRIVATE_STACK (256u * 1024u)
#define CP_ERROR_MAX 160u

enum { CP_OP_CONST, CP_OP_REG, CP_OP_LOAD, CP_OP_ADD, CP_OP_SUB };
typedef struct cp_op { uint8_t kind, reg; uint32_t value; } cp_op;
typedef struct cp_expr { unsigned count; cp_op ops[CP_MAX_OPS]; } cp_expr;
typedef struct cp_test { cp_expr left, right; BOOL equal; } cp_test;
typedef struct cp_config {
    uint32_t va;
    unsigned range_count;
    uint32_t from[CP_MAX_RANGES], to[CP_MAX_RANGES], last_frame;
    unsigned test_count;
    cp_test tests[CP_MAX_TESTS];
    cp_expr pointer;
    int32_t offset;
    uint32_t length, max_dumps;
    WCHAR report[MAX_PATH];
    char frames_text[128], when_text[256], pointer_text[128];
} cp_config;
enum { CPS_OFF = 0, CPS_INSTALLED = 1, CPS_DONE = 2, CPS_REFUSED = -1 };

static const char *const cp_register_names[8] = {"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"};
static char cp_error[CP_ERROR_MAX];

/* ---- configuration ------------------------------------------------------ */
static BOOL cp_fail(const char *format, const char *detail) {
    snprintf(cp_error, sizeof(cp_error), format, detail ? detail : "");
    return FALSE;
}
/* [call_probe] key as ASCII; FALSE when too long or not ASCII. */
static BOOL cp_get(const WCHAR *ini, const WCHAR *key, char *out, unsigned size) {
    WCHAR text[512];
    DWORD n = GetPrivateProfileStringW(L"call_probe", key, L"", text, 512, ini);
    if (n >= 511 || n >= size) return FALSE;
    for (DWORD i = 0; i <= n; ++i) {
        if (text[i] > 0x7e || (text[i] && text[i] < 0x20)) return FALSE;
        out[i] = (char)text[i];
    }
    return TRUE;
}
static const char *cp_skip(const char *p) { while (*p == ' ' || *p == '\t') ++p; return p; }
/* Decimal or 0x hexadecimal, at most 32 bits. */
static BOOL cp_number(const char **cursor, uint32_t *out) {
    const char *p = *cursor;
    uint64_t v = 0;
    unsigned digits = 0;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        for (;; ++p, ++digits) {
            unsigned d = *p >= '0' && *p <= '9' ? (unsigned)(*p - '0') : *p >= 'a' && *p <= 'f' ? (unsigned)(*p - 'a' + 10) :
                         *p >= 'A' && *p <= 'F' ? (unsigned)(*p - 'A' + 10) : 16u;
            if (d == 16) break;
            v = v * 16 + d;
            if (v > UINT32_MAX) return FALSE;
        }
    } else {
        for (; *p >= '0' && *p <= '9'; ++p, ++digits) {
            v = v * 10 + (unsigned)(*p - '0');
            if (v > UINT32_MAX) return FALSE;
        }
    }
    if (!digits) return FALSE;
    *cursor = p; *out = (uint32_t)v;
    return TRUE;
}
static BOOL cp_emit(cp_expr *e, uint8_t kind, uint8_t reg, uint32_t value) {
    if (e->count == CP_MAX_OPS) return cp_fail("expression longer than 48 operations", NULL);
    e->ops[e->count++] = (cp_op){kind, reg, value};
    return TRUE;
}
static BOOL cp_parse_expr(const char **cursor, cp_expr *e, unsigned depth, const char *what);
static BOOL cp_parse_term(const char **cursor, cp_expr *e, unsigned depth, const char *what) {
    const char *p = cp_skip(*cursor);
    uint32_t v;
    if (*p == '[') {
        if (depth >= 8) return cp_fail("%s: more than 8 nested reads", what);
        ++p;
        if (!cp_parse_expr(&p, e, depth + 1, what)) return FALSE;
        p = cp_skip(p);
        if (*p != ']') return cp_fail("%s: expected ']'", what);
        ++p;
        if (!cp_emit(e, CP_OP_LOAD, 0, 0)) return FALSE;
    } else if (*p >= '0' && *p <= '9') {
        if (!cp_number(&p, &v)) return cp_fail("%s: bad number (decimal or 0x hex, 32 bits)", what);
        if (!cp_emit(e, CP_OP_CONST, 0, v)) return FALSE;
    } else if (*p >= 'a' && *p <= 'z') {
        unsigned r = 0;
        while (r < 8 && (strncmp(p, cp_register_names[r], 3) || (p[3] >= 'a' && p[3] <= 'z') || (p[3] >= '0' && p[3] <= '9'))) ++r;
        if (r == 8) return cp_fail("%s: unknown register (eax ecx edx ebx esp ebp esi edi)", what);
        p += 3;
        if (!cp_emit(e, CP_OP_REG, (uint8_t)r, 0)) return FALSE;
    } else return cp_fail("%s: expected a number, a register or '['", what);
    *cursor = p;
    return TRUE;
}
static BOOL cp_parse_expr(const char **cursor, cp_expr *e, unsigned depth, const char *what) {
    const char *p = *cursor;
    if (!cp_parse_term(&p, e, depth, what)) return FALSE;
    for (;;) {
        p = cp_skip(p);
        if (*p != '+' && *p != '-') break;
        uint8_t op = *p == '+' ? CP_OP_ADD : CP_OP_SUB;
        ++p;
        if (!cp_parse_term(&p, e, depth, what) || !cp_emit(e, op, 0, 0)) return FALSE;
    }
    *cursor = p;
    return TRUE;
}
/* A whole expression (nothing may follow). */
static BOOL cp_parse_whole(const char *text, cp_expr *e, const char *what) {
    const char *p = text;
    e->count = 0;
    if (!*cp_skip(p)) return cp_fail("%s: empty", what);
    if (!cp_parse_expr(&p, e, 0, what)) return FALSE;
    if (*cp_skip(p)) return cp_fail("%s: unexpected text after the expression", what);
    return TRUE;
}
static BOOL cp_parse_when(const char *text, cp_config *c) {
    const char *p = cp_skip(text);
    c->test_count = 0;
    if (!*p) return TRUE;
    for (;;) {
        if (c->test_count == CP_MAX_TESTS) return cp_fail("when: more than 8 tests", NULL);
        cp_test *t = &c->tests[c->test_count++];
        t->left.count = t->right.count = 0;
        if (!cp_parse_expr(&p, &t->left, 0, "when")) return FALSE;
        p = cp_skip(p);
        if ((p[0] != '=' && p[0] != '!') || p[1] != '=') return cp_fail("when: expected '==' or '!=' after an expression", NULL);
        t->equal = p[0] == '=';
        p += 2;
        if (!cp_parse_expr(&p, &t->right, 0, "when")) return FALSE;
        p = cp_skip(p);
        if (!*p) return TRUE;
        if (p[0] != '&' || p[1] != '&') return cp_fail("when: expected '&&' between tests", NULL);
        p += 2;
    }
}
static BOOL cp_parse_frames(const char *text, cp_config *c) {
    const char *p = cp_skip(text);
    c->range_count = 0; c->last_frame = 0;
    if (!*p) return cp_fail("frames: empty (numbers or from-to, separated by commas)", NULL);
    for (;;) {
        uint32_t from, to;
        if (c->range_count == CP_MAX_RANGES) return cp_fail("frames: more than 16 entries", NULL);
        if (*p == '0' && (p[1] == 'x' || p[1] == 'X')) return cp_fail("frames: decimal numbers only", NULL);
        if (!cp_number(&p, &from)) return cp_fail("frames: expected a decimal frame number", NULL);
        to = from;
        if (*p == '-') {
            ++p;
            if (*p == '0' && (p[1] == 'x' || p[1] == 'X')) return cp_fail("frames: decimal numbers only", NULL);
            if (!cp_number(&p, &to)) return cp_fail("frames: expected a decimal frame number after '-'", NULL);
            if (to < from) return cp_fail("frames: a range ends before it starts", NULL);
        }
        c->from[c->range_count] = from; c->to[c->range_count] = to; ++c->range_count;
        if (to > c->last_frame) c->last_frame = to;
        p = cp_skip(p);
        if (!*p) return TRUE;
        if (*p != ',') return cp_fail("frames: expected ',' between entries", NULL);
        p = cp_skip(p + 1);
    }
}
static BOOL cp_parse_signed(const char *text, int32_t *out) {
    const char *p = cp_skip(text);
    BOOL negative = *p == '-';
    if (negative || *p == '+') ++p;
    uint32_t v;
    if (!cp_number(&p, &v) || *cp_skip(p) || v > (negative ? 0x80000000u : 0x7fffffffu)) return FALSE;
    *out = negative ? (int32_t)(0u - v) : (int32_t)v;
    return TRUE;
}
static BOOL cp_parse_unsigned(const char *text, uint32_t *out) {
    const char *p = cp_skip(text);
    return cp_number(&p, out) && !*cp_skip(p);
}
/* Returns 0 (off), 1 (configured) or -1 (refused; cp_error holds why). The
 * report path is taken as given ("beside_tick_log" is the caller's). */
static int cp_configure(const WCHAR *ini, cp_config *c) {
    char text[256];
    memset(c, 0, sizeof(*c));
    cp_error[0] = 0;
    if (!cp_get(ini, L"enabled", text, sizeof(text))) { cp_fail("enabled must be 0 or 1", NULL); return -1; }
    if (!text[0] || !strcmp(text, "0")) return 0;
    if (strcmp(text, "1")) { cp_fail("enabled must be 0 or 1", NULL); return -1; }
    DWORD n = GetPrivateProfileStringW(L"call_probe", L"report", L"", c->report, MAX_PATH, ini);
    if (!n || n >= MAX_PATH - 32) { c->report[0] = 0; cp_fail("report: missing or longer than MAX_PATH-32", NULL); return -1; }
    if (!cp_get(ini, L"va", text, sizeof(text)) || !text[0]) { cp_fail("va: missing (0x hex)", NULL); return -1; }
    { const char *p = cp_skip(text);
      if (p[0] != '0' || (p[1] != 'x' && p[1] != 'X') || !cp_number(&p, &c->va) || *cp_skip(p) || !c->va) { cp_fail("va: expected one 0x hex address", NULL); return -1; } }
    if (!cp_get(ini, L"frames", c->frames_text, sizeof(c->frames_text))) { cp_fail("frames: longer than 127 characters", NULL); return -1; }
    if (!cp_parse_frames(c->frames_text, c)) return -1;
    if (!cp_get(ini, L"when", c->when_text, sizeof(c->when_text))) { cp_fail("when: longer than 255 characters or not ASCII", NULL); return -1; }
    if (!cp_parse_when(c->when_text, c)) return -1;
    if (!cp_get(ini, L"pointer", c->pointer_text, sizeof(c->pointer_text))) { cp_fail("pointer: longer than 127 characters or not ASCII", NULL); return -1; }
    if (!cp_parse_whole(c->pointer_text, &c->pointer, "pointer")) return -1;
    if (!cp_get(ini, L"offset", text, sizeof(text))) { cp_fail("offset: not ASCII", NULL); return -1; }
    if (!text[0]) c->offset = 0;
    else if (!cp_parse_signed(text, &c->offset)) { cp_fail("offset: expected a signed decimal or 0x hex number", NULL); return -1; }
    if (!cp_get(ini, L"length", text, sizeof(text)) || !cp_parse_unsigned(text, &c->length) || !c->length || c->length > CP_MAX_LENGTH) {
        cp_fail("length: expected 1..256 bytes", NULL); return -1;
    }
    if (!cp_get(ini, L"max_dumps", text, sizeof(text))) { cp_fail("max_dumps: not ASCII", NULL); return -1; }
    if (!text[0]) c->max_dumps = CP_DEFAULT_DUMPS;
    else if (!cp_parse_unsigned(text, &c->max_dumps) || !c->max_dumps || c->max_dumps > CP_MAX_DUMPS) {
        cp_fail("max_dumps: expected 1..1024", NULL); return -1;
    }
    return 1;
}

/* ---- the probe ---------------------------------------------------------- */
typedef struct cp_counts {
    uint64_t calls, in_range, matched, dumps, duplicates, seen_overflow, read_faults;
} cp_counts;
static cp_config cp_active;
static cp_counts cp_stats;
static volatile LONG cp_state;
static BYTE *cp_site, cp_saved[CP_PATCH_SIZE], cp_jump[CP_PATCH_SIZE], *cp_tail;
static unsigned cp_relocated;
static uintptr_t cp_frame_counter;
static HANDLE cp_file = INVALID_HANDLE_VALUE;
static uint32_t cp_seen_frame = UINT32_MAX, cp_seen[CP_SEEN_MAX], cp_frame_calls;
static unsigned cp_seen_count;
static BOOL cp_restored;

/* Read by the assembly below. */
__attribute__((used)) static volatile LONG cp_busy;
__attribute__((used)) static uint32_t cp_entry_esp, cp_private_base, cp_private_top, cp_tail_target;
__attribute__((used)) static BYTE cp_fpu_state[512] __attribute__((aligned(16)));

__asm__(
    ".text\n.p2align 4\n"
    "_cp_entry:\n"
    "    pushfl\n"                      /* [ESP-4]: the function's first push overwrites it */
    "    lock btsl $0, _cp_busy\n"
    "    jc 1f\n"
    "    movl %esp, _cp_entry_esp\n"
    "    movl _cp_private_top, %esp\n"
    "    pushal\n"
    "    pushl %fs:4\n"
    "    pushl %fs:8\n"
    "    movl _cp_private_top, %eax\n"
    "    movl %eax, %fs:4\n"
    "    movl _cp_private_base, %eax\n"
    "    movl %eax, %fs:8\n"
    "    fxsave _cp_fpu_state\n"
    "    cld\n"
    "    subl $8, %esp\n"               /* 40 bytes pushed + 8: ESP is 16-aligned at the call */
    "    call _cp_run\n"
    "    addl $8, %esp\n"
    "    fxrstor _cp_fpu_state\n"
    "    popl %fs:8\n"
    "    popl %fs:4\n"
    "    popal\n"
    "    movl _cp_entry_esp, %esp\n"
    "    movl $0, _cp_busy\n"
    "1:  popfl\n"
    "    jmp *_cp_tail_target\n");
extern void cp_entry(void) __asm__("_cp_entry");

static BOOL cp_read(uint32_t address, void *out, uint32_t size) {
    SIZE_T got = 0;
    return address && size <= UINT32_MAX - address &&
        ReadProcessMemory(GetCurrentProcess(), (const void *)(uintptr_t)address, out, size, &got) && got == size;
}
static BOOL cp_eval(const cp_expr *e, const uint32_t regs[8], uint32_t *out) {
    uint32_t stack[CP_MAX_OPS];
    unsigned top = 0;
    for (unsigned i = 0; i < e->count; ++i) {
        const cp_op *o = &e->ops[i];
        switch (o->kind) {
        case CP_OP_CONST: stack[top++] = o->value; break;
        case CP_OP_REG: stack[top++] = regs[o->reg & 7]; break;
        case CP_OP_LOAD: if (!top || !cp_read(stack[top - 1], &stack[top - 1], 4)) return FALSE; break;
        case CP_OP_ADD: if (top < 2) return FALSE; --top; stack[top - 1] += stack[top]; break;
        case CP_OP_SUB: if (top < 2) return FALSE; --top; stack[top - 1] -= stack[top]; break;
        default: return FALSE;
        }
    }
    if (top != 1) return FALSE;
    *out = stack[0];
    return TRUE;
}
static void cp_write(const char *text, int n) {
    DWORD written = 0;
    if (cp_file != INVALID_HANDLE_VALUE && n > 0) WriteFile(cp_file, text, (DWORD)n, &written, NULL);
}
static BOOL cp_restore(void) {
    if (cp_restored) return TRUE;
    if (!cp_site || memcmp(cp_site, cp_jump, CP_PATCH_SIZE)) return FALSE;
    DWORD protection, ignored;
    if (!VirtualProtect(cp_site, CP_PATCH_SIZE, PAGE_EXECUTE_READWRITE, &protection)) return FALSE;
    memcpy(cp_site, cp_saved, CP_PATCH_SIZE);
    BOOL ok = FlushInstructionCache(GetCurrentProcess(), cp_site, CP_PATCH_SIZE);
    if (!VirtualProtect(cp_site, CP_PATCH_SIZE, protection, &ignored)) ok = FALSE;
    cp_restored = TRUE;
    return ok;
}
/* The end record; restores the original bytes. Once. */
static void cp_finish(const char *reason) {
    if (InterlockedCompareExchange(&cp_state, CPS_DONE, CPS_INSTALLED) != CPS_INSTALLED) return;
    BOOL restored = cp_restore();
    char line[512];
    int n = snprintf(line, sizeof(line),
        "{\"event\":\"end\",\"reason\":\"%s\",\"calls\":%llu,\"in_range\":%llu,\"matched\":%llu,\"dumps\":%llu,"
        "\"duplicates\":%llu,\"seen_overflow\":%llu,\"read_faults\":%llu,\"restored\":%s}\r\n",
        reason, (unsigned long long)cp_stats.calls, (unsigned long long)cp_stats.in_range, (unsigned long long)cp_stats.matched,
        (unsigned long long)cp_stats.dumps, (unsigned long long)cp_stats.duplicates, (unsigned long long)cp_stats.seen_overflow,
        (unsigned long long)cp_stats.read_faults, restored ? "true" : "false");
    cp_write(line, n);
    if (cp_file != INVALID_HANDLE_VALUE) { FlushFileBuffers(cp_file); CloseHandle(cp_file); cp_file = INVALID_HANDLE_VALUE; }
}
static void cp_dump(uint32_t frame, uint32_t pointer, const uint32_t regs[8]) {
    static const char hex[] = "0123456789abcdef";
    const cp_config *c = &cp_active;
    uint32_t from = pointer + (uint32_t)c->offset, unreadable = 0, stack[4] = {0, 0, 0, 0};
    BYTE bytes[CP_MAX_LENGTH];
    BOOL whole = cp_read(from, bytes, c->length);
    char text[2 * CP_MAX_LENGTH + 1];
    for (uint32_t i = 0; i < c->length; ++i) {
        if (!whole && !cp_read(from + i, &bytes[i], 1)) { text[2 * i] = text[2 * i + 1] = '?'; ++unreadable; continue; }
        text[2 * i] = hex[bytes[i] >> 4]; text[2 * i + 1] = hex[bytes[i] & 15];
    }
    text[2 * c->length] = 0;
    for (unsigned i = 0; i < 4; ++i) cp_read(regs[4] + 4u * i, &stack[i], 4);
    char line[2 * CP_MAX_LENGTH + 384];
    int n = snprintf(line, sizeof(line),
        "{\"event\":\"dump\",\"frame\":%lu,\"call\":%lu,\"pointer\":\"0x%08lx\",\"from\":\"0x%08lx\",\"offset\":%ld,\"length\":%lu,"
        "\"unreadable\":%lu,\"stack\":[\"0x%08lx\",\"0x%08lx\",\"0x%08lx\",\"0x%08lx\"],\"bytes\":\"%s\"}\r\n",
        (unsigned long)frame, (unsigned long)cp_frame_calls, (unsigned long)pointer, (unsigned long)from, (long)c->offset,
        (unsigned long)c->length, (unsigned long)unreadable, (unsigned long)stack[0], (unsigned long)stack[1],
        (unsigned long)stack[2], (unsigned long)stack[3], text);
    cp_write(line, n);
}
static BOOL cp_frame_wanted(uint32_t frame) {
    for (unsigned i = 0; i < cp_active.range_count; ++i) if (frame >= cp_active.from[i] && frame <= cp_active.to[i]) return TRUE;
    return FALSE;
}
/* One call of the probed function (registers at its entry). */
static void cp_step(const uint32_t regs[8]) {
    if (cp_state != CPS_INSTALLED) return;
    ++cp_stats.calls;
    uint32_t frame;
    if (!cp_read((uint32_t)cp_frame_counter, &frame, 4)) { ++cp_stats.read_faults; return; }
    if (frame != cp_seen_frame) { cp_seen_frame = frame; cp_seen_count = 0; cp_frame_calls = 0; }
    ++cp_frame_calls;
    if (frame > cp_active.last_frame) { cp_finish("frame_range_done"); return; }
    if (!cp_frame_wanted(frame)) return;
    ++cp_stats.in_range;
    for (unsigned i = 0; i < cp_active.test_count; ++i) {
        uint32_t left, right;
        const cp_test *t = &cp_active.tests[i];
        if (!cp_eval(&t->left, regs, &left) || !cp_eval(&t->right, regs, &right)) { ++cp_stats.read_faults; return; }
        if ((left == right) != t->equal) return;
    }
    ++cp_stats.matched;
    uint32_t pointer;
    if (!cp_eval(&cp_active.pointer, regs, &pointer)) { ++cp_stats.read_faults; return; }
    for (unsigned i = 0; i < cp_seen_count; ++i) if (cp_seen[i] == pointer) { ++cp_stats.duplicates; return; }
    if (cp_seen_count == CP_SEEN_MAX) { ++cp_stats.seen_overflow; return; }
    cp_seen[cp_seen_count++] = pointer;
    cp_dump(frame, pointer, regs);
    if (++cp_stats.dumps >= cp_active.max_dumps) cp_finish("max_dumps");
}
/* Runs on the private stack; pushal left EDI..EAX at cp_private_top-32. */
void __cdecl cp_run(void) __asm__("_cp_run") __attribute__((used));
void __cdecl cp_run(void) {
    DWORD error = GetLastError();
    const uint32_t *p = (const uint32_t *)(uintptr_t)(cp_private_top - 32u);
    const uint32_t regs[8] = {p[7], p[6], p[5], p[4], cp_entry_esp + 4u, p[2], p[1], p[0]};
    cp_step(regs);
    SetLastError(error);
}

/* The kind of the instruction at code (EP_KINDS if none). */
static unsigned cp_kind_at(const BYTE *code) {
    for (unsigned k = 0; k < EP_KINDS; ++k) if (ep_kind_size(k, code)) return k;
    return EP_KINDS;
}
static BOOL cp_relocatable(unsigned kind) {
    return kind < EP_KINDS && kind != EP_CALL_REL32 && kind != EP_JMP_REL32 && kind != EP_JMP_ABS && kind != EP_RET;
}
static BOOL cp_pushes(unsigned kind) {
    return kind == EP_PUSH || kind == EP_ENTER || kind == EP_PUSH_IMM8 || kind == EP_PUSH_IMM32;
}
/* The whole instructions that cover the five patched bytes at code (n bytes,
 * at most CP_RELOCATE_MAX, readable); FALSE with cp_error when one is not a
 * relocatable kind or the first does not push. */
static BOOL cp_decode(const BYTE *code, unsigned *size, unsigned *first) {
    unsigned n = 0;
    *first = EP_KINDS;
    while (n < CP_PATCH_SIZE) {
        unsigned kind = cp_kind_at(code + n);
        char at[32];
        snprintf(at, sizeof(at), "va+%u", n);
        if (!cp_relocatable(kind)) return cp_fail("the instruction at %s is not a relocatable entry kind", at);
        if (!n) *first = kind;
        n += ep_kind_size(kind, code + n);
    }
    if (!cp_pushes(*first)) return cp_fail("the first instruction does not push (it must overwrite the flags word at ESP-4)", NULL);
    *size = n;
    return TRUE;
}
/* The refused record (only when the report can be created). */
static void cp_write_refused(const WCHAR *report) {
    if (!report || !report[0]) return;
    HANDLE f = CreateFileW(report, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    char line[CP_ERROR_MAX + 96];
    int n = snprintf(line, sizeof(line), "{\"event\":\"refused\",\"schema\":1,\"kind\":\"es2-call-probe\",\"reason\":\"%s\"}\r\n", cp_error);
    DWORD written;
    if (n > 0) WriteFile(f, line, (DWORD)n, &written, NULL);
    CloseHandle(f);
}
/* Installs the configured probe. frame_counter: the dword address of the
 * frame counter; module_sha256: written into the start record (may be "").
 * expected_first_kind: the inventory's kind of va (EP_KINDS: not checked).
 * FALSE (cp_error says why) leaves every byte as it was. */
static BOOL cp_install(const cp_config *c, uintptr_t frame_counter, const char *module_sha256, unsigned expected_first_kind) {
    if (cp_state != CPS_OFF) return cp_fail("already installed or refused", NULL);
    BYTE *code = (BYTE *)(uintptr_t)c->va;
    if (!lc_readable((uintptr_t)code, CP_RELOCATE_MAX + 8)) { cp_state = CPS_REFUSED; return cp_fail("va: not readable", NULL); }
    uint32_t probe;
    if (!frame_counter || !cp_read((uint32_t)frame_counter, &probe, 4)) { cp_state = CPS_REFUSED; return cp_fail("frame counter not readable", NULL); }
    unsigned n = 0, first = EP_KINDS;
    if (!cp_decode(code, &n, &first)) { cp_state = CPS_REFUSED; return FALSE; }
    if (expected_first_kind != EP_KINDS && first != expected_first_kind) { cp_state = CPS_REFUSED; return cp_fail("the first instruction is not the inventory's kind", NULL); }
    if (!cp_private_base) {
        BYTE *stack = VirtualAlloc(NULL, CP_PRIVATE_STACK, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!stack) { cp_state = CPS_REFUSED; return cp_fail("no private stack", NULL); }
        cp_private_base = (uint32_t)(uintptr_t)stack;
        cp_private_top = cp_private_base + CP_PRIVATE_STACK;
    }
    if (!cp_tail) {
        cp_tail = VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (!cp_tail) { cp_state = CPS_REFUSED; return cp_fail("no tail", NULL); }
    }
    cp_file = CreateFileW(c->report, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (cp_file == INVALID_HANDLE_VALUE) { cp_state = CPS_REFUSED; return cp_fail("report: exists already or cannot be created", NULL); }
    cp_active = *c;
    memset(&cp_stats, 0, sizeof(cp_stats));
    cp_seen_frame = UINT32_MAX; cp_seen_count = 0; cp_frame_calls = 0; cp_restored = FALSE;
    cp_frame_counter = frame_counter;
    cp_site = code; cp_relocated = n;
    memcpy(cp_saved, code, CP_PATCH_SIZE);
    memcpy(cp_tail, code, n);
    uint32_t back = (uint32_t)((uintptr_t)code + n - ((uintptr_t)cp_tail + n + 5));
    cp_tail[n] = 0xe9; memcpy(cp_tail + n + 1, &back, 4);
    FlushInstructionCache(GetCurrentProcess(), cp_tail, n + 5);
    cp_tail_target = (uint32_t)(uintptr_t)cp_tail;
    uint32_t rel = (uint32_t)((uintptr_t)cp_entry - ((uintptr_t)code + 5));
    cp_jump[0] = 0xe9; memcpy(cp_jump + 1, &rel, 4);
    char line[1024];
    int length = snprintf(line, sizeof(line),
        "{\"event\":\"start\",\"schema\":1,\"kind\":\"es2-call-probe\",\"module_sha256\":\"%s\",\"va\":\"0x%08lx\",\"relocated\":%u,"
        "\"frame_counter\":\"0x%08lx\",\"frames\":\"%s\",\"last_frame\":%lu,\"when\":\"%s\",\"pointer\":\"%s\",\"offset\":%ld,"
        "\"length\":%lu,\"max_dumps\":%lu,\"pid\":%lu}\r\n",
        module_sha256 ? module_sha256 : "", (unsigned long)c->va, n, (unsigned long)frame_counter, c->frames_text,
        (unsigned long)c->last_frame, c->when_text, c->pointer_text, (long)c->offset, (unsigned long)c->length,
        (unsigned long)c->max_dumps, (unsigned long)GetCurrentProcessId());
    cp_write(line, length);
    DWORD protection, ignored;
    cp_state = CPS_INSTALLED;   /* published before the bytes */
    MemoryBarrier();
    if (!VirtualProtect(code, CP_PATCH_SIZE, PAGE_EXECUTE_READWRITE, &protection)) {
        cp_state = CPS_REFUSED; CloseHandle(cp_file); cp_file = INVALID_HANDLE_VALUE; return cp_fail("va: not writable", NULL);
    }
    memcpy(code, cp_jump, CP_PATCH_SIZE);
    FlushInstructionCache(GetCurrentProcess(), code, CP_PATCH_SIZE);
    VirtualProtect(code, CP_PATCH_SIZE, protection, &ignored);
    return TRUE;
}

#ifndef ES2_CP_NO_PROCESS_ENTRY
/* ------------------------------------------------------------------------
 * Proxy part (media_proxy.c, after the hooks and entry probes are armed, in
 * initialize_forwarding): DBSIM.EXE, the reference file hash, the image at
 * 0x400000, va an instruction start of the hash-bound entry inventory. */
#define CP_FRAME_COUNTER_VA 0x004d257bu   /* g_sim_ctx 0x4d24bc + 0xbf (tick_log.h); the capture's frame key */
static cp_config cp_process_config;
static void install_call_probe(void) {
    WCHAR exe[MAX_PATH], ini[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);
    WCHAR *name = n && n < MAX_PATH ? wcsrchr(exe, L'\\') : NULL;
    if (!name || _wcsicmp(name + 1, L"DBSIM.EXE") || !pr_config_file(ini)) return;
    cp_config *c = &cp_process_config;
    int configured = cp_configure(ini, c);
    if (!configured) return;
    /* Resolved even for a refused configuration, so that the refused record
     * lands where the report would have been. */
    if (!_wcsicmp(c->report, L"beside_tick_log")) {
        n = config_path[0] ? GetPrivateProfileStringW(L"trace", L"tick_log", L"", c->report, MAX_PATH, config_path) : 0;
        if (!n || n + wcslen(L".call-probe.ndjson") >= MAX_PATH) {
            c->report[0] = 0;
            if (configured > 0) { cp_fail("report: beside_tick_log without [trace] tick_log", NULL); configured = -1; }
        } else wcscat(c->report, L".call-probe.ndjson");
    }
    if (configured < 0) { log_event("call_probe_config_refused"); cp_write_refused(c->report); return; }
    BYTE *image = (BYTE *)GetModuleHandleW(NULL);
    if (!rng_hook_reference_file(exe) || !es2_hook_image_valid(image, 0x400000, RNG_HOOK_IMAGE_SIZE) || (uintptr_t)image != 0x400000u) {
        cp_fail("the executable is not the reference DBSIM.EXE at 0x400000", NULL);
        log_event("call_probe_hash_refused"); cp_write_refused(c->report); return;
    }
    unsigned kind = c->va >= 0x400000u && c->va - 0x400000u < RNG_HOOK_IMAGE_SIZE - 32u ?
        (probe_opcode(c->va) ? (unsigned)EP_PUSH : probe_kind_of(c->va)) : (unsigned)EP_KINDS;
    if (kind == EP_KINDS) {
        cp_fail("va: not an instruction start of the entry inventory (probe_entry_inventory.h)", NULL);
        log_event("call_probe_va_refused"); cp_write_refused(c->report); return;
    }
    if (!cp_install(c, CP_FRAME_COUNTER_VA, LC_MODULE_HASH, kind)) { log_event("call_probe_refused"); cp_write_refused(c->report); return; }
    log_event("call_probe_installed");
}
/* DLL_PROCESS_DETACH: the end record, if the range did not end before. */
static void call_probe_shutdown(void) { cp_finish("detach"); }
#endif
#endif
