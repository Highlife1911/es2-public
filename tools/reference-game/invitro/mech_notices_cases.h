/* MECH notices: mech_contact 412800 (virtual +48h), mech_kill 415710 (+60h),
 * mech_attacked 41f7b8 (+50h) and mech_alert 41f790
 * (docs/reverse-engineering/ai.md, section "MECH notices"). Direct cases run
 * the original body and the core against logging stubs for every game callee
 * (ordered argument slots and entry EAX/EDX/ECX, stack values masked); the
 * stubs return planned values (actors or 0 where the original dereferences the
 * result) and poke fields the originals read again after the call (flag bytes,
 * groups, side bytes, type and index words, targets, order state, timers, the
 * player pointer, the seen bytes and the timer word 4a9b65). The arena (eight
 * actors, four groups with leader lists, four state records) and the global
 * windows 499358, 4a9a80..4a9b87 (seen bytes, timer 4a9b64, player 4a9b84),
 * 4a9e84, 49b088 and 4d3b64 are compared and restored; the full EDX:EAX and the
 * callee counter deltas (first 4,096 cases) as well. Then installed 31/a7
 * poison probes and a real-callee pass: timer_word_step, mech_alert and
 * script_start nested in rotating modes, the real 420a98 with the cockpit's
 * message port virtual 0 as a stub (the record by content); the large AI
 * subtrees (411aec, 41dbcc, 41e0e0, 41ce08, 41c0f4, 411fa0, 41d5ec, 41eb34)
 * stay stubs. */
typedef struct mn_record { uint32_t id,args[3],regs[3],input[2],eax,edx; } mn_record;
enum { MN_SIZE=0x2800, MN_ACT=0x000, MN_ACTORS=8, MN_GRP=0x2000, MN_LIST=0x2100, MN_SR=0x2140, MN_H=0x2200, MN_PORT=0x2400,
    MN_PORTVT=0x2410, MN_MAX=32, MN_IDS=13, MN_GLOBALS=1+0x108+2+4+2 };
#define MN_BASE ((uint32_t)(uintptr_t)mn_arena)
#define MN_ACTOR(i) (MN_BASE+MN_ACT+0x400u*(uint32_t)(i))
#define MN_GROUP(g) (MN_BASE+MN_GRP+0x40u*(uint32_t)(g))
#define MN_RECORD(k) (MN_BASE+MN_SR+0x10u*(uint32_t)(k))
static BYTE mn_arena[MN_SIZE],mn_before[MN_SIZE],mn_after[MN_SIZE];
static mn_record mn_log[MN_MAX];
static unsigned mn_calls;
static int mn_real;
static uint32_t mn_seed,mn_stream;
__attribute__((used)) static uint32_t mn_ecx;
/* 0 timer_word_step 467a48, 1 420a98, 2 411aec, 3 actor_destination
 * 41dbcc, 4 target_search 41e0e0, 5 script_start 413e50, 6 part_pick
 * 41ce08, 7 attack_target 41c0f4, 8 target_select 411fa0, 9 attack_replan
 * 41d5ec, 10 order_resume 41eb34, 11 mech_alert 41f790 (only under
 * mech_attacked), 12 the cockpit message port's virtual 0 (only in the
 * real pass, behind the real 420a98). */
static const uint32_t mn_addresses[MN_IDS]={0x467a48,0x420a98,0x411aec,0x41dbcc,0x41e0e0,0x413e50,0x41ce08,0x41c0f4,0x411fa0,
    0x41d5ec,0x41eb34,0x41f790,0};
