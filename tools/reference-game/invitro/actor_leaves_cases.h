/* actor_leaves group: the 33 hookable actor slot leaves
 * (docs/reverse-engineering/vtables.md, "actor slot leaves"). No stubs: the
 * three leaves with a game call reach the real part_condition 40db2c or
 * multiply_q10 47e0b0 (pure), the others call nothing.
 *
 * Direct cases run the original body (four argument slots, EAX/EDX/ECX at
 * entry from the case; EAX/EDX/ECX after it captured), the adapter (pair)
 * and the core (pair and its ECX) over an 8 KiB arena: objects with random
 * bytes and the fields each leaf reads biased to the edges (configuration
 * words 0/FFFFh/8000h, the record word at 2, the byte +99h, part counts
 * -3..12 and 8000h, start indices negative, inside, at and beyond the
 * count, 16-bit angles at the 7000h boundary, part-table pointers placed so
 * that any 32-bit index or 16-bit word lands in the arena). Compared: the
 * pair, ECX (core; and the original's when the hook is not installed), the
 * arena after each run and the callee counter delta. Then installed 31/a7
 * poison probes, a real-callee pass for the three callers (part_condition
 * or multiply_q10 nested in rotating modes, the hook original or replaced)
 * and the compare probe (compare_probes.h; all 33 accept compare).
 * mech_next_part starts below 7 only: from 7 on the walk reads the image's
 * words behind the seven-slot table 49a060 (0BCE0h at index 8) and leaves
 * the record by 34 KiB, which no caller passes (the weapon's slot word
 * +7Dh holds -1..6). Identifiers: alv_/ALV_. */
enum { ALV_SIZE=0x2000, ALV_O=0x100, ALV_C=0x600, ALV_B=0x680, ALV_TABLE=0x800, ALV_FAR=0xc00, ALV_OUT=0x1000,
    ALV_P=0x1100, ALV_S=0x1180, ALV_T1=0x1200, ALV_T2=0x1400, ALV_A1=0x1500, ALV_A2=0x1600 };
#define ALV_BASE ((uint32_t)(uintptr_t)alv_arena)
static BYTE alv_arena[ALV_SIZE],alv_before[ALV_SIZE],alv_after[ALV_SIZE];
static unsigned alv_hits[4];
static BYTE *alv_at(uint32_t offset){return alv_arena+offset;}
static uint16_t alv_word(const uint16_t *values,unsigned count){return random32()%4?values[random32()%count]:(uint16_t)random32();}
static int alv_is(unsigned index,unsigned a,unsigned b,unsigned c){return index==a||index==b||index==c;}
/* part_condition's record P (O+206h or O+200h): two counts, value arrays
 * and maxima; the maxima are positive so the IDIV of the real callee never
 * faults. */
static void alv_condition(uint32_t p){
    const uint32_t b=ALV_BASE;
    put32((BYTE *)(uintptr_t)p,b+ALV_A2);put32((BYTE *)(uintptr_t)p+4,b+ALV_A1);put32((BYTE *)(uintptr_t)p+0xc,b+ALV_S);
    put32(alv_at(ALV_S),b+ALV_T1);put32(alv_at(ALV_S+4),b+ALV_T2);
    unsigned c1=random32()%7,c2=random32()%7;if(!c1&&!c2)c1=1;
    put16((BYTE *)(uintptr_t)p+0x18,(uint16_t)c1);put16((BYTE *)(uintptr_t)p+0x1a,(uint16_t)c2);
    for(unsigned i=0;i<8;++i){
        put16(alv_at(ALV_T1+18*i),(uint16_t)(1+random32()%3000));put16(alv_at(ALV_T2+2*i),(uint16_t)(1+random32()%3000));
        put16(alv_at(ALV_A1+2*i),(uint16_t)(random32()%3?random32()%3000:random32()));
        put16(alv_at(ALV_A2+2*i),(uint16_t)(random32()%3?random32()%3000:random32()));
    }
}
/* One state for hook index (case n): a[0..3] the argument slots, r the
 * entry EAX/EDX/ECX. */
