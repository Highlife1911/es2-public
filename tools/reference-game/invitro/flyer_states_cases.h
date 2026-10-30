/* FLYER AI state methods: flyer_attacking 422ca8, flyer_follow 422598,
 * flyer_formation 422a50, flyer_search 422a80, flyer_patrol 422b34, flyer_scout
 * 422bdc, flyer_decision 422d00, flyer_engage 422bf0
 * (docs/reverse-engineering/flyer.md, "the FLYER AI state methods"). Direct
 * cases run the original body from an own caller (31/a7-patterned stack,
 * distinct residue bytes planted at the shim's entry offsets) and the core
 * against logging stubs for the game callees of the hook under test: ordered
 * argument slots, entry EAX/EDX/ECX (stack addresses masked), the point P by
 * content, and the residue bytes each residue-reading stub (target_emit,
 * flyer_follow, flyer_steer, 4224c4, flyer_formation) finds at its offsets.
 * Outside the branch-trap window the original's stubs must see exactly the
 * planted bytes (this checks the depth table of the contract); the replacement
 * gets them through its residue record and must plant the same. Stubs return
 * planned values on the branch edges and change what the originals reread.
 * flyer_engage's recursion runs the original in both runs. Then installed 31/a7
 * probes through the entry shims with planted residue, and a real-callee pass:
 * the group's own hooks and point_offset, dist_gate, heading_point,
 * dist2d_point, timer_word_step, order_target and script_start in rotating
 * modes with the real 4224c4, 422108 and 422260; target_emit, flyer_steer,
 * target_select, the route helpers 423b0c/42313c and the virtual +78h stay
 * stubs. */
typedef struct fx_record { uint32_t id,args[5],regs[3],input[3],residue[2],eax,edx; uint32_t count; } fx_record;
enum { FX_SIZE=0x1800, FX_ACT=0x000, FX_STRIDE=0x300, FX_ACTORS=6, FX_GRP=0x1200, FX_MEM=0x1300, FX_ORD=0x1340,
    FX_VT=0x1400, FX_PT=0x1480, FX_MAX=64, FX_IDS=18 };
#define FX_BASE ((uint32_t)(uintptr_t)fx_arena)
#define FX_ACTOR(i) (FX_BASE+FX_ACT+FX_STRIDE*(uint32_t)(i))
#define FX_GROUP(j) (FX_BASE+FX_GRP+0x80u*(uint32_t)(j))
#define FX_MEMBERS(j) (FX_BASE+FX_MEM+0x20u*(uint32_t)(j))
#define FX_ORDER(k) (FX_BASE+FX_ORD+0x10u*(uint32_t)(k))
static BYTE fx_arena[FX_SIZE],fx_before[FX_SIZE],fx_after[FX_SIZE];
static fx_record fx_log[FX_MAX];
static unsigned fx_calls,fx_hook,fx_route_calls,fx_limit;
static uint32_t fx_seed,fx_stream,fx_actor;
__attribute__((used)) static uint32_t fx_ecx,fx_tmp;
__attribute__((used)) static BYTE fx_res[8];
/* 0 target_emit 4226a0, 1 flyer_follow 422598, 2 point_offset 4929fc,
 * 3 virtual +78h (arena vtable, never patched), 4 dist_gate 49288c,
 * 5 heading_point 492934, 6 422108, 7 flyer_steer 4222fc, 8 422260,
 * 9 4224c4, 10 flyer_formation 422a50, 11 timer_word_step 467a48,
 * 12 target_select 411fa0, 13 order_target 423918, 14 flyer_engage
 * 422bf0, 15 script_start 413e50, 16 423b0c, 17 42313c (the last two only
 * under the real 4224c4). */
static const uint32_t fx_addresses[FX_IDS]={0x4226a0,0x422598,0x4929fc,0,0x49288c,0x492934,0x422108,0x4222fc,0x422260,
    0x4224c4,0x422a50,0x467a48,0x411fa0,0x423918,0x422bf0,0x413e50,0x423b0c,0x42313c};
static const unsigned fx_counts[FX_IDS]={1,1,3,2,2,2,2,4,5,1,1,1,3,2,1,2,2,1};
/* Residue bytes each stub reads below its entry ESP (flyer_states_hooks.h). */
static const unsigned fx_residue_count[FX_IDS]={8,4,0,0,0,0,0,4,0,4,8,0,0,0,0,0,0,0};
static const uint32_t fx_residue_offsets[FX_IDS][8]={
    {0x14,0x13,0x12,0x11,0x289,0x29d,0x26d,0x281},{0x261,0x275,0x245,0x259},{0},{0},{0},{0},{0},
    {0x229,0x23d,0x20d,0x221},{0},{0x259,0x26d,0x23d,0x251},{0x269,0x27d,0x24d,0x261,0x271,0x285,0x255,0x269}};
