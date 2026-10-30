/* Object destructors: explosion_dtor 407e48, fire_dtor 46b574, beam_dtor
 * 40ba94, debris_dtor 40f7e6, flat_dtor 41bd46, meteor_dtor 424034, bullet_dtor
 * 40b64d, rocket_dtor 40ab8d, shape_dtor 490bdc and shape_base_dtor 48be20
 * (docs/reverse-engineering/main_loop.md, "object destructors"). Direct cases
 * run the original body and the adapter (and the core directly, for ECX) on a 4
 * KiB arena with every game callee a logging stub: 49630c (record stub as in
 * removal_cases.h: real FS:0 head at record+0, context word zeroed), 496406,
 * the shape's virtual +18h (two arena vtables), the heap free 477548, the arena
 * free 47758c, removal_append 478628, sfx_stop 462ac4 and shape_base_dtor
 * 48be20. Ordered calls with their arguments, entry EAX/EDX/ECX (stack
 * addresses masked) and the record's context word (read through FS:0 once a
 * record is linked); the stubs change what the original rereads (the counter
 * pointer 6cbee4, the shape slot O+34h, the link O+53h, the list pointer
 * 4a9602, the shape's vtable after 496406). Compared: the pair, ECX (original
 * through an own caller against the core), the arena, the globals, FS:0 and the
 * callee counter deltas. Then installed 31/a7 poison probes, and a real-callee
 * pass per hook: removal_flush over a consistent pool of objects of the eight
 * classes with their real vtables and shape instances of the real classes
 * 4a62ed, 4a14c4 (its destructor 47895c runs original) and 4a6a14, over real
 * 49630c, 496406, pool_free, removal_append and sfx_stop (only 477548, 47758c
 * and the sound driver call 464118 are stubs); the hook under test in rotating
 * original/replaced modes, the other nine and the four callee hooks nested in
 * rotating modes in both runs. Identifiers: dtr_/DTR_. */
typedef struct dtr_record { uint32_t id,args[2],regs[3],context,eax,edx; } dtr_record;
enum { DTR_SIZE=0x1000, DTR_O=0x100, DTR_S1=0x300, DTR_S2=0x340, DTR_VT1=0x400, DTR_VT2=0x440, DTR_COUNTER=0x500,
    DTR_COUNTER2=0x510, DTR_MAX=64, DTR_IDS=9, DTR_GLOBALS=20, DTR_HOOKS=10,
    /* real pass */
    DTR_L=0x40, DTR_ITEMS=0x60, DTR_LIST1=0x90, DTR_LIST1_ITEMS=0xa0, DTR_LIST2=0xe0, DTR_LIST2_ITEMS=0xf0, DTR_POOL=0x140,
    DTR_NODES=0x200, DTR_NODE_COUNT=8, DTR_SHAPES=0x600, DTR_SFX=0x720 };
#define DTR_BASE ((uint32_t)(uintptr_t)dtr_arena)
#define DTR_NODE(i) (DTR_BASE+DTR_NODES+0x80u*(unsigned)(i))
#define DTR_SHAPE(i) (DTR_BASE+DTR_SHAPES+0x20u*(unsigned)(i))
static BYTE dtr_arena[DTR_SIZE],dtr_before[DTR_SIZE],dtr_after[DTR_SIZE];
static dtr_record dtr_log[DTR_MAX];
static unsigned dtr_calls;
static int dtr_real,dtr_frame;
static uint32_t dtr_seed,dtr_stream,dtr_o;
__attribute__((used)) static uint32_t dtr_ecx;
/* 0 49630c, 1 496406, 2 slot +18h, 3 477548, 4 47758c, 5 478628,
 * 6 462ac4, 7 48be20, 8 464118 (real pass only). */
static const uint32_t dtr_addresses[DTR_IDS]={0x49630c,0x496406,0,0x477548,0x47758c,0x478628,0x462ac4,0x48be20,0x464118};
static const unsigned dtr_slots[DTR_IDS]={0,0,0x18,0,0,0,0,0,0};
static const unsigned dtr_counts[DTR_IDS]={0,2,2,1,1,2,1,2,2};
/* The ten hooks in registry order, their kinds and the real vtables. */
static const unsigned dtr_hooks[DTR_HOOKS]={ES2_HOOK_INDEX_explosion_dtor,ES2_HOOK_INDEX_fire_dtor,ES2_HOOK_INDEX_beam_dtor,
    ES2_HOOK_INDEX_debris_dtor,ES2_HOOK_INDEX_flat_dtor,ES2_HOOK_INDEX_meteor_dtor,ES2_HOOK_INDEX_bullet_dtor,
    ES2_HOOK_INDEX_rocket_dtor,ES2_HOOK_INDEX_shape_dtor,ES2_HOOK_INDEX_shape_base_dtor};
