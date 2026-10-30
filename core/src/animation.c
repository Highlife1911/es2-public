#include "es2/core/animation.h"
#include "es2/core/matrix.h"
#include "es2/core/movement.h"
#include "es2/core/pose.h"
#include "es2/core/track.h"
#include "es2/core/trig.h"
#include "fixed.h"
#include <string.h>

static uint64_t pair(uint32_t edx, uint32_t eax) { return ((uint64_t)edx << 32) | eax; }

/* Zeroed matrix with the three Q14 diagonal words, in the original's order. */
static void identity(uint8_t *m) {
    memset(m, 0, 32);
    fx_w16(m + 0x10, 0x4000);
    fx_w16(m + 6, 0x4000);
    fx_w16(m, 0x4000);
}

uint64_t es2_manager_reset_registers(const es2_guest_memory *m, uint32_t manager,
                                     const void *input, const void *quarter,
                                     uint8_t padding, uint32_t edx) {
    void *c = m->context;
    const uint8_t *in = input;
    uint32_t track = m->read32(c, manager + 0x1au);
    if (!track) return pair(edx, 0);
    (void)es2_track_scaled_inverse_registers(m->host(c, track), input, quarter, padding);
    /* The matrix pointer is read after the track transform, as in the original. */
    uint32_t matrix = m->read32(c, manager + 0x16u);
    for (uint32_t i = 0; i < 20; i += 4) m->write32(c, matrix + i, fx_r32(in + i));
    m->write32(c, matrix + 0x1cu, fx_r32(in + 0x1c));
    m->write32(c, matrix + 0x14u, fx_r32(in + 0x14));
    m->write32(c, matrix + 0x18u, fx_r32(in + 0x18));
    return pair(matrix, matrix + 0x14u);
}

uint64_t es2_animation_step_registers(const es2_guest_memory *m, uint32_t actor,
                                      uint32_t manager, uint32_t step,
                                      const es2_math_tables *t,
                                      es2_track_advance advance, void *context,
                                      uint8_t padding) {
    void *c = m->context;
    uint8_t start[32], point[12], angles[6];
    identity(start);
    (void)es2_manager_reset_registers(m, manager, start, t->quarter, padding, 0);
    advance(context, manager, step);
    uint32_t matrix = m->read32(c, manager + 0x16u);
    uint8_t *animation = m->host(c, matrix);
    uint8_t *a = m->host(c, actor);
    if (!fx_r16(a + 0x32)) {
        es2_matrix_euler(a + 0x0c, a + 0x12, t->quarter);
        fx_w16(a + 0x32, 1);
    }
    es2_transform_point(animation + 0x14, a + 0x12, point);
    fx_w32(a + 0x2e, fx_r32(point + 8));
    fx_w32(a + 0x26, fx_r32(point));
    fx_w32(a + 0x2a, fx_r32(point + 4));
    es2_matrix_angles(animation, angles, t->large, t->small, t->corrections);
    fx_w16(angles + 4, (uint32_t)fx_r16(angles + 4) + fx_r16(a + 0x10));
    fx_w16(angles, (uint32_t)fx_r16(angles) + fx_r16(a + 0x0c));
    fx_w16(angles + 2, (uint32_t)fx_r16(angles + 2) + fx_r16(a + 0x0e));
    fx_w32(a + 0x0c, fx_r32(angles));
    fx_w16(a + 0x10, fx_r16(angles + 4));
    fx_w16(a + 0x32, 0);
    identity(animation);
    return pair(0x20, matrix);
}

/* ---- 0x478d38 track advance and its subtree ----------------------------
 * Track T: resource [T+0], clip/frame words +4/+6, next clip/frame +8/+0a,
 * transition trigger frame +0c (0xffff = none) with transition target clip/
 * frame +0e/+10, duration +12 and pose +14, stop clip/frame +16/+18
 * (0xffff = any frame), stop flags byte +1a, time in frame +1c, frame
 * duration +1e, pose +20..2d, matrix +2e..4d, rate +4e, state word +50
 * (bit 0 stopped, bit 1 in transition). Guest words are read and written in
 * the original order; byte arrays, poses and matrices go through host(). */

#define R16(a) m->read16(m->context, (a))
#define R32(a) m->read32(m->context, (a))
#define W16(a, v) m->write16(m->context, (a), (uint16_t)(v))
#define W32(a, v) m->write32(m->context, (a), (v))

static uint32_t clip_at(const es2_guest_memory *m, uint32_t track, uint32_t index_field) {
    uint32_t index = (uint32_t)fx_s16(R16(track + index_field));
    return R32(R32(R32(track) + 6u) + 4u * index);
}

/* ANSequence 478760/47877c/47878c/4787b8, ANCyclicSequence 4787e4/478804/
 * 478820/47882c: arithmetic on the full argument slots, compares on words. */
