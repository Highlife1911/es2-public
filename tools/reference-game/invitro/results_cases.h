/* Results group: result_conditions 423d68 (docs/reverse-engineering/
 * mission_runtime.md, "team result records 423d68"). Direct cases run the
 * original body and the core against logging stubs for condition_record 41b4ec
 * (patched) and for the stream's write virtual +14h (two arena tables): ordered
 * calls with their arguments, entry EAX/EDX/ECX (stack addresses masked) and
 * the bytes each write receives. The condition_record stub fills the 84-byte
 * record with random condition words (Q8 0..100h, extremes, random), and on
 * later calls of a run sometimes writes only word +0, so the original's
 * in-place stores of the transformed words are read back. The stubs lower or
 * raise the team count (0..6), switch the member array and the stream's table
 * and change the 6 bytes at actor+2A4h; the original rereads them. EDX:EAX is
 * compared in full. No image global is read or written. Then installed 31/a7
 * poison probes and a real-callee pass with the real condition_record and
 * condition_table and both as nested hooks in rotating modes (the write stays a
 * stub). */
typedef struct rc_record { uint32_t id,args[3],regs[3],eax;BYTE data[8]; } rc_record;
enum { RC_SIZE=0x1100, RC_TEAM=0x000, RC_LIST_A=0x040, RC_LIST_B=0x080, RC_ACTORS=0x100, RC_ACTOR_SIZE=0x2c0,
    RC_STREAM=0xc00, RC_TABLE_A=0xc40, RC_TABLE_B=0xc80, RC_CONFIG=0xd00, RC_PARTS=0xd80, RC_MAXIMA=0xdc0,
    RC_GROUPS=0xe00, RC_PAIR=0xe40, RC_RECORDS=0xe80, RC_MAX=224, RC_IDS=2, RC_MEMBERS=6 };
#define RC_BASE ((uint32_t)(uintptr_t)rc_arena)
static BYTE rc_arena[RC_SIZE],rc_before[RC_SIZE],rc_after[RC_SIZE];
static rc_record rc_log[RC_MAX];
static unsigned rc_calls,rc_records;
static uint32_t rc_seed,rc_stream;
__attribute__((used)) static uint32_t rc_ecx;
static uint32_t rc_next(void){
    uint32_t z=rc_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static int rc_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static uint32_t rc_actor(unsigned k){return RC_BASE+RC_ACTORS+RC_ACTOR_SIZE*(k%4u);}
/* A Q8 condition word: mostly 0..100h, sometimes an extreme or random. */
static uint16_t rc_condition(uint32_t pick){
    switch(pick%8u){
    case 0:return (uint16_t)(pick>>8);
    case 1:{static const uint16_t edge[8]={0x8000,0x7fff,0xffff,0,0x100,0x101,0x0ff,0xff00};return edge[pick>>8&7];}
    default:return (uint16_t)((pick>>8)%0x101u);
    }
}
__attribute__((used,noinline)) static uint64_t __cdecl rc_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(rc_calls==RC_MAX){fail("results synthetic call overflow");ExitProcess(1);}
    rc_record *v=rc_log+rc_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->regs,regs,12);
    uint32_t pick=rc_next();BYTE *team=rc_arena+RC_TEAM;
    if(id==0){
        /* condition_record(actor, out). */
        const uint32_t out=a[1];v->args[0]=a[0];v->args[1]=out;
        const int full=rc_records++==0||pick%4u;
        put16((BYTE *)(uintptr_t)out,(uint16_t)rc_next());
        if(full)for(unsigned i=0;i<41;++i)put16((BYTE *)(uintptr_t)out+2u+2u*i,rc_condition(rc_next()));
    }
    else{
        /* stream write(stream, size, pointer): the bytes it receives. */
        memcpy(v->args,a,12);const uint32_t n=a[1]<=8u?a[1]:8u;memcpy(v->data,(const void *)(uintptr_t)a[2],n);
        if(pick%13u==0)put32(rc_arena+RC_STREAM,RC_BASE+(pick>>8&1?RC_TABLE_B:RC_TABLE_A));
        if(pick%23u==0)rc_arena[RC_ACTORS+RC_ACTOR_SIZE*(pick>>8&3)+0x2a4+(pick>>10)%6u]^=(BYTE)(pick>>16|1u);
    }
    if(pick%17u==0)put16(team+0x10,(uint16_t)((pick>>12)%(RC_MEMBERS+1u)));
    if(pick%19u==0)put32(team+0x0c,RC_BASE+(pick>>13&1?RC_LIST_B:RC_LIST_A));
    for(unsigned j=0;j<3;++j){if(rc_stack(v->regs[j]))v->regs[j]=0;if(rc_stack(v->args[j]))v->args[j]=0;}
    uint32_t eax=rc_next(),edx=rc_next();rc_ecx=rc_next();
    v->eax=eax;return (uint64_t)edx<<32|eax;
}
#define RC_STUB(n) __attribute__((naked)) static void rc_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _rc_stub_impl\n\taddl $24,%esp\n\tmovl _rc_ecx,%ecx\n\tret\n\t");}
RC_STUB(0) RC_STUB(1)
#undef RC_STUB
static void rc_reset(void){rc_calls=0;rc_records=0;rc_stream=rc_seed;}
/* Team, member arrays, actors, stream and its two tables. real: actors
 * with a configuration word and part tables condition_table can divide
 * (nonzero group bases and loop-2 divisors). */
