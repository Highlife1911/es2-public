/* The own heap (es2/sim/heap.h) over a synthetic arena and an emulated OS:
 * CW3220's block contract (zero fill, header s|1 at p-4, 4-byte alignment,
 * NULL for 0), the preferred reservation base, the documented registers,
 * a valid heap walk, and refusals of foreign pointers. No game data. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/sim/heap.h"
#include "es2/sim/memory.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

#define HEAP_BASE 0x20000000u
#define HEAP_SIZE 0x01000000u   /* 16 MiB of guest addresses */
#define HEAD_BASE 0x00100000u

/* Emulated OS: reservations inside the arena only, at the asked address or
 * (address 0) the lowest free 4 MiB-aligned place above `system_floor`. */
typedef struct fake_os {
    uint32_t reserved_lo[16], reserved_hi[16];
    unsigned count, calls, frees;
    uint32_t system_floor;
} fake_os;
static int overlaps(const fake_os *o, uint32_t lo, uint32_t hi) {
    for (unsigned i = 0; i < o->count; ++i) if (lo < o->reserved_hi[i] && o->reserved_lo[i] < hi) return 1;
    return 0;
}
static uint32_t fake_alloc(void *c, uint32_t address, uint32_t size, uint32_t type, uint32_t protect) {
    fake_os *o = c;
    ++o->calls;
    (void)protect;
    if (type & ES2_HEAP_MEM_RESERVE) {
        if (!address) {
            for (uint32_t at = o->system_floor; at + size <= HEAP_BASE + HEAP_SIZE; at += 0x400000u)
                if (!overlaps(o, at, at + size)) { address = at; break; }
            if (!address) return 0;
        }
        if (address < HEAP_BASE || address + size > HEAP_BASE + HEAP_SIZE || overlaps(o, address, address + size) || o->count >= 16) return 0;
        o->reserved_lo[o->count] = address; o->reserved_hi[o->count] = address + size; ++o->count;
        return address;
    }
    return address;   /* commit inside a reservation */
}
static uint32_t fake_free(void *c, uint32_t address, uint32_t size, uint32_t type) {
    (void)address; (void)size; (void)type;
    ++((fake_os *)c)->frees;
    return 1;
}

static uint8_t *arena_bytes, head_bytes[64];
static es2_sim_arena arena;
static es2_guest_memory memory;
static fake_os os_state;
static es2_sim_heap heap;

static void init_sized(uint32_t preferred, uint32_t size) {
    memset(arena_bytes, 0xee, HEAP_SIZE);              /* not zero: calloc must clear */
    memset(head_bytes, 0, sizeof(head_bytes));
    memset(&os_state, 0, sizeof(os_state));
    os_state.system_floor = HEAP_BASE + 0x00800000u;
    es2_sim_heap_os os = {&os_state, fake_alloc, fake_free};
    es2_sim_heap_init(&heap, &memory, &os, HEAD_BASE + 8u, 0, preferred, size);
}
static void init(uint32_t preferred) { init_sized(preferred, preferred ? 0x00800000u : 0); }

static uint32_t rd(uint32_t a) { return memory.read32(memory.context, a); }

