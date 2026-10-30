/* mission_ctors group: mech_ctor 415bb0, flyer_ctor 4215f4, building_factory
 * 405314, group_ctor 423b34 and base_marks 405db0
 * (docs/reverse-engineering/mission_load.md, "L5 constructors").
 *
 * Direct cases run the original body (EAX/EDX/ECX, EBX, ESI and EDI at entry
 * from the case, six argument slots, over a stack filled with a case
 * pattern) and the core (with the same ESI for mech_ctor and EDI for the
 * factory, and the ECX it reports) against logging stubs for every game
 * callee of the hook under test: 49630c (a record stub: the real FS:0 head
 * at record+0, context word +10h zeroed, the record linked into FS:0), the
 * shape set-ups 4033a4/40332c/403368, 40d2cc, 49667c, 402374, new[] 4774f0,
 * pool_alloc, object_init, script_start, 492bec, 40d160, 40cd88, 40d354,
 * euler_matrix, matrix_transpose, rotate_point, track_rate, track_seek,
 * pool_nth, the join methods 417aa8/421ee8/405c3c and 47136c; memset 49681a
 * is the harness's own CW3220 memset; the leaves 41ff00, 422e80, 411dd4 and
 * 413a90 run as original code in the original and are computed by the core.
 * Stack addresses in logged registers and slots are masked (frame addresses
 * differ between the original's frame and the core's scratch), and the
 * vector rotate_point reads is logged by content. Compared: the full return
 * pair, ECX, the ordered calls with argument slots (and the bridge's slot
 * count), entry EAX/EDX/ECX and the record's context word, the arena, the
 * image globals (the actor list header, the counter pointer, the type
 * tables and their index bytes, the pool and table pointers) and FS:0. The
 * stubs change what the originals reread (the type index byte and the
 * record table after 4033a4/403368, the counter pointer, the type words
 * after 402374, the flat-object count and array, the shape table, the
 * matrix after euler_matrix, the translation after matrix_transpose, the
 * part count after new[], the group class after new[], the member array
 * after pool_nth), and objects overlap (the actor-list store over its
 * count word), so the order of reads and writes is observable. In even
 * cases the inputs are the real callers' (ESI = the actor for mech_ctor, no
 * factory case 0) and the adapter is compared as well, with the callee
 * counter deltas.
 *
 * Then installed 31/a7 poison probes and real-callee passes: mech_ctor over
 * the real 49630c, pool_alloc (a real flat pool), object_init (its new
 * 47749c a bump allocator, 490b64 a stub), script_start, euler_matrix,
 * matrix_transpose and rotate_point; flyer_ctor over the real 403368,
 * object_init, script_start and the three matrix hooks; building_factory
 * over the real 49630c, pool_alloc (a real building pool), 49667c with its
 * element constructors, 40332c/402278 (478838 a stub) and 403368/object_init;
 * group_ctor over the real pool_nth, pool_prev and pool_next on three real
 * pools; the nested hooks in rotating original/replaced modes, the hook
 * under test original or replaced against a run without it. Identifiers
 * use the prefix mct_/MCT_. */
typedef struct mct_record { uint32_t id,slots,args[6],regs[3],context,extra[3],eax,edx; } mct_record;
enum {
    MCT_SIZE=0x4000, MCT_MAX=96, MCT_IDS=28,
    MCT_OBJ=0x100, MCT_OBJ2=0x480, MCT_TYPES=0x800, MCT_FTYPES=0xf00, MCT_X=0x1080, MCT_Y=0x10a0, MCT_Z=0x10e0, MCT_ARR=0x1100,
    MCT_NODES=0x1140, MCT_LIST=0x1200, MCT_FLAT=0x1280, MCT_FOBJ=0x1300, MCT_SHAPES=0x1500, MCT_NAMES=0x1580, MCT_COUNTER=0x15c0,
    MCT_COUNTER2=0x15d0, MCT_BT=0x1600, MCT_BCFG=0x1640, MCT_PARTS=0x1740, MCT_WORDS=0x17a0, MCT_SHAPEI=0x17c0, MCT_FRAMES=0x1800,
    MCT_MODELS=0x1820, MCT_REC=0x1840, MCT_GROUP=0x1900, MCT_GREC=0x1980, MCT_ORDERS=0x1a20, MCT_ORDREC=0x1a50, MCT_MEMBERS=0x1b00,
    MCT_BASES=0x1b80, MCT_POOLS=0x1c00, MCT_PNODES=0x1c80, MCT_BUMP=0x2400
};
#define MCT_B ((uint32_t)(uintptr_t)mct_arena)
#define MCT_A(o) (MCT_B+(uint32_t)(o))
#define MCT_BIT(i) ((uint32_t)1<<(i))
static BYTE mct_arena[MCT_SIZE],mct_before[MCT_SIZE],mct_after[MCT_SIZE];
static mct_record mct_log[MCT_MAX];
static unsigned mct_calls,mct_real,mct_effects,mct_index,mct_paths[MCT_IDS];
static uint32_t mct_seed,mct_stream,mct_bump,mct_head,mct_type_byte,mct_obj;
__attribute__((used)) static uint32_t mct_ecx;
/* 0 49630c, 1 4033a4, 2 40d2cc, 3 49667c, 4 402374, 5 new[] 4774f0, 6
 * pool_alloc 471b30, 7 object_init 402188, 8 script_start 413e50, 9 492bec,
 * 10 40d160, 11 40cd88, 12 40d354, 13 euler_matrix 47ebb8, 14
 * matrix_transpose 47df19, 15 rotate_point 480304, 16 403368, 17 40332c, 18
 * track_rate 4796d8, 19 track_seek 479344, 20 pool_nth 471cb0, 21 417aa8, 22
 * 421ee8, 23 405c3c, 24 47136c; in the real passes only: 25 new 47749c, 26
 * 490b64, 27 478838. The slots each original call site pushes. */
