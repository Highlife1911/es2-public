/* Sky, horizon, object binning and the sort tree as own code (es2/sim/scene.h).
 * Contract: horizon.md, "the sky and the horizon as own code" and terrain.md,
 * "binning, the cell callback and the sort tree as own code" (every store and
 * call below with its listing address range); "listing" = llvm-objdump -d -M
 * intel of the reference DBSIM.EXE. Internal callees of the original inside
 * 0x428000..0x429660 and 0x42e700..0x42f6ff are own functions called directly;
 * hooks, other packages' services and the graphics driver's slots
 * 0x4a57bc/0x4a57c0/0x4a57ec are called through the dispatch over the bound
 * main loop's call area. Guest state is read and written only through the guest
 * view.
 *
 * Nothing here writes pixels itself: the sky bands and the ground fill hand
 * their polygons and rectangles to the graphics library's fills (0x484222,
 * 0x483eb8, 0x486704), the objects draw through their own vtables. The
 * module's state is the cell table 0x4cf4f0/0x4cf874, the cell buckets
 * 0x4aaafc, the loose list 0x4cf88c/0x4cf92c, the draw records and list
 * 0x4cf884/0x4cf888, the sort nodes from 0x49ac40, the draw distances
 * 0x4cf988, the pick state 0x4cf930..0x4cf980 and the horizon object's
 * line and band fields. Where the original faults (an integer division by
 * zero, a write through a NULL horizon) the call is refused with the
 * faulting address. Where the original pushes a dword whose upper bits are
 * left over from earlier code (the byte 0x4cf984 for 0x42e8e8, the word
 * arguments of heading_cos, the word count of 0x429620) the own code passes
 * the value zero-extended; every callee reads only the low part. */
#include <stdint.h>
#include <string.h>
#include "es2/sim/scene.h"
#include "es2/sim/memory.h"
#include "es2/sim/services.h"

/* ---- guest VAs */
#define VA_COUNTER_POINTER 0x006cbee4u
#define VA_GRID 0x004a0be0u           /* the terrain grid object (cells by position) */
#define VA_CELL_TABLE 0x004cf4f0u     /* 6-byte entries: column, row, bucket index (words) */
#define VA_CELL_COUNT 0x004cf874u     /* entries in use (0x96 before the assertion) */
#define VA_BUCKETS 0x004aaafcu        /* 1000 bytes per cell; slot k (1-based) at +4k */
#define VA_RECORDS 0x004cf884u        /* the draw records (0x36 bytes each) */
#define VA_DRAW_LIST 0x004cf888u      /* the draw list (record pointers) */
#define VA_LOOSE 0x004cf88cu          /* objects without a cell, drawn by 0x42883c */
#define VA_LOOSE_COUNT 0x004cf92cu
#define VA_PICK_ON 0x004cf930u        /* byte: a pick is pending this frame */
#define VA_PICK_STATE 0x004cf931u     /* the driver state record of the pick */
#define VA_PICKED 0x004cf93cu         /* the object under the pick */
#define VA_PICK_SLOTS 0x004cf940u     /* 4 driver state records of 8 bytes */
#define VA_PICK_SAVED 0x004cf960u     /* 4 saved states */
#define VA_PICK_DONE 0x004cf970u      /* 4 bytes */
#define VA_PICK_TARGET 0x004cf974u
#define VA_PICK_DEVICE 0x004cf978u
#define VA_PICK_PREV 0x004cf97cu
#define VA_PICK_RESULT 0x004cf980u
#define VA_LIGHTS_FLAG 0x004cf984u    /* word: the object is lit by the scene lights */
#define VA_DRAW_DIST 0x004cf988u      /* 5 dwords */
#define VA_DIST_FACTORS 0x0049abb0u   /* 5 words */
#define VA_PICK_PENDING 0x0049abbcu   /* byte */
#define VA_SORT_CAMERA 0x0049ac3cu
#define VA_SORT_ROOT 0x0049ac40u      /* the first sort node (0x1d bytes each) */
#define VA_PLAYER 0x004d24e6u
#define VA_CAMERA 0x004d24eau
#define VA_LIGHTS_BYTE 0x004d24c5u
#define VA_DETAIL 0x004a5b04u
#define VA_DETAIL_SAVED 0x004a5b08u
#define VA_LIGHT_TABLE 0x004a9608u
#define VA_DEVICE 0x006c5ed0u         /* the graphics library's current device */
#define VA_DEVICE_PALETTE 0x004a3686u
#define VA_DRV_GET 0x004a57bcu        /* driver slots */
#define VA_DRV_SET 0x004a57c0u
#define VA_DRV_PALETTE 0x004a57ecu
#define VA_GC 0x006c5f70u             /* the graphics context */
#define VA_HORIZON 0x0049aee0u
#define VA_SKY_OBJECT 0x0049aee4u
#define VA_JOLT 0x004cf9a0u
#define VA_TERRAIN 0x0049aac8u
#define VA_TERRAIN_MATRIX 0x004aaa84u
#define VA_JOLT_PALETTE 0x0049aef8u
#define VA_JOLT_FIRST 0x004cfd0cu
#define VA_JOLT_CURRENT 0x004cfd08u
#define VA_JOLT_ON 0x0049aef3u
#define VA_SHIFT_X 0x004d253au
#define VA_SHIFT_Y 0x004d253cu
#define VA_FOCAL_SHIFT 0x006c6028u
#define VA_CENTRE_X 0x006c6034u
#define VA_CENTRE_Y 0x006c6038u
/* pools walked by 0x42841c */
#define POOL_FLAT 0x004a968du
#define POOL_BUILDING 0x004a95a0u
#define POOL_MECH 0x004a9b7au
#define POOL_FLYER 0x004a9db9u
#define POOL_5 0x004a96c2u
#define POOL_6 0x004a961eu
#define POOL_7 0x004a9642u
#define POOL_8 0x004a966eu
#define POOL_9 0x006b4f30u
#define POOL_10 0x004a96aau
/* callees (dispatch) */
#define VA_ASSERT 0x00479c78u
#define VA_GRID_BEGIN 0x0046e944u
#define VA_GRID_AT 0x0046e62cu
#define VA_GRID_CELL 0x0046e8c4u
#define VA_GRID_MARK 0x0046e5a4u
#define VA_HORIZON_EDGE 0x00470a1cu
#define VA_TERRAIN_DRAW 0x0046d1a8u
#define VA_POOL6_HIDDEN 0x00408240u
#define VA_POOL6_CELL 0x00408228u
#define VA_POOL9_CELL 0x0046b850u
#define VA_LIGHT_UPDATE 0x00407098u
#define VA_SHAPE_LIGHT 0x0042e8e8u
#define VA_DEPTH 0x004680f0u
#define VA_SCENE_LIGHTS 0x00407878u
#define VA_OBJECT_SELECT 0x0048c444u
#define VA_VECTOR_NEW 0x0049667cu
#define VA_EMPTY_CTOR 0x0049545bu
#define VA_POLY_FILL 0x00484222u
#define VA_POLY_FILL_CLIP 0x00483eb8u
#define VA_RECT_FILL 0x00486704u
#define VA_PALETTE_SWAP 0x004303b0u
#define VA_SKY_AFTER 0x0042eb40u
#define VA_LINE_BASE_DTOR 0x00475900u
#define VA_DELETE 0x00477548u
#define VA_COMPARE_SHAPES 0x0042900cu
#define VA_COMPARE_KIND 0x0042937cu
/* hooks */
#define VA_HEADING_COS 0x0047dff0u
#define VA_MULTIPLY_Q10 0x0047e0b0u
#define VA_DIST_GATE 0x0049288cu
#define VA_POOL_PREV 0x00471c70u
#define VA_CAMERA_ATTACHED 0x00401078u

