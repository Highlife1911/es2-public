/* The INI reader against GetPrivateProfileStringW/IntW and
 * GetPrivateProfileStringA: every case is written to a fresh temp file in
 * each encoding and line ending, and every query compares the result and the
 * returned length with the API's. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "es2/platform/ini.h"
#include "es2/platform/os.h"

static int failures, compared;
static wchar_t dir[MAX_PATH];
static unsigned file_number;

#define Z10 "0000000000"
#define Z100 Z10 Z10 Z10 Z10 Z10 Z10 Z10 Z10 Z10 Z10

/* The cases: text in Latin-1 with '\n' line ends. */
static const char *const cases[] = {
    /* every key the code reads, plain */
    "[trace]\nwatch_count=2\nwatch0=0x004b1234 4 tick\nwatch1=6cc400 2\nheap_trace=D:\\out\\heap.ndjson\n"
    "[time]\nvirtual_time_step_ms=20\nvirtual_time_start_ms=1000\nvirtual_time_stall_limit=50000\nvirtual_time_intra_frame=1\n"
    "[game]\ncommand_line=dbsim.exe -X2 -R0\narguments=-eggplant -X1 -G\n"
    "[snapshot]\nafter_load=1\npath=D:\\snap\\after.bin\nextra=0x400000 0x1000\nmax_bytes_mib=512\n"
    "[input]\nmode=replay\njournal=D:\\journals\\c1_08.ndjson\nmessage_trace=1\n"
    "[run]\nframe_limit=4000\ntime=820454400\n"
    "[sim]\ncommand=\"C:\\Program Files\\es2\\es2sim.exe\" --headless\noutput=D:\\out\\sim.log\n",
    /* case, spaces, tabs */
    "[TRACE]\n  Watch_Count  =   3  \n\tWATCH0\t=\t0x10 1\t\n[ Time ]\nvirtual_time_step_ms = 16\n"
    "[run]   \n FRAME_LIMIT=12\n  time  =  \t 77 \t\n[Input]\nMODE = Replay\n",
    /* duplicate keys and duplicate sections */
    "[run]\nframe_limit=1\nframe_limit=2\ntime=5\n[input]\nmode=off\n[run]\nframe_limit=3\ntime=9\nextra=4\n",
    /* defaults: empty values, keys without '=', missing keys */
    "[run]\nframe_limit=\ntime\n[trace]\nwatch_count= \nheap_trace=\n[game]\narguments=   \n",
    /* signs and garbage for the Int reads */
    "[run]\nframe_limit=-1\ntime=+42\n[time]\nvirtual_time_step_ms=12abc\nvirtual_time_start_ms=abc\n"
    "virtual_time_stall_limit=0x1F\nvirtual_time_intra_frame=-0\n[trace]\nwatch_count=4294967295\n"
    "[snapshot]\nafter_load=0b101\nmax_bytes_mib=0o17\n[input]\nmessage_trace= - 5\n",
    "[run]\nframe_limit=99999999999\ntime=--3\n[time]\nvirtual_time_step_ms=0X10\nvirtual_time_start_ms=0x\n"
    "virtual_time_stall_limit=+-2\nvirtual_time_intra_frame=1.5\n[trace]\nwatch_count=000000000000000000000000000000000012\n"
    "[snapshot]\nafter_load=09\nmax_bytes_mib=-2147483648\n[input]\nmessage_trace=0b2\n",
    /* comments */
    "; leading comment\n[run]\n; frame_limit=5\nframe_limit=6 ; trailing\n;time=1\n# time=2\ntime=3\n"
    "[input]\nmode=replay;x\njournal=a;b\n;[trace]\n[trace]\nwatch_count=1 # c\n",
    /* quotes */
    "[game]\ncommand_line=\"quoted\"\narguments='single'\n[sim]\ncommand=\"a\" \"b\"\noutput=\"unbalanced\n"
    "[input]\nmode=\"\"\njournal=\"\n[snapshot]\npath=\"x\"y\"\nextra='\"'\n[run]\ntime=\"12\"\nframe_limit='7'\n",
    /* section edge cases */
    "frame_limit=1\n[run\nframe_limit=2\n[run]x\nframe_limit=3\n[run] ]\nframe_limit=4\n[]\ntime=1\n[run]\nframe_limit=5\n",
    "[run]]\nframe_limit=8\n[ run ]\nframe_limit=9\n[[run]\ntime=6\n",
    /* long values for the truncation */
    "[trace]\nheap_trace=0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmn"
    "opqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789\n"
    "[game]\narguments=\"0123456789abcdefghij\"\ncommand_line='abcd'\n[input]\nmode=replay-and-more-than-16\n",
    /* Latin-1 text and an empty file */
    "[game]\narguments=-name M\xfcller \xe4\xf6\xdf \x80\x9f\n[sim]\noutput=C:\\Spi\xe9le\\log.txt\n",
    "",
    "\n\n   \n",
    /* a lone CR, form feed and EOF characters as blanks */
    "[run]\rframe_limit=10\r[input]\rmode=replay\n[time]\nvirtual_time_step_ms=\x0c" "15\x1a\n",
    /* integers longer than a small read buffer */
    "[run]\nframe_limit=" Z10 Z10 Z10 Z10 Z10 Z10 "5\ntime=" Z100 Z10 Z10 Z10 Z10 Z10 "6\n"
    "[time]\nvirtual_time_step_ms=" Z100 Z100 Z10 Z10 Z10 Z10 Z10 "7\n"
    "virtual_time_start_ms=" Z100 Z100 Z10 Z10 Z10 Z10 Z10 "8" Z10 "\n"
    "virtual_time_stall_limit=" Z100 Z100 Z100 Z100 Z100 Z100 Z100 Z100 Z100 Z100 Z100 "9\n"
    "[trace]\nwatch_count=" Z10 Z10 "1\n[snapshot]\nafter_load=" Z10 Z10 Z10 "\n"
    "max_bytes_mib=" Z10 Z10 Z10 Z10 Z10 Z10 Z10 Z10 Z10 Z10 Z10 Z10 Z10 "3\n",
    /* no final line end */
    "[run]\nframe_limit=11",
};

