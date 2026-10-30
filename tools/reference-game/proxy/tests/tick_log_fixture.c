// Own-code fixture for proxy/tick_log.h. Only a synthetic image with an own
// loop stub is executed; no original game code runs here.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
static WCHAR config_path[MAX_PATH];
static unsigned installed_events, refused_events, output_refused_events;
static void log_event(const char *event) {
    if (!strcmp(event, "tick_log_installed")) ++installed_events;
    if (!strcmp(event, "tick_log_refused")) ++refused_events;
    if (!strcmp(event, "tick_log_output_refused")) ++output_refused_events;
}
#include "../tick_log.h"

#define LOOP_TOP_RVA 0x0005f458u
static BYTE actor[TICK_LOG_ACTOR_SIZE];

static BYTE *make_image(void) {
    BYTE *image = VirtualAlloc(NULL, TICK_LOG_IMAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!image) return NULL;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)image;
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 0x80;
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(image + 0x80);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    nt->OptionalHeader.ImageBase = (DWORD)(uintptr_t)image;
    nt->OptionalHeader.SizeOfImage = TICK_LOG_IMAGE_SIZE;
    memcpy(image + TICK_LOG_SITE_RVA, tick_log_site_signature, sizeof(tick_log_site_signature));
    memcpy(image + TICK_LOG_RNG_CORE_RVA, tick_log_rng_signature, sizeof(tick_log_rng_signature));
    image[TICK_LOG_CTX_SETUP_RVA] = 0xbb;
    DWORD context = (DWORD)(uintptr_t)(image + TICK_LOG_CTX_RVA);
    memcpy(image + TICK_LOG_CTX_SETUP_RVA + 1, &context, sizeof(context));
    // Loop stub: top: dec ecx; jnz site; ret. The site's original `jmp` returns to top.
    static const BYTE top[] = {0x49, 0x75, 0x52, 0xc3};
    memcpy(image + LOOP_TOP_RVA, top, sizeof(top));
    BYTE *ctx = image + TICK_LOG_CTX_RVA;
    DWORD counter = 1000, actor_address = (DWORD)(uintptr_t)actor;
    memcpy(ctx + 0xbf, &counter, sizeof(counter));
    memcpy(ctx + 0x2a, &actor_address, sizeof(actor_address));
    for (unsigned i = 0; i < TICK_LOG_RNG_SIZE; ++i) {
        image[TICK_LOG_RNG_A_RVA + i] = (BYTE)i;
        image[TICK_LOG_RNG_B_RVA + i] = (BYTE)(0xff - i);
    }
    DWORD clock = 123456;
    memcpy(image + TICK_LOG_CLOCK_RVA + 0x10, &clock, sizeof(clock));
    LONG position[3] = {2227902, 2324298, -13030};
    WORD angles[3] = {1, 2, 63524};
    SHORT speed = -276, throttle = 1024;
    memcpy(actor + 0x26, position, sizeof(position));
    memcpy(actor + 0x0c, angles, sizeof(angles));
    memcpy(actor + 0x28e, &speed, sizeof(speed));
    memcpy(actor + 0x290, &throttle, sizeof(throttle));
    return image;
}

static void run_loop(BYTE *image, DWORD count) {
    BYTE *top = image + LOOP_TOP_RVA, *ctx = image + TICK_LOG_CTX_RVA;
    __asm__ volatile(
        "pushl %%ebx\n\t"
        "movl %%edx, %%ebx\n\t"
        "call *%%eax\n\t"
        "popl %%ebx\n\t"
        : "+a"(top), "+d"(ctx), "+c"(count) : : "memory", "cc", "esi", "edi");
}

static DWORD counter_of(BYTE *image) {
    DWORD value;
    memcpy(&value, image + TICK_LOG_CTX_RVA + 0xbf, sizeof(value));
    return value;
}

static char *read_all(const WCHAR *path) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return NULL;
    DWORD size = GetFileSize(file, NULL), got = 0;
    char *text = calloc(1, size + 1);
    if (text) ReadFile(file, text, size, &got, NULL);
    CloseHandle(file);
    return text;
}

static unsigned count_of(const char *text, const char *needle) {
    unsigned n = 0;
    for (const char *p = text; (p = strstr(p, needle)); p += strlen(needle)) ++n;
    return n;
}

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); return 1; } } while (0)

static BYTE *loop_image;
static DWORD WINAPI loop_thread(void *count) { run_loop(loop_image, (DWORD)(uintptr_t)count); return 0; }

