/* Meteor constructor 409b44 (meteor_create) and shape instance 490b64
 * (shape_instance) (docs/reverse-engineering/main_loop.md, "meteor constructor
 * 409b44 and shape instance 490b64"). Direct cases run the original body and
 * the adapter (and the core directly, for ECX) on a 4 KiB arena with every game
 * callee replaced by a logging stub: 49630c (record stub as in removal_cases.h:
 * real FS:0 head at record+0, context word zeroed), object_init 402188,
 * rng_next 492ee0, sincos_pair 47eb30, rng_bounded 492f24, rotate_2d 47ffe8,
 * the rotation 47ff8c, the heap 47749c, 490b64, and for shape_instance the
 * arena allocation 47a288 and the memcpy thunk 49680e. Ordered calls with their
 * arguments, entry EAX/EDX/ECX (stack addresses masked), the record's context
 * word (read through FS:0) and the frame buffers each callee reads (V, M, D and
 * the z slot by content); the stubs write the buffers the real callees write
 * (M, R, the words O+45h/O+47h) and change what the original rereads (the
 * resource table pointer 4a96b4 and its second entry, the counter pointer
 * 6cbee4, O+4 and the shape's count after the arena allocation). P may overlap
 * O (the write/read order of step 11). Compared: the pair, ECX (original
 * through an own caller against the core), the arena, the globals, FS:0 and the
 * callee counter deltas. Then installed 31/a7 poison probes, and real-callee
 * passes: meteor_create over real 49630c, 47ff8c, 47a288, the memset/memcpy
 * thunks (the harness's emulations) and a real stream A, with object_init,
 * rng_next, rng_bounded, sincos_pair, rotate_2d and shape_instance nested in
 * rotating modes (only the heap stays a stub); shape_instance over real 47a288
 * and memcpy. Identifiers: met_/MET_. */
typedef struct met_record { uint32_t id,args[3],regs[3],input[6],context,eax,edx; } met_record;
enum { MET_SIZE=0x1000, MET_O=0x100, MET_P=0x200, MET_TABLE=0x220, MET_TABLE2=0x230, MET_SHAPE1=0x240, MET_SHAPE2=0x280,
    MET_SRC1=0x2c0, MET_SRC2=0x300, MET_DESC=0x340, MET_POOL=0x400, MET_POOL_SIZE=0x200, MET_HEAP1=0x600, MET_HEAP2=0x640,
    MET_COUNTER=0x680, MET_COUNTER2=0x690, MET_BLOCK=0x700, MET_MAX=32, MET_IDS=11, MET_GLOBALS=12+0x72 };
#define MET_BASE ((uint32_t)(uintptr_t)met_arena)
static BYTE met_arena[MET_SIZE],met_before[MET_SIZE],met_after[MET_SIZE];
static met_record met_log[MET_MAX];
static unsigned met_calls,met_heap_calls;
static int met_real;
static uint32_t met_seed,met_stream,met_o,met_s;
__attribute__((used)) static uint32_t met_ecx;
/* 0 49630c, 1 402188, 2 492ee0, 3 47eb30, 4 492f24, 5 47ffe8, 6 47ff8c,
 * 7 47749c, 8 490b64, 9 47a288, 10 49680e. */
static const uint32_t met_addresses[MET_IDS]={0x49630c,0x402188,0x492ee0,0x47eb30,0x492f24,0x47ffe8,0x47ff8c,0x47749c,0x490b64,
    0x47a288,0x49680e};