static const uint32_t dtr_vtables[ES2_DTOR_KINDS]={0x497d4c,0x4a0ba0,0x4987c4,0x497ed8,0x49a262,0x4982b8,0x498628,0x498448};
static const uint32_t dtr_shape_vtables[3]={0x4a62ed,0x4a14c4,0x4a6a14};
static int dtr_kind(unsigned index){for(unsigned k=0;k<ES2_DTOR_KINDS;++k)if(dtr_hooks[k]==index)return (int)k;return -1;}
static uint32_t dtr_next(void){
    uint32_t z=dtr_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *dtr_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static BYTE *dtr_at(unsigned offset){return dtr_arena+offset;}
static int dtr_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static uint32_t dtr_fs0(void){uint32_t v;__asm__ volatile("movl %%fs:0,%0":"=r"(v));return v;}
static uint32_t dtr_shape_pick(uint32_t pick){uint32_t b=DTR_BASE;return pick%3==0?0:b+(pick%3==1?DTR_S1:DTR_S2);}
/* Rereads of the destructors between their calls (direct passes only). */
static void dtr_touch(unsigned id,const uint32_t *a,uint32_t pick){
    if(dtr_real)return;
    uint32_t b=DTR_BASE;
    if(pick%5==0)put32(mapped+0x2cbee4,b+(pick>>8&1?DTR_COUNTER2:DTR_COUNTER));
    if(dtr_o&&pick%7==0)put32(dtr_host(dtr_o+0x34),dtr_shape_pick(pick>>9));
    if(dtr_o&&pick%11==0)put32(dtr_host(dtr_o+0x53),pick>>12&1?0:dtr_next());
    if(pick%13==0)put32(mapped+0xa9602,dtr_next());
    if(id==1&&pick%3==0)put32(dtr_host(a[0]),b+(pick>>10&1?DTR_VT2:DTR_VT1));
}
__attribute__((used,noinline)) static uint64_t __cdecl dtr_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(dtr_calls==DTR_MAX){fail("destructors synthetic call overflow");ExitProcess(1);}
    dtr_record *v=dtr_log+dtr_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,4*dtr_counts[id]);memcpy(v->regs,regs,12);
    for(unsigned j=0;j<3;++j)if(dtr_stack(v->regs[j]))v->regs[j]=0;
    for(unsigned j=0;j<dtr_counts[id];++j)if(dtr_stack(v->args[j]))v->args[j]=0;
    if(dtr_real)v->regs[2]=0; /* ECX after a nested hook is the framework's; no stub reads it */
    if(id&&dtr_frame)v->context=get16(dtr_host(dtr_fs0()+16u));
    uint32_t eax=dtr_next(),edx=dtr_next(),pick=dtr_next();dtr_ecx=dtr_next();
    if(id==0){eax=dtr_fs0();dtr_frame=1;} /* the record stub returns the real FS:0 head */
    dtr_touch(id,a,pick);
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
/* Pushes the entry EAX/EDX/ECX for the record before the slot pointer. */
#define DTR_STUB(n) __attribute__((naked)) static void dtr_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _dtr_stub_impl\n\taddl $24,%esp\n\tmovl _dtr_ecx,%ecx\n\tret\n\t");}
DTR_STUB(1) DTR_STUB(2) DTR_STUB(3) DTR_STUB(4) DTR_STUB(5) DTR_STUB(6) DTR_STUB(7) DTR_STUB(8)
#undef DTR_STUB
/* 49630c: captures the entry registers like the other stubs, then links a
 * record at EBP + [descriptor+4] into FS:0 with the saved head at record+0
 * and a zero context word at record+16, as the original does. */
__attribute__((naked)) static void dtr_stub_0(void){__asm__ volatile(
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
    "pushl $0\n\tcalll _dtr_stub_impl\n\taddl $12,%esp\n\t"
    "popl %ecx\n\tmovl 4(%ecx),%ecx\n\taddl %ebp,%ecx\n\tmovl %eax,(%ecx)\n\tmovw $0,16(%ecx)\n\tmovl %ecx,%fs:0\n\t"
    "popl %edx\n\taddl $4,%esp\n\tmovl _dtr_ecx,%ecx\n\tret\n\t");}
