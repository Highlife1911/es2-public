#ifndef ES2_WEAPONS_HOOKS_H
#define ES2_WEAPONS_HOOKS_H
#include "guest_native.h"
#include "es2/core/weapons.h"
#include "es2/core/camera.h"
#include "machine_call.h"
/* The call in progress through weapons_machine_call_at: the machine-call
 * depth it runs at (es2_machine_x86_depth during the call, 0 outside a call)
 * and its slot count. Host memory only (the game never sees it). Read by the
 * in-vitro logging stubs through weapons_bridge_slots, so a core call with
 * one argument too few or too many is observable even when the slot above
 * the pushed arguments happens to hold the missing value. */
static unsigned weapons_machine_depth,weapons_machine_count;
/* Shim stack budget (docs/reverse-engineering/main_loop.md, section
 * "shim stack budget"). A gapped entry shim runs the game's code
 * above its guard-probed gap at the original coordinate and its host
 * frames (context, framework dispatch, adapter, core, machine call) below
 * the gap. A shim reached from game code that an outer shim's replacement
 * called starts inside the outer gap, and its own gap may end below the
 * outer gap, where the outer's host frames live (the F12/Alt+P dialog
 * under the logic frame, a modal dialog's window procedure). The floor,
 * es2_machine_x86_floor (es2/hook/machine.h), is the lowest host ESP at
 * which a machine call switched from host frames to a game coordinate and
 * is still suspended (every live host frame lies at or above it), 0 when
 * none is. After its probe loop every gapped shim continues at
 * es2_machine_floor (shim_gap, entry_shim.h): when the floor is set, lies
 * on this thread's committed stack (at or above TIB StackLimit, FS:8) and
 * below the gap's end, the context and all host frames of the inner shim
 * go below it, under every suspended host frame. The ESP only moves down:
 * the gap is never shortened, the game's stack coordinates and the bytes
 * the game reads stay as before, and the skipped pages are committed
 * (above StackLimit). Nothing is stored between the gap's end and the
 * floor: when the probe ends just above the floor, that slot lies in the
 * suspended call's host frame (cockpit_sync's 112 pages under the F12
 * dialog end 4 bytes above the saved EBP of player_input's call). Main
 * thread only, like the shims' pending contexts. */
/* A cdecl (or stdcall, adapter_callee_abi) call of core's callees: EAX, EDX
 * and ECX in and out (radius needs outgoing ECX as well); frame = {EBX, ESI,
 * EDI, EBP} for the callee (NULL: all 0); esp the stack to push on (0: the
 * current one); the planted residue bytes below the callee's entry ESP,
 * which *entry gets (when not NULL; 0 when the call is refused). */
static void weapons_machine_call_at(uint32_t target,const uint32_t *args,unsigned count,es2_weapon_registers *registers,
    const uint32_t *frame,uint32_t esp,const es2_machine_plant *plants,unsigned plant_count,uint32_t *entry) {
    es2_machine m={registers->eax,registers->ecx,registers->edx,frame?frame[0]:0,esp,frame?frame[3]:0,
        frame?frame[1]:0,frame?frame[2]:0};
    unsigned depth=weapons_machine_depth,slots=weapons_machine_count;
    uint32_t at=0;
    weapons_machine_count=count;
#if defined(__i386__) && !defined(ES2_SIM_HOST)
    weapons_machine_depth=es2_machine_x86_depth+1u;
#endif
    if(es2_machine_call_planted(&m,target,adapter_callee_abi(target),args,count,plants,plant_count,&at))at=0;
    weapons_machine_depth=depth;weapons_machine_count=slots;
    registers->eax=m.eax;registers->edx=m.edx;registers->ecx=m.ecx;
    if(entry)*entry=at;
}
static void weapons_machine_call(uint32_t target,const uint32_t *args,unsigned count,es2_weapon_registers *registers) {
    weapons_machine_call_at(target,args,count,registers,NULL,0,NULL,0,NULL);
}
/* For a logging stub whose first argument slot is `args`: the slots the
 * call pushed when the stub was called straight from weapons_machine_call_at
 * (the innermost machine call is that one and pushed its slots at `args`),
 * otherwise `original` (the count the original's call site pushes). A call
 * from a real callee below a machine call has its own, lower slot address. */
