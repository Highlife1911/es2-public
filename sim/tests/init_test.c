/* sim/src/init.c on a synthetic image (an arena over zeroed host memory at the
 * original VAs, a synthetic stack and heap range) with recording services in
 * place of the sim's: the c0w32 entry's stores, the selector's order and done
 * flags, the stage registry in the init records' order, the stores of the records with
 * visible effect, both outcomes of every branch (new failing, a VxD handle
 * other than -1, buffers present at exit, the function-local statics
 * constructed), the construction counter back to 0, and a refusal for a record
 * VA without own code. With ES2_DATA_ROOT: the record tables of the reference
 * DBSIM.EXE hold exactly the own list's VAs in table order (the in-vitro
 * comparison against the original is
 * //tools/reference-game/invitro:differential_init_records_test). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/sim/dispatch.h"
#include "es2/sim/image_file.h"
#include "es2/sim/init.h"
#include "es2/sim/memory.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

#define IMAGE_BASE 0x00400000u
#define IMAGE_SIZE 0x002d8000u
#define STACK_BASE 0x07000000u          /* synthetic StackBase; the arena covers the page below */
#define HEAP_BASE 0x09000000u
#define HEAP_SIZE 0x1000u

static es2_sim_arena arena;
static es2_guest_memory memory;
static uint8_t *image, stack[0x1000], heap[HEAP_SIZE];

static uint32_t r32(uint32_t a) { return memory.read32(memory.context, a); }
static uint16_t r16(uint32_t a) { return memory.read16(memory.context, a); }
static uint8_t r8(uint32_t a) { return es2_sim_read8(&memory, a); }
static void w32(uint32_t a, uint32_t v) { memory.write32(memory.context, a, v); }
static void w16(uint32_t a, uint16_t v) { memory.write16(memory.context, a, v); }
static void w8(uint32_t a, uint8_t v) { es2_sim_write8(&memory, a, v); }

/* ---- recording services */
static char log_text[256];
static unsigned log_count;
static uint32_t new_result, create_result, created_name, create_flags, closed, deleted[16], deleted_count;
static void note(char c) { if (log_count + 1 < sizeof(log_text)) log_text[log_count++] = c; }
static void svc_memset(es2_sim_service_call *call, void *user) {
    (void)user; note('S');
    for (uint32_t i = 0; i < call->slots[2]; ++i) w8(call->slots[0] + i, (uint8_t)call->slots[1]);
    call->eax = call->slots[0];
}
static void svc_memmove(es2_sim_service_call *call, void *user) {
    (void)user; note('M');
    for (uint32_t i = 0; i < call->slots[2]; ++i) w8(call->slots[0] + i, r8(call->slots[1] + i));
    call->eax = call->slots[0];
}
static void svc_new(es2_sim_service_call *call, void *user) {
    (void)user; note('N');
    if (call->slots[0] != 0x808u) { call->refused = 1; return; }
    call->eax = new_result;
}
static void svc_delete(es2_sim_service_call *call, void *user) {
    (void)user; note('D');
    if (deleted_count < 16) deleted[deleted_count++] = call->slots[0];
}
static void svc_delete_array(es2_sim_service_call *call, void *user) {
    (void)user; note('A');
    if (deleted_count < 16) deleted[deleted_count++] = call->slots[0];
}
static void svc_create(es2_sim_service_call *call, void *user) {
    (void)user; note('V');
    created_name = call->slots[0];
    create_flags = call->slots[5];
    if (call->slots[1] || call->slots[2] || call->slots[3] || call->slots[4] || call->slots[6]) call->refused = 1;
    call->eax = create_result;
}
static void svc_close(es2_sim_service_call *call, void *user) { (void)user; note('X'); closed = call->slots[0]; call->eax = 1; }
static void svc_module(es2_sim_service_call *call, void *user) { (void)user; note('G'); call->eax = call->slots[0] ? 0 : IMAGE_BASE; }

