/* The image-access measurement (es2/sim/image_access.h) on a synthetic image:
 * off by default; armed, a CODE read is recorded and served, a call into CODE
 * still reaches the stage-1 trap (no-access page and guarded table page),
 * guarded DATA reads and writes are recorded per page and byte (also beyond the
 * file's bytes), the host's pause hides the measurement, the map is written
 * without image bytes, disarm restores the protections; the operand-size
 * decoder on hand-assembled instructions. No game data. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/sim/image_access.h"
#include "es2/sim/trap.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static uint8_t *image;
static uint32_t A(uint32_t offset) { return (uint32_t)(uintptr_t)image + offset; }

static es2_sim_dispatch table;
static es2_sim_entry storage[8];
static void on_refuse(const es2_sim_refusal *r, void *user) { (void)r; (void)user; }
static void svc_sum(es2_sim_service_call *c, void *user) { (void)user; c->eax = c->slots[0] + c->slots[1] + c->slots[2]; }
typedef uint32_t (__cdecl *sum_fn)(uint32_t, uint32_t, uint32_t);

static DWORD protection(uint32_t va) {
    MEMORY_BASIC_INFORMATION m;
    return VirtualQuery((void *)(uintptr_t)va, &m, sizeof(m)) ? m.Protect : 0;
}

/* Headers 0x400 bytes; CODE 0x1000..0x3000 (raw 0x1e00); DATA 0x3000
 * (raw 0x1800, virtual 0x3000); .rsrc 0x6000 (raw 0x200). */
static es2_sim_image_info layout(void) {
    es2_sim_image_info info;
    memset(&info, 0, sizeof(info));
    info.image_base = A(0);
    info.image_size = 0x7000u;
    info.headers_size = 0x400u;
    info.section_count = 3;
    info.sections[0] = (es2_sim_section){"CODE", 0x1000u, 0x2000u, 0x400u, 0x1e00u, 0x60000020u};
    info.sections[1] = (es2_sim_section){"DATA", 0x3000u, 0x3000u, 0x2200u, 0x1800u, 0xc0000040u};
    info.sections[2] = (es2_sim_section){".rsrc", 0x6000u, 0x200u, 0x3a00u, 0x200u, 0x40000040u};
    info.code_lo = A(0x1000);
    info.code_hi = A(0x3000);
    return info;
}

static int decoder(void) {
    static const struct { uint8_t bytes[8]; unsigned n, size; int string; } cases[] = {
        {{0x8b, 0x45, 0x08}, 3, 4, 0},              /* mov eax, [ebp+8] */
        {{0x66, 0x8b, 0x45, 0x08}, 4, 2, 0},        /* mov ax, [ebp+8] */
        {{0x8a, 0x01}, 2, 1, 0},                    /* mov al, [ecx] */
        {{0x0f, 0xb6, 0x01}, 3, 1, 0},              /* movzx eax, byte [ecx] */
        {{0x0f, 0xbf, 0x01}, 3, 2, 0},              /* movsx eax, word [ecx] */
        {{0xdd, 0x45, 0x00}, 3, 8, 0},              /* fld qword [ebp] */
        {{0xd9, 0x00}, 2, 4, 0},                    /* fld dword [eax] */
        {{0xdb, 0x28}, 2, 10, 0},                   /* fld tbyte [eax] */
        {{0xdf, 0x00}, 2, 2, 0},                    /* fild word [eax] */
        {{0xf3, 0xa5}, 2, 4, 1},                    /* rep movsd */
        {{0xa4}, 1, 1, 1},                          /* movsb */
        {{0xa1, 0x00, 0x10, 0x40, 0x00}, 5, 4, 0},  /* mov eax, [0x401000] */
        {{0xf2, 0x0f, 0x10, 0x00}, 4, 8, 0},        /* movsd xmm0, [eax] */
        {{0x66, 0x0f, 0x6f, 0x00}, 4, 16, 0},       /* movdqa xmm0, [eax] */
        {{0xff, 0x30}, 2, 4, 0},                    /* push dword [eax] */
        {{0x80, 0x38, 0x01}, 3, 1, 0},              /* cmp byte [eax], 1 */
        {{0x83, 0x38, 0x01}, 3, 4, 0},              /* cmp dword [eax], 1 */
        {{0x8b, 0xc0}, 2, 0, 0},                    /* mov eax, eax: no memory operand */
        {{0x8d, 0x45, 0x08}, 3, 0, 0},              /* lea: no access */
        {{0x0f, 0x0b}, 2, 0, 0},                    /* ud2: not decoded */
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        int string = -1;
        unsigned size = es2_sim_access_operand_size(cases[i].bytes, cases[i].n, &string);
        if (size != cases[i].size || string != cases[i].string) {
            fprintf(stderr, "FAIL decoder case %u: size %u string %d\n", i, size, string);
            return 1;
        }
    }
    CHECK(es2_sim_access_operand_size((const uint8_t *)"\x8b", 1, NULL) == 0);   /* truncated */
    return 0;
}

