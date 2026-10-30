/* Remaining pool updates: smoke_ball_update 4091cc, smoke_update 4092dc,
 * smoke_ball_init 409178, debris_update 408bd8, ground_height 427d80,
 * meteor_update 409d2c. Direct cases run the original body and the core against
 * logging stubs for every game callee (entry registers recorded, frame payloads
 * compared by content, stack addresses masked in the fixed window of
 * node_cases.h); the runtime frame setup 49630c is a record stub that keeps the
 * real FS:0 head, as in effect_create_cases.h. The pure leaf 4089bc runs as
 * original code. Then installed 31/a7 poison probes and a real-callee pass with
 * the nested hooks in rotating modes. */
typedef struct pl_record { uint32_t id,args[5],regs[3],input[3],eax,edx; } pl_record;
enum { PL_SIZE=0x1400, PL_OBJECT=0x100, PL_SHAPE=0x180, PL_NODE=0x1a0, PL_FRAMES=0x1e0, PL_COUNTS=0x1f0,
    PL_OWNER=0x200, PL_OWNER2=0x260, PL_OWNER_SHAPE=0x2c0, PL_NODE_OBJECT=0x2e0, PL_VTABLE=0x2f0, PL_RECORD=0x318,
    PL_MATRICES=0x320, PL_MATRICES2=0x460, PL_BALL=0x5a0, PL_POOL=0x600, PL_GROUP=0x640, PL_LIST=0x670, PL_ACTOR=0x680,
    PL_TABLE=0x6c0, PL_CAMERA=0x6e0, PL_COUNTER=0x6f8, PL_SHAPES=0x700, PL_POINT=0x720, PL_INIT=0x740, PL_HEAP=0x760,
    PL_GRID=0x780, PL_CELLS=0x8a0, PL_NODES=0x9c0, PL_MAX=40, PL_IDS=24, PL_GLOBALS=99 };
#define PL_BASE ((uint32_t)(uintptr_t)pl_arena)
static BYTE pl_arena[PL_SIZE],pl_before[PL_SIZE],pl_after[PL_SIZE];
static pl_record pl_log[PL_MAX];
static unsigned pl_calls;
static int pl_real;
static uint32_t pl_seed,pl_stream,pl_plan_p[3],pl_plan_q[3],pl_last_q10;
__attribute__((used)) static uint32_t pl_ecx;
/* 49630c (record), 467a48, 467aa8, virtual +20 (not patched), 4801d4,
 * 47ebb8, 48043c, 471b30, 409178, 467924, 47e0b0, virtual +10, 427d80,
 * 407f1c, 408530, 402188, 496406, virtual +18, 47d32c, 4628e0, 46e180,
 * 426a20, and object_init's heap callees 47749c and 490b64. */
static const uint32_t pl_addresses[PL_IDS]={0x49630c,0x467a48,0x467aa8,0,0x4801d4,0x47ebb8,0x48043c,0x471b30,0x409178,0x467924,
    0x47e0b0,0,0x427d80,0x407f1c,0x408530,0x402188,0x496406,0,0x47d32c,0x4628e0,0x46e180,0x426a20,0x47749c,0x490b64};