uint64_t es2_clip_call(const es2_guest_memory *m, const es2_clip_classes *k, uint32_t clip,
                       uint32_t slot, const uint32_t args[3], uint32_t eax, uint32_t edx) {
    uint32_t vtable = R32(clip);
    if (vtable != k->sequence && vtable != k->cyclic) return k->other(k->context, clip, slot, args);
    int cyclic = vtable == k->cyclic;
    uint32_t a = args[0];
    if (slot == 0x20u) {
        uint32_t last = (uint32_t)(fx_s16(R16(clip + 0x0au)) - 1);
        if (fx_s16(a) < (int32_t)last) return pair(cyclic ? a : last, a + 1u);
        return cyclic ? pair(a, 0) : pair(last, a);
    }
    if (slot == 0x24u) {
        if (!cyclic) return pair(edx, (a & 0xffffu) ? a - 1u : a);
        if (a & 0xffffu) return pair(a, a - 1u);
        uint32_t last = ((a & 0xffff0000u) | R16(clip + 0x0au)) - 1u;
        return pair(last, last);
    }
    if (cyclic) return pair(edx, (eax & 0xffff0000u) | 1u);
    uint32_t b = args[1];
    int beyond = slot == 0x28u ? fx_s16(b) > fx_s16(a) : fx_s16(b) < fx_s16(a);
    if (beyond || ((b & 0xffffu) == (a & 0xffffu) && !(args[2] & 0xffffu)))
        return pair(b, (a & 0xffff0000u) | 1u);
    return pair(b, 0);
}

uint16_t es2_clip_step(const es2_guest_memory *m, const es2_clip_classes *k,
                       uint32_t clip, int forward, uint16_t frame) {
    const uint32_t args[3] = {frame, 0, 0};
    return (uint16_t)es2_clip_call(m, k, clip, forward ? 0x20u : 0x24u, args, 0, 0);
}

/* Duration word of (clip, frame): [clip+0x0c] holds 6-byte frame records. */
static uint16_t frame_duration(const es2_guest_memory *m, uint32_t clip, uint16_t frame) {
    return R16(R32(clip + 0x0cu) + 6u * (uint32_t)fx_s16(frame));
}

uint64_t es2_track_clip_registers(const es2_guest_memory *m, uint32_t t,
                                  uint32_t clip, uint32_t frame, uint32_t phase,
                                  uint32_t eax, uint32_t pose_function,
                                  es2_guest_call call, void *context) {
    W16(t + 4u, clip);
    W16(t + 6u, frame);
    W16(t + 8u, R16(t + 4u));
    uint32_t frame_slot = (eax & 0xffff0000u) | R16(t + 6u);
    uint32_t object = clip_at(m, t, 4u);
    uint32_t args[3] = {object, frame_slot, 0};
    uint64_t next = call(context, R32(R32(object) + 0x20u), args, 2);
    W16(t + 0x0au, next);
    W16(t + 0x1cu, phase);
    /* The virtual method may have changed both indices and the graph. */
    object = clip_at(m, t, 4u);
    W16(t + 0x1eu, frame_duration(m, object, R16(t + 6u)));
    args[2] = (uint32_t)fx_s16(R16(t + 6u));
    args[1] = (uint32_t)fx_s16(R16(t + 4u));
    args[0] = t;
    uint64_t result = call(context, pose_function, args, 3);
    W16(t + 0x18u, 0xffffu);
    W16(t + 0x16u, 0xffffu);
    W16(t + 0x1au, 0);
    W16(t + 0x0cu, 0xffffu);
    W16(t + 0x50u, 1);
    return result;
}

/* ---- 0x479720 track step, 0x4789d0 sort, 0x478aac track list ------------
 * Exact EDX:EAX: register words the original leaves behind are followed
 * wherever they reach a virtual-call slot or the result. LOW() replaces the
 * low word only, as the original's 16-bit moves into AX/DX do. */
#define LOW(r, w) ((r) = ((r) & 0xffff0000u) | (uint16_t)(w))

/* Transition pose [R+0x12] + 12 * s16 T+0x14 into T+0x22..0x2d, enable word
 * T+0x20 = 1. Leaves EAX = 3 * index and EDX = high word of the first angle
 * dword : the third angle word. */
static void transition_pose(const es2_guest_memory *m, uint32_t t, uint32_t *eax, uint32_t *edx) {
    uint32_t three = 3u * (uint32_t)fx_s16(R16(t + 0x14u));
    uint32_t poses = R32(R32(t) + 0x12u);
    W32(t + 0x22u, R32(poses + 4u * three + 6u));
    W16(t + 0x26u, R16(poses + 4u * three + 0x0au));
    poses = R32(R32(t) + 0x12u);
    uint32_t angles = R32(poses + 4u * three);
    W32(t + 0x28u, angles);
    uint16_t last = R16(poses + 4u * three + 4u);
    W16(t + 0x2cu, last);
    W16(t + 0x20u, 1);
    *eax = three;
    *edx = (angles & 0xffff0000u) | last;
}

/* Virtual next (0x20) or previous (0x24) frame of the clip with index word
 * `index`, with the full 32-bit frame slot. At the call EAX holds the vtable
 * and EDX the clip list, except at 4797bf (EAX list, EDX clip). */
static uint64_t frame_call(const es2_guest_memory *m, const es2_clip_classes *k, uint32_t t,
                           uint16_t index, uint32_t method, uint32_t slot, int list_in_eax) {
    uint32_t clips = R32(R32(t) + 6u);
    uint32_t clip = R32(clips + 4u * (uint32_t)fx_s16(index));
    const uint32_t args[3] = {slot, 0, 0};
    if (list_in_eax) return es2_clip_call(m, k, clip, method, args, clips, clip);
    return es2_clip_call(m, k, clip, method, args, R32(clip), clips);
}

/* 0x478ef4 with the current clip and frame (sign-extended words). */
static uint64_t pose_call(const es2_guest_memory *m, uint32_t t) {
    uint32_t frame = (uint32_t)fx_s16(R16(t + 6u));
    uint32_t clip = (uint32_t)fx_s16(R16(t + 4u));
    return es2_clip_pose_registers(m, t, clip, frame);
}

