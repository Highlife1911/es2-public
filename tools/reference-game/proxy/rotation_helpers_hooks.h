#ifndef ES2_ROTATION_HELPERS_HOOKS_H
#define ES2_ROTATION_HELPERS_HOOKS_H
/* rotation_helpers group (docs/reverse-engineering/flyer.md, section
 * "the rotation helpers"): the pure enter-prologue leaves rotation_compose 47f058 (the
 * 3 x 3 part of a x b, no translation) and rotation_place 47f4f4 (b's
 * translation, then the same); cores appended to core/src/matrix.c.
 * Neither calls game code (no graph, no counters). EAX = out, EDX exact
 * (the entry EDX on the copy paths). ECX is live after 47f058 at
 * debris_piece's call 40863e (it enters matrix_angles 47f9a0 and, through
 * its callees, debris_spin_any) and after 47f4f4 at weapon_prepare's
 * 40e873 (it enters euler_matrix or matrix_compose), so each hook has an
 * exit shim (the transform_helpers pattern with three slots) that loads the
 * core's ECX when the replacement ran. The shims are chained from
 * cockpit_sync_capture_entry through
 * rotation_helpers_capture_entry. No entry register other than EDX is
 * read, no uninitialized local (enter 0,0). Flags at return are dead at
 * the three call sites (40863e, 40e873, 46754e: each writes flags with
 * ADD ESP before reading them).
 *
 * Both accept compare mode: spans on a (20 bytes), b (20 bytes, 32 for
 * rotation_place, whose translation it reads) and out (20 bytes, 32 for
 * rotation_place); the comparator remaps EAX (out) to the clone and
 * compares EDX and every span byte. Overlapping arguments (no caller
 * passes them) make the framework skip the compare. In compare mode ECX
 * comes from the replacement, which reads the same pre-state through its
 * clones; ECX is not compared (the direct cases check it). No game
 * instruction is embedded; the assembly only marshals registers. */
#include "es2/core/matrix.h"
#define ROTATION_HELPERS_EXIT(name) __attribute__((used)) static uint32_t rotation_helpers_##name##_ecx,rotation_helpers_##name##_ecx_valid; \
    __attribute__((used)) static void *rotation_helpers_##name##_bridge;
ROTATION_HELPERS_EXIT(compose) ROTATION_HELPERS_EXIT(place)
#undef ROTATION_HELPERS_EXIT
static uint64_t __cdecl rotation_helpers_rotation_compose(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;uint32_t ecx;
    const uint32_t edx=es2_matrix_rotation_compose_registers((const void *)(uintptr_t)a[0],(const void *)(uintptr_t)a[1],
        (void *)(uintptr_t)a[2],r[1],&ecx);
    rotation_helpers_compose_ecx=ecx;rotation_helpers_compose_ecx_valid=1;return (uint64_t)edx<<32|a[2];
}
static uint64_t __cdecl rotation_helpers_rotation_place(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;uint32_t ecx;
    const uint32_t edx=es2_matrix_rotation_place_registers((const void *)(uintptr_t)a[0],(const void *)(uintptr_t)a[1],
        (void *)(uintptr_t)a[2],r[1],&ecx);
    rotation_helpers_place_ecx=ecx;rotation_helpers_place_ecx_valid=1;return (uint64_t)edx<<32|a[2];
}
static callee_counter *rotation_helpers_hook_counters(unsigned hook,unsigned *count) {
    (void)hook;*count=0;return NULL;
}
/* enter 0,0; push esi; push edi; push ebx; the three slots into EBX, ESI,
 * EDI; then the first distinct instruction (cmp byte [ebx+12h],1 / mov
 * eax,[esi+14h]). */
