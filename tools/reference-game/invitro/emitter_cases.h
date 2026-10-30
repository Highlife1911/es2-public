/* Synthetic caller/receiver graph for the independently derived emitter. */
static uint32_t te_tables[2][32],te_distance,te_heading,te_pitch;
static const uint32_t te_addresses[]={0x492934,0x4928d0,0x40ffb0,0x4929fc,0x422108,0x49295c,0x422098,0x467a48,0x47ebb8,0x4801d4,0x40a9c4,0x40b43c,0x4222fc};
static const unsigned te_counts[]={2,2,1,3,2,2,2,1,2,3,5,4,4,1};
__attribute__((used,noinline)) static uint64_t __cdecl te_stub_impl(unsigned id,const uint32_t *a) {
    if(r2_calls>=40){fail("emitter call overflow");ExitProcess(1);}
    r2_record *v=r2_log+r2_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,4*te_counts[id]);
    v->state[0]=get16(r2_arena+0x1fe);v->state[1]=get16(r2_arena+0x5a);v->state[2]=get16(r2_arena+0x32);v->state[3]=get32(r2_arena+0x21c);
    (void)es2_rng_next(r2_arena+0xe80+0x80*(id&1));
    uint32_t eax=r2_next(),edx=r2_next();r2_ecx=r2_next();
    if(id==0||id==1||id==3||id==5){for(unsigned j=0;j<3;++j)v->input[j]=get32((BYTE *)(uintptr_t)a[0]+4*j);v->args[0]=0;}
    if(id==0)eax=(eax&0xffff0000u)|(uint16_t)te_heading;
    if(id==1)eax=te_distance;
    if(id==2)eax=(uint32_t)(uintptr_t)(r2_arena+0x600);
    if(id==3){for(unsigned j=0;j<3;++j)put32((BYTE *)(uintptr_t)a[0]+4*j,v->input[j]+j+1);}
    if(id==5)eax=(eax&0xffff0000u)|(uint16_t)te_pitch;
    if(id==7)eax=(eax&0xffff0000u)|((r2_case/4)%2);
    if(id==8){for(unsigned j=0;j<9;++j)put16((BYTE *)(uintptr_t)a[1]+2*j,(uint16_t)r2_next());}
    if(id==9){
        for(unsigned j=0;j<3;++j)v->input[j]=get16((BYTE *)(uintptr_t)a[0]+2*j);
        for(unsigned j=0;j<16;++j)v->input[3+j]=get16((BYTE *)(uintptr_t)a[1]+2*j);
        v->args[0]=v->args[2]=0;for(unsigned j=0;j<3;++j)put32((BYTE *)(uintptr_t)a[2]+4*j,r2_next());
    }
    if(id==10||id==11){for(unsigned j=0;j<3;++j)v->input[j]=get32((BYTE *)(uintptr_t)a[1]+4*j);v->args[1]=0;
        if(r2_case%7==0)put16(r2_arena+0x32,0);
        if(r2_case>=16384 && r2_case%17==0)put16(r2_arena+0x1fe,0xffff);
    }
    if(id==12)r2_excluded=a[2];
    if(id==13){static const uint16_t speeds[]={0,1,0xffff,32767,32768};unsigned k=a[0]==(uint32_t)(uintptr_t)r2_arena?r2_case/3:r2_case/5;eax=(eax&0xffff0000u)|speeds[k%5];
        if(a[0]==(uint32_t)(uintptr_t)r2_arena && r2_case%11==0)put32(r2_arena+0x1a4,(uint32_t)(uintptr_t)(r2_arena+0x500));}
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define TE_STUB(n) __attribute__((naked)) static void te_stub_##n(void) {__asm__ volatile( \
    "leal 4(%esp),%eax\n\tpushl %eax\n\tpushl $" #n "\n\tcalll _te_stub_impl\n\taddl $8,%esp\n\tmovl _r2_ecx,%ecx\n\tret\n\t");}
