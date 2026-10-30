/* Fixture: VOL registration probes armed at process attach. Only an own
 * synthetic image with own stubs is executed; the reference mode reads the
 * original DBSIM.EXE bytes read-only and executes none of them. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static WCHAR config_path[MAX_PATH];
static void log_event(const char *s) { (void)s; }
#include "../oracle_perf.h"
#include "../rng_hooks.h"
#include "../live_coverage.h"
#include "../entry_probes.h"
#include "../vol_probes.h"
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); exit(1); } } while (0)

/* ---- synthetic image ---------------------------------------------------- */
#define IMAGE_SIZE 0x10000u
#define MANAGER_RVA 0x2000u
#define ANCHOR_RVA 0x1f00u
enum { S_REGISTER, S_UPDATE, S_INSERT, S_OPEN, S_LOOKUP, S_MEMBER, S_COUNT };
static const uint32_t stub_rva[S_COUNT] = {0x1000, 0x1100, 0x1200, 0x1300, 0x1400, 0x1500};
/* push ebp; mov ebp,esp; mov eax,[ebp+8]; pop ebp; ret  (returns argument 0) */
static const BYTE frame_stub[] = {0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08, 0x5d, 0xc3};
/* push esi; mov eax,[esp+8]; pop esi; ret  (argument 0 only if the PUSH was emulated) */
static const BYTE esp_stub[] = {0x56, 0x8b, 0x44, 0x24, 0x08, 0x5e, 0xc3};
static const BYTE anchor[] = {0x83, 0x3d, 0x00, 0x20, 0x00, 0x00, 0x00};
static vp_spec specs[S_COUNT] = {
    {0x1000, "register", 4, {1, 2}, VP_SNAP_GLOBAL, VP_NONE, 0, 8, {0}},
    {0x1100, "manager_update", 5, {2, 3}, VP_SNAP_ARG0, VP_NONE, 0, 8, {0}},
    {0x1200, "insert", 3, {1, VP_NONE}, VP_SNAP_ARG0, VP_NONE, 0, 8, {0}},
    {0x1300, "archive_open", 3, {0, VP_NONE}, VP_SNAP_NONE, VP_NONE, 0, 7, {0}},
    {0x1400, "lookup", 3, {1, VP_NONE}, VP_SNAP_NONE, VP_NONE, 1, 8, {0}},
    {0x1500, "member_lookup", 3, {1, VP_NONE}, VP_SNAP_NONE, 0, 1, 8, {0}},
};
static BYTE *image;
static vp_target target;
typedef DWORD (__cdecl *stub_fn)(DWORD, DWORD, DWORD, DWORD, DWORD);
static WCHAR directory[MAX_PATH], ini[MAX_PATH], report[MAX_PATH];
static const WCHAR exe_path[] = L"C:\\synthetic\\DBSIM.EXE";

