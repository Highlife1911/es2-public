/* Model of the CW3220.DLL heap. Read from the llvm-objdump listing of
 * the reference CW3220.DLL (link base 0x400000; addresses below are at that
 * base): _malloc 41729c, _free 417160, _calloc 416ffc, _realloc 417b8c,
 * __expand 417a70, the growth step 417028, __heapadd 4171f8, the OS layer
 * 4182b8 (reserve), 418324 (commit), 418378 (decommit), 4183b8 (release)
 * and the init record 4170fc. Contract and evidence:
 * docs/reference-game/rtl_imports.md, section "the heap, shadow stage". */
#include "es2/core/rtl_heap.h"

#include <string.h>

#define IN_USE 1u
#define MIN_BLOCK 16u
#define GROWTH_UNIT 0x10000u

static uint32_t rd(const es2_heap_env *env, uint32_t address) { return env->read32(env->context, address); }
static void wr(const es2_heap_env *env, uint32_t address, uint32_t value) { env->write32(env->context, address, value); }

uint32_t es2_heap_block_size(uint32_t n)
{
    return n < 8u ? MIN_BLOCK : ((n + 3u) & ~3u) + 8u;
}
static int refused(uint32_t n, uint32_t size) { return (int32_t)size < (int32_t)MIN_BLOCK || n == 0u; }

void es2_heap_init(es2_heap *heap, const es2_heap_env *env, uint32_t sentinel, uint32_t platform)
{
    memset(heap, 0, sizeof(*heap));
    heap->sentinel = sentinel;
    heap->rover = sentinel;
    heap->platform = platform;
    wr(env, sentinel - 4u, 0u);
    wr(env, sentinel - 8u, 0u);
    wr(env, sentinel + 4u, sentinel);
    wr(env, sentinel, sentinel);
}

/* ------------------------------------------------------------------------
 * OS layer: one VirtualAlloc/VirtualFree per 4 KiB page for commit and
 * decommit; reservations rounded to the granularity chosen at the first one. */
static int os_decommit(const es2_heap_env *env, uint32_t address, uint32_t size)
{
    for (; size; address += ES2_HEAP_PAGE, size -= ES2_HEAP_PAGE)
        if (env->virtual_free(env->context, address, ES2_HEAP_PAGE, ES2_HEAP_MEM_DECOMMIT) != 1u) return 0;
    return 1;
}
static int os_commit(const es2_heap_env *env, uint32_t address, uint32_t size)
{
    uint32_t start = address;
    for (; size; address += ES2_HEAP_PAGE, size -= ES2_HEAP_PAGE)
        if (!env->virtual_alloc(env->context, address, ES2_HEAP_PAGE, ES2_HEAP_MEM_COMMIT, ES2_HEAP_PAGE_READWRITE)) {
            os_decommit(env, start, address - start);
            return 0;
        }
    return 1;
}
static int os_reserve(es2_heap *heap, const es2_heap_env *env, uint32_t size, uint32_t *base, uint32_t *reserved)
{
    if (!heap->granularity) heap->granularity = (heap->platform == 1u || heap->platform == 2u) ? 0x100000u : 0x400000u;
    uint32_t unit = heap->granularity;
    uint32_t rounded = (size + unit - 1u) & ~(unit - 1u);
    *base = env->virtual_alloc(env->context, 0u, rounded, ES2_HEAP_MEM_RESERVE, ES2_HEAP_PAGE_NOACCESS);
    if (!*base) return 0;
    *reserved = rounded;
    return 1;
}
static void os_release(const es2_heap_env *env, uint32_t base)
{
    env->virtual_free(env->context, base, 0u, ES2_HEAP_MEM_RELEASE);
}

/* ------------------------------------------------------------------------
 * Free and the region list. */
