/* Synthetic lifecycle graph for 417de4; every external entry is controlled. */
static unsigned vp_phase;
static uint32_t vp_table[32],vp_equipment_table[32],vp_source_table[32];
static const uint32_t vp_addresses[]={0x4240f4,0x40dbc0,0x40da38,0x420a68,0x417bec,0x478628,0x411bc8,0x423430,0x413e50,0x40d9f8,0x409904};
static const unsigned vp_counts[]={0,2,3,1,1,2,1,1,2,2,0,4,3,4};
static const uint16_t vp_edges[]={0,1,80,81,128,129,140,141,192,193,255,256,257,0xffff};
__attribute__((used,noinline)) static uint64_t __cdecl vp_stub_impl(unsigned id,const uint32_t *a) {
    if(r2_calls>=40){fail("vehicle call overflow");ExitProcess(1);}
    r2_record *v=r2_log+r2_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,4*vp_counts[id]);
    v->state[0]=get32(r2_arena+0x96);v->state[1]=get32(r2_arena+0xa3);v->state[2]=get32(r2_arena+0xa8);
    v->state[3]=get32(r2_arena+0x1a4);v->state[4]=get32(r2_arena+0x2e);v->state[5]=get16(r2_arena+0x23c);
    v->state[6]=get32(mapped+0xa964c);v->state[7]=get16(r2_arena+0x7a2);
    for(unsigned i=0;i<4;++i)v->input[i]=get32(r2_arena+0xd80+4*i);
    (void)es2_rng_next(r2_arena+0xe80+0x80*(id&1));
    uint32_t eax=r2_next(),edx=r2_next();r2_ecx=r2_next();
    if(id==0)eax=(eax&0xffff0000u)|(r2_case%37==0);
    if(id==1)eax=(eax&0xffff0000u)|vp_edges[(r2_case+vp_phase*3+(uint16_t)a[1])%14];
    if(id==2){vp_phase=1;put16(r2_arena+0xcc8,vp_edges[(r2_case/5)%14]);
        if(r2_case%7==0)put32(r2_arena+0x202,(uint32_t)(uintptr_t)(r2_arena+0xe20));}
    if(id==5 && r2_case%3==0)put32(r2_arena+0x238,(uint32_t)(uintptr_t)(r2_arena+0xe00));
    if(id==6 && r2_case%5==0)put32(r2_arena+0x1b6,(uint32_t)(uintptr_t)(r2_arena+0xda4));
    if(id==13 && r2_case%7==0)r2_arena[0xa4]^=0x81;
    if(id==9)eax=(eax&0xffff0000u)|((r2_case/7)%4==a[1]);
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define VP_STUB(n) __attribute__((naked)) static void vp_stub_##n(void) {__asm__ volatile( \
    "leal 4(%esp),%eax\n\tpushl %eax\n\tpushl $" #n "\n\tcalll _vp_stub_impl\n\taddl $8,%esp\n\tmovl _r2_ecx,%ecx\n\tret\n\t");}
