/* building_aim group: turret_target 403eec and part_facing 406868
 * (docs/reverse-engineering/weapons.md, "building turret target 403eec and part
 * facing 406868"). Direct cases run the original body and the core against
 * logging stubs for every game callee (euler_matrix, the shape manager's slot
 * +20h, matrix_transpose, rotate_point, transform_point, aim_angles, turret_aim
 * and the building's slot +58h): ordered calls with their arguments, the pushed
 * slot count (weapons_bridge_slots) and entry EAX/EDX/ECX (stack addresses
 * masked, as in convoy_cases.h), frame payloads by content. The stubs change
 * what the originals reread (the manager pointer, the records base, the part
 * flags, the definition pointer and the viewer's angle words). The guard byte
 * 4974d5, the angle triple 4a95ec and the default record 6c56a8 are compared
 * with a window and restored. Then installed 31/a7 poison probes and a
 * real-callee pass with the nested hooks in rotating modes (turret_aim and slot
 * +20h stay stubs; part_facing runs the real building slot +58h 406808). */
typedef struct ba_record { uint32_t id,args[3],regs[3],input[12],eax,edx,count; } ba_record;
enum { BA_SIZE=0x900, BA_B=0x100, BA_VTABLE=0x320, BA_MGR=0x380, BA_MGR2=0x3a0, BA_SHAPE=0x3c0, BA_SVT=0x3d0,
    BA_RESULT=0x400, BA_RECORDS=0x420, BA_POINT=0x620, BA_DEF=0x660, BA_DEF2=0x680, BA_FLAGS=0x6a0, BA_PARTS=0x720,
    BA_MAX=96, BA_IDS=8, BA_GUARD=0x974d0, BA_ANGLES=0xa95e8, BA_RECORD0=0x2c56a8, BA_GLOBALS=8+16+32 };
#define BA_BASE ((uint32_t)(uintptr_t)ba_arena)
static BYTE ba_arena[BA_SIZE],ba_before[BA_SIZE],ba_after[BA_SIZE];
static ba_record ba_log[BA_MAX];
static unsigned ba_calls;
static uint32_t ba_seed,ba_stream;
__attribute__((used)) static uint32_t ba_ecx;
/* 47ebb8 euler_matrix, slot +20h, 47df19 matrix_transpose, 480304
 * rotate_point, 48043c transform_point, 492990 aim_angles, 403d5c
 * turret_aim, slot +58h. Slots are reached through generated vtables. */
