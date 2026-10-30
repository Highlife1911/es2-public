/* MECH travel, follow, sleep, ram and dead states: travel_step 41d9cc,
 * follow_step 41daac, sleep_step 41c418, ram_step 41e570, ram_move 41e488,
 * dead_step 41e554, leader_route 41fb60, point_move 41fac4 and fire_permit
 * 41c3c8 (docs/reverse-engineering/ai.md, "MECH travel, follow").
 *
 * Direct cases run the original body and the core against logging stubs
 * for the game callees of the hook under test (ordered argument slots,
 * entry EAX/EDX/ECX, stack addresses masked, ram_move's point by content);
 * the actor virtuals +3Ch and +74h go through the arena vtables. Pair,
 * arena and the globals (terrain map pointer, RNG stream A, frame time)
 * are compared. Then installed 31/a7 poison probes; for travel_step and
 * follow_step (entry shims, calls at the original stack coordinate) the
 * stubs also record EBX, ESI, EDI and EBP/ESP relative to the caller, and
 * the 64 bytes below the caller's argument slots are compared after
 * return. Then a real-callee pass per hook with the nested hooks in
 * rotating modes (travel_step and follow_step with the real aim_select and
 * its shim below theirs; a quarter of those cases fill 264 KiB below the
 * aim_fire/aim_target stubs). */
typedef struct trv_record { uint32_t id,args[5],regs[3],input[3],eax,edx,saved[5]; uint32_t count; } trv_record;
enum { TRV_SIZE=0x2000, TRV_ACT=0x100, TRV_STRIDE=0x340, TRV_ACTORS=6, TRV_GRP=0x1500, TRV_MEM=0x1600, TRV_ORD=0x1680,
    TRV_ROUTE=0x1740, TRV_PTS=0x1800, TRV_CFG=0x1880, TRV_PARTS=0x1980, TRV_MAP=0x1a00, TRV_VT=0x1c00, TRV_SND=0x1d00, TRV_STATE=0x1e00,
    TRV_MAX=64, TRV_IDS=29, TRV_POINTS=8, TRV_GLOBALS=4+0x72+2 };
#define TRV_BASE ((uint32_t)(uintptr_t)trv_arena)
#define TRV_ACTOR(i) (TRV_BASE+TRV_ACT+TRV_STRIDE*(uint32_t)(i))
#define TRV_GROUP(i) (TRV_BASE+TRV_GRP+0x80u*(uint32_t)(i))
#define TRV_POINT(i) (TRV_BASE+TRV_PTS+12u*(uint32_t)(i))
static BYTE trv_arena[TRV_SIZE],trv_before[TRV_SIZE],trv_after[TRV_SIZE];
static trv_record trv_log[TRV_MAX];
static unsigned trv_calls;
static int trv_real,trv_track;
static unsigned trv_hook;
static uint32_t trv_seed,trv_stream;
/* EBX, ESI, EDI, EBP, ESP at the last stub entry; trv_invoke's ESP before
 * its call; the 64 bytes below the argument slots after its call; the
 * deep-fill switch and the stub's ESP during the fill. */
__attribute__((used)) static uint32_t trv_ecx,trv_entry[5],trv_origin,trv_tmp[3],trv_deep,trv_deep_esp;
__attribute__((used)) static BYTE trv_frame_copy[64];
/* 0 41fb60 leader_route, 1 467aa8 timer_dword_step, 2 411fa0
 * target_select, 3 492934 heading_point, 4 41ea7c aim_select, 5 4238a0
 * order_actor, 6 49288c dist_gate, 7 416a04 vehicle_control, 8 41fac4
 * point_move, 9 41c3c8 fire_permit, 10 41e488 ram_move, 11 467a48
 * timer_word_step, 12 492f24 rng_bounded, 13 418f40 ground_animate, 14
 * 46e180 terrain_height, 15 4195c8 ground_effects, 16 418f74
 * ground_collide, 17 426a20 radius_hit, 18 423b0c route_point, 19 42313c
 * route_advance, 20 4928d0 dist2d_point, 21 40f09c (sound trigger), 22
 * 41e984 aim_target, 23 41f5a0 aim_fire (both under the real aim_select);
 * 24..28 the virtuals +3Ch, +74h, +54h, +58h, +30h (arena vtables, never
 * patched). */
static const uint32_t trv_addresses[TRV_IDS]={0x41fb60,0x467aa8,0x411fa0,0x492934,0x41ea7c,0x4238a0,0x49288c,0x416a04,
    0x41fac4,0x41c3c8,0x41e488,0x467a48,0x492f24,0x418f40,0x46e180,0x4195c8,0x418f74,0x426a20,0x423b0c,0x42313c,
    0x4928d0,0x40f09c,0x41e984,0x41f5a0,0,0,0,0,0};
