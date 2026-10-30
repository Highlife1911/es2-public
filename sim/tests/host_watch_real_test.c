/* The own process's memory watches on the real image, in-process (no game data
 * needed): claim 0x400000 before the CRT, build the image from the own tables,
 * refuse a malformed [trace] watch at set-up (exit 67, nothing else set up),
 * then set up with three watches and a tick log and write tick records by hand
 * (the replay does not reach the frame boundary yet): the start record's watch
 * count, whole records, an unreadable pointer chain becoming readable, silence
 * without a change, the frame number of the counter before its increment, and
 * the events after their tick record. Guest bytes changed here are restored. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/sim/host.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static BYTE *reserved;
static DWORD reservation_error;
static int reservation_in_stack;
extern void __cdecl mainCRTStartup(void);
#include "tools/reference-game/invitro/startup_reservation.h"
void __cdecl es2sim_start(void);
void __cdecl es2sim_start(void) {
    MEMORY_BASIC_INFORMATION here = {0};
    VirtualQuery(&here, &here, sizeof(here));
    reserved = claim_image_range(here.AllocationBase, &reservation_error, &reservation_in_stack);
    mainCRTStartup();
    ExitProcess(3);
}

static es2_sim sim;
static char text[1 << 16];

static int write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    size_t n = strlen(content);
    int bad = fwrite(content, 1, n, f) != n;
    return fclose(f) || bad;
}

#define FRAME_VA (ES2_SIM_VA_TICK_CONTEXT + 0xbfu)   /* 0x4d257b */

