/* MECH combat states: engage_end 41c478, skirt_check 41de9c, attack_step
 * 41c594, base_attack 41c86c, flyer_attack 41c9cc, face_off 41d41c,
 * flank_attack 41d4e4 and drive_off 41def0 (docs/reverse-engineering/ai.md,
 * "MECH combat states"). Direct cases run the original body and the core
 * against logging stubs for the game callees of the hook under test (ordered
 * argument slots, entry EAX/EDX/ECX, the steering record and the points by
 * content, stack addresses masked); pair, arena and the stagger word are
 * compared. Then installed 31/a7 poison probes; for the six steps (entry shims,
 * calls at the original stack coordinate) the stubs also record EBX, ESI, EDI,
 * EBP, ESP and stack arguments relative to the caller's argument slots, and the
 * 64 bytes below the slots are compared after return. Then a real-callee pass
 * per hook with the nested hooks in rotating modes (aim_select and face_off
 * with their shims; a quarter of the cases model a replaced aim_fire by filling
 * 65 pages below its stub). Identifiers use the prefix mcs_/MCS_. */
typedef struct mcs_record { uint32_t id,args[4],regs[3],input[6],eax,edx,saved[5]; } mcs_record;
enum { MCS_SIZE=0x2100, MCS_ACT=0x100, MCS_STRIDE=0x340, MCS_ACTORS=6, MCS_GRP=0x1600, MCS_CFG=0x1700, MCS_TBL=0x1900,
    MCS_VT=0x2000, MCS_MAX=96, MCS_IDS=21, MCS_GLOBALS=2 };
#define MCS_BASE ((uint32_t)(uintptr_t)mcs_arena)
#define MCS_ACTOR(i) (MCS_BASE+MCS_ACT+MCS_STRIDE*(uint32_t)(i))
#define MCS_GROUP(i) (MCS_BASE+MCS_GRP+0x80u*(uint32_t)(i))
static BYTE mcs_arena[MCS_SIZE],mcs_before[MCS_SIZE],mcs_after[MCS_SIZE];
static mcs_record mcs_log[MCS_MAX];
static unsigned mcs_calls;
static int mcs_real,mcs_deep;
static uint32_t mcs_seed,mcs_stream;
__attribute__((used)) static uint32_t mcs_ecx;
/* EBX, ESI, EDI, EBP, ESP at the last stub entry; mcs_invoke's ESP before
 * its call; the 64 bytes below the argument slots after its call. */
__attribute__((used)) static uint32_t mcs_entry[5],mcs_origin;
__attribute__((used)) static BYTE mcs_frame_copy[64];
static int mcs_track;
/* 0 41de9c skirt_check, 1 41e758 target_bearing, 2 4929fc point_offset,
 * 3 492934 heading_point, 4 41e828 drive_apply, 5 41ea7c aim_select,
 * 6 41c4a8 engage_done, 7 41c478 engage_end, 8 41c72c flank_step,
 * 9 423918 order_target, 10 41d41c face_off, 11 413e50 script_start,
 * 12 49288c dist_gate; kept in the real passes: 13 416a04
 * vehicle_control, 14 41e984 aim_target, 15 41f5a0 aim_fire, 16 411cb4
 * target_class; 17..20 the virtuals +3Ch, +30h, +54h, +58h (arena vtable,
 * never patched). */
static const uint32_t mcs_addresses[MCS_IDS]={0x41de9c,0x41e758,0x4929fc,0x492934,0x41e828,0x41ea7c,0x41c4a8,0x41c478,
    0x41c72c,0x423918,0x41d41c,0x413e50,0x49288c,0x416a04,0x41e984,0x41f5a0,0x411cb4,0,0,0,0};