typedef struct query { const char *section, *key; } query;
static const query queries[] = {
    {"trace", "watch_count"}, {"trace", "watch0"}, {"trace", "watch1"}, {"trace", "heap_trace"},
    {"time", "virtual_time_step_ms"}, {"time", "virtual_time_start_ms"}, {"time", "virtual_time_stall_limit"},
    {"time", "virtual_time_intra_frame"}, {"game", "command_line"}, {"game", "arguments"},
    {"snapshot", "after_load"}, {"snapshot", "path"}, {"snapshot", "extra"}, {"snapshot", "max_bytes_mib"},
    {"input", "mode"}, {"input", "journal"}, {"input", "message_trace"}, {"run", "frame_limit"}, {"run", "time"},
    {"run", "extra"}, {"sim", "command"}, {"sim", "output"}, {"RUN", "Frame_Limit"}, {" run ", " time "},
    {"", "frame_limit"}, {"missing", "time"}, {"run", "missing"},
};
static const char *const fallbacks[] = {"", "off", "x  ", "  ", " ", "\"q\"", "a b \t"};
static const int32_t int_fallbacks[] = {0, -1, 256, 7};
static const uint32_t sizes[] = {1024, 260, 256, 32, 30, 16, 6, 5, 2, 1};

static void widen(const char *in, wchar_t *out) {
    while ((*out++ = (unsigned char)*in++) != 0) {
    }
}
static void widen16(const char *in, uint16_t *out) {
    while ((*out++ = (unsigned char)*in++) != 0) {
    }
}

enum { ENC_UTF16, ENC_ANSI, ENC_UTF8, ENC_COUNT };
static const char *const enc_name[] = {"utf16", "ansi", "utf8"};

