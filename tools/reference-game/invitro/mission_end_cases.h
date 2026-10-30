/* mission_end group: the mission end 461ff0 (docs/reverse-engineering/
 * launcher.md, "the mission end `0x461ff0` as own code"). Direct cases run the
 * original body (EBX = 0 at entry, restored by the body), the adapter (pair)
 * and the core (pair and ECX) with both game callees replaced by logging stubs:
 * _fclose 496838 (the runtime's import thunk) and results_writer 42412c. The
 * two bare-RET exits 426514 and 425344 stay original (one byte; they cannot be
 * patched). Ordered calls with the argument, the entry EAX/EDX/ECX and the
 * three globals the original has written by then (flag byte, dump and tape
 * stream dwords) are compared, with the pair, ECX (core; not with the hook
 * installed), the global windows (flag block, run block 4d24b0..4d2540, exit
 * code, word 4a9e52), the object arena and the callee-counter deltas of the
 * first 4,096 cases. The stubs return random registers and now and then change
 * what the original rereads after the call: the stream dwords (after a close),
 * the quit, demo and bank words, the object's byte +99h and the object dword
 * 4d24e6 (after the results writer). The state covers tape closed / open, dump
 * closed / open, dwords whose low word is zero, words with garbage in the high
 * word. Then 64 installed 31/a7 cases (fast and preserving dispatch, nonempty
 * x87). No real-callee pass: the real callees are the runtime's file close and
 * the results writer's file writes (the writer's own group covers it with the
 * platform calls stubbed); both stay stubs here. Identifiers: mse_/MSE_. */
typedef struct mse_record { uint32_t id,arg,regs[3],eax,edx,flag,dump,tape; } mse_record;
enum { MSE_ARENA=0x400, MSE_MAX=8, MSE_IDS=2, MSE_WINDOWS=4, MSE_OBJECT=0x100 };
#define MSE_BASE ((uint32_t)(uintptr_t)mse_arena)
static BYTE mse_arena[MSE_ARENA],mse_arena_before[MSE_ARENA],mse_arena_after[MSE_ARENA];
static mse_record mse_log[MSE_MAX],mse_expected[MSE_MAX];
static unsigned mse_calls;
static uint32_t mse_seed,mse_stream;
__attribute__((used)) static uint32_t mse_ecx;
/* Global windows (RVA, length): the flag block, the run block through the
 * quit and demo words, the exit code, the word 4a9e52. */
