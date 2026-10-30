/* Fire group: fire_alloc 46b430, fire_create 46b48c and fire_release 46b62c
 * (docs/reverse-engineering/main_loop.md, "fire allocation 46b430, fire
 * constructor 46b48c, fire release 46b62c"). Direct cases run the original body
 * (through an own five-slot caller that returns ECX too), the adapter and the
 * core on a 4 KiB arena with every game callee replaced by a logging stub:
 * pool_alloc 471b30, pool_prev 471c70 (a planned walk of up to 12 fires,
 * repeats allowed), object_init 402188, the memset thunk 49681a (the stub
 * clears the bytes), sfx_play 462830, fire_update 46b678 and removal_append
 * 478628. Each record holds the argument slots the bridge pushed, the
 * arguments, the entry EAX/EDX/ECX (stack values masked), the burning count
 * 6b4f38, the counter pointer 6cbee4 and a hash of the pool/fire area and the
 * globals at the time of the call; the stubs change what the original rereads
 * after a call (the pool pointer 6b4f30, the list pointer 6b4f34, the world
 * word 4a9e4e, the burning count, the counter pointer, the fire's fields, the
 * offset point) and what it must not reread (the kind table pointer 6b4f28).
 * The offset point P may alias the fire (the read/write order of the copy),
 * kinds index both sides of the table. Compared: the pair, ECX (original
 * against the core; fire_alloc's allocation path returns an uninitialized local
 * and is skipped), the call log, the arena, the globals and the callee counter
 * deltas. Then installed 31/a7 poison probes, and real-callee passes over a
 * consistent pool (used and free lists), removal list and counter, with
 * pool_alloc, pool_prev, pool_next, object_init, sfx_play (sound off: [49f8ec]
 * = 0) and removal_append nested in rotating modes, the real memset emulation
 * and fire_update as the logging stub, the caller in original and replaced
 * mode. Identifiers: fir_/FIR_. */
typedef struct fir_record { uint32_t id,slots,args[3],regs[3],burn,counter,hash,eax,edx; } fir_record;
enum { FIR_SIZE=0x1000, FIR_POOL=0x00, FIR_POOL2=0x20, FIR_NODES=0x40, FIR_STRIDE=0x68, FIR_COUNT=10, FIR_OWNERS=0x480,
    FIR_POINT=0x580, FIR_TABLE=0x640, FIR_TABLE2=0x6c0, FIR_LIST=0x700, FIR_LIST2=0x710, FIR_ITEMS=0x780, FIR_COUNTER=0x880,
    FIR_COUNTER2=0x890, FIR_HASHED=0x900, FIR_MAX=48, FIR_IDS=7, FIR_WALK=16, FIR_GLOBALS=32, FIR_SOUND=0x9f8ec };
#define FIR_BASE ((uint32_t)(uintptr_t)fir_arena)
#define FIR_FIRE(i) (FIR_BASE+FIR_NODES+FIR_STRIDE*(unsigned)(i)+8u)
#define FIR_OWNER(i) (FIR_BASE+FIR_OWNERS+0x40u*(unsigned)(i))
static BYTE fir_arena[FIR_SIZE],fir_before[FIR_SIZE],fir_after[FIR_SIZE];
static fir_record fir_log[FIR_MAX];
static unsigned fir_calls,fir_walk_length,fir_walk_at;
static int fir_real;
static uint32_t fir_seed,fir_stream,fir_walk[FIR_WALK],fir_point;
static unsigned fir_stats[3][3];
__attribute__((used)) static uint32_t fir_ecx;
/* 0 pool_alloc 471b30, 1 pool_prev 471c70, 2 object_init 402188, 3 memset
 * thunk 49681a, 4 sfx_play 462830, 5 fire_update 46b678, 6 removal_append
 * 478628. */