VP_STUB(0) VP_STUB(1) VP_STUB(2) VP_STUB(3) VP_STUB(4) VP_STUB(5) VP_STUB(6)
VP_STUB(7) VP_STUB(8) VP_STUB(9) VP_STUB(10) VP_STUB(11) VP_STUB(12) VP_STUB(13)
#undef VP_STUB
static void *vp_stubs[]={vp_stub_0,vp_stub_1,vp_stub_2,vp_stub_3,vp_stub_4,vp_stub_5,vp_stub_6,vp_stub_7,vp_stub_8,vp_stub_9,vp_stub_10};
static void vp_build(unsigned n,uint32_t a[6],uint32_t r[3]) {
    r2_case=n;r2_seed=random32();random_bytes(r2_arena,sizeof(r2_arena));uint32_t b=(uint32_t)(uintptr_t)r2_arena;
    for(unsigned i=0;i<3;++i)r[i]=random32();memset(a,0,24);a[0]=b;a[1]=(random32()&0xffff0000u)|(n%29);a[2]=random32();a[3]=n%3?b+0x300:0;
    put32(r2_arena,(uint32_t)(uintptr_t)vp_table);vp_table[0x74/4]=(uint32_t)(uintptr_t)vp_stub_13;
    put32(r2_arena+0x300,(uint32_t)(uintptr_t)vp_source_table);vp_source_table[0x60/4]=(uint32_t)(uintptr_t)vp_stub_12;
    vp_equipment_table[0x68/4]=(uint32_t)(uintptr_t)vp_stub_11;
    put32(r2_arena+0x1f2,b+0x800);put32(r2_arena+0x202,b+0xb00);put32(r2_arena+0xb04,b+0xb20);
    put16(r2_arena+0xb08,(uint16_t)(n%5));put32(r2_arena+0xe24,b+0xb20);put16(r2_arena+0xe28,(uint16_t)(n%5));put32(r2_arena+0x206,b+0xcc0);put32(r2_arena+0x212,b+0xd40);put32(r2_arena+0xd44,b+0xd00);
    put32(r2_arena+0x20e,b+0xd60);for(unsigned i=0;i<29;++i)put16(r2_arena+0xd60+2*i,1);
    if(n%41==0)put16(r2_arena+0xd60+2*(n%29),0);
    put32(r2_arena+0x238,b+0xd80);put16(r2_arena+0x23c,4);
    for(unsigned i=0;i<4;++i){
        put32(r2_arena+0xb20+4*i,(n+i)%3?b+0xb40+64*i:0);put32(r2_arena+0xb40+64*i,(uint32_t)(uintptr_t)vp_equipment_table);
        put32(r2_arena+0xb58+64*i,b+0xc40+32*i);r2_arena[0xc57+32*i]=(BYTE)(n+i);
        put32(r2_arena+0xd80+4*i,(n/3+i)%2?b+0xda0+4*i:0);put32(r2_arena+0xe00+4*i,(n/5+i)%2?b+0xda0+4*i:0);
    }
    static const uint16_t denominators[]={1,2,255,256,32767,0xffff,0x8000,0xfeff};
    for(unsigned i=0;i<24;i+=2){
        uint32_t value=random32();put16(r2_arena+0xcc0+i,n<16384?vp_edges[(value>>8)%14]:(uint16_t)value);
        put16(r2_arena+0xd00+i,n<16384?256:denominators[(n+i/2)%8]);
    }
    put16(r2_arena+0x84a,(uint16_t)(2*((n/5)%3)));put16(r2_arena+0x84c,(uint16_t)((n/7)%2));put16(r2_arena+0x850,(uint16_t)((n/11)%2));
    r2_arena[0xa3]=(BYTE)(n%2);r2_arena[0x99]=(BYTE)((n/13)%2);r2_arena[0xa4]=(BYTE)((n/17)%3);
    r2_arena[0xa8]=(BYTE)((n/19)%2);r2_arena[0xa9]=(BYTE)((n/23)%2);r2_arena[0xaa]=(BYTE)((n/29)%2);r2_arena[0xab]=(BYTE)((n/31)%2);
    put32(r2_arena+0x1a4,n%3?b+0x600:0);put32(r2_arena+0x4a4,(n/3)%2?b:0);put32(r2_arena+0x1b6,(n/7)%2?b+0xda0:0);
    put32(mapped+0xd24e6,(n/5)%2?b+0x300:b+0x600);put32(mapped+0xa9691,b+0xda0);put32(mapped+0xa964c,random32());
    r2_arena[0xef0]=r2_arena[0xf70]=0;r2_arena[0xef1]=r2_arena[0xf71]=17;
}

