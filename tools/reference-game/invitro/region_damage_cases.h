/* Region-damage helpers: region_absorb 40d3ec, region_spread 40cf44,
 * region_entry 40ca58 and region_find 40ca8c
 * (docs/reverse-engineering/hit_tests.md, "region-damage helpers"). Direct
 * cases run the original body through an own cdecl caller that hands over and
 * returns EAX/EDX/ECX, then the adapter, then the core directly (for ECX), on
 * an 8 KiB arena. Game callees are logging stubs: 40ca58 under region_find,
 * rng_bounded 492f24 and 40d3ec under region_spread (ordered calls with their
 * arguments and entry EAX/EDX/ECX, stack addresses masked; for 40d3ec also the
 * region word it is handed and the damage word behind its stack pointer). The
 * stubs change what the originals reread (region_find's count word;
 * region_spread's count, entry pointer, entries and region words, and the
 * damage word through the pointer) and return planned results (40d3ec: a
 * nonzero low word a bounded number of times, also high-word-only results). The
 * leaves run on random words inside the arena (aliased region/damage pointers,
 * 16-bit sign and count edges). Then installed 31/a7 poison probes, and real
 * callees for region_find (40ca58) and region_spread (rng_bounded, rng_next,
 * 40d3ec) with the nested hooks in rotating modes on game-like tables.
 * Identifiers: rgd_/RGD_. */
typedef struct rgd_record { uint32_t id,args[3],regs[3],words[2],count; } rgd_record;
enum { RGD_SIZE=0x2000, RGD_OUT=0x10, RGD_LIST=0x20, RGD_ABSORB=0x40, RGD_NREC=0x80, RGD_RECORDS=0x100, RGD_ENTRIES=0x200,
    RGD_ENTRY_COUNT=56, RGD_PART=0x600, RGD_WEIGHTS=0x640, RGD_WEIGHT_COUNT=40, RGD_REGIONS=0x900, RGD_LIMITS=0xc00,
    RGD_MAX=48, RGD_IDS=3, RGD_GLOBALS=0x72 };
#define RGD_BASE ((uint32_t)(uintptr_t)rgd_arena)
static BYTE rgd_arena[RGD_SIZE],rgd_before[RGD_SIZE],rgd_after[RGD_SIZE];
static rgd_record rgd_log[RGD_MAX];
static unsigned rgd_calls,rgd_budget;
static int rgd_real;
static uint32_t rgd_seed,rgd_stream,rgd_list,rgd_part;
__attribute__((used)) static uint32_t rgd_ecx;
/* 0 region_entry 40ca58, 1 rng_bounded 492f24, 2 region_absorb 40d3ec. */
static const uint32_t rgd_addresses[RGD_IDS]={0x40ca58,0x492f24,0x40d3ec};
static const unsigned rgd_counts[RGD_IDS]={3,2,3};
static uint32_t rgd_next(void){
    uint32_t z=rgd_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *rgd_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int rgd_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static int rgd_in_arena(uint32_t a){return a-RGD_BASE<RGD_SIZE-1u;}
static uint16_t rgd_pick(const uint16_t *values,unsigned count,uint32_t pick){return values[pick%count];}
/* A region index: mostly 0..15 (shared regions), else -64..191; all stay
 * inside the region and limit windows. */
static uint16_t rgd_index(int real){return (uint16_t)(real||random32()%4?random32()%16u:random32()%256u-64u);}
/* The rereads of region_spread between its calls (direct passes only). */
static void rgd_touch_part(uint32_t pick){
    if(rgd_real||!rgd_part)return;
    static const uint16_t counts[]={0,1,2,3,5,8,0xffff,0x8000};
    if(pick%5==0)put16(rgd_host(rgd_part+6),rgd_pick(counts,8,pick>>8));
    if(pick%7==0)put32(rgd_host(rgd_part+8),RGD_BASE+RGD_WEIGHTS+4u*(pick>>12&3u));
    if(pick%11==0)put16(rgd_arena+RGD_REGIONS+2u*(pick>>16&15u),(uint16_t)(pick>>20&1?0xffff:pick>>21&0x3f));
    if(pick%13==0)put16(rgd_arena+RGD_WEIGHTS+4u*(pick>>14&15u),(uint16_t)(pick>>24&7u));
}
__attribute__((used,noinline)) static uint64_t __cdecl rgd_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(rgd_calls==RGD_MAX){fail("region_damage synthetic call overflow");ExitProcess(1);}
    rgd_record *v=rgd_log+rgd_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,4*rgd_counts[id]);memcpy(v->regs,regs,12);
    v->count=weapons_bridge_slots(a,rgd_counts[id]);
    uint32_t eax=rgd_next(),edx=rgd_next(),pick=rgd_next();rgd_ecx=rgd_next();
    switch(id){
    case 0: /* 40ca58: mostly not found; found as a pointer or a high-word-only value. */
        eax=pick%4?0:pick>>2&1?0x10000u:eax;
        if(pick%7==0&&rgd_list)put16(rgd_host(rgd_list),(uint16_t)(pick>>8&1?0xffff:pick>>9&15u));
        break;
    case 1: /* rng_bounded: small draws that walk the weights, or anything. */
        if(pick%3)eax=pick>>4&0x3fu;
        rgd_touch_part(pick);break;
    case 2: /* 40d3ec: the region word and the damage word by content. */
        if(rgd_in_arena(a[0]))v->words[0]=get16(rgd_host(a[0]));
        v->words[1]=get16(rgd_host(a[2]));
        if(rgd_budget&&pick%3){--rgd_budget;eax=pick>>2&1?1u:eax|1u;}else eax=pick>>3&1?0:eax&0xffff0000u;
        if(pick%5==0)put16(rgd_host(a[2]),(uint16_t)(pick>>16));
        if(pick%7==0&&rgd_in_arena(a[0]))put16(rgd_host(a[0]),0xffff);
        rgd_touch_part(pick>>1);break;
    }
    for(unsigned j=0;j<3;++j)if(rgd_stack(v->regs[j]))v->regs[j]=0;
    for(unsigned j=0;j<rgd_counts[id];++j)if(rgd_stack(v->args[j]))v->args[j]=0;
    return (uint64_t)edx<<32|eax;
}
/* Pushes the entry EAX/EDX/ECX for the record before the slot pointer. */
#define RGD_STUB(n) __attribute__((naked)) static void rgd_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _rgd_stub_impl\n\taddl $24,%esp\n\tmovl _rgd_ecx,%ecx\n\tret\n\t");}
RGD_STUB(0) RGD_STUB(1) RGD_STUB(2)
#undef RGD_STUB
static void *rgd_stubs[RGD_IDS]={rgd_stub_0,rgd_stub_1,rgd_stub_2};
/* A four-slot cdecl caller that hands over EAX/EDX/ECX and returns all
 * three (the hook ABI returns EDX:EAX only). */
