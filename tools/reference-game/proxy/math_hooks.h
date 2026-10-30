#ifndef ES2_MATH_HOOKS_H
#define ES2_MATH_HOOKS_H
#include "hook_registry_ids.h"
#include "es2/core/matrix.h"
#include "es2/core/pose.h"
#include "es2/core/trig.h"
#include "es2/core/energy.h"
#include "es2/core/track.h"
#include "es2/core/animation.h"
#include "es2/core/terrain.h"
#include "es2/core/collision.h"
#include "es2/core/effects.h"
#include "es2/core/damage.h"
#include "callee_counters.h"
#include "sim_bridge.h"
/* The game function at `rva`, in es2sim through the sim_bridge.h seam. */
#define MATH_GAME_TARGET(image,rva) ((void *)(uintptr_t)ES2_SIM_BRIDGE((uint32_t)(uintptr_t)((BYTE *)(image)+(rva))))
#define MP(i) ((void *)(uintptr_t)a[i])
static uint32_t __cdecl math_euler_matrix(const uint32_t *a,void *u) { es2_matrix_euler(MP(0),MP(1),(BYTE *)u+0xa25c4); return a[1]; }
static uint32_t __cdecl math_matrix_compose(const uint32_t *a,void *u) { (void)u; es2_matrix_compose(MP(0),MP(1),MP(2)); return a[2]; }
static uint32_t __cdecl math_rotate_point(const uint32_t *a,void *u) { (void)u; unsigned mode=((BYTE *)MP(1))[18]; es2_matrix_rotate(MP(0),MP(1),MP(2)); return a[2]+((mode==0||mode>=128)?12:0); }
static uint32_t __cdecl math_matrix_transpose(const uint32_t *a,void *u) { (void)u; es2_matrix_transpose(MP(0)); return a[0]; }
static uint32_t __cdecl math_pose_matrix(const uint32_t *a,void *u) { es2_pose_matrix(MP(0),MP(1),(BYTE *)u+0xa25c4); return a[1]; }
static uint32_t __cdecl math_pose_interpolate(const uint32_t *a,void *u) { (void)u; es2_pose_interpolate(MP(0),MP(1),a[2],MP(3)); return a[3]; }
static uint32_t __cdecl math_scalar_interpolate(const uint32_t *a,void *u) { (void)u; return es2_scalar_interpolate(a[0],a[1],a[2],a[3],a[4]); }
static uint32_t __cdecl math_track_count(const uint32_t *a,void *u) { (void)u; return es2_track_count(MP(0)); }
static uint32_t __cdecl math_inverse_sine(const uint32_t *a,void *u) { return es2_inverse_sine((uint16_t)a[0],(BYTE *)u+0xa05bc,(BYTE *)u+0xa1e3c); }
static uint32_t __cdecl math_angle_ratio(const uint32_t *a,void *u) { return es2_angle_ratio(a[0],a[1],(BYTE *)u+0xa1c34); }
static uint32_t __cdecl math_matrix_angles(const uint32_t *a,void *u) { es2_matrix_angles(MP(0),MP(1),(BYTE *)u+0xa05bc,(BYTE *)u+0xa1e3c,(BYTE *)u+0xa1c34); return a[1]; }
static uint32_t __cdecl math_multiply_q10(const uint32_t *a,void *u) { (void)u; return es2_multiply_q10(a[0],a[1]); }
static uint32_t __cdecl math_approach_word(const uint32_t *a,void *u) { (void)u; return es2_approach_word(MP(0),a[1],a[2]); }
static uint32_t __cdecl math_energy_accumulate(const uint32_t *a,void *u) { (void)u; return es2_energy_accumulate(MP(0),(uint16_t)a[1]); }
/* Inside the original 32-bit process guest addresses are host addresses. */
static uint32_t math_native_read32(void *c,uint32_t x) { (void)c; const BYTE *p=(const BYTE *)(uintptr_t)x; return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static uint16_t math_native_read16(void *c,uint32_t x) { (void)c; const BYTE *p=(const BYTE *)(uintptr_t)x; return (uint16_t)(p[0]|(p[1]<<8)); }
static void math_native_write32(void *c,uint32_t x,uint32_t v) { (void)c; BYTE *p=(BYTE *)(uintptr_t)x; for(unsigned i=0;i<4;++i) p[i]=(BYTE)(v>>(8*i)); }
static void math_native_write16(void *c,uint32_t x,uint16_t v) { (void)c; BYTE *p=(BYTE *)(uintptr_t)x; p[0]=(BYTE)v; p[1]=(BYTE)(v>>8); }
static void *math_native_host(void *c,uint32_t x) { (void)c; return (void *)(uintptr_t)x; }
static const es2_guest_memory math_native_memory={NULL,math_native_read32,math_native_read16,math_native_write32,math_native_write16,math_native_host};
static uint32_t __cdecl math_clip_pose(const uint32_t *a,void *u) { (void)u; return (uint32_t)es2_clip_pose_registers(&math_native_memory,a[0],a[1],a[2]); }
#undef MP
static BOOL __cdecl math_pointer0(const es2_hook_comparison *v,void *u) { (void)u; return v->original_edx==v->replacement_edx && movement_pointer_equal(v,0); }
static BOOL __cdecl math_pointer1(const es2_hook_comparison *v,void *u) { (void)u; return v->original_edx==v->replacement_edx && movement_pointer_equal(v,1); }
static BOOL __cdecl math_pointer2(const es2_hook_comparison *v,void *u) { (void)u; return v->original_edx==v->replacement_edx && movement_pointer_equal(v,2); }
static BOOL __cdecl math_rotate_equal(const es2_hook_comparison *v,void *u) {
    (void)u; unsigned mode=v->original[1][18],delta=(mode==0||mode>=128)?12:0;
    return v->original_edx==v->replacement_edx && timing_state_equal(v) && v->original_result==(uint32_t)(uintptr_t)v->original[2]+delta &&
        v->replacement_result==(uint32_t)(uintptr_t)v->replacement[2]+delta;
}
static BOOL __cdecl math_word_result_equal(const es2_hook_comparison *v,void *u) {
    (void)u;
    if(v->original_edx!=v->replacement_edx) return FALSE;
    if(!timing_state_equal(v) || (uint16_t)v->original_result!=(uint16_t)v->replacement_result) return FALSE;
    if(v->original_result==v->replacement_result) return TRUE;
    return (v->original_result&0xffff0000u)==((uint32_t)(uintptr_t)v->original[0]&0xffff0000u) &&
        (v->replacement_result&0xffff0000u)==((uint32_t)(uintptr_t)v->replacement[0]&0xffff0000u);
}
static const es2_hook_region math_table_region[]={{ES2_REGION_RVA,0,0xa25c4,0,0,2050,TRUE,0,{0}}};
/* All machine-readable asin indices, including non-rotation inputs, fit this
 * single contiguous region. No image bytes are embedded in own code. */
static const es2_hook_region math_inverse_region[]={{ES2_REGION_RVA,0,0xa1c34,0,0,0x298a,TRUE,0,{0}}};
static const es2_hook_region math_ratio_region[]={{ES2_REGION_RVA,0,0xa1c34,0,0,513,TRUE,0,{0}}};
#define P(i) ((void *)(uintptr_t)a[i])
static uint64_t __cdecl math_euler_matrix_registers(const uint32_t *a,void *u,const uint32_t *regs) { (void)u;(void)regs; uint32_t edx=es2_matrix_euler_registers(P(0),P(1),(BYTE *)u+0xa25c4,regs[1]); return ((uint64_t)edx<<32)|a[1]; }
static uint64_t __cdecl math_matrix_compose_registers(const uint32_t *a,void *u,const uint32_t *regs) { (void)u;(void)regs; uint32_t edx=es2_matrix_compose_registers(P(0),P(1),P(2),regs[1]); return ((uint64_t)edx<<32)|a[2]; }
static uint64_t __cdecl math_rotate_point_registers(const uint32_t *a,void *u,const uint32_t *regs) { (void)u;(void)regs; unsigned mode=((BYTE *)P(1))[18]; uint32_t edx=es2_matrix_rotate_registers(P(0),P(1),P(2),regs[1]); return ((uint64_t)edx<<32)|(a[2]+((!mode||mode>=128)?12:0)); }
static uint64_t __cdecl math_matrix_transpose_registers(const uint32_t *a,void *u,const uint32_t *regs) { (void)u;(void)regs; es2_matrix_transpose(P(0));return ((uint64_t)regs[1]<<32)|a[0]; }
static uint64_t __cdecl math_pose_matrix_registers(const uint32_t *a,void *u,const uint32_t *regs) { (void)u;(void)regs; uint32_t edx=es2_pose_matrix_registers(P(0),P(1),(BYTE *)u+0xa25c4);return ((uint64_t)edx<<32)|a[1]; }
static uint64_t __cdecl math_pose_interpolate_registers(const uint32_t *a,void *u,const uint32_t *regs) { (void)u;(void)regs; uint32_t edx=es2_pose_interpolate_registers(P(0),P(1),a[2],P(3));return ((uint64_t)edx<<32)|a[3]; }
static uint64_t __cdecl math_scalar_interpolate_registers(const uint32_t *a,void *u,const uint32_t *regs) { (void)u;(void)regs; return es2_scalar_interpolate_registers(a[0],a[1],a[2],a[3],a[4]); }
static uint64_t __cdecl math_track_count_registers(const uint32_t *a,void *u,const uint32_t *regs) { (void)u;(void)regs; uint32_t n=es2_track_count(P(0));return ((uint64_t)(a[0]+26+4*n)<<32)|n; }
static uint64_t __cdecl math_inverse_sine_registers(const uint32_t *a,void *u,const uint32_t *regs) { (void)u;(void)regs; return ((uint64_t)((a[0]&0x8000)?UINT32_MAX:0)<<32)|es2_inverse_sine((uint16_t)a[0],(BYTE *)u+0xa05bc,(BYTE *)u+0xa1e3c); }
static uint64_t __cdecl math_angle_ratio_registers(const uint32_t *a,void *u,const uint32_t *regs) { (void)u;(void)regs; return es2_angle_ratio_registers(a[0],a[1],(BYTE *)u+0xa1c34); }
static uint64_t __cdecl math_matrix_angles_registers(const uint32_t *a,void *u,const uint32_t *regs) { (void)u;(void)regs; uint32_t edx=es2_matrix_angles_registers(P(0),P(1),(BYTE *)u+0xa05bc,(BYTE *)u+0xa1e3c,(BYTE *)u+0xa1c34);return ((uint64_t)edx<<32)|a[1]; }
static uint64_t __cdecl math_multiply_q10_registers(const uint32_t *a,void *u,const uint32_t *regs) { (void)u;(void)regs; return es2_multiply_q10_registers(a[0],a[1]); }
static uint64_t __cdecl math_approach_word_registers(const uint32_t *a,void *u,const uint32_t *regs) { (void)u;(void)regs; return es2_approach_word_registers(P(0),a[1],a[2]); }
static uint64_t __cdecl math_energy_accumulate_registers(const uint32_t *a,void *u,const uint32_t *regs) { (void)u;(void)regs; return es2_energy_accumulate_registers(P(0),(uint16_t)a[1]); }
static uint64_t __cdecl math_clip_pose_registers(const uint32_t *a,void *u,const uint32_t *regs) { (void)u;(void)regs; return es2_clip_pose_registers(&math_native_memory,a[0],a[1],a[2]); }
/* Padding wrappers: the original leaves byte 19 of a local matrix
 * uninitialized; the replacement writes MATH_PADDING there (see track.h). */
#define MATH_PADDING 0u
#define MATH_QUARTER(u) ((const BYTE *)(u)+0xa25c4)
static uint64_t __cdecl math_pose_parent_registers(const uint32_t *a,void *u,const uint32_t *regs) { (void)regs; return es2_pose_parent_registers(P(0),P(1),P(2),MATH_QUARTER(u),MATH_PADDING); }
static uint64_t __cdecl math_track_forward_registers(const uint32_t *a,void *u,const uint32_t *regs) { return es2_track_forward_registers(P(0),MATH_QUARTER(u),MATH_PADDING,regs[0],regs[1]); }
static uint64_t __cdecl math_track_inverse_registers(const uint32_t *a,void *u,const uint32_t *regs) { return es2_track_inverse_registers(P(0),MATH_QUARTER(u),MATH_PADDING,regs[0],regs[1]); }
static uint64_t __cdecl math_track_scaled_registers(const uint32_t *a,void *u,const uint32_t *regs) { (void)regs; return es2_track_scaled_registers(P(0),P(1),MATH_QUARTER(u),MATH_PADDING); }
static uint64_t __cdecl math_track_scaled_inverse_registers(const uint32_t *a,void *u,const uint32_t *regs) { (void)regs; return es2_track_scaled_inverse_registers(P(0),P(1),MATH_QUARTER(u),MATH_PADDING); }
static uint64_t __cdecl math_manager_reset_registers(const uint32_t *a,void *u,const uint32_t *regs) { return es2_manager_reset_registers(&math_native_memory,a[0],P(1),MATH_QUARTER(u),MATH_PADDING,regs[1]); }
/* The replacement of 0x40250c calls the game's 0x478d38, so the track
 * advance follows its own hook mode (track_advance below). */
typedef void (__cdecl *math_track_advance_fn)(uint32_t,uint32_t);
static void math_advance_original(void *image,uint32_t manager,uint32_t step) { ((math_track_advance_fn)MATH_GAME_TARGET(image,0x78d38))(manager,step); }
/* 0x478d38: clip classes ANSequence (vtable RVA 0xa684c) and
 * ANCyclicSequence (0xa6810) run in core; any other clip class calls its
 * original virtual method. Only the low result word is used. */
typedef uint64_t (__cdecl *math_clip_method1_fn)(uint32_t,uint32_t);
typedef uint64_t (__cdecl *math_clip_method3_fn)(uint32_t,uint32_t,uint32_t,uint32_t);
static uint64_t math_clip_other(void *c,uint32_t clip,uint32_t slot,const uint32_t *args) {
    (void)c; void *method=(void *)(uintptr_t)math_native_read32(NULL,math_native_read32(NULL,clip)+slot);
    if(slot<0x28u) return ((math_clip_method1_fn)method)(clip,args[0]);
    return ((math_clip_method3_fn)method)(clip,args[0],args[1],args[2]);
}
static es2_clip_classes math_clip_classes(void *u) {
    es2_clip_classes k={(uint32_t)(uintptr_t)u+0xa684cu,(uint32_t)(uintptr_t)u+0xa6810u,math_clip_other,NULL}; return k;
}
static uint64_t __cdecl math_track_advance_registers(const uint32_t *a,void *u,const uint32_t *regs) {
    (void)regs; es2_clip_classes k=math_clip_classes(u);
    return es2_track_advance_registers(&math_native_memory,a[0],a[1],&k,MATH_QUARTER(u),MATH_PADDING);
}
/* 0x47967c, 0x4796d8: pure track/resource state plus clip methods. */
static uint64_t __cdecl math_track_target_registers(const uint32_t *a,void *u,const uint32_t *regs) {
    es2_clip_classes k=math_clip_classes(u);
    return es2_track_target_registers(&math_native_memory,&k,a[0],a[1],a[2],a[3],regs[1]);
}
static uint64_t __cdecl math_track_rate_registers(const uint32_t *a,void *u,const uint32_t *regs) {
    (void)regs; es2_clip_classes k=math_clip_classes(u);
    return es2_track_rate_registers(&math_native_memory,&k,a[0],a[1]);
}
/* 0x402604 and 0x418f40 call the game's 0x40250c, 0x4796d8 and 0x402604, so
 * each of these hooks keeps its own mode. */
typedef uint64_t (__cdecl *math_call2_fn)(uint32_t,uint32_t);
typedef uint64_t (__cdecl *math_call3_fn)(uint32_t,uint32_t,uint32_t);
static uint64_t math_animation_step_game(void *image,uint32_t actor,uint32_t manager,uint32_t step) {
    return ((math_call3_fn)MATH_GAME_TARGET(image,0x0250c))(actor,manager,step);
}
static uint64_t math_track_rate_game(void *image,uint32_t track,uint32_t rate) {
    return ((math_call2_fn)MATH_GAME_TARGET(image,0x796d8))(track,rate);
}
static uint64_t math_actor_animate_game(void *image,uint32_t actor,uint32_t step) {
    return ((math_call2_fn)MATH_GAME_TARGET(image,0x02604))(actor,step);
}
/* 0x46e180: read-only terrain grid lookup. */
static uint64_t __cdecl math_terrain_height_registers(const uint32_t *a,void *u,const uint32_t *regs) {
    (void)u;(void)regs; return es2_terrain_height_registers(&math_native_memory,a[0],a[1]);
}
/* 0x402628/0x4027fc: snapshot block at RVA 0xa8cd0 (see animation.h). */
static uint64_t __cdecl math_actor_snapshot_registers(const uint32_t *a,void *u,const uint32_t *regs) {
    (void)regs; return es2_actor_snapshot_registers(&math_native_memory,a[0],(uint32_t)(uintptr_t)u+0xa8cd0u,MATH_QUARTER(u));
}
static uint64_t __cdecl math_actor_rollback_registers(const uint32_t *a,void *u,const uint32_t *regs) {
    (void)regs; return es2_actor_rollback_registers(&math_native_memory,a[0],(uint32_t)(uintptr_t)u+0xa8cd0u,MATH_QUARTER(u));
}
/* 0x41a360: every callee is the game's function (hooked or not). */
typedef uint64_t (__cdecl *math_call1_fn)(uint32_t);
typedef uint64_t (__cdecl *math_call4_fn)(uint32_t,uint32_t,uint32_t,uint32_t);
#define MATH_GAME1(name,rva) static uint64_t name(void *image,uint32_t x) { ground_update_count(rva); return ((math_call1_fn)MATH_GAME_TARGET(image,rva))(x); }
MATH_GAME1(math_snapshot_game,0x02628)
MATH_GAME1(math_ground_animate_game,0x18f40)
MATH_GAME1(math_collide_game,0x18f74)
MATH_GAME1(math_rollback_game,0x027fc)
MATH_GAME1(math_random_game,0x92ee0)
MATH_GAME1(math_sound_game,0x62830)
#undef MATH_GAME1
static uint64_t math_effects_game(void *image,uint32_t actor,uint32_t eax,uint32_t edx) {
    ground_update_count(0x195c8);
    uint32_t registers[3]={eax,edx,actor};BYTE fpu[108];es2_hook_result result;es2_hook_fpu_save(fpu);
    es2_hook_call call={MATH_GAME_TARGET(image,0x195c8),ES2_ABI_CDECL,1,&actor,fpu,&result,registers};
    es2_hook_machine_call(&call);return ((uint64_t)result.edx<<32)|result.eax;
}
static uint64_t math_height_game(void *image,uint32_t terrain,uint32_t x,uint32_t y,uint32_t z) {
    ground_update_count(0x6e180);
    uint32_t point[3]={x,y,z};
    return ((math_call2_fn)MATH_GAME_TARGET(image,0x6e180))(terrain,(uint32_t)(uintptr_t)point);
}
static uint64_t math_clip_game(void *image,uint32_t track,uint32_t clip,uint32_t frame,uint32_t phase) {
    ground_update_count(0x792ac);
    /* 41a4df pushes EAX=T: track_clip retains that high word in V20's frame. */
    uint32_t args[4]={track,clip,frame,phase},registers[3]={track,0,0};
    BYTE fpu[108];es2_hook_result result;es2_hook_fpu_save(fpu);
    es2_hook_call call={MATH_GAME_TARGET(image,0x792ac),ES2_ABI_CDECL,4,args,fpu,&result,registers};
    es2_hook_machine_call(&call);return ((uint64_t)result.edx<<32)|result.eax;
}
static uint64_t __cdecl math_ground_update_registers(const uint32_t *a,void *u,const uint32_t *regs) {
    (void)regs; uint32_t base=(uint32_t)(uintptr_t)u;
    const es2_ground_globals g={base+0xa0be0u,base+0xa9cd0u,base+0xa9ce4u,base+0xa9cd8u,base+0xd2599u};
    const es2_ground_calls k={math_snapshot_game,math_ground_animate_game,math_height_game,math_collide_game,
        math_rollback_game,math_random_game,math_sound_game,math_clip_game,math_effects_game,u};
    return es2_ground_update_registers(&math_native_memory,a[0],&g,&k);
}
/* 0x418f74: virtual methods and game callees through one cdecl caller. */
typedef uint64_t (__cdecl *math_call5_fn)(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
static uint64_t math_guest_call(void *context,uint32_t function,const uint32_t *x,unsigned count) {
    (void)context; void *f=(void *)(uintptr_t)ES2_SIM_BRIDGE(function);   /* es2sim: sim_bridge.h seam */
    switch(count) {
    case 0: return ((uint64_t (__cdecl *)(void))f)();
    case 1: return ((math_call1_fn)f)(x[0]);
    case 2: return ((math_call2_fn)f)(x[0],x[1]);
    case 3: return ((math_call3_fn)f)(x[0],x[1],x[2]);
    case 4: return ((math_call4_fn)f)(x[0],x[1],x[2],x[3]);
    default: return ((math_call5_fn)f)(x[0],x[1],x[2],x[3],x[4]);
    }
}
static es2_collide_globals math_collide_globals(void *u) {
    uint32_t base=(uint32_t)(uintptr_t)u;
    es2_collide_globals g={base+0xa0be0u,base+0xa9af8u,base+0xa9afeu,base+0xa9cd0u,base+0xa9ce4u,base+0xa9cd8u,
        base+0xa9e5cu,base+0x9a058u,base+0x2c56a8u,base+0xd2599u,base+0x92f24u,base+0x04ae4u,base+0x6e498u,
        base+0x34010u,base+0x62830u,MATH_QUARTER(u),ground_collide_observe,u};
    return g;
}
static uint64_t __cdecl math_ground_collide_registers(const uint32_t *a,void *u,const uint32_t *regs) {
    (void)regs; es2_collide_globals g=math_collide_globals(u);
    return es2_ground_collide_registers(&math_native_memory,a[0],&g,math_guest_call,NULL);
}
static uint64_t __cdecl math_track_clip_registers(const uint32_t *a,void *u,const uint32_t *regs) {
    return es2_track_clip_registers(&math_native_memory,a[0],a[1],a[2],a[3],regs[0],
        (uint32_t)(uintptr_t)u+0x78ef4u,math_guest_call,NULL);
}
static uint64_t math_effects_call(void *c,uint32_t function,const uint32_t *args,unsigned count,const uint32_t *registers) {
    if(!registers)return math_guest_call(c,function,args,count);
    BYTE fpu[108];es2_hook_result result;es2_hook_fpu_save(fpu);
    es2_hook_call call={(void *)(uintptr_t)ES2_SIM_BRIDGE(function),ES2_ABI_CDECL,count,args,fpu,&result,registers};
    es2_hook_machine_call(&call);
    return ((uint64_t)result.edx<<32)|result.eax;
}
static es2_effects_context math_effects_context(void *u,uint32_t point) {
    uint32_t base=(uint32_t)(uintptr_t)u;
    es2_effects_context g={base+0xd24eau,base+0x2c56a8u,point,
        base+0x7ebb8u,base+0x8043cu,base+0x7de72u,base+0x628e0u,base+0x34144u,
        math_effects_call,NULL,ground_effects_observe,u};
    return g;
}
static uint64_t __cdecl math_ground_effects_registers(const uint32_t *a,void *u,const uint32_t *regs) {
    BYTE point[12];
    es2_effects_context g=math_effects_context(u,(uint32_t)(uintptr_t)point);
    return es2_ground_effects_registers(&math_native_memory,a[0],&g,regs[0],regs[1]);
}
static uint64_t __cdecl math_actor_animate_registers(const uint32_t *a,void *u,const uint32_t *regs) {
    (void)regs; return es2_actor_animate_registers(&math_native_memory,a[0],a[1],math_animation_step_game,u);
}
static uint64_t __cdecl math_ground_animate_registers(const uint32_t *a,void *u,const uint32_t *regs) {
    const BYTE *scale=(const BYTE *)u+0xd3b64;
    return es2_ground_animate_registers(&math_native_memory,a[0],(uint16_t)(scale[0]|(scale[1]<<8)),regs[0],
        math_track_rate_game,math_actor_animate_game,u);
}
static es2_math_tables math_tables(void *u) {
    es2_math_tables t={MATH_QUARTER(u),(const BYTE *)u+0xa05bc,(const BYTE *)u+0xa1e3c,(const BYTE *)u+0xa1c34}; return t;
}
static uint64_t __cdecl math_animation_step_registers(const uint32_t *a,void *u,const uint32_t *regs) {
    (void)regs; es2_math_tables t=math_tables(u);
    return es2_animation_step_registers(&math_native_memory,a[0],a[1],a[2],&t,math_advance_original,u,MATH_PADDING);
}
static const uint32_t math_no_registers[3]={0,0,0};
static uint32_t __cdecl math_manager_reset(const uint32_t *a,void *u) { return (uint32_t)math_manager_reset_registers(a,u,math_no_registers); }
static uint32_t __cdecl math_animation_step(const uint32_t *a,void *u) { return (uint32_t)math_animation_step_registers(a,u,math_no_registers); }
static uint32_t __cdecl math_track_advance(const uint32_t *a,void *u) { return (uint32_t)math_track_advance_registers(a,u,math_no_registers); }
static uint32_t __cdecl math_track_target(const uint32_t *a,void *u) { return (uint32_t)math_track_target_registers(a,u,math_no_registers); }
static uint32_t __cdecl math_track_rate(const uint32_t *a,void *u) { return (uint32_t)math_track_rate_registers(a,u,math_no_registers); }
static uint32_t __cdecl math_actor_animate(const uint32_t *a,void *u) { return (uint32_t)math_actor_animate_registers(a,u,math_no_registers); }
static uint32_t __cdecl math_terrain_height(const uint32_t *a,void *u) { return (uint32_t)math_terrain_height_registers(a,u,math_no_registers); }
static uint32_t __cdecl math_actor_snapshot(const uint32_t *a,void *u) { return (uint32_t)math_actor_snapshot_registers(a,u,math_no_registers); }
static uint32_t __cdecl math_actor_rollback(const uint32_t *a,void *u) { return (uint32_t)math_actor_rollback_registers(a,u,math_no_registers); }
static uint32_t __cdecl math_ground_update(const uint32_t *a,void *u) { return (uint32_t)math_ground_update_registers(a,u,math_no_registers); }
static uint32_t __cdecl math_ground_collide(const uint32_t *a,void *u) { return (uint32_t)math_ground_collide_registers(a,u,math_no_registers); }
static uint32_t __cdecl math_ground_effects(const uint32_t *a,void *u) { return (uint32_t)math_ground_effects_registers(a,u,math_no_registers); }
static uint32_t __cdecl math_track_clip(const uint32_t *a,void *u) { return (uint32_t)math_track_clip_registers(a,u,math_no_registers); }
static uint32_t __cdecl math_ground_animate(const uint32_t *a,void *u) { return (uint32_t)math_ground_animate_registers(a,u,math_no_registers); }
static uint32_t __cdecl math_pose_parent(const uint32_t *a,void *u) { return (uint32_t)math_pose_parent_registers(a,u,math_no_registers); }
static uint32_t __cdecl math_track_forward(const uint32_t *a,void *u) { return (uint32_t)math_track_forward_registers(a,u,math_no_registers); }
static uint32_t __cdecl math_track_inverse(const uint32_t *a,void *u) { return (uint32_t)math_track_inverse_registers(a,u,math_no_registers); }
static uint32_t __cdecl math_track_scaled(const uint32_t *a,void *u) { return (uint32_t)math_track_scaled_registers(a,u,math_no_registers); }
static uint32_t __cdecl math_track_scaled_inverse(const uint32_t *a,void *u) { return (uint32_t)math_track_scaled_inverse_registers(a,u,math_no_registers); }
#undef P
static BOOL __cdecl math_pose_matrix_equal(const es2_hook_comparison *v,void *u) {
    (void)u;
    if(!movement_pointer_equal(v,1)) return FALSE;
    if(v->original[1][18]) return v->original_edx==v->replacement_edx;
    return v->original_edx==(uint32_t)(uintptr_t)v->original[0] && v->replacement_edx==(uint32_t)(uintptr_t)v->replacement[0];
}
static BOOL __cdecl math_track_equal(const es2_hook_comparison *v,void *u) {
    (void)u;return timing_state_equal(v) && v->original_result==v->replacement_result &&
        v->original_edx==(uint32_t)(uintptr_t)v->original[0]+26+4*v->original_result &&
        v->replacement_edx==(uint32_t)(uintptr_t)v->replacement[0]+26+4*v->replacement_result;
}
static const es2_hook_extension math_euler_matrix_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,math_table_region,1,math_euler_matrix_registers,FALSE};
static const es2_hook_extension math_matrix_compose_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,math_matrix_compose_registers,FALSE};
static const es2_hook_extension math_rotate_point_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,math_rotate_point_registers,FALSE};
static const es2_hook_extension math_matrix_transpose_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,math_matrix_transpose_registers,FALSE};
static const es2_hook_extension math_pose_matrix_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,math_table_region,1,math_pose_matrix_registers,FALSE};
static const es2_hook_extension math_pose_interpolate_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,math_pose_interpolate_registers,FALSE};
static const es2_hook_extension math_scalar_interpolate_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,math_scalar_interpolate_registers,FALSE};
static const es2_hook_extension math_track_count_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,math_track_count_registers,FALSE};
static const es2_hook_extension math_inverse_sine_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,math_inverse_region,1,math_inverse_sine_registers,FALSE};
static const es2_hook_extension math_angle_ratio_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,math_ratio_region,1,math_angle_ratio_registers,FALSE};
static const es2_hook_extension math_matrix_angles_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,math_inverse_region,1,math_matrix_angles_registers,FALSE};
static const es2_hook_extension math_multiply_q10_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,math_multiply_q10_registers,FALSE};
static const es2_hook_extension math_approach_word_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,math_approach_word_registers,FALSE};
static const es2_hook_extension math_energy_accumulate_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,math_energy_accumulate_registers,FALSE};
static const es2_hook_extension math_clip_pose_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,math_clip_pose_registers,FALSE};
static const es2_hook_extension math_pose_parent_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,math_table_region,1,math_pose_parent_registers,FALSE};
static const es2_hook_extension math_track_forward_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,math_table_region,1,math_track_forward_registers,FALSE};
static const es2_hook_extension math_track_inverse_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,math_table_region,1,math_track_inverse_registers,FALSE};
static const es2_hook_extension math_track_scaled_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,math_table_region,1,math_track_scaled_registers,FALSE};
static const es2_hook_extension math_track_scaled_inverse_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,math_table_region,1,math_track_scaled_inverse_registers,FALSE};
/* [M+0x16] animation matrix and [M+0x1a] first track are written in place. */
static const es2_hook_region math_manager_regions[]={
    {ES2_REGION_ARG_INDIRECT,0,0,0x16,0,32,FALSE,0,{0}},
    {ES2_REGION_ARG_INDIRECT,0,0,0x1a,0,0x4e,FALSE,0,{0}}};
