/* pool_iter group: pool_prev 471c70 and pool_next 471c18, the object-pool
 * iterator (docs/reverse-engineering/main_loop.md, "pool iterator 471c70 /
 * 471c18"). One arena holds the pool header and the nodes. The debug report of
 * 471c18 needs a node below 100000h; no 64 KiB granule is free there in the
 * harness, so such nodes lie in a read-only system mapping below 1 MiB found at
 * run time (stable contents, never written; preferably the one that spans
 * 100000h, which also gives the boundary nodes 0FFFF8h..100000h). The CW3220
 * sprintf thunk 496796 is a logging stub in every pass (the harness traps
 * imports): ordered calls with all six arguments and entry EAX/EDX/ECX, stack
 * values masked. pool_next: the direct cases run the whole original 471c18 and
 * the core. pool_prev: 471c18 is a logging stub with a planned walk (nonzero
 * results, then 0; random EDX/ECX that the next call must receive). Both
 * compare the full EDX:EAX pair, the call log and the arena. Then installed
 * 31/a7 poison probes, and a real pass over real used lists (a low node with
 * two zero links as the tail in a quarter of them) through the original skip
 * helper 471cb0 and direct 471c70 calls, with both hooks in original/replaced
 * modes against the all-original run. */
typedef struct pq_record { uint32_t id,args[6],regs[3]; } pq_record;
enum { PQ_SIZE=0x1000, PQ_POOL=0x40, PQ_NODES=0x100, PQ_NODE_STRIDE=0x40, PQ_NODE_COUNT=48, PQ_MAX=40, PQ_IDS=2,
    PQ_PLAN=12, PQ_ZEROS=64 };
