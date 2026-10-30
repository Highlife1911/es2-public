/* building_types group: the building module's stage-2 callback base_types
 * 405fac and the building pools base_pools 405df4
 * (docs/reverse-engineering/mission_load.md, "Building types and pools").
 *
 * Direct cases run the original body (six argument slots, EAX/EDX/ECX at
 * entry from the case, over a stack filled with a case pattern), the
 * adapter (with the callee counter deltas) and the core (with the ECX it
 * reports) against logging stubs for every direct game callee of the hook
 * under test: 49630c (a record stub: the real FS:0 head at record+0,
 * context word +10h zeroed, the record linked into FS:0), the path builder
 * 492bec, the stream constructor 472f14 (the stream's table, a state that is
 * nonzero one time in four), the error report 479c78 (may move the slot
 * block 4a95b0, which base_types loaded before the report), stream_read
 * 4735b8 and two arena tables whose slot +18h is a read stub (count dwords
 * for the 4-byte reads, count words for the 2-byte reads, bounded data
 * otherwise, sometimes a switch of the stream's table), the array
 * construction 49667c and new[] 4774f0 (arena blocks; the count last read
 * may change), stream_destroy 472f64, collision_read 40ccf8 (may change the
 * type count word 4a9598, which the loop rereads), the shape loader 474cd8
 * (stores one of two arena shape lists), the bitmap load 45cdf4, its
 * descriptor 46a03c (may change the bitmap slots 4a9588/4a9590, which the
 * release rereads), the bitmap release 48349c (may move the construction
 * counter pointer), the debris set 40874c, new 47749c (0 one time in four)
 * and pool_create 471ad8 (writes the header; its capacity slot is logged as
 * the word it reads, because the original pushes the caller's EBX high word
 * with it). Frame addresses (the four stream objects, the length byte
 * f-E9h) are logged relative to the hook's record (FS:0), so the original's
 * stack frame and the core's scratch frame compare equal. Compared: the
 * full return pair, ECX, FS:0, the ordered calls with their argument slots
 * (and the bridge's slot count), entry EAX/EDX/ECX and the record's context
 * word, the arena and the image globals the two read or write (the
 * directory pointers 4a09f0 and 4a0a10, the building globals 4a9572..4a95bf,
 * the construction counter pointer 6cbee4; randomized per case, every random
 * register value and pointer outside the thread's stack reservation).
 *
 * Then installed 31/a7 poison probes and a real-callee pass: the real
 * 49630c and 49667c (with its empty element constructors), a bump
 * allocator for new and new[], the other callees stubbed; collision_read,
 * collision_entry and collision_bounds nested under base_types and
 * pool_create nested under base_pools, in rotating original/replaced modes,
 * the hook under test original or replaced against a run without it.
 * Identifiers use the prefix bty_/BTY_. */
typedef struct bty_record { uint32_t id,slots,args[6],regs[3],context,eax,edx; } bty_record;
enum {
    BTY_SIZE=0x8000, BTY_MAX=2048, BTY_IDS=18,
    BTY_VT1=0x40, BTY_VT2=0x80, BTY_COUNTER=0xc0, BTY_COUNTER2=0xd0, BTY_SHAPES=0x100, BTY_SHAPES2=0x180,
    BTY_OBJECTS=0x200, BTY_BUMP=0x600, BTY_FIXED=0x6c00,
    BTY_GLOBALS=0x5a   /* the sizes of bty_ranges: 4 + 4 + 4Eh + 4 */
};
#define BTY_B ((uint32_t)(uintptr_t)bty_arena)
#define BTY_A(o) (BTY_B+(uint32_t)(o))
static BYTE bty_arena[BTY_SIZE],bty_before[BTY_SIZE],bty_after[BTY_SIZE];
static bty_record bty_log[BTY_MAX];
static unsigned bty_calls,bty_real,bty_index,bty_paths[BTY_IDS],bty_last_size;
static uint32_t bty_seed,bty_stream,bty_bump,bty_head,bty_last;
__attribute__((used)) static uint32_t bty_ecx;
/* 0 49630c, 1 492bec, 2 472f14, 3 479c78, 4 stream_read 4735b8, 5 49667c,
 * 6 new[] 4774f0, 7 stream_destroy 472f64, 8 collision_read 40ccf8,
 * 9 474cd8, 10 45cdf4, 11 46a03c, 12 48349c, 13 40874c, 14 new 47749c,
 * 15 pool_create 471ad8, 16/17 the read method of the two arena tables
 * (slot +18h); the slots each original call site pushes. */
