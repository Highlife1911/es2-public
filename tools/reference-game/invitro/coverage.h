/* Observe original Jcc decisions in the private mapping. Only an INT3 is
 * installed in memory; no original instruction is exported or persisted. The
 * handler consumes EFLAGS exactly as Jcc would and redirects EIP accordingly.
 * All original bytes are restored before detour/signature integration tests.
 * Early exit: a site is disarmed (its byte restored) as soon as it has both
 * outcomes, since further counts cannot change its verdict; proved sites stay
 * armed for every window so their impossible edge is watched as before. A
 * window (coverage_active) still opens and closes where the runner says. */
#include "coverage_diagnostics.h"
#include "branch_condition.h"
typedef struct branch_site {
    unsigned owner; uint32_t address; unsigned length;
    enum branch_condition condition; uint32_t target;
    uint64_t counts[2]; BYTE saved,armed;
} branch_site;
static branch_site branch_sites[]={
#define BRANCH(name,address,length,condition,target) {ES2_HOOK_INDEX_##name,address,length,condition,target,{0,0},0,0},
#define PROVED(name,address,edge,reason)
#include "branch_sites.h"
#undef PROVED
#undef BRANCH
};
/* Proved unreachable edges (PROVED lines of branch_sites.h): edge is the
 * only feasible outcome of the site. */
typedef struct branch_proof { unsigned owner; uint32_t address; unsigned edge; const char *reason; } branch_proof;
static const branch_proof branch_proofs[]={
#define BRANCH(name,address,length,condition,target)
#define PROVED(name,address,edge,reason) {ES2_HOOK_INDEX_##name,address,edge,reason},
#include "branch_sites.h"
#undef PROVED
#undef BRANCH
};
static const branch_proof *coverage_proof(const branch_site *s) {
    for(unsigned i=0;i<sizeof(branch_proofs)/sizeof(branch_proofs[0]);++i)
        if(branch_proofs[i].address==s->address)return &branch_proofs[i];
    return NULL;
}
/* A site still needs INT3 while an outcome is missing or a proof watches it. */
static int coverage_needed(const branch_site *s) { return !s->counts[0] || !s->counts[1] || coverage_proof(s); }
static void coverage_patch(branch_site *s,int arm) {
    BYTE *p=(BYTE *)(uintptr_t)s->address; DWORD old,ignored;
    if(!VirtualProtect(p,1,PAGE_EXECUTE_READWRITE,&old)) ExitProcess(130);
    if(arm) { s->saved=*p; *p=0xcc; } else *p=s->saved;
    if(!VirtualProtect(p,1,old,&ignored)) ExitProcess(131);
    s->armed=(BYTE)arm;
}
static int coverage_active;
static unsigned coverage_released; /* sites disarmed early, all windows */
static void coverage_enable(int enabled) {
    if(enabled==coverage_active) return;
    for(unsigned i=0;i<sizeof(branch_sites)/sizeof(branch_sites[0]);++i) {
        branch_site *s=&branch_sites[i];
        if(enabled?coverage_needed(s):s->armed) coverage_patch(s,enabled);
    }
    FlushInstructionCache(GetCurrentProcess(),NULL,0); coverage_active=enabled;
}
static LONG coverage_fault(EXCEPTION_POINTERS *fault) {
    if(!coverage_active || fault->ExceptionRecord->ExceptionCode!=EXCEPTION_BREAKPOINT) return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t pc=(uintptr_t)fault->ExceptionRecord->ExceptionAddress;
    for(unsigned i=0;i<sizeof(branch_sites)/sizeof(branch_sites[0]);++i) {
        branch_site *s=&branch_sites[i]; if(s->address!=pc || !s->armed) continue;
        DWORD flags=fault->ContextRecord->EFlags;
        unsigned taken=branch_condition_taken(s->condition,flags);
        ++s->counts[taken]; fault->ContextRecord->Eip=taken?s->target:s->address+s->length;
        if(!s->counts[!taken] || coverage_proof(s)) return EXCEPTION_CONTINUE_EXECUTION;
        coverage_patch(s,0); FlushInstructionCache(GetCurrentProcess(),(void *)pc,1); ++coverage_released;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
static int coverage_report(unsigned selected,int all) {
    /* Each proof names exactly one listed site of the same hook. */
    for(unsigned p=0;p<sizeof(branch_proofs)/sizeof(branch_proofs[0]);++p) {
        unsigned matches=0;
        for(unsigned i=0;i<sizeof(branch_sites)/sizeof(branch_sites[0]);++i) if(branch_sites[i].address==branch_proofs[p].address) {
            if(branch_sites[i].owner!=branch_proofs[p].owner)return fail("PROVED line names another hook than its branch site");
            ++matches;
        }
        if(matches!=1 || branch_proofs[p].edge>1)return fail("PROVED line without exactly one branch site or with an edge other than 0/1");
    }
    unsigned sites=0,proved=0;
    for(unsigned i=0;i<sizeof(branch_sites)/sizeof(branch_sites[0]);++i) {
        branch_site *s=&branch_sites[i]; if(!all && s->owner!=selected) continue;
        if(all && !function_in_shard(s->owner)) continue;
        const char *name=function_hook_descriptor(s->owner)->name;
        printf("BRANCH %-20s VA=%08x fall=%llu taken=%llu\n",name,s->address,
            (unsigned long long)s->counts[0],(unsigned long long)s->counts[1]);
        ++sites;
        const branch_proof *proof=coverage_proof(s);
        if(proof) {
            unsigned edge=proof->edge;
            if(!s->counts[edge] || s->counts[!edge]) {
                fprintf(stderr,"FAIL: %s %08x proved edge=%u (%s) but fall=%llu taken=%llu\n",name,s->address,edge,proof->reason,
                    (unsigned long long)s->counts[0],(unsigned long long)s->counts[1]);
                return fail("proved-unreachable site: feasible edge missing or impossible edge observed");
            }
            ++proved;printf("PROVED: %s %08x %s; edge=%u\n",name,s->address,proof->reason,edge);continue;
        }
        if(coverage_missing(name,s->address,s->counts[0],s->counts[1])) return 1;
    }
    printf("PASS: %u original conditional branches: %u both outcomes, %u with proved unreachable edge; %u sites disarmed at their second outcome\n",
        sites,sites-proved,proved,coverage_released);return 0;
}