/* The file bytes of a case in an encoding and line ending. */
static size_t encode(const char *text, int enc, int crlf, uint8_t *out) {
    size_t n = 0;
    if (enc == ENC_UTF16) { out[n++] = 0xff; out[n++] = 0xfe; }
    if (enc == ENC_UTF8) { out[n++] = 0xef; out[n++] = 0xbb; out[n++] = 0xbf; }
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        unsigned units[2] = {*p, 0}, count = 1;
        if (*p == '\n' && crlf) { units[0] = '\r'; units[1] = '\n'; count = 2; }
        for (unsigned u = 0; u < count; ++u) {
            unsigned c = units[u];
            if (enc == ENC_UTF16) {
                /* Latin-1 as the code page shows it: the same text in each
                 * encoding. */
                wchar_t w = (wchar_t)c;
                if (c >= 0x80) {
                    char b = (char)c;
                    MultiByteToWideChar(1252, 0, &b, 1, &w, 1);
                }
                out[n++] = (uint8_t)w;
                out[n++] = (uint8_t)(w >> 8);
            } else if (enc == ENC_UTF8 && c >= 0x80) {
                wchar_t w = (wchar_t)c;
                char b = (char)c;
                MultiByteToWideChar(1252, 0, &b, 1, &w, 1);
                char u8[4];
                int k = WideCharToMultiByte(CP_UTF8, 0, &w, 1, u8, 4, NULL, NULL);
                for (int i = 0; i < k; ++i) out[n++] = (uint8_t)u8[i];
            } else {
                out[n++] = (uint8_t)c;
            }
        }
    }
    return n;
}

static void write_case(const wchar_t *path, const uint8_t *bytes, size_t n) {
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written = 0;
    if (h == INVALID_HANDLE_VALUE || !WriteFile(h, bytes, (DWORD)n, &written, NULL) || written != n) {
        printf("FAIL: cannot write %ls\n", path);
        exit(1);
    }
    CloseHandle(h);
}

/* One line per case, file and query; the rest of its failures are counted. */
static int shown;
static int fail(const char *what, unsigned c, int enc, int crlf, const query *q, const char *fallback, uint32_t size) {
    static const query *last_q;
    static unsigned last_c = ~0u;
    static int last_enc, last_crlf;
    ++failures;
    if (last_q == q && last_c == c && last_enc == enc && last_crlf == crlf) return 0;
    last_q = q;
    last_c = c;
    last_enc = enc;
    last_crlf = crlf;
    if (++shown > 80) return 0;
    printf("FAIL %s: case %u %s %s [%s] %s fallback \"%s\" size %lu\n", what, c, enc_name[enc], crlf ? "crlf" : "lf",
           q->section, q->key, fallback, (unsigned long)size);
    return 1;
}

