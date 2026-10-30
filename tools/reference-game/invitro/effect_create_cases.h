/* Effect creation: explosion_create 407f1c, object_init 402188, light_create
 * 407604, pool_alloc 471b30. Direct cases run the original body and the core
 * against logging stubs for every game callee (entry registers recorded, buffer
 * arguments compared by content), with the runtime frame setup 49630c
 * reproduced by a record stub that keeps the real FS:0 chain head and zeroes
 * the context word, exactly as the original expects. The pure leaf 407b20 runs
 * as original code. Then installed 31/a7 poison probes and, for
 * explosion_create, a real-callee pass with the pool allocation nested as a
 * hook in rotating modes. pool_alloc is a pure leaf (no game calls, no frame
 * record): it is compared directly and under installed probes. */
typedef struct ec_record { uint32_t id,args[3],regs[3],input[3],eax,edx,slots; } ec_record;
enum { EC_SIZE=0x1400, EC_OBJECT=0x100, EC_SHAPE=0x180, EC_NODE=0x1a0, EC_POS=0x1c0, EC_TYPES=0x200, EC_SHAPETAB=0x400,
    EC_ANIMS=0x440, EC_LINKED=0x480, EC_LIGHT=0x500, EC_LINKSHAPE=0x580, EC_FLATPOOL=0x600, EC_LIGHTPOOL=0x640,
    EC_FLATNODES=0x700, EC_LIGHTNODES=0x900, EC_SHAPEENT=0xb00, EC_LIGHTTAB=0xc00, EC_COUNTER=0xf00,
    EC_IDS=11, EC_MAX=24, EC_GLOBALS=36 };
#define EC_BASE ((uint32_t)(uintptr_t)ec_arena)
static BYTE ec_arena[EC_SIZE],ec_before[EC_SIZE],ec_after[EC_SIZE];
static ec_record ec_log[EC_MAX];
static unsigned ec_calls;
static uint32_t ec_seed,ec_stream;
__attribute__((used)) static uint32_t ec_ecx;
/* 49630c (record), 402188, 471b30, 407604, 47749c, 490b64, 49681a (memset),
 * 406f38, 407048, 492f24 (rng_bounded), 4628e0 (sound). */
