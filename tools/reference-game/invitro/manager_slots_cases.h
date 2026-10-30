/* manager_slots group: the object base methods object_position 46b905 and
 * shape_word 46b910 and the eight clip methods 478760 ... 47882c
 * (docs/reverse-engineering/main_loop.md, "the stage-5 flushes and the manager
 * slot methods"). Leaves without calls or writes.
 *
 * Direct cases run the original body (four argument slots, EAX/EDX/ECX at
 * entry from the case; EAX/EDX/ECX after it captured), the adapter (pair)
 * and the core (pair and the ECX it reports) over a 1 KiB arena: objects
 * whose [O+34h] is 0 or a shape instance S, whose [S+4] is 0 or a record Q
 * (word Q+8 random), and clips whose count word C+0Ah takes the edges 0, 1,
 * 7FFFh, 8000h, FFFFh or any value; the frame slots take the edges 0, 1,
 * the count and its neighbours, 7FFFh, 8000h, FFFFh under random high words.
 * Compared: the pair, ECX (core) and the arena (unchanged). Then installed
 * 31/a7 poison probes and the compare probe (compare_probes.h; all ten
 * accept compare). The branch sites of 46b910 are owned here (46b92e
 * PROVED one way); the clip sites belong to track_advance/track_target and
 * are armed in every window. Identifiers: mgs_/MGS_. */
enum { MGS_SIZE=0x400, MGS_OBJECTS=8, MGS_SHAPES=0x200, MGS_RECORDS=0x280, MGS_CLIPS=0x300 };
#define MGS_BASE ((uint32_t)(uintptr_t)mgs_arena)
static BYTE mgs_arena[MGS_SIZE],mgs_before[MGS_SIZE];
static BYTE *mgs_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static uint32_t mgs_method(unsigned index){return function_hook_descriptor(index)->rva+0x400000u;}
static uint32_t mgs_word(uint32_t count){
    const uint32_t pick=random32(),high=random32()%3?random32()&0xffff0000u:0;
    static const uint32_t edges[]={0,1,0x7fff,0x8000,0xffff,0x7ffe,0x8001};
    switch(pick%6){
    case 0:return high|edges[(pick>>8)%7];
    case 1:case 2:return high|((count+(pick>>8)%5-2u)&0xffffu);   /* around the count */
    case 3:return high|((pick>>8)%8);
    default:return random32();
    }
}
/* a[0] = the object or clip, a[1..3] the slots; r = entry EAX/EDX/ECX. */
static void mgs_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    random_bytes(mgs_arena,MGS_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    const uint32_t b=MGS_BASE;
    for(unsigned i=0;i<MGS_OBJECTS;++i){BYTE *o=mgs_arena+0x40*i;const uint32_t pick=random32();
        put32(o+0x34,pick%4==0?0:b+MGS_SHAPES+0x10u*(pick>>4&7u));}
    for(unsigned i=0;i<8;++i){const uint32_t pick=random32();
        put32(mgs_arena+MGS_SHAPES+0x10*i+4,pick%4==0?0:b+MGS_RECORDS+0x10u*(pick>>4&7u));}
    if(index==ES2_HOOK_INDEX_object_position){a[0]=n<4?(n==0?0:n==1?0xffffffffu:n==2?0xffffffdau:0x80000000u):random32();return;}
    if(index==ES2_HOOK_INDEX_shape_word){
        a[0]=b+0x40u*(random32()%MGS_OBJECTS);
        if(n<3){put32(mgs_host(a[0]+0x34),n==0?0:b+MGS_SHAPES);put32(mgs_arena+MGS_SHAPES+4,n==1?0:b+MGS_RECORDS);}
        return;
    }
    const uint32_t clip=b+MGS_CLIPS+0x20u*(random32()%4);a[0]=clip;
    static const uint16_t counts[]={0,1,2,5,0x7fff,0x8000,0xffff,0x8001};
    const uint32_t pick=random32();
    const uint16_t count=(uint16_t)(pick%3?counts[(pick>>4)%8]:random32());
    put16(mgs_host(clip+0x0a),count);
    a[1]=mgs_word(count);a[2]=random32()%3?mgs_word(count):(a[1]&0xffffu)|(random32()&0xffff0000u);a[3]=mgs_word(count);
    if(random32()%4==0)a[3]&=0xffff0000u;
}
/* Calls target with four argument slots and EAX/EDX/ECX = r; out receives
 * EAX, EDX and ECX after the call. */
