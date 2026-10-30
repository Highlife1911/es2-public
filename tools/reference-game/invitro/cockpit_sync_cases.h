/* cockpit_sync group: the cockpit<->player synchronisation 41b130
 * (cockpit_sync), the throttle record getter 447df8 (throttle_record), the
 * throttle gauge set 447da8 (throttle_gauge) and the HUD record getter 43dbb0
 * (hud_record) (docs/reverse-engineering/hud.md, "player
 * synchronisation as own code").
 *
 * An 8 KiB arena holds the vehicle P (and a second vehicle O used as the
 * view subject or as the player of 4D24E6h), two cockpits, the
 * EnergyPoolGauge, two ThrottleGauges with their sliders S and class
 * tables K (slot +14h = a slot stub), two HUD objects, three targets, two
 * weapon systems, two chassis records, two player weapons and a record
 * area; the image globals 4A9CE8h, 49B088h, 49EF34h, 4D24E6h, 4D2684h and
 * the words 4D2504h, 4D3B64h are set per case, compared and restored.
 *
 * cockpit_sync: its twelve callees (4450f8, shield_balance, dist_gate,
 * weapon_frame, 447df8, 447da8, 43dbb0, 40e484, timer_word_step, 43d9b4,
 * aim_point, 434a24) are logging stubs patched into the private image:
 * ordered calls with the bridge's slot count, arguments, entry EAX/EDX/ECX
 * and EBX/ESI/EDI, a hash of the record behind a pointer argument (447da8:
 * 8 bytes, 43d9b4: 26h, 434a24: 12); results from a per-case stream (the
 * getters return G+0B1h or another record, the countdown AX = 0 in half,
 * aim_point writes the aim point and its words); in one call of four a
 * side effect on the vehicle, the cockpit pointer and fields, the
 * targets, the weapon and chassis records, the globals or a frame byte,
 * so a replacement that caches a value the original reads again differs.
 * The first stub fills the frame EBP-40h..EBP-1 from the stream (direct
 * mode), so bytes the original leaves unwritten are equal and nonuniform
 * in both runs. Direct cases run the original body (EBX = ESI = EDI = 0;
 * the adapter's plain path gets 0 as the caller's ESI/EDI) and the
 * adapter's plain path and compare EDX:EAX, the calls (frame addresses as
 * offsets from the callee's EBP), the arena, the globals and (first 4,096
 * cases) the callee counters. Installed probes run the hook through its
 * entry shim (fast and preserving dispatch, a position-dependent stack
 * fill, EAX..EDI distinct patterns): raw frame addresses, every stub's
 * EBX/ESI/EDI, EBP and ESP relative to the caller's slot and a hash of the
 * 256 bytes below its ESP; one-slot stubs change their argument slot in
 * one call of four. The real-callee pass runs 41b130 over the real 4450f8,
 * dist_gate, 447df8, 447da8 (slider slot stubs), 43dbb0, 40e484,
 * timer_word_step and 434a24 with cockpit_sync and its three helpers
 * original/replaced in all 16 combinations (dist_gate and timer_word_step
 * replaced in odd cases) against the all-original run.
 *
 * throttle_gauge: the slider slot +14h is a stub reached through the
 * arena's class tables (side effects on G+0B1h, G+18h and the record L);
 * L is a separate record or overlaps the gauge or the slider; direct,
 * installed 31/a7 cases. throttle_record and hud_record: direct cases over
 * the whole argument range, installed 31/a7 cases and the compare cases of
 * compare_probes.h. Identifiers: csy_/CSY_. */
enum { CSY_SIZE=0x2000, CSY_P=0x100, CSY_OTHER=0x480, CSY_H1=0x800, CSY_H2=0xb00, CSY_G1=0xe00,
    CSY_TG1=0xf00, CSY_TG2=0x1000, CSY_HUD1=0x1100, CSY_HUD2=0x1200, CSY_T1=0x1300, CSY_T2=0x14c0, CSY_T3=0x1680,
    CSY_W1=0x1840, CSY_W2=0x1880, CSY_C1=0x18c0, CSY_C2=0x1920, CSY_PW1=0x1980, CSY_PW2=0x1a00,
    CSY_S1=0x1a80, CSY_S2=0x1ac0, CSY_K1=0x1b00, CSY_K2=0x1b20, CSY_REC=0x1b40, CSY_L=0x1c80,
    CSY_IDS=14, CSY_MAX=32, CSY_GLOBALS=24 };
#define CSY_BASE ((uint32_t)(uintptr_t)csy_arena)
#define CSY_ADDR(o) (CSY_BASE+(uint32_t)(o))
static BYTE csy_arena[CSY_SIZE],csy_before[CSY_SIZE],csy_after[CSY_SIZE];
typedef struct csy_record { uint32_t id,slots,a[4],regs[3],cs[3],hash,frame[2],window; } csy_record;
static csy_record csy_log[CSY_MAX];
static unsigned csy_calls,csy_track,csy_fill,csy_clobbers,csy_paths[CSY_IDS];
/* Real pass with dist_gate replaced: weapon_frame's entry ECX is the one
 * dist_gate returns, a frame address in the original and the entry ECX
 * under the hook (dead at weapon_frame's callees), so it is not logged. */
