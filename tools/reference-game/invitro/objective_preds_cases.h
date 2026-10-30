/* objective_preds group (docs/reverse-engineering/mission_runtime.md,
 * "objective predicates"): subject_status 412dc4, subject_group 412bd0,
 * subject_actor 412bbc, group_down 412be4, route_flag 41324c, state_reached
 * 413920, group_safe 413950, group_marked 412d90, group_flagged 412c58,
 * bounds_margin 413780. The mission state comes from the mission_objectives
 * builder (mo_build, its arena and globals). Direct cases run the original body
 * and the core against logging stubs for the game callees of the hook under
 * test (412bd0, 412bbc, group_state 412c8c and the actors' virtual +40h under
 * 412dc4; 412c8c under 413920; 413920 and actor_safe 4137b4 under 413950;
 * bounds_outside 41373c under 413780): ordered calls with their arguments and
 * entry EAX/EDX/ECX, the full return pair, arena and globals. The stubs change
 * what the original reads again after a call (actor flags, the group's count
 * and actor array) and return actors for 412bbc, which the caller dereferences.
 * Then installed 31/a7 poison cases; a caller pass: each hook runs under the
 * existing hooks that call it (subject_done 412e58, rivals_done 412e74,
 * objective_eval 413280, status_check 4135e8) with those callers and the real
 * callees nested in rotating modes and only the actors' virtual +40h and the
 * cockpit message slot stubbed; and the installed compare cases of
 * compare_probes.h for the seven hooks that accept compare. */
typedef struct op_record { uint32_t id,args[2],regs[3],eax,edx; } op_record;
enum { OP_IDS=7, OP_MAX=64 };
static op_record op_log[OP_MAX];
static unsigned op_calls;
static uint32_t op_seed,op_stream;
__attribute__((used)) static uint32_t op_ecx;
/* 0 412bd0, 1 412bbc, 2 group_state 412c8c, 3 the actor virtual +40h
 * (through the vtable), 4 413920, 5 actor_safe 4137b4, 6 bounds_outside
 * 41373c. */
