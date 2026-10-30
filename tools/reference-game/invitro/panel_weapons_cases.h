/* panel_weapons group: weapon_select 410708, weapon_find 4106ac and
 * panel_weapon 432a50 (hud.md, "panel weapon selection as own code"). One arena
 * holds a HUD object H ([H+203h] = P), P ([P+202h] = W), the weapon system W
 * with two slot lists (index -8..31), six weapons on two vtables and their
 * definitions. Direct cases run the original body and the core against logging
 * stubs for the hook's game callees (410708 under weapon_find, 4106ac under
 * panel_weapon, patched in the private image; the weapon virtual +58h through
 * the arena vtables, part of the cases with the real body 40df08): ordered
 * arguments, the bridge's slot count, entry EAX/EDX/ECX. The stubs return edge
 * values and change what 4106ac reads again (the count W+8, the list pointer
 * W+4, list entries, E+4Ch, a vtable). EAX, EDX and ECX (from the adapter's
 * exit value), the call log, the arena and the word 4d2528 are compared. Then
 * installed 31/a7 poison probes through the exit shims, and a real-callee pass:
 * 432a50 over the real 4106ac and 410708 with the three hooks in all eight
 * original/replaced combinations against the all-original run. */
typedef struct pw_record { uint32_t id,args[2],slots,regs[3]; } pw_record;
enum { PW_SIZE=0xb00, PW_H=0x000, PW_P=0x220, PW_W=0x440, PW_LIST=0x4a0, PW_LIST2=0x560, PW_E=0x600, PW_D=0x900,
    PW_VT1=0xa00, PW_VT2=0xa80, PW_WEAPONS=6, PW_MAX=128, PW_IDS=4, PW_GLOBALS=16 };
#define PW_BASE ((uint32_t)(uintptr_t)pw_arena)
#define PW_ADDR(o) (PW_BASE+(uint32_t)(o))
static BYTE pw_arena[PW_SIZE],pw_before[PW_SIZE],pw_after[PW_SIZE];
static pw_record pw_log[PW_MAX];
static unsigned pw_calls,pw_selects,pw_real_v58;
/* Real-callee pass: 410708 is real and reads the matched slot again, so
 * the stubs never leave a null entry there (the original would fault). */