__attribute__((unused)) static unsigned weapons_bridge_slots(const uint32_t *args,unsigned original) {
#if defined(__i386__) && !defined(ES2_SIM_HOST)
    if(weapons_machine_depth&&weapons_machine_depth==es2_machine_x86_depth&&
       es2_machine_x86_slots==(uint32_t)(uintptr_t)args)return weapons_machine_count;
#else
    (void)args;
#endif
    return original;
}
static void weapons_guest_call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    (void)c;weapons_machine_call(f,a,n,r);
}
static es2_weapon_context weapons_context(void *image) {
    uint32_t b=(uint32_t)(uintptr_t)image;
    es2_weapon_context g={b+0xa9af8u,b+0xa9afeu,b+0x9288cu,b+0xf881u,b+0x67a48u,
        b+0xe788u,b+0xa9c4u,b+0xb43cu,b+0xa9854u,b+0xa9860u,
        weapons_guest_call,NULL,radius_hit_observe,NULL};return g;
}
static uint64_t __cdecl weapons_projectile_def(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;return es2_projectile_def_registers(math_native_read32(NULL,(uint32_t)(uintptr_t)u+0xa96d0u),*(const BYTE *)es2_guest_span(a[0]+0x41u,1u));
}
static uint64_t __cdecl weapons_bullet_def(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;return es2_bullet_def_registers(math_native_read32(NULL,(uint32_t)(uintptr_t)u+0xa96f8u),*(const BYTE *)es2_guest_span(a[0]+0x41u,1u));
}
static uint64_t __cdecl weapons_mask_merge(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;return es2_mask_merge_registers(&math_native_memory,a[0],a[1]);
}
static uint64_t __cdecl weapons_radius_hit(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_context g=weapons_context(u);return es2_radius_hit_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],a[4],r);
}
static uint64_t __cdecl weapons_weapon_step(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_context g=weapons_context(u);return es2_weapon_step_registers(&math_native_memory,&g,a[0],a[1],r);
}
/* The original fire body reaches preparation at entry ESP-32. Preserve
 * its two otherwise uninitialized matrix bytes across this changed caller:
 * planted at the preparation's entry ESP-33 and -65. */
typedef struct weapons_fire_stack { uint32_t prepare,residue; } weapons_fire_stack;
static void weapons_prepare_stack_call(uint32_t target,const uint32_t *args,es2_weapon_registers *registers,
    uint32_t residue) {
    const es2_machine_plant plants[2]={{33,residue&0xffu},{65,residue>>8&0xffu}};
    weapons_machine_call_at(target,args,3,registers,NULL,0,plants,2,NULL);
}
static void weapons_fire_guest_call(void *context,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    const weapons_fire_stack *stack=context;
    if(f==stack->prepare && n==3)weapons_prepare_stack_call(f,a,r,stack->residue);
    else weapons_machine_call(f,a,n,r);
}
static uint64_t __cdecl weapons_weapon_fire(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_context g=weapons_context(u);weapons_fire_stack stack={g.prepare,r[2]};
    g.call=weapons_fire_guest_call;g.context=&stack;
    return es2_weapon_fire_registers(&math_native_memory,&g,a[0],a[1],a[2],r);
}
static BOOL __cdecl weapons_mask_equal(const es2_hook_comparison *v,void *u) {
    (void)u;
    return timing_state_equal(v) && v->original_result==(uint32_t)(uintptr_t)v->original[1]+8u &&
        v->replacement_result==(uint32_t)(uintptr_t)v->replacement[1]+8u &&
        v->original_edx==(uint32_t)(uintptr_t)v->original[0]+8u &&
        v->replacement_edx==(uint32_t)(uintptr_t)v->replacement[0]+8u;
}
static es2_weapon_graph weapons_graph(void *image,BYTE *scratch,unsigned hook) {
    es2_weapon_graph g={(uint32_t)(uintptr_t)image,(uint32_t)(uintptr_t)scratch,0,0,0,
        weapons_guest_call,NULL,weapons_graph_observe,weapons_graph_context(hook)};return g;
}
#include "building_weapons_hooks.h"
#define WEAPONS_GRAPH_ADAPTER(name) \
static uint64_t __cdecl weapons_##name(const uint32_t *a,void *u,const uint32_t *r) { \
    BYTE scratch[128]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_##name); g.excluded=r[2]; \
    return es2_##name##_registers(&math_native_memory,&g,a[0],r); }
