#include "es2/core/collision.h"
#include "es2/core/energy.h"
#include "es2/core/geometry.h"
#include "es2/core/matrix.h"
#include "fixed.h"
#include <string.h>

#define R16(a) m->read16(m->context, (a))
#define R32(a) m->read32(m->context, (a))
#define W16(a, v) m->write16(m->context, (a), (uint16_t)(v))
#define W32(a, v) m->write32(m->context, (a), (v))

static uint64_t pair(uint32_t edx, uint32_t eax) { return ((uint64_t)edx << 32) | eax; }
static uint32_t low(uint64_t r) { return (uint32_t)r; }
static uint32_t high(uint64_t r) { return (uint32_t)(r >> 32); }
/* EDX:EAX of a call with AX replaced by 1, as `mov ax, 1` leaves it. */
static uint64_t flag(uint64_t r) { return pair(high(r), (low(r) & 0xffff0000u) | 1u); }
static uint32_t guest(const void *p) { return (uint32_t)(uintptr_t)p; }
static void observe(const es2_collide_globals *g, es2_collide_event event, uint32_t key) {
    if (g->observe) g->observe(g->observer_context, event, key);
}
static uint64_t direct(const es2_collide_globals *g, es2_guest_call call, void *c,
                       uint32_t function, const uint32_t *args, unsigned count) {
    observe(g, ES2_COLLIDE_CALL, function);
    return call(c, function, args, count);
}

static uint64_t method(const es2_guest_memory *m, const es2_collide_globals *g,
                       es2_guest_call call, void *c, uint32_t object,
                       uint32_t slot, const uint32_t *args, unsigned count) {
    uint32_t list[5] = {object};
    for (unsigned i = 0; i < count; ++i) list[i + 1] = args[i];
    uint32_t function = R32(R32(object) + slot);
    observe(g, ES2_COLLIDE_SLOT, slot);
    return call(c, function, list, count + 1);
}

/* Euler matrix at +0x12 from the angles at +0x0c, rebuilt when +0x32 is 0,
 * then its 20 rotation bytes. */
static void rotation(const es2_guest_memory *m, uint32_t object, const void *quarter, uint8_t out[20]) {
    uint8_t *o = m->host(m->context, object);
    if (!R16(object + 0x32u)) {
        es2_matrix_euler(o + 0x0c, o + 0x12, quarter);
        W16(object + 0x32u, 1);
    }
    memcpy(out, o + 0x12, 20);
}

/* Part record strength: vehicle part object [O+0x34], its list [+4] maps the
 * config word +0x0a through virtual +0x20 to an entry with index +4 into the
 * 32-byte records at [+0x16]; dword +0x1c of the record (default record when
 * the lookup fails or the index is negative). `wide` is the register the word
 * was loaded into; its upper half is pushed along. */
static uint32_t part_strength(const es2_guest_memory *m, es2_guest_call call, void *c,
                              const es2_collide_globals *g, uint32_t object, uint32_t config,
                              uint32_t wide) {
    uint32_t parts = R32(object + 0x34u), list = R32(parts + 4u);
    uint32_t arg = (wide & 0xffff0000u) | R16(config + 0x0au);
    uint32_t entry = low(method(m, g, call, c, list, 0x20u, &arg, 1));
    uint32_t record = g->default_part;
    if (entry && fx_s16(R16(entry + 4u)) > -1)
        record = R32(parts + 0x16u) + ((uint32_t)fx_s16(R16(entry + 4u)) << 5);
    return R32(record + 0x1cu);
}

/* Contact between A and scene object B: impact from both speeds, damage to
 * both at the midpoint. */