static uint32_t fx_next(void){
    uint32_t z=fx_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static int fx_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static BYTE *fx_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int fx_in(uint32_t a,uint32_t n){return a-FX_BASE<FX_SIZE&&FX_SIZE-(a-FX_BASE)>=n;}
static int fx_ok(uint32_t p,uint32_t n){
    const NT_TIB *t=(const NT_TIB *)NtCurrentTeb();uint32_t low=(uint32_t)(uintptr_t)t->StackLimit,top=(uint32_t)(uintptr_t)t->StackBase;
    return fx_in(p,n)||(p>=low&&p<top&&top-p>=n);
}
/* The leader of A's group (0 if the pointers leave the arena). */
static uint32_t fx_leader(uint32_t A){
    uint32_t g=get32(fx_host(A)+0x45);if(!fx_in(g,0x80))return 0;
    uint32_t m=get32(fx_host(g)+0xc);return fx_in(m,4)?get32(fx_host(m)):0;
}
__attribute__((used,noinline)) static uint64_t __cdecl fx_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(fx_calls==FX_MAX){fail("flyer states call overflow");ExitProcess(1);}
    fx_record *v=fx_log+fx_calls++;memset(v,0,sizeof(*v));v->id=id;
    v->count=weapons_bridge_slots(a,fx_counts[id]);
    for(unsigned k=0;k<fx_counts[id];++k)v->args[k]=fx_stack(a[k])?0:a[k];
    for(unsigned j=0;j<3;++j)v->regs[j]=fx_stack(regs[j])?0:regs[j];
    if(fx_residue_count[id]&&!coverage_active)memcpy(v->residue,fx_res,fx_residue_count[id]);
    uint32_t eax=fx_next(),edx=fx_next(),pick=fx_next();fx_ecx=fx_next();
    const uint32_t A=fx_actor;BYTE *o=fx_host(A);
    switch(id){
    case 2: /* point_offset: P by content, moved in place half of the time. */
        if(fx_ok(a[0],12))for(unsigned j=0;j<3;++j)v->input[j]=get32(fx_host(a[0])+4*j);
        if(pick%2&&fx_ok(a[0],12))for(unsigned j=0;j<3;++j)put32(fx_host(a[0])+4*j,fx_next());
        break;
    case 3: /* virtual +78h: the formation point written into P. */
        if(fx_ok(a[1],12))for(unsigned j=0;j<3;++j)v->input[j]=get32(fx_host(a[1])+4*j);
        if(pick%4&&fx_ok(a[1],12))for(unsigned j=0;j<3;++j)put32(fx_host(a[1])+4*j,fx_next());
        break;
    case 4: { /* dist_gate: the 10000/15000 edges. */
        if(fx_ok(a[0],12))for(unsigned j=0;j<3;++j)v->input[j]=get32(fx_host(a[0])+4*j);
        static const uint32_t ds[]={9999,10000,10001,14999,15000,15001,0,0xffffffffu,0x80000000u,0x7fffffffu};
        if(pick%5)eax=ds[(pick>>4)%10];
        break; }
    case 5: { /* heading_point: h - A.h on the quarter-turn edges; A.h, L.h
               * and the group's first member are reread or cached. */
        if(fx_ok(a[0],12))for(unsigned j=0;j<3;++j)v->input[j]=get32(fx_host(a[0])+4*j);
        static const uint16_t deltas[]={0x3fff,0x4000,0xbfff,0xc000,0x8000,0,0x7fff,0xc001};
        if(pick%4)eax=(eax&0xffff0000u)|(uint16_t)(get16(o+0x10)+deltas[(pick>>4)%8]);
        const uint32_t L=fx_leader(A);
        if(pick%7==0)put16(o+0x10,(uint16_t)fx_next());
        if(pick%7==1&&fx_in(L,FX_STRIDE))put16(fx_host(L)+0x10,(uint16_t)fx_next());
        if(pick%7==2){uint32_t g=get32(o+0x45);if(fx_in(g,0x80)&&fx_in(get32(fx_host(g)+0xc),4))put32(fx_host(get32(fx_host(g)+0xc)),FX_ACTOR((pick>>8)%FX_ACTORS));}
        break; }
    case 10: { /* flyer_formation: may switch A's group or its leader. */
        uint32_t g=get32(o+0x45);
        if(pick%5==0)put32(o+0x45,g==FX_GROUP(0)?FX_GROUP(1):FX_GROUP(0));
        if(pick%5==1&&fx_in(g,0x80)&&fx_in(get32(fx_host(g)+0xc),4))put32(fx_host(get32(fx_host(g)+0xc)),pick>>8&1?A:FX_ACTOR((pick>>9)%FX_ACTORS));
        break; }
    case 11: /* timer_word_step: AX zero half of the time. */
        if(pick%2)eax&=0xffff0000u;else if(!(uint16_t)eax)eax|=1u;
        break;
    case 12: { /* target_select: 0, the current target or another actor; the
                * target pointer may change behind the caller's back. */
        const uint32_t T=get32(o+0x1a4);
        eax=pick%4==0?0:pick%4==1?T:FX_ACTOR((pick>>4)%FX_ACTORS);
        if(pick%4==3){ /* the actor A's current order names (order_target's yes) */
            const uint32_t g=get32(o+0x45);
            if(fx_in(g,0x80)){const uint32_t q=get32(fx_host(g)+0x44+4*(get32(fx_host(g)+0x6c)%10u));
                if(fx_in(q,0x12)&&fx_in(get32(fx_host(q)+0xe),FX_STRIDE))eax=get32(fx_host(q)+0xe);}
        }
        if(pick%6==5)put32(o+0x1a4,pick>>8&1?0:FX_ACTOR((pick>>9)%FX_ACTORS));
        break; }
    case 13: /* order_target: AX zero a third of the time; the target is reread. */
        if(pick%3==0)eax&=0xffff0000u;else if(!(uint16_t)eax)eax|=1u;
        if(pick%5==4)put32(o+0x1a4,pick>>8&1?0:FX_ACTOR((pick>>9)%FX_ACTORS));
        break;
    case 15: /* script_start: under flyer_engage, the loop rereads A's
              * target, the count and the members' flag/target (no change
              * of +45h or of the member order: no recursion cycle). */
        if(fx_hook==ES2_HOOK_INDEX_flyer_engage){
            const uint32_t m=FX_ACTOR((pick>>8)%FX_ACTORS);
            if(pick%6==0)put32(o+0x1a4,pick>>12&1?0:FX_ACTOR((pick>>13)%FX_ACTORS));
            if(pick%6==1)fx_host(m)[0x99]=(BYTE)(pick>>12&1);
            if(pick%6==2)put32(fx_host(m)+0x1a4,pick>>12&1?0:FX_ACTOR((pick>>13)%FX_ACTORS));
            if(pick%6==3)put16(fx_host(FX_GROUP(0))+0x10,(uint16_t)((pick>>12)%9));
        }
        break;
    case 16: /* 423b0c: a route point; 0 only on the first call (the second
              * result is dereferenced through heading_point). */
        eax=fx_route_calls++==0&&pick%3==0?0:FX_BASE+FX_PT+12u*((pick>>4)%4u);
        break;
    default:break;
    }
    v->eax=fx_stack(eax)?0:eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define FX_BODY(n) "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _fx_stub_impl\n\taddl $24,%esp\n\tmovl _fx_ecx,%ecx\n\tret\n\t"
#define FX_RB(i,o) "movb -" #o "(%esp),%al\n\tmovb %al,_fx_res+" #i "\n\t"
#define FX_STUB(n) __attribute__((naked)) static void fx_stub_##n(void){__asm__ volatile(FX_BODY(n));}
#define FX_STUB4(n,a,b,c,d) __attribute__((naked)) static void fx_stub_##n(void){__asm__ volatile( \
    "movl %eax,_fx_tmp\n\t" FX_RB(0,a) FX_RB(1,b) FX_RB(2,c) FX_RB(3,d) "movl _fx_tmp,%eax\n\t" FX_BODY(n));}
