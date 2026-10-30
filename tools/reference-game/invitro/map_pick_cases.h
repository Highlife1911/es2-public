/* map_pick group: map_pick 44d888, the HDD map pick (cockpit.md, "map pick
 * 44d888"). Its callee 44d288 is computed in core; the direct cases run the
 * whole original (with the real 44d288) and the core on one arena: the map
 * object M with its screen rectangle, centre/scale and the stored point
 * M+374h..+37Fh, the view object (separate or over the stored point), the
 * cursor point (usually M+11Dh, else elsewhere, also over the stored point), a
 * twelve-slot actor list with a null in the last slot and eight actors (some
 * over the stored point), plus the globals 4a9af8..4a9aff (list pointer and
 * count word) and 4d2538..4d253f (the two half-size shift words), which are set
 * per case, compared and restored. Actor positions are placed around the pick
 * square's edges (x-h, y-h, x+h, y+h, each -1/0/+1) from an independent model
 * of the arithmetic, so every test goes both ways; scale, shift and half-size
 * words cover the masked counts. The full EDX:EAX pair is compared. Then
 * installed 31/a7 poison probes and a real-caller pass: the original 44d6e0
 * (order target) and 44d82c (wingman selection) over the hook in original and
 * replaced mode, with the real 433134 and logging stubs for the sound call
 * 462830, the text update 44dd14 and the selection 44a748. */
enum { MP_SIZE=0x1000, MP_MAP=0x40, MP_VIEW=0x400, MP_LIST=0x480, MP_SLOTS=12, MP_SIDES=0x4c0, MP_ACTORS=0x500,
    MP_ACTOR_COUNT=8, MP_ACTOR_SIZE=0x100, MP_GLOBAL_LIST=0xa9af8, MP_GLOBAL_SHIFT=0xd2538, MP_GLOBAL_MAP=0x9d6ac,
    MP_GLOBAL_WINGMEN=0xd03c8, MP_GLOBALS=8+8+8+12, MP_MAX=8, MP_IDS=3 };
#define MP_BASE ((uint32_t)(uintptr_t)mp_arena)
static BYTE mp_arena[MP_SIZE],mp_before[MP_SIZE],mp_after[MP_SIZE];
static unsigned mp_hits,mp_overlaps;
static void mp_globals(BYTE *out,int restore){
    BYTE *p[4]={mapped+MP_GLOBAL_LIST,mapped+MP_GLOBAL_SHIFT,mapped+MP_GLOBAL_MAP,mapped+MP_GLOBAL_WINGMEN};
    const unsigned len[4]={8,8,8,12};
    for(unsigned i=0,at=0;i<4;at+=len[i++]){if(restore)memcpy(p[i],out+at,len[i]);else memcpy(out+at,p[i],len[i]);}
}
static uint32_t mp_sar(uint32_t x,uint32_t n){n&=31u;return n?(uint32_t)((int32_t)x>>n):x;}
static uint32_t mp_pick(const uint32_t *values,unsigned count){return values[random32()%count];}
/* Coordinates: small, screen-like, large, edges. */
static uint32_t mp_coordinate(void){
    static const uint32_t edges[]={0,1,0xffffffffu,0x7fffffffu,0x80000000u,0x7fffu,0x8000u,0xffff8000u};
    switch(random32()%8){
    case 0:return edges[random32()%8];
    case 1:return random32();
    case 2:return random32()%0x100000u-0x80000u;
    default:return random32()%1024u;
    }
}
/* Fills one state; with `real`, the actors stay apart from the map and
 * carry the fields 44d6e0/44d82c read. a[0] = M, a[1] = the point. */
