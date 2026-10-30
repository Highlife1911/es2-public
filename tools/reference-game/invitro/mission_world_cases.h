/* mission_world group: the world load 424308 (script pass 1) and the leaves
 * only it calls, actor_list_init 411df8, gun_slot_table 40fecc, dmg_slot_table
 * 40d218 and col_slot_table 40cbec (docs/reverse-engineering/mission_load.md,
 * "L7 world load").
 *
 * World load. Direct cases run the original body (EAX/EDX/ECX and
 * EBX/ESI/EDI at entry from the case, six unread argument slots, over a
 * stack filled with a case pattern) and the core against logging stubs for
 * every game callee: 49630c (a record stub: the real FS:0 head at record+0,
 * context word +10h zeroed, the record linked into FS:0), memset 49681a (it
 * fills), the path builders 45ee00/492b90, the stream constructor 47ad40
 * (the next of the three files: mission.var, player.mec, the script; an
 * arena table whose slot +18h is a read stub; a nonzero state one time in
 * eight), the fatal report 479c78, the reads 47af98 and slot +18h (the next
 * bytes of the stream's file; a second arena table with its own read stub
 * may replace the first), the close 47ad78, new[] 4774f0, the STR reader
 * 453878, 4539a0 (string pointers into the arena and a count in AX),
 * strlen 496766, the world 42e010, 49667c, region_init, timer_init,
 * group_array, mech_type_select, flyer_type_select, 405ebc, new 47749c (0
 * one time in eight), pool_create, base_pools, the four leaves, mech_types,
 * flyer_types and zone_load. The core runs with its scratch frame at the
 * original's frame address (its own code on a separate stack), so every
 * frame address and register is compared raw, and with a zero stack
 * pattern the whole frame f-0E54h .. f is compared too (every local the
 * original writes, dead stores included). Compared: the pair, ECX, FS:0,
 * the ordered calls with argument slots (and the bridge's slot count), entry
 * EAX/EDX/ECX, the record's context word and the frame contents region_init
 * and timer_init read (hashes), the arena and the image globals. The stubs
 * change what the original reads afterwards (counts the loops reread, the
 * table pointers, selection flags and factory bytes, the header words read
 * at the end, the stream's table). In even cases the adapter is compared as
 * well (stack values masked) with the callee counter deltas. A script
 * without groups lets the original read the group-0 slots f-0D4Eh from
 * stack residue (outside the contract): such scripts come with a zero stack
 * pattern, which equals the core's zeroed scratch.
 *
 * Then installed 31/a7 poison probes and a real-callee pass: the real
 * 49630c and 49667c, a bump allocator for new and new[], the file layer,
 * 405ebc, the world and the type loads stubbed, and fourteen nested hooks
 * (region_init, timer_init, group_array, mech_type_select,
 * flyer_type_select, the three slot bases, pool_create, base_pools and the
 * four leaves) in random original/replaced modes per case, the hook under
 * test original or replaced against a run without it (stack values masked).
 *
 * Leaves. Direct cases with new[] stubbed: the original, the core (with
 * ECX) and the adapter; pair, ECX, the call with its slot and entry
 * registers, the arena and the globals; installed 31/a7 probes. They have
 * no nested hooks. Identifiers use the prefix mwl_/MWL_. */
typedef struct mwl_record { uint32_t id,slots,args[6],regs[3],context,extra,eax,edx; } mwl_record;
enum {
    MWL_SIZE=0x10000, MWL_MAX=6144, MWL_IDS=32, MWL_FILE=0x3000,
    MWL_VT=0x000, MWL_VT2=0x040,     /* stream tables (slot +18h: stubs 6 and 31) */
    MWL_STR=0x100,                   /* 16 strings of 16 bytes (a guard byte, then up to 12 characters) */
    MWL_LIST=0x300,                  /* actor_list_init's list in the leaf cases */
    MWL_COUNTER=0x380,               /* the construction counter [6cbee4] (the real 49630c reads it) */
    MWL_BUMP=0x400,                  /* blocks of new, new[] and 49667c */
    MWL_FIXED=0xe000,                /* the block when the bump area is exhausted (direct cases) */
    MWL_GLOBALS=8+8+4+8+8+8+8+8+110+8+0x24+0xc66+1+4   /* the sizes of mwl_ranges */
};
#define MWL_B ((uint32_t)(uintptr_t)mwl_arena)
#define MWL_A(o) (MWL_B+(uint32_t)(o))
#define MWL_BIT(i) ((uint32_t)1<<(i))
static BYTE mwl_arena[MWL_SIZE],mwl_before[MWL_SIZE],mwl_after[MWL_SIZE];
static BYTE mwl_files[3][MWL_FILE];
static unsigned mwl_file_size[3],mwl_pos[3],mwl_opened;
static uint32_t mwl_streams[3];
static mwl_record mwl_log[MWL_MAX];
static unsigned mwl_calls,mwl_real,mwl_effects,mwl_paths[MWL_IDS];
/* The arena bytes a case copies and compares: all for the world load, the
 * list and the first blocks for the leaves. */
static unsigned mwl_span=MWL_SIZE;
static uint32_t mwl_seed,mwl_stream,mwl_head,mwl_record_va,mwl_bump;
__attribute__((used)) static uint32_t mwl_ecx;
/* 0 49630c, 1 memset 49681a, 2 45ee00, 3 47ad40, 4 479c78, 5 47af98, 6 the
 * read at slot +18h of MWL_VT, 7 47ad78, 8 new[] 4774f0, 9 453878, 10 4539a0,
 * 11 strlen 496766, 12 492b90, 13 42e010, 14 49667c, 15 region_init 423358,
 * 16 timer_init 423104, 17 group_array 423d38, 18 mech_type_select 41ff10,
 * 19 flyer_type_select 422e90, 20 405ebc, 21 new 47749c, 22 pool_create
 * 471ad8, 23 base_pools 405df4, 24 actor_list_init 411df8, 25 gun_slot_table
 * 40fecc, 26 dmg_slot_table 40d218, 27 col_slot_table 40cbec, 28 mech_types
 * 42049c, 29 flyer_types 422fb4, 30 zone_load 42789c, 31 the read at slot
 * +18h of MWL_VT2; and the slots each original call site pushes. */
static const uint32_t mwl_addresses[MWL_IDS]={0x49630c,0x49681a,0x45ee00,0x47ad40,0x479c78,0x47af98,0,0x47ad78,
    0x4774f0,0x453878,0x4539a0,0x496766,0x492b90,0x42e010,0x49667c,0x423358,0x423104,0x423d38,0x41ff10,0x422e90,
    0x405ebc,0x47749c,0x471ad8,0x405df4,0x411df8,0x40fecc,0x40d218,0x40cbec,0x42049c,0x422fb4,0x42789c,0};
static const unsigned mwl_counts[MWL_IDS]={0,3,1,2,6,3,3,2,1,2,4,1,2,1,5,4,4,2,1,1,2,1,3,1,2,0,0,0,0,0,2,3};
/* The image ranges the hooks read or write (rva, size): the collision and
 * damage slot words with their counters, the directory pointer, the
 * building pool and list, the collision and damage slot tables, the gun
 * slot table and word, the actor list, the MECH pool and list, the MECH type
 * count, index table and base word, the FLYER pool and list, the FLYER type
 * count and index table, the script tables, header, result pairs,
 * variables, rectangle, STR object, string pointers, word 4, selection,
 * remap and MEC fields (4a9e00 .. 4aaa65), the vehicle-8 byte, the
 * construction counter pointer. */
