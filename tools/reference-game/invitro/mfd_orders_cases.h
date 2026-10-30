/* mfd_orders group: the MFD order path (docs/reverse-engineering/cockpit.md,
 * "MFD order path"). A 16 KiB arena holds the MFDGauge G (button table +18h,
 * count +68h, mode +C1h, objects +CDh, menu +D1h, selection +B1h, range +C5h,
 * view list +308h..+31Ch), its menu with six entry gadgets (rectangle at +4),
 * the cursor point, the radio pair (buttons 11/12, slot +4 through two arena
 * tables), the cockpit H, two groups with member tables, eleven actors (order
 * slot +28h through an arena table, byte [[B+4Dh]+9] at B+109h) and an order
 * record; the image globals 49b088, 49b0f8, 49dbe0, 4a9b7a and 4d03c8..4d03d9
 * (the wingman table and the order record 4d03d4) are set per case, compared
 * and restored.
 *
 * Direct cases run the original body and the adapter (and for group_order
 * the core over a filled scratch frame) against logging stubs for every
 * game callee of the hook under test: the direct calls are patched in the
 * private image (446e60, 446ff0, 446fc4, 446fb0, 447248, 438d9c, 4470a4,
 * 43f9c0, 43166c, 43f9f8, 4231a4, dist_gate 49288c, cockpit_next 43349c,
 * pool_prev 471c70, 44a7e8, 4472bc), the radio buttons' slot +4 and the
 * members' order slot +28h come from arena tables; memset 49681a (the own
 * runtime emulation) and 447014 run as real code. A record keeps the
 * callee, the bridge's slot count, the arguments and the entry EAX/EDX/ECX
 * (stack values masked). The stubs return values from a per-case stream
 * (dist_gate distances with ties and the signed extremes, order answers
 * with a zero low word over a nonzero high word, pool_prev the case's actor
 * list) and change in about one call of four what the originals reread
 * (the MFD mode, the radio buttons and their state, the selected entry
 * [G+B1h], the order code and issuer, the group count and table, a
 * member's bytes, H's objects and the wingman table). The full EDX:EAX
 * pair, ECX (pure leaves; not with the hook installed), the call log, the
 * arena and the globals are compared, the callee counters for the first
 * 4,096 cases. group_order
 * runs the original over 2 KiB of stack filled with the case's priority
 * residue (one in three cases 0, the adapter's zeroed scratch; else a
 * random word, the core over a scratch frame with the same fill) and also
 * compares the original's frame (locals and argument slots) after return;
 * group counts 0..12 (and negative): members 10 and 11 have their flag
 * bytes in the result word as in the original; from 13 members on the flag
 * bytes of members 12..15 are the best-member dword, cleared at every pass,
 * so the original can order such a member forever (outside the contract).
 *
 * Then installed probes through the entry shims: the original and the
 * replaced hook over a 31/a7-filled stack, with every stub also recording
 * EBX/ESI/EDI, EBP/ESP relative to the caller's slots and a hash of the
 * 256 bytes below its entry, and the 1,024 bytes below the slots compared
 * after return (fast and preserving dispatch); real-callee passes: the
 * order chain from mfd_order, mfd_button (button 10) or mfd_click over the
 * real 43f9c0, 43166c, 4231a4, 43f9f8, dist_gate, memset, 4470a4 and
 * button_press (its slot +8 and 4530bc stubbed), the member slot stubbed,
 * every chain hook of this group uninstalled or replaced at random per
 * case, against the all-uninstalled run on the same 31/a7 fill: Alt+O's
 * code 3 is given over the 3131h fill and refused over A7A7h in both;
 * mfd_button over the real 446ff0/447014, 446fc4, 446fb0 (cockpit_next
 * stubbed); order_setup over the real pool_prev (a built actor list) with
 * pool_prev and order_setup original or replaced; and the compare probes
 * of compare_probes.h for menu_code, menu_toggle, mfd_entry and
 * mfd_view_next. Identifiers: mf_/MF_. */
enum { MF_SIZE=0x4000, MF_G=0x100, MF_MENU=0x500, MF_ENTRY=0x580, MF_POINT=0x640, MF_RB=0x660, MF_VT=0x780,
    MF_H=0x800, MF_GROUP2=0xa20, MF_GROUP=0xa40, MF_TABLE=0xa60, MF_OBJ=0xb00, MF_ACTOR=0xc00, MF_STRIDE=0x2b0,
    MF_ACTORS=11, MF_RECORD=0x3e80, MF_IDS=21, MF_MAX=600, MF_GLOBALS=4+4+1+4+18, MF_COMPARE=0x580,
    MF_RADIO0=16, MF_RADIO1=17, MF_ORDER=18, MF_BUTTON8=19, MF_RELEASE=20 };
#define MF_BASE ((uint32_t)(uintptr_t)mf_arena)
#define MF_ADDR(o) (MF_BASE+(uint32_t)(o))
#define MF_A(k) MF_ADDR(MF_ACTOR+MF_STRIDE*(k))
static BYTE mf_arena[MF_SIZE],mf_before[MF_SIZE],mf_after[MF_SIZE];
typedef struct mf_record { uint32_t id,slots,a[3],regs[3],cs[3],frame[2],window; } mf_record;
static mf_record mf_log[MF_MAX];
static unsigned mf_calls,mf_track,mf_index,mf_counted,mf_hits[MF_IDS];
static uint32_t mf_seed,mf_stream,mf_list[8];
static unsigned mf_list_count,mf_list_used;
/* Captured by the stubs at entry: EAX, EDX, ECX, EBX, ESI, EDI, EBP, ESP;
 * the 256 bytes below the entry ESP; the ECX a stub returns. */
__attribute__((used)) static uint32_t mf_cap[8],mf_ecx,mf_origin;
__attribute__((used)) static BYTE mf_window[256],mf_below[1024];
static const uint32_t mf_addr[MF_IDS]={0x446e60,0x446ff0,0x446fc4,0x446fb0,0x447248,0x438d9c,0x4470a4,0x43f9c0,0x43166c,
    0x43f9f8,0x4231a4,0x49288c,0x43349c,0x471c70,0x44a7e8,0x4472bc,0,0,0,0,0x4530bc};
