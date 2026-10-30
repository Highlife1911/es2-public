/* mission_weapons group: weapon_fit 4175dc, player_weapons 4104ec and
 * weapon_factory 40fff8 (docs/reverse-engineering/mission_load.md, "L4 weapon
 * fit").
 *
 * Direct cases run the original body (seven argument slots, EAX/EDX/ECX and
 * EBX at entry from the case, over a stack filled with a case pattern: the
 * factory's projectile local [EBP-58h] is that residue until a record with
 * a selector other than 21h defines it) and the core (the same EBX, the same
 * residue in its scratch, with the ECX it reports) against logging stubs
 * for every game callee of the hook under test: 49630c (a record stub: the
 * real FS:0 head at record+0, context word +10h zeroed, the record linked
 * into FS:0), the mount table 420634, new 47749c, player_weapons and the
 * factory (stubs that install a scripted slot array), actor_animate,
 * mount_offset, 40e61c, the shape virtual +20h (an arena vtable entry),
 * 417530, the part helpers 40cec0/40d240/40d128 (they log the frame words
 * their pointer arguments name), 40cb84 (writes the three frame buffers),
 * 40d284 (logs a hash of the entries it would read), 40fb2c,
 * shield_capacity, 413ac8, 417d08, new[] 4774f0, the arena calls
 * 474bbc/474be4, weapon_record, def_lookup, proj_record, the allocator
 * 47a2c8, the seven weapon constructors, 49667c and fire_group. Frame
 * addresses that weapon_fit passes (f-30h ... f-12Ch) are logged relative to
 * its record (FS:0 + 28h), so the original's stack frame and the core's
 * scratch frame compare equal. Compared: the full return pair, ECX, FS:0,
 * the ordered calls with argument slots (and the bridge's slot count), entry
 * EAX/EDX/ECX and the record's context word, the arena and the image
 * globals (the construction counter pointer 6cbee4, the heap 6c5830). The
 * stubs change what the originals read after a call (the weapon system
 * pointer after actor_animate, a weapon's +10h after mount_offset, a frame
 * word after 40cec0, a record's selector after weapon_record, the counter
 * and its pointer after the constructors), and objects overlap (a record's
 * [C+8] inside the record or the part table, the condition words over the
 * part table, the factory's slot array over the item words, a fire group
 * array over the system's own fields), so the order of reads and writes is
 * observable. In even cases (and in the integration pass) the inputs are
 * those of the real callers (EBX = the actor, the seventh slot from the
 * player byte, residue 0) and the adapter is compared as well.
 *
 * Then installed 31/a7 poison probes (data whose residue is never consumed)
 * and real-callee passes: the real 49630c, 49667c and its element
 * constructor 411892, the nested player_weapons and weapon_factory hooks in
 * rotating original/replaced modes (they refuse compare), new/new[]/47a2c8
 * as a zeroing bump allocator and constructors that set a weapon's mount,
 * record and +10h, the hook under test original or replaced against a run
 * without it. Where the factory runs original under a replaced caller its
 * EBX is the bridge's, so the slots and the ECX that carry EBX's high word
 * (weapon_record's argument, the constructors' sixth slot, ECX at def_lookup
 * and proj_record) are logged as their low word in these passes. Identifiers
 * use the prefix mw_/MW_. */
typedef struct mw_record { uint32_t id,slots,args[7],regs[3],context,extra[3],eax,edx; } mw_record;
enum {
    MW_SIZE=0x8000, MW_SMALL=0x3100, MW_MAX=240, MW_IDS=35,
    MW_ACTOR=0x100, MW_X=0x600, MW_Y=0x640, MW_VT=0x680, MW_PARTPTR=0x6c0, MW_VRET=0x700,
    MW_TABLE=0x800, MW_COND=0xb00, MW_ITEMS=0xc40, MW_SECOND=0xd40, MW_M=0xe00, MW_MOUNTS=0xe40,
    MW_SYS=0x1000, MW_SYS2=0x1080, MW_SLOTS=0x1100, MW_SLOTS2=0x1140, MW_SLOTS3=0x1180, MW_W=0x1200,
    MW_REC=0x1900, MW_Q=0x2800, MW_OBJ=0x2900, MW_FG=0x2f00, MW_COUNTER=0x3000, MW_COUNTER2=0x3010,
    MW_HEAP=0x3050, MW_S=0x3080, MW_BUMP=0x4000,
    MW_RECORDS=40, MW_WEAPONS=12, MW_MOUNT_COUNT=12, MW_GLOBALS=8
};
#define MW_B ((uint32_t)(uintptr_t)mw_arena)
#define MW_A(o) (MW_B+(uint32_t)(o))
#define MW_BIT(i) ((uint64_t)1<<(i))
static BYTE mw_arena[MW_SIZE],mw_before[MW_SIZE],mw_after[MW_SIZE];
static mw_record mw_log[MW_MAX];
static unsigned mw_calls,mw_span=MW_SMALL,mw_real,mw_effects,mw_index,mw_paths[MW_IDS];
static uint32_t mw_seed,mw_stream,mw_bump,mw_count1,mw_count2,mw_head;
__attribute__((used)) static uint32_t mw_ecx;
/* 0 49630c, 1 420634, 2 new 47749c, 3 player_weapons 4104ec, 4 the
 * factory 40fff8, 5 actor_animate 402604, 6 mount_offset 40f540, 7 40e61c,
 * 8 the shape virtual +20h (arena vtable), 9 417530, 10 40cec0, 11 40d240,
 * 12 40d128, 13 40cb84, 14 40d284, 15 40fb2c, 16 shield_capacity 417bec,
 * 17 413ac8, 18 417d08, 19 new[] 4774f0, 20 474bbc, 21 weapon_record
 * 40fe84, 22 def_lookup 40ffc8, 23 proj_record 40ffb0, 24 47a2c8, 25..31
 * the constructors 40e074, 40e140, 40e274, 40e308, 40e344, 40e2bc, 40e380,
 * 32 474be4, 33 49667c, 34 fire_group 410ae4; the slots each original call
 * site pushes. */
