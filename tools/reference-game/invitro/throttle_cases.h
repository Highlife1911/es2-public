/* Throttle group: slider_event 4483a0, slider_base 43904c and shield_step
 * 413af8 (docs/reverse-engineering/cockpit.md, "throttle slider event and
 * shield step"). A 4 KiB arena holds the gadget G (class table at +17h,
 * sub-object table at +3Eh, handler object at +42h), the handler object H with
 * its table, two class tables and two sub-object tables, two joystick summary
 * blocks, the shield block S, and for the real-callee pass of shield_step a
 * cockpit object, gauge and vehicle block for the original 413bc8. The globals
 * 49a06e (lever word), 49db6c (button word), 4d1f46 (control byte) and 49b088
 * (cockpit pointer) are set per case, compared and restored. The gadget
 * virtuals (the handler's slot 0, the class table's slot 4, the sub-object's
 * slot 8) and the joystick summary 477904 are logging stubs: ordered calls with
 * their arguments, the bridge's slot count and entry EAX/EDX/ECX (stack values
 * masked), results from a per-case stream; in about one call of four they also
 * repoint G+17h or G+3Eh, rewrite the button word, the summary byte, the
 * control byte or the lever word, so a replacement that caches a value the
 * original rereads differs. slider_base runs as original code under
 * slider_event in the direct cases. The full EDX:EAX pair, the call log, the
 * arena and the globals are compared, and for the first 4,096 cases the callee
 * counters against the logged calls. Then installed 31/a7 poison probes and
 * real-callee passes against the all-original run: slider_event and slider_base
 * in original or replaced mode in every combination over the entry 4483a0;
 * shield_step under the original 413bc8 (443880 a plain return). shield_step
 * also runs its compare-mode cases (compare_probes.h). ECX after a hook is not
 * reproduced (the stubs write it before use). Identifiers: th_/TH_. */
enum { TH_SIZE=0x1000, TH_G=0x100, TH_H=0x200, TH_HT=0x240, TH_CT=0x280, TH_CT2=0x2c0, TH_ST=0x300, TH_ST2=0x340,
    TH_SUM=0x380, TH_SUM2=0x3a0, TH_S=0x400, TH_COCKPIT=0x600, TH_GAUGE=0x700, TH_IDS=5, TH_MAX=8,
    TH_GLOBALS=16, TH_STUB_COUNT=4 };
#define TH_BASE ((uint32_t)(uintptr_t)th_arena)
#define TH_ADDR(o) (TH_BASE+(uint32_t)(o))
static BYTE th_arena[TH_SIZE],th_before[TH_SIZE],th_after[TH_SIZE];
typedef struct th_record { uint32_t id,slots,a[3],regs[3]; } th_record;
static th_record th_log[TH_MAX];
static unsigned th_calls,th_paths[TH_IDS+4],th_mask_ecx;
static uint32_t th_seed,th_stream;
__attribute__((used)) static uint32_t th_ecx;
/* ids 0..2 are the gadget virtuals (in the arena tables), 3 is the patched
 * joystick summary 477904 and 4 the plain return that replaces 443880. */
