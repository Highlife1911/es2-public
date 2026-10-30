/* VEHICLE update chain: convoy_steer 46a89c, convoy_seek 46a958, convoy_lead
 * 46a9e8, convoy_follow 46aa60, convoy_drive 46a810 and convoy_think 46a6d4
 * (docs/reverse-engineering/convoy.md). Direct cases run the original body and
 * the core against logging stubs for every game callee (ordered slots, entry
 * EAX/EDX/ECX, the pushed slot count through weapons_bridge_slots, frame
 * payloads by content, stack addresses masked in the fixed window of
 * node_cases.h). The originals are entered with EDI = object + 26h (the game's
 * only chain); every second convoy_follow case uses a random EDI and masks the
 * three high words it reaches (4928d0's EAX, 4929fc's slot 2, 46a958's slot 3),
 * which those callees never read. Then installed 31/a7 poison probes and a
 * real-callee pass with the nested hooks in rotating modes. convoy_steer
 * accepts compare; compare_probes.h's installed compare cases run on its
 * real-pass inputs with the real timer, approach and multiply. */
typedef struct cv_record { uint32_t id,args[4],regs[3],input[8],eax,edx,count; } cv_record;
enum { CV_SIZE=0xd00, CV_OBJECT=0x100, CV_LEADER=0x340, CV_GROUP=0x580, CV_GROUP2=0x5a0, CV_LIST=0x5c0, CV_POINT=0x600,
    CV_POINT2=0x620, CV_VTABLE=0x640, CV_CONFIG=0x6c0, CV_ROUTE=0x700, CV_ARRAY=0x720, CV_POINTS=0x760,
    CV_MEMBER2=0x800, CV_MEMBER3=0xa40, CV_MAX=24, CV_IDS=24, CV_GLOBALS=2 };
#define CV_BASE ((uint32_t)(uintptr_t)cv_arena)
/* The arena is the static block or a block placed so that the object's +0Ch or
 * +0Dh starts a 64 KiB page: the high word of the EAX that lo(o+0Ch,...) hands
 * 47e0a0/46a810 then differs from o+0Bh's or o+0Dh's. */
static BYTE cv_storage[CV_SIZE],*cv_arena=cv_storage,*cv_pages,cv_before[CV_SIZE],cv_after[CV_SIZE];
static cv_record cv_log[CV_MAX];
static unsigned cv_calls;
static int cv_real,cv_mask;
static uint32_t cv_seed,cv_stream,cv_leader;
__attribute__((used)) static uint32_t cv_ecx;
/* 467a48 timer_word_step, 467adc approach_word, 47e0a0 multiply_q8,
 * 4928d0 dist2d_point, 4929fc projection, 492934 heading_point, 46a89c,
 * 423b0c route_point, 42313c route_advance, 46a958, 46a5bc leader search,
 * virtual +78h (not patched), 47ebb8 euler_matrix, 47df04 swap, 47ffe8
 * rotate_2d, 46a9e8, 46aa60, 48043c transform_point, 403ca8 base update,
 * 404100 turret, 403d5c turret_aim, 46a810, 4029d8 terrain_pose, 46a614. */
static const uint32_t cv_addresses[CV_IDS]={0x467a48,0x467adc,0x47e0a0,0x4928d0,0x4929fc,0x492934,0x46a89c,0x423b0c,0x42313c,
    0x46a958,0x46a5bc,0,0x47ebb8,0x47df04,0x47ffe8,0x46a9e8,0x46aa60,0x48043c,0x403ca8,0x404100,0x403d5c,0x46a810,0x4029d8,0x46a614};