static const es2_hook_extension math_manager_reset_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,math_manager_regions,2,math_manager_reset_registers,FALSE};
static const es2_hook_extension math_animation_step_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,math_animation_step_registers,TRUE};
/* Compare mode would advance the tracks twice (variable object graph). */
static const es2_hook_extension math_track_advance_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,math_track_advance_registers,TRUE};
static const es2_hook_extension math_track_target_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,math_track_target_registers,FALSE};
static const es2_hook_extension math_track_rate_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,math_track_rate_registers,FALSE};
/* Both call game functions with effects outside any span. */
static const es2_hook_extension math_actor_animate_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,math_actor_animate_registers,TRUE};
static const es2_hook_extension math_terrain_height_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,math_terrain_height_registers,FALSE};
/* Variable-size copies between the actor graph and a global block. */
static const es2_hook_extension math_actor_snapshot_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,math_actor_snapshot_registers,TRUE};
static const es2_hook_extension math_actor_rollback_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,math_actor_rollback_registers,TRUE};
static const es2_hook_extension math_ground_update_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,math_ground_update_registers,TRUE};
static const es2_hook_extension math_ground_collide_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,math_ground_collide_registers,TRUE};
/* Variable attachment/latch graph and sound/feedback side effects cannot be
 * isolated by fixed spans. Actor cache is described below; compare refused. */
