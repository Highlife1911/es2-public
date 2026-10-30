/* MECH orders: mech_order 420ad4 (the MECH vehicle virtual +28h) and
 * wingman_order 431610 (docs/reverse-engineering/mech_order.md). Direct cases
 * run the original body and the core against logging stubs for every game
 * callee (ordered argument slots, entry EAX/EDX/ECX, distance points by
 * content); the arena, the globals and the full EDX:EAX are compared, and every
 * order code (each jump-table target and the default) must occur in the
 * instrumented prefix. Then installed 31/a7 poison probes and a real-callee
 * pass with the nested hooks in rotating modes; only the AI helpers
 * 41c0f4/41eb34, the HUD port's virtual 0, the attacker's virtual +4Ch and
 * 435ac8 stay stubs there. */
typedef struct mx_record { uint32_t id,args[3],regs[3],input[6],eax,edx; } mx_record;
enum { MX_SIZE=0x2000, MX_H=0x000, MX_PORT=0x280, MX_PORTVT=0x290, MX_VT=0x2c0, MX_GRP=0x340, MX_ORD=0x540, MX_SCRIPT=0x640,
    MX_REC=0x700, MX_LIST=0x740, MX_TSLIST=0x760, MX_ACT=0x800, MX_ACTORS=8, MX_MAX=64, MX_IDS=14, MX_GLOBALS=94, MX_CODES=19 };
#define MX_BASE ((uint32_t)(uintptr_t)mx_arena)
#define MX_ACTOR(i) (MX_BASE+MX_ACT+0x300u*(uint32_t)(i)+8u)
#define MX_GROUP(i) (MX_BASE+MX_GRP+0x80u*(uint32_t)(i))
static BYTE mx_arena[MX_SIZE],mx_before[MX_SIZE],mx_after[MX_SIZE];
static mx_record mx_log[MX_MAX];
static unsigned mx_calls,mx_pos,mx_seq_count;
static int mx_real;
static uint32_t mx_seed,mx_stream,mx_seq[8];
static uint64_t mx_codes[MX_CODES];
__attribute__((used)) static uint32_t mx_ecx;
/* 0 411cb4, 1 41c0f4, 2 411fa0 target_select, 3 41eb34, 4 471c70, 5 49288c
 * dist_gate, 6 4238a0, 7 4928d0 dist2d_point, 8 413e50, 9 420a98, 10 the
 * wingman's virtual +28h, 11 435ac8, 12 the HUD port's virtual 0 (only in
 * the real pass, behind the real 420a98), 13 the attacker's virtual +4Ch
 * (only in the real pass, from the real target_select). */
static const uint32_t mx_addresses[MX_IDS]={0x411cb4,0x41c0f4,0x411fa0,0x41eb34,0x471c70,0x49288c,0x4238a0,0x4928d0,0x413e50,0x420a98,0,0x435ac8,0,0};
static const unsigned mx_counts[MX_IDS]={1,2,3,1,2,2,1,2,2,3,3,3,2,2},mx_slots[MX_IDS]={0,0,0,0,0,0,0,0,0,0,0x28,0,0x100,0x4c};
/* Globals (VA, size): cockpit, player, attacker list, the 413e50 stagger
 * word, the actor list and its count, the target_valid order word and the
 * wingman slots 4d03c0..4d0407 (slot words -2..15). */
