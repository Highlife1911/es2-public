/* MECH "player fly" movement 4198f4 (docs/reverse-engineering/hit_tests.md,
 * section "MECH player fly movement"). Direct cases run the original
 * body and the core against logging stubs patched over every direct game
 * callee: euler_matrix, transform_point (writes a planned point),
 * movement_step, terrain_height (answers relative to the probe's Z: below,
 * level, just above, far), sweep (hit or miss; may write the segment length and
 * inverse pose as the real one does), multiply_q10, mech_part_hit (may clear
 * the region flag word of its part), debris_table (may lower the word),
 * debris_burst, the SOS calls 462ac4/463114/46297c, dist3d_delta (answers at
 * the volume clamps), dist_gate and weapon_converge; ordered calls with
 * arguments and entry EAX/EDX/ECX (stack addresses masked), by content the
 * probe points, the pose copy, the segment and the packet at every sweep, the
 * part-hit and debris points and the debris word. The stubs change what the
 * original rereads (region words, the rates R+18h/R+1Ah, the length R+4, the
 * angles A+0Ch/A+0Eh, the position, the player, dead and target fields, the
 * terrain map pointer, the frame's Z f-78h through the segment) and what it
 * must not reread (the region pointer A+20Eh). Compared: the pair, the arena,
 * the image window 49a158..49a1cf (the effect table, the static packet with
 * [49a170] as "a stack address", the probe offsets and guards), the map pointer
 * 4a0be0 and the debris list words, and the callee counters. Then installed
 * 31/a7 poison probes and a real-callee pass with euler_matrix,
 * transform_point, movement_step, multiply_q10, dist3d_delta, dist_gate and
 * debris_table in rotating modes. Identifiers carry the prefix mfl_/MFL_. */
typedef struct mfl_record { uint32_t id,args[4],regs[3],input[20],eax,edx; } mfl_record;
enum { MFL_SIZE=0x800, MFL_A=0x100, MFL_FLAGS=0x400, MFL_FLAGS2=0x440, MFL_T=0x480, MFL_REC=0x500, MFL_REC2=0x520,
    MFL_MAX=64, MFL_IDS=15, MFL_GLOBALS=0x78+4+14 };
#define MFL_BASE ((uint32_t)(uintptr_t)mfl_arena)
static BYTE mfl_arena[MFL_SIZE],mfl_before[MFL_SIZE],mfl_after[MFL_SIZE];
static mfl_record mfl_log[MFL_MAX];
static unsigned mfl_calls;
static int mfl_real;
static uint32_t mfl_seed,mfl_stream;
__attribute__((used)) static uint32_t mfl_ecx;
/* euler_matrix, transform_point, movement_step, terrain_height, sweep,
 * multiply_q10, mech_part_hit, debris_table, debris_burst, 462ac4,
 * dist3d_delta, 463114, 46297c, dist_gate, weapon_converge. */
static const uint32_t mfl_addresses[MFL_IDS]={0x47ebb8,0x48043c,0x467924,0x46e180,0x426528,0x47e0b0,0x4188c8,0x4089bc,
    0x408530,0x462ac4,0x47de72,0x463114,0x46297c,0x49288c,0x41a74c};
