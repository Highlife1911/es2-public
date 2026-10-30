/* logic_frame group: the logic frame 45f570 (logic_frame)
 * (docs/reverse-engineering/main_loop.md, "the logic frame as own code"). A 13
 * KiB arena holds eleven pool headers (nine for the frame's pool globals, two
 * spares) with linked objects (8-byte link header, vtable LFR_VT with +14h/+18h
 * slot stubs, group pointer +45h, byte +99h), nine removal lists with arrays,
 * six groups (stride 7Ah), four side groups, two players, the control record R,
 * two counters and the dialog object (vtable +8 slot stub); the image globals
 * the frame reads and writes live in the mapped image's DATA (saved, compared
 * and restored around every case). Every callee (49630c, 45a810, the six pool
 * updates, 478628, 471c70, 4236c4, 423a74, 460870, 426b48, 426b70, 45de30,
 * 4123ac, 41aa5c, 45fa84, 47749c, 455950, 4543b0, 4131ac, 423f08) is a logging
 * stub patched into the private image and the three virtuals (+14h, +18h,
 * dialog +8) are slot stubs: a stub logs its arguments, the entry EAX/EDX/ECX
 * and EBX/ESI/EDI and the slots the bridge pushed, returns registers from a
 * per-case stream (471c70 walks the arena's real links; the updates request a
 * removal in one of three calls with AX, not EAX; 460870 ends the frame in one
 * of eight; 4131ac draws statuses around -1, 2, 3 and the table; the dialog's
 * +8 matches the status table in half the calls) and in one call of four
 * applies a side effect that the original reads again (freeze, step and tape
 * words, the player pointer and +99h, a pool or list global switched to a
 * spare, group and object flags, a new node at a pool's head, the counter
 * pointer); 49630c's stub links the record at EBP+[EAX+4] as the runtime does.
 *
 * Direct cases run the original body (EBX=ESI=EDI=0, the adapter's plain
 * path gets 0 as the caller's registers) and the adapter over the same
 * stubs and compare EDX:EAX, the ordered calls with all six registers and
 * slot counts, the arena, the globals, FS:0 and (first 4,096 cases) the
 * callee-counter deltas. The frame probe runs the original and the hook
 * installed through its entry shim (fast and preserving dispatch, 31/a7
 * stack, EAX..EDI distinct patterns, the real 49630c) and compares EAX,
 * EDX, ECX, every stub's EBX/ESI/EDI, EBP and ESP relative to the
 * caller's ESP, a hash of the 256 bytes below the stub's ESP (residue), the
 * FS:0 head and a hash of the head record's 20h bytes (the SEH chain the
 * callee sees), the arena, globals, FS:0 after return, the x87 environment
 * and the 1,024 bytes below the caller's ESP; one-slot stubs change their
 * argument slot in one call of four. The real-callee pass runs the frame
 * over the real 471c70, 471c18, 478628 and 49630c with logic_frame,
 * pool_prev, pool_next and removal_append original/replaced in all 16
 * combinations against the all-original run. Identifiers: lfr_/LFR_. */
enum { LFR_SIZE=0x3400, LFR_LISTS=9, LFR_POOLS=11, LFR_SLOTS=5, LFR_GROUPN=6, LFR_SIDES=4,
    LFR_LIST0=0x100, LFR_ARRAY0=0x180, LFR_GROUPS=0x600, LFR_SIDE0=0x900, LFR_VT=0x980, LFR_DVT=0x9c0,
    LFR_P1=0xa00, LFR_P2=0xaa0, LFR_R=0xb40, LFR_C1=0xb70, LFR_C2=0xb74, LFR_D1=0xb80, LFR_OBJ0=0xc00,
    LFR_IDS=27, LFR_MAX=256, LFR_GLOBALS=88 };
#define LFR_BASE ((uint32_t)(uintptr_t)lfr_arena)
#define LFR_ADDR(o) (LFR_BASE+(uint32_t)(o))
#define LFR_POOL(k) (0x40u+0x10u*(uint32_t)(k))
#define LFR_LIST(k) (LFR_LIST0+0x8u*(uint32_t)(k))
#define LFR_ARRAY(k) (LFR_ARRAY0+0x80u*(uint32_t)(k))
#define LFR_SIDE(k) (LFR_SIDE0+0x20u*(uint32_t)(k))
/* Object i of pool k; its link node is 8 bytes below. */
#define LFR_OBJ(k,i) (LFR_OBJ0+0xa8u*(LFR_SLOTS*(uint32_t)(k)+(uint32_t)(i))+8u)
static BYTE lfr_arena[LFR_SIZE],lfr_before[LFR_SIZE],lfr_after[LFR_SIZE];
typedef struct lfr_record { uint32_t id,a[4],r[3],cs[3],slots,frame[2],hash,seh[2]; } lfr_record;
static lfr_record lfr_log[LFR_MAX];
static unsigned lfr_calls,lfr_track,lfr_hash_on,lfr_clobbers,lfr_paths[LFR_IDS];
static unsigned lfr_used[LFR_POOLS],lfr_used0[LFR_POOLS];
static uint32_t lfr_seed,lfr_stream,lfr_status;
/* Stub entry: EBP, ESP, the saved ESI/EDI/ECX while the window is copied,
 * the ECX a stub returns, the caller's ESP before the CALL of the tracked
 * run, the 256 bytes below the stub's ESP, the 1,024 bytes below the
 * caller's ESP after the tracked run. */
