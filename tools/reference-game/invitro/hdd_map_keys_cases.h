/* hdd_map_keys group: the HDD map key path map_key 44cc68, map_press 44d5c0,
 * wingman_pick 44d82c, wingman_index 433134, mode_next 44ee48, mode_prev
 * 44ee88, target_cycle1 44ef08, target_cycle0 44ef1c and target_cycle 44ef30
 * (docs/reverse-engineering/cockpit.md, "HDD map key path"). An 8 KiB arena
 * holds the HDDMap object M, its eight mode buttons (rectangle at +4), the map
 * view (M+14h), two map gadgets G/G2 and their holders P/P2, the graphics
 * context C with its offsets object B, twelve actors with their type records
 * (side byte +12h, dead byte +99h) and the actor list (16 slots and a null
 * guard); the image globals 49d6ac..49d6b3, 49b088, 4a9af8..4a9aff, the wingman
 * table 4d03c8..4d03d3, 49dbe0 and 4d2556..4d255d are set per case, compared
 * and restored. Per case the key code (the fifteen handled keys, other words,
 * random high words), the gadget/mode arguments of map_press, the map mode
 * (pick modes, -1, edges of the signed step tests), the pick byte M+103h, the
 * wingman index M+105h (0..60, -1..-4: the record stays in the arena), the
 * current target M+15Ch (0, a list member before or behind the count, or actor
 * 11, never in the list, side byte 7Fh), the count word (0..16 or negative),
 * the side argument and the entry EAX/EDX/ECX vary. Every callee is a logging
 * stub (ordered calls with their arguments, the bridge's slot count and entry
 * EAX/EDX/ECX, stack values masked; the points behind 44d3cc's, 48c5fc's and
 * 44d24c's pointers), returns registers from a per-case stream, writes the
 * outputs of 486ece, 48c57c, 48c5fc and 44d24c and in about one call of four
 * applies a side effect to M, P, B, an actor or a global, so a replacement that
 * caches a value the original rereads differs. 44d888's stub returns 0 or an
 * actor, 44f03c's accepts one call of four and every call from the 40th on (the
 * target cycle can loop without end in the original when the start entry lies
 * behind the count). The full EDX:EAX pair, the call log, the arena and the
 * globals are compared, and for the first 4,096 cases the callee counters
 * against the logged calls. Then installed 31/a7 poison probes and real-callee
 * passes against the all-original run: map_key with map_press, target_cycle1
 * and target_cycle (real 44d5c0, 44ee48, 44ee88, 44ef08, 44ef1c, 44ef30) in
 * every original/replaced combination; map_press with the real 44d3cc and
 * 44d24c; wingman_pick and wingman_index (real 433134); mode_next/mode_prev
 * with map_mode 44d9f4 (real); target_cycle1/target_cycle0 with target_cycle;
 * target_cycle under its real callers 44ef08/44ef1c. Identifiers: hmk_/HMK_. */
enum { HMK_SIZE=0x2000, HMK_M=0x100, HMK_BUTTONS=0x600, HMK_VIEW=0x700, HMK_P=0x780, HMK_P2=0x7c0, HMK_G=0x800,
    HMK_G2=0xa00, HMK_C=0xc00, HMK_B=0xc80, HMK_HDD=0xf00, HMK_ACT=0x1000, HMK_ACTORS=12, HMK_TYPES=0x1c00,
    HMK_LIST=0x1d00, HMK_SLOTS=16, HMK_IDS=23, HMK_MAX=64, HMK_ACCEPT=40, HMK_GLOBALS=8+4+8+12+4+8 };
#define HMK_BASE ((uint32_t)(uintptr_t)hmk_arena)
#define HMK_ADDR(o) (HMK_BASE+(uint32_t)(o))
#define HMK_ACTOR(i) HMK_ADDR(HMK_ACT+0x100u*(i))
static BYTE hmk_arena[HMK_SIZE],hmk_before[HMK_SIZE],hmk_after[HMK_SIZE];
typedef struct hmk_record { uint32_t id,slots,a[3],regs[3],local[3]; } hmk_record;
static hmk_record hmk_log[HMK_MAX];
static unsigned hmk_calls,hmk_paths[HMK_IDS];
static uint32_t hmk_seed,hmk_stream;
__attribute__((used)) static uint32_t hmk_ecx;
static const uint32_t hmk_addr[HMK_IDS]={0x438d9c,0x44d5c0,0x44ee48,0x44ee88,0x44ef08,0x44ef1c,0x44dd14,0x462830,
    0x44d3cc,0x486ece,0x48c2e4,0x48c444,0x48c57c,0x48c5fc,0x44d24c,0x44d888,0x433134,0x44a748,0x44d9f4,0x44ef30,
    0x44f03c,0x433e3c,0x44dc6c};
