/* Object removal: removal_flush 478644, pool_free 471bc8 and removal_append
 * 478628 (docs/reverse-engineering/main_loop.md, "object removal"). Direct
 * cases run the original body and the adapter (and the core directly, for ECX)
 * on a 4 KiB arena. The flush's direct game callees are logging stubs: 49630c
 * (record stub as in weapon_eject_cases.h: real FS:0 head at record+0, context
 * word zeroed), 496406, pool_free 471bc8 and the destructor slot +8 in two
 * arena vtables. Ordered calls with their arguments, entry EAX/EDX/ECX (stack
 * addresses masked) and the record's context word (read through FS:0); the
 * stubs change what the original rereads (the current and later entries, the
 * count, the counter pointer 6cbee4, an object's vtable after 496406) and what
 * it must not reread (the array pointer). Compared: the pair, ECX (original
 * through an own caller against the core), the arena, 6cbee4, FS:0 and the
 * callee counter deltas. The leaves pool_free and removal_append run on random
 * links and lists inside the arena (aliased links, a pool header over a node,
 * an array over the list header, 16-bit count edges). Then installed 31/a7
 * poison probes, and for the flush real-callee cases (real 49630c, 496406 and a
 * consistent pool) with pool_free nested in rotating modes. Identifiers:
 * rmv_/RMV_. */
typedef struct rmv_record { uint32_t id,args[2],regs[3],context,eax,edx; } rmv_record;
enum { RMV_SIZE=0x1000, RMV_L=0x80, RMV_ITEMS=0xc0, RMV_ITEMS2=0x100, RMV_POOL=0x160, RMV_NODES=0x200, RMV_NODE_COUNT=8,
    RMV_VT1=0x440, RMV_VT2=0x480, RMV_TYPE=0x4c0, RMV_COUNTER=0x500, RMV_COUNTER2=0x510, RMV_MAX=64, RMV_IDS=4,
    RMV_GLOBALS=4, RMV_SLOTS=16 };
#define RMV_BASE ((uint32_t)(uintptr_t)rmv_arena)
#define RMV_NODE(i) (RMV_BASE+RMV_NODES+0x40u*(unsigned)(i))
static BYTE rmv_arena[RMV_SIZE],rmv_before[RMV_SIZE],rmv_after[RMV_SIZE];
static rmv_record rmv_log[RMV_MAX];
static unsigned rmv_calls,rmv_index;
static int rmv_real;
static uint32_t rmv_seed,rmv_stream,rmv_items0;
__attribute__((used)) static uint32_t rmv_ecx;
/* 0 49630c, 1 496406, 2 slot +8 (destructor), 3 pool_free 471bc8. */
static const uint32_t rmv_addresses[RMV_IDS]={0x49630c,0x496406,0,0x471bc8};
static const unsigned rmv_slots[RMV_IDS]={0,0,8,0};
static const unsigned rmv_counts[RMV_IDS]={0,2,2,2};
static uint32_t rmv_next(void){
    uint32_t z=rmv_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *rmv_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static BYTE *rmv_at(unsigned offset){return rmv_arena+offset;}
static int rmv_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static uint32_t rmv_fs0(void){uint32_t v;__asm__ volatile("movl %%fs:0,%0":"=r"(v));return v;}
/* An entry: one of the eight objects, or (direct passes) null. */
static uint32_t rmv_entry(uint32_t pick){return pick%5==0?0:RMV_NODE(pick>>3&7u)+8u;}
static uint16_t rmv_count(uint32_t pick){
    static const uint16_t edges[]={0xffff,0x8000,0x8001,0xfff0};
    return pick%6==0?edges[pick>>4&3u]:(uint16_t)(pick>>8&15u)+(uint16_t)(pick>>12&1u);
}
/* Rereads of the flush between its calls (direct passes only). */
static void rmv_touch(unsigned id,const uint32_t *a,uint32_t pick){
    if(rmv_real)return;
    uint32_t b=RMV_BASE;
    if(id==3){
        ++rmv_index;
        if(pick%3==0&&rmv_index<RMV_SLOTS)put32(rmv_host(rmv_items0+4u*rmv_index),rmv_entry(rmv_next()));
        if(pick%11==0)put16(rmv_at(RMV_L+4),rmv_count(rmv_next()));
        return;
    }
    if(pick%5==0)put32(rmv_host(rmv_items0+4u*rmv_index),rmv_entry(rmv_next()));
    if(pick%7==0)put32(rmv_at(RMV_L),b+RMV_ITEMS2);
    if(pick%11==0)put16(rmv_at(RMV_L+4),rmv_count(rmv_next()));
    if(pick%13==0)put32(mapped+0x2cbee4,b+(pick>>8&1?RMV_COUNTER2:RMV_COUNTER));
    if(id==1&&pick%17==0)put32(rmv_host(a[0]),b+(pick>>9&1?RMV_VT2:RMV_VT1));
}
__attribute__((used,noinline)) static uint64_t __cdecl rmv_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(rmv_calls==RMV_MAX){fail("removal synthetic call overflow");ExitProcess(1);}
    rmv_record *v=rmv_log+rmv_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,4*rmv_counts[id]);memcpy(v->regs,regs,12);
    for(unsigned j=0;j<3;++j)if(rmv_stack(v->regs[j]))v->regs[j]=0;
    for(unsigned j=0;j<rmv_counts[id];++j)if(rmv_stack(v->args[j]))v->args[j]=0;
    if(id)v->context=get16(rmv_host(rmv_fs0()+16u));
    uint32_t eax=rmv_next(),edx=rmv_next(),pick=rmv_next();rmv_ecx=rmv_next();
    if(id==0)eax=rmv_fs0(); /* the record stub returns the real FS:0 head */
    else rmv_touch(id,a,pick);
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
/* Pushes the entry EAX/EDX/ECX for the record before the slot pointer. */
#define RMV_STUB(n) __attribute__((naked)) static void rmv_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _rmv_stub_impl\n\taddl $24,%esp\n\tmovl _rmv_ecx,%ecx\n\tret\n\t");}
RMV_STUB(1) RMV_STUB(2) RMV_STUB(3)
#undef RMV_STUB
/* 49630c: captures the entry registers like the other stubs, then links a
 * record at EBP + [descriptor+4] into FS:0 with the saved head at record+0
 * and a zero context word at record+16, as the original does. */
