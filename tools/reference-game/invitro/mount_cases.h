/* Mount group: sincos_pair 47eb30, rotate_2d 47ffe8, mount_offset 40f540 and
 * mount_base 40f904 (player_movement.md and weapons.md, "weapon mount helper
 * 40f540 and 40f904"). Direct cases run the original body and the core on
 * generated states: sincos_pair over the whole 16-bit angle domain (repeated,
 * random slot high words, unaligned outputs), rotate_2d on edge and random
 * dwords and words with out overlapping v or m, mount_base on every side class
 * with the guard byte clear or set and M's words overlapping the static vector,
 * mount_offset against a logging stub for 40f904 (ordered arguments, the pushed
 * slot count (weapons_bridge_slots) and entry EAX/EDX/ECX; the stub returns
 * vectors over the offset words and changes the definition pointer and its
 * words before the reread). The static vector 4a9af0 and the guard byte 498f34
 * are compared (with a window around each) and restored afterwards. Then
 * installed 31/a7 poison probes, and real-caller passes: the original wrappers
 * 488dc7/488ddd over sincos_pair/rotate_2d, and the real 40f540 over the real
 * 40f904 with mount_base nested and mount_offset original or replaced. The
 * two leaves also run compare_probes.h's installed compare cases and
 * compare mode under their wrappers. */
typedef struct mt_record { uint32_t args[2],regs[3],eax,edx,count; } mt_record;
enum { MT_SIZE=0x200, MT_W=0x20, MT_C=0x40, MT_C2=0x70, MT_M=0xa0, MT_V=0x100, MT_OUT=0x140, MT_MAX=4,
    MT_VECTOR=0xa9ae8, MT_VECTOR_SIZE=24, MT_GUARD=0x98f30, MT_GUARD_SIZE=8, MT_GLOBALS=MT_VECTOR_SIZE+MT_GUARD_SIZE };