/* 0x478f6c (forward) or 0x478ff4 (inverse) on T. A disabled track (T+0x20
 * zero) keeps the registers; otherwise EAX = T+0x42, EDX = T+0x2e. */
static void track_matrix(const es2_guest_memory *m, uint32_t t, const void *quarter, uint8_t padding,
                         int inverse, uint32_t *eax, uint32_t *edx) {
    if (!R16(t + 0x20u)) return;
    void *host = m->host(m->context, t);
    if (inverse) (void)es2_track_inverse_registers(host, quarter, padding, *eax, *edx);
    else (void)es2_track_forward_registers(host, quarter, padding, *eax, *edx);
    *eax = t + 0x42u;
    *edx = t + 0x2eu;
}

uint64_t es2_track_step_registers(const es2_guest_memory *m, const es2_clip_classes *k, uint32_t t,
                                  uint32_t step, uint32_t edx, const void *quarter, uint8_t padding) {
    uint32_t eax = step;
    if ((R16(t + 0x50u) & 1) && (R16(t + 0x1au) & 1)) return pair(edx, eax);
    /* IMUL of two sign-extended words: EDX is the sign of the product. */
    int32_t product = fx_s16(step) * fx_s16(R16(t + 0x4eu));
    edx = product < 0 ? UINT32_MAX : 0;
    eax = fx_sar((uint32_t)product, 8);
    if (!(uint16_t)eax) return pair(edx, eax);
    uint16_t time = (uint16_t)(R16(t + 0x1cu) + eax);
    /* Backward at frame 0: a clip whose previous frame is 0 (ANSequence)
     * clamps the time to 0, unless frame 0 is the transition trigger. */
    if (fx_s16(time) < 0 && !R16(t + 6u)) {
        LOW(eax, R16(t + 6u));
        if ((uint16_t)eax != R16(t + 0x0cu)) {
            LOW(edx, R16(t + 6u));
            uint64_t r = frame_call(m, k, t, R16(t + 4u), 0x24u, edx, 0);
            eax = (uint32_t)r;
            edx = (uint32_t)(r >> 32);
            if (!(uint16_t)eax) time = 0;
        }
    }
    if (!time) {
        W16(t + 0x1cu, 0);
        if (R16(t + 0x50u) & 2) {
            /* Leave the transition: next frame of the current clip. */
            uint16_t clip = R16(t + 4u);
            W16(t + 8u, clip);
            LOW(eax, R16(t + 6u));
            uint64_t r = frame_call(m, k, t, clip, 0x20u, eax, 1);
            W16(t + 0x0au, (uint16_t)r);
            uint32_t object = clip_at(m, t, 4u);
            W16(t + 0x1eu, frame_duration(m, object, R16(t + 6u)));
            W16(t + 0x50u, R16(t + 0x50u) & 0xfffdu);
            r = pose_call(m, t);
            eax = (uint32_t)r;
            edx = (uint32_t)(r >> 32);
        }
        LOW(edx, R16(t + 4u));
        if ((uint16_t)edx == R16(t + 0x16u)) {
            LOW(eax, R16(t + 0x18u));
            if ((uint16_t)eax == R16(t + 6u) || (uint16_t)eax == 0xffffu) W16(t + 0x50u, R16(t + 0x50u) | 1u);
        }
        return pair(edx, eax);
    }
    if (fx_s16(time) >= fx_s16(R16(t + 0x1eu))) {
        while (time && fx_s16(time) >= fx_s16(R16(t + 0x1eu))) {
            track_matrix(m, t, quarter, padding, 0, &eax, &edx);
            LOW(edx, R16(t + 8u));
            W16(t + 4u, (uint16_t)edx);
            W16(t + 6u, R16(t + 0x0au));
            LOW(eax, R16(t + 0x0au));
            if ((uint16_t)eax == R16(t + 0x0cu)) {
                /* The new frame triggers the transition. */
                LOW(edx, R16(t + 0x0eu));
                W16(t + 8u, (uint16_t)edx);
                W16(t + 0x0au, R16(t + 0x10u));
                time = (uint16_t)(time - R16(t + 0x1eu));
                LOW(eax, R16(t + 0x12u));
                W16(t + 0x1eu, (uint16_t)eax);
                W16(t + 0x1cu, 0);
                W16(t + 0x0cu, 0xffffu);
                W16(t + 0x50u, R16(t + 0x50u) | 2u);
                transition_pose(m, t, &eax, &edx);
            } else {
                LOW(eax, R16(t + 0x0au));
                uint64_t r = frame_call(m, k, t, R16(t + 8u), 0x20u, eax, 0);
                W16(t + 0x0au, (uint16_t)r);
                uint32_t object = clip_at(m, t, 4u);
                time = (uint16_t)(time - R16(t + 0x1eu));
                uint16_t duration = frame_duration(m, object, R16(t + 6u));
                W16(t + 0x1eu, duration);
                /* A zero frame duration ends the step (0x479906). */
                if (!duration) time = 0;
                W16(t + 0x1cu, 0);
                W16(t + 0x50u, R16(t + 0x50u) & 0xfffdu);
                r = pose_call(m, t);
                eax = (uint32_t)r;
                edx = (uint32_t)(r >> 32);
            }
            if (R16(t + 4u) == R16(t + 0x16u)) {
                LOW(eax, R16(t + 0x18u));
                if ((uint16_t)eax == R16(t + 6u) || (uint16_t)eax == 0xffffu) {
                    W16(t + 0x50u, R16(t + 0x50u) | 1u);
                    if (R16(t + 0x1au) & 1) time = 0;
                }
            }
        }
        W16(t + 0x1cu, time);
        return pair(edx, eax);
    }
    if (fx_s16(time) >= 0) {
        W16(t + 0x1cu, time);
        return pair(edx, eax);
    }
    do {
        LOW(eax, R16(t + 4u));
        W16(t + 8u, (uint16_t)eax);
        LOW(edx, R16(t + 6u));
        W16(t + 0x0au, (uint16_t)edx);
        if (R16(t + 6u) == R16(t + 0x0cu)) {
            /* Backward into the transition target. */
            LOW(eax, R16(t + 0x0eu));
            W16(t + 4u, (uint16_t)eax);
            LOW(edx, R16(t + 0x10u));
            W16(t + 6u, (uint16_t)edx);
            W16(t + 0x1eu, R16(t + 0x12u));
            W16(t + 0x0cu, 0xffffu);
            W16(t + 0x50u, R16(t + 0x50u) | 2u);
            transition_pose(m, t, &eax, &edx);
        } else {
            LOW(eax, R16(t + 6u));
            uint64_t r = frame_call(m, k, t, R16(t + 4u), 0x24u, eax, 0);
            uint16_t frame = (uint16_t)r;
            W16(t + 6u, frame);
            uint32_t object = clip_at(m, t, 4u);
            W16(t + 0x1eu, frame_duration(m, object, frame));
            W16(t + 0x50u, R16(t + 0x50u) & 0xfffdu);
            r = pose_call(m, t);
            eax = (uint32_t)r;
            edx = (uint32_t)(r >> 32);
        }
        track_matrix(m, t, quarter, padding, 1, &eax, &edx);
        edx = 0u - (uint32_t)fx_s16(time);
        if ((int32_t)edx > fx_s16(R16(t + 0x1eu))) {
            W16(t + 0x1cu, 0);
            time = (uint16_t)(time + R16(t + 0x1eu));
        } else {
            time = (uint16_t)(time + R16(t + 0x1eu));
            W16(t + 0x1cu, time);
            time = 0;
        }
        LOW(eax, R16(t + 4u));
        if ((uint16_t)eax == R16(t + 0x16u)) {
            LOW(eax, R16(t + 0x18u));
            if ((uint16_t)eax == R16(t + 6u) || (uint16_t)eax == 0xffffu) {
                W16(t + 0x50u, R16(t + 0x50u) | 1u);
                if (R16(t + 0x1au) & 1) {
                    time = 0;
                    W16(t + 0x1cu, 0);
                }
            }
        }
    } while (fx_s16(time) < 0);
    return pair(edx, eax);
}

