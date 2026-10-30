/* The host on the real image, in-process: claim 0x400000 before the CRT, build
 * the image from the own tables (no DBSIM.EXE, no game data needed), set up,
 * and call CODE VAs through the sim backend (es2_machine_sim_enter): rng_next
 * 0x492ee0 (a registry adapter) against core, the _memset thunk, GetTickCount,
 * operator new/delete with [0x6bb30c] (init record 53's numbers), refusals of
 * an unmapped VA and of an import slot, the frame record refused without the
 * c0w32 entry's [0x6cbee4]; then the cost per call on a real adapter.
 * CODE is never executable; read-only only on the pages of the own CODE_TABLE
 * runs (the seven measured table sites), no-access elsewhere, so a stray read
 * fails. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/sim/host.h"
#include "es2/sim/image_access.h"
#include "es2/sim/image_data.h"
#include "es2/sim/machine_sim.h"
#include "es2/core/rng.h"

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
static es2_sim_refusal last;
static unsigned refusals;
static es2_sim_refuse_fn host_refuse;
static void on_refuse(const es2_sim_refusal *r, void *user) {
    last = *r;
    ++refusals;
    host_refuse(r, user);          /* the host's log line (stderr) */
}

static es2_sim_result r;
static uint32_t caller_frame[4];
/* `va` with `count` slots, EAX = `eax` and the caller's EBP at a zeroed frame
 * (es2_sim_call's path with an EBP); 0, or 1 when refused. */
static int call(uint32_t va, unsigned count, const uint32_t *slots, uint32_t eax) {
    static const uint32_t none[2];
    const uint32_t first = (uint32_t)(uintptr_t)(slots ? slots : none + 1);
    es2_machine m = {eax, 0, 0, 0, first - 4u, (uint32_t)(uintptr_t)caller_frame, 0, 0};
    memset(&r, 0, sizeof(r));
    return es2_machine_sim_enter(&sim.dispatch, &m, va, count, &r);
}

