/* AI behaviours: aim_select 41ea7c, attack_plan 41cf18, attack_replan 41d5ec,
 * group_scan 41d60c, order_scan 41d7d0 and retreat_fire 41d2c4
 * (docs/reverse-engineering/ai.md, "Aim selection, attack planning, group
 * scans and retreat"). Direct cases run
 * the original body and the core against logging stubs for the game callees of
 * the hook under test (ordered argument slots, entry EAX/EDX/ECX, frame records
 * by content, stack addresses masked); the actor virtuals +30h, +3Ch, +4Ch,
 * +54h and +58h go through the arena vtable. Pair, arena and the RNG/stagger
 * globals are compared. Then installed 31/a7 poison probes; for aim_select and
 * retreat_fire (entry shims, calls at the original stack coordinate) the stubs
 * also record EBX, ESI, EDI and EBP/ESP relative to the caller, and the 64
 * bytes below the caller's argument slots are compared after return. Then a
 * real-callee pass per hook with the nested hooks in rotating modes
 * (aim_select's on the arena and logging stubs of ai_behaviour_cases.h, so the
 * real aim_target and aim_fire run under it). */
typedef struct b2_record { uint32_t id,args[6],regs[3],input[6],eax,edx,saved[5],slots; } b2_record;
enum { B2_SIZE=0x2000, B2_ACT=0x100, B2_STRIDE=0x340, B2_ACTORS=6, B2_GRP=0x1500, B2_MEM=0x1600, B2_ORD=0x1680,
    B2_TBL=0x1740, B2_CFG=0x1800, B2_VT=0x1c00, B2_MAX=64, B2_IDS=24, B2_DIRECT=18,
    B2_GLOBALS=0x72+2 };
#define B2_BASE ((uint32_t)(uintptr_t)b2_arena)
#define B2_ACTOR(i) (B2_BASE+B2_ACT+B2_STRIDE*(uint32_t)(i))
#define B2_GROUP(i) (B2_BASE+B2_GRP+0x80u*(uint32_t)(i))
static BYTE b2_arena[B2_SIZE],b2_before[B2_SIZE],b2_after[B2_SIZE];
static b2_record b2_log[B2_MAX];
static unsigned b2_calls;
static int b2_real,b2_retreat;
static uint32_t b2_seed,b2_stream;
__attribute__((used)) static uint32_t b2_ecx;
/* EBX, ESI, EDI, EBP, ESP at the last stub entry; b2_invoke's ESP before
 * its call; the 64 bytes below the argument slots after its call. */
__attribute__((used)) static uint32_t b2_entry[5],b2_origin;
__attribute__((used)) static BYTE b2_frame_copy[64];
static int b2_track;
/* 0 411fa0 target_select, 1 41eb34 order_resume, 2 41d5ec attack_replan,
 * 3 41cb94 retreat_check, 4 41ce08 part_pick, 5 413e50 script_start,
 * 6 41d598 group_move, 7 467a48 timer_word_step, 8 423918 order_target,
 * 9 41e758 target_bearing, 10 467aa8 timer_dword_step, 11 492ee0
 * rng_next, 12 40f09c (sound trigger), 13 41e828 drive_apply, 14 41ea7c
 * aim_select, 15 41e984 aim_target, 16 41f5a0 aim_fire, 17 41cf18
 * attack_plan; 18..22 the virtuals +4Ch, +54h, +58h, +30h, +3Ch (arena
 * vtable, never patched); 23 416a04 vehicle_control (real retreat_fire pass). */
static const uint32_t b2_addresses[B2_IDS]={0x411fa0,0x41eb34,0x41d5ec,0x41cb94,0x41ce08,0x413e50,0x41d598,0x467a48,
    0x423918,0x41e758,0x467aa8,0x492ee0,0x40f09c,0x41e828,0x41ea7c,0x41e984,0x41f5a0,0x41cf18,0,0,0,0,0,0x416a04};
