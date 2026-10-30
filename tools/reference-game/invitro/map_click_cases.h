/* map_click group: map_click 44d6e0, the HDD map click (cockpit.md, "map click
 * 44d6e0"). The arena and the image globals are those of the map_pick cases
 * (mp_build with the caller fields: actors apart from the map, each with a side
 * pointer +45h into a four-entry side table), so the real 44d888 runs inside
 * both the original 44d6e0 and the core's bridge call and picks an actor in
 * part of the cases. Per case the map mode M+5Dh (mostly 0Bh..0Eh), the wingman
 * index M+105h (0..2, -1, small, or an index whose record stores land on a
 * chosen field of the map object, e.g. M+5Dh, M+105h or the stored point, so
 * the rereads of M+105h and the read/write order of the point copy matter), the
 * four side bytes (0, 1, FFh, 80h, any), the click byte M+11Ch and the entry
 * EDX/ECX are drawn. The sound 462830 and the label update 44dd14 are logging
 * stubs in the direct and installed passes (ordered calls with their argument,
 * the bridge's slot count and entry EAX/EDX/ECX, stack values masked;
 * deterministic EDX:EAX/ECX results), so the full EDX:EAX pair, the call log,
 * the arena and the globals (with 49d6ac..49d6b3) are compared. Then installed
 * 31/a7 poison probes, and a real-callee pass: map_pick 44d888 hooked in
 * original or replaced mode under map_click in original or replaced mode, with
 * the real 462830 (sound off: [49f8ec] = 0, so it returns its entry EAX/EDX,
 * which 44dd14 receives) and the logging stub for 44dd14, against the
 * all-original run. */
typedef struct mc_record { uint32_t id,arg,slots,regs[3]; } mc_record;
enum { MC_MAX=4, MC_IDS=2, MC_SIDE_SLOTS=4, MC_SOUND=0x9f8ec };
static mc_record mc_log[MC_MAX];
static unsigned mc_calls,mc_stores[3];
__attribute__((used)) static uint32_t mc_ecx;
static const uint32_t mc_addresses[MC_IDS]={0x462830,0x44dd14};
__attribute__((used,noinline)) static uint64_t __cdecl mc_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(mc_calls==MC_MAX){fail("map_click synthetic call overflow");ExitProcess(1);}
    mc_record *v=mc_log+mc_calls++;memset(v,0,sizeof(*v));v->id=id;v->arg=a[0];v->slots=weapons_bridge_slots(a,1);memcpy(v->regs,regs,12);
    for(unsigned j=0;j<3;++j)if(mp_stack(v->regs[j]))v->regs[j]=0;
    if(mp_stack(v->arg))v->arg=0;
    mc_ecx=0x3c000000u+mc_calls*0x11u+id;return (uint64_t)(0x2b000000u+mc_calls*0x13u+id)<<32|(0x1a000000u+mc_calls*0x17u+id);
}
#define MC_STUB(n) __attribute__((naked)) static void mc_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _mc_stub_impl\n\taddl $24,%esp\n\tmovl _mc_ecx,%ecx\n\tret\n\t");}
MC_STUB(0) MC_STUB(1)
#undef MC_STUB
static void *mc_stubs[MC_IDS]={mc_stub_0,mc_stub_1};
/* which: bit i patches stub i. */
static int mc_patch(BYTE saved[MC_IDS][5],unsigned which,int on){
    for(unsigned i=0;i<MC_IDS;++i)if(which>>i&1&&actor_patch(mc_addresses[i]-0x400000u,on?mc_stubs[i]:NULL,saved[i],on?TRUE:FALSE))return 1;
    return 0;
}
/* An index whose record field `field` (offset from the record base
 * M+91h+2Ah*i, the base the stores address) lands on M+target (distance
 * rounded to even, since 2Ah*i is even): 21*i = d/2 modulo 2^31. */
static uint32_t mc_alias_index(uint32_t target,uint32_t field){
    static uint32_t inverse;if(!inverse){inverse=21u;for(unsigned k=0;k<5;++k)inverse*=2u-21u*inverse;}
    uint32_t d=target-0x91u-field;d&=~1u;
    return (d/2u*inverse&0x7fffffffu)|(random32()&0x80000000u);
}
/* The first store of either path (actor dword +10h, point copy +0..+0Bh
 * from the record base) must not touch the index M+105h..+108h, or the
 * reread index is a pointer or a coordinate and the next store leaves the
 * arena; the kind dword +1Ah may only replace it whole (then the last
 * store uses index 0 or 1). */
