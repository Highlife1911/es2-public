/* effect_pools group: explosion_pools 407b54, debris_pools 408860, debris_set
 * 40874c, debris_record 4083f8, flat_pools 4097a8, meteor_pools 409a34,
 * fire_pools 46b1a8 and smoke_balls 408fb8
 * (docs/reverse-engineering/mission_load.md, "Effect pools with resources").
 *
 * Direct cases run the original body (six argument slots, EAX/EDX/ECX at
 * entry from the case, over a stack filled with a case pattern), the
 * adapter (with the callee counter deltas) and the core (with the ECX it
 * reports) against logging stubs for every direct game callee of the hook
 * under test: 49630c (a record stub: the real FS:0 head at record+0,
 * context word +10h zeroed, the record linked into FS:0), the path builder
 * 492bec, the stream constructor 472f14 (the stream's table, a state that is
 * nonzero one time in four), the error report 479c78, stream_read 4735b8 and
 * two arena tables whose slot +18h is a read stub (count dwords for the
 * 4-byte reads, count words for the 2-byte reads, bounded data otherwise,
 * sometimes a switch of the stream's table), the array construction 49667c
 * and new[] 4774f0 (arena blocks; the count last read may change),
 * stream_destroy 472f64, the shape loader 474cd8 (stores one of two arena
 * shape lists, returns a count over a random high word), the bitmap load
 * 45cdf4, its descriptor 46a03c (may change the flat bitmap slot, which the
 * release rereads), the bitmap release 48349c (may change the flat shape
 * count), the debris set 40874c and the debris record 4083f8 (may lower the
 * set's record count, which the loop rereads), new 47749c (0 one time in
 * four), pool_create 471ad8 (writes the header) and the RTL thunks 4967ea
 * and 496820. Frame addresses (the stream objects, the name buffers, the
 * count words) are logged relative to the hook's record (FS:0), so the
 * original's stack frame and the core's scratch frame compare equal.
 * Compared: the full return pair, ECX, FS:0, the ordered calls with their
 * argument slots (and the bridge's slot count), entry EAX/EDX/ECX and the
 * record's context word, the arena and the image globals the group reads
 * or writes (the directory pointers, the low-memory word 4d2680 (-l), the fire base
 * name 4a0a74 with its terminator in one of its five bytes, the guard byte
 * 498098, the effect globals 4a961e..4a96bb and 6b4f28..6b4f3b, the
 * construction counter pointer 6cbee4; randomized per case, every random
 * register value and pointer outside the thread's stack reservation).
 *
 * Then installed 31/a7 poison probes and a real-callee pass: the real
 * 49630c and 49667c (with its element constructor 46bbc1), a bump
 * allocator for new and new[], the other callees stubbed; pool_create
 * nested under the pool hooks, debris_set and debris_record nested under
 * debris_pools and debris_record under debris_set, in rotating
 * original/replaced modes, the hook under test original or replaced
 * against a run without it. Identifiers use the prefix epc_/EPC_. */
typedef struct epc_record { uint32_t id,slots,args[6],regs[3],context,eax,edx; } epc_record;
enum {
    EPC_SIZE=0x8000, EPC_MAX=2048, EPC_IDS=20,
    EPC_VT1=0x40, EPC_VT2=0x80, EPC_COUNTER=0xc0, EPC_COUNTER2=0xd0, EPC_SHAPES=0x100, EPC_SHAPES2=0x180,
    EPC_OBJECTS=0x200, EPC_SET=0x600, EPC_RECORD=0x620, EPC_STREAM=0x640, EPC_BUMP=0x700, EPC_FIXED=0x7800,
    EPC_GLOBALS=202   /* the sizes of epc_ranges: 4 + 4 + 4 + 2 + 5 + 1 + 9Eh + 14h + 4 */
};
#define EPC_B ((uint32_t)(uintptr_t)epc_arena)
#define EPC_A(o) (EPC_B+(uint32_t)(o))
static BYTE epc_arena[EPC_SIZE],epc_before[EPC_SIZE],epc_after[EPC_SIZE];
static epc_record epc_log[EPC_MAX];
static unsigned epc_calls,epc_real,epc_index,epc_paths[EPC_IDS],epc_last_size;
static uint32_t epc_seed,epc_stream,epc_bump,epc_head,epc_last,epc_set;
__attribute__((used)) static uint32_t epc_ecx;
/* 0 49630c, 1 492bec, 2 472f14, 3 479c78, 4 stream_read 4735b8, 5 49667c,
 * 6 new[] 4774f0, 7 stream_destroy 472f64, 8 474cd8, 9 45cdf4, 10 46a03c,
 * 11 48349c, 12 debris_set 40874c, 13 new 47749c, 14 pool_create 471ad8,
 * 15 debris_record 4083f8, 16 4967ea, 17 496820, 18/19 the read method of
 * the two arena tables (slot +18h); the slots each original call site
 * pushes. */
