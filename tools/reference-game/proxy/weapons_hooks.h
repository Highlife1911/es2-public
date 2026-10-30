#ifndef ES2_WEAPONS_HOOKS_H
#define ES2_WEAPONS_HOOKS_H
#include "es2/core/weapons.h"
#include "es2/core/camera.h"
#include "sim_bridge.h"
/* The call in progress through weapons_machine_call: the address of its
 * first argument slot (ESP at the CALL) and the number of slots pushed;
 * the slot address is 0 outside a call. Host memory only (the game never
 * sees it, the guest stack layout is unchanged). Read by the in-vitro
 * logging stubs through weapons_bridge_slots, so a core call with
 * one argument too few or too many is observable even when the slot above
 * the pushed arguments happens to hold the missing value. */
__attribute__((used)) static uint32_t weapons_bridge_args,weapons_bridge_count;
/* Shim stack budget (docs/reverse-engineering/main_loop.md, section
 * "shim stack budget"). A gapped entry shim runs the game's code
 * above its guard-probed gap at the original coordinate and its host
 * frames (context, framework dispatch, adapter, core, bridge) below the
 * gap. A shim reached from game code that an outer shim's replacement
 * called starts inside the outer gap, and its own gap may end below the
 * outer gap, where the outer's host frames live (the F12/Alt+P dialog
 * under the logic frame, a modal dialog's window procedure). es2_shim_floor
 * is the lowest ESP at which a stack bridge switched from host frames to
 * a game coordinate and is still suspended (every live host frame lies at
 * or above it), 0 when none is. The bridges that switch
 * (ai_behaviour_stack_call, weapons_round3_stack_call) save it, lower it
 * to their ESP and restore it on return, so it nests. After its probe
 * loop every gapped shim runs ES2_SHIM_FLOOR: when the floor is set, lies
 * on this thread's committed stack (at or above TIB StackLimit, FS:8) and
 * below ESP, ESP moves down to it, so the context and all host frames of
 * the inner shim lie below every suspended host frame. ESP only moves
 * down: the gap is never shortened, the game's stack coordinates and the
 * bytes the game reads stay as before, and the skipped pages are committed
 * (above StackLimit). No register changes; the flags are clobbered (the
 * probe loops clobber them anyway). The macro writes no stack: EAX, which
 * the StackLimit compare needs, is kept in es2_shim_floor_eax (MOV keeps
 * the flags). A PUSH would store at ESP-4, and when the probe ends just
 * above the floor that slot lies in the suspended bridge's host frame
 * (cockpit_sync's 112 pages under the F12 dialog ended 4 bytes above
 * the saved EBP of player_input's bridge, which the push replaced with EAX).
 * Main thread only, like the shims' pending contexts. */
__attribute__((used)) static uint32_t es2_shim_floor,es2_shim_floor_eax;
#define ES2_SHIM_FLOOR \
    "cmpl $0,_es2_shim_floor\n\tje 8f\n\tcmpl _es2_shim_floor,%esp\n\tjbe 8f\n\t" \
    "movl %eax,_es2_shim_floor_eax\n\tmovl %fs:8,%eax\n\tcmpl %eax,_es2_shim_floor\n\tmovl _es2_shim_floor_eax,%eax\n\tjb 8f\n\t" \
    "movl _es2_shim_floor,%esp\n\t8:\n\t"
/* Own cdecl bridge. Unlike the general pair bridge, radius needs outgoing
 * ECX as well. Preserve nonvolatile registers and forward all scratch bits.
 * No game instructions are embedded; this assembly only marshals the ABI.
 * In es2sim the target passes the bridge seam (sim_bridge.h). */
