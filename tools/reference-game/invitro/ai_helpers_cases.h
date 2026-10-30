/* Route and behaviour helpers: point_offset 4929fc, threat_sum 41cb44,
 * engage_done 41c4a8, flank_step 41c72c, retreat_check 41cb94, part_pick 41ce08
 * and drive_apply 41e828 (docs/reverse-engineering/mission_runtime.md, "Route
 * and behaviour helpers"). Direct cases run the original body and the core
 * against logging stubs for the game callees of the hook under test (ordered
 * argument slots, entry EAX/EDX/ECX, the frame point by content, stack
 * addresses masked in the fixed window of node_cases.h); the actor virtuals
 * +3Ch/+40h go through the arena vtable. Pair, arena and globals (the MECH list
 * head, RNG stream A, the time-scale and stagger words) are compared. Then
 * installed 31/a7 poison probes and a real-callee pass with the nested hooks in
 * rotating modes over a real MECH list, real groups/orders, script tables and
 * part records. */
typedef struct ah_record { uint32_t id,args[4],regs[3],input[4],eax,edx,slots; } ah_record;
enum { AH_SIZE=0x2800, AH_ACT=0x100, AH_STRIDE=0x340, AH_ACTORS=6, AH_GRP=0x1600, AH_ORD=0x1700, AH_CFG=0x1780,
    AH_VT=0x1880, AH_LIST=0x18c0, AH_S=0x18e0, AH_POINT=0x1900, AH_CUR=0x1940, AH_MAXW=0x1980, AH_TAB=0x19c0,
    AH_GVAL=0x19d0, AH_ENTRY=0x1a10, AH_W=0x1c80, AH_R202=0x1d00, AH_R30B=0x1d40, AH_TBL=0x1e00, AH_MAX=40, AH_IDS=14,
    AH_GLOBALS=4+0x72+2+2 };
#define AH_BASE ((uint32_t)(uintptr_t)ah_arena)
/* Actor i with its 8-byte list node header in front of it. */
#define AH_ACTOR(i) (AH_BASE+AH_ACT+AH_STRIDE*(uint32_t)(i)+8u)
#define AH_GROUP(i) (AH_BASE+AH_GRP+0x80u*(uint32_t)(i))
static BYTE ah_arena[AH_SIZE],ah_before[AH_SIZE],ah_after[AH_SIZE];
static ah_record ah_log[AH_MAX];
static unsigned ah_calls,ah_cursor;
static uint32_t ah_seed,ah_stream,ah_s,ah_seq[AH_ACTORS+1];
__attribute__((used)) static uint32_t ah_ecx;
/* 0 411cb4 target_class, 1 423918 order_target, 2 467a48 timer_word_step,
 * 3 4929fc point_offset, 4 492934 heading_point, 5 492ee0 rng_next,
 * 6 40dbc0 part_ratio, 7 416a04 vehicle_control, 8 413e50 script_start,
 * 9 41eb34 order_resume, 10 471c70 list step, 11 41cb44 threat_sum,
 * 12 virtual +3Ch, 13 virtual +40h (12 and 13 are reached through the
 * arena vtable, never patched). */
static const uint32_t ah_addresses[AH_IDS]={0x411cb4,0x423918,0x467a48,0x4929fc,0x492934,0x492ee0,0x40dbc0,0x416a04,
    0x413e50,0x41eb34,0x471c70,0x41cb44,0,0};
