/* Cockpit target selection: cockpit_valid 433174, cockpit_arc 433250,
 * cockpit_next 43349c, cockpit_nearest 4333c8 and cockpit_select 4332dc
 * (docs/reverse-engineering/hud.md, "cockpit target cycling as own code").
 * Direct cases run the original body and the core against logging stubs for
 * every game callee and virtual (ordered argument slots, entry EAX/EDX/ECX,
 * position vectors and the HUD record by content); the arena, the eight globals
 * and the full EDX:EAX are compared. Then installed 31/a7 poison probes and a
 * real-callee pass with the nested hooks in rotating modes; only the vehicle
 * virtuals +3Ch/+10h and the HUD record update 43d9b4 stay stubs there. */
typedef struct ct_record { uint32_t id,args[2],regs[3],input[10],eax,edx,slots; } ct_record;
enum { CT_SIZE=0x1800, CT_H=0x100, CT_HUD=0x400, CT_VT=0x500, CT_GRP=0x540, CT_LIST=0x600, CT_TOP=0x640,
    CT_BUCKETS=0x660, CT_ARRAYS=0x6a0, CT_ACT=0x800, CT_ACTORS=7, CT_SLOTS=16, CT_MAX=160, CT_IDS=10, CT_GLOBALS=28 };
#define CT_BASE ((uint32_t)(uintptr_t)ct_arena)
#define CT_ACTOR(i) (CT_BASE+CT_ACT+0x200u*(uint32_t)(i))
static BYTE ct_arena[CT_SIZE],ct_before[CT_SIZE],ct_after[CT_SIZE];
static ct_record ct_log[CT_MAX];
static unsigned ct_calls;
static int ct_real;
static unsigned ct_hook;
static uint32_t ct_seed,ct_stream,ct_allowed[CT_SLOTS+1];
static unsigned ct_allowed_count;
__attribute__((used)) static uint32_t ct_ecx;
/* 0 49288c dist_gate, 1 492934 heading_point, 2 V3c, 3 V10, 4 47d32c
 * angle_ratio, 5 47e0b0 multiply_q10, 6 43d9b4 HUD record, 7 433174,
 * 8 433250, 9 43349c. Virtuals have no address; one table serves every
 * actor. */
static const uint32_t ct_addresses[CT_IDS]={0x49288c,0x492934,0,0,0x47d32c,0x47e0b0,0x43d9b4,0x433174,0x433250,0x43349c};
static const unsigned ct_counts[CT_IDS]={2,2,1,1,2,2,2,2,2,1},ct_slots[CT_IDS]={0,0,0x3c,0x10,0,0,0,0,0,0};
/* Globals (VA, size): view flag, view subject, near limit, list pointer,
 * top list, buckets, far-limit word and count word. */