static es2_sim_scene *bound_scene, own_scene;
void es2_sim_scene_bind(es2_sim_scene *scene) { bound_scene = scene; }

/* ---- guest view and calls */
typedef struct ctx { es2_sim_scene *v; es2_sim_loop *l; const es2_guest_memory *m; } ctx;
static uint32_t r32(const ctx *c, uint32_t a) { return c->m->read32(c->m->context, a); }
static uint16_t r16(const ctx *c, uint32_t a) { return c->m->read16(c->m->context, a); }
static uint8_t r8(const ctx *c, uint32_t a) { return es2_sim_read8(c->m, a); }
static void w32(const ctx *c, uint32_t a, uint32_t v) { c->m->write32(c->m->context, a, v); }
static void w16(const ctx *c, uint32_t a, uint16_t v) { c->m->write16(c->m->context, a, v); }
static void w8(const ctx *c, uint32_t a, uint8_t v) { es2_sim_write8(c->m, a, v); }
static int32_t s32(const ctx *c, uint32_t a) { return (int32_t)r32(c, a); }
static int32_t sw(const ctx *c, uint32_t a) { return (int32_t)(int16_t)r16(c, a); }
static void add32(const ctx *c, uint32_t a, uint32_t v) { w32(c, a, r32(c, a) + v); }
static int refuse(ctx *c, uint32_t va, const char *name, const char *reason) {
    c->v->refused_va = va;
    return es2_sim_loop_refuse(c->l, va, name, reason);
}
/* The original reads, writes or calls through NULL at va (es2_sim_loop_null_refuse). */
static int null_refuse(ctx *c, uint32_t va, const char *name, const char *reason) {
    c->v->refused_va = va;
    return es2_sim_loop_null_refuse(c->l, va, name, reason);
}
/* D: the original's IDIV of EDX:EAX = dividend by the divisor faults at va
 * (es2_sim_loop_divide_refuse). */
static int divide_refuse(ctx *c, uint32_t va, uint64_t dividend, uint32_t divisor, const char *name, const char *reason) {
    c->v->refused_va = va;
    return es2_sim_loop_divide_refuse(c->l, va, dividend, divisor, 1, name, reason);
}
static int call(ctx *c, uint32_t va, const uint32_t *slots, unsigned count, uint32_t *eax) {
    es2_sim_loop *l = c->l;
    uint32_t bytes = 4u * (count ? count : 1u), top = l->stack_hi;
    if (top - l->stack_lo < bytes) return refuse(c, va, NULL, "the own code's call area is exhausted");
    l->stack_hi -= bytes;
    for (unsigned i = 0; i < count; ++i) w32(c, l->stack_hi + 4u * i, slots[i]);
    uint32_t zero[3] = {0, 0, 0};
    es2_sim_result r;
    memset(&r, 0, sizeof(r));
    int refused = es2_sim_call(l->dispatch, va, c->m->host(c->m->context, l->stack_hi), count, zero, &r);
    l->stack_hi = top;
    if (refused) {
        c->v->refused_va = va;
        if (l->outcome == ES2_SIM_LOOP_EXITED) return 1;   /* the process ended inside */
        l->outcome = ES2_SIM_LOOP_REFUSED;
        l->refused_va = va;
        return 1;
    }
    if (eax) *eax = r.eax;
    return 0;
}
#define ARGS(...) (const uint32_t[]){__VA_ARGS__}, (unsigned)(sizeof((const uint32_t[]){__VA_ARGS__}) / sizeof(uint32_t))
#define CALL(c, va, eax, ...) call((c), (va), ARGS(__VA_ARGS__), (eax))
/* A call through the dword at `slot` (a vtable slot or a driver slot). */
#define ICALL(c, slot, eax, ...) call((c), r32((c), (slot)), ARGS(__VA_ARGS__), (eax))
/* A virtual call through slot `offset` of the object's vtable, the object first. */
#define VCALL(c, obj, offset, eax, ...) ICALL((c), r32((c), (obj)) + (offset), (eax), __VA_ARGS__)
/* A zero-filled block of the call area for the original's stack locals. */
static uint32_t frame_push(ctx *c, uint32_t bytes) {
    bytes = (bytes + 3u) & ~3u;
    if (c->l->stack_hi - c->l->stack_lo < bytes + 0x400u) return 0;
    c->l->stack_hi -= bytes;
    for (uint32_t i = 0; i < bytes; i += 4u) w32(c, c->l->stack_hi + i, 0);
    return c->l->stack_hi;
}
static void frame_pop(ctx *c, uint32_t top) { c->l->stack_hi = top; }
/* The low dword of a product (IMUL r32, r/m32 and the low half of the one-operand IMUL). */
static int32_t mul32(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }
/* SAR r32, cl: the count masked to five bits. */
static int32_t sar(int32_t v, int32_t n) { return v >> (n & 31); }
/* NEG and ADD r32 (wrapping). */
static int32_t neg(int32_t v) { return (int32_t)(0u - (uint32_t)v); }
static int32_t add(int32_t a, int32_t b) { return (int32_t)((uint32_t)a + (uint32_t)b); }

static int serving(es2_sim_service_call *sc, ctx *c) {
    c->v = bound_scene ? bound_scene : &own_scene;
    if (!c->v->loop) c->v->loop = es2_sim_loop_bound();
    if (!c->v->loop) { sc->refused = 1; sc->reason = "no main loop bound (RD-06)"; return 1; }
    c->l = c->v->loop;
    c->m = c->l->memory;
    ++c->v->calls;
    return 0;
}
static void failed(es2_sim_service_call *sc) { sc->refused = 1; sc->reason = "a call inside the service was refused"; }
#define SERVE(sc, c) ctx c; if (serving((sc), &c)) return
#define A(n) (sc->slots[n])

/* ======================================================================
 * The cell table and the binning (listing 0x4281b0..0x428418). */

/* 0x4281b0(col, row): the bucket index of the cell, appended when new. The
 * assertion 0x479c78 fires from the 0x96th entry on; the original goes on
 * writing past the table. */
static int cell_index(ctx *c, uint32_t col, uint32_t row, uint32_t *eax) {
    int32_t n = s32(c, VA_CELL_COUNT), i = 0;
    uint32_t e = VA_CELL_TABLE;
    for (; i < n; ++i, e += 6u)
        if ((int32_t)col == sw(c, e) && sw(c, e + 2u) == (int32_t)row) {
            *eax = (uint32_t)sw(c, e + 4u);
            return 0;
        }
    if (i >= 0x96 && CALL(c, VA_ASSERT, NULL, 0x006c56c8u, 5, 0, 0x0049abbdu, 0x45, 0x0049abc9u)) return 1;
    w16(c, e, (uint16_t)col);
    w16(c, e + 2u, (uint16_t)row);
    w16(c, e + 4u, (uint16_t)i);
    add32(c, VA_CELL_COUNT, 1u);
    *eax = (uint32_t)i;
    return 0;
}
/* 0x4282f8(obj, rec): the object into its cell's bucket (rec: column,
 * row, slot bytes) or, without a cell record, onto the loose list. EAX,
 * EDX, ECX as the original leaves them (unchanged EAX/ECX without a record). */