__attribute__((naked,noinline)) static void __cdecl weapons_machine_call(
    uint32_t target,const uint32_t *args,unsigned count,es2_weapon_registers *registers) {
    __asm__ volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        ES2_SIM_BRIDGE_ARG8
        "movl 12(%ebp),%esi\n\tmovl 16(%ebp),%ebx\n\tmovl 20(%ebp),%edi\n\tmovl %ebx,_weapons_bridge_count\n\t"
        "testl %ebx,%ebx\n\tjz 2f\n\t1: decl %ebx\n\tpushl (%esi,%ebx,4)\n\ttestl %ebx,%ebx\n\tjnz 1b\n\t"
        "2: movl %esp,_weapons_bridge_args\n\tmovl 0(%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tcall *8(%ebp)\n\t"
        "movl $0,_weapons_bridge_args\n\tmovl %eax,0(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* For a logging stub whose first argument slot is `args`: the slots the
 * bridge pushed when the stub was called straight from weapons_machine_call,
 * otherwise `original` (the count the original's call site pushes). A call
 * from a real callee below a bridge call has its own, lower slot address. */
__attribute__((unused)) static unsigned weapons_bridge_slots(const uint32_t *args,unsigned original) {
    return weapons_bridge_args&&weapons_bridge_args==(uint32_t)(uintptr_t)args?weapons_bridge_count:original;
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
    (void)r;return es2_projectile_def_registers(math_native_read32(NULL,(uint32_t)(uintptr_t)u+0xa96d0u),*((BYTE *)(uintptr_t)a[0]+0x41));
}
static uint64_t __cdecl weapons_bullet_def(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;return es2_bullet_def_registers(math_native_read32(NULL,(uint32_t)(uintptr_t)u+0xa96f8u),*((BYTE *)(uintptr_t)a[0]+0x41));
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
 * its two otherwise uninitialized matrix bytes across this changed caller. */
typedef struct weapons_fire_stack { uint32_t prepare,residue; } weapons_fire_stack;
__attribute__((naked,noinline)) static void weapons_prepare_stack_call(
    uint32_t target,const uint32_t *args,es2_weapon_registers *registers,uint32_t residue) {
    __asm__ volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl 12(%ebp),%esi\n\tmovl 16(%ebp),%edi\n\t"
        "pushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 20(%ebp),%ebx\n\tmovb %bl,-37(%esp)\n\tmovb %bh,-69(%esp)\n\t"
        "movl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tcall *8(%ebp)\n\t"
        "movl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
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
        weapons_guest_call,NULL,weapons_graph_observe,(void *)(uintptr_t)hook};return g;
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
__attribute__((naked,noinline)) static void weapons_round3_stack_call(
    uint32_t target,const uint32_t *args,unsigned count,es2_weapon_registers *registers,
    weapons_emitter_residue *residue,unsigned impact) {
    __asm__ volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "pushl _es2_shim_floor\n\tmovl %esp,%esi\n\t"
        "movl _es2_shim_floor,%eax\n\ttestl %eax,%eax\n\tje 6f\n\tcmpl %eax,%esp\n\tjae 7f\n\t6: movl %esp,_es2_shim_floor\n\t7:\n\t"
        "movl 20(%ebp),%edi\n\tmovl 24(%ebp),%ebx\n\t"
        "movl 8(%ebx),%eax\n\ttestl %eax,%eax\n\tjz 5f\n\tmovl %eax,%esp\n\t"
        "5: movl 12(%ebp),%edx\n\tmovl 16(%ebp),%ebx\n\t"
        "1: decl %ebx\n\tpushl (%edx,%ebx,4)\n\ttestl %ebx,%ebx\n\tjnz 1b\n\t"
        "movl 24(%ebp),%ebx\n\tcmpl $0,28(%ebp)\n\tjne 2f\n\t"
        "movl (%ebx),%edx\n\tmovb %dl,-37(%esp)\n\tmovb %dh,-69(%esp)\n\t"
        "movl 4(%ebx),%edx\n\tmovl %edx,-148(%esp)\n\tjmp 3f\n\t"
        "2: movl 4(%ebx),%edx\n\tmovl %edx,-140(%esp)\n\t"
        "3: movl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tcall *8(%ebp)\n\t"
        "movl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "cmpl $0,28(%ebp)\n\tjne 4f\n\tmovl -148(%esp),%eax\n\tmovl %eax,4(%ebx)\n\t"
        "4: movl %esi,%esp\n\tpopl _es2_shim_floor\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
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
    weapons_emitter_call_context c={(uint32_t)(uintptr_t)u,r[2]?*(const weapons_emitter_residue *)(uintptr_t)r[2]:(weapons_emitter_residue){0,0,0}}; \
    g.call=weapons_emitter_guest_call;g.context=&c; \
    return es2_##name##_registers(&math_native_memory,&g,a[0],a[1],a[2],r); }
WEAPONS_EMITTER_ADAPTER(emit)
WEAPONS_EMITTER_ADAPTER(alt_emit)
#undef WEAPONS_EMITTER_ADAPTER
/* Runtime setup is a register/EBP ABI, not cdecl. The synthetic frame lives
 * throughout all delegated calls; the adapter restores fs:0 on normal exit. */
__attribute__((naked,noinline)) static void weapons_exception_call(uint32_t target,uint32_t frame,es2_weapon_registers *r) {
    __asm__ volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        ES2_SIM_BRIDGE_ARG8
        "movl %ebp,%esi\n\tmovl 16(%ebp),%edi\n\tmovl 8(%ebp),%ebx\n\tmovl 12(%ebp),%ebp\n\t"
        "movl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tcall *%ebx\n\t"
        "movl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\tmovl %esi,%ebp\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
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
    uint32_t restored=(uint32_t)result;__asm__ volatile("movl %0,%%fs:0"::"r"(restored):"memory");return result;
}
/* The swept query keeps the same EBP-aware runtime setup; its epilogue
 * restores FS:0 from EDX instead of EAX. */
static uint64_t __cdecl weapons_sweep(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[256]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_sweep);
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+208)};
    g.call=weapons_impact_guest_call;g.context=&c;
    uint64_t result=es2_sweep_registers(&math_native_memory,&g,a[0],r);
    uint32_t restored=(uint32_t)(result>>32);__asm__ volatile("movl %0,%%fs:0"::"r"(restored):"memory");return result;
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
__attribute__((used)) static void *weapons_variant_entry_bridge,*weapons_alternate_entry_bridge,*weapons_impact_entry_bridge;
/* These wrappers move only the framework invocation, preserve incoming EAX
 * and EDX, and copy residue before any helper may consume the caller stack. */
/* Reserve a separate, guard-probed core frame below the original guest
 * frame. Preparation and impact run at the original outgoing-stack address,
 * so even callee-produced stack-pointer residue is preserved bit for bit. */
#define WEAPONS_EMITTER_ENTRY(name,pad1,pad2,excluded,top) \
__attribute__((naked)) static void weapons_##name##_entry(void) { __asm__ volatile( \
    "pushl %ebp\n\tmovl %esp,%ebp\n\tmovl $32,%ecx\n\t1: subl $4096,%esp\n\torl $0,(%esp)\n\tloop 1b\n\t" ES2_SHIM_FLOOR "subl $12,%esp\n\t" \
    "movb " #pad1 "(%ebp),%cl\n\tmovb " #pad2 "(%ebp),%ch\n\tmovl %ecx,(%esp)\n\t" \
    "movl " #excluded "(%ebp),%ecx\n\tmovl %ecx,4(%esp)\n\tleal " #top "(%ebp),%ecx\n\tmovl %ecx,8(%esp)\n\tmovl %esp,%ecx\n\t" \
    "pushl 16(%ebp)\n\tpushl 12(%ebp)\n\tpushl 8(%ebp)\n\tcall *_weapons_" #name "_entry_bridge\n\t" \
    "movl %ebp,%esp\n\tpopl %ebp\n\tret\n\t"); }