static void rc_build(unsigned n,uint32_t a[6],uint32_t r[3],int real){
    uint32_t b=RC_BASE;
    rc_seed=random32();random_bytes(rc_arena,RC_SIZE);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    BYTE *team=rc_arena+RC_TEAM;
    put32(team+0x0c,b+(random32()%2?RC_LIST_B:RC_LIST_A));
    uint16_t count=(uint16_t)(n%16u==0?0:n%16u==1?0x8000u|random32():random32()%8u?1u+random32()%3u:1u+random32()%RC_MEMBERS);
    put16(team+0x10,count);
    for(unsigned i=0;i<RC_MEMBERS;++i){put32(rc_arena+RC_LIST_A+4*i,rc_actor(random32()));put32(rc_arena+RC_LIST_B+4*i,rc_actor(random32()));}
    put32(rc_arena+RC_STREAM,b+(random32()%2?RC_TABLE_B:RC_TABLE_A));
    put32(rc_arena+RC_TABLE_A+0x14,(uint32_t)(uintptr_t)rc_stub_1);put32(rc_arena+RC_TABLE_B+0x14,(uint32_t)(uintptr_t)rc_stub_1);
    a[0]=b+RC_TEAM;a[1]=b+RC_STREAM;for(unsigned i=2;i<6;++i)a[i]=random32();
    if(!real)return;
    for(unsigned k=0;k<4;++k){BYTE *actor=rc_arena+RC_ACTORS+RC_ACTOR_SIZE*k;
        put32(actor+0x1f2,b+RC_CONFIG);put32(actor+0x206,b+RC_PARTS);put32(actor+0x20a,b+RC_GROUPS);put32(actor+0x212,b+RC_PAIR);}
    put32(rc_arena+RC_PAIR,b+RC_RECORDS);put32(rc_arena+RC_PAIR+4,b+RC_MAXIMA);
    for(unsigned i=0;i<29;++i){uint16_t base=(uint16_t)(1u+random32()%0x200u);put16(rc_arena+RC_RECORDS+18*i,base);
        put16(rc_arena+RC_GROUPS+2*i,(uint16_t)(random32()%4?random32()%(base+1u):0xffffu-random32()%4u));}
    for(unsigned i=0;i<22;++i){uint16_t max=(uint16_t)(i<12&&random32()%8==0?0:1u+random32()%0x200u);put16(rc_arena+RC_MAXIMA+2*i,max);
        put16(rc_arena+RC_PARTS+2*i,(uint16_t)(random32()%4?random32()%(max+1u):0x8000u|random32()));}
}
static int rc_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const rc_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,rc_calls);
    for(unsigned j=0;j<calls&&j<rc_calls&&expected;++j)if(memcmp(expected+j,rc_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(rc_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<RC_SIZE;++j)if(rc_after[j]!=rc_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,rc_after[j],rc_arena[j]);break;}
    return 1;
}
static unsigned rc_expected_hits(const callee_counter *c){
    unsigned hits=0;for(unsigned j=0;j<rc_calls;++j)if(rc_log[j].id==(c->event==ES2_COLLIDE_SLOT?1u:0u))++hits;return hits;
}
static int rc_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    static rc_record expected[RC_MAX];
    memcpy(rc_before,rc_arena,RC_SIZE);
    rc_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=rc_calls;memcpy(expected,rc_log,calls*sizeof(*expected));
    memcpy(rc_after,rc_arena,RC_SIZE);memcpy(rc_arena,rc_before,RC_SIZE);rc_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];for(unsigned i=0;i<count&&i<8;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);
    if(n<4096)for(unsigned i=0;i<count&&i<8;++i)if(c[i].count-counts[i]!=rc_expected_hits(c+i))return fail("results callee counter delta");
    if(pair[0]!=pair[1]||calls!=rc_calls||memcmp(expected,rc_log,calls*sizeof(*expected))||memcmp(rc_after,rc_arena,RC_SIZE))
        return rc_report("direct",n,pair,calls,expected);
    return 0;
}
static int rc_patch(BYTE saved[5],int on){
    return actor_patch(0x1b4ecu,on?(void *)rc_stub_0:NULL,saved,on?TRUE:FALSE);
}
static int rc_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;rc_build(n,a,r,0);
        BYTE expected_fpu[108],actual_fpu[108];static rc_record expected[RC_MAX];
        memcpy(rc_before,rc_arena,RC_SIZE);
        rc_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=rc_calls;memcpy(expected,rc_log,calls*sizeof(*expected));memcpy(rc_after,rc_arena,RC_SIZE);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("results poison install");
        memcpy(rc_arena,rc_before,RC_SIZE);rc_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);es2_hook_fpu_save(actual_fpu);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=rc_calls||memcmp(expected,rc_log,calls*sizeof(*expected))||memcmp(rc_after,rc_arena,RC_SIZE)||
           memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=rc_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("results poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, EDX:EAX/trace/arena\n",d->name,total);
    return status;
}
/* Real condition_record 41b4ec and condition_table 4151a4, both as nested
 * hooks in rotating original/replaced/compare modes (compare falls back to
 * replaced where refused), the hook itself original or replaced; the write
 * stays a stub. EDX:EAX, the write log and the arena against a run with
 * only the nested hooks installed. */
