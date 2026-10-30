/* Formation group: the actor virtual +78h, formation_base 405c04,
 * formation_mech 417898 and formation_flyer 421e98 (docs/reverse-
 * engineering/ai.md, section "Formation slot virtual +78h"). Direct cases run
 * the original body (with its helpers 405b9c/4205cc/423044 and the placement
 * 411d64) and the core against logging stubs for euler_matrix 47ebb8 and
 * rotate_2d 47ffe8: their arguments, entry EAX/EDX/ECX (stack values masked: Q
 * and R are the original's frame or the adapter's scratch) and the bytes they
 * receive (the angles, Q, the matrix words), by content. The stubs write the
 * matrix and R from a per-case stream and, in some cases, poke a word the
 * original reads or writes later (the point P after rotate_2d, the cache flag
 * or the BASE buffer's z after euler_matrix), so the read/write order around
 * the calls is compared. The actor, group, member list, leader, slot entries
 * and P live in an arena; the slot tables are reached through pointers set so
 * that any type and slot word (also negative) lands in the arena; the static
 * buffers with their first-call bytes and the three table pointers are compared
 * and restored global windows. Some cases let P overlap the leader's cache flag
 * or a static buffer, or the BASE entry overlap its static buffer. EDX:EAX and
 * the callee counter deltas are compared. Then installed 31/a7 poison probes
 * and a real-callee pass with the real euler_matrix and rotate_2d (themselves
 * in rotating original/replaced/compare modes), the hook original or replaced.
 * The three accept compare; compare_probes.h's installed compare cases
 * run on the real-pass inputs with the real euler_matrix and rotate_2d (the
 * leader's cache flag 0 in some cases, so the original phase fills the cache
 * that the replacement phase reads). */
typedef struct fo_record { uint32_t id,args[3],regs[3]; BYTE angles[6],q[12],matrix[8],pad[2]; } fo_record;
enum { FO_SIZE=0x800, FO_ACTOR=0x000, FO_GROUP=0x080, FO_LIST=0x0c0, FO_LEADER=0x100, FO_ENTRY=0x300, FO_ROWS=0x400,
    FO_POINT=0x600, FO_MAX=4, FO_GLOBALS=64 };
#define FO_BASE ((uint32_t)(uintptr_t)fo_arena)
static BYTE fo_arena[FO_SIZE],fo_before[FO_SIZE],fo_after[FO_SIZE];
static fo_record fo_log[FO_MAX];
static unsigned fo_calls,fo_poke_call;
static uint32_t fo_seed,fo_stream,fo_poke_address,fo_poke_value;
__attribute__((used)) static uint32_t fo_ecx;
/* The BASE buffer 4975d6..4975e2 and the MECH buffer 49a430..49a436 with
 * eight bytes around each (P may overlap them), and the table pointers
 * 4a95ac, 4a9d6c, 4a9dfc. */
static const uint32_t fo_window_rva[]={0x975ce,0x9a428,0xa95ac,0xa9d6c,0xa9dfc};
static const unsigned fo_window_size[]={29,23,4,4,4};
static uint32_t fo_next(void){
    uint32_t z=fo_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static int fo_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
/* id 0: euler_matrix(angles, matrix); id 1: rotate_2d(Q, matrix, R). */
__attribute__((used,noinline)) static uint64_t __cdecl fo_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(fo_calls==FO_MAX){fail("formation synthetic call overflow");ExitProcess(1);}
    fo_record *v=fo_log+fo_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,id?12:8);memcpy(v->regs,regs,12);
    if(id)memcpy(v->q,(const void *)(uintptr_t)a[0],12);else memcpy(v->angles,(const void *)(uintptr_t)a[0],6);
    memcpy(v->matrix,(const void *)(uintptr_t)a[1],8);
    for(unsigned j=0;j<3;++j)if(fo_stack(v->regs[j]))v->regs[j]=0;
    for(unsigned j=0;j<3;++j)if(fo_stack(v->args[j]))v->args[j]=0;
    if(id){put32((BYTE *)(uintptr_t)a[2],fo_next());put32((BYTE *)(uintptr_t)a[2]+4,fo_next());}
    else for(unsigned j=0;j<8;j+=4)put32((BYTE *)(uintptr_t)a[1]+j,fo_next());
    if(fo_poke_address&&fo_poke_call==id)put32((BYTE *)(uintptr_t)fo_poke_address,fo_poke_value);
    uint32_t eax=fo_next(),edx=fo_next();fo_ecx=fo_next();
    return (uint64_t)edx<<32|eax;
}
#define FO_STUB(n) __attribute__((naked)) static void fo_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _fo_stub_impl\n\taddl $24,%esp\n\tmovl _fo_ecx,%ecx\n\tret\n\t");}
FO_STUB(0)
FO_STUB(1)
#undef FO_STUB
static void fo_globals(BYTE *out,int restore){
    unsigned at=0;
    for(unsigned i=0;i<5;++i){
        if(restore)memcpy(mapped+fo_window_rva[i],out+at,fo_window_size[i]);else memcpy(out+at,mapped+fo_window_rva[i],fo_window_size[i]);
        at+=fo_window_size[i];
    }
}
static void fo_reset(void){fo_calls=0;fo_stream=fo_seed;}
static uint16_t fo_word(const uint16_t *values,unsigned count){return random32()%4?values[random32()%count]:(uint16_t)random32();}
static BYTE fo_first(void){return (BYTE)(random32()%4==0?0:random32()%2?1:random32()|1u);}
/* One state for hook index. kind: 0 BASE, 1 MECH, 2 FLYER. real: no
 * pokes and no aliasing (the real callees run). */
static unsigned fo_kind(unsigned index){return index==ES2_HOOK_INDEX_formation_base?0:index==ES2_HOOK_INDEX_formation_mech?1:2;}
static void fo_build(unsigned index,unsigned n,int real,uint32_t a[6]){
    static const uint16_t slots[]={1,2,3,4,5,6,7,8,0x100,0x7fff,0x8000,0xffff,0xfffe};
    static const uint16_t types[]={0,1,2,3,4,5,6,0x7fff,0x8000,0xffff};
    static const uint16_t flags[]={0,0,1,1,2,0x100,0x8000,0xffff};
    const unsigned kind=fo_kind(index);const uint32_t b=FO_BASE;
    fo_seed=random32();random_bytes(fo_arena,FO_SIZE);
    BYTE *actor=fo_arena+FO_ACTOR,*group=fo_arena+FO_GROUP,*leader=fo_arena+FO_LEADER;
    const uint16_t slot=n%8==0?0:fo_word(slots,sizeof(slots)/sizeof(slots[0])),type=fo_word(types,sizeof(types)/sizeof(types[0]));
    put16(actor+0x49,slot);put32(actor+0x45,b+FO_GROUP);put16(group+2,type);put32(group+0xc,b+FO_LIST);
    put32(fo_arena+FO_LIST,b+FO_LEADER);put16(leader+0x32,flags[random32()%8]);
    /* Static buffers and their first-call bytes; table pointers random. */
    random_bytes(mapped+0x975ce,29);random_bytes(mapped+0x9a428,23);
    mapped[0x975e2]=fo_first();mapped[0x9a436]=fo_first();
    put32(mapped+0xa95ac,random32());put32(mapped+0xa9d6c,random32());put32(mapped+0xa9dfc,random32());
    /* The slot entry E; BASE entries sometimes overlap the static buffer. */
    const uint32_t t=(uint32_t)(int32_t)(int16_t)type,s=(uint32_t)(int32_t)(int16_t)slot;
    uint32_t e=b+FO_ENTRY+random32()%0x40u;
    if(!real&&kind==0&&n%16==7)e=0x4975d6u-6u+random32()%13u;
    if(kind==0){
        uint32_t rows=b+FO_ROWS+random32()%0x40u;put32((BYTE *)(uintptr_t)rows,e-10u*s+10u);
        put32(mapped+0xa95ac,rows-28u*t-4u);
    } else if(kind==1)put32(mapped+0xa9d6c,e-28u*t-4u*s+4u);
    else put32(mapped+0xa9dfc,e-18u*t-6u*s+6u);
    /* P: normally in the arena; some cases overlap the cache flag L+32h or
     * the static buffer of the hook's class. */
    uint32_t p=b+FO_POINT+random32()%0x40u;
    if(!real&&n%16==5)p=b+FO_LEADER+0x28u+random32()%0xcu;
    else if(!real&&n%16==9&&kind==0)p=0x4975d6u-8u+random32()%17u;
    else if(!real&&n%16==9&&kind==1)p=0x49a430u-8u+random32()%12u;
    /* Pokes: rotate_2d writes into P (read after the call by the adds);
     * euler_matrix writes the cache flag (written again after the call) or
     * the BASE buffer's z (read before the call). */
    fo_poke_address=0;fo_poke_call=0;fo_poke_value=random32();
    if(!real&&n%4==3){fo_poke_call=1;fo_poke_address=p+4u*(random32()%3u);}
    else if(!real&&n%8==6){fo_poke_call=0;fo_poke_address=kind==0&&random32()%2?0x4975deu:b+FO_LEADER+0x30u+random32()%3u;}
    a[0]=b+FO_ACTOR;a[1]=p;
}
static int fo_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const fo_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,fo_calls);
    for(unsigned j=0;j<calls&&j<fo_calls&&expected;++j)if(memcmp(expected+j,fo_log+j,sizeof(*expected))){
        const BYTE *e=(const BYTE *)(expected+j),*f=(const BYTE *)(fo_log+j);
        for(unsigned k=0;k<sizeof(*expected);++k)if(e[k]!=f[k])fprintf(stderr," call%u byte%u %02x/%02x\n",j,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<FO_SIZE;++j)if(fo_after[j]!=fo_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,fo_after[j],fo_arena[j]);break;}
    return 1;
}
static int fo_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[FO_GLOBALS],after[FO_GLOBALS],actual[FO_GLOBALS];static fo_record expected[FO_MAX];
    memcpy(fo_before,fo_arena,FO_SIZE);fo_globals(before,0);
    const uint64_t placed=get16(fo_arena+FO_ACTOR+0x49)!=0;
    fo_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=fo_calls;memcpy(expected,fo_log,calls*sizeof(*expected));
    memcpy(fo_after,fo_arena,FO_SIZE);fo_globals(after,0);
    memcpy(fo_arena,fo_before,FO_SIZE);fo_globals(before,1);fo_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[4]={0};
    for(unsigned i=0;i<count&&i<4;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);fo_globals(actual,0);
    uint64_t eulers=0,rotations=0;for(unsigned j=0;j<calls;++j){if(expected[j].id)++rotations;else ++eulers;}
    if(count!=4||c[0].count-counts[0]!=placed||c[1].count-counts[1]!=placed||c[2].count-counts[2]!=eulers||c[3].count-counts[3]!=rotations)
        return fail("formation callee counter delta");
    if(pair[0]!=pair[1]||calls!=fo_calls||memcmp(expected,fo_log,calls*sizeof(*expected))||memcmp(fo_after,fo_arena,FO_SIZE)||
       memcmp(after,actual,FO_GLOBALS))
        return fo_report("direct",n,pair,calls,expected);
    return 0;
}
static int fo_patch(BYTE saved[2][5],int on){
    if(actor_patch(0x7ebb8u,on?(void *)fo_stub_0:NULL,saved[0],on?TRUE:FALSE))return 1;
    return actor_patch(0x7ffe8u,on?(void *)fo_stub_1:NULL,saved[1],on?TRUE:FALSE);
}
static int fo_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6]={0},expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;fo_build(index,n,0,a);
        BYTE before[FO_GLOBALS],after[FO_GLOBALS],actual[FO_GLOBALS],expected_fpu[108],actual_fpu[108];static fo_record expected[FO_MAX];
        memcpy(fo_before,fo_arena,FO_SIZE);fo_globals(before,0);
        fo_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=fo_calls;memcpy(expected,fo_log,calls*sizeof(*expected));memcpy(fo_after,fo_arena,FO_SIZE);fo_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("formation poison install");
        memcpy(fo_arena,fo_before,FO_SIZE);fo_globals(before,1);fo_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);es2_hook_fpu_save(actual_fpu);fo_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=fo_calls||memcmp(expected,fo_log,calls*sizeof(*expected))||memcmp(fo_after,fo_arena,FO_SIZE)||
           memcmp(after,actual,FO_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=fo_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("formation poison uninstall");fo_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, EDX:EAX/trace/arena/globals\n",d->name,total);
    return status;
}
/* Real euler_matrix and rotate_2d, themselves in rotating original/
 * replaced/compare modes (compare falls back to replaced where refused),
 * the hook original (modes 0-2) or replaced (3-5), against a plain run of
 * the original. EDX:EAX, arena (leader matrix and flag, P) and globals. */