static int pw_real_mode;
static uint32_t pw_seed,pw_stream;
__attribute__((used)) static uint32_t pw_ecx;
/* 0 410708, 1 4106ac (patched); 2/3 the +58h slot of VT1/VT2. */
static const uint32_t pw_addresses[PW_IDS]={0x410708,0x4106ac,0,0};
static uint32_t pw_next(void){
    uint32_t z=pw_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static void pw_reset(void){pw_calls=0;pw_stream=pw_seed;}
static uint32_t pw_e(unsigned k){return PW_ADDR(PW_E+0x80u*k);}
static void pw_globals(BYTE *out,int restore){if(restore)memcpy(mapped+0xd2520,out,PW_GLOBALS);else memcpy(out,mapped+0xd2520,PW_GLOBALS);}
static int16_t pw_count(void){
    static const uint16_t counts[]={0,0xffff,0x8000,1,2,3,6,12,24};
    return (int16_t)(random32()%2?1u+random32()%24u:counts[random32()%9]);
}
/* Changes 4106ac reads again after the virtual +58h (stub stream only). */
static void pw_side_effect(uint32_t e,uint32_t pick){
    BYTE *w=pw_arena+PW_W;
    switch(pick%16){
    case 2:put16(w+8,(uint16_t)(int16_t)((int)(pw_next()%27u)-2));break;
    case 3:put32(w+4,pw_real_mode||get32(w+4)==PW_ADDR(PW_LIST)?PW_ADDR(PW_LIST2):PW_ADDR(PW_LIST));break;
    case 4:put32(pw_arena+(pw_next()%2?PW_LIST:PW_LIST2)+4*(pw_next()%32),pw_real_mode||pw_next()%3?pw_e(pw_next()%PW_WEAPONS):0);break;
    case 5:if(e-PW_BASE<PW_SIZE-0x80u)((BYTE *)(uintptr_t)e)[0x4c]^=1;break;
    case 6:put32(pw_arena+(PW_E+0x80u*(pw_next()%PW_WEAPONS)),pw_next()%2?PW_ADDR(PW_VT1):PW_ADDR(PW_VT2));break;
    default:break;
    }
}
__attribute__((used,noinline)) static uint64_t __cdecl pw_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(pw_calls==PW_MAX){fail("panel_weapons synthetic call overflow");ExitProcess(1);}
    uint32_t eax=pw_next(),edx=pw_next(),pick=pw_next();pw_ecx=pw_next();
    pw_record *v=pw_log+pw_calls++;memset(v,0,sizeof(*v));v->id=id;v->args[0]=a[0];v->args[1]=a[1];
    v->slots=weapons_bridge_slots(a,2);memcpy(v->regs,regs,12);
    switch(id){
    case 1:if(pick%3==0)eax&=0xffff0000u;break;                 /* AX = 0: no selection */
    case 2:case 3:
        if(pick%3==0)eax&=0xffff0000u;else if(!(uint16_t)eax)eax|=1u;
        pw_side_effect(a[0],pick>>2);break;
    default:break;
    }
    return (uint64_t)edx<<32|eax;
}
#define PW_STUB(n) __attribute__((naked)) static void pw_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _pw_stub_impl\n\taddl $24,%esp\n\tmovl _pw_ecx,%ecx\n\tret\n\t");}
PW_STUB(0) PW_STUB(1) PW_STUB(2) PW_STUB(3)
#undef PW_STUB
static void *pw_stubs[PW_IDS]={pw_stub_0,pw_stub_1,pw_stub_2,pw_stub_3};
/* Calls a target with two slots and chosen EAX/EDX/ECX; returns all three. */
__attribute__((naked)) static void pw_machine(void *target,const uint32_t *a,const uint32_t *registers,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl 12(%ebp),%esi\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\t"
        "call *8(%ebp)\n\tmovl 20(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* The same from a stack filled with a pattern, EAX = EDX = ECX = pattern. */
__attribute__((naked)) static void pw_poison_call(void *target,const uint32_t *a,uint32_t pattern,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 16(%ebp),%eax\n\tmovl $1024,%ecx\n\trep stosl\n\taddl $3840,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%eax\n\tmovl %eax,%edx\n\tmovl %eax,%ecx\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* One state for hook `index`: a = the two slots, r = entry EAX/EDX/ECX. */
static void pw_build(unsigned index,uint32_t a[2],uint32_t r[3]){
    pw_seed=random32();random_bytes(pw_arena,PW_SIZE);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    put32(pw_arena+PW_H+0x203,PW_ADDR(PW_P));put32(pw_arena+PW_P+0x202,PW_ADDR(PW_W));
    /* VT1 +58h: stub 2, or in a quarter of the states the real 40df08. */
    pw_real_v58=random32()%4==0;
    put32(pw_arena+PW_VT1+0x58,pw_real_v58?(uint32_t)(uintptr_t)(mapped+0xdf08):(uint32_t)(uintptr_t)pw_stubs[2]);
    put32(pw_arena+PW_VT2+0x58,(uint32_t)(uintptr_t)pw_stubs[3]);
    const uint32_t elem=random32()%4?PW_ADDR(PW_H+random32()%0x40u):random32();
    static const BYTE defs[]={0,1,0x7f,0x80,0xff,0xfd,2};
    for(unsigned k=0;k<PW_WEAPONS;++k){
        BYTE *p=pw_arena+PW_E+0x80u*k,*d=pw_arena+PW_D+0x20u*k;
        put32(p,random32()%2?PW_ADDR(PW_VT1):PW_ADDR(PW_VT2));put32(p+0x18,PW_ADDR(PW_D+0x20u*k));
        p[0x4b]=(BYTE)(random32()%2?0:1u+random32()%255u);p[0x4c]=(BYTE)(random32()%3?1u+random32()%255u:0);
        put32(p+0x77,random32()%2?elem:random32());
        d[0x16]=random32()%4?defs[random32()%7]:(BYTE)random32();
    }
    const int full=index==ES2_HOOK_INDEX_weapon_select;
    for(int i=-8;i<32;++i){
        put32(pw_arena+PW_LIST+4*i,full||random32()%4?pw_e(random32()%PW_WEAPONS):0);
        put32(pw_arena+PW_LIST2+4*i,full||pw_real_mode||random32()%4?pw_e(random32()%PW_WEAPONS):0);
    }
    BYTE *w=pw_arena+PW_W;put32(w+4,PW_ADDR(PW_LIST));
    const int16_t count=pw_count();put16(w+8,(uint16_t)count);
    const int top=count>0?count:1;
    int sel=random32()%8==0?-1:(int)(random32()%(unsigned)top);
    if(random32()%8==0)sel=(int8_t)random32();
    w[0x1d]=(BYTE)sel;
    put16(mapped+0xd2528,(uint16_t)random32());
    a[1]=elem;a[0]=PW_ADDR(PW_W);
    if(index==ES2_HOOK_INDEX_weapon_select){
        /* The selected slot, or a slot -8..31 (every list entry is a
         * weapon); any high word. */
        const uint32_t low=random32()%3==0?(uint16_t)(int16_t)(int8_t)w[0x1d]:(uint16_t)(int16_t)((int)(random32()%40u)-8);
        a[1]=(random32()%2?random32()&0xffff0000u:low>=0x8000u?0xffff0000u:0)|low;
    } else if(index==ES2_HOOK_INDEX_panel_weapon){
        a[0]=PW_ADDR(PW_H);
        if(random32()%8==0)put32(pw_arena+PW_P+0x202,random32());   /* W is only passed on */
    }
}
static int pw_report(const char *what,unsigned n,const uint32_t want[3],const uint32_t got[3],unsigned calls,const pw_record *expected,
    const BYTE *globals_want,const BYTE *globals_got){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x calls %u/%u\n",current_function,n,what,
        want[0],got[0],want[1],got[1],want[2],got[2],calls,pw_calls);
    for(unsigned j=0;j<calls&&j<pw_calls&&expected;++j)if(memcmp(expected+j,pw_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(pw_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<PW_SIZE;++j)if(pw_after[j]!=pw_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,pw_after[j],pw_arena[j]);break;}
    for(unsigned j=0;j<PW_GLOBALS;++j)if(globals_want[j]!=globals_got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,globals_want[j],globals_got[j]);break;}
    return 1;
}
static uint32_t pw_exit_ecx(unsigned index){
    if(index==ES2_HOOK_INDEX_weapon_select)return panel_weapons_select_ecx;
    if(index==ES2_HOOK_INDEX_weapon_find)return panel_weapons_find_ecx;
    return panel_weapons_panel_ecx;
}
static unsigned pw_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<pw_calls;++j){unsigned id=pw_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?id>=2&&c->key==0x58:id<2&&pw_addresses[id]==c->key)++hits;}
    return hits;
}
static int pw_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    static pw_record expected[PW_MAX];BYTE before[PW_GLOBALS],after[PW_GLOBALS],actual[PW_GLOBALS];
    memcpy(pw_before,pw_arena,PW_SIZE);pw_globals(before,0);
    uint32_t want[3],got[3];pw_reset();pw_machine(mapped+d->rva,a,r,want);
    unsigned calls=pw_calls;memcpy(expected,pw_log,calls*sizeof(*expected));memcpy(pw_after,pw_arena,PW_SIZE);pw_globals(after,0);
    memcpy(pw_arena,pw_before,PW_SIZE);pw_globals(before,1);pw_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[2]={0,0};
    for(unsigned i=0;i<count&&i<2;++i)counts[i]=c[i].count;
    uint64_t pair=x->replacement_context(a,d->user,r);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);got[2]=pw_exit_ecx(index);
    pw_globals(actual,0);
    if(n<4096&&!pw_real_v58)for(unsigned i=0;i<count&&i<2;++i)if(c[i].count-counts[i]!=pw_expected_hits(c+i))return fail("panel_weapons callee counter delta");
    if(memcmp(want,got,sizeof(want))||calls!=pw_calls||memcmp(expected,pw_log,calls*sizeof(*expected))||memcmp(pw_after,pw_arena,PW_SIZE)||
       memcmp(after,actual,PW_GLOBALS))
        return pw_report("direct",n,want,got,calls,expected,after,actual);
    if(index==ES2_HOOK_INDEX_weapon_find&&pw_arena[PW_W+0x18]!=pw_before[PW_W+0x18])++pw_selects;
    if(index==ES2_HOOK_INDEX_weapon_select&&memcmp(after+8,before+8,2))++pw_selects;
    if(index==ES2_HOOK_INDEX_panel_weapon&&want[0])++pw_selects;
    return 0;
}
/* which: bit i patches stub i (410708, 4106ac). */
static int pw_patch(BYTE saved[2][5],unsigned which,int on){
    for(unsigned i=0;i<2;++i)if(which>>i&1&&actor_patch(pw_addresses[i]-0x400000u,on?pw_stubs[i]:NULL,saved[i],on?TRUE:FALSE))return 1;
    return 0;
}
static unsigned pw_which(unsigned index){
    return index==ES2_HOOK_INDEX_weapon_find?1u:index==ES2_HOOK_INDEX_panel_weapon?2u:0u;
}
/* Installed probes: the original from a 31/a7 stack against the installed
 * hook (through its exit shim); EAX, EDX, ECX, the trace, arena, globals. */