static const unsigned b2_slots[B2_IDS]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x4c,0x54,0x58,0x30,0x3c,0};
static const unsigned b2_counts[B2_IDS]={3,1,1,1,1,2,1,1,2,3,1,1,1,2,3,3,4,1,2,2,3,3,1,4};
static const struct { uint32_t rva,size; } b2_ranges[]={{0xd2599,0x72},{0xa9b70,2}};
static void b2_globals(BYTE *out,int restore){
    unsigned at=0;
    for(unsigned i=0;i<sizeof(b2_ranges)/sizeof(b2_ranges[0]);++i){BYTE *g=mapped+b2_ranges[i].rva;
        if(restore)memcpy(g,out+at,b2_ranges[i].size);else memcpy(out+at,g,b2_ranges[i].size);at+=b2_ranges[i].size;}
}
static uint32_t b2_next(void){
    uint32_t z=b2_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *b2_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int b2_in(uint32_t a,uint32_t n){return a-B2_BASE<B2_SIZE&&B2_SIZE-(a-B2_BASE)>=n;}
/* Frame addresses (the committed stack window) are masked. */
static int b2_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static int b2_ok(uint32_t p,uint32_t n){
    const NT_TIB *t=(const NT_TIB *)NtCurrentTeb();uint32_t low=(uint32_t)(uintptr_t)t->StackLimit,top=(uint32_t)(uintptr_t)t->StackBase;
    return b2_in(p,n)||(p>=low&&p<top&&top-p>=n);
}
static void b2_dwords(b2_record *v,unsigned at,uint32_t p,unsigned n){
    if(b2_ok(p,4u*n))for(unsigned j=0;j<n;++j)v->input[at+j]=get32(b2_host(p)+4*j);
}
static uint32_t b2_actor_pick(uint32_t pick){return B2_ACTOR(pick%B2_ACTORS);}
__attribute__((used,noinline)) static uint64_t __cdecl b2_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(b2_calls==B2_MAX){fail("ai behaviour2 call overflow");ExitProcess(1);}
    b2_record *v=b2_log+b2_calls++;memset(v,0,sizeof(*v));v->id=id;v->slots=weapons_bridge_slots(a,b2_counts[id]);
    if(b2_track){for(unsigned j=0;j<3;++j)v->saved[j]=b2_entry[j];v->saved[3]=b2_entry[3]-b2_origin;v->saved[4]=b2_entry[4]-b2_origin;}
    for(unsigned k=0;k<b2_counts[id];++k)v->args[k]=b2_stack(a[k])?0:a[k];
    for(unsigned j=0;j<3;++j)v->regs[j]=b2_stack(regs[j])?0:regs[j];
    uint32_t eax=b2_next(),edx=b2_next(),pick=b2_next();b2_ecx=b2_next();
    BYTE *o=b2_host(B2_ACTOR(0));
    /* Perturbations of the target word that the originals reread (direct
     * and installed cases only: the real passes keep the game's order). */
    const int perturb=!b2_real;
    switch(id){
    case 0: /* target_select: 0, the current target or another actor. */
        eax=pick%3==0?0:pick%3==1?get32(o+0x1a4):b2_actor_pick(pick>>4);
        if(perturb&&pick%11==0)put32(o+0x1a4,(pick>>8)%3==0?0:(pick>>8)%3==1?eax:b2_actor_pick(pick>>12));
        break;
    case 3: /* retreat_check: AX zero or not for the whole case (the second
             * call of attack_plan repeats the first, PROVED). */
        if(b2_retreat){if(!(uint16_t)eax)eax|=1u;}else eax&=0xffff0000u;
        break;
    case 6: /* group_move: may change the order state word (order_scan read
             * it before) and the target. */
        if(perturb&&pick%5==0)put16(o+0x23e,(uint16_t)((pick>>4)%4));
        if(perturb&&pick%7==0)put32(o+0x1a4,pick>>8&1?0:b2_actor_pick(pick>>12));
        break;
    case 7: /* timer_word_step: AX zero half of the time; the target may be
             * set behind the scan's back (it is reread). */
        if(pick%2)eax&=0xffff0000u;else if(!(uint16_t)eax)eax|=1u;
        if(perturb&&pick%9==0)put32(o+0x1a4,b2_actor_pick(pick>>4));
        break;
    case 8: /* order_target: AX zero a third of the time. */
        if(pick%3==0)eax&=0xffff0000u;else if(!(uint16_t)eax)eax|=1u;
        break;
    case 9: { /* target_bearing: fills the 18h-byte record; the magnitude
               * word +4 on the 3000h edge. */
        static const uint16_t mags[]={0x2fff,0x3000,0x3001,0,0x7fff,0x8000,0xffff,0x1000};
        if(b2_ok(a[1],0x18)){for(unsigned j=0;j<6;++j)put32(b2_host(a[1])+4*j,b2_next());
            put16(b2_host(a[1])+4,pick%4?mags[(pick>>4)%8]:(uint16_t)b2_next());}
        break; }
    case 10: /* timer_dword_step: EAX zero half of the time. */
        if(pick%2)eax=0;else if(!eax)eax=1;
        break;
    case 13: /* drive_apply: the steering record by content. */
        b2_dwords(v,0,a[1],6);
        break;
    case 14: /* aim_select: the remembered target may change (reread). */
        if(perturb&&pick%7==0)put32(o+0x61,b2_actor_pick(pick>>4));
        break;
    case 16: /* aim_fire: the aim point by content. */
        b2_dwords(v,0,a[1],3);
        break;
    case 18: /* virtual +4Ch: AX 0, 1 or 2 (PROVED domain). */
        eax=(eax&0xffff0000u)|pick%3;
        break;
    case 19: /* virtual +54h: a part index. */
        eax=(eax&0xffff0000u)|(uint16_t)(pick%4?pick>>4&0x1f:b2_next());
        break;
    case 20: /* virtual +58h: writes the part position (12 bytes). */
        if(b2_ok(a[2],12))for(unsigned j=0;j<3;++j)put32(b2_host(a[2])+4*j,b2_next());
        break;
    case 21: /* virtual +30h: writes both 6-byte records. */
        for(unsigned k=1;k<3;++k)if(b2_ok(a[k],6))for(unsigned j=0;j<3;++j)put16(b2_host(a[k])+2*j,(uint16_t)b2_next());
        break;
    default:break;
    }
    v->eax=b2_stack(eax)?0:eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define B2_STUB(n) __attribute__((naked)) static void b2_stub_##n(void){__asm__ volatile( \
    "movl %ebx,_b2_entry\n\tmovl %esi,_b2_entry+4\n\tmovl %edi,_b2_entry+8\n\tmovl %ebp,_b2_entry+12\n\tmovl %esp,_b2_entry+16\n\t" \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _b2_stub_impl\n\taddl $24,%esp\n\tmovl _b2_ecx,%ecx\n\tret\n\t");}
B2_STUB(0) B2_STUB(1) B2_STUB(2) B2_STUB(3) B2_STUB(4) B2_STUB(5) B2_STUB(6) B2_STUB(7) B2_STUB(8) B2_STUB(9)
B2_STUB(10) B2_STUB(11) B2_STUB(12) B2_STUB(13) B2_STUB(14) B2_STUB(15) B2_STUB(16) B2_STUB(17) B2_STUB(18) B2_STUB(19)
B2_STUB(20) B2_STUB(21) B2_STUB(22) B2_STUB(23)
#undef B2_STUB
static void *b2_stubs[B2_IDS]={b2_stub_0,b2_stub_1,b2_stub_2,b2_stub_3,b2_stub_4,b2_stub_5,b2_stub_6,b2_stub_7,b2_stub_8,
    b2_stub_9,b2_stub_10,b2_stub_11,b2_stub_12,b2_stub_13,b2_stub_14,b2_stub_15,b2_stub_16,b2_stub_17,b2_stub_18,b2_stub_19,
    b2_stub_20,b2_stub_21,b2_stub_22,b2_stub_23};
#define B2_BIT(i) (1u<<(i))
/* Direct callees of each hook (bit i = b2_addresses[i]). */
static uint32_t b2_which(unsigned index){
    if(index==ES2_HOOK_INDEX_aim_select)return B2_BIT(15)|B2_BIT(16);
    if(index==ES2_HOOK_INDEX_attack_plan)return B2_BIT(0)|B2_BIT(1)|B2_BIT(2)|B2_BIT(3)|B2_BIT(4)|B2_BIT(5);
    if(index==ES2_HOOK_INDEX_attack_replan)return B2_BIT(17);
    if(index==ES2_HOOK_INDEX_group_scan)return B2_BIT(6)|B2_BIT(7)|B2_BIT(0)|B2_BIT(5)|B2_BIT(8)|B2_BIT(2);
    if(index==ES2_HOOK_INDEX_order_scan)return B2_BIT(6)|B2_BIT(7)|B2_BIT(0)|B2_BIT(5)|B2_BIT(2);
    if(index==ES2_HOOK_INDEX_retreat_fire)return B2_BIT(9)|B2_BIT(10)|B2_BIT(11)|B2_BIT(12)|B2_BIT(13)|B2_BIT(14);
    return 0;
}
static int b2_patch(BYTE saved[B2_IDS][5],int active[B2_IDS],uint32_t which,int on){
    for(unsigned i=0;i<B2_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!b2_addresses[i])continue;if(actor_patch(b2_addresses[i]-0x400000u,b2_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(b2_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
static void b2_reset(void){b2_calls=0;b2_stream=b2_seed;}
static uint16_t b2_word(const uint16_t *values,unsigned count){return random32()%4?values[random32()%count]:(uint16_t)random32();}
static BYTE b2_flag(void){return (BYTE)(random32()%4?0:random32()%2?1:random32());}
/* The arena: six actors, two groups with member lists, eight orders, four
 * script tables and configurations, the vtable. real: consistent groups
 * (each actor in the list of its own group, leader first), every actor
 * with a target where it can reach the type word. */
static void b2_build(unsigned index,uint32_t a[6],uint32_t r[3],int real){
    const uint32_t b=B2_BASE,A=B2_ACTOR(0);
    b2_seed=random32();random_bytes(b2_arena,B2_SIZE);memset(a,0,24);b2_real=real;b2_retreat=random32()%4==0;
    for(unsigned i=0;i<3;++i)r[i]=random32();
    static const unsigned slots[]={0x4c,0x54,0x58,0x30,0x3c};
    for(unsigned k=0;k<5;++k)put32(b2_arena+B2_VT+slots[k],(uint32_t)(uintptr_t)b2_stubs[18+k]);
    unsigned group_of[B2_ACTORS];
    for(unsigned i=0;i<B2_ACTORS;++i){
        BYTE *p=b2_host(B2_ACTOR(i));group_of[i]=random32()%2;
        put32(p,b+B2_VT);put32(p+0x45,B2_GROUP(group_of[i]));
        put32(p+0x4d,b+B2_TBL+0x20u*(random32()%4));
        static const unsigned bytes[]={0x4,0x7,0xb,0x11,0x12,0x13,0x16,0x17,0x1a,0x20,0x24};
        for(unsigned k=0;k<11;++k)p[0x92+bytes[k]]=b2_flag();
        if(random32()%3==0)p[0x92+0x1a]=1;
        static const uint16_t counts[]={0,1,2,3,0xffff};put16(p+0x1a2,b2_word(counts,5));
        put32(p+0x1a4,random32()%4?B2_ACTOR(random32()%B2_ACTORS):0);
        static const uint16_t kinds[]={0,1,2,3,0,1};put16(p+0x1a8,b2_word(kinds,6));
        put32(p+0x1f2,b+B2_CFG+0x100u*(random32()%4));
        static const uint16_t states[]={0,1,2,3,4,5,0,2};put16(p+0x23e,b2_word(states,8));
        put32(p+0x248,random32()%3==0?0:B2_ACTOR(random32()%B2_ACTORS));
        static const uint16_t flags16[]={0,1};put16(p+0x26b,b2_word(flags16,2));
        put32(p+0x61,B2_ACTOR(random32()%B2_ACTORS));
        put32(p+0x317,random32()%2?0:b+B2_TBL);
        static const uint16_t sides[]={0,1};put16(p+0x5f,b2_word(sides,2));
    }
    /* Groups: member list (+0Ch), count word (+10h), byte +12h, order index
     * +6Ch and four order slots +44h. */
    for(unsigned g=0;g<2;++g){BYTE *p=b2_host(B2_GROUP(g)),*list=b2_arena+B2_MEM+0x40*g;
        put32(p+0xc,b+B2_MEM+0x40u*g);p[0x12]=b2_flag();put32(p+0x6c,random32()%4);
        for(unsigned s=0;s<4;++s)put32(p+0x44+4*s,random32()%4?b+B2_ORD+0x18u*(random32()%8):0);
        if(real){unsigned n=0;for(unsigned i=0;i<B2_ACTORS;++i)if(group_of[i]==g)put32(list+4*n++,B2_ACTOR(i));
            for(unsigned i=n;i>1;--i){unsigned j=1+random32()%(i-1);uint32_t x=get32(list+4*(i-1));put32(list+4*(i-1),get32(list+4*j));put32(list+4*j,x);}
            if(n>1&&random32()%2){uint32_t x=get32(list);put32(list,get32(list+4));put32(list+4,x);}
            put16(p+0x10,(uint16_t)n);}
        else{static const uint16_t sizes[]={0,1,2,3,5,7,8,0x8000,0xffff};put16(p+0x10,sizes[random32()%9]);
            for(unsigned k=0;k<8;++k)put32(list+4*k,random32()%5==0?0:B2_ACTOR(random32()%B2_ACTORS));
            /* The leader slot is never empty (41cf18 dereferences it). */
            put32(list,random32()%2?B2_ACTOR(0):B2_ACTOR(random32()%B2_ACTORS));}
    }
    static const uint16_t types[]={0,1,2,3,4,5,6,0xb};
    for(unsigned k=0;k<8;++k){BYTE *o=b2_arena+B2_ORD+0x18*k;put16(o,b2_word(types,8));
        static const uint16_t scopes[]={0xffff,0,1,2,3,4};put16(o+0xc,b2_word(scopes,6));
        put32(o+0xe,random32()%2?B2_GROUP(random32()%2):B2_ACTOR(random32()%B2_ACTORS));}
    for(unsigned k=0;k<4;++k)b2_arena[B2_TBL+0x20*k+9]=b2_flag();
    static const uint16_t cfgs[]={0xb8,0xb9,0xba,0,0x8000,0x7fff};
    for(unsigned k=0;k<4;++k)put16(b2_arena+B2_CFG+0x100*k+0xc8,b2_word(cfgs,6));
    if(real)for(unsigned i=0;i<B2_ACTORS;++i){BYTE *p=b2_host(B2_ACTOR(i));
        /* Every actor that can reach its target's type word has a target. */
        if(!get32(p+0x1a4))put32(p+0x1a4,B2_ACTOR((i+1)%B2_ACTORS));
        for(unsigned j=0;j<3;++j)put32(p+0x26+4*j,(uint32_t)((int32_t)(random32()%40001u)-20000));
        put16(p+0x1a2,(uint16_t)(random32()%3));}
    a[0]=A;
    /* attack_plan reads the type word of A's target without a test when
     * byte A+0ACh sends it straight to the group walk; aim_select reads
     * the target's type word. */
    if(!get32(b2_host(A)+0x1a4)&&(b2_host(A)[0xac]||index==ES2_HOOK_INDEX_aim_select))
        put32(b2_host(A)+0x1a4,B2_ACTOR(1+random32()%(B2_ACTORS-1)));
    if(index==ES2_HOOK_INDEX_aim_select){a[1]=random32();a[2]=random32()%2?0:B2_ACTOR(random32()%B2_ACTORS);}
    if(index==ES2_HOOK_INDEX_retreat_fire&&random32()%3==0)put32(b2_host(A)+0x1a4,0);
    if(index==ES2_HOOK_INDEX_attack_plan||index==ES2_HOOK_INDEX_attack_replan){
        if(random32()%2)put16(b2_host(A)+0x1a2,1);}
}
static int b2_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const b2_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,b2_calls);
    for(unsigned j=0;j<calls&&j<b2_calls&&expected;++j)if(memcmp(expected+j,b2_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(b2_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<B2_SIZE;++j)if(b2_after[j]!=b2_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,b2_after[j],b2_arena[j]);break;}
    return 1;
}
static unsigned b2_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<b2_calls;++j){unsigned id=b2_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?b2_slots[id]==c->key:!b2_slots[id]&&b2_addresses[id]==c->key)++hits;}
    return hits;
}
static int b2_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[B2_GLOBALS],after[B2_GLOBALS],actual[B2_GLOBALS];static b2_record expected[B2_MAX];
    memcpy(b2_before,b2_arena,B2_SIZE);b2_globals(before,0);
    b2_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=b2_calls;memcpy(expected,b2_log,calls*sizeof(*expected));
    memcpy(b2_after,b2_arena,B2_SIZE);b2_globals(after,0);
    memcpy(b2_arena,b2_before,B2_SIZE);b2_globals(before,1);b2_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];
    if(count>16)return fail("ai behaviour2 counter table too long");
    for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);b2_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=b2_expected_hits(c+i))return fail("ai behaviour2 callee counter delta");
    if(pair[0]!=pair[1]||calls!=b2_calls||memcmp(expected,b2_log,calls*sizeof(*expected))||memcmp(b2_after,b2_arena,B2_SIZE)||
       memcmp(after,actual,B2_GLOBALS))
        return b2_report("direct",n,pair,calls,expected);
    b2_globals(before,1);
    return 0;
}
/* Calls target with four slots, EAX/EDX/ECX/EBX = pattern, ESI and EDI
 * two other values derived from it (so a callee-saved register handed on
 * in the wrong place shows) and a patterned stack below the arguments;
 * returns EAX, EDX and ECX in out and copies the 64 bytes below the slots
 * after the call. */
