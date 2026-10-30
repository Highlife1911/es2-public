/* A fixed script of file and directory calls through es2/platform/os.h,
 * printed as a transcript: os_win32.c natively, os_wasm.c and the JS host
 * of the "es2os" imports as wasm (web/node/host_test.mjs compares the two).
 * Before each call the last error is set to a sentinel, so the transcript
 * also shows which calls set, clear or keep it.
 *
 *   os_calls BASE      the script, every "$" being BASE (an empty directory)
 *   os_calls --clocks  the clock and calendar values, one per line
 *
 * Output paths show BASE as "$". The names are valid 8.3 names, so no short
 * name enters FindFirstFile's matching. */
#include "es2/platform/os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SENTINEL 0xe5e5u

static const char *const script[] = {
    "mkdir $\\SUB",
    "mkdir $\\SUB",
    "mkdir $\\NONE\\X",
    "attr $\\SUB",
    "attr $\\sub\\\\",
    "attr $\\ABSENT.DAT",
    "attr $\\NONE\\ABSENT.DAT",
    "open a $\\Find1.dat w 1",
    "write a hello",
    "close a",
    "open b $\\FIND1.DAT w 1",
    "open c $/find1.DAT r 3",
    "read c 3",
    "read c 10",
    "read c 10",
    "seek c 1 0",
    "read c 2",
    "seek c -2 1",
    "read c 1",
    "seek c -1 2",
    "read c 5",
    "seek c -10 0",
    "seek c 0 1",
    "seek c 0 3",
    "size c",
    "type c",
    "write c x",
    "close c",
    "close c",
    "close invalid",
    "read invalid 1",
    "size invalid",
    "type invalid",
    "attr $\\find1.dat",
    "open d $\\f2.dat w 2",
    "write d abc",
    "seek d 4 2",
    "write d Z",
    "size d",
    "read d 1",
    "close d",
    "open e $\\F2.DAT r 3",
    "read e 20",
    "close e",
    "open f $\\f2.dat rw 2",
    "size f",
    "close f",
    "open g $\\f2.dat w 4",
    "close g",
    "open g $\\new4.dat w 4",
    "close g",
    "open g $\\f2.dat w 5",
    "size g",
    "close g",
    "open x $\\absent.dat w 5",
    "open x $\\absent.dat r 3",
    "open x $\\NONE\\absent.dat r 3",
    "open x $\\NONE\\absent.dat w 2",
    "open x $\\find1.dat\\x r 3",
    "open x $\\SUB r 3",
    "open x $\\SUB w 2",
    "open x $\\SUB w 1",
    "open x $\\find1.dat r 6",
    "open x $\\find1.dat r 0",
    "open g $\\noext w 1",
    "write g 1234567",
    "close g",
    "open g $\\ab.txt w 1",
    "close g",
    "open g $\\x.da w 1",
    "close g",
    "open g $\\SUB\\in.dat w 1",
    "close g",
    "openw g $\\Wide.dat w 1",
    "close g",
    "open s $\\noext r 3 0",
    "open t $\\noext r 3 1",
    "open t $\\noext r 3 3",
    "close s",
    "open u $\\noext w 3 1",
    "open u $\\noext w 3 3",
    "del $\\noext",
    "close t",
    "close u",
    "find f $\\*",
    "next f",
    "next f",
    "next f",
    "next f",
    "next f",
    "next f",
    "next f",
    "next f",
    "next f",
    "next f",
    "fclose f",
    "find f $\\*.*",
    "fclose f",
    "find f $\\*.DAT",
    "next f",
    "next f",
    "next f",
    "next f",
    "fclose f",
    "find f $\\find?.DAT",
    "next f",
    "fclose f",
    "find f $\\f?.dat",
    "next f",
    "fclose f",
    "find f $\\f???.dat",
    "fclose f",
    "find f $\\*.",
    "next f",
    "next f",
    "fclose f",
    "find f $\\*.d?",
    "next f",
    "fclose f",
    "find f $\\*.da?",
    "next f",
    "next f",
    "next f",
    "fclose f",
    "find f $\\??.*",
    "next f",
    "next f",
    "fclose f",
    "find f $\\NOEXT",
    "next f",
    "fclose f",
    "find f $\\*X*",
    "next f",
    "next f",
    "fclose f",
    "find x $\\none.*",
    "find x $\\NONE\\*",
    "find x $\\find1.dat\\*",
    "find x $\\SUB\\\\",
    "find f $\\SUB",
    "fclose f",
    "find f $\\sub\\*",
    "next f",
    "next f",
    "next f",
    "fclose f",
    "find f $\\*",
    "read f 1",
    "close f",
    "fclose invalid",
    "mtime $\\find1.dat",
    "mtime $\\absent.dat",
    "move $\\x.da $\\y.da",
    "move $\\y.da $\\Ab.TXT",
    "attr $\\y.da",
    "find f $\\ab.txt",
    "fclose f",
    "open g $\\ab.txt r 3",
    "read g 5",
    "close g",
    "move $\\absent.dat $\\z.dat",
    "move $\\ab.txt $\\NONE\\z.dat",
    "move $\\ab.txt $\\SUB",
    "open g $\\m1.dat w 1",
    "close g",
    "open h $\\m1.dat r 3",
    "move $\\m1.dat $\\m2.dat",
    "open g $\\m3.dat w 1",
    "close g",
    "move $\\m3.dat $\\m1.dat",
    "close h",
    "move $\\m1.dat $\\m2.dat",
    "move $\\m3.dat $\\M2.DAT",
    "attr $\\m3.dat",
    "open h $\\M2.dat r 3 7",
    "del $\\m2.dat",
    "attr $\\m2.dat",
    "open g $\\m2.dat r 3 7",
    "close h",
    "attr $\\m2.dat",
    "del $\\f2.dat",
    "del $\\f2.dat",
    "del $\\NONE\\f2.dat",
    "del $\\SUB",
    "cwd 260",
    "cwd 3",
    "full x.dat 260",
    "full SUB\\..\\x.dat 260",
    "full a/b\\\\c 260",
    "full sub\\ 260",
    "full . 260",
    "full .\\ 260",
    "full .. 260",
    "full x. 260",
    "full x.dat. 260",
    "full $\\SUB\\..\\..\\q 260",
    "full $\\a\\.\\b 260",
    "full x.dat 4",
    "open n nul w 3",
    "type n",
    "write n abc",
    "read n 4",
    "size n",
    "seek n 5 0",
    "close n",
    "open g SUB\\in.dat r 3",
    "close g",
    "open g sub/IN.DAT r 3",
    "close g",
    "attr sub",
    "find f sub\\in*",
    "fclose f",
    "open g $\\app.dat w 1",
    "write g ab",
    "close g",
    "open g $\\app.dat a 3",
    "write g cd",
    "seek g 0 0",
    "write g ef",
    "read g 1",
    "size g",
    "close g",
    "open g $\\app.dat r 3",
    "read g 10",
    "close g",
};

