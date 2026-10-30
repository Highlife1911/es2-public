/* fit_leaves group: weapon_shape_word 40e61c, mount_entry 40fea0, mount_table
 * 420634, shield_split 413ac8, energy_rate 417d08, equip_slots 40fb2c and
 * node_state_fill 417530 (docs/reverse-engineering/mission_load.md, "The
 * remaining weapon_fit callees").
 *
 * Direct cases run the original body, the adapter and the core on the same
 * generated state (an arena of 10 KiB with random bytes; the actor, the
 * weapons and their definitions, the set and slot arrays, the shield
 * record, the node tree with its child lists and word arrays) against
 * logging stubs for every game callee: the memset thunk 49681a (the stub
 * clears the buffer like the runtime), multiply_q10 47e0b0, part_ratio
 * 40dbc0 (the stub returns a planned condition word), mount_entry 40fea0
 * and the shape node's virtual slot 0 (an arena vtable; the stub returns the
 * class id planned at node+40h). The stubs log the call (ordered arguments,
 * the slot count the caller pushed, entry EAX/EDX/ECX and a hash of the
 * arena at the call, so writes before and after a call are ordered), return
 * registers from a per-case stream and, in some cases, poke a word that the
 * original reads again after the call (the weapon pointer A+313h after the
 * flag multiply, the second record word of shield_split, a node's child
 * count or array count after a class call). Compared: EDX:EAX of the
 * original, the adapter and the core, ECX of the original and the core
 * (equip_slots: the low word, the high word is stack residue; node_state_fill:
 * only when the root class is 140015h, otherwise [EBP-4] is residue), the
 * logged calls of all three, the arena and the globals (the mount table
 * pointer 4a98e4 and the word 4a9d70) after each run, and the callee counter
 * deltas of the adapter. The set arrays and the record of equip_slots are
 * aliased in some cases (the record over the slot array or the set); node
 * arrays alias the node's own fields only with a value whose low word keeps
 * the loop short. Then installed 31/a7 poison probes and a real-callee pass
 * for the three callers with game callees (mount_table over the real
 * mount_entry, shield_split over the real multiply_q10, energy_rate over the
 * real multiply_q10 and part_ratio, built part tables), the nested hooks in
 * rotating original/replaced/compare modes and the hook original or
 * replaced. Prefix fit_/FIT_. */
typedef struct fit_record { uint32_t id,args[3],regs[3],hash,count; } fit_record;
enum { FIT_SIZE=0x2800, FIT_A=0x000, FIT_W=0x400, FIT_W2=0x440, FIT_D=0x480, FIT_D2=0x4c0, FIT_E=0x500, FIT_S=0x520, FIT_S2=0x540,
    FIT_R=0x560, FIT_SLOTS=0x5a0, FIT_WEAPONS=0x600, FIT_DEFS=0xa00, FIT_VT=0x1200, FIT_LISTS=0x1210, FIT_ARRS=0x1340,
    FIT_NODES=0x1540, FIT_NODE_COUNT=16, FIT_P=0x1d40, FIT_MAX=96, FIT_IDS=5, FIT_GLOBALS=6 };
#define FIT_BASE ((uint32_t)(uintptr_t)fit_arena)
static BYTE fit_arena[FIT_SIZE],fit_before[FIT_SIZE],fit_after[FIT_SIZE];
static fit_record fit_log[FIT_MAX];
static unsigned fit_calls,fit_poke_at,fit_nodes_used,fit_budget;
static uint32_t fit_seed,fit_stream,fit_poke_address,fit_poke_value,fit_poke_size,fit_condition;
static int fit_root_x;
__attribute__((used)) static uint32_t fit_ecx;
/* Stub ids: 0 memset 49681a, 1 multiply_q10 47e0b0, 2 part_ratio 40dbc0, 3
 * mount_entry 40fea0, 4 the shape node's slot 0 (arena vtable). */
