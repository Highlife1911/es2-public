/* Route group (mission_runtime.md, "Player route callback, mission timers and
 * status check"): player_route 41c194, route_closing 423170, route_advance
 * 42313c, timer_scan 426b48, mission_timer 4230a4, status_check 4135e8. Direct
 * cases: every game callee is a logging stub that records its ordered arguments
 * and entry registers (stack addresses masked; for 413280 also ESI, which
 * 4135e8 keeps equal to force) and changes the mode, step, latch, group links,
 * timer slots and status globals between calls, so local copies and re-reads
 * are checked. Event records are compared by their six written bytes; the 16
 * bytes the original never writes only in the installed 31/a7 poison cases,
 * where status_check's entry shim copies them. Then a real-callee pass: the
 * nested hooks in rotating modes over the real 41dbcc, 4238a0, 420a68, 433158,
 * 413780 and 413280; only the two cockpit ports' slot 0 are stubs. */
typedef struct rt_record { uint32_t id,args[3],regs[4],input[6],eax,edx; } rt_record;
enum { RT_SIZE=0x3000, RT_ACTORS=0x0000, RT_GROUPS=0x1800, RT_ORDERS=0x1b00, RT_ROUTES=0x1bc0, RT_POINTERS=0x1c40,
    RT_POINTS=0x1d40, RT_MEMBERS=0x1e00, RT_TIMERS=0x1e80, RT_TRIGGERS=0x2000, RT_OBJECTIVES=0x2200, RT_HOLDER=0x2400,
    RT_PORTS=0x2700, RT_VTABLES=0x2780, RT_RESIDUE=0x2800, RT_MAX=96, RT_IDS=20, RT_NACTORS=8, RT_NGROUPS=6,
    RT_NORDERS=8, RT_NROUTES=4, RT_NPOINTS=16, RT_NTIMERS=6, RT_NTRIGGERS=6, RT_NOBJECTIVES=6, RT_GLOBALS=0x900 };
#define RT_BASE ((uint32_t)(uintptr_t)rt_arena)
static BYTE rt_arena[RT_SIZE],rt_before[RT_SIZE],rt_after[RT_SIZE];
static rt_record rt_log[RT_MAX];
static unsigned rt_calls,rt_index;
static int rt_real,rt_full;
static uint32_t rt_seed,rt_stream,rt_subject,rt_force;
__attribute__((used)) static uint32_t rt_ecx;
/* Patched callees (ids 0..17) and the two cockpit ports' slot 0 (18: the
 * EventPort, counted by status_check as its call site 4136fb; 19: the
 * DialogPort that 433158 returns), reached through own vtables. */
static const uint32_t rt_addresses[RT_IDS]={0x423b0c,0x4928d0,0x423170,0x420a68,0x42313c,0x423918,0x41dbcc,0x4238a0,
    0x49288c,0x492934,0x467aa8,0x4230a4,0x423430,0x467ac4,0x467a48,0x413780,0x41379c,0x413280,0x4136fb,0};