static unsigned track_total(const es2_guest_memory *m, uint32_t manager) {
    unsigned n = 0;
    while (n < 10 && R32(manager + 0x1au + 4u * n)) ++n;
    return n;
}

/* 0x478e8c: EAX = high word of the current clip pointer : its priority
 * word +6, EDX = sign-extended clip index T+4. */
static uint64_t clip_priority(const es2_guest_memory *m, uint32_t t) {
    uint32_t index = (uint32_t)fx_s16(R16(t + 4u));
    uint32_t clip = R32(R32(R32(t) + 6u) + 4u * index);
    return pair(index, (clip & 0xffff0000u) | R16(clip + 6u));
}

uint64_t es2_track_sort_registers(const es2_guest_memory *m, uint32_t manager) {
    uint32_t n = track_total(m, manager);
    uint32_t eax = n, edx = manager + 0x1au + 4u * n;
    if (n < 2) return pair(edx, eax);
    int swapped;
    do {
        swapped = 0;
        for (uint32_t i = 0; i + 1 < n; ++i) {
            uint32_t slot = manager + 0x1au + 4u * i;
            uint64_t first = clip_priority(m, R32(slot));
            uint64_t second = clip_priority(m, R32(slot + 4u));
            eax = (uint32_t)second;
            edx = (uint32_t)(second >> 32);
            if (fx_s16((uint32_t)first) > fx_s16(eax)) {
                eax = R32(slot);
                edx = R32(slot + 4u);
                W32(slot, edx);
                W32(slot + 4u, eax);
                swapped = 1;
            }
        }
    } while (swapped);
    return pair(edx, eax);
}

uint64_t es2_track_list_registers(const es2_guest_memory *m, uint32_t manager, uint32_t step,
                                  const es2_clip_classes *k, const void *quarter, uint8_t padding) {
    uint32_t n = track_total(m, manager), eax = n;
    /* Each step slot keeps the high word of the previous call's EAX. */
    for (uint32_t i = n; i-- > 0;) {
        uint32_t t = R32(manager + 0x1au + 4u * i);
        eax = (uint32_t)es2_track_step_registers(m, k, t, (eax & 0xffff0000u) | (step & 0xffffu), t,
                                                 quarter, padding);
    }
    uint32_t edx = (uint32_t)(es2_track_sort_registers(m, manager) >> 32);
    uint32_t first = R32(manager + 0x1au);
    if (!first) return pair(edx, 0);
    void *c = m->context;
    void *track = m->host(c, first);
    return es2_track_scaled_registers(track, m->host(c, R32(manager + 0x16u)), quarter, padding);
}