static const uint32_t ct_global_va[8]={0x49ef34,0x4d2684,0x4d1c78,0x4a9af8,0x4d040c,0x4d0410,0x4a9e5e,0x4a9afe};
static const unsigned ct_global_size[8]={4,4,4,4,4,4,2,2};
static void ct_globals(BYTE *out,int restore){
    unsigned at=0;
    for(unsigned i=0;i<8;++i){BYTE *g=mapped+ct_global_va[i]-0x400000u;
        if(restore)memcpy(g,out+at,ct_global_size[i]);else memcpy(out+at,g,ct_global_size[i]);at+=ct_global_size[i];}
}
static uint32_t ct_next(void){
    uint32_t z=ct_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *ct_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int ct_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static void ct_vector(uint32_t at,uint32_t *in){for(unsigned j=0;j<3;++j)in[j]=get32(ct_host(at)+4*j);}
/* A word near the arc limits and the 8000h special case, or anything. */
static uint16_t ct_heading(uint32_t pick){
    static const uint16_t words[]={8999,9000,9001,(uint16_t)-8999,(uint16_t)-9000,(uint16_t)-9001,0x8000,0x7fff,0,1,0xffff,0x400,0xc00};
    return pick%3?words[(pick>>4)%13]:(uint16_t)(pick>>8);
}
__attribute__((used,noinline)) static uint64_t __cdecl ct_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    uint32_t eax=ct_next(),edx=ct_next(),pick=ct_next();ct_ecx=ct_next();
    if(ct_calls==CT_MAX){fail("cockpit target call overflow");ExitProcess(1);}
    ct_record *v=ct_log+ct_calls++;memset(v,0,sizeof(*v));v->id=id;v->slots=weapons_bridge_slots(a,ct_counts[id]);
    for(unsigned k=0;k<ct_counts[id];++k)v->args[k]=ct_stack(a[k])?0:a[k];
    if(!ct_real)for(unsigned j=0;j<3;++j)v->regs[j]=ct_stack(regs[j])?0:regs[j];
    const uint32_t H=CT_BASE+CT_H;
    switch(id){
    case 0: { /* dist_gate: near both limits, the bucket fill value, negative. */
        ct_vector(a[0],v->input);ct_vector(a[1],v->input+3);
        static const uint32_t gates[]={29999,30000,30001,59999,60000,60001,0x7ffffffu,0x7fffffeu,0x8000000u,0,1,0x80000000u,0x7fffffffu};
        const uint32_t limit=get32(mapped+0xd1c78);
        eax=pick%4==0?gates[(pick>>4)%13]:pick%4==1?limit+(pick>>8)%3-1u:pick%4==2?(pick>>8)%2000u:eax;
        if(pick%32==5)put32(ct_host(H+0x203),CT_ACTOR((pick>>12)%CT_ACTORS));
        break; }
    case 1: /* heading_point: arc limits, 8000h; may switch the player. */
        ct_vector(a[0],v->input);ct_vector(a[1],v->input+3);
        eax=(eax&0xffff0000u)|ct_heading(pick);
        if(pick%32==7)put32(ct_host(H+0x203),CT_ACTOR((pick>>12)%CT_ACTORS));
        break;
    case 2: /* the player's twist: zero, small or anything. */
        eax=pick%3==0?eax&0xffff0000u:pick%3==1?(eax&0xffff0000u)|(uint16_t)((pick>>8)%401u-200u):eax;
        break;
    case 3: /* the target's radius: small in the real pass (angle_ratio). */
        if(ct_real)eax=(eax&0xffff0000u)|(pick>>8)%2000u;
        break;
    case 5: /* multiply_q10: sometimes above |b| (negative rest), sometimes 0. */
        if(pick%3==0)eax&=0xffff0000u;
        break;
    case 6: /* the HUD record by content. */
        for(unsigned j=0;j<10;++j)v->input[j]=get32(ct_host(a[1])+4*j);
        break;
    case 7:case 8: /* AL zero or not; may replace the cockpit target. */
        eax=pick%3==0?eax&0xffffff00u:eax|1u;
        if(pick%16==3)put32(ct_host(H+0x210),pick%2?0:CT_ACTOR((pick>>8)%CT_ACTORS));
        break;
    case 9: /* 43349c: a target the select walk reaches (precondition). */
        put32(ct_host(H+0x210),ct_allowed[(pick>>4)%ct_allowed_count]);
        break;
    default:break;
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define CT_STUB(n) __attribute__((naked)) static void ct_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _ct_stub_impl\n\taddl $24,%esp\n\tmovl _ct_ecx,%ecx\n\tret\n\t");}
CT_STUB(0) CT_STUB(1) CT_STUB(2) CT_STUB(3) CT_STUB(4) CT_STUB(5) CT_STUB(6) CT_STUB(7) CT_STUB(8) CT_STUB(9)
#undef CT_STUB
static void *ct_stubs[CT_IDS]={ct_stub_0,ct_stub_1,ct_stub_2,ct_stub_3,ct_stub_4,ct_stub_5,ct_stub_6,ct_stub_7,ct_stub_8,ct_stub_9};
/* Calls target with the slots, EAX/EDX/ECX/EBX from regs and a patterned
 * stack below the arguments; returns EDX:EAX in pair. */
__attribute__((naked,noinline)) static void ct_invoke(void *target,const uint32_t *a,const uint32_t regs[4],uint32_t pattern,uint32_t pair[2]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 20(%ebp),%eax\n\tmovl $1024,%ecx\n\trep stosl\n\taddl $4088,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%esi\n\tmovl (%esi),%eax\n\tmovl 4(%esi),%edx\n\tmovl 8(%esi),%ecx\n\tmovl 12(%esi),%ebx\n\tcall *8(%ebp)\n\t"
        "movl 24(%ebp),%ecx\n\tmovl %eax,(%ecx)\n\tmovl %edx,4(%ecx)\n\tleal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static void ct_reset(void){ct_calls=0;ct_stream=ct_seed;}
static uint32_t ct_actor_or_zero(void){return random32()%5==0?0:CT_ACTOR(random32()%CT_ACTORS);}
/* One cockpit H with HUD object, up to seven actors (actor 0 is usually
 * the player) in four groups, the actor list (16 slots, count -2..14), the
 * four-entry list and the bucket blocks; the select walk's cursor stays
 * inside the slots and its start target is one the walk reaches. */
static void ct_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    const uint32_t b=CT_BASE,H=b+CT_H;
    ct_seed=random32();random_bytes(ct_arena,CT_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    for(unsigned s=0;s<0x40;s+=4)put32(ct_arena+CT_VT+s,0);
    put32(ct_arena+CT_VT+0x3c,(uint32_t)(uintptr_t)ct_stubs[2]);put32(ct_arena+CT_VT+0x10,(uint32_t)(uintptr_t)ct_stubs[3]);
    for(unsigned g=0;g<4;++g)ct_arena[CT_GRP+0x20*g+0x12]=(BYTE)(random32()%3?random32()%3:random32());
    for(unsigned i=0;i<CT_ACTORS;++i){
        const uint32_t A=CT_ACTOR(i);BYTE *p=ct_host(A);
        put32(p,b+CT_VT);
        for(unsigned j=0;j<3;++j)put32(p+0x26+4*j,random32()%3?random32()%0x80000u-0x40000u:random32());
        put16(p+0x10,random32()%2?0:(uint16_t)random32());
        put32(p+0x45,b+CT_GRP+0x20*(random32()%4));
        put16(p+0x4b,(uint16_t)(random32()%0xa0u-0x20u));
        p[0x95]=(BYTE)(random32()%2?0:random32());p[0x99]=(BYTE)(random32()%4?0:random32());p[0xa4]=(BYTE)(random32()%4?0:random32());
        for(unsigned j=0;j<0xa0;++j)if(random32()%2)p[0xa2+j]=0;
        static const uint16_t types[]={0,0,2,2,1,3};
        put16(p+0x1a8,random32()%8?types[random32()%6]:(uint16_t)random32());
    }
    put32(ct_host(H+0x203),random32()%4?CT_ACTOR(0):CT_ACTOR(random32()%CT_ACTORS));
    put32(ct_host(H+0x1f5),b+CT_HUD);
    /* Globals: view flag and subject, near limit, far-limit word. */
    put32(mapped+0x9ef34,random32()%3?0:1+random32()%0xff);put32(mapped+0xd2684,CT_ACTOR(random32()%CT_ACTORS));
    static const uint32_t limits[]={0,1000,30000,60000,0x7fffffffu};
    put32(mapped+0xd1c78,random32()%2?limits[random32()%5]:random32()%70000u);
    put16(mapped+0xa9e5e,(uint16_t)(random32()%2?1:random32()%3));
    /* Actor list: a count of -2..14 over 16 slots, nulls now and then. */
    put32(mapped+0xa9af8,b+CT_LIST);
    int32_t count=random32()%8==0?(int32_t)(random32()%3)-2:(int32_t)(random32()%15);
    put16(mapped+0xa9afe,(uint16_t)count);
    for(unsigned j=0;j<CT_SLOTS;++j)put32(ct_arena+CT_LIST+4*j,random32()%9==0?0:CT_ACTOR(random32()%CT_ACTORS));
    if(random32()%4==0)put32(ct_arena+CT_LIST+4*(random32()%CT_SLOTS),0);
    /* The top list (old head) and the buckets (counts and arrays get cleared). */
    put32(mapped+0xd040c,b+CT_TOP);put32(mapped+0xd0410,b+CT_BUCKETS);
    for(unsigned j=0;j<4;++j)put32(ct_arena+CT_TOP+4*j,ct_actor_or_zero());
    for(unsigned k=0;k<4;++k){put32(ct_arena+CT_BUCKETS+12*k+4,b+CT_ARRAYS+0x20*k);put32(ct_arena+CT_BUCKETS+12*k+8,b+CT_ARRAYS+0x20*k+0x10);}
    /* Targets the select walk reaches after its reset: 0 and the list
     * prefix before the first null within the count. */
    ct_allowed_count=0;ct_allowed[ct_allowed_count++]=0;
    for(int32_t j=0;j<count&&j<CT_SLOTS;++j){uint32_t t=get32(ct_arena+CT_LIST+4*j);if(!t)break;ct_allowed[ct_allowed_count++]=t;}
    /* Cursor: index i, pointer slot s, so that the walk stays in the slots. */
    uint32_t i=random32()%(uint32_t)(count+2>0?count+2:1);
    int32_t left=count-(int32_t)i;uint32_t span=left>0?(uint32_t)(CT_SLOTS-left):CT_SLOTS-1u;
    put32(ct_host(H+0x214),b+CT_LIST+4*(random32()%(span+1)));put16(ct_host(H+0x218),(uint16_t)i);
    if(index==ES2_HOOK_INDEX_cockpit_select)
        put32(ct_host(H+0x210),random32()%4==0?0:ct_allowed[random32()%ct_allowed_count]);
    else put32(ct_host(H+0x210),random32()%3==0?0:random32()%2?get32(ct_arena+CT_TOP+4*(random32()%4)):ct_actor_or_zero());
    a[0]=H;
    if(index==ES2_HOOK_INDEX_cockpit_valid||index==ES2_HOOK_INDEX_cockpit_arc)a[1]=CT_ACTOR(random32()%CT_ACTORS);
    if(index==ES2_HOOK_INDEX_cockpit_select)a[1]=random32()%5==0?0:random32()%3?ct_allowed[random32()%ct_allowed_count]:CT_ACTOR(random32()%CT_ACTORS);
    (void)n;
}
static int ct_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const ct_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,ct_calls);
    for(unsigned j=0;j<calls&&j<ct_calls&&expected;++j)if(memcmp(expected+j,ct_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(ct_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<CT_SIZE;++j)if(ct_after[j]!=ct_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,ct_after[j],ct_arena[j]);break;}
    for(unsigned j=0;j<calls&&expected&&j<24;++j)fprintf(stderr," trace %u id%u args %08x %08x eax %08x\n",j,expected[j].id,
        expected[j].args[0],expected[j].args[1],expected[j].eax);
    return 1;
}
static unsigned ct_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<ct_calls;++j){unsigned id=ct_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?ct_slots[id]==c->key:!ct_slots[id]&&ct_addresses[id]==c->key)++hits;}
    return hits;
}
static int ct_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[CT_GLOBALS],after[CT_GLOBALS],actual[CT_GLOBALS];static ct_record expected[CT_MAX];
    memcpy(ct_before,ct_arena,CT_SIZE);ct_globals(before,0);
    ct_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=ct_calls;memcpy(expected,ct_log,calls*sizeof(*expected));
    memcpy(ct_after,ct_arena,CT_SIZE);ct_globals(after,0);
    memcpy(ct_arena,ct_before,CT_SIZE);ct_globals(before,1);ct_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);ct_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=ct_expected_hits(c+i))return fail("cockpit target callee counter delta");
    if(pair[0]!=pair[1]||calls!=ct_calls||memcmp(expected,ct_log,calls*sizeof(*expected))||memcmp(ct_after,ct_arena,CT_SIZE)||
       memcmp(after,actual,sizeof(after)))
        return ct_report("direct",n,pair,calls,expected);
    return 0;
}
static int ct_patch(BYTE saved[CT_IDS][5],int active[CT_IDS],uint32_t which,int on){
    for(unsigned i=0;i<CT_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!ct_addresses[i])continue;if(actor_patch(ct_addresses[i]-0x400000u,ct_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(ct_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
/* Direct-call stubs per hook (bit i = callee id i). */
static uint32_t ct_direct_stubs(unsigned index){
    if(index==ES2_HOOK_INDEX_cockpit_valid)return 1u<<0;
    if(index==ES2_HOOK_INDEX_cockpit_arc)return 1u<<1;
    if(index==ES2_HOOK_INDEX_cockpit_next)return 0x1f3u;
    if(index==ES2_HOOK_INDEX_cockpit_nearest)return (1u<<0)|(1u<<6)|(1u<<7);
    return (1u<<6)|(1u<<7)|(1u<<8)|(1u<<9);
}
static int ct_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],regs[4],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;ct_build(index,n,a,r);
        regs[0]=regs[1]=regs[2]=regs[3]=pattern;
        BYTE before[CT_GLOBALS],after[CT_GLOBALS],actual[CT_GLOBALS],expected_fpu[108],actual_fpu[108];static ct_record expected[CT_MAX];
        memcpy(ct_before,ct_arena,CT_SIZE);ct_globals(before,0);
        ct_reset();ct_invoke(mapped+d->rva,a,regs,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=ct_calls;memcpy(expected,ct_log,calls*sizeof(*expected));memcpy(ct_after,ct_arena,CT_SIZE);ct_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("cockpit target poison install");
        memcpy(ct_arena,ct_before,CT_SIZE);ct_globals(before,1);ct_reset();ct_invoke(mapped+d->rva,a,regs,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);ct_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=ct_calls||memcmp(expected,ct_log,calls*sizeof(*expected))||memcmp(ct_after,ct_arena,CT_SIZE)||
           memcmp(after,actual,sizeof(after))||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=ct_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("cockpit target poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state\n",d->name,total);
    return status;
}
/* Real callees with the nested hooks in rotating original/replaced/compare
 * modes (compare falls back to replaced where refused) and a patterned
 * stack; the virtuals and 43d9b4 stay stubs and their inputs are compared
 * in order (entry registers only in the direct pass). */
static int ct_real_pass(unsigned index){
    static const unsigned valid_nested[]={ES2_HOOK_INDEX_dist_gate};
    static const unsigned arc_nested[]={ES2_HOOK_INDEX_heading_point};
    static const unsigned next_nested[]={ES2_HOOK_INDEX_dist_gate,ES2_HOOK_INDEX_heading_point,ES2_HOOK_INDEX_angle_ratio,
        ES2_HOOK_INDEX_multiply_q10,ES2_HOOK_INDEX_cockpit_valid,ES2_HOOK_INDEX_cockpit_arc};
    static const unsigned nearest_nested[]={ES2_HOOK_INDEX_dist_gate,ES2_HOOK_INDEX_cockpit_valid};
    static const unsigned select_nested[]={ES2_HOOK_INDEX_dist_gate,ES2_HOOK_INDEX_heading_point,ES2_HOOK_INDEX_angle_ratio,
        ES2_HOOK_INDEX_multiply_q10,ES2_HOOK_INDEX_cockpit_valid,ES2_HOOK_INDEX_cockpit_arc,ES2_HOOK_INDEX_cockpit_next};
    const unsigned *nested;unsigned count;
    if(index==ES2_HOOK_INDEX_cockpit_valid){nested=valid_nested;count=1;}
    else if(index==ES2_HOOK_INDEX_cockpit_arc){nested=arc_nested;count=1;}
    else if(index==ES2_HOOK_INDEX_cockpit_next){nested=next_nested;count=6;}
    else if(index==ES2_HOOK_INDEX_cockpit_nearest){nested=nearest_nested;count=2;}
    else {nested=select_nested;count=7;}
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[8]={0};
    BYTE saved[CT_IDS][5];int active[CT_IDS];if(ct_patch(saved,active,1u<<6,1))return 1;
    static ct_record expected[CT_MAX];
    ct_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<1024&&!status;++n){
        uint32_t a[6],r[3],regs[4],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;ct_build(index,n,a,r);
        /* Positions within reach so that the real distances and headings
         * vary around the limits. */
        for(unsigned i=0;i<CT_ACTORS;++i)for(unsigned j=0;j<3;++j)put32(ct_host(CT_ACTOR(i))+0x26+4*j,random32()%0x20000u-0x10000u);
        regs[0]=regs[1]=regs[2]=regs[3]=pattern;
        BYTE before[CT_GLOBALS],after[CT_GLOBALS],actual[CT_GLOBALS];
        memcpy(ct_before,ct_arena,CT_SIZE);ct_globals(before,0);
        es2_hook hk[8];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("cockpit target nested install");}
        ct_reset();ct_invoke(mapped+d->rva,a,regs,pattern,expected_pair);
        unsigned calls=ct_calls;memcpy(expected,ct_log,calls*sizeof(*expected));
        memcpy(ct_after,ct_arena,CT_SIZE);ct_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+count,d,&o,function_hook_extension(index)))return fail("cockpit target caller install");
        memcpy(ct_arena,ct_before,CT_SIZE);ct_globals(before,1);ct_reset();
        ct_invoke(mapped+d->rva,a,regs,pattern,actual_pair);ct_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=ct_calls||memcmp(expected,ct_log,calls*sizeof(*expected))||
           memcmp(ct_after,ct_arena,CT_SIZE)||memcmp(after,actual,sizeof(after))){
            fprintf(stderr,"real mode=%u\n",mode);status=ct_report("real",n,pair,calls,expected);
        }
        for(unsigned j=0;j<=count;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("cockpit target nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("cockpit target nested uninstall");}
        ct_globals(before,1);++total;
    }
    ct_real=0;
    if(ct_patch(saved,active,0,0))return 1;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){
        fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, 31/a7 stack, pair, memory, globals and stub inputs\n",
        d->name,total,count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
static int cockpit_target_cases(unsigned index,unsigned limit){
    BYTE saved[CT_IDS][5],old_globals[CT_GLOBALS];int active[CT_IDS];
    ct_hook=index;ct_globals(old_globals,0);if(ct_patch(saved,active,ct_direct_stubs(index),1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_COCKPIT_TARGET_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];ct_build(index,n,a,r);status=ct_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=ct_installed_probes(index,64);
    if(ct_patch(saved,active,0,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=ct_real_pass(index);
    ct_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, payloads by content, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