int main(void) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    const char *tmp = getenv("TEST_TMPDIR");
    char ini[MAX_PATH], log[MAX_PATH];
    if (!reserved) { fprintf(stderr, "FAIL: 0x400000 not claimed (Win32 %lu)\n", (unsigned long)reservation_error); return 1; }
    CHECK(tmp && *tmp);
    snprintf(ini, sizeof(ini), "%s\\watch.ini", tmp);
    snprintf(log, sizeof(log), "%s\\watch.jsonl", tmp);
    wchar_t ini_w[MAX_PATH], log_w[MAX_PATH];
    CHECK(MultiByteToWideChar(CP_ACP, 0, ini, -1, ini_w, MAX_PATH) && MultiByteToWideChar(CP_ACP, 0, log, -1, log_w, MAX_PATH));
    DeleteFileA(log);
    int code = es2_sim_map(&sim, reserved);
    if (code) { fprintf(stderr, "FAIL: map: %s\n", sim.error); return 1; }

    /* Refusals: a bad name, a missing entry, too many watches. */
    static const char *bad[] = {
        "[trace]\r\nwatch_count=1\r\nwatch1=bad-name 0x4d27b8 4\r\n",
        "[trace]\r\nwatch_count=2\r\nwatch1=exit 0x4d27b8 4\r\n",
        "[trace]\r\nwatch_count=49\r\n",
    };
    for (unsigned i = 0; i < 3; ++i) {
        CHECK(!write_file(ini, bad[i]));
        code = es2_sim_setup(&sim, ini_w, log_w);
        CHECK(code == ES2_SIM_EXIT_SETUP && !strcmp(sim.error, "[trace] watch refused") && sim.watches.count == 0);
        CHECK(!sim.log && GetFileAttributesA(log) == INVALID_FILE_ATTRIBUTES && sim.dispatch.count == 0);
    }

    CHECK(!write_file(ini, "[trace]\r\nwatch_count=3\r\nwatch1=frame 0x4d257b 4\r\nwatch2=exit 0x4d27b8 4\r\n"
                           "watch3=chain 0x4d27b8@2 6\r\n"));
    code = es2_sim_setup(&sim, ini_w, log_w);
    if (code) { fprintf(stderr, "FAIL: setup: %s\n", sim.error); return 1; }
    CHECK(sim.watches.count == 3 && sim.log);

    uint32_t frame_saved = sim.memory.read32(sim.memory.context, FRAME_VA);
    uint32_t exit_saved = sim.memory.read32(sim.memory.context, ES2_SIM_VA_EXIT_CODE);
    static uint8_t target[8] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17};
    sim.memory.write32(sim.memory.context, FRAME_VA, 0x2a);
    sim.memory.write32(sim.memory.context, ES2_SIM_VA_EXIT_CODE, 0);
    es2_sim_tick(&sim, NULL);                                   /* 1: frame, exit whole; chain unreadable */
    es2_sim_tick(&sim, NULL);                                   /* 2: nothing changed */
    sim.memory.write32(sim.memory.context, FRAME_VA, 0x2b);
    uint32_t pointer = (uint32_t)(uintptr_t)target;
    sim.memory.write32(sim.memory.context, ES2_SIM_VA_EXIT_CODE, pointer);
    es2_sim_tick(&sim, NULL);                                   /* 3: all three */
    sim.memory.write32(sim.memory.context, FRAME_VA, frame_saved);
    sim.memory.write32(sim.memory.context, ES2_SIM_VA_EXIT_CODE, exit_saved);
    es2_sim_finish(&sim, "test", 0);
    es2_sim_trap_remove();

    FILE *f = fopen(log, "rb");
    CHECK(f);
    size_t got = fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    text[got] = 0;
    CHECK(strstr(text, "\"mode\":\"own_process_headless\"") && strstr(text, "\"watches\":3}\r\n"));
    char expected[1024];
    snprintf(expected, sizeof(expected),
        "{\"event\":\"watch\",\"sequence\":1,\"frame\":42,\"name\":\"frame\",\"address\":\"0x004d257b\",\"length\":4,\"hex\":\"2a000000\"}\r\n"
        "{\"event\":\"watch\",\"sequence\":1,\"frame\":42,\"name\":\"exit\",\"address\":\"0x004d27b8\",\"length\":4,\"hex\":\"00000000\"}\r\n"
        "{\"event\":\"watch\",\"sequence\":1,\"frame\":42,\"name\":\"chain\",\"unreadable\":true}\r\n"
        "{\"event\":\"tick\",\"sequence\":2,");
    char *one = strstr(text, "{\"event\":\"tick\",\"sequence\":1,");
    CHECK(one);
    char *after = strstr(one, "}\r\n");
    CHECK(after && !strncmp(after + 3, expected, strlen(expected)));
    char *two = after + 3 + strlen(expected) - strlen("{\"event\":\"tick\",\"sequence\":2,");
    char *three = strstr(two, "}\r\n");
    CHECK(three && !strncmp(three + 3, "{\"event\":\"tick\",\"sequence\":3,", strlen("{\"event\":\"tick\",\"sequence\":3,")));   /* tick 2: no watch lines */
    three = strstr(three + 3, "}\r\n");
    CHECK(three);
    char hex[9];
    for (unsigned i = 0; i < 4; ++i) snprintf(hex + 2 * i, 3, "%02x", (unsigned)((pointer >> (8 * i)) & 0xffu));
    snprintf(expected, sizeof(expected),
        "{\"event\":\"watch\",\"sequence\":3,\"frame\":43,\"name\":\"frame\",\"address\":\"0x004d257b\",\"length\":4,\"hex\":\"2b000000\"}\r\n"
        "{\"event\":\"watch\",\"sequence\":3,\"frame\":43,\"name\":\"exit\",\"address\":\"0x004d27b8\",\"length\":4,\"hex\":\"%s\"}\r\n"
        "{\"event\":\"watch\",\"sequence\":3,\"frame\":43,\"name\":\"chain\",\"address\":\"0x%08lx\",\"length\":6,\"hex\":\"121314151617\"}\r\n"
        "{\"event\":\"exit\",\"reason\":\"test\",", hex, (unsigned long)(pointer + 2u));
    CHECK(!strncmp(three + 3, expected, strlen(expected)));
    printf("PASS: es2sim watches on the real image: 3 set-up refusals, start record watches 3, 7 watch events after 3 tick records\n");
    return 0;
}