static const unsigned trv_slots[TRV_IDS]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x3c,0x74,0x54,0x58,0x30};
static const unsigned trv_counts[TRV_IDS]={1,1,3,2,3,1,2,4,2,1,1,1,2,1,2,1,1,5,2,1,2,1,3,4,2,4,2,3,3};
static const struct { uint32_t rva,size; } trv_ranges[]={{0xa0be0,4},{0xd2599,0x72},{0xd3b64,2}};
static void trv_globals(BYTE *out,int restore){
    unsigned at=0;
    for(unsigned i=0;i<sizeof(trv_ranges)/sizeof(trv_ranges[0]);++i){BYTE *g=mapped+trv_ranges[i].rva;
        if(restore)memcpy(g,out+at,trv_ranges[i].size);else memcpy(out+at,g,trv_ranges[i].size);at+=trv_ranges[i].size;}
}
static uint32_t trv_next(void){
    uint32_t z=trv_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *trv_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int trv_in(uint32_t a,uint32_t n){return a-TRV_BASE<TRV_SIZE&&TRV_SIZE-(a-TRV_BASE)>=n;}
/* Frame addresses (the committed stack window) are masked. */
static int trv_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static int trv_ok(uint32_t p,uint32_t n){
    const NT_TIB *t=(const NT_TIB *)NtCurrentTeb();uint32_t low=(uint32_t)(uintptr_t)t->StackLimit,top=(uint32_t)(uintptr_t)t->StackBase;
    return trv_in(p,n)||(p>=low&&p<top&&top-p>=n);
}
static void trv_dwords(trv_record *v,uint32_t p,unsigned n){
    if(trv_ok(p,4u*n))for(unsigned j=0;j<n;++j)v->input[j]=get32(trv_host(p)+4*j);
}
static uint32_t trv_actor_pick(uint32_t pick){return TRV_ACTOR(pick%TRV_ACTORS);}
__attribute__((used,noinline)) static uint64_t __cdecl trv_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(trv_calls==TRV_MAX){fail("mech travel call overflow");ExitProcess(1);}
    trv_record *v=trv_log+trv_calls++;memset(v,0,sizeof(*v));v->id=id;
    /* Slots the core pushed: too few is a mismatch; more are allowed, since
     * the core pre-pushes a later call's slot where the original does
     * (PUSH ESI at 41da77 before 492934). */
    v->count=weapons_bridge_slots(a,trv_counts[id]);if(v->count>trv_counts[id])v->count=trv_counts[id];
    if(trv_track){for(unsigned j=0;j<3;++j)v->saved[j]=trv_entry[j];v->saved[3]=trv_entry[3]-trv_origin;v->saved[4]=trv_entry[4]-trv_origin;}
    for(unsigned k=0;k<trv_counts[id];++k)v->args[k]=trv_stack(a[k])?0:a[k];
    for(unsigned j=0;j<3;++j)v->regs[j]=trv_stack(regs[j])?0:regs[j];
    uint32_t eax=trv_next(),edx=trv_next(),pick=trv_next();trv_ecx=trv_next();
    BYTE *o=trv_host(TRV_ACTOR(0));
    /* Perturbations of what the originals read again (direct and installed
     * cases only: the real passes keep the game's order). */
    const int perturb=!trv_real;
    switch(id){
    case 0: /* leader_route: the target travel_step drops next. */
        if(perturb&&pick%5==0)put32(o+0x1a4,pick>>8&1?0:trv_actor_pick(pick>>12));
        break;
    case 1: /* timer_dword_step: EAX zero half of the time; the remembered
             * target (read again when the timer runs). */
        if(pick%2)eax=0;else if(!eax)eax=1;
        if(perturb&&pick%7==0)put32(o+0x5f,pick>>8&1?0:trv_actor_pick(pick>>12));
        break;
    case 2: /* target_select: 0, the current target or another actor; the
             * target, which ram_step compares with the result. */
        eax=pick%3==0?0:pick%3==1?get32(o+0x1a4):trv_actor_pick(pick>>4);
        if(perturb&&pick%11==0)put32(o+0x1a4,(pick>>8)%3==0?0:(pick>>8)%3==1?eax:trv_actor_pick(pick>>12));
        break;
    case 3: /* heading_point: the heading words read after it. */
        if(perturb&&pick%6==0)put16(trv_host(trv_actor_pick(pick>>4))+0x10,(uint16_t)trv_next());
        break;
    case 5: /* order_actor: an actor, in direct cases also any value. */
        if(!perturb||pick%4)eax=trv_actor_pick(pick>>4);
        break;
    case 6: { /* dist_gate: the 25000 edge. */
        static const uint32_t d[]={24999,25000,25001,0,0xffffffffu,0x7fffffffu,0x80000000u};
        if(!trv_real&&pick%4)eax=d[(pick>>4)%7];
        break; }
    case 7: /* vehicle_control: the target (follow_step's drop) and the
             * charge word (ram_step). */
        if(perturb&&pick%9==0)put32(o+0x1a4,pick>>8&1?0:trv_actor_pick(pick>>12));
        if(perturb&&trv_hook==ES2_HOOK_INDEX_ram_step&&pick%7==0)put16(o+0x5f,(uint16_t)(pick>>16&1));
        break;
    case 8: /* point_move: AX zero half of the time; the group (leader_route
             * reads [A+45h] again) and the target. */
        if(pick%2)eax&=0xffff0000u;else if(!(uint16_t)eax)eax|=1u;
        if(perturb&&pick%5==0)put32(o+0x45,TRV_GROUP(pick>>8&1));
        if(perturb&&pick%9==0)put32(o+0x1a4,pick>>12&1?0:trv_actor_pick(pick>>16));
        break;
    case 10: /* ram_move: the target (read again). */
        if(perturb&&pick%3==0)put32(o+0x1a4,pick>>8&1?0:trv_actor_pick(pick>>12));
        break;
    case 11: /* timer_word_step: AX zero half of the time. */
        if(pick%2)eax&=0xffff0000u;else if(!(uint16_t)eax)eax|=1u;
        break;
    case 13: /* ground_animate: the position (read after it). */
        if(perturb&&pick%3==0)for(unsigned j=0;j<3;++j)put32(o+0x26+4*j,trv_next());
        break;
    case 14: /* terrain_height: the point by content; the configuration
              * (read after it). */
        trv_dwords(v,a[1],3);
        if(perturb&&pick%5==0)put32(o+0x1f2,TRV_BASE+TRV_CFG+0x80u*(pick>>8&1));
        if(perturb&&pick%7==0)put16(trv_host(get32(o+0x1f2))+0x16,(uint16_t)trv_next());
        break;
    case 16: /* ground_collide: AX zero half of the time; byte A+0B1h. */
        if(pick%2)eax&=0xffff0000u;else if(!(uint16_t)eax)eax|=1u;
        if(perturb&&pick%5==0)o[0xb1]=(BYTE)(pick>>8&1);
        break;
    case 17: /* radius_hit: the parts pointer and a part word (read after). */
        if(perturb&&pick%5==0)put32(o+0x20e,TRV_BASE+TRV_PARTS+0x40u*(pick>>8&1));
        if(perturb&&pick%3==0)put16(trv_host(get32(o+0x20e))+2u*((pick>>12)%29u),(uint16_t)(pick>>20&1));
        break;
    case 18: /* route_point: 0, a point or any value. */
        eax=pick%3==0?0:pick%3==1?TRV_POINT((pick>>4)%TRV_POINTS):eax;
        break;
    case 20: { /* dist2d_point: the 10000 and 30000 edges. */
        static const uint32_t d[]={9999,10000,10001,29999,30000,30001,0,0xffffffffu,0x80000000u};
        if(pick%4)eax=d[(pick>>4)%9];
        break; }
    case 23: /* aim_fire: the aim point by content. */
        trv_dwords(v,a[1],3);
        break;
    case 25: /* +74h: a later part word (read in every round) and A's
              * vtable (read for every call). */
        if(perturb&&pick%4==0)put16(trv_host(get32(o+0x20e))+2u*((pick>>8)%29u),(uint16_t)(pick>>16&1));
        if(perturb&&pick%7==0)put32(o,TRV_BASE+TRV_VT+0x80u*(pick>>20&1));
        break;
    case 26: /* +54h: a part index. */
        eax=(eax&0xffff0000u)|(uint16_t)(pick%4?pick>>4&0x1f:trv_next());
        break;
    case 27: /* +58h: writes the part position (12 bytes). */
        if(trv_ok(a[2],12))for(unsigned j=0;j<3;++j)put32(trv_host(a[2])+4*j,trv_next());
        break;
    case 28: /* +30h: writes both 6-byte records. */
        for(unsigned k=1;k<3;++k)if(trv_ok(a[k],6))for(unsigned j=0;j<3;++j)put16(trv_host(a[k])+2*j,(uint16_t)trv_next());
        break;
    default:break;
    }
    v->eax=trv_stack(eax)?0:eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
/* Every stub saves the entry registers; the aim stubs (deep=1) then fill
 * 66 pages (264 KiB: aim_fire's 256-KiB gap plus 8 KiB of its host frames)
 * below their entry with 5Ah while trv_deep is set. */
#define TRV_STUB(n,deep) __attribute__((naked)) static void trv_stub_##n(void){__asm__ volatile( \
    "movl %ebx,_trv_entry\n\tmovl %esi,_trv_entry+4\n\tmovl %edi,_trv_entry+8\n\tmovl %ebp,_trv_entry+12\n\tmovl %esp,_trv_entry+16\n\t" \
    ".if " #deep "\n\tcmpl $0,_trv_deep\n\tje 2f\n\t" \
    "movl %eax,_trv_tmp\n\tmovl %ecx,_trv_tmp+4\n\tmovl %edi,_trv_tmp+8\n\tmovl %esp,_trv_deep_esp\n\tmovl $66,%ecx\n\t" \
    "1: subl $4096,%esp\n\torl $0,(%esp)\n\tloop 1b\n\t" \
    "movl %esp,%edi\n\tmovl $0x5a5a5a5a,%eax\n\tmovl $67584,%ecx\n\tcld\n\trep stosl\n\tmovl _trv_deep_esp,%esp\n\t" \
    "movl _trv_tmp,%eax\n\tmovl _trv_tmp+4,%ecx\n\tmovl _trv_tmp+8,%edi\n\t2:\n\t.endif\n\t" \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _trv_stub_impl\n\taddl $24,%esp\n\tmovl _trv_ecx,%ecx\n\tret\n\t");}
TRV_STUB(0,0) TRV_STUB(1,0) TRV_STUB(2,0) TRV_STUB(3,0) TRV_STUB(4,0) TRV_STUB(5,0) TRV_STUB(6,0) TRV_STUB(7,0) TRV_STUB(8,0)
TRV_STUB(9,0) TRV_STUB(10,0) TRV_STUB(11,0) TRV_STUB(12,0) TRV_STUB(13,0) TRV_STUB(14,0) TRV_STUB(15,0) TRV_STUB(16,0)
TRV_STUB(17,0) TRV_STUB(18,0) TRV_STUB(19,0) TRV_STUB(20,0) TRV_STUB(21,0) TRV_STUB(22,1) TRV_STUB(23,1) TRV_STUB(24,0)
TRV_STUB(25,0) TRV_STUB(26,0) TRV_STUB(27,0) TRV_STUB(28,0)
#undef TRV_STUB
static void *trv_stubs[TRV_IDS]={trv_stub_0,trv_stub_1,trv_stub_2,trv_stub_3,trv_stub_4,trv_stub_5,trv_stub_6,trv_stub_7,
    trv_stub_8,trv_stub_9,trv_stub_10,trv_stub_11,trv_stub_12,trv_stub_13,trv_stub_14,trv_stub_15,trv_stub_16,trv_stub_17,
    trv_stub_18,trv_stub_19,trv_stub_20,trv_stub_21,trv_stub_22,trv_stub_23,trv_stub_24,trv_stub_25,trv_stub_26,trv_stub_27,
    trv_stub_28};
#define TRV_BIT(i) (1u<<(i))
/* Direct callees of each hook (bit i = trv_addresses[i]). */
static uint32_t trv_which(unsigned index){
    if(index==ES2_HOOK_INDEX_travel_step)return TRV_BIT(0)|TRV_BIT(1)|TRV_BIT(2)|TRV_BIT(3)|TRV_BIT(4);
    if(index==ES2_HOOK_INDEX_follow_step)return TRV_BIT(5)|TRV_BIT(6)|TRV_BIT(7)|TRV_BIT(8)|TRV_BIT(1)|TRV_BIT(2)|TRV_BIT(3)|TRV_BIT(4);
    if(index==ES2_HOOK_INDEX_sleep_step)return TRV_BIT(7)|TRV_BIT(9);
    if(index==ES2_HOOK_INDEX_ram_step)return TRV_BIT(1)|TRV_BIT(2)|TRV_BIT(3)|TRV_BIT(7)|TRV_BIT(10)|TRV_BIT(11)|TRV_BIT(12);
    if(index==ES2_HOOK_INDEX_ram_move)return TRV_BIT(13)|TRV_BIT(14)|TRV_BIT(15)|TRV_BIT(16)|TRV_BIT(17);
    if(index==ES2_HOOK_INDEX_dead_step)return TRV_BIT(7);
    if(index==ES2_HOOK_INDEX_leader_route)return TRV_BIT(18)|TRV_BIT(7)|TRV_BIT(8)|TRV_BIT(19);
    if(index==ES2_HOOK_INDEX_point_move)return TRV_BIT(3)|TRV_BIT(20)|TRV_BIT(21)|TRV_BIT(7);
    return 0;
}
static int trv_patch(BYTE saved[TRV_IDS][5],int active[TRV_IDS],uint32_t which,int on){
    for(unsigned i=0;i<TRV_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!trv_addresses[i])continue;if(actor_patch(trv_addresses[i]-0x400000u,trv_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(trv_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
static void trv_reset(void){trv_calls=0;trv_stream=trv_seed;}
static uint16_t trv_word(const uint16_t *values,unsigned count){return random32()%4?values[random32()%count]:(uint16_t)random32();}
static BYTE trv_flag(void){return (BYTE)(random32()%4?0:random32()%2?1:random32());}
static uint32_t trv_coordinate(void){return (uint32_t)((int32_t)(random32()%40001u)-20000);}
/* The arena: six actors, two groups with member lists, route holders and
 * order slots, eight orders, two routes over eight points, two
 * configurations and part lists, the terrain map header, two vtables with
 * the virtual stubs, a sound record. real: consistent data for the real
 * callees (positions in range, routes over arena points, orders whose
 * actor exists, a terrain map that answers 0 off the grid, small frame
 * time). */
static void trv_build(unsigned index,uint32_t a[6],uint32_t r[3],int real){
    const uint32_t b=TRV_BASE,A=TRV_ACTOR(0);
    trv_seed=random32();random_bytes(trv_arena,TRV_SIZE);memset(a,0,24);trv_real=real;trv_hook=index;
    for(unsigned i=0;i<3;++i)r[i]=random32();
    for(unsigned v=0;v<2;++v){static const unsigned slots[]={0x3c,0x74,0x54,0x58,0x30};
        for(unsigned k=0;k<5;++k)put32(trv_arena+TRV_VT+0x80*v+slots[k],(uint32_t)(uintptr_t)trv_stubs[24+k]);}
    for(unsigned i=0;i<TRV_ACTORS;++i){
        BYTE *p=trv_host(TRV_ACTOR(i));
        put32(p,b+TRV_VT+0x80u*(random32()%2));put32(p+0x45,TRV_GROUP(random32()%2));
        static const unsigned bytes[]={0x97,0xa3,0xb1,0xb2};
        for(unsigned k=0;k<4;++k)p[bytes[k]]=trv_flag();
        put32(p+0x1a4,random32()%4?TRV_ACTOR(random32()%TRV_ACTORS):0);
        static const uint16_t counts[]={0,1,2,3,0xffff};put16(p+0x1a2,trv_word(counts,5));
        static const uint16_t kinds[]={0,1,2,3,0,1};put16(p+0x1a8,trv_word(kinds,6));
        put32(p+0x1f2,b+TRV_CFG+0x80u*(random32()%2));put32(p+0x20e,b+TRV_PARTS+0x40u*(random32()%2));
        static const uint16_t speeds[]={0,0,0x80,0x100,0xffff};put16(p+0x252,trv_word(speeds,5));
        static const uint16_t states[]={0,0,1,2,4};put16(p+0x23e,trv_word(states,5));
        put32(p+0x317,random32()%2?0:b+TRV_SND);
        /* B+5: the remembered target of travel/follow (an actor or 0). */
        put32(p+0x5f,random32()%3?TRV_ACTOR(random32()%TRV_ACTORS):0);
        if(real){for(unsigned j=0;j<3;++j)put32(p+0x26+4*j,trv_coordinate());
            if(!get32(p+0x26))put32(p+0x26,1);
            put16(p+0x1a2,(uint16_t)(random32()%3));put32(p+0x5b,random32()%3==0?0:random32()%400);}
    }
    for(unsigned g=0;g<2;++g){BYTE *p=trv_host(TRV_GROUP(g));const uint32_t list=b+TRV_MEM+0x40u*g;
        put32(p+0xc,list);static const uint16_t sizes[]={1,2,3,6};put16(p+0x10,sizes[random32()%4]);
        for(unsigned k=0;k<8;++k)put32(trv_arena+TRV_MEM+0x40*g+4*k,TRV_ACTOR(random32()%TRV_ACTORS));
        if(random32()%2)put32(trv_arena+TRV_MEM+0x40*g,A);
        /* The route holder G+4: index word, route pointer at +2. */
        static const uint16_t indices[]={0,1,2,3,7,0xffff};put16(p+4,real?indices[random32()%6]:trv_word(indices,6));
        put32(p+6,random32()%5==0?0:b+TRV_ROUTE+0x40u*(random32()%2));
        put32(p+0x6c,random32()%4);
        for(unsigned s=0;s<4;++s)put32(p+0x44+4*s,real||random32()%4?b+TRV_ORD+0x18u*(random32()%8):0);
    }
    for(unsigned k=0;k<8;++k){BYTE *o=trv_arena+TRV_ORD+0x18*k;
        static const uint16_t scopes[]={0,1,2,3,4,0xffff};
        const uint16_t scope=real?(uint16_t)(random32()%4):trv_word(scopes,6);put16(o+0xc,scope);
        put32(o+0xe,scope==0?TRV_GROUP(random32()%2):TRV_ACTOR(random32()%TRV_ACTORS));}
    for(unsigned k=0;k<2;++k){BYTE *q=trv_arena+TRV_ROUTE+0x40*k;
        static const uint16_t lengths[]={0,1,2,3,8};put16(q,trv_word(lengths,5));put32(q+2,b+TRV_ROUTE+0x40u*k+8u);
        for(unsigned j=0;j<8;++j)put32(q+8+4*j,random32()%6==0?0:TRV_POINT(random32()%TRV_POINTS));
        if(real)put16(q,(uint16_t)(random32()%9));}
    for(unsigned k=0;k<TRV_POINTS;++k){BYTE *q=trv_host(TRV_POINT(k));
        if(real){const uint32_t centre=TRV_ACTOR(0);for(unsigned j=0;j<3;++j)put32(q+4*j,get32(trv_host(centre)+0x26+4*j)+(uint32_t)(int32_t)(random32()%24001u)-12000u);}}
    for(unsigned k=0;k<2;++k){static const uint16_t lifts[]={0,0x200,0xfff6};put16(trv_arena+TRV_CFG+0x80*k+0x16,trv_word(lifts,3));
        for(unsigned j=0;j<29;++j)put16(trv_arena+TRV_PARTS+0x40*k+2*j,(uint16_t)(random32()%3?0:random32()%2?1:random32()));}
    put32(mapped+0xa0be0,b+TRV_MAP);
    if(real){memset(trv_arena+TRV_MAP+0x100,0,12);put16(mapped+0xd3b64,(uint16_t)(1+random32()%200));
        /* Without a route the leader stands; the real route data needs the
         * holder pointer. */
        for(unsigned g=0;g<2;++g)if(!get32(trv_host(TRV_GROUP(g))+6))put32(trv_host(TRV_GROUP(g))+6,b+TRV_ROUTE);}
    BYTE *o=trv_host(A);
    if(index==ES2_HOOK_INDEX_ram_step){static const uint16_t phases[]={0,1,0,1,2};put16(o+0x5f,trv_word(phases,5));}
    a[0]=A;
    if(index==ES2_HOOK_INDEX_point_move)a[1]=random32()%2?TRV_ACTOR(random32()%TRV_ACTORS)+0x26u:TRV_POINT(random32()%TRV_POINTS);
}
static int trv_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const trv_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,trv_calls);
    for(unsigned j=0;j<calls&&j<trv_calls&&expected;++j)if(memcmp(expected+j,trv_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(trv_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<TRV_SIZE;++j)if(trv_after[j]!=trv_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,trv_after[j],trv_arena[j]);break;}
    return 1;
}
static unsigned trv_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<trv_calls;++j){unsigned id=trv_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?trv_slots[id]==c->key:!trv_slots[id]&&trv_addresses[id]==c->key)++hits;}
    return hits;
}
static int trv_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[TRV_GLOBALS],after[TRV_GLOBALS],actual[TRV_GLOBALS];static trv_record expected[TRV_MAX];
    memcpy(trv_before,trv_arena,TRV_SIZE);trv_globals(before,0);
    trv_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=trv_calls;memcpy(expected,trv_log,calls*sizeof(*expected));
    memcpy(trv_after,trv_arena,TRV_SIZE);trv_globals(after,0);
    memcpy(trv_arena,trv_before,TRV_SIZE);trv_globals(before,1);trv_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];
    if(count>16)return fail("mech travel counter table too long");
    for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);trv_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=trv_expected_hits(c+i))return fail("mech travel callee counter delta");
    if(pair[0]!=pair[1]||calls!=trv_calls||memcmp(expected,trv_log,calls*sizeof(*expected))||memcmp(trv_after,trv_arena,TRV_SIZE)||
       memcmp(after,actual,TRV_GLOBALS))
        return trv_report("direct",n,pair,calls,expected);
    trv_globals(before,1);
    return 0;
}
/* Calls target with two slots, EAX/EDX/ECX/EBX = pattern, ESI and EDI two
 * other values derived from it and a patterned stack below the arguments;
 * returns EAX, EDX and ECX in out and copies the 64 bytes below the slots
 * after the call. */
