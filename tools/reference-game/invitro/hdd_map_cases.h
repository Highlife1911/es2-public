/* hdd_map group: the HDD map frame 44c988 (map_frame)
 * (docs/reverse-engineering/cockpit.md, "HDD map frame"). A 16 KiB arena holds
 * the map object M (every dword M+15h+4j, j < 10, and the eight mode buttons
 * M+3Dh+4i point at button objects, so the mode values 0..11h except 7, also
 * below 0Ah, address valid buttons; M+31h is the wingman-button array), 16
 * buttons, four actors (the order targets), two graphics contexts X1/X2 and the
 * label bytes; for the real pass also the gadget, the cockpit holder and the
 * cursor block of map_areas. The image globals 49d7e6 (latch), 49d6ac..49d6b9
 * (blink and redraw bytes, the map holder, the blink counter), 6c5f70 (the
 * context), 49b0ac..49b0bb (colours), the label tables 4d12a8..4d1337, 4d3ba2,
 * 49b088 and 4d1638 are set per case, compared and restored. Per case the
 * event, click and pick bytes, the selected wingman (-1..2), its order record
 * and the targets' dead bytes, the state M+0Ch (0..3 or random), the clock
 * against the timer M+149h (equal, +-2 or random), bit 4 and the latch, the
 * counter word (also negative), the panel, label and redraw bytes, the previous
 * and current modes (-1, 0Ah..11h, sometimes 0..9 but 7, or equal) and their
 * enable bytes vary. The 13 callees are logging stubs (ordered calls with the
 * bridge's slot count, arguments and entry EAX/EDX/ECX; the clock returns the
 * case's time in three calls of four) that apply a side effect from the same
 * stream to M, a record, an actor, a global, a table or the context pointer in
 * about one call of four, so a replacement that caches a value the original
 * rereads differs. Every stub also records, for X1 and X2, whether [X+22Ch]
 * holds the frame's EBP-0Ch and then the record words [p] and [p+8] (and [p+4],
 * which is stack residue on the button path, in the installed probes and for
 * the map-path calls 486704/44e334), and for 43122c the 16 bytes of the
 * rectangle. Frame-valued registers and arguments (EBP-0Ch, EBP-20h) are
 * compared relative to the run's frame: the original's EBP (from the stubs),
 * the adapter's plain scratch frame (hdd_map_plain_frame) or, installed, the
 * slot address - 8. Direct cases compare the full EDX:EAX pair, the call log,
 * the arena and the globals, and for the first 4,096 cases the callee counters.
 * Installed probes run the hook through its entry shim (fast and preserving
 * dispatch, 31/a7 stack, EAX..EDI distinct patterns): the stubs also record
 * EBX/ESI/EDI, EBP and ESP relative to the caller's slot and a hash of the 256
 * bytes below their ESP, and one-slot stubs overwrite their argument slot in
 * one call of four. The real-callee pass runs 44c988 over the real tick 44d370
 * and the real map_areas 44d450 (its callees 44dc6c, 44d9f4, 438d9c and 486e7c
 * stubbed; the point behind 486e7c's argument is logged instead of its
 * address), map_frame and map_areas original/replaced in all four combinations,
 * against the all-original run. Identifiers: hmf_/HMF_. */
enum { HMF_SIZE=0x4000, HMF_M=0x200, HMF_BTN=0x800, HMF_BUTTONS=16, HMF_ARRAY=0xc10, HMF_ACT=0xd00, HMF_X1=0x1100, HMF_X2=0x1400,
    HMF_TXT=0x1700, HMF_XB=0x1800, HMF_CB=0x1900, HMF_GAD=0x1c00, HMF_IDS=17, HMF_MAX=40, HMF_MAIN=0x1fff,
    HMF_REAL=0x1ffff&~(1u<<0|1u<<2) };
