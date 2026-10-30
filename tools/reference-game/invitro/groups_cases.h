/* Group activation and current-order update (mission_runtime.md, "Group
 * activation and current-order update"): group_activate 4236c4, spawn_point
 * 42354c, order_update 423a74, group_assess 423f08, order_actor 4238a0,
 * order_point 4238d4, actor_destination 41dbcc. Direct cases: every game callee
 * is a logging stub that records its ordered arguments, entry registers (stack
 * addresses masked) and the contents of the frame buffers it receives, and
 * changes the group, order, trigger, list, player and counter state between
 * calls, so cached and re-read values are checked. The runtime frame setup
 * 49630c is a record stub that keeps the real FS:0 head. The original is called
 * with ESI = 0 (group_activate builds its mode 4/5 angle over the caller's ESI
 * high word, which the core takes as 0). Then installed 31/a7 poison cases and,
 * for the hooks with nested hooks, a real-callee pass: rng_next on a random
 * stream-A state, pool_alloc on a real pool, spawn_point, dist_gate and
 * face_test (flat terrain grid), order_done with its helpers, and order_point
 * in rotating modes; only 409b44, 404ae4, 411cec and the actors' virtuals
 * +40h/+78h/+7Ch are stubs. */
typedef struct gp_record { uint32_t id,args[4],regs[3],input[8],eax,edx,slots; } gp_record;
enum { GP_SIZE=0x2400, GP_ACTORS=0x0000, GP_GROUPS=0x1300, GP_ORDERS=0x1600, GP_TRIGGERS=0x1700, GP_MEMBERS=0x1780,
    GP_ROUTES=0x1840, GP_POINTERS=0x1880, GP_LIST=0x1900, GP_VTABLE=0x1940, GP_OUT=0x19c0, GP_COUNTER=0x19f0,
    GP_POOL=0x1a00, GP_NODES=0x1a20, GP_OBJECT=0x1ae0, GP_MAP=0x1b00, GP_CELLS=0x1c40, GP_END=0x2040,
    GP_MAX=192, GP_IDS=17, GP_NACTORS=8, GP_NGROUPS=6, GP_NORDERS=10, GP_NTRIGGERS=6, GP_NROUTES=4, GP_NLIST=8,
    GP_GLOBALS=144, GP_LIMIT=6 };
#define GP_BASE ((uint32_t)(uintptr_t)gp_arena)
static BYTE gp_arena[GP_SIZE],gp_before[GP_SIZE],gp_after[GP_SIZE];
static gp_record gp_log[GP_MAX];
static unsigned gp_calls,gp_index,gp_iter,gp_rejects,gp_radius_calls;
static int gp_real;
static uint32_t gp_seed,gp_stream,gp_subject,gp_radius;
__attribute__((used)) static uint32_t gp_ecx;
/* Patched callees (ids 0..13) and the actors' virtual slots reached through
 * the arena vtable (14: +78h, 15: +7Ch, 16: +40h for the real 412dc4). */
static const uint32_t gp_addresses[GP_IDS]={0x49630c,0x492ee0,0x42354c,0x471b30,0x409b44,0x47eb30,0x47ffe8,0x49288c,
    0x404ae4,0x46ff90,0x4239fc,0x411cec,0x41b804,0x4238d4,0,0,0};
