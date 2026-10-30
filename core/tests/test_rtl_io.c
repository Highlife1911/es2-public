/* Unit test of core/src/rtl_io.c on a synthetic arena with a scripted fake
 * OS. Expected values are hand-traced from the contract in
 * docs/reference-game/rtl_imports.md ("file and console imports"); the in-vitro
 * harness compares the model with the original DLL. */
#include "es2/core/rtl_io.h"

#include <stdio.h>
#include <string.h>

static unsigned failures, checks;
static void check(int passed, const char *expression, int line)
{
    ++checks;
    if (!passed) {
        if (failures < 30) fprintf(stderr, "rtl_io:%d: %s\n", line, expression);
        ++failures;
    }
}
#define CHECK(expression) check((expression) ? 1 : 0, #expression, __LINE__)

/* Guest memory. */
#define BASE 0x01000000u
#define SIZE 0x40000u
static uint8_t arena[SIZE];
static unsigned outside;
static uint8_t read8(void *context, uint32_t a) { (void)context; if (a - BASE >= SIZE) { ++outside; return 0; } return arena[a - BASE]; }
static void write8(void *context, uint32_t a, uint8_t v) { (void)context; if (a - BASE >= SIZE) { ++outside; return; } arena[a - BASE] = v; }
static void put32(uint32_t a, uint32_t v) { for (unsigned i = 0; i < 4; ++i) arena[a - BASE + i] = (uint8_t)(v >> (8 * i)); }
static uint32_t get32(uint32_t a) { uint32_t v = 0; for (unsigned i = 0; i < 4; ++i) v |= (uint32_t)arena[a - BASE + i] << (8 * i); return v; }
static uint32_t get16(uint32_t a) { return (uint32_t)arena[a - BASE] | (uint32_t)arena[a - BASE + 1] << 8; }
static void text(uint32_t a, const char *s) { memcpy(arena + (a - BASE), s, strlen(s) + 1); }
static const char *at(uint32_t a) { return (const char *)arena + (a - BASE); }

/* Layout: the variables in the arena. */
#define L(offset) (BASE + (offset))
static es2_io_layout layout;
static void layout_init(void)
{
    layout.nfile = L(0x100); layout.openfd = L(0x104); layout.handles = L(0x200); layout.streams = L(0x300);
    layout.errno_word = L(0x800); layout.doserrno = L(0x804); layout.error_count = L(0x808); layout.error_map = L(0x900);
    layout.fmode_pointer = L(0x810); layout.umask_word = L(0x818); layout.exit_buffers = L(0x820); layout.exit_buffers_value = 0x11111111u;
    layout.exit_files = L(0x824); layout.exit_files_value = 0x22222222u; layout.getc_byte = L(0x828); layout.find_list = L(0x82c);
    layout.scan_class = L(0xb00); layout.time_zone = L(0x830); layout.summer_time = L(0x834); layout.stdout_file = L(0x300 + 0x18);
}
/* A fresh process state: handles 0..2 in use, FILE 0..2 the standard
 * streams, every other FILE free with its token. */
static void state_init(void)
{
    memset(arena, 0, sizeof(arena));
    layout_init();
    put32(layout.nfile, 50);
    put32(layout.openfd, 0x6000); put32(layout.openfd + 4, 0x6001); put32(layout.openfd + 8, 0x6001);
    for (unsigned i = 0; i < 50; ++i) put32(layout.handles + 4 * i, i < 3 ? 0x50u + i : 0xffffffffu);
    for (unsigned i = 0; i < 50; ++i) {
        uint32_t fp = layout.streams + 0x18u * i;
        arena[fp + 0x14 - BASE] = (uint8_t)fp; arena[fp + 0x15 - BASE] = (uint8_t)(fp >> 8);
        arena[fp + 0x16 - BASE] = i < 3 ? (uint8_t)i : 0xffu;
    }
    put32(layout.error_count, 49);
    for (unsigned i = 0; i < 0x12b; ++i) arena[layout.error_map - BASE + i] = (uint8_t)(i < 50 ? i + 100 : 0x13);
    put32(layout.fmode_pointer, L(0x814)); put32(L(0x814), 0x4000);
    put32(layout.umask_word, 0xffffffffu);
    put32(layout.time_zone, 18000); put32(layout.summer_time, 1);
    /* Scanner classes: 1 white space, 2 other, 3 '%', 4 '*', 5 digits, conversions as documented. */
    for (unsigned c = 1; c < 0x80; ++c) arena[layout.scan_class - BASE + c] = 2;
    const char *ws = "\t\n\v\f\r ";
    for (const char *p = ws; *p; ++p) arena[layout.scan_class - BASE + (uint8_t)*p] = 1;
    static const struct { char c; uint8_t k; } classes[] = {
        {'%', 3}, {'*', 4}, {'c', 6}, {'d', 7}, {'D', 7}, {'u', 8}, {'U', 8}, {'i', 9}, {'I', 9}, {'e', 10}, {'f', 10}, {'g', 10},
        {'E', 10}, {'G', 10}, {'L', 11}, {'h', 12}, {'H', 12}, {'l', 13}, {'o', 14}, {'O', 14}, {'s', 15}, {'[', 16}, {']', 16},
        {'n', 17}, {'x', 18}, {'X', 18}, {'p', 19}, {'N', 20}, {'F', 21},
    };
    for (unsigned i = 0; i < sizeof(classes) / sizeof(classes[0]); ++i) arena[layout.scan_class - BASE + (uint8_t)classes[i].c] = classes[i].k;
    for (unsigned c = '0'; c <= '9'; ++c) arena[layout.scan_class - BASE + c] = 5;
}