__attribute__((naked,noinline)) static void rgd_machine(void *target,const uint32_t *a,const uint32_t *registers,uint32_t *out){
    __asm__ volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl 12(%ebp),%esi\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* Direct callees of each hook (bit i = rgd_addresses[i]). */
static unsigned rgd_which(unsigned index){
    if(index==ES2_HOOK_INDEX_region_find)return 1u;
    if(index==ES2_HOOK_INDEX_region_spread)return 2u|4u;
    return 0;
}
static int rgd_patch(BYTE saved[RGD_IDS][5],int active[RGD_IDS],unsigned which,int on){
    for(unsigned i=0;i<RGD_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(rgd_addresses[i]-0x400000u,rgd_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(rgd_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
/* RNG stream A (changed only by the real pass). */
static void rgd_globals(BYTE *out,int restore){if(restore)memcpy(mapped+0xd2599,out,RGD_GLOBALS);else memcpy(out,mapped+0xd2599,RGD_GLOBALS);}
static void rgd_reset(void){rgd_calls=0;rgd_stream=rgd_seed;}
static uint32_t rgd_budget0;
static uint16_t rgd_word(uint32_t pick){
    static const uint16_t words[]={0,1,2,0x7fff,0x8000,0x8001,0xffff,0xfffe,50,100,200,0x4000};
    return pick%3?words[pick/3%12]:(uint16_t)(pick>>8);
}
/* region_absorb: v and d anywhere in a 64-byte window (any alignment,
 * aliased one time in eight); the first 1,728 cases walk the word edges. */
static void rgd_build_absorb(unsigned n,uint32_t a[6],uint32_t r[3]){
    const uint32_t w=RGD_BASE+RGD_ABSORB,v=w+random32()%62u,d=random32()%8==0?v:w+random32()%62u;
    static const uint16_t edges[]={0,1,0x7fff,0x8000,0xffff,100,200,0xff9c,0x4000,0xc000,0x3fff,0x8001};
    uint16_t region=n<1728?edges[n%12]:rgd_word(random32()),damage=n<1728?edges[n/12%12]:rgd_word(random32()),
        limit=n<1728?edges[n/144%12]:rgd_word(random32());
    put16(rgd_host(d),damage);put16(rgd_host(v),region);
    a[0]=v;a[1]=(random32()%2?random32()<<16:0)|limit;a[2]=d;(void)r;
}
/* region_entry: record N with up to 48 entries of 16 bytes (keys 0..7,
 * FFFFh), the record's count word possibly negative. */
static void rgd_build_entry(unsigned n,uint32_t a[6],int real){
    static const uint16_t counts[]={0,1,2,3,5,8,0xffff,0x8000,16,48};
    for(unsigned i=0;i<RGD_ENTRY_COUNT;++i)put16(rgd_arena+RGD_ENTRIES+16u*i+6,(uint16_t)(random32()%9==0?0xffff:random32()%8));
    const uint32_t nrec=RGD_BASE+RGD_NREC+random32()%4u;
    put16(rgd_host(nrec+4),real?(uint16_t)(random32()%7):n<1024?counts[n%10]:rgd_pick(counts,10,random32()));
    put32(rgd_host(nrec+6),RGD_BASE+RGD_ENTRIES+16u*(random32()%8)+(real?0:random32()%4u));
    a[0]=nrec;a[1]=(random32()%2?random32()<<16:0)|(random32()%9==0?0xffffu:random32()%8u);a[2]=RGD_BASE+RGD_OUT+random32()%4u;
}
/* region_find: list S {count, records}; records of 10 bytes. Real cases
 * give each record a valid entry list (count 0..6). */
static void rgd_build_find(unsigned n,uint32_t a[6],int real){
    static const uint16_t counts[]={0,1,2,3,5,8,16,0xffff,0x8000,4};
    const uint32_t s=RGD_BASE+RGD_LIST+random32()%4u,recs=RGD_BASE+RGD_RECORDS+random32()%4u;
    put16(rgd_host(s),real?(uint16_t)(random32()%7):n<1024?counts[n%10]:rgd_pick(counts,10,random32()));put32(rgd_host(s+2),recs);
    if(real)for(unsigned i=0;i<6;++i){
        BYTE *p=rgd_host(recs+10u*i);
        put16(p+4,(uint16_t)(random32()%7));put32(p+6,RGD_BASE+RGD_ENTRIES+16u*(random32()%48));
        for(unsigned j=0;j<RGD_ENTRY_COUNT;++j)put16(rgd_arena+RGD_ENTRIES+16u*j+6,(uint16_t)(random32()%12));
    }
    a[0]=s;a[1]=(random32()%2?random32()<<16:0)|(real?random32()%12u:random32()%9==0?0xffffu:random32()%8u);
    a[2]=RGD_BASE+RGD_OUT+random32()%4u;rgd_list=real?0:s;
}
/* region_spread: record R (count, entries), 40 weight entries (weights
 * with sign and wrap edges in direct cases, 0..20 in real ones), region
 * words (a third negative) and limit words. */
static void rgd_build_spread(unsigned n,uint32_t a[6],int real){
    static const uint16_t counts[]={0,1,2,3,4,6,8,12,0xffff,0x8000};
    static const uint16_t weights[]={0,1,2,3,5,7,0xffff,0xfffe,0x8000,0x7fff,0x4000,0x3fff};
    const uint32_t part=RGD_BASE+RGD_PART+random32()%4u;
    for(unsigned i=0;i<RGD_WEIGHT_COUNT+1;++i){
        BYTE *p=rgd_arena+RGD_WEIGHTS+4u*i;
        put16(p,real?(uint16_t)(random32()%21):random32()%3?(uint16_t)(random32()%12):rgd_pick(weights,12,random32()));
        put16(p+2,rgd_index(real));
    }
    for(int i=-64;i<192;++i){
        BYTE *p=rgd_arena+RGD_REGIONS+2*i;
        put16(p,random32()%3==0?(uint16_t)(random32()%2?0xffff:0x8000u|random32()):(uint16_t)(random32()%(real?1000u:0x8000u)));
        p=rgd_arena+RGD_LIMITS+2*i;
        put16(p,real?(uint16_t)(1+random32()%2000):rgd_word(random32()));
    }
    put16(rgd_host(part+6),real?(uint16_t)(1+random32()%8):n<1024?counts[n%10]:rgd_pick(counts,10,random32()));
    put32(rgd_host(part+8),RGD_BASE+RGD_WEIGHTS+4u*(random32()%4));
    a[0]=part;a[1]=RGD_BASE+RGD_REGIONS;a[3]=RGD_BASE+RGD_LIMITS;
    a[2]=real?random32()%3000u|(random32()%2?random32()<<16:0):random32()%2?random32():(random32()<<16)|rgd_word(random32());
    rgd_part=real?0:part;rgd_budget0=n<64?n%4:random32()%7;
    if(real){BYTE *state=mapped+0xd2599;random_bytes(state,112);state[112]=(BYTE)(random32()%56);state[113]=(BYTE)(random32()%56);}
}
static void rgd_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    rgd_seed=random32();random_bytes(rgd_arena,RGD_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    rgd_list=rgd_part=0;rgd_budget0=0;
    if(index==ES2_HOOK_INDEX_region_absorb)rgd_build_absorb(n,a,r);
    else if(index==ES2_HOOK_INDEX_region_entry)rgd_build_entry(n,a,real);
    else if(index==ES2_HOOK_INDEX_region_find)rgd_build_find(n,a,real);
    else rgd_build_spread(n,a,real);
}
static int rgd_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const rgd_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,rgd_calls);
    for(unsigned j=0;j<calls&&j<rgd_calls&&expected;++j)if(memcmp(expected+j,rgd_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(rgd_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<RGD_SIZE;++j)if(rgd_after[j]!=rgd_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,rgd_after[j],rgd_arena[j]);break;}
    return 1;
}
static unsigned rgd_expected_hits(const callee_counter *c){
    unsigned hits=0;for(unsigned j=0;j<rgd_calls;++j)if(rgd_addresses[rgd_log[j].id]==c->key)++hits;return hits;
}
/* The core called directly with its ECX out pointer (the adapter drops
 * ECX); graph calls go through the same bridge, unobserved. */
static uint64_t rgd_core(unsigned index,const uint32_t *a,const uint32_t *r,uint32_t *ecx){
    if(index==ES2_HOOK_INDEX_region_absorb)return es2_region_absorb_registers(&math_native_memory,a[0],a[1],a[2],r[2],ecx);
    if(index==ES2_HOOK_INDEX_region_entry)return es2_region_entry_registers(&math_native_memory,a[0],a[1],a[2],ecx);
    BYTE scratch[ES2_REGION_SPREAD_SCRATCH]={0};es2_weapon_graph g=weapons_graph(mapped,scratch,index);g.observe=NULL;
    if(index==ES2_HOOK_INDEX_region_find)return es2_region_find_registers(&math_native_memory,&g,a[0],a[1],a[2],r,ecx);
    return es2_region_spread_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r,ecx);
}
static int rgd_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    static rgd_record expected[RGD_MAX];uint32_t out[3],core_ecx=0;
    memcpy(rgd_before,rgd_arena,RGD_SIZE);
    rgd_reset();rgd_budget=rgd_budget0;rgd_machine(mapped+d->rva,a,r,out);unsigned calls=rgd_calls;memcpy(expected,rgd_log,calls*sizeof(*expected));
    memcpy(rgd_after,rgd_arena,RGD_SIZE);
    uint64_t pair[2];pair[0]=(uint64_t)out[1]<<32|out[0];
    memcpy(rgd_arena,rgd_before,RGD_SIZE);rgd_reset();rgd_budget=rgd_budget0;
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[2];for(unsigned i=0;i<count&&i<2;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);
    if(n<4096)for(unsigned i=0;i<count&&i<2;++i)if(c[i].count-counts[i]!=rgd_expected_hits(c+i))return fail("region_damage callee counter delta");
    if(pair[0]!=pair[1]||calls!=rgd_calls||memcmp(expected,rgd_log,calls*sizeof(*expected))||memcmp(rgd_after,rgd_arena,RGD_SIZE))
        return rgd_report("direct",n,pair,calls,expected);
    memcpy(rgd_arena,rgd_before,RGD_SIZE);rgd_reset();rgd_budget=rgd_budget0;
    pair[1]=rgd_core(index,a,r,&core_ecx);
    /* With the hook installed (integration pass) the "original" run is the
       hook, whose ECX is the dispatcher's; ECX is compared otherwise. */
    const int ecx_known=!rng_hooks[index].installed;
    if(pair[0]!=pair[1]||(ecx_known&&core_ecx!=out[2])||calls!=rgd_calls||memcmp(expected,rgd_log,calls*sizeof(*expected))||
       memcmp(rgd_after,rgd_arena,RGD_SIZE)){
        fprintf(stderr," ECX %08x/%08x\n",out[2],core_ecx);return rgd_report("core ECX",n,pair,calls,expected);
    }
    return 0;
}
static int rgd_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;rgd_build(index,n,a,r,0);
        BYTE expected_fpu[108],actual_fpu[108];static rgd_record expected[RGD_MAX];
        memcpy(rgd_before,rgd_arena,RGD_SIZE);
        rgd_reset();rgd_budget=rgd_budget0;sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=rgd_calls;memcpy(expected,rgd_log,calls*sizeof(*expected));memcpy(rgd_after,rgd_arena,RGD_SIZE);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("region_damage poison install");
        memcpy(rgd_arena,rgd_before,RGD_SIZE);rgd_reset();rgd_budget=rgd_budget0;
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);es2_hook_fpu_save(actual_fpu);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=rgd_calls||memcmp(expected,rgd_log,calls*sizeof(*expected))||memcmp(rgd_after,rgd_arena,RGD_SIZE)||
           memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=rgd_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("region_damage poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/arena\n",d->name,total);
    return status;
}
/* Real callees with the nested hooks in rotating original/replaced/compare
 * modes (compare falls back to replaced where refused), against a run with
 * only the nested hooks: pair, arena and RNG stream A. */