static const unsigned ah_slots[AH_IDS]={0,0,0,0,0,0,0,0,0,0,0,0,0x3c,0x40};
static const unsigned ah_counts[AH_IDS]={1,2,1,3,2,1,2,4,2,1,2,1,1,1};
static const struct { uint32_t rva,size; } ah_ranges[]={{0xa9b7a,4},{0xd2599,0x72},{0xd3b64,2},{0xa9b70,2}};
static void ah_globals(BYTE *out,int restore){
    unsigned at=0;
    for(unsigned i=0;i<4;++i){BYTE *g=mapped+ah_ranges[i].rva;
        if(restore)memcpy(g,out+at,ah_ranges[i].size);else memcpy(out+at,g,ah_ranges[i].size);at+=ah_ranges[i].size;}
}
static uint32_t ah_next(void){
    uint32_t z=ah_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *ah_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
/* Same fixed stack window as node_cases.h: frame addresses are masked. */
static int ah_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
/* Readable payload: inside the arena or the committed stack. */
static int ah_ok(uint32_t p,uint32_t n){
    const NT_TIB *t=(const NT_TIB *)NtCurrentTeb();uint32_t low=(uint32_t)(uintptr_t)t->StackLimit,top=(uint32_t)(uintptr_t)t->StackBase;
    return (p-AH_BASE<AH_SIZE&&AH_SIZE-(p-AH_BASE)>=n)||(p>=low&&p<top&&top-p>=n);
}
static void ah_dwords(ah_record *v,unsigned at,uint32_t p,unsigned n){
    if(ah_ok(p,4u*n))for(unsigned j=0;j<n;++j)v->input[at+j]=get32(ah_host(p)+4*j);
}
static uint32_t ah_small(uint32_t pick,uint32_t range){return (uint32_t)((int32_t)(pick%(2u*range+1u))-(int32_t)range);}
__attribute__((used,noinline)) static uint64_t __cdecl ah_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(ah_calls==AH_MAX){fail("ai helpers call overflow");ExitProcess(1);}
    ah_record *v=ah_log+ah_calls++;memset(v,0,sizeof(*v));v->id=id;v->slots=weapons_bridge_slots(a,ah_counts[id]);
    for(unsigned k=0;k<ah_counts[id];++k)v->args[k]=ah_stack(a[k])?0:a[k];
    for(unsigned j=0;j<3;++j)v->regs[j]=ah_stack(regs[j])?0:regs[j];
    uint32_t eax=ah_next(),edx=ah_next(),pick=ah_next();ah_ecx=ah_next();
    BYTE *o=ah_host(AH_ACTOR(0)),*t=ah_host(get32(o+0x1a4));
    switch(id){
    case 0: /* target_class: AX around A's class limit, which is reread. */
        if(pick%2)eax=(eax&0xffff0000u)|(uint16_t)(get16(o+0x250)+ah_small(pick>>4,1));
        if(pick%7==0)put16(o+0x250,(uint16_t)ah_next());
        break;
    case 1: /* order_target: AX zero half of the time; group, target, the
             * group byte and the target's flags are reread. */
        if(pick%2)eax&=0xffff0000u;
        if(pick%11==0)put32(o+0x45,AH_GROUP(pick>>8&1));
        if(pick%11==1)put32(o+0x1a4,AH_ACTOR(1+(pick>>8)%(AH_ACTORS-1)));
        if(pick%11==2)ah_host(get32(o+0x45))[0x12]=(BYTE)(pick>>8&1?0:pick>>16);
        if(pick%11==3)t[(pick>>8)%3==0?0x99:(pick>>8)%3==1?0xa4:0xa5]=(BYTE)(pick>>16&1?0:pick>>20);
        break;
    case 2: /* timer_word_step: AX zero half of the time; byte +66h, +288h and S+8 are reread. */
        if(pick%2)eax&=0xffff0000u;
        if(pick%9==0)o[0x66]^=1;
        if(pick%9==1)put32(o+0x288,0x64u+ah_small(pick>>8,1));
        if(pick%9==2&&ah_s)put16(ah_host(ah_s)+8,(uint16_t)((pick>>12&1?0x4000u:0x6000u)+ah_small(pick>>16,1)));
        break;
    case 3: /* point_offset: L by content (x, y), moved in place. */
        ah_dwords(v,0,a[0],2);
        if(pick%2&&ah_ok(a[0],8)){put32(ah_host(a[0]),ah_next());put32(ah_host(a[0])+4,ah_next());}
        break;
    case 4: /* heading_point: both points by content; the yaw is reread. */
        ah_dwords(v,0,a[0],2);ah_dwords(v,2,a[1],2);
        if(pick%5==0)put16(o+0x10,(uint16_t)ah_next());
        break;
    case 5: /* rng_next: +30Bh, T's +0B4h and the W entries are reread. */
        if(pick%9==0)put32(o+0x30b,pick>>8&1?0:AH_BASE+AH_R30B);
        if(pick%9==1)t[0xb4]=(BYTE)(pick>>8&1);
        if(pick%9==2)put16(ah_arena+AH_W+2*((pick>>8)%0x20),(uint16_t)(pick>>16&1));
        break;
    case 6: /* part_ratio: a ratio near the running top; W entries reread. */
        if(pick%3)eax=(eax&0xffff0000u)|(uint16_t)ah_small(pick>>4,0x80);
        if(pick%5==0)put16(ah_arena+AH_W+2*((pick>>8)%0x20),(uint16_t)(pick>>16&1));
        break;
    case 10: /* list step: the planned entries, then 0; the head and the
              * returned entry's target and strength may change. */
        eax=ah_seq[ah_cursor];if(eax)++ah_cursor;
        if(pick%9==0)put32(mapped+0xa9b7a,ah_next());
        if(eax&&pick%9==1)put16(ah_host(eax)+0x29e,(uint16_t)ah_next());
        if(eax&&pick%9==2)put32(ah_host(eax)+0x1a4,pick>>8&1?AH_ACTOR(0):AH_ACTOR(1));
        break;
    case 11: { /* threat_sum: a sum around the count times +29Eh; both are
                * reread (the count stays above 2 for the division). */
        int32_t n=(int16_t)get16(o+0x1a2),s=(int16_t)get16(o+0x29e);
        if(pick%4)eax=(uint32_t)(s*(n>2?n:1)+(int32_t)ah_small(pick>>4,3));
        if(pick%7==0)put16(o+0x1a2,(uint16_t)(3u+(pick>>8)%5));
        if(pick%7==1)put16(o+0x29e,(uint16_t)ah_small(pick>>8,40));
        break; }
    case 12: { /* virtual +3Ch: AX on the edges of the window F830h..07CFh. */
        static const uint16_t edges[]={0xf830,0xf82f,0x07cf,0x07d0,0,0x8000,0x7fff};
        if(pick%3)eax=(eax&0xffff0000u)|edges[(pick>>4)%7];
        break; }
    case 13: { /* virtual +40h: score bases on the three thresholds. */
        static const uint16_t bases[]={0x1e,0x1f,0x32,0x33,0x46,0x47,0,0xfffb,0x10,0x3c};
        if(pick%4)eax=(eax&0xffff0000u)|bases[(pick>>4)%10];
        break; }
    default:break;
    }
    v->eax=ah_stack(eax)?0:eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define AH_STUB(n) __attribute__((naked)) static void ah_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _ah_stub_impl\n\taddl $24,%esp\n\tmovl _ah_ecx,%ecx\n\tret\n\t");}
AH_STUB(0) AH_STUB(1) AH_STUB(2) AH_STUB(3) AH_STUB(4) AH_STUB(5) AH_STUB(6) AH_STUB(7) AH_STUB(8) AH_STUB(9)
AH_STUB(10) AH_STUB(11) AH_STUB(12) AH_STUB(13)
#undef AH_STUB
static void *ah_stubs[AH_IDS]={ah_stub_0,ah_stub_1,ah_stub_2,ah_stub_3,ah_stub_4,ah_stub_5,ah_stub_6,ah_stub_7,
    ah_stub_8,ah_stub_9,ah_stub_10,ah_stub_11,ah_stub_12,ah_stub_13};
/* Direct callees of each hook (bit i = ah_addresses[i]). */
static unsigned ah_which(unsigned index){
    if(index==ES2_HOOK_INDEX_threat_sum)return 1u<<10;
    if(index==ES2_HOOK_INDEX_engage_done)return 1u|1u<<1;
    if(index==ES2_HOOK_INDEX_flank_step)return 1u<<2|1u<<3|1u<<4;
    if(index==ES2_HOOK_INDEX_retreat_check)return 1u<<8|1u<<9|1u<<11;
    if(index==ES2_HOOK_INDEX_part_pick)return 1u<<5|1u<<6;
    if(index==ES2_HOOK_INDEX_drive_apply)return 1u<<7;
    return 0;
}
static void ah_reset(void){ah_calls=0;ah_cursor=0;ah_stream=ah_seed;}
static uint16_t ah_word(const uint16_t *values,unsigned count){return random32()%4?values[random32()%count]:(uint16_t)random32();}
static BYTE ah_flag(void){return (BYTE)(random32()%4?0:random32()%2?1:random32());}
/* One actor (random bytes otherwise): vtable, group, script table, out
 * flags, attacker count, target, type, configuration, part block,
 * W array, order state and target, class limit, +288h, strength, +30Bh. */
static void ah_actor(unsigned i,int real){
    BYTE *p=ah_host(AH_ACTOR(i));const uint32_t b=AH_BASE;
    put32(p,b+AH_VT);put32(p+0x45,AH_GROUP(random32()%2));put32(p+0x4d,random32()%5?b+AH_TBL+0x20u*(random32()%4):0);
    p[0x99]=ah_flag();p[0xa4]=ah_flag();p[0xa5]=ah_flag();p[0xb4]=ah_flag();p[0x66]=(BYTE)(random32()%2);
    static const uint16_t counts[]={0,1,2,3,4,2,3,0xffff};
    put16(p+0x1a2,ah_word(counts,8));
    put32(p+0x1a4,AH_ACTOR(random32()%3==0?0:1+random32()%(AH_ACTORS-1)));
    static const uint16_t types[]={0,1,2,3,1,3,4};
    put16(p+0x1a8,ah_word(types,7));put32(p+0x1f2,b+AH_CFG+0x40u*(random32()%4));
    put32(p+0x202,b+AH_R202);put32(p+0x206,b+AH_CUR);put32(p+0x20a,b+AH_GVAL);put32(p+0x212,b+AH_TAB);
    put32(p+0x20e,b+AH_W);
    static const uint16_t states[]={4,4,3,5,6,0};
    put16(p+0x23e,ah_word(states,6));put32(p+0x248,random32()%3==0?0:AH_ACTOR(random32()%AH_ACTORS));
    static const uint16_t limits[]={0,1,2,0xffff,0x7fff};
    put16(p+0x250,ah_word(limits,5));
    put32(p+0x288,random32()%2?0x64u+ah_small(random32(),1):random32());
    put16(p+0x29e,(uint16_t)(random32()%4?ah_small(random32(),40):random32()));
    put32(p+0x30b,random32()%3==0?0:b+AH_R30B);
    if(real){for(unsigned j=0;j<3;++j)put32(p+0x26+4*j,ah_small(random32(),1u<<20));}
}
/* The arena for hook index; a[] and r[] are the slots and entry registers. */
static void ah_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    const uint32_t b=AH_BASE,A=AH_ACTOR(0);BYTE *o=ah_host(A);
    ah_seed=random32();random_bytes(ah_arena,AH_SIZE);memset(a,0,24);ah_s=0;
    for(unsigned i=0;i<3;++i)r[i]=random32();
    for(unsigned i=0;i<AH_ACTORS;++i)ah_actor(i,real);
    put32(ah_arena+AH_VT+0x3c,(uint32_t)(uintptr_t)ah_stubs[12]);put32(ah_arena+AH_VT+0x40,(uint32_t)(uintptr_t)ah_stubs[13]);
    /* Groups: byte +12h, current order index +6Ch and four order slots. */
    for(unsigned g=0;g<2;++g){BYTE *p=ah_host(AH_GROUP(g));
        p[0x12]=(BYTE)(random32()%2?0:random32()%2?1:random32());put32(p+0x6c,random32()%4);
        for(unsigned s=0;s<4;++s)put32(p+0x44+4*s,random32()%4?b+AH_ORD+0x10u*(random32()%8):0);}
    static const uint16_t scopes[]={0,1,2,3,0xffff,7};
    for(unsigned k=0;k<8;++k){BYTE *q=ah_arena+AH_ORD+0x10*k;put16(q+0xc,ah_word(scopes,6));
        put32(q+0xe,random32()%2?AH_ACTOR(random32()%AH_ACTORS):AH_GROUP(random32()%2));}
    for(unsigned k=0;k<4;++k)put16(ah_arena+AH_CFG+0x40*k+0x2e,(uint16_t)(random32()%2?0:random32()%2?1:random32()));
    for(unsigned k=0;k<4;++k){BYTE *q=ah_arena+AH_TBL+0x20*k;q[0xc]=(BYTE)(random32()%3==0);q[0xd]=(BYTE)(random32()%3==0);}
    /* Part block: current words (all negative, all zero or random),
     * nonzero maxima, group values and 32 group entries with no parts. */
    unsigned mode=random32()%4;
    for(unsigned k=0;k<0x20;++k){
        uint16_t c=mode==0?(uint16_t)(0x8000u|random32()):mode==1?0:(uint16_t)(random32()%2?random32()%0x200:random32());
        put16(ah_arena+AH_CUR+2*k,c);
        uint16_t m=(uint16_t)(random32()%3?1u+random32()%0x200u:random32());if(!m)m=1;
        if(real&&(m&0x8000u))m&=0x7fffu;if(!m)m=1;
        put16(ah_arena+AH_MAXW+2*k,m);
        put16(ah_arena+AH_GVAL+2*k,(uint16_t)(random32()%3?random32()%0x400:random32()));
        BYTE *e=ah_arena+AH_ENTRY+18*k;put16(e,(uint16_t)(1u+random32()%0x7ffeu));put16(e+6,0);
    }
    put32(ah_arena+AH_TAB,b+AH_ENTRY);put32(ah_arena+AH_TAB+4,b+AH_MAXW);
    for(unsigned k=0;k<0x40;++k)put16(ah_arena+AH_W+2*k,(uint16_t)(random32()%3?random32()%2:random32()));
    put16(ah_arena+AH_R202+8,(uint16_t)ah_small(random32(),random32()%4?3u:12u));
    static const uint16_t closes[]={0xa9,0xaa,0xab,0,0x8000};
    put16(ah_arena+AH_R30B+0x7f,ah_word(closes,5));
    /* The MECH list: a random subset, linked through the node headers
     * (+0 next, +4 previous); header +0Eh = the first node. The stub
     * returns it tail first, as 471c70 does. */
    unsigned order[AH_ACTORS],count=random32()%(AH_ACTORS+1);
    for(unsigned i=0;i<AH_ACTORS;++i)order[i]=i;
    for(unsigned i=AH_ACTORS-1;i>0;--i){unsigned j=random32()%(i+1),x=order[i];order[i]=order[j];order[j]=x;}
    put32(ah_arena+AH_LIST+0xe,count?AH_ACTOR(order[0])-8u:0);
    for(unsigned i=0;i<count;++i){BYTE *node=ah_host(AH_ACTOR(order[i])-8u);
        put32(node,i+1<count?AH_ACTOR(order[i+1])-8u:0);put32(node+4,i?AH_ACTOR(order[i-1])-8u:0);}
    for(unsigned i=0;i<count;++i)ah_seq[i]=AH_ACTOR(order[count-1-i]);ah_seq[count]=0;
    put32(mapped+0xa9b7a,b+AH_LIST);
    /* S record: drive word, magnitude, distance words and bounds. */
    BYTE *s=ah_arena+AH_S;
    static const uint16_t deltas[]={0x8000,0,1,0xffff,0x7fff,0x40,0xffc0};
    static const uint16_t mags[]={0x4000,0x3fff,0x4001,0,0x8000};
    static const uint16_t dists[]={0x4000,0x3fff,0x6000,0x5fff,0x6001,0x4001};
    static const uint16_t drives[]={0,1,0xffff,0x100,0xff00};
    put16(s,(uint16_t)random32());put16(s+2,ah_word(deltas,7));put16(s+4,ah_word(mags,5));put16(s+8,ah_word(dists,6));
    int32_t lo_bound=(int32_t)ah_small(random32(),1000),hi_bound=lo_bound+(int32_t)(random32()%2000);
    put32(s+0x10,(uint32_t)lo_bound);put32(s+0x14,(uint32_t)hi_bound);
    int32_t edges[]={lo_bound-1,lo_bound,lo_bound+1,hi_bound-1,hi_bound,hi_bound+1};
    put32(s+0xa,random32()%4?(uint32_t)edges[random32()%6]:random32());put16(s+0xe,ah_word(drives,5));
    if(real)put16(mapped+0xd3b64,(uint16_t)(1+random32()%200));
    a[0]=A;
    if(index==ES2_HOOK_INDEX_point_offset){
        a[0]=random32()%4?b+AH_POINT+random32()%16:b+0x10u+random32()%(AH_SIZE-0x20u);
        static const uint16_t angles[]={0,0x4000,0x8000,0xc000,0x3ff8,0x4008,0x7ff7,0xfff8};
        a[1]=random32()%2?(random32()&0xffff0000u)|angles[random32()%8]:random32();
        static const uint16_t lengths[]={0x3a98,0x4e20,0x7fff,0x8000,0xffff,1,0};
        a[2]=random32()%2?(random32()&0xffff0000u)|lengths[random32()%7]:random32();
    } else if(index==ES2_HOOK_INDEX_threat_sum){
        a[0]=random32()%4?AH_ACTOR(random32()%AH_ACTORS):random32();
        for(unsigned i=0;i<AH_ACTORS;++i)if(random32()%2)put32(ah_host(AH_ACTOR(i))+0x1a4,a[0]);
    } else if(index==ES2_HOOK_INDEX_flank_step){
        a[1]=ah_s=b+AH_S;
    } else if(index==ES2_HOOK_INDEX_drive_apply){
        /* S apart, or overlapping A+5Ah so that the drive word reread at
         * 41e8b0 can see zero (byte +5Ch zero, a drive word below 100h). */
        unsigned overlap=random32()%8;
        if(overlap<2){
            a[1]=A+0x5au-0xdu-overlap;o[0x5c]=(BYTE)(random32()%2?0:random32());
            put16(ah_host(a[1])+0xe,(uint16_t)(random32()%2?1u+random32()%0xffu:random32()));
        } else a[1]=b+AH_S;
        ah_s=a[1];
    }
    (void)n;
}
static int ah_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const ah_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,ah_calls);
    for(unsigned j=0;j<calls&&j<ah_calls&&expected;++j)if(memcmp(expected+j,ah_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(ah_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<AH_SIZE;++j)if(ah_after[j]!=ah_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,ah_after[j],ah_arena[j]);break;}
    return 1;
}
static unsigned ah_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<ah_calls;++j){unsigned id=ah_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?ah_slots[id]==c->key:!ah_slots[id]&&ah_addresses[id]==c->key)++hits;}
    return hits;
}
static int ah_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[AH_GLOBALS],after[AH_GLOBALS],actual[AH_GLOBALS];static ah_record expected[AH_MAX];
    memcpy(ah_before,ah_arena,AH_SIZE);ah_globals(before,0);
    ah_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=ah_calls;memcpy(expected,ah_log,calls*sizeof(*expected));
    memcpy(ah_after,ah_arena,AH_SIZE);ah_globals(after,0);
    memcpy(ah_arena,ah_before,AH_SIZE);ah_globals(before,1);ah_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);ah_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=ah_expected_hits(c+i))return fail("ai helpers callee counter delta");
    if(pair[0]!=pair[1]||calls!=ah_calls||memcmp(expected,ah_log,calls*sizeof(*expected))||memcmp(ah_after,ah_arena,AH_SIZE)||
       memcmp(after,actual,AH_GLOBALS))
        return ah_report("direct",n,pair,calls,expected);
    ah_globals(before,1);
    return 0;
}
static int ah_patch(BYTE saved[AH_IDS][5],int active[AH_IDS],unsigned which,int on){
    for(unsigned i=0;i<AH_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!ah_addresses[i])continue;if(actor_patch(ah_addresses[i]-0x400000u,ah_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(ah_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
/* Calls target with four slots, EAX/EDX/ECX/EBX = pattern and a patterned
 * stack below the arguments; returns EDX:EAX in pair. */
__attribute__((naked,noinline)) static void ah_invoke(void *target,const uint32_t *a,uint32_t pattern,uint32_t pair[2]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 16(%ebp),%eax\n\tmovl $1024,%ecx\n\trep stosl\n\taddl $4080,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%eax\n\tmovl %eax,%edx\n\tmovl %eax,%ecx\n\tmovl %eax,%ebx\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%ecx\n\tmovl %eax,(%ecx)\n\tmovl %edx,4(%ecx)\n\tleal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static int ah_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;ah_build(index,n,a,r,0);
        BYTE before[AH_GLOBALS],after[AH_GLOBALS],actual[AH_GLOBALS],expected_fpu[108],actual_fpu[108];static ah_record expected[AH_MAX];
        memcpy(ah_before,ah_arena,AH_SIZE);ah_globals(before,0);
        ah_reset();ah_invoke(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=ah_calls;memcpy(expected,ah_log,calls*sizeof(*expected));memcpy(ah_after,ah_arena,AH_SIZE);ah_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("ai helpers poison install");
        memcpy(ah_arena,ah_before,AH_SIZE);ah_globals(before,1);ah_reset();ah_invoke(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);ah_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=ah_calls||memcmp(expected,ah_log,calls*sizeof(*expected))||memcmp(ah_after,ah_arena,AH_SIZE)||
           memcmp(after,actual,AH_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=ah_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("ai helpers poison uninstall");ah_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state\n",d->name,total);
    return status;
}
/* Real callees with the nested hooks in rotating original/replaced/compare
 * modes (compare falls back to replaced where refused); stubs that stay:
 * the virtuals (arena vtable) everywhere, order_resume 41eb34 under
 * retreat_check (its subtree reaches the AI function 41cf18).
 * drive_apply has no real pass (vehicle_control's subtree is the whole
 * vehicle control; vehicle_control_cases.h covers it). */
static unsigned ah_nested(unsigned index,unsigned nested[4],unsigned *keep){
    unsigned count=0;*keep=0;
    if(index==ES2_HOOK_INDEX_engage_done){nested[count++]=ES2_HOOK_INDEX_target_class;nested[count++]=ES2_HOOK_INDEX_order_target;}
    else if(index==ES2_HOOK_INDEX_flank_step){nested[count++]=ES2_HOOK_INDEX_timer_word_step;nested[count++]=ES2_HOOK_INDEX_point_offset;
        nested[count++]=ES2_HOOK_INDEX_heading_point;}
    else if(index==ES2_HOOK_INDEX_retreat_check){nested[count++]=ES2_HOOK_INDEX_threat_sum;nested[count++]=ES2_HOOK_INDEX_script_start;
        *keep=1u<<9;}
    else if(index==ES2_HOOK_INDEX_part_pick){nested[count++]=ES2_HOOK_INDEX_rng_next;nested[count++]=ES2_HOOK_INDEX_part_ratio;}
    return count;
}
static int ah_real_pass(unsigned index){
    unsigned nested[4],keep,count=ah_nested(index,nested,&keep);
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[4]={0};
    BYTE saved[AH_IDS][5];int active[AH_IDS];
    if(ah_patch(saved,active,keep,1))return 1;
    static ah_record expected[AH_MAX];

    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;ah_build(index,n,a,r,1);
        /* retreat_check's count: the real threat_sum may return any sum,
         * so the division never sees -1 or 0 (the original faults). */
        BYTE before[AH_GLOBALS],after[AH_GLOBALS],actual[AH_GLOBALS];
        memcpy(ah_before,ah_arena,AH_SIZE);ah_globals(before,0);
        es2_hook hk[5];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("ai helpers nested install");}
        ah_reset();ah_invoke(mapped+d->rva,a,pattern,expected_pair);
        unsigned calls=ah_calls;memcpy(expected,ah_log,calls*sizeof(*expected));
        memcpy(ah_after,ah_arena,AH_SIZE);ah_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+4,d,&o,function_hook_extension(index)))return fail("ai helpers caller install");
        memcpy(ah_arena,ah_before,AH_SIZE);ah_globals(before,1);ah_reset();
        ah_invoke(mapped+d->rva,a,pattern,actual_pair);ah_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=ah_calls||memcmp(expected,ah_log,calls*sizeof(*expected))||memcmp(ah_after,ah_arena,AH_SIZE)||
           memcmp(after,actual,AH_GLOBALS)){
            fprintf(stderr,"real mode=%u\n",mode);status=ah_report("real",n,pair,calls,expected);
        }
        for(unsigned j=0;j<5;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("ai helpers nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("ai helpers nested uninstall");}
        ah_globals(before,1);++total;
    }

    if(ah_patch(saved,active,0,0))return 1;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, 31/a7 stack, pair, memory, globals and stub inputs\n",
        d->name,total,count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
/* Compare cases (compare_probes.h) for point_offset (span P[0..7], EAX = P
 * remapped by its comparator). */
static void ah_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){ah_build(index,n,a,r,1);}
static void ah_compare_state(BYTE *s,int restore){
    if(restore){memcpy(ah_arena,s,AH_SIZE);ah_globals(s+AH_SIZE,1);}else{memcpy(s,ah_arena,AH_SIZE);ah_globals(s+AH_SIZE,0);}
}
static int ai_helpers_cases(unsigned index,unsigned limit){
    unsigned which=ah_which(index);
    BYTE saved[AH_IDS][5],old_globals[AH_GLOBALS];int active[AH_IDS];
    ah_globals(old_globals,0);
    if(limit&&compare_probe_installed(index)){int status=compare_probe_run(index,limit,ah_compare_build,ah_compare_state,AH_SIZE+AH_GLOBALS);
        ah_globals(old_globals,1);return status;}
    if(ah_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_AI_HELPERS_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];ah_build(index,n,a,r,0);status=ah_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=ah_installed_probes(index,64);
    if(ah_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status&&index!=ES2_HOOK_INDEX_point_offset&&index!=ES2_HOOK_INDEX_drive_apply)status=ah_real_pass(index);
    if(!limit&&!status&&compare_probe_capable(index))status=compare_probe_run(index,0,ah_compare_build,ah_compare_state,AH_SIZE+AH_GLOBALS);
    ah_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, frame payloads by content, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