static void make_image(void) {
    image = VirtualAlloc(NULL, IMAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    CHECK(image);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)image;
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 0x80;
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(image + 0x80);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    nt->OptionalHeader.ImageBase = (DWORD)(uintptr_t)image;
    nt->OptionalHeader.SizeOfImage = IMAGE_SIZE;
    for (unsigned i = 0; i < S_COUNT; ++i) {
        const BYTE *code = i == S_OPEN ? esp_stub : frame_stub;
        size_t n = i == S_OPEN ? sizeof(esp_stub) : sizeof(frame_stub);
        memcpy(image + stub_rva[i], code, n);
        memcpy(specs[i].signature, code, n);
    }
    memcpy(image + ANCHOR_RVA, anchor, sizeof(anchor));
    CHECK(FlushInstructionCache(GetCurrentProcess(), image, IMAGE_SIZE));
    target = (vp_target){(uintptr_t)image, IMAGE_SIZE, specs, S_COUNT, MANAGER_RVA, ANCHOR_RVA, anchor, sizeof(anchor)};
}
static DWORD call(unsigned site, DWORD a, DWORD b, DWORD c, DWORD d, DWORD e) {
    SetLastError(0x2468);
    DWORD r = ((stub_fn)(void *)(image + stub_rva[site]))(a, b, c, d, e);
    CHECK(GetLastError() == 0x2468);
    CHECK(r == a);
    return r;
}
static BOOL stubs_original(void) {
    for (unsigned i = 0; i < S_COUNT; ++i)
        if (memcmp(image + stub_rva[i], specs[i].signature, specs[i].length)) return FALSE;
    return TRUE;
}
static void reset(void) {
    if (vp_active) CHECK(vp_disarm());
    vp_state = VP_OFF; vp_count = 0; vp_refusal = NULL; vp_reason = "armed";
    vp_report_reserved = vp_forwarded = vp_complete = vp_has_tick = FALSE; vp_lookups = TRUE;
    vp_forwarding_event = vp_published_events = -1; vp_module_verified = -1; vp_frame = -1;
    vp_event_next = vp_events_dropped = vp_snapshot_next = vp_archive_count = 0;
    vp_error = vp_first_thread = vp_other_thread = 0;
    vp_tick_thread = vp_last_frame = vp_frame_limit = 0; vp_period = 32;
    memset(vp_report_path, 0, sizeof(vp_report_path));
    memset(vp_events, 0, sizeof(vp_events));
    DeleteFileW(report);
}
static void setup_paths(void) {
    DWORD n = GetEnvironmentVariableW(L"TEST_TMPDIR", directory, MAX_PATH);
    if (!n) n = GetTempPathW(MAX_PATH, directory);
    CHECK(n && n < MAX_PATH - 64);
    if (directory[wcslen(directory) - 1] != L'\\') wcscat(directory, L"\\");
    swprintf(ini, MAX_PATH, L"%lsvol-probes-%lu.ini", directory, (unsigned long)GetCurrentProcessId());
    swprintf(report, MAX_PATH, L"%lsvol-probes-%lu.json", directory, (unsigned long)GetCurrentProcessId());
    DeleteFileW(ini);
    DeleteFileW(report);
}
static void write_ini(const WCHAR *enabled, const WCHAR *lookups, const WCHAR *frame_limit) {
    DeleteFileW(ini);
    if (!enabled) { CHECK(WritePrivateProfileStringW(L"coverage", L"enabled", L"0", ini)); return; }
    CHECK(WritePrivateProfileStringW(L"vol_probes", L"enabled", enabled, ini));
    CHECK(WritePrivateProfileStringW(L"vol_probes", L"report", report, ini));
    if (lookups) CHECK(WritePrivateProfileStringW(L"vol_probes", L"lookups", lookups, ini));
    if (frame_limit) CHECK(WritePrivateProfileStringW(L"vol_probes", L"frame_limit", frame_limit, ini));
}
static char *read_report(void) {
    FILE *f = _wfopen(report, L"rb");
    if (!f) return NULL;
    static char text[1 << 20];
    size_t n = fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    text[n] = 0;
    return text;
}
static BOOL report_exists(void) { return GetFileAttributesW(report) != INVALID_FILE_ATTRIBUTES; }
/* Every needle must occur, each after the previous one. */
static BOOL in_order(const char *text, const char *const *needles, unsigned count) {
    const char *p = text;
    for (unsigned i = 0; i < count; ++i) {
        const char *q = strstr(p, needles[i]);
        if (!q) { fprintf(stderr, "missing (in order): %s\n", needles[i]); return FALSE; }
        p = q + strlen(needles[i]);
    }
    return TRUE;
}

