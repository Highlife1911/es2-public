/* Own synthetic graph/callee contracts; original image is private and read-only on disk. */
typedef struct r2_record { uint32_t id,args[5],input[24],state[8],eax,edx; } r2_record;
static BYTE r2_arena[4096],r2_before[4096],r2_after[4096];
static uint32_t r2_table[32],r2_object,r2_owner,r2_seed,r2_stream;
__attribute__((used)) static uint32_t r2_ecx;
static unsigned r2_case,r2_hook,r2_calls;
static uint32_t r2_excluded;
static r2_record r2_log[40];
static const uint32_t r2_addresses[]={0x40adc0,0x40a234,0x467a48,0x467924,0x40aff0,
    0x47ebb8,0x4801d4,0x47e0b0,0x49288c,0x40501c,0x426b04,0x426528,0x426a20,
    0x40a488,0x40a254,0x45b734,0x47de72,0x462830,0x47f4f4,0x47fa20,0x40f540,0x47f9a0,0};
static const unsigned r2_counts[]={1,1,1,1,1,2,3,2,2,2,2,1,5,2,1,2,3,1,3,3,1,2,2};
static uint32_t r2_next(void) {r2_stream=r2_stream*1664525u+1013904223u;return r2_stream;}
static void r2_words(r2_record *v,unsigned at,uint32_t p,unsigned count) {
    for(unsigned i=0;i<count;++i)v->input[at+i]=get16((BYTE *)(uintptr_t)p+2*i);
}
static void r2_matrix(r2_record *v,unsigned at,uint32_t p,int translation) {
    r2_words(v,at,p,9);v->input[at+9]=*((BYTE *)(uintptr_t)p+18);
    if(translation)for(unsigned i=0;i<3;++i)v->input[at+10+i]=get32((BYTE *)(uintptr_t)p+20+4*i);
}
__attribute__((used,noinline)) static uint64_t __cdecl r2_stub_impl(unsigned id,const uint32_t *a) {
    if(r2_calls>=40){fail("round2 call overflow");ExitProcess(1);}
    r2_record *v=r2_log+r2_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,4*r2_counts[id]);
    BYTE *o=(BYTE *)(uintptr_t)r2_object;
    v->state[0]=get32(o+0x52);v->state[1]=get16(o+0x32);v->state[2]=get32(o+0x26);
    v->state[3]=get32(o+0x2a);v->state[4]=get32(o+0x2e);v->state[5]=get16(o+6);
    v->state[6]=get16(mapped+0xd2526);v->state[7]=get32(r2_arena+0xe00);
    put32(r2_arena+0xe00,v->state[7]+1);
    (void)es2_rng_next(r2_arena+0xe80+0x80*(id&1));
    uint32_t eax=r2_next(),edx=r2_next();r2_ecx=r2_next();
    uint32_t b=(uint32_t)(uintptr_t)r2_arena;
    if(id<2)eax=b+0x300;
    else if(id==2){eax=(eax&0xffff0000u)|((r2_case/3)%3?0:1);put16((BYTE *)(uintptr_t)a[0]+1,(uint16_t)eax);}
    else if(id==3) {
        static const uint16_t steps[]={0,1,0xffff,0x7fff,0x8000,64,450,512};
        eax=(eax&0xffff0000u)|(r2_case<1024?steps[(r2_case/3)%8]:(uint16_t)eax);
    } else if(id==4) {
        static const uint16_t angles[]={0,1,0xffff,0x7fff,0x8000,0x3fff,0x4000,0x4001,0xc000,0xbfff};
        eax=(eax&0xffff0000u)|angles[(r2_case/2)%10];
    } else if(id==5) {
        r2_words(v,0,a[0],3);v->args[0]=0;
        if(a[1]!=r2_object+0x12u && a[1]!=r2_owner+0x12u && a[1]!=b+0x512u)v->args[1]=0;
        BYTE *out=(BYTE *)(uintptr_t)a[1];
        for(unsigned i=0;i<18;++i)out[i]=(BYTE)(v->input[i%3]+i);
        out[18]=(BYTE)(r2_case%3);
    } else if(id==6) {
        r2_words(v,0,a[0],3);r2_matrix(v,3,a[1],1);v->args[0]=v->args[2]=0;
        for(unsigned i=0;i<3;++i)put32((BYTE *)(uintptr_t)a[2]+4*i,v->input[i]+v->input[13+i]+100u*i);
        if(r2_case%2)put16(o+0x32,0);
    } else if(id==8 || id==16) {
        static const uint32_t distances[]={0,1999,2000,2001,39999,40000,40001,0xffffffffu,0x80000000u};
        eax=distances[(r2_case/7)%9];
    } else if(id==10) {
        v->input[0]=get16((BYTE *)(uintptr_t)a[0]);v->args[0]=0;
        put16((BYTE *)(uintptr_t)a[0],(uint16_t)(v->input[0]+(uint16_t)a[1]));
    } else if(id==11) {
        BYTE *impact=(BYTE *)(uintptr_t)a[0],*segment=(BYTE *)(uintptr_t)get32(impact);
        v->args[0]=0;v->input[0]=get32(segment);v->input[1]=get32(segment+4);v->input[2]=get16(segment+8);
        for(unsigned i=0;i<3;++i)v->input[3+i]=get16(impact+4+2*i);
        v->input[6]=get32(impact+10);v->input[7]=get32(impact+14);v->input[8]=get16(impact+18);v->input[9]=get32(impact+20);r2_excluded=v->input[9];
        eax=r2_case%4?0:0xa55a0000u; /* full EAX, not only AX */
        memset(segment+10,0x6b,32);put32(segment+4,0x12345678); /* caller commits endpoint, not shortened segment */
    } else if(id==18 || id==19) {
        r2_matrix(v,0,a[0],id==19);r2_matrix(v,13,a[1],0);
        if(id==18)v->args[0]=v->args[2]=0;
        else if(a[0]!=(uint32_t)(uintptr_t)(mapped+0x2c56a8) && (a[0]<b || a[0]>=b+4096))v->args[0]=0;
        if(id==18 && (a[1]<b || a[1]>=b+4096) && a[1]!=(uint32_t)(uintptr_t)(mapped+0x2c56a8))v->args[1]=0;
        BYTE *out=(BYTE *)(uintptr_t)a[2];
        for(unsigned i=0;i<32;++i)out[i]=(BYTE)(v->input[i%23]+i);
        out[19]=0x6d;
    } else if(id==20)eax=b+0x780;
    else if(id==21) {
        r2_matrix(v,0,a[0],0);
        for(unsigned i=0;i<3;++i)put16((BYTE *)(uintptr_t)a[1]+2*i,(uint16_t)(v->input[i]+i));
    } else if(id==22) {
        eax=r2_case%5?b+0x700:0;
        if(r2_case%11==0)put32(o+0x18,b+0x380);
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define R2_STUB(n) __attribute__((naked)) static void r2_stub_##n(void) {__asm__ volatile( \
    "leal 4(%esp),%eax\n\tpushl %eax\n\tpushl $" #n "\n\tcalll _r2_stub_impl\n\taddl $8,%esp\n\tmovl _r2_ecx,%ecx\n\tret\n\t");}
R2_STUB(0) R2_STUB(1) R2_STUB(2) R2_STUB(3) R2_STUB(4) R2_STUB(5) R2_STUB(6) R2_STUB(7)
R2_STUB(8) R2_STUB(9) R2_STUB(10) R2_STUB(11) R2_STUB(12) R2_STUB(13) R2_STUB(14) R2_STUB(15)
R2_STUB(16) R2_STUB(17) R2_STUB(18) R2_STUB(19) R2_STUB(20) R2_STUB(21) R2_STUB(22)
#undef R2_STUB
static void *r2_stubs[]={r2_stub_0,r2_stub_1,r2_stub_2,r2_stub_3,r2_stub_4,r2_stub_5,r2_stub_6,r2_stub_7,
    r2_stub_8,r2_stub_9,r2_stub_10,r2_stub_11,r2_stub_12,r2_stub_13,r2_stub_14,r2_stub_15,r2_stub_16,
    r2_stub_17,r2_stub_18,r2_stub_19,r2_stub_20,r2_stub_21,r2_stub_22};
static const struct {unsigned rva,size;} r2_globals[]={
    {0xa9834,50},{0x9ef34,4},{0xd24e6,4},{0xd2684,4},{0xd2526,2},
    {0xd22d3,1},{0x9c394,4},{0x9c398,1},{0xd24ea,4}};
static void r2_global_copy(BYTE *out,int restore) {
    for(unsigned i=0;i<sizeof(r2_globals)/sizeof(r2_globals[0]);++i) {
        BYTE *p=mapped+r2_globals[i].rva;unsigned size=r2_globals[i].size;
        if(restore)memcpy(p,out,size);else memcpy(out,p,size);out+=size;
    }
}
static void r2_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]) {
    r2_hook=index;r2_case=n;r2_seed=random32();memset(r2_arena,0xa5,sizeof(r2_arena));
    uint32_t b=(uint32_t)(uintptr_t)r2_arena;r2_object=b+0x20;r2_owner=b+0x120;
    BYTE *o=r2_arena+0x20;
    for(unsigned i=0;i<3;++i)r[i]=random32();memset(a,0,24);a[0]=r2_object;a[1]=r2_owner;
    r2_arena[0xef0]=r2_arena[0xf70]=0;r2_arena[0xef1]=r2_arena[0xf71]=17;put32(r2_arena+0xe00,0);
    static const uint16_t edges[]={0,1,0xffff,0x7fff,0x8000,512,64,32766};
    put32(o+0x34,b+0x400);put32(o+0x4a,r2_owner);put32(o+0x4e,b+0x340);
    put32(o+0x5b,(n/3)%3?b+0x600:0);put32(o+0x5f,n%4?b+0x600:0);
    put16(o+0x52,n<1024?edges[n%8]:(uint16_t)random32());put16(o+0x54,n<1024?edges[(n/8)%8]:(uint16_t)random32());
    put16(o+0x56,n%3?(uint16_t)random32():0);put16(o+6,(uint16_t)((n/2)%2));
    put16(o+0x32,(uint16_t)(n%2));o[0x41]=(BYTE)(index==ES2_HOOK_INDEX_bullet_flight?(n%3?9:2):(n%3?3:2));
    r2_arena[0x1c3]=(BYTE)(n%2);put32(r2_arena+0x165,b+0x900);r2_arena[0x912]=(BYTE)n;
    put32(r2_arena+0x404,b+0x440);put32(r2_arena+0x408,b+0x480);put32(r2_arena+0x460,b+0x4c0);
    put32(r2_arena+0x440,(uint32_t)(uintptr_t)r2_table);r2_table[0x20/4]=(uint32_t)(uintptr_t)r2_stub_22;
    for(unsigned i=0;i<8;++i){put16(r2_arena+0x480+2*i,edges[(n+i)%8]);put16(r2_arena+0x4c0+2*i,(uint16_t)(i+2));}
    put16(r2_arena+0x302,n<1024?edges[(n/64)%8]:(uint16_t)random32());
    for(unsigned i=4;i<14;i+=2)put16(r2_arena+0x300+i,(uint16_t)random32());
    put16(r2_arena+0x306,(uint16_t)(n%3));put16(r2_arena+0x308,(uint16_t)(n%3));put16(r2_arena+0x30a,(uint16_t)(n%8));
    put16(r2_arena+0x34a,n<1024?edges[(n/16)%8]:(uint16_t)random32());
    put32(mapped+0x9c394,n%3?r2_object:0);mapped[0x9c398]=0;put16(mapped+0xd2526,0);mapped[0xd22d3]=1;
    put32(mapped+0xd24ea,b+0x800);put32(mapped+0x9ef34,n%2);put32(mapped+0xd24e6,n%3?r2_owner:0);put32(mapped+0xd2684,b+0x500);
    put32(o+0x18,b+0x380);put32(o+0x1c,b+0xa00);put32(r2_arena+0x154,b+0x400);put32(r2_arena+0x416,b+0x740);
    put16(r2_arena+0x704,n%3?0:0xffff);put16(r2_arena+0x532,(uint16_t)(n%2));put16(r2_arena+0x152,(uint16_t)(n%2));
    for(unsigned i=0;i<50;++i)mapped[0xa9834+i]=(BYTE)(n+i);
}