static int fo_real_pass(unsigned index){
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[2]={0},hooked=0,replaced=0;
    static const unsigned nested[2]={ES2_HOOK_INDEX_euler_matrix,ES2_HOOK_INDEX_rotate_2d};
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[4]={0};for(unsigned i=0;i<count&&i<4;++i)counts[i]=c[i].count;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6]={0},expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;fo_build(index,n,1,a);
        BYTE before[FO_GLOBALS],after[FO_GLOBALS],actual[FO_GLOBALS];
        memcpy(fo_before,fo_arena,FO_SIZE);fo_globals(before,0);
        fo_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        memcpy(fo_after,fo_arena,FO_SIZE);fo_globals(after,0);
        es2_hook hk[3];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<2;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("formation nested install");}
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+2,d,&o,function_hook_extension(index)))return fail("formation real install");
        memcpy(fo_arena,fo_before,FO_SIZE);fo_globals(before,1);fo_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);fo_globals(actual,0);
        if(memcmp(expected_pair,actual_pair,8)||memcmp(fo_after,fo_arena,FO_SIZE)||memcmp(after,actual,FO_GLOBALS)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            fprintf(stderr,"real mode=%u\n",mode);fo_calls=0;status=fo_report("real",n,pair,0,NULL);
        }
        for(unsigned j=0;j<3;++j){if(j<2)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("formation nested compare mismatch");
            if(!es2_hook_uninstall(hk+j))return fail("formation real uninstall");}
        hooked+=hk[2].calls;replaced+=mode>=3;
        fo_globals(before,1);++total;
    }
    /* The hook counts its calls only when non-original. */
    if(!status&&hooked!=replaced){fprintf(stderr,"FAIL: %s real pass entered %llu of %llu\n",d->name,(unsigned long long)hooked,(unsigned long long)replaced);status=1;}
    for(unsigned j=0;j<2&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    for(unsigned i=0;i<count&&!status;++i)if(c[i].count==counts[i]){fprintf(stderr,"FAIL: %s real pass never called %s\n",d->name,c[i].name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, euler_matrix/rotate_2d nested in rotating modes, hook original/replaced, 31/a7 stack, EDX:EAX, arena and globals\n",d->name,total);
        for(unsigned j=0;j<2;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
/* Compare cases (compare_probes.h), real callees. */
static void fo_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    memset(a,0,24);fo_build(index,n,1,a);for(unsigned i=0;i<3;++i)r[i]=random32();
}
static void fo_compare_state(BYTE *s,int restore){
    if(restore){memcpy(fo_arena,s,FO_SIZE);fo_globals(s+FO_SIZE,1);}else{memcpy(s,fo_arena,FO_SIZE);fo_globals(s+FO_SIZE,0);}
}
static int formation_cases(unsigned index,unsigned limit){
    BYTE saved[2][5],old_globals[FO_GLOBALS];
    fo_globals(old_globals,0);
    if(limit&&compare_probe_installed(index)){int status=compare_probe_run(index,limit,fo_compare_build,fo_compare_state,FO_SIZE+FO_GLOBALS);
        fo_globals(old_globals,1);return status;}
    if(fo_patch(saved,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_FORMATION_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6]={0},r[3];fo_build(index,n,0,a);for(unsigned i=0;i<3;++i)r[i]=random32();status=fo_compare(index,n,a,r);fo_globals(old_globals,1);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=fo_installed_probes(index,64);
    if(fo_patch(saved,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=fo_real_pass(index);
    if(!limit&&!status)status=compare_probe_run(index,0,fo_compare_build,fo_compare_state,FO_SIZE+FO_GLOBALS);
    fo_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; EDX:EAX, euler_matrix/rotate_2d calls with entry registers and inputs, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