static uint64_t contact(const es2_guest_memory *m, es2_guest_call call, void *c,
                        const es2_collide_globals *g, uint32_t a, uint32_t b, const uint32_t start[3]) {
    observe(g, ES2_COLLIDE_CONTACT, 0);
    uint64_t r = method(m, g, call, c, b, 0x68u, NULL, 0);
    if (R16(b + 0x1a8u)) return flag(r);
    uint32_t config_a = R32(a + 0x1f2u) + 2u, config_b = R32(b + 0x1f2u) + 2u;
    uint8_t own[20], other[20], speed[12], world[12], local[12];
    rotation(m, a, g->quarter, own);
    rotation(m, b, g->quarter, other);
    fx_w32(speed, 0);
    fx_w32(speed + 4, (uint32_t)fx_s16(R16(b + 0x28eu)));
    fx_w32(speed + 8, 0);
    es2_matrix_transpose(own);
    es2_matrix_rotate(speed, other, world);
    es2_matrix_rotate(world, own, local);
    uint32_t closing = es2_multiply_q10((uint32_t)fx_s16(R16(config_a + 0x4cu)), (uint32_t)fx_s16(R16(a + 0x28eu))) -
                       es2_multiply_q10((uint32_t)fx_s16(R16(config_b + 0x4cu)), fx_r32(local + 4));
    uint64_t impact = es2_multiply_q10_registers(0x12cu, closing);
    if (fx_s32(low(impact)) <= 0xc8) return flag(impact);
    uint32_t strength_a = part_strength(m, call, c, g, a, config_a, config_a);
    uint32_t strength_b = part_strength(m, call, c, g, b, config_b, config_b);
    uint32_t point[3] = {R32(b + 0x26u), R32(b + 0x2au), R32(b + 0x2eu)};
    point[2] += start[2];
    point[0] += start[0];
    point[1] += start[1];
    point[2] = fx_sar(point[2], 1);
    point[0] = fx_sar(point[0], 1);
    point[1] = fx_sar(point[1], 1);
    point[2] = fx_s32(strength_a) < fx_s32(strength_b) ? strength_a : strength_b;
    uint32_t hit_a[4] = {low(impact), guest(point), 0x4b0u, b};
    (void)method(m, g, call, c, a, 0x70u, hit_a, 4);
    uint32_t hit_b[4] = {low(impact), guest(point), 0x4b0u, a};
    return flag(method(m, g, call, c, b, 0x70u, hit_b, 4));
}

uint64_t es2_ground_collide_registers(const es2_guest_memory *m, uint32_t a,
                                      const es2_collide_globals *g, es2_guest_call call, void *c) {
    uint32_t next = R32(g->objects);
    uint16_t radius = (uint16_t)low(method(m, g, call, c, a, 0x5cu, NULL, 0));
    uint32_t start[3] = {R32(a + 0x26u), R32(a + 0x2au), R32(a + 0x2eu)};
    W32(a + 0x2b0u, 0);
    /* EAX at the loop exit: its upper half is pushed with the radius word. */
    uint32_t eax = start[2];
    for (uint16_t index = 0;;) {
        uint16_t current = index++;
        if (fx_s16(current) >= fx_s16(R16(g->object_count))) break;
        uint32_t b = R32(next);
        next += 4u;
        if (!b) break;
        if (b == a) continue;
        eax = R32(R32(b + 0x45u) + 0x14u) != 0;
        if (eax) continue;
        uint16_t reach = (uint16_t)low(method(m, g, call, c, b, 0x7cu, NULL, 0));
        uint8_t *ha = m->host(m->context, a + 0x26u), *hb = m->host(m->context, b + 0x26u);
        uint32_t distance = es2_distance3_points(ha, hb);
        eax = distance;
        if (R16(b + 0x1a8u) == 1) {
            eax = (uint32_t)fx_s16((uint16_t)low(method(m, g, call, c, b, 0x5cu, NULL, 0)));
            if (fx_s32(eax) > fx_s32(distance)) W32(a + 0x2b0u, b);
        }
        if (!reach) continue;
        if (fx_s16(radius) + fx_s16(reach) > fx_s32(distance)) return contact(m, call, c, g, a, b, start);
    }
    /* The start copy is the probe point; both callees may change it. */
    uint32_t *position = start;
    uint32_t probe[3] = {guest(position), (eax & 0xffff0000u) | radius, 0};
    uint64_t r = direct(g, call, c, g->buildings, probe, 3);
    if ((uint16_t)low(r)) return flag(r);
    uint32_t face_args[2] = {R32(g->terrain), guest(position)};
    uint32_t face = low(direct(g, call, c, g->terrain_face, face_args, 2));
    uint32_t steep = 1;
    if (face) {
        int32_t z = fx_s16(R16(face + 4u));
        steep = (z < 0 ? -z : z) < 0x5aa;
    }
    uint32_t edx = a + 0x92u;
    if (!*(const uint8_t *)m->host(m->context, a + 0xa3u)) return pair(edx, steep);
    if (steep) {
        if (!R32(g->drift_enabled) && fx_s32(position[2]) > fx_s32(R32(g->last_position + 8u))) return pair(edx, 1);
        observe(g, ES2_COLLIDE_SLOPE_DRIFT, 0);
        W32(g->drift_enabled, 1);
        uint64_t x = es2_multiply_q10_registers(10, (uint32_t)fx_s16(R16(face)));
        W32(g->drift, R32(g->drift) + low(x));
        uint64_t y = es2_multiply_q10_registers(10, (uint32_t)fx_s16(R16(face + 2u)));
        W32(g->drift + 4u, R32(g->drift + 4u) + low(y));
        return pair(high(y), 0);
    }
    if (!R32(g->drift_enabled)) return pair(edx, 0);
    W32(g->drift_enabled, 0);
    uint32_t dy = R32(g->drift + 4u), dx = R32(g->drift);
    uint32_t slide = es2_distance2(dx, dy);
    edx = (uint32_t)(fx_s32(dy) >> 31);
    if (fx_s32(slide) > 0xfa) {
        observe(g, ES2_COLLIDE_SLIDE_DAMAGE, 0);
        uint32_t level = (uint32_t)fx_s16(R16(g->damage_level));
        uint32_t scale = (uint32_t)fx_s16(R16(g->damage_table + 2u * level));
        uint32_t damage = es2_multiply_q10(scale, slide), range = 3u * damage;
        for (uint32_t part = 7; part <= 12; ++part) {
            uint32_t bound[2] = {g->generator, range};
            uint32_t v = low(direct(g, call, c, g->rng_bounded, bound, 2));
            uint32_t hit[3] = {part, (v & 0xffff0000u) | (uint16_t)(v + damage), 0};
            (void)method(m, g, call, c, a, 0x74u, hit, 3);
        }
        uint32_t zero = 0, id = 0x29;
        (void)direct(g, call, c, g->effect, &zero, 1);
        edx = high(direct(g, call, c, g->sound, &id, 1));
    }
    W32(g->drift + 4u, 0);
    W32(g->drift, 0);
    return pair(edx, 0);
}