static const uint32_t mse_window_rva[MSE_WINDOWS]={0xd1dd0,0xd24b0,0xd27b8,0xa9e50};
static const unsigned mse_window_len[MSE_WINDOWS]={0x10,0x90,4,4};
#define MSE_GLOBALS (0x10+0x90+4+4)
static BYTE mse_globals_after[MSE_GLOBALS];
/* 0 _fclose 496838, 1 results_writer 42412c. */
static const uint32_t mse_direct[MSE_IDS]={0x496838,0x42412c};
static uint32_t mse_next(void){
    uint32_t z=mse_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static void mse_globals(BYTE *buffer,int restore){
    for(unsigned i=0,at=0;i<MSE_WINDOWS;at+=mse_window_len[i++]){
        BYTE *p=mapped+mse_window_rva[i];
        if(restore)memcpy(p,buffer+at,mse_window_len[i]);else memcpy(buffer+at,p,mse_window_len[i]);
    }
}
#define MSE_VA(va) (mapped+(uint32_t)(va)-0x400000u)
__attribute__((used,noinline)) static uint64_t __cdecl mse_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(mse_calls==MSE_MAX){fail("mission_end synthetic call overflow");ExitProcess(1);}
    mse_record *v=mse_log+mse_calls++;memset(v,0,sizeof(*v));v->id=id;v->arg=a[0];memcpy(v->regs,regs,12);
    v->flag=*MSE_VA(0x4d1dd6);v->dump=get32(MSE_VA(0x4d24de));v->tape=get32(MSE_VA(0x4d24e2));
    uint32_t eax=mse_next(),edx=mse_next(),pick=mse_next();mse_ecx=mse_next();
    if(id==0){
        /* A close can change the other stream word (the original rereads the tape word after the dump close). */
        if(pick%8==0)put32(MSE_VA(0x4d24e2),pick>>8&1?0:mse_next());
    } else {
        if(pick%8==0)put16(MSE_VA(0x4d24fe),(uint16_t)(pick>>8&1?0:mse_next()));
        if(pick%8==1)put16(MSE_VA(0x4d2530),(uint16_t)(pick>>8&1?0:mse_next()));
        if(pick%8==2)put16(MSE_VA(0x4a9e52),(uint16_t)(pick>>8&1?0:mse_next()));
        if(pick%8==3){BYTE *o=(BYTE *)(uintptr_t)get32(MSE_VA(0x4d24e6));o[0x99]=(BYTE)(pick>>8&1?0:mse_next());}
        if(pick%16==4)put32(MSE_VA(0x4d24e6),MSE_BASE+(pick>>8&1?0:MSE_OBJECT));
        if(pick%16==5)put32(MSE_VA(0x4d24e2),mse_next());
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define MSE_STUB(n) __attribute__((naked)) static void mse_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _mse_stub_impl\n\taddl $24,%esp\n\tmovl _mse_ecx,%ecx\n\tret\n\t");}
MSE_STUB(0) MSE_STUB(1)
#undef MSE_STUB
static void *mse_stubs[MSE_IDS]={mse_stub_0,mse_stub_1};
static int mse_patch(BYTE saved[MSE_IDS][5],int on){
    for(unsigned i=0;i<MSE_IDS;++i)
        if(actor_patch(mse_direct[i]-0x400000u,on?mse_stubs[i]:NULL,saved[i],on?TRUE:FALSE))return 1;
    return 0;
}
static void mse_reset(void){mse_calls=0;mse_stream=mse_seed;}
/* A word (or dword) for the tests of the original: 0, 1, a random value, or
 * a zero low word with garbage above. */
static uint32_t mse_dword(void){
    switch(random32()%5){
    case 0:return 0;
    case 1:return 1;
    case 2:return random32()<<16;
    default:return random32();
    }
}
static uint16_t mse_word(void){unsigned k=random32()%3;return k==0?0:k==1?1:(uint16_t)random32();}
static void mse_build(unsigned n,uint32_t r[3]){
    (void)n;
    mse_seed=random32();random_bytes(mse_arena,MSE_ARENA);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    random_bytes(mapped+mse_window_rva[0],mse_window_len[0]);        /* the flag byte starts with a random value */
    put32(MSE_VA(0x4d24de),mse_dword());put32(MSE_VA(0x4d24e2),mse_dword());
    put32(MSE_VA(0x4d24e6),MSE_BASE+(random32()%2?0:MSE_OBJECT));
    put32(MSE_VA(0x4d24fe),random32()%2?(mse_dword()&0xffff0000u)|mse_word():mse_word());
    put32(MSE_VA(0x4d2530),random32()%2?(mse_dword()&0xffff0000u)|mse_word():mse_word());
    put32(MSE_VA(0x4a9e52),random32()%2?(mse_dword()&0xffff0000u)|mse_word():mse_word());
    put32(MSE_VA(0x4d27b8),random32());
    for(unsigned k=0;k<2;++k)mse_arena[(k?MSE_OBJECT:0)+0x99]=(BYTE)(random32()%3==0?0:random32()%2?1:random32());
}
static unsigned mse_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<mse_calls;++j)if(c->event==ES2_COLLIDE_CALL&&c->key==mse_direct[mse_log[j].id])++hits;
    return hits;
}
static uint64_t mse_core(const uint32_t *r,uint32_t *ecx){
    const es2_guest_memory *m=&math_native_memory;
    es2_weapon_graph g={(uint32_t)(uintptr_t)mapped,0,0,0,0,weapons_guest_call,NULL,NULL,NULL};
    return es2_mission_end_registers(m,&g,r,ecx);
}
static int mse_report(const char *what,unsigned n,const uint32_t *want,const uint32_t *got,unsigned calls){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x calls %u/%u\n",current_function,n,what,
        want[0],got[0],want[1],got[1],want[2],got[2],calls,mse_calls);
    for(unsigned j=0;j<calls&&j<mse_calls;++j)if(memcmp(mse_expected+j,mse_log+j,sizeof(*mse_log))){
        const uint32_t *e=(const uint32_t *)(mse_expected+j),*f=(const uint32_t *)(mse_log+j);
        for(unsigned k=0;k<sizeof(*mse_log)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,mse_expected[j].id,k,e[k],f[k]);
        break;
    }
    BYTE actual[MSE_GLOBALS];mse_globals(actual,0);
    for(unsigned j=0;j<MSE_GLOBALS;++j)if(mse_globals_after[j]!=actual[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,mse_globals_after[j],actual[j]);break;}
    for(unsigned j=0;j<MSE_ARENA;++j)if(mse_arena_after[j]!=mse_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,mse_arena_after[j],mse_arena[j]);break;}
    return 1;
}
/* Restore the initial state (arena and windows) before another run. */
static void mse_restore(const BYTE *globals){memcpy(mse_arena,mse_arena_before,MSE_ARENA);mse_globals((BYTE *)globals,1);}
static int mse_same_state(const BYTE *globals_expected){
    BYTE actual[MSE_GLOBALS];mse_globals(actual,0);
    return !memcmp(actual,globals_expected,MSE_GLOBALS) && !memcmp(mse_arena,mse_arena_after,MSE_ARENA);
}
static int mse_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    uint32_t want[3],got[3];BYTE initial[MSE_GLOBALS];
    memcpy(mse_arena_before,mse_arena,MSE_ARENA);mse_globals(initial,0);
    mse_reset();wm_invoke(mapped+d->rva,a,r,want);unsigned calls=mse_calls;
    memcpy(mse_expected,mse_log,calls*sizeof(*mse_expected));memcpy(mse_arena_after,mse_arena,MSE_ARENA);
    mse_globals(mse_globals_after,0);
    /* The adapter, with the callee-counter deltas. */
    mse_restore(initial);mse_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[4]={0};
    for(unsigned i=0;i<count&&i<4;++i)counts[i]=c[i].count;
    const uint64_t pair=x->replacement_context(a,d->user,r);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);got[2]=want[2];
    if(n<4096)for(unsigned i=0;i<count&&i<4;++i)if(c[i].count-counts[i]!=mse_expected_hits(c+i))return fail("mission_end callee counter delta");
    if(want[0]!=got[0]||want[1]!=got[1]||calls!=mse_calls||memcmp(mse_expected,mse_log,calls*sizeof(*mse_expected))||
       !mse_same_state(mse_globals_after))
        return mse_report("adapter",n,want,got,calls);
    /* The core, with the original's ECX (not when the hook is installed:
       the "original" is then the hook, whose ECX is the framework's). */
    mse_restore(initial);mse_reset();
    uint32_t ecx;const uint64_t core=mse_core(r,&ecx);got[0]=(uint32_t)core;got[1]=(uint32_t)(core>>32);got[2]=ecx;
    const int ecx_known=!rng_hooks[index].installed;
    if(core!=pair||(ecx_known&&ecx!=want[2])||calls!=mse_calls||memcmp(mse_expected,mse_log,calls*sizeof(*mse_expected))||
       !mse_same_state(mse_globals_after))
        return mse_report("core",n,want,got,calls);
    return 0;
}
/* The installed hook against the original over a patterned stack. */
static int mse_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6]={0},r[3],want[3]={0},got[3]={0},pattern=poison?0xa7a7a7a7u:0x31313131u;BYTE initial[MSE_GLOBALS];
        mse_build(n,r);
        BYTE expected_fpu[108],actual_fpu[108];
        memcpy(mse_arena_before,mse_arena,MSE_ARENA);mse_globals(initial,0);
        mse_reset();wm_poison_call(mapped+d->rva,a,pattern,want);es2_hook_fpu_save(expected_fpu);
        unsigned calls=mse_calls;memcpy(mse_expected,mse_log,calls*sizeof(*mse_expected));memcpy(mse_arena_after,mse_arena,MSE_ARENA);
        mse_globals(mse_globals_after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("mission_end poison install");
        mse_restore(initial);mse_reset();
        wm_poison_call(mapped+d->rva,a,pattern,got);es2_hook_fpu_save(actual_fpu);
        if(want[0]!=got[0]||want[1]!=got[1]||calls!=mse_calls||memcmp(mse_expected,mse_log,calls*sizeof(*mse_expected))||
           !mse_same_state(mse_globals_after)||
           memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=mse_report(fast?"installed fast":"installed preserving",n,want,got,calls);
        if(!es2_hook_uninstall(&h))return fail("mission_end poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, pair/trace/globals\n",d->name,total);
    return status;
}
static int mission_end_cases(unsigned index,unsigned limit){
    current_function=function_hook_descriptor(index)->name;
    BYTE saved[MSE_IDS][5],old_globals[MSE_GLOBALS];
    mse_globals(old_globals,0);
    if(mse_patch(saved,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_MISSION_END_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6]={0},r[3];mse_build(n,r);status=mse_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    if(!limit&&!status)status=mse_installed_probes(index,64);
    if(mse_patch(saved,0))return 1;
    mse_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ECX (core), ordered calls with entry registers and the stream words, global windows, object arena, callee counters\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