static const unsigned th_arity[TH_IDS]={3,1,1,0,2};
static uint32_t th_next(void){
    uint32_t z=th_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static void th_reset(void){th_calls=0;th_stream=th_seed;}
static int th_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
/* The lever word, the button word, the control byte, the cockpit pointer. */
static void th_globals(BYTE *out,int restore){
    static const struct { uint32_t rva,size; } r[]={{0x9a06e,4},{0x9db6c,4},{0xd1f46,4},{0x9b088,4}};
    BYTE *p=out;for(unsigned i=0;i<4;++i){if(restore)memcpy(mapped+r[i].rva,p,r[i].size);else memcpy(p,mapped+r[i].rva,r[i].size);p+=r[i].size;}
}
/* One stub side effect (about one call in four), drawn from the stream. */
static void th_effect(void){
    uint32_t pick=th_next();if(pick%4)return;
    switch(pick/4%8){
    case 0:put32(th_arena+TH_G+0x17,TH_ADDR(TH_CT2));break;
    case 1:put32(th_arena+TH_G+0x3e,TH_ADDR(TH_ST2));break;
    case 2:put32(mapped+0x9db6c,th_next()%2?th_next()|1u:th_next()&~1u);break;
    case 3:th_arena[TH_SUM+4]=th_arena[TH_SUM2+4]=(BYTE)(th_next()%2?0:th_next()|1u);break;
    case 4:mapped[0xd1f46]=(BYTE)(th_next()%3);break;
    case 5:put32(th_arena+TH_G+0x17,TH_ADDR(TH_CT));put32(th_arena+TH_G+0x3e,TH_ADDR(TH_ST));break;
    default:put16(mapped+0x9a06e,(uint16_t)th_next());break;
    }
}
__attribute__((used,noinline)) static uint64_t __cdecl th_log_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(th_calls==TH_MAX){fail("throttle synthetic call overflow");ExitProcess(1);}
    th_record *v=th_log+th_calls++;memset(v,0,sizeof(*v));v->id=id;v->slots=weapons_bridge_slots(a,th_arity[id]);
    for(unsigned j=0;j<th_arity[id];++j)v->a[j]=th_stack(a[j])?0u:a[j];
    memcpy(v->regs,regs,12);for(unsigned j=0;j<3;++j)if(th_stack(v->regs[j]))v->regs[j]=0;
    if(id==3&&th_mask_ecx)v->regs[2]=0;
    uint32_t eax=th_next(),edx=th_next();th_ecx=th_next();
    if(id==3)eax=TH_ADDR(th_next()%4?TH_SUM:TH_SUM2);
    th_effect();
    return (uint64_t)edx<<32|eax;
}
#define TH_STUB(n) __attribute__((naked)) static void th_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _th_log_impl\n\taddl $24,%esp\n\tmovl _th_ecx,%ecx\n\tret\n\t");}
TH_STUB(0) TH_STUB(1) TH_STUB(2) TH_STUB(3)
#undef TH_STUB
/* 443880 for the real-callee pass of shield_step: returns at once. */
__attribute__((naked)) static void th_stub_4(void){__asm__ volatile("ret\n\t");}
static void *th_stubs[TH_IDS]={th_stub_0,th_stub_1,th_stub_2,th_stub_3,th_stub_4};
/* Image functions replaced by stubs: ids 3 and 4 (which = bit mask). */
static const uint32_t th_addr[TH_IDS]={0,0,0,0x477904,0x443880};
static int th_patch(BYTE saved[TH_IDS][5],int active[TH_IDS],unsigned which,int on){
    for(unsigned i=0;i<TH_IDS;++i){
        if(!th_addr[i]){active[i]=0;continue;}
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(th_addr[i]-0x400000u,th_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(th_addr[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static uint32_t th_event(void){
    static const uint32_t edges[]={0,1,0xffffffffu,0x7fffffffu,0x80000000u,0x80000001u,0x10000u,0xffffu};
    return th_next()%3?edges[th_next()%8]:th_next();
}
/* Builds one slider state: a[0] = G, a[1] = the event. */
static void th_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    (void)index;
    th_seed=random32();th_stream=th_seed^n;random_bytes(th_arena,TH_SIZE);memset(a,0,24);for(unsigned i=0;i<3;++i)r[i]=th_next();
    BYTE *g=th_arena+TH_G;
    put32(g+0x42,TH_ADDR(TH_H));put32(th_arena+TH_H,TH_ADDR(TH_HT));put32(th_arena+TH_HT,(uint32_t)(uintptr_t)th_stub_0);
    put32(g+0x17,TH_ADDR(TH_CT));put32(th_arena+TH_CT+4,(uint32_t)(uintptr_t)th_stub_1);
    put32(th_arena+TH_CT2+4,(uint32_t)(uintptr_t)th_stub_1);
    put32(g+0x3e,TH_ADDR(TH_ST));put32(th_arena+TH_ST+8,(uint32_t)(uintptr_t)th_stub_2);
    put32(th_arena+TH_ST2+8,(uint32_t)(uintptr_t)th_stub_2);
    /* The gadget as its own handler object (its table dword at +0). */
    if(th_next()%16==0){put32(g+0x42,TH_ADDR(TH_G));put32(g,TH_ADDR(TH_HT));}
    /* Button word bit 0 set, summary byte nonzero, control byte 1: about
     * two thirds each, so every site of 4483a0 goes both ways. */
    put32(mapped+0x9db6c,th_next()%3?th_next()|1u:th_next()&~1u);
    th_arena[TH_SUM+4]=th_arena[TH_SUM2+4]=(BYTE)(th_next()%3?th_next()|1u:0);
    th_arena[TH_SUM2+4]=(BYTE)(th_next()%3?th_next()|1u:0);
    mapped[0xd1f46]=(BYTE)(th_next()%4?1:th_next()%3==0?0:th_next());
    put16(mapped+0x9a06e,(uint16_t)th_next());
    a[0]=TH_ADDR(TH_G);a[1]=th_event();
    th_stream=th_seed;   /* the stubs' stream restarts here in both runs */
}
static unsigned th_hits(unsigned id){
    unsigned h=0;for(unsigned j=0;j<th_calls;++j)if(th_log[j].id==id)++h;
    return h;
}
/* The calls a hook makes itself, from the log of its replacement run. */
static int th_counter_check(unsigned index,const callee_counter *c,uint64_t delta){
    uint64_t want;
    if(index==ES2_HOOK_INDEX_slider_event)want=c->key==0x43904cu?1u:th_hits(3);
    else want=1u;
    return delta!=want;
}
static int th_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const th_record *e,const BYTE *want,const BYTE *got){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,th_calls);
    for(unsigned j=0;j<calls&&j<th_calls&&e;++j)if(memcmp(e+j,th_log+j,sizeof(*e))){
        const uint32_t *x=(const uint32_t*)(e+j),*y=(const uint32_t*)(th_log+j);
        for(unsigned k=0;k<sizeof(*e)/4;++k)if(x[k]!=y[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,e[j].id,k,x[k],y[k]);break;}
    for(unsigned j=0;j<TH_SIZE;++j)if(th_after[j]!=th_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,th_after[j],th_arena[j]);break;}
    for(unsigned j=0;j<TH_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);break;}
    return 1;
}
/* Statistics of the direct cases: logged calls per id. */
static void th_count(unsigned calls,const th_record *e){
    for(unsigned j=0;j<calls;++j)th_paths[e[j].id]++;
}
/* shield_step: the block S in the arena; the balance word at the clamp
 * edges and the 16-bit wrap, the request word with and without a nonzero
 * high half. a[0] = S, a[1] = up. */
static void th_build_step(unsigned n,uint32_t a[6],uint32_t r[3]){
    static const uint16_t balances[]={0,1,0x65,0x66,0x67,0x100,0x200,0x399,0x39a,0x39b,0x3ff,0x400,0x401,0x466,
        0x7f9a,0x7fc0,0x7fff,0x8000,0x8065,0xff9a,0xffff};
    (void)n;random_bytes(th_arena,TH_SIZE);memset(a,0,24);for(unsigned i=0;i<3;++i)r[i]=random32();
    BYTE *s=th_arena+TH_S+random32()%0x40u;
    put16(s+4,random32()%4?balances[random32()%21]:(uint16_t)random32());
    a[0]=(uint32_t)(uintptr_t)s;
    switch(random32()%6){case 0:a[1]=0;break;case 1:a[1]=1;break;case 2:a[1]=0x10000u;break;case 3:a[1]=random32()&0xffff0000u;break;
        case 4:a[1]=0xff00u;break;default:a[1]=random32();break;}
}
static void th_build_any(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    if(index==ES2_HOOK_INDEX_shield_step){th_seed=0;th_stream=0;th_build_step(n,a,r);}
    else th_build(index,n,a,r);
}
static int th_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[TH_GLOBALS],after[TH_GLOBALS],actual[TH_GLOBALS];static th_record expected[TH_MAX];
    memcpy(th_before,th_arena,TH_SIZE);th_globals(before,0);
    th_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);
    unsigned calls=th_calls;memcpy(expected,th_log,calls*sizeof(*expected));memcpy(th_after,th_arena,TH_SIZE);th_globals(after,0);
    th_count(calls,expected);
    memcpy(th_arena,th_before,TH_SIZE);th_globals(before,1);th_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[3]={0};
    for(unsigned i=0;i<count&&i<3;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);th_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count&&i<3;++i)if(th_counter_check(index,c+i,c[i].count-counts[i]))return fail("throttle callee counter delta");
    if(pair[0]!=pair[1]||calls!=th_calls||memcmp(expected,th_log,calls*sizeof(*expected))||memcmp(th_after,th_arena,TH_SIZE)||
       memcmp(after,actual,TH_GLOBALS))
        return th_report("direct",n,pair,calls,expected,after,actual);
    th_globals(before,1);
    return 0;
}
static int th_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;th_build_any(index,n*41u+3u,a,r);
        BYTE before[TH_GLOBALS],after[TH_GLOBALS],actual[TH_GLOBALS],expected_fpu[108],actual_fpu[108];static th_record expected[TH_MAX];
        memcpy(th_before,th_arena,TH_SIZE);th_globals(before,0);
        th_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=th_calls;memcpy(expected,th_log,calls*sizeof(*expected));memcpy(th_after,th_arena,TH_SIZE);th_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("throttle poison install");
        memcpy(th_arena,th_before,TH_SIZE);th_globals(before,1);th_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);es2_hook_fpu_save(actual_fpu);th_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=th_calls||memcmp(expected,th_log,calls*sizeof(*expected))||memcmp(th_after,th_arena,TH_SIZE)||
           memcmp(after,actual,TH_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=th_report(fast?"installed fast":"installed preserving",n,pair,calls,expected,after,actual);
        if(!es2_hook_uninstall(&h))return fail("throttle poison uninstall");th_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/calls/arena/globals\n",d->name,total);
    return status;
}
/* Real-callee pass of the sliders: slider_event and slider_base installed
 * in every original/replaced combination over the entry 4483a0, against the
 * all-original run with the same stubs. ECX of the stub log entries is
 * masked for the joystick summary 477904 only: after slider_base the hook
 * does not reproduce ECX = G+3Eh (the summary writes ECX before reading). */