/* ---- the synthetic "game" ---------------------------------------------- */
typedef struct archive { BYTE bytes[0x30]; } archive;
static __attribute__((aligned(4))) BYTE manager[0x7c];
static archive archives[3];
static const char *const paths[3] = {"vol\\A.VOL", "vol\\B.VOL", "vol\\C.VOL"};
static const BYTE priorities[3] = {5, 5, 10};
static DWORD p32(const void *p) { return (DWORD)(uintptr_t)p; }
/* Carry insertion as DBSIM 0x467f94 (vol.md, "Exact tie rule"), on own data. */
static void insert_slot(DWORD carried) {
    DWORD *slots = (DWORD *)manager;
    for (unsigned i = 0; i < manager[0x78]; ++i) {
        BYTE occupant = ((archive *)(uintptr_t)slots[i])->bytes[0x1c], incoming = ((archive *)(uintptr_t)carried)->bytes[0x1c];
        if (occupant < incoming) { DWORD displaced = slots[i]; slots[i] = carried; carried = displaced; }
    }
    slots[manager[0x78]++] = carried;
}
/* What DBSIM does before its first DirectDraw call: register vol\*.vol. */
static void synthetic_startup(void) {
    memset(manager, 0, sizeof(manager));
    memset(image + MANAGER_RVA, 0, 4);
    call(S_REGISTER, 1, p32(".vol"), p32("C:\\ROOT\\"), 0, 0);
    call(S_UPDATE, p32(manager), 1, p32(".vol"), p32("C:\\ROOT\\"), 0);
    for (unsigned i = 0; i < 3; ++i) {
        archive *a = &archives[i];
        memset(a, 0, sizeof(*a));
        strcpy((char *)a->bytes + 8, paths[i]);
        a->bytes[0x1c] = priorities[i];
        a->bytes[0x1d] = 2;
        a->bytes[0x20] = 1;
        call(S_INSERT, p32(manager), p32(paths[i]), 1, 0, 0);
        call(S_OPEN, p32(paths[i]), 1, 0x400, 0, 0);
        insert_slot(p32(a));
    }
    DWORD global = p32(manager);
    memcpy(image + MANAGER_RVA, &global, 4);
}
static void synthetic_lookups(void) {
    call(S_LOOKUP, p32(manager), p32("dfn\\font.dfn"), 0, 0, 0);
    call(S_MEMBER, p32(&archives[2]), p32("dfn\\font.dfn"), 0, 0, 0);
    call(S_MEMBER, p32(&archives[1]), p32("dfn\\font.dfn"), 0, 0, 0);
}

/* ---- modes -------------------------------------------------------------- */
/* Arming at process attach precedes the startup registration; the records
 * keep the call order, arguments, strings and slot snapshots; the frame limit
 * disarms and restores every byte. */