/* Fake OS: files by name, handles with positions, a call log. */
#define FILES 8
static struct { char name[32]; uint8_t data[4096]; uint32_t size; int used; } files[FILES];
static struct { int file; uint32_t position; int open; } handles[16];
static uint32_t last_error;
static es2_io_os log_calls[256];
static uint8_t log_bytes[256][640];
static unsigned log_count;
static uint16_t local_time[8];
/* Answers of the start-up's calls. */
static uint32_t handle_count_answer = 50, startup_size, startup_block, std_handles[3], tz_result;
static int32_t tz_bias, tz_standard;
static int find_file(const char *name) { for (int i = 0; i < FILES; ++i) if (files[i].used && !strcmp(files[i].name, name)) return i; return -1; }
static void add_file(const char *name, const char *content, uint32_t size)
{
    for (int i = 0; i < FILES; ++i) if (!files[i].used) {
        files[i].used = 1; strcpy(files[i].name, name); memcpy(files[i].data, content, size); files[i].size = size; return;
    }
}
static int handle_index(uint32_t h) { return h >= 0x100u && h < 0x100u + 16u * 4u && !(h & 3u) && handles[(h - 0x100u) / 4u].open ? (int)((h - 0x100u) / 4u) : -1; }
static void fake_os(void *context, es2_io_os *call)
{
    (void)context;
    uint32_t *a = call->arg;
    int h;
    switch (call->function) {
    case ES2_IO_OS_GET_FILE_ATTRIBUTES:
        if (find_file(at(a[0])) >= 0) call->result = 0x20; else { call->result = 0xffffffffu; last_error = 2; }
        break;
    case ES2_IO_OS_CREATE_FILE: {
        int f = find_file(at(a[0]));
        if (!strcmp(at(a[0]), "busy")) { last_error = 0x6e; call->result = 0xffffffffu; break; }
        if ((a[4] == 3 || a[4] == 5) && f < 0) { last_error = 2; call->result = 0xffffffffu; break; }
        if (a[4] == 1 && f >= 0) { last_error = 80; call->result = 0xffffffffu; break; }
        if (f < 0) { add_file(at(a[0]), "", 0); f = find_file(at(a[0])); }
        if (a[4] == 2 || a[4] == 5) files[f].size = 0;
        for (h = 0; h < 16 && handles[h].open; ++h) {}
        handles[h].open = 1; handles[h].file = f; handles[h].position = 0;
        call->result = 0x100u + 4u * (uint32_t)h;
        break;
    }
    case ES2_IO_OS_CLOSE_HANDLE:
        h = handle_index(a[0]);
        if (h < 0) { last_error = 6; call->result = 0; } else { handles[h].open = 0; call->result = 1; }
        break;
    case ES2_IO_OS_READ_FILE: {
        h = handle_index(a[0]);
        if (h < 0) { last_error = 6; call->result = 0; break; }
        uint32_t n = 0, p = handles[h].position;
        while (n < a[2] && p + n < files[handles[h].file].size) { write8(NULL, a[1] + n, files[handles[h].file].data[p + n]); ++n; }
        handles[h].position += n;
        call->count = n; call->result = 1;
        break;
    }
    case ES2_IO_OS_WRITE_FILE: {
        h = handle_index(a[0]);
        if (h < 0) { last_error = 6; call->result = 0; break; }
        int f = handles[h].file;
        for (uint32_t i = 0; i < a[2]; ++i) {
            uint32_t p = handles[h].position + i;
            files[f].data[p] = call->data ? call->data[i] : read8(NULL, a[1] + i);
            if (p + 1 > files[f].size) files[f].size = p + 1;
        }
        handles[h].position += a[2];
        call->count = a[2]; call->result = 1;
        break;
    }
    case ES2_IO_OS_SET_FILE_POINTER: {
        h = handle_index(a[0]);
        if (h < 0) { last_error = 6; call->result = 0xffffffffu; break; }
        int64_t base = a[2] == 0 ? 0 : a[2] == 1 ? handles[h].position : files[handles[h].file].size;
        int64_t to = base + (int32_t)a[1];
        if (to < 0) { last_error = 131; call->result = 0xffffffffu; break; }
        handles[h].position = (uint32_t)to; call->result = (uint32_t)to;
        break;
    }
    case ES2_IO_OS_GET_FILE_SIZE:
        h = handle_index(a[0]);
        call->result = h < 0 ? 0xffffffffu : files[handles[h].file].size;
        if (h < 0) last_error = 6;
        break;
    case ES2_IO_OS_GET_FILE_TYPE: h = handle_index(a[0]); call->result = h >= 0 && !strcmp(files[handles[h].file].name, "con") ? 2u : 1u; break;
    case ES2_IO_OS_GET_LAST_ERROR: call->result = last_error; break;
    case ES2_IO_OS_FIND_FIRST: case ES2_IO_OS_FIND_NEXT: {
        static unsigned cursor;
        if (call->function == ES2_IO_OS_FIND_FIRST) cursor = 0;
        memset(call->out, 0, sizeof(call->out));
        for (; cursor < FILES && !files[cursor].used; ++cursor) {}
        if (cursor >= FILES) { last_error = 18; call->result = call->function == ES2_IO_OS_FIND_FIRST ? 0xffffffffu : 0u; break; }
        uint32_t attributes = !strcmp(files[cursor].name, "dir") ? 0x10u : 0x20u;
        call->out[0] = (uint8_t)attributes;
        call->out[0x14] = 0x34; call->out[0x15] = 0x12; call->out[0x18] = 0x78; call->out[0x19] = 0x56;
        call->out[0x20] = (uint8_t)files[cursor].size; call->out[0x21] = (uint8_t)(files[cursor].size >> 8);
        strcpy((char *)call->out + 0x2c, files[cursor].name);
        ++cursor;
        call->result = call->function == ES2_IO_OS_FIND_FIRST ? 0x900u : 1u;
        break;
    }
    case ES2_IO_OS_FIND_CLOSE: call->result = 1; break;
    case ES2_IO_OS_FILE_TIME_TO_LOCAL:
        for (unsigned i = 0; i < 4; ++i) { call->out[i] = (uint8_t)(a[0] >> (8 * i)); call->out[4 + i] = (uint8_t)(a[1] >> (8 * i)); }
        call->result = 1;
        break;
    case ES2_IO_OS_FILE_TIME_TO_DOS:
        write8(NULL, a[2], (uint8_t)a[1]); write8(NULL, a[2] + 1, (uint8_t)(a[1] >> 8));
        write8(NULL, a[3], (uint8_t)a[0]); write8(NULL, a[3] + 1, (uint8_t)(a[0] >> 8));
        call->result = 1;
        break;
    case ES2_IO_OS_GET_LOCAL_TIME:
        for (unsigned i = 0; i < 8; ++i) { call->out[2 * i] = (uint8_t)local_time[i]; call->out[2 * i + 1] = (uint8_t)(local_time[i] >> 8); }
        break;
    /* The start-up's calls. */
    case ES2_IO_OS_SET_HANDLE_COUNT: call->result = a[0] < handle_count_answer ? a[0] : handle_count_answer; break;
    case ES2_IO_OS_GET_STARTUP_INFO:
        memset(call->out, 0, 0x44);
        call->out[0x32] = (uint8_t)startup_size; call->out[0x33] = (uint8_t)(startup_size >> 8);
        for (unsigned i = 0; i < 4; ++i) call->out[0x34 + i] = (uint8_t)(startup_block >> (8 * i));
        break;
    case ES2_IO_OS_GET_STD_HANDLE: call->result = a[0] == 0xfffffff6u ? std_handles[0] : a[0] == 0xfffffff5u ? std_handles[1] : std_handles[2]; break;
    case ES2_IO_OS_GET_TIME_ZONE:
        memset(call->out, 0, 172);
        for (unsigned i = 0; i < 4; ++i) { call->out[i] = (uint8_t)((uint32_t)tz_bias >> (8 * i)); call->out[0x54 + i] = (uint8_t)((uint32_t)tz_standard >> (8 * i)); }
        call->result = tz_result;
        break;
    default: break;
    }
    if (log_count < 256) {
        /* The bytes a recorder sees at the call: read output, write input. */
        if (call->function == ES2_IO_OS_READ_FILE && call->result == 1)
            memcpy(log_bytes[log_count], arena + (a[1] - BASE), call->count < 640 ? call->count : 640);
        if (call->function == ES2_IO_OS_WRITE_FILE)
            for (uint32_t i = 0; i < a[2] && i < 640; ++i) log_bytes[log_count][i] = call->data ? call->data[i] : read8(NULL, a[1] + i);
        log_calls[log_count++] = *call;
    }
}
/* CW3220-like heap: header word size|1 at p-4. */
static uint32_t heap_next, frees;
static uint32_t fake_malloc(void *context, uint32_t n, uint32_t hint)
{
    (void)context; (void)hint;
    uint32_t size = n < 8 ? 16 : ((n + 3) & ~3u) + 8, p = heap_next + 4;
    put32(p - 4, size | 1u);
    heap_next += size;
    return p;
}
static void fake_free(void *context, uint32_t p) { (void)context; (void)p; ++frees; }
static const es2_io_env env = {NULL, read8, write8, fake_os, fake_malloc, fake_free};

