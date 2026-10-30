/* Building update: base_update 403ca8, base_parts 403914 (with the pure 403668
 * run as original code inside), base_part_chain 403890, base_part_blast 4036c0,
 * base_part_debris 40379c (docs/reverse- engineering/vtables.md, "base_update:
 * contract"). Direct cases run the original body and the core against logging
 * stubs for every game callee (ordered calls with their arguments and entry
 * EAX/EDX/ECX, frame payloads by content, stack addresses masked in the fixed
 * window of node_cases.h); the runtime frame setup 49630c is a record stub that
 * keeps the real FS:0 head, as in pools_cases.h. The stubs change what the
 * originals reread (configuration and shape pointers, stage, count, animation
 * slot, the dead byte, the debris word and the debris count word).
 * base_part_chain's recursion runs as original code in the original run and
 * through the bridge in the core run; slot +74h is a stub in the arena vtable
 * that destroys the part it damages (the recursion ends). Then installed 31/a7
 * poison probes and a real-callee pass with the nested hooks in rotating
 * modes. */
typedef struct bs_record { uint32_t id,args[5],regs[3],input[3],eax,edx; uint32_t count; } bs_record;
enum { BS_SIZE=0x1000, BS_OBJECT=0x100, BS_VTABLE=0x320, BS_CONFIG=0x3a0, BS_CONFIG2=0x3d0, BS_QPARTS=0x400,
    BS_PPARTS=0x520, BS_SHAPE=0x580, BS_SHAPE2=0x590, BS_NODE=0x5a0, BS_NODE2=0x5d0, BS_FRAMES=0x600, BS_FRAMES2=0x640,
    BS_COUNTS=0x680, BS_COUNTS2=0x6c0, BS_MODELS=0x700, BS_COUNTER=0x740, BS_FIRE=0x760, BS_EXPLOSION=0x780,
    BS_POOL=0x7a0, BS_NODES=0x800, BS_PARTS=8, BS_MAX=96, BS_IDS=18, BS_GLOBALS=201, BS_TABLES=0x973fc, BS_TABLES_LEN=0x30 };
#define BS_BASE ((uint32_t)(uintptr_t)bs_arena)
static BYTE bs_arena[BS_SIZE],bs_before[BS_SIZE],bs_after[BS_SIZE],bs_shipped[BS_TABLES_LEN];
static bs_record bs_log[BS_MAX];
static unsigned bs_calls;
static int bs_real;
static uint32_t bs_seed,bs_stream,bs_last_part,bs_debris_q,bs_plan[3];
__attribute__((used)) static uint32_t bs_ecx;
/* 49630c (record), 467a48, 403914, 402604, 46b430, 46b48c, 4036c0, 40379c,
 * 403890, 492f24, 47ebb8, 48043c, 471b30, 407f1c, 4801d4, 4089bc, 408530,
 * virtual +74h (arena vtable, never patched). */
static const uint32_t bs_addresses[BS_IDS]={0x49630c,0x467a48,0x403914,0x402604,0x46b430,0x46b48c,0x4036c0,0x40379c,
    0x403890,0x492f24,0x47ebb8,0x48043c,0x471b30,0x407f1c,0x4801d4,0x4089bc,0x408530,0};