static const unsigned rt_counts[RT_IDS]={2,2,1,1,1,2,1,1,2,2,1,1,1,2,1,1,1,1,2,2};
static uint32_t rt_next(void){
    uint32_t z=rt_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *rt_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int rt_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static uint32_t rt_actor(unsigned k){return RT_BASE+RT_ACTORS+0x300u*k;}
static uint32_t rt_group(unsigned k){return RT_BASE+RT_GROUPS+0x7au*k;}
static uint32_t rt_order(unsigned k){return RT_BASE+RT_ORDERS+0x16u*k;}
static uint32_t rt_route(unsigned k){return RT_BASE+RT_ROUTES+8u*k;}
static uint32_t rt_point(unsigned k){return RT_BASE+RT_POINTS+12u*k;}
static uint32_t rt_timer(unsigned k){return RT_BASE+RT_TIMERS+0x31u*k;}
static uint32_t rt_trigger(unsigned k){return RT_BASE+RT_TRIGGERS+0x3au*k;}
/* Predicates of every flavour: zero, one, the sign bit, a nonzero high word
 * over AX = 0, anything. */
static uint32_t rt_flag(uint32_t pick){
    switch(pick%8){case 0:case 1:case 2:return 0;case 3:return 1;case 4:return 0x8000u;
        case 5:return 0x10000u|(rt_next()&0xffff0000u);case 6:return (rt_next()&0xffff0000u)|1u;default:return rt_next();}
}
static uint32_t rt_distance(uint32_t pick,uint32_t threshold){
    switch(pick%6){case 0:case 1:case 2:return threshold+(uint32_t)((int)(pick/6%3)-1);
        case 3:return rt_next()%(2u*threshold);case 4:return 0u-rt_next()%1000u;default:return rt_next();}
}
static uint32_t rt_status(uint32_t pick){
    static const uint16_t values[]={6,7,8,9,6,9,2,3,4,5,10,0,1,0xffff,11};
    uint32_t v=(rt_next()&0xffff0000u)|values[pick%15];
    if(pick%9==0)v=(v&0xffff0000u)|(uint16_t)get32(mapped+0x9939c);
    if(pick%23==0)v=rt_next();
    return v;
}
/* 4a9cec, the status globals and the mode: changed by stubs between calls. */
static void rt_shake(uint32_t pick){
    if(pick%5==0)put32(mapped+0xa9cec,rt_next()%3?rt_next()%6u:rt_next()%0x8000u);
    if(pick%7==0)put16(mapped+0xa9e54,(uint16_t)(rt_next()%3?(uint32_t[]){0,3,5,7}[rt_next()%4]:rt_next()));
    if(pick%11==0)put32(mapped+0xa9cf8,rt_next()%2?0:rt_next());
    if(pick%13==0)put32(rt_host(rt_subject+0x45),rt_group(rt_next()%RT_NGROUPS));
}
__attribute__((used,noinline)) static uint64_t __cdecl rt_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(rt_calls==RT_MAX){fail("route synthetic call overflow");ExitProcess(1);}
    rt_record *v=rt_log+rt_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,4*rt_counts[id]);
    if(!rt_real){memcpy(v->regs,regs,12);if(id==17)v->regs[3]=regs[3];}
    for(unsigned j=0;j<4;++j)if(rt_stack(v->regs[j]))v->regs[j]=0;
    for(unsigned j=0;j<rt_counts[id];++j)if(rt_stack(v->args[j]))v->args[j]=0;
    /* 4135e8's entry ECX reaches the first 467a48 (periodic) or 413280
     * (forced); neither reads it, and the entry shim replaces it. */
    if(rt_index==ES2_HOOK_INDEX_status_check&&((id==14&&rt_calls==1)||(id==17&&(uint16_t)rt_force)))v->regs[2]=0;
    uint32_t eax=rt_next(),edx=rt_next(),pick=rt_next();rt_ecx=rt_next();
    if(id==18||id==19){ /* port slot 0: the record by content */
        const BYTE *p=rt_host(a[1]);
        for(unsigned j=0;j<22;++j)if(j<6||rt_full)v->input[j/4]|=(uint32_t)p[j]<<(8*(j%4));
        if(!rt_real&&pick%3==0)rt_shake(rt_next());
        if(!rt_real&&pick%5==0)put32(mapped+0x9939c,rt_next()%2?6u+rt_next()%4u:rt_next());
        v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
    }
    switch(id){
    case 0: eax=pick%3?rt_point(rt_next()%RT_NPOINTS):pick%9==0?rt_next():0;break;
    case 1: eax=rt_distance(pick>>3,(pick>>20)%2?10000u:40000u);break;
    case 8: eax=rt_distance(pick>>3,10000u);break;
    case 2: case 5: case 15: case 16: eax=rt_flag(pick>>3);break;
    case 6: eax=rt_point(rt_next()%RT_NPOINTS);break;
    case 7: eax=rt_actor(rt_next()%RT_NACTORS);break;
    case 9: { /* heading: often on the front window's edges */
        uint32_t base=get16(rt_host(rt_subject+0x10))-get16(rt_host(rt_subject+0x298))-0x2000u;
        static const uint32_t edges[]={0,0x3fff,0x4000,0xffff,1,0x3ffe};
        eax=pick%3?(rt_next()&0xffff0000u)|(uint16_t)(base+edges[(pick>>2)%6]):rt_next();break; }
    case 10: case 14: eax=pick%2?0:(rt_next()&0xffff0000u)|(pick%3?rt_next()%500u:0);
        if(id==14&&pick%5==0)eax=(rt_next()&0xffff0000u)|(1u+rt_next()%500u);break;
    case 11: if(!rt_real&&pick%7==0)put16(mapped+0xa9e34,(uint16_t)(rt_next()%(RT_NTIMERS+1u)));break;
    case 12: if(!rt_real&&pick%3==0)put32(rt_host(rt_subject+4+4*(rt_next()%10u)),rt_next()%3?rt_trigger(rt_next()%RT_NTRIGGERS):0);break;
    case 17: eax=rt_status(pick>>3);break;
    default: break;
    }
    if(!rt_real){
        if(id<=10&&pick%4==0)rt_shake(rt_next());
        if(id>=14&&id<=17){
            if(pick%5==0)put32(mapped+0x9939c,rt_next()%2?rt_status(rt_next())&0xffffu:rt_next());
            if(pick%7==0)put32(mapped+0x993a0,rt_next()%2?0xffffffffu:rt_next()%11u);
        }
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
/* Pushes ESI, ECX, EDX, EAX for the record before the slot pointer. */
#define RT_STUB(n) __attribute__((naked)) static void rt_stub_##n(void){__asm__ volatile( \
    "pushl %esi\n\tpushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 20(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _rt_stub_impl\n\taddl $28,%esp\n\tmovl _rt_ecx,%ecx\n\tret\n\t");}
