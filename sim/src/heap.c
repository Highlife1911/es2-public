/* The own process's heap; see es2/sim/heap.h. The register results follow
 * rtl_imports.md, "the heap, replaced" (confirmed in vitro there against
 * CW3220). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/sim/heap.h"
#include "es2/sim/guest_memory.h"

static uint32_t env_read(void *c, uint32_t a) { const es2_sim_heap *h = c; return h->memory->read32(h->memory->context, a); }
static void env_write(void *c, uint32_t a, uint32_t v) { const es2_sim_heap *h = c; h->memory->write32(h->memory->context, a, v); }
static void env_fill(void *c, uint32_t a, uint32_t count) {
    const es2_sim_heap *h = c;
    /* Byte stores through the view: any alignment, any count. */
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t *p = h->memory->host(h->memory->context, a + i);
        if (p) *p = 0;
    }
}
static void env_move(void *c, uint32_t dest, uint32_t source, uint32_t count) {
    const es2_sim_heap *h = c;
    /* memmove semantics: copy backwards when the destination is above. */
    if (dest > source) {
        for (uint32_t i = count; i-- > 0;) {
            uint8_t *d = h->memory->host(h->memory->context, dest + i), *s = h->memory->host(h->memory->context, source + i);
            if (d && s) *d = *s;
        }
    } else if (dest < source) {
        for (uint32_t i = 0; i < count; ++i) {
            uint8_t *d = h->memory->host(h->memory->context, dest + i), *s = h->memory->host(h->memory->context, source + i);
            if (d && s) *d = *s;
        }
    }
}
static uint32_t env_alloc(void *c, uint32_t address, uint32_t size, uint32_t type, uint32_t protect) {
    es2_sim_heap *h = c;
    ++h->os_calls;
    uint32_t result = 0;
    /* The web build's host-heap ceiling (es2/sim/guest_memory.h). */
    if (type & ES2_HEAP_MEM_RESERVE) es2_sim_guest_check_heap();
    /* A reservation the allocator leaves to the system goes to the preferred
     * base first, so that runs repeat their addresses. The next one starts
     * ES2_SIM_HEAP_RESERVATION_GAP higher: never adjoining, as the original's
     * (heap.h). */
    if (!address && (type & ES2_HEAP_MEM_RESERVE) && h->preferred_next &&
        size <= h->preferred_end - h->preferred_next) {
        result = h->os.virtual_alloc(h->os.context, h->preferred_next, size, type, protect);
        if (result) {
            uint32_t end = result + size;
            h->preferred_next = h->preferred_end - end > ES2_SIM_HEAP_RESERVATION_GAP ? end + ES2_SIM_HEAP_RESERVATION_GAP
                                                                                        : h->preferred_end;
        } else ++h->preferred_misses;
    }
    if (!result) result = h->os.virtual_alloc(h->os.context, address, size, type, protect);
    if (!result) ++h->os_failed;
    return result;
}
static uint32_t env_release(void *c, uint32_t address, uint32_t size, uint32_t type) {
    es2_sim_heap *h = c;
    ++h->os_calls;
    uint32_t result = h->os.virtual_free(h->os.context, address, size, type);
    if (result != 1u) ++h->os_failed;
    return result;
}

void es2_sim_heap_init(es2_sim_heap *h, const es2_guest_memory *memory, const es2_sim_heap_os *os,
                       uint32_t sentinel, uint32_t platform, uint32_t preferred_base, uint32_t preferred_size) {
    h->memory = memory;
    h->os = *os;
    h->preferred_next = preferred_base;
    h->preferred_end = preferred_base ? preferred_base + preferred_size : 0;
    h->os_calls = h->os_failed = h->preferred_misses = 0;
    h->observe = NULL;
    h->observe_user = NULL;
    h->caller = 0;
    es2_heap_env env = {h, env_read, env_write, env_fill, env_move, env_alloc, env_release};
    h->env = env;
    es2_heap_init(&h->heap, &h->env, sentinel, platform);
}