static const unsigned bs_slots[BS_IDS]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x74};
static const unsigned bs_counts[BS_IDS]={0,1,1,2,1,5,4,2,2,2,2,3,1,5,3,1,4,4};
static uint32_t bs_next(void){
    uint32_t z=bs_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *bs_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int bs_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static uint32_t bs_small(uint32_t pick,uint32_t range){return (uint32_t)((int32_t)(pick%(2u*range+1u))-(int32_t)range);}
static uint16_t bs_word(const uint16_t *values,unsigned count,uint32_t pick){return values[pick%count];}
/* Animation slots and frame indices stay inside the 32-word arrays;
 * models inside the 16-dword table; -1 for "none". */
static uint16_t bs_slot(uint32_t pick){return (uint16_t)(pick%4==0?0xffffu:pick>>2&31u);}
static uint16_t bs_none_or(uint32_t pick,unsigned range){return (uint16_t)(pick%2?0xffffu:(pick>>1)%range);}
__attribute__((used,noinline)) static uint64_t __cdecl bs_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(bs_calls==BS_MAX){fail("base synthetic call overflow");ExitProcess(1);}
    bs_record *v=bs_log+bs_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,4*bs_counts[id]);memcpy(v->regs,regs,12);
    v->count=weapons_bridge_slots(a,bs_counts[id]);
    for(unsigned j=0;j<3;++j)if(bs_stack(v->regs[j]))v->regs[j]=0;
    for(unsigned j=0;j<bs_counts[id];++j)if(bs_stack(v->args[j]))v->args[j]=0;
    uint32_t eax=bs_next(),edx=bs_next(),pick=bs_next(),b=BS_BASE;bs_ecx=bs_next();
    BYTE *o=bs_arena+BS_OBJECT;
    switch(id){
    case 0: /* 49630c record stub returns the real FS:0 head for bs_stub_0. */
        __asm__ volatile("movl %%fs:0,%0":"=r"(eax));break;
    case 1: /* timer_word_step: AX = 0 half of the time; the configuration,
             * the shape, the animation slot and the part's stage are
             * reread after it. */
        if(pick%2)eax&=0xffff0000u;
        bs_last_part=a[0]-2u;
        if(bs_real)break;
        /* A slot the frame step uses afterwards is never -1 (the original
         * tested the configuration of before the call). */
        if(pick%7==0){uint32_t t=b+(pick>>8&1?BS_CONFIG2:BS_CONFIG);put16(bs_host(t)+0x24,(uint16_t)(pick>>20&31u));put32(o+0x1f2,t);}
        if(pick%11==0)put16(bs_host(get32(o+0x1f2))+0x24,(uint16_t)(pick>>12&31u));
        if(pick%13==0)put32(o+0x34,b+(pick>>10&1?BS_SHAPE2:BS_SHAPE));
        if(pick%5==0&&a[0]-2u-(b+BS_PPARTS)<11u*BS_PARTS)put16(bs_host(a[0]+3u),(uint16_t)(pick>>16&7u));
        break;
    case 2: /* base_parts (under base_update): the dead byte and the
             * configuration are reread. */
        if(pick%4==0)o[0x99]=(BYTE)(o[0x99]?0:pick>>8|1u);
        if(pick%9==0)put32(o+0x1f2,b+(pick>>12&1?BS_CONFIG2:BS_CONFIG));
        break;
    case 4: /* 46b430 FIRE allocation: object or 0; C+8 is reread. */
        eax=pick%3?b+BS_FIRE:0;
        if(!bs_real&&pick%7==0)put16(bs_host(get32(o+0x1f2))+8,(uint16_t)bs_next());
        break;
    case 6: case 7: /* blast, debris: the model index, the frame slot, the
                     * shape and the configuration are reread. */
        if(bs_real)break;
        if(pick%5==0)put16(bs_host(get32(o+0x1f2))+4,bs_none_or(pick>>4,16));
        if(pick%7==0)put32(o+0x34,b+(pick>>10&1?BS_SHAPE2:BS_SHAPE));
        if(pick%11==0)put32(o+0x1f2,b+(pick>>11&1?BS_CONFIG2:BS_CONFIG));
        break;
    case 9: /* rng_bounded: small or full results; ECX feeds the next bound. */
        if(pick%3)eax=pick>>4&0x7fffu;
        break;
    case 10: /* euler_matrix: rewrites the matrix words. */
        if(pick%3)for(unsigned j=0;j<9;++j)put16(bs_host(a[1])+2*j,(uint16_t)bs_next());
        break;
    case 11: /* transform_point: point by content, planned point out. */
        for(unsigned j=0;j<3;++j)v->input[j]=get32(bs_host(a[0])+4*j);
        for(unsigned j=0;j<3;++j)put32(bs_host(a[2])+4*j,bs_plan[j]);
        break;
    case 12: /* pool_alloc: explosion object or 0. */
        eax=pick%3?b+BS_EXPLOSION:0;
        break;
    case 13: /* explosion_create: point by content; the stage is reread. */
        for(unsigned j=0;j<3;++j)v->input[j]=get32(bs_host(a[2])+4*j);
        if(!bs_real&&pick%5==0&&bs_last_part-(b+BS_PPARTS)<11u*BS_PARTS)put16(bs_host(bs_last_part+5u),(uint16_t)(pick>>8&1?4:pick>>9&7));
        break;
    case 14: /* 4801d4: offset words by content, planned point out. */
        for(unsigned j=0;j<3;++j)v->input[j]=get16(bs_host(a[0])+2*j);
        for(unsigned j=0;j<3;++j)put32(bs_host(a[2])+4*j,bs_plan[j]);
        break;
    case 15: /* 4089bc: the word by content, lowered in a third of the calls;
              * one of the two image records. */
        v->input[0]=get16(bs_host(a[0]));
        if(pick%3==0)put16(bs_host(a[0]),(uint16_t)(v->input[0]-(pick>>8&15u)));
        eax=0x400000u+(pick>>4&1?0xa9650u:0xa95c0u);
        break;
    case 16: /* 408530: point by content; the debris word is reread. */
        for(unsigned j=0;j<3;++j)v->input[j]=get32(bs_host(a[2])+4*j);
        if(!bs_real&&pick%7==0&&bs_debris_q)put16(bs_host(bs_debris_q+8u),(uint16_t)(pick>>8&1?bs_small(pick>>9,8):pick>>9));
        break;
    case 17: { /* slot +74h: the damaged part is destroyed (runtime word =
                * its maximum), so the recursion ends; the count may drop
                * or grow up to 8 and another part may be destroyed. */
        uint32_t j=a[1];BYTE *c=bs_host(get32(o+0x1f2));uint32_t q=get32(c+0x14);
        if(j<BS_PARTS){put16(bs_arena+BS_PPARTS+11u*j,get16(bs_host(q+0x1eu*j)));}
        if(bs_real)break;
        if(pick%5==0)put16(c+0x12,(uint16_t)(pick>>8)%(BS_PARTS+1u));
        if(pick%7==0){uint32_t k=pick>>12&7u;put16(bs_arena+BS_PPARTS+11u*k,0x7fff);}
        break; }
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
/* Pushes the entry EAX/EDX/ECX for the record before the slot pointer. */
#define BS_STUB(n) __attribute__((naked)) static void bs_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _bs_stub_impl\n\taddl $24,%esp\n\tmovl _bs_ecx,%ecx\n\tret\n\t");}
BS_STUB(1) BS_STUB(2) BS_STUB(3) BS_STUB(4) BS_STUB(5) BS_STUB(6) BS_STUB(7) BS_STUB(8) BS_STUB(9) BS_STUB(10) BS_STUB(11)
BS_STUB(12) BS_STUB(13) BS_STUB(14) BS_STUB(15) BS_STUB(16) BS_STUB(17)
#undef BS_STUB
/* 49630c: captures the entry registers like the other stubs, then links a
 * record at EBP + [descriptor+4] into FS:0 with the saved head at record+0
 * and a zero context word at record+16, as the original does. */
__attribute__((naked)) static void bs_stub_0(void){__asm__ volatile(
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
    "pushl $0\n\tcalll _bs_stub_impl\n\taddl $12,%esp\n\t"
    "popl %ecx\n\tmovl 4(%ecx),%ecx\n\taddl %ebp,%ecx\n\tmovl %eax,(%ecx)\n\tmovw $0,16(%ecx)\n\tmovl %ecx,%fs:0\n\t"
    "popl %edx\n\taddl $4,%esp\n\tmovl _bs_ecx,%ecx\n\tret\n\t");}
static void *bs_stubs[BS_IDS]={bs_stub_0,bs_stub_1,bs_stub_2,bs_stub_3,bs_stub_4,bs_stub_5,bs_stub_6,bs_stub_7,bs_stub_8,bs_stub_9,
    bs_stub_10,bs_stub_11,bs_stub_12,bs_stub_13,bs_stub_14,bs_stub_15,bs_stub_16,bs_stub_17};
/* Direct callees of each hook (bit i = bs_addresses[i]; the slot is in the
 * arena vtable and never patched). */
static unsigned bs_which(unsigned index){
    if(index==ES2_HOOK_INDEX_base_update)return 1u<<1|1u<<2|1u<<3;
    if(index==ES2_HOOK_INDEX_base_parts)return 1u|1u<<1|1u<<4|1u<<5|1u<<6|1u<<7|1u<<8|1u<<9|1u<<10|1u<<11|1u<<12|1u<<13;
    if(index==ES2_HOOK_INDEX_base_part_chain)return 0;
    if(index==ES2_HOOK_INDEX_base_part_blast)return 1u|1u<<10|1u<<12|1u<<13|1u<<14;
    return 1u<<10|1u<<14|1u<<15|1u<<16;
}
/* Counter pointer, explosion pool, detail byte, model table, the two debris
 * records (with the pointer 4a964c and the count word 4a9654), frame time,
 * RNG stream A and the kind tables 4973fc..49742b (blast type, at-position
 * flag, re-arm time, puff type). */
static const struct { uint32_t rva,size; } bs_ranges[]={{0x2cbee4,4},{0xa961e,4},{0xd1f43,1},{0xa9584,4},{0xa95c0,10},
    {0xa964c,14},{0xd3b64,2},{0xd2599,0x72},{BS_TABLES,BS_TABLES_LEN}};
static void bs_globals(BYTE *out,int restore){
    for(unsigned i=0;i<sizeof(bs_ranges)/sizeof(bs_ranges[0]);++i){
        if(restore)memcpy(mapped+bs_ranges[i].rva,out,bs_ranges[i].size);else memcpy(out,mapped+bs_ranges[i].rva,bs_ranges[i].size);
        out+=bs_ranges[i].size;
    }
}
static int bs_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<BS_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
static void bs_reset(void){bs_calls=0;bs_stream=bs_seed;}
/* Pool header (free count +0, used count +2, free head +0Ah, used head
 * +0Eh) with count free nodes of 80h bytes; objects start at node+8. */
static void bs_pool(uint32_t pool,uint32_t nodes,unsigned count){
    put16(bs_host(pool),(uint16_t)count);put16(bs_host(pool)+2,0);put32(bs_host(pool)+0xe,0);
    for(unsigned i=0;i<count;++i){uint32_t n=nodes+0x80u*i;put32(bs_host(n),i+1<count?n+0x80u:0);put32(bs_host(n)+4,i?n-0x80u:0);}
    put32(bs_host(pool)+0xa,count?nodes:0);
}
static void bs_config(BYTE *c,uint32_t q,int real,unsigned n){
    uint32_t b=BS_BASE;
    static const uint16_t counts[]={0,1,1,1,2,3,4,5,8,0xffff,0x8000};
    put32(c+0x14,q);
    put16(c+0x12,real?(uint16_t)(1+random32()%BS_PARTS):bs_word(counts,11,n<1024?n:random32()));
    put16(c+4,bs_none_or(random32(),16));put16(c+8,random32()%2?0xffff:(uint16_t)(random32()%4?random32()%64:random32()));
    put16(c+0x10,(uint16_t)(random32()%4?random32()%6:random32()));put16(c+0x24,bs_slot(random32()));
    put16(c+6,(uint16_t)(random32()%2?0:random32()));
    (void)b;
}
/* Part records: the stage word mostly 0..6 (1, 2 and 5 reach stages 0, 1
 * and 4), the timer word small, damage and maximum close to each other. */
static void bs_parts(int real){
    static const uint16_t stages[]={0,1,2,3,4,5,6,1,2,5,0x8000,0xffff};
    for(unsigned i=0;i<BS_PARTS+1u;++i){
        BYTE *q=bs_arena+BS_QPARTS+0x1eu*i;
        uint16_t max=(uint16_t)(random32()%4?random32()%8:random32());put16(q,max);
        put16(q+2,bs_none_or(random32(),32));put16(q+4,(uint16_t)(random32()%3==0?0xffff:random32()%4?random32()%6:random32()));
        /* +6: also words whose two bytes differ in sign (0080h..00FFh,
         * FF00h..FF7Fh), so a word read one byte off changes 403668's test. */
        put16(q+6,random32()%2?0xffff:(uint16_t)(random32()%4?random32()%64:random32()%2?0x80u+random32()%0x80u:0xff00u+random32()%0x80u));
        put16(q+8,(uint16_t)(random32()%4?3+random32()%5:random32()));
        for(unsigned k=0;k<3;++k){
            put16(q+0x10+2*k,(uint16_t)(random32()%2?bs_small(random32(),3000):random32()));
            uint16_t range=(uint16_t)(random32()%4?1+random32()%2000:random32());
            if(real&&(range==0||range==0x8000))range=7;put16(q+0x16+2*k,range);
        }
        put16(q+0x1c,(uint16_t)(random32()%4?random32()%(BS_PARTS+1u)-1u:random32()));
        if(i<BS_PARTS){
            BYTE *p=bs_arena+BS_PPARTS+11u*i;
            put16(p,(uint16_t)(random32()%2?max:max+bs_small(random32(),2)));
            put16(p+5,stages[random32()%12]);
            put16(p+3,(uint16_t)(real?random32()%40:random32()));
        }
    }
}
static void bs_shape(BYTE *m,uint32_t node,uint32_t frames,uint32_t counts,int real){
    put32(m+4,node);put32(m+8,frames);put32(bs_host(node)+0x20,counts);
    for(unsigned i=0;i<32;++i){
        uint16_t c=(uint16_t)(random32()%2?1+random32()%9:1+random32()%0xffff);if(real&&c>16)c=(uint16_t)(1+c%9);
        put16(bs_host(counts)+2*i,c);
        put16(bs_host(frames)+2*i,(uint16_t)(random32()%2?c-1u:random32()));
    }
}
static void bs_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    uint32_t b=BS_BASE;BYTE *o=bs_arena+BS_OBJECT;
    bs_seed=random32();random_bytes(bs_arena,BS_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    for(unsigned j=0;j<3;++j)bs_plan[j]=random32();
    put32(o,b+BS_VTABLE);put32(bs_arena+BS_VTABLE+0x74,(uint32_t)(uintptr_t)bs_stubs[17]);
    put32(o+0x1f2,b+BS_CONFIG);put32(o+0x205,b+BS_PPARTS);put32(o+0x34,b+BS_SHAPE);
    o[0x99]=(BYTE)(random32()%4?0:1+random32()%255);
    put16(o+0x32,(uint16_t)(random32()%2?0:random32()%3?1:random32()));
    if(real){o[0x39]=0;o[0x1f6]=(BYTE)random32();put16(o+0x1f7,(uint16_t)(random32()%40));
        for(unsigned j=0;j<3;++j)put32(o+0x26+4*j,random32()%65536u-32768u);}
    bs_config(bs_arena+BS_CONFIG,b+BS_QPARTS,real,n);bs_config(bs_arena+BS_CONFIG2,b+BS_QPARTS+0x1e,real,n>>4);
    bs_parts(real);
    bs_shape(bs_arena+BS_SHAPE,b+BS_NODE,b+BS_FRAMES,b+BS_COUNTS,real);
    bs_shape(bs_arena+BS_SHAPE2,b+BS_NODE2,b+BS_FRAMES2,b+BS_COUNTS2,real);
    /* Globals: counter, explosion pool (real pool in the real pass), detail
     * byte 0/1/2/other, model table, debris records, frame time, RNG. */
    put32(mapped+0x2cbee4,b+BS_COUNTER);put32(mapped+0xa961e,b+BS_POOL);put32(mapped+0xa9584,b+BS_MODELS);
    /* Destroyed models are shape nodes (base_update reads [M+4]+20h). */
    for(unsigned i=0;i<16;++i)put32(bs_arena+BS_MODELS+4*i,b+(random32()%2?BS_NODE:BS_NODE2));
    static const BYTE details[]={0,1,2,2,1,0,3,0xff};
    mapped[0xd1f43]=details[random32()%8];
    random_bytes(mapped+0xa95c0,10);random_bytes(mapped+0xa964c,14);
    if(random32()%4==0)random_bytes(mapped+BS_TABLES,BS_TABLES_LEN);else memcpy(mapped+BS_TABLES,bs_shipped,BS_TABLES_LEN);
    if(real){
        put16(mapped+0xd3b64,(uint16_t)(1+random32()%40));
        BYTE *state=mapped+0xd2599;random_bytes(state,112);state[112]=(BYTE)(random32()%56);state[113]=(BYTE)(random32()%56);
        bs_pool(b+BS_POOL,b+BS_NODES,random32()%3);
        put16(mapped+0xa9654,(uint16_t)(random32()%2?random32()%8:random32()));
    }
    bs_debris_q=0;a[0]=b+BS_OBJECT;
    /* k: a part, -1 (the "no parent" word) or a value no sign-extended
     * parent word can equal. */
    if(index==ES2_HOOK_INDEX_base_part_chain)a[1]=random32()%4?random32()%BS_PARTS:random32()%2?0xffffffffu:(random32()&0x7fffffffu)|0x10000u;
    else if(index==ES2_HOOK_INDEX_base_part_blast){
        uint32_t i=random32()%BS_PARTS;a[1]=b+BS_PPARTS+11u*i;
        a[2]=random32()%4?random32()%4:random32()%2?(uint32_t)(int32_t)(int16_t)random32():random32()%8;
        a[3]=random32()%2?b+BS_CONFIG+0xa:b+BS_QPARTS+0x1eu*i+0xa;bs_last_part=a[1];
    } else if(index==ES2_HOOK_INDEX_base_part_debris){
        uint32_t i=random32()%BS_PARTS;a[1]=b+BS_QPARTS+0x1eu*i;bs_debris_q=a[1];
        static const uint16_t words[]={0xffff,0x8000,0,4,5,6,7,0x7fff};
        put16(bs_host(a[1])+8,n<1024?words[n%8]:bs_word(words,8,random32()));
    }
}
static int bs_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const bs_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,bs_calls);
    for(unsigned j=0;j<calls&&j<bs_calls&&expected;++j)if(memcmp(expected+j,bs_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(bs_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<BS_SIZE;++j)if(bs_after[j]!=bs_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,bs_after[j],bs_arena[j]);break;}
    return 1;
}
static unsigned bs_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<bs_calls;++j){unsigned id=bs_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?bs_slots[id]==c->key:!bs_slots[id]&&bs_addresses[id]==c->key)++hits;}
    return hits;
}
static int bs_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[BS_GLOBALS],after[BS_GLOBALS],actual[BS_GLOBALS];static bs_record expected[BS_MAX];uint32_t head,head_after;
    memcpy(bs_before,bs_arena,BS_SIZE);bs_globals(before,0);
    __asm__ volatile("movl %%fs:0,%0":"=r"(head));
    uint32_t last=bs_last_part;
    bs_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=bs_calls;memcpy(expected,bs_log,calls*sizeof(*expected));
    memcpy(bs_after,bs_arena,BS_SIZE);bs_globals(after,0);
    memcpy(bs_arena,bs_before,BS_SIZE);bs_globals(before,1);bs_reset();bs_last_part=last;
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];for(unsigned i=0;i<count&&i<16;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);bs_globals(actual,0);
    __asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
    if(head_after!=head)return fail("base left FS:0 changed");
    /* The chain's log also holds the recursion's slot calls (original code
     * below the first level in the core run); its counters see the first
     * level only: one slot call and one recursive call per dependent. */
    if(n<4096&&index==ES2_HOOK_INDEX_base_part_chain){
        if(c[0].count-counts[0]!=c[1].count-counts[1]||c[0].count-counts[0]>bs_expected_hits(c))return fail("base chain counter delta");
    } else if(n<4096)for(unsigned i=0;i<count&&i<16;++i)if(c[i].count-counts[i]!=bs_expected_hits(c+i))return fail("base callee counter delta");
    if(pair[0]!=pair[1]||calls!=bs_calls||memcmp(expected,bs_log,calls*sizeof(*expected))||memcmp(bs_after,bs_arena,BS_SIZE)||
       bs_globals_differ(after,actual))
        return bs_report("direct",n,pair,calls,expected);
    bs_globals(before,1);
    return 0;
}
static int bs_patch(BYTE saved[BS_IDS][5],int active[BS_IDS],unsigned which,int on){
    for(unsigned i=0;i<BS_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!bs_addresses[i])continue;if(actor_patch(bs_addresses[i]-0x400000u,bs_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(bs_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static int bs_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;uint32_t head,head_after;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;bs_build(index,n,a,r,0);
        BYTE before[BS_GLOBALS],after[BS_GLOBALS],actual[BS_GLOBALS],expected_fpu[108],actual_fpu[108];static bs_record expected[BS_MAX];
        memcpy(bs_before,bs_arena,BS_SIZE);bs_globals(before,0);uint32_t last=bs_last_part;
        __asm__ volatile("movl %%fs:0,%0":"=r"(head));
        bs_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=bs_calls;memcpy(expected,bs_log,calls*sizeof(*expected));memcpy(bs_after,bs_arena,BS_SIZE);bs_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("base poison install");
        memcpy(bs_arena,bs_before,BS_SIZE);bs_globals(before,1);bs_reset();bs_last_part=last;
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);bs_globals(actual,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(head_after!=head||pair[0]!=pair[1]||calls!=bs_calls||memcmp(expected,bs_log,calls*sizeof(*expected))||memcmp(bs_after,bs_arena,BS_SIZE)||
           bs_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||
           h.calls!=1u+(index==ES2_HOOK_INDEX_base_part_chain?bs_calls:0u))
            status=bs_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("base poison uninstall");bs_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state/FS:0%s\n",d->name,total,
        index==ES2_HOOK_INDEX_base_part_chain?", recursion through the hook":"");
    return status;
}
/* Real callees with the nested hooks in rotating original/replaced/compare
 * modes (compare falls back to replaced where refused). The runtime frame
 * setup, the FIRE creation, explosion_create and the debris spawn stay
 * stubs (keep masks below); slot +74h stays the arena stub; 4801d4 and
 * 4089bc run as original code; actor_animate runs real with its gate byte
 * B+39h clear. Pair, arena, globals and FS:0 are compared against a run
 * with only the nested hooks. */
static unsigned bs_nested(unsigned index,unsigned nested[12],unsigned *keep){
    unsigned count=0;const unsigned stubs=1u|1u<<4|1u<<5|1u<<13|1u<<16;
    if(index==ES2_HOOK_INDEX_base_update||index==ES2_HOOK_INDEX_base_parts){
        if(index==ES2_HOOK_INDEX_base_update){nested[count++]=ES2_HOOK_INDEX_base_parts;nested[count++]=ES2_HOOK_INDEX_actor_animate;}
        nested[count++]=ES2_HOOK_INDEX_base_part_chain;nested[count++]=ES2_HOOK_INDEX_base_part_blast;
        nested[count++]=ES2_HOOK_INDEX_base_part_debris;nested[count++]=ES2_HOOK_INDEX_timer_word_step;
        nested[count++]=ES2_HOOK_INDEX_rng_bounded;nested[count++]=ES2_HOOK_INDEX_euler_matrix;
        nested[count++]=ES2_HOOK_INDEX_transform_point;nested[count++]=ES2_HOOK_INDEX_pool_alloc;*keep=stubs;}
    else if(index==ES2_HOOK_INDEX_base_part_chain)*keep=0;
    else if(index==ES2_HOOK_INDEX_base_part_blast){
        nested[count++]=ES2_HOOK_INDEX_euler_matrix;nested[count++]=ES2_HOOK_INDEX_pool_alloc;*keep=1u|1u<<13;}
    else {nested[count++]=ES2_HOOK_INDEX_euler_matrix;*keep=1u<<16;}
    return count;
}
static int bs_real_pass(unsigned index){
    unsigned nested[12],keep,count=bs_nested(index,nested,&keep);
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[12]={0};
    BYTE saved[BS_IDS][5];int active[BS_IDS];
    if(bs_patch(saved,active,keep,1))return 1;
    bs_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u,head,head_after;bs_build(index,n,a,r,1);
        BYTE before[BS_GLOBALS],after[BS_GLOBALS],actual[BS_GLOBALS];
        memcpy(bs_before,bs_arena,BS_SIZE);bs_globals(before,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head));
        es2_hook hk[13];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!nested_install_hook(hk+j,nested[j],&o,e))return fail("base nested install");}
        bs_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        memcpy(bs_after,bs_arena,BS_SIZE);bs_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+12,d,&o,function_hook_extension(index)))return fail("base caller install");
        memcpy(bs_arena,bs_before,BS_SIZE);bs_globals(before,1);bs_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);bs_globals(actual,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
        if(head_after!=head||expected_pair[0]!=actual_pair[0]||expected_pair[1]!=actual_pair[1]||memcmp(bs_after,bs_arena,BS_SIZE)||
           bs_globals_differ(after,actual)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            fprintf(stderr,"real mode=%u head %08x/%08x\n",mode,head,head_after);bs_calls=0;status=bs_report("real",n,pair,0,NULL);
        }
        for(unsigned j=0;j<13;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("base nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("base nested uninstall");}
        bs_globals(before,1);++total;
    }
    bs_real=0;
    if(bs_patch(saved,active,0,0))return 1;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, 31/a7 stack, full pair, memory and FS:0\n",d->name,total,count);
    return status;
}
static int base_cases(unsigned index,unsigned limit){
    unsigned which=bs_which(index);
    BYTE saved[BS_IDS][5],old_globals[BS_GLOBALS];int active[BS_IDS];
    bs_globals(old_globals,0);memcpy(bs_shipped,mapped+BS_TABLES,BS_TABLES_LEN);
    if(bs_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_BASE_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];bs_build(index,n,a,r,0);status=bs_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=bs_installed_probes(index,64);
    if(bs_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=bs_real_pass(index);
    bs_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, frame payloads by content, arena/globals/FS:0\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
