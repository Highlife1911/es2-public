/* math_lib group: tex_edge 48aaac, the edge setup of the textured polygon fill
 * (player_movement.md, "graphics-library candidates and the texture edge setup
 * 48aaac"). Direct cases run the original body and the core on one arena: the
 * edge record, the screen points and the texture points (with slack on both
 * sides for wrapped indices), and the globals 6c5f80..6c5f8f (end vertex,
 * count, the two array pointers), which are set per case, compared with a
 * window around them and restored afterwards. In a share of the cases the
 * record lies over one of the arrays, so its writes change what the original
 * reads later. The entry EDX is random (returned when the start is the end
 * vertex). States the original cannot finish (a #DE in 47e068 or at the x
 * division, a walk that never ends) are excluded by an emulation of the walk on
 * a copy of the arena; such a state is regenerated. Then installed 31/a7 poison
 * probes and the real caller 48aa50 (the edge stepper) over the hook in
 * original and replaced mode. The Q16 divide 47e068 is never patched. */
enum { ML_SIZE=0x800, ML_EDGE=0x40, ML_SCREEN=0x200, ML_TEXTURE=0x500, ML_SLOTS=28, ML_SLACK=4,
    ML_GLOBALS=0x2c5f78, ML_GLOBALS_SIZE=32 };
#define ML_BASE ((uint32_t)(uintptr_t)ml_arena)
static BYTE ml_arena[ML_SIZE],ml_before[ML_SIZE],ml_after[ML_SIZE],ml_scratch[ML_SIZE];
static unsigned ml_aliased,ml_fallbacks;
static void ml_globals(BYTE *out,int restore){
    if(restore)memcpy(mapped+ML_GLOBALS,out,ML_GLOBALS_SIZE);else memcpy(out,mapped+ML_GLOBALS,ML_GLOBALS_SIZE);
}
static int ml_inside(uint32_t a,uint32_t size){return a-ML_BASE<=ML_SIZE-size;}
static uint32_t ml_read(uint32_t a,int *ok){if(!ml_inside(a,4)){*ok=0;return 0;}return get32(ml_scratch+(a-ML_BASE));}
/* Coordinates: screen-like, a few shared rows (equal y), random, extremes. */
static uint32_t ml_coordinate(unsigned kind){
    static const uint32_t edges[]={0,1,0xffffffffu,0x7fffffffu,0x80000000u,0x7fffu,0x8000u,0xffff8000u,0x10000u,0xffff0000u};
    switch(random32()%(kind?6:8)){
    case 0:return edges[random32()%10];
    case 1:return random32();
    case 2:return random32()%0x20000u-0x10000u;
    case 3:return random32()%2048u-1024u;
    case 6:case 7:return 8u*(random32()%3);
    default:return kind?random32()%256u:random32()%640u;
    }
}
/* Fills the arrays and globals for one state; returns the edge address. */
static uint32_t ml_state(unsigned n,uint32_t *vertex,int *alias){
    static const uint32_t counts[]={1,2,3,4,5,6,8,16,0,0xffffffffu};
    static const uint32_t steps[]={0,2,0xfffffffeu,5,0xfffffffbu,0x7fffffffu,0x80000000u,3};
    uint32_t b=ML_BASE,screen=b+ML_SCREEN,texture=b+ML_TEXTURE;
    random_bytes(ml_arena,ML_SIZE);
    uint32_t count=n<1024?counts[n%10]:random32()%8==0?counts[random32()%10]:3u+random32()%14u;
    uint32_t span=count-1u<16u?count:16u,end,start;
    /* Element rows: one coordinate kind per array, sometimes all rows on
     * three levels (many zero-height edges). */
    int flat=random32()%3==0;
    for(int k=-ML_SLACK;k<ML_SLOTS-ML_SLACK;++k){
        BYTE *p=ml_arena+ML_SCREEN+8*k,*t=ml_arena+ML_TEXTURE+8*k;
        put32(p,ml_coordinate(0));put32(p+4,flat?8u*(random32()%3):ml_coordinate(0));
        put32(t,ml_coordinate(1));put32(t+4,ml_coordinate(1));
    }
    uint32_t direction=random32()%8?(random32()%2?1u:0xffffffffu):steps[random32()%8];
    if(n<1024)direction=(n>>4)%8==0?steps[(n>>7)%8]:(n>>4)&1?1u:0xffffffffu;
    end=random32()%span;start=random32()%span;
    switch(random32()%16){
    case 0:start=end;break;
    case 1:start=random32()%(span+5u)-2u;break;
    case 2:end=random32()%(span+5u)-2u;break;
    default:break;
    }
    uint32_t edge=b+ML_EDGE;*alias=0;
    if(random32()%8==0&&count>=2u&&count<=8u){
        /* The record over an array (dword aligned or not); the direction
         * word it holds is written last, so the original reads it there. */
        uint32_t base=random32()%2?screen:texture;
        edge=base+4u*(random32()%14)-8u+(random32()%4==0?random32()%4u:0u);
        direction=random32()%2?1u:0xffffffffu;start=random32()%count;end=random32()%count;*alias=1;
    }
    put32(mapped+0x2c5f80,end);put32(mapped+0x2c5f84,count);put32(mapped+0x2c5f88,screen);put32(mapped+0x2c5f8c,texture);
    put32((BYTE *)(uintptr_t)edge,direction);
    *vertex=start;return edge;
}
/* Emulates the walk (with the +4 store) on a copy of the arena and checks
 * the divisions; 0 when the original would fault, loop or read outside. */