static const unsigned mcs_slots[MCS_IDS]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x3c,0x30,0x54,0x58};
static const unsigned mcs_counts[MCS_IDS]={1,3,3,2,2,3,1,1,2,2,1,2,2,4,3,4,1,1,3,2,3};
static void mcs_globals(BYTE *out,int restore){
    BYTE *g=mapped+0xa9b70;if(restore)memcpy(g,out,2);else memcpy(out,g,2);
}
static uint32_t mcs_next(void){
    uint32_t z=mcs_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *mcs_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int mcs_in(uint32_t a,uint32_t n){return a-MCS_BASE<MCS_SIZE&&MCS_SIZE-(a-MCS_BASE)>=n;}
/* Frame addresses (the committed stack window): masked in direct runs,
 * relative to the caller's argument slots in the shim-tracked runs. */
static int mcs_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static uint32_t mcs_mask(uint32_t value){return mcs_stack(value)?(mcs_track?value-mcs_origin:0):value;}
static int mcs_ok(uint32_t p,uint32_t n){
    const NT_TIB *t=(const NT_TIB *)NtCurrentTeb();uint32_t low=(uint32_t)(uintptr_t)t->StackLimit,top=(uint32_t)(uintptr_t)t->StackBase;
    return mcs_in(p,n)||(p>=low&&p<top&&top-p>=n);
}
static void mcs_dwords(mcs_record *v,unsigned at,uint32_t p,unsigned n){
    if(mcs_ok(p,4u*n))for(unsigned j=0;j<n;++j)v->input[at+j]=get32(mcs_host(p)+4*j);
}
static uint32_t mcs_actor_pick(uint32_t pick){return MCS_ACTOR(pick%MCS_ACTORS);}
static uint16_t mcs_pick16(const uint16_t *values,unsigned count,uint32_t pick){
    return pick%5?values[(pick>>3)%count]:(uint16_t)(pick>>8);
}
/* A model of a replaced aim_fire under a stub: 64 pages of shim gap plus a
 * page of host frames, filled with a pattern below the stub. */
__attribute__((naked,noinline)) static void mcs_deep_fill(void){
    __asm__ volatile("pushl %edi\n\tmovl %esp,%edx\n\tmovl $65,%ecx\n\t1: subl $4096,%esp\n\torl $0,(%esp)\n\tdecl %ecx\n\tjnz 1b\n\t"
        "movl %esp,%edi\n\tmovl $0x5d5d5d5d,%eax\n\tmovl $66560,%ecx\n\tcld\n\trep stosl\n\tmovl %edx,%esp\n\tpopl %edi\n\tret\n\t");
}
__attribute__((used,noinline)) static uint64_t __cdecl mcs_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(mcs_calls==MCS_MAX){fail("mech combat call overflow");ExitProcess(1);}
    mcs_record *v=mcs_log+mcs_calls++;memset(v,0,sizeof(*v));v->id=id;
    if(mcs_track){for(unsigned j=0;j<3;++j)v->saved[j]=mcs_mask(mcs_entry[j]);v->saved[3]=mcs_entry[3]-mcs_origin;v->saved[4]=mcs_entry[4]-mcs_origin;}
    for(unsigned k=0;k<mcs_counts[id];++k)v->args[k]=mcs_mask(a[k]);
    for(unsigned j=0;j<3;++j)v->regs[j]=mcs_mask(regs[j]);
    uint32_t eax=mcs_next(),edx=mcs_next(),pick=mcs_next();mcs_ecx=mcs_next();
    BYTE *o=mcs_host(MCS_ACTOR(0));
    /* Perturbations of what the originals reread (direct and installed
     * cases only: the real passes keep the game's order). */
    const int perturb=!mcs_real;
    switch(id){
    case 0: /* skirt_check: AX zero three times in four; the target may change. */
        if(pick%4)eax&=0xffff0000u;else if(!(uint16_t)eax)eax|=1u;
        if(perturb&&pick%8==1)put32(o+0x1a4,mcs_actor_pick(pick>>8));
        break;
    case 1: { /* target_bearing: fills the 18h-byte record on the edges of the steps. */
        static const uint16_t bear[]={0x1fff,0x2000,0x2001,0x4000,0x4001,0x7060,0x7061,0x7cf,0x7d0,0x7d1,0,0x7fff,0x8000};
        static const uint16_t aspect[]={0x1f3f,0x1f40,0x1f41,0x7cf,0x7d0,0x7d1,0,0x7fff,0x8000};
        static const uint32_t dist[]={0x88b7,0x88b8,0x88b9,0x249ef,0x249f0,0x249f1,0,0x80000000u,0x7fffffffu};
        if(mcs_ok(a[1],0x18)){BYTE *s=mcs_host(a[1]);for(unsigned j=0;j<6;++j)put32(s+4*j,mcs_next());
            put16(s+4,mcs_pick16(bear,13,mcs_next()));put16(s+8,mcs_pick16(aspect,9,mcs_next()));
            uint32_t q=mcs_next();put32(s+0xa,q%5?dist[(q>>3)%9]:mcs_next());
            if(pick%3==0)put16(s+2,(uint16_t)(get16(s+2)|0x8000));}
        if(perturb&&pick%8==3)put32(o+0x1a4,mcs_actor_pick(pick>>8));
        break; }
    case 2: /* point_offset: the point by content, then moved (x, y). */
        mcs_dwords(v,0,a[0],3);
        if(mcs_ok(a[0],12)&&pick%2){put32(mcs_host(a[0]),get32(mcs_host(a[0]))+(pick>>20)-2048);put32(mcs_host(a[0])+4,get32(mcs_host(a[0])+4)+mcs_next()%4096-2048);}
        break;
    case 3: /* heading_point: both points by content. */
        mcs_dwords(v,0,a[0],3);mcs_dwords(v,3,a[1],3);
        break;
    case 4: /* drive_apply: the record by content; it writes +4 and +0Eh, and
             * the perturbation the aspect word and the distance (reread). */
        mcs_dwords(v,0,a[1],6);
        if(mcs_ok(a[1],0x18)){BYTE *s=mcs_host(a[1]);put16(s+4,(uint16_t)mcs_next());put16(s+0xe,(uint16_t)mcs_next());
            if(perturb&&pick%4==0){put16(s+6,(uint16_t)mcs_next());put32(s+0xa,(pick>>5)%2?0x249f1u:mcs_next());}}
        break;
    case 5: /* aim_select: the target may change (reread by the out tests). */
        if(perturb&&pick%6==0)put32(o+0x1a4,mcs_actor_pick(pick>>8));
        if(perturb&&pick%9==0&&mcs_ok(a[0],4)&&mcs_in(a[0],0x340))put32(mcs_host(a[0])+0x23e,(pick>>12)%2?4u:0u);
        break;
    case 6: /* engage_done: zero, nonzero low word or only the high word set. */
        eax=pick%3==0?0:pick%3==1?(eax|1u)&0xffffu:(eax|0x10000u)&0xffff0000u;
        break;
    case 8: /* flank_step: the record by content, then some of it rewritten. */
        mcs_dwords(v,0,a[1],6);
        if(mcs_ok(a[1],0x18)&&pick%2){BYTE *s=mcs_host(a[1]);put16(s+6,(uint16_t)mcs_next());put32(s+0x10,mcs_next());put16(s+0xe,(uint16_t)mcs_next());}
        break;
    case 9: /* order_target: AX zero half of the time. */
        if(pick%2)eax&=0xffff0000u;else if(!(uint16_t)eax)eax|=1u;
        break;
    case 11: /* script_start: changes the state record and the block B (the
              * caller reads [A+4Dh] before and writes B+0 after). */
        if(perturb&&mcs_in(a[0],8)){put32(mcs_host(a[0]),mcs_next());put32(mcs_host(a[0])+0xd,mcs_next());}
        break;
    case 14: case 15: /* aim_target / aim_fire: the point by content (aim_fire);
                       * a replaced aim_fire's stack depth in a quarter of the
                       * real cases. */
        if(id==15)mcs_dwords(v,0,a[1],3);
        if(mcs_deep)mcs_deep_fill();
        break;
    case 17: eax=(eax&0xffff0000u)|(uint16_t)(pick%2?pick>>16:0x4000u); break;
    case 18: /* +30h: writes both 6-byte records. */
        for(unsigned k=1;k<3;++k)if(mcs_ok(a[k],6))for(unsigned j=0;j<3;++j)put16(mcs_host(a[k])+2*j,(uint16_t)(mcs_next()%4001u-2000u));
        break;
    case 19: eax=(eax&0xffff0000u)|(pick>>4&0x1f); break;
    case 20: /* +58h: the part position (12 bytes). */
        if(mcs_ok(a[2],12))for(unsigned j=0;j<3;++j)put32(mcs_host(a[2])+4*j,mcs_next());
        break;
    default:break;
    }
    v->eax=mcs_mask(eax);v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define MCS_STUB(n) __attribute__((naked)) static void mcs_stub_##n(void){__asm__ volatile( \
    "movl %ebx,_mcs_entry\n\tmovl %esi,_mcs_entry+4\n\tmovl %edi,_mcs_entry+8\n\tmovl %ebp,_mcs_entry+12\n\tmovl %esp,_mcs_entry+16\n\t" \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _mcs_stub_impl\n\taddl $24,%esp\n\tmovl _mcs_ecx,%ecx\n\tret\n\t");}
MCS_STUB(0) MCS_STUB(1) MCS_STUB(2) MCS_STUB(3) MCS_STUB(4) MCS_STUB(5) MCS_STUB(6) MCS_STUB(7) MCS_STUB(8) MCS_STUB(9)
MCS_STUB(10) MCS_STUB(11) MCS_STUB(12) MCS_STUB(13) MCS_STUB(14) MCS_STUB(15) MCS_STUB(16) MCS_STUB(17) MCS_STUB(18)
MCS_STUB(19) MCS_STUB(20)
#undef MCS_STUB
static void *mcs_stubs[MCS_IDS]={mcs_stub_0,mcs_stub_1,mcs_stub_2,mcs_stub_3,mcs_stub_4,mcs_stub_5,mcs_stub_6,mcs_stub_7,
    mcs_stub_8,mcs_stub_9,mcs_stub_10,mcs_stub_11,mcs_stub_12,mcs_stub_13,mcs_stub_14,mcs_stub_15,mcs_stub_16,mcs_stub_17,
    mcs_stub_18,mcs_stub_19,mcs_stub_20};
#define MCS_BIT(i) (1u<<(i))
/* Direct callees of each hook (bit i = mcs_addresses[i]). */
static uint32_t mcs_which(unsigned index){
    if(index==ES2_HOOK_INDEX_skirt_check)return MCS_BIT(11)|MCS_BIT(12);
    if(index==ES2_HOOK_INDEX_attack_step)return MCS_BIT(0)|MCS_BIT(1)|MCS_BIT(2)|MCS_BIT(3)|MCS_BIT(4)|MCS_BIT(5)|MCS_BIT(6)|MCS_BIT(7);
    if(index==ES2_HOOK_INDEX_base_attack)return MCS_BIT(0)|MCS_BIT(1)|MCS_BIT(8)|MCS_BIT(4)|MCS_BIT(5)|MCS_BIT(9)|MCS_BIT(7);
    if(index==ES2_HOOK_INDEX_flyer_attack)return MCS_BIT(1)|MCS_BIT(4)|MCS_BIT(5)|MCS_BIT(7);
    if(index==ES2_HOOK_INDEX_face_off)return MCS_BIT(0)|MCS_BIT(1)|MCS_BIT(4)|MCS_BIT(5)|MCS_BIT(6)|MCS_BIT(7);
    if(index==ES2_HOOK_INDEX_flank_attack)return MCS_BIT(0)|MCS_BIT(1)|MCS_BIT(8)|MCS_BIT(4)|MCS_BIT(5)|MCS_BIT(6)|MCS_BIT(7);
    if(index==ES2_HOOK_INDEX_drive_off)return MCS_BIT(0)|MCS_BIT(10);
    return 0;
}
static int mcs_patch(BYTE saved[MCS_IDS][5],int active[MCS_IDS],uint32_t which,int on){
    for(unsigned i=0;i<MCS_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!mcs_addresses[i])continue;if(actor_patch(mcs_addresses[i]-0x400000u,mcs_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(mcs_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
static void mcs_reset(void){mcs_calls=0;mcs_stream=mcs_seed;}
static uint16_t mcs_word(const uint16_t *values,unsigned count){return random32()%4?values[random32()%count]:(uint16_t)random32();}
static BYTE mcs_flag(void){return (BYTE)(random32()%4?0:random32()%2?1:random32());}
/* The arena: six actors (every one with a target, a group, a state record
 * and a configuration), two groups, four configurations, four state
 * records and the vtable. real: positions in a small square, so that the
 * real heading and distance helpers see plausible geometry. */
static void mcs_build(unsigned index,uint32_t a[6],uint32_t r[3],int real){
    const uint32_t b=MCS_BASE,A=MCS_ACTOR(0);
    mcs_seed=random32();random_bytes(mcs_arena,MCS_SIZE);memset(a,0,24);mcs_real=real;mcs_deep=real&&random32()%4==0;
    for(unsigned i=0;i<3;++i)r[i]=random32();
    static const unsigned slots[]={0x3c,0x30,0x54,0x58};
    for(unsigned k=0;k<4;++k)put32(mcs_arena+MCS_VT+slots[k],(uint32_t)(uintptr_t)mcs_stubs[17+k]);
    for(unsigned i=0;i<MCS_ACTORS;++i){
        BYTE *p=mcs_host(MCS_ACTOR(i));
        put32(p,b+MCS_VT);put32(p+0x45,MCS_GROUP(random32()%2));put32(p+0x4d,b+MCS_TBL+0x40u*(random32()%4));
        if(real)for(unsigned j=0;j<3;++j){put32(p+0x26+4*j,(uint32_t)((int32_t)(random32()%40001u)-20000));
            put32(p+0x31eu+4*j,(uint32_t)((int32_t)(random32()%40001u)-20000));}
        static const unsigned bytes[]={0x99,0xa4,0xa5};
        for(unsigned k=0;k<3;++k)p[bytes[k]]=mcs_flag();
        p[0xad]=random32()%4==0?(random32()%2?1:(BYTE)(random32()|1)):0;
        static const uint16_t counts[]={0,1,2,3};put16(p+0x1a2,mcs_word(counts,4));
        put32(p+0x1a4,MCS_ACTOR(random32()%MCS_ACTORS));
        static const uint16_t kinds[]={0,1,2,3,0,1};put16(p+0x1a8,mcs_word(kinds,6));
        put32(p+0x1f2,b+MCS_CFG+0x40u*(random32()%4));
        static const uint16_t states[]={0,4,4,1,2,3,4,5};put16(p+0x23e,mcs_word(states,8));
        put32(p+0x248,random32()%3==0?0:random32()%2?get32(p+0x1a4):MCS_ACTOR(random32()%MCS_ACTORS));
        static const uint16_t classes[]={0,1,2,3,0xffff};put16(p+0x250,mcs_word(classes,5));
        if(real)put32(p+0x288,random32()%200);
    }
    for(unsigned g=0;g<2;++g)mcs_host(MCS_GROUP(g))[0x12]=mcs_flag();
    static const uint16_t cfgs[]={0,0,1,0x8000};
    for(unsigned k=0;k<4;++k)put16(mcs_arena+MCS_CFG+0x40*k+0x2e,mcs_word(cfgs,4));
    a[0]=A;
    (void)index;
}
static int mcs_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const mcs_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,mcs_calls);
    for(unsigned j=0;j<calls&&j<mcs_calls&&expected;++j)if(memcmp(expected+j,mcs_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(mcs_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<MCS_SIZE;++j)if(mcs_after[j]!=mcs_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,mcs_after[j],mcs_arena[j]);break;}
    return 1;
}
static unsigned mcs_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<mcs_calls;++j){unsigned id=mcs_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?mcs_slots[id]==c->key:!mcs_slots[id]&&mcs_addresses[id]==c->key)++hits;}
    return hits;
}
static int mcs_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[MCS_GLOBALS],after[MCS_GLOBALS],actual[MCS_GLOBALS];static mcs_record expected[MCS_MAX];
    memcpy(mcs_before,mcs_arena,MCS_SIZE);mcs_globals(before,0);
    mcs_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=mcs_calls;memcpy(expected,mcs_log,calls*sizeof(*expected));
    memcpy(mcs_after,mcs_arena,MCS_SIZE);mcs_globals(after,0);
    memcpy(mcs_arena,mcs_before,MCS_SIZE);mcs_globals(before,1);mcs_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];
    if(count>16)return fail("mech combat counter table too long");
    for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);mcs_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=mcs_expected_hits(c+i))return fail("mech combat callee counter delta");
    if(pair[0]!=pair[1]||calls!=mcs_calls||memcmp(expected,mcs_log,calls*sizeof(*expected))||memcmp(mcs_after,mcs_arena,MCS_SIZE)||
       memcmp(after,actual,MCS_GLOBALS))
        return mcs_report("direct",n,pair,calls,expected);
    mcs_globals(before,1);
    return 0;
}
/* Calls target with four slots, EAX/EDX/ECX/EBX = pattern, ESI and EDI two
 * other values derived from it and a patterned stack below the arguments;
 * returns EAX, EDX and ECX in out and copies the 64 bytes below the slots
 * after the call. */