/* Sweep receiver object query 427da8, grid sweep 4273c8 and set_length 4927f0
 * (docs/reverse-engineering/weapons.md). Frame buffers that callees receive
 * live in scratch at their original EBP offsets. */
static uint64_t oq_pair(uint32_t eax, uint32_t edx) { return ((uint64_t)edx << 32) | eax; }
static uint32_t oq_word(uint32_t high, uint32_t word) { return (high & 0xffff0000u) | (uint16_t)word; }
static void oq_call(const es2_weapon_graph *g, uint32_t va, const uint32_t *a, unsigned n,
                    es2_weapon_registers *r, uint32_t eax, uint32_t edx, uint32_t ecx) {
    uint32_t target = g->image + va - 0x400000u;
    r->eax = eax; r->edx = edx; r->ecx = ecx;
    if (g->observe) g->observe(g->observer_context, target, 0);
    g->call(g->context, target, a, n, r);
}

/* Original: DBSIM.EXE 0x4927f0. 492a7d (octagonal length through 47de72),
 * 492b04 (IDIV) and 47e08d (Q16 product) are computed here. */
uint64_t es2_set_length_registers(const es2_guest_memory *m, const es2_weapon_graph *g, uint32_t vector,
                                  uint32_t length) {
    uint32_t x = R32(vector), y = R32(vector + 4u), z = R32(vector + 8u);
    uint32_t size = es2_distance3(x, y, z);
    if (!size) {
        /* memset(V, 0, 12): EDX = CDQ of V.z from 47de72, ECX = the popped slot V. */
        es2_weapon_registers r;
        uint32_t a[3] = {vector, 0, 12};
        oq_call(g, 0x49681au, a, 3, &r, 0, (z & 0x80000000u) ? 0xffffffffu : 0, vector);
        return oq_pair(vector, r.edx);
    }
    int64_t dividend = (int64_t)fx_s16(length) * 65536;
    int64_t factor = fx_s16((uint32_t)(dividend / fx_s32(size)));
    uint32_t edx = 0;
    for (uint32_t i = 0; i < 12u; i += 4u) {
        int64_t product = fx_s32(R32(vector + i)) * factor;
        W32(vector + i, (uint32_t)((uint64_t)product >> 16));
        edx = (uint32_t)((uint64_t)product >> 32);
    }
    return oq_pair(vector, edx);
}

/* Original: DBSIM.EXE 0x4273c8. Frame f = scratch+50h: A (current sample)
 * at f-30h, E (end) at f-24h, D (scaled step) at f-3Ch. */