static uint16_t r2_indexed_state[65536],r2_indexed_counts[65536];
static int weapons_round2_boundaries(unsigned index) {
    if(index==ES2_HOOK_INDEX_weapon_prepare)return 0;
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    static const uint16_t indices[]={0,1,32767,32768,65535};
    for(unsigned n=0;n<5;++n) {
        uint32_t a[6],r[3];r2_build(index,7,a,r);put16(r2_arena+0x302,0x8000);put16((BYTE *)(uintptr_t)a[0]+0x54,0);
        unsigned slot=index==ES2_HOOK_INDEX_bullet_flight?0:indices[n];
        put16(r2_arena+0x30a,(uint16_t)slot);put32(r2_arena+0x408,(uint32_t)(uintptr_t)r2_indexed_state);put32(r2_arena+0x460,(uint32_t)(uintptr_t)r2_indexed_counts);
        r2_indexed_state[slot]=65535;r2_indexed_counts[slot]=n%2?65535:1;
        memcpy(r2_before,r2_arena,sizeof(r2_arena));r2_stream=r2_seed;r2_calls=0;uint64_t got=weapons_original(index,a,r);uint16_t expected=r2_indexed_state[slot];
        memcpy(r2_after,r2_arena,sizeof(r2_arena));memcpy(r2_arena,r2_before,sizeof(r2_arena));r2_indexed_state[slot]=65535;r2_stream=r2_seed;r2_calls=0;
        uint64_t want=x->replacement_context(a,d->user,r);
        if(got!=want||r2_indexed_state[slot]!=expected||memcmp(r2_after,r2_arena,sizeof(r2_arena)))return fail("flight unsigned animation index/remainder boundary");
    }
    uint32_t a[6],r[3];r2_build(index,7,a,r);unsigned slot=index==ES2_HOOK_INDEX_bullet_flight?0:7;put16(r2_arena+0x4c0+2*slot,0);
    r2_stream=r2_seed;r2_calls=0;expecting_divide=1;divide_site=0;
    if(!setjmp(divide_return)){(void)weapons_original(index,a,r);expecting_divide=0;return fail("flight zero divisor did not fault");}
    if(divide_site!=(index==ES2_HOOK_INDEX_bullet_flight?0x40b17du:0x40a5a5u))return fail("flight zero-divisor fault site");
    r2_build(index,7,a,r);put16(r2_arena+0x4c0+2*slot,0);put16(r2_arena+(index==ES2_HOOK_INDEX_bullet_flight?0x306:0x308),0);
    put16(r2_arena+0x302,0x8000);put16((BYTE *)(uintptr_t)a[0]+0x54,0);r2_calls=0;r2_stream=r2_seed;
    (void)weapons_original(index,a,r);
    printf("PASS: %s unsigned animation extrema, 65535+1 numerator, zero-divisor bypass and original fault %08x\n",d->name,(unsigned)divide_site);return 0;
}
static int weapons_round2_cases(unsigned index,unsigned limit) {
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE saved[22][5],old_globals[80],before_globals[80],after_globals[80],actual_globals[80];
    r2_global_copy(old_globals,0);
    for(unsigned i=0;i<22;++i)if(actor_patch(r2_addresses[i]-0x400000u,r2_stubs[i],saved[i],TRUE))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_WEAPONS2_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n) {
        current_case=n;current_case_valid=1;if(!limit&&n==8192)coverage_enable(0);
        uint32_t a[6],r[3];r2_build(index,n,a,r);memcpy(r2_before,r2_arena,sizeof(r2_arena));r2_global_copy(before_globals,0);
        r2_calls=0;r2_stream=r2_seed;uint64_t got=weapons_original(index,a,r);
        r2_record expected[40];unsigned calls=r2_calls;memcpy(expected,r2_log,calls*sizeof(*expected));
        memcpy(r2_after,r2_arena,sizeof(r2_arena));r2_global_copy(after_globals,0);
        memcpy(r2_arena,r2_before,sizeof(r2_arena));r2_global_copy(before_globals,1);r2_calls=0;r2_stream=r2_seed;
        unsigned counter_count;callee_counter *counters=weapons_graph_counters(index,&counter_count);uint64_t counter_before[16];
        for(unsigned i=0;i<counter_count;++i)counter_before[i]=counters[i].count;
        r[2]=r2_excluded;uint64_t want=x->replacement_context(a,d->user,r);r2_global_copy(actual_globals,0);
        if(n<1024)for(unsigned i=0;i<counter_count;++i){unsigned expected_count=0;for(unsigned j=0;j<r2_calls;++j){unsigned id=r2_log[j].id;if((id==22&&counters[i].event==ES2_COLLIDE_SLOT)||(id<22&&counters[i].event==ES2_COLLIDE_CALL&&counters[i].key==r2_addresses[id]))++expected_count;}
            if(counters[i].count-counter_before[i]!=expected_count)status=fail("round2 callee counter delta");}
        if(got!=want||calls!=r2_calls||memcmp(expected,r2_log,calls*sizeof(*expected))||memcmp(r2_arena,r2_after,sizeof(r2_arena))||memcmp(after_globals,actual_globals,74)) {
            unsigned diff=0;while(diff<calls&&diff<r2_calls&&!memcmp(expected+diff,r2_log+diff,sizeof(*expected)))++diff;
            fprintf(stderr,"FAIL: %s case %u pair %016llx/%016llx calls %u/%u first=%u\n",d->name,n,(unsigned long long)got,(unsigned long long)want,calls,r2_calls,diff);
            if(diff<calls&&diff<r2_calls) {
                uint32_t *e=(uint32_t *)(expected+diff),*f=(uint32_t *)(r2_log+diff);
                for(unsigned j=0;j<sizeof(*expected)/4;++j)if(e[j]!=f[j])fprintf(stderr," record word %u %08x/%08x id=%u\n",j,e[j],f[j],expected[diff].id);
            }
            for(unsigned j=0;j<sizeof(r2_arena);++j)if(r2_arena[j]!=r2_after[j]){fprintf(stderr," arena +%x %x/%x\n",j,r2_after[j],r2_arena[j]);break;}
            for(unsigned j=0;j<74;++j)if(after_globals[j]!=actual_globals[j]){fprintf(stderr," global +%x %x/%x\n",j,after_globals[j],actual_globals[j]);break;}
            status=1;
        }
    }
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=weapons_round2_boundaries(index);
    for(unsigned i=0;i<22;++i)if(actor_patch(r2_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    r2_global_copy(old_globals,1);current_case_valid=0;
    if(status)return 1;
    printf("PASS: %-20s %u cases; full pair/x87, ordered calls and full slots, graph state, guards and both RNG streams\n",d->name,cases);fflush(stdout);return 0;
}

/* Distinct residues deliberately occupy the exact pre-entry addresses. The
 * bridge is ours; it marshals arguments and poisons unused stack storage. */
__attribute__((naked)) static void r2_poison_call(void *target,uint32_t a0,uint32_t a1,uint32_t a2,uint32_t pattern,uint32_t result[2]) {
    __asm__ volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %edi\n\tsubl $512,%esp\n\t"
        "movl %esp,%edi\n\tmovl 24(%ebp),%eax\n\tmovl $128,%ecx\n\trep stosl\n\taddl $384,%esp\n\t"
        "pushl 20(%ebp)\n\tpushl 16(%ebp)\n\tpushl 12(%ebp)\n\t"
        "movb $0x31,-37(%esp)\n\tmovb $0xa7,-69(%esp)\n\t"
        "movl 24(%ebp),%eax\n\tmovl %eax,-108(%esp)\n\tmovl %eax,-96(%esp)\n\t"
        "call *8(%ebp)\n\tmovl 28(%ebp),%ecx\n\tmovl %eax,(%ecx)\n\tmovl %edx,4(%ecx)\n\t"
        "leal -4(%ebp),%esp\n\tpopl %edi\n\tpopl %ebp\n\tret\n\t");
}
__attribute__((naked)) static void r2_fire_poison_call(void *target,uint32_t a0,uint32_t a1,uint32_t a2,uint32_t pattern,uint32_t result[2]) {
    __asm__ volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %edi\n\tsubl $512,%esp\n\t"
        "movl %esp,%edi\n\tmovl 24(%ebp),%eax\n\tmovl $128,%ecx\n\trep stosl\n\taddl $384,%esp\n\t"
        "pushl 20(%ebp)\n\tpushl 16(%ebp)\n\tpushl 12(%ebp)\n\t"
        "movb %al,-69(%esp)\n\tmovb %ah,-101(%esp)\n\t"
        "call *8(%ebp)\n\tmovl 28(%ebp),%ecx\n\tmovl %eax,(%ecx)\n\tmovl %edx,4(%ecx)\n\t"
        "leal -4(%ebp),%esp\n\tpopl %edi\n\tpopl %ebp\n\tret\n\t");
}
static int weapons_entry_residue_probe(void) {
    BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);__asm__ volatile("fninit; fld1; fldpi");
    BYTE saved[22][5],old_globals[80];r2_global_copy(old_globals,0);
    for(unsigned i=0;i<22;++i)if(actor_patch(r2_addresses[i]-0x400000u,r2_stubs[i],saved[i],TRUE))return 1;
    for(unsigned index=ES2_HOOK_INDEX_bullet_flight;index<=ES2_HOOK_INDEX_projectile_flight;++index) {
        const es2_hook_descriptor *d=function_hook_descriptor(index);
        for(unsigned fast=0;fast<2;++fast)for(unsigned poison=0;poison<2;++poison) {
            uint32_t pattern=poison?0xa7a7a7a7u:0x31313131u;
            uint32_t a[6],r[3],expected_pair[2],actual_pair[2];r2_build(index,9,a,r);
            put16((BYTE *)(uintptr_t)a[0]+0x54,0);put16(r2_arena+0x302,32767);((BYTE *)(uintptr_t)a[0])[0x41]=2;
            BYTE before_globals[80],after_globals[80],actual_globals[80];memcpy(r2_before,r2_arena,sizeof(r2_arena));r2_global_copy(before_globals,0);
            r2_calls=0;r2_stream=r2_seed;r2_poison_call(mapped+d->rva,a[0],a[1],0,pattern,expected_pair);
            BYTE expected_fpu[108];es2_hook_fpu_save(expected_fpu);
            if(r2_excluded!=pattern)return fail("original flight residue offset");
            r2_record expected[40];unsigned calls=r2_calls;memcpy(expected,r2_log,calls*sizeof(*expected));memcpy(r2_after,r2_arena,sizeof(r2_arena));r2_global_copy(after_globals,0);
            es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
            es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("flight residue install");
            memcpy(r2_arena,r2_before,sizeof(r2_arena));r2_global_copy(before_globals,1);r2_calls=0;r2_stream=r2_seed;
            r2_poison_call(mapped+d->rva,a[0],a[1],0,pattern,actual_pair);r2_global_copy(actual_globals,0);
            BYTE actual_fpu[108];es2_hook_fpu_save(actual_fpu);
            int bad=memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||memcmp(expected_pair,actual_pair,8)||calls!=r2_calls||memcmp(expected,r2_log,calls*sizeof(*expected))||memcmp(r2_after,r2_arena,sizeof(r2_arena))||memcmp(after_globals,actual_globals,74)||h.calls!=1;
            if(!es2_hook_uninstall(&h))return fail("flight residue uninstall");if(bad)return fail("installed flight residue/full-state comparison");
        }
    }
    es2_hook_set_fast_path(TRUE);for(unsigned i=0;i<22;++i)if(actor_patch(r2_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    __asm__ volatile("frstor %0"::"m"(saved_fpu));r2_global_copy(old_globals,1);puts("PASS: both flight entry shims capture 31/a7 exclusion words; preserving/fast dispatch; exact calls, pair and state");return 0;
}

/* Real math/timing/radius entries in alternating independent modes. The
 * external guidance, impact/effects and resource graph remain synthetic. */
static int weapons_round2_modes(void) {
    static const unsigned nested[]={ES2_HOOK_INDEX_timer_word_step,ES2_HOOK_INDEX_movement_step,
        ES2_HOOK_INDEX_time_scale,ES2_HOOK_INDEX_euler_matrix,ES2_HOOK_INDEX_matrix_compose,
        ES2_HOOK_INDEX_matrix_angles,ES2_HOOK_INDEX_multiply_q10,ES2_HOOK_INDEX_dist_gate,
        ES2_HOOK_INDEX_dist3d_delta,ES2_HOOK_INDEX_radius_hit};
    BYTE saved[22][5],old_globals[80];int patched[22]={0};r2_global_copy(old_globals,0);
    uint32_t old_list=get32(mapped+0xa9af8);uint16_t old_count=get16(mapped+0xa9afe),old_scale=get16(mapped+0xd3b64);
    for(unsigned i=0;i<22;++i) {
        int real=i==2||i==3||i==5||i==6||i==7||i==8||i==12||i==16||i==18||i==19||i==21;
        if(!real){if(actor_patch(r2_addresses[i]-0x400000u,r2_stubs[i],saved[i],TRUE))return 1;patched[i]=1;}
    }
    int status=0;unsigned total=0;
    for(unsigned index=ES2_HOOK_INDEX_weapon_prepare;index<=ES2_HOOK_INDEX_projectile_flight&&!status;++index) {
        for(unsigned modes=0;modes<6&&!status;++modes)for(unsigned n=0;n<96&&!status;++n){
            uint32_t a[6],r[3],expected_pair[2],actual_pair[2];r2_build(index,n,a,r);
            put16(mapped+0xd3b64,(uint16_t)(64+n*4));put32(mapped+0xa9af8,0);put16(mapped+0xa9afe,0);
            /* Exercise real transforms from a well formed cache. */
            put16((BYTE *)(uintptr_t)a[0]+0x32,0);put16((BYTE *)(uintptr_t)a[1]+0x32,0);
            put16(r2_arena+0x532,0);r2_arena[0x752]=0;r2_arena[0x753]=0x6d;
            uint32_t pattern=n%2?0xa7a7a7a7u:0x31313131u;
            BYTE before_globals[80],after_globals[80],actual_globals[80];memcpy(r2_before,r2_arena,sizeof(r2_arena));r2_global_copy(before_globals,0);
            es2_hook h[11]={{0}};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
            for(unsigned j=0;j<10;++j){o.mode=(es2_hook_mode)((modes+j)%3);if(nested[j]==ES2_HOOK_INDEX_radius_hit&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
                if(!nested_install_hook(h+j,nested[j],&o,function_hook_extension(nested[j])))return fail("round2 nested mode install");}
            uint32_t scale_args[2]={16,n*17u};es2_weapon_registers scale_regs={0,0,0};weapons_machine_call((uint32_t)(uintptr_t)(mapped+0x67894),scale_args,2,&scale_regs);put16(mapped+0xd3b64,(uint16_t)scale_regs.eax);
            if(h[2].options.mode!=ES2_HOOK_ORIGINAL && !h[2].calls)return fail("mixed time_scale setup entry");
            r2_calls=0;r2_stream=r2_seed;es2_probe_call(mapped+function_hook_descriptor(index)->rva,a[0],a[1],0,pattern,expected_pair);
            unsigned calls=r2_calls;r2_record expected[40];memcpy(expected,r2_log,calls*sizeof(*expected));memcpy(r2_after,r2_arena,sizeof(r2_arena));r2_global_copy(after_globals,0);
            o.mode=(modes&1)?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(h+10,function_hook_descriptor(index),&o,function_hook_extension(index)))return fail("round2 caller install");
            memcpy(r2_arena,r2_before,sizeof(r2_arena));r2_global_copy(before_globals,1);r2_calls=0;r2_stream=r2_seed;
            es2_probe_call(mapped+function_hook_descriptor(index)->rva,a[0],a[1],0,pattern,actual_pair);r2_global_copy(actual_globals,0);
            if(memcmp(expected_pair,actual_pair,8)||calls!=r2_calls||memcmp(expected,r2_log,calls*sizeof(*expected))||memcmp(r2_after,r2_arena,sizeof(r2_arena))||memcmp(after_globals,actual_globals,74)) {
                fprintf(stderr,"FAIL: %s mixed mode=%u case=%u pair %08x:%08x/%08x:%08x calls %u/%u\n",function_hook_descriptor(index)->name,modes,n,expected_pair[1],expected_pair[0],actual_pair[1],actual_pair[0],calls,r2_calls);status=1;
                for(unsigned j=0;j<74;++j)if(after_globals[j]!=actual_globals[j])fprintf(stderr," global%u %02x/%02x\n",j,after_globals[j],actual_globals[j]);
                for(unsigned j=0;j<calls&&j<r2_calls;++j)if(memcmp(expected+j,r2_log+j,sizeof(*expected))){uint32_t *e=(uint32_t *)(expected+j),*f=(uint32_t *)(r2_log+j);for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %x/%x\n",j,expected[j].id,k,e[k],f[k]);break;}
            }
            for(unsigned j=0;j<11;++j){if(h[j].mismatches)status=fail("round2 nested compare mismatch");if(!es2_hook_uninstall(h+j))return fail("round2 nested uninstall");}
            ++total;
        }
    }
    for(unsigned i=0;i<22;++i)if(patched[i]&&actor_patch(r2_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    r2_global_copy(old_globals,1);put32(mapped+0xa9af8,old_list);put16(mapped+0xa9afe,old_count);put16(mapped+0xd3b64,old_scale);
    if(!status)printf("PASS: round2 %u real-callee mixed-mode cases; independent callers, timing, transforms, radius and distance modes\n",total);return status;
}

static uint32_t fire_prep_table[16];
static uint64_t __cdecl fire_prep_lookup(uint32_t receiver,uint32_t selector) {
    (void)receiver;(void)selector;return (uint32_t)(uintptr_t)(weapon_arena+0xac0);
}
static uint64_t __cdecl fire_prep_offset(uint32_t weapon) {
    (void)weapon;return (uint32_t)(uintptr_t)(weapon_arena+0xad0);
}
static int weapon_fire_prepare_modes(void) {
    BYTE saved[3][5],old_globals[80];r2_global_copy(old_globals,0);
    unsigned rvas[]={0xa9c4,0xb43c,0xf540};void *stubs[]={weapon_projectile_stub,weapon_bullet_stub,fire_prep_offset};
    for(unsigned i=0;i<3;++i)if(actor_patch(rvas[i],stubs[i],saved[i],TRUE))return 1;
    int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned modes=0;modes<4&&!status;++modes)for(unsigned n=0;n<128&&!status;++n) {
        uint32_t a[6],r[3];weapon_build(ES2_HOOK_INDEX_weapon_fire,n,a,r);uint32_t b=(uint32_t)(uintptr_t)weapon_arena;
        put32(weapon_arena+0x3c,b+0x200);put32(weapon_arena+0x154,b+0xa00);put32(weapon_arena+0xa04,b+0xa40);
        put32(weapon_arena+0xa40,(uint32_t)(uintptr_t)fire_prep_table);fire_prep_table[8]=(uint32_t)(uintptr_t)fire_prep_lookup;
        put16(weapon_arena+0xac4,0);memset(weapon_arena+0xad0,0,6);
        put32(weapon_arena+0xa16,b+0xb00);memset(weapon_arena+0xb00,0,32);
        put16(weapon_arena+0xb00,16384);put16(weapon_arena+0xb08,16384);put16(weapon_arena+0xb10,16384);weapon_arena[0xb12]=(BYTE)(n%3);
        put32(weapon_arena+0x7b,n%4<2?0:b+0xad0);put32(weapon_arena+0x7f,n%4%2?0:b+0xad0);put16(weapon_arena+0x152,0);memset(weapon_arena+0x12c,0,6);put32(mapped+0x9ef34,0);
        BYTE before_globals[80],after_globals[80],actual_globals[80];memcpy(weapon_before,weapon_arena,sizeof(weapon_arena));r2_global_copy(before_globals,0);
        weapon_reset();uint32_t expected_pair[2],actual_pair[2],pattern=n%5==0?0x5252a731u:n&1?0xa7a7a7a7u:0x31313131u;
        r2_fire_poison_call(mapped+0xe964,a[0],a[1],a[2],pattern,expected_pair);unsigned calls=weapon_logged;weapon_record expected[48];memcpy(expected,weapon_log,sizeof(expected));
        memcpy(weapon_after,weapon_arena,sizeof(weapon_arena));r2_global_copy(after_globals,0);
        if(n%4!=2 && mapped[0xa9847]!=(BYTE)(pattern>>(n%3?8:0)))return fail("fire local padding path was not observed");
        es2_hook_set_fast_path(fast);es2_hook h[2]={{0}};unsigned indices[]={ES2_HOOK_INDEX_weapon_prepare,ES2_HOOK_INDEX_weapon_fire};
        for(unsigned j=0;j<2;++j){es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=(es2_hook_mode)((modes>>j)&1);
            if(!nested_install_hook(h+j,indices[j],&o,function_hook_extension(indices[j])))return fail("fire prepare nested install");}
        memcpy(weapon_arena,weapon_before,sizeof(weapon_arena));r2_global_copy(before_globals,1);weapon_reset();
        r2_fire_poison_call(mapped+0xe964,a[0],a[1],a[2],pattern,actual_pair);r2_global_copy(actual_globals,0);
        if(memcmp(expected_pair,actual_pair,8)||calls!=weapon_logged||memcmp(expected,weapon_log,sizeof(expected))||memcmp(weapon_arena,weapon_after,sizeof(weapon_arena))||memcmp(after_globals,actual_globals,74))status=fail("weapon_fire real preparation mixed state");
        for(unsigned j=0;j<2;++j){
            if(((modes>>j)&1) && h[j].calls!=1)status=fail("fire/preparation replaced mode not entered");
            if(!es2_hook_uninstall(h+j))return fail("fire prepare nested uninstall");
        }++total;
    }
    es2_hook_set_fast_path(TRUE);for(unsigned i=0;i<3;++i)if(actor_patch(rvas[i],NULL,saved[i],FALSE))return 1;r2_global_copy(old_globals,1);
    if(!status)printf("PASS: weapon_fire + real weapon_prepare %u cases, all four independent original/replaced modes, local/resource matrices, uniform/distinct 31/a7 poison, preserving/fast dispatch\n",total);return status;
}

static int weapon_prepare_distinct_residue(void) {
    BYTE saved[5],old_globals[80],state[1024];r2_global_copy(old_globals,0);
    if(actor_patch(0xf540,weapon_prep_offset,saved,TRUE))return 1;
    uint32_t b=(uint32_t)(uintptr_t)weapon_prep_arena,w=b+0x20,owner=b+0x100;
    memset(weapon_prep_arena,0,1024);weapon_prep_table[8]=(uint32_t)(uintptr_t)weapon_prep_lookup;
    put32(weapon_prep_arena+0x38,b+0x180);put32(weapon_prep_arena+0x3c,b+0x200);
    put16(weapon_prep_arena+0x44,0x1000);put16(weapon_prep_arena+0x48,0x2000);
    put32(weapon_prep_arena+0x134,b+0x280);put32(weapon_prep_arena+0x284,b+0x2c0);put32(weapon_prep_arena+0x296,b+0x340);
    put32(weapon_prep_arena+0x2c0,(uint32_t)(uintptr_t)weapon_prep_table);
    put16(weapon_prep_arena+0x340,16384);put16(weapon_prep_arena+0x346,16384);put16(weapon_prep_arena+0x350,16384);
    put32(mapped+0x9ef34,0);memcpy(state,weapon_prep_arena,1024);
    int status=0;
    for(unsigned mode=0;mode<3&&!status;++mode)for(unsigned fast=0;fast<2&&!status;++fast) {
        uint32_t expected_pair[2],actual_pair[2];BYTE expected[50],expected_state[1024];
        memcpy(weapon_prep_arena,state,1024);weapon_prep_arena[0x352]=(BYTE)mode;memset(mapped+0xa9834,0,50);
        r2_poison_call(mapped+0xe788,w,owner,0,0x52525252,expected_pair);
        memcpy(expected,mapped+0xa9834,50);memcpy(expected_state,weapon_prep_arena,1024);
        if(expected[19]!=(mode?0xa7:0x31))return fail("distinct original matrix residue source");
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,function_hook_descriptor(ES2_HOOK_INDEX_weapon_prepare),&o,function_hook_extension(ES2_HOOK_INDEX_weapon_prepare)))return fail("distinct prepare install");
        memcpy(weapon_prep_arena,state,1024);weapon_prep_arena[0x352]=(BYTE)mode;memset(mapped+0xa9834,0,50);
        r2_poison_call(mapped+0xe788,w,owner,0,0x52525252,actual_pair);
        status=memcmp(expected_pair,actual_pair,8)||memcmp(expected,mapped+0xa9834,50)||memcmp(expected_state,weapon_prep_arena,1024);
        if(!es2_hook_uninstall(&h))return fail("distinct prepare uninstall");if(status)fail("distinct matrix residue output");
    }
    es2_hook_set_fast_path(TRUE);r2_global_copy(old_globals,1);if(actor_patch(0xf540,NULL,saved,FALSE))return 1;
    if(!status)puts("PASS: prepare distinct entry residues rotation=31/composition=a7, record modes 0/1/2, preserving/fast dispatch");return status;
}
