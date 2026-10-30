/* mission_load group: pool_create 471ad8 (with its initializer 471a50),
 * pool_nth 471cb0, subject_resolve 425348, action_copy 411b90, condition_init
 * 415068, region_init 423358, timer_init 423104, group_array 423d38 and
 * initial_damage 4178e8 (docs/reverse-engineering/mission_load.md, "Pieces L1
 * to L3 and the snapshot noise model").
 *
 * Direct cases run the original body (EAX/EDX/ECX at entry from the case;
 * EAX/EDX/ECX after it captured) and the core (with the ECX it reports)
 * against logging stubs for the game callees of the hook under test: new[]
 * 4774f0 (a block in the arena), pool_prev 471c70 (a scripted object
 * list), pool_nth 471cb0, timer_arm 467ac4, the runtime's array
 * construction 49667c, multiply_q8 47e0a0, rng_next 492ee0 (scripted
 * values), random_damage 417a04 and the actor's virtual +74h (a stub in an
 * arena vtable); memset 49681a is the harness's own CW3220 memset. Compared:
 * the full return pair, ECX, the ordered calls with their argument slots
 * (and the bridge's slot count) and entry EAX/EDX/ECX, the arena span of
 * the hook and the image globals (group array 4a9e00, the three pool
 * pointers, the three remap tables, RNG stream A). The stubs change what
 * the originals read after a call (the timer words after timer_arm, the
 * group array record after 49667c, the table pointer after multiply_q8,
 * the vtable, type record and type word after the virtual call), and the
 * objects overlap in some cases (a pool header inside its block, region
 * and record, timer and words, the condition tables, list and words), so
 * the order of reads and writes is observable. Negative pool capacities
 * fault in the original (memset of a negative size, or the link write at
 * 0000FFF8h) and are not generated (PROVED edge of 471aa3).
 *
 * Then installed 31/a7 poison probes and real-callee passes (the hook
 * under test original or replaced against a run without it, the nested
 * hooks in the same modes in both): pool_nth over the real
 * pool_prev/pool_next on a pool
 * in the arena, subject_resolve over the real pool_nth, pool_prev and
 * pool_next on three pools at the image's pool pointers, timer_init over
 * the real timer_arm, group_array over the real 49667c and element
 * constructor 4240e4 (new[] stubbed), initial_damage over the real
 * rng_next, rng_bounded, multiply_q8, random_damage and part_group_max
 * (stream A seeded per case, the virtual +74h stubbed), each nested hook
 * in rotating original/replaced/compare modes (compare falls back to
 * replaced where refused) and the hook under test original or replaced.
 * Identifiers use the prefix mld_/MLD_. */
typedef struct mld_record { uint32_t id,slots,args[5],regs[3],eax,edx; } mld_record;
enum {
    MLD_SIZE=0x3000, MLD_SMALL=0x1400, MLD_MAX=40, MLD_IDS=9,
    MLD_H=0x40, MLD_OBJ=0x100, MLD_TAB=0x400, MLD_TAB2=0x480, MLD_T=0x500, MLD_TB=0x520, MLD_LIST=0x740, MLD_ITEMS=0x7a0,
    MLD_W=0x900, MLD_VT=0xa00, MLD_VT2=0xa80, MLD_TYPE=0xb00, MLD_TYPE2=0xb80, MLD_PARTS=0xc00, MLD_POOL=0xc80,
    MLD_NODES=0xd00, MLD_REGION=0x1000, MLD_REC=0x1040, MLD_TIMER=0x1100, MLD_WORDS=0x1180, MLD_GROUPS=0x1200,
    MLD_POOLS=0x1240, MLD_BLOCK=0x1400,
    MLD_GLOBALS=16+100+50+140+0x72
};
#define MLD_B ((uint32_t)(uintptr_t)mld_arena)
#define MLD_A(o) (MLD_B+(uint32_t)(o))
#define MLD_BIT(i) (1u<<(i))
static BYTE mld_arena[MLD_SIZE],mld_before[MLD_SIZE],mld_after[MLD_SIZE];
static mld_record mld_log[MLD_MAX];
static unsigned mld_calls,mld_span=MLD_SIZE,mld_list_count,mld_effects;
static uint32_t mld_seed,mld_stream,mld_block,mld_list[8];
__attribute__((used)) static uint32_t mld_ecx;
/* 0 new[] 4774f0, 1 pool_prev 471c70, 2 pool_nth 471cb0, 3 timer_arm
 * 467ac4, 4 array construction 49667c, 5 multiply_q8 47e0a0, 6 rng_next
 * 492ee0, 7 random_damage 417a04 (patched); 8 the actor's virtual +74h
 * (an arena vtable entry). The slots each original call site pushes. */