__attribute__((naked,noinline)) static void mcs_invoke(void *target,const uint32_t *a,uint32_t pattern,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 16(%ebp),%eax\n\tmovl $1024,%ecx\n\trep stosl\n\taddl $4080,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl %esp,_mcs_origin\n\t"
        "movl 16(%ebp),%eax\n\tmovl %eax,%edx\n\tmovl %eax,%ecx\n\tmovl %eax,%ebx\n\tmovl %eax,%esi\n\txorl $0x5a5a5a5a,%esi\n\t"
        "movl %eax,%edi\n\txorl $0x3c3c3c3c,%edi\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%ebx\n\tmovl %eax,(%ebx)\n\tmovl %edx,4(%ebx)\n\tmovl %ecx,8(%ebx)\n\t"
        "leal -64(%esp),%esi\n\tmovl $_mcs_frame_copy,%edi\n\tmovl $16,%ecx\n\tcld\n\trep movsl\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static int mcs_shimmed(unsigned index){
    return index==ES2_HOOK_INDEX_attack_step||index==ES2_HOOK_INDEX_base_attack||index==ES2_HOOK_INDEX_flyer_attack||
        index==ES2_HOOK_INDEX_face_off||index==ES2_HOOK_INDEX_flank_attack||index==ES2_HOOK_INDEX_drive_off;
}
static int mcs_frame_report(const char *what,unsigned n,const BYTE expected[64]){
    fprintf(stderr,"FAIL: %s case %u (%s)\n",current_function,n,what);
    for(unsigned j=0;j<64;++j)if(expected[j]!=mcs_frame_copy[j])fprintf(stderr," below the arguments -%u: %02x/%02x\n",64-j,expected[j],mcs_frame_copy[j]);
    return 1;
}
static int mcs_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;mcs_track=mcs_shimmed(index);
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_out[3],actual_out[3],pattern=poison?0xa7a7a7a7u:0x31313131u;mcs_build(index,a,r,0);BYTE expected_frame[64];
        BYTE before[MCS_GLOBALS],after[MCS_GLOBALS],actual[MCS_GLOBALS],expected_fpu[108],actual_fpu[108];static mcs_record expected[MCS_MAX];
        memcpy(mcs_before,mcs_arena,MCS_SIZE);mcs_globals(before,0);
        mcs_reset();mcs_invoke(mapped+d->rva,a,pattern,expected_out);es2_hook_fpu_save(expected_fpu);memcpy(expected_frame,mcs_frame_copy,64);
        unsigned calls=mcs_calls;memcpy(expected,mcs_log,calls*sizeof(*expected));memcpy(mcs_after,mcs_arena,MCS_SIZE);mcs_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("mech combat poison install");
        memcpy(mcs_arena,mcs_before,MCS_SIZE);mcs_globals(before,1);mcs_reset();mcs_invoke(mapped+d->rva,a,pattern,actual_out);
        es2_hook_fpu_save(actual_fpu);mcs_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_out[1]<<32|expected_out[0],(uint64_t)actual_out[1]<<32|actual_out[0]};
        if(pair[0]!=pair[1]||calls!=mcs_calls||memcmp(expected,mcs_log,calls*sizeof(*expected))||memcmp(mcs_after,mcs_arena,MCS_SIZE)||
           memcmp(after,actual,MCS_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=mcs_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        else if(mcs_track&&memcmp(expected_frame,mcs_frame_copy,64))status=mcs_frame_report(fast?"installed fast frame":"installed preserving frame",n,expected_frame);
        if(!es2_hook_uninstall(&h))return fail("mech combat poison uninstall");mcs_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));mcs_track=0;
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state%s\n",d->name,total,
        mcs_shimmed(index)?", EBX/ESI/EDI/EBP/ESP and stack arguments at every callee entry and the frame below the arguments":"");
    return status;
}
/* The real-callee pass: nested hooks in rotating original/replaced/compare
 * modes (compare falls back to replaced where refused), the hook under test
 * original or replaced; keep = the callees that stay logging stubs. */
