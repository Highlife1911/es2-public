#include "es2/core/track.h"
#include "es2/core/matrix.h"
#include "fixed.h"
#include <string.h>

static uint32_t host32(const void *p) { return (uint32_t)(uintptr_t)p; }
static uint64_t pair(uint32_t edx, uint32_t eax) { return ((uint64_t)edx << 32) | eax; }

/* Local matrix: every byte the original writes is written below; the rest
 * keeps the uninitialized-stack value. */
static void local_matrix(uint8_t local[32], uint8_t padding) { memset(local, padding, 32); }

/* Translation Z, X, Y (signed words at +4, +0, +2 of `words`) into the local
 * matrix, in the original's store order. */
static void local_translation(uint8_t *local, const uint8_t *words) {
    fx_w32(local + 28, (uint32_t)fx_s16(fx_r16(words + 4)));
    fx_w32(local + 20, (uint32_t)fx_s16(fx_r16(words)));
    fx_w32(local + 24, (uint32_t)fx_s16(fx_r16(words + 2)));
}

/* Transposed rotation with the negated translation rotated into place. */
static void local_inverse(uint8_t *local) {
    uint8_t vector[12];
    es2_matrix_transpose(local);
    fx_w32(local + 28, 0u - fx_r32(local + 28));
    fx_w32(local + 20, 0u - fx_r32(local + 20));
    fx_w32(local + 24, 0u - fx_r32(local + 24));
    memcpy(vector, local + 20, 12);
    (void)es2_matrix_rotate_registers(vector, local, local + 20, 0);
}

/* Bytes 0..19 as five DWORDs, then translation Z, X, Y; sequential reads
 * keep the original's behaviour for overlapping source and destination. */
static void copy_matrix(uint8_t *destination, const uint8_t *source) {
    for (unsigned i = 0; i < 20; i += 4) fx_w32(destination + i, fx_r32(source + i));
    fx_w32(destination + 28, fx_r32(source + 28));
    fx_w32(destination + 20, fx_r32(source + 20));
    fx_w32(destination + 24, fx_r32(source + 24));
}

/* Six pose words scaled by s16(T+0x1c)/s16(T+0x1e): 32-bit product, IDIV
 * truncating toward zero. Angles keep the low word, translations 32 bits. */
static void scaled_local(const uint8_t *t, uint8_t *local, uint8_t angles[6]) {
    int32_t numerator = fx_s16(fx_r16(t + 0x1c)), denominator = fx_s16(fx_r16(t + 0x1e));
    for (unsigned i = 0; i < 6; i += 2)
        fx_w16(angles + i, (uint32_t)(fx_s16(fx_r16(t + 0x28 + i)) * numerator / denominator));
    uint32_t x = (uint32_t)(fx_s16(fx_r16(t + 0x22)) * numerator / denominator);
    uint32_t y = (uint32_t)(fx_s16(fx_r16(t + 0x24)) * numerator / denominator);
    uint32_t z = (uint32_t)(fx_s16(fx_r16(t + 0x26)) * numerator / denominator);
    fx_w32(local + 28, z);
    fx_w32(local + 20, x);
    fx_w32(local + 24, y);
}

uint64_t es2_pose_parent_registers(const void *pose, const void *parent, void *out,
                                   const void *table, uint8_t padding) {
    uint8_t local[32];
    local_matrix(local, padding);
    local_translation(local, (const uint8_t *)pose + 6);
    (void)es2_matrix_euler_registers(pose, local, table, 0);
    uint32_t edx = es2_matrix_compose_registers(local, parent, out, host32(parent));
    return pair(edx, host32(out));
}

static uint64_t track_pose(uint8_t *t, const void *table, uint8_t padding, int inverse) {
    uint8_t local[32], composed[32];
    local_matrix(local, padding);
    local_matrix(composed, padding);
    local_translation(local, t + 0x22);
    (void)es2_matrix_euler_registers(t + 0x28, local, table, 0);
    if (inverse) local_inverse(local);
    (void)es2_matrix_compose_registers(local, t + 0x2e, composed, 0);
    copy_matrix(t + 0x2e, composed);
    return pair(host32(t + 0x2e), host32(t + 0x42));
}

uint64_t es2_track_forward_registers(void *track, const void *table, uint8_t padding,
                                     uint32_t eax, uint32_t edx) {
    uint8_t *t = track;
    if (!fx_r16(t + 0x20)) return pair(edx, eax);
    return track_pose(t, table, padding, 0);
}

uint64_t es2_track_inverse_registers(void *track, const void *table, uint8_t padding,
                                     uint32_t eax, uint32_t edx) {
    uint8_t *t = track;
    if (!fx_r16(t + 0x20)) return pair(edx, eax);
    return track_pose(t, table, padding, 1);
}

uint64_t es2_track_scaled_registers(void *track, void *out, const void *table, uint8_t padding) {
    uint8_t *t = track, *o = out;
    if (!fx_r16(t + 0x20)) {
        for (unsigned i = 0; i < 20; i += 4) fx_w32(o + i, fx_r32(t + 0x2e + i));
        fx_w32(o + 28, fx_r32(t + 0x4a));
        fx_w32(o + 20, fx_r32(t + 0x42));
        uint32_t y = fx_r32(t + 0x46);
        fx_w32(o + 24, y);
        return pair(y, host32(o + 20));
    }
    uint8_t local[32], angles[6];
    local_matrix(local, padding);
    scaled_local(t, local, angles);
    (void)es2_matrix_euler_registers(angles, local, table, 0);
    uint32_t edx = es2_matrix_compose_registers(local, t + 0x2e, o, host32(t + 0x2e));
    return pair(edx, host32(o));
}

uint64_t es2_track_scaled_inverse_registers(void *track, const void *input, const void *table,
                                            uint8_t padding) {
    uint8_t *t = track;
    if (!fx_r16(t + 0x20)) {
        copy_matrix(t + 0x2e, input);
        return pair(host32(t + 0x2e), host32(t + 0x42));
    }
    uint8_t local[32], angles[6];
    local_matrix(local, padding);
    scaled_local(t, local, angles);
    (void)es2_matrix_euler_registers(angles, local, table, 0);
    local_inverse(local);
    uint32_t edx = es2_matrix_compose_registers(local, input, t + 0x2e, host32(input));
    return pair(edx, host32(t + 0x2e));
}
