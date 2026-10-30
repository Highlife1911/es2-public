/* Own-code fixture for proxy/call_probe.h (the call probe). An own
 * function shaped like the entry of 0x4911c0 (`enter 0,0; push esi`) is the
 * probed function, an own word stands for the frame counter and another for
 * the table pointer [0x6b7b68]; two synthetic heap blocks carry a header word
 * and known bytes. No original game code runs. Modes:
 *   off        no [call_probe] section, or enabled=0: nothing allocated, no
 *              file, no byte of the function changed
 *   fire       the probe fires only in the configured frames, only when the
 *              condition holds, once per frame and pointer, and dumps the
 *              right bytes; the function sees the same registers, flags,
 *              LastError, x87 control word, result and stack below ESP as
 *              without the probe; past the last frame the bytes are original
 *   limit      max_dumps ends the probe and restores the bytes
 *   refuse     malformed configurations are refused with their reason; a
 *              function that does not start with a push, or whose first bytes
 *              hold a relative call, is refused with every byte unchanged;
 *              an existing report is refused; the refused record
 *   reference  ES2_DATA_ROOT's DBSIM.EXE: 0x4911c0 decodes to enter+push (5
 *              bytes, the inventory's kind), its only caller 0x476b8f passes
 *              (tw << 5) + [0x6b7b68] as the second argument, and the frame
 *              counter is the word the tick-log site increments */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ES2_CP_NO_PROCESS_ENTRY
#include "../call_probe.h"
#include "../probe_entry_inventory.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s (%s)\n", __LINE__, #c, cp_error); return 1; } } while (0)

/* ---- the probed function and its caller ---- */
__attribute__((used)) static uint32_t fx_in[11], fx_result, fx_bad, fx_esp_call;
__attribute__((used)) static uint16_t fx_cw_orig, fx_cw_set = 0x0e7f;
__asm__(
    ".text\n.p2align 4\n"
    /* int fx_target(a, b, c): records its state after the two entry
     * instructions, returns a + b + c. */
    "_fx_target:\n"
    "    enter $0, $0\n"
    "    pushl %esi\n"
    "    movl %eax, _fx_in\n    movl %ecx, _fx_in+4\n    movl %edx, _fx_in+8\n    movl %ebx, _fx_in+12\n"
    "    movl %esi, _fx_in+16\n    movl %edi, _fx_in+20\n    movl %esp, _fx_in+24\n    movl 0(%ebp), %eax\n    movl %eax, _fx_in+28\n"
    "    pushfl\n    popl _fx_in+32\n"
    "    movl %fs:0x34, %eax\n    movl %eax, _fx_in+36\n"
    "    fnstcw _fx_in+40\n"
    "    movl 8(%ebp), %eax\n    addl 12(%ebp), %eax\n    addl 16(%ebp), %eax\n"
    "    popl %esi\n"
    "    leave\n"
    "    ret\n"
    /* Refused shapes: no push first; a relative call inside the five bytes. */
    "_fx_plain:\n"
    "    .byte 0x33, 0xc0\n    movl $1, %eax\n    ret\n"   /* xor eax,eax in the inventory's form 33 c0 */
    "_fx_calls:\n"
    "    pushl %ebp\n    call _fx_plain\n    popl %ebp\n    ret\n"
    /* cdecl uint32_t fx_call(a, b, c): known registers, every arithmetic flag set, LastError, x87
     * control word and a pattern below ESP; one call of fx_target; then the
     * pattern from ESP-256 up to ESP-17 is checked (ESP-16..-4 are the
     * function's own pushes: return address, EBP, ESI, its flags). */
    "_fx_call:\n"
    "    pushl %ebp\n    movl %esp, %ebp\n    pushl %ebx\n    pushl %esi\n    pushl %edi\n"
    "    pushl 16(%ebp)\n    pushl 12(%ebp)\n    pushl 8(%ebp)\n"
    "    leal -256(%esp), %edi\n    movl $64, %ecx\n    movl $0x5a5a5a5a, %eax\n    cld\n    rep stosl\n"
    "    fnstcw _fx_cw_orig\n    fldcw _fx_cw_set\n"
    "    movl $0x1234abcd, %eax\n    movl %eax, %fs:0x34\n"
    "    movl $0xa0a0a0a0, %eax\n    movl $0xb0b0b0b0, %ebx\n    movl $0xc0c0c0c0, %ecx\n    movl $0xd0d0d0d0, %edx\n"
    "    movl $0x51515151, %esi\n    movl $0xd1d1d1d1, %edi\n"
    "    movl %esp, _fx_esp_call\n"
    "    pushl $0x8d5\n    popfl\n"     /* OF SF ZF AF PF CF set, DF clear */
    "    call _fx_target\n"
    "    movl %eax, _fx_result\n"
    "    fldcw _fx_cw_orig\n"
    "    movl $0, _fx_bad\n    leal -256(%esp), %esi\n    movl $60, %ecx\n"
    "1:  cmpl $0x5a5a5a5a, (%esi)\n    je 2f\n    incl _fx_bad\n"
    "2:  addl $4, %esi\n    decl %ecx\n    jnz 1b\n"
    "    movl _fx_result, %eax\n"
    "    addl $12, %esp\n"
    "    popl %edi\n    popl %esi\n    popl %ebx\n    popl %ebp\n"
    "    ret\n");
