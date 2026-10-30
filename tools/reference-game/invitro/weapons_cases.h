/* Generated states only. Original callees are replaced in the private image;
 * own cdecl stubs make every result register and callback write observable. */
static uint64_t weapons_original(unsigned index,const uint32_t *a,const uint32_t *registers) {
    es2_hook_result r;es2_hook_call c={mapped+function_hook_descriptor(index)->rva,ES2_ABI_CDECL,
        function_hook_descriptor(index)->arg_count,a,initial_fpu,&r,registers};
    es2_hook_machine_call(&c);
    if(get16(r.fpu)!=get16(initial_fpu)||get16(r.fpu+4)!=get16(initial_fpu+4)||get16(r.fpu+8)!=get16(initial_fpu+8)) {
        fail("weapons changed x87 control/status/tags");ExitProcess(1);
    }
    return (uint64_t)r.edx<<32|r.eax;
}
static int weapons_leaf_cases(unsigned index,unsigned limit) {
    const es2_hook_descriptor *d=function_hook_descriptor(index);
    const es2_hook_extension *x=function_hook_extension(index);
    unsigned cases=case_budget(limit,1001024);
    BYTE arena[128],before[128],after[128];
    uint32_t base_slot=index==ES2_HOOK_INDEX_projectile_def?0xa96d0u:0xa96f8u;
    uint32_t saved=get32(mapped+base_slot);
    if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases;++n) {
        current_case=n;current_case_valid=1;
        if(!limit&&n==4096)coverage_enable(0);
        random_bytes(arena,sizeof(arena));
        uint32_t a[6]={0},r[3]={random32(),random32(),random32()};
        if(index==ES2_HOOK_INDEX_mask_merge) {
            a[0]=(uint32_t)(uintptr_t)(arena+24);
            int displacement=(int)(n%31)-15;
            a[1]=limit?(uint32_t)(uintptr_t)(arena+64):a[0]+(uint32_t)displacement;
        } else {
            a[0]=(uint32_t)(uintptr_t)(arena+1);arena[0x42]=(BYTE)n;
            static const uint32_t bases[]={0,1,0xffffffffu,0x80000000u};
            put32(mapped+base_slot,n<1024?bases[n/256]:random32());
        }
        memcpy(before,arena,sizeof(arena));uint64_t got=weapons_original(index,a,r);
        memcpy(after,arena,sizeof(arena));memcpy(arena,before,sizeof(arena));
        uint64_t want=x->replacement_context(a,d->user,r);
        if(got!=want||memcmp(arena,after,sizeof(arena))) {
            fprintf(stderr,"FAIL: %s case %u pair %016llx/%016llx\n",d->name,n,(unsigned long long)got,(unsigned long long)want);return 1;
        }
    }
    put32(mapped+base_slot,saved);if(!limit)coverage_enable(0);current_case_valid=0;
    printf("PASS: %-20s %u cases; full pair, every signed-byte index, guards and all mask displacements -15..15\n",d->name,cases);return 0;
}