__attribute__((naked,noinline)) static void trv_invoke(void *target,const uint32_t *a,uint32_t pattern,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 16(%ebp),%eax\n\tmovl $1024,%ecx\n\tcld\n\trep stosl\n\taddl $4088,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl %esp,_trv_origin\n\t"
        "movl 16(%ebp),%eax\n\tmovl %eax,%edx\n\tmovl %eax,%ecx\n\tmovl %eax,%ebx\n\tmovl %eax,%esi\n\txorl $0x5a5a5a5a,%esi\n\t"
        "movl %eax,%edi\n\txorl $0x3c3c3c3c,%edi\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%ebx\n\tmovl %eax,(%ebx)\n\tmovl %edx,4(%ebx)\n\tmovl %ecx,8(%ebx)\n\t"
        "leal -64(%esp),%esi\n\tmovl $_trv_frame_copy,%edi\n\tmovl $16,%ecx\n\tcld\n\trep movsl\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static int trv_shimmed(unsigned index){return index==ES2_HOOK_INDEX_travel_step||index==ES2_HOOK_INDEX_follow_step;}
static int trv_frame_report(const char *what,unsigned n,const BYTE expected[64]){
    fprintf(stderr,"FAIL: %s case %u (%s)\n",current_function,n,what);
    for(unsigned j=0;j<64;++j)if(expected[j]!=trv_frame_copy[j]){fprintf(stderr," below the arguments -%u: %02x/%02x\n",64-j,expected[j],trv_frame_copy[j]);break;}
    return 1;
}
static int trv_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;trv_track=trv_shimmed(index);
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_out[3],actual_out[3],pattern=poison?0xa7a7a7a7u:0x31313131u;trv_build(index,a,r,0);BYTE expected_frame[64];
        BYTE before[TRV_GLOBALS],after[TRV_GLOBALS],actual[TRV_GLOBALS],expected_fpu[108],actual_fpu[108];static trv_record expected[TRV_MAX];
        memcpy(trv_before,trv_arena,TRV_SIZE);trv_globals(before,0);
        trv_reset();trv_invoke(mapped+d->rva,a,pattern,expected_out);es2_hook_fpu_save(expected_fpu);memcpy(expected_frame,trv_frame_copy,64);
        unsigned calls=trv_calls;memcpy(expected,trv_log,calls*sizeof(*expected));memcpy(trv_after,trv_arena,TRV_SIZE);trv_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("mech travel poison install");
        memcpy(trv_arena,trv_before,TRV_SIZE);trv_globals(before,1);trv_reset();trv_invoke(mapped+d->rva,a,pattern,actual_out);
        es2_hook_fpu_save(actual_fpu);trv_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_out[1]<<32|expected_out[0],(uint64_t)actual_out[1]<<32|actual_out[0]};
        if(pair[0]!=pair[1]||calls!=trv_calls||memcmp(expected,trv_log,calls*sizeof(*expected))||memcmp(trv_after,trv_arena,TRV_SIZE)||
           memcmp(after,actual,TRV_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=trv_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        else if(trv_track&&memcmp(expected_frame,trv_frame_copy,64))status=trv_frame_report(fast?"installed fast frame":"installed preserving frame",n,expected_frame);
        if(!es2_hook_uninstall(&h))return fail("mech travel poison uninstall");trv_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));trv_track=0;
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state%s\n",d->name,total,
        trv_shimmed(index)?", EBX/ESI/EDI/EBP/ESP at every callee entry and the frame below the arguments":"");
    return status;
}
/* The real-callee pass: nested hooks in rotating original/replaced/compare
 * modes (compare falls back to replaced where refused), the hook under test
 * original or replaced; keep = the callees that stay logging stubs. For
 * the two shim hooks every third rotation installs the nested hooks
 * without a shim original and aim_select original or replaced, and only
 * there the registers at every stub entry and the frame below the
 * arguments are compared: a replaced callee without a shim leaves its
 * framework's entry flags below its entry, which come from the caller's
 * last instruction (vtables.md, "Stack coordinate"). */
