/* Shield group: shield_balance 413bc8 (docs/reverse-engineering/cockpit.md,
 * "shield balance as own code"). Direct cases run the original body and the
 * core against a logging stub for the gauge setter 443880: its two arguments,
 * entry EAX/EDX/ECX (stack values masked: the record pointer is the original's
 * frame or the adapter's scratch) and the 15 record bytes it receives, by
 * content. The getter 443908 and the step 413af8 run as original code. The
 * vehicle block S, the gauge G and the cockpit object H live in an arena; the
 * cockpit pointer 49b088 is a compared and restored global window. Some cases
 * let S overlap the gauge record or the gauge pointer H+1E9h, so the original's
 * read/write order and its second read of 49b088/H+1E9h are visible. EDX:EAX is
 * the stub's pair and is compared whole. Then installed 31/a7 poison probes and
 * a real-callee pass with the real 443880 (hook original or replaced); EDX
 * there is the record pointer (a stack address) and dead at the only caller, so
 * only EAX, arena and globals are compared. */
typedef struct sb_record { uint32_t id,args[2],regs[3],eax; BYTE record[16]; uint32_t count; } sb_record;
enum { SB_SIZE=0x800, SB_COCKPIT=0x000, SB_GAUGE=0x300, SB_VEHICLE=0x500, SB_MAX=4, SB_GLOBAL=0x9b088,
    SB_GLOBALS=4 };
#define SB_BASE ((uint32_t)(uintptr_t)sb_arena)
static BYTE sb_arena[SB_SIZE],sb_before[SB_SIZE],sb_after[SB_SIZE];
static sb_record sb_log[SB_MAX];
static unsigned sb_calls;
static uint32_t sb_seed,sb_stream,sb_vehicle;
__attribute__((used)) static uint32_t sb_ecx;
static uint32_t sb_next(void){
    uint32_t z=sb_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static int sb_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
__attribute__((used,noinline)) static uint64_t __cdecl sb_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(sb_calls==SB_MAX){fail("shield synthetic call overflow");ExitProcess(1);}
    sb_record *v=sb_log+sb_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,8);memcpy(v->regs,regs,12);
    v->count=weapons_bridge_slots(a,2);
    memcpy(v->record,(const void *)(uintptr_t)a[1],15);
    for(unsigned j=0;j<3;++j)if(sb_stack(v->regs[j]))v->regs[j]=0;
    for(unsigned j=0;j<2;++j)if(sb_stack(v->args[j]))v->args[j]=0;
    uint32_t eax=sb_next(),edx=sb_next();sb_ecx=sb_next();
    v->eax=eax;return (uint64_t)edx<<32|eax;
}
__attribute__((naked)) static void sb_stub_0(void){__asm__ volatile(
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
    "pushl $0\n\tcalll _sb_stub_impl\n\taddl $24,%esp\n\tmovl _sb_ecx,%ecx\n\tret\n\t");}
static void sb_globals(BYTE *out,int restore){
    if(restore)memcpy(mapped+SB_GLOBAL,out,SB_GLOBALS);else memcpy(out,mapped+SB_GLOBAL,SB_GLOBALS);
}
static void sb_reset(void){sb_calls=0;sb_stream=sb_seed;}
static uint16_t sb_word(const uint16_t *values,unsigned count){return random32()%4?values[random32()%count]:(uint16_t)random32();}
static BYTE sb_request(void){return (BYTE)(random32()%2?0:random32()%2?1:random32()|1u);}
/* Cockpit, gauge, vehicle block. Balance words cluster at the clamp edges
 * and the 16-bit wrap; the divisor word is never 0 (the original's #DE
 * precondition). alias: 0 none, 1 S overlaps the gauge record, 2 S+4
 * overlaps the gauge pointer H+1E9h (direct cases only: the real 443880
 * would follow the changed pointer). */