typedef struct weapon_record {uint32_t id,args[5],state[6];uint64_t result;} weapon_record;
static BYTE weapon_arena[4096],weapon_before[4096],weapon_after[4096];
static uint32_t weapon_tables[2][32],weapon_script[64],weapon_w,weapon_owner;
static weapon_record weapon_log[48];
static unsigned weapon_logged,weapon_next,weapon_case;
static uint32_t weapon_distance,weapon_radius;
static uint32_t weapon_take(void) {return weapon_script[weapon_next++%64];}
__attribute__((used,noinline)) static uint64_t __cdecl weapon_stub_impl(unsigned id,const uint32_t *args) {
    if(weapon_logged>=48){fail("weapon stub overflow");ExitProcess(1);}
    static const unsigned counts[]={1,2,5,2,1,3,1,5,4,5};
    weapon_record *record=weapon_log+weapon_logged++;memset(record,0,sizeof(*record));record->id=id;
    memcpy(record->args,args,counts[id]*4);
    record->state[0]=get32((BYTE *)(uintptr_t)weapon_w+0x30);
    record->state[1]=get32((BYTE *)(uintptr_t)weapon_w+0x44);
    record->state[2]=get16((BYTE *)(uintptr_t)weapon_w+0x7b);
    record->state[3]=get32((BYTE *)(uintptr_t)weapon_w+0x20);
    record->state[4]=get32(weapon_arena+0x7e0);
    record->state[5]=get16(mapped+0xa9afe);
    /* Stand-in for constructor/RNG/lifecycle state. Exact sequence is checked. */
    put32(weapon_arena+0x7e0,record->state[4]+1u);
    (void)es2_rng_next(weapon_arena+0x800+0x100*(id&1));
    uint32_t eax=weapon_take(),edx=weapon_take();
    if(id==0) {
        eax=(eax&0xffff0000u)|(uint16_t)weapon_radius;
        if(weapon_case%7==0)put32((BYTE *)(uintptr_t)args[0],(uint32_t)(uintptr_t)weapon_tables[1]);
        if(weapon_case%17==0)put16(mapped+0xa9afe,2);
    } else if(id==1) {
        eax=weapon_distance;
        if(weapon_case%11==0)put32(mapped+0xa9af8,(uint32_t)(uintptr_t)(weapon_arena+0x680));
    } else if(id==2||id==9) {
        BYTE *actor=(BYTE *)(uintptr_t)args[0];put32(actor+0x80,get32(actor+0x80)+args[1]);
        if(weapon_case%13==0)put16(mapped+0xa9afe,1);
    } else if(id==3) {
        BYTE *dst=(BYTE *)(uintptr_t)args[0],*src=(BYTE *)(uintptr_t)args[1];
        for(unsigned i=0;i<8;++i)dst[i]&=src[i];
    } else if(id==4) {
        put16((BYTE *)(uintptr_t)args[0]+1,(uint16_t)eax);
    } else if(id==5) {
        if(weapon_case%3==0)put32((BYTE *)(uintptr_t)weapon_w+0x1c,(uint32_t)(uintptr_t)(weapon_arena+0x280));
        if(weapon_case%5==0)put32((BYTE *)(uintptr_t)weapon_w+0x20,(uint32_t)(uintptr_t)(weapon_arena+0x310));
    } else if(id==6) {
        if(weapon_case%7==0)put32((BYTE *)(uintptr_t)weapon_w+0x20,(uint32_t)(uintptr_t)(weapon_arena+0x310));
        if(weapon_case%11==0)put16(weapon_arena+0x302,0xfffe);
    } else {
        if(weapon_case%3==0)eax=0; /* allocation failure is not a guard */
        put32(weapon_arena+0x7e4,eax);
        if(weapon_case%7==0)put32((BYTE *)(uintptr_t)weapon_w+0x18,(uint32_t)(uintptr_t)(weapon_arena+0x370));
    }
    record->result=((uint64_t)edx<<32)|eax;return record->result;
}
#define WEAPON_STUB(name,id) __attribute__((naked)) static void name(void) { __asm__ volatile( \
    "leal 4(%esp),%eax\n\tpushl %eax\n\tpushl $" #id "\n\tcalll _weapon_stub_impl\n\taddl $8,%esp\n\tmovl $0x76543210,%ecx\n\tret\n\t"); }
