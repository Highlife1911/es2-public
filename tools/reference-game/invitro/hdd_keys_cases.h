/* hdd_keys group: the HDD gauge's key method hdd_key 449ff4
 * (docs/reverse-engineering/cockpit.md, "HDD key method 449ff4"). A 4 KiB arena
 * holds the HDD gauge H (page array H+101h/H+105h, active index H+109h, the
 * buttons H+18h+4*b for the real pass), three page objects with their vtables
 * (slot +10h = a logging stub), the button objects with their vtable (slot +8 =
 * a logging stub) and the graphics context behind the image global 49b088 (set
 * per case, compared and restored). Per case the key code (every low word
 * 0..5Fh, the handled keys, any word; the high word zero or random), the active
 * index (0, 1, and wrapped values 40000000h/C0000001h whose page slot address
 * wraps onto H+101h/H+105h), the page entries and the entry EAX/EDX/ECX vary.
 * The callees 438d9c, 44a60c, 450b88 are patched logging stubs; with the page
 * slot they log ordered calls with their arguments, the bridge's slot count and
 * entry EAX/EDX/ECX (stack values masked), return registers from a per-case
 * stream and in about one call of four apply a side effect (H+105h, H+109h or a
 * page entry), so a replacement that reads H+105h before 44a60c differs. The
 * full EDX:EAX pair, the call log and the arena are compared, and for the first
 * 4,096 cases the callee counters against the logged calls. Then installed
 * 31/a7 poison probes and a real-callee pass: hdd_key and button_press 438d9c
 * in all four original/replaced combinations against the all-original run (the
 * button slot +8 and 4530bc stubbed). Identifiers: hy_/HY_. */
enum { HY_SIZE=0x1000, HY_H=0x100, HY_PAGES=0x400, HY_PVT=0x600, HY_BUTTONS=0x800, HY_BVT=0xc00, HY_C=0xc80,
    HY_IDS=6, HY_MAX=8, HY_GLOBALS=4, HY_ALL=0x07, HY_REAL=0x16 };
#define HY_BASE ((uint32_t)(uintptr_t)hy_arena)
#define HY_ADDR(o) (HY_BASE+(uint32_t)(o))
static BYTE hy_arena[HY_SIZE],hy_before[HY_SIZE],hy_after[HY_SIZE];
typedef struct hy_record { uint32_t id,slots,a[3],regs[3]; } hy_record;
static hy_record hy_log[HY_MAX];
static unsigned hy_calls,hy_paths[HY_IDS];
static uint32_t hy_seed,hy_stream;
__attribute__((used)) static uint32_t hy_ecx;
/* ids 0..2 and 4 are patched callees; 3 is the page slot +10h, 5 the
 * button slot +8. */
