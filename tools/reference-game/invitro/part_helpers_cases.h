/* part_helpers group (docs/reverse-engineering/vehicle.md, section "Vehicle
 * part and weapon-group helpers"): group_fire_any 411a1d, group_fire_sel
 * 41194a, part_list_dead 40cf10, part_group_dead 40d9f8, part_group_max 40dc58,
 * mission_guard 4240f4, actor_offsets 411a74. Direct cases run the original
 * body and the core against logging stubs for the game callees of the hook
 * under test (group_fire 4107e4 under the two slot-0 bodies, 40cf10 under
 * 40d9f8, the memset thunk 49681a under 411a74): ordered calls with their
 * arguments and entry EAX/EDX/ECX, the full return pair, the arena and the
 * three guard words of the image. Part tables: in-range part and group indices
 * (the sign extension of the index words is exercised with negative indices),
 * counts 0..16 and negative. Then installed 31/a7 poison cases; real-callee
 * passes: the slot-0 bodies over weapon_helpers' real group (wph_build: the
 * real V48 bodies, weapon_step as V34, the no-op V28) with group_fire,
 * weapon_ready, weapon_step and timer_word_step nested in rotating modes;
 * part_group_dead with part_list_dead nested; actor_offsets with the harness's
 * memset under base_offsets 40351c (a hook in rotating modes, which reaches
 * 411a74 through its bridge) and called directly; and the installed compare
 * cases of compare_probes.h for the four hooks that accept compare. */
typedef struct ph_record { uint32_t id,args[4],regs[3],eax,edx; } ph_record;
enum { PH_SIZE=0x800, PH_GROUP=0x40, PH_OWNER=0x60, PH_P=0x80, PH_TABS=0xa0, PH_VALUES=0x100, PH_GROUPS=0x180,
    PH_MAXIMA=0x200, PH_RECORDS=0x300, PH_LISTS=0x400, PH_X=0x600, PH_CONFIG=0x640, PH_REC=0x700, PH_MAX=8, PH_IDS=3,
    PH_GLOBALS=12 };
