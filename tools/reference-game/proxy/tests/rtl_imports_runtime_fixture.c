/* The import shim's runtime check (rtl_runtime_check) and exit-record
 * fields (proxy/rtl_imports.h) with the own cw3220.dll (//rtl:cw3220.dll,
 * argv[1]) loaded into the test process: the modes that work on CW3220.DLL's
 * internals are refused as own_runtime, the forwarding modes pass, and the
 * exit record names the runtime, the original's absence and the DLL's
 * counters. A module that is neither (kernel32) is refused as cw_hash for
 * the internal modes. No game code and no original DLL runs. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ES2_RTL_NO_PROCESS_ENTRY
#include "../rtl_imports.h"
static void rtl_arm(void) {}

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

/* A data file's runfiles path (Windows tests get a manifest). */
static const char *resolve(const char *path) {
    static char out[MAX_PATH];
    const char *manifest = getenv("RUNFILES_MANIFEST_FILE");
    FILE *stream = manifest && *manifest ? fopen(manifest, "rb") : NULL;
    if (!stream) return path;
    char line[4096], key[MAX_PATH + 8];
    snprintf(key, sizeof(key), "_main/%s ", path);
    size_t n = strlen(key);
    const char *found = path;
    while (fgets(line, sizeof(line), stream)) {
        if (strncmp(line, key, n)) continue;
        snprintf(out, sizeof(out), "%s", line + n);
        out[strcspn(out, "\r\n")] = 0;
        found = out;
        break;
    }
    fclose(stream);
    return found;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    HMODULE own = LoadLibraryA(resolve(argv[1]));
    CHECK(own != NULL);
    static char line[16384];
    /* Before the arming checked anything: no runtime fields. */
    rtl_state = RTL_REFUSED;
    snprintf(rtl_reason, sizeof(rtl_reason), "hash");
    CHECK(rtl_tick_fields(line, sizeof(line), TRUE) > 0 && !strcmp(line, ",\"rtl_refused\":\"hash\""));
    /* The own DLL with an internal mode requested: refused as own_runtime. */
    CHECK(!rtl_runtime_check(own, TRUE) && !strcmp(rtl_reason, "own_runtime") && rtl_own_runtime && rtl_runtime_known);
    CHECK(rtl_own_runtime->mode == ES2_OWN_MODE_OWN && rtl_own_runtime->image_base == (uint32_t)(uintptr_t)own);
    rtl_state = RTL_REFUSED;
    CHECK(rtl_tick_fields(line, sizeof(line), TRUE) > 0);
    CHECK(!strncmp(line, ",\"rtl_refused\":\"own_runtime\",\"rtl_runtime\":\"own\",\"rtl_runtime_original_loaded\":0,\"rtl_own_calls\":0,", 99));
    CHECK(strstr(line, ",\"rtl_own_delegated\":0,\"rtl_own_exit_status\":-1"));
    /* A tick (not the exit record) carries only the refusal. */
    CHECK(rtl_tick_fields(line, sizeof(line), FALSE) > 0 && !strcmp(line, ",\"rtl_refused\":\"own_runtime\""));
    /* The forwarding modes: no refusal; calls of the DLL appear in its counters. */
    rtl_reason[0] = 0;
    CHECK(rtl_runtime_check(own, FALSE) && !rtl_reason[0] && rtl_own_runtime);
    typedef unsigned (__cdecl *strlen_fn)(const char *);
    strlen_fn own_strlen = (strlen_fn)(void *)GetProcAddress(own, "_strlen");
    CHECK(own_strlen && own_strlen("four") == 4);
    rtl_state = RTL_INSTALLED;
    memset(rtl_installed, 0, sizeof(rtl_installed));
    int n = rtl_tick_fields(line, sizeof(line), TRUE);
    CHECK(n > 0 && strstr(line, ",\"rtl_runtime\":\"own\",\"rtl_runtime_original_loaded\":0,\"rtl_own_calls\":1,\"rtl_own_foreign_calls\":0,"));
    /* Too small a buffer for the runtime fields: the record reports no fit. */
    CHECK(rtl_tick_fields(line, (size_t)n - 10u, TRUE) == 0);
    /* Not the own DLL: the internal modes need the reference file. */
    HMODULE kernel = GetModuleHandleA("kernel32.dll");
    CHECK(!rtl_runtime_check(kernel, TRUE) && !strcmp(rtl_reason, "cw_hash") && !rtl_own_runtime);
    CHECK(rtl_tick_fields(line, sizeof(line), TRUE) > 0 && strstr(line, ",\"rtl_runtime\":\"cw3220\"") && !strstr(line, "rtl_own_"));
    CHECK(rtl_runtime_check(kernel, FALSE));
    CHECK(GetModuleHandleA(ES2_OWN_ORIGINAL_FILE) == NULL);
    puts("PASS rtl_imports runtime: own cw3220.dll detected; internal modes refused as own_runtime (cw_hash for another module); "
         "exit-record fields rtl_runtime, rtl_runtime_original_loaded and the DLL's counters");
    return 0;
}