static es2_sim_entry entries[16];
static es2_sim_dispatch dispatch;
static unsigned refusals;
static uint32_t refused_va;
static void on_refuse(const es2_sim_refusal *r, void *user) { (void)user; ++refusals; refused_va = r->va; }

static int setup(void) {
    memset(image, 0, IMAGE_SIZE);
    memset(stack, 0, sizeof(stack));
    memset(heap, 0, sizeof(heap));
    log_count = 0; memset(log_text, 0, sizeof(log_text));
    refusals = 0; refused_va = 0; closed = 0; deleted_count = 0; created_name = 0; create_flags = 0;
    new_result = HEAP_BASE + 8u;
    create_result = 0xffffffffu;
    /* The module table and the records: the own list's VAs, flags 0, priority 0x20. */
    w32(ES2_SIM_VA_MODULE_TABLE + 0x0u, 0x004a79d8u);
    w32(ES2_SIM_VA_MODULE_TABLE + 0x4u, 0x004a79d8u + 6u * ES2_SIM_INIT_RECORDS);
    w32(ES2_SIM_VA_MODULE_TABLE + 0x8u, 0x004a7b5eu);
    w32(ES2_SIM_VA_MODULE_TABLE + 0xcu, 0x004a7b5eu + 6u * ES2_SIM_EXIT_RECORDS);
    for (unsigned i = 0; i < ES2_SIM_INIT_RECORDS; ++i) { w8(0x004a79d8u + 6u * i + 1u, 0x20); w32(0x004a79d8u + 6u * i + 2u, es2_sim_record_va(0, i)); }
    for (unsigned i = 0; i < ES2_SIM_EXIT_RECORDS; ++i) { w8(0x004a7b5eu + 6u * i + 1u, 0x20); w32(0x004a7b5eu + 6u * i + 2u, es2_sim_record_va(1, i)); }
    /* Garbage in BSS that the entry must clear; the dwords the entry saves. */
    w32(ES2_SIM_VA_BSS_START, 0x11111111u);
    w32(ES2_SIM_VA_BSS_END - 8u, 0x22222222u);
    w32(0x005a0000u, 0x33333333u);
    w32(0x00497059u, 0x01020304u);
    w32(STACK_BASE - 8u, 0xaaaa0008u);
    w32(STACK_BASE - 4u, 0xaaaa0004u);
    /* Synthetic DATA the records copy: the RNG seed and the state constants. */
    for (uint32_t i = 0; i < 0x70u; ++i) w8(0x004a6940u + i, (uint8_t)(0x80u + i));
    for (uint32_t i = 0; i < 22u * 0x24u; i += 4u) w32(0x004998f8u + i, 0x5000u + i);
    for (uint32_t i = 0; i < 7u * 0x24u; i += 4u) w32(0x00499e9cu + i, 0x6000u + i);
    const char *vxd = "\\\\.\\DARKMONO.VXD";
    for (uint32_t i = 0; vxd[i]; ++i) w8(0x004a6a60u + i, (uint8_t)vxd[i]);
    return 0;
}

static int run_startup(es2_sim_init *c) {
    memset(c, 0, sizeof(*c));
    c->memory = &memory; c->dispatch = &dispatch; c->stack_base = STACK_BASE;
    return es2_sim_c0w32_entry(c) || es2_sim_run_init_records(c);
}