static void alv_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    static const uint16_t edges[]={0,1,2,0x7fff,0x8000,0xffff,0xfffe,0x3800,0x7000};
    const uint32_t b=ALV_BASE;
    random_bytes(alv_arena,ALV_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    for(unsigned i=0;i<4;++i)a[i]=random32();
    const uint32_t o=b+ALV_O;a[0]=o;
    BYTE *O=alv_at(ALV_O),*C=alv_at(ALV_C);
    put32(O+0x1f2,b+ALV_C);
    if(index==ES2_HOOK_INDEX_base_sight_radius){
        const unsigned k=random32()%8;
        put16(C+6,k==0?0:alv_word(edges,9));
        put16(C+4,k==1||random32()%4==0?0xffff:alv_word(edges,9));
        put32(O+0x205,b+ALV_B);
        put16(alv_at(ALV_B+5),k==2?2:k==3?1:alv_word(edges,9));
        O[0x99]=(BYTE)(random32()%2?0:random32()%3?1:random32());
        if(n<4){put16(C+6,0x8000);put16(C+4,0);put16(alv_at(ALV_B+5),(uint16_t)(n<2?1:0x7fff));O[0x99]=(BYTE)(n&1);}
    } else if(alv_is(index,ES2_HOOK_INDEX_base_part_word,ES2_HOOK_INDEX_mech_part_word,ES2_HOOK_INDEX_mech_mount_word)){
        /* Any index (32-bit) or word (16-bit): the table pointer is placed
         * so that the read lands at a random arena word. */
        const uint32_t target=b+ALV_TABLE+2u*(random32()%0x100);
        if(index==ES2_HOOK_INDEX_mech_mount_word){
            static const uint16_t words[]={0,1,2,0x7fff,0x8000,0xffff};
            a[1]=(random32()%2?random32()&0xffff0000u:0)|(n<6?words[n]:alv_word(words,6));
            put32(O+0x202,target-2u*(uint32_t)(int32_t)(int16_t)a[1]-0x0au);
        } else {
            static const uint32_t indices[]={0,1,0xffffffffu,0x7fffffffu,0x80000000u,6};
            a[1]=n<6?indices[n]:random32()%2?random32()%16:random32();
            put32(O+(index==ES2_HOOK_INDEX_base_part_word?0x201:0x20e),target-2u*a[1]);
        }
    } else if(index==ES2_HOOK_INDEX_base_next_part){
        a[2]=b+ALV_OUT+4u*(random32()%4);
        const unsigned k=random32()%4;
        int32_t count=k==0?(int32_t)(random32()%13)-3:(int32_t)(1+random32()%12);
        if(random32()%16==0)count=(int16_t)(0x8000u|random32());
        put16(C+0x12,(uint16_t)count);
        uint32_t start=random32()%3==0?(uint32_t)(-(int32_t)(1+random32()%5)):count>0?random32()%(uint32_t)count:random32()%4;
        if(random32()%8==0)start=random32()%2?0x80000000u|random32():random32()%40;
        if(random32()%16==0&&count>0)start=(uint32_t)count+random32()%3;
        a[1]=start;
        const uint32_t s=(int32_t)start<0?0u:start;
        if(count>0&&s<(uint32_t)count){
            /* A table of count words, zero with probability 1/2 (or all). */
            const int none=random32()%6==0;
            for(int i=0;i<count;++i)put16(alv_at(ALV_TABLE+2*i),(uint16_t)(none||random32()%2?0:1+random32()%0xfffe));
            put32(O+0x201,b+ALV_TABLE);
        } else {
            /* The walk passes the count: words from s+1 on, a nonzero one
             * within the first eight. */
            for(unsigned i=0;i<16;++i)put16(alv_at(ALV_FAR+2*i),(uint16_t)(random32()%2?0:1+random32()%0xfffe));
            put16(alv_at(ALV_FAR+2*(random32()%8)),(uint16_t)(1+random32()%0xfffe));
            put32(O+0x201,b+ALV_FAR-2u*(s+1u));
        }
    } else if(index==ES2_HOOK_INDEX_mech_next_part){
        a[2]=b+ALV_OUT+4u*(random32()%4);
        static const uint32_t starts[]={0,6,0xffffffffu,0x80000000u,3};
        a[1]=n<5?starts[n]:random32()%4==0?0x80000000u|random32():random32()%7;
        put32(O+0x20e,b+ALV_TABLE);
        const int none=random32()%6==0;
        for(unsigned i=0;i<12;++i)put16(alv_at(ALV_TABLE+2*i),(uint16_t)(none||random32()%3?0:1+random32()%0xfffe));
    } else if(index==ES2_HOOK_INDEX_actor_next_part){
        a[2]=b+ALV_OUT+4u*(random32()%4);
    } else if(index==ES2_HOOK_INDEX_actor_in_front){
        put16(O+0x10,alv_word(edges,9));
        const uint32_t d=random32()%4==0?random32():(uint32_t)(0x7000u-0x3800u+(random32()%5)-2u+(random32()%2?0u:0x9000u));
        a[1]=(random32()&0xffff0000u)|((d+get16(O+0x10))&0xffffu);
    } else if(index==ES2_HOOK_INDEX_mech_condition||index==ES2_HOOK_INDEX_flyer_condition){
        alv_condition(o+(index==ES2_HOOK_INDEX_mech_condition?0x206u:0x200u));
    } else if(index==ES2_HOOK_INDEX_flyer_target_scale){
        put16(O+0x233,alv_word(edges,9));
    } else if(index==ES2_HOOK_INDEX_base_radius||index==ES2_HOOK_INDEX_mech_radius||index==ES2_HOOK_INDEX_mech_sight_radius){
        if(random32()%4==0)put32(O+0x1f2,b+ALV_C+random32()%64);
    }
}
/* Calls target with four argument slots and EAX/EDX/ECX = r; out receives
 * EAX, EDX and ECX after the call. */