static void mp_build(unsigned n,uint32_t a[6],uint32_t r[3],int real){
    static const uint32_t scales[]={1,0x100,0x400,0x800,0x10000,2,0};
    static const uint32_t shifts[]={0,1,4,8,10,12,31,32,33,0x104};
    static const uint16_t halves[]={0,1,2,3,4,6,14,15,16,17,31,32,0xffff};
    uint32_t b=MP_BASE,M=b+MP_MAP;BYTE *map=mp_arena+MP_MAP;
    random_bytes(mp_arena,MP_SIZE);memset(a,0,24);for(unsigned i=0;i<3;++i)r[i]=random32();
    /* The rectangle (origin, far corner), the centre and scale. */
    uint32_t x0=mp_coordinate(),y0=mp_coordinate();
    put32(map+0x71,x0);put32(map+0x75,y0);
    put32(map+0x79,random32()%4?x0+random32()%640u:mp_coordinate());put32(map+0x7d,random32()%4?y0+random32()%480u:mp_coordinate());
    put32(map+0x18,mp_coordinate());put32(map+0x1c,mp_coordinate());
    put32(map+0x20,random32()%4?mp_pick(scales,7):random32()%3?random32()%0x2000u:random32());
    uint32_t view=b+MP_VIEW;
    if(!real&&random32()%8==0){view=M+0x35au+random32()%12u;++mp_overlaps;}
    put32(map+0x14,view);
    uint32_t point=M+0x11du;
    if(!real&&random32()%8==0)point=random32()%2?M+0x374u+random32()%9u:b+random32()%(MP_SIZE-8u);
    put32(map+0x11d,x0+random32()%700u-30u);put32(map+0x121,y0+random32()%520u-20u);
    if(random32()%16==0){put32(map+0x11d,mp_coordinate());put32(map+0x121,mp_coordinate());}
    /* The view's shift dword is written last (it may lie over the point). */
    put32((BYTE *)(uintptr_t)view+0x1a,random32()%4?mp_pick(shifts,10):random32());
    put16(mapped+0xd253a,random32()%4?halves[random32()%5]:halves[random32()%13]);
    put16(mapped+0xd253c,random32()%2?halves[random32()%13]:(uint16_t)random32());
    /* The pick square from an independent model of the arithmetic. */
    uint32_t p0=get32((BYTE *)(uintptr_t)point),p1=get32((BYTE *)(uintptr_t)point+4);
    uint32_t r0=p0-get32(map+0x71)-mp_sar(get32(map+0x79)-get32(map+0x71),1);
    uint32_t r1=0u-(p1-get32(map+0x75)-mp_sar(get32(map+0x7d)-get32(map+0x75),1));
    uint32_t k=get32(map+0x20),cl=get32((BYTE *)(uintptr_t)view+0x1a);
    uint32_t px=mp_sar(r0*k,cl)+get32(map+0x18),py=mp_sar(r1*k,cl)+get32(map+0x1c);
    unsigned w=get16(mapped+0xd253a)&31u;uint32_t half=(uint32_t)(int16_t)(uint16_t)(w<16u?5u<<w:0u);
    uint32_t h=px-(mp_sar((r0-mp_sar(half,1))*k,cl)+get32(map+0x18));
    /* Actors: around the edges in most cases. */
    for(unsigned i=0;i<MP_ACTOR_COUNT;++i){
        BYTE *t=mp_arena+MP_ACTORS+MP_ACTOR_SIZE*i;uint32_t c[2]={px,py};
        for(unsigned j=0;j<2;++j){
            switch(random32()%10){
            case 0:c[j]=c[j]-h+random32()%3u-1u;break;
            case 1:c[j]=c[j]+h+random32()%3u-1u;break;
            case 2:c[j]=c[j]-h+2u;break;
            case 3:c[j]=mp_coordinate();break;
            case 4:c[j]=random32();break;
            default:c[j]+=random32()%2?random32()%3u-1u:(h&0x7fffffffu?random32()%(h&0x7fffffffu):0u)*(random32()%2?1u:0xffffffffu);break;
            }
        }
        put32(t+0x26,c[0]);put32(t+0x2a,c[1]);
        if(real){put32(t+0x45,b+MP_SIDES+4u*(random32()%4));t[0x99]=(BYTE)(random32()%3?0:1+random32()%255);}
    }
    mp_arena[MP_SIDES+0x12]=0;mp_arena[MP_SIDES+0x16]=1;
    /* The list: actors (some over the stored point), nulls, a final null. */
    for(unsigned s=0;s<MP_SLOTS;++s){
        uint32_t entry=b+MP_ACTORS+MP_ACTOR_SIZE*(random32()%MP_ACTOR_COUNT);
        if(s==MP_SLOTS-1u||random32()%12==0)entry=0;
        else if(!real&&random32()%16==0){entry=M+0x34eu+random32()%9u;++mp_overlaps;}
        put32(mp_arena+MP_LIST+4*s,entry);
    }
    put32(mapped+MP_GLOBAL_LIST,b+MP_LIST);
    uint16_t count=n<64?(uint16_t)(n%14):random32()%8?(uint16_t)(random32()%14):random32()%2?(uint16_t)(0x8000u|random32()):(uint16_t)random32();
    put16(mapped+MP_GLOBAL_LIST+6,count);
    a[0]=M;a[1]=point;
}
static int mp_report(const char *what,unsigned n,const uint64_t pair[2],const BYTE *want,const BYTE *got){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1]);
    for(unsigned j=0;j<MP_SIZE;++j)if(mp_after[j]!=mp_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,mp_after[j],mp_arena[j]);break;}
    for(unsigned j=0;j<MP_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);break;}
    return 1;
}
static int mp_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[MP_GLOBALS],after[MP_GLOBALS],actual[MP_GLOBALS];
    memcpy(mp_before,mp_arena,MP_SIZE);mp_globals(before,0);
    uint64_t pair[2];pair[0]=weapons_original(index,a,r);
    memcpy(mp_after,mp_arena,MP_SIZE);mp_globals(after,0);
    memcpy(mp_arena,mp_before,MP_SIZE);mp_globals(before,1);
    pair[1]=x->replacement_context(a,d->user,r);mp_globals(actual,0);
    if((uint32_t)pair[0])++mp_hits;
    if(pair[0]!=pair[1]||memcmp(mp_after,mp_arena,MP_SIZE)||memcmp(after,actual,MP_GLOBALS))
        return mp_report("direct",n,pair,after,actual);
    return 0;
}
static int mp_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;mp_build(n*37u+5u,a,r,0);
        BYTE before[MP_GLOBALS],after[MP_GLOBALS],actual[MP_GLOBALS],expected_fpu[108],actual_fpu[108];
        memcpy(mp_before,mp_arena,MP_SIZE);mp_globals(before,0);
        sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        memcpy(mp_after,mp_arena,MP_SIZE);mp_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("map_pick poison install");
        memcpy(mp_arena,mp_before,MP_SIZE);mp_globals(before,1);
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);es2_hook_fpu_save(actual_fpu);mp_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||memcmp(mp_after,mp_arena,MP_SIZE)||memcmp(after,actual,MP_GLOBALS)||
           memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=mp_report(fast?"installed fast":"installed preserving",n,pair,after,actual);
        if(!es2_hook_uninstall(&h))return fail("map_pick poison uninstall");mp_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/arena/globals\n",d->name,total);
    return status;
}
/* Logging stubs for the callers' other callees: ordered calls with their
 * argument and entry EAX/EDX/ECX (stack values masked); deterministic
 * results. */
