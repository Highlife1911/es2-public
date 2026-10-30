/* ES2SNAP1 version 2 (snapshot.h). The layout and every rule below are the
 * proxy's writer's (tools/reference-game/proxy/early_arming.h,
 * ea_snapshot_write and its helpers) over callbacks instead of the
 * process's own memory. */
#include "sim/oracle/snapshot.h"
#include <stdlib.h>
#include <string.h>

const es2_snapshot_region es2_snapshot_fixed_regions[ES2_SNAPSHOT_FIXED_COUNT] = {
    {0x004a9572u, 0x004a9dfcu - 0x004a9572u},   /* stage-2 type globals, actor pools and lists, player 0x4a9b84 */
    {0x004a9e00u, 0x004aa728u - 0x004a9e00u},   /* header and mission tables, variables 0x4a9e70 (2000), strings */
    {0x004aa728u, 0x004aaa7cu - 0x004aa728u},   /* status word, selection and remap, MEC, terrain object table */
    {0x004aab00u, 0x000249f0u},                 /* the table 0x428230 clears (stage 2, record 27) */
    {0x004cf878u, 0x14u},                       /* 0x428230's pointers 0x4cf878..0x4cf888 */
    {0x006b4f2cu, 0x10u},                       /* fires */
    {0x004d24bcu, 0x004d27b8u - 0x004d24bcu},   /* globals object: player, cameras, both RNG streams */
    {0x0049a871u, 0x0049a884u - 0x0049a871u},   /* pass 2's default position and angle statics */
    {0x004d03c8u, 4u}, {0x0049b0f8u, 4u}, {0x0049b034u, 4u},
    {0x004a0be0u, 4u},                          /* terrain object pointer */
    {0x006bb2f8u, 0x18u},                       /* arena header, threshold, CW3220 byte sum 0x6bb30c */
};