static void *dtr_stubs[DTR_IDS]={dtr_stub_0,dtr_stub_1,dtr_stub_2,dtr_stub_3,dtr_stub_4,dtr_stub_5,dtr_stub_6,dtr_stub_7,dtr_stub_8};
/* A two-slot cdecl caller that hands over EAX/EDX/ECX and returns all
 * three (the hook ABI returns EDX:EAX only). */
__attribute__((naked,noinline)) static void dtr_machine(void *target,const uint32_t *a,const uint32_t *registers,uint32_t *out){
    __asm__ volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl 12(%ebp),%esi\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* Globals: the counter pointer 6cbee4, the removal lists 4a9691/4a9602,
 * the fire count 6b4f38, the sound context 49f8ec. */
static void dtr_globals(BYTE *out,int restore){
    static const uint32_t rva[5]={0x2cbee4,0xa9691,0xa9602,0x2b4f38,0x9f8ec};
    for(unsigned i=0;i<5;++i){if(restore)memcpy(mapped+rva[i],out+4*i,4);else memcpy(out+4*i,mapped+rva[i],4);}
}
static void dtr_reset(void){dtr_calls=0;dtr_frame=0;dtr_stream=dtr_seed;}
/* Direct passes. Arguments (O, flag) for the object destructors, (S, flag)
 * for the shape pair; the shapes S1/S2 use the two arena vtables. */
static void dtr_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    uint32_t b=DTR_BASE;
    dtr_seed=random32();random_bytes(dtr_arena,DTR_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    for(unsigned v=DTR_VT1;v<=DTR_VT2;v+=DTR_VT2-DTR_VT1)put32(dtr_at(v+0x18),(uint32_t)(uintptr_t)dtr_stubs[2]);
    put32(dtr_at(DTR_S1),b+(random32()%2?DTR_VT2:DTR_VT1));put32(dtr_at(DTR_S2),b+(random32()%2?DTR_VT2:DTR_VT1));
    put32(mapped+0x2cbee4,b+DTR_COUNTER);put32(mapped+0xa9691,random32());put32(mapped+0xa9602,random32());
    static const uint32_t fires[4]={1,2,0,0x80000000u};
    put32(mapped+0x2b4f38,n%3==0?1:fires[random32()%4]);put32(mapped+0x9f8ec,random32());
    a[1]=random32();
    if(n<4)a[1]=n; /* flag 0..3 first */
    if(dtr_kind(index)<0){ /* the shape pair */
        dtr_o=0;a[0]=n%5==1?0:b+DTR_S1;return;
    }
    dtr_o=a[0]=n%7==3?0:b+DTR_O+random32()%4u;
    if(!dtr_o)return;
    put32(dtr_host(dtr_o+0x34),dtr_shape_pick(random32()));
    if(random32()%3==0)put32(dtr_host(dtr_o+0x4f),0);
    if(random32()%3==0)put32(dtr_host(dtr_o+0x53),0);
}
static int dtr_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const dtr_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,dtr_calls);
    for(unsigned j=0;j<calls&&j<dtr_calls&&expected;++j)if(memcmp(expected+j,dtr_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(dtr_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<DTR_SIZE;++j)if(dtr_after[j]!=dtr_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,dtr_after[j],dtr_arena[j]);break;}
    return 1;
}
static unsigned dtr_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<dtr_calls;++j){unsigned id=dtr_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?dtr_slots[id]==c->key:!dtr_slots[id]&&dtr_addresses[id]==c->key)++hits;}
    return hits;
}
/* The core called directly (the adapters drop ECX), with the adapter's
 * graph; FS:0 restored to the returned head. */