#define HMF_BASE ((uint32_t)(uintptr_t)hmf_arena)
#define HMF_ADDR(o) (HMF_BASE+(uint32_t)(o))
static BYTE hmf_arena[HMF_SIZE],hmf_before[HMF_SIZE],hmf_after[HMF_SIZE];
typedef struct hmf_record { uint32_t id,slots,a[4],regs[3],cs[3],frame[2],window,ctx[2][4],rect[4]; } hmf_record;
static hmf_record hmf_log[HMF_MAX];
static unsigned hmf_calls,hmf_track,hmf_clobbers,hmf_paths[HMF_IDS];
/* hmf_run: 0 the original (frame = the stubs' EBP), 1 the adapter's plain
 * path (hdd_map_plain_frame), 2 installed (slot address - 8). */
static unsigned hmf_run;
static uint32_t hmf_seed,hmf_stream,hmf_now;
/* Captured by the stubs at entry: EAX, EDX, ECX, EBX, ESI, EDI, EBP, ESP;
 * the 256 bytes below the entry ESP; the ECX a stub returns. */
__attribute__((used)) static uint32_t hmf_cap[8],hmf_ecx,hmf_origin;
__attribute__((used)) static BYTE hmf_window[256];
static const uint32_t hmf_addr[HMF_IDS]={0x44d450,0x467828,0x44d370,0x44d6e0,0x44d82c,0x44f644,0x44de14,0x43122c,
    0x438aac,0x44dd74,0x44dd14,0x486704,0x44e334,0x44dc6c,0x44d9f4,0x438d9c,0x486e7c};