static const unsigned met_counts[MET_IDS]={0,2,1,2,2,3,3,1,2,2,3};
static uint32_t met_next(void){
    uint32_t z=met_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *met_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static BYTE *met_at(unsigned offset){return met_arena+offset;}
static int met_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static uint32_t met_fs0(void){uint32_t v;__asm__ volatile("movl %%fs:0,%0":"=r"(v));return v;}
/* What the stubs read and write (direct passes) or answer (the heap in
 * the real passes). */
static uint32_t met_answer(unsigned id,const uint32_t *a,met_record *v,uint32_t eax,uint32_t pick){
    uint32_t b=MET_BASE;
    switch(id){
    case 1: /* object_init: scribbles over O (the meteor overwrites what matters), may switch the table and counter */
        put32(met_host(a[0]),met_next());put32(met_host(a[0]+0x41),met_next());put32(met_host(a[0]+0x26),met_next());
        if(pick%5==0)put32(mapped+0xa96b4,b+MET_TABLE2);
        if(pick%7==0)put32(mapped+0x2cbee4,b+MET_COUNTER2);
        return a[0];
    case 3: /* sincos_pair: four words into M */
        for(unsigned j=0;j<4;++j)put16(met_host(a[1]+2*j),(uint16_t)met_next());
        return eax;
    case 5: /* rotate_2d: reads V and M, writes R */
        v->input[0]=get32(met_host(a[0]));v->input[1]=get32(met_host(a[0]+4));v->input[2]=get32(met_host(a[1]));v->input[3]=get32(met_host(a[1]+4));
        put32(met_host(a[2]),met_next());put32(met_host(a[2]+4),met_next());
        return eax;
    case 6: /* 47ff8c: reads D and M (and the z slot below D), writes the words O+45h, O+47h */
        v->input[0]=get32(met_host(a[0]));v->input[1]=get16(met_host(a[0]+4));v->input[2]=get32(met_host(a[1]));v->input[3]=get32(met_host(a[1]+4));
        v->input[4]=get32(met_host(a[0]-4));
        put16(met_host(a[2]),(uint16_t)met_next());put16(met_host(a[2]+2),(uint16_t)met_next());
        return eax;
    case 7: /* heap: two blocks, null a quarter of the time; may switch the table, its second entry, the counter */
        if(!met_real){
            if(pick%3==0)put32(mapped+0xa96b4,b+(pick>>4&1?MET_TABLE2:MET_TABLE));
            if(pick%5==0)put32(met_at(MET_TABLE+4),met_next());
            if(pick%7==0)put32(mapped+0x2cbee4,b+(pick>>5&1?MET_COUNTER2:MET_COUNTER));
        }
        ++met_heap_calls;
        return pick%4==0?0:b+(met_heap_calls&1?MET_HEAP1:MET_HEAP2);
    case 8: /* 490b64: the counter pointer may change */
        if(pick%3==0)put32(mapped+0x2cbee4,b+(pick>>4&1?MET_COUNTER2:MET_COUNTER));
        return a[0];
    case 9: /* 47a288: a block; may rewrite O+4 or the shape's count (both reread) */
        if(pick%3==0)put32(met_host(met_o+4),b+(pick>>4&1?MET_SHAPE1:MET_SHAPE2));
        if(pick%5==0&&(met_s+0x26u<=met_o+4u||met_s+0x24u>=met_o+8u))put16(met_host(met_s+0x24),(uint16_t)(met_next()%0x40u));
        return b+MET_BLOCK;
    case 10: v->input[0]=get32(met_host(a[1]));return eax; /* memcpy: the first source dword */
    }
    return eax;
}
__attribute__((used,noinline)) static uint64_t __cdecl met_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(met_calls==MET_MAX){fail("meteor synthetic call overflow");ExitProcess(1);}
    met_record *v=met_log+met_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,4*met_counts[id]);memcpy(v->regs,regs,12);
    for(unsigned j=0;j<3;++j)if(met_stack(v->regs[j]))v->regs[j]=0;
    for(unsigned j=0;j<met_counts[id];++j)if(met_stack(v->args[j]))v->args[j]=0;
    if(id&&id<9)v->context=get16(met_host(met_fs0()+16u));
    uint32_t eax=met_next(),edx=met_next(),pick=met_next();met_ecx=met_next();
    if(id==0)eax=met_fs0(); /* the record stub returns the real FS:0 head */
    else eax=met_answer(id,a,v,eax,pick);
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
/* Pushes the entry EAX/EDX/ECX for the record before the slot pointer. */
#define MET_STUB(n) __attribute__((naked)) static void met_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _met_stub_impl\n\taddl $24,%esp\n\tmovl _met_ecx,%ecx\n\tret\n\t");}
MET_STUB(1) MET_STUB(2) MET_STUB(3) MET_STUB(4) MET_STUB(5) MET_STUB(6) MET_STUB(7) MET_STUB(8) MET_STUB(9) MET_STUB(10)
#undef MET_STUB
/* 49630c: captures the entry registers like the other stubs, then links a
 * record at EBP + [descriptor+4] into FS:0 with the saved head at record+0
 * and a zero context word at record+16, as the original does. */
__attribute__((naked)) static void met_stub_0(void){__asm__ volatile(
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
    "pushl $0\n\tcalll _met_stub_impl\n\taddl $12,%esp\n\t"
    "popl %ecx\n\tmovl 4(%ecx),%ecx\n\taddl %ebp,%ecx\n\tmovl %eax,(%ecx)\n\tmovw $0,16(%ecx)\n\tmovl %ecx,%fs:0\n\t"
    "popl %edx\n\taddl $4,%esp\n\tmovl _met_ecx,%ecx\n\tret\n\t");}