static void startup(void) {
    write_ini(L"1", NULL, L"2");
    CHECK(SetEnvironmentVariableW(L"ES2_LIVE_COVERAGE_CONFIG", ini));
    vol_probes_attach(image, exe_path, &target, NULL, 0);
    CHECK(vp_state == VP_ARMED && vp_active && vp_handler && vp_count == S_COUNT && report_exists());
    for (unsigned i = 0; i < S_COUNT; ++i) CHECK(image[stub_rva[i]] == 0xcc);
    synthetic_startup();
    CHECK(vp_event_next == 8);
    vol_probes_forwarding_verified(TRUE);
    CHECK(vp_forwarding_event == 8 && vp_module_verified == 1);
    char *text = read_report();
    CHECK(text && strstr(text, "\"reason\":\"forwarding\"") && strstr(text, "\"forwarding_event\":8") && strstr(text, "\"complete\":false"));
    vol_probes_tick(0);
    synthetic_lookups();
    vol_probes_tick(1);  /* frame_limit=2: frame 1 is the last */
    CHECK(vp_state == VP_DONE && vp_complete && !vp_active && !vp_handler && stubs_original());
    synthetic_lookups();  /* restored: no further records */
    CHECK(vp_event_next == 11);
    text = read_report();
    CHECK(text && strstr(text, "\"complete\":true") && strstr(text, "\"reason\":\"frame_limit\"") && strstr(text, "\"error\":0"));
    CHECK(strstr(text, "\"armed\":true") && strstr(text, "\"module_verified\":true") && strstr(text, "\"events_total\":11"));
    char a[3][16], m[16], line[256];
    for (unsigned i = 0; i < 3; ++i) snprintf(a[i], sizeof(a[i]), "0x%08lx", (unsigned long)p32(&archives[i]));
    snprintf(m, sizeof(m), "0x%08lx", (unsigned long)p32(manager));
    const char *const order[] = {
        "\"events\":[",
        "{\"seq\":0,", "\"role\":\"register\"", "\"frame\":-1", "\"args\":[\"0x00000001\"", "\"text\":[\".vol\",\"C:\\\\ROOT\\\\\"]", "\"snapshot\":0}",
        "{\"seq\":1,", "\"role\":\"manager_update\"", "\"frame\":-1", "\"snapshot\":1}",
        "{\"seq\":2,", "\"role\":\"insert\"", "\"text\":[\"vol\\\\A.VOL\",null]", "\"snapshot\":2}",
        "{\"seq\":3,", "\"role\":\"archive_open\"", "\"args\":[", "\"0x00000001\",\"0x00000400\"]", "\"text\":[\"vol\\\\A.VOL\",null]",
        "{\"seq\":4,", "\"role\":\"insert\"", "\"text\":[\"vol\\\\B.VOL\",null]", "\"snapshot\":3}",
        "{\"seq\":6,", "\"role\":\"insert\"", "\"text\":[\"vol\\\\C.VOL\",null]", "\"snapshot\":4}",
        "{\"seq\":7,", "\"role\":\"archive_open\"", "\"frame\":-1",
        "{\"seq\":8,", "\"role\":\"lookup\"", "\"frame\":0", "\"text\":[\"dfn\\\\font.dfn\",null]",
        "{\"seq\":9,", "\"role\":\"member_lookup\"", a[2],
        "{\"seq\":10,", "\"role\":\"member_lookup\"", a[1],
        "],\"snapshots\":[",
        "\"state\":\"null_manager\"",                       /* register: global not yet set */
        m, "\"count\":0,\"slots\":[]",                       /* update */
        m, "\"count\":0,\"slots\":[]",                       /* insert A */
        m, "\"count\":1,\"slots\":[", a[0],                  /* insert B */
        m, "\"count\":2,\"slots\":[", a[0], a[1],            /* insert C */
        "],\"final\":", m, "\"count\":3,\"slots\":[", a[2], a[1], a[0],  /* C10, B5, A5 */
        "\"archives\":[", "\"name\":\"vol\\\\A.VOL\",\"priority\":5,\"dirs\":2,\"mask\":\"0x00000001\"",
        "\"name\":\"vol\\\\B.VOL\",\"priority\":5", "\"name\":\"vol\\\\C.VOL\",\"priority\":10",
    };
    CHECK(in_order(text, order, sizeof(order) / sizeof(order[0])));
    snprintf(line, sizeof(line), "\"va\":\"0x%08lx\",\"role\":\"register\",\"count\":1,\"restored\":true", (unsigned long)p32(image + 0x1000));
    CHECK(strstr(text, line));
    snprintf(line, sizeof(line), "\"va\":\"0x%08lx\",\"role\":\"member_lookup\",\"count\":2,\"restored\":true", (unsigned long)p32(image + 0x1500));
    CHECK(strstr(text, line));
    /* Second start of the same life cycle is refused (attach runs once). */
    vol_probes_attach(image, exe_path, &target, NULL, 0);
    CHECK(vp_state == VP_DONE && stubs_original());
    puts("PASS startup: armed at attach before the synthetic registration; 11 ordered records with args, strings, "
         "5 slot snapshots, final C10,B5,A5; frame limit restored all 6 entries");
}
/* Option absent or disabled: nothing is read beyond the gate, nothing patched,
 * no handler, no file; every later entry point is inert. */