static int bin_store(ctx *c, uint32_t obj, uint32_t rec, uint32_t regs[3]) {
    if (!rec) {
        uint32_t n = r32(c, VA_LOOSE_COUNT);
        w32(c, VA_LOOSE + 4u * n, obj);
        w32(c, VA_LOOSE_COUNT, n + 1u);
        regs[1] = n;
        return 0;
    }
    if (r8(c, rec + 2u) >= 0xfau && CALL(c, VA_ASSERT, NULL, 0x006c56c8u, 5, 0, 0x0049abe1u, 0x8f, 0x0049abedu)) return 1;
    uint32_t idx = 0;
    if (cell_index(c, r8(c, rec), r8(c, rec + 1u), &idx)) return 1;
    uint32_t slot = r8(c, rec + 2u), bucket = VA_BUCKETS + 1000u * idx;
    w32(c, bucket + 4u * slot, obj);
    regs[0] = slot;
    regs[1] = 125u * idx;
    regs[2] = bucket;
    return 0;
}
/* 0x42837c(obj, size): the cell at the object's position (slot +4), binned. EAX the record. */
static int bin_at_position(ctx *c, uint32_t obj, uint32_t size, uint32_t *eax) {
    uint32_t grid = r32(c, VA_GRID), pos = 0, rec = 0, regs[3] = {0, 0, 0};
    if (VCALL(c, obj, 4u, &pos, obj, size) || CALL(c, VA_GRID_AT, &rec, grid, pos, size) || bin_store(c, obj, rec, regs)) return 1;
    *eax = rec;
    return 0;
}
/* 0x4283b4(obj, cw): the cell of the column/row words at cw, binned. EAX the record. */
static int bin_at_cell(ctx *c, uint32_t obj, uint32_t cw, uint32_t *eax) {
    uint32_t rec = 0, regs[3] = {0, 0, 0};
    if (CALL(c, VA_GRID_CELL, &rec, r32(c, VA_GRID), cw) || bin_store(c, obj, rec, regs)) return 1;
    *eax = rec;
    return 0;
}
/* 0x4282d8(obj): binned by position with the object's size (slot +0x10). EAX the record. */
static int bin_object(ctx *c, uint32_t obj, uint32_t *eax) {
    uint32_t size = 0;
    if (VCALL(c, obj, 0x10u, &size, obj)) return 1;
    return bin_at_position(c, obj, (uint32_t)(int32_t)(int16_t)size, eax);
}
/* The object's cell words +0x1e8/+0x1ea: the record's column and row, or -1/-1. */
static void bin_mark(const ctx *c, uint32_t obj, uint32_t rec) {
    w16(c, obj + 0x1e8u, rec ? r8(c, rec) : 0xffffu);
    w16(c, obj + 0x1eau, rec ? r8(c, rec + 1u) : 0xffffu);
}
/* The skip test of the vehicle pools: [[obj+0x45]+0x14] != 0. */
static int vehicle_hidden(const ctx *c, uint32_t obj) { return r32(c, r32(c, obj + 0x45u) + 0x14u) != 0; }
static int pool_next(ctx *c, uint32_t pool, uint32_t *cur) { return CALL(c, VA_POOL_PREV, cur, r32(c, pool), *cur); }

/* 0x42841c(): every drawable object into its cell's bucket or the loose list. */
static int bin_objects(ctx *c) {
    uint32_t obj = 0, rec = 0, size = 0, d = 0;
    w32(c, VA_CELL_COUNT, 0);
    if (CALL(c, VA_GRID_BEGIN, NULL, r32(c, VA_GRID))) return 1;
    /* The flats within 30000 of the camera (binned before the loose count is reset). */
    for (obj = 0;;) {
        if (pool_next(c, POOL_FLAT, &obj)) return 1;
        if (!obj) break;
        if (CALL(c, VA_DIST_GATE, &d, obj + 0x26u, r32(c, VA_CAMERA) + 4u)) return 1;
        if ((int32_t)d < 0x7530 && bin_object(c, obj, &rec)) return 1;
    }
    w32(c, VA_LOOSE_COUNT, 0);
    static const uint32_t vehicles[3] = {POOL_BUILDING, POOL_MECH, POOL_FLYER};
    for (unsigned p = 0; p < 3u; ++p)
        for (obj = 0;;) {
            if (pool_next(c, vehicles[p], &obj)) return 1;
            if (!obj) break;
            if (vehicle_hidden(c, obj)) continue;
            uint32_t link = vehicles[p] == POOL_MECH ? r32(c, obj + 0x2b0u) : 0;
            if (link) {
                if (bin_at_cell(c, obj, link + 0x1e8u, &rec)) return 1;
            } else if (VCALL(c, obj, 0x5cu, &size, obj) || bin_at_position(c, obj, (uint32_t)(int32_t)(int16_t)size, &rec)) {
                return 1;
            }
            bin_mark(c, obj, rec);
        }
    for (obj = 0;;) {
        if (pool_next(c, POOL_5, &obj)) return 1;
        if (!obj) break;
        if (bin_object(c, obj, &rec)) return 1;
    }
    for (obj = 0;;) {
        if (pool_next(c, POOL_6, &obj)) return 1;
        if (!obj) break;
        uint32_t hidden = 0, cw = 0;
        if (CALL(c, VA_POOL6_HIDDEN, &hidden, obj)) return 1;
        if (hidden) continue;
        if (CALL(c, VA_POOL6_CELL, &cw, obj)) return 1;
        if (cw ? bin_at_cell(c, obj, cw, &rec) : bin_object(c, obj, &rec)) return 1;
    }
    static const uint32_t plain[2] = {POOL_7, POOL_8};
    for (unsigned p = 0; p < 2u; ++p)
        for (obj = 0;;) {
            if (pool_next(c, plain[p], &obj)) return 1;
            if (!obj) break;
            if (bin_object(c, obj, &rec)) return 1;
        }
    for (obj = 0;;) {
        if (pool_next(c, POOL_9, &obj)) return 1;
        if (!obj) break;
        uint32_t cw = 0;
        if (CALL(c, VA_POOL9_CELL, &cw, obj) || bin_at_cell(c, obj, cw, &rec)) return 1;
    }
    for (obj = 0;;) {
        if (pool_next(c, POOL_10, &obj)) return 1;
        if (!obj) break;
        if (bin_object(c, obj, &rec)) return 1;
    }
    return 0;
}

/* ======================================================================
 * The pick state (listing 0x428ae0..0x428bbe and the pick parts of
 * 0x42876c/0x42883c). */

/* The device block of 0x42883c and 0x428b38: the current device's palette
 * +0x8c into its +0x38 and, while it is still current, to the driver. */
static int device_palette(ctx *c) {
    uint32_t e = r32(c, VA_DEVICE);
    w32(c, VA_PICK_DEVICE, e);
    uint32_t b = r32(c, e + 0x8cu);
    w32(c, e + 0x38u, b);
    if (e == r32(c, VA_DEVICE)) {
        w32(c, VA_DEVICE_PALETTE, b);
        if (ICALL(c, VA_DRV_PALETTE, NULL, b)) return 1;
    }
    return 0;
}
/* 0x428ae0(): every pick slot not yet done saved and switched to state 2. */
static int pick_restore(ctx *c) {
    w32(c, VA_PICK_DEVICE, r32(c, VA_DEVICE));
    for (uint32_t k = 0; k < 4u; ++k) {
        if (r8(c, VA_PICK_DONE + k)) continue;
        uint32_t s = VA_PICK_SLOTS + 8u * k, v = 0;
        if (ICALL(c, VA_DRV_GET, &v, s)) return 1;
        w32(c, VA_PICK_SAVED + 4u * k, v);
        if (ICALL(c, VA_DRV_SET, NULL, s, 2)) return 1;
    }
    return 0;
}
/* 0x428b38(): the device block, then every pick slot not yet done: a slot
 * whose state is no longer 2 is done, the others get their saved state back. */