static unsigned csy_mask_ecx3;
static uint32_t csy_seed,csy_stream;
/* Captured by the stubs at entry: EAX, EDX, ECX, EBX, ESI, EDI, EBP, ESP;
 * the 256 bytes below the entry ESP; the ECX a stub returns; the caller's
 * slot address of a tracked run. */
__attribute__((used)) static uint32_t csy_cap[8],csy_ecx,csy_origin;
__attribute__((used)) static BYTE csy_window[256];
/* ids 0..11 the patched callees of 41b130; 12/13 the slider slot +14h of
 * the class tables K1/K2. */
static const uint32_t csy_addr[CSY_IDS]={0x4450f8,0x413bc8,0x49288c,0x410b40,0x447df8,0x447da8,0x43dbb0,0x40e484,
    0x467a48,0x43d9b4,0x41b728,0x434a24,0,0};
static const unsigned csy_arity[CSY_IDS]={2,1,2,2,1,2,1,2,1,2,4,4,2,2};
/* Logged positions (EAX, EDX, ECX, a0..a3 = bits 0..6) that hold frame
 * addresses: 447da8 &L, 43d9b4 ECX and &R, aim_point EAX/EDX/ECX and its
 * three pointers, 434a24 EDX and &A. */
static const unsigned csy_frame_mask[CSY_IDS]={0,0,0,0,0,1u<<4,0,0,0,(1u<<2)|(1u<<4),0x77,(1u<<1)|(1u<<4),0,0};
static uint32_t csy_next(void){
    uint32_t z=csy_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static void csy_reset(void){csy_calls=0;csy_stream=csy_seed;}
static void csy_globals(BYTE *out,int restore){
    static const struct { uint32_t rva,size; } g[]={{0xa9ce8,4},{0x9b088,4},{0x9ef34,4},{0xd24e6,4},{0xd2684,4},{0xd2504,2},{0xd3b64,2}};
    BYTE *p=out;for(unsigned i=0;i<7;++i){if(restore)memcpy(mapped+g[i].rva,p,g[i].size);else memcpy(p,mapped+g[i].rva,g[i].size);p+=g[i].size;}
}
static uint32_t csy_hash(const BYTE *p,unsigned n){uint32_t h=2166136261u;for(unsigned i=0;i<n;++i)h=(h^p[i])*16777619u;return h;}
static BYTE *csy_host(uint32_t address,uint32_t n){return address-CSY_BASE<=CSY_SIZE-n?csy_arena+(address-CSY_BASE):NULL;}
static uint32_t csy_target(void){switch(csy_next()%4){case 0:return 0;case 1:return CSY_ADDR(CSY_T1);case 2:return CSY_ADDR(CSY_T2);default:return CSY_ADDR(CSY_T3);}}
static uint32_t csy_weapon(void){switch(csy_next()%3){case 0:return 0;case 1:return CSY_ADDR(CSY_PW1);default:return CSY_ADDR(CSY_PW2);}}
/* A nonzero chassis speed limit word (the original faults on zero). */
static uint16_t csy_limit(void){uint16_t v=(uint16_t)(csy_next()%4==0?csy_next():csy_next()%2000+1);return v?v:(uint16_t)0x8000u;}
/* One side effect of a 41b130 callee (about one call in four). */
static void csy_effect(uint32_t ebp){
    const uint32_t pick=csy_next();if(pick%4)return;
    BYTE *P=csy_arena+CSY_P,*H=csy_host(get32(mapped+0x9b088),0x300);
    switch(pick/4%13){
    case 0:put32(P+0x1a4,csy_target());break;
    case 1:if(H)put32(H+0x210,csy_target());break;
    case 2:put32(mapped+0x9b088,CSY_ADDR(csy_next()%2?CSY_H1:CSY_H2));break;
    case 3:if(H)switch(csy_next()%3){
        case 0:put32(H+0x1f1,CSY_ADDR(csy_next()%2?CSY_TG1:CSY_TG2));break;
        case 1:put32(H+0x1f5,CSY_ADDR(csy_next()%2?CSY_HUD1:CSY_HUD2));break;
        default:put32(H+0x1e5,CSY_ADDR(CSY_G1));break;}
        break;
    case 4:if(csy_next()%2)put32(P+0x202,CSY_ADDR(csy_next()%2?CSY_W1:CSY_W2));
        else csy_arena[(csy_next()%2?CSY_W1:CSY_W2)+0x14]=(BYTE)(csy_next()%2?0u:csy_next()|1u);
        break;
    case 5:if(csy_next()%2)put32(P+0x1f2,CSY_ADDR(csy_next()%2?CSY_C1:CSY_C2));
        else put16(csy_arena+(csy_next()%2?CSY_C1:CSY_C2)+0x50,(uint16_t)(csy_next()%2?0u:csy_next()));
        break;
    case 6:{static const uint16_t o[3]={0x93,0x9b,0xa4};P[o[csy_next()%3]]=(BYTE)(csy_next()%2?0u:csy_next());break;}
    case 7:{static const uint16_t o[7]={0x28e,0x290,0x292,0x298,0x29a,0x10,0x31c};put16(P+o[csy_next()%7],(uint16_t)csy_next());break;}
    case 8:put32(P+0x30b,csy_weapon());break;
    case 9:switch(csy_next()%4){
        case 0:put32(mapped+0x9ef34,csy_next()%2?0u:csy_next());break;
        case 1:put32(mapped+0xd24e6,CSY_ADDR(csy_next()%2?CSY_P:CSY_OTHER));break;
        case 2:put32(mapped+0xd2684,CSY_ADDR(csy_next()%2?CSY_OTHER:CSY_P));break;
        default:put16(mapped+0xd2504,(uint16_t)csy_next());break;}
        break;
    case 10:((BYTE *)(uintptr_t)ebp)[-1-(int)(csy_next()%0x40)]=(BYTE)csy_next();break;
    case 11:put32(csy_arena+(csy_next()%2?CSY_TG1:CSY_TG2)+0xb1+4*(csy_next()%2),csy_next());break;
    default:csy_arena[(csy_next()%2?CSY_HUD1:CSY_HUD2)+0xb1+csy_next()%0x26]=(BYTE)csy_next();break;
    }
}
/* The record L of a throttle_gauge case (0 in other runs). */
static uint32_t csy_l;
/* One side effect of the slider slot (about one call in four): the gauge
 * ratio, the gauge's slider, the record L. */
static void csy_effect_slot(void){
    const uint32_t pick=csy_next();if(pick%4)return;
    BYTE *g=csy_arena+(csy_next()%2?CSY_TG1:CSY_TG2);
    switch(pick/4%4){
    case 0:put32(g+0xb1,csy_next());break;
    case 1:put32(g+0x18,CSY_ADDR(csy_next()%2?CSY_S1:CSY_S2));break;
    case 2:if(csy_l)put32((BYTE *)(uintptr_t)csy_l,csy_next());break;
    default:if(csy_l)put32((BYTE *)(uintptr_t)csy_l+4,csy_next());break;
    }
}
static uint32_t csy_norm(unsigned id,unsigned position,uint32_t v,uint32_t ebp){
    return !csy_track&&(csy_frame_mask[id]>>position&1u)?v-ebp:v;
}
__attribute__((used,noinline)) static uint64_t __cdecl csy_log_impl(unsigned id){
    if(csy_calls==CSY_MAX){fail("cockpit_sync synthetic call overflow");ExitProcess(1);}
    uint32_t *a=(uint32_t *)(uintptr_t)(csy_cap[7]+4u);const uint32_t ebp=csy_cap[6];
    csy_record *v=csy_log+csy_calls++;memset(v,0,sizeof(*v));v->id=id;v->slots=weapons_bridge_slots(a,csy_arity[id]);
    for(unsigned j=0;j<csy_arity[id];++j)v->a[j]=csy_norm(id,3+j,a[j],ebp);
    for(unsigned j=0;j<3;++j)v->regs[j]=csy_norm(id,j,csy_cap[j],ebp);
    if(id<12)for(unsigned j=0;j<3;++j)v->cs[j]=csy_cap[3+j];
    if(id==3&&csy_mask_ecx3)v->regs[2]=0;
    if(id==5)v->hash=csy_hash((const BYTE *)(uintptr_t)a[1],8);
    if(id==9)v->hash=csy_hash((const BYTE *)(uintptr_t)a[1],0x26);
    if(id==11)v->hash=csy_hash((const BYTE *)(uintptr_t)a[1],12);
    if(csy_track){v->frame[0]=ebp-csy_origin;v->frame[1]=csy_cap[7]-csy_origin;v->window=csy_hash(csy_window,256);}
    /* The first callee leaves the frame bytes of this run (direct mode). */
    if(id==0&&csy_fill)for(unsigned i=1;i<=0x40;++i)((BYTE *)(uintptr_t)ebp)[-(int)i]=(BYTE)csy_next();
    uint32_t eax=csy_next(),edx=csy_next();const uint32_t x=csy_next();csy_ecx=csy_next();
    switch(id){
    case 4:case 6:eax=x%4?a[0]+0xb1u:CSY_ADDR(CSY_REC+8u*(x>>2&7u));break;   /* the record (or another one) */
    case 8:if(x&1)eax&=0xffff0000u;break;                                     /* the countdown ran out */
    case 10:                                                                  /* aim_point's outputs */
        if(x%4){BYTE *o=(BYTE *)(uintptr_t)a[1];for(unsigned i=0;i<12;++i)o[i]=(BYTE)csy_next();}
        if((x>>2)%4)put16((BYTE *)(uintptr_t)a[2],(uint16_t)((x>>4)%2?0u:csy_next()));
        if((x>>5)%4==0)put16((BYTE *)(uintptr_t)a[3],(uint16_t)csy_next());
        break;
    default:break;
    }
    if(csy_track&&csy_arity[id]==1&&csy_next()%4==0){a[0]=csy_next();++csy_clobbers;}   /* the callee changes its slot */
    if(id<12)csy_effect(ebp);else csy_effect_slot();
    return (uint64_t)edx<<32|eax;
}
#define CSY_STUB(n) __attribute__((naked)) static void csy_stub_##n(void){__asm__ volatile( \
    "movl %eax,_csy_cap\n\tmovl %edx,_csy_cap+4\n\tmovl %ecx,_csy_cap+8\n\tmovl %ebx,_csy_cap+12\n\t" \
    "movl %esi,_csy_cap+16\n\tmovl %edi,_csy_cap+20\n\tmovl %ebp,_csy_cap+24\n\tmovl %esp,_csy_cap+28\n\t" \
    "leal -256(%esp),%esi\n\tmovl $_csy_window,%edi\n\tmovl $64,%ecx\n\tcld\n\trep movsl\n\t" \
    "movl _csy_cap+16,%esi\n\tmovl _csy_cap+20,%edi\n\t" \
    "pushl $" #n "\n\tcalll _csy_log_impl\n\taddl $4,%esp\n\tmovl _csy_ecx,%ecx\n\tret\n\t");}
CSY_STUB(0) CSY_STUB(1) CSY_STUB(2) CSY_STUB(3) CSY_STUB(4) CSY_STUB(5) CSY_STUB(6) CSY_STUB(7) CSY_STUB(8) CSY_STUB(9)
CSY_STUB(10) CSY_STUB(11) CSY_STUB(12) CSY_STUB(13)
#undef CSY_STUB
static void *csy_stubs[CSY_IDS]={csy_stub_0,csy_stub_1,csy_stub_2,csy_stub_3,csy_stub_4,csy_stub_5,csy_stub_6,csy_stub_7,
    csy_stub_8,csy_stub_9,csy_stub_10,csy_stub_11,csy_stub_12,csy_stub_13};
#define CSY_ALL 0xfffu
#define CSY_REAL ((1u<<1)|(1u<<3)|(1u<<9)|(1u<<10))
static int csy_patch(BYTE saved[CSY_IDS][5],int active[CSY_IDS],unsigned which,int on){
    for(unsigned i=0;i<CSY_IDS;++i){
        if(!csy_addr[i]){active[i]=0;continue;}
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(csy_addr[i]-0x400000u,csy_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(csy_addr[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
/* The world of one case: the arena from the case stream, then every
 * pointer the function and the real callees follow; a[0] = P (cockpit_sync)
 * or G, L (throttle_gauge); r the entry EAX/EDX/ECX. */
static void csy_world(void){
    for(unsigned i=0;i<CSY_SIZE;i+=4)put32(csy_arena+i,csy_next());
    BYTE *P=csy_arena+CSY_P;
    static const uint32_t hs[2]={CSY_H1,CSY_H2};
    for(unsigned k=0;k<2;++k){BYTE *H=csy_arena+hs[k];
        put32(H+0x1e5,CSY_ADDR(CSY_G1));put32(H+0x1f1,CSY_ADDR(csy_next()%2?CSY_TG1:CSY_TG2));
        put32(H+0x1f5,CSY_ADDR(csy_next()%2?CSY_HUD1:CSY_HUD2));put32(H+0x210,csy_target());}
    static const uint32_t tg[2]={CSY_TG1,CSY_TG2},sl[2]={CSY_S1,CSY_S2},kt[2]={CSY_K1,CSY_K2};
    for(unsigned k=0;k<2;++k){
        put32(csy_arena+tg[k]+0x18,CSY_ADDR(sl[csy_next()%2]));put32(csy_arena+sl[k]+0x17,CSY_ADDR(kt[k]));
        put32(csy_arena+kt[k]+0x14,(uint32_t)(uintptr_t)csy_stubs[12+k]);
        if(csy_next()%3==0)put32(csy_arena+tg[k]+0xb5,get32(csy_arena+tg[k]+0xb1));}
    static const uint32_t ts[3]={CSY_T1,CSY_T2,CSY_T3};
    for(unsigned k=0;k<3;++k)put16(csy_arena+ts[k]+0x1a8,(uint16_t)(csy_next()%2?0u:csy_next()));
    for(unsigned k=0;k<2;++k){BYTE *c=csy_arena+(k?CSY_C2:CSY_C1);put16(c+4,csy_limit());put16(c+6,csy_limit());
        put16(c+0x50,(uint16_t)(csy_next()%2?0u:csy_next()));
        csy_arena[(k?CSY_W2:CSY_W1)+0x14]=(BYTE)(csy_next()%2?0u:csy_next()|1u);}
    put32(P+0x1a4,csy_target());put32(P+0x1f2,CSY_ADDR(csy_next()%2?CSY_C1:CSY_C2));
    put32(P+0x202,CSY_ADDR(csy_next()%2?CSY_W1:CSY_W2));put32(P+0x30b,csy_weapon());
    P[0x93]=(BYTE)(csy_next()%2?0u:csy_next()|1u);P[0xa4]=(BYTE)(csy_next()%2?0u:csy_next()|1u);
    const uint32_t speed=csy_next();
    put16(P+0x28e,(uint16_t)(speed%8==0?0u:speed%8==1?0x8000u:speed%8==2?0x7fffu:speed>>3));
    /* The cockpit target equals the vehicle's in about half of the cases. */
    const uint32_t H=CSY_ADDR(csy_next()%4?CSY_H1:CSY_H2);
    if(csy_next()%2)put32(csy_arena+(H-CSY_BASE)+0x210,get32(P+0x1a4));
    put32(mapped+0xa9ce8,csy_next());put32(mapped+0x9b088,H);put32(mapped+0x9ef34,csy_next()%2?0u:csy_next()|1u);
    put32(mapped+0xd24e6,CSY_ADDR(csy_next()%3?CSY_P:CSY_OTHER));put32(mapped+0xd2684,CSY_ADDR(csy_next()%2?CSY_OTHER:CSY_P));
    put16(mapped+0xd2504,(uint16_t)csy_next());put16(mapped+0xd3b64,(uint16_t)(csy_next()%64));
}
static void csy_build(unsigned n,uint32_t a[6],uint32_t r[3]){
    csy_seed=random32();csy_stream=csy_seed^n;csy_l=0;memset(a,0,24);
    csy_world();for(unsigned i=0;i<3;++i)r[i]=csy_next();
    a[0]=CSY_ADDR(CSY_P);
    csy_seed=csy_stream;   /* the stubs' stream restarts here in both runs */
}
static unsigned csy_hits(unsigned lo,unsigned hi){
    unsigned h=0;for(unsigned j=0;j<csy_calls;++j)if(csy_log[j].id>=lo&&csy_log[j].id<=hi)++h;
    return h;
}
static int csy_counter_check(const callee_counter *c,uint64_t delta){
    uint64_t want=0;
    if(c->event==ES2_COLLIDE_SLOT)want=c->key==0x14?csy_hits(12,13):0;
    else for(unsigned i=0;i<12;++i)if(csy_addr[i]==c->key)want=csy_hits(i,i);
    return delta!=want;
}
static int csy_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const csy_record *e,const BYTE *want,const BYTE *got){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,csy_calls);
    for(unsigned j=0;j<calls&&j<csy_calls&&e;++j)if(memcmp(e+j,csy_log+j,sizeof(*e))){
        const uint32_t *x=(const uint32_t*)(e+j),*y=(const uint32_t*)(csy_log+j);
        for(unsigned k=0;k<sizeof(*e)/4;++k)if(x[k]!=y[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,e[j].id,k,x[k],y[k]);break;}
    for(unsigned j=0;j<CSY_SIZE;++j)if(csy_after[j]!=csy_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,csy_after[j],csy_arena[j]);break;}
    for(unsigned j=0;want&&j<CSY_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);break;}
    return 1;
}
/* Calls target with two slots (a[0], a[1]), EAX/EDX/ECX = r[0..2] and
 * EBX = ESI = EDI = 0; EDX:EAX into out. */
__attribute__((naked,noinline)) static void csy_run(void *target,const uint32_t *a,const uint32_t *r,uint32_t out[2]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl 12(%ebp),%eax\n\tpushl 4(%eax)\n\tpushl (%eax)\n\t"
        "movl 16(%ebp),%eax\n\tmovl 4(%eax),%edx\n\tmovl 8(%eax),%ecx\n\tmovl (%eax),%eax\n\t"
        "xorl %ebx,%ebx\n\txorl %esi,%esi\n\txorl %edi,%edi\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%ebx\n\tmovl %eax,(%ebx)\n\tmovl %edx,4(%ebx)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* Direct case of cockpit_sync or throttle_gauge: the original body (or,
 * in the installed pass, the hooked entry) against the adapter. */
static int csy_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[CSY_GLOBALS],after[CSY_GLOBALS],actual[CSY_GLOBALS];static csy_record expected[CSY_MAX];uint32_t want[2];
    memcpy(csy_before,csy_arena,CSY_SIZE);csy_globals(before,0);
    csy_reset();csy_run(mapped+d->rva,a,r,want);
    const unsigned calls=csy_calls;memcpy(expected,csy_log,calls*sizeof(*expected));memcpy(csy_after,csy_arena,CSY_SIZE);csy_globals(after,0);
    for(unsigned j=0;j<calls;++j)csy_paths[expected[j].id]++;
    memcpy(csy_arena,csy_before,CSY_SIZE);csy_globals(before,1);csy_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16]={0};
    for(unsigned i=0;i<count&&i<16;++i)counts[i]=c[i].count;
    uint64_t pair[2]={(uint64_t)want[1]<<32|want[0],x->replacement_context(a,d->user,r)};csy_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count&&i<16;++i)if(csy_counter_check(c+i,c[i].count-counts[i]))return fail("cockpit_sync callee counter delta");
    if(pair[0]!=pair[1]||calls!=csy_calls||memcmp(expected,csy_log,calls*sizeof(*expected))||memcmp(csy_after,csy_arena,CSY_SIZE)||
       memcmp(after,actual,CSY_GLOBALS))
        return csy_report("direct",n,pair,calls,expected,after,actual);
    csy_globals(before,1);
    return 0;
}
/* Calls target with one slot over a 4-KiB position-dependent fill (each
 * dword the pattern plus k times 9E3779B9h), EAX..EDI distinct pattern
 * words; keeps the slot address in csy_origin and EDX:EAX in out. */