__attribute__((naked,noinline)) static void b2_invoke(void *target,const uint32_t *a,uint32_t pattern,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 16(%ebp),%eax\n\tmovl $1024,%ecx\n\trep stosl\n\taddl $4080,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl %esp,_b2_origin\n\t"
        "movl 16(%ebp),%eax\n\tmovl %eax,%edx\n\tmovl %eax,%ecx\n\tmovl %eax,%ebx\n\tmovl %eax,%esi\n\txorl $0x5a5a5a5a,%esi\n\t"
        "movl %eax,%edi\n\txorl $0x3c3c3c3c,%edi\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%ebx\n\tmovl %eax,(%ebx)\n\tmovl %edx,4(%ebx)\n\tmovl %ecx,8(%ebx)\n\t"
        "leal -64(%esp),%esi\n\tmovl $_b2_frame_copy,%edi\n\tmovl $16,%ecx\n\tcld\n\trep movsl\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static int b2_shimmed(unsigned index){return index==ES2_HOOK_INDEX_aim_select||index==ES2_HOOK_INDEX_retreat_fire;}
static int b2_frame_report(const char *what,unsigned n,const BYTE expected[64]){
    fprintf(stderr,"FAIL: %s case %u (%s)\n",current_function,n,what);
    for(unsigned j=0;j<64;++j)if(expected[j]!=b2_frame_copy[j]){fprintf(stderr," below the arguments -%u: %02x/%02x\n",64-j,expected[j],b2_frame_copy[j]);break;}
    return 1;
}
static int b2_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;b2_track=b2_shimmed(index);
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_out[3],actual_out[3],pattern=poison?0xa7a7a7a7u:0x31313131u;b2_build(index,a,r,0);BYTE expected_frame[64];
        BYTE before[B2_GLOBALS],after[B2_GLOBALS],actual[B2_GLOBALS],expected_fpu[108],actual_fpu[108];static b2_record expected[B2_MAX];
        memcpy(b2_before,b2_arena,B2_SIZE);b2_globals(before,0);
        b2_reset();b2_invoke(mapped+d->rva,a,pattern,expected_out);es2_hook_fpu_save(expected_fpu);memcpy(expected_frame,b2_frame_copy,64);
        unsigned calls=b2_calls;memcpy(expected,b2_log,calls*sizeof(*expected));memcpy(b2_after,b2_arena,B2_SIZE);b2_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("ai behaviour2 poison install");
        memcpy(b2_arena,b2_before,B2_SIZE);b2_globals(before,1);b2_reset();b2_invoke(mapped+d->rva,a,pattern,actual_out);
        es2_hook_fpu_save(actual_fpu);b2_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_out[1]<<32|expected_out[0],(uint64_t)actual_out[1]<<32|actual_out[0]};
        if(pair[0]!=pair[1]||calls!=b2_calls||memcmp(expected,b2_log,calls*sizeof(*expected))||memcmp(b2_after,b2_arena,B2_SIZE)||
           memcmp(after,actual,B2_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=b2_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        else if(b2_track&&memcmp(expected_frame,b2_frame_copy,64))status=b2_frame_report(fast?"installed fast frame":"installed preserving frame",n,expected_frame);
        if(!es2_hook_uninstall(&h))return fail("ai behaviour2 poison uninstall");b2_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));b2_track=0;
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state%s\n",d->name,total,
        b2_shimmed(index)?", EBX/ESI/EDI/EBP/ESP at every callee entry and the frame below the arguments":"");
    return status;
}
/* The real-callee pass: nested hooks in rotating original/replaced/compare
 * modes (compare falls back to replaced where refused), the hook under test
 * original or replaced; keep = the callees that stay logging stubs. */