WEAPONS_EMITTER_ENTRY(variant,-69,-101,-180,-20)
WEAPONS_EMITTER_ENTRY(alternate,-57,-89,-168,-8)
#undef WEAPONS_EMITTER_ENTRY
__attribute__((naked)) static void weapons_impact_entry(void) {
    __asm__ volatile("movl -136(%esp),%ecx\n\tjmp *_weapons_impact_entry_bridge\n\t");
}
__attribute__((used)) static void *weapons_bullet_entry_bridge,*weapons_projectile_entry_bridge,*weapons_prepare_entry_bridge,*weapons_fire_entry_bridge,*weapons_emit_entry_bridge;
__attribute__((naked)) static void weapons_bullet_entry(void) {
    __asm__ volatile("movl -104(%esp),%ecx\n\tjmp *_weapons_bullet_entry_bridge\n\t");
}
__attribute__((naked)) static void weapons_projectile_entry(void) {
    __asm__ volatile("movl -92(%esp),%ecx\n\tjmp *_weapons_projectile_entry_bridge\n\t");
}
__attribute__((naked)) static void weapons_prepare_entry(void) {
    __asm__ volatile("movb -33(%esp),%cl\n\tmovb -65(%esp),%ch\n\tjmp *_weapons_prepare_entry_bridge\n\t");
}
__attribute__((naked)) static void weapons_fire_entry(void) {
    __asm__ volatile("movb -65(%esp),%cl\n\tmovb -97(%esp),%ch\n\tjmp *_weapons_fire_entry_bridge\n\t");
}
__attribute__((naked)) static void weapons_emit_entry(void) {
    __asm__ volatile("movl -20(%esp),%ecx\n\tjmp *_weapons_emit_entry_bridge\n\t");
}
__attribute__((used)) static void *weapons_face_entry_bridge;
/* 46ff90's local -18h (entry ESP-1Ch) is read uninitialized for flags 3. */
__attribute__((naked)) static void weapons_face_entry(void) {
    __asm__ volatile("movl -28(%esp),%ecx\n\tjmp *_weapons_face_entry_bridge\n\t");
}
/* Called only after the framework has validated hash/signature and installed
 * its own jump. Keep h->patch consistent so normal rollback/uninstall still
 * validates the exact patch. No game instruction is embedded in the shim. */