static const uint32_t fir_addresses[FIR_IDS]={0x471b30,0x471c70,0x402188,0x49681a,0x462830,0x46b678,0x478628};
static const unsigned fir_counts[FIR_IDS]={1,2,2,3,1,1,2};
static uint32_t fir_next(void){
    uint32_t z=fir_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *fir_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static BYTE *fir_at(unsigned offset){return fir_arena+offset;}
static int fir_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static uint32_t fir_fs0(void){uint32_t v;__asm__ volatile("movl %%fs:0,%0":"=r"(v));return v;}
/* Globals: kind table 6b4f28, pool 6b4f30, list 6b4f34, burning count
 * 6b4f38, world word 4a9e4e with the next script word (random, so a
 * dword compare differs), counter pointer 6cbee4, sound flag 49f8ec. */
static void fir_globals(BYTE *out,int restore){
    static const struct { uint32_t rva,size; } r[]={{0x2b4f28,20},{0xa9e4e,4},{0x2cbee4,4},{0x9f8ec,4}};
    for(unsigned i=0;i<4;++i){if(restore)memcpy(mapped+r[i].rva,out,r[i].size);else memcpy(out,mapped+r[i].rva,r[i].size);out+=r[i].size;}
}
static uint32_t fir_hash(void){
    BYTE g[FIR_GLOBALS];fir_globals(g,0);uint32_t h=0x811c9dc5u;
    for(unsigned i=0;i<FIR_HASHED;++i)h=(h^fir_arena[i])*0x01000193u;
    for(unsigned i=0;i<FIR_GLOBALS;++i)h=(h^g[i])*0x01000193u;
    return h;
}
static uint16_t fir_life(uint32_t pick){
    static const uint16_t lives[]={0,1,5,0x1e,0x2710,0x270f,0x2711,0x7fff,0x8000,0xffff,0xfff0,0x40};
    return pick%4?lives[pick>>4&7u]+(uint16_t)(pick>>8&1u)*lives[8+(pick>>9&3u)]:(uint16_t)(pick>>12);
}
/* What the stubs read and change (direct passes); in the real passes only
 * fire_update is a stub and changes nothing. */
static uint32_t fir_answer(unsigned id,const uint32_t *a,uint32_t eax,uint32_t pick){
    const uint32_t b=FIR_BASE;
    if(fir_real)return eax;
    switch(id){
    case 0: /* pool_alloc: a fire or 0; may switch the pool (reread by the walk) */
        if(pick%7==0)put32(mapped+0x2b4f30,b+(pick>>8&1?FIR_POOL2:FIR_POOL));
        return pick%2?FIR_FIRE((pick>>4)%FIR_COUNT):0;
    case 1: /* pool_prev: the planned walk; may switch the pool and list, a lifetime */
        if(pick%5==0)put32(mapped+0x2b4f30,b+(pick>>8&1?FIR_POOL2:FIR_POOL));
        if(pick%7==0)put32(mapped+0x2b4f34,b+(pick>>9&1?FIR_LIST2:FIR_LIST));
        eax=fir_walk_at<fir_walk_length?fir_walk[fir_walk_at]:0;++fir_walk_at;
        if(eax&&pick%3==0)put16(fir_host(eax+0x59),fir_life(fir_next()));
        return eax;
    case 2: /* object_init: scribbles over F, may switch the world, count, list, counter, table, the point */
        put32(fir_host(a[0]+0x41),fir_next());put32(fir_host(a[0]+0x45),fir_next());put16(fir_host(a[0]+0x59),(uint16_t)fir_next());
        put32(fir_host(a[0]+0x4a),fir_next());put16(fir_host(a[0]+4),(uint16_t)fir_next());put32(fir_host(a[0]+0x50),fir_next());
        if(pick%3==0)put16(mapped+0xa9e4e,pick>>8&1?4:(uint16_t)fir_next());
        if(pick%5==0)put32(mapped+0x2b4f38,pick>>9&1?0:fir_next());
        if(pick%7==0)put32(mapped+0x2b4f34,b+(pick>>10&1?FIR_LIST2:FIR_LIST));
        if(pick%11==0)put32(mapped+0x2cbee4,b+(pick>>11&1?FIR_COUNTER2:FIR_COUNTER));
        if(pick%13==0)put32(mapped+0x2b4f28,b+FIR_TABLE2);
        if(pick%4==1){put32(fir_host(fir_point),fir_next());put16(fir_host(fir_point+4),(uint16_t)fir_next());}
        return pick%8?a[0]:eax;
    case 3: /* memset thunk: clears as the emulation does; may change the burning count */
        if(a[2]<=16&&a[0]-b<FIR_SIZE-16u)memset(fir_host(a[0]),(int)a[1],a[2]);
        if(pick%5==0)put32(mapped+0x2b4f38,pick>>8&1?0:fir_next());
        return a[0];
    case 4: /* sfx_play: the burning count (incremented after it), the counter */
        if(pick%3==0)put32(mapped+0x2b4f38,fir_next());
        if(pick%5==0)put32(mapped+0x2cbee4,b+(pick>>8&1?FIR_COUNTER2:FIR_COUNTER));
        return eax;
    case 5: /* fire_update: the counter, the lifetime */
        if(pick%3==0)put32(mapped+0x2cbee4,b+(pick>>8&1?FIR_COUNTER2:FIR_COUNTER));
        if(pick%2)put16(fir_host(a[0]+0x59),(uint16_t)fir_next());
        return eax;
    case 6: /* removal_append: the list and pool pointers, the counter, later owners */
        if(pick%3==0)put32(mapped+0x2b4f34,b+(pick>>8&1?FIR_LIST2:FIR_LIST));
        if(pick%5==0)put32(mapped+0x2b4f30,b+(pick>>9&1?FIR_POOL2:FIR_POOL));
        if(pick%7==0)put32(mapped+0x2cbee4,b+(pick>>10&1?FIR_COUNTER2:FIR_COUNTER));
        if(pick%2)put32(fir_host(FIR_FIRE((pick>>12)%FIR_COUNT)+0x4a),FIR_OWNER(pick>>16&3u));
        return a[0];
    }
    return eax;
}
__attribute__((used,noinline)) static uint64_t __cdecl fir_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(fir_calls==FIR_MAX){fail("fire synthetic call overflow");ExitProcess(1);}
    fir_record *v=fir_log+fir_calls++;memset(v,0,sizeof(*v));v->id=id;v->slots=weapons_bridge_slots(a,fir_counts[id]);
    memcpy(v->args,a,4*fir_counts[id]);memcpy(v->regs,regs,12);
    for(unsigned j=0;j<3;++j)if(fir_stack(v->regs[j]))v->regs[j]=0;
    for(unsigned j=0;j<fir_counts[id];++j)if(fir_stack(v->args[j]))v->args[j]=0;
    v->burn=get32(mapped+0x2b4f38);v->counter=get32(mapped+0x2cbee4);v->hash=fir_hash();
    uint32_t eax=fir_next(),edx=fir_next(),pick=fir_next();fir_ecx=fir_next();
    eax=fir_answer(id,a,eax,pick);
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define FIR_STUB(n) __attribute__((naked)) static void fir_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _fir_stub_impl\n\taddl $24,%esp\n\tmovl _fir_ecx,%ecx\n\tret\n\t");}
FIR_STUB(0) FIR_STUB(1) FIR_STUB(2) FIR_STUB(3) FIR_STUB(4) FIR_STUB(5) FIR_STUB(6)
#undef FIR_STUB
static void *fir_stubs[FIR_IDS]={fir_stub_0,fir_stub_1,fir_stub_2,fir_stub_3,fir_stub_4,fir_stub_5,fir_stub_6};
static int fir_patch(BYTE saved[FIR_IDS][5],int active[FIR_IDS],unsigned which,int on){
    for(unsigned i=0;i<FIR_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(fir_addresses[i]-0x400000u,fir_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(fir_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
/* A five-slot cdecl caller that hands over EAX/EDX/ECX and returns all
 * three (the hook ABI returns EDX:EAX only). */
__attribute__((naked,noinline)) static void fir_machine(void *target,const uint32_t *a,const uint32_t *registers,uint32_t *out){
    __asm__ volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl 12(%ebp),%esi\n\tpushl 16(%esi)\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static void fir_reset(void){fir_calls=0;fir_walk_at=0;fir_stream=fir_seed;}
/* A consistent pool at FIR_POOL: `used` of the ten nodes on the used list
 * (head first, in a random order), the rest on the free list. */
static void fir_pool(unsigned used){
    unsigned order[FIR_COUNT];for(unsigned i=0;i<FIR_COUNT;++i)order[i]=i;
    for(unsigned i=FIR_COUNT;i>1;--i){unsigned j=random32()%i,t=order[i-1];order[i-1]=order[j];order[j]=t;}
    BYTE *p=fir_at(FIR_POOL);
    put16(p,(uint16_t)(FIR_COUNT-used));put16(p+2,(uint16_t)used);put16(p+4,0x5b);put32(p+6,FIR_FIRE(0));
    for(unsigned i=0;i<FIR_COUNT;++i){
        const unsigned first=i<used?0:used,last=i<used?used:FIR_COUNT;
        const uint32_t node=FIR_FIRE(order[i])-8u;
        put32(fir_host(node),i+1<last?FIR_FIRE(order[i+1])-8u:0);
        put32(fir_host(node+4),i>first&&i<used?FIR_FIRE(order[i-1])-8u:0);
    }
    put32(p+0xa,used<FIR_COUNT?FIR_FIRE(order[used])-8u:0);
    put32(p+0xe,used?FIR_FIRE(order[0])-8u:0);
}
/* Arguments: () for fire_alloc (the pushed size), (F, O, w, P, k) for
 * fire_create, (O) for fire_release. */
static void fir_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    const uint32_t b=FIR_BASE;
    fir_seed=random32();random_bytes(fir_arena,FIR_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    put32(mapped+0x2b4f28,b+FIR_TABLE);put32(mapped+0x2b4f30,b+FIR_POOL);put32(mapped+0x2b4f34,b+FIR_LIST);
    put32(mapped+0x2cbee4,b+FIR_COUNTER);
    static const uint16_t worlds[]={0,1,2,3,5,0x104,0xff04,0x4000};
    put16(mapped+0xa9e4e,random32()%2?4:worlds[random32()%8]);put16(mapped+0xa9e50,(uint16_t)(random32()%2?0:random32()));
    const uint32_t burning=random32()%3;
    put32(mapped+0x2b4f38,burning==0?0:burning==1?random32()%3:random32());
    for(unsigned i=0;i<FIR_COUNT;++i){
        put16(fir_host(FIR_FIRE(i)+0x59),fir_life(random32()));
        put32(fir_host(FIR_FIRE(i)+0x4a),random32()%8?FIR_OWNER(random32()%4):random32());
    }
    fir_point=b+FIR_POINT;
    if(real){
        /* every kind's shape 0 (object_init allocates nothing), a short removal list */
        memset(fir_at(FIR_TABLE-0x40),0,0x80);
        put32(fir_at(FIR_LIST),b+FIR_ITEMS);put16(fir_at(FIR_LIST+4),(uint16_t)(random32()%8));
        fir_pool(index==ES2_HOOK_INDEX_fire_alloc&&n%2?FIR_COUNT:1u+random32()%FIR_COUNT);
    } else {
        fir_walk_length=n<16?n%13:random32()%13;
        for(unsigned i=0;i<fir_walk_length;++i)fir_walk[i]=FIR_FIRE(random32()%FIR_COUNT);
    }
    if(index==ES2_HOOK_INDEX_fire_alloc){a[0]=random32()%8?0x5b:random32();return;}
    if(index==ES2_HOOK_INDEX_fire_release){a[0]=random32()%8?FIR_OWNER(random32()%4):random32();return;}
    const unsigned f=random32()%FIR_COUNT;
    a[0]=FIR_FIRE(f);a[1]=random32()%2?FIR_OWNER(random32()%4):random32();a[2]=random32();
    static const uint16_t kinds[]={0,1,2,3,4,5,0xffff,0xfff0,0x000f,1,3,3};
    a[4]=(random32()&0xffff0000u)|(random32()%4?kinds[random32()%12]:(uint16_t)(random32()%32u-16u));
    const unsigned k=random32()%6;
    if(k==0)a[3]=0;
    else if(k<4||real)a[3]=b+FIR_POINT;
    else { /* P inside F: its reads follow the stores before them */
        static const unsigned offsets[]={0x3c,0x3e,0x41,0x44,0x46,0x48,0x4a,0x4c,0x4e,0x50,0x52,0x54,0x55,0x56,0x57,0x59};
        a[3]=a[0]+offsets[random32()%16];
    }
    if(a[3])fir_point=a[3];
    if(real&&random32()%3==0)a[3]=0;
}
static int fir_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const fir_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,fir_calls);
    for(unsigned j=0;j<calls&&j<fir_calls&&expected;++j)if(memcmp(expected+j,fir_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(fir_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<FIR_SIZE;++j)if(fir_after[j]!=fir_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,fir_after[j],fir_arena[j]);break;}
    return 1;
}
static unsigned fir_expected_hits(const callee_counter *c){
    unsigned hits=0;for(unsigned j=0;j<fir_calls;++j)if(fir_addresses[fir_log[j].id]==c->key)++hits;return hits;
}
static uint64_t fir_core(unsigned index,const uint32_t *a,const uint32_t *r,uint32_t *ecx){
    es2_weapon_graph g=weapons_graph(mapped,NULL,index);g.observe=NULL;
    if(index==ES2_HOOK_INDEX_fire_alloc)return es2_fire_alloc_registers(&math_native_memory,&g,r,ecx);
    if(index==ES2_HOOK_INDEX_fire_create)return es2_fire_create_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],a[4],r,ecx);
    return es2_fire_release_registers(&math_native_memory,&g,a[0],r,ecx);
}
/* Path statistics per hook: fire_alloc allocated / chose a fire / none;
 * fire_create sound / burning without sound / discarded; fire_release no
 * release / one / more. */
static void fir_count(unsigned index,const uint64_t pair,unsigned calls,const fir_record *log){
    unsigned k=0;
    if(index==ES2_HOOK_INDEX_fire_alloc)k=calls==1?0:(uint32_t)pair?1:2;
    else if(index==ES2_HOOK_INDEX_fire_create){k=2;for(unsigned j=0;j<calls;++j){if(log[j].id==4)k=0;else if(log[j].id==5&&k==2)k=1;}}
    else {unsigned m=0;for(unsigned j=0;j<calls;++j)m+=log[j].id==6;k=m>1?2:m;}
    ++fir_stats[index==ES2_HOOK_INDEX_fire_alloc?0:index==ES2_HOOK_INDEX_fire_create?1:2][k];
}
static int fir_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[FIR_GLOBALS],after[FIR_GLOBALS],actual[FIR_GLOBALS];static fir_record expected[FIR_MAX];
    uint32_t out[3],core_ecx=0;
    memcpy(fir_before,fir_arena,FIR_SIZE);fir_globals(before,0);
    fir_reset();fir_machine(mapped+d->rva,a,r,out);unsigned calls=fir_calls;memcpy(expected,fir_log,calls*sizeof(*expected));
    memcpy(fir_after,fir_arena,FIR_SIZE);fir_globals(after,0);
    uint64_t pair[2];pair[0]=(uint64_t)out[1]<<32|out[0];
    fir_count(index,pair[0],calls,expected);
    memcpy(fir_arena,fir_before,FIR_SIZE);fir_globals(before,1);fir_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];for(unsigned i=0;i<count&&i<8;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);fir_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count&&i<8;++i)if(c[i].count-counts[i]!=fir_expected_hits(c+i))return fail("fire callee counter delta");
    if(pair[0]!=pair[1]||calls!=fir_calls||memcmp(expected,fir_log,calls*sizeof(*expected))||memcmp(fir_after,fir_arena,FIR_SIZE)||
       memcmp(after,actual,FIR_GLOBALS))
        return fir_report("direct",n,pair,calls,expected);
    memcpy(fir_arena,fir_before,FIR_SIZE);fir_globals(before,1);fir_reset();
    pair[1]=fir_core(index,a,r,&core_ecx);fir_globals(actual,0);
    /* With the hook installed (integration pass) the "original" run is the
       hook, whose ECX is the dispatcher's; fire_alloc's allocation path
       returns an uninitialized local. ECX is compared otherwise. */
    const int ecx_known=!rng_hooks[index].installed&&!(index==ES2_HOOK_INDEX_fire_alloc&&calls&&expected[0].eax);
    if(pair[0]!=pair[1]||(ecx_known&&core_ecx!=out[2])||calls!=fir_calls||memcmp(expected,fir_log,calls*sizeof(*expected))||
       memcmp(fir_after,fir_arena,FIR_SIZE)||memcmp(after,actual,FIR_GLOBALS)){
        fprintf(stderr," ECX %08x/%08x\n",out[2],core_ecx);return fir_report("core ECX",n,pair,calls,expected);
    }
    fir_globals(before,1);
    return 0;
}
static int fir_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;fir_build(index,n*41u+3u,a,r,0);
        BYTE before[FIR_GLOBALS],after[FIR_GLOBALS],actual[FIR_GLOBALS],expected_fpu[108],actual_fpu[108];static fir_record expected[FIR_MAX];
        memcpy(fir_before,fir_arena,FIR_SIZE);fir_globals(before,0);
        fir_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=fir_calls;memcpy(expected,fir_log,calls*sizeof(*expected));memcpy(fir_after,fir_arena,FIR_SIZE);fir_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("fire poison install");
        memcpy(fir_arena,fir_before,FIR_SIZE);fir_globals(before,1);fir_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);es2_hook_fpu_save(actual_fpu);fir_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=fir_calls||memcmp(expected,fir_log,calls*sizeof(*expected))||memcmp(fir_after,fir_arena,FIR_SIZE)||
           memcmp(after,actual,FIR_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=fir_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("fire poison uninstall");fir_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/calls/arena/globals\n",d->name,total);
    return status;
}
/* Real-callee passes: only fire_update 46b678 stays a stub; the nested
 * hooks run in rotating original/replaced/compare modes (compare falls
 * back to replaced where refused), the same modes in the expected run
 * (caller original body) and the actual run (caller hook in original mode
 * for mode < 3, replaced otherwise). Compared: pair, ordered fire_update
 * calls, arena, globals and FS:0. */
