/* mission_actors group: actor_load 4253d8 (docs/reverse-engineering/
 * mission_load.md, "L6 actor load").
 *
 * Direct cases run the original body (EAX/EDX/ECX, EBX/ESI/EDI at entry from
 * the case, no argument slots, over a stack filled with a case pattern) and
 * the core against logging stubs for every game callee: 49630c (a record
 * stub: the real FS:0 head at record+0, context word +10h zeroed, the record
 * linked into FS:0), the script name 45ee00/492b90, the stream 47ad40 (it
 * stores an arena vtable whose slot +18h is a second read stub), the reads
 * 47af98 and the virtual +18h (each copies the requested bytes of a
 * synthetic script.dat built per case), 40f998, pool_alloc, mech_ctor,
 * terrain_height, weapon_fit, condition_init, action_copy, initial_damage,
 * 40fa88, flyer_ctor, building_factory, the building virtual +10h (an arena
 * vtable), 470ed4, new[] 4774f0, subject_resolve, heading_point, group_ctor,
 * base_marks, 47129c, pool_prev and 47ad78; memcpy 49680e is the harness's
 * own CW3220 memcpy. The core runs with its scratch frame at the
 * original's frame address (its own code on a static stack), so every frame
 * address a callee receives and every register that combines one with a
 * data word is logged and compared raw; the contents a callee reads from
 * the frame (the points of terrain_height and 470ed4, the building record,
 * the id/action words, the item/secondary words, the group record and its
 * order pointers) are logged as hashes. Compared: the full return pair,
 * ECX, the ordered calls with argument slots (and the bridge's slot count),
 * entry EAX/EDX/ECX and the record's context word, the arena, the image
 * globals (the statics, the script tables and counts, the selection, remap
 * and MEC tables, the pools, the player pointer, the counter pointer, the
 * string table) and FS:0. The
 * stubs change what the original reads afterwards (N and the MEC table after
 * 40f998, the player ordinal and the counter pointer after pool_alloc, the
 * coordinate table after mech_ctor, the type words, angle table and terrain
 * after terrain_height, the trigger table and MEC words after weapon_fit and
 * condition_init, the tables after flyer_ctor and building_factory, the
 * terrain after the shape word and 470ed4, the trigger count and types after
 * subject_resolve, the member count and array after group_ctor, the pool and
 * terrain after 47129c and pool_prev, the points terrain_height and 470ed4
 * received, and the order and objective counts at a chosen iteration, with
 * the script holding the switched number of records). In even cases the
 * adapter is compared as well (its frame lies elsewhere: stack addresses
 * masked in both logs), with the callee counter deltas.
 *
 * Then installed 31/a7 poison probes and a real-callee pass over the real
 * 49630c, pool_alloc (real MECH and FLYER pools), action_copy,
 * subject_resolve, pool_nth, pool_prev and pool_next (three real pools),
 * heading_point and vector_angle, the nested hooks in all original/replaced
 * combinations, the hook under test original or replaced against a run
 * without it (stack addresses masked). Identifiers use the prefix
 * mal_/MAL_. */
typedef struct mal_record { uint32_t id,slots,args[6],regs[3],context,extra[3],eax,edx; } mal_record;
enum {
    MAL_SIZE=0x6000, MAL_MAX=320, MAL_IDS=27, MAL_FILE=0x2000,
    MAL_VT=0x000,       /* the stream vtable (slot +18h: stub 5) */
    MAL_BVT=0x040,      /* the building vtable (slot +10h: stub 17) */
    MAL_COUNTER=0x080, MAL_COUNTER2=0x090,
    MAL_COORDS=0x100,   /* 34 points x 12 */
    MAL_ANGLES=0x2a0,   /* 36 words */
    MAL_ROUTES=0x2f0,   /* 8 routes x 6 */
    MAL_RPTS=0x330,     /* 8 x 2 point pointers */
    MAL_TRIGS=0x380,    /* 8 x 58 */
    MAL_GROUPS=0x560,   /* 5 x 122 */
    MAL_MEC=0x7e0,      /* 6 x 126 */
    MAL_ORDERS=0xae0,   /* 10 x 22, then the objectives' room */
    MAL_OBJS=0xc40,     /* 5 x 76 */
    MAL_MEMBERS=0xdc0,  /* 8 dwords */
    MAL_TYPES=0xe00,    /* 4 x 60h */
    MAL_MECHS=0xf80,    /* 4 x 2C0h */
    MAL_FLYERS=0x1a80,  /* 4 x 1C0h */
    MAL_BLDGS=0x2180,   /* 4 x 1C0h */
    MAL_POOLS=0x2880,   /* three pool headers (14h each) */
    MAL_PNODES=0x2900   /* real passes: MECH 10 x 372h, FLYER 4 x 2A0h, buildings 4 x 208h (to 5E14h) */
};
#define MAL_B ((uint32_t)(uintptr_t)mal_arena)
#define MAL_A(o) (MAL_B+(uint32_t)(o))
#define MAL_BIT(i) ((uint32_t)1<<(i))
#define MAL_MECH_POOL MAL_A(MAL_POOLS)
#define MAL_FLYER_POOL MAL_A(MAL_POOLS+0x14)
#define MAL_BUILDING_POOL MAL_A(MAL_POOLS+0x28)
static BYTE mal_arena[MAL_SIZE],mal_before[MAL_SIZE],mal_after[MAL_SIZE];
static BYTE mal_file[MAL_FILE];
static mal_record mal_log[MAL_MAX];
static unsigned mal_calls,mal_real,mal_effects,mal_paths[MAL_IDS],mal_file_size,mal_pos,mal_new_count,mal_bcount;
/* The count switches: the subject_resolve call of order (objective)
 * iteration *_switch rewrites the count word 4A9E2Ch (4A9E3Ch), which the
 * loop rereads, to *_final; the script holds *_final records. */
static unsigned mal_phase,mal_phase_calls,mal_orders_planned,mal_order_switch,mal_order_final,mal_objective_switch,mal_objective_final;
static uint32_t mal_seed,mal_stream,mal_head,mal_record_va,mal_buildings[4];
__attribute__((used)) static uint32_t mal_ecx;
/* 0 49630c, 1 45ee00, 2 492b90, 3 47ad40, 4 47af98, 5 the stream's virtual
 * read +18h (arena vtable), 6 40f998, 7 pool_alloc 471b30, 8 mech_ctor
 * 415bb0, 9 terrain_height 46e180, 10 weapon_fit 4175dc, 11 condition_init
 * 415068, 12 action_copy 411b90, 13 initial_damage 4178e8, 14 40fa88, 15
 * flyer_ctor 4215f4, 16 building_factory 405314, 17 the building virtual
 * +10h (arena vtable), 18 470ed4, 19 new[] 4774f0, 20 subject_resolve
 * 425348, 21 heading_point 492934, 22 group_ctor 423b34, 23 base_marks
 * 405db0, 24 47129c, 25 pool_prev 471c70, 26 47ad78. The slots each
 * original call site pushes. */
