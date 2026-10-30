/* Synthetic weapon graphs. Only the private, hash-checked mapping executes
 * original code; log entries contain our generated state and call contracts. */
typedef struct r3_record {uint32_t id,args[5],input[40],state[5],eax,edx;} r3_record;
static BYTE r3_arena[4096],r3_before[4096],r3_after[4096];
static r3_record r3_log[48];
static unsigned r3_case,r3_hook,r3_calls,r3_allocations;
static uint32_t r3_seed,r3_stream,r3_excluded,r3_sweep_result;
__attribute__((used)) static uint32_t r3_ecx;
static const uint32_t r3_addresses[]={0x40e788,0x40bf74,0x40b5a0,0x40b43c,0x40f540,
    0x4801d4,0x49630c,0x40ffc8,0x47e0b0,0x426528,0x4628e0,0x48043c,0x471b30,0x40b804,0x480304};
static const unsigned r3_counts[]={3,5,5,4,1,3,1,2,2,1,2,3,1,5,3};
static uint32_t r3_next(void){r3_stream=r3_stream*1664525u+1013904223u;return r3_stream;}
static void r3_copy_words(r3_record *v,unsigned at,uint32_t address,unsigned count,unsigned width){
    const BYTE *p=(const BYTE *)(uintptr_t)address;
    for(unsigned i=0;i<count;++i)v->input[at+i]=width==4?get32(p+4*i):get16(p+2*i);
}
__attribute__((used,noinline)) static uint64_t __cdecl r3_stub_impl(unsigned id,const uint32_t *a){
    if(r3_calls==48){fail("round3 synthetic call overflow");ExitProcess(1);}
    r3_record *v=r3_log+r3_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,4*r3_counts[id]);
    v->state[0]=get16(r3_arena+0x9d);v->state[1]=r3_arena[0x64];v->state[2]=r3_arena[0x6d];
    v->state[3]=get16(r3_arena+0x51);v->state[4]=get32(r3_arena+0xc00);
    (void)es2_rng_next(r3_arena+0xd00+0x100*(id&1));
    uint32_t eax=r3_next(),edx=r3_next();r3_ecx=r3_next();uint32_t b=(uint32_t)(uintptr_t)r3_arena;
    if(id==0){
        /* Preparation can replace each graph pointer; emitters must reread. */
        if(r3_case%3==0)put32(r3_arena+0x38,b+0x290);
        if(r3_case%5==0)put32(r3_arena+0x3c,b+0x400);
        if(r3_case%7==0)put32(r3_arena+0x40,b+0x580);
        for(unsigned j=0;j<50;++j)mapped[0xa9834+j]=(BYTE)r3_next();
    }else if(id==1){
        r3_copy_words(v,0,a[1],8,4);
    }else if(id==2||id==3){
        r3_copy_words(v,0,a[1],3,4);r3_copy_words(v,3,a[2],3,2);
        if(r3_case%11==0)put32(r3_arena+0x3c,b+0x400);
        if(r3_case%13==0)put32(r3_arena+0x40,b+0x580);
        if(r3_case%17==0)put16(r3_arena+0x9d,(uint16_t)r3_next());
    }else if(id==4){eax=b+0x600;
    }else if(id==5){
        r3_copy_words(v,0,a[0],3,2);r3_copy_words(v,3,a[1],8,4);v->args[0]=0;
        for(unsigned j=0;j<3;++j)put32((BYTE *)(uintptr_t)a[2]+4*j,r3_next());
    }else if(id==6){__asm__ volatile("movl %%fs:0,%0":"=r"(eax));
    }else if(id==7){eax=b+0x700;
    }else if(id==9){
        BYTE *p=(BYTE *)(uintptr_t)a[0],*segment=(BYTE *)(uintptr_t)get32(p);
        v->args[0]=0;v->input[0]=get32(segment);v->input[1]=get32(segment+4);v->input[2]=get16(segment+8);
        v->input[3]=get16(p+4);v->input[4]=get16(p+6);v->input[5]=get16(p+8);
        v->input[6]=get32(p+10);v->input[7]=get32(p+14);v->input[8]=get16(p+18);
        r3_excluded=get32(p+20);v->input[9]=r3_excluded;
        memset(segment+10,0x6b,32);put32(segment+4,r3_next());eax=r3_sweep_result;
        if(r3_case%11==0){BYTE *matrix=(BYTE *)(uintptr_t)get32(segment);matrix[19]^=0xa7;put32(matrix+20,r3_next());}
    }else if(id==10){r3_copy_words(v,0,a[1],3,4);
    }else if(id==11||id==14){
        r3_copy_words(v,0,a[0],3,4);r3_copy_words(v,3,a[1],id==11?8:5,4);
        v->args[0]=v->args[2]=0;if(id==14)v->args[1]=0;
        for(unsigned j=0;j<3;++j)put32((BYTE *)(uintptr_t)a[2]+4*j,r3_next());
    }else if(id==12){eax=((r3_case+r3_allocations++)%3)?b+0x800:0;
    }else if(id==13){
        r3_copy_words(v,0,a[2],3,4);r3_copy_words(v,3,a[3],3,4);v->args[3]=0;
        uint32_t head;__asm__ volatile("movl %%fs:0,%0":"=r"(head));
        v->input[6]=get16((BYTE *)(uintptr_t)head+16);
        v->input[7]=get32((BYTE *)(uintptr_t)head+(v->input[6]==20?40:v->input[6]==44?36:32));
        put32(r3_arena+0x800,get32(r3_arena+0x800)+1);
        if(r3_case%7==0)put32(mapped+0x2cbee4,b+0xc04);
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define R3_STUB(n) __attribute__((naked)) static void r3_stub_##n(void){__asm__ volatile( \
    "leal 4(%esp),%eax\n\tpushl %eax\n\tpushl $" #n "\n\tcalll _r3_stub_impl\n\taddl $8,%esp\n\tmovl _r3_ecx,%ecx\n\tret\n\t");}
R3_STUB(0) R3_STUB(1) R3_STUB(2) R3_STUB(3) R3_STUB(4) R3_STUB(5)
R3_STUB(7) R3_STUB(8) R3_STUB(9) R3_STUB(10) R3_STUB(11) R3_STUB(12) R3_STUB(13) R3_STUB(14)
#undef R3_STUB
/* This stub implements a caller-frame contract, not an exception runtime.
 * Keeping the real thread head in the saved slot makes original cleanup safe. */
__attribute__((naked)) static void r3_stub_6(void){__asm__ volatile(
    "pushl %eax\n\tmovl %esp,%eax\n\tpushl %eax\n\tpushl $6\n\tcalll _r3_stub_impl\n\taddl $12,%esp\n\t"
    "movl %eax,-44(%ebp)\n\tmovw $0,-28(%ebp)\n\tleal -44(%ebp),%ecx\n\tmovl %ecx,%fs:0\n\t"
    "movl _r3_ecx,%ecx\n\tret\n\t");}
static void *r3_stubs[]={r3_stub_0,r3_stub_1,r3_stub_2,r3_stub_3,r3_stub_4,r3_stub_5,r3_stub_6,
    r3_stub_7,r3_stub_8,r3_stub_9,r3_stub_10,r3_stub_11,r3_stub_12,r3_stub_13,r3_stub_14};
static void r3_globals(BYTE *out,int restore){
    static const unsigned offsets[]={0xa9834,0xa96c2,0x2cbee4};
    static const unsigned sizes[]={50,4,4};
    for(unsigned i=0;i<3;++i){if(restore)memcpy(mapped+offsets[i],out,sizes[i]);else memcpy(out,mapped+offsets[i],sizes[i]);out+=sizes[i];}
}
static void r3_reset(void){r3_calls=r3_allocations=0;r3_stream=r3_seed;}
static void r3_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    static const uint16_t edges[]={0,1,0xffff,32767,32768,2,3,4,19,0xfffc};
    static const int8_t classes[]={-128,-1,0,3,4,5,127};
    static const uint32_t distances[]={0,1,4999,5000,5001,9999,10000,10001,15000,15001,0xffffffffu,0x80000000u};
    r3_hook=index;r3_case=n;r3_seed=random32();random_bytes(r3_arena,sizeof(r3_arena));
    uint32_t b=(uint32_t)(uintptr_t)r3_arena;memset(a,0,24);for(unsigned i=0;i<3;++i)r[i]=random32();
    r3_arena[0xd70]=r3_arena[0xe70]=0;r3_arena[0xd71]=r3_arena[0xe71]=17;
    a[0]=b+0x20;a[1]=b+0x180;a[2]=random32();
    put32(r3_arena+0x38,b+0x280);r3_arena[0x286]=(BYTE)classes[n%7];r3_arena[0x296]=(BYTE)classes[(n+1)%7];
    put32(r3_arena+0x3c,b+0x300);put32(r3_arena+0x40,b+0x500);
    put16(r3_arena+0x9d,n<1024?edges[(n/7)%10]:(uint16_t)random32());r3_arena[0x6d]=(BYTE)((n/2)%2);
    for(unsigned k=0;k<2;++k){
        BYTE *c=r3_arena+0x300+0x100*k;put32(c+0x30,distances[(n/36+k)%12]);put16(c+0x38,n<1024?edges[(n/70+k)%10]:(uint16_t)random32());
        put16(c+0x3c,(n/3)%2?3:2);put16(c+0x3e,(n/5)%2?19:18);put16(c+0x4c,n<1024?edges[(n/10+k)%10]:(uint16_t)random32());
        put16(r3_arena+0x500+0x80*k,(n/2+k)%3?2:4);
    }
    for(unsigned j=0;j<50;++j)mapped[0xa9834+j]=(BYTE)random32();
    put32(mapped+0xa96c2,b+0xa00);put32(mapped+0x2cbee4,b+0xc00);
    r3_sweep_result=distances[(n/3)%12];
    if(index==ES2_HOOK_INDEX_impact){
        a[0]=(random32()&0xffff0000u)|edges[n%10];if(n%5==0)a[0]=(a[0]&0xffff0000u)|7;
        a[1]=b+0x900;a[2]=distances[(n/36)%12];a[3]=b+0x180;a[4]=n<1024?edges[(n/10)%10]:random32();
        r3_arena[0x912]=(BYTE)(n%3);
        if(n==1023){a[0]=(a[0]&0xffff0000u)|1;a[2]=0x7fffffffu;r3_sweep_result=0x7fffffffu;}
    }
}
static int r3_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before_globals[58],after_globals[58],actual_globals[58];memcpy(r3_before,r3_arena,sizeof(r3_arena));r3_globals(before_globals,0);
    r3_reset();uint64_t expected_pair=weapons_original(index,a,r);unsigned calls=r3_calls;r3_record expected[48];memcpy(expected,r3_log,calls*sizeof(*expected));
    memcpy(r3_after,r3_arena,sizeof(r3_arena));r3_globals(after_globals,0);
    memcpy(r3_arena,r3_before,sizeof(r3_arena));r3_globals(before_globals,1);r3_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    weapons_emitter_residue residue={0x0000a731u,r3_excluded,0};
    r[2]=index==ES2_HOOK_INDEX_impact?r3_excluded:(uint32_t)(uintptr_t)&residue;
    uint64_t actual_pair=x->replacement_context(a,d->user,r);r3_globals(actual_globals,0);
    if(n<1024)for(unsigned i=0;i<count;++i){unsigned hits=0;for(unsigned j=0;j<r3_calls;++j)if(c[i].key==r3_addresses[r3_log[j].id])++hits;if(c[i].count-counts[i]!=hits)return fail("round3 callee counter delta");}
    if(expected_pair!=actual_pair||calls!=r3_calls||memcmp(expected,r3_log,calls*sizeof(*expected))||memcmp(r3_after,r3_arena,sizeof(r3_arena))||memcmp(after_globals,actual_globals,58)){
        fprintf(stderr,"FAIL: %s case %u pair %016llx/%016llx calls %u/%u\n",d->name,n,(unsigned long long)expected_pair,(unsigned long long)actual_pair,calls,r3_calls);
        for(unsigned j=0;j<calls&&j<r3_calls;++j)if(memcmp(expected+j,r3_log+j,sizeof(*expected))){uint32_t *e=(uint32_t *)(expected+j),*f=(uint32_t *)(r3_log+j);for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);break;}
        for(unsigned j=0;j<sizeof(r3_arena);++j)if(r3_after[j]!=r3_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,r3_after[j],r3_arena[j]);break;}return 1;
    }return 0;
}
static int r3_installed_probes(unsigned index,unsigned states);
static int r3_modes(unsigned index);
static int r3_deep_stack_probe(unsigned index);
static int weapons_round3_cases(unsigned index,unsigned limit){
    BYTE saved[15][5],old_globals[58];r3_globals(old_globals,0);int active[15]={0};
    for(unsigned i=0;i<15;++i)if(r3_addresses[i]!=function_hook_descriptor(index)->rva+0x400000u){if(actor_patch(r3_addresses[i]-0x400000u,r3_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_WEAPONS3_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==8192)coverage_enable(0);uint32_t a[6],r[3];r3_build(index,n,a,r);status=r3_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=r3_installed_probes(index,12);
    if(!limit&&!status&&index!=ES2_HOOK_INDEX_impact)status=r3_deep_stack_probe(index);
    for(unsigned i=0;i<15;++i)if(active[i]&&actor_patch(r3_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    r3_globals(old_globals,1);current_case_valid=0;
    if(!limit&&!status)status=r3_modes(index);
    if(!status)printf("PASS: %-20s %u cases; full return pair/x87, ordered full-slot calls, transient payloads, graph/state/RNG and guards\n",function_hook_descriptor(index)->name,cases);return status;
}

/* A five-slot caller with a deliberately initialized unused stack region. */
__attribute__((naked)) static void r3_poison_call(void *target,const uint32_t *a,uint32_t pattern,uint32_t pair[2]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $768,%esp\n\tmovl %esp,%edi\n\tmovl 16(%ebp),%eax\n\tmovl $192,%ecx\n\trep stosl\n\taddl $512,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 16(%esi)\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%eax\n\tmovl %eax,%edx\n\tmovl %eax,%ecx\n\t"
        "cmpl $0x5252a731,%eax\n\tjne 1f\n\tmovb $0x31,-77(%esp)\n\tmovb $0xa7,-109(%esp)\n\t"
        "movb $0x31,-65(%esp)\n\tmovb $0xa7,-97(%esp)\n\t1: call *8(%ebp)\n\t"
        "movl 20(%ebp),%ecx\n\tmovl %eax,(%ecx)\n\tmovl %edx,4(%ecx)\n\tleal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static int r3_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;r3_build(index,n,a,r);
        BYTE before_globals[58],after_globals[58],actual_globals[58],expected_fpu[108],actual_fpu[108];memcpy(r3_before,r3_arena,sizeof(r3_arena));r3_globals(before_globals,0);
        r3_reset();r3_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        if(index==ES2_HOOK_INDEX_impact&&r3_excluded!=pattern){status=fail("immediate original excluded word offset");break;}
        unsigned calls=r3_calls;r3_record expected[48];memcpy(expected,r3_log,calls*sizeof(*expected));memcpy(r3_after,r3_arena,sizeof(r3_arena));r3_globals(after_globals,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("round3 poison install");
        memcpy(r3_arena,r3_before,sizeof(r3_arena));r3_globals(before_globals,1);r3_reset();r3_poison_call(mapped+d->rva,a,pattern,actual_pair);es2_hook_fpu_save(actual_fpu);r3_globals(actual_globals,0);
        if(memcmp(expected_pair,actual_pair,8)||calls!=r3_calls||memcmp(expected,r3_log,calls*sizeof(*expected))||memcmp(r3_after,r3_arena,sizeof(r3_arena))||memcmp(after_globals,actual_globals,58)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1){
            fprintf(stderr,"FAIL: %s installed poison fast=%u pattern=%08x case=%u pair=%08x:%08x/%08x:%08x calls=%u/%u\n",d->name,fast,pattern,n,expected_pair[1],expected_pair[0],actual_pair[1],actual_pair[0],calls,r3_calls);status=1;
            for(unsigned j=0;j<calls&&j<r3_calls;++j)if(memcmp(expected+j,r3_log+j,sizeof(*expected))){uint32_t *e=(uint32_t *)(expected+j),*f=(uint32_t *)(r3_log+j);for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);break;}
        }
        if(!es2_hook_uninstall(&h))return fail("round3 poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state\n",d->name,total);return status;
}

static uint32_t r3_resource_vtable[16];
static uint64_t __cdecl r3_resource_lookup(uint32_t receiver,uint32_t selector){
    (void)receiver;(void)selector;return (uint32_t)(uintptr_t)(r3_arena+0xb80);
}
static void r3_real_prepare_graph(unsigned n){
    uint32_t b=(uint32_t)(uintptr_t)r3_arena;
    put32(r3_arena+0x1b4,b+0xa00);put32(r3_arena+0xa04,b+0xa80);
    put32(r3_arena+0xa80,(uint32_t)(uintptr_t)r3_resource_vtable);r3_resource_vtable[8]=(uint32_t)(uintptr_t)r3_resource_lookup;
    put32(r3_arena+0xa16,b+0xb00);put16(r3_arena+0xb84,0);
    memset(r3_arena+0xb00,0,32);put16(r3_arena+0xb00,16384);put16(r3_arena+0xb08,16384);put16(r3_arena+0xb10,16384);
    r3_arena[0xb12]=(BYTE)(n%3);r3_arena[0xb13]=0x6d;
    memset(r3_arena+0x18c,0,6);put16(r3_arena+0x1b2,0);r3_arena[0x1a5]=0x5b;
    put32(r3_arena+0x7b,n%4<2?0:b+0x600);put32(r3_arena+0x7f,n%4%2?0:b+0x600);
    put32(mapped+0x9ef34,0);put16(mapped+0xd3b64,(uint16_t)(64+n*4));
}
static int r3_modes(unsigned index){
    static const unsigned nested[]={ES2_HOOK_INDEX_weapon_prepare,ES2_HOOK_INDEX_impact,
        ES2_HOOK_INDEX_time_scale,ES2_HOOK_INDEX_euler_matrix,ES2_HOOK_INDEX_matrix_compose,
        ES2_HOOK_INDEX_matrix_angles,ES2_HOOK_INDEX_multiply_q10,ES2_HOOK_INDEX_transform_point,ES2_HOOK_INDEX_rotate_point};
    BYTE saved[15][5],old_globals[58];int patched[15]={0};r3_globals(old_globals,0);
    uint32_t old_adjust=get32(mapped+0x9ef34);uint16_t old_scale=get16(mapped+0xd3b64);
    for(unsigned i=0;i<15;++i){if(i==0||i==1||i==5||i==6||i==8||i==11||i==14)continue;
        if(actor_patch(r3_addresses[i]-0x400000u,r3_stubs[i],saved[i],TRUE))return 1;patched[i]=1;}
    int status=0;unsigned total=0;uint64_t entered[9]={0};
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<64&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n%3==2?0x5252a731u:n&1?0xa7a7a7a7u:0x31313131u;r3_build(index,n,a,r);r3_real_prepare_graph(n);
        /* Ensure both emitters exercise the real impact and its transforms. */
        if(index!=ES2_HOOK_INDEX_impact)put16(r3_arena+0x500,4);
        BYTE before_globals[58],after_globals[58],actual_globals[58];memcpy(r3_before,r3_arena,sizeof(r3_arena));r3_globals(before_globals,0);
        es2_hook h[10]={{0}};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<9;++j){if(nested[j]==index)continue;o.mode=(es2_hook_mode)((mode+j)%3);
            if((j==0||j==1)&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!nested_install_hook(h+j,nested[j],&o,function_hook_extension(nested[j])))return fail("round3 nested install");}
        uint32_t scale_args[2]={16,1200+n*17u};es2_weapon_registers scale_regs={0};weapons_machine_call((uint32_t)(uintptr_t)(mapped+0x67894),scale_args,2,&scale_regs);
        r3_reset();r3_poison_call(mapped+function_hook_descriptor(index)->rva,a,pattern,expected_pair);
        unsigned calls=r3_calls;r3_record expected[48];memcpy(expected,r3_log,calls*sizeof(*expected));memcpy(r3_after,r3_arena,sizeof(r3_arena));r3_globals(after_globals,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;if(!weapons_install_hook(h+9,function_hook_descriptor(index),&o,function_hook_extension(index)))return fail("round3 caller mode install");
        memcpy(r3_arena,r3_before,sizeof(r3_arena));r3_globals(before_globals,1);r3_reset();r3_poison_call(mapped+function_hook_descriptor(index)->rva,a,pattern,actual_pair);r3_globals(actual_globals,0);
        if(memcmp(expected_pair,actual_pair,8)||calls!=r3_calls||memcmp(expected,r3_log,calls*sizeof(*expected))||memcmp(r3_after,r3_arena,sizeof(r3_arena))||memcmp(after_globals,actual_globals,58)){
            fprintf(stderr,"FAIL: %s mixed mode=%u case=%u pair=%08x:%08x/%08x:%08x calls=%u/%u\n",function_hook_descriptor(index)->name,mode,n,expected_pair[1],expected_pair[0],actual_pair[1],actual_pair[0],calls,r3_calls);status=1;
            for(unsigned j=0;j<calls&&j<r3_calls;++j)if(memcmp(expected+j,r3_log+j,sizeof(*expected))){uint32_t *e=(uint32_t *)(expected+j),*f=(uint32_t *)(r3_log+j);for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);break;}
            for(unsigned j=0;j<58;++j)if(after_globals[j]!=actual_globals[j])fprintf(stderr," global +%u %02x/%02x\n",j,after_globals[j],actual_globals[j]);
        }
        for(unsigned j=0;j<10;++j){if(j<9)entered[j]+=h[j].calls;if(h[j].mismatches)status=fail("round3 nested compare mismatch");if(h[j].descriptor&&!es2_hook_uninstall(h+j))return fail("round3 nested uninstall");}++total;
    }
    for(unsigned i=0;i<15;++i)if(patched[i]&&actor_patch(r3_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    r3_globals(old_globals,1);put32(mapped+0x9ef34,old_adjust);put16(mapped+0xd3b64,old_scale);
    if(!status&&(!entered[2]||!entered[6]||!entered[7]))status=fail("round3 expected mixed callee never entered");
    if(!status&&index!=ES2_HOOK_INDEX_impact&&(!entered[0]||!entered[1]||!entered[3]||!entered[4]||!entered[5]))status=fail("round3 preparation mixed callee never entered");
    if(!status)printf("PASS: %s %u real preparation/impact/timing/transform mixed-mode cases with 31/a7 stack\n",function_hook_descriptor(index)->name,total);return status;
}

/* The original-coordinate bridge suspends its core frame below the guest.
 * Fill more than one comparison frame: an 8 KiB gap corrupts suspended state,
 * even when a small comparison span happens not to touch most of its frame. */
__attribute__((used,noinline)) static uint64_t __cdecl r3_deep_prepare_impl(const uint32_t *a){
    volatile BYTE stack[73728];uint32_t checksum=0;
    for(unsigned i=0;i<sizeof(stack);++i){stack[i]=(BYTE)(i+r3_case);checksum+=stack[i];}
    put32(r3_arena+0xc08,checksum);
    return r3_stub_impl(0,a);
}
__attribute__((naked)) static void r3_deep_prepare_stub(void){__asm__ volatile(
    "leal 4(%esp),%eax\n\tpushl %eax\n\tcalll _r3_deep_prepare_impl\n\taddl $4,%esp\n\tmovl _r3_ecx,%ecx\n\tret\n\t");}
static int r3_deep_stack_probe(unsigned index){
    BYTE saved[5];if(actor_patch(0xe788,r3_deep_prepare_stub,saved,TRUE))return 1;
    int result=r3_installed_probes(index,512);
    if(actor_patch(0xe788,NULL,saved,FALSE))return 1;
    if(!result)printf("PASS: %s 2048 deep-stack cases; guest fills 73728 bytes above suspended core, exact pair/trace/state/RNG\n",function_hook_descriptor(index)->name);
    return result;
}
