/* Death group: death_sequence 45fa84 (docs/reverse-engineering/main_loop.md,
 * "death sequence 45fa84"). Direct cases run the original body and the core
 * against logging stubs for every game callee (display request 42a3f4, view
 * subject 45df34, display start 42a4c4, view update 45de30, outside camera
 * 401c74, timer_dword_step 467aa8): ordered calls with their arguments and
 * entry EAX/EDX/ECX. The stubs change what the original rereads (the camera and
 * player pointers, the display pointer, the camera lock). The view block
 * 4d24bc, the start flag and countdown 4d2a58..4d2a6b, the class tables
 * (49f4c0..49f53f, randomized in a quarter of the cases) and the frame-time
 * word 4d3b64 are compared as windows and restored. EAX is compared; EDX and
 * ECX are the original's two frame locals popped on return, uninitialized on
 * the per-frame path and dead at the only caller 45f570, so they are outside
 * the contract. Then installed 31/a7 poison probes and a real-callee pass with
 * real 401c74 and 467aa8 and the nested hooks timer_dword_step, multiply_q8 and
 * approach_word in rotating modes (the four presentation callees stay
 * stubs). */
typedef struct dt_record { uint32_t id,args[4],regs[3],eax; uint32_t count; } dt_record;
enum { DT_SIZE=0x800, DT_PLAYER=0x100, DT_PLAYER2=0x300, DT_DEF=0x500, DT_CAMERA=0x600, DT_CAMERA2=0x700,
    DT_MAX=16, DT_IDS=6, DT_VIEW=0xd24bc, DT_VIEW_LEN=0x6c, DT_COUNT=0xd2a58, DT_COUNT_LEN=0x14,
    DT_TABLES=0x9f4c0, DT_TABLES_LEN=0x80, DT_FRAME=0xd3b64,
    DT_GLOBALS=DT_VIEW_LEN+DT_COUNT_LEN+DT_TABLES_LEN+4 };
#define DT_BASE ((uint32_t)(uintptr_t)dt_arena)
static BYTE dt_arena[DT_SIZE],dt_before[DT_SIZE],dt_after[DT_SIZE];
static dt_record dt_log[DT_MAX];
static unsigned dt_calls;
static uint32_t dt_seed,dt_stream;
__attribute__((used)) static uint32_t dt_ecx;
static const uint32_t dt_addresses[DT_IDS]={0x42a3f4,0x45df34,0x42a4c4,0x45de30,0x401c74,0x467aa8};
static const unsigned dt_counts[DT_IDS]={2,1,1,0,4,1};
static uint32_t dt_next(void){
    uint32_t z=dt_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static int dt_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
__attribute__((used,noinline)) static uint64_t __cdecl dt_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(dt_calls==DT_MAX){fail("death synthetic call overflow");ExitProcess(1);}
    dt_record *v=dt_log+dt_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,4*dt_counts[id]);memcpy(v->regs,regs,12);
    v->count=weapons_bridge_slots(a,dt_counts[id]);
    for(unsigned j=0;j<3;++j)if(dt_stack(v->regs[j]))v->regs[j]=0;
    uint32_t eax=dt_next(),edx=dt_next(),pick=dt_next(),b=DT_BASE;dt_ecx=dt_next();
    BYTE *view=mapped+DT_VIEW;
    if(id<5){
        /* The callees may move the camera, lock or release it, replace the
         * player or the display object; the original rereads them. */
        if(pick%5==0)put32(view+0x2e,b+(pick>>8&1?DT_CAMERA2:DT_CAMERA));
        if(pick%7==0)put32(dt_arena+(pick>>9&1?DT_CAMERA2:DT_CAMERA)+0x4a,pick>>10&1?0:dt_next());
        if(id<4&&pick%11==0)put32(view+0x2a,b+(pick>>11&1?DT_PLAYER2:DT_PLAYER));
        if(id<4&&pick%13==0)put32(view+4,dt_next());
    }
    else if(pick%3==0)eax=0;
    v->eax=eax;return (uint64_t)edx<<32|eax;
}
#define DT_STUB(n) __attribute__((naked)) static void dt_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _dt_stub_impl\n\taddl $24,%esp\n\tmovl _dt_ecx,%ecx\n\tret\n\t");}
DT_STUB(0) DT_STUB(1) DT_STUB(2) DT_STUB(3) DT_STUB(4) DT_STUB(5)
#undef DT_STUB
static void *dt_stubs[DT_IDS]={dt_stub_0,dt_stub_1,dt_stub_2,dt_stub_3,dt_stub_4,dt_stub_5};
static void dt_globals(BYTE *out,int restore){
    BYTE *p[4]={mapped+DT_VIEW,mapped+DT_COUNT,mapped+DT_TABLES,mapped+DT_FRAME};
    const unsigned len[4]={DT_VIEW_LEN,DT_COUNT_LEN,DT_TABLES_LEN,4};
    for(unsigned i=0,at=0;i<4;at+=len[i++]){if(restore)memcpy(p[i],out+at,len[i]);else memcpy(out+at,p[i],len[i]);}
}
static int dt_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<DT_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
static void dt_reset(void){dt_calls=0;dt_stream=dt_seed;}
/* The shipped class tables, kept for most cases (death_cases restores
 * them at the end with the other globals). */