static char base[600];

/* "$" in a script token becomes BASE. */
static void expand(const char *in, char *out, size_t capacity) {
    size_t n = 0;
    for (; *in && n + 1 < capacity; in++) {
        if (*in == '$') {
            for (const char *b = base; *b && n + 1 < capacity; b++) out[n++] = *b;
        } else {
            out[n++] = *in;
        }
    }
    out[n] = 0;
}

/* Prints text with BASE (compared without case) as "$". */
static void put_path(const char *text) {
    size_t b = strlen(base);
    while (*text) {
        size_t i = 0;
        while (i < b && text[i]) {
            char x = text[i], y = base[i];
            if (x >= 'a' && x <= 'z') x = (char)(x - 32);
            if (y >= 'a' && y <= 'z') y = (char)(y - 32);
            if (x != y) break;
            i++;
        }
        if (b && i == b) {
            putchar('$');
            text += b;
        } else {
            putchar(*text++);
        }
    }
}

/* The script's handles by label; a failed open or find stores the invalid handle. */
static struct {
    char label[8];
    es2_os_handle h;
} handles[16];

static es2_os_handle *slot(const char *label) {
    for (int i = 0; i < 16; i++) {
        if (!strcmp(handles[i].label, label)) return &handles[i].h;
        if (!handles[i].label[0]) {
            snprintf(handles[i].label, sizeof handles[i].label, "%s", label);
            handles[i].h = ES2_OS_INVALID_HANDLE;
            return &handles[i].h;
        }
    }
    return NULL;
}

static es2_os_handle handle_of(const char *label) {
    es2_os_handle *h = strcmp(label, "invalid") ? slot(label) : NULL;
    return h ? *h : ES2_OS_INVALID_HANDLE;
}