static const unsigned mf_arity[MF_IDS]={2,1,1,1,1,3,2,1,2,1,2,2,1,2,3,2,1,1,3,2,2};
static uint32_t mf_next(void){
    uint32_t z=mf_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static void mf_reset(void){mf_calls=0;mf_stream=mf_seed;mf_list_used=0;}
static int mf_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static void mf_globals(BYTE *out,int restore){
    static const struct { uint32_t rva,size; } g[]={{0x9b088,4},{0x9b0f8,4},{0x9dbe0,1},{0xa9b7a,4},{0xd03c8,18}};
    BYTE *p=out;for(unsigned i=0;i<5;++i){if(restore)memcpy(mapped+g[i].rva,p,g[i].size);else memcpy(p,mapped+g[i].rva,g[i].size);p+=g[i].size;}
}
static uint32_t mf_hash(const BYTE *p,unsigned n){uint32_t h=2166136261u;for(unsigned i=0;i<n;++i)h=(h^p[i])*16777619u;return h;}
/* Stack values: masked in direct cases, relative to the caller's slots in
 * the tracked probes. */
static uint32_t mf_mask(uint32_t v){return mf_stack(v)?(mf_track?v-mf_origin:0u):v;}
/* One callee side effect (about one call in four), drawn from the stream. */
static void mf_effect(unsigned id){
    const uint32_t pick=mf_next();if(pick%4)return;
    BYTE *G=mf_arena+MF_G;const uint32_t R=0x4d03d4u;
    mf_counted=0;   /* a changed layout: skip the counter check */
    switch(id){
    case 0:case 1:case 2:case 3:case 4:case 5:case 6:case MF_RADIO0:case MF_RADIO1:case MF_BUTTON8:case MF_RELEASE:
        switch(pick/4%5){
        case 0:put32(G+0xc1,mf_next()%6);break;
        case 1:put32(G+0x48,MF_ADDR(MF_RB+(mf_next()%2?0x60:0)));break;
        case 2:mf_arena[MF_RB+0x60*(mf_next()%2)+0x40]^=1;break;
        case 3:put32(G+0xb1,mf_next()%6);break;
        default:put32(mf_arena+MF_RB+0x60*(mf_next()%2)+0x17,MF_ADDR(MF_VT+0x10*(mf_next()%2)));break;
        }
        break;
    case 7:case 8:case 9:case 10:case 11:case MF_ORDER:
        switch(pick/4%6){
        case 0:put16(mapped+R-0x400000u,(uint16_t)(mf_next()%10));break;
        case 1:put32(mapped+R+2-0x400000u,MF_A(mf_next()%MF_ACTORS));break;
        case 2:put16(mf_arena+MF_GROUP+0x10,(uint16_t)(mf_next()%7));break;
        case 3:put32(mf_arena+MF_GROUP+0xc,MF_ADDR(MF_TABLE+0x40*(mf_next()%2)));break;
        case 4:mf_arena[MF_ACTOR+MF_STRIDE*(mf_next()%MF_ACTORS)+0x99]^=1;break;
        default:mf_arena[MF_ACTOR+MF_STRIDE*(mf_next()%MF_ACTORS)+(mf_next()%2?0x96:0x109)]=(BYTE)(mf_next()%3-1);break;
        }
        break;
    default:
        switch(pick/4%4){
        case 0:put32(mf_arena+MF_H+0x1f9,mf_next());break;
        case 1:put32(mapped+0xd03c8+4*(mf_next()%3),MF_A(mf_next()%MF_ACTORS));break;
        case 2:put16(mf_arena+MF_ACTOR+MF_STRIDE*(mf_next()%MF_ACTORS)+0x29c,(uint16_t)mf_next());break;
        default:put32(mf_arena+MF_H+0x203,MF_A(mf_next()%MF_ACTORS));break;
        }
        break;
    }
}
__attribute__((used,noinline)) static uint64_t __cdecl mf_log_impl(unsigned id){
    if(mf_calls==MF_MAX){fail("mfd_orders synthetic call overflow");ExitProcess(1);}
    uint32_t *a=(uint32_t *)(uintptr_t)(mf_cap[7]+4u);
    mf_record *v=mf_log+mf_calls++;memset(v,0,sizeof(*v));v->id=id;v->slots=weapons_bridge_slots(a,mf_arity[id]);
    for(unsigned j=0;j<mf_arity[id];++j)v->a[j]=mf_mask(a[j]);
    for(unsigned j=0;j<3;++j)v->regs[j]=mf_mask(mf_cap[j]);
    if(mf_track){for(unsigned j=0;j<3;++j)v->cs[j]=mf_mask(mf_cap[3+j]);
        v->frame[0]=mf_cap[6]-mf_origin;v->frame[1]=mf_cap[7]-mf_origin;v->window=mf_hash(mf_window,256);}
    mf_hits[id]++;
    uint32_t eax=mf_next(),edx=mf_next();mf_ecx=mf_next();
    if(id==11){static const uint32_t d[]={0,1,0x7fffffffu,0x80000000u,0xffffffffu,0x7ffffffeu};
        const uint32_t k=mf_next();eax=k%4==0?d[k/4%6]:k%3==0?1000u*(mf_next()%8):mf_next()%5000u;}
    if(id==MF_ORDER){const uint32_t k=mf_next();eax=k%3==0?eax&0xffff0000u:k%3==1?(eax|1u)&0x0001ffffu:eax;}
    if(id==13)eax=mf_list_used<mf_list_count?mf_list[mf_list_used++]:0;
    /* The order call and the group order answer 0 or 1 mostly (4231a4's
     * EAX), and any word otherwise. */
    if(id==8||id==10){const uint32_t k=mf_next();eax=k%3==0?0u:k%3==1?1u:eax;}
    mf_effect(id);
    return (uint64_t)edx<<32|eax;
}
#define MF_STUB(n) __attribute__((naked)) static void mf_stub_##n(void){__asm__ volatile( \
    "movl %eax,_mf_cap\n\tmovl %edx,_mf_cap+4\n\tmovl %ecx,_mf_cap+8\n\tmovl %ebx,_mf_cap+12\n\t" \
    "movl %esi,_mf_cap+16\n\tmovl %edi,_mf_cap+20\n\tmovl %ebp,_mf_cap+24\n\tmovl %esp,_mf_cap+28\n\t" \
    "leal -256(%esp),%esi\n\tmovl $_mf_window,%edi\n\tmovl $64,%ecx\n\tcld\n\trep movsl\n\t" \
    "movl _mf_cap+16,%esi\n\tmovl _mf_cap+20,%edi\n\t" \
    "pushl $" #n "\n\tcalll _mf_log_impl\n\taddl $4,%esp\n\tmovl _mf_ecx,%ecx\n\tret\n\t");}
MF_STUB(0) MF_STUB(1) MF_STUB(2) MF_STUB(3) MF_STUB(4) MF_STUB(5) MF_STUB(6) MF_STUB(7) MF_STUB(8) MF_STUB(9) MF_STUB(10)
MF_STUB(11) MF_STUB(12) MF_STUB(13) MF_STUB(14) MF_STUB(15) MF_STUB(16) MF_STUB(17) MF_STUB(18) MF_STUB(19) MF_STUB(20)
#undef MF_STUB
static void *mf_stubs[MF_IDS]={mf_stub_0,mf_stub_1,mf_stub_2,mf_stub_3,mf_stub_4,mf_stub_5,mf_stub_6,mf_stub_7,mf_stub_8,mf_stub_9,
    mf_stub_10,mf_stub_11,mf_stub_12,mf_stub_13,mf_stub_14,mf_stub_15,mf_stub_16,mf_stub_17,mf_stub_18,mf_stub_19,mf_stub_20};
/* The patched callees of each hook's direct cases (bit = stub id). */
static unsigned mf_patch_set(unsigned index){
    if(index==ES2_HOOK_INDEX_mfd_button)return 0x1fu;
    if(index==ES2_HOOK_INDEX_mfd_click)return 1u<<4|1u<<5|1u<<6;
    if(index==ES2_HOOK_INDEX_mfd_order)return 1u<<7|1u<<8|1u<<9;
    if(index==ES2_HOOK_INDEX_order_call)return 1u<<10;
    if(index==ES2_HOOK_INDEX_group_order)return 1u<<11;
    if(index==ES2_HOOK_INDEX_mfd_target)return 1u<<12;
    if(index==ES2_HOOK_INDEX_order_setup)return 1u<<13|1u<<14|1u<<15;
    return 0;
}
static int mf_patch(BYTE saved[MF_IDS][5],int active[MF_IDS],unsigned which,int on){
    for(unsigned i=0;i<MF_IDS;++i){
        if(!mf_addr[i]){active[i]=0;continue;}
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(mf_addr[i]-0x400000u,mf_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(mf_addr[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static uint32_t mf_edge(void){
    static const uint32_t e[]={0,1,0xffffffffu,0x7fffffffu,0x80000000u,0x7ffffffeu,0x80000001u};
    switch(mf_next()%10){case 0:return e[mf_next()%7];case 1:return mf_next();case 2:return mf_next()%64u-32u;default:return mf_next()%640u;}
}
static uint32_t mf_near(uint32_t lo,uint32_t hi){
    switch(mf_next()%8){case 0:return lo+mf_next()%3u-1u;case 1:return hi+mf_next()%3u-1u;case 2:return mf_edge();
        default:return lo+(hi-lo)/2u+mf_next()%3u-1u;}
}
/* Builds one state; a[] the hook's slots, r the entry EAX/EDX/ECX. */
static void mf_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    mf_seed=random32();mf_stream=mf_seed^n;random_bytes(mf_arena,MF_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=mf_next();
    BYTE *G=mf_arena+MF_G,*M=mf_arena+MF_MENU,*H=mf_arena+MF_H;mf_counted=1;
    /* G: thirteen buttons (11/12 the radio pair), mode, objects, menu. */
    /* Up to 200h buttons (the scan stays in the arena) or a negative count. */
    const uint32_t count=mf_next()%4?13u:mf_next()%3?mf_next()%16u:mf_next()%2?mf_next()%0x200u:0x80000000u|mf_next();
    put32(G+0x68,count);
    for(unsigned i=0;i<11;++i)put32(G+0x18+4*i,0x50000u+(mf_next()%2?i:mf_next()%16u));
    put32(G+0x44,MF_ADDR(MF_RB));put32(G+0x48,MF_ADDR(MF_RB+0x60));
    if(mf_next()%8==0){put32(G+0x48,MF_ADDR(MF_RB));mf_counted=0;}
    for(unsigned k=0;k<2;++k){put32(mf_arena+MF_RB+0x60*k+0x17,MF_ADDR(MF_VT+0x10*k));mf_arena[MF_RB+0x60*k+0x40]=(BYTE)(mf_next()%3?mf_next()%2:mf_next());}
    put32(mf_arena+MF_VT+4,(uint32_t)(uintptr_t)mf_stubs[MF_RADIO0]);put32(mf_arena+MF_VT+0x14,(uint32_t)(uintptr_t)mf_stubs[MF_RADIO1]);
    put32(mf_arena+MF_VT+0x20+0x28,(uint32_t)(uintptr_t)mf_stubs[MF_ORDER]);
    put32(G+0xc1,mf_next()%3?mf_next()%2:mf_next()%6);
    for(unsigned k=0;k<8;++k)put32(G+0xcd+4*k,MF_ADDR(MF_OBJ+0x20*k));
    put32(G+0x14,mf_next()%2?MF_ADDR(MF_H):mf_next());put32(G+0xd1,MF_ADDR(MF_MENU));
    put32(G+0xb1,mf_next()%3?mf_next()%6:mf_next());
    put32(G+0xc5,mf_next()%10u-3u);
    const uint32_t views=mf_next()%5;put32(G+0x31c,mf_next()%4?views:mf_next());put32(G+0x318,mf_next()%3?mf_next()%(views+1):mf_next());
    /* The menu: entries 0..5 (a few beyond), flag bytes with bit 1 set or clear. */
    for(unsigned i=0;i<6;++i){put32(M+0x14+4*i,MF_ADDR(MF_ENTRY+0x20*i));M[0x2c+i]=(BYTE)(mf_next()%2?mf_next()&2u:mf_next());}
    put32(M+0x32,mf_next()%8?mf_next()%6:mf_next()%16u);
    /* The point near a chosen entry's rectangle (inclusive edges). */
    for(unsigned i=0;i<6;++i){BYTE *rc=mf_arena+MF_ENTRY+0x20*i+4;
        const uint32_t x0=mf_edge(),y0=mf_edge(),x1=mf_next()%6?x0+mf_next()%40u:mf_edge(),y1=mf_next()%6?y0+mf_next()%30u:mf_edge();
        put32(rc,x0);put32(rc+4,y0);put32(rc+8,x1);put32(rc+12,y1);}
    {const unsigned t=mf_next()%6;const BYTE *rc=mf_arena+MF_ENTRY+0x20*t+4;
        put32(mf_arena+MF_POINT,mf_near(get32(rc),get32(rc+8)));put32(mf_arena+MF_POINT+4,mf_near(get32(rc+4),get32(rc+12)));
        if(mf_next()%3==0)put32(G+0xb1,t);}
    /* The actors: order table, position, group, the priority bytes. */
    for(unsigned k=0;k<MF_ACTORS;++k){BYTE *B=mf_arena+MF_ACTOR+MF_STRIDE*k;
        put32(B,MF_ADDR(MF_VT+0x20));put32(B+0x4d,MF_A(k)+0x100u);
        B[0x109]=(BYTE)(mf_next()%3?mf_next()%3-1u:mf_next());B[0x96]=(BYTE)(mf_next()%3?mf_next()%2:mf_next());
        B[0x99]=(BYTE)(mf_next()%5?0:1);B[0xa3]=(BYTE)(mf_next()%3?0:1);
        put32(B+0x45,MF_ADDR(mf_next()%4?MF_GROUP:MF_GROUP2));}
    /* Two groups over two member tables (duplicates allowed; a null only for
     * order_setup, whose walk stops there: the group order faults on one). */
    for(unsigned t=0;t<2;++t)for(unsigned i=0;i<16;++i)
        put32(mf_arena+MF_TABLE+0x40*t+4*i,index==ES2_HOOK_INDEX_order_setup&&mf_next()%10==0?0u:MF_A(mf_next()%MF_ACTORS));
    for(unsigned t=0;t<2;++t){BYTE *g=mf_arena+(t?MF_GROUP2:MF_GROUP);put32(g+0xc,MF_ADDR(MF_TABLE+0x40*t));
        put16(g+0x10,(uint16_t)(mf_next()%6?mf_next()%7:mf_next()%3?mf_next()%13:0u-1u-mf_next()%4));}
    /* The cockpit and the globals. */
    put32(H+0x203,MF_A(mf_next()%MF_ACTORS));put32(H+0x1ed,mf_next());put32(H+0x1f9,mf_next());
    put32(mapped+0x9b088,MF_ADDR(MF_H));put32(mapped+0x9b0f8,MF_ADDR(mf_next()%2?MF_GROUP:MF_GROUP2));
    mapped[0x9dbe0]=(BYTE)(mf_next()%2?0:mf_next()%2?1:mf_next());put32(mapped+0xa9b7a,mf_next());
    for(unsigned k=0;k<3;++k)put32(mapped+0xd03c8+4*k,mf_next());
    put16(mapped+0xd03d4,(uint16_t)(mf_next()%4?mf_next()%10:mf_next()%3?mf_next()%0x20:mf_next()));
    put32(mapped+0xd03d6,mf_next()%3?get32(H+0x203):MF_A(mf_next()%MF_ACTORS));
    put16(mf_arena+MF_RECORD,(uint16_t)(mf_next()%10));put32(mf_arena+MF_RECORD+2,MF_A(mf_next()%MF_ACTORS));
    /* pool_prev's answers for order_setup: actors, then 0. */
    mf_list_count=mf_next()%5;for(unsigned k=0;k<mf_list_count;++k)mf_list[k]=MF_A(mf_next()%MF_ACTORS);
    /* The slots. */
    const uint32_t Ga=MF_ADDR(MF_G);
    if(index==ES2_HOOK_INDEX_mfd_button){
        /* A button of the table (also beyond index 12 with 16 buttons), or none. */
        const unsigned i=mf_next()%17;if(i>=13&&mf_next()%2)put32(G+0x68,16);
        a[0]=Ga;a[1]=i<16?get32(G+0x18+4*i):mf_next()%3?0x50000u+mf_next()%16:mf_next();a[2]=mf_next();
    } else if(index==ES2_HOOK_INDEX_mfd_click){a[0]=Ga;a[1]=mf_next();a[2]=MF_ADDR(MF_POINT);if(mf_next()%2)put32(G+0xc1,1);}
    else if(index==ES2_HOOK_INDEX_order_call){a[0]=mf_next()%4?MF_ADDR(MF_H):MF_A(mf_next()%MF_ACTORS);a[1]=mf_next()%4?0x4d03d4u:MF_ADDR(MF_RECORD);}
    else if(index==ES2_HOOK_INDEX_group_order){a[0]=MF_ADDR(mf_next()%2?MF_GROUP:MF_GROUP2);a[1]=mf_next()%4?0x4d03d4u:MF_ADDR(MF_RECORD);}
    else if(index==ES2_HOOK_INDEX_menu_code||index==ES2_HOOK_INDEX_menu_toggle)a[0]=MF_ADDR(MF_MENU);
    else if(index==ES2_HOOK_INDEX_mfd_entry){a[0]=Ga;a[1]=mf_next()%3?mf_next()%6:mf_next();}
    else if(index==ES2_HOOK_INDEX_order_setup)a[0]=MF_ADDR(MF_H);
    else a[0]=Ga;
    mf_stream=mf_seed;mf_list_used=0;   /* the stubs' stream restarts here in both runs */
}
/* group_order's original over 2 KiB of stack filled with `fill`; out[0..1]
 * EDX:EAX, out[2..13] the 30h bytes from the locals to the argument slots,
 * out[14] its EBP. */
__attribute__((naked,noinline)) static void mf_frame_invoke(void *target,const uint32_t *a,uint32_t fill,const uint32_t *r,uint32_t *out){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $2048,%esp\n\tmovl %esp,%edi\n\tmovl 16(%ebp),%eax\n\tmovl $512,%ecx\n\tcld\n\trep stosl\n\taddl $2048,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 20(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tcall *8(%ebp)\n\taddl $8,%esp\n\t"
        "movl 24(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tleal -16(%esp),%eax\n\tmovl %eax,56(%edi)\n\taddl $8,%edi\n\tleal -0x30(%esp),%esi\n\tmovl $12,%ecx\n\tcld\n\trep movsl\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* Calls target with three argument slots and EAX/EDX/ECX = r; out
 * receives EAX, EDX and ECX after the call. */
__attribute__((naked,noinline)) static void mf_invoke(void *target,const uint32_t *a,const uint32_t *r,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl 12(%ebp),%esi\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static uint64_t mf_original(unsigned index,const uint32_t *a,const uint32_t *r,uint32_t *ecx){
    uint32_t out[3];mf_invoke(mapped+function_hook_descriptor(index)->rva,a,r,out);*ecx=out[2];
    return (uint64_t)out[1]<<32|out[0];
}
/* The core of a pure leaf (with its ECX). */
static uint64_t mf_leaf_core(unsigned index,const uint32_t *a,const uint32_t *r,uint32_t *ecx){
    const es2_guest_memory *m=&math_native_memory;
    if(index==ES2_HOOK_INDEX_menu_code)return es2_menu_code_registers(m,a[0],ecx);
    if(index==ES2_HOOK_INDEX_menu_toggle)return es2_menu_toggle_registers(m,a[0],r,ecx);
    if(index==ES2_HOOK_INDEX_mfd_entry)return es2_mfd_entry_registers(m,a[0],a[1],r,ecx);
    return es2_mfd_view_next_registers(m,a[0],r,ecx);
}
static int mf_leaf(unsigned index){
    return index==ES2_HOOK_INDEX_menu_code||index==ES2_HOOK_INDEX_menu_toggle||index==ES2_HOOK_INDEX_mfd_entry||
        index==ES2_HOOK_INDEX_mfd_view_next;
}
/* The calls the hook makes itself: every logged call in a direct case
 * (all callees are stubs) plus memset (group_order) and 447014
 * (mfd_range); the radio pair by its slot key in the canonical layout. */
static int mf_counter_check(const callee_counter *c,uint64_t delta){
    uint64_t want=0;
    if(c->key==0x49681au||c->key==0x447014u)want=1;
    else if(c->event==ES2_COLLIDE_SLOT&&c->key==0x28u){for(unsigned j=0;j<mf_calls;++j)want+=mf_log[j].id==MF_ORDER;}
    else if(c->event==ES2_COLLIDE_SLOT){
        if(!mf_counted)return 0;
        for(unsigned j=0;j<mf_calls;++j)want+=mf_log[j].id==(c->key==0x44u?MF_RADIO0:MF_RADIO1);
    } else for(unsigned j=0;j<mf_calls;++j)want+=mf_addr[mf_log[j].id]==c->key;
    return delta!=want;
}
static int mf_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const mf_record *e,const BYTE *want,const BYTE *got){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,mf_calls);
    for(unsigned j=0;j<calls&&j<mf_calls&&e;++j)if(memcmp(e+j,mf_log+j,sizeof(*e))){
        const uint32_t *x=(const uint32_t*)(e+j),*y=(const uint32_t*)(mf_log+j);
        for(unsigned k=0;k<sizeof(*e)/4;++k)if(x[k]!=y[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,e[j].id,k,x[k],y[k]);break;}
    for(unsigned j=0;j<MF_SIZE;++j)if(mf_after[j]!=mf_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,mf_after[j],mf_arena[j]);break;}
    for(unsigned j=0;want&&j<MF_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);break;}
    return 1;
}
/* One direct case: the original against the adapter, and for group_order
 * against the core over the same residue fill. */
static int mf_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[MF_GLOBALS],after[MF_GLOBALS],actual[MF_GLOBALS];static mf_record expected[MF_MAX];
    const int group=index==ES2_HOOK_INDEX_group_order;
    uint32_t fill=0,frame[15],ecx=0;
    if(group&&n%3){const uint32_t w=random32()&0xffffu;fill=w|w<<16;}
    memcpy(mf_before,mf_arena,MF_SIZE);mf_globals(before,0);
    mf_reset();uint64_t pair[2];
    if(group){mf_frame_invoke(mapped+d->rva,a,fill,r,frame);pair[0]=(uint64_t)frame[1]<<32|frame[0];}
    else pair[0]=mf_original(index,a,r,&ecx);
    const unsigned calls=mf_calls;memcpy(expected,mf_log,calls*sizeof(*expected));memcpy(mf_after,mf_arena,MF_SIZE);mf_globals(after,0);
    memcpy(mf_arena,mf_before,MF_SIZE);mf_globals(before,1);mf_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8]={0};
    for(unsigned i=0;i<count&&i<8;++i)counts[i]=c[i].count;
    if(!group||!fill) {
        pair[1]=x->replacement_context(a,d->user,r);mf_globals(actual,0);
        /* group_order's EDX may be its flag pointer, a frame address: the
         * adapter's scratch frame lies elsewhere (compared relative to the
         * frame below). */
        if(group&&mf_stack((uint32_t)(pair[0]>>32))&&mf_stack((uint32_t)(pair[1]>>32)))pair[1]=(pair[1]&0xffffffffu)|(pair[0]&~(uint64_t)0xffffffffu);
        if(n<4096)for(unsigned i=0;i<count&&i<8;++i)if(mf_counter_check(c+i,c[i].count-counts[i]))return fail("mfd_orders callee counter delta");
        if(pair[0]!=pair[1]||calls!=mf_calls||memcmp(expected,mf_log,calls*sizeof(*expected))||memcmp(mf_after,mf_arena,MF_SIZE)||
           memcmp(after,actual,MF_GLOBALS))
            return mf_report("adapter",n,pair,calls,expected,after,actual);
        if(mf_leaf(index)){
            memcpy(mf_arena,mf_before,MF_SIZE);mf_globals(before,1);
            uint32_t core_ecx;const uint64_t core=mf_leaf_core(index,a,r,&core_ecx);
            if(core!=pair[0]||(!rng_hooks[index].installed&&core_ecx!=ecx)||memcmp(mf_after,mf_arena,MF_SIZE)){
                fprintf(stderr,"core ecx %08x/%08x\n",ecx,core_ecx);const uint64_t both[2]={pair[0],core};
                return mf_report("core",n,both,calls,expected,after,actual);
            }
        }
    }
    if(group){
        /* The core over a scratch frame with the same fill: locals and
         * argument slots as the original leaves them. */
        memcpy(mf_arena,mf_before,MF_SIZE);mf_globals(before,1);mf_reset();
        uint32_t scratch[12];for(unsigned j=0;j<12;++j)scratch[j]=fill;scratch[10]=a[0];scratch[11]=a[1];
        const uint32_t f=(uint32_t)(uintptr_t)(scratch+8),caller[2]={0,0};
        es2_weapon_graph g=weapons_graph(mapped,NULL,index);
        uint64_t core=es2_group_order_registers(&math_native_memory,&g,NULL,f,caller,a[0],a[1],r);mf_globals(actual,0);
        if(mf_stack((uint32_t)(pair[0]>>32))&&(uint32_t)(core>>32)-f==(uint32_t)(pair[0]>>32)-frame[14])core=(core&0xffffffffu)|(pair[0]&~(uint64_t)0xffffffffu);
        /* [f-20h] is the flag pointer f-10h+i: relative to the frame. */
        if(scratch[0]-f==frame[2]-frame[14])scratch[0]=frame[2];
        if(core!=pair[0]||calls!=mf_calls||memcmp(expected,mf_log,calls*sizeof(*expected))||memcmp(mf_after,mf_arena,MF_SIZE)||
           memcmp(after,actual,MF_GLOBALS)||memcmp(scratch,frame+2,0x20)||memcmp(scratch+10,frame+12,8)){
            for(unsigned j=0;j<12;++j)if(scratch[j]!=frame[2+j])fprintf(stderr," frame dword %u %08x/%08x (fill %08x)\n",j,frame[2+j],scratch[j],fill);
            const uint64_t both[2]={pair[0],core};return mf_report("core frame",n,both,calls,expected,after,actual);
        }
    }
    mf_globals(before,1);
    return 0;
}
/* Calls target with three slots over 4 KiB of stack filled with `pattern`,
 * EAX..EDI distinct pattern words; the slot address in mf_origin, EDX:EAX
 * in out, the 1,024 bytes below the slots after return in mf_below. */
__attribute__((naked,noinline)) static void mf_track_run(void *target,const uint32_t *a,uint32_t pattern,uint32_t out[2]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 16(%ebp),%eax\n\tmovl $1024,%ecx\n\tcld\n\trep stosl\n\taddl $4084,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\tmovl %esp,_mf_origin\n\tmovl 16(%ebp),%eax\n\t"
        "movl %eax,%edx\n\txorl $0x01010101,%edx\n\tmovl %eax,%ecx\n\txorl $0x02020202,%ecx\n\tmovl %eax,%ebx\n\txorl $0x04040404,%ebx\n\t"
        "movl %eax,%esi\n\txorl $0x08080808,%esi\n\tmovl %eax,%edi\n\txorl $0x10101010,%edi\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%ebx\n\tmovl %eax,(%ebx)\n\tmovl %edx,4(%ebx)\n\t"
        "movl _mf_origin,%esi\n\tsubl $1024,%esi\n\tmovl $_mf_below,%edi\n\tmovl $256,%ecx\n\tcld\n\trep movsl\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* Installed probes through the entry shims: the original and the replaced
 * hook over the same filled stack; records with EBX/ESI/EDI, EBP/ESP
 * relative to the slots and the window below each callee; the 1,024 bytes
 * below the slots after return. */
static int mf_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    const int tracked=index!=ES2_HOOK_INDEX_order_setup;
    mf_track=tracked;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],want[2],got[2],pattern=poison?0xa7a7a7a7u:0x31313131u;mf_build(index,n*41u+3u,a,r);
        BYTE before[MF_GLOBALS],after[MF_GLOBALS],actual[MF_GLOBALS],expected_fpu[108],actual_fpu[108],below[1024];
        static mf_record expected[MF_MAX];
        memcpy(mf_before,mf_arena,MF_SIZE);mf_globals(before,0);
        mf_reset();
        if(tracked)mf_track_run(mapped+d->rva,a,pattern,want);else sg_poison_call(mapped+d->rva,a,pattern,want);
        es2_hook_fpu_save(expected_fpu);memcpy(below,mf_below,1024);
        const unsigned calls=mf_calls;memcpy(expected,mf_log,calls*sizeof(*expected));memcpy(mf_after,mf_arena,MF_SIZE);mf_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("mfd_orders poison install");
        memcpy(mf_arena,mf_before,MF_SIZE);mf_globals(before,1);mf_reset();
        if(tracked)mf_track_run(mapped+d->rva,a,pattern,got);else sg_poison_call(mapped+d->rva,a,pattern,got);
        es2_hook_fpu_save(actual_fpu);mf_globals(actual,0);
        uint64_t pair[2]={(uint64_t)want[1]<<32|want[0],(uint64_t)got[1]<<32|got[0]};
        if(pair[0]!=pair[1]||calls!=mf_calls||memcmp(expected,mf_log,calls*sizeof(*expected))||memcmp(mf_after,mf_arena,MF_SIZE)||
           memcmp(after,actual,MF_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=mf_report(fast?"installed fast":"installed preserving",n,pair,calls,expected,after,actual);
        else if(tracked&&memcmp(below,mf_below,1024)){
            for(unsigned j=0;j<1024;++j)if(below[j]!=mf_below[j]){fprintf(stderr,"FAIL: %s case %u installed: stack byte origin-%u %02x/%02x\n",
                d->name,n,1024-j,below[j],mf_below[j]);break;}
            status=1;
        }
        if(!es2_hook_uninstall(&h))return fail("mfd_orders poison uninstall");mf_globals(before,1);++total;
    }
    mf_track=0;
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status&&tracked)printf("PASS: %s %u installed cases through the entry shim, fast/preserving dispatch, 31/a7 stack, per-call EBX/ESI/EDI, EBP/ESP and the residue below each callee, the 1,024 bytes below the slots after return, nonempty x87, full pair/calls/arena/globals\n",
        d->name,total);
    else if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair, arena, globals\n",d->name,total);
    return status;
}
/* The real-callee chain: entry 0 mfd_order 447248(G), 1 mfd_button with
 * button 10, 2 mfd_click on the selected entry with 49dbe0 set, 3
 * mfd_button with button 8/7/9 (446ff0/447014, 446fc4, 446fb0 with
 * cockpit_next stubbed). The chain hooks of this group are uninstalled or
 * replaced at random per case; the all-uninstalled run on the same
 * filled stack is the reference. Only the member slot, the radio slots,
 * 446e60, cockpit_next, button_press's slot +8 and 4530bc are stubs. */
static int mf_chain_pass(unsigned index){
    static const unsigned chain[]={ES2_HOOK_INDEX_mfd_button,ES2_HOOK_INDEX_mfd_click,ES2_HOOK_INDEX_mfd_order,ES2_HOOK_INDEX_order_call,
        ES2_HOOK_INDEX_group_order,ES2_HOOK_INDEX_menu_code,ES2_HOOK_INDEX_menu_toggle,ES2_HOOK_INDEX_mfd_entry,
        ES2_HOOK_INDEX_mfd_target,ES2_HOOK_INDEX_mfd_view_next,ES2_HOOK_INDEX_mfd_range};
    enum { CHAIN=sizeof(chain)/sizeof(chain[0]) };
    BYTE saved[MF_IDS][5];int active[MF_IDS];
    if(mf_patch(saved,active,1u<<0|1u<<12|1u<<MF_RELEASE,1))return 1;
    int status=0;unsigned total=0,given[2]={0},refused[2]={0};uint64_t entered[CHAIN]={0};
    mf_track=1;
    for(unsigned n=0;n<4096&&!status;++n){
        const unsigned entry=index==ES2_HOOK_INDEX_mfd_order?0:index==ES2_HOOK_INDEX_mfd_button?(n/2%2?1:3):2;
        /* Alt+O's code 3 (menu entry 3, bit 1 clear) in one case of two. */
        const int alt_o=n%2==0;
        uint32_t a[6],r[3],want[2],got[2],pattern=n&4?0xa7a7a7a7u:0x31313131u;mf_build(ES2_HOOK_INDEX_mfd_order,n*67u+entry,a,r);
        BYTE *G=mf_arena+MF_G;
        /* Button 10 / the select gadget: button_press's button 0Ah at
         * G+40h with its own table (slot +8 the stub). */
        put32(G+0x18+40,MF_ADDR(MF_OBJ+0xe0));put32(mf_arena+MF_OBJ+0xe0+0x17,MF_ADDR(MF_OBJ+0xc0));
        put16(mf_arena+MF_OBJ+0xe0+0x1b,(uint16_t)(n%5?1:2));put32(mf_arena+MF_OBJ+0xc0+8,(uint32_t)(uintptr_t)mf_stubs[MF_BUTTON8]);
        put32(mf_arena+MF_H,MF_ADDR(MF_H+0x100));put32(G+0x14,MF_ADDR(MF_H));
        uint32_t target=0x447248u;
        if(entry==1){target=0x446844u;a[1]=get32(G+0x18+40);put32(G+0x68,13);mapped[0x9dbe0]=0;}
        else if(entry==3){static const unsigned b[]={8,7,9};target=0x446844u;put32(G+0x68,13);a[1]=get32(G+0x18+4*b[n/2%3]);
            if(n%4==1)put32(G+0xc1,0);}
        else if(entry==2){target=0x4470c0u;put32(G+0xc1,1);mapped[0x9dbe0]=1;
            const unsigned t=mf_next()%6;const BYTE *rc=mf_arena+MF_ENTRY+0x20*t+4;put32(G+0xb1,n%3?t:mf_next()%6);
            put32(mf_arena+MF_POINT,get32(rc));put32(mf_arena+MF_POINT+4,get32(rc+4));a[1]=mf_next();a[2]=MF_ADDR(MF_POINT);}
        if(alt_o){put32(mf_arena+MF_MENU+0x32,3);mf_arena[MF_MENU+0x2c+3]&=0xfdu;}
        BYTE before[MF_GLOBALS],after[MF_GLOBALS],actual[MF_GLOBALS],below[1024];static mf_record expected[MF_MAX];
        memcpy(mf_before,mf_arena,MF_SIZE);mf_globals(before,0);
        mf_reset();mf_track_run(mapped+target-0x400000u,a,pattern,want);memcpy(below,mf_below,1024);
        const unsigned calls=mf_calls;memcpy(expected,mf_log,calls*sizeof(*expected));memcpy(mf_after,mf_arena,MF_SIZE);mf_globals(after,0);
        es2_hook hk[CHAIN];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        o.mode=ES2_HOOK_REPLACED;const uint32_t mask=random32()|(n%7==0?0x7ffu:0u);
        for(unsigned j=0;j<CHAIN;++j)if(mask>>j&1&&!weapons_install_hook(hk+j,function_hook_descriptor(chain[j]),&o,function_hook_extension(chain[j])))
            return fail("mfd_orders chain install");
        es2_hook_set_fast_path(n%3!=0);
        memcpy(mf_arena,mf_before,MF_SIZE);mf_globals(before,1);mf_reset();
        mf_track_run(mapped+target-0x400000u,a,pattern,got);mf_globals(actual,0);
        es2_hook_set_fast_path(TRUE);
        uint64_t pair[2]={(uint64_t)want[1]<<32|want[0],(uint64_t)got[1]<<32|got[0]};
        if(pair[0]!=pair[1]||calls!=mf_calls||memcmp(expected,mf_log,calls*sizeof(*expected))||memcmp(mf_after,mf_arena,MF_SIZE)||
           memcmp(after,actual,MF_GLOBALS)){fprintf(stderr,"chain entry %u mask %03x\n",entry,mask&0x7ffu);
            status=mf_report("real chain",n,pair,calls,expected,after,actual);}
        else if(memcmp(below,mf_below,1024)){
            for(unsigned j=0;j<1024;++j)if(below[j]!=mf_below[j]){fprintf(stderr,"FAIL: %s case %u chain entry %u mask %03x: stack byte origin-%u %02x/%02x\n",
                current_function,n,entry,mask&0x7ffu,1024-j,below[j],mf_below[j]);break;}
            status=1;
        }
        for(unsigned j=0;j<CHAIN;++j){entered[j]+=hk[j].calls;if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("mfd_orders chain uninstall");}
        /* Alt+O's decision (entries 0 and 1 always reach the order): the
         * first pass gives it to an eligible member or to none. */
        if(alt_o&&entry<2){unsigned orders=0;for(unsigned j=0;j<calls;++j)orders+=expected[j].id==MF_ORDER;
            const unsigned k=pattern==0xa7a7a7a7u;if(orders)++given[k];else ++refused[k];}
        mf_globals(before,1);++total;
    }
    mf_track=0;
    if(mf_patch(saved,active,0,0))return 1;
    if(!status&&index!=ES2_HOOK_INDEX_mfd_click&&(!given[0]||given[1])){fprintf(stderr,"FAIL: %s chain: code 3 given %u/%u (31/a7 fill), refused %u/%u\n",
        current_function,given[0],given[1],refused[0],refused[1]);status=1;}
    if(!status)printf("PASS: %s %u real-chain cases (the chain hooks uninstalled or replaced at random, fast/preserving dispatch, 31/a7 stack; code 3: 31 fill %u given/%u refused, a7 fill %u given/%u refused; replaced entries %llu/%llu/%llu/%llu/%llu), pair/calls/arena/globals and the 1,024 bytes below the slots\n",
        current_function,total,given[0],refused[0],given[1],refused[1],(unsigned long long)entered[2],(unsigned long long)entered[3],
        (unsigned long long)entered[4],(unsigned long long)entered[5],(unsigned long long)entered[6]);
    return status;
}
/* order_setup over the real pool_prev 471c70 (a built backwards list of the
 * case's actors), the panels and the MFD list stubbed; order_setup and
 * pool_prev original or replaced. */
static int mf_setup_pass(void){
    BYTE saved[MF_IDS][5];int active[MF_IDS];
    if(mf_patch(saved,active,1u<<14|1u<<15,1))return 1;
    int status=0;unsigned total=0;uint64_t entered=0;
    for(unsigned mode=0;mode<4&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],want[2],got[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;mf_build(ES2_HOOK_INDEX_order_setup,n*31u+mode,a,r);
        /* The list: head +0Eh, nodes 8 bytes before each actor {next, prev}. */
        BYTE *L=mf_arena+MF_RECORD+0x10;const uint32_t La=MF_ADDR(MF_RECORD+0x10);put32(mapped+0xa9b7a,La);
        const unsigned k=1+n%MF_ACTORS;uint32_t prev=0;
        for(unsigned i=0;i<k;++i){BYTE *node=mf_arena+MF_ACTOR+MF_STRIDE*i-8;put32(node+4,prev?prev-8u:0u);put32(node,0);
            if(prev)put32((BYTE *)(uintptr_t)(prev-8u),MF_A(i)-8u);prev=MF_A(i);}
        put32(L+0xe,MF_A(0)-8u);
        BYTE before[MF_GLOBALS],after[MF_GLOBALS],actual[MF_GLOBALS];static mf_record expected[MF_MAX];
        memcpy(mf_before,mf_arena,MF_SIZE);mf_globals(before,0);
        mf_reset();sg_poison_call(mapped+0x31530,a,pattern,want);
        const unsigned calls=mf_calls;memcpy(expected,mf_log,calls*sizeof(*expected));memcpy(mf_after,mf_arena,MF_SIZE);mf_globals(after,0);
        es2_hook hk[2];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        o.mode=mode&1?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(hk,function_hook_descriptor(ES2_HOOK_INDEX_order_setup),&o,function_hook_extension(ES2_HOOK_INDEX_order_setup)))
            return fail("mfd_orders setup install");
        o.mode=mode&2?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(hk+1,function_hook_descriptor(ES2_HOOK_INDEX_pool_prev),&o,function_hook_extension(ES2_HOOK_INDEX_pool_prev)))
            return fail("mfd_orders pool_prev install");
        memcpy(mf_arena,mf_before,MF_SIZE);mf_globals(before,1);mf_reset();
        sg_poison_call(mapped+0x31530,a,pattern,got);mf_globals(actual,0);
        uint64_t pair[2]={(uint64_t)want[1]<<32|want[0],(uint64_t)got[1]<<32|got[0]};
        if(pair[0]!=pair[1]||calls!=mf_calls||memcmp(expected,mf_log,calls*sizeof(*expected))||memcmp(mf_after,mf_arena,MF_SIZE)||
           memcmp(after,actual,MF_GLOBALS)){fprintf(stderr,"setup mode %u\n",mode);status=mf_report("real setup",n,pair,calls,expected,after,actual);}
        entered+=hk[1].calls;
        if(!es2_hook_uninstall(hk+1)||!es2_hook_uninstall(hk))return fail("mfd_orders setup uninstall");
        mf_globals(before,1);++total;
    }
    if(mf_patch(saved,active,0,0))return 1;
    if(!status&&!entered){fprintf(stderr,"FAIL: order_setup real pass: pool_prev never entered\n");status=1;}
    if(!status)printf("PASS: order_setup %u real-callee cases over the real pool_prev (both hooks original/replaced, %llu pool_prev entries), 31/a7 stack, pair/calls/arena/globals\n",
        total,(unsigned long long)entered);
    return status;
}
static void mf_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){mf_build(index,n,a,r);}
static void mf_compare_state(BYTE *s,int restore){if(restore)memcpy(mf_arena,s,MF_COMPARE);else memcpy(s,mf_arena,MF_COMPARE);}
static int mfd_orders_cases(unsigned index,unsigned limit){
    current_function=function_hook_descriptor(index)->name;mf_index=index;
    BYTE old_globals[MF_GLOBALS];mf_globals(old_globals,0);
    if(limit&&compare_probe_installed(index)){
        const int status=compare_probe_run(index,limit,mf_compare_build,mf_compare_state,MF_COMPARE);
        mf_globals(old_globals,1);return status;
    }
    BYTE saved[MF_IDS][5];int active[MF_IDS];
    if(mf_patch(saved,active,mf_patch_set(index),1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_MFD_ORDERS_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;memset(mf_hits,0,sizeof(mf_hits));if(!limit)coverage_enable(1);mf_track=0;
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];mf_build(index,n,a,r);status=mf_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;mf_globals(old_globals,1);
    if(!limit&&!status)status=mf_installed_probes(index,index==ES2_HOOK_INDEX_order_setup?64:256);
    mf_globals(old_globals,1);
    if(mf_patch(saved,active,0,0))return 1;
    if(!limit&&!status&&(index==ES2_HOOK_INDEX_mfd_order||index==ES2_HOOK_INDEX_mfd_button||index==ES2_HOOK_INDEX_mfd_click))
        status=mf_chain_pass(index);
    if(!limit&&!status&&index==ES2_HOOK_INDEX_order_setup)status=mf_setup_pass();
    mf_globals(old_globals,1);
    if(!limit&&!status&&mf_leaf(index))status=compare_probe_run(index,0,mf_compare_build,mf_compare_state,MF_COMPARE);
    mf_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases (stub calls: page %u, range %u, view %u, target %u, order %u, button_press %u, select %u, code %u, order call %u, toggle %u, group order %u, dist_gate %u, cockpit_next %u, pool_prev %u, panels %u, MFD list %u, radio %u/%u, order slot %u); full return pair, ordered calls with entry registers, arena, globals%s\n",
        function_hook_descriptor(index)->name,cases,mf_hits[0],mf_hits[1],mf_hits[2],mf_hits[3],mf_hits[4],mf_hits[5],mf_hits[6],mf_hits[7],mf_hits[8],
        mf_hits[9],mf_hits[10],mf_hits[11],mf_hits[12],mf_hits[13],mf_hits[14],mf_hits[15],mf_hits[16],mf_hits[17],mf_hits[18],
        index==ES2_HOOK_INDEX_group_order?", the frame's locals and argument slots over the residue fill":"");
    return status;
}