static int pick_save(ctx *c) {
    if (device_palette(c)) return 1;
    for (uint32_t k = 0; k < 4u; ++k) {
        if (r8(c, VA_PICK_DONE + k)) continue;
        uint32_t s = VA_PICK_SLOTS + 8u * k, v = 0;
        if (ICALL(c, VA_DRV_GET, &v, s)) return 1;
        if (v != 2u) w8(c, VA_PICK_DONE + k, 1);
        else if (ICALL(c, VA_DRV_SET, NULL, s, r32(c, VA_PICK_SAVED + 4u * k))) return 1;
    }
    w8(c, VA_PICK_PENDING, 0);
    return 0;
}

/* ======================================================================
 * The draw distances and the cell callback (listing 0x428bc0..0x428de7). */

/* 0x428bc0(limit): the five draw distances limit * factor (Q10). EAX the last. */
static int draw_distances(ctx *c, uint32_t limit, uint32_t *eax) {
    for (uint32_t k = 0; k < 5u; ++k) {
        if (CALL(c, VA_MULTIPLY_Q10, eax, limit, (uint32_t)sw(c, VA_DIST_FACTORS + 2u * k))) return 1;
        w32(c, VA_DRAW_DIST + 4u * k, *eax);
    }
    return 0;
}
/* 0x428c08(obj, d): 1 when the object is beyond its kind's draw distance. */
static int distance_cull(ctx *c, uint32_t obj, uint32_t d, uint32_t *eax) {
    int32_t kind = sw(c, obj + 4u);
    uint32_t k = 0;
    if (kind == 0) k = 1;
    else if (kind == 5) {
        uint32_t size = 0;
        if (VCALL(c, obj, 0x10u, &size, obj)) return 1;
        k = (int32_t)(int16_t)size < 0x1b58 ? 3u : 4u;
    } else if (kind == 8) k = 2;
    *eax = s32(c, VA_DRAW_DIST + 4u * k) < (int32_t)d;
    return 0;
}
static int sort_draw(ctx *c, uint32_t list, uint32_t n, uint32_t *eax);
/* 0x428c60(x, y, n): the first n objects of the cell's bucket as draw
 * records on the draw list, sorted and drawn; kind 9 draws at once with the
 * detail level 0. */
static int cell_objects_draw(ctx *c, uint32_t x, uint32_t y, uint32_t n) {
    uint32_t cam = r32(c, VA_CAMERA), idx = 0, v = 0;
    if (cell_index(c, x, y, &idx)) return 1;
    uint32_t rec = r32(c, VA_RECORDS), list = r32(c, VA_DRAW_LIST), slot = VA_BUCKETS + 4u + 1000u * idx;
    uint16_t m = 0;
    for (uint16_t i = 0; (int16_t)i < (int16_t)n; ++i, slot += 4u) {
        uint32_t obj = r32(c, slot);
        if (r16(c, obj + 4u) == 9u) {
            w32(c, VA_DETAIL, 0);
            if (VCALL(c, obj, 0, NULL, obj)) return 1;
            w32(c, VA_DETAIL, r32(c, VA_DETAIL_SAVED));
            continue;
        }
        if (CALL(c, VA_CAMERA_ATTACHED, &v, cam, obj)) return 1;
        if (v & 0xffffu) continue;
        w16(c, VA_LIGHTS_FLAG, r8(c, VA_LIGHTS_BYTE));
        w32(c, rec + 0x32u, obj);
        w16(c, rec + 0x16u, 0);
        uint32_t pos = 0;
        if (VCALL(c, obj, 4u, &pos, obj)) return 1;
        w32(c, rec + 0xcu, r32(c, pos + 8u));
        w32(c, rec + 4u, r32(c, pos));
        w32(c, rec + 8u, r32(c, pos + 4u));
        if (VCALL(c, obj, 0x10u, &v, obj)) return 1;
        w16(c, rec + 0x10u, (uint16_t)v);
        uint16_t kind = r16(c, obj + 4u);
        if (kind == 5u || kind == 7u) {
            uint32_t s = 0;
            if (VCALL(c, obj, 0x24u, &s, obj)) return 1;
            add32(c, rec + 0xcu, s ? r32(c, s + 0x1cu) : 0x1f4u);
        }
        uint32_t d = 0, beyond = 0;
        if (CALL(c, VA_DIST_GATE, &d, rec + 4u, cam + 4u)) return 1;
        w32(c, rec + 0x12u, d);
        if (distance_cull(c, r32(c, rec + 0x32u), d, &beyond)) return 1;
        if (beyond) continue;
        w32(c, list, rec);
        rec += 0x36u;
        list += 4u;
        ++m;
    }
    w32(c, VA_SORT_CAMERA, cam);
    return sort_draw(c, r32(c, VA_DRAW_LIST), m, &v);
}

/* ======================================================================
 * The sort tree (listing 0x429460..0x429654). A node: +0 left, +4 right,
 * +8 the draw record, byte +0xc; 0x1d bytes. A record's kind is the word
 * +0x16: kinds 1 and 2 are sorted in the first pass, the rest in the second. */

/* 0x429460(a, b): nonzero when b goes right of a. */
static int sort_compare(ctx *c, uint32_t a, uint32_t b, uint32_t *eax) {
    uint32_t ra = r32(c, a + 8u), rb = r32(c, b + 8u);
    uint16_t ka = r16(c, ra + 0x16u), kb = r16(c, rb + 0x16u);
    if ((ka == 1u || ka == 2u) && (kb == 1u || kb == 2u)) return CALL(c, VA_COMPARE_KIND, eax, a, b);
    if (ka == 0 && kb == 0) {
        *eax = s32(c, ra + 0x12u) > s32(c, rb + 0x12u);
        return 0;
    }
    return CALL(c, VA_COMPARE_SHAPES, eax, a, b, 1);
}
/* 0x4294d4(node, new): new into the tree below node (left on a zero word). */
static int sort_insert(ctx *c, uint32_t node, uint32_t add, uint32_t *eax) {
    if (sort_compare(c, node, add, eax)) return 1;
    uint32_t side = (*eax & 0xffffu) ? node + 4u : node;
    uint32_t next = r32(c, side);
    if (!next) {
        w32(c, side, add);
        return 0;
    }
    return sort_insert(c, next, add, eax);
}
/* 0x429524/0x42958c: one pass over the list; kinds 1/2 (first) or the rest
 * (second) as nodes from `node` on. Returns the next free node. */
