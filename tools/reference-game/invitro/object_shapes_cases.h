/* object_shapes group: ts_obj_ctor 402278, det_anim_shape 40332c,
 * det_anim_static 403368, det_anim_record 4033a4, anim_instance_ctor 478838,
 * object_track 402374, thread_add 478a3c, thread_ctor 478e48 and
 * shape_word_block 402fc0 (docs/reverse-engineering/mission_load.md, "Object
 * shape set-up").
 *
 * Direct cases run the original body (EAX/EDX/ECX, EBX, ESI and EDI at entry
 * from the case, six argument slots, over a stack filled with a case
 * pattern) and the core against logging stubs for every game callee of the
 * hook under test: 49630c (a record stub: the real FS:0 head at record+0,
 * context word +10h zeroed, the record linked into FS:0), new 47749c, the
 * group's own hooks when they are callees (402278, 478838, 478e48, 478a3c),
 * object_init 402188, shape_instance 490b64, new[] 4774f0, 49667c,
 * node_matrices 478c64, track_clip 4792ac, track_rate 4796d8, track_sort
 * 4789d0, the arena allocation 47a288, and the CW3220 thunks memset 49681a
 * and memcpy 49680e (stubs that fill or copy and return random registers).
 * Stack addresses in logged registers and slots are masked. Compared: the
 * full return pair, ECX, the ordered calls with argument slots (and the
 * bridge's slot count), entry EAX/EDX/ECX and the record's context word,
 * the arena, the image globals (the counter pointer, the heap, the global
 * rotation 6C56A8h) and FS:0. The stubs change what the originals reread
 * (the counter pointer after the constructors, [O+34h] and the shape chain
 * after new and the thread calls, the node count word I+0Ch and the shape's
 * tables after new[] and 49667c, the matrix array pointer I+16h after
 * node_matrices, the block's shape and the word count after 47a288), so the
 * order of reads and writes is observable. In even cases the inputs are the
 * real callers' (valid objects, non-null allocations, small node counts)
 * and the adapter is compared as well, with the callee counter deltas.
 *
 * Then installed 31/a7 poison probes and real-callee passes: the
 * constructors over the real 49630c, 402278, 478838, object_init, 49667c
 * with its element constructors 4922e6 and matrix_ctor (new, new[],
 * shape_instance and node_matrices stubs); anim_instance_ctor over the real
 * shape_instance (47a288 a bump allocator) and 49667c; object_track,
 * thread_ctor and thread_add over the real thread functions, track_clip
 * (its shape node's slot +20h a stub), clip_pose, track_rate, track_sort and
 * track_count on a synthetic node table; the nested hooks in rotating
 * original/replaced modes, the hook under test original or replaced
 * against a run without it. Identifiers use the prefix osh_/OSH_. */
typedef struct osh_record { uint32_t id,slots,args[5],regs[3],context,eax,edx; } osh_record;
enum {
    OSH_SIZE=0x4000, OSH_MAX=64, OSH_IDS=18,
    OSH_OBJ=0x100, OSH_INST=0x200, OSH_SHAPE=0x280, OSH_SHAPE2=0x2c0, OSH_NT=0x300, OSH_NT2=0x320, OSH_IDX=0x340,
    OSH_POSE=0x400, OSH_FLAGS=0x700, OSH_ARR1=0x780, OSH_ARR2=0x900, OSH_REC=0xa00, OSH_WORDS=0xb00, OSH_COPY=0xd00,
    OSH_COUNTER=0xf00, OSH_COUNTER2=0xf10, OSH_NODES=0x1000, OSH_NODE=0x1040, OSH_VT=0x1140, OSH_NWORDS=0x1180,
    OSH_THREADS=0x1200, OSH_BUMP=0x2000
};
#define OSH_B ((uint32_t)(uintptr_t)osh_arena)
#define OSH_A(o) (OSH_B+(uint32_t)(o))
#define OSH_BIT(i) ((uint32_t)1<<(i))
static BYTE osh_arena[OSH_SIZE],osh_before[OSH_SIZE],osh_after[OSH_SIZE];
static osh_record osh_log[OSH_MAX];
static unsigned osh_calls,osh_real,osh_effects,osh_index,osh_paths[OSH_IDS];
static uint32_t osh_seed,osh_stream,osh_bump,osh_head,osh_obj;
__attribute__((used)) static uint32_t osh_ecx;
/* 0 49630c, 1 new 47749c, 2 478838, 3 memset 49681a, 4 402278, 5
 * object_init 402188, 6 shape_instance 490b64, 7 new[] 4774f0, 8 49667c, 9
 * node_matrices 478c64, 10 478e48, 11 track_clip 4792ac, 12 track_rate
 * 4796d8, 13 478a3c, 14 track_sort 4789d0, 15 47a288, 16 memcpy 49680e,
 * 17 the shape node's slot +20h (real passes, called through the arena
 * vtable, never patched). The slots each original call site pushes. */
