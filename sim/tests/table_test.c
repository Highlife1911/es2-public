/* The dispatch table (es2/sim/dispatch.h): lookup, sealing and its refusals,
 * the argument conventions and the C call path on synthetic entries; then,
 * with ES2_DATA_ROOT, the real table (registry adapters + services) against
 * the reference DBSIM.EXE read as data: every registry and service VA lies
 * in CODE at an entry (a relocated pointer target or a call/jump target),
 * every import-thunk service is `jmp dword [slot]` on the import it names,
 * and no service shadows a hook. SKIP without the data. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/sim/adapters.h"
#include "es2/sim/dispatch.h"
#include "es2/sim/image_file.h"
#include "es2/sim/services.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static uint32_t __cdecl legacy(const uint32_t a[ES2_HOOK_MAX_ARGS], void *user) { (void)user; return a[0] * 100u + a[1]; }
static uint64_t __cdecl pair(const uint32_t *a, void *user) { (void)user; return (uint64_t)a[0] << 32 | a[5]; }
static void svc(es2_sim_service_call *c, void *user) { (void)user; c->eax = c->slots[1]; }
static void svc_refuses(es2_sim_service_call *c, void *user) { (void)user; c->refused = 1; c->reason = "test reason"; }

static es2_hook_descriptor d2 = {"two", 0, NULL, 0, 0, NULL, 0, 2, legacy, NULL, NULL, NULL, 0};
static es2_hook_descriptor d6 = {"six", 0, NULL, 0, 0, NULL, 0, 6, NULL, NULL, NULL, NULL, 0};
static es2_hook_descriptor d7 = {"seven", 0, NULL, 0, 0, NULL, 0, 7, legacy, NULL, NULL, NULL, 0};
static es2_hook_extension x6 = {ES2_ABI_BORLAND, ES2_RESULT_PAIR, pair, NULL, NULL, 0, NULL, 0};
static long double __cdecl st0(const uint32_t *a, void *user) { (void)a; (void)user; return 1.0L; }
/* The sim backend has no x87 state: the ST0 kind is refused even with its replacement. */
static es2_hook_extension bad_st0 = {ES2_ABI_CDECL, ES2_RESULT_ST0, NULL, st0, NULL, 0, NULL, 0};

static es2_sim_refusal last;
static unsigned refusals;
static void on_refuse(const es2_sim_refusal *r, void *user) { (void)user; last = *r; ++refusals; }