static const uint32_t op_addresses[OP_IDS]={0x412bd0,0x412bbc,0x412c8c,0,0x413920,0x4137b4,0x41373c};
static const unsigned op_counts[OP_IDS]={1,1,1,1,1,1,2};
static uint32_t op_next(void){
    uint32_t z=op_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
/* A group of the builder whose count and array the stubs may change. */
static void op_regroup(uint32_t pick){
    BYTE *g=mo_host(mo_group(op_next()%MO_NGROUPS));
    if(pick%3==0)put16(g+0x10,(uint16_t)(op_next()%5));
    else if(pick%3==1)put32(mo_host(get32(g+0xc))+4*(op_next()%4),mo_actor(op_next()%MO_NACTORS));
    else mo_host(mo_actor(op_next()%MO_NACTORS))[op_next()%2?0x99:0xb4]=(BYTE)(op_next()%2?0:1+op_next()%255);
}
__attribute__((used,noinline)) static uint64_t __cdecl op_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(op_calls==OP_MAX){fail("objective_preds synthetic call overflow");ExitProcess(1);}
    op_record *v=op_log+op_calls++;memset(v,0,sizeof(*v));v->id=id;
    for(unsigned k=0;k<op_counts[id];++k)v->args[k]=mo_stack(a[k])?0:a[k];
    for(unsigned j=0;j<3;++j)v->regs[j]=mo_stack(regs[j])?0:regs[j];
    uint32_t eax=op_next(),edx=op_next(),pick=op_next();op_ecx=op_next();
    switch(id){
    case 0: /* 412bd0: the subject (only passed on to 412c8c) */
        if(pick%2)eax=mo_group(pick>>8&3);
        break;
    case 1: /* 412bbc: an actor (dereferenced); flags may change between calls */
        eax=mo_actor(pick>>8&7);
        if(pick%5==0)op_regroup(pick>>16);
        break;
    case 2: { /* group_state: levels around the thresholds 3 and 4 */
        static const uint16_t levels[]={0,1,2,3,4,5,0x7fff,0x8000,0xffff,3,4};
        eax=pick%13?(eax&0xffff0000u)|levels[(pick>>8)%11]:eax;
        break; }
    case 3: { /* virtual +40h: around 32h, 80h and C0h */
        static const uint16_t levels[]={0x31,0x32,0x33,0x7f,0x80,0x81,0xbf,0xc0,0xc1,0,0xff,0x7fff,0x8000,0xffff};
        eax=pick%11?(eax&0xffff0000u)|levels[(pick>>8)%14]:eax;
        break; }
    case 4: /* 413920: the full EAX is tested */
        eax=pick%4==0?0x10000u:pick%4==1?1u:pick%4==2?0:eax;
        if(pick%7==0)op_regroup(pick>>16);
        break;
    case 5: /* actor_safe: only AX is tested */
        eax=pick%4==0?0x10000u:pick%4==1?(eax&0xffff0000u)|1u:pick%4==2?0:eax;
        if(pick%7==0)op_regroup(pick>>16);
        break;
    default: break;
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define OP_STUB(n) __attribute__((naked)) static void op_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _op_stub_impl\n\taddl $24,%esp\n\tmovl _op_ecx,%ecx\n\tret\n\t");}
OP_STUB(0) OP_STUB(1) OP_STUB(2) OP_STUB(3) OP_STUB(4) OP_STUB(5) OP_STUB(6)
#undef OP_STUB
static void *op_stubs[OP_IDS]={op_stub_0,op_stub_1,op_stub_2,op_stub_3,op_stub_4,op_stub_5,op_stub_6};
static unsigned op_which(unsigned index){
    if(index==ES2_HOOK_INDEX_subject_status)return 0x7u;
    if(index==ES2_HOOK_INDEX_state_reached)return 0x4u;
    if(index==ES2_HOOK_INDEX_group_safe)return 0x30u;
    if(index==ES2_HOOK_INDEX_bounds_margin)return 0x40u;
    return 0;
}
static void op_reset(void){op_calls=0;op_stream=op_seed;}
/* The builder's state; the hook's group with a current order (412bd0,
 * 412bbc and 412dc4 read it unchecked). Direct cases (real = 0) add
 * negative actor counts and use the stub virtual; real cases keep the
 * builder's counts 1..4 (group_state divides by the count). */
static void op_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],uint32_t *esi,int real){
    mo_index=index;mo_build(index,a,r,esi,real);op_seed=random32();
    if(!real)put32(mo_arena+MO_VTABLE+0x40,(uint32_t)(uintptr_t)op_stubs[3]);
    const uint32_t group=mo_group(random32()%MO_NGROUPS);BYTE *g=mo_host(group);
    const uint32_t current=get32(g+0x6c);
    if(!get32(g+0x44+4*current))put32(g+0x44+4*current,mo_order(random32()%MO_NORDERS));
    if(!real&&random32()%8==0){static const uint16_t negative[]={0x8000,0xffff,0x8001,0xfffe};put16(g+0x10,negative[random32()%4]);}
    if(!real&&n<64)put16(g+0x10,(uint16_t)(n%6==5?0xffff:n%6));
    a[0]=group;
    if(index==ES2_HOOK_INDEX_route_flag){
        /* The route of one of the ten slots (null slots read nothing), an
         * own route, or anything. */
        const uint32_t order=get32(g+0x44+4*(random32()%10));
        a[1]=order&&random32()%2?get32(mo_host(order)+8):random32()%2?mo_route(random32()%MO_NROUTES):random32();
    } else if(index==ES2_HOOK_INDEX_bounds_margin)a[0]=random32()%3?mo_actor(random32()%MO_NACTORS):get32(mapped+0xd24e6);
}
static int op_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const op_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,op_calls);
    for(unsigned j=0;j<calls&&j<op_calls&&expected;++j)if(memcmp(expected+j,op_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(op_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<MO_SIZE;++j)if(mo_after[j]!=mo_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,mo_after[j],mo_arena[j]);break;}
    return 1;
}
static unsigned op_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<op_calls;++j)
        if(c->event==ES2_COLLIDE_SLOT?op_log[j].id==3&&c->key==0x40:op_addresses[op_log[j].id]==c->key)++hits;
    return hits;
}
static int op_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    static BYTE before[MO_GLOBALS],after[MO_GLOBALS],actual[MO_GLOBALS];static op_record expected[OP_MAX];
    memcpy(mo_before,mo_arena,MO_SIZE);mo_globals(before,0);
    op_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=op_calls;
    memcpy(expected,op_log,calls*sizeof(*expected));memcpy(mo_after,mo_arena,MO_SIZE);mo_globals(after,0);
    memcpy(mo_arena,mo_before,MO_SIZE);mo_globals(before,1);op_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];for(unsigned i=0;i<count&&i<8;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);mo_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count&&i<8;++i)if(c[i].count-counts[i]!=op_expected_hits(c+i))return fail("objective_preds callee counter delta");
    if(pair[0]!=pair[1]||calls!=op_calls||memcmp(expected,op_log,calls*sizeof(*expected))||memcmp(mo_after,mo_arena,MO_SIZE)||memcmp(after,actual,MO_GLOBALS)){
        mo_global_diff(after,actual);return op_report("direct",n,pair,calls,expected);
    }
    return 0;
}
static int op_patch(BYTE saved[OP_IDS][5],int active[OP_IDS],unsigned which,int on){
    for(unsigned i=0;i<OP_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!op_addresses[i])continue;if(actor_patch(op_addresses[i]-0x400000u,op_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(op_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static int op_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],esi,expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;op_build(index,n,a,r,&esi,0);
        static BYTE before[MO_GLOBALS],after[MO_GLOBALS],actual[MO_GLOBALS];BYTE expected_fpu[108],actual_fpu[108];static op_record expected[OP_MAX];
        memcpy(mo_before,mo_arena,MO_SIZE);mo_globals(before,0);
        op_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=op_calls;memcpy(expected,op_log,calls*sizeof(*expected));memcpy(mo_after,mo_arena,MO_SIZE);mo_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("objective_preds poison install");
        memcpy(mo_arena,mo_before,MO_SIZE);mo_globals(before,1);op_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);es2_hook_fpu_save(actual_fpu);mo_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=op_calls||memcmp(expected,op_log,calls*sizeof(*expected))||memcmp(mo_after,mo_arena,MO_SIZE)||
           memcmp(after,actual,MO_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1){
            mo_global_diff(after,actual);status=op_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        }
        if(!es2_hook_uninstall(&h))return fail("objective_preds poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state\n",d->name,total);
    return status;
}
/* Caller pass: the hook under test runs under the existing hook that calls
 * it, over the builder's real state (mo_real: only the actors' virtual
 * +40h and the cockpit message slot are logging stubs). The caller and the
 * real callees are nested hooks in rotating original/replaced/compare
 * modes (compare falls back to replaced where refused); the hook under
 * test is original, replaced or compare (where accepted). Pair, stub
 * trace, arena and globals against the same call without the hook under
 * test; the hook and every nested hook must be entered and no compare may
 * mismatch. objective_eval reads the caller's ESI: the call sets it. */
static unsigned op_nested(unsigned index,unsigned nested[8],unsigned *caller){
    static const unsigned subject[]={ES2_HOOK_INDEX_subject_done,ES2_HOOK_INDEX_subject_status,ES2_HOOK_INDEX_subject_group,
        ES2_HOOK_INDEX_subject_actor,ES2_HOOK_INDEX_group_state};
    static const unsigned objectives[]={ES2_HOOK_INDEX_objective_eval,ES2_HOOK_INDEX_route_flag,ES2_HOOK_INDEX_state_reached,
        ES2_HOOK_INDEX_group_safe,ES2_HOOK_INDEX_group_marked,ES2_HOOK_INDEX_group_flagged,ES2_HOOK_INDEX_group_state,ES2_HOOK_INDEX_actor_safe};
    static const unsigned status[]={ES2_HOOK_INDEX_status_check,ES2_HOOK_INDEX_bounds_outside,ES2_HOOK_INDEX_bounds_check,ES2_HOOK_INDEX_timer_word_step};
    const unsigned *set;unsigned size,count=0;
    if(index==ES2_HOOK_INDEX_subject_status||index==ES2_HOOK_INDEX_subject_group||index==ES2_HOOK_INDEX_subject_actor){set=subject;size=5;*caller=ES2_HOOK_INDEX_subject_done;}
    else if(index==ES2_HOOK_INDEX_group_down){nested[0]=*caller=ES2_HOOK_INDEX_rivals_done;return 1;}
    else if(index==ES2_HOOK_INDEX_bounds_margin){set=status;size=4;*caller=ES2_HOOK_INDEX_status_check;}
    else {set=objectives;size=8;*caller=ES2_HOOK_INDEX_objective_eval;}
    for(unsigned i=0;i<size;++i)if(set[i]!=index)nested[count++]=set[i];
    return count;
}
static int op_caller_pass(unsigned index){
    unsigned nested[8],caller,count=op_nested(index,nested,&caller);
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0,virtuals=0;uint64_t entered[9]={0};
    const es2_hook_extension *x=function_hook_extension(index);
    BYTE *target=mapped+function_hook_descriptor(caller)->rva;
    mo_real=1;
    for(unsigned mode=0;mode<9&&!status;++mode)for(unsigned n=0;n<256&&!status;++n){
        uint32_t a[6],r[3],esi,expected_pair[2],actual_pair[2];op_build(index,n,a,r,&esi,1);
        static BYTE before[MO_GLOBALS],after[MO_GLOBALS],actual[MO_GLOBALS];static mo_record expected[MO_MAX];
        /* The caller's arguments: the group (subject_done; rivals_done with
         * a current order), the player, or the player, any, force (mostly
         * the periodic 0) for status_check. */
        if(caller==ES2_HOOK_INDEX_objective_eval)a[0]=get32(mapped+0xd24e6);
        else if(caller==ES2_HOOK_INDEX_status_check){a[0]=get32(mapped+0xd24e6);a[1]=random32();a[2]=random32()%8?0:1;}
        memcpy(mo_before,mo_arena,MO_SIZE);mo_globals(before,0);
        es2_hook hk[9];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!nested_install_hook(hk+j,nested[j],&o,e))return fail("objective_preds nested install");}
        mo_reset();mo_machine(target,a,r,esi,expected_pair);
        unsigned calls=mo_calls;memcpy(expected,mo_log,calls*sizeof(*expected));virtuals+=calls;
        memcpy(mo_after,mo_arena,MO_SIZE);mo_globals(after,0);
        o.mode=(es2_hook_mode)(mode/3);if(x->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+8,d,&o,x))return fail("objective_preds hook install");
        memcpy(mo_arena,mo_before,MO_SIZE);mo_globals(before,1);mo_reset();
        mo_machine(target,a,r,esi,actual_pair);mo_globals(actual,0);
        if(expected_pair[0]!=actual_pair[0]||expected_pair[1]!=actual_pair[1]||calls!=mo_calls||memcmp(expected,mo_log,calls*sizeof(*expected))||
           memcmp(mo_after,mo_arena,MO_SIZE)||memcmp(after,actual,MO_GLOBALS)){
            fprintf(stderr,"FAIL: %s case %u (caller %s mode=%u) pair %08x%08x/%08x%08x calls %u/%u\n",d->name,n,
                function_hook_descriptor(caller)->name,mode,expected_pair[1],expected_pair[0],actual_pair[1],actual_pair[0],calls,mo_calls);
            for(unsigned j=0;j<MO_SIZE;++j)if(mo_after[j]!=mo_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,mo_after[j],mo_arena[j]);break;}
            mo_global_diff(after,actual);status=1;
        }
        for(unsigned j=0;j<9;++j){if(j<count||j==8)entered[j]+=hk[j].calls;
            if(hk[j].mismatches){fprintf(stderr,"FAIL: %s case %u (caller mode=%u) compare mismatch in %s\n",d->name,n,mode,hk[j].descriptor->name);status=1;}
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("objective_preds nested uninstall");}
        mo_globals(before,1);++total;
    }
    mo_real=0;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&!entered[8]){fprintf(stderr,"FAIL: %s never entered under %s\n",d->name,function_hook_descriptor(caller)->name);status=1;}
    if(!status)printf("PASS: %s %u caller cases (%llu calls under %s), %u nested hooks in rotating modes, %u stub calls, hook original/replaced/compare, full pair, trace and memory\n",
        d->name,total,(unsigned long long)entered[8],function_hook_descriptor(caller)->name,count,virtuals);
    return status;
}
/* Compare cases (compare_probes.h) for the six leaves and bounds_margin
 * (real bounds_outside) on the real-mode state. */