static const unsigned hmf_arity[HMF_IDS]={1,0,1,1,1,1,1,3,3,4,1,1,1,3,2,3,1};
static uint32_t hmf_next(void){
    uint32_t z=hmf_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static uint32_t hmf_value(void){return stack_clean(hmf_next());}
static void hmf_reset(void){hmf_calls=0;hmf_stream=hmf_seed;}
enum { HMF_GLOBALS=1+14+4+16+0x90+2+4+4 };
static void hmf_globals(BYTE *out,int restore){
    static const struct { uint32_t rva,size; } r[]={{0x9d7e6,1},{0x9d6ac,14},{0x2c5f70,4},{0x9b0ac,16},{0xd12a8,0x90},
        {0xd3ba2,2},{0x9b088,4},{0xd1638,4}};
    BYTE *p=out;for(unsigned i=0;i<8;++i){if(restore)memcpy(mapped+r[i].rva,p,r[i].size);else memcpy(p,mapped+r[i].rva,r[i].size);p+=r[i].size;}
}
static uint32_t hmf_button(void){return HMF_ADDR(HMF_BTN+0x40u*(hmf_next()%HMF_BUTTONS));}
static uint32_t hmf_mode(void){
    switch(hmf_next()%10){case 0:case 1:return 0xffffffffu;case 8:{const uint32_t k=hmf_next()%9;return k<7?k:k+1u;}case 9:return 0x10u+hmf_next()%2;
    default:return 0xau+hmf_next()%8;}
}
static uint32_t hmf_wingman(void){return hmf_next()%4?hmf_next()%3:0xffffffffu;}
static uint32_t hmf_frame_base(void){
    return hmf_run==0?hmf_cap[6]:hmf_run==1?hdd_map_plain_frame:hmf_origin-8u;
}
/* A frame-valued word relative to the run's frame. */
static uint32_t hmf_alias_value,hmf_alias_call;static int hmf_alias_hit;
static uint32_t hmf_rel(uint32_t v,uint32_t f){
    if(!hmf_alias_hit&&stack_uncommitted(v)){hmf_alias_hit=1;hmf_alias_value=v;hmf_alias_call=hmf_calls-1u;}
    if(v==f-0xcu)return 0xffff000cu;
    if(v==f-0x20u)return 0xffff0020u;
    return v;
}
/* One callee side effect (about one call in four), drawn from the stream. */
static void hmf_effect(void){
    uint32_t pick=hmf_next();if(pick%4)return;
    BYTE *M=hmf_arena+HMF_M;
    switch(pick/4%16){
    case 0:put32(M+0x149,hmf_next()%2?hmf_now+hmf_next()%5-2u:hmf_value());break;
    case 1:M[0x11c+(hmf_next()%2)*(0x103-0x11c)]=(BYTE)(hmf_next()%2);break;
    case 2:put32(M+0x105,hmf_wingman());break;
    case 3:put32(M+0xc,hmf_next()%4);break;
    case 4:{BYTE *R=M+0x85+0x2a*(hmf_next()%3);switch(hmf_next()%3){case 0:R[0x20]=(BYTE)(hmf_next()%2);break;
        case 1:R[0x21]=(BYTE)(hmf_next()%2);break;default:put32(R+0x22,hmf_next()%2?1u:hmf_value());}break;}
    case 5:hmf_arena[HMF_ACT+0x100*(hmf_next()%4)+0x99]=(BYTE)(hmf_next()%2);break;
    case 6:mapped[hmf_next()%2?0x9d6ac:0x9d6ae]=(BYTE)(hmf_next()%2);break;
    case 7:M[hmf_next()%2?0x139:0x144]=(BYTE)(hmf_next()%2);break;
    case 8:put32(M+(hmf_next()%2?0x81:0x5d),hmf_mode());break;
    case 9:M[0x127+hmf_next()%18]=(BYTE)(hmf_next()%2);break;
    case 10:put32(mapped+0x9b0ac+4*(hmf_next()%4),hmf_value());break;
    case 11:if(hmf_next()%2)put32(mapped+0xd12a8+4*(hmf_next()%18),hmf_value());
        else put32(mapped+0xd12f0+4*(hmf_next()%18),HMF_ADDR(HMF_TXT+hmf_next()%32));break;
    case 12:put16(mapped+0xd3ba2,(uint16_t)hmf_value());break;
    case 13:put32(mapped+0x2c5f70,HMF_ADDR(hmf_next()%2?HMF_X1:HMF_X2));break;
    case 14:if(hmf_next()%2)put16(mapped+0x9d6b8,(uint16_t)hmf_value());else mapped[0x9d7e6]=(BYTE)(hmf_next()%2);break;
    default:if(hmf_next()%2)put32(hmf_arena+HMF_ARRAY+4*(hmf_next()%8),hmf_button());else put32(M+0x3d+4*(hmf_next()%8),hmf_button());break;
    }
}
static uint32_t hmf_hash(const BYTE *p,unsigned n){uint32_t h=2166136261u;for(unsigned i=0;i<n;++i)h=(h^p[i])*16777619u;return h;}
static uint32_t hmf_word(uint32_t a){return get32((const BYTE *)(uintptr_t)a);}
__attribute__((used,noinline)) static uint64_t __cdecl hmf_log_impl(unsigned id){
    if(hmf_calls==HMF_MAX){fail("map_frame synthetic call overflow");ExitProcess(1);}
    uint32_t *a=(uint32_t *)(uintptr_t)(hmf_cap[7]+4u);
    const uint32_t f=hmf_frame_base();
    hmf_record *v=hmf_log+hmf_calls++;memset(v,0,sizeof(*v));v->id=id;v->slots=weapons_bridge_slots(a,hmf_arity[id]);
    for(unsigned j=0;j<hmf_arity[id];++j)v->a[j]=hmf_rel(a[j],f);
    for(unsigned j=0;j<3;++j)v->regs[j]=hmf_rel(hmf_cap[j],f);
    if(id==16){v->a[0]=hmf_word(a[0]);v->a[1]=hmf_word(a[0]+4u);v->regs[1]=0;}   /* map_areas' local point */
    if(id==7&&a[1]==f-0x20u)for(unsigned j=0;j<4;++j)v->rect[j]=hmf_word(a[1]+4u*j);
    for(unsigned c=0;c<2;++c){
        const uint32_t p=get32(hmf_arena+(c?HMF_X2:HMF_X1)+0x22c);
        if(p==f-0xcu){v->ctx[c][0]=1;v->ctx[c][1]=hmf_word(p);v->ctx[c][3]=hmf_word(p+8u);
            if(hmf_run==2||id==11||id==12)v->ctx[c][2]=hmf_word(p+4u);}
        else v->ctx[c][1]=p;
    }
    if(hmf_track){for(unsigned j=0;j<3;++j)v->cs[j]=hmf_cap[3+j];
        v->frame[0]=hmf_cap[6]-hmf_origin;v->frame[1]=hmf_cap[7]-hmf_origin;v->window=hmf_hash(hmf_window,256);}
    uint32_t eax=hmf_value(),edx=hmf_value();hmf_ecx=hmf_value();
    if(id==1&&hmf_next()%4)eax=hmf_now;
    if(hmf_track&&hmf_arity[id]==1&&hmf_next()%4==0){a[0]=hmf_value();++hmf_clobbers;}   /* the callee changes its slot */
    hmf_effect();
    return (uint64_t)edx<<32|eax;
}
#define HMF_STUB(n) __attribute__((naked)) static void hmf_stub_##n(void){__asm__ volatile( \
    "movl %eax,_hmf_cap\n\tmovl %edx,_hmf_cap+4\n\tmovl %ecx,_hmf_cap+8\n\tmovl %ebx,_hmf_cap+12\n\t" \
    "movl %esi,_hmf_cap+16\n\tmovl %edi,_hmf_cap+20\n\tmovl %ebp,_hmf_cap+24\n\tmovl %esp,_hmf_cap+28\n\t" \
    "leal -256(%esp),%esi\n\tmovl $_hmf_window,%edi\n\tmovl $64,%ecx\n\tcld\n\trep movsl\n\t" \
    "movl _hmf_cap+16,%esi\n\tmovl _hmf_cap+20,%edi\n\t" \
    "pushl $" #n "\n\tcalll _hmf_log_impl\n\taddl $4,%esp\n\tmovl _hmf_ecx,%ecx\n\tret\n\t");}
HMF_STUB(0) HMF_STUB(1) HMF_STUB(2) HMF_STUB(3) HMF_STUB(4) HMF_STUB(5) HMF_STUB(6) HMF_STUB(7) HMF_STUB(8) HMF_STUB(9)
HMF_STUB(10) HMF_STUB(11) HMF_STUB(12) HMF_STUB(13) HMF_STUB(14) HMF_STUB(15) HMF_STUB(16)
#undef HMF_STUB
static void *hmf_stubs[HMF_IDS]={hmf_stub_0,hmf_stub_1,hmf_stub_2,hmf_stub_3,hmf_stub_4,hmf_stub_5,hmf_stub_6,hmf_stub_7,
    hmf_stub_8,hmf_stub_9,hmf_stub_10,hmf_stub_11,hmf_stub_12,hmf_stub_13,hmf_stub_14,hmf_stub_15,hmf_stub_16};
static int hmf_patch(BYTE saved[HMF_IDS][5],int active[HMF_IDS],unsigned which,int on){
    for(unsigned i=0;i<HMF_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(hmf_addr[i]-0x400000u,hmf_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(hmf_addr[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
/* Builds one state; a[0] = M. */
static void hmf_build(unsigned n,uint32_t a[6],uint32_t r[3]){
    hmf_seed=random32();hmf_stream=hmf_seed^n;random_bytes(hmf_arena,HMF_SIZE);stack_clean_bytes(hmf_arena,HMF_SIZE);memset(a,0,24);for(unsigned i=0;i<3;++i)r[i]=hmf_value();
    BYTE *M=hmf_arena+HMF_M;
    for(unsigned j=0;j<10;++j)put32(M+0x15+4*j,hmf_button());
    for(unsigned i=0;i<8;++i)put32(M+0x3d+4*i,hmf_button());
    put32(M+0x31,HMF_ADDR(HMF_ARRAY));for(unsigned i=0;i<8;++i)put32(hmf_arena+HMF_ARRAY+4*i,hmf_button());
    put32(hmf_arena+HMF_ARRAY-4,hmf_button());   /* [M+105h] -1 after 44f644 reads entry -1 */
    M[0x14d]=(BYTE)(hmf_next()%2?hmf_value()|1u:0u);
    hmf_now=hmf_value();if(hmf_next()%3==0)hmf_now=(hmf_now&~0x10u)|(hmf_next()%2?0x10u:0u);
    switch(hmf_next()%4){case 0:put32(M+0x149,hmf_now);break;case 1:put32(M+0x149,hmf_value());break;
        default:put32(M+0x149,hmf_now+hmf_next()%5-2u);}
    mapped[0x9d7e6]=(BYTE)(hmf_next()%2);put16(mapped+0x9d6b8,(uint16_t)(hmf_next()%2?hmf_next()%8:hmf_value()));
    put32(mapped+0x2c5f70,HMF_ADDR(hmf_next()%2?HMF_X1:HMF_X2));
    M[0x11c]=(BYTE)(hmf_next()%2?hmf_value()|1u:0u);M[0x103]=(BYTE)(hmf_next()%2?hmf_value()|1u:0u);put32(M+0x105,hmf_wingman());
    for(unsigned e=0;e<3;++e){BYTE *R=M+0x85+0x2a*e;
        put32(R+0x18,HMF_ADDR(HMF_ACT+0x100*(hmf_next()%4)));put32(R+0x1c,HMF_ADDR(HMF_ACT+0x100*(hmf_next()%4)));
        put32(R+0x22,hmf_next()%4?1u:hmf_value());R[0x20]=(BYTE)(hmf_next()%2);R[0x21]=(BYTE)(hmf_next()%2);}
    for(unsigned i=0;i<4;++i)hmf_arena[HMF_ACT+0x100*i+0x99]=(BYTE)(hmf_next()%2);
    switch(hmf_next()%6){case 0:put32(M+0xc,0);break;case 1:put32(M+0xc,1);break;case 2:put32(M+0xc,3);break;
        case 3:put32(M+0xc,hmf_value());break;default:put32(M+0xc,2);}
    mapped[0x9d6ac]=(BYTE)(hmf_next()%2);mapped[0x9d6ae]=(BYTE)(hmf_next()%2);
    M[0x139]=(BYTE)(hmf_next()%3==0);M[0x144]=(BYTE)(hmf_next()%2);
    put32(M+0x81,hmf_mode());put32(M+0x5d,hmf_next()%4==0?get32(M+0x81):hmf_mode());
    for(unsigned i=0;i<18;++i)M[0x127+i]=(BYTE)(hmf_next()%2);
    for(unsigned i=0;i<4;++i)put32(mapped+0x9b0ac+4*i,hmf_value());
    for(unsigned k=0;k<18;++k){put32(mapped+0xd12a8+4*k,hmf_value());put32(mapped+0xd12f0+4*k,HMF_ADDR(HMF_TXT+hmf_next()%32));}
    put16(mapped+0xd3ba2,(uint16_t)hmf_value());
    /* The real pass: map_areas' cursor block and holder, the label word,
     * the map holder 49d6b0 (also 44d370's) and the gadget M+8. */
    put32(mapped+0x9b088,HMF_ADDR(HMF_XB));put32(hmf_arena+HMF_XB+4,HMF_ADDR(HMF_CB));put32(mapped+0xd1638,hmf_value());
    put32(mapped+0x9d6b0,HMF_ADDR(HMF_M));put32(M+8,HMF_ADDR(HMF_GAD));
    {const uint32_t g=hmf_next()%3;put32(M+0x14e,g==0?get32(M+0x35):g==1?get32(M+0x39):hmf_value());}
    {const uint32_t b=get32(M+0x3d+4*(hmf_next()%8))-HMF_BASE+4u;if(b+16u<=HMF_SIZE){
        put32(M+0x152,get32(hmf_arena+b)+hmf_next()%3-1u+(hmf_next()%2?2:0));put32(M+0x156,get32(hmf_arena+b+4)+hmf_next()%3);}}
    M[0x15a]=(BYTE)(hmf_next()%2);M[0x15b]=(BYTE)(hmf_next()%2);M[0x104]=(BYTE)(hmf_next()%2);
    put16(M+0x13e,(uint16_t)(hmf_next()%3));
    a[0]=HMF_ADDR(HMF_M);
    hmf_stream=hmf_seed;   /* the stubs' stream restarts here in both runs */
}
/* [X+22Ch] relative to the run's frame (the arena compare). */
static void hmf_normalise(uint32_t f){
    for(unsigned c=0;c<2;++c){BYTE *p=hmf_arena+(c?HMF_X2:HMF_X1)+0x22c;if(get32(p)==f-0xcu)put32(p,0xffff000cu);}
}
static unsigned hmf_hits(unsigned id){unsigned h=0;for(unsigned j=0;j<hmf_calls;++j)if(hmf_log[j].id==id)++h;return h;}
static int hmf_counter_check(const callee_counter *c,uint64_t delta){
    uint64_t want=0;for(unsigned i=0;i<13;++i)if(hmf_addr[i]==c->key)want=hmf_hits(i);
    return delta!=want;
}
static int hmf_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const hmf_record *e,const BYTE *want,const BYTE *got){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,hmf_calls);
    for(unsigned j=0;j<calls&&j<hmf_calls&&e;++j)if(memcmp(e+j,hmf_log+j,sizeof(*e))){
        const uint32_t *x=(const uint32_t*)(e+j),*y=(const uint32_t*)(hmf_log+j);
        for(unsigned k=0;k<sizeof(*e)/4;++k)if(x[k]!=y[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,e[j].id,k,x[k],y[k]);break;}
    for(unsigned j=0;j<HMF_SIZE;++j)if(hmf_after[j]!=hmf_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,hmf_after[j],hmf_arena[j]);break;}
    for(unsigned j=0;j<HMF_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);break;}
    return 1;
}
static int hmf_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[HMF_GLOBALS],after[HMF_GLOBALS],actual[HMF_GLOBALS];static hmf_record expected[HMF_MAX];
    memcpy(hmf_before,hmf_arena,HMF_SIZE);hmf_globals(before,0);hmf_alias_hit=0;
    hmf_run=0;hmf_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);hmf_normalise(hmf_cap[6]);
    unsigned calls=hmf_calls;memcpy(expected,hmf_log,calls*sizeof(*expected));memcpy(hmf_after,hmf_arena,HMF_SIZE);hmf_globals(after,0);
    for(unsigned j=0;j<calls;++j)hmf_paths[expected[j].id]++;
    memcpy(hmf_arena,hmf_before,HMF_SIZE);hmf_globals(before,1);hmf_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16]={0};
    for(unsigned i=0;i<count&&i<16;++i)counts[i]=c[i].count;
    hmf_run=1;pair[1]=x->replacement_context(a,d->user,r);hmf_normalise(hdd_map_plain_frame);hmf_globals(actual,0);
    if(hmf_alias_hit){fprintf(stderr,"FAIL: %s case %u call %u: data word %08x lies in the worker stack reservation\n",current_function,n,hmf_alias_call,hmf_alias_value);return 1;}
    if(n<4096)for(unsigned i=0;i<count&&i<16;++i)if(hmf_counter_check(c+i,c[i].count-counts[i]))return fail("map_frame callee counter delta");
    if(pair[0]!=pair[1]||calls!=hmf_calls||memcmp(expected,hmf_log,calls*sizeof(*expected))||memcmp(hmf_after,hmf_arena,HMF_SIZE)||
       memcmp(after,actual,HMF_GLOBALS))
        return hmf_report("direct",n,pair,calls,expected,after,actual);
    hmf_globals(before,1);
    return 0;
}
/* Calls target with one slot over a 4-KiB fill, EAX..EDI distinct pattern
 * words; keeps the slot address in hmf_origin and returns EDX:EAX in out. */