static unsigned b2_nested(unsigned index,unsigned nested[8],uint32_t *keep){
    unsigned count=0;*keep=0;
#define B2_N(name) nested[count++]=ES2_HOOK_INDEX_##name
    if(index==ES2_HOOK_INDEX_attack_plan){B2_N(script_start);B2_N(attack_replan);*keep=B2_BIT(0)|B2_BIT(1)|B2_BIT(3)|B2_BIT(4);}
    else if(index==ES2_HOOK_INDEX_attack_replan){B2_N(attack_plan);B2_N(script_start);*keep=B2_BIT(0)|B2_BIT(1)|B2_BIT(3)|B2_BIT(4);}
    else if(index==ES2_HOOK_INDEX_group_scan){B2_N(timer_word_step);B2_N(script_start);B2_N(order_target);
        *keep=B2_BIT(0)|B2_BIT(2)|B2_BIT(6);}
    else if(index==ES2_HOOK_INDEX_order_scan){B2_N(timer_word_step);B2_N(script_start);*keep=B2_BIT(0)|B2_BIT(2)|B2_BIT(6);}
    else if(index==ES2_HOOK_INDEX_retreat_fire){B2_N(target_bearing);B2_N(timer_dword_step);B2_N(rng_next);B2_N(drive_apply);
        B2_N(aim_select);B2_N(heading_point);B2_N(dist_gate);*keep=B2_BIT(12)|B2_BIT(15)|B2_BIT(16)|B2_BIT(23);}
#undef B2_N
    return count;
}
static int b2_real_pass(unsigned index){
    unsigned nested[8];uint32_t keep;unsigned count=b2_nested(index,nested,&keep);
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[8]={0};
    BYTE saved[B2_IDS][5];int active[B2_IDS];
    if(b2_patch(saved,active,keep,1))return 1;
    static b2_record expected[B2_MAX];
    b2_track=b2_shimmed(index);
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_out[3],actual_out[3],pattern=n&1?0xa7a7a7a7u:0x31313131u;b2_build(index,a,r,1);BYTE expected_frame[64];
        BYTE before[B2_GLOBALS],after[B2_GLOBALS],actual[B2_GLOBALS];
        memcpy(b2_before,b2_arena,B2_SIZE);b2_globals(before,0);
        es2_hook hk[9];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("ai behaviour2 nested install");}
        b2_reset();b2_invoke(mapped+d->rva,a,pattern,expected_out);memcpy(expected_frame,b2_frame_copy,64);
        unsigned calls=b2_calls;memcpy(expected,b2_log,calls*sizeof(*expected));
        memcpy(b2_after,b2_arena,B2_SIZE);b2_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+8,d,&o,function_hook_extension(index)))return fail("ai behaviour2 caller install");
        memcpy(b2_arena,b2_before,B2_SIZE);b2_globals(before,1);b2_reset();
        b2_invoke(mapped+d->rva,a,pattern,actual_out);b2_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_out[1]<<32|expected_out[0],(uint64_t)actual_out[1]<<32|actual_out[0]};
        if(pair[0]!=pair[1]||calls!=b2_calls||memcmp(expected,b2_log,calls*sizeof(*expected))||memcmp(b2_after,b2_arena,B2_SIZE)||
           memcmp(after,actual,B2_GLOBALS)){
            fprintf(stderr,"real mode=%u\n",mode);status=b2_report("real",n,pair,calls,expected);
        }
        else if(b2_track&&memcmp(expected_frame,b2_frame_copy,64)){fprintf(stderr,"real mode=%u\n",mode);status=b2_frame_report("real frame",n,expected_frame);}
        for(unsigned j=0;j<9;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("ai behaviour2 nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("ai behaviour2 nested uninstall");}
        b2_globals(before,1);++total;
    }
    if(b2_patch(saved,active,0,0))return 1;
    b2_real=0;b2_track=0;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, 31/a7 stack, pair, memory, globals and stub inputs%s\n",
        d->name,total,count,b2_shimmed(index)?", per-call EBX/ESI/EDI/EBP/ESP and the frame below the arguments":"");
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
/* aim_select's real pass runs on ai_behaviour_cases.h's arena and logging
 * stubs (its aim_target and aim_fire generator): aim_target and aim_fire
 * are nested hooks in rotating modes, their callees stay ab stubs, the
 * target's +58h is the ab part-position stub and +54h/+30h are the stubs
 * below (logged in ab_log). EBX/ESI/EDI/EBP/ESP at every stub entry and
 * the frame below the arguments are compared (both shims nest here). */