__attribute__((naked)) static void rmv_stub_0(void){__asm__ volatile(
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
    "pushl $0\n\tcalll _rmv_stub_impl\n\taddl $12,%esp\n\t"
    "popl %ecx\n\tmovl 4(%ecx),%ecx\n\taddl %ebp,%ecx\n\tmovl %eax,(%ecx)\n\tmovw $0,16(%ecx)\n\tmovl %ecx,%fs:0\n\t"
    "popl %edx\n\taddl $4,%esp\n\tmovl _rmv_ecx,%ecx\n\tret\n\t");}
static void *rmv_stubs[RMV_IDS]={rmv_stub_0,rmv_stub_1,rmv_stub_2,rmv_stub_3};
/* A two-slot cdecl caller that hands over EAX/EDX/ECX and returns all
 * three (the hook ABI returns EDX:EAX only). */
__attribute__((naked,noinline)) static void rmv_machine(void *target,const uint32_t *a,const uint32_t *registers,uint32_t *out){
    __asm__ volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl 12(%ebp),%esi\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static void rmv_globals(BYTE *out,int restore){if(restore)memcpy(mapped+0x2cbee4,out,4);else memcpy(out,mapped+0x2cbee4,4);}
static void rmv_reset(void){rmv_calls=0;rmv_index=0;rmv_stream=rmv_seed;}
static uint32_t rmv_link(void){return random32()%3==0?0:RMV_NODE(random32()%RMV_NODE_COUNT);}
static uint16_t rmv_word(void){static const uint16_t w[]={0,1,2,0x7fff,0x8000,0xffff,0xfffe};return random32()%3?w[random32()%7]:(uint16_t)random32();}
/* A consistent pool for the real pass: k of the eight nodes on the used
 * list (head first), the rest on the free list; the removal list holds a
 * shuffled subset of the used objects. */
static void rmv_real_pool(uint32_t pool){
    unsigned order[RMV_NODE_COUNT],k=random32()%(RMV_NODE_COUNT+1u);
    for(unsigned i=0;i<RMV_NODE_COUNT;++i)order[i]=i;
    for(unsigned i=RMV_NODE_COUNT;i>1;--i){unsigned j=random32()%i,t=order[i-1];order[i-1]=order[j];order[j]=t;}
    for(unsigned i=0;i<RMV_NODE_COUNT;++i){
        const int used=i<k;const unsigned first=used?0:k,last=used?k:RMV_NODE_COUNT;
        uint32_t n=RMV_NODE(order[i]);
        put32(rmv_host(n),i+1<last?RMV_NODE(order[i+1]):0);put32(rmv_host(n)+4,i>first?RMV_NODE(order[i-1]):0);
    }
    put16(rmv_host(pool),(uint16_t)(RMV_NODE_COUNT-k));put16(rmv_host(pool)+2,(uint16_t)k);
    put32(rmv_host(pool)+0xa,k<RMV_NODE_COUNT?RMV_NODE(order[k]):0);put32(rmv_host(pool)+0xe,k?RMV_NODE(order[0]):0);
    unsigned s=k?random32()%(k+1u):0;
    for(unsigned i=k;i>1;--i){unsigned j=random32()%i,t=order[i-1];order[i-1]=order[j];order[j]=t;}
    for(unsigned i=0;i<s;++i)put32(rmv_at(RMV_ITEMS+4*i),RMV_NODE(order[i])+8u);
    put16(rmv_at(RMV_L+4),(uint16_t)s);
}
/* Arguments: (L, P) for the flush and pool_free, (L, v) for the append. */
static void rmv_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    uint32_t b=RMV_BASE;
    rmv_seed=random32();random_bytes(rmv_arena,RMV_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    if(index==ES2_HOOK_INDEX_removal_append){
        a[1]=random32();
        uint32_t list=b+0x600u+random32()%0x100u;a[0]=list;
        if(random32()%4){ /* an array apart from the header */
            put32(rmv_host(list),b+0x900u+random32()%0x100u);put16(rmv_host(list)+4,(uint16_t)(random32()%129u-64u));
        } else { /* the store lands on or around the header (count -3..3) */
            int c=(int)(random32()%7u)-3;put16(rmv_host(list)+4,(uint16_t)c);
            put32(rmv_host(list),list+4u-4u*(uint32_t)c+random32()%7u-3u);
        }
        /* count edges: FFFFh (index -1), 7FFFh and 8000h (the array placed
           so that the store stays in the arena), 0 with the array = the
           header (the store replaces the array pointer itself) */
        static const uint16_t counts[4]={0xffff,0x7fff,0x8000,0};
        static const uint32_t arrays[4]={0x900u,0x940u-0x1fffcu,0x940u+0x20000u,0};
        if(n<4){put16(rmv_host(list)+4,counts[n]);put32(rmv_host(list),n==3?list:b+arrays[n]);}
        return;
    }
    if(index==ES2_HOOK_INDEX_pool_free){
        uint32_t pool=random32()%8?b+RMV_POOL:RMV_NODE(random32()%RMV_NODE_COUNT)+0x10u;a[0]=pool; /* a header inside a node, off its links */
        put16(rmv_host(pool),rmv_word());put16(rmv_host(pool)+2,rmv_word());
        for(unsigned i=0;i<RMV_NODE_COUNT;++i){put32(rmv_host(RMV_NODE(i)),rmv_link());put32(rmv_host(RMV_NODE(i))+4,rmv_link());}
        put32(rmv_host(pool)+0xa,rmv_link());put32(rmv_host(pool)+0xe,rmv_link());
        a[1]=RMV_NODE(random32()%RMV_NODE_COUNT)+8u;
        return;
    }
    /* the flush: objects with two vtables (RTTI words for the real 496406) */
    for(unsigned i=0;i<RMV_NODE_COUNT;++i)put32(rmv_host(RMV_NODE(i)+8),b+(random32()%2?RMV_VT2:RMV_VT1));
    for(unsigned v=RMV_VT1;v<=RMV_VT2;v+=RMV_VT2-RMV_VT1){
        put32(rmv_at(v+8),(uint32_t)(uintptr_t)rmv_stubs[2]);put32(rmv_at(v-4),0);put32(rmv_at(v-0xc),b+RMV_TYPE);
    }
    put32(rmv_at(RMV_L),b+RMV_ITEMS);rmv_items0=b+RMV_ITEMS;
    put32(mapped+0x2cbee4,b+RMV_COUNTER);
    a[0]=b+RMV_L;
    if(real){a[1]=b+RMV_POOL;rmv_real_pool(a[1]);return;}
    a[1]=random32();
    for(unsigned i=0;i<RMV_SLOTS;++i){put32(rmv_at(RMV_ITEMS+4*i),rmv_entry(random32()));put32(rmv_at(RMV_ITEMS2+4*i),rmv_entry(random32()));}
    put16(rmv_at(RMV_L+4),n<17?(uint16_t)n:rmv_count(random32()));
}
static int rmv_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const rmv_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,rmv_calls);
    for(unsigned j=0;j<calls&&j<rmv_calls&&expected;++j)if(memcmp(expected+j,rmv_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(rmv_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<RMV_SIZE;++j)if(rmv_after[j]!=rmv_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,rmv_after[j],rmv_arena[j]);break;}
    return 1;
}
static unsigned rmv_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<rmv_calls;++j){unsigned id=rmv_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?rmv_slots[id]==c->key:!rmv_slots[id]&&rmv_addresses[id]==c->key)++hits;}
    return hits;
}
/* The core called directly (the adapter drops ECX): the flush with the
 * adapter's graph; FS:0 restored to the returned head. */