static const unsigned pl_slots[PL_IDS]={0,0,0,0x20,0,0,0,0,0,0,0,0x10,0,0,0,0,0,0x18,0,0,0,0,0,0};
static const unsigned pl_counts[PL_IDS]={0,1,1,2,3,2,3,1,3,1,2,1,2,5,4,2,2,2,2,2,2,5,1,2};
static uint32_t pl_next(void){
    uint32_t z=pl_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *pl_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
/* Same fixed stack window as node_cases.h: frame addresses are masked. */
static int pl_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static uint32_t pl_small(uint32_t pick,uint32_t range){return (uint32_t)((int32_t)(pick%(2u*range+1u))-(int32_t)range);}
__attribute__((used,noinline)) static uint64_t __cdecl pl_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(pl_calls==PL_MAX){fail("pools synthetic call overflow");ExitProcess(1);}
    pl_record *v=pl_log+pl_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,4*pl_counts[id]);memcpy(v->regs,regs,12);
    for(unsigned j=0;j<3;++j)if(pl_stack(v->regs[j]))v->regs[j]=0;
    for(unsigned j=0;j<pl_counts[id];++j)if(pl_stack(v->args[j]))v->args[j]=0;
    uint32_t eax=pl_next(),edx=pl_next(),pick=pl_next(),b=PL_BASE;pl_ecx=pl_next();
    BYTE *o=pl_arena+PL_OBJECT;
    switch(id){
    case 0: /* 49630c record stub returns the real FS:0 head for pl_stub_0. */
        __asm__ volatile("movl %%fs:0,%0":"=r"(eax));break;
    case 1: /* timer_word_step: AX = 0 half of the time; the count is reread. */
        if(pick%2)eax&=0xffff0000u;
        if(pick%11==0)put16(o+0x24,(uint16_t)(pick>>8&1));
        break;
    case 2: /* timer_dword_step: EAX = 0 half of the time. */
        if(pick%2)eax=0;
        break;
    case 3: /* node virtual +20: record or 0; may change what is reread. */
        eax=pick%4?b+PL_RECORD:0;
        if(pl_real)break;
        if(pick%10==0)o[0xc]^=1;
        if(pick%7==0)put32(o+0x15,b+(pick>>8&1?PL_OWNER2:PL_OWNER));
        if(pick%13==0)put32(pl_arena+PL_OWNER_SHAPE+0x16,b+(pick>>12&1?PL_MATRICES2:PL_MATRICES));
        break;
    case 4: /* 4801d4: offset words by content, planned local point out. */
        for(unsigned j=0;j<3;++j)v->input[j]=get16(pl_host(a[0])+2*j);
        for(unsigned j=0;j<3;++j)put32(pl_host(a[2])+4*j,pl_plan_p[j]);
        if(pick%8==0)put16(pl_host(get32(o+0x15))+0x32,(uint16_t)(pick>>8&1));
        break;
    case 5: /* euler_matrix: angles by content; rewrites matrix words. */
        for(unsigned j=0;j<3;++j)v->input[j]=get16(pl_host(a[0])+2*j);
        if(pick%3)for(unsigned j=0;j<9;++j)put16(pl_host(a[1])+2*j,(uint16_t)pl_next());
        break;
    case 6: /* transform_point: point by content, planned world point out. */
        for(unsigned j=0;j<3;++j)v->input[j]=get32(pl_host(a[0])+4*j);
        for(unsigned j=0;j<3;++j)put32(pl_host(a[2])+4*j,pl_plan_q[j]);
        if(pick%9==0)put16(o+0x26,(uint16_t)pick);
        break;
    case 7: /* pool_alloc: the meteor's flat pool always succeeds (proof). */
        eax=a[0]==b+PL_POOL+0x20?b+PL_BALL:pick%3?b+PL_BALL:0;
        break;
    case 8: /* smoke_ball_init: Q by content; the frame words are reread. */
        for(unsigned j=0;j<3;++j)v->input[j]=get32(pl_host(a[1])+4*j);
        if(pick%5==0)put16(o+0x28,(uint16_t)pick);
        break;
    case 9: /* movement_step: mostly small steps. */
        if(pick%4)eax=(eax&0xffff0000u)|(uint16_t)pl_small(pick>>4,64);
        break;
    case 10: /* multiply_q10: the bounce product lands around -2Dh. */
        if(a[0]==0x1c2u&&pick%2)eax=(eax&0xffff0000u)|(uint16_t)(0u-0x2du+pl_small(pick>>4,2));
        else if(pick%4)eax=pl_small(pick>>4,2000);
        pl_last_q10=eax;
        break;
    case 12: { /* 427d80: P by content; a third of the heights land the sum on
                * P.z - 1, P.z or P.z + 1. */
        for(unsigned j=0;j<3;++j)v->input[j]=get32(pl_host(a[0])+4*j);
        int64_t d=(int64_t)(int32_t)v->input[2]-(int32_t)pl_last_q10+(int32_t)pl_small(pick>>4,1);
        if(pick%3==0&&d>=-32768&&d<=32767)eax=(eax&0xffff0000u)|(uint16_t)d;
        else if(pick%3==1)eax=(eax&0xffff0000u)|(uint16_t)pl_small(pick>>4,4000);
        break; }
    case 15: /* object_init: never touches the meteor's shape word O+41. */
        break;
    case 20: { /* terrain_height: point by content; a third of the heights
                * at its z - 1, z or z + 1. */
        for(unsigned j=0;j<3;++j)v->input[j]=get32(pl_host(a[1])+4*j);
        if(pick%3==0)eax=v->input[2]+pl_small(pick>>4,1);
        else if(pick%3==1)eax=v->input[2]+pl_small(pick>>4,5000);
        break; }
    case 21: /* radius_hit: point by content, AX = 0 half of the time. */
        for(unsigned j=0;j<3;++j)v->input[j]=get32(pl_host(a[0])+4*j);
        if(pick%2)eax&=0xffff0000u;
        break;
    case 22: /* 47749c heap block or 0. */
        eax=pick%4?b+PL_HEAP:0;break;
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
/* Pushes the entry EAX/EDX/ECX for the record before the slot pointer. */
#define PL_STUB(n) __attribute__((naked)) static void pl_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _pl_stub_impl\n\taddl $24,%esp\n\tmovl _pl_ecx,%ecx\n\tret\n\t");}
PL_STUB(1) PL_STUB(2) PL_STUB(3) PL_STUB(4) PL_STUB(5) PL_STUB(6) PL_STUB(7) PL_STUB(8) PL_STUB(9) PL_STUB(10) PL_STUB(11)
PL_STUB(12) PL_STUB(13) PL_STUB(14) PL_STUB(15) PL_STUB(16) PL_STUB(17) PL_STUB(18) PL_STUB(19) PL_STUB(20) PL_STUB(21)
PL_STUB(22) PL_STUB(23)
#undef PL_STUB
/* 49630c: captures the entry registers like the other stubs, then links a
 * record at EBP + [descriptor+4] into FS:0 with the saved head at record+0
 * and a zero context word at record+16, as the original does. */
__attribute__((naked)) static void pl_stub_0(void){__asm__ volatile(
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
    "pushl $0\n\tcalll _pl_stub_impl\n\taddl $12,%esp\n\t"
    "popl %ecx\n\tmovl 4(%ecx),%ecx\n\taddl %ebp,%ecx\n\tmovl %eax,(%ecx)\n\tmovw $0,16(%ecx)\n\tmovl %ecx,%fs:0\n\t"
    "popl %edx\n\taddl $4,%esp\n\tmovl _pl_ecx,%ecx\n\tret\n\t");}
static void *pl_stubs[PL_IDS]={pl_stub_0,pl_stub_1,pl_stub_2,pl_stub_3,pl_stub_4,pl_stub_5,pl_stub_6,pl_stub_7,pl_stub_8,pl_stub_9,
    pl_stub_10,pl_stub_11,pl_stub_12,pl_stub_13,pl_stub_14,pl_stub_15,pl_stub_16,pl_stub_17,pl_stub_18,pl_stub_19,pl_stub_20,
    pl_stub_21,pl_stub_22,pl_stub_23};
/* Direct callees of each hook (bit i = pl_addresses[i]; virtual slots are
 * in the arena vtable and never patched). */
static unsigned pl_which(unsigned index){
    if(index==ES2_HOOK_INDEX_smoke_ball_update)return 1u<<2;
    if(index==ES2_HOOK_INDEX_smoke_update)return 1u|1u<<1|1u<<4|1u<<5|1u<<6|1u<<7|1u<<8;
    if(index==ES2_HOOK_INDEX_smoke_ball_init)return 1u<<15;
    if(index==ES2_HOOK_INDEX_debris_update)return 1u|1u<<2|1u<<7|1u<<9|1u<<10|1u<<12|1u<<13|1u<<14;
    if(index==ES2_HOOK_INDEX_ground_height)return 1u<<20;
    return 1u|1u<<7|1u<<9|1u<<15|1u<<16|1u<<18|1u<<19|1u<<20|1u<<21;
}
/* Counter pointer, smoke pool, shape table, static node matrix, level
 * word, explosion pool, burst flag, the debris effect globals 4a964c..
 * 4a9669, flat pool, flat shape list, camera, grid, time scale. */
static void pl_globals(BYTE *out,int restore){
    static const unsigned offsets[]={0x2cbee4,0xa966e,0xa9678,0x2c56a8,0xa9e4e,0xa961e,0xd1f43,0xa964c,0xa968d,0xa9698,0xd24ea,0xa0be0,0xd3b64},
        sizes[]={4,4,4,32,2,4,1,30,4,4,4,4,2};
    for(unsigned i=0;i<13;++i){if(restore)memcpy(mapped+offsets[i],out,sizes[i]);else memcpy(out,mapped+offsets[i],sizes[i]);out+=sizes[i];}
}
static void pl_reset(void){pl_calls=0;pl_stream=pl_seed;}
static uint16_t pl_word(const uint16_t *values,unsigned count){return values[random32()%count];}
/* Pool header (free count +0, used count +2, free head +0Ah, used head
 * +0Eh) with count free nodes of 80h bytes; objects start at node+8. */
static void pl_pool(uint32_t pool,uint32_t nodes,unsigned count){
    put16(pl_host(pool),(uint16_t)count);put16(pl_host(pool)+2,0);put32(pl_host(pool)+0xe,0);
    for(unsigned i=0;i<count;++i){uint32_t n=nodes+0x80u*i;put32(pl_host(n),i+1<count?n+0x80u:0);put32(pl_host(n)+4,i?n-0x80u:0);}
    put32(pl_host(pool)+0xa,count?nodes:0);
}
/* A 4 x 4 terrain grid (46e180's layout, terrain_cases.h): cells at +ECh,
 * log2 columns/rows at +100h/+104h, shift +108h, scale and offset. */
static void pl_grid(void){
    BYTE *g=pl_arena+PL_GRID;put32(g+0xec,PL_BASE+PL_CELLS);put32(g+0x100,2);put32(g+0x104,2);
    put32(g+0x108,6+random32()%4);put32(g+0x110,random32()%4096);put32(g+0x118,random32()%64);
}
static uint32_t pl_grid_coordinate(void){return (uint32_t)((int32_t)(random32()%700u)-64);}
static void pl_common(void){
    uint32_t b=PL_BASE;
    put32(mapped+0x2cbee4,b+PL_COUNTER);put32(mapped+0xa966e,b+PL_POOL);put32(mapped+0xa961e,b+PL_POOL+0x10);
    put32(mapped+0xa968d,b+PL_POOL+0x20);put32(mapped+0xa9678,b+PL_SHAPES+8);put32(mapped+0xa9698,b+PL_INIT);
    put32(mapped+0xd24ea,b+PL_CAMERA);put32(mapped+0xa0be0,b+PL_GRID);random_bytes(mapped+0x2c56a8,32);
    put32(pl_arena+PL_VTABLE+0x10,(uint32_t)(uintptr_t)pl_stubs[11]);put32(pl_arena+PL_VTABLE+0x18,(uint32_t)(uintptr_t)pl_stubs[17]);
    put32(pl_arena+PL_VTABLE+0x20,(uint32_t)(uintptr_t)pl_stubs[3]);
}
static void pl_shape(int real){
    uint32_t b=PL_BASE;BYTE *o=pl_arena+PL_OBJECT;
    static const uint16_t counts[]={1,2,3,9,0x8000,0xffff};
    put32(pl_arena+PL_SHAPE,b+PL_VTABLE);put32(pl_arena+PL_SHAPE+4,b+PL_NODE);put32(pl_arena+PL_SHAPE+8,b+PL_FRAMES);
    put32(pl_arena+PL_NODE+0x20,b+PL_COUNTS);
    uint16_t c=random32()%3?pl_word(counts,6):(uint16_t)(1+random32()%0xfffe);if(real&&c>16)c=(uint16_t)(1+c%9);
    put16(pl_arena+PL_COUNTS,c);
    uint32_t k=random32()%4;put16(pl_arena+PL_FRAMES,(uint16_t)(k==0?c-1:k==1&&c==1?0xffff:random32()));
    (void)o;
}
static void pl_smoke_ball(unsigned n,int real){
    BYTE *o=pl_arena+PL_OBJECT;(void)n;
    pl_shape(real);put32(o+0x34,PL_BASE+PL_SHAPE);
    if(real){put32(o+0x48,random32()%400);put16(mapped+0xd3b64,(uint16_t)(1+random32()%200));}
}
static void pl_owner(BYTE *owner,int real){
    uint32_t b=PL_BASE;
    put32(owner+0x34,b+PL_OWNER_SHAPE);put16(owner+0x32,random32()%3?(uint16_t)(random32()%2):(uint16_t)random32());
    if(real)for(unsigned j=0;j<3;++j)put32(owner+0x26+4*j,random32()%65536u-32768u);
}
static void pl_smoke(unsigned n,int real){
    uint32_t b=PL_BASE;BYTE *o=pl_arena+PL_OBJECT;
    static const uint16_t nodes[]={0xffff,0x8000,0,1,2,7,0x7fff,0xfffe};
    static const uint16_t records[]={0,1,2,7,0xffff,0x8000};
    static const uint16_t spawns[]={1,2,3,0xffff,0x8000};
    put16(o+0x24,n%8==0?0:random32()%2?pl_word(spawns,5):(uint16_t)random32());
    put16(o+0x19,n<1024?nodes[n%8]:pl_word(nodes,8));
    o[0xc]=(BYTE)(random32()%2?0:random32()%3?1:random32());
    put32(o+0x15,b+(random32()%4?PL_OWNER:PL_OWNER2));
    pl_owner(pl_arena+PL_OWNER,real);pl_owner(pl_arena+PL_OWNER2,real);
    put32(pl_arena+PL_OWNER_SHAPE+4,b+PL_NODE_OBJECT);put32(pl_arena+PL_OWNER_SHAPE+0x16,b+PL_MATRICES);
    put32(pl_arena+PL_NODE_OBJECT,b+PL_VTABLE);put16(pl_arena+PL_RECORD+4,pl_word(records,6));
    for(unsigned j=0;j<3;++j){pl_plan_p[j]=random32();pl_plan_q[j]=random32()%4?random32()%8192u-4096u:random32();}
    if(real){o[0x21]=(BYTE)random32();put16(o+0x22,(uint16_t)(random32()%400));put16(mapped+0xd3b64,(uint16_t)(1+random32()%200));
        for(unsigned j=0;j<3;++j)put16(o+0x1b+2*j,(uint16_t)(random32()%2048u-1024u));
        pl_pool(b+PL_POOL,b+PL_NODES,random32()%3);put16(o+0x26,(uint16_t)(random32()%4));}
}
static void pl_init(unsigned n,uint32_t a[6]){
    uint32_t b=PL_BASE;
    for(int k=-2;k<=2;++k)put32(pl_arena+PL_SHAPES+8+4*k,k==0&&n%5==0?0:b+0x1000u+(uint32_t)k);
    static const uint32_t points[]={PL_POINT,PL_POINT,PL_POINT,PL_BALL+0x26,PL_BALL+0x2a,PL_BALL+0x22,PL_BALL+0x2e,PL_BALL+0x45};
    a[0]=b+PL_BALL;a[1]=b+points[n<1024?n%8:random32()%8];
    a[2]=(random32()&0xffff0000u)|(uint16_t)((int)(random32()%5)-2);
}
static void pl_debris(unsigned n,int real){
    uint32_t b=PL_BASE;BYTE *o=pl_arena+PL_OBJECT;
    static const BYTE kinds[]={0xfe,0xff,0,1,2,5,0x80,0x7f};
    static const uint16_t lives[]={0xffff,0x8000,0,1,5};
    static const uint16_t counts[]={0,1,2,5,0xffff,0x8000};
    put32(o,b+PL_VTABLE);
    put16(mapped+0xa9e4e,random32()%2?4:(uint16_t)random32());
    put32(o+0x2e,random32()%4?pl_small(random32(),3000):random32());
    for(unsigned j=0;j<3;++j)put16(o+0x4a+2*j,random32()%3?(uint16_t)pl_small(random32(),300):(uint16_t)random32());
    o[0x41]=random32()%4?kinds[n<1024?n%8:random32()%8]:(BYTE)random32();
    put16(o+0x59,random32()%2?pl_word(lives,5):(uint16_t)random32());
    put32(o+0x5d,b+PL_TABLE);mapped[0xd1f43]=(BYTE)(random32()%2?0:1+random32()%255);
    put16(mapped+0xa9654,random32()%2?pl_word(counts,6):(uint16_t)random32());
    if(real){o[0x54]=(BYTE)random32();put32(o+0x55,random32()%400);put16(mapped+0xd3b64,(uint16_t)(1+random32()%200));
        pl_pool(b+PL_POOL+0x10,b+PL_NODES,random32()%3);}
}
static void pl_meteor(unsigned n,int real){
    uint32_t b=PL_BASE;BYTE *o=pl_arena+PL_OBJECT;
    o[0x4b]=(BYTE)(n%2?0:random32()%3?1:random32()|1);
    pl_shape(real);put32(o+0x41,b+PL_SHAPE);
    uint32_t c=get16(pl_arena+PL_COUNTS),k=random32()%4;
    put32(o+0x4d,k<3?((c+k-1u)<<10)+random32()%1024:random32());
    put32(o+0x55,random32()%4?b+PL_GROUP:0);put32(pl_arena+PL_GROUP+0xc,b+PL_LIST);put32(pl_arena+PL_LIST,b+PL_ACTOR);
    o[0x4c]=(BYTE)(random32()%2?0:1+random32()%255);
    put32(o+0x51,random32()%2?0:random32());
    for(unsigned j=0;j<3;++j)put16(o+0x45+2*j,(uint16_t)(random32()%3?pl_small(random32(),2000):random32()));
    if(random32()%3==0)put32(o+0x2e,50000u-(uint32_t)(int32_t)(int16_t)get16(o+0x49)+pl_small(random32(),1));
    if(real){put16(mapped+0xd3b64,(uint16_t)(1+random32()%200));pl_grid();random_bytes(pl_arena+PL_CELLS,256);
        put32(o+0x26,pl_grid_coordinate());put32(o+0x2a,pl_grid_coordinate());
        pl_pool(b+PL_POOL+0x20,b+PL_NODES+0x200,1+random32()%2);}
}
static void pl_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    pl_seed=random32();random_bytes(pl_arena,PL_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    pl_common();a[0]=PL_BASE+PL_OBJECT;
    if(index==ES2_HOOK_INDEX_smoke_ball_update)pl_smoke_ball(n,real);
    else if(index==ES2_HOOK_INDEX_smoke_update)pl_smoke(n,real);
    else if(index==ES2_HOOK_INDEX_smoke_ball_init)pl_init(n,a);
    else if(index==ES2_HOOK_INDEX_debris_update)pl_debris(n,real);
    else if(index==ES2_HOOK_INDEX_ground_height){a[0]=PL_BASE+PL_POINT;
        if(real){pl_grid();random_bytes(pl_arena+PL_CELLS,256);for(unsigned j=0;j<2;++j)put32(pl_arena+PL_POINT+4*j,pl_grid_coordinate());}}
    else pl_meteor(n,real);
}
static int pl_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const pl_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,pl_calls);
    for(unsigned j=0;j<calls&&j<pl_calls&&expected;++j)if(memcmp(expected+j,pl_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(pl_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<PL_SIZE;++j)if(pl_after[j]!=pl_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,pl_after[j],pl_arena[j]);break;}
    return 1;
}
static unsigned pl_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<pl_calls;++j){unsigned id=pl_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?pl_slots[id]==c->key:!pl_slots[id]&&pl_addresses[id]==c->key)++hits;}
    return hits;
}
static int pl_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[PL_GLOBALS],after[PL_GLOBALS],actual[PL_GLOBALS];static pl_record expected[PL_MAX];uint32_t head,head_after;
    memcpy(pl_before,pl_arena,PL_SIZE);pl_globals(before,0);
    __asm__ volatile("movl %%fs:0,%0":"=r"(head));
    pl_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=pl_calls;memcpy(expected,pl_log,calls*sizeof(*expected));
    memcpy(pl_after,pl_arena,PL_SIZE);pl_globals(after,0);
    memcpy(pl_arena,pl_before,PL_SIZE);pl_globals(before,1);pl_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);pl_globals(actual,0);
    __asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
    if(head_after!=head)return fail("pools left FS:0 changed");
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=pl_expected_hits(c+i))return fail("pools callee counter delta");
    if(pair[0]!=pair[1]||calls!=pl_calls||memcmp(expected,pl_log,calls*sizeof(*expected))||memcmp(pl_after,pl_arena,PL_SIZE)||memcmp(after,actual,PL_GLOBALS)){
        for(unsigned j=0;j<PL_GLOBALS;++j)if(after[j]!=actual[j])fprintf(stderr," global byte %u %02x/%02x\n",j,after[j],actual[j]);
        return pl_report("direct",n,pair,calls,expected);
    }
    pl_globals(before,1);
    return 0;
}
static int pl_patch(BYTE saved[PL_IDS][5],int active[PL_IDS],unsigned which,int on){
    for(unsigned i=0;i<PL_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!pl_addresses[i])continue;if(actor_patch(pl_addresses[i]-0x400000u,pl_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(pl_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static int pl_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;uint32_t head,head_after;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;pl_build(index,n,a,r,0);
        BYTE before[PL_GLOBALS],after[PL_GLOBALS],actual[PL_GLOBALS],expected_fpu[108],actual_fpu[108];static pl_record expected[PL_MAX];
        memcpy(pl_before,pl_arena,PL_SIZE);pl_globals(before,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head));
        pl_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=pl_calls;memcpy(expected,pl_log,calls*sizeof(*expected));memcpy(pl_after,pl_arena,PL_SIZE);pl_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("pools poison install");
        memcpy(pl_arena,pl_before,PL_SIZE);pl_globals(before,1);pl_reset();sg_poison_call(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);pl_globals(actual,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(head_after!=head||pair[0]!=pair[1]||calls!=pl_calls||memcmp(expected,pl_log,calls*sizeof(*expected))||memcmp(pl_after,pl_arena,PL_SIZE)||
           memcmp(after,actual,PL_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=pl_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("pools poison uninstall");pl_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state/FS:0\n",d->name,total);
    return status;
}
/* Real callees with the nested hooks in rotating original/replaced/compare
 * modes (compare falls back to replaced where refused). The stubs that stay
 * patched are the runtime frame setup, heap and SOS-reaching callees and
 * the leaves outside this pass (keep masks below). Pair, arena, globals
 * and FS:0 are compared against a run with only the nested hooks. */
static unsigned pl_nested(unsigned index,unsigned nested[6],unsigned *keep){
    unsigned count=0;
    if(index==ES2_HOOK_INDEX_smoke_ball_update){nested[count++]=ES2_HOOK_INDEX_timer_dword_step;*keep=0;}
    else if(index==ES2_HOOK_INDEX_smoke_update){
        nested[count++]=ES2_HOOK_INDEX_timer_word_step;nested[count++]=ES2_HOOK_INDEX_euler_matrix;nested[count++]=ES2_HOOK_INDEX_transform_point;
        nested[count++]=ES2_HOOK_INDEX_pool_alloc;nested[count++]=ES2_HOOK_INDEX_smoke_ball_init;*keep=1u|1u<<15;}
    else if(index==ES2_HOOK_INDEX_smoke_ball_init){nested[count++]=ES2_HOOK_INDEX_object_init;*keep=1u|1u<<22|1u<<23;}
    else if(index==ES2_HOOK_INDEX_debris_update){
        nested[count++]=ES2_HOOK_INDEX_movement_step;nested[count++]=ES2_HOOK_INDEX_multiply_q10;nested[count++]=ES2_HOOK_INDEX_ground_height;
        nested[count++]=ES2_HOOK_INDEX_timer_dword_step;nested[count++]=ES2_HOOK_INDEX_pool_alloc;*keep=1u|1u<<13|1u<<14|1u<<20;}
    else if(index==ES2_HOOK_INDEX_ground_height){nested[count++]=ES2_HOOK_INDEX_terrain_height;*keep=0;}
    else {nested[count++]=ES2_HOOK_INDEX_movement_step;nested[count++]=ES2_HOOK_INDEX_pool_alloc;nested[count++]=ES2_HOOK_INDEX_object_init;
        nested[count++]=ES2_HOOK_INDEX_angle_ratio;nested[count++]=ES2_HOOK_INDEX_terrain_height;
        *keep=1u|1u<<16|1u<<19|1u<<21|1u<<22|1u<<23;}
    return count;
}
static int pl_real_pass(unsigned index){
    unsigned nested[6],keep,count=pl_nested(index,nested,&keep);
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[6]={0};
    BYTE saved[PL_IDS][5];int active[PL_IDS];
    if(pl_patch(saved,active,keep,1))return 1;
    pl_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u,head,head_after;pl_build(index,n,a,r,1);
        BYTE before[PL_GLOBALS],after[PL_GLOBALS],actual[PL_GLOBALS];
        memcpy(pl_before,pl_arena,PL_SIZE);pl_globals(before,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head));
        es2_hook hk[7];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            /* Under debris_update terrain_height stays a stub whose results
             * come from a stream, so a compared ground_height would see other
             * values in its second phase: replaced there. */
            if(nested[j]==ES2_HOOK_INDEX_ground_height&&(keep>>20&1u)&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("pools nested install");}
        pl_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        memcpy(pl_after,pl_arena,PL_SIZE);pl_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+6,d,&o,function_hook_extension(index)))return fail("pools caller install");
        memcpy(pl_arena,pl_before,PL_SIZE);pl_globals(before,1);pl_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);pl_globals(actual,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
        if(head_after!=head||expected_pair[0]!=actual_pair[0]||expected_pair[1]!=actual_pair[1]||memcmp(pl_after,pl_arena,PL_SIZE)||memcmp(after,actual,PL_GLOBALS)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            for(unsigned j=0;j<PL_GLOBALS;++j)if(after[j]!=actual[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,after[j],actual[j]);break;}
            fprintf(stderr,"real mode=%u head %08x/%08x\n",mode,head,head_after);pl_calls=0;status=pl_report("real",n,pair,0,NULL);
        }
        for(unsigned j=0;j<7;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("pools nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("pools nested uninstall");}
        pl_globals(before,1);++total;
    }
    pl_real=0;
    if(pl_patch(saved,active,0,0))return 1;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, 31/a7 stack, full pair, memory and FS:0\n",d->name,total,count);
    return status;
}
/* Compare cases (compare_probes.h) for ground_height: real terrain_height,
 * real-pass inputs. */
static void pl_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){pl_build(index,n,a,r,1);}
static void pl_compare_state(BYTE *s,int restore){
    if(restore){memcpy(pl_arena,s,PL_SIZE);pl_globals(s+PL_SIZE,1);}else{memcpy(s,pl_arena,PL_SIZE);pl_globals(s+PL_SIZE,0);}
}
static int pools_cases(unsigned index,unsigned limit){
    unsigned which=pl_which(index);
    BYTE saved[PL_IDS][5],old_globals[PL_GLOBALS];int active[PL_IDS];
    pl_globals(old_globals,0);
    if(limit&&compare_probe_installed(index)){pl_real=1;int status=compare_probe_run(index,limit,pl_compare_build,pl_compare_state,PL_SIZE+PL_GLOBALS);
        pl_real=0;pl_globals(old_globals,1);return status;}
    if(pl_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_POOLS_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];pl_build(index,n,a,r,0);status=pl_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=pl_installed_probes(index,64);
    if(pl_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=pl_real_pass(index);
    if(!limit&&!status&&compare_probe_capable(index)){pl_real=1;status=compare_probe_run(index,0,pl_compare_build,pl_compare_state,PL_SIZE+PL_GLOBALS);pl_real=0;}
    pl_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, frame payloads by content, arena/globals/FS:0\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
