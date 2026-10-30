/* stage_flush group: the eleven stage-5 callbacks and 408f84
 * (docs/reverse-engineering/main_loop.md, "the stage-5 flushes and the manager
 * slot methods").
 *
 * Direct cases run the original body (EAX/EDX/ECX at entry from the case,
 * EAX/EDX/ECX after it captured), the adapter and the core (with the ECX it
 * reports) against logging stubs for removal_flush 478644 and, under
 * flush_smoke, 408f84: ordered calls with the bridge's slot count, the
 * argument slots and entry EAX/EDX/ECX. The stubs return random registers
 * and in one call of three rewrite one of the twelve pool/list pairs or
 * 6b4f3c, so a re-read or a late read would show. Compared: the pair, ECX
 * (core), the calls, the globals (the 24 pool/list dwords and 6b4f3c) and
 * the callee counter deltas. Then installed 31/a7 poison probes; a
 * real-callee pass over the removal group's consistent pool (rmv_build:
 * real 49630c, 496406 and pool_free, the destructor a logging stub) with
 * removal_flush nested original/replaced (under flush_smoke also
 * flush_smoke_balls) and the hook original/replaced against a run with
 * only the nested hooks; and, for flush_buildings, a dispatch pass: the
 * real 401d94(5) over a table of the eleven callbacks with removal_flush
 * stubbed, every flush hook and stage_dispatch original or replaced,
 * against the all-original run (ECX at each removal_flush masked: the hooks
 * return the entry ECX, dead at every caller). Identifiers: sfl_/SFL_. */