static const unsigned cv_slots[CV_IDS]={0,0,0,0,0,0,0,0,0,0,0,0x78,0,0,0,0,0,0,0,0,0,0,0,0};
static const unsigned cv_counts[CV_IDS]={1,3,2,2,3,2,4,2,1,4,1,2,2,1,3,1,1,3,1,1,2,1,1,1};
static const unsigned cv_member_offsets[4]={CV_OBJECT,CV_LEADER,CV_MEMBER2,CV_MEMBER3};
static uint32_t cv_next(void){
    uint32_t z=cv_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *cv_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
/* Same fixed stack window as node_cases.h: frame addresses are masked. */
static int cv_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
/* Readable payload: inside the arena or the committed stack. */
static int cv_ok(uint32_t p,uint32_t n){
    const NT_TIB *t=(const NT_TIB *)NtCurrentTeb();uint32_t low=(uint32_t)(uintptr_t)t->StackLimit,top=(uint32_t)(uintptr_t)t->StackBase;
    return (p-CV_BASE<CV_SIZE&&CV_SIZE-(p-CV_BASE)>=n)||(p>=low&&p<top&&top-p>=n);
}
static uint32_t cv_small(uint32_t pick,uint32_t range){return (uint32_t)((int32_t)(pick%(2u*range+1u))-(int32_t)range);}
static void cv_dwords(cv_record *v,unsigned at,uint32_t p,unsigned n){
    if(cv_ok(p,4u*n))for(unsigned j=0;j<n;++j)v->input[at+j]=get32(cv_host(p)+4*j);
}
static void cv_words(cv_record *v,unsigned at,uint32_t p,unsigned n){
    if(cv_ok(p,2u*n))for(unsigned j=0;j<n;++j)v->input[at+j]=get16(cv_host(p)+2*j);
}
__attribute__((used,noinline)) static uint64_t __cdecl cv_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(cv_calls==CV_MAX){fail("convoy synthetic call overflow");ExitProcess(1);}
    cv_record *v=cv_log+cv_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,4*cv_counts[id]);memcpy(v->regs,regs,12);
    v->count=weapons_bridge_slots(a,cv_counts[id]);
    if(cv_mask){if(id==3)v->regs[0]&=0xffffu;if(id==4)v->args[1]&=0xffffu;if(id==9)v->args[2]&=0xffffu;}
    for(unsigned j=0;j<3;++j)if(cv_stack(v->regs[j]))v->regs[j]=0;
    for(unsigned j=0;j<cv_counts[id];++j)if(cv_stack(v->args[j]))v->args[j]=0;
    uint32_t eax=cv_next(),edx=cv_next(),pick=cv_next(),b=CV_BASE;cv_ecx=cv_next();
    BYTE *o=cv_arena+CV_OBJECT;
    switch(id){
    case 0: /* timer_word_step: AX = 0 half of the time; byte +227h is reread. */
        if(pick%2)eax&=0xffff0000u;
        if(pick%7==0)o[0x227]^=1;
        break;
    case 1: /* approach_word: the three angle words are read after it. */
        if(pick%5==0)put16(o+0xc+2*(pick>>8)%6,(uint16_t)cv_next());
        break;
    case 2: /* multiply_q8: changes words the original wrote from its copies. */
        if(pick%6==0)put16(o+0x10,(uint16_t)cv_next());
        if(pick%6==1)put16(o+0xc,(uint16_t)cv_next());
        break;
    case 3: /* dist2d_point: point by content; distances at the 10000 test
             * and at the far-path clamps (100h and -96h after >> 5). */
        cv_dwords(v,0,a[1],2);
        if(pick%5==0)eax=10000u+cv_small(pick>>4,2);
        else if(pick%5==1)eax=(0x100u<<5)+cv_small(pick>>4,40);
        else if(pick%5==2)eax=(0u-(0x96u<<5))+cv_small(pick>>4,40);
        else if(pick%5==3)eax=cv_small(pick>>4,30000);
        if(pick%9==0&&cv_ok(a[1],8))put32(cv_host(a[1])+4*(pick>>8&1),cv_next());
        break;
    case 4: /* 4929fc: point by content, moved in place. */
        cv_dwords(v,0,a[0],2);
        if(pick%2&&cv_ok(a[0],8)){put32(cv_host(a[0]),cv_next());put32(cv_host(a[0])+4,cv_next());}
        break;
    case 5: /* heading_point: both points by content; the yaw is reread. */
        cv_dwords(v,0,a[0],2);cv_dwords(v,2,a[1],2);
        if(pick%7==0)put16(o+0x10,(uint16_t)cv_next());
        break;
    case 7: /* route_point: point, other point or 0; holder and group reread. */
        eax=pick%4==0?0:b+(pick>>4&1?CV_POINT2:CV_POINT);
        if(pick%11==0)put16(cv_arena+CV_GROUP+4,(uint16_t)cv_next());
        if(pick%13==0)put32(o+0x45,b+CV_GROUP2);
        break;
    case 9: /* 46a958: point by content; AX zero, one or random. */
        cv_dwords(v,0,a[1],3);
        if(pick%3==0)eax&=0xffff0000u;else if(pick%3==1)eax=(eax&0xffff0000u)|1u;
        break;
    case 10: /* 46a5bc: the object itself or another member (never 0). */
        eax=b+cv_member_offsets[pick%3==0?0:1+(pick>>4)%3];cv_leader=eax;
        break;
    case 11: { /* virtual +78h: V by content, moved; the leader's yaw is reread. */
        cv_dwords(v,0,a[1],3);
        if(pick%2)for(unsigned j=0;j<3;++j)put32(cv_host(a[1])+4*j,cv_next());
        if(pick%5==0&&cv_leader)put16(cv_host(cv_leader)+0x10,(uint16_t)cv_next());
        break; }
    case 12: /* euler_matrix: angles by content; rewrites the matrix words. */
        cv_words(v,0,a[0],3);
        if(pick%3&&cv_ok(a[1],18))for(unsigned j=0;j<9;++j)put16(cv_host(a[1])+2*j,(uint16_t)cv_next());
        break;
    case 13: /* 47df04: M by content, rewritten; EAX = M as in the original. */
        cv_words(v,0,a[0],4);
        if(pick%2)for(unsigned j=0;j<4;++j)put16(cv_host(a[0])+2*j,(uint16_t)cv_next());
        eax=a[0];
        break;
    case 14: { /* rotate_2d: D and M by content; R.y lands the speed clamp
                * on 100h, -96h or their neighbours. */
        cv_dwords(v,0,a[0],3);cv_words(v,3,a[1],4);
        if(!cv_leader)break;
        int32_t speed=(int16_t)get16(cv_host(cv_leader)+0x220),target=pick%3==0?0x100:pick%3==1?-0x96:(int32_t)cv_small(pick>>4,300);
        target+=(int32_t)cv_small(pick>>12,1);
        put32(cv_host(a[2]),cv_next());put32(cv_host(a[2])+4,(uint32_t)((speed-target)*32+(int32_t)(pick>>20&31)));
        if(pick%23==0)put16(cv_host(cv_leader)+0x220,(uint16_t)cv_next());
        break; }
    case 15: case 16: /* 46a9e8 / 46aa60: the speed and matrix flag are reread. */
        if(pick%5==0)put16(o+0x220,(uint16_t)cv_next());
        if(pick%5==1)put16(o+0x32,(uint16_t)(pick>>8&1));
        break;
    case 17: /* transform_point: velocity by content, result written. */
        cv_dwords(v,0,a[0],3);
        for(unsigned j=0;j<3;++j)put32(cv_host(a[2])+4*j,cv_next());
        break;
    case 18: /* 403ca8: the alive byte is reread. */
        if(pick%4==0)o[0x99]=(BYTE)(pick>>8&1);
        break;
    case 19: /* turret: angle words, link and alive byte reread. */
        if(pick%5==0)put16(o+0x20d,(uint16_t)cv_next());
        if(pick%5==1)put32(o+0x1a4,pick>>8&1?0:cv_next());
        if(pick%5==2)o[0x99]=(BYTE)(pick>>8&1);
        break;
    case 20: /* turret_aim: the two angle words by content. */
        if(cv_ok(a[1],6)){v->input[0]=get16(cv_host(a[1]));v->input[1]=get16(cv_host(a[1])+4);}
        if(pick%4==0)o[0x99]=(BYTE)(pick>>8&1);
        break;
    case 21: /* 46a810: Z and speed are reread. */
        if(pick%4==0)put32(o+0x2e,cv_next());
        if(pick%4==1)put16(o+0x220,(uint16_t)cv_next());
        break;
    case 22: /* terrain_pose: Z is reread (and rewritten unchanged); like
              * the real 4029d8 (node.c) it may write new pitch/roll words
              * at +0Ch, which convoy_think's restore path overwrites. */
        if(pick%3==0)put32(o+0x2e,cv_next());
        if(pick>>8&1)put32(o+0xc,cv_next());
        break;
    case 23: /* 46a614: AX = 0 half of the time; the speed is reread. */
        if(pick%2)eax&=0xffff0000u;
        if(pick%5==0)put16(o+0x220,(uint16_t)cv_next());
        break;
    }
    v->eax=cv_stack(eax)?0:eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