#define PQ_BASE ((uint32_t)(uintptr_t)pq_arena)
static BYTE pq_arena[PQ_SIZE],pq_before[PQ_SIZE],pq_after[PQ_SIZE];
static uint32_t pq_low_lo,pq_low_hi;     /* readable [lo, hi), lo below 100000h */
static uint32_t pq_zero[PQ_ZEROS];       /* low nodes N with [N] = [N+4] = 0 */
static unsigned pq_zero_count;
static int pq_edge;                      /* 100000h..100107h readable */
static pq_record pq_log[PQ_MAX];
static unsigned pq_calls,pq_reports,pq_boundary;
static uint32_t pq_seed,pq_stream,pq_plan[PQ_PLAN];
__attribute__((used)) static uint32_t pq_ecx;
static const uint32_t pq_addresses[PQ_IDS]={0x496796,0x471c18};
static const unsigned pq_counts[PQ_IDS]={6,2};
static uint32_t pq_next(void){
    uint32_t z=pq_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static int pq_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static int pq_readonly(const MEMORY_BASIC_INFORMATION *m){
    return m->State==MEM_COMMIT&&m->Type!=MEM_PRIVATE&&m->Protect==PAGE_READONLY;
}
/* The read-only mapped (or image) region below 1 MiB: the one that
 * reaches past 100000h if any, else the largest; read-only regions that
 * follow it directly are joined. Then up to PQ_ZEROS aligned nodes with
 * two zero links. */
static int pq_find_low(void){
    if(pq_low_hi)return 0;
    if(PQ_BASE<0x100000u)return fail("pool_iter: arena below 1 MiB");
    uint32_t best_lo=0,best_hi=0;
    for(uint32_t at=0x10000u;at<0x100000u;){
        MEMORY_BASIC_INFORMATION m;if(!VirtualQuery((void *)(uintptr_t)at,&m,sizeof(m)))break;
        uint32_t lo=(uint32_t)(uintptr_t)m.BaseAddress,next=lo+(uint32_t)m.RegionSize,hi=next;
        if(pq_readonly(&m)){
            for(;hi<0x200000u;){MEMORY_BASIC_INFORMATION n;
                if(!VirtualQuery((void *)(uintptr_t)hi,&n,sizeof(n))||!pq_readonly(&n))break;hi+=(uint32_t)n.RegionSize;}
            int spans=hi>=0x100108u,best_spans=best_hi>=0x100108u;
            if(!best_hi||(spans&&!best_spans)||(spans==best_spans&&hi-lo>best_hi-best_lo)){best_lo=lo;best_hi=hi;}
        }
        at=next;
    }
    if(!best_hi||best_lo+0x100u>0x100000u)return fail("pool_iter: no read-only mapping below 1 MiB");
    pq_low_lo=best_lo;pq_low_hi=best_hi;
    /* Boundary nodes 100000h..1000FFh need readable bytes there: the mapping
     * itself, else a zero page reserved at 100000h when that granule is free. */
    pq_edge=best_hi>=0x100108u||VirtualAlloc((void *)(uintptr_t)0x100000u,0x10000u,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)!=NULL;
    if(!pq_edge){MEMORY_BASIC_INFORMATION m;  /* else any committed readable region (only read) */
        pq_edge=VirtualQuery((void *)(uintptr_t)0x100000u,&m,sizeof(m))&&m.State==MEM_COMMIT&&!(m.Protect&(PAGE_GUARD|PAGE_NOACCESS))&&
            (uint32_t)(uintptr_t)m.BaseAddress+(uint32_t)m.RegionSize>=0x100108u;}
    for(uint32_t node=(best_lo+3u)&~3u;node+8u<=best_hi&&node<0x100000u-8u&&pq_zero_count<PQ_ZEROS;node+=4u)
        if(!get32((BYTE *)(uintptr_t)node)&&!get32((BYTE *)(uintptr_t)node+4)){pq_zero[pq_zero_count++]=node;node+=0x40u;}
    if(!pq_zero_count)return fail("pool_iter: no zero link pair in the low mapping");
    return 0;
}
__attribute__((used,noinline)) static uint64_t __cdecl pq_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(pq_calls==PQ_MAX){fail("pool_iter synthetic call overflow");ExitProcess(1);}
    unsigned k=pq_calls++;pq_record *v=pq_log+k;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,4*pq_counts[id]);memcpy(v->regs,regs,12);
    for(unsigned j=0;j<3;++j)if(pq_stack(v->regs[j]))v->regs[j]=0;
    for(unsigned j=0;j<pq_counts[id];++j)if(pq_stack(v->args[j]))v->args[j]=0;
    uint32_t eax=pq_next(),edx=pq_next();pq_ecx=pq_next();
    if(id==1)eax=k<PQ_PLAN?pq_plan[k]:0;
    return (uint64_t)edx<<32|eax;
}
#define PQ_STUB(n) __attribute__((naked)) static void pq_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _pq_stub_impl\n\taddl $24,%esp\n\tmovl _pq_ecx,%ecx\n\tret\n\t");}
PQ_STUB(0) PQ_STUB(1)
#undef PQ_STUB
static void *pq_stubs[PQ_IDS]={pq_stub_0,pq_stub_1};
static int pq_patch(BYTE saved[PQ_IDS][5],int active[PQ_IDS],unsigned which,int on){
    for(unsigned i=0;i<PQ_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(pq_addresses[i]-0x400000u,pq_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(pq_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static void pq_reset(void){pq_calls=0;pq_stream=pq_seed;}
/* A node address: in the arena (any alignment), or below 1 MiB (a quarter
 * of them just below 100000h or the mapping's end). */
static uint32_t pq_arena_node(void){return PQ_BASE+PQ_NODES+PQ_NODE_STRIDE*(random32()%PQ_NODE_COUNT)+random32()%4u;}
static uint32_t pq_low_node(void){
    uint32_t top=pq_low_hi<0x100000u?pq_low_hi:0x100000u;
    if(random32()%4==0)return top-8u-random32()%4u;
    return pq_low_lo+random32()%(top-8u-pq_low_lo);
}
/* A link value: 0, a node, FFFFFFF8h (object 0) or anything. */
static uint32_t pq_link(void){
    switch(random32()%8){
    case 0:case 1:return 0;
    case 2:return 0xfffffff8u;
    case 3:return random32();
    case 4:return pq_low_node();
    default:return pq_arena_node();
    }
}
static void pq_fill(void){
    random_bytes(pq_arena,PQ_SIZE);
    for(unsigned i=0;i<PQ_NODE_COUNT;++i)for(unsigned j=0;j<2;++j)
        if(random32()%2)put32(pq_arena+PQ_NODES+PQ_NODE_STRIDE*i+4*j,pq_link());
}
/* a[0] = pool, a[1] = object; r = entry EAX/EDX/ECX. */
static void pq_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    pq_seed=random32();pq_fill();memset(a,0,24);for(unsigned i=0;i<3;++i)r[i]=random32();
    a[0]=PQ_BASE+PQ_POOL+random32()%8u;
    if(random32()%4)put32((BYTE *)(uintptr_t)a[0]+0xe,pq_link());
    const unsigned pick=n<16?n%4:random32()%8;
    if(pick==0)a[1]=0;
    else if(pick==1)a[1]=pq_low_node()+8u;
    else if(pick==2&&pq_edge&&index==ES2_HOOK_INDEX_pool_next){  /* node at or just above 100000h: no report */
        a[1]=0x100000u+random32()%2u*(random32()%0x100u)+8u;++pq_boundary;
    }
    else a[1]=pq_arena_node()+8u;
    /* The planned walk for the 471c18 stub (pool_prev with O = 0). */
    unsigned steps=random32()%(n<64?3u:PQ_PLAN);
    for(unsigned i=0;i<PQ_PLAN;++i)pq_plan[i]=i<steps?(random32()%8?pq_arena_node()+8u:random32()|1u):0;
}
static int pq_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const pq_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,pq_calls);
    for(unsigned j=0;j<calls&&j<pq_calls&&expected;++j)if(memcmp(expected+j,pq_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(pq_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<PQ_SIZE;++j)if(pq_after[j]!=pq_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,pq_after[j],pq_arena[j]);break;}
    return 1;
}
static unsigned pq_expected_hits(const callee_counter *c){
    unsigned hits=0;for(unsigned j=0;j<pq_calls;++j)if(pq_addresses[pq_log[j].id]==c->key)++hits;return hits;
}
static int pq_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    static pq_record expected[PQ_MAX];
    memcpy(pq_before,pq_arena,PQ_SIZE);
    pq_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=pq_calls;memcpy(expected,pq_log,calls*sizeof(*expected));
    memcpy(pq_after,pq_arena,PQ_SIZE);memcpy(pq_arena,pq_before,PQ_SIZE);pq_reset();
    for(unsigned j=0;j<calls;++j)if(expected[j].id==0)++pq_reports;
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[2];for(unsigned i=0;i<count&&i<2;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);
    if(n<4096)for(unsigned i=0;i<count&&i<2;++i)if(c[i].count-counts[i]!=pq_expected_hits(c+i))return fail("pool_iter callee counter delta");
    if(pair[0]!=pair[1]||calls!=pq_calls||memcmp(expected,pq_log,calls*sizeof(*expected))||memcmp(pq_after,pq_arena,PQ_SIZE))
        return pq_report("direct",n,pair,calls,expected);
    return 0;
}
/* Stubs per hook: always the sprintf thunk; under pool_prev also 471c18. */
static unsigned pq_which(unsigned index){return index==ES2_HOOK_INDEX_pool_prev?3u:1u;}
static int pq_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;pq_build(index,n,a,r);
        BYTE expected_fpu[108],actual_fpu[108];static pq_record expected[PQ_MAX];
        memcpy(pq_before,pq_arena,PQ_SIZE);
        pq_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=pq_calls;memcpy(expected,pq_log,calls*sizeof(*expected));memcpy(pq_after,pq_arena,PQ_SIZE);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("pool_iter poison install");
        memcpy(pq_arena,pq_before,PQ_SIZE);pq_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);es2_hook_fpu_save(actual_fpu);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=pq_calls||memcmp(expected,pq_log,calls*sizeof(*expected))||memcmp(pq_after,pq_arena,PQ_SIZE)||
           memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=pq_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("pool_iter poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/arena\n",d->name,total);
    return status;
}
/* A real used list of up to 14 arena nodes, next links acyclic and
 * ending in 0, previous links mostly the true predecessors (else another
 * listed node or 0). In a quarter of the lists the tail is a low node with
 * two zero links (its report ends the walk; its previous link is 0). */