#define FX_STUB8(n,a,b,c,d,e,f,g,h) __attribute__((naked)) static void fx_stub_##n(void){__asm__ volatile( \
    "movl %eax,_fx_tmp\n\t" FX_RB(0,a) FX_RB(1,b) FX_RB(2,c) FX_RB(3,d) FX_RB(4,e) FX_RB(5,f) FX_RB(6,g) FX_RB(7,h) \
    "movl _fx_tmp,%eax\n\t" FX_BODY(n));}
FX_STUB8(0,0x14,0x13,0x12,0x11,0x289,0x29d,0x26d,0x281) FX_STUB4(1,0x261,0x275,0x245,0x259)
FX_STUB(2) FX_STUB(3) FX_STUB(4) FX_STUB(5) FX_STUB(6) FX_STUB4(7,0x229,0x23d,0x20d,0x221) FX_STUB(8)
FX_STUB4(9,0x259,0x26d,0x23d,0x251) FX_STUB8(10,0x269,0x27d,0x24d,0x261,0x271,0x285,0x255,0x269)
FX_STUB(11) FX_STUB(12) FX_STUB(13) FX_STUB(14) FX_STUB(15) FX_STUB(16) FX_STUB(17)
#undef FX_STUB
#undef FX_STUB4
#undef FX_STUB8
static void *fx_stubs[FX_IDS]={fx_stub_0,fx_stub_1,fx_stub_2,fx_stub_3,fx_stub_4,fx_stub_5,fx_stub_6,fx_stub_7,fx_stub_8,
    fx_stub_9,fx_stub_10,fx_stub_11,fx_stub_12,fx_stub_13,fx_stub_14,fx_stub_15,fx_stub_16,fx_stub_17};
