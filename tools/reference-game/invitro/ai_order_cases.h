/* AI order helpers: target_class 411cb4, script_start 413e50, attack_target
 * 41c0f4 and order_resume 41eb34 (docs/reverse-engineering/mech_order.md, "AI
 * order helpers"). Direct cases run the original body and the core against
 * logging stubs for the game callees (memset 49681a under script_start; 413e50
 * and 41d5ec under attack_target; 413e50 and 41c0f4 under order_resume, whose
 * call of itself runs the original body): ordered argument slots and entry
 * EAX/EDX/ECX. The arena, the globals (stagger word 4a9b70, player 4a9b84) and
 * the full EDX:EAX are compared; both jump tables of 41eb34 must run every
 * reachable entry in the instrumented prefix. Then installed 31/a7 poison
 * probes, and a real-callee pass: script_start over the real memset,
 * attack_target over script_start, order_resume over script_start,
 * attack_target and itself, nested in rotating modes; only 41d5ec (the AI
 * function 41cf18) stays a stub there. */
typedef struct ao_record { uint32_t id,args[3],regs[3],eax,edx,slots; } ao_record;
enum { AO_SIZE=0x2400, AO_GRP=0x100, AO_ORD=0x300, AO_CFG=0x380, AO_TBL=0x480, AO_ACT=0x800, AO_ACTORS=8,
    AO_MAX=32, AO_IDS=4, AO_GLOBALS=6 };