extern void fx_target(void) __asm__("_fx_target");
extern void fx_plain(void) __asm__("_fx_plain");
extern void fx_calls(void) __asm__("_fx_calls");
extern uint32_t fx_call(uint32_t a, uint32_t b, uint32_t c) __asm__("_fx_call");

/* ---- synthetic state ---- */
static volatile uint32_t fx_frame;
static volatile uint32_t fx_table;                     /* stands for [0x6b7b68] */
static BYTE fx_block_a[0x80], fx_block_b[0x80];      /* header word at +0, table at +4 */
static WCHAR tmp_dir[MAX_PATH];

static void put32(BYTE *p, uint32_t v) { memcpy(p, &v, 4); }
static uint32_t addr(const volatile void *p) { return (uint32_t)(uintptr_t)p; }
static uint32_t table_of(BYTE *block) { return addr(block + 4); }

static BOOL temp_directory(void) {
    const WCHAR *t = _wgetenv(L"TEST_TMPDIR");
    DWORD n = t ? (DWORD)wcslen(t) : GetTempPathW(MAX_PATH, tmp_dir);
    if (!n || n >= MAX_PATH - 64) return FALSE;
    if (t) wcscpy(tmp_dir, t);
    else if (tmp_dir[n - 1] == L'\\') tmp_dir[n - 1] = 0;
    return TRUE;
}
/* An INI file with `body` (ANSI) at <tmp>/<name>.ini; returns its path. */
static const WCHAR *write_ini(const WCHAR *name, const char *body) {
    static WCHAR path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%ls\\%ls.ini", tmp_dir, name);
    FILE *f = _wfopen(path, L"wb");
    if (!f) return NULL;
    fputs(body, f);
    fclose(f);
    return path;
}
static char *read_text(const WCHAR *path) {
    FILE *f = _wfopen(path, L"rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long size = ftell(f); rewind(f);
    char *text = calloc(1, (size_t)size + 1);
    if (text && fread(text, 1, (size_t)size, f) != (size_t)size) { free(text); text = NULL; }
    fclose(f);
    return text;
}
/* The n-th line (from 0) of text into out, without CRLF. */
static BOOL line_of(const char *text, unsigned n, char *out, size_t size) {
    for (; n && text; --n) { text = strchr(text, '\n'); if (text) ++text; }
    if (!text || !*text) return FALSE;
    size_t length = strcspn(text, "\r\n");
    if (length >= size) return FALSE;
    memcpy(out, text, length); out[length] = 0;
    return TRUE;
}
static unsigned line_count(const char *text) {
    unsigned n = 0;
    for (; *text; ++text) n += *text == '\n';
    return n;
}
static void hex_of(const BYTE *p, unsigned length, char *out) {
    static const char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < length; ++i) { out[2 * i] = digits[p[i] >> 4]; out[2 * i + 1] = digits[p[i] & 15]; }
    out[2 * length] = 0;
}
static void reset_probe(void) { cp_state = CPS_OFF; cp_restored = FALSE; cp_site = NULL; }