static const es2_hook_region math_effects_regions[]={
    {ES2_REGION_RVA,0,0xd24ea,0,0,4,TRUE,0,{0}},
    {ES2_REGION_RVA,0,0x2c56a8,0,0x14,12,TRUE,0,{0}}};
static const es2_hook_extension math_ground_effects_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,math_effects_regions,2,math_ground_effects_registers,TRUE};
/* Arbitrary clip virtual methods may write outside the track span. */
static const es2_hook_extension math_track_clip_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,math_track_clip_registers,TRUE};
static const es2_hook_extension math_ground_animate_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,math_ground_animate_registers,TRUE};
/* A register equals its counterpart, or both point at the same offset of the
 * same span (real object in the original phase, clone in the replacement). */
static BOOL math_register_equivalent(const es2_hook_comparison *v,unsigned spans,uint32_t o,uint32_t r) {
    if(o==r) return TRUE;
    for(unsigned i=0;i<spans;++i) {
        uint32_t ob=(uint32_t)(uintptr_t)v->original[i],rb=(uint32_t)(uintptr_t)v->replacement[i];
        if(o-ob<=v->sizes[i] && o-ob==r-rb) return TRUE;
    }
    return FALSE;
}
/* All views equal except the one stack-residue byte of the output matrix. */
static BOOL math_padding_equal(const es2_hook_comparison *v,unsigned spans,unsigned span,size_t padding) {
    for(unsigned i=0;i<v->span_count;++i) for(size_t j=0;j<v->sizes[i];++j)
        if(v->original[i][j]!=v->replacement[i][j] && !(i==span && j==padding)) return FALSE;
    return math_register_equivalent(v,spans,v->original_result,v->replacement_result) &&
        math_register_equivalent(v,spans,v->original_edx,v->replacement_edx);
}
static BOOL __cdecl math_pose_parent_equal(const es2_hook_comparison *v,void *u) { (void)u; return math_padding_equal(v,3,2,19); }
static BOOL __cdecl math_track_matrix_equal(const es2_hook_comparison *v,void *u) { (void)u; return math_padding_equal(v,1,0,0x41); }
static BOOL __cdecl math_track_scaled_equal(const es2_hook_comparison *v,void *u) { (void)u; return math_padding_equal(v,2,1,19); }
static BOOL __cdecl math_track_scaled_inverse_equal(const es2_hook_comparison *v,void *u) { (void)u; return math_padding_equal(v,2,0,0x41); }
/* Views: spans M, input; regions matrix (2), first track (3, padding T+0x41). */
/* Track span T+0x00..0x51 compared completely; EAX may be T itself. */
static BOOL __cdecl math_track_state_equal(const es2_hook_comparison *v,void *u) { (void)u; return math_padding_equal(v,1,1,0); }
static BOOL __cdecl math_manager_reset_equal(const es2_hook_comparison *v,void *u) { (void)u; return math_padding_equal(v,2,3,0x41); }
static const BYTE math_euler_matrix_sig[]={0xc8,0,0,0,0x57};
static const BYTE math_euler_matrix_lens[]={4,1};
static const es2_hook_span math_euler_matrix_spans[]={{0,0,6},{1,0,32}};
static const BYTE math_matrix_compose_sig[]={0xc8,0,0,0,0x56};
static const BYTE math_matrix_compose_lens[]={4,1};
static const es2_hook_span math_matrix_compose_spans[]={{0,0,32},{1,0,32},{2,0,32}};
static const BYTE math_rotate_point_sig[]={0xc8,0,0,0,0x56};
static const BYTE math_rotate_point_lens[]={4,1};
static const es2_hook_span math_rotate_point_spans[]={{0,0,12},{1,0,32},{2,0,12}};
static const BYTE math_matrix_transpose_sig[]={0xc8,0,0,0,0x8b,0x45,8};
static const BYTE math_matrix_transpose_lens[]={4,3};
static const es2_hook_span math_matrix_transpose_spans[]={{0,0,32}};
static const BYTE math_pose_matrix_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf4};
static const BYTE math_pose_matrix_lens[]={1,2,3};
static const es2_hook_span math_pose_matrix_spans[]={{0,0,12},{1,0,32}};
static const BYTE math_pose_interpolate_sig[]={0xc8,0,0,0,0x53};
static const BYTE math_pose_interpolate_lens[]={4,1};
static const es2_hook_span math_pose_interpolate_spans[]={{0,0,12},{1,0,12},{3,0,12}};
static const BYTE math_scalar_interpolate_sig[]={0xc8,0,0,0,0x53};
static const BYTE math_scalar_interpolate_lens[]={4,1};
static const BYTE math_track_count_sig[]={0x55,0x8b,0xec,0x8b,0x55,8};
static const BYTE math_track_count_lens[]={1,2,3};
static const es2_hook_span math_track_count_spans[]={{0,0,66}};
static const BYTE math_inverse_sine_sig[]={0xc8,0,0,0,0x0f,0xbf,0x45,8};
static const BYTE math_inverse_sine_lens[]={4,4};
static const BYTE math_angle_ratio_sig[]={0xc8,0,0,0,0x56};
static const BYTE math_angle_ratio_lens[]={4,1};
static const BYTE math_matrix_angles_sig[]={0xc8,0,0,0,0x57};
static const BYTE math_matrix_angles_lens[]={4,1};
static const es2_hook_span math_matrix_angles_spans[]={{0,0,32},{1,0,6}};
static const BYTE math_multiply_q10_sig[]={0xc8,0,0,0,0x8b,0x45,8};
static const BYTE math_multiply_q10_lens[]={4,3};
static const BYTE math_approach_word_sig[]={0x55,0x8b,0xec,0x8b,0x45,8};
static const BYTE math_approach_word_lens[]={1,2,3};
static const es2_hook_span math_approach_word_spans[]={{0,0,2}};
static const BYTE math_energy_accumulate_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf4};
static const BYTE math_energy_accumulate_lens[]={1,2,3};
static const es2_hook_span math_energy_accumulate_spans[]={{0,0,8}};
static const BYTE math_clip_pose_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x55,0x0c};
static const BYTE math_clip_pose_lens[]={1,2,1,1};
/* Only T+0x20..0x2d is written. The resource graph is read through the
 * unchanged pointer at T+0, so both compare phases read the same addresses. */