void es2_heap_free(es2_heap *heap, const es2_heap_env *env, uint32_t p)
{
    if (!p) return;
    uint32_t size = rd(env, p - 4u);
    if (!(size & IN_USE)) return;
    size &= ~IN_USE;
    int joined_previous = 0;
    uint32_t previous = rd(env, p - 8u);              /* footer of the block below */
    if (!(previous & IN_USE)) {
        p -= previous;
        size += previous;
        joined_previous = 1;
    }
    uint32_t next = p + size;
    uint32_t next_size = rd(env, next - 4u);
    if (!(next_size & IN_USE)) {
        uint32_t after = rd(env, next + 4u), before = rd(env, next);
        wr(env, after, before);
        wr(env, before + 4u, after);
        if (next == heap->rover) heap->rover = after;
        size += next_size;
    }
    wr(env, p + size - 8u, size);
    wr(env, p - 4u, size);
    if (joined_previous) return;                       /* the block below stays where it is in the list */
    uint32_t first = rd(env, heap->sentinel + 4u);   /* insert right after the head */
    wr(env, heap->sentinel + 4u, p);
    wr(env, p + 4u, first);
    wr(env, p, heap->sentinel);
    wr(env, first, p);
}

/* __heapadd: a committed range becomes heap. It extends the region that ends
 * at `base` (the new block starts on that region's end marker) or opens a
 * new region (marker word, first block at base+8). The range is marked as
 * one block in use and freed, which joins it with a free block below. */
static int32_t heap_add(es2_heap *heap, const es2_heap_env *env, uint32_t base, uint32_t size)
{
    if (size < MIN_BLOCK) return -1;
    size &= ~3u;
    uint32_t p = base, i = 0;
    while ((int32_t)i < (int32_t)heap->region_count && heap->region_base[i] + heap->region_size[i] != base) ++i;
    if (i != heap->region_count) {
        heap->region_size[i] += size;
    } else {
        if (heap->region_count == ES2_HEAP_REGION_LIMIT) return -1;
        heap->region_base[i] = base;
        heap->region_size[i] = size;
        ++heap->region_count;
        size -= 8u;
        p = base + 8u;
        wr(env, base, IN_USE);
    }
    wr(env, p - 4u, size | IN_USE);
    wr(env, p + size - 8u, size | IN_USE);
    wr(env, p + size - 4u, IN_USE);
    es2_heap_free(heap, env, p);
    return 0;
}

/* Growth by at least `block` bytes in 64 KiB units: first whatever is still
 * reserved but uncommitted, then a new reservation; when that reservation
 * directly follows the previous one, only the missing part is committed. */
static int32_t grow(es2_heap *heap, const es2_heap_env *env, uint32_t block)
{
    uint32_t want = (block + GROWTH_UNIT - 1u) & ~(GROWTH_UNIT - 1u);
    uint32_t part = heap->reserved_end - heap->committed_end;
    if (part) {
        if (want < part) part = want;
        if (!os_commit(env, heap->committed_end, part)) return -1;
        heap_add(heap, env, heap->committed_end, part);
        heap->committed_end += part;
    }
    if (want <= part) return 0;
    uint32_t base, reserved;
    if (!os_reserve(heap, env, want, &base, &reserved)) return -1;
    if (base == heap->reserved_end) want -= part;
    if (!os_commit(env, base, want)) {
        os_release(env, base);
        return -1;
    }
    heap_add(heap, env, base, want);
    heap->committed_end = base + want;
    heap->reserved_end = base + reserved;
    return 0;
}

/* ------------------------------------------------------------------------
 * Allocation: next fit from the rover around the circular list; a remainder
 * of at least 16 bytes stays in the list in the block's place and becomes the
 * rover; a whole block leaves the list and moves the rover only when it was
 * the rover. After a full circle the heap grows (at most twice). */
static uint32_t take(es2_heap *heap, const es2_heap_env *env, uint32_t p, uint32_t size, uint32_t block)
{
    uint32_t before = rd(env, p), after = rd(env, p + 4u), rest = size - block;
    if (rest < MIN_BLOCK) {
        wr(env, before + 4u, after);
        wr(env, after, before);
        if (p == heap->rover) heap->rover = after;
        wr(env, p + size - 8u, size | IN_USE);
        wr(env, p - 4u, size | IN_USE);
        return p;
    }
    uint32_t q = p + block;
    wr(env, before + 4u, q);
    heap->rover = q;
    wr(env, after, q);
    wr(env, q + 4u, after);
    wr(env, q, before);
    wr(env, q - 8u, block | IN_USE);
    wr(env, p - 4u, block | IN_USE);
    wr(env, q + rest - 8u, rest);
    wr(env, q - 4u, rest);
    return p;
}