static const BYTE rotation_helpers_compose_sig[]={0xc8,0x00,0x00,0x00,0x56,0x57,0x53,0x8b,0x5d,0x08,0x8b,0x75,0x0c,0x8b,0x7d,0x10,0x80,0x7b,0x12,0x01};
static const BYTE rotation_helpers_place_sig[]={0xc8,0x00,0x00,0x00,0x56,0x57,0x53,0x8b,0x5d,0x08,0x8b,0x75,0x0c,0x8b,0x7d,0x10,0x8b,0x46,0x14};
static const BYTE rotation_helpers_lens[]={4,1};
/* Compare spans: a, b, out (out third: the comparator's remap). */
static const es2_hook_span rotation_helpers_compose_spans[]={{0,0,20},{1,0,20},{2,0,20}};
static const es2_hook_span rotation_helpers_place_spans[]={{0,0,20},{1,0,32},{2,0,32}};
static BOOL __cdecl rotation_helpers_equal(const es2_hook_comparison *v,void *u) {
    (void)u;
    for(unsigned i=0;i<v->span_count;++i)if(memcmp(v->original[i],v->replacement[i],v->sizes[i]))return FALSE;
    return v->original_result==(uint32_t)(uintptr_t)v->original[2]&&v->replacement_result==(uint32_t)(uintptr_t)v->replacement[2]&&
        v->original_edx==v->replacement_edx;
}
/* The 32-bit legacy entries never see registers: the copy paths then
 * return a zero entry EDX (legacy callers ignore it). */
static const uint32_t rotation_helpers_no_registers[3]={0,0,0};
#define ROTATION_HELPERS_EXT(name) \
    static const es2_hook_extension rotation_helpers_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,rotation_helpers_##name,FALSE}; \
    static uint32_t __cdecl rotation_helpers_##name##32(const uint32_t *a,void *u) {return (uint32_t)rotation_helpers_##name(a,u,rotation_helpers_no_registers);}
ROTATION_HELPERS_EXT(rotation_compose)
ROTATION_HELPERS_EXT(rotation_place)
#undef ROTATION_HELPERS_EXT
static es2_hook_descriptor rotation_helpers_hook_descriptors[]={
    {ES2_HOOK_ID(rotation_compose),rotation_helpers_compose_sig,20,5,rotation_helpers_lens,2,3,rotation_helpers_rotation_compose32,
        rotation_helpers_equal,NULL,rotation_helpers_compose_spans,3},
    {ES2_HOOK_ID(rotation_place),rotation_helpers_place_sig,19,5,rotation_helpers_lens,2,3,rotation_helpers_rotation_place32,
        rotation_helpers_equal,NULL,rotation_helpers_place_spans,3},
};
/* The exit shims: a frame, the three arguments copied, the framework's
 * dispatch as a call, then the core's ECX if the replacement ran. */
#define ROTATION_HELPERS_SHIM(name) __attribute__((naked)) static void rotation_helpers_##name##_entry(void) { __asm__ volatile( \
    "pushl %ebp\n\tmovl %esp,%ebp\n\tpushl 16(%ebp)\n\tpushl 12(%ebp)\n\tpushl 8(%ebp)\n\tmovl $0,_rotation_helpers_" #name "_ecx_valid\n\t" \
    "call *_rotation_helpers_" #name "_bridge\n\tmovl %ebp,%esp\n\tcmpl $0,_rotation_helpers_" #name "_ecx_valid\n\tje 1f\n\t" \
    "movl _rotation_helpers_" #name "_ecx,%ecx\n\tmovl $0,_rotation_helpers_" #name "_ecx_valid\n\t1: popl %ebp\n\tret\n\t"); }
ROTATION_HELPERS_SHIM(compose) ROTATION_HELPERS_SHIM(place)
#undef ROTATION_HELPERS_SHIM
/* Called by cockpit_sync_capture_entry after the framework installed its
 * jump: redirect the two jumps to the shims and keep h->patch consistent,
 * so uninstall still validates the exact patch. The chain continues in
 * render_root_capture_entry
 * (render_root_hooks.h). */
static BOOL render_root_capture_entry(es2_hook *h);
static BOOL rotation_helpers_capture_entry(es2_hook *h) {
    void **bridge;void (*entry)(void);
    switch(h->descriptor->rva) {
    case 0x7f058u:bridge=&rotation_helpers_compose_bridge;entry=rotation_helpers_compose_entry;break;
    case 0x7f4f4u:bridge=&rotation_helpers_place_bridge;entry=rotation_helpers_place_entry;break;
    default:return render_root_capture_entry(h);
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