__attribute__((used)) static uint32_t lfr_entry_ebp,lfr_entry_esp,lfr_tmp[3],lfr_ecx,lfr_origin,lfr_window_on;
__attribute__((used)) static BYTE lfr_window[256],lfr_frame_copy[1024];
/* ids 0..23 patched callees (VA); 24..26 the slots +14h, +18h (LFR_VT) and
 * the dialog's +8 (LFR_DVT). */
static const uint32_t lfr_addr[LFR_IDS]={0x49630c,0x45a810,0x40813c,0x4091cc,0x4092dc,0x46b678,0x408bd8,0x409d2c,
    0x478628,0x471c70,0x4236c4,0x423a74,0x460870,0x426b48,0x426b70,0x45de30,0x4123ac,0x41aa5c,0x45fa84,0x47749c,
    0x455950,0x4543b0,0x4131ac,0x423f08,0,0,0};
static const unsigned lfr_arity[LFR_IDS]={0,0,1,1,1,1,1,1,2,2,1,1,1,0,1,0,0,1,0,1,4,2,1,1,1,1,1};
static const unsigned lfr_slotkey[LFR_IDS]={[24]=0x14,[25]=0x18,[26]=0x8};
/* The frame's pool globals (explosion, smoke ball, smoke, fire, debris,
 * meteor, projectile, actor, MECH) and removal lists (the first seven). */
static const uint32_t lfr_pool_rva[9]={0xa961e,0xa966e,0xa9684,0x2b4f30,0xa9642,0xa96aa,0xa96c2,0xa95a0,0xa9b7a};
static const uint32_t lfr_list_rva[7]={0xa9622,0xa9672,0xa9688,0x2b4f34,0xa9646,0xa96ae,0xa96c6};
static const struct { uint32_t rva,size; } lfr_global_list[]={
    {0xd24d6,2},{0xd3b64,2},{0xd24f2,2},{0xa961e,4},{0xa9622,4},{0xa966e,4},{0xa9672,4},{0xa9684,4},{0xa9688,4},
    {0x2b4f30,4},{0x2b4f34,4},{0xa9642,4},{0xa9646,4},{0xa96aa,4},{0xa96ae,4},{0xd2526,2},{0xa96c2,4},{0xa96c6,4},
    {0xa95a0,4},{0xa9e00,4},{0xa9e04,2},{0xd24fc,2},{0xd24e6,4},{0xa9b7a,4},{0x2cbee4,4}};