static void os_reset(void)
{
    memset(files, 0, sizeof(files)); memset(handles, 0, sizeof(handles));
    last_error = 0; log_count = 0; heap_next = L(0x20000); frees = 0;
}
#define ARGS L(0x1000)
static uint32_t call(es2_io_function f, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, int *status)
{
    put32(ARGS, a0); put32(ARGS + 4, a1); put32(ARGS + 8, a2); put32(ARGS + 12, a3); put32(ARGS + 16, a4);
    uint32_t eax = 0xdeadbeefu;
    int s = es2_io_call(f, &env, &layout, ARGS, &eax);
    if (status) *status = s;
    return eax;
}
#define S(offset) L(0x2000 + (offset))   /* strings */

static void test_handles(void)
{
    state_init(); os_reset();
    text(S(0), "missing");
    CHECK(call(ES2_IO_OPEN, S(0), 0x8000, 0, 0, 0, NULL) == 0xffffffffu);
    CHECK(log_count == 3 && log_calls[0].function == ES2_IO_OS_GET_FILE_ATTRIBUTES && log_calls[1].function == ES2_IO_OS_CREATE_FILE &&
          log_calls[2].function == ES2_IO_OS_GET_LAST_ERROR);
    CHECK(log_calls[1].arg[1] == 0x80000000u && log_calls[1].arg[2] == 3 && log_calls[1].arg[3] == 1 && log_calls[1].arg[4] == 3 && log_calls[1].arg[5] == 0);
    CHECK(get32(layout.doserrno) == 2 && get32(layout.errno_word) == 102);
    /* 0x6e with O_CREAT becomes 0x50 (errno map entry 0x50 -> 0x13 here). */
    text(S(0x20), "busy");
    CHECK(call(ES2_IO_OPEN, S(0x20), 0x8301, 0x180, 0, 0, NULL) == 0xffffffffu && get32(layout.doserrno) == 0x50 && get32(layout.errno_word) == 0x13);
    /* An existing file: the first free handle is 3; flags without 0x700. */
    add_file("data", "a\r\nb\r\n\x1azz", 9);
    text(S(0x40), "data");
    log_count = 0;
    CHECK(call(ES2_IO_OPEN, S(0x40), 0x8000, 0, 0, 0, NULL) == 3);
    CHECK(get32(layout.openfd + 12) == 0x8000 && get32(layout.handles + 12) == 0x100);
    CHECK(log_calls[2].function == ES2_IO_OS_GET_FILE_TYPE);
    /* Binary read: the bytes as they are. */
    CHECK(call(ES2_IO_READ, 3, L(0x3000), 4, 0, 0, NULL) == 4 && !memcmp(at(L(0x3000)), "a\r\nb", 4));
    CHECK(call(ES2_IO_READ, 3, L(0x3000), 0, 0, 0, NULL) == 0 && call(ES2_IO_READ, 3, L(0x3000), 0xffffffffu, 0, 0, NULL) == 0);
    CHECK(call(ES2_IO_LSEEK, 3, 0, 0, 0, 0, NULL) == 0);
    CHECK(call(ES2_IO_LSEEK, 3, 0, 3, 0, 0, NULL) == 0xffffffffu && get32(layout.doserrno) == 1);
    CHECK(call(ES2_IO_FILELENGTH, 3, 0, 0, 0, 0, NULL) == 9);
    CHECK(call(ES2_IO_CLOSE, 3, 0, 0, 0, 0, NULL) == 0 && get32(layout.openfd + 12) == 0 && get32(layout.handles + 12) == 0xffffffffu);
    CHECK(call(ES2_IO_CLOSE, 50, 0, 0, 0, 0, NULL) == 0xffffffffu && get32(layout.doserrno) == 6);
    CHECK(call(ES2_IO_READ, 50, L(0x3000), 1, 0, 0, NULL) == 0xffffffffu && get32(layout.doserrno) == 0xffffffffu && get32(layout.errno_word) == 6);
    /* Text read: every CR removed, Ctrl-Z ends the file and sets 0x200. */
    CHECK(call(ES2_IO_OPEN, S(0x40), 0x4000, 0, 0, 0, NULL) == 3);
    memset(arena + 0x3000, 0, 16);
    CHECK(call(ES2_IO_READ, 3, L(0x3000), 16, 0, 0, NULL) == 4 && !memcmp(at(L(0x3000)), "a\nb\n", 4));
    CHECK(get32(layout.openfd + 12) & 0x200u);
    CHECK(call(ES2_IO_READ, 3, L(0x3000), 16, 0, 0, NULL) == 0);
    /* A CR at the end of a chunk is replaced by the next byte. */
    CHECK(call(ES2_IO_LSEEK, 3, 0, 0, 0, 0, NULL) == 0 && !(get32(layout.openfd + 12) & 0x200u));
    CHECK(call(ES2_IO_READ, 3, L(0x3000), 2, 0, 0, NULL) == 2 && !memcmp(at(L(0x3000)), "a\n", 2));
    call(ES2_IO_CLOSE, 3, 0, 0, 0, 0, NULL);
    /* Text write: LF becomes CR LF; the result counts source bytes. */
    text(S(0x60), "out");
    CHECK(call(ES2_IO_OPEN, S(0x60), 0x4301, 0x180, 0, 0, NULL) == 3);
    text(L(0x3100), "x\ny\n");
    CHECK(call(ES2_IO_WRITE, 3, L(0x3100), 4, 0, 0, NULL) == 4);
    int f = find_file("out");
    CHECK(f >= 0 && files[f].size == 6 && !memcmp(files[f].data, "x\r\ny\r\n", 6));
    /* Append: a seek to the end before each write. */
    call(ES2_IO_CLOSE, 3, 0, 0, 0, 0, NULL);
    CHECK(call(ES2_IO_OPEN, S(0x60), 0x8901, 0x180, 0, 0, NULL) == 3);
    CHECK(call(ES2_IO_WRITE, 3, L(0x3100), 2, 0, 0, NULL) == 2 && files[f].size == 8);
    call(ES2_IO_CLOSE, 3, 0, 0, 0, 0, NULL);
}

