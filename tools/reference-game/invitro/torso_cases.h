/* Torso group: torso_twist 41a550, torso_pitch 41a808, weapon_converge 41a74c
 * and weapon_keys 410f78 (input.md, "torso servos and weapon keys"). Direct
 * cases run the original body and the core against logging stubs for every game
 * callee of the hook under test (direct calls patched in the private image, the
 * weapon virtual +38h through an arena vtable): ordered arguments, entry
 * EAX/EDX/ECX, payloads by content; values in the worker's stack reservation
 * are masked, and the frame addresses that 41a74c passes to aim_angles are
 * recorded relative to its out pointer. Stubs return edge values and change
 * what the originals reread after a call (the rate and angle words, the player
 * flag P+A3h, the configuration pointer, the weapon count and list). The
 * originals are called with a chosen ESI (410f78 returns its high word). Then
 * installed 31/a7 poison probes (through weapon_keys' entry shim) and
 * real-callee passes with the nested hooks in rotating modes: multiply_q8,
 * movement_step and approach_word under both servos, weapon_converge and
 * aim_angles under torso_pitch, aim_angles, vector_angle and dist2d_delta under
 * weapon_converge. Only track_seek, the sound latches and 40f540 stay stubs
 * there; their inputs are compared. The 41074c stub also records the ESI
 * it receives (41074c reads the high word), and weapon_keys gets a real-callee
 * pass over the real 41074c (select_step original or replaced) and 410708 that
 * compares the full pair, EDX included. */
typedef struct tv_record { uint32_t id,args[5],regs[3],input[6],eax,edx; } tv_record;
enum { TV_SIZE=0x800, TV_P=0x40, TV_CFG=0x340, TV_W=0x3c0, TV_LIST=0x400, TV_S=0x480, TV_VTABLE=0x5c0, TV_V=0x640,
    TV_DEF=0x100, TV_TABLES=0x180, TV_MAX=64, TV_IDS=14, TV_REAL_MAX=4096, TV_SLOTS=16 };
#define TV_BASE ((uint32_t)(uintptr_t)tv_arena)
static BYTE tv_arena[TV_SIZE],tv_before[TV_SIZE],tv_after[TV_SIZE];
static tv_record tv_log[TV_MAX];
static uint32_t tv_real_log[TV_REAL_MAX];
static unsigned tv_calls,tv_real_count,tv_index;
static int tv_real;
static uint32_t tv_seed,tv_stream,tv_target;
__attribute__((used)) static uint32_t tv_ecx,tv_esi;
/* 0 47e0a0, 1 467924, 2 467adc, 3 479344, 4 41a6d0, 5 41a994, 6 41a74c,
 * 7 40f540, 8 492990, 9 410d04, 10 410ae4, 11 41074c, 12 4110ac; 13 the
 * weapon virtual +38h. */
static const uint32_t tv_addresses[TV_IDS]={0x47e0a0,0x467924,0x467adc,0x479344,0x41a6d0,0x41a994,0x41a74c,0x40f540,0x492990,
    0x410d04,0x410ae4,0x41074c,0x4110ac,0};
