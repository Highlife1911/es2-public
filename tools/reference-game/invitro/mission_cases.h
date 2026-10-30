/* Mission triggers (mission_runtime.md): trigger_scan 426b70, trigger_regions
 * 4234b8, region_test 4233a4, trigger_group 423508, trigger_fire 423430. Direct
 * cases: every game callee is a logging stub that records its ordered
 * arguments, entry registers and the event record by content (frame addresses
 * masked; the 16 record bytes the original never writes are masked too, because
 * only the installed entry shim reproduces them). Stubs change trigger, group
 * and global state between calls. Then installed 31/a7 poison cases with full
 * records, and a real-callee pass: the group's own hooks, dist2d_point and
 * 433158 run as real code in rotating modes; only the cockpit handler (virtual
 * slot 0) is a stub. */
typedef struct ms_record { uint32_t id,args[2],regs[3],input[6],eax,edx; } ms_record;
enum { MS_SIZE=0x1000, MS_POINTS=0x000, MS_REGIONS=0x080, MS_LISTS=0x0e0, MS_TRIGGERS=0x200, MS_GROUPS=0x400,
    MS_ARRAYS=0x700, MS_ACTORS=0x780, MS_COCKPIT=0xb00, MS_VTABLES=0xb80, MS_HOLDER=0xc00, MS_POSITION=0xe40,
    MS_RESIDUE=0xe80, MS_MAX=128, MS_IDS=7, MS_NTRIGGERS=6, MS_NGROUPS=5, MS_NACTORS=8, MS_NREGIONS=8, MS_NPOINTS=8,
    MS_GLOBALS=0x844 };
#define MS_BASE ((uint32_t)(uintptr_t)ms_arena)
static BYTE ms_arena[MS_SIZE],ms_before[MS_SIZE],ms_after[MS_SIZE];
static ms_record ms_log[MS_MAX];
static unsigned ms_calls;
static int ms_real,ms_full;
static uint32_t ms_seed,ms_stream,ms_threshold,ms_trigger;
__attribute__((used)) static uint32_t ms_ecx;
/* 4234b8, 423508, 423430, 4233a4, 4928d0, 433158, cockpit virtual slot 0
 * (reached through the generated vtables, never patched). */