static const uint32_t ba_addresses[BA_IDS]={0x47ebb8,0,0x47df19,0x480304,0x48043c,0x492990,0x403d5c,0};
static const unsigned ba_slots[BA_IDS]={0,0x20,0,0,0,0,0,0x58};
static const unsigned ba_counts[BA_IDS]={2,2,1,3,3,3,2,3};
static uint32_t ba_next(void){
    uint32_t z=ba_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *ba_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int ba_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static int ba_ok(uint32_t p,uint32_t n){
    const NT_TIB *t=(const NT_TIB *)NtCurrentTeb();uint32_t low=(uint32_t)(uintptr_t)t->StackLimit,top=(uint32_t)(uintptr_t)t->StackBase;
    uint32_t image=(uint32_t)(uintptr_t)mapped;
    return (p-BA_BASE<BA_SIZE&&BA_SIZE-(p-BA_BASE)>=n)||(p>=low&&p<top&&top-p>=n)||(p-image<0x2d8000u&&0x2d8000u-(p-image)>=n);
}
static void ba_dwords(ba_record *v,unsigned at,uint32_t p,unsigned n){
    if(ba_ok(p,4u*n))for(unsigned j=0;j<n;++j)v->input[at+j]=get32(ba_host(p)+4*j);
}
static void ba_words(ba_record *v,unsigned at,uint32_t p,unsigned n){
    if(ba_ok(p,2u*n))for(unsigned j=0;j<n;++j)v->input[at+j]=get16(ba_host(p)+2*j);
}
static void ba_fill(uint32_t p,unsigned n){if(ba_ok(p,n))for(unsigned j=0;j<n;++j)ba_host(p)[j]=(BYTE)ba_next();}
__attribute__((used,noinline)) static uint64_t __cdecl ba_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(ba_calls==BA_MAX){fail("building_aim synthetic call overflow");ExitProcess(1);}
    ba_record *v=ba_log+ba_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,4*ba_counts[id]);memcpy(v->regs,regs,12);
    v->count=weapons_bridge_slots(a,ba_counts[id]);
    for(unsigned j=0;j<3;++j)if(ba_stack(v->regs[j]))v->regs[j]=0;
    for(unsigned j=0;j<ba_counts[id];++j)if(ba_stack(v->args[j]))v->args[j]=0;
    uint32_t eax=ba_next(),edx=ba_next(),pick=ba_next(),b=BA_BASE;ba_ecx=ba_next();
    BYTE *o=ba_arena+BA_B;
    switch(id){
    case 0: /* euler_matrix: angles by content; matrix bytes 0..18 rewritten
             * (byte 19 is padding); the manager pointer may move. */
        ba_words(v,0,a[0],3);ba_fill(a[1],19);
        if(pick%5==0)put32(o+0x34,b+(pick>>8&1?BA_MGR2:BA_MGR));
        break;
    case 1: { /* shape slot +20h: null, or a result whose index word selects
               * a record or the default; the records bases may move. */
        static const uint16_t words[]={0,1,2,3,4,5,0xffff,0x8000,0xfffe,0x7fff};
        unsigned k=pick%12;
        if(k<2)eax=0;
        else {eax=b+BA_RESULT;uint16_t w=words[(pick>>4)%10];if(w==0x7fff)w=(uint16_t)(pick>>12)%6;put16(ba_arena+BA_RESULT+4,w);}
        if(pick&0x100000u){put32(ba_arena+BA_MGR+0x16,b+BA_RECORDS+0x20);put32(ba_arena+BA_MGR2+0x16,b+BA_RECORDS+0x40);}
        break; }
    case 2: /* matrix_transpose: M by content, rewritten; EAX = M. */
        ba_dwords(v,0,a[0],8);
        if(pick%3)ba_fill(a[0],32);
        eax=a[0];
        break;
    case 3: case 4: /* rotate_point / transform_point: XYZ and M by content. */
        ba_dwords(v,0,a[0],3);ba_dwords(v,3,a[1],8);
        if(ba_ok(a[2],12))for(unsigned j=0;j<3;++j)put32(ba_host(a[2])+4*j,ba_next());
        if(id==4&&pick%9==0)ba_fill(b+BA_POINT,12);
        break;
    case 5: /* aim_angles: end and start by content; three words out. Under
             * part_facing the words are the viewer's plus edge offsets. */
        ba_dwords(v,0,a[1],3);ba_dwords(v,3,a[2],3);
        if(a[0]==(uint32_t)(uintptr_t)(mapped+0xa95ec)){for(unsigned j=0;j<3;++j)put16(ba_host(a[0])+2*j,(uint16_t)ba_next());}
        else if(ba_ok(a[0],6)){
            static const uint16_t edges[]={0x8000,0x7fff,0x8001,0,1,0xffff,0x4000,0xc000};
            uint32_t pose=a[2]-0x1au;
            for(unsigned j=0;j<3;++j){uint16_t base=ba_ok(pose,6)?get16(ba_host(pose)+2*j):0,x=ba_next()%3?edges[ba_next()%8]:(uint16_t)ba_next();
                put16(ba_host(a[0])+2*j,(uint16_t)(base+x));}
        }
        break;
    case 6: /* turret_aim: the angle words by content. */
        ba_words(v,0,a[1],3);
        break;
    case 7: { /* building slot +58h: out written; later flags, the
               * definition pointer and the viewer's words may change. */
        if(ba_ok(a[2],12))for(unsigned j=0;j<3;++j)put32(ba_host(a[2])+4*j,ba_next());
        if(pick%7==0)put16(ba_arena+BA_FLAGS+2*((a[1]+1u+(pick>>8)%3)%48u),(uint16_t)(pick>>16&1?0:ba_next()|1u));
        if(pick%11==0)put32(o+0x1f2,b+(pick>>8&1?BA_DEF2:BA_DEF));
        if(pick%13==0)put16(ba_arena+BA_POINT+2*((pick>>8)%3),(uint16_t)ba_next());
        break; }
    }
    v->eax=ba_stack(eax)?0:eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define BA_STUB(n) __attribute__((naked)) static void ba_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _ba_stub_impl\n\taddl $24,%esp\n\tmovl _ba_ecx,%ecx\n\tret\n\t");}
BA_STUB(0) BA_STUB(1) BA_STUB(2) BA_STUB(3) BA_STUB(4) BA_STUB(5) BA_STUB(6) BA_STUB(7)
#undef BA_STUB
static void *ba_stubs[BA_IDS]={ba_stub_0,ba_stub_1,ba_stub_2,ba_stub_3,ba_stub_4,ba_stub_5,ba_stub_6,ba_stub_7};
static void ba_globals(BYTE *out,int restore){
    if(restore){memcpy(mapped+BA_GUARD,out,8);memcpy(mapped+BA_ANGLES,out+8,16);memcpy(mapped+BA_RECORD0,out+24,32);}
    else {memcpy(out,mapped+BA_GUARD,8);memcpy(out+8,mapped+BA_ANGLES,16);memcpy(out+24,mapped+BA_RECORD0,32);}
}
static void ba_reset(void){ba_calls=0;ba_stream=ba_seed;}
static uint32_t ba_small(uint32_t pick,uint32_t range){return (uint32_t)((int32_t)(pick%(2u*range+1u))-(int32_t)range);}
/* Building, managers, shape object, records, viewer, definitions, flags.
 * real: translations and positions bounded (aim_angles' domain), the
 * building's slot +58h is the real 406808, at most 8 parts. */