static const unsigned gp_counts[GP_IDS]={0,1,4,1,3,2,3,2,3,3,2,1,1,1,2,1,1};
static uint32_t gp_next(void){
    uint32_t z=gp_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *gp_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
/* The worker's 16 MiB stack window: frame addresses are masked identically. */
static int gp_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static uint32_t gp_actor(unsigned k){return GP_BASE+GP_ACTORS+0x260u*k;}
static uint32_t gp_group(unsigned k){return GP_BASE+GP_GROUPS+0x7au*k;}
static uint32_t gp_order(unsigned k){return GP_BASE+GP_ORDERS+0x16u*k;}
static uint32_t gp_trigger(unsigned k){return GP_BASE+GP_TRIGGERS+0x10u*k;}
static uint32_t gp_members(unsigned k){return GP_BASE+GP_MEMBERS+0x20u*k;}
static uint32_t gp_route(unsigned k){return GP_BASE+GP_ROUTES+8u*k;}
static uint32_t gp_pick(const uint32_t *values,unsigned count){return values[random32()%count];}
/* Predicates: zero, one, the sign bit, a nonzero high word over AX = 0. */
static uint32_t gp_flag(uint32_t pick){
    switch(pick%8){case 0:case 1:case 2:return 0;case 3:return 1;case 4:return 0x80000000u;
        case 5:return 0x10000u|(gp_next()&0xffff0000u);case 6:return (gp_next()&0xffff0000u)|1u;default:return gp_next();}
}
/* Re-read state, changed by stubs between calls. */
static void gp_shake(uint32_t pick){
    uint32_t g=gp_subject;
    if(gp_index==ES2_HOOK_INDEX_actor_destination||gp_index==ES2_HOOK_INDEX_spawn_point)g=gp_group(gp_next()%GP_NGROUPS);
    switch(pick%9){
    case 0: { static const uint16_t counts[]={0,1,2,3,4,5,8,0xffff,0x8000,0xfff0};
        put16(gp_host(g+0x10),counts[gp_next()%10]);break; }
    case 1: put32(gp_host(g+0xc),gp_members(gp_next()%GP_NGROUPS));break;
    case 2: put32(gp_host(g+0x6c),gp_next()%3?gp_next()%13u:0xffffffffu);break;
    case 3: put32(mapped+0xd24e6,gp_actor(gp_next()%GP_NACTORS));break;
    case 4: put16(mapped+0xa9afe,(uint16_t)(gp_next()%(GP_NLIST+1u)));break;
    case 5: put32(gp_host(GP_BASE+GP_LIST+4*(gp_next()%GP_NLIST)),gp_next()%4?gp_actor(gp_next()%GP_NACTORS):0);break;
    case 6: put32(gp_arena+GP_COUNTER,gp_next());break;
    case 7: put32(mapped+0xa96aa,gp_next()%2?GP_BASE+GP_POOL:gp_next());break;
    default: put32(gp_host(gp_group(gp_next()%GP_NGROUPS)+0x14),gp_next()%2?0:gp_trigger(gp_next()%GP_NTRIGGERS));break;
    }
}
/* Stub results after GP_LIMIT placement candidates accept, so the
 * original's unbounded retry loop ends. */
__attribute__((used,noinline)) static uint64_t __cdecl gp_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(gp_calls==GP_MAX){fail("groups synthetic call overflow");ExitProcess(1);}
    gp_record *v=gp_log+gp_calls++;memset(v,0,sizeof(*v));v->id=id;v->slots=weapons_bridge_slots(a,gp_counts[id]);memcpy(v->args,a,4*gp_counts[id]);
    if(!gp_real)memcpy(v->regs,regs,12);
    for(unsigned j=0;j<3;++j)if(gp_stack(v->regs[j]))v->regs[j]=0;
    for(unsigned j=0;j<gp_counts[id];++j)if(gp_stack(v->args[j]))v->args[j]=0;
    uint32_t eax=gp_next(),edx=gp_next(),pick=gp_next();gp_ecx=gp_next();
    int done=gp_iter>=GP_LIMIT;
    switch(id){
    case 0: __asm__ volatile("movl %%fs:0,%0":"=r"(eax));break;   /* 49630c: the real FS:0 head */
    case 1: { static const uint16_t lows[]={0,0x1fff,0x2000,0x3fff,0x4000,0x7fff,0x8000,0xffff,0x0123};
        if(pick%3)eax=(eax&0xffff0000u)|lows[(pick>>2)%9];break; }
    case 2: /* spawn_point: a placement into the caller's frame */
        for(unsigned j=0;j<12;j+=4)put32(gp_host(a[0]+j),pick%4?gp_next()%400001u-200000u:gp_next());
        if(!gp_real&&pick%3==0)gp_shake(gp_next());break;
    case 3: eax=pick%3?GP_BASE+GP_OBJECT:0;if(!gp_real&&pick%5==0)gp_shake(gp_next());break;
    case 4: for(unsigned j=0;j<3;++j)v->input[j]=get32(gp_host(a[1]+4*j));
        if(pick%3==0)put32(gp_arena+GP_COUNTER,get32(gp_arena+GP_COUNTER)+gp_next()%16u);
        if(!gp_real&&pick%4==0)gp_shake(gp_next());break;
    case 5: for(unsigned j=0;j<8;j+=4)put32(gp_host(a[1]+j),gp_next());break;
    case 6: ++gp_iter;for(unsigned j=0;j<3;++j)v->input[j]=get32(gp_host(a[0]+4*j));
        v->input[3]=get32(gp_host(a[1]));v->input[4]=get32(gp_host(a[1]+4));v->input[5]=get32(gp_host(a[2]+8));
        put32(gp_host(a[2]),gp_next()%200001u-100000u);put32(gp_host(a[2]+4),pick%7?gp_next()%200001u-100000u:gp_next());
        if(!gp_real&&pick%3==0)gp_shake(gp_next());break;
    case 7: for(unsigned j=0;j<3;++j)v->input[j]=get32(gp_host(a[1]+4*j));
        { uint32_t bound=(uint32_t)(int16_t)gp_radius+5000u;
          eax=done?0x7fffffffu:pick%4==0?eax:bound+(uint32_t)((int)(pick>>2)%3-1); }
        if(!gp_real&&pick%7==0)gp_shake(gp_next());break;
    case 8: for(unsigned j=0;j<3;++j)v->input[j]=get32(gp_host(a[0]+4*j));
        if(gp_real){eax=gp_rejects?(--gp_rejects,(eax&0xffff0000u)|1u):eax&0xffff0000u;break;}
        eax=done||pick%2?eax&0xffff0000u:(eax&0xffff0000u)|(pick%3?1u:0x8000u|gp_next());break;
    case 9: v->input[0]=get32(gp_host(a[1]));v->input[1]=get16(gp_host(a[1]+4));
        for(unsigned j=0;j<3;++j)v->input[2+j]=get32(gp_host(a[2]+4*j));
        eax=done||pick%2?eax&0xffffff00u:(eax&0xffffff00u)|(1u+gp_next()%255u);break;
    case 10: eax=gp_flag(pick>>3);if(!gp_real&&pick%3==0)gp_shake(gp_next());
        if(!gp_real&&pick%5==0)put16(gp_host(gp_trigger(gp_next()%GP_NTRIGGERS)+0xa),(uint16_t)(gp_next()%2));break;
    case 11: case 12: if(!gp_real&&pick%4==0)gp_shake(gp_next());break;
    case 14: for(unsigned j=0;j<3;++j){v->input[j]=get32(gp_host(a[1]+4*j));put32(gp_host(a[1]+4*j),gp_next()%400001u-200000u);}
        if(!gp_real&&pick%4==0)gp_shake(gp_next());break;
    case 15: { static const uint16_t radii[]={0,0,1,300,5000,0x7fff,0x8000,0xffff,0xec78,0xec77};
        eax=(eax&0xffff0000u)|radii[pick%10];
        if(gp_real)eax=gp_radius_calls++<6&&pick%2?(eax&0xffff0000u)|(uint16_t)(2000u+gp_next()%60000u):eax&0xffff0000u;
        if(done)eax&=0xffff0000u;gp_radius=eax;
        if(!gp_real&&pick%9==0)gp_shake(gp_next());break; }
    case 16: { static const uint16_t levels[]={0,0x31,0x32,0x7f,0x80,0xbf,0xc0,0xff};eax=(eax&0xffff0000u)|levels[pick%8];break; }
    default: break;
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
/* Records the entry EAX/EDX/ECX before the slot pointer. */
#define GP_STUB(n) __attribute__((naked)) static void gp_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _gp_stub_impl\n\taddl $24,%esp\n\tmovl _gp_ecx,%ecx\n\tret\n\t");}
GP_STUB(1) GP_STUB(2) GP_STUB(3) GP_STUB(4) GP_STUB(5) GP_STUB(6) GP_STUB(7) GP_STUB(8) GP_STUB(9) GP_STUB(10)
GP_STUB(11) GP_STUB(12) GP_STUB(13) GP_STUB(14) GP_STUB(15) GP_STUB(16)
#undef GP_STUB
/* 49630c: the runtime frame setup. Captures the entry registers like the
 * other stubs (gp_stub_impl returns the real FS:0 head for id 0), then links
 * a record at EBP + [descriptor+4] into FS:0 with the saved head at record+0
 * and a zero context word at record+16; EDX comes back unchanged. */
__attribute__((naked)) static void gp_stub_0(void){__asm__ volatile(
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
    "pushl $0\n\tcalll _gp_stub_impl\n\taddl $12,%esp\n\t"
    "popl %ecx\n\tmovl 4(%ecx),%ecx\n\taddl %ebp,%ecx\n\tmovl %eax,(%ecx)\n\tmovw $0,16(%ecx)\n\tmovl %ecx,%fs:0\n\t"
    "popl %edx\n\taddl $4,%esp\n\tmovl _gp_ecx,%ecx\n\tret\n\t");}
static void *gp_stubs[GP_IDS]={gp_stub_0,gp_stub_1,gp_stub_2,gp_stub_3,gp_stub_4,gp_stub_5,gp_stub_6,gp_stub_7,gp_stub_8,
    gp_stub_9,gp_stub_10,gp_stub_11,gp_stub_12,gp_stub_13,gp_stub_14,gp_stub_15,gp_stub_16};
/* Globals: terrain map 4a0be0, meteor pool 4a96aa, actor list 4a9af8 and
 * count 4a9afe, groups 4a9e00/4a9e04 (rivals_done), RNG stream A 4d2599,
 * player 4d24e6, construction count pointer 6cbee4. */
static const struct { uint32_t rva,size; } gp_ranges[]={{0xa0be0,4},{0xa96aa,4},{0xa9af8,8},{0xa9e00,6},{0xd2599,114},
    {0xd24e6,4},{0x2cbee4,4}};
static void gp_globals(BYTE *out,int restore){
    unsigned at=0;
    for(unsigned i=0;i<sizeof(gp_ranges)/sizeof(gp_ranges[0]);++i){
        if(restore)memcpy(mapped+gp_ranges[i].rva,out+at,gp_ranges[i].size);else memcpy(out+at,mapped+gp_ranges[i].rva,gp_ranges[i].size);
        at+=gp_ranges[i].size;
    }
}
static void gp_reset(void){gp_calls=0;gp_stream=gp_seed;gp_iter=0;gp_radius=0;gp_radius_calls=0;}
/* Real cases sit near the centre (40000h, 40000h) of the terrain grid. */
static uint32_t gp_coordinate(int real){
    return real?(uint32_t)(0x40000+(int32_t)(random32()%16001u)-8000):(uint32_t)((int32_t)(random32()%400001u)-200000);
}
/* A real pool of `count` free nodes (0x28 apart, object at +8). */
static void gp_pool(unsigned count){
    uint32_t pool=GP_BASE+GP_POOL,nodes=GP_BASE+GP_NODES;
    put16(gp_host(pool),(uint16_t)count);put16(gp_host(pool)+2,0);put32(gp_host(pool)+0xe,0);
    for(unsigned i=0;i<count;++i){uint32_t n=nodes+0x28u*i;put32(gp_host(n),i+1<count?n+0x28u:0);put32(gp_host(n)+4,i?n-0x28u:0);}
    put32(gp_host(pool)+0xa,count?nodes:0);
}
/* A flat 8 x 8 terrain grid of 65536-unit cells (normal Z words 1000h on
 * both triangles, flags 0..2) for the real face_test: every placement
 * within 40000h of the grid centre is accepted, which covers the 150000
 * placements of group_activate and their retries. */
static void gp_map(void){
    BYTE *map=gp_arena+GP_MAP;
    put32(map+0x100,3);put32(map+0x104,3);put32(map+0x108,16);put32(map+0xec,GP_BASE+GP_CELLS);
    for(unsigned j=0;j<64;++j){BYTE *c=gp_arena+GP_CELLS+16*j;put16(c+5,0x1000);put16(c+11,0x1000);
        c[15]=(BYTE)((c[15]&0xfcu)|random32()%3);}
}
static void gp_build(unsigned index,uint32_t a[6],uint32_t r[3],int real){
    uint32_t b=GP_BASE;gp_seed=random32();memset(a,0,24);
    random_bytes(gp_arena,GP_SIZE);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    /* Vtable for every actor: +40h, +78h, +7Ch stubs. */
    put32(gp_arena+GP_VTABLE+0x40,(uint32_t)(uintptr_t)gp_stubs[16]);
    put32(gp_arena+GP_VTABLE+0x78,(uint32_t)(uintptr_t)gp_stubs[14]);put32(gp_arena+GP_VTABLE+0x7c,(uint32_t)(uintptr_t)gp_stubs[15]);
    static const uint32_t modes[]={2,3,4,5,0,1,6,0xffff,2,3,4,5};
    for(unsigned k=0;k<GP_NTRIGGERS;++k){BYTE *t=gp_host(gp_trigger(k));
        put16(t+2,(uint16_t)(real?gp_pick(modes,6):random32()%8?gp_pick(modes,12):random32()));
        static const uint32_t latches[]={0,1,1,0x100,0xffff,1};put16(t+0xa,(uint16_t)(real?random32()%6!=0:gp_pick(latches,6)));}
    for(unsigned k=0;k<GP_NROUTES;++k){put16(gp_host(gp_route(k)),(uint16_t)(1+random32()%6));
        put32(gp_host(gp_route(k))+2,b+GP_POINTERS+0x20*k);
        for(unsigned j=0;j<8;++j)put32(gp_host(b+GP_POINTERS+0x20*k+4*j),real?b+GP_OUT+12*(random32()%2):random32());}
    for(unsigned k=0;k<GP_NACTORS;++k){BYTE *o=gp_host(gp_actor(k));
        put32(o,b+GP_VTABLE);put32(o+0x26,gp_coordinate(real));put32(o+0x2a,gp_coordinate(real));put32(o+0x2e,random32()%4001u-2000u);
        put32(o+0x45,gp_group(random32()%GP_NGROUPS));
        static const uint32_t states[]={3,5,6,6,0,1,2,4,7,0xffff,0x8003};put16(o+0x23e,(uint16_t)gp_pick(states,11));
        put32(o+0x248,gp_actor(random32()%GP_NACTORS));put32(o+0x24c,random32()%3?gp_actor(random32()%GP_NACTORS):0);
        if(real){o[0x99]=random32()%6==0;o[0xa4]=random32()%8==0;o[0xa5]=random32()%8==0;o[0x92+0x13]=random32()%4==0;
            o[0x92+0x12]=random32()%4==0;o[0x92+0x22]=random32()%3==0;}
    }
    static const uint32_t scopes[]={0,1,2,3,0,1,4,0xffff,0x8000,5,7};
    for(unsigned k=0;k<GP_NORDERS;++k){BYTE *o=gp_host(gp_order(k));
        static const uint32_t types[]={0,2,3,5,6,1,4,7};
        uint32_t scope=real?random32()%4:gp_pick(scopes,11);
        put16(o,(uint16_t)gp_pick(types,8));put16(o+0xc,(uint16_t)scope);
        put32(o+0xe,scope==0?gp_group(random32()%GP_NGROUPS):real||random32()%4?gp_actor(random32()%GP_NACTORS):random32());
        put32(o+0x12,random32()%3?gp_trigger(random32()%GP_NTRIGGERS):0);
    }
    for(unsigned k=0;k<GP_NGROUPS;++k){BYTE *g=gp_host(gp_group(k));
        put32(g+6,gp_route(random32()%GP_NROUTES));put16(g+4,(uint16_t)(random32()%4));
        put32(g+0xc,gp_members(k));
        static const uint32_t counts[]={0,1,2,3,4,8,1,2,0xffff,0x8000};
        put16(g+0x10,(uint16_t)(real?1+random32()%4:gp_pick(counts,10)));
        g[0x12]=(BYTE)(random32()%3);g[0x13]=(BYTE)(random32()%3?0:random32()%2?1:0x80);
        put32(g+0x14,random32()%4?gp_trigger(random32()%GP_NTRIGGERS):0);
        for(unsigned j=0;j<10;++j)put32(g+0x44+4*j,random32()%4==0?0:gp_order(random32()%GP_NORDERS));
        /* Indices 0..9, 9..12 (no advance), -1 (slot at +40h). */
        static const uint32_t indexes[]={0,1,2,3,4,5,6,7,8,9,9,0xffffffffu};
        put32(g+0x40,random32()%3?gp_order(random32()%GP_NORDERS):0);
        put32(g+0x6c,real?random32()%10:gp_pick(indexes,12));
        /* The leaves never write through the index: 4*index wraps. */
        if(!real&&(index==ES2_HOOK_INDEX_order_actor||index==ES2_HOOK_INDEX_order_point)&&random32()%4==0)
            put32(g+0x6c,get32(g+0x6c)+0x40000000u*(1u+random32()%3));
        if(real)put32(g+0x44+4*get32(g+0x6c),gp_order(random32()%GP_NORDERS));
        for(unsigned j=0;j<8;++j)put32(gp_host(gp_members(k)+4*j),gp_actor(random32()%GP_NACTORS));
    }
    for(unsigned j=0;j<GP_NLIST;++j)put32(gp_arena+GP_LIST+4*j,!real&&random32()%6==0?0:gp_actor(random32()%GP_NACTORS));
    put32(mapped+0xa9af8,b+GP_LIST);
    put16(mapped+0xa9afe,(uint16_t)(real?random32()%(GP_NLIST+1u):random32()%8?random32()%(GP_NLIST+1u):random32()%2?0xffffu:0x8000u));
    put32(mapped+0xd24e6,gp_actor(random32()%GP_NACTORS));
    put32(mapped+0xa0be0,real?b+GP_MAP:random32());put32(mapped+0xa96aa,b+GP_POOL);
    put32(mapped+0x2cbee4,b+GP_COUNTER);
    put32(mapped+0xa9e00,gp_group(0));put16(mapped+0xa9e04,GP_NGROUPS);
    random_bytes(mapped+0xd2599,112);mapped[0xd2599+112]=(BYTE)(random32()%56);mapped[0xd2599+113]=(BYTE)(random32()%56);
    if(real){
        gp_map();gp_pool(random32()%4);
        /* Every actor on its own group's member list (412c8c divides by the
         * count), the player near the grid centre. */
        put32(gp_host(get32(mapped+0xd24e6))+0x26,gp_coordinate(1));put32(gp_host(get32(mapped+0xd24e6))+0x2a,gp_coordinate(1));
    }
    /* Arguments. */
    switch(index){
    case ES2_HOOK_INDEX_group_activate: { a[0]=gp_group(random32()%GP_NGROUPS);
        if(!get32(gp_host(a[0]+0x14)))put32(gp_host(a[0]+0x14),gp_trigger(random32()%GP_NTRIGGERS));
        if(real)gp_host(a[0])[0x13]=(BYTE)(random32()%5==0);break; }
    case ES2_HOOK_INDEX_spawn_point: {
        static const uint32_t distances[]={0,1,2000,90000,150000,0xfffff830u,0x7ffff000u,0x80000000u};
        static const uint32_t checks[]={0,1,0x10000,0x80000000u,1,1};
        a[0]=random32()%8?b+GP_OUT:get32(mapped+0xd24e6)+0x26u;
        a[1]=real?random32()%20001u:random32()%4?gp_pick(distances,8):random32();
        a[2]=random32();a[3]=real?random32()%2:gp_pick(checks,6);
        if(real)a[0]=b+GP_OUT;break; }
    case ES2_HOOK_INDEX_actor_destination: a[0]=gp_actor(random32()%GP_NACTORS);break;
    default: a[0]=gp_group(random32()%GP_NGROUPS);break;
    }
    gp_subject=a[0];gp_rejects=random32()%4;
}
static int gp_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const gp_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,gp_calls);
    for(unsigned j=0;j<calls&&j<gp_calls&&expected;++j)if(memcmp(expected+j,gp_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(gp_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<GP_SIZE;++j)if(gp_after[j]!=gp_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,gp_after[j],gp_arena[j]);break;}
    return 1;
}
static void gp_global_diff(const BYTE *expected,const BYTE *actual){
    for(unsigned j=0;j<GP_GLOBALS;++j)if(expected[j]!=actual[j]){fprintf(stderr," global byte %x %02x/%02x\n",j,expected[j],actual[j]);break;}
}
static unsigned gp_expected_hits(const callee_counter *c){
    static const unsigned slots[GP_IDS]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x78,0x7c,0x40};
    unsigned hits=0;
    for(unsigned j=0;j<gp_calls;++j){unsigned id=gp_log[j].id;
        if(c->event==ES2_COLLIDE_CALL?gp_addresses[id]==c->key:slots[id]==c->key)++hits;}
    return hits;
}
/* Calls the original with four slots, the chosen EAX/EDX/ECX and ESI = 0. */
__attribute__((naked)) static void gp_machine(void *target,const uint32_t *a,const uint32_t *registers,uint32_t pair[2]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl 12(%ebp),%esi\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\txorl %esi,%esi\n\t"
        "call *8(%ebp)\n\tmovl 20(%ebp),%ecx\n\tmovl %eax,(%ecx)\n\tmovl %edx,4(%ecx)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* A four-slot caller over 3840 bytes of patterned stack, EAX = EDX = ECX =
 * the pattern, ESI = 0. */
__attribute__((naked)) static void gp_poison_call(void *target,const uint32_t *a,uint32_t pattern,uint32_t pair[2]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 16(%ebp),%eax\n\tmovl $1024,%ecx\n\trep stosl\n\taddl $3840,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%eax\n\tmovl %eax,%edx\n\tmovl %eax,%ecx\n\txorl %esi,%esi\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%ecx\n\tmovl %eax,(%ecx)\n\tmovl %edx,4(%ecx)\n\tleal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static int gp_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    static BYTE before[GP_GLOBALS],after[GP_GLOBALS],actual[GP_GLOBALS];static gp_record expected[GP_MAX];
    memcpy(gp_before,gp_arena,GP_SIZE);gp_globals(before,0);
    uint32_t got[2];gp_reset();gp_machine(mapped+d->rva,a,r,got);
    uint64_t pair[2];pair[0]=(uint64_t)got[1]<<32|got[0];unsigned calls=gp_calls;memcpy(expected,gp_log,calls*sizeof(*expected));
    memcpy(gp_after,gp_arena,GP_SIZE);gp_globals(after,0);
    memcpy(gp_arena,gp_before,GP_SIZE);gp_globals(before,1);gp_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);gp_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=gp_expected_hits(c+i))return fail("groups callee counter delta");
    if(pair[0]!=pair[1]||calls!=gp_calls||memcmp(expected,gp_log,calls*sizeof(*expected))||memcmp(gp_after,gp_arena,GP_SIZE)||memcmp(after,actual,GP_GLOBALS)){
        gp_global_diff(after,actual);return gp_report("direct",n,pair,calls,expected);
    }
    return 0;
}
static unsigned gp_which(unsigned index,int real){
    switch(index){
    case ES2_HOOK_INDEX_group_activate:return real?0x110u:0x1fu;   /* real: 409b44, 404ae4 */
    case ES2_HOOK_INDEX_spawn_point:return real?0x100u:0x3e0u;     /* real: 404ae4 */
    case ES2_HOOK_INDEX_order_update:return real?0x800u:0xc00u;    /* real: 411cec */
    case ES2_HOOK_INDEX_group_assess:return 0x1000u;
    case ES2_HOOK_INDEX_actor_destination:return real?0:0x2000u;
    default:return 0;
    }
}
static int gp_patch(BYTE saved[GP_IDS][5],int active[GP_IDS],unsigned which,int on){
    for(unsigned i=0;i<GP_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!gp_addresses[i])continue;if(actor_patch(gp_addresses[i]-0x400000u,gp_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(gp_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static int gp_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;gp_build(index,a,r,0);
        static BYTE before[GP_GLOBALS],after[GP_GLOBALS],actual[GP_GLOBALS];BYTE expected_fpu[108],actual_fpu[108];static gp_record expected[GP_MAX];
        memcpy(gp_before,gp_arena,GP_SIZE);gp_globals(before,0);
        gp_reset();gp_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=gp_calls;memcpy(expected,gp_log,calls*sizeof(*expected));memcpy(gp_after,gp_arena,GP_SIZE);gp_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("groups poison install");
        memcpy(gp_arena,gp_before,GP_SIZE);gp_globals(before,1);gp_reset();gp_poison_call(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);gp_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=gp_calls||memcmp(expected,gp_log,calls*sizeof(*expected))||memcmp(gp_after,gp_arena,GP_SIZE)||
           memcmp(after,actual,GP_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1){
            gp_global_diff(after,actual);status=gp_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        }
        if(!es2_hook_uninstall(&h))return fail("groups poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state\n",d->name,total);
    return status;
}
/* Real callees with the nested hooks in rotating original/replaced/compare
 * modes (compare falls back to replaced where refused) and a patterned
 * stack, against a run with the caller not installed. EDX is compared
 * unless both runs return a frame address. */
static int gp_real_pass(unsigned index){
    static const unsigned activate[]={ES2_HOOK_INDEX_rng_next,ES2_HOOK_INDEX_pool_alloc,ES2_HOOK_INDEX_spawn_point,
        ES2_HOOK_INDEX_dist_gate,ES2_HOOK_INDEX_face_test};
    static const unsigned spawn[]={ES2_HOOK_INDEX_dist_gate,ES2_HOOK_INDEX_face_test};
    static const unsigned update[]={ES2_HOOK_INDEX_order_done,ES2_HOOK_INDEX_route_point,ES2_HOOK_INDEX_subject_done,ES2_HOOK_INDEX_rivals_done};
    static const unsigned destination[]={ES2_HOOK_INDEX_order_point};
    const unsigned *nested;unsigned nested_count;
    switch(index){
    case ES2_HOOK_INDEX_group_activate:nested=activate;nested_count=5;break;
    case ES2_HOOK_INDEX_spawn_point:nested=spawn;nested_count=2;break;
    case ES2_HOOK_INDEX_order_update:nested=update;nested_count=4;break;
    default:nested=destination;nested_count=1;break;
    }
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0,stubs=0;uint64_t entered[8]={0};
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    BYTE saved[GP_IDS][5];int active[GP_IDS];if(gp_patch(saved,active,gp_which(index,1),1))return 1;
    gp_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<1024&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;gp_build(index,a,r,1);
        static BYTE before[GP_GLOBALS],after[GP_GLOBALS],actual[GP_GLOBALS];static gp_record expected[GP_MAX];
        memcpy(gp_before,gp_arena,GP_SIZE);gp_globals(before,0);uint32_t rejects=gp_rejects;
        es2_hook hk[8];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<nested_count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if((!e||e->compare_unsupported)&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("groups nested install");}
        gp_reset();gp_rejects=rejects;gp_poison_call(mapped+d->rva,a,pattern,expected_pair);
        unsigned calls=gp_calls;memcpy(expected,gp_log,calls*sizeof(*expected));stubs+=calls;
        memcpy(gp_after,gp_arena,GP_SIZE);gp_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+nested_count,d,&o,function_hook_extension(index)))return fail("groups caller install");
        memcpy(gp_arena,gp_before,GP_SIZE);gp_globals(before,1);gp_reset();gp_rejects=rejects;
        gp_poison_call(mapped+d->rva,a,pattern,actual_pair);gp_globals(actual,0);
        /* The real face_test returns slope_test's EDX, the address of
         * spawn_point's frame words: a frame address on both sides. */
        if(gp_stack(expected_pair[1])&&gp_stack(actual_pair[1]))expected_pair[1]=actual_pair[1];
        if(expected_pair[0]!=actual_pair[0]||expected_pair[1]!=actual_pair[1]||calls!=gp_calls||memcmp(expected,gp_log,calls*sizeof(*expected))||
           memcmp(gp_after,gp_arena,GP_SIZE)||memcmp(after,actual,GP_GLOBALS)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            gp_global_diff(after,actual);fprintf(stderr,"real mode=%u\n",mode);status=gp_report("real",n,pair,calls,expected);
        }
        for(unsigned j=0;j<=nested_count;++j){if(j<nested_count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("groups nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("groups nested uninstall");}
        gp_globals(before,1);++total;
    }
    gp_real=0;
    if(gp_patch(saved,active,0,0))return 1;
    for(unsigned j=0;j<nested_count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    for(unsigned i=0;i<count&&!status;++i)if(c[i].count==counts[i]){fprintf(stderr,"FAIL: %s real pass never called %s\n",d->name,c[i].name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, %u stub calls, 31/a7 stack, full pair, trace and memory\n",d->name,total,nested_count,stubs);
    return status;
}
static int groups_cases(unsigned index,unsigned limit){
    unsigned which=gp_which(index,0);gp_index=index;
    BYTE saved[GP_IDS][5];int active[GP_IDS];static BYTE old_globals[GP_GLOBALS],old_arena[GP_SIZE];
    gp_globals(old_globals,0);memcpy(old_arena,gp_arena,GP_SIZE);gp_real=0;
    if(gp_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_GROUPS_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];gp_build(index,a,r,0);status=gp_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=gp_installed_probes(index,64);
    if(gp_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status&&index!=ES2_HOOK_INDEX_group_assess&&index!=ES2_HOOK_INDEX_order_actor&&index!=ES2_HOOK_INDEX_order_point)
        status=gp_real_pass(index);
    gp_globals(old_globals,1);memcpy(gp_arena,old_arena,GP_SIZE);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers and frame buffers, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