static int rgd_real_pass(unsigned index){
    unsigned nested[3],count=0;
    if(index==ES2_HOOK_INDEX_region_find)nested[count++]=ES2_HOOK_INDEX_region_entry;
    else{nested[count++]=ES2_HOOK_INDEX_region_absorb;nested[count++]=ES2_HOOK_INDEX_rng_bounded;nested[count++]=ES2_HOOK_INDEX_rng_next;}
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[3]={0};
    rgd_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;rgd_build(index,n,a,r,1);
        BYTE before[RGD_GLOBALS],after[RGD_GLOBALS],actual[RGD_GLOBALS];
        memcpy(rgd_before,rgd_arena,RGD_SIZE);rgd_globals(before,0);
        es2_hook hk[4];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("region_damage nested install");}
        rgd_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        memcpy(rgd_after,rgd_arena,RGD_SIZE);rgd_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+3,d,&o,function_hook_extension(index)))return fail("region_damage caller install");
        memcpy(rgd_arena,rgd_before,RGD_SIZE);rgd_globals(before,1);rgd_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);rgd_globals(actual,0);
        if(expected_pair[0]!=actual_pair[0]||expected_pair[1]!=actual_pair[1]||memcmp(rgd_after,rgd_arena,RGD_SIZE)||memcmp(after,actual,RGD_GLOBALS)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            fprintf(stderr,"real mode=%u rng %d\n",mode,memcmp(after,actual,RGD_GLOBALS)!=0);rgd_calls=0;status=rgd_report("real",n,pair,0,NULL);
        }
        for(unsigned j=0;j<4;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("region_damage nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("region_damage nested uninstall");}
        rgd_globals(before,1);++total;
    }
    rgd_real=0;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, 31/a7 stack, full pair, arena and RNG stream A\n",d->name,total,count);
    return status;
}
/* Compare cases (compare_probes.h) for region_entry, the group's
 * compare-capable member; the state is the arena and the globals. */