static uint32_t baseline[11], baseline_result, baseline_esp;
/* One call with the table entry tw; FALSE when anything the function saw
 * differs from the unprobed baseline (taken with tw 2). */
static BOOL call_same(unsigned tw) {
    uint32_t result = fx_call(0x1000u, fx_table + (tw << 5), 0x30u);
    if (fx_bad || result != 0x1030u + fx_table + (tw << 5) || fx_in[6] - fx_esp_call != baseline[6] - baseline_esp) {
        fprintf(stderr, "call tw %u: stack pattern %lu bytes changed, result 0x%08lx, esp 0x%08lx (baseline 0x%08lx)\n", tw,
                (unsigned long)fx_bad, (unsigned long)result, (unsigned long)fx_esp_call, (unsigned long)baseline_esp);
        return FALSE;
    }
    for (unsigned i = 0; i < 11; ++i)   /* [6] ESP checked relative to the call, [7] the caller's EBP: both depend on the C caller's depth */
        if (i != 6 && i != 7 && fx_in[i] != baseline[i]) {
            fprintf(stderr, "call tw %u: field %u 0x%08lx, baseline 0x%08lx\n", tw, i, (unsigned long)fx_in[i], (unsigned long)baseline[i]);
            return FALSE;
        }
    return TRUE;
}
static char config_text[1024];
static const char *probe_config(const char *frames, const char *extra, const WCHAR *report) {
    char report_a[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, report, -1, report_a, MAX_PATH, NULL, NULL);
    snprintf(config_text, sizeof(config_text),
             "[call_probe]\r\nenabled=1\r\nva=0x%08lx\r\nframes=%s\r\nwhen=[esp+8]-[0x%08lx] == 0x40 && [[0x%08lx]-4]==0x51\r\n"
             "pointer=[0x%08lx]\r\noffset=-4\r\nlength=0x64\r\nreport=%s\r\n%s",
             (unsigned long)addr((const void *)fx_target), frames, (unsigned long)addr(&fx_table), (unsigned long)addr(&fx_table),
             (unsigned long)addr(&fx_table), report_a, extra);
    return config_text;
}
static int setup(void) {
    if (!temp_directory()) return 1;
    for (unsigned i = 0; i < 0x80; ++i) { fx_block_a[i] = (BYTE)(i * 7 + 1); fx_block_b[i] = (BYTE)(0xff - i); }
    put32(fx_block_a, 0x51u); put32(fx_block_b, 0x51u);
    fx_table = table_of(fx_block_a);
    fx_frame = 0;
    fx_call(0x1000u, fx_table + 0x40u, 0x30u);
    memcpy(baseline, fx_in, sizeof(baseline)); baseline_result = fx_result; baseline_esp = fx_esp_call;
    if (fx_bad || baseline_result != 0x1070u + fx_table || (baseline[8] & 0xcd5u) != 0x8d5u || baseline[9] != 0x1234abcdu || (baseline[10] & 0xffffu) != 0x0e7fu) return 1;
    return 0;
}

/* ---- modes ---- */
static int off_case(void) {
    CHECK(!setup());
    BYTE before[16];
    memcpy(before, (const void *)fx_target, 16);
    cp_config c;
    const WCHAR *ini = write_ini(L"none", "[presentation]\r\ncapture=1\r\n");
    CHECK(ini && cp_configure(ini, &c) == 0);
    ini = write_ini(L"zero", "[call_probe]\r\nenabled=0\r\nva=junk\r\nlength=0\r\n");
    CHECK(ini && cp_configure(ini, &c) == 0);
    CHECK(cp_state == CPS_OFF && !cp_private_base && !cp_tail && cp_file == INVALID_HANDLE_VALUE);
    CHECK(!memcmp(before, (const void *)fx_target, 16));
    CHECK(call_same(2));
    puts("PASS off: no section and enabled=0 leave the function, the allocations and the files untouched");
    return 0;
}