static const unsigned hmk_arity[HMK_IDS]={3,3,1,1,1,1,1,1,3,1,1,1,2,2,3,2,1,2,2,2,3,1,3};
#define HMK_ALL ((1u<<HMK_IDS)-1u)
#define HMK_BIT(id) (1u<<(id))
static uint32_t hmk_next(void){
    uint32_t z=hmk_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static void hmk_reset(void){hmk_calls=0;hmk_stream=hmk_seed;}
static int hmk_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static void hmk_globals(BYTE *out,int restore){
    static const struct { uint32_t rva,size; } g[]={{0x9d6ac,8},{0x9b088,4},{0xa9af8,8},{0xd03c8,12},{0x9dbe0,4},{0xd2556,8}};
    BYTE *p=out;for(unsigned i=0;i<6;++i){if(restore)memcpy(mapped+g[i].rva,p,g[i].size);else memcpy(p,mapped+g[i].rva,g[i].size);p+=g[i].size;}
}
/* A wingman index whose order record stays in the arena. */
static uint32_t hmk_index(void){
    switch(hmk_next()%8){
    case 0:return 0xffffffffu;
    case 1:return 3u+hmk_next()%58u;
    case 2:return 0u-2u-hmk_next()%3u;
    default:return hmk_next()%3u;
    }
}
static uint32_t hmk_mode(void){
    static const uint32_t modes[]={0xa,0xb,0xc,0xd,0xe,0xf,0x10,0x11,0xb,0xc,0xd,0xe,0xffffffffu,0x9,0x12,0x80000008u,
        0x7fffffffu,0x80000000u,0xb,0xc};
    return hmk_next()%16?modes[hmk_next()%20]:hmk_next();
}
/* A current target: 0, a list member (usually before the count) or actor
 * 11, which is never listed. */
static uint32_t hmk_target(void){
    const BYTE *L=hmk_arena+HMK_LIST;int c=(int16_t)get16(mapped+0xa9afe);
    switch(hmk_next()%8){
    case 0:return 0u;
    case 1:return HMK_ACTOR(11);
    case 2:{uint32_t v=get32(L+4u*(hmk_next()%HMK_SLOTS));return v?v:HMK_ACTOR(11);}
    default:{
        if(c<=0||c>HMK_SLOTS)return 0u;
        uint32_t v=get32(L+4u*(hmk_next()%(unsigned)c));return v?v:0u;}
    }
}
/* One callee side effect (about one call in four), drawn from the stream. */
static void hmk_effect(void){
    uint32_t pick=hmk_next();if(pick%4)return;
    BYTE *M=hmk_arena+HMK_M;
    switch(pick/4%12){
    case 0:put32(M+0x5d,hmk_mode());break;
    case 1:put32(M+0x105,hmk_index());break;
    case 2:put32(M+0x15c,hmk_target());break;
    case 3:M[0x103]=(BYTE)(hmk_next()%3?hmk_next()%2:hmk_next());break;
    case 4:put32(M+0x39,get32(M+0x35)^(1u+hmk_next()%0xffu));break;
    case 5:put32(M+0x8,hmk_next());put32(M+0x18,hmk_next());break;
    case 6:put32(hmk_arena+HMK_P+8,HMK_ADDR(hmk_next()%2?HMK_G2:HMK_G));break;
    case 7:put32(hmk_arena+HMK_B+0x210+4*(hmk_next()%6),hmk_next());break;
    case 8:hmk_arena[HMK_ACT+0x100*(hmk_next()%HMK_ACTORS)+0x99]=(BYTE)(hmk_next()%2);break;
    case 9:put32(M+0x14,HMK_ADDR(hmk_next()%2?HMK_VIEW:HMK_VIEW+0x40));break;
    case 10:put32(M+0x71,hmk_next());put32(M+0x7d,hmk_next());break;
    default:put32(mapped+0x9d6b0,HMK_ADDR(hmk_next()%2?HMK_P2:HMK_P));break;
    }
}
static uint32_t hmk_word(uint32_t address,unsigned k){return get32((const BYTE *)(uintptr_t)address+4u*k);}
static void hmk_put(uint32_t address,unsigned words){for(unsigned k=0;k<words;++k)put32((BYTE *)(uintptr_t)address+4u*k,hmk_next());}
__attribute__((used,noinline)) static uint64_t __cdecl hmk_log_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(hmk_calls==HMK_MAX){fail("hdd_map_keys synthetic call overflow");ExitProcess(1);}
    hmk_record *v=hmk_log+hmk_calls++;memset(v,0,sizeof(*v));v->id=id;v->slots=weapons_bridge_slots(a,hmk_arity[id]);
    for(unsigned j=0;j<hmk_arity[id];++j)v->a[j]=hmk_stack(a[j])?0u:a[j];
    memcpy(v->regs,regs,12);for(unsigned j=0;j<3;++j)if(hmk_stack(v->regs[j]))v->regs[j]=0;
    /* The points the callee reads through its pointers. */
    if(id==8){v->local[0]=hmk_word(a[2],0);v->local[1]=hmk_word(a[2],1);}
    if(id==13){for(unsigned k=0;k<3;++k)v->local[k]=hmk_word(a[0],k);}
    if(id==14){v->local[0]=hmk_word(a[1],0);v->local[1]=hmk_word(a[1],1);}
    /* The outputs the callee writes. */
    if(id==9)hmk_put(a[0],2);
    if(id==12)hmk_put(a[1],3);
    if(id==13)hmk_put(a[1],2);
    if(id==14)hmk_put(a[2],2);
    uint32_t eax=hmk_next(),edx=hmk_next();hmk_ecx=hmk_next();
    if(id==15)eax=hmk_next()%4?HMK_ACTOR(hmk_next()%HMK_ACTORS):0u;
    if((id==4||id==5||id==19)&&hmk_next()%4==0)eax=0u;
    if(id==16){uint32_t k=hmk_next()%6;eax=k<3?k:k==3?0xffffffffu:hmk_next();}
    if(id==20)eax=(eax&0xffffff00u)|(hmk_calls>=HMK_ACCEPT||hmk_next()%4==0?(hmk_next()%255u+1u):0u);
    hmk_effect();
    return (uint64_t)edx<<32|eax;
}
#define HMK_STUB(n) __attribute__((naked)) static void hmk_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _hmk_log_impl\n\taddl $24,%esp\n\tmovl _hmk_ecx,%ecx\n\tret\n\t");}
HMK_STUB(0) HMK_STUB(1) HMK_STUB(2) HMK_STUB(3) HMK_STUB(4) HMK_STUB(5) HMK_STUB(6) HMK_STUB(7) HMK_STUB(8) HMK_STUB(9)
HMK_STUB(10) HMK_STUB(11) HMK_STUB(12) HMK_STUB(13) HMK_STUB(14) HMK_STUB(15) HMK_STUB(16) HMK_STUB(17) HMK_STUB(18)
HMK_STUB(19) HMK_STUB(20) HMK_STUB(21) HMK_STUB(22)
#undef HMK_STUB
static void *hmk_stubs[HMK_IDS]={hmk_stub_0,hmk_stub_1,hmk_stub_2,hmk_stub_3,hmk_stub_4,hmk_stub_5,hmk_stub_6,hmk_stub_7,
    hmk_stub_8,hmk_stub_9,hmk_stub_10,hmk_stub_11,hmk_stub_12,hmk_stub_13,hmk_stub_14,hmk_stub_15,hmk_stub_16,hmk_stub_17,
    hmk_stub_18,hmk_stub_19,hmk_stub_20,hmk_stub_21,hmk_stub_22};