#define FX_BIT(i) (1u<<(i))
/* Direct callees of each hook (patched; the virtual +78h goes through the
 * arena vtable). */
static unsigned fx_which(unsigned index){
    switch(index){
    case ES2_HOOK_INDEX_flyer_attacking:return FX_BIT(0)|FX_BIT(1);
    case ES2_HOOK_INDEX_flyer_follow:return FX_BIT(2)|FX_BIT(4)|FX_BIT(5)|FX_BIT(6)|FX_BIT(7)|FX_BIT(8);
    case ES2_HOOK_INDEX_flyer_formation:return FX_BIT(9)|FX_BIT(1);
    case ES2_HOOK_INDEX_flyer_search:return FX_BIT(10)|FX_BIT(11)|FX_BIT(12)|FX_BIT(13)|FX_BIT(14);
    case ES2_HOOK_INDEX_flyer_patrol:return FX_BIT(10)|FX_BIT(11)|FX_BIT(12)|FX_BIT(14);
    case ES2_HOOK_INDEX_flyer_scout:return FX_BIT(10);
    default:return FX_BIT(15);
    }
}
/* The shim record of a hook (flyer_states_hooks.h), or FS_SHIMS. */
static unsigned fx_shim(unsigned index){
    for(unsigned k=0;k<FS_SHIMS;++k)if(flyer_states_shim_rva[k]==function_hook_descriptor(index)->rva)return k;
    return FS_SHIMS;
}
/* Depth of a residue-reading callee's entry below the hook's entry. */
static uint32_t fx_depth(unsigned index,unsigned id){
    switch(index){
    case ES2_HOOK_INDEX_flyer_attacking:return id==0||id==1?0x14u:0;
    case ES2_HOOK_INDEX_flyer_follow:return id==7?0x38u:0;
    case ES2_HOOK_INDEX_flyer_formation:return id==9||id==1?0x10u:0;
    case ES2_HOOK_INDEX_flyer_search:return id==10?0x14u:0;
    case ES2_HOOK_INDEX_flyer_patrol:return id==10?0x18u:0;
    case ES2_HOOK_INDEX_flyer_scout:return id==10?0x0cu:0;
    default:return 0;
    }
}
/* The planted byte at an offset below the hook's entry (distinct per
 * offset, never a pattern byte). */
static uint32_t fx_plant_seed;
static BYTE fx_byte(uint32_t offset){
    BYTE b=(BYTE)(fx_plant_seed>>(offset%3*8)^offset*0x9du^offset>>3);
    return b==0x31||b==0xa7?(BYTE)(b^0x5a):b;
}
typedef struct fx_plant { uint32_t offset,byte; } fx_plant;
static unsigned fx_plants(unsigned index,fx_plant *p){
    const unsigned k=fx_shim(index);if(k==FS_SHIMS)return 0;
    for(unsigned j=0;j<flyer_states_shim_bytes[k];++j){p[j].offset=flyer_states_capture[k][j];p[j].byte=fx_byte(p[j].offset);}
    return flyer_states_shim_bytes[k];
}
/* Own caller: a patterned stack, one slot, planted residue bytes, entry
 * EAX/EDX/ECX; EAX/EDX come back in registers[0..1]. */