static const uint32_t tv_slots[TV_IDS]={0,0,0,0,0,0,0,0,0,0,0,0,0,0x38};
static const unsigned tv_counts[TV_IDS]={2,1,3,3,2,2,2,1,3,1,2,2,2,2};
static uint32_t tv_next(void){
    uint32_t z=tv_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *tv_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int tv_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static int tv_pitch(void){return tv_index==ES2_HOOK_INDEX_torso_pitch;}
/* The servo fields of the hook under test. */
static uint32_t tv_rate_off(void){return tv_pitch()?0x296u:0x294u;}
static uint32_t tv_angle_off(void){return tv_pitch()?0x29au:0x298u;}
static uint32_t tv_cfg(void){return get32(tv_arena+TV_P+0x1f2)+2u;}
static int tv_in_arena(uint32_t a,uint32_t size){return a-TV_BASE<=TV_SIZE-size;}
static uint16_t tv_bound(int upper){
    uint32_t c=tv_cfg();if(!tv_in_arena(c+0x20,4)||!tv_in_arena(c+0x28,4))return 0;
    if(tv_pitch())return get16(tv_host(c+(upper?0x28u:0x2au)));
    uint16_t w=get16(tv_host(c+0x20));return upper?w:(uint16_t)(0u-w);
}
/* The word a step must have to land on x from the current angle. */
static uint16_t tv_land(uint16_t x){return (uint16_t)(x-get16(tv_arena+TV_P+tv_angle_off()));}
static void tv_perturb_servo(uint32_t pick){
    BYTE *p=tv_arena+TV_P;
    switch(pick%32){
    case 1:put16(p+0x294,(uint16_t)tv_next());put16(p+0x296,(uint16_t)tv_next());break;
    case 2:put16(p+0x298,(uint16_t)tv_next());put16(p+0x29a,(uint16_t)tv_next());break;
    case 3:p[0xa3]^=1;break;
    case 4:put32(p+0x230,tv_next()%2?0:tv_next()|1u);put32(p+0x234,tv_next()%2?0:tv_next()|1u);break;
    default:break;
    }
}
/* 40f540's result: three words in the V area (sometimes overlapping). */
static uint32_t tv_mount(uint32_t pick){
    uint32_t v=TV_BASE+TV_V+2u*(pick%4);
    for(unsigned j=0;j<3;++j)put16(tv_host(v+2*j),pick&64?(uint16_t)(tv_next()%0x400u-0x200u):(uint16_t)tv_next());
    return v;
}
static uint64_t tv_real_stub(unsigned id,const uint32_t *a,uint32_t eax,uint32_t edx,uint32_t pick){
    if(tv_real_count+8>TV_REAL_MAX){fail("torso real stub overflow");ExitProcess(1);}
    uint32_t *o=tv_real_log+tv_real_count;o[0]=id;unsigned k=1;
    for(unsigned j=0;j<tv_counts[id];++j)o[k++]=tv_stack(a[j])?0:a[j];
    switch(id){
    case 3:case 4:case 5:tv_perturb_servo(pick);break;
    case 7:eax=tv_mount(pick|64);break;
    case 9:case 10:case 12:case 13:break;   /* weapon_keys' stubbed callees */
    default:fail("torso real stub id");ExitProcess(1);
    }
    tv_real_count+=k;return (uint64_t)edx<<32|eax;
}
__attribute__((used,noinline)) static uint64_t __cdecl tv_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    uint32_t eax=tv_next(),edx=tv_next(),pick=tv_next();tv_ecx=tv_next();
    if(tv_real)return tv_real_stub(id,a,eax,edx,pick);
    if(tv_calls==TV_MAX){fail("torso synthetic call overflow");ExitProcess(1);}
    tv_record *v=tv_log+tv_calls++;memset(v,0,sizeof(*v));v->id=id;
    if(id==8){
        /* aim_angles(out, end, start) on the caller's frame: offsets from
         * out, both points by content; all six out bytes are written. */
        v->args[1]=a[1]-a[0];v->args[2]=a[2]-a[0];
        for(unsigned j=0;j<3;++j){v->regs[j]=regs[j]-a[0];v->input[j]=get32(tv_host(a[1])+4*j);v->input[3+j]=get32(tv_host(a[2])+4*j);}
        for(unsigned j=0;j<6;j+=2)put16(tv_host(a[0])+j,(uint16_t)tv_next());
    } else {
        for(unsigned k=0;k<tv_counts[id];++k)v->args[k]=tv_stack(a[k])?0:a[k];
        for(unsigned j=0;j<3;++j)v->regs[j]=tv_stack(regs[j])?0:regs[j];
        /* 41074c: the whole ESI (caller's high word, SI = 1 from 410f83). */
        if(id==11)v->input[0]=tv_esi;
    }
    switch(id){
    case 0: { /* multiply_q8: 8000h, |q| = |rate| ties, small or any */
        uint16_t rate=get16(tv_arena+TV_P+tv_rate_off());
        static const uint16_t edges[]={0x8000,0x7fff,0x8001,0,1,0xffff};
        switch(pick%8){
        case 0:eax=(eax&0xffff0000u)|edges[pick/8%6];break;
        case 1:eax=(eax&0xffff0000u)|rate;break;
        case 2:eax=(eax&0xffff0000u)|(uint16_t)(0u-rate);break;
        case 3:eax=(eax&0xffff0000u)|(uint16_t)(rate+(pick&256?1u:0xffffu));break;
        case 4:case 5:eax=(eax&0xffff0000u)|(uint16_t)(tv_next()%0x800u-0x400u);break;
        default:break;
        }
        if(pick%64==7)put16(tv_arena+TV_P+tv_rate_off(),(uint16_t)tv_next());
        if(pick%64==9)put16(tv_arena+TV_P+tv_angle_off(),(uint16_t)tv_next());
        break; }
    case 1: /* movement_step: steps that land on the bounds or the target */
        switch(pick%10){
        case 0:eax=(eax&0xffff0000u)|tv_land(tv_bound(1));break;
        case 1:eax=(eax&0xffff0000u)|tv_land((uint16_t)(tv_bound(1)+1u));break;
        case 2:eax=(eax&0xffff0000u)|tv_land(tv_bound(0));break;
        case 3:eax=(eax&0xffff0000u)|tv_land((uint16_t)(tv_bound(0)-1u));break;
        case 4:eax=(eax&0xffff0000u)|tv_land((uint16_t)tv_target);break;
        case 5:eax=(eax&0xffff0000u)|tv_land((uint16_t)(tv_target+(pick&512?1u:0xffffu)));break;
        case 6:case 7:eax=(eax&0xffff0000u)|(uint16_t)(tv_next()%0x200u-0x100u);break;
        default:break;
        }
        if(pick%64==11)tv_perturb_servo(pick>>6);
        break;
    case 2: /* approach_word moves the rate word */
        v->input[0]=get16(tv_host(a[0]));
        if(pick%4)put16(tv_host(a[0]),pick%4==1?(uint16_t)a[1]:(uint16_t)tv_next());
        break;
    case 3:case 4:case 5:case 6:tv_perturb_servo(pick);break;
    case 7: /* 40f540: mount words; the caller's cached C, count, list are reread */
        eax=tv_mount(pick);
        if(pick%16==1)put32(tv_arena+TV_P+0x1f2,TV_BASE+TV_CFG+0x10u);
        if(pick%16==2)put16(tv_arena+TV_W+8,(uint16_t)(tv_next()%(TV_SLOTS+1u)));
        if(pick%16==3)put32(tv_arena+TV_LIST+4u*(tv_next()%TV_SLOTS),tv_next()%2?0:TV_BASE+TV_S+0x50u*(tv_next()%4));
        if(pick%16==4)put16(tv_arena+TV_CFG+0x10+0x66,(uint16_t)tv_next());
        break;
    default:break;
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define TV_STUB(n) __attribute__((naked)) static void tv_stub_##n(void){__asm__ volatile( \
    "movl %esi,_tv_esi\n\tpushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _tv_stub_impl\n\taddl $24,%esp\n\tmovl _tv_ecx,%ecx\n\tret\n\t");}