/* Original: DBSIM.EXE 0x479ab0. Blend the poses of (clip, frame) and (next
 * clip, next frame) into the node poses selected by the clip's node map;
 * node 0 is skipped. The blend fraction is time/duration in Q10, rounded to
 * nearest. The registers follow the last node (see animation.h). */
uint64_t es2_track_poses_registers(const es2_guest_memory *m, uint32_t t, uint32_t poses, uint32_t flags) {
    void *c = m->context;
    uint32_t clip = clip_at(m, t, 4u);
    int32_t count = fx_s16(R16(clip + 0x10u));
    uint32_t map = R32(clip + 0x12u);
    int32_t duration = fx_s16(R16(t + 0x1eu));
    int32_t fraction = (fx_s16(R16(t + 0x1cu)) * 1024 + duration / 2) / duration;
    uint32_t first = R32(clip + 0x16u) + 2u * (uint32_t)(fx_s16(R16(t + 6u)) * count);
    uint32_t second = R32(clip_at(m, t, 8u) + 0x16u) + 2u * (uint32_t)(fx_s16(R16(t + 0x0au)) * count);
    uint32_t eax = 0, edx = map;
    for (int32_t j = 0; j < count; ++j, first += 2u, second += 2u, map += 2u) {
        uint16_t a = R16(first), b = R16(second), node = R16(map);
        if (!node) {
            /* A skipped node leaves the two frame words over their addresses' high words. */
            eax = (first & 0xffff0000u) | a;
            edx = (second & 0xffff0000u) | b;
            continue;
        }
        uint32_t out = poses + 12u * (uint32_t)fx_s16(node);
        uint32_t source = R32(R32(t) + 0x12u);
        if (a == b) {
            uint32_t from = source + 12u * (uint32_t)fx_s16(a);
            for (uint32_t i = 0; i < 12; i += 4) W32(out + i, R32(from + i));
        } else {
            es2_pose_interpolate(m->host(c, source + 12u * (uint32_t)fx_s16(a)),
                                 m->host(c, source + 12u * (uint32_t)fx_s16(b)),
                                 (uint32_t)fraction, m->host(c, out));
        }
        *(uint8_t *)m->host(c, flags + (uint32_t)fx_s16(node)) = 1;
        eax = (uint32_t)fx_s16(node);
        edx = flags;
    }
    return pair(edx, eax);
}

/* Original: DBSIM.EXE 0x478b00. Every track from last to first; the pose and
 * flag arrays and the track pointer are reread per track. */
uint64_t es2_pose_list_registers(const es2_guest_memory *m, uint32_t manager) {
    uint32_t n = track_total(m, manager);
    uint64_t result = pair(manager + 0x1au + 4u * n, n);
    for (uint32_t i = n; i-- > 0;) {
        uint32_t flags = R32(manager + 0x0eu), poses = R32(manager + 0x12u);
        result = es2_track_poses_registers(m, R32(manager + 0x1au + 4u * i), poses, flags);
    }
    return result;
}

static uint8_t *byte_ref(const es2_guest_memory *m, uint32_t address) {
    return m->host(m->context, address);
}

/* 0x478c64: rebuild node matrices in hierarchy order. A root node (parent
 * 0xffff) is rebuilt when its pose changed (flag 1), a child also when its
 * parent was rebuilt (flag 2). Then all flags are cleared.
 * Trap: pose_parent's uninitialized local byte 19 ([ebp-0Dh]) shares its
 * stack slot with the high byte of the matrix pointer the root call 0x478c1c
 * pushes at 0x478c54, so after a root call it holds that byte; before the
 * first one it is `padding`. */
uint64_t es2_node_matrices_registers(const es2_guest_memory *m, uint32_t manager, const void *quarter,
                                     uint8_t padding) {
    void *c = m->context;
    uint32_t hierarchy = R32(R32(manager + 4u) + 0x2au);
    uint32_t entry = R32(hierarchy + 0x0cu);
    uint8_t residue = padding;
    /* The entry count is reread every iteration, as in the original. */
    for (int32_t i = 0; i < fx_s16(R16(hierarchy + 0x0au)); ++i, entry += 4u) {
        uint32_t node = (uint32_t)fx_s16(R16(entry + 2u));
        if (R16(entry) == 0xffffu) {
            if (!(*byte_ref(m, R32(manager + 0x0eu) + node) & 1)) continue;
            uint32_t out = R32(manager + 0x16u) + 32u * node;
            es2_pose_matrix(m->host(c, R32(manager + 0x12u) + 12u * node), m->host(c, out), quarter);
            residue = (uint8_t)(out >> 24);
        } else {
            if (!(*byte_ref(m, R32(manager + 0x0eu) + node) & 1) &&
                !(*byte_ref(m, R32(manager + 0x0eu) + (uint32_t)fx_s16(R16(entry))) & 2))
                continue;
            uint32_t matrices = R32(manager + 0x16u);
            uint32_t parent = matrices + 32u * (uint32_t)fx_s16(R16(entry));
            uint32_t pose = R32(manager + 0x12u) + 12u * node;
            (void)es2_pose_parent_registers(m->host(c, pose), m->host(c, parent), m->host(c, matrices + 32u * node),
                                            quarter, residue);
        }
        /* The node word and the flag array are reread after the call. */
        *byte_ref(m, R32(manager + 0x0eu) + (uint32_t)fx_s16(R16(entry + 2u))) |= 2;
    }
    uint32_t count = (uint32_t)fx_s16(R16(manager + 0x0cu));
    uint32_t flags = R32(manager + 0x0eu);
    memset(m->host(c, flags), 0, count);
    return pair(count, flags);
}