/* Pushes the entry EAX/EDX/ECX for the record before the slot pointer. */
#define CV_STUB(n) __attribute__((naked)) static void cv_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _cv_stub_impl\n\taddl $24,%esp\n\tmovl _cv_ecx,%ecx\n\tret\n\t");}
CV_STUB(0) CV_STUB(1) CV_STUB(2) CV_STUB(3) CV_STUB(4) CV_STUB(5) CV_STUB(6) CV_STUB(7) CV_STUB(8) CV_STUB(9) CV_STUB(10)
CV_STUB(11) CV_STUB(12) CV_STUB(13) CV_STUB(14) CV_STUB(15) CV_STUB(16) CV_STUB(17) CV_STUB(18) CV_STUB(19) CV_STUB(20)
CV_STUB(21) CV_STUB(22) CV_STUB(23)
#undef CV_STUB
static void *cv_stubs[CV_IDS]={cv_stub_0,cv_stub_1,cv_stub_2,cv_stub_3,cv_stub_4,cv_stub_5,cv_stub_6,cv_stub_7,cv_stub_8,
    cv_stub_9,cv_stub_10,cv_stub_11,cv_stub_12,cv_stub_13,cv_stub_14,cv_stub_15,cv_stub_16,cv_stub_17,cv_stub_18,cv_stub_19,
    cv_stub_20,cv_stub_21,cv_stub_22,cv_stub_23};