static int mc_overlaps(int32_t first,int32_t length){return first<0x109&&first+length>0x105;}
static int mc_index_safe(uint32_t i){
    const int32_t base=(int32_t)(42u*i+0x91u);
    if(mc_overlaps(base,0xc)||mc_overlaps(base+0x10,4))return 0;
    return !mc_overlaps(base+0x1a,4)||base+0x1a==0x105;
}
static uint32_t mc_index(void){
    static const uint32_t targets[]={0x5d,0x5e,0x105,0x104,0x11c,0x15c,0x374,0x376,0x378,0x37c,0x380,0x71,0x11d};
    static const uint32_t fields[]={0,4,8,0x10,0x15,0x1a,0x1b,0x1c,0x1e};
    switch(random32()%10){
    case 0:return 0xffffffffu;
    case 1:return 3u+random32()%8u;
    case 2:return mc_alias_index(0x105,0x1a);
    case 3:case 4:for(;;){uint32_t i=mc_alias_index(targets[random32()%13],fields[random32()%9]+random32()%3u);if(mc_index_safe(i))return i;}
    default:return random32()%3u;
    }
}
/* One state: a[0] = M; r = entry EAX/EDX/ECX. */
static void mc_build(unsigned n,uint32_t a[6],uint32_t r[3]){
    static const uint32_t modes[]={0xb,0xc,0xb,0xc,0xd,0xe,0xa,0xf};
    static const BYTE sides[]={0,1,0xff,0x80,2};
    mp_build(n,a,r,1);
    BYTE *map=mp_arena+MP_MAP;
    put32(map+0x5d,random32()%16?modes[random32()%8]:random32()%2?random32():0x10bu);
    put32(map+0x105,mc_index());
    map[0x11c]=(BYTE)(random32()%2?1:random32());
    for(unsigned k=0;k<MC_SIDE_SLOTS;++k)mp_arena[MP_SIDES+0x12+4*k]=random32()%4?sides[random32()%5]:(BYTE)random32();
    a[1]=0;
}
/* Statistics: no store, an actor store (M+15Ch written), a point store. */
static void mc_count(unsigned calls){
    const uint32_t at=MP_MAP+0x15cu;
    ++mc_stores[!calls?0:memcmp(mp_before+at,mp_after+at,4)?1:2];
}
static int mc_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const mc_record *expected,const BYTE *want,const BYTE *got){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,mc_calls);
    for(unsigned j=0;j<calls&&j<mc_calls&&expected;++j)if(memcmp(expected+j,mc_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(mc_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<MP_SIZE;++j)if(mp_after[j]!=mp_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,mp_after[j],mp_arena[j]);break;}
    for(unsigned j=0;j<MP_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);break;}
    return 1;
}
static int mc_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[MP_GLOBALS],after[MP_GLOBALS],actual[MP_GLOBALS];static mc_record expected[MC_MAX];
    memcpy(mp_before,mp_arena,MP_SIZE);mp_globals(before,0);
    mc_calls=0;uint64_t pair[2];pair[0]=weapons_original(index,a,r);
    unsigned calls=mc_calls;memcpy(expected,mc_log,calls*sizeof(*expected));memcpy(mp_after,mp_arena,MP_SIZE);mp_globals(after,0);
    mc_count(calls);
    memcpy(mp_arena,mp_before,MP_SIZE);mp_globals(before,1);mc_calls=0;
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[3]={0};
    for(unsigned i=0;i<count&&i<3;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);mp_globals(actual,0);
    if(n<4096){
        if(count!=3||c[0].count-counts[0]!=1u)return fail("map_click call_44d888 counter delta");
        for(unsigned i=1;i<3;++i){unsigned hits=0;for(unsigned j=0;j<mc_calls;++j)if(mc_log[j].id==i-1u)++hits;
            if(c[i].count-counts[i]!=hits)return fail("map_click callee counter delta");}
    }
    if(pair[0]!=pair[1]||calls!=mc_calls||memcmp(expected,mc_log,calls*sizeof(*expected))||memcmp(mp_after,mp_arena,MP_SIZE)||
       memcmp(after,actual,MP_GLOBALS))
        return mc_report("direct",n,pair,calls,expected,after,actual);
    return 0;
}
static int mc_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;mc_build(n*41u+3u,a,r);
        BYTE before[MP_GLOBALS],after[MP_GLOBALS],actual[MP_GLOBALS],expected_fpu[108],actual_fpu[108];static mc_record expected[MC_MAX];
        memcpy(mp_before,mp_arena,MP_SIZE);mp_globals(before,0);
        mc_calls=0;sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=mc_calls;memcpy(expected,mc_log,calls*sizeof(*expected));memcpy(mp_after,mp_arena,MP_SIZE);mp_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("map_click poison install");
        memcpy(mp_arena,mp_before,MP_SIZE);mp_globals(before,1);mc_calls=0;
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);es2_hook_fpu_save(actual_fpu);mp_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=mc_calls||memcmp(expected,mc_log,calls*sizeof(*expected))||memcmp(mp_after,mp_arena,MP_SIZE)||
           memcmp(after,actual,MP_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=mc_report(fast?"installed fast":"installed preserving",n,pair,calls,expected,after,actual);
        if(!es2_hook_uninstall(&h))return fail("map_click poison uninstall");mp_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/calls/arena/globals\n",d->name,total);
    return status;
}
/* map_pick (bit 0) and map_click (bit 1) in original or replaced mode,
 * the real 462830 with the sound off, the stub for 44dd14. */