TE_STUB(0) TE_STUB(1) TE_STUB(2) TE_STUB(3) TE_STUB(4) TE_STUB(5) TE_STUB(6)
TE_STUB(7) TE_STUB(8) TE_STUB(9) TE_STUB(10) TE_STUB(11) TE_STUB(12) TE_STUB(13)
#undef TE_STUB
static void *te_stubs[]={te_stub_0,te_stub_1,te_stub_2,te_stub_3,te_stub_4,te_stub_5,te_stub_6,te_stub_7,te_stub_8,te_stub_9,te_stub_10,te_stub_11,te_stub_12};
static void te_build(unsigned n,uint32_t a[6],uint32_t r[3]) {
    static const uint32_t distances[]={0xffffffffu,9999,10000,29999,30000,64999,65000,90000,90001,199999,200000,200001,0x80000000u,0x7fffffffu};
    static const uint16_t angles[]={64535,64536,999,1000,57343,57344,8191,8192,32767,32768};
    static const uint16_t phases[]={0,1,2,0xffff,32767,32768},rates[]={0xfff5,0xfff6,9,10};
    r2_case=n;r2_seed=random32();random_bytes(r2_arena,sizeof(r2_arena));memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();uint32_t b=(uint32_t)(uintptr_t)r2_arena;a[0]=b;
    put32(r2_arena,(uint32_t)(uintptr_t)te_tables[0]);put32(r2_arena+0x400,(uint32_t)(uintptr_t)te_tables[1]);put32(r2_arena+0x500,(uint32_t)(uintptr_t)te_tables[1]);
    te_tables[0][14]=te_tables[1][14]=(uint32_t)(uintptr_t)te_stub_13;
    put32(r2_arena+0x1a4,b+0x400);put16(r2_arena+0xc,0);put16(r2_arena+0x10,0);
    put16(r2_arena+0x32,(n/13)%2);put16(r2_arena+0x5a,(n/3)%2);put16(r2_arena+0x1fe,phases[(n/140)%6]);
    put16(r2_arena+0x1f4,rates[(n/840)%4]);r2_arena[0xae]=(BYTE)((n/7)%2);
    put32(r2_arena+0x2e,2999+n%4);put32(r2_arena+0x42e,0);put32(r2_arena+0x52e,0);
    put16(r2_arena+0x60a,(uint16_t)((n/11)%3?256:0));
    te_distance=distances[(n/10)%14];te_heading=angles[n%10];te_pitch=angles[(n/2)%10];
    r2_arena[0xef0]=r2_arena[0xf70]=0;r2_arena[0xef1]=r2_arena[0xf71]=17;
}
static int target_emit_entry_probe(void) {
    for(unsigned fast=0;fast<2;++fast)for(unsigned p=0;p<2;++p){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],poison=p?0xa7a7a7a7u:0x31313131u;te_build(100,a,r);
        put16(r2_arena+0x1fe,0xffff);te_distance=65000;memcpy(r2_before,r2_arena,sizeof(r2_arena));r2_calls=0;r2_stream=r2_seed;
        es2_probe_call(mapped+0x226a0,a[0],0,0,poison,expected_pair);
        if(r2_excluded!=poison)return fail("emitter original control residue offset");
        unsigned calls=r2_calls;r2_record expected[40];memcpy(expected,r2_log,calls*sizeof(*expected));memcpy(r2_after,r2_arena,sizeof(r2_arena));
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,function_hook_descriptor(ES2_HOOK_INDEX_target_emit),&o,function_hook_extension(ES2_HOOK_INDEX_target_emit)))return fail("emitter residue install");
        memcpy(r2_arena,r2_before,sizeof(r2_arena));r2_calls=0;r2_stream=r2_seed;es2_probe_call(mapped+0x226a0,a[0],0,0,poison,actual_pair);
        int bad=memcmp(expected_pair,actual_pair,8)||calls!=r2_calls||memcmp(expected,r2_log,calls*sizeof(*expected))||memcmp(r2_after,r2_arena,sizeof(r2_arena))||h.calls!=1;
        if(!es2_hook_uninstall(&h))return fail("emitter residue uninstall");if(bad)return fail("emitter entry residue comparison");
    }
    es2_hook_set_fast_path(TRUE);puts("PASS: target_emit control residue 31/a7, fast/preserving dispatch, exact full trace/pair/state");return 0;
}
static int target_emit_modes(void) {
    unsigned nested[]={ES2_HOOK_INDEX_timer_word_step,ES2_HOOK_INDEX_heading_point,ES2_HOOK_INDEX_euler_matrix};
    uint16_t old_scale=get16(mapped+0xd3b64);int status=0;unsigned total=0;uint64_t leaf_calls[3]={0};
    for(unsigned modes=0;modes<6&&!status;++modes)for(unsigned n=0;n<64&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2];te_build(n,a,r);
        put16(r2_arena+0x32,0);put16(r2_arena+0x1fe,2);put16(r2_arena+0x1f4,0);r2_arena[0x21e]=0;put16(r2_arena+0x21f,n%2?0:1000);
        te_distance=20000;te_pitch=0;put16(mapped+0xd3b64,(uint16_t)(64+16*n));
        uint32_t heading_args[2]={(uint32_t)(uintptr_t)(r2_arena+0x426),a[0]+0x26u};es2_weapon_registers heading_regs={0};
        weapons_machine_call((uint32_t)(uintptr_t)(mapped+0x92934),heading_args,2,&heading_regs);put16(r2_arena+0x10,(uint16_t)heading_regs.eax);
        memcpy(r2_before,r2_arena,sizeof(r2_arena));es2_hook h[4]={{0}};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<3;++j){o.mode=(es2_hook_mode)((modes+j)%3);if(!nested_install_hook(h+j,nested[j],&o,function_hook_extension(nested[j])))return fail("emitter leaf mode install");}
        r2_calls=0;r2_stream=r2_seed;es2_probe_call(mapped+0x226a0,a[0],0,0,0x31313131,expected_pair);
        unsigned calls=r2_calls;r2_record expected[40];memcpy(expected,r2_log,calls*sizeof(*expected));memcpy(r2_after,r2_arena,sizeof(r2_arena));
        o.mode=modes<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(h+3,function_hook_descriptor(ES2_HOOK_INDEX_target_emit),&o,function_hook_extension(ES2_HOOK_INDEX_target_emit)))return fail("emitter caller mode install");
        memcpy(r2_arena,r2_before,sizeof(r2_arena));r2_calls=0;r2_stream=r2_seed;es2_probe_call(mapped+0x226a0,a[0],0,0,0x31313131,actual_pair);
        if(memcmp(expected_pair,actual_pair,8)||calls!=r2_calls||memcmp(expected,r2_log,calls*sizeof(*expected))||memcmp(r2_after,r2_arena,sizeof(r2_arena)))status=fail("emitter real-callee mode comparison");
        for(unsigned j=0;j<4;++j){if(j<3)leaf_calls[j]+=h[j].calls;if(h[j].mismatches)status=fail("emitter leaf compare mismatch");if(!es2_hook_uninstall(h+j))return fail("emitter mode uninstall");}++total;
    }
    for(unsigned j=0;j<3;++j)if(!leaf_calls[j])status=fail("emitter mixed leaf was never entered");
    put16(mapped+0xd3b64,old_scale);if(!status)printf("PASS: target_emit %u mixed original/replaced caller and timer/heading/Euler modes with real transforms\n",total);return status;
}
static int target_emit_cases(unsigned limit) {
    unsigned index=ES2_HOOK_INDEX_target_emit;const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE saved[13][5];for(unsigned i=0;i<13;++i)if(actor_patch(te_addresses[i]-0x400000u,te_stubs[i],saved[i],TRUE))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_WEAPONS2_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){
        current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];te_build(n,a,r);memcpy(r2_before,r2_arena,sizeof(r2_arena));r2_calls=0;r2_stream=r2_seed;
        uint64_t got=weapons_original(index,a,r);unsigned calls=r2_calls;r2_record expected[40];memcpy(expected,r2_log,calls*sizeof(*expected));memcpy(r2_after,r2_arena,sizeof(r2_arena));
        r[2]=r2_excluded;memcpy(r2_arena,r2_before,sizeof(r2_arena));r2_calls=0;r2_stream=r2_seed;
        unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t before[16];for(unsigned i=0;i<count;++i)before[i]=c[i].count;
        uint64_t want=x->replacement_context(a,d->user,r);
        if(n<1024)for(unsigned i=0;i<count;++i){unsigned expected_count=0;for(unsigned j=0;j<r2_calls;++j){unsigned id=r2_log[j].id;if(c[i].key==(id==13?0x38:te_addresses[id]))++expected_count;}if(c[i].count-before[i]!=expected_count)status=fail("emitter counter delta");}
        if(got!=want||calls!=r2_calls||memcmp(expected,r2_log,calls*sizeof(*expected))||memcmp(r2_after,r2_arena,sizeof(r2_arena))){
            fprintf(stderr,"FAIL: target_emit case %u pair %016llx/%016llx calls %u/%u\n",n,(unsigned long long)got,(unsigned long long)want,calls,r2_calls);
            for(unsigned j=0;j<calls&&j<r2_calls;++j)if(memcmp(expected+j,r2_log+j,sizeof(*expected))){uint32_t *e=(uint32_t *)(expected+j),*f=(uint32_t *)(r2_log+j);for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %x/%x\n",j,expected[j].id,k,e[k],f[k]);break;}status=1;
        }
    }
    if(!limit)coverage_enable(0);if(!limit&&!status)status=target_emit_entry_probe();
    if(!limit&&!status){unsigned real[]={0,7,8,9};for(unsigned j=0;j<4;++j){unsigned i=real[j];if(actor_patch(te_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;}status=target_emit_modes();}
    for(unsigned i=0;i<13;++i)if(actor_patch(te_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;current_case_valid=0;
    if(!status)printf("PASS: target_emit %u cases; full pair/x87, ordered calls/slots, target mutation, muzzle cache, state and both RNG streams\n",cases);return status;
}