__attribute__((naked,noinline)) static void csy_track_run(void *target,const uint32_t *a,uint32_t pattern,uint32_t out[2]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 16(%ebp),%eax\n\tmovl $1024,%ecx\n\tcld\n\t"
        "1: stosl\n\taddl $0x9e3779b9,%eax\n\tdecl %ecx\n\tjnz 1b\n\taddl $4092,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl (%esi)\n\tmovl %esp,_csy_origin\n\tmovl 16(%ebp),%eax\n\t"
        "movl %eax,%edx\n\txorl $0x01010101,%edx\n\tmovl %eax,%ecx\n\txorl $0x02020202,%ecx\n\tmovl %eax,%ebx\n\txorl $0x04040404,%ebx\n\t"
        "movl %eax,%esi\n\txorl $0x08080808,%esi\n\tmovl %eax,%edi\n\txorl $0x10101010,%edi\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%ebx\n\tmovl %eax,(%ebx)\n\tmovl %edx,4(%ebx)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* Installed probes through the entry shim: the original 41b130 and the
 * installed replacement must agree in EDX:EAX, the records (raw frame
 * addresses, EBX/ESI/EDI, EBP/ESP relative to the slot and the residue
 * hash below each callee's ESP), the arena and the globals. */
static int csy_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    csy_track=1;csy_fill=0;csy_clobbers=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],want[2],got[2],pattern=(poison?0xa7a7a7a7u:0x31313131u)+n;csy_build(n*41u+3u,a,r);
        BYTE before[CSY_GLOBALS],after[CSY_GLOBALS],actual[CSY_GLOBALS],expected_fpu[108],actual_fpu[108];static csy_record expected[CSY_MAX];
        memcpy(csy_before,csy_arena,CSY_SIZE);csy_globals(before,0);
        csy_reset();csy_track_run(mapped+d->rva,a,pattern,want);es2_hook_fpu_save(expected_fpu);
        const unsigned calls=csy_calls;memcpy(expected,csy_log,calls*sizeof(*expected));memcpy(csy_after,csy_arena,CSY_SIZE);csy_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("cockpit_sync poison install");
        memcpy(csy_arena,csy_before,CSY_SIZE);csy_globals(before,1);csy_reset();
        csy_track_run(mapped+d->rva,a,pattern,got);es2_hook_fpu_save(actual_fpu);csy_globals(actual,0);
        const uint64_t pair[2]={(uint64_t)want[1]<<32|want[0],(uint64_t)got[1]<<32|got[0]};
        if(pair[0]!=pair[1]||calls!=csy_calls||memcmp(expected,csy_log,calls*sizeof(*expected))||memcmp(csy_after,csy_arena,CSY_SIZE)||
           memcmp(after,actual,CSY_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=csy_report(fast?"installed fast":"installed preserving",n,pair,calls,expected,after,actual);
        if(!es2_hook_uninstall(&h))return fail("cockpit_sync poison uninstall");csy_globals(before,1);++total;
    }
    csy_track=0;
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status&&!csy_clobbers){fprintf(stderr,"FAIL: cockpit_sync installed probes: no changed argument slot was observed\n");status=1;}
    if(!status)printf("PASS: %s %u installed cases through the entry shim, fast/preserving dispatch, position-dependent stack, per-call EBX/ESI/EDI, EBP/ESP, residue below each callee, changed argument slots (%u in both runs), nonempty x87, full pair/calls/arena/globals\n",
        d->name,total,csy_clobbers);
    return status;
}
/* Real-callee pass: 41b130 over the real 4450f8, dist_gate, 447df8,
 * 447da8 (slider slot stubs), 43dbb0, 40e484, timer_word_step and 434a24
 * (shield_balance, weapon_frame, 43d9b4 and aim_point stubbed), with
 * cockpit_sync, throttle_record, throttle_gauge and hud_record installed
 * original/replaced in all 16 combinations (dist_gate and timer_word_step
 * replaced in odd cases), against the all-original run. */