static int hmk_patch(BYTE saved[HMK_IDS][5],int active[HMK_IDS],unsigned which,int on){
    for(unsigned i=0;i<HMK_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(hmk_addr[i]-0x400000u,hmk_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(hmk_addr[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
/* The stub id of a hook's own function (not patched in its direct cases). */
static unsigned hmk_own(unsigned index){
    const uint32_t va=function_hook_descriptor(index)->rva+0x400000u;
    for(unsigned i=0;i<HMK_IDS;++i)if(hmk_addr[i]==va)return HMK_BIT(i);
    return 0u;
}
static uint32_t hmk_coordinate(void){
    static const uint32_t edges[]={0,1,0xffffffffu,0x7fffffffu,0x80000000u};
    switch(hmk_next()%8){case 0:return edges[hmk_next()%5];case 1:return hmk_next();default:return hmk_next()%640u;}
}
static uint32_t hmk_code(void){
    static const uint16_t keys[]={0x20,0x1e,0x21,0x14,0x22,0x18,0x2e,0x12,0x2d,0x0e,0x34,0x33,0x1c,0x0f,0x4c,0x1c,0x0f,0x1c,0x0f,0x1c};
    uint32_t low;
    switch(hmk_next()%8){
    case 0:low=hmk_next()%0x60u;break;
    case 1:low=hmk_next()&0xffffu;break;
    case 2:low=hmk_next()%2?0x0du:0x4du;break;
    default:low=keys[hmk_next()%20];break;
    }
    return hmk_next()%4?low:(hmk_next()&0xffff0000u)|low;
}
/* Builds one state. a[0] = M (the actor for wingman_index); a[1]/a[2] the
 * code (map_key), gadget and mode (map_press) or side (target_cycle). */
static void hmk_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    hmk_seed=random32();hmk_stream=hmk_seed^n;random_bytes(hmk_arena,HMK_SIZE);memset(a,0,24);for(unsigned i=0;i<3;++i)r[i]=hmk_next();
    BYTE *M=hmk_arena+HMK_M;const uint32_t Ma=HMK_ADDR(HMK_M);
    /* Globals: holders, gadgets, the graphics context, the event globals. */
    put32(mapped+0x9d6b0,HMK_ADDR(HMK_P));put32(hmk_arena+HMK_P+8,HMK_ADDR(HMK_G));put32(hmk_arena+HMK_P2+8,HMK_ADDR(HMK_G2));
    for(unsigned k=0;k<4;++k)mapped[0x9d6ac+k]=(BYTE)hmk_next();
    put32(mapped+0x9b088,HMK_ADDR(HMK_C));put32(hmk_arena+HMK_C+4,HMK_ADDR(HMK_B));
    put32(mapped+0x9dbe0,hmk_next());put32(mapped+0xd2556,hmk_coordinate());put32(mapped+0xd255a,hmk_coordinate());
    /* Actors 0..11 (type records, side and dead bytes); 11 is never listed. */
    for(unsigned i=0;i<HMK_ACTORS;++i){
        BYTE *t=hmk_arena+HMK_TYPES+0x10*i,*x=hmk_arena+HMK_ACT+0x100*i;
        put32(x+0x45,HMK_ADDR(HMK_TYPES+0x10*i));
        static const BYTE sides[]={0,1,0xff,0,1};
        t[0x12]=i==11?0x7f:hmk_next()%6?sides[hmk_next()%5]:(BYTE)hmk_next();
        if(t[0x12]==0x7f&&i!=11)t[0x12]=0;
        x[0x99]=(BYTE)(hmk_next()%4==0);
    }
    /* The list: 16 slots and a null guard, the count word, the wingmen. */
    BYTE *L=hmk_arena+HMK_LIST;
    for(unsigned s=0;s<HMK_SLOTS;++s)put32(L+4*s,hmk_next()%12?HMK_ACTOR(hmk_next()%11):0u);
    put32(L+4*HMK_SLOTS,0u);
    put32(mapped+0xa9af8,HMK_ADDR(HMK_LIST));put16(mapped+0xa9afc,(uint16_t)hmk_next());
    put16(mapped+0xa9afe,(uint16_t)(hmk_next()%16?hmk_next()%13u:hmk_next()%2?14u+hmk_next()%3u:0x8000u|hmk_next()));
    for(unsigned k=0;k<3;++k)put32(mapped+0xd03c8+4*k,hmk_next()%5?HMK_ACTOR(hmk_next()%HMK_ACTORS):hmk_next()%2?0u:hmk_next());
    /* M: HDD, view, centre, gadget codes, buttons, mode, flags, target. */
    put32(M+8,HMK_ADDR(HMK_HDD));put32(M+0x14,HMK_ADDR(HMK_VIEW));
    uint32_t panel=hmk_next(),area=hmk_next();if(area==panel)area^=1u;   /* distinct, as the game's gadget codes */
    put32(M+0x35,panel);put32(M+0x39,area);
    for(unsigned i=0;i<8;++i){
        uint32_t btn=HMK_ADDR(HMK_BUTTONS+0x20u*i);
        if(hmk_next()%32==0)btn=Ma+0x100u+hmk_next()%0x240u;
        put32(M+0x3d+4*i,btn);
        BYTE *rect=(BYTE *)(uintptr_t)btn+4;put32(rect,hmk_coordinate());put32(rect+4,hmk_coordinate());
    }
    put32(M+0x5d,hmk_mode());
    M[0x103]=(BYTE)(hmk_next()%4?1u:hmk_next()%2?0u:hmk_next());
    put32(M+0x105,hmk_index());
    put32(M+0x15c,hmk_target());
    for(unsigned k=0;k<4;++k)put32(M+0x71+4*k,hmk_coordinate());
    a[0]=Ma;
    if(index==ES2_HOOK_INDEX_map_key)a[1]=hmk_code();
    if(index==ES2_HOOK_INDEX_map_press){
        switch(hmk_next()%4){
        case 0:case 1:a[1]=panel;a[2]=0xau+hmk_next()%8u;break;
        case 2:a[1]=area;{static const uint32_t m[]={0,1,0,1,2,0xffffffffu};a[2]=hmk_next()%8?m[hmk_next()%6]:hmk_next();}break;
        default:a[1]=hmk_next();a[2]=hmk_next()%20u;break;
        }
    }
    if(index==ES2_HOOK_INDEX_wingman_index){
        switch(hmk_next()%5){
        case 0:a[0]=0u;break;
        case 1:a[0]=hmk_next();break;
        case 2:a[0]=HMK_ACTOR(hmk_next()%HMK_ACTORS);break;
        default:a[0]=get32(mapped+0xd03c8+4*(hmk_next()%3));break;
        }
    }
    if(index==ES2_HOOK_INDEX_target_cycle){
        static const uint32_t s[]={0,1,0xffff,0x10001,0xffffffffu,0x10000};
        a[1]=hmk_next()%4?s[hmk_next()%6]:hmk_next();
        if((uint16_t)a[1]==0x7f)a[1]^=0x100u;
    }
    hmk_stream=hmk_seed;   /* the stubs' stream restarts here in both runs */
}
static unsigned hmk_hits(uint32_t key){
    unsigned h=0;for(unsigned j=0;j<hmk_calls;++j)if(hmk_addr[hmk_log[j].id]==key)++h;
    return h;
}
static int hmk_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const hmk_record *e,const BYTE *want,const BYTE *got){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,hmk_calls);
    for(unsigned j=0;j<calls&&j<hmk_calls&&e;++j)if(memcmp(e+j,hmk_log+j,sizeof(*e))){
        const uint32_t *x=(const uint32_t*)(e+j),*y=(const uint32_t*)(hmk_log+j);
        for(unsigned k=0;k<sizeof(*e)/4;++k)if(x[k]!=y[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,e[j].id,k,x[k],y[k]);break;}
    for(unsigned j=0;j<HMK_SIZE;++j)if(hmk_after[j]!=hmk_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,hmk_after[j],hmk_arena[j]);break;}
    for(unsigned j=0;j<HMK_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);break;}
    return 1;
}
static int hmk_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[HMK_GLOBALS],after[HMK_GLOBALS],actual[HMK_GLOBALS];static hmk_record expected[HMK_MAX];
    memcpy(hmk_before,hmk_arena,HMK_SIZE);hmk_globals(before,0);
    hmk_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);
    unsigned calls=hmk_calls;memcpy(expected,hmk_log,calls*sizeof(*expected));memcpy(hmk_after,hmk_arena,HMK_SIZE);hmk_globals(after,0);
    for(unsigned j=0;j<calls;++j)hmk_paths[expected[j].id]++;
    memcpy(hmk_arena,hmk_before,HMK_SIZE);hmk_globals(before,1);hmk_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8]={0};
    for(unsigned i=0;i<count&&i<8;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);hmk_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count&&i<8;++i)if(c[i].count-counts[i]!=hmk_hits(c[i].key))return fail("hdd_map_keys callee counter delta");
    if(pair[0]!=pair[1]||calls!=hmk_calls||memcmp(expected,hmk_log,calls*sizeof(*expected))||memcmp(hmk_after,hmk_arena,HMK_SIZE)||
       memcmp(after,actual,HMK_GLOBALS))
        return hmk_report("direct",n,pair,calls,expected,after,actual);
    hmk_globals(before,1);
    return 0;
}
static int hmk_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;hmk_build(index,n*41u+3u,a,r);
        BYTE before[HMK_GLOBALS],after[HMK_GLOBALS],actual[HMK_GLOBALS],expected_fpu[108],actual_fpu[108];static hmk_record expected[HMK_MAX];
        memcpy(hmk_before,hmk_arena,HMK_SIZE);hmk_globals(before,0);
        hmk_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=hmk_calls;memcpy(expected,hmk_log,calls*sizeof(*expected));memcpy(hmk_after,hmk_arena,HMK_SIZE);hmk_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("hdd_map_keys poison install");
        memcpy(hmk_arena,hmk_before,HMK_SIZE);hmk_globals(before,1);hmk_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);es2_hook_fpu_save(actual_fpu);hmk_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=hmk_calls||memcmp(expected,hmk_log,calls*sizeof(*expected))||memcmp(hmk_after,hmk_arena,HMK_SIZE)||
           memcmp(after,actual,HMK_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=hmk_report(fast?"installed fast":"installed preserving",n,pair,calls,expected,after,actual);
        if(!es2_hook_uninstall(&h))return fail("hdd_map_keys poison uninstall");hmk_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/calls/arena/globals\n",d->name,total);
    return status;
}
/* Real-callee pass: the hooks of `set` installed in every original/
 * replaced combination over the entries `targets`, against the
 * all-original run; the stub ids in `real` run as original code. */