static int synthetic(void) {
    image = calloc(1, IMAGE_SIZE);
    CHECK(image);
    CHECK(es2_sim_arena_add(&arena, IMAGE_BASE, image, IMAGE_SIZE));
    CHECK(es2_sim_arena_add(&arena, STACK_BASE - sizeof(stack), stack, sizeof(stack)));
    CHECK(es2_sim_arena_add(&arena, HEAP_BASE, heap, HEAP_SIZE));
    es2_sim_arena_memory(&arena, &memory);
    es2_sim_dispatch_init(&dispatch, entries, 16, on_refuse, NULL);
    CHECK(!es2_sim_dispatch_add_service(&dispatch, 0x0049681au, "memset", svc_memset, NULL, 0, 3));
    CHECK(!es2_sim_dispatch_add_service(&dispatch, 0x00496814u, "memmove", svc_memmove, NULL, 0, 3));
    CHECK(!es2_sim_dispatch_add_service(&dispatch, 0x0047749cu, "new", svc_new, NULL, 0, 1));
    CHECK(!es2_sim_dispatch_add_service(&dispatch, 0x00477548u, "delete", svc_delete, NULL, 0, 1));
    CHECK(!es2_sim_dispatch_add_service(&dispatch, 0x0047758cu, "delete[]", svc_delete_array, NULL, 0, 1));
    CHECK(!es2_sim_dispatch_add_service(&dispatch, 0x004968feu, "CreateFileA", svc_create, NULL, 28, 7));
    CHECK(!es2_sim_dispatch_add_service(&dispatch, 0x004968b0u, "CloseHandle", svc_close, NULL, 4, 1));
    CHECK(!es2_sim_dispatch_add_service(&dispatch, 0x00496898u, "GetModuleHandleA", svc_module, NULL, 4, 1));
    const char *error = NULL;
    uint32_t bad = 0;
    CHECK(!es2_sim_dispatch_seal(&dispatch, &error, &bad));

    /* ---- the start-up on the synthetic image */
    es2_sim_init c;
    CHECK(!setup() && !run_startup(&c));
    CHECK(c.init_run == ES2_SIM_INIT_RECORDS && c.exit_run == 0 && arena.faults == 0);
    /* The entry. */
    CHECK(r32(0x0049705du) == 0x01020304u << 2);
    CHECK(r32(ES2_SIM_VA_BSS_START) == 0 && r32(0x005a0000u) == 0 && r32(ES2_SIM_VA_BSS_END - 8u) == 0);
    CHECK(r32(0x00497061u) == 0xaaaa0008u && r32(0x00497065u) == 0xaaaa0004u);
    CHECK(r32(0x004a78a0u) == 0xaaaa0008u + 0x24u);
    CHECK(r32(STACK_BASE - 8u) == ES2_SIM_VA_EXCEPT_CONTEXT && r32(0x006cbee4u) == STACK_BASE - 4u);
    CHECK(r32(0x0049706au) == IMAGE_BASE);
    /* The calls in order: GetModuleHandleA, memset, memmove x2, new, CreateFileA. */
    CHECK(!strcmp(log_text, "GSMMNV"));
    /* Every init record done, no exit record. */
    for (unsigned i = 0; i < ES2_SIM_INIT_RECORDS; ++i) CHECK(r8(0x004a79d8u + 6u * i) == 1);
    for (unsigned i = 0; i < ES2_SIM_EXIT_RECORDS; ++i) CHECK(r8(0x004a7b5eu + 6u * i) == 0);
    /* The stage registry: 31 entries in registration order. */
    static const struct { uint32_t f; uint16_t s; } stages[31] = {
        {0x401e5c, 6}, {0x405fac, 2}, {0x405ea4, 5}, {0x4076e4, 2}, {0x4077e8, 5}, {0x407b54, 2}, {0x407b3c, 5},
        {0x408860, 2}, {0x4083e0, 5}, {0x4090cc, 2}, {0x408f9c, 5}, {0x409904, 5}, {0x409a34, 2}, {0x409a1c, 5},
        {0x40a060, 2}, {0x40a108, 5}, {0x40a818, 2}, {0x40ac38, 2}, {0x40ade0, 2}, {0x40b6e0, 2}, {0x40fc8c, 2},
        {0x415464, 2}, {0x41544c, 5}, {0x41fdb0, 2}, {0x4215a8, 5}, {0x422d90, 2}, {0x428230, 2}, {0x43034c, 2},
        {0x467b64, 2}, {0x46b1a8, 2}, {0x46b188, 5},
    };
    CHECK(r16(ES2_SIM_VA_STAGE_COUNT) == 31);
    for (uint32_t i = 0; i < 31u; ++i)
        CHECK(r32(ES2_SIM_VA_STAGE_TABLE + 6u * i) == stages[i].f && r16(ES2_SIM_VA_STAGE_TABLE + 6u * i + 4u) == stages[i].s);
    /* MECH state 18 and FLYER state 4 (own_process.md, "The 65 init records"). */
    uint32_t m = 0x004993a4u + 0x3eu * 18u;
    CHECK(r32(m) == 0x00499c10u + 0xc8u && r32(m + 4u) == 15000u && r16(m + 0x3cu) == 5);
    for (uint32_t b = 0; b < 16u; ++b) CHECK(r8(m + 8u + b) == ((0x12u >> b) & 1u));
    for (uint32_t k = 0; k < 9u; ++k) CHECK(r32(m + 0x18u + 4u * k) == 0x5000u + 0x24u * 18u + 4u * k);
    uint32_t f = 0x00499cf8u + 0x3cu * 4u;
    CHECK(r32(f) == 0x00499f98u + 0x31u && r32(f + 4u) == 500u && r8(f + 8u) == 1 && r8(f + 9u) == 0);
    CHECK(r32(f + 0x38u) == 0x6000u + 0x24u * 4u + 0x20u && r16(0x00499cf8u + 0x3cu * 5u) != 0);
    /* The class registry, the rectangle, record 30, the key pairs. */
    CHECK(r32(0x006c5834u) == 0x0049aa7cu && r32(0x006c5838u) == 1 && r32(0x004a1690u) == 1);
    CHECK(r32(0x0049ac5cu) == 0x140u && r32(0x0049ac58u) == 0xd2u && r32(0x0049ac60u) == 0xf0u);
    CHECK(r32(0x0049b072u) == 1000u && r32(0x0049b07au) == (uint32_t)-1000 && r32(0x004d03b4u) == 4u);
    CHECK((int8_t)r8(0x0049eb45u) == -1 && (int8_t)r8(0x0049eb4au) == -1 && r8(0x0049eb60u) == 1);
    /* Regions and bitmaps: the last vtable store wins. */
    CHECK(r32(0x004cfa7au) == 0x004a3264u && r32(0x004d1a2cu) == 0x004a3264u);
    CHECK(r32(0x004d1cf6u) == 0x004a306cu && r32(0x004d257fu) == 0x004a306cu && r32(0x006b4f04u) == 0x004a306cu);
    CHECK(r16(0x0049c454u) == 0xc000u && r16(0x0049d6c0u) == 0xc000u);
    /* The globals object and both RNG streams. */
    CHECK(r32(0x004d24bcu + 0x4au) == 1 && r8(0x004d24bcu + 0x0cu) == 1 && r16(0x004d24bcu + 0x56u) == 0xffffu);
    CHECK(r8(0x004d24bcu + 0x7au) == 0x72 && r32(0x004d24bcu + 0x8au) == 640u && r32(0x004d24bcu + 0xbbu) == 13u);
    for (uint32_t s = 0; s < 2u; ++s) {
        uint32_t stream = s ? 0x004d260bu : 0x004d2599u;
        CHECK(r8(stream + 0x70u) == 24 && r8(stream + 0x71u) == 55 && r8(stream) == 0x80 && r8(stream + 0x6fu) == 0xef);
    }
    /* The block of record 53 and the lights of record 57. */
    CHECK(r32(ES2_SIM_VA_TABLE_808) == HEAP_BASE + 8u);
    CHECK(r32(HEAP_BASE + 8u) == 0 && r32(HEAP_BASE + 12u) == 0x100u && r32(HEAP_BASE + 8u + 8u * 255u + 4u) == 0x100u);
    CHECK(r32(HEAP_BASE + 8u + 0x804u) == 0);
    CHECK(r32(0x006b7ba4u) == 0x004a5b39u && r32(0x006b7ba4u + 0x110u * 51u + 0x16u * 9u) == 0x004a5b39u);
    CHECK(r32(0x006c6054u) == 1);
    /* The VxD: name, flags, handle. */
    CHECK(created_name == 0x004a6a60u && create_flags == 0x04000000u && r32(ES2_SIM_VA_VXD_HANDLE) == 0xffffffffu);
    /* The construction counter: +616 over the init records (in vitro). */
    CHECK(r32(STACK_BASE - 4u) == 616u);

    /* ---- the exit records */
    log_count = 0; memset(log_text, 0, sizeof(log_text));
    CHECK(!es2_sim_run_exit_records(&c) && c.exit_run == ES2_SIM_EXIT_RECORDS);
    for (unsigned i = 0; i < ES2_SIM_EXIT_RECORDS; ++i) CHECK(r8(0x004a7b5eu + 6u * i) == 1);
    CHECK(r32(STACK_BASE - 4u) == 0);
    /* Records 0 and 7 delete their pointer unconditionally (0 here); no CloseHandle for -1. */
    CHECK(!strcmp(log_text, "DD") && deleted_count == 2 && deleted[0] == 0 && deleted[1] == 0 && !closed);
    CHECK(r32(0x004d1cf6u) == 0x004a3368u && r32(0x004d257fu) == 0x004a3368u && r32(0x006b4f04u) == 0x004a3368u);
    CHECK(r32(0x006b7ba4u) == 0x004a5b39u);

    /* ---- the other outcomes: new fails, a real handle, buffers and pools
     * present at exit, the function-local statics constructed. */
    CHECK(!setup());
    new_result = 0;
    create_result = 0x1234u;
    CHECK(!run_startup(&c));
    CHECK(r32(ES2_SIM_VA_TABLE_808) == 0 && r32(HEAP_BASE + 12u) == 0 && r32(ES2_SIM_VA_VXD_HANDLE) == 0x1234u);
    w32(0x004a961eu, 0x5100u);                        /* pool of exit record 2 */
    w32(0x004aa650u, 0x5200u);                        /* zero16 buffer of exit record 10 */
    w16(0x004d1cf6u + 0x14u, 1); w32(0x004d1cf6u + 0x16u, 0x5300u);   /* bitmap buffers, exit record 16 */
    w32(0x004d1cf6u + 0xau, 1); w32(0x004d1cf6u + 0xeu, 0x5400u);
    w8(0x0049cc26u, 1); w8(0x0049f1ecu, 1); w8(0x0049f032u, 1);
    w32(0x004d26e0u, 0x5500u);
    log_count = 0; memset(log_text, 0, sizeof(log_text)); deleted_count = 0;
    CHECK(!es2_sim_run_exit_records(&c));
    CHECK(closed == 0x1234u);
    /* The statics constructed on first use (never here) take 3 + 3 + 1 more. */
    CHECK(r32(STACK_BASE - 4u) == (uint32_t)-7);
    CHECK(r32(0x004d2754u) == 0x004a5b39u);
    /* Exit order: 25 X, 19 A (0x4d26e0), 16 A A (bitmap), 10 A, 7 D, 2 D, 0 D. */
    CHECK(!strcmp(log_text, "XAAAADDD"));
    CHECK(deleted[0] == 0x5500u && deleted[1] == 0x5300u && deleted[2] == 0x5400u && deleted[3] == 0x5200u);
    CHECK(deleted[4] == 0 && deleted[5] == 0x5100u && deleted[6] == 0);

    /* ---- a record VA without own code is refused through the dispatch. */
    CHECK(!setup());
    w32(0x004a79d8u + 6u * 30u + 2u, 0x00434a70u);
    memset(&c, 0, sizeof(c));
    c.memory = &memory; c.dispatch = &dispatch; c.stack_base = STACK_BASE;
    CHECK(!es2_sim_c0w32_entry(&c));
    CHECK(es2_sim_run_init_records(&c) == 1 && refusals == 1 && refused_va == 0x00434a70u && c.current == 0x00434a70u);
    CHECK(c.init_run == 30);
    /* The entry's DLL branch (byte 0x497058 set) is refused, not guessed. */
    CHECK(!setup());
    w8(0x00497058u, 1);
    memset(&c, 0, sizeof(c));
    c.memory = &memory; c.dispatch = &dispatch; c.stack_base = STACK_BASE;
    refusals = 0;
    CHECK(es2_sim_c0w32_entry(&c) == 1 && refusals == 1);
    CHECK(arena.faults == 0);
    puts("PASS: init records on a synthetic image: entry, 65 init and 26 exit records, both outcomes of every branch, refusals");
    return 0;
}