TV_STUB(0) TV_STUB(1) TV_STUB(2) TV_STUB(3) TV_STUB(4) TV_STUB(5) TV_STUB(6) TV_STUB(7) TV_STUB(8) TV_STUB(9)
TV_STUB(10) TV_STUB(11) TV_STUB(12) TV_STUB(13)
#undef TV_STUB
static void *tv_stubs[TV_IDS]={tv_stub_0,tv_stub_1,tv_stub_2,tv_stub_3,tv_stub_4,tv_stub_5,tv_stub_6,tv_stub_7,tv_stub_8,
    tv_stub_9,tv_stub_10,tv_stub_11,tv_stub_12,tv_stub_13};
static void tv_reset(void){tv_calls=tv_real_count=0;tv_stream=tv_seed;}
static uint16_t tv_word(const uint16_t *edges,unsigned count){
    return random32()%4?edges[random32()%count]:(uint16_t)random32();
}
/* Weapon system W: list [W+4] of TV_SLOTS entries (null or one of four
 * weapons with the arena vtable), count word W+8, selection s8 W+1Dh,
 * fire mode byte W+1Ch. */
static void tv_weapons(int dense){
    uint32_t b=TV_BASE;BYTE *w=tv_arena+TV_W;
    put32(w+4,b+TV_LIST);
    for(unsigned i=0;i<TV_SLOTS;++i)put32(tv_arena+TV_LIST+4*i,!dense&&random32()%4==0?0:b+TV_S+0x50u*(random32()%4));
    for(unsigned k=0;k<4;++k)put32(tv_arena+TV_S+0x50*k,b+TV_VTABLE);
    static const uint16_t counts[]={0,1,2,3,4,16,0xffff,0x8000};
    put16(w+8,random32()%2?counts[random32()%8]:(uint16_t)(random32()%(TV_SLOTS+1u)));
    static const BYTE selections[]={0,1,2,15,0xff,0x80,0xfe,0x7f};
    BYTE sel=selections[random32()%8];if((int8_t)sel>=0)sel=(BYTE)(sel%TV_SLOTS);
    w[0x1d]=random32()%3?sel:(BYTE)(random32()%TV_SLOTS);
    static const BYTE modes[]={0,1,2,3,0x7f,0x80,0xff,0xfe};
    w[0x1c]=random32()%4?modes[random32()%8]:(BYTE)random32();
}
static void tv_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],uint32_t *esi){
    uint32_t b=TV_BASE;tv_seed=random32();random_bytes(tv_arena,TV_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    *esi=random32();
    put32(tv_arena+TV_VTABLE+0x38,(uint32_t)(uintptr_t)tv_stubs[13]);
    BYTE *p=tv_arena+TV_P,*c=tv_arena+TV_CFG+2;
    put32(p+0x1f2,b+TV_CFG);put32(p+0x202,b+TV_W);
    tv_weapons(index==ES2_HOOK_INDEX_weapon_keys);
    if(index==ES2_HOOK_INDEX_weapon_keys){
        static const uint16_t codes[]={0x0c,0x0d,0x4a,0x4e,0x11,0x211,0x2c,0x30,0x202,0x203,0x207,0x20b,0x20c,0x201,0,0x0b,0x0e,
            0x10,0x12,0x2b,0x2d,0x2f,0x31,0x49,0x4b,0x4d,0x4f,0x210,0x212,0x1b4,0x8000,0xffff};
        uint32_t code=random32()%8?codes[random32()%32]:random32()%2?(uint16_t)random32():(uint16_t)(random32()%0x300u);
        a[0]=b+TV_W;a[1]=(random32()%2?random32()&0xffff0000u:0)|code;(void)n;return;
    }
    /* Servo configuration and state: scale, step, bounds, clip, mount z. */
    static const uint16_t scales[]={0x100,0x80,0x200,0x40,0,1,0x7fff,0x8000,0xffff};
    static const uint16_t bounds[]={0x1000,0x2000,0x800,0,1,0x7fff,0x8000,0xffff,0xf000,0xe000};
    static const uint16_t words[]={0,1,0xffff,0x8000,0x7fff,0x100,0xff00,0x40,0xffc0};
    for(unsigned k=0;k<2;++k){put16(c+0x1c+(8*k),tv_word(scales,9));put16(c+0x1e +(8*k),(uint16_t)(random32()%2?random32()%0x400u:random32()));}
    put16(c+0x20,tv_word(bounds,10));put16(c+0x28,tv_word(bounds,10));put16(c+0x2a,tv_word(bounds,10));
    put16(c+0x64,(uint16_t)(random32()%2?random32()%0x100u:random32()));
    for(unsigned k=0;k<4;++k)put16(p+0x294+2*k,tv_word(words,9));
    if(random32()%3==0)put16(p+tv_angle_off(),tv_word(bounds,10));
    put32(p+0x230,random32()%3?0:random32()|1u);put32(p+0x234,random32()%3?0:random32()|1u);
    p[0xa3]=(BYTE)(random32()%2?0:random32()%2?1:random32());
    if(index==ES2_HOOK_INDEX_weapon_converge){
        static const uint32_t distances[]={0,0,1,0x5000,0x7530,0xffffffffu,0x80000000u,0x7fffffffu,0x1000000};
        a[0]=b+TV_P;a[1]=random32()%3?distances[random32()%9]:random32()%2?random32()%0x40000u:random32();
        if(tv_real)a[1]=(uint32_t)((int32_t)(a[1]<<7)>>7);
        return;
    }
    uint32_t rate=(random32()&0xffff0000u)|tv_word(words,9);
    uint16_t angle=get16(p+tv_angle_off());
    static const uint16_t offsets[]={0,1,0xffff,2,0xfffe};
    uint16_t target=random32()%2?(uint16_t)(angle+offsets[random32()%5]):random32()%2?tv_word(bounds,10):(uint16_t)random32();
    uint32_t snap=random32()%2?random32()&0xffff0000u:random32()|1u;
    tv_target=target;
    a[0]=b+TV_P;a[1]=rate;
    if(index==ES2_HOOK_INDEX_torso_twist){a[2]=(random32()&0xffff0000u)|target;a[3]=snap;return;}
    static const uint32_t distances[]={0,0x5000,1,0xffffffffu};
    a[2]=random32()%2?distances[random32()%4]:random32();
    if(tv_real)a[2]=(uint32_t)((int32_t)(a[2]<<7)>>7);
    a[3]=(random32()&0xffff0000u)|target;a[4]=snap;
}
/* Calls the original with chosen EAX/EDX/ECX and ESI (410f78 returns the
 * caller's ESI high word); five argument slots. */
__attribute__((naked)) static void tv_machine(void *target,const uint32_t *a,const uint32_t *registers,uint32_t esi,uint32_t pair[2]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl 12(%ebp),%esi\n\tpushl 16(%esi)\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tmovl 20(%ebp),%esi\n\t"
        "call *8(%ebp)\n\tmovl 24(%ebp),%ecx\n\tmovl %eax,(%ecx)\n\tmovl %edx,4(%ecx)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* The same from a stack filled with a pattern, EAX = EDX = ECX = pattern. */
__attribute__((naked)) static void tv_poison_call(void *target,const uint32_t *a,uint32_t pattern,uint32_t esi,uint32_t pair[2]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 16(%ebp),%eax\n\tmovl $1024,%ecx\n\trep stosl\n\taddl $3840,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 16(%esi)\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%eax\n\tmovl %eax,%edx\n\tmovl %eax,%ecx\n\tmovl 20(%ebp),%esi\n\tcall *8(%ebp)\n\t"
        "movl 24(%ebp),%ecx\n\tmovl %eax,(%ecx)\n\tmovl %edx,4(%ecx)\n\tleal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static int tv_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const tv_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,tv_calls);
    for(unsigned j=0;j<calls&&j<tv_calls&&expected;++j)if(memcmp(expected+j,tv_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(tv_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<TV_SIZE;++j)if(tv_after[j]!=tv_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,tv_after[j],tv_arena[j]);break;}
    return 1;
}
static unsigned tv_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<tv_calls;++j){unsigned id=tv_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?tv_slots[id]&&tv_slots[id]==c->key:!tv_slots[id]&&tv_addresses[id]==c->key)++hits;}
    return hits;
}
static int tv_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r,uint32_t esi){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    static tv_record expected[TV_MAX];
    memcpy(tv_before,tv_arena,TV_SIZE);
    uint32_t got[2];tv_reset();tv_machine(mapped+d->rva,a,r,esi,got);
    uint64_t pair[2];pair[0]=(uint64_t)got[1]<<32|got[0];unsigned calls=tv_calls;memcpy(expected,tv_log,calls*sizeof(*expected));
    memcpy(tv_after,tv_arena,TV_SIZE);memcpy(tv_arena,tv_before,TV_SIZE);tv_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    /* weapon_keys' adapter takes the caller's ESI in the EAX slot (shim). */
    const uint32_t entry[3]={index==ES2_HOOK_INDEX_weapon_keys?esi:r[0],r[1],r[2]};
    pair[1]=x->replacement_context(a,d->user,entry);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=tv_expected_hits(c+i))return fail("torso callee counter delta");
    if(pair[0]!=pair[1]||calls!=tv_calls||memcmp(expected,tv_log,calls*sizeof(*expected))||memcmp(tv_after,tv_arena,TV_SIZE))
        return tv_report("direct",n,pair,calls,expected);
    return 0;
}
static unsigned tv_which(unsigned index){
    switch(index){
    case ES2_HOOK_INDEX_torso_twist:return 0x1fu;
    case ES2_HOOK_INDEX_torso_pitch:return 0x6fu;
    case ES2_HOOK_INDEX_weapon_converge:return 0x180u;
    default:return 0x1e00u;
    }
}
static int tv_patch(BYTE saved[TV_IDS][5],int active[TV_IDS],unsigned which,int on){
    for(unsigned i=0;i<TV_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!tv_addresses[i])continue;if(actor_patch(tv_addresses[i]-0x400000u,tv_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(tv_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
static int tv_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],esi,expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;tv_build(index,n,a,r,&esi);
        BYTE expected_fpu[108],actual_fpu[108];static tv_record expected[TV_MAX];
        memcpy(tv_before,tv_arena,TV_SIZE);
        tv_reset();tv_poison_call(mapped+d->rva,a,pattern,esi,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=tv_calls;memcpy(expected,tv_log,calls*sizeof(*expected));memcpy(tv_after,tv_arena,TV_SIZE);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("torso poison install");
        memcpy(tv_arena,tv_before,TV_SIZE);tv_reset();tv_poison_call(mapped+d->rva,a,pattern,esi,actual_pair);
        es2_hook_fpu_save(actual_fpu);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=tv_calls||memcmp(expected,tv_log,calls*sizeof(*expected))||memcmp(tv_after,tv_arena,TV_SIZE)||
           memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=tv_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("torso poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state\n",d->name,total);
    return status;
}
/* Real callees with the nested hooks in rotating original/replaced/compare
 * modes (compare falls back to replaced where refused) and a patterned
 * stack; the remaining stubs' inputs are compared in order. */
static int tv_real_pass(unsigned index){
    static const unsigned twist_nested[]={ES2_HOOK_INDEX_multiply_q8,ES2_HOOK_INDEX_movement_step,ES2_HOOK_INDEX_approach_word};
    static const unsigned pitch_nested[]={ES2_HOOK_INDEX_multiply_q8,ES2_HOOK_INDEX_movement_step,ES2_HOOK_INDEX_approach_word,
        ES2_HOOK_INDEX_weapon_converge,ES2_HOOK_INDEX_aim_angles};
    static const unsigned converge_nested[]={ES2_HOOK_INDEX_aim_angles,ES2_HOOK_INDEX_vector_angle,ES2_HOOK_INDEX_dist2d_delta};
    const unsigned *nested;unsigned count,stubs;
    if(index==ES2_HOOK_INDEX_torso_twist){nested=twist_nested;count=3;stubs=0x18u;}
    else if(index==ES2_HOOK_INDEX_torso_pitch){nested=pitch_nested;count=5;stubs=0xa8u;}
    else {nested=converge_nested;count=3;stubs=0x80u;}
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[6]={0};
    BYTE saved[TV_IDS][5];int active[TV_IDS];if(tv_patch(saved,active,stubs,1))return 1;
    static uint32_t expected_log[TV_REAL_MAX];
    tv_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],esi,expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;tv_build(index,n,a,r,&esi);
        memcpy(tv_before,tv_arena,TV_SIZE);
        es2_hook hk[6];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("torso nested install");}
        tv_reset();tv_poison_call(mapped+d->rva,a,pattern,esi,expected_pair);
        unsigned logged=tv_real_count;memcpy(expected_log,tv_real_log,4*logged);
        memcpy(tv_after,tv_arena,TV_SIZE);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+count,d,&o,function_hook_extension(index)))return fail("torso caller install");
        memcpy(tv_arena,tv_before,TV_SIZE);tv_reset();
        tv_poison_call(mapped+d->rva,a,pattern,esi,actual_pair);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||logged!=tv_real_count||memcmp(expected_log,tv_real_log,4*logged)||memcmp(tv_after,tv_arena,TV_SIZE)){
            fprintf(stderr,"real mode=%u stub log %u/%u\n",mode,logged,tv_real_count);tv_calls=0;status=tv_report("real",n,pair,0,NULL);
        }
        for(unsigned j=0;j<=count;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("torso nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("torso nested uninstall");}
        ++total;
    }
    tv_real=0;
    if(tv_patch(saved,active,0,0))return 1;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){
        fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, 31/a7 stack, pair, memory and stub inputs\n",
        d->name,total,count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
/* a weapon system on which the real 41074c and 410708 run: count 1..16,
 * selection -1..count-1 (often 0, so that Alt+W steps below 0), group 0..2 with
 * its member table, weapons with definitions. The loop of 41074c ends within
 * count+1 rounds there (weapons.md, "weapon-system routines below the weapon
 * keys"). */
static void tv_keys_state(uint32_t a[6]){
    const uint32_t b=TV_BASE;BYTE *w=tv_arena+TV_W;
    const unsigned count=1u+random32()%TV_SLOTS;put16(w+8,(uint16_t)count);
    for(unsigned i=0;i<TV_SLOTS;++i)put32(tv_arena+TV_LIST+4*i,random32()%4==0?0:b+TV_S+0x50u*(random32()%4));
    for(unsigned k=0;k<4;++k){
        BYTE *e=tv_arena+TV_S+0x50*k,*d=tv_arena+TV_DEF+0x20*k;
        put32(e,b+TV_VTABLE);put32(e+0x18,b+TV_DEF+0x20u*k);
        e[0x4b]=(BYTE)(random32()%2);e[0x4c]=(BYTE)(random32()%4?1:0);
        d[0x16]=(BYTE)(random32()%2?random32()%3u:random32());
    }
    w[0x1c]=(BYTE)(random32()%3);
    for(unsigned g=0;g<3;++g){
        put32(w+0x1f+6*g,b+TV_TABLES+0x20u*g);
        for(unsigned i=0;i<TV_SLOTS;++i)put16(tv_arena+TV_TABLES+0x20*g+2*i,random32()%2?0:(uint16_t)(1u+random32()%0xffffu));
    }
    w[0x1d]=(BYTE)(random32()%8==0?0xffu:random32()%2?0u:random32()%count);
    /* The selected slot is filled: -/= and Num-/Num+ call its +38h. */
    if((int8_t)w[0x1d]>=0)put32(tv_arena+TV_LIST+4*w[0x1d],b+TV_S+0x50u*(random32()%4));
    static const uint16_t codes[]={0x211,0x211,0x211,0x211,0x11,0x11,0x2c,0x30,0x202,0x20b,0x0c,0x0d,0x4a,0x4e,0x10,0x212};
    a[0]=b+TV_W;a[1]=(random32()%2?random32()&0xffff0000u:0)|codes[random32()%16];
}
/* The real weapon_keys 410f78 (original or replaced) over the real 41074c
 * (select_step original or replaced) and 410708; 410d04, 410ae4, 4110ac and the
 * virtual +38h stay stubs (inputs compared in order). The whole pair is
 * compared: on "step below 0" 41074c puts ESI - 1 into the index slot that
 * 410708 returns in EDX, so EDX shows which ESI the calls of the replaced
 * 410f78 carry. The word 4d2528 that 410708 clears is planted, compared and
 * restored. */
static int tv_real_keys(void){
    const unsigned outer=ES2_HOOK_INDEX_weapon_keys,nested=ES2_HOOK_INDEX_select_step;
    const es2_hook_descriptor *d=function_hook_descriptor(outer);
    int status=0;unsigned total=0,below=0;uint64_t entered[2]={0,0};static uint32_t expected_log[TV_REAL_MAX];
    BYTE saved[TV_IDS][5];int active[TV_IDS];if(tv_patch(saved,active,0x1600u,1))return 1;
    BYTE saved_word[2],planted[2],after_word[2];memcpy(saved_word,mapped+0xd2528,2);
    tv_real=1;
    for(unsigned mode=0;mode<4&&!status;++mode)for(unsigned n=0;n<768&&!status;++n){
        uint32_t a[6],r[3],esi,expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;
        tv_build(outer,n,a,r,&esi);tv_keys_state(a);
        if((uint16_t)a[1]==0x211&&tv_arena[TV_W+0x1d]==0)++below;
        put16(mapped+0xd2528,(uint16_t)random32());memcpy(planted,mapped+0xd2528,2);memcpy(tv_before,tv_arena,TV_SIZE);
        tv_reset();tv_poison_call(mapped+d->rva,a,pattern,esi,expected_pair);
        unsigned logged=tv_real_count;memcpy(expected_log,tv_real_log,4*logged);
        memcpy(tv_after,tv_arena,TV_SIZE);memcpy(after_word,mapped+0xd2528,2);
        es2_hook hk[2];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        o.mode=mode&1?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(hk,function_hook_descriptor(nested),&o,function_hook_extension(nested)))return fail("torso keys nested install");
        o.mode=mode&2?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(hk+1,d,&o,function_hook_extension(outer)))return fail("torso keys caller install");
        memcpy(tv_arena,tv_before,TV_SIZE);memcpy(mapped+0xd2528,planted,2);tv_reset();
        tv_poison_call(mapped+d->rva,a,pattern,esi,actual_pair);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||logged!=tv_real_count||memcmp(expected_log,tv_real_log,4*logged)||memcmp(tv_after,tv_arena,TV_SIZE)||
           memcmp(after_word,mapped+0xd2528,2)){
            fprintf(stderr,"real keys mode=%u stub log %u/%u\n",mode,logged,tv_real_count);tv_calls=0;status=tv_report("real keys",n,pair,0,NULL);
        }
        entered[0]+=hk[0].calls;entered[1]+=hk[1].calls;
        for(unsigned j=0;j<2;++j)if(!es2_hook_uninstall(hk+j))return fail("torso keys uninstall");
        ++total;
    }
    tv_real=0;memcpy(mapped+0xd2528,saved_word,2);
    if(tv_patch(saved,active,0,0))return 1;
    if(!status&&(!entered[0]||!entered[1]||!below)){
        fprintf(stderr,"FAIL: weapon_keys real keys pass entered %llu/%llu, below 0 %u\n",
            (unsigned long long)entered[0],(unsigned long long)entered[1],below);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases over the real 41074c/410708 (select_step nested, %llu entries, %u steps below 0), full pair\n",
        d->name,total,(unsigned long long)entered[0],below);
    return status;
}
static int torso_cases(unsigned index,unsigned limit){
    tv_index=index;tv_real=0;
    BYTE saved[TV_IDS][5];int active[TV_IDS];
    if(tv_patch(saved,active,tv_which(index),1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_TORSO_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3],esi;tv_build(index,n,a,r,&esi);status=tv_compare(index,n,a,r,esi);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=tv_installed_probes(index,64);
    if(tv_patch(saved,active,0,0))return 1;
    current_case_valid=0;
    if(!limit&&!status&&index!=ES2_HOOK_INDEX_weapon_keys)status=tv_real_pass(index);
    if(!limit&&!status&&index==ES2_HOOK_INDEX_weapon_keys)status=tv_real_keys();
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, payloads by content, arena\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