static int csy_real(unsigned index){
    const unsigned set[6]={index,ES2_HOOK_INDEX_throttle_record,ES2_HOOK_INDEX_throttle_gauge,ES2_HOOK_INDEX_hud_record,
        ES2_HOOK_INDEX_dist_gate,ES2_HOOK_INDEX_timer_word_step};
    BYTE saved[CSY_IDS][5];int active[CSY_IDS];if(csy_patch(saved,active,CSY_REAL,1))return 1;
    int status=0;unsigned total=0;uint64_t entered[4]={0},slots=0;csy_fill=0;
    for(unsigned mode=0;mode<16&&!status;++mode)for(unsigned n=0;n<128&&!status;++n){
        uint32_t a[6],r[3],want[2],got[2],pattern=n&2?0xa7a7a7a7u:0x31313131u;csy_build(n*67u+mode*7u,a,r);csy_mask_ecx3=n&1;
        BYTE before[CSY_GLOBALS],after[CSY_GLOBALS],actual[CSY_GLOBALS];static csy_record expected[CSY_MAX];
        memcpy(csy_before,csy_arena,CSY_SIZE);csy_globals(before,0);
        csy_reset();sg_poison_call(mapped+function_hook_descriptor(index)->rva,a,pattern,want);
        const unsigned calls=csy_calls;memcpy(expected,csy_log,calls*sizeof(*expected));memcpy(csy_after,csy_arena,CSY_SIZE);csy_globals(after,0);
        es2_hook hk[6];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<6;++j){o.mode=(j<4?(mode>>j&1):(n&1))?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(set[j]),&o,function_hook_extension(set[j])))return fail("cockpit_sync real install");}
        memcpy(csy_arena,csy_before,CSY_SIZE);csy_globals(before,1);csy_reset();
        sg_poison_call(mapped+function_hook_descriptor(index)->rva,a,pattern,got);csy_globals(actual,0);
        const uint64_t pair[2]={(uint64_t)want[1]<<32|want[0],(uint64_t)got[1]<<32|got[0]};
        if(pair[0]!=pair[1]||calls!=csy_calls||memcmp(expected,csy_log,calls*sizeof(*expected))||memcmp(csy_after,csy_arena,CSY_SIZE)||
           memcmp(after,actual,CSY_GLOBALS)){
            fprintf(stderr,"real mode=%u\n",mode);status=csy_report("real",n,pair,calls,expected,after,actual);}
        for(unsigned j=0;j<4;++j)if(mode>>j&1)entered[j]+=hk[j].calls;
        for(unsigned j=0;j<calls;++j)if(expected[j].id>=12)++slots;
        for(unsigned j=0;j<6;++j)if(!es2_hook_uninstall(hk+j))return fail("cockpit_sync real uninstall");
        csy_globals(before,1);++total;
    }
    csy_mask_ecx3=0;
    if(csy_patch(saved,active,0,0))return 1;
    if(!status&&(!entered[0]||!entered[1]||!entered[2]||!entered[3]||!slots)){fprintf(stderr,"FAIL: %s real pass: entered %llu/%llu/%llu/%llu slider %llu\n",
        current_function,(unsigned long long)entered[0],(unsigned long long)entered[1],(unsigned long long)entered[2],
        (unsigned long long)entered[3],(unsigned long long)slots);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases (cockpit_sync, throttle_record, throttle_gauge, hud_record original/replaced in all 16 combinations, dist_gate and timer_word_step replaced in odd cases; replaced entries %llu/%llu/%llu/%llu, %llu slider slot calls), 31/a7 stack, pair/calls/arena/globals\n",
        current_function,total,(unsigned long long)entered[0],(unsigned long long)entered[1],(unsigned long long)entered[2],
        (unsigned long long)entered[3],(unsigned long long)slots);
    return status;
}
static int csy_sync_cases(unsigned index,unsigned limit){
    BYTE old_globals[CSY_GLOBALS];csy_globals(old_globals,0);
    BYTE saved[CSY_IDS][5];int active[CSY_IDS];
    if(csy_patch(saved,active,CSY_ALL,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_COCKPIT_SYNC_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);memset(csy_paths,0,sizeof(csy_paths));csy_track=0;csy_fill=1;
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];csy_build(n,a,r);status=csy_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);current_case_valid=0;
    csy_globals(old_globals,1);
    if(!limit&&!status)status=csy_installed_probes(index,256);
    csy_globals(old_globals,1);
    if(csy_patch(saved,active,CSY_ALL,0))return 1;
    if(!limit&&!status)status=csy_real(index);
    csy_globals(old_globals,1);csy_fill=0;
    if(!status)printf("PASS: %-20s %u cases (stub calls 4450f8 %u, 413bc8 %u, 49288c %u, 410b40 %u, 447df8 %u, 447da8 %u, 43dbb0 %u, 40e484 %u, 467a48 %u, 43d9b4 %u, 41b728 %u, 434a24 %u); full return pair, ordered calls with entry registers and frame offsets, arena and globals\n",
        function_hook_descriptor(index)->name,cases,csy_paths[0],csy_paths[1],csy_paths[2],csy_paths[3],csy_paths[4],csy_paths[5],
        csy_paths[6],csy_paths[7],csy_paths[8],csy_paths[9],csy_paths[10],csy_paths[11]);
    return status;
}
/* throttle_gauge: G (TG1 or TG2), the record L (separate, or overlapping
 * the gauge's record or the slider's redraw byte), the record equal to the
 * gauge's in about a third of the cases per field. */