__attribute__((naked,noinline)) static void alv_invoke(void *target,const uint32_t *a,const uint32_t *r,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl 12(%ebp),%esi\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static uint64_t alv_core(unsigned index,const uint32_t *a,const uint32_t *r,uint32_t *ecx){
    const es2_guest_memory *m=&math_native_memory;
    es2_weapon_graph g=weapons_graph(mapped,NULL,index);
#define ALV_C0(name) if(index==ES2_HOOK_INDEX_##name)return es2_##name##_registers(r,ecx);
#define ALV_C1(name) if(index==ES2_HOOK_INDEX_##name)return es2_##name##_registers(m,a[0],r,ecx);
#define ALV_C2(name) if(index==ES2_HOOK_INDEX_##name)return es2_##name##_registers(m,a[0],a[1],r,ecx);
#define ALV_CG(name) if(index==ES2_HOOK_INDEX_##name)return es2_##name##_registers(m,&g,a[0],r,ecx);
    ALV_C0(base_in_front) ALV_C0(base_decide) ALV_C0(actor_armour) ALV_C0(actor_target_scale) ALV_C0(actor_angle)
    ALV_C0(actor_order) ALV_C0(actor_keys) ALV_C0(actor_aim_node) ALV_C0(actor_radius) ALV_C0(actor_sight_radius)
    ALV_C0(actor_move) ALV_C0(actor_engage_roll) ALV_C0(actor_mount_word) ALV_C0(actor_part_word) ALV_C0(flyer_keys)
    ALV_C0(object_zero)
    ALV_C1(gun_angle) ALV_C1(base_radius) ALV_C1(base_sight_radius) ALV_C1(mech_angle) ALV_C1(mech_radius)
    ALV_C1(mech_sight_radius) ALV_C1(actor_mark)
    ALV_C2(base_part_word) ALV_C2(mech_part_word) ALV_C2(mech_mount_word) ALV_C2(actor_in_front)
    ALV_CG(mech_condition) ALV_CG(flyer_condition) ALV_CG(flyer_target_scale)
#undef ALV_C0
#undef ALV_C1
#undef ALV_C2
#undef ALV_CG
    if(index==ES2_HOOK_INDEX_base_next_part)return es2_base_next_part_registers(m,a[0],a[1],a[2],r,ecx);
    if(index==ES2_HOOK_INDEX_mech_next_part)return es2_mech_next_part_registers(m,(uint32_t)(uintptr_t)mapped,a[0],a[1],a[2],r,ecx);
    return es2_actor_next_part_registers(m,a[2],r,ecx);
}
static uint64_t alv_counter(unsigned index){
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);return count?c[0].count:0;
}
static int alv_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    uint32_t want[3],got[3];unsigned calls;weapons_graph_counters(index,&calls);
    memcpy(alv_before,alv_arena,ALV_SIZE);
    alv_invoke(mapped+d->rva,a,r,want);
    memcpy(alv_after,alv_arena,ALV_SIZE);memcpy(alv_arena,alv_before,ALV_SIZE);
    const uint64_t counted=alv_counter(index);
    const uint64_t pair=x->replacement_context(a,d->user,r);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);
    const int adapter_arena=memcmp(alv_after,alv_arena,ALV_SIZE)!=0;
    const uint64_t delta=alv_counter(index)-counted;
    memcpy(alv_arena,alv_before,ALV_SIZE);
    const uint64_t core=alv_core(index,a,r,&got[2]);
    /* With the hook installed (integration pass) the "original" run is the
       hook, whose ECX is the framework's; ECX is compared otherwise. */
    const int ecx_known=!rng_hooks[index].installed;
    if(want[0]!=got[0]||want[1]!=got[1]||core!=pair||(ecx_known&&want[2]!=got[2])||adapter_arena||
       memcmp(alv_after,alv_arena,ALV_SIZE)||delta!=(calls?1u:0u)){
        fprintf(stderr,"FAIL: %s case %u eax %08x/%08x edx %08x/%08x ecx %08x/%08x core %016llx arena %d/%d calls %llu args %08x %08x %08x\n",
            d->name,n,want[0],got[0],want[1],got[1],want[2],got[2],(unsigned long long)core,adapter_arena,
            memcmp(alv_after,alv_arena,ALV_SIZE)!=0,(unsigned long long)delta,a[0],a[1],a[2]);
        for(unsigned j=0;j<ALV_SIZE;++j)if(alv_after[j]!=alv_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,alv_after[j],alv_arena[j]);break;}
        return 1;
    }
    /* Path statistics of the walks and the sight radius (informational). */
    if(index==ES2_HOOK_INDEX_base_sight_radius)++alv_hits[want[1]==1?2:want[0]==0?0:1];
    if(index==ES2_HOOK_INDEX_base_next_part||index==ES2_HOOK_INDEX_mech_next_part)++alv_hits[want[0]==0xffffffffu?0:1];
    return 0;
}
static int alv_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],want[2],got[2],pattern=poison?0xa7a7a7a7u:0x31313131u;alv_build(index,n,a,r);
        BYTE expected_fpu[108],actual_fpu[108];
        memcpy(alv_before,alv_arena,ALV_SIZE);
        sg_poison_call(mapped+d->rva,a,pattern,want);es2_hook_fpu_save(expected_fpu);
        memcpy(alv_after,alv_arena,ALV_SIZE);memcpy(alv_arena,alv_before,ALV_SIZE);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("actor_leaves poison install");
        sg_poison_call(mapped+d->rva,a,pattern,got);es2_hook_fpu_save(actual_fpu);
        if(want[0]!=got[0]||want[1]!=got[1]||memcmp(alv_after,alv_arena,ALV_SIZE)||
           memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1){
            fprintf(stderr,"FAIL: %s case %u (installed %s) pair %08x%08x/%08x%08x\n",d->name,n,fast?"fast":"preserving",want[1],want[0],got[1],got[0]);
            status=1;
        }
        if(!es2_hook_uninstall(&h))return fail("actor_leaves poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair, arena\n",d->name,total);
    return status;
}
/* Real-callee pass of the three callers: the nested hook (part_condition or
 * multiply_q10) in original, replaced and compare mode (replaced where it
 * refuses compare), this hook original or replaced, against the unhooked
 * original on the same state: pair and arena. */