static int vehicle_part_recursion_probe(void) {
    unsigned index=ES2_HOOK_INDEX_vehicle_part;
    for(unsigned n=0;n<32;++n){
        uint32_t a[6],r[3];vp_build(7+n*4,a,r);r2_case=7;a[1]=0;put16(r2_arena+0xd60,1);put16(r2_arena+0xb08,0);put16(r2_arena+0xe28,0);
        r2_arena[0xa3]=0;r2_arena[0x99]=0;r2_arena[0xa4]=0;r2_arena[0xaa]=1;put16(r2_arena+0x850,1);
        vp_table[0x74/4]=(uint32_t)(uintptr_t)(mapped+0x17de4);
        uint32_t before_global=get32(mapped+0xa964c);memcpy(r2_before,r2_arena,sizeof(r2_arena));r2_calls=0;r2_stream=r2_seed;vp_phase=0;
        uint64_t got=weapons_original(index,a,r);r2_record expected[40];unsigned calls=r2_calls;memcpy(expected,r2_log,sizeof(expected));memcpy(r2_after,r2_arena,sizeof(r2_arena));
        unsigned final_calls=0;for(unsigned i=0;i<calls;++i)if(expected[i].id==10)++final_calls;
        if(final_calls!=2)return fail("vehicle recursion did not reach both final calls");
        uint32_t after_global=get32(mapped+0xa964c);
        memcpy(r2_arena,r2_before,sizeof(r2_arena));put32(mapped+0xa964c,before_global);r2_calls=0;r2_stream=r2_seed;vp_phase=0;
        uint64_t want=weapons_vehicle_part(a,mapped,r);
        if(got!=want||calls!=r2_calls||memcmp(expected,r2_log,calls*sizeof(*expected))||memcmp(r2_after,r2_arena,sizeof(r2_arena))||get32(mapped+0xa964c)!=after_global)return fail("vehicle real recursive V74");
    }
    puts("PASS: vehicle_part 32 real recursive V74 cases; destroyed guard terminates recursion, full state/pair/calls/RNG");return 0;
}
static int vehicle_part_cases(unsigned limit) {
    unsigned index=ES2_HOOK_INDEX_vehicle_part;const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE saved[11][5];uint32_t globals[]={0xa964c,0xa9691,0xd24e6},old[3],before[3],after[3];
    for(unsigned i=0;i<3;++i)old[i]=get32(mapped+globals[i]);
    for(unsigned i=0;i<11;++i)if(actor_patch(vp_addresses[i]-0x400000u,vp_stubs[i],saved[i],TRUE))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_WEAPONS2_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){
        current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];vp_build(n,a,r);memcpy(r2_before,r2_arena,sizeof(r2_arena));for(unsigned i=0;i<3;++i)before[i]=get32(mapped+globals[i]);
        vp_phase=0;r2_calls=0;r2_stream=r2_seed;uint64_t got=weapons_original(index,a,r);
        r2_record expected[40];unsigned calls=r2_calls;memcpy(expected,r2_log,calls*sizeof(*expected));memcpy(r2_after,r2_arena,sizeof(r2_arena));for(unsigned i=0;i<3;++i)after[i]=get32(mapped+globals[i]);
        memcpy(r2_arena,r2_before,sizeof(r2_arena));for(unsigned i=0;i<3;++i)put32(mapped+globals[i],before[i]);vp_phase=0;r2_calls=0;r2_stream=r2_seed;
        unsigned counter_count;callee_counter *counters=weapons_graph_counters(index,&counter_count);uint64_t counter_before[16];
        for(unsigned i=0;i<counter_count;++i)counter_before[i]=counters[i].count;
        uint64_t want=x->replacement_context(a,d->user,r);
        if(n<1024)for(unsigned i=0;i<counter_count;++i){unsigned expected_count=0;for(unsigned j=0;j<r2_calls;++j){unsigned id=r2_log[j].id;uint32_t key=id<11?vp_addresses[id]:id==11?0x68:id==12?0x60:0x74;
            if(counters[i].key==key && (counters[i].event==ES2_COLLIDE_SLOT)==(id>=11))++expected_count;}
            if(counters[i].count-counter_before[i]!=expected_count)status=fail("vehicle callee counter delta");}
        if(got!=want||calls!=r2_calls||memcmp(expected,r2_log,calls*sizeof(*expected))||memcmp(r2_arena,r2_after,sizeof(r2_arena))) {
            fprintf(stderr,"FAIL: vehicle_part case %u pair %016llx/%016llx calls %u/%u\n",n,(unsigned long long)got,(unsigned long long)want,calls,r2_calls);
            for(unsigned i=0;i<calls&&i<r2_calls;++i)if(memcmp(expected+i,r2_log+i,sizeof(*expected))){uint32_t *e=(uint32_t *)(expected+i),*f=(uint32_t *)(r2_log+i);for(unsigned j=0;j<sizeof(*expected)/4;++j)if(e[j]!=f[j])fprintf(stderr," call%u id%u word%u %08x/%08x\n",i,expected[i].id,j,e[j],f[j]);break;}
            for(unsigned j=0;j<sizeof(r2_arena);++j)if(r2_arena[j]!=r2_after[j]){fprintf(stderr," arena +%x %x/%x\n",j,r2_after[j],r2_arena[j]);break;}status=1;
        }
        for(unsigned i=0;i<3;++i)if(get32(mapped+globals[i])!=after[i])status=fail("vehicle globals");
    }
    if(!limit)coverage_enable(0);if(!limit&&!status)status=vehicle_part_recursion_probe();for(unsigned i=0;i<11;++i)if(actor_patch(vp_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    for(unsigned i=0;i<3;++i)put32(mapped+globals[i],old[i]);current_case_valid=0;
    if(!status)printf("PASS: vehicle_part %u cases; full pair/x87, ordered lifecycle/virtual calls, narrow slots, graph and both RNG streams\n",cases);return status;
}