__attribute__((naked)) static void fx_call(void *target,uint32_t arg,uint32_t *registers,const fx_plant *plant,unsigned plants,
    uint32_t pattern){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 28(%ebp),%eax\n\tmovl $1024,%ecx\n\tcld\n\trep stosl\n\taddl $3840,%esp\n\t"
        "pushl 12(%ebp)\n\tmovl 20(%ebp),%esi\n\tmovl 24(%ebp),%ecx\n\ttestl %ecx,%ecx\n\tjz 2f\n\t"
        "1: movl %esp,%eax\n\tsubl (%esi),%eax\n\tmovb 4(%esi),%bl\n\tmovb %bl,-4(%eax)\n\taddl $8,%esi\n\tdecl %ecx\n\tjnz 1b\n\t"
        "2: movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tcall *8(%ebp)\n\t"
        "movl 16(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static void fx_reset(void){fx_calls=0;fx_route_calls=0;fx_stream=fx_seed;}
static uint16_t fx_word(const uint16_t *values,unsigned count){return random32()%4?values[random32()%count]:(uint16_t)random32();}
static uint32_t fx_actor_or_zero(unsigned zero_one_in){return random32()%zero_one_in==0?0:FX_ACTOR(random32()%FX_ACTORS);}
/* Six actors, two groups (G0 with A, G1 led by actor 5 with actors 3 and
 * 4: an engage recursion reaches at most G1's members), ten order slots
 * per group, eight orders, the vtable (slot +78h = stub 3) and the route
 * points. real: small coordinates and words for the real callees. */
static void fx_build(unsigned index,uint32_t a[6],uint32_t r[3],int real){
    const uint32_t b=FX_BASE;fx_seed=random32();fx_plant_seed=random32();random_bytes(fx_arena,FX_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    put32(fx_arena+FX_VT+0x78,(uint32_t)(uintptr_t)fx_stubs[3]);
    const uint32_t A=FX_ACTOR(random32()%2);a[0]=A;fx_actor=A;
    const int engage=index==ES2_HOOK_INDEX_flyer_engage;
    for(unsigned i=0;i<FX_ACTORS;++i){BYTE *p=fx_host(FX_ACTOR(i));
        put32(p,b+FX_VT);
        uint32_t g=FX_GROUP(0);
        if(i==5&&random32()%2)g=FX_GROUP(1);
        if((i==3||i==4)&&random32()%3==0)g=FX_GROUP(1);
        put32(p+0x45,g);
        static const BYTE flags[]={0,0,0,1,0x80};
        p[0x99]=flags[random32()%5];p[0xa4]=flags[random32()%5];
        put16(p+0x1a2,(uint16_t)(random32()%4?1u+random32()%4u:random32()));
        put32(p+0x1a4,fx_actor_or_zero(4));
        if(real){
            for(unsigned j=0;j<3;++j)put32(p+0x26+4*j,random32()%40000u-20000u);
            p[0xae]=(BYTE)(random32()%2);
            put16(p+0x23c,(uint16_t)(random32()%0x400u-0x200u));put16(p+0x25b,(uint16_t)(random32()%0x400u-0x200u));
            put16(p+0x233,(uint16_t)(random32()%0x200u));put16(p+0x21c,(uint16_t)(random32()%0x200u));
            put16(p+0x5b,(uint16_t)(random32()%3?random32()%40u:random32()));
        }
    }
    put32(fx_host(A)+0x45,FX_GROUP(0));
    /* Heading of A and of the leader around the quarter-turn edges. */
    BYTE *o=fx_host(A);put16(o+0x10,(uint16_t)random32());
    uint32_t members=FX_MEMBERS(0);BYTE *ms=fx_host(members);
    for(unsigned j=0;j<8;++j)put32(ms+4*j,random32()%6?FX_ACTOR(random32()%FX_ACTORS):0);
    put32(ms,random32()%2?A:FX_ACTOR(random32()%FX_ACTORS));
    if(engage)for(unsigned j=1;j<8;++j)if(get32(ms+4*j)==A&&random32()%2)put32(ms+4*j,FX_ACTOR(5));
    const uint32_t L=get32(ms);
    static const uint16_t diffs[]={0xc000,0xbfff,0x3fff,0x4000,0,0x8000,0x4001,0xc001};
    if(L!=A)put16(fx_host(L)+0x10,(uint16_t)(get16(o+0x10)-fx_word(diffs,8)));
    for(unsigned j=0;j<2;++j){BYTE *g=fx_host(FX_GROUP(j));
        put32(g+0xc,FX_MEMBERS(j));
        put16(g+0x10,(uint16_t)(j?3:random32()%4?random32()%9:random32()));
        put16(g+4,(uint16_t)random32());
        put32(g+0x6c,random32()%10);
        for(unsigned k=0;k<10;++k)put32(g+0x44+4*k,random32()%4?FX_ORDER(random32()%8):0);
    }
    BYTE *m1=fx_host(FX_MEMBERS(1));put32(m1,FX_ACTOR(5));put32(m1+4,FX_ACTOR(3));put32(m1+8,FX_ACTOR(4));
    for(unsigned j=3;j<8;++j)put32(m1+4*j,0);
    static const uint16_t types[]={0,1,2,3,4,5,6,0xb,0xffff,0x8000};
    for(unsigned k=0;k<8;++k){BYTE *q=fx_host(FX_ORDER(k));put16(q,fx_word(types,10));
        put16(q+0xc,(uint16_t)(random32()%5-1u));put32(q+0xe,random32()%2?fx_actor_or_zero(3):FX_GROUP(random32()%2));}
    /* flyer_attacking dereferences its target. */
    if(index==ES2_HOOK_INDEX_flyer_attacking&&!get32(o+0x1a4))put32(o+0x1a4,FX_ACTOR(random32()%FX_ACTORS));
    if(real)for(unsigned j=0;j<12;++j)put32(fx_arena+FX_PT+4*j,random32()%40000u-20000u);
    /* flyer_decision starts the record in its entry ECX on some order types;
     * under the real script_start it must be readable. The trampoline's value
     * is the member-pointer OR, 422d00 (code); a record is used here because
     * with the hook installed the bytes at 422d04 are the patched entry
     * (flyer.md, "the FLYER AI state methods", quirks). */
    if(real&&index==ES2_HOOK_INDEX_flyer_decision)r[2]=(uint32_t)(uintptr_t)mapped+(random32()%2?0x99d34u:0x99cf8u);
}
static int fx_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const fx_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,fx_calls);
    for(unsigned j=0;j<calls&&j<fx_calls&&expected;++j)if(memcmp(expected+j,fx_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(fx_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<FX_SIZE;++j)if(fx_after[j]!=fx_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,fx_after[j],fx_arena[j]);break;}
    return 1;
}
/* In the original the residue stubs must see the planted bytes. */
static int fx_check_residue(unsigned index,unsigned n){
    if(coverage_active)return 0;
    for(unsigned j=0;j<fx_calls;++j){
        const fx_record *v=fx_log+j;const uint32_t depth=fx_depth(index,v->id);if(!depth)continue;
        const BYTE *got=(const BYTE *)v->residue;
        for(unsigned i=0;i<fx_residue_count[v->id];++i)if(got[i]!=fx_byte(depth+fx_residue_offsets[v->id][i])){
            fprintf(stderr,"FAIL: %s case %u residue at stub %u byte %u %02x, planted %02x\n",current_function,n,v->id,i,got[i],
                fx_byte(depth+fx_residue_offsets[v->id][i]));return 1;}
    }
    return 0;
}
static unsigned fx_expected_hits(unsigned index,const callee_counter *c){
    unsigned hits=0;
    if(index==ES2_HOOK_INDEX_flyer_engage){ /* the recursion runs the original: one script_start per level */
        if(fx_limit)return UINT_MAX; /* integration mode: the recursion enters the installed hook */
        for(unsigned j=0;j<fx_calls;++j)if(fx_log[j].id==15)++hits;
        return c->key==0x413e50?(hits?1u:0u):UINT_MAX; /* nested levels below are not observed */
    }
    for(unsigned j=0;j<fx_calls;++j)
        if(c->event==ES2_COLLIDE_SLOT?fx_log[j].id==3:fx_addresses[fx_log[j].id]==c->key)++hits;
    return hits;
}
/* The record the shim would leave for a direct call of the adapter. */
static void fx_pending(unsigned index,const uint32_t *a){
    const unsigned k=fx_shim(index);if(k==FS_SHIMS)return;
    flyer_states_pending[k].esp=(uint32_t)(uintptr_t)a-4u;
    for(unsigned j=0;j<flyer_states_shim_bytes[k];++j)flyer_states_pending[k].bytes[j]=fx_byte(flyer_states_capture[k][j]);
}
static int fx_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    static fx_record expected[FX_MAX];fx_plant p[12];const unsigned plants=fx_plants(index,p);
    memcpy(fx_before,fx_arena,FX_SIZE);
    uint32_t regs[3]={r[0],r[1],r[2]};
    fx_reset();fx_call(mapped+d->rva,a[0],regs,p,plants,random32());
    uint64_t pair[2];pair[0]=(uint64_t)regs[1]<<32|regs[0];unsigned calls=fx_calls;memcpy(expected,fx_log,calls*sizeof(*expected));
    if(fx_check_residue(index,n))return 1;
    if(coverage_active)for(unsigned j=0;j<calls;++j)memset(expected[j].residue,0,8);
    memcpy(fx_after,fx_arena,FX_SIZE);memcpy(fx_arena,fx_before,FX_SIZE);fx_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];for(unsigned i=0;i<count&&i<8;++i)counts[i]=c[i].count;
    fx_pending(index,a);
    pair[1]=x->replacement_context(a,d->user,r);
    if(coverage_active)for(unsigned j=0;j<fx_calls;++j)memset(fx_log[j].residue,0,8);
    if(n<4096)for(unsigned i=0;i<count&&i<8;++i)if(fx_expected_hits(index,c+i)!=UINT_MAX&&c[i].count-counts[i]!=fx_expected_hits(index,c+i))return fail("flyer states callee counter delta");
    if(pair[0]!=pair[1]||calls!=fx_calls||memcmp(expected,fx_log,calls*sizeof(*expected))||memcmp(fx_after,fx_arena,FX_SIZE))
        return fx_report("direct",n,pair,calls,expected);
    return 0;
}
static int fx_patch(unsigned mask,BYTE saved[FX_IDS][5],int active[FX_IDS],int on){
    for(unsigned i=0;i<FX_IDS;++i){
        if(on){active[i]=0;if(!(mask>>i&1)||!fx_addresses[i])continue;if(actor_patch(fx_addresses[i]-0x400000u,fx_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(fx_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
/* Installed through the entry shim: the own caller plants the residue in
 * a 31/a7 stack; the shim must deliver it to the residue stubs. */
static int fx_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3];fx_build(index,a,r,0);const uint32_t pattern=poison?0xa7a7a7a7u:0x31313131u;
        fx_plant p[12];const unsigned plants=fx_plants(index,p);
        BYTE expected_fpu[108],actual_fpu[108];static fx_record expected[FX_MAX];
        memcpy(fx_before,fx_arena,FX_SIZE);uint32_t er[3]={r[0],r[1],r[2]},ar[3]={r[0],r[1],r[2]};
        fx_reset();fx_call(mapped+d->rva,a[0],er,p,plants,pattern);es2_hook_fpu_save(expected_fpu);
        unsigned calls=fx_calls;memcpy(expected,fx_log,calls*sizeof(*expected));memcpy(fx_after,fx_arena,FX_SIZE);
        if(fx_check_residue(index,n))return 1;
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("flyer states poison install");
        memcpy(fx_arena,fx_before,FX_SIZE);fx_reset();fx_call(mapped+d->rva,a[0],ar,p,plants,pattern);es2_hook_fpu_save(actual_fpu);
        uint64_t pair[2]={(uint64_t)er[1]<<32|er[0],(uint64_t)ar[1]<<32|ar[0]};
        if(pair[0]!=pair[1]||calls!=fx_calls||memcmp(expected,fx_log,calls*sizeof(*expected))||memcmp(fx_after,fx_arena,FX_SIZE)||
           memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||(index==ES2_HOOK_INDEX_flyer_engage?h.calls<1:h.calls!=1))
            status=fx_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("flyer states poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases through the entry shim with planted residue, fast/preserving dispatch, nonempty x87\n",
        d->name,total);
    return status;
}
/* Real callees: the nested hooks of each hook's tree in rotating modes
 * (not installed, original, replaced; the hook itself original or
 * replaced) with the real 4224c4, 422108 and 422260; the leaves listed in
 * fx_real_stubs stay stubs. Pair, arena, stub log (with the residue the
 * leaves find) and the stagger word 4a9b70 against an all-original run. */
static unsigned fx_real_stubs(unsigned index){
    switch(index){
    case ES2_HOOK_INDEX_flyer_attacking:return FX_BIT(0)|FX_BIT(7);
    case ES2_HOOK_INDEX_flyer_follow:return FX_BIT(7);
    case ES2_HOOK_INDEX_flyer_formation:case ES2_HOOK_INDEX_flyer_scout:return FX_BIT(7)|FX_BIT(16)|FX_BIT(17);
    case ES2_HOOK_INDEX_flyer_search:case ES2_HOOK_INDEX_flyer_patrol:return FX_BIT(7)|FX_BIT(16)|FX_BIT(17)|FX_BIT(12);
    default:return 0;
    }
}
static unsigned fx_real_nested(unsigned index,unsigned *nested){
    static const unsigned steer[]={ES2_HOOK_INDEX_point_offset,ES2_HOOK_INDEX_dist_gate,ES2_HOOK_INDEX_heading_point};
    unsigned n=0;
    switch(index){
    case ES2_HOOK_INDEX_flyer_search:nested[n++]=ES2_HOOK_INDEX_order_target; /* fall through */
    case ES2_HOOK_INDEX_flyer_patrol:
        nested[n++]=ES2_HOOK_INDEX_timer_word_step;nested[n++]=ES2_HOOK_INDEX_flyer_engage;nested[n++]=ES2_HOOK_INDEX_script_start;
        /* fall through */
    case ES2_HOOK_INDEX_flyer_scout:nested[n++]=ES2_HOOK_INDEX_flyer_formation; /* fall through */
    case ES2_HOOK_INDEX_flyer_formation:nested[n++]=ES2_HOOK_INDEX_dist2d_point; /* fall through */
    case ES2_HOOK_INDEX_flyer_attacking:nested[n++]=ES2_HOOK_INDEX_flyer_follow; /* fall through */
    case ES2_HOOK_INDEX_flyer_follow:for(unsigned j=0;j<3;++j)nested[n++]=steer[j];break;
    default:nested[n++]=ES2_HOOK_INDEX_script_start;break;
    }
    return n;
}
static int fx_real_pass(unsigned index){
    unsigned nested[12];const unsigned count=fx_real_nested(index,nested);
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0,carried=0;uint64_t entered[12]={0};
    BYTE saved[FX_IDS][5];int active[FX_IDS];const unsigned stubs=fx_real_stubs(index);
    BYTE stagger[2];memcpy(stagger,mapped+0xa9b70,2);
    if(fx_patch(stubs,saved,active,1))return 1;
    for(unsigned mode=0;mode<2&&!status;++mode)for(unsigned n=0;n<1536&&!status;++n){
        uint32_t a[6],r[3];fx_build(index,a,r,1);const uint32_t pattern=n&1?0xa7a7a7a7u:0x31313131u;
        fx_plant p[12];const unsigned plants=fx_plants(index,p);static fx_record expected[FX_MAX];
        memcpy(fx_before,fx_arena,FX_SIZE);BYTE stagger_before[2],stagger_after[2];memcpy(stagger_before,mapped+0xa9b70,2);
        uint32_t er[3]={r[0],r[1],r[2]},ar[3]={r[0],r[1],r[2]};
        fx_reset();fx_call(mapped+d->rva,a[0],er,p,plants,pattern);
        unsigned calls=fx_calls;memcpy(expected,fx_log,calls*sizeof(*expected));
        memcpy(fx_after,fx_arena,FX_SIZE);memcpy(stagger_after,mapped+0xa9b70,2);
        for(unsigned j=0;j<calls;++j)if(fx_residue_count[expected[j].id]){++carried;break;}
        es2_hook hk[13];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        unsigned digit=n/2;
        for(unsigned j=0;j<count;++j){
            const unsigned m=(digit+mode+j)%3;digit/=3;if(!digit)digit=n+j;
            if(!m)continue;
            o.mode=m==1?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,function_hook_extension(nested[j])))return fail("flyer states nested install");
        }
        o.mode=mode?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(hk+count,d,&o,function_hook_extension(index)))return fail("flyer states caller install");
        memcpy(fx_arena,fx_before,FX_SIZE);memcpy(mapped+0xa9b70,stagger_before,2);fx_reset();
        fx_call(mapped+d->rva,a[0],ar,p,plants,pattern);
        /* ECX at the stub entries is not compared here: a replaced nested hook
         * returns its entry ECX (framework), e.g. point_offset's, which
         * flyer_follow forwards to +78h, dist_gate and heading_point as the
         * original does with the original's ECX (flyer.md, "the FLYER AI state
         * methods"). */
        for(unsigned j=0;j<calls;++j)expected[j].regs[2]=0;
        for(unsigned j=0;j<fx_calls;++j)fx_log[j].regs[2]=0;
        /* Likewise EDX after a replaced timer_word_step (a legacy-path hook
         * with a 32-bit result): it reaches target_select's entry and the
         * returned EDX of flyer_search/flyer_patrol, dead at the trampoline. */
        for(unsigned j=0;j<count;++j)if(nested[j]==ES2_HOOK_INDEX_timer_word_step&&hk[j].descriptor&&hk[j].options.mode==ES2_HOOK_REPLACED){
            for(unsigned i=0;i<calls;++i)expected[i].regs[1]=0;
            for(unsigned i=0;i<fx_calls;++i)fx_log[i].regs[1]=0;
            er[1]=ar[1]=0;
        }
        uint64_t pair[2]={(uint64_t)er[1]<<32|er[0],(uint64_t)ar[1]<<32|ar[0]};
        if(pair[0]!=pair[1]||calls!=fx_calls||memcmp(expected,fx_log,calls*sizeof(*expected))||memcmp(fx_after,fx_arena,FX_SIZE)||
           memcmp(stagger_after,mapped+0xa9b70,2)){
            fprintf(stderr,"real mode=%u modes:",mode);for(unsigned j=0;j<count;++j)fprintf(stderr," %s=%d",function_hook_descriptor(nested[j])->name,hk[j].descriptor?(int)hk[j].options.mode:-1);
            fprintf(stderr,"\n");status=fx_report("real",n,pair,calls,expected);
        }
        for(unsigned j=0;j<=count;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("flyer states nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("flyer states nested uninstall");}
        ++total;
    }
    if(fx_patch(0,saved,active,0))return 1;
    memcpy(mapped+0xa9b70,stagger,2);
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){
        fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&fx_shim(index)!=FS_SHIMS&&!carried)status=fail("flyer states real pass never reached a residue stub");
    if(!status){printf("PASS: %s %u real-callee cases (%u reached a residue stub), nested hooks in rotating modes, pair, stub log, arena, stagger\n",
        d->name,total,carried);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
static int flyer_states_cases(unsigned index,unsigned limit){
    BYTE saved[FX_IDS][5];int active[FX_IDS];fx_hook=index;fx_limit=limit;
    BYTE stagger[2];memcpy(stagger,mapped+0xa9b70,2);
    if(fx_patch(fx_which(index),saved,active,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_FLYER_STATES_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];fx_build(index,a,r,0);status=fx_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=fx_installed_probes(index,64);
    if(fx_patch(0,saved,active,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=fx_real_pass(index);
    memcpy(mapped+0xa9b70,stagger,2);
    for(unsigned k=0;k<FS_SHIMS;++k)flyer_states_pending[k].esp=0;
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, P by content, residue, arena\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