/* Shims of later adapter groups (mission_hooks.h), defined after this file. */
static BOOL mission_capture_entry(es2_hook *h);
static BOOL weapons_capture_entry(es2_hook *h) {
    if(!h->installed)return TRUE;
    uint32_t rva=h->descriptor->rva;
    void *target;void **bridge;
    switch(rva) {
    case 0x4100u:target=weapons_building_entry;bridge=&weapons_building_entry_bridge;break;
    case 0xea58u:target=weapons_variant_entry;bridge=&weapons_variant_entry_bridge;break;
    case 0xec64u:target=weapons_alternate_entry;bridge=&weapons_alternate_entry_bridge;break;
    case 0xbf74u:target=weapons_impact_entry;bridge=&weapons_impact_entry_bridge;break;
    case 0xb124u:target=weapons_bullet_entry;bridge=&weapons_bullet_entry_bridge;break;
    case 0xa538u:target=weapons_projectile_entry;bridge=&weapons_projectile_entry_bridge;break;
    case 0xe788u:target=weapons_prepare_entry;bridge=&weapons_prepare_entry_bridge;break;
    case 0xe964u:target=weapons_fire_entry;bridge=&weapons_fire_entry_bridge;break;
    case 0x226a0u:target=weapons_emit_entry;bridge=&weapons_emit_entry_bridge;break;
    case 0x6ff90u:target=weapons_face_entry;bridge=&weapons_face_entry_bridge;break;
    default:return mission_capture_entry(h);
    }
    DWORD old,ignored;
    if(memcmp(h->site,h->patch,h->descriptor->relocate_size))return FALSE;
    if(!VirtualProtect(h->site,h->descriptor->relocate_size,PAGE_EXECUTE_READWRITE,&old))return FALSE;
    *bridge=(void *)(uintptr_t)((uint32_t)(uintptr_t)(h->site+5)+math_native_read32(NULL,(uint32_t)(uintptr_t)(h->patch+1)));
    uint32_t displacement=(uint32_t)(uintptr_t)target-(uint32_t)(uintptr_t)(h->site+5);
    math_native_write32(NULL,(uint32_t)(uintptr_t)(h->patch+1),displacement);
    memcpy(h->site,h->patch,h->descriptor->relocate_size);
    BOOL flushed=FlushInstructionCache(GetCurrentProcess(),h->site,h->descriptor->relocate_size);
    BOOL protected=VirtualProtect(h->site,h->descriptor->relocate_size,old,&ignored);
    return flushed && protected;
}
static BOOL weapons_install_hook(es2_hook *h,const es2_hook_descriptor *d,const es2_hook_options *o,const es2_hook_extension *e) {
    if(!es2_hook_install_ex(h,d,o,e))return FALSE;
    if(weapons_capture_entry(h))return TRUE;
    (void)es2_hook_uninstall(h);return FALSE;
}
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