static void test_files(void)
{
    state_init(); os_reset();
    add_file("cfg", "C:\\  42 -7 0x1F 017 abc]def x", 29);
    text(S(0), "cfg"); text(S(0x10), "rt"); text(S(0x20), "%s %d %i %i %i %[a-c]%n");
    uint32_t fp = call(ES2_IO_FOPEN, S(0), S(0x10), 0, 0, 0, NULL);
    CHECK(fp == layout.streams + 3 * 0x18u);
    CHECK(get16(fp + 0x12) == 5 && (int8_t)arena[fp + 0x16 - BASE] == 3 && get32(fp + 0xc) == 0x200);
    CHECK(get32(fp + 4) == L(0x20004) && get32(fp) == get32(fp + 4) && get32(layout.exit_files) == 0x22222222u && get32(layout.exit_buffers) == 0x11111111u);
    CHECK(get32(layout.openfd + 12) == 0x4000);
    put32(L(0x3000), L(0x3100)); put32(L(0x3004), L(0x3200)); put32(L(0x3008), L(0x3204)); put32(L(0x300c), L(0x3208));
    put32(L(0x3010), L(0x320c)); put32(L(0x3014), L(0x3300)); put32(L(0x3018), L(0x3210));
    put32(ARGS, fp); put32(ARGS + 4, S(0x20));
    for (unsigned i = 0; i < 7; ++i) put32(ARGS + 8 + 4 * i, get32(L(0x3000) + 4 * i));
    uint32_t eax = 0;
    CHECK(es2_io_call(ES2_IO_FSCANF, &env, &layout, ARGS, &eax) == ES2_IO_DONE);
    CHECK(eax == 6);
    CHECK(!strcmp(at(L(0x3100)), "C:\\") && get32(L(0x3200)) == 42 && get32(L(0x3204)) == (uint32_t)-7 && get32(L(0x3208)) == 0x1f &&
          get32(L(0x320c)) == 15 && !strcmp(at(L(0x3300)), "abc") && get32(L(0x3210)) == 23);
    /* fread the rest after the pushed-back byte. */
    memset(arena + 0x3400, 0, 16);
    CHECK(call(ES2_IO_FREAD, L(0x3400), 1, 10, fp, 0, NULL) == 6 && !memcmp(at(L(0x3400)), "]def x", 6));
    CHECK((get16(fp + 0x12) & 0x20u) && get32(fp + 8) == 0);   /* end of file seen by the refill */
    CHECK(call(ES2_IO_FTELL, fp, 0, 0, 0, 0, NULL) == 29u);
    CHECK(call(ES2_IO_REWIND, fp, 0, 0, 0, 0, NULL) == 0 && get32(fp + 8) == 0);
    CHECK(call(ES2_IO_FREAD, L(0x3400), 3, 2, fp, 0, NULL) == 2 && !memcmp(at(L(0x3400)), "C:\\  4", 6));
    CHECK(call(ES2_IO_FSEEK, fp, 3, 0, 0, 0, NULL) == 0 && call(ES2_IO_FTELL, fp, 0, 0, 0, 0, NULL) == 3);
    /* A float conversion is not modelled. */
    text(S(0x40), "%f");
    int status = 0;
    put32(ARGS, fp); put32(ARGS + 4, S(0x40));
    CHECK(es2_io_call(ES2_IO_FSCANF, &env, &layout, ARGS, &eax) == ES2_IO_UNSUPPORTED);
    (void)status;
    CHECK(call(ES2_IO_FCLOSE, fp, 0, 0, 0, 0, NULL) == 0 && frees == 1 && (int8_t)arena[fp + 0x16 - BASE] == -1 && get16(fp + 0x12) == 0);
    CHECK(call(ES2_IO_FCLOSE, fp + 1, 0, 0, 0, 0, NULL) == 0xffffffffu);   /* token */
    /* Write a binary file through the buffer; fclose flushes it. */
    text(S(0x50), "bin"); text(S(0x60), "wb");
    fp = call(ES2_IO_FOPEN, S(0x50), S(0x60), 0, 0, 0, NULL);
    CHECK(fp == layout.streams + 3 * 0x18u && get16(fp + 0x12) == 0x46);
    text(L(0x3500), "0123456789");
    CHECK(call(ES2_IO_FWRITE, L(0x3500), 5, 2, fp, 0, NULL) == 2 && get32(fp + 8) == (uint32_t)(-0x201 + 10));
    CHECK(call(ES2_IO_FTELL, fp, 0, 0, 0, 0, NULL) == 10);
    CHECK(call(ES2_IO_FWRITE, L(0x3500), 0, 7, fp, 0, NULL) == 7);
    CHECK(call(ES2_IO_FLUSHALL, 0, 0, 0, 0, 0, NULL) == 1 && find_file("bin") >= 0 && files[find_file("bin")].size == 10);
    CHECK(call(ES2_IO_FCLOSE, fp, 0, 0, 0, 0, NULL) == 0);
    /* Invalid mode, missing file. */
    text(S(0x70), "x"); text(S(0x80), "nofile"); text(S(0x90), "r");
    CHECK(call(ES2_IO_FOPEN, S(0x50), S(0x70), 0, 0, 0, NULL) == 0);
    CHECK(call(ES2_IO_FOPEN, S(0x80), S(0x90), 0, 0, 0, NULL) == 0 && (int8_t)arena[layout.streams + 3 * 0x18 + 0x16 - BASE] == -1);
}