static const uint32_t ms_addresses[MS_IDS]={0x4234b8,0x423508,0x423430,0x4233a4,0x4928d0,0x433158,0};
static const unsigned ms_counts[MS_IDS]={2,2,1,2,2,0,2};
static uint32_t ms_next(void){
    uint32_t z=ms_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *ms_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
/* A fixed window (the worker's 16 MiB stack reservation), so equal values
 * are masked identically in both runs; frame addresses always fall inside. */
static int ms_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static uint32_t ms_trigger_at(unsigned k){return MS_BASE+MS_TRIGGERS+0x3au*k;}
static uint32_t ms_group_at(unsigned k){return MS_BASE+MS_GROUPS+0x7au*k;}
static uint32_t ms_actor_at(unsigned k){return MS_BASE+MS_ACTORS+0x60u*k;}
/* A nonzero AX of every flavour (sign bit, all ones, one). */
static uint32_t ms_true(uint32_t high,uint32_t pick){
    static const uint16_t values[]={1,0x8000,0xffff,0x7fff,2};
    uint16_t v=values[pick%5];if(pick%7==0)v=(uint16_t)(ms_next()|1u);return (high&0xffff0000u)|v;
}
static void ms_group_fields(BYTE *g,uint32_t pick){
    static const BYTE bytes[]={0,1,2,0xff,0,1};static const uint16_t words[]={0,1,2,3,0,1,2};
    if(pick%3==0)g[0x12]=bytes[(pick>>4)%6];
    if(pick%5==0)put16(g,words[(pick>>8)%7]);
    if(pick%7==0)put32(g+0x14,(pick>>12)%2?0u:ms_next());
}
__attribute__((used,noinline)) static uint64_t __cdecl ms_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(ms_calls==MS_MAX){fail("mission synthetic call overflow");ExitProcess(1);}
    ms_record *v=ms_log+ms_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,4*ms_counts[id]);memcpy(v->regs,regs,12);
    for(unsigned j=0;j<3;++j)if(ms_stack(v->regs[j]))v->regs[j]=0;
    for(unsigned j=0;j<ms_counts[id];++j)if(ms_stack(v->args[j]))v->args[j]=0;
    /* 433158 never reads ECX. Before any action-6 slot it holds 423430's
     * entry ECX, which the entry shim replaces by the residue address. */
    if(id==5)v->regs[2]=0;
    uint32_t eax=ms_next(),edx=ms_next(),pick=ms_next();ms_ecx=ms_next();
    switch(id){
    case 0: case 1: case 3: { /* predicates: mostly false so loops continue */
        uint32_t trigger=id==1?a[1]:id==0?a[0]:ms_trigger;
        eax=pick%4==0?ms_true(eax,pick>>3):eax&0xffff0000u;
        if(pick%5==0&&!ms_real)put16(ms_host(trigger+0xa),(pick>>9)%2?0:(uint16_t)ms_next());
        if(id==3&&pick%9==0)put16(ms_host(trigger+4),(uint16_t)(ms_next()%9u));
        if(id!=3&&pick%11==0)put16(mapped+0xa9e04,(uint16_t)(ms_next()%(MS_NGROUPS+1u)));
        if(id!=3&&pick%13==0)put16(mapped+0xa9e24,(uint16_t)(ms_next()%(MS_NTRIGGERS+1u)));
        if(id!=3&&pick%17==0)ms_group_fields(ms_arena+MS_GROUPS+0x7a*(ms_next()%MS_NGROUPS),ms_next());
        if(id==0&&pick%19==0)put32(mapped+0xa9e28,MS_BASE+MS_TRIGGERS+0x3au*(ms_next()%2u));
        break; }
    case 2: /* 423430: may latch and retype a later trigger */
        if(pick%3==0)put16(ms_host(a[0]+0xa),1);
        if(pick%7==0)put16(ms_host(ms_trigger_at(ms_next()%MS_NTRIGGERS)),(uint16_t)(ms_next()%12u));
        break;
    case 4: { /* 4928d0: distances around the region threshold */
        static const int deltas[]={-1,0,1};
        switch(pick%6){case 0:case 1:case 2:eax=ms_threshold+(uint32_t)deltas[pick%3];break;
            case 3:eax=ms_next()%4096u;break;case 4:eax=0u-ms_next()%4096u;break;default:break;}
        break; }
    case 5: /* 433158: one of the two cockpit objects */
        eax=MS_BASE+MS_COCKPIT+0x40u*((pick>>5)&1u);
        break;
    case 6: { /* cockpit handler: record by content; may rewrite the trigger */
        const BYTE *p=ms_host(a[1]);
        for(unsigned j=0;j<24;++j){
            if(j>=22||(j>=6&&!ms_full))continue;
            v->input[j/4]|=(uint32_t)p[j]<<(8*(j%4));
        }
        if(!ms_real&&pick%5==0){BYTE *t=ms_host(ms_trigger);unsigned k=ms_next()%10u;
            put16(t+0xc+2*k,(uint16_t)((pick>>8)%3?ms_next()%16u:0xffffu));put16(t+0x20+2*k,(uint16_t)(5+ms_next()%2u));}
        if(!ms_real&&pick%7==0)put16(ms_host(ms_trigger+0x34),(uint16_t)((pick>>8)%2?0xffffu:ms_next()%64u));
        if(pick%11==0)put16(mapped+0xa9e70+2*(ms_next()%16u),(uint16_t)ms_next());
        break; }
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
/* Pushes the entry EAX/EDX/ECX for the record before the slot pointer. */
#define MS_STUB(n) __attribute__((naked)) static void ms_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _ms_stub_impl\n\taddl $24,%esp\n\tmovl _ms_ecx,%ecx\n\tret\n\t");}
MS_STUB(0) MS_STUB(1) MS_STUB(2) MS_STUB(3) MS_STUB(4) MS_STUB(5) MS_STUB(6)
#undef MS_STUB
static void *ms_stubs[MS_IDS]={ms_stub_0,ms_stub_1,ms_stub_2,ms_stub_3,ms_stub_4,ms_stub_5,ms_stub_6};
/* Group and trigger globals, the variable bank (4a9e00..4aa643) and the
 * cockpit pointer 49b088. */
static void ms_globals(BYTE *out,int restore){
    if(restore){memcpy(mapped+0xa9e00,out,0x840);memcpy(mapped+0x9b088,out+0x840,4);}
    else{memcpy(out,mapped+0xa9e00,0x840);memcpy(out+0x840,mapped+0x9b088,4);}
}
static void ms_reset(void){ms_calls=0;ms_stream=ms_seed;}
/* Coordinates: direct cases use a tiny range so every strict bound meets
 * its equality, plus signed extremes; real cases a playable range. */
static uint32_t ms_coordinate(int real){
    static const uint32_t extremes[]={0x7fffffffu,0x80000000u,0x80000001u,0x7ffffffeu,0};
    if(real)return (uint32_t)((int32_t)(random32()%4001u)-2000);
    uint32_t k=random32()%16;
    return k<13?(uint32_t)((int32_t)(random32()%7u)-3):extremes[random32()%5];
}
static uint16_t ms_id(void){
    uint32_t k=random32()%20;
    return (uint16_t)(k<8?0xffffu:k<9?(0x8000u|random32()):k<17?random32()%16u:random32()%1000u);
}
static void ms_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    uint32_t b=MS_BASE;ms_seed=random32();random_bytes(ms_arena,MS_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    random_bytes(mapped+0xa9e70,2000);
    for(unsigned i=0;i<MS_NPOINTS;++i){BYTE *p=ms_arena+MS_POINTS+12*i;put32(p,ms_coordinate(real));put32(p+4,ms_coordinate(real));}
    for(unsigned i=0;i<MS_NREGIONS;++i){
        BYTE *q=ms_arena+MS_REGIONS+10*i;uint32_t k=random32()%8;
        put16(q,k<4?0:k<6?1:(uint16_t)random32());
        put32(q+2,b+MS_POINTS+12*(random32()%MS_NPOINTS));
        if(!get16(q))put32(q+6,b+MS_POINTS+12*(random32()%MS_NPOINTS));
        else put32(q+6,real?random32()%3000u:random32()%4?random32()%4096u:random32());
    }
    /* Cockpit: two objects with their own vtables, both slot 0 = the stub. */
    for(unsigned i=0;i<2;++i){put32(ms_arena+MS_COCKPIT+0x40*i,b+MS_VTABLES+0x20*i);put32(ms_arena+MS_VTABLES+0x20*i,(uint32_t)(uintptr_t)ms_stubs[6]);}
    put32(ms_arena+MS_HOLDER+0x207,b+MS_COCKPIT+0x40*(random32()%2));put32(mapped+0x9b088,b+MS_HOLDER);
    for(unsigned i=0;i<MS_NACTORS;++i){BYTE *o=ms_arena+MS_ACTORS+0x60*i;
        put32(o+0x26,ms_coordinate(real));put32(o+0x2a,ms_coordinate(real));put32(o+0x45,ms_group_at(random32()%MS_NGROUPS));}
    for(unsigned k=0;k<MS_NGROUPS;++k){BYTE *g=ms_arena+MS_GROUPS+0x7a*k;
        static const BYTE bytes[]={0,1,2,0xff};static const uint16_t words[]={0,1,2,3};
        put16(g,random32()%8?words[random32()%4]:(uint16_t)random32());
        g[0x12]=random32()%8?bytes[random32()%4]:(BYTE)random32();
        put32(g+0x14,random32()%3?0:random32());
        put32(g+0xc,b+MS_ARRAYS+16*k);
        uint32_t count=real?1+random32()%4:random32()%6;if(!real&&random32()%32==0)count=0x8000u|random32();
        put16(g+0x10,(uint16_t)(count>4&&count<0x8000u?4:count));
        for(unsigned j=0;j<4;++j)put32(ms_arena+MS_ARRAYS+16*k+4*j,ms_actor_at(random32()%MS_NACTORS));
    }
    for(unsigned k=0;k<MS_NTRIGGERS;++k){BYTE *t=ms_arena+MS_TRIGGERS+0x3a*k;
        uint32_t type=random32()%13;if(random32()%16==0)type=random32()%4?0xffffu:random32();
        put16(t,(uint16_t)type);
        put16(t+0xa,random32()%4?0:(uint16_t)(random32()|1u));
        uint32_t regions=random32()%5;if(random32()%8==0)regions=real?0:random32()%3?5+random32()%4:0x8000u|random32();
        put16(t+4,(uint16_t)regions);put32(t+6,b+MS_LISTS+32*k);
        for(unsigned j=0;j<8;++j)put32(ms_arena+MS_LISTS+32*k+4*j,b+MS_REGIONS+10*(random32()%MS_NREGIONS));
        for(unsigned j=0;j<10;++j){put16(t+0xc+2*j,ms_id());static const uint16_t actions[]={5,6,5,6,4,7,0};
            put16(t+0x20+2*j,random32()%8?actions[random32()%7]:(uint16_t)random32());}
        uint32_t s=random32()%10;put16(t+0x34,(uint16_t)(s<3?0xffffu:s<4?(0x8000u|random32()):random32()%0x8000u));
        uint16_t subject=get16(t);
        put32(t+0x36,subject==10?ms_group_at(random32()%MS_NGROUPS):ms_actor_at(random32()%MS_NACTORS));
    }
    put32(mapped+0xa9e28,b+MS_TRIGGERS);put16(mapped+0xa9e24,(uint16_t)(random32()%8==0?(random32()%2?0:0xffffu):1+random32()%MS_NTRIGGERS));
    put32(mapped+0xa9e00,random32()%32?b+MS_GROUPS:0);put16(mapped+0xa9e04,(uint16_t)(random32()%(MS_NGROUPS+1u)));
    ms_trigger=ms_trigger_at(random32()%MS_NTRIGGERS);
    random_bytes(ms_arena+MS_RESIDUE,16);
    if(index==ES2_HOOK_INDEX_trigger_scan){a[0]=ms_actor_at(0);}
    else if(index==ES2_HOOK_INDEX_trigger_regions){a[0]=ms_trigger;a[1]=random32()%3?ms_actor_at(random32()%MS_NACTORS)+0x26u:b+MS_POSITION;
        put32(ms_arena+MS_POSITION,ms_coordinate(real));put32(ms_arena+MS_POSITION+4,ms_coordinate(real));}
    else if(index==ES2_HOOK_INDEX_region_test){
        uint32_t region=b+MS_REGIONS+10*(random32()%MS_NREGIONS);a[0]=region;a[1]=b+MS_POSITION;
        put32(ms_arena+MS_POSITION,ms_coordinate(real));put32(ms_arena+MS_POSITION+4,ms_coordinate(real));
        /* Positions on a corner coordinate +-1 reach every strict edge. */
        if(!get16(ms_host(region))&&random32()%2){uint32_t p=get32(ms_host(region+2+4*(random32()%2)));
            for(unsigned j=0;j<2;++j)if(random32()%2)put32(ms_arena+MS_POSITION+4*j,get32(ms_host(p+4*j))+random32()%3-1u);}
        ms_threshold=get32(ms_host(region+6));}
    else if(index==ES2_HOOK_INDEX_trigger_group){a[0]=ms_group_at(random32()%MS_NGROUPS);a[1]=ms_trigger;}
    else {a[0]=ms_trigger;r[2]=b+MS_RESIDUE;}
    (void)n;
}
static int ms_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const ms_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,ms_calls);
    for(unsigned j=0;j<calls&&j<ms_calls&&expected;++j)if(memcmp(expected+j,ms_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(ms_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<MS_SIZE;++j)if(ms_after[j]!=ms_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,ms_after[j],ms_arena[j]);break;}
    return 1;
}
static unsigned ms_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<ms_calls;++j)if(c->event==ES2_COLLIDE_CALL&&ms_addresses[ms_log[j].id]==c->key)++hits;
    return hits;
}
static int ms_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    static BYTE before[MS_GLOBALS],after[MS_GLOBALS],actual[MS_GLOBALS];static ms_record expected[MS_MAX];
    memcpy(ms_before,ms_arena,MS_SIZE);ms_globals(before,0);
    ms_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=ms_calls;memcpy(expected,ms_log,calls*sizeof(*expected));
    memcpy(ms_after,ms_arena,MS_SIZE);ms_globals(after,0);
    memcpy(ms_arena,ms_before,MS_SIZE);ms_globals(before,1);ms_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[4];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);ms_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=ms_expected_hits(c+i))return fail("mission callee counter delta");
    if(pair[0]!=pair[1]||calls!=ms_calls||memcmp(expected,ms_log,calls*sizeof(*expected))||memcmp(ms_after,ms_arena,MS_SIZE)||memcmp(after,actual,MS_GLOBALS)){
        for(unsigned j=0;j<MS_GLOBALS;++j)if(after[j]!=actual[j]){fprintf(stderr," global byte %x %02x/%02x\n",j,after[j],actual[j]);break;}
        return ms_report("direct",n,pair,calls,expected);
    }
    return 0;
}
static unsigned ms_which(unsigned index){
    return index==ES2_HOOK_INDEX_trigger_scan?0x07u:index==ES2_HOOK_INDEX_trigger_regions?0x0cu:
        index==ES2_HOOK_INDEX_trigger_group?0x01u:index==ES2_HOOK_INDEX_region_test?0x10u:0x20u;
}
static int ms_patch(BYTE saved[MS_IDS][5],int active[MS_IDS],unsigned which,int on){
    for(unsigned i=0;i<MS_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!ms_addresses[i])continue;if(actor_patch(ms_addresses[i]-0x400000u,ms_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(ms_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static int ms_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;ms_full=1;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;ms_build(index,n,a,r,0);
        static BYTE before[MS_GLOBALS],after[MS_GLOBALS],actual[MS_GLOBALS];BYTE expected_fpu[108],actual_fpu[108];static ms_record expected[MS_MAX];
        memcpy(ms_before,ms_arena,MS_SIZE);ms_globals(before,0);
        ms_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=ms_calls;memcpy(expected,ms_log,calls*sizeof(*expected));memcpy(ms_after,ms_arena,MS_SIZE);ms_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("mission poison install");
        memcpy(ms_arena,ms_before,MS_SIZE);ms_globals(before,1);ms_reset();sg_poison_call(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);ms_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=ms_calls||memcmp(expected,ms_log,calls*sizeof(*expected))||memcmp(ms_after,ms_arena,MS_SIZE)||
           memcmp(after,actual,MS_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=ms_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("mission poison uninstall");++total;
    }
    ms_full=0;es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/records/state\n",d->name,total);
    return status;
}
/* Real callees with the nested hooks in rotating original/replaced/compare
 * modes (compare is refused for the mission hooks except region_test and
 * falls back to replaced) and a patterned stack. Groups are never empty here: an empty
 * group returns the high word of ECX, which a replaced callee leaves
 * outside its contract. Records are compared in full when the call trees
 * are identical (caller original) or the hook is trigger_fire itself. */
static int ms_real_pass(unsigned index){
    static const unsigned sets[5][5]={
        {ES2_HOOK_INDEX_trigger_regions,ES2_HOOK_INDEX_trigger_group,ES2_HOOK_INDEX_trigger_fire,ES2_HOOK_INDEX_region_test,ES2_HOOK_INDEX_dist2d_point},
        {ES2_HOOK_INDEX_region_test,ES2_HOOK_INDEX_trigger_fire,ES2_HOOK_INDEX_dist2d_point},
        {ES2_HOOK_INDEX_dist2d_point},
        {ES2_HOOK_INDEX_trigger_regions,ES2_HOOK_INDEX_region_test,ES2_HOOK_INDEX_trigger_fire},
        {0}};
    static const unsigned sizes[5]={5,3,1,3,0};
    unsigned set=index-ES2_HOOK_INDEX_trigger_scan,nested_count=sizes[set];const unsigned *nested=sets[set];
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0,messages=0;uint64_t entered[5]={0};
    ms_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<1024&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;ms_build(index,n,a,r,1);
        static BYTE before[MS_GLOBALS],after[MS_GLOBALS],actual[MS_GLOBALS];static ms_record expected[MS_MAX];
        ms_full=index==ES2_HOOK_INDEX_trigger_fire||mode<3;
        memcpy(ms_before,ms_arena,MS_SIZE);ms_globals(before,0);
        es2_hook hk[6];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<nested_count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("mission nested install");}
        ms_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        unsigned calls=ms_calls;memcpy(expected,ms_log,calls*sizeof(*expected));messages+=calls;
        memcpy(ms_after,ms_arena,MS_SIZE);ms_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+nested_count,d,&o,function_hook_extension(index)))return fail("mission caller install");
        memcpy(ms_arena,ms_before,MS_SIZE);ms_globals(before,1);ms_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);ms_globals(actual,0);
        if(expected_pair[0]!=actual_pair[0]||expected_pair[1]!=actual_pair[1]||calls!=ms_calls||memcmp(expected,ms_log,calls*sizeof(*expected))||
           memcmp(ms_after,ms_arena,MS_SIZE)||memcmp(after,actual,MS_GLOBALS)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            for(unsigned j=0;j<MS_GLOBALS;++j)if(after[j]!=actual[j]){fprintf(stderr," global byte %x %02x/%02x\n",j,after[j],actual[j]);break;}
            fprintf(stderr,"real mode=%u\n",mode);status=ms_report("real",n,pair,calls,expected);
        }
        for(unsigned j=0;j<=nested_count;++j){if(j<nested_count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("mission nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("mission nested uninstall");}
        ms_globals(before,1);++total;
    }
    ms_real=0;ms_full=0;
    for(unsigned j=0;j<nested_count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    /* Every hook but region_test reaches the cockpit handler through a real fire. */
    if(!status&&index!=ES2_HOOK_INDEX_region_test&&!messages)status=fail("mission real pass sent no message");
    if(!status)printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, %u handler calls, 31/a7 stack, full pair, records and memory\n",d->name,total,nested_count,messages);
    return status;
}
/* Compare cases (compare_probes.h) for region_test: real dist2d_point,
 * real-pass inputs. */
static void ms_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){ms_build(index,n,a,r,1);}
static void ms_compare_state(BYTE *s,int restore){
    if(restore){memcpy(ms_arena,s,MS_SIZE);ms_globals(s+MS_SIZE,1);}else{memcpy(s,ms_arena,MS_SIZE);ms_globals(s+MS_SIZE,0);}
}
static int mission_cases(unsigned index,unsigned limit){
    unsigned which=ms_which(index);
    BYTE saved[MS_IDS][5],old_arena[MS_SIZE];int active[MS_IDS];static BYTE old_globals[MS_GLOBALS];
    ms_globals(old_globals,0);memcpy(old_arena,ms_arena,MS_SIZE);ms_full=0;ms_real=0;
    if(limit&&compare_probe_installed(index)){ms_real=1;int status=compare_probe_run(index,limit,ms_compare_build,ms_compare_state,MS_SIZE+MS_GLOBALS);
        ms_real=0;ms_globals(old_globals,1);memcpy(ms_arena,old_arena,MS_SIZE);return status;}
    if(ms_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_MISSION_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];ms_build(index,n,a,r,0);status=ms_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=ms_installed_probes(index,64);
    if(ms_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=ms_real_pass(index);
    if(!limit&&!status&&compare_probe_capable(index)){ms_real=1;status=compare_probe_run(index,0,ms_compare_build,ms_compare_state,MS_SIZE+MS_GLOBALS);ms_real=0;}
    ms_globals(old_globals,1);memcpy(ms_arena,old_arena,MS_SIZE);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, event records by content, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