static const unsigned fir_nested[]={ES2_HOOK_INDEX_pool_alloc,ES2_HOOK_INDEX_pool_prev,ES2_HOOK_INDEX_pool_next,
    ES2_HOOK_INDEX_object_init,ES2_HOOK_INDEX_sfx_play,ES2_HOOK_INDEX_removal_append};
enum { FIR_NESTED=sizeof(fir_nested)/sizeof(fir_nested[0]) };
static int fir_real_pass(unsigned index){
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[FIR_NESTED]={0},caller=0;
    /* the nested hooks each caller must reach: pool_alloc/pool_prev/pool_next;
       object_init/sfx_play/removal_append; pool_prev/pool_next/removal_append */
    const unsigned need=index==ES2_HOOK_INDEX_fire_alloc?0x07u:index==ES2_HOOK_INDEX_fire_create?0x38u:0x26u;
    static fir_record expected[FIR_MAX];BYTE saved[FIR_IDS][5],sound[4];int active[FIR_IDS];
    memcpy(sound,mapped+FIR_SOUND,4);
    if(fir_patch(saved,active,1u<<5,1))return 1;
    fir_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u,head=fir_fs0();fir_build(index,n,a,r,1);
        put32(mapped+FIR_SOUND,0);
        BYTE before[FIR_GLOBALS],after[FIR_GLOBALS],actual[FIR_GLOBALS];
        memcpy(fir_before,fir_arena,FIR_SIZE);fir_globals(before,0);
        es2_hook hk[FIR_NESTED+1];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<FIR_NESTED;++j){
            o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(fir_nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(fir_nested[j]),&o,e))return fail("fire nested install");
        }
        fir_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        unsigned calls=fir_calls;memcpy(expected,fir_log,calls*sizeof(*expected));
        memcpy(fir_after,fir_arena,FIR_SIZE);fir_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+FIR_NESTED,d,&o,function_hook_extension(index)))return fail("fire caller install");
        memcpy(fir_arena,fir_before,FIR_SIZE);fir_globals(before,1);fir_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);fir_globals(actual,0);
        if(fir_fs0()!=head||expected_pair[0]!=actual_pair[0]||expected_pair[1]!=actual_pair[1]||calls!=fir_calls||
           memcmp(expected,fir_log,calls*sizeof(*expected))||memcmp(fir_after,fir_arena,FIR_SIZE)||memcmp(after,actual,FIR_GLOBALS)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            for(unsigned j=0;j<FIR_GLOBALS;++j)if(after[j]!=actual[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,after[j],actual[j]);break;}
            fprintf(stderr,"real mode=%u head %08x/%08x\n",mode,head,fir_fs0());status=fir_report("real",n,pair,calls,expected);
        }
        if(o.mode==ES2_HOOK_REPLACED&&hk[FIR_NESTED].calls!=1){fprintf(stderr,"fire real mode=%u case %u caller calls %llu\n",mode,n,(unsigned long long)hk[FIR_NESTED].calls);status=fail("fire real caller not entered once");}
        if(o.mode==ES2_HOOK_REPLACED)++caller;
        for(unsigned j=0;j<=FIR_NESTED;++j){if(j<FIR_NESTED)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("fire nested compare mismatch");
            if(!es2_hook_uninstall(hk+j))return fail("fire nested uninstall");}
        fir_globals(before,1);++total;
    }
    fir_real=0;
    memcpy(mapped+FIR_SOUND,sound,4);
    if(fir_patch(saved,active,0,0))return 1;
    for(unsigned j=0;j<FIR_NESTED&&!status;++j)if(need>>j&1&&!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,
        function_hook_descriptor(fir_nested[j])->name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases (%llu with the caller replaced; pool_alloc/pool_prev/pool_next/object_init/sfx_play/removal_append nested in rotating modes, real memset, fire_update stub), 31/a7 stack, pair, calls, arena, globals and FS:0\n",
        d->name,total,(unsigned long long)caller);
    return status;
}
static int fire_cases(unsigned index,unsigned limit){
    BYTE saved[FIR_IDS][5],old_globals[FIR_GLOBALS];int active[FIR_IDS];
    fir_globals(old_globals,0);
    if(fir_patch(saved,active,0x7fu,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_FIRE_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    memset(fir_stats,0,sizeof(fir_stats));
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];fir_build(index,n,a,r,0);status=fir_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    const unsigned s=index==ES2_HOOK_INDEX_fire_alloc?0:index==ES2_HOOK_INDEX_fire_create?1:2;
    const unsigned paths[3]={fir_stats[s][0],fir_stats[s][1],fir_stats[s][2]};
    if(!limit&&!status)status=fir_installed_probes(index,64);
    if(fir_patch(saved,active,0,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=fir_real_pass(index);
    fir_globals(old_globals,1);
    static const char *names[3][3]={{"allocated","chosen on a full pool","none"},{"with sound","burning without sound","discarded"},
        {"no release","one release","several releases"}};
    if(!status)printf("PASS: %-20s %u cases (%u %s, %u %s, %u %s); full return pair and ECX, ordered calls with entry registers and state hash, arena/globals\n",
        function_hook_descriptor(index)->name,cases,paths[0],names[s][0],paths[1],names[s][1],paths[2],names[s][2]);
    return status;
}
