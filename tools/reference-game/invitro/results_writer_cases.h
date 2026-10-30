/* results_writer group: results_writer 42412c, score_sum 423e88, vehicle_value
 * 418e60 and list_append 426ac8 (docs/reverse-engineering/mission_runtime.md,
 * "results writer 42412c and its helpers"). Direct cases run the original body
 * (through an own caller that returns ECX), the adapter and the core (for ECX)
 * on an arena with every game callee replaced by a logging stub: for the writer
 * the record setup 49630c (links a record at EBP-38h into FS:0, as in
 * meteor_cases.h), objective_eval 413280, the path builder 45ee00, the stream
 * open 47b064 (builds the stream object with one of two arena tables, a nonzero
 * error dword an eighth of the time), the fatal report 479c78, the write
 * virtual +14h (through the tables), score_sum 423e88, multiply_q10 47e0b0,
 * result_conditions 423d68, the close 47b09c and the block write 47b2b8; for
 * score_sum pool_prev 471c70 (a planned walk) and vehicle_value 418e60; for
 * vehicle_value part_ratio 40dbc0, list_append 426ac8, damage_score 41537c and
 * multiply_q10. Ordered calls with the slot count the bridge pushed, arguments
 * and entry EAX/EDX/ECX (stack values relative to the writer's FS:0 record when
 * inside its frame, else masked), the record's context word and up to 8 bytes
 * each write receives (the dword handed to list_append). The stubs change what
 * the originals reread: the stream's table, the pair count and list words (the
 * list pointer is read once), the team slot f-3Ch through the first write's
 * buffer, the bank word 4a9e98, the MECH list dword, the team's side byte, the
 * weapon list, its pointer and count. Compared: the pair, ECX (original through
 * an own caller against the core; vehicle_value only when it appended), the
 * arena, the globals, FS:0 and the callee counter deltas. Then installed 31/a7
 * poison probes and real-callee passes (vehicle_value and score_sum without
 * stubs over consistent vehicles, part tables and a real pool list; the writer
 * with real 49630c and only the platform calls, objective_eval and
 * condition_record stubbed), every nested hook in rotating modes. Identifiers:
 * rwr_/RWR_. */
/* raw: args, regs and eax before rwr_mask (see rwr_same). */
typedef struct rwr_record { uint32_t id,n,args[6],regs[3],context,eax,edx;BYTE data[8];uint32_t raw[10]; } rwr_record;
enum { RWR_SIZE=0x1900, RWR_P=0x000, RWR_T=0x080, RWR_T2=0x0c0, RWR_LIST_A=0x100, RWR_LIST_B=0x180,
    RWR_TABLE_A=0x200, RWR_TABLE_B=0x240, RWR_PATH=0x280, RWR_POOL=0x2c0, RWR_COUNTER=0x2e0, RWR_SIDES=0x300,
    RWR_MEMBERS=0x380, RWR_W=0x3c0, RWR_W2=0x3e0, RWR_ITEMS=0x400, RWR_ITEMS2=0x420, RWR_ITEM=0x440, RWR_DEF=0x540,
    RWR_CFG=0x840, RWR_X=0x8a0, RWR_RECS=0x8c0, RWR_ENTRIES=0xae0, RWR_MAXIMA=0xb00, RWR_VALUES=0xb40,
    RWR_GROUPV=0xb80, RWR_FLAGS=0xbc0, RWR_NODES=0xc00, RWR_NODE_STRIDE=0x300, RWR_VEHICLES=4,
    RWR_MAX=256, RWR_IDS=17, RWR_PLAN=8, RWR_GLOBALS=0x7ec+8 };
#define RWR_BASE ((uint32_t)(uintptr_t)rwr_arena)
static BYTE rwr_arena[RWR_SIZE],rwr_before[RWR_SIZE],rwr_after[RWR_SIZE];
static rwr_record rwr_log[RWR_MAX];
static unsigned rwr_calls,rwr_writes,rwr_plan_count,rwr_plan_at;
static int rwr_real,rwr_frame;
static uint32_t rwr_seed,rwr_stream,rwr_v,rwr_plan[RWR_PLAN];
__attribute__((used)) static uint32_t rwr_ecx;
/* 0 49630c, 1 413280, 2 45ee00, 3 47b064, 4 479c78, 5 write +14h (tables),
 * 6 423e88, 7 47e0b0, 8 423d68, 9 47b09c, 10 47b2b8, 11 471c70, 12 418e60,
 * 13 40dbc0, 14 426ac8, 15 41537c, 16 41b4ec. */