__attribute__((naked,noinline)) static void hmf_track_run(void *target,const uint32_t *a,uint32_t pattern,uint32_t out[2]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 16(%ebp),%eax\n\tmovl $1024,%ecx\n\tcld\n\trep stosl\n\taddl $4092,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl (%esi)\n\tmovl %esp,_hmf_origin\n\tmovl 16(%ebp),%eax\n\t"
        "movl %eax,%edx\n\txorl $0x01010101,%edx\n\tmovl %eax,%ecx\n\txorl $0x02020202,%ecx\n\tmovl %eax,%ebx\n\txorl $0x04040404,%ebx\n\t"
        "movl %eax,%esi\n\txorl $0x08080808,%esi\n\tmovl %eax,%edi\n\txorl $0x10101010,%edi\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%ebx\n\tmovl %eax,(%ebx)\n\tmovl %edx,4(%ebx)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* Installed probes through the entry shim: the original 44c988 and the
 * installed replacement must agree in EDX:EAX, the records (with
 * EBX/ESI/EDI, EBP/ESP relative to the slot, the residue hash below each
 * callee's ESP and the record behind [X+22Ch] with its residue word), the
 * arena (the stored frame pointer relative to the slot) and the globals. */
static int hmf_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    hmf_track=1;hmf_clobbers=0;hmf_run=2;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],want[2],got[2],pattern=poison?0xa7a7a7a7u:0x31313131u;hmf_build(n*41u+3u,a,r);
        BYTE before[HMF_GLOBALS],after[HMF_GLOBALS],actual[HMF_GLOBALS],expected_fpu[108],actual_fpu[108];static hmf_record expected[HMF_MAX];
        memcpy(hmf_before,hmf_arena,HMF_SIZE);hmf_globals(before,0);
        hmf_reset();hmf_track_run(mapped+d->rva,a,pattern,want);es2_hook_fpu_save(expected_fpu);hmf_normalise(hmf_origin-8u);
        unsigned calls=hmf_calls;memcpy(expected,hmf_log,calls*sizeof(*expected));memcpy(hmf_after,hmf_arena,HMF_SIZE);hmf_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("map_frame poison install");
        memcpy(hmf_arena,hmf_before,HMF_SIZE);hmf_globals(before,1);hmf_reset();
        hmf_track_run(mapped+d->rva,a,pattern,got);es2_hook_fpu_save(actual_fpu);hmf_normalise(hmf_origin-8u);hmf_globals(actual,0);
        uint64_t pair[2]={(uint64_t)want[1]<<32|want[0],(uint64_t)got[1]<<32|got[0]};
        if(pair[0]!=pair[1]||calls!=hmf_calls||memcmp(expected,hmf_log,calls*sizeof(*expected))||memcmp(hmf_after,hmf_arena,HMF_SIZE)||
           memcmp(after,actual,HMF_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=hmf_report(fast?"installed fast":"installed preserving",n,pair,calls,expected,after,actual);
        if(!es2_hook_uninstall(&h))return fail("map_frame poison uninstall");hmf_globals(before,1);++total;
    }
    hmf_track=0;hmf_run=0;
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status&&!hmf_clobbers){fprintf(stderr,"FAIL: map_frame installed probes: no changed argument slot was observed\n");status=1;}
    if(!status)printf("PASS: %s %u installed cases through the entry shim, fast/preserving dispatch, 31/a7 stack, per-call EBX/ESI/EDI, EBP/ESP, residue below each callee and behind [X+22Ch], changed argument slots (%u in both runs), nonempty x87, full pair/calls/arena/globals\n",
        d->name,total,hmf_clobbers);
    return status;
}
/* Real-callee pass: 44c988 over the real tick 44d370 and the real map_areas
 * 44d450 (its four callees stubbed), map_frame and map_areas installed
 * original/replaced in all four combinations, against the all-original
 * run; the frame values are compared relative to the slot. */