static void csy_gauge_build(unsigned n,uint32_t a[6],uint32_t r[3]){
    csy_seed=random32();csy_stream=csy_seed^n;memset(a,0,24);
    csy_world();for(unsigned i=0;i<3;++i)r[i]=csy_next();
    const uint32_t g=csy_next()%2?CSY_TG1:CSY_TG2,k=csy_next()%16,s=get32(csy_arena+g+0x18)-CSY_BASE;
    const uint32_t l=k<11?CSY_L:k==11?s+0x1c:k==12?g+0xb1:k==13?g+0xad:k==14?g+0xb3:s+0x1b;
    BYTE *L=csy_arena+l;
    if(l==CSY_L){put32(L,csy_next());put32(L+4,csy_next());}
    if(csy_next()%3==0)put32(L+4,get32(csy_arena+g+0xb5));
    if(csy_next()%3==0)put32(L,get32(csy_arena+g+0xb1));
    a[0]=CSY_ADDR(g);a[1]=CSY_ADDR(l);csy_l=a[1];
    csy_seed=csy_stream;
}
static int csy_gauge_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],want[2],got[2],pattern=poison?0xa7a7a7a7u:0x31313131u;csy_gauge_build(n*41u+5u,a,r);
        BYTE before[CSY_GLOBALS],after[CSY_GLOBALS],actual[CSY_GLOBALS],expected_fpu[108],actual_fpu[108];static csy_record expected[CSY_MAX];
        memcpy(csy_before,csy_arena,CSY_SIZE);csy_globals(before,0);
        csy_reset();sg_poison_call(mapped+d->rva,a,pattern,want);es2_hook_fpu_save(expected_fpu);
        const unsigned calls=csy_calls;memcpy(expected,csy_log,calls*sizeof(*expected));memcpy(csy_after,csy_arena,CSY_SIZE);csy_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("throttle_gauge poison install");
        memcpy(csy_arena,csy_before,CSY_SIZE);csy_globals(before,1);csy_reset();
        sg_poison_call(mapped+d->rva,a,pattern,got);es2_hook_fpu_save(actual_fpu);csy_globals(actual,0);
        const uint64_t pair[2]={(uint64_t)want[1]<<32|want[0],(uint64_t)got[1]<<32|got[0]};
        if(pair[0]!=pair[1]||calls!=csy_calls||memcmp(expected,csy_log,calls*sizeof(*expected))||memcmp(csy_after,csy_arena,CSY_SIZE)||
           memcmp(after,actual,CSY_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=csy_report(fast?"installed fast":"installed preserving",n,pair,calls,expected,after,actual);
        if(!es2_hook_uninstall(&h))return fail("throttle_gauge poison uninstall");csy_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/calls/arena/globals\n",d->name,total);
    return status;
}
static int csy_gauge_cases(unsigned index,unsigned limit){
    BYTE old_globals[CSY_GLOBALS];csy_globals(old_globals,0);
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_COCKPIT_SYNC_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);memset(csy_paths,0,sizeof(csy_paths));csy_track=0;csy_fill=0;
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];csy_gauge_build(n,a,r);status=csy_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);current_case_valid=0;
    csy_globals(old_globals,1);
    if(!limit&&!status)status=csy_gauge_probes(index,256);
    csy_globals(old_globals,1);csy_l=0;
    if(!status)printf("PASS: %-20s %u cases (slider slot calls %u); full return pair, ordered calls with entry registers, arena and globals\n",
        function_hook_descriptor(index)->name,cases,csy_paths[12]+csy_paths[13]);
    return status;
}
/* throttle_record and hud_record: the argument over its whole range
 * (around the 32-bit wrap in one case of eight), random entry registers. */