#define MT_BASE ((uint32_t)(uintptr_t)mt_arena)
static BYTE mt_arena[MT_SIZE],mt_before[MT_SIZE],mt_after[MT_SIZE];
static mt_record mt_log[MT_MAX];
static unsigned mt_calls;
static uint32_t mt_seed,mt_stream;
__attribute__((used)) static uint32_t mt_ecx;
static uint32_t mt_next(void){
    uint32_t z=mt_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static void mt_reset(void){mt_calls=0;mt_stream=mt_seed;}
static void mt_globals(BYTE *out,int restore){
    if(restore){memcpy(mapped+MT_VECTOR,out,MT_VECTOR_SIZE);memcpy(mapped+MT_GUARD,out+MT_VECTOR_SIZE,MT_GUARD_SIZE);}
    else {memcpy(out,mapped+MT_VECTOR,MT_VECTOR_SIZE);memcpy(out+MT_VECTOR_SIZE,mapped+MT_GUARD,MT_GUARD_SIZE);}
}
/* 40f904 stub: logs M, side and the entry registers; returns V in the
 * arena (sometimes over the definition's offset words), writes V's three
 * words, and may move [W+18h] or change an offset word before 40f540
 * rereads them. */
__attribute__((used,noinline)) static uint64_t __cdecl mt_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    (void)id;uint32_t b=MT_BASE,eax,edx=mt_next(),pick=mt_next();mt_ecx=mt_next();
    if(mt_calls==MT_MAX){fail("mount synthetic call overflow");ExitProcess(1);}
    mt_record *v=mt_log+mt_calls++;memset(v,0,sizeof(*v));
    v->args[0]=a[0];v->args[1]=a[1];for(unsigned j=0;j<3;++j)v->regs[j]=regs[j];
    v->count=weapons_bridge_slots(a,2);
    switch(pick%8){
    case 0:case 1:eax=b+MT_C+0x0cu+mt_next()%12u;break;   /* V over C+10h..15h */
    case 2:eax=b+MT_C2+0x0cu+mt_next()%12u;break;         /* over the other definition */
    default:eax=b+MT_V+mt_next()%8u;break;
    }
    for(unsigned j=0;j<3;++j)put16((BYTE *)(uintptr_t)(eax+2*j),(uint16_t)mt_next());
    if(pick&8)put32(mt_arena+MT_W+0x18,b+MT_C2+mt_next()%4u);
    if(pick&16)put16(mt_arena+MT_C+0x10+2*(mt_next()%3u),(uint16_t)mt_next());
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
__attribute__((naked)) static void mt_stub(void){__asm__ volatile(
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
    "pushl $0\n\tcalll _mt_stub_impl\n\taddl $24,%esp\n\tmovl _mt_ecx,%ecx\n\tret\n\t");}
static uint32_t mt_dword(void){
    static const uint32_t edges[]={0,1,0xffffffffu,0x80000000u,0x7fffffffu,0x2000,0xffffe000u,0x4000,0x10000,0xffff0000u,0x3fff,0xffffc001u};
    switch(random32()%4){
    case 0:return edges[random32()%12];
    case 1:return random32()%0x20001u-0x10000u;
    case 2:return random32()%0x2000001u-0x1000000u;
    default:return random32();
    }
}
static uint16_t mt_word(void){
    static const uint16_t edges[]={0,1,0xffff,0x4000,0xc000,0x7fff,0x8000,0x8001,0x3fff,0x2000,0xe000,0x2d41};
    return random32()%2?edges[random32()%12]:(uint16_t)random32();
}
static void mt_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    uint32_t b=MT_BASE;mt_seed=random32();random_bytes(mt_arena,MT_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    if(index==ES2_HOOK_INDEX_sincos_pair){
        /* The whole word domain, repeated; the slot's high word is ignored. */
        a[0]=(random32()&0xffff0000u)|(n&0xffffu);a[1]=b+MT_OUT+random32()%8u;return;
    }
    if(index==ES2_HOOK_INDEX_rotate_2d){
        uint32_t v=b+MT_V+random32()%4u,m=b+MT_OUT+random32()%4u,out=b+MT_OUT+0x20u+random32()%4u;
        unsigned overlap=random32()%16;
        if(overlap==6)m=v+random32()%8u;   /* the matrix over the vector */
        put32((BYTE *)(uintptr_t)v,mt_dword());put32((BYTE *)(uintptr_t)(v+4),mt_dword());
        for(unsigned j=0;j<4;++j)put16((BYTE *)(uintptr_t)(m+2*j),mt_word());
        switch(overlap){
        case 0:out=v;break;
        case 1:out=v+4u;break;
        case 2:out=v-4u;break;
        case 3:out=v+1u+random32()%7u;break;
        case 4:out=m;break;
        case 5:out=m-2u+random32()%8u;break;
        default:break;
        }
        a[0]=v;a[1]=m;a[2]=out;return;
    }
    if(index==ES2_HOOK_INDEX_mount_base){
        static const uint16_t sides[]={0,1,2,3,4,5,6,0xffff,0xfffe,0x7fff,0x8000,0xfffc,0x100,0x104};
        uint16_t side=n<1024?(uint16_t)(n%8u):random32()%4?sides[random32()%14]:(uint16_t)random32();
        uint32_t M=b+MT_M+random32()%4u;
        mapped[0x98f34]=(BYTE)(random32()%2?0:random32()|1u);
        random_bytes(mapped+0xa9af0,6);
        /* M+46h..4Bh over V (inside the compared window 4a9ae8..4a9aff). */
        if(random32()%16==0)M=(uint32_t)(uintptr_t)(mapped+0xa9af0)-0x4au+random32()%11u-4u;
        BYTE *p=(BYTE *)(uintptr_t)M;
        if(random32()%2){put32(p+0x46,mt_dword());put16(p+0x4a,mt_word());}
        a[0]=M;a[1]=(random32()&0xffff0000u)|side;return;
    }
    /* mount_offset: W, its definition C and carrier M. */
    static const BYTE sides[]={0,1,2,3,4,5,0x7f,0x80,0xff,0xfe,0xfc};
    BYTE *c=mt_arena+MT_C;
    put32(mt_arena+MT_W+0x18,b+MT_C);put32(mt_arena+MT_W+0x1c,b+MT_M+(random32()%4?0:random32()%4u));
    c[6]=random32()%4?sides[random32()%11]:(BYTE)random32();
    for(unsigned j=0;j<3;++j)put16(c+0x10+2*j,mt_word());
    a[0]=b+MT_W;
}
static int mt_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const mt_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,mt_calls);
    for(unsigned j=0;j<calls&&j<mt_calls&&expected;++j)if(memcmp(expected+j,mt_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(mt_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u word%u %08x/%08x\n",j,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<MT_SIZE;++j)if(mt_after[j]!=mt_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,mt_after[j],mt_arena[j]);break;}
    return 1;
}
static int mt_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<MT_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
static int mt_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    static mt_record expected[MT_MAX];BYTE before[MT_GLOBALS],after[MT_GLOBALS],actual[MT_GLOBALS];
    memcpy(mt_before,mt_arena,MT_SIZE);mt_globals(before,0);
    mt_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);
    unsigned calls=mt_calls;memcpy(expected,mt_log,calls*sizeof(*expected));
    memcpy(mt_after,mt_arena,MT_SIZE);mt_globals(after,0);
    memcpy(mt_arena,mt_before,MT_SIZE);mt_globals(before,1);mt_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[4]={0};
    for(unsigned i=0;i<count&&i<4;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);mt_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count&&i<4;++i)if(c[i].count-counts[i]!=calls)return fail("mount callee counter delta");
    if(pair[0]!=pair[1]||calls!=mt_calls||memcmp(expected,mt_log,calls*sizeof(*expected))||memcmp(mt_after,mt_arena,MT_SIZE)||
       mt_globals_differ(after,actual))
        return mt_report("direct",n,pair,calls,expected);
    return 0;
}
static int mt_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;mt_build(index,n,a,r);
        BYTE before[MT_GLOBALS],after[MT_GLOBALS],actual[MT_GLOBALS],expected_fpu[108],actual_fpu[108];static mt_record expected[MT_MAX];
        memcpy(mt_before,mt_arena,MT_SIZE);mt_globals(before,0);
        mt_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=mt_calls;memcpy(expected,mt_log,calls*sizeof(*expected));memcpy(mt_after,mt_arena,MT_SIZE);mt_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("mount poison install");
        memcpy(mt_arena,mt_before,MT_SIZE);mt_globals(before,1);mt_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);es2_hook_fpu_save(actual_fpu);mt_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=mt_calls||memcmp(expected,mt_log,calls*sizeof(*expected))||memcmp(mt_after,mt_arena,MT_SIZE)||
           mt_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=mt_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("mount poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/arena/globals\n",d->name,total);
    return status;
}
/* Compare cases (compare_probes.h) for the leaves. rotate_2d's out is moved off
 * v and m (no game caller overlaps them; an out that v or m starts inside is
 * skipped by the framework, a partial overlap would read the original's stores
 * in the replacement phase). */