typedef struct sfl_record { uint32_t id,slots,args[2],regs[3]; } sfl_record;
enum { SFL_MAX=32, SFL_IDS=2, SFL_GLOBALS=ES2_STAGE_FLUSHES*8+4, SFL_ENTRIES=11 };
static sfl_record sfl_log[SFL_MAX];
static unsigned sfl_calls,sfl_mask_ecx,sfl_paths[SFL_IDS];
static uint32_t sfl_seed,sfl_stream;
__attribute__((used)) static uint32_t sfl_ecx;
/* 0 removal_flush 478644, 1 408f84 (flush_smoke's second call). */
static const uint32_t sfl_addresses[SFL_IDS]={0x478644,0x408f84};
static const unsigned sfl_counts[SFL_IDS]={2,0};
static uint32_t sfl_next(void){
    uint32_t z=sfl_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static uint32_t sfl_fs0(void){uint32_t v;__asm__ volatile("movl %%fs:0,%0":"=r"(v));return v;}
static void sfl_reset(void){sfl_calls=0;sfl_stream=sfl_seed;}
/* The core's flush index of a hook. */
static unsigned sfl_index(unsigned index){
    const uint32_t va=function_hook_descriptor(index)->rva+0x400000u;
    for(unsigned k=0;k<ES2_STAGE_FLUSHES;++k)if(es2_stage_flush_va(k)==va)return k;
    return ES2_STAGE_FLUSHES;
}
static BYTE *sfl_global(unsigned k,unsigned list){return mapped+es2_stage_flush_pool(k)-0x400000u+4u*list;}
static void sfl_globals(BYTE *out,int restore){
    for(unsigned k=0;k<ES2_STAGE_FLUSHES;++k){
        if(restore)memcpy(sfl_global(k,0),out+8*k,8);else memcpy(out+8*k,sfl_global(k,0),8);
    }
    if(restore)memcpy(mapped+0x2b4f3c,out+8*ES2_STAGE_FLUSHES,4);else memcpy(out+8*ES2_STAGE_FLUSHES,mapped+0x2b4f3c,4);
}
static int sfl_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<SFL_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
__attribute__((used,noinline)) static uint64_t __cdecl sfl_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(sfl_calls==SFL_MAX){fail("stage_flush call overflow");ExitProcess(1);}
    sfl_record *v=sfl_log+sfl_calls++;memset(v,0,sizeof(*v));v->id=id;
    v->slots=weapons_bridge_slots(a,sfl_counts[id]);
    for(unsigned k=0;k<sfl_counts[id];++k)v->args[k]=a[k];
    for(unsigned j=0;j<3;++j)v->regs[j]=regs[j];
    if(sfl_mask_ecx)v->regs[2]=0;
    const uint32_t eax=sfl_next(),edx=sfl_next(),pick=sfl_next();sfl_ecx=sfl_next();
    if(pick%3==0){
        const unsigned k=(pick>>8)%(ES2_STAGE_FLUSHES+1u);
        if(k==ES2_STAGE_FLUSHES)put32(mapped+0x2b4f3c,sfl_next());else put32(sfl_global(k,pick>>16&1u),sfl_next());
    }
    return (uint64_t)edx<<32|eax;
}
#define SFL_STUB(n) __attribute__((naked)) static void sfl_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _sfl_stub_impl\n\taddl $24,%esp\n\tmovl _sfl_ecx,%ecx\n\tret\n\t");}
SFL_STUB(0) SFL_STUB(1)
#undef SFL_STUB
static void *sfl_stubs[SFL_IDS]={sfl_stub_0,sfl_stub_1};
static int sfl_patch(BYTE saved[SFL_IDS][5],int active[SFL_IDS],uint32_t which,int on){
    for(unsigned i=0;i<SFL_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(sfl_addresses[i]-0x400000u,sfl_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(sfl_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
static uint32_t sfl_which(unsigned index){return index==ES2_HOOK_INDEX_flush_smoke?3u:1u;}
/* Random pool/list globals (some equal, some zero) and 6b4f3c; r = entry
 * EAX/EDX/ECX. */
static void sfl_build(unsigned n,uint32_t r[3]){
    sfl_seed=random32();sfl_stream=sfl_seed^n;
    for(unsigned i=0;i<3;++i)r[i]=sfl_next();
    for(unsigned k=0;k<ES2_STAGE_FLUSHES;++k)for(unsigned j=0;j<2;++j){
        const uint32_t pick=sfl_next();put32(sfl_global(k,j),pick%7==0?0:pick%11==0?r[pick>>8&1]:sfl_next());
    }
    put32(mapped+0x2b4f3c,sfl_next());
    sfl_stream=sfl_seed;   /* the stubs' stream restarts here in every run */
}
/* Calls target with four argument slots and EAX/EDX/ECX = r; out receives
 * EAX, EDX and ECX after the call. */
__attribute__((naked,noinline)) static void sfl_invoke(void *target,const uint32_t *r,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "pushl $0x5a5a5a5a\n\tpushl $0x5a5a5a5a\n\tpushl $0x5a5a5a5a\n\tpushl $0x5a5a5a5a\n\t"
        "movl 12(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tcall *8(%ebp)\n\t"
        "movl 16(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static int sfl_report(const char *what,unsigned n,const uint32_t want[3],const uint32_t got[3],unsigned calls,const sfl_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x calls %u/%u\n",current_function,n,what,
        want[0],got[0],want[1],got[1],want[2],got[2],calls,sfl_calls);
    for(unsigned j=0;j<calls&&j<sfl_calls&&expected;++j)if(memcmp(expected+j,sfl_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(sfl_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    return 1;
}
static unsigned sfl_expected_hits(const callee_counter *c){
    unsigned hits=0;for(unsigned j=0;j<sfl_calls;++j)if(sfl_addresses[sfl_log[j].id]==c->key)++hits;return hits;
}
static int sfl_compare(unsigned index,unsigned n,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[SFL_GLOBALS],after[SFL_GLOBALS],actual[SFL_GLOBALS];static sfl_record expected[SFL_MAX];uint32_t want[3],got[3];
    const uint32_t none[6]={0};
    sfl_globals(before,0);
    sfl_reset();sfl_invoke(mapped+d->rva,r,want);const unsigned calls=sfl_calls;memcpy(expected,sfl_log,calls*sizeof(*expected));
    sfl_globals(after,0);
    for(unsigned j=0;j<calls;++j)++sfl_paths[expected[j].id];
    /* the adapter (the framework's entry: no argument slot) */
    sfl_globals(before,1);sfl_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[4];
    if(count>4)return fail("stage_flush counter table too long");
    for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    const uint64_t pair=x->replacement_context(none,d->user,r);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);got[2]=want[2];
    sfl_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=sfl_expected_hits(c+i))return fail("stage_flush callee counter delta");
    if(want[0]!=got[0]||want[1]!=got[1]||calls!=sfl_calls||memcmp(expected,sfl_log,calls*sizeof(*expected))||sfl_globals_differ(after,actual))
        return sfl_report("adapter",n,want,got,calls,expected);
    /* the core with the ECX it reports */
    sfl_globals(before,1);sfl_reset();
    es2_weapon_graph g=weapons_graph(mapped,NULL,index);
    const uint64_t core=es2_stage_flush_registers(&math_native_memory,&g,sfl_index(index),r,&got[2]);
    got[0]=(uint32_t)core;got[1]=(uint32_t)(core>>32);sfl_globals(actual,0);
    /* With the hook installed (integration pass) the "original" run is the
       hook, whose ECX is the framework's; ECX is compared otherwise. */
    const int ecx_known=!rng_hooks[index].installed;
    if(want[0]!=got[0]||want[1]!=got[1]||(ecx_known&&want[2]!=got[2])||calls!=sfl_calls||memcmp(expected,sfl_log,calls*sizeof(*expected))||
       sfl_globals_differ(after,actual))
        return sfl_report("core ECX",n,want,got,calls,expected);
    sfl_globals(before,1);
    return 0;
}
static int sfl_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;const uint32_t none[6]={0};
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t r[3],want[3]={0},got[3]={0},pattern=poison?0xa7a7a7a7u:0x31313131u;sfl_build(n*37u+11u,r);
        BYTE before[SFL_GLOBALS],after[SFL_GLOBALS],actual[SFL_GLOBALS],expected_fpu[108],actual_fpu[108];static sfl_record expected[SFL_MAX];
        sfl_globals(before,0);
        sfl_reset();sg_poison_call(mapped+d->rva,none,pattern,want);es2_hook_fpu_save(expected_fpu);
        const unsigned calls=sfl_calls;memcpy(expected,sfl_log,calls*sizeof(*expected));sfl_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("stage_flush poison install");
        sfl_globals(before,1);sfl_reset();sg_poison_call(mapped+d->rva,none,pattern,got);
        es2_hook_fpu_save(actual_fpu);sfl_globals(actual,0);
        if(want[0]!=got[0]||want[1]!=got[1]||calls!=sfl_calls||memcmp(expected,sfl_log,calls*sizeof(*expected))||
           sfl_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=sfl_report(fast?"installed fast":"installed preserving",n,want,got,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("stage_flush poison uninstall");sfl_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, pair/trace/globals\n",d->name,total);
    return status;
}
/* Real callees: the removal group's consistent pool (rmv_build, real
 * 49630c, 496406 and pool_free; the destructor slot is rmv's logging stub)
 * behind the pool/list globals of the hook (and, under flush_smoke, of
 * 408f84, which then finds the list emptied). Four modes: removal_flush
 * (and flush_smoke_balls) nested original/replaced, the hook
 * original/replaced; against a run with only the nested hooks. */
static int sfl_real_pass(unsigned index){
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[3]={0};
    const unsigned k=sfl_index(index);const int smoke=index==ES2_HOOK_INDEX_flush_smoke;const uint32_t none[6]={0};
    static rmv_record expected[RMV_MAX];
    rmv_real=1;
    for(unsigned mode=0;mode<4&&!status;++mode)for(unsigned n=0;n<256&&!status;++n){
        uint32_t a[6],r[3],want[2],got[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;const uint32_t head=sfl_fs0();
        BYTE before[SFL_GLOBALS],after[SFL_GLOBALS],actual[SFL_GLOBALS],rg_before[RMV_GLOBALS],rg_after[RMV_GLOBALS],rg_actual[RMV_GLOBALS];
        sfl_globals(before,0);rmv_globals(rg_before,0);
        rmv_build(ES2_HOOK_INDEX_removal_flush,n,a,r,1);
        put32(sfl_global(k,0),a[1]);put32(sfl_global(k,1),a[0]);
        if(smoke){put32(sfl_global(4,0),a[1]);put32(sfl_global(4,1),a[0]);}
        BYTE start[SFL_GLOBALS],rg_start[RMV_GLOBALS];sfl_globals(start,0);rmv_globals(rg_start,0);
        memcpy(rmv_before,rmv_arena,RMV_SIZE);
        es2_hook hk[3];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        o.mode=mode&1?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(hk,function_hook_descriptor(ES2_HOOK_INDEX_removal_flush),&o,function_hook_extension(ES2_HOOK_INDEX_removal_flush)))
            return fail("stage_flush nested install");
        if(smoke&&!weapons_install_hook(hk+1,function_hook_descriptor(ES2_HOOK_INDEX_flush_smoke_balls),&o,
            function_hook_extension(ES2_HOOK_INDEX_flush_smoke_balls)))return fail("stage_flush nested smoke install");
        rmv_reset();sg_poison_call(mapped+d->rva,none,pattern,want);
        const unsigned calls=rmv_calls;memcpy(expected,rmv_log,calls*sizeof(*expected));
        memcpy(rmv_after,rmv_arena,RMV_SIZE);sfl_globals(after,0);rmv_globals(rg_after,0);
        o.mode=mode<2?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+2,d,&o,function_hook_extension(index)))return fail("stage_flush caller install");
        memcpy(rmv_arena,rmv_before,RMV_SIZE);sfl_globals(start,1);rmv_globals(rg_start,1);rmv_reset();
        sg_poison_call(mapped+d->rva,none,pattern,got);sfl_globals(actual,0);rmv_globals(rg_actual,0);
        if(sfl_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||calls!=rmv_calls||memcmp(expected,rmv_log,calls*sizeof(*expected))||
           memcmp(rmv_after,rmv_arena,RMV_SIZE)||sfl_globals_differ(after,actual)||memcmp(rg_after,rg_actual,RMV_GLOBALS)){
            fprintf(stderr,"FAIL: %s case %u (real mode=%u) pair %08x%08x/%08x%08x calls %u/%u head %08x/%08x\n",d->name,n,mode,
                want[1],want[0],got[1],got[0],calls,rmv_calls,head,sfl_fs0());
            for(unsigned j=0;j<RMV_SIZE;++j)if(rmv_after[j]!=rmv_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,rmv_after[j],rmv_arena[j]);break;}
            status=1;
        }
        for(unsigned j=0;j<3;++j){entered[j]+=hk[j].calls;
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("stage_flush real uninstall");}
        sfl_globals(before,1);rmv_globals(rg_before,1);++total;
    }
    rmv_real=0;
    if(!status&&(!entered[0]||!entered[2]||(smoke&&!entered[1]))){fprintf(stderr,"FAIL: %s real pass: removal_flush entered %llu, "
        "flush_smoke_balls %llu, hook %llu\n",d->name,(unsigned long long)entered[0],(unsigned long long)entered[1],(unsigned long long)entered[2]);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases, real 49630c/496406/pool_free over a consistent pool, removal_flush%s nested "
        "original/replaced (entered %llu), hook original/replaced, 31/a7 stack, pair, destructor calls, arena, globals and FS:0\n",
        d->name,total,smoke?" and flush_smoke_balls":"",(unsigned long long)entered[0]);
    return status;
}
/* The dispatch pass: the real 401d94(5) over a table of the eleven
 * registered flushes (stage word 5, some entries repeated or of another
 * stage), removal_flush stubbed. The all-original run against the same
 * table with every flush hook replaced and stage_dispatch original (even
 * cases) or replaced (odd): ordered removal_flush calls with arguments
 * and EAX/EDX (ECX masked), globals and the returned pair; every flush hook
 * in the table must be entered. */
static int sfl_dispatch_pass(void){
    static const unsigned hooks[ES2_STAGE_FLUSHES]={ES2_HOOK_INDEX_flush_buildings,ES2_HOOK_INDEX_flush_lights,
        ES2_HOOK_INDEX_flush_explosions,ES2_HOOK_INDEX_flush_debris,ES2_HOOK_INDEX_flush_smoke_balls,ES2_HOOK_INDEX_flush_smoke,
        ES2_HOOK_INDEX_flush_attached,ES2_HOOK_INDEX_flush_meteors,ES2_HOOK_INDEX_flush_projectiles,ES2_HOOK_INDEX_flush_mechs,
        ES2_HOOK_INDEX_flush_flyers,ES2_HOOK_INDEX_flush_fires};
    BYTE saved[SFL_IDS][5],table[2+6*16];int active[SFL_IDS],status=0;unsigned total=0;uint64_t entered[ES2_STAGE_FLUSHES+1]={0};
    memcpy(table,mapped+0x97074,2);memcpy(table+2,mapped+0xa804c,6*16);
    if(sfl_patch(saved,active,1u,1))return 1;
    sfl_mask_ecx=1;static sfl_record expected[SFL_MAX];
    for(unsigned n=0;n<512&&!status;++n){
        uint32_t r[3],want[2],got[2],a[6]={5,0,0,0,0,0},pattern=n&2?0xa7a7a7a7u:0x31313131u;sfl_build(n^0x5000u,r);
        unsigned count=0;
        for(unsigned i=0;i<16;++i){
            const unsigned k=i<SFL_ENTRIES?(i<4?i:i+1):sfl_next()%ES2_STAGE_FLUSHES;
            if(k==4)continue;   /* 408f84 is not registered */
            BYTE *e=mapped+0xa804c+6*count++;put32(e,es2_stage_flush_va(k));put16(e+4,(uint16_t)(sfl_next()%8?5:6));
        }
        put16(mapped+0x97074,(uint16_t)count);
        BYTE before[SFL_GLOBALS],after[SFL_GLOBALS],actual[SFL_GLOBALS];sfl_globals(before,0);sfl_stream=sfl_seed;
        sfl_reset();sg_poison_call(mapped+0x1d94,a,pattern,want);
        const unsigned calls=sfl_calls;memcpy(expected,sfl_log,calls*sizeof(*expected));sfl_globals(after,0);
        es2_hook hk[ES2_STAGE_FLUSHES+1];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;
        o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        for(unsigned j=0;j<ES2_STAGE_FLUSHES;++j)if(!weapons_install_hook(hk+j,function_hook_descriptor(hooks[j]),&o,function_hook_extension(hooks[j])))
            return fail("stage_flush dispatch install");
        o.mode=n&1?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(hk+ES2_STAGE_FLUSHES,function_hook_descriptor(ES2_HOOK_INDEX_stage_dispatch),&o,
            function_hook_extension(ES2_HOOK_INDEX_stage_dispatch)))return fail("stage_flush dispatcher install");
        sfl_globals(before,1);sfl_reset();sg_poison_call(mapped+0x1d94,a,pattern,got);sfl_globals(actual,0);
        if(want[0]!=got[0]||want[1]!=got[1]||calls!=sfl_calls||memcmp(expected,sfl_log,calls*sizeof(*expected))||sfl_globals_differ(after,actual)){
            const uint32_t w[3]={want[0],want[1],0},g3[3]={got[0],got[1],0};status=sfl_report("dispatch",n,w,g3,calls,expected);
        }
        for(unsigned j=0;j<=ES2_STAGE_FLUSHES;++j){entered[j]+=hk[j].calls;if(!es2_hook_uninstall(hk+j))return fail("stage_flush dispatch uninstall");}
        sfl_globals(before,1);++total;
    }
    sfl_mask_ecx=0;
    if(sfl_patch(saved,active,1u,0))return 1;
    memcpy(mapped+0x97074,table,2);memcpy(mapped+0xa804c,table+2,6*16);
    for(unsigned j=0;j<=ES2_STAGE_FLUSHES&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: dispatch pass: %s never entered\n",
        j<ES2_STAGE_FLUSHES?function_hook_descriptor(hooks[j])->name:"stage_dispatch");status=1;}
    if(!status)printf("PASS: stage_flush %u dispatch cases: real 401d94(5) over the table, the twelve flush hooks replaced, stage_dispatch "
        "original/replaced, ordered removal_flush calls (ECX masked), pair and globals\n",total);
    return status;
}
static int stage_flush_cases(unsigned index,unsigned limit){
    current_function=function_hook_descriptor(index)->name;
    const uint32_t which=sfl_which(index);
    BYTE saved[SFL_IDS][5],old_globals[SFL_GLOBALS];int active[SFL_IDS];
    sfl_globals(old_globals,0);
    if(sfl_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_STAGE_FLUSH_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);memset(sfl_paths,0,sizeof(sfl_paths));
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t r[3];sfl_build(n,r);status=sfl_compare(index,n,r);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    sfl_globals(old_globals,1);
    if(!limit&&!status)status=sfl_installed_probes(index,64);
    if(sfl_patch(saved,active,which,0))return 1;
    sfl_globals(old_globals,1);
    if(!limit&&!status)status=sfl_real_pass(index);
    if(!limit&&!status&&index==ES2_HOOK_INDEX_flush_buildings)status=sfl_dispatch_pass();
    sfl_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases (stub calls 478644 %u, 408f84 %u); full return pair and ECX, ordered calls with entry "
        "registers and slots, globals\n",function_hook_descriptor(index)->name,cases,sfl_paths[0],sfl_paths[1]);
    return status;
}