static int fire_case(void) {
    CHECK(!setup());
    BYTE before[16];
    memcpy(before, (const void *)fx_target, 16);
    WCHAR report[MAX_PATH];
    swprintf(report, MAX_PATH, L"%ls\\fire.call-probe.ndjson", tmp_dir);
    const WCHAR *ini = write_ini(L"fire", probe_config("5, 7-8", "max_dumps=16\r\n", report));
    cp_config c;
    CHECK(ini && cp_configure(ini, &c) == 1);
    CHECK(c.range_count == 2 && c.last_frame == 8 && c.test_count == 2 && c.offset == -4 && c.length == 0x64 && c.max_dumps == 16);
    CHECK(cp_install(&c, addr(&fx_frame), "fixture", EP_ENTER));
    CHECK(cp_relocated == 5 && ((const BYTE *)fx_target)[0] == 0xe9 && !memcmp(before + 5, (const BYTE *)fx_target + 5, 11));
    fx_frame = 4; CHECK(call_same(2));                        /* before the range */
    fx_frame = 5; CHECK(call_same(2));                        /* dump: A, call 1 */
    CHECK(call_same(2));                                      /* same pointer: duplicate */
    CHECK(call_same(1));                                      /* tw 1: condition false */
    put32(fx_block_a, 0x50u); CHECK(call_same(2)); put32(fx_block_a, 0x51u);   /* header 0x50: false */
    fx_table = table_of(fx_block_b); CHECK(call_same(2));     /* dump: B, call 5 */
    fx_frame = 6; CHECK(call_same(2));                        /* between the ranges */
    fx_frame = 7; fx_table = table_of(fx_block_a); CHECK(call_same(2));   /* dump: A again, new frame */
    fx_table = 0x10u;                                         /* unreadable: read fault, no dump */
    { uint32_t r = fx_call(0x1000u, 0x50u, 0x30u); CHECK(r == 0x1080u && !fx_bad); }
    fx_table = table_of(fx_block_a);
    CHECK(((const BYTE *)fx_target)[0] == 0xe9);
    fx_frame = 9; CHECK(call_same(2));                        /* past the range: end, restored */
    CHECK(!memcmp(before, (const void *)fx_target, 16) && cp_state == CPS_DONE);
    CHECK(call_same(2));                                      /* original bytes run */
    char *text = read_text(report), line[1024], expect[1024], hex[2 * 0x64 + 1];
    CHECK(text && line_count(text) == 5);
    CHECK(line_of(text, 0, line, sizeof(line)) && strstr(line, "\"event\":\"start\"") && strstr(line, "\"relocated\":5") &&
          strstr(line, "\"frames\":\"5, 7-8\"") && strstr(line, "\"last_frame\":8"));
    const struct { unsigned frame, call; BYTE *block; } dumps[3] = {{5, 1, fx_block_a}, {5, 5, fx_block_b}, {7, 1, fx_block_a}};
    for (unsigned i = 0; i < 3; ++i) {
        CHECK(line_of(text, 1 + i, line, sizeof(line)));
        hex_of(dumps[i].block, 0x64, hex);
        snprintf(expect, sizeof(expect), "{\"event\":\"dump\",\"frame\":%u,\"call\":%u,\"pointer\":\"0x%08lx\",\"from\":\"0x%08lx\",\"offset\":-4,\"length\":100,\"unreadable\":0,",
                 dumps[i].frame, dumps[i].call, (unsigned long)table_of(dumps[i].block), (unsigned long)addr(dumps[i].block));
        CHECK(!strncmp(line, expect, strlen(expect)));
        snprintf(expect, sizeof(expect), "\"bytes\":\"%s\"}", hex);
        CHECK(strstr(line, expect));
        /* stack: the return address, then the three arguments */
        snprintf(expect, sizeof(expect), "\"0x%08lx\",\"0x%08lx\",\"0x%08lx\"]", 0x1000ul, (unsigned long)(table_of(dumps[i].block) + 0x40u), 0x30ul);
        CHECK(strstr(line, expect));
    }
    CHECK(line_of(text, 4, line, sizeof(line)));
    CHECK(!strcmp(line, "{\"event\":\"end\",\"reason\":\"frame_range_done\",\"calls\":10,\"in_range\":7,\"matched\":4,\"dumps\":3,"
                        "\"duplicates\":1,\"seen_overflow\":0,\"read_faults\":1,\"restored\":true}"));
    free(text);
    puts("PASS fire: frames 5 and 7-8 only, the condition, once per frame and pointer, the bytes, the function's view unchanged, "
         "bytes original after the range");
    return 0;
}