uint64_t es2_grid_sweep_registers(const es2_guest_memory *m, const es2_weapon_graph *g, uint32_t shape,
                                  uint32_t out, uint32_t segment, uint32_t radius, const uint32_t entry[3]) {
    const uint32_t f = g->scratch + 0x50u, sample = f - 0x30u, end = f - 0x24u, step_vector = f - 0x3cu;
    es2_weapon_registers r;
    uint32_t a[3];
    if (!R16(shape + 0x2au)) return oq_pair(0, entry[1]);
    uint32_t top = R32(shape + 0x430u);
    if (fx_s32(R32(segment + 8u)) >= fx_s32(top) && fx_s32(top) <= fx_s32(R32(segment + 0x14u))) return oq_pair(0, top);
    for (uint32_t i = 0; i < 24u; i += 4u) W32(sample + i, R32(segment + i));
    unsigned k = R16(shape + 0x32u) & 31u;
    uint32_t ox = (uint32_t)fx_s16((uint32_t)R16(shape + 0x2eu) << k);
    uint32_t oy = (uint32_t)fx_s16((uint32_t)R16(shape + 0x30u) << k);
    W32(sample, R32(sample) + ox);
    W32(sample + 4u, R32(sample + 4u) + oy);
    W32(end, R32(end) + ox);
    W32(end + 4u, R32(end + 4u) + oy);
    uint16_t step = (uint16_t)((1u << k) + (uint16_t)radius);
    W32(step_vector, R32(end));
    W32(step_vector + 4u, R32(end + 4u));
    W32(step_vector + 8u, R32(end + 8u));
    W32(step_vector + 8u, R32(step_vector + 8u) - R32(sample + 8u));
    W32(step_vector, R32(step_vector) - R32(sample));
    W32(step_vector + 4u, R32(step_vector + 4u) - R32(sample + 4u));
    /* The step slot is EBX: s16 oy with BX replaced by the step word. */
    a[0] = step_vector; a[1] = oq_word(oy, step);
    oq_call(g, 0x4927f0u, a, 2, &r, step_vector, R32(sample), R32(sample + 4u));
    uint32_t dz = R32(sample + 8u) - R32(end + 8u), dy = R32(sample + 4u) - R32(end + 4u), dx = R32(sample) - R32(end);
    a[0] = dx; a[1] = dy; a[2] = dz;
    oq_call(g, 0x47de72u, a, 3, &r, dz, dy, dx);
    int64_t divisor = fx_s16(step), size = fx_s32(r.eax);
    uint32_t eax = (uint32_t)(size / divisor) + 1u, edx = (uint32_t)(size % divisor), ecx = (uint32_t)divisor;
    uint16_t count = (uint16_t)eax;
    for (uint16_t i = 0; fx_s16(i) < fx_s16(count); ++i) {
        a[0] = shape; a[1] = sample; a[2] = oq_word(eax, radius);
        oq_call(g, 0x427238u, a, 3, &r, a[2], edx, ecx);
        uint32_t height = r.eax;
        if (height && fx_s32(height) > fx_s32(R32(sample + 8u))) {
            uint32_t hit = out + 4u;
            W32(hit + 8u, R32(sample + 8u));
            W32(hit, R32(sample));
            W32(hit + 4u, R32(sample + 4u));
            W32(hit, R32(hit) - ox);
            W32(hit + 4u, R32(hit + 4u) - oy);
            return oq_pair(oq_word(hit, 1), oy);
        }
        ecx = R32(step_vector + 8u); W32(sample + 8u, R32(sample + 8u) + ecx);
        eax = R32(step_vector); W32(sample, R32(sample) + eax);
        edx = R32(step_vector + 4u); W32(sample + 4u, R32(sample + 4u) + edx);
    }
    return oq_pair(0, edx);
}

/* One list entry of 427da8. Returns 1 when the < 500 exit ends the loop;
 * The eax/edx outputs carry the registers the original holds at 428025 (or at that
 * exit). Frame f: see es2_object_query_registers. */
