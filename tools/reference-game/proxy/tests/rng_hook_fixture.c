/* Synthetic integration checks for the production RNG hook adapters.
 * Only our generated entry stubs and our C model execute. No original seed,
 * executable, DLL or instruction sequence is read from a game installation. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static WCHAR config_path[MAX_PATH];
static unsigned events;
static void log_event(const char *event) { (void)event; ++events; }
#include "../rng_hooks.h"

_Static_assert(sizeof(void *) == 4, "RNG detour fixture is Windows x86 only");

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "rng-hooks FAIL line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)

static unsigned model_calls;

static void put16(BYTE *bytes, uint16_t value) {
    bytes[0] = (BYTE)value;
    bytes[1] = (BYTE)(value >> 8);
}

static void put32(BYTE *bytes, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[i] = (BYTE)(value >> (8 * i));
}

static void seed_state(BYTE *state) {
    BYTE seed[ES2_RNG_SEED_SIZE];
    for (unsigned i = 0; i < ES2_RNG_WORD_COUNT; ++i)
        put16(seed + 2 * i, (uint16_t)(0xef01u + i * 1297u));
    es2_rng_init(state, seed);
}

/* Our separate numeric-array model derives only from rng.md. It decodes and
 * encodes the complete table rather than sharing the core's byte update. */
static uint32_t __cdecl model_next(void *memory) {
    BYTE *state = memory;
    uint32_t table[ES2_RNG_WORD_COUNT];
    unsigned source = state[0x70], destination = state[0x71];
    for (unsigned i = 0; i < ES2_RNG_WORD_COUNT; ++i)
        table[i] = (uint32_t)state[2 * i] + 256u * state[2 * i + 1];
    table[destination] = (table[destination] + table[source]) % 65536u;
    for (unsigned i = 0; i < ES2_RNG_WORD_COUNT; ++i)
        put16(state + 2 * i, (uint16_t)table[i]);
    state[0x70] = (BYTE)((source + 1) % ES2_RNG_WORD_COUNT);
    state[0x71] = (BYTE)((destination + 1) % ES2_RNG_WORD_COUNT);
    ++model_calls;
    return table[destination];
}

static uint32_t __cdecl model_bounded(void *state, uint32_t argument) {
    int32_t bound = (int16_t)(uint16_t)argument;
    return (uint32_t)((int32_t)(model_next(state) % 32768u) % bound);
}

static BYTE *make_image(void) {
    BYTE *image = VirtualAlloc(NULL, RNG_HOOK_IMAGE_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!image) return NULL;
    /* A minimal, entirely synthetic PE header for the framework's checks. */
    put16(image, IMAGE_DOS_SIGNATURE);
    put32(image + 0x3c, 0x80);
    put32(image + 0x80, IMAGE_NT_SIGNATURE);
    put16(image + 0x84, IMAGE_FILE_MACHINE_I386);
    put16(image + 0x98, IMAGE_NT_OPTIONAL_HDR32_MAGIC);
    put32(image + 0x98 + 28, (uint32_t)(uintptr_t)image);
    put32(image + 0x98 + 56, RNG_HOOK_IMAGE_SIZE);

    /* Own x86 stubs intentionally use the documented prologue forms. They
     * restore the stack and tail-call the own C model instead of game code.
     * next: push ebp; mov ebp,esp; mov eax,[ebp+8]; movsx edx,[eax+0x70];
     *       pop ebp; jmp model_next.
     * bounded: push ebp; mov ebp,esp; push ebx; mov ebx,[ebp+8]; push ebx;
     *          add esp,4; pop ebx; pop ebp; jmp model_bounded. */
    static const BYTE next_code[] = {
        0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08, 0x0f, 0xbe, 0x50, 0x70, 0x5d, 0xe9,
    };
    static const BYTE bounded_code[] = {
        0x55, 0x8b, 0xec, 0x53, 0x8b, 0x5d, 0x08, 0x53,
        0x83, 0xc4, 0x04, 0x5b, 0x5d, 0xe9,
    };
    BYTE *next = image + rng_hook_descriptors[0].rva;
    BYTE *bounded = image + rng_hook_descriptors[1].rva;
    memcpy(next, next_code, sizeof(next_code));
    memcpy(bounded, bounded_code, sizeof(bounded_code));
    put32(next + sizeof(next_code), (uint32_t)(uintptr_t)model_next - (uint32_t)(uintptr_t)(next + sizeof(next_code) + 4));
    put32(bounded + sizeof(bounded_code), (uint32_t)(uintptr_t)model_bounded - (uint32_t)(uintptr_t)(bounded + sizeof(bounded_code) + 4));
    if (!FlushInstructionCache(GetCurrentProcess(), image, RNG_HOOK_IMAGE_SIZE)) {
        VirtualFree(image, 0, MEM_RELEASE);
        return NULL;
    }
    return image;
}