uint64_t es2_track_advance_registers(const es2_guest_memory *m, uint32_t manager,
                                     uint32_t step, const es2_clip_classes *k,
                                     const void *quarter, uint8_t padding) {
    (void)es2_track_list_registers(m, manager, step, k, quarter, padding);
    (void)es2_pose_list_registers(m, manager);
    return es2_node_matrices_registers(m, manager, quarter, padding);
}

/* ---- 0x4793d4 route search, 0x47967c target, 0x4796d8 rate ------------
 * Frame records ([clip+0x0c] + 6*frame): duration, transition count,
 * first transition index into [R+0x1e]. Register values are tracked where
 * they reach a virtual call or the result. */
/* Original: DBSIM.EXE 0x4793d4. The high word of the first reached call's
 * time slot is the caller's EDI in the original; no hook sees it (0 here). */
uint64_t es2_track_route_registers(const es2_guest_memory *m, const es2_clip_classes *k, uint32_t t) {
    int forward = fx_s16(R16(t + 0x4eu)) >= 0;
    uint32_t start = forward ? 0x0au : 6u, eax, edx, edi = 0;
    W16(t + 0x0cu, R16(t + start));
    for (;;) {
        uint32_t frame = (uint32_t)fx_s16(R16(t + 0x0cu));
        uint32_t record = R32(clip_at(m, t, 4u) + 0x0cu) + 6u * frame;
        uint32_t count = (uint32_t)fx_s16(R16(record + 2u));
        uint32_t index = (uint32_t)fx_s16(R16(record + 4u));
        eax = 3u * frame;
        /* A negative count would loop about 2^32 times in the original. */
        for (; count; --count, ++index) {
            uint32_t resource = R32(t), transitions = R32(resource + 0x1eu) + 8u * index;
            eax = resource;
            if (R16(transitions + 2u) != R16(t + 0x16u)) continue;
            W16(t + 0x12u, R16(transitions));
            W16(t + 0x0eu, R16(t + 0x16u));
            W16(t + 0x10u, R16(transitions + 4u));
            W16(t + 0x14u, R16(transitions + 6u));
            if (R16(t + 0x18u) == 0xffffu) return pair(resource, (resource & 0xffff0000u) | R16(t + 0x14u));
            uint16_t rate = R16(t + 0x4eu), target = R16(t + 0x18u), to = R16(t + 0x10u);
            edi = (edi & 0xffff0000u) | R16(t + 0x1cu);
            uint32_t clips = R32(resource + 6u);
            uint32_t clip = R32(clips + 4u * (uint32_t)fx_s16(R16(t + 0x16u)));
            uint64_t result;
            if (fx_s16(rate) >= 0) {
                const uint32_t args[3] = {(clips & 0xffff0000u) | to, (resource & 0xffff0000u) | target, edi};
                result = es2_clip_call(m, k, clip, 0x28u, args, args[0], clip);
            } else {
                const uint32_t args[3] = {(clip & 0xffff0000u) | to, (clips & 0xffff0000u) | target, edi};
                result = es2_clip_call(m, k, clip, 0x2cu, args, R32(clip), args[0]);
            }
            if ((uint16_t)result) return result;
            eax = (uint32_t)result;
        }
        edi = (uint32_t)fx_s16(R16(t + 0x0cu));
        uint32_t clip = clip_at(m, t, 4u);
        const uint32_t args[3] = {(eax & 0xffff0000u) | R16(t + 0x0cu), 0, 0};
        uint64_t result = es2_clip_call(m, k, clip, forward ? 0x20u : 0x24u, args, R32(R32(t) + 6u), clip);
        edx = (uint32_t)(result >> 32);
        W16(t + 0x0cu, (uint16_t)result);
        eax = (uint32_t)fx_s16((uint32_t)result);
        if (eax == edi || (uint16_t)result == R16(t + start)) break;
    }
    W16(t + 0x0cu, 0xffffu);
    return pair(edx, eax);
}

uint64_t es2_track_target_registers(const es2_guest_memory *m, const es2_clip_classes *k,
                                    uint32_t t, uint32_t clip, uint32_t frame, uint32_t flags,
                                    uint32_t edx) {
    W16(t + 0x16u, clip);
    W16(t + 0x18u, frame);
    W16(t + 0x1au, flags);
    edx = (edx & 0xffff0000u) | (flags & 0xffffu);
    W16(t + 0x50u, R16(t + 0x50u) & ~1u);
    if (R16(t + 0x16u) != 0xffffu) {
        if (R16(t + 4u) != R16(t + 0x16u)) return es2_track_route_registers(m, k, t);
        edx = (edx & 0xffff0000u) | R16(t + 6u);
        if (R16(t + 6u) != R16(t + 0x18u) || R16(t + 0x1cu)) return es2_track_route_registers(m, k, t);
    }
    W16(t + 0x50u, R16(t + 0x50u) | 1u);
    W16(t + 0x0cu, 0xffffu);
    return pair(edx, t);
}