static void csy_record_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    (void)index;csy_seed=random32()^n;csy_stream=csy_seed;memset(a,0,24);
    const uint32_t v=csy_next();a[0]=n%8==0?0xffffff4fu+(v%3)-1u:v;
    for(unsigned i=0;i<3;++i)r[i]=csy_next();
}
static void csy_record_state(BYTE *state,int restore){(void)state;(void)restore;}
static int csy_record_cases(unsigned index,unsigned limit){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    if(limit&&compare_probe_installed(index))return compare_probe_run(index,limit,csy_record_build,csy_record_state,0);
    unsigned cases=case_budget(limit,1001024);int status=0;
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;
        uint32_t a[6],r[3];csy_record_build(index,n,a,r);
        const uint64_t want=weapons_original(index,a,r),got=x->replacement_context(a,d->user,r);
        if(want!=got){fprintf(stderr,"FAIL: %s case %u pair %016llx/%016llx\n",d->name,n,(unsigned long long)want,(unsigned long long)got);status=1;}}
    current_case_valid=0;
    if(!limit&&!status){
        BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);__asm__ volatile("fninit; fld1; fldpi");unsigned total=0;
        for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<256&&!status;++n){
            uint32_t a[6],r[3],want[2],got[2],pattern=poison?0xa7a7a7a7u:0x31313131u;csy_record_build(index,n,a,r);
            BYTE expected_fpu[108],actual_fpu[108];
            sg_poison_call(mapped+d->rva,a,pattern,want);es2_hook_fpu_save(expected_fpu);
            es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
            es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,x))return fail("record getter poison install");
            sg_poison_call(mapped+d->rva,a,pattern,got);es2_hook_fpu_save(actual_fpu);
            if(memcmp(want,got,8)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1){
                fprintf(stderr,"FAIL: %s installed case %u pair %08x:%08x/%08x:%08x\n",d->name,n,want[1],want[0],got[1],got[0]);status=1;}
            if(!es2_hook_uninstall(&h))return fail("record getter poison uninstall");++total;
        }
        es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
        if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair\n",d->name,total);
    }
    if(!limit&&!status&&compare_probe_capable(index))status=compare_probe_run(index,0,csy_record_build,csy_record_state,0);
    if(!status)printf("PASS: %-20s %u cases; full return pair\n",d->name,cases);
    return status;
}
static int cockpit_sync_cases(unsigned index,unsigned limit){
    if(index==ES2_HOOK_INDEX_cockpit_sync)return csy_sync_cases(index,limit);
    if(index==ES2_HOOK_INDEX_throttle_gauge)return csy_gauge_cases(index,limit);
    return csy_record_cases(index,limit);
}