static int hmk_real(unsigned index){
    unsigned set[4],k=0,real=0,ntargets=1;uint32_t targets[2]={function_hook_descriptor(index)->rva,0};
    set[k++]=index;
    if(index==ES2_HOOK_INDEX_map_key){
        set[k++]=ES2_HOOK_INDEX_map_press;set[k++]=ES2_HOOK_INDEX_target_cycle1;set[k++]=ES2_HOOK_INDEX_target_cycle;
        real=HMK_BIT(1)|HMK_BIT(2)|HMK_BIT(3)|HMK_BIT(4)|HMK_BIT(5)|HMK_BIT(19);
    } else if(index==ES2_HOOK_INDEX_map_press)real=HMK_BIT(1)|HMK_BIT(8)|HMK_BIT(14);
    else if(index==ES2_HOOK_INDEX_wingman_pick){set[k++]=ES2_HOOK_INDEX_wingman_index;real=HMK_BIT(16);}
    else if(index==ES2_HOOK_INDEX_wingman_index){set[k++]=ES2_HOOK_INDEX_wingman_pick;real=HMK_BIT(16);targets[0]=0x4d82cu;}
    else if(index==ES2_HOOK_INDEX_mode_next||index==ES2_HOOK_INDEX_mode_prev){set[k++]=ES2_HOOK_INDEX_map_mode;real=HMK_BIT(2)|HMK_BIT(3)|HMK_BIT(18);}
    else if(index==ES2_HOOK_INDEX_target_cycle1||index==ES2_HOOK_INDEX_target_cycle0){set[k++]=ES2_HOOK_INDEX_target_cycle;real=HMK_BIT(4)|HMK_BIT(5)|HMK_BIT(19);}
    else {real=HMK_BIT(4)|HMK_BIT(5)|HMK_BIT(19);targets[0]=0x4ef08u;targets[1]=0x4ef1cu;ntargets=2;}
    const unsigned modes=1u<<k;
    BYTE saved[HMK_IDS][5];int active[HMK_IDS];if(hmk_patch(saved,active,HMK_ALL&~real,1))return 1;
    int status=0;unsigned total=0;uint64_t entered=0,nested=0;
    for(unsigned t=0;t<ntargets&&!status;++t)for(unsigned mode=0;mode<modes&&!status;++mode)for(unsigned n=0;n<256u*4u/ntargets/(modes>4?2u:1u)&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;hmk_build(index,n*67u+mode*7u+t,a,r);
        if(index==ES2_HOOK_INDEX_wingman_index)a[0]=HMK_ADDR(HMK_M);
        BYTE before[HMK_GLOBALS],after[HMK_GLOBALS],actual[HMK_GLOBALS];static hmk_record expected[HMK_MAX];
        memcpy(hmk_before,hmk_arena,HMK_SIZE);hmk_globals(before,0);
        hmk_reset();sg_poison_call(mapped+targets[t],a,pattern,expected_pair);
        unsigned calls=hmk_calls;memcpy(expected,hmk_log,calls*sizeof(*expected));memcpy(hmk_after,hmk_arena,HMK_SIZE);hmk_globals(after,0);
        es2_hook hk[4];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<k;++j){o.mode=(mode>>j&1)?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(set[j]),&o,function_hook_extension(set[j])))return fail("hdd_map_keys real install");}
        memcpy(hmk_arena,hmk_before,HMK_SIZE);hmk_globals(before,1);hmk_reset();
        sg_poison_call(mapped+targets[t],a,pattern,actual_pair);hmk_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=hmk_calls||memcmp(expected,hmk_log,calls*sizeof(*expected))||memcmp(hmk_after,hmk_arena,HMK_SIZE)||
           memcmp(after,actual,HMK_GLOBALS)){
            fprintf(stderr,"real target=%x mode=%u\n",targets[t]+0x400000u,mode);status=hmk_report("real",n,pair,calls,expected,after,actual);}
        if(mode&1)entered+=hk[0].calls;
        for(unsigned j=1;j<k;++j)if(mode>>j&1)nested+=hk[j].calls;
        for(unsigned j=0;j<k;++j)if(!es2_hook_uninstall(hk+j))return fail("hdd_map_keys real uninstall");
        hmk_globals(before,1);++total;
    }
    if(hmk_patch(saved,active,0,0))return 1;
    if(!status&&!entered){fprintf(stderr,"FAIL: %s real pass: never entered replaced\n",current_function);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases (%u hooks original/replaced in all %u combinations%s; %llu replaced entries, %llu replaced nested entries), 31/a7 stack, pair/calls/arena/globals\n",
        current_function,total,k,modes,ntargets==2?" under 44ef08/44ef1c":"",(unsigned long long)entered,(unsigned long long)nested);
    return status;
}
static int hdd_map_keys_cases(unsigned index,unsigned limit){
    BYTE old_globals[HMK_GLOBALS];hmk_globals(old_globals,0);
    BYTE saved[HMK_IDS][5];int active[HMK_IDS];const unsigned stubs=HMK_ALL&~hmk_own(index);
    if(hmk_patch(saved,active,stubs,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_HDD_MAP_KEYS_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);memset(hmk_paths,0,sizeof(hmk_paths));
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];hmk_build(index,n,a,r);status=hmk_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);current_case_valid=0;
    hmk_globals(old_globals,1);
    if(!limit&&!status)status=hmk_installed_probes(index,64);
    hmk_globals(old_globals,1);
    if(hmk_patch(saved,active,stubs,0))return 1;
    if(!limit&&!status)status=hmk_real(index);
    hmk_globals(old_globals,1);
    if(!status){
        unsigned stubbed=0;for(unsigned i=0;i<HMK_IDS;++i)stubbed+=hmk_paths[i];
        printf("PASS: %-20s %u cases (%u stub calls: 438d9c %u, 44d5c0 %u, 44ef08/1c %u/%u, 462830 %u, 44d3cc %u, 44d24c %u, 433134 %u, 44a748 %u, 44d9f4 %u, 44ef30 %u, 44f03c %u); full return pair, ordered calls with entry registers, arena and globals\n",
            function_hook_descriptor(index)->name,cases,stubbed,hmk_paths[0],hmk_paths[1],hmk_paths[4],hmk_paths[5],hmk_paths[7],hmk_paths[8],
            hmk_paths[14],hmk_paths[16],hmk_paths[17],hmk_paths[18],hmk_paths[19],hmk_paths[20]);
    }
    return status;
}