static int ml_valid(uint32_t edge,uint32_t from,int alias){
    memcpy(ml_scratch,ml_arena,ML_SIZE);
    const uint32_t end=get32(mapped+0x2c5f80),count=get32(mapped+0x2c5f84),screen=get32(mapped+0x2c5f88),texture=get32(mapped+0x2c5f8c);
    int ok=ml_inside(edge,0x34);uint32_t to=0,dy=0;
    for(unsigned step=0;;++step){
        if(!ok||step>64)return 0;
        if(from==end)return 1;
        to=ml_read(edge,&ok)+from;
        if(!((int32_t)to<(int32_t)count))to=0;else if((int32_t)to<0)to=count-1u;
        dy=ml_read(screen+8u*to+4u,&ok)-ml_read(screen+8u*from+4u,&ok);
        if(!ok)return 0;
        put32(ml_scratch+(edge+4u-ML_BASE),dy);
        if(dy)break;
        from=to;
    }
    for(unsigned j=0;j<2;++j){ml_read(screen+8u*(j?to:from),&ok);ml_read(screen+8u*(j?to:from)+4u,&ok);
        ml_read(texture+8u*(j?to:from),&ok);ml_read(texture+8u*(j?to:from)+4u,&ok);}
    if(!ok)return 0;
    int32_t h=(int32_t)dy;
    if(h!=-1&&h>-32768&&h<32768)return 1;   /* no quotient can leave 32 bits */
    if(alias||(uint32_t)(dy<<16)==0)return 0;
    for(unsigned j=0;j<2;++j){
        uint32_t a=(get32(ml_scratch+(texture+8u*to+4u*j-ML_BASE))<<16)-(get32(ml_scratch+(texture+8u*from+4u*j-ML_BASE))<<16);
        int64_t q=(int64_t)(int32_t)a*65536/(int64_t)(int32_t)(dy<<16);
        if(q<INT32_MIN||q>INT32_MAX)return 0;
    }
    uint32_t dx=get32(ml_scratch+(screen+8u*to-ML_BASE))-get32(ml_scratch+(screen+8u*from-ML_BASE));
    if((int32_t)dx<0)dx=0u-dx;
    return !(dx==0x80000000u&&h==-1);
}
static void ml_build(unsigned n,uint32_t a[6],uint32_t r[3],int *alias){
    memset(a,0,24);for(unsigned i=0;i<3;++i)r[i]=random32();
    uint32_t vertex,edge;
    for(unsigned attempt=0;;++attempt){
        edge=ml_state(n,&vertex,alias);
        if(ml_valid(edge,vertex,*alias))break;
        if(attempt==16){vertex=get32(mapped+0x2c5f80);++ml_fallbacks;break;}   /* start at the end: returns at once */
    }
    ml_aliased+=(unsigned)*alias;
    a[0]=edge;a[1]=vertex;
}
static int ml_report(const char *what,unsigned n,const uint64_t pair[2],const BYTE *want,const BYTE *got){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1]);
    for(unsigned j=0;j<ML_SIZE;++j)if(ml_after[j]!=ml_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,ml_after[j],ml_arena[j]);break;}
    for(unsigned j=0;j<ML_GLOBALS_SIZE;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);break;}
    return 1;
}
static int ml_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[ML_GLOBALS_SIZE],after[ML_GLOBALS_SIZE],actual[ML_GLOBALS_SIZE];
    memcpy(ml_before,ml_arena,ML_SIZE);ml_globals(before,0);
    uint64_t pair[2];pair[0]=weapons_original(index,a,r);
    memcpy(ml_after,ml_arena,ML_SIZE);ml_globals(after,0);
    memcpy(ml_arena,ml_before,ML_SIZE);ml_globals(before,1);
    pair[1]=x->replacement_context(a,d->user,r);ml_globals(actual,0);
    if(pair[0]!=pair[1]||memcmp(ml_after,ml_arena,ML_SIZE)||memcmp(after,actual,ML_GLOBALS_SIZE))
        return ml_report("direct",n,pair,after,actual);
    return 0;
}
static int ml_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;int alias;ml_build(n*37u,a,r,&alias);
        BYTE before[ML_GLOBALS_SIZE],after[ML_GLOBALS_SIZE],actual[ML_GLOBALS_SIZE],expected_fpu[108],actual_fpu[108];
        memcpy(ml_before,ml_arena,ML_SIZE);ml_globals(before,0);
        sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        memcpy(ml_after,ml_arena,ML_SIZE);ml_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("math_lib poison install");
        memcpy(ml_arena,ml_before,ML_SIZE);ml_globals(before,1);
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);es2_hook_fpu_save(actual_fpu);ml_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||memcmp(ml_after,ml_arena,ML_SIZE)||memcmp(after,actual,ML_GLOBALS_SIZE)||
           memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=ml_report(fast?"installed fast":"installed preserving",n,pair,after,actual);
        if(!es2_hook_uninstall(&h))return fail("math_lib poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/arena/globals\n",d->name,total);
    return status;
}
/* The real caller 48aa50(E): it decrements E+4 and calls 48aaac(E, [E+8])
 * when that reaches zero (E+4 = 1 in three of four cases), else it steps
 * the record. tex_edge original or replaced; 31/a7 stack. */