WEAPONS_GRAPH_ADAPTER(bullet_flight)
WEAPONS_GRAPH_ADAPTER(projectile_flight)
WEAPONS_GRAPH_ADAPTER(target_emit)
#undef WEAPONS_GRAPH_ADAPTER
static uint64_t __cdecl weapons_weapon_prepare(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[128]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_weapon_prepare);
    g.padding=(uint8_t)r[2];g.composed_padding=(uint8_t)(r[2]>>8);
    return es2_weapon_prepare_registers(&math_native_memory,&g,a[0],a[1],r);
}
/* Six bytes of observable emitter caller residue require a small explicit
 * capture object. The entry wrapper owns its lifetime across dispatch. */
typedef struct weapons_emitter_residue { uint32_t padding,excluded,stack_top; } weapons_emitter_residue;
typedef struct weapons_emitter_call_context { uint32_t image; weapons_emitter_residue residue; } weapons_emitter_call_context;
/* The emitters' preparation (40e788, 3 slots) and impact (40bf74, 5 slots)
 * calls at the residue record's ESP (stack_top; 0: the current stack).
 * Preparation: the padding bytes at its entry ESP-33 and -65 and the
 * excluded word at entry-90h, the slot that its last callee 47e0b0 (ENTER
 * 0,0) overwrites with 40e788's EBP, entry-4 (own_process.md section 5,
 * "the emitter's excluded word"); the word read back there is the excluded
 * value from then on (a replaced 40e788 leaves the planted word). Impact:
 * the excluded word at its entry ESP-88h, the same address (the emitter
 * calls both from one ESP). Trap: on the current stack that word lies below
 * ESP after the return; it is read before any deeper call. */
static void weapons_round3_stack_call(uint32_t target,const uint32_t *args,unsigned count,es2_weapon_registers *registers,
    weapons_emitter_residue *residue,unsigned impact) {
    const uint32_t x=residue->excluded,base=impact?0x88u:0x90u;
    es2_machine_plant plants[6]={{base,x&0xffu},{base-1u,x>>8&0xffu},{base-2u,x>>16&0xffu},{base-3u,x>>24},
        {33,residue->padding&0xffu},{65,residue->padding>>8&0xffu}};
    uint32_t entry=0;
    weapons_machine_call_at(target,args,count,registers,NULL,residue->stack_top,plants,impact?4u:6u,&entry);
    if(!impact&&entry)residue->excluded=math_native_read32(NULL,entry-0x90u);
}
static void weapons_emitter_guest_call(void *context,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    weapons_emitter_call_context *c=context;
    if(f==c->image+0xe788u && n==3)weapons_round3_stack_call(f,a,n,r,&c->residue,0);
    else if(f==c->image+0xbf74u && n==5)weapons_round3_stack_call(f,a,n,r,&c->residue,1);
    else weapons_machine_call(f,a,n,r);
}
#define WEAPONS_EMITTER_ADAPTER(name) \
static uint64_t __cdecl weapons_##name(const uint32_t *a,void *u,const uint32_t *r) { \
    BYTE scratch[128]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_##name); \
    weapons_emitter_call_context c={(uint32_t)(uintptr_t)u,r[2]?*(const weapons_emitter_residue *)es2_guest_span(r[2],sizeof(weapons_emitter_residue)):(weapons_emitter_residue){0,0,0}}; \
    g.call=weapons_emitter_guest_call;g.context=&c; \
    return es2_##name##_registers(&math_native_memory,&g,a[0],a[1],a[2],r); }