uint32_t es2_heap_malloc(es2_heap *heap, const es2_heap_env *env, uint32_t n)
{
    uint32_t block = es2_heap_block_size(n);
    if (refused(n, block)) return 0;
    uint32_t p = heap->rover;
    for (unsigned grown = 0;;) {
        uint32_t size = rd(env, p - 4u);
        if (block <= size) return take(heap, env, p, size, block);
        p = rd(env, p + 4u);
        if (p != heap->rover) continue;
        if (grown == 2u) return 0;
        ++grown;
        if (grow(heap, env, block)) return 0;
        p = heap->rover;
    }
}

uint32_t es2_heap_calloc(es2_heap *heap, const es2_heap_env *env, uint32_t count, uint32_t n)
{
    uint32_t total = count * n;
    uint32_t p = es2_heap_malloc(heap, env, total);
    if (p && env->fill) env->fill(env->context, p, total);
    return p;
}

/* _expand: resize in place. Shrinking splits off a remainder of at least 16
 * bytes (freed, so it joins a free block above); growing takes the free block
 * directly above when the two together are large enough, keeping a remainder
 * of at least 16 bytes free in its list position. 0 when it cannot. */
uint32_t es2_heap_expand(es2_heap *heap, const es2_heap_env *env, uint32_t p, uint32_t n)
{
    uint32_t block = es2_heap_block_size(n);
    if (refused(n, block)) return 0;
    uint32_t current = rd(env, p - 4u) & ~IN_USE;
    if (block == current) return p;
    if (current >= block) {
        uint32_t rest = current - block;
        if (rest >= MIN_BLOCK) {
            uint32_t q = p + block;
            wr(env, q - 8u, block | IN_USE);
            wr(env, p - 4u, block | IN_USE);
            wr(env, q + rest - 8u, rest | IN_USE);
            wr(env, q - 4u, rest | IN_USE);
            es2_heap_free(heap, env, q);
        }
        return p;
    }
    uint32_t next = p + current, next_size = rd(env, next - 4u);
    if (next_size & IN_USE) return 0;
    if (block > current + next_size) return 0;
    uint32_t before = rd(env, next);
    uint32_t after = rd(env, next + 4u);
    if (next == heap->rover) heap->rover = after;
    uint32_t total = current + next_size, rest = total - block;
    if (rest < MIN_BLOCK) {
        wr(env, before + 4u, after);
        wr(env, after, before);
        wr(env, p + total - 8u, total | IN_USE);
        wr(env, p - 4u, total | IN_USE);
        return p;
    }
    uint32_t q = p + block;
    wr(env, before + 4u, q);
    wr(env, after, q);
    wr(env, q + 4u, after);
    wr(env, q, before);
    wr(env, q - 8u, block | IN_USE);
    wr(env, p - 4u, block | IN_USE);
    wr(env, q + rest - 8u, rest);
    wr(env, q - 4u, rest);
    return p;
}

/* realloc: n = 0 frees, p = 0 allocates; then _expand; then the free block
 * directly below when it and the block together are large enough (the data
 * moves down; a remainder of at least 16 bytes is freed); otherwise a new
 * block, the data copied, the old block freed. */
uint32_t es2_heap_realloc(es2_heap *heap, const es2_heap_env *env, uint32_t p, uint32_t n)
{
    if (!n) {
        es2_heap_free(heap, env, p);
        return 0;
    }
    if (!p) return es2_heap_malloc(heap, env, n);
    if (es2_heap_expand(heap, env, p, n)) return p;
    uint32_t block = es2_heap_block_size(n);
    if (refused(n, block)) return 0;
    uint32_t current = rd(env, p - 4u) & ~IN_USE;
    uint32_t below = rd(env, p - 8u);
    if (!(below & IN_USE) && below + current >= block) {
        uint32_t q = p - below;
        uint32_t after = rd(env, q + 4u), before = rd(env, q);
        wr(env, before + 4u, after);
        wr(env, after, before);
        if (q == heap->rover) heap->rover = after;
        if (env->move) env->move(env->context, q, p, current);
        uint32_t total = current + below, rest = total - block;
        if (rest < MIN_BLOCK) {
            wr(env, q + total - 8u, total | IN_USE);
            wr(env, q - 4u, total | IN_USE);
        } else {
            uint32_t r = q + block;
            wr(env, r - 8u, block | IN_USE);
            wr(env, q - 4u, block | IN_USE);
            wr(env, r + rest - 8u, rest | IN_USE);
            wr(env, r - 4u, rest | IN_USE);
            es2_heap_free(heap, env, r);
        }
        return q;
    }
    uint32_t r = es2_heap_malloc(heap, env, n);
    if (r) {
        if (env->move) env->move(env->context, r, p, current - 8u);
        es2_heap_free(heap, env, p);
    }
    return r;
}

