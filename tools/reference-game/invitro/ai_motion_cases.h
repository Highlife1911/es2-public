/* AI flyer move step flyer_move 4218c4 (docs/reverse-engineering/flyer.md, "the
 * AI flyer move step `0x4218c4`"). Direct cases run the original body and the
 * core against logging stubs for all six game callees (ordered argument slots,
 * entry EAX/EDX/ECX, the probe and transformed points by content; values in the
 * worker's stack reservation are masked). The stubs also rewrite fields the
 * original reads again after a call, so a core that caches them fails. Then
 * installed 31/a7 poison probes and a real-callee pass: euler_matrix,
 * transform_point, terrain_height, multiply_q10 and dist3d_delta are real
 * nested hooks in rotating modes on a generated terrain; only the sound call
 * stays a stub and its inputs are compared. formation_steer 41fbb8 follows
 * below (prefix fs_). */
typedef struct fm_record { uint32_t id,args[3],regs[3],input[3],eax,edx,slots; } fm_record;
enum { FM_SIZE=0x1000, FM_X=0x100, FM_CAM=0x500, FM_MAP=0x600, FM_CELLS=0x800, FM_MAX=24, FM_IDS=6, FM_GLOBALS=8 };
#define FM_BASE ((uint32_t)(uintptr_t)fm_arena)
static BYTE fm_arena[FM_SIZE],fm_before[FM_SIZE],fm_after[FM_SIZE];
static fm_record fm_log[FM_MAX];
static unsigned fm_calls;
static int fm_real;
static uint32_t fm_seed,fm_stream;
__attribute__((used)) static uint32_t fm_ecx;
/* 0 47ebb8 euler_matrix, 1 48043c transform_point, 2 46e180 terrain_height,
 * 3 47e0b0 multiply_q10, 4 47de72 dist3d_delta, 5 462830 sound. */
