#ifndef ES2_TRANSFORM_HELPERS_HOOKS_H
#define ES2_TRANSFORM_HELPERS_HOOKS_H
/* transform_helpers group (docs/reverse-engineering/player_movement.md,
 * section "enter-prologue helpers 47ff8c and 4801d4"): the pure enter-prologue leaves rotate_words 47ff8c and
 * matrix_point 4801d4. Neither calls game code (no graph, no counters).
 * Both return EDX:EAX and ECX exactly: matrix_point's ECX escapes through
 * thin wrappers and hooks that return it unchanged (48db93, 406808,
 * 41b60c, col_bound's JBE exit) and into callees, rotate_words' through
 * slope_speed's early return, so each hook has an exit shim (the
 * panel_weapons pattern with three slots): it calls the framework's
 * dispatch with the three arguments copied and, if the replacement ran,
 * loads the ECX the core computed. EAX and EDX pass through; the entry
 * EDX/ECX reach the replacement context unchanged (matrix_point's
 * mode < 1 path returns them). The shims are chained from
 * bullet_ctor_capture_entry through
 * transform_helpers_capture_entry. No entry register is read otherwise,
 * no uninitialized local. Flags at return are dead at all 33 call sites
 * (each writes flags before reading them). Both refuse compare mode (the
 * output may overlap the inputs). No game instruction is embedded; the
 * assembly only marshals registers. */
#include "es2/core/transform_helpers.h"
#define TRANSFORM_HELPERS_EXIT(name) __attribute__((used)) static uint32_t transform_helpers_##name##_ecx,transform_helpers_##name##_ecx_valid; \
    __attribute__((used)) static void *transform_helpers_##name##_bridge;
TRANSFORM_HELPERS_EXIT(rotate) TRANSFORM_HELPERS_EXIT(point)
#undef TRANSFORM_HELPERS_EXIT
static uint64_t __cdecl transform_helpers_rotate_words(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;uint32_t ecx;
    uint64_t v=es2_rotate_words_registers(&math_native_memory,a[0],a[1],a[2],&ecx);
    transform_helpers_rotate_ecx=ecx;transform_helpers_rotate_ecx_valid=1;return v;
}
static uint64_t __cdecl transform_helpers_matrix_point(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;uint32_t ecx;
    uint64_t v=es2_matrix_point_registers(&math_native_memory,a[0],a[1],a[2],r,&ecx);
    transform_helpers_point_ecx=ecx;transform_helpers_point_ecx_valid=1;return v;
}
static callee_counter *transform_helpers_hook_counters(unsigned hook,unsigned *count) {
    (void)hook;*count=0;return NULL;
}
/* enter 0,0; push esi; push edi; push ebx; the three slots into EBX, ESI,
 * EDI; then the first distinct instruction (movsx eax,[ebx] / cmp byte
 * [esi+12h],1). */
static const BYTE transform_helpers_rotate_sig[]={0xc8,0x00,0x00,0x00,0x56,0x57,0x53,0x8b,0x5d,0x08,0x8b,0x75,0x0c,0x8b,0x7d,0x10,0x0f,0xbf,0x03};
static const BYTE transform_helpers_point_sig[]={0xc8,0x00,0x00,0x00,0x56,0x57,0x53,0x8b,0x5d,0x08,0x8b,0x75,0x0c,0x8b,0x7d,0x10,0x80,0x7e,0x12,0x01};
static const BYTE transform_helpers_lens[]={4,1};
/* The 32-bit legacy entries never see registers: matrix_point's mode < 1
 * path then reads zero entry EDX/ECX (legacy callers ignore them). */
static const uint32_t transform_helpers_no_registers[3]={0,0,0};
#define TRANSFORM_HELPERS_EXT(name) \
    static const es2_hook_extension transform_helpers_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,transform_helpers_##name,TRUE}; \
    static uint32_t __cdecl transform_helpers_##name##32(const uint32_t *a,void *u) {return (uint32_t)transform_helpers_##name(a,u,transform_helpers_no_registers);}
TRANSFORM_HELPERS_EXT(rotate_words)
TRANSFORM_HELPERS_EXT(matrix_point)
#undef TRANSFORM_HELPERS_EXT
static es2_hook_descriptor transform_helpers_hook_descriptors[]={
    {ES2_HOOK_ID(rotate_words),transform_helpers_rotate_sig,19,5,transform_helpers_lens,2,3,transform_helpers_rotate_words32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(matrix_point),transform_helpers_point_sig,20,5,transform_helpers_lens,2,3,transform_helpers_matrix_point32,NULL,NULL,NULL,0},
};
/* The exit shims: a frame, the three arguments copied, the framework's
 * dispatch as a call, then the core's ECX if the replacement ran. */
#define TRANSFORM_HELPERS_SHIM(name) __attribute__((naked)) static void transform_helpers_##name##_entry(void) { __asm__ volatile( \
    "pushl %ebp\n\tmovl %esp,%ebp\n\tpushl 16(%ebp)\n\tpushl 12(%ebp)\n\tpushl 8(%ebp)\n\tmovl $0,_transform_helpers_" #name "_ecx_valid\n\t" \
    "call *_transform_helpers_" #name "_bridge\n\tmovl %ebp,%esp\n\tcmpl $0,_transform_helpers_" #name "_ecx_valid\n\tje 1f\n\t" \
    "movl _transform_helpers_" #name "_ecx,%ecx\n\tmovl $0,_transform_helpers_" #name "_ecx_valid\n\t1: popl %ebp\n\tret\n\t"); }
TRANSFORM_HELPERS_SHIM(rotate) TRANSFORM_HELPERS_SHIM(point)
#undef TRANSFORM_HELPERS_SHIM
/* Called by bullet_ctor_capture_entry after the framework installed its
 * jump: redirect the two jumps to the shims and keep h->patch consistent,
 * so uninstall still validates the exact patch. The chain continues in
 * commands_capture_entry (commands_hooks.h). */
static BOOL commands_capture_entry(es2_hook *h);
static BOOL transform_helpers_capture_entry(es2_hook *h) {
    void **bridge;void (*entry)(void);
    switch(h->descriptor->rva) {
    case 0x7ff8cu:bridge=&transform_helpers_rotate_bridge;entry=transform_helpers_rotate_entry;break;
    case 0x801d4u:bridge=&transform_helpers_point_bridge;entry=transform_helpers_point_entry;break;
    default:return commands_capture_entry(h);
    }
    DWORD old,ignored;
    if(memcmp(h->site,h->patch,h->descriptor->relocate_size))return FALSE;
    if(!VirtualProtect(h->site,h->descriptor->relocate_size,PAGE_EXECUTE_READWRITE,&old))return FALSE;
    *bridge=(void *)(uintptr_t)((uint32_t)(uintptr_t)(h->site+5)+math_native_read32(NULL,(uint32_t)(uintptr_t)(h->patch+1)));
    math_native_write32(NULL,(uint32_t)(uintptr_t)(h->patch+1),(uint32_t)(uintptr_t)entry-(uint32_t)(uintptr_t)(h->site+5));
    memcpy(h->site,h->patch,h->descriptor->relocate_size);
    BOOL flushed=FlushInstructionCache(GetCurrentProcess(),h->site,h->descriptor->relocate_size);
    BOOL protected=VirtualProtect(h->site,h->descriptor->relocate_size,old,&ignored);
    return flushed && protected;
}
#endif