static int read_file(const char *path, char **text) {
    FILE *f = fopen(path, "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    *text = calloc((size_t)n + 1u, 1);
    size_t got = *text ? fread(*text, 1, (size_t)n, f) : 0;
    fclose(f);
    return got != (size_t)n;
}

int main(void) {
    CHECK(!decoder());
    unsigned long flags = 0; int permanent = 0;
    CHECK(!es2_sim_enable_dep(&flags, &permanent));
    image = VirtualAlloc(NULL, 0x7000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    CHECK(image);
    for (unsigned i = 0; i < 0x7000u; ++i) image[i] = (uint8_t)(i * 7u + 3u);
    DWORD old;
    /* The host's protections: headers and .rsrc read-only, CODE read-only
     * (not executable), DATA read-write. */
    CHECK(VirtualProtect(image, 0x1000, PAGE_READONLY, &old));
    CHECK(VirtualProtect(image + 0x1000, 0x2000, PAGE_READONLY, &old));
    CHECK(VirtualProtect(image + 0x6000, 0x1000, PAGE_READONLY, &old));
    es2_sim_image_info info = layout();
    const uint32_t sites[1] = {A(0x2010)};
    const char *error = NULL;

    /* Off by default: arming does nothing. */
    CHECK(!es2_sim_image_access_configured());
    CHECK(!es2_sim_image_access_arm(&info, sites, 1, &error));
    CHECK(!es2_sim_image_access_armed());
    CHECK(protection(A(0x1000)) == PAGE_READONLY && protection(A(0x3000)) == PAGE_READWRITE);
    CHECK(!es2_sim_image_access_write("off"));

    char path[MAX_PATH];
    const char *tmp = getenv("TEST_TMPDIR");
    snprintf(path, sizeof(path), "%s\\image-access-test.json", tmp && *tmp ? tmp : ".");
    DeleteFileA(path);
    CHECK(!es2_sim_image_access_configure(path));
    CHECK(es2_sim_image_access_configured());
    CHECK(!es2_sim_image_access_arm(&info, sites, 1, &error));
    CHECK(es2_sim_image_access_armed());
    CHECK(protection(A(0x1000)) == PAGE_NOACCESS);
    CHECK(protection(A(0x2000)) == (PAGE_READONLY | PAGE_GUARD));
    CHECK(protection(A(0x3000)) == (PAGE_READWRITE | PAGE_GUARD));
    CHECK(protection(A(0x0000)) == (PAGE_READONLY | PAGE_GUARD));
    CHECK(protection(A(0x6000)) == (PAGE_READONLY | PAGE_GUARD));
    CHECK(protection(A(0x5000)) == PAGE_READWRITE);        /* zero-filled only: not watched */

    /* The stage-1 trap after the measurement, as in the host. */
    es2_sim_dispatch_init(&table, storage, 8, on_refuse, NULL);
    CHECK(!es2_sim_dispatch_add_service(&table, A(0x1010), "sum", svc_sum, NULL, 0, 3));
    CHECK(!es2_sim_dispatch_add_service(&table, A(0x2040), "sum_on_table_page", svc_sum, NULL, 0, 3));
    uint32_t bad = 0;
    CHECK(!es2_sim_dispatch_seal(&table, &error, &bad));
    es2_sim_trap_range ranges[1] = {{A(0x1000), A(0x3000), NULL, 0}};
    CHECK(!es2_sim_trap_install(&table, ranges, 1));

    /* A CODE read: recorded (VA, size, module) and served. */
    const es2_sim_code_read *reads = NULL;
    CHECK(es2_sim_image_access_code_reads(&reads) == 0);
    volatile const uint8_t *byte = (volatile const uint8_t *)(uintptr_t)A(0x1123);
    CHECK(*byte == (uint8_t)(0x1123u * 7u + 3u));
    CHECK(es2_sim_image_access_code_reads(&reads) == 1);
    CHECK(reads[0].va == A(0x1123) && reads[0].size == 1 && reads[0].count == 1);
    CHECK(strstr(reads[0].module, "image_access_test") != NULL);
    CHECK(protection(A(0x1000)) == PAGE_NOACCESS);         /* no-access again after the one instruction */
    volatile const uint32_t *dword = (volatile const uint32_t *)(uintptr_t)A(0x1200);
    uint32_t expect;
    memcpy(&expect, (uint8_t[]){(uint8_t)(0x1200u * 7u + 3u), (uint8_t)(0x1201u * 7u + 3u), (uint8_t)(0x1202u * 7u + 3u),
                                (uint8_t)(0x1203u * 7u + 3u)}, 4);
    CHECK(*dword == expect);
    CHECK(es2_sim_image_access_code_reads(&reads) == 2);
    CHECK(reads[1].va == A(0x1200) && reads[1].size == 4);

    /* The table page: readable, its reads sampled, not CODE reads. */
    es2_sim_access_page_stats st;
    volatile const uint8_t *site = (volatile const uint8_t *)(uintptr_t)A(0x2010);
    CHECK(*site == (uint8_t)(0x2010u * 7u + 3u));
    CHECK(es2_sim_image_access_code_reads(NULL) == 2);
    CHECK(!es2_sim_image_access_page(A(0x2010), &st) && st.read_faults == 1 && st.read_bytes == 1);
    CHECK(es2_sim_image_access_byte(A(0x2010)) == 1u);

    /* Calls into CODE still reach the trap: on a no-access page and on the
     * guarded table page (the guard fault is counted as an execute). */
    sum_fn sum = (sum_fn)(uintptr_t)A(0x1010);
    CHECK(sum(1, 2, 3) == 6);
    CHECK(es2_sim_image_access_code_reads(NULL) == 2);
    CHECK(protection(A(0x2000)) == (PAGE_READONLY | PAGE_GUARD));
    sum_fn sum2 = (sum_fn)(uintptr_t)A(0x2040);
    CHECK(sum2(10, 20, 30) == 60);
    CHECK(!es2_sim_image_access_page(A(0x2040), &st) && st.exec_faults == 1);
    CHECK(es2_sim_trap_calls() == 2);

    /* DATA: reads and writes per page and byte; the guard is re-armed after
     * the instruction, so the next access is seen too. */
    volatile uint8_t *data = (volatile uint8_t *)(uintptr_t)A(0x3000);
    CHECK(data[0x100] == (uint8_t)(0x3100u * 7u + 3u));
    CHECK(protection(A(0x3000)) == (PAGE_READWRITE | PAGE_GUARD));
    CHECK(data[0x300] == (uint8_t)(0x3300u * 7u + 3u));
    *(volatile uint32_t *)(uintptr_t)A(0x3200) = 0x11223344u;
    CHECK(*(volatile uint32_t *)(uintptr_t)A(0x3200) == 0x11223344u);
    CHECK(!es2_sim_image_access_page(A(0x3000), &st));
    CHECK(st.read_faults == 3 && st.write_faults == 1 && st.written_bytes == 4 && st.read_bytes == 6);
    CHECK(es2_sim_image_access_byte(A(0x3100)) == 1u && es2_sim_image_access_byte(A(0x3203)) == 3u);
    CHECK(es2_sim_image_access_byte(A(0x3101)) == 0u);
    /* Past the file's bytes on the last DATA page (raw ends at 0x4800). */
    CHECK(data[0x1900] == (uint8_t)(0x4900u * 7u + 3u));
    CHECK(!es2_sim_image_access_page(A(0x4900), &st) && st.read_faults == 1);

    /* The host's own reads between pause and resume are not counted. */
    es2_sim_image_access_pause();
    CHECK(protection(A(0x1000)) == PAGE_READONLY && protection(A(0x3000)) == PAGE_READWRITE);
    CHECK(*byte == (uint8_t)(0x1123u * 7u + 3u) && data[0x400] == (uint8_t)(0x3400u * 7u + 3u));
    es2_sim_image_access_resume();
    CHECK(es2_sim_image_access_code_reads(NULL) == 2);
    CHECK(es2_sim_image_access_byte(A(0x3400)) == 0u);
    CHECK(protection(A(0x1000)) == PAGE_NOACCESS && protection(A(0x3000)) == (PAGE_READWRITE | PAGE_GUARD));

    /* The budget: within one epoch a page faults at most 32 times
     * (IA_BUDGET), then stays open; the next epoch (resume) guards it again. */
    CHECK(!es2_sim_image_access_page(A(0x4000), &st));
    uint64_t before = st.read_faults;
    unsigned total = 0;
    for (unsigned i = 0; i < 40u; ++i) total += data[0x1010u + i];
    CHECK(total != 0u);
    CHECK(!es2_sim_image_access_page(A(0x4000), &st) && st.read_faults == before + 32u);
    CHECK(protection(A(0x4000)) == PAGE_READWRITE);
    es2_sim_image_access_pause();
    es2_sim_image_access_resume();
    CHECK(protection(A(0x4000)) == (PAGE_READWRITE | PAGE_GUARD));
    CHECK(data[0x1080] == (uint8_t)(0x4080u * 7u + 3u));
    CHECK(!es2_sim_image_access_page(A(0x4000), &st) && st.read_faults == before + 33u);

    /* The map: VA ranges and counts. */
    CHECK(!es2_sim_image_access_write("test"));
    CHECK(es2_sim_image_access_armed());
    char *text = NULL;
    CHECK(!read_file(path, &text));
    char needle[160];
    snprintf(needle, sizeof(needle), "{\"va\":\"0x%08lx\",\"size\":1,", (unsigned long)A(0x1123));
    CHECK(strstr(text, needle) != NULL);
    snprintf(needle, sizeof(needle), "\"read\":[[\"0x%08lx\",\"0x%08lx\"],", (unsigned long)A(0x3100), (unsigned long)A(0x3101));
    CHECK(strstr(text, needle) != NULL);
    snprintf(needle, sizeof(needle), "\"written\":[[\"0x%08lx\",\"0x%08lx\"]]", (unsigned long)A(0x3200), (unsigned long)A(0x3204));
    CHECK(strstr(text, needle) != NULL);
    CHECK(strstr(text, "\"name\":\"DATA\"") != NULL && strstr(text, "\"bytes_read_beyond_file\":1") != NULL);
    CHECK(strstr(text, "\"code_read_faults\":2") != NULL);
    free(text);
    CHECK(!es2_sim_image_access_write("again"));            /* once only */

    /* Disarm: the host's protections again. */
    es2_sim_trap_remove();
    es2_sim_image_access_disarm();
    CHECK(!es2_sim_image_access_armed());
    CHECK(protection(A(0x1000)) == PAGE_READONLY && protection(A(0x2000)) == PAGE_READONLY);
    CHECK(protection(A(0x3000)) == PAGE_READWRITE && protection(A(0x0000)) == PAGE_READONLY);
    DeleteFileA(path);
    puts("image_access_test: PASS");
    return 0;
}