static int limit_case(void) {
    CHECK(!setup());
    BYTE before[16];
    memcpy(before, (const void *)fx_target, 16);
    WCHAR report[MAX_PATH];
    swprintf(report, MAX_PATH, L"%ls\\limit.call-probe.ndjson", tmp_dir);
    const WCHAR *ini = write_ini(L"limit", probe_config("1-100", "max_dumps=2\r\n", report));
    cp_config c;
    CHECK(ini && cp_configure(ini, &c) == 1 && cp_install(&c, addr(&fx_frame), "", EP_KINDS));
    fx_frame = 1;
    fx_table = table_of(fx_block_a); CHECK(call_same(2));
    fx_table = table_of(fx_block_b); CHECK(call_same(2));
    CHECK(!memcmp(before, (const void *)fx_target, 16) && cp_state == CPS_DONE);
    fx_table = table_of(fx_block_a); fx_frame = 2; CHECK(call_same(2));
    char *text = read_text(report), line[1024];
    CHECK(text && line_count(text) == 4 && line_of(text, 3, line, sizeof(line)) &&
          strstr(line, "\"reason\":\"max_dumps\"") && strstr(line, "\"dumps\":2,") && strstr(line, "\"restored\":true"));
    free(text);
    puts("PASS limit: max_dumps=2 ends the probe after two dumps and restores the bytes");
    return 0;
}