#define AO_BASE ((uint32_t)(uintptr_t)ao_arena)
#define AO_ACTOR(i) (AO_BASE+AO_ACT+0x300u*(uint32_t)(i)+8u)
#define AO_GROUP(i) (AO_BASE+AO_GRP+0x80u*(uint32_t)(i))
#define AO_TABLE(i) (AO_BASE+AO_TBL+0x40u*(uint32_t)(i))
static BYTE ao_arena[AO_SIZE],ao_before[AO_SIZE],ao_after[AO_SIZE];
static ao_record ao_log[AO_MAX];
static unsigned ao_calls;
static int ao_real;
static uint32_t ao_seed,ao_stream;
static uint64_t ao_states[8],ao_types[8];
__attribute__((used)) static uint32_t ao_ecx;
/* 0 memset 49681a, 1 413e50 script_start, 2 41d5ec, 3 41c0f4 attack_target. */
static const uint32_t ao_addresses[AO_IDS]={0x49681a,0x413e50,0x41d5ec,0x41c0f4};
static const unsigned ao_counts[AO_IDS]={3,2,1,2};
static void ao_globals(BYTE *out,int restore){
    static const uint32_t va[2]={0x4a9b70,0x4a9b84};static const unsigned size[2]={2,4};unsigned at=0;
    for(unsigned i=0;i<2;++i){BYTE *g=mapped+va[i]-0x400000u;
        if(restore)memcpy(g,out+at,size[i]);else memcpy(out+at,g,size[i]);at+=size[i];}
}
static uint32_t ao_next(void){
    uint32_t z=ao_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *ao_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int ao_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static uint32_t ao_actor_or_zero(void){return random32()%4==0?0:AO_ACTOR(random32()%AO_ACTORS);}
__attribute__((used,noinline)) static uint64_t __cdecl ao_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    uint32_t eax=ao_next(),edx=ao_next(),pick=ao_next();ao_ecx=ao_next();
    if(ao_calls==AO_MAX){fail("ai order call overflow");ExitProcess(1);}
    ao_record *v=ao_log+ao_calls++;memset(v,0,sizeof(*v));v->id=id;v->slots=weapons_bridge_slots(a,ao_counts[id]);
    for(unsigned k=0;k<ao_counts[id];++k)v->args[k]=ao_stack(a[k])?0:a[k];
    if(!ao_real)for(unsigned j=0;j<3;++j)v->regs[j]=ao_stack(regs[j])?0:regs[j];
    const uint32_t A=AO_ACTOR(0);
    switch(id){
    case 1: /* 413e50: may change A's target (re-read by the tail) and its count. */
        if(pick%8==3)put32(ao_host(A+0x1a4),pick&0x100?0:AO_ACTOR((pick>>12)%AO_ACTORS));
        if(pick%8==5){uint32_t t=get32(ao_host(A+0x1a4));if(t)put16(ao_host(t+0x1a2),(uint16_t)pick);}
        break;
    case 3: /* 41c0f4: sets A's target like the original. */
        if(pick%2)put32(ao_host(a[0]+0x1a4),a[1]);
        break;
    default:break;
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define AO_STUB(n) __attribute__((naked)) static void ao_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _ao_stub_impl\n\taddl $24,%esp\n\tmovl _ao_ecx,%ecx\n\tret\n\t");}
AO_STUB(0) AO_STUB(1) AO_STUB(2) AO_STUB(3)
#undef AO_STUB
static void *ao_stubs[AO_IDS]={ao_stub_0,ao_stub_1,ao_stub_2,ao_stub_3};
/* Calls target with three slots, EAX/EDX/ECX/EBX from regs and a patterned
 * stack below the arguments; returns EDX:EAX in pair. */
__attribute__((naked,noinline)) static void ao_invoke(void *target,const uint32_t *a,const uint32_t regs[4],uint32_t pattern,uint32_t pair[2]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 20(%ebp),%eax\n\tmovl $1024,%ecx\n\trep stosl\n\taddl $4084,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%esi\n\tmovl (%esi),%eax\n\tmovl 4(%esi),%edx\n\tmovl 8(%esi),%ecx\n\tmovl 12(%esi),%ebx\n\tcall *8(%ebp)\n\t"
        "movl 24(%ebp),%ecx\n\tmovl %eax,(%ecx)\n\tmovl %edx,4(%ecx)\n\tleal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static void ao_reset(void){ao_calls=0;ao_stream=ao_seed;}
static uint32_t ao_rare(unsigned one_in){return random32()%one_in==0?1u+random32()%0xffu:0u;}
static uint16_t ao_pick16(const uint16_t *values,unsigned count){return random32()%8?values[random32()%count]:(uint16_t)random32();}
/* One actor: group, script table (or none), the flag bytes, the target
 * fields (+1A2h count, +1A4h target, +1A8h type, +248h order target, never
 * null), the configuration +1F2h and the order state +23Eh. */
static void ao_actor(unsigned i){
    const uint32_t X=AO_ACTOR(i);BYTE *p=ao_host(X);
    put32(p+0x45,AO_GROUP(random32()%4));put32(p+0x4d,random32()%6?AO_TABLE(random32()%8):0);
    p[0x99]=(BYTE)ao_rare(4);p[0xa3]=(BYTE)ao_rare(4);
    put16(p+0x1a2,(uint16_t)random32());put32(p+0x1a4,ao_actor_or_zero());
    static const uint16_t types[]={0,1,2,3,0,1,2,3,4,7,0xffff,0x8000};
    put16(p+0x1a8,ao_pick16(types,12));put32(p+0x1f2,AO_BASE+AO_CFG+0x40u*(random32()%4));
    static const uint16_t states[]={0,1,2,3,4,5,6,4,4,6,1,7,0xffff,0x8000};
    put16(p+0x23e,ao_pick16(states,14));put32(p+0x248,AO_ACTOR(random32()%AO_ACTORS));
}
/* The arena: four groups (leader slot +0Ch -> +70h, order slots +44h..50h,
 * index +6Ch), eight order records, four configurations (+22h, +50h),
 * eight script tables (timer +4, flags +0Ch/+0Dh) and eight actors (A =
 * actor 0); the player is A's group leader about half the time. */
static void ao_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    const uint32_t A=AO_ACTOR(0);
    ao_seed=random32();random_bytes(ao_arena,AO_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    for(unsigned g=0;g<4;++g){
        const uint32_t G=AO_GROUP(g);BYTE *p=ao_host(G);
        put32(p+0xc,G+0x70);put32(p+0x70,AO_ACTOR(random32()%AO_ACTORS));put32(p+0x6c,random32()%4);
        for(unsigned s=0;s<4;++s)put32(p+0x44+4*s,random32()%4?AO_BASE+AO_ORD+0x10u*(random32()%8):0);
    }
    static const uint16_t types[]={0,1,2,3,4,5,6,0,1,2,3,4,5,6,0xb,7,0xffff,0x8000};
    for(unsigned k=0;k<8;++k)put16(ao_arena+AO_ORD+0x10*k,ao_pick16(types,18));
    static const uint16_t limits[]={0x7cff,0x7d00,0x7d01,0,0x8000,0xffff,0x7fff};
    for(unsigned k=0;k<4;++k){BYTE *c=ao_arena+AO_CFG+0x40*k;put16(c+0x22,ao_pick16(limits,7));put16(c+0x50,(uint16_t)(random32()%2?0:random32()));}
    for(unsigned k=0;k<8;++k){BYTE *t=ao_arena+AO_TBL+0x40*k;t[0xc]=(BYTE)(random32()%3==0);t[0xd]=(BYTE)(random32()%3==0);}
    for(unsigned i=0;i<AO_ACTORS;++i)ao_actor(i);
    const uint32_t leader=get32(ao_host(get32(ao_host(get32(ao_host(A+0x45))+0xc))));
    put32(mapped+0xa9b84,random32()%2?leader:AO_ACTOR(random32()%AO_ACTORS));put16(mapped+0xa9b70,(uint16_t)random32());
    if(index==ES2_HOOK_INDEX_target_class){a[0]=AO_ACTOR(random32()%AO_ACTORS);}
    else if(index==ES2_HOOK_INDEX_script_start){
        /* S: an actor's +4Dh or any arena offset; the table: an arena table,
         * a DBSIM table, or overlapping S (the original re-reads [S]). */
        const uint32_t S=random32()%4?AO_ACTOR(random32()%AO_ACTORS)+0x4du:AO_BASE+0x100u+random32()%(AO_SIZE-0x200u);
        static const int32_t shifts[]={-4,-8,1,5,9,0xd,0x31,0x35,-0x20,0x40};
        static const uint32_t images[]={0x499594,0x499746,0x499518,0x4993e2};
        a[0]=S;a[1]=random32()%4==0?S+(uint32_t)shifts[random32()%10]:random32()%3?AO_TABLE(random32()%8):images[random32()%4];
    } else if(index==ES2_HOOK_INDEX_attack_target){
        /* T never null (the original dereferences T+1A8h unchecked); often A's current target or A itself. */
        const uint32_t cur=get32(ao_host(A+0x1a4));
        a[0]=A;a[1]=cur&&random32()%3==0?cur:AO_ACTOR(random32()%AO_ACTORS);
    } else a[0]=A;
    (void)n;
}
/* The path of 41eb34, decided before any call: 0 own flag, s = 1..6 the
 * player's state jump table (7 = above 6 or negative), 9+t the group type
 * table (t = 0..6, 7 = above 6 or negative). */
static unsigned ao_path(void){
    const uint32_t A=AO_ACTOR(0);
    if(ao_host(A)[0xa3])return 0;
    const uint32_t G=get32(ao_host(A+0x45)),leader=get32(ao_host(get32(ao_host(G+0xc))));
    const uint16_t s=get16(ao_host(A+0x23e));
    if(leader==get32(mapped+0xa9b84)&&s)return s>6?7u:s;
    const uint32_t rec=get32(ao_host(G+0x44+4*get32(ao_host(G+0x6c))));const uint16_t t=rec?get16(ao_host(rec)):0xb;
    return 9u+(t>6?7u:t);
}
static int ao_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const ao_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,ao_calls);
    for(unsigned j=0;j<calls&&j<ao_calls&&expected;++j)if(memcmp(expected+j,ao_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(ao_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<AO_SIZE;++j)if(ao_after[j]!=ao_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,ao_after[j],ao_arena[j]);break;}
    for(unsigned j=0;j<calls&&expected&&j<16;++j)fprintf(stderr," trace %u id%u args %08x %08x %08x eax %08x\n",j,expected[j].id,
        expected[j].args[0],expected[j].args[1],expected[j].args[2],expected[j].eax);
    return 1;
}
/* Counter deltas of the replacement: one per logged stub call of the
 * hook's own body. order_resume's call of itself (state 4, the order
 * target down) is the only call of that path; the inner calls count only
 * when the hook is installed (the integration pass), else the original
 * body runs them. */
static int ao_counters(unsigned index,const uint64_t *before,int recursion){
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);
    const int installed=rng_hooks[index].installed==1;
    for(unsigned i=0;i<count;++i){
        uint64_t want=0;
        if(c[i].key==0x41eb34)want=recursion?1u:0u;
        else if(!recursion||installed)for(unsigned j=0;j<ao_calls;++j)want+=ao_addresses[ao_log[j].id]==c[i].key;
        if(c[i].count-before[i]!=want)return fail("ai order callee counter delta");
    }
    return 0;
}
static int ao_recursion(unsigned index){
    const uint32_t A=AO_ACTOR(0);
    return index==ES2_HOOK_INDEX_order_resume&&ao_path()==4u&&ao_host(get32(ao_host(A+0x248)))[0x99];
}
static int ao_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[AO_GLOBALS],after[AO_GLOBALS],actual[AO_GLOBALS];static ao_record expected[AO_MAX];
    const int recursion=ao_recursion(index);
    memcpy(ao_before,ao_arena,AO_SIZE);ao_globals(before,0);
    ao_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=ao_calls;memcpy(expected,ao_log,calls*sizeof(*expected));
    memcpy(ao_after,ao_arena,AO_SIZE);ao_globals(after,0);
    memcpy(ao_arena,ao_before,AO_SIZE);ao_globals(before,1);ao_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);ao_globals(actual,0);
    if(n<4096&&ao_counters(index,counts,recursion))return 1;
    if(pair[0]!=pair[1]||calls!=ao_calls||memcmp(expected,ao_log,calls*sizeof(*expected))||memcmp(ao_after,ao_arena,AO_SIZE)||
       memcmp(after,actual,sizeof(after)))
        return ao_report("direct",n,pair,calls,expected);
    return 0;
}
static int ao_patch(BYTE saved[AO_IDS][5],int active[AO_IDS],uint32_t which,int on){
    for(unsigned i=0;i<AO_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(ao_addresses[i]-0x400000u,ao_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(ao_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
/* Direct-call stubs per hook (bit i = callee id i). */
static uint32_t ao_direct_stubs(unsigned index){
    return index==ES2_HOOK_INDEX_script_start?1u:index==ES2_HOOK_INDEX_attack_target?6u:index==ES2_HOOK_INDEX_order_resume?0xau:0u;
}
static int ao_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],regs[4],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;ao_build(index,n,a,r);
        regs[0]=regs[1]=regs[2]=regs[3]=pattern;
        BYTE before[AO_GLOBALS],after[AO_GLOBALS],actual[AO_GLOBALS],expected_fpu[108],actual_fpu[108];static ao_record expected[AO_MAX];
        memcpy(ao_before,ao_arena,AO_SIZE);ao_globals(before,0);
        ao_reset();ao_invoke(mapped+d->rva,a,regs,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=ao_calls;memcpy(expected,ao_log,calls*sizeof(*expected));memcpy(ao_after,ao_arena,AO_SIZE);ao_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("ai order poison install");
        memcpy(ao_arena,ao_before,AO_SIZE);ao_globals(before,1);ao_reset();ao_invoke(mapped+d->rva,a,regs,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);ao_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=ao_calls||memcmp(expected,ao_log,calls*sizeof(*expected))||memcmp(ao_after,ao_arena,AO_SIZE)||
           memcmp(after,actual,sizeof(after))||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||!h.calls)
            status=ao_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("ai order poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state\n",d->name,total);
    return status;
}
/* Real callees with the nested hooks in rotating original/replaced modes
 * (compare is refused by all four) and a patterned stack; the hook itself
 * original or replaced. Only 41d5ec stays a stub (inputs compared without
 * entry registers). order_resume gets a readable table in the entry EDX
 * (the original passes it to 413e50 for group types above 6). */
static int ao_real_pass(unsigned index){
    static const unsigned attack_nested[]={ES2_HOOK_INDEX_script_start};
    static const unsigned resume_nested[]={ES2_HOOK_INDEX_script_start,ES2_HOOK_INDEX_attack_target};
    const unsigned *nested=index==ES2_HOOK_INDEX_order_resume?resume_nested:attack_nested;
    const unsigned count=index==ES2_HOOK_INDEX_order_resume?2u:index==ES2_HOOK_INDEX_attack_target?1u:0u;
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[3]={0},self=0;
    BYTE saved[AO_IDS][5];int active[AO_IDS];if(ao_patch(saved,active,index==ES2_HOOK_INDEX_script_start?0u:4u,1))return 1;
    static ao_record expected[AO_MAX];
    ao_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<1024&&!status;++n){
        uint32_t a[6],r[3],regs[4],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;ao_build(index,n,a,r);
        regs[0]=regs[2]=regs[3]=pattern;regs[1]=index==ES2_HOOK_INDEX_order_resume?AO_TABLE(n%8):pattern;
        /* The call of itself enters with EDX = 4, which a group type above
         * 6 would use as the table (a fault in the original): keep 0..6. */
        if(ao_recursion(index)){const uint32_t G=get32(ao_host(AO_ACTOR(0)+0x45)),slot=G+0x44+4*get32(ao_host(G+0x6c));
            if(!get32(ao_host(slot)))put32(ao_host(slot),AO_BASE+AO_ORD);put16(ao_host(get32(ao_host(slot))),(uint16_t)(random32()%7));}
        BYTE before[AO_GLOBALS],after[AO_GLOBALS],actual[AO_GLOBALS];
        memcpy(ao_before,ao_arena,AO_SIZE);ao_globals(before,0);
        es2_hook hk[3];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(mode+j)%2?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,function_hook_extension(nested[j])))return fail("ai order nested install");}
        ao_reset();ao_invoke(mapped+d->rva,a,regs,pattern,expected_pair);
        unsigned calls=ao_calls;memcpy(expected,ao_log,calls*sizeof(*expected));
        memcpy(ao_after,ao_arena,AO_SIZE);ao_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+count,d,&o,function_hook_extension(index)))return fail("ai order caller install");
        memcpy(ao_arena,ao_before,AO_SIZE);ao_globals(before,1);ao_reset();
        ao_invoke(mapped+d->rva,a,regs,pattern,actual_pair);ao_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=ao_calls||memcmp(expected,ao_log,calls*sizeof(*expected))||
           memcmp(ao_after,ao_arena,AO_SIZE)||memcmp(after,actual,sizeof(after))){
            fprintf(stderr,"real mode=%u\n",mode);status=ao_report("real",n,pair,calls,expected);
        }
        if(hk[count].calls>1)++self;
        for(unsigned j=0;j<=count;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("ai order nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("ai order nested uninstall");}
        ao_globals(before,1);++total;
    }
    ao_real=0;
    if(ao_patch(saved,active,0,0))return 1;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){
        fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&index==ES2_HOOK_INDEX_order_resume&&!self){fprintf(stderr,"FAIL: order_resume never re-entered itself\n");status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, 31/a7 stack, pair, memory, globals and stub inputs\n",
        d->name,total,count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);
        if(index==ES2_HOOK_INDEX_order_resume)printf("REAL order_resume re-entered itself in %llu cases\n",(unsigned long long)self);}
    return status;
}
/* Compare cases (compare_probes.h) for target_class, the group's
 * compare-capable member; the state is the arena and the globals. */
static void ao_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){ao_build(index,n,a,r);}
static void ao_compare_state(BYTE *s,int restore){
    if(restore){memcpy(ao_arena,s,AO_SIZE);ao_globals(s+AO_SIZE,1);}else{memcpy(s,ao_arena,AO_SIZE);ao_globals(s+AO_SIZE,0);}
}
static int ai_order_cases(unsigned index,unsigned limit){
    if(limit&&compare_probe_installed(index))return compare_probe_run(index,limit,ao_compare_build,ao_compare_state,AO_SIZE+AO_GLOBALS);
    BYTE saved[AO_IDS][5],old_globals[AO_GLOBALS];int active[AO_IDS];
    ao_globals(old_globals,0);if(ao_patch(saved,active,ao_direct_stubs(index),1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_AI_ORDER_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    memset(ao_states,0,sizeof(ao_states));memset(ao_types,0,sizeof(ao_types));
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];ao_build(index,n,a,r);
        if(n<16384&&index==ES2_HOOK_INDEX_order_resume){unsigned p=ao_path();if(p>=1&&p<=7)++ao_states[p];if(p>=9)++ao_types[p-9];}
        status=ao_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    /* Every reachable entry of both 41eb34 jump tables and their default
     * edges in the instrumented prefix (state 0 never reaches 41ebef). */
    if(!limit&&!status&&index==ES2_HOOK_INDEX_order_resume){
        for(unsigned s=1;s<8&&!status;++s)if(!ao_states[s]){fprintf(stderr,"FAIL: order_resume state %u never run\n",s);status=1;}
        for(unsigned t=0;t<8&&!status;++t)if(!ao_types[t]){fprintf(stderr,"FAIL: order_resume group type %u never run\n",t);status=1;}
        if(!status){printf("JUMPTABLE order_resume 41ebef states");for(unsigned s=1;s<8;++s)printf(" %u:%llu",s,(unsigned long long)ao_states[s]);
            printf(" (7 = above 6); 41ed22 types");for(unsigned t=0;t<8;++t)printf(" %u:%llu",t,(unsigned long long)ao_types[t]);printf(" (7 = above 6)\n");}
    }
    if(!limit&&!status)status=ao_installed_probes(index,64);
    if(ao_patch(saved,active,0,0))return 1;
    current_case_valid=0;
    if(!limit&&!status&&index!=ES2_HOOK_INDEX_target_class)status=ao_real_pass(index);
    ao_globals(old_globals,1);
    if(!limit&&!status&&compare_probe_capable(index))status=compare_probe_run(index,0,ao_compare_build,ao_compare_state,AO_SIZE+AO_GLOBALS);
    ao_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