static const struct { uint32_t rva,size; } mwl_ranges[]={{0x987d8,8},{0x98850,8},{0xa09f0,4},{0xa95a0,8},{0xa9824,8},
    {0xa98e4,8},{0xa9af8,8},{0xa9b7a,8},{0xa9d06,0x6e},{0xa9db9,8},{0xa9dca,0x24},{0xa9e00,0xc66},{0xd2571,1},{0x2cbee4,4}};
static uint32_t mwl_next(void){
    uint32_t z=mwl_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
/* A random register value; bit 31 set, so it is never a stack address. */
static uint32_t mwl_value(void){return mwl_next()|0x80000000u;}
static BYTE *mwl_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static uint32_t mwl_fs0(void){uint32_t v;__asm__ volatile("movl %%fs:0,%0":"=r"(v));return v;}
static int mwl_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
/* The masked form of a logged value: 0 for an address of this thread's
 * stack (the adapter, installed and real-callee runs have their frames
 * elsewhere). */
static uint32_t mwl_mask(uint32_t value){return mwl_stack(value)?0:value;}
static uint32_t mwl_hash(uint32_t a,unsigned n){
    uint32_t h=0x811c9dc5u;const BYTE *p=mwl_host(a);
    for(unsigned i=0;i<n;++i)h=(h^p[i])*0x01000193u;
    return h;
}
/* A random data byte that is never the top byte of an address of this
 * thread's stack, so no four data bytes read as a dword look like one. */
static BYTE mwl_byte(void){
    const uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;
    BYTE b=(BYTE)mwl_next();
    if(b==(BYTE)((top-0x1000000u)>>24)||b==(BYTE)((top-1u)>>24))b^=0x80;
    return b;
}
/* n random data bytes, four per draw, none the top byte of a stack address. */
static void mwl_fill(BYTE *p,unsigned n){
    const uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;
    const BYTE t1=(BYTE)((top-0x1000000u)>>24),t2=(BYTE)((top-1u)>>24);
    for(unsigned i=0;i<n;i+=4){
        const uint32_t v=mwl_next();
        for(unsigned k=0;k<4&&i+k<n;++k){BYTE b=(BYTE)(v>>(8*k));if(b==t1||b==t2)b^=0x80;p[i+k]=b;}
    }
}
static void mwl_reset(void){
    mwl_calls=0;mwl_stream=mwl_seed;mwl_head=mwl_fs0();mwl_record_va=0;mwl_bump=MWL_A(MWL_BUMP);mwl_opened=0;
    memset(mwl_pos,0,sizeof(mwl_pos));memset(mwl_streams,0,sizeof(mwl_streams));
}
static int mwl_in(uint32_t a,uint32_t size){return a-MWL_B<MWL_SIZE&&size<=MWL_SIZE&&a-MWL_B+size<=MWL_SIZE;}
/* The world load's own frame (its record lies at f-54h, its lowest local
 * at f-0E54h). */
static int mwl_frame(uint32_t a,uint32_t size){
    const uint32_t fs=mwl_fs0();
    if(fs==mwl_head)return 0;
    const uint32_t lo=fs+0x54u-0xe54u,o=a-lo;
    return o<0xe54u&&size<=0xe54u&&o+size<=0xe54u;
}
static int mwl_writable(uint32_t a,uint32_t size){
    if(mwl_in(a,size)||mwl_frame(a,size))return 1;
    for(unsigned i=0;i<sizeof(mwl_ranges)/sizeof(mwl_ranges[0]);++i){
        const uint32_t base=(uint32_t)(uintptr_t)(mapped+mwl_ranges[i].rva);
        if(a-base<mwl_ranges[i].size&&size<=mwl_ranges[i].size&&a-base+size<=mwl_ranges[i].size)return 1;
    }
    return 0;
}
/* A block in the bump area with 240h bytes of slack for a list the
 * original fills; sizes above 2000h (or negative) get 400h, except the real
 * pass's pool blocks up to 8000h; the fixed block once the area is
 * exhausted. */
static uint32_t mwl_alloc(uint64_t size){
    if(size>0x2000u&&!(mwl_real&&size<=0x8000u))size=0x400u;
    const uint32_t need=((uint32_t)size+0x243u)&~3u;
    if(mwl_bump+need>MWL_A(MWL_FIXED)){
        if(mwl_real){fail("mission_world bump area exhausted");ExitProcess(1);}
        return MWL_A(MWL_FIXED);
    }
    const uint32_t p=mwl_bump;mwl_bump+=need;return p;
}
static int mwl_file_of(uint32_t stream){for(unsigned i=0;i<mwl_opened;++i)if(mwl_streams[i]==stream)return (int)i;return -1;}
/* A read: the next `size` bytes of the stream's file (0 past its end);
 * sizes of 0 or above 800h (negative counts) read nothing. With the stubs'
 * effects a lowered count makes the lists slide, so there every 2-byte
 * read gives a small word (-3 .. 11): no loop runs long and no index
 * reaches far. */
static void mwl_read(const uint32_t *a){
    const int file=mwl_file_of(a[0]);const uint32_t size=a[1],buf=a[2];
    if(file<0||size-1u>=0x800u||!mwl_writable(buf,size))return;
    BYTE *p=mwl_host(buf);
    for(uint32_t i=0;i<size;++i)p[i]=mwl_pos[file]<mwl_file_size[file]?mwl_files[file][mwl_pos[file]++]:0;
    if(mwl_effects&&size==2)put16(p,(uint16_t)((int)(get16(p)%15u)-3));
}
/* A count word of the image lowered (the loops reread it). */
static void mwl_lower(uint32_t rva,uint32_t pick){
    const int16_t v=(int16_t)get16(mapped+rva);
    if(v>0)put16(mapped+rva,(uint16_t)((pick>>12)%(unsigned)v));
}
/* What the stubs answer and change. */
static uint32_t mwl_answer(unsigned id,const uint32_t *a,uint32_t eax,uint32_t pick){
    const int effect=mwl_effects&&pick%4==0;
    switch(id){
    case 1:   /* memset: fills; EAX = the destination */
        if(a[2]<=0x8000u&&mwl_writable(a[0],a[2]))memset(mwl_host(a[0]),(int)(BYTE)a[1],a[2]);
        return a[0];
    case 3:   /* 47ad40: the next file, the table, the state */
        if(mwl_opened<3)mwl_streams[mwl_opened++]=a[0];
        if(mwl_writable(a[0],8)){put32(mwl_host(a[0]),MWL_A(MWL_VT));put32(mwl_host(a[0]+4),!mwl_real&&pick%8==0?(pick>>8)|1u:0);}
        return a[0];
    case 5:case 6:case 31:   /* the reads; the stream's table or a count the loop rereads may change */
        mwl_read(a);
        if(effect)switch(pick>>8&7){
        case 0:if(mwl_writable(a[0],4))put32(mwl_host(a[0]),MWL_A(pick&0x800?MWL_VT2:MWL_VT));break;
        case 1:mwl_lower(0xa9e06,pick);break;
        case 2:mwl_lower(0xa9e14,pick);break;
        case 3:mwl_lower(0xa9e24,pick);break;
        case 4:mwl_lower(0xaaa5e,pick);break;
        case 5:mwl_lower(0xa9e08,pick);break;
        default:break;
        }
        return eax;
    case 8:   /* new[] */
        return mwl_alloc(a[0]);
    case 10: {   /* 4539a0: k string pointers, the count in AX (or 0 / negative) */
        const unsigned k=pick%6==0?0:(pick>>4)%13;
        for(unsigned i=0;i<k&&mwl_writable(a[1]+4u*i,4);++i)put32(mwl_host(a[1]+4u*i),MWL_A(MWL_STR+16u*((pick>>8)+i*5u)%256u+1u));
        if(!mwl_real&&pick%16==1)return (eax&0xffff0000u)|(0x8000u|(pick>>16));
        return (eax&0xffff0000u)|k;
    }
    case 11:   /* strlen */
        return (uint32_t)strlen((const char *)mwl_host(a[0]));
    case 13:   /* the world: the header words read at the end */
        if(effect){if(pick&0x100)put16(mapped+0xa9e4e,(uint16_t)mwl_next());else put16(mapped+0xa9e50,(uint16_t)mwl_next());}
        return eax;
    case 14:   /* 49667c: a block */
        return mwl_alloc((uint64_t)a[1]*(uint16_t)a[2]);
    case 15:   /* region_init: the region count, the coordinate base or count */
        if(effect)switch(pick>>8&3){
        case 0:mwl_lower(0xa9e1c,pick);break;
        case 1:put32(mapped+0xa9e0c,mwl_value());break;
        default:put16(mapped+0xa9e06,(uint16_t)mwl_next());break;
        }
        return eax;
    case 16:   /* timer_init: the trigger or timer base, the timer count */
        if(effect)switch(pick>>8&3){
        case 0:put32(mapped+0xa9e28,mwl_value());break;
        case 1:put32(mapped+0xa9e38,mwl_value());break;
        default:mwl_lower(0xa9e34,pick);break;
        }
        return eax;
    case 18:   /* mech_type_select: a mech flag */
        if(effect)mapped[0xaa72a+(pick>>8)%12]=(BYTE)(pick>>16&1);
        return eax;
    case 19:   /* flyer_type_select: a flyer flag */
        if(effect)mapped[0xaa856+(pick>>8)%8]=(BYTE)(pick>>16&1);
        return eax;
    case 20:   /* 405ebc: a building flag or factory byte */
        if(effect){if(pick&0x100)mapped[0xaa8ba+(pick>>9)%8]=(BYTE)(pick>>16&1);else mapped[0xaa9d2+(pick>>9)%8]=(BYTE)(pick>>16);}
        return eax;
    case 21:   /* new: 0 one time in eight outside the real pass */
        if(!mwl_real&&pick%8==0)return 0;
        return mwl_alloc(a[0]);
    case 22:   /* pool_create: writes into the header */
        if(mwl_writable(a[0],10)){put16(mwl_host(a[0]),(uint16_t)a[1]);put16(mwl_host(a[0]+4),(uint16_t)a[2]);put32(mwl_host(a[0]+6),mwl_next());}
        return a[0];
    case 24:   /* actor_list_init: the list */
        if(mwl_writable(a[0],6)){put32(mwl_host(a[0]),mwl_value());put16(mwl_host(a[0]+4),(uint16_t)a[1]);}
        return eax;
    case 25:put32(mapped+0xa98e4,mwl_value());return eax;
    case 26:put32(mapped+0xa9828,mwl_value());return eax;
    case 27:put32(mapped+0xa9824,mwl_value());return eax;
    case 28:case 29:   /* the type loads: the header words read for zone_load */
        if(effect){if(pick&0x100)put16(mapped+0xa9e4e,(uint16_t)mwl_next());else put16(mapped+0xa9e50,(uint16_t)mwl_next());}
        return eax;
    case 30:   /* zone_load: header word 4, read after it */
        if(effect)put16(mapped+0xa9e56,(uint16_t)mwl_next());
        return eax;
    }
    return eax;
}
__attribute__((used,noinline)) static uint64_t __cdecl mwl_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(mwl_calls==MWL_MAX){fail("mission_world call overflow");ExitProcess(1);}
    mwl_record *v=mwl_log+mwl_calls++;memset(v,0,sizeof(*v));v->id=id;
    v->slots=weapons_bridge_slots(a,mwl_counts[id]);
    for(unsigned k=0;k<mwl_counts[id];++k)v->args[k]=a[k];
    for(unsigned j=0;j<3;++j)v->regs[j]=regs[j];
    if(id&&mwl_fs0()!=mwl_head){v->context=get16(mwl_host(mwl_fs0()+16u));if(!mwl_record_va)mwl_record_va=mwl_fs0();}
    /* the frame contents region_init and timer_init read */
    if(id==15)v->extra=mwl_hash(a[1],6);
    if(id==16)v->extra=mwl_hash(a[2],40);
    uint32_t eax=mwl_value(),edx=mwl_value(),pick=mwl_next();mwl_ecx=mwl_value();
    if(id==0)eax=mwl_fs0();
    else eax=mwl_answer(id,a,eax,pick);
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define MWL_STUB(n) __attribute__((naked)) static void mwl_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _mwl_stub_impl\n\taddl $24,%esp\n\tmovl _mwl_ecx,%ecx\n\tret\n\t");}
MWL_STUB(1) MWL_STUB(2) MWL_STUB(3) MWL_STUB(4) MWL_STUB(5) MWL_STUB(6) MWL_STUB(7) MWL_STUB(8) MWL_STUB(9) MWL_STUB(10)
MWL_STUB(11) MWL_STUB(12) MWL_STUB(13) MWL_STUB(14) MWL_STUB(15) MWL_STUB(16) MWL_STUB(17) MWL_STUB(18) MWL_STUB(19)
MWL_STUB(20) MWL_STUB(21) MWL_STUB(22) MWL_STUB(23) MWL_STUB(24) MWL_STUB(25) MWL_STUB(26) MWL_STUB(27) MWL_STUB(28)
MWL_STUB(29) MWL_STUB(30) MWL_STUB(31)
#undef MWL_STUB
/* 49630c: logs like the other stubs (EDX and ECX come back unchanged, as the
 * original's do), then links a record at EBP + [descriptor+4] into FS:0
 * with the saved head at record+0 and a zero context word. */
__attribute__((naked)) static void mwl_stub_0(void){__asm__ volatile(
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
    "pushl $0\n\tcalll _mwl_stub_impl\n\taddl $12,%esp\n\t"
    "popl %ecx\n\tmovl 4(%ecx),%ecx\n\taddl %ebp,%ecx\n\tmovl %eax,(%ecx)\n\tmovw $0,16(%ecx)\n\tmovl %ecx,%fs:0\n\t"
    "popl %edx\n\tpopl %ecx\n\tret\n\t");}
static void *mwl_stubs[MWL_IDS]={mwl_stub_0,mwl_stub_1,mwl_stub_2,mwl_stub_3,mwl_stub_4,mwl_stub_5,mwl_stub_6,mwl_stub_7,
    mwl_stub_8,mwl_stub_9,mwl_stub_10,mwl_stub_11,mwl_stub_12,mwl_stub_13,mwl_stub_14,mwl_stub_15,mwl_stub_16,mwl_stub_17,
    mwl_stub_18,mwl_stub_19,mwl_stub_20,mwl_stub_21,mwl_stub_22,mwl_stub_23,mwl_stub_24,mwl_stub_25,mwl_stub_26,
    mwl_stub_27,mwl_stub_28,mwl_stub_29,mwl_stub_30,mwl_stub_31};
static int mwl_patch(BYTE saved[MWL_IDS][5],int active[MWL_IDS],uint32_t which,int on){
    for(unsigned i=0;i<MWL_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!mwl_addresses[i])continue;
            if(actor_patch(mwl_addresses[i]-0x400000u,mwl_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(mwl_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
/* Stubbed in the world load's direct cases: every callee. In its real pass
 * 49630c, 49667c and the nested hooks run for real (new and new[] are a
 * bump allocator). The leaves' cases stub new[] only. */
#define MWL_ALL 0x7fffffffu
#define MWL_REAL (MWL_ALL&~(MWL_BIT(0)|MWL_BIT(14)|MWL_BIT(15)|MWL_BIT(16)|MWL_BIT(17)|MWL_BIT(18)|MWL_BIT(19)| \
    MWL_BIT(22)|MWL_BIT(23)|MWL_BIT(24)|MWL_BIT(25)|MWL_BIT(26)|MWL_BIT(27)))
#define MWL_LEAF MWL_BIT(8)
static void mwl_globals(BYTE *out,int restore){
    for(unsigned i=0;i<sizeof(mwl_ranges)/sizeof(mwl_ranges[0]);++i){
        if(restore)memcpy(mapped+mwl_ranges[i].rva,out,mwl_ranges[i].size);else memcpy(out,mapped+mwl_ranges[i].rva,mwl_ranges[i].size);
        out+=mwl_ranges[i].size;
    }
}
static int mwl_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<MWL_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
/* ---- the synthetic files ---- */
static void mwl_put16(unsigned f,uint16_t w){
    if(mwl_file_size[f]+2u>MWL_FILE)return;
    mwl_files[f][mwl_file_size[f]++]=(BYTE)w;mwl_files[f][mwl_file_size[f]++]=(BYTE)(w>>8);
}
static BYTE *mwl_bytes(unsigned f,unsigned n){
    static BYTE sink[0x800];
    if(mwl_file_size[f]+n>MWL_FILE)return sink;
    BYTE *p=mwl_files[f]+mwl_file_size[f];for(unsigned i=0;i<n;++i)p[i]=mwl_byte();mwl_file_size[f]+=n;return p;
}
/* A list count: 0, 1 .. limit, or (not exact) -1 .. -3 (a negative count
 * never runs a loop; the MEC actors are appended at the mech count, which
 * indexes the frame's type array, so it stays small). */
static uint16_t mwl_count(unsigned limit,int exact){
    const unsigned k=mwl_next()%8;
    if(k==0)return 0;
    if(k==1&&!exact)return (uint16_t)(0xffffu-mwl_next()%3);
    return (uint16_t)(1u+mwl_next()%limit);
}
/* An index word: -1 (or another negative), or 0 .. limit-1. */
static uint16_t mwl_index(unsigned limit,int exact){
    const unsigned k=mwl_next()%4;
    if(k==0)return 0xffff;
    if(k==1&&!exact)return (uint16_t)(0xfff0u|mwl_next());
    return (uint16_t)(mwl_next()%limit);
}
/* The case: the arena (stream tables, strings, random blocks), the image
 * ranges, and the three files. exact: the real callers' kind of input;
 * real: data for the real-callee pass (small non-negative counts, valid
 * types and member indices); patterned: the caller runs over a nonzero
 * stack pattern (then, like with a nonzero *fill, the script has groups). */
static void mwl_build(unsigned n,uint32_t r[3],uint32_t regs[3],uint32_t *fill,int exact,int real,int patterned){
    mwl_seed=random32();mwl_stream=mwl_seed^n;mwl_real=real;
    /* the other inputs come in two kinds: the stubs' effects (a lowered
     * count makes the script's lists slide, so a group count may read 0) or
     * a nonzero stack pattern (the script read as written, so it keeps its
     * groups) */
    const int patterned_case=!exact&&!real&&mwl_next()%2;
    mwl_effects=!real&&!exact&&!patterned_case;
    for(unsigned i=0;i<3;++i){r[i]=mwl_value();regs[i]=mwl_value();}
    mwl_fill(mwl_arena,MWL_SIZE);
    *fill=patterned_case?mwl_next()|1u:0;
    const int pattern=patterned||*fill;
    put32(mwl_arena+MWL_VT+0x18,(uint32_t)(uintptr_t)mwl_stub_6);
    put32(mwl_arena+MWL_VT2+0x18,(uint32_t)(uintptr_t)mwl_stub_31);
    /* strings: a guard byte (LF one time in four) and 0 .. 12 characters,
     * the last one an LF one time in two */
    for(unsigned k=0;k<16;++k){
        BYTE *s=mwl_arena+MWL_STR+16u*k;const unsigned len=mwl_next()%13;
        s[0]=mwl_next()%4==0?0x0a:(BYTE)('A'+mwl_next()%26);
        for(unsigned i=0;i<len;++i)s[1+i]=(BYTE)('a'+mwl_next()%26);
        if(len&&mwl_next()%2)s[len]=0x0a;
        s[1+len]=0;
    }
    /* the image ranges */
    for(unsigned i=0;i<sizeof(mwl_ranges)/sizeof(mwl_ranges[0]);++i)mwl_fill(mapped+mwl_ranges[i].rva,mwl_ranges[i].size);
    put32(mapped+0xa09f0,mwl_value());put32(mapped+0x2cbee4,MWL_A(MWL_COUNTER));
    if(real){
        memset(mapped+0xa9d08,0xff,0x55);put16(mapped+0xa9d06,0);memset(mapped+0xa9dcc,0xff,0x22);put16(mapped+0xa9dca,0);
        put16(mapped+0xa98e8,0);put16(mapped+0x987dc,0);put16(mapped+0x98856,0);
    }
    /* mission.var */
    memset(mwl_file_size,0,sizeof(mwl_file_size));
    mwl_bytes(0,2000);
    /* player.mec: the player ordinal, N, the records (word +6 the item count) */
    {const uint16_t mec=(uint16_t)(!exact&&!real&&mwl_next()%16==0?0xffffu-mwl_next()%3:mwl_next()%4);
     mwl_put16(1,(uint16_t)mwl_next());mwl_put16(1,mec);
     for(int i=0;i<(int16_t)mec;++i){
        const uint16_t items=(uint16_t)(!exact&&mwl_next()%8==0?0xfffeu:mwl_next()%13);
        mwl_put16(1,(uint16_t)mwl_next());mwl_put16(1,(uint16_t)mwl_next());
        mwl_put16(1,(uint16_t)(real?mwl_next()%20:mwl_next()%4==0?8:mwl_next()));mwl_put16(1,items);
        if((int16_t)items>0){mwl_bytes(1,2u*items);mwl_bytes(1,2u*items);}
        mwl_put16(1,(uint16_t)mwl_next());mwl_bytes(1,26);mwl_bytes(1,20);mwl_bytes(1,20);
     }}
    /* the script: header; coordinates, angles, routes, regions, triggers, timers */
    mwl_bytes(2,20);
    {const uint16_t c=mwl_count(6,exact||real);mwl_put16(2,c);for(int i=0;c&&i<(int16_t)c;++i)mwl_bytes(2,12);}
    {const uint16_t c=mwl_count(5,exact||real);mwl_put16(2,c);for(int i=0;c&&i<(int16_t)c;++i)mwl_bytes(2,2);}
    {const uint16_t c=mwl_count(3,exact||real);mwl_put16(2,c);
     for(int i=0;c&&i<(int16_t)c;++i){const uint16_t k=mwl_count(5,exact||real);mwl_put16(2,k);
        for(int j=0;k&&j<(int16_t)k;++j)mwl_put16(2,mwl_index(8,exact||real));}}
    {const uint16_t c=mwl_count(4,exact||real);mwl_put16(2,c);for(int i=0;c&&i<(int16_t)c;++i)mwl_bytes(2,6);}
    {const uint16_t c=mwl_count(3,exact||real);mwl_put16(2,c);
     for(int i=0;c&&i<(int16_t)c;++i){
        mwl_bytes(2,4);
        const unsigned lead=mwl_next()%3==0?8u:mwl_next()%9;   /* the leading non-negative region indices */
        for(unsigned j=0;j<8;++j)mwl_put16(2,j<lead?(uint16_t)(mwl_next()%5):(uint16_t)(0x8000u|mwl_next()));
        mwl_bytes(2,50);mwl_put16(2,(uint16_t)mwl_next());mwl_put16(2,(uint16_t)mwl_next());
     }}
    {const uint16_t c=mwl_count(3,exact||real);mwl_put16(2,c);
     for(int i=0;c&&i<(int16_t)c;++i){mwl_put16(2,mwl_index(4,exact||real));mwl_put16(2,(uint16_t)mwl_next());
        for(unsigned j=0;j<10;++j)mwl_put16(2,mwl_index(4,exact||real));}}
    /* the authored mechs (type +28h), flyers (+2Ch), buildings (+0), orders */
    const uint16_t mechs=(uint16_t)(!exact&&!real&&mwl_next()%16==0?0xffffu-mwl_next()%3:mwl_next()%8);
    mwl_put16(2,mechs);
    for(int i=0;i<(int16_t)mechs;++i){BYTE *p=mwl_bytes(2,134);put16(p+0x28,(uint16_t)(real?mwl_next()%30:mwl_next()));}
    const uint16_t flyers=mwl_count(4,exact||real);mwl_put16(2,flyers);
    for(int i=0;flyers&&i<(int16_t)flyers;++i){BYTE *p=mwl_bytes(2,92);put16(p+0x2c,(uint16_t)(real?mwl_next()%12:mwl_next()));}
    const uint16_t buildings=mwl_count(6,exact||real);mwl_put16(2,buildings);
    for(int i=0;buildings&&i<(int16_t)buildings;++i)mwl_bytes(2,52);
    {const uint16_t c=mwl_count(4,exact||real);mwl_put16(2,c);for(int i=0;c&&i<(int16_t)c;++i)mwl_bytes(2,14);}
    /* groups: with a nonzero stack pattern always some (the original reads
     * the group-0 slots from the stack otherwise); group 0's roster, the
     * others' class +28h and members +32h (-3 .. 139: the selection writes
     * stay inside 4aa72a .. 4aaa65) */
    {uint16_t c=mwl_count(5,exact||real);if(pattern&&(int16_t)c<=0)c=(uint16_t)(1u+mwl_next()%4);
     mwl_put16(2,c);
     for(int i=0;c&&i<(int16_t)c;++i){
        BYTE *p=mwl_bytes(2,156);
        static const uint16_t classes[8]={0,0,1,2,2,3,0xffff,1};
        put16(p+0x28,real?(uint16_t)(mwl_next()%3):classes[mwl_next()%8]);
        for(unsigned k=0;k<20;++k){
            uint16_t w;
            if(mwl_next()%3==0)w=(uint16_t)(0xfffdu+mwl_next()%3);
            else w=(uint16_t)(real?mwl_next()%8:mwl_next()%(mwl_next()%4==0?140:10));
            put16(p+0x32u+2u*k,w);
        }
     }}
    {const uint16_t c=mwl_count(4,exact||real);mwl_put16(2,c);for(int i=0;c&&i<(int16_t)c;++i)mwl_bytes(2,54);}
    {const uint16_t c=mwl_count(6,exact||real);mwl_put16(2,c);if((int16_t)c>0)mwl_bytes(2,2u*c);}
    mwl_stream=mwl_seed;   /* the stubs' stream restarts here in every run */
}
/* Calls target with six (unread) argument slots, EAX/EDX/ECX = r and
 * EBX/ESI/EDI = regs over 7,936 bytes of stack filled with `fill` (the
 * frame of 0E54h bytes lies inside); out receives EAX, EDX and ECX. */
__attribute__((naked,noinline)) static void mwl_invoke(void *target,const uint32_t *a,const uint32_t *r,const uint32_t *regs,uint32_t fill,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $8192,%esp\n\tmovl %esp,%edi\n\tmovl 24(%ebp),%eax\n\tmovl $2048,%ecx\n\tcld\n\trep stosl\n\taddl $7936,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 20(%esi)\n\tpushl 16(%esi)\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 20(%ebp),%edi\n\tmovl (%edi),%ebx\n\tmovl 4(%edi),%esi\n\tmovl 8(%edi),%edi\n\t"
        "movl 16(%ebp),%eax\n\tmovl 4(%eax),%edx\n\tmovl 8(%eax),%ecx\n\tmovl (%eax),%eax\n\tcall *8(%ebp)\n\t"
        "movl 28(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* Calls fn(arg) with ESP switched to `top` (a static stack) and back. */
__attribute__((naked,noinline)) static void mwl_switch_call(void (*fn)(void *),void *arg,uint32_t top){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tmovl 16(%ebp),%esp\n\tpushl 12(%ebp)\n\tcalll *8(%ebp)\n\t"
        "movl %ebp,%esp\n\tpopl %ebp\n\tret\n\t");
}
typedef struct mwl_core_call { const uint32_t *r; uint32_t frame; uint64_t pair; uint32_t head; } mwl_core_call;
static void mwl_core_body(void *p){
    mwl_core_call *c=p;
    es2_weapon_graph g=weapons_graph(mapped,mwl_host(c->frame-ES2_WORLD_LOAD_FRAME),ES2_HOOK_INDEX_world_load);
    weapons_impact_context x={(uint32_t)(uintptr_t)mapped,c->frame};
    g.call=weapons_impact_guest_call;g.context=&x;
    c->head=0;
    c->pair=es2_world_load_registers(&math_native_memory,&g,c->r,&c->head);
    __asm__ volatile("movl %0,%%fs:0"::"r"(c->head):"memory");
}
static BYTE mwl_core_stack[0x10000] __attribute__((aligned(16)));
/* The core with the adapter's graph and a zeroed scratch whose frame f lies
 * at the original's EBP (`frame`, on the stack below the caller, free after
 * the original returned); the core's own code runs on a static stack. */
static uint64_t mwl_core(const uint32_t *r,uint32_t frame,uint32_t *ecx){
    memset(mwl_host(frame-ES2_WORLD_LOAD_FRAME),0,ES2_WORLD_LOAD_SCRATCH);
    mwl_core_call c={r,frame,0,0};
    mwl_switch_call(mwl_core_body,&c,(uint32_t)(uintptr_t)(mwl_core_stack+sizeof(mwl_core_stack)));
    if(ecx)*ecx=c.head;
    return c.pair;
}
static void mwl_mask_log(mwl_record *log,unsigned n){
    for(unsigned j=0;j<n;++j){
        for(unsigned k=0;k<6;++k)log[j].args[k]=mwl_mask(log[j].args[k]);
        for(unsigned k=0;k<3;++k)log[j].regs[k]=mwl_mask(log[j].regs[k]);
        log[j].eax=mwl_mask(log[j].eax);log[j].edx=mwl_mask(log[j].edx);
    }
}
static int mwl_report(const char *what,unsigned n,const uint32_t want[3],const uint32_t got[3],unsigned calls,const mwl_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x calls %u/%u\n",current_function,n,what,
        want[0],got[0],want[1],got[1],want[2],got[2],calls,mwl_calls);
    for(unsigned j=0;j<calls&&j<mwl_calls&&expected;++j)if(memcmp(expected+j,mwl_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(mwl_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        for(unsigned i=j>8?j-8:0;i<j;++i)fprintf(stderr,"  call%u id%u args %08x %08x %08x regs %08x %08x %08x ret %08x %08x\n",i,
            expected[i].id,expected[i].args[0],expected[i].args[1],expected[i].args[2],expected[i].regs[0],expected[i].regs[1],
            expected[i].regs[2],expected[i].eax,expected[i].edx);
        break;
    }
    for(unsigned j=0;j<mwl_span;++j)if(mwl_after[j]!=mwl_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,mwl_after[j],mwl_arena[j]);break;}
    return 1;
}
/* The callee counter a call adds: by VA, or by slot for the two read stubs. */
static unsigned mwl_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<mwl_calls;++j){
        const unsigned id=mwl_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?(id==6||id==31)&&c->key==0x18u:mwl_addresses[id]&&mwl_addresses[id]==c->key)++hits;
    }
    return hits;
}
/* One direct case of the world load: the original, the core (frame at the
 * original's address) and (exact inputs) the adapter. */