static int adapters_case(void) {
    _Alignas(uint32_t) BYTE guarded[ES2_RNG_STATE_SIZE + 2];
    BYTE expected[ES2_RNG_STATE_SIZE];
    BYTE *state = guarded + 1;
    uint32_t args[ES2_HOOK_MAX_ARGS] = {(uint32_t)(uintptr_t)state, 0, 0, 0};
    memset(guarded, 0xa7, sizeof(guarded));
    seed_state(state);
    memcpy(expected, state, sizeof(expected));
    CHECK(((uintptr_t)state & 1u) != 0);
    CHECK(rng_hook_next(args, NULL) == model_next(expected));
    CHECK(memcmp(state, expected, sizeof(expected)) == 0);
    args[1] = UINT32_C(0xffff0011); /* High bits ignored; low signed word is +17. */
    CHECK(rng_hook_bounded(args, NULL) == model_bounded(expected, args[1]));
    CHECK(memcmp(state, expected, sizeof(expected)) == 0);
    args[1] = UINT32_C(0x89ab7fff);
    CHECK(rng_hook_bounded(args, NULL) == model_bounded(expected, args[1]));
    CHECK(memcmp(state, expected, sizeof(expected)) == 0);
    CHECK(guarded[0] == 0xa7 && guarded[sizeof(guarded) - 1] == 0xa7);
    return 0;
}

static int disabled_case(void) {
    es2_hook before[2];
    es2_hook_log log_before;
    memcpy(before, rng_hooks, sizeof(before));
    memcpy(&log_before, &rng_hook_log, sizeof(log_before));
    CHECK(install_rng_hooks()); /* Completely absent [hooks] section. */
    CHECK(events == 0 && !rng_hooks_configured && rng_hook_image == NULL);
    CHECK(memcmp(before, rng_hooks, sizeof(before)) == 0);
    CHECK(memcmp(&log_before, &rng_hook_log, sizeof(log_before)) == 0);
    CHECK(WritePrivateProfileStringW(L"hooks", L"mismatch_log", L"Z:\\missing\\must-not-create.jsonl", config_path));
    CHECK(install_rng_hooks());
    CHECK(events == 0 && !rng_hooks_configured && rng_hook_image == NULL);
    CHECK(memcmp(before, rng_hooks, sizeof(before)) == 0);
    CHECK(memcmp(&log_before, &rng_hook_log, sizeof(log_before)) == 0);
    CHECK(WritePrivateProfileStringW(L"hooks", L"rng_next", L"original", config_path));
    CHECK(WritePrivateProfileStringW(L"hooks", L"rng_bounded", L"original", config_path));
    CHECK(install_rng_hooks());
    CHECK(events == 0 && !rng_hooks_configured);
    return 0;
}

static int enabled_refused_case(void) {
    CHECK(WritePrivateProfileStringW(L"hooks", L"rng_next", L"compare", config_path));
    CHECK(!install_rng_hooks()); /* This own fixture is not named DBSIM.EXE. */
    CHECK(!rng_hooks_configured && rng_hook_image == NULL);
    CHECK(rng_hooks[0].installed == 0 && rng_hooks[1].installed == 0);
    CHECK(rng_hook_log.file == NULL);
    CHECK(WritePrivateProfileStringW(L"hooks", L"rng_next", L"bogus", config_path));
    CHECK(!install_rng_hooks());
    CHECK(events == 1);
    return 0;
}

static int limit_case(void) {
    static const WCHAR *invalid[] = {L"", L"-1", L"+1", L"1x", L"1 2", L"4294967296", L"999999999999999999999999999999"};
    uint32_t limit = 99;
    CHECK(rng_hook_limit(config_path, &limit) && limit == 32);
    CHECK(WritePrivateProfileStringW(L"hooks", L"mismatch_limit", L"0", config_path));
    CHECK(rng_hook_limit(config_path, &limit) && limit == 0);
    CHECK(WritePrivateProfileStringW(L"hooks", L"mismatch_limit", L"4294967295", config_path));
    CHECK(rng_hook_limit(config_path, &limit) && limit == UINT32_MAX);
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        limit = 17;
        CHECK(WritePrivateProfileStringW(L"hooks", L"mismatch_limit", invalid[i], config_path));
        CHECK(!rng_hook_limit(config_path, &limit));
        CHECK(limit == 17);
    }
    return 0;
}