static const unsigned mn_counts[MN_IDS]={1,3,3,1,2,2,1,2,3,1,1,1,2};
/* Global windows (VA, size). */
static const uint32_t mn_global_va[5]={0x499358,0x4a9a80,0x4a9e84,0x49b088,0x4d3b64};
static const unsigned mn_global_size[5]={1,0x108,2,4,2};
static void mn_globals(BYTE *out,int restore){
    unsigned at=0;
    for(unsigned i=0;i<5;++i){BYTE *g=mapped+mn_global_va[i]-0x400000u;
        if(restore)memcpy(g,out+at,mn_global_size[i]);else memcpy(out+at,g,mn_global_size[i]);at+=mn_global_size[i];}
}
static uint32_t mn_next(void){
    uint32_t z=mn_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *mn_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static BYTE *mn_global(uint32_t va){return mapped+va-0x400000u;}
static int mn_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static uint32_t mn_actor_pick(uint32_t pick){return MN_ACTOR(pick%MN_ACTORS);}
/* An index word for +4Bh: the seen bytes 4a9b00[] and the contact rows
 * +0C2h[] stay inside the windows (-2Ch..67h, including the timer bytes;
 * the rows then reach down to the flag bytes +96h, not the pointers). */
static uint16_t mn_index(uint32_t pick){
    switch(pick%8){case 0: return (uint16_t)(0xffd4u+(pick>>8)%0x2cu); case 1: return (uint16_t)(0x64u+(pick>>8)%4u);
    default: return (uint16_t)((pick>>3)%0x64u);}
}
/* A type word for +1A8h: mostly 0..3, sometimes negative or 4..0Fh (the
 * kill words +2A4h[type] stay inside the actor). */
static uint16_t mn_type(uint32_t pick){
    switch(pick%8){case 0: return (uint16_t)(0xfff8u+(pick>>8)%8u); case 1: return (uint16_t)(4u+(pick>>8)%12u);
    default: return (uint16_t)((pick>>3)%4u);}
}
/* One poke of a field the originals read again after a call. Pointers stay
 * valid: actors, groups and records only, 0 where the original tests it. */
static void mn_poke(uint32_t pick){
    const uint32_t X=mn_actor_pick(pick>>4),q=pick>>8;
    static const unsigned flags[]={0x95,0x99,0xa3,0xa4,0xa5,0x9a,0x9d,0xac};
    switch(pick%16){
    case 0: case 1: mn_host(X)[flags[q%8]]=(BYTE)(q&0x100?0:1+(q>>9)%3);break;
    case 2: mn_global(0x4a9b00)[(int)(int16_t)mn_index(q)]=(BYTE)(q&0x400?0:1);break;
    case 3: put16(mn_global(0x4a9b65),(uint16_t)(q&0x100?0:q>>9));break;
    case 4: put32(mn_global(0x4a9b84),mn_actor_pick(q));break;
    case 5: put32(mn_host(X+0x45),MN_GROUP(q%4));break;
    case 6: mn_host(MN_GROUP(q%4))[0x12]=(BYTE)((q>>2)%3);break;
    case 7: put16(mn_host(X+0x1a8),mn_type(q));break;
    case 8: put16(mn_host(X+0x4b),mn_index(q));break;
    case 9: put32(mn_host(X+0x1a4),q%3?mn_actor_pick(q>>2):0);break;
    case 10: put32(mn_host(X+0x248),q%3?mn_actor_pick(q>>2):0);break;
    case 11: put16(mn_host(X+0x23e),(uint16_t)(q%2?4:q%7));break;
    case 12: { static const unsigned at[]={0x273,0x278,0x27d,0x281};put32(mn_host(X+at[q%4]),(q>>2)%3?0:8000u-16u+(q>>4)%32u);break; }
    case 13: mn_host(MN_RECORD(q%4))[1+(q>>2)%3]=(BYTE)(q&0x40?0:1);break;
    case 14: put32(mn_host(X+0x4d),MN_RECORD(q%4)-8u);break;
    default: mn_host(X)[0xc2+(int)(int16_t)mn_index(q)]=(BYTE)(q&0x100?0:1);break;
    }
}
__attribute__((used,noinline)) static uint64_t __cdecl mn_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    uint32_t eax=mn_next(),edx=mn_next(),pick=mn_next();mn_ecx=mn_next();
    if(mn_calls==MN_MAX){fail("mech notices call overflow");ExitProcess(1);}
    mn_record *v=mn_log+mn_calls++;memset(v,0,sizeof(*v));v->id=id;
    for(unsigned k=0;k<mn_counts[id];++k)v->args[k]=mn_stack(a[k])?0:a[k];
    for(unsigned j=0;j<3;++j)v->regs[j]=mn_stack(regs[j])?0:regs[j];
    switch(id){
    case 0: /* timer_word_step: the timer word expired or not. */
        put16(mn_global(0x4a9b65),(uint16_t)(pick%2?0:1+(pick>>8)%500u));break;
    case 4: case 8: /* target_search, target_select: 0 or an actor, which may attack A. */
        eax=pick%3==0?0:mn_actor_pick(pick>>4);
        if(eax&&pick%16<8)put32(mn_host(eax+0x1a4),a[0]);
        break;
    case 12: /* the message port: the record (code word, vehicle) by content. */
        v->input[0]=get16(mn_host(a[1]));v->input[1]=get32(mn_host(a[1]+2));break;
    default:break;
    }
    if(!mn_real&&id!=12&&pick%4==0){mn_poke(mn_next());if(pick%8==0)mn_poke(mn_next());}
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define MN_STUB(n) __attribute__((naked)) static void mn_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _mn_stub_impl\n\taddl $24,%esp\n\tmovl _mn_ecx,%ecx\n\tret\n\t");}
MN_STUB(0) MN_STUB(1) MN_STUB(2) MN_STUB(3) MN_STUB(4) MN_STUB(5) MN_STUB(6) MN_STUB(7) MN_STUB(8) MN_STUB(9)
MN_STUB(10) MN_STUB(11) MN_STUB(12)
#undef MN_STUB
static void *mn_stubs[MN_IDS]={mn_stub_0,mn_stub_1,mn_stub_2,mn_stub_3,mn_stub_4,mn_stub_5,mn_stub_6,mn_stub_7,mn_stub_8,mn_stub_9,
    mn_stub_10,mn_stub_11,mn_stub_12};
/* Calls target with three slots, EAX/EDX/ECX/EBX from regs and a patterned
 * stack below the arguments; returns EDX:EAX in pair. */
__attribute__((naked,noinline)) static void mn_invoke(void *target,const uint32_t *a,const uint32_t regs[4],uint32_t pattern,uint32_t pair[2]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 20(%ebp),%eax\n\tmovl $1024,%ecx\n\trep stosl\n\taddl $4084,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%esi\n\tmovl (%esi),%eax\n\tmovl 4(%esi),%edx\n\tmovl 8(%esi),%ecx\n\tmovl 12(%esi),%ebx\n\tcall *8(%ebp)\n\t"
        "movl 24(%ebp),%ecx\n\tmovl %eax,(%ecx)\n\tmovl %edx,4(%ecx)\n\tleal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static void mn_reset(void){mn_calls=0;mn_stream=mn_seed;}
static BYTE mn_flag(unsigned one_in){return (BYTE)(random32()%one_in==0?1u+random32()%0xffu:0u);}
/* One actor: group, state record, flags (set in one case of four to
 * eight), index and type words, targets, order state, timers, the contact
 * rows +0C2h (mostly clear) and the kill words. */
static void mn_actor(unsigned i){
    BYTE *p=mn_host(MN_ACTOR(i));
    memset(p+0x96,0,0x94);for(unsigned j=0;j<3;++j)if(random32()%4==0)p[0x96+random32()%0x94]=(BYTE)(1+random32()%0xff);
    put32(p+0x45,MN_GROUP(random32()%4));put16(p+0x4b,mn_index(random32()));put32(p+0x4d,MN_RECORD(random32()%4)-8u);
    static const unsigned flags[]={0x95,0x9a,0x9d,0xa6,0x99,0xa3,0xa4,0xa5,0xac},odds[]={2,2,2,2,4,5,5,5,5};
    for(unsigned j=0;j<9;++j)p[flags[j]]=mn_flag(odds[j]);
    put16(p+0x1a2,(uint16_t)(1+random32()%3));put32(p+0x1a4,random32()%3?mn_actor_pick(random32()):0);put16(p+0x1a8,mn_type(random32()));
    put16(p+0x23e,(uint16_t)(random32()%2?4:random32()%4?random32()%7:random32()));put32(p+0x248,random32()%3?mn_actor_pick(random32()):0);
    put32(p+0x273,random32()%3?0:random32());put32(p+0x278,random32()%2?0:random32());
    put32(p+0x281,random32()%2?8000u-24u+random32()%48u:random32()%3?random32()%8000u:random32());
}
/* The arena: eight actors, four groups (leader list +0Ch, side +12h),
 * the lists, four state records (bytes +1..+3 tested), the cockpit H with
 * the message port for the real 420a98. */
static void mn_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    const uint32_t b=MN_BASE;
    mn_seed=random32();random_bytes(mn_arena,MN_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    for(unsigned g=0;g<4;++g){BYTE *p=mn_host(MN_GROUP(g));put32(p+0xc,b+MN_LIST+4*g);p[0x12]=(BYTE)(random32()%4?random32()%2:random32());
        put32(mn_arena+MN_LIST+4*g,mn_actor_pick(random32()));}
    for(unsigned k=0;k<4;++k){BYTE *s=mn_host(MN_RECORD(k));for(unsigned j=1;j<4;++j)s[j]=mn_flag(j==2?2:4);}
    for(unsigned i=0;i<MN_ACTORS;++i)mn_actor(i);
    put32(mn_arena+MN_H+0x207,b+MN_PORT);put32(mn_arena+MN_PORT,b+MN_PORTVT);put32(mn_arena+MN_PORTVT,(uint32_t)(uintptr_t)mn_stubs[12]);
    put32(mn_global(0x49b088),b+MN_H);put16(mn_global(0x4d3b64),(uint16_t)(1+random32()%200));
    mn_global(0x499358)[0]=(BYTE)(random32()%2?0:random32());
    /* Seen bytes mostly clear; random other bytes up to the player pointer. */
    memset(mn_global(0x4a9a80),0,0xe8);for(unsigned j=0;j<4;++j)if(random32()%3==0)mn_global(0x4a9a80)[random32()%0xe8]=1;
    random_bytes(mn_global(0x4a9b68),0x1c);put16(mn_global(0x4a9b65),(uint16_t)(random32()%2?0:random32()));mn_global(0x4a9b64)[0]=(BYTE)random32();
    put16(mn_global(0x4a9e84),(uint16_t)random32());
    const uint32_t A=mn_actor_pick(random32()),O=random32()%8==0?A:mn_actor_pick(random32());
    uint32_t P=random32()%4==0?A:random32()%4==0?O:mn_actor_pick(random32());
    put32(mn_global(0x4a9b84),P);
    /* The player's group often A's (or O's); A and O on different sides. */
    if(random32()%2)put32(mn_host(P+0x45),get32(mn_host(A+0x45)));else if(random32()%2)put32(mn_host(P+0x45),get32(mn_host(O+0x45)));
    if(random32()%3&&get32(mn_host(A+0x45))==get32(mn_host(O+0x45)))put32(mn_host(O+0x45),MN_GROUP(random32()%4));
    a[0]=A;a[1]=O;
    if(index==ES2_HOOK_INDEX_mech_kill)a[2]=(random32()&0xffff0000u)|(random32()%2?0u:(uint16_t)random32());
    else if(index==ES2_HOOK_INDEX_mech_attacked){
        a[2]=random32()%2?(random32()&0xffff0000u)|(random32()%64):random32();
        /* S often A's current target or its leader's. */
        if(random32()%4==0)put32(mn_host(A+0x1a4),O);
        if(random32()%4==0)put32(mn_host(get32(mn_host(get32(mn_host(get32(mn_host(A+0x45))+0xc))))+0x1a4),O);
    } else if(index==ES2_HOOK_INDEX_mech_alert)a[1]=0;
    (void)n;
}
static int mn_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const mn_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,mn_calls);
    for(unsigned j=0;j<calls&&j<mn_calls&&expected;++j)if(memcmp(expected+j,mn_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(mn_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<MN_SIZE;++j)if(mn_after[j]!=mn_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,mn_after[j],mn_arena[j]);break;}
    for(unsigned j=0;j<calls&&expected&&j<16;++j)fprintf(stderr," trace %u id%u args %08x %08x %08x regs %08x %08x %08x\n",j,expected[j].id,
        expected[j].args[0],expected[j].args[1],expected[j].args[2],expected[j].regs[0],expected[j].regs[1],expected[j].regs[2]);
    return 1;
}
static unsigned mn_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<mn_calls;++j)if(mn_addresses[mn_log[j].id]==c->key)++hits;
    return hits;
}
static int mn_globals_report(const BYTE *after,const BYTE *actual){
    for(unsigned j=0;j<MN_GLOBALS;++j)if(after[j]!=actual[j]){fprintf(stderr," globals byte %u %02x/%02x\n",j,after[j],actual[j]);break;}
    return 1;
}
static int mn_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[MN_GLOBALS],after[MN_GLOBALS],actual[MN_GLOBALS];static mn_record expected[MN_MAX];
    memcpy(mn_before,mn_arena,MN_SIZE);mn_globals(before,0);
    mn_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=mn_calls;memcpy(expected,mn_log,calls*sizeof(*expected));
    memcpy(mn_after,mn_arena,MN_SIZE);mn_globals(after,0);
    memcpy(mn_arena,mn_before,MN_SIZE);mn_globals(before,1);mn_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);mn_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=mn_expected_hits(c+i))return fail("mech notices callee counter delta");
    if(pair[0]!=pair[1]||calls!=mn_calls||memcmp(expected,mn_log,calls*sizeof(*expected))||memcmp(mn_after,mn_arena,MN_SIZE)||
       memcmp(after,actual,sizeof(after))){
        if(memcmp(after,actual,sizeof(after)))mn_globals_report(after,actual);
        fprintf(stderr," args %08x %08x %08x regs %08x %08x %08x arena %08x player %08x\n",a[0],a[1],a[2],r[0],r[1],r[2],MN_BASE,get32(mn_global(0x4a9b84)));
        return mn_report("direct",n,pair,calls,expected);
    }
    return 0;
}
static int mn_patch(BYTE saved[MN_IDS][5],int active[MN_IDS],uint32_t which,int on){
    for(unsigned i=0;i<MN_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!mn_addresses[i])continue;if(actor_patch(mn_addresses[i]-0x400000u,mn_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(mn_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
/* Direct-call stubs (bit i = callee id i): every callee; 41f790 only under
 * mech_attacked (it is mech_alert's own body). */
static uint32_t mn_direct_stubs(unsigned index){
    return index==ES2_HOOK_INDEX_mech_attacked?0x7ffu|1u<<11:0x7ffu;
}
static int mn_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],regs[4],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;mn_build(index,n,a,r);
        regs[0]=regs[1]=regs[2]=regs[3]=pattern;
        BYTE before[MN_GLOBALS],after[MN_GLOBALS],actual[MN_GLOBALS],expected_fpu[108],actual_fpu[108];static mn_record expected[MN_MAX];
        memcpy(mn_before,mn_arena,MN_SIZE);mn_globals(before,0);
        mn_reset();mn_invoke(mapped+d->rva,a,regs,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=mn_calls;memcpy(expected,mn_log,calls*sizeof(*expected));memcpy(mn_after,mn_arena,MN_SIZE);mn_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("mech notices poison install");
        memcpy(mn_arena,mn_before,MN_SIZE);mn_globals(before,1);mn_reset();mn_invoke(mapped+d->rva,a,regs,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);mn_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=mn_calls||memcmp(expected,mn_log,calls*sizeof(*expected))||memcmp(mn_after,mn_arena,MN_SIZE)||
           memcmp(after,actual,sizeof(after))||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=mn_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("mech notices poison uninstall");mn_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state\n",d->name,total);
    return status;
}
/* Real callees with the nested hooks in rotating original/replaced/compare
 * modes (compare falls back to replaced where refused), the real 420a98
 * and a patterned stack; the port's virtual 0 and, under mech_attacked,
 * the large AI subtrees stay stubs (inputs compared in order, without the
 * entry registers). The hook under test original (modes 0-2) or replaced
 * (3-5) against a run with the same nested hooks. */
static int mn_real_pass(unsigned index){
    static const unsigned contact_nested[]={ES2_HOOK_INDEX_timer_word_step};
    static const unsigned attacked_nested[]={ES2_HOOK_INDEX_mech_alert,ES2_HOOK_INDEX_script_start};
    const unsigned *nested=index==ES2_HOOK_INDEX_mech_contact?contact_nested:index==ES2_HOOK_INDEX_mech_attacked?attacked_nested:NULL;
    const unsigned count=index==ES2_HOOK_INDEX_mech_contact?1u:index==ES2_HOOK_INDEX_mech_attacked?2u:0u;
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[2]={0},posted=0;
    /* Stubs: the AI subtrees under mech_attacked (411aec .. 41eb34 except
     * script_start); nothing else (420a98 runs as game code). */
    const uint32_t stubs=index==ES2_HOOK_INDEX_mech_attacked?(1u<<2)|(1u<<3)|(1u<<4)|(1u<<6)|(1u<<7)|(1u<<8)|(1u<<9)|(1u<<10):0u;
    BYTE saved[MN_IDS][5];int active[MN_IDS];if(mn_patch(saved,active,stubs,1))return 1;
    static mn_record expected[MN_MAX];
    mn_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],regs[4],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;mn_build(index,n,a,r);
        regs[0]=regs[1]=regs[2]=regs[3]=pattern;
        BYTE before[MN_GLOBALS],after[MN_GLOBALS],actual[MN_GLOBALS];
        memcpy(mn_before,mn_arena,MN_SIZE);mn_globals(before,0);
        es2_hook hk[3];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!nested_install_hook(hk+j,nested[j],&o,e))return fail("mech notices nested install");}
        mn_reset();mn_invoke(mapped+d->rva,a,regs,pattern,expected_pair);
        unsigned calls=mn_calls;memcpy(expected,mn_log,calls*sizeof(*expected));
        for(unsigned j=0;j<calls;++j)if(expected[j].id==12)++posted;
        memcpy(mn_after,mn_arena,MN_SIZE);mn_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+count,d,&o,function_hook_extension(index)))return fail("mech notices caller install");
        memcpy(mn_arena,mn_before,MN_SIZE);mn_globals(before,1);mn_reset();
        mn_invoke(mapped+d->rva,a,regs,pattern,actual_pair);mn_globals(actual,0);
        /* Entry registers are compared in the direct pass only (a replaced
         * nested hook leaves other scratch registers than the original). */
        for(unsigned j=0;j<calls&&j<mn_calls;++j){memset(expected[j].regs,0,12);memset(mn_log[j].regs,0,12);}
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=mn_calls||memcmp(expected,mn_log,calls*sizeof(*expected))||
           memcmp(mn_after,mn_arena,MN_SIZE)||memcmp(after,actual,sizeof(after))){
            fprintf(stderr,"real mode=%u\n",mode);if(memcmp(after,actual,sizeof(after)))mn_globals_report(after,actual);
            status=mn_report("real",n,pair,calls,expected);
        }
        for(unsigned j=0;j<=count;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("mech notices nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("mech notices nested uninstall");}
        mn_globals(before,1);++total;
    }
    mn_real=0;
    if(mn_patch(saved,active,0,0))return 1;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){
        fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&!posted){fprintf(stderr,"FAIL: %s real pass never reached the message port\n",d->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, real 420a98 (%llu port posts), 31/a7 stack, pair, memory, globals and stub inputs\n",
        d->name,total,count,(unsigned long long)posted);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
static int mech_notices_cases(unsigned index,unsigned limit){
    BYTE saved[MN_IDS][5],old_globals[MN_GLOBALS];int active[MN_IDS];
    mn_globals(old_globals,0);if(mn_patch(saved,active,mn_direct_stubs(index),1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_MECH_NOTICES_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];mn_build(index,n,a,r);status=mn_compare(index,n,a,r);mn_globals(old_globals,1);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=mn_installed_probes(index,64);
    if(mn_patch(saved,active,0,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=mn_real_pass(index);
    mn_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