static const uint32_t fm_addresses[FM_IDS]={0x47ebb8,0x48043c,0x46e180,0x47e0b0,0x47de72,0x462830};
static const unsigned fm_counts[FM_IDS]={2,3,2,2,3,1};
static uint32_t fm_next(void){
    uint32_t z=fm_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *fm_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int fm_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static void fm_vector(uint32_t at,uint32_t *in){for(unsigned j=0;j<3;++j)in[j]=get32(fm_host(at)+4*j);}
/* Fields of X that the original reads again after a call; a stub may
 * rewrite one of them (cache word, angles, rates, latch, speed, position). */
static void fm_scribble(uint32_t pick){
    static const unsigned words[]={0x32,0xc,0xe,0x243+0x18,0x243+0x1a,6};
    BYTE *x=fm_arena+FM_X;
    switch(pick%9){
    case 0:case 1:case 2:case 3:case 4:case 5:put16(x+words[pick%9],(uint16_t)(fm_next()%3?fm_next():0));break;
    case 6:put32(x+0x243+4,fm_next()%2?0:fm_next());break;
    case 7:put32(x+0x26+4*((pick>>8)%3),fm_next());break;
    default:put32(x+0x243+0xc+4*((pick>>8)%3),fm_next());break;
    }
}
/* A height relative to the point's z: equal, just above/below, about 500
 * below (the clearance test), far, or unrelated. */
static uint32_t fm_height(uint32_t z,uint32_t pick){
    switch(pick%6){
    case 0:return z+(uint32_t)((int32_t)(pick>>8)%3-1);
    case 1:return z-500u+(uint32_t)((int32_t)(pick>>8)%3-1);
    case 2:return z+(fm_next()%4000u)-2000u;
    case 3:return z+(fm_next()%0x40000u)-0x20000u;
    default:return fm_next();
    }
}
__attribute__((used,noinline)) static uint64_t __cdecl fm_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    uint32_t eax=fm_next(),edx=fm_next(),pick=fm_next();fm_ecx=fm_next();
    if(fm_calls==FM_MAX){fail("flyer move call overflow");ExitProcess(1);}
    fm_record *v=fm_log+fm_calls++;memset(v,0,sizeof(*v));v->id=id;v->slots=weapons_bridge_slots(a,fm_counts[id]);
    for(unsigned k=0;k<fm_counts[id];++k)v->args[k]=fm_stack(a[k])?0:a[k];
    if(!fm_real)for(unsigned j=0;j<3;++j)v->regs[j]=fm_stack(regs[j])?0:regs[j];
    switch(id){
    case 1: /* transform_point: the probe point by content, a new Q. */
        fm_vector(a[0],v->input);
        for(unsigned j=0;j<3;++j)put32(fm_host(a[2])+4*j,pick%5?fm_next()%0x10000u-0x8000u:fm_next());
        break;
    case 2: /* terrain_height: Q by content; the height around Q.z. */
        fm_vector(a[1],v->input);eax=fm_height(v->input[2],pick);break;
    case 3: /* multiply_q10: any AX. */
        break;
    case 4: /* dist3d_delta: around the 30000 threshold, small or negative. */
        if(pick%4==0)eax=29999u+(pick>>8)%3;else if(pick%4==1)eax=fm_next()%60000u;else if(pick%4==2)eax=0u-fm_next()%1000u;
        break;
    default:break;
    }
    if(!fm_real&&fm_next()%6==0)fm_scribble(fm_next());
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define FM_STUB(n) __attribute__((naked)) static void fm_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _fm_stub_impl\n\taddl $24,%esp\n\tmovl _fm_ecx,%ecx\n\tret\n\t");}
FM_STUB(0) FM_STUB(1) FM_STUB(2) FM_STUB(3) FM_STUB(4) FM_STUB(5)
#undef FM_STUB
static void *fm_stubs[FM_IDS]={fm_stub_0,fm_stub_1,fm_stub_2,fm_stub_3,fm_stub_4,fm_stub_5};
/* Calls target with one slot, EAX/EDX/ECX/EBX from regs and a patterned
 * stack below the argument; returns EDX:EAX in pair. */
__attribute__((naked,noinline)) static void fm_call(void *target,const uint32_t *a,const uint32_t regs[4],uint32_t pattern,uint32_t pair[2]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 20(%ebp),%eax\n\tmovl $1024,%ecx\n\trep stosl\n\taddl $3840,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%esi\n\tmovl (%esi),%eax\n\tmovl 4(%esi),%edx\n\tmovl 8(%esi),%ecx\n\tmovl 12(%esi),%ebx\n\tcall *8(%ebp)\n\t"
        "movl 24(%ebp),%ecx\n\tmovl %eax,(%ecx)\n\tmovl %edx,4(%ecx)\n\tleal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* Globals: the terrain pointer 4a0be0 and the camera pointer 4d24ea. */
static void fm_globals(BYTE *out,int restore){
    if(restore){memcpy(mapped+0xa0be0,out,4);memcpy(mapped+0xd24ea,out+4,4);}
    else{memcpy(out,mapped+0xa0be0,4);memcpy(out+4,mapped+0xd24ea,4);}
}
static void fm_reset(void){fm_calls=0;fm_stream=fm_seed;}
/* One flyer X (pose O = X+0Ch, flight state S = X+243h) and a camera. In the
 * real pass also a terrain of up to 8 x 8 cells around the flyer. */
static void fm_build(unsigned n,uint32_t a[6],uint32_t r[3],int real){
    uint32_t b=FM_BASE,X=b+FM_X;BYTE *x=fm_arena+FM_X,*s=x+0x243;
    fm_seed=random32();random_bytes(fm_arena,FM_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    put16(x+0x32,(uint16_t)(random32()%2?0:random32()%4?1:random32()));
    put16(x+6,(uint16_t)(random32()%2?0:random32()%4?1:random32()));
    put32(s+4,random32()%3==0?0:random32()%3?random32()%3000:random32());
    for(unsigned j=0;j<3;++j)put32(s+0xc+4*j,random32()%4?random32()%400-200:random32());
    static const uint16_t rates[]={0,1,0xffff,0x8000,0x7fff,0xfff0,0x10};
    put16(s+0x18,random32()%2?rates[random32()%7]:(uint16_t)random32());put16(s+0x1a,random32()%2?rates[random32()%7]:(uint16_t)random32());
    uint32_t cam=b+FM_CAM;
    if(!real){
        put32(mapped+0xa0be0,random32());put32(mapped+0xd24ea,cam);
        /* Camera near the flyer in a share of the cases. */
        if(random32()%2)for(unsigned j=0;j<3;++j)put32(fm_host(cam+4+4*j),get32(x+0x26+4*j)+random32()%40000-20000);
        a[0]=X;(void)n;return;
    }
    BYTE *map=fm_arena+FM_MAP;uint32_t w=random32()%4,h=random32()%4,shift=6+random32()%6;
    put32(map+0x100,w);put32(map+0x104,h);put32(map+0x108,shift);
    put32(map+0xec,b+FM_CELLS);put32(map+0x110,random32()%4096);put32(map+0x118,random32()%64);
    /* Game terrain stores flags & 3 as 0, 1 or 2 only (weapons.md). */
    for(unsigned j=0;j<(16u<<(w+h));++j)if(j%16==15)fm_arena[FM_CELLS+j]=(BYTE)((random32()&0xfcu)|random32()%3);
    int32_t width=(int32_t)(1u<<(w+shift)),height=(int32_t)(1u<<(h+shift));
    put32(x+0x26,(uint32_t)((int32_t)(random32()%(uint32_t)(width+width/2))-width/4));
    put32(x+0x2a,(uint32_t)((int32_t)(random32()%(uint32_t)(height+height/2))-height/4));
    put32(x+0x2e,random32()%3?random32()%6000-1000:random32()%60000);
    for(unsigned j=0;j<3;++j)put16(x+0xc+2*j,(uint16_t)(random32()%3?random32():0));
    for(unsigned j=0;j<3;++j)put32(fm_host(cam+4+4*j),get32(x+0x26+4*j)+(random32()%2?random32()%40000-20000:random32()));
    put32(mapped+0xa0be0,b+FM_MAP);put32(mapped+0xd24ea,cam);
    a[0]=X;
}
static int fm_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const fm_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,fm_calls);
    for(unsigned j=0;j<calls&&j<fm_calls&&expected;++j)if(memcmp(expected+j,fm_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(fm_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<FM_SIZE;++j)if(fm_after[j]!=fm_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,fm_after[j],fm_arena[j]);break;}
    for(unsigned j=0;j<calls&&expected;++j)fprintf(stderr," trace %u id%u args %08x %08x %08x eax %08x\n",j,expected[j].id,
        expected[j].args[0],expected[j].args[1],expected[j].args[2],expected[j].eax);
    return 1;
}
static unsigned fm_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<fm_calls;++j)if(c->event==ES2_COLLIDE_CALL&&fm_addresses[fm_log[j].id]==c->key)++hits;
    return hits;
}
static int fm_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[FM_GLOBALS],after[FM_GLOBALS],actual[FM_GLOBALS];static fm_record expected[FM_MAX];
    memcpy(fm_before,fm_arena,FM_SIZE);fm_globals(before,0);
    fm_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=fm_calls;memcpy(expected,fm_log,calls*sizeof(*expected));
    memcpy(fm_after,fm_arena,FM_SIZE);fm_globals(after,0);
    memcpy(fm_arena,fm_before,FM_SIZE);fm_globals(before,1);fm_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);fm_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=fm_expected_hits(c+i))return fail("flyer move callee counter delta");
    if(pair[0]!=pair[1]||calls!=fm_calls||memcmp(expected,fm_log,calls*sizeof(*expected))||memcmp(fm_after,fm_arena,FM_SIZE)||
       memcmp(after,actual,sizeof(after)))
        return fm_report("direct",n,pair,calls,expected);
    return 0;
}
static int fm_patch(BYTE saved[FM_IDS][5],int active[FM_IDS],uint32_t which,int on){
    for(unsigned i=0;i<FM_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(fm_addresses[i]-0x400000u,fm_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(fm_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
static int fm_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],regs[4],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;fm_build(n,a,r,0);
        regs[0]=regs[1]=regs[2]=regs[3]=pattern;
        BYTE before[FM_GLOBALS],after[FM_GLOBALS],actual[FM_GLOBALS],expected_fpu[108],actual_fpu[108];static fm_record expected[FM_MAX];
        memcpy(fm_before,fm_arena,FM_SIZE);fm_globals(before,0);
        fm_reset();fm_call(mapped+d->rva,a,regs,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=fm_calls;memcpy(expected,fm_log,calls*sizeof(*expected));memcpy(fm_after,fm_arena,FM_SIZE);fm_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("flyer move poison install");
        memcpy(fm_arena,fm_before,FM_SIZE);fm_globals(before,1);fm_reset();fm_call(mapped+d->rva,a,regs,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);fm_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=fm_calls||memcmp(expected,fm_log,calls*sizeof(*expected))||memcmp(fm_after,fm_arena,FM_SIZE)||
           memcmp(after,actual,sizeof(after))||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=fm_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("flyer move poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state\n",d->name,total);
    return status;
}
/* Real callees with the nested hooks in rotating original/replaced/compare
 * modes and a patterned stack; the sound stub's arguments are compared in
 * order (entry registers only in the direct pass). */
static int fm_real_pass(unsigned index){
    static const unsigned nested[]={ES2_HOOK_INDEX_euler_matrix,ES2_HOOK_INDEX_transform_point,ES2_HOOK_INDEX_terrain_height,
        ES2_HOOK_INDEX_multiply_q10,ES2_HOOK_INDEX_dist3d_delta};
    const unsigned count=5;
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0,acting=0,lifted=0,sounds=0;uint64_t entered[8]={0};
    BYTE saved[FM_IDS][5];int active[FM_IDS];if(fm_patch(saved,active,1u<<5,1))return 1;
    static fm_record expected[FM_MAX];
    fm_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<1024&&!status;++n){
        uint32_t a[6],r[3],regs[4],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;fm_build(n,a,r,1);
        regs[0]=regs[1]=regs[2]=regs[3]=pattern;
        BYTE before[FM_GLOBALS],after[FM_GLOBALS],actual[FM_GLOBALS];
        memcpy(fm_before,fm_arena,FM_SIZE);fm_globals(before,0);
        es2_hook hk[8];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("flyer move nested install");}
        fm_reset();fm_call(mapped+d->rva,a,regs,pattern,expected_pair);
        unsigned calls=fm_calls;memcpy(expected,fm_log,calls*sizeof(*expected));
        memcpy(fm_after,fm_arena,FM_SIZE);fm_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+count,d,&o,function_hook_extension(index)))return fail("flyer move caller install");
        memcpy(fm_arena,fm_before,FM_SIZE);fm_globals(before,1);fm_reset();
        fm_call(mapped+d->rva,a,regs,pattern,actual_pair);fm_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=fm_calls||memcmp(expected,fm_log,calls*sizeof(*expected))||
           memcmp(fm_after,fm_arena,FM_SIZE)||memcmp(after,actual,sizeof(after))){
            fprintf(stderr,"real mode=%u\n",mode);status=fm_report("real",n,pair,calls,expected);
        }
        acting+=get32(fm_before+FM_X+0x243+0x18)!=get32(fm_after+FM_X+0x243+0x18);
        lifted+=get32(fm_before+FM_X+0x2e)+get32(fm_before+FM_X+0x243+0x14)!=get32(fm_after+FM_X+0x2e);
        sounds+=calls!=0;
        for(unsigned j=0;j<=count;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("flyer move nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("flyer move nested uninstall");}
        fm_globals(before,1);++total;
    }
    fm_real=0;
    if(fm_patch(saved,active,0,0))return 1;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){
        fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&(!acting||!lifted||!sounds))status=fail("flyer move real cases without pitch reaction, clearance lift or sound");
    if(!status){printf("PASS: %s %u real-callee cases on generated terrain (%u pitch reactions, %u clearance lifts, %u sounds), "
        "5 nested hooks in rotating modes, 31/a7 stack, pair, memory, globals and sound inputs\n",d->name,total,acting,lifted,sounds);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
static int fm_cases(unsigned index,unsigned limit){
    BYTE saved[FM_IDS][5],old_globals[FM_GLOBALS];int active[FM_IDS];
    fm_globals(old_globals,0);if(fm_patch(saved,active,(1u<<FM_IDS)-1u,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_AI_MOTION_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];fm_build(n,a,r,0);status=fm_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=fm_installed_probes(index,64);
    if(fm_patch(saved,active,0,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=fm_real_pass(index);
    fm_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, points by content, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}

/* formation_steer 41fbb8 (docs/reverse-engineering/ai.md, "Formation
 * steering"). Direct cases with logging stubs for the virtual +78h,
 * dist2d_point, heading_point, euler_matrix and vehicle_control (ordered
 * argument slots, entry registers, the slot point T by content); stubs also
 * rewrite fields read again later. Installed 31/a7 probes, then a real pass
 * with dist2d_point, heading_point and euler_matrix nested in rotating modes
 * (the virtual and vehicle_control stay stubs, their inputs compared). */
typedef struct fs_record { uint32_t id,args[4],regs[3],input[3],eax,edx,slots; } fs_record;
enum { FS_SIZE=0x1000, FS_A=0x100, FS_L=0x500, FS_G=0x900, FS_LIST=0x940, FS_VT=0x980, FS_MAX=16, FS_IDS=5 };
#define FS_BASE ((uint32_t)(uintptr_t)fs_arena)
static BYTE fs_arena[FS_SIZE],fs_before[FS_SIZE],fs_after[FS_SIZE];
static fs_record fs_log[FS_MAX];
static unsigned fs_calls;
static int fs_real;
static uint32_t fs_seed,fs_stream,fs_leader;
__attribute__((used)) static uint32_t fs_ecx;
/* 0 virtual +78h, 1 4928d0 dist2d_point, 2 492934 heading_point,
 * 3 47ebb8 euler_matrix, 4 416a04 vehicle_control. */
static const uint32_t fs_addresses[FS_IDS]={0,0x4928d0,0x492934,0x47ebb8,0x416a04};
static const unsigned fs_counts[FS_IDS]={2,2,2,2,4};
static BYTE *fs_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static uint32_t fs_next(void){
    uint32_t z=fs_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static void fs_scribble(uint32_t pick){
    BYTE *A=fs_arena+FS_A,*L=fs_host(fs_leader);
    switch(pick%7){
    case 0:put16(A+0x10,(uint16_t)fs_next());break;
    case 1:put16(L+0x10,(uint16_t)fs_next());break;
    case 2:put16(L+0x28e,(uint16_t)(fs_next()%60u-30u));break;
    case 3:put16(L+0x32,(uint16_t)(fs_next()%2));break;
    case 4:put16(L+0x12+2*((pick>>8)%4),(uint16_t)fs_next());break;
    case 5:put32(A+0x26+4*((pick>>8)%3),fs_next());break;
    default:put32(L+0x26+4*((pick>>8)%3),fs_next());break;
    }
}
__attribute__((used,noinline)) static uint64_t __cdecl fs_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    uint32_t eax=fs_next(),edx=fs_next(),pick=fs_next();fs_ecx=fs_next();
    if(fs_calls==FS_MAX){fail("formation steer call overflow");ExitProcess(1);}
    fs_record *v=fs_log+fs_calls++;memset(v,0,sizeof(*v));v->id=id;v->slots=weapons_bridge_slots(a,fs_counts[id]);
    for(unsigned k=0;k<fs_counts[id];++k)v->args[k]=fm_stack(a[k])?0:a[k];
    if(!fs_real)for(unsigned j=0;j<3;++j)v->regs[j]=fm_stack(regs[j])?0:regs[j];
    switch(id){
    case 0: /* virtual +78h: T by content, then a slot offset. */
        fm_vector(a[1],v->input);
        if(pick%4)for(unsigned j=0;j<3;++j)put32(fs_host(a[1])+4*j,get32(fs_host(a[1])+4*j)+fs_next()%8000u-4000u);
        break;
    case 1: { /* dist2d_point: around 2000 and 25000, or anything. */
        static const uint32_t marks[]={1999,2000,2001,24999,25000,25001};
        fm_vector(a[1],v->input);
        if(!fs_real)eax=pick%3==0?marks[(pick>>8)%6]:pick%3==1?fs_next()%40000u:eax;
        break; }
    case 2:fm_vector(a[0],v->input);break;
    case 3: /* euler_matrix writes the leader's matrix words. */
        if(pick%2)for(unsigned j=0;j<4;++j)put16(fs_host(a[1])+2*j,(uint16_t)fs_next());
        break;
    default:break;
    }
    if(!fs_real&&fs_next()%6==0)fs_scribble(fs_next());
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define FS_STUB(n) __attribute__((naked)) static void fs_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _fs_stub_impl\n\taddl $24,%esp\n\tmovl _fs_ecx,%ecx\n\tret\n\t");}
FS_STUB(0) FS_STUB(1) FS_STUB(2) FS_STUB(3) FS_STUB(4)
#undef FS_STUB
static void *fs_stubs[FS_IDS]={fs_stub_0,fs_stub_1,fs_stub_2,fs_stub_3,fs_stub_4};
static void fs_reset(void){fs_calls=0;fs_stream=fs_seed;}
/* Vehicle A, its group (list at G+0Ch) whose first member L is the leader
 * (A itself in an eighth of the cases), A's table with the stub at +78h. */
static void fs_build(unsigned n,uint32_t a[6],uint32_t r[3],int real){
    uint32_t b=FS_BASE,A=b+FS_A;
    fs_seed=random32();random_bytes(fs_arena,FS_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    fs_leader=random32()%8==0?A:b+FS_L;uint32_t L=fs_leader;BYTE *x=fs_arena+FS_A,*l=fs_host(L);
    put32(x+0x45,b+FS_G);put32(fs_arena+FS_G+0xc,b+FS_LIST);put32(fs_arena+FS_LIST,L);
    put32(x,b+FS_VT);put32(fs_arena+FS_VT+0x78,(uint32_t)(uintptr_t)fs_stubs[0]);
    put16(x+0x10,(uint16_t)random32());
    static const uint16_t deltas[]={0x8000,0x2000,0x1fff,0xe000,0xe001,0xdfff,0x2001,0,1,0xffff};
    if(L!=A)put16(l+0x10,(uint16_t)(get16(x+0x10)-(random32()%3?deltas[random32()%10]:(uint16_t)random32())));
    put16(l+0x28e,(uint16_t)(random32()%2?random32()%60u-30u:random32()));
    put16(l+0x32,(uint16_t)(random32()%2?0:random32()%4?1:random32()));
    if(real){
        for(unsigned j=0;j<3;++j)put32(x+0x26+4*j,random32()%0x40000u-0x20000u);
        if(L!=A)for(unsigned j=0;j<3;++j)put32(l+0x26+4*j,get32(x+0x26+4*j)+(random32()%2?random32()%6000u-3000u:random32()%60000u-30000u));
        for(unsigned j=0;j<3;++j)put16(l+0xc+2*j,(uint16_t)(random32()%3?random32():0));
    }
    a[0]=A;(void)n;
}
static int fs_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const fs_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,fs_calls);
    for(unsigned j=0;j<calls&&j<fs_calls&&expected;++j)if(memcmp(expected+j,fs_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(fs_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<FS_SIZE;++j)if(fs_after[j]!=fs_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,fs_after[j],fs_arena[j]);break;}
    for(unsigned j=0;j<calls&&expected;++j)fprintf(stderr," trace %u id%u args %08x %08x %08x eax %08x\n",j,expected[j].id,
        expected[j].args[0],expected[j].args[1],expected[j].args[2],expected[j].eax);
    return 1;
}
static unsigned fs_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<fs_calls;++j){unsigned id=fs_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?id==0&&c->key==0x78:id&&fs_addresses[id]==c->key)++hits;}
    return hits;
}
static int fs_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    static fs_record expected[FS_MAX];
    memcpy(fs_before,fs_arena,FS_SIZE);
    fs_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=fs_calls;memcpy(expected,fs_log,calls*sizeof(*expected));
    memcpy(fs_after,fs_arena,FS_SIZE);
    memcpy(fs_arena,fs_before,FS_SIZE);fs_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=fs_expected_hits(c+i))return fail("formation steer callee counter delta");
    if(pair[0]!=pair[1]||calls!=fs_calls||memcmp(expected,fs_log,calls*sizeof(*expected))||memcmp(fs_after,fs_arena,FS_SIZE))
        return fs_report("direct",n,pair,calls,expected);
    return 0;
}
static int fs_patch(BYTE saved[FS_IDS][5],int active[FS_IDS],uint32_t which,int on){
    for(unsigned i=1;i<FS_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(fs_addresses[i]-0x400000u,fs_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(fs_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
static int fs_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],regs[4],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;fs_build(n,a,r,0);
        regs[0]=regs[1]=regs[2]=regs[3]=pattern;
        BYTE expected_fpu[108],actual_fpu[108];static fs_record expected[FS_MAX];
        memcpy(fs_before,fs_arena,FS_SIZE);
        fs_reset();fm_call(mapped+d->rva,a,regs,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=fs_calls;memcpy(expected,fs_log,calls*sizeof(*expected));memcpy(fs_after,fs_arena,FS_SIZE);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("formation steer poison install");
        memcpy(fs_arena,fs_before,FS_SIZE);fs_reset();fm_call(mapped+d->rva,a,regs,pattern,actual_pair);es2_hook_fpu_save(actual_fpu);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=fs_calls||memcmp(expected,fs_log,calls*sizeof(*expected))||memcmp(fs_after,fs_arena,FS_SIZE)||
           memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=fs_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("formation steer poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state\n",d->name,total);
    return status;
}
static int fs_real_pass(unsigned index){
    static const unsigned nested[]={ES2_HOOK_INDEX_dist2d_point,ES2_HOOK_INDEX_heading_point,ES2_HOOK_INDEX_euler_matrix};
    const unsigned count=3;
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0,paths[4]={0};uint64_t entered[4]={0};
    BYTE saved[FS_IDS][5];int active[FS_IDS];if(fs_patch(saved,active,1u<<4,1))return 1;
    static fs_record expected[FS_MAX];
    fs_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<1024&&!status;++n){
        uint32_t a[6],r[3],regs[4],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;fs_build(n,a,r,1);
        regs[0]=regs[1]=regs[2]=regs[3]=pattern;
        memcpy(fs_before,fs_arena,FS_SIZE);
        es2_hook hk[4];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("formation steer nested install");}
        fs_reset();fm_call(mapped+d->rva,a,regs,pattern,expected_pair);
        unsigned calls=fs_calls;memcpy(expected,fs_log,calls*sizeof(*expected));memcpy(fs_after,fs_arena,FS_SIZE);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+count,d,&o,function_hook_extension(index)))return fail("formation steer caller install");
        memcpy(fs_arena,fs_before,FS_SIZE);fs_reset();fm_call(mapped+d->rva,a,regs,pattern,actual_pair);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=fs_calls||memcmp(expected,fs_log,calls*sizeof(*expected))||memcmp(fs_after,fs_arena,FS_SIZE)){
            fprintf(stderr,"real mode=%u\n",mode);status=fs_report("real",n,pair,calls,expected);
        }
        /* Path of the final vehicle_control call: at the slot (speed 0),
         * turn in place (FFFFFF00h), formation (matrix path) or other. */
        if(calls){const fs_record *e=expected+calls-1;
            paths[e->args[2]==0xffffff00u?1:hk[2].calls||(e->args[2]==0&&e->args[1]==0)?2:e->args[2]==0?0:3]++;}
        for(unsigned j=0;j<=count;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("formation steer nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("formation steer nested uninstall");}
        ++total;
    }
    fs_real=0;
    if(fs_patch(saved,active,0,0))return 1;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){
        fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, 3 nested hooks in rotating modes, 31/a7 stack, pair, memory and stub inputs "
        "(final calls: %u speed 0, %u turn in place, %u matrix or zero, %u other)\n",d->name,total,paths[0],paths[1],paths[2],paths[3]);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
static int fs_cases(unsigned index,unsigned limit){
    BYTE saved[FS_IDS][5];int active[FS_IDS];
    if(fs_patch(saved,active,(1u<<FS_IDS)-2u,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_AI_MOTION_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];fs_build(n,a,r,0);status=fs_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=fs_installed_probes(index,64);
    if(fs_patch(saved,active,0,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=fs_real_pass(index);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, slot point by content, arena\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
static int ai_motion_cases(unsigned index,unsigned limit){
    return index==ES2_HOOK_INDEX_formation_steer?fs_cases(index,limit):fm_cases(index,limit);
}