#define PH_B ((uint32_t)(uintptr_t)ph_arena)
static BYTE ph_arena[PH_SIZE],ph_before[PH_SIZE],ph_after[PH_SIZE];
static ph_record ph_log[PH_MAX];
static unsigned ph_calls;
static uint32_t ph_seed,ph_stream;
__attribute__((used)) static uint32_t ph_ecx;
/* 0 group_fire 4107e4, 1 40cf10, 2 memset thunk 49681a. */
static const uint32_t ph_addresses[PH_IDS]={0x4107e4,0x40cf10,0x49681a};
static const unsigned ph_counts[PH_IDS]={4,2,3};
static uint32_t ph_next(void){
    uint32_t z=ph_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *ph_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int ph_in(uint32_t p,uint32_t n){return p-PH_B<PH_SIZE&&PH_SIZE-(p-PH_B)>=n;}
__attribute__((used,noinline)) static uint64_t __cdecl ph_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(ph_calls==PH_MAX){fail("part_helpers synthetic call overflow");ExitProcess(1);}
    ph_record *v=ph_log+ph_calls++;memset(v,0,sizeof(*v));v->id=id;
    for(unsigned k=0;k<ph_counts[id];++k)v->args[k]=a[k];
    for(unsigned j=0;j<3;++j)v->regs[j]=regs[j];
    uint32_t eax=ph_next(),edx=ph_next(),pick=ph_next();ph_ecx=ph_next();
    /* memset: the six bytes are cleared (a record outside the arena is
     * left alone); its EAX is the second call's entry EAX. */
    if(id==2&&a[2]<=64&&ph_in(a[0],a[2]))memset(ph_host(a[0]),0,a[2]);
    if(id==2&&pick%2)eax=a[0];
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define PH_STUB(n) __attribute__((naked)) static void ph_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _ph_stub_impl\n\taddl $24,%esp\n\tmovl _ph_ecx,%ecx\n\tret\n\t");}
PH_STUB(0) PH_STUB(1) PH_STUB(2)
#undef PH_STUB
static void *ph_stubs[PH_IDS]={ph_stub_0,ph_stub_1,ph_stub_2};
static unsigned ph_which(unsigned index){
    if(index==ES2_HOOK_INDEX_group_fire_any||index==ES2_HOOK_INDEX_group_fire_sel)return 0x1u;
    if(index==ES2_HOOK_INDEX_part_group_dead)return 0x2u;
    if(index==ES2_HOOK_INDEX_actor_offsets)return 0x4u;
    return 0;
}
static void ph_reset(void){ph_calls=0;ph_stream=ph_seed;}
/* The guard words 4a9e52, 4a9e5a, 4a9e5c (with the bytes between). */
static void ph_globals(BYTE *out,int restore){if(restore)memcpy(mapped+0xa9e52,out,PH_GLOBALS);else memcpy(out,mapped+0xa9e52,PH_GLOBALS);}
static uint16_t ph_word(const uint16_t *values,unsigned count){return values[random32()%count];}
/* A signed index word in -range..span-1, now and then with a random high
 * word (the functions sign-extend the low word). */
static uint32_t ph_index(int range,unsigned span){
    uint32_t low=(uint32_t)((int)(random32()%(span+(unsigned)range))-range)&0xffffu;
    return random32()%4?low|(random32()&0xffff0000u):low|(low&0x8000u?0xffff0000u:0);
}
/* Part tables P: values and group values (indices -8..23), group records
 * (indices -4..12; base word, count +6, list +8) with their entry lists
 * (part index word +2 in -8..23), part maxima. Counts 0..16 or negative;
 * real: counts 0..8. Values are negative three times out of four, so the
 * walk of 40cf10 reaches its end. */
static void ph_tables(int real){
    const uint32_t b=PH_B;
    put32(ph_arena+PH_P,b+PH_VALUES);put32(ph_arena+PH_P+4,b+PH_GROUPS);put32(ph_arena+PH_P+0xc,b+PH_TABS);
    put32(ph_arena+PH_TABS,b+PH_RECORDS);put32(ph_arena+PH_TABS+4,b+PH_MAXIMA);
    static const uint16_t counts[]={0,1,2,3,8,16,0x8000,0xffff,0x8001};
    for(int k=-4;k<=12;++k){BYTE *r=ph_arena+PH_RECORDS+18*k;
        put16(r+6,(uint16_t)(real?random32()%9:random32()%3?random32()%17:ph_word(counts,9)));
        put32(r+8,b+PH_LISTS+32u*(unsigned)(k+4)%0x1e0u);
        if(random32()%8==0)put16(r,(uint16_t)random32());else put16(r,(uint16_t)(random32()%4000));}
    for(unsigned j=0;j<0x200/4;++j)put16(ph_arena+PH_LISTS+4*j+2,(uint16_t)((int)(random32()%32)-8));
    for(int k=-8;k<24;++k){
        put16(ph_arena+PH_VALUES+2*k,(uint16_t)(random32()%4?0x8000u|random32():random32()%2?0:random32()&0x7fffu));
        put16(ph_arena+PH_GROUPS+2*k,(uint16_t)(random32()%2?0x8000u|random32():random32()%2?0:random32()&0x7fffu));
        put16(ph_arena+PH_MAXIMA+2*k,(uint16_t)(random32()%4?random32()%3000:random32()));}
}
static void ph_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    const uint32_t b=PH_B;
    ph_seed=random32();random_bytes(ph_arena,PH_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    ph_tables(real);
    if(index==ES2_HOOK_INDEX_group_fire_any||index==ES2_HOOK_INDEX_group_fire_sel){
        static const BYTE selected[]={0xff,0x80,0x00,0x09,0x7f,0x01,0xfe};
        ph_arena[PH_GROUP+0x1d]=random32()%2?selected[random32()%7]:(BYTE)random32();
        a[0]=b+PH_GROUP;a[1]=random32();a[2]=random32()%4?b+PH_OWNER:random32();
        if(n<16)a[1]=(a[1]&0xffff0000u)|(n%2?0xffffu:0x8000u);
    } else if(index==ES2_HOOK_INDEX_part_list_dead){
        a[0]=b+PH_RECORDS+18u*(uint32_t)((int)(random32()%17)-4);a[1]=b+PH_VALUES;
    } else if(index==ES2_HOOK_INDEX_part_group_dead||index==ES2_HOOK_INDEX_part_group_max){
        a[0]=b+PH_P;
        a[1]=ph_index(4,13);
    } else if(index==ES2_HOOK_INDEX_mission_guard){
        static const uint16_t words[]={0,1,2,0xffff,0x100,0x8000};
        for(unsigned i=0;i<3;++i){const unsigned at=i==0?0:i==1?8:10;
            put16(mapped+0xa9e52+at,(uint16_t)(random32()%4?ph_word(words,random32()%2?2:6):random32()));}
        if(n<8){put16(mapped+0xa9e52,(uint16_t)(n&1?0:0x100));put16(mapped+0xa9e5a,(uint16_t)(n&2?1:0x101));put16(mapped+0xa9e5c,(uint16_t)(n&4?0:0x100));}
    } else {
        /* actor_offsets: two records in the arena, sometimes the same or
         * overlapping; the first argument is never read. */
        a[0]=random32()%2?b+PH_X:random32();a[1]=b+PH_REC+random32()%32;
        a[2]=random32()%4?b+PH_REC+0x40+random32()%32:a[1]+random32()%8;
    }
}
static int ph_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const ph_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,ph_calls);
    for(unsigned j=0;j<calls&&j<ph_calls&&expected;++j)if(memcmp(expected+j,ph_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(ph_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<PH_SIZE;++j)if(ph_after[j]!=ph_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,ph_after[j],ph_arena[j]);break;}
    return 1;
}
static unsigned ph_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<ph_calls;++j)if(ph_addresses[ph_log[j].id]==c->key)++hits;
    return hits;
}
static int ph_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[PH_GLOBALS],after[PH_GLOBALS],actual[PH_GLOBALS];static ph_record expected[PH_MAX];
    memcpy(ph_before,ph_arena,PH_SIZE);ph_globals(before,0);
    ph_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=ph_calls;
    memcpy(expected,ph_log,calls*sizeof(*expected));memcpy(ph_after,ph_arena,PH_SIZE);ph_globals(after,0);
    memcpy(ph_arena,ph_before,PH_SIZE);ph_globals(before,1);ph_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[2]={0,0};for(unsigned i=0;i<count&&i<2;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);ph_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count&&i<2;++i)if(c[i].count-counts[i]!=ph_expected_hits(c+i))return fail("part_helpers callee counter delta");
    if(pair[0]!=pair[1]||calls!=ph_calls||memcmp(expected,ph_log,calls*sizeof(*expected))||memcmp(ph_after,ph_arena,PH_SIZE)||memcmp(after,actual,PH_GLOBALS)){
        for(unsigned j=0;j<PH_GLOBALS;++j)if(after[j]!=actual[j])fprintf(stderr," guard byte %u %02x/%02x\n",j,after[j],actual[j]);
        return ph_report("direct",n,pair,calls,expected);
    }
    return 0;
}
static int ph_patch(BYTE saved[PH_IDS][5],int active[PH_IDS],unsigned which,int on){
    for(unsigned i=0;i<PH_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(ph_addresses[i]-0x400000u,ph_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(ph_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static int ph_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;ph_build(index,n,a,r,0);
        BYTE before[PH_GLOBALS],after[PH_GLOBALS],actual[PH_GLOBALS],expected_fpu[108],actual_fpu[108];static ph_record expected[PH_MAX];
        memcpy(ph_before,ph_arena,PH_SIZE);ph_globals(before,0);
        ph_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=ph_calls;memcpy(expected,ph_log,calls*sizeof(*expected));memcpy(ph_after,ph_arena,PH_SIZE);ph_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("part_helpers poison install");
        memcpy(ph_arena,ph_before,PH_SIZE);ph_globals(before,1);ph_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);es2_hook_fpu_save(actual_fpu);ph_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=ph_calls||memcmp(expected,ph_log,calls*sizeof(*expected))||memcmp(ph_after,ph_arena,PH_SIZE)||
           memcmp(after,actual,PH_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=ph_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("part_helpers poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/arena/guard words\n",d->name,total);
    return status;
}
/* Real callees. The slot-0 bodies: weapon_helpers' real group (wph_build
 * for group_fire, real = 1; the selected byte G+1Dh from its preferred
 * index, so s8 <= 9 as group_fire's domain requires), group_fire with
 * weapon_ready, weapon_step and timer_word_step nested; the pair as
 * weapon_helpers compares group_fire's (EAX low word for a stack address,
 * which moves with the depth group_fire runs at). part_group_dead: the real
 * 40cf10 as part_list_dead in its modes. actor_offsets: the harness's
 * memset, under base_offsets (odd cases; X+1F2h -> a configuration whose
 * word +2Ch base_offsets copies) or called directly (even cases). The hook
 * under test original, replaced or compare (where accepted), each with
 * three rotations of the nested modes (compare falls back to replaced
 * where refused). Pair and memory against a run without the hook under
 * test; the hook and every nested hook must be entered, no compare may
 * mismatch. */
static unsigned ph_nested(unsigned index,unsigned nested[4]){
    if(index==ES2_HOOK_INDEX_part_group_dead){nested[0]=ES2_HOOK_INDEX_part_list_dead;return 1;}
    if(index==ES2_HOOK_INDEX_actor_offsets){nested[0]=ES2_HOOK_INDEX_base_offsets;return 1;}
    nested[0]=ES2_HOOK_INDEX_group_fire;nested[1]=ES2_HOOK_INDEX_weapon_ready;nested[2]=ES2_HOOK_INDEX_weapon_step;
    nested[3]=ES2_HOOK_INDEX_timer_word_step;return 4;
}
static int ph_real_pass(unsigned index){
    unsigned nested[4],count=ph_nested(index,nested);
    const int fire=index==ES2_HOOK_INDEX_group_fire_any||index==ES2_HOOK_INDEX_group_fire_sel;
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    int status=0;unsigned total=0,under=0;uint64_t entered[5]={0};
    for(unsigned mode=0;mode<9&&!status;++mode)for(unsigned n=0;n<256&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;
        BYTE *arena=fire?wph_arena:ph_arena,*saved=fire?wph_before:ph_before,*post=fire?wph_after:ph_after;
        BYTE before[WPH_GLOBALS+PH_GLOBALS],after[WPH_GLOBALS+PH_GLOBALS],actual[WPH_GLOBALS+PH_GLOBALS];
        size_t size=PH_SIZE;void *target=mapped+d->rva;
        if(fire){
            uint32_t w[6];wph_build(ES2_HOOK_INDEX_group_fire,n,w,r,1);size=wph_size;
            const uint16_t preferred=(uint16_t)w[2];
            wph_arena[WPH_GROUP+0x1d]=(BYTE)(preferred==0xffffu?0xffu:preferred&0x8000u?0x80u|preferred:preferred);
            memset(a,0,24);a[0]=w[0];a[1]=w[1];a[2]=w[3];
        } else ph_build(index,n,a,r,1);
        if(index==ES2_HOOK_INDEX_actor_offsets&&n&1){
            /* base_offsets(X, A, B): the configuration X+1F2h, word +2Ch. */
            a[0]=PH_B+PH_X;put32(ph_arena+PH_X+0x1f2,PH_B+PH_CONFIG);
            target=mapped+function_hook_descriptor(ES2_HOOK_INDEX_base_offsets)->rva;++under;
        }
        memcpy(saved,arena,size);wph_globals(before,0);ph_globals(before+WPH_GLOBALS,0);
        es2_hook hk[5];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!nested_install_hook(hk+j,nested[j],&o,e))return fail("part_helpers nested install");}
        sg_poison_call(target,a,pattern,expected_pair);
        memcpy(post,arena,size);wph_globals(after,0);ph_globals(after+WPH_GLOBALS,0);
        o.mode=(es2_hook_mode)(mode/3);if(x->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+4,d,&o,x))return fail("part_helpers hook install");
        memcpy(arena,saved,size);wph_globals(before,1);ph_globals(before+WPH_GLOBALS,1);
        sg_poison_call(target,a,pattern,actual_pair);wph_globals(actual,0);ph_globals(actual+WPH_GLOBALS,0);
        const int same=fire?wph_pair_equal(ES2_HOOK_INDEX_group_fire,expected_pair[0],expected_pair[1],actual_pair[0],actual_pair[1]):
            expected_pair[0]==actual_pair[0]&&expected_pair[1]==actual_pair[1];
        if(!same||memcmp(post,arena,size)||memcmp(after,actual,sizeof(after))){
            fprintf(stderr,"FAIL: %s case %u (real mode=%u) pair %08x%08x/%08x%08x\n",d->name,n,mode,expected_pair[1],expected_pair[0],actual_pair[1],actual_pair[0]);
            for(size_t j=0;j<size;++j)if(post[j]!=arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",(unsigned)j,post[j],arena[j]);break;}
            status=1;
        }
        for(unsigned j=0;j<5;++j){if(j<count||j==4)entered[j]+=hk[j].calls;
            if(hk[j].mismatches){fprintf(stderr,"FAIL: %s case %u (real mode=%u) compare mismatch in %s\n",d->name,n,mode,hk[j].descriptor->name);status=1;}
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("part_helpers nested uninstall");}
        wph_globals(before,1);ph_globals(before+WPH_GLOBALS,1);++total;
    }
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&!entered[4]){fprintf(stderr,"FAIL: %s never entered\n",d->name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases (%llu hook calls, %u under base_offsets), %u nested hook(s) in rotating modes, hook original/replaced/compare, 31/a7 stack, pair and memory\n",
        d->name,total,(unsigned long long)entered[4],under,count);
    return status;
}
/* Compare cases (compare_probes.h) for part_list_dead, part_group_dead
 * (real 40cf10), part_group_max and mission_guard on real-mode tables. */
static void ph_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){ph_build(index,n,a,r,1);}
static void ph_compare_state(BYTE *s,int restore){
    if(restore){memcpy(ph_arena,s,PH_SIZE);ph_globals(s+PH_SIZE,1);}else{memcpy(s,ph_arena,PH_SIZE);ph_globals(s+PH_SIZE,0);}
}
static int part_helpers_cases(unsigned index,unsigned limit){
    BYTE old_globals[PH_GLOBALS];ph_globals(old_globals,0);
    if(limit&&compare_probe_installed(index)){int status=compare_probe_run(index,limit,ph_compare_build,ph_compare_state,PH_SIZE+PH_GLOBALS);
        ph_globals(old_globals,1);return status;}
    unsigned which=ph_which(index);
    BYTE saved[PH_IDS][5];int active[PH_IDS];
    if(ph_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_PART_HELPERS_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];ph_build(index,n,a,r,0);status=ph_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=ph_installed_probes(index,64);
    if(ph_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status&&which)status=ph_real_pass(index);
    if(!limit&&!status&&compare_probe_capable(index))status=compare_probe_run(index,0,ph_compare_build,ph_compare_state,PH_SIZE+PH_GLOBALS);
    ph_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, arena/guard words\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