__attribute__((naked,noinline)) static void mgs_invoke(void *target,const uint32_t *a,const uint32_t *r,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl 12(%ebp),%esi\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static uint64_t mgs_core(unsigned index,const uint32_t *a,const uint32_t *r,uint32_t *ecx){
    if(index==ES2_HOOK_INDEX_object_position)return es2_object_position_registers(a[0],r,ecx);
    if(index==ES2_HOOK_INDEX_shape_word)return es2_shape_word_registers(&math_native_memory,a[0],r,ecx);
    const uint32_t args[3]={a[1],a[2],a[3]};
    return es2_clip_method_registers(&math_native_memory,mgs_method(index),a[0],args,r,ecx);
}
static int mgs_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    uint32_t want[3],got[3];
    memcpy(mgs_before,mgs_arena,MGS_SIZE);
    mgs_invoke(mapped+d->rva,a,r,want);
    const uint64_t pair=x->replacement_context(a,d->user,r);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);
    const uint64_t core=mgs_core(index,a,r,&got[2]);
    /* With the hook installed (integration pass) the "original" run is the
       hook, whose ECX is the framework's; ECX is compared otherwise. */
    const int ecx_known=!rng_hooks[index].installed;
    if(want[0]!=got[0]||want[1]!=got[1]||core!=pair||(ecx_known&&want[2]!=got[2])||memcmp(mgs_before,mgs_arena,MGS_SIZE)){
        fprintf(stderr,"FAIL: %s case %u eax %08x/%08x edx %08x/%08x ecx %08x/%08x core %016llx args %08x %08x %08x %08x\n",
            d->name,n,want[0],got[0],want[1],got[1],want[2],got[2],(unsigned long long)core,a[0],a[1],a[2],a[3]);
        return 1;
    }
    return 0;
}
static int mgs_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],want[2],got[2],pattern=poison?0xa7a7a7a7u:0x31313131u;mgs_build(index,n,a,r);
        BYTE expected_fpu[108],actual_fpu[108];
        memcpy(mgs_before,mgs_arena,MGS_SIZE);
        sg_poison_call(mapped+d->rva,a,pattern,want);es2_hook_fpu_save(expected_fpu);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("manager_slots poison install");
        sg_poison_call(mapped+d->rva,a,pattern,got);es2_hook_fpu_save(actual_fpu);
        if(want[0]!=got[0]||want[1]!=got[1]||memcmp(mgs_before,mgs_arena,MGS_SIZE)||
           memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1){
            fprintf(stderr,"FAIL: %s case %u (installed %s) pair %08x%08x/%08x%08x\n",d->name,n,fast?"fast":"preserving",want[1],want[0],got[1],got[0]);
            status=1;
        }
        if(!es2_hook_uninstall(&h))return fail("manager_slots poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair, arena\n",d->name,total);
    return status;
}
static void mgs_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){mgs_build(index,n,a,r);}
static void mgs_compare_state(BYTE *s,int restore){if(restore)memcpy(mgs_arena,s,MGS_SIZE);else memcpy(s,mgs_arena,MGS_SIZE);}
static int manager_slots_cases(unsigned index,unsigned limit){
    current_function=function_hook_descriptor(index)->name;
    if(limit&&compare_probe_installed(index))return compare_probe_run(index,limit,mgs_compare_build,mgs_compare_state,MGS_SIZE);
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_MANAGER_SLOTS_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];mgs_build(index,n,a,r);status=mgs_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    if(!limit&&!status)status=mgs_installed_probes(index,64);
    if(!limit&&!status)status=compare_probe_run(index,0,mgs_compare_build,mgs_compare_state,MGS_SIZE);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ECX (core), arena unchanged\n",function_hook_descriptor(index)->name,cases);
    return status;
}