static uint64_t dtr_core(unsigned index,const uint32_t *a,const uint32_t *r,uint32_t *ecx){
    const int kind=dtr_kind(index);
    if(kind<0){
        es2_weapon_graph g=weapons_graph(mapped,NULL,index);g.observe=NULL;
        if(index==ES2_HOOK_INDEX_shape_dtor)return es2_shape_dtor_registers(&math_native_memory,&g,a[0],a[1],r,ecx);
        return es2_shape_base_dtor_registers(&math_native_memory,&g,a[0],a[1],r,ecx);
    }
    BYTE scratch[ES2_DESTRUCTORS_SCRATCH]={0};es2_weapon_graph g=weapons_graph(mapped,scratch,index);
    g.observe=NULL;weapons_impact_context c={(uint32_t)(uintptr_t)mapped,(uint32_t)(uintptr_t)(scratch+ES2_DESTRUCTORS_FRAME)};
    g.call=weapons_impact_guest_call;g.context=&c;uint32_t head;
    uint64_t v=es2_object_dtor_registers(&math_native_memory,&g,(unsigned)kind,a[0],a[1],r,ecx,&head);
    __asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");return v;
}
static int dtr_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[DTR_GLOBALS],after[DTR_GLOBALS],actual[DTR_GLOBALS];static dtr_record expected[DTR_MAX];
    uint32_t head=dtr_fs0(),out[3],core_ecx=0;
    memcpy(dtr_before,dtr_arena,DTR_SIZE);dtr_globals(before,0);
    dtr_reset();dtr_machine(mapped+d->rva,a,r,out);unsigned calls=dtr_calls;memcpy(expected,dtr_log,calls*sizeof(*expected));
    memcpy(dtr_after,dtr_arena,DTR_SIZE);dtr_globals(after,0);
    uint64_t pair[2];pair[0]=(uint64_t)out[1]<<32|out[0];
    if(dtr_fs0()!=head)return fail("destructors original left FS:0 changed");
    memcpy(dtr_arena,dtr_before,DTR_SIZE);dtr_globals(before,1);dtr_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];for(unsigned i=0;i<count&&i<8;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);dtr_globals(actual,0);
    if(dtr_fs0()!=head)return fail("destructors adapter left FS:0 changed");
    if(n<4096)for(unsigned i=0;i<count&&i<8;++i)if(c[i].count-counts[i]!=dtr_expected_hits(c+i))return fail("destructors callee counter delta");
    if(pair[0]!=pair[1]||calls!=dtr_calls||memcmp(expected,dtr_log,calls*sizeof(*expected))||memcmp(dtr_after,dtr_arena,DTR_SIZE)||
       memcmp(after,actual,DTR_GLOBALS))
        return dtr_report("direct",n,pair,calls,expected);
    memcpy(dtr_arena,dtr_before,DTR_SIZE);dtr_globals(before,1);dtr_reset();
    pair[1]=dtr_core(index,a,r,&core_ecx);dtr_globals(actual,0);
    if(dtr_fs0()!=head)return fail("destructors core left FS:0 changed");
    /* With the hook installed (integration pass) the "original" run is the
       hook, whose ECX is the dispatcher's; ECX is compared otherwise. */
    const int ecx_known=!rng_hooks[index].installed;
    if(pair[0]!=pair[1]||(ecx_known&&core_ecx!=out[2])||calls!=dtr_calls||memcmp(expected,dtr_log,calls*sizeof(*expected))||
       memcmp(dtr_after,dtr_arena,DTR_SIZE)||memcmp(after,actual,DTR_GLOBALS)){
        fprintf(stderr," ECX %08x/%08x\n",out[2],core_ecx);return dtr_report("core ECX",n,pair,calls,expected);
    }
    dtr_globals(before,1);
    return 0;
}
static int dtr_patch(BYTE saved[DTR_IDS][5],int active[DTR_IDS],unsigned which,int on){
    for(unsigned i=0;i<DTR_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!dtr_addresses[i])continue;if(actor_patch(dtr_addresses[i]-0x400000u,dtr_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
    }
    if(!on)for(unsigned i=DTR_IDS;i-->0;)if(active[i]&&actor_patch(dtr_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    return 0;
}
static int dtr_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u,head=dtr_fs0();dtr_build(index,n,a,r);
        BYTE before[DTR_GLOBALS],after[DTR_GLOBALS],actual[DTR_GLOBALS],expected_fpu[108],actual_fpu[108];static dtr_record expected[DTR_MAX];
        memcpy(dtr_before,dtr_arena,DTR_SIZE);dtr_globals(before,0);
        dtr_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=dtr_calls;memcpy(expected,dtr_log,calls*sizeof(*expected));memcpy(dtr_after,dtr_arena,DTR_SIZE);dtr_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("destructors poison install");
        memcpy(dtr_arena,dtr_before,DTR_SIZE);dtr_globals(before,1);dtr_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);dtr_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(dtr_fs0()!=head||pair[0]!=pair[1]||calls!=dtr_calls||memcmp(expected,dtr_log,calls*sizeof(*expected))||memcmp(dtr_after,dtr_arena,DTR_SIZE)||
           memcmp(after,actual,DTR_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=dtr_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("destructors poison uninstall");dtr_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state/FS:0\n",d->name,total);
    return status;
}
/* Real pass: a consistent pool (k of the eight nodes used, the rest free),
 * each node's object of a random class with its real vtable, a shape
 * instance of a real class (or none) at +34h, the removal list a shuffled
 * subset of the used objects. */
static void dtr_real_build(unsigned n,uint32_t a[6],uint32_t r[3]){
    uint32_t b=DTR_BASE;unsigned order[DTR_NODE_COUNT],k=1u+random32()%DTR_NODE_COUNT;
    dtr_seed=random32();random_bytes(dtr_arena,DTR_SIZE);memset(a,0,24);dtr_o=0;
    for(unsigned i=0;i<3;++i)r[i]=random32();
    for(unsigned i=0;i<DTR_NODE_COUNT;++i){
        uint32_t o=DTR_NODE(i)+8u;unsigned kind=(n+i)%ES2_DTOR_KINDS;if(random32()%2)kind=random32()%ES2_DTOR_KINDS;
        put32(dtr_host(o),dtr_vtables[kind]);
        unsigned s=random32()%8;put32(dtr_host(o+0x34),s<2?0:DTR_SHAPE(i));
        put32(dtr_host(DTR_SHAPE(i)),dtr_shape_vtables[s<5?0:s<7?1:2]);
        if(random32()%3==0)put32(dtr_host(o+0x4f),0);
        if(random32()%3==0)put32(dtr_host(o+0x53),0);
    }
    for(unsigned i=0;i<DTR_NODE_COUNT;++i)order[i]=i;
    for(unsigned i=DTR_NODE_COUNT;i>1;--i){unsigned j=random32()%i,t=order[i-1];order[i-1]=order[j];order[j]=t;}
    for(unsigned i=0;i<DTR_NODE_COUNT;++i){
        const int used=i<k;const unsigned first=used?0:k,last=used?k:DTR_NODE_COUNT;
        uint32_t node=DTR_NODE(order[i]);
        put32(dtr_host(node),i+1<last?DTR_NODE(order[i+1]):0);put32(dtr_host(node)+4,i>first?DTR_NODE(order[i-1]):0);
    }
    uint32_t pool=b+DTR_POOL;
    put16(dtr_host(pool),(uint16_t)(DTR_NODE_COUNT-k));put16(dtr_host(pool)+2,(uint16_t)k);
    put32(dtr_host(pool)+0xa,k<DTR_NODE_COUNT?DTR_NODE(order[k]):0);put32(dtr_host(pool)+0xe,DTR_NODE(order[0]));
    unsigned s=1u+random32()%k;
    for(unsigned i=k;i>1;--i){unsigned j=random32()%i,t=order[i-1];order[i-1]=order[j];order[j]=t;}
    for(unsigned i=0;i<s;++i)put32(dtr_at(DTR_ITEMS+4*i),DTR_NODE(order[i])+8u);
    put32(dtr_at(DTR_L),b+DTR_ITEMS);put16(dtr_at(DTR_L+4),(uint16_t)s);
    put32(dtr_at(DTR_LIST1),b+DTR_LIST1_ITEMS);put16(dtr_at(DTR_LIST1+4),0);
    put32(dtr_at(DTR_LIST2),b+DTR_LIST2_ITEMS);put16(dtr_at(DTR_LIST2+4),0);
    put32(mapped+0x2cbee4,b+DTR_COUNTER);put32(mapped+0xa9691,b+DTR_LIST1);put32(mapped+0xa9602,b+DTR_LIST2);
    put32(mapped+0x2b4f38,1u+random32()%3u);put32(mapped+0x9f8ec,b+DTR_SFX);
    a[0]=b+DTR_L;a[1]=pool;
}
/* The nested hooks: the four callees/drivers and the other nine. */
static const unsigned dtr_callee_hooks[]={ES2_HOOK_INDEX_removal_flush,ES2_HOOK_INDEX_pool_free,ES2_HOOK_INDEX_removal_append,
    ES2_HOOK_INDEX_sfx_stop};
enum { DTR_CALLEE_HOOKS=sizeof(dtr_callee_hooks)/sizeof(dtr_callee_hooks[0]), DTR_NESTED=DTR_CALLEE_HOOKS+DTR_HOOKS-1 };
static int dtr_install(es2_hook *h,unsigned index,unsigned mode){
    es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=(es2_hook_mode)mode;
    const es2_hook_extension *e=function_hook_extension(index);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
    return weapons_install_hook(h,function_hook_descriptor(index),&o,e)?0:1;
}
static int dtr_real_pass(unsigned index){
    const es2_hook_descriptor *d=function_hook_descriptor(index),*flush=function_hook_descriptor(ES2_HOOK_INDEX_removal_flush);
    int status=0;unsigned total=0;uint64_t entered=0;static dtr_record expected[DTR_MAX];
    BYTE saved[DTR_IDS][5];int active[DTR_IDS];
    unsigned nested[DTR_NESTED],count=0;
    for(unsigned j=0;j<DTR_CALLEE_HOOKS;++j)nested[count++]=dtr_callee_hooks[j];
    for(unsigned j=0;j<DTR_HOOKS;++j)if(dtr_hooks[j]!=index)nested[count++]=dtr_hooks[j];
    if(dtr_patch(saved,active,(1u<<3)|(1u<<4)|(1u<<8),1))return 1;
    dtr_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<256&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u,head=dtr_fs0();dtr_real_build(n,a,r);
        BYTE before[DTR_GLOBALS],after[DTR_GLOBALS],actual[DTR_GLOBALS];
        memcpy(dtr_before,dtr_arena,DTR_SIZE);dtr_globals(before,0);
        es2_hook hk[DTR_NESTED+1];memset(hk,0,sizeof(hk));
        for(unsigned j=0;j<count;++j)if(dtr_install(hk+j,nested[j],(mode+j)%3))return fail("destructors nested install");
        dtr_reset();sg_poison_call(mapped+flush->rva,a,pattern,expected_pair);
        unsigned calls=dtr_calls;memcpy(expected,dtr_log,calls*sizeof(*expected));
        memcpy(dtr_after,dtr_arena,DTR_SIZE);dtr_globals(after,0);
        if(dtr_install(hk+count,index,mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED))return fail("destructors caller install");
        memcpy(dtr_arena,dtr_before,DTR_SIZE);dtr_globals(before,1);dtr_reset();
        sg_poison_call(mapped+flush->rva,a,pattern,actual_pair);dtr_globals(actual,0);
        if(dtr_fs0()!=head||expected_pair[0]!=actual_pair[0]||expected_pair[1]!=actual_pair[1]||calls!=dtr_calls||
           memcmp(expected,dtr_log,calls*sizeof(*expected))||memcmp(dtr_after,dtr_arena,DTR_SIZE)||memcmp(after,actual,DTR_GLOBALS)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            for(unsigned j=0;j<DTR_GLOBALS;++j)if(after[j]!=actual[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,after[j],actual[j]);break;}
            fprintf(stderr,"real mode=%u head %08x/%08x\n",mode,head,dtr_fs0());status=dtr_report("real",n,pair,calls,expected);
        }
        entered+=hk[count].calls;
        for(unsigned j=0;j<=count;++j){if(hk[j].mismatches)status=fail("destructors nested compare mismatch");
            if(!es2_hook_uninstall(hk+j))return fail("destructors nested uninstall");}
        dtr_globals(before,1);++total;
    }
    dtr_real=0;
    if(dtr_patch(saved,active,0,0))return 1;
    if(!status&&!entered){fprintf(stderr,"FAIL: %s never entered in the real-callee pass\n",d->name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases (entered %llu times) under removal_flush, real 49630c/496406/47895c, pool_free/"
        "removal_append/sfx_stop and the other nine destructors nested in rotating modes, 31/a7 stack, full pair, calls, memory, "
        "globals and FS:0\n",d->name,total,(unsigned long long)entered);
    return status;
}
static int destructors_cases(unsigned index,unsigned limit){
    /* every stub but the function under test (48be20 for shape_base_dtor) and 464118 */
    const unsigned which=index==ES2_HOOK_INDEX_shape_base_dtor?0x7fu:0xffu;
    BYTE saved[DTR_IDS][5],old_globals[DTR_GLOBALS];int active[DTR_IDS];
    dtr_globals(old_globals,0);
    if(dtr_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_DESTRUCTORS_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];dtr_build(index,n,a,r);status=dtr_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=dtr_installed_probes(index,64);
    if(dtr_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=dtr_real_pass(index);
    dtr_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair and ECX, ordered calls with entry registers and context word, arena/globals/FS:0\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