static void new_handle(const char *label, es2_os_handle h) {
    es2_os_handle *at = slot(label);
    if (at) *at = h;
    printf(" %s", h == ES2_OS_INVALID_HANDLE ? "invalid" : "valid");
}

static uint32_t u32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* WIN32_FIND_DATAA: attributes, which of the three times are set, size, dwReserved0, name and
 * short name. dwReserved1 is not compared: Windows leaves arbitrary values there. */
static void put_find(const uint8_t *f) {
    int times = 0;
    for (int t = 0; t < 3; t++)
        if (u32(f + 4 + 8 * t) || u32(f + 8 + 8 * t)) times |= 1 << t;
    printf(" attr=%x times=%d size=%u:%u reserved=%u name=%.260s alt=%.14s", (unsigned)u32(f), times,
           (unsigned)u32(f + 28), (unsigned)u32(f + 32), (unsigned)u32(f + 36), (const char *)f + 44,
           (const char *)f + 304);
}

static uint32_t access_of(const char *token) {
    if (!strcmp(token, "r")) return ES2_OS_GENERIC_READ;
    if (!strcmp(token, "w")) return ES2_OS_GENERIC_WRITE;
    if (!strcmp(token, "rw")) return ES2_OS_GENERIC_READ | ES2_OS_GENERIC_WRITE;
    if (!strcmp(token, "a")) return ES2_OS_FILE_APPEND_DATA;
    return 0;
}

static void run(const char *line) {
    char copy[256], *tok[6] = {0};
    int n = 0;
    snprintf(copy, sizeof copy, "%s", line);
    for (char *t = strtok(copy, " "); t && n < 6; t = strtok(NULL, " ")) tok[n++] = t;
    const char *op = tok[0];
    /* open and find name their handle first */
    int at = !strcmp(op, "open") || !strcmp(op, "openw") || !strcmp(op, "find");
    char a[600] = "", b[600] = "";
    if (n > 1 + at) expand(tok[1 + at], a, sizeof a);
    if (n > 2 + at) expand(tok[2 + at], b, sizeof b);
    printf("%s ->", line);
    es2_os_set_last_error(SENTINEL);
    uint8_t find[ES2_OS_FIND_DATA_SIZE];
    if (!strcmp(op, "open") || !strcmp(op, "openw")) {
        uint32_t share = n > 5 ? (uint32_t)atoi(tok[5]) : 3u;
        uint32_t access = access_of(tok[3]), disposition = (uint32_t)atoi(tok[4]);
        es2_os_handle h;
        if (op[4] == 'w') {
            uint16_t wide[600];
            size_t i = 0;
            for (; a[i] && i + 1 < 600; i++) wide[i] = (unsigned char)a[i];
            wide[i] = 0;
            h = es2_os_create_file_utf16(wide, access, share, 0, disposition, ES2_OS_FILE_ATTRIBUTE_NORMAL);
        } else {
            h = es2_os_create_file(a, access, share, 0, disposition, ES2_OS_FILE_ATTRIBUTE_NORMAL);
        }
        new_handle(tok[1], h);
    } else if (!strcmp(op, "read")) {
        char data[64] = {0};
        uint32_t want = (uint32_t)atoi(tok[2]), done = 0xdeadu;
        int ok = es2_os_read_file(handle_of(tok[1]), data, want < 63 ? want : 63, &done);
        printf(" ok=%d done=%u data=%.*s", ok, (unsigned)done, (int)(done < 63 ? done : 0), data);
    } else if (!strcmp(op, "write")) {
        uint32_t done = 0xdeadu;
        int ok = es2_os_write_file(handle_of(tok[1]), tok[2], (uint32_t)strlen(tok[2]), &done);
        printf(" ok=%d done=%u", ok, (unsigned)done);
    } else if (!strcmp(op, "seek")) {
        printf(" %u", (unsigned)es2_os_set_file_pointer(handle_of(tok[1]), atoi(tok[2]), (uint32_t)atoi(tok[3])));
    } else if (!strcmp(op, "size")) {
        printf(" %u", (unsigned)es2_os_file_size(handle_of(tok[1])));
    } else if (!strcmp(op, "type")) {
        printf(" %u", (unsigned)es2_os_file_type(handle_of(tok[1])));
    } else if (!strcmp(op, "close")) {
        printf(" %d", es2_os_close_handle(handle_of(tok[1])));
    } else if (!strcmp(op, "attr")) {
        printf(" %x", (unsigned)es2_os_file_attributes(a));
    } else if (!strcmp(op, "del")) {
        printf(" %d", es2_os_delete_file(a));
    } else if (!strcmp(op, "mkdir")) {
        printf(" %d", es2_os_create_directory(a));
    } else if (!strcmp(op, "mtime")) {
        int64_t seconds = 0;
        int ok = es2_os_file_mtime(a, &seconds);
        printf(" %d recent=%d", ok, seconds > 1000000000);
    } else if (!strcmp(op, "move")) {
        printf(" %d", es2_os_move_file_replace(a, b));
    } else if (!strcmp(op, "find")) {
        memset(find, 0xcc, sizeof find);
        es2_os_handle h = es2_os_find_first(a, find);
        new_handle(tok[1], h);
        if (h != ES2_OS_INVALID_HANDLE) put_find(find);
    } else if (!strcmp(op, "next")) {
        memset(find, 0xcc, sizeof find);
        int ok = es2_os_find_next(handle_of(tok[1]), find);
        printf(" %d", ok);
        if (ok) put_find(find);
    } else if (!strcmp(op, "fclose")) {
        printf(" %d", es2_os_find_close(handle_of(tok[1])));
    } else if (!strcmp(op, "cwd") || !strcmp(op, "full")) {
        char out[600];
        memset(out, 'x', sizeof out);
        uint32_t capacity = (uint32_t)atoi(op[0] == 'c' ? tok[1] : tok[2]);
        uint32_t r = op[0] == 'c' ? es2_os_current_directory(out, capacity) : es2_os_full_path(a, out, capacity);
        printf(" %u ", (unsigned)r);
        if (r && r < capacity) put_path(out);
    } else {
        printf(" unknown");
    }
    printf(" e=%u\n", (unsigned)es2_os_last_error());
}