static void pq_list(uint32_t pool){
    uint32_t nodes[14];unsigned count=random32()%15u;
    for(unsigned i=0;i<count;++i){
        uint32_t node;int fresh;
        do{node=PQ_BASE+PQ_NODES+PQ_NODE_STRIDE*(random32()%PQ_NODE_COUNT);
            fresh=1;for(unsigned j=0;j<i;++j)if(nodes[j]==node)fresh=0;}while(!fresh);
        nodes[i]=node;
    }
    const int low_tail=random32()%4==0;
    const uint32_t tail=low_tail?pq_zero[random32()%pq_zero_count]:0;
    put32((BYTE *)(uintptr_t)pool+0xe,count?nodes[0]:tail);
    for(unsigned i=0;i<count;++i){
        BYTE *p=(BYTE *)(uintptr_t)nodes[i];
        put32(p,i+1<count?nodes[i+1]:tail);
        put32(p+4,random32()%8?(i?nodes[i-1]:0):random32()%2?nodes[random32()%count]:0);
    }
}
static int pq_real(unsigned index){
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0,reports=0;uint64_t entered=0;
    const unsigned hooks[2]={ES2_HOOK_INDEX_pool_prev,ES2_HOOK_INDEX_pool_next};
    BYTE saved[PQ_IDS][5];int active[PQ_IDS];if(pq_patch(saved,active,1u,1))return 1;
    for(unsigned mode=0;mode<4&&!status;++mode)for(unsigned n=0;n<768&&!status;++n){
        uint32_t a[6]={0},expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;static pq_record expected[PQ_MAX];
        pq_seed=random32();pq_fill();uint32_t pool=PQ_BASE+PQ_POOL+random32()%8u;pq_list(pool);
        /* 471cb0 (pool, count word) in half the cases, else 471c70 itself
         * with O = 0 or the object of a listed node. */
        uint32_t target=0x71cb0u;a[0]=pool;a[1]=random32()%16u|(random32()%4?0:random32()<<16);
        if(n%2){target=0x71c70u;uint32_t head=get32((BYTE *)(uintptr_t)pool+0xe);a[1]=random32()%2||!head?0:head+8u;}
        memcpy(pq_before,pq_arena,PQ_SIZE);
        pq_reset();sg_poison_call(mapped+target,a,pattern,expected_pair);
        unsigned calls=pq_calls;memcpy(expected,pq_log,calls*sizeof(*expected));memcpy(pq_after,pq_arena,PQ_SIZE);reports+=calls;
        es2_hook hk[2];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<2;++j){o.mode=(mode>>j&1)?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(hooks[j]),&o,function_hook_extension(hooks[j])))return fail("pool_iter real install");}
        memcpy(pq_arena,pq_before,PQ_SIZE);pq_reset();
        sg_poison_call(mapped+target,a,pattern,actual_pair);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=pq_calls||memcmp(expected,pq_log,calls*sizeof(*expected))||memcmp(pq_after,pq_arena,PQ_SIZE)){
            fprintf(stderr,"real target=%x mode=%u\n",target+0x400000u,mode);status=pq_report("real",n,pair,calls,expected);}
        for(unsigned j=0;j<2;++j){if(hooks[j]==index)entered+=hk[j].calls;if(!es2_hook_uninstall(hk+j))return fail("pool_iter real uninstall");}
        ++total;
    }
    if(pq_patch(saved,active,0,0))return 1;
    if(!status&&(!entered||!reports)){fprintf(stderr,"FAIL: %s real pass entered %llu, reports %u\n",d->name,(unsigned long long)entered,reports);status=1;}
    if(!status)printf("PASS: %s %u real cases under 471cb0/471c70 (%llu replaced entries, %u reports), both hooks original/replaced, 31/a7 stack, pair/trace/arena\n",
        d->name,total,(unsigned long long)entered,reports);
    return status;
}
static int pool_iter_cases(unsigned index,unsigned limit){
    if(pq_find_low())return 1;
    const unsigned which=pq_which(index);BYTE saved[PQ_IDS][5];int active[PQ_IDS];
    if(pq_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_POOL_ITER_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    pq_reports=pq_boundary=0;
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];pq_build(index,n,a,r);status=pq_compare(index,n,a,r);}
    const unsigned reports=pq_reports,boundary=pq_boundary;
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=pq_installed_probes(index,64);
    if(pq_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=pq_real(index);
    if(!status)printf("PASS: %-20s %u cases (%u debug reports, %u boundary nodes at 100000h (edge %d), low mapping %08x..%08x, %u zero pairs); pair, ordered calls with entry registers, arena\n",
        function_hook_descriptor(index)->name,cases,reports,boundary,pq_edge,pq_low_lo,pq_low_hi,pq_zero_count);
    return status;
}
