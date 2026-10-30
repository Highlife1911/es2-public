#ifndef ES2_HOOK_INSTALL_H
#define ES2_HOOK_INSTALL_H
/* The proxy's installation of the registry hooks over the x86 framework
 * (hook/hook.h): the [hooks] modes of the proxy INI, the image and file
 * checks of DBSIM.EXE, the mismatch log and the per-hook tick fields.
 * Included by rng_hooks.h after the adapter headers; es2sim does not
 * include it. */
#include <wincrypt.h>
#include "hook/hook.h"
#include "tools/reference-game/proxy/entry_shims_x86.h"
#include "tools/reference-game/proxy/actor_pass_snapshot.h"

#define RNG_HOOK_IMAGE_SIZE 0x002d8000u
#define RNG_HOOK_FRAME_RVA 0x000d257bu

/* Shared registry/log so mixed RNG, geometry and timing selections install
 * transactionally and use one CREATE_NEW mismatch file and one record cap. */
static es2_hook rng_hooks[FUNCTION_HOOK_COUNT];
static es2_hook_log rng_hook_log;
static BOOL rng_hooks_configured;
static BYTE *rng_hook_image;

/* After the install: the shim's stub or the group's runtime record
 * (entry_shims_x86.h). */
static BOOL weapons_install_hook(es2_hook *h,const es2_hook_descriptor *d,const es2_hook_options *o,const es2_hook_extension *e) {
    if(!es2_hook_install_ex(h,d,o,e))return FALSE;
    if(weapons_capture_entry(h))return TRUE;
    (void)es2_hook_uninstall(h);return FALSE;
}

static BOOL __cdecl rng_hook_frame(void *user, uint32_t *frame) {
    const BYTE *image = user;
    const BYTE *address = image + RNG_HOOK_FRAME_RVA;
    MEMORY_BASIC_INFORMATION region;
    if (!VirtualQuery(address, &region, sizeof(region)) || region.State != MEM_COMMIT ||
        (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
        (uintptr_t)address + 4 > (uintptr_t)region.BaseAddress + region.RegionSize) return FALSE;
    DWORD protection = region.Protect & 0xff;
    if (protection != PAGE_READONLY && protection != PAGE_READWRITE && protection != PAGE_WRITECOPY &&
        protection != PAGE_EXECUTE_READ && protection != PAGE_EXECUTE_READWRITE &&
        protection != PAGE_EXECUTE_WRITECOPY) return FALSE;
    memcpy(frame, address, sizeof(*frame));
    return TRUE;
}

/* Hash the unchanged executable on disk outside DllMain. PE/site validation
 * separately checks the loaded image; earlier own IAT/tick hooks are allowed.
 */
static BOOL rng_hook_reference_file(const WCHAR *path) {
    static const char expected[] = "8561aa47e5bb1bf1f837044b08e5d807e8de492784331708a7ddeabf6d44c4ad";
    static const char digits[] = "0123456789abcdef";
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    LARGE_INTEGER size;
    BOOL ok = GetFileSizeEx(file, &size) && size.QuadPart == 724512 &&
        CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) &&
        CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash);
    BYTE buffer[16384], digest[32];
    DWORD got = 0, total = 0;
    while (ok) {
        if (!ReadFile(file, buffer, sizeof(buffer), &got, NULL)) { ok = FALSE; break; }
        if (!got) break;
        total += got;
        ok = CryptHashData(hash, buffer, got, 0);
    }
    DWORD length = sizeof(digest);
    ok = ok && total == 724512 && CryptGetHashParam(hash, HP_HASHVAL, digest, &length, 0) && length == sizeof(digest);
    if (ok) {
        char actual[65];
        for (unsigned i = 0; i < sizeof(digest); ++i) {
            actual[2 * i] = digits[digest[i] >> 4];
            actual[2 * i + 1] = digits[digest[i] & 15];
        }
        actual[64] = 0;
        ok = !strcmp(actual, expected);
    }
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    CloseHandle(file);
    return ok;
}

static BOOL rng_hook_limit(const WCHAR *ini, uint32_t *limit) {
    WCHAR value[24];
    DWORD length = GetPrivateProfileStringW(L"hooks", L"mismatch_limit", L"32", value, 24, ini);
    if (!length || length >= 23) return FALSE;
    uint32_t parsed = 0;
    for (DWORD i = 0; i < length; ++i) {
        if (value[i] < L'0' || value[i] > L'9' || parsed > (UINT32_MAX - (value[i] - L'0')) / 10u) return FALSE;
        parsed = parsed * 10u + (value[i] - L'0');
    }
    *limit = parsed;
    return TRUE;
}

