/* mission_root group: the mission-load root 461608 (mission_root)
 * (docs/reverse-engineering/mission_load.md, "The mission-load root").
 *
 * A 28 KiB arena holds six pool headers (explosions, debris, MECH,
 * buildings and two spares) with linked objects (8-byte link header; per
 * object the position +26h, the group pointer +45h, the player byte +0A3h,
 * the type pointer +1F2h and the display slot +1FEh), four removal lists
 * with arrays, four groups (+10h member count, +12h category), four type
 * records (+16h height offset, +50h the 5000 flag), the terrain object
 * table (ten entries, the last null), two terrain objects, two counters,
 * two key devices, a spare camera and the blocks that new returns; the
 * image globals the root reads and writes live in the mapped image's DATA
 * (saved, compared and restored around every case).
 *
 * Every direct callee (49630c, 479cfc, 45a5dc, 4955c4, the two stdcall
 * thunks 49694c/496970, 461294, 464c78, 46548c, 4614c0, 4967ea, 492bec,
 * 465f7c, 481fe0, 461450, 47749c, 480af0, 462410, 48dd08, 4913dc, 401d94,
 * 46134c, 467840, 424308, 4282d8, 452ae4, 4253d8, 44f6f4, 471c70, 478628,
 * the four flushes, 46e180, 48bd84, 45cdf4, 465618, 4544c0, 48bda4,
 * 45edb0, 429660, 431530, 437c8c, 41b130, 4011a0, 42a1f0, 49288c, 46313c,
 * 42eb40, 43048c, 45fca8, 45d548, 464a14, 43122c, 4328cc, 433f7c, 45d490,
 * 45d50c, 45d85c, 45b758, 477abc) is a logging stub patched into the
 * private image, the graphics slot [4A5824h] points at a slot stub, and the
 * bare RET 463370 stays original. A stub logs its arguments, the entry
 * EAX/EDX/ECX and EBX/ESI/EDI, the slots the bridge pushed and a hash of the
 * frame buffers some callees receive (the loading-screen name, the
 * position, the camera vectors, the text rectangle); addresses inside the
 * root's frame are logged relative to its EBP. Results come from a
 * per-case stream: new hands out arena blocks (null one time in eight,
 * except for the main camera, whose null is only allowed when 45edb0 then
 * stores a camera: the original reads it at 461c6e), 471c70 walks the
 * arena's links, the first flush of each drain unlinks the pool's tail (as
 * the removal does), 49288c draws distances around 40000, 4614c0 writes the
 * world and flag words, the thunks write the rectangles; one call of four
 * applies a side effect the root reads again (player bytes, the player,
 * group counts and categories, pool and list globals, an inserted node,
 * the view-block words, the size and sound words, the main camera's +4Ah,
 * terrain-table entries, the terrain object, counter and device pointers,
 * the prefs bytes).
 *
 * Direct cases run the original body (EBX=ESI=EDI=0) and (a) the core with
 * its frame at the original's EBP (its own code on a static stack, the
 * frame refilled with the case's stack pattern) and (b) one case in four
 * (zero pattern) the adapter's plain path, over the same stubs; they
 * compare EDX:EAX (and ECX for the core), the ordered calls with all six
 * registers, slot counts and buffer hashes, the arena, the globals, FS:0,
 * the core's frame bytes and (first 4,096 cases) the callee-counter
 * deltas. The frame probe runs the original and the hook installed through
 * its entry shim (fast and preserving dispatch, 31/a7 stack, EAX..EDI
 * distinct patterns, the real 49630c) and compares EAX, EDX, ECX, every
 * stub's EBX/ESI/EDI, EBP and ESP relative to the caller's ESP, a hash of
 * the 256 bytes below the stub's ESP (residue), the FS:0 head and its
 * record, the arena, the globals, FS:0 after return, the x87 environment
 * and the 1,024 bytes below the caller's ESP; one-slot stubs change their
 * argument slot in one call of four. The depth probe measures the shimmed
 * root's deepest stack write. The real-callee pass runs the root over the
 * real 49630c, 471c70, 471c18, 478628 and 49288c with mission_root,
 * pool_prev, pool_next, removal_append and dist_gate original/replaced in
 * all 32 combinations against the all-original run. Identifiers:
 * mrt_/MRT_. */
enum { MRT_SIZE=0x7000, MRT_POOLS=6, MRT_SLOTS=5, MRT_LISTS=4, MRT_GROUPS=4, MRT_TYPES=4, MRT_ENTRIES=10,
    MRT_POOL0=0x40, MRT_LIST0=0x200, MRT_ARRAY0=0x300, MRT_GROUP0=0x600, MRT_TYPE0=0x700, MRT_TABLE=0x900,
    MRT_TERRAIN0=0xa00, MRT_TERRAIN1=0xb40, MRT_C1=0xc80, MRT_C2=0xc84, MRT_DEV0=0xc90, MRT_DEV1=0xca0,
    MRT_SPARECAM=0xd00, MRT_BUMP=0x1000, MRT_BUMP_END=0x2000, MRT_BLOCK=0x60, MRT_OBJ0=0x2000, MRT_OBJ_SIZE=0x210,
    MRT_IDS=63, MRT_SLOT_ID=57, MRT_MAX=512, MRT_INSERTS=3 };
#define MRT_BASE ((uint32_t)(uintptr_t)mrt_arena)
#define MRT_ADDR(o) (MRT_BASE+(uint32_t)(o))
#define MRT_POOL(k) (MRT_POOL0+0x20u*(uint32_t)(k))
#define MRT_LIST(k) (MRT_LIST0+0x10u*(uint32_t)(k))
#define MRT_ARRAY(k) (MRT_ARRAY0+0x80u*(uint32_t)(k))
#define MRT_GROUP(k) (MRT_GROUP0+0x20u*(uint32_t)(k))
#define MRT_TYPE(k) (MRT_TYPE0+0x60u*(uint32_t)(k))
/* Object i of pool k; its link node is 8 bytes below. */
#define MRT_OBJ(k,i) (MRT_OBJ0+MRT_OBJ_SIZE*(MRT_SLOTS*(uint32_t)(k)+(uint32_t)(i))+8u)
#define MRT_VIEW 0xd24bcu
static BYTE mrt_arena[MRT_SIZE],mrt_before[MRT_SIZE],mrt_after[MRT_SIZE];
typedef struct mrt_record { uint32_t id,a[6],r[3],cs[3],slots,frame[2],hash,seh[2],extra; } mrt_record;
static mrt_record mrt_log[MRT_MAX];
static unsigned mrt_calls,mrt_track,mrt_hash_on,mrt_clobbers,mrt_paths[MRT_IDS],mrt_inserts;
static unsigned mrt_used[MRT_POOLS],mrt_used0[MRT_POOLS];
static uint32_t mrt_seed,mrt_stream,mrt_bump,mrt_f,mrt_main_next,mrt_fix_camera;
/* Stub entry: EBP, ESP, the saved ESI/EDI/ECX while the window is copied,
 * the ECX a stub returns, the caller's ESP before the CALL of the tracked
 * run, the 256 bytes below the stub's ESP, the 1,024 bytes below the
 * caller's ESP after the tracked run, the deepest write of the depth probe. */