static void op_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){uint32_t esi;op_build(index,n,a,r,&esi,1);}
static int objective_preds_cases(unsigned index,unsigned limit){
    unsigned which=op_which(index);
    BYTE saved[OP_IDS][5],old_arena[MO_SIZE];int active[OP_IDS];static BYTE old_globals[MO_GLOBALS];static BYTE old_bank[2000];
    mo_globals(old_globals,0);memcpy(old_bank,mapped+0xa9e70,2000);memcpy(old_arena,mo_arena,MO_SIZE);mo_real=0;
    if(limit&&compare_probe_installed(index)){mo_real=1;int status=compare_probe_run(index,limit,op_compare_build,mo_compare_state,MO_SIZE+MO_GLOBALS);
        mo_real=0;mo_globals(old_globals,1);memcpy(mapped+0xa9e70,old_bank,2000);memcpy(mo_arena,old_arena,MO_SIZE);return status;}
    if(op_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_OBJECTIVE_PREDS_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3],esi;op_build(index,n,a,r,&esi,0);status=op_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=op_installed_probes(index,64);
    if(op_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=op_caller_pass(index);
    if(!limit&&!status&&compare_probe_capable(index)){mo_real=1;status=compare_probe_run(index,0,op_compare_build,mo_compare_state,MO_SIZE+MO_GLOBALS);mo_real=0;}
    mo_globals(old_globals,1);memcpy(mapped+0xa9e70,old_bank,2000);memcpy(mo_arena,old_arena,MO_SIZE);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