RT_STUB(0) RT_STUB(1) RT_STUB(2) RT_STUB(3) RT_STUB(4) RT_STUB(5) RT_STUB(6) RT_STUB(7) RT_STUB(8) RT_STUB(9)
RT_STUB(10) RT_STUB(11) RT_STUB(12) RT_STUB(13) RT_STUB(14) RT_STUB(15) RT_STUB(16) RT_STUB(17) RT_STUB(18) RT_STUB(19)
#undef RT_STUB
static void *rt_stubs[RT_IDS]={rt_stub_0,rt_stub_1,rt_stub_2,rt_stub_3,rt_stub_4,rt_stub_5,rt_stub_6,rt_stub_7,rt_stub_8,rt_stub_9,
    rt_stub_10,rt_stub_11,rt_stub_12,rt_stub_13,rt_stub_14,rt_stub_15,rt_stub_16,rt_stub_17,rt_stub_18,rt_stub_19};
/* Globals: status state 499398..4993a3, cockpit 49b088, actor list,
 * status timer 4a9b68, route state 4a9cec..4a9cfb, the mission block
 * 4a9e00..4aa64f (timers, objectives, mode, variables, rectangle), alert
 * text 4d1e98, player, time scale. */
static const struct { uint32_t rva,size; } rt_ranges[]={{0x99398,12},{0x9b088,4},{0xa9af8,8},{0xa9b68,4},{0xa9cec,16},
    {0xa9e00,0x850},{0xd1e98,4},{0xd24e6,4},{0xd3b64,2}};
