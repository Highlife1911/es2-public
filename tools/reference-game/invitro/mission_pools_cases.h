/* mission_pools group: the pool-only stage-2 callbacks light_pools 4076e4,
 * smoke_pools 4090cc and projectile_pools 40a060
 * (docs/reverse-engineering/mission_load.md, "L8 stage-2 pool callbacks").
 *
 * Direct cases run the original body (EAX/EDX/ECX at entry from the case;
 * EAX/EDX/ECX after it captured), the adapter and the core (with the ECX it
 * reports) against logging stubs for every game callee: 49630c (a record
 * stub: the real FS:0 head at record+0, context word +10h zeroed, the
 * record linked into FS:0), new 47749c (blocks in the arena, overlapping
 * each other in some cases, or 0; never 0 for the light table, whose null
 * path faults in the original), the light table constructor 406e44,
 * pool_create 471ad8, new[] 4774f0 and the smoke-ball set-up 408fb8.
 * Compared: the full return pair, ECX, FS:0, the ordered calls with their
 * argument slots (and the bridge's slot count), entry EAX/EDX/ECX and the
 * record's context word (read through FS:0), the arena, the image globals
 * (the light table, the pools and lists of the three callbacks, the
 * construction counter pointer 6cbee4) and the callee counter deltas. The
 * stubs change what the originals read after a call (406e44 may move the
 * counter pointer and the counter, the pool and new[] stubs write into the
 * blocks), so the order of reads and writes is observable.
 *
 * Then installed 31/a7 poison probes and a real-callee pass: the real
 * 49630c and pool_create (nested, in rotating original/replaced modes; it
 * refuses compare) over a bump allocator for new/new[], with 406e44 and
 * 408fb8 stubbed, the hook under test original or replaced against a run
 * without it. Identifiers use the prefix msp_/MSP_. */
typedef struct msp_record { uint32_t id,slots,args[3],regs[3],context,eax,edx; } msp_record;
enum {
    MSP_SIZE=0x8000, MSP_SMALL=0x1000, MSP_MAX=16, MSP_IDS=6,
    MSP_TABLE=0x100, MSP_HEAP=0x600, MSP_COUNTER=0x900, MSP_COUNTER2=0x910, MSP_BLOCK=0xa00, MSP_BUMP=0x1000,
    MSP_GLOBALS=12+8+8+4
};
#define MSP_B ((uint32_t)(uintptr_t)msp_arena)
#define MSP_A(o) (MSP_B+(uint32_t)(o))
static BYTE msp_arena[MSP_SIZE],msp_before[MSP_SIZE],msp_after[MSP_SIZE];
static msp_record msp_log[MSP_MAX];
static unsigned msp_calls,msp_span=MSP_SMALL,msp_real,msp_paths[MSP_IDS];
static uint32_t msp_seed,msp_stream,msp_bump;
__attribute__((used)) static uint32_t msp_ecx;
/* 0 49630c, 1 new 47749c, 2 406e44, 3 pool_create 471ad8, 4 new[] 4774f0,
 * 5 408fb8, and the slots each original call site pushes. */