static const uint32_t rwr_addresses[RWR_IDS]={0x49630c,0x413280,0x45ee00,0x47b064,0x479c78,0,0x423e88,0x47e0b0,0x423d68,
    0x47b09c,0x47b2b8,0x471c70,0x418e60,0x40dbc0,0x426ac8,0x41537c,0x41b4ec};
static const unsigned rwr_counts[RWR_IDS]={0,1,1,2,6,3,2,2,2,2,3,2,1,2,1,1,2};
static uint32_t rwr_next(void){
    uint32_t z=rwr_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *rwr_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static BYTE *rwr_at(unsigned offset){return rwr_arena+offset;}
static uint32_t rwr_vehicle(unsigned k){return RWR_BASE+RWR_NODES+RWR_NODE_STRIDE*k+8u;}
static int rwr_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static uint32_t rwr_fs0(void){uint32_t v;__asm__ volatile("movl %%fs:0,%0":"=r"(v));return v;}
/* Stack values: relative to the writer's record (f-38h) inside its frame
 * f-4Ah..f-1 (direct writer passes), else masked. */
static uint32_t rwr_mask(uint32_t v){
    if(!rwr_stack(v))return v;
    if(rwr_frame){const int32_t rel=(int32_t)(v-rwr_fs0());if(rel>=-0x14&&rel<0x38)return 0x5f000000u|((uint32_t)rel&0xffffu);}
    return 0;
}
/* A Q8 condition word for the condition_record stub. */
static uint16_t rwr_condition(uint32_t pick){return (uint16_t)(pick%8u==0?pick>>8:(pick>>8)%0x101u);}
static uint32_t rwr_answer(unsigned id,const uint32_t *a,rwr_record *v,uint32_t eax,uint32_t pick){
    const uint32_t b=RWR_BASE;
    switch(id){
    case 1: return pick%2?(eax&0xffff0000u)|9u:eax; /* objective status 9 half of the time */
    case 2: return b+RWR_PATH+pick%16u;
    case 3: /* open: the stream object in the caller's frame */
        put32(rwr_host(a[0]),b+(pick>>4&1?RWR_TABLE_B:RWR_TABLE_A));put32(rwr_host(a[0]+4),pick%8==0?rwr_next()|1u:0);
        put16(rwr_host(a[0]+8),(uint16_t)rwr_next());rwr_host(a[0])[10]=(BYTE)rwr_next();
        return a[0];
    case 5: { /* write(stream, size, pointer): the bytes it receives */
        const uint32_t size=a[1]<8u?a[1]:8u;memcpy(v->data,rwr_host(a[2]),size);
        if(!rwr_real){
            if(pick%13==0)put32(rwr_host(a[0]),b+(pick>>8&1?RWR_TABLE_B:RWR_TABLE_A));
            if(pick%11==0)put16(mapped+0xa9e6a,(uint16_t)((pick>>8)%6u));
            if(pick%17==0)put32(mapped+0xa9e6c,b+(pick>>9&1?RWR_LIST_B:RWR_LIST_A));
            if(pick%7==0)put16(rwr_at(RWR_LIST_A+2u*((pick>>10)%0x80u)),(uint16_t)rwr_next());
            if(rwr_writes==0&&pick%5==0)put32(rwr_host(a[2]+2u),b+RWR_T2); /* the team slot f-3Ch */
        }
        ++rwr_writes;return eax;
    }
    case 7: if(!rwr_real&&pick%3==0)put16(mapped+0xa9e98,(uint16_t)rwr_next());return eax;
    case 10: memcpy(v->data,rwr_host(a[2]),8);return eax;
    case 11: /* pool_prev: the planned walk; may move the MECH list dword */
        if(pick%5==0)put32(mapped+0xa9b7a,rwr_next());
        return rwr_plan_at<rwr_plan_count?rwr_plan[rwr_plan_at++]:0;
    case 12: /* vehicle_value: may change the team's or a vehicle's side byte */
        if(pick%5==0)rwr_arena[RWR_T+0x12]^=(BYTE)(pick>>8|1u);
        if(pick%7==0)rwr_arena[RWR_SIDES+0x20*(pick>>12&3)+0x12]^=(BYTE)(pick>>16|1u);
        return eax;
    case 13: { /* part_ratio: ratios around the 80h and 100h boundaries */
        static const uint16_t ratios[]={0,0x40,0x7f,0x80,0x81,0x100,0xff00,0x8000,0x7fff,0xffff};
        eax=(eax&0xffff0000u)|(pick%4?ratios[(pick>>4)%10u]:(uint16_t)rwr_next());
        if(pick%7==0)put16(rwr_host(get32(rwr_host(rwr_v+0x202))+8),(uint16_t)((pick>>8)%6u));
        if(pick%11==0)put32(rwr_host(rwr_v+0x202),b+(pick>>12&1?RWR_W2:RWR_W));
        return eax;
    }
    case 14: memcpy(v->data,rwr_host(a[0]),4);return eax;
    case 16: /* condition_record(actor, out): word 0 and the 41 condition words */
        put16(rwr_host(a[1]),(uint16_t)rwr_next());
        for(unsigned i=0;i<41;++i)put16(rwr_host(a[1]+2u+2u*i),rwr_condition(rwr_next()));
        return eax;
    }
    return eax;
}
__attribute__((used,noinline)) static uint64_t __cdecl rwr_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(rwr_calls==RWR_MAX){fail("results_writer synthetic call overflow");ExitProcess(1);}
    rwr_record *v=rwr_log+rwr_calls++;memset(v,0,sizeof(*v));v->id=id;
    const unsigned n=weapons_bridge_slots(a,rwr_counts[id]);v->n=n;
    for(unsigned j=0;j<n&&j<6;++j){v->raw[j]=a[j];v->args[j]=rwr_mask(a[j]);}
    for(unsigned j=0;j<3;++j){v->raw[6+j]=regs[j];v->regs[j]=rwr_mask(regs[j]);}
    if(rwr_frame&&id&&id<=10)v->context=get16(rwr_host(rwr_fs0()+16u));
    uint32_t eax=rwr_next(),edx=rwr_next(),pick=rwr_next();rwr_ecx=rwr_next();
    if(id==0)eax=rwr_fs0(); /* the record stub returns the real FS:0 head */
    else eax=rwr_answer(id,a,v,eax,pick);
    v->raw[9]=eax;v->eax=rwr_mask(eax);v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define RWR_STUB(n) __attribute__((naked)) static void rwr_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _rwr_stub_impl\n\taddl $24,%esp\n\tmovl _rwr_ecx,%ecx\n\tret\n\t");}
RWR_STUB(1) RWR_STUB(2) RWR_STUB(3) RWR_STUB(4) RWR_STUB(5) RWR_STUB(6) RWR_STUB(7) RWR_STUB(8) RWR_STUB(9) RWR_STUB(10)
RWR_STUB(11) RWR_STUB(12) RWR_STUB(13) RWR_STUB(14) RWR_STUB(15) RWR_STUB(16)
#undef RWR_STUB
/* 49630c: captures the entry registers, then links a record at EBP +
 * [descriptor+4] into FS:0 with the saved head at record+0 and a zero
 * context word at record+16, as the original does. */
__attribute__((naked)) static void rwr_stub_0(void){__asm__ volatile(
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
    "pushl $0\n\tcalll _rwr_stub_impl\n\taddl $12,%esp\n\t"
    "popl %ecx\n\tmovl 4(%ecx),%ecx\n\taddl %ebp,%ecx\n\tmovl %eax,(%ecx)\n\tmovw $0,16(%ecx)\n\tmovl %ecx,%fs:0\n\t"
    "popl %edx\n\taddl $4,%esp\n\tmovl _rwr_ecx,%ecx\n\tret\n\t");}
static void *rwr_stubs[RWR_IDS]={rwr_stub_0,rwr_stub_1,rwr_stub_2,rwr_stub_3,rwr_stub_4,rwr_stub_5,rwr_stub_6,rwr_stub_7,
    rwr_stub_8,rwr_stub_9,rwr_stub_10,rwr_stub_11,rwr_stub_12,rwr_stub_13,rwr_stub_14,rwr_stub_15,rwr_stub_16};
/* A three-slot cdecl caller that hands over EAX/EDX/ECX and returns all
 * three (the hook ABI returns EDX:EAX only). */
__attribute__((naked,noinline)) static void rwr_machine(void *target,const uint32_t *a,const uint32_t *registers,uint32_t *out){
    __asm__ volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl 12(%ebp),%esi\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* Globals: 4a9e54 (mode word) .. the end of the variable bank 4a9e70+7D0h
 * (the bank word 4a9e98, the pair count 4a9e6a and list 4a9e6c inside),
 * the MECH list 4a9b7a and the counter pointer 6cbee4. */
static void rwr_globals(BYTE *out,int restore){
    static const struct { uint32_t rva,size; } r[]={{0xa9e54,0x7ec},{0xa9b7a,4},{0x2cbee4,4}};
    for(unsigned i=0;i<3;++i){if(restore)memcpy(mapped+r[i].rva,out,r[i].size);else memcpy(out,mapped+r[i].rva,r[i].size);out+=r[i].size;}
}
static void rwr_reset(void){rwr_calls=0;rwr_writes=0;rwr_plan_at=0;rwr_stream=rwr_seed;}
/* Part tables part_ratio and damage_score can divide by (bases >= 1,
 * nonnegative maxima, indices in range). */
static void rwr_parts(void){
    const uint32_t b=RWR_BASE;
    put32(rwr_at(RWR_X),b+RWR_RECS);put32(rwr_at(RWR_X+4),b+RWR_MAXIMA);
    for(unsigned i=0;i<29;++i){BYTE *rec=rwr_at(RWR_RECS+18*i);
        put16(rec,(uint16_t)(1u+random32()%0x7fffu));put16(rec+6,(uint16_t)(random32()%5));put32(rec+8,b+RWR_ENTRIES);}
    for(unsigned i=0;i<8;++i)put16(rwr_at(RWR_ENTRIES+4*i+2),(uint16_t)(random32()%12));
    for(unsigned i=0;i<32;++i){
        put16(rwr_at(RWR_MAXIMA+2*i),(uint16_t)(random32()%4?random32()%0x200u:0));
        put16(rwr_at(RWR_VALUES+2*i),(uint16_t)(random32()%4?random32()%0x200u:0x8000u|random32()));
        put16(rwr_at(RWR_GROUPV+2*i),(uint16_t)(random32()%4?random32()%0x200u:0x8000u|random32()));
        put16(rwr_at(RWR_FLAGS+2*i),(uint16_t)(random32()%3?random32()|1u:0));
    }
}
static void rwr_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    const uint32_t b=RWR_BASE;
    rwr_seed=random32();random_bytes(rwr_arena,RWR_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    put32(mapped+0x2cbee4,b+RWR_COUNTER);
    put32(rwr_at(RWR_TABLE_A+0x14),(uint32_t)(uintptr_t)rwr_stub_5);put32(rwr_at(RWR_TABLE_B+0x14),(uint32_t)(uintptr_t)rwr_stub_5);
    static const uint16_t counts[]={0,1,2,3,5,0xffff,0x8000,4};
    put16(mapped+0xa9e6a,real?(uint16_t)(random32()%4):n<8?counts[n]:counts[random32()%8]);
    put32(mapped+0xa9e6c,b+(random32()%2?RWR_LIST_B:RWR_LIST_A));
    put16(mapped+0xa9e54,(uint16_t)random32());put16(mapped+0xa9e98,(uint16_t)random32());
    put32(mapped+0xa9b7a,b+RWR_POOL);
    static const BYTE sides[]={0,1,0xfe,0x80};
    for(unsigned k=0;k<4;++k)rwr_arena[RWR_SIDES+0x20*k+0x12]=sides[random32()%4];
    rwr_arena[RWR_T+0x12]=sides[random32()%4];rwr_arena[RWR_T2+0x12]=sides[random32()%4];
    for(unsigned k=0;k<RWR_VEHICLES;++k){
        const uint32_t v=rwr_vehicle(k);BYTE *h=rwr_host(v);
        put32(h+0x45,b+RWR_SIDES+0x20*(random32()%4));
        h[0xa4]=(BYTE)(random32()%3?0:random32()|1u);h[0x99]=(BYTE)(random32()%2?0:random32()|1u);
        h[0xb3]=(BYTE)(random32()%4?0:random32()|1u);
        put32(h+0x202,b+(random32()%2?RWR_W2:RWR_W));put32(h+0x1f2,b+RWR_CFG);
        put32(h+0x206,b+RWR_VALUES);put32(h+0x20a,b+RWR_GROUPV);put32(h+0x20e,b+RWR_FLAGS);put32(h+0x212,b+RWR_X);
    }
    static const uint16_t weapons[]={0,1,2,3,5,0xffff,0x8000};
    put32(rwr_at(RWR_W+4),b+RWR_ITEMS);put32(rwr_at(RWR_W2+4),b+RWR_ITEMS2);
    put16(rwr_at(RWR_W+8),real?(uint16_t)(random32()%4):weapons[random32()%7]);
    put16(rwr_at(RWR_W2+8),real?(uint16_t)(random32()%4):weapons[random32()%7]);
    for(unsigned i=0;i<16;++i)put32(rwr_at(RWR_ITEMS+4*i),random32()%5==0?0:b+RWR_ITEM+0x20*(random32()%8));
    for(unsigned k=0;k<8;++k){
        const uint32_t d=b+RWR_DEF+0x60*k;put32(rwr_at(RWR_ITEM+0x20*k+0x18),d);put32(rwr_at(RWR_ITEM+0x20*k+0x1c),d);
        rwr_host(d)[0x17]=real?(BYTE)(random32()%4?random32()%10:0u-(1u+random32()%19)):(BYTE)random32();
        put16(rwr_host(d+0x56),(uint16_t)random32());
    }
    if(real)rwr_parts();
    /* The pool: nodes 0..count-1 linked both ways (real passes). */
    const unsigned pool=real?random32()%5:0;
    put32(rwr_at(RWR_POOL+0xe),pool?rwr_vehicle(0)-8u:0);
    for(unsigned k=0;k<pool;++k){
        put32(rwr_host(rwr_vehicle(k)-8u),k+1<pool?rwr_vehicle(k+1)-8u:0);put32(rwr_host(rwr_vehicle(k)-4u),k?rwr_vehicle(k-1)-8u:0);
    }
    /* The team: side byte, members (vehicles) for result_conditions. */
    put32(rwr_at(RWR_T+0x0c),b+RWR_MEMBERS);put16(rwr_at(RWR_T+0x10),(uint16_t)(random32()%4));
    put32(rwr_at(RWR_T2+0x0c),b+RWR_MEMBERS);put16(rwr_at(RWR_T2+0x10),(uint16_t)(random32()%4));
    for(unsigned k=0;k<4;++k)put32(rwr_at(RWR_MEMBERS+4*k),rwr_vehicle(random32()%4));
    put32(rwr_at(RWR_P+0x45),b+RWR_T);
    rwr_plan_count=random32()%6;for(unsigned k=0;k<rwr_plan_count;++k)rwr_plan[k]=rwr_vehicle(random32()%4);
    rwr_v=rwr_vehicle(random32()%4);
    if(index==ES2_HOOK_INDEX_results_writer)a[0]=b+RWR_P;
    else if(index==ES2_HOOK_INDEX_score_sum){a[0]=b+RWR_T;a[1]=random32();}
    else if(index==ES2_HOOK_INDEX_vehicle_value)a[0]=rwr_v;
    else {
        /* list_append: an arena dword; a list in the arena (counts -16..16)
         * or, an eighth of the time, a list whose store hits the count. */
        const int16_t c=(int16_t)(n<33?(int)n-16:(int)(random32()%33)-16);
        put16(mapped+0xa9e6a,(uint16_t)c);
        uint32_t list=b+RWR_LIST_A+0x40;
        if(n%8==7)list=(uint32_t)(uintptr_t)(mapped+0xa9e6a)-4u*(uint32_t)(int32_t)c;
        put32(mapped+0xa9e6c,list);
        a[0]=b+0x10+4*(random32()%16);
    }
}
/* Two logs agree when every word but the raw ones is equal, except that a
 * masked word (an argument, an entry register, the stub's EAX) may differ when
 * its raw value is the same in both runs. rwr_mask takes a value for the
 * writer's frame by its distance to the FS:0 record, which lies in the
 * original's frame in one run and in the core's scratch frame in the other; a
 * value that is no frame address but happens to lie in the worker thread's
 * stack range (a stub's random EAX/EDX, whose low word the pair loop replaces,
 * or the tally's EDX = s16(bank word) * 25000) mapped to 0 in one run and to a
 * frame offset in the other, depending on the randomized stack address (4 to 6
 * of 50 shard-1 runs: case 9188 write EAX, case 60689 tally write EDX,
 * 0/5f0000xxh). Equal raw values are the same value, and a frame address of the
 * original's frame cannot recur raw in the core's run, so nothing a masked
 * comparison catches is lost. rwr_raw_matches counts the words accepted that
 * way (PASS line). */
static unsigned rwr_raw_matches;
static int rwr_same(const rwr_record *expected,const rwr_record *actual,unsigned calls){
    enum { WORDS=(unsigned)(offsetof(rwr_record,raw)/4u) };
    unsigned matches=0;
    for(unsigned j=0;j<calls;++j){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(actual+j);
        for(unsigned k=0;k<WORDS;++k){
            if(e[k]==f[k])continue;
            const unsigned raw=k>=2&&k<11?k-2:k==12?9u:10u;
            if(raw==10||expected[j].raw[raw]!=actual[j].raw[raw])return 0;
            ++matches;
        }
    }
    rwr_raw_matches+=matches;
    return 1;
}
static int rwr_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const rwr_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,rwr_calls);
    for(unsigned j=0;j<calls&&j<rwr_calls&&expected;++j)if(!rwr_same(expected+j,rwr_log+j,1)){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(rwr_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<RWR_SIZE;++j)if(rwr_after[j]!=rwr_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,rwr_after[j],rwr_arena[j]);break;}
    return 1;
}
static unsigned rwr_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<rwr_calls;++j)if(c->event==ES2_COLLIDE_SLOT?rwr_log[j].id==5:rwr_addresses[rwr_log[j].id]==c->key)++hits;
    return hits;
}
/* The core called directly (the adapters drop ECX). */
static uint64_t rwr_core(unsigned index,const uint32_t *a,const uint32_t *r,uint32_t *ecx){
    if(index==ES2_HOOK_INDEX_list_append)return es2_list_append_registers(&math_native_memory,(uint32_t)(uintptr_t)mapped,a[0],ecx);
    if(index==ES2_HOOK_INDEX_score_sum){
        es2_weapon_graph g=weapons_graph(mapped,NULL,index);g.observe=NULL;
        return es2_score_sum_registers(&math_native_memory,&g,a[0],r,ecx);
    }
    if(index==ES2_HOOK_INDEX_vehicle_value){
        BYTE scratch[ES2_VEHICLE_VALUE_SCRATCH]={0};es2_weapon_graph g=weapons_graph(mapped,scratch,index);g.observe=NULL;
        return es2_vehicle_value_registers(&math_native_memory,&g,a[0],r,ecx);
    }
    BYTE scratch[ES2_RESULTS_WRITER_SCRATCH]={0};es2_weapon_graph g=weapons_graph(mapped,scratch,index);g.observe=NULL;
    weapons_impact_context c={(uint32_t)(uintptr_t)mapped,(uint32_t)(uintptr_t)(scratch+ES2_RESULTS_WRITER_FRAME)};
    g.call=weapons_impact_guest_call;g.context=&c;
    uint64_t v=es2_results_writer_registers(&math_native_memory,&g,a[0],r,ecx);
    uint32_t head=(uint32_t)(v>>32);__asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");return v;
}
static int rwr_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[RWR_GLOBALS],after[RWR_GLOBALS],actual[RWR_GLOBALS];static rwr_record expected[RWR_MAX];
    uint32_t head=rwr_fs0(),out[3],core_ecx=0;
    memcpy(rwr_before,rwr_arena,RWR_SIZE);rwr_globals(before,0);
    rwr_reset();rwr_machine(mapped+d->rva,a,r,out);unsigned calls=rwr_calls;memcpy(expected,rwr_log,calls*sizeof(*expected));
    memcpy(rwr_after,rwr_arena,RWR_SIZE);rwr_globals(after,0);
    uint64_t pair[2];pair[0]=(uint64_t)out[1]<<32|out[0];
    if(rwr_fs0()!=head)return fail("results_writer original left FS:0 changed");
    int appended=0;for(unsigned j=0;j<calls;++j)if(expected[j].id==14)appended=1;
    memcpy(rwr_arena,rwr_before,RWR_SIZE);rwr_globals(before,1);rwr_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];for(unsigned i=0;i<count&&i<16;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);rwr_globals(actual,0);
    if(rwr_fs0()!=head)return fail("results_writer adapter left FS:0 changed");
    if(n<4096)for(unsigned i=0;i<count&&i<16;++i)if(c[i].count-counts[i]!=rwr_expected_hits(c+i))return fail("results_writer callee counter delta");
    if(pair[0]!=pair[1]||calls!=rwr_calls||!rwr_same(expected,rwr_log,calls)||memcmp(rwr_after,rwr_arena,RWR_SIZE)||
       memcmp(after,actual,RWR_GLOBALS)){
        for(unsigned j=0;j<RWR_GLOBALS;++j)if(after[j]!=actual[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,after[j],actual[j]);break;}
        return rwr_report("direct",n,pair,calls,expected);
    }
    memcpy(rwr_arena,rwr_before,RWR_SIZE);rwr_globals(before,1);rwr_reset();
    pair[1]=rwr_core(index,a,r,&core_ecx);rwr_globals(actual,0);
    if(rwr_fs0()!=head)return fail("results_writer core left FS:0 changed");
    /* With the hook installed (integration pass) the "original" run is the
       hook, whose ECX is the dispatcher's; vehicle_value's ECX is stack
       residue unless it appended. */
    const int ecx_known=!rng_hooks[index].installed&&(index!=ES2_HOOK_INDEX_vehicle_value||appended);
    if(pair[0]!=pair[1]||(ecx_known&&core_ecx!=out[2])||calls!=rwr_calls||!rwr_same(expected,rwr_log,calls)||
       memcmp(rwr_after,rwr_arena,RWR_SIZE)||memcmp(after,actual,RWR_GLOBALS)){
        fprintf(stderr," ECX %08x/%08x\n",out[2],core_ecx);return rwr_report("core ECX",n,pair,calls,expected);
    }
    rwr_globals(before,1);
    return 0;
}
static int rwr_patch(BYTE saved[RWR_IDS][5],int active[RWR_IDS],unsigned which,int on){
    for(unsigned i=0;i<RWR_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!rwr_addresses[i])continue;if(actor_patch(rwr_addresses[i]-0x400000u,rwr_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(rwr_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static unsigned rwr_which(unsigned index){
    if(index==ES2_HOOK_INDEX_results_writer)return 0x7dfu;     /* 0..4, 6..10 */
    if(index==ES2_HOOK_INDEX_score_sum)return 0x1800u;         /* 11, 12 */
    if(index==ES2_HOOK_INDEX_vehicle_value)return 0xe080u;     /* 7, 13, 14, 15 */
    return 0;
}
static int rwr_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u,head=rwr_fs0();rwr_build(index,n,a,r,0);
        BYTE before[RWR_GLOBALS],after[RWR_GLOBALS],actual[RWR_GLOBALS],expected_fpu[108],actual_fpu[108];static rwr_record expected[RWR_MAX];
        memcpy(rwr_before,rwr_arena,RWR_SIZE);rwr_globals(before,0);
        rwr_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=rwr_calls;memcpy(expected,rwr_log,calls*sizeof(*expected));memcpy(rwr_after,rwr_arena,RWR_SIZE);rwr_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("results_writer poison install");
        memcpy(rwr_arena,rwr_before,RWR_SIZE);rwr_globals(before,1);rwr_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);rwr_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(rwr_fs0()!=head||pair[0]!=pair[1]||calls!=rwr_calls||!rwr_same(expected,rwr_log,calls)||memcmp(rwr_after,rwr_arena,RWR_SIZE)||
           memcmp(after,actual,RWR_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=rwr_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("results_writer poison uninstall");rwr_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state/FS:0\n",d->name,total);
    return status;
}
/* Real-callee passes: every nested hook in rotating original/replaced/
 * compare modes (compare falls back to replaced where refused), the same
 * modes in the expected run (caller original body) and the actual run
 * (caller hook in original mode for mode < 3, replaced otherwise). The
 * writer keeps stubs for objective_eval, the platform calls (45ee00,
 * 47b064, 479c78, the write virtual, 47b09c, 47b2b8) and condition_record
 * 41b4ec; vehicle_value and score_sum run without stubs. Compared: pair,
 * ordered stub calls, arena, globals and FS:0. */
static const unsigned rwr_nested_writer[]={ES2_HOOK_INDEX_score_sum,ES2_HOOK_INDEX_pool_prev,ES2_HOOK_INDEX_pool_next,
    ES2_HOOK_INDEX_vehicle_value,ES2_HOOK_INDEX_part_ratio,ES2_HOOK_INDEX_list_append,ES2_HOOK_INDEX_damage_score,
    ES2_HOOK_INDEX_multiply_q10,ES2_HOOK_INDEX_result_conditions};
static const unsigned rwr_nested_sum[]={ES2_HOOK_INDEX_pool_prev,ES2_HOOK_INDEX_pool_next,ES2_HOOK_INDEX_vehicle_value,
    ES2_HOOK_INDEX_part_ratio,ES2_HOOK_INDEX_list_append,ES2_HOOK_INDEX_damage_score,ES2_HOOK_INDEX_multiply_q10};
static const unsigned rwr_nested_value[]={ES2_HOOK_INDEX_part_ratio,ES2_HOOK_INDEX_list_append,ES2_HOOK_INDEX_damage_score,
    ES2_HOOK_INDEX_multiply_q10};
enum { RWR_NESTED_MAX=9 };
static int rwr_real_pass(unsigned index){
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[RWR_NESTED_MAX]={0};
    const int writer=index==ES2_HOOK_INDEX_results_writer;
    const unsigned *list=writer?rwr_nested_writer:index==ES2_HOOK_INDEX_score_sum?rwr_nested_sum:rwr_nested_value;
    const unsigned nested=writer?9u:index==ES2_HOOK_INDEX_score_sum?7u:4u;
    static rwr_record expected[RWR_MAX];BYTE saved[RWR_IDS][5];int active[RWR_IDS];
    if(writer&&rwr_patch(saved,active,0x1061eu,1))return 1;   /* 1..4, 9, 10, 16 */
    rwr_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u,head=rwr_fs0();rwr_build(index,n,a,r,1);
        BYTE before[RWR_GLOBALS],after[RWR_GLOBALS],actual[RWR_GLOBALS];
        memcpy(rwr_before,rwr_arena,RWR_SIZE);rwr_globals(before,0);
        es2_hook hk[RWR_NESTED_MAX+1];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<nested;++j){
            o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(list[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(list[j]),&o,e))return fail("results_writer nested install");
        }
        rwr_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        unsigned calls=rwr_calls;memcpy(expected,rwr_log,calls*sizeof(*expected));
        memcpy(rwr_after,rwr_arena,RWR_SIZE);rwr_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+nested,d,&o,function_hook_extension(index)))return fail("results_writer caller install");
        memcpy(rwr_arena,rwr_before,RWR_SIZE);rwr_globals(before,1);rwr_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);rwr_globals(actual,0);
        if(rwr_fs0()!=head||expected_pair[0]!=actual_pair[0]||expected_pair[1]!=actual_pair[1]||calls!=rwr_calls||
           !rwr_same(expected,rwr_log,calls)||memcmp(rwr_after,rwr_arena,RWR_SIZE)||memcmp(after,actual,RWR_GLOBALS)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            for(unsigned j=0;j<RWR_GLOBALS;++j)if(after[j]!=actual[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,after[j],actual[j]);break;}
            fprintf(stderr,"real mode=%u head %08x/%08x\n",mode,head,rwr_fs0());status=rwr_report("real",n,pair,calls,expected);
        }
        if(o.mode==ES2_HOOK_REPLACED&&hk[nested].calls!=1){fprintf(stderr,"results_writer real mode=%u case %u caller calls %llu\n",mode,n,(unsigned long long)hk[nested].calls);status=fail("results_writer real caller not entered once");}
        for(unsigned j=0;j<=nested;++j){if(j<nested)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("results_writer nested compare mismatch");
            if(!es2_hook_uninstall(hk+j))return fail("results_writer nested uninstall");}
        rwr_globals(before,1);++total;
    }
    rwr_real=0;
    if(writer&&rwr_patch(saved,active,0,0))return 1;
    for(unsigned j=0;j<nested&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,
        function_hook_descriptor(list[j])->name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, 31/a7 stack, full pair, calls, memory, globals and FS:0\n",
        d->name,total,nested);
    return status;
}
static int results_writer_cases(unsigned index,unsigned limit){
    const unsigned which=rwr_which(index);
    BYTE saved[RWR_IDS][5],old_globals[RWR_GLOBALS];int active[RWR_IDS];
    rwr_globals(old_globals,0);rwr_frame=index==ES2_HOOK_INDEX_results_writer;
    if(rwr_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_RESULTS_WRITER_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];rwr_build(index,n,a,r,0);status=rwr_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=rwr_installed_probes(index,64);
    if(rwr_patch(saved,active,which,0))return 1;
    current_case_valid=0;rwr_frame=0;
    if(!limit&&!status&&index!=ES2_HOOK_INDEX_list_append)status=rwr_real_pass(index);
    rwr_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair and ECX, ordered calls with slots, entry registers, context word and written bytes, arena/globals/FS:0 (%u masked words equal only raw)\n",
        function_hook_descriptor(index)->name,cases,rwr_raw_matches);
    rwr_raw_matches=0;
    return status;
}
