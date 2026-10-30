#ifndef ES2_TIMING_HOOKS_H
#define ES2_TIMING_HOOKS_H
#include <string.h>
#include "es2/hook/descriptor.h"
#include "es2/core/timing.h"
#include "guest_native.h"

static uint16_t timing_scale_word(const void *image) {
    const BYTE *p = (const BYTE *)image + 0xd3b64;
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t __cdecl timing_scale(const uint32_t *a, void *user) {
    (void)user;
    return es2_time_scale((uint16_t)a[0], (uint16_t)a[1]);
}
static uint32_t __cdecl timing_word(const uint32_t *a, void *user) {
    /* Original MOV AX,[EAX+1] retains the object's address in EAX[31:16]. */
    return (a[0] & UINT32_C(0xffff0000)) |
        es2_timer_word_step(es2_guest_span(a[0], 3u), timing_scale_word(user));
}
static uint32_t __cdecl timing_dword(const uint32_t *a, void *user) {
    return es2_timer_dword_step(es2_guest_span(a[0], 5u), timing_scale_word(user));
}
static uint32_t __cdecl timing_arm(const uint32_t *a, void *user) {
    (void)user;
    es2_timer_arm(es2_guest_span(a[0], 5u), (uint16_t)a[1]);
    return a[0];
}

static BOOL timing_state_equal(const es2_hook_comparison *v) {
    for (unsigned i = 0; i < v->span_count; ++i)
        if (memcmp(v->original[i], v->replacement[i], v->sizes[i])) return FALSE;
    return TRUE;
}
static BOOL __cdecl timing_word_equal(const es2_hook_comparison *v, void *user) {
    (void)user;
    /* Pointer-derived high word differs because compare remaps the object.
     * Validate it against each actual object address, rather than ignore it. */
    uint32_t original_base = (uint32_t)(uintptr_t)(v->original[0] - 1);
    uint32_t private_base = (uint32_t)(uintptr_t)(v->replacement[0] - 1);
    return timing_state_equal(v) &&
        (uint16_t)v->original_result == (uint16_t)v->replacement_result &&
        (v->original_result & 0xffff0000u) == (original_base & 0xffff0000u) &&
        (v->replacement_result & 0xffff0000u) == (private_base & 0xffff0000u);
}
static BOOL __cdecl timing_arm_equal(const es2_hook_comparison *v, void *user) {
    (void)user;
    return timing_state_equal(v) &&
        v->original_result == (uint32_t)(uintptr_t)(v->original[0] - 1) &&
        v->replacement_result == (uint32_t)(uintptr_t)(v->replacement[0] - 1);
}
static const BYTE timing_scale_sig[] = {0x55,0x8b,0xec,0x0f,0xbf,0x45,0x0c};
static const BYTE timing_object_sig[] = {0x55,0x8b,0xec,0x8b,0x45,0x08};
static const BYTE timing_scale_lengths[] = {1,2,4};
static const BYTE timing_object_lengths[] = {1,2,3};
static const es2_hook_span timing_word_spans[] = {{0,1,2}};
static const es2_hook_span timing_dword_spans[] = {{0,1,4}};
/* Mutable only during quiescent installation: user becomes loaded image base.
 * No candidate writes globals. Only the two step functions read RVA 0xd3b64. */
static es2_hook_descriptor timing_hook_descriptors[] = {
    {ES2_HOOK_ID(time_scale),timing_scale_sig,sizeof(timing_scale_sig),7,
     timing_scale_lengths,3,2,timing_scale,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(timer_word_step),timing_object_sig,sizeof(timing_object_sig),6,
     timing_object_lengths,3,1,timing_word,timing_word_equal,NULL,timing_word_spans,1},
    {ES2_HOOK_ID(timer_dword_step),timing_object_sig,sizeof(timing_object_sig),6,
     timing_object_lengths,3,1,timing_dword,NULL,NULL,timing_dword_spans,1},
    {ES2_HOOK_ID(timer_arm),timing_object_sig,sizeof(timing_object_sig),6,
     timing_object_lengths,3,2,timing_arm,timing_arm_equal,NULL,timing_dword_spans,1},
};
#define TIMING_HOOK_COUNT 4u
#endif