static int sort_pass(ctx *c, uint32_t list, uint32_t n, uint32_t node, int first, uint32_t *next) {
    uint32_t root = r32(c, VA_SORT_ROOT), eax = 0;
    for (uint16_t i = 0; (int16_t)i < (int16_t)n; ++i, list += 4u) {
        uint32_t rec = r32(c, list);
        uint16_t kind = r16(c, rec + 0x16u);
        if ((kind == 1u || kind == 2u) != first) continue;
        w32(c, node + 8u, rec);
        w32(c, node + 4u, 0);
        w32(c, node, 0);
        if (first) w8(c, node + 0xcu, 0);
        if (node != root && sort_insert(c, root, node, &eax)) return 1;
        node += 0x1du;
    }
    *next = node;
    return 0;
}
/* 0x4295f0(node): in order: left, the record's draw (its slot 0), right. */
static int sort_walk(ctx *c, uint32_t node) {
    uint32_t left = r32(c, node), rec;
    if (left && sort_walk(c, left)) return 1;
    rec = r32(c, node + 8u);
    if (VCALL(c, rec, 0, NULL, rec)) return 1;
    uint32_t right = r32(c, node + 4u);
    return right ? sort_walk(c, right) : 0;
}
/* 0x429620(list, n): both passes, then the walk; nothing for a zero word count. */
static int sort_draw(ctx *c, uint32_t list, uint32_t n, uint32_t *eax) {
    if (!(n & 0xffffu)) return 0;
    uint32_t next = 0, unused = 0;
    if (sort_pass(c, list, n, r32(c, VA_SORT_ROOT), 1, &next) || sort_pass(c, list, n, next, 0, &unused) ||
        sort_walk(c, r32(c, VA_SORT_ROOT)))
        return 1;
    *eax = 0;
    return 0;
}

/* ======================================================================
 * The per-object draw and the scene finish (listing 0x42876c..0x42895f). */

/* 0x42876c(rec): a sorted draw record: the light table for its position and
 * size, the shape lighting and depth, the object's draw (slot 0), the scene
 * lights, the pick test. The record is the draw record's object +0x32. */
static int sort_obj_draw(ctx *c, uint32_t rec) {
    uint32_t top = c->l->stack_hi, l = frame_push(c, 0x10u);
    if (!l) return refuse(c, 0x0042876cu, "sort_obj_draw", "the own code's call area is exhausted");
    w32(c, l + 8u, r32(c, rec + 0xcu));
    w32(c, l, r32(c, rec + 4u));
    w32(c, l + 4u, r32(c, rec + 8u));
    uint32_t table = r32(c, VA_LIGHT_TABLE);
    w32(c, l + 0xcu, (uint32_t)sw(c, rec + 0x10u));
    int bad = CALL(c, VA_LIGHT_UPDATE, NULL, table, l);
    frame_pop(c, top);
    if (bad) return 1;
    if (CALL(c, VA_SHAPE_LIGHT, NULL, r32(c, rec + 0x32u), r32(c, rec + 0x12u), r8(c, VA_LIGHTS_FLAG)) ||
        CALL(c, VA_DEPTH, NULL, r32(c, rec + 0x12u)))
        return 1;
    uint32_t obj = r32(c, rec + 0x32u);
    if (VCALL(c, obj, 0, NULL, obj)) return 1;
    if (r16(c, VA_LIGHTS_FLAG) && call(c, VA_SCENE_LIGHTS, NULL, 0, NULL)) return 1;
    if (r8(c, VA_PICK_ON)) {
        uint32_t v = 0;
        if (ICALL(c, VA_DRV_GET, &v, VA_PICK_STATE)) return 1;
        w32(c, VA_PICK_RESULT, v);
        if (v != 1u) {
            w32(c, VA_PICKED, r32(c, rec + 0x32u));
            w32(c, VA_PICK_PREV, v);
            if (ICALL(c, VA_DRV_SET, NULL, VA_PICK_STATE, 1)) return 1;
        }
    }
    if (r8(c, VA_PICK_PENDING) && r32(c, rec + 0x32u) == r32(c, VA_PICK_TARGET)) return pick_restore(c);
    return 0;
}
/* 0x42883c(cam, flag): the loose objects' draws, the player's cell marked
 * when the camera is not attached to the player, the pick close-down. */
static int scene_finish(ctx *c, uint32_t cam) {
    for (uint32_t i = 0; (int32_t)i < s32(c, VA_LOOSE_COUNT); ++i) {
        uint32_t obj = r32(c, VA_LOOSE + 4u * i);
        if (VCALL(c, obj, 0, NULL, obj)) return 1;
    }
    uint32_t p = r32(c, VA_PLAYER), v = 0;
    if (r16(c, r32(c, p + 0x1f2u) + 0x50u)) {
        if (CALL(c, VA_CAMERA_ATTACHED, &v, cam, p)) return 1;
        if (!(v & 0xffffu)) {
            p = r32(c, VA_PLAYER);
            int32_t col = sw(c, p + 0x1e8u), row = sw(c, p + 0x1eau);
            if (col > -1 && row > -1 && CALL(c, VA_GRID_MARK, NULL, r32(c, VA_GRID), (uint32_t)col, (uint32_t)row)) return 1;
        }
    }
    if (r8(c, VA_PICK_ON)) {
        if (device_palette(c) || ICALL(c, VA_DRV_GET, &v, VA_PICK_STATE)) return 1;
        w32(c, VA_PICK_PREV, v);
        if (ICALL(c, VA_DRV_SET, NULL, VA_PICK_STATE, 1)) return 1;
        w32(c, VA_PICKED, 0);
    }
    w32(c, VA_SORT_CAMERA, cam);
    if (r8(c, VA_PICK_ON)) {
        if (ICALL(c, VA_DRV_SET, NULL, VA_PICK_STATE, r32(c, VA_PICK_PREV))) return 1;
        w8(c, VA_PICK_ON, 0);
    }
    if (r8(c, VA_PICK_PENDING)) return pick_save(c);
    return 0;
}

/* ======================================================================
 * The sky and the horizon (listing 0x42e700..0x42e75f, 0x428140..0x428190,
 * 0x42ea44..0x42eab0, 0x42ebe8..0x42f396, 0x42f6d2..0x42f6fe). The horizon
 * object H: +0x28 the sky colour, +0x2c the ground colour, +0x30 the
 * vertical offset, +0x34/+0x36 pitch and roll words, +0x38..+0x44 the
 * line's end points p1 (+0x38, +0x3c) and p2 (+0x40, +0x44), +0x48/+0x4c
 * the band direction, +0x54/+0x58 the sky band step and count, +0x5c/+0x60
 * the ground band step and count, +0x6c the first band's height, byte
 * +0x71 the flat-ground flag, +0x72/+0x76 the terrain edge point. */

static int heading_cos(ctx *c, uint32_t angle, int32_t *out) {
    uint32_t v = 0;
    if (CALL(c, VA_HEADING_COS, &v, angle & 0xffffu)) return 1;
    *out = (int16_t)v;
    return 0;
}
/* 0x42ec68(H, J): the horizon line for H's pitch and roll in screen space,
 * pushed by the jolt J (+0 sideways sign, +4 both ends). EAX the last store. */
static int horizon_line(ctx *c, uint32_t h, uint32_t j, uint32_t *eax) {
    uint16_t pitch = r16(c, h + 0x34u);
    /* The listing's flip test: pitch above 0x4000 (unsigned) and, as a
     * signed dword, below 0xffffc000; the zero-extended word never is. */
    int flip = pitch > 0x4000u && (int32_t)pitch < (int32_t)0xffffc000u;
    int32_t cp, sp, cr, sr;
    if (heading_cos(c, pitch + 0xc000u, &cp) || heading_cos(c, r16(c, h + 0x34u), &sp)) return 1;
    if (!sp) sp = 1;
    if (heading_cos(c, r16(c, h + 0x36u) + 0xc000u, &cr) || heading_cos(c, r16(c, h + 0x36u), &sr)) return 1;
    int32_t sx = sw(c, VA_SHIFT_X) + 8, sy = sw(c, VA_SHIFT_Y) + 8, a;
    a = sar(sr, 14 - sx);
    w32(c, h + 0x40u, (uint32_t)a);
    w32(c, h + 0x38u, 0u - (uint32_t)a);
    a = sar(cr, 14 - sy);
    w32(c, h + 0x44u, (uint32_t)a);
    w32(c, h + 0x3cu, 0u - (uint32_t)a);
    int32_t num = (int32_t)((uint32_t)cp << (r32(c, VA_FOCAL_SHIFT) & 31u));
    if (num == INT32_MIN && sp == -1)
        return divide_refuse(c, 0x0042ed4eu, (uint64_t)(int64_t)num, (uint32_t)sp, "horizon_line", "integer division fault (the original raises #DE)");
    int32_t q = num / sp;
    int32_t t1 = sar(mul32(q, s32(c, h + 0x3cu)), sx), t2 = sar(mul32(q, s32(c, h + 0x40u)), sy);
    add32(c, h + 0x38u, (uint32_t)t1);
    add32(c, h + 0x3cu, (uint32_t)t2);
    add32(c, h + 0x40u, (uint32_t)t1);
    add32(c, h + 0x44u, (uint32_t)t2);
    add32(c, h + 0x38u, r32(c, VA_CENTRE_X));
    add32(c, h + 0x40u, r32(c, VA_CENTRE_X));
    w32(c, h + 0x3cu, r32(c, VA_CENTRE_Y) - r32(c, h + 0x3cu) + r32(c, h + 0x30u));
    w32(c, h + 0x44u, r32(c, VA_CENTRE_Y) - r32(c, h + 0x44u) + r32(c, h + 0x30u));
    if (j) {
        uint32_t dx = r32(c, h + 0x38u) - r32(c, h + 0x40u), dy = r32(c, h + 0x3cu) - r32(c, h + 0x44u);
        int up = 0, down = 0; /* p1 += d, p2 -= d */
        if (r32(c, j + 4u)) up = down = 1;
        else if (r32(c, j)) {
            uint16_t roll = r16(c, h + 0x36u);
            int neg = s32(c, j) < 0;
            if (roll > 0x4000u && roll < 0xc000u) { if (neg) down = 1; else up = 1; }
            else { if (neg) up = 1; else down = 1; }
        }
        if (up) { add32(c, h + 0x38u, dx); add32(c, h + 0x3cu, dy); }
        if (down) { add32(c, h + 0x40u, 0u - dx); add32(c, h + 0x44u, 0u - dy); }
    }
    if (flip) {
        w32(c, h + 0x48u, 0u - (uint32_t)cr);
        w32(c, h + 0x4cu, (uint32_t)sr);
        *eax = 0u - (uint32_t)cr;
    } else {
        w32(c, h + 0x48u, (uint32_t)cr);
        w32(c, h + 0x4cu, 0u - (uint32_t)sr);
        *eax = 0u - (uint32_t)sr;
    }
    return 0;
}

/* Points of the 5 x 8 array of 0x42ee88/0x42f0b0. */
static void pt_set(const ctx *c, uint32_t p, uint32_t x, uint32_t y) { w32(c, p, x); w32(c, p + 4u, y); }
static void pt_copy(const ctx *c, uint32_t d, uint32_t s) { pt_set(c, d, r32(c, s), r32(c, s + 4u)); }
static void pt_add(const ctx *c, uint32_t p, uint32_t x, uint32_t y) { add32(c, p, x); add32(c, p + 4u, y); }

/* 0x42ee88(H): the sky bands above the line: quads along the band
 * direction (rolled) or rectangles (level), each one colour darker, then
 * the top band to 5000 or the screen top. The graphics context's colour
 * record +0x22c points at the local {0, colour} record meanwhile. EAX the
 * restored record pointer. */
static int sky_bands(ctx *c, uint32_t h, uint32_t *eax) {
    uint32_t gc = r32(c, VA_GC), top = c->l->stack_hi, f = frame_push(c, 0x60u);
    if (!f) return refuse(c, 0x0042ee88u, "sky_bands", "the own code's call area is exhausted");
    uint32_t rect = f, pts = f + 0x10u, rec = f + 0x48u;
    int bad = 1;
    if (CALL(c, VA_VECTOR_NEW, NULL, pts, 8, 5, 1, VA_EMPTY_CTOR)) goto out;
    int32_t e0 = sar(neg(mul32(s32(c, h + 0x48u), s32(c, h + 0x54u))), 14);
    int32_t f0 = sar(mul32(s32(c, h + 0x4cu), s32(c, h + 0x54u)), 14);
    w32(c, rec, 0);
    uint32_t saved = r32(c, gc + 0x22cu);
    w32(c, gc + 0x22cu, rec);
    w32(c, rec + 4u, r32(c, h + 0x28u));
    int32_t bands = s32(c, h + 0x58u) - 1;
    if (r16(c, h + 0x36u)) {
        pt_copy(c, pts, h + 0x38u);
        pt_copy(c, pts + 8u, h + 0x40u);
        for (int k = 0; k < 2; ++k) {
            pt_add(c, pts, 0u - (uint32_t)e0, 0u - (uint32_t)f0);
            pt_add(c, pts + 8u, 0u - (uint32_t)e0, 0u - (uint32_t)f0);
        }
        for (int32_t i = 0; i < bands; ++i) {
            int32_t e = sar(neg(mul32(mul32(s32(c, h + 0x48u), s32(c, h + 0x54u)), i)), 14);
            int32_t g = sar(mul32(add(sar(s32(c, h + 0x6cu), 1), mul32(i - 1, s32(c, h + 0x54u))), s32(c, h + 0x4cu)), 14);
            pt_copy(c, pts + 0x10u, h + 0x40u);
            pt_copy(c, pts + 0x18u, h + 0x38u);
            pt_add(c, pts + 0x10u, (uint32_t)e, (uint32_t)g);
            pt_add(c, pts + 0x18u, (uint32_t)e, (uint32_t)g);
            if (CALL(c, VA_POLY_FILL, NULL, 4, pts)) goto out;
            pt_copy(c, pts, pts + 0x18u);
            pt_copy(c, pts + 8u, pts + 0x10u);
            add32(c, rec + 4u, 0xffffffffu);
        }
        int32_t e = sar(neg(mul32(s32(c, h + 0x48u), 5000)), 14), g = sar(mul32(s32(c, h + 0x4cu), 5000), 14);
        pt_copy(c, pts + 0x10u, pts);
        pt_copy(c, pts + 0x18u, pts + 8u);
        pt_add(c, pts + 0x10u, (uint32_t)e, (uint32_t)g);
        pt_add(c, pts + 0x18u, (uint32_t)e, (uint32_t)g);
        if (CALL(c, VA_POLY_FILL, NULL, 4, pts)) goto out;
    } else {
        pt_copy(c, rect, h + 0x38u);
        pt_copy(c, rect + 8u, h + 0x40u);
        uint32_t step = (uint32_t)neg(sar(s32(c, h + 0x6cu), 1));
        for (int32_t i = 0; i < bands; ++i) {
            add32(c, rect, step);
            add32(c, rect + 4u, step);
            step = (uint32_t)f0;
            if (CALL(c, VA_RECT_FILL, NULL, rect)) goto out;
            w32(c, rect + 0xcu, r32(c, rect + 4u));
            add32(c, rec + 4u, 0xffffffffu);
        }
        w32(c, rect + 4u, r32(c, gc + 0x214u));
        if (s32(c, rect + 4u) <= s32(c, rect + 0xcu) && CALL(c, VA_RECT_FILL, NULL, rect)) goto out;
    }
    w32(c, gc + 0x22cu, saved);
    *eax = saved;
    bad = 0;
out:
    frame_pop(c, top);
    return bad;
}