static const uint32_t mw_addresses[MW_IDS]={0x49630c,0x420634,0x47749c,0x4104ec,0x40fff8,0x402604,0x40f540,0x40e61c,0,
    0x417530,0x40cec0,0x40d240,0x40d128,0x40cb84,0x40d284,0x40fb2c,0x417bec,0x413ac8,0x417d08,0x4774f0,0x474bbc,
    0x40fe84,0x40ffc8,0x40ffb0,0x47a2c8,0x40e074,0x40e140,0x40e274,0x40e308,0x40e344,0x40e2bc,0x40e380,0x474be4,
    0x49667c,0x410ae4};
static const unsigned mw_counts[MW_IDS]={0,1,1,6,7,2,1,1,2,2,4,4,4,4,5,2,1,1,1,1,1,1,2,1,2,6,6,5,5,5,5,5,0,5,2};
static uint32_t mw_next(void){
    uint32_t z=mw_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *mw_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static uint32_t mw_fs0(void){uint32_t v;__asm__ volatile("movl %%fs:0,%0":"=r"(v));return v;}
static void mw_reset(void){mw_calls=0;mw_stream=mw_seed;mw_bump=MW_A(MW_BUMP);mw_head=mw_fs0();}
static void mw_random(unsigned offset,unsigned size){for(unsigned i=0;i<size;++i)mw_arena[offset+i]=(BYTE)mw_next();stack_clean_bytes(mw_arena+offset,size);}
static uint32_t mw_value(void){return stack_clean(mw_next());}
/* Frame addresses of weapon_fit (record at FS:0 = f-28h) as offsets below f.
 * The window lies in a different frame on each side, so every generated
 * word is kept out of the stack (mw_value, mw_random); mw_alias guards it. */
static uint32_t mw_alias_value,mw_alias_call;static int mw_alias_hit;
static uint32_t mw_frame_relative(uint32_t v){
    if(!mw_alias_hit&&stack_uncommitted(v)){mw_alias_hit=1;mw_alias_value=v;mw_alias_call=mw_calls-1u;}
    const uint32_t f=mw_fs0()+0x28u;
    return f-v<=0x140u?0xf0000000u|(f-v):v;
}
static uint32_t mw_hash(uint32_t base,unsigned bytes){
    uint32_t h=0x811c9dc5u;for(unsigned i=0;i<bytes;++i)h=(h^mw_host(base)[i])*0x01000193u;return h;
}
static uint32_t mw_alloc(uint32_t size){
    const uint32_t p=mw_bump;size=(size+15u)&~7u;
    if(p+size>MW_A(MW_SIZE)){fail("mission_weapons bump allocator overflow");ExitProcess(1);}
    memset(mw_host(p),0,size);mw_bump+=size;return p;
}
/* What the stubs answer and change. */
static uint32_t mw_answer(unsigned id,const uint32_t *a,uint32_t eax,uint32_t pick){
    switch(id){
    case 1:return MW_A(MW_M);
    case 2:if(mw_real)return mw_alloc(a[0]);return MW_A(MW_SYS);   /* a null system faults (PROVED) */
    case 3:case 4:   /* the weapon system: the scripted slot array */
        put32(mw_host(a[0]+4),MW_A(MW_SLOTS));put16(mw_host(a[0]+8),(uint16_t)mw_count1);
        if(mw_effects&&pick%4==0)put32(mw_host(get32(mapped+0x2cbee4)),mw_value());
        return a[0];
    case 5:if(mw_effects&&pick%4==0)put32(mw_arena+MW_ACTOR+0x202,MW_A(MW_SYS2));return eax;
    case 6:if(mw_effects&&pick%4==0)put32(mw_host(a[0]+0x10),pick&0x100?0:mw_value());return eax;
    case 8:return MW_A(MW_VRET+8u*(pick%8));
    case 10:if(mw_effects&&pick%4==0)put32(mw_host(a[1]-4),mw_value());return eax;   /* the frame word f-34h */
    case 13:{   /* 40cb84: n entries into the word, word and dword buffers */
        const uint32_t n=pick%30;
        for(uint32_t i=0;i<n;++i){put16(mw_host(a[1]+2*i),(uint16_t)mw_next());put16(mw_host(a[2]+2*i),(uint16_t)mw_next());put32(mw_host(a[3]+4*i),mw_value());}
        return n|(eax&0xffff0000u);
    }
    case 19:   /* new[]: the factory's slot array (sometimes over the item
                * words) or a fire group array (sometimes over the system's
                * own selection byte +1Dh and group 0) */
        if(mw_real)return mw_alloc(a[0]);
        if(mw_index==ES2_HOOK_INDEX_weapon_factory)return MW_A(pick%8==0?MW_ITEMS-8:MW_SLOTS3);
        return MW_A(pick%8==0?MW_SYS+0x10:MW_FG+0x40*(pick/8%3));
    case 20:if(mw_effects&&pick%4==0)put16(mw_arena+MW_ITEMS+2*(pick/4%12),(uint16_t)(mw_next()%0x24));return eax;
    case 21:{   /* weapon_record: the record of the ID (40 records) */
        const uint32_t rec=MW_A(MW_REC+0x60u*((uint16_t)a[0]%MW_RECORDS));
        if(mw_effects&&pick%4==0)put16(mw_host(rec+0x3e),(uint16_t)(pick&0x100?0x22:mw_next()%0x21));
        return rec;
    }
    case 24:if(mw_real)return mw_alloc(a[0]);return pick%5==0?0:MW_A(MW_OBJ+0x90u*(pick/5%10));
    case 25:case 26:case 27:case 28:case 29:case 30:case 31:
        if(mw_real){put32(mw_host(a[0]+0x10),pick%3?0:mw_value()|1);put32(mw_host(a[0]+0x18),a[2]);put32(mw_host(a[0]+0x1c),a[3]);}
        else if(mw_effects&&pick%4==0){if(pick&0x100)put32(mapped+0x2cbee4,MW_A(pick&0x200?MW_COUNTER2:MW_COUNTER));
            else put32(mw_host(get32(mapped+0x2cbee4)),mw_value());}
        return a[0];
    }
    return eax;
}
__attribute__((used,noinline)) static uint64_t __cdecl mw_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a,uint32_t ebp){
    (void)ebp;
    if(mw_calls==MW_MAX){fail("mission_weapons call overflow");ExitProcess(1);}
    mw_record *v=mw_log+mw_calls++;memset(v,0,sizeof(*v));v->id=id;
    v->slots=weapons_bridge_slots(a,mw_counts[id]);
    /* the frame words the part helpers' pointers name, read before the call's effects */
    if(id==10){v->extra[0]=get32(mw_host(a[1]));v->extra[1]=get32(mw_host(a[2]));}
    else if(id==11){v->extra[0]=get16(mw_host(a[1]));v->extra[1]=get32(mw_host(a[2]));}
    else if(id==12){v->extra[0]=get16(mw_host(a[1]));v->extra[1]=get16(mw_host(a[2]));}
    else if(id==14){const uint32_t n=(int16_t)a[1]<0?0:(int16_t)a[1]>29?29:(uint16_t)a[1];
        v->extra[0]=mw_hash(a[2],2*n);v->extra[1]=mw_hash(a[3],2*n);v->extra[2]=mw_hash(a[4],4*n);}
    for(unsigned k=0;k<mw_counts[id];++k)v->args[k]=a[k];
    for(unsigned j=0;j<3;++j)v->regs[j]=regs[j];
    if(id>=10&&id<=14){for(unsigned k=0;k<mw_counts[id];++k)v->args[k]=mw_frame_relative(v->args[k]);
        for(unsigned j=0;j<3;++j)v->regs[j]=mw_frame_relative(v->regs[j]);}
    if(mw_real){   /* EBX's high word (see the header) */
        if(id==21)v->args[0]&=0xffffu;
        if(id==25||id==26)v->args[5]&=0xffffu;
        if(id==22||id==23)v->regs[2]&=0xffffu;
    }
    if(id&&mw_fs0()!=mw_head)v->context=get16(mw_host(mw_fs0()+16u));   /* a hook's own record */
    uint32_t eax=mw_value(),edx=mw_value(),pick=mw_next();mw_ecx=mw_value();
    if(id==0)eax=mw_fs0();
    else eax=mw_answer(id,a,eax,pick);
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define MW_STUB(n) __attribute__((naked)) static void mw_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tpushl %ebp\n\tleal 20(%esp),%eax\n\tpushl %eax\n\tleal 8(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _mw_stub_impl\n\taddl $28,%esp\n\tmovl _mw_ecx,%ecx\n\tret\n\t");}
MW_STUB(1) MW_STUB(2) MW_STUB(3) MW_STUB(4) MW_STUB(5) MW_STUB(6) MW_STUB(7) MW_STUB(8) MW_STUB(9) MW_STUB(10)
MW_STUB(11) MW_STUB(12) MW_STUB(13) MW_STUB(14) MW_STUB(15) MW_STUB(16) MW_STUB(17) MW_STUB(18) MW_STUB(19) MW_STUB(20)
MW_STUB(21) MW_STUB(22) MW_STUB(23) MW_STUB(24) MW_STUB(25) MW_STUB(26) MW_STUB(27) MW_STUB(28) MW_STUB(29) MW_STUB(30)
MW_STUB(31) MW_STUB(32) MW_STUB(33) MW_STUB(34)
#undef MW_STUB
/* 49630c: logs like the other stubs (EDX and ECX come back unchanged, as
 * the original's do), then links a record at EBP + [descriptor+4] into
 * FS:0 with the saved head at record+0 and a zero context word. */
__attribute__((naked)) static void mw_stub_0(void){__asm__ volatile(
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tpushl %ebp\n\tleal 20(%esp),%eax\n\tpushl %eax\n\tleal 8(%esp),%eax\n\tpushl %eax\n\t"
    "pushl $0\n\tcalll _mw_stub_impl\n\taddl $16,%esp\n\t"
    "popl %ecx\n\tmovl 4(%ecx),%ecx\n\taddl %ebp,%ecx\n\tmovl %eax,(%ecx)\n\tmovw $0,16(%ecx)\n\tmovl %ecx,%fs:0\n\t"
    "popl %edx\n\tpopl %ecx\n\tret\n\t");}
static void *mw_stubs[MW_IDS]={mw_stub_0,mw_stub_1,mw_stub_2,mw_stub_3,mw_stub_4,mw_stub_5,mw_stub_6,mw_stub_7,mw_stub_8,
    mw_stub_9,mw_stub_10,mw_stub_11,mw_stub_12,mw_stub_13,mw_stub_14,mw_stub_15,mw_stub_16,mw_stub_17,mw_stub_18,mw_stub_19,
    mw_stub_20,mw_stub_21,mw_stub_22,mw_stub_23,mw_stub_24,mw_stub_25,mw_stub_26,mw_stub_27,mw_stub_28,mw_stub_29,mw_stub_30,
    mw_stub_31,mw_stub_32,mw_stub_33,mw_stub_34};
static int mw_patch(BYTE saved[MW_IDS][5],int active[MW_IDS],uint64_t which,int on){
    for(unsigned i=0;i<MW_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!mw_addresses[i])continue;if(actor_patch(mw_addresses[i]-0x400000u,mw_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(mw_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
#define MW_RANGE(lo,hi) ((((uint64_t)1<<((hi)+1))-1)&~(((uint64_t)1<<(lo))-1))
/* Direct callees of each hook (bit i = mw_addresses[i]; 8 is never patched). */
static uint64_t mw_which(unsigned index){
    if(index==ES2_HOOK_INDEX_weapon_fit)return MW_RANGE(0,18)&~MW_BIT(8);
    if(index==ES2_HOOK_INDEX_player_weapons)return MW_BIT(4)|MW_BIT(19)|MW_BIT(33)|MW_BIT(34);
    return MW_BIT(0)|MW_RANGE(19,32);
}
/* Callees stubbed in the real-callee pass (49630c, 49667c and the nested
 * hooks run for real). */
static uint64_t mw_real_which(unsigned index){
    if(index==ES2_HOOK_INDEX_weapon_fit)return (MW_RANGE(1,34)&~MW_BIT(3)&~MW_BIT(4)&~MW_BIT(8)&~MW_BIT(33));
    if(index==ES2_HOOK_INDEX_player_weapons)return MW_RANGE(19,32)|MW_BIT(34);
    return MW_RANGE(19,32);
}
/* The construction counter pointer and the heap pointer. */
static const struct { uint32_t rva,size; } mw_ranges[]={{0x2cbee4,4},{0x2c5830,4}};
static void mw_globals(BYTE *out,int restore){
    for(unsigned i=0;i<sizeof(mw_ranges)/sizeof(mw_ranges[0]);++i){
        if(restore)memcpy(mapped+mw_ranges[i].rva,out,mw_ranges[i].size);else memcpy(out,mapped+mw_ranges[i].rva,mw_ranges[i].size);
        out+=mw_ranges[i].size;
    }
}
static int mw_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<MW_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
/* The dispatch class of an item ID (the byte table at 4100fd; 0 above 20h). */
static unsigned mw_class(uint32_t id){return id<=0x20u?mapped[0x100fd+id]:0;}
/* An item word: no weapon (0 or negative), an ID 1..20h, above 20h, or (not
 * in exact runs) anything. */
static uint16_t mw_item(int exact){
    const unsigned k=mw_next()%16;
    if(k==0)return 0;
    if(k==1)return (uint16_t)(0x8000u|mw_next());
    if(k==2)return (uint16_t)(0x21u+mw_next()%7);
    if(k==3&&!exact)return (uint16_t)mw_next();
    return (uint16_t)(1u+mw_next()%0x20);
}
/* The case data. exact: the real callers' inputs and data whose residue is
 * never consumed (a selector 21h only for IDs whose class does not pass the
 * projectile record, IDs below 40, so every ID has its own record). */
static void mw_build(unsigned index,unsigned n,uint32_t a[7],uint32_t r[3],uint32_t *ebx,uint32_t *fill,int exact,int real){
    mw_seed=random32();mw_stream=mw_seed^n;mw_real=real;mw_effects=!real&&!exact;mw_index=index;
    for(unsigned i=0;i<3;++i)r[i]=mw_value();
    mw_span=real?MW_SIZE:MW_SMALL;
    mw_random(0,MW_SIZE);
    BYTE *A=mw_arena+MW_ACTOR;
    /* the actor: player byte, the shape object chain to the arena vtable,
     * the part table pointer, the condition words (sometimes over the part
     * table), S (only passed) */
    A[0xa3]=(BYTE)(mw_next()%2?0:1+mw_next()%255);
    put32(A+0x34,MW_A(MW_X));put32(mw_arena+MW_X+4,MW_A(MW_Y));put32(mw_arena+MW_Y,MW_A(MW_VT));
    put32(mw_arena+MW_VT+0x20,(uint32_t)(uintptr_t)mw_stub_8);
    put32(A+0x1f6,MW_A(MW_S));put32(A+0x1fa,MW_A(MW_PARTPTR));put32(mw_arena+MW_PARTPTR,MW_A(MW_TABLE));
    put32(A+0x20e,mw_next()%4?MW_A(MW_COND):MW_A(MW_TABLE+2u*(mw_next()%300)));
    for(unsigned row=0;row<40;++row)if(mw_next()%2)put16(mw_arena+MW_TABLE+18*row+6,0);
    /* mounts: the index byte +17h in -19..20 (the part row +13h, the item
     * word) */
    for(unsigned k=0;k<MW_MOUNT_COUNT;++k)mw_arena[MW_MOUNTS+0x1a*k+0x17]=(BYTE)(int8_t)((int)(mw_next()%40)-19);
    for(unsigned i=0;i<40;++i){put16(mw_arena+MW_ITEMS-40+2*i,mw_item(exact||real));put16(mw_arena+MW_SECOND-40+2*i,(uint16_t)(mw_next()%3?5:mw_next()%40));}
    /* records: the selector +3Eh, the ID word +56h, [C+8] (Q, inside the
     * record, or inside the part table) */
    for(unsigned i=0;i<MW_RECORDS;++i){
        BYTE *C=mw_arena+MW_REC+0x60*i;const unsigned k=mw_next()%6;
        const int quiet=mw_class(i)<=4||mw_class(i)==9;
        uint16_t sel=k==0&&((!exact&&!real)||quiet)?0x21:k==1?0x22:(uint16_t)(mw_next()%0x21);
        put16(C+0x3e,sel);
        static const uint16_t ids[]={0x12,0x1d,0x1e,0x1f,0x20};
        put16(C+0x56,mw_next()%2?ids[mw_next()%5]:(uint16_t)(mw_next()%40));
        const unsigned q=mw_next()%3;
        put32(C+8,q==0?MW_A(MW_Q+8u*(i%16)):q==1?MW_A(MW_REC+0x60u*i+0x10):MW_A(MW_TABLE+2u*(mw_next()%300)));
    }
    /* weapons and slot arrays */
    for(unsigned k=0;k<MW_WEAPONS;++k){
        BYTE *W=mw_arena+MW_W+0x90*k;
        put32(W+0x10,mw_next()%2?0:mw_value()|1);
        put32(W+0x18,MW_A(MW_MOUNTS+0x1au*(mw_next()%MW_MOUNT_COUNT)));
        put32(W+0x1c,MW_A(MW_REC+0x60u*(mw_next()%MW_RECORDS)));
    }
    mw_count1=mw_next()%9;mw_count2=mw_next()%9;
    for(unsigned i=0;i<9;++i){
        put32(mw_arena+MW_SLOTS+4*i,mw_next()%4?MW_A(MW_W+0x90u*(mw_next()%MW_WEAPONS)):0);
        put32(mw_arena+MW_SLOTS2+4*i,mw_next()%4?MW_A(MW_W+0x90u*(mw_next()%MW_WEAPONS)):0);
    }
    put32(mw_arena+MW_SYS2+4,MW_A(MW_SLOTS2));put16(mw_arena+MW_SYS2+8,(uint16_t)mw_count2);
    /* the mount table of 420634 (the real factory reads it) */
    const uint32_t mounts=mw_next()%9;
    put16(mw_arena+MW_M,(uint16_t)mounts);put32(mw_arena+MW_M+2,MW_A(MW_MOUNTS));
    put32(mapped+0x2cbee4,MW_A(MW_COUNTER));put32(mapped+0x2c5830,MW_A(MW_HEAP));
    memset(a,0,28);
    const uint32_t actor=MW_A(MW_ACTOR);
    if(index==ES2_HOOK_INDEX_weapon_fit){
        a[0]=actor;a[1]=MW_A(MW_ITEMS);a[2]=MW_A(MW_SECOND);
        if(!exact&&!real&&mw_next()%2){a[1]=mw_value();a[2]=mw_value();}   /* only passed on */
    } else {
        a[0]=MW_A(MW_SYS);a[1]=actor;a[3]=MW_A(MW_MOUNTS);a[4]=MW_A(MW_ITEMS);a[5]=MW_A(MW_SECOND);
        const uint32_t c=index==ES2_HOOK_INDEX_player_weapons&&!real&&mw_next()%16==0?0xffffu:mw_next()%9;
        a[2]=stack_clean((mw_next()%2?mw_next()<<16:0)|c);
        if(index==ES2_HOOK_INDEX_player_weapons&&real)A[0xa3]=1;
    }
    a[6]=A[0xa3]?1:0;
    *ebx=actor;*fill=0;
    if(!exact&&!real){
        if(index==ES2_HOOK_INDEX_weapon_factory){a[6]=mw_value();*ebx=mw_value();}
        *fill=mw_next()%2?mw_value():0;
    }
    mw_stream=mw_seed;   /* the stubs' stream restarts here in every run */
}
/* Calls target with seven argument slots, EAX/EDX/ECX = r and EBX = ebx,
 * over 3,840 bytes of stack filled with `fill`; out receives EAX, EDX and
 * ECX after the call. */
__attribute__((naked,noinline)) static void mw_invoke(void *target,const uint32_t *a,const uint32_t *r,uint32_t ebx,uint32_t fill,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 24(%ebp),%eax\n\tmovl $1024,%ecx\n\tcld\n\trep stosl\n\taddl $3840,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 24(%esi)\n\tpushl 20(%esi)\n\tpushl 16(%esi)\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%edi\n\tmovl 20(%ebp),%ebx\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tcall *8(%ebp)\n\t"
        "movl 28(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* The core with the adapter's graph and frame (the residue in the
 * factory's scratch); FS:0 restored to the record's saved head as the
 * adapters do. */
static uint64_t mw_core(unsigned index,const uint32_t *a,const uint32_t *r,uint32_t ebx,uint32_t residue,uint32_t *ecx){
    const es2_guest_memory *m=&math_native_memory;
    if(index==ES2_HOOK_INDEX_player_weapons){
        es2_weapon_graph g=weapons_graph(mapped,NULL,index);
        return es2_player_weapons_registers(m,&g,a[0],a[1],a[2],a[3],a[4],a[5],r,ecx);
    }
    if(index==ES2_HOOK_INDEX_weapon_fit){
        BYTE scratch[ES2_WEAPON_FIT_SCRATCH]={0};es2_weapon_graph g=weapons_graph(mapped,scratch,index);
        weapons_impact_context c={(uint32_t)(uintptr_t)mapped,(uint32_t)(uintptr_t)(scratch+ES2_WEAPON_FIT_FRAME)};
        g.call=weapons_impact_guest_call;g.context=&c;
        const uint64_t v=es2_weapon_fit_registers(m,&g,a[0],a[1],a[2],r,ecx);
        uint32_t head=(uint32_t)v;__asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");return v;
    }
    BYTE scratch[ES2_WEAPON_FACTORY_SCRATCH]={0};es2_weapon_graph g=weapons_graph(mapped,scratch,index);
    put32(scratch+ES2_WEAPON_FACTORY_FRAME-0x58,residue);
    weapons_impact_context c={(uint32_t)(uintptr_t)mapped,(uint32_t)(uintptr_t)(scratch+ES2_WEAPON_FACTORY_FRAME)};
    g.call=weapons_impact_guest_call;g.context=&c;
    uint32_t head=0;
    const uint64_t v=es2_weapon_factory_registers(m,&g,a[0],a[1],a[2],a[3],a[4],a[5],a[6],ebx,r,&head);
    __asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");
    if(ecx)*ecx=head;
    return v;
}
static int mw_report(const char *what,unsigned n,const uint32_t want[3],const uint32_t got[3],unsigned calls,const mw_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x calls %u/%u\n",current_function,n,what,
        want[0],got[0],want[1],got[1],want[2],got[2],calls,mw_calls);
    for(unsigned j=0;j<calls&&j<mw_calls&&expected;++j)if(memcmp(expected+j,mw_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(mw_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<mw_span;++j)if(mw_after[j]!=mw_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,mw_after[j],mw_arena[j]);break;}
    return 1;
}
static unsigned mw_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<mw_calls;++j){
        const unsigned id=mw_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?id==8&&c->key==0x20:id!=8&&mw_addresses[id]==c->key)++hits;
    }
    return hits;
}
static int mw_alias_report(const char *what,unsigned n){
    if(!mw_alias_hit)return 0;
    fprintf(stderr,"FAIL: %s case %u (%s) call %u: data word %08x lies in the worker stack reservation\n",current_function,n,what,mw_alias_call,mw_alias_value);
    return 1;
}
/* One direct case: the original, the core and (exact inputs) the adapter. */
static int mw_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r,uint32_t ebx,uint32_t fill,int exact,int check_ecx){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[MW_GLOBALS],after[MW_GLOBALS],actual[MW_GLOBALS];static mw_record expected[MW_MAX];uint32_t want[3],got[3];
    const uint32_t head=mw_fs0();mw_alias_hit=0;
    memcpy(mw_before,mw_arena,mw_span);mw_globals(before,0);
    mw_reset();mw_invoke(mapped+d->rva,a,r,ebx,fill,want);const unsigned calls=mw_calls;memcpy(expected,mw_log,calls*sizeof(*expected));
    memcpy(mw_after,mw_arena,mw_span);mw_globals(after,0);
    if(mw_fs0()!=head)return fail("mission_weapons original left FS:0 changed");
    for(unsigned j=0;j<calls;++j)++mw_paths[expected[j].id];
    /* the core with the ECX it reports */
    memcpy(mw_arena,mw_before,mw_span);mw_globals(before,1);mw_reset();
    got[2]=~want[2];
    uint64_t pair=mw_core(index,a,r,ebx,fill,&got[2]);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);mw_globals(actual,0);
    if(mw_fs0()!=head)return fail("mission_weapons core left FS:0 changed");
    if(mw_alias_report("core",n))return 1;
    if(want[0]!=got[0]||want[1]!=got[1]||(check_ecx&&want[2]!=got[2])||calls!=mw_calls||memcmp(expected,mw_log,calls*sizeof(*expected))||
       memcmp(mw_after,mw_arena,mw_span)||mw_globals_differ(after,actual))
        return mw_report("core",n,want,got,calls,expected);
    if(!exact){mw_globals(before,1);return 0;}
    /* the adapter (the framework's entry: six argument slots) */
    memcpy(mw_arena,mw_before,mw_span);mw_globals(before,1);mw_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[32];
    if(count>32)return fail("mission_weapons counter table too long");
    for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair=x->replacement_context(a,d->user,r);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);got[2]=want[2];
    mw_globals(actual,0);
    if(mw_fs0()!=head)return fail("mission_weapons adapter left FS:0 changed");
    if(mw_alias_report("adapter",n))return 1;
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=mw_expected_hits(c+i))return fail("mission_weapons callee counter delta");
    if(want[0]!=got[0]||want[1]!=got[1]||calls!=mw_calls||memcmp(expected,mw_log,calls*sizeof(*expected))||
       memcmp(mw_after,mw_arena,mw_span)||mw_globals_differ(after,actual))
        return mw_report("adapter",n,want,got,calls,expected);
    mw_globals(before,1);
    return 0;
}
static int mw_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[7],r[3],ebx,fill,want[3]={0},got[3]={0},pattern=poison?0xa7a7a7a7u:0x31313131u;
        mw_build(index,n*37u+11u,a,r,&ebx,&fill,1,0);
        const uint32_t regs[3]={pattern,pattern,pattern};const uint32_t head=mw_fs0();
        BYTE before[MW_GLOBALS],after[MW_GLOBALS],actual[MW_GLOBALS],expected_fpu[108],actual_fpu[108];static mw_record expected[MW_MAX];
        memcpy(mw_before,mw_arena,mw_span);mw_globals(before,0);
        mw_reset();mw_invoke(mapped+d->rva,a,regs,ebx,pattern,want);es2_hook_fpu_save(expected_fpu);
        const unsigned calls=mw_calls;memcpy(expected,mw_log,calls*sizeof(*expected));memcpy(mw_after,mw_arena,mw_span);mw_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("mission_weapons poison install");
        memcpy(mw_arena,mw_before,mw_span);mw_globals(before,1);mw_reset();mw_invoke(mapped+d->rva,a,regs,ebx,pattern,got);
        es2_hook_fpu_save(actual_fpu);mw_globals(actual,0);
        if(mw_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||calls!=mw_calls||memcmp(expected,mw_log,calls*sizeof(*expected))||
           memcmp(mw_after,mw_arena,mw_span)||mw_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||
           memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=mw_report(fast?"installed fast":"installed preserving",n,want,got,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("mission_weapons poison uninstall");mw_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, pair/trace/arena/globals/FS:0\n",d->name,total);
    return status;
}
/* ---- real callees ---- */
static unsigned mw_nested(unsigned index,unsigned nested[2]){
    if(index==ES2_HOOK_INDEX_weapon_fit){nested[0]=ES2_HOOK_INDEX_player_weapons;nested[1]=ES2_HOOK_INDEX_weapon_factory;return 2;}
    if(index==ES2_HOOK_INDEX_player_weapons){nested[0]=ES2_HOOK_INDEX_weapon_factory;return 1;}
    return 0;
}
static int mw_real_pass(unsigned index){
    unsigned nested[2];const unsigned count=mw_nested(index,nested);
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[3]={0};
    BYTE saved[MW_IDS][5];int active[MW_IDS];
    if(mw_patch(saved,active,mw_real_which(index),1))return 1;
    static mw_record expected[MW_MAX];
    const unsigned modes=2u<<count;
    for(unsigned mode=0;mode<modes&&!status;++mode)for(unsigned n=0;n<2048u/modes&&!status;++n){
        uint32_t a[7],r[3],ebx,fill,want[3]={0},got[3]={0},pattern=n&1?0xa7a7a7a7u:0x31313131u;
        mw_build(index,n*13u+mode,a,r,&ebx,&fill,1,1);
        const uint32_t regs[3]={pattern,pattern,pattern};const uint32_t head=mw_fs0();
        BYTE before[MW_GLOBALS],after[MW_GLOBALS],actual[MW_GLOBALS];
        memcpy(mw_before,mw_arena,mw_span);mw_globals(before,0);
        es2_hook hk[3];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(mode>>(1+j)&1)?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,function_hook_extension(nested[j])))return fail("mission_weapons nested install");}
        mw_reset();mw_invoke(mapped+d->rva,a,regs,ebx,pattern,want);
        const unsigned calls=mw_calls;memcpy(expected,mw_log,calls*sizeof(*expected));
        memcpy(mw_after,mw_arena,mw_span);mw_globals(after,0);
        o.mode=mode&1?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(hk+2,d,&o,function_hook_extension(index)))return fail("mission_weapons caller install");
        memcpy(mw_arena,mw_before,mw_span);mw_globals(before,1);mw_reset();
        mw_invoke(mapped+d->rva,a,regs,ebx,pattern,got);mw_globals(actual,0);
        if(mw_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||calls!=mw_calls||memcmp(expected,mw_log,calls*sizeof(*expected))||
           memcmp(mw_after,mw_arena,mw_span)||mw_globals_differ(after,actual)){
            fprintf(stderr,"real mode=%u\n",mode);status=mw_report("real",n,want,got,calls,expected);
        }
        for(unsigned j=0;j<3;++j){if(!hk[j].descriptor)continue;entered[j]+=hk[j].calls;
            if(hk[j].mismatches)status=fail("mission_weapons real compare mismatch");
            if(!es2_hook_uninstall(hk+j))return fail("mission_weapons real uninstall");}
        mw_globals(before,1);++total;
    }
    if(mw_patch(saved,active,0,0))return 1;
    mw_real=0;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&!entered[2]){fprintf(stderr,"FAIL: %s never entered\n",d->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, real 49630c/49667c/411892, %u nested hooks original/replaced, hook original/replaced, "
        "31/a7 stack, pair, calls, arena, globals and FS:0\n",d->name,total,count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
static int mission_weapons_cases(unsigned index,unsigned limit){
    current_function=function_hook_descriptor(index)->name;
    const uint64_t which=mw_which(index);
    BYTE saved[MW_IDS][5],old_globals[MW_GLOBALS];int active[MW_IDS];
    mw_globals(old_globals,0);
    if(mw_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_MISSION_WEAPONS_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);memset(mw_paths,0,sizeof(mw_paths));
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[7],r[3],ebx,fill;const int exact=limit||!(n&1);
        mw_build(index,n,a,r,&ebx,&fill,exact,0);status=mw_compare(index,n,a,r,ebx,fill,exact&&!limit,!limit);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    mw_globals(old_globals,1);
    if(!limit&&!status)status=mw_installed_probes(index,64);
    if(mw_patch(saved,active,which,0))return 1;
    mw_globals(old_globals,1);
    if(!limit&&!status)status=mw_real_pass(index);
    mw_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases (stub calls 49630c %u, new %u, system %u/%u, 40cec0 %u, 40cb84 %u, new[] %u, weapon_record %u, "
        "def_lookup %u, proj_record %u, 47a2c8 %u, constructors %u/%u/%u/%u/%u/%u/%u, 49667c %u, fire_group %u); "
        "full return pair and ECX, ordered calls with entry registers, slots, frame words and context word, arena/globals/FS:0\n",
        function_hook_descriptor(index)->name,cases,mw_paths[0],mw_paths[2],mw_paths[3],mw_paths[4],mw_paths[10],mw_paths[13],mw_paths[19],
        mw_paths[21],mw_paths[22],mw_paths[23],mw_paths[24],mw_paths[25],mw_paths[26],mw_paths[27],mw_paths[28],mw_paths[29],mw_paths[30],
        mw_paths[31],mw_paths[33],mw_paths[34]);
    return status;
}
