/* Objectives (mission_runtime.md, "Objectives, route completion and outcome
 * policy"): objective_eval 413280, actor_safe 4137b4, target_known 411c58,
 * order_target 423918, order_done 4239fc, subject_done 412e58, rivals_done
 * 412e74, route_point 423b0c, outcome_poll 4131ac, status_eval 413180,
 * bounds_check 41379c, bounds_outside 41373c. Direct cases: every game callee
 * is a logging stub that records its ordered arguments and entry registers
 * (stack addresses masked) and changes the objective, actor, group, order and
 * outcome state between calls, so local copies and re-reads are checked. The
 * original runs from an own caller that also sets ESI (413280 reads the
 * caller's ESI). Then installed 31/a7 poison cases, and a real-callee pass: the
 * group's hooks, dist_gate and timer_word_step nested in rotating modes over
 * real status helpers (41324c, 413920, 413950, 412d90, 412c58, 412dc4, 412be4,
 * 4135e8); only the actors' virtual +40h and the cockpit message slot are
 * stubs. */
typedef struct mo_record { uint32_t id,args[3],regs[3],eax,edx; } mo_record;
enum { MO_SIZE=0x2000, MO_ACTORS=0x0000, MO_GROUPS=0x1000, MO_ORDERS=0x1300, MO_ROUTES=0x1420, MO_POINTERS=0x1440,
    MO_POINTS=0x14c0, MO_ARRAYS=0x1580, MO_OBJECTIVES=0x1600, MO_LIST=0x1880, MO_VTABLE=0x1900, MO_HOLDER=0x1a00,
    MO_COCKPIT=0x1c00, MO_POSITION=0x1d00, MO_MAX=192, MO_IDS=21, MO_NACTORS=8, MO_NGROUPS=6, MO_NORDERS=12,
    MO_NROUTES=4, MO_NPOINTS=16, MO_NOBJECTIVES=8, MO_NLIST=10, MO_GLOBALS=0x900 };
#define MO_BASE ((uint32_t)(uintptr_t)mo_arena)
static BYTE mo_arena[MO_SIZE],mo_before[MO_SIZE],mo_after[MO_SIZE];
static mo_record mo_log[MO_MAX];
static unsigned mo_calls,mo_index;
static int mo_real;
static uint32_t mo_seed,mo_stream,mo_subject;
__attribute__((used)) static uint32_t mo_ecx;
/* Patched callees (ids 0..18) and the two virtual slots reached through the
 * generated vtables (19: actor +40h, 20: cockpit message slot 0). */
static const uint32_t mo_addresses[MO_IDS]={0x41324c,0x413920,0x413950,0x412d90,0x412c58,0x4137b4,0x411c58,0x423918,0x423b0c,
    0x49288c,0x412dc4,0x412be4,0x412e58,0x412e74,0x467a48,0x41379c,0x413180,0x4135e8,0x41373c,0,0};