static const uint32_t mal_addresses[MAL_IDS]={0x49630c,0x45ee00,0x492b90,0x47ad40,0x47af98,0,0x40f998,0x471b30,0x415bb0,
    0x46e180,0x4175dc,0x415068,0x411b90,0x4178e8,0x40fa88,0x4215f4,0x405314,0,0x470ed4,0x4774f0,0x425348,0x492934,0x423b34,
    0x405db0,0x47129c,0x471c70,0x47ad78};
static const unsigned mal_counts[MAL_IDS]={0,1,2,2,3,3,0,1,2,2,3,4,3,2,0,2,2,1,3,1,2,2,6,1,1,2,2};
static uint32_t mal_next(void){
    uint32_t z=mal_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
/* A random register value; bit 31 set, so it is never a stack address. */
static uint32_t mal_value(void){return mal_next()|0x80000000u;}
static BYTE *mal_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static uint32_t mal_fs0(void){uint32_t v;__asm__ volatile("movl %%fs:0,%0":"=r"(v));return v;}
static int mal_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
/* The masked form of a logged value: 0 for an address of this thread's
 * stack. The direct core cases compare raw values (the core's frame lies at
 * the original's addresses); the adapter, installed and real-callee runs
 * have their frames elsewhere and compare masked logs. */
static uint32_t mal_mask(uint32_t value){return mal_stack(value)?0:value;}
static uint32_t mal_hash(uint32_t a,unsigned n){
    uint32_t h=0x811c9dc5u;const BYTE *p=mal_host(a);
    for(unsigned i=0;i<n;++i)h=(h^p[i])*0x01000193u;
    return h;
}
static void mal_reset(void){mal_calls=0;mal_stream=mal_seed;mal_pos=0;mal_new_count=0;mal_head=mal_fs0();mal_record_va=0;mal_phase=0;mal_phase_calls=0;}
/* A random data byte that is never the top byte of an address of this
 * thread's stack (16 MiB below StackBase span at most two top bytes), so no
 * four data bytes read as a dword look like a stack address and a masked
 * comparison never hides a data value. */
static BYTE mal_byte(void){
    const uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;
    BYTE b=(BYTE)mal_next();
    if(b==(BYTE)((top-0x1000000u)>>24)||b==(BYTE)((top-1u)>>24))b^=0x80;
    return b;
}
static void mal_random(unsigned offset,unsigned size){for(unsigned i=0;i<size;++i)mal_arena[offset+i]=mal_byte();}
/* A read: the next `size` bytes of the case's script (0 past its end);
 * sizes of 0 or above 800h (negative counts) read nothing. */
static void mal_read(uint32_t size,uint32_t buffer){
    if(size-1u>=0x800u)return;
    BYTE *p=mal_host(buffer);
    for(uint32_t i=0;i<size;++i)p[i]=mal_pos<mal_file_size?mal_file[mal_pos++]:0;
}
/* What the stubs answer and change. */
static uint32_t mal_answer(unsigned id,const uint32_t *a,uint32_t eax,uint32_t pick){
    const int effect=mal_effects&&pick%4==0;
    switch(id){
    case 3:   /* 47ad40: the stream object's vtable */
        put32(mal_host(a[0]),MAL_A(MAL_VT));
        return a[0];
    case 4:case 5:
        mal_read(a[1],a[2]);
        return eax;
    case 6:   /* 40f998: N or the MEC table (the mech loop reads both) */
        if(effect){if(pick&0x100)put16(mapped+0xaaa5e,(uint16_t)(mal_next()%4));else put32(mapped+0xaaa60,MAL_A(MAL_MEC+126u*(mal_next()%2)));}
        return eax;
    case 7:   /* pool_alloc: a mech or flyer slot, never null (a null object faults, PROVED) */
        if(effect){if(pick&0x100)put16(mapped+0xaaa64,(uint16_t)(mal_next()%4));else put32(mapped+0x2cbee4,MAL_A(MAL_COUNTER2));}
        if(a[0]==MAL_FLYER_POOL)return MAL_A(MAL_FLYERS+0x1c0u*(mal_next()%4));
        return MAL_A(MAL_MECHS+0x2c0u*(mal_next()%4));
    case 8:   /* mech_ctor: the type record pointer A+1F2h, as the real one sets it */
        put32(mal_host(a[0]+0x1f2u),MAL_A(MAL_TYPES+0x60u*(mal_next()%4)));
        if(effect){if(pick&0x100)put32(mapped+0xa9e0c,MAL_A(MAL_COORDS+12u*(mal_next()%3)));else put32(mapped+0x2cbee4,MAL_A(pick&0x200?MAL_COUNTER2:MAL_COUNTER));}
        return a[0];
    case 9:   /* terrain_height: a height, sometimes -32768; the point it
               * received (the frame copy the original rereads), the type
               * words, the angle table or the terrain pointer */
        if(effect)switch(pick>>8&7){
        case 0:put16(mal_arena+MAL_TYPES+0x60u*(mal_next()%4)+0x16u,(uint16_t)mal_next());break;
        case 1:put16(mal_arena+MAL_TYPES+0x60u*(mal_next()%4)+0x50u,(uint16_t)(mal_next()%2));break;
        case 2:put32(mapped+0xa9e10,MAL_A(MAL_ANGLES+2u*(mal_next()%3)));break;
        case 3:put32(mapped+0xa0be0,mal_value());break;
        default:put32(mal_host(a[1]+4u*(mal_next()%3)),mal_value());break;
        }
        return pick%8==1?0xffff8000u:mal_value();
    case 10:  /* weapon_fit: the trigger table or a MEC word 0 */
        if(effect){if(pick&0x100)put32(mapped+0xa9e28,MAL_A(MAL_TRIGS+58u*(mal_next()%2)));else put16(mal_arena+MAL_MEC+126u*(mal_next()%5),(uint16_t)mal_next());}
        return eax;
    case 11:  /* condition_init: a MEC word 0 */
        if(effect)put16(mal_arena+MAL_MEC+126u*(mal_next()%5),(uint16_t)mal_next());
        return eax;
    case 15:  /* flyer_ctor: the coordinate, angle or trigger table */
        if(effect)switch(pick>>8&3){
        case 0:put32(mapped+0xa9e0c,MAL_A(MAL_COORDS+12u*(mal_next()%3)));break;
        case 1:put32(mapped+0xa9e10,MAL_A(MAL_ANGLES+2u*(mal_next()%3)));break;
        default:put32(mapped+0xa9e28,MAL_A(MAL_TRIGS+58u*(mal_next()%2)));break;
        }
        return a[0];
    case 16:  /* building_factory: a building slot (its vtable at +0) */
        if(effect){if(pick&0x100)put32(mapped+0xa9e0c,MAL_A(MAL_COORDS+12u*(mal_next()%3)));else put32(mapped+0xa9e10,MAL_A(MAL_ANGLES+2u*(mal_next()%3)));}
        return mal_buildings[mal_next()%4];
    case 17:   /* the shape word: the terrain pointer read after it */
        if(effect)put32(mapped+0xa0be0,mal_value());
        return eax;
    case 18:   /* 470ed4: the terrain pointer or the point (both reread) */
        if(effect){if(pick&0x100)put32(mapped+0xa0be0,mal_value());else put32(mal_host(a[1]+4u*(mal_next()%3)),mal_value());}
        return eax;
    case 19:  /* new[]: the orders, then the objectives (the phases of the count switches) */
        mal_phase=!mal_new_count&&mal_orders_planned?1u:2u;mal_phase_calls=0;
        return MAL_A(mal_new_count++?MAL_OBJS:MAL_ORDERS);
    case 20:  /* subject_resolve: the loop counts, a trigger's type, the tables read after it */
        if(mal_phase==1&&mal_phase_calls==mal_order_switch)put16(mapped+0xa9e2c,(uint16_t)mal_order_final);
        if(mal_phase==2&&mal_phase_calls==mal_objective_switch)put16(mapped+0xa9e3c,(uint16_t)mal_objective_final);
        ++mal_phase_calls;
        if(effect)switch(pick>>8&3){
        case 0:put16(mapped+0xa9e24,(uint16_t)(mal_next()%5));break;
        case 1:put16(mal_arena+MAL_TRIGS+58u*(mal_next()%7),(uint16_t)(mal_next()%13));break;
        case 2:put32(mapped+0xa9e28,MAL_A(MAL_TRIGS+58u*(mal_next()%2)));break;
        default:put32(mapped+0xa9e0c,MAL_A(MAL_COORDS+12u*(mal_next()%3)));break;
        }
        return mal_value();
    case 22:  /* group_ctor: the member count and array, as the real one builds them */
        put16(mal_host(a[0]+0x10u),(uint16_t)(pick&0x300?1u+mal_next()%3u:pick&0x400?0u:0x8000u|mal_next()));
        put32(mal_host(a[0]+0xcu),MAL_A(MAL_MEMBERS+4u*(mal_next()%4)));
        if(effect){if(pick&0x800)put32(mapped+0xa9e10,MAL_A(MAL_ANGLES+2u*(mal_next()%3)));else put32(mapped+0xa9e28,MAL_A(MAL_TRIGS+58u*(mal_next()%2)));}
        return eax;
    case 24:  /* 47129c: the building pool or the terrain pointer read after it */
        if(effect){if(pick&0x100)put32(mapped+0xa95a0,mal_value());else put32(mapped+0xa0be0,mal_value());}
        return eax;
    case 25: {  /* pool_prev: the case's buildings in order, then 0 */
        if(effect)put32(mapped+0xa0be0,mal_value());
        unsigned k=0;
        if(a[1]){while(k<mal_bcount&&mal_buildings[k]!=a[1])++k;++k;}
        return k<mal_bcount?mal_buildings[k]:0;
    }
    }
    return eax;
}
__attribute__((used,noinline)) static uint64_t __cdecl mal_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(mal_calls==MAL_MAX){fail("mission_actors call overflow");ExitProcess(1);}
    mal_record *v=mal_log+mal_calls++;memset(v,0,sizeof(*v));v->id=id;
    v->slots=weapons_bridge_slots(a,mal_counts[id]);
    for(unsigned k=0;k<mal_counts[id];++k)v->args[k]=a[k];
    for(unsigned j=0;j<3;++j)v->regs[j]=regs[j];
    if(id&&mal_fs0()!=mal_head){v->context=get16(mal_host(mal_fs0()+16u));if(!mal_record_va)mal_record_va=mal_fs0();}
    switch(id){   /* the contents a callee reads from the frame */
    case 9:case 18:for(unsigned j=0;j<3;++j)v->extra[j]=get32(mal_host(a[1]+4u*j));break;
    case 10:v->extra[0]=mal_hash(a[1],32);v->extra[1]=mal_hash(a[2],24);break;
    case 12:v->extra[0]=mal_hash(a[1],20);v->extra[1]=mal_hash(a[2],20);break;
    case 16:v->extra[0]=mal_hash(a[0],52);break;
    case 22:v->extra[0]=mal_hash(a[1],156);v->extra[1]=mal_hash(a[3],40);break;
    }
    uint32_t eax=mal_value(),edx=mal_value(),pick=mal_next();mal_ecx=mal_value();
    if(id==0)eax=mal_fs0();
    else eax=mal_answer(id,a,eax,pick);
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define MAL_STUB(n) __attribute__((naked)) static void mal_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _mal_stub_impl\n\taddl $24,%esp\n\tmovl _mal_ecx,%ecx\n\tret\n\t");}
MAL_STUB(1) MAL_STUB(2) MAL_STUB(3) MAL_STUB(4) MAL_STUB(5) MAL_STUB(6) MAL_STUB(7) MAL_STUB(8) MAL_STUB(9) MAL_STUB(10)
MAL_STUB(11) MAL_STUB(12) MAL_STUB(13) MAL_STUB(14) MAL_STUB(15) MAL_STUB(16) MAL_STUB(17) MAL_STUB(18) MAL_STUB(19) MAL_STUB(20)
MAL_STUB(21) MAL_STUB(22) MAL_STUB(23) MAL_STUB(24) MAL_STUB(25) MAL_STUB(26)
#undef MAL_STUB
/* 49630c: logs like the other stubs (EDX and ECX come back unchanged, as the
 * original's do), then links a record at EBP + [descriptor+4] into FS:0
 * with the saved head at record+0 and a zero context word. */
__attribute__((naked)) static void mal_stub_0(void){__asm__ volatile(
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
    "pushl $0\n\tcalll _mal_stub_impl\n\taddl $12,%esp\n\t"
    "popl %ecx\n\tmovl 4(%ecx),%ecx\n\taddl %ebp,%ecx\n\tmovl %eax,(%ecx)\n\tmovw $0,16(%ecx)\n\tmovl %ecx,%fs:0\n\t"
    "popl %edx\n\tpopl %ecx\n\tret\n\t");}
static void *mal_stubs[MAL_IDS]={mal_stub_0,mal_stub_1,mal_stub_2,mal_stub_3,mal_stub_4,mal_stub_5,mal_stub_6,mal_stub_7,
    mal_stub_8,mal_stub_9,mal_stub_10,mal_stub_11,mal_stub_12,mal_stub_13,mal_stub_14,mal_stub_15,mal_stub_16,mal_stub_17,
    mal_stub_18,mal_stub_19,mal_stub_20,mal_stub_21,mal_stub_22,mal_stub_23,mal_stub_24,mal_stub_25,mal_stub_26};
static int mal_patch(BYTE saved[MAL_IDS][5],int active[MAL_IDS],uint32_t which,int on){
    for(unsigned i=0;i<MAL_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!mal_addresses[i])continue;
            if(actor_patch(mal_addresses[i]-0x400000u,mal_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(mal_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
/* Stubbed in the direct cases: every callee. In the real-callee pass the
 * nested hooks pool_alloc (7), action_copy (12), subject_resolve (20),
 * heading_point (21), pool_prev (25) and the runtime 49630c (0) run for
 * real. */
#define MAL_ALL 0x7ffffffu
#define MAL_REAL (MAL_ALL&~(MAL_BIT(0)|MAL_BIT(7)|MAL_BIT(12)|MAL_BIT(20)|MAL_BIT(21)|MAL_BIT(25)))
/* The image globals: the statics 49a871..49a884, the name and terrain
 * pointers, the building, MECH and FLYER pools, the player, the script
 * tables and counts 4a9e00..4a9e43, the status word, the string table, the
 * selection, remap and MEC fields 4aa72a..4aaa65, the counter pointer. */
static const struct { uint32_t rva,size; } mal_ranges[]={{0x9a871,0x14},{0xa09f0,4},{0xa0be0,4},{0xa95a0,4},{0xa9b7a,4},
    {0xa9b84,4},{0xa9db9,4},{0xa9e00,0x44},{0xa9e4c,2},{0xaa660,0xc8},{0xaa72a,0x33c},{0x2cbee4,4}};
enum { MAL_GLOBALS=0x14+4+4+4+4+4+4+0x44+2+0xc8+0x33c+4 };
static void mal_globals(BYTE *out,int restore){
    for(unsigned i=0;i<sizeof(mal_ranges)/sizeof(mal_ranges[0]);++i){
        if(restore)memcpy(mapped+mal_ranges[i].rva,out,mal_ranges[i].size);else memcpy(out,mapped+mal_ranges[i].rva,mal_ranges[i].size);
        out+=mal_ranges[i].size;
    }
}
static int mal_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<MAL_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
/* ---- the synthetic script.dat ---- */
static void mal_put16(uint16_t w){mal_file[mal_file_size++]=(BYTE)w;mal_file[mal_file_size++]=(BYTE)(w>>8);}
static BYTE *mal_bytes(unsigned n){BYTE *p=mal_file+mal_file_size;for(unsigned i=0;i<n;++i)p[i]=mal_byte();mal_file_size+=n;return p;}
/* A list count: 0, small, or (not exact) negative. */
static uint16_t mal_count(unsigned limit,int exact){
    const unsigned k=mal_next()%8;
    if(k==0)return 0;
    if(k==1&&!exact)return (uint16_t)(0x8000u|mal_next());
    return (uint16_t)(1u+mal_next()%limit);
}
/* An index word: -1 (or another negative), or 0 .. limit-1. */
static uint16_t mal_index(unsigned limit,int exact){
    const unsigned k=mal_next()%4;
    if(k==0)return 0xffff;
    if(k==1&&!exact&&mal_next()%2)return (uint16_t)(0xfff0u|mal_next());
    return (uint16_t)(mal_next()%limit);
}
static void mal_rec16(BYTE *p,unsigned offset,uint16_t w){put16(p+offset,w);}
/* A real pool at `header` (arena offset): `used` objects on the used list and
 * `free` free nodes (stride `stride`, links +0/+4, objects at node+8). */
static void mal_real_pool(unsigned header,unsigned nodes,unsigned stride,unsigned used,unsigned free){
    BYTE *H=mal_arena+header;const uint32_t first=MAL_A(nodes);
    put16(H,(uint16_t)free);put16(H+2,(uint16_t)used);put16(H+4,(uint16_t)(stride-8u));put32(H+6,first);
    put32(H+0xe,used?first:0);put32(H+0xa,free?first+stride*used:0);
    for(unsigned k=0;k<used;++k){BYTE *q=mal_arena+nodes+stride*k;put32(q,k+1<used?first+stride*(k+1):0);put32(q+4,k?first+stride*(k-1):0);}
    for(unsigned k=0;k<free;++k){BYTE *q=mal_arena+nodes+stride*(used+k);put32(q,k+1<free?first+stride*(used+k+1):0);put32(q+4,k?first+stride*(used+k-1):0);}
}
/* A list skipped by one read of size x count (if the count is not 0). */
static void mal_skip(unsigned unit,unsigned limit,int exact){
    const uint16_t n=mal_count(limit,exact);mal_put16(n);
    const uint32_t size=(uint32_t)unit*(uint32_t)(int32_t)(int16_t)n;
    if(n&&size-1u<0x800u)mal_bytes(size);
}
/* A list skipped by one read per record. */
static void mal_skip_records(unsigned unit,unsigned limit,int exact){
    const uint16_t n=mal_count(limit,exact);mal_put16(n);
    for(int i=0;n&&i<(int16_t)n;++i)mal_bytes(unit);
}
/* The case data. exact: the real callers' inputs; real: data for the
 * real-callee pass (real pools, valid subjects). */
static void mal_build(unsigned n,uint32_t r[3],uint32_t regs[3],uint32_t *fill,int exact,int real){
    mal_seed=random32();mal_stream=mal_seed^n;mal_real=real;mal_effects=!real&&!exact;
    for(unsigned i=0;i<3;++i){r[i]=mal_value();regs[i]=mal_value();}
    mal_random(0,MAL_SIZE);
    *fill=!exact&&!real&&mal_next()%2?mal_next():0;
    /* the two vtables, the counters, the tables */
    put32(mal_arena+MAL_VT+0x18,(uint32_t)(uintptr_t)mal_stub_5);
    put32(mal_arena+MAL_BVT+0x10,(uint32_t)(uintptr_t)mal_stub_17);
    put32(mapped+0x2cbee4,MAL_A(MAL_COUNTER));
    for(unsigned k=0;k<8;++k){   /* routes: a point count (0 .. 3) and two point pointers */
        put16(mal_arena+MAL_ROUTES+6u*k,(uint16_t)(mal_next()%4));put32(mal_arena+MAL_ROUTES+6u*k+2u,MAL_A(MAL_RPTS+8u*k));
        put32(mal_arena+MAL_RPTS+8u*k,MAL_A(MAL_COORDS+12u*(mal_next()%32)));put32(mal_arena+MAL_RPTS+8u*k+4u,MAL_A(MAL_COORDS+12u*(mal_next()%32)));
    }
    for(unsigned k=0;k<10;++k)   /* orders before the order loop writes them: the route pointer +8 */
        put32(mal_arena+MAL_ORDERS+22u*k+8u,mal_next()%3?MAL_A(MAL_ROUTES+6u*(mal_next()%8)):0);
    for(unsigned k=0;k<8;++k){put16(mal_arena+MAL_TRIGS+58u*k,(uint16_t)(mal_next()%16-3u));put32(mal_arena+MAL_TRIGS+58u*k+0x36u,mal_next()%100);}
    for(unsigned k=0;k<4;++k){put16(mal_arena+MAL_TYPES+0x60u*k+0x50u,(uint16_t)(mal_next()%2));mal_buildings[k]=MAL_A(MAL_BLDGS+0x1c0u*k);}
    for(unsigned k=0;k<8;++k)put32(mal_arena+MAL_MEMBERS+4u*k,mal_buildings[mal_next()%4]);
    for(unsigned k=0;k<4;++k)put32(mal_arena+MAL_BLDGS+0x1c0u*k,MAL_A(MAL_BVT));
    /* the image globals */
    for(unsigned k=0;k<0x14;++k)mapped[0x9a871+k]=mal_byte();
    mapped[0x9a87d]=(BYTE)(mal_next()%2?0:1+mal_next()%255);
    mapped[0x9a884]=(BYTE)(mal_next()%2?0:1+mal_next()%255);
    put32(mapped+0xa09f0,mal_value());put32(mapped+0xa0be0,mal_value());
    put32(mapped+0xa9b7a,MAL_MECH_POOL);put32(mapped+0xa9db9,MAL_FLYER_POOL);put32(mapped+0xa95a0,MAL_BUILDING_POOL);
    put32(mapped+0xa9b84,mal_value());
    for(unsigned k=0;k<0x44;++k)mapped[0xa9e00+k]=mal_byte();
    put32(mapped+0xa9e00,!exact&&mal_next()%16==0?0:MAL_A(MAL_GROUPS));
    const uint16_t groups=(uint16_t)(mal_next()%5);put16(mapped+0xa9e04,groups);
    put32(mapped+0xa9e0c,MAL_A(MAL_COORDS));put32(mapped+0xa9e10,MAL_A(MAL_ANGLES));put32(mapped+0xa9e18,MAL_A(MAL_ROUTES));
    put32(mapped+0xa9e28,MAL_A(MAL_TRIGS));put16(mapped+0xa9e24,(uint16_t)(!exact&&mal_next()%8==0?0x8000u|mal_next():mal_next()%7));
    put32(mapped+0xa9e30,MAL_A(MAL_ORDERS));
    put16(mapped+0xa9e4c,(uint16_t)mal_next());
    for(unsigned k=0;k<0xc8;++k)mapped[0xaa660+k]=mal_byte();
    for(unsigned k=0;k<0x33c;++k)mapped[0xaa72a+k]=mal_byte();
    const unsigned mec=mal_next()%5;
    put16(mapped+0xaaa5e,(uint16_t)mec);put32(mapped+0xaaa60,MAL_A(MAL_MEC));
    put16(mapped+0xaaa64,(uint16_t)(mal_next()%4?mal_next()%(mec+1u):mal_next()));
    for(unsigned k=0;k<100;++k){mapped[0xaa72a+k]=(BYTE)(mal_next()%6?1:0);mapped[0xaa78eu+k]=(BYTE)(mal_next()%12-2u);}
    for(unsigned k=0;k<50;++k){mapped[0xaa856+k]=(BYTE)(mal_next()%5?1:0);mapped[0xaa888+k]=(BYTE)(mal_next()%12-2u);}
    for(unsigned k=0;k<140;++k){mapped[0xaa8ba+k]=(BYTE)(mal_next()%5?1:0);mapped[0xaa946+k]=(BYTE)(mal_next()%12-2u);}
    /* the script: header, the skipped lists of pass 1 */
    mal_file_size=0;
    mal_bytes(20);
    mal_skip(12,20,exact);mal_skip(2,20,exact);
    {const uint16_t routes=mal_count(3,exact);mal_put16(routes);
     for(int i=0;routes&&i<(int16_t)routes;++i){const uint16_t k=mal_count(5,exact);mal_put16(k);
        const uint32_t size=2u*(uint32_t)(int32_t)(int16_t)k;if(size-1u<0x800u)mal_bytes(size);}}
    mal_skip(6,4,exact);mal_skip_records(74,4,exact);mal_skip_records(24,3,exact);
    /* mechs (M; the records of the M authored ones are read) */
    const uint16_t mechs=(uint16_t)(!exact&&mal_next()%16==0?0x8000u|mal_next():mal_next()%(real?7:8));
    mal_put16(mechs);
    for(int i=0;i<(int16_t)mechs;++i){
        BYTE *p=mal_bytes(134);
        mal_rec16(p,0,(uint16_t)(mal_next()%2?0:mal_next()));
        mal_rec16(p,2,(uint16_t)(mal_next()%3?mal_next()%8:0xffffu-mal_next()%3));
        mal_rec16(p,0x3e,mal_index(32,exact));mal_rec16(p,0x40,mal_index(32,exact));
        mal_rec16(p,0x80,mal_index(40,exact));mal_rec16(p,0x82,mal_index(40,exact));
    }
    for(unsigned k=0;k<6;++k){BYTE *p=mal_arena+MAL_MEC+126u*k;put16(p+4,(uint16_t)(mal_next()%32));}
    /* flyers */
    const uint16_t flyers=mal_count(4,exact);mal_put16(flyers);
    for(int i=0;flyers&&i<(int16_t)flyers;++i){
        BYTE *p=mal_bytes(92);
        mal_rec16(p,0x28,mal_index(32,exact));mal_rec16(p,0x2a,mal_index(32,exact));
        mal_rec16(p,0x56,mal_index(40,exact));mal_rec16(p,0x58,mal_index(40,exact));
    }
    /* buildings */
    const uint16_t buildings=mal_count(5,exact);mal_put16(buildings);
    for(int i=0;buildings&&i<(int16_t)buildings;++i){
        BYTE *p=mal_bytes(52);
        mal_rec16(p,2,mal_index(32,exact));mal_rec16(p,4,mal_index(32,exact));
        mal_rec16(p,0x2e,mal_index(40,exact));mal_rec16(p,0x30,mal_index(40,exact));
    }
    /* orders (the count into 4a9e2c; with a count switch the script holds
     * the switched number of records) */
    const uint16_t orders=(uint16_t)(mal_next()%4==0?0:1u+mal_next()%6);mal_put16(orders);
    mal_orders_planned=orders;mal_order_switch=0xffffffffu;unsigned order_records=orders;
    if(mal_effects&&orders&&mal_next()%4==0){mal_order_switch=mal_next()%orders;mal_order_final=mal_order_switch+1u+mal_next()%3u;order_records=mal_order_final;}
    for(unsigned i=0;i<order_records;++i){
        BYTE *p=mal_bytes(14);
        mal_rec16(p,4,mal_index(32,exact));mal_rec16(p,6,mal_index(8,1));
        mal_rec16(p,8,(uint16_t)(mal_next()%5));mal_rec16(p,0xa,(uint16_t)(mal_next()%40));mal_rec16(p,0xc,mal_index(40,exact));
    }
    /* groups: the count word (only tested for 0), then pass 1's array
     * count of records */
    const uint16_t gword=(uint16_t)(mal_next()%8?1u+mal_next():0);mal_put16(gword);
    if(gword&&get32(mapped+0xa9e00))for(unsigned i=0;i<groups;++i){
        BYTE *p=mal_bytes(156);
        static const uint16_t classes[]={0,1,2,2,0,3,0xffff,1};
        mal_rec16(p,0,(uint16_t)(mal_next()%2?1:mal_next()%4));
        mal_rec16(p,0x28,real?(uint16_t)(mal_next()%3):classes[mal_next()%8]);
        mal_rec16(p,0x2c,(uint16_t)(mal_next()%3==0?0xffff:mal_next()%4==0&&!exact?0xfffe:mal_next()%32));
        const uint16_t angle=mal_index(32,exact);mal_rec16(p,0x2e,angle);
        for(unsigned k=0;k<20;++k)mal_rec16(p,0x32u+2u*k,(uint16_t)(mal_next()%3==0?0xffff:mal_next()%100));
        for(unsigned k=0;k<10;++k)mal_rec16(p,0x5au+2u*k,(uint16_t)(mal_next()%3==0?0xffff:mal_next()%10));
        if((int16_t)angle<0)mal_rec16(p,0x5a,(uint16_t)(mal_next()%10));   /* the first order exists (a null one faults) */
        mal_rec16(p,0x70,mal_index(40,exact));
    }
    /* objectives (the count into 4a9e3c; a count switch as for the orders) */
    const uint16_t objectives=(uint16_t)(mal_next()%4==0?0:1u+mal_next()%4);mal_put16(objectives);
    mal_objective_switch=0xffffffffu;unsigned objective_records=objectives;
    if(mal_effects&&objectives&&mal_next()%4==0){mal_objective_switch=mal_next()%objectives;mal_objective_final=mal_objective_switch+1u+mal_next()%2u;objective_records=mal_objective_final;}
    for(unsigned i=0;i<objective_records;++i){
        BYTE *p=mal_bytes(54);
        mal_rec16(p,4,(uint16_t)(mal_next()%5));mal_rec16(p,6,(uint16_t)(mal_next()%40));
        mal_rec16(p,8,mal_index(32,exact));mal_rec16(p,0xa,mal_index(8,exact));mal_rec16(p,0xc,mal_index(40,exact));
    }
    mal_bcount=mal_next()%5;
    if(real){
        /* real pools with room for the objects action_copy writes (up to
         * +1E1h): MECH 10 x 372h (the game's 36Ah objects), FLYER 4 x 2A0h
         * (291h), all free; the buildings 4 x 208h with mal_bcount used (the
         * factory's objects) */
        mal_real_pool(MAL_POOLS,MAL_PNODES,0x372,0,10);
        mal_real_pool(MAL_POOLS+0x14,MAL_PNODES+0x372*10,0x2a0,0,4);
        const unsigned bnodes=MAL_PNODES+0x372*10+0x2a0*4;
        mal_real_pool(MAL_POOLS+0x28,bnodes,0x208,mal_bcount,4-mal_bcount);
        for(unsigned k=0;k<4;++k){mal_buildings[k]=MAL_A(bnodes+0x208u*(k<mal_bcount?k:0)+8u);put32(mal_host(mal_buildings[k]),MAL_A(MAL_BVT));}
        for(unsigned k=0;k<8;++k)put32(mal_arena+MAL_MEMBERS+4u*k,mal_buildings[mal_next()%4]);
        put32(mal_arena+MAL_COUNTER,mal_next()%1000);
    }
    mal_stream=mal_seed;   /* the stubs' stream restarts here in every run */
}
/* Calls target with six (unread) argument slots, EAX/EDX/ECX = r and
 * EBX/ESI/EDI = regs over 3,840 bytes of stack filled with `fill`; out
 * receives EAX, EDX and ECX after the call. */
__attribute__((naked,noinline)) static void mal_invoke(void *target,const uint32_t *a,const uint32_t *r,const uint32_t *regs,uint32_t fill,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 24(%ebp),%eax\n\tmovl $1024,%ecx\n\tcld\n\trep stosl\n\taddl $3840,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 20(%esi)\n\tpushl 16(%esi)\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 20(%ebp),%edi\n\tmovl (%edi),%ebx\n\tmovl 4(%edi),%esi\n\tmovl 8(%edi),%edi\n\t"
        "movl 16(%ebp),%eax\n\tmovl 4(%eax),%edx\n\tmovl 8(%eax),%ecx\n\tmovl (%eax),%eax\n\tcall *8(%ebp)\n\t"
        "movl 28(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* Calls fn(arg) with ESP switched to `top` (a static stack) and back. */
__attribute__((naked,noinline)) static void mal_switch_call(void (*fn)(void *),void *arg,uint32_t top){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tmovl 16(%ebp),%esp\n\tpushl 12(%ebp)\n\tcalll *8(%ebp)\n\t"
        "movl %ebp,%esp\n\tpopl %ebp\n\tret\n\t");
}
typedef struct mal_core_call { const uint32_t *r; uint32_t frame; uint64_t pair; uint32_t head; } mal_core_call;
static void mal_core_body(void *p){
    mal_core_call *c=p;
    es2_weapon_graph g=weapons_graph(mapped,mal_host(c->frame-ES2_ACTOR_LOAD_FRAME),ES2_HOOK_INDEX_actor_load);
    weapons_impact_context x={(uint32_t)(uintptr_t)mapped,c->frame};
    g.call=weapons_impact_guest_call;g.context=&x;
    c->head=0;
    c->pair=es2_actor_load_registers(&math_native_memory,&g,c->r,&c->head);
    __asm__ volatile("movl %0,%%fs:0"::"r"(c->head):"memory");
}
static BYTE mal_core_stack[0x10000] __attribute__((aligned(16)));
/* The core with the adapter's graph and a zeroed scratch whose frame f lies
 * at the original's EBP (`frame`, on the stack below the caller, free after
 * the original returned); the core's own code runs on a static stack, so
 * every frame address, and every register that combines one with a data
 * word, is the original's. FS:0 restored to the record's saved head as the
 * adapter does. */
static uint64_t mal_core(const uint32_t *r,uint32_t frame,uint32_t *ecx){
    memset(mal_host(frame-ES2_ACTOR_LOAD_FRAME),0,ES2_ACTOR_LOAD_SCRATCH);
    mal_core_call c={r,frame,0,0};
    mal_switch_call(mal_core_body,&c,(uint32_t)(uintptr_t)(mal_core_stack+sizeof(mal_core_stack)));
    if(ecx)*ecx=c.head;
    return c.pair;
}
static void mal_mask_log(mal_record *log,unsigned n){
    for(unsigned j=0;j<n;++j){
        for(unsigned k=0;k<6;++k)log[j].args[k]=mal_mask(log[j].args[k]);
        for(unsigned k=0;k<3;++k)log[j].regs[k]=mal_mask(log[j].regs[k]);
        log[j].eax=mal_mask(log[j].eax);log[j].edx=mal_mask(log[j].edx);
    }
}
static int mal_report(const char *what,unsigned n,const uint32_t want[3],const uint32_t got[3],unsigned calls,const mal_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x calls %u/%u\n",current_function,n,what,
        want[0],got[0],want[1],got[1],want[2],got[2],calls,mal_calls);
    for(unsigned j=0;j<calls&&j<mal_calls&&expected;++j)if(memcmp(expected+j,mal_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(mal_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<MAL_SIZE;++j)if(mal_after[j]!=mal_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,mal_after[j],mal_arena[j]);break;}
    return 1;
}
/* The callee counter a call adds: by VA, or by slot for the two virtual
 * stubs; memcpy 49680e (not stubbed) twice per objective record read. */
static unsigned mal_expected_hits(const callee_counter *c){
    unsigned hits=0;
    if(c->event==ES2_COLLIDE_SLOT){
        const unsigned id=c->key==0x18u?5u:17u;
        for(unsigned j=0;j<mal_calls;++j)if(mal_log[j].id==id)++hits;
        return hits;
    }
    if(c->key==0x49680eu){
        for(unsigned j=0;j<mal_calls;++j)if(mal_log[j].id==4&&mal_log[j].args[1]==0x36u)hits+=2;
        return hits;
    }
    for(unsigned j=0;j<mal_calls;++j)if(mal_addresses[mal_log[j].id]==c->key)++hits;
    return hits;
}
/* One direct case: the original, the core and (exact inputs) the adapter. */
static int mal_compare(unsigned n,const uint32_t *r,const uint32_t *regs,uint32_t fill,int exact,int check_ecx){
    const unsigned index=ES2_HOOK_INDEX_actor_load;
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[MAL_GLOBALS],after[MAL_GLOBALS],actual[MAL_GLOBALS];static mal_record expected[MAL_MAX];uint32_t want[3],got[3];
    static const uint32_t a[6]={0};
    const uint32_t head=mal_fs0();
    memcpy(mal_before,mal_arena,MAL_SIZE);mal_globals(before,0);
    mal_reset();mal_invoke(mapped+d->rva,a,r,regs,fill,want);const unsigned calls=mal_calls;memcpy(expected,mal_log,calls*sizeof(*expected));
    memcpy(mal_after,mal_arena,MAL_SIZE);mal_globals(after,0);
    if(mal_fs0()!=head)return fail("mission_actors original left FS:0 changed");
    for(unsigned j=0;j<calls;++j)++mal_paths[expected[j].id];
    if(!mal_record_va)return fail("mission_actors original linked no record");
    const uint32_t frame=mal_record_va+0x34u;
    memcpy(mal_arena,mal_before,MAL_SIZE);mal_globals(before,1);mal_reset();
    got[2]=~want[2];
    uint64_t pair=mal_core(r,frame,&got[2]);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);mal_globals(actual,0);
    if(mal_fs0()!=head)return fail("mission_actors core left FS:0 changed");
    if(want[0]!=got[0]||want[1]!=got[1]||(check_ecx&&want[2]!=got[2])||calls!=mal_calls||memcmp(expected,mal_log,calls*sizeof(*expected))||
       memcmp(mal_after,mal_arena,MAL_SIZE)||mal_globals_differ(after,actual))
        return mal_report("core",n,want,got,calls,expected);
    if(!exact){mal_globals(before,1);return 0;}
    /* the adapter (the framework's entry: no argument slots) */
    memcpy(mal_arena,mal_before,MAL_SIZE);mal_globals(before,1);mal_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[32];
    if(count>32)return fail("mission_actors counter table too long");
    for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair=x->replacement_context(a,d->user,r);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);got[2]=want[2];
    mal_globals(actual,0);
    if(mal_fs0()!=head)return fail("mission_actors adapter left FS:0 changed");
    mal_mask_log(expected,calls);mal_mask_log(mal_log,mal_calls);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=mal_expected_hits(c+i)){
        fprintf(stderr,"FAIL: actor_load case %u counter %s delta %llu, expected %u\n",n,c[i].name,
            (unsigned long long)(c[i].count-counts[i]),mal_expected_hits(c+i));
        return 1;
    }
    if(want[0]!=got[0]||want[1]!=got[1]||calls!=mal_calls||memcmp(expected,mal_log,calls*sizeof(*expected))||
       memcmp(mal_after,mal_arena,MAL_SIZE)||mal_globals_differ(after,actual))
        return mal_report("adapter",n,want,got,calls,expected);
    mal_globals(before,1);
    return 0;
}
static int mal_installed_probes(unsigned states){
    const unsigned index=ES2_HOOK_INDEX_actor_load;
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    static const uint32_t a[6]={0};
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t r[3],regs[3],fill,want[3]={0},got[3]={0},pattern=poison?0xa7a7a7a7u:0x31313131u;
        mal_build(n*37u+11u,r,regs,&fill,1,0);
        const uint32_t entry[3]={pattern,pattern,pattern};const uint32_t head=mal_fs0();
        BYTE before[MAL_GLOBALS],after[MAL_GLOBALS],actual[MAL_GLOBALS],expected_fpu[108],actual_fpu[108];static mal_record expected[MAL_MAX];
        memcpy(mal_before,mal_arena,MAL_SIZE);mal_globals(before,0);
        mal_reset();mal_invoke(mapped+d->rva,a,entry,regs,pattern,want);es2_hook_fpu_save(expected_fpu);
        const unsigned calls=mal_calls;memcpy(expected,mal_log,calls*sizeof(*expected));memcpy(mal_after,mal_arena,MAL_SIZE);mal_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("mission_actors poison install");
        memcpy(mal_arena,mal_before,MAL_SIZE);mal_globals(before,1);mal_reset();mal_invoke(mapped+d->rva,a,entry,regs,pattern,got);
        es2_hook_fpu_save(actual_fpu);mal_globals(actual,0);
        mal_mask_log(expected,calls);mal_mask_log(mal_log,mal_calls);
        if(mal_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||calls!=mal_calls||memcmp(expected,mal_log,calls*sizeof(*expected))||
           memcmp(mal_after,mal_arena,MAL_SIZE)||mal_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||
           memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=mal_report(fast?"installed fast":"installed preserving",n,want,got,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("mission_actors poison uninstall");mal_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, pair/trace/arena/globals/FS:0\n",d->name,total);
    return status;
}
/* ---- real callees ---- */
enum { MAL_NESTED=6 };
static int mal_real_pass(void){
    const unsigned index=ES2_HOOK_INDEX_actor_load;
    unsigned nested[MAL_NESTED],count=0;
    nested[count++]=ES2_HOOK_INDEX_pool_alloc;nested[count++]=ES2_HOOK_INDEX_action_copy;nested[count++]=ES2_HOOK_INDEX_subject_resolve;
    nested[count++]=ES2_HOOK_INDEX_pool_nth;nested[count++]=ES2_HOOK_INDEX_pool_prev;nested[count++]=ES2_HOOK_INDEX_heading_point;
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[MAL_NESTED+1]={0};
    BYTE saved[MAL_IDS][5];int active[MAL_IDS];
    if(mal_patch(saved,active,MAL_REAL,1))return 1;
    static mal_record expected[MAL_MAX];static const uint32_t a[6]={0};
    const unsigned modes=2u<<count;
    for(unsigned mode=0;mode<modes&&!status;++mode)for(unsigned n=0;n<4096u/modes&&!status;++n){
        uint32_t r[3],regs[3],fill,want[3]={0},got[3]={0},pattern=n&1?0xa7a7a7a7u:0x31313131u;
        mal_build(n*13u+mode,r,regs,&fill,1,1);
        const uint32_t entry[3]={pattern,pattern,pattern};const uint32_t head=mal_fs0();
        BYTE before[MAL_GLOBALS],after[MAL_GLOBALS],actual[MAL_GLOBALS];
        memcpy(mal_before,mal_arena,MAL_SIZE);mal_globals(before,0);
        es2_hook hk[MAL_NESTED+1];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(mode>>(1+j)&1)?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,function_hook_extension(nested[j])))return fail("mission_actors nested install");}
        mal_reset();mal_invoke(mapped+d->rva,a,entry,regs,pattern,want);
        const unsigned calls=mal_calls;memcpy(expected,mal_log,calls*sizeof(*expected));
        memcpy(mal_after,mal_arena,MAL_SIZE);mal_globals(after,0);
        o.mode=mode&1?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(hk+MAL_NESTED,d,&o,function_hook_extension(index)))return fail("mission_actors caller install");
        memcpy(mal_arena,mal_before,MAL_SIZE);mal_globals(before,1);mal_reset();
        mal_invoke(mapped+d->rva,a,entry,regs,pattern,got);mal_globals(actual,0);
        mal_mask_log(expected,calls);mal_mask_log(mal_log,mal_calls);
        if(mal_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||calls!=mal_calls||memcmp(expected,mal_log,calls*sizeof(*expected))||
           memcmp(mal_after,mal_arena,MAL_SIZE)||mal_globals_differ(after,actual)){
            fprintf(stderr,"real mode=%u\n",mode);status=mal_report("real",n,want,got,calls,expected);
        }
        for(unsigned j=0;j<=MAL_NESTED;++j){if(!hk[j].descriptor)continue;entered[j]+=hk[j].calls;
            if(hk[j].mismatches)status=fail("mission_actors real compare mismatch");
            if(!es2_hook_uninstall(hk+j))return fail("mission_actors real uninstall");}
        mal_globals(before,1);++total;
    }
    if(mal_patch(saved,active,0,0))return 1;
    mal_real=0;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&!entered[MAL_NESTED]){fprintf(stderr,"FAIL: %s never entered\n",d->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, %u nested hooks original/replaced, hook original/replaced, 31/a7 stack, pair, calls, arena, globals and FS:0\n",
        d->name,total,count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
static int mission_actors_cases(unsigned index,unsigned limit){
    current_function=function_hook_descriptor(index)->name;
    BYTE saved[MAL_IDS][5],old_globals[MAL_GLOBALS];int active[MAL_IDS];
    mal_globals(old_globals,0);
    if(mal_patch(saved,active,MAL_ALL,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_MISSION_ACTORS_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);memset(mal_paths,0,sizeof(mal_paths));
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t r[3],regs[3],fill;const int exact=limit||!(n&1);
        mal_build(n,r,regs,&fill,exact,0);status=mal_compare(n,r,regs,fill,exact&&!limit,!limit);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    mal_globals(old_globals,1);
    if(!limit&&!status)status=mal_installed_probes(64);
    if(mal_patch(saved,active,MAL_ALL,0))return 1;
    mal_globals(old_globals,1);
    if(!limit&&!status)status=mal_real_pass();
    mal_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases (stub calls header/skip reads %u, virtual reads %u, pool_alloc %u, mech_ctor %u, terrain %u, "
        "weapon_fit %u, condition_init %u, action_copy %u, initial_damage %u, flyer_ctor %u, building_factory %u, shape word %u, "
        "new[] %u, subject_resolve %u, heading_point %u, group_ctor %u, base_marks %u, pool_prev %u); "
        "full return pair and ECX, ordered calls with entry registers, frame offsets, slots and context word, arena/globals/FS:0\n",
        function_hook_descriptor(index)->name,cases,mal_paths[4],mal_paths[5],mal_paths[7],mal_paths[8],mal_paths[9],mal_paths[10],
        mal_paths[11],mal_paths[12],mal_paths[13],mal_paths[15],mal_paths[16],mal_paths[17],mal_paths[19],mal_paths[20],mal_paths[21],
        mal_paths[22],mal_paths[23],mal_paths[25]);
    return status;
}