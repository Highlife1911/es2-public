/* key_axes group: key_axes 45a4cc, the keyboard-axis block of the control
 * record (input.md, "keyboard axes 45a4cc"). The direct cases run the original
 * and the core on the same state: the key arrays 4d2394 (down), 4d23a3
 * (released before seen), 4d23b2 (seen), the block 4d23c1 and the Alt word
 * 4d23d6 (window 4d2380..4d23ff), the tables 49eb3d/49eb45 (window
 * 49eb30..49eb6f) and a small arena. Key densities from none to all, with
 * nonzero bytes other than 1; the tables as the initializer 45b8a4 writes them
 * (run once as original code, the exclusive flags as shipped) or random with
 * the signed extremes 7Fh/80h; the Alt word zero or not; the flag any dword. In
 * one case in four the block is not 4d23c1 but lies in the arena, over the key
 * arrays and the Alt word, or over the tables, so the original's read and write
 * order (the reread of the exclusive flag at 45a54c included) is compared. The
 * EDX:EAX pair, both windows and the arena are compared. Then installed 31/a7
 * poison probes and a real-caller pass through control_record 45a810 (its other
 * seventeen callees as control_record_cases.h's logging stubs), key_axes
 * original or replaced, control_record original or replaced, against a run
 * without hooks: EAX/EDX/ECX of 45a810, the ordered stub calls with their entry
 * registers (the EDX that 45a4cc leaves reaches 45feb8 and 452d40), the record,
 * the arena and the globals. key_axes accepts compare; compare_probes.h's
 * installed compare cases run on the direct inputs with the block at 4d23c1,
 * and the real-caller pass adds key_axes in compare under control_record
 * original or replaced. */
enum { KX_SIZE=0x100, KX_W1=0xd2380, KX_L1=0x80, KX_W2=0x9eb30, KX_L2=0x40, KX_INIT=0x5b8a4, KX_REAL=768 };
#define KX_GLOBALS (KX_L1+KX_L2)
#define KX_BASE ((uint32_t)(uintptr_t)kx_arena)
#define KX_VA(va) (mapped+(uint32_t)(va)-0x400000u)
static BYTE kx_arena[KX_SIZE],kx_before[KX_SIZE],kx_after[KX_SIZE],kx_shipped[KX_L2];
static unsigned kx_pad,kx_exclusive,kx_twist,kx_moved,kx_tap;
static void kx_globals(BYTE *out,int restore){
    if(restore){memcpy(mapped+KX_W1,out,KX_L1);memcpy(mapped+KX_W2,out+KX_L1,KX_L2);}
    else{memcpy(out,mapped+KX_W1,KX_L1);memcpy(out+KX_L1,mapped+KX_W2,KX_L2);}
}
/* The tables as 45b8a4 writes them over the shipped exclusive flags. */
static void kx_init_tables(void){
    BYTE saved[KX_L2];memcpy(saved,mapped+KX_W2,KX_L2);
    ((void (*)(void))(void *)(mapped+KX_INIT))();
    memcpy(kx_shipped,mapped+KX_W2,KX_L2);memcpy(mapped+KX_W2,saved,KX_L2);
}
static BYTE kx_byte(unsigned odds){unsigned k=random32()%16;return k>=odds?0:k%4?1:(BYTE)(random32()%255+1);}
/* Key arrays, Alt word; counts the paths the state selects. */
static void kx_keys(void){
    BYTE *down=KX_VA(0x4d2394),*released=KX_VA(0x4d23a3),*seen=KX_VA(0x4d23b2);
    unsigned density=random32()%5,odds=density==0?0:density==1?1:density==2?4:density==3?8:16;
    for(unsigned j=0;j<15;++j)down[j]=kx_byte(odds);
    if(density==0&&random32()%2)down[random32()%15]=1;
    for(unsigned j=0;j<15;++j)released[j]=kx_byte(random32()%2?2:6);
    for(unsigned j=0;j<15;++j)seen[j]=(BYTE)(random32()%3?random32()%2:random32());
    put16(KX_VA(0x4d23d6),(uint16_t)(random32()%2?0:random32()%2?0x200:random32()));
    unsigned pad=0,twist=0,tap=0;
    for(unsigned j=0;j<15;++j){if(!down[j])continue;if(j<8)pad=1;else if(j<14)twist=1;if(released[j])tap=1;}
    kx_pad+=pad;kx_twist+=twist;kx_tap+=tap;
}
static void kx_tables(void){
    BYTE *exclusive=KX_VA(0x49eb3d),*table=KX_VA(0x49eb45);
    memcpy(mapped+KX_W2,kx_shipped,KX_L2);
    if(random32()%4)return;
    static const BYTE values[]={0xff,0,1,0x7f,0x80,0x81,0xfe,2};
    for(unsigned j=0;j<8;++j)exclusive[j]=(BYTE)(random32()%4?random32()%2:random32());
    for(unsigned j=0;j<28;++j)table[j]=random32()%4?values[random32()%8]:(BYTE)random32();
}
/* Fills one state; a[0] = block, a[1] = flag, r = entry EAX/EDX/ECX. */
static void kx_build(unsigned n,uint32_t a[6],uint32_t r[3],int real){
    (void)n;memset(a,0,24);for(unsigned i=0;i<3;++i)r[i]=random32();
    /* Under the real caller the window also holds control_record's
     * pointer tables (4d2380..4d238f), the device pointer 4d2390 and the
     * button defaults, which ctl_build prepared; only the keys change. */
    if(real)memcpy(mapped+KX_W2,kx_shipped,KX_L2);
    else{random_bytes(kx_arena,KX_SIZE);random_bytes(mapped+KX_W1,KX_L1);kx_tables();}
    kx_keys();
    a[0]=0x4d23c1u;a[1]=random32()%4?random32()%2:random32();
    if(real||random32()%4)return;
    ++kx_moved;
    switch(random32()%4){
    case 0:a[0]=KX_BASE+random32()%(KX_SIZE-0x14);break;
    case 1:a[0]=0x4d2394u+random32()%0x44;break;
    case 2:a[0]=0x49eb30u+random32()%0x2c;break;
    default:a[0]=0x4d23a0u+random32()%0x30;break;
    }
}
static int kx_report(const char *what,unsigned n,const uint64_t pair[2],const BYTE *want,const BYTE *got){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1]);
    for(unsigned j=0;j<KX_SIZE;++j)if(kx_after[j]!=kx_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,kx_after[j],kx_arena[j]);break;}
    for(unsigned j=0;j<KX_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);break;}
    return 1;
}
static int kx_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[KX_GLOBALS],after[KX_GLOBALS],actual[KX_GLOBALS];
    memcpy(kx_before,kx_arena,KX_SIZE);kx_globals(before,0);
    /* The paths the original takes, read before it runs. */
    if(a[0]==0x4d23c1u)for(unsigned j=0;j<8;++j)if(KX_VA(0x4d2394)[j]&&KX_VA(0x49eb3d)[j]){++kx_exclusive;break;}
    uint64_t pair[2];pair[0]=weapons_original(index,a,r);
    memcpy(kx_after,kx_arena,KX_SIZE);kx_globals(after,0);
    memcpy(kx_arena,kx_before,KX_SIZE);kx_globals(before,1);
    pair[1]=x->replacement_context(a,d->user,r);kx_globals(actual,0);
    if(pair[0]!=pair[1]||memcmp(kx_after,kx_arena,KX_SIZE)||memcmp(after,actual,KX_GLOBALS))
        return kx_report("direct",n,pair,after,actual);
    return 0;
}
static int kx_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;kx_build(n*37u+5u,a,r,0);
        BYTE before[KX_GLOBALS],after[KX_GLOBALS],actual[KX_GLOBALS],expected_fpu[108],actual_fpu[108];
        memcpy(kx_before,kx_arena,KX_SIZE);kx_globals(before,0);
        sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        memcpy(kx_after,kx_arena,KX_SIZE);kx_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("key_axes poison install");
        memcpy(kx_arena,kx_before,KX_SIZE);kx_globals(before,1);
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);es2_hook_fpu_save(actual_fpu);kx_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||memcmp(kx_after,kx_arena,KX_SIZE)||memcmp(after,actual,KX_GLOBALS)||
           memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=kx_report(fast?"installed fast":"installed preserving",n,pair,after,actual);
        if(!es2_hook_uninstall(&h))return fail("key_axes poison uninstall");kx_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/arena/globals\n",d->name,total);
    return status;
}
/* Through the only caller: 45a810 with every callee but 45a4cc stubbed
 * (control_record_cases.h), the reference without hooks against
 * key_axes original (mode 0) or replaced (1), control_record original
 * (modes 0, 1) or replaced through its entry shim (2, 3); key_axes
 * in compare under control_record original (4) or replaced (5). */
static int kx_real(unsigned index){
    const es2_hook_descriptor *d=function_hook_descriptor(index),*c=function_hook_descriptor(ES2_HOOK_INDEX_control_record);
    const uint32_t which=0x3ffffu&~(1u<<KS_AXES);int status=0;unsigned total=0;uint64_t entered=0,nested=0;
    BYTE saved[CTL_IDS][5];int active[CTL_IDS];
    if(ctl_patch(saved,active,which,1))return 1;
    uint64_t compared=0;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<KX_REAL&&!status;++n){
        const int compare=mode>=4,outer=compare?mode&1:mode>>1&1;
        uint32_t a[6],r[3],want[3],got[3],pattern=n&1?0xa7a7a7a7u:0x31313131u;
        ctl_build(n,r);kx_build(n,a,r,1);
        static BYTE before[CTL_GLOBALS],after[CTL_GLOBALS],actual[CTL_GLOBALS];static ctl_record expected[CTL_MAX];
        BYTE kx_b[KX_GLOBALS],kx_a[KX_GLOBALS],kx_g[KX_GLOBALS];
        memcpy(ctl_before,ctl_arena,CTL_SIZE);ctl_globals(before,0);kx_globals(kx_b,0);
        ctl_reset();ctl_invoke(mapped+c->rva,r,pattern,want);unsigned calls=ctl_calls;memcpy(expected,ctl_log,calls*sizeof(*expected));
        memcpy(ctl_after,ctl_arena,CTL_SIZE);ctl_globals(after,0);kx_globals(kx_a,0);
        es2_hook h={0},hc={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        o.mode=compare?ES2_HOOK_COMPARE:mode&1?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("key_axes real install");
        if(outer){o.mode=ES2_HOOK_REPLACED;if(!weapons_install_hook(&hc,c,&o,function_hook_extension(ES2_HOOK_INDEX_control_record)))return fail("key_axes real control_record install");}
        memcpy(ctl_arena,ctl_before,CTL_SIZE);ctl_globals(before,1);kx_globals(kx_b,1);ctl_reset();
        control_record_ecx_valid=0;ctl_invoke(mapped+c->rva,r,pattern,got);ctl_globals(actual,0);kx_globals(kx_g,0);
        if(memcmp(want,got,12)||calls!=ctl_calls||memcmp(expected,ctl_log,calls*sizeof(*expected))||memcmp(ctl_after,ctl_arena,CTL_SIZE)||
           ctl_globals_differ(after,actual)||memcmp(kx_a,kx_g,KX_GLOBALS)||h.calls>1||(outer&&hc.calls!=1)||control_record_ecx_valid||
           (compare&&(h.mismatches||h.skipped||h.bypassed))){
            fprintf(stderr,"real 45a810 mode=%u key_axes calls %llu control_record calls %llu\n",mode,(unsigned long long)h.calls,(unsigned long long)hc.calls);
            status=ctl_report("key_axes real",n,want,got,calls,expected);
        }
        if(compare)compared+=h.calls;else if(mode&1){entered+=h.calls;if(outer)nested+=h.calls;}
        if(outer&&!es2_hook_uninstall(&hc))return fail("key_axes real control_record uninstall");
        if(!es2_hook_uninstall(&h))return fail("key_axes real uninstall");
        ctl_globals(before,1);kx_globals(kx_b,1);++total;
    }
    if(ctl_patch(saved,active,which,0))return 1;
    if(!status&&(!entered||!nested||!compared)){fprintf(stderr,"FAIL: %s real pass entered %llu, nested %llu, compared %llu\n",d->name,
        (unsigned long long)entered,(unsigned long long)nested,(unsigned long long)compared);status=1;}
    if(!status)printf("PASS: %s %u real-caller cases under 45a810 (%llu replaced entries, %llu under the replaced control_record, %llu compared without mismatch), "
        "key_axes original/replaced/compare, control_record original/replaced, EAX/EDX/ECX, stub calls with registers, arena/globals\n",d->name,total,
        (unsigned long long)entered,(unsigned long long)nested,(unsigned long long)compared);
    return status;
}
/* Compare cases (compare_probes.h): the direct inputs with the block at 4d23c1
 * (a block over the key arrays would overlap the region). */
static void kx_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){(void)index;kx_build(n,a,r,0);a[0]=0x4d23c1u;}
static void kx_compare_state(BYTE *s,int restore){
    if(restore){memcpy(kx_arena,s,KX_SIZE);kx_globals(s+KX_SIZE,1);}else{memcpy(s,kx_arena,KX_SIZE);kx_globals(s+KX_SIZE,0);}
}
static int key_axes_cases(unsigned index,unsigned limit){
    BYTE saved_globals[KX_GLOBALS];static BYTE saved_ctl[CTL_GLOBALS];kx_globals(saved_globals,0);ctl_globals(saved_ctl,0);
    kx_init_tables();
    if(limit&&compare_probe_installed(index)){int status=compare_probe_run(index,limit,kx_compare_build,kx_compare_state,KX_SIZE+KX_GLOBALS);
        kx_globals(saved_globals,1);return status;}
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_KEY_AXES_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    kx_pad=kx_exclusive=kx_twist=kx_moved=kx_tap=0;
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];kx_build(n,a,r,0);status=kx_compare(index,n,a,r);}
    const unsigned pad=kx_pad,exclusive=kx_exclusive,twist=kx_twist,moved=kx_moved,tap=kx_tap;
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    kx_globals(saved_globals,1);
    if(!limit&&!status)status=kx_installed_probes(index,64);
    kx_globals(saved_globals,1);
    if(!limit&&!status)status=kx_real(index);
    kx_globals(saved_globals,1);
    if(!limit&&!status)status=compare_probe_run(index,0,kx_compare_build,kx_compare_state,KX_SIZE+KX_GLOBALS);
    kx_globals(saved_globals,1);ctl_globals(saved_ctl,1);
    if(!status)printf("PASS: %-20s %u cases (%u with a pad key, %u exclusive at 4d23c1, %u with a twist/pitch key, %u taps, %u moved blocks); pair, arena and globals\n",
        function_hook_descriptor(index)->name,cases,pad,exclusive,twist,tap,moved);
    return status;
}