static const uint32_t bty_addresses[BTY_IDS]={0x49630c,0x492bec,0x472f14,0x479c78,0x4735b8,0x49667c,0x4774f0,0x472f64,
    0x40ccf8,0x474cd8,0x45cdf4,0x46a03c,0x48349c,0x40874c,0x47749c,0x471ad8,0,0};
static const unsigned bty_counts[BTY_IDS]={0,2,2,6,3,5,1,2,2,4,1,1,2,3,1,3,3,3};
/* The image ranges the two read or write (rva, size). */
static const struct { uint32_t rva,size; } bty_ranges[]={{0xa09f0,4},{0xa0a10,4},{0xa9572,0x4e},{0x2cbee4,4}};
static uint32_t bty_next(void){
    uint32_t z=bty_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *bty_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
/* A random dword outside the thread's 16 MiB stack reservation (bit 31
 * flipped), so that no random value can fall into the frame window of
 * bty_rel in one run and not in the other. */
static uint32_t bty_value(void){
    uint32_t base;__asm__ volatile("movl %%fs:4,%0":"=r"(base));
    const uint32_t v=bty_next();return v-(base-0x1000000u)<0x1000000u?v^0x80000000u:v;
}
static uint32_t bty_fs0(void){uint32_t v;__asm__ volatile("movl %%fs:0,%0":"=r"(v));return v;}
static void bty_reset(void){bty_calls=0;bty_stream=bty_seed;bty_bump=BTY_A(BTY_BUMP);bty_head=bty_fs0();bty_last=0;bty_last_size=0;}
static int bty_in(uint32_t a,uint32_t size){return a-BTY_B<BTY_SIZE&&size<=BTY_SIZE&&a-BTY_B+size<=BTY_SIZE;}
/* The hook's own frame relative to its record (FS:0): base_types' record
 * lies at f-E0h, its locals from f-11Ch; base_pools' at f-28h. */
static uint32_t bty_window_low(void){return bty_index==ES2_HOOK_INDEX_base_types?0x40u:0x4u;}
static uint32_t bty_window(void){return bty_index==ES2_HOOK_INDEX_base_types?0x124u:0x30u;}
static int bty_frame(uint32_t a,uint32_t size){
    const uint32_t fs=bty_fs0(),w=bty_window(),o=a-fs+bty_window_low();
    return fs!=bty_head&&o<w&&size<=w&&o+size<=w;
}
static int bty_writable(uint32_t a,uint32_t size){
    if(bty_in(a,size)||bty_frame(a,size))return 1;
    for(unsigned i=0;i<sizeof(bty_ranges)/sizeof(bty_ranges[0]);++i){
        const uint32_t base=(uint32_t)(uintptr_t)(mapped+bty_ranges[i].rva);
        if(a-base<bty_ranges[i].size&&size<=bty_ranges[i].size&&a-base+size<=bty_ranges[i].size)return 1;
    }
    return 0;
}
/* Values inside the hook's own frame as offsets from its record. */
static uint32_t bty_rel(uint32_t v){
    const uint32_t fs=bty_fs0(),o=v-fs+bty_window_low();
    return fs!=bty_head&&o<bty_window()?0xf0000000u|o:v;
}
/* A count: 0, 1..5 or a small negative; 0..3 in the real pass (the nested
 * collision readers multiply the counts into the arena). */
static uint32_t bty_count(uint32_t pick){
    const unsigned k=(pick>>12)%8;
    if(k==0)return 0;
    if(bty_real)return 1u+(pick>>16)%3;
    if(k==1)return 0u-1u-(pick>>16)%3;
    return 1u+(pick>>16)%5;
}
/* A block in the bump area with 40h bytes of slack (the originals write a
 * count that a stub may raise after the allocation); large or negative
 * sizes get 200h bytes (enough for five records of any table, so a count
 * raised after the allocation never makes two tables alias), the real
 * pass's pool blocks up to 1000h; the fixed block at the arena's end only
 * when the bump area is exhausted. */
static uint32_t bty_alloc(uint64_t size){
    if(size>0x200u&&!(bty_real&&size<=0x1000u))size=0x200u;
    const uint32_t p=bty_bump;bty_bump+=((uint32_t)size+0x43u)&~3u;
    if(bty_bump+0x200u>BTY_A(BTY_FIXED)){bty_bump=BTY_A(BTY_FIXED);return BTY_A(BTY_FIXED);}
    return p;
}
/* The read: a count dword for the 4-byte reads, a count word for the
 * 2-byte reads (remembered for the scribbles), bounded data otherwise;
 * sometimes the stream's table switches (the originals reread it). */
static void bty_read(const uint32_t *a,uint32_t pick){
    const uint32_t size=a[1],buf=a[2];
    if(!bty_real&&pick%9==0&&bty_writable(a[0],4))put32(bty_host(a[0]),BTY_A(pick&0x10?BTY_VT1:BTY_VT2));
    if((size==2||size==4)&&bty_writable(buf,size)){
        const uint32_t v=bty_count(pick);
        if(size==2)put16(bty_host(buf),(uint16_t)v);else put32(bty_host(buf),v);
        bty_last=buf;bty_last_size=size;return;
    }
    if(size>0x200u||!bty_writable(buf,size))return;
    for(uint32_t i=0;i<size;++i)bty_host(buf)[i]=(BYTE)bty_next();
}
static void bty_scribble(uint32_t pick){
    if(bty_real||!bty_last||pick%6!=1)return;
    const uint32_t v=bty_count(pick>>8);
    if(bty_last_size==2)put16(bty_host(bty_last),(uint16_t)v);else put32(bty_host(bty_last),v);
}
/* What the stubs answer and change. */
static uint32_t bty_answer(unsigned id,const uint32_t *a,uint32_t eax,uint32_t pick){
    switch(id){
    case 2: /* 472f14: the stream's table and its state (+4) */
        if(bty_writable(a[0],8)){put32(bty_host(a[0]),BTY_A(pick&1?BTY_VT2:BTY_VT1));put32(bty_host(a[0]+4),pick%4==0?(pick>>8)|1:0);}
        return a[0];
    case 3: /* 479c78: may move the slot block */
        if(!bty_real&&pick%3==0)put32(mapped+0xa95b0,BTY_A(BTY_OBJECTS+0x100));
        return eax;
    case 4: case 16: case 17: /* AL = success in the real pass (the collision readers test it) */
        bty_read(a,pick);return bty_real?eax|1u:eax;
    case 5: /* 49667c: a block; the count may change */
        bty_scribble(pick);
        return bty_alloc((uint64_t)a[1]*a[2]);
    case 6: /* new[]: a block; the count may change */
        bty_scribble(pick);
        return bty_alloc(a[0]);
    case 8: /* collision_read: the type count may drop (only the records read are walked again) */
        if(!bty_real&&pick%5==0&&(int16_t)get16(mapped+0xa9598)>0)put16(mapped+0xa9598,(uint16_t)((pick>>8)%(unsigned)(int16_t)get16(mapped+0xa9598)));
        return eax;
    case 9: /* 474cd8: one of the shape lists and a count */
        if(bty_writable(a[0],4))put32(bty_host(a[0]),BTY_A(pick&1?BTY_SHAPES:BTY_SHAPES2));
        return (bty_value()&0xffff0000u)|(uint16_t)bty_count(pick);
    case 11: /* 46a03c: the bitmap slots may change */
        if(!bty_real&&pick%3==0)put32(mapped+0xa9588,bty_value());
        if(!bty_real&&pick%5==1)put32(mapped+0xa9590,bty_value());
        return eax;
    case 12: /* 48349c: the counter pointer may move */
        if(!bty_real&&pick%3==0)put32(mapped+0x2cbee4,BTY_A(pick&0x100?BTY_COUNTER2:BTY_COUNTER));
        return eax;
    case 14: /* new: 0 one time in four outside the real pass */
        if(!bty_real&&pick%4==0)return 0;
        return bty_alloc(a[0]);
    case 15: /* pool_create: writes into the header */
        if(bty_writable(a[0],10)){put16(bty_host(a[0]),(uint16_t)a[1]);put16(bty_host(a[0]+4),(uint16_t)a[2]);put32(bty_host(a[0]+6),bty_next());}
        return a[0];
    }
    return eax;
}
__attribute__((used,noinline)) static uint64_t __cdecl bty_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(bty_calls==BTY_MAX){fail("building_types call overflow");ExitProcess(1);}
    bty_record *v=bty_log+bty_calls++;memset(v,0,sizeof(*v));v->id=id;
    v->slots=weapons_bridge_slots(a,bty_counts[id]);
    /* pool_create's capacity slot holds the caller's EBX high word over the
     * count word; pool_create reads the word only (SI) */
    for(unsigned k=0;k<bty_counts[id];++k)v->args[k]=id==15&&k==1?a[k]&0xffffu:bty_rel(a[k]);
    for(unsigned j=0;j<3;++j)v->regs[j]=bty_rel(regs[j]);
    if(id&&bty_fs0()!=bty_head)v->context=get16(bty_host(bty_fs0()+16u));
    uint32_t eax=bty_value(),edx=bty_value(),pick=bty_next();bty_ecx=bty_value();
    if(id==0)eax=bty_fs0(); /* the record stub returns the real FS:0 head */
    else eax=bty_answer(id,a,eax,pick);
    v->eax=bty_rel(eax);v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define BTY_STUB(n) __attribute__((naked)) static void bty_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _bty_stub_impl\n\taddl $24,%esp\n\tmovl _bty_ecx,%ecx\n\tret\n\t");}
BTY_STUB(1) BTY_STUB(2) BTY_STUB(3) BTY_STUB(4) BTY_STUB(5) BTY_STUB(6) BTY_STUB(7) BTY_STUB(8) BTY_STUB(9)
BTY_STUB(10) BTY_STUB(11) BTY_STUB(12) BTY_STUB(13) BTY_STUB(14) BTY_STUB(15) BTY_STUB(16) BTY_STUB(17)
#undef BTY_STUB
/* 49630c: logs like the other stubs (EDX and ECX come back unchanged, as
 * the original's do), then links a record at EBP + [descriptor+4] into
 * FS:0 with the saved head at record+0 and a zero context word. */
__attribute__((naked)) static void bty_stub_0(void){__asm__ volatile(
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
    "pushl $0\n\tcalll _bty_stub_impl\n\taddl $12,%esp\n\t"
    "popl %ecx\n\tmovl 4(%ecx),%ecx\n\taddl %ebp,%ecx\n\tmovl %eax,(%ecx)\n\tmovw $0,16(%ecx)\n\tmovl %ecx,%fs:0\n\t"
    "popl %edx\n\tpopl %ecx\n\tret\n\t");}
static void *bty_stubs[BTY_IDS]={bty_stub_0,bty_stub_1,bty_stub_2,bty_stub_3,bty_stub_4,bty_stub_5,bty_stub_6,bty_stub_7,
    bty_stub_8,bty_stub_9,bty_stub_10,bty_stub_11,bty_stub_12,bty_stub_13,bty_stub_14,bty_stub_15,bty_stub_16,bty_stub_17};
static int bty_patch(BYTE saved[BTY_IDS][5],int active[BTY_IDS],uint32_t which,int on){
    for(unsigned i=0;i<BTY_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!bty_addresses[i])continue;if(actor_patch(bty_addresses[i]-0x400000u,bty_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(bty_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
/* Direct callees of each hook (bit i = bty_addresses[i]; the read method is
 * reached through the arena tables). */
static uint32_t bty_which(unsigned index){
    if(index==ES2_HOOK_INDEX_base_types)return 0x3fffu;   /* ids 0..13 */
    return 0xc041u;                                       /* 0, 6, 14, 15 */
}
static void bty_globals(BYTE *out,int restore){
    for(unsigned i=0;i<sizeof(bty_ranges)/sizeof(bty_ranges[0]);++i){
        if(restore)memcpy(mapped+bty_ranges[i].rva,out,bty_ranges[i].size);else memcpy(out,mapped+bty_ranges[i].rva,bty_ranges[i].size);
        out+=bty_ranges[i].size;
    }
}
static int bty_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<BTY_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
/* The case data: the arena (tables, shape lists over shape objects), the
 * image ranges randomized, the directory pointers random, the counter
 * pointer into the arena; base_pools' count slot (a small word in the real
 * pass, where the real pool_create sizes its block from it). */
static void bty_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    bty_seed=random32();bty_stream=bty_seed^n;bty_real=real;bty_index=index;
    for(unsigned i=0;i<3;++i)r[i]=bty_value();
    for(unsigned i=0;i<BTY_SIZE;++i)bty_arena[i]=(BYTE)bty_next();
    put32(bty_arena+BTY_VT1+0x18,(uint32_t)(uintptr_t)bty_stub_16);
    put32(bty_arena+BTY_VT2+0x18,(uint32_t)(uintptr_t)bty_stub_17);
    for(unsigned k=0;k<64;++k)put32(bty_arena+BTY_SHAPES+4*k,BTY_A(BTY_OBJECTS+0x40*((k*7u)%16u)));   /* both lists */
    for(unsigned i=0;i<sizeof(bty_ranges)/sizeof(bty_ranges[0]);++i)
        for(unsigned j=0;j<bty_ranges[i].size;++j)mapped[bty_ranges[i].rva+j]=(BYTE)bty_next();
    put32(mapped+0xa09f0,bty_value());put32(mapped+0xa0a10,bty_value());
    put32(mapped+0x2cbee4,BTY_A(BTY_COUNTER));
    for(unsigned k=0;k<6;++k)a[k]=bty_value();
    if(real&&index==ES2_HOOK_INDEX_base_pools)a[0]=(a[0]&0xffff0000u)|(n%4u);
    bty_stream=bty_seed;   /* the stubs' stream restarts here in every run */
}
/* Calls target with six argument slots, EAX/EDX/ECX = r, over 3,840 bytes
 * of stack filled with `fill`; out receives EAX, EDX and ECX after it. */
__attribute__((naked,noinline)) static void bty_invoke(void *target,const uint32_t *a,const uint32_t *r,uint32_t fill,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 20(%ebp),%eax\n\tmovl $1024,%ecx\n\tcld\n\trep stosl\n\taddl $3840,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 20(%esi)\n\tpushl 16(%esi)\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tcall *8(%ebp)\n\t"
        "movl 24(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* The core with the adapter's graph and frame; FS:0 restored to the
 * returned head as the adapters do. */
static uint64_t bty_core(unsigned index,const uint32_t *a,const uint32_t *r,uint32_t *ecx){
    const es2_guest_memory *m=&math_native_memory;uint64_t v;
    if(index==ES2_HOOK_INDEX_base_types){
        BYTE scratch[ES2_BASE_TYPES_SCRATCH]={0};es2_weapon_graph g=weapons_graph(mapped,scratch,index);
        weapons_impact_context c={(uint32_t)(uintptr_t)mapped,(uint32_t)(uintptr_t)(scratch+ES2_BASE_TYPES_FRAME)};
        g.call=weapons_impact_guest_call;g.context=&c;
        v=es2_base_types_registers(m,&g,r,ecx);
    } else {
        BYTE scratch[ES2_BASE_POOLS_SCRATCH]={0};es2_weapon_graph g=weapons_graph(mapped,scratch,index);
        weapons_impact_context c={(uint32_t)(uintptr_t)mapped,(uint32_t)(uintptr_t)(scratch+ES2_BASE_POOLS_FRAME)};
        g.call=weapons_impact_guest_call;g.context=&c;
        v=es2_base_pools_registers(m,&g,a[0],0,r,ecx);
    }
    uint32_t head=(uint32_t)v;__asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");return v;
}
static int bty_report(const char *what,unsigned n,const uint32_t want[3],const uint32_t got[3],unsigned calls,const bty_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x calls %u/%u\n",current_function,n,what,
        want[0],got[0],want[1],got[1],want[2],got[2],calls,bty_calls);
    for(unsigned j=0;j<calls&&j<bty_calls&&expected;++j)if(memcmp(expected+j,bty_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(bty_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<BTY_SIZE;++j)if(bty_after[j]!=bty_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,bty_after[j],bty_arena[j]);break;}
    return 1;
}
static unsigned bty_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<bty_calls;++j){
        const unsigned id=bty_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?(id==16||id==17)&&c->key==0x18:bty_addresses[id]&&bty_addresses[id]==c->key)++hits;
    }
    return hits;
}
/* One direct case: the original, the adapter and the core. */
static int bty_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r,uint32_t fill){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[BTY_GLOBALS],after[BTY_GLOBALS],actual[BTY_GLOBALS];static bty_record expected[BTY_MAX];uint32_t want[3],got[3];
    const uint32_t head=bty_fs0();
    memcpy(bty_before,bty_arena,BTY_SIZE);bty_globals(before,0);
    bty_reset();bty_invoke(mapped+d->rva,a,r,fill,want);const unsigned calls=bty_calls;memcpy(expected,bty_log,calls*sizeof(*expected));
    memcpy(bty_after,bty_arena,BTY_SIZE);bty_globals(after,0);
    if(bty_fs0()!=head)return fail("building_types original left FS:0 changed");
    for(unsigned j=0;j<calls;++j)++bty_paths[expected[j].id];
    /* the adapter (the framework's entry: six argument slots) */
    memcpy(bty_arena,bty_before,BTY_SIZE);bty_globals(before,1);bty_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];
    if(count>16)return fail("building_types counter table too long");
    for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    const uint64_t pair=x->replacement_context(a,d->user,r);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);got[2]=want[2];
    bty_globals(actual,0);
    if(bty_fs0()!=head)return fail("building_types adapter left FS:0 changed");
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=bty_expected_hits(c+i))return fail("building_types callee counter delta");
    if(want[0]!=got[0]||want[1]!=got[1]||calls!=bty_calls||memcmp(expected,bty_log,calls*sizeof(*expected))||
       memcmp(bty_after,bty_arena,BTY_SIZE)||bty_globals_differ(after,actual))
        return bty_report("adapter",n,want,got,calls,expected);
    /* the core with the ECX it reports */
    memcpy(bty_arena,bty_before,BTY_SIZE);bty_globals(before,1);bty_reset();
    got[2]=~want[2];
    const uint64_t core=bty_core(index,a,r,&got[2]);got[0]=(uint32_t)core;got[1]=(uint32_t)(core>>32);bty_globals(actual,0);
    if(bty_fs0()!=head)return fail("building_types core left FS:0 changed");
    /* With the hook installed (integration pass) the "original" run is the
       hook, whose ECX is the framework's; ECX is compared otherwise. */
    const int ecx_known=!rng_hooks[index].installed;
    if(want[0]!=got[0]||want[1]!=got[1]||(ecx_known&&want[2]!=got[2])||calls!=bty_calls||memcmp(expected,bty_log,calls*sizeof(*expected))||
       memcmp(bty_after,bty_arena,BTY_SIZE)||bty_globals_differ(after,actual))
        return bty_report("core ECX",n,want,got,calls,expected);
    bty_globals(before,1);
    return 0;
}
static int bty_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],want[3]={0},got[3]={0},pattern=poison?0xa7a7a7a7u:0x31313131u;const uint32_t head=bty_fs0();bty_build(index,n*37u+11u,a,r,0);
        BYTE before[BTY_GLOBALS],after[BTY_GLOBALS],actual[BTY_GLOBALS],expected_fpu[108],actual_fpu[108];static bty_record expected[BTY_MAX];
        memcpy(bty_before,bty_arena,BTY_SIZE);bty_globals(before,0);
        bty_reset();sg_poison_call(mapped+d->rva,a,pattern,want);es2_hook_fpu_save(expected_fpu);
        const unsigned calls=bty_calls;memcpy(expected,bty_log,calls*sizeof(*expected));memcpy(bty_after,bty_arena,BTY_SIZE);bty_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("building_types poison install");
        memcpy(bty_arena,bty_before,BTY_SIZE);bty_globals(before,1);bty_reset();sg_poison_call(mapped+d->rva,a,pattern,got);
        es2_hook_fpu_save(actual_fpu);bty_globals(actual,0);
        if(bty_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||calls!=bty_calls||memcmp(expected,bty_log,calls*sizeof(*expected))||
           memcmp(bty_after,bty_arena,BTY_SIZE)||bty_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||
           memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=bty_report(fast?"installed fast":"installed preserving",n,want,got,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("building_types poison uninstall");bty_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, pair/trace/arena/globals/FS:0\n",d->name,total);
    return status;
}
/* ---- real callees ---- */
static unsigned bty_nested(unsigned index,unsigned nested[3]){
    if(index==ES2_HOOK_INDEX_base_types){nested[0]=ES2_HOOK_INDEX_collision_read;nested[1]=ES2_HOOK_INDEX_collision_entry;
        nested[2]=ES2_HOOK_INDEX_collision_bounds;return 3;}
    nested[0]=ES2_HOOK_INDEX_pool_create;return 1;
}
/* Stubbed in the real-callee pass: every callee except 49630c, 49667c and
 * the nested hooks (collision_read under base_types, pool_create under
 * base_pools); new and new[] are a bump allocator. */
static uint32_t bty_real_which(unsigned index){
    if(index==ES2_HOOK_INDEX_base_types)return 0x3edeu;   /* ids 1..4, 6, 7, 9..13 */
    return 0x4040u;                                       /* new[], new */
}
static int bty_real_pass(unsigned index){
    unsigned nested[3];const unsigned count=bty_nested(index,nested);
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[4]={0};
    BYTE saved[BTY_IDS][5];int active[BTY_IDS];
    if(bty_patch(saved,active,bty_real_which(index),1))return 1;
    static bty_record expected[BTY_MAX];
    const unsigned modes=2u<<count;
    for(unsigned mode=0;mode<modes&&!status;++mode)for(unsigned n=0;n<2048u/modes&&!status;++n){
        uint32_t a[6],r[3],want[3]={0},got[3]={0},pattern=n&1?0xa7a7a7a7u:0x31313131u;
        bty_build(index,n*13u+mode,a,r,1);
        const uint32_t regs[3]={pattern,pattern,pattern};const uint32_t head=bty_fs0();
        BYTE before[BTY_GLOBALS],after[BTY_GLOBALS],actual[BTY_GLOBALS];
        memcpy(bty_before,bty_arena,BTY_SIZE);bty_globals(before,0);
        es2_hook hk[4];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(mode>>(1+j)&1)?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,function_hook_extension(nested[j])))return fail("building_types nested install");}
        bty_reset();bty_invoke(mapped+d->rva,a,regs,pattern,want);
        const unsigned calls=bty_calls;memcpy(expected,bty_log,calls*sizeof(*expected));
        memcpy(bty_after,bty_arena,BTY_SIZE);bty_globals(after,0);
        o.mode=mode&1?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(hk+3,d,&o,function_hook_extension(index)))return fail("building_types caller install");
        memcpy(bty_arena,bty_before,BTY_SIZE);bty_globals(before,1);bty_reset();
        bty_invoke(mapped+d->rva,a,regs,pattern,got);bty_globals(actual,0);
        if(bty_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||calls!=bty_calls||memcmp(expected,bty_log,calls*sizeof(*expected))||
           memcmp(bty_after,bty_arena,BTY_SIZE)||bty_globals_differ(after,actual)){
            fprintf(stderr,"real mode=%u\n",mode);status=bty_report("real",n,want,got,calls,expected);
        }
        for(unsigned j=0;j<4;++j){if(!hk[j].descriptor)continue;entered[j]+=hk[j].calls;
            if(hk[j].mismatches)status=fail("building_types real compare mismatch");
            if(!es2_hook_uninstall(hk+j))return fail("building_types real uninstall");}
        bty_globals(before,1);++total;
    }
    if(bty_patch(saved,active,0,0))return 1;
    bty_real=0;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&!entered[3]){fprintf(stderr,"FAIL: %s never entered\n",d->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, real 49630c/49667c, %u nested hooks original/replaced, hook original/replaced, "
        "31/a7 stack, pair, calls, arena, globals and FS:0\n",d->name,total,count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
static int building_types_cases(unsigned index,unsigned limit){
    current_function=function_hook_descriptor(index)->name;
    const uint32_t which=bty_which(index);
    BYTE saved[BTY_IDS][5],old_globals[BTY_GLOBALS];int active[BTY_IDS];
    bty_globals(old_globals,0);
    if(bty_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_BUILDING_TYPES_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);memset(bty_paths,0,sizeof(bty_paths));
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];bty_build(index,n,a,r,0);const uint32_t fill=n%3==0?0:bty_next();
        status=bty_compare(index,n,a,r,fill);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    bty_globals(old_globals,1);
    if(!limit&&!status)status=bty_installed_probes(index,64);
    if(bty_patch(saved,active,which,0))return 1;
    bty_globals(old_globals,1);
    if(!limit&&!status)status=bty_real_pass(index);
    bty_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases (stub calls 49630c %u, 492bec %u, 472f14 %u, 479c78 %u, stream_read %u, 49667c %u, "
        "new[] %u, stream_destroy %u, collision_read %u, 474cd8 %u, 45cdf4 %u, 46a03c %u, 48349c %u, 40874c %u, new %u, "
        "pool_create %u, read %u/%u); "
        "full return pair and ECX, ordered calls with entry registers, slots and context word, arena/globals/FS:0\n",
        function_hook_descriptor(index)->name,cases,bty_paths[0],bty_paths[1],bty_paths[2],bty_paths[3],bty_paths[4],bty_paths[5],
        bty_paths[6],bty_paths[7],bty_paths[8],bty_paths[9],bty_paths[10],bty_paths[11],bty_paths[12],bty_paths[13],
        bty_paths[14],bty_paths[15],bty_paths[16],bty_paths[17]);
    return status;
}
