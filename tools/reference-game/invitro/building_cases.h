/* Building logic (weapons.md, "Building logic: target selection and destruction
 * actions"): target_select 411fa0, target_valid 411e80, death_actions 411bc8,
 * group_death 423f30. Direct cases: every game callee is a logging stub that
 * records its ordered arguments and entry registers and changes candidate
 * flags, groups, orders, the actor list and the variable tables between calls,
 * so re-reads are checked. The actors' virtual +4Ch is a stub reached through
 * the generated vtable in every pass. Then installed 31/a7 poison cases, and a
 * real-callee pass: the group's hooks, target_known, order_target, dist_gate,
 * heading_point and multiply_q10 nested in rotating modes over the real 4238a0.
 * Generated states respect the one fault of the original: [C+1A2] is never
 * FFFFh and is at least 1 for the attacker's current target, so the IDIV at
 * 412352 never divides by zero. */
typedef struct bd_record { uint32_t id,args[3],regs[3],eax,edx,slots; } bd_record;
enum { BD_SIZE=0x2000, BD_ACTORS=0x0000, BD_GROUPS=0x1000, BD_ORDERS=0x1200, BD_MEMBERS=0x1300, BD_LIST=0x1400,
    BD_VTABLE=0x1480, BD_RECORDS=0x1500, BD_COCKPIT=0x1600, BD_MAX=256, BD_IDS=9, BD_NACTORS=8, BD_NGROUPS=4,
    BD_NORDERS=6, BD_NLIST=10, BD_NRECORDS=4, BD_GLOBALS=0x900 };
