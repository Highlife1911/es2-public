/* slot_leaves group: the six remaining actor slot methods
 * (docs/reverse-engineering/vtables.md, "Actor slot leaves").
 * Identifiers: slv_/SLV_ (the five empty or constant bodies) and slr_/SLR_ (the
 * radial hit).
 *
 * The five bodies (actor_contact 4127f8, actor_part_position 411a3c,
 * actor_part_facing 411a44, actor_kill 411b2c, actor_link_position 411b34)
 * touch no memory: direct cases run the original body (four argument
 * slots, random EAX/EDX/ECX/EBX at entry, edge values first), the adapter
 * (pair) and the core (pair and its ECX) and compare EAX, EDX and the
 * original's ECX (the entry ECX); then installed 31/a7 poison probes
 * (fast and preserving dispatch, nonempty x87, full pair) and the compare
 * probes (compare_probes.h), whose original side is the relocated
 * trampoline: the five-byte bodies run through the trampoline's own RET.
 *
 * actor_radial_hit 411b3c: direct cases run the original body and the
 * core against logging stubs for its three game calls: the radius slot +5Ch
 * and the part slot +74h (two arena vtables with different stubs, so a
 * wrong table or a missing reread shows) and dist_gate 49288c (patched over
 * the original). Each stub logs its id, arguments and entry EAX/EDX/ECX in
 * order and answers a planned value; the second stub (dist_gate) and the
 * first may swap the object's vtable pointer, which the original rereads
 * for the tail call. Compared: EAX, EDX, the original's ECX (the tail
 * stub's, 0 as flyer_part's), the arena, the ordered log and the callee
 * counter deltas. Then installed 31/a7 poison probes through the entry shim
 * and a real-callee pass: the real dist_gate (nested hook in rotating
 * modes) and the real radius slot body 411aa4 (with its hook, same
 * rotation), the part slot a stub, the radial hit hook original or
 * replaced. */
enum { SLV_SIZE=0x100 };
static BYTE slv_arena[SLV_SIZE];
/* Calls target with n argument slots and EAX/EDX/ECX/EBX from regs; with
 * pattern != 0 the 4 KiB below are filled first. EAX/EDX/ECX after RET. */