static void sb_build(unsigned n,int real){
    static const uint16_t balances[]={0,1,0x65,0x66,0x67,0x100,0x200,0x399,0x39a,0x39b,0x3ff,0x400,0x401,0x466,
        0x7f9a,0x7fc0,0x7fff,0x8000,0x8065,0xff9a,0xffff};
    static const uint16_t maxima[]={1,2,3,100,0x400,0x1388,0x7fff,0xffff,0x8000,0xfffd};
    uint32_t b=SB_BASE;BYTE *h=sb_arena+SB_COCKPIT,*gauge=sb_arena+SB_GAUGE;
    sb_seed=random32();random_bytes(sb_arena,SB_SIZE);
    put32(mapped+SB_GLOBAL,b+SB_COCKPIT);put32(h+0x1e9,b+SB_GAUGE);
    gauge[0xc2]=sb_request();gauge[0xc3]=sb_request();
    gauge[0x8d]=(BYTE)(random32()%2?0:random32()%2?1:random32());
    unsigned alias=real?0:n%16==3?2:n%8==5?1:0;
    uint32_t s=alias==1?SB_GAUGE+0xa0u+random32()%0x30u:alias==2?SB_COCKPIT+0x1e5u+random32()%4u:SB_VEHICLE+random32()%0x20u;
    BYTE *v=sb_arena+s;
    /* With alias 2 only the divisor (S+8 lies above the pointer) is
     * written; S+0..S+5 keep the pointer bytes and a request is forced. */
    if(alias==2&&!gauge[0xc2]&&!gauge[0xc3])gauge[0xc2]=1;
    uint16_t maximum=sb_word(maxima,sizeof(maxima)/sizeof(maxima[0]));if(!maximum)maximum=1;
    if(alias!=2){
        put16(v+4,sb_word(balances,sizeof(balances)/sizeof(balances[0])));
        if(random32()%2){put16(v,(uint16_t)(random32()%((uint32_t)maximum+1u)));put16(v+2,(uint16_t)(random32()%((uint32_t)maximum+1u)));}
    }
    put16(v+8,maximum);
    sb_vehicle=b+s;
}
static int sb_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const sb_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,sb_calls);
    for(unsigned j=0;j<calls&&j<sb_calls&&expected;++j)if(memcmp(expected+j,sb_log+j,sizeof(*expected))){
        const BYTE *e=(const BYTE *)(expected+j),*f=(const BYTE *)(sb_log+j);
        for(unsigned k=0;k<sizeof(*expected);++k)if(e[k]!=f[k])fprintf(stderr," call%u byte%u %02x/%02x\n",j,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<SB_SIZE;++j)if(sb_after[j]!=sb_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,sb_after[j],sb_arena[j]);break;}
    return 1;
}
static int sb_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[SB_GLOBALS],after[SB_GLOBALS],actual[SB_GLOBALS];static sb_record expected[SB_MAX];
    memcpy(sb_before,sb_arena,SB_SIZE);sb_globals(before,0);
    /* 413af8 runs once when either request byte of the record is set. */
    const BYTE *gauge=sb_arena+SB_GAUGE;const uint64_t steps=gauge[0xc2]||gauge[0xc3];
    sb_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=sb_calls;memcpy(expected,sb_log,calls*sizeof(*expected));
    memcpy(sb_after,sb_arena,SB_SIZE);sb_globals(after,0);
    memcpy(sb_arena,sb_before,SB_SIZE);sb_globals(before,1);sb_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[2]={0};
    for(unsigned i=0;i<count&&i<2;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);sb_globals(actual,0);
    if(count!=2||c[0].count-counts[0]!=calls||c[1].count-counts[1]!=steps)return fail("shield callee counter delta");
    if(pair[0]!=pair[1]||calls!=sb_calls||memcmp(expected,sb_log,calls*sizeof(*expected))||memcmp(sb_after,sb_arena,SB_SIZE)||
       memcmp(after,actual,SB_GLOBALS))
        return sb_report("direct",n,pair,calls,expected);
    return 0;
}
static int sb_patch(BYTE saved[5],int on){
    return actor_patch(0x43880u,on?(void *)sb_stub_0:NULL,saved,on?TRUE:FALSE);
}
static int sb_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6]={0},expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;sb_build(n,0);a[0]=sb_vehicle;
        BYTE before[SB_GLOBALS],after[SB_GLOBALS],actual[SB_GLOBALS],expected_fpu[108],actual_fpu[108];static sb_record expected[SB_MAX];
        memcpy(sb_before,sb_arena,SB_SIZE);sb_globals(before,0);
        sb_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=sb_calls;memcpy(expected,sb_log,calls*sizeof(*expected));memcpy(sb_after,sb_arena,SB_SIZE);sb_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("shield poison install");
        memcpy(sb_arena,sb_before,SB_SIZE);sb_globals(before,1);sb_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);es2_hook_fpu_save(actual_fpu);sb_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=sb_calls||memcmp(expected,sb_log,calls*sizeof(*expected))||memcmp(sb_after,sb_arena,SB_SIZE)||
           memcmp(after,actual,SB_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=sb_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("shield poison uninstall");sb_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, EDX:EAX/trace/arena/globals\n",d->name,total);
    return status;
}
/* Real 443880 (and 443908), the hook itself original or replaced, against
 * a plain run of the original. EAX, arena (the gauge record, the redraw
 * counter +0B0h, the sweep targets +0CCh/+0D0h, the balance word) and the
 * global. */
static int sb_real_pass(unsigned index){
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0,replaced=0;uint64_t entered=0;
    for(unsigned mode=0;mode<2&&!status;++mode)for(unsigned n=0;n<1536&&!status;++n){
        uint32_t a[6]={0},expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;sb_build(n,1);a[0]=sb_vehicle;
        BYTE before[SB_GLOBALS],after[SB_GLOBALS],actual[SB_GLOBALS];
        memcpy(sb_before,sb_arena,SB_SIZE);sb_globals(before,0);
        sb_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        memcpy(sb_after,sb_arena,SB_SIZE);sb_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        o.mode=mode?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("shield real install");
        memcpy(sb_arena,sb_before,SB_SIZE);sb_globals(before,1);sb_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);sb_globals(actual,0);
        if(expected_pair[0]!=actual_pair[0]||expected_pair[0]!=SB_BASE+SB_GAUGE||memcmp(sb_after,sb_arena,SB_SIZE)||memcmp(after,actual,SB_GLOBALS)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            fprintf(stderr,"real mode=%u\n",mode);sb_calls=0;status=sb_report("real",n,pair,0,NULL);
        }
        entered+=h.calls;replaced+=mode;
        if(!es2_hook_uninstall(&h))return fail("shield real uninstall");
        sb_globals(before,1);++total;
    }
    /* The hook counts its calls only in replaced mode. */
    if(!status&&entered!=replaced){fprintf(stderr,"FAIL: %s real pass entered %llu of %u\n",d->name,(unsigned long long)entered,replaced);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases, real 443880, hook original/replaced, 31/a7 stack, EAX, arena and globals\n",d->name,total);
    return status;
}
static int shield_cases(unsigned index,unsigned limit){
    BYTE saved[5],old_globals[SB_GLOBALS];
    sb_globals(old_globals,0);
    if(sb_patch(saved,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_SHIELD_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6]={0},r[3];sb_build(n,0);a[0]=sb_vehicle;for(unsigned i=0;i<3;++i)r[i]=random32();status=sb_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=sb_installed_probes(index,64);
    if(sb_patch(saved,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=sb_real_pass(index);
    sb_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; EDX:EAX, gauge-setter call with entry registers and record, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