static const unsigned mfl_counts[MFL_IDS]={2,3,1,2,1,2,4,1,4,1,3,2,2,2,2};
static uint32_t mfl_next(void){
    uint32_t z=mfl_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *mfl_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int mfl_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static uint32_t mfl_mask(uint32_t value){return mfl_stack(value)?0:value;}
static void mfl_words(mfl_record *v,unsigned at,uint32_t address,unsigned count){
    for(unsigned j=0;j<count&&at+j<20;++j)v->input[at+j]=get32(mfl_host(address)+4*j);
}
/* The region word of a part (8, 7, 4, 5, 0, 6). */
static unsigned mfl_region(uint32_t part){
    switch(part){case 8:return 0x10;case 7:return 0xe;case 4:return 8;case 5:return 0xa;case 6:return 0xc;default:return 0;}
}
/* Rereads of the MECH's state between the calls. */
static void mfl_touch(uint32_t pick){
    if(mfl_real)return;
    BYTE *a=mfl_arena+MFL_A,*r=a+0x2b9;uint32_t b=MFL_BASE;
    if(pick%5==0)put32(r+4,pick>>8&1?mfl_next():mfl_next()%3000u);
    if(pick%7==0)put16(r+0x18,(uint16_t)mfl_next());
    if(pick%11==0)put16(r+0x1a,(uint16_t)mfl_next());
    if(pick%13==0)put16(a+0xc,(uint16_t)mfl_next());
    if(pick%17==0)put16(a+0xe,(uint16_t)mfl_next());
    if(pick%19==0){put32(a+0x26,mfl_next());put32(a+0x2a,mfl_next());put32(a+0x2e,mfl_next()%20000u);}
    if(pick%23==0)a[0xa3]=(BYTE)(pick>>9&1?0:mfl_next());
    if(pick%29==0)a[0x99]=(BYTE)(pick>>10&1?0:mfl_next());
    if(pick%31==0)put32(a+0x1a4,pick>>11&1?0:b+MFL_T);
    if(pick%37==0)put32(a+0x20e,b+(pick>>12&1?MFL_FLAGS2:MFL_FLAGS)); /* read once (f-8) */
    if(pick%41==0)put32(mapped+0xa0be0,mfl_next());
    if(pick%3==0)put16(mfl_arena+MFL_FLAGS+2*(pick>>13&7u),(uint16_t)(pick>>16&1?0:mfl_next()));
}
__attribute__((used,noinline)) static uint64_t __cdecl mfl_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(mfl_calls==MFL_MAX){fail("mech_fly synthetic call overflow");ExitProcess(1);}
    mfl_record *v=mfl_log+mfl_calls++;memset(v,0,sizeof(*v));v->id=id;
    for(unsigned j=0;j<mfl_counts[id];++j)v->args[j]=mfl_mask(a[j]);
    for(unsigned j=0;j<3;++j)v->regs[j]=mfl_mask(regs[j]);
    uint32_t eax=mfl_next(),edx=mfl_next(),pick=mfl_next();mfl_ecx=mfl_next();
    switch(id){
    case 0:mfl_touch(pick);break;
    case 1: /* transform_point: source and pose by content; a planned point */
        mfl_words(v,0,a[0],3);mfl_words(v,3,a[1],8);
        for(unsigned j=0;j<3;++j)put32(mfl_host(a[2])+4*j,pick%4?mfl_next()%40000u-20000u:mfl_next());
        break;
    case 2:break;
    case 3: { /* terrain_height: the probe's Z plus a planned offset */
        mfl_words(v,0,a[1],3);static const int32_t deltas[]={-2,-1,0,1,2,1000,-1000};
        uint32_t z=get32(mfl_host(a[1])+8);eax=pick%9==8?mfl_next():z+(uint32_t)deltas[pick%7];
        mfl_touch(pick>>3);break; }
    case 4: { /* sweep: the segment (pose, length, radius) and the packet */
        uint32_t s=get32(mapped+0x9a170),pose=get32(mfl_host(s));
        v->input[0]=mfl_mask(s);v->input[1]=mfl_mask(pose);mfl_words(v,2,pose,8);
        v->input[10]=get32(mfl_host(s)+4);v->input[11]=get16(mfl_host(s)+8);
        mfl_words(v,12,(uint32_t)(uintptr_t)(mapped+0x9a174),5);
        v->input[17]=get32(mapped+0x9a184);
        eax=pick%5<2?0:eax|1u;
        if(mfl_real)break;
        if(eax&&pick>>4&1){put32(mfl_host(s)+4,mfl_next());for(unsigned j=0;j<32;++j)mfl_host(s)[0xa+j]=(BYTE)mfl_next();}
        if(pick%7==0)put32(mfl_host(s)+0x114-0x78,mfl_next()); /* the frame's Z, reread after the sweep */
        mfl_touch(pick>>5);break; }
    case 5:break;
    case 6: { /* mech_part_hit: the point; may clear its part's region word */
        mfl_words(v,0,a[3],3);
        if(mfl_real)break;
        uint32_t flags=get32(mfl_arena+MFL_A+0x20e);
        if(pick%3==0)put16(mfl_host(flags)+mfl_region(a[1]),0);
        mfl_touch(pick>>2);break; }
    case 7: /* debris_table: the word by content, lowered in a third */
        v->input[0]=get16(mfl_host(a[0]));
        if(pick%3==0)put16(mfl_host(a[0]),(uint16_t)(v->input[0]-(pick>>8&3u)));
        eax=MFL_BASE+(pick>>4&1?MFL_REC:MFL_REC2);
        break;
    case 8:mfl_words(v,0,a[2],3);break;
    case 10: { /* dist3d_delta: the volume clamps 0 and FFFFh */
        static const uint32_t d[]={2345,2346,0xfffff92au,0xfffff92bu,0,0x7fffffffu,0x80000000u,0x0ffff000u};
        eax=pick%9==8?eax:d[pick%8];mfl_touch(pick>>4);break; }
    case 9: case 11: case 12:mfl_touch(pick);break;
    case 13: case 14:break;
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
/* Pushes the entry EAX/EDX/ECX for the record before the slot pointer. */
#define MFL_STUB(n) __attribute__((naked)) static void mfl_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _mfl_stub_impl\n\taddl $24,%esp\n\tmovl _mfl_ecx,%ecx\n\tret\n\t");}
MFL_STUB(0) MFL_STUB(1) MFL_STUB(2) MFL_STUB(3) MFL_STUB(4) MFL_STUB(5) MFL_STUB(6) MFL_STUB(7) MFL_STUB(8) MFL_STUB(9)
MFL_STUB(10) MFL_STUB(11) MFL_STUB(12) MFL_STUB(13) MFL_STUB(14)
#undef MFL_STUB
static void *mfl_stubs[MFL_IDS]={mfl_stub_0,mfl_stub_1,mfl_stub_2,mfl_stub_3,mfl_stub_4,mfl_stub_5,mfl_stub_6,mfl_stub_7,
    mfl_stub_8,mfl_stub_9,mfl_stub_10,mfl_stub_11,mfl_stub_12,mfl_stub_13,mfl_stub_14};
/* The effect table, the static packet, the probe offsets and guards
 * (49a158..49a1cf), the terrain map pointer and the debris list words. */
static const struct { uint32_t rva,size; } mfl_ranges[]={{0x9a158,0x78},{0xa0be0,4},{0xa964c,14}};
static void mfl_globals(BYTE *out,int restore){
    for(unsigned i=0;i<sizeof(mfl_ranges)/sizeof(mfl_ranges[0]);++i){
        if(restore)memcpy(mapped+mfl_ranges[i].rva,out,mfl_ranges[i].size);else memcpy(out,mapped+mfl_ranges[i].rva,mfl_ranges[i].size);
        out+=mfl_ranges[i].size;
    }
}
/* [49a170] holds the segment's address: the original's frame or the
 * adapter's scratch, both on this stack. */
static int mfl_globals_differ(const BYTE *want,const BYTE *got){
    BYTE w[MFL_GLOBALS],g[MFL_GLOBALS];memcpy(w,want,MFL_GLOBALS);memcpy(g,got,MFL_GLOBALS);
    const unsigned at=0x9a170-0x9a158;
    if(mfl_stack(get32(w+at))!=mfl_stack(get32(g+at))){fprintf(stderr," [49a170] %08x/%08x\n",get32(w+at),get32(g+at));return 1;}
    if(mfl_stack(get32(w+at))){put32(w+at,0);put32(g+at,0);}
    for(unsigned j=0;j<MFL_GLOBALS;++j)if(w[j]!=g[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,w[j],g[j]);return 1;}
    return 0;
}
static void mfl_reset(void){mfl_calls=0;mfl_stream=mfl_seed;}
static void mfl_build(unsigned n,uint32_t a[6],uint32_t r[3],int real){
    uint32_t b=MFL_BASE;BYTE *A=mfl_arena+MFL_A,*R=A+0x2b9;
    mfl_seed=random32();random_bytes(mfl_arena,MFL_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    /* Angles and pose, the matrix flag, the integration latch, rates. */
    if(random32()%2)put16(A+0x32,0);
    A[0xa4]=(BYTE)(random32()%2?0:1+random32()%255);
    if(random32()%2)put16(R+0x18,(uint16_t)(0x8000u|random32()));else put16(R+0x18,(uint16_t)(random32()%0x8000u));
    put32(A+0x26,random32()%200000u);put32(A+0x2a,random32()%200000u);put32(A+0x2e,random32()%20000u);
    if(random32()%4)put32(R+4,random32()%3000u);
    for(unsigned i=0;i<3;++i)if(random32()%4)put32(R+0xc+4*i,random32()%512u-256u);
    /* The region words: each 0 in a third. */
    put32(A+0x20e,b+(random32()%2?MFL_FLAGS:MFL_FLAGS2));
    for(unsigned i=0;i<16;++i){put16(mfl_arena+MFL_FLAGS+2*i,(uint16_t)(random32()%3?1+random32()%0xffff:0));
        put16(mfl_arena+MFL_FLAGS2+2*i,(uint16_t)(random32()%3?1+random32()%0xffff:0));}
    A[0xa3]=(BYTE)(random32()%3?1+random32()%255:0);A[0x99]=(BYTE)(random32()%3?0:1+random32()%255);
    put32(A+0x1a4,random32()%3?b+MFL_T:0);
    /* Debris records: a table and the shapes. */
    put32(mfl_arena+MFL_REC,b+0x600);put32(mfl_arena+MFL_REC+6,b+0x700);
    put32(mfl_arena+MFL_REC2,b+0x640);put32(mfl_arena+MFL_REC2+6,b+0x740);
    /* Image statics: the guards 0 together in a quarter, else each 0 in an
     * eighth; random probe offsets and packet; the map pointer. */
    BYTE *s=mapped+0x9a158;
    random_bytes(s+0x18,0x78-0x18);
    int fresh=random32()%4==0;
    for(unsigned i=0;i<5;++i)s[0x3c+13*i]=(BYTE)(fresh||random32()%8==0?0:1+random32()%255);
    put32(mapped+0xa0be0,random32());
    random_bytes(mapped+0xa964c,14);put32(mapped+0xa964c,b+MFL_REC2);
    if(real)put16(mapped+0xa9654,(uint16_t)(random32()%2?random32()%5:random32()));
    (void)n;a[0]=b+MFL_A;
}
static int mfl_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const mfl_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,mfl_calls);
    for(unsigned j=0;j<calls&&j<mfl_calls&&expected;++j)if(memcmp(expected+j,mfl_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(mfl_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<MFL_SIZE;++j)if(mfl_after[j]!=mfl_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,mfl_after[j],mfl_arena[j]);break;}
    return 1;
}
static unsigned mfl_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<mfl_calls;++j)if(c->event==ES2_COLLIDE_CALL&&mfl_addresses[mfl_log[j].id]==c->key)++hits;
    return hits;
}
static int mfl_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[MFL_GLOBALS],after[MFL_GLOBALS],actual[MFL_GLOBALS];static mfl_record expected[MFL_MAX];
    memcpy(mfl_before,mfl_arena,MFL_SIZE);mfl_globals(before,0);
    mfl_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=mfl_calls;memcpy(expected,mfl_log,calls*sizeof(*expected));
    memcpy(mfl_after,mfl_arena,MFL_SIZE);mfl_globals(after,0);
    memcpy(mfl_arena,mfl_before,MFL_SIZE);mfl_globals(before,1);mfl_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];for(unsigned i=0;i<count&&i<16;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);mfl_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count&&i<16;++i)if(c[i].count-counts[i]!=mfl_expected_hits(c+i))return fail("mech_fly callee counter delta");
    if(pair[0]!=pair[1]||calls!=mfl_calls||memcmp(expected,mfl_log,calls*sizeof(*expected))||memcmp(mfl_after,mfl_arena,MFL_SIZE)||
       mfl_globals_differ(after,actual))
        return mfl_report("direct",n,pair,calls,expected);
    mfl_globals(before,1);
    return 0;
}
static int mfl_patch(BYTE saved[MFL_IDS][5],int active[MFL_IDS],unsigned which,int on){
    for(unsigned i=0;i<MFL_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(mfl_addresses[i]-0x400000u,mfl_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(mfl_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static int mfl_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;mfl_build(n,a,r,0);
        BYTE before[MFL_GLOBALS],after[MFL_GLOBALS],actual[MFL_GLOBALS],expected_fpu[108],actual_fpu[108];static mfl_record expected[MFL_MAX];
        memcpy(mfl_before,mfl_arena,MFL_SIZE);mfl_globals(before,0);
        mfl_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=mfl_calls;memcpy(expected,mfl_log,calls*sizeof(*expected));memcpy(mfl_after,mfl_arena,MFL_SIZE);mfl_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("mech_fly poison install");
        memcpy(mfl_arena,mfl_before,MFL_SIZE);mfl_globals(before,1);mfl_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);mfl_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=mfl_calls||memcmp(expected,mfl_log,calls*sizeof(*expected))||memcmp(mfl_after,mfl_arena,MFL_SIZE)||
           mfl_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=mfl_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("mech_fly poison uninstall");mfl_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state\n",d->name,total);
    return status;
}
/* Real callees with the nested hooks in rotating original/replaced/compare
 * modes (compare falls back to replaced where refused): euler_matrix,
 * transform_point, movement_step, multiply_q10, dist3d_delta, dist_gate
 * and debris_table (over the arena's debris records). terrain_height,
 * sweep, mech_part_hit, debris_burst, the SOS calls and weapon_converge
 * stay stubs. Pair, arena and globals are compared against a run with
 * only the nested hooks; the hook under test is original (relocated
 * prologue) or replaced. */
static int mfl_real_pass(unsigned index){
    static const unsigned keep=1u<<3|1u<<4|1u<<6|1u<<8|1u<<9|1u<<11|1u<<12|1u<<14;
    const unsigned nested[7]={ES2_HOOK_INDEX_euler_matrix,ES2_HOOK_INDEX_transform_point,ES2_HOOK_INDEX_movement_step,
        ES2_HOOK_INDEX_multiply_q10,ES2_HOOK_INDEX_dist3d_delta,ES2_HOOK_INDEX_dist_gate,ES2_HOOK_INDEX_debris_table};
    const unsigned count=7;
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[7]={0};
    BYTE saved[MFL_IDS][5];int active[MFL_IDS];
    if(mfl_patch(saved,active,keep,1))return 1;
    mfl_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;mfl_build(n,a,r,1);
        BYTE before[MFL_GLOBALS],after[MFL_GLOBALS],actual[MFL_GLOBALS];
        memcpy(mfl_before,mfl_arena,MFL_SIZE);mfl_globals(before,0);
        es2_hook hk[8];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("mech_fly nested install");}
        mfl_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        memcpy(mfl_after,mfl_arena,MFL_SIZE);mfl_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+7,d,&o,function_hook_extension(index)))return fail("mech_fly caller install");
        memcpy(mfl_arena,mfl_before,MFL_SIZE);mfl_globals(before,1);mfl_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);mfl_globals(actual,0);
        if(expected_pair[0]!=actual_pair[0]||expected_pair[1]!=actual_pair[1]||memcmp(mfl_after,mfl_arena,MFL_SIZE)||mfl_globals_differ(after,actual)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            fprintf(stderr,"real mode=%u\n",mode);mfl_calls=0;status=mfl_report("real",n,pair,0,NULL);
        }
        for(unsigned j=0;j<8;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("mech_fly nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("mech_fly nested uninstall");}
        mfl_globals(before,1);++total;
    }
    mfl_real=0;
    if(mfl_patch(saved,active,0,0))return 1;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, 31/a7 stack, full pair and memory\n",d->name,total,count);
    return status;
}
static int mech_fly_cases(unsigned index,unsigned limit){
    const unsigned which=(1u<<MFL_IDS)-1u;
    BYTE saved[MFL_IDS][5],old_globals[MFL_GLOBALS];int active[MFL_IDS];
    mfl_globals(old_globals,0);
    if(mfl_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_MECH_FLY_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];mfl_build(n,a,r,0);status=mfl_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=mfl_installed_probes(index,64);
    if(mfl_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=mfl_real_pass(index);
    mfl_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, points/pose/segment/packet by content, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