static const uint32_t fit_addresses[FIT_IDS]={0x49681a,0x47e0b0,0x40dbc0,0x40fea0,0};
static const unsigned fit_argc[FIT_IDS]={3,2,2,1,1};
static uint32_t fit_next(void){
    uint32_t z=fit_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static uint32_t fit_hash(void){
    uint32_t h=0x811c9dc5u;
    for(unsigned i=0;i<FIT_SIZE;i+=4)h=(h^get32(fit_arena+i))*0x01000193u;
    return h;
}
__attribute__((used,noinline)) static uint64_t __cdecl fit_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(fit_calls==FIT_MAX){fail("fit_leaves synthetic call overflow");ExitProcess(1);}
    fit_record *v=fit_log+fit_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,4*fit_argc[id]);memcpy(v->regs,regs,12);
    v->count=weapons_bridge_slots(a,fit_argc[id]);v->hash=fit_hash();
    uint32_t eax=fit_next(),edx=fit_next();fit_ecx=fit_next();
    if(id==0)memset((BYTE *)(uintptr_t)a[0],0,a[2]<64?a[2]:64);
    else if(id==2)eax=(eax&0xffff0000u)|(fit_condition&0xffffu);
    else if(id==4)eax=get32((const BYTE *)(uintptr_t)a[0]+0x40);
    if(fit_poke_address&&fit_poke_at==fit_calls-1){
        if(fit_poke_size==2)put16((BYTE *)(uintptr_t)fit_poke_address,(uint16_t)fit_poke_value);
        else put32((BYTE *)(uintptr_t)fit_poke_address,fit_poke_value);
    }
    return (uint64_t)edx<<32|eax;
}
#define FIT_STUB(n) __attribute__((naked)) static void fit_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _fit_stub_impl\n\taddl $24,%esp\n\tmovl _fit_ecx,%ecx\n\tret\n\t");}
FIT_STUB(0)
FIT_STUB(1)
FIT_STUB(2)
FIT_STUB(3)
FIT_STUB(4)
#undef FIT_STUB
static void (*const fit_stubs[FIT_IDS])(void)={fit_stub_0,fit_stub_1,fit_stub_2,fit_stub_3,fit_stub_4};
static void fit_globals(BYTE *out,int restore){
    if(restore){memcpy(mapped+0xa98e4,out,4);memcpy(mapped+0xa9d70,out+4,2);}
    else{memcpy(out,mapped+0xa98e4,4);memcpy(out+4,mapped+0xa9d70,2);}
}
static void fit_reset(void){fit_calls=0;fit_stream=fit_seed;}
/* Patch the stubbed callees; keep[i] leaves one unpatched (the real callee,
 * or the function under test). */
static int fit_patch(BYTE saved[FIT_IDS][5],int on,const int *keep){
    for(unsigned i=0;i<FIT_IDS;++i){
        if(!fit_addresses[i]||(keep&&keep[i]))continue;
        if(actor_patch(fit_addresses[i]-0x400000u,on?(void *)fit_stubs[i]:NULL,saved[i],on?TRUE:FALSE))return 1;
    }
    return 0;
}
static void fit_keep(unsigned index,int keep[FIT_IDS],int real){
    memset(keep,0,FIT_IDS*sizeof(int));
    if(index==ES2_HOOK_INDEX_mount_entry)keep[3]=1;           /* the function under test */
    if(real){keep[1]=keep[2]=keep[3]=1;}                       /* real multiply_q10, part_ratio, mount_entry */
}
static uint16_t fit_word(const uint16_t *values,unsigned count){return random32()%4?values[random32()%count]:(uint16_t)random32();}
static BYTE *fit_at(uint32_t offset){return fit_arena+offset;}
/* The part tables of the real part_ratio (vehicle A+206h): X (current
 * words), G (group words), the record array and the maxima M, the item
 * lists; groups 0..27, parts 0..63. Every maximum is positive, so the
 * IDIV never faults. */