static const es2_hook_span math_clip_pose_spans[]={{0,0,0x2e}};
static const BYTE math_pose_parent_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xd4,0x8b,0x55,0x08};
static const BYTE math_track_forward_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xb4,0x53,0x56,0x57};
static const BYTE math_track_inverse_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xa8,0x53,0x56,0x57};
static const BYTE math_track_scaled_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xcc,0x53,0x56,0x57};
static const BYTE math_track_scaled_inverse_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xc0,0x53,0x56,0x57};
static const BYTE math_frame_lens[]={1,2,3};
/* Track span T+0x00..0x4d: enable/scale/pose words read, matrix T+0x2e written. */
static const es2_hook_span math_pose_parent_spans[]={{0,0,12},{1,0,32},{2,0,32}};
static const es2_hook_span math_track_spans[]={{0,0,0x4e}};
static const es2_hook_span math_track_pair_spans[]={{0,0,0x4e},{1,0,32}};
static const BYTE math_manager_reset_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x75,0x0c};
static const BYTE math_manager_reset_lens[]={1,2,1,1};
static const es2_hook_span math_manager_reset_spans[]={{0,0,0x1e},{1,0,32}};
static const BYTE math_animation_step_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xc8,0x53,0x56,0x57};
static const BYTE math_track_advance_sig[]={0x55,0x8b,0xec,0x53,0x66,0x8b,0x45,0x0c};
static const BYTE math_track_advance_lens[]={1,2,1,4};
static const BYTE math_track_target_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x66,0x8b,0x55,0x0c};
static const BYTE math_track_rate_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8b,0x55,0x0c};
static const BYTE math_actor_animate_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8d,0x50,0x38};
static const BYTE math_ground_animate_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08};
static const BYTE math_ground_animate_lens[]={1,2,1,3};
static const BYTE math_actor_copy_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf0,0x53,0x56,0x57};
static const BYTE math_ground_update_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe8,0x53,0x56,0x57};
static const BYTE math_ground_collide_sig[]={0x55,0x8b,0xec,0x81,0xc4,0x38,0xff,0xff,0xff};
static const BYTE math_ground_collide_lens[]={1,2,6};
static const BYTE math_ground_effects_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xa0,0x53,0x56,0x57};
static const es2_hook_span math_ground_effects_spans[]={{0,0x12,0x22}};
static const BYTE math_terrain_height_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xb0,0x53,0x56,0x57};
/* Track span: clip/frame/target/transition words, rate T+0x4e, state T+0x50. */
static const es2_hook_span math_track_state_spans[]={{0,0,0x52}};
/* track_advance's subtree as standalone hooks: 0x4789d0 sort, 0x479720 one
 * track step, 0x478aac all tracks + sort + first track transform, 0x478c64
 * node matrices (also called by the manager constructor 0x478838). Callees
 * run in core, as inside track_advance; frame steps of unknown clip classes
 * call their original method. */