static const uint32_t mld_addresses[MLD_IDS]={0x4774f0,0x471c70,0x471cb0,0x467ac4,0x49667c,0x47e0a0,0x492ee0,0x417a04,0};
static const unsigned mld_counts[MLD_IDS]={1,2,2,2,5,2,1,3,4};
static uint32_t mld_next(void){
    uint32_t z=mld_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static int32_t mld_s16(uint32_t x){return (int32_t)(int16_t)(uint16_t)x;}
static void mld_reset(void){mld_calls=0;mld_stream=mld_seed;}
/* One callee side effect in about one call of four (mld_effects on). */
static void mld_effect(unsigned id,const uint32_t *a){
    uint32_t pick=mld_next();
    if(!mld_effects||pick%4)return;
    switch(id){
    case 3: /* timer_arm: a word the copy reads next. */
        put32(mld_arena+MLD_WORDS+4*(pick/4%10),mld_next());break;
    case 4: /* 49667c: the group record's array pointer or count. */
        if(pick&0x100)put32(mld_arena+MLD_GROUPS,mld_next());else put16(mld_arena+MLD_GROUPS+4,(uint16_t)mld_next());break;
    case 5: /* multiply_q8: the table pointer [A+206h] is read after it. */
        put32(mld_arena+MLD_OBJ+0x206,MLD_A(pick&0x100?MLD_TAB2:MLD_TAB+2));break;
    case 8: /* the virtual: vtable, type record, type word. */
        switch(pick/4%3){
        case 0:put32(mld_arena+MLD_OBJ,MLD_A(MLD_VT2));break;
        case 1:put32(mld_arena+MLD_OBJ+0x1f2,MLD_A(MLD_TYPE2));break;
        default:put16(mld_arena+MLD_TYPE+0x4a,(uint16_t)(mld_next()%5));break;
        }
        break;
    default:(void)a;break;
    }
}
__attribute__((used,noinline)) static uint64_t __cdecl mld_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(mld_calls==MLD_MAX){fail("mission_load call overflow");ExitProcess(1);}
    mld_record *v=mld_log+mld_calls++;memset(v,0,sizeof(*v));v->id=id;
    v->slots=weapons_bridge_slots(a,mld_counts[id]);
    for(unsigned k=0;k<mld_counts[id];++k)v->args[k]=a[k];
    for(unsigned j=0;j<3;++j)v->regs[j]=regs[j];
    uint32_t eax=mld_next(),edx=mld_next();mld_ecx=mld_next();
    if(id==0)eax=mld_block;
    else if(id==1){
        /* pool_prev(pool, current): the object after current in the list. */
        unsigned i=0;
        if(!a[1])eax=mld_list_count?mld_list[0]:0;
        else {while(i<mld_list_count&&mld_list[i]!=a[1])++i;eax=i+1<mld_list_count?mld_list[i+1]:0;}
    }
    mld_effect(id,a);
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define MLD_STUB(n) __attribute__((naked)) static void mld_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _mld_stub_impl\n\taddl $24,%esp\n\tmovl _mld_ecx,%ecx\n\tret\n\t");}
MLD_STUB(0) MLD_STUB(1) MLD_STUB(2) MLD_STUB(3) MLD_STUB(4) MLD_STUB(5) MLD_STUB(6) MLD_STUB(7) MLD_STUB(8)
#undef MLD_STUB
static void *mld_stubs[MLD_IDS]={mld_stub_0,mld_stub_1,mld_stub_2,mld_stub_3,mld_stub_4,mld_stub_5,mld_stub_6,mld_stub_7,mld_stub_8};
/* Direct callees of each hook (bit i = mld_addresses[i]; 8 is never patched). */
static uint32_t mld_which(unsigned index){
    if(index==ES2_HOOK_INDEX_pool_create)return MLD_BIT(0);
    if(index==ES2_HOOK_INDEX_pool_nth)return MLD_BIT(1);
    if(index==ES2_HOOK_INDEX_subject_resolve)return MLD_BIT(2);
    if(index==ES2_HOOK_INDEX_timer_init)return MLD_BIT(3);
    if(index==ES2_HOOK_INDEX_group_array)return MLD_BIT(4);
    if(index==ES2_HOOK_INDEX_initial_damage)return MLD_BIT(5)|MLD_BIT(6)|MLD_BIT(7);
    return 0;
}
static int mld_patch(BYTE saved[MLD_IDS][5],int active[MLD_IDS],uint32_t which,int on){
    for(unsigned i=0;i<MLD_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!mld_addresses[i])continue;if(actor_patch(mld_addresses[i]-0x400000u,mld_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(mld_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
/* The group array, the MECH/FLYER/building pool pointers, the three remap
 * tables and RNG stream A. */
static const struct { uint32_t rva,size; } mld_ranges[]={{0xa9e00,4},{0xa9b7a,4},{0xa9db9,4},{0xa95a0,4},
    {0xaa78e,100},{0xaa888,50},{0xaa946,140},{0xd2599,0x72}};
static void mld_globals(BYTE *out,int restore){
    for(unsigned i=0;i<sizeof(mld_ranges)/sizeof(mld_ranges[0]);++i){
        if(restore)memcpy(mapped+mld_ranges[i].rva,out,mld_ranges[i].size);else memcpy(out,mapped+mld_ranges[i].rva,mld_ranges[i].size);
        out+=mld_ranges[i].size;
    }
}
static int mld_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<MLD_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
static void mld_random(unsigned offset,unsigned size){for(unsigned i=0;i<size;++i)mld_arena[offset+i]=(BYTE)mld_next();}
/* A word: small, a boundary, or anything. */
static uint16_t mld_word(uint32_t small){
    static const uint16_t edges[]={0,1,0xffff,0x7fff,0x8000,0x8001,0x7ffe,100,99,101};
    unsigned k=mld_next()%8;return k==0?edges[mld_next()%10]:k==1?(uint16_t)mld_next():(uint16_t)(mld_next()%small);
}
/* The condition structure: C = A+206h with [C] = TAB2, [C+4] = TAB, [C+0Ch]
 * = T, [T] = TB (29 rows of nine words; +6 a count of 0..2, +8 the items,
 * each a dword whose word +2 is a list index 0..21), [T+4] = LIST (24
 * words). real: values that keep random_damage's real path in range. */
static void mld_conditions(int real){
    BYTE *A=mld_arena+MLD_OBJ;
    put32(A+0x206,MLD_A(MLD_TAB2));put32(A+0x20a,MLD_A(MLD_TAB));put32(A+0x212,MLD_A(MLD_T));
    put32(mld_arena+MLD_T,MLD_A(MLD_TB));put32(mld_arena+MLD_T+4,MLD_A(MLD_LIST));
    for(unsigned r=0;r<29;++r){
        BYTE *row=mld_arena+MLD_TB+18*r;
        put16(row,real?(uint16_t)(mld_next()%400):mld_word(2000));
        put16(row+6,(uint16_t)(mld_next()%3));put32(row+8,MLD_A(MLD_ITEMS+12*r));
        for(unsigned k=0;k<3;++k)put16(mld_arena+MLD_ITEMS+12*r+4*k+2,(uint16_t)(mld_next()%22));
    }
    for(unsigned i=0;i<24;++i)put16(mld_arena+MLD_LIST+2*i,real?(uint16_t)(mld_next()%400):mld_word(2000));
}
static void mld_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    mld_seed=random32();mld_stream=mld_seed^n;memset(a,0,24);mld_effects=1;
    for(unsigned i=0;i<3;++i)r[i]=mld_next();
    mld_span=index==ES2_HOOK_INDEX_pool_create?MLD_SIZE:MLD_SMALL;
    memset(mld_arena,(BYTE)mld_seed,MLD_SIZE);
    if(index==ES2_HOOK_INDEX_pool_create){
        /* Capacity 0, small or up to 40; size -8 .. what fits, or one of
         * the game's object sizes. */
        static const uint16_t sizes[]={0x36a,0x291,0x26d,0x0e,0x5b,0x61,0x4c,0x2a,0x41,0x59,0x66};
        const uint32_t room=MLD_SIZE-MLD_BLOCK-16;
        const unsigned k=mld_next()%16;
        const uint32_t c=k==0?0:k<6?1+mld_next()%4:1+mld_next()%40;
        const uint32_t most=c?room/c:room;   /* the largest stride */
        uint32_t s=0;
        switch(mld_next()%6){
        case 0:s=(uint32_t)(-(int32_t)(1+mld_next()%8))&0xffffu;break;
        case 1:{const uint16_t g=sizes[mld_next()%11];s=g+8u<=most?g:most-8u;break;}
        default:s=most>=8?mld_next()%(most-7):0;break;
        }
        const uint32_t total=c*((uint32_t)mld_s16(s)+8u);
        mld_block=MLD_A(MLD_BLOCK+4*(mld_next()%3));
        if(mld_block+total>MLD_A(MLD_SIZE))mld_block=MLD_A(MLD_BLOCK);
        a[0]=MLD_A(MLD_H);
        if(total>=0x20&&mld_next()%8==0)a[0]=mld_block+mld_next()%(total-0x12);   /* the header inside its block */
        a[1]=(mld_next()%3?0:mld_next()<<16)|c;a[2]=(mld_next()%3?0:mld_next()<<16)|s;
        mld_random(MLD_H,0x20);
    } else if(index==ES2_HOOK_INDEX_pool_nth){
        mld_list_count=mld_next()%7;
        for(unsigned i=0;i<mld_list_count;++i)mld_list[i]=MLD_A(MLD_NODES+0x40*i+8);
        static const uint32_t indices[]={0,1,2,3,5,6,7,0xffff,0x8000,0xfffe};
        a[0]=MLD_A(MLD_POOL);a[1]=(mld_next()%4?indices[mld_next()%10]:mld_next()%16)|(mld_next()%2?mld_next()<<16:0);
    } else if(index==ES2_HOOK_INDEX_subject_resolve){
        static const uint32_t scopes[]={0,1,2,3,4,5,0xffffffffu,0x10001u};
        const uint32_t scope=mld_next()%8?scopes[mld_next()%6]:scopes[mld_next()%8];
        const unsigned limit=scope==1?100:scope==2?50:140;
        uint32_t index16=mld_next()%4?mld_next()%limit:mld_next()%3?(uint32_t)mld_s16(mld_next()):(uint32_t)-(int32_t)(1+mld_next()%64);
        if(scope==0&&mld_next()%2)index16=(index16&0xffffu)|(mld_next()<<16);
        a[0]=index16;a[1]=scope;
        for(unsigned i=0;i<100;++i)mapped[0xaa78e + i]=(BYTE)mld_next();
        for(unsigned i=0;i<50;++i)mapped[0xaa888+i]=(BYTE)mld_next();
        for(unsigned i=0;i<140;++i)mapped[0xaa946+i]=(BYTE)mld_next();
        put32(mapped+0xa9e00,mld_next());put32(mapped+0xa9b7a,mld_next());put32(mapped+0xa9db9,mld_next());put32(mapped+0xa95a0,mld_next());
    } else if(index==ES2_HOOK_INDEX_action_copy){
        /* The words apart, or overlapping the destination A+1BAh..1E1h. */
        a[0]=MLD_A(MLD_OBJ+mld_next()%4);
        mld_random(MLD_OBJ+0x1b0,0x40);mld_random(MLD_W,0x80);
        const unsigned layout=mld_next()%6;
        a[1]=layout==1?a[0]+0x1ba+2*(mld_next()%20):MLD_A(MLD_W+mld_next()%3);
        a[2]=layout==2?a[0]+0x1ba+2*(mld_next()%20)-0x14:layout==3?a[1]+2*(mld_next()%12)-8:MLD_A(MLD_W+0x40+mld_next()%3);
    } else if(index==ES2_HOOK_INDEX_condition_init){
        mld_conditions(0);
        for(unsigned i=0;i<40;++i)put16(mld_arena+MLD_W+2*i,mld_next()%3?(uint16_t)(mld_next()%101):mld_word(200));
        mld_random(MLD_TAB,0x40);mld_random(MLD_TAB2,0x40);
        BYTE *A=mld_arena+MLD_OBJ;
        a[0]=MLD_A(MLD_OBJ+0x206);a[1]=MLD_A(MLD_W);a[2]=MLD_A(MLD_W+0x20);a[3]=MLD_A(MLD_W+0x40);
        switch(mld_next()%8){
        case 0: /* table 1 runs over the list: later rows read written words */
            put32(A+0x20a,MLD_A(MLD_LIST)-2*(mld_next()%16));break;
        case 1: /* table 2 over the third block of words */
            put32(A+0x206,a[3]-2*(mld_next()%12));break;
        case 2: /* the third block over table 1's tail: the third loop reads
                 * words the first loop wrote */
            a[3]=MLD_A(MLD_TAB)+2*(mld_next()%20);break;
        case 3: /* table 2 over the list (the third loop rereads it) */
            put32(A+0x206,MLD_A(MLD_LIST)+24-2*(mld_next()%12));break;
        default:break;
        }
    } else if(index==ES2_HOOK_INDEX_region_init){
        a[1]=MLD_A(MLD_REC);
        a[0]=mld_next()%4?MLD_A(MLD_REGION):a[1]+(uint32_t)((int32_t)(mld_next()%21)-10);
        put16(mld_arena+MLD_REC,mld_next()%2?0:mld_word(4));put16(mld_arena+MLD_REC+2,mld_word(400));put16(mld_arena+MLD_REC+4,mld_word(400));
        a[2]=mld_next()%3?MLD_A(MLD_BLOCK):mld_next();
    } else if(index==ES2_HOOK_INDEX_timer_init){
        mld_random(MLD_TIMER,0x40);mld_random(MLD_WORDS,0x40);
        a[0]=MLD_A(MLD_TIMER);a[1]=mld_next();a[3]=mld_next();
        const unsigned layout=mld_next()%6;
        a[2]=layout==0?a[0]+4*(1+mld_next()%4):layout==1?a[0]+4-4*(1+mld_next()%4):MLD_A(MLD_WORDS);
    } else if(index==ES2_HOOK_INDEX_group_array){
        mld_random(MLD_GROUPS,0x10);
        a[0]=MLD_A(MLD_GROUPS);a[1]=mld_next()%2?mld_next()%64:mld_next();
    } else {
        /* initial_damage: the damage word around every threshold; the
         * actor's vtable, type record and condition structure. */
        static const uint16_t words[]={0xffff,0,1,19,20,21,39,40,41,59,60,61,79,80,81,0x7fff,0x8000,10,30,50,70};
        mld_conditions(0);
        BYTE *A=mld_arena+MLD_OBJ;
        put32(A,MLD_A(MLD_VT));put32(mld_arena+MLD_VT+0x74,(uint32_t)(uintptr_t)mld_stubs[8]);
        put32(mld_arena+MLD_VT2+0x74,(uint32_t)(uintptr_t)mld_stubs[8]);
        put32(A+0x1f2,MLD_A(MLD_TYPE));put16(mld_arena+MLD_TYPE+0x4a,(uint16_t)(mld_next()%5));
        put16(mld_arena+MLD_TYPE2+0x4a,mld_next()%2?(uint16_t)(mld_next()%5):(uint16_t)mld_next());
        mld_random(MLD_TAB,0x40);mld_random(MLD_TAB2,0x40);
        a[0]=MLD_A(MLD_OBJ);
        a[1]=(mld_next()%2?mld_next()<<16:0)|(mld_next()%4?words[mld_next()%21]:(uint16_t)(mld_next()%100));
    }
    mld_stream=mld_seed;   /* the stubs' stream restarts here in both runs */
}
/* Calls target with five argument slots and EAX/EDX/ECX = r; out receives
 * EAX, EDX and ECX after the call. */
__attribute__((naked,noinline)) static void mld_invoke(void *target,const uint32_t *a,const uint32_t *r,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl 12(%ebp),%esi\n\tpushl 16(%esi)\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* The core with the graph of the adapter (observer = the hook's counters)
 * and the ECX the original returns. */
static uint64_t mld_core(unsigned index,const uint32_t *a,const uint32_t *r,uint32_t *ecx){
    es2_weapon_graph g=weapons_graph(mapped,NULL,index);
    const es2_guest_memory *m=&math_native_memory;
    if(index==ES2_HOOK_INDEX_pool_create)return es2_pool_create_registers(m,&g,a[0],a[1],a[2],r,ecx);
    if(index==ES2_HOOK_INDEX_pool_nth)return es2_pool_nth_registers(m,&g,a[0],a[1],r,ecx);
    if(index==ES2_HOOK_INDEX_subject_resolve)return es2_subject_resolve_registers(m,&g,a[0],a[1],r,ecx);
    if(index==ES2_HOOK_INDEX_action_copy)return es2_action_copy_registers(m,a[0],a[1],a[2],ecx);
    if(index==ES2_HOOK_INDEX_condition_init)return es2_condition_init_registers(m,a[0],a[1],a[2],a[3],ecx);
    if(index==ES2_HOOK_INDEX_region_init)return es2_region_init_registers(m,a[0],a[1],a[2],ecx);
    if(index==ES2_HOOK_INDEX_timer_init)return es2_timer_init_registers(m,&g,a[0],a[1],a[2],a[3],r,ecx);
    if(index==ES2_HOOK_INDEX_group_array)return es2_group_array_registers(m,&g,a[0],a[1],r,ecx);
    return es2_initial_damage_registers(m,&g,a[0],a[1],r,ecx);
}
static int mld_report(const char *what,unsigned n,const uint32_t want[3],const uint32_t got[3],unsigned calls,const mld_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x calls %u/%u\n",current_function,n,what,
        want[0],got[0],want[1],got[1],want[2],got[2],calls,mld_calls);
    for(unsigned j=0;j<calls&&j<mld_calls&&expected;++j)if(memcmp(expected+j,mld_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(mld_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<mld_span;++j)if(mld_after[j]!=mld_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,mld_after[j],mld_arena[j]);break;}
    return 1;
}
static unsigned mld_expected_hits(const callee_counter *c){
    unsigned hits=0;
    if(c->event==ES2_COLLIDE_CALL&&c->key==0x49681au)return 1;   /* pool_create's memset (not stubbed): once */
    for(unsigned j=0;j<mld_calls;++j){
        const unsigned id=mld_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?id==8&&c->key==0x74:id<8&&mld_addresses[id]==c->key)++hits;
    }
    return hits;
}
static unsigned mld_paths[MLD_IDS];
/* check_ecx: the core's ECX against the original's (not in the installed
 * pass, where the replaced hook stands in for the original and the
 * framework returns the entry ECX). */
static int mld_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r,int check_ecx){
    const es2_hook_descriptor *d=function_hook_descriptor(index);
    BYTE before[MLD_GLOBALS],after[MLD_GLOBALS],actual[MLD_GLOBALS];static mld_record expected[MLD_MAX];uint32_t want[3],got[3];
    memcpy(mld_before,mld_arena,mld_span);mld_globals(before,0);
    mld_reset();mld_invoke(mapped+d->rva,a,r,want);const unsigned calls=mld_calls;memcpy(expected,mld_log,calls*sizeof(*expected));
    memcpy(mld_after,mld_arena,mld_span);mld_globals(after,0);
    for(unsigned j=0;j<calls;++j)++mld_paths[expected[j].id];
    memcpy(mld_arena,mld_before,mld_span);mld_globals(before,1);mld_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[4];
    if(count>4)return fail("mission_load counter table too long");
    for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    /* initial_damage returns its local [EBP-4] in ECX, written only on the
     * 20..39 path (uninitialized otherwise): ECX is compared there only. */
    if(index==ES2_HOOK_INDEX_initial_damage&&(uint16_t)(a[1]-20u)>=20u)check_ecx=0;
    got[2]=want[2];
    const uint64_t pair=mld_core(index,a,r,&got[2]);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);mld_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=mld_expected_hits(c+i))return fail("mission_load callee counter delta");
    if(want[0]!=got[0]||want[1]!=got[1]||(check_ecx&&want[2]!=got[2])||calls!=mld_calls||memcmp(expected,mld_log,calls*sizeof(*expected))||
       memcmp(mld_after,mld_arena,mld_span)||mld_globals_differ(after,actual))
        return mld_report("direct",n,want,got,calls,expected);
    mld_globals(before,1);
    return 0;
}
static int mld_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],want[3]={0},got[3]={0},pattern=poison?0xa7a7a7a7u:0x31313131u;mld_build(index,n*37u+11u,a,r);
        BYTE before[MLD_GLOBALS],after[MLD_GLOBALS],actual[MLD_GLOBALS],expected_fpu[108],actual_fpu[108];static mld_record expected[MLD_MAX];
        memcpy(mld_before,mld_arena,mld_span);mld_globals(before,0);
        mld_reset();r3_poison_call(mapped+d->rva,a,pattern,want);es2_hook_fpu_save(expected_fpu);
        const unsigned calls=mld_calls;memcpy(expected,mld_log,calls*sizeof(*expected));memcpy(mld_after,mld_arena,mld_span);mld_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("mission_load poison install");
        memcpy(mld_arena,mld_before,mld_span);mld_globals(before,1);mld_reset();r3_poison_call(mapped+d->rva,a,pattern,got);
        es2_hook_fpu_save(actual_fpu);mld_globals(actual,0);
        if(want[0]!=got[0]||want[1]!=got[1]||calls!=mld_calls||memcmp(expected,mld_log,calls*sizeof(*expected))||memcmp(mld_after,mld_arena,mld_span)||
           mld_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=mld_report(fast?"installed fast":"installed preserving",n,want,got,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("mission_load poison uninstall");mld_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, pair/trace/arena/globals\n",d->name,total);
    return status;
}
/* ---- real callees ---- */
enum { MLD_NESTED=7 };
static unsigned mld_nested(unsigned index,unsigned nested[MLD_NESTED]){
    unsigned count=0;
#define MLD_N(name) nested[count++]=ES2_HOOK_INDEX_##name
    if(index==ES2_HOOK_INDEX_pool_nth){MLD_N(pool_prev);MLD_N(pool_next);}
    else if(index==ES2_HOOK_INDEX_subject_resolve){MLD_N(pool_nth);MLD_N(pool_prev);MLD_N(pool_next);}
    else if(index==ES2_HOOK_INDEX_timer_init){MLD_N(timer_arm);}
    else if(index==ES2_HOOK_INDEX_initial_damage){MLD_N(rng_next);MLD_N(multiply_q8);MLD_N(random_damage);MLD_N(rng_bounded);MLD_N(part_group_max);}
#undef MLD_N
    return count;
}
/* A real pool at `header` (arena offset) with `used` objects of 0x30 bytes
 * on the used list (links at node+0/+4, objects at node+8) and `free`
 * nodes on the free list; nodes at `nodes`. */
static void mld_real_pool(unsigned header,unsigned nodes,unsigned used,unsigned free){
    BYTE *H=mld_arena+header;const uint32_t first=MLD_A(nodes),stride=0x38;
    put16(H,(uint16_t)free);put16(H+2,(uint16_t)used);put16(H+4,0x30);put32(H+6,first);
    put32(H+0xe,used?first:0);put32(H+0xa,free?first+stride*used:0);
    for(unsigned k=0;k<used;++k){BYTE *p=mld_arena+nodes+stride*k;put32(p,k+1<used?first+stride*(k+1):0);put32(p+4,k?first+stride*(k-1):0);}
    for(unsigned k=0;k<free;++k){BYTE *p=mld_arena+nodes+stride*(used+k);put32(p,k+1<free?first+stride*(used+k+1):0);put32(p+4,k?first+stride*(used+k-1):0);}
}
static void mld_real_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    mld_build(index,n,a,r);mld_effects=0;
    if(index==ES2_HOOK_INDEX_group_array)mld_span=MLD_SIZE;   /* the elements the real construction writes */
    if(index==ES2_HOOK_INDEX_pool_nth){
        mld_real_pool(MLD_POOL,MLD_NODES,mld_next()%6,mld_next()%3);a[0]=MLD_A(MLD_POOL);
        a[1]=(mld_next()%2?mld_next()<<16:0)|(mld_next()%8?mld_next()%7:0xffff);
    } else if(index==ES2_HOOK_INDEX_subject_resolve){
        /* Three pools after the nodes area; the image pointers at them. */
        mld_real_pool(MLD_POOLS,MLD_NODES,mld_next()%6,1);mld_real_pool(MLD_POOLS+0x14,MLD_NODES+0x38*7,mld_next()%4,0);
        mld_real_pool(MLD_POOLS+0x28,MLD_NODES+0x38*12,mld_next()%5,2);
        put32(mapped+0xa9b7a,MLD_A(MLD_POOLS));put32(mapped+0xa9db9,MLD_A(MLD_POOLS+0x14));put32(mapped+0xa95a0,MLD_A(MLD_POOLS+0x28));
        if(a[1]-1u<3u){const unsigned limit=a[1]==1?100:a[1]==2?50:140;a[0]=mld_next()%limit;}
        for(unsigned i=0;i<100;++i)mapped[0xaa78e + i]=(BYTE)(mld_next()%8?mld_next()%7:0xff);
        for(unsigned i=0;i<50;++i)mapped[0xaa888+i]=(BYTE)(mld_next()%8?mld_next()%5:0xff);
        for(unsigned i=0;i<140;++i)mapped[0xaa946+i]=(BYTE)(mld_next()%8?mld_next()%6:0xff);
    } else if(index==ES2_HOOK_INDEX_initial_damage){
        /* Real random_damage: parts [A+20Eh], the condition structure in
         * range, stream A seeded (indices 0..55). */
        mld_conditions(1);
        put32(mld_arena+MLD_OBJ+0x20e,MLD_A(MLD_PARTS));
        for(unsigned i=0;i<29;++i)put16(mld_arena+MLD_PARTS+2*i,mld_next()%3?(uint16_t)(1+mld_next()%300):0);
        for(unsigned i=0;i<0x70;++i)mapped[0xd2599+i]=(BYTE)mld_next();
        mapped[0xd2599+0x70]=(BYTE)(mld_next()%56);mapped[0xd2599+0x71]=(BYTE)(mld_next()%56);
    }
}
static int mld_real_pass(unsigned index){
    unsigned nested[MLD_NESTED];const unsigned count=mld_nested(index,nested);
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[MLD_NESTED+1]={0};
    BYTE saved[MLD_IDS][5];int active[MLD_IDS];
    /* group_array keeps new[] stubbed (the real 49667c allocates). */
    if(mld_patch(saved,active,index==ES2_HOOK_INDEX_group_array?MLD_BIT(0):0,1))return 1;
    if(index==ES2_HOOK_INDEX_group_array)mld_block=MLD_A(MLD_BLOCK);
    static mld_record expected[MLD_MAX];
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],want[2],got[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;mld_real_build(index,n*13u+mode,a,r);
        if(index==ES2_HOOK_INDEX_group_array)a[1]=mld_next()%40;   /* the real construction writes count x 7Ah bytes */
        BYTE before[MLD_GLOBALS],after[MLD_GLOBALS],actual[MLD_GLOBALS];
        memcpy(mld_before,mld_arena,mld_span);mld_globals(before,0);
        es2_hook hk[MLD_NESTED+1];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("mission_load nested install");}
        mld_reset();r3_poison_call(mapped+d->rva,a,pattern,want);
        const unsigned calls=mld_calls;memcpy(expected,mld_log,calls*sizeof(*expected));
        memcpy(mld_after,mld_arena,mld_span);mld_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+MLD_NESTED,d,&o,function_hook_extension(index)))return fail("mission_load caller install");
        memcpy(mld_arena,mld_before,mld_span);mld_globals(before,1);mld_reset();
        r3_poison_call(mapped+d->rva,a,pattern,got);mld_globals(actual,0);
        if(want[0]!=got[0]||want[1]!=got[1]||calls!=mld_calls||memcmp(expected,mld_log,calls*sizeof(*expected))||memcmp(mld_after,mld_arena,mld_span)||
           mld_globals_differ(after,actual)){
            fprintf(stderr,"FAIL: %s case %u (real mode=%u) pair %08x%08x/%08x%08x calls %u/%u\n",d->name,n,mode,want[1],want[0],got[1],got[0],calls,mld_calls);
            for(unsigned j=0;j<mld_span;++j)if(mld_after[j]!=mld_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,mld_after[j],mld_arena[j]);break;}
            status=1;
        }
        for(unsigned j=0;j<=MLD_NESTED;++j){if(j<count||j==MLD_NESTED)entered[j]+=hk[j].calls;
            if(hk[j].mismatches){fprintf(stderr,"FAIL: %s case %u (real mode=%u) compare mismatch in %s\n",d->name,n,mode,hk[j].descriptor->name);status=1;}
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("mission_load nested uninstall");}
        mld_globals(before,1);
        ++total;
    }
    if(mld_patch(saved,active,0,0))return 1;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&!entered[MLD_NESTED]){fprintf(stderr,"FAIL: %s never entered\n",d->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, hook original/replaced, 31/a7 stack, pair, memory, globals, stub trace\n",
        d->name,total,count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
/* Compare probes (compare_probes.h) for the six hooks that accept compare: real
 * callees (the pools of the real pass for pool_nth and subject_resolve, the
 * real timer_arm), and layouts a live load passes (no argument inside a span;
 * compare skips such a call): action_copy's ids and actions apart from A's
 * arrays, region_init's record apart from R, timer_init's words apart from T.
 * condition_init keeps every layout (its tables are in-place regions). */
static void mld_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    if(index==ES2_HOOK_INDEX_pool_nth||index==ES2_HOOK_INDEX_subject_resolve){mld_real_build(index,n,a,r);return;}
    mld_build(index,n,a,r);mld_effects=0;
    if(index==ES2_HOOK_INDEX_action_copy){a[1]=MLD_A(MLD_W+mld_next()%3);a[2]=MLD_A(MLD_W+0x40+mld_next()%3);}
    else if(index==ES2_HOOK_INDEX_region_init){a[0]=MLD_A(MLD_REGION);if(a[2]-a[0]<16u)a[2]=MLD_A(MLD_BLOCK);}
    else if(index==ES2_HOOK_INDEX_timer_init)a[2]=MLD_A(MLD_WORDS);
}
static void mld_compare_state(BYTE *s,int restore){
    if(restore){memcpy(mld_arena,s,MLD_SIZE);mld_globals(s+MLD_SIZE,1);}else{memcpy(s,mld_arena,MLD_SIZE);mld_globals(s+MLD_SIZE,0);}
}
static int mission_load_cases(unsigned index,unsigned limit){
    current_function=function_hook_descriptor(index)->name;
    const uint32_t which=mld_which(index);
    BYTE saved[MLD_IDS][5],old_globals[MLD_GLOBALS];int active[MLD_IDS];
    mld_globals(old_globals,0);
    if(limit&&compare_probe_installed(index)){
        const int status=compare_probe_run(index,limit,mld_compare_build,mld_compare_state,MLD_SIZE+MLD_GLOBALS);
        mld_globals(old_globals,1);return status;
    }
    if(mld_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_MISSION_LOAD_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);memset(mld_paths,0,sizeof(mld_paths));
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];mld_build(index,n,a,r);status=mld_compare(index,n,a,r,!limit);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    mld_globals(old_globals,1);
    if(!limit&&!status)status=mld_installed_probes(index,64);
    if(mld_patch(saved,active,which,0))return 1;
    mld_globals(old_globals,1);
    if(!limit&&!status&&(mld_nested(index,(unsigned[MLD_NESTED]){0})||index==ES2_HOOK_INDEX_group_array))status=mld_real_pass(index);
    mld_globals(old_globals,1);
    if(!limit&&!status&&compare_probe_capable(index))status=compare_probe_run(index,0,mld_compare_build,mld_compare_state,MLD_SIZE+MLD_GLOBALS);
    mld_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases (stub calls new %u, pool_prev %u, pool_nth %u, timer_arm %u, 49667c %u, multiply_q8 %u, rng_next %u, random_damage %u, +74h %u); "
        "full return pair and ECX, ordered calls with entry registers and slots, arena/globals\n",function_hook_descriptor(index)->name,cases,
        mld_paths[0],mld_paths[1],mld_paths[2],mld_paths[3],mld_paths[4],mld_paths[5],mld_paths[6],mld_paths[7],mld_paths[8]);
    return status;
}