static void test_find_time_console(void)
{
    state_init(); os_reset();
    add_file("dir", "", 0);
    add_file("a.vol", "12345", 5);
    add_file("b.vol", "", 0);
    text(S(0), "vol\\*.vol");
    uint32_t ffblk = L(0x3000);
    CHECK(call(ES2_IO_FINDFIRST, S(0), ffblk, 0, 0, 0, NULL) == 0);
    uint32_t node = get32(ffblk);
    CHECK(node == L(0x20004) && get32(layout.find_list) == node && get32(node) == ffblk && get32(node + 4) == 0x900 && get32(node + 0xc) == S(0));
    CHECK(!strcmp(at(ffblk + 0x10), "a.vol") && get32(ffblk + 4) == 5 && get32(ffblk + 8) == 0x20 && get16(ffblk + 0xc) == 0x1234 && get16(ffblk + 0xe) == 0x5678);
    CHECK(call(ES2_IO_FINDNEXT, ffblk, 0, 0, 0, 0, NULL) == 0 && !strcmp(at(ffblk + 0x10), "b.vol"));
    CHECK(call(ES2_IO_FINDNEXT, ffblk, 0, 0, 0, 0, NULL) == 0xffffffffu && get32(layout.doserrno) == 18);
    /* The same block again: the node is reused after FindClose. */
    log_count = 0;
    CHECK(call(ES2_IO_FINDFIRST, S(0), ffblk, 0x10, 0, 0, NULL) == 0 && get32(ffblk) == node && log_calls[0].function == ES2_IO_OS_FIND_CLOSE);
    CHECK(!strcmp(at(ffblk + 0x10), "dir"));
    /* time: 1990-07-01 12:00:00 local, EST with summer time. */
    local_time[0] = 1990; local_time[1] = 7; local_time[3] = 1; local_time[4] = 12;
    CHECK(call(ES2_IO_TIME, L(0x3200), 0, 0, 0, 0, NULL) == 646848000u && get32(L(0x3200)) == 646848000u);
    put32(layout.summer_time, 0); put32(layout.time_zone, 0);
    local_time[0] = 1970; local_time[1] = 1; local_time[3] = 2; local_time[4] = 0;
    CHECK(call(ES2_IO_TIME, 0, 0, 0, 0, 0, NULL) == 86400u);
    local_time[0] = 1969;
    CHECK(call(ES2_IO_TIME, 0, 0, 0, 0, 0, NULL) == 0xffffffffu);
    /* Stubs. */
    text(S(0x40), "x=%d!");
    CHECK(call(ES2_IO_PRINTF, S(0x40), 42, 0, 0, 0, NULL) == 5);
    CHECK(call(ES2_IO_FPUTC, 0x1ff, 0, 0, 0, 0, NULL) == 0xff);
    CHECK(call(ES2_IO_FPUTS, S(0x40), 0, 0, 0, 0, NULL) == '!' && call(ES2_IO_FPUTS, 0, 0, 0, 0, 0, NULL) == 0);
}

/* Replay: record the fake OS calls of a run, then replay the same call on
 * the state before it; then plant a difference. */
static uint32_t keys[1u << 12], stamps[1u << 12], written[1u << 11];
static uint8_t values[1u << 12], snapshot[16384];
static es2_io_record records[64];
static uint8_t pool[8192];
static void to_records(void)
{
    uint32_t used = 0;
    for (unsigned i = 0; i < log_count; ++i) {
        es2_io_os *c = &log_calls[i];
        es2_io_record *k = &records[i];
        memset(k, 0, sizeof(*k));
        k->function = c->function; k->result = c->result; k->count = c->count; k->data = used;
        uint32_t *a = c->arg;
        switch (c->function) {
        case ES2_IO_OS_CREATE_FILE:
            k->arg[0] = a[0]; k->arg[1] = a[1]; k->arg[2] = a[2]; k->arg[3] = 0x77777777u; k->arg[4] = a[4]; k->arg[5] = a[5];
            pool[used] = 12; memset(pool + used + 1, 0, 7); pool[used + 8] = (uint8_t)a[3]; memset(pool + used + 9, 0, 3); k->size = 12;
            break;
        case ES2_IO_OS_READ_FILE:
            k->arg[0] = a[0]; k->arg[1] = a[1]; k->arg[2] = a[2]; k->arg[3] = 0x66666666u;
            memcpy(pool + used, log_bytes[i], c->count); k->size = c->count;
            break;
        case ES2_IO_OS_WRITE_FILE:
            k->arg[0] = a[0]; k->arg[1] = a[1] ? a[1] : 0x55555555u; k->arg[2] = a[2]; k->arg[3] = 0x66666666u;
            memcpy(pool + used, log_bytes[i], a[2]); k->size = a[2];
            break;
        case ES2_IO_OS_SET_FILE_POINTER: k->arg[0] = a[0]; k->arg[1] = a[1]; k->arg[3] = a[2]; break;
        default: k->arg[0] = a[0]; break;
        }
        used += k->size;
    }
}
static void test_replay(void)
{
    state_init(); os_reset();
    add_file("cfg", "ab\r\ncd", 6);
    text(S(0), "cfg"); text(S(0x10), "rt");
    uint32_t fp = call(ES2_IO_FOPEN, S(0), S(0x10), 0, 0, 0, NULL);
    CHECK(fp != 0);
    es2_io_replay replay;
    es2_io_replay_init(&replay, keys, stamps, values, written, 12, 1u << 11, snapshot, sizeof(snapshot));
    /* The original: fread 5 bytes (a refill through the text read). */
    es2_io_replay_reset(&replay);
    CHECK(es2_io_replay_track_call(&replay, &layout, fp, 0, read8, NULL));
    log_count = 0;
    uint32_t real = call(ES2_IO_FREAD, L(0x3000), 1, 5, fp, 0, NULL);
    CHECK(real == 5 && !memcmp(at(L(0x3000)), "ab\ncd", 5));
    to_records();
    /* The model on the state before the call: the arena now holds the
     * original's result; the replay reads the tracked state from the snapshot. */
    es2_io_env replay_env;
    replay.live_read8 = read8; replay.live_context = NULL;
    es2_io_replay_env(&replay_env, &replay, records, log_count, pool, sizeof(pool));
    put32(ARGS, L(0x3000)); put32(ARGS + 4, 1); put32(ARGS + 8, 5); put32(ARGS + 12, fp);
    uint32_t eax = 0;
    CHECK(es2_io_call(ES2_IO_FREAD, &replay_env, &layout, ARGS, &eax) == ES2_IO_DONE);
    CHECK(es2_io_replay_finish(&replay, eax, real) == NULL && !replay.skip);
    /* Planted: the original's buffer byte differs from what the record says. */
    es2_io_replay_reset(&replay);
    CHECK(es2_io_replay_track_call(&replay, &layout, fp, 0, read8, NULL));
    log_count = 0;
    real = call(ES2_IO_FSEEK, fp, 0, 0, 0, 0, NULL);
    to_records();
    records[0].arg[1] = 1;   /* a different seek distance than the model's */
    es2_io_replay_env(&replay_env, &replay, records, log_count, pool, sizeof(pool));
    put32(ARGS, fp); put32(ARGS + 4, 0); put32(ARGS + 8, 0);
    CHECK(es2_io_call(ES2_IO_FSEEK, &replay_env, &layout, ARGS, &eax) == ES2_IO_DONE);
    const char *what = es2_io_replay_finish(&replay, eax, real);
    CHECK(what && !strcmp(what, "os"));
    /* An unexplained change of tracked state. */
    es2_io_replay_reset(&replay);
    CHECK(es2_io_replay_track_call(&replay, &layout, fp, 0, read8, NULL));
    log_count = 0;
    real = call(ES2_IO_FTELL, fp, 0, 0, 0, 0, NULL);
    to_records();
    arena[layout.errno_word - BASE] ^= 1;
    es2_io_replay_env(&replay_env, &replay, records, log_count, pool, sizeof(pool));
    put32(ARGS, fp);
    CHECK(es2_io_call(ES2_IO_FTELL, &replay_env, &layout, ARGS, &eax) == ES2_IO_DONE);
    what = es2_io_replay_finish(&replay, eax, real);
    CHECK(what && !strcmp(what, "state") && replay.where == layout.errno_word);
}

/* Static state, start-up, time zone and the exit hooks. */
static void test_startup(void)
{
    memset(arena, 0, sizeof(arena));
    layout_init();
    os_reset();
    es2_io_static_state(&env, &layout, L(0x814), 0x33333333u);
    CHECK(get32(layout.nfile) == 50 && get32(layout.openfd) == 0x6000 && get32(layout.openfd + 4) == 0x6001 && get32(layout.openfd + 8) == 0x6001);
    CHECK(get32(layout.openfd + 12) == 0 && get32(layout.handles) == 0 && get32(layout.handles + 196) == 0);
    CHECK(get16(layout.streams + 0x12) == 0x209 && get16(layout.streams + 0x2a) == 0x20a && get16(layout.streams + 0x42) == 0x202);
    CHECK(arena[layout.streams + 0x16 - BASE] == 0 && arena[layout.streams + 0x2e - BASE] == 1 && arena[layout.streams + 0x46 - BASE] == 2);
    CHECK(get16(layout.streams + 0x14) == 0 && arena[layout.streams + 0x5e - BASE] == 0);
    CHECK(get32(layout.error_count) == 49 && arena[layout.error_map + 2 - BASE] == 2 && arena[layout.error_map + 0x50 - BASE] == 35 &&
          arena[layout.error_map + 0x2a - BASE] == 14 && arena[layout.error_map + 0x12a - BASE] == 19 && arena[layout.error_map - BASE] == 0);
    CHECK(arena[layout.scan_class + 'd' - BASE] == 7 && arena[layout.scan_class + ']' - BASE] == 16 && arena[layout.scan_class + ' ' - BASE] == 1 &&
          arena[layout.scan_class + 'Q' - BASE] == 2 && arena[layout.scan_class + '7' - BASE] == 5);
    CHECK(get32(layout.fmode_pointer) == L(0x814) && get32(L(0x814)) == 0x4000 && get32(layout.umask_word) == 0xffffffffu);
    CHECK(get32(layout.exit_buffers) == 0x33333333u && get32(layout.exit_files) == 0x33333333u);
    CHECK(get32(layout.time_zone) == 18000 && get32(layout.summer_time) == 1);
    CHECK(es2_io_ctype(' ') == 0x81 && es2_io_ctype('\t') == 0x21 && es2_io_ctype('A') == 0x14 && es2_io_ctype('Z') == 4 &&
          es2_io_ctype('f') == 0x18 && es2_io_ctype('g') == 8 && es2_io_ctype('5') == 0x12 && es2_io_ctype('!') == 0x40 &&
          es2_io_ctype(0x7f) == 0x20 && es2_io_ctype(0x80) == 0x81 && es2_io_ctype(0xfe) == 0xff && es2_io_ctype(0xff) == 0);
    /* The standard handles: a smaller handle count lowers __nfile. */
    add_file("con", "", 0); add_file("log", "", 0);
    handles[0].open = 1; handles[0].file = find_file("con");
    handles[1].open = 1; handles[1].file = find_file("log");
    handles[2].open = 1; handles[2].file = find_file("con");
    std_handles[0] = 0x100; std_handles[1] = 0x104; std_handles[2] = 0x108;
    handle_count_answer = 40; startup_size = 0; startup_block = 0;
    es2_io_startup(&env, &layout);
    CHECK(get32(layout.nfile) == 40);
    CHECK(log_count == 8 && log_calls[0].function == ES2_IO_OS_SET_HANDLE_COUNT && log_calls[0].arg[0] == 50 &&
          log_calls[1].function == ES2_IO_OS_GET_STARTUP_INFO && log_calls[2].function == ES2_IO_OS_GET_STD_HANDLE &&
          log_calls[2].arg[0] == 0xfffffff6u && log_calls[4].arg[0] == 0xfffffff4u && log_calls[5].function == ES2_IO_OS_GET_FILE_TYPE &&
          log_calls[6].arg[0] == 0x104 && log_calls[7].arg[0] == 0x108);
    CHECK(get32(layout.handles) == 0x100 && get32(layout.handles + 4) == 0x104 && get32(layout.handles + 8) == 0x108);
    CHECK(get32(layout.openfd) == 0x6000 && get32(layout.openfd + 4) == 0x4001 && get32(layout.openfd + 8) == 0x6001);
    CHECK(get16(layout.streams + 0x14) == (layout.streams & 0xffff) && get16(layout.streams + 0x18 * 39 + 0x14) == ((layout.streams + 0x18 * 39) & 0xffff));
    CHECK(arena[layout.streams + 0x18 * 3 + 0x16 - BASE] == 0xff && arena[layout.streams + 0x18 * 39 + 0x16 - BASE] == 0xff &&
          arena[layout.streams + 0x18 * 40 + 0x16 - BASE] == 0 && get16(layout.streams + 0x18 * 40 + 0x14) == 0);
    /* stdin a terminal: line buffered with a 512-byte block; stdout a file: fully buffered. */
    uint32_t in = layout.streams, out = layout.streams + 0x18;
    CHECK(get16(in + 0x12) == 0x20d && get32(in + 0xc) == 0x200 && get32(in + 4) == L(0x20004) && get32(in) == L(0x20004));
    CHECK(get16(out + 0x12) == 0x006 && get32(out + 0xc) == 0x200 && get32(out + 4) == L(0x20004 + 0x208));
    CHECK(get32(layout.exit_buffers) == 0x11111111u && get32(layout.exit_files) == 0x33333333u);
    /* Inherited handles: four entries in a block of 4 + 5 * 4 bytes. */
    memset(arena, 0, sizeof(arena));
    layout_init();
    os_reset();
    es2_io_static_state(&env, &layout, L(0x814), 0x33333333u);
    handle_count_answer = 50;
    startup_size = 24; startup_block = L(0x6000);
    put32(L(0x6000), 4);
    arena[0x6004] = 0x80; arena[0x6005] = 0xc0; arena[0x6006] = 0x20; arena[0x6007] = 0x00;
    for (unsigned i = 0; i < 4; ++i) put32(L(0x6008) + 4 * i, 0x200 + i);
    es2_io_startup(&env, &layout);
    CHECK(log_count == 2);
    CHECK(get32(layout.openfd) == 0x4000 && get32(layout.openfd + 4) == 0x6001 && get32(layout.openfd + 8) == 0x8801 && get32(layout.openfd + 12) == 0x8000);
    CHECK(get32(layout.handles) == 0x200 && get32(layout.handles + 12) == 0x203);
    CHECK(get16(in + 0x12) == 0x005 && get32(in + 0xc) == 0x200);              /* not a terminal: full buffering */
    CHECK(get16(out + 0x12) == 0x202 && get32(out + 0xc) == 0 && get32(out + 4) == out + 0x17);   /* a terminal: unbuffered */
    /* A block of the wrong size is ignored: the standard handles. */
    memset(arena, 0, sizeof(arena));
    layout_init();
    os_reset();
    es2_io_static_state(&env, &layout, L(0x814), 0x33333333u);
    startup_size = 25;
    put32(L(0x6000), 4);
    es2_io_startup(&env, &layout);
    CHECK(log_count == 8 && log_calls[2].function == ES2_IO_OS_GET_STD_HANDLE);
    startup_size = 0;
    /* Time zone: TZ from the block (case-sensitive name), else GetTimeZoneInformation. */
    static const struct { const char *block; uint32_t zone, summer; } zones[] = {
        {"A=1\0TZ=PST8PDT\0", 28800, 1}, {"TZ=EST5\0", 18000, 0}, {"TZX=1\0TZ=CET-1CEST\0", 0xfffff1f0u, 1},
        {"TZ=GMT+0\0", 0, 0}, {"TZ=ABC10DE\0", 36000, 0}, {"TZ=ABC 1\0", 0xffffffffu, 0xffffffffu},
        {"TZ=AB\0", 0xffffffffu, 0xffffffffu}, {"tz=PST8\0", 0xffffffffu, 0xffffffffu}, {"TZ=AB1C2\0", 0xffffffffu, 0xffffffffu},
    };
    tz_bias = 300; tz_standard = 0; tz_result = 2;
    for (unsigned i = 0; i < sizeof(zones) / sizeof(zones[0]); ++i) {
        memset(arena + 0x7000, 0, 0x100);
        const char *p = zones[i].block;
        uint32_t at_ = L(0x7000);
        while (*p) { size_t n = strlen(p) + 1; memcpy(arena + (at_ - BASE), p, n); at_ += (uint32_t)n; p += n; }
        log_count = 0;
        es2_io_tzset(&env, &layout, L(0x7000));
        if (zones[i].zone == 0xffffffffu) CHECK(log_count == 1 && get32(layout.time_zone) == 18000 && get32(layout.summer_time) == 1);
        else CHECK(log_count == 0 && get32(layout.time_zone) == zones[i].zone && get32(layout.summer_time) == zones[i].summer);
    }
    tz_bias = -60; tz_standard = 0; tz_result = 1;
    es2_io_tzset(&env, &layout, 0);
    CHECK(get32(layout.time_zone) == 0xfffff1f0u && get32(layout.summer_time) == 0);
    /* Exit hooks: exitbuf flushes the standard output's pending bytes,
     * exitfopen closes every open FILE (the standard streams included). */
    memset(arena, 0, sizeof(arena));
    layout_init();
    os_reset();
    es2_io_static_state(&env, &layout, L(0x814), 0x33333333u);
    add_file("log", "", 0); add_file("con", "", 0);
    handles[0].open = 1; handles[0].file = find_file("con");
    handles[1].open = 1; handles[1].file = find_file("log");
    handles[2].open = 1; handles[2].file = find_file("con");
    es2_io_startup(&env, &layout);
    text(L(0x3500), "hello");
    CHECK(call(ES2_IO_FWRITE, L(0x3500), 1, 5, out, 0, NULL) == 5 && files[find_file("log")].size == 0);
    log_count = 0;
    es2_io_exit_buffers(&env, &layout);
    CHECK(files[find_file("log")].size == 5 && !memcmp(files[find_file("log")].data, "hello", 5) && log_count == 1);
    text(S(0), "data"); text(S(0x10), "w");
    uint32_t fp = call(ES2_IO_FOPEN, S(0), S(0x10), 0, 0, 0, NULL);
    CHECK(fp == layout.streams + 3 * 0x18u && get32(layout.exit_files) == 0x22222222u);
    CHECK(call(ES2_IO_FWRITE, L(0x3500), 1, 3, fp, 0, NULL) == 3);
    frees = 0;
    es2_io_exit_files(&env, &layout);
    CHECK(files[find_file("data")].size == 3 && frees == 3);   /* stdin's, stdout's and the file's buffer */
    CHECK((int8_t)arena[fp + 0x16 - BASE] == -1 && (int8_t)arena[in + 0x16 - BASE] == -1 && (int8_t)arena[layout.streams + 0x46 - BASE] == -1);
    CHECK(!handles[0].open && !handles[1].open && !handles[2].open && get32(layout.handles) == 0xffffffffu);
}

int main(void)
{
    test_handles();
    test_files();
    test_find_time_console();
    test_replay();
    test_startup();
    CHECK(outside == 0);
    printf("rtl_io: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