__attribute__((used,noinline)) static uint64_t __cdecl b2_ab_virtual_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(ab_calls==AB_MAX){fail("ai behaviour2 ab call overflow");ExitProcess(1);}
    ab_record *v=ab_log+ab_calls++;memset(v,0,sizeof(*v));v->id=id;
    if(ab_track){for(unsigned j=0;j<3;++j)v->saved[j]=ab_entry[j];v->saved[3]=ab_entry[3]-ab_origin;v->saved[4]=ab_entry[4]-ab_origin;}
    const unsigned n=id==41?2:3;v->slots=weapons_bridge_slots(a,n);
    for(unsigned k=0;k<n;++k)v->args[k]=ab_stack(a[k])?0:a[k];
    for(unsigned j=0;j<3;++j)v->regs[j]=ab_stack(regs[j])?0:regs[j];
    uint32_t eax=ab_next(),edx=ab_next(),pick=ab_next();ab_ecx=ab_next();
    if(id==41)eax=(eax&0xffff0000u)|(pick>>4&7);
    else for(unsigned k=1;k<3;++k)if(ab_ok(a[k],6))for(unsigned j=0;j<3;++j)put16(ab_host(a[k])+2*j,(uint16_t)ab_small(ab_next(),2000));
    v->eax=ab_stack(eax)?0:eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define B2_AB_STUB(n) __attribute__((naked)) static void b2_ab_stub_##n(void){__asm__ volatile( \
    "movl %ebx,_ab_entry\n\tmovl %esi,_ab_entry+4\n\tmovl %edi,_ab_entry+8\n\tmovl %ebp,_ab_entry+12\n\tmovl %esp,_ab_entry+16\n\t" \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _b2_ab_virtual_impl\n\taddl $24,%esp\n\tmovl _ab_ecx,%ecx\n\tret\n\t");}