static int counters_case(void) {
    char text[1024];
    memset(text, 'Q', sizeof(text));
    CHECK(rng_hooks_tick_fields(text, sizeof(text)) == 0 && text[0] == 'Q');
    rng_hooks_configured = TRUE;
    rng_hooks[0].calls = UINT64_C(4294967301);
    rng_hooks[0].mismatches = 2;
    rng_hooks[0].skipped = 3;
    rng_hooks[0].bypassed = 4;
    rng_hooks[1].calls = 5;
    rng_hooks[1].mismatches = 6;
    rng_hooks[1].skipped = 7;
    rng_hooks[1].bypassed = 8;
    int written = rng_hooks_tick_fields(text, sizeof(text));
    CHECK(written == (int)strlen(text));
    CHECK(!strcmp(text,
        ",\"hk_rng_next_calls\":4294967301,\"hk_rng_next_mismatches\":2,\"hk_rng_next_skipped\":3,\"hk_rng_next_bypassed\":4"
        ",\"hk_rng_bounded_calls\":5,\"hk_rng_bounded_mismatches\":6,\"hk_rng_bounded_skipped\":7,\"hk_rng_bounded_bypassed\":8"));
    text[16] = 'R';
    CHECK(rng_hooks_tick_fields(text, 16) == 0);
    CHECK(text[15] == 0 && text[16] == 'R');
    /* The same omission rule applies to both legacy and later hooks. */
    for(unsigned i=0;i<2;++i) {
        rng_hooks[i].options.mode=ES2_HOOK_REPLACED;
        rng_hooks[i].mismatches=rng_hooks[i].skipped=0;
    }
    CHECK(rng_hooks_tick_fields(text,sizeof(text))>0);
    CHECK(!strstr(text,"mismatches") && !strstr(text,"skipped"));
    CHECK(strstr(text,"hk_rng_next_calls") && strstr(text,"hk_rng_bounded_bypassed"));
    rng_hooks[0].options.mode=ES2_HOOK_COMPARE;
    rng_hooks[1].mismatches=1;
    CHECK(rng_hooks_tick_fields(text,sizeof(text))>0);
    CHECK(strstr(text,"\"hk_rng_next_mismatches\":0") && strstr(text,"\"hk_rng_next_skipped\":0"));
    CHECK(strstr(text,"\"hk_rng_bounded_mismatches\":1") && strstr(text,"\"hk_rng_bounded_skipped\":0"));
    rng_hooks_shutdown();
    return 0;
}

static int descriptor_case(es2_hook_mode mode) {
    BYTE *image = make_image();
    CHECK(image != NULL);
    typedef uint32_t (__cdecl *next_function)(void *);
    typedef uint32_t (__cdecl *bounded_function)(void *, uint32_t);
    next_function next = (next_function)(uintptr_t)(image + rng_hook_descriptors[0].rva);
    bounded_function bounded = (bounded_function)(uintptr_t)(image + rng_hook_descriptors[1].rva);
    BYTE original[2][16];
    es2_hook_options options = {0};
    options.image = image;
    options.expected_base = (uintptr_t)image;
    options.expected_image_size = RNG_HOOK_IMAGE_SIZE;
    options.mode = mode;
    CHECK(rng_hook_descriptors[0].relocate_size == 6);
    CHECK(rng_hook_descriptors[1].relocate_size == 7);
    for (unsigned i = 0; i < 2; ++i) {
        const es2_hook_descriptor *descriptor = &rng_hook_descriptors[i];
        CHECK(descriptor->span_count == 1 && descriptor->spans[0].arg_index == 0);
        CHECK(descriptor->spans[0].offset == 0 && descriptor->spans[0].size == 114);
        CHECK(es2_hook_prologue_valid(descriptor->expected, descriptor->relocate_size,
                                      descriptor->instruction_lengths, descriptor->instruction_count));
        CHECK(memcmp(image + descriptor->rva, descriptor->expected, descriptor->expected_size) == 0);
        memcpy(original[i], image + descriptor->rva, sizeof(original[i]));
        CHECK(es2_hook_install(&rng_hooks[i], descriptor, &options));
        CHECK(image[descriptor->rva] == 0xe9);
    }
    _Alignas(uint32_t) BYTE guarded[ES2_RNG_STATE_SIZE + 2];
    BYTE expected[ES2_RNG_STATE_SIZE];
    BYTE *state = guarded + 1;
    memset(guarded, 0x92, sizeof(guarded));
    seed_state(state);
    memcpy(expected, state, sizeof(expected));
    for (unsigned i = 0; i < 256; ++i) {
        uint32_t expected_result = model_next(expected);
        unsigned before = model_calls;
        CHECK(next(state) == expected_result);
        CHECK(model_calls == before + (mode == ES2_HOOK_COMPARE ? 1 : 0));
        CHECK(memcmp(state, expected, sizeof(expected)) == 0);
        expected_result = model_bounded(expected, UINT32_C(0xabcd0123));
        before = model_calls;
        CHECK(bounded(state, UINT32_C(0xabcd0123)) == expected_result);
        CHECK(model_calls == before + (mode == ES2_HOOK_COMPARE ? 1 : 0));
        CHECK(memcmp(state, expected, sizeof(expected)) == 0);
    }
    CHECK(guarded[0] == 0x92 && guarded[sizeof(guarded) - 1] == 0x92);
    for (unsigned i = 0; i < 2; ++i) {
        CHECK(rng_hooks[i].calls == 256 && rng_hooks[i].mismatches == 0);
        CHECK(rng_hooks[i].skipped == 0 && rng_hooks[i].bypassed == 0);
        CHECK(es2_hook_uninstall(&rng_hooks[i]));
        CHECK(memcmp(image + rng_hook_descriptors[i].rva, original[i], sizeof(original[i])) == 0);
    }
    CHECK(next(state) == model_next(expected));
    CHECK(memcmp(state, expected, sizeof(expected)) == 0);
    CHECK(VirtualFree(image, 0, MEM_RELEASE));
    return 0;
}