static BYTE dt_shipped[DT_TABLES_LEN];
/* View block, players, definition, cameras. Class words mostly 0 or 1,
 * else -8..8 (neighbour reads stay inside the table window). real: camera
 * modes 0..3 for 401c74, small frame time for 467aa8. */
static void dt_build(unsigned n,uint32_t r[3],int real){
    uint32_t b=DT_BASE;BYTE *view=mapped+DT_VIEW;
    dt_seed=random32();random_bytes(dt_arena,DT_SIZE);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    random_bytes(view,DT_VIEW_LEN);random_bytes(mapped+DT_COUNT,DT_COUNT_LEN);
    if(random32()%4==0)random_bytes(mapped+DT_TABLES,DT_TABLES_LEN);else memcpy(mapped+DT_TABLES,dt_shipped,DT_TABLES_LEN);
    put32(mapped+DT_FRAME,real?random32()%2000:random32());
    put32(mapped+0xd2a68,n%3?0:random32()%2?1:random32());
    put32(view+0x2a,b+(random32()%2?DT_PLAYER:DT_PLAYER2));
    put32(view+0x2e,b+(random32()%2?DT_CAMERA:DT_CAMERA2));
    put16(view+0x32,(uint16_t)(random32()%2?2:random32()%3?random32()%5:random32()));
    put32(dt_arena+DT_PLAYER+0x1f2,b+DT_DEF);put32(dt_arena+DT_PLAYER2+0x1f2,b+DT_DEF);
    uint16_t k=(uint16_t)(random32()%4?random32()%2:(uint16_t)(random32()%17)-8u);put16(dt_arena+DT_DEF+0x50,k);
    for(unsigned c=0;c<2;++c){BYTE *cam=dt_arena+(c?DT_CAMERA2:DT_CAMERA);
        put32(cam+0x4a,random32()%2?0:random32()%2?1:random32());
        if(real)put16(cam+0x36,(uint16_t)(random32()%4));}
}
static int dt_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const dt_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,dt_calls);
    for(unsigned j=0;j<calls&&j<dt_calls&&expected;++j)if(memcmp(expected+j,dt_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(dt_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<DT_SIZE;++j)if(dt_after[j]!=dt_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,dt_after[j],dt_arena[j]);break;}
    return 1;
}
static unsigned dt_expected_hits(const callee_counter *c){
    unsigned hits=0;for(unsigned j=0;j<dt_calls;++j)if(dt_addresses[dt_log[j].id]==c->key)++hits;return hits;
}
static int dt_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[DT_GLOBALS],after[DT_GLOBALS],actual[DT_GLOBALS];static dt_record expected[DT_MAX];
    memcpy(dt_before,dt_arena,DT_SIZE);dt_globals(before,0);
    dt_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=dt_calls;memcpy(expected,dt_log,calls*sizeof(*expected));
    memcpy(dt_after,dt_arena,DT_SIZE);dt_globals(after,0);
    memcpy(dt_arena,dt_before,DT_SIZE);dt_globals(before,1);dt_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];for(unsigned i=0;i<count&&i<8;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);dt_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count&&i<8;++i)if(c[i].count-counts[i]!=dt_expected_hits(c+i))return fail("death callee counter delta");
    if((uint32_t)pair[0]!=(uint32_t)pair[1]||calls!=dt_calls||memcmp(expected,dt_log,calls*sizeof(*expected))||memcmp(dt_after,dt_arena,DT_SIZE)||
       dt_globals_differ(after,actual))
        return dt_report("direct",n,pair,calls,expected);
    return 0;
}
static int dt_patch(BYTE saved[DT_IDS][5],int active[DT_IDS],unsigned which,int on){
    for(unsigned i=0;i<DT_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(dt_addresses[i]-0x400000u,dt_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(dt_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static int dt_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6]={0},r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;dt_build(n,r,0);
        BYTE before[DT_GLOBALS],after[DT_GLOBALS],actual[DT_GLOBALS],expected_fpu[108],actual_fpu[108];static dt_record expected[DT_MAX];
        memcpy(dt_before,dt_arena,DT_SIZE);dt_globals(before,0);
        dt_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=dt_calls;memcpy(expected,dt_log,calls*sizeof(*expected));memcpy(dt_after,dt_arena,DT_SIZE);dt_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("death poison install");
        memcpy(dt_arena,dt_before,DT_SIZE);dt_globals(before,1);dt_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);es2_hook_fpu_save(actual_fpu);dt_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(expected_pair[0]!=actual_pair[0]||calls!=dt_calls||memcmp(expected,dt_log,calls*sizeof(*expected))||memcmp(dt_after,dt_arena,DT_SIZE)||
           dt_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=dt_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("death poison uninstall");dt_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, EAX/trace/arena/globals\n",d->name,total);
    return status;
}
/* Real 401c74 and 467aa8 with timer_dword_step, multiply_q8 and
 * approach_word in rotating original/replaced/compare modes (compare
 * falls back to replaced where refused), the hook itself original or
 * replaced; the presentation callees stay stubs. EAX, arena and globals
 * against a run with only the nested hooks installed. */
static int dt_real_pass(unsigned index){
    const unsigned nested[3]={ES2_HOOK_INDEX_timer_dword_step,ES2_HOOK_INDEX_multiply_q8,ES2_HOOK_INDEX_approach_word},count=3;
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[3]={0};
    BYTE saved[DT_IDS][5];int active[DT_IDS];
    if(dt_patch(saved,active,0x0fu,1))return 1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6]={0},r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;dt_build(n,r,1);
        BYTE before[DT_GLOBALS],after[DT_GLOBALS],actual[DT_GLOBALS];
        memcpy(dt_before,dt_arena,DT_SIZE);dt_globals(before,0);
        es2_hook hk[4];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("death nested install");}
        dt_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        memcpy(dt_after,dt_arena,DT_SIZE);dt_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+3,d,&o,function_hook_extension(index)))return fail("death caller install");
        memcpy(dt_arena,dt_before,DT_SIZE);dt_globals(before,1);dt_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);dt_globals(actual,0);
        if(expected_pair[0]!=actual_pair[0]||memcmp(dt_after,dt_arena,DT_SIZE)||dt_globals_differ(after,actual)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            fprintf(stderr,"real mode=%u\n",mode);dt_calls=0;status=dt_report("real",n,pair,0,NULL);
        }
        for(unsigned j=0;j<4;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("death nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("death nested uninstall");}
        dt_globals(before,1);++total;
    }
    if(dt_patch(saved,active,0,0))return 1;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, 31/a7 stack, EAX, arena and globals\n",d->name,total,count);
    return status;
}
static int death_cases(unsigned index,unsigned limit){
    const unsigned which=0x3fu;
    BYTE saved[DT_IDS][5],old_globals[DT_GLOBALS];int active[DT_IDS];
    dt_globals(old_globals,0);memcpy(dt_shipped,mapped+DT_TABLES,DT_TABLES_LEN);
    if(dt_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_DEATH_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6]={0},r[3];dt_build(n,r,0);status=dt_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=dt_installed_probes(index,64);
    if(dt_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=dt_real_pass(index);
    dt_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; EAX, ordered calls with entry registers, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