static const uint32_t mx_global_va[8]={0x49b088,0x4a9b84,0x4a9b7a,0x4a9b70,0x4a9af8,0x4a9afe,0x4a9e54,0x4d03c0};
static const unsigned mx_global_size[8]={4,4,4,2,4,2,2,72};
static void mx_globals(BYTE *out,int restore){
    unsigned at=0;
    for(unsigned i=0;i<8;++i){BYTE *g=mapped+mx_global_va[i]-0x400000u;
        if(restore)memcpy(g,out+at,mx_global_size[i]);else memcpy(out+at,g,mx_global_size[i]);at+=mx_global_size[i];}
}
static uint32_t mx_next(void){
    uint32_t z=mx_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *mx_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int mx_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static void mx_vector(uint32_t at,uint32_t *in){for(unsigned j=0;j<3;++j)in[j]=get32(mx_host(at)+4*j);}
static uint32_t mx_actor_pick(uint32_t pick){return MX_ACTOR(pick%MX_ACTORS);}
__attribute__((used,noinline)) static uint64_t __cdecl mx_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    uint32_t eax=mx_next(),edx=mx_next(),pick=mx_next();mx_ecx=mx_next();
    if(mx_calls==MX_MAX){fail("mech order call overflow");ExitProcess(1);}
    mx_record *v=mx_log+mx_calls++;memset(v,0,sizeof(*v));v->id=id;
    for(unsigned k=0;k<mx_counts[id];++k)v->args[k]=mx_stack(a[k])?0:a[k];
    if(!mx_real)for(unsigned j=0;j<3;++j)v->regs[j]=mx_stack(regs[j])?0:regs[j];
    const uint32_t A=MX_ACTOR(0),R=MX_BASE+MX_REC,H=MX_BASE+MX_H;
    switch(id){
    case 0: /* 411cb4: 0, 1, 2 or anything; may retarget R+12h (re-read). */
        eax=pick%4==3?eax:(eax&0xffff0000u)|pick%3u;
        if(pick%16==5)put32(mx_host(R+0x12),pick&0x100?0:mx_actor_pick(pick>>12));
        break;
    case 1: /* 41c0f4: sets A's target like the original. */
        put32(mx_host(a[0]+0x1a4),a[1]);
        break;
    case 2: /* target_select: 0 or an actor; may switch A's group or its order index. */
        eax=pick%3==0?0:mx_actor_pick(pick>>4);
        if(pick%16==7)put32(mx_host(A+0x45),MX_GROUP((pick>>8)%4));
        if(pick%16==9)put32(mx_host(get32(mx_host(A+0x45))+0x6c),(pick>>8)%4);
        break;
    case 4: /* 471c70: the next attacker; may change the player or the actor's flags. */
        eax=mx_pos<mx_seq_count?mx_seq[mx_pos]:0;++mx_pos;
        if(pick%16==3)put32(mapped+0xa9b84,mx_actor_pick(pick>>8));
        if(eax&&pick%16==4)mx_host(eax)[0xa4]^=1;
        if(eax&&pick%16==6)put32(mx_host(eax+0x1a4),get32(mapped+0xa9b84));
        break;
    case 5: { /* dist_gate: around 60000, 100000, 2000 and 25000. */
        mx_vector(a[0],v->input);mx_vector(a[1],v->input+3);
        static const uint32_t d[]={59999,60000,60001,99999,100000,100001,1999,2000,2001,24999,25000,25001,0,0x80000000u,0x7fffffffu,0xffffffffu};
        eax=pick%4<2?d[(pick>>4)%16]:pick%4==2?(pick>>8)%120000u:eax;
        if(pick%32==9)put32(mapped+0xa9b84,mx_actor_pick(pick>>12));
        break; }
    case 6: /* 4238a0: an actor (its position is read next). */
        eax=mx_actor_pick(pick>>4);
        break;
    case 7: { /* dist2d_point: around 1000. */
        mx_vector(a[0],v->input);mx_vector(a[1],v->input+3);
        static const uint32_t d[]={999,1000,1001,0,0xffffffffu,0x80000000u,0x7fffffffu,500};
        eax=pick%4<3?d[(pick>>4)%8]:eax;
        break; }
    case 8: /* 413e50: may rewrite the point R+6..Dh and R+12h (both re-read). */
        if(pick%8==2){put32(mx_host(R+6),mx_next());put32(mx_host(R+10),mx_next());}
        if(pick%8==3)put32(mx_host(R+0x12),pick&0x100?0:mx_actor_pick(pick>>12));
        break;
    case 10: /* the wingman's order: may move the HUD port pointer (re-read). */
        if(pick%8==1)put32(mx_host(H+0x207),mx_next());
        break;
    case 12: /* the HUD port: the message record (code word, vehicle) by content. */
        v->input[0]=get16(mx_host(a[1]));v->input[1]=get32(mx_host(a[1]+2));
        break;
    case 13: /* the attacker's classification of the candidate: kind 0..2. */
        eax=(eax&0xffff0000u)|pick%3u;
        break;
    default:break;
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define MX_STUB(n) __attribute__((naked)) static void mx_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _mx_stub_impl\n\taddl $24,%esp\n\tmovl _mx_ecx,%ecx\n\tret\n\t");}
MX_STUB(0) MX_STUB(1) MX_STUB(2) MX_STUB(3) MX_STUB(4) MX_STUB(5) MX_STUB(6) MX_STUB(7) MX_STUB(8) MX_STUB(9)
MX_STUB(10) MX_STUB(11) MX_STUB(12) MX_STUB(13)
#undef MX_STUB
static void *mx_stubs[MX_IDS]={mx_stub_0,mx_stub_1,mx_stub_2,mx_stub_3,mx_stub_4,mx_stub_5,mx_stub_6,mx_stub_7,mx_stub_8,mx_stub_9,
    mx_stub_10,mx_stub_11,mx_stub_12,mx_stub_13};
/* Calls target with three slots, EAX/EDX/ECX/EBX from regs and a patterned
 * stack below the arguments; returns EDX:EAX in pair. */
__attribute__((naked,noinline)) static void mx_invoke(void *target,const uint32_t *a,const uint32_t regs[4],uint32_t pattern,uint32_t pair[2]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 20(%ebp),%eax\n\tmovl $1024,%ecx\n\trep stosl\n\taddl $4084,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%esi\n\tmovl (%esi),%eax\n\tmovl 4(%esi),%edx\n\tmovl 8(%esi),%ecx\n\tmovl 12(%esi),%ebx\n\tcall *8(%ebp)\n\t"
        "movl 24(%ebp),%ecx\n\tmovl %eax,(%ecx)\n\tmovl %edx,4(%ecx)\n\tleal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static void mx_reset(void){mx_calls=0;mx_pos=0;mx_stream=mx_seed;}
static uint32_t mx_rare(unsigned one_in){return random32()%one_in==0?1u+random32()%0xffu:0u;}
static uint32_t mx_actor_or_zero(void){return random32()%5==0?0:MX_ACTOR(random32()%MX_ACTORS);}
/* One actor: vtable, position, group, script, flags (the disabled bytes
 * rare), the target-select fields kept in the original's defined domain
 * (+4Bh index 0..3Fh, +1A2h attacker count 1..3, +1A8h type 0..3). */
static void mx_actor(unsigned i){
    const uint32_t X=MX_ACTOR(i);BYTE *p=mx_host(X);
    put32(p,MX_BASE+MX_VT);put16(p+0x10,(uint16_t)random32());
    for(unsigned j=0;j<3;++j)put32(p+0x26+4*j,random32()%3?random32()%0x40000u-0x20000u:random32());
    put32(p+0x45,MX_GROUP(random32()%4));put16(p+0x4b,(uint16_t)(random32()%0x40));
    put32(p+0x4d,random32()%8==0&&i?0:MX_BASE+MX_SCRIPT+0x20u*(random32()%4)-8u);
    p[0x95]=(BYTE)(random32()%2);p[0x96]=(BYTE)(random32()%2);p[0x99]=(BYTE)mx_rare(8);p[0x9a]=(BYTE)(random32()%2);
    p[0xa3]=(BYTE)(random32()%2);p[0xa4]=(BYTE)mx_rare(8);p[0xa5]=(BYTE)mx_rare(8);p[0xb4]=(BYTE)mx_rare(4);p[0xb7]=(BYTE)mx_rare(4);
    put16(p+0x1a2,(uint16_t)(1+random32()%3));put32(p+0x1a4,mx_actor_or_zero());put16(p+0x1a8,(uint16_t)(random32()%4));
    static const uint16_t states[]={0,1,2,4,4,6,6,3};
    put16(p+0x23e,random32()%8?states[random32()%8]:(uint16_t)random32());
    put32(p+0x248,mx_actor_or_zero());put32(p+0x24c,random32()%2?0:mx_actor_or_zero());
}
/* The arena: cockpit H (player +203h, port +207h, target +210h), the HUD
 * port and its vtable, the actor vtable (+28h, +4Ch), four groups (leader
 * slot +0Ch, side +12h, order slots +44h, index +6Ch), eight order records,
 * four script records, the order record R, the attacker list object, the
 * actor list and eight actors (A = actor 0, the player usually actor 1). */
static void mx_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    const uint32_t b=MX_BASE,H=b+MX_H,A=MX_ACTOR(0),R=b+MX_REC;
    mx_seed=random32();random_bytes(mx_arena,MX_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    for(unsigned s=0;s<0x80;s+=4)put32(mx_arena+MX_VT+s,0);
    put32(mx_arena+MX_VT+0x28,(uint32_t)(uintptr_t)(mx_real?mapped+0x20ad4:(BYTE *)mx_stubs[10]));
    put32(mx_arena+MX_VT+0x4c,(uint32_t)(uintptr_t)mx_stubs[13]);
    put32(mx_arena+MX_PORT,b+MX_PORTVT);put32(mx_arena+MX_PORTVT,(uint32_t)(uintptr_t)mx_stubs[12]);
    for(unsigned k=0;k<8;++k){
        const uint32_t O=b+MX_ORD+0x20u*k;BYTE *o=mx_host(O);
        static const uint16_t types[]={0,1,2,2,3,4,0xb,5};
        put16(o,random32()%8?types[random32()%8]:(uint16_t)random32());
        /* 4238a0/423918 subtypes: 0 group-relative ([[+0Eh]+0Ch] holds the
         * leader), 1..3 the actor +0Eh; others give 0, which 420ad4 would
         * dereference after a type-2 order, so type 2 keeps 0..3. */
        uint16_t sub=(uint16_t)(get16(o)==2||random32()%4?random32()%4:(uint16_t)(random32()%8)-2u);
        put16(o+0xc,sub);put32(o+0xe,sub==0?MX_GROUP(random32()%4):MX_ACTOR(random32()%MX_ACTORS));
    }
    for(unsigned g=0;g<4;++g){
        const uint32_t G=MX_GROUP(g);BYTE *p=mx_host(G);
        put32(p+0xc,G+0x70);put32(p+0x70,MX_ACTOR(random32()%MX_ACTORS));
        p[0x12]=(BYTE)(random32()%3);put32(p+0x6c,random32()%4);
        for(unsigned s=0;s<4;++s)put32(p+0x44+4*s,random32()%4?b+MX_ORD+0x20u*(random32()%8):0);
    }
    for(unsigned k=0;k<4;++k){BYTE *s=mx_arena+MX_SCRIPT+0x20*k;s[1]=(BYTE)(random32()%3==0);s[2]=(BYTE)(random32()%3==0);s[4]=(BYTE)(random32()%3==0);}
    for(unsigned i=0;i<MX_ACTORS;++i)mx_actor(i);
    /* The vehicle: its own target and order target often the cockpit's or R's. */
    put32(mx_host(H+0x203),random32()%4?MX_ACTOR(1):MX_ACTOR(random32()%MX_ACTORS));
    put32(mx_host(H+0x207),b+MX_PORT);
    put32(mx_host(H+0x210),random32()%5==0?0:MX_ACTOR(random32()%MX_ACTORS));
    put32(mapped+0x9b088,H);put32(mapped+0xa9b84,random32()%4?MX_ACTOR(1):MX_ACTOR(random32()%MX_ACTORS));
    put32(mapped+0xa9b7a,b+MX_LIST);
    /* The order record: code word (every table entry, sometimes above 11h),
     * player +2, point +6, object +12h. */
    uint16_t code=(uint16_t)(random32()%16?random32()%0x12:random32()%3?0x12+random32()%4:random32());
    /* 0Bh dereferences R+12h unchecked (a null object faults in the original). */
    put16(mx_host(R),code);put32(mx_host(R+0x12),code==0xb?MX_ACTOR(random32()%MX_ACTORS):mx_actor_or_zero());
    const uint32_t T=random32()%2?get32(mx_host(H+0x210)):get32(mx_host(R+0x12));
    if(random32()%2)put32(mx_host(A+0x248),T);
    if(random32()%2)put32(mx_host(A+0x1a4),T);
    if(random32()%3==0)put32(mx_host(A+0x24c),get32(mx_host(R+0x12)));
    /* Attackers of the player for 2 (the stub's sequence). */
    mx_seq_count=random32()%7;
    for(unsigned j=0;j<mx_seq_count;++j){mx_seq[j]=MX_ACTOR(1+random32()%(MX_ACTORS-1));
        if(random32()%2)put32(mx_host(mx_seq[j]+0x1a4),get32(mapped+0xa9b84));}
    if(mx_seq_count&&random32()%2)put32(mx_host(A+0x1a4),mx_seq[random32()%mx_seq_count]);
    /* The same attackers as a list for the real 471c70: object +0Eh -> first
     * node, node = actor - 8, [node] = next, [node+4] = previous. */
    uint32_t prev=0;put32(mx_arena+MX_LIST+0xe,mx_seq_count?mx_seq[0]-8u:0);
    for(unsigned j=0;j<mx_seq_count;++j){
        /* A repeated actor would make the real list cyclic: the real pass
         * keeps them distinct (mx_distinct). */
        put32(mx_host(mx_seq[j]-8u),j+1<mx_seq_count?mx_seq[j+1]-8u:0);put32(mx_host(mx_seq[j]-4u),prev);prev=mx_seq[j]-8u;
    }
    /* The actor list for the real target_select: 0..4 actors. */
    put32(mapped+0xa9af8,b+MX_TSLIST);put16(mapped+0xa9afe,(uint16_t)(random32()%5));
    for(unsigned j=0;j<8;++j)put32(mx_arena+MX_TSLIST+4*j,MX_ACTOR(random32()%MX_ACTORS));
    put16(mapped+0xa9e54,(uint16_t)(random32()%2?3:random32()%5));put16(mapped+0xa9b70,(uint16_t)random32());
    /* Wingman slots -2..15: empty, the player, the vehicle or another actor. */
    for(unsigned j=0;j<18;++j){uint32_t w=random32()%4;put32(mapped+0xd03c0+4*j,w==0?0:w==1?get32(mx_host(H+0x203)):w==2?A:mx_actor_pick(random32()));}
    if(index==ES2_HOOK_INDEX_mech_order){
        a[0]=A;a[1]=R;
        static const uint32_t modes[]={0,1,2,0x10000,0xffff0001u};
        a[2]=random32()%6?modes[random32()%5]:random32();
    } else {
        a[0]=H;a[1]=R;a[2]=(random32()&0xffff0000u)|(uint16_t)((int)(random32()%18)-2);
    }
    (void)n;
}
/* The real 471c70 walks the list; it must be acyclic. */
static void mx_distinct(void){
    unsigned used=0;uint32_t prev=0;
    for(unsigned j=0;j<mx_seq_count;++j){unsigned k=(mx_seq[j]-MX_ACTOR(0))/0x300u;
        while(used>>k&1)k=1+k%(MX_ACTORS-1);used|=1u<<k;mx_seq[j]=MX_ACTOR(k);}
    put32(mx_arena+MX_LIST+0xe,mx_seq_count?mx_seq[0]-8u:0);
    for(unsigned j=0;j<mx_seq_count;++j){
        put32(mx_host(mx_seq[j]-8u),j+1<mx_seq_count?mx_seq[j+1]-8u:0);put32(mx_host(mx_seq[j]-4u),prev);prev=mx_seq[j]-8u;
    }
}
static int mx_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const mx_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) code %04x pair %016llx/%016llx calls %u/%u\n",current_function,n,what,get16(mx_arena+MX_REC),
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,mx_calls);
    for(unsigned j=0;j<calls&&j<mx_calls&&expected;++j)if(memcmp(expected+j,mx_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(mx_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<MX_SIZE;++j)if(mx_after[j]!=mx_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,mx_after[j],mx_arena[j]);break;}
    for(unsigned j=0;j<calls&&expected&&j<24;++j)fprintf(stderr," trace %u id%u args %08x %08x %08x eax %08x\n",j,expected[j].id,
        expected[j].args[0],expected[j].args[1],expected[j].args[2],expected[j].eax);
    return 1;
}
static unsigned mx_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<mx_calls;++j){unsigned id=mx_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?mx_slots[id]==c->key:!mx_slots[id]&&mx_addresses[id]==c->key)++hits;}
    return hits;
}
static int mx_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[MX_GLOBALS],after[MX_GLOBALS],actual[MX_GLOBALS];static mx_record expected[MX_MAX];
    memcpy(mx_before,mx_arena,MX_SIZE);mx_globals(before,0);
    mx_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=mx_calls;memcpy(expected,mx_log,calls*sizeof(*expected));
    memcpy(mx_after,mx_arena,MX_SIZE);mx_globals(after,0);
    memcpy(mx_arena,mx_before,MX_SIZE);mx_globals(before,1);mx_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);mx_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=mx_expected_hits(c+i))return fail("mech order callee counter delta");
    if(pair[0]!=pair[1]||calls!=mx_calls||memcmp(expected,mx_log,calls*sizeof(*expected))||memcmp(mx_after,mx_arena,MX_SIZE)||
       memcmp(after,actual,sizeof(after)))
        return mx_report("direct",n,pair,calls,expected);
    return 0;
}
static int mx_patch(BYTE saved[MX_IDS][5],int active[MX_IDS],uint32_t which,int on){
    for(unsigned i=0;i<MX_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!mx_addresses[i])continue;if(actor_patch(mx_addresses[i]-0x400000u,mx_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(mx_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
/* Direct-call stubs per hook (bit i = callee id i); the wingman's virtual
 * is reached through the actor vtable. */
static uint32_t mx_direct_stubs(unsigned index){
    return index==ES2_HOOK_INDEX_mech_order?0x3ffu:1u<<11;
}
static int mx_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],regs[4],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;mx_build(index,n,a,r);
        regs[0]=regs[1]=regs[2]=regs[3]=pattern;
        BYTE before[MX_GLOBALS],after[MX_GLOBALS],actual[MX_GLOBALS],expected_fpu[108],actual_fpu[108];static mx_record expected[MX_MAX];
        memcpy(mx_before,mx_arena,MX_SIZE);mx_globals(before,0);
        mx_reset();mx_invoke(mapped+d->rva,a,regs,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=mx_calls;memcpy(expected,mx_log,calls*sizeof(*expected));memcpy(mx_after,mx_arena,MX_SIZE);mx_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("mech order poison install");
        memcpy(mx_arena,mx_before,MX_SIZE);mx_globals(before,1);mx_reset();mx_invoke(mapped+d->rva,a,regs,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);mx_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=mx_calls||memcmp(expected,mx_log,calls*sizeof(*expected))||memcmp(mx_after,mx_arena,MX_SIZE)||
           memcmp(after,actual,sizeof(after))||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=mx_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("mech order poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state\n",d->name,total);
    return status;
}
/* Real callees with the nested hooks in rotating original/replaced/compare
 * modes (compare falls back to replaced where refused) and a patterned
 * stack. Stubs: 41c0f4, 41eb34 (large AI subtrees), the HUD port's
 * virtual 0 behind the real 420a98, the attacker's virtual +4Ch and 435ac8;
 * their inputs are compared in order (entry registers only in the direct
 * pass). wingman_order reaches the real 420ad4 through the actor vtable. */
static int mx_real_pass(unsigned index){
    static const unsigned order_nested[]={ES2_HOOK_INDEX_dist_gate,ES2_HOOK_INDEX_dist2d_point,ES2_HOOK_INDEX_target_select,
        ES2_HOOK_INDEX_target_valid,ES2_HOOK_INDEX_target_known,ES2_HOOK_INDEX_order_target,ES2_HOOK_INDEX_heading_point,
        ES2_HOOK_INDEX_multiply_q10};
    static const unsigned wingman_nested[]={ES2_HOOK_INDEX_mech_order,ES2_HOOK_INDEX_dist_gate,ES2_HOOK_INDEX_dist2d_point,
        ES2_HOOK_INDEX_target_select,ES2_HOOK_INDEX_target_valid,ES2_HOOK_INDEX_target_known,ES2_HOOK_INDEX_order_target,
        ES2_HOOK_INDEX_heading_point,ES2_HOOK_INDEX_multiply_q10};
    const unsigned *nested=index==ES2_HOOK_INDEX_mech_order?order_nested:wingman_nested;
    const unsigned count=index==ES2_HOOK_INDEX_mech_order?8u:9u;
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[10]={0};
    BYTE saved[MX_IDS][5];int active[MX_IDS];if(mx_patch(saved,active,(1u<<1)|(1u<<3)|(1u<<11),1))return 1;
    static mx_record expected[MX_MAX];
    mx_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<1024&&!status;++n){
        uint32_t a[6],r[3],regs[4],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;mx_build(index,n,a,r);mx_distinct();
        /* Positions within reach so that the real distances vary around the limits. */
        static const uint32_t spans[]={0x1000,0x10000,0x30000};const uint32_t span=spans[n%3];
        for(unsigned i=0;i<MX_ACTORS;++i)for(unsigned j=0;j<3;++j)put32(mx_host(MX_ACTOR(i))+0x26+4*j,random32()%span-span/2);
        for(unsigned j=0;j<2;++j)put32(mx_arena+MX_REC+6+4*j,random32()%span-span/2);
        /* The ordered vehicle's script record is dereferenced unchecked (a
         * MECH always has one); wingman_order may order any actor. */
        if(index==ES2_HOOK_INDEX_wingman_order)for(unsigned i=0;i<MX_ACTORS;++i)
            if(!get32(mx_host(MX_ACTOR(i))+0x4d))put32(mx_host(MX_ACTOR(i))+0x4d,MX_BASE+MX_SCRIPT-8u);
        regs[0]=regs[1]=regs[2]=regs[3]=pattern;
        BYTE before[MX_GLOBALS],after[MX_GLOBALS],actual[MX_GLOBALS];
        memcpy(mx_before,mx_arena,MX_SIZE);mx_globals(before,0);
        es2_hook hk[10];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("mech order nested install");}
        mx_reset();mx_invoke(mapped+d->rva,a,regs,pattern,expected_pair);
        unsigned calls=mx_calls;memcpy(expected,mx_log,calls*sizeof(*expected));
        memcpy(mx_after,mx_arena,MX_SIZE);mx_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+count,d,&o,function_hook_extension(index)))return fail("mech order caller install");
        memcpy(mx_arena,mx_before,MX_SIZE);mx_globals(before,1);mx_reset();
        mx_invoke(mapped+d->rva,a,regs,pattern,actual_pair);mx_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=mx_calls||memcmp(expected,mx_log,calls*sizeof(*expected))||
           memcmp(mx_after,mx_arena,MX_SIZE)||memcmp(after,actual,sizeof(after))){
            fprintf(stderr,"real mode=%u\n",mode);status=mx_report("real",n,pair,calls,expected);
        }
        for(unsigned j=0;j<=count;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("mech order nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("mech order nested uninstall");}
        mx_globals(before,1);++total;
    }
    mx_real=0;
    if(mx_patch(saved,active,0,0))return 1;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){
        fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, 31/a7 stack, pair, memory, globals and stub inputs\n",
        d->name,total,count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
static int mech_order_cases(unsigned index,unsigned limit){
    BYTE saved[MX_IDS][5],old_globals[MX_GLOBALS];int active[MX_IDS];
    mx_globals(old_globals,0);if(mx_patch(saved,active,mx_direct_stubs(index),1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_MECH_ORDER_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    memset(mx_codes,0,sizeof(mx_codes));
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];mx_build(index,n,a,r);
        if(n<16384){uint16_t code=get16(mx_arena+MX_REC);++mx_codes[code<0x12?code:0x12];}
        status=mx_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    /* Each jump-table entry 0..11h and the default edge in the instrumented prefix. */
    if(!limit&&!status&&index==ES2_HOOK_INDEX_mech_order)for(unsigned c=0;c<MX_CODES&&!status;++c){
        if(!mx_codes[c]){fprintf(stderr,"FAIL: mech_order order code %s%x never run\n",c==0x12?">":"",c);status=1;}}
    if(!limit&&!status&&index==ES2_HOOK_INDEX_mech_order){printf("JUMPTABLE mech_order 420afe codes");
        for(unsigned c=0;c<MX_CODES;++c)printf(" %x:%llu",c,(unsigned long long)mx_codes[c]);printf(" (12 = above 11h)\n");}
    if(!limit&&!status)status=mx_installed_probes(index,64);
    if(mx_patch(saved,active,0,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=mx_real_pass(index);
    mx_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, points by content, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
