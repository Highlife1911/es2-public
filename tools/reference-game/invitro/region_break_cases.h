/* Region damage: region_hit 40da38 and region_break 40d434
 * (docs/reverse-engineering/hit_tests.md, "region damage 40da38 and part break
 * 40d434"). Direct cases run the original body through an own cdecl caller that
 * hands over and returns EAX/EDX/ECX, then the adapter, then the core directly
 * (for ECX), against logging stubs for every game callee: 49630c (the record
 * stub of base_cases.h, keeping the frame for frame-relative comparisons),
 * region_find, region_spread, fire_release, fire_alloc, fire_create,
 * euler_matrix, the node lookup (virtual +20h in an arena vtable),
 * matrix_compose, 4801d4, pool_alloc, explosion_create, debris_table and
 * debris_pick under region_break; region_absorb, region_spread and region_break
 * under region_hit. Logged: ordered calls with their arguments and entry
 * EAX/EDX/ECX (frame addresses by offset, other stack addresses masked), the
 * slots the bridge pushed, the record's context word at every call, by content
 * the point and the pose handed to 4801d4, the explosion position, the debris
 * word and pose, the damage word. The stubs answer near the tested values (o
 * around -1, a missing entry only for a broken part, null objects, node index
 * words around -1, AX zero or not) and change what the originals reread after a
 * call (the actor pointer, the configuration and intact-word pointers, the part
 * record bytes, the flag word 4a982c, the shape instance, the counter and pool
 * pointers, the part count, the cascade stack and its list) and what they must
 * not reread (the cached record R). The arena spans three 64-KiB regions so
 * that the high words of lo(R, w) registers tell their sources apart. Then
 * installed 31/a7 poison probes and real-callee passes: region_find,
 * region_entry, region_spread, region_absorb, rng_bounded, rng_next,
 * euler_matrix, matrix_compose, pool_alloc, debris_table (and region_break
 * under region_hit) in rotating modes over consistent game-like tables, the
 * real node lookups 476850/47622c over an own tree and the real 4801d4; the
 * fire, explosion and debris-pick callees stay stubs. Identifiers:
 * rbk_/RBK_. */
typedef struct rbk_record { uint32_t id,args[5],regs[3],input[12],eax,edx,context,count; } rbk_record;
enum { RBK_W=0x1000, RBK_MAX=64, RBK_IDS=16, RBK_GLOBALS=256,
    /* window 0: damage state and tables (part 0's entries at the *0 offsets) */
    RBK_D=0x000, RBK_WORDS0=0x040, RBK_WORDS0B=0x0c0, RBK_INTACT0=0x140, RBK_INTACT0B=0x1c0, RBK_V=0x220, RBK_V2=0x240,
    RBK_LIMITS=0x260, RBK_C=0x280, RBK_C2=0x288, RBK_S=0x290, RBK_SRECS=0x2a0, RBK_E=0x2c0, RBK_E2=0x2d0,
    RBK_REC0=0x380, RBK_REC0B=0x5e0, RBK_SENTRIES=0xb80, RBK_NODE=0x860, RBK_NODE2=0x870, RBK_RD=0x880, RBK_RD2=0x890,
    RBK_COUNTER=0x8a0, RBK_COUNTER2=0x8a8, RBK_WEIGHTS=0x8c0, RBK_POOL=0x900, RBK_POOLNODES=0xa00,
    /* window 1: the actors, shape instances, node tree and state words */
    RBK_A=0x000, RBK_A2=0x040, RBK_H=0x080, RBK_H2=0x0a0, RBK_L=0x0c0, RBK_VT=0x0e0, RBK_VTLEAF=0x110, RBK_CHILD0=0x140,
    RBK_CHILD1=0x150, RBK_CHILDREN=0x160, RBK_STATE=0x200, RBK_NODES=0x400,
    /* window 2: the cascade stacks */
    RBK_Q=0x000, RBK_Q2=0x010, RBK_LIST=0x200, RBK_LIST2=0x600 };
static BYTE rbk_space[0x40000],*rbk_w0,*rbk_w1,*rbk_w2,rbk_before[3][RBK_W],rbk_after[3][RBK_W];
#define RBK_B0 ((uint32_t)(uintptr_t)rbk_w0)
#define RBK_B1 ((uint32_t)(uintptr_t)rbk_w1)
#define RBK_B2 ((uint32_t)(uintptr_t)rbk_w2)
static void rbk_place(void){
    uintptr_t base=((uintptr_t)rbk_space+0xffffu)&~(uintptr_t)0xffffu;
    rbk_w0=(BYTE *)base;rbk_w1=(BYTE *)(base+0x10000u);rbk_w2=(BYTE *)(base+0x20000u);
}
static BYTE *rbk_win(unsigned i){return i==0?rbk_w0:i==1?rbk_w1:rbk_w2;}
static void rbk_save(void){for(unsigned i=0;i<3;++i)memcpy(rbk_before[i],rbk_win(i),RBK_W);}
static void rbk_restore(void){for(unsigned i=0;i<3;++i)memcpy(rbk_win(i),rbk_before[i],RBK_W);}
static void rbk_keep(void){for(unsigned i=0;i<3;++i)memcpy(rbk_after[i],rbk_win(i),RBK_W);}
static int rbk_memory_differs(void){
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<RBK_W;++j)if(rbk_after[i][j]!=rbk_win(i)[j]){
        fprintf(stderr," window %u +%x %02x/%02x\n",i,j,rbk_after[i][j],rbk_win(i)[j]);return 1;}
    return 0;
}
static rbk_record rbk_log[RBK_MAX];
static unsigned rbk_calls,rbk_index,rbk_breaks;
static int rbk_real;
static uint32_t rbk_seed,rbk_stream,rbk_rec;
/* rbk_frame: the EBP the record stub saw (the original's frame, or the
 * replacement's synthetic EBP), so frame addresses compare by offset. */
