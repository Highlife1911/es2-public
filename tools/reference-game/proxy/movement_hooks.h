#ifndef ES2_MOVEMENT_HOOKS_H
#define ES2_MOVEMENT_HOOKS_H
#include "es2/hook/descriptor.h"
#include "es2/core/movement.h"
#include "es2/core/geometry.h"
#include "guest_native.h"
#include <string.h>
static uint32_t movement_read32(const BYTE *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static uint32_t __cdecl movement_cos(const uint32_t *a,void *image) { return es2_heading_cos((uint16_t)a[0],(BYTE *)image+0xa25c4); }
static uint32_t __cdecl movement_sin(const uint32_t *a,void *image) { return es2_heading_sin((uint16_t)a[0],(BYTE *)image+0xa25c4); }
static uint32_t __cdecl movement_multiply(const uint32_t *a,void *image) { (void)image; return es2_multiply_q8(a[0],a[1]); }
static uint32_t __cdecl movement_step(const uint32_t *a,void *image) {
    const BYTE *p=(BYTE *)image+0xd3b64;
    return es2_movement_step((uint16_t)a[0],(uint16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8)));
}
static uint32_t __cdecl movement_hud(const uint32_t *a,void *image) {
    /* es2_hud_speed reads the actor up to +0x2c0 and the config up to +0x209. */
    (void)image; const BYTE *actor=es2_guest_span(a[0],0x2c1u);
    return es2_hud_speed(actor,es2_guest_span(movement_read32(actor+0x1f2),0x20au));
}
static uint32_t __cdecl movement_transform(const uint32_t *a,void *image) {
    (void)image; const BYTE *matrix=es2_guest_span(a[1],32u); BYTE *out=es2_guest_span(a[2],12u); unsigned mode=matrix[18];
    es2_transform_point(es2_guest_span(a[0],12u),matrix,out);
    return mode==0 || mode>=128 ? movement_read32(out+8) : a[2];
}
static uint32_t __cdecl movement_subtract(const uint32_t *a,void *image) {
    (void)image; es2_vector_subtract(es2_guest_span(a[0],8u),es2_guest_span(a[1],8u)); return a[0];
}
static uint32_t __cdecl movement_distance(const uint32_t *a,void *image) {
    (void)image; return es2_distance2_points(es2_guest_span(a[0],8u),es2_guest_span(a[1],8u));
}
static BOOL movement_pointer_equal(const es2_hook_comparison *v,unsigned output) {
    for(unsigned i=0;i<v->span_count;++i) if(memcmp(v->original[i],v->replacement[i],v->sizes[i])) return FALSE;
    return v->original_result==(uint32_t)(uintptr_t)v->original[output] &&
        v->replacement_result==(uint32_t)(uintptr_t)v->replacement[output];
}
static BOOL __cdecl movement_transform_equal(const es2_hook_comparison *v,void *u) {
    (void)u; unsigned mode=v->original[1][18];
    if(mode>0 && mode<128) return movement_pointer_equal(v,2);
    for(unsigned i=0;i<v->span_count;++i) if(memcmp(v->original[i],v->replacement[i],v->sizes[i])) return FALSE;
    return v->original_result==movement_read32(v->original[2]+8) && v->replacement_result==movement_read32(v->replacement[2]+8);
}
static BOOL __cdecl movement_subtract_equal(const es2_hook_comparison *v,void *u) { (void)u; return movement_pointer_equal(v,0); }
static const BYTE movement_trig_sig[]={0xc8,0,0,0,0x0f,0xbf,0x45,8};
static const BYTE movement_mul_sig[]={0xc8,0,0,0,0x8b,0x45,8};
static const BYTE movement_step_sig[]={0x55,0x8b,0xec,0x53,0x56};
static const BYTE movement_hud_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,8};
static const BYTE movement_transform_sig[]={0xc8,0,0,0,0x56};
static const BYTE movement_subtract_sig[]={0x55,0x8b,0xec,0x8b,0x55,12};
static const BYTE movement_distance_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf8};
static const BYTE movement_trig_lens[]={4,4},movement_mul_lens[]={4,3},movement_step_lens[]={1,2,1,1};
static const BYTE movement_hud_lens[]={1,2,1,3},movement_transform_lens[]={4,1},movement_standard_lens[]={1,2,3};
static const es2_hook_span movement_actor_spans[]={{0,0,0x2c1}};
static const es2_hook_span movement_transform_spans[]={{0,0,12},{1,0,32},{2,0,12}};
static const es2_hook_span movement_xy_spans[]={{0,0,8},{1,0,8}};
static const es2_hook_region movement_table_region[]={ {ES2_REGION_RVA,0,0xa25c4,0,0,2050,TRUE,0,{0}} };
static const es2_hook_region movement_scale_region[]={ {ES2_REGION_RVA,0,0xd3b64,0,0,2,TRUE,0,{0}} };
static const es2_hook_region movement_config_region[]={ {ES2_REGION_ARG_INDIRECT,0,0,0x1f2,0,0x20a,TRUE,0,{0}} };
static const es2_hook_extension movement_table_extension={ES2_ABI_CDECL,ES2_RESULT_EAX,NULL,NULL,movement_table_region,1,NULL,FALSE};
static const es2_hook_extension movement_scale_extension={ES2_ABI_CDECL,ES2_RESULT_EAX,NULL,NULL,movement_scale_region,1,NULL,FALSE};
static const es2_hook_extension movement_config_extension={ES2_ABI_CDECL,ES2_RESULT_EAX,NULL,NULL,movement_config_region,1,NULL,FALSE};
static es2_hook_descriptor movement_hook_descriptors[]={
    {ES2_HOOK_ID(heading_cos),movement_trig_sig,8,8,movement_trig_lens,2,1,movement_cos,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(heading_sin),movement_trig_sig,8,8,movement_trig_lens,2,1,movement_sin,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(multiply_q8),movement_mul_sig,7,7,movement_mul_lens,2,2,movement_multiply,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(movement_step),movement_step_sig,5,5,movement_step_lens,4,1,movement_step,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(hud_speed),movement_hud_sig,7,7,movement_hud_lens,4,1,movement_hud,NULL,NULL,movement_actor_spans,1},
    {ES2_HOOK_ID(transform_point),movement_transform_sig,5,5,movement_transform_lens,2,3,movement_transform,movement_transform_equal,NULL,movement_transform_spans,3},
    {ES2_HOOK_ID(vector_subtract),movement_subtract_sig,6,6,movement_standard_lens,3,2,movement_subtract,movement_subtract_equal,NULL,movement_xy_spans,2},
    {ES2_HOOK_ID(distance_display),movement_distance_sig,6,6,movement_standard_lens,3,2,movement_distance,NULL,NULL,movement_xy_spans,2},
};
#endif