enum { MCS_NESTED=16 };
static unsigned mcs_nested(unsigned index,unsigned nested[MCS_NESTED],uint32_t *keep){
    unsigned count=0;*keep=MCS_BIT(9)|MCS_BIT(13)|MCS_BIT(14)|MCS_BIT(15)|MCS_BIT(16);
#define MCS_N(name) nested[count++]=ES2_HOOK_INDEX_##name
    if(index==ES2_HOOK_INDEX_skirt_check){MCS_N(script_start);MCS_N(dist_gate);*keep=0;return count;}
    if(index==ES2_HOOK_INDEX_drive_off)MCS_N(face_off);
    if(index!=ES2_HOOK_INDEX_flyer_attack){MCS_N(skirt_check);MCS_N(script_start);}
    MCS_N(dist_gate);MCS_N(target_bearing);MCS_N(heading_point);MCS_N(drive_apply);MCS_N(aim_select);MCS_N(engage_end);
    if(index==ES2_HOOK_INDEX_base_attack||index==ES2_HOOK_INDEX_flank_attack){MCS_N(flank_step);MCS_N(timer_word_step);}
    if(index==ES2_HOOK_INDEX_attack_step||index==ES2_HOOK_INDEX_base_attack||index==ES2_HOOK_INDEX_flank_attack)MCS_N(point_offset);
    if(index!=ES2_HOOK_INDEX_base_attack&&index!=ES2_HOOK_INDEX_flyer_attack)MCS_N(engage_done);
#undef MCS_N
    return count;
}
static int mcs_real_pass(unsigned index){
    unsigned nested[MCS_NESTED];uint32_t keep;unsigned count=mcs_nested(index,nested,&keep);
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0,deep=0;uint64_t entered[MCS_NESTED]={0};
    BYTE saved[MCS_IDS][5];int active[MCS_IDS];
    if(mcs_patch(saved,active,keep,1))return 1;
    static mcs_record expected[MCS_MAX];
    mcs_track=mcs_shimmed(index);
    /* In the 18h-byte frames the lowest dword of the window is the PUSHFD
     * slot of a one-slot callee's framework entry when that callee is a
     * replaced hook: the bridge's flags there, not those of the
     * instructions before the original's CALL (accepted; ai.md, "MECH
     * combat states"). The installed probes, with stubs, compare
     * it. */
    const unsigned skip=mcs_track&&index!=ES2_HOOK_INDEX_attack_step&&index!=ES2_HOOK_INDEX_drive_off?4u:0u;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_out[3],actual_out[3],pattern=n&1?0xa7a7a7a7u:0x31313131u;mcs_build(index,a,r,1);BYTE expected_frame[64];
        BYTE before[MCS_GLOBALS],after[MCS_GLOBALS],actual[MCS_GLOBALS];deep+=mcs_deep?1u:0u;
        memcpy(mcs_before,mcs_arena,MCS_SIZE);mcs_globals(before,0);
        es2_hook hk[MCS_NESTED+1];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("mech combat nested install");}
        mcs_reset();mcs_invoke(mapped+d->rva,a,pattern,expected_out);memcpy(expected_frame,mcs_frame_copy,64);
        unsigned calls=mcs_calls;memcpy(expected,mcs_log,calls*sizeof(*expected));
        memcpy(mcs_after,mcs_arena,MCS_SIZE);mcs_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+MCS_NESTED,d,&o,function_hook_extension(index)))return fail("mech combat caller install");
        memcpy(mcs_arena,mcs_before,MCS_SIZE);mcs_globals(before,1);mcs_reset();
        mcs_invoke(mapped+d->rva,a,pattern,actual_out);mcs_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_out[1]<<32|expected_out[0],(uint64_t)actual_out[1]<<32|actual_out[0]};
        if(pair[0]!=pair[1]||calls!=mcs_calls||memcmp(expected,mcs_log,calls*sizeof(*expected))||memcmp(mcs_after,mcs_arena,MCS_SIZE)||
           memcmp(after,actual,MCS_GLOBALS)){
            fprintf(stderr,"real mode=%u\n",mode);status=mcs_report("real",n,pair,calls,expected);
        }
        else if(mcs_track&&memcmp(expected_frame+skip,mcs_frame_copy+skip,64-skip)){fprintf(stderr,"real mode=%u\n",mode);status=mcs_frame_report("real frame",n,expected_frame);}
        for(unsigned j=0;j<=MCS_NESTED;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("mech combat nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("mech combat nested uninstall");}
        mcs_globals(before,1);++total;
    }
    if(mcs_patch(saved,active,0,0))return 1;
    mcs_real=0;mcs_deep=0;mcs_track=0;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases (%u with a modelled aim_fire depth), %u nested hooks in rotating modes, 31/a7 stack, pair, memory, globals and stub inputs%s\n",
        d->name,total,deep,count,mcs_shimmed(index)?", per-call EBX/ESI/EDI/EBP/ESP, stack arguments and the frame below the arguments":"");
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
/* Compare cases (compare_probes.h) for engage_end, the group's compare-capable
 * member; the state is the arena and the globals. */