static int th_real_sliders(unsigned index){
    const unsigned set[2]={ES2_HOOK_INDEX_slider_event,ES2_HOOK_INDEX_slider_base};
    BYTE saved[TH_IDS][5];int active[TH_IDS];if(th_patch(saved,active,1u<<3,1))return 1;
    int status=0;unsigned total=0;uint64_t entered[2]={0,0};th_mask_ecx=1;
    for(unsigned mode=0;mode<4&&!status;++mode)for(unsigned n=0;n<256u*4u&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;th_build(index,n*67u+mode*7u,a,r);
        BYTE before[TH_GLOBALS],after[TH_GLOBALS],actual[TH_GLOBALS];static th_record expected[TH_MAX];
        memcpy(th_before,th_arena,TH_SIZE);th_globals(before,0);
        th_reset();sg_poison_call(mapped+0x483a0,a,pattern,expected_pair);
        unsigned calls=th_calls;memcpy(expected,th_log,calls*sizeof(*expected));memcpy(th_after,th_arena,TH_SIZE);th_globals(after,0);
        es2_hook hk[2];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<2;++j){o.mode=(mode>>j&1)?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(set[j]),&o,function_hook_extension(set[j])))return fail("throttle real install");}
        memcpy(th_arena,th_before,TH_SIZE);th_globals(before,1);th_reset();
        sg_poison_call(mapped+0x483a0,a,pattern,actual_pair);th_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=th_calls||memcmp(expected,th_log,calls*sizeof(*expected))||memcmp(th_after,th_arena,TH_SIZE)||
           memcmp(after,actual,TH_GLOBALS)){
            fprintf(stderr,"real mode=%u\n",mode);status=th_report("real",n,pair,calls,expected,after,actual);}
        for(unsigned j=0;j<2;++j)if(mode>>j&1)entered[j]+=hk[j].calls;
        for(unsigned j=0;j<2;++j)if(!es2_hook_uninstall(hk+j))return fail("throttle real uninstall");
        th_globals(before,1);++total;
    }
    th_mask_ecx=0;
    if(th_patch(saved,active,0,0))return 1;
    if(!status&&(!entered[0]||!entered[1])){fprintf(stderr,"FAIL: %s real pass: a hook was never entered replaced\n",current_function);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases (slider_event and slider_base original/replaced in all 4 combinations over 4483a0; %llu event entries, %llu nested base entries), 31/a7 stack, pair/calls/arena/globals\n",
        current_function,total,(unsigned long long)entered[0],(unsigned long long)entered[1]);
    return status;
}
/* shield_step under the original 413bc8: the cockpit object, the gauge with
 * its request bytes and the vehicle block S in the arena; 443880 returns at
 * once. */