static uint64_t __cdecl math_track_sort_registers(const uint32_t *a,void *u,const uint32_t *regs) {
    (void)u;(void)regs; return es2_track_sort_registers(&math_native_memory,a[0]);
}
/* Entry EDX is the result of a stopped track. */
static uint64_t __cdecl math_track_step_registers(const uint32_t *a,void *u,const uint32_t *regs) {
    es2_clip_classes k=math_clip_classes(u);
    return es2_track_step_registers(&math_native_memory,&k,a[0],a[1],regs[1],MATH_QUARTER(u),MATH_PADDING);
}
static uint64_t __cdecl math_track_list_registers(const uint32_t *a,void *u,const uint32_t *regs) {
    (void)regs; es2_clip_classes k=math_clip_classes(u);
    return es2_track_list_registers(&math_native_memory,a[0],a[1],&k,MATH_QUARTER(u),MATH_PADDING);
}
static uint64_t __cdecl math_node_matrices_registers(const uint32_t *a,void *u,const uint32_t *regs) {
    (void)regs; return es2_node_matrices_registers(&math_native_memory,a[0],MATH_QUARTER(u),MATH_PADDING);
}
static uint32_t __cdecl math_track_sort(const uint32_t *a,void *u) { return (uint32_t)math_track_sort_registers(a,u,math_no_registers); }
static uint32_t __cdecl math_track_step(const uint32_t *a,void *u) { return (uint32_t)math_track_step_registers(a,u,math_no_registers); }
static uint32_t __cdecl math_track_list(const uint32_t *a,void *u) { return (uint32_t)math_track_list_registers(a,u,math_no_registers); }
static uint32_t __cdecl math_node_matrices(const uint32_t *a,void *u) { return (uint32_t)math_node_matrices_registers(a,u,math_no_registers); }
/* The sort writes only the pointer list M+0x1a..0x41 and reads the tracks
 * and clips through it (unchanged in both phases); EDX may point into the
 * list (fewer than two tracks), so math_track_state_equal remaps it. */