static int run(void) {
    CHECK(reserved == (BYTE *)(uintptr_t)0x400000u);
    int code = es2_sim_map(&sim, reserved);
    if (code) { fprintf(stderr, "FAIL: map: %s\n", sim.error); return 1; }
    code = es2_sim_setup(&sim, NULL, NULL);
    if (code) { fprintf(stderr, "FAIL: setup: %s\n", sim.error); return 1; }
    host_refuse = sim.dispatch.refuse;
    sim.dispatch.refuse = on_refuse;
    MEMORY_BASIC_INFORMATION m;
    CHECK(VirtualQuery((void *)(uintptr_t)0x492ee0u, &m, sizeof(m)) && m.Protect == PAGE_NOACCESS);
    CHECK(VirtualQuery((void *)(uintptr_t)0x4d27b8u, &m, sizeof(m)) && m.Protect == PAGE_READWRITE);
    CHECK(VirtualQuery((void *)(uintptr_t)0x400000u, &m, sizeof(m)) && m.Protect == PAGE_READONLY);
    /* Every CODE page is read-only exactly when it holds one of the seven measured
     * table sites (= the CODE_TABLE runs), else no-access; a read of a
     * no-access page fails (here without a fault). */
    unsigned readable = 0;
    for (uint32_t page = sim.info.code_lo; page < sim.info.code_hi; page += 0x1000u) {
        int site = 0;
        for (unsigned i = 0; i < ES2_SIM_IMAGE_TABLE_SITES; ++i) site |= (es2_sim_image_table_sites[i] & ~0xfffu) == page;
        CHECK(VirtualQuery((void *)(uintptr_t)page, &m, sizeof(m)) && m.State == MEM_COMMIT);
        CHECK(m.Protect == (DWORD)(site ? PAGE_READONLY : PAGE_NOACCESS));
        CHECK(es2_sim_code_page_protection(page) == m.Protect);
        readable += site;
    }
    CHECK(readable == ES2_SIM_IMAGE_TABLE_SITES);
    uint32_t probe_word = 0;
    SIZE_T got = 0;
    CHECK(!ReadProcessMemory(GetCurrentProcess(), (void *)(uintptr_t)0x0047cf71u, &probe_word, 4, &got) && got == 0);
    /* The table bytes and the init records are there; the RTTI counts come
     * from the own runs (0x4836e0: the most read one). */
    CHECK(*(const uint8_t *)(uintptr_t)0x004967aeu != 0 && *(const uint32_t *)(uintptr_t)0x004a79d8u != 0);
    uint32_t rtti = 0;
    CHECK(!es2_sim_dbsim_rtti_count(0x004836e0u, &rtti) && es2_sim_dbsim_rtti_count(0x004836e4u, &rtti));
    CHECK(sim.import_count == 134 && es2_sim_exit_code(&sim) == 0);

    /* rng_next: the adapter runs, core gives the same bytes. */
    static uint8_t state[ES2_RNG_STATE_SIZE], copy[ES2_RNG_STATE_SIZE];
    for (unsigned i = 0; i < 0x70; ++i) state[i] = (uint8_t)(i * 37 + 11);
    state[0x70] = 24; state[0x71] = 55;
    memcpy(copy, state, sizeof(state));
    uint32_t arg = (uint32_t)(uintptr_t)state;
    CHECK(!call(0x00492ee0u, 1, &arg, 0));
    uint32_t expected = (uint32_t)es2_rng_next(copy);
    CHECK(r.eax == expected && !memcmp(state, copy, sizeof(state)) && r.pop_bytes == 0);
    const es2_sim_entry *e = es2_sim_dispatch_find(&sim.dispatch, 0x00492ee0u);
    CHECK(e && e->kind == ES2_SIM_HOOK && e->calls == 1 && !strcmp(e->name, "rng_next"));

    /* _memset through its thunk: CW3220's registers. */
    static uint8_t buffer[16];
    uint32_t ms[3] = {(uint32_t)(uintptr_t)buffer, 0x7f, 9};
    CHECK(!call(0x0049681au, 3, ms, 0));
    CHECK(r.eax == ms[0] && r.ecx == 0 && r.edx == 9 && buffer[8] == 0x7f && buffer[9] == 0);

    /* GetTickCount: virtual time starts at 16,777,216. */
    CHECK(!call(0x004968ecu, 0, NULL, 0));
    CHECK(r.eax == 0x01000000u && r.pop_bytes == 0);

    /* operator new(0x808), as init record 53 asks: header 0x811 at p-4,
     * [0x6bb30c] = 0x811, zero-filled, at the preferred heap base; delete gives
     * [0x6bb30c] = 0 again. */
    uint32_t n = 0x808;
    CHECK(!call(0x0047749cu, 1, &n, 0));
    uint32_t block = r.eax;
    CHECK(block && r.ecx == 0x808 && r.edx == 0x811 && *(uint32_t *)(uintptr_t)(block - 4) == 0x811u);
    CHECK(*(uint32_t *)(uintptr_t)0x6bb30cu == 0x811u && block >= ES2_SIM_HEAP_PREFERRED_BASE);
    for (uint32_t i = 0; i < 0x808; ++i) CHECK(((uint8_t *)(uintptr_t)block)[i] == 0);
    CHECK(!call(0x00477548u, 1, &block, 0));
    CHECK(r.ecx == block && *(uint32_t *)(uintptr_t)0x6bb30cu == 0);

    /* Refusals: the c0w32 entry (unmapped), an import slot, the frame record. */
    CHECK(call(0x00401000u, 0, NULL, 0) == 1);
    CHECK(last.va == 0x00401000u && !last.name && !strcmp(last.reason, "unmapped"));
    CHECK(call(0x006cc40cu, 0, NULL, 0) == 1);                           /* USER32 PeekMessageA's slot */
    CHECK(last.va == 0x006cc40cu && last.name && strstr(last.name, "PeekMessageA"));
    CHECK(call(0x0049630cu, 0, NULL, 0x0049f558u) == 1);
    CHECK(last.va == 0x0049630cu && strstr(last.reason, "0x6cbee4"));
    CHECK(refusals == 3);

    /* The cost per call on a real adapter (rng_next): the sim backend against
     * direct calls of the core function. */
    const unsigned count = 100000;
    LARGE_INTEGER f, t0, t1, t2;
    QueryPerformanceFrequency(&f);
    uint32_t acc = 0;
    QueryPerformanceCounter(&t0);
    for (unsigned i = 0; i < count; ++i) { call(0x00492ee0u, 1, &arg, 0); acc += r.eax; }
    QueryPerformanceCounter(&t1);
    for (unsigned i = 0; i < count; ++i) acc += (uint32_t)es2_rng_next(copy);
    QueryPerformanceCounter(&t2);
    CHECK(!memcmp(state, copy, sizeof(state)));
    printf("CALL COST (rng_next 0x492ee0): %.0f ns per sim backend call, %.1f ns per direct core call (%u calls; acc %u)\n",
           (double)(t1.QuadPart - t0.QuadPart) * 1e9 / (double)f.QuadPart / count,
           (double)(t2.QuadPart - t1.QuadPart) * 1e9 / (double)f.QuadPart / count, count, acc);
    CHECK(es2_sim_trap_calls() == 0);
    puts("PASS: host on the real image: adapter, thunks, heap, refusals; no trapped call");
    return 0;
}

static DWORD WINAPI worker(void *a) { (void)a; return (DWORD)run(); }

int main(void) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (!reserved) { fprintf(stderr, "FAIL: 0x400000 not claimed (Win32 %lu)\n", (unsigned long)reservation_error); return 1; }
    HANDLE h = CreateThread(NULL, 0x1000000, worker, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (!h) return 1;
    DWORD result = 1;
    WaitForSingleObject(h, INFINITE);
    GetExitCodeThread(h, &result);
    CloseHandle(h);
    return (int)result;
}