static int refuse_case(void) {
    CHECK(!setup());
    static const struct { const char *body, *reason; } bad[] = {
        {"enabled=yes\r\n", "enabled must be 0 or 1"},
        {"enabled=1\r\nva=0x4911c0\r\n", "report: missing"},
        {"enabled=1\r\nreport=x\r\n", "va: missing"},
        {"enabled=1\r\nreport=x\r\nva=4911c0\r\n", "va: expected one 0x hex address"},
        {"enabled=1\r\nreport=x\r\nva=0x4911c0\r\n", "frames: empty"},
        {"enabled=1\r\nreport=x\r\nva=0x4911c0\r\nframes=653-652\r\n", "frames: a range ends before it starts"},
        {"enabled=1\r\nreport=x\r\nva=0x4911c0\r\nframes=652;653\r\n", "frames: expected ',' between entries"},
        {"enabled=1\r\nreport=x\r\nva=0x4911c0\r\nframes=0x28c\r\n", "frames: decimal numbers only"},
        {"enabled=1\r\nreport=x\r\nva=0x4911c0\r\nframes=652\r\nwhen=[esp+8 == 2\r\n", "when: expected ']'"},
        {"enabled=1\r\nreport=x\r\nva=0x4911c0\r\nframes=652\r\nwhen=[esp+8] = 2\r\n", "when: expected '==' or '!='"},
        {"enabled=1\r\nreport=x\r\nva=0x4911c0\r\nframes=652\r\nwhen=[esp+8]==2 & 1==1\r\n", "when: expected '&&' between tests"},
        {"enabled=1\r\nreport=x\r\nva=0x4911c0\r\nframes=652\r\nwhen=[esx+8]==2\r\n", "when: unknown register"},
        {"enabled=1\r\nreport=x\r\nva=0x4911c0\r\nframes=652\r\nwhen=0x1ffffffff==2\r\n", "when: bad number"},
        {"enabled=1\r\nreport=x\r\nva=0x4911c0\r\nframes=652\r\n", "pointer: empty"},
        {"enabled=1\r\nreport=x\r\nva=0x4911c0\r\nframes=652\r\npointer=[0x6b7b68] 4\r\n", "pointer: unexpected text"},
        {"enabled=1\r\nreport=x\r\nva=0x4911c0\r\nframes=652\r\npointer=[[[[[[[[[1]]]]]]]]]\r\n", "pointer: more than 8 nested reads"},
        {"enabled=1\r\nreport=x\r\nva=0x4911c0\r\nframes=652\r\npointer=[0x6b7b68]\r\noffset=-x\r\n", "offset: expected"},
        {"enabled=1\r\nreport=x\r\nva=0x4911c0\r\nframes=652\r\npointer=[0x6b7b68]\r\n", "length: expected 1..256"},
        {"enabled=1\r\nreport=x\r\nva=0x4911c0\r\nframes=652\r\npointer=[0x6b7b68]\r\nlength=257\r\n", "length: expected 1..256"},
        {"enabled=1\r\nreport=x\r\nva=0x4911c0\r\nframes=652\r\npointer=[0x6b7b68]\r\nlength=0x64\r\nmax_dumps=0\r\n", "max_dumps: expected 1..1024"},
    };
    char body[512];
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        snprintf(body, sizeof(body), "[call_probe]\r\n%s", bad[i].body);
        const WCHAR *ini = write_ini(L"bad", body);
        cp_config c;
        int r = ini ? cp_configure(ini, &c) : 0;
        if (r != -1 || !strstr(cp_error, bad[i].reason)) {
            fprintf(stderr, "FAIL bad configuration %u: result %d, reason '%s', expected '%s'\n", i, r, cp_error, bad[i].reason);
            return 1;
        }
    }
    /* A complete configuration parses (the base of the shape refusals). */
    WCHAR report[MAX_PATH];
    swprintf(report, MAX_PATH, L"%ls\\refuse.call-probe.ndjson", tmp_dir);
    const WCHAR *ini = write_ini(L"good", probe_config("652", "", report));
    cp_config c;
    CHECK(ini && cp_configure(ini, &c) == 1);
    BYTE plain[16], calls[16], target[16];
    memcpy(plain, (const void *)fx_plain, 16); memcpy(calls, (const void *)fx_calls, 16); memcpy(target, (const void *)fx_target, 16);
    c.va = addr((const void *)fx_plain);
    CHECK(!cp_install(&c, addr(&fx_frame), "", EP_KINDS) && strstr(cp_error, "the first instruction does not push"));
    reset_probe();
    c.va = addr((const void *)fx_calls);
    CHECK(!cp_install(&c, addr(&fx_frame), "", EP_KINDS) && strstr(cp_error, "the instruction at va+1 is not a relocatable entry kind"));
    reset_probe();
    c.va = addr((const void *)fx_target);
    CHECK(!cp_install(&c, addr(&fx_frame), "", EP_PUSH) && strstr(cp_error, "not the inventory's kind"));
    reset_probe();
    CHECK(!cp_install(&c, 0x10u, "", EP_ENTER) && strstr(cp_error, "frame counter not readable"));
    reset_probe();
    HANDLE f = CreateFileW(report, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(f != INVALID_HANDLE_VALUE); CloseHandle(f);
    CHECK(!cp_install(&c, addr(&fx_frame), "", EP_ENTER) && strstr(cp_error, "report: exists already"));
    CHECK(!memcmp(plain, (const void *)fx_plain, 16) && !memcmp(calls, (const void *)fx_calls, 16) && !memcmp(target, (const void *)fx_target, 16));
    CHECK(call_same(2));
    /* The refused record carries the reason. */
    WCHAR refused[MAX_PATH];
    swprintf(refused, MAX_PATH, L"%ls\\refused.call-probe.ndjson", tmp_dir);
    cp_fail("frames: a range ends before it starts", NULL);
    cp_write_refused(refused);
    char *text = read_text(refused);
    CHECK(text && !strcmp(text, "{\"event\":\"refused\",\"schema\":1,\"kind\":\"es2-call-probe\",\"reason\":\"frames: a range ends before it starts\"}\r\n"));
    free(text);
    printf("PASS refuse: %u malformed configurations with their reasons; no push first, a relative call, the inventory kind, "
           "the frame counter and an existing report refused with every byte unchanged; the refused record\n",
           (unsigned)(sizeof(bad) / sizeof(bad[0])));
    return 0;
}

