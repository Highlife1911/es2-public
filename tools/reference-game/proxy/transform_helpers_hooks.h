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
 * mode < 1 path returns them). No entry register is read otherwise,
 * no uninitialized local. Flags at return are dead at all 33 call sites
 * (each writes flags before reading them). Both refuse compare mode (the
 * output may overlap the inputs). No game instruction is embedded; the
 * shim only marshals registers. */
#include "es2/core/transform_helpers.h"
#define TRANSFORM_HELPERS_EXIT(name) __attribute__((used)) static uint32_t transform_helpers_##name##_ecx,transform_helpers_##name##_ecx_valid;
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
/* The exit shims (shim_exit_call) with the three arguments copied. */
#define TRANSFORM_HELPERS_SHIM(name,rva) \
    static int transform_helpers_##name##_entry(es2_machine *m,uint32_t bridge) { \
        return shim_exit_call(m,bridge,3,&transform_helpers_##name##_ecx,&transform_helpers_##name##_ecx_valid); } \
    ES2_SHIM(transform_helpers_##name##_shim,rva,transform_helpers_##name##_entry,0);
TRANSFORM_HELPERS_SHIM(rotate,0x7ff8cu) TRANSFORM_HELPERS_SHIM(point,0x801d4u)
#undef TRANSFORM_HELPERS_SHIM
#endif