int es2_sim_heap_owns(const es2_sim_heap *h, uint32_t p) {
    for (uint32_t i = 0; i < h->heap.region_count && i < ES2_HEAP_REGION_LIMIT; ++i)
        if (p - h->heap.region_base[i] < h->heap.region_size[i]) return 1;
    return 0;
}

static void observed(es2_sim_heap *h, es2_sim_heap_function function, int internal, uint32_t a0, uint32_t a1, uint32_t result) {
    if (h->observe) h->observe(h->observe_user, function, internal, internal ? 0 : h->caller, a0, a1, result);
    h->caller = 0;
}

uint32_t es2_sim_heap_calloc(es2_sim_heap *h, uint32_t count, uint32_t n, es2_rtl_regs *regs) {
    uint32_t total = count * n;
    regs->eax = es2_heap_calloc(&h->heap, &h->env, count, n);
    /* After the zero fill ECX = 0 and EDX = count*n; NULL: ECX = count*n,
     * EDX unchanged (exact for a refused size; out of memory the original's
     * EDX comes from the growth code and is not reproduced). */
    if (regs->eax) { regs->ecx = 0; regs->edx = total; }
    else regs->ecx = total;
    observed(h, ES2_SIM_HEAP_CALLOC, 0, count, n, regs->eax);
    return regs->eax;
}

int es2_sim_heap_free(es2_sim_heap *h, uint32_t p, es2_rtl_regs *regs) {
    if (p && !es2_sim_heap_owns(h, p)) return 1;
    /* CW3220 _free (0x417160): p = 0 leaves ECX/EDX and gives EAX 0; a block
     * without the in-use flag leaves ECX, EAX = p, EDX = its header word;
     * otherwise EAX = the start of the merged block and, when the block
     * below absorbed it, ECX = start + size, EDX = size, else ECX = the head
     * block's former next block and EDX = the head block. */
    regs->eax = p;
    if (!p) { observed(h, ES2_SIM_HEAP_FREE, 0, p, 0, 0); return 0; }
    uint32_t header = env_read(h, p - 4u);
    if (!(header & 1u)) { regs->edx = header; observed(h, ES2_SIM_HEAP_FREE, 0, p, 0, 0); return 0; }
    uint32_t below = env_read(h, p - 8u);
    int joined = !(below & 1u);
    uint32_t start = joined ? p - below : p;
    es2_heap_free(&h->heap, &h->env, p);
    uint32_t size = env_read(h, start - 4u);
    regs->eax = start;
    if (joined) { regs->ecx = start + size; regs->edx = size; }
    else { regs->ecx = env_read(h, start + 4u); regs->edx = h->heap.sentinel; }
    observed(h, ES2_SIM_HEAP_FREE, 0, p, 0, 0);
    return 0;
}

int es2_sim_heap_realloc(es2_sim_heap *h, uint32_t p, uint32_t n, es2_rtl_regs *regs) {
    if (p && !es2_sim_heap_owns(h, p)) return 1;
    /* EAX = the block; ECX/EDX unchanged (the original's depend on the nested
     * calls; dead at both DBSIM call sites). */
    regs->eax = es2_heap_realloc(&h->heap, &h->env, p, n);
    observed(h, ES2_SIM_HEAP_REALLOC, 0, p, n, regs->eax);
    return 0;
}

uint32_t es2_sim_heap_runtime_malloc(es2_sim_heap *h, uint32_t n) {
    uint32_t p = es2_heap_malloc(&h->heap, &h->env, n);
    observed(h, ES2_SIM_HEAP_MALLOC, 1, n, 0, p);
    return p;
}

int es2_sim_heap_runtime_free(es2_sim_heap *h, uint32_t p) {
    if (p && !es2_sim_heap_owns(h, p)) return 1;
    if (p) es2_heap_free(&h->heap, &h->env, p);
    observed(h, ES2_SIM_HEAP_FREE, 1, p, 0, 0);
    return 0;
}