static const unsigned mo_counts[MO_IDS]={2,1,1,1,1,1,2,2,2,2,1,1,1,1,1,1,2,3,2,1,2};
static uint32_t mo_next(void){
    uint32_t z=mo_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *mo_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
/* The worker's 16 MiB stack window: frame addresses are masked identically. */
static int mo_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static uint32_t mo_actor(unsigned k){return MO_BASE+MO_ACTORS+0x200u*k;}
static uint32_t mo_group(unsigned k){return MO_BASE+MO_GROUPS+0x7au*k;}
static uint32_t mo_order(unsigned k){return MO_BASE+MO_ORDERS+0x16u*k;}
static uint32_t mo_objective(unsigned k){return MO_BASE+MO_OBJECTIVES+0x4cu*k;}
static uint32_t mo_route(unsigned k){return MO_BASE+MO_ROUTES+8u*k;}
/* Predicate results: zero, one, the sign bit, a nonzero high word over a
 * zero AX (AX tests and full-EAX tests disagree), or anything. */
static uint32_t mo_flag(uint32_t pick){
    switch(pick%8){case 0:case 1:case 2:return 0;case 3:return 1;case 4:return 0x8000u;
        case 5:return 0x10000u|(mo_next()&0xffff0000u);case 6:return (mo_next()&0xffff0000u)|1u;default:return mo_next();}
}
/* Statuses of 413180/4135e8: the documented values, -1, the stored status,
 * or anything (the table index is the sign-extended AX). */
static uint32_t mo_status(uint32_t pick){
    static const uint16_t values[]={2,3,4,5,6,7,8,9,10,0,1,0xffff,11,12};
    uint32_t v=(mo_next()&0xffff0000u)|values[pick%14];
    if(pick%9==0)v=(v&0xffff0000u)|get16(mapped+0xa9e4c);
    if(pick%23==0)v=mo_next();
    return v;
}
static uint16_t mo_id(void){
    uint32_t k=random32()%20;
    return (uint16_t)(k<8?0xffffu:k<9?(0x8000u|random32()):k<17?random32()%16u:random32()%1000u);
}
__attribute__((used,noinline)) static uint64_t __cdecl mo_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(mo_calls==MO_MAX){fail("objectives synthetic call overflow");ExitProcess(1);}
    mo_record *v=mo_log+mo_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,4*mo_counts[id]);
    if(!mo_real)memcpy(v->regs,regs,12);
    for(unsigned j=0;j<3;++j)if(mo_stack(v->regs[j]))v->regs[j]=0;
    for(unsigned j=0;j<mo_counts[id];++j)if(mo_stack(v->args[j]))v->args[j]=0;
    /* 413280's entry ECX reaches 4137b4 only for an empty table; the entry
     * shim replaces it by the caller's ESI and 4137b4 never reads it. */
    if(mo_index==ES2_HOOK_INDEX_objective_eval&&id==5&&mo_calls==1&&(int16_t)get16(mapped+0xa9e3c)<=0)v->regs[2]=0;
    uint32_t eax=mo_next(),edx=mo_next(),pick=mo_next();mo_ecx=mo_next();
    switch(id){
    case 0: case 1: case 2: case 3: case 4: case 5: /* objective predicates */
        eax=mo_flag(pick>>3);
        if(mo_real)break;
        if(pick%7==0)put16(mapped+0xa9e3c,(uint16_t)(mo_next()%(MO_NOBJECTIVES+1u)));
        if(pick%11==0)put16(mo_host(mo_objective(mo_next()%MO_NOBJECTIVES)+0x22),(uint16_t)(mo_next()%3?0:1));
        if(pick%13==0)put16(mo_host(mo_objective(mo_next()%MO_NOBJECTIVES)),(uint16_t)(mo_next()%3));
        if(pick%17==0){BYTE *b=mo_host(mo_objective(mo_next()%MO_NOBJECTIVES));unsigned k=mo_next()%10;
            put16(b+0x24+2*k,(uint16_t)(mo_next()%3?mo_next()%16u:0xffffu));put16(b+0x38+2*k,(uint16_t)(4+mo_next()%4));}
        if(pick%19==0)put32(mapped+0xa9e40,mo_objective(mo_next()%2));
        break;
    case 6: case 7: case 8: /* actor_safe callees */
        eax=id==8?(pick%3?0:mo_actor(mo_next()%MO_NACTORS)):mo_flag(pick>>3);
        if(mo_real)break;
        if(pick%7==0)put32(mo_host(mo_actor(mo_next()%MO_NACTORS)+0x1a4),mo_next()%2?mo_subject:mo_actor(mo_next()%MO_NACTORS));
        if(pick%11==0)put32(mo_host(mo_actor(mo_next()%MO_NACTORS)+0x45),mo_group(mo_next()%MO_NGROUPS));
        if(pick%13==0)put16(mapped+0xa9afe,(uint16_t)(mo_next()%(MO_NLIST+1u)));
        if(pick%17==0)put16(mo_host(mo_group(mo_next()%MO_NGROUPS)+4),(uint16_t)(mo_next()%6));
        if(pick%19==0)put32(mo_arena+MO_LIST+4*(mo_next()%MO_NLIST),mo_next()%4?mo_actor(mo_next()%MO_NACTORS):0);
        break;
    case 9: { /* 49288c: distances around 80000, 100000 and 1000000 */
        static const uint32_t thresholds[]={80000,100000,1000000};
        switch(pick%6){case 0:case 1:case 2:eax=thresholds[pick%3]+(uint32_t)((int)(mo_next()%3)-1);break;
            case 3:eax=mo_next()%2000000u;break;case 4:eax=0u-mo_next()%1000u;break;default:break;}
        if(!mo_real&&pick%13==0)put16(mapped+0xa9afe,(uint16_t)(mo_next()%(MO_NLIST+1u)));
        break; }
    case 10: { static const uint32_t s[]={3,4,5,0,0x10004,1,2}; /* 412dc4: status around 4 */
        eax=(pick%11==0)?mo_next():(mo_next()&0xffff0000u)|s[pick%7];if(pick%5==0)eax=s[pick%7];break; }
    case 11: /* 412be4: may retarget orders and regroup between calls */
        eax=mo_flag(pick>>3);
        if(mo_real)break;
        if(pick%5==0)put32(mo_host(mo_order(mo_next()%MO_NORDERS)+0xe),mo_next()%2?mo_subject:mo_actor(mo_next()%MO_NACTORS));
        if(pick%7==0)mo_host(mo_group(mo_next()%MO_NGROUPS))[0x12]=(BYTE)(mo_next()%3);
        if(pick%11==0)put16(mapped+0xa9e04,(uint16_t)(mo_next()%(MO_NGROUPS+1u)));
        if(pick%13==0)put32(mo_host(mo_group(mo_next()%MO_NGROUPS)+0x6c),mo_next()%10);
        break;
    case 12: case 13: case 15: case 18: eax=mo_flag(pick>>3);break;
    case 14: case 16: case 17: /* timer, status: may change the outcome globals */
        eax=id==14?(pick%3?0:mo_next()):mo_status(pick>>4);
        if(mo_real)break;
        if(pick%5==0)put16(mapped+0xa9e4c,(uint16_t)(mo_next()%3?mo_status(mo_next()):eax));
        if(pick%7==0)put16(mapped+0xa9e66,(uint16_t)(mo_next()%2?0:mo_next()));
        if(pick%11==0)put16(mapped+0xa9e68,(uint16_t)(mo_next()%2));
        break;
    case 19: eax=(mo_next()&0xffff0000u)|(mo_next()%300u);break; /* actor +40h */
    case 20: { const BYTE *p=mo_host(a[1]); /* cockpit message: the six written record bytes */
        v->regs[0]=get32(p);v->regs[1]=get16(p+4);break; }
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define MO_STUB(n) __attribute__((naked)) static void mo_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _mo_stub_impl\n\taddl $24,%esp\n\tmovl _mo_ecx,%ecx\n\tret\n\t");}
MO_STUB(0) MO_STUB(1) MO_STUB(2) MO_STUB(3) MO_STUB(4) MO_STUB(5) MO_STUB(6) MO_STUB(7) MO_STUB(8) MO_STUB(9) MO_STUB(10)
MO_STUB(11) MO_STUB(12) MO_STUB(13) MO_STUB(14) MO_STUB(15) MO_STUB(16) MO_STUB(17) MO_STUB(18) MO_STUB(19) MO_STUB(20)
#undef MO_STUB
static void *mo_stubs[MO_IDS]={mo_stub_0,mo_stub_1,mo_stub_2,mo_stub_3,mo_stub_4,mo_stub_5,mo_stub_6,mo_stub_7,mo_stub_8,
    mo_stub_9,mo_stub_10,mo_stub_11,mo_stub_12,mo_stub_13,mo_stub_14,mo_stub_15,mo_stub_16,mo_stub_17,mo_stub_18,mo_stub_19,mo_stub_20};
/* Globals read or written by the twelve functions and the real callees:
 * status table and 4135e8's state (49935c..4993a3), cockpit 49b088, actor
 * list, 4135e8's timer, groups/objectives/outcome/variables/rectangle
 * (4a9e00..4aa64f), alert text 4d1e98, player, time scale. */
static const struct { uint32_t rva,size; } mo_ranges[]={{0x9935c,0x48},{0x9b088,4},{0xa9af8,8},{0xa9b68,4},{0xa9e00,0x850},
    {0xd1e98,4},{0xd24e6,4},{0xd3b64,2}};
static void mo_globals(BYTE *out,int restore){
    unsigned at=0;
    for(unsigned i=0;i<sizeof(mo_ranges)/sizeof(mo_ranges[0]);++i){
        if(restore)memcpy(mapped+mo_ranges[i].rva,out+at,mo_ranges[i].size);else memcpy(out+at,mapped+mo_ranges[i].rva,mo_ranges[i].size);
        at+=mo_ranges[i].size;
    }
}
static void mo_reset(void){mo_calls=0;mo_stream=mo_seed;}
static uint32_t mo_pick(const uint32_t *values,unsigned count){return values[random32()%count];}
/* A coordinate near an edge e (e-1, e, e+1) or anywhere in a small range. */
static uint32_t mo_near(uint32_t edge){return random32()%2?edge+random32()%3-1u:(uint32_t)((int32_t)(random32()%400001u)-200000);}
static void mo_build(unsigned index,uint32_t a[6],uint32_t r[3],uint32_t *esi,int real){
    uint32_t b=MO_BASE;mo_seed=random32();memset(a,0,24);
    for(unsigned i=0;i<MO_SIZE;i+=4)put32(mo_arena+i,random32());
    for(unsigned i=0;i<3;++i)r[i]=random32();
    *esi=random32()%2?0:random32()%4?1:random32();
    for(unsigned i=0;i<2000;i+=4)put32(mapped+0xa9e70+i,random32());
    /* Vtables: actor +40h and the cockpit's slot 0 are stubs. */
    put32(mo_arena+MO_VTABLE+0x40,(uint32_t)(uintptr_t)mo_stubs[19]);
    put32(mo_arena+MO_HOLDER+0x20b,b+MO_COCKPIT);put32(mo_arena+MO_COCKPIT,b+MO_COCKPIT+0x80);
    put32(mo_arena+MO_COCKPIT+0x80,(uint32_t)(uintptr_t)mo_stubs[20]);put32(mapped+0x9b088,b+MO_HOLDER);
    /* Routes: count word, pointer to eight point pointers (some null). */
    for(unsigned k=0;k<MO_NPOINTS;++k){BYTE *p=mo_arena+MO_POINTS+12*k;put32(p,random32()%400001u-200000u);put32(p+4,random32()%400001u-200000u);}
    for(unsigned k=0;k<MO_NROUTES;++k){
        static const uint32_t counts[]={0,1,2,3,5,8,8,0xffff,0x8000};
        put16(mo_arena+MO_ROUTES+8*k,(uint16_t)(real?1+random32()%8:mo_pick(counts,9)));put32(mo_arena+MO_ROUTES+8*k+2,b+MO_POINTERS+32*k);
        for(unsigned j=0;j<8;++j)put32(mo_arena+MO_POINTERS+32*k+4*j,random32()%6?b+MO_POINTS+12*(random32()%MO_NPOINTS):0);
    }
    /* Orders: type, route, scope, subject (a group for scope 0, else an actor). */
    for(unsigned k=0;k<MO_NORDERS;++k){BYTE *o=mo_arena+MO_ORDERS+0x16*k;
        static const uint32_t types[]={0,1,2,3,4,5,6,7,0,2,3,0xffff};static const uint32_t scopes[]={0xffff,0,1,2,3,4,0,1,0x8000};
        put16(o,(uint16_t)(random32()%16?mo_pick(types,12):random32()));
        put32(o+8,random32()%4?mo_route(random32()%MO_NROUTES):0);
        uint16_t scope=(uint16_t)mo_pick(scopes,9);put16(o+0xc,scope);
        put32(o+0xe,!real&&random32()%6==0?0:scope==0?mo_group(random32()%MO_NGROUPS):mo_actor(random32()%MO_NACTORS));
    }
    for(unsigned k=0;k<MO_NGROUPS;++k){BYTE *g=mo_arena+MO_GROUPS+0x7a*k;
        static const uint32_t points[]={0,1,2,3,4,6,0xffff};static const uint32_t categories[]={0,1,2,0xff,0,1};
        put16(g,(uint16_t)(random32()%3));put16(g+4,(uint16_t)mo_pick(points,7));
        put32(g+6,real||random32()%5?mo_route(random32()%MO_NROUTES):0);
        put32(g+0xc,b+MO_ARRAYS+16*k);put16(g+0x10,(uint16_t)(real?1+random32()%4:random32()%5));
        g[0x12]=(BYTE)mo_pick(categories,6);put32(g+0x14,random32()%5?0:random32()|1u);
        for(unsigned j=0;j<10;++j)put32(g+0x44+4*j,random32()%4?mo_order(random32()%MO_NORDERS):0);
        uint32_t current=random32()%10;put32(g+0x6c,current);
        if(real||random32()%4)put32(g+0x44+4*current,mo_order(random32()%MO_NORDERS));
        for(unsigned j=0;j<4;++j)put32(mo_arena+MO_ARRAYS+16*k+4*j,mo_actor(random32()%MO_NACTORS));
    }
    for(unsigned k=0;k<MO_NACTORS;++k){BYTE *o=mo_arena+MO_ACTORS+0x200*k;
        put32(o,b+MO_VTABLE);
        put32(o+0x26,random32()%400001u-200000u);put32(o+0x2a,random32()%400001u-200000u);put32(o+0x2e,random32()%2001u-1000u);
        put32(o+0x45,mo_group(random32()%MO_NGROUPS));put16(o+0x4b,(uint16_t)(random32()%128u-64u));
        static const unsigned bytes[]={0x95,0x99,0x9e,0xa0,0xa4,0xa5,0xb4};
        for(unsigned j=0;j<7;++j)o[bytes[j]]=(BYTE)(random32()%4?0:mo_pick((const uint32_t[]){1,0x80,0xff,2},4));
        if(real){o[0x99]=random32()%6==0;o[0xa4]=random32()%8==0;o[0xa5]=random32()%8==0;}
        for(unsigned j=0x82;j<0x102;++j)o[j]=(BYTE)(random32()%2?0:random32()|1u);
        put32(o+0x1a4,random32()%3?mo_actor(random32()%MO_NACTORS):random32());
    }
    /* Actor list (some null entries end the walk early). */
    for(unsigned k=0;k<MO_NLIST;++k)put32(mo_arena+MO_LIST+4*k,random32()%8?mo_actor(random32()%MO_NACTORS):0);
    put32(mapped+0xa9af8,b+MO_LIST);
    put16(mapped+0xa9afe,(uint16_t)(random32()%16?random32()%(MO_NLIST+1u):random32()%2?0:0x8000u|random32()));
    put32(mapped+0xd24e6,mo_actor(random32()%3?0:random32()%MO_NACTORS));
    /* Objectives: marker, type, scope, subject, route, latch, ten slots. */
    for(unsigned k=0;k<MO_NOBJECTIVES;++k){BYTE *o=mo_arena+MO_OBJECTIVES+0x4c*k;
        static const uint32_t markers[]={0,1,1,1,2,0xffff};static const uint32_t scopes[]={0,1,2,3,0,1,4,0xffff};
        put16(o,(uint16_t)mo_pick(markers,6));
        uint32_t type=random32()%11;if(random32()%12==0)type=random32()%3?11:random32();put16(o+2,(uint16_t)type);
        uint32_t scope=random32()%16?mo_pick(scopes,8):random32();put16(o+4,(uint16_t)scope);
        put32(o+6,(uint16_t)scope==0?mo_group(random32()%MO_NGROUPS):mo_actor(random32()%MO_NACTORS));
        put32(o+0xe,mo_route(random32()%MO_NROUTES));put16(o+0x22,(uint16_t)(random32()%5?0:1));
        for(unsigned j=0;j<10;++j){static const uint32_t actions[]={4,5,6,7,4,5,6,7,0,1,8,0xffff};
            put16(o+0x24+2*j,mo_id());put16(o+0x38+2*j,(uint16_t)(random32()%10?mo_pick(actions,12):random32()));}
    }
    put32(mapped+0xa9e40,b+MO_OBJECTIVES);
    put16(mapped+0xa9e3c,(uint16_t)(random32()%10?random32()%(MO_NOBJECTIVES+1u):random32()%2?0:0x8000u|random32()));
    put32(mapped+0xa9e00,random32()%32?b+MO_GROUPS:0);put16(mapped+0xa9e04,(uint16_t)(random32()%(MO_NGROUPS+1u)));
    /* Outcome state: status, timers (flag byte + countdown word), grace flag. */
    static const uint32_t statuses[]={0xffff,0,2,3,4,5,6,7,8,9,10};
    put16(mapped+0xa9e4c,(uint16_t)mo_pick(statuses,11));put16(mapped+0xa9e54,(uint16_t)random32());
    for(unsigned j=0;j<6;++j)mapped[0xa9e62+j]=(BYTE)random32();
    static const uint32_t countdowns[]={0,0,1,50,100,200,10000,0x8000};
    put16(mapped+0xa9e63,(uint16_t)mo_pick(countdowns,8));put16(mapped+0xa9e66,(uint16_t)mo_pick(countdowns,8));
    put16(mapped+0xa9e68,(uint16_t)(random32()%3?random32()%2:random32()));
    put16(mapped+0xd3b64,(uint16_t)(random32()%4?50+random32()%100:random32()%3?0:random32()));
    put16(mapped+0xa9b69,(uint16_t)mo_pick(countdowns,8));mapped[0xa9b68]=(BYTE)random32();
    mapped[0x99398]=(BYTE)(random32()%2);put32(mapped+0x9939c,random32()%11);put32(mapped+0x993a0,random32()%2?0xffffffffu:random32()%11);
    /* Map rectangle: x0 < x1, y0 < y1 within the actor range. */
    uint32_t x0=0u-random32()%200000u,y0=0u-random32()%200000u,x1=random32()%200000u,y1=random32()%200000u;
    put32(mapped+0xaa640,x0);put32(mapped+0xaa644,y0);put32(mapped+0xaa648,x1);put32(mapped+0xaa64c,y1);
    put32(mapped+0xd1e98,random32());
    /* Arguments. */
    uint32_t actor=mo_actor(random32()%MO_NACTORS),group=mo_group(random32()%MO_NGROUPS);
    switch(index){
    case ES2_HOOK_INDEX_objective_eval: case ES2_HOOK_INDEX_outcome_poll: a[0]=get32(mapped+0xd24e6);break;
    case ES2_HOOK_INDEX_actor_safe: case ES2_HOOK_INDEX_bounds_check: a[0]=random32()%3?actor:get32(mapped+0xd24e6);
        if(index==ES2_HOOK_INDEX_bounds_check&&random32()%2){BYTE *o=mo_host(a[0]);
            put32(o+0x26,mo_near(random32()%2?x0:x1));put32(o+0x2a,mo_near(random32()%2?y0:y1));}
        break;
    case ES2_HOOK_INDEX_target_known: a[0]=actor;a[1]=mo_actor(random32()%MO_NACTORS);break;
    case ES2_HOOK_INDEX_order_target: a[0]=group;a[1]=actor;break;
    case ES2_HOOK_INDEX_order_done: a[0]=group;a[1]=mo_order(random32()%MO_NORDERS);break;
    case ES2_HOOK_INDEX_subject_done: a[0]=group;break;
    case ES2_HOOK_INDEX_rivals_done: /* the caller's contract: a current order */
        a[0]=group;put32(mo_host(group+0x44+4*get32(mo_host(group+0x6c))),mo_order(random32()%MO_NORDERS));break;
    case ES2_HOOK_INDEX_route_point: a[0]=group+4;a[1]=(random32()&0xffff0000u)|(uint16_t)(random32()%26u-12u);break;
    case ES2_HOOK_INDEX_status_eval: a[0]=get32(mapped+0xd24e6);a[1]=mo_pick((const uint32_t[]){0,0,1,0x10000,0xffff,0x12340000},6);
        if(random32()%8==0)a[1]=random32();break;
    case ES2_HOOK_INDEX_bounds_outside: { static const uint32_t margins[]={0,0,110000,1,0xffffffff,0x80000000u};
        uint32_t m=random32()%8?mo_pick(margins,6):random32();a[0]=b+MO_POSITION;a[1]=m;
        put32(mo_arena+MO_POSITION,mo_near(random32()%2?x0-m:x1+m));put32(mo_arena+MO_POSITION+4,mo_near(random32()%2?y0-m:y1+m));break; }
    }
    mo_subject=a[0];
}
/* Calls the original with chosen EAX/EDX/ECX and ESI (413280 reads ESI). */
__attribute__((naked)) static void mo_machine(void *target,const uint32_t *a,const uint32_t *registers,uint32_t esi,uint32_t pair[2]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl 12(%ebp),%esi\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tmovl 20(%ebp),%esi\n\t"
        "call *8(%ebp)\n\tmovl 24(%ebp),%ecx\n\tmovl %eax,(%ecx)\n\tmovl %edx,4(%ecx)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static int mo_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const mo_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,mo_calls);
    for(unsigned j=0;j<calls&&j<mo_calls&&expected;++j)if(memcmp(expected+j,mo_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(mo_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<MO_SIZE;++j)if(mo_after[j]!=mo_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,mo_after[j],mo_arena[j]);break;}
    return 1;
}
static void mo_global_diff(const BYTE *expected,const BYTE *actual){
    for(unsigned j=0;j<MO_GLOBALS;++j)if(expected[j]!=actual[j]){fprintf(stderr," global byte %x %02x/%02x\n",j,expected[j],actual[j]);break;}
}
static unsigned mo_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<mo_calls;++j)if(c->event==ES2_COLLIDE_CALL&&mo_addresses[mo_log[j].id]==c->key)++hits;
    return hits;
}
static int mo_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r,uint32_t esi){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    static BYTE before[MO_GLOBALS],after[MO_GLOBALS],actual[MO_GLOBALS];static mo_record expected[MO_MAX];
    memcpy(mo_before,mo_arena,MO_SIZE);mo_globals(before,0);
    uint32_t got[2];mo_reset();mo_machine(mapped+d->rva,a,r,esi,got);
    uint64_t pair[2];pair[0]=(uint64_t)got[1]<<32|got[0];unsigned calls=mo_calls;memcpy(expected,mo_log,calls*sizeof(*expected));
    memcpy(mo_after,mo_arena,MO_SIZE);mo_globals(after,0);
    memcpy(mo_arena,mo_before,MO_SIZE);mo_globals(before,1);mo_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    /* objective_eval's adapter takes the caller's ESI in the ECX slot. */
    uint32_t entry[3]={r[0],r[1],index==ES2_HOOK_INDEX_objective_eval?esi:r[2]};
    pair[1]=x->replacement_context(a,d->user,entry);mo_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=mo_expected_hits(c+i))return fail("objectives callee counter delta");
    if(pair[0]!=pair[1]||calls!=mo_calls||memcmp(expected,mo_log,calls*sizeof(*expected))||memcmp(mo_after,mo_arena,MO_SIZE)||memcmp(after,actual,MO_GLOBALS)){
        mo_global_diff(after,actual);return mo_report("direct",n,pair,calls,expected);
    }
    return 0;
}
static unsigned mo_which(unsigned index){
    switch(index){
    case ES2_HOOK_INDEX_objective_eval:return 0x3fu;
    case ES2_HOOK_INDEX_actor_safe:return 0x3c0u;
    case ES2_HOOK_INDEX_target_known:return 0x200u;
    case ES2_HOOK_INDEX_order_done:return 0x3100u;
    case ES2_HOOK_INDEX_subject_done:return 0x400u;
    case ES2_HOOK_INDEX_rivals_done:return 0x800u;
    case ES2_HOOK_INDEX_outcome_poll:return 0x1c000u;
    case ES2_HOOK_INDEX_status_eval:return 0x20000u;
    case ES2_HOOK_INDEX_bounds_check:return 0x40000u;
    default:return 0;
    }
}
static int mo_patch(BYTE saved[MO_IDS][5],int active[MO_IDS],unsigned which,int on){
    for(unsigned i=0;i<MO_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!mo_addresses[i])continue;if(actor_patch(mo_addresses[i]-0x400000u,mo_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(mo_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static int mo_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],esi,expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;mo_build(index,a,r,&esi,0);
        static BYTE before[MO_GLOBALS],after[MO_GLOBALS],actual[MO_GLOBALS];BYTE expected_fpu[108],actual_fpu[108];static mo_record expected[MO_MAX];
        memcpy(mo_before,mo_arena,MO_SIZE);mo_globals(before,0);
        mo_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=mo_calls;memcpy(expected,mo_log,calls*sizeof(*expected));memcpy(mo_after,mo_arena,MO_SIZE);mo_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("objectives poison install");
        memcpy(mo_arena,mo_before,MO_SIZE);mo_globals(before,1);mo_reset();sg_poison_call(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);mo_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=mo_calls||memcmp(expected,mo_log,calls*sizeof(*expected))||memcmp(mo_after,mo_arena,MO_SIZE)||
           memcmp(after,actual,MO_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1){
            mo_global_diff(after,actual);status=mo_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        }
        if(!es2_hook_uninstall(&h))return fail("objectives poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state\n",d->name,total);
    return status;
}
/* Real callees with the nested hooks in rotating original/replaced/compare
 * modes (where compare is refused it falls back to replaced) and a
 * patterned stack. The leaves have no callees and no real pass. */
static int mo_real_pass(unsigned index){
    static const unsigned sets[12][6]={
        {ES2_HOOK_INDEX_actor_safe,ES2_HOOK_INDEX_target_known,ES2_HOOK_INDEX_order_target,ES2_HOOK_INDEX_route_point,ES2_HOOK_INDEX_dist_gate},
        {ES2_HOOK_INDEX_target_known,ES2_HOOK_INDEX_order_target,ES2_HOOK_INDEX_route_point,ES2_HOOK_INDEX_dist_gate},
        {ES2_HOOK_INDEX_dist_gate},
        {0},
        {ES2_HOOK_INDEX_subject_done,ES2_HOOK_INDEX_rivals_done,ES2_HOOK_INDEX_route_point},
        {0},
        {0},
        {0},
        {ES2_HOOK_INDEX_timer_word_step,ES2_HOOK_INDEX_bounds_check,ES2_HOOK_INDEX_bounds_outside,ES2_HOOK_INDEX_status_eval,ES2_HOOK_INDEX_objective_eval,ES2_HOOK_INDEX_actor_safe},
        {ES2_HOOK_INDEX_objective_eval,ES2_HOOK_INDEX_actor_safe,ES2_HOOK_INDEX_bounds_check,ES2_HOOK_INDEX_bounds_outside,ES2_HOOK_INDEX_timer_word_step},
        {ES2_HOOK_INDEX_bounds_outside},
        {0}};
    static const unsigned sizes[12]={5,4,1,0,3,0,0,0,6,5,1,0};
    unsigned set=index-ES2_HOOK_INDEX_objective_eval,nested_count=sizes[set];const unsigned *nested=sets[set];
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0,virtuals=0;uint64_t entered[6]={0};
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    mo_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<1024&&!status;++n){
        uint32_t a[6],r[3],esi,expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;mo_build(index,a,r,&esi,1);
        static BYTE before[MO_GLOBALS],after[MO_GLOBALS],actual[MO_GLOBALS];static mo_record expected[MO_MAX];
        memcpy(mo_before,mo_arena,MO_SIZE);mo_globals(before,0);
        es2_hook hk[7];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<nested_count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!nested_install_hook(hk+j,nested[j],&o,e))return fail("objectives nested install");}
        mo_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        unsigned calls=mo_calls;memcpy(expected,mo_log,calls*sizeof(*expected));virtuals+=calls;
        memcpy(mo_after,mo_arena,MO_SIZE);mo_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+nested_count,d,&o,function_hook_extension(index)))return fail("objectives caller install");
        memcpy(mo_arena,mo_before,MO_SIZE);mo_globals(before,1);mo_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);mo_globals(actual,0);
        if(expected_pair[0]!=actual_pair[0]||expected_pair[1]!=actual_pair[1]||calls!=mo_calls||memcmp(expected,mo_log,calls*sizeof(*expected))||
           memcmp(mo_after,mo_arena,MO_SIZE)||memcmp(after,actual,MO_GLOBALS)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            mo_global_diff(after,actual);fprintf(stderr,"real mode=%u\n",mode);status=mo_report("real",n,pair,calls,expected);
        }
        for(unsigned j=0;j<=nested_count;++j){if(j<nested_count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("objectives nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("objectives nested uninstall");}
        mo_globals(before,1);++total;
    }
    mo_real=0;
    for(unsigned j=0;j<nested_count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    /* The replaced caller (modes 3..5) reaches every one of its callees. */
    for(unsigned i=0;i<count&&!status;++i)if(c[i].count==counts[i]){fprintf(stderr,"FAIL: %s real pass never called %s\n",d->name,c[i].name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, %u virtual calls, 31/a7 stack, full pair, trace and memory\n",d->name,total,nested_count,virtuals);
    return status;
}
/* Compare cases (compare_probes.h) for target_known, order_target, route_point,
 * bounds_check and bounds_outside: real callees (dist_gate, bounds_outside),
 * real-pass inputs; the globals include the bank 4a9e70. */
static void mo_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){uint32_t esi;(void)n;mo_build(index,a,r,&esi,1);}
static void mo_compare_state(BYTE *s,int restore){
    if(restore){memcpy(mo_arena,s,MO_SIZE);mo_globals(s+MO_SIZE,1);}else{memcpy(s,mo_arena,MO_SIZE);mo_globals(s+MO_SIZE,0);}
}
static int mission_objectives_cases(unsigned index,unsigned limit){
    unsigned which=mo_which(index);mo_index=index;
    BYTE saved[MO_IDS][5],old_arena[MO_SIZE];int active[MO_IDS];static BYTE old_globals[MO_GLOBALS];static BYTE old_bank[2000];
    mo_globals(old_globals,0);memcpy(old_bank,mapped+0xa9e70,2000);memcpy(old_arena,mo_arena,MO_SIZE);mo_real=0;
    if(limit&&compare_probe_installed(index)){mo_real=1;int status=compare_probe_run(index,limit,mo_compare_build,mo_compare_state,MO_SIZE+MO_GLOBALS);
        mo_real=0;mo_globals(old_globals,1);memcpy(mapped+0xa9e70,old_bank,2000);memcpy(mo_arena,old_arena,MO_SIZE);return status;}
    if(mo_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_OBJECTIVES_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3],esi;mo_build(index,a,r,&esi,0);status=mo_compare(index,n,a,r,esi);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=mo_installed_probes(index,64);
    if(mo_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status&&mo_which(index))status=mo_real_pass(index);
    if(!limit&&!status&&compare_probe_capable(index)){mo_real=1;status=compare_probe_run(index,0,mo_compare_build,mo_compare_state,MO_SIZE+MO_GLOBALS);mo_real=0;}
    mo_globals(old_globals,1);memcpy(mapped+0xa9e70,old_bank,2000);memcpy(mo_arena,old_arena,MO_SIZE);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