static int mc_real(unsigned index){
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0,stores=0;uint64_t entered=0;
    const unsigned hooks[2]={ES2_HOOK_INDEX_map_pick,ES2_HOOK_INDEX_map_click};
    BYTE sound[4];memcpy(sound,mapped+MC_SOUND,4);put32(mapped+MC_SOUND,0);
    BYTE saved[MC_IDS][5];if(mc_patch(saved,2u,1))return 1;
    for(unsigned mode=0;mode<4&&!status;++mode)for(unsigned n=0;n<768&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;mc_build(n*67u+mode,a,r);
        BYTE before[MP_GLOBALS],after[MP_GLOBALS],actual[MP_GLOBALS];static mc_record expected[MC_MAX];
        memcpy(mp_before,mp_arena,MP_SIZE);mp_globals(before,0);
        mc_calls=0;sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        unsigned calls=mc_calls;memcpy(expected,mc_log,calls*sizeof(*expected));memcpy(mp_after,mp_arena,MP_SIZE);mp_globals(after,0);
        es2_hook hk[2];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<2;++j){o.mode=(mode>>j&1)?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(hooks[j]),&o,function_hook_extension(hooks[j])))return fail("map_click real install");}
        memcpy(mp_arena,mp_before,MP_SIZE);mp_globals(before,1);
        mc_calls=0;sg_poison_call(mapped+d->rva,a,pattern,actual_pair);mp_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=mc_calls||memcmp(expected,mc_log,calls*sizeof(*expected))||memcmp(mp_after,mp_arena,MP_SIZE)||
           memcmp(after,actual,MP_GLOBALS)||get32(mapped+MC_SOUND)){
            fprintf(stderr,"real mode=%u\n",mode);status=mc_report("real",n,pair,calls,expected,after,actual);}
        for(unsigned j=0;j<2;++j){if(hooks[j]==index&&mode>>1)entered+=hk[j].calls;if(!es2_hook_uninstall(hk+j))return fail("map_click real uninstall");}
        mp_globals(before,1);stores+=calls;++total;
    }
    if(mc_patch(saved,2u,0))return 1;
    memcpy(mapped+MC_SOUND,sound,4);
    if(!status&&(!entered||!stores)){fprintf(stderr,"FAIL: %s real pass entered %llu, stores %u\n",d->name,(unsigned long long)entered,stores);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases (map_pick and map_click original/replaced, real 462830 with the sound off; %llu replaced entries, %u stores), 31/a7 stack, pair/calls/arena/globals\n",
        d->name,total,(unsigned long long)entered,stores);
    return status;
}
static int map_click_cases(unsigned index,unsigned limit){
    BYTE saved_globals[MP_GLOBALS];mp_globals(saved_globals,0);
    BYTE saved[MC_IDS][5];if(mc_patch(saved,3u,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_MAP_CLICK_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    memset(mc_stores,0,sizeof(mc_stores));
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];mc_build(n,a,r);status=mc_compare(index,n,a,r);}
    const unsigned plain=mc_stores[0],actors=mc_stores[1],points=mc_stores[2];
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    mp_globals(saved_globals,1);
    if(!limit&&!status)status=mc_installed_probes(index,64);
    mp_globals(saved_globals,1);
    if(mc_patch(saved,3u,0))return 1;
    if(!limit&&!status)status=mc_real(index);
    mp_globals(saved_globals,1);
    if(!status)printf("PASS: %-20s %u cases (%u actor stores, %u point stores, %u without a store); full return pair, ordered calls, arena and globals\n",
        function_hook_descriptor(index)->name,cases,actors,points,plain);
    return status;
}