static void mcs_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){mcs_build(index,a,r,0);(void)n;}
static void mcs_compare_state(BYTE *s,int restore){
    if(restore){memcpy(mcs_arena,s,MCS_SIZE);mcs_globals(s+MCS_SIZE,1);}else{memcpy(s,mcs_arena,MCS_SIZE);mcs_globals(s+MCS_SIZE,0);}
}
static int mech_combat_cases(unsigned index,unsigned limit){
    if(limit&&compare_probe_installed(index))return compare_probe_run(index,limit,mcs_compare_build,mcs_compare_state,MCS_SIZE+MCS_GLOBALS);
    uint32_t which=mcs_which(index);
    BYTE saved[MCS_IDS][5],old_globals[MCS_GLOBALS];int active[MCS_IDS];
    mcs_globals(old_globals,0);if(mcs_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_MECH_COMBAT_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];mcs_build(index,a,r,0);status=mcs_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=mcs_installed_probes(index,64);
    if(mcs_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status&&index!=ES2_HOOK_INDEX_engage_end)status=mcs_real_pass(index);
    mcs_globals(old_globals,1);
    if(!limit&&!status&&compare_probe_capable(index))status=compare_probe_run(index,0,mcs_compare_build,mcs_compare_state,MCS_SIZE+MCS_GLOBALS);
    mcs_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, frame records by content, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