/* Direct callees of each hook (bit i = cv_addresses[i]). */
static unsigned cv_which(unsigned index){
    if(index==ES2_HOOK_INDEX_convoy_steer)return 1u|1u<<1|1u<<2;
    if(index==ES2_HOOK_INDEX_convoy_seek)return 1u<<3|1u<<4|1u<<5|1u<<6;
    if(index==ES2_HOOK_INDEX_convoy_lead)return 1u<<5|1u<<6|1u<<7|1u<<8|1u<<9;
    /* The swap 47df04 (id 13) runs as real code; convoy_follow computes it in
     * core and only counts it. */
    if(index==ES2_HOOK_INDEX_convoy_follow)return 1u<<3|1u<<4|1u<<5|1u<<6|1u<<9|1u<<10|1u<<12|1u<<14;
    if(index==ES2_HOOK_INDEX_convoy_drive)return 1u<<10|1u<<12|1u<<15|1u<<16|1u<<17;
    return 1u<<18|1u<<19|1u<<20|1u<<21|1u<<22|1u<<23;
}
/* The time-scale word timer_word_step reads. */
static void cv_globals(BYTE *out,int restore){
    if(restore)memcpy(mapped+0xd3b64,out,2);else memcpy(out,mapped+0xd3b64,2);
}
static void cv_reset(void){cv_calls=0;cv_stream=cv_seed;cv_leader=0;}
static uint16_t cv_word(const uint16_t *values,unsigned count){return values[random32()%count];}
static uint32_t cv_coordinate(void){return cv_small(random32(),random32()%2?8000u:1u<<20);}
/* A member: vtable, group, alive bytes +99h/+0A4h, type +1A8h, speed,
 * timer, matrix flag; positions and matrices are random arena bytes in the
 * direct cases and bounded in the real pass. */