static void rgd_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){rgd_build(index,n,a,r,0);}
static void rgd_compare_state(BYTE *s,int restore){
    if(restore){memcpy(rgd_arena,s,RGD_SIZE);rgd_globals(s+RGD_SIZE,1);}else{memcpy(s,rgd_arena,RGD_SIZE);rgd_globals(s+RGD_SIZE,0);}
}
static int region_damage_cases(unsigned index,unsigned limit){
    if(limit&&compare_probe_installed(index))return compare_probe_run(index,limit,rgd_compare_build,rgd_compare_state,RGD_SIZE+RGD_GLOBALS);
    const unsigned which=rgd_which(index);BYTE saved[RGD_IDS][5],old_globals[RGD_GLOBALS];int active[RGD_IDS];
    rgd_globals(old_globals,0);
    if(rgd_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_REGION_DAMAGE_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];rgd_build(index,n,a,r,0);status=rgd_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=rgd_installed_probes(index,64);
    if(rgd_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status&&which)status=rgd_real_pass(index);
    rgd_globals(old_globals,1);
    if(!limit&&!status&&compare_probe_capable(index))status=compare_probe_run(index,0,rgd_compare_build,rgd_compare_state,RGD_SIZE+RGD_GLOBALS);
    rgd_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ECX of the core, ordered calls with entry registers, arena\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