/* 0x42f0b0(H, C, J): the ground below the line: bands (rolled: quads,
 * level: rectangles) or, on flat ground (+0x71), one band to the terrain
 * edge point; then the rest to the viewport's bottom corners. EAX the
 * restored colour record pointer. */
static int ground_fill(ctx *c, uint32_t h, uint32_t *eax) {
    uint32_t gc = r32(c, VA_GC), bottom = r32(c, gc + 0x21cu), top = c->l->stack_hi, f = frame_push(c, 0x78u);
    if (!f) return refuse(c, 0x0042f0b0u, "ground_fill", "the own code's call area is exhausted");
    uint32_t rect = f, pts = f + 0x10u, rec = f + 0x58u;
    int bad = 1;
    if (CALL(c, VA_VECTOR_NEW, NULL, pts, 8, 5, 1, VA_EMPTY_CTOR)) goto out;
    int32_t e = sar(neg(mul32(s32(c, h + 0x48u), s32(c, h + 0x5cu))), 14);
    int32_t fb = sar(mul32(s32(c, h + 0x4cu), s32(c, h + 0x5cu)), 14);
    (void)r32(c, VA_DEVICE); /* read into an unused local by the original */
    w32(c, rec, 0);
    uint32_t saved = r32(c, gc + 0x22cu);
    w32(c, gc + 0x22cu, rec);
    w32(c, rec + 4u, r32(c, h + 0x2cu) + r32(c, h + 0x60u) - 1u);
    uint32_t g = (uint32_t)neg(sar(s32(c, h + 0x6cu), 1));
    int32_t bands = s32(c, h + 0x60u) - 1;
    if (r16(c, h + 0x36u)) {
        uint32_t c1 = gc + 0x210u, c2 = gc + 0x218u;
        pt_copy(c, pts, h + 0x38u);
        pt_copy(c, pts + 8u, h + 0x40u);
        if (r8(c, h + 0x71u)) {
            int32_t mx = (int32_t)(r32(c, h + 0x38u) + r32(c, h + 0x40u)) >> 1;
            int32_t my = (int32_t)(r32(c, h + 0x3cu) + r32(c, h + 0x44u)) >> 1;
            uint32_t dx = (uint32_t)mx - r32(c, h + 0x72u), dy = (uint32_t)my - r32(c, h + 0x76u);
            pt_copy(c, pts + 0x10u, pts + 8u);
            pt_copy(c, pts + 0x18u, pts);
            pt_add(c, pts + 0x10u, 0u - dx, 0u - dy);
            pt_add(c, pts + 0x18u, 0u - dx, 0u - dy);
            w32(c, rec + 4u, r32(c, h + 0x28u));
        } else {
            for (int32_t i = 0; i < bands; ++i) {
                pt_copy(c, pts + 0x18u, pts);
                pt_add(c, pts + 0x18u, 0u - (uint32_t)e, 0u - g);
                pt_copy(c, pts + 0x10u, pts + 8u);
                pt_add(c, pts + 0x10u, 0u - (uint32_t)e, 0u - g);
                g = (uint32_t)fb;
                if (CALL(c, VA_POLY_FILL_CLIP, NULL, 4, pts)) goto out;
                pt_copy(c, pts, pts + 0x18u);
                pt_copy(c, pts + 8u, pts + 0x10u);
                add32(c, rec + 4u, 0xffffffffu);
            }
        }
        /* The polygon from the line's ends to the viewport's bottom edge:
         * the corner vertices overwrite points 2.. */
        uint32_t n = 2;
        if (s32(c, pts + 4u) < s32(c, pts + 0xcu)) {
            if (s32(c, c2) < s32(c, pts + 8u)) pt_set(c, pts + 8u * n++, r32(c, c2), r32(c, c2 + 4u));
            pt_set(c, pts + 8u * n++, r32(c, c1), r32(c, c2 + 4u));
            if (s32(c, c1) < s32(c, pts)) pt_set(c, pts + 8u * n++, r32(c, c1), r32(c, c1 + 4u));
        } else {
            if (s32(c, c2) > s32(c, pts + 8u)) pt_set(c, pts + 8u * n++, r32(c, c2), r32(c, c1 + 4u));
            pt_set(c, pts + 8u * n++, r32(c, c2), r32(c, c2 + 4u));
            if (s32(c, c1) > s32(c, pts)) pt_set(c, pts + 8u * n++, r32(c, c1), r32(c, c2 + 4u));
        }
        if (CALL(c, VA_POLY_FILL_CLIP, NULL, n, pts)) goto out;
    } else {
        pt_copy(c, rect, h + 0x38u);
        pt_copy(c, rect + 8u, h + 0x40u);
        if (r8(c, h + 0x71u)) {
            w32(c, rec + 4u, r32(c, h + 0x28u));
        } else {
            for (int32_t i = 0; i < bands; ++i) {
                add32(c, rect + 8u, 0u - g);
                add32(c, rect + 0xcu, 0u - g);
                g = (uint32_t)fb;
                if (CALL(c, VA_RECT_FILL, NULL, rect)) goto out;
                w32(c, rect + 4u, r32(c, rect + 0xcu));
                add32(c, rec + 4u, 0xffffffffu);
            }
        }
        w32(c, rect + 0xcu, bottom);
        if ((int32_t)bottom >= s32(c, rect + 4u) && CALL(c, VA_RECT_FILL, NULL, rect)) goto out;
    }
    w32(c, gc + 0x22cu, saved);
    *eax = saved;
    bad = 0;
out:
    frame_pop(c, top);
    return bad;
}

/* 0x42ec08(H, C, off, J): pitch and roll from the camera C (+0x10 negated,
 * +0x12), the line, half the offset onto the line's y, the sky bands, the
 * line one lower, the ground. EAX the ground fill's. */
static int horizon_paint(ctx *c, uint32_t h, uint32_t cam, uint32_t off, uint32_t j, uint32_t *eax) {
    w16(c, h + 0x34u, (uint16_t)(0u - r16(c, cam + 0x10u)));
    w16(c, h + 0x36u, r16(c, cam + 0x12u));
    if (horizon_line(c, h, j, eax)) return 1;
    uint32_t t = (uint32_t)((int32_t)off >> 1);
    add32(c, h + 0x3cu, t);
    add32(c, h + 0x44u, t);
    if (sky_bands(c, h, eax)) return 1;
    add32(c, h + 0x3cu, 1u);
    add32(c, h + 0x44u, 1u);
    if (j) (void)r32(c, j); /* the jolt's first dword is passed but not read by 0x42f0b0 */
    return ground_fill(c, h, eax);
}
/* 0x428140(cam): the terrain cells under the terrain matrix; a single-
 * buffer context with an empty first plane draws with +0x208 set to 1. */
static int terrain_cells_draw(ctx *c, uint32_t cam, uint32_t *eax) {
    if (CALL(c, VA_OBJECT_SELECT, NULL, VA_TERRAIN_MATRIX)) return 1;
    uint32_t gc = r32(c, VA_GC);
    uint32_t kind = r32(c, gc + 4u), keep = r32(c, gc + 0x208u);
    if (kind == 1u && r32(c, r32(c, gc + 8u)) == 0) w32(c, gc + 0x208u, 1);
    if (CALL(c, VA_TERRAIN_DRAW, eax, r32(c, VA_TERRAIN), cam)) return 1;
    w32(c, gc + 0x208u, keep);
    return 0;
}
/* 0x42e700(cam): the frame's sky: the horizon's terrain edge point, the
 * horizon, the sky object (slot +0x34), the terrain cells, 0x42eb40. */