static void *met_stubs[MET_IDS]={met_stub_0,met_stub_1,met_stub_2,met_stub_3,met_stub_4,met_stub_5,met_stub_6,met_stub_7,met_stub_8,met_stub_9,met_stub_10};
/* A three-slot cdecl caller that hands over EAX/EDX/ECX and returns all
 * three (the hook ABI returns EDX:EAX only). */
__attribute__((naked,noinline)) static void met_machine(void *target,const uint32_t *a,const uint32_t *registers,uint32_t *out){
    __asm__ volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl 12(%ebp),%esi\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* Globals: the counter pointer 6cbee4, the resource table pointer 4a96b4,
 * the arena descriptor pointer 6c5830, the stream A state 4d2599. */
static void met_globals(BYTE *out,int restore){
    static const struct { uint32_t rva,size; } r[]={{0x2cbee4,4},{0xa96b4,4},{0x2c5830,4},{0xd2599,0x72}};
    for(unsigned i=0;i<4;++i){if(restore)memcpy(mapped+r[i].rva,out,r[i].size);else memcpy(out,mapped+r[i].rva,r[i].size);out+=r[i].size;}
}
static void met_reset(void){met_calls=0;met_heap_calls=0;met_stream=met_seed;}
/* A shape: count word +24h, source dword +1Ch. */
static void met_shape(uint32_t shape,uint32_t source,uint16_t count){put16(met_host(shape+0x24),count);put32(met_host(shape+0x1c),source);}
/* Arguments: (O, P, G) for meteor_create, (O, S) for shape_instance. */
static void met_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    uint32_t b=MET_BASE;
    met_seed=random32();random_bytes(met_arena,MET_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    put32(mapped+0x2cbee4,b+MET_COUNTER);put32(mapped+0xa96b4,b+MET_TABLE);put32(mapped+0x2c5830,b+MET_DESC);
    if(real){
        /* consistent shapes (count 0..16), a bump arena with room, a valid stream A state */
        static const uint16_t counts[]={0,1,2,16};
        met_shape(b+MET_SHAPE1,b+MET_SRC1,n<64?counts[n%4]:(uint16_t)(random32()%17u));
        met_shape(b+MET_SHAPE2,b+MET_SRC2,n<64?counts[n/4%4]:(uint16_t)(random32()%17u));
        put32(met_at(MET_TABLE),b+MET_SHAPE1);put32(met_at(MET_TABLE+4),b+MET_SHAPE2);
        put32(met_at(MET_DESC),b+MET_POOL);put32(met_at(MET_DESC+4),MET_POOL_SIZE);put32(met_at(MET_DESC+8),random32()%0x40u);
        BYTE *state=mapped+0xd2599;random_bytes(state,112);state[112]=(BYTE)(random32()%56);state[113]=(BYTE)(random32()%56);
    }
    a[0]=met_o=b+MET_O+random32()%4u;
    if(index==ES2_HOOK_INDEX_shape_instance){
        uint32_t s=b+MET_SHAPE2;
        if(!real){
            /* both shapes valid: the arena stub may point O+4 at either */
            met_shape(b+MET_SHAPE1,b+MET_SRC1,(uint16_t)random32());met_shape(b+MET_SHAPE2,b+MET_SRC2,(uint16_t)random32());
            /* S apart, or aliasing O: S+24h on O+4 (the count is the low word of S itself), or S = O */
            unsigned k=random32()%6;
            if(k==1)s=met_o-0x20u;else if(k==2)s=met_o;
            uint16_t count=n%5==0?0:(uint16_t)(random32()%0x40u);
            if(n<8)count=(uint16_t)(n&1?0:n);
            met_shape(s,b+MET_SRC2,count);
        }
        a[1]=met_s=s;return;
    }
    met_s=b+MET_SHAPE2;
    a[2]=random32();
    a[1]=b+MET_P;
    if(!real&&random32()%4==0){ /* P inside O: its reads follow the stores of steps 3..10 */
        static const unsigned offsets[]={0,0x0c,0x10,0x2e,0x30,0x41,0x43,0x45,0x47,0x49,0x4b,0x4d,0x51,0x55};
        a[1]=met_o+offsets[random32()%(sizeof(offsets)/sizeof(offsets[0]))];
    }
}
static int met_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const met_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,met_calls);
    for(unsigned j=0;j<calls&&j<met_calls&&expected;++j)if(memcmp(expected+j,met_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(met_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<MET_SIZE;++j)if(met_after[j]!=met_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,met_after[j],met_arena[j]);break;}
    return 1;
}
static unsigned met_expected_hits(const callee_counter *c){
    unsigned hits=0;for(unsigned j=0;j<met_calls;++j)if(met_addresses[met_log[j].id]==c->key)++hits;return hits;
}
/* The core called directly (the adapters drop ECX); meteor_create with
 * the adapter's graph, FS:0 restored to the returned head. */