static const uint32_t mct_addresses[MCT_IDS]={0x49630c,0x4033a4,0x40d2cc,0x49667c,0x402374,0x4774f0,0x471b30,0x402188,0x413e50,
    0x492bec,0x40d160,0x40cd88,0x40d354,0x47ebb8,0x47df19,0x480304,0x403368,0x40332c,0x4796d8,0x479344,0x471cb0,0x417aa8,
    0x421ee8,0x405c3c,0x47136c,0x47749c,0x490b64,0x478838};
static const unsigned mct_counts[MCT_IDS]={0,2,3,5,2,1,1,2,2,2,1,1,4,2,1,3,2,2,2,3,2,3,3,4,5,1,2,2};
static uint32_t mct_next(void){
    uint32_t z=mct_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *mct_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static uint32_t mct_fs0(void){uint32_t v;__asm__ volatile("movl %%fs:0,%0":"=r"(v));return v;}
static int mct_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static void mct_reset(void){mct_calls=0;mct_stream=mct_seed;mct_bump=MCT_A(MCT_BUMP);mct_head=mct_fs0();}
static void mct_random(unsigned offset,unsigned size){for(unsigned i=0;i<size;++i)mct_arena[offset+i]=(BYTE)mct_next();}
static uint32_t mct_alloc(uint32_t size){
    const uint32_t p=mct_bump;size=(size+15u)&~7u;
    if(size>0x1000u||p+size>MCT_A(MCT_SIZE)){fail("mission_ctors bump allocator overflow");ExitProcess(1);}
    memset(mct_host(p),0,size);mct_bump+=size;return p;
}
/* The type index window of the MECH (4a9d08) and FLYER (4a9dcc) byte tables
 * the cases use (sx(type) in -8 .. 23). */
static uint32_t mct_mech_index(uint32_t type){return 0xa9d08u+(uint32_t)(int32_t)(int16_t)type;}
static uint32_t mct_flyer_index(uint32_t type){return 0xa9dccu+(uint32_t)(int32_t)(int16_t)type;}
/* What the stubs answer and change. */
static uint32_t mct_answer(unsigned id,const uint32_t *a,uint32_t eax,uint32_t pick){
    const int effect=mct_effects&&pick%4==0;
    switch(id){
    case 1:   /* 4033a4: the type index byte or the type table (both reread) */
        if(effect){if(pick&0x100)mapped[mct_type_byte]=(BYTE)(mct_next()%3);else put32(mapped+0xa9d64,MCT_A(MCT_TYPES+0x21e*(mct_next()%2)));}
        return a[0];
    case 2:   /* 40d2cc: the counter pointer */
        if(effect)put32(mapped+0x2cbee4,MCT_A(pick&0x100?MCT_COUNTER2:MCT_COUNTER));
        return eax;
    case 3:   /* 49667c: the factory's part records (the type's part count reread) */
        if(mct_index==ES2_HOOK_INDEX_building_factory){
            if(effect)put16(mct_arena+MCT_BT+0x12,(uint16_t)(mct_next()%7));
            return MCT_A(MCT_PARTS);
        }
        return eax;
    case 4:   /* 402374: a track; the type words reread after it */
        if(effect){
            if(mct_index==ES2_HOOK_INDEX_mech_ctor){
                const uint32_t t=get32(mct_host(a[0]+0x1f2));
                if(t-MCT_B<MCT_SIZE-0x30u)put16(mct_host(t+(pick&0x100?0x1c:0x24)),(uint16_t)(pick&0x200?0xffff:mct_next()%4));
                else put32(mct_host(a[0]+0x34),MCT_A(pick&0x200?MCT_X:MCT_X+8));
            } else if(pick&0x100)put16(mct_arena+MCT_BT+0x20+2*(pick>>9&1),(uint16_t)mct_next());
            else put16(mct_arena+MCT_BT+6,(uint16_t)(mct_next()%3));
        }
        return mct_next();
    case 5:   /* new[]: the flat array, the part words, the member array */
        if(mct_real)return mct_alloc(a[0]);
        if(mct_index==ES2_HOOK_INDEX_mech_ctor){
            if(effect)put16(mct_host(mct_obj+0x23c),(uint16_t)(1+mct_next()%3));
            return MCT_A(pick%8==0?MCT_FLAT-4:MCT_FLAT);
        }
        if(mct_index==ES2_HOOK_INDEX_building_factory){
            if(effect)put16(mct_arena+MCT_BT+0x12,(uint16_t)(mct_next()%7));
            return MCT_A(MCT_WORDS);
        }
        if(effect)put16(mct_arena+MCT_GROUP,(uint16_t)(pick&0x100?mct_next()%4:mct_next()));
        return MCT_A(MCT_MEMBERS);
    case 6:   /* pool_alloc: never null (null objects fault, PROVED) */
        if(mct_index==ES2_HOOK_INDEX_mech_ctor){
            if(effect)switch(pick>>8&3){
            case 0:put16(mct_host(mct_obj+0x23c),(uint16_t)(mct_next()%5));break;
            case 1:put32(mapped+0xa9698,MCT_A(MCT_SHAPES+4*(mct_next()%4)));break;
            case 2:put32(mct_host(mct_obj+0x238),MCT_A(MCT_FLAT+4*(mct_next()%3)));break;
            default:put32(mapped+0x2cbee4,MCT_A(MCT_COUNTER2));break;
            }
            return MCT_A(MCT_FOBJ+0x40*(mct_next()%8));
        }
        if(effect)put32(mct_arena+MCT_SHAPES+4*(mct_next()%8),mct_next());
        return MCT_A(MCT_OBJ);
    case 7:case 16:case 17:   /* object_init, the shape set-ups: the counter pointer, the FLYER type byte or table */
        if(effect){
            if(pick&0x100)put32(mapped+0x2cbee4,MCT_A(pick&0x200?MCT_COUNTER2:MCT_COUNTER));
            else if(id==16&&mct_index==ES2_HOOK_INDEX_flyer_ctor){
                if(pick&0x200)mapped[mct_type_byte]=(BYTE)(mct_next()%3);else put32(mapped+0xa9df4,MCT_A(MCT_FTYPES+0x70));
            }
        }
        return a[0];
    case 8:   /* script_start: the type record pointer and the name table */
        if(effect){if(pick&0x100)put32(mct_host(mct_obj+0x1f2),MCT_A(MCT_TYPES+0x21e*(mct_next()%3)));else put32(mapped+0xa9d60,MCT_A(MCT_NAMES+4));}
        return eax;
    case 10:  /* 40d160: the second suffix */
        if(effect)put32(mapped+0xa0a08,mct_next());
        return mct_next();
    case 11:return mct_next();
    case 12:  /* 40d354: the matrix flag and the word read at the end */
        if(effect){if(pick&0x100)put16(mct_host(mct_obj+0x32),(uint16_t)(pick>>9&1));else put16(mct_host(mct_obj+0x290),(uint16_t)mct_next());}
        return eax;
    case 13:  /* euler_matrix: the matrix copied after it */
        if(effect)put32(mct_host(a[0]+6+4*(mct_next()%5)),mct_next());
        return eax;
    case 14:  /* matrix_transpose: the translation copy negated after it */
        if(effect)put32(mct_host(a[0]+0x14+4*(mct_next()%3)),mct_next());
        return eax;
    case 15:  /* rotate_point: the word read after it */
        if(effect&&mct_index==ES2_HOOK_INDEX_mech_ctor)put16(mct_host(mct_obj+0x290),(uint16_t)mct_next());
        return eax;
    case 18:  /* track_rate: the track slot reread for track_seek */
        if(effect)put32(mct_host(mct_obj+0x1fd),mct_next());
        return eax;
    case 20:  /* pool_nth: an object or 0; the member array or the next slot */
        if(effect){if(pick&0x100)put32(mct_arena+MCT_GROUP+0xc,MCT_A(MCT_MEMBERS+4*(pick>>9&3)));
            else put16(mct_arena+MCT_GREC+0x32+2*(mct_next()%20),(uint16_t)(pick&0x200?0xffff:mct_next()%8));}
        return pick%7==0?0:MCT_A(MCT_OBJ2+0x40*(mct_next()%8));
    case 21:case 22:case 23:  /* the join methods: the member array pointer */
        if(effect)put32(mct_arena+MCT_GROUP+0xc,MCT_A(MCT_MEMBERS+4*(pick>>8&3)));
        return eax;
    case 25:return mct_alloc(a[0]);          /* new (real passes) */
    case 26:case 27:return a[0];            /* 490b64, 478838 (real passes) */
    }
    return eax;
}
__attribute__((used,noinline)) static uint64_t __cdecl mct_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(mct_calls==MCT_MAX){fail("mission_ctors call overflow");ExitProcess(1);}
    mct_record *v=mct_log+mct_calls++;memset(v,0,sizeof(*v));v->id=id;
    v->slots=weapons_bridge_slots(a,mct_counts[id]);
    for(unsigned k=0;k<mct_counts[id];++k)v->args[k]=mct_stack(a[k])?0:a[k];
    for(unsigned j=0;j<3;++j)v->regs[j]=mct_stack(regs[j])?0:regs[j];
    if(id==15)for(unsigned j=0;j<3;++j)v->extra[j]=get32(mct_host(a[0]+4*j));   /* the vector, by content */
    if(id&&mct_fs0()!=mct_head)v->context=get16(mct_host(mct_fs0()+16u));         /* a hook's own record */
    uint32_t eax=mct_next(),edx=mct_next(),pick=mct_next();mct_ecx=mct_next();
    if(id==0)eax=mct_fs0();
    else eax=mct_answer(id,a,eax,pick);
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define MCT_STUB(n) __attribute__((naked)) static void mct_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _mct_stub_impl\n\taddl $24,%esp\n\tmovl _mct_ecx,%ecx\n\tret\n\t");}
MCT_STUB(1) MCT_STUB(2) MCT_STUB(3) MCT_STUB(4) MCT_STUB(5) MCT_STUB(6) MCT_STUB(7) MCT_STUB(8) MCT_STUB(9) MCT_STUB(10)
MCT_STUB(11) MCT_STUB(12) MCT_STUB(13) MCT_STUB(14) MCT_STUB(15) MCT_STUB(16) MCT_STUB(17) MCT_STUB(18) MCT_STUB(19) MCT_STUB(20)
MCT_STUB(21) MCT_STUB(22) MCT_STUB(23) MCT_STUB(24) MCT_STUB(25) MCT_STUB(26) MCT_STUB(27)
#undef MCT_STUB
/* 49630c: logs like the other stubs (EDX and ECX come back unchanged, as the
 * original's do), then links a record at EBP + [descriptor+4] into FS:0
 * with the saved head at record+0 and a zero context word. */
__attribute__((naked)) static void mct_stub_0(void){__asm__ volatile(
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
    "pushl $0\n\tcalll _mct_stub_impl\n\taddl $12,%esp\n\t"
    "popl %ecx\n\tmovl 4(%ecx),%ecx\n\taddl %ebp,%ecx\n\tmovl %eax,(%ecx)\n\tmovw $0,16(%ecx)\n\tmovl %ecx,%fs:0\n\t"
    "popl %edx\n\tpopl %ecx\n\tret\n\t");}
static void *mct_stubs[MCT_IDS]={mct_stub_0,mct_stub_1,mct_stub_2,mct_stub_3,mct_stub_4,mct_stub_5,mct_stub_6,mct_stub_7,mct_stub_8,
    mct_stub_9,mct_stub_10,mct_stub_11,mct_stub_12,mct_stub_13,mct_stub_14,mct_stub_15,mct_stub_16,mct_stub_17,mct_stub_18,mct_stub_19,
    mct_stub_20,mct_stub_21,mct_stub_22,mct_stub_23,mct_stub_24,mct_stub_25,mct_stub_26,mct_stub_27};
static int mct_patch(BYTE saved[MCT_IDS][5],int active[MCT_IDS],uint32_t which,int on){
    for(unsigned i=0;i<MCT_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(mct_addresses[i]-0x400000u,mct_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(mct_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
/* Direct callees of each hook (bit i = mct_addresses[i]). */
static uint32_t mct_which(unsigned index){
    if(index==ES2_HOOK_INDEX_mech_ctor)return 0xffffu;   /* 0 .. 15 */
    if(index==ES2_HOOK_INDEX_flyer_ctor)return MCT_BIT(2)|MCT_BIT(8)|MCT_BIT(12)|MCT_BIT(13)|MCT_BIT(14)|MCT_BIT(15)|MCT_BIT(16);
    if(index==ES2_HOOK_INDEX_building_factory)
        return MCT_BIT(0)|MCT_BIT(3)|MCT_BIT(4)|MCT_BIT(5)|MCT_BIT(6)|MCT_BIT(16)|MCT_BIT(17)|MCT_BIT(18)|MCT_BIT(19);
    if(index==ES2_HOOK_INDEX_group_ctor)return MCT_BIT(5)|MCT_BIT(20)|MCT_BIT(21)|MCT_BIT(22)|MCT_BIT(23);
    return MCT_BIT(24);
}
/* Stubbed in the real-callee passes (the rest of the direct callees and the
 * nested hooks run for real; new, 490b64 and 478838 are stubs below them). */
static uint32_t mct_real_which(unsigned index){
    if(index==ES2_HOOK_INDEX_mech_ctor)
        return MCT_BIT(1)|MCT_BIT(2)|MCT_BIT(3)|MCT_BIT(4)|MCT_BIT(5)|MCT_BIT(9)|MCT_BIT(10)|MCT_BIT(11)|MCT_BIT(12)|MCT_BIT(25)|MCT_BIT(26);
    if(index==ES2_HOOK_INDEX_flyer_ctor)return MCT_BIT(2)|MCT_BIT(12)|MCT_BIT(25)|MCT_BIT(26);
    if(index==ES2_HOOK_INDEX_building_factory)return MCT_BIT(4)|MCT_BIT(5)|MCT_BIT(18)|MCT_BIT(19)|MCT_BIT(25)|MCT_BIT(26)|MCT_BIT(27);
    return MCT_BIT(5)|MCT_BIT(21)|MCT_BIT(22)|MCT_BIT(23);
}
/* The image globals: the actor list header (and the dword a count-word
 * store may reach), the counter pointer, the MECH tables (type table, names,
 * flat pool, shapes, suffixes, the index-byte window), the FLYER table and
 * index window, the building tables and pool, the MECH and FLYER pools, the
 * base layouts and the terrain, script_start's sequence word. */
static const struct { uint32_t rva,size; } mct_ranges[]={{0xa9af8,12},{0x2cbee4,4},{0xa9d64,4},{0xa9d60,4},{0xa968d,4},
    {0xa9698,4},{0xa0a04,8},{0xa9d00,32},{0xa9df4,4},{0xa9dc4,32},{0xa9574,4},{0xa957c,4},{0xa959c,4},{0xa95a0,4},{0xa9584,4},
    {0xa9b7a,4},{0xa9db9,4},{0xa95ac,4},{0xa0be0,4},{0xa9b70,2}};
enum { MCT_GLOBALS=12+4+4+4+4+4+8+32+4+32+4+4+4+4+4+4+4+4+4+2 };
static void mct_globals(BYTE *out,int restore){
    for(unsigned i=0;i<sizeof(mct_ranges)/sizeof(mct_ranges[0]);++i){
        if(restore)memcpy(mapped+mct_ranges[i].rva,out,mct_ranges[i].size);else memcpy(out,mapped+mct_ranges[i].rva,mct_ranges[i].size);
        out+=mct_ranges[i].size;
    }
}
static int mct_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<MCT_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
/* A small signed word: -1, 0 .. limit-1, or (not exact) anything. */
static uint16_t mct_word(unsigned limit,int exact){
    const unsigned k=mct_next()%8;
    if(k==0)return 0xffff;
    if(k==1&&!exact)return (uint16_t)(0x8000u|mct_next());
    return (uint16_t)(mct_next()%limit);
}
/* Factory types per case of the byte table at 405366 (read from the image). */
static uint32_t mct_factory_type(int allow_default){
    for(;;){
        const unsigned k=mct_next()%8;
        uint32_t t=k==0?mct_next()%62u:k==1?34u:k==2?5u+mct_next()%2u:k==3?8u:k==4?45u+mct_next()%7u:mct_next()%62u;
        if(allow_default&&mct_next()%6==0)t=mct_next()%3==0?(uint32_t)(0x3eu+mct_next()%8u):mct_next()%2?10u:(uint32_t)(0xffff0000u|mct_next());
        const unsigned kase=(t&0xffffu)<=0x3du?mapped[0x5366+(t&0xffffu)]:0;
        if(allow_default||kase)return t;
    }
}
/* A real pool at `header` (arena offset) of `used` objects on the used list
 * and `free` free nodes (stride `stride`, links +0/+4, objects at node+8). */
static void mct_real_pool(unsigned header,unsigned nodes,unsigned stride,unsigned used,unsigned free){
    BYTE *H=mct_arena+header;const uint32_t first=MCT_A(nodes);
    put16(H,(uint16_t)free);put16(H+2,(uint16_t)used);put16(H+4,(uint16_t)(stride-8u));put32(H+6,first);
    put32(H+0xe,used?first:0);put32(H+0xa,free?first+stride*used:0);
    for(unsigned k=0;k<used;++k){BYTE *p=mct_arena+nodes+stride*k;put32(p,k+1<used?first+stride*(k+1):0);put32(p+4,k?first+stride*(k-1):0);}
    for(unsigned k=0;k<free;++k){BYTE *p=mct_arena+nodes+stride*(used+k);put32(p,k+1<free?first+stride*(used+k+1):0);put32(p+4,k?first+stride*(used+k-1):0);}
}
/* The actor list header at 4a9af8: the array in the arena, or (not exact)
 * an array whose store hits the count word (the count is reread). */
static void mct_list(int exact){
    const uint16_t count=(uint16_t)(mct_next()%16);
    put16(mapped+0xa9afe,count);
    put32(mapped+0xa9af8,!exact&&mct_next()%8==0?0x4a9afeu-4u*count:MCT_A(MCT_LIST));
}
/* The case data. exact: the real callers' inputs (mech_ctor's ESI = the
 * actor, no factory case 0); real: data for the real-callee passes. */
static void mct_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],uint32_t regs[3],uint32_t *fill,int exact,int real){
    mct_seed=random32();mct_stream=mct_seed^n;mct_real=real;mct_effects=!real&&!exact;mct_index=index;
    for(unsigned i=0;i<3;++i){r[i]=mct_next();regs[i]=mct_next();}
    mct_random(0,MCT_SIZE);memset(a,0,24);
    put32(mapped+0x2cbee4,MCT_A(MCT_COUNTER));mct_list(exact||real);
    *fill=!exact&&!real&&mct_next()%2?mct_next():0;
    if(index==ES2_HOOK_INDEX_mech_ctor){
        const uint32_t A=MCT_A(MCT_OBJ+mct_next()%4);BYTE *P=mct_host(A);mct_obj=A;
        a[0]=A;a[1]=(mct_next()%2?mct_next()<<16:0)|(uint16_t)(mct_next()%32u-8u);
        mct_type_byte=mct_mech_index(a[1]);
        for(unsigned i=0;i<32;++i)mapped[0xa9d00+i]=(BYTE)(mct_next()%3);
        put32(mapped+0xa9d64,MCT_A(MCT_TYPES));
        for(unsigned k=0;k<4;++k){   /* index 0..2, and one more record for 4033a4's table shift */
            BYTE *T=mct_arena+MCT_TYPES+0x21e*k;
            put16(T+0x12,mct_word(4,exact||real));put16(T+0x14,(uint16_t)(mct_next()%4));put16(T+0x1c,mct_word(6,exact||real));
            put16(T+0x24,mct_word(6,exact||real));put16(T+0x54,(uint16_t)(mct_next()%2));put16(T+0x50,(uint16_t)(mct_next()%2));
            put16(T+0x6c,(uint16_t)(mct_next()%8));
            const unsigned flats=mct_next()%5;
            for(unsigned i=0;i<flats;++i)T[0x72+i]=(BYTE)(mct_next()%8);
            T[0x72+flats]=(BYTE)(0x80u|mct_next());
        }
        P[0xa3]=(BYTE)(mct_next()%2?0:1+mct_next()%255);
        if(real||mct_next()%2)put16(P+0x32,0);   /* the shape set-up leaves it 0 in the game */
        put32(P+0x34,MCT_A(MCT_X));put32(mct_arena+MCT_X+4,MCT_A(MCT_Y));put32(mct_arena+MCT_X+0xc,MCT_A(MCT_Y+0x10));
        put32(mct_arena+MCT_Y+0x2a,MCT_A(MCT_Z));put32(mct_arena+MCT_Y+0x3a,MCT_A(MCT_Z+8));
        put32(mct_arena+MCT_Z+6,MCT_A(MCT_ARR));put32(mct_arena+MCT_Z+0xe,MCT_A(MCT_ARR+0x20));
        for(unsigned i=0;i<16;++i)put32(mct_arena+MCT_ARR+4*i,MCT_A(MCT_NODES+0x10*(i%8)));
        put32(mapped+0xa968d,real?MCT_A(MCT_POOLS):mct_next());
        put32(mapped+0xa9698,MCT_A(MCT_SHAPES));put32(mapped+0xa9d60,MCT_A(MCT_NAMES));
        put32(mapped+0xa0a04,mct_next());put32(mapped+0xa0a08,mct_next());
        regs[1]=exact||real?A:regs[1];
        if(real){
            mct_real_pool(MCT_POOLS,MCT_PNODES,0x48,mct_next()%3,8);
            for(unsigned i=0;i<0x70;i+=4)put32(mct_arena+MCT_FOBJ+i,0);
            for(unsigned i=0;i<8;++i)put32(mct_arena+MCT_SHAPES+4*i,0);   /* object_init without a shape instance */
        }
    } else if(index==ES2_HOOK_INDEX_flyer_ctor){
        const uint32_t F=MCT_A(MCT_OBJ+mct_next()%4);mct_obj=F;
        a[0]=F;a[1]=(mct_next()%2?mct_next()<<16:0)|(uint16_t)(mct_next()%32u-8u);
        mct_type_byte=mct_flyer_index(a[1]);
        for(unsigned i=0;i<32;++i)mapped[0xa9dc4+i]=(BYTE)(mct_next()%3);
        put32(mapped+0xa9df4,MCT_A(MCT_FTYPES));
        if(real||mct_next()%2)put16(mct_host(F+0x32),0);   /* object_init leaves it 0 in the game */
        if(real)for(unsigned k=0;k<4;++k)put32(mct_arena+MCT_FTYPES+0x70*k+0x36,0);
    } else if(index==ES2_HOOK_INDEX_building_factory){
        const uint32_t R=MCT_A(MCT_REC);mct_obj=MCT_A(MCT_OBJ);
        const uint32_t t=mct_factory_type(!exact&&!real);
        put16(mct_host(R),(uint16_t)t);
        /* The record's word +32h; the real passes avoid 0 (its path reads
         * the shape instance [object+34h], which the real shape set-ups
         * leave null without a shape). */
        static const uint16_t words[]={0,0,0,1,50,99,100,0xffff,0x8000,0x7fff};
        put16(mct_host(R+0x32),real?words[3+mct_next()%7]:words[mct_next()%10]);
        put32(mapped+0xa959c,MCT_A(MCT_BT)-60u*(uint32_t)(int32_t)(int16_t)t);
        BYTE *T=mct_arena+MCT_BT;
        put16(T+2,(uint16_t)(mct_next()%8));put16(T+4,mct_word(8,exact||real));put16(T+6,(uint16_t)(mct_next()%4-1u));
        put16(T+0x12,(uint16_t)(mct_next()%7));put32(T+0x14,MCT_A(MCT_BCFG));put16(T+0x1e,(uint16_t)(mct_next()%2));
        for(unsigned i=0;i<8;++i)put16(mct_arena+MCT_BCFG+0x1e*i+2,mct_word(16,exact||real));
        put32(mapped+0xa9574,MCT_A(MCT_SHAPES));put32(mapped+0xa957c,MCT_A(MCT_SHAPES+0x20));put32(mapped+0xa9584,MCT_A(MCT_MODELS));
        put32(mapped+0xa95a0,real?MCT_A(MCT_POOLS):mct_next());
        put32(mct_arena+MCT_OBJ+0x34,MCT_A(MCT_SHAPEI));put32(mct_arena+MCT_SHAPEI+8,MCT_A(MCT_FRAMES));
        a[0]=R;a[1]=mct_next();
        regs[2]=MCT_A(MCT_OBJ);   /* EDI: the object of case 0 */
        if(real){
            mct_real_pool(MCT_POOLS,MCT_PNODES,0x280,0,1);
            for(unsigned i=0;i<16;++i)put32(mct_arena+MCT_SHAPES+4*i,0);
        }
    } else if(index==ES2_HOOK_INDEX_group_ctor){
        BYTE *R=mct_arena+MCT_GREC;mct_obj=MCT_A(MCT_GROUP);
        static const uint16_t classes[]={0,1,2,0,1,2,3,0xffff};
        put16(R+0x28,real?(uint16_t)(mct_next()%3):mct_next()%8?classes[mct_next()%8]:(uint16_t)mct_next());
        for(unsigned i=0;i<20;++i)put16(R+0x32+2*i,mct_word(real?5:8,exact||real));
        for(unsigned i=0;i<10;++i)put32(mct_arena+MCT_ORDERS+4*i,mct_next()%3?MCT_A(MCT_ORDREC+0x10*i):0);
        a[0]=MCT_A(MCT_GROUP);a[1]=MCT_A(MCT_GREC);a[2]=mct_next();a[3]=MCT_A(MCT_ORDERS);a[4]=mct_next();a[5]=mct_next();
        put32(mapped+0xa9b7a,mct_next());put32(mapped+0xa9db9,mct_next());put32(mapped+0xa95a0,mct_next());
        if(real){
            mct_real_pool(MCT_POOLS,MCT_PNODES,0x48,mct_next()%6,1);mct_real_pool(MCT_POOLS+0x14,MCT_PNODES+0x48*7,0x48,mct_next()%5,0);
            mct_real_pool(MCT_POOLS+0x28,MCT_PNODES+0x48*12,0x48,mct_next()%6,2);
            put32(mapped+0xa9b7a,MCT_A(MCT_POOLS));put32(mapped+0xa9db9,MCT_A(MCT_POOLS+0x14));put32(mapped+0xa95a0,MCT_A(MCT_POOLS+0x28));
        }
    } else {
        const uint32_t B=MCT_A(MCT_OBJ+mct_next()%4);mct_obj=B;
        put32(mct_host(B+0x45),MCT_A(MCT_GROUP));
        const uint16_t k=mct_word(4,exact);
        put16(mct_arena+MCT_GROUP+2,k);
        put32(mapped+0xa95ac,MCT_A(MCT_BASES)-28u*(uint32_t)(int32_t)(int16_t)k);
        put32(mct_arena+MCT_BASES+8,mct_next()%4==0?0xffffffffu:mct_next());
        put32(mapped+0xa0be0,mct_next());
        a[0]=B;
    }
    mct_stream=mct_seed;   /* the stubs' stream restarts here in every run */
}
/* Calls target with six argument slots, EAX/EDX/ECX = r and EBX/ESI/EDI =
 * regs over 3,840 bytes of stack filled with `fill`; out receives EAX, EDX
 * and ECX after the call. */
__attribute__((naked,noinline)) static void mct_invoke(void *target,const uint32_t *a,const uint32_t *r,const uint32_t *regs,uint32_t fill,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 24(%ebp),%eax\n\tmovl $1024,%ecx\n\tcld\n\trep stosl\n\taddl $3840,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 20(%esi)\n\tpushl 16(%esi)\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 20(%ebp),%edi\n\tmovl (%edi),%ebx\n\tmovl 4(%edi),%esi\n\tmovl 8(%edi),%edi\n\t"
        "movl 16(%ebp),%eax\n\tmovl 4(%eax),%edx\n\tmovl 8(%eax),%ecx\n\tmovl (%eax),%eax\n\tcall *8(%ebp)\n\t"
        "movl 28(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* The core with the adapter's graph and scratch; FS:0 restored to the
 * record's saved head as the adapters do. */
static uint64_t mct_core(unsigned index,const uint32_t *a,const uint32_t *r,const uint32_t *regs,uint32_t *ecx){
    const es2_guest_memory *m=&math_native_memory;
    if(index==ES2_HOOK_INDEX_mech_ctor){
        BYTE scratch[ES2_MECH_CTOR_SCRATCH]={0};es2_weapon_graph g=weapons_graph(mapped,scratch,index);
        weapons_impact_context c={(uint32_t)(uintptr_t)mapped,(uint32_t)(uintptr_t)(scratch+ES2_MECH_CTOR_FRAME)};
        g.call=weapons_impact_guest_call;g.context=&c;
        uint32_t head=0;
        const uint64_t v=es2_mech_ctor_registers(m,&g,a[0],a[1],regs[1],r,&head);
        __asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");
        if(ecx)*ecx=head;
        return v;
    }
    if(index==ES2_HOOK_INDEX_flyer_ctor){
        BYTE scratch[ES2_FLYER_CTOR_SCRATCH]={0};es2_weapon_graph g=weapons_graph(mapped,scratch,index);
        return es2_flyer_ctor_registers(m,&g,a[0],a[1],r,ecx);
    }
    if(index==ES2_HOOK_INDEX_building_factory){
        BYTE scratch[ES2_BUILDING_FACTORY_SCRATCH]={0};es2_weapon_graph g=weapons_graph(mapped,scratch,index);
        weapons_impact_context c={(uint32_t)(uintptr_t)mapped,(uint32_t)(uintptr_t)(scratch+ES2_BUILDING_FACTORY_FRAME)};
        g.call=weapons_impact_guest_call;g.context=&c;
        const uint64_t v=es2_building_factory_registers(m,&g,a[0],regs[2],r,ecx);
        const uint32_t head=(uint32_t)(v>>32);__asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");
        return v;
    }
    es2_weapon_graph g=weapons_graph(mapped,NULL,index);
    if(index==ES2_HOOK_INDEX_group_ctor)return es2_group_ctor_registers(m,&g,a[0],a[1],a[2],a[3],a[4],a[5],r,ecx);
    return es2_base_marks_registers(m,&g,a[0],r,ecx);
}
static int mct_report(const char *what,unsigned n,const uint32_t want[3],const uint32_t got[3],unsigned calls,const mct_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x calls %u/%u\n",current_function,n,what,
        want[0],got[0],want[1],got[1],want[2],got[2],calls,mct_calls);
    for(unsigned j=0;j<calls&&j<mct_calls&&expected;++j)if(memcmp(expected+j,mct_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(mct_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<MCT_SIZE;++j)if(mct_after[j]!=mct_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,mct_after[j],mct_arena[j]);break;}
    return 1;
}
/* The memset calls of the core (not stubbed): one in mech_ctor, flyer_ctor
 * and group_ctor. */
static unsigned mct_expected_hits(unsigned index,const callee_counter *c){
    if(c->key==0x49681au)return index==ES2_HOOK_INDEX_building_factory||index==ES2_HOOK_INDEX_base_marks?0:1;
    unsigned hits=0;
    for(unsigned j=0;j<mct_calls;++j)if(mct_addresses[mct_log[j].id]==c->key)++hits;
    return hits;
}
/* One direct case: the original, the core and (exact inputs) the adapter. */
static int mct_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r,const uint32_t *regs,uint32_t fill,int exact,int check_ecx){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[MCT_GLOBALS],after[MCT_GLOBALS],actual[MCT_GLOBALS];static mct_record expected[MCT_MAX];uint32_t want[3],got[3];
    const uint32_t head=mct_fs0();
    memcpy(mct_before,mct_arena,MCT_SIZE);mct_globals(before,0);
    mct_reset();mct_invoke(mapped+d->rva,a,r,regs,fill,want);const unsigned calls=mct_calls;memcpy(expected,mct_log,calls*sizeof(*expected));
    memcpy(mct_after,mct_arena,MCT_SIZE);mct_globals(after,0);
    if(mct_fs0()!=head)return fail("mission_ctors original left FS:0 changed");
    for(unsigned j=0;j<calls;++j)++mct_paths[expected[j].id];
    memcpy(mct_arena,mct_before,MCT_SIZE);mct_globals(before,1);mct_reset();
    got[2]=~want[2];
    uint64_t pair=mct_core(index,a,r,regs,&got[2]);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);mct_globals(actual,0);
    if(mct_fs0()!=head)return fail("mission_ctors core left FS:0 changed");
    if(want[0]!=got[0]||want[1]!=got[1]||(check_ecx&&want[2]!=got[2])||calls!=mct_calls||memcmp(expected,mct_log,calls*sizeof(*expected))||
       memcmp(mct_after,mct_arena,MCT_SIZE)||mct_globals_differ(after,actual))
        return mct_report("core",n,want,got,calls,expected);
    if(!exact){mct_globals(before,1);return 0;}
    /* the adapter (the framework's entry: six argument slots) */
    memcpy(mct_arena,mct_before,MCT_SIZE);mct_globals(before,1);mct_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[32];
    if(count>32)return fail("mission_ctors counter table too long");
    for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair=x->replacement_context(a,d->user,r);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);got[2]=want[2];
    mct_globals(actual,0);
    if(mct_fs0()!=head)return fail("mission_ctors adapter left FS:0 changed");
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=mct_expected_hits(index,c+i))return fail("mission_ctors callee counter delta");
    if(want[0]!=got[0]||want[1]!=got[1]||calls!=mct_calls||memcmp(expected,mct_log,calls*sizeof(*expected))||
       memcmp(mct_after,mct_arena,MCT_SIZE)||mct_globals_differ(after,actual))
        return mct_report("adapter",n,want,got,calls,expected);
    mct_globals(before,1);
    return 0;
}
static int mct_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],regs[3],fill,want[3]={0},got[3]={0},pattern=poison?0xa7a7a7a7u:0x31313131u;
        mct_build(index,n*37u+11u,a,r,regs,&fill,1,0);
        const uint32_t entry[3]={pattern,pattern,pattern};const uint32_t head=mct_fs0();
        BYTE before[MCT_GLOBALS],after[MCT_GLOBALS],actual[MCT_GLOBALS],expected_fpu[108],actual_fpu[108];static mct_record expected[MCT_MAX];
        memcpy(mct_before,mct_arena,MCT_SIZE);mct_globals(before,0);
        mct_reset();mct_invoke(mapped+d->rva,a,entry,regs,pattern,want);es2_hook_fpu_save(expected_fpu);
        const unsigned calls=mct_calls;memcpy(expected,mct_log,calls*sizeof(*expected));memcpy(mct_after,mct_arena,MCT_SIZE);mct_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("mission_ctors poison install");
        memcpy(mct_arena,mct_before,MCT_SIZE);mct_globals(before,1);mct_reset();mct_invoke(mapped+d->rva,a,entry,regs,pattern,got);
        es2_hook_fpu_save(actual_fpu);mct_globals(actual,0);
        if(mct_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||calls!=mct_calls||memcmp(expected,mct_log,calls*sizeof(*expected))||
           memcmp(mct_after,mct_arena,MCT_SIZE)||mct_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||
           memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=mct_report(fast?"installed fast":"installed preserving",n,want,got,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("mission_ctors poison uninstall");mct_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, pair/trace/arena/globals/FS:0\n",d->name,total);
    return status;
}
/* ---- real callees ---- */
enum { MCT_NESTED=6 };
static unsigned mct_nested(unsigned index,unsigned nested[MCT_NESTED]){
    unsigned count=0;
#define MCT_N(name) nested[count++]=ES2_HOOK_INDEX_##name
    if(index==ES2_HOOK_INDEX_mech_ctor){MCT_N(pool_alloc);MCT_N(object_init);MCT_N(script_start);MCT_N(euler_matrix);MCT_N(matrix_transpose);MCT_N(rotate_point);}
    else if(index==ES2_HOOK_INDEX_flyer_ctor){MCT_N(object_init);MCT_N(script_start);MCT_N(euler_matrix);MCT_N(matrix_transpose);MCT_N(rotate_point);}
    else if(index==ES2_HOOK_INDEX_building_factory){MCT_N(pool_alloc);MCT_N(object_init);}
    else if(index==ES2_HOOK_INDEX_group_ctor){MCT_N(pool_nth);MCT_N(pool_prev);MCT_N(pool_next);}
#undef MCT_N
    return count;
}
static int mct_real_pass(unsigned index){
    unsigned nested[MCT_NESTED];const unsigned count=mct_nested(index,nested);
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[MCT_NESTED+1]={0};
    BYTE saved[MCT_IDS][5];int active[MCT_IDS];
    if(mct_patch(saved,active,mct_real_which(index),1))return 1;
    static mct_record expected[MCT_MAX];
    const unsigned modes=2u<<count;
    for(unsigned mode=0;mode<modes&&!status;++mode)for(unsigned n=0;n<4096u/modes&&!status;++n){
        uint32_t a[6],r[3],regs[3],fill,want[3]={0},got[3]={0},pattern=n&1?0xa7a7a7a7u:0x31313131u;
        mct_build(index,n*13u+mode,a,r,regs,&fill,1,1);
        const uint32_t entry[3]={pattern,pattern,pattern};const uint32_t head=mct_fs0();
        BYTE before[MCT_GLOBALS],after[MCT_GLOBALS],actual[MCT_GLOBALS];
        memcpy(mct_before,mct_arena,MCT_SIZE);mct_globals(before,0);
        es2_hook hk[MCT_NESTED+1];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(mode>>(1+j)&1)?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,function_hook_extension(nested[j])))return fail("mission_ctors nested install");}
        mct_reset();mct_invoke(mapped+d->rva,a,entry,regs,pattern,want);
        const unsigned calls=mct_calls;memcpy(expected,mct_log,calls*sizeof(*expected));
        memcpy(mct_after,mct_arena,MCT_SIZE);mct_globals(after,0);
        o.mode=mode&1?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(hk+MCT_NESTED,d,&o,function_hook_extension(index)))return fail("mission_ctors caller install");
        memcpy(mct_arena,mct_before,MCT_SIZE);mct_globals(before,1);mct_reset();
        mct_invoke(mapped+d->rva,a,entry,regs,pattern,got);mct_globals(actual,0);
        if(mct_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||calls!=mct_calls||memcmp(expected,mct_log,calls*sizeof(*expected))||
           memcmp(mct_after,mct_arena,MCT_SIZE)||mct_globals_differ(after,actual)){
            fprintf(stderr,"real mode=%u\n",mode);status=mct_report("real",n,want,got,calls,expected);
        }
        for(unsigned j=0;j<=MCT_NESTED;++j){if(!hk[j].descriptor)continue;entered[j]+=hk[j].calls;
            if(hk[j].mismatches)status=fail("mission_ctors real compare mismatch");
            if(!es2_hook_uninstall(hk+j))return fail("mission_ctors real uninstall");}
        mct_globals(before,1);++total;
    }
    if(mct_patch(saved,active,0,0))return 1;
    mct_real=0;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&!entered[MCT_NESTED]){fprintf(stderr,"FAIL: %s never entered\n",d->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, %u nested hooks original/replaced, hook original/replaced, 31/a7 stack, pair, calls, arena, globals and FS:0\n",
        d->name,total,count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
static int mission_ctors_cases(unsigned index,unsigned limit){
    current_function=function_hook_descriptor(index)->name;
    const uint32_t which=mct_which(index);
    BYTE saved[MCT_IDS][5],old_globals[MCT_GLOBALS];int active[MCT_IDS];
    mct_globals(old_globals,0);
    if(mct_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_MISSION_CTORS_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);memset(mct_paths,0,sizeof(mct_paths));
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3],regs[3],fill;const int exact=limit||!(n&1);
        mct_build(index,n,a,r,regs,&fill,exact,0);status=mct_compare(index,n,a,r,regs,fill,exact&&!limit,!limit);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    mct_globals(old_globals,1);
    if(!limit&&!status)status=mct_installed_probes(index,64);
    if(mct_patch(saved,active,which,0))return 1;
    mct_globals(old_globals,1);
    if(!limit&&!status&&mct_nested(index,(unsigned[MCT_NESTED]){0}))status=mct_real_pass(index);
    mct_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases (stub calls 49630c %u, 4033a4 %u, 402374 %u, new[] %u, pool_alloc %u, object_init %u, "
        "script_start %u, 40d354 %u, euler %u, 403368 %u, 40332c %u, track_rate %u, track_seek %u, pool_nth %u, joins %u/%u/%u, 47136c %u); "
        "full return pair and ECX, ordered calls with entry registers, slots and context word, arena/globals/FS:0\n",
        function_hook_descriptor(index)->name,cases,mct_paths[0],mct_paths[1],mct_paths[4],mct_paths[5],mct_paths[6],mct_paths[7],mct_paths[8],
        mct_paths[12],mct_paths[13],mct_paths[16],mct_paths[17],mct_paths[18],mct_paths[19],mct_paths[20],mct_paths[21],mct_paths[22],mct_paths[23],
        mct_paths[24]);
    return status;
}