uint64_t es2_track_rate_registers(const es2_guest_memory *m, const es2_clip_classes *k,
                                  uint32_t t, uint32_t rate) {
    uint16_t old = R16(t + 0x4eu);
    W16(t + 0x4eu, rate);
    if ((fx_s16(rate) < 0) == (fx_s16(old) < 0)) return pair(rate, t);
    uint16_t flags = R16(t + 0x1au), frame = R16(t + 0x18u), clip = R16(t + 0x16u);
    return es2_track_target_registers(m, k, t, clip, frame, flags, (rate & 0xffff0000u) | frame);
}

uint64_t es2_actor_animate_registers(const es2_guest_memory *m, uint32_t actor, uint32_t step,
                                     es2_actor_step animation_step, void *context) {
    if (!(R16(actor + 0x38u) >> 8)) return pair(actor + 0x38u, actor);
    return animation_step(context, actor, R32(actor + 0x34u), step);
}

uint64_t es2_ground_animate_registers(const es2_guest_memory *m, uint32_t actor, uint16_t scale,
                                      uint32_t eax, es2_actor_rate rate, es2_actor_animate animate,
                                      void *context) {
    uint32_t value = (eax & 0xffff0000u) | R16(actor + 0x2a0u);
    (void)rate(context, R32(actor + 0x22cu), value);
    return animate(context, actor, es2_movement_step(100, scale));
}

/* ---- 0x402628 snapshot, 0x4027fc rollback -------------------------------- */

/* CW3220 memcpy: dwords forward, then the remaining bytes forward. */
static void runtime_copy(const es2_guest_memory *m, uint32_t to, uint32_t from, uint32_t count) {
    uint8_t *d = m->host(m->context, to);
    const uint8_t *s = m->host(m->context, from);
    uint32_t i = 0;
    for (; i < (count & ~3u); i += 4)
        for (unsigned j = 0; j < 4; ++j) d[i + j] = s[i + j];
    for (; i < count; ++i) d[i] = s[i];
}

/* Field order of the original's unrolled copy; covers all 82 bytes. */
static void copy_track(const es2_guest_memory *m, uint32_t to, uint32_t from) {
    W32(to, R32(from));
    for (uint32_t o = 4; o <= 0x20u; o += 2) W16(to + o, R16(from + o));
    W32(to + 0x22u, R32(from + 0x22u));
    W16(to + 0x26u, R16(from + 0x26u));
    W32(to + 0x28u, R32(from + 0x28u));
    W16(to + 0x2cu, R16(from + 0x2cu));
    W16(to + 0x4eu, R16(from + 0x4eu));
    W16(to + 0x50u, R16(from + 0x50u));
    for (uint32_t o = 0x2e; o < 0x42u; o += 4) W32(to + o, R32(from + o));
    W32(to + 0x4au, R32(from + 0x4au));
    W32(to + 0x42u, R32(from + 0x42u));
    W32(to + 0x46u, R32(from + 0x46u));
}

static void cache_rotation(const es2_guest_memory *m, uint32_t actor, const void *quarter) {
    if (R16(actor + 0x32u)) return;
    es2_matrix_euler(m->host(m->context, actor + 0x0cu), m->host(m->context, actor + 0x12u), quarter);
    W16(actor + 0x32u, 1);
}

static uint64_t copy_actor(const es2_guest_memory *m, uint32_t actor, uint32_t block,
                           const void *quarter, int save) {
    uint32_t manager = R32(actor + 0x34u);
    cache_rotation(m, actor, quarter);
    uint32_t mine = actor + 0x12u, from = save ? mine : block, to = save ? block : mine;
    for (uint32_t o = 0; o < 20u; o += 4) W32(to + o, R32(from + o));
    W32(to + 0x1cu, R32(from + 0x1cu));
    W32(to + 0x14u, R32(from + 0x14u));
    W32(to + 0x18u, R32(from + 0x18u));
    for (uint32_t i = 0; i < 10; ++i) {
        uint32_t track = R32(manager + 0x1au + 4u * i), state = block + 0x20u + 82u * i;
        if (track) copy_track(m, save ? state : track, save ? track : state);
    }
    uint32_t n = (uint32_t)fx_s16(R16(manager + 0x0cu));
    struct { uint32_t field, offset, size; } arrays[3] = {{0x0e, 0x354, n}, {0x12, 0x372, 12u * n}, {0x16, 0x4da, 32u * n}};
    uint32_t destination = 0;
    for (unsigned i = 0; i < 3; ++i) {
        uint32_t array = R32(manager + arrays[i].field), copy = block + arrays[i].offset;
        destination = save ? copy : array;
        runtime_copy(m, destination, save ? array : copy, arrays[i].size);
    }
    return pair(32u * n, destination);
}

uint64_t es2_actor_snapshot_registers(const es2_guest_memory *m, uint32_t actor, uint32_t block,
                                      const void *quarter) {
    return copy_actor(m, actor, block, quarter, 1);
}

uint64_t es2_actor_rollback_registers(const es2_guest_memory *m, uint32_t actor, uint32_t block,
                                      const void *quarter) {
    return copy_actor(m, actor, block, quarter, 0);
}

/* ---- 0x41a360 ground vehicle update ---------------------------------------- */

static uint8_t byte_at(const es2_guest_memory *m, uint32_t address) {
    return *(const uint8_t *)m->host(m->context, address);
}

uint64_t es2_ground_update_registers(const es2_guest_memory *m, uint32_t a,
                                     const es2_ground_globals *g, const es2_ground_calls *k) {
    void *c = k->context;
    uint32_t config = R32(a + 0x1f2u);
    if (byte_at(m, a + 0xa3u)) {
        W32(g->last_position + 8u, R32(a + 0x2eu));
        W32(g->last_position, R32(a + 0x26u));
        W32(g->last_position + 4u, R32(a + 0x2au));
    }
    (void)k->snapshot(c, a);
    (void)k->animate(c, a);
    uint32_t x = R32(a + 0x26u), y = R32(a + 0x2au), z = R32(a + 0x2eu);
    if (byte_at(m, a + 0xa3u) && R32(g->drift_enabled)) {
        x += R32(g->drift);
        y += R32(g->drift + 4u);
    }
    z = (uint32_t)k->height(c, R32(g->terrain), x, y, z);
    z += (uint32_t)fx_s16(R16(R32(a + 0x1f2u) + 0x16u));
    W32(a + 0x2eu, z);
    W32(a + 0x26u, x);
    W32(a + 0x2au, y);
    uint64_t last = k->collide(c, a);
    if ((uint16_t)last) {
        (void)k->rollback(c, a);
        if (!byte_at(m, a + 0xa3u)) {
            W32(a + 0x26eu, 10000);
            uint16_t recoil = (uint16_t)k->random(c, g->generator) & 1 ? 0xec78u : 0x1388u;
            W16(a + 0x254u, recoil);
            *(uint8_t *)m->host(m->context, a + 0xaeu) = fx_s16(R16(a + 0x28eu)) > 0;
        } else {
            (void)k->sound(c, 0x29);
        }
        W16(a + 0x290u, 0);
        *(uint8_t *)m->host(m->context, a + 0x93u) = 1;
        uint32_t track = R32(a + 0x22cu);
        if (R16(track + 0x50u) & 2) {
            last = k->clip(c, track, R16(R32(a + 0x1f2u) + 0x12u), 0, 0);
        } else {
            uint16_t limit = R16(config + 6u), reverse = R16(config + 0x6eu);
            uint16_t back = (uint16_t)(0u - R16(a + 0x28eu)), speed = limit;
            if (fx_s16(limit) > fx_s16(back)) speed = fx_s16(reverse) < fx_s16(back) ? back : reverse;
            W16(a + 0x28eu, speed);
            W16(a + 0x2a0u, 0u - R16(a + 0x2a0u));
            (void)k->animate(c, a);
            last = k->collide(c, a);
            if ((uint16_t)last) {
                last = k->rollback(c, a);
                W16(a + 0x28eu, 0);
            }
        }
    }
    uint64_t r = k->effects(c, a, (uint32_t)last, (uint32_t)(last >> 32));
    return pair((uint32_t)(r >> 32), ((uint32_t)r & 0xffff0000u) | 1u);
}

/* ---- the clip methods as functions of their own ----------------
 * (main_loop.md, section "the stage-5 flushes and the manager slot methods",
 * "The clip methods"). The method is fixed by its address, not by the clip's
 * vtable; registers as in the table there, ECX included. */
/* Original: DBSIM.EXE 0x478760, 0x47877c, 0x47878c, 0x4787b8, 0x4787e4,
 * 0x478804, 0x478820, 0x47882c */
uint64_t es2_clip_method_registers(const es2_guest_memory *m, uint32_t method, uint32_t clip,
                                   const uint32_t args[3], const uint32_t entry[3], uint32_t *ecx) {
    const uint32_t a = args[0], b = args[1], c = args[2];
    *ecx = entry[2];
    switch (method) {
    case 0x478760u: { /* MOVSX ECX,AX; MOVSX EDX,[C+0Ah]; DEC EDX; JGE; INC EAX */
        const uint32_t last = (uint32_t)fx_s16(R16(clip + 0x0au)) - 1u;
        *ecx = (uint32_t)fx_s16(a);
        return pair(last, fx_s16(a) < (int32_t)last ? a + 1u : a);
    }
    case 0x47877cu: /* TEST AX,AX; DEC EAX */
        return pair(entry[1], (a & 0xffffu) ? a - 1u : a);
    case 0x47878cu: case 0x4787b8u: { /* CMP DX,AX (JLE / JGE); CMP word c,0 */
        const int up = fx_s16(b) > fx_s16(a), down = fx_s16(b) < fx_s16(a);
        if ((method == 0x47878cu ? up : down) || ((b & 0xffffu) == (a & 0xffffu) && !(c & 0xffffu)))
            return pair(b, (a & 0xffff0000u) | 1u);
        return pair(b, 0);
    }
    case 0x4787e4u: { /* MOVSX ECX,DX; MOVSX EAX,[C+0Ah]; DEC EAX; LEA EAX,[EDX+1] */
        const uint32_t last = (uint32_t)fx_s16(R16(clip + 0x0au)) - 1u;
        *ecx = (uint32_t)fx_s16(a);
        return pair(a, fx_s16(a) < (int32_t)last ? a + 1u : 0u);
    }
    case 0x478804u: { /* TEST DX,DX; LEA EAX,[EDX-1] / MOV DX,[C+0Ah]; DEC EDX */
        if (a & 0xffffu) return pair(a, a - 1u);   /* 478812 */
        const uint32_t last = ((a & 0xffff0000u) | R16(clip + 0x0au)) - 1u;
        return pair(last, last);
    }
    case 0x478820u: case 0x47882cu: /* MOV AX,1 */
        return pair(entry[1], (entry[0] & 0xffff0000u) | 1u);
    }
    return pair(entry[1], entry[0]);
}