static int pw_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[2],r[3],want[3],got[3],pattern=poison?0xa7a7a7a7u:0x31313131u;pw_build(index,a,r);
        BYTE before[PW_GLOBALS],after[PW_GLOBALS],actual[PW_GLOBALS],expected_fpu[108],actual_fpu[108];static pw_record expected[PW_MAX];
        memcpy(pw_before,pw_arena,PW_SIZE);pw_globals(before,0);
        pw_reset();pw_poison_call(mapped+d->rva,a,pattern,want);es2_hook_fpu_save(expected_fpu);
        unsigned calls=pw_calls;memcpy(expected,pw_log,calls*sizeof(*expected));memcpy(pw_after,pw_arena,PW_SIZE);pw_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("panel_weapons poison install");
        memcpy(pw_arena,pw_before,PW_SIZE);pw_globals(before,1);pw_reset();
        pw_poison_call(mapped+d->rva,a,pattern,got);es2_hook_fpu_save(actual_fpu);pw_globals(actual,0);
        if(memcmp(want,got,sizeof(want))||calls!=pw_calls||memcmp(expected,pw_log,calls*sizeof(*expected))||memcmp(pw_after,pw_arena,PW_SIZE)||
           memcmp(after,actual,PW_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=pw_report(fast?"installed fast":"installed preserving",n,want,got,calls,expected,after,actual);
        if(!es2_hook_uninstall(&h))return fail("panel_weapons poison uninstall");pw_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, EAX/EDX/ECX (exit shim), trace, arena, globals\n",
        d->name,total);
    return status;
}
/* 432a50 over the real 4106ac and 410708; the three hooks in the eight
 * original/replaced combinations (bit 0 weapon_select, bit 1 weapon_find,
 * bit 2 panel_weapon). The +58h stubs (or the real 40df08) stay. */