/* The record tables of the reference file against the own list. */
static int real(void) {
    const char *root = getenv("ES2_DATA_ROOT");
    if (!root || !*root) { puts("SKIP: ES2_DATA_ROOT is not set; no original data read"); return 0; }
    char path[1024];
    FILE *file = NULL;
    for (unsigned k = 0; k < 2 && !file; ++k) {
        snprintf(path, sizeof(path), k ? "%s/extracted/cd/VER95/DBSIM.EXE" : "%s/VER95/DBSIM.EXE", root);
        file = fopen(path, "rb");
    }
    if (!file) { puts("SKIP: no DBSIM.EXE below ES2_DATA_ROOT"); return 0; }
    uint8_t *bytes = malloc(ES2_SIM_DBSIM_FILE_SIZE + 1u);
    CHECK(bytes);
    size_t got = fread(bytes, 1, ES2_SIM_DBSIM_FILE_SIZE + 1u, file);
    fclose(file);
    const char *error = NULL;
    CHECK(got == ES2_SIM_DBSIM_FILE_SIZE && !es2_sim_image_gate(bytes, got, ES2_SIM_DBSIM_FILE_SIZE, ES2_SIM_DBSIM_SHA256, &error));
    es2_sim_image_info info;
    CHECK(!es2_sim_image_parse(bytes, got, &info, &error));
    uint8_t *placed = calloc(1, info.image_size);
    CHECK(placed && !es2_sim_image_place(bytes, got, &info, placed, &error));
    free(bytes);
    #define AT32(va) ((uint32_t)placed[(va) - IMAGE_BASE] | (uint32_t)placed[(va) - IMAGE_BASE + 1] << 8 | \
                      (uint32_t)placed[(va) - IMAGE_BASE + 2] << 16 | (uint32_t)placed[(va) - IMAGE_BASE + 3] << 24)
    CHECK(AT32(ES2_SIM_VA_MODULE_TABLE) == 0x004a79d8u && AT32(ES2_SIM_VA_MODULE_TABLE + 4u) == 0x004a7b5eu);
    CHECK(AT32(ES2_SIM_VA_MODULE_TABLE + 8u) == 0x004a7b5eu && AT32(ES2_SIM_VA_MODULE_TABLE + 12u) == 0x004a7bfau);
    for (unsigned mode = 0; mode < 2; ++mode) {
        uint32_t start = mode ? 0x004a7b5eu : 0x004a79d8u;
        unsigned count = mode ? ES2_SIM_EXIT_RECORDS : ES2_SIM_INIT_RECORDS;
        for (unsigned i = 0; i < count; ++i) {
            uint32_t r = start + 6u * i;
            CHECK(placed[r - IMAGE_BASE] == 0 && placed[r + 1u - IMAGE_BASE] == 0x20);
            CHECK(AT32(r + 2u) == es2_sim_record_va(mode, i) && es2_sim_record_function(AT32(r + 2u), mode));
        }
    }
    /* The bytes the entry and record 64 rely on. */
    CHECK(placed[0x00497058u - IMAGE_BASE] == 0);
    CHECK(!memcmp(placed + (0x004a6a60u - IMAGE_BASE), "\\\\.\\DARKMONO.VXD", 17));
    free(placed);
    puts("PASS: the reference file's 65 init and 26 exit records are the own list, in table order, priority 0x20, flag 0");
    return 0;
}

int main(void) { return synthetic() || real(); }