static const uint32_t ec_addresses[EC_IDS]={0x49630c,0x402188,0x471b30,0x407604,0x47749c,0x490b64,0x49681a,0x406f38,0x407048,0x492f24,0x4628e0};
static const unsigned ec_counts[EC_IDS]={0,2,1,3,1,2,3,2,3,2,2};
static uint32_t ec_next(void){
    uint32_t z=ec_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *ec_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
/* Same fixed stack window as node_cases.h: frame addresses are masked. */
static int ec_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static uint32_t ec_flat_result,ec_light_result;
__attribute__((used,noinline)) static uint64_t __cdecl ec_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(ec_calls==EC_MAX){fail("effect synthetic call overflow");ExitProcess(1);}
    ec_record *v=ec_log+ec_calls++;memset(v,0,sizeof(*v));v->id=id;v->slots=weapons_bridge_slots(a,ec_counts[id]);memcpy(v->args,a,4*ec_counts[id]);memcpy(v->regs,regs,12);
    for(unsigned j=0;j<3;++j)if(ec_stack(v->regs[j]))v->regs[j]=0;
    for(unsigned j=0;j<ec_counts[id];++j)if(ec_stack(v->args[j]))v->args[j]=0;
    uint32_t eax=ec_next(),edx=ec_next(),pick=ec_next();ec_ecx=ec_next();
    switch(id){
    case 0:/* 49630c record stub returns the real FS:0 head for ec_stub_0. */
        __asm__ volatile("movl %%fs:0,%0":"=r"(eax));break;
    case 1: /* 402188(O,shape): sets O+34 to a valid shape (as the real
             * constructor does; its +8 frame array is read afterwards) and
             * returns O. */
        put32(ec_host(a[0])+0x34,EC_BASE+EC_SHAPE);eax=a[0];break;
    case 2: /* 471b30(pool): the flat pool always succeeds (proof); the light
             * pool returns 0 roughly a third of the time. */
        if(a[0]==EC_BASE+EC_FLATPOOL)eax=EC_BASE+EC_LINKED;
        else if(a[0]==EC_BASE+EC_LIGHTPOOL)eax=pick%3?EC_BASE+EC_LIGHT:0;
        else eax=pick%2?EC_BASE+EC_LINKED:0;
        break;
    case 3: /* 407604(L,P,v): returns L. */ eax=a[0];break;
    case 4: /* 47749c(size): heap block or 0. */ eax=pick%4?EC_BASE+EC_SHAPEENT:0;break;
    case 7: /* 406f38(table,P): light index. */ eax=(eax&0xffff0000u)|(uint16_t)(pick%8);break;
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
/* Records the entry EAX/EDX/ECX before the slot pointer. */
#define EC_STUB(n) __attribute__((naked)) static void ec_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _ec_stub_impl\n\taddl $24,%esp\n\tmovl _ec_ecx,%ecx\n\tret\n\t");}
EC_STUB(1) EC_STUB(2) EC_STUB(3) EC_STUB(4) EC_STUB(5) EC_STUB(6) EC_STUB(7) EC_STUB(8) EC_STUB(9) EC_STUB(10)
#undef EC_STUB
/* 49630c: the Borland runtime frame setup. Captures the entry registers like
 * the other stubs (ec_stub_impl returns the real FS:0 head in EAX for id 0),
 * then links a record at EBP + [descriptor+4] into FS:0 with the saved head
 * at record+0 and a zero context word at record+16, as the original does. */
__attribute__((naked)) static void ec_stub_0(void){__asm__ volatile(
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
    "pushl $0\n\tcalll _ec_stub_impl\n\taddl $12,%esp\n\t"
    "popl %ecx\n\tmovl 4(%ecx),%ecx\n\taddl %ebp,%ecx\n\tmovl %eax,(%ecx)\n\tmovw $0,16(%ecx)\n\tmovl %ecx,%fs:0\n\t"
    "popl %edx\n\taddl $4,%esp\n\tmovl _ec_ecx,%ecx\n\tret\n\t");}
static void *ec_stubs[EC_IDS]={ec_stub_0,ec_stub_1,ec_stub_2,ec_stub_3,ec_stub_4,ec_stub_5,ec_stub_6,ec_stub_7,ec_stub_8,ec_stub_9,ec_stub_10};
/* Which callees each hook reaches (bit i = ec_addresses[i]). */
static unsigned ec_which(unsigned index){
    if(index==ES2_HOOK_INDEX_explosion_create)return 0x60f;      /* 49630c,402188,471b30,407604,492f24,4628e0 */
    if(index==ES2_HOOK_INDEX_object_init)return 0x71;            /* 49630c,47749c,490b64,49681a */
    if(index==ES2_HOOK_INDEX_light_create)return 0x180;          /* 406f38,407048 */
    return 0;                                                    /* pool_alloc: none */
}
/* 49630c (counter is restored from DATA), the type/shape/animation/light
 * tables, the two pools, the construction count. */
static void ec_globals(BYTE *out,int restore){
    static const unsigned offsets[]={0x2cbee4,0xa9628,0xa9630,0xa963c,0xa9608,0xa968d,0xa95fe,0xa9698},sizes[]={4,4,4,4,4,4,4,4};
    for(unsigned i=0;i<8;++i){if(restore)memcpy(mapped+offsets[i],out,sizes[i]);else memcpy(out,mapped+offsets[i],sizes[i]);out+=sizes[i];}
    uint32_t counter=EC_BASE+EC_COUNTER;if(restore)memcpy(ec_arena+EC_COUNTER,out,4);else memcpy(out,ec_arena+EC_COUNTER,4);(void)counter;
}
static void ec_reset(void){ec_calls=0;ec_stream=ec_seed;}
static void ec_build_pool(uint32_t pool,uint32_t nodes,unsigned count){
    put16(ec_host(pool),(uint16_t)count);put16(ec_host(pool)+2,0);put32(ec_host(pool)+0xe,0);
    for(unsigned i=0;i<count;++i){uint32_t n=nodes+0x28u*i;put32(ec_host(n),i+1<count?n+0x28u:0);put32(ec_host(n)+4,i?n-0x28u:0);}
    put32(ec_host(pool)+0xa,count?nodes:0);
}
static void ec_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    ec_seed=random32();random_bytes(ec_arena,EC_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    uint32_t b=EC_BASE;put32(mapped+0x2cbee4,b+EC_COUNTER);put32(ec_arena+EC_COUNTER,random32());
    if(index==ES2_HOOK_INDEX_pool_alloc){
        static const unsigned counts[]={0,1,2,3,0,1};unsigned c=n<1024?counts[n%6]:random32()%4;
        ec_build_pool(b+EC_OBJECT,b+EC_FLATNODES,c);
        /* A nonzero used-count word +2 (a read of +1 or +3 is visible). */
        if(n%4)put16(ec_host(b+EC_OBJECT)+2,(uint16_t)random32());
        /* A stale back link in the free head (the clear of node+4 is visible). */
        if(c&&n%5<2)put32(ec_host(b+EC_FLATNODES)+4,random32());
        /* A free successor present/absent, a used head present/absent. */
        if(c&&n%2)put32(ec_host(b+EC_FLATNODES),0);
        if(c&&n%3)put32(ec_host(b+EC_OBJECT)+0xe,b+EC_LINKED);else if(c)put32(ec_host(b+EC_OBJECT)+0xe,0);
        a[0]=b+EC_OBJECT;return;
    }
    if(index==ES2_HOOK_INDEX_object_init){
        a[0]=b+EC_OBJECT;a[1]=n%4?b+0x1234:0;return;
    }
    if(index==ES2_HOOK_INDEX_light_create){
        put32(mapped+0xa9608,b+EC_LIGHTTAB);a[0]=b+EC_LIGHT;a[1]=b+EC_POS;a[2]=random32();return;
    }
    /* explosion_create. The 16-bit type indexes the type table (40-byte
     * entries) and, through the type entry's word +0, the shape and
     * animation tables; the low byte O+41 drives 407b20. Both indices are
     * kept in the small signed range -2..2 so every table access and the
     * frame-reset write stay inside the arena, while still covering every
     * branch and both signs. */
    static const uint16_t idxwords[]={0,1,2,0xffff,0xfffe};   /* s16 -2..2 */
    static const int types_i[]={0,1,2,-1,-2};
    uint32_t types=b+EC_TYPES+0x80;   /* room below for the -2 index entry */
    put32(mapped+0xa9628,types);put32(mapped+0xa9630,b+EC_SHAPETAB);put32(mapped+0xa963c,b+EC_ANIMS);
    put32(mapped+0xa968d,b+EC_FLATPOOL);put32(mapped+0xa95fe,b+EC_LIGHTPOOL);put32(mapped+0xa9698,b+EC_LIGHTTAB);
    ec_build_pool(b+EC_FLATPOOL,b+EC_FLATNODES,1+random32()%3);
    ec_build_pool(b+EC_LIGHTPOOL,b+EC_LIGHTNODES,random32()%4);
    /* Type entries for signed indices -2..2 (EC_TYPES+0x80 is the zero entry). */
    for(int k=-2;k<=2;++k){BYTE *t=ec_host(types)+40*k;
        put16(t,idxwords[n<1024?(n+(unsigned)(k+2))%5:random32()%5]);   /* shape/anim index */
        put16(t+2,(uint16_t)random32());put16(t+4,random32()%3?(uint16_t)(1+random32()%3):0);
        put16(t+6,random32()%3?(uint16_t)(1+random32()%3):0);put16(t+8,(uint16_t)random32());
        /* Sound word +24h: 0..7, FFFFh, and 0080h..00FFh / FF00h..FF7Fh, whose two
         * bytes differ in sign, so a sign test of the word at +23h is visible. */
        {uint32_t pick=random32()%6;put16(t+0x24,pick<3?(uint16_t)(random32()%8):pick==3?0xffff:pick==4?(uint16_t)(0x80u|random32()%0x80u):(uint16_t)(0xff00u|random32()%0x80u));}
        put16(t+0x26,random32()%2?(uint16_t)random32():0);}
    /* Shape table (dword shape pointers) and animation table (words), both
     * indexed by the type entry's word +0 in -2..2. */
    for(int k=-2;k<=2;++k){put32(ec_arena+EC_SHAPETAB+4*k,b+EC_SHAPEENT);
        put16(ec_arena+EC_ANIMS+4*k,idxwords[random32()%4]);}  /* 0,1,2 reset a frame; -1 skips */
    put32(ec_arena+EC_OBJECT+0x34,b+EC_SHAPE);put32(ec_arena+EC_SHAPE+8,b+EC_NODE);
    put32(ec_arena+EC_LIGHTTAB+4,b+EC_SHAPEENT);
    unsigned ti=n<1024?n%5:random32()%5;
    a[0]=b+EC_OBJECT;a[1]=(uint32_t)(int32_t)types_i[ti];   /* s16 and s8 both -2..2 */
    a[2]=b+EC_POS;a[3]=random32();a[4]=n%3?random32()|1:0;
    for(unsigned j=0;j<3;++j)put32(ec_arena+EC_POS+4*j,random32());
    ec_flat_result=b+EC_LINKED;ec_light_result=b+EC_LIGHT;
}
static int ec_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const ec_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,ec_calls);
    for(unsigned j=0;j<calls&&j<ec_calls&&expected;++j)if(memcmp(expected+j,ec_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(ec_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<EC_SIZE;++j)if(ec_after[j]!=ec_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,ec_after[j],ec_arena[j]);break;}
    return 1;
}
static unsigned ec_expected_hits(uint32_t key){
    unsigned hits=0;for(unsigned j=0;j<ec_calls;++j)if(ec_addresses[ec_log[j].id]==key)++hits;return hits;
}
static int ec_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[EC_GLOBALS],after[EC_GLOBALS],actual[EC_GLOBALS];ec_record expected[EC_MAX];uint32_t head,head_after;
    memcpy(ec_before,ec_arena,EC_SIZE);ec_globals(before,0);
    __asm__ volatile("movl %%fs:0,%0":"=r"(head));
    ec_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=ec_calls;memcpy(expected,ec_log,calls*sizeof(*expected));
    memcpy(ec_after,ec_arena,EC_SIZE);ec_globals(after,0);
    memcpy(ec_arena,ec_before,EC_SIZE);ec_globals(before,1);ec_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);ec_globals(actual,0);
    __asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
    if(head_after!=head)return fail("effect left FS:0 changed");
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=ec_expected_hits(c[i].key))return fail("effect callee counter delta");
    if(pair[0]!=pair[1]||calls!=ec_calls||memcmp(expected,ec_log,calls*sizeof(*expected))||memcmp(ec_after,ec_arena,EC_SIZE)||memcmp(after,actual,EC_GLOBALS)){
        for(unsigned j=0;j<EC_GLOBALS;++j)if(after[j]!=actual[j])fprintf(stderr," global byte %u %02x/%02x\n",j,after[j],actual[j]);
        return ec_report("direct",n,pair,calls,expected);
    }
    return 0;
}
static int ec_patch(BYTE saved[EC_IDS][5],int active[EC_IDS],unsigned which,int on){
    for(unsigned i=0;i<EC_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(ec_addresses[i]-0x400000u,ec_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(ec_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static int ec_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;uint32_t head,head_after;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;ec_build(index,n,a,r);
        BYTE before[EC_GLOBALS],after[EC_GLOBALS],actual[EC_GLOBALS],expected_fpu[108],actual_fpu[108];ec_record expected[EC_MAX];
        memcpy(ec_before,ec_arena,EC_SIZE);ec_globals(before,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head));
        ec_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=ec_calls;memcpy(expected,ec_log,calls*sizeof(*expected));memcpy(ec_after,ec_arena,EC_SIZE);ec_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("effect poison install");
        memcpy(ec_arena,ec_before,EC_SIZE);ec_globals(before,1);ec_reset();sg_poison_call(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);ec_globals(actual,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(head_after!=head||pair[0]!=pair[1]||calls!=ec_calls||memcmp(expected,ec_log,calls*sizeof(*expected))||memcmp(ec_after,ec_arena,EC_SIZE)||
           memcmp(after,actual,EC_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=ec_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("effect poison uninstall");ec_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state\n",d->name,total);
    return status;
}
/* explosion_create over the real pool allocation nested as a hook in rotating
 * modes (pool_alloc is a pure leaf; its replacement reproduces the free/used
 * list exactly). 49630c, object_init, light_create, rng_bounded and the sound
 * wrapper stay stubs, so no heap allocation or SOS import is reached. */
static int ec_real_pass(unsigned index){
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered=0;
    /* Keep every stub except the pool allocation, which the hook owns. */
    BYTE saved[EC_IDS][5];int active[EC_IDS];
    if(ec_patch(saved,active,ec_which(index)&~(1u<<2),1))return 1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;ec_build(index,n,a,r);
        BYTE before[EC_GLOBALS],after[EC_GLOBALS],actual[EC_GLOBALS];uint32_t head,head_after;
        memcpy(ec_before,ec_arena,EC_SIZE);ec_globals(before,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head));
        es2_hook nest={0},caller={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        o.mode=(es2_hook_mode)(mode%3);
        const es2_hook_extension *pe=function_hook_extension(ES2_HOOK_INDEX_pool_alloc);
        if(pe->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
        if(!weapons_install_hook(&nest,function_hook_descriptor(ES2_HOOK_INDEX_pool_alloc),&o,pe))return fail("effect nested install");
        ec_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        memcpy(ec_after,ec_arena,EC_SIZE);ec_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(&caller,d,&o,function_hook_extension(index)))return fail("effect caller install");
        memcpy(ec_arena,ec_before,EC_SIZE);ec_globals(before,1);ec_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);ec_globals(actual,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
        if(head_after!=head||memcmp(expected_pair,actual_pair,8)||memcmp(ec_after,ec_arena,EC_SIZE)||memcmp(after,actual,EC_GLOBALS)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            for(unsigned j=0;j<EC_GLOBALS;++j)if(after[j]!=actual[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,after[j],actual[j]);break;}
            fprintf(stderr,"real mode=%u\n",mode);ec_calls=0;status=ec_report("real",n,pair,0,NULL);
        }
        entered+=nest.calls;if(nest.mismatches)status=fail("effect nested compare mismatch");
        if(!es2_hook_uninstall(&nest)||!es2_hook_uninstall(&caller))return fail("effect uninstall");
        ec_globals(before,1);++total;
    }
    if(ec_patch(saved,active,0,0))return 1;
    if(!status&&!entered){fprintf(stderr,"FAIL: %s real pool_alloc never entered\n",d->name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases, nested pool_alloc in rotating modes, 31/a7 stack, full pair and memory\n",d->name,total);
    return status;
}
static int effect_cases(unsigned index,unsigned limit){
    unsigned which=ec_which(index);
    BYTE saved[EC_IDS][5];int active[EC_IDS];
    if(ec_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_EFFECT_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];ec_build(index,n,a,r);status=ec_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=ec_installed_probes(index,64);
    if(ec_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status&&index==ES2_HOOK_INDEX_explosion_create)status=ec_real_pass(index);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, buffer payloads by content, arena/globals/FS:0\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