static BOOL install_rng_hooks(void) {
    es2_hook_mode modes[FUNCTION_HOOK_COUNT];
    BOOL enabled = FALSE, compare = FALSE;
    for (unsigned i = 0; i < FUNCTION_HOOK_COUNT; ++i) {
        if (!es2_hook_read_mode(config_path, function_hook_descriptor(i)->name, &modes[i])) {
            log_event("rng_hooks_config_refused"); return FALSE;
        }
        enabled |= modes[i] != ES2_HOOK_ORIGINAL;
        compare |= modes[i] == ES2_HOOK_COMPARE;
    }
    if (!enabled) return TRUE;
    WCHAR executable[MAX_PATH];
    DWORD length = GetModuleFileNameW(NULL, executable, MAX_PATH);
    WCHAR *name = length && length < MAX_PATH ? wcsrchr(executable, L'\\') : NULL;
    if (!name || _wcsicmp(name + 1, L"DBSIM.EXE")) return FALSE;
    BYTE *image = (BYTE *)GetModuleHandleW(NULL);
    if (!es2_hook_image_valid(image, (uintptr_t)0x00400000u, RNG_HOOK_IMAGE_SIZE) ||
        !rng_hook_reference_file(executable)) {
        log_event("rng_hooks_image_refused"); return FALSE;
    }
    if (compare) {
        WCHAR path[MAX_PATH];
        uint32_t limit;
        length = GetPrivateProfileStringW(L"hooks", L"mismatch_log", L"", path, MAX_PATH, config_path);
        if (!length || length >= MAX_PATH - 1 || !rng_hook_limit(config_path, &limit) ||
            !es2_hook_log_open(&rng_hook_log, path, limit, rng_hook_frame, image)) {
            log_event("rng_hooks_output_refused"); return FALSE;
        }
    }
    function_hooks_set_user(image);
    rng_hook_image = image;
    for (unsigned i = 0; i < FUNCTION_HOOK_COUNT; ++i) {
        es2_hook_options options = {0};
        options.image = image;
        options.expected_base = 0x00400000u;
        options.expected_image_size = RNG_HOOK_IMAGE_SIZE;
        options.mode = modes[i];
        options.log = compare ? &rng_hook_log : NULL;
        if (!weapons_install_hook(&rng_hooks[i], function_hook_descriptor(i), &options, function_hook_extension(i))) {
            /* Each entry is independently rolled back by the framework. Also
             * undo earlier entries, retaining all static state if an OS error
             * prevents rollback. This proxy is process-lifetime, never unloaded.
             */
            BOOL restored = TRUE;
            for (unsigned j = 0; j <= i; ++j)
                if (!es2_hook_uninstall(&rng_hooks[j])) restored = FALSE;
            rng_hooks_configured = !restored;
            if (restored && compare) es2_hook_log_close(&rng_hook_log);
            log_event(restored ? "rng_hooks_install_refused" : "rng_hooks_rollback_failed");
            return FALSE;
        }
    }
    rng_hooks_configured = TRUE;
    log_event("rng_hooks_installed");
    return TRUE;
}

static int rng_hooks_tick_fields(char *out, size_t capacity) {
    if (!rng_hooks_configured) return 0;
    int n = 0;
    for (unsigned i = 0; i < FUNCTION_HOOK_COUNT; ++i) {
        if (i >= 2 && rng_hooks[i].options.mode == ES2_HOOK_ORIGINAL) continue;
        const char *name = function_hook_descriptor(i)->name;
        /* Replaced mode never compares/snapshots. Omit its two zero-only
         * diagnostics to retain the fixed tick capacity as callees grow.
         * Nonzero values and compare-mode fields remain explicit. */
        int added;
        if(rng_hooks[i].options.mode==ES2_HOOK_REPLACED && !rng_hooks[i].mismatches && !rng_hooks[i].skipped)
            added=snprintf(out+n,capacity-(size_t)n,",\"hk_%s_calls\":%llu,\"hk_%s_bypassed\":%llu",
                name,(unsigned long long)rng_hooks[i].calls,name,(unsigned long long)rng_hooks[i].bypassed);
        else added = snprintf(out + n, capacity - (size_t)n,
            ",\"hk_%s_calls\":%llu,\"hk_%s_mismatches\":%llu,\"hk_%s_skipped\":%llu,\"hk_%s_bypassed\":%llu",
            name, (unsigned long long)rng_hooks[i].calls,
            name, (unsigned long long)rng_hooks[i].mismatches,
            name, (unsigned long long)rng_hooks[i].skipped,
            name, (unsigned long long)rng_hooks[i].bypassed);
        if (added <= 0 || (size_t)added >= capacity - (size_t)n) return 0;
        n += added;
        unsigned graph_count;callee_counter *graph_counters=weapons_graph_counters(i,&graph_count);
        if(graph_count) {
            added=callee_tick_fields(out+n,capacity-(size_t)n,name,graph_counters,graph_count);
            if(!added)return 0;n+=added;
        }
        if(i==ES2_HOOK_INDEX_radius_hit) {
            added=callee_tick_fields(out+n,capacity-(size_t)n,name,radius_hit_counters,RADIUS_HIT_COUNTERS);
            if(!added)return 0;
            n+=added;
        }
        if(i==ES2_HOOK_INDEX_ground_update || i==ES2_HOOK_INDEX_ground_collide || i==ES2_HOOK_INDEX_ground_effects || i==ES2_HOOK_INDEX_vehicle_hit || i==ES2_HOOK_INDEX_building_hit) {
            const callee_counter *c=i==ES2_HOOK_INDEX_ground_update?ground_update_counters:
                i==ES2_HOOK_INDEX_ground_collide?ground_collide_counters:
                i==ES2_HOOK_INDEX_ground_effects?ground_effects_counters:
                i==ES2_HOOK_INDEX_vehicle_hit?vehicle_hit_counters:building_hit_counters;
            unsigned count=i==ES2_HOOK_INDEX_ground_update?GROUND_UPDATE_COUNTERS:
                i==ES2_HOOK_INDEX_ground_collide?GROUND_COLLIDE_COUNTERS:
                i==ES2_HOOK_INDEX_ground_effects?GROUND_EFFECTS_COUNTERS:
                i==ES2_HOOK_INDEX_vehicle_hit?VEHICLE_HIT_COUNTERS:BUILDING_HIT_COUNTERS;
            added=callee_tick_fields(out+n,capacity-(size_t)n,name,c,count);
            if(!added) return 0;
            n+=added;
        }
    }
    return n;
}

static void rng_hooks_shutdown(void) {
    if (rng_hooks_configured) es2_hook_log_close(&rng_hook_log);
    /* No unpatching from DllMain. All installed code/context lives until exit. */
}
#endif