static int alv_real_pass(unsigned index){
    const es2_hook_descriptor *d=function_hook_descriptor(index);
    const unsigned nested=index==ES2_HOOK_INDEX_flyer_target_scale?ES2_HOOK_INDEX_multiply_q10:ES2_HOOK_INDEX_part_condition;
    int status=0;unsigned total=0;uint64_t entered=0,hooked=0,replaced=0;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<256&&!status;++n){
        uint32_t a[6],r[3],want[2],got[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;alv_build(index,n,a,r);
        memcpy(alv_before,alv_arena,ALV_SIZE);
        sg_poison_call(mapped+d->rva,a,pattern,want);
        memcpy(alv_after,alv_arena,ALV_SIZE);memcpy(alv_arena,alv_before,ALV_SIZE);
        es2_hook hk[2];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        const es2_hook_extension *e=function_hook_extension(nested);o.mode=(es2_hook_mode)(mode%3);
        if((!e||e->compare_unsupported)&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk,function_hook_descriptor(nested),&o,e))return fail("actor_leaves nested install");
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+1,d,&o,function_hook_extension(index)))return fail("actor_leaves real install");
        sg_poison_call(mapped+d->rva,a,pattern,got);
        if(memcmp(want,got,8)||memcmp(alv_after,alv_arena,ALV_SIZE)){
            fprintf(stderr,"FAIL: %s real case %u mode %u pair %08x%08x/%08x%08x\n",d->name,n,mode,want[1],want[0],got[1],got[0]);status=1;
        }
        entered+=hk[0].calls;hooked+=hk[1].calls;replaced+=mode>=3;
        if(hk[0].mismatches)status=fail("actor_leaves nested compare mismatch");
        if(!es2_hook_uninstall(hk+1)||!es2_hook_uninstall(hk))return fail("actor_leaves real uninstall");
        ++total;
    }
    if(!status&&hooked!=replaced){fprintf(stderr,"FAIL: %s real pass entered %llu of %llu\n",d->name,(unsigned long long)hooked,(unsigned long long)replaced);status=1;}
    if(!status&&!entered){fprintf(stderr,"FAIL: %s real callee never entered\n",d->name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases, %s nested in rotating modes (entered %llu), hook original/replaced, 31/a7 stack, pair and arena\n",
        d->name,total,function_hook_descriptor(nested)->name,(unsigned long long)entered);
    return status;
}
static void alv_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){alv_build(index,n,a,r);}
static void alv_compare_state(BYTE *s,int restore){if(restore)memcpy(alv_arena,s,ALV_SIZE);else memcpy(s,alv_arena,ALV_SIZE);}
static int actor_leaves_cases(unsigned index,unsigned limit){
    current_function=function_hook_descriptor(index)->name;
    if(limit&&compare_probe_installed(index))return compare_probe_run(index,limit,alv_compare_build,alv_compare_state,ALV_SIZE);
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_ACTOR_LEAVES_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;memset(alv_hits,0,sizeof(alv_hits));if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];alv_build(index,n,a,r);status=alv_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    if(!limit&&!status)status=alv_installed_probes(index,64);
    if(!limit&&!status&&(index==ES2_HOOK_INDEX_mech_condition||index==ES2_HOOK_INDEX_flyer_condition||index==ES2_HOOK_INDEX_flyer_target_scale))
        status=alv_real_pass(index);
    if(!limit&&!status)status=compare_probe_run(index,0,alv_compare_build,alv_compare_state,ALV_SIZE);
    if(!status)printf("PASS: %-20s %u cases (paths %u/%u/%u); full return pair, ECX (core), arena, callee calls\n",
        function_hook_descriptor(index)->name,cases,alv_hits[0],alv_hits[1],alv_hits[2]);
    return status;
}