static void lfr_globals(BYTE *out,int restore){
    BYTE *p=out;
    for(unsigned i=0;i<sizeof(lfr_global_list)/sizeof(lfr_global_list[0]);++i){
        if(restore)memcpy(mapped+lfr_global_list[i].rva,p,lfr_global_list[i].size);
        else memcpy(p,mapped+lfr_global_list[i].rva,lfr_global_list[i].size);
        p+=lfr_global_list[i].size;
    }
}
static uint32_t lfr_next(void){
    uint32_t z=lfr_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static uint32_t lfr_pick(uint32_t bound){return lfr_next()%bound;}
static uint32_t lfr_fs0(void){uint32_t v;__asm__ volatile("movl %%fs:0,%0":"=r"(v));return v;}
static uint32_t lfr_hash(const BYTE *p,unsigned n){uint32_t h=2166136261u;for(unsigned i=0;i<n;++i)h=(h^p[i])*16777619u;return h;}
/* A host pointer for an arena address with n readable bytes, else NULL. */
static BYTE *lfr_host(uint32_t address,uint32_t n){return address-LFR_BASE<=LFR_SIZE-n?lfr_arena+(address-LFR_BASE):NULL;}
static void lfr_reset(void){
    lfr_calls=0;lfr_stream=lfr_seed;lfr_status=0;memcpy(lfr_used,lfr_used0,sizeof(lfr_used));
}
/* 471c70's walk over the arena's links: the previous object of O, or (O =
 * 0) the tail of P's used list. */
static uint32_t lfr_walk(uint32_t pool,uint32_t object){
    if(object){const BYTE *n=lfr_host(object-8u,8);if(!n)return 0;const uint32_t prev=get32(n+4);return prev?prev+8u:0;}
    const BYTE *h=lfr_host(pool,0x12);if(!h)return 0;
    uint32_t node=get32(h+0xe),last=0;
    for(unsigned guard=0;node&&guard<64;++guard){const BYTE *n=lfr_host(node,8);if(!n)break;last=node;node=get32(n);}
    return last?last+8u:0;
}
/* A new object at the head of pool k (an allocation during the frame). */
static void lfr_insert(unsigned k){
    if(lfr_used[k]>=LFR_SLOTS)return;
    BYTE *h=lfr_arena+LFR_POOL(k);
    const uint32_t node=LFR_ADDR(LFR_OBJ(k,lfr_used[k]))-8u,head=get32(h+0xe);
    BYTE *n=lfr_arena+(node-LFR_BASE);put32(n,head);put32(n+4,0);
    if(head){BYTE *o=lfr_host(head,8);if(o)put32(o+4,node);}
    put32(h+0xe,node);++lfr_used[k];
}
/* One callee side effect (about one call in four), drawn from the stream. */
static void lfr_effect(void){
    const uint32_t pick=lfr_next();if(pick%4)return;
    switch(pick/4%12){
    case 0:put16(mapped+0xd24f2,(uint16_t)(lfr_pick(3)?lfr_pick(2):lfr_next()));break;
    case 1:put16(mapped+0xd24fc,(uint16_t)(lfr_pick(2)?0:lfr_next()|1u));break;
    case 2:put16(mapped+0xd24d6,(uint16_t)lfr_pick(2));break;
    case 3:put32(mapped+0xd24e6,get32(mapped+0xd24e6)==LFR_ADDR(LFR_P1)?LFR_ADDR(LFR_P2):LFR_ADDR(LFR_P1));break;
    case 4:{BYTE *p=lfr_host(get32(mapped+0xd24e6),0xa0);if(p)p[0x99]^=1;break;}
    case 5:{const unsigned k=lfr_pick(9);put32(mapped+lfr_pool_rva[k],LFR_ADDR(lfr_pick(2)?LFR_POOL(9+lfr_pick(2)):LFR_POOL(k)));break;}
    case 6:{const unsigned k=lfr_pick(7);put32(mapped+lfr_list_rva[k],LFR_ADDR(LFR_LIST(lfr_pick(2)?7+lfr_pick(2):k)));break;}
    case 7:{BYTE *g=lfr_arena+LFR_SIDE(lfr_pick(LFR_SIDES));put32(g+0x14,get32(g+0x14)?0:lfr_next()|1u);break;}
    case 8:{const unsigned k=lfr_pick(LFR_POOLS),i=lfr_pick(LFR_SLOTS);lfr_arena[LFR_OBJ(k,i)+0x99]^=1;break;}
    case 9:lfr_insert(lfr_pick(LFR_POOLS));break;
    case 10:put32(mapped+0x2cbee4,LFR_ADDR(lfr_pick(2)?LFR_C1:LFR_C2));break;
    default:{BYTE *g=lfr_arena+LFR_GROUPS+0x7au*lfr_pick(LFR_GROUPN);put32(g+0x14,get32(g+0x14)?0:lfr_next()|1u);break;}
    }
}
__attribute__((used,noinline)) static uint64_t __cdecl lfr_log_impl(unsigned id,const uint32_t *regs,uint32_t *a){
    if(lfr_calls==LFR_MAX){fail("logic_frame synthetic call overflow");ExitProcess(1);}
    lfr_record *v=lfr_log+lfr_calls++;memset(v,0,sizeof(*v));v->id=id;
    for(unsigned j=0;j<lfr_arity[id]&&j<4;++j)v->a[j]=a[j];
    for(unsigned j=0;j<3;++j){v->r[j]=regs[j];v->cs[j]=regs[3+j];}
    v->slots=weapons_bridge_slots(a,lfr_arity[id]);
    if(lfr_track){
        v->frame[0]=lfr_entry_ebp-lfr_origin;v->frame[1]=lfr_entry_esp-lfr_origin;
        if(lfr_hash_on)v->hash=lfr_hash(lfr_window,256);
        const uint32_t head=lfr_fs0();v->seh[0]=head-lfr_origin;v->seh[1]=lfr_hash((const BYTE *)(uintptr_t)head,0x20);
    }
    uint32_t eax=lfr_next(),edx=lfr_next();const uint32_t x=lfr_next();lfr_ecx=lfr_next();
    switch(id){
    case 1:eax=LFR_ADDR(LFR_R);break;                        /* 45a810: R */
    case 2:case 3:case 4:case 5:case 6:case 7:case 24:     /* updates: removal in one of three (AX) */
        if(x%3)eax&=0xffff0000u;break;
    case 9:eax=lfr_walk(a[0],a[1]);break;                  /* 471c70 */
    case 12:if(x%8)eax&=0xffff0000u;break;                 /* 460870: AX != 0 ends the frame */
    case 18:eax=x%4==0?0:x%4==1?0x10000u:eax|1u;break;     /* 45fa84: full EAX tested */
    case 19:eax=LFR_ADDR(LFR_D1);break;                     /* 47749c: never null (PROVED) */
    case 22:{const uint32_t k=x%16;                        /* 4131ac: the status word */
        const uint32_t st=k<4?(uint32_t)(-1-(int32_t)((x>>8)%3)):k<6?2u:k<8?3u:k<12?(x>>8)%20u:(x>>8);
        eax=(eax&0xffff0000u)|(st&0xffffu);lfr_status=eax;break;}
    case 23:if(x&1){BYTE *p=lfr_host(get32(mapped+0xd24e6),0xa0);if(p)p[0x99]=1;}break;   /* 423f08 kills */
    case 26:if(x&1)eax=(uint32_t)(int32_t)(int16_t)get16(mapped+(uint32_t)(0x9f5c0+2*(int32_t)(int16_t)lfr_status));
        else if(x&2)eax=x>>2&1u;break;                     /* dialog +8: the status match, 0 or 1 */
    default:break;
    }
    if(lfr_track&&lfr_arity[id]==1&&lfr_pick(4)==0){a[0]=lfr_next();++lfr_clobbers;}   /* the callee changes its slot */
    lfr_effect();
    return (uint64_t)edx<<32|eax;
}
/* EBP and ESP at entry; with lfr_window_on the 256 bytes below ESP; then
 * EDI, ESI, EBX, ECX, EDX, EAX (the register block) before the argument
 * pointer, the block pointer and the id. */
#define LFR_STUB_HEAD \
    "movl %ebp,_lfr_entry_ebp\n\tmovl %esp,_lfr_entry_esp\n\tcmpl $0,_lfr_window_on\n\tje 1f\n\t" \
    "movl %esi,_lfr_tmp\n\tmovl %edi,_lfr_tmp+4\n\tmovl %ecx,_lfr_tmp+8\n\t" \
    "leal -256(%esp),%esi\n\tmovl $_lfr_window,%edi\n\tmovl $64,%ecx\n\tcld\n\trep movsl\n\t" \
    "movl _lfr_tmp,%esi\n\tmovl _lfr_tmp+4,%edi\n\tmovl _lfr_tmp+8,%ecx\n\t" \
    "1: pushl %edi\n\tpushl %esi\n\tpushl %ebx\n\tpushl %ecx\n\tpushl %edx\n\tpushl %eax\n\t" \
    "leal 28(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
#define LFR_STUB(n) __attribute__((naked)) static void lfr_stub_##n(void){__asm__ volatile( \
    LFR_STUB_HEAD "pushl $" #n "\n\tcalll _lfr_log_impl\n\taddl $36,%esp\n\tmovl _lfr_ecx,%ecx\n\tret\n\t");}
LFR_STUB(1) LFR_STUB(2) LFR_STUB(3) LFR_STUB(4) LFR_STUB(5) LFR_STUB(6) LFR_STUB(7) LFR_STUB(8) LFR_STUB(9)
LFR_STUB(10) LFR_STUB(11) LFR_STUB(12) LFR_STUB(13) LFR_STUB(14) LFR_STUB(15) LFR_STUB(16) LFR_STUB(17) LFR_STUB(18)
LFR_STUB(19) LFR_STUB(20) LFR_STUB(21) LFR_STUB(22) LFR_STUB(23) LFR_STUB(24) LFR_STUB(25) LFR_STUB(26)
#undef LFR_STUB
/* 49630c: log, then link the record at EBP+[EAX+4] into FS:0 (saved head,
 * context word 0) as the runtime does; EAX = the old head, EDX/ECX kept. */
__attribute__((naked)) static void lfr_stub_0(void){__asm__ volatile(
    LFR_STUB_HEAD "pushl $0\n\tcalll _lfr_log_impl\n\taddl $12,%esp\n\t"
    "movl (%esp),%ecx\n\tmovl 4(%ecx),%ecx\n\taddl %ebp,%ecx\n\t"
    "movl %fs:0,%eax\n\tmovl %eax,(%ecx)\n\tmovw $0,16(%ecx)\n\tmovl %ecx,%fs:0\n\t"
    "movl 4(%esp),%edx\n\tmovl 8(%esp),%ecx\n\taddl $24,%esp\n\tret\n\t");}
#undef LFR_STUB_HEAD
static void *lfr_stubs[LFR_IDS]={lfr_stub_0,lfr_stub_1,lfr_stub_2,lfr_stub_3,lfr_stub_4,lfr_stub_5,lfr_stub_6,lfr_stub_7,
    lfr_stub_8,lfr_stub_9,lfr_stub_10,lfr_stub_11,lfr_stub_12,lfr_stub_13,lfr_stub_14,lfr_stub_15,lfr_stub_16,lfr_stub_17,
    lfr_stub_18,lfr_stub_19,lfr_stub_20,lfr_stub_21,lfr_stub_22,lfr_stub_23,lfr_stub_24,lfr_stub_25,lfr_stub_26};
/* Patches the callees of `which` (bit per id) with their stubs (on) or
 * restores them. */
#define LFR_ALL ((1u<<24)-1u)
#define LFR_PROBE (LFR_ALL&~1u)                               /* the real 49630c */
#define LFR_REAL (LFR_ALL&~((1u<<0)|(1u<<8)|(1u<<9)))         /* real 49630c, 478628, 471c70 */
static int lfr_patch(BYTE saved[LFR_IDS][5],int active[LFR_IDS],uint32_t which,int on){
    for(unsigned i=0;i<LFR_IDS;++i){
        if(!lfr_addr[i]){active[i]=0;continue;}
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(lfr_addr[i]-0x400000u,lfr_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(lfr_addr[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
/* A complete world: the arena filled from the case stream, then every
 * structure the frame and the real callees read; regs[0..2] the entry
 * EAX/EDX/ECX (EBX/ESI/EDI are the caller's, set by the runner). */
static void lfr_build(unsigned n,uint32_t regs[6]){
    lfr_seed=random32();lfr_stream=lfr_seed^n;
    for(unsigned i=0;i<LFR_SIZE;i+=4)put32(lfr_arena+i,lfr_next());
    for(unsigned i=0;i<6;++i)regs[i]=lfr_next();
    /* vtables */
    put32(lfr_arena+LFR_VT+0x14,(uint32_t)(uintptr_t)lfr_stubs[24]);put32(lfr_arena+LFR_VT+0x18,(uint32_t)(uintptr_t)lfr_stubs[25]);
    put32(lfr_arena+LFR_DVT+8,(uint32_t)(uintptr_t)lfr_stubs[26]);put32(lfr_arena+LFR_D1,LFR_ADDR(LFR_DVT));
    /* side groups, objects, pools (links: head = newest, iteration from the tail) */
    for(unsigned k=0;k<LFR_SIDES;++k)put32(lfr_arena+LFR_SIDE(k)+0x14,lfr_pick(4)?0:lfr_next()|1u);
    for(unsigned k=0;k<LFR_POOLS;++k){
        for(unsigned i=0;i<LFR_SLOTS;++i){
            BYTE *o=lfr_arena+LFR_OBJ(k,i);put32(o,LFR_ADDR(LFR_VT));put32(o+0x45,LFR_ADDR(LFR_SIDE(lfr_pick(LFR_SIDES))));
            o[0x99]=(BYTE)(lfr_pick(4)==0);
        }
        const unsigned count=k>=9?lfr_pick(3):lfr_pick(4)==0?0:1+lfr_pick(3);
        put32(lfr_arena+LFR_POOL(k)+0xe,0);lfr_used0[k]=lfr_used[k]=0;
        for(unsigned i=0;i<count;++i)lfr_insert(k);
        lfr_used0[k]=lfr_used[k];
    }
    for(unsigned k=0;k<9;++k)put32(mapped+lfr_pool_rva[k],LFR_ADDR(LFR_POOL(k)));
    for(unsigned k=0;k<LFR_LISTS;++k){put32(lfr_arena+LFR_LIST(k),LFR_ADDR(LFR_ARRAY(k)));put16(lfr_arena+LFR_LIST(k)+4,(uint16_t)lfr_pick(4));}
    for(unsigned k=0;k<7;++k)put32(mapped+lfr_list_rva[k],LFR_ADDR(LFR_LIST(k)));
    /* groups: pending (+14h) in one of three; a null array in one of 64 */
    for(unsigned k=0;k<LFR_GROUPN;++k)put32(lfr_arena+LFR_GROUPS+0x7a*k+0x14,lfr_pick(3)?0:lfr_next()|1u);
    put32(mapped+0xa9e00,lfr_pick(64)?LFR_ADDR(LFR_GROUPS):0);put16(mapped+0xa9e04,(uint16_t)lfr_pick(LFR_GROUPN+1));
    /* players: dead (+99h) in one of three */
    for(unsigned k=0;k<2;++k){BYTE *p=lfr_arena+(k?LFR_P2:LFR_P1);put32(p+0x45,LFR_ADDR(LFR_SIDE(lfr_pick(LFR_SIDES))));p[0x99]=(BYTE)(lfr_pick(3)==0);}
    put32(mapped+0xd24e6,LFR_ADDR(LFR_P1));
    put32(mapped+0x2cbee4,LFR_ADDR(LFR_C1));
    /* words: tape, freeze, step in one of four */
    put16(mapped+0xd24d6,(uint16_t)(lfr_pick(4)?0:lfr_next()|1u));
    put16(mapped+0xd24f2,(uint16_t)(lfr_pick(4)?0:lfr_pick(2)?1u:lfr_next()|1u));
    put16(mapped+0xd24fc,(uint16_t)(lfr_pick(4)?0:lfr_next()|1u));
    put16(mapped+0xd3b64,(uint16_t)lfr_next());put16(mapped+0xd2526,(uint16_t)lfr_next());
    lfr_stream=lfr_seed^~n;                                 /* the stubs' stream restarts here in both runs */
    lfr_seed=lfr_stream;
}
static unsigned lfr_hits(const callee_counter *c){
    unsigned h=0;
    for(unsigned j=0;j<lfr_calls;++j){const unsigned id=lfr_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?(lfr_slotkey[id]==c->key):(lfr_slotkey[id]==0&&lfr_addr[id]==c->key))++h;}
    return h;
}
static int lfr_report(const char *what,unsigned n,const uint32_t want[3],const uint32_t got[3],unsigned calls,const lfr_record *e,
    const BYTE *globals_want,const BYTE *globals_got){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x calls %u/%u\n",current_function,n,what,
        want[0],got[0],want[1],got[1],want[2],got[2],calls,lfr_calls);
    for(unsigned j=0;j<calls&&j<lfr_calls&&e;++j)if(memcmp(e+j,lfr_log+j,sizeof(*e))){
        const uint32_t *x=(const uint32_t*)(e+j),*y=(const uint32_t*)(lfr_log+j);
        for(unsigned k=0;k<sizeof(*e)/4;++k)if(x[k]!=y[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,e[j].id,k,x[k],y[k]);break;}
    for(unsigned j=0;j<LFR_SIZE;++j)if(lfr_after[j]!=lfr_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,lfr_after[j],lfr_arena[j]);break;}
    for(unsigned j=0;globals_want&&j<LFR_GLOBALS;++j)if(globals_want[j]!=globals_got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,globals_want[j],globals_got[j]);break;}
    return 1;
}
/* Calls target (no argument slot) with EAX..EDI = regs[0..5] over 2 KiB of
 * stack filled with `fill`; EAX, EDX, ECX into out[0..2]. */
__attribute__((naked,noinline)) static void lfr_run(void *target,const uint32_t *regs,uint32_t out[3],uint32_t fill){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $2048,%esp\n\tmovl %esp,%edi\n\tmovl 20(%ebp),%eax\n\tmovl $512,%ecx\n\tcld\n\trep stosl\n\taddl $2048,%esp\n\t"
        "movl 12(%ebp),%eax\n\tmovl 12(%eax),%ebx\n\tmovl 16(%eax),%esi\n\tmovl 20(%eax),%edi\n\t"
        "movl 4(%eax),%edx\n\tmovl 8(%eax),%ecx\n\tmovl (%eax),%eax\n\tcall *8(%ebp)\n\t"
        "movl 16(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* Calls target (no argument slot) over a 4-KiB fill with EAX..EDI distinct
 * pattern words; lfr_origin = the caller's ESP before the CALL; EAX, EDX,
 * ECX into out; the 1,024 bytes below the origin after return into
 * lfr_frame_copy. */
__attribute__((naked,noinline)) static void lfr_track_run(void *target,uint32_t pattern,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 12(%ebp),%eax\n\tmovl $1024,%ecx\n\tcld\n\trep stosl\n\taddl $4096,%esp\n\t"
        "movl %esp,_lfr_origin\n\tmovl 12(%ebp),%eax\n\t"
        "movl %eax,%edx\n\txorl $0x01010101,%edx\n\tmovl %eax,%ecx\n\txorl $0x02020202,%ecx\n\tmovl %eax,%ebx\n\txorl $0x04040404,%ebx\n\t"
        "movl %eax,%esi\n\txorl $0x08080808,%esi\n\tmovl %eax,%edi\n\txorl $0x10101010,%edi\n\tcall *8(%ebp)\n\t"
        "movl 16(%ebp),%ebx\n\tmovl %eax,(%ebx)\n\tmovl %edx,4(%ebx)\n\tmovl %ecx,8(%ebx)\n\t"
        "leal -1024(%esp),%esi\n\tmovl $_lfr_frame_copy,%edi\n\tmovl $256,%ecx\n\tcld\n\trep movsl\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* Direct case: the original body and the adapter (plain path) over the
 * same stubs. The caller's EBX/ESI/EDI are 0 in both. */
static int lfr_compare(unsigned index,unsigned n,uint32_t regs[6]){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[LFR_GLOBALS],after[LFR_GLOBALS],actual[LFR_GLOBALS];static lfr_record expected[LFR_MAX];
    const uint32_t head=lfr_fs0(),dummy[1]={0};uint32_t want[3],got[3]={0,0,0};
    regs[3]=regs[4]=regs[5]=0;
    memcpy(lfr_before,lfr_arena,LFR_SIZE);lfr_globals(before,0);
    lfr_reset();lfr_run(mapped+d->rva,regs,want,0x01010101u*(n&0xff));
    const unsigned calls=lfr_calls;memcpy(expected,lfr_log,calls*sizeof(*expected));memcpy(lfr_after,lfr_arena,LFR_SIZE);lfr_globals(after,0);
    if(lfr_fs0()!=head)return fail("logic_frame original left FS:0 changed");
    for(unsigned j=0;j<calls;++j)lfr_paths[expected[j].id]++;
    memcpy(lfr_arena,lfr_before,LFR_SIZE);lfr_globals(before,1);lfr_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[32]={0};
    for(unsigned i=0;i<count&&i<32;++i)counts[i]=c[i].count;
    const uint64_t pair=x->replacement_context(dummy,d->user,regs);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);
    lfr_globals(actual,0);
    if(lfr_fs0()!=head)return fail("logic_frame adapter left FS:0 changed");
    if(n<4096)for(unsigned i=0;i<count&&i<32;++i)if(c[i].count-counts[i]!=lfr_hits(c+i))return fail("logic_frame callee counter delta");
    want[2]=got[2]=0;                                       /* ECX: exact through the shim only */
    if(want[0]!=got[0]||want[1]!=got[1]||calls!=lfr_calls||memcmp(expected,lfr_log,calls*sizeof(*expected))||
       memcmp(lfr_after,lfr_arena,LFR_SIZE)||memcmp(after,actual,LFR_GLOBALS))
        return lfr_report("direct",n,want,got,calls,expected,after,actual);
    lfr_globals(before,1);
    return 0;
}
/* Frame probe through the entry shim: the original 45f570 (real 49630c)
 * and the installed replacement must agree in EAX/EDX/ECX, the records
 * (EBX/ESI/EDI, EBP/ESP relative to the caller's ESP, the residue hash
 * below each callee, the FS:0 head and its record), the arena, the
 * globals, FS:0 after return, the x87 environment and the 1,024 bytes
 * below the caller's ESP. */
static int lfr_frame_probe(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    static lfr_record expected[LFR_MAX];static BYTE expected_frame[1024];
    lfr_track=1;lfr_hash_on=1;lfr_window_on=1;lfr_clobbers=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t regs[6],want[3],got[3];const uint32_t pattern=poison?0xa7a7a7a7u:0x31313131u,head=lfr_fs0();lfr_build(n*41u+3u,regs);
        BYTE before[LFR_GLOBALS],after[LFR_GLOBALS],actual[LFR_GLOBALS],efpu[108],afpu[108];
        memcpy(lfr_before,lfr_arena,LFR_SIZE);lfr_globals(before,0);
        lfr_reset();lfr_track_run(mapped+d->rva,pattern,want);es2_hook_fpu_save(efpu);memcpy(expected_frame,lfr_frame_copy,1024);
        const unsigned calls=lfr_calls;memcpy(expected,lfr_log,calls*sizeof(*expected));memcpy(lfr_after,lfr_arena,LFR_SIZE);lfr_globals(after,0);
        if(lfr_fs0()!=head){status=fail("logic_frame original left FS:0 changed");break;}
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("logic_frame frame install");
        memcpy(lfr_arena,lfr_before,LFR_SIZE);lfr_globals(before,1);lfr_reset();
        lfr_track_run(mapped+d->rva,pattern,got);es2_hook_fpu_save(afpu);lfr_globals(actual,0);
        if(lfr_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||want[2]!=got[2]||calls!=lfr_calls||memcmp(expected,lfr_log,calls*sizeof(*expected))||
           memcmp(lfr_after,lfr_arena,LFR_SIZE)||memcmp(after,actual,LFR_GLOBALS)||memcmp(efpu,afpu,12)||memcmp(efpu+28,afpu+28,80)||h.calls!=1)
            status=lfr_report(fast?"frame fast":"frame preserving",n,want,got,calls,expected,after,actual);
        else if(memcmp(expected_frame,lfr_frame_copy,1024)){
            fprintf(stderr,"FAIL: %s case %u (frame window)\n",current_function,n);
            for(unsigned j=0;j<1024;++j)if(expected_frame[j]!=lfr_frame_copy[j]){fprintf(stderr," below-%u: %02x/%02x\n",1024-j,expected_frame[j],lfr_frame_copy[j]);break;}
            status=1;}
        if(!es2_hook_uninstall(&h))return fail("logic_frame frame uninstall");lfr_globals(before,1);++total;
    }
    lfr_track=0;lfr_hash_on=0;lfr_window_on=0;
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status&&!lfr_clobbers){fprintf(stderr,"FAIL: %s frame probe: no changed argument slot was observed\n",current_function);status=1;}
    if(!status)printf("PASS: %s %u frame cases through the entry shim, fast/preserving dispatch, 31/a7 stack, real 49630c, per-call EAX..EDI, EBP/ESP, residue below each callee, FS:0 head and record, changed argument slots (%u in both runs), EAX/EDX/ECX, nonempty x87, 1024 bytes below the caller\n",
        d->name,total,lfr_clobbers);
    return status;
}
/* Real-callee pass: 45f570 over the real 471c70, 471c18, 478628 and
 * 49630c (the rest stubbed), logic_frame, pool_prev, pool_next and
 * removal_append original/replaced in all 16 combinations against the
 * all-original run. */
static int lfr_real(unsigned index){
    const unsigned set[4]={index,ES2_HOOK_INDEX_pool_prev,ES2_HOOK_INDEX_pool_next,ES2_HOOK_INDEX_removal_append};
    BYTE saved[LFR_IDS][5];int active[LFR_IDS];if(lfr_patch(saved,active,LFR_REAL,1))return 1;
    int status=0;unsigned total=0;uint64_t entered[4]={0,0,0,0};static lfr_record expected[LFR_MAX];
    lfr_track=1;
    for(unsigned mode=0;mode<16&&!status;++mode)for(unsigned n=0;n<64&&!status;++n){
        uint32_t regs[6],want[3],got[3];const uint32_t pattern=n&1?0xa7a7a7a7u:0x31313131u,head=lfr_fs0();lfr_build(n*67u+mode*7u,regs);
        BYTE before[LFR_GLOBALS],after[LFR_GLOBALS],actual[LFR_GLOBALS];
        memcpy(lfr_before,lfr_arena,LFR_SIZE);lfr_globals(before,0);
        lfr_reset();lfr_track_run(mapped+function_hook_descriptor(index)->rva,pattern,want);
        const unsigned calls=lfr_calls;memcpy(expected,lfr_log,calls*sizeof(*expected));memcpy(lfr_after,lfr_arena,LFR_SIZE);lfr_globals(after,0);
        es2_hook hk[4];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<4;++j){o.mode=(mode>>j&1)?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(set[j]),&o,function_hook_extension(set[j])))return fail("logic_frame real install");}
        memcpy(lfr_arena,lfr_before,LFR_SIZE);lfr_globals(before,1);lfr_reset();
        lfr_track_run(mapped+function_hook_descriptor(index)->rva,pattern,got);lfr_globals(actual,0);
        if(lfr_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||want[2]!=got[2]||calls!=lfr_calls||memcmp(expected,lfr_log,calls*sizeof(*expected))||
           memcmp(lfr_after,lfr_arena,LFR_SIZE)||memcmp(after,actual,LFR_GLOBALS)){
            fprintf(stderr,"real mode=%u\n",mode);status=lfr_report("real",n,want,got,calls,expected,after,actual);}
        for(unsigned j=0;j<4;++j){if(mode>>j&1)entered[j]+=hk[j].calls;
            if(!es2_hook_uninstall(hk+j))return fail("logic_frame real uninstall");}
        lfr_globals(before,1);++total;
    }
    lfr_track=0;
    if(lfr_patch(saved,active,0,0))return 1;
    if(!status&&(!entered[0]||!entered[1]||!entered[2]||!entered[3])){
        fprintf(stderr,"FAIL: %s real pass: entered %llu/%llu/%llu/%llu\n",current_function,(unsigned long long)entered[0],
            (unsigned long long)entered[1],(unsigned long long)entered[2],(unsigned long long)entered[3]);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases over the real 471c70/471c18/478628/49630c (logic_frame, pool_prev, pool_next, removal_append original/replaced in all 16 combinations; replaced entries %llu/%llu/%llu/%llu), 31/a7 stack, EAX/EDX/ECX, calls with EBX/ESI/EDI, EBP/ESP and FS:0 record, arena, globals\n",
        current_function,total,(unsigned long long)entered[0],(unsigned long long)entered[1],(unsigned long long)entered[2],(unsigned long long)entered[3]);
    return status;
}
static int logic_frame_cases(unsigned index,unsigned limit){
    unsigned bytes=0;for(unsigned i=0;i<sizeof(lfr_global_list)/sizeof(lfr_global_list[0]);++i)bytes+=lfr_global_list[i].size;
    if(bytes!=LFR_GLOBALS)return fail("logic_frame global list size");
    BYTE old_globals[LFR_GLOBALS];lfr_globals(old_globals,0);
    BYTE saved[LFR_IDS][5];int active[LFR_IDS];
    if(lfr_patch(saved,active,LFR_ALL,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_LOGIC_FRAME_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);memset(lfr_paths,0,sizeof(lfr_paths));lfr_track=0;lfr_hash_on=0;lfr_window_on=0;
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==32768)coverage_enable(0);
        uint32_t regs[6];lfr_build(n,regs);status=lfr_compare(index,n,regs);}
    if(!limit)coverage_enable(0);current_case_valid=0;
    lfr_globals(old_globals,1);
    if(lfr_patch(saved,active,LFR_ALL,0))return 1;
    if(!limit&&!status){
        if(lfr_patch(saved,active,LFR_PROBE,1))return 1;
        status=lfr_frame_probe(index,256);
        if(lfr_patch(saved,active,LFR_PROBE,0))return 1;
        lfr_globals(old_globals,1);
    }
    if(!limit&&!status)status=lfr_real(index);
    lfr_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases (stub calls 49630c %u, 45a810 %u, updates %u/%u/%u/%u/%u/%u, 478628 %u, 471c70 %u, 4236c4 %u, 423a74 %u, 460870 %u, 426b48 %u, 426b70 %u, 45de30 %u, 4123ac %u, 41aa5c %u, 45fa84 %u, 47749c %u, 455950 %u, 4543b0 %u, 4131ac %u, 423f08 %u, slot +14h %u, +18h %u, +8 %u); EAX/EDX, ordered calls with EAX..EDI and slot counts, arena, globals, FS:0\n",
        function_hook_descriptor(index)->name,cases,lfr_paths[0],lfr_paths[1],lfr_paths[2],lfr_paths[3],lfr_paths[4],lfr_paths[5],lfr_paths[6],
        lfr_paths[7],lfr_paths[8],lfr_paths[9],lfr_paths[10],lfr_paths[11],lfr_paths[12],lfr_paths[13],lfr_paths[14],lfr_paths[15],
        lfr_paths[16],lfr_paths[17],lfr_paths[18],lfr_paths[19],lfr_paths[20],lfr_paths[21],lfr_paths[22],lfr_paths[23],lfr_paths[24],
        lfr_paths[25],lfr_paths[26]);
    return status;
}