static const es2_hook_extension math_track_sort_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,math_track_sort_registers,FALSE};
/* Results combine high words of the track's own matrix/pose addresses with
 * field words (no clone remapping), and unknown clip classes may write
 * anywhere: compare refused, as for track_clip. */
static const es2_hook_extension math_track_step_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,math_track_step_registers,TRUE};
/* Variable track list, node matrices and flags (runtime-indexed, variable
 * size): compare would need graph cloning, as for track_advance. */
static const es2_hook_extension math_track_list_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,math_track_list_registers,TRUE};
static const es2_hook_extension math_node_matrices_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,math_node_matrices_registers,TRUE};
static const BYTE math_track_sort_sig[]={0x55,0x8b,0xec,0x51,0x53,0x56,0x57,0x8b,0x45,0x08};
static const BYTE math_track_step_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x45,0x0c};
static const BYTE math_track_list_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x7d,0x08};
static const BYTE math_node_matrices_sig[]={0x55,0x8b,0xec,0x51,0x53,0x56,0x57,0x8b,0x75,0x08};
static const BYTE math_saved_lens[]={1,2,1,1};
static const es2_hook_span math_track_sort_spans[]={{0,0,0x42}};
static es2_hook_descriptor math_hook_descriptors[]={
    {ES2_HOOK_ID(euler_matrix),math_euler_matrix_sig,5,5,math_euler_matrix_lens,2,2,math_euler_matrix,math_pointer1,NULL,math_euler_matrix_spans,2},
    {ES2_HOOK_ID(matrix_compose),math_matrix_compose_sig,5,5,math_matrix_compose_lens,2,3,math_matrix_compose,math_pointer2,NULL,math_matrix_compose_spans,3},
    {ES2_HOOK_ID(rotate_point),math_rotate_point_sig,5,5,math_rotate_point_lens,2,3,math_rotate_point,math_rotate_equal,NULL,math_rotate_point_spans,3},
    {ES2_HOOK_ID(matrix_transpose),math_matrix_transpose_sig,7,7,math_matrix_transpose_lens,2,1,math_matrix_transpose,math_pointer0,NULL,math_matrix_transpose_spans,1},
    {ES2_HOOK_ID(pose_matrix),math_pose_matrix_sig,6,6,math_pose_matrix_lens,3,2,math_pose_matrix,math_pose_matrix_equal,NULL,math_pose_matrix_spans,2},
    {ES2_HOOK_ID(pose_interpolate),math_pose_interpolate_sig,5,5,math_pose_interpolate_lens,2,4,math_pose_interpolate,math_pointer2,NULL,math_pose_interpolate_spans,3},
    {ES2_HOOK_ID(scalar_interpolate),math_scalar_interpolate_sig,5,5,math_scalar_interpolate_lens,2,5,math_scalar_interpolate,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(track_count),math_track_count_sig,6,6,math_track_count_lens,3,1,math_track_count,math_track_equal,NULL,math_track_count_spans,1},
    {ES2_HOOK_ID(inverse_sine),math_inverse_sine_sig,8,8,math_inverse_sine_lens,2,1,math_inverse_sine,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(angle_ratio),math_angle_ratio_sig,5,5,math_angle_ratio_lens,2,2,math_angle_ratio,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(matrix_angles),math_matrix_angles_sig,5,5,math_matrix_angles_lens,2,2,math_matrix_angles,math_pointer1,NULL,math_matrix_angles_spans,2},
    {ES2_HOOK_ID(multiply_q10),math_multiply_q10_sig,7,7,math_multiply_q10_lens,2,2,math_multiply_q10,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(approach_word),math_approach_word_sig,6,6,math_approach_word_lens,3,3,math_approach_word,math_word_result_equal,NULL,math_approach_word_spans,1},
    {ES2_HOOK_ID(energy_accumulate),math_energy_accumulate_sig,6,6,math_energy_accumulate_lens,3,2,math_energy_accumulate,math_word_result_equal,NULL,math_energy_accumulate_spans,1},
    {ES2_HOOK_ID(clip_pose),math_clip_pose_sig,8,5,math_clip_pose_lens,4,3,math_clip_pose,math_pointer0,NULL,math_clip_pose_spans,1},
    {ES2_HOOK_ID(pose_parent),math_pose_parent_sig,9,6,math_frame_lens,3,3,math_pose_parent,math_pose_parent_equal,NULL,math_pose_parent_spans,3},
    {ES2_HOOK_ID(track_forward),math_track_forward_sig,9,6,math_frame_lens,3,1,math_track_forward,math_track_matrix_equal,NULL,math_track_spans,1},
    {ES2_HOOK_ID(track_inverse),math_track_inverse_sig,9,6,math_frame_lens,3,1,math_track_inverse,math_track_matrix_equal,NULL,math_track_spans,1},
    {ES2_HOOK_ID(track_scaled),math_track_scaled_sig,9,6,math_frame_lens,3,2,math_track_scaled,math_track_scaled_equal,NULL,math_track_pair_spans,2},
    {ES2_HOOK_ID(track_scaled_inverse),math_track_scaled_inverse_sig,9,6,math_frame_lens,3,2,math_track_scaled_inverse,math_track_scaled_inverse_equal,NULL,math_track_pair_spans,2},
    {ES2_HOOK_ID(manager_reset),math_manager_reset_sig,9,5,math_manager_reset_lens,4,2,math_manager_reset,math_manager_reset_equal,NULL,math_manager_reset_spans,2},
    {ES2_HOOK_ID(animation_step),math_animation_step_sig,9,6,math_frame_lens,3,3,math_animation_step,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(track_advance),math_track_advance_sig,8,8,math_track_advance_lens,4,2,math_track_advance,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(track_target),math_track_target_sig,10,6,math_frame_lens,3,4,math_track_target,math_track_state_equal,NULL,math_track_state_spans,1},
    {ES2_HOOK_ID(track_rate),math_track_rate_sig,9,6,math_frame_lens,3,2,math_track_rate,math_track_state_equal,NULL,math_track_state_spans,1},
    {ES2_HOOK_ID(actor_animate),math_actor_animate_sig,9,6,math_frame_lens,3,2,math_actor_animate,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(ground_animate),math_ground_animate_sig,7,7,math_ground_animate_lens,4,1,math_ground_animate,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(terrain_height),math_terrain_height_sig,9,6,math_frame_lens,3,2,math_terrain_height,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(actor_snapshot),math_actor_copy_sig,9,6,math_frame_lens,3,1,math_actor_snapshot,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(actor_rollback),math_actor_copy_sig,9,6,math_frame_lens,3,1,math_actor_rollback,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(ground_update),math_ground_update_sig,9,6,math_frame_lens,3,1,math_ground_update,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(ground_collide),math_ground_collide_sig,9,9,math_ground_collide_lens,3,1,math_ground_collide,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(ground_effects),math_ground_effects_sig,9,6,math_frame_lens,3,1,math_ground_effects,NULL,NULL,math_ground_effects_spans,1},
    {ES2_HOOK_ID(track_clip),math_ground_animate_sig,7,7,math_ground_animate_lens,4,4,math_track_clip,NULL,NULL,math_track_state_spans,1},
    {ES2_HOOK_ID(track_sort),math_track_sort_sig,10,5,math_saved_lens,4,1,math_track_sort,math_track_state_equal,NULL,math_track_sort_spans,1},
    {ES2_HOOK_ID(track_step),math_track_step_sig,9,5,math_saved_lens,4,2,math_track_step,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(track_list),math_track_list_sig,9,5,math_saved_lens,4,2,math_track_list,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(node_matrices),math_node_matrices_sig,10,5,math_saved_lens,4,1,math_node_matrices,NULL,NULL,NULL,0},
};
#endif