static int rc_real_pass(unsigned index){
    const unsigned nested[2]={ES2_HOOK_INDEX_condition_record,ES2_HOOK_INDEX_condition_table},count=2;
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[2]={0};
    static rc_record expected[RC_MAX];
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;rc_build(n,a,r,1);
        memcpy(rc_before,rc_arena,RC_SIZE);
        es2_hook hk[3];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("results nested install");}
        rc_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        unsigned calls=rc_calls;memcpy(expected,rc_log,calls*sizeof(*expected));memcpy(rc_after,rc_arena,RC_SIZE);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+2,d,&o,function_hook_extension(index)))return fail("results caller install");
        memcpy(rc_arena,rc_before,RC_SIZE);rc_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);
        if(expected_pair[0]!=actual_pair[0]||expected_pair[1]!=actual_pair[1]||calls!=rc_calls||
           memcmp(expected,rc_log,calls*sizeof(*expected))||memcmp(rc_after,rc_arena,RC_SIZE)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            fprintf(stderr,"real mode=%u\n",mode);status=rc_report("real",n,pair,calls,expected);
        }
        for(unsigned j=0;j<3;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("results nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("results nested uninstall");}
        ++total;
    }
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, 31/a7 stack, EDX:EAX, write log and arena\n",d->name,total,count);
    return status;
}
static int results_cases(unsigned index,unsigned limit){
    BYTE saved[5];
    if(rc_patch(saved,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_RESULTS_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];rc_build(n,a,r,0);status=rc_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=rc_installed_probes(index,64);
    if(rc_patch(saved,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=rc_real_pass(index);
    if(!status)printf("PASS: %-20s %u cases; EDX:EAX, ordered calls with entry registers and written bytes, arena\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
