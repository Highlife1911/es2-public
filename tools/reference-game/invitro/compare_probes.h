/* Installed compare-mode cases for hooks that accept compare mode
 * (docs/reference-game/compare_mode.md). The group supplies valid inputs for
 * its real callees (build: arguments and entry EAX/EDX/ECX) and the state its
 * function can touch (state: arena and globals, saved or restored); every
 * callee runs as real code.
 *
 * Own run (limit 0, the hook not installed): per case an unhooked call of
 * the original gives the expected pair and state; the same call through the
 * hook installed in compare mode must return that pair and leave that state,
 * with one call, no skip, no bypass and no mismatch. A second pass installs
 * a skewed replacement (the exact one with EAX bit 0 flipped, or for a hook
 * with spans in every other case bit 0 of its first declared byte flipped
 * in the clone): every call must count one mismatch while the caller still
 * gets the original's pair and state, so the comparison is shown to look at
 * the result and the declared bytes.
 *
 * Installed pass (limit > 0; main.c's adapter integration with every hook
 * installed, this one in compare mode): the hook's trampoline (the original
 * body, nested hooks in their modes) against the hooked call on the same
 * state, EAX and state; main.c then requires zero mismatches. Prefix
 * compare_probe_.
 *
 * Options, set by the group around one compare_probe_run call:
 * compare_probe_skew_edx flips EDX bit 0 instead of EAX bit 0 (for a hook whose
 * comparator ignores EAX on a path where EAX is a dead frame address,
 * slope_speed); compare_probe_same replaces the pair equality of the unhooked
 * and the compared call (a frame address in EAX or EDX moves with the stack
 * depth at which the original runs: line_plane's hit EDX, slope_speed's face-0
 * EAX, object_query's residue). */
typedef void (*compare_probe_build)(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]);
typedef void (*compare_probe_state)(BYTE *state,int restore);
typedef int (*compare_probe_pair)(unsigned index,uint64_t want,uint64_t got);
enum { COMPARE_PROBE_STATE=0x4000, COMPARE_PROBE_CASES=256 };
static es2_hook_replacement_context compare_probe_exact;
static const es2_hook_descriptor *compare_probe_descriptor;
static int compare_probe_skew_span,compare_probe_skew_edx;
static compare_probe_pair compare_probe_same;
/* The skewed replacement: EAX (or EDX) bit 0 flipped, or (alternate cases
 * of a hook with spans) bit 0 of the first declared byte flipped in the
 * clone. */
static uint64_t __cdecl compare_probe_skew(const uint32_t *a,void *u,const uint32_t *r) {
    const uint64_t result=compare_probe_exact(a,u,r);
    if(!compare_probe_skew_span)return result^(compare_probe_skew_edx?(uint64_t)1u<<32:1u);
    const es2_hook_span *s=compare_probe_descriptor->spans;
    ((BYTE *)(uintptr_t)a[s->arg_index])[s->offset]^=1u;
    return result;
}
static uint64_t compare_probe_call(void *target,unsigned index,const uint32_t *a,const uint32_t *r) {
    es2_hook_result result;
    es2_hook_call call={target,ES2_ABI_CDECL,function_hook_descriptor(index)->arg_count,a,initial_fpu,&result,r};
    es2_hook_machine_call(&call);
    return (uint64_t)result.edx<<32|result.eax;
}
/* The hook's extension accepts compare mode (with a context replacement). */
static int compare_probe_capable(unsigned index) {
    const es2_hook_extension *x=function_hook_extension(index);
    return x&&!x->compare_unsupported&&x->replacement_context;
}
/* The harness's installed pass has this hook in compare mode. */
static int compare_probe_installed(unsigned index) {
    return rng_hooks[index].installed==1&&rng_hooks[index].options.mode==ES2_HOOK_COMPARE;
}
static int compare_probe_run(unsigned index,unsigned limit,compare_probe_build build,compare_probe_state state,size_t size) {
    static BYTE before[COMPARE_PROBE_STATE],after[COMPARE_PROBE_STATE],actual[COMPARE_PROBE_STATE];
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    if(size>COMPARE_PROBE_STATE)return fail("compare probe state too large");
    if(!compare_probe_capable(index))return fail("compare probe on a hook that refuses compare");
    if(limit&&!compare_probe_installed(index))return fail("compare probe outside the compare pass");
    es2_hook_extension skewed=*x;compare_probe_exact=x->replacement_context;skewed.replacement_context=compare_probe_skew;
    compare_probe_descriptor=d;
    const unsigned cases=limit?limit:COMPARE_PROBE_CASES;unsigned total=0,span_skews=0;uint64_t skew_mismatches=0;
    for(unsigned k=0;k<(limit?1u:2u);++k)for(unsigned n=0;n<cases;++n){
        uint32_t a[6]={0},r[3]={0};current_case=n;current_case_valid=1;
        compare_probe_skew_span=k&&d->span_count&&(n&1);span_skews+=compare_probe_skew_span;
        build(index,n,a,r);state(before,0);
        const uint64_t expected=compare_probe_call(limit?rng_hooks[index].trampoline:mapped+d->rva,index,a,r);
        state(after,0);state(before,1);
        es2_hook h={0};
        if(!limit){
            es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_COMPARE;
            if(!weapons_install_hook(&h,d,&o,k?&skewed:x))return fail("compare probe install");
        }
        const uint64_t got=compare_probe_call(mapped+d->rva,index,a,r);state(actual,0);
        /* In the installed pass the trampoline's nested hooks run in their
         * modes, the compare phase's original with them bypassed; a legacy
         * nested hook (EAX contract, e.g. dist2d_point under region_test)
         * leaves another EDX there, so that pass checks EAX and state. */
        const uint64_t compared=limit?(got&0xffffffffu)|(expected&~(uint64_t)0xffffffffu):got;
        int bad=(compare_probe_same?!compare_probe_same(index,expected,compared):compared!=expected)||memcmp(after,actual,size);
        if(!limit){
            bad|=h.calls!=1||h.skipped||h.bypassed||h.mismatches!=k;skew_mismatches+=k?h.mismatches:0;
            if(!es2_hook_uninstall(&h))return fail("compare probe uninstall");
        }
        state(before,1);
        if(bad){
            unsigned j=0;while(j<size&&after[j]==actual[j])++j;
            fprintf(stderr,"FAIL: %s compare case %u (%s) pair %016llx/%016llx calls %llu skipped %llu bypassed %llu mismatches %llu state +%x\n",
                d->name,n,limit?"installed pass":k?"skewed":"exact",(unsigned long long)expected,(unsigned long long)got,
                (unsigned long long)h.calls,(unsigned long long)h.skipped,(unsigned long long)h.bypassed,(unsigned long long)h.mismatches,j);
            return 1;
        }
        ++total;
    }
    current_case_valid=0;
    if(!limit)printf("PASS: %s %u installed compare cases: exact replacement 0 mismatches, skewed replacement %llu mismatches (%u by a span byte), original pair and state kept\n",
        d->name,total,(unsigned long long)skew_mismatches,span_skews);
    return 0;
}