__attribute__((used)) static uint32_t mrt_entry_ebp,mrt_entry_esp,mrt_tmp[3],mrt_ecx,mrt_origin,mrt_window_on,mrt_deepest;
__attribute__((used)) static BYTE mrt_window[256],mrt_frame_copy[1024];
static const uint32_t mrt_addr[MRT_IDS]={0x49630c,0x479cfc,0x45a5dc,0x4955c4,0x49694c,0x496970,0x461294,0x464c78,0x46548c,
    0x4614c0,0x4967ea,0x492bec,0x465f7c,0x481fe0,0x461450,0x47749c,0x480af0,0x462410,0x48dd08,0x4913dc,0x401d94,0x46134c,
    0x467840,0x424308,0x4282d8,0x452ae4,0x4253d8,0x44f6f4,0x471c70,0x478628,0x407b3c,0x4077e8,0x409904,0x4083e0,0x46e180,
    0x48bd84,0x45cdf4,0x465618,0x4544c0,0x48bda4,0x45edb0,0x429660,0x431530,0x437c8c,0x41b130,0x4011a0,0x42a1f0,0x49288c,
    0x46313c,0x42eb40,0x43048c,0x45fca8,0x45d548,0x464a14,0x43122c,0x4328cc,0x433f7c,0,0x45d490,0x45d50c,0x45d85c,0x45b758,
    0x477abc};
static const unsigned mrt_arity[MRT_IDS]={0,1,0,0,2,2,0,0,0,2,2,2,1,1,1,1,2,6,0,0,1,0,0,0,1,0,0,0,2,2,0,0,0,0,2,
    1,1,1,0,6,1,2,1,1,1,1,2,2,0,1,0,0,0,0,3,0,0,0,1,1,0,0,1};
/* The image ranges the root reads or writes (rva, size). */
static const struct { uint32_t rva,size; } mrt_global_list[]={
    {0xd24bc,0xc0},{0xd267d,1},{0xd2684,0x24},{0xd3610,4},{0xa09fc,4},{0x2cbee4,4},{0xaaa78,6},{0xd1dd6,1},
    {0xa961e,4},{0xa9622,4},{0xa9642,4},{0xa9646,4},{0x9abba,2},{0xcf880,2},{0xa9b7a,4},{0xa0be0,4},{0xa0a10,4},
    {0x9dd94,8},{0xd3040,4},{0x9b088,4},{0xa95a0,4},{0xa9e54,2},{0x9f8ec,4},{0xaa728,2},{0x9f8fc,4},{0x9f900,1},
    {0x9aef6,2},{0x9b105,1},{0xa5824,4},{0xd1f40,3},{0x2c5eb8,4}};