static int pw_real(unsigned index){
    static const unsigned hooks[3]={ES2_HOOK_INDEX_weapon_select,ES2_HOOK_INDEX_weapon_find,ES2_HOOK_INDEX_panel_weapon};
    const unsigned mine=index==hooks[0]?0u:index==hooks[1]?1u:2u;
    int status=0;unsigned total=0,selections=0;uint64_t entered=0;pw_real_mode=1;
    for(unsigned mode=0;mode<8&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[2],r[3],want[3],got[3],pattern=n&1?0xa7a7a7a7u:0x31313131u;pw_build(ES2_HOOK_INDEX_panel_weapon,a,r);
        put32(pw_arena+PW_P+0x202,PW_ADDR(PW_W));
        BYTE before[PW_GLOBALS],after[PW_GLOBALS],actual[PW_GLOBALS];static pw_record expected[PW_MAX];
        memcpy(pw_before,pw_arena,PW_SIZE);pw_globals(before,0);
        pw_reset();pw_poison_call(mapped+0x32a50,a,pattern,want);
        unsigned calls=pw_calls;memcpy(expected,pw_log,calls*sizeof(*expected));memcpy(pw_after,pw_arena,PW_SIZE);pw_globals(after,0);
        es2_hook hk[3];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<3;++j){o.mode=(mode>>j&1)?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(hooks[j]),&o,function_hook_extension(hooks[j])))return fail("panel_weapons real install");}
        memcpy(pw_arena,pw_before,PW_SIZE);pw_globals(before,1);pw_reset();
        pw_poison_call(mapped+0x32a50,a,pattern,got);pw_globals(actual,0);
        if(memcmp(want,got,sizeof(want))||calls!=pw_calls||memcmp(expected,pw_log,calls*sizeof(*expected))||memcmp(pw_after,pw_arena,PW_SIZE)||
           memcmp(after,actual,PW_GLOBALS)){
            fprintf(stderr,"real mode=%u\n",mode);status=pw_report("real",n,want,got,calls,expected,after,actual);}
        if(mode>>mine&1)entered+=hk[mine].calls;
        for(unsigned j=0;j<3;++j)if(!es2_hook_uninstall(hk+j))return fail("panel_weapons real uninstall");
        if(memcmp(after+8,before+8,2))++selections;
        pw_globals(before,1);++total;
    }
    pw_real_mode=0;
    if(!status&&(!entered||!selections)){fprintf(stderr,"FAIL: %s real pass entered %llu, selections %u\n",current_function,
        (unsigned long long)entered,selections);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases (432a50 over the real 4106ac/410708, three hooks original/replaced; %llu replaced entries, %u selections), 31/a7 stack, EAX/EDX/ECX/calls/arena/globals\n",
        current_function,total,(unsigned long long)entered,selections);
    return status;
}
static int panel_weapons_cases(unsigned index,unsigned limit){
    BYTE saved_globals[PW_GLOBALS];pw_globals(saved_globals,0);
    BYTE saved[2][5];if(pw_patch(saved,pw_which(index),1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_PANEL_WEAPONS_CASES");
    if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);pw_selects=0;
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[2],r[3];pw_build(index,a,r);status=pw_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    pw_globals(saved_globals,1);
    if(!limit&&!status)status=pw_installed_probes(index,64);
    pw_globals(saved_globals,1);
    if(pw_patch(saved,pw_which(index),0))return 1;
    if(!limit&&!status)status=pw_real(index);
    pw_globals(saved_globals,1);
    if(!status)printf("PASS: %-20s %u cases (%u selections); EAX/EDX/ECX, ordered calls with entry registers and slot counts, arena, globals\n",
        function_hook_descriptor(index)->name,cases,pw_selects);
    return status;
}