static int ml_real(unsigned index){
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered=0;
    for(unsigned mode=0;mode<2&&!status;++mode)for(unsigned n=0;n<1536&&!status;++n){
        uint32_t a[6],r[3],w[6]={0},expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;int alias;
        do ml_build(n*61u+mode,a,r,&alias);while(alias);
        BYTE *e=(BYTE *)(uintptr_t)a[0];
        put32(e+4,n%4?1u:random32());put32(e+8,a[1]);w[0]=a[0];
        BYTE before[ML_GLOBALS_SIZE],after[ML_GLOBALS_SIZE],actual[ML_GLOBALS_SIZE];
        memcpy(ml_before,ml_arena,ML_SIZE);ml_globals(before,0);
        sg_poison_call(mapped+0x8aa50,w,pattern,expected_pair);memcpy(ml_after,ml_arena,ML_SIZE);ml_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        o.mode=mode?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("math_lib real install");
        memcpy(ml_arena,ml_before,ML_SIZE);ml_globals(before,1);
        sg_poison_call(mapped+0x8aa50,w,pattern,actual_pair);ml_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||memcmp(ml_after,ml_arena,ML_SIZE)||memcmp(after,actual,ML_GLOBALS_SIZE)){
            fprintf(stderr,"real mode=%u\n",mode);status=ml_report("real",n,pair,after,actual);}
        if(mode)entered+=h.calls;
        if(!es2_hook_uninstall(&h))return fail("math_lib real uninstall");++total;
    }
    if(!status&&!entered){fprintf(stderr,"FAIL: %s real pass never entered the hook\n",d->name);status=1;}
    if(!status)printf("PASS: %s %u real-caller cases under 48aa50 (%llu replaced entries), original/replaced, 31/a7 stack, pair/arena/globals\n",
        d->name,total,(unsigned long long)entered);
    return status;
}
static int math_lib_cases(unsigned index,unsigned limit){
    BYTE saved_globals[ML_GLOBALS_SIZE];ml_globals(saved_globals,0);
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_MATH_LIB_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    ml_aliased=ml_fallbacks=0;
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];int alias;ml_build(n,a,r,&alias);status=ml_compare(index,n,a,r);}
    const unsigned aliased=ml_aliased,fallbacks=ml_fallbacks;
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    if(!limit&&!status)status=ml_installed_probes(index,64);
    if(!limit&&!status)status=ml_real(index);
    ml_globals(saved_globals,1);
    if(!status)printf("PASS: %-20s %u cases (%u with the record over an array, %u regenerated to start at the end); full return pair, arena and globals 6c5f78..6c5f97\n",
        function_hook_descriptor(index)->name,cases,aliased,fallbacks);
    return status;
}