__attribute__((naked,noinline)) static void slv_invoke(void *target,const uint32_t *a,unsigned n,const uint32_t regs[4],uint32_t pattern,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "cmpl $0,24(%ebp)\n\tje 1f\n\tsubl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 24(%ebp),%eax\n\tmovl $1024,%ecx\n\tcld\n\trep stosl\n\taddl $4096,%esp\n\t"
        "1: movl 12(%ebp),%esi\n\tmovl 16(%ebp),%ecx\n\t"
        "2: testl %ecx,%ecx\n\tjz 3f\n\tdecl %ecx\n\tpushl (%esi,%ecx,4)\n\tjmp 2b\n\t"
        "3: movl 20(%ebp),%esi\n\tmovl (%esi),%eax\n\tmovl 4(%esi),%edx\n\tmovl 8(%esi),%ecx\n\tmovl 12(%esi),%ebx\n\tcall *8(%ebp)\n\t"
        "movl 28(%ebp),%ebx\n\tmovl %eax,(%ebx)\n\tmovl %edx,4(%ebx)\n\tmovl %ecx,8(%ebx)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static void slv_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    static const uint32_t edges[]={0,0xffffffffu,0x80000000u,0x7fffffffu,0xffffu,0x10000u,1,0xffff0000u};
    (void)index;
    random_bytes(slv_arena,SLV_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=n<8?edges[(n+i)%8]:random32();
    for(unsigned i=0;i<4;++i)a[i]=random32();
}
static uint64_t slv_core(unsigned index,const uint32_t *r,uint32_t *ecx){
    return index==ES2_HOOK_INDEX_actor_part_facing?es2_actor_no_part_registers(r,ecx):es2_actor_empty_registers(r,ecx);
}
static int slv_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    uint32_t want[3],got[3],regs[4]={r[0],r[1],r[2],random32()};
    slv_invoke(mapped+d->rva,a,4,regs,0,want);
    const uint64_t pair=x->replacement_context(a,d->user,r);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);
    const uint64_t core=slv_core(index,r,&got[2]);
    /* With the hook installed (integration pass) the "original" run is the
       hook, whose ECX is the framework's entry ECX: the same value. */
    if(want[0]!=got[0]||want[1]!=got[1]||want[2]!=got[2]||core!=pair||want[2]!=r[2]){
        fprintf(stderr,"FAIL: %s case %u eax %08x/%08x edx %08x/%08x ecx %08x/%08x core %016llx entry %08x %08x %08x\n",d->name,n,
            want[0],got[0],want[1],got[1],want[2],got[2],(unsigned long long)core,r[0],r[1],r[2]);
        return 1;
    }
    return 0;
}
static int slv_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],want[3],got[3],pattern=poison?0xa7a7a7a7u:0x31313131u,regs[4]={pattern,pattern,pattern,pattern};
        BYTE expected_fpu[108],actual_fpu[108];slv_build(index,n,a,r);
        slv_invoke(mapped+d->rva,a,4,regs,pattern,want);es2_hook_fpu_save(expected_fpu);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("slot_leaves poison install");
        slv_invoke(mapped+d->rva,a,4,regs,pattern,got);es2_hook_fpu_save(actual_fpu);
        if(memcmp(want,got,12)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1){
            fprintf(stderr,"FAIL: %s case %u (installed %s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x\n",d->name,n,fast?"fast":"preserving",
                want[0],got[0],want[1],got[1],want[2],got[2]);
            status=1;
        }
        if(!es2_hook_uninstall(&h))return fail("slot_leaves poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair (EAX, EDX, ECX)\n",d->name,total);
    return status;
}
static void slv_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){slv_build(index,n,a,r);}
static void slv_compare_state(BYTE *s,int restore){if(restore)memcpy(slv_arena,s,SLV_SIZE);else memcpy(s,slv_arena,SLV_SIZE);}
static int slv_leaf_cases(unsigned index,unsigned limit){
    if(limit&&compare_probe_installed(index))return compare_probe_run(index,limit,slv_compare_build,slv_compare_state,SLV_SIZE);
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_SLOT_LEAVES_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];slv_build(index,n,a,r);status=slv_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    if(!limit&&!status)status=slv_installed_probes(index,64);
    if(!limit&&!status)status=compare_probe_run(index,0,slv_compare_build,slv_compare_state,SLV_SIZE);
    if(!status)printf("PASS: %-20s %u cases; EAX, EDX and the original's ECX (= entry ECX), core pair, compare probes\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}

/* ---- actor_radial_hit 411b3c ---- */
typedef struct slr_record { uint32_t id,args[4],regs[3],eax,edx; } slr_record;
enum { SLR_SIZE=0x1000, SLR_O=0x100, SLR_VT1=0x400, SLR_VT2=0x500, SLR_P=0x600, SLR_MAX=8, SLR_IDS=5 };
#define SLR_BASE ((uint32_t)(uintptr_t)slr_arena)
static BYTE slr_arena[SLR_SIZE],slr_before[SLR_SIZE],slr_after[SLR_SIZE];
static slr_record slr_log[SLR_MAX];
static unsigned slr_calls;
static int slr_real;
static uint32_t slr_stream;
__attribute__((used)) static uint32_t slr_ecx;
/* The plan the stubs answer from: the radius slot's EAX, dist_gate's EAX,
 * the tail call's EAX/EDX (its ECX is 0), the seed of the other registers
 * and whether a stub swaps the object's vtable. */
static struct { uint32_t radius_eax,dist,tail_eax,tail_edx,seed; unsigned swap_after_radius,swap_after_dist; } slr_plan;
/* Stub ids: 0 radius slot (table 1), 1 dist_gate, 2 part slot (table 1),
 * 3 radius slot (table 2), 4 part slot (table 2). */
static const uint32_t slr_addresses[SLR_IDS]={0,0x49288c,0,0,0};
static const unsigned slr_slots[SLR_IDS]={0x5c,0,0x74,0x5c,0x74};
static const unsigned slr_counts[SLR_IDS]={1,2,4,1,4};
static uint32_t slr_next(void){
    uint32_t z=slr_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static int slr_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static void slr_swap(void){
    const uint32_t b=SLR_BASE,now=get32(slr_arena+SLR_O);
    put32(slr_arena+SLR_O,now==b+SLR_VT1?b+SLR_VT2:b+SLR_VT1);
}
__attribute__((used,noinline)) static uint64_t __cdecl slr_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(slr_calls==SLR_MAX){fail("radial hit synthetic call overflow");ExitProcess(1);}
    slr_record *v=slr_log+slr_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,4*slr_counts[id]);memcpy(v->regs,regs,12);
    for(unsigned j=0;j<3;++j)if(slr_stack(v->regs[j]))v->regs[j]=0;
    for(unsigned j=0;j<slr_counts[id];++j)if(slr_stack(v->args[j]))v->args[j]=0;
    uint32_t eax,edx=slr_next(),ecx=slr_next();
    if(slr_slots[id]==0x74){eax=slr_plan.tail_eax;edx=slr_plan.tail_edx;ecx=0;}   /* flyer_part leaves ECX = 0 */
    else if(id==1){eax=slr_plan.dist;if(slr_plan.swap_after_dist)slr_swap();}
    else{eax=slr_plan.radius_eax;if(slr_plan.swap_after_radius)slr_swap();}
    slr_ecx=ecx;v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
/* Pushes the entry EAX/EDX/ECX for the record before the slot pointer. */
#define SLR_STUB(n) __attribute__((naked)) static void slr_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _slr_stub_impl\n\taddl $24,%esp\n\tmovl _slr_ecx,%ecx\n\tret\n\t");}
SLR_STUB(0) SLR_STUB(1) SLR_STUB(2) SLR_STUB(3) SLR_STUB(4)
#undef SLR_STUB
static void *slr_stubs[SLR_IDS]={slr_stub_0,slr_stub_1,slr_stub_2,slr_stub_3,slr_stub_4};
static void slr_reset(void){slr_calls=0;slr_stream=slr_plan.seed;}
/* The core's quotient and divisor, to keep the original's IDIV from
 * faulting (zero divisor, INT_MIN / -1). */
static int slr_fault(uint32_t damage,uint32_t reach,uint32_t radius_eax,uint32_t dist){
    const uint32_t r16=(uint32_t)(int32_t)(int16_t)reach,radius=(uint32_t)(int32_t)(int16_t)radius_eax;
    const uint32_t scaled=(r16-(dist-radius))*(uint32_t)(int32_t)(int16_t)damage;
    return r16==0||(r16==0xffffffffu&&scaled==0x80000000u);
}
static void slr_build(unsigned n,uint32_t a[6],uint32_t r[3],int real){
    static const uint16_t words[]={0,1,2,0x7fff,0x8000,0xffff,0xfffe,0x100,0x64,0xfff8};
    const uint32_t b=SLR_BASE;BYTE *o=slr_arena+SLR_O;
    random_bytes(slr_arena,SLR_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    slr_plan.seed=random32();
    put32(o,b+(random32()%4?SLR_VT1:SLR_VT2));
    put32(slr_arena+SLR_VT1+0x5c,real?(uint32_t)(uintptr_t)(mapped+0x11aa4):(uint32_t)(uintptr_t)slr_stubs[0]);
    put32(slr_arena+SLR_VT2+0x5c,real?(uint32_t)(uintptr_t)(mapped+0x11aa4):(uint32_t)(uintptr_t)slr_stubs[3]);
    put32(slr_arena+SLR_VT1+0x74,(uint32_t)(uintptr_t)slr_stubs[2]);put32(slr_arena+SLR_VT2+0x74,(uint32_t)(uintptr_t)slr_stubs[4]);
    slr_plan.radius_eax=(random32()%2?random32()&0xffff0000u:0)|(random32()%2?words[random32()%10]:(random32()&0xffffu));
    slr_plan.dist=n<8?(const uint32_t[]){0,1,100,0x7fffffffu,0x80000000u,0xffffffffu,0x7fffu,0x8000u}[n]:
        random32()%3?random32()%50000:random32();
    slr_plan.tail_eax=random32();slr_plan.tail_edx=random32();
    slr_plan.swap_after_radius=random32()%4==0;slr_plan.swap_after_dist=random32()%4==0;
    uint32_t damage=(random32()%2?random32()&0xffff0000u:0)|(random32()%2?words[random32()%10]:(random32()&0xffffu));
    uint32_t reach=(random32()%2?random32()&0xffff0000u:0)|(random32()%2?words[random32()%10]:(random32()&0xffffu));
    if(n<10)reach=(reach&0xffff0000u)|words[n];
    if((uint16_t)reach==0)reach|=1u;
    if(real&&(uint16_t)reach==0xffffu)reach&=~1u;
    while(!real&&slr_fault(damage,reach,slr_plan.radius_eax,slr_plan.dist))reach=(reach&0xffff0000u)|(uint16_t)(1u+random32()%0xfffeu);
    a[0]=b+SLR_O;a[1]=damage;a[2]=b+SLR_P;a[3]=reach;a[4]=random32()%4?random32():b+SLR_O;
}
/* Calls target with five argument slots, EAX/EDX/ECX/EBX from regs. */
static void slr_invoke(void *target,const uint32_t *a,const uint32_t regs[4],uint32_t pattern,uint32_t out[3]){
    slv_invoke(target,a,5,regs,pattern,out);
}
static int slr_report(const char *what,unsigned n,const uint32_t want[3],const uint32_t got[3],unsigned calls,const slr_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x calls %u/%u\n",current_function,n,what,
        want[0],got[0],want[1],got[1],want[2],got[2],calls,slr_calls);
    for(unsigned j=0;j<calls&&j<slr_calls&&expected;++j)if(memcmp(expected+j,slr_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(slr_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<SLR_SIZE;++j)if(slr_after[j]!=slr_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,slr_after[j],slr_arena[j]);break;}
    return 1;
}
static unsigned slr_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<slr_calls;++j){unsigned id=slr_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?slr_slots[id]==c->key:!slr_slots[id]&&slr_addresses[id]==c->key)++hits;}
    return hits;
}
static uint64_t slr_core(unsigned index,const uint32_t *a,const uint32_t *r,uint32_t *ecx){
    es2_weapon_graph g=weapons_graph(mapped,NULL,index);
    return es2_actor_radial_hit_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],a[4],r,ecx);
}
static int slr_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    static slr_record expected[SLR_MAX];
    uint32_t regs[4]={r[0],r[1],r[2],0},want[3],got[3];
    memcpy(slr_before,slr_arena,SLR_SIZE);
    slr_reset();slr_invoke(mapped+d->rva,a,regs,0,want);unsigned calls=slr_calls;memcpy(expected,slr_log,calls*sizeof(*expected));
    memcpy(slr_after,slr_arena,SLR_SIZE);
    memcpy(slr_arena,slr_before,SLR_SIZE);slr_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[SLR_IDS];
    for(unsigned i=0;i<count&&i<SLR_IDS;++i)counts[i]=c[i].count;
    const uint64_t pair=x->replacement_context(a,d->user,r);
    got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);got[2]=want[2];   /* the adapter's ECX is the shim's constant 0 */
    if(n<4096)for(unsigned i=0;i<count&&i<SLR_IDS;++i)if(c[i].count-counts[i]!=slr_expected_hits(c+i))return fail("radial hit callee counter delta");
    if(want[2]||memcmp(want,got,12)||calls!=slr_calls||memcmp(expected,slr_log,calls*sizeof(*expected))||memcmp(slr_after,slr_arena,SLR_SIZE))
        return slr_report("adapter",n,want,got,calls,expected);
    /* The core on the same state: its pair and ECX. */
    memcpy(slr_arena,slr_before,SLR_SIZE);slr_reset();
    uint32_t core_ecx;const uint64_t core=slr_core(index,a,r,&core_ecx);
    got[0]=(uint32_t)core;got[1]=(uint32_t)(core>>32);got[2]=core_ecx;
    if(memcmp(want,got,12)||calls!=slr_calls||memcmp(expected,slr_log,calls*sizeof(*expected))||memcmp(slr_after,slr_arena,SLR_SIZE))
        return slr_report("core",n,want,got,calls,expected);
    memcpy(slr_arena,slr_before,SLR_SIZE);
    return 0;
}
static int slr_patch(BYTE saved[5],int *active,int on){
    if(on){*active=0;if(actor_patch(0x9288c,slr_stubs[1],saved,TRUE))return 1;*active=1;}
    else if(*active){if(actor_patch(0x9288c,NULL,saved,FALSE))return 1;*active=0;}
    return 0;
}
static int slr_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],want[3],got[3],pattern=poison?0xa7a7a7a7u:0x31313131u,regs[4]={pattern,pattern,pattern,pattern};slr_build(n,a,r,0);
        BYTE expected_fpu[108],actual_fpu[108];static slr_record expected[SLR_MAX];
        memcpy(slr_before,slr_arena,SLR_SIZE);
        slr_reset();slr_invoke(mapped+d->rva,a,regs,pattern,want);es2_hook_fpu_save(expected_fpu);
        unsigned calls=slr_calls;memcpy(expected,slr_log,calls*sizeof(*expected));memcpy(slr_after,slr_arena,SLR_SIZE);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("radial hit poison install");
        memcpy(slr_arena,slr_before,SLR_SIZE);slr_reset();
        slr_invoke(mapped+d->rva,a,regs,pattern,got);es2_hook_fpu_save(actual_fpu);
        if(want[2]||memcmp(want,got,12)||calls!=slr_calls||memcmp(expected,slr_log,calls*sizeof(*expected))||memcmp(slr_after,slr_arena,SLR_SIZE)||
           memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=slr_report(fast?"installed fast":"installed preserving",n,want,got,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("radial hit poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, EAX/EDX/ECX through the entry shim, trace/arena\n",d->name,total);
    return status;
}
/* Real dist_gate and real radius slot body 411aa4 as nested hooks in
 * rotating original/replaced/compare modes (compare falls back to replaced
 * where refused); the part slot stays a stub. The hook original (through
 * its shim) or replaced, against a run with only the nested hooks: EAX/EDX/
 * ECX, the part slot's log and the arena. */
static int slr_real_pass(unsigned index){
    const unsigned nested[2]={ES2_HOOK_INDEX_dist_gate,ES2_HOOK_INDEX_actor_radius};
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[2]={0};
    slr_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],want[3],got[3],pattern=n&1?0xa7a7a7a7u:0x31313131u,regs[4]={pattern,pattern,pattern,pattern};slr_build(n,a,r,1);
        static slr_record expected[SLR_MAX];
        memcpy(slr_before,slr_arena,SLR_SIZE);
        es2_hook hk[3];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<2;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("radial hit nested install");}
        slr_reset();slr_invoke(mapped+d->rva,a,regs,pattern,want);
        unsigned calls=slr_calls;memcpy(expected,slr_log,calls*sizeof(*expected));memcpy(slr_after,slr_arena,SLR_SIZE);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+2,d,&o,function_hook_extension(index)))return fail("radial hit caller install");
        memcpy(slr_arena,slr_before,SLR_SIZE);slr_reset();
        slr_invoke(mapped+d->rva,a,regs,pattern,got);
        if(want[2]||memcmp(want,got,12)||calls!=slr_calls||memcmp(expected,slr_log,calls*sizeof(*expected))||memcmp(slr_after,slr_arena,SLR_SIZE)||
           (mode>=3&&hk[2].calls!=1)){
            fprintf(stderr,"real mode=%u\n",mode);status=slr_report("real",n,want,got,calls,expected);
        }
        for(unsigned j=0;j<3;++j){if(j<2)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("radial hit nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("radial hit nested uninstall");}
        ++total;
    }
    slr_real=0;
    for(unsigned j=0;j<2&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases, dist_gate and actor_radius as nested hooks in rotating modes, part slot a stub, 31/a7 stack, EAX/EDX/ECX, trace/arena\n",d->name,total);
    return status;
}
static int slr_cases(unsigned index,unsigned limit){
    BYTE saved[5];int active=0;
    if(slr_patch(saved,&active,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_SLOT_LEAVES_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];slr_build(n,a,r,0);status=slr_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    if(!limit&&!status)status=slr_installed_probes(index,64);
    if(slr_patch(saved,&active,0))return 1;
    if(!limit&&!status)status=slr_real_pass(index);
    if(!status)printf("PASS: %-20s %u cases; EAX/EDX and the original's ECX (0), ordered calls with entry registers, arena, callee counters\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
static int slot_leaves_cases(unsigned index,unsigned limit){
    current_function=function_hook_descriptor(index)->name;
    if(index==ES2_HOOK_INDEX_actor_radial_hit)return slr_cases(index,limit);
    return slv_leaf_cases(index,limit);
}