#define MRT_GLOBALS (0xc0+1+0x24+4+4+4+6+1+4+4+4+4+2+2+4+4+4+8+4+4+4+2+4+2+4+1+2+1+4+3+4)
static void mrt_globals(BYTE *out,int restore){
    BYTE *p=out;
    for(unsigned i=0;i<sizeof(mrt_global_list)/sizeof(mrt_global_list[0]);++i){
        if(restore)memcpy(mapped+mrt_global_list[i].rva,p,mrt_global_list[i].size);
        else memcpy(p,mapped+mrt_global_list[i].rva,mrt_global_list[i].size);
        p+=mrt_global_list[i].size;
    }
}
static uint32_t mrt_next(void){
    uint32_t z=mrt_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static uint32_t mrt_pick(uint32_t bound){return mrt_next()%bound;}
/* A random register or data value with bit 31 set: never an address of
 * the arena, the image or a stack. */
static uint32_t mrt_value(void){return mrt_next()|0x80000000u;}
static uint32_t mrt_fs0(void){uint32_t v;__asm__ volatile("movl %%fs:0,%0":"=r"(v));return v;}
static uint32_t mrt_hash(const BYTE *p,unsigned n){uint32_t h=2166136261u;for(unsigned i=0;i<n;++i)h=(h^p[i])*16777619u;return h;}
static BYTE *mrt_host(uint32_t address,uint32_t n){return address-MRT_BASE<=MRT_SIZE-n?mrt_arena+(address-MRT_BASE):NULL;}
/* A value inside the root's frame (EBP-200h .. EBP+0Fh) as its offset. */
static uint32_t mrt_norm(uint32_t v){return mrt_f&&v-(mrt_f-0x200u)<0x210u?0xf0000000u|((v-mrt_f)&0xffffu):v;}
/* mrt_norm is exact only if no data word the root forwards lies near a
 * frame: the original's frame and the adapter's buffer frame have
 * different EBPs (4 bytes apart), and the worker stack moves with ASLR, so
 * a data word inside both windows normalises to two offsets and one inside
 * one window only is normalised on one side. The generator keeps every
 * forwarded 32-bit data word out of the worker stack's reservation (bit 31
 * set, a signed 16-bit value, below 1 MiB, or an arena or image address);
 * in the original's
 * direct run every logged argument and register is checked against the
 * reservation (the frame window and the FS:0 head that 49630c returns
 * excepted). */
static uint32_t mrt_stack_lo,mrt_stack_hi,mrt_head,mrt_alias_value;
static unsigned mrt_alias_on,mrt_alias_hit,mrt_alias_call,mrt_alias_word;
static void mrt_alias(unsigned word,uint32_t v){
    if(mrt_alias_hit||v-mrt_stack_lo>=mrt_stack_hi-mrt_stack_lo||v==mrt_head||(mrt_f&&v-(mrt_f-0x200u)<0x210u))return;
    mrt_alias_hit=1;mrt_alias_call=mrt_calls-1u;mrt_alias_word=word;mrt_alias_value=v;
}
static void mrt_reset(void){
    mrt_calls=0;mrt_stream=mrt_seed;mrt_bump=MRT_ADDR(MRT_BUMP);mrt_f=0;mrt_main_next=mrt_fix_camera=0;mrt_inserts=0;
    memcpy(mrt_used,mrt_used0,sizeof(mrt_used));
}
/* 471c70's walk over the arena's links: the previous object of O, or (O =
 * 0) the tail of P's used list. */
static uint32_t mrt_walk(uint32_t pool,uint32_t object){
    if(object){const BYTE *n=mrt_host(object-8u,8);if(!n)return 0;const uint32_t prev=get32(n+4);return prev?prev+8u:0;}
    const BYTE *h=mrt_host(pool,0x12);if(!h)return 0;
    uint32_t node=get32(h+0xe),last=0;
    for(unsigned guard=0;node&&guard<64;++guard){const BYTE *n=mrt_host(node,8);if(!n)break;last=node;node=get32(n);}
    return last?last+8u:0;
}
/* The removal of a drained object: the tail node of the pool at the global
 * `rva` is unlinked. */
static void mrt_unlink(uint32_t rva){
    BYTE *h=mrt_host(get32(mapped+rva),0x12);if(!h)return;
    uint32_t node=get32(h+0xe),prev=0;
    for(unsigned guard=0;node&&guard<64;++guard){const BYTE *n=mrt_host(node,8);if(!n)return;if(!get32(n))break;prev=node;node=get32(n);}
    if(!node)return;
    if(prev){BYTE *p=mrt_host(prev,8);put32(p,0);}else put32(h+0xe,0);
}
/* A new object at the head of pool k. */
static void mrt_insert(unsigned k){
    if(mrt_used[k]>=MRT_SLOTS)return;
    BYTE *h=mrt_arena+MRT_POOL(k);
    const uint32_t node=MRT_ADDR(MRT_OBJ(k,mrt_used[k]))-8u,head=get32(h+0xe);
    BYTE *n=mrt_arena+(node-MRT_BASE);put32(n,head);put32(n+4,0);
    if(head){BYTE *o=mrt_host(head,8);if(o)put32(o+4,node);}
    put32(h+0xe,node);++mrt_used[k];
}
/* A block from the bump area (MRT_BLOCK bytes, at least 4Eh). */
static uint32_t mrt_alloc(void){
    if(mrt_bump+MRT_BLOCK>MRT_ADDR(MRT_BUMP_END)){fail("mission_root bump area exhausted");ExitProcess(1);}
    const uint32_t p=mrt_bump;mrt_bump+=MRT_BLOCK;return p;
}
/* One callee side effect (about one call in four), drawn from the stream. */
static void mrt_effect(void){
    const uint32_t pick=mrt_next();if(pick%4)return;
    BYTE *view=mapped+MRT_VIEW;
    switch(pick/4%14){
    case 0:{const unsigned k=mrt_pick(2)?2u:3u,i=1u+mrt_pick(MRT_SLOTS-1);mrt_arena[MRT_OBJ(k,i)+0xa3]^=1;break;}   /* not the MECH tail */
    case 1:put32(view+0x2a,MRT_ADDR(MRT_OBJ(2,mrt_pick(MRT_SLOTS))));break;
    case 2:put16(mrt_arena+MRT_GROUP(mrt_pick(MRT_GROUPS))+0x10,(uint16_t)(mrt_pick(11)-2));break;
    case 3:mrt_arena[MRT_GROUP(mrt_pick(MRT_GROUPS))+0x12]^=1;break;
    case 4:{static const uint32_t g[3]={0xa961e,0xa9642,0xa95a0};const unsigned j=mrt_pick(3);
        put32(mapped+g[j],MRT_ADDR(MRT_POOL(mrt_pick(2)?4u+mrt_pick(2):(j==2?3u:j))));break;}
    case 5:if(mrt_inserts<MRT_INSERTS){++mrt_inserts;mrt_insert(mrt_pick(MRT_POOLS));}break;
    case 6:switch(mrt_pick(8)){
        case 0:put32(view+0x82,mrt_pick(2)?0x27fu+mrt_pick(3):mrt_value());break;
        case 1:view[0x7b]=(BYTE)(mrt_pick(4)?mrt_pick(3):mrt_next());break;
        case 2:view[0xb]=(BYTE)(mrt_pick(2)?0:mrt_next());break;
        case 3:put16(view+0xaa,(uint16_t)(mrt_pick(2)?0:mrt_next()));break;
        case 4:put16(view+0x54,(uint16_t)(mrt_pick(2)?0:mrt_next()));break;
        case 5:put16(view+0x1a,(uint16_t)(mrt_pick(2)?0:mrt_next()));break;
        case 6:view[0x7c]=(BYTE)(mrt_pick(2)?2:mrt_next());break;
        default:view[0xb5]=(BYTE)(mrt_pick(2)?0:mrt_next());break;
        }break;
    case 7:switch(mrt_pick(4)){
        case 0:put32(mapped+0xd3040,mrt_pick(2)?0x4afu+mrt_pick(3):mrt_value());break;
        case 1:put32(mapped+0x9f8ec,mrt_pick(2)?0:mrt_value());break;
        case 2:put16(mapped+0xaa728,(uint16_t)(mrt_pick(2)?0:mrt_next()));break;
        default:mapped[0xd267d]=(BYTE)(mrt_pick(2)?0:mrt_next());break;
        }break;
    case 8:{BYTE *c=mrt_host(get32(view+0x2e),0x4e);if(c)put32(c+0x4a,mrt_pick(2)?0:mrt_value());break;}
    case 9:put32(mrt_arena+MRT_TABLE+4u*mrt_pick(MRT_ENTRIES-1),mrt_pick(4)?MRT_ADDR(MRT_OBJ(mrt_pick(MRT_POOLS),mrt_pick(MRT_SLOTS))):0);break;
    case 10:put32(mapped+0xa0be0,MRT_ADDR(mrt_pick(2)?MRT_TERRAIN1:MRT_TERRAIN0));break;
    case 11:if(mrt_pick(2))put32(mapped+0x2c5eb8,MRT_ADDR(mrt_pick(2)?MRT_DEV1:MRT_DEV0));
        else put32(mapped+0x2cbee4,MRT_ADDR(mrt_pick(2)?MRT_C1:MRT_C2));break;
    case 12:if(mrt_pick(2))mapped[0xd1f40+mrt_pick(3)]=(BYTE)mrt_next();else put16(mapped+0x9abba,(uint16_t)mrt_next());break;
    default:{static const uint32_t l[2]={0xa9622,0xa9646};put32(mapped+l[mrt_pick(2)],MRT_ADDR(MRT_LIST(mrt_pick(MRT_LISTS))));break;}
    }
}
/* The frame buffers some callees receive, hashed at the call. */
static uint32_t mrt_extra(unsigned id,const uint32_t *a){
    switch(id){
    case 10:case 11:case 14:return mrt_hash((const BYTE *)(uintptr_t)a[0],8);
    case 34:return mrt_hash((const BYTE *)(uintptr_t)a[1],12);
    case 39:return mrt_hash((const BYTE *)(uintptr_t)a[4],12)^mrt_hash((const BYTE *)(uintptr_t)a[5],6)*3u;
    case 54:return mrt_hash((const BYTE *)(uintptr_t)a[1],16);
    default:return 0;
    }
}
__attribute__((used,noinline)) static uint64_t __cdecl mrt_log_impl(unsigned id,const uint32_t *regs,uint32_t *a){
    if(mrt_calls==MRT_MAX){fail("mission_root synthetic call overflow");ExitProcess(1);}
    if(!mrt_f)mrt_f=mrt_entry_ebp;                          /* the first call (49630c, or 479cfc with the real one) */
    mrt_record *v=mrt_log+mrt_calls++;memset(v,0,sizeof(*v));v->id=id;
    if(mrt_alias_on){for(unsigned j=0;j<mrt_arity[id];++j)mrt_alias(1u+j,a[j]);for(unsigned j=0;j<6;++j)mrt_alias(7u+j,regs[j]);}
    for(unsigned j=0;j<mrt_arity[id];++j)v->a[j]=mrt_norm(a[j]);
    for(unsigned j=0;j<3;++j){v->r[j]=mrt_norm(regs[j]);v->cs[j]=mrt_norm(regs[3+j]);}
    v->slots=weapons_bridge_slots(a,mrt_arity[id]);
    v->extra=mrt_extra(id,a);
    if(mrt_track){
        v->frame[0]=mrt_entry_ebp-mrt_origin;v->frame[1]=mrt_entry_esp-mrt_origin;
        if(mrt_hash_on)v->hash=mrt_hash(mrt_window,256);
        const uint32_t head=mrt_fs0();v->seh[0]=head-mrt_origin;v->seh[1]=mrt_hash((const BYTE *)(uintptr_t)head,0x20);
    }
    uint32_t eax=mrt_value(),edx=mrt_value();const uint32_t x=mrt_next();mrt_ecx=mrt_value();
    BYTE *view=mapped+MRT_VIEW;
    switch(id){
    case 4:case 5:   /* the window and client rectangles (signed 16-bit: their differences are forwarded) */
        for(unsigned k=0;k<4;++k)put32((BYTE *)(uintptr_t)a[1]+4u*k,x%2?mrt_pick(1024):(uint32_t)(int32_t)(int16_t)mrt_next());
        eax=1;break;
    case 9:          /* the launch peek: the world and the flag word */
        put16((BYTE *)(uintptr_t)a[0],(uint16_t)(x%4?mrt_pick(6):mrt_next()));
        put16((BYTE *)(uintptr_t)a[1],(uint16_t)(x%3==0?0:x%3==1?1:mrt_next()));break;
    case 15:{        /* new: an arena block, null one time in eight */
        const int main_camera=mrt_main_next&&a[0]==0x4eu;
        if(main_camera)mrt_main_next=0;
        if(x%8==0){eax=0;if(main_camera)mrt_fix_camera=1;}
        else eax=mrt_alloc();
        break;}
    case 28:eax=mrt_walk(a[0],a[1]);break;                                  /* 471c70 */
    case 30:mrt_unlink(0xa961e);break;                                      /* the first flush of drain 1 */
    case 33:mrt_unlink(0xa9642);break;                                      /* the flush of drain 2 */
    case 38:mrt_main_next=1;break;                                          /* 4544c0: the main camera is next */
    case 40:if(mrt_fix_camera){mrt_fix_camera=0;put32(view+0x2e,MRT_ADDR(MRT_SPARECAM));}break;   /* 45edb0 */
    case 47:eax=x%3==0?mrt_pick(0x9c40):0x9c40u+mrt_pick(0x10000);break;    /* 49288c */
    default:break;
    }
    if(mrt_track&&mrt_arity[id]==1&&mrt_pick(4)==0){a[0]=mrt_next();++mrt_clobbers;}   /* the callee changes its slot */
    mrt_effect();
    return (uint64_t)edx<<32|eax;
}
/* EBP and ESP at entry; with mrt_window_on the 256 bytes below ESP; then
 * EDI, ESI, EBX, ECX, EDX, EAX (the register block) before the argument
 * pointer, the block pointer and the id. */
#define MRT_STUB_HEAD \
    "movl %ebp,_mrt_entry_ebp\n\tmovl %esp,_mrt_entry_esp\n\tcmpl $0,_mrt_window_on\n\tje 1f\n\t" \
    "movl %esi,_mrt_tmp\n\tmovl %edi,_mrt_tmp+4\n\tmovl %ecx,_mrt_tmp+8\n\t" \
    "leal -256(%esp),%esi\n\tmovl $_mrt_window,%edi\n\tmovl $64,%ecx\n\tcld\n\trep movsl\n\t" \
    "movl _mrt_tmp,%esi\n\tmovl _mrt_tmp+4,%edi\n\tmovl _mrt_tmp+8,%ecx\n\t" \
    "1: pushl %edi\n\tpushl %esi\n\tpushl %ebx\n\tpushl %ecx\n\tpushl %edx\n\tpushl %eax\n\t" \
    "leal 28(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
#define MRT_STUB(n) __attribute__((naked)) static void mrt_stub_##n(void){__asm__ volatile( \
    MRT_STUB_HEAD "pushl $" #n "\n\tcalll _mrt_log_impl\n\taddl $36,%esp\n\tmovl _mrt_ecx,%ecx\n\tret\n\t");}
/* The two stdcall thunks pop their two slots. */
#define MRT_STUB_STD(n) __attribute__((naked)) static void mrt_stub_##n(void){__asm__ volatile( \
    MRT_STUB_HEAD "pushl $" #n "\n\tcalll _mrt_log_impl\n\taddl $36,%esp\n\tmovl _mrt_ecx,%ecx\n\tret $8\n\t");}
MRT_STUB(1) MRT_STUB(2) MRT_STUB(3) MRT_STUB_STD(4) MRT_STUB_STD(5) MRT_STUB(6) MRT_STUB(7) MRT_STUB(8) MRT_STUB(9)
MRT_STUB(10) MRT_STUB(11) MRT_STUB(12) MRT_STUB(13) MRT_STUB(14) MRT_STUB(15) MRT_STUB(16) MRT_STUB(17) MRT_STUB(18)
MRT_STUB(19) MRT_STUB(20) MRT_STUB(21) MRT_STUB(22) MRT_STUB(23) MRT_STUB(24) MRT_STUB(25) MRT_STUB(26) MRT_STUB(27)
MRT_STUB(28) MRT_STUB(29) MRT_STUB(30) MRT_STUB(31) MRT_STUB(32) MRT_STUB(33) MRT_STUB(34) MRT_STUB(35) MRT_STUB(36)
MRT_STUB(37) MRT_STUB(38) MRT_STUB(39) MRT_STUB(40) MRT_STUB(41) MRT_STUB(42) MRT_STUB(43) MRT_STUB(44) MRT_STUB(45)
MRT_STUB(46) MRT_STUB(47) MRT_STUB(48) MRT_STUB(49) MRT_STUB(50) MRT_STUB(51) MRT_STUB(52) MRT_STUB(53) MRT_STUB(54)
MRT_STUB(55) MRT_STUB(56) MRT_STUB(57) MRT_STUB(58) MRT_STUB(59) MRT_STUB(60) MRT_STUB(61) MRT_STUB(62)
#undef MRT_STUB
#undef MRT_STUB_STD
/* 49630c: log, then link the record at EBP+[EAX+4] into FS:0 (saved head,
 * context word 0) as the runtime does; EAX = the old head, EDX/ECX kept. */
__attribute__((naked)) static void mrt_stub_0(void){__asm__ volatile(
    MRT_STUB_HEAD "pushl $0\n\tcalll _mrt_log_impl\n\taddl $12,%esp\n\t"
    "movl (%esp),%ecx\n\tmovl 4(%ecx),%ecx\n\taddl %ebp,%ecx\n\t"
    "movl %fs:0,%eax\n\tmovl %eax,(%ecx)\n\tmovw $0,16(%ecx)\n\tmovl %ecx,%fs:0\n\t"
    "movl 4(%esp),%edx\n\tmovl 8(%esp),%ecx\n\taddl $24,%esp\n\tret\n\t");}
#undef MRT_STUB_HEAD
static void *mrt_stubs[MRT_IDS]={mrt_stub_0,mrt_stub_1,mrt_stub_2,mrt_stub_3,mrt_stub_4,mrt_stub_5,mrt_stub_6,mrt_stub_7,
    mrt_stub_8,mrt_stub_9,mrt_stub_10,mrt_stub_11,mrt_stub_12,mrt_stub_13,mrt_stub_14,mrt_stub_15,mrt_stub_16,mrt_stub_17,
    mrt_stub_18,mrt_stub_19,mrt_stub_20,mrt_stub_21,mrt_stub_22,mrt_stub_23,mrt_stub_24,mrt_stub_25,mrt_stub_26,mrt_stub_27,
    mrt_stub_28,mrt_stub_29,mrt_stub_30,mrt_stub_31,mrt_stub_32,mrt_stub_33,mrt_stub_34,mrt_stub_35,mrt_stub_36,mrt_stub_37,
    mrt_stub_38,mrt_stub_39,mrt_stub_40,mrt_stub_41,mrt_stub_42,mrt_stub_43,mrt_stub_44,mrt_stub_45,mrt_stub_46,mrt_stub_47,
    mrt_stub_48,mrt_stub_49,mrt_stub_50,mrt_stub_51,mrt_stub_52,mrt_stub_53,mrt_stub_54,mrt_stub_55,mrt_stub_56,mrt_stub_57,
    mrt_stub_58,mrt_stub_59,mrt_stub_60,mrt_stub_61,mrt_stub_62};
/* Patches the callees of `which` (bit per id) with their stubs (on) or
 * restores them. */
#define MRT_BIT(i) ((uint64_t)1<<(i))
#define MRT_ALL ((MRT_BIT(MRT_IDS)-1u)&~MRT_BIT(MRT_SLOT_ID))
#define MRT_PROBE (MRT_ALL&~MRT_BIT(0))                                          /* the real 49630c */
#define MRT_REAL (MRT_ALL&~(MRT_BIT(0)|MRT_BIT(28)|MRT_BIT(29)|MRT_BIT(47)))     /* real 49630c, 471c70, 478628, 49288c */
static int mrt_patch(BYTE saved[MRT_IDS][5],int active[MRT_IDS],uint64_t which,int on){
    for(unsigned i=0;i<MRT_IDS;++i){
        if(!mrt_addr[i]){active[i]=0;continue;}
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(mrt_addr[i]-0x400000u,mrt_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(mrt_addr[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
/* A complete world: the arena filled from the case stream, then every
 * structure the root and the real callees read; regs[0..2] the entry
 * EAX/EDX/ECX (EBX/ESI/EDI are the caller's, set by the runner). */
static void mrt_build(unsigned n,uint32_t regs[6]){
    mrt_seed=random32();mrt_stream=mrt_seed^n;
    for(unsigned i=0;i<MRT_SIZE;i+=4)put32(mrt_arena+i,mrt_value());
    for(unsigned i=0;i<6;++i)regs[i]=mrt_value();
    /* groups, types, objects, pools (links: head = newest, iteration from the tail) */
    for(unsigned k=0;k<MRT_GROUPS;++k){put16(mrt_arena+MRT_GROUP(k)+0x10,(uint16_t)(mrt_pick(11)-2));mrt_arena[MRT_GROUP(k)+0x12]=(BYTE)(mrt_pick(3)==0);}
    for(unsigned k=0;k<MRT_TYPES;++k){put16(mrt_arena+MRT_TYPE(k)+0x16,(uint16_t)mrt_next());put16(mrt_arena+MRT_TYPE(k)+0x50,(uint16_t)(mrt_pick(2)?0:mrt_next()));}
    for(unsigned k=0;k<MRT_POOLS;++k){
        for(unsigned i=0;i<MRT_SLOTS;++i){
            BYTE *o=mrt_arena+MRT_OBJ(k,i);
            put32(o+0x26,mrt_next()%0x100000u);put32(o+0x2a,mrt_next()%0x100000u);put32(o+0x2e,mrt_value());
            put32(o+0x45,MRT_ADDR(MRT_GROUP(mrt_pick(MRT_GROUPS))));put32(o+0x1f2,MRT_ADDR(MRT_TYPE(mrt_pick(MRT_TYPES))));
            o[0xa3]=(BYTE)(k==2&&i==0?1:mrt_pick(3)==0);
        }
        const unsigned count=k==2?1+mrt_pick(4):k>=4?mrt_pick(3):mrt_pick(4)==0?0:1+mrt_pick(3);
        put32(mrt_arena+MRT_POOL(k)+0xe,0);mrt_used0[k]=mrt_used[k]=0;
        for(unsigned i=0;i<count;++i)mrt_insert(k);
        mrt_used0[k]=mrt_used[k];
    }
    put32(mapped+0xa961e,MRT_ADDR(MRT_POOL(0)));put32(mapped+0xa9642,MRT_ADDR(MRT_POOL(1)));
    put32(mapped+0xa9b7a,MRT_ADDR(MRT_POOL(2)));put32(mapped+0xa95a0,MRT_ADDR(MRT_POOL(3)));
    for(unsigned k=0;k<MRT_LISTS;++k){put32(mrt_arena+MRT_LIST(k),MRT_ADDR(MRT_ARRAY(k)));put16(mrt_arena+MRT_LIST(k)+4,(uint16_t)mrt_pick(4));}
    put32(mapped+0xa9622,MRT_ADDR(MRT_LIST(0)));put32(mapped+0xa9646,MRT_ADDR(MRT_LIST(1)));
    /* the terrain object table: up to nine entries (null in one of five), the tenth null */
    for(unsigned i=0;i<MRT_ENTRIES;++i)put32(mrt_arena+MRT_TABLE+4u*i,i+1<MRT_ENTRIES&&mrt_pick(5)?MRT_ADDR(MRT_OBJ(mrt_pick(MRT_POOLS),mrt_pick(MRT_SLOTS))):0);
    put32(mapped+0xaaa78,MRT_ADDR(MRT_TABLE));
    put16(mapped+0xaaa7c,(uint16_t)(mrt_pick(8)?mrt_pick(9):0x10000u-1u-mrt_pick(3)));
    put32(mapped+0xa0be0,MRT_ADDR(mrt_pick(2)?MRT_TERRAIN1:MRT_TERRAIN0));
    put32(mapped+0x2cbee4,MRT_ADDR(MRT_C1));put32(mapped+0x2c5eb8,MRT_ADDR(mrt_pick(2)?MRT_DEV1:MRT_DEV0));
    put32(mapped+0xa5824,(uint32_t)(uintptr_t)mrt_stubs[MRT_SLOT_ID]);
    /* the view block: random, then the words and bytes the root tests */
    BYTE *view=mapped+MRT_VIEW;
    for(unsigned i=0;i<0xc0;i+=4)put32(view+i,mrt_next());
    put32(view+0x82,mrt_pick(2)?0x27fu+mrt_pick(3):mrt_next());
    view[0x7b]=(BYTE)(mrt_pick(4)?mrt_pick(3):mrt_next());view[0xb]=(BYTE)(mrt_pick(2)?0:mrt_next());
    put16(view+0xaa,(uint16_t)(mrt_pick(2)?0:mrt_next()));put16(view+0x54,(uint16_t)(mrt_pick(2)?0:mrt_next()));
    put16(view+0x1a,(uint16_t)(mrt_pick(2)?0:mrt_next()));view[0x7c]=(BYTE)(mrt_pick(2)?2:mrt_next());
    view[0xb5]=(BYTE)(mrt_pick(2)?0:mrt_next());
    /* the forwarded words: the context +12h (48bda4's second slot and ECX),
     * the run counter +0B7h (its quotient by 5 is 46313c's EAX) */
    put32(view+0x12,get32(view+0x12)|0x80000000u);
    {const uint32_t v=get32(view+0xb7);put32(view+0xb7,v&0x10000u?v|0x80000000u:(uint32_t)(int32_t)(int16_t)v);}
    /* the other globals */
    mapped[0xd267d]=(BYTE)(mrt_pick(2)?0:mrt_next());
    put32(mapped+0xd3040,mrt_pick(2)?0x4afu+mrt_pick(3):mrt_next());
    put32(mapped+0x9f8ec,mrt_pick(2)?0:mrt_value());put16(mapped+0xaa728,(uint16_t)(mrt_pick(2)?0:mrt_next()));
    for(unsigned i=0;i<3;++i)mapped[0xd1f40+i]=(BYTE)mrt_next();
    put16(mapped+0x9abba,(uint16_t)mrt_next());put16(mapped+0xa9e54,(uint16_t)mrt_next());
    put32(mapped+0xd3610,mrt_value());put32(mapped+0xa09fc,mrt_value());put32(mapped+0xa0a10,mrt_value());put32(mapped+0x9b088,mrt_value());
    mrt_stream=mrt_seed^~n;                                 /* the stubs' stream restarts here in every run */
    mrt_seed=mrt_stream;
}
static unsigned mrt_hits(const callee_counter *c){
    if(c->event==ES2_COLLIDE_CALL&&c->key==0x463370u)return 1;   /* the bare RET (not stubbed): once per run */
    unsigned h=0;
    for(unsigned j=0;j<mrt_calls;++j){const unsigned id=mrt_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?(id==MRT_SLOT_ID&&c->key==0x4a5824u):(id!=MRT_SLOT_ID&&mrt_addr[id]==c->key))++h;}
    return h;
}
static int mrt_report(const char *what,unsigned n,const uint32_t want[3],const uint32_t got[3],unsigned calls,const mrt_record *e,
    const BYTE *globals_want,const BYTE *globals_got){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x calls %u/%u\n",current_function,n,what,
        want[0],got[0],want[1],got[1],want[2],got[2],calls,mrt_calls);
    for(unsigned j=0;j<calls&&j<mrt_calls&&e;++j)if(memcmp(e+j,mrt_log+j,sizeof(*e))){
        const uint32_t *x=(const uint32_t*)(e+j),*y=(const uint32_t*)(mrt_log+j);
        for(unsigned k=0;k<sizeof(*e)/4;++k)if(x[k]!=y[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,e[j].id,k,x[k],y[k]);break;}
    for(unsigned j=0;j<MRT_SIZE;++j)if(mrt_after[j]!=mrt_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,mrt_after[j],mrt_arena[j]);break;}
    for(unsigned j=0;globals_want&&j<MRT_GLOBALS;++j)if(globals_want[j]!=globals_got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,globals_want[j],globals_got[j]);break;}
    return 1;
}
/* Calls target (no argument slot) with EAX..EDI = regs[0..5] over 4 KiB of
 * stack filled with `fill`; EAX, EDX, ECX into out[0..2]. */
__attribute__((naked,noinline)) static void mrt_run(void *target,const uint32_t *regs,uint32_t out[3],uint32_t fill){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 20(%ebp),%eax\n\tmovl $1024,%ecx\n\tcld\n\trep stosl\n\taddl $4096,%esp\n\t"
        "movl 12(%ebp),%eax\n\tmovl 12(%eax),%ebx\n\tmovl 16(%eax),%esi\n\tmovl 20(%eax),%edi\n\t"
        "movl 4(%eax),%edx\n\tmovl 8(%eax),%ecx\n\tmovl (%eax),%eax\n\tcall *8(%ebp)\n\t"
        "movl 16(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* Calls target (no argument slot) over a 4-KiB fill with EAX..EDI distinct
 * pattern words; mrt_origin = the caller's ESP before the CALL; EAX, EDX,
 * ECX into out; the 1,024 bytes below the origin after return into
 * mrt_frame_copy. */
__attribute__((naked,noinline)) static void mrt_track_run(void *target,uint32_t pattern,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 12(%ebp),%eax\n\tmovl $1024,%ecx\n\tcld\n\trep stosl\n\taddl $4096,%esp\n\t"
        "movl %esp,_mrt_origin\n\tmovl 12(%ebp),%eax\n\t"
        "movl %eax,%edx\n\txorl $0x01010101,%edx\n\tmovl %eax,%ecx\n\txorl $0x02020202,%ecx\n\tmovl %eax,%ebx\n\txorl $0x04040404,%ebx\n\t"
        "movl %eax,%esi\n\txorl $0x08080808,%esi\n\tmovl %eax,%edi\n\txorl $0x10101010,%edi\n\tcall *8(%ebp)\n\t"
        "movl 16(%ebp),%ebx\n\tmovl %eax,(%ebx)\n\tmovl %edx,4(%ebx)\n\tmovl %ecx,8(%ebx)\n\t"
        "leal -1024(%esp),%esi\n\tmovl $_mrt_frame_copy,%edi\n\tmovl $256,%ecx\n\tcld\n\trep movsl\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* The depth probe: fills 640 KiB below the caller's ESP with the pattern
 * (one dword at a time downward, so every guard page is touched in order),
 * calls target (no argument slot) and finds the lowest dword that no longer
 * holds the pattern; mrt_deepest = its distance below the caller's ESP. */
__attribute__((naked,noinline)) static void mrt_depth_run(void *target,uint32_t pattern){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl %esp,_mrt_origin\n\tmovl %esp,%edi\n\tmovl 12(%ebp),%eax\n\tmovl $0x28000,%ecx\n\t"
        "1: subl $4,%edi\n\tmovl %eax,(%edi)\n\tdecl %ecx\n\tjnz 1b\n\t"
        "call *8(%ebp)\n\t"
        "movl _mrt_origin,%edi\n\tsubl $0xa0000,%edi\n\tmovl 12(%ebp),%eax\n\tmovl $0x28000,%ecx\n\tcld\n\trepe scasl\n\t"
        "movl _mrt_origin,%eax\n\tsubl %edi,%eax\n\taddl $4,%eax\n\tmovl %eax,_mrt_deepest\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* Calls fn(arg) with ESP switched to `top` (a static stack) and back. */
__attribute__((naked,noinline)) static void mrt_switch_call(void (*fn)(void *),void *arg,uint32_t top){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tmovl 16(%ebp),%esp\n\tpushl 12(%ebp)\n\tcalll *8(%ebp)\n\t"
        "movl %ebp,%esp\n\tpopl %ebp\n\tret\n\t");
}
typedef struct mrt_core_call { const uint32_t *r; uint32_t f; uint64_t pair; uint32_t ecx; } mrt_core_call;
static void mrt_core_body(void *p){
    mrt_core_call *c=p;
    es2_weapon_graph g=weapons_graph(mapped,NULL,ES2_HOOK_INDEX_mission_root);
    mission_root_guest x={NULL,{{0,0,0,0},0},c->f};
    g.scratch=c->f-ES2_MISSION_ROOT_LOCALS;g.call=mission_root_plain_call;g.context=&x;
    c->ecx=0;
    c->pair=es2_mission_root_registers(&math_native_memory,&g,&x.frame,0,c->r,&c->ecx);
    __asm__ volatile("movl %0,%%fs:0"::"r"(c->ecx):"memory");
}
static BYTE mrt_core_stack[0x10000] __attribute__((aligned(16)));
/* The core with its frame at the original's EBP `f` (below the caller's
 * stack, free after the original returned) and its own code on a static
 * stack (the plain bridge with EBP = f). */
static uint64_t mrt_core(const uint32_t *r,uint32_t f,uint32_t *ecx){
    mrt_core_call c={r,f,0,0};
    mrt_switch_call(mrt_core_body,&c,(uint32_t)(uintptr_t)(mrt_core_stack+sizeof(mrt_core_stack)));
    *ecx=c.ecx;
    return c.pair;
}
/* Direct case: the original body, the core (frame at the original's EBP,
 * refilled with the pattern) and, with a zero pattern, the adapter's plain
 * path over the same stubs. The caller's EBX/ESI/EDI are 0 in all three. */
static int mrt_compare(unsigned index,unsigned n,uint32_t regs[6],uint32_t fill){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[MRT_GLOBALS],after[MRT_GLOBALS],actual[MRT_GLOBALS];static mrt_record expected[MRT_MAX];static BYTE frame_want[ES2_MISSION_ROOT_LOCALS];
    const uint32_t head=mrt_fs0(),dummy[1]={0};uint32_t want[3],got[3]={0,0,0};
    regs[3]=regs[4]=regs[5]=0;
    memcpy(mrt_before,mrt_arena,MRT_SIZE);mrt_globals(before,0);
    mrt_reset();mrt_head=head;mrt_alias_hit=0;mrt_alias_on=1;mrt_run(mapped+d->rva,regs,want,fill);mrt_alias_on=0;
    if(mrt_alias_hit){fprintf(stderr,"FAIL: %s case %u call %u id %u word %u: data word %08x lies in the worker stack %08x..%08x outside the frame\n",
        current_function,n,mrt_alias_call,mrt_log[mrt_alias_call].id,mrt_alias_word,mrt_alias_value,mrt_stack_lo,mrt_stack_hi);return 1;}
    const unsigned calls=mrt_calls;memcpy(expected,mrt_log,calls*sizeof(*expected));memcpy(mrt_after,mrt_arena,MRT_SIZE);mrt_globals(after,0);
    if(mrt_fs0()!=head)return fail("mission_root original left FS:0 changed");
    const uint32_t f=mrt_f;if(!f)return fail("mission_root original made no call");
    memcpy(frame_want,(const BYTE *)(uintptr_t)(f-ES2_MISSION_ROOT_LOCALS),ES2_MISSION_ROOT_LOCALS);
    for(unsigned j=0;j<calls;++j)mrt_paths[expected[j].id]++;
    /* (a) the core */
    memcpy(mrt_arena,mrt_before,MRT_SIZE);mrt_globals(before,1);mrt_reset();
    for(uint32_t a=f-ES2_MISSION_ROOT_OUTGOING;a<f;a+=4)put32((BYTE *)(uintptr_t)a,fill);
    uint64_t pair=mrt_core(regs,f,&got[2]);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);mrt_globals(actual,0);
    if(mrt_fs0()!=head)return fail("mission_root core left FS:0 changed");
    if(want[0]!=got[0]||want[1]!=got[1]||want[2]!=got[2]||calls!=mrt_calls||memcmp(expected,mrt_log,calls*sizeof(*expected))||
       memcmp(mrt_after,mrt_arena,MRT_SIZE)||memcmp(after,actual,MRT_GLOBALS))
        return mrt_report("core",n,want,got,calls,expected,after,actual);
    if(memcmp(frame_want,(const BYTE *)(uintptr_t)(f-ES2_MISSION_ROOT_LOCALS),ES2_MISSION_ROOT_LOCALS)){
        const BYTE *g2=(const BYTE *)(uintptr_t)(f-ES2_MISSION_ROOT_LOCALS);
        for(unsigned j=0;j<ES2_MISSION_ROOT_LOCALS;++j)if(frame_want[j]!=g2[j]){
            fprintf(stderr,"FAIL: mission_root case %u frame f-%x %02x/%02x\n",n,ES2_MISSION_ROOT_LOCALS-j,frame_want[j],g2[j]);break;}
        return 1;
    }
    /* (b) the adapter's plain path (its frame is a zeroed buffer: zero pattern only) */
    if(fill){mrt_globals(before,1);return 0;}
    memcpy(mrt_arena,mrt_before,MRT_SIZE);mrt_globals(before,1);mrt_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[80]={0};
    if(count>80)return fail("mission_root counter table too long");
    for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair=x->replacement_context(dummy,d->user,regs);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);got[2]=want[2];
    mrt_globals(actual,0);
    if(mrt_fs0()!=head)return fail("mission_root adapter left FS:0 changed");
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=mrt_hits(c+i)){
        fprintf(stderr,"FAIL: mission_root case %u counter %s delta %llu, expected %u\n",n,c[i].name,
            (unsigned long long)(c[i].count-counts[i]),mrt_hits(c+i));
        return 1;
    }
    if(want[0]!=got[0]||want[1]!=got[1]||calls!=mrt_calls||memcmp(expected,mrt_log,calls*sizeof(*expected))||
       memcmp(mrt_after,mrt_arena,MRT_SIZE)||memcmp(after,actual,MRT_GLOBALS))
        return mrt_report("adapter",n,want,got,calls,expected,after,actual);
    mrt_globals(before,1);
    return 0;
}
/* Frame probe through the entry shim: the original 461608 (real 49630c)
 * and the installed replacement must agree in EAX/EDX/ECX, the records
 * (EBX/ESI/EDI, EBP/ESP relative to the caller's ESP, the residue hash
 * below each callee, the FS:0 head and its record), the arena, the
 * globals, FS:0 after return, the x87 environment and the 1,024 bytes
 * below the caller's ESP; then the depth probe. */
static int mrt_frame_probe(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    static mrt_record expected[MRT_MAX];static BYTE expected_frame[1024];
    mrt_track=1;mrt_hash_on=1;mrt_window_on=1;mrt_clobbers=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t regs[6],want[3],got[3];const uint32_t pattern=poison?0xa7a7a7a7u:0x31313131u,head=mrt_fs0();mrt_build(n*41u+3u,regs);
        BYTE before[MRT_GLOBALS],after[MRT_GLOBALS],actual[MRT_GLOBALS],efpu[108],afpu[108];
        memcpy(mrt_before,mrt_arena,MRT_SIZE);mrt_globals(before,0);
        mrt_reset();mrt_track_run(mapped+d->rva,pattern,want);es2_hook_fpu_save(efpu);memcpy(expected_frame,mrt_frame_copy,1024);
        const unsigned calls=mrt_calls;memcpy(expected,mrt_log,calls*sizeof(*expected));memcpy(mrt_after,mrt_arena,MRT_SIZE);mrt_globals(after,0);
        if(mrt_fs0()!=head){status=fail("mission_root original left FS:0 changed");break;}
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("mission_root frame install");
        memcpy(mrt_arena,mrt_before,MRT_SIZE);mrt_globals(before,1);mrt_reset();
        mrt_track_run(mapped+d->rva,pattern,got);es2_hook_fpu_save(afpu);mrt_globals(actual,0);
        if(mrt_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||want[2]!=got[2]||calls!=mrt_calls||memcmp(expected,mrt_log,calls*sizeof(*expected))||
           memcmp(mrt_after,mrt_arena,MRT_SIZE)||memcmp(after,actual,MRT_GLOBALS)||memcmp(efpu,afpu,12)||memcmp(efpu+28,afpu+28,80)||h.calls!=1)
            status=mrt_report(fast?"frame fast":"frame preserving",n,want,got,calls,expected,after,actual);
        else if(memcmp(expected_frame,mrt_frame_copy,1024)){
            fprintf(stderr,"FAIL: %s case %u (frame window)\n",current_function,n);
            for(unsigned j=0;j<1024;++j)if(expected_frame[j]!=mrt_frame_copy[j]){fprintf(stderr," below-%u: %02x/%02x\n",1024-j,expected_frame[j],mrt_frame_copy[j]);break;}
            status=1;}
        if(!es2_hook_uninstall(&h))return fail("mission_root frame uninstall");mrt_globals(before,1);++total;
    }
    mrt_track=0;mrt_hash_on=0;mrt_window_on=0;
    /* Depth: the shimmed root's deepest stack write, both dispatch paths. */
    uint32_t deepest[2]={0,0};
    for(unsigned fast=0;fast<2&&!status;++fast){
        uint32_t regs[6];mrt_build(fast+7u,regs);BYTE before[MRT_GLOBALS];mrt_globals(before,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("mission_root depth install");
        mrt_reset();mrt_depth_run(mapped+d->rva,0x5ca1ab1eu);deepest[fast]=mrt_deepest;
        if(!es2_hook_uninstall(&h))return fail("mission_root depth uninstall");mrt_globals(before,1);
        const uint32_t gap=MISSION_ROOT_PAGES*4096u+8u;   /* return address, PUSH EBP, the gap */
        if(h.calls!=1||mrt_deepest<=gap||mrt_deepest-gap>8192u){
            fprintf(stderr,"FAIL: %s depth probe (%s): deepest write %u bytes below the caller, gap %u\n",current_function,fast?"fast":"preserving",mrt_deepest,gap);status=1;}
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status&&!mrt_clobbers){fprintf(stderr,"FAIL: %s frame probe: no changed argument slot was observed\n",current_function);status=1;}
    if(!status)printf("PASS: %s %u frame cases through the entry shim, fast/preserving dispatch, 31/a7 stack, real 49630c, per-call EAX..EDI, EBP/ESP, residue below each callee, FS:0 head and record, changed argument slots (%u in both runs), EAX/EDX/ECX, nonempty x87, 1024 bytes below the caller; deepest stack write %u (fast) / %u (preserving) bytes below the caller = return address, PUSH EBP, %u-page gap + %u / %u context and host bytes\n",
        d->name,total,mrt_clobbers,deepest[1],deepest[0],MISSION_ROOT_PAGES,deepest[1]-MISSION_ROOT_PAGES*4096u-8u,deepest[0]-MISSION_ROOT_PAGES*4096u-8u);
    return status;
}
/* Real-callee pass: 461608 over the real 49630c, 471c70 (with 471c18),
 * 478628 and 49288c (the rest stubbed), mission_root, pool_prev,
 * pool_next, removal_append and dist_gate original/replaced in all 32
 * combinations against the all-original run. */
static int mrt_real(unsigned index){
    const unsigned set[5]={index,ES2_HOOK_INDEX_pool_prev,ES2_HOOK_INDEX_pool_next,ES2_HOOK_INDEX_removal_append,ES2_HOOK_INDEX_dist_gate};
    BYTE saved[MRT_IDS][5];int active[MRT_IDS];if(mrt_patch(saved,active,MRT_REAL,1))return 1;
    int status=0;unsigned total=0;uint64_t entered[5]={0,0,0,0,0};static mrt_record expected[MRT_MAX];
    mrt_track=1;
    for(unsigned mode=0;mode<32&&!status;++mode)for(unsigned n=0;n<32&&!status;++n){
        uint32_t regs[6],want[3],got[3];const uint32_t pattern=n&1?0xa7a7a7a7u:0x31313131u,head=mrt_fs0();mrt_build(n*67u+mode*7u,regs);
        BYTE before[MRT_GLOBALS],after[MRT_GLOBALS],actual[MRT_GLOBALS];
        memcpy(mrt_before,mrt_arena,MRT_SIZE);mrt_globals(before,0);
        mrt_reset();mrt_track_run(mapped+function_hook_descriptor(index)->rva,pattern,want);
        const unsigned calls=mrt_calls;memcpy(expected,mrt_log,calls*sizeof(*expected));memcpy(mrt_after,mrt_arena,MRT_SIZE);mrt_globals(after,0);
        es2_hook hk[5];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<5;++j){o.mode=(mode>>j&1)?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(set[j]),&o,function_hook_extension(set[j])))return fail("mission_root real install");}
        memcpy(mrt_arena,mrt_before,MRT_SIZE);mrt_globals(before,1);mrt_reset();
        mrt_track_run(mapped+function_hook_descriptor(index)->rva,pattern,got);mrt_globals(actual,0);
        /* A replaced removal_append returns the framework's ECX: dead at both
         * drains (the first flush passes it on to removal_flush, which
         * overwrites it after 496406 or returns it unread, and the root
         * reloads ECX before pool_prev), so the first flush's entry ECX is
         * not compared then. */
        if(mode>>3&1)for(unsigned j=0;j<calls&&j<mrt_calls;++j)if(expected[j].id==30||expected[j].id==33)expected[j].r[2]=mrt_log[j].r[2]=0;
        /* Likewise a replaced dist_gate's ECX (the original leaves an address
         * of its own frame): it passes the building walk's pool_prev unread
         * and reaches 42eb40 or 45d548 (when 46313c does not run), which
         * write ECX before reading it. */
        if(mode>>4&1)for(unsigned j=0;j<calls&&j<mrt_calls;++j)if(expected[j].id==49||expected[j].id==52)expected[j].r[2]=mrt_log[j].r[2]=0;
        if(mrt_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||want[2]!=got[2]||calls!=mrt_calls||memcmp(expected,mrt_log,calls*sizeof(*expected))||
           memcmp(mrt_after,mrt_arena,MRT_SIZE)||memcmp(after,actual,MRT_GLOBALS)){
            fprintf(stderr,"real mode=%u\n",mode);status=mrt_report("real",n,want,got,calls,expected,after,actual);}
        for(unsigned j=0;j<5;++j){if(mode>>j&1)entered[j]+=hk[j].calls;
            if(!es2_hook_uninstall(hk+j))return fail("mission_root real uninstall");}
        mrt_globals(before,1);++total;
    }
    mrt_track=0;
    if(mrt_patch(saved,active,0,0))return 1;
    if(!status&&(!entered[0]||!entered[1]||!entered[2]||!entered[3]||!entered[4])){
        fprintf(stderr,"FAIL: %s real pass: entered %llu/%llu/%llu/%llu/%llu\n",current_function,(unsigned long long)entered[0],
            (unsigned long long)entered[1],(unsigned long long)entered[2],(unsigned long long)entered[3],(unsigned long long)entered[4]);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases over the real 49630c/471c70/471c18/478628/49288c (mission_root, pool_prev, pool_next, removal_append, dist_gate original/replaced in all 32 combinations; replaced entries %llu/%llu/%llu/%llu/%llu), 31/a7 stack, EAX/EDX/ECX, calls with EBX/ESI/EDI, EBP/ESP and FS:0 record, arena, globals\n",
        current_function,total,(unsigned long long)entered[0],(unsigned long long)entered[1],(unsigned long long)entered[2],
        (unsigned long long)entered[3],(unsigned long long)entered[4]);
    return status;
}
static int mission_root_cases(unsigned index,unsigned limit){
    current_function=function_hook_descriptor(index)->name;
    unsigned bytes=0;for(unsigned i=0;i<sizeof(mrt_global_list)/sizeof(mrt_global_list[0]);++i)bytes+=mrt_global_list[i].size;
    if(bytes!=MRT_GLOBALS)return fail("mission_root global list size");
    BYTE old_globals[MRT_GLOBALS];mrt_globals(old_globals,0);
    {const stack_layout s=current_stack();mrt_stack_lo=(uint32_t)(uintptr_t)s.allocation;mrt_stack_hi=(uint32_t)(uintptr_t)s.base;}
    BYTE saved[MRT_IDS][5];int active[MRT_IDS];
    if(mrt_patch(saved,active,MRT_ALL,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_MISSION_ROOT_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);memset(mrt_paths,0,sizeof(mrt_paths));mrt_track=0;mrt_hash_on=0;mrt_window_on=0;
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==32768)coverage_enable(0);
        uint32_t regs[6];mrt_build(n,regs);status=mrt_compare(index,n,regs,n%4==0?0u:0x01010101u*(1u+n%255u));}
    if(!limit)coverage_enable(0);current_case_valid=0;
    mrt_globals(old_globals,1);
    if(mrt_patch(saved,active,MRT_ALL,0))return 1;
    if(!limit&&!status){
        if(mrt_patch(saved,active,MRT_PROBE,1))return 1;
        status=mrt_frame_probe(index,256);
        if(mrt_patch(saved,active,MRT_PROBE,0))return 1;
        mrt_globals(old_globals,1);
    }
    if(!limit&&!status)status=mrt_real(index);
    mrt_globals(old_globals,1);
    if(!status){
        printf("PASS: %-20s %u cases (stub calls:",function_hook_descriptor(index)->name,cases);
        for(unsigned i=0;i<MRT_IDS;++i)printf(" %06x %u",i==MRT_SLOT_ID?0x4a5824u:mrt_addr[i],mrt_paths[i]);
        printf("); EAX/EDX/ECX (core), ordered calls with EAX..EDI, slot counts and frame buffers, the core's frame, arena, globals, FS:0\n");
    }
    return status;
}