/* ---- CW3220's attach (listing 0x41a050..0x41a0d4, 0x409f98, 0x40a67c,
 * 0x418444, 0x417dc8, 0x41b08c, 0x41d274, 0x41d444, 0x403444). */
void es2_sim_heap_copy_environment(es2_sim_heap *h, const char *block) {
    uint32_t count = 0;
    for (const char *e = block; e && *e; e += strlen(e) + 1u) ++count;
    uint32_t array = es2_sim_heap_runtime_malloc(h, (count + 1u) * 4u);
    if (!array) return;
    uint32_t i = 0;
    for (const char *e = block; e && *e; e += strlen(e) + 1u, ++i) {
        uint32_t n = (uint32_t)strlen(e) + 1u, p = es2_sim_heap_runtime_malloc(h, n);
        if (!p) break;
        for (uint32_t k = 0; k < n; ++k) {
            uint8_t *b = h->memory->host(h->memory->context, p + k);
            if (b) *b = (uint8_t)e[k];
        }
        h->memory->write32(h->memory->context, array + 4u * i, p);
    }
    h->memory->write32(h->memory->context, array + 4u * i, 0);
}

/* `entry`'s name (up to the first '=' after position 0, so "=C:=C:\x" is
 * "=C:") against `name`: upper-cased ASCII, a prefix first. This is the order
 * Windows keeps the block in. */
static int env_name_compare(const char *entry, const char *name) {
    size_t length = entry[0] ? 1 : 0;
    while (length && entry[length] && entry[length] != '=') ++length;
    size_t i = 0;
    for (; i < length && name[i]; ++i) {
        int a = (unsigned char)entry[i], b = (unsigned char)name[i];
        if (a >= 'a' && a <= 'z') a -= 'a' - 'A';
        if (b >= 'a' && b <= 'z') b -= 'a' - 'A';
        if (a != b) return a < b ? -1 : 1;
    }
    return i < length ? 1 : name[i] ? -1 : 0;
}

char *es2_sim_guest_environment(const char *block) {
    if (!block) return NULL;
    static const char layer[] = "__COMPAT_LAYER", carrier[] = "ES2SIM_COMPAT_LAYER";
    size_t total = 1;   /* the closing NUL */
    int carried = 0;
    const char *value = "";
    for (const char *e = block; *e; e += strlen(e) + 1u) {
        total += strlen(e) + 1u;
        if (!env_name_compare(e, carrier)) { carried = 1; value = e[sizeof carrier - 1] == '=' ? e + sizeof carrier : ""; }
    }
    char *out = malloc(total + sizeof layer + strlen(value) + 1u);
    if (!out) return NULL;
    char *at = out;
    int insert = carried && *value;
    for (const char *e = block; *e; e += strlen(e) + 1u) {
        size_t n = strlen(e) + 1u;
        if (carried && (!env_name_compare(e, carrier) || (insert && !env_name_compare(e, layer)))) continue;
        if (insert && env_name_compare(e, layer) > 0) {
            at += sprintf(at, "%s=%s", layer, value) + 1;
            insert = 0;
        }
        memcpy(at, e, n);
        at += n;
    }
    if (insert) at += sprintf(at, "%s=%s", layer, value) + 1;
    *at = 0;
    return out;
}

void es2_sim_heap_static_objects(es2_sim_heap *h) {
    static const uint32_t sizes[] = {
        0x3c, 0x204, 0x3c, 0x204, 0x3c, 0x204,   /* 0x409f98: the filebufs of handles 0, 1, 2 and their buffers */
        0x100,                                     /* 0x418444 */
        0x12, 0x40, 0x4,                           /* 0x417dc8: TStringRef, its characters, xmsg's string */
    };
    for (uint32_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) es2_sim_heap_runtime_malloc(h, sizes[i]);
}