B2_AB_STUB(41) B2_AB_STUB(42)
#undef B2_AB_STUB
static int b2_aim_select_real(void){
    const unsigned index=ES2_HOOK_INDEX_aim_select;const es2_hook_descriptor *d=function_hook_descriptor(index);
    const unsigned nested[2]={ES2_HOOK_INDEX_aim_target,ES2_HOOK_INDEX_aim_fire};
    int status=0;unsigned total=0;uint64_t entered[2]={0};
    BYTE saved[AB_DIRECT][5];int active[AB_DIRECT];
    if(ab_patch(saved,active,(ab_which(ES2_HOOK_INDEX_aim_target)|ab_which(ES2_HOOK_INDEX_aim_fire))&~AB_BIT(24),1))return 1;
    static ab_record expected[AB_MAX];ab_track=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_out[3],actual_out[3],pattern=n&1?0xa7a7a7a7u:0x31313131u;ab_build(ES2_HOOK_INDEX_aim_target,n,a,r,1);
        put32(ab_arena+AB_VT+0x54,(uint32_t)(uintptr_t)b2_ab_stub_41);put32(ab_arena+AB_VT+0x30,(uint32_t)(uintptr_t)b2_ab_stub_42);
        a[1]=random32();a[2]=random32()%2?0:AB_ACTOR(random32()%AB_ACTORS);
        BYTE before[AB_GLOBALS],after[AB_GLOBALS],actual[AB_GLOBALS],expected_frame[64];
        memcpy(ab_before,ab_arena,AB_SIZE);ab_globals(before,0);
        es2_hook hk[3];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<2;++j){o.mode=(es2_hook_mode)((mode+j)%3);if(o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,function_hook_extension(nested[j])))return fail("ai behaviour2 aim nested install");}
        ab_reset();ab_invoke(mapped+d->rva,a,pattern,expected_out);memcpy(expected_frame,ab_frame_copy,64);
        unsigned calls=ab_calls;memcpy(expected,ab_log,calls*sizeof(*expected));
        memcpy(ab_after,ab_arena,AB_SIZE);ab_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+2,d,&o,function_hook_extension(index)))return fail("ai behaviour2 aim caller install");
        memcpy(ab_arena,ab_before,AB_SIZE);ab_globals(before,1);ab_reset();
        ab_invoke(mapped+d->rva,a,pattern,actual_out);ab_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_out[1]<<32|expected_out[0],(uint64_t)actual_out[1]<<32|actual_out[0]};
        if(pair[0]!=pair[1]||calls!=ab_calls||memcmp(expected,ab_log,calls*sizeof(*expected))||memcmp(ab_after,ab_arena,AB_SIZE)||
           memcmp(after,actual,AB_GLOBALS)){fprintf(stderr,"real mode=%u\n",mode);status=ab_report("aim_select real",n,pair,calls,expected);}
        else if(memcmp(expected_frame,ab_frame_copy,64)){fprintf(stderr,"real mode=%u\n",mode);status=ab_frame_report("aim_select real frame",n,expected_frame);}
        for(unsigned j=0;j<3;++j){if(j<2)entered[j]+=hk[j].calls;if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("ai behaviour2 aim nested uninstall");}
        ab_globals(before,1);++total;
    }
    if(ab_patch(saved,active,0,0))return 1;
    ab_real=0;ab_track=0;
    for(unsigned j=0;j<2&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: aim_select real callee %s never entered\n",function_hook_descriptor(nested[j])->name);status=1;}
    if(!status){printf("PASS: aim_select %u real-callee cases on the ai_behaviour arena, aim_target and aim_fire nested in rotating modes, 31/a7 stack, per-call EBX/ESI/EDI/EBP/ESP and the frame below the arguments\n",total);
        for(unsigned j=0;j<2;++j)printf("REAL aim_select nested %s entered %llu\n",function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
static int ai_behaviour2_cases(unsigned index,unsigned limit){
    uint32_t which=b2_which(index);
    BYTE saved[B2_IDS][5],old_globals[B2_GLOBALS];int active[B2_IDS];
    b2_globals(old_globals,0);if(b2_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_AI_BEHAVIOUR2_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];b2_build(index,a,r,0);status=b2_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=b2_installed_probes(index,64);
    if(b2_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=index==ES2_HOOK_INDEX_aim_select?b2_aim_select_real():b2_real_pass(index);
    b2_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, frame records by content, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