static void compare_file(unsigned c, int enc, int crlf, const wchar_t *path) {
    static uint16_t file16[MAX_PATH];
    for (size_t i = 0; (file16[i] = path[i]) != 0; ++i) {
    }
    for (size_t qi = 0; qi < sizeof(queries) / sizeof(queries[0]); ++qi) {
        const query *q = &queries[qi];
        wchar_t section[64], key[64];
        uint16_t section16[64], key16[64];
        widen(q->section, section);
        widen(q->key, key);
        widen16(q->section, section16);
        widen16(q->key, key16);
        for (size_t fi = 0; fi < sizeof(fallbacks) / sizeof(fallbacks[0]); ++fi) {
            wchar_t fallback[16];
            uint16_t fallback16[16];
            widen(fallbacks[fi], fallback);
            widen16(fallbacks[fi], fallback16);
            for (size_t si = 0; si < sizeof(sizes) / sizeof(sizes[0]); ++si) {
                uint32_t size = sizes[si];
                wchar_t want[1024];
                uint16_t got[1024];
                wmemset(want, 0x5a5a, 1024);
                for (unsigned i = 0; i < 1024; ++i) got[i] = 0x5a5a;
                DWORD n = GetPrivateProfileStringW(section, key, fallback, want, size, path);
                uint32_t m = es2_ini_file_string(file16, section16, key16, fallback16, got, size);
                int same = n == m && (size == 0 || (DWORD)want[n] == got[m]);
                for (DWORD i = 0; same && i < n; ++i) same = (uint16_t)want[i] == got[i];
                ++compared;
                if (!same) {
                    if (fail("string", c, enc, crlf, q, fallbacks[fi], size)) printf("  api %lu [%.*ls] ini %lu\n", (unsigned long)n, (int)n, want, (unsigned long)m);
                }
                /* The A function on the ANSI files (the launcher's reads). It
                 * faults on an empty section name, which no caller passes. */
                if (enc == ENC_ANSI && q->section[0]) {
                    char path_a[MAX_PATH], want_a[1024], got_a[1024];
                    WideCharToMultiByte(CP_ACP, 0, path, -1, path_a, MAX_PATH, NULL, NULL);
                    memset(want_a, 0x5a, sizeof want_a);
                    memset(got_a, 0x5a, sizeof got_a);
                    if (getenv("INI_TEST_TRACE")) printf("A %s %s %s %lu\n", q->section, q->key, fallbacks[fi], (unsigned long)size);
                    DWORD na = GetPrivateProfileStringA(q->section, q->key, fallbacks[fi], want_a, size, path_a);
                    if (getenv("INI_TEST_TRACE")) printf("A api %lu\n", (unsigned long)na);
                    uint32_t ma = es2_ini_file_string_ansi(path_a, q->section, q->key, fallbacks[fi], got_a, size);
                    ++compared;
                    if (na != ma || (size && memcmp(want_a, got_a, na + 1))) {
                        if (fail("string A", c, enc, crlf, q, fallbacks[fi], size)) printf("  api %lu [%.*s] ini %lu [%.*s]\n", (unsigned long)na, (int)na, want_a, (unsigned long)ma, (int)ma, got_a);
                    }
                }
            }
        }
        for (size_t ii = 0; ii < sizeof(int_fallbacks) / sizeof(int_fallbacks[0]); ++ii) {
            UINT want = GetPrivateProfileIntW(section, key, int_fallbacks[ii], path);
            uint32_t got = es2_ini_file_int(file16, section16, key16, int_fallbacks[ii]);
            ++compared;
            if (want != got) {
                if (fail("int", c, enc, crlf, q, "", 0)) printf("  api %lu ini %lu (fallback %ld)\n", (unsigned long)want, (unsigned long)got, (long)int_fallbacks[ii]);
            }
        }
    }
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *tmp = getenv("TEST_TMPDIR");
    if (!tmp || !MultiByteToWideChar(CP_ACP, 0, tmp, -1, dir, MAX_PATH)) {
        DWORD n = GetTempPathW(MAX_PATH, dir);
        if (!n || n >= MAX_PATH) return 1;
    }
    for (wchar_t *p = dir; *p; ++p)
        if (*p == '/') *p = '\\';
    static uint8_t bytes[1 << 16];
    /* ini.c reads ANSI as Windows-1252; another code page only for ASCII. */
    int cp1252 = GetACP() == 1252;
    if (!cp1252) printf("note: ANSI code page %u, the ANSI files with high bytes are skipped\n", (unsigned)GetACP());
    for (unsigned c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c) {
        int high = 0;
        for (const unsigned char *p = (const unsigned char *)cases[c]; *p; ++p) high |= *p >= 0x80;
        for (int enc = 0; enc < ENC_COUNT; ++enc) {
            if (enc == ENC_ANSI && high && !cp1252) continue;
            for (int crlf = 0; crlf < 2; ++crlf) {
                wchar_t path[MAX_PATH];
                swprintf(path, MAX_PATH, L"%ls\\ini_%u_%lu.ini", dir, file_number++, (unsigned long)GetCurrentProcessId());
                size_t n = encode(cases[c], enc, crlf, bytes);
                write_case(path, bytes, n);
                if (getenv("INI_TEST_TRACE")) printf("case %u %d %d\n", c, enc, crlf);
                compare_file(c, enc, crlf, path);
                DeleteFileW(path);
            }
        }
    }
    /* A missing file and a missing directory. */
    {
        wchar_t path[MAX_PATH];
        swprintf(path, MAX_PATH, L"%ls\\absent_%lu.ini", dir, (unsigned long)GetCurrentProcessId());
        compare_file(1000, ENC_ANSI, 0, path);
        swprintf(path, MAX_PATH, L"%ls\\absent_dir_%lu\\x.ini", dir, (unsigned long)GetCurrentProcessId());
        compare_file(1001, ENC_ANSI, 0, path);
    }
    printf("%s: %d comparisons, %d failures\n", failures ? "FAIL" : "PASS", compared, failures);
    return failures != 0 || compared == 0;
}