static void th_build_balance(uint32_t a[6],uint32_t r[3]){
    th_build_step(0,a,r);
    BYTE *s=(BYTE *)(uintptr_t)a[0];
    put16(s+8,(uint16_t)(1u+random32()%0x2000u));
    put32(mapped+0x9b088,TH_ADDR(TH_COCKPIT));put32(th_arena+TH_COCKPIT+0x1e9,TH_ADDR(TH_GAUGE));
    BYTE *gauge=th_arena+TH_GAUGE;
    gauge[0xc2]=(BYTE)(random32()%3==0?0:random32()|1u);gauge[0xc3]=(BYTE)(random32()%3==0?0:random32()|1u);
    a[1]=0;
}
static int th_real_step(unsigned index){
    const es2_hook_descriptor *d=function_hook_descriptor(index);
    BYTE saved[TH_IDS][5];int active[TH_IDS];if(th_patch(saved,active,1u<<4,1))return 1;
    int status=0;unsigned total=0;uint64_t entered=0;
    for(unsigned mode=0;mode<2&&!status;++mode)for(unsigned n=0;n<1536&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;th_build_balance(a,r);
        BYTE before[TH_GLOBALS],after[TH_GLOBALS],actual[TH_GLOBALS];
        memcpy(th_before,th_arena,TH_SIZE);th_globals(before,0);
        sg_poison_call(mapped+0x13bc8,a,pattern,expected_pair);
        memcpy(th_after,th_arena,TH_SIZE);th_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        o.mode=mode?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("throttle step real install");
        memcpy(th_arena,th_before,TH_SIZE);th_globals(before,1);
        sg_poison_call(mapped+0x13bc8,a,pattern,actual_pair);th_globals(actual,0);
        if(memcmp(th_after,th_arena,TH_SIZE)||memcmp(after,actual,TH_GLOBALS)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            fprintf(stderr,"real step mode=%u\n",mode);th_calls=0;status=th_report("real",n,pair,0,NULL,after,actual);
        }
        entered+=h.calls;
        if(!es2_hook_uninstall(&h))return fail("throttle step real uninstall");
        th_globals(before,1);++total;
    }
    if(th_patch(saved,active,0,0))return 1;
    if(!status&&!entered){fprintf(stderr,"FAIL: %s real pass: never entered under 413bc8\n",d->name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases under the original 413bc8 (443880 a plain return), hook original/replaced, 31/a7 stack, arena and globals; %llu replaced entries\n",
        d->name,total,(unsigned long long)entered);
    return status;
}
/* Compare cases (compare_probes.h): shield_step accepts compare. */
static void th_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){(void)index;th_build_step(n,a,r);}
static void th_compare_state(BYTE *s,int restore){if(restore)memcpy(th_arena,s,TH_SIZE);else memcpy(s,th_arena,TH_SIZE);}
static int throttle_cases(unsigned index,unsigned limit){
    if(limit&&compare_probe_installed(index))return compare_probe_run(index,limit,th_compare_build,th_compare_state,TH_SIZE);
    BYTE old_globals[TH_GLOBALS];th_globals(old_globals,0);
    BYTE saved[TH_IDS][5];int active[TH_IDS];
    if(th_patch(saved,active,1u<<3,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_THROTTLE_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);memset(th_paths,0,sizeof(th_paths));
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];th_build_any(index,n,a,r);status=th_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);current_case_valid=0;
    th_globals(old_globals,1);
    if(!limit&&!status)status=th_installed_probes(index,64);
    th_globals(old_globals,1);
    if(th_patch(saved,active,1u<<3,0))return 1;
    if(!limit&&!status)status=index==ES2_HOOK_INDEX_shield_step?th_real_step(index):th_real_sliders(index);
    th_globals(old_globals,1);
    if(!limit&&!status&&index==ES2_HOOK_INDEX_shield_step)status=compare_probe_run(index,0,th_compare_build,th_compare_state,TH_SIZE);
    if(!status)printf("PASS: %-20s %u cases (stub calls: handler %u, class %u, sub-object %u, joystick summary %u); full return pair, ordered calls with entry registers, arena and globals\n",
        function_hook_descriptor(index)->name,cases,th_paths[0],th_paths[1],th_paths[2],th_paths[3]);
    return status;
}