static uint64_t met_core(unsigned index,const uint32_t *a,const uint32_t *r,uint32_t *ecx){
    if(index==ES2_HOOK_INDEX_shape_instance){
        es2_weapon_graph g=weapons_graph(mapped,NULL,index);g.observe=NULL;
        return es2_shape_instance_registers(&math_native_memory,&g,a[0],a[1],r,ecx);
    }
    BYTE scratch[ES2_METEOR_SCRATCH]={0};es2_weapon_graph g=weapons_graph(mapped,scratch,index);
    g.observe=NULL;weapons_impact_context c={(uint32_t)(uintptr_t)mapped,(uint32_t)(uintptr_t)(scratch+ES2_METEOR_FRAME)};
    g.call=weapons_impact_guest_call;g.context=&c;
    uint64_t v=es2_meteor_create_registers(&math_native_memory,&g,a[0],a[1],a[2],r,ecx);
    uint32_t head=*ecx;__asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");return v;
}
static int met_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[MET_GLOBALS],after[MET_GLOBALS],actual[MET_GLOBALS];static met_record expected[MET_MAX];
    uint32_t head=met_fs0(),out[3],core_ecx=0;
    memcpy(met_before,met_arena,MET_SIZE);met_globals(before,0);
    met_reset();met_machine(mapped+d->rva,a,r,out);unsigned calls=met_calls;memcpy(expected,met_log,calls*sizeof(*expected));
    memcpy(met_after,met_arena,MET_SIZE);met_globals(after,0);
    uint64_t pair[2];pair[0]=(uint64_t)out[1]<<32|out[0];
    if(met_fs0()!=head)return fail("meteor original left FS:0 changed");
    memcpy(met_arena,met_before,MET_SIZE);met_globals(before,1);met_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];for(unsigned i=0;i<count&&i<16;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);met_globals(actual,0);
    if(met_fs0()!=head)return fail("meteor adapter left FS:0 changed");
    if(n<4096)for(unsigned i=0;i<count&&i<16;++i)if(c[i].count-counts[i]!=met_expected_hits(c+i))return fail("meteor callee counter delta");
    if(pair[0]!=pair[1]||calls!=met_calls||memcmp(expected,met_log,calls*sizeof(*expected))||memcmp(met_after,met_arena,MET_SIZE)||
       memcmp(after,actual,MET_GLOBALS))
        return met_report("direct",n,pair,calls,expected);
    memcpy(met_arena,met_before,MET_SIZE);met_globals(before,1);met_reset();
    pair[1]=met_core(index,a,r,&core_ecx);met_globals(actual,0);
    if(met_fs0()!=head)return fail("meteor core left FS:0 changed");
    /* With the hook installed (integration pass) the "original" run is the
       hook, whose ECX is the dispatcher's; ECX is compared otherwise. */
    const int ecx_known=!rng_hooks[index].installed;
    if(pair[0]!=pair[1]||(ecx_known&&core_ecx!=out[2])||calls!=met_calls||memcmp(expected,met_log,calls*sizeof(*expected))||
       memcmp(met_after,met_arena,MET_SIZE)||memcmp(after,actual,MET_GLOBALS)){
        fprintf(stderr," ECX %08x/%08x\n",out[2],core_ecx);return met_report("core ECX",n,pair,calls,expected);
    }
    met_globals(before,1);
    return 0;
}
static int met_patch(BYTE saved[MET_IDS][5],int active[MET_IDS],unsigned which,int on){
    for(unsigned i=0;i<MET_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(met_addresses[i]-0x400000u,met_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(met_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static int met_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u,head=met_fs0();met_build(index,n,a,r,0);
        BYTE before[MET_GLOBALS],after[MET_GLOBALS],actual[MET_GLOBALS],expected_fpu[108],actual_fpu[108];static met_record expected[MET_MAX];
        memcpy(met_before,met_arena,MET_SIZE);met_globals(before,0);
        met_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=met_calls;memcpy(expected,met_log,calls*sizeof(*expected));memcpy(met_after,met_arena,MET_SIZE);met_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("meteor poison install");
        memcpy(met_arena,met_before,MET_SIZE);met_globals(before,1);met_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);met_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(met_fs0()!=head||pair[0]!=pair[1]||calls!=met_calls||memcmp(expected,met_log,calls*sizeof(*expected))||memcmp(met_after,met_arena,MET_SIZE)||
           memcmp(after,actual,MET_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=met_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("meteor poison uninstall");met_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state/FS:0\n",d->name,total);
    return status;
}
/* Real-callee passes. meteor_create: only the heap 47749c is a stub; the
 * six nested hooks run in rotating original/replaced/compare modes
 * (compare falls back to replaced where refused), the same modes in the
 * expected run (caller original body) and the actual run (caller hook in
 * original mode for mode < 3, replaced otherwise). shape_instance: no
 * stub, the caller hook replaced. Compared: pair, ordered heap calls,
 * arena, globals (stream A included) and FS:0. */
static const unsigned met_nested[]={ES2_HOOK_INDEX_object_init,ES2_HOOK_INDEX_rng_next,ES2_HOOK_INDEX_rng_bounded,
    ES2_HOOK_INDEX_sincos_pair,ES2_HOOK_INDEX_rotate_2d,ES2_HOOK_INDEX_shape_instance};
enum { MET_NESTED=sizeof(met_nested)/sizeof(met_nested[0]) };
static int met_real_pass(unsigned index){
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[MET_NESTED]={0};
    const int meteor=index==ES2_HOOK_INDEX_meteor_create;const unsigned nested=meteor?MET_NESTED:0;
    static met_record expected[MET_MAX];BYTE saved[MET_IDS][5];int active[MET_IDS];
    if(meteor&&met_patch(saved,active,1u<<7,1))return 1;
    met_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u,head=met_fs0();met_build(index,n,a,r,1);
        BYTE before[MET_GLOBALS],after[MET_GLOBALS],actual[MET_GLOBALS];
        memcpy(met_before,met_arena,MET_SIZE);met_globals(before,0);
        es2_hook hk[MET_NESTED+1];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<nested;++j){
            o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(met_nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(met_nested[j]),&o,e))return fail("meteor nested install");
        }
        met_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        unsigned calls=met_calls;memcpy(expected,met_log,calls*sizeof(*expected));
        memcpy(met_after,met_arena,MET_SIZE);met_globals(after,0);
        o.mode=meteor&&mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+nested,d,&o,function_hook_extension(index)))return fail("meteor caller install");
        memcpy(met_arena,met_before,MET_SIZE);met_globals(before,1);met_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);met_globals(actual,0);
        if(met_fs0()!=head||expected_pair[0]!=actual_pair[0]||expected_pair[1]!=actual_pair[1]||calls!=met_calls||
           memcmp(expected,met_log,calls*sizeof(*expected))||memcmp(met_after,met_arena,MET_SIZE)||memcmp(after,actual,MET_GLOBALS)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            for(unsigned j=0;j<MET_GLOBALS;++j)if(after[j]!=actual[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,after[j],actual[j]);break;}
            fprintf(stderr,"real mode=%u head %08x/%08x\n",mode,head,met_fs0());status=met_report("real",n,pair,calls,expected);
        }
        if(o.mode==ES2_HOOK_REPLACED&&hk[nested].calls!=1){fprintf(stderr,"meteor real mode=%u case %u caller calls %llu\n",mode,n,(unsigned long long)hk[nested].calls);status=fail("meteor real caller not entered once");}
        for(unsigned j=0;j<=nested;++j){if(j<nested)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("meteor nested compare mismatch");
            if(!es2_hook_uninstall(hk+j))return fail("meteor nested uninstall");}
        met_globals(before,1);++total;
    }
    met_real=0;
    if(meteor&&met_patch(saved,active,0,0))return 1;
    for(unsigned j=0;j<nested&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,
        function_hook_descriptor(met_nested[j])->name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases, %s, 31/a7 stack, full pair, calls, memory, globals and FS:0\n",d->name,total,
        meteor?"object_init/rng_next/rng_bounded/sincos_pair/rotate_2d/shape_instance nested in rotating modes, real 49630c/47ff8c/47a288/memset/memcpy":
        "real 47a288 and memcpy");
    return status;
}
static int meteor_cases(unsigned index,unsigned limit){
    const unsigned which=index==ES2_HOOK_INDEX_meteor_create?0x1ffu:0x600u;
    BYTE saved[MET_IDS][5],old_globals[MET_GLOBALS];int active[MET_IDS];
    met_globals(old_globals,0);
    if(met_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_METEOR_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];met_build(index,n,a,r,0);status=met_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=met_installed_probes(index,64);
    if(met_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=met_real_pass(index);
    met_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair and ECX, ordered calls with entry registers, context word and frame buffers, arena/globals/FS:0\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