static void fit_part_tables(uint32_t actor_offset){
    const uint32_t b=FIT_BASE,P=FIT_P,X=P,G=P+0x80,TAB=P+0x100,RECS=P+0x140,M=P+0x340,ITEMS=P+0x3c0;
    BYTE *rec=fit_at(actor_offset+0x206);
    put32(rec,b+X);put32(rec+4,b+G);put32(rec+0xc,b+TAB);put32(fit_at(TAB),b+RECS);put32(fit_at(TAB+4),b+M);
    for(unsigned i=0;i<64;++i){
        put16(fit_at(X+2*i),(uint16_t)(random32()%4==0?0x8000u|random32()%1000u:random32()%1000u));
        put16(fit_at(M+2*i),(uint16_t)(1+random32()%1000));
        put16(fit_at(G+2*i),(uint16_t)(random32()%4==0?0x8000u|random32()%1000u:random32()%2000u));
    }
    for(unsigned g=0;g<28;++g){
        BYTE *e=fit_at(RECS+18*g);
        put16(e,(uint16_t)(1+random32()%2000));put16(e+6,(uint16_t)(random32()%4));put32(e+8,b+ITEMS+12*g);
        for(unsigned k=0;k<3;++k)put16(fit_at(ITEMS+12*g+4*k+2),(uint16_t)(random32()%64));
    }
}
static uint32_t fit_class(int recursing){
    static const uint32_t recursive[]={0x140007u,0x140008u,0x14000bu,0x14000cu,0x140015u};
    static const uint32_t others[]={0x140005u,0x140006u,0x140009u,0x14000au,0x14000du,0x140013u,0x140014u,0x140016u,0x1e0003u,
        0x1e0004u,0x1e0005u,0x1e0006u,0u,1u,0xffffffffu,0x80000000u,0x7fffffffu,0x140017u,0x13ffffu};
    if(recursing||random32()%3==0)return recursive[random32()%5];
    return random32()%6==0?random32():others[random32()%(sizeof(others)/sizeof(others[0]))];
}
/* One node (index i, 0 = the sentinel with class 140005h): vtable, planned
 * class, child count and list (four valid entries; unused ones and the
 * sentinel's point to the sentinel), array count and array. */
static uint32_t fit_gen(unsigned depth,uint32_t value_small){
    const uint32_t b=FIT_BASE;const unsigned i=fit_nodes_used++;
    BYTE *N=fit_at(FIT_NODES+0x80u*i);
    put32(N,b+FIT_VT);put32(N+0x40,fit_class(depth==0&&random32()%2));
    put16(N+0x10,0);put16(N+0x16,(uint16_t)(random32()%7));put32(N+0x1c,b+FIT_ARRS+0x20u*i);
    const unsigned c=random32()%5;uint32_t entries[4]={b+FIT_NODES,b+FIT_NODES,b+FIT_NODES,b+FIT_NODES};
    for(unsigned j=0;j<c;++j)if(depth<3&&fit_budget>0){--fit_budget;entries[j]=fit_gen(depth+1,value_small);}
    put16(N+0x10,(uint16_t)c);put32(N+0x12,b+FIT_LISTS+16u*i);
    for(unsigned j=0;j<4;++j)put32(fit_at(FIT_LISTS+16u*i+4u*j),entries[j]);
    if(value_small&&random32()%4==0)put32(N+0x1c,b+FIT_NODES+0x80u*i+0x16u);   /* the array starts at the node's own array count word */
    return b+FIT_NODES+0x80u*i;
}
/* One state for hook index (case n). real: valid inputs for the real
 * callees and no pokes. */