static const uint32_t osh_addresses[OSH_IDS]={0x49630c,0x47749c,0x478838,0x49681a,0x402278,0x402188,0x490b64,0x4774f0,0x49667c,
    0x478c64,0x478e48,0x4792ac,0x4796d8,0x478a3c,0x4789d0,0x47a288,0x49680e,0};
static const unsigned osh_counts[OSH_IDS]={0,1,2,3,2,2,2,1,5,1,2,4,2,2,1,2,3,2};
static uint32_t osh_next(void){
    uint32_t z=osh_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *osh_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static uint32_t osh_fs0(void){uint32_t v;__asm__ volatile("movl %%fs:0,%0":"=r"(v));return v;}
static int osh_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static void osh_reset(void){osh_calls=0;osh_stream=osh_seed;osh_bump=OSH_A(OSH_BUMP);osh_head=osh_fs0();}
static void osh_random(unsigned offset,unsigned size){for(unsigned i=0;i<size;++i)osh_arena[offset+i]=(BYTE)osh_next();}
static uint32_t osh_alloc(uint32_t size){
    const uint32_t p=osh_bump;size=(size+15u)&~7u;
    if(size>0x800u||p+size>OSH_A(OSH_SIZE)){fail("object_shapes bump allocator overflow");ExitProcess(1);}
    memset(osh_host(p),0,size);osh_bump+=size;return p;
}
/* A guest range the memset/memcpy stubs may touch: the arena or the global
 * rotation of the image. */
static int osh_range(uint32_t p,uint32_t n){
    if(p-OSH_B<OSH_SIZE&&n<=OSH_SIZE-(p-OSH_B))return 1;
    const uint32_t g=(uint32_t)(uintptr_t)(mapped+0x2c56a8);return p==g&&n<=0x20u;
}
/* What the stubs answer and change. */
static uint32_t osh_answer(unsigned id,const uint32_t *a,uint32_t eax,uint32_t pick){
    const int effect=osh_effects&&pick%4==0;
    switch(id){
    case 1:   /* new: a block, null in odd cases; the counter pointer or O+34h's chain reread */
        if(osh_real)return osh_alloc(a[0]);
        if(effect){
            if(osh_index==ES2_HOOK_INDEX_object_track){
                if(pick&0x100)put32(osh_host(osh_obj+0x34),OSH_A(pick&0x200?OSH_INST+0x40:OSH_INST));
                else put32(osh_arena+OSH_SHAPE+0x2a,osh_next());
            } else put32(mapped+0x2cbee4,OSH_A(OSH_COUNTER2));
        }
        if(osh_effects&&pick%8==1)return 0;
        return osh_index==ES2_HOOK_INDEX_object_track?OSH_A(OSH_COPY+4*(osh_next()%4)):osh_next();
    case 2:case 4:case 5:   /* the constructors: the counter pointer */
        if(effect)put32(mapped+0x2cbee4,OSH_A(pick&0x100?OSH_COUNTER2:OSH_COUNTER));
        return osh_real?a[0]:eax;
    case 3:   /* memset */
        if(!osh_range(a[0],a[2])){fail("object_shapes memset outside the arena");ExitProcess(1);}
        memset(osh_host(a[0]),(int)(a[1]&0xff),a[2]);return eax;
    case 6:   /* shape_instance: I+4 = the shape (another one as an effect) */
        put32(osh_host(a[0]+4),effect&&(pick&0x100)?OSH_A(OSH_SHAPE2):a[1]);
        return osh_real?a[0]:eax;
    case 7:   /* new[]: the flag bytes; the node count word reread */
        if(osh_real)return osh_alloc(a[0]&0xffffu);
        if(effect)put16(osh_host(osh_obj+0x0c),(uint16_t)(osh_next()%7));
        return OSH_A(OSH_FLAGS);
    case 8:   /* 49667c: the two arrays; the node count, the shape, the tables reread */
        if(effect)switch(pick>>8&3){
        case 0:put16(osh_host(osh_obj+0x0c),(uint16_t)(osh_next()%7));break;
        case 1:put32(osh_host(osh_obj+4),OSH_A(OSH_SHAPE2));break;
        case 2:put32(osh_arena+OSH_NT+0x18,OSH_A(OSH_IDX+2*(osh_next()%4)));break;
        default:put32(osh_arena+OSH_NT+0x12,OSH_A(OSH_POSE+0x60+12*(osh_next()%4)));break;
        }
        return OSH_A(a[1]==0x0cu?OSH_ARR1:OSH_ARR2);
    case 9:   /* node_matrices: the matrix array pointer or the counter pointer */
        if(effect){if(pick&0x100)put32(osh_host(osh_obj+0x16),OSH_A(OSH_ARR2+0x40));else put32(mapped+0x2cbee4,OSH_A(OSH_COUNTER2));}
        return eax;
    case 10:case 11:case 12:   /* the thread calls: O+34h reread for thread_add */
        if(effect&&osh_index==ES2_HOOK_INDEX_object_track)put32(osh_host(osh_obj+0x34),OSH_A(OSH_INST+0x40*(pick>>8&1)));
        return eax;
    case 15:  /* 47a288: the copy block; the block's shape or its word array reread */
        if(osh_real)return osh_alloc(a[0]);
        if(effect)switch(pick>>8&3){
        case 0:put32(osh_host(osh_obj),OSH_A(OSH_SHAPE2));break;
        case 1:put16(osh_arena+OSH_SHAPE+0x24,(uint16_t)(osh_next()%0x80));break;
        default:put32(osh_arena+OSH_SHAPE+0x1c,OSH_A(OSH_WORDS+2*(osh_next()%8)));break;
        }
        return OSH_A(OSH_COPY);
    case 16:  /* memcpy */
        if(!osh_range(a[0],a[2])||!osh_range(a[1],a[2])){fail("object_shapes memcpy outside the arena");ExitProcess(1);}
        memmove(osh_host(a[0]),osh_host(a[1]),a[2]);return eax;
    }
    return eax;
}
__attribute__((used,noinline)) static uint64_t __cdecl osh_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(osh_calls==OSH_MAX){fail("object_shapes call overflow");ExitProcess(1);}
    osh_record *v=osh_log+osh_calls++;memset(v,0,sizeof(*v));v->id=id;
    v->slots=weapons_bridge_slots(a,osh_counts[id]);
    for(unsigned k=0;k<osh_counts[id];++k)v->args[k]=osh_stack(a[k])?0:a[k];
    for(unsigned j=0;j<3;++j)v->regs[j]=osh_stack(regs[j])?0:regs[j];
    if(id&&osh_fs0()!=osh_head)v->context=get16(osh_host(osh_fs0()+16u));         /* a hook's own record */
    uint32_t eax=osh_next(),edx=osh_next(),pick=osh_next();osh_ecx=osh_next();
    if(id==0)eax=osh_fs0();
    else eax=osh_answer(id,a,eax,pick);
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define OSH_STUB(n) __attribute__((naked)) static void osh_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _osh_stub_impl\n\taddl $24,%esp\n\tmovl _osh_ecx,%ecx\n\tret\n\t");}
OSH_STUB(1) OSH_STUB(2) OSH_STUB(3) OSH_STUB(4) OSH_STUB(5) OSH_STUB(6) OSH_STUB(7) OSH_STUB(8) OSH_STUB(9) OSH_STUB(10)
OSH_STUB(11) OSH_STUB(12) OSH_STUB(13) OSH_STUB(14) OSH_STUB(15) OSH_STUB(16) OSH_STUB(17)
#undef OSH_STUB
/* 49630c: logs like the other stubs (EDX and ECX come back unchanged, as the
 * original's do), then links a record at EBP + [descriptor+4] into FS:0
 * with the saved head at record+0 and a zero context word. */
__attribute__((naked)) static void osh_stub_0(void){__asm__ volatile(
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
    "pushl $0\n\tcalll _osh_stub_impl\n\taddl $12,%esp\n\t"
    "popl %ecx\n\tmovl 4(%ecx),%ecx\n\taddl %ebp,%ecx\n\tmovl %eax,(%ecx)\n\tmovw $0,16(%ecx)\n\tmovl %ecx,%fs:0\n\t"
    "popl %edx\n\tpopl %ecx\n\tret\n\t");}
static void *osh_stubs[OSH_IDS]={osh_stub_0,osh_stub_1,osh_stub_2,osh_stub_3,osh_stub_4,osh_stub_5,osh_stub_6,osh_stub_7,osh_stub_8,
    osh_stub_9,osh_stub_10,osh_stub_11,osh_stub_12,osh_stub_13,osh_stub_14,osh_stub_15,osh_stub_16,osh_stub_17};
static int osh_patch(BYTE saved[OSH_IDS][5],int active[OSH_IDS],uint32_t which,int on){
    for(unsigned i=0;i<OSH_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!osh_addresses[i])continue;if(actor_patch(osh_addresses[i]-0x400000u,osh_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(osh_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
/* Direct callees of each hook (bit i = osh_addresses[i]). */
static uint32_t osh_which(unsigned index){
    if(index==ES2_HOOK_INDEX_ts_obj_ctor)return OSH_BIT(0)|OSH_BIT(1)|OSH_BIT(2)|OSH_BIT(3);
    if(index==ES2_HOOK_INDEX_det_anim_shape||index==ES2_HOOK_INDEX_det_anim_record)return OSH_BIT(4);
    if(index==ES2_HOOK_INDEX_det_anim_static)return OSH_BIT(5);
    if(index==ES2_HOOK_INDEX_anim_instance_ctor)return OSH_BIT(3)|OSH_BIT(6)|OSH_BIT(7)|OSH_BIT(8)|OSH_BIT(9);
    if(index==ES2_HOOK_INDEX_object_track)return OSH_BIT(0)|OSH_BIT(1)|OSH_BIT(10)|OSH_BIT(11)|OSH_BIT(12)|OSH_BIT(13);
    if(index==ES2_HOOK_INDEX_thread_add)return OSH_BIT(14);
    if(index==ES2_HOOK_INDEX_thread_ctor)return OSH_BIT(3)|OSH_BIT(11);
    return OSH_BIT(15)|OSH_BIT(16);
}
/* Stubbed in the real-callee passes (the other callees run for real; the
 * CW3220 memset and memcpy are the harness's own). */
static uint32_t osh_real_which(unsigned index){
    if(index==ES2_HOOK_INDEX_det_anim_static)return OSH_BIT(1)|OSH_BIT(6);
    if(index==ES2_HOOK_INDEX_anim_instance_ctor)return OSH_BIT(7)|OSH_BIT(9)|OSH_BIT(15);
    if(index==ES2_HOOK_INDEX_object_track)return OSH_BIT(1);
    if(index==ES2_HOOK_INDEX_thread_add||index==ES2_HOOK_INDEX_thread_ctor)return 0;
    return OSH_BIT(1)|OSH_BIT(6)|OSH_BIT(7)|OSH_BIT(9);   /* ts_obj_ctor, det_anim_shape, det_anim_record */
}
/* The image globals: the counter pointer, the heap, the global rotation. */
static const struct { uint32_t rva,size; } osh_ranges[]={{0x2cbee4,4},{0x2c5830,4},{0x2c56a8,32}};
enum { OSH_GLOBALS=4+4+32 };
static void osh_globals(BYTE *out,int restore){
    for(unsigned i=0;i<sizeof(osh_ranges)/sizeof(osh_ranges[0]);++i){
        if(restore)memcpy(mapped+osh_ranges[i].rva,out,osh_ranges[i].size);else memcpy(out,mapped+osh_ranges[i].rva,osh_ranges[i].size);
        out+=osh_ranges[i].size;
    }
}
static int osh_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<OSH_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
/* The synthetic node table M (OSH_NT): +6 the four node pointers, +12h the
 * 12-byte pose table (entries -8 .. 55), +16h the node count, +18h the
 * pose index words. Each node: the vtable (slot +20h the stub 17), the
 * priority word +6, the clip word +8, the word arrays +0Ch and +16h, the
 * stride word +10h. `threads` valid threads in the instance's slots. */
static void osh_tree(uint32_t instance,unsigned nodes,int exact,unsigned threads){
    BYTE *M=osh_arena+OSH_NT;
    put32(M+6,OSH_A(OSH_NODES));put32(M+0x12,OSH_A(OSH_POSE+0x60));put16(M+0x16,(uint16_t)nodes);put32(M+0x18,OSH_A(OSH_IDX));
    if(!exact&&osh_next()%8==0)put16(M+0x16,(uint16_t)(0x8000u|osh_next()));
    for(unsigned i=0;i<16;++i)put16(osh_arena+OSH_IDX+2*i,(uint16_t)(osh_next()%48u-8u));
    memcpy(osh_arena+OSH_NT2,M,0x20);put16(osh_arena+OSH_NT2+0x16,(uint16_t)(osh_next()%7));
    put32(osh_arena+OSH_SHAPE+0x2a,OSH_A(OSH_NT));put32(osh_arena+OSH_SHAPE2+0x2a,OSH_A(OSH_NT2));
    put32(osh_arena+OSH_VT+0x20,(uint32_t)(uintptr_t)osh_stub_17);
    for(unsigned k=0;k<4;++k){
        BYTE *N=osh_arena+OSH_NODE+0x40*k;
        put32(osh_arena+OSH_NODES+4*k,OSH_A(OSH_NODE+0x40*k));
        put32(N,OSH_A(OSH_VT));put16(N+8,(uint16_t)(osh_next()%2));put32(N+0xc,OSH_A(OSH_NWORDS));
        put16(N+0x10,(uint16_t)(osh_next()%3));put32(N+0x16,OSH_A(OSH_NWORDS+0x20));
    }
    for(unsigned i=0;i<16;++i)put16(osh_arena+OSH_NWORDS+0x20+2*i,(uint16_t)(osh_next()%8));
    for(unsigned i=0;i<10;++i){
        const uint32_t T=OSH_A(OSH_THREADS+0x50*i);
        put32(osh_host(T),OSH_A(OSH_NT));put16(osh_host(T+4),(uint16_t)(osh_next()%4));
        put32(osh_host(instance+0x1a+4*i),i<threads?T:0);
    }
}
/* The case data. exact: the real callers' inputs; real: data for the
 * real-callee passes. */
static void osh_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],uint32_t regs[3],uint32_t *fill,int exact,int real){
    osh_seed=random32();osh_stream=osh_seed^n;osh_real=real;osh_effects=!real&&!exact;osh_index=index;
    for(unsigned i=0;i<3;++i){r[i]=osh_next();regs[i]=osh_next();}
    osh_random(0,OSH_SIZE);memset(a,0,24);
    put32(mapped+0x2cbee4,OSH_A(OSH_COUNTER));put32(mapped+0x2c5830,osh_next());
    for(unsigned i=0;i<32;++i)mapped[0x2c56a8+i]=(BYTE)osh_next();
    *fill=!exact&&!real&&osh_next()%2?osh_next():0;
    const uint32_t O=OSH_A(OSH_OBJ+osh_next()%4);osh_obj=O;a[0]=O;
    const unsigned k=osh_next()%4;
    if(index==ES2_HOOK_INDEX_ts_obj_ctor||index==ES2_HOOK_INDEX_det_anim_shape||index==ES2_HOOK_INDEX_det_anim_static||
       index==ES2_HOOK_INDEX_det_anim_record){
        const uint32_t S=k==0&&!real?0:exact||real?OSH_A(OSH_SHAPE):osh_next();
        if(index==ES2_HOOK_INDEX_det_anim_record){
            a[1]=OSH_A(OSH_REC);put32(osh_arena+OSH_REC,OSH_A(OSH_REC+0x10));put32(osh_arena+OSH_REC+0x10,S);
        } else a[1]=S;
        if(real){
            osh_tree(O,osh_next()%5,1,0);
            put32(osh_arena+OSH_SHAPE+0x1c,OSH_A(OSH_WORDS));put16(osh_arena+OSH_SHAPE+0x24,(uint16_t)(osh_next()%6));
        }
    } else if(index==ES2_HOOK_INDEX_anim_instance_ctor){
        a[1]=OSH_A(OSH_SHAPE);
        osh_tree(O,osh_next()%7,exact||real,0);
        put32(osh_arena+OSH_SHAPE+0x1c,OSH_A(OSH_WORDS));put16(osh_arena+OSH_SHAPE+0x24,(uint16_t)(osh_next()%6));
    } else if(index==ES2_HOOK_INDEX_object_track){
        put32(osh_host(O+0x34),OSH_A(OSH_INST));put32(osh_arena+OSH_INST+4,OSH_A(OSH_SHAPE));put32(osh_arena+OSH_INST+0x44,OSH_A(OSH_SHAPE));
        osh_tree(OSH_A(OSH_INST),4,1,osh_next()%11);
        a[1]=exact||real?(osh_next()<<16)|osh_next()%4:osh_next();
    } else if(index==ES2_HOOK_INDEX_thread_add){
        const unsigned used=osh_next()%11;
        osh_tree(O,4,1,used);
        if(!exact&&!real)for(unsigned i=0;i<10;++i)if(osh_next()%3==0)put32(osh_host(O+0x1a+4*i),osh_next()%2?0:osh_next());
        a[1]=real?OSH_A(OSH_COPY):osh_next();
        if(real)put32(osh_arena+OSH_COPY,OSH_A(OSH_NT)),put16(osh_arena+OSH_COPY+4,(uint16_t)(osh_next()%4));
    } else if(index==ES2_HOOK_INDEX_thread_ctor){
        osh_tree(OSH_A(OSH_INST),4,1,0);
        a[1]=exact||real?OSH_A(OSH_NT):osh_next();
    } else {
        a[1]=k==0?0:OSH_A(OSH_SHAPE);
        put32(osh_arena+OSH_SHAPE+0x1c,OSH_A(OSH_WORDS+2*(osh_next()%4)));
        put16(osh_arena+OSH_SHAPE+0x24,(uint16_t)(k==1?0:osh_next()%(exact?0x40u:0x80u)));
        put32(osh_arena+OSH_SHAPE2+0x1c,OSH_A(OSH_WORDS+0x40));put16(osh_arena+OSH_SHAPE2+0x24,(uint16_t)(osh_next()%0x40));
    }
    osh_stream=osh_seed;   /* the stubs' stream restarts here in every run */
}
/* Calls target with six argument slots, EAX/EDX/ECX = r and EBX/ESI/EDI =
 * regs over 3,840 bytes of stack filled with `fill`; out receives EAX, EDX
 * and ECX after the call. */
__attribute__((naked,noinline)) static void osh_invoke(void *target,const uint32_t *a,const uint32_t *r,const uint32_t *regs,uint32_t fill,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 24(%ebp),%eax\n\tmovl $1024,%ecx\n\tcld\n\trep stosl\n\taddl $3840,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 20(%esi)\n\tpushl 16(%esi)\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 20(%ebp),%edi\n\tmovl (%edi),%ebx\n\tmovl 4(%edi),%esi\n\tmovl 8(%edi),%edi\n\t"
        "movl 16(%ebp),%eax\n\tmovl 4(%eax),%edx\n\tmovl 8(%eax),%ecx\n\tmovl (%eax),%eax\n\tcall *8(%ebp)\n\t"
        "movl 28(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* The core with the adapter's graph and scratch; FS:0 restored to the
 * record's saved head as the adapters do. */
static uint64_t osh_core(unsigned index,const uint32_t *a,const uint32_t *r,uint32_t *ecx){
    const es2_guest_memory *m=&math_native_memory;
    if(index==ES2_HOOK_INDEX_ts_obj_ctor||index==ES2_HOOK_INDEX_object_track){
        BYTE scratch[ES2_OBJECT_SHAPES_SCRATCH]={0};es2_weapon_graph g=weapons_graph(mapped,scratch,index);
        weapons_impact_context c={(uint32_t)(uintptr_t)mapped,(uint32_t)(uintptr_t)(scratch+ES2_OBJECT_SHAPES_FRAME)};
        g.call=weapons_impact_guest_call;g.context=&c;
        if(index==ES2_HOOK_INDEX_ts_obj_ctor){
            uint32_t head=0;
            const uint64_t v=es2_ts_obj_ctor_registers(m,&g,a[0],a[1],r,&head);
            __asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");
            if(ecx)*ecx=head;
            return v;
        }
        const uint64_t v=es2_object_track_registers(m,&g,a[0],a[1],r,ecx);
        const uint32_t head=(uint32_t)(v>>32);__asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");
        return v;
    }
    es2_weapon_graph g=weapons_graph(mapped,NULL,index);
    if(index==ES2_HOOK_INDEX_det_anim_shape)return es2_det_anim_shape_registers(m,&g,a[0],a[1],r,ecx);
    if(index==ES2_HOOK_INDEX_det_anim_static)return es2_det_anim_static_registers(m,&g,a[0],a[1],r,ecx);
    if(index==ES2_HOOK_INDEX_det_anim_record)return es2_det_anim_record_registers(m,&g,a[0],a[1],r,ecx);
    if(index==ES2_HOOK_INDEX_anim_instance_ctor)return es2_anim_instance_ctor_registers(m,&g,a[0],a[1],r,ecx);
    if(index==ES2_HOOK_INDEX_thread_add)return es2_thread_add_registers(m,&g,a[0],a[1],r,ecx);
    if(index==ES2_HOOK_INDEX_thread_ctor)return es2_thread_ctor_registers(m,&g,a[0],a[1],r,ecx);
    return es2_shape_word_block_registers(m,&g,a[0],a[1],r,ecx);
}
static int osh_report(const char *what,unsigned n,const uint32_t want[3],const uint32_t got[3],unsigned calls,const osh_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x calls %u/%u\n",current_function,n,what,
        want[0],got[0],want[1],got[1],want[2],got[2],calls,osh_calls);
    for(unsigned j=0;j<calls&&j<osh_calls&&expected;++j)if(memcmp(expected+j,osh_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(osh_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<OSH_SIZE;++j)if(osh_after[j]!=osh_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,osh_after[j],osh_arena[j]);break;}
    return 1;
}
static unsigned osh_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<osh_calls;++j)if(osh_addresses[osh_log[j].id]==c->key)++hits;
    return hits;
}
/* One direct case: the original, the core and (exact inputs) the adapter. */
static int osh_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r,const uint32_t *regs,uint32_t fill,int exact,int check_ecx){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[OSH_GLOBALS],after[OSH_GLOBALS],actual[OSH_GLOBALS];static osh_record expected[OSH_MAX];uint32_t want[3],got[3];
    const uint32_t head=osh_fs0();
    memcpy(osh_before,osh_arena,OSH_SIZE);osh_globals(before,0);
    osh_reset();osh_invoke(mapped+d->rva,a,r,regs,fill,want);const unsigned calls=osh_calls;memcpy(expected,osh_log,calls*sizeof(*expected));
    memcpy(osh_after,osh_arena,OSH_SIZE);osh_globals(after,0);
    if(osh_fs0()!=head)return fail("object_shapes original left FS:0 changed");
    for(unsigned j=0;j<calls;++j)++osh_paths[expected[j].id];
    memcpy(osh_arena,osh_before,OSH_SIZE);osh_globals(before,1);osh_reset();
    got[2]=~want[2];
    uint64_t pair=osh_core(index,a,r,&got[2]);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);osh_globals(actual,0);
    if(osh_fs0()!=head)return fail("object_shapes core left FS:0 changed");
    if(want[0]!=got[0]||want[1]!=got[1]||(check_ecx&&want[2]!=got[2])||calls!=osh_calls||memcmp(expected,osh_log,calls*sizeof(*expected))||
       memcmp(osh_after,osh_arena,OSH_SIZE)||osh_globals_differ(after,actual))
        return osh_report("core",n,want,got,calls,expected);
    if(!exact){osh_globals(before,1);return 0;}
    /* the adapter (the framework's entry: six argument slots) */
    memcpy(osh_arena,osh_before,OSH_SIZE);osh_globals(before,1);osh_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];
    if(count>16)return fail("object_shapes counter table too long");
    for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair=x->replacement_context(a,d->user,r);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);got[2]=want[2];
    osh_globals(actual,0);
    if(osh_fs0()!=head)return fail("object_shapes adapter left FS:0 changed");
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=osh_expected_hits(c+i))return fail("object_shapes callee counter delta");
    if(want[0]!=got[0]||want[1]!=got[1]||calls!=osh_calls||memcmp(expected,osh_log,calls*sizeof(*expected))||
       memcmp(osh_after,osh_arena,OSH_SIZE)||osh_globals_differ(after,actual))
        return osh_report("adapter",n,want,got,calls,expected);
    osh_globals(before,1);
    return 0;
}
static int osh_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],regs[3],fill,want[3]={0},got[3]={0},pattern=poison?0xa7a7a7a7u:0x31313131u;
        osh_build(index,n*37u+11u,a,r,regs,&fill,1,0);
        const uint32_t entry[3]={pattern,pattern,pattern};const uint32_t head=osh_fs0();
        BYTE before[OSH_GLOBALS],after[OSH_GLOBALS],actual[OSH_GLOBALS],expected_fpu[108],actual_fpu[108];static osh_record expected[OSH_MAX];
        memcpy(osh_before,osh_arena,OSH_SIZE);osh_globals(before,0);
        osh_reset();osh_invoke(mapped+d->rva,a,entry,regs,pattern,want);es2_hook_fpu_save(expected_fpu);
        const unsigned calls=osh_calls;memcpy(expected,osh_log,calls*sizeof(*expected));memcpy(osh_after,osh_arena,OSH_SIZE);osh_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("object_shapes poison install");
        memcpy(osh_arena,osh_before,OSH_SIZE);osh_globals(before,1);osh_reset();osh_invoke(mapped+d->rva,a,entry,regs,pattern,got);
        es2_hook_fpu_save(actual_fpu);osh_globals(actual,0);
        if(osh_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||calls!=osh_calls||memcmp(expected,osh_log,calls*sizeof(*expected))||
           memcmp(osh_after,osh_arena,OSH_SIZE)||osh_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||
           memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=osh_report(fast?"installed fast":"installed preserving",n,want,got,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("object_shapes poison uninstall");osh_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, pair/trace/arena/globals/FS:0\n",d->name,total);
    return status;
}
/* ---- real callees ---- */
enum { OSH_NESTED=6 };
static unsigned osh_nested(unsigned index,unsigned nested[OSH_NESTED]){
    unsigned count=0;
#define OSH_N(name) nested[count++]=ES2_HOOK_INDEX_##name
    if(index==ES2_HOOK_INDEX_ts_obj_ctor){OSH_N(anim_instance_ctor);OSH_N(matrix_ctor);}
    else if(index==ES2_HOOK_INDEX_det_anim_shape||index==ES2_HOOK_INDEX_det_anim_record){OSH_N(ts_obj_ctor);OSH_N(anim_instance_ctor);OSH_N(matrix_ctor);}
    else if(index==ES2_HOOK_INDEX_det_anim_static){OSH_N(object_init);}
    else if(index==ES2_HOOK_INDEX_anim_instance_ctor){OSH_N(shape_instance);OSH_N(matrix_ctor);}
    else if(index==ES2_HOOK_INDEX_object_track){OSH_N(thread_ctor);OSH_N(track_clip);OSH_N(clip_pose);OSH_N(track_rate);OSH_N(thread_add);OSH_N(track_sort);}
    else if(index==ES2_HOOK_INDEX_thread_add){OSH_N(track_sort);OSH_N(track_count);}
    else if(index==ES2_HOOK_INDEX_thread_ctor){OSH_N(track_clip);OSH_N(clip_pose);}
#undef OSH_N
    return count;
}
static int osh_real_pass(unsigned index){
    unsigned nested[OSH_NESTED];const unsigned count=osh_nested(index,nested);
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[OSH_NESTED+1]={0};
    BYTE saved[OSH_IDS][5];int active[OSH_IDS];
    if(osh_patch(saved,active,osh_real_which(index),1))return 1;
    static osh_record expected[OSH_MAX];
    const unsigned modes=2u<<count;
    for(unsigned mode=0;mode<modes&&!status;++mode)for(unsigned n=0;n<4096u/modes&&!status;++n){
        uint32_t a[6],r[3],regs[3],fill,want[3]={0},got[3]={0},pattern=n&1?0xa7a7a7a7u:0x31313131u;
        osh_build(index,n*13u+mode,a,r,regs,&fill,1,1);
        const uint32_t entry[3]={pattern,pattern,pattern};const uint32_t head=osh_fs0();
        BYTE before[OSH_GLOBALS],after[OSH_GLOBALS],actual[OSH_GLOBALS];
        memcpy(osh_before,osh_arena,OSH_SIZE);osh_globals(before,0);
        es2_hook hk[OSH_NESTED+1];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(mode>>(1+j)&1)?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,function_hook_extension(nested[j])))return fail("object_shapes nested install");}
        osh_reset();osh_invoke(mapped+d->rva,a,entry,regs,pattern,want);
        const unsigned calls=osh_calls;memcpy(expected,osh_log,calls*sizeof(*expected));
        memcpy(osh_after,osh_arena,OSH_SIZE);osh_globals(after,0);
        o.mode=mode&1?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(hk+OSH_NESTED,d,&o,function_hook_extension(index)))return fail("object_shapes caller install");
        memcpy(osh_arena,osh_before,OSH_SIZE);osh_globals(before,1);osh_reset();
        osh_invoke(mapped+d->rva,a,entry,regs,pattern,got);osh_globals(actual,0);
        if(osh_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||calls!=osh_calls||memcmp(expected,osh_log,calls*sizeof(*expected))||
           memcmp(osh_after,osh_arena,OSH_SIZE)||osh_globals_differ(after,actual)){
            fprintf(stderr,"real mode=%u\n",mode);status=osh_report("real",n,want,got,calls,expected);
        }
        for(unsigned j=0;j<=OSH_NESTED;++j){if(!hk[j].descriptor)continue;entered[j]+=hk[j].calls;
            if(hk[j].mismatches)status=fail("object_shapes real compare mismatch");
            if(!es2_hook_uninstall(hk+j))return fail("object_shapes real uninstall");}
        osh_globals(before,1);++total;
    }
    if(osh_patch(saved,active,0,0))return 1;
    osh_real=0;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&!entered[OSH_NESTED]){fprintf(stderr,"FAIL: %s never entered\n",d->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, %u nested hooks original/replaced, hook original/replaced, 31/a7 stack, pair, calls, arena, globals and FS:0\n",
        d->name,total,count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
static int object_shapes_cases(unsigned index,unsigned limit){
    current_function=function_hook_descriptor(index)->name;
    const uint32_t which=osh_which(index);
    BYTE saved[OSH_IDS][5],old_globals[OSH_GLOBALS];int active[OSH_IDS];
    osh_globals(old_globals,0);
    if(osh_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_OBJECT_SHAPES_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);memset(osh_paths,0,sizeof(osh_paths));
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3],regs[3],fill;const int exact=limit||!(n&1);
        osh_build(index,n,a,r,regs,&fill,exact,0);status=osh_compare(index,n,a,r,regs,fill,exact&&!limit,!limit);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    osh_globals(old_globals,1);
    if(!limit&&!status)status=osh_installed_probes(index,64);
    if(osh_patch(saved,active,which,0))return 1;
    osh_globals(old_globals,1);
    if(!limit&&!status&&osh_nested(index,(unsigned[OSH_NESTED]){0}))status=osh_real_pass(index);
    osh_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases (stub calls 49630c %u, new %u, 478838 %u, memset %u, 402278 %u, object_init %u, "
        "shape_instance %u, new[] %u, 49667c %u, node_matrices %u, 478e48 %u, track_clip %u, track_rate %u, 478a3c %u, track_sort %u, "
        "47a288 %u, memcpy %u); full return pair and ECX, ordered calls with entry registers, slots and context word, arena/globals/FS:0\n",
        function_hook_descriptor(index)->name,cases,osh_paths[0],osh_paths[1],osh_paths[2],osh_paths[3],osh_paths[4],osh_paths[5],osh_paths[6],
        osh_paths[7],osh_paths[8],osh_paths[9],osh_paths[10],osh_paths[11],osh_paths[12],osh_paths[13],osh_paths[14],osh_paths[15],osh_paths[16]);
    return status;
}