static int sky_horizon(ctx *c, uint32_t cam, uint32_t *eax) {
    uint32_t h = r32(c, VA_HORIZON);
    if (!h) return null_refuse(c, 0x0042e70cu, "sky_horizon", "write through a NULL horizon object (the original faults)");
    w8(c, h + 0x71u, 1);
    if (CALL(c, VA_HORIZON_EDGE, NULL, r32(c, VA_GRID), cam, h + 0x72u)) return 1;
    h = r32(c, VA_HORIZON);
    if (h && horizon_paint(c, h, cam, r32(c, h + 0x30u), VA_JOLT, eax)) return 1;
    uint32_t x = r32(c, VA_SKY_OBJECT);
    if (x && VCALL(c, x, 0x34u, NULL, x, cam)) return 1;
    if (terrain_cells_draw(c, cam, eax)) return 1;
    /* 0x42eb40 is push ebp/mov/pop/ret: EAX stays the terrain draw's. */
    return CALL(c, VA_SKY_AFTER, NULL, cam);
}

/* ======================================================================
 * Service rows. */
void es2_sim_service_scene_bin_cell_index(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (cell_index(&c, A(0), A(1), &eax)) { failed(sc); return; }
    sc->eax = eax;
}
/* 0x4282c0(): the picked object. */
void es2_sim_service_scene_pick_object(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    sc->eax = r32(&c, VA_PICKED);
}
/* 0x4282c8(): 1 while no pick is pending. */
void es2_sim_service_scene_pick_idle(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    sc->eax = r8(&c, VA_PICK_ON) == 0;
}
void es2_sim_service_scene_bin_object(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (bin_object(&c, A(0), &eax)) { failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_scene_bin_store(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t regs[3] = {sc->eax, sc->edx, sc->ecx};
    if (bin_store(&c, A(0), A(1), regs)) { failed(sc); return; }
    sc->eax = regs[0];
    sc->edx = regs[1];
    sc->ecx = regs[2];
}
void es2_sim_service_scene_bin_at_position(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (bin_at_position(&c, A(0), A(1), &eax)) { failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_scene_bin_at_cell(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (bin_at_cell(&c, A(0), A(1), &eax)) { failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_scene_bin_objects(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    if (bin_objects(&c)) { failed(sc); return; }
    sc->eax = 0;
}
void es2_sim_service_scene_sort_obj_draw(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    if (sort_obj_draw(&c, A(0))) { failed(sc); return; }
}
void es2_sim_service_scene_finish(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    if (scene_finish(&c, A(0))) { failed(sc); return; }
}
void es2_sim_service_scene_pick_restore(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    if (pick_restore(&c)) { failed(sc); return; }
}
void es2_sim_service_scene_pick_save(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    if (pick_save(&c)) { failed(sc); return; }
}
void es2_sim_service_scene_draw_distances(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (draw_distances(&c, A(0), &eax)) { failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_scene_distance_cull(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (distance_cull(&c, A(0), A(1), &eax)) { failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_scene_cell_objects_draw(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    if (cell_objects_draw(&c, A(0), A(1), A(2))) { failed(sc); return; }
}
/* 0x428eec(cam): the sort camera. EAX cam. */
void es2_sim_service_scene_sort_camera(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    w32(&c, VA_SORT_CAMERA, A(0));
    sc->eax = A(0);
}
void es2_sim_service_scene_sort_compare(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (sort_compare(&c, A(0), A(1), &eax)) { failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_scene_sort_insert(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (sort_insert(&c, A(0), A(1), &eax)) { failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_scene_sort_pass1(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (sort_pass(&c, A(0), A(1), r32(&c, VA_SORT_ROOT), 1, &eax)) { failed(sc); return; }
    sc->eax = eax;
}
/* EAX: the original's is left over from the loop (not read by 0x429620). */
void es2_sim_service_scene_sort_pass2(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (sort_pass(&c, A(0), A(1), A(2), 0, &eax)) { failed(sc); return; }
}
void es2_sim_service_scene_sort_walk(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    if (sort_walk(&c, A(0))) { failed(sc); return; }
    sc->eax = 0;
}
void es2_sim_service_scene_sort_draw(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = sc->eax;
    if (sort_draw(&c, A(0), A(1), &eax)) { failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_scene_sky_horizon(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (sky_horizon(&c, A(0), &eax)) { failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_scene_terrain_cells_draw(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (terrain_cells_draw(&c, A(0), &eax)) { failed(sc); return; }
    sc->eax = eax;
}
/* 0x42ea44(): with a jolt palette, swap it in and remember the previous
 * one as both the first and the current; the jolt flag set. */
void es2_sim_service_scene_jolt_on(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t b = r32(&c, VA_JOLT_PALETTE), e = 0;
    if (!b) return;
    if (CALL(&c, VA_PALETTE_SWAP, &e, b)) { failed(sc); return; }
    w32(&c, VA_JOLT_FIRST, e);
    w32(&c, VA_JOLT_CURRENT, e);
    w8(&c, VA_JOLT_ON, 1);
    sc->eax = e;
}
/* 0x42ea70(): swap the current palette back and forth; the flag toggled. */
void es2_sim_service_scene_jolt_flicker(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t e = 0;
    if (!r32(&c, VA_JOLT_PALETTE)) return;
    if (CALL(&c, VA_PALETTE_SWAP, &e, r32(&c, VA_JOLT_CURRENT))) { failed(sc); return; }
    w32(&c, VA_JOLT_CURRENT, e);
    w8(&c, VA_JOLT_ON, (uint8_t)(r8(&c, VA_JOLT_ON) ^ 1u));
    sc->eax = e;
}
/* 0x42ea94(): the first palette back; the flag cleared. */
void es2_sim_service_scene_jolt_off(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t e = 0;
    if (!r32(&c, VA_JOLT_PALETTE)) return;
    if (CALL(&c, VA_PALETTE_SWAP, &e, r32(&c, VA_JOLT_FIRST))) { failed(sc); return; }
    w8(&c, VA_JOLT_ON, 0);
    sc->eax = e;
}
/* 0x42ebe8(H, C, J): 0x42ec08 with H's own offset +0x30. */
void es2_sim_service_scene_horizon_forward(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (horizon_paint(&c, A(0), A(1), r32(&c, A(0) + 0x30u), A(2), &eax)) { failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_scene_horizon_paint(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (horizon_paint(&c, A(0), A(1), A(2), A(3), &eax)) { failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_scene_horizon_line(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (horizon_line(&c, A(0), A(1), &eax)) { failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_scene_sky_bands(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (sky_bands(&c, A(0), &eax)) { failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_scene_ground_fill(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (ground_fill(&c, A(0), &eax)) { failed(sc); return; }
    sc->eax = eax;
}
/* 0x42f6d2(obj, flags): the horizon line object's destructor: counter - 1,
 * the base destructor 0x475900(obj, 0), flags bit 0: delete. */
void es2_sim_service_scene_hzline_dtor(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t obj = A(0), eax = sc->eax;
    if (!obj) return;
    uint32_t p = r32(&c, VA_COUNTER_POINTER);
    w32(&c, p, r32(&c, p) - 1u);
    if (CALL(&c, VA_LINE_BASE_DTOR, &eax, obj, 0)) { failed(sc); return; }
    if ((A(1) & 1u) && CALL(&c, VA_DELETE, &eax, obj)) { failed(sc); return; }
    sc->eax = eax;
}