static int oq_object(const es2_guest_memory *m, const es2_weapon_graph *g, uint32_t f, uint32_t out,
                     uint32_t segment, uint32_t object, uint16_t *result, uint32_t *eax, uint32_t *edx) {
    const uint32_t position = f - 0x30u, local = f - 0x24u, inverse = f - 0x50u, back = f - 0x5cu, hit = f - 0x6cu,
        start = f - 0x84u, end = f - 0x78u, world = f - 0x90u, tip = f - 0x9cu, nearest = g->image + 0xaaad0u;
    es2_weapon_registers r;
    uint32_t a[4];
    /* A null holder makes the original read the word at address 8 (fault). */
    uint32_t holder = R32(object + 0x34u), shape = holder ? R32(holder + 4u) : 0;
    uint32_t length = R32(segment + 4u);
    uint32_t radius = (uint32_t)(fx_s16(R16(shape + 8u)) + fx_s16(R16(segment + 8u)));
    uint32_t reach = length + radius;
    uint32_t z = R32(object + 0x2eu);
    W32(position, R32(object + 0x26u));
    W32(position + 4u, R32(object + 0x2au));
    W32(position + 8u, z);
    a[0] = R32(segment) + 0x14u; a[1] = position;
    oq_call(g, 0x49288cu, a, 2, &r, z, position, a[0]);
    uint32_t distance = r.eax;
    if (fx_s32(distance) < fx_s32(R32(nearest))) W32(nearest, distance);
    *eax = distance; *edx = r.edx;
    if (fx_s32(distance) > fx_s32(reach)) return 0;
    W32(nearest, 0);
    a[0] = position; a[1] = segment + 0xau; a[2] = local;
    oq_call(g, 0x48043cu, a, 3, &r, segment + 0xau, local, position);
    *eax = R32(local); *edx = r.edx;
    if (fx_s32(radius) <= fx_s32(*eax)) return 0;
    *edx = 0u - radius;
    if (fx_s32(*eax) <= fx_s32(*edx)) return 0;
    *eax = R32(local + 4u);
    if (fx_s32(0u - radius) >= fx_s32(*eax) || fx_s32(*eax) >= fx_s32(reach)) return 0;
    /* Inline pose matrix, then the world-to-object pose at -50h. */
    if (!R16(object + 0x32u)) {
        a[0] = object + 0xcu; a[1] = object + 0x12u;
        oq_call(g, 0x47ebb8u, a, 2, &r, object + 0xcu, object + 0x12u, object + 0xcu);
        W16(object + 0x32u, 1);
    }
    for (uint32_t i = 0; i < 20u; i += 4u) W32(inverse + i, R32(object + 0x12u + i));
    uint32_t tx = R32(object + 0x26u), ty = R32(object + 0x2au), tz = R32(object + 0x2eu);
    W32(inverse + 0x14u, tx); W32(inverse + 0x18u, ty); W32(inverse + 0x1cu, tz);
    a[0] = inverse;
    oq_call(g, 0x47df19u, a, 1, &r, tz, ty, inverse);
    W32(inverse + 0x1cu, 0u - R32(inverse + 0x1cu));
    W32(inverse + 0x14u, 0u - R32(inverse + 0x14u));
    uint32_t y = 0u - R32(inverse + 0x18u);
    W32(inverse + 0x18u, y);
    W32(back, R32(inverse + 0x14u)); W32(back + 4u, y); W32(back + 8u, R32(inverse + 0x1cu));
    a[0] = back; a[1] = inverse; a[2] = inverse + 0x14u;
    oq_call(g, 0x480304u, a, 3, &r, inverse + 0x14u, inverse, back);
    length = R32(segment + 4u);
    W32(tip, 0); W32(tip + 4u, length); W32(tip + 8u, 0);
    a[0] = R32(segment) + 0x14u; a[1] = inverse; a[2] = start;
    oq_call(g, 0x48043cu, a, 3, &r, a[0], start, inverse);
    uint32_t pose = R32(segment);
    a[0] = tip; a[1] = pose; a[2] = world;
    oq_call(g, 0x48043cu, a, 3, &r, pose, world, tip);
    a[0] = world; a[1] = inverse; a[2] = end;
    oq_call(g, 0x48043cu, a, 3, &r, end, inverse, world);
    a[0] = shape; a[1] = hit; a[2] = start; a[3] = oq_word(r.eax, R16(segment + 8u));
    oq_call(g, 0x4273c8u, a, 4, &r, shape, start, hit);
    *eax = r.eax; *edx = r.edx;
    if (!(uint16_t)r.eax) return 0;
    a[0] = start; a[1] = hit + 4u;
    oq_call(g, 0x49288cu, a, 2, &r, r.eax, hit + 4u, start);
    W32(segment + 4u, r.eax);
    *result = 1;
    W32(out + 4u, object ? object + 0xcu : object);
    *edx = out;
    *eax = R32(segment + 4u);
    W32(out, *eax);
    return fx_s32(R32(segment + 4u)) < 500;
}

/* Original: DBSIM.EXE 0x427da8. Frame f = scratch+0A0h. */
uint64_t es2_object_query_registers(const es2_guest_memory *m, const es2_weapon_graph *g, uint32_t out,
                                    uint32_t segment, uint32_t list, uint32_t count, const uint32_t entry[3]) {
    const uint32_t f = g->scratch + 0xa0u;
    uint16_t result = 0;
    uint32_t eax = list, edx = list, cursor = list;
    (void)entry;
    for (uint16_t index = 0; fx_s16(index) < fx_s16(count);) {
        if (oq_object(m, g, f, out, segment, R32(cursor), &result, &eax, &edx)) break;
        ++index;
        cursor += 4u;
        edx = oq_word(edx, index);
    }
    return oq_pair(oq_word(eax, result), edx);
}