static uint64_t rmv_core(unsigned index,const uint32_t *a,const uint32_t *r,uint32_t *ecx){
    if(index==ES2_HOOK_INDEX_pool_free)return es2_pool_free_registers(&math_native_memory,a[0],a[1],ecx);
    if(index==ES2_HOOK_INDEX_removal_append)return es2_removal_append_registers(&math_native_memory,a[0],a[1],ecx);
    BYTE scratch[ES2_REMOVAL_SCRATCH]={0};es2_weapon_graph g=weapons_graph(mapped,scratch,ES2_HOOK_INDEX_removal_flush);
    g.observe=NULL;weapons_impact_context c={(uint32_t)(uintptr_t)mapped,(uint32_t)(uintptr_t)(scratch+ES2_REMOVAL_FRAME)};
    g.call=weapons_impact_guest_call;g.context=&c;
    uint64_t v=es2_removal_flush_registers(&math_native_memory,&g,a[0],a[1],r,ecx);
    uint32_t head=(uint32_t)(v>>32);__asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");return v;
}
static int rmv_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[RMV_GLOBALS],after[RMV_GLOBALS],actual[RMV_GLOBALS];static rmv_record expected[RMV_MAX];
    uint32_t head=rmv_fs0(),out[3],core_ecx=0;
    memcpy(rmv_before,rmv_arena,RMV_SIZE);rmv_globals(before,0);
    rmv_reset();rmv_machine(mapped+d->rva,a,r,out);unsigned calls=rmv_calls;memcpy(expected,rmv_log,calls*sizeof(*expected));
    memcpy(rmv_after,rmv_arena,RMV_SIZE);rmv_globals(after,0);
    uint64_t pair[2];pair[0]=(uint64_t)out[1]<<32|out[0];
    if(rmv_fs0()!=head)return fail("removal original left FS:0 changed");
    memcpy(rmv_arena,rmv_before,RMV_SIZE);rmv_globals(before,1);rmv_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];for(unsigned i=0;i<count&&i<8;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);rmv_globals(actual,0);
    if(rmv_fs0()!=head)return fail("removal adapter left FS:0 changed");
    if(n<4096)for(unsigned i=0;i<count&&i<8;++i)if(c[i].count-counts[i]!=rmv_expected_hits(c+i))return fail("removal callee counter delta");
    if(pair[0]!=pair[1]||calls!=rmv_calls||memcmp(expected,rmv_log,calls*sizeof(*expected))||memcmp(rmv_after,rmv_arena,RMV_SIZE)||
       memcmp(after,actual,RMV_GLOBALS))
        return rmv_report("direct",n,pair,calls,expected);
    memcpy(rmv_arena,rmv_before,RMV_SIZE);rmv_globals(before,1);rmv_reset();
    pair[1]=rmv_core(index,a,r,&core_ecx);rmv_globals(actual,0);
    if(rmv_fs0()!=head)return fail("removal core left FS:0 changed");
    /* With the hook installed (integration pass) the "original" run is the
       hook, whose ECX is the dispatcher's; ECX is compared otherwise. */
    const int ecx_known=!rng_hooks[index].installed;
    if(pair[0]!=pair[1]||(ecx_known&&core_ecx!=out[2])||calls!=rmv_calls||memcmp(expected,rmv_log,calls*sizeof(*expected))||
       memcmp(rmv_after,rmv_arena,RMV_SIZE)||memcmp(after,actual,RMV_GLOBALS)){
        fprintf(stderr," ECX %08x/%08x\n",out[2],core_ecx);return rmv_report("core ECX",n,pair,calls,expected);
    }
    rmv_globals(before,1);
    return 0;
}
static int rmv_patch(BYTE saved[RMV_IDS][5],int active[RMV_IDS],unsigned which,int on){
    for(unsigned i=0;i<RMV_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!rmv_addresses[i])continue;if(actor_patch(rmv_addresses[i]-0x400000u,rmv_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(rmv_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static int rmv_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u,head=rmv_fs0();rmv_build(index,n,a,r,0);
        BYTE before[RMV_GLOBALS],after[RMV_GLOBALS],actual[RMV_GLOBALS],expected_fpu[108],actual_fpu[108];static rmv_record expected[RMV_MAX];
        memcpy(rmv_before,rmv_arena,RMV_SIZE);rmv_globals(before,0);
        rmv_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=rmv_calls;memcpy(expected,rmv_log,calls*sizeof(*expected));memcpy(rmv_after,rmv_arena,RMV_SIZE);rmv_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("removal poison install");
        memcpy(rmv_arena,rmv_before,RMV_SIZE);rmv_globals(before,1);rmv_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);rmv_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(rmv_fs0()!=head||pair[0]!=pair[1]||calls!=rmv_calls||memcmp(expected,rmv_log,calls*sizeof(*expected))||memcmp(rmv_after,rmv_arena,RMV_SIZE)||
           memcmp(after,actual,RMV_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=rmv_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("removal poison uninstall");rmv_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state/FS:0\n",d->name,total);
    return status;
}
/* The flush over real 49630c, 496406 and a consistent pool, with pool_free
 * nested in rotating original/replaced/compare modes (compare falls back
 * to replaced where refused); the destructor stays a logging stub. Pair,
 * ordered destructor calls, arena, 6cbee4 and FS:0 are compared against a
 * run with only the nested hook. */
static int rmv_real_pass(unsigned index){
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered=0;
    static rmv_record expected[RMV_MAX];
    rmv_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u,head=rmv_fs0();rmv_build(index,n,a,r,1);
        BYTE before[RMV_GLOBALS],after[RMV_GLOBALS],actual[RMV_GLOBALS];
        memcpy(rmv_before,rmv_arena,RMV_SIZE);rmv_globals(before,0);
        es2_hook hk[2];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        o.mode=(es2_hook_mode)(mode%3);
        const es2_hook_extension *e=function_hook_extension(ES2_HOOK_INDEX_pool_free);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk,function_hook_descriptor(ES2_HOOK_INDEX_pool_free),&o,e))return fail("removal nested install");
        rmv_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        unsigned calls=rmv_calls;memcpy(expected,rmv_log,calls*sizeof(*expected));
        memcpy(rmv_after,rmv_arena,RMV_SIZE);rmv_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+1,d,&o,function_hook_extension(index)))return fail("removal caller install");
        memcpy(rmv_arena,rmv_before,RMV_SIZE);rmv_globals(before,1);rmv_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);rmv_globals(actual,0);
        if(rmv_fs0()!=head||expected_pair[0]!=actual_pair[0]||expected_pair[1]!=actual_pair[1]||calls!=rmv_calls||
           memcmp(expected,rmv_log,calls*sizeof(*expected))||memcmp(rmv_after,rmv_arena,RMV_SIZE)||memcmp(after,actual,RMV_GLOBALS)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            fprintf(stderr,"real mode=%u head %08x/%08x\n",mode,head,rmv_fs0());status=rmv_report("real",n,pair,calls,expected);
        }
        entered+=hk[0].calls;
        for(unsigned j=0;j<2;++j){if(hk[j].mismatches)status=fail("removal nested compare mismatch");
            if(!es2_hook_uninstall(hk+j))return fail("removal nested uninstall");}
        rmv_globals(before,1);++total;
    }
    rmv_real=0;
    if(!status&&!entered){fprintf(stderr,"FAIL: %s real callee pool_free never entered\n",d->name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases, pool_free nested in rotating modes, real 49630c/496406, 31/a7 stack, full pair, calls, memory and FS:0\n",d->name,total);
    return status;
}
static int removal_cases(unsigned index,unsigned limit){
    const int flush=index==ES2_HOOK_INDEX_removal_flush;const unsigned which=flush?0xfu:0u;
    BYTE saved[RMV_IDS][5],old_globals[RMV_GLOBALS];int active[RMV_IDS];
    rmv_globals(old_globals,0);
    if(rmv_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_REMOVAL_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];rmv_build(index,n,a,r,0);status=rmv_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=rmv_installed_probes(index,64);
    if(rmv_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status&&flush)status=rmv_real_pass(index);
    rmv_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair and ECX, ordered calls with entry registers and context word, arena/globals/FS:0\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