static int frame_case(void) {
    BYTE *image = make_image();
    CHECK(image != NULL);
    BYTE *address = image + RNG_HOOK_FRAME_RVA;
    uint32_t frame = 0;
    DWORD previous;
    put32(address, UINT32_C(0x89abcdef));
    CHECK(rng_hook_frame(image, &frame) && frame == UINT32_C(0x89abcdef));
    CHECK(VirtualProtect(address, 4, PAGE_NOACCESS, &previous));
    frame = 17;
    CHECK(!rng_hook_frame(image, &frame) && frame == 17);
    CHECK(VirtualProtect(address, 4, previous, &previous));
    CHECK(VirtualFree(image, 0, MEM_RELEASE));
    return 0;
}

static int hash_refused_case(void) {
    /* The fixture's own empty INI is intentionally not an original EXE. */
    CHECK(!rng_hook_reference_file(config_path));
    CHECK(!rng_hook_reference_file(L"Z:\\missing\\no-original-read.exe"));
    return 0;
}

int wmain(int argc, WCHAR **argv) {
    if (argc != 2 && argc != 3) return 2;
    WCHAR directory[MAX_PATH];
    if (argc == 3) {
        if (wcslen(argv[2]) >= MAX_PATH) return 2;
        wcscpy(directory, argv[2]);
    } else {
        DWORD length = GetEnvironmentVariableW(L"TEST_TMPDIR", directory, MAX_PATH);
        if (!length || length >= MAX_PATH) return 2;
    }
    int length = swprintf(config_path, MAX_PATH, L"%ls\\rng-hook-%ls-%lu.ini", directory, argv[1], (unsigned long)GetCurrentProcessId());
    if (length < 0 || length >= MAX_PATH) return 2;
    HANDLE ini = CreateFileW(config_path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (ini == INVALID_HANDLE_VALUE) return 2;
    CloseHandle(ini);
    int result;
    if (!wcscmp(argv[1], L"adapters")) result = adapters_case();
    else if (!wcscmp(argv[1], L"disabled")) result = disabled_case();
    else if (!wcscmp(argv[1], L"enabled_refused")) result = enabled_refused_case();
    else if (!wcscmp(argv[1], L"limit")) result = limit_case();
    else if (!wcscmp(argv[1], L"counters")) result = counters_case();
    else if (!wcscmp(argv[1], L"descriptor_compare")) result = descriptor_case(ES2_HOOK_COMPARE);
    else if (!wcscmp(argv[1], L"descriptor_replaced")) result = descriptor_case(ES2_HOOK_REPLACED);
    else if (!wcscmp(argv[1], L"frame")) result = frame_case();
    else if (!wcscmp(argv[1], L"hash_refused")) result = hash_refused_case();
    else result = 2;
    if (!result) printf("rng-hooks fixture PASS\n");
    return result;
}