WEAPON_STUB(weapon_radius_stub,0)
WEAPON_STUB(weapon_distance_stub,1)
WEAPON_STUB(weapon_hit_stub,2)
WEAPON_STUB(weapon_hit_alt_stub,9)
WEAPON_STUB(weapon_merge_stub,3)
WEAPON_STUB(weapon_timer_stub,4)
WEAPON_STUB(weapon_prepare_stub,5)
WEAPON_STUB(weapon_owner_stub,6)
WEAPON_STUB(weapon_projectile_stub,7)
WEAPON_STUB(weapon_bullet_stub,8)
#undef WEAPON_STUB
static void weapon_reset(void){weapon_logged=weapon_next=0;memset(weapon_log,0,sizeof(weapon_log));}
static void weapon_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]) {
    weapon_case=n;
    for(unsigned i=0;i<64;++i)weapon_script[i]=random32();
    memset(weapon_arena,0xa5,sizeof(weapon_arena));
    weapon_arena[0x870]=weapon_arena[0x970]=0;weapon_arena[0x871]=weapon_arena[0x971]=17;
    uint32_t b=(uint32_t)(uintptr_t)weapon_arena;
    weapon_w=b+0x20;weapon_owner=b+0x120;
    for(unsigned i=0;i<2;++i) {
        weapon_tables[i][0x5c/4]=(uint32_t)(uintptr_t)weapon_radius_stub;
        weapon_tables[i][0x70/4]=(uint32_t)(uintptr_t)(i?weapon_hit_alt_stub:weapon_hit_stub);
        weapon_tables[i][0x38/4]=(uint32_t)(uintptr_t)weapon_owner_stub;
    }
    for(unsigned i=0;i<3;++i)r[i]=random32();
    memset(a,0,6*4);
    if(index==ES2_HOOK_INDEX_radius_hit) {
        static const uint32_t distances[]={0,1,1199,1200,1201,0xffffffffu,0x80000000u,0x7fffffffu};
        static const uint32_t radii[]={0,1,0xffff,0x7fff,0x8000};
        weapon_radius=n<1024?radii[(n/8)%5]:random32();
        weapon_distance=n<1024?distances[n%8]:random32();
        a[0]=b+0x500;a[1]=n<1024?distances[(n/40)%8]:random32();a[2]=random32();a[3]=b+0x100;
        a[4]=n%4?b+0x200:0;
        for(unsigned i=0;i<4;++i) {
            BYTE *o=weapon_arena+0x100+i*0x100;
            put32(o,(uint32_t)(uintptr_t)weapon_tables[0]);put32(o+0x45,b+0x700+0x20*i);
            put32(weapon_arena+0x714+0x20*i,(n+i)%6==0);
            put32(weapon_arena+0x600+i*4,b+0x100+i*0x100);
        }
        if(n%9==0)put32(weapon_arena+0x600+4*(n%4),0);
        put32(weapon_arena+0x610,0);
        put32(mapped+0xa9af8,b+0x600);
        put16(mapped+0xa9afe,n%17==0?0x8000:n%11==0?0x7fff:(uint16_t)((n%7)-2));
    } else {
        a[0]=weapon_w;a[1]=index==ES2_HOOK_INDEX_weapon_step?random32():weapon_owner;a[2]=(n&1)?0xabcd0000u:0x12340001u;
        BYTE *w=(BYTE *)(uintptr_t)weapon_w;
        put32(w+0x10,b+0x400);put32(w+0x14,b+0x450);put32(weapon_arena+0x420,b+0x460);
        static const uint16_t bounds[]={0,1,0x7fff,0x8000,0xffff,2,255,256};
        put16(weapon_arena+0x450,n<1024?bounds[n%8]:(uint16_t)random32());
        put16(weapon_arena+0x460,n<1024?(n%5==0?1:bounds[n%7+1]):(uint16_t)(random32()%65535+1));
        w[0x49]=(BYTE)(n%4==0);w[0x44]=(BYTE)(n%3!=0);
        for(unsigned i=0;i<16;++i)w[0x33+i]=(BYTE)random32();
        put32(w+0x1c,b+0x200);put32(w+0x20,b+0x300);put32(w+0x18,b+0x350);
        put32((BYTE *)(uintptr_t)weapon_owner,(uint32_t)(uintptr_t)weapon_tables[0]);
        put16(w+0x7b,n<1024?bounds[(n/8)%8]:(uint16_t)random32());
        put16(weapon_arena+0x238,n<1024?bounds[n%8]:(uint16_t)random32());
        put16(weapon_arena+0x2b8,n<1024?bounds[(n/16)%8]:(uint16_t)random32());
        put16(weapon_arena+0x300,(uint16_t)(n%3));put16(weapon_arena+0x302,(uint16_t)random32());
        put16(weapon_arena+0x310,(uint16_t)(n%2));put16(weapon_arena+0x312,(uint16_t)random32());
        weapon_arena[0x356]=(BYTE)(n%8-2);weapon_arena[0x376]=(BYTE)(n%9-3);
        if(index==ES2_HOOK_INDEX_weapon_step && n%9==0) {
            static const unsigned aliases[]={0x31,0x33,0x3b,0x44};
            put32(w+0x14,weapon_w+aliases[(n/9)%4]);w[0x44]=1;
        }
        if(index==ES2_HOOK_INDEX_weapon_fire && n%19==0)put32(w+0x1c,weapon_w+0x43u);

    }
}
static int weapon_radius_count_probes(unsigned index) {
    static uint32_t entries[32768];
    const uint16_t counts[]={0,0xffff,0x8000,0x7fff};
    uint32_t a[6],r[3];weapon_build(index,1,a,r);
    for(unsigned i=0;i<32768;++i)entries[i]=weapon_w;
    put32((BYTE *)(uintptr_t)weapon_w+0x45,(uint32_t)(uintptr_t)(weapon_arena+0x700));
    put32(weapon_arena+0x714,1); /* every candidate is excluded without calls */
    for(unsigned n=0;n<4;++n) {
        put32(mapped+0xa9af8,(uint32_t)(uintptr_t)entries);put16(mapped+0xa9afe,counts[n]);
        weapon_reset();uint64_t got=weapons_original(index,a,r);
        uint64_t want=weapons_radius_hit(a,mapped,r);
        if(got!=want||weapon_logged)return fail("signed radius count boundary");
    }
    puts("PASS: radius_hit counts 0/-1/-32768/32767, including 32767 nonnull entries");return 0;
}
static int weapon_division_probes(unsigned index) {
    uint32_t a[6],r[3];weapon_build(index,1,a,r);
    put16(weapon_arena+0x460,0);
    BYTE *w=(BYTE *)(uintptr_t)weapon_w;
    w[0x49]=1;weapon_reset();(void)weapons_original(index,a,r);
    w[0x49]=0;w[0x44]=0;weapon_reset();(void)weapons_original(index,a,r);
    w[0x44]=1;weapon_reset();expecting_divide=1;divide_site=0;
    if(!setjmp(divide_return)) {
        (void)weapons_original(index,a,r);expecting_divide=0;return fail("zero animation divisor did not fault");
    }
    if(divide_site!=0x40efedu)return fail("wrong animation divide site");
    puts("PASS: weapon_step zero-divisor bypasses and original IDIV fault at 40efed");return 0;
}
static int weapon_step_modes(unsigned index) {
    const unsigned nested[2]={ES2_HOOK_INDEX_mask_merge,ES2_HOOK_INDEX_timer_word_step};
    uint16_t scale=get16(mapped+0xd3b64);int status=0;
    for(unsigned modes=0;modes<9&&!status;++modes) {
        es2_hook installed[2]={{0}};
        for(unsigned i=0;i<2;++i) {
            es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
            o.mode=(es2_hook_mode)(i?modes/3:modes%3);
            if(!es2_hook_install_ex(installed+i,nested_descriptor(nested[i],&o,function_hook_extension(nested[i])),&o,function_hook_extension(nested[i])))return fail("weapon nested mode install");
        }
        for(unsigned n=0;n<256&&!status;++n) {
            uint32_t a[6],r[3];weapon_build(index,n,a,r);put16(mapped+0xd3b64,(uint16_t)random32());
            memcpy(weapon_before,weapon_arena,sizeof(weapon_arena));
            uint64_t got=weapons_original(index,a,r);memcpy(weapon_after,weapon_arena,sizeof(weapon_arena));memcpy(weapon_arena,weapon_before,sizeof(weapon_arena));
            uint64_t want=weapons_weapon_step(a,mapped,r);
            if(got!=want||memcmp(weapon_arena,weapon_after,sizeof(weapon_arena))) {
                fprintf(stderr,"FAIL: weapon_step mode %u case %u pair %016llx/%016llx\n",modes,n,(unsigned long long)got,(unsigned long long)want);status=1;
            }
        }
        for(unsigned i=0;i<2;++i) {
            if(installed[i].mismatches)status=fail("nested weapon compare mismatch");
            if(!es2_hook_uninstall(installed+i))return fail("nested weapon uninstall");
        }
    }
    put16(mapped+0xd3b64,scale);
    if(!status)puts("PASS: weapon_step 2304 states, all nine original/replaced/compare merge and timer combinations");
    return status;
}
static int weapons_graph_cases(unsigned index,unsigned limit) {
    const es2_hook_descriptor *d=function_hook_descriptor(index);
    const es2_hook_extension *x=function_hook_extension(index);
    const uint32_t rvas[]={0x9288c,0xf881,0x67a48,0xe788,0xa9c4,0xb43c};
    void *stubs[]={weapon_distance_stub,weapon_merge_stub,weapon_timer_stub,weapon_prepare_stub,weapon_projectile_stub,weapon_bullet_stub};
    BYTE saved[6][5];
    /* Do not patch an installed nested hook: preserve its stubbed entry for
     * this case and restore before the harness's uninstall/signature checks. */
    for(unsigned i=0;i<6;++i)if(actor_patch(rvas[i],stubs[i],saved[i],TRUE))return 1;
    uint32_t list=get32(mapped+0xa9af8);uint16_t count_before=get16(mapped+0xa9afe);
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_WEAPONS_CASES");
    if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n) {
        current_case=n;current_case_valid=1;if(!limit&&n==4096)coverage_enable(0);
        uint32_t a[6],r[3];weapon_build(index,n,a,r);
        memcpy(weapon_before,weapon_arena,sizeof(weapon_arena));
        uint32_t old_list=get32(mapped+0xa9af8);uint16_t old_count=get16(mapped+0xa9afe);
        weapon_reset();uint64_t got=weapons_original(index,a,r);
        unsigned calls=weapon_logged;weapon_record expected[48];memcpy(expected,weapon_log,sizeof(expected));
        memcpy(weapon_after,weapon_arena,sizeof(weapon_arena));
        uint32_t after_list=get32(mapped+0xa9af8);uint16_t after_count=get16(mapped+0xa9afe);
        memcpy(weapon_arena,weapon_before,sizeof(weapon_arena));put32(mapped+0xa9af8,old_list);put16(mapped+0xa9afe,old_count);
        weapon_reset();uint64_t previous[2]={radius_hit_counters[0].count,radius_hit_counters[1].count};
        uint64_t want=x->replacement_context(a,d->user,r);
        if(got!=want||calls!=weapon_logged||memcmp(expected,weapon_log,calls*sizeof(*expected))||memcmp(weapon_arena,weapon_after,sizeof(weapon_arena))||after_list!=get32(mapped+0xa9af8)||after_count!=get16(mapped+0xa9afe)) {
            unsigned diff=0;while(diff<calls&&diff<weapon_logged&&!memcmp(expected+diff,weapon_log+diff,sizeof(*expected)))++diff;
            fprintf(stderr,"FAIL: %s case %u pair %016llx/%016llx calls %u/%u first=%u\n",d->name,n,(unsigned long long)got,(unsigned long long)want,calls,weapon_logged,diff);
            if(diff<calls&&diff<weapon_logged)for(unsigned j=0;j<5;++j)fprintf(stderr," arg%u %08x/%08x\n",j,expected[diff].args[j],weapon_log[diff].args[j]);
            status=1;
        }
        if(index==ES2_HOOK_INDEX_radius_hit) {
            uint64_t observed[2]={0};for(unsigned i=0;i<calls;++i){if(expected[i].id==0)++observed[0];if(expected[i].id==2||expected[i].id==9)++observed[1];}
            for(unsigned i=0;i<2;++i)if(radius_hit_counters[i].count-previous[i]!=observed[i])status=fail("radius counter delta");
        }
    }
    if(!limit)coverage_enable(0);
    if(!limit&&!status&&index==ES2_HOOK_INDEX_weapon_step)status=weapon_division_probes(index);
    if(!limit&&!status&&index==ES2_HOOK_INDEX_radius_hit)status=weapon_radius_count_probes(index);
    put32(mapped+0xa9af8,list);put16(mapped+0xa9afe,count_before);
    for(unsigned i=0;i<6;++i)if(actor_patch(rvas[i],NULL,saved[i],FALSE))return 1;
    current_case_valid=0;if(status)return 1;
    if(!limit&&index==ES2_HOOK_INDEX_weapon_step&&weapon_step_modes(index))return 1;
    printf("PASS: %-20s %u cases; full pair, ordered full-slot calls/results, both RNG states, graph mutations and guards\n",d->name,cases);fflush(stdout);return 0;
}
static int weapons_round1_cases(unsigned index,unsigned limit) {
    return index<=ES2_HOOK_INDEX_mask_merge?weapons_leaf_cases(index,limit):weapons_graph_cases(index,limit);
}
/* B1: execute common preparation with real arithmetic and two synthetic
 * resource lookups. Stack poison establishes whether whole-state replacement
 * can choose a fresh local matrix buffer without an explicit padding rule. */