static void cv_member(BYTE *x,int real){
    uint32_t b=CV_BASE;
    put32(x,b+CV_VTABLE);put32(x+0x45,b+CV_GROUP);
    x[0x99]=(BYTE)(random32()%4?0:random32()%2?1:random32());
    x[0xa4]=(BYTE)(random32()%4?0:random32()%2?1:random32());
    put16(x+0x1a8,random32()%4?3:random32()%2?1:(uint16_t)random32());
    static const uint16_t speeds[]={0,1,0xffff,0xc8,0xff38,0x7fff,0x8000};
    put16(x+0x220,random32()%3?(uint16_t)cv_small(random32(),real?400:2000):cv_word(speeds,7));
    put16(x+0x32,random32()%2?0:random32()%2?1:(uint16_t)random32());
    x[0x227]=(BYTE)(random32()%2?0:random32()%2?1:random32());
    if(real){
        for(unsigned j=0;j<3;++j)put32(x+0x26+4*j,cv_coordinate());
        put16(x+0x223,(uint16_t)(random32()%3?random32()%300:0));
    }
}
static void cv_place(void){
    if(!cv_pages&&!(cv_pages=VirtualAlloc(NULL,0x30000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE))){fail("convoy arena pages");ExitProcess(1);}
    unsigned k=random32()%4;
    cv_arena=k<2?cv_storage:cv_pages+(k==2?0x10000u-0xcu:0x20000u-0xdu)-CV_OBJECT;
}
static void cv_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    cv_place();uint32_t b=CV_BASE;BYTE *o=cv_arena+CV_OBJECT,*g=cv_arena+CV_GROUP;
    cv_seed=random32();random_bytes(cv_arena,CV_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    for(unsigned i=0;i<4;++i)cv_member(cv_arena+cv_member_offsets[i],real);
    put32(cv_arena+CV_VTABLE+0x78,(uint32_t)(uintptr_t)cv_stubs[11]);
    /* Group: holder (index word, route), member list, count. */
    unsigned order[4]={0,1,2,3};
    for(unsigned i=3;i>0;--i){unsigned j=random32()%(i+1),t=order[i];order[i]=order[j];order[j]=t;}
    for(unsigned i=0;i<4;++i)put32(cv_arena+CV_LIST+4*i,b+cv_member_offsets[order[i]]);
    put32(g+0xc,b+CV_LIST);put32(cv_arena+CV_GROUP2+0xc,b+CV_LIST);
    unsigned count=1+random32()%4;put16(g+0x10,(uint16_t)(real||random32()%2?count:1+random32()%0x7fff));
    unsigned points=1+random32()%6;
    put16(cv_arena+CV_ROUTE,(uint16_t)points);put32(cv_arena+CV_ROUTE+2,b+CV_ARRAY);
    for(unsigned i=0;i<points;++i)put32(cv_arena+CV_ARRAY+4*i,b+CV_POINTS+12u*(random32()%8));
    if(random32()%3==0)put32(cv_arena+CV_ARRAY+4*(points-1),get32(cv_arena+CV_ARRAY));
    for(unsigned i=0;i<8;++i)for(unsigned j=0;j<3;++j)put32(cv_arena+CV_POINTS+12*i+4*j,cv_coordinate());
    put16(g+4,(uint16_t)(real||random32()%2?random32()%(points+1):random32()));put32(g+6,b+CV_ROUTE);
    put16(cv_arena+CV_GROUP2+4,(uint16_t)(random32()%(points+1)));put32(cv_arena+CV_GROUP2+6,b+CV_ROUTE);
    /* Configuration word +2Eh, turret link, angle words. */
    put32(o+0x1f2,b+CV_CONFIG);
    put16(cv_arena+CV_CONFIG+0x2e,random32()%3?(uint16_t)(random32()%2):(uint16_t)random32());
    put32(o+0x1a4,random32()%2?0:random32());
    /* Leader yaw at the 8000h and 4000h boundaries of the follow test. */
    static const uint16_t turns[]={0x8000,0x4000,0x3fff,0xc000,0xc001,0x4001,0xbfff,0};
    for(unsigned i=1;i<4;++i)if(random32()%2)put16(cv_arena+cv_member_offsets[i]+0x10,(uint16_t)(get16(o+0x10)+cv_word(turns,8)));
    for(unsigned j=0;j<3;++j){put32(cv_arena+CV_POINT+4*j,cv_coordinate());put32(cv_arena+CV_POINT2+4*j,cv_coordinate());}
    a[0]=b+CV_OBJECT;
    if(index==ES2_HOOK_INDEX_convoy_steer){
        static const uint32_t steers[]={0x100,0xff,0x101,0xff00,0xff01,0xfeff,0,0x8000,0x7fff};
        a[1]=random32()%2?steers[n%9]|(random32()%2?random32()&0xffff0000u:0):random32();
        a[2]=random32();
    } else if(index==ES2_HOOK_INDEX_convoy_seek){
        a[1]=b+(random32()%2?CV_POINT:CV_POINT2);a[2]=random32();
        a[3]=random32()%3==0?0:random32()%2?random32()&0xffff0000u:random32();
    }
    if(real){
        put16(mapped+0xd3b64,(uint16_t)(1+random32()%200));
        /* The leader search must find an alive member in the counted part
         * of the list (the original reads a null member otherwise). */
        unsigned alive=random32()%count;BYTE *m=cv_host(get32(cv_arena+CV_LIST+4*alive));m[0x99]=0;m[0xa4]=0;
        if(random32()%3==0){o[0x99]=0;put16(cv_host(get32(cv_arena+CV_LIST))+0x1a8,3);}
    }
}
/* Original entry with EAX/EDX/ECX from r and the chosen EDI. */
__attribute__((naked,noinline)) static void cv_machine(uint32_t target,const uint32_t *args,unsigned count,uint32_t *registers,uint32_t edi){
    __asm__ volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl 12(%ebp),%esi\n\tmovl 16(%ebp),%ebx\n\t"
        "testl %ebx,%ebx\n\tjz 2f\n\t1: decl %ebx\n\tpushl (%esi,%ebx,4)\n\ttestl %ebx,%ebx\n\tjnz 1b\n\t"
        "2: movl 20(%ebp),%eax\n\tmovl 4(%eax),%edx\n\tmovl 8(%eax),%ecx\n\tmovl (%eax),%eax\n\tmovl 24(%ebp),%edi\n\tcall *8(%ebp)\n\t"
        "pushl %ecx\n\tmovl 20(%ebp),%ecx\n\tmovl %eax,0(%ecx)\n\tmovl %edx,4(%ecx)\n\tpopl %eax\n\tmovl %eax,8(%ecx)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* sg_poison_call with EDI = the caller's value (object + 26h). */
__attribute__((naked)) static void cv_poison_call(void *target,const uint32_t *a,uint32_t pattern,uint32_t pair[2],uint32_t edi){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 16(%ebp),%eax\n\tmovl $1024,%ecx\n\trep stosl\n\taddl $3840,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 20(%esi)\n\tpushl 16(%esi)\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 24(%ebp),%edi\n\tmovl 16(%ebp),%eax\n\tmovl %eax,%edx\n\tmovl %eax,%ecx\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%ecx\n\tmovl %eax,(%ecx)\n\tmovl %edx,4(%ecx)\n\tleal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static uint64_t cv_original(unsigned index,const uint32_t *a,const uint32_t *r,uint32_t edi){
    const es2_hook_descriptor *d=function_hook_descriptor(index);uint32_t regs[3]={r[0],r[1],r[2]};
    cv_machine((uint32_t)(uintptr_t)(mapped+d->rva),a,d->arg_count,regs,edi);
    return (uint64_t)regs[1]<<32|regs[0];
}
static int cv_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const cv_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,cv_calls);
    for(unsigned j=0;j<calls&&j<cv_calls&&expected;++j)if(memcmp(expected+j,cv_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(cv_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<CV_SIZE;++j)if(cv_after[j]!=cv_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,cv_after[j],cv_arena[j]);break;}
    return 1;
}
/* call_47df04 (not stubbed) counts the swaps, each directly
 * followed by rotate_2d 47ffe8 (id 14) in convoy_follow. */
static unsigned cv_expected_hits(const callee_counter *c){
    unsigned hits=0;const uint32_t key=c->event==ES2_COLLIDE_CALL&&c->key==0x47df04u?0x47ffe8u:c->key;
    for(unsigned j=0;j<cv_calls;++j){unsigned id=cv_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?cv_slots[id]==key:!cv_slots[id]&&cv_addresses[id]==key)++hits;}
    return hits;
}
static int cv_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[CV_GLOBALS],after[CV_GLOBALS],actual[CV_GLOBALS];static cv_record expected[CV_MAX];
    uint32_t edi=a[0]+0x26u;cv_mask=0;
    if(index==ES2_HOOK_INDEX_convoy_follow&&(n&1)){edi=random32();cv_mask=1;}
    memcpy(cv_before,cv_arena,CV_SIZE);cv_globals(before,0);
    cv_reset();uint64_t pair[2];pair[0]=cv_original(index,a,r,edi);unsigned calls=cv_calls;memcpy(expected,cv_log,calls*sizeof(*expected));
    memcpy(cv_after,cv_arena,CV_SIZE);cv_globals(after,0);
    memcpy(cv_arena,cv_before,CV_SIZE);cv_globals(before,1);cv_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);cv_globals(actual,0);cv_mask=0;
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=cv_expected_hits(c+i))return fail("convoy callee counter delta");
    if(pair[0]!=pair[1]||calls!=cv_calls||memcmp(expected,cv_log,calls*sizeof(*expected))||memcmp(cv_after,cv_arena,CV_SIZE)||memcmp(after,actual,CV_GLOBALS))
        return cv_report("direct",n,pair,calls,expected);
    cv_globals(before,1);
    return 0;
}
static int cv_patch(BYTE saved[CV_IDS][5],int active[CV_IDS],unsigned which,int on){
    for(unsigned i=0;i<CV_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!cv_addresses[i])continue;if(actor_patch(cv_addresses[i]-0x400000u,cv_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(cv_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static int cv_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;cv_build(index,n,a,r,0);
        BYTE before[CV_GLOBALS],after[CV_GLOBALS],actual[CV_GLOBALS],expected_fpu[108],actual_fpu[108];static cv_record expected[CV_MAX];
        memcpy(cv_before,cv_arena,CV_SIZE);cv_globals(before,0);
        cv_reset();cv_poison_call(mapped+d->rva,a,pattern,expected_pair,a[0]+0x26u);es2_hook_fpu_save(expected_fpu);
        unsigned calls=cv_calls;memcpy(expected,cv_log,calls*sizeof(*expected));memcpy(cv_after,cv_arena,CV_SIZE);cv_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("convoy poison install");
        memcpy(cv_arena,cv_before,CV_SIZE);cv_globals(before,1);cv_reset();cv_poison_call(mapped+d->rva,a,pattern,actual_pair,a[0]+0x26u);
        es2_hook_fpu_save(actual_fpu);cv_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=cv_calls||memcmp(expected,cv_log,calls*sizeof(*expected))||memcmp(cv_after,cv_arena,CV_SIZE)||
           memcmp(after,actual,CV_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=cv_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("convoy poison uninstall");cv_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state\n",d->name,total);
    return status;
}
/* Real callees with the nested hooks in rotating original/replaced/compare
 * modes (compare falls back to replaced where refused). The virtual +78h
 * stays a stub; under convoy_think the base update, turret, turret_aim,
 * terrain_pose and the collision test 46a614 stay stubs (keep masks). The
 * leader search 46a5bc, 4929fc and 47df04 run as original code. Pair,
 * arena and the time-scale word are compared against a run with only the
 * nested hooks. */
static unsigned cv_nested(unsigned index,unsigned nested[6],unsigned *keep){
    unsigned count=0;*keep=0;
    if(index==ES2_HOOK_INDEX_convoy_steer){
        nested[count++]=ES2_HOOK_INDEX_timer_word_step;nested[count++]=ES2_HOOK_INDEX_approach_word;nested[count++]=ES2_HOOK_INDEX_multiply_q8;}
    else if(index==ES2_HOOK_INDEX_convoy_seek){
        nested[count++]=ES2_HOOK_INDEX_dist2d_point;nested[count++]=ES2_HOOK_INDEX_heading_point;nested[count++]=ES2_HOOK_INDEX_convoy_steer;
        nested[count++]=ES2_HOOK_INDEX_timer_word_step;nested[count++]=ES2_HOOK_INDEX_approach_word;nested[count++]=ES2_HOOK_INDEX_multiply_q8;}
    else if(index==ES2_HOOK_INDEX_convoy_lead){
        nested[count++]=ES2_HOOK_INDEX_route_point;nested[count++]=ES2_HOOK_INDEX_heading_point;nested[count++]=ES2_HOOK_INDEX_convoy_seek;
        nested[count++]=ES2_HOOK_INDEX_route_advance;nested[count++]=ES2_HOOK_INDEX_convoy_steer;nested[count++]=ES2_HOOK_INDEX_dist2d_point;}
    else if(index==ES2_HOOK_INDEX_convoy_follow){
        nested[count++]=ES2_HOOK_INDEX_euler_matrix;nested[count++]=ES2_HOOK_INDEX_rotate_2d;nested[count++]=ES2_HOOK_INDEX_dist2d_point;
        nested[count++]=ES2_HOOK_INDEX_heading_point;nested[count++]=ES2_HOOK_INDEX_convoy_seek;nested[count++]=ES2_HOOK_INDEX_convoy_steer;}
    else if(index==ES2_HOOK_INDEX_convoy_drive){
        nested[count++]=ES2_HOOK_INDEX_convoy_lead;nested[count++]=ES2_HOOK_INDEX_convoy_follow;nested[count++]=ES2_HOOK_INDEX_euler_matrix;
        nested[count++]=ES2_HOOK_INDEX_transform_point;nested[count++]=ES2_HOOK_INDEX_convoy_seek;nested[count++]=ES2_HOOK_INDEX_convoy_steer;}
    else {
        nested[count++]=ES2_HOOK_INDEX_convoy_drive;nested[count++]=ES2_HOOK_INDEX_convoy_lead;nested[count++]=ES2_HOOK_INDEX_convoy_follow;
        nested[count++]=ES2_HOOK_INDEX_convoy_seek;nested[count++]=ES2_HOOK_INDEX_convoy_steer;nested[count++]=ES2_HOOK_INDEX_transform_point;
        *keep=1u<<18|1u<<19|1u<<20|1u<<22|1u<<23;}
    return count;
}
static int cv_real_pass(unsigned index){
    unsigned nested[6],keep,count=cv_nested(index,nested,&keep);
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[6]={0};
    BYTE saved[CV_IDS][5];int active[CV_IDS];
    if(cv_patch(saved,active,keep,1))return 1;
    cv_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;cv_build(index,n,a,r,1);
        BYTE before[CV_GLOBALS],after[CV_GLOBALS],actual[CV_GLOBALS];
        memcpy(cv_before,cv_arena,CV_SIZE);cv_globals(before,0);
        es2_hook hk[7];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("convoy nested install");}
        cv_reset();cv_poison_call(mapped+d->rva,a,pattern,expected_pair,a[0]+0x26u);
        memcpy(cv_after,cv_arena,CV_SIZE);cv_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+6,d,&o,function_hook_extension(index)))return fail("convoy caller install");
        memcpy(cv_arena,cv_before,CV_SIZE);cv_globals(before,1);cv_reset();
        cv_poison_call(mapped+d->rva,a,pattern,actual_pair,a[0]+0x26u);cv_globals(actual,0);
        if(expected_pair[0]!=actual_pair[0]||expected_pair[1]!=actual_pair[1]||memcmp(cv_after,cv_arena,CV_SIZE)||memcmp(after,actual,CV_GLOBALS)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            fprintf(stderr,"real mode=%u\n",mode);cv_calls=0;status=cv_report("real",n,pair,0,NULL);
        }
        for(unsigned j=0;j<7;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("convoy nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("convoy nested uninstall");}
        cv_globals(before,1);++total;
    }
    cv_real=0;
    if(cv_patch(saved,active,0,0))return 1;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, 31/a7 stack, full pair and memory\n",d->name,total,count);
    return status;
}
/* Compare cases (compare_probes.h) for convoy_steer, real callees. */
static void cv_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){cv_build(index,n,a,r,1);}
static void cv_compare_state(BYTE *s,int restore){
    if(restore){memcpy(cv_arena,s,CV_SIZE);cv_globals(s+CV_SIZE,1);}else{memcpy(s,cv_arena,CV_SIZE);cv_globals(s+CV_SIZE,0);}
}
static int convoy_cases(unsigned index,unsigned limit){
    unsigned which=cv_which(index);
    BYTE saved[CV_IDS][5],old_globals[CV_GLOBALS];int active[CV_IDS];
    cv_globals(old_globals,0);
    if(limit&&index==ES2_HOOK_INDEX_convoy_steer&&compare_probe_installed(index)){
        int status=compare_probe_run(index,limit,cv_compare_build,cv_compare_state,CV_SIZE+CV_GLOBALS);cv_globals(old_globals,1);return status;}if(cv_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_CONVOY_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];cv_build(index,n,a,r,0);status=cv_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=cv_installed_probes(index,64);
    if(cv_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=cv_real_pass(index);
    if(!limit&&!status&&index==ES2_HOOK_INDEX_convoy_steer)
        status=compare_probe_run(index,0,cv_compare_build,cv_compare_state,CV_SIZE+CV_GLOBALS);
    cv_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, frame payloads by content, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