typedef struct mp_record { uint32_t id,arg,regs[3]; } mp_record;
static mp_record mp_log[MP_MAX];
static unsigned mp_calls;
__attribute__((used)) static uint32_t mp_ecx;
static const uint32_t mp_addresses[MP_IDS]={0x462830,0x44dd14,0x44a748};
static int mp_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
__attribute__((used,noinline)) static uint64_t __cdecl mp_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(mp_calls==MP_MAX){fail("map_pick synthetic call overflow");ExitProcess(1);}
    mp_record *v=mp_log+mp_calls++;memset(v,0,sizeof(*v));v->id=id;v->arg=a[0];memcpy(v->regs,regs,12);
    for(unsigned j=0;j<3;++j)if(mp_stack(v->regs[j]))v->regs[j]=0;
    mp_ecx=0x3c000000u+mp_calls;return (uint64_t)(0x2b000000u+mp_calls)<<32|(0x1a000000u+mp_calls);
}
#define MP_STUB(n) __attribute__((naked)) static void mp_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _mp_stub_impl\n\taddl $24,%esp\n\tmovl _mp_ecx,%ecx\n\tret\n\t");}
MP_STUB(0) MP_STUB(1) MP_STUB(2)
#undef MP_STUB
static void *mp_stubs[MP_IDS]={mp_stub_0,mp_stub_1,mp_stub_2};
static int mp_patch(BYTE saved[MP_IDS][5],int on){
    for(unsigned i=0;i<MP_IDS;++i)if(actor_patch(mp_addresses[i]-0x400000u,on?mp_stubs[i]:NULL,saved[i],on?TRUE:FALSE))return 1;
    return 0;
}
/* The caller fields: map mode M+5Dh (0Bh..0Eh, else other), the selected
 * wingman M+105h (0..2), the wingman table 4d03c8 (433134) holding listed
 * actors, the map pointer 49d6b0 (44d82c passes [[49d6b0]+8]). */