static void mt_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    mt_build(index,n,a,r);
    if(index==ES2_HOOK_INDEX_rotate_2d)a[2]=MT_BASE+MT_OUT+0x20u+random32()%4u;
}
static void mt_compare_state(BYTE *s,int restore){
    if(restore){memcpy(mt_arena,s,MT_SIZE);mt_globals(s+MT_SIZE,1);}else{memcpy(s,mt_arena,MT_SIZE);mt_globals(s+MT_SIZE,0);}
}
/* Real callers. sincos_pair under the wrapper 488dc7(m, angle), rotate_2d
 * under 488ddd(v, m, out); mount_offset and mount_base: the real 40f540
 * over the real 40f904. The nested hook is original or replaced, for the
 * leaves also compare (mode 2, disjoint rotate_2d out); mount_offset
 * original or replaced; 31/a7 stack. */
static int mt_real(unsigned index){
    const int mount=index==ES2_HOOK_INDEX_mount_offset||index==ES2_HOOK_INDEX_mount_base;
    const unsigned nested=mount?ES2_HOOK_INDEX_mount_base:index,build=mount?ES2_HOOK_INDEX_mount_offset:index;
    const uint32_t caller=index==ES2_HOOK_INDEX_sincos_pair?0x88dc7u:index==ES2_HOOK_INDEX_rotate_2d?0x88dddu:0x0f540u;
    int status=0;unsigned total=0;uint64_t entered=0,outer=0,compared=0;
    for(unsigned mode=0;mode<4&&!status;++mode)for(unsigned n=0;n<768&&!status;++n){
        const int compare=!mount&&mode==2;
        uint32_t a[6],r[3],w[6]={0},expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;
        if(compare)mt_compare_build(build,random32(),a,r);else mt_build(build,mount?n:random32(),a,r);
        if(index==ES2_HOOK_INDEX_sincos_pair){w[0]=a[1];w[1]=a[0];}else memcpy(w,a,sizeof(w));
        if(mount){mapped[0x98f34]=(BYTE)(n%3?0:n|1u);if(n%2){BYTE *p=(BYTE *)(uintptr_t)get32(mt_arena+MT_W+0x1c);
            put32(p+0x46,mt_dword());put16(p+0x4a,mt_word());}}
        BYTE before[MT_GLOBALS],after[MT_GLOBALS],actual[MT_GLOBALS];
        memcpy(mt_before,mt_arena,MT_SIZE);mt_globals(before,0);
        sg_poison_call(mapped+caller,w,pattern,expected_pair);memcpy(mt_after,mt_arena,MT_SIZE);mt_globals(after,0);
        es2_hook nest={0},outside={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        o.mode=mode&1?ES2_HOOK_REPLACED:compare?ES2_HOOK_COMPARE:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(&nest,function_hook_descriptor(nested),&o,function_hook_extension(nested)))return fail("mount nested install");
        if(mount){o.mode=mode&2?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(&outside,function_hook_descriptor(ES2_HOOK_INDEX_mount_offset),&o,
                function_hook_extension(ES2_HOOK_INDEX_mount_offset)))return fail("mount caller install");}
        memcpy(mt_arena,mt_before,MT_SIZE);mt_globals(before,1);
        sg_poison_call(mapped+caller,w,pattern,actual_pair);mt_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||memcmp(mt_after,mt_arena,MT_SIZE)||mt_globals_differ(after,actual)){
            fprintf(stderr,"real mode=%u\n",mode);mt_calls=0;status=mt_report("real",n,pair,0,NULL);}
        if(mode&1)entered+=nest.calls;
        if(compare){compared+=nest.calls-nest.skipped;if(nest.mismatches||nest.bypassed)status=fail("mount nested compare mismatch");}
        if(mount&&(mode&2))outer+=outside.calls;
        if(mount&&!es2_hook_uninstall(&outside))return fail("mount caller uninstall");
        if(!es2_hook_uninstall(&nest))return fail("mount nested uninstall");
        mt_globals(before,1);++total;
    }
    if(!status&&(!entered||(mount&&!outer)||(!mount&&!compared))){
        fprintf(stderr,"FAIL: %s real pass entered %llu/%llu, compared %llu\n",function_hook_descriptor(index)->name,(unsigned long long)entered,
            (unsigned long long)outer,(unsigned long long)compared);status=1;}
    if(!status)printf("PASS: %s %u real-caller cases under %06x (%s nested, %llu replaced entries, %llu compared without mismatch), 31/a7 stack, pair/arena/globals\n",
        function_hook_descriptor(index)->name,total,0x400000u+caller,function_hook_descriptor(nested)->name,(unsigned long long)entered,(unsigned long long)compared);
    return status;
}
static int mount_cases(unsigned index,unsigned limit){
    BYTE saved_globals[MT_GLOBALS],saved_stub[5];mt_globals(saved_globals,0);
    const int leaf=index==ES2_HOOK_INDEX_sincos_pair||index==ES2_HOOK_INDEX_rotate_2d;
    if(limit&&leaf&&compare_probe_installed(index)){int status=compare_probe_run(index,limit,mt_compare_build,mt_compare_state,MT_SIZE+MT_GLOBALS);
        mt_globals(saved_globals,1);return status;}
    const int stubbed=index==ES2_HOOK_INDEX_mount_offset;
    if(stubbed&&actor_patch(0x0f904,mt_stub,saved_stub,TRUE))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_MOUNT_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    const unsigned instrumented=index==ES2_HOOK_INDEX_sincos_pair?65536:16384;
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==instrumented)coverage_enable(0);
        uint32_t a[6],r[3];mt_build(index,n,a,r);status=mt_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    if(!limit&&!status)status=mt_installed_probes(index,64);
    if(stubbed&&actor_patch(0x0f904,NULL,saved_stub,FALSE))return 1;
    if(!limit&&!status)status=mt_real(index);
    if(!limit&&!status&&leaf)status=compare_probe_run(index,0,mt_compare_build,mt_compare_state,MT_SIZE+MT_GLOBALS);
    mt_globals(saved_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered 40f904 calls with entry registers, arena, vector 4a9af0 and guard 498f34\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