static unsigned trv_nested(unsigned index,unsigned nested[10],uint32_t *keep){
    unsigned count=0;*keep=0;
#define TRV_N(name) nested[count++]=ES2_HOOK_INDEX_##name
    if(index==ES2_HOOK_INDEX_travel_step){TRV_N(leader_route);TRV_N(route_point);TRV_N(route_advance);TRV_N(point_move);
        TRV_N(heading_point);TRV_N(dist2d_point);TRV_N(timer_dword_step);TRV_N(aim_select);
        *keep=TRV_BIT(2)|TRV_BIT(7)|TRV_BIT(21)|TRV_BIT(22)|TRV_BIT(23);}
    else if(index==ES2_HOOK_INDEX_follow_step){TRV_N(order_actor);TRV_N(dist_gate);TRV_N(point_move);TRV_N(heading_point);
        TRV_N(dist2d_point);TRV_N(timer_dword_step);TRV_N(aim_select);*keep=TRV_BIT(2)|TRV_BIT(7)|TRV_BIT(21)|TRV_BIT(22)|TRV_BIT(23);}
    else if(index==ES2_HOOK_INDEX_sleep_step){TRV_N(fire_permit);*keep=TRV_BIT(7);}
    else if(index==ES2_HOOK_INDEX_ram_step){TRV_N(ram_move);TRV_N(terrain_height);TRV_N(timer_dword_step);TRV_N(timer_word_step);
        TRV_N(rng_bounded);TRV_N(heading_point);*keep=TRV_BIT(2)|TRV_BIT(7)|TRV_BIT(13)|TRV_BIT(15)|TRV_BIT(16)|TRV_BIT(17);}
    else if(index==ES2_HOOK_INDEX_ram_move){TRV_N(terrain_height);*keep=TRV_BIT(13)|TRV_BIT(15)|TRV_BIT(16)|TRV_BIT(17);}
    else if(index==ES2_HOOK_INDEX_leader_route){TRV_N(route_point);TRV_N(route_advance);TRV_N(point_move);TRV_N(heading_point);
        TRV_N(dist2d_point);*keep=TRV_BIT(7)|TRV_BIT(21);}
    else if(index==ES2_HOOK_INDEX_point_move){TRV_N(heading_point);TRV_N(dist2d_point);*keep=TRV_BIT(7)|TRV_BIT(21);}
#undef TRV_N
    return count;
}
static int trv_real_pass(unsigned index){
    unsigned nested[10];uint32_t keep;const unsigned count=trv_nested(index,nested,&keep);
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0,tracked=0,deep=0;uint64_t entered[10]={0},trampoline=0;
    BYTE saved[TRV_IDS][5];int active[TRV_IDS];
    BYTE old_frame_time[2];memcpy(old_frame_time,mapped+0xd3b64,2);
    if(trv_patch(saved,active,keep,1))return 1;
    static trv_record expected[TRV_MAX];
    const int shim=trv_shimmed(index);
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_out[3],actual_out[3],pattern=n&1?0xa7a7a7a7u:0x31313131u;trv_build(index,a,r,1);BYTE expected_frame[64];
        BYTE before[TRV_GLOBALS],after[TRV_GLOBALS],actual[TRV_GLOBALS];
        trv_track=shim&&mode%3==0;trv_deep=shim&&n%4==3;
        /* One shim-hook case in eight enters through the MECH behaviour
         * trampoline 415b38 (mech_act, original or replaced in both runs)
         * from a state record whose member +18h is the hook under test, so
         * three shims nest (80, 72 and 68 pages). */
        const int via=shim&&n%8==7;void *target=mapped+d->rva;
        if(via){const uint32_t st=TRV_BASE+TRV_STATE;put32(trv_host(TRV_ACTOR(0))+0x4d,st);
            put32(trv_arena+TRV_STATE+0x18,(uint32_t)(uintptr_t)(mapped+d->rva));put32(trv_arena+TRV_STATE+0x1c,0);put32(trv_arena+TRV_STATE+0x20,0);
            target=mapped+0x15b38;}
        memcpy(trv_before,trv_arena,TRV_SIZE);trv_globals(before,0);
        es2_hook hk[11];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        if(via){o.mode=(mode+n/8)%2?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+9,function_hook_descriptor(ES2_HOOK_INDEX_mech_act),&o,function_hook_extension(ES2_HOOK_INDEX_mech_act)))return fail("mech travel trampoline install");}
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            if(trv_track)o.mode=nested[j]==ES2_HOOK_INDEX_aim_select&&n%2?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!nested_install_hook(hk+j,nested[j],&o,e))return fail("mech travel nested install");}
        trv_reset();trv_invoke(target,a,pattern,expected_out);memcpy(expected_frame,trv_frame_copy,64);
        unsigned calls=trv_calls;memcpy(expected,trv_log,calls*sizeof(*expected));
        memcpy(trv_after,trv_arena,TRV_SIZE);trv_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+10,d,&o,function_hook_extension(index)))return fail("mech travel caller install");
        memcpy(trv_arena,trv_before,TRV_SIZE);trv_globals(before,1);trv_reset();
        trv_invoke(target,a,pattern,actual_out);trv_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_out[1]<<32|expected_out[0],(uint64_t)actual_out[1]<<32|actual_out[0]};
        if(pair[0]!=pair[1]||calls!=trv_calls||memcmp(expected,trv_log,calls*sizeof(*expected))||memcmp(trv_after,trv_arena,TRV_SIZE)||
           memcmp(after,actual,TRV_GLOBALS)){
            fprintf(stderr,"real mode=%u via=%d\n",mode,via);status=trv_report("real",n,pair,calls,expected);
        }
        else if(trv_track&&memcmp(expected_frame,trv_frame_copy,64)){fprintf(stderr,"real mode=%u via=%d\n",mode,via);status=trv_frame_report("real frame",n,expected_frame);}
        trampoline+=hk[9].calls;
        for(unsigned j=0;j<11;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("mech travel nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("mech travel nested uninstall");}
        trv_globals(before,1);++total;tracked+=(unsigned)trv_track;deep+=(unsigned)trv_deep;
    }
    if(trv_patch(saved,active,0,0))return 1;
    trv_real=0;trv_track=0;trv_deep=0;memcpy(mapped+0xd3b64,old_frame_time,2);
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&shim&&!trampoline){fprintf(stderr,"FAIL: %s never entered through a replaced mech_act\n",d->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases (%u with the stack compared, %u filling 264 KiB below the aim stubs, %llu through a replaced mech_act), %u nested hooks in rotating modes, 31/a7 stack, pair, memory, globals and stub inputs\n",
        d->name,total,tracked,deep,(unsigned long long)trampoline,count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
/* Compare cases (compare_probes.h) for fire_permit (span A+96h..A+0B2h, EAX =
 * EDX = A+92h remapped by its comparator). */
static void trv_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){(void)n;trv_build(index,a,r,1);}
static void trv_compare_state(BYTE *s,int restore){
    if(restore){memcpy(trv_arena,s,TRV_SIZE);trv_globals(s+TRV_SIZE,1);}else{memcpy(s,trv_arena,TRV_SIZE);trv_globals(s+TRV_SIZE,0);}
}
static int mech_travel_cases(unsigned index,unsigned limit){
    uint32_t which=trv_which(index);
    BYTE saved[TRV_IDS][5],old_globals[TRV_GLOBALS];int active[TRV_IDS];
    trv_globals(old_globals,0);
    if(limit&&compare_probe_installed(index)){int status=compare_probe_run(index,limit,trv_compare_build,trv_compare_state,TRV_SIZE+TRV_GLOBALS);
        trv_globals(old_globals,1);return status;}
    if(trv_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_MECH_TRAVEL_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];trv_build(index,a,r,0);status=trv_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=trv_installed_probes(index,64);
    if(trv_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    /* fire_permit is a leaf; dead_step's only callee, vehicle_control, stays
     * a stub (its subtree reaches the SOS sound calls): no real pass. */
    if(!limit&&!status&&index!=ES2_HOOK_INDEX_fire_permit&&index!=ES2_HOOK_INDEX_dead_step)status=trv_real_pass(index);
    if(!limit&&!status&&compare_probe_capable(index))status=compare_probe_run(index,0,trv_compare_build,trv_compare_state,TRV_SIZE+TRV_GLOBALS);
    trv_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, frame records by content, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