static const uint32_t hy_addr[HY_IDS]={0x438d9c,0x44a60c,0x450b88,0,0x4530bc,0};
static const unsigned hy_arity[HY_IDS]={3,2,2,2,2,2};
static uint32_t hy_next(void){
    uint32_t z=hy_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static void hy_reset(void){hy_calls=0;hy_stream=hy_seed;}
static int hy_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static void hy_globals(BYTE *out,int restore){
    if(restore)memcpy(mapped+0x9b088,out,4);else memcpy(out,mapped+0x9b088,4);
}
static uint32_t hy_page(void){return HY_ADDR(HY_PAGES+0x40u*(hy_next()%3));}
static uint32_t hy_index(void){
    switch(hy_next()%8){case 0:return 0x40000000u;case 1:return 0xc0000001u;default:return hy_next()%2;}
}
/* One callee side effect (about one call in four), drawn from the stream. */
static void hy_effect(void){
    uint32_t pick=hy_next();if(pick%4)return;
    BYTE *H=hy_arena+HY_H;
    switch(pick/4%3){
    case 0:put32(H+0x105,hy_page());break;
    case 1:put32(H+0x109,hy_index());break;
    default:put32(H+0x101,hy_page());break;
    }
}
__attribute__((used,noinline)) static uint64_t __cdecl hy_log_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(hy_calls==HY_MAX){fail("hdd_keys synthetic call overflow");ExitProcess(1);}
    hy_record *v=hy_log+hy_calls++;memset(v,0,sizeof(*v));v->id=id;v->slots=weapons_bridge_slots(a,hy_arity[id]);
    for(unsigned j=0;j<hy_arity[id];++j)v->a[j]=hy_stack(a[j])?0u:a[j];
    memcpy(v->regs,regs,12);for(unsigned j=0;j<3;++j)if(hy_stack(v->regs[j]))v->regs[j]=0;
    uint32_t eax=hy_next(),edx=hy_next();hy_ecx=hy_next();
    hy_effect();
    return (uint64_t)edx<<32|eax;
}
#define HY_STUB(n) __attribute__((naked)) static void hy_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _hy_log_impl\n\taddl $24,%esp\n\tmovl _hy_ecx,%ecx\n\tret\n\t");}
HY_STUB(0) HY_STUB(1) HY_STUB(2) HY_STUB(3) HY_STUB(4) HY_STUB(5)
#undef HY_STUB
static void *hy_stubs[HY_IDS]={hy_stub_0,hy_stub_1,hy_stub_2,hy_stub_3,hy_stub_4,hy_stub_5};
static int hy_patch(BYTE saved[HY_IDS][5],int active[HY_IDS],unsigned which,int on){
    for(unsigned i=0;i<HY_IDS;++i){
        if(!hy_addr[i]){active[i]=0;continue;}
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(hy_addr[i]-0x400000u,hy_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(hy_addr[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
/* The key code: every low word up to 5Fh, the handled keys, any word. */
static uint32_t hy_code(void){
    static const uint16_t keys[]={2,3,4,0xc,0xd,0x11,0x17,0x1f,0x48,0x4a,0x4b,0x4d,0x4e,0x50,0x49,0x4c,0x4f,0x51,0x47,
        0x10,0x12,0x16,0x18,0x1e,0x20,0x1,0x0,0x5,0xb,0xe,0x218,0x22e,0x212,0xffff};
    uint32_t low;
    switch(hy_next()%4){
    case 0:low=hy_next()%0x60u;break;
    case 1:low=(uint16_t)hy_next();break;
    default:low=keys[hy_next()%(sizeof(keys)/sizeof(keys[0]))];break;
    }
    return (hy_next()%2?0u:hy_next()<<16)|low;
}
/* Builds one state. a[0] = H, a[1] = the code. */
static void hy_build(unsigned n,uint32_t a[6],uint32_t r[3]){
    hy_seed=random32();hy_stream=hy_seed^n;random_bytes(hy_arena,HY_SIZE);memset(a,0,24);for(unsigned i=0;i<3;++i)r[i]=hy_next();
    BYTE *H=hy_arena+HY_H;
    put32(H+0x101,hy_page());put32(H+0x105,hy_page());put32(H+0x109,hy_index());
    for(unsigned p=0;p<3;++p){put32(hy_arena+HY_PAGES+0x40u*p,HY_ADDR(HY_PVT+0x20u*p));put32(hy_arena+HY_PVT+0x20u*p+0x10,(uint32_t)(uintptr_t)hy_stub_3);}
    /* The HDD buttons 2..0Ch (for the real button_press) and their vtable. */
    for(unsigned b=0;b<16;++b){
        const uint32_t w=HY_ADDR(HY_BUTTONS+0x40u*b);put32(H+0x18+4*b,w);
        put16((BYTE *)(uintptr_t)w+0x1b,hy_next()%4?1:2);put32((BYTE *)(uintptr_t)w+0x17,HY_ADDR(HY_BVT));
    }
    put32(hy_arena+HY_BVT+8,(uint32_t)(uintptr_t)hy_stub_5);
    put32(mapped+0x9b088,HY_ADDR(HY_C));
    a[0]=HY_ADDR(HY_H);a[1]=hy_code();
    hy_stream=hy_seed;   /* the stubs' stream restarts here in both runs */
}
static unsigned hy_hits(unsigned id){
    unsigned h=0;for(unsigned j=0;j<hy_calls;++j)if(hy_log[j].id==id)++h;
    return h;
}
static int hy_counter_check(const callee_counter *c,uint64_t delta){
    unsigned id=c->event==ES2_COLLIDE_SLOT?3u:c->key==0x438d9cu?0u:c->key==0x44a60cu?1u:2u;
    return delta!=hy_hits(id);
}
static int hy_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const hy_record *e,const BYTE *want,const BYTE *got){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,hy_calls);
    for(unsigned j=0;j<calls&&j<hy_calls&&e;++j)if(memcmp(e+j,hy_log+j,sizeof(*e))){
        const uint32_t *x=(const uint32_t*)(e+j),*y=(const uint32_t*)(hy_log+j);
        for(unsigned k=0;k<sizeof(*e)/4;++k)if(x[k]!=y[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,e[j].id,k,x[k],y[k]);break;}
    for(unsigned j=0;j<HY_SIZE;++j)if(hy_after[j]!=hy_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,hy_after[j],hy_arena[j]);break;}
    for(unsigned j=0;j<HY_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);break;}
    return 1;
}
static int hy_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[HY_GLOBALS],after[HY_GLOBALS],actual[HY_GLOBALS];static hy_record expected[HY_MAX];
    memcpy(hy_before,hy_arena,HY_SIZE);hy_globals(before,0);
    hy_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);
    unsigned calls=hy_calls;memcpy(expected,hy_log,calls*sizeof(*expected));memcpy(hy_after,hy_arena,HY_SIZE);hy_globals(after,0);
    for(unsigned j=0;j<calls;++j)hy_paths[expected[j].id]++;
    memcpy(hy_arena,hy_before,HY_SIZE);hy_globals(before,1);hy_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8]={0};
    for(unsigned i=0;i<count&&i<8;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);hy_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count&&i<8;++i)if(hy_counter_check(c+i,c[i].count-counts[i]))return fail("hdd_keys callee counter delta");
    if(pair[0]!=pair[1]||calls!=hy_calls||memcmp(expected,hy_log,calls*sizeof(*expected))||memcmp(hy_after,hy_arena,HY_SIZE)||
       memcmp(after,actual,HY_GLOBALS))
        return hy_report("direct",n,pair,calls,expected,after,actual);
    hy_globals(before,1);
    return 0;
}
static int hy_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;hy_build(n*41u+3u,a,r);
        BYTE before[HY_GLOBALS],after[HY_GLOBALS],actual[HY_GLOBALS],expected_fpu[108],actual_fpu[108];static hy_record expected[HY_MAX];
        memcpy(hy_before,hy_arena,HY_SIZE);hy_globals(before,0);
        hy_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=hy_calls;memcpy(expected,hy_log,calls*sizeof(*expected));memcpy(hy_after,hy_arena,HY_SIZE);hy_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("hdd_keys poison install");
        memcpy(hy_arena,hy_before,HY_SIZE);hy_globals(before,1);hy_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);es2_hook_fpu_save(actual_fpu);hy_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=hy_calls||memcmp(expected,hy_log,calls*sizeof(*expected))||memcmp(hy_after,hy_arena,HY_SIZE)||
           memcmp(after,actual,HY_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=hy_report(fast?"installed fast":"installed preserving",n,pair,calls,expected,after,actual);
        if(!es2_hook_uninstall(&h))return fail("hdd_keys poison uninstall");hy_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/calls/arena/globals\n",d->name,total);
    return status;
}
/* Real-callee pass: hdd_key and button_press 438d9c in every original/
 * replaced combination against the all-original run (44a60c, 450b88,
 * 4530bc and both slots stubbed). */
