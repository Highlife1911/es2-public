#ifndef ES2_CONTROL_RECORD_HOOKS_H
#define ES2_CONTROL_RECORD_HOOKS_H
/* control_record group (docs/reverse-engineering/input.md, section
 * "control record 45a810"): control_record 45a810, row 2 of the
 * logic frame 45f570 and the input step of the modal screen loops. The
 * record construction is own code; the message pump 464c78, the device
 * poll 45baa8 and every other callee (key read, joystick query, keyboard
 * axes, command handler, event list, the tape helpers and the runtime's
 * memset/fopen/fread/fwrite/fclose thunks) stay original and go through
 * the weapons guest bridge and callee observer with the original's entry
 * EAX/EDX/ECX, so nested hooks keep their modes. The original's 0C8h-byte
 * frame lives in the adapter's scratch (callees receive addresses of its
 * key word, tape buffers and recorded entry). Refuses compare mode.
 *
 * The original's ECX at RET reaches the modal loops' screen input virtual
 * (+10h) at entry, so an entry shim (chained from
 * ai_behaviour2_capture_entry; the end of the shim chain) calls the
 * framework's dispatch and, if the replacement ran, loads the ECX the
 * core computed. No game instruction is embedded. */
#include "es2/core/control_record.h"
/* Set by the adapter, consumed by the shim; a nested invocation (a modal
 * screen inside the pump) sets and consumes its own value first. */
__attribute__((used)) static uint32_t control_record_ecx,control_record_ecx_valid;
static uint64_t __cdecl control_record_control_record(const uint32_t *a,void *u,const uint32_t *r) {
    (void)a;BYTE frame[ES2_CONTROL_RECORD_SCRATCH]={0};uint32_t ecx;
    es2_weapon_graph g=weapons_graph(u,frame,ES2_HOOK_INDEX_control_record);
    uint64_t v=es2_control_record_registers(&math_native_memory,&g,&ecx,r);
    control_record_ecx=ecx;control_record_ecx_valid=1;
    return v;
}
/* Callee counters (hk_control_record_<suffix>), incremented only while
 * the hook is non-original. Keys: the callee VA. */
static callee_counter control_record_counters[]={
    {"call_464c78",ES2_COLLIDE_CALL,0x464c78,0},
    {"call_49681a",ES2_COLLIDE_CALL,0x49681a,0},
    {"call_45baa8",ES2_COLLIDE_CALL,0x45baa8,0},
    {"call_45bbc4",ES2_COLLIDE_CALL,0x45bbc4,0},
    {"call_477904",ES2_COLLIDE_CALL,0x477904,0},
    {"call_49684a",ES2_COLLIDE_CALL,0x49684a,0},
    {"call_486ece",ES2_COLLIDE_CALL,0x486ece,0},
    {"call_45fe6c",ES2_COLLIDE_CALL,0x45fe6c,0},
    {"call_496838",ES2_COLLIDE_CALL,0x496838,0},
    {"call_453078",ES2_COLLIDE_CALL,0x453078,0},
    {"call_45c524",ES2_COLLIDE_CALL,0x45c524,0},
    {"call_45a4cc",ES2_COLLIDE_CALL,0x45a4cc,0},
    {"call_45feb8",ES2_COLLIDE_CALL,0x45feb8,0},
    {"call_452d40",ES2_COLLIDE_CALL,0x452d40,0},
    {"call_492b90",ES2_COLLIDE_CALL,0x492b90,0},
    {"call_496802",ES2_COLLIDE_CALL,0x496802,0},
    {"call_479c78",ES2_COLLIDE_CALL,0x479c78,0},
    {"call_496832",ES2_COLLIDE_CALL,0x496832,0},
};
static callee_counter *control_record_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_control_record) { *count=sizeof(control_record_counters)/sizeof(control_record_counters[0]);return control_record_counters; }
    *count=0;return NULL;
}
/* push ebx; push esi; push edi; push ebp; add esp,-0C8h; mov ebx,4d22c6h */
static const BYTE control_record_sig[]={0x53,0x56,0x57,0x55,0x81,0xc4,0x38,0xff,0xff,0xff,0xbb,0xc6,0x22,0x4d,0x00};
static const BYTE control_record_lens[]={1,1,1,1,6};
static const es2_hook_extension control_record_control_record_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,control_record_control_record,TRUE};
static uint32_t __cdecl control_record_control_record32(const uint32_t *a,void *u) {return (uint32_t)control_record_control_record(a,u,weapons_no_registers);}
static es2_hook_descriptor control_record_hook_descriptors[]={
    {ES2_HOOK_ID(control_record),control_record_sig,15,10,control_record_lens,5,0,control_record_control_record32,NULL,NULL,NULL,0},
};
/* The entry shim: the framework's dispatch as a call (no arguments), then
 * the core's ECX if the replacement ran; in original mode the original's
 * own ECX stays. EAX and EDX pass through. */
__attribute__((used)) static void *control_record_bridge;
__attribute__((naked)) static void control_record_entry(void) {
    __asm__ volatile(
        "movl $0,_control_record_ecx_valid\n\tcall *_control_record_bridge\n\t"
        "cmpl $0,_control_record_ecx_valid\n\tje 1f\n\t"
        "movl _control_record_ecx,%ecx\n\tmovl $0,_control_record_ecx_valid\n\t"
        "1: ret\n\t");
}
/* Called by ai_behaviour2_capture_entry after the framework installed its
 * jump: redirect it to the shim and keep h->patch consistent, so uninstall
 * still validates the exact patch. The chain continues in
 * actor_step_capture_entry (actor_step_hooks.h). */
static BOOL actor_step_capture_entry(es2_hook *h);
static BOOL control_record_capture_entry(es2_hook *h) {
    if(h->descriptor->rva!=0x5a810u)return actor_step_capture_entry(h);
    DWORD old,ignored;
    if(memcmp(h->site,h->patch,h->descriptor->relocate_size))return FALSE;
    if(!VirtualProtect(h->site,h->descriptor->relocate_size,PAGE_EXECUTE_READWRITE,&old))return FALSE;
    control_record_bridge=(void *)(uintptr_t)((uint32_t)(uintptr_t)(h->site+5)+math_native_read32(NULL,(uint32_t)(uintptr_t)(h->patch+1)));
    math_native_write32(NULL,(uint32_t)(uintptr_t)(h->patch+1),(uint32_t)(uintptr_t)control_record_entry-(uint32_t)(uintptr_t)(h->site+5));
    memcpy(h->site,h->patch,h->descriptor->relocate_size);
    BOOL flushed=FlushInstructionCache(GetCurrentProcess(),h->site,h->descriptor->relocate_size);
    BOOL protected=VirtualProtect(h->site,h->descriptor->relocate_size,old,&ignored);
    return flushed && protected;
}
#endif