typedef struct buffer { uint8_t *data; size_t used, capacity; int failed; } buffer;
static void put(buffer *b, const void *p, size_t n) {
    if (b->failed || !n) return;
    if (b->used + n > b->capacity) {
        size_t c = b->capacity ? b->capacity : 65536u;
        while (c < b->used + n) c *= 2u;
        uint8_t *d = realloc(b->data, c);
        if (!d) { b->failed = 1; return; }
        b->data = d;
        b->capacity = c;
    }
    memcpy(b->data + b->used, p, n);
    b->used += n;
}
static void put32(buffer *b, uint32_t v) {
    uint8_t w[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    put(b, w, 4);
}
static void patch32(buffer *b, size_t at, uint32_t v) {
    if (b->failed || at + 4u > b->used) return;
    b->data[at] = (uint8_t)v; b->data[at + 1] = (uint8_t)(v >> 8); b->data[at + 2] = (uint8_t)(v >> 16); b->data[at + 3] = (uint8_t)(v >> 24);
}
static uint32_t get32(const es2_snapshot_source *s, uint32_t va) {
    uint8_t w[4];
    s->read(s->user, va, w, 4);
    return (uint32_t)w[0] | (uint32_t)w[1] << 8 | (uint32_t)w[2] << 16 | (uint32_t)w[3] << 24;
}
/* Copies n bytes in pieces (the guest view may be read in chunks). */
static void put_guest(buffer *b, const es2_snapshot_source *s, uint32_t va, uint32_t n) {
    uint8_t chunk[4096];
    while (n && !b->failed) {
        uint32_t k = n < sizeof(chunk) ? n : (uint32_t)sizeof(chunk);
        s->read(s->user, va, chunk, k);
        put(b, chunk, k);
        va += k;
        n -= k;
    }
}
static void put_region(buffer *b, const es2_snapshot_source *s, const es2_snapshot_input *in, uint32_t va, uint32_t length) {
    int ok = va >= in->image_base && va - in->image_base <= in->image_size && length <= in->image_size - (va - in->image_base) &&
             s->readable(s->user, va, length);
    put32(b, va); put32(b, length); put32(b, ok ? 1u : 0u);
    if (ok) put_guest(b, s, va, length);
}
/* Block count of one region, or -1 when malformed (ea_heap_count). */
static int32_t heap_count(const es2_snapshot_source *s, uint32_t base, uint32_t size) {
    uint32_t end = base + size;
    if (end < base || size < 24u || !s->readable(s->user, base, size) || get32(s, base) != 1u) return -1;
    int32_t blocks = 0;
    uint32_t p = base + 8u;
    while (p < end) {
        uint32_t header = get32(s, p - 4u), n = header & ~1u;
        if (n < 16u || (n & 3u) || n > end - p || get32(s, p + n - 8u) != header) return -1;
        p += n;
        ++blocks;
    }
    return p == end && get32(s, end - 4u) == 1u ? blocks : -1;
}

int es2_snapshot_build(const es2_snapshot_source *s, const es2_snapshot_input *in, uint8_t **out, size_t *length) {
    buffer b = {0};
    uint32_t flags = in->flags;
    unsigned fixed = ES2_SNAPSHOT_FIXED_COUNT + in->extra_count + (in->arena ? 1u : 0u);
    put(&b, "ES2SNAP1", 8);
    put32(&b, ES2_SNAPSHOT_VERSION);
    size_t flags_at = b.used;
    put32(&b, 0);
    put32(&b, in->frame);
    put32(&b, in->image_base);
    put32(&b, in->image_size);
    put32(&b, fixed);
    for (unsigned i = 0; i < ES2_SNAPSHOT_FIXED_COUNT; ++i) put_region(&b, s, in, es2_snapshot_fixed_regions[i].va, es2_snapshot_fixed_regions[i].length);
    for (unsigned i = 0; i < in->extra_count; ++i) put_region(&b, s, in, in->extra[i].va, in->extra[i].length);
    if (in->arena) put_region(&b, s, in, ES2_SNAPSHOT_ARENA_BASE, ES2_SNAPSHOT_ARENA_SIZE);
    if (!in->heap_available) { flags |= ES2_SNAPSHOT_FLAG_UNAVAILABLE; put32(&b, 0); }
    else if (in->heap_count > ES2_SNAPSHOT_REGION_LIMIT) { flags |= ES2_SNAPSHOT_FLAG_WALK_ERROR; put32(&b, 0); }
    else {
        flags |= ES2_SNAPSHOT_FLAG_WALKED;
        put32(&b, in->heap_count);
        uint64_t dumped = 0;
        for (uint32_t r = 0; r < in->heap_count; ++r) {
            uint32_t base = in->heap_base[r], size = in->heap_size[r];
            int32_t blocks = heap_count(s, base, size);
            put32(&b, base); put32(&b, size); put32(&b, blocks >= 0 ? 1u : 0u); put32(&b, blocks >= 0 ? (uint32_t)blocks : 0u);
            if (blocks < 0) { flags |= ES2_SNAPSHOT_FLAG_WALK_ERROR; continue; }
            for (uint32_t p = base + 8u; p < base + size;) {
                uint32_t header = get32(s, p - 4u), n = header & ~1u, block_flags = header & 1u;
                if ((header & 1u) && dumped + (n - 8u) <= in->max_bytes) { block_flags |= 2u; dumped += n - 8u; }
                else if (header & 1u) flags |= ES2_SNAPSHOT_FLAG_TRUNCATED;
                put32(&b, p); put32(&b, n); put32(&b, block_flags);
                if (block_flags & 2u) put_guest(&b, s, p, n - 8u);
                p += n;
            }
        }
    }
    put32(&b, in->map_count);
    for (unsigned i = 0; i < in->map_count; ++i) {
        const es2_snapshot_map_entry *m = &in->map[i];
        put32(&b, m->base); put32(&b, m->size); put32(&b, m->allocation_base); put32(&b, m->protect); put32(&b, m->type);
    }
    put32(&b, in->module_count);
    for (unsigned i = 0; i < in->module_count; ++i) {
        const char *name = in->modules[i].name ? in->modules[i].name : "";
        uint32_t n = (uint32_t)strlen(name);
        put32(&b, in->modules[i].base); put32(&b, in->modules[i].size); put32(&b, n);
        put(&b, name, n);
    }
    if (in->map_truncated) flags |= ES2_SNAPSHOT_FLAG_MAP_TRUNCATED;
    flags |= ES2_SNAPSHOT_FLAG_COMPLETE;
    patch32(&b, flags_at, flags);
    if (b.failed) { free(b.data); return 1; }
    *out = b.data;
    *length = b.used;
    return 0;
}

static int hex_digit(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}
/* strtoul-like readers (base 16, and base 0 for the length) without locale. */
static const char *number(const char *p, int hex_only, uint64_t *v) {
    int base = 10;
    if (hex_only) base = 16;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; }
    else if (!hex_only && p[0] == '0') base = 8;
    const char *start = p;
    uint64_t r = 0;
    for (;; ++p) {
        int d = hex_digit(*p);
        if (d < 0 || d >= base) break;
        r = r * (uint64_t)base + (uint64_t)d;
        if (r > 0xffffffffu) return NULL;
    }
    if (p == start) return NULL;
    *v = r;
    return p;
}
int es2_snapshot_parse_extra(const char *text, es2_snapshot_region *out, unsigned capacity, unsigned *count) {
    *count = 0;
    const char *p = text;
    while (*p == ' ') ++p;
    if (!*p) return 0;
    for (;;) {
        if (*count == capacity) return 1;
        uint64_t va = 0, length = 0;
        if (!(p = number(p, 1, &va)) || *p != ':') return 1;
        ++p;
        if (!(p = number(p, 0, &length)) || !length || va < 0x00400000u || va - 0x00400000u >= ES2_SNAPSHOT_IMAGE_SIZE ||
            length > ES2_SNAPSHOT_IMAGE_SIZE - (va - 0x00400000u))
            return 1;
        out[(*count)++] = (es2_snapshot_region){(uint32_t)va, (uint32_t)length};
        while (*p == ' ') ++p;
        if (!*p) return 0;
        if (*p++ != ',') return 1;
        while (*p == ' ') ++p;
    }
}