static int hy_real(unsigned index){
    const unsigned set[2]={index,ES2_HOOK_INDEX_button_press},k=2,modes=4;
    BYTE saved[HY_IDS][5];int active[HY_IDS];if(hy_patch(saved,active,HY_REAL,1))return 1;
    int status=0;unsigned total=0,button=0;uint64_t entered=0,nested=0;
    for(unsigned mode=0;mode<modes&&!status;++mode)for(unsigned n=0;n<1024u&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;hy_build(n*67u+mode*7u,a,r);
        /* Half the cases press a button. */
        if(n%2){static const uint16_t keys[]={2,3,4,0xc,0xd,0x48,0x4a,0x4b,0x4d,0x4e,0x50};a[1]=(a[1]&0xffff0000u)|keys[hy_next()%11];}
        BYTE before[HY_GLOBALS],after[HY_GLOBALS],actual[HY_GLOBALS];static hy_record expected[HY_MAX];
        memcpy(hy_before,hy_arena,HY_SIZE);hy_globals(before,0);
        hy_reset();sg_poison_call(mapped+function_hook_descriptor(index)->rva,a,pattern,expected_pair);
        unsigned calls=hy_calls;memcpy(expected,hy_log,calls*sizeof(*expected));memcpy(hy_after,hy_arena,HY_SIZE);hy_globals(after,0);
        es2_hook hk[2];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<k;++j){o.mode=(mode>>j&1)?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(set[j]),&o,function_hook_extension(set[j])))return fail("hdd_keys real install");}
        memcpy(hy_arena,hy_before,HY_SIZE);hy_globals(before,1);hy_reset();
        sg_poison_call(mapped+function_hook_descriptor(index)->rva,a,pattern,actual_pair);hy_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=hy_calls||memcmp(expected,hy_log,calls*sizeof(*expected))||memcmp(hy_after,hy_arena,HY_SIZE)||
           memcmp(after,actual,HY_GLOBALS)){
            fprintf(stderr,"real mode=%u\n",mode);status=hy_report("real",n,pair,calls,expected,after,actual);}
        if(mode&1)entered+=hk[0].calls;
        if(mode&2)nested+=hk[1].calls;
        for(unsigned j=0;j<calls;++j)if(expected[j].id==4||expected[j].id==5)++button;
        for(unsigned j=0;j<k;++j)if(!es2_hook_uninstall(hk+j))return fail("hdd_keys real uninstall");
        hy_globals(before,1);++total;
    }
    if(hy_patch(saved,active,0,0))return 1;
    if(!status&&(!entered||!nested)){fprintf(stderr,"FAIL: %s real pass: never entered replaced\n",current_function);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases (%u hooks original/replaced in all %u combinations; %llu replaced entries, %llu replaced nested entries, %u button_press callee calls), 31/a7 stack, pair/calls/arena/globals\n",
        current_function,total,k,modes,(unsigned long long)entered,(unsigned long long)nested,button);
    return status;
}
static int hdd_keys_cases(unsigned index,unsigned limit){
    BYTE old_globals[HY_GLOBALS];hy_globals(old_globals,0);
    BYTE saved[HY_IDS][5];int active[HY_IDS];
    if(hy_patch(saved,active,HY_ALL,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_HDD_KEYS_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);memset(hy_paths,0,sizeof(hy_paths));
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];hy_build(n,a,r);status=hy_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);current_case_valid=0;
    hy_globals(old_globals,1);
    if(!limit&&!status)status=hy_installed_probes(index,64);
    hy_globals(old_globals,1);
    if(hy_patch(saved,active,HY_ALL,0))return 1;
    if(!limit&&!status)status=hy_real(index);
    hy_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases (stub calls 438d9c %u, 44a60c %u, 450b88 %u, page slot +10h %u); full return pair, ordered calls with entry registers, arena and globals\n",
        function_hook_descriptor(index)->name,cases,hy_paths[0],hy_paths[1],hy_paths[2],hy_paths[3]);
    return status;
}