int main(void) {
    arena_bytes = malloc(HEAP_SIZE);
    CHECK(arena_bytes);
    CHECK(es2_sim_arena_add(&arena, HEAP_BASE, arena_bytes, HEAP_SIZE));
    CHECK(es2_sim_arena_add(&arena, HEAD_BASE, head_bytes, sizeof(head_bytes)));
    es2_sim_arena_memory(&arena, &memory);

    /* Preferred base: the first reservation lands there, not at the system's choice. */
    init(HEAP_BASE);
    es2_rtl_regs r = {0x11, 0x22, 0x33};
    uint32_t p = es2_sim_heap_calloc(&heap, 1, 0x808, &r);
    CHECK(p && p - HEAP_BASE < 0x00400000u && heap.os_calls >= 2);
    CHECK(r.eax == p && r.ecx == 0 && r.edx == 0x808);
    CHECK(rd(p - 4) == (0x810u | 1u) && (p & 3u) == 0);          /* ((0x808 + 3) & ~3) + 8 = 0x810, in use */
    for (uint32_t i = 0; i < 0x808; i += 4) CHECK(rd(p + i) == 0);
    CHECK(es2_sim_heap_owns(&heap, p) && !es2_sim_heap_owns(&heap, HEAD_BASE));
    /* calloc(0) = NULL: ECX = count*n, EDX unchanged. */
    r.eax = 1; r.ecx = 2; r.edx = 0x55;
    CHECK(es2_sim_heap_calloc(&heap, 1, 0, &r) == 0 && r.ecx == 0 && r.edx == 0x55);
    /* Small requests become 16-byte blocks; distinct and aligned. */
    uint32_t q = es2_sim_heap_calloc(&heap, 1, 3, &r), s = es2_sim_heap_calloc(&heap, 2, 6, &r);
    CHECK(q && s && q != s && rd(q - 4) == 17u && rd(s - 4) == (20u | 1u) && !(q & 3u) && !(s & 3u));
    /* free registers: a block whose neighbour below is in use is inserted
     * after the head block: EAX = p, ECX = the former next, EDX = the head. */
    r.eax = r.ecx = r.edx = 0;
    CHECK(!es2_sim_heap_free(&heap, p, &r));
    CHECK(r.eax == p && r.edx == HEAD_BASE + 8u && rd(p - 4) == 0x810u);
    /* free(0): EAX 0, ECX/EDX unchanged. */
    r.eax = 9; r.ecx = 8; r.edx = 7;
    CHECK(!es2_sim_heap_free(&heap, 0, &r) && r.eax == 0 && r.ecx == 8 && r.edx == 7);
    /* realloc keeps the contents; ECX/EDX unchanged. */
    memory.write32(memory.context, q, 0xdeadbeefu);
    r.ecx = 0x66; r.edx = 0x77;
    CHECK(!es2_sim_heap_realloc(&heap, q, 0x100, &r) && r.eax && rd(r.eax) == 0xdeadbeefu && r.ecx == 0x66 && r.edx == 0x77);
    /* Foreign pointers are refused, never passed on. */
    CHECK(es2_sim_heap_free(&heap, HEAD_BASE + 16u, &r) == 1);
    CHECK(es2_sim_heap_realloc(&heap, HEAD_BASE + 16u, 4, &r) == 1);
    /* The allocator's words form a valid heap. */
    CHECK(es2_heap_walk(&heap.heap, memory.read32, memory.context, NULL, NULL) >= 3);
    CHECK(arena.faults == 0);

    /* Without a preference the system's choice is used. */
    init(0);
    uint32_t t = es2_sim_heap_calloc(&heap, 1, 64, &r);
    CHECK(t >= HEAP_BASE + 0x00800000u && heap.preferred_misses == 0);
    /* A preference that cannot be had falls back to the system. */
    init(HEAP_BASE + HEAP_SIZE - 0x1000u);
    t = es2_sim_heap_calloc(&heap, 1, 64, &r);
    CHECK(t >= HEAP_BASE + 0x00800000u);

    /* Reservations at the preferred base never adjoin: growing past the
     * first 4 MiB opens a second region at base + 4 MiB + the gap (its block
     * at the region's base + 8); the first region keeps its free tail. */
    init_sized(HEAP_BASE, 0x00a00000u);
    uint32_t big = es2_sim_heap_calloc(&heap, 1, 0x3f0000u, &r), grown = es2_sim_heap_calloc(&heap, 1, 0x7a120u, &r);
    CHECK(big == HEAP_BASE + 8u && grown == HEAP_BASE + 0x00400000u + ES2_SIM_HEAP_RESERVATION_GAP + 8u);
    CHECK(heap.heap.region_count == 2 && heap.heap.region_size[0] == 0x00400000u);
    CHECK(es2_heap_walk(&heap.heap, memory.read32, memory.context, NULL, NULL) >= 2);

    /* CW3220's attach blocks: the pointer array, one block per string
     * (strlen + 1, copied), the array NULL-terminated; then the static
     * objects' ten blocks in their order. */
    init_sized(HEAP_BASE, 0x00a00000u);
    es2_sim_heap_copy_environment(&heap, "A=1\0BB=22\0");
    uint32_t array = HEAP_BASE + 8u;                    /* (2 + 1) * 4 = 12: a 20-byte block */
    CHECK(rd(array - 4) == (20u | 1u) && rd(array) == array + 20u && rd(array + 4) == array + 36u && rd(array + 8) == 0);
    CHECK(rd(array + 16) == (16u | 1u) && rd(array + 32) == (16u | 1u));
    CHECK(!memcmp(memory.host(memory.context, array + 20u), "A=1", 4) && !memcmp(memory.host(memory.context, array + 36u), "BB=22", 6));
    es2_sim_heap_static_objects(&heap);
    static const uint32_t statics[] = {68, 524, 68, 524, 68, 524, 264, 28, 72, 16};
    uint32_t at = array + 52u;
    for (unsigned i = 0; i < sizeof(statics) / sizeof(statics[0]); ++i) { CHECK(rd(at - 4) == (statics[i] | 1u)); at += statics[i]; }
    CHECK(heap.heap.rover == at && !(rd(at - 4) & 1u));    /* the top free block follows */
    es2_sim_heap_copy_environment(&heap, NULL);           /* no block: an empty array */
    CHECK(rd(at - 4) == (16u | 1u) && rd(at) == 0);
    CHECK(arena.faults == 0);

    /* The guest's environment: the launcher's __COMPAT_LAYER (handed on as
     * ES2SIM_COMPAT_LAYER) at its sorted place, none without it. */
    {
        static const char plain[] = "=C:=C:\\x\0A=1\0USERNAME=a\0windir=C:\\W\0";
        char *same = es2_sim_guest_environment(plain);
        CHECK(same && !memcmp(same, plain, sizeof plain));
        free(same);
        CHECK(es2_sim_guest_environment(NULL) == NULL);

        /* After windir: upper-cased, '_' sorts after the letters. */
        static const char with[] = "=C:=C:\\x\0A=1\0ES2SIM_COMPAT_LAYER=256COLOR\0USERNAME=a\0windir=C:\\W\0";
        static const char want[] = "=C:=C:\\x\0A=1\0USERNAME=a\0windir=C:\\W\0__COMPAT_LAYER=256COLOR\0";
        char *got = es2_sim_guest_environment(with);
        CHECK(got && !memcmp(got, want, sizeof want));
        free(got);

        /* In the middle: "USE_X" and "_Z" sort before it, "__COMPAT_LAYERX"
         * and "~t" after it; the lower-case "__compat_layer" is the same name
         * and is replaced. */
        static const char mid[] = "A=1\0USE_X=1\0_Z=2\0__compat_layer=old\0__COMPAT_LAYERX=3\0ES2SIM_COMPAT_LAYER=NEW\0~t=4\0";
        static const char mid_want[] = "A=1\0USE_X=1\0_Z=2\0__COMPAT_LAYER=NEW\0__COMPAT_LAYERX=3\0~t=4\0";
        got = es2_sim_guest_environment(mid);
        CHECK(got && !memcmp(got, mid_want, sizeof mid_want));
        free(got);

        /* An empty hand-over is dropped and adds nothing; a layer of es2sim's
         * own stays when nothing is handed on. */
        static const char empty[] = "A=1\0ES2SIM_COMPAT_LAYER=\0__COMPAT_LAYER=own\0";
        static const char empty_want[] = "A=1\0__COMPAT_LAYER=own\0";
        got = es2_sim_guest_environment(empty);
        CHECK(got && !memcmp(got, empty_want, sizeof empty_want));
        free(got);
        static const char own[] = "A=1\0__COMPAT_LAYER=own\0";
        got = es2_sim_guest_environment(own);
        CHECK(got && !memcmp(got, own, sizeof own));
        free(got);

        /* The copy: "__COMPAT_LAYER=256COLOR" is 23 characters, a 24-byte
         * string and a 32-byte block (header 33); the array holds one more word. */
        init_sized(HEAP_BASE, 0x00a00000u);
        got = es2_sim_guest_environment("A=1\0ES2SIM_COMPAT_LAYER=256COLOR\0");
        es2_sim_heap_copy_environment(&heap, got);
        free(got);
        uint32_t a = HEAP_BASE + 8u;                    /* (2 + 1) * 4 = 12: a 20-byte block */
        CHECK(rd(a - 4) == (20u | 1u) && rd(a) == a + 20u && rd(a + 4) == a + 36u && rd(a + 8) == 0);
        CHECK(!memcmp(memory.host(memory.context, a + 20u), "A=1", 4));
        CHECK(rd(a + 36u - 4) == (32u | 1u) && !memcmp(memory.host(memory.context, a + 36u), "__COMPAT_LAYER=256COLOR", 24));
    }
    puts("PASS: own heap: preferred base, zero fill, header s|1, alignment, NULL for 0, the documented registers, walk, foreign refused, "
         "reservation gap, attach blocks, guest environment");
    free(arena_bytes);
    return 0;
}