WEAPONS_EMITTER_ADAPTER(emit)
WEAPONS_EMITTER_ADAPTER(alt_emit)
#undef WEAPONS_EMITTER_ADAPTER
/* Runtime setup is a register/EBP ABI, not cdecl: EAX, EDX, ECX and EBP (the
 * frame) in, no stack slot, EAX, EDX and ECX out. The synthetic frame lives
 * throughout all delegated calls; the adapter restores fs:0 on normal exit. */
static void weapons_exception_call(uint32_t target,uint32_t frame,es2_weapon_registers *r) {
    es2_machine m={r->eax,r->ecx,r->edx,0,0,frame,0,0};
    (void)es2_machine_call(&m,target,ES2_MACHINE_CDECL,NULL,0);
    r->eax=m.eax;r->edx=m.edx;r->ecx=m.ecx;
}
typedef struct weapons_impact_context { uint32_t image,frame; } weapons_impact_context;
static void weapons_impact_guest_call(void *context,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    weapons_impact_context *c=context;
    if(f==c->image+0x9630cu)weapons_exception_call(f,c->frame,r);
    else weapons_machine_call(f,a,n,r);
}
static uint64_t __cdecl weapons_impact(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[256]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_impact);
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+208)};
    g.call=weapons_impact_guest_call;g.context=&c;g.excluded=r[2];
    uint64_t result=es2_impact_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],a[4],r);
    uint32_t restored=(uint32_t)result;es2_machine_set_fs0(restored);return result;
}
/* The swept query keeps the same EBP-aware runtime setup; its epilogue
 * restores FS:0 from EDX instead of EAX. */
static uint64_t __cdecl weapons_sweep(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[256]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_sweep);
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+208)};
    g.call=weapons_impact_guest_call;g.context=&c;
    uint64_t result=es2_sweep_registers(&math_native_memory,&g,a[0],r);
    uint32_t restored=(uint32_t)(result>>32);es2_machine_set_fs0(restored);return result;
}
static uint64_t __cdecl weapons_side_scale(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_side_scale);
    return es2_side_scale_registers(&math_native_memory,&g,a[0],a[1],r);
}
static uint64_t __cdecl weapons_cell(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;return es2_cell_registers(&math_native_memory,a[0],a[1],a[2]);
}
static uint64_t __cdecl weapons_segment(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[256]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_segment);
    return es2_segment_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],a[4],a[5],r);
}
static uint64_t __cdecl weapons_sweep_terrain(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[64]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_sweep_terrain);
    return es2_sweep_terrain_registers(&math_native_memory,&g,a[0],a[1],r);
}
static uint64_t __cdecl weapons_slope_test(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;return es2_slope_test_registers(&math_native_memory,a[1],a[2],r);
}
static uint64_t __cdecl weapons_normalize(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_normalize);
    return es2_normalize_registers(&math_native_memory,&g,a[0],a[1],r);
}
static uint64_t __cdecl weapons_diagonal(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;return es2_diagonal_registers(&math_native_memory,a[0],a[1],a[2],a[3],a[4],a[5]);
}
static uint64_t __cdecl weapons_edge_test(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE frame[64]={0};(void)u;(void)r;
    return es2_edge_test_registers(&math_native_memory,(uint32_t)(uintptr_t)frame,a[0],a[1],a[2],a[3],a[4]);
}
/* ECX carries the residue that the entry shim read at the original's EBP-18h. */
static uint64_t __cdecl weapons_face_test(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_face_test);
    return es2_face_test_registers(&math_native_memory,&g,a[0],a[1],a[2],r[2],r);
}
static uint64_t __cdecl weapons_face_cross(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_face_cross);
    return es2_face_cross_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],a[4],r);
}
static uint64_t __cdecl weapons_contact(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE frame[64]={0};es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_contact);
    return es2_contact_registers(&math_native_memory,&g,(uint32_t)(uintptr_t)frame,a[0],a[1],a[2],a[4],a[5],r);
}
static uint64_t __cdecl weapons_terrain_line(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[64]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_terrain_line);
    return es2_terrain_line_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r);
}
static uint64_t __cdecl weapons_terrain_face(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;return es2_terrain_face_registers(&math_native_memory,a[0],a[1]);
}
/* 4011a0 passes pointers into its 1C0h-byte frame to its callees. */
static uint64_t __cdecl weapons_camera_update(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE frame[0x1c0]={0};es2_weapon_graph g=weapons_graph(u,frame,ES2_HOOK_INDEX_camera_update);(void)r;
    return es2_camera_update_registers(&math_native_memory,&g,a[0]);
}
/* Reserve a separate, guard-probed core frame below the original guest
 * frame (32 pages below the saved EBP, then the floor), with the 12-byte
 * residue record {padding bytes at EBP+pad1 (low) and EBP+pad2, the
 * excluded word at EBP+excluded, EBP+top} at its bottom; the replacement
 * runs there with ECX the record and the three argument slots copied.
 * Preparation and impact run at the record's stack_top, the original
 * outgoing-stack address, so even callee-produced stack-pointer residue is
 * preserved bit for bit (weapons_round3_stack_call). */