#define BD_BASE ((uint32_t)(uintptr_t)bd_arena)
static BYTE bd_arena[BD_SIZE],bd_before[BD_SIZE],bd_after[BD_SIZE];
static bd_record bd_log[BD_MAX];
static unsigned bd_calls,bd_index;
static int bd_real;
static uint32_t bd_seed,bd_stream,bd_attacker;
__attribute__((used)) static uint32_t bd_ecx;
/* Patched callees (ids 0..4, 6..8) and the actors' virtual +4Ch (id 5). */
static const uint32_t bd_addresses[BD_IDS]={0x411e80,0x423918,0x49288c,0x492934,0x47e0b0,0,0x411c58,0x4238a0,0x423f30};
static const unsigned bd_counts[BD_IDS]={3,2,2,2,2,2,2,1,2};
static uint32_t bd_next(void){
    uint32_t z=bd_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *bd_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int bd_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static uint32_t bd_actor(unsigned k){return BD_BASE+BD_ACTORS+0x200u*k;}
static uint32_t bd_group(unsigned k){return BD_BASE+BD_GROUPS+0x7au*k;}
static uint32_t bd_order(unsigned k){return BD_BASE+BD_ORDERS+0x16u*k;}
static uint32_t bd_pick(const uint32_t *values,unsigned count){return values[random32()%count];}
/* Predicate results: zero, one, a nonzero high word over AX = 0, or anything. */
static uint32_t bd_flag(uint32_t pick){
    switch(pick%7){case 0:case 1:return 0;case 2:case 3:return 1;case 4:return 0x10000u|(bd_next()&0xffff0000u);
        case 5:return (bd_next()&0xffff0000u)|1u;default:return bd_next();}
}
/* +1A2 values that never make the IDIV divisor zero (FFFFh is excluded). */
static uint16_t bd_attackers(uint32_t pick){static const uint16_t v[]={0,0,1,1,2,3,7,0x7fff,0x8000,0xfffe};return v[pick%10];}
static uint16_t bd_variable(uint32_t pick){
    uint32_t k=pick%20;return (uint16_t)(k<6?0xffffu:k<7?(0x8000u|bd_next()):k<16?bd_next()%16u:bd_next()%1000u);
}
static uint16_t bd_action(uint32_t pick){
    static const uint16_t v[]={0,1,2,3,11,12,13,14,15,16,17,1,2,13,16,0xffff};return pick%17<16?v[pick%17]:(uint16_t)bd_next();
}
/* Keeps the IDIV defined after a stub moved data: the attacker's current
 * target counts at least one attacker. */
static void bd_guard(void){
    uint32_t t=get32(bd_host(bd_attacker+0x1a4));
    if(t-BD_BASE<BD_ACTORS+0x200u*BD_NACTORS&&(t-BD_BASE)%0x200u==0&&(uint16_t)(get16(bd_host(t+0x1a2))-1u)>=0xfffeu)put16(bd_host(t+0x1a2),1);
}
__attribute__((used,noinline)) static uint64_t __cdecl bd_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(bd_calls==BD_MAX){fail("building synthetic call overflow");ExitProcess(1);}
    bd_record *v=bd_log+bd_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,4*bd_counts[id]);v->slots=weapons_bridge_slots(a,bd_counts[id]);
    if(!bd_real)memcpy(v->regs,regs,12);
    for(unsigned j=0;j<3;++j)if(bd_stack(v->regs[j]))v->regs[j]=0;
    for(unsigned j=0;j<bd_counts[id];++j)if(bd_stack(v->args[j]))v->args[j]=0;
    uint32_t eax=bd_next(),edx=bd_next(),pick=bd_next();bd_ecx=bd_next();
    switch(id){
    case 0: case 6: /* target_valid, target_known: may change the candidate's flags and groups */
        eax=bd_flag(pick>>3);
        if(bd_real)break;
        /* 411f26 re-reads the candidate's +99 after target_known. */
        if(id==6&&pick%4==0)bd_host(a[1])[0x99]=(BYTE)(bd_next()%2?1:0x80);
        if(pick%5==0){BYTE *c=bd_host(bd_actor(bd_next()%BD_NACTORS));static const unsigned f[]={0x99,0xa3,0xa4,0xa5,0xb7};
            c[f[bd_next()%5]]=(BYTE)(bd_next()%2?0:1);}
        if(pick%7==0)put16(bd_host(bd_actor(bd_next()%BD_NACTORS)+0x1a8),(uint16_t)(bd_next()%4));
        if(pick%11==0)put16(mapped+0xa9afe,(uint16_t)(bd_next()%(BD_NLIST+1u)));
        if(pick%13==0)put32(bd_host(bd_actor(bd_next()%BD_NACTORS)+0x45),bd_group(bd_next()%BD_NGROUPS));
        if(pick%17==0)put16(mapped+0xa9e54,(uint16_t)(bd_next()%2?3:bd_next()%4));
        break;
    case 1: /* order_target: may retarget the group's current order */
        eax=bd_flag(pick>>3);
        if(bd_real)break;
        if(pick%5==0)put32(bd_host(bd_group(bd_next()%BD_NGROUPS)+0x6c),bd_next()%10);
        if(pick%7==0)put16(bd_host(bd_order(bd_next()%BD_NORDERS)),(uint16_t)(bd_next()%3?0:bd_next()%4));
        if(pick%11==0)put32(bd_host(bd_group(bd_next()%BD_NGROUPS)+0x44+4*(bd_next()%10)),bd_next()%3?bd_order(bd_next()%BD_NORDERS):0);
        break;
    case 2: { /* dist_gate: around the limits 100000/1000000, 60000, 4096, or negative */
        static const uint32_t edges[]={100000,1000000,60000,4096,0};
        switch(pick%8){case 0:case 1:case 2:eax=edges[(pick>>3)%5]+(uint32_t)((int)(bd_next()%3)-1);break;
            case 3:case 4:eax=bd_next()%120000u;break;case 5:eax=0u-bd_next()%20000u;break;case 6:eax=bd_next()%1200000u;break;default:break;}
        if(!bd_real&&pick%13==0){put32(bd_host(bd_actor(bd_next()%BD_NACTORS)+0x1a4),bd_next()%2?bd_attacker:bd_actor(bd_next()%BD_NACTORS));bd_guard();}
        break; }
    case 3: { /* heading_point: the attacker's heading, +8000h, nearby or anything */
        uint16_t h=get16(bd_host(a[1]-0x26+0x10));
        switch(pick%6){case 0:eax=(bd_next()&0xffff0000u)|(uint16_t)(h+0x8000u);break;case 1:eax=(bd_next()&0xffff0000u)|h;break;
            case 2:case 3:eax=(bd_next()&0xffff0000u)|(uint16_t)(h+bd_next()%0x2000u-0x1000u);break;default:break;}
        break; }
    case 4: /* multiply_q10: scores as words, may change the candidate's links */
        switch(pick%5){case 0:case 1:eax=(bd_next()&0xffff0000u)|(bd_next()%0x1400u);break;case 2:eax=bd_next()%0x1000u;break;
            case 3:eax=0u-bd_next()%0x1000u;break;default:break;}
        if(bd_real)break;
        if(pick%7==0)put32(bd_host(bd_actor(bd_next()%BD_NACTORS)+0x1a4),bd_next()%3?bd_attacker:bd_actor(bd_next()%BD_NACTORS));
        if(pick%11==0)put32(bd_host(bd_actor(bd_next()%BD_NACTORS)+0x4d),bd_next()%3?BD_BASE+BD_RECORDS+16*(bd_next()%BD_NRECORDS):0);
        if(pick%13==0)put16(bd_host(bd_actor(bd_next()%BD_NACTORS)+0x1a2),bd_attackers(bd_next()));
        if(pick%17==0)bd_host(BD_BASE+BD_RECORDS+16*(bd_next()%BD_NRECORDS))[9]=(BYTE)(bd_next()%2);
        bd_guard();
        break;
    case 5: /* the attacker's virtual +4Ch: the class index k, usually 0..2 */
        eax=(bd_next()&0xffff0000u)|(pick%9<7?pick%3:pick%9==7?(uint16_t)(0u-bd_next()%8u):bd_next()%300u);
        break;
    case 7: /* 4238a0: the current order's target, often the candidate */
        eax=pick%3?get32(bd_host(BD_BASE+BD_COCKPIT+0x100)):bd_actor(bd_next()%BD_NACTORS);
        if(!bd_real&&pick%7==0)bd_host(bd_attacker+0x9a)[0]=(BYTE)(bd_next()%2);
        break;
    case 8: /* group_death: may change the actor's own table */
        if(bd_real)break;
        if(pick%5==0){uint32_t c=a[1];unsigned k=bd_next()%10;put16(bd_host(c+0x1ba+2*k),bd_variable(bd_next()));put16(bd_host(c+0x1ce + 2*k),bd_action(bd_next()));}
        break;
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define BD_STUB(n) __attribute__((naked)) static void bd_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _bd_stub_impl\n\taddl $24,%esp\n\tmovl _bd_ecx,%ecx\n\tret\n\t");}
BD_STUB(0) BD_STUB(1) BD_STUB(2) BD_STUB(3) BD_STUB(4) BD_STUB(5) BD_STUB(6) BD_STUB(7) BD_STUB(8)
#undef BD_STUB
static void *bd_stubs[BD_IDS]={bd_stub_0,bd_stub_1,bd_stub_2,bd_stub_3,bd_stub_4,bd_stub_5,bd_stub_6,bd_stub_7,bd_stub_8};
/* Globals read or written: the factor tables 49933c..49935b, the cockpit
 * 49b088, the actor list, the mode word 4a9e54 and the variables
 * 4a9e70..4aa63f. */
static const struct { uint32_t rva,size; } bd_ranges[]={{0x9933c,0x20},{0x9b088,4},{0xa9af8,8},{0xa9e54,2},{0xa9e70,0x7d0}};
static void bd_globals(BYTE *out,int restore){
    unsigned at=0;
    for(unsigned i=0;i<sizeof(bd_ranges)/sizeof(bd_ranges[0]);++i){
        if(restore)memcpy(mapped+bd_ranges[i].rva,out+at,bd_ranges[i].size);else memcpy(out+at,mapped+bd_ranges[i].rva,bd_ranges[i].size);
        at+=bd_ranges[i].size;
    }
}
static void bd_reset(void){bd_calls=0;bd_stream=bd_seed;}
static void bd_build(unsigned index,uint32_t a[6],uint32_t r[3],int real){
    uint32_t b=BD_BASE;bd_seed=random32();memset(a,0,24);
    for(unsigned i=0;i<BD_SIZE;i+=4)put32(bd_arena+i,random32());
    for(unsigned i=0;i<3;++i)r[i]=random32();
    for(unsigned i=0;i<0x7d0;i+=4)put32(mapped+0xa9e70+i,random32());
    put32(bd_arena+BD_VTABLE+0x4c,(uint32_t)(uintptr_t)bd_stubs[5]);
    for(unsigned k=0;k<BD_NRECORDS;++k)bd_arena[BD_RECORDS+16*k+9]=(BYTE)(random32()%2?0:random32()|1u);
    /* Orders: type (0 matches), scope 0 names a group, 1..3 an actor. */
    for(unsigned k=0;k<BD_NORDERS;++k){BYTE *o=bd_arena+BD_ORDERS+0x16*k;
        static const uint32_t types[]={0,0,3,1,2,0xb,3,0xffff};static const uint32_t scopes[]={0,1,2,3,0xffff,4,1,0};
        put16(o,(uint16_t)(random32()%16?bd_pick(types,8):random32()));
        uint16_t scope=(uint16_t)bd_pick(scopes,8);put16(o+0xc,scope);
        put32(o+0xe,scope==0?bd_group(random32()%BD_NGROUPS):bd_actor(random32()%BD_NACTORS));
    }
    for(unsigned k=0;k<BD_NGROUPS;++k){BYTE *g=bd_arena+BD_GROUPS+0x7a*k;
        static const uint32_t categories[]={0,1,2,0xff,0x80,1,2,0};static const uint32_t counts[]={0,1,2,3,5,8,8,0xffff,0x8000};
        put32(g+0xc,b+BD_MEMBERS+32*k);put16(g+0x10,(uint16_t)(real?1+random32()%8:bd_pick(counts,9)));
        g[0x12]=(BYTE)bd_pick(categories,8);
        for(unsigned j=0;j<10;++j){put16(g+0x1c+2*j,bd_variable(random32()));put16(g+0x30+2*j,bd_action(random32()));}
        for(unsigned j=0;j<10;++j)put32(g+0x44+4*j,random32()%4?bd_order(random32()%BD_NORDERS):0);
        put32(g+0x6c,random32()%10);
        for(unsigned j=0;j<8;++j)put32(bd_arena+BD_MEMBERS+32*k+4*j,bd_actor(random32()%BD_NACTORS));
    }
    for(unsigned k=0;k<BD_NACTORS;++k){BYTE *o=bd_arena+BD_ACTORS+0x200*k;
        int32_t spread=real?60000:200000;
        put32(o,b+BD_VTABLE);put16(o+0x10,(uint16_t)random32());
        put32(o+0x26,(uint32_t)((int32_t)(random32()%(2u*spread+1u))-spread));put32(o+0x2a,(uint32_t)((int32_t)(random32()%(2u*spread+1u))-spread));
        put32(o+0x2e,random32()%2001u-1000u);
        put32(o+0x45,bd_group(random32()%BD_NGROUPS));put16(o+0x4b,(uint16_t)(random32()%128u-64u));
        put32(o+0x4d,random32()%3?0:b+BD_RECORDS+16*(random32()%BD_NRECORDS));
        static const unsigned bytes[]={0x95,0x99,0x9a,0xa3,0xa4,0xa5,0xb4,0xb7};
        for(unsigned j=0;j<8;++j)o[bytes[j]]=(BYTE)(random32()%4?0:bd_pick((const uint32_t[]){1,0x80,0xff,2},4));
        for(unsigned j=0x82;j<0x102;++j)o[j]=(BYTE)(random32()%2?0:random32()|1u);
        put16(o+0x1a2,bd_attackers(random32()));
        put32(o+0x1a4,random32()%3?bd_actor(random32()%BD_NACTORS):random32()%2?0:random32());
        put16(o+0x1a8,(uint16_t)(random32()%8?random32()%4:random32()));
        for(unsigned j=0;j<10;++j){put16(o+0x1ba+2*j,bd_variable(random32()));put16(o+0x1ce + 2*j,bd_action(random32()));}
    }
    for(unsigned k=0;k<BD_NLIST;++k)put32(bd_arena+BD_LIST+4*k,random32()%10?bd_actor(random32()%BD_NACTORS):0);
    put32(mapped+0xa9af8,b+BD_LIST);
    put16(mapped+0xa9afe,(uint16_t)(random32()%16?random32()%(BD_NLIST+1u):random32()%2?0:0x8000u|random32()));
    put32(mapped+0x9b088,b+BD_COCKPIT);put32(bd_arena+BD_COCKPIT+0x210,random32()%3?bd_actor(random32()%BD_NACTORS):0);
    put32(bd_arena+BD_COCKPIT+0x100,bd_actor(random32()%BD_NACTORS));
    put16(mapped+0xa9e54,(uint16_t)(random32()%2?3:random32()%3?random32()%4:random32()));
    /* Arguments. */
    uint32_t actor=bd_actor(random32()%BD_NACTORS);bd_attacker=actor;
    for(unsigned k=0;k<BD_NACTORS;++k)if(random32()%3==0)put32(bd_host(bd_actor(k)+0x1a4),actor);
    switch(index){
    case ES2_HOOK_INDEX_target_select: {
        static const uint32_t arcs[]={0,0,0x3000,0x100,0x7fff,0x8000,0x10000};
        a[0]=actor;a[1]=random32()&~0xffu;
        static const uint32_t bits[]={1,2,4,0x10,0x20};for(unsigned j=0;j<5;++j)if(random32()%2)a[1]|=bits[j];
        if(random32()%8==0)a[1]=random32();
        if(random32()%2)a[1]=(a[1]&~0xffffu)|(random32()%2?0x30u:0x10u);
        a[2]=random32()%8?bd_pick(arcs,7):random32();break; }
    case ES2_HOOK_INDEX_target_valid:
        a[0]=actor;a[1]=bd_actor(random32()%BD_NACTORS);a[2]=random32()%2?random32():random32()%2?0x30u:0x10u;
        /* Mostly a hostile, unflagged candidate, so the later tests are reached. */
        if(random32()%4){BYTE *c=bd_host(a[1]);uint32_t own=get32(bd_host(actor+0x45)),other=get32(bd_host(a[1]+0x45));
            if(other!=own&&bd_host(other)[0x12]==bd_host(own)[0x12])bd_host(other)[0x12]^=1;
            if(other==own){put32(bd_host(a[1]+0x45),bd_group((unsigned)(other-bd_group(0))/0x7au^1u));other=get32(bd_host(a[1]+0x45));
                if(bd_host(other)[0x12]==bd_host(own)[0x12])bd_host(other)[0x12]^=1;}
            c[0x99]=c[0xb4]=c[0xb7]=0;if(random32()%2)put16(c+0x1a8,2);}
        if(random32()%3==0)put32(bd_arena+BD_COCKPIT+0x210,a[1]);
        if(random32()%3==0)put32(bd_arena+BD_COCKPIT+0x100,a[1]);
        break;
    case ES2_HOOK_INDEX_death_actions: a[0]=actor;if(random32()%6==0)put32(bd_host(actor+0x45),0);break;
    case ES2_HOOK_INDEX_group_death: { uint32_t g=bd_group(random32()%BD_NGROUPS);a[0]=g;
        a[1]=random32()%2?get32(bd_host(get32(bd_host(g+0xc))+4*(random32()%8))):actor;
        /* Often every other member destroyed. */
        if(random32()%2)for(unsigned j=0;j<8;++j){uint32_t m=get32(bd_host(get32(bd_host(g+0xc))+4*j));
            if(m!=a[1]&&random32()%8)bd_host(m)[random32()%2?0xa4:0x99]=(BYTE)(random32()%2?1:0x80);}
        break; }
    }
    bd_guard();
}
/* Calls the original with chosen EAX/EDX/ECX. */
__attribute__((naked)) static void bd_machine(void *target,const uint32_t *a,const uint32_t *registers,uint32_t pair[2]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl 12(%ebp),%esi\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\t"
        "call *8(%ebp)\n\tmovl 20(%ebp),%ecx\n\tmovl %eax,(%ecx)\n\tmovl %edx,4(%ecx)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static int bd_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const bd_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,bd_calls);
    for(unsigned j=0;j<calls&&j<bd_calls&&expected;++j)if(memcmp(expected+j,bd_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(bd_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<BD_SIZE;++j)if(bd_after[j]!=bd_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,bd_after[j],bd_arena[j]);break;}
    return 1;
}
static void bd_global_diff(const BYTE *expected,const BYTE *actual){
    for(unsigned j=0;j<BD_GLOBALS;++j)if(expected[j]!=actual[j]){fprintf(stderr," global byte %x %02x/%02x\n",j,expected[j],actual[j]);break;}
}
/* Direct calls count by address, the virtual (id 5) by its slot. */
static unsigned bd_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<bd_calls;++j){unsigned id=bd_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?id==5&&c->key==0x4c:bd_addresses[id]==c->key)++hits;}
    return hits;
}
static int bd_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    static BYTE before[BD_GLOBALS],after[BD_GLOBALS],actual[BD_GLOBALS];static bd_record expected[BD_MAX];
    memcpy(bd_before,bd_arena,BD_SIZE);bd_globals(before,0);
    uint32_t got[2];bd_reset();bd_machine(mapped+d->rva,a,r,got);
    uint64_t pair[2];pair[0]=(uint64_t)got[1]<<32|got[0];unsigned calls=bd_calls;memcpy(expected,bd_log,calls*sizeof(*expected));
    memcpy(bd_after,bd_arena,BD_SIZE);bd_globals(after,0);
    memcpy(bd_arena,bd_before,BD_SIZE);bd_globals(before,1);bd_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    uint32_t entry[3]={r[0],r[1],r[2]};
    pair[1]=x->replacement_context(a,d->user,entry);bd_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=bd_expected_hits(c+i))return fail("building callee counter delta");
    if(pair[0]!=pair[1]||calls!=bd_calls||memcmp(expected,bd_log,calls*sizeof(*expected))||memcmp(bd_after,bd_arena,BD_SIZE)||memcmp(after,actual,BD_GLOBALS)){
        bd_global_diff(after,actual);return bd_report("direct",n,pair,calls,expected);
    }
    return 0;
}
static unsigned bd_which(unsigned index){
    switch(index){
    case ES2_HOOK_INDEX_target_select:return 0x1fu;
    case ES2_HOOK_INDEX_target_valid:return 0xc0u;
    case ES2_HOOK_INDEX_death_actions:return 0x100u;
    default:return 0;
    }
}
static int bd_patch(BYTE saved[BD_IDS][5],int active[BD_IDS],unsigned which,int on){
    for(unsigned i=0;i<BD_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!bd_addresses[i])continue;if(actor_patch(bd_addresses[i]-0x400000u,bd_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(bd_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static int bd_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;bd_build(index,a,r,0);
        static BYTE before[BD_GLOBALS],after[BD_GLOBALS],actual[BD_GLOBALS];BYTE expected_fpu[108],actual_fpu[108];static bd_record expected[BD_MAX];
        memcpy(bd_before,bd_arena,BD_SIZE);bd_globals(before,0);
        bd_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=bd_calls;memcpy(expected,bd_log,calls*sizeof(*expected));memcpy(bd_after,bd_arena,BD_SIZE);bd_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("building poison install");
        memcpy(bd_arena,bd_before,BD_SIZE);bd_globals(before,1);bd_reset();sg_poison_call(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);bd_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=bd_calls||memcmp(expected,bd_log,calls*sizeof(*expected))||memcmp(bd_after,bd_arena,BD_SIZE)||
           memcmp(after,actual,BD_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1){
            bd_global_diff(after,actual);status=bd_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        }
        if(!es2_hook_uninstall(&h))return fail("building poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state\n",d->name,total);
    return status;
}
/* Real callees with the nested hooks in rotating original/replaced/compare
 * modes (compare falls back to replaced where refused) and a patterned
 * stack; only the virtual +4Ch stays a stub. group_death is a leaf. */
static int bd_real_pass(unsigned index){
    static const unsigned sets[3][6]={
        {ES2_HOOK_INDEX_target_valid,ES2_HOOK_INDEX_target_known,ES2_HOOK_INDEX_order_target,ES2_HOOK_INDEX_dist_gate,ES2_HOOK_INDEX_heading_point,ES2_HOOK_INDEX_multiply_q10},
        {ES2_HOOK_INDEX_target_known,ES2_HOOK_INDEX_dist_gate},
        {ES2_HOOK_INDEX_group_death}};
    static const unsigned sizes[3]={6,2,1};
    unsigned set=index-ES2_HOOK_INDEX_target_select,nested_count=sizes[set];const unsigned *nested=sets[set];
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0,virtuals=0;uint64_t entered[6]={0};
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    bd_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<1024&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;bd_build(index,a,r,1);
        static BYTE before[BD_GLOBALS],after[BD_GLOBALS],actual[BD_GLOBALS];static bd_record expected[BD_MAX];
        memcpy(bd_before,bd_arena,BD_SIZE);bd_globals(before,0);
        es2_hook hk[7];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<nested_count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("building nested install");}
        bd_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        unsigned calls=bd_calls;memcpy(expected,bd_log,calls*sizeof(*expected));virtuals+=calls;
        memcpy(bd_after,bd_arena,BD_SIZE);bd_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+nested_count,d,&o,function_hook_extension(index)))return fail("building caller install");
        memcpy(bd_arena,bd_before,BD_SIZE);bd_globals(before,1);bd_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);bd_globals(actual,0);
        if(expected_pair[0]!=actual_pair[0]||expected_pair[1]!=actual_pair[1]||calls!=bd_calls||memcmp(expected,bd_log,calls*sizeof(*expected))||
           memcmp(bd_after,bd_arena,BD_SIZE)||memcmp(after,actual,BD_GLOBALS)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            bd_global_diff(after,actual);fprintf(stderr,"real mode=%u\n",mode);status=bd_report("real",n,pair,calls,expected);
        }
        for(unsigned j=0;j<=nested_count;++j){if(j<nested_count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("building nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("building nested uninstall");}
        bd_globals(before,1);++total;
    }
    bd_real=0;
    for(unsigned j=0;j<nested_count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    for(unsigned i=0;i<count&&!status;++i)if(c[i].count==counts[i]){fprintf(stderr,"FAIL: %s real pass never called %s\n",d->name,c[i].name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, %u virtual calls, 31/a7 stack, full pair, trace and memory\n",d->name,total,nested_count,virtuals);
    return status;
}
static int building_cases(unsigned index,unsigned limit){
    unsigned which=bd_which(index);bd_index=index;
    BYTE saved[BD_IDS][5],old_arena[BD_SIZE];int active[BD_IDS];static BYTE old_globals[BD_GLOBALS];
    bd_globals(old_globals,0);memcpy(old_arena,bd_arena,BD_SIZE);bd_real=0;
    if(bd_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_BUILDING_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];bd_build(index,a,r,0);status=bd_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=bd_installed_probes(index,64);
    if(bd_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status&&index!=ES2_HOOK_INDEX_group_death)status=bd_real_pass(index);
    bd_globals(old_globals,1);memcpy(bd_arena,old_arena,BD_SIZE);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