__attribute__((used)) static uint32_t rbk_ecx,rbk_frame;
/* 0 49630c (record), 1 region_find, 2 region_spread, 3 fire_release,
 * 4 fire_alloc, 5 fire_create, 6 euler_matrix, 7 node lookup (slot +20h,
 * arena vtable), 8 matrix_compose, 9 4801d4, 10 pool_alloc,
 * 11 explosion_create, 12 debris_table, 13 debris_pick, 14 region_absorb,
 * 15 region_break. */
static const uint32_t rbk_addresses[RBK_IDS]={0x49630c,0x40ca8c,0x40cf44,0x46b62c,0x46b430,0x46b48c,0x47ebb8,0,0x47fa20,
    0x4801d4,0x471b30,0x407f1c,0x4089bc,0x4086bc,0x40d3ec,0x40d434};
static const unsigned rbk_slots[RBK_IDS]={0,0,0,0,0,0,0,0x20,0,0,0,0,0,0,0,0};
static const unsigned rbk_counts[RBK_IDS]={0,3,4,1,1,5,2,2,3,3,1,5,1,4,3,2};
static uint32_t rbk_next(void){
    uint32_t z=rbk_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *rbk_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int rbk_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
/* Recorded words of two runs (frames fe, fa) are equal when they are the
 * same value, or both frame addresses (f-100h..f+0Fh) at the same offset
 * from their frame, or both other stack addresses. */
static int rbk_near(uint32_t value,uint32_t frame){return frame&&value-(frame-0x100u)<0x110u;}
static int rbk_word_equal(uint32_t e,uint32_t a,uint32_t fe,uint32_t fa){
    if(e==a)return 1;
    int ne=rbk_near(e,fe),na=rbk_near(a,fa);
    if(ne||na)return ne&&na&&e-fe==a-fa;
    return rbk_stack(e)&&rbk_stack(a);
}
static int rbk_record_equal(const rbk_record *e,const rbk_record *a,uint32_t fe,uint32_t fa){
    if(e->id!=a->id||memcmp(e->input,a->input,sizeof(e->input))||e->eax!=a->eax||e->edx!=a->edx||e->context!=a->context||
       e->count!=a->count)return 0;
    for(unsigned j=0;j<5;++j)if(!rbk_word_equal(e->args[j],a->args[j],fe,fa))return 0;
    for(unsigned j=0;j<3;++j)if(!rbk_word_equal(e->regs[j],a->regs[j],fe,fa))return 0;
    return 1;
}
static int rbk_trace_differs(const rbk_record *expected,unsigned calls,uint32_t fe){
    if(calls!=rbk_calls)return 1;
    for(unsigned j=0;j<calls;++j)if(!rbk_record_equal(expected+j,rbk_log+j,fe,rbk_frame))return 1;
    return 0;
}
/* The point (three words) and the pose (nine words, mode byte 18, the
 * translation; byte 19 is copied, never computed with, and left out). */
static void rbk_point(rbk_record *v,unsigned at,uint32_t address){
    const BYTE *p=rbk_host(address);v->input[at]=get32(p);v->input[at+1]=get16(p+4);
}
static void rbk_pose(rbk_record *v,unsigned at,uint32_t address){
    const BYTE *p=rbk_host(address);
    for(unsigned j=0;j<4;++j)v->input[at+j]=get32(p+4*j);
    v->input[at+4]=get16(p+16)|(uint32_t)p[18]<<16;
    for(unsigned j=0;j<3;++j)v->input[at+5+j]=get32(p+20+4*j);
}
static BYTE *rbk_R(void){return rbk_host(rbk_rec);}
static void rbk_actor(uint32_t pick){put32(rbk_w0+RBK_D+0x14,RBK_B1+(pick&1?RBK_A2:RBK_A));}
static void rbk_toggle_big(uint32_t pick){put16(mapped+0xa982c,(uint16_t)(pick&1?0:1+(pick>>1&0xff)));}
static BYTE rbk_state_byte(uint32_t pick){static const BYTE v[]={0xff,0x80,0,1,5,0x7f,0xfe,0x40};return v[pick%8];}
static BYTE rbk_debris_byte(uint32_t pick){static const BYTE v[]={0xff,0xff,0,1,2,5,0x80,0x7f};return v[pick%8];}
static uint16_t rbk_count(uint32_t pick){static const uint16_t v[]={0,1,2,3,5,8,12,24,0xffff,0x8000};return v[pick%10];}
static uint16_t rbk_o(uint32_t pick){static const uint16_t v[]={0,1,2,3,5,0x7fff,0xffff,0xfffe,0x8000,0x8001};return v[pick%10];}
__attribute__((used,noinline)) static uint64_t __cdecl rbk_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(rbk_calls==RBK_MAX){fail("region_break synthetic call overflow");ExitProcess(1);}
    rbk_record *v=rbk_log+rbk_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,4*rbk_counts[id]);memcpy(v->regs,regs,12);
    v->count=weapons_bridge_slots(a,rbk_counts[id]);
    if(id&&rbk_frame)v->context=get16(rbk_host(rbk_frame-0x24u));
    uint32_t eax=rbk_next(),edx=rbk_next(),pick=rbk_next();rbk_ecx=rbk_next();
    const uint32_t b0=RBK_B0,b2=RBK_B2;BYTE *d=rbk_w0+RBK_D;
    switch(id){
    case 0: /* 49630c record stub returns the real FS:0 head for rbk_stub_0. */
        __asm__ volatile("movl %%fs:0,%0":"=r"(eax));break;
    case 1: { /* region_find: an entry and its word o; a miss only where the
               * part is broken (the original faults otherwise). Pokes of the
               * pointers read after it. */
        if(!rbk_real){
            if(pick%5==0)put32(d+0xc,b0+(pick>>4&1?RBK_C2:RBK_C));
            if(pick%7==0)put32(d+8,b0+(pick>>5&1?RBK_INTACT0B:RBK_INTACT0));
            if(pick%11==0)put32(d+4,b0+(pick>>6&1?RBK_WORDS0B:RBK_WORDS0));
        }
        if(pick%8==0){eax=0;put16(rbk_host(get32(d+8)+2u*a[1]),0);}
        else {eax=b0+(pick>>3&1?RBK_E2:RBK_E);put16(rbk_host(a[2]),rbk_o(pick>>9));}
        break; }
    case 2: /* region_spread: AX zero or not; pokes of the part record's
             * bytes (R stays cached), the flag word and the actor. */
        if(pick%3==0)eax&=0xffff0000u;else if(pick%3==1)eax=1;
        if(rbk_real)break;
        if(pick%5==0)rbk_R()[3]=rbk_state_byte(pick>>8);
        if(pick%7==0)rbk_R()[5]=(BYTE)(pick>>12);
        if(rbk_index==ES2_HOOK_INDEX_region_break){
            if(pick%11==0)rbk_toggle_big(pick>>16);
            if(pick%13==0)rbk_actor(pick>>18);
        }
        break;
    case 3: /* fire_release: the actor (fire_create rereads it). */
        if(!rbk_real&&pick%3==0)rbk_actor(pick>>4);
        break;
    case 4: /* fire_alloc: the fire or 0. */
        eax=pick%4==0?0:eax|1u;
        if(!rbk_real&&pick%5==0)rbk_actor(pick>>4);
        break;
    case 5: /* fire_create: pokes of the state index (kept 0..7Fh: the
             * original took the path for a non-negative one), the actor,
             * its shape instance and the counter pointer. */
        if(rbk_real)break;
        if(pick%3==0)rbk_R()[3]=(BYTE)(pick>>4&0x7f);
        if(pick%5==0)rbk_actor(pick>>8);
        if(pick%7==0)put32(rbk_host(get32(d+0x14))+0x34,RBK_B1+(pick>>9&1?RBK_H2:RBK_H));
        if(pick%11==0)put32(mapped+0x2cbee4,b0+(pick>>10&1?RBK_COUNTER2:RBK_COUNTER));
        break;
    case 6: /* euler_matrix: the rotation out in a third; pokes of the actor
             * pointer (the copy uses the cached pose) and the translation. */
        if(pick%3==0)for(unsigned j=0;j<20;++j)rbk_host(a[1])[j]=(BYTE)rbk_next();
        if(rbk_real)break;
        if(pick%5==0)rbk_actor(pick>>8);
        if(pick%7==0)put32(rbk_host(a[1])+0x14,rbk_next());
        break;
    case 7: { /* node lookup: 0, or a node whose index word is around -1;
               * pokes of the actor and the node pose pointers. */
        static const uint16_t index[]={0xffff,0xfffe,0,1,2,3,0x7fff,0x8000};
        const unsigned node=pick>>4&1?RBK_NODE2:RBK_NODE;
        eax=pick%4==0?0:b0+node;
        if(eax)put16(rbk_w0+node+4,index[pick>>5&7]);
        if(rbk_real)break;
        if(pick%5==0)rbk_actor(pick>>8);
        if(pick%7==0)put32(rbk_w1+RBK_H+0x16,rbk_next());
        if(pick%11==0)put32(rbk_w1+RBK_H2+0x16,rbk_next());
        break; }
    case 8: /* matrix_compose: the pose out (32 bytes); poke of the flag
             * byte read later. */
        for(unsigned j=0;j<32;++j)rbk_host(a[2])[j]=(BYTE)rbk_next();
        if(!rbk_real&&pick%5==0)rbk_R()[5]=(BYTE)(pick>>8);
        break;
    case 9: /* 4801d4: the point and the pose by content, the output; pokes
             * of the flag word (read right after), the flag byte, the
             * actor, the pool pointer and the debris byte. */
        rbk_point(v,0,a[0]);rbk_pose(v,2,a[1]);
        for(unsigned j=0;j<3;++j)put32(rbk_host(a[2])+4*j,rbk_next());
        if(rbk_real)break;
        if(pick%3==0)rbk_toggle_big(pick>>4);
        if(pick%5==0)rbk_R()[5]=(BYTE)(pick>>8);
        if(pick%7==0)rbk_actor(pick>>12);
        if(pick%11==0)put32(mapped+0xa961e,rbk_next());
        if(pick%13==0)rbk_R()[2]=rbk_debris_byte(pick>>16);
        break;
    case 10: /* pool_alloc: the explosion object or 0. */
        eax=pick%4==0?0:eax|1u;
        if(rbk_real)break;
        if(pick%5==0)rbk_actor(pick>>8);
        if(pick%7==0)rbk_R()[2]=rbk_debris_byte(pick>>12);
        break;
    case 11: /* explosion_create: the position by content; pokes of the
              * debris byte and the counter pointer. */
        for(unsigned j=0;j<3;++j)v->input[j]=get32(rbk_host(a[2])+4*j);
        if(rbk_real)break;
        if(pick%5==0)rbk_R()[2]=rbk_debris_byte(pick>>8);
        if(pick%7==0)put32(mapped+0x2cbee4,b0+(pick>>12&1?RBK_COUNTER2:RBK_COUNTER));
        break;
    case 12: /* debris_table: the word by content, lowered in a third; one
              * of two records. */
        v->input[0]=get16(rbk_host(a[0]));
        if(pick%3==0)put16(rbk_host(a[0]),(uint16_t)(v->input[0]-(pick>>8&3u)));
        eax=b0+(pick>>4&1?RBK_RD2:RBK_RD);
        break;
    case 13: /* debris_pick: the pose by content; pokes of what the push loop
              * reads. */
        rbk_pose(v,0,a[2]);
        if(rbk_real)break;
        if(pick%5==0)put16(d+0x18,rbk_count(pick>>8));
        if(pick%7==0)put32(d+0xc,b0+(pick>>12&1?RBK_C2:RBK_C));
        if(pick%11==0)put32(d+8,b0+(pick>>13&1?RBK_INTACT0B:RBK_INTACT0));
        if(pick%13==0)put32(mapped+0x98864,b2+(pick>>14&1?RBK_Q2:RBK_Q));
        break;
    case 14: /* region_absorb: the damage word by content, a new one in a
              * third; AX zero or not; pokes of [D+0Ch] and [D]. */
        v->input[0]=get16(rbk_host(a[2]));
        if(pick%3==0)put16(rbk_host(a[2]),(uint16_t)rbk_next());
        if((pick>>2)%3==0)eax&=0xffff0000u;else if((pick>>2)%3==1)eax=1;
        if(rbk_real)break;
        if(pick%5==0)put32(d+0xc,b0+(pick>>8&1?RBK_C2:RBK_C));
        if(pick%7==0)put32(d,b0+(pick>>9&1?RBK_V2:RBK_V));
        break;
    case 15: { /* region_break under region_hit: pushes on the current stack
                * and drains it; pokes of the stack pointer and its list. At
                * most seven calls per case. */
        if(rbk_real)break;
        BYTE *qp=rbk_host(get32(mapped+0x98864));
        if(++rbk_breaks>6){rbk_w2[RBK_Q+5]=0;rbk_w2[RBK_Q2+5]=0;break;}
        if(pick%3==0){uint8_t c=qp[5];put16(rbk_host(get32(qp)+2u*(uint32_t)(int32_t)(int8_t)c),(uint16_t)(rbk_next()%40u));qp[5]=(BYTE)(c+1);}
        if(pick%4==0)qp[5]=0;
        if(pick%11==0)put32(mapped+0x98864,b2+(pick>>8&1?RBK_Q2:RBK_Q));
        if(pick%13==0)put32(qp,b2+(pick>>9&1?RBK_LIST2:RBK_LIST));
        break; }
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
/* Pushes the entry EAX/EDX/ECX for the record before the slot pointer. */
#define RBK_STUB(n) __attribute__((naked)) static void rbk_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _rbk_stub_impl\n\taddl $24,%esp\n\tmovl _rbk_ecx,%ecx\n\tret\n\t");}
RBK_STUB(1) RBK_STUB(2) RBK_STUB(3) RBK_STUB(4) RBK_STUB(5) RBK_STUB(6) RBK_STUB(7) RBK_STUB(8) RBK_STUB(9) RBK_STUB(10)
RBK_STUB(11) RBK_STUB(12) RBK_STUB(13) RBK_STUB(14) RBK_STUB(15)
#undef RBK_STUB
/* 49630c: keeps EBP in rbk_frame, captures the entry registers like the
 * other stubs, then links a record at EBP + [descriptor+4] into FS:0 with
 * the saved head at record+0 and a zero context word at record+16, as the
 * original does. */
__attribute__((naked)) static void rbk_stub_0(void){__asm__ volatile(
    "movl %ebp,_rbk_frame\n\t"
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
    "pushl $0\n\tcalll _rbk_stub_impl\n\taddl $12,%esp\n\t"
    "popl %ecx\n\tmovl 4(%ecx),%ecx\n\taddl %ebp,%ecx\n\tmovl %eax,(%ecx)\n\tmovw $0,16(%ecx)\n\tmovl %ecx,%fs:0\n\t"
    "popl %edx\n\taddl $4,%esp\n\tmovl _rbk_ecx,%ecx\n\tret\n\t");}
static void *rbk_stubs[RBK_IDS]={rbk_stub_0,rbk_stub_1,rbk_stub_2,rbk_stub_3,rbk_stub_4,rbk_stub_5,rbk_stub_6,rbk_stub_7,
    rbk_stub_8,rbk_stub_9,rbk_stub_10,rbk_stub_11,rbk_stub_12,rbk_stub_13,rbk_stub_14,rbk_stub_15};
/* Direct callees of each hook (bit i = rbk_addresses[i]; the lookup slot is
 * in the arena vtable and never patched). */
static unsigned rbk_which(unsigned index){
    if(index==ES2_HOOK_INDEX_region_hit)return 1u<<2|1u<<14|1u<<15;
    return 0x3fffu&~(1u<<7);
}
static int rbk_patch(BYTE saved[RBK_IDS][5],int active[RBK_IDS],unsigned which,int on){
    for(unsigned i=0;i<RBK_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!rbk_addresses[i])continue;if(actor_patch(rbk_addresses[i]-0x400000u,rbk_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(rbk_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
/* The counter pointer, the explosion pool pointer, the flag word, the
 * stack pointer, the debris records 4a964c..4a9659 and RNG streams A and
 * B (4d2599, 4d260b; B is drawn only inside the stubbed callees). */
static const struct { uint32_t rva,size; } rbk_ranges[]={{0x2cbee4,4},{0xa961e,4},{0xa982c,2},{0x98864,4},{0xa964c,14},{0xd2599,0xe4}};
static void rbk_globals(BYTE *out,int restore){
    for(unsigned i=0;i<sizeof(rbk_ranges)/sizeof(rbk_ranges[0]);++i){
        if(restore)memcpy(mapped+rbk_ranges[i].rva,out,rbk_ranges[i].size);else memcpy(out,mapped+rbk_ranges[i].rva,rbk_ranges[i].size);
        out+=rbk_ranges[i].size;
    }
}
static int rbk_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<RBK_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
static void rbk_reset(void){rbk_calls=0;rbk_stream=rbk_seed;rbk_frame=0;rbk_breaks=0;}
/* A realistic pose: rotation words, mode byte 0..2, translation. */
static void rbk_matrix(BYTE *mx){
    for(unsigned j=0;j<9;++j)put16(mx+2*j,(uint16_t)(random32()%32769u-16384u));
    mx[18]=(BYTE)(random32()%3);
    for(unsigned j=0;j<3;++j)put32(mx+20+4*j,random32()%4096u-2048u);
}
/* Pool header (free count +0, used count +2, free head +0Ah, used head
 * +0Eh) with count free nodes of 80h bytes; objects start at node+8. */
static void rbk_pool(uint32_t pool,uint32_t nodes,unsigned count){
    put16(rbk_host(pool),(uint16_t)count);put16(rbk_host(pool)+2,0);put32(rbk_host(pool)+0xe,0);
    for(unsigned i=0;i<count;++i){uint32_t n=nodes+0x80u*i;put32(rbk_host(n),i+1<count?n+0x80u:0);put32(rbk_host(n)+4,i?n-0x80u:0);}
    put32(rbk_host(pool)+0xa,count?nodes:0);
}
/* Real tables: parts 0..7 found by region_find (record 0 holds one entry
 * per part, its id word becomes o); spread entries with live and dead
 * regions; parents form a forest (a smaller index or -1); the node tree
 * (group class 476850 with two leaves 47622c, ids 0..3) and its poses. */
static void rbk_real_tables(void){
    const uint32_t b0=RBK_B0,b1=RBK_B1;
    for(int i=-8;i<24;++i){
        BYTE *rc=rbk_w0+RBK_REC0+18*i;
        put16(rc,(uint16_t)(100+random32()%2000));
        rc[2]=(BYTE)(random32()%3?random32()%3:0xff);
        rc[3]=(BYTE)(random32()%3?random32()%16:0xff);
        rc[4]=(BYTE)(i>0&&random32()%3?random32()%(unsigned)i:0xff);
        rc[5]=(BYTE)(random32()%3?1u|random32():random32());
        put16(rc+6,(uint16_t)(random32()%3?random32()%4:0));
        put32(rc+8,b0+RBK_WEIGHTS+4u*(random32()%8));
    }
    for(unsigned k=0;k<16;++k){
        BYTE *w=rbk_w0+RBK_WEIGHTS+4*k;put16(w,(uint16_t)(random32()%21));put16(w+2,(uint16_t)(random32()%8));
        put16(rbk_w0+RBK_V+2*k,(uint16_t)(random32()%3==0?0xffff:random32()%1000));
        put16(rbk_w0+RBK_LIMITS+2*k,(uint16_t)(100+random32()%2000));
    }
    static const uint16_t ids[]={0xffff,0,1,2,3,0xfffe};
    unsigned records=1+random32()%3;
    put16(rbk_w0+RBK_S,(uint16_t)records);put32(rbk_w0+RBK_S+2,b0+RBK_SRECS);
    for(unsigned r=0;r<records;++r){
        BYTE *sr=rbk_w0+RBK_SRECS+10*r;unsigned count=r?1+random32()%3:8;
        put16(sr,ids[random32()%6]);put16(sr+4,(uint16_t)count);put32(sr+6,b0+RBK_SENTRIES+0x80u*r);
        for(unsigned e=0;e<count;++e){
            BYTE *en=rbk_w0+RBK_SENTRIES+0x80u*r+16*e;
            put16(en+6,(uint16_t)(r?8+random32()%8:e));
            for(unsigned j=0;j<3;++j)put16(en+8+2*j,(uint16_t)(random32()%2048u-1024u));
        }
    }
    /* The node tree, its poses and the debris records. */
    put32(rbk_w1+RBK_VT+0x20,(uint32_t)(uintptr_t)(mapped+0x76850));
    put32(rbk_w1+RBK_VTLEAF+0x20,(uint32_t)(uintptr_t)(mapped+0x7622c));
    put16(rbk_w1+RBK_L+4,(uint16_t)(random32()%5u-1u));put16(rbk_w1+RBK_L+6,(uint16_t)(random32()%4));
    put16(rbk_w1+RBK_L+0x10,(uint16_t)(random32()%3));put32(rbk_w1+RBK_L+0x12,b1+RBK_CHILDREN);
    put32(rbk_w1+RBK_CHILDREN,b1+RBK_CHILD0);put32(rbk_w1+RBK_CHILDREN+4,b1+RBK_CHILD1);
    for(unsigned c=0;c<2;++c){BYTE *child=rbk_w1+(c?RBK_CHILD1:RBK_CHILD0);
        put32(child,b1+RBK_VTLEAF);put16(child+4,(uint16_t)(random32()%5u-1u));put16(child+6,(uint16_t)(random32()%4));}
    for(unsigned j=0;j<4;++j)rbk_matrix(rbk_w1+RBK_NODES+32*j);
    for(unsigned h=0;h<2;++h)put32(rbk_w1+(h?RBK_H2:RBK_H)+0x16,b1+RBK_NODES);
    for(unsigned k=0;k<2;++k){BYTE *A=rbk_w1+(k?RBK_A2:RBK_A);rbk_matrix(A+0x12);}
    put32(rbk_w0+RBK_RD,b0+RBK_RD2);put32(rbk_w0+RBK_RD+6,random32());
    put16(mapped+0xa9654,(uint16_t)(random32()%2?random32()%4:random32()));put32(mapped+0xa964c,b0+RBK_RD);
    BYTE *state=mapped+0xd2599;random_bytes(state,112);state[112]=(BYTE)(random32()%56);state[113]=(BYTE)(random32()%56);
    rbk_pool(b0+RBK_POOL,b0+RBK_POOLNODES,random32()%4);put32(mapped+0xa961e,b0+RBK_POOL);
    put16(mapped+0xa982c,(uint16_t)(random32()%4?0:1));
}
static void rbk_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    const uint32_t b0=RBK_B0,b1=RBK_B1,b2=RBK_B2;BYTE *d=rbk_w0+RBK_D;
    rbk_seed=random32();random_bytes(rbk_w0,RBK_W);random_bytes(rbk_w1,RBK_W);random_bytes(rbk_w2,RBK_W);
    memset(a,0,24);for(unsigned i=0;i<3;++i)r[i]=random32();rbk_index=index;
    put32(d,b0+RBK_V);put32(d+4,b0+RBK_WORDS0);put32(d+8,b0+RBK_INTACT0);put32(d+0xc,b0+RBK_C);put32(d+0x10,b0+RBK_S);
    put32(d+0x14,b1+RBK_A);put16(d+0x18,real?(uint16_t)(random32()%9):rbk_count(random32()));
    put32(rbk_w0+RBK_C,b0+RBK_REC0);put32(rbk_w0+RBK_C+4,b0+RBK_LIMITS);
    put32(rbk_w0+RBK_C2,b0+RBK_REC0B);put32(rbk_w0+RBK_C2+4,random32());
    /* Records of parts -8..23 in both tables; intact words of -16..47. */
    for(unsigned t=0;t<2;++t)for(int i=-8;i<24;++i){
        BYTE *rc=rbk_w0+(t?RBK_REC0B:RBK_REC0)+18*i;
        rc[2]=rbk_debris_byte(random32());rc[3]=rbk_state_byte(random32());
        rc[4]=(BYTE)(random32()%4?random32()%10:random32()%2?0xff:random32());
    }
    for(unsigned t=0;t<2;++t)for(int i=-16;i<48;++i)
        put16(rbk_w0+(t?RBK_INTACT0B:RBK_INTACT0)+2*i,(uint16_t)(random32()%4==0?0:1+random32()%0xffff));
    /* Actors, shape instances, the lookup object, its vtable. */
    for(unsigned k=0;k<2;++k){BYTE *A=rbk_w1+(k?RBK_A2:RBK_A);
        put16(A+0x32,(uint16_t)(random32()%2?0:random32()%3?1:random32()));put32(A+0x34,b1+(random32()%3?RBK_H:RBK_H2));}
    for(unsigned k=0;k<2;++k){BYTE *H=rbk_w1+(k?RBK_H2:RBK_H);
        put32(H+4,b1+RBK_L);put32(H+8,b1+RBK_STATE);if(random32()%2)put32(H+0x16,b1+RBK_NODES);}
    put32(rbk_w1+RBK_L,b1+RBK_VT);put32(rbk_w1+RBK_VT+0x20,(uint32_t)(uintptr_t)rbk_stubs[7]);
    /* The cascade stacks: small counts, or around the signed byte edge. */
    static const BYTE counts[]={0,0,1,2,3,0x7f,0x80,0x81,0xfe,0xff};
    for(unsigned k=0;k<2;++k){BYTE *q=rbk_w2+(k?RBK_Q2:RBK_Q);put32(q,b2+(random32()%2?RBK_LIST:RBK_LIST2));
        q[5]=real?0:counts[random32()%10];}
    if(index==ES2_HOOK_INDEX_region_hit&&!real)
        for(unsigned k=0;k<2;++k){BYTE *q=rbk_w2+(k?RBK_Q2:RBK_Q);if(q[5]>=0x7f&&random32()%2)q[5]=(BYTE)(random32()%3);}
    /* Globals: counter, explosion pool pointer, flag word, stack. */
    put32(mapped+0x2cbee4,b0+RBK_COUNTER);put32(mapped+0xa961e,random32());
    put16(mapped+0xa982c,(uint16_t)(random32()%2?0:random32()));put32(mapped+0x98864,b2+RBK_Q);
    random_bytes(mapped+0xa964c,14);
    int part=n<64?(int)(n%32)-8:random32()%4==0?(int)(random32()%32)-8:(int)(random32()%13);
    if(real){
        rbk_real_tables();part=(int)(random32()%8);put16(d+0x18,8);
        for(int i=0;i<8;++i)put16(rbk_w0+RBK_INTACT0+2*i,(uint16_t)(random32()%5?1:0));
        for(int i=-16;i<48;++i)put16(rbk_w0+RBK_WORDS0+2*i,(uint16_t)(random32()%4?random32()%1500:0xff00u|random32()));
    }
    rbk_rec=b0+RBK_REC0+18u*(uint32_t)part;
    a[0]=b0+RBK_D;a[1]=(random32()%2?random32()<<16:0)|(uint16_t)part;
    if(index==ES2_HOOK_INDEX_region_hit)a[2]=real?(random32()%2?random32()<<16:0)|random32()%3000u:random32();
}
static int rbk_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const rbk_record *expected,uint32_t fe){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u frames %08x/%08x\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,rbk_calls,fe,rbk_frame);
    for(unsigned j=0;j<calls&&j<rbk_calls&&expected;++j)if(!rbk_record_equal(expected+j,rbk_log+j,fe,rbk_frame)){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(rbk_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    rbk_memory_differs();
    return 1;
}
static unsigned rbk_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<rbk_calls;++j){unsigned id=rbk_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?rbk_slots[id]==c->key:!rbk_slots[id]&&rbk_addresses[id]==c->key)++hits;}
    return hits;
}
/* A four-slot cdecl caller that hands over EAX/EDX/ECX and returns all
 * three (the hook ABI returns EDX:EAX only). */
__attribute__((naked,noinline)) static void rbk_machine(void *target,const uint32_t *a,const uint32_t *registers,uint32_t *out){
    __asm__ volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl 12(%ebp),%esi\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* The core called directly with its ECX out pointer (the adapter drops
 * ECX); its game calls go through the same bridges, unobserved; FS:0 is
 * restored from the record as the adapter does. */
static uint64_t rbk_core(unsigned index,const uint32_t *a,const uint32_t *r,uint32_t *ecx){
    if(index==ES2_HOOK_INDEX_region_hit){
        BYTE scratch[ES2_REGION_HIT_SCRATCH]={0};es2_weapon_graph g=weapons_graph(mapped,scratch,index);g.observe=NULL;
        return es2_region_hit_registers(&math_native_memory,&g,a[0],a[1],a[2],r,ecx);
    }
    BYTE scratch[ES2_REGION_BREAK_SCRATCH]={0};es2_weapon_graph g=weapons_graph(mapped,scratch,index);g.observe=NULL;
    weapons_impact_context c={(uint32_t)(uintptr_t)mapped,(uint32_t)(uintptr_t)(scratch+ES2_REGION_BREAK_FRAME)};
    g.call=weapons_impact_guest_call;g.context=&c;
    uint64_t result=es2_region_break_registers(&math_native_memory,&g,a[0],a[1],r,ecx);
    uint32_t head=get32(scratch+ES2_REGION_BREAK_FRAME-0x34u);__asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");
    return result;
}
static int rbk_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[RBK_GLOBALS],after[RBK_GLOBALS],actual[RBK_GLOBALS];static rbk_record expected[RBK_MAX];
    uint32_t head,head_after,out[3],core_ecx=0;
    rbk_save();rbk_globals(before,0);
    __asm__ volatile("movl %%fs:0,%0":"=r"(head));
    rbk_reset();rbk_machine(mapped+d->rva,a,r,out);unsigned calls=rbk_calls;memcpy(expected,rbk_log,calls*sizeof(*expected));
    const uint32_t frame=rbk_frame;
    rbk_keep();rbk_globals(after,0);
    __asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
    if(head_after!=head)return fail("region_break original left FS:0 changed");
    uint64_t pair[2];pair[0]=(uint64_t)out[1]<<32|out[0];
    rbk_restore();rbk_globals(before,1);rbk_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];for(unsigned i=0;i<count&&i<16;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);rbk_globals(actual,0);
    __asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
    if(head_after!=head)return fail("region_break adapter left FS:0 changed");
    if(n<4096)for(unsigned i=0;i<count&&i<16;++i)if(c[i].count-counts[i]!=rbk_expected_hits(c+i))return fail("region_break callee counter delta");
    if(pair[0]!=pair[1]||rbk_trace_differs(expected,calls,frame)||rbk_memory_differs()||rbk_globals_differ(after,actual))
        return rbk_report("direct",n,pair,calls,expected,frame);
    rbk_restore();rbk_globals(before,1);rbk_reset();
    pair[1]=rbk_core(index,a,r,&core_ecx);rbk_globals(actual,0);
    __asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
    if(head_after!=head)return fail("region_break core left FS:0 changed");
    /* With the hook installed (integration pass) the "original" run is the
     * hook, whose ECX is the dispatcher's; ECX is compared otherwise. */
    const int ecx_known=!rng_hooks[index].installed;
    if(pair[0]!=pair[1]||(ecx_known&&core_ecx!=out[2])||rbk_trace_differs(expected,calls,frame)||rbk_memory_differs()||
       rbk_globals_differ(after,actual)){
        fprintf(stderr," ECX %08x/%08x\n",out[2],core_ecx);return rbk_report("core ECX",n,pair,calls,expected,frame);
    }
    rbk_globals(before,1);
    return 0;
}
static int rbk_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;uint32_t head,head_after;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;rbk_build(index,n,a,r,0);
        BYTE before[RBK_GLOBALS],after[RBK_GLOBALS],actual[RBK_GLOBALS],expected_fpu[108],actual_fpu[108];static rbk_record expected[RBK_MAX];
        rbk_save();rbk_globals(before,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head));
        rbk_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=rbk_calls;memcpy(expected,rbk_log,calls*sizeof(*expected));rbk_keep();rbk_globals(after,0);
        const uint32_t frame=rbk_frame;
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("region_break poison install");
        rbk_restore();rbk_globals(before,1);rbk_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);rbk_globals(actual,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(head_after!=head||pair[0]!=pair[1]||rbk_trace_differs(expected,calls,frame)||rbk_memory_differs()||
           rbk_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=rbk_report(fast?"installed fast":"installed preserving",n,pair,calls,expected,frame);
        if(!es2_hook_uninstall(&h))return fail("region_break poison uninstall");rbk_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state/FS:0\n",d->name,total);
    return status;
}
/* Real callees with the nested hooks in rotating original/replaced/compare
 * modes (compare falls back to replaced where refused) over the real
 * tables; the record stub, fire_release, fire_alloc, fire_create,
 * explosion_create and debris_pick stay stubs; the node lookups and 4801d4
 * run as original code. Pair, trace, arena, globals (both RNG streams)
 * and FS:0 are compared against a run with only the nested hooks. */
static unsigned rbk_nested(unsigned index,unsigned nested[16]){
    static const unsigned list[]={ES2_HOOK_INDEX_region_find,ES2_HOOK_INDEX_region_entry,ES2_HOOK_INDEX_region_spread,
        ES2_HOOK_INDEX_region_absorb,ES2_HOOK_INDEX_rng_bounded,ES2_HOOK_INDEX_rng_next,ES2_HOOK_INDEX_euler_matrix,
        ES2_HOOK_INDEX_matrix_compose,ES2_HOOK_INDEX_pool_alloc,ES2_HOOK_INDEX_debris_table,ES2_HOOK_INDEX_region_break};
    unsigned count=index==ES2_HOOK_INDEX_region_hit?11:10;
    for(unsigned j=0;j<count;++j)nested[j]=list[j];
    return count;
}
static int rbk_real_pass(unsigned index){
    unsigned nested[16],count=rbk_nested(index,nested);
    const unsigned keep=1u|1u<<3|1u<<4|1u<<5|1u<<11|1u<<13;
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0,cascades=0,pushes=0;uint64_t entered[16]={0};
    BYTE saved[RBK_IDS][5];int active[RBK_IDS];
    if(rbk_patch(saved,active,keep,1))return 1;
    rbk_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u,head,head_after;rbk_build(index,n,a,r,1);
        BYTE before[RBK_GLOBALS],after[RBK_GLOBALS],actual[RBK_GLOBALS];static rbk_record expected[RBK_MAX];
        rbk_save();rbk_globals(before,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head));
        es2_hook hk[17];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("region_break nested install");}
        rbk_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        unsigned calls=rbk_calls;memcpy(expected,rbk_log,calls*sizeof(*expected));const uint32_t frame=rbk_frame;
        rbk_keep();rbk_globals(after,0);
        if(index==ES2_HOOK_INDEX_region_hit&&(uint16_t)expected_pair[0]==1)++cascades;
        if(index==ES2_HOOK_INDEX_region_break&&rbk_w2[RBK_Q+5])++pushes;
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+16,d,&o,function_hook_extension(index)))return fail("region_break caller install");
        rbk_restore();rbk_globals(before,1);rbk_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);rbk_globals(actual,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
        if(head_after!=head||expected_pair[0]!=actual_pair[0]||expected_pair[1]!=actual_pair[1]||rbk_trace_differs(expected,calls,frame)||
           rbk_memory_differs()||rbk_globals_differ(after,actual)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            fprintf(stderr,"real mode=%u head %08x/%08x\n",mode,head,head_after);status=rbk_report("real",n,pair,calls,expected,frame);
        }
        for(unsigned j=0;j<17;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("region_break nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("region_break nested uninstall");}
        rbk_globals(before,1);++total;
    }
    rbk_real=0;
    if(rbk_patch(saved,active,keep,0))return 1;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&index==ES2_HOOK_INDEX_region_hit&&!cascades){fprintf(stderr,"FAIL: %s real pass without a cascade\n",d->name);status=1;}
    if(!status&&index==ES2_HOOK_INDEX_region_break&&!pushes){fprintf(stderr,"FAIL: %s real pass without a pushed child\n",d->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases (%u %s), real node lookups and 4801d4, %u nested hooks in rotating modes, 31/a7 stack, pair, trace, memory, RNG and FS:0\n",
            d->name,total,index==ES2_HOOK_INDEX_region_hit?cascades:pushes,index==ES2_HOOK_INDEX_region_hit?"cascades":"with pushed children",count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
static int region_break_cases(unsigned index,unsigned limit){
    const unsigned which=rbk_which(index);
    BYTE saved[RBK_IDS][5],old_globals[RBK_GLOBALS];int active[RBK_IDS];
    rbk_place();rbk_globals(old_globals,0);
    if(rbk_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_REGION_BREAK_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];rbk_build(index,n,a,r,0);status=rbk_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=rbk_installed_probes(index,64);
    if(rbk_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=rbk_real_pass(index);
    rbk_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ECX of the core, ordered calls with entry registers and context word, frame payloads by content, arena/globals/FS:0\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