static void fit_build(unsigned index,unsigned n,int real,uint32_t a[6],uint32_t r[3]){
    static const uint16_t words[]={0,1,2,3,0x7fff,0x8000,0xffff,0xfffe,0x100,0x400,0x1234,0xe0,0xe1,0x33};
    static const uint32_t classes[]={0x12,0x13,0x1c,0x1d,0x1e,0x1f,0x20,0x21,0x11,0x22,0x10,0xffff,0x8012,0x8020,0};
    const uint32_t b=FIT_BASE;
    fit_seed=random32();random_bytes(fit_arena,FIT_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    fit_poke_address=0;fit_poke_at=0;fit_poke_value=random32();fit_poke_size=random32()%2?2:4;fit_condition=random32();fit_root_x=0;
    random_bytes(mapped+0xa98e4,4);random_bytes(mapped+0xa9d70,2);
    if(random32()%4==0)put16(mapped+0xa9d70,fit_word(words,14));
    BYTE *A=fit_at(FIT_A);
    if(index==ES2_HOOK_INDEX_weapon_shape_word){
        BYTE *W=fit_at(FIT_W);a[0]=b+FIT_W;
        const int self=random32()%8==0;       /* [W+18h] = W+18h: the word is the pointer's low half */
        put32(W+0x18,self?b+FIT_W+0x18:b+FIT_D+random32()%8u);
        if(!self)put16((BYTE *)(uintptr_t)get32(W+0x18),fit_word(words,14));
    } else if(index==ES2_HOOK_INDEX_mount_entry){
        static const uint32_t edges[]={0,1,0xffff,0x8000,0x7fff,0x10000,0xffff8000u,0xfffffffeu,0x12345,0xffff0001u};
        a[0]=n<10?edges[n]:random32()%2?random32():(random32()&0xffff0000u)|fit_word(words,14);
    } else if(index==ES2_HOOK_INDEX_mount_table){
        a[0]=b+FIT_A;put32(A+0x1f2,b+FIT_E+random32()%4u);
        put16((BYTE *)(uintptr_t)get32(A+0x1f2),fit_word(words,14));
        if(random32()%16==0)put32(A+0x1f2,b+FIT_A+0x1f2u-2u+random32()%5u);        /* the record over the pointer */
    } else if(index==ES2_HOOK_INDEX_shield_split){
        const uint32_t S=b+FIT_S2+random32()%4u;a[0]=S;
        put16((BYTE *)(uintptr_t)S+4,fit_word(words,14));put16((BYTE *)(uintptr_t)S+6,fit_word(words,14));
        if(!real&&random32()%3==0){fit_poke_at=0;fit_poke_address=S+6;fit_poke_size=2;}
    } else if(index==ES2_HOOK_INDEX_energy_rate){
        static const uint32_t conditions[]={0,1,0x50,0xe0,0xe1,0xdf,0xffff,0x8000,0x7fff,0xff9c,0x33,0x32,0x66,0xcc,0x21a,0x1};
        static const BYTE sides[]={0,1,2,3,5,8,0xfd,0xfe,0xff,0x7f,0x80,0x40};
        a[0]=b+FIT_A;
        A[0xaa]=(BYTE)(random32()%2?0:random32()|1u);A[0xab]=(BYTE)(random32()%2?0:random32()|1u);
        const int weapon=random32()%4!=0;
        put32(A+0x313,weapon?b+FIT_W:0);
        put32(fit_at(FIT_W)+0x18,b+FIT_D);put32(fit_at(FIT_W2)+0x18,b+FIT_D2);
        fit_at(FIT_D)[0x17]=real?(BYTE)(random32()%12-3):sides[random32()%12];
        fit_at(FIT_D2)[0x17]=(BYTE)random32();
        if(real)fit_part_tables(FIT_A);
        else {
            fit_condition=random32()%2?conditions[random32()%16]:random32();
            if(random32()%8==0)fit_condition=(random32()&0xffff0000u)|(51u*(random32()%5)+random32()%2*50u);
            if(random32()%4==0){fit_poke_at=0;fit_poke_address=b+FIT_A+0x313u;fit_poke_size=4;fit_poke_value=random32()%2?0:b+FIT_W2;}
        }
    } else if(index==ES2_HOOK_INDEX_equip_slots){
        a[0]=b+FIT_S;
        const unsigned k=random32()%4,count=n<6?n*3u:k==0?random32()%3u:random32()%17u;
        put32(fit_at(FIT_S)+4,random32()%10==0?0:b+FIT_SLOTS);put16(fit_at(FIT_S)+8,(uint16_t)count);
        if(n==3)put16(fit_at(FIT_S)+8,0);
        for(unsigned i=0;i<16;++i){
            const unsigned w=random32()%16;
            put32(fit_at(FIT_SLOTS+4*i),random32()%4==0?0:b+FIT_WEAPONS+0x40u*w);
            put32(fit_at(FIT_WEAPONS+0x40u*i+0x1c),b+FIT_DEFS+0x80u*i);
            put16(fit_at(FIT_DEFS+0x80u*i+0x56),random32()%6==0?(uint16_t)random32():(uint16_t)classes[random32()%15]);
        }
        switch(random32()%8){
        case 0:a[1]=b+FIT_SLOTS+4u*(random32()%8);break;                /* over the slot array */
        case 1:a[1]=b+FIT_S+random32()%12u;break;                       /* over the set record */
        default:a[1]=b+FIT_R+random32()%4u;break;
        }
    } else {
        /* node_state_fill: a tree below the root (node 1) with the sentinel
         * node 0; the root class is planned from the case number. */
        static const uint32_t roots[]={0x140007u,0x140008u,0x14000bu,0x14000cu,0x140015u,0x140014u,0x140005u,0x140006u,0x140009u,
            0x14000au,0x14000du,0x1e0003u,0x1e0004u,0x1e0005u,0x1e0006u,0u,0x140013u,0x140016u};
        fit_nodes_used=0;fit_budget=FIT_NODE_COUNT-2;
        const int small=n%8==5;
        BYTE *S=fit_at(FIT_NODES);put32(S,b+FIT_VT);put32(S+0x40,0x140005u);put16(S+0x10,0);put16(S+0x16,0);
        put32(S+0x1c,b+FIT_ARRS);put32(S+0x12,b+FIT_LISTS);
        for(unsigned j=0;j<4;++j)put32(fit_at(FIT_LISTS+4u*j),b+FIT_NODES);
        ++fit_nodes_used;
        const uint32_t root=fit_gen(0,small);
        if(n<18){put32((BYTE *)(uintptr_t)root+0x40,roots[n]);}
        fit_root_x=get32((BYTE *)(uintptr_t)root+0x40)==0x140015u;
        a[0]=root;a[1]=small?(random32()&0xffff0000u)|(random32()%4u):random32();
        if(small)put16(fit_at(FIT_NODES+0x80u+0x16),3);
        if(!real&&random32()%3==0){
            /* A non-leaf node's child count or array count after a class call. */
            const unsigned node=1+random32()%(fit_nodes_used-1);
            fit_poke_at=random32()%4;fit_poke_size=2;fit_poke_value=random32()%5;
            fit_poke_address=b+FIT_NODES+0x80u*node+(random32()%2?0x10u:0x16u);
        }
    }
    put32(fit_at(FIT_VT),(uint32_t)(uintptr_t)fit_stub_4);
}
/* The original with the four argument slots and EAX/EDX/ECX = r; out
 * receives EAX, EDX and ECX after the call. */
__attribute__((naked,noinline)) static void fit_invoke(void *target,const uint32_t *a,const uint32_t *r,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl 12(%ebp),%esi\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static uint64_t fit_core(unsigned index,const uint32_t *a,const uint32_t *r,uint32_t *ecx){
    const es2_guest_memory *m=&math_native_memory;es2_weapon_graph g=weapons_graph(mapped,NULL,index);
    if(index==ES2_HOOK_INDEX_weapon_shape_word)return es2_weapon_shape_word_registers(m,a[0],r,ecx);
    if(index==ES2_HOOK_INDEX_mount_entry)return es2_mount_entry_registers(m,&g,a[0],r,ecx);
    if(index==ES2_HOOK_INDEX_mount_table)return es2_mount_table_registers(m,&g,a[0],r,ecx);
    if(index==ES2_HOOK_INDEX_shield_split)return es2_shield_split_registers(m,&g,a[0],r,ecx);
    if(index==ES2_HOOK_INDEX_energy_rate)return es2_energy_rate_registers(m,&g,a[0],r,ecx);
    if(index==ES2_HOOK_INDEX_equip_slots)return es2_equip_slots_registers(m,&g,a[0],a[1],r,ecx);
    return es2_node_state_fill_registers(m,&g,a[0],a[1],r,ecx);
}
static uint32_t fit_ecx_mask(unsigned index){
    if(index==ES2_HOOK_INDEX_equip_slots)return 0xffffu;
    if(index==ES2_HOOK_INDEX_node_state_fill)return fit_root_x?0xffffffffu:0u;
    return 0xffffffffu;
}
static int fit_report(const char *what,unsigned n,const uint64_t pair[3],unsigned calls,const fit_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],(unsigned long long)pair[2],calls,fit_calls);
    for(unsigned j=0;j<calls&&j<fit_calls&&expected;++j)if(memcmp(expected+j,fit_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(fit_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u word%u %08x/%08x\n",j,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<FIT_SIZE;++j)if(fit_after[j]!=fit_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,fit_after[j],fit_arena[j]);break;}
    return 1;
}
static int fit_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[FIT_GLOBALS],after[FIT_GLOBALS],actual[FIT_GLOBALS];static fit_record expected[FIT_MAX],adapter[FIT_MAX];
    static BYTE adapter_arena[FIT_SIZE];
    uint32_t want[3],core_ecx=0;uint64_t pair[3];
    memcpy(fit_before,fit_arena,FIT_SIZE);fit_globals(before,0);
    fit_reset();fit_invoke(mapped+d->rva,a,r,want);
    const unsigned calls=fit_calls;memcpy(expected,fit_log,calls*sizeof(*expected));
    memcpy(fit_after,fit_arena,FIT_SIZE);fit_globals(after,0);
    /* the adapter */
    memcpy(fit_arena,fit_before,FIT_SIZE);fit_globals(before,1);fit_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[4]={0};
    for(unsigned i=0;i<count&&i<4;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);
    const unsigned adapter_calls=fit_calls;memcpy(adapter,fit_log,adapter_calls*sizeof(*adapter));
    memcpy(adapter_arena,fit_arena,FIT_SIZE);fit_globals(actual,0);
    for(unsigned i=0;i<count&&i<4;++i){
        uint64_t want_count=0;
        for(unsigned j=0;j<calls;++j)if(fit_addresses[expected[j].id]==c[i].key)++want_count;
        if(c[i].count-counts[i]!=want_count)return fail("fit_leaves callee counter delta");
    }
    pair[0]=(uint64_t)want[1]<<32|want[0];
    int bad=pair[0]!=pair[1]||calls!=adapter_calls||memcmp(expected,adapter,calls*sizeof(*expected))||
        memcmp(fit_after,adapter_arena,FIT_SIZE)||memcmp(after,actual,FIT_GLOBALS);
    /* the core */
    memcpy(fit_arena,fit_before,FIT_SIZE);fit_globals(before,1);fit_reset();
    pair[2]=fit_core(index,a,r,&core_ecx);fit_globals(actual,0);
    const int ecx_known=!rng_hooks[index].installed;
    bad=bad||pair[2]!=pair[0]||calls!=fit_calls||memcmp(expected,fit_log,calls*sizeof(*expected))||
        memcmp(fit_after,fit_arena,FIT_SIZE)||memcmp(after,actual,FIT_GLOBALS)||
        (ecx_known&&((want[2]^core_ecx)&fit_ecx_mask(index)));
    if(bad){
        if(ecx_known)fprintf(stderr," ecx %08x/%08x mask %08x\n",want[2],core_ecx,fit_ecx_mask(index));
        return fit_report("direct",n,pair,calls,expected);
    }
    fit_globals(before,1);
    return 0;
}
static int fit_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;fit_build(index,n,0,a,r);
        BYTE before[FIT_GLOBALS],after[FIT_GLOBALS],actual[FIT_GLOBALS],expected_fpu[108],actual_fpu[108];static fit_record expected[FIT_MAX];
        memcpy(fit_before,fit_arena,FIT_SIZE);fit_globals(before,0);
        fit_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=fit_calls;memcpy(expected,fit_log,calls*sizeof(*expected));memcpy(fit_after,fit_arena,FIT_SIZE);fit_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("fit_leaves poison install");
        memcpy(fit_arena,fit_before,FIT_SIZE);fit_globals(before,1);fit_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);es2_hook_fpu_save(actual_fpu);fit_globals(actual,0);
        uint64_t pair[3]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0],0};
        if(pair[0]!=pair[1]||calls!=fit_calls||memcmp(expected,fit_log,calls*sizeof(*expected))||memcmp(fit_after,fit_arena,FIT_SIZE)||
           memcmp(after,actual,FIT_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=fit_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("fit_leaves poison uninstall");fit_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, EDX:EAX/trace/arena/globals\n",d->name,total);
    return status;
}
/* Nested hooks of the real pass: the callees that are hooks and run as
 * original code (no stub) under the caller. */
static unsigned fit_nested(unsigned index,unsigned out[2]){
    if(index==ES2_HOOK_INDEX_mount_table){out[0]=ES2_HOOK_INDEX_mount_entry;return 1;}
    if(index==ES2_HOOK_INDEX_shield_split){out[0]=ES2_HOOK_INDEX_multiply_q10;return 1;}
    if(index==ES2_HOOK_INDEX_energy_rate){out[0]=ES2_HOOK_INDEX_multiply_q10;out[1]=ES2_HOOK_INDEX_part_ratio;return 2;}
    return 0;
}
static int fit_real_pass(unsigned index){
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[2]={0},hooked=0,replaced=0;
    unsigned nested[2];const unsigned count=fit_nested(index,nested);
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;fit_build(index,n,1,a,r);
        BYTE before[FIT_GLOBALS],after[FIT_GLOBALS],actual[FIT_GLOBALS];
        memcpy(fit_before,fit_arena,FIT_SIZE);fit_globals(before,0);
        fit_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        memcpy(fit_after,fit_arena,FIT_SIZE);fit_globals(after,0);
        es2_hook hk[3];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if((!e||e->compare_unsupported)&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("fit_leaves nested install");}
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+2,d,&o,function_hook_extension(index)))return fail("fit_leaves real install");
        memcpy(fit_arena,fit_before,FIT_SIZE);fit_globals(before,1);fit_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);fit_globals(actual,0);
        if(memcmp(expected_pair,actual_pair,8)||memcmp(fit_after,fit_arena,FIT_SIZE)||memcmp(after,actual,FIT_GLOBALS)){
            uint64_t pair[3]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0],0};
            fprintf(stderr,"real mode=%u\n",mode);fit_calls=0;status=fit_report("real",n,pair,0,NULL);
        }
        for(unsigned j=0;j<3;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("fit_leaves nested compare mismatch");
            if((j<count||j==2)&&!es2_hook_uninstall(hk+j))return fail("fit_leaves real uninstall");}
        hooked+=hk[2].calls;replaced+=mode>=3;
        fit_globals(before,1);++total;
    }
    if(!status&&hooked!=replaced){fprintf(stderr,"FAIL: %s real pass entered %llu of %llu\n",d->name,(unsigned long long)hooked,(unsigned long long)replaced);status=1;}
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, %u nested hook(s) in rotating modes, hook original/replaced, 31/a7 stack, EDX:EAX, arena and globals\n",d->name,total,count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
static int fit_leaves_cases(unsigned index,unsigned limit){
    BYTE saved[FIT_IDS][5],old_globals[FIT_GLOBALS];int keep[FIT_IDS];
    fit_globals(old_globals,0);fit_keep(index,keep,0);
    if(fit_patch(saved,1,keep))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_FIT_LEAVES_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];fit_build(index,n,0,a,r);status=fit_compare(index,n,a,r);fit_globals(old_globals,1);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    if(!limit&&!status)status=fit_installed_probes(index,64);
    if(fit_patch(saved,0,keep))return 1;
    current_case_valid=0;
    if(!limit&&!status&&fit_nested(index,(unsigned[2]){0,0})){
        BYTE real_saved[FIT_IDS][5];int real_keep[FIT_IDS];fit_keep(index,real_keep,1);
        if(fit_patch(real_saved,1,real_keep))return 1;
        status=fit_real_pass(index);
        if(fit_patch(real_saved,0,real_keep))return 1;
    }
    fit_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; EDX:EAX of the original, adapter and core, ECX (core), callee calls with entry registers and arena hash, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