static const uint32_t epc_addresses[EPC_IDS]={0x49630c,0x492bec,0x472f14,0x479c78,0x4735b8,0x49667c,0x4774f0,0x472f64,
    0x474cd8,0x45cdf4,0x46a03c,0x48349c,0x40874c,0x47749c,0x471ad8,0x4083f8,0x4967ea,0x496820,0,0};
static const unsigned epc_counts[EPC_IDS]={0,2,2,6,3,5,1,2,4,1,1,2,3,1,3,2,2,3,3,3};
/* The image ranges the group reads or writes (rva, size). */
static const struct { uint32_t rva,size; } epc_ranges[]={{0xa09f0,4},{0xa09f8,4},{0xa0a10,4},{0xd2680,2},{0xa0a74,5},
    {0x98098,1},{0xa961e,0x9e},{0x2b4f28,0x14},{0x2cbee4,4}};
static uint32_t epc_next(void){
    uint32_t z=epc_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *epc_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
/* A random dword outside the thread's 16 MiB stack reservation (bit 31
 * flipped), so that no random value can fall into the frame window of
 * epc_rel in one run and not in the other. */
static uint32_t epc_value(void){
    uint32_t base;__asm__ volatile("movl %%fs:4,%0":"=r"(base));
    const uint32_t v=epc_next();return v-(base-0x1000000u)<0x1000000u?v^0x80000000u:v;
}
static uint32_t epc_fs0(void){uint32_t v;__asm__ volatile("movl %%fs:0,%0":"=r"(v));return v;}
static void epc_reset(void){epc_calls=0;epc_stream=epc_seed;epc_bump=EPC_A(EPC_BUMP);epc_head=epc_fs0();epc_last=0;epc_last_size=0;}
static int epc_in(uint32_t a,uint32_t size){return a-EPC_B<EPC_SIZE&&size<=EPC_SIZE&&a-EPC_B+size<=EPC_SIZE;}
/* The frame window relative to the current record (FS:0): from the
 * lowest local to the frame base, with 4 bytes of slack on each side.
 * debris_pools passes no frame address; under it the window is
 * debris_set's (its nested record in the real pass). */
static uint32_t epc_window_low(void){
    if(epc_index==ES2_HOOK_INDEX_explosion_pools)return 0x18u;
    if(epc_index==ES2_HOOK_INDEX_fire_pools)return 0x20u;
    return 4u;
}
static uint32_t epc_window(void){
    if(epc_index==ES2_HOOK_INDEX_explosion_pools)return 0x74u;
    if(epc_index==ES2_HOOK_INDEX_fire_pools)return 0x7cu;
    if(epc_index==ES2_HOOK_INDEX_debris_set||epc_index==ES2_HOOK_INDEX_debris_pools)return 0x58u;
    return 0x30u;
}
static int epc_frame(uint32_t a,uint32_t size){
    const uint32_t fs=epc_fs0(),w=epc_window(),o=a-fs+epc_window_low();
    return fs!=epc_head&&o<w&&size<=w&&o+size<=w;
}
static int epc_writable(uint32_t a,uint32_t size){
    if(epc_in(a,size)||epc_frame(a,size))return 1;
    for(unsigned i=0;i<sizeof(epc_ranges)/sizeof(epc_ranges[0]);++i){
        const uint32_t base=(uint32_t)(uintptr_t)(mapped+epc_ranges[i].rva);
        if(a-base<epc_ranges[i].size&&size<=epc_ranges[i].size&&a-base+size<=epc_ranges[i].size)return 1;
    }
    return 0;
}
/* Values inside the hook's own frame as offsets from its record; the head
 * the current record saved (an outer record: debris_pools' in its real
 * pass, on the stack in one run and in the adapter's scratch in the other)
 * as a mark. */
static uint32_t epc_rel(uint32_t v){
    const uint32_t fs=epc_fs0(),o=v-fs+epc_window_low();
    if(fs!=epc_head&&o<epc_window())return 0xf0000000u|o;
    if(epc_real&&fs!=epc_head&&v!=epc_head&&v==get32(epc_host(fs)))return 0xe0000000u;
    return v;
}
/* A count: 0, 1..5 or a small negative (FFFFh included); 1..3 in the real
 * pass. */
static uint32_t epc_count(uint32_t pick){
    const unsigned k=(pick>>12)%8;
    if(k==0)return 0;
    if(epc_real)return 1u+(pick>>16)%3;
    if(k==1)return 0u-1u-(pick>>16)%3;
    return 1u+(pick>>16)%5;
}
/* A block in the bump area with 40h bytes of slack; large or negative
 * sizes get 200h bytes (the stub reads skip sizes above 200h), the real
 * pass's pool blocks up to 5000h unclipped; the fixed block at the arena's
 * end only when the bump area is exhausted. */
static uint32_t epc_alloc(uint64_t size){
    if(size>0x200u&&!(epc_real&&size<=0x5000u))size=0x200u;
    const uint32_t p=epc_bump;epc_bump+=((uint32_t)size+0x43u)&~3u;
    if(epc_bump+0x200u>EPC_A(EPC_FIXED)){epc_bump=EPC_A(EPC_FIXED);return EPC_A(EPC_FIXED);}
    return p;
}
static void epc_read(const uint32_t *a,uint32_t pick){
    const uint32_t size=a[1],buf=a[2];
    if(!epc_real&&pick%9==0&&epc_writable(a[0],4))put32(epc_host(a[0]),EPC_A(pick&0x10?EPC_VT1:EPC_VT2));
    if((size==2||size==4)&&epc_writable(buf,size)){
        const uint32_t v=epc_count(pick);
        if(size==2)put16(epc_host(buf),(uint16_t)v);else put32(epc_host(buf),v);
        epc_last=buf;epc_last_size=size;return;
    }
    if(size>0x200u||!epc_writable(buf,size))return;
    for(uint32_t i=0;i<size;++i)epc_host(buf)[i]=(BYTE)epc_next();
}
static void epc_scribble(uint32_t pick){
    if(epc_real||!epc_last||pick%6!=1)return;
    const uint32_t v=epc_count(pick>>8);
    if(epc_last_size==2)put16(epc_host(epc_last),(uint16_t)v);else put32(epc_host(epc_last),v);
}
/* What the stubs answer and change. */
static uint32_t epc_answer(unsigned id,const uint32_t *a,uint32_t eax,uint32_t pick){
    switch(id){
    case 2: /* 472f14: the stream's table and its state (+4) */
        if(epc_writable(a[0],8)){put32(epc_host(a[0]),EPC_A(pick&1?EPC_VT2:EPC_VT1));put32(epc_host(a[0]+4),pick%4==0?(pick>>8)|1:0);}
        return a[0];
    case 4: case 18: case 19:
        epc_read(a,pick);return eax;
    case 5: /* 49667c: a block; the count may change */
        epc_scribble(pick);
        return epc_alloc((uint64_t)a[1]*a[2]);
    case 6: /* new[]: a block; the count may change */
        epc_scribble(pick);
        return epc_alloc(a[0]);
    case 8: /* 474cd8: one of the shape lists and a count */
        if(epc_writable(a[0],4))put32(epc_host(a[0]),EPC_A(pick&1?EPC_SHAPES:EPC_SHAPES2));
        return (epc_value()&0xffff0000u)|(uint16_t)epc_count(pick);
    case 10: /* 46a03c: the flat bitmap slot may change (the release rereads it) */
        if(!epc_real&&pick%3==0)put32(mapped+0xa969e,epc_value());
        return eax;
    case 11: /* 48349c: the flat shape count may change (the loop rereads it) */
        if(!epc_real&&pick%3==0)put16(mapped+0xa969c,(uint16_t)epc_count(pick>>4));
        return eax;
    case 13: /* new: 0 one time in four outside the real pass */
        if(!epc_real&&pick%4==0)return 0;
        return epc_alloc(a[0]);
    case 14: /* pool_create: writes into the header */
        if(epc_writable(a[0],10)){put16(epc_host(a[0]),(uint16_t)a[1]);put16(epc_host(a[0]+4),(uint16_t)a[2]);put32(epc_host(a[0]+6),epc_next());}
        return a[0];
    case 15: /* debris_record: the set's record count may drop */
        if(!epc_real&&pick%5==0&&epc_set&&(int16_t)get16(epc_host(epc_set+4))>0)
            put16(epc_host(epc_set+4),(uint16_t)((pick>>8)%(unsigned)(int16_t)get16(epc_host(epc_set+4))));
        return eax;
    }
    return eax;
}
__attribute__((used,noinline)) static uint64_t __cdecl epc_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(epc_calls==EPC_MAX){fail("effect_pools call overflow");ExitProcess(1);}
    epc_record *v=epc_log+epc_calls++;memset(v,0,sizeof(*v));v->id=id;
    v->slots=weapons_bridge_slots(a,epc_counts[id]);
    for(unsigned k=0;k<epc_counts[id];++k)v->args[k]=epc_rel(a[k]);
    for(unsigned j=0;j<3;++j)v->regs[j]=epc_rel(regs[j]);
    if(id&&epc_fs0()!=epc_head)v->context=get16(epc_host(epc_fs0()+16u));
    uint32_t eax=epc_value(),edx=epc_value(),pick=epc_next();epc_ecx=epc_value();
    if(id==0)eax=epc_fs0(); /* the record stub returns the real FS:0 head */
    else eax=epc_answer(id,a,eax,pick);
    v->eax=epc_rel(eax);v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define EPC_STUB(n) __attribute__((naked)) static void epc_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _epc_stub_impl\n\taddl $24,%esp\n\tmovl _epc_ecx,%ecx\n\tret\n\t");}
EPC_STUB(1) EPC_STUB(2) EPC_STUB(3) EPC_STUB(4) EPC_STUB(5) EPC_STUB(6) EPC_STUB(7) EPC_STUB(8) EPC_STUB(9)
EPC_STUB(10) EPC_STUB(11) EPC_STUB(12) EPC_STUB(13) EPC_STUB(14) EPC_STUB(15) EPC_STUB(16) EPC_STUB(17) EPC_STUB(18)
EPC_STUB(19)
#undef EPC_STUB
/* 49630c: logs like the other stubs (EDX and ECX come back unchanged, as
 * the original's do), then links a record at EBP + [descriptor+4] into
 * FS:0 with the saved head at record+0 and a zero context word. */
__attribute__((naked)) static void epc_stub_0(void){__asm__ volatile(
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
    "pushl $0\n\tcalll _epc_stub_impl\n\taddl $12,%esp\n\t"
    "popl %ecx\n\tmovl 4(%ecx),%ecx\n\taddl %ebp,%ecx\n\tmovl %eax,(%ecx)\n\tmovw $0,16(%ecx)\n\tmovl %ecx,%fs:0\n\t"
    "popl %edx\n\tpopl %ecx\n\tret\n\t");}
static void *epc_stubs[EPC_IDS]={epc_stub_0,epc_stub_1,epc_stub_2,epc_stub_3,epc_stub_4,epc_stub_5,epc_stub_6,epc_stub_7,
    epc_stub_8,epc_stub_9,epc_stub_10,epc_stub_11,epc_stub_12,epc_stub_13,epc_stub_14,epc_stub_15,epc_stub_16,epc_stub_17,
    epc_stub_18,epc_stub_19};
static int epc_patch(BYTE saved[EPC_IDS][5],int active[EPC_IDS],uint32_t which,int on){
    for(unsigned i=0;i<EPC_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!epc_addresses[i])continue;if(actor_patch(epc_addresses[i]-0x400000u,epc_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(epc_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
/* Direct callees of each hook (bit i = epc_addresses[i]; the read method is
 * reached through the arena tables). */
static uint32_t epc_which(unsigned index){
    if(index==ES2_HOOK_INDEX_explosion_pools)return 0x363ffu;   /* 0..9, 13, 14, 16, 17 */
    if(index==ES2_HOOK_INDEX_debris_pools)return 0x7041u;       /* 0, 6, 12, 13, 14 */
    if(index==ES2_HOOK_INDEX_debris_set)return 0x81cfu;         /* 0..3, 6..8, 15 */
    if(index==ES2_HOOK_INDEX_debris_record)return 0x40u;        /* 6 */
    if(index==ES2_HOOK_INDEX_flat_pools)return 0x6f43u;         /* 0, 1, 6, 8..11, 13, 14 */
    if(index==ES2_HOOK_INDEX_fire_pools)return 0x363efu;        /* 0..3, 5..9, 13, 14, 16, 17 */
    return 0x6343u;                                             /* meteor, smoke: 0, 1, 6, 8, 9, 13, 14 */
}
static void epc_globals(BYTE *out,int restore){
    for(unsigned i=0;i<sizeof(epc_ranges)/sizeof(epc_ranges[0]);++i){
        if(restore)memcpy(mapped+epc_ranges[i].rva,out,epc_ranges[i].size);else memcpy(out,mapped+epc_ranges[i].rva,epc_ranges[i].size);
        out+=epc_ranges[i].size;
    }
}
static int epc_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<EPC_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
/* The case data: the arena (tables, shape lists over shape objects, the
 * debris set, record and stream), the image ranges randomized, the
 * directory pointers random, the low-memory word 0 or not, the fire base name
 * with its terminator, the counter pointer into the arena; the hooks'
 * argument slots. */
static void epc_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    epc_seed=random32();epc_stream=epc_seed^n;epc_real=real;epc_index=index;epc_set=0;
    for(unsigned i=0;i<3;++i)r[i]=epc_value();
    for(unsigned i=0;i<EPC_SIZE;++i)epc_arena[i]=(BYTE)epc_next();
    put32(epc_arena+EPC_VT1+0x18,(uint32_t)(uintptr_t)epc_stub_18);
    put32(epc_arena+EPC_VT2+0x18,(uint32_t)(uintptr_t)epc_stub_19);
    for(unsigned k=0;k<64;++k)put32(epc_arena+EPC_SHAPES+4*k,EPC_A(EPC_OBJECTS+0x40*((k*7u)%16u)));   /* both lists */
    for(unsigned i=0;i<sizeof(epc_ranges)/sizeof(epc_ranges[0]);++i)
        for(unsigned j=0;j<epc_ranges[i].size;++j)mapped[epc_ranges[i].rva+j]=(BYTE)epc_next();
    put32(mapped+0xa09f0,epc_value());put32(mapped+0xa09f8,epc_value());put32(mapped+0xa0a10,epc_value());
    put16(mapped+0xd2680,epc_next()&1?0:(uint16_t)(1u+epc_next()%0xffffu));
    {const unsigned length=epc_next()%5u;for(unsigned j=0;j<5;++j)mapped[0xa0a74+j]=j<length?(BYTE)(1u+epc_next()%255u):0;}
    put32(mapped+0x2cbee4,EPC_A(EPC_COUNTER));
    put32(epc_arena+EPC_STREAM,EPC_A(epc_next()&1?EPC_VT1:EPC_VT2));
    for(unsigned k=0;k<6;++k)a[k]=epc_value();
    if(index==ES2_HOOK_INDEX_debris_set){
        a[0]=EPC_A(EPC_SET);if(epc_next()&1)a[2]=0;epc_set=a[0];
    } else if(index==ES2_HOOK_INDEX_debris_record){
        a[0]=EPC_A(EPC_RECORD);a[1]=EPC_A(EPC_STREAM);
    } else if(index==ES2_HOOK_INDEX_flat_pools&&epc_next()%3==0)a[0]&=0xffff0000u;
    epc_stream=epc_seed;   /* the stubs' stream restarts here in every run */
}
/* Calls target with six argument slots, EAX/EDX/ECX = r, over 3,840 bytes
 * of stack filled with `fill`; out receives EAX, EDX and ECX after it. */
__attribute__((naked,noinline)) static void epc_invoke(void *target,const uint32_t *a,const uint32_t *r,uint32_t fill,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 20(%ebp),%eax\n\tmovl $1024,%ecx\n\tcld\n\trep stosl\n\taddl $3840,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 20(%esi)\n\tpushl 16(%esi)\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tcall *8(%ebp)\n\t"
        "movl 24(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* The core with the adapter's graph and frame; FS:0 restored to the
 * returned head as the adapters do. */
static uint64_t epc_core(unsigned index,const uint32_t *a,const uint32_t *r,uint32_t *ecx){
    const es2_guest_memory *m=&math_native_memory;uint64_t v;
    BYTE scratch[ES2_FIRE_POOLS_SCRATCH]={0};es2_weapon_graph g=weapons_graph(mapped,scratch,index);
    weapons_impact_context c={(uint32_t)(uintptr_t)mapped,0};
    g.call=weapons_impact_guest_call;g.context=&c;
#define EPC_FRAME(x) c.frame=(uint32_t)(uintptr_t)(scratch+(x))
    if(index==ES2_HOOK_INDEX_explosion_pools){EPC_FRAME(ES2_EXPLOSION_POOLS_FRAME);v=es2_explosion_pools_registers(m,&g,r,ecx);}
    else if(index==ES2_HOOK_INDEX_debris_pools){EPC_FRAME(ES2_DEBRIS_POOLS_FRAME);v=es2_debris_pools_registers(m,&g,r,ecx);}
    else if(index==ES2_HOOK_INDEX_debris_set){EPC_FRAME(ES2_DEBRIS_SET_FRAME);v=es2_debris_set_registers(m,&g,a[0],a[1],a[2],r,ecx);}
    else if(index==ES2_HOOK_INDEX_debris_record){g.scratch=0;g.call=weapons_guest_call;g.context=NULL;
        v=es2_debris_record_registers(m,&g,a[0],a[1],r,ecx);}
    else if(index==ES2_HOOK_INDEX_flat_pools){EPC_FRAME(ES2_EFFECT_POOL_FRAME);v=es2_flat_pools_registers(m,&g,a[0],r,ecx);}
    else if(index==ES2_HOOK_INDEX_meteor_pools){EPC_FRAME(ES2_EFFECT_POOL_FRAME);v=es2_meteor_pools_registers(m,&g,r,ecx);}
    else if(index==ES2_HOOK_INDEX_fire_pools){EPC_FRAME(ES2_FIRE_POOLS_FRAME);v=es2_fire_pools_registers(m,&g,r,ecx);}
    else {EPC_FRAME(ES2_EFFECT_POOL_FRAME);v=es2_smoke_balls_registers(m,&g,r,ecx);}
#undef EPC_FRAME
    if(index!=ES2_HOOK_INDEX_debris_record){
        const uint32_t head=index==ES2_HOOK_INDEX_debris_pools||index==ES2_HOOK_INDEX_debris_set?*ecx:(uint32_t)v;
        __asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");
    }
    return v;
}
static int epc_report(const char *what,unsigned n,const uint32_t want[3],const uint32_t got[3],unsigned calls,const epc_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x calls %u/%u\n",current_function,n,what,
        want[0],got[0],want[1],got[1],want[2],got[2],calls,epc_calls);
    for(unsigned j=0;j<calls&&j<epc_calls&&expected;++j)if(memcmp(expected+j,epc_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(epc_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<EPC_SIZE;++j)if(epc_after[j]!=epc_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,epc_after[j],epc_arena[j]);break;}
    return 1;
}
static unsigned epc_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<epc_calls;++j){
        const unsigned id=epc_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?(id==18||id==19)&&c->key==0x18:epc_addresses[id]&&epc_addresses[id]==c->key)++hits;
    }
    return hits;
}
/* One direct case: the original, the adapter and the core. */
static int epc_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r,uint32_t fill){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[EPC_GLOBALS],after[EPC_GLOBALS],actual[EPC_GLOBALS];static epc_record expected[EPC_MAX];uint32_t want[3],got[3];
    const uint32_t head=epc_fs0();
    memcpy(epc_before,epc_arena,EPC_SIZE);epc_globals(before,0);
    epc_reset();epc_invoke(mapped+d->rva,a,r,fill,want);const unsigned calls=epc_calls;memcpy(expected,epc_log,calls*sizeof(*expected));
    memcpy(epc_after,epc_arena,EPC_SIZE);epc_globals(after,0);
    if(epc_fs0()!=head)return fail("effect_pools original left FS:0 changed");
    for(unsigned j=0;j<calls;++j)++epc_paths[expected[j].id];
    /* the adapter (the framework's entry: six argument slots) */
    memcpy(epc_arena,epc_before,EPC_SIZE);epc_globals(before,1);epc_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];
    if(count>16)return fail("effect_pools counter table too long");
    for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    const uint64_t pair=x->replacement_context(a,d->user,r);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);got[2]=want[2];
    epc_globals(actual,0);
    if(epc_fs0()!=head)return fail("effect_pools adapter left FS:0 changed");
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=epc_expected_hits(c+i))return fail("effect_pools callee counter delta");
    if(want[0]!=got[0]||want[1]!=got[1]||calls!=epc_calls||memcmp(expected,epc_log,calls*sizeof(*expected))||
       memcmp(epc_after,epc_arena,EPC_SIZE)||epc_globals_differ(after,actual))
        return epc_report("adapter",n,want,got,calls,expected);
    /* the core with the ECX it reports */
    memcpy(epc_arena,epc_before,EPC_SIZE);epc_globals(before,1);epc_reset();
    got[2]=~want[2];
    const uint64_t core=epc_core(index,a,r,&got[2]);got[0]=(uint32_t)core;got[1]=(uint32_t)(core>>32);epc_globals(actual,0);
    if(epc_fs0()!=head)return fail("effect_pools core left FS:0 changed");
    /* With the hook installed (integration pass) the "original" run is the
       hook, whose ECX is the framework's; ECX is compared otherwise. */
    const int ecx_known=!rng_hooks[index].installed;
    if(want[0]!=got[0]||want[1]!=got[1]||(ecx_known&&want[2]!=got[2])||calls!=epc_calls||memcmp(expected,epc_log,calls*sizeof(*expected))||
       memcmp(epc_after,epc_arena,EPC_SIZE)||epc_globals_differ(after,actual))
        return epc_report("core ECX",n,want,got,calls,expected);
    epc_globals(before,1);
    return 0;
}
static int epc_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],want[3]={0},got[3]={0},pattern=poison?0xa7a7a7a7u:0x31313131u;const uint32_t head=epc_fs0();epc_build(index,n*37u+11u,a,r,0);
        BYTE before[EPC_GLOBALS],after[EPC_GLOBALS],actual[EPC_GLOBALS],expected_fpu[108],actual_fpu[108];static epc_record expected[EPC_MAX];
        memcpy(epc_before,epc_arena,EPC_SIZE);epc_globals(before,0);
        epc_reset();sg_poison_call(mapped+d->rva,a,pattern,want);es2_hook_fpu_save(expected_fpu);
        const unsigned calls=epc_calls;memcpy(expected,epc_log,calls*sizeof(*expected));memcpy(epc_after,epc_arena,EPC_SIZE);epc_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("effect_pools poison install");
        memcpy(epc_arena,epc_before,EPC_SIZE);epc_globals(before,1);epc_reset();sg_poison_call(mapped+d->rva,a,pattern,got);
        es2_hook_fpu_save(actual_fpu);epc_globals(actual,0);
        if(epc_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||calls!=epc_calls||memcmp(expected,epc_log,calls*sizeof(*expected))||
           memcmp(epc_after,epc_arena,EPC_SIZE)||epc_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||
           memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=epc_report(fast?"installed fast":"installed preserving",n,want,got,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("effect_pools poison uninstall");epc_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, pair/trace/arena/globals/FS:0\n",d->name,total);
    return status;
}
/* ---- real callees ---- */
static unsigned epc_nested(unsigned index,unsigned nested[3]){
    if(index==ES2_HOOK_INDEX_debris_pools){nested[0]=ES2_HOOK_INDEX_pool_create;nested[1]=ES2_HOOK_INDEX_debris_set;
        nested[2]=ES2_HOOK_INDEX_debris_record;return 3;}
    if(index==ES2_HOOK_INDEX_debris_set){nested[0]=ES2_HOOK_INDEX_debris_record;return 1;}
    if(index==ES2_HOOK_INDEX_debris_record)return 0;
    nested[0]=ES2_HOOK_INDEX_pool_create;return 1;
}
/* Stubbed in the real-callee pass: every callee except 49630c, 49667c and
 * the nested hooks; new and new[] are a bump allocator; under debris_pools
 * also the callees of the nested debris_set and debris_record. */
static uint32_t epc_real_which(unsigned index){
    if(index==ES2_HOOK_INDEX_debris_pools)return 0x21ceu;   /* 1..3, 6..8, 13 */
    if(index==ES2_HOOK_INDEX_debris_set)return 0x01ceu;     /* 1..3, 6..8 */
    return epc_which(index)&~(uint32_t)0x4021u;             /* without 0, 5 and 14 */
}
static int epc_real_pass(unsigned index){
    unsigned nested[3];const unsigned count=epc_nested(index,nested);
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[4]={0};
    BYTE saved[EPC_IDS][5];int active[EPC_IDS];
    if(epc_patch(saved,active,epc_real_which(index),1))return 1;
    static epc_record expected[EPC_MAX];
    const unsigned modes=2u<<count;
    for(unsigned mode=0;mode<modes&&!status;++mode)for(unsigned n=0;n<2048u/modes&&!status;++n){
        uint32_t a[6],r[3],want[3]={0},got[3]={0},pattern=n&1?0xa7a7a7a7u:0x31313131u;
        epc_build(index,n*13u+mode,a,r,1);
        const uint32_t regs[3]={pattern,pattern,pattern};const uint32_t head=epc_fs0();
        BYTE before[EPC_GLOBALS],after[EPC_GLOBALS],actual[EPC_GLOBALS];
        memcpy(epc_before,epc_arena,EPC_SIZE);epc_globals(before,0);
        es2_hook hk[4];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(mode>>(1+j)&1)?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,function_hook_extension(nested[j])))return fail("effect_pools nested install");}
        epc_reset();epc_invoke(mapped+d->rva,a,regs,pattern,want);
        const unsigned calls=epc_calls;memcpy(expected,epc_log,calls*sizeof(*expected));
        memcpy(epc_after,epc_arena,EPC_SIZE);epc_globals(after,0);
        o.mode=mode&1?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(hk+3,d,&o,function_hook_extension(index)))return fail("effect_pools caller install");
        memcpy(epc_arena,epc_before,EPC_SIZE);epc_globals(before,1);epc_reset();
        epc_invoke(mapped+d->rva,a,regs,pattern,got);epc_globals(actual,0);
        if(epc_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||calls!=epc_calls||memcmp(expected,epc_log,calls*sizeof(*expected))||
           memcmp(epc_after,epc_arena,EPC_SIZE)||epc_globals_differ(after,actual)){
            fprintf(stderr,"real mode=%u\n",mode);status=epc_report("real",n,want,got,calls,expected);
        }
        for(unsigned j=0;j<4;++j){if(!hk[j].descriptor)continue;entered[j]+=hk[j].calls;
            if(hk[j].mismatches)status=fail("effect_pools real compare mismatch");
            if(!es2_hook_uninstall(hk+j))return fail("effect_pools real uninstall");}
        epc_globals(before,1);++total;
    }
    if(epc_patch(saved,active,0,0))return 1;
    epc_real=0;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&!entered[3]){fprintf(stderr,"FAIL: %s never entered\n",d->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, real 49630c/49667c, %u nested hooks original/replaced, hook original/replaced, "
        "31/a7 stack, pair, calls, arena, globals and FS:0\n",d->name,total,count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
static int effect_pools_cases(unsigned index,unsigned limit){
    current_function=function_hook_descriptor(index)->name;
    const uint32_t which=epc_which(index);
    BYTE saved[EPC_IDS][5],old_globals[EPC_GLOBALS];int active[EPC_IDS];
    epc_globals(old_globals,0);
    if(epc_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_EFFECT_POOLS_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);memset(epc_paths,0,sizeof(epc_paths));
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];epc_build(index,n,a,r,0);const uint32_t fill=n%3==0?0:epc_next();
        status=epc_compare(index,n,a,r,fill);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    epc_globals(old_globals,1);
    if(!limit&&!status)status=epc_installed_probes(index,64);
    if(epc_patch(saved,active,which,0))return 1;
    epc_globals(old_globals,1);
    if(!limit&&!status)status=epc_real_pass(index);
    epc_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases (stub calls 49630c %u, 492bec %u, 472f14 %u, 479c78 %u, stream_read %u, 49667c %u, "
        "new[] %u, stream_destroy %u, 474cd8 %u, 45cdf4 %u, 46a03c %u, 48349c %u, 40874c %u, new %u, pool_create %u, "
        "4083f8 %u, 4967ea %u, 496820 %u, read %u/%u); "
        "full return pair and ECX, ordered calls with entry registers, slots and context word, arena/globals/FS:0\n",
        function_hook_descriptor(index)->name,cases,epc_paths[0],epc_paths[1],epc_paths[2],epc_paths[3],epc_paths[4],epc_paths[5],
        epc_paths[6],epc_paths[7],epc_paths[8],epc_paths[9],epc_paths[10],epc_paths[11],epc_paths[12],epc_paths[13],
        epc_paths[14],epc_paths[15],epc_paths[16],epc_paths[17],epc_paths[18],epc_paths[19]);
    return status;
}