static void mp_callers(uint32_t M){
    static const uint32_t modes[]={0xb,0xc,0xd,0xe,0xb,0xc,0xa,0xf};
    BYTE *map=(BYTE *)(uintptr_t)M;
    put32(map+0x5d,random32()%8?mp_pick(modes,8):random32());put32(map+0x105,random32()%3);
    for(unsigned i=0;i<3;++i)put32(mapped+MP_GLOBAL_WINGMEN+4*i,random32()%3?MP_BASE+MP_ACTORS+MP_ACTOR_SIZE*(random32()%MP_ACTOR_COUNT):random32()%2?0:random32());
    put32(mapped+MP_GLOBAL_MAP+4,MP_BASE+MP_VIEW+0x40);
}
static int mp_real(unsigned index){
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0,stubbed=0;uint64_t entered=0;
    BYTE saved[MP_IDS][5];if(mp_patch(saved,1))return 1;
    for(unsigned caller=0;caller<2&&!status;++caller)for(unsigned mode=0;mode<2&&!status;++mode)for(unsigned n=0;n<768&&!status;++n){
        uint32_t a[6],r[3],w[6]={0},expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u,target=caller?0x4d82cu:0x4d6e0u;
        mp_build(n*61u+caller*7u+mode,a,r,1);mp_callers(a[0]);w[0]=a[0];
        BYTE before[MP_GLOBALS],after[MP_GLOBALS],actual[MP_GLOBALS];static mp_record expected[MP_MAX];
        memcpy(mp_before,mp_arena,MP_SIZE);mp_globals(before,0);
        mp_calls=0;sg_poison_call(mapped+target,w,pattern,expected_pair);
        unsigned calls=mp_calls;memcpy(expected,mp_log,calls*sizeof(*expected));memcpy(mp_after,mp_arena,MP_SIZE);mp_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        o.mode=mode?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("map_pick real install");
        memcpy(mp_arena,mp_before,MP_SIZE);mp_globals(before,1);
        mp_calls=0;sg_poison_call(mapped+target,w,pattern,actual_pair);mp_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=mp_calls||memcmp(expected,mp_log,calls*sizeof(*expected))||memcmp(mp_after,mp_arena,MP_SIZE)||
           memcmp(after,actual,MP_GLOBALS)){
            fprintf(stderr,"real caller=%x mode=%u calls %u/%u\n",target+0x400000u,mode,calls,mp_calls);status=mp_report("real",n,pair,after,actual);}
        if(mode)entered+=h.calls;stubbed+=calls;
        if(!es2_hook_uninstall(&h))return fail("map_pick real uninstall");mp_globals(before,1);++total;
    }
    if(mp_patch(saved,0))return 1;
    if(!status&&(!entered||!stubbed)){fprintf(stderr,"FAIL: %s real pass entered %llu, stub calls %u\n",d->name,(unsigned long long)entered,stubbed);status=1;}
    if(!status)printf("PASS: %s %u real-caller cases under 44d6e0/44d82c (%llu replaced entries, %u stub calls), original/replaced, 31/a7 stack, pair/calls/arena/globals\n",
        d->name,total,(unsigned long long)entered,stubbed);
    return status;
}
static int map_pick_cases(unsigned index,unsigned limit){
    BYTE saved_globals[MP_GLOBALS];mp_globals(saved_globals,0);
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_MAP_PICK_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    mp_hits=mp_overlaps=0;
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];mp_build(n,a,r,0);status=mp_compare(index,n,a,r);}
    const unsigned hits=mp_hits,overlaps=mp_overlaps;
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    mp_globals(saved_globals,1);
    if(!limit&&!status)status=mp_installed_probes(index,64);
    mp_globals(saved_globals,1);
    if(!limit&&!status)status=mp_real(index);
    mp_globals(saved_globals,1);
    if(!status)printf("PASS: %-20s %u cases (%u hits, %u overlapping placements); full return pair, arena and globals\n",
        function_hook_descriptor(index)->name,cases,hits,overlaps);
    return status;
}