static int weapons_emitter_entry(es2_machine *m,uint32_t bridge,int pad1,int pad2,int excluded,int top) {
    const uint32_t entry=m->esp,ebp=entry-4u;
    shim_w32(ebp,m->ebp);
    if(es2_machine_probe(bridge,ebp,32u*4096u+12u))return shim_return(m,m,entry,ebp);
    const uint32_t record=es2_machine_floor(ebp-32u*4096u)-12u;
    shim_w32(record,shim_r8(ebp+(uint32_t)pad1)|shim_r8(ebp+(uint32_t)pad2)<<8);
    shim_w32(record+4u,shim_r32(ebp+(uint32_t)excluded));
    shim_w32(record+8u,ebp+(uint32_t)top);
    es2_machine c=*m;c.ecx=record;c.ebp=ebp;c.esp=record;
    const uint32_t args[3]={shim_r32(entry+4u),shim_r32(entry+8u),shim_r32(entry+12u)};
    (void)es2_machine_call(&c,bridge,ES2_MACHINE_CDECL,args,3);
    return shim_return(m,&c,entry,ebp);
}
static int weapons_variant_entry(es2_machine *m,uint32_t bridge) {return weapons_emitter_entry(m,bridge,-69,-101,-180,-20);}
static int weapons_alternate_entry(es2_machine *m,uint32_t bridge) {return weapons_emitter_entry(m,bridge,-57,-89,-168,-8);}
/* The other shims hand the caller's residue on in ECX (or CL and CH): a
 * word or two bytes below the entry ESP. 46ff90's local -18h (entry ESP-1Ch)
 * is read uninitialized for flags 3. */
#define WEAPONS_ECX_ENTRY(name,offset) \
    static int weapons_##name##_entry(es2_machine *m,uint32_t bridge) {(void)bridge;m->ecx=shim_r32(m->esp-offset##u);return 0;}