/* ------------------------------------------------------------------------
 * Walk and attach. */
static void visit_word(es2_heap_visit visit, void *context, uint32_t address, uint32_t value)
{
    if (visit) visit(context, address, value);
}
int32_t es2_heap_walk(const es2_heap *heap, uint32_t (*read32)(void *context, uint32_t address), void *read_context,
                      es2_heap_visit visit, void *visit_context)
{
    if (heap->region_count > ES2_HEAP_REGION_LIMIT) return -1;
    uint32_t s = heap->sentinel;
    for (uint32_t k = 0; k < 4u; ++k) {
        uint32_t address = s - 8u + 4u * k;
        visit_word(visit, visit_context, address, read32(read_context, address));
    }
    int32_t blocks = 0;
    for (uint32_t i = 0; i < heap->region_count; ++i) {
        uint32_t base = heap->region_base[i], end = base + heap->region_size[i];
        if (end < base || heap->region_size[i] < 8u + MIN_BLOCK) return -1;
        uint32_t marker = read32(read_context, base);
        if (marker != IN_USE) return -1;
        visit_word(visit, visit_context, base, marker);
        uint32_t p = base + 8u;
        while (p < end) {
            uint32_t header = read32(read_context, p - 4u), size = header & ~IN_USE;
            if (size < MIN_BLOCK || (size & 3u) || size > end - p) return -1;
            uint32_t footer = read32(read_context, p + size - 8u);
            if (footer != header) return -1;
            visit_word(visit, visit_context, p - 4u, header);
            visit_word(visit, visit_context, p + size - 8u, footer);
            if (!(header & IN_USE)) {
                visit_word(visit, visit_context, p, read32(read_context, p));
                visit_word(visit, visit_context, p + 4u, read32(read_context, p + 4u));
            }
            p += size;
            ++blocks;
        }
        if (p != end) return -1;
        marker = read32(read_context, end - 4u);
        if (marker != IN_USE) return -1;
        visit_word(visit, visit_context, end - 4u, marker);
    }
    return blocks;
}

static void attach_word(void *context, uint32_t address, uint32_t value)
{
    const es2_heap_env *env = context;
    wr(env, address, value);
}
int32_t es2_heap_attach(es2_heap *heap, const es2_heap *state, const es2_heap_env *env,
                        uint32_t (*read32)(void *context, uint32_t address), void *read_context)
{
    *heap = *state;
    return es2_heap_walk(state, read32, read_context, attach_word, (void *)env);
}

/* ------------------------------------------------------------------------
 * Word store: open addressing over the word address, empty key 0xffffffff
 * (allocator words are 4-aligned). Filled to at most three quarters. */
