/* Legacy nested hooks with fixed scratch registers in the real-callee passes.
 *
 * The fourteen hooks without an extension (rng_next, rng_bounded, the four
 * distances, time_scale, the three timer steps, timer_arm, multiply_q8,
 * transform_point, vector_subtract, distance_display) use the framework's legacy path.
 * Its contract is EAX and the spans; the bridge leaves EDX and ECX as the C dispatcher
 * and, in replaced mode, the replacement left them (hook/hook.c, dispatch: nothing
 * after the replacement call touches them). The originals' callers pass those registers
 * on: the hooked caller returns them as its EDX (base_update returns "the timer's EDX",
 * vtables.md, "`base_update`: contract";
 * likewise mech_contact, status_eval, ram_step and weapon_step on paths whose last call
 * is a legacy callee) or they are the entry EDX/ECX a logging stub records at the next
 * game call (lc_base_update, hunt, target_emit). A real-callee pass compares a run with
 * the hook under test original or replaced against a run with the same nested hooks and
 * the caller's original body, so those values matched only while the replacement left
 * the same scratch value in both runs. Built with -fprofile-instr-generate,
 * es2_timer_word_step leaves the low dword of one of its own profile counters in EDX
 * (it adds 2 per call), which grows between the two runs: EDX 2 against 4
 * (mech_contact), 26f8h against 2704h (base_update).
 *
 * nested_install_hook installs a legacy nested hook in replaced mode through
 * a copy of its descriptor whose replacement calls the registered one with
 * the registered user pointer and then sets EDX and ECX to two fixed values.
 * EAX, every byte the replacement writes and the dispatch itself are
 * unchanged, so both runs see the same EDX/ECX after the hook and the passes
 * keep comparing the returned EDX and the stub entry registers exactly, on
 * every path, including the paths where they carry the nested hook's EDX.
 * Compare and original modes, and every hook with an extension (its bridge
 * returns the adapter's EDX:EAX and the entry ECX, both deterministic), are
 * installed unchanged. The live proxy never uses this. The two values have
 * the top bit set, so a stub's stack mask never takes them for a stack
 * address. */
#define LEGACY_NESTED_EDX 0xdead1e9au
#define LEGACY_NESTED_ECX 0xdead1e9cu
typedef struct legacy_nested_context { es2_hook_replacement replacement; void *user; } legacy_nested_context;
static legacy_nested_context legacy_nested_contexts[FUNCTION_HOOK_COUNT];
static es2_hook_descriptor legacy_nested_descriptors[FUNCTION_HOOK_COUNT];
/* cdecl (args, context): calls context->replacement(args, context->user)
 * with the caller's 16-byte stack alignment, keeps its EAX, fixes EDX/ECX. */
__attribute__((naked,used)) static uint32_t __cdecl legacy_nested_replacement(const uint32_t *args,void *context){
    __asm__ volatile(
        "movl 8(%esp),%eax\n\t"
        "subl $4,%esp\n\t"
        "pushl 4(%eax)\n\t"
        "pushl 12(%esp)\n\t"
        "calll *(%eax)\n\t"
        "addl $12,%esp\n\t"
        "movl $0xdead1e9a,%edx\n\t"
        "movl $0xdead1e9c,%ecx\n\t"
        "ret\n\t");
}
/* The descriptor to install for nested hook index in mode o->mode with
 * extension e: the registered one, or for a legacy hook in replaced mode its
 * copy (valid until the next call for the same index replaces it in place;
 * install and uninstall happen per case, one hook per index at a time). */
static const es2_hook_descriptor *nested_descriptor(unsigned index,const es2_hook_options *o,const es2_hook_extension *e){
    const es2_hook_descriptor *d=function_hook_descriptor(index);
    if(e||o->mode!=ES2_HOOK_REPLACED||index>=FUNCTION_HOOK_COUNT)return d;
    legacy_nested_contexts[index].replacement=d->replacement;legacy_nested_contexts[index].user=d->user;
    legacy_nested_descriptors[index]=*d;
    legacy_nested_descriptors[index].replacement=legacy_nested_replacement;
    legacy_nested_descriptors[index].user=&legacy_nested_contexts[index];
    return &legacy_nested_descriptors[index];
}
static BOOL nested_install_hook(es2_hook *h,unsigned index,const es2_hook_options *o,const es2_hook_extension *e){
    return weapons_install_hook(h,nested_descriptor(index,o,e),o,e);
}