static void rt_globals(BYTE *out,int restore){
    unsigned at=0;
    for(unsigned i=0;i<sizeof(rt_ranges)/sizeof(rt_ranges[0]);++i){
        if(restore)memcpy(mapped+rt_ranges[i].rva,out+at,rt_ranges[i].size);else memcpy(out+at,mapped+rt_ranges[i].rva,rt_ranges[i].size);
        at+=rt_ranges[i].size;
    }
}
static void rt_reset(void){rt_calls=0;rt_stream=rt_seed;}
static uint32_t rt_pick(const uint32_t *values,unsigned count){return values[random32()%count];}
static uint32_t rt_coordinate(int real){
    return real?(uint32_t)((int32_t)(random32()%30001u)-15000):(uint32_t)((int32_t)(random32()%400001u)-200000);
}
static void rt_build(unsigned index,uint32_t a[6],uint32_t r[3],int real){
    uint32_t b=RT_BASE;rt_seed=random32();memset(a,0,24);
    for(unsigned i=0;i<RT_SIZE;i+=4)put32(rt_arena+i,random32());
    for(unsigned i=0;i<3;++i)r[i]=random32();
    for(unsigned i=0;i<2000;i+=4)put32(mapped+0xa9e70+i,random32());
    /* Cockpit: [49b088] -> holder; +207 DialogPort, +20B EventPort, each
     * with an own vtable whose slot 0 is a stub. */
    put32(mapped+0x9b088,b+RT_HOLDER);put32(rt_arena+RT_HOLDER+0x207,b+RT_PORTS);put32(rt_arena+RT_HOLDER+0x20b,b+RT_PORTS+0x40);
    put32(rt_arena+RT_PORTS,b+RT_VTABLES);put32(rt_arena+RT_PORTS+0x40,b+RT_VTABLES+0x40);
    put32(rt_arena+RT_VTABLES,(uint32_t)(uintptr_t)rt_stubs[19]);put32(rt_arena+RT_VTABLES+0x40,(uint32_t)(uintptr_t)rt_stubs[18]);
    for(unsigned k=0;k<RT_NPOINTS;++k){put32(rt_arena+RT_POINTS+12*k,rt_coordinate(real));put32(rt_arena+RT_POINTS+12*k+4,rt_coordinate(real));
        put32(rt_arena+RT_POINTS+12*k+8,random32()%2001u-1000u);}
    /* Routes: count word, 16 point pointers (the last sometimes the first:
     * a closed route). Counts stay small so every read is in the arena. */
    for(unsigned k=0;k<RT_NROUTES;++k){
        static const uint32_t counts[]={0,1,2,3,4,5,8,9,0xffff,0x8000,2,3};
        uint32_t count=real?1+random32()%8:rt_pick(counts,12);put16(rt_arena+RT_ROUTES+8*k,(uint16_t)count);
        uint32_t list=b+RT_POINTERS+0x40*k;put32(rt_arena+RT_ROUTES+8*k+2,list);
        for(unsigned j=0;j<16;++j)put32(rt_host(list+4*j),!real&&random32()%8==0?0:rt_point(random32()%RT_NPOINTS));
        if(random32()%2&&count-1u<16u)put32(rt_host(list+4*(count-1u)),get32(rt_host(list)));
        if(!real&&random32()%3&&count-2u<16u)put32(rt_host(list+4*(count-2u)),get32(rt_host(list)));
    }
    /* Orders: scope 0 names a group, 1..3 an actor (real: always 0..3). */
    for(unsigned k=0;k<RT_NORDERS;++k){BYTE *o=rt_arena+RT_ORDERS+0x16*k;
        static const uint32_t scopes[]={0,1,2,3,0,1,0xffff,4};
        uint32_t scope=real?random32()%4:rt_pick(scopes,8);put16(o,(uint16_t)(random32()%8));put32(o+8,rt_route(random32()%RT_NROUTES));
        put16(o+0xc,(uint16_t)scope);put32(o+0xe,scope==0?rt_group(random32()%RT_NGROUPS):rt_actor(random32()%RT_NACTORS));
    }
    for(unsigned k=0;k<RT_NGROUPS;++k){BYTE *g=rt_arena+RT_GROUPS+0x7a*k;
        static const uint32_t indexes[]={0,1,2,3,6,7,0xffff,0xfffe};
        put16(g,(uint16_t)(random32()%3));
        uint32_t route=rt_route(random32()%RT_NROUTES),count=get16(rt_host(route));
        uint32_t index=random32()%2?rt_pick(indexes,8):count-1u-random32()%3u;
        if(real)index=random32()%(count+1u);
        put16(g+4,(uint16_t)index);put32(g+6,!real&&random32()%8==0?0:route);
        put32(g+0xc,b+RT_MEMBERS+16*k);put16(g+0x10,(uint16_t)(1+random32()%4));g[0x12]=(BYTE)(random32()%3);
        put32(g+0x14,random32()%3?0:random32()|1u);
        for(unsigned j=0;j<10;++j)put32(g+0x44+4*j,!real&&random32()%4==0?0:rt_order(random32()%RT_NORDERS));
        uint32_t current=random32()%10;put32(g+0x6c,current);if(real)put32(g+0x44+4*current,rt_order(random32()%RT_NORDERS));
        for(unsigned j=0;j<4;++j)put32(rt_arena+RT_MEMBERS+16*k+4*j,rt_actor(random32()%RT_NACTORS));
    }
    for(unsigned k=0;k<RT_NACTORS;++k){BYTE *o=rt_arena+RT_ACTORS+0x300*k;
        put32(o+0x26,rt_coordinate(real));put32(o+0x2a,rt_coordinate(real));put32(o+0x2e,random32()%2001u-1000u);
        put32(o+0x45,rt_group(random32()%RT_NGROUPS));put16(o+0x10,(uint16_t)random32());put16(o+0x298,(uint16_t)(random32()%3?0:random32()));
        static const unsigned bytes[]={0x99,0x9f,0xa0,0xa4,0xa5};
        for(unsigned j=0;j<5;++j)o[bytes[j]]=(BYTE)(random32()%4?0:(uint32_t[]){1,0x80,0xff}[random32()%3]);
        put32(o+0x1a4,random32()%4?rt_actor(random32()%RT_NACTORS):0);
        static const uint32_t states[]={3,5,6,0,1,4};
        put16(o+0x23e,(uint16_t)rt_pick(states,6));
        put32(o+0x248,rt_actor(random32()%RT_NACTORS));put32(o+0x24c,random32()%4?rt_actor(random32()%RT_NACTORS):0);
        if(get16(o+0x23e)==3)put32(o+0x24c,rt_actor(random32()%RT_NACTORS));
    }
    put32(mapped+0xd24e6,rt_actor(random32()%RT_NACTORS));
    /* Route state: mode, step, step timer, target latch; time scale. */
    static const uint32_t modes[]={0,0,5,5,3,3,7,7,1,2,4,6,0x8000,0xffff};
    put16(mapped+0xa9e54,(uint16_t)rt_pick(modes,14));
    /* An unbounded step indexes 49a318 (the original reads the image there). */
    put32(mapped+0xa9cec,real?random32()%5:random32()%8?random32()%6:random32()%0x8000u);
    mapped[0xa9cf0]=(BYTE)random32();put32(mapped+0xa9cf1,real?random32()%3*(random32()%200u):random32());
    put32(mapped+0xa9cf8,random32()%3?0:random32()%2?1:random32());
    put16(mapped+0xd3b64,(uint16_t)(50+random32()%100));
    /* Triggers (for the real trigger_fire): latch, ids, actions, selector. */
    for(unsigned k=0;k<RT_NTRIGGERS;++k){BYTE *t=rt_arena+RT_TRIGGERS+0x3a*k;
        put16(t+0xa,(uint16_t)(random32()%3?0:1));
        for(unsigned j=0;j<10;++j){put16(t+0xc+2*j,(uint16_t)(random32()%3?0xffffu:random32()%16u));put16(t+0x20+2*j,(uint16_t)(4+random32()%4));}
        put16(t+0x34,(uint16_t)(random32()%2?0xffffu:random32()%64u));
    }
    /* Mission timers: gate (none, latched or not), ten slots, timer. */
    for(unsigned k=0;k<RT_NTIMERS;++k){BYTE *t=rt_arena+RT_TIMERS+0x31*k;
        put32(t,random32()%2?0:rt_trigger(random32()%RT_NTRIGGERS));
        for(unsigned j=0;j<10;++j)put32(t+4+4*j,random32()%3?0:rt_trigger(random32()%RT_NTRIGGERS));
        t[0x2c]=(BYTE)random32();put32(t+0x2d,real?random32()%2*(random32()%300u):random32());
    }
    put32(mapped+0xa9e38,b+RT_TIMERS);
    put16(mapped+0xa9e34,(uint16_t)(random32()%8?random32()%(RT_NTIMERS+1u):random32()%2?0xffffu:0x8000u|random32()));
    if(real)put16(mapped+0xa9e34,(uint16_t)(random32()%(RT_NTIMERS+1u)));
    /* Status state and the real 413280's inputs: objectives whose types call
     * no helper (3, 4, 9, 10 read the player, 5 and 11 assign nothing, so
     * the caller's ESI decides), an empty actor list, the map rectangle. */
    mapped[0x99398]=(BYTE)(random32()%2?0:random32());
    static const uint32_t statuses[]={6,7,8,9,2,3,4,5,10,0xffffffffu};
    put32(mapped+0x9939c,rt_pick(statuses,10));put32(mapped+0x993a0,random32()%2?0xffffffffu:rt_pick(statuses,10));
    mapped[0xa9b68]=(BYTE)random32();put16(mapped+0xa9b69,(uint16_t)(random32()%2?0:random32()%300u));mapped[0xa9b6b]=(BYTE)random32();
    for(unsigned k=0;k<RT_NOBJECTIVES;++k){BYTE *o=rt_arena+RT_OBJECTIVES+0x4c*k;
        static const uint32_t types[]={3,4,5,9,10,11};
        put16(o,(uint16_t)(random32()%3));put16(o+2,(uint16_t)rt_pick(types,6));put16(o+4,(uint16_t)(random32()%5));
        put32(o+6,rt_group(random32()%RT_NGROUPS));put16(o+0x22,(uint16_t)(random32()%4?0:1));
        for(unsigned j=0;j<10;++j){put16(o+0x24+2*j,(uint16_t)(random32()%3?0xffffu:random32()%16u));put16(o+0x38+2*j,(uint16_t)(4+random32()%4));}
    }
    put32(mapped+0xa9e40,b+RT_OBJECTIVES);put16(mapped+0xa9e3c,(uint16_t)(random32()%(RT_NOBJECTIVES+1u)));
    put32(mapped+0xa9af8,b+RT_MEMBERS);put16(mapped+0xa9afe,0);
    uint32_t x0=0u-random32()%20000u,y0=0u-random32()%20000u,x1=random32()%20000u,y1=random32()%20000u;
    put32(mapped+0xaa640,x0);put32(mapped+0xaa644,y0);put32(mapped+0xaa648,x1);put32(mapped+0xaa64c,y1);
    random_bytes(rt_arena+RT_RESIDUE,16);
    /* Arguments. */
    switch(index){
    case ES2_HOOK_INDEX_player_route: {
        a[0]=rt_actor(random32()%RT_NACTORS);uint32_t g=get32(rt_host(a[0]+0x45));
        if(random32()%8)put32(rt_host(get32(rt_host(g+0xc))),a[0]);
        break; }
    case ES2_HOOK_INDEX_route_closing: case ES2_HOOK_INDEX_route_advance: a[0]=rt_group(random32()%RT_NGROUPS)+4u;
        if(!get32(rt_host(a[0]+2)))put32(rt_host(a[0]+2),rt_route(random32()%RT_NROUTES));
        r[1]=(random32()&0xffff0000u)|(uint16_t)random32();break;
    case ES2_HOOK_INDEX_mission_timer: a[0]=rt_timer(random32()%RT_NTIMERS);break;
    case ES2_HOOK_INDEX_status_check: a[0]=get32(mapped+0xd24e6);a[1]=random32();
        a[2]=rt_pick((const uint32_t[]){0,0,0,1,0x10000,0xffff,0x12340000,0x8000},8);if(random32()%8==0)a[2]=random32();
        if(!real)r[2]=b+RT_RESIDUE;break;
    default: break;
    }
    if(index==ES2_HOOK_INDEX_player_route&&real){BYTE *o=rt_host(a[0]);
        /* Near the next route point or the order's target, so 10000 and
         * 40000 are met both ways. */
        uint32_t g=get32(o+0x45),route=get32(rt_host(g+6)),index2=(uint16_t)(get16(rt_host(g+4))+1u);
        if(random32()%2&&index2<get16(rt_host(route))){uint32_t p=get32(rt_host(get32(rt_host(route+2))+4*index2));
            put32(o+0x26,get32(rt_host(p))+random32()%16001u-8000u);put32(o+0x2a,get32(rt_host(p+4))+random32()%16001u-8000u);}
    }
    rt_subject=a[0];rt_force=a[2];
}
static int rt_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const rt_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,rt_calls);
    for(unsigned j=0;j<calls&&j<rt_calls&&expected;++j)if(memcmp(expected+j,rt_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(rt_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<RT_SIZE;++j)if(rt_after[j]!=rt_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,rt_after[j],rt_arena[j]);break;}
    return 1;
}
static void rt_global_diff(const BYTE *expected,const BYTE *actual){
    for(unsigned j=0;j<RT_GLOBALS;++j)if(expected[j]!=actual[j]){fprintf(stderr," global byte %x %02x/%02x\n",j,expected[j],actual[j]);break;}
}
static unsigned rt_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<rt_calls;++j)if(c->event==ES2_COLLIDE_CALL&&rt_addresses[rt_log[j].id]==c->key)++hits;
    return hits;
}
static int rt_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    static BYTE before[RT_GLOBALS],after[RT_GLOBALS],actual[RT_GLOBALS];static rt_record expected[RT_MAX];
    memcpy(rt_before,rt_arena,RT_SIZE);rt_globals(before,0);
    uint32_t got[2];rt_reset();mo_machine(mapped+d->rva,a,r,0,got);
    uint64_t pair[2];pair[0]=(uint64_t)got[1]<<32|got[0];unsigned calls=rt_calls;memcpy(expected,rt_log,calls*sizeof(*expected));
    memcpy(rt_after,rt_arena,RT_SIZE);rt_globals(after,0);
    memcpy(rt_arena,rt_before,RT_SIZE);rt_globals(before,1);rt_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);rt_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=rt_expected_hits(c+i))return fail("route callee counter delta");
    if(pair[0]!=pair[1]||calls!=rt_calls||memcmp(expected,rt_log,calls*sizeof(*expected))||memcmp(rt_after,rt_arena,RT_SIZE)||memcmp(after,actual,RT_GLOBALS)){
        rt_global_diff(after,actual);return rt_report("direct",n,pair,calls,expected);
    }
    return 0;
}
static unsigned rt_which(unsigned index){
    switch(index){
    case ES2_HOOK_INDEX_player_route:return 0x7ffu;
    case ES2_HOOK_INDEX_timer_scan:return 0x800u;
    case ES2_HOOK_INDEX_mission_timer:return 0x3400u;
    case ES2_HOOK_INDEX_status_check:return 0x3c000u;
    default:return 0;
    }
}
static int rt_patch(BYTE saved[RT_IDS][5],int active[RT_IDS],unsigned which,int on){
    for(unsigned i=0;i<RT_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||i>=18)continue;if(actor_patch(rt_addresses[i]-0x400000u,rt_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(rt_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static int rt_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;rt_full=1;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;rt_build(index,a,r,0);
        static BYTE before[RT_GLOBALS],after[RT_GLOBALS],actual[RT_GLOBALS];BYTE expected_fpu[108],actual_fpu[108];static rt_record expected[RT_MAX];
        memcpy(rt_before,rt_arena,RT_SIZE);rt_globals(before,0);
        rt_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=rt_calls;memcpy(expected,rt_log,calls*sizeof(*expected));memcpy(rt_after,rt_arena,RT_SIZE);rt_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("route poison install");
        memcpy(rt_arena,rt_before,RT_SIZE);rt_globals(before,1);rt_reset();sg_poison_call(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);rt_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=rt_calls||memcmp(expected,rt_log,calls*sizeof(*expected))||memcmp(rt_after,rt_arena,RT_SIZE)||
           memcmp(after,actual,RT_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1){
            rt_global_diff(after,actual);status=rt_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        }
        if(!es2_hook_uninstall(&h))return fail("route poison uninstall");++total;
    }
    rt_full=0;es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/records/state\n",d->name,total);
    return status;
}
/* Real callees with the nested hooks in rotating original/replaced/compare
 * modes (compare falls back to replaced where refused) and a patterned
 * stack. Port records are compared by their written bytes (the residue
 * below a changed caller differs, as live). A timing hook (32-bit result)
 * in a non-original mode leaves EDX outside its contract, and these cores
 * return that EDX on some paths: then EDX is not compared. */
static int rt_real_pass(unsigned index){
    static const unsigned player[]={ES2_HOOK_INDEX_route_point,ES2_HOOK_INDEX_dist2d_point,ES2_HOOK_INDEX_route_closing,ES2_HOOK_INDEX_order_target,
        ES2_HOOK_INDEX_route_advance,ES2_HOOK_INDEX_dist_gate,ES2_HOOK_INDEX_heading_point,ES2_HOOK_INDEX_timer_dword_step};
    static const unsigned scan[]={ES2_HOOK_INDEX_mission_timer,ES2_HOOK_INDEX_timer_dword_step,ES2_HOOK_INDEX_trigger_fire,ES2_HOOK_INDEX_timer_arm};
    static const unsigned timer[]={ES2_HOOK_INDEX_timer_dword_step,ES2_HOOK_INDEX_trigger_fire,ES2_HOOK_INDEX_timer_arm};
    static const unsigned status_set[]={ES2_HOOK_INDEX_timer_word_step,ES2_HOOK_INDEX_bounds_check,ES2_HOOK_INDEX_bounds_outside,
        ES2_HOOK_INDEX_objective_eval,ES2_HOOK_INDEX_actor_safe};
    const unsigned *nested;unsigned nested_count;
    switch(index){
    case ES2_HOOK_INDEX_player_route:nested=player;nested_count=8;break;
    case ES2_HOOK_INDEX_timer_scan:nested=scan;nested_count=4;break;
    case ES2_HOOK_INDEX_mission_timer:nested=timer;nested_count=3;break;
    default:nested=status_set;nested_count=5;break;
    }
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0,virtuals=0;uint64_t entered[8]={0};
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    rt_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<1024&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;rt_build(index,a,r,1);
        static BYTE before[RT_GLOBALS],after[RT_GLOBALS],actual[RT_GLOBALS];static rt_record expected[RT_MAX];
        memcpy(rt_before,rt_arena,RT_SIZE);rt_globals(before,0);
        es2_hook hk[10];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        int junk_edx=0;
        for(unsigned j=0;j<nested_count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!e&&o.mode!=ES2_HOOK_ORIGINAL)junk_edx=1;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("route nested install");}
        rt_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        unsigned calls=rt_calls;memcpy(expected,rt_log,calls*sizeof(*expected));virtuals+=calls;
        memcpy(rt_after,rt_arena,RT_SIZE);rt_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+nested_count,d,&o,function_hook_extension(index)))return fail("route caller install");
        memcpy(rt_arena,rt_before,RT_SIZE);rt_globals(before,1);rt_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);rt_globals(actual,0);
        if(junk_edx)expected_pair[1]=actual_pair[1];
        if(expected_pair[0]!=actual_pair[0]||expected_pair[1]!=actual_pair[1]||calls!=rt_calls||memcmp(expected,rt_log,calls*sizeof(*expected))||
           memcmp(rt_after,rt_arena,RT_SIZE)||memcmp(after,actual,RT_GLOBALS)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            rt_global_diff(after,actual);fprintf(stderr,"real mode=%u\n",mode);status=rt_report("real",n,pair,calls,expected);
        }
        for(unsigned j=0;j<=nested_count;++j){if(j<nested_count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("route nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("route nested uninstall");}
        rt_globals(before,1);++total;
    }
    rt_real=0;
    for(unsigned j=0;j<nested_count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    for(unsigned i=0;i<count&&!status;++i)if(c[i].count==counts[i]){fprintf(stderr,"FAIL: %s real pass never called %s\n",d->name,c[i].name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, %u port calls, 31/a7 stack, full pair, trace and memory\n",d->name,total,nested_count,virtuals);
    return status;
}
static int route_cases(unsigned index,unsigned limit){
    unsigned which=rt_which(index);rt_index=index;
    BYTE saved[RT_IDS][5];int active[RT_IDS];static BYTE old_globals[RT_GLOBALS],old_arena[RT_SIZE],old_bank[2000];
    rt_globals(old_globals,0);memcpy(old_bank,mapped+0xa9e70,2000);memcpy(old_arena,rt_arena,RT_SIZE);rt_real=0;
    if(rt_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_ROUTE_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];rt_build(index,a,r,0);status=rt_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=rt_installed_probes(index,64);
    if(rt_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status&&which)status=rt_real_pass(index);
    rt_globals(old_globals,1);memcpy(mapped+0xa9e70,old_bank,2000);memcpy(rt_arena,old_arena,RT_SIZE);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