// Hold after 5 of 20 possible ticks: the loop thread parks for good, the 5th
// increment never runs, and the file holds exactly 5 ticks plus the hold line
// without a flush at shutdown (buffered: no worker, nothing else would write).
static int check_hold(BYTE *image, uintptr_t base, const WCHAR *path, BOOL buffered) {
    tick_log_hold = 5;
    CHECK(install_tick_log_image(image, base, path, 0, FALSE));
    tick_io_buffered = buffered;
    loop_image = image;
    HANDLE thread = CreateThread(NULL, 0, loop_thread, (void *)(uintptr_t)21, 0, NULL);
    CHECK(thread);
    for (unsigned i = 0; i < 500 && !tick_log_holding; ++i) Sleep(10);
    CHECK(tick_log_holding);
    CHECK(WaitForSingleObject(thread, 300) == WAIT_TIMEOUT);
    CHECK(counter_of(image) == 1004 && tick_log_sequence == 5 && tick_log_used == 0);
    char *text = read_all(path);
    CHECK(text);
    CHECK(count_of(text, "\"event\":\"tick\"") == 5);
    CHECK(strstr(text, "\"sequence\":5,") && strstr(text, "\"frame_counter_before_increment\":1004,"));
    CHECK(!strstr(text, "\"frame_counter_before_increment\":1005,"));
    static const char held[] = "{\"event\":\"hold\",\"sequence\":5}\r\n";
    size_t length = strlen(text);
    CHECK(length >= sizeof(held) - 1 && !strcmp(text + length - (sizeof(held) - 1), held));
    CHECK(count_of(text, "\"event\":\"hold\"") == 1);
    free(text);
    return 0;
}