static void off(void) {
    for (unsigned variant = 0; variant < 5; ++variant) {
        reset();
        if (variant == 0) CHECK(SetEnvironmentVariableW(L"ES2_LIVE_COVERAGE_CONFIG", NULL));
        else {
            if (variant == 1) write_ini(NULL, NULL, NULL);        /* INI without [vol_probes] */
            if (variant == 2) write_ini(L"0", NULL, NULL);        /* enabled=0 */
            if (variant >= 3) write_ini(L"1", NULL, NULL);        /* enabled, but ... */
            CHECK(SetEnvironmentVariableW(L"ES2_LIVE_COVERAGE_CONFIG", ini));
        }
        const WCHAR *exe = variant == 3 ? L"C:\\synthetic\\VSHELL.EXE" : exe_path;  /* ... another process */
        if (variant == 4) CHECK(SetEnvironmentVariableW(L"ES2_LIVE_COVERAGE_CONFIG", NULL));  /* ... variable unset */
        vol_probes_attach(image, exe, &target, NULL, 0);
        CHECK(vp_state == VP_OFF && !vp_active && !vp_handler && !vp_count && !vp_report_reserved && stubs_original());
        CHECK(!*vp_report_path);  /* the report key is never reached */
        synthetic_startup();
        vol_probes_forwarding_verified(TRUE);
        vol_probes_tick(0);
        synthetic_lookups();
        vol_probes_tick(1);
        vol_probes_shutdown("detach");
        CHECK(vp_state == VP_OFF && !vp_event_next && vp_forwarding_event == -1 && !report_exists() && stubs_original());
    }
    CHECK(SetEnvironmentVariableW(L"ES2_LIVE_COVERAGE_CONFIG", NULL));
    puts("PASS off: variable unset, section absent, enabled=0, other executable: no read, patch, handler, record or file");
}
/* Refusals change no byte; shutdown and other-thread rules; lookups=0. */
static DWORD WINAPI other_thread(void *arg) { (void)arg; call(S_LOOKUP, p32(manager), p32("x"), 0, 0, 0); return 0; }
static void restore(void) {
    CHECK(SetEnvironmentVariableW(L"ES2_LIVE_COVERAGE_CONFIG", ini));
    /* Malformed configuration: refused before the report is reserved. */
    reset(); write_ini(L"2", NULL, NULL);
    vol_probes_attach(image, exe_path, &target, NULL, 0);
    CHECK(vp_state == VP_REFUSED && !strcmp(vp_refusal, "config") && !vp_active && stubs_original() && !report_exists());
    vol_probes_forwarding_verified(TRUE);
    CHECK(!report_exists());
    /* Signature mismatch on the last site: no site patched, refusal published. */
    reset(); write_ini(L"1", NULL, NULL);
    image[stub_rva[S_MEMBER] + 7] = 0xc2;
    vol_probes_attach(image, exe_path, &target, NULL, 0);
    CHECK(vp_state == VP_REFUSED && !strcmp(vp_refusal, "sites") && !vp_active && !vp_handler);
    for (unsigned i = 0; i < S_COUNT; ++i) CHECK(image[stub_rva[i]] == specs[i].signature[0]);
    image[stub_rva[S_MEMBER] + 7] = 0xc3;
    vol_probes_forwarding_verified(TRUE);
    char *text = read_report();
    CHECK(text && strstr(text, "\"armed\":false") && strstr(text, "\"refusal\":\"sites\"") && strstr(text, "\"reason\":\"refused\"") && strstr(text, "\"final\":null"));
    /* Excluded range (a registered hook relocation or coverage site). */
    reset(); write_ini(L"1", NULL, NULL);
    vp_range exclusion = {(uintptr_t)image + stub_rva[S_INSERT], 5};
    vol_probes_attach(image, exe_path, &target, &exclusion, 1);
    CHECK(vp_state == VP_REFUSED && !strcmp(vp_refusal, "sites") && stubs_original());
    /* Lookup-set entries inside excluded ranges (the vfs hooks) are
     * left out; the four registration entries are armed. */
    reset(); write_ini(L"1", NULL, NULL);
    vp_range lookup_hooks[2] = {{(uintptr_t)image + stub_rva[S_LOOKUP], 5}, {(uintptr_t)image + stub_rva[S_MEMBER], 6}};
    vol_probes_attach(image, exe_path, &target, lookup_hooks, 2);
    CHECK(vp_state == VP_ARMED && vp_count == 4 && image[stub_rva[S_REGISTER]] == 0xcc && image[stub_rva[S_OPEN]] == 0xcc);
    CHECK(image[stub_rva[S_LOOKUP]] == frame_stub[0] && image[stub_rva[S_MEMBER]] == frame_stub[0]);
    vol_probes_forwarding_verified(TRUE); vol_probes_shutdown("detach");
    CHECK(vp_state == VP_DONE && stubs_original());
    /* Wrong anchor: the image is not the expected executable. */
    reset(); write_ini(L"1", NULL, NULL);
    image[ANCHOR_RVA + 2] ^= 1;
    vol_probes_attach(image, exe_path, &target, NULL, 0);
    CHECK(vp_state == VP_REFUSED && !strcmp(vp_refusal, "image") && stubs_original());
    image[ANCHOR_RVA + 2] ^= 1;
    /* Existing report: never overwritten. */
    reset(); write_ini(L"1", NULL, NULL);
    FILE *f = _wfopen(report, L"wb"); CHECK(f && fputs("old", f) >= 0); fclose(f);
    vol_probes_attach(image, exe_path, &target, NULL, 0);
    CHECK(vp_state == VP_REFUSED && !strcmp(vp_refusal, "report_exists") && stubs_original());
    vol_probes_forwarding_verified(TRUE); vol_probes_shutdown("detach");
    text = read_report(); CHECK(text && !strcmp(text, "old"));
    /* Module hash mismatch at forwarding: disarmed, restored, flagged. */
    reset(); write_ini(L"1", NULL, NULL);
    vol_probes_attach(image, exe_path, &target, NULL, 0);
    CHECK(vp_state == VP_ARMED);
    synthetic_startup();
    vol_probes_forwarding_verified(FALSE);
    CHECK(vp_state == VP_DONE && !vp_complete && !vp_active && stubs_original());
    text = read_report();
    CHECK(text && strstr(text, "\"module_verified\":false") && strstr(text, "\"reason\":\"module_hash_mismatch\"") && strstr(text, "\"error\":10"));
    /* lookups=0: registration sites only; process detach restores. */
    reset(); write_ini(L"1", L"0", NULL);
    vol_probes_attach(image, exe_path, &target, NULL, 0);
    CHECK(vp_state == VP_ARMED && vp_count == 4 && image[stub_rva[S_LOOKUP]] == frame_stub[0] && image[stub_rva[S_REGISTER]] == 0xcc);
    synthetic_startup(); vol_probes_forwarding_verified(TRUE); synthetic_lookups();
    CHECK(vp_event_next == 8);
    vol_probes_shutdown("detach");
    CHECK(vp_state == VP_DONE && vp_complete && stubs_original());
    text = read_report();
    CHECK(text && strstr(text, "\"reason\":\"detach\"") && strstr(text, "\"lookups\":false") && !strstr(text, "\"role\":\"lookup\""));
    /* A second executing thread: shutdown keeps the INT3s and the handler. */
    reset(); write_ini(L"1", NULL, NULL);
    vol_probes_attach(image, exe_path, &target, NULL, 0);
    synthetic_startup();
    HANDLE thread = CreateThread(NULL, 0, other_thread, NULL, 0, NULL);
    CHECK(thread && WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0); CloseHandle(thread);
    vol_probes_shutdown("detach");
    CHECK(vp_state == VP_DONE && !vp_complete && vp_error == 6 && vp_active && image[stub_rva[S_REGISTER]] == 0xcc);
    text = read_report(); CHECK(text && strstr(text, "\"error\":6") && strstr(text, "\"other_thread\":"));
    CHECK(vp_disarm() && stubs_original() && !vp_handler);
    CHECK(SetEnvironmentVariableW(L"ES2_LIVE_COVERAGE_CONFIG", NULL));
    DeleteFileW(report); DeleteFileW(ini);
    puts("PASS restore: config/signature/exclusion/anchor/existing-report refusals patch nothing; hash mismatch, "
         "detach restore; lookups=0; foreign thread keeps INT3s until quiescent");
}
/* Reference table against the original bytes (read only). */
static void reference(void) {
    const char *root = getenv("ES2_DATA_ROOT");
    if (!root || !*root) { puts("SKIP: ES2_DATA_ROOT unset"); return; }
    char path[4096];
    snprintf(path, sizeof(path), "%s/VER95/DBSIM.EXE", root);
    FILE *f = fopen(path, "rb"); CHECK(f);
    CHECK(!fseek(f, 0, SEEK_END)); long size = ftell(f); CHECK(size == 724512); rewind(f);
    BYTE *bytes = malloc((size_t)size); CHECK(bytes && fread(bytes, 1, (size_t)size, f) == (size_t)size); fclose(f);
    HCRYPTPROV provider; HCRYPTHASH hash;
    CHECK(CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) && CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) &&
          CryptHashData(hash, bytes, (DWORD)size, 0));
    BYTE digest[32]; DWORD length = 32; char hex[65];
    CHECK(CryptGetHashParam(hash, HP_HASHVAL, digest, &length, 0));
    for (unsigned i = 0; i < 32; ++i) sprintf(hex + 2 * i, "%02x", digest[i]);
    CHECK(!strcmp(hex, VP_MODULE_SHA256) && !strcmp(hex, LC_MODULE_HASH));
    CryptDestroyHash(hash); CryptReleaseContext(provider, 0);
    DWORD pe = vp_u32(bytes + 0x3c); CHECK(pe < (DWORD)size - 256);
    CHECK(vp_u32(bytes + pe + 24 + 28) == vp_reference_target.base && vp_u32(bytes + pe + 24 + 56) == vp_reference_target.size);
    unsigned sections = bytes[pe + 6] | bytes[pe + 7] << 8, optional = bytes[pe + 20] | bytes[pe + 21] << 8;
    DWORD table = pe + 24 + optional;
    CHECK(table < (DWORD)size && sections <= ((DWORD)size - table) / 40);
    #define FILE_AT(rva, n, out) do { BOOL found_ = FALSE; for (unsigned j = 0; j < sections; ++j) { const BYTE *h = bytes + table + 40 * j; \
        DWORD start = vp_u32(h + 12), count = vp_u32(h + 16), offset = vp_u32(h + 20); \
        if ((rva) >= start && (rva) - start + (n) <= count) { CHECK(offset + ((rva) - start) + (n) <= (DWORD)size); (out) = bytes + offset + ((rva) - start); \
            (out##_code) = (vp_u32(h + 36) & 0x20000000u) != 0; found_ = TRUE; break; } } CHECK(found_); } while (0)
    const BYTE *at; BOOL at_code;
    FILE_AT(vp_reference_target.anchor_rva, vp_reference_target.anchor_length, at);
    CHECK(at_code && !memcmp(at, vp_reference_anchor, sizeof(vp_reference_anchor)));
    /* Registered hook relocations and coverage sites, as the proxy excludes them. */
    unsigned hooked_lookups = 0;
    for (unsigned i = 0; i < vp_reference_target.count; ++i) {
        const vp_spec *s = &vp_reference_specs[i];
        FILE_AT(s->rva, s->length, at);
        CHECK(at_code && !memcmp(at, s->signature, s->length));
        uint32_t va = 0x400000u + s->rva;
        CHECK(probe_opcode(va) == s->signature[0]);
        for (unsigned j = 0; j < i; ++j) CHECK(vp_reference_specs[j].rva != s->rva);
        /* The three lookup entries are vfs hooks: allowed for the
         * lookup set (left out at arming), only as the hook's own entry. */
        for (unsigned j = 0; j < FUNCTION_HOOK_COUNT; ++j) {
            const es2_hook_descriptor *d = function_hook_descriptor(j);
            if (s->rva >= d->rva && s->rva - d->rva < d->relocate_size) {
                CHECK(s->lookup && s->rva == d->rva);
                ++hooked_lookups;
            }
        }
        for (unsigned j = 0; j < LIVE_SITE_COUNT; ++j)
            CHECK(!(va >= live_all_sites[j].address && va - live_all_sites[j].address < live_all_sites[j].length));
    }
    CHECK(hooked_lookups == 3);
    free(bytes);
    printf("PASS reference: %u entry signatures and the manager anchor match the hash-verified DBSIM; "
           "inventory starts; no coverage overlap; hook overlap only at the %u hooked lookup entries; no game code executed\n",
           vp_reference_target.count, hooked_lookups);
}
/* The same arming from a real DllMain: LoadLibrary runs DLL_PROCESS_ATTACH
 * under the loader lock before the synthetic startup; FreeLibrary's
 * DLL_PROCESS_DETACH finishes, restores and removes the handler. */
static BOOL resolve(const char *name, char path[4096]) {
    FILE *f = fopen(name, "rb");
    if (f) { fclose(f); snprintf(path, 4096, "%s", name); return TRUE; }
    const char *manifest = getenv("RUNFILES_MANIFEST_FILE");
    f = manifest ? fopen(manifest, "rb") : NULL;
    if (!f) return FALSE;
    char key[1024], line[8192];
    snprintf(key, sizeof(key), "_main/%s ", name);
    while (fgets(line, sizeof(line), f))
        if (!strncmp(line, key, strlen(key))) {
            line[strcspn(line, "\r\n")] = 0;
            snprintf(path, 4096, "%s", line + strlen(key));
            fclose(f);
            return TRUE;
        }
    fclose(f);
    return FALSE;
}
static void dllmain(const char *name) {
    char path[4096];
    CHECK(resolve(name, path));
    write_ini(L"1", NULL, NULL);
    WCHAR address[32];
    swprintf(address, 32, L"%lx", (unsigned long)p32(image));
    CHECK(SetEnvironmentVariableW(L"ES2_VP_FIXTURE_IMAGE", address) && SetEnvironmentVariableW(L"ES2_LIVE_COVERAGE_CONFIG", ini));
    CHECK(stubs_original());
    HMODULE dll = LoadLibraryA(path);
    CHECK(dll);
    int (__cdecl *state)(void) = (void *)GetProcAddress(dll, "vp_fixture_state");
    DWORD (__cdecl *attach_thread)(void) = (void *)GetProcAddress(dll, "vp_fixture_attach_thread");
    void (__cdecl *forwarding)(void) = (void *)GetProcAddress(dll, "vp_fixture_forwarding");
    LONG (__cdecl *events)(void) = (void *)GetProcAddress(dll, "vp_fixture_events");
    CHECK(state && attach_thread && forwarding && events);
    CHECK(state() == VP_ARMED && attach_thread() == GetCurrentThreadId() && report_exists());
    for (unsigned i = 0; i < S_COUNT; ++i) CHECK(image[stub_rva[i]] == 0xcc);
    synthetic_startup();
    forwarding();
    synthetic_lookups();
    CHECK(events() == 11);
    CHECK(FreeLibrary(dll));
    CHECK(stubs_original());
    synthetic_startup();  /* handler gone with the DLL: unpatched stubs only */
    char *text = read_report();
    char m[16];
    snprintf(m, sizeof(m), "0x%08lx", (unsigned long)p32(manager));
    const char *const order[] = {
        "\"complete\":true", "\"reason\":\"detach\"", "\"forwarding_event\":8", "\"events_total\":11",
        "{\"seq\":0,", "\"role\":\"register\"", "\"frame\":-1", "{\"seq\":2,", "\"role\":\"insert\"",
        "{\"seq\":10,", "\"role\":\"member_lookup\"", "\"final\":", m, "\"count\":3",
    };
    CHECK(text && in_order(text, order, sizeof(order) / sizeof(order[0])));
    CHECK(SetEnvironmentVariableW(L"ES2_LIVE_COVERAGE_CONFIG", NULL) && SetEnvironmentVariableW(L"ES2_VP_FIXTURE_IMAGE", NULL));
    DeleteFileW(report);
    puts("PASS dllmain: armed inside DllMain under the loader lock before the synthetic startup; detach restored "
         "every byte and removed the handler before unload");
}
int main(int argc, char **argv) {
    if (argc < 2 || (argc != 2 && strcmp(argv[1], "dllmain")) || (!strcmp(argv[1], "dllmain") && argc != 3)) return 2;
    setup_paths();
    make_image();
    if (!strcmp(argv[1], "dllmain")) dllmain(argv[2]);
    else if (!strcmp(argv[1], "startup")) startup();
    else if (!strcmp(argv[1], "off")) off();
    else if (!strcmp(argv[1], "restore")) restore();
    else if (!strcmp(argv[1], "reference")) reference();
    else return 2;
    DeleteFileW(ini);
    return 0;
}