static const uint32_t msp_addresses[MSP_IDS]={0x49630c,0x47749c,0x406e44,0x471ad8,0x4774f0,0x408fb8};
static const unsigned msp_counts[MSP_IDS]={0,1,1,3,1,0};
static uint32_t msp_next(void){
    uint32_t z=msp_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *msp_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static uint32_t msp_fs0(void){uint32_t v;__asm__ volatile("movl %%fs:0,%0":"=r"(v));return v;}
static void msp_reset(void){msp_calls=0;msp_stream=msp_seed;msp_bump=MSP_A(MSP_BUMP);}
/* What the stubs answer and change. */
static uint32_t msp_answer(unsigned id,const uint32_t *a,uint32_t eax,uint32_t pick){
    const uint32_t b=MSP_B;
    switch(id){
    case 1: /* new: the light table never 0; otherwise 0 one time in four */
        if(msp_real){const uint32_t p=msp_bump;msp_bump+=(a[0]+15u)&~7u;return p;}
        if(a[0]==0x404u)return b+MSP_TABLE+4u*(pick%3);
        if(pick%4==0)return 0;
        {static const unsigned offsets[]={MSP_HEAP,MSP_HEAP+0x40,MSP_HEAP+0x42,MSP_HEAP+0x80,MSP_TABLE+0x0c,MSP_TABLE+0x12};
         return b+offsets[pick/4%6];}
    case 2: /* 406e44: may move the counter pointer, change the counter, scribble on the table */
        if(pick%3==0)put32(mapped+0x2cbee4,b+(pick>>4&1?MSP_COUNTER2:MSP_COUNTER));
        if(pick%5==0)put32(msp_host(get32(mapped+0x2cbee4)),msp_next());
        if(pick%7==0)put32(msp_host(a[0]+0x10),msp_next());
        return a[0];
    case 3: /* pool_create: writes into the header */
        put16(msp_host(a[0]),(uint16_t)a[1]);put16(msp_host(a[0]+4),(uint16_t)a[2]);put32(msp_host(a[0]+6),msp_next());
        return a[0];
    case 4: /* new[]: a block, or one over the list itself */
        if(msp_real){const uint32_t p=msp_bump;msp_bump+=(a[0]+15u)&~7u;return p;}
        return pick%5==0?b+MSP_HEAP+0x40:b+MSP_BLOCK;
    }
    return eax;
}
__attribute__((used,noinline)) static uint64_t __cdecl msp_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(msp_calls==MSP_MAX){fail("mission_pools call overflow");ExitProcess(1);}
    msp_record *v=msp_log+msp_calls++;memset(v,0,sizeof(*v));v->id=id;
    v->slots=weapons_bridge_slots(a,msp_counts[id]);
    for(unsigned k=0;k<msp_counts[id];++k)v->args[k]=a[k];
    for(unsigned j=0;j<3;++j)v->regs[j]=regs[j];
    if(id)v->context=get16(msp_host(msp_fs0()+16u));
    uint32_t eax=msp_next(),edx=msp_next(),pick=msp_next();msp_ecx=msp_next();
    if(id==0)eax=msp_fs0(); /* the record stub returns the real FS:0 head */
    else eax=msp_answer(id,a,eax,pick);
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define MSP_STUB(n) __attribute__((naked)) static void msp_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _msp_stub_impl\n\taddl $24,%esp\n\tmovl _msp_ecx,%ecx\n\tret\n\t");}
MSP_STUB(1) MSP_STUB(2) MSP_STUB(3) MSP_STUB(4) MSP_STUB(5)
#undef MSP_STUB
/* 49630c: logs like the other stubs (EDX and ECX come back unchanged, as
 * the original's do), then links a record at EBP + [descriptor+4] into
 * FS:0 with the saved head at record+0 and a zero context word. */
__attribute__((naked)) static void msp_stub_0(void){__asm__ volatile(
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
    "pushl $0\n\tcalll _msp_stub_impl\n\taddl $12,%esp\n\t"
    "popl %ecx\n\tmovl 4(%ecx),%ecx\n\taddl %ebp,%ecx\n\tmovl %eax,(%ecx)\n\tmovw $0,16(%ecx)\n\tmovl %ecx,%fs:0\n\t"
    "popl %edx\n\tpopl %ecx\n\tret\n\t");}
static void *msp_stubs[MSP_IDS]={msp_stub_0,msp_stub_1,msp_stub_2,msp_stub_3,msp_stub_4,msp_stub_5};
static int msp_patch(BYTE saved[MSP_IDS][5],int active[MSP_IDS],uint32_t which,int on){
    for(unsigned i=0;i<MSP_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(msp_addresses[i]-0x400000u,msp_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(msp_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
/* Direct callees of each hook (bit i = msp_addresses[i]). */
static uint32_t msp_which(unsigned index){
    if(index==ES2_HOOK_INDEX_light_pools)return 0x1fu;
    if(index==ES2_HOOK_INDEX_smoke_pools)return 0x3bu;
    return 0x1bu;
}
/* The light table, pool and list (4a95fe..4a960b), the smoke and
 * projectile pools and lists, the construction counter pointer. */
static const struct { uint32_t rva,size; } msp_ranges[]={{0xa95fe,12},{0xa9684,8},{0xa96c2,8},{0x2cbee4,4}};
static void msp_globals(BYTE *out,int restore){
    for(unsigned i=0;i<sizeof(msp_ranges)/sizeof(msp_ranges[0]);++i){
        if(restore)memcpy(mapped+msp_ranges[i].rva,out,msp_ranges[i].size);else memcpy(out,mapped+msp_ranges[i].rva,msp_ranges[i].size);
        out+=msp_ranges[i].size;
    }
}
static int msp_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<MSP_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
static void msp_build(unsigned n,uint32_t r[3],int real){
    msp_seed=random32();msp_stream=msp_seed^n;msp_real=real;
    for(unsigned i=0;i<3;++i)r[i]=msp_next();
    msp_span=real?MSP_SIZE:MSP_SMALL;
    for(unsigned i=0;i<msp_span;++i)msp_arena[i]=(BYTE)msp_next();
    put32(mapped+0x2cbee4,MSP_A(MSP_COUNTER));
    for(unsigned i=0;i<sizeof(msp_ranges)/sizeof(msp_ranges[0])-1;++i)
        for(unsigned j=0;j<msp_ranges[i].size;++j)mapped[msp_ranges[i].rva+j]=(BYTE)msp_next();
    msp_stream=msp_seed;   /* the stubs' stream restarts here in every run */
}
/* Calls target with three argument slots and EAX/EDX/ECX = r; out receives
 * EAX, EDX and ECX after the call. */
__attribute__((naked,noinline)) static void msp_invoke(void *target,const uint32_t *r,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "pushl $0x5a5a5a5a\n\tpushl $0x5a5a5a5a\n\tpushl $0x5a5a5a5a\n\t"
        "movl 12(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tcall *8(%ebp)\n\t"
        "movl 16(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* The core with the adapter's graph and frame; FS:0 restored to the
 * returned head as the adapter does. */
static uint64_t msp_core(unsigned index,const uint32_t *r,uint32_t *ecx){
    BYTE scratch[ES2_STAGE_POOLS_SCRATCH]={0};es2_weapon_graph g=weapons_graph(mapped,scratch,index);
    weapons_impact_context c={(uint32_t)(uintptr_t)mapped,(uint32_t)(uintptr_t)(scratch+ES2_STAGE_POOLS_FRAME)};
    g.call=weapons_impact_guest_call;g.context=&c;
    const es2_guest_memory *m=&math_native_memory;uint64_t v;
    if(index==ES2_HOOK_INDEX_light_pools)v=es2_light_pools_registers(m,&g,r,ecx);
    else if(index==ES2_HOOK_INDEX_smoke_pools)v=es2_smoke_pools_registers(m,&g,r,ecx);
    else v=es2_projectile_pools_registers(m,&g,r,ecx);
    uint32_t head=(uint32_t)v;__asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");return v;
}
static int msp_report(const char *what,unsigned n,const uint32_t want[3],const uint32_t got[3],unsigned calls,const msp_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x calls %u/%u\n",current_function,n,what,
        want[0],got[0],want[1],got[1],want[2],got[2],calls,msp_calls);
    for(unsigned j=0;j<calls&&j<msp_calls&&expected;++j)if(memcmp(expected+j,msp_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(msp_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<msp_span;++j)if(msp_after[j]!=msp_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,msp_after[j],msp_arena[j]);break;}
    return 1;
}
static unsigned msp_expected_hits(const callee_counter *c){
    unsigned hits=0;for(unsigned j=0;j<msp_calls;++j)if(msp_addresses[msp_log[j].id]==c->key)++hits;return hits;
}
static int msp_compare(unsigned index,unsigned n,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[MSP_GLOBALS],after[MSP_GLOBALS],actual[MSP_GLOBALS];static msp_record expected[MSP_MAX];uint32_t want[3],got[3];
    const uint32_t head=msp_fs0();const uint32_t none[6]={0};
    memcpy(msp_before,msp_arena,msp_span);msp_globals(before,0);
    msp_reset();msp_invoke(mapped+d->rva,r,want);const unsigned calls=msp_calls;memcpy(expected,msp_log,calls*sizeof(*expected));
    memcpy(msp_after,msp_arena,msp_span);msp_globals(after,0);
    if(msp_fs0()!=head)return fail("mission_pools original left FS:0 changed");
    for(unsigned j=0;j<calls;++j)++msp_paths[expected[j].id];
    /* the adapter (the framework's entry: no argument slots) */
    memcpy(msp_arena,msp_before,msp_span);msp_globals(before,1);msp_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];
    if(count>8)return fail("mission_pools counter table too long");
    for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    const uint64_t pair=x->replacement_context(none,d->user,r);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);got[2]=want[2];
    msp_globals(actual,0);
    if(msp_fs0()!=head)return fail("mission_pools adapter left FS:0 changed");
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=msp_expected_hits(c+i))return fail("mission_pools callee counter delta");
    if(want[0]!=got[0]||want[1]!=got[1]||calls!=msp_calls||memcmp(expected,msp_log,calls*sizeof(*expected))||
       memcmp(msp_after,msp_arena,msp_span)||msp_globals_differ(after,actual))
        return msp_report("adapter",n,want,got,calls,expected);
    /* the core with the ECX it reports */
    memcpy(msp_arena,msp_before,msp_span);msp_globals(before,1);msp_reset();
    got[2]=~want[2];
    const uint64_t core=msp_core(index,r,&got[2]);got[0]=(uint32_t)core;got[1]=(uint32_t)(core>>32);msp_globals(actual,0);
    if(msp_fs0()!=head)return fail("mission_pools core left FS:0 changed");
    /* With the hook installed (integration pass) the "original" run is the
       hook, whose ECX is the framework's; ECX is compared otherwise. */
    const int ecx_known=!rng_hooks[index].installed;
    if(want[0]!=got[0]||want[1]!=got[1]||(ecx_known&&want[2]!=got[2])||calls!=msp_calls||memcmp(expected,msp_log,calls*sizeof(*expected))||
       memcmp(msp_after,msp_arena,msp_span)||msp_globals_differ(after,actual))
        return msp_report("core ECX",n,want,got,calls,expected);
    msp_globals(before,1);
    return 0;
}
static int msp_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;const uint32_t none[6]={0};
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t r[3],want[3]={0},got[3]={0},pattern=poison?0xa7a7a7a7u:0x31313131u;const uint32_t head=msp_fs0();msp_build(n*37u+11u,r,0);
        BYTE before[MSP_GLOBALS],after[MSP_GLOBALS],actual[MSP_GLOBALS],expected_fpu[108],actual_fpu[108];static msp_record expected[MSP_MAX];
        memcpy(msp_before,msp_arena,msp_span);msp_globals(before,0);
        msp_reset();sg_poison_call(mapped+d->rva,none,pattern,want);es2_hook_fpu_save(expected_fpu);
        const unsigned calls=msp_calls;memcpy(expected,msp_log,calls*sizeof(*expected));memcpy(msp_after,msp_arena,msp_span);msp_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("mission_pools poison install");
        memcpy(msp_arena,msp_before,msp_span);msp_globals(before,1);msp_reset();sg_poison_call(mapped+d->rva,none,pattern,got);
        es2_hook_fpu_save(actual_fpu);msp_globals(actual,0);
        if(msp_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||calls!=msp_calls||memcmp(expected,msp_log,calls*sizeof(*expected))||
           memcmp(msp_after,msp_arena,msp_span)||msp_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||
           memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=msp_report(fast?"installed fast":"installed preserving",n,want,got,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("mission_pools poison uninstall");msp_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, pair/trace/arena/globals/FS:0\n",d->name,total);
    return status;
}
/* Real callees: 49630c and pool_create (nested, rotating original and
 * replaced; compare is refused) run for real, new and new[] are a bump
 * allocator in the arena, 406e44 and 408fb8 stay stubs. */
static int msp_real_pass(unsigned index){
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[2]={0};
    BYTE saved[MSP_IDS][5];int active[MSP_IDS];const uint32_t none[6]={0};
    if(msp_patch(saved,active,(1u<<1)|(1u<<2)|(1u<<4)|(1u<<5),1))return 1;
    static msp_record expected[MSP_MAX];
    for(unsigned mode=0;mode<4&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t r[3],want[3]={0},got[3]={0},pattern=n&1?0xa7a7a7a7u:0x31313131u;const uint32_t head=msp_fs0();msp_build(n*13u+mode,r,1);
        BYTE before[MSP_GLOBALS],after[MSP_GLOBALS],actual[MSP_GLOBALS];
        memcpy(msp_before,msp_arena,msp_span);msp_globals(before,0);
        es2_hook hk[2];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        o.mode=mode&1?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(hk,function_hook_descriptor(ES2_HOOK_INDEX_pool_create),&o,function_hook_extension(ES2_HOOK_INDEX_pool_create)))
            return fail("mission_pools nested install");
        msp_reset();sg_poison_call(mapped+d->rva,none,pattern,want);
        const unsigned calls=msp_calls;memcpy(expected,msp_log,calls*sizeof(*expected));
        memcpy(msp_after,msp_arena,msp_span);msp_globals(after,0);
        o.mode=mode<2?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+1,d,&o,function_hook_extension(index)))return fail("mission_pools caller install");
        memcpy(msp_arena,msp_before,msp_span);msp_globals(before,1);msp_reset();
        sg_poison_call(mapped+d->rva,none,pattern,got);msp_globals(actual,0);
        if(msp_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||calls!=msp_calls||memcmp(expected,msp_log,calls*sizeof(*expected))||
           memcmp(msp_after,msp_arena,msp_span)||msp_globals_differ(after,actual)){
            fprintf(stderr,"real mode=%u\n",mode);status=msp_report("real",n,want,got,calls,expected);
        }
        for(unsigned j=0;j<2;++j){entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("mission_pools real compare mismatch");
            if(!es2_hook_uninstall(hk+j))return fail("mission_pools real uninstall");}
        msp_globals(before,1);++total;
    }
    if(msp_patch(saved,active,0,0))return 1;
    msp_real=0;
    if(!status&&(!entered[0]||!entered[1])){fprintf(stderr,"FAIL: %s real pass: pool_create entered %llu, caller %llu\n",d->name,
        (unsigned long long)entered[0],(unsigned long long)entered[1]);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases, real 49630c, pool_create nested original/replaced (entered %llu), hook original/replaced, "
        "31/a7 stack, pair, calls, arena, globals and FS:0\n",d->name,total,(unsigned long long)entered[0]);
    return status;
}
static int mission_pools_cases(unsigned index,unsigned limit){
    current_function=function_hook_descriptor(index)->name;
    const uint32_t which=msp_which(index);
    BYTE saved[MSP_IDS][5],old_globals[MSP_GLOBALS];int active[MSP_IDS];
    msp_globals(old_globals,0);
    if(msp_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_MISSION_POOLS_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);memset(msp_paths,0,sizeof(msp_paths));
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t r[3];msp_build(n,r,0);status=msp_compare(index,n,r);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    msp_globals(old_globals,1);
    if(!limit&&!status)status=msp_installed_probes(index,64);
    if(msp_patch(saved,active,which,0))return 1;
    msp_globals(old_globals,1);
    if(!limit&&!status)status=msp_real_pass(index);
    msp_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases (stub calls 49630c %u, new %u, 406e44 %u, pool_create %u, new[] %u, 408fb8 %u); "
        "full return pair and ECX, ordered calls with entry registers, slots and context word, arena/globals/FS:0\n",
        function_hook_descriptor(index)->name,cases,msp_paths[0],msp_paths[1],msp_paths[2],msp_paths[3],msp_paths[4],msp_paths[5]);
    return status;
}