#define WORD_EMPTY 0xffffffffu
void es2_heap_words_init(es2_heap_words *words, uint32_t *keys, uint32_t *values, uint32_t bits)
{
    words->keys = keys;
    words->values = values;
    words->bits = bits;
    words->limit = (uint32_t)(((uint64_t)1u << bits) / 4u * 3u);
    es2_heap_words_clear(words);
}
void es2_heap_words_clear(es2_heap_words *words)
{
    memset(words->keys, 0xff, sizeof(uint32_t) << words->bits);
    words->count = 0;
    words->overflow = 0;
    words->missing = 0;
}
static uint32_t slot_of(const es2_heap_words *words, uint32_t address)
{
    uint32_t mask = (uint32_t)(((uint64_t)1u << words->bits) - 1u);
    uint32_t h = (uint32_t)(((uint64_t)(address >> 2) * 0x9e3779b1u) >> (32u - words->bits)) & mask;
    while (words->keys[h] != WORD_EMPTY && words->keys[h] != address) h = (h + 1u) & mask;
    return h;
}
int es2_heap_words_find(const es2_heap_words *words, uint32_t address, uint32_t *value)
{
    if (address == WORD_EMPTY) return 0;
    uint32_t h = slot_of(words, address);
    if (words->keys[h] != address) return 0;
    *value = words->values[h];
    return 1;
}
uint32_t es2_heap_words_read(es2_heap_words *words, uint32_t address)
{
    uint32_t value;
    if (es2_heap_words_find(words, address, &value)) return value;
    ++words->missing;
    return 0;
}
void es2_heap_words_write(es2_heap_words *words, uint32_t address, uint32_t value)
{
    if (address == WORD_EMPTY) { words->overflow = 1; return; }
    uint32_t h = slot_of(words, address);
    if (words->keys[h] != address) {
        if (words->count >= words->limit) { words->overflow = 1; return; }
        words->keys[h] = address;
        ++words->count;
    }
    words->values[h] = value;
}

/* Shadow env: the model's words in the store (each write journaled), the
 * Win32 calls replayed from the original's record. */
static uint32_t shadow_read(void *context, uint32_t address)
{
    es2_heap_shadow *s = context;
    return es2_heap_words_read(&s->words, address);
}
static void shadow_write(void *context, uint32_t address, uint32_t value)
{
    es2_heap_shadow *s = context;
    es2_heap_words_write(&s->words, address, value);
    if (s->journal_count < s->journal_capacity) s->journal[s->journal_count++] = address;
    else s->journal_overflow = 1;
}
static const es2_heap_os_call *shadow_next(es2_heap_shadow *s, const es2_heap_os_call *want)
{
    if (s->call_next >= s->call_count) {
        if (!s->os_mismatch) { s->os_mismatch = 1; s->os_mismatch_index = s->call_next; }
        return NULL;
    }
    const es2_heap_os_call *c = &s->calls[s->call_next++];
    if (c->kind != want->kind || c->address != want->address || c->size != want->size || c->type != want->type ||
        c->protect != want->protect) {
        if (!s->os_mismatch) { s->os_mismatch = 1; s->os_mismatch_index = s->call_next - 1u; }
        return c->kind == want->kind ? c : NULL;
    }
    return c;
}
static uint32_t shadow_alloc(void *context, uint32_t address, uint32_t size, uint32_t type, uint32_t protect)
{
    es2_heap_os_call want = {ES2_HEAP_OS_ALLOC, address, size, type, protect, 0};
    const es2_heap_os_call *c = shadow_next(context, &want);
    return c ? c->result : 0u;
}
static uint32_t shadow_free(void *context, uint32_t address, uint32_t size, uint32_t type)
{
    es2_heap_os_call want = {ES2_HEAP_OS_FREE, address, size, type, 0, 0};
    const es2_heap_os_call *c = shadow_next(context, &want);
    return c ? c->result : 0u;
}
void es2_heap_shadow_env(es2_heap_env *env, es2_heap_shadow *shadow)
{
    memset(env, 0, sizeof(*env));
    env->context = shadow;
    env->read32 = shadow_read;
    env->write32 = shadow_write;
    env->virtual_alloc = shadow_alloc;
    env->virtual_free = shadow_free;
}
void es2_heap_shadow_begin(es2_heap_shadow *shadow, const es2_heap_os_call *calls, uint32_t count)
{
    shadow->journal_count = 0;
    shadow->journal_overflow = 0;
    shadow->calls = calls;
    shadow->call_count = count;
    shadow->call_next = 0;
    shadow->os_mismatch = 0;
    shadow->os_mismatch_index = 0;
}
int es2_heap_shadow_finish(es2_heap_shadow *shadow)
{
    if (!shadow->os_mismatch && shadow->call_next != shadow->call_count) {
        shadow->os_mismatch = 1;
        shadow->os_mismatch_index = shadow->call_next;
    }
    return shadow->os_mismatch;
}