/* ---- the reference file (ES2_DATA_ROOT) ---- */
static int reference_case(void) {
    const char *root = getenv("ES2_DATA_ROOT");
    if (!root || !*root) { puts("SKIP: ES2_DATA_ROOT unset"); return 0; }
    char path[1024];
    snprintf(path, sizeof(path), "%s/VER95/DBSIM.EXE", root);
    FILE *f = fopen(path, "rb");
    CHECK(f);
    fseek(f, 0, SEEK_END); long size = ftell(f); rewind(f);
    CHECK(size == 724512);
    BYTE *bytes = malloc((size_t)size);
    CHECK(bytes && fread(bytes, 1, (size_t)size, f) == (size_t)size);
    fclose(f);
    uint32_t pe = lc_u32(bytes + 0x3c), image_size = lc_u32(bytes + pe + 24 + 56);
    CHECK(lc_u32(bytes + pe + 24 + 28) == 0x400000u && image_size == 0x2d8000u);
    uint32_t headers = lc_u32(bytes + pe + 24 + 60);
    unsigned sections = bytes[pe + 6] | bytes[pe + 7] << 8, optional = bytes[pe + 20] | bytes[pe + 21] << 8;
    BYTE *map = calloc(1, image_size);
    CHECK(map);
    memcpy(map, bytes, headers);
    for (unsigned s = 0; s < sections; ++s) {
        const BYTE *h = bytes + pe + 24 + optional + 40 * s;
        uint32_t rva = lc_u32(h + 12), raw = lc_u32(h + 16), offset = lc_u32(h + 20);
        if (raw && rva < image_size) memcpy(map + rva, bytes + offset, raw < image_size - rva ? raw : image_size - rva);
    }
#define AT(va) (map + ((va) - 0x400000u))
    /* 0x4911c0: enter 0,0; push esi (5 bytes), the inventory's kind. */
    unsigned n = 0, first = EP_KINDS;
    CHECK(cp_decode(AT(0x4911c0u), &n, &first) && n == 5 && first == EP_ENTER);
    CHECK(!memcmp(AT(0x4911c0u), "\xc8\x00\x00\x00\x56", 5));
    BOOL listed = FALSE;
    for (size_t i = 0; i < sizeof(probe_entry_kinds) / sizeof(probe_entry_kinds[0]); ++i)
        if (probe_entry_kinds[i].va == 0x4911c0u) listed = probe_entry_kinds[i].kind == EP_ENTER;
    for (size_t i = 0; i < sizeof(probe_instructions) / sizeof(probe_instructions[0]); ++i)   /* the push table would make the proxy expect EP_PUSH */
        if (probe_instructions[i].va == 0x4911c0u) listed = FALSE;
    CHECK(listed);
    /* Its caller 0x476b7c..0x476b8f: movsx edx,di; shl edx,5; push ebx; add edx,[0x6b7b68]; push edx; push 0x6bb2e8; call 0x4911c0. */
    CHECK(!memcmp(AT(0x476b7cu), "\x0f\xbf\xd7\xc1\xe2\x05\x53\x03\x15\x68\x7b\x6b\x00\x52\x68\xe8\xb2\x6b\x00\xe8", 20));
    CHECK(0x476b8fu + 5u + lc_u32(AT(0x476b90u)) == 0x4911c0u);
    unsigned calls = 0;
    for (uint32_t va = 0x401000u; va + 5 <= 0x400000u + image_size; ++va)
        if (*AT(va) == 0xe8 && va + 5u + lc_u32(AT(va + 1)) == 0x4911c0u) ++calls;
    CHECK(calls == 1);
    /* The frame counter: the tick-log site increments [ebx+0xbf] (g_sim_ctx 0x4d24bc), the capture's key 0x4d257b. */
    CHECK(!memcmp(AT(0x45f4adu), "\xff\x83\xbf\x00\x00\x00", 6) && CP_PATCH_SIZE == 5);
#undef AT
    puts("PASS reference: 0x4911c0 is enter+push (5 bytes, inventory kind enter), its only rel32 caller 0x476b8f passes "
         "(tw << 5) + [0x6b7b68] as [esp+8], the tick-log site increments the counter at g_sim_ctx+0xbf");
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    if (!strcmp(argv[1], "off")) return off_case();
    if (!strcmp(argv[1], "fire")) return fire_case();
    if (!strcmp(argv[1], "limit")) return limit_case();
    if (!strcmp(argv[1], "refuse")) return refuse_case();
    if (!strcmp(argv[1], "reference")) return reference_case();
    return 2;
}