WEAPONS_ECX_ENTRY(impact,136)
WEAPONS_ECX_ENTRY(bullet,104)
WEAPONS_ECX_ENTRY(projectile,92)
WEAPONS_ECX_ENTRY(emit,20)
WEAPONS_ECX_ENTRY(face,28)
#undef WEAPONS_ECX_ENTRY
#define WEAPONS_CX_ENTRY(name,low,high) \
    static int weapons_##name##_entry(es2_machine *m,uint32_t bridge) { \
        (void)bridge;m->ecx=shim_high8(shim_low8(m->ecx,shim_r8(m->esp-low##u)),shim_r8(m->esp-high##u));return 0;}
WEAPONS_CX_ENTRY(prepare,33,65)
WEAPONS_CX_ENTRY(fire,65,97)
#undef WEAPONS_CX_ENTRY
ES2_SHIM(weapons_variant_shim,0xea58u,weapons_variant_entry,0);
ES2_SHIM(weapons_alternate_shim,0xec64u,weapons_alternate_entry,0);
ES2_SHIM(weapons_impact_shim,0xbf74u,weapons_impact_entry,0);
ES2_SHIM(weapons_bullet_shim,0xb124u,weapons_bullet_entry,0);
ES2_SHIM(weapons_projectile_shim,0xa538u,weapons_projectile_entry,0);
ES2_SHIM(weapons_prepare_shim,0xe788u,weapons_prepare_entry,0);
ES2_SHIM(weapons_fire_shim,0xe964u,weapons_fire_entry,0);
ES2_SHIM(weapons_emit_shim,0x226a0u,weapons_emit_entry,0);
ES2_SHIM(weapons_face_shim,0x6ff90u,weapons_face_entry,0);
static uint64_t __cdecl weapons_vehicle_part(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[128]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_vehicle_part);
    return es2_vehicle_part_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r);
}
static const BYTE weapons_vehicle_part_sig[]={0x55,0x8b,0xec,0x81,0xc4,0xdc,0xfe,0xff,0xff};
static const BYTE weapons_vehicle_part_lens[]={1,2,6};
static const BYTE weapons_variant_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf8};
static const BYTE weapons_alternate_sig[]={0x55,0x8b,0xec,0x53,0x56};
static const BYTE weapons_impact_sig[]={0x55,0x8b,0xec,0x81,0xc4,0x30,0xff,0xff,0xff};
static const es2_hook_span weapons_round3_spans[]={{0,0x31,0x4e}};
static const es2_hook_span weapons_impact_span[]={{1,20,12}};
static const BYTE weapons_emit_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xc4};
static const BYTE weapons_prepare_sig[]={0x55,0x8b,0xec,0x83,0xc4,0x90};
static const BYTE weapons_flight_sig[]={0x55,0x8b,0xec,0x83,0xc4,0x88};
static const BYTE weapons_selector_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08};
static const BYTE weapons_mask_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x45,0x08};
static const BYTE weapons_radius_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf4};
static const BYTE weapons_saved_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57};
static const BYTE weapons_side_scale_sig[]={0x55,0x8b,0xec,0x51,0x53};
static const BYTE weapons_cell_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xec};
static const BYTE weapons_segment_sig[]={0x55,0x8b,0xec,0x81,0xc4,0x58,0xff,0xff,0xff};
static const BYTE weapons_sweep_terrain_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xdc};
static const BYTE weapons_normalize_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf8},weapons_diagonal_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xcc};
static const BYTE weapons_edge_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xc0},weapons_face_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe8};
static const BYTE weapons_cross_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xb0};
static const BYTE weapons_camera_sig[]={0x55,0xa1,0x48,0x80,0x4a,0x00};
static const BYTE weapons_camera_lens[]={1,5};
static const BYTE weapons_frame_lens[]={1,2,3},weapons_mask_lens[]={1,2,1,3},weapons_saved_lens[]={1,2,1,1};
static const es2_hook_span weapons_selector_span[]={{0,0x41,1}};
static const es2_hook_span weapons_mask_spans[]={{0,0,8},{1,0,8}};
static const es2_hook_span weapons_step_span[]={{0,0x30,0x15}};
static const es2_hook_span weapons_fire_spans[]={{0,0x31,0x4c}};
static const es2_hook_region weapons_projectile_region[]={{ES2_REGION_RVA,0,0xa96d0,0,0,4,TRUE,0,{0}}};
static const es2_hook_region weapons_bullet_region[]={{ES2_REGION_RVA,0,0xa96f8,0,0,4,TRUE,0,{0}}};
#define WEAPONS_EXT(name,regions,n,unsafe) static const es2_hook_extension weapons_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,regions,n,weapons_##name,unsafe};
WEAPONS_EXT(projectile_def,weapons_projectile_region,1,FALSE)
WEAPONS_EXT(bullet_def,weapons_bullet_region,1,FALSE)
WEAPONS_EXT(mask_merge,NULL,0,FALSE)
WEAPONS_EXT(radius_hit,NULL,0,TRUE)
WEAPONS_EXT(weapon_step,NULL,0,TRUE)
WEAPONS_EXT(weapon_fire,NULL,0,TRUE)
WEAPONS_EXT(vehicle_part,NULL,0,TRUE)
WEAPONS_EXT(target_emit,NULL,0,TRUE)
WEAPONS_EXT(emit,NULL,0,TRUE)
WEAPONS_EXT(alt_emit,NULL,0,TRUE)
WEAPONS_EXT(impact,NULL,0,TRUE)
WEAPONS_EXT(sweep,NULL,0,TRUE)
WEAPONS_EXT(side_scale,NULL,0,TRUE)
WEAPONS_EXT(cell,NULL,0,FALSE)
WEAPONS_EXT(segment,NULL,0,TRUE)
WEAPONS_EXT(sweep_terrain,NULL,0,TRUE)
WEAPONS_EXT(slope_test,NULL,0,FALSE)
WEAPONS_EXT(normalize,NULL,0,TRUE)
WEAPONS_EXT(diagonal,NULL,0,TRUE)
WEAPONS_EXT(edge_test,NULL,0,TRUE)
WEAPONS_EXT(face_test,NULL,0,TRUE)
WEAPONS_EXT(face_cross,NULL,0,TRUE)
WEAPONS_EXT(contact,NULL,0,TRUE)
WEAPONS_EXT(terrain_line,NULL,0,TRUE)
WEAPONS_EXT(terrain_face,NULL,0,FALSE)
WEAPONS_EXT(camera_update,NULL,0,TRUE)
WEAPONS_EXT(weapon_prepare,NULL,0,TRUE)
WEAPONS_EXT(bullet_flight,NULL,0,TRUE)
WEAPONS_EXT(projectile_flight,NULL,0,TRUE)
#undef WEAPONS_EXT
static const uint32_t weapons_no_registers[3]={0,0,0};
#define WEAPONS_LEGACY(name) static uint32_t __cdecl weapons_##name##32(const uint32_t *a,void *u) {return (uint32_t)weapons_##name(a,u,weapons_no_registers);}
WEAPONS_LEGACY(projectile_def)
WEAPONS_LEGACY(bullet_def)
WEAPONS_LEGACY(mask_merge)
WEAPONS_LEGACY(radius_hit)
WEAPONS_LEGACY(weapon_step)
WEAPONS_LEGACY(weapon_fire)
WEAPONS_LEGACY(vehicle_part)
WEAPONS_LEGACY(target_emit)
WEAPONS_LEGACY(emit)
WEAPONS_LEGACY(alt_emit)
WEAPONS_LEGACY(impact)
WEAPONS_LEGACY(sweep)
WEAPONS_LEGACY(side_scale)
WEAPONS_LEGACY(cell)
WEAPONS_LEGACY(segment)
WEAPONS_LEGACY(sweep_terrain)
WEAPONS_LEGACY(slope_test)
WEAPONS_LEGACY(normalize)
WEAPONS_LEGACY(diagonal)
WEAPONS_LEGACY(edge_test)
WEAPONS_LEGACY(face_test)
WEAPONS_LEGACY(face_cross)
WEAPONS_LEGACY(contact)
WEAPONS_LEGACY(terrain_line)
WEAPONS_LEGACY(terrain_face)
WEAPONS_LEGACY(camera_update)
WEAPONS_LEGACY(weapon_prepare)
WEAPONS_LEGACY(bullet_flight)
WEAPONS_LEGACY(projectile_flight)
#undef WEAPONS_LEGACY
static es2_hook_descriptor weapons_hook_descriptors[]={
    {ES2_HOOK_ID(projectile_def),weapons_selector_sig,6,6,weapons_frame_lens,3,1,weapons_projectile_def32,NULL,NULL,weapons_selector_span,1},
    {ES2_HOOK_ID(bullet_def),weapons_selector_sig,6,6,weapons_frame_lens,3,1,weapons_bullet_def32,NULL,NULL,weapons_selector_span,1},
    {ES2_HOOK_ID(mask_merge),weapons_mask_sig,7,7,weapons_mask_lens,4,2,weapons_mask_merge32,weapons_mask_equal,NULL,weapons_mask_spans,2},
    {ES2_HOOK_ID(radius_hit),weapons_radius_sig,6,6,weapons_frame_lens,3,5,weapons_radius_hit32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(weapon_step),weapons_saved_sig,6,5,weapons_saved_lens,4,2,weapons_weapon_step32,NULL,NULL,weapons_step_span,1},
    {ES2_HOOK_ID(weapon_fire),weapons_saved_sig,6,5,weapons_saved_lens,4,3,weapons_weapon_fire32,NULL,NULL,weapons_fire_spans,1},
    {ES2_HOOK_ID(weapon_prepare),weapons_prepare_sig,6,6,weapons_frame_lens,3,3,weapons_weapon_prepare32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(bullet_flight),weapons_flight_sig,6,6,weapons_frame_lens,3,1,weapons_bullet_flight32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(projectile_flight),weapons_flight_sig,6,6,weapons_frame_lens,3,1,weapons_projectile_flight32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(vehicle_part),weapons_vehicle_part_sig,9,9,weapons_vehicle_part_lens,3,4,weapons_vehicle_part32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(target_emit),weapons_emit_sig,6,6,weapons_frame_lens,3,1,weapons_target_emit32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(emit),weapons_variant_sig,6,6,weapons_frame_lens,3,3,weapons_emit32,NULL,NULL,weapons_round3_spans,1},
    {ES2_HOOK_ID(alt_emit),weapons_alternate_sig,5,5,weapons_saved_lens,4,3,weapons_alt_emit32,NULL,NULL,weapons_round3_spans,1},
    {ES2_HOOK_ID(impact),weapons_impact_sig,9,9,weapons_vehicle_part_lens,3,5,weapons_impact32,NULL,NULL,weapons_impact_span,1},
    {ES2_HOOK_ID(turret),weapons_building_sig,6,6,weapons_building_lens,3,1,weapons_turret32,NULL,NULL,weapons_building_span,1},
    {ES2_HOOK_ID(sweep),weapons_impact_sig,9,9,weapons_vehicle_part_lens,3,1,weapons_sweep32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(side_scale),weapons_side_scale_sig,5,5,weapons_saved_lens,4,2,weapons_side_scale32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(cell),weapons_cell_sig,6,6,weapons_frame_lens,3,3,weapons_cell32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(segment),weapons_segment_sig,9,9,weapons_vehicle_part_lens,3,6,weapons_segment32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(sweep_terrain),weapons_sweep_terrain_sig,6,6,weapons_frame_lens,3,2,weapons_sweep_terrain32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(slope_test),weapons_saved_sig,6,5,weapons_saved_lens,4,3,weapons_slope_test32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(normalize),weapons_normalize_sig,6,6,weapons_frame_lens,3,2,weapons_normalize32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(diagonal),weapons_diagonal_sig,6,6,weapons_frame_lens,3,6,weapons_diagonal32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(edge_test),weapons_edge_sig,6,6,weapons_frame_lens,3,5,weapons_edge_test32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(face_test),weapons_face_sig,6,6,weapons_frame_lens,3,3,weapons_face_test32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(face_cross),weapons_cross_sig,6,6,weapons_frame_lens,3,5,weapons_face_cross32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(contact),weapons_edge_sig,6,6,weapons_frame_lens,3,6,weapons_contact32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(terrain_line),weapons_radius_sig,6,6,weapons_frame_lens,3,4,weapons_terrain_line32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(terrain_face),weapons_face_sig,6,6,weapons_frame_lens,3,2,weapons_terrain_face32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(camera_update),weapons_camera_sig,6,6,weapons_camera_lens,2,1,weapons_camera_update32,NULL,NULL,NULL,0},
};
#endif