static BYTE weapon_prep_arena[1024];
static uint32_t weapon_prep_table[16];
static uint64_t __cdecl weapon_prep_lookup(uint32_t receiver,uint32_t selector) {
    (void)receiver;(void)selector;
    return (uint32_t)(uintptr_t)(weapon_prep_arena+0x300);
}
static uint64_t __cdecl weapon_prep_offset(uint32_t weapon) {
    (void)weapon;return (uint32_t)(uintptr_t)(weapon_prep_arena+0x380);
}
static int weapon_prepare_padding_probe(void) {
    BYTE saved[5],globals[50],first[50],state[1024];uint32_t values[2][2];
    uint32_t flag=get32(mapped+0x9ef34);
    memcpy(globals,mapped+0xa9834,sizeof(globals));
    if(actor_patch(0xf540,weapon_prep_offset,saved,TRUE))return 1;
    memset(weapon_prep_arena,0,sizeof(weapon_prep_arena));
    uint32_t b=(uint32_t)(uintptr_t)weapon_prep_arena,w=b+0x20,a=b+0x100;
    weapon_prep_table[0x20/4]=(uint32_t)(uintptr_t)weapon_prep_lookup;
    put32(weapon_prep_arena+0x38,b+0x180);put32(weapon_prep_arena+0x3c,b+0x200);
    put16(weapon_prep_arena+0x48,0x4000); /* local yaw, record and owner identity */
    put16(weapon_prep_arena+0x83,1024);put16(weapon_prep_arena+0x24c,17);
    put32(weapon_prep_arena+0x134,b+0x280);
    put32(weapon_prep_arena+0x284,b+0x2c0);put32(weapon_prep_arena+0x296,b+0x340);
    put32(weapon_prep_arena+0x2c0,(uint32_t)(uintptr_t)weapon_prep_table);
    memcpy(state,weapon_prep_arena,sizeof(state));
    put32(mapped+0x9ef34,0);
    for(unsigned i=0;i<2;++i) {
        memcpy(weapon_prep_arena,state,sizeof(state));memcpy(mapped+0xa9834,globals,sizeof(globals));
        es2_probe_call((void *)(mapped+0xe788),w,a,0,i?0xa7a7a7a7u:0x31313131u,values[i]);
        BYTE expected[50],expected_state[1024],scratch[128]={0};
        memcpy(expected,mapped+0xa9834,50);memcpy(expected_state,weapon_prep_arena,1024);
        if(!i)memcpy(first,expected,sizeof(first));
        memcpy(weapon_prep_arena,state,sizeof(state));memcpy(mapped+0xa9834,globals,sizeof(globals));
        es2_weapon_graph g=weapons_graph(mapped,scratch,ES2_HOOK_INDEX_weapon_prepare);g.padding=i?0xa7:0x31;g.composed_padding=g.padding;
        uint32_t entry[3]={0,0,0};uint64_t actual=es2_weapon_prepare_registers(&math_native_memory,&g,w,a,entry);
        if(actual!=((uint64_t)values[i][1]<<32|values[i][0]) || memcmp(expected,mapped+0xa9834,50) || memcmp(expected_state,weapon_prep_arena,1024))
            return fail("weapon_prepare explicit padding differs under real transforms");
    }
    /* Installed adapter must use the same original-frame residue. */
    for(unsigned fast=0;fast<2;++fast)for(unsigned i=0;i<2;++i) {
        BYTE expected[50],expected_state[1024];uint32_t expected_pair[2],actual_pair[2];
        memcpy(weapon_prep_arena,state,sizeof(state));memcpy(mapped+0xa9834,globals,50);
        es2_probe_call(mapped+0xe788,w,a,0,i?0xa7a7a7a7u:0x31313131u,expected_pair);
        memcpy(expected,mapped+0xa9834,50);memcpy(expected_state,weapon_prep_arena,1024);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);
        if(!weapons_install_hook(&h,function_hook_descriptor(ES2_HOOK_INDEX_weapon_prepare),&o,function_hook_extension(ES2_HOOK_INDEX_weapon_prepare)))return fail("prepare residue install");
        memcpy(weapon_prep_arena,state,sizeof(state));memcpy(mapped+0xa9834,globals,50);
        es2_probe_call(mapped+0xe788,w,a,0,i?0xa7a7a7a7u:0x31313131u,actual_pair);
        int bad=memcmp(expected_pair,actual_pair,8)||memcmp(expected,mapped+0xa9834,50)||memcmp(expected_state,weapon_prep_arena,1024)||h.calls!=1;
        if(!es2_hook_uninstall(&h))return fail("prepare residue uninstall");if(bad)return fail("prepare installed stack poison");
    }
    es2_hook_set_fast_path(TRUE);
    unsigned differences=0,offset=0;
    for(unsigned i=0;i<sizeof(first);++i)if(first[i]!=mapped[0xa9834+i]){++differences;offset=i;}
    int status=0;
    if(differences!=1||offset!=19||first[19]!=0x31||mapped[0xa9834+19]!=0xa7||memcmp(values[0],values[1],sizeof(values[0])))
        status=fail("preparation stack-padding hypothesis not reproduced");
    else puts("PASS: 40e788 and core exact under 31/a7 poison including matrix byte 19; only 4a9847 differs between poisons");
    memcpy(mapped+0xa9834,globals,sizeof(globals));put32(mapped+0x9ef34,flag);
    if(actor_patch(0xf540,NULL,saved,FALSE))return 1;
    return status;
}