static int hmf_real(unsigned index){
    const unsigned set[2]={index,ES2_HOOK_INDEX_map_areas};
    BYTE saved[HMF_IDS][5];int active[HMF_IDS];if(hmf_patch(saved,active,HMF_REAL,1))return 1;
    int status=0;unsigned total=0,ticks=0;uint64_t entered=0,nested=0;
    hmf_run=2;
    for(unsigned mode=0;mode<4&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],want[2],got[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;hmf_build(n*67u+mode*7u,a,r);
        BYTE before[HMF_GLOBALS],after[HMF_GLOBALS],actual[HMF_GLOBALS];static hmf_record expected[HMF_MAX];
        memcpy(hmf_before,hmf_arena,HMF_SIZE);hmf_globals(before,0);
        hmf_reset();hmf_track_run(mapped+function_hook_descriptor(index)->rva,a,pattern,want);hmf_normalise(hmf_origin-8u);
        unsigned calls=hmf_calls;memcpy(expected,hmf_log,calls*sizeof(*expected));memcpy(hmf_after,hmf_arena,HMF_SIZE);hmf_globals(after,0);
        es2_hook hk[2];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<2;++j){o.mode=(mode>>j&1)?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(set[j]),&o,function_hook_extension(set[j])))return fail("map_frame real install");}
        memcpy(hmf_arena,hmf_before,HMF_SIZE);hmf_globals(before,1);hmf_reset();
        hmf_track_run(mapped+function_hook_descriptor(index)->rva,a,pattern,got);hmf_normalise(hmf_origin-8u);hmf_globals(actual,0);
        uint64_t pair[2]={(uint64_t)want[1]<<32|want[0],(uint64_t)got[1]<<32|got[0]};
        if(pair[0]!=pair[1]||calls!=hmf_calls||memcmp(expected,hmf_log,calls*sizeof(*expected))||memcmp(hmf_after,hmf_arena,HMF_SIZE)||
           memcmp(after,actual,HMF_GLOBALS)){
            fprintf(stderr,"real mode=%u\n",mode);status=hmf_report("real",n,pair,calls,expected,after,actual);}
        if(mode&1)entered+=hk[0].calls;
        if(mode&2)nested+=hk[1].calls;
        if(get32(hmf_before+HMF_M+0x149)<hmf_now)++ticks;
        for(unsigned j=0;j<2;++j)if(!es2_hook_uninstall(hk+j))return fail("map_frame real uninstall");
        hmf_globals(before,1);++total;
    }
    hmf_track=0;hmf_run=0;
    if(hmf_patch(saved,active,0,0))return 1;
    if(!status&&(!entered||!nested||!ticks)){fprintf(stderr,"FAIL: %s real pass: entered %llu nested %llu ticks %u\n",current_function,
        (unsigned long long)entered,(unsigned long long)nested,ticks);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases over the real 44d370 and map_areas (map_frame and map_areas original/replaced in all 4 combinations; %llu replaced entries, %llu replaced map_areas entries), 31/a7 stack, pair/calls/arena/globals\n",
        current_function,total,(unsigned long long)entered,(unsigned long long)nested);
    return status;
}
static int hdd_map_cases(unsigned index,unsigned limit){
    BYTE old_globals[HMF_GLOBALS];hmf_globals(old_globals,0);
    BYTE saved[HMF_IDS][5];int active[HMF_IDS];
    if(hmf_patch(saved,active,HMF_MAIN,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_HDD_MAP_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);memset(hmf_paths,0,sizeof(hmf_paths));hmf_track=0;
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];hmf_build(n,a,r);status=hmf_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);current_case_valid=0;
    hmf_globals(old_globals,1);
    if(!limit&&!status)status=hmf_installed_probes(index,256);
    hmf_globals(old_globals,1);
    if(hmf_patch(saved,active,HMF_MAIN,0))return 1;
    if(!limit&&!status)status=hmf_real(index);
    hmf_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases (stub calls 44d450 %u, 467828 %u, 44d370 %u, 44d6e0 %u, 44d82c %u, 44f644 %u, 44de14 %u, 43122c %u, 438aac %u, 44dd74 %u, 44dd14 %u, 486704 %u, 44e334 %u); full return pair, ordered calls with entry registers, the record behind [X+22Ch], arena and globals\n",
        function_hook_descriptor(index)->name,cases,hmf_paths[0],hmf_paths[1],hmf_paths[2],hmf_paths[3],hmf_paths[4],hmf_paths[5],hmf_paths[6],
        hmf_paths[7],hmf_paths[8],hmf_paths[9],hmf_paths[10],hmf_paths[11],hmf_paths[12]);
    return status;
}