int wmain(int argc, WCHAR **argv) {
    (void)install_tick_log; // production configuration entry point, not exercised here
    if (argc != 3) return 2;
    const WCHAR *mode = argv[1], *path = argv[2];
    BYTE *image = make_image();
    CHECK(image);
    uintptr_t base = (uintptr_t)image;

    if (!wcscmp(mode, L"valid")) {
        CHECK(install_tick_log_image(image, base, path, 0, TRUE));
        CHECK(installed_events == 1);
        CHECK(image[TICK_LOG_SITE_RVA] == 0xe8 && image[TICK_LOG_SITE_RVA + 5] == 0x90);
        CHECK(image[TICK_LOG_SITE_RVA + 6] == 0xeb && image[TICK_LOG_SITE_RVA + 7] == 0xa3);
        run_loop(image, 101);
        CHECK(counter_of(image) == 1100);
        tick_log_shutdown();
        char *text = read_all(path);
        CHECK(text);
        CHECK(count_of(text, "\"event\":\"start\",\"schema\":2") == 1);
        CHECK(count_of(text, "\"event\":\"tick\"") == 100);
        CHECK(strstr(text, "\"sequence\":1,") && strstr(text, "\"frame_counter_before_increment\":1000,"));
        CHECK(strstr(text, "\"sequence\":100,") && strstr(text, "\"frame_counter_before_increment\":1099,"));
        CHECK(!strstr(text, "\"frame_counter_before_increment\":1100,"));
        CHECK(count_of(text, "\"completed_clock_ms\":123456,") == 100);
        CHECK(count_of(text, "\"position_raw_i32\":[2227902,2324298,-13030],\"angles_raw_u16\":[1,2,63524],\"speed_raw_i16\":-276,\"throttle_raw_q10\":1024") == 100);
        CHECK(count_of(text, "\"rng_a_hex\":\"000102") == 100 && count_of(text, "\"rng_b_hex\":\"fffefd") == 100);
        CHECK(count_of(text, "\"actor_raw_hex\":\"") == 100);
        CHECK(!tick_log_hold && !tick_log_holding && !strstr(text, "\"event\":\"hold\""));
        free(text);
    } else if (!wcscmp(mode, L"hold")) {
        if (check_hold(image, base, path, FALSE)) return 1;
    } else if (!wcscmp(mode, L"hold_buffered")) {
        if (check_hold(image, base, path, TRUE)) return 1;
    } else if (!wcscmp(mode, L"limit")) {
        CHECK(install_tick_log_image(image, base, path, 10, FALSE));
        run_loop(image, 21);
        CHECK(counter_of(image) == 1020);
        tick_log_shutdown();
        char *text = read_all(path);
        CHECK(text);
        CHECK(count_of(text, "\"event\":\"tick\"") == 10);
        CHECK(count_of(text, "\"event\":\"limit_reached\"") == 1);
        CHECK(!strstr(text, "actor_raw_hex"));
        free(text);
    } else if (!wcscmp(mode, L"watch")) {
        // Synthetic structures: a pointer chain to a small record, a counted table
        // and a large bank that later changes in two separate places.
        static BYTE record[16], table[3 * 8], bank[600], holder[8];
        static DWORD record_pointer, table_pointer, holder_pointer;
        static SHORT table_count = 3;
        for (unsigned i = 0; i < sizeof(record); ++i) record[i] = (BYTE)(0xa0 + i);
        for (unsigned i = 0; i < sizeof(table); ++i) table[i] = (BYTE)i;
        holder_pointer = (DWORD)(uintptr_t)holder;
        record_pointer = (DWORD)(uintptr_t)record;
        memcpy(holder + 4, &record_pointer, 4);
        table_pointer = (DWORD)(uintptr_t)table;
        char spec[128];
        snprintf(spec, sizeof(spec), "chain 0x%lx@4@2 4", (unsigned long)(uintptr_t)&holder_pointer);
        CHECK(mem_watch_add(spec));
        snprintf(spec, sizeof(spec), "table 0x%lx@0 w0x%lx*8", (unsigned long)(uintptr_t)&table_pointer,
                 (unsigned long)(uintptr_t)&table_count);
        CHECK(mem_watch_add(spec));
        snprintf(spec, sizeof(spec), "bank 0x%lx 600", (unsigned long)(uintptr_t)bank);
        CHECK(mem_watch_add(spec));
        CHECK(mem_watch_add("missing 0x10@0 4"));
        CHECK(!mem_watch_add("bad-name 0x10 4") && !mem_watch_add("x 0x10") && !mem_watch_add("x 0x10 0") &&
              !mem_watch_add("x 0x10 70000") && !mem_watch_add("x 0x10@ 4") && !mem_watch_add("x 0x10 4 junk"));
        CHECK(mem_watch_count == 4);
        CHECK(install_tick_log_image(image, base, path, 0, FALSE));
        run_loop(image, 2);                      // tick 1: every watch written whole, missing unreadable
        run_loop(image, 2);                      // tick 2: nothing changed
        record[3] = 0x11;                        // inside chain range (record+2..+5)
        bank[10] = 1; bank[12] = 2; bank[500] = 3;
        run_loop(image, 2);                      // tick 3: chain whole (small), bank as two runs
        table_count = 2;                         // length changes: table written whole again
        holder_pointer = 0;                      // chain becomes unreadable
        run_loop(image, 2);                      // tick 4
        run_loop(image, 2);                      // tick 5: no repeated unreadable line
        tick_log_shutdown();
        char *text = read_all(path);
        CHECK(text);
        CHECK(count_of(text, "\"watches\":4}") == 1);
        CHECK(count_of(text, "\"event\":\"tick\"") == 5);
        CHECK(count_of(text, "\"event\":\"watch\"") == 8);
        CHECK(count_of(text, "\"name\":\"chain\"") == 3 && count_of(text, "\"name\":\"table\"") == 2 &&
              count_of(text, "\"name\":\"bank\"") == 2 && count_of(text, "\"name\":\"missing\"") == 1);
        CHECK(strstr(text, "\"sequence\":1,\"frame\":1000,\"name\":\"chain\""));
        CHECK(strstr(text, "\"length\":4,\"hex\":\"a2a3a4a5\""));
        CHECK(strstr(text, "\"length\":4,\"hex\":\"a211a4a5\""));
        CHECK(strstr(text, "\"length\":24,\"hex\":\"000102030405060708090a0b0c0d0e0f1011121314151617\""));
        CHECK(strstr(text, "\"length\":16,\"hex\":\"000102030405060708090a0b0c0d0e0f\""));
        CHECK(strstr(text, "\"sequence\":3,\"frame\":1002,\"name\":\"bank\""));
        CHECK(strstr(text, "\"diff\":[[10,\"010002\"],[500,\"03\"]]}"));
        CHECK(strstr(text, "\"sequence\":4,\"frame\":1003,\"name\":\"chain\",\"unreadable\":true"));
        CHECK(strstr(text, "\"name\":\"missing\",\"unreadable\":true"));
        free(text);
    } else if (!wcscmp(mode, L"mismatch")) {
        image[TICK_LOG_SITE_RVA + 2] = 0xc0;
        CHECK(!install_tick_log_image(image, base, path, 0, FALSE));
        CHECK(refused_events == 1 && installed_events == 0);
        CHECK(GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES);
        CHECK(image[TICK_LOG_SITE_RVA] == 0xff);
    } else if (!wcscmp(mode, L"exists")) {
        HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
        CHECK(file != INVALID_HANDLE_VALUE);
        DWORD written;
        WriteFile(file, "keep", 4, &written, NULL);
        CloseHandle(file);
        CHECK(!install_tick_log_image(image, base, path, 0, FALSE));
        CHECK(output_refused_events == 1 && refused_events == 1);
        CHECK(image[TICK_LOG_SITE_RVA] == 0xff);
        char *text = read_all(path);
        CHECK(text && !strcmp(text, "keep"));
        free(text);
    } else {
        return 2;
    }
    printf("PASS %ls\n", mode);
    return 0;
}