static void clocks(void) {
    es2_os_time t;
    uint8_t zone[ES2_OS_TIME_ZONE_SIZE];
    printf("tick %u\n", (unsigned)es2_os_tick_count());
    printf("frequency %llu\n", (unsigned long long)es2_os_counter_frequency());
    es2_os_local_time(&t);
    printf("local %u %u %u %u %u %u %u %u\n", t.year, t.month, t.day_of_week, t.day, t.hour, t.minute, t.second,
           t.milliseconds);
    es2_os_system_time(&t);
    printf("system %u %u %u %u %u %u %u %u\n", t.year, t.month, t.day_of_week, t.day, t.hour, t.minute, t.second,
           t.milliseconds);
    memset(zone, 0xcc, sizeof zone);
    uint32_t r = es2_os_time_zone(zone);
    /* Bias, StandardDate, StandardBias, DaylightDate, DaylightBias; the names are not compared. */
    printf("zone %u bias=%d", (unsigned)r, (int)u32(zone));
    printf(" standard=");
    for (int i = 0; i < 8; i++) printf("%s%u", i ? "," : "", (unsigned)(zone[68 + 2 * i] | zone[69 + 2 * i] << 8));
    printf(" standard_bias=%d daylight=", (int)u32(zone + 84));
    for (int i = 0; i < 8; i++) printf("%s%u", i ? "," : "", (unsigned)(zone[152 + 2 * i] | zone[153 + 2 * i] << 8));
    printf(" daylight_bias=%d\n", (int)u32(zone + 168));
    /* 2020-01-15 12:00 and 2020-07-15 12:00 UTC as FILETIME */
    static const uint32_t instants[2][2] = {{0x8b6f8000u, 0x01d5cba0u}, {0x31010000u, 0x01d65aa7u}};
    for (int i = 0; i < 2; i++) {
        uint32_t local[2] = {0, 0};
        int ok = es2_os_file_time_to_local(instants[i], local);
        uint64_t in = (uint64_t)instants[i][1] << 32 | instants[i][0], out = (uint64_t)local[1] << 32 | local[0];
        printf("to_local %d %d %lld\n", i, ok, (long long)(int64_t)(out - in) / 600000000);
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("usage: os_calls BASE | --clocks\n");
        return 2;
    }
    if (!strcmp(argv[1], "--clocks")) {
        clocks();
        return 0;
    }
    snprintf(base, sizeof base, "%s", argv[1]);
    for (size_t i = 0; i < sizeof script / sizeof script[0]; i++) run(script[i]);
    return 0;
}