static void ba_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    uint32_t b=BA_BASE;BYTE *o=ba_arena+BA_B;
    ba_seed=random32();random_bytes(ba_arena,BA_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    random_bytes(mapped+BA_RECORD0,32);
    mapped[0x974d5]=(BYTE)(random32()%2?0:random32()%2?1:random32());
    put32(o,b+BA_VTABLE);put32(ba_arena+BA_VTABLE+0x58,real?(uint32_t)(uintptr_t)(mapped+0x6808):(uint32_t)(uintptr_t)ba_stubs[7]);
    put16(o+0x32,(uint16_t)(random32()%2?0:random32()%2?1:random32()));
    put32(o+0x34,b+BA_MGR);
    put32(ba_arena+BA_MGR+4,b+BA_SHAPE);put32(ba_arena+BA_MGR+0x16,b+BA_RECORDS);
    put32(ba_arena+BA_MGR2+4,b+BA_SHAPE);put32(ba_arena+BA_MGR2+0x16,b+BA_RECORDS+0x20);
    put32(ba_arena+BA_SHAPE,b+BA_SVT);put32(ba_arena+BA_SVT+0x20,(uint32_t)(uintptr_t)ba_stubs[1]);
    if(real){
        for(unsigned j=0;j<3;++j){put32(o+0x26+4*j,ba_small(random32(),1u<<20));put32(ba_arena+BA_POINT+4*j,ba_small(random32(),1u<<20));
            put32(ba_arena+BA_POINT+0x1a+4*j,ba_small(random32(),1u<<20));put32(mapped+BA_RECORD0+0x14+4*j,ba_small(random32(),1u<<16));}
        for(unsigned k=0;k<10;++k)for(unsigned j=0;j<3;++j)put32(ba_arena+BA_RECORDS+32*k+0x14+4*j,ba_small(random32(),1u<<16));
    }
    /* part_facing: definition count, flags, viewer. */
    static const uint16_t counts[]={0,1,2,3,4,5,8,12,20,40,0xffff,0x8000,0xfff0};
    uint16_t count=real?(uint16_t)(random32()%9):n<1024?counts[n%13]:random32()%4?(uint16_t)(random32()%41):counts[random32()%13];
    put32(o+0x1f2,b+BA_DEF);put16(ba_arena+BA_DEF+0x12,count);put16(ba_arena+BA_DEF2+0x12,(uint16_t)(real?random32()%9:random32()%41));
    put32(ba_arena+BA_DEF+0x14,b+BA_PARTS);put32(ba_arena+BA_DEF2+0x14,b+BA_PARTS);
    put32(o+0x201,b+BA_FLAGS);
    unsigned density=random32()%4;
    for(unsigned j=0;j<64;++j)put16(ba_arena+BA_FLAGS+2*j,(uint16_t)(random32()%4<density?0:random32()%2?1:random32()|1u));
    a[0]=b+BA_B;
    if(index==ES2_HOOK_INDEX_turret_target)a[1]=b+BA_POINT;
    else a[1]=random32()%4==0?0:b+BA_POINT;
}
static int ba_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const ba_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,ba_calls);
    for(unsigned j=0;j<calls&&j<ba_calls&&expected;++j)if(memcmp(expected+j,ba_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(ba_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<BA_SIZE;++j)if(ba_after[j]!=ba_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,ba_after[j],ba_arena[j]);break;}
    return 1;
}
static int ba_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<BA_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
static unsigned ba_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<ba_calls;++j){unsigned id=ba_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?ba_slots[id]==c->key:!ba_slots[id]&&ba_addresses[id]==c->key)++hits;}
    return hits;
}
static int ba_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[BA_GLOBALS],after[BA_GLOBALS],actual[BA_GLOBALS];static ba_record expected[BA_MAX];
    memcpy(ba_before,ba_arena,BA_SIZE);ba_globals(before,0);
    ba_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=ba_calls;memcpy(expected,ba_log,calls*sizeof(*expected));
    memcpy(ba_after,ba_arena,BA_SIZE);ba_globals(after,0);
    memcpy(ba_arena,ba_before,BA_SIZE);ba_globals(before,1);ba_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];for(unsigned i=0;i<count&&i<8;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);ba_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count&&i<8;++i)if(c[i].count-counts[i]!=ba_expected_hits(c+i))return fail("building_aim callee counter delta");
    if(pair[0]!=pair[1]||calls!=ba_calls||memcmp(expected,ba_log,calls*sizeof(*expected))||memcmp(ba_after,ba_arena,BA_SIZE)||
       ba_globals_differ(after,actual))
        return ba_report("direct",n,pair,calls,expected);
    return 0;
}
static int ba_patch(BYTE saved[BA_IDS][5],int active[BA_IDS],unsigned which,int on){
    for(unsigned i=0;i<BA_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!ba_addresses[i])continue;if(actor_patch(ba_addresses[i]-0x400000u,ba_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(ba_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static int ba_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;ba_build(index,n,a,r,0);
        BYTE before[BA_GLOBALS],after[BA_GLOBALS],actual[BA_GLOBALS],expected_fpu[108],actual_fpu[108];static ba_record expected[BA_MAX];
        memcpy(ba_before,ba_arena,BA_SIZE);ba_globals(before,0);
        ba_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=ba_calls;memcpy(expected,ba_log,calls*sizeof(*expected));memcpy(ba_after,ba_arena,BA_SIZE);ba_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("building_aim poison install");
        memcpy(ba_arena,ba_before,BA_SIZE);ba_globals(before,1);ba_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);es2_hook_fpu_save(actual_fpu);ba_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=ba_calls||memcmp(expected,ba_log,calls*sizeof(*expected))||memcmp(ba_after,ba_arena,BA_SIZE)||
           ba_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=ba_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("building_aim poison uninstall");ba_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/arena/globals\n",d->name,total);
    return status;
}
/* Real callees with the nested hooks in rotating original/replaced/compare
 * modes (compare falls back to replaced where refused) and the hook itself
 * original or replaced. turret_target: real euler_matrix,
 * matrix_transpose, rotate_point, transform_point and aim_angles; the
 * shape slot +20h and turret_aim stay stubs. part_facing: the real
 * building slot +58h 406808 (euler_matrix, 4801d4) and aim_angles. Pair,
 * arena and globals against a run with only the nested hooks installed. */
static int ba_real_pass(unsigned index){
    unsigned nested[5],count=0,keep=0;
    if(index==ES2_HOOK_INDEX_turret_target){
        nested[count++]=ES2_HOOK_INDEX_euler_matrix;nested[count++]=ES2_HOOK_INDEX_matrix_transpose;nested[count++]=ES2_HOOK_INDEX_rotate_point;
        nested[count++]=ES2_HOOK_INDEX_transform_point;nested[count++]=ES2_HOOK_INDEX_aim_angles;keep=1u<<6;}
    else {nested[count++]=ES2_HOOK_INDEX_euler_matrix;nested[count++]=ES2_HOOK_INDEX_aim_angles;}
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[6]={0};
    BYTE saved[BA_IDS][5];int active[BA_IDS];
    if(ba_patch(saved,active,keep,1))return 1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;ba_build(index,n,a,r,1);
        BYTE before[BA_GLOBALS],after[BA_GLOBALS],actual[BA_GLOBALS];
        memcpy(ba_before,ba_arena,BA_SIZE);ba_globals(before,0);
        es2_hook hk[6];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("building_aim nested install");}
        ba_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        memcpy(ba_after,ba_arena,BA_SIZE);ba_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+5,d,&o,function_hook_extension(index)))return fail("building_aim caller install");
        memcpy(ba_arena,ba_before,BA_SIZE);ba_globals(before,1);ba_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);ba_globals(actual,0);
        if(expected_pair[0]!=actual_pair[0]||expected_pair[1]!=actual_pair[1]||memcmp(ba_after,ba_arena,BA_SIZE)||ba_globals_differ(after,actual)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            fprintf(stderr,"real mode=%u\n",mode);ba_calls=0;status=ba_report("real",n,pair,0,NULL);
        }
        for(unsigned j=0;j<6;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("building_aim nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("building_aim nested uninstall");}
        ba_globals(before,1);++total;
    }
    if(ba_patch(saved,active,0,0))return 1;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, 31/a7 stack, full pair, arena and globals\n",d->name,total,count);
    return status;
}
static int building_aim_cases(unsigned index,unsigned limit){
    const unsigned which=0xffu;
    BYTE saved[BA_IDS][5],old_globals[BA_GLOBALS];int active[BA_IDS];
    ba_globals(old_globals,0);if(ba_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_BUILDING_AIM_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];ba_build(index,n,a,r,0);status=ba_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=ba_installed_probes(index,64);
    if(ba_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=ba_real_pass(index);
    ba_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, frame payloads by content, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