static int mwl_compare(unsigned n,const uint32_t *r,const uint32_t *regs,uint32_t fill,int exact,int check_ecx){
    const unsigned index=ES2_HOOK_INDEX_world_load;
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[MWL_GLOBALS],after[MWL_GLOBALS],actual[MWL_GLOBALS];static mwl_record expected[MWL_MAX];static BYTE frame_want[0xe54];
    uint32_t want[3],got[3];
    static const uint32_t a[6]={0};
    const uint32_t head=mwl_fs0();
    memcpy(mwl_before,mwl_arena,mwl_span);mwl_globals(before,0);
    mwl_reset();mwl_invoke(mapped+d->rva,a,r,regs,fill,want);const unsigned calls=mwl_calls;memcpy(expected,mwl_log,calls*sizeof(*expected));
    memcpy(mwl_after,mwl_arena,mwl_span);mwl_globals(after,0);
    if(mwl_fs0()!=head)return fail("mission_world original left FS:0 changed");
    for(unsigned j=0;j<calls;++j)++mwl_paths[expected[j].id];
    if(!mwl_record_va)return fail("mission_world original linked no record");
    const uint32_t frame=mwl_record_va+0x54u;
    memcpy(frame_want,mwl_host(frame-0xe54u),0xe54u);
    memcpy(mwl_arena,mwl_before,mwl_span);mwl_globals(before,1);mwl_reset();
    got[2]=~want[2];
    uint64_t pair=mwl_core(r,frame,&got[2]);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);mwl_globals(actual,0);
    if(mwl_fs0()!=head)return fail("mission_world core left FS:0 changed");
    if(want[0]!=got[0]||want[1]!=got[1]||(check_ecx&&want[2]!=got[2])||calls!=mwl_calls||memcmp(expected,mwl_log,calls*sizeof(*expected))||
       memcmp(mwl_after,mwl_arena,mwl_span)||mwl_globals_differ(after,actual))
        return mwl_report("core",n,want,got,calls,expected);
    /* with a zero stack pattern the whole frame: every local the original
     * writes, dead stores included */
    if(!fill&&memcmp(frame_want,mwl_host(frame-0xe54u),0xe54u)){
        for(unsigned j=0;j<0xe54u;++j)if(frame_want[j]!=mwl_host(frame-0xe54u)[j]){
            fprintf(stderr,"FAIL: world_load case %u frame f-%x %02x/%02x\n",n,0xe54u-j,frame_want[j],mwl_host(frame-0xe54u)[j]);break;}
        return 1;
    }
    if(!exact){mwl_globals(before,1);return 0;}
    /* the adapter (the framework's entry: no argument slots) */
    memcpy(mwl_arena,mwl_before,mwl_span);mwl_globals(before,1);mwl_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[40];
    if(count>40)return fail("mission_world counter table too long");
    for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair=x->replacement_context(a,d->user,r);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);got[2]=want[2];
    mwl_globals(actual,0);
    if(mwl_fs0()!=head)return fail("mission_world adapter left FS:0 changed");
    mwl_mask_log(expected,calls);mwl_mask_log(mwl_log,mwl_calls);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=mwl_expected_hits(c+i)){
        fprintf(stderr,"FAIL: world_load case %u counter %s delta %llu, expected %u\n",n,c[i].name,
            (unsigned long long)(c[i].count-counts[i]),mwl_expected_hits(c+i));
        return 1;
    }
    if(want[0]!=got[0]||want[1]!=got[1]||calls!=mwl_calls||memcmp(expected,mwl_log,calls*sizeof(*expected))||
       memcmp(mwl_after,mwl_arena,mwl_span)||mwl_globals_differ(after,actual))
        return mwl_report("adapter",n,want,got,calls,expected);
    mwl_globals(before,1);
    return 0;
}
static int mwl_installed_probes(unsigned states){
    const unsigned index=ES2_HOOK_INDEX_world_load;
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    static const uint32_t a[6]={0};
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t r[3],regs[3],fill,want[3]={0},got[3]={0},pattern=poison?0xa7a7a7a7u:0x31313131u;
        /* patterned: the script has groups (without them the original would
         * read the poison as group-0 slots, outside the contract) */
        mwl_build(n*37u+11u,r,regs,&fill,1,0,1);
        const uint32_t entry[3]={pattern,pattern,pattern};const uint32_t head=mwl_fs0();
        BYTE before[MWL_GLOBALS],after[MWL_GLOBALS],actual[MWL_GLOBALS],expected_fpu[108],actual_fpu[108];static mwl_record expected[MWL_MAX];
        memcpy(mwl_before,mwl_arena,mwl_span);mwl_globals(before,0);
        mwl_reset();mwl_invoke(mapped+d->rva,a,entry,regs,pattern,want);es2_hook_fpu_save(expected_fpu);
        const unsigned calls=mwl_calls;memcpy(expected,mwl_log,calls*sizeof(*expected));memcpy(mwl_after,mwl_arena,mwl_span);mwl_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("mission_world poison install");
        memcpy(mwl_arena,mwl_before,mwl_span);mwl_globals(before,1);mwl_reset();mwl_invoke(mapped+d->rva,a,entry,regs,pattern,got);
        es2_hook_fpu_save(actual_fpu);mwl_globals(actual,0);
        mwl_mask_log(expected,calls);mwl_mask_log(mwl_log,mwl_calls);
        if(mwl_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||calls!=mwl_calls||memcmp(expected,mwl_log,calls*sizeof(*expected))||
           memcmp(mwl_after,mwl_arena,mwl_span)||mwl_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||
           memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=mwl_report(fast?"installed fast":"installed preserving",n,want,got,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("mission_world poison uninstall");mwl_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, pair/trace/arena/globals/FS:0\n",d->name,total);
    return status;
}
/* ---- real callees ---- */
enum { MWL_NESTED=14 };
static int mwl_real_pass(void){
    const unsigned index=ES2_HOOK_INDEX_world_load;
    static const unsigned nested[MWL_NESTED]={ES2_HOOK_INDEX_region_init,ES2_HOOK_INDEX_timer_init,ES2_HOOK_INDEX_group_array,
        ES2_HOOK_INDEX_mech_type_select,ES2_HOOK_INDEX_flyer_type_select,ES2_HOOK_INDEX_gun_slot_base,ES2_HOOK_INDEX_col_slot_base,
        ES2_HOOK_INDEX_dmg_slot_base,ES2_HOOK_INDEX_pool_create,ES2_HOOK_INDEX_base_pools,ES2_HOOK_INDEX_actor_list_init,
        ES2_HOOK_INDEX_gun_slot_table,ES2_HOOK_INDEX_dmg_slot_table,ES2_HOOK_INDEX_col_slot_table};
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[MWL_NESTED+1]={0};
    BYTE saved[MWL_IDS][5];int active[MWL_IDS];
    if(mwl_patch(saved,active,MWL_REAL,1))return 1;
    static mwl_record expected[MWL_MAX];static const uint32_t a[6]={0};
    for(unsigned n=0;n<4096u&&!status;++n){
        const uint32_t mode=random32();
        uint32_t r[3],regs[3],fill,want[3]={0},got[3]={0},pattern=n&1?0xa7a7a7a7u:0x31313131u;
        mwl_build(n*13u+5u,r,regs,&fill,1,1,1);
        const uint32_t entry[3]={pattern,pattern,pattern};const uint32_t head=mwl_fs0();
        BYTE before[MWL_GLOBALS],after[MWL_GLOBALS],actual[MWL_GLOBALS];
        memcpy(mwl_before,mwl_arena,mwl_span);mwl_globals(before,0);
        es2_hook hk[MWL_NESTED+1];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<MWL_NESTED;++j){o.mode=(mode>>(1+j)&1)?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,function_hook_extension(nested[j])))return fail("mission_world nested install");}
        mwl_reset();mwl_invoke(mapped+d->rva,a,entry,regs,pattern,want);
        const unsigned calls=mwl_calls;memcpy(expected,mwl_log,calls*sizeof(*expected));
        memcpy(mwl_after,mwl_arena,mwl_span);mwl_globals(after,0);
        o.mode=mode&1?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(hk+MWL_NESTED,d,&o,function_hook_extension(index)))return fail("mission_world caller install");
        memcpy(mwl_arena,mwl_before,mwl_span);mwl_globals(before,1);mwl_reset();
        mwl_invoke(mapped+d->rva,a,entry,regs,pattern,got);mwl_globals(actual,0);
        mwl_mask_log(expected,calls);mwl_mask_log(mwl_log,mwl_calls);
        if(mwl_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||calls!=mwl_calls||memcmp(expected,mwl_log,calls*sizeof(*expected))||
           memcmp(mwl_after,mwl_arena,mwl_span)||mwl_globals_differ(after,actual)){
            fprintf(stderr,"real mode=%08x\n",mode);status=mwl_report("real",n,want,got,calls,expected);
        }
        for(unsigned j=0;j<=MWL_NESTED;++j){if(!hk[j].descriptor)continue;entered[j]+=hk[j].calls;
            if(hk[j].mismatches)status=fail("mission_world real compare mismatch");
            if(!es2_hook_uninstall(hk+j))return fail("mission_world real uninstall");}
        mwl_globals(before,1);++total;
    }
    if(mwl_patch(saved,active,0,0))return 1;
    mwl_real=0;
    for(unsigned j=0;j<MWL_NESTED&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&!entered[MWL_NESTED]){fprintf(stderr,"FAIL: %s never entered\n",d->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, real 49630c/49667c, %u nested hooks original/replaced, hook original/replaced, "
        "31/a7 stack, pair, calls, arena, globals and FS:0\n",d->name,total,MWL_NESTED);
        for(unsigned j=0;j<MWL_NESTED;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
/* ---- the leaves ---- */
/* The case: random entry registers, the slot words (0 one time in four,
 * small or negative otherwise), the table pointers; actor_list_init's list
 * in the arena and a random count dword. */
static void mwl_leaf_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    mwl_seed=random32();mwl_stream=mwl_seed^n;mwl_real=0;mwl_effects=0;
    for(unsigned i=0;i<3;++i)r[i]=mwl_value();
    mwl_fill(mwl_arena,0x800);   /* the leaves touch only the list and the first blocks */
    for(unsigned i=0;i<sizeof(mwl_ranges)/sizeof(mwl_ranges[0]);++i)mwl_fill(mapped+mwl_ranges[i].rva,mwl_ranges[i].size);
    static const uint32_t words[3]={0xa98e8,0x98856,0x987dc};
    for(unsigned k=0;k<3;++k){const unsigned p=mwl_next()%4;put16(mapped+words[k],(uint16_t)(p==0?0:p==1?0x8000u|mwl_next():mwl_next()%400));}
    for(unsigned k=0;k<6;++k)a[k]=mwl_value();
    if(index==ES2_HOOK_INDEX_actor_list_init){a[0]=MWL_A(MWL_LIST+(mwl_next()%8));a[1]=mwl_next()%4==0?mwl_value():mwl_next();}
    mwl_stream=mwl_seed;
}
static uint64_t mwl_leaf_core(unsigned index,const uint32_t *a,const uint32_t *r,uint32_t *ecx){
    es2_weapon_graph g=weapons_graph(mapped,NULL,index);const es2_guest_memory *m=&math_native_memory;
    if(index==ES2_HOOK_INDEX_actor_list_init)return es2_actor_list_init_registers(m,&g,a[0],a[1],r,ecx);
    if(index==ES2_HOOK_INDEX_gun_slot_table)return es2_gun_slot_table_registers(m,&g,r,ecx);
    if(index==ES2_HOOK_INDEX_dmg_slot_table)return es2_dmg_slot_table_registers(m,&g,r,ecx);
    return es2_col_slot_table_registers(m,&g,r,ecx);
}
static int mwl_leaf_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r,uint32_t fill){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[MWL_GLOBALS],after[MWL_GLOBALS],actual[MWL_GLOBALS];mwl_record expected[4];uint32_t want[3],got[3];
    const uint32_t regs[3]={0,0,0};
    memcpy(mwl_before,mwl_arena,mwl_span);mwl_globals(before,0);
    mwl_reset();mwl_invoke(mapped+d->rva,a,r,regs,fill,want);const unsigned calls=mwl_calls;
    if(calls>4)return fail("mission_world leaf call count");
    memcpy(expected,mwl_log,calls*sizeof(*expected));memcpy(mwl_after,mwl_arena,mwl_span);mwl_globals(after,0);
    for(unsigned j=0;j<calls;++j)++mwl_paths[expected[j].id];
    /* the core with the ECX it reports */
    memcpy(mwl_arena,mwl_before,mwl_span);mwl_globals(before,1);mwl_reset();
    got[2]=~want[2];
    uint64_t pair=mwl_leaf_core(index,a,r,&got[2]);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);mwl_globals(actual,0);
    /* With the hook installed (integration pass) the "original" run is the
       hook, whose ECX is the framework's; ECX is compared otherwise. */
    const int ecx_known=!rng_hooks[index].installed;
    if(want[0]!=got[0]||want[1]!=got[1]||(ecx_known&&want[2]!=got[2])||calls!=mwl_calls||memcmp(expected,mwl_log,calls*sizeof(*expected))||
       memcmp(mwl_after,mwl_arena,mwl_span)||mwl_globals_differ(after,actual))
        return mwl_report("core",n,want,got,calls,expected);
    /* the adapter (six argument slots) with its callee counter delta */
    memcpy(mwl_arena,mwl_before,mwl_span);mwl_globals(before,1);mwl_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);const uint64_t count0=count?c[0].count:0;
    pair=x->replacement_context(a,d->user,r);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);got[2]=want[2];mwl_globals(actual,0);
    if(count!=1||c[0].count-count0!=calls)return fail("mission_world leaf callee counter delta");
    if(want[0]!=got[0]||want[1]!=got[1]||calls!=mwl_calls||memcmp(expected,mwl_log,calls*sizeof(*expected))||
       memcmp(mwl_after,mwl_arena,mwl_span)||mwl_globals_differ(after,actual))
        return mwl_report("adapter",n,want,got,calls,expected);
    mwl_globals(before,1);
    return 0;
}
static int mwl_leaf_installed(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],want[2]={0},got[2]={0},pattern=poison?0xa7a7a7a7u:0x31313131u;mwl_leaf_build(index,n*37u+11u,a,r);
        BYTE before[MWL_GLOBALS],after[MWL_GLOBALS],actual[MWL_GLOBALS],expected_fpu[108],actual_fpu[108];mwl_record expected[4];
        memcpy(mwl_before,mwl_arena,mwl_span);mwl_globals(before,0);
        mwl_reset();sg_poison_call(mapped+d->rva,a,pattern,want);es2_hook_fpu_save(expected_fpu);
        const unsigned calls=mwl_calls;if(calls>4)return fail("mission_world leaf call count");
        memcpy(expected,mwl_log,calls*sizeof(*expected));memcpy(mwl_after,mwl_arena,mwl_span);mwl_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("mission_world leaf install");
        memcpy(mwl_arena,mwl_before,mwl_span);mwl_globals(before,1);mwl_reset();sg_poison_call(mapped+d->rva,a,pattern,got);
        es2_hook_fpu_save(actual_fpu);mwl_globals(actual,0);
        if(want[0]!=got[0]||want[1]!=got[1]||calls!=mwl_calls||memcmp(expected,mwl_log,calls*sizeof(*expected))||
           memcmp(mwl_after,mwl_arena,mwl_span)||mwl_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||
           memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1){
            const uint32_t w3[3]={want[0],want[1],0},g3[3]={got[0],got[1],0};
            status=mwl_report(fast?"installed fast":"installed preserving",n,w3,g3,calls,expected);
        }
        if(!es2_hook_uninstall(&h))return fail("mission_world leaf uninstall");mwl_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, pair/trace/arena/globals\n",d->name,total);
    return status;
}
static int mwl_leaf_cases(unsigned index,unsigned limit){
    BYTE saved[MWL_IDS][5],old_globals[MWL_GLOBALS];int active[MWL_IDS];
    mwl_span=0x800;
    mwl_globals(old_globals,0);
    if(mwl_patch(saved,active,MWL_LEAF,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_MISSION_WORLD_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);memset(mwl_paths,0,sizeof(mwl_paths));
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];mwl_leaf_build(index,n,a,r);const uint32_t fill=n%3==0?0:mwl_next();
        status=mwl_leaf_compare(index,n,a,r,fill);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    mwl_globals(old_globals,1);
    if(!limit&&!status)status=mwl_leaf_installed(index,64);
    if(mwl_patch(saved,active,MWL_LEAF,0))return 1;
    mwl_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases (new[] %u); full return pair and ECX, the call with its slot and entry registers, "
        "arena/globals\n",function_hook_descriptor(index)->name,cases,mwl_paths[8]);
    return status;
}
static int mission_world_cases(unsigned index,unsigned limit){
    current_function=function_hook_descriptor(index)->name;
    if(index!=ES2_HOOK_INDEX_world_load)return mwl_leaf_cases(index,limit);
    mwl_span=MWL_SIZE;
    BYTE saved[MWL_IDS][5],old_globals[MWL_GLOBALS];int active[MWL_IDS];
    mwl_globals(old_globals,0);
    if(mwl_patch(saved,active,MWL_ALL,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_MISSION_WORLD_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);memset(mwl_paths,0,sizeof(mwl_paths));
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t r[3],regs[3],fill;const int exact=limit||!(n&1);
        mwl_build(n,r,regs,&fill,exact,0,0);status=mwl_compare(n,r,regs,fill,exact&&!limit,!limit);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    mwl_globals(old_globals,1);
    if(!limit&&!status)status=mwl_installed_probes(64);
    if(mwl_patch(saved,active,MWL_ALL,0))return 1;
    mwl_globals(old_globals,1);
    if(!limit&&!status)status=mwl_real_pass();
    mwl_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases (stub calls memset %u, opens %u, reports %u, reads %u/%u/%u, new[] %u, strlen %u, "
        "49667c %u, region_init %u, timer_init %u, group_array %u, mech_type_select %u, flyer_type_select %u, 405ebc %u, "
        "new %u, pool_create %u); full return pair and ECX, ordered calls with entry registers, frame offsets, slots and "
        "context word, the frame (zero stack pattern), arena/globals/FS:0\n",
        function_hook_descriptor(index)->name,cases,mwl_paths[1],mwl_paths[3],mwl_paths[4],mwl_paths[5],mwl_paths[6],mwl_paths[31],
        mwl_paths[8],mwl_paths[11],mwl_paths[14],mwl_paths[15],mwl_paths[16],mwl_paths[17],mwl_paths[18],mwl_paths[19],
        mwl_paths[20],mwl_paths[21],mwl_paths[22]);
    return status;
}