static int synthetic(void) {
    es2_sim_entry storage[8];
    es2_sim_dispatch d;
    es2_sim_dispatch_init(&d, storage, 8, on_refuse, NULL);
    CHECK(!es2_sim_dispatch_add_hook(&d, 0x30, "two", &d2, NULL));
    CHECK(!es2_sim_dispatch_add_service(&d, 0x10, "svc", svc, NULL, 0, 2));
    CHECK(!es2_sim_dispatch_add_hook(&d, 0x20, "six", &d6, &x6));
    CHECK(!es2_sim_dispatch_add_service(&d, 0x40, "refuses", svc_refuses, NULL, 0, 0));
    const char *error = NULL; uint32_t bad = 0;
    CHECK(!es2_sim_dispatch_seal(&d, &error, &bad));
    CHECK(d.entries[0].va == 0x10 && d.entries[1].va == 0x20 && d.entries[2].va == 0x30 && d.entries[3].va == 0x40);
    CHECK(es2_sim_dispatch_find(&d, 0x20)->extension == &x6 && !es2_sim_dispatch_find(&d, 0x25));
    CHECK(es2_sim_dispatch_add_hook(&d, 0x50, "late", &d2, NULL));       /* sealed */
    /* Borland six arguments: EAX EDX ECX, then slots 2, 1, 0; three slots popped. */
    uint32_t args[ES2_HOOK_MAX_ARGS];
    const uint32_t slots[3] = {0xa, 0xb, 0xc}, regs[3] = {1, 2, 3};
    es2_sim_hook_args(&d6, &x6, slots, regs, args);
    CHECK(args[0] == 1 && args[1] == 2 && args[2] == 3 && args[3] == 0xc && args[4] == 0xb && args[5] == 0xa);
    CHECK(es2_sim_hook_pop_bytes(&d6, &x6) == 12 && es2_sim_hook_pop_bytes(&d2, NULL) == 0);
    es2_sim_result r;
    CHECK(!es2_sim_call(&d, 0x20, slots, 3, regs, &r) && r.eax == 0xa && r.edx == 1 && r.ecx == 3 && r.pop_bytes == 12);
    CHECK(!es2_sim_call(&d, 0x30, slots, 2, regs, &r) && r.eax == 0xa * 100u + 0xb);
    CHECK(!es2_sim_call(&d, 0x10, slots, 2, regs, &r) && r.eax == 0xb);
    /* Refusals: unmapped, too few slots, a service's own refusal. */
    CHECK(es2_sim_call(&d, 0x18, slots, 3, regs, &r) == 1 && refusals == 1 && last.va == 0x18 && !strcmp(last.reason, "unmapped"));
    CHECK(es2_sim_call(&d, 0x30, slots, 1, regs, &r) == 1 && refusals == 2);
    CHECK(es2_sim_call(&d, 0x40, slots, 0, regs, &r) == 1 && refusals == 3 && !strcmp(last.reason, "test reason") &&
          !strcmp(last.name, "refuses"));
    CHECK(d.refusals == 3 && es2_sim_dispatch_find(&d, 0x20)->calls == 1);
    /* Sealing refuses a VA twice (a service shadowing a hook), more than six
     * arguments, an invalid result kind, a stack cleanup not of whole slots. */
    es2_sim_dispatch_init(&d, storage, 8, on_refuse, NULL);
    es2_sim_dispatch_add_hook(&d, 0x30, "two", &d2, NULL);
    es2_sim_dispatch_add_service(&d, 0x30, "shadow", svc, NULL, 0, 0);
    CHECK(es2_sim_dispatch_seal(&d, &error, &bad) && bad == 0x30 && !strcmp(error, "VA listed twice"));
    es2_sim_dispatch_init(&d, storage, 8, on_refuse, NULL);
    es2_sim_dispatch_add_hook(&d, 0x30, "seven", &d7, NULL);
    CHECK(es2_sim_dispatch_seal(&d, &error, &bad) && bad == 0x30);
    es2_sim_dispatch_init(&d, storage, 8, on_refuse, NULL);
    es2_sim_dispatch_add_hook(&d, 0x30, "st0", &d2, &bad_st0);
    CHECK(es2_sim_dispatch_seal(&d, &error, &bad) && !strcmp(error, "hook ABI or result kind invalid"));
    es2_sim_dispatch_init(&d, storage, 8, on_refuse, NULL);
    es2_sim_dispatch_add_service(&d, 0x30, "odd", svc, NULL, 6, 0);
    CHECK(es2_sim_dispatch_seal(&d, &error, &bad) && !strcmp(error, "stack cleanup not a multiple of 4"));
    es2_sim_dispatch_init(&d, storage, 1, on_refuse, NULL);
    CHECK(!es2_sim_dispatch_add_service(&d, 1, "a", svc, NULL, 0, 0) && es2_sim_dispatch_add_service(&d, 2, "b", svc, NULL, 0, 0));
    puts("PASS: dispatch: sorted lookup, Borland/stdcall conventions, C path, refusals, seal checks");
    return 0;
}

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static int real(const char *root) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/VER95/DBSIM.EXE", root);
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(path, sizeof(path), "%s/extracted/cd/VER95/DBSIM.EXE", root); f = fopen(path, "rb"); }
    if (!f) { puts("SKIP: no DBSIM.EXE below ES2_DATA_ROOT"); return 0; }
    uint8_t *bytes = malloc(ES2_SIM_DBSIM_FILE_SIZE + 1);
    size_t got = bytes ? fread(bytes, 1, ES2_SIM_DBSIM_FILE_SIZE + 1, f) : 0;
    fclose(f);
    const char *error = NULL;
    CHECK(got == ES2_SIM_DBSIM_FILE_SIZE && !es2_sim_image_gate(bytes, got, ES2_SIM_DBSIM_FILE_SIZE, ES2_SIM_DBSIM_SHA256, &error));
    es2_sim_image_info info;
    CHECK(!es2_sim_image_parse(bytes, got, &info, &error));
    CHECK(info.image_base == 0x400000u && info.image_size == 0x2d8000u && info.code_lo == 0x401000u && info.code_hi == 0x497000u);
    /* Placed into ordinary memory: this test reads the image as data only. */
    uint8_t *image = calloc(1, info.image_size);
    CHECK(image && !es2_sim_image_place(bytes, got, &info, image, &error));
    free(bytes);
    static es2_sim_import imports[ES2_SIM_MAX_IMPORTS];
    unsigned import_count = 0;
    CHECK(!es2_sim_image_imports(image, &info, imports, ES2_SIM_MAX_IMPORTS, &import_count, &error));
    CHECK(import_count == 134 && imports[0].slot_va == 0x6cc2d4u);
    /* Entry evidence: relocated dwords that point into CODE, and rel32
     * call/jmp targets of the CODE bytes (a byte scan: a superset). */
    uint8_t *entry = calloc(1, info.code_hi - info.code_lo);
    CHECK(entry);
    unsigned sites = 0;
    for (uint32_t b = info.reloc_rva; b + 8 <= info.reloc_rva + info.reloc_size;) {
        uint32_t page = rd32(image + b), size = rd32(image + b + 4);
        if (!size) break;
        CHECK(size >= 8 && b + size <= info.reloc_rva + info.reloc_size);
        for (uint32_t i = 8; i + 2 <= size; i += 2) {
            unsigned item = image[b + i] | image[b + i + 1] << 8;
            if ((item >> 12) != 3) continue;
            uint32_t at = page + (item & 0xfffu), target = rd32(image + at);
            ++sites;
            if (target >= info.code_lo && target < info.code_hi) entry[target - info.code_lo] |= 1;
        }
        b += size;
    }
    CHECK(sites == 16850);                                                /* own_process.md 2.3 */
    for (uint32_t va = info.code_lo; va + 5 <= info.code_hi; ++va) {
        uint8_t op = image[va - info.image_base];
        if (op != 0xe8 && op != 0xe9) continue;
        uint32_t target = va + 5 + rd32(image + va - info.image_base + 1);
        if (target >= info.code_lo && target < info.code_hi) entry[target - info.code_lo] |= 2;
    }
    /* The real table seals: adapters and services, no VA twice. */
    static es2_sim_entry storage[4096];
    es2_sim_dispatch d;
    es2_sim_services services = {0};
    es2_sim_dispatch_init(&d, storage, 4096, on_refuse, NULL);
    CHECK(!es2_sim_register_services(&d, &services) && !es2_sim_register_adapters(&d, info.image_base));
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&d, &error, &bad)) { fprintf(stderr, "FAIL: real table: %s at 0x%08lx\n", error, (unsigned long)bad); return 1; }
    /* Known exception: heading_sin 0x47e027 (`enter 0, 0`, right after
     * heading_cos's `ret` at 0x47e026) has no static reference at all: no
     * relocated pointer and no rel32 call or jump targets it (checked on the
     * listing). Hooked with heading_cos, never reached through a static
     * reference. The same holds for dot_bytes 0x47dba4 (`enter 0, 0` right
     * after the cross product's `ret` at 0x47dba3; runtime_leaves.h's row, checked on
     * the listing). Any other VA without entry evidence fails. */
    unsigned hooks = 0, outside = 0, no_entry = 0, unreferenced = 0;
    for (unsigned i = 0; i < d.count; ++i) {
        const es2_sim_entry *e = &d.entries[i];
        if (e->va < info.code_lo || e->va >= info.code_hi) { ++outside; fprintf(stderr, "outside CODE: %s 0x%08lx\n", e->name, (unsigned long)e->va); continue; }
        if (e->kind == ES2_SIM_HOOK) ++hooks;
        if (entry[e->va - info.code_lo]) continue;
        if (e->va == 0x0047e027u || e->va == 0x0047dba4u) { ++unreferenced; continue; }
        ++no_entry;
        fprintf(stderr, "no entry evidence: %s 0x%08lx\n", e->name, (unsigned long)e->va);
    }
    CHECK(hooks == es2_sim_adapter_count() && outside == 0 && no_entry == 0 && unreferenced == 2);
    /* The hooks the proxy enters through an entry shim: every shim of
     * entry_shims.h on its registered hook. */
    unsigned shims = es2_sim_adapter_shims(&d);
    printf("entry shims: %u of %u hooks\n", shims, hooks);
    CHECK(shims == es2_sim_adapter_shim_count() && shims > 0 && shims < hooks);
    /* Import-thunk services: `jmp dword [slot]` on the import they name. */
    static const struct { uint32_t va; const char *import; } thunks[] = {
        {0x0049681au, "cw3220.DLL!_memset"}, {0x0049680eu, "cw3220.DLL!_memcpy"}, {0x00496814u, "cw3220.DLL!_memmove"},
        {0x004967bau, "cw3220.DLL!_memcmp"}, {0x00496766u, "cw3220.DLL!_strlen"}, {0x004967eau, "cw3220.DLL!_strcat"},
        {0x004967c0u, "cw3220.DLL!_strncat"}, {0x00496760u, "cw3220.DLL!_strncpy"}, {0x004967a2u, "cw3220.DLL!_strchr"},
        {0x004967ccu, "cw3220.DLL!_strrchr"}, {0x00496784u, "cw3220.DLL!_strstr"}, {0x00496778u, "cw3220.DLL!_strncmp"},
        {0x00496868u, "cw3220.DLL!_stricmp"}, {0x00496790u, "cw3220.DLL!_strnicmp"}, {0x00496754u, "cw3220.DLL!_strupr"},
        {0x0049678au, "cw3220.DLL!_strlwr"}, {0x00496820u, "cw3220.DLL!_itoa"}, {0x00496880u, "cw3220.DLL!_atol"},
        {0x00496796u, "cw3220.DLL!_sprintf"}, {0x0049683eu, "cw3220.DLL!_vsprintf"}, {0x0049686eu, "cw3220.DLL!_calloc"},
        {0x004967a8u, "cw3220.DLL!_free"}, {0x0049687au, "cw3220.DLL!_realloc"}, {0x004968ecu, "KERNEL32.dll!GetTickCount"},
        /* platform */
        {0x004968feu, "KERNEL32.dll!CreateFileA"}, {0x004968b0u, "KERNEL32.dll!CloseHandle"}, {0x00496898u, "KERNEL32.dll!GetModuleHandleA"},
        /* input */
        {0x0049695eu, "USER32.dll!GetKeyState"}, {0x00496a66u, "WINMM.dll!joyGetDevCapsA"}, {0x00496a6cu, "WINMM.dll!joyGetPosEx"},
        {0x0049696au, "USER32.dll!GetCursorPos"}, {0x00496970u, "USER32.dll!GetClientRect"}, {0x0049694cu, "USER32.dll!GetWindowRect"},
        {0x00496910u, "KERNEL32.dll!GetPrivateProfileStringA"},
        /* cursor */
        {0x0049699au, "USER32.dll!ClientToScreen"}, {0x004969e2u, "USER32.dll!SetCursorPos"},
    };
    for (unsigned i = 0; i < sizeof(thunks) / sizeof(thunks[0]); ++i) {
        const uint8_t *t = image + (thunks[i].va - info.image_base);
        CHECK(t[0] == 0xff && t[1] == 0x25);
        uint32_t slot = rd32(t + 2);
        const char *name = NULL;
        for (unsigned k = 0; k < import_count; ++k) if (imports[k].slot_va == slot) name = imports[k].name;
        if (!name || strcmp(name, thunks[i].import)) { fprintf(stderr, "FAIL: thunk 0x%08lx is %s\n", (unsigned long)thunks[i].va, name ? name : "?"); return 1; }
        CHECK(es2_sim_dispatch_find(&d, thunks[i].va) && es2_sim_dispatch_find(&d, thunks[i].va)->kind == ES2_SIM_SERVICE);
    }
    /* The other services start like the listing says: the allocator's
     * `push ebp; mov ebp, esp` and the frame-record set-up `push ebx; mov ebx, ebp`. */
    static const uint32_t framed[] = {0x0047747cu, 0x0047749cu, 0x004774f0u, 0x00477548u, 0x0047758cu};
    for (unsigned i = 0; i < 5; ++i) { const uint8_t *t = image + framed[i] - info.image_base; CHECK(t[0] == 0x55 && t[1] == 0x8b && t[2] == 0xec); }
    const uint8_t *fr = image + 0x0049630cu - info.image_base;
    CHECK(fr[0] == 0x53 && fr[1] == 0x8b && fr[2] == 0xdd);
    printf("PASS: real table: %u hooks + %u services sealed; every VA in CODE at an entry (%u relocated sites); %u thunks checked\n",
           hooks, es2_sim_service_count(), sites, (unsigned)(sizeof(thunks) / sizeof(thunks[0])));
    free(entry); free(image);
    return 0;
}

int main(void) {
    if (synthetic()) return 1;
    const char *root = getenv("ES2_DATA_ROOT");
    if (!root || !*root) { puts("SKIP: ES2_DATA_ROOT is not set; no original data read"); return 0; }
    return real(root);
}
