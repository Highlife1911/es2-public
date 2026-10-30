/* type_loads group: mech_types 42049c, mech_type_load 4201a8, lod_remap 420090,
 * node_remap 41ff88, remap_word 41ff68, mech_type_select 41ff10, gun_list_load
 * 40fee8, gun_classes 40fc50, node_list 40304c, the slot bases gun_slot_base
 * 40feb4, col_slot_base 40cbd4, dmg_slot_base 40d200, flyer_types 422fb4,
 * flyer_type_load 422ed0, flyer_type_select 422e90
 * (docs/reverse-engineering/mission_load.md, "MECH and FLYER type loads").
 *
 * Direct cases run the original body (EAX/EDX/ECX, EBX, ESI and EDI at entry
 * from the case, six argument slots, over a stack filled with a case
 * pattern) and the core against logging stubs for every game callee of the
 * hook under test (35 stub ids: the direct callees patched, the shape
 * virtuals +20h/+24h, the node class +0 and the stream read +18h through
 * arena vtables): 49630c is a record stub (the real FS:0 head at record+0,
 * context word zeroed, the record linked into FS:0) that also takes EBP as
 * the frame base; lod_remap's memset stub takes its frame base from the
 * rows. Stack values in the logged slots and registers are logged relative
 * to that frame (the original's against the core's scratch frame), the rows
 * of lod_remap and the class words of mech_type_load by content (hashes).
 * node_remap's recursion runs the original in the original run and through
 * the bridge in the core run. Compared: the full return pair, ECX, the
 * ordered calls with argument slots (and the bridge's slot count), entry
 * EAX/EDX/ECX and the record's context word, the arena, the image globals
 * (the type tables and counts, the slot words and counters) and FS:0. The
 * stubs change what the originals reread (counts, part bytes, the shape
 * table, a stream's table, flags and frame words), and objects overlap, so
 * the order of reads and writes is observable. In even cases the inputs
 * are the real callers' and the adapter is compared too (callee counter
 * deltas included).
 *
 * Then installed 31/a7 poison probes and real-callee passes with the nested
 * hooks in rotating original/replaced modes and the hook under test original
 * or replaced, over a deterministic world: shape lists with node trees and
 * lookup maps, the OVL and "fm" records, gun lists, the real 49630c, 49667c
 * with its element constructors, 40fea0, memset, multiply_q10 and the slot
 * bases, a bump allocator for new[], stubs for the file layer, the H3
 * displays and the debris set; dmg_load and collision_load (with their
 * readers) over a stream whose reads give zero counts. The three slot bases
 * and remap_word also run the compare probes of compare_probes.h.
 * Identifiers use the prefix tyl_/TYL_. */
typedef struct tyl_record { uint32_t id,slots,args[6],regs[3],context,eax,edx,extra,raw[10]; } tyl_record;
enum {
    TYL_SIZE=0x10000,TYL_MAX=768,TYL_IDS=35,
    TYL_NAMES=0x0000,TYL_STR=0x0200,TYL_VT=0x0300,TYL_VT2=0x0340,TYL_NVT=0x0380,TYL_STVT=0x03c0,TYL_COUNTER=0x03f0,
    TYL_L=0x0400,TYL_S=0x0480,TYL_E=0x0500,TYL_C=0x0580,TYL_HOLD=0x0600,TYL_T=0x0700,TYL_OUT=0x0800,TYL_W=0x0900,
    TYL_SHAPEARR=0x0a00,TYL_SHAPES=0x0c00,TYL_NODES=0x2000,TYL_TYPES=0x4400,TYL_MOUNT=0x5400,TYL_DMGSLOTS=0x5800,
    TYL_COLSLOTS=0x5c00,TYL_BLK=0x6000,TYL_FIXED=0x7c00,TYL_BUMP=0x8000,
    TYL_SHAPE=0x200,TYL_NODE=0x80,TYL_NODE_COUNT=72,TYL_SHAPE_COUNT=10
};
#define TYL_B0 ((uint32_t)(uintptr_t)tyl_arena)
#define TYL_A(o) (TYL_B0+(uint32_t)(o))
#define TYL_BIT(i) ((uint64_t)1<<(i))
#define TYL_IS(name) (index==ES2_HOOK_INDEX_##name)
static BYTE tyl_arena[TYL_SIZE],tyl_before[TYL_SIZE],tyl_after[TYL_SIZE];
static tyl_record tyl_log[TYL_MAX];
static unsigned tyl_calls,tyl_real,tyl_effects,tyl_exact,tyl_index,tyl_paths[TYL_IDS],tyl_shapes_built,tyl_nodes_built,tyl_lists_built;
static uint32_t tyl_seed,tyl_stream,tyl_bump,tyl_head,tyl_frame,tyl_count_answer,tyl_obj;
static BYTE tyl_ovl[0xd8],tyl_fm[0x36];
__attribute__((used)) static uint32_t tyl_ecx,tyl_stub_ebp;
/* 0 49630c, 1 492d00, 2 new[] 4774f0, 3 49667c, 4 4378d8, 5 mech_type_load,
 * 6 492bec, 7 492f54, 8 474cd8, 9 lod_remap, 10 multiply_q10, 11
 * gun_list_load, 12 gun_classes, 13 node_list, 14 4379cc, 15 40874c, 16
 * memset 49681a, 17 node_remap, 18 remap_word, 19 gun_slot_base, 20
 * col_slot_base, 21 dmg_slot_base, 22 472f14, 23 40fea0, 24 479c78, 25
 * stream_read 4735b8, 26 stream_destroy 472f64, 27 flyer_type_load, 28
 * 474c38, 29 dmg_load, 30 collision_load; 31..34 the virtual slots +20h,
 * +24h, +0 and the stream read +18h (arena vtables, never patched). The
 * slots each original call site pushes. */
static const uint32_t tyl_addresses[TYL_IDS]={0x49630c,0x492d00,0x4774f0,0x49667c,0x4378d8,0x4201a8,0x492bec,0x492f54,
    0x474cd8,0x420090,0x47e0b0,0x40fee8,0x40fc50,0x40304c,0x4379cc,0x40874c,0x49681a,0x41ff88,0x41ff68,0x40feb4,0x40cbd4,
    0x40d200,0x472f14,0x40fea0,0x479c78,0x4735b8,0x472f64,0x422ed0,0x474c38,0x40d160,0x40cd88,0,0,0,0};
static const unsigned tyl_counts[TYL_IDS]={0,3,1,5,1,1,2,4,4,2,2,2,2,4,2,3,3,3,2,1,1,1,2,1,6,3,2,1,1,1,1,2,2,1,3};
static const unsigned tyl_slot_keys[TYL_IDS]={[31]=0x20,[32]=0x24,[33]=0x100,[34]=0x18};
static uint32_t tyl_next(void){
    uint32_t z=tyl_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *tyl_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static uint32_t tyl_fs0(void){uint32_t v;__asm__ volatile("movl %%fs:0,%0":"=r"(v));return v;}
static int tyl_on_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
/* A random dword that never looks like a stack address (the logs take
 * stack values relative to the frame, which differs between the runs). */
static uint32_t tyl_value(void){const uint32_t v=tyl_next();return tyl_on_stack(v)?v^0x80000000u:v;}
static int tyl_in(uint32_t a,uint32_t size){return a-TYL_B0<TYL_SIZE&&size<=TYL_SIZE&&a-TYL_B0+size<=TYL_SIZE;}
/* A stack value relative to the current frame base (0 when there is none):
 * the original's EBP against the core's scratch frame. */
static uint32_t tyl_rel(uint32_t v){
    if(!tyl_on_stack(v))return v;
    return tyl_frame&&v-(tyl_frame-0x400u)<0x500u?0x5fac0000u+(v-tyl_frame):0;
}
static void tyl_reset(void){tyl_calls=0;tyl_stream=tyl_seed;tyl_bump=TYL_A(TYL_BUMP);tyl_head=tyl_fs0();tyl_frame=0;
    tyl_shapes_built=0;tyl_nodes_built=0;tyl_lists_built=0;}
static void tyl_random(unsigned offset,unsigned size){for(unsigned i=0;i+4<=size;i+=4)put32(tyl_arena+offset+i,tyl_next());}
static uint32_t tyl_alloc(uint32_t size){
    if(size>0x1000u)return size>=0x80000000u?0:TYL_A(TYL_FIXED);
    const uint32_t p=tyl_bump;tyl_bump+=(size+11u)&~3u;
    if(tyl_bump>TYL_A(TYL_SIZE)){fail("type_loads bump allocator overflow");ExitProcess(1);}
    memset(tyl_host(p),0,size);return p;
}
static uint32_t tyl_hash(uint32_t a,uint32_t size){
    uint32_t h=0x811c9dc5u;for(uint32_t i=0;i<size;++i)h=(h^tyl_host(a)[i])*0x01000193u;return h;
}
/* The node class ids the walk distinguishes (and some it does not). */
static uint32_t tyl_class(uint32_t pick){
    static const uint32_t classes[]={0x140014,0x140015,0x14000b,0x140007,0x140008,0x14000c,0x1e0003,0x1e0005,0x140005,0x140006,
        0x140009,0x14000a,0x14000d,0x140016,0x1e0004,0x1e0006,0x140004,0,0xffffffffu,0x140013};
    const unsigned k=pick%22u;
    return k<20?classes[k]:pick>>8;
}
/* ---- the deterministic world of the real-callee passes ---- */
/* A node tree under the node at `node` (depth 0 = a shape): containers get
 * children (nodes built in increasing order, so the graph is acyclic). */
static void tyl_tree(uint32_t node,unsigned depth){
    BYTE *p=tyl_host(node);
    const unsigned children=depth>=2||tyl_nodes_built+3>=TYL_NODE_COUNT?0:1+tyl_next()%3u;
    put16(p+0x10,(uint16_t)children);put32(p+0x12,node+0x50);
    for(unsigned i=0;i<children;++i){
        const uint32_t child=TYL_A(TYL_NODES+TYL_NODE*tyl_nodes_built++);BYTE *c=tyl_host(child);
        memset(c,0,TYL_NODE);put32(c,TYL_A(TYL_NVT));put16(c+4,(uint16_t)(tyl_next()%20u));
        static const uint32_t kinds[]={0x140014,0x140015,0x14000b,0x140007,0x14000c,0x140013,0x140014,0x140015};
        const uint32_t k=kinds[tyl_next()%8u];put32(c+0x40,k);
        const unsigned words=k==0x140015?tyl_next()%5u:0;put16(c+0x16,(uint16_t)words);put32(c+0x1c,child+0x60);
        for(unsigned w=0;w<words;++w)put16(c+0x60+2*w,(uint16_t)(tyl_next()%25u));
        put32(p+0x50+4*i,child);
        if(k!=0x140014&&k!=0x140013)tyl_tree(child,depth+1);else put16(c+0x10,0);
    }
}
/* The shape list of one type (474cd8 in the real passes): count shapes
 * (ANShape) with node trees, lookup maps for the virtuals +20h (part ->
 * node; shape 0 never empty) and +24h (class -> node or none), and the
 * holder of the node count word (+2Ah -> +10h). */
static uint32_t tyl_world_shapes(uint32_t list,unsigned count){
    const uint32_t array=TYL_A(TYL_SHAPEARR+0x40*(tyl_lists_built++%6u));
    for(unsigned k=0;k<count;++k){
        const uint32_t shape=TYL_A(TYL_SHAPES+TYL_SHAPE*(tyl_shapes_built++%TYL_SHAPE_COUNT));BYTE *s=tyl_host(shape);
        memset(s,0,TYL_SHAPE);put32(s,TYL_A(TYL_VT));put16(s+4,(uint16_t)(tyl_next()%20u));put32(s+0x40,0x1e0003);
        put32(s+0x2a,shape+0x180);put16(s+0x190,(uint16_t)(18u+tyl_next()%8u));
        /* the maps name the shape's own nodes (FEh: the shape itself, when
         * the node pool is used up), so a later walk's remapped words never
         * reach another shape's lookups (as in the game) */
        const unsigned first=tyl_nodes_built;
        tyl_tree(shape,0);
        const unsigned own=tyl_nodes_built-first;
        for(unsigned i=0;i<0x80;++i)s[0x80+i]=(BYTE)(k==0||tyl_next()%4u?own?first+tyl_next()%own:0xfeu:0xffu);
        for(unsigned i=0;i<0x40;++i)s[0x100+i]=(BYTE)(own&&tyl_next()%3u?first+tyl_next()%own:0xffu);
        put32(tyl_host(array+4*k),shape);
    }
    put32(tyl_host(list),array);
    return count;
}
static uint32_t tyl_map_node(uint32_t shape,unsigned offset){
    const BYTE v=tyl_host(shape)[offset];
    return v==0xff?0:v==0xfe?shape:TYL_A(TYL_NODES+TYL_NODE*(v%TYL_NODE_COUNT));
}
/* The read method: zero counts and data in the real passes (dmg_load,
 * collision_load), bounded random data otherwise; gun_list_load's reads
 * (the stream_read stub) give the count word and the 26-byte entries. */
static void tyl_read(const uint32_t *a,uint32_t pick,int gun){
    const uint32_t size=a[1],buf=a[2];
    if(size>0x200u||!tyl_in(buf,size))return;
    if(tyl_real&&!gun){memset(tyl_host(buf),0,size);return;}
    for(uint32_t i=0;i<size;++i)tyl_host(buf)[i]=(BYTE)tyl_next();
    if(size==2){
        const unsigned k=pick%8u;
        put16(tyl_host(buf),!tyl_real&&!tyl_exact&&k==0?(uint16_t)(0xffffu-pick%3u):(uint16_t)((pick>>8)%6u));
    } else if(gun) for(uint32_t i=0;i+26u<=size;i+=26u){
        put16(tyl_host(buf+i),(uint16_t)(tyl_next()%30u));tyl_host(buf+i)[6]=(BYTE)((int)(tyl_next()%9u)-2);
    }
}
/* What the stubs answer and change. */
static uint32_t tyl_answer(unsigned id,const uint32_t *a,uint32_t eax,uint32_t pick){
    const int effect=tyl_effects&&pick%4==0;
    switch(id){
    case 1: /* 492d00: the name list; the count word is reread */
        if(effect)put16(mapped+(tyl_index==ES2_HOOK_INDEX_flyer_types?0xa9dca:0xa9d06),(uint16_t)((pick>>8)%4));
        return TYL_A(TYL_NAMES);
    case 2: /* new[] */
        if(tyl_real)return tyl_alloc(a[0]);
        if(effect&&tyl_index==ES2_HOOK_INDEX_mech_types)put16(mapped+0xa9d06,(uint16_t)((pick>>8)%4));
        if(a[0]>=0x80000000u||pick%5==0)return 0;
        return TYL_A(TYL_BLK+0x40*((pick>>8)%8));
    case 3: /* 49667c: the given block (+4 for a counted array) or a new one */
        if(effect&&tyl_index==ES2_HOOK_INDEX_mech_types)put16(mapped+0xa9d06,(uint16_t)((pick>>8)%4));
        return a[0]?a[0]+4u:TYL_A(TYL_BLK+0x200+0x40*((pick>>8)%4));
    case 4: case 5: case 27: /* 4378d8, mech_type_load, flyer_type_load: index bytes ahead change */
        if(effect){const uint32_t t=(pick>>8)%0x55u;mapped[(id==27?0xa9dcc:0xa9d08)+(id==27?t%0x22u:t)]=(BYTE)(pick&0x10?0xff:(pick>>16)%3);}
        return eax;
    case 6: /* 492bec: a path */
        return TYL_A(TYL_STR+8*(pick%16));
    case 7: { /* 492f54: the OVL, "dat" or "fm" record */
        const uint32_t size=(uint32_t)(uint16_t)a[1]*(uint32_t)(uint16_t)a[2];
        if(tyl_in(a[0],size)&&size<=0xd8u)memcpy(tyl_host(a[0]),size==0x36u?tyl_fm:tyl_ovl,size);
        return eax;
    }
    case 8: /* 474cd8: the shape list */
        if(tyl_real)return (eax&0xffff0000u)|tyl_world_shapes(a[0],2u+pick%3u);
        put32(tyl_host(a[0]),TYL_A(TYL_SHAPEARR));
        return (eax&0xffff0000u)|tyl_count_answer;
    case 9: /* lod_remap: R+4 and the flag R+4Eh are reread */
        if(effect&&tyl_in(tyl_obj+2,0xd8)){if(pick&0x10)put16(tyl_host(tyl_obj+6),(uint16_t)tyl_next());else put16(tyl_host(tyl_obj+0x50),(uint16_t)(pick>>20&1));}
        return eax;
    case 12: { /* gun_classes: n words into the list (sometimes past its 21 words) */
        const unsigned n=tyl_effects&&pick%7==0?22u+(pick>>8)%8u:(pick>>8)%7u;
        for(unsigned i=0;i<n;++i)put16(tyl_host(a[1]+2*i),(uint16_t)(tyl_next()%8u==0?0xffffu:tyl_next()%40u));
        return (eax&0xffff0000u)|n;
    }
    case 13: /* node_list: the shape count and the table word are reread */
        if(effect){if(pick&0x10)put16(tyl_host(tyl_obj+0xeau),(uint16_t)((pick>>8)%9));else put16(tyl_host(tyl_obj+0x96),(uint16_t)tyl_next());}
        return eax;
    case 14: /* 4379cc: the flag word rec+50h is reread */
        if(effect&&tyl_in(tyl_obj,0x21e))put16(tyl_host(tyl_obj+0x50),(uint16_t)(pick>>20&1));
        return eax;
    case 16: /* memset: the rows of lod_remap (the frame base) */
        if(!tyl_frame)tyl_frame=a[0]+0x1a0u;
        if(a[2]<=0x200u&&(tyl_on_stack(a[0])||tyl_in(a[0],a[2])))memset(tyl_host(a[0]),(int)(BYTE)a[1],a[2]);
        return a[0];
    case 18: /* remap_word: an inner node's counts may change (reread); its
              * spare child entries name leaves */
        if(effect){const uint32_t node=TYL_A(TYL_NODES+TYL_NODE*((pick>>8)%(TYL_NODE_COUNT-8u)));
            put16(tyl_host(node+(pick&0x10?0x16:0x10)),(uint16_t)((pick>>12)%4));}
        return eax;
    case 22: /* 472f14: the stream's table and state */
        put32(tyl_host(a[0]),TYL_A(TYL_STVT));
        put32(tyl_host(a[0]+4),tyl_exact||tyl_real||pick%4?0:(pick>>8)|1u);
        return a[0];
    case 23: /* 40fea0: the mount entry */
        return TYL_A(TYL_MOUNT+6*(pick%16));
    case 25: /* stream_read (direct call) */
        tyl_read(a,pick,1);return tyl_real||pick%6?eax|1u:eax&0xffffff00u;
    case 28: /* 474c38: a shape */
        return TYL_A(TYL_SHAPES+TYL_SHAPE*(pick%4));
    case 31: { /* shape +20h (shape, part) */
        if(tyl_real)return tyl_map_node(a[0],0x80+(a[1]&0x7fu));
        if(effect){if(pick&0x10)put16(tyl_host(TYL_A(TYL_S)+0xc),(uint16_t)(2u+(pick>>8)%7));else tyl_arena[TYL_W+(pick>>8)%50]=(BYTE)(pick>>16);}
        const int first=a[0]==get32(tyl_host(get32(tyl_host(TYL_A(TYL_S)))));
        return !first&&pick%3==0?0:TYL_A(TYL_NODES+TYL_NODE*((pick>>8)%TYL_NODE_COUNT));
    }
    case 32: /* shape +24h (shape, class) */
        if(tyl_real)return tyl_map_node(a[0],0x100+(a[1]&0x3fu));
        if(effect){if(pick&0x10)put32(tyl_host(a[0]),TYL_A(pick&0x20?TYL_VT:TYL_VT2));else put16(tyl_host(TYL_A(TYL_C)+2*((pick>>8)%8)),(uint16_t)tyl_next());}
        return pick%3==0?0:TYL_A(TYL_NODES+TYL_NODE*((pick>>8)%TYL_NODE_COUNT));
    case 33: /* node +0: the class stored in the node */
        return tyl_in(a[0]+0x40,4)?get32(tyl_host(a[0]+0x40)):tyl_class(pick);
    case 34: /* the stream's read slot +18h */
        tyl_read(a,pick,0);return eax|1u;
    }
    return eax;
}
__attribute__((used,noinline)) static uint64_t __cdecl tyl_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(tyl_calls==TYL_MAX){fail("type_loads call overflow");ExitProcess(1);}
    tyl_record *v=tyl_log+tyl_calls++;memset(v,0,sizeof(*v));v->id=id;
    if(id==0&&!tyl_frame)tyl_frame=tyl_stub_ebp;
    v->slots=weapons_bridge_slots(a,tyl_counts[id]);
    for(unsigned k=0;k<tyl_counts[id];++k){v->raw[k]=a[k];v->args[k]=tyl_real&&tyl_on_stack(a[k])?0:tyl_rel(a[k]);}
    for(unsigned j=0;j<3;++j){v->raw[6+j]=regs[j];v->regs[j]=tyl_real&&tyl_on_stack(regs[j])?0:tyl_rel(regs[j]);}
    if(id&&tyl_fs0()!=tyl_head)v->context=get16(tyl_host(tyl_fs0()+16u));
    if(!tyl_real&&id==17&&tyl_frame)v->extra=tyl_hash(tyl_frame-0x1a0u,0x190u);
    if(!tyl_real&&id==13&&tyl_on_stack(a[3])&&(uint16_t)a[2]<=29u)v->extra=tyl_hash(a[3],2u*(uint16_t)a[2]);
    uint32_t eax=tyl_value(),edx=tyl_value(),pick=tyl_next();tyl_ecx=tyl_value();
    if(id==0)eax=tyl_fs0();
    else eax=tyl_answer(id,a,eax,pick);
    v->raw[9]=eax;v->eax=tyl_real&&tyl_on_stack(eax)?0:tyl_rel(eax);v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define TYL_STUB(n) __attribute__((naked)) static void tyl_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _tyl_stub_impl\n\taddl $24,%esp\n\tmovl _tyl_ecx,%ecx\n\tret\n\t");}
TYL_STUB(1) TYL_STUB(2) TYL_STUB(3) TYL_STUB(4) TYL_STUB(5) TYL_STUB(6) TYL_STUB(7) TYL_STUB(8) TYL_STUB(9) TYL_STUB(10)
TYL_STUB(11) TYL_STUB(12) TYL_STUB(13) TYL_STUB(14) TYL_STUB(15) TYL_STUB(16) TYL_STUB(17) TYL_STUB(18) TYL_STUB(19)
TYL_STUB(20) TYL_STUB(21) TYL_STUB(22) TYL_STUB(23) TYL_STUB(24) TYL_STUB(25) TYL_STUB(26) TYL_STUB(27) TYL_STUB(28)
TYL_STUB(29) TYL_STUB(30) TYL_STUB(31) TYL_STUB(32) TYL_STUB(33) TYL_STUB(34)
#undef TYL_STUB
/* 49630c: takes EBP as the frame base, logs like the other stubs (EDX and
 * ECX come back unchanged, as the original's do), then links a record at
 * EBP + [descriptor+4] into FS:0 with the saved head at record+0 and a zero
 * context word. */
__attribute__((naked)) static void tyl_stub_0(void){__asm__ volatile(
    "movl %ebp,_tyl_stub_ebp\n\t"
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
    "pushl $0\n\tcalll _tyl_stub_impl\n\taddl $12,%esp\n\t"
    "popl %ecx\n\tmovl 4(%ecx),%ecx\n\taddl %ebp,%ecx\n\tmovl %eax,(%ecx)\n\tmovw $0,16(%ecx)\n\tmovl %ecx,%fs:0\n\t"
    "popl %edx\n\tpopl %ecx\n\tret\n\t");}
static void *tyl_stubs[TYL_IDS]={tyl_stub_0,tyl_stub_1,tyl_stub_2,tyl_stub_3,tyl_stub_4,tyl_stub_5,tyl_stub_6,tyl_stub_7,
    tyl_stub_8,tyl_stub_9,tyl_stub_10,tyl_stub_11,tyl_stub_12,tyl_stub_13,tyl_stub_14,tyl_stub_15,tyl_stub_16,tyl_stub_17,
    tyl_stub_18,tyl_stub_19,tyl_stub_20,tyl_stub_21,tyl_stub_22,tyl_stub_23,tyl_stub_24,tyl_stub_25,tyl_stub_26,tyl_stub_27,
    tyl_stub_28,tyl_stub_29,tyl_stub_30,tyl_stub_31,tyl_stub_32,tyl_stub_33,tyl_stub_34};
static int tyl_patch(BYTE saved[TYL_IDS][5],int active[TYL_IDS],uint64_t which,int on){
    for(unsigned i=0;i<TYL_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!tyl_addresses[i])continue;
            if(actor_patch(tyl_addresses[i]-0x400000u,tyl_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(tyl_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
/* Direct callees of each hook (bit i = tyl_addresses[i]; the virtual
 * slots are reached through the arena vtables). node_remap's recursion is
 * never patched. */
static uint64_t tyl_which(unsigned index){
    if(TYL_IS(mech_types))return TYL_BIT(0)|TYL_BIT(1)|TYL_BIT(2)|TYL_BIT(3)|TYL_BIT(4)|TYL_BIT(5);
    if(TYL_IS(mech_type_load))return TYL_BIT(0)|TYL_BIT(2)|TYL_BIT(3)|TYL_BIT(6)|TYL_BIT(7)|TYL_BIT(8)|TYL_BIT(9)|TYL_BIT(10)|
        TYL_BIT(11)|TYL_BIT(12)|TYL_BIT(13)|TYL_BIT(14)|TYL_BIT(15);
    if(TYL_IS(lod_remap))return TYL_BIT(16)|TYL_BIT(17);
    if(TYL_IS(node_remap))return TYL_BIT(18);
    if(TYL_IS(mech_type_select))return TYL_BIT(19)|TYL_BIT(20)|TYL_BIT(21);
    if(TYL_IS(gun_list_load))return TYL_BIT(0)|TYL_BIT(3)|TYL_BIT(22)|TYL_BIT(23)|TYL_BIT(24)|TYL_BIT(25)|TYL_BIT(26);
    if(TYL_IS(node_list))return TYL_BIT(2);
    if(TYL_IS(flyer_types))return TYL_BIT(1)|TYL_BIT(3)|TYL_BIT(27);
    if(TYL_IS(flyer_type_load))return TYL_BIT(6)|TYL_BIT(7)|TYL_BIT(28)|TYL_BIT(29)|TYL_BIT(30);
    if(TYL_IS(flyer_type_select))return TYL_BIT(20)|TYL_BIT(21);
    return 0;
}
/* Stubbed in the real-callee passes (the rest runs for real). */
static uint64_t tyl_real_which(unsigned index){
    if(TYL_IS(mech_types)||TYL_IS(mech_type_load))return TYL_BIT(1)|TYL_BIT(2)|TYL_BIT(4)|TYL_BIT(6)|TYL_BIT(7)|TYL_BIT(8)|
        TYL_BIT(14)|TYL_BIT(15)|TYL_BIT(22)|TYL_BIT(24)|TYL_BIT(25)|TYL_BIT(26);
    if(TYL_IS(gun_list_load))return TYL_BIT(2)|TYL_BIT(22)|TYL_BIT(24)|TYL_BIT(25)|TYL_BIT(26);
    if(TYL_IS(node_list))return TYL_BIT(2);
    if(TYL_IS(flyer_types)||TYL_IS(flyer_type_load))return TYL_BIT(1)|TYL_BIT(2)|TYL_BIT(6)|TYL_BIT(7)|TYL_BIT(22)|TYL_BIT(24)|
        TYL_BIT(26)|TYL_BIT(28);
    return 0;
}
/* The image globals: slot words and counters, the slot arrays, the mount
 * table, the MECH and FLYER type tables and counts, the extension
 * pointers, the construction-count pointer. */
static const struct { uint32_t rva,size; } tyl_ranges[]={{0x987d8,8},{0x98850,0x18},{0xa98e0,0x10},{0xa9820,0x10},
    {0xa9cf0,0x110},{0xa09e8,0x28},{0x2cbee4,4}};
enum { TYL_GLOBALS=8+0x18+0x10+0x10+0x110+0x28+4 };
static void tyl_globals(BYTE *out,int restore){
    for(unsigned i=0;i<sizeof(tyl_ranges)/sizeof(tyl_ranges[0]);++i){
        if(restore)memcpy(mapped+tyl_ranges[i].rva,out,tyl_ranges[i].size);else memcpy(out,mapped+tyl_ranges[i].rva,tyl_ranges[i].size);
        out+=tyl_ranges[i].size;
    }
}
static int tyl_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<TYL_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
/* The OVL record (0D8h bytes at rec+2): a valid 47e068 division (the
 * divisor R+0C2h not 0, the quotient of (400 x R+0C4h) << 16 inside 32
 * bits), part bytes at R+2Eh (rec+30h) with a negative end or (not in the
 * real passes) none within 50; the "fm" record: [+2Ah]-[+2Eh] not 0 and the
 * quotient of ([+22h]-[+26h]) << 16 inside 32 bits. */
static void tyl_records(int exact){
    for(unsigned i=0;i<sizeof(tyl_ovl);++i)tyl_ovl[i]=(BYTE)tyl_next();
    int32_t b=(int32_t)(1u+tyl_next()%32767u);if(tyl_next()&1)b=-b;
    const int32_t bound=81*(b<0?-b:b)>32767?32767:81*(b<0?-b:b);
    const int32_t c=(int32_t)(tyl_next()%(2u*(uint32_t)bound+1u))-bound;
    put16(tyl_ovl+0xc2,(uint16_t)b);put16(tyl_ovl+0xc4,(uint16_t)c);
    /* the part bytes R+2Eh .. R+5Fh cover the flag word R+4Eh (rec+50h), so
     * a list that ends before byte 32 leaves the flag alone */
    const unsigned parts=tyl_real?tyl_next()%12u:exact?tyl_next()%20u:tyl_next()%52u;
    for(unsigned i=0;i<50;++i)tyl_ovl[i+0x2eu]=(BYTE)(i<parts?tyl_next()%(tyl_real?20u:64u):0x80u|tyl_next());
    put16(tyl_ovl+0x4e,(uint16_t)(tyl_next()%2?0:1u+tyl_next()%3));
    tyl_ovl[0xcc]=(BYTE)(tyl_next()%3?'d':0);
    for(unsigned i=0;i<sizeof(tyl_fm);++i)tyl_fm[i]=(BYTE)tyl_next();
    int32_t d=(int32_t)(1u+tyl_next()%1000u);if(tyl_next()&1)d=-d;
    const int32_t n=(int32_t)(tyl_next()%60001u)-30000;
    const uint32_t lo26=tyl_next(),lo2e=tyl_next();
    put32(tyl_fm+0x26,lo26);put32(tyl_fm+0x22,lo26+(uint32_t)n);put32(tyl_fm+0x2e,lo2e);put32(tyl_fm+0x2a,lo2e+(uint32_t)d);
}
/* The case data. exact: the real callers' inputs; real: the world of the
 * real-callee passes. */
static void tyl_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],uint32_t regs[3],uint32_t *fill,int exact,int real){
    tyl_seed=random32();tyl_stream=tyl_seed^n;tyl_real=real;tyl_exact=exact||real;tyl_effects=!real&&!exact;tyl_index=index;
    for(unsigned i=0;i<3;++i){r[i]=tyl_value();regs[i]=tyl_value();}
    tyl_random(0,TYL_SIZE);memset(a,0,24);for(unsigned i=0;i<6;++i)a[i]=tyl_value();
    *fill=!exact&&!real&&tyl_next()%2?tyl_next():0;
    tyl_shapes_built=0;tyl_nodes_built=0;
    /* vtables: shapes (+0 class, +20h, +24h), nodes (+0), the stream (+18h) */
    put32(tyl_arena+TYL_VT,(uint32_t)(uintptr_t)tyl_stub_33);put32(tyl_arena+TYL_VT+0x20,(uint32_t)(uintptr_t)tyl_stub_31);
    put32(tyl_arena+TYL_VT+0x24,(uint32_t)(uintptr_t)tyl_stub_32);
    put32(tyl_arena+TYL_VT2,(uint32_t)(uintptr_t)tyl_stub_33);put32(tyl_arena+TYL_VT2+0x20,(uint32_t)(uintptr_t)tyl_stub_31);
    put32(tyl_arena+TYL_VT2+0x24,(uint32_t)(uintptr_t)tyl_stub_32);
    put32(tyl_arena+TYL_NVT,(uint32_t)(uintptr_t)tyl_stub_33);put32(tyl_arena+TYL_STVT+0x18,(uint32_t)(uintptr_t)tyl_stub_34);
    put32(mapped+0x2cbee4,TYL_A(TYL_COUNTER));put32(tyl_arena+TYL_COUNTER,0);
    put32(mapped+0xa98e4,TYL_A(TYL_MOUNT));put32(mapped+0xa9824,TYL_A(TYL_COLSLOTS));put32(mapped+0xa9828,TYL_A(TYL_DMGSLOTS));
    put16(mapped+0x987de,(uint16_t)(tyl_next()%8));put16(mapped+0x98854,(uint16_t)(tyl_next()%8));
    put16(mapped+0xa98e8,(uint16_t)tyl_next());put16(mapped+0x987dc,(uint16_t)tyl_next());put16(mapped+0x98856,(uint16_t)tyl_next());
    for(unsigned i=0;i<85;++i)put32(tyl_arena+TYL_NAMES+4*i,TYL_A(TYL_STR+8*(i%32)));
    tyl_records(exact);
    const int mech=TYL_IS(mech_types)||TYL_IS(mech_type_load)||TYL_IS(mech_type_select),flyer=TYL_IS(flyer_types)||TYL_IS(flyer_type_load)||TYL_IS(flyer_type_select);
    /* the type tables: index bytes FFh or small, the counts, the arrays */
    for(unsigned i=0;i<0x58;++i)mapped[0xa9d08+i]=(BYTE)(tyl_next()%4?0xff:tyl_next()%4);
    for(unsigned i=0;i<0x24;++i)mapped[0xa9dcc+i]=(BYTE)(tyl_next()%4?0xff:tyl_next()%4);
    put16(mapped+0xa9d06,(uint16_t)(exact||real?1u+tyl_next()%3:tyl_next()%8==0?0x8000u|tyl_next():tyl_next()%5));
    put16(mapped+0xa9dca,(uint16_t)(exact||real?tyl_next()%3:tyl_next()%8==0?0x8000u|tyl_next():tyl_next()%5));
    put32(mapped+0xa9d60,TYL_A(TYL_NAMES));put32(mapped+0xa9d64,TYL_A(TYL_TYPES));put16(mapped+0xa9d70,(uint16_t)(tyl_next()%16));
    put32(mapped+0xa9df0,TYL_A(TYL_NAMES));put32(mapped+0xa9df4,TYL_A(TYL_TYPES));
    (void)mech;(void)flyer;
    /* shapes and nodes for the direct cases */
    if(!real){
        for(unsigned k=0;k<TYL_SHAPE_COUNT;++k){
            const uint32_t shape=TYL_A(TYL_SHAPES+TYL_SHAPE*k);put32(tyl_host(shape),TYL_A(TYL_VT));
            put32(tyl_host(shape+0x2a),TYL_A(TYL_HOLD));put32(tyl_arena+TYL_SHAPEARR+4*k,shape);
        }
        for(unsigned j=0;j<TYL_NODE_COUNT;++j){
            BYTE *p=tyl_arena+TYL_NODES+TYL_NODE*j;put32(p,TYL_A(TYL_NVT));put32(p+0x40,tyl_class(tyl_next()));
            put16(p+4,(uint16_t)(exact?tyl_next()%50u:tyl_next()%8==0?0xff00u|tyl_next():tyl_next()%50u));
        }
    }
    if(TYL_IS(gun_slot_base)||TYL_IS(col_slot_base)||TYL_IS(dmg_slot_base)){
        /* n any dword */
    } else if(TYL_IS(remap_word)){
        /* exact: the word apart from the table (a node word and a frame row);
         * otherwise anywhere, the table over the word too */
        const unsigned k=tyl_next()%4;
        a[0]=TYL_A(exact?TYL_T+0x100u+tyl_next()%0x100u:TYL_T+tyl_next()%0x200u);
        a[1]=TYL_A(!exact&&k==0?TYL_T+tyl_next()%0x200u:TYL_T);
        const uint16_t w=exact?(uint16_t)(tyl_next()%60u):tyl_next()%4==0?(uint16_t)(0x8000u|tyl_next()):(uint16_t)(tyl_next()%0x1000u);
        put16(tyl_host(a[0]),w);tyl_obj=a[0];
    } else if(TYL_IS(gun_classes)){
        const uint32_t L=TYL_A(TYL_L+tyl_next()%8u),E=TYL_A(TYL_E+(exact?0:tyl_next()%0x20u));
        const unsigned count=exact?tyl_next()%7u:tyl_next()%8==0?0x8000u|tyl_next():tyl_next()%9u;
        put16(tyl_host(L),(uint16_t)count);put32(tyl_host(L+2),E);
        for(unsigned i=0;i<9;++i){put16(tyl_host(E+26*i),(uint16_t)(tyl_next()%8==0?0xffffu:tyl_next()%16u));
            tyl_host(E+26*i)[6]=(BYTE)(tyl_next()%3?(int)(tyl_next()%9u)-2:(int)tyl_next());}
        const unsigned w=tyl_next()%6;
        a[0]=L;a[1]=exact||w>1?TYL_A(TYL_OUT):w==0?L+(tyl_next()%3u):E+2u*(tyl_next()%20u);
    } else if(TYL_IS(node_list)){
        const uint32_t L=TYL_A(TYL_L),S=TYL_A(TYL_SHAPES);
        const unsigned count=exact?tyl_next()%8u:tyl_next()%8==0?0x8000u|tyl_next():tyl_next()%12u;
        for(unsigned i=0;i<16;++i)put16(tyl_arena+TYL_C+2*i,(uint16_t)(tyl_next()%4==0?0xff00u|tyl_next():tyl_next()%64u));
        a[0]=L;a[1]=S;a[2]=(a[2]&0xffff0000u)|count;a[3]=TYL_A(TYL_C);tyl_obj=L;
    } else if(TYL_IS(node_remap)){
        /* a random tree over the nodes (each child a new node, so every node
         * is visited once); the last 8 nodes stay leaves, and the spare child
         * entries of the others name them (a stub may raise a count) */
        unsigned next=1;
        for(unsigned j=0;j<TYL_NODE_COUNT;++j){
            BYTE *p=tyl_arena+TYL_NODES+TYL_NODE*j;
            const unsigned children=j<TYL_NODE_COUNT-8u&&next+3u<=TYL_NODE_COUNT-8u?tyl_next()%4u:0;
            put16(p+0x10,(uint16_t)children);put32(p+0x12,TYL_A(TYL_NODES+TYL_NODE*j+0x50));
            for(unsigned i=0;i<4;++i)put32(p+0x50+4*i,TYL_A(TYL_NODES+TYL_NODE*(i<children?next+i:TYL_NODE_COUNT-1u-tyl_next()%8u)));
            next+=children;
            const unsigned words=tyl_next()%6u;put16(p+0x16,(uint16_t)words);put32(p+0x1c,TYL_A(TYL_NODES+TYL_NODE*j+0x60));
            for(unsigned w=0;w<8;++w)put16(p+0x60+2*w,(uint16_t)(exact?tyl_next()%40u:(int)(tyl_next()%70u)-5));
        }
        for(unsigned i=0;i<64;++i)tyl_arena[TYL_T+i]=(BYTE)(tyl_next()%3?tyl_next()%30u:0xff);
        tyl_nodes_built=TYL_NODE_COUNT;
        a[0]=TYL_A(TYL_NODES);a[1]=TYL_A(TYL_T);
        a[2]=exact?(a[2]&0xffff0000u)|(10u+tyl_next()%30u):(a[2]&0xffff0000u)|(tyl_next()%8==0?0x8000u|tyl_next():tyl_next()%64u);
    } else if(TYL_IS(lod_remap)){
        const uint32_t S=TYL_A(TYL_S);
        const unsigned count=exact?2u+tyl_next()%8u:tyl_next()%8==0?0x8000u|tyl_next():tyl_next()%10u;
        put32(tyl_host(S),TYL_A(TYL_SHAPEARR));put16(tyl_host(S+0xc),(uint16_t)count);put16(tyl_arena+TYL_HOLD+0x10,(uint16_t)tyl_next());
        const unsigned parts=exact?tyl_next()%20u:tyl_next()%52u;
        for(unsigned i=0;i<64;++i)tyl_arena[TYL_W+i]=(BYTE)(i<parts?tyl_next()%64u:0x80u|tyl_next());
        a[0]=S;a[1]=TYL_A(TYL_W);
    } else if(TYL_IS(mech_type_select)||TYL_IS(flyer_type_select)){
        const int32_t t=exact?(int32_t)(tyl_next()%(TYL_IS(flyer_type_select)?0x22u:0x55u)):(int32_t)(tyl_next()%0x70u)-16;
        a[0]=(a[0]&0xffff0000u)|(uint16_t)t;
    } else if(TYL_IS(gun_list_load)){
        a[0]=TYL_A(TYL_STR);a[1]=exact?(a[1]&0xffff0000u)|(tyl_next()%40u):a[1];
        /* the caller's EBX as 4201a8 has it (the adapter's reconstruction) */
        if(exact){const uint16_t idx=(uint16_t)(a[1]-get16(mapped+0xa9d70));regs[0]=get32(mapped+0xa9d64)+0x21eu*(uint32_t)(int32_t)(int16_t)idx+0xdeu;}
    } else if(TYL_IS(mech_type_load)){
        const unsigned t=tyl_next()%0x55u,idx=tyl_next()%4u;mapped[0xa9d08+t]=(BYTE)idx;
        a[0]=exact?t:(a[0]&0xffff0000u)|t;
        tyl_obj=TYL_A(TYL_TYPES)+0x21eu*idx;
        tyl_count_answer=exact?tyl_next()%7u:tyl_next()%8==0?0x8000u|tyl_next():tyl_next()%9u;
    } else if(TYL_IS(flyer_type_load)){
        const unsigned t=tyl_next()%0x22u,idx=tyl_next()%4u;mapped[0xa9dcc+t]=(BYTE)idx;
        a[0]=exact?t:(a[0]&0xffff0000u)|t;tyl_obj=TYL_A(TYL_TYPES)+0x70u*idx;
    }
    if(real){
        tyl_shapes_built=0;tyl_nodes_built=0;tyl_lists_built=0;
        /* the mount table and the slot arrays; consistent type tables: the
         * counts and the indices 0 .. count-1 of as many selected types */
        memset(tyl_arena+TYL_MOUNT,0,0x400);memset(tyl_arena+TYL_DMGSLOTS,0,0x800);
        memset(mapped+0xa9d08,0xff,0x58);memset(mapped+0xa9dcc,0xff,0x24);
        const unsigned mechs=1u+tyl_next()%3u,flyers=tyl_next()%3u;
        put16(mapped+0xa9d06,(uint16_t)mechs);put16(mapped+0xa9dca,(uint16_t)flyers);
        for(unsigned i=0;i<mechs;++i)mapped[0xa9d08+(tyl_next()%0x55u)]=(BYTE)i;
        for(unsigned i=0;i<flyers;++i)mapped[0xa9dcc+(tyl_next()%0x22u)]=(BYTE)i;
        if(TYL_IS(mech_type_load)){const unsigned t=a[0];mapped[0xa9d08+t]=(BYTE)((tyl_obj-TYL_A(TYL_TYPES))/0x21eu);}
        if(TYL_IS(flyer_type_load)){const unsigned t=a[0];mapped[0xa9dcc+t]=(BYTE)((tyl_obj-TYL_A(TYL_TYPES))/0x70u);}
        if(TYL_IS(lod_remap)){
            const unsigned shapes=2u+tyl_next()%3u;
            tyl_world_shapes(TYL_A(TYL_S),shapes);put16(tyl_arena+TYL_S+0xc,(uint16_t)shapes);
            for(unsigned i=0;i<64;++i)tyl_arena[TYL_W+i]=(BYTE)(i<12?tyl_next()%20u:0xff);
        } else if(TYL_IS(node_remap)){
            const uint32_t S=TYL_A(TYL_S);tyl_world_shapes(S,1);
            for(unsigned i=0;i<64;++i)tyl_arena[TYL_T+i]=(BYTE)(tyl_next()%3?tyl_next()%20u:0xff);
            a[0]=get32(tyl_host(get32(tyl_host(S))));a[1]=TYL_A(TYL_T);a[2]=20u+tyl_next()%6u;
        } else if(TYL_IS(node_list)){
            const uint32_t S=TYL_A(TYL_S);tyl_world_shapes(S,1);a[1]=get32(tyl_host(get32(tyl_host(S))));
        }
    }
    tyl_stream=tyl_seed;   /* the stubs' stream restarts here in every run */
}
/* Calls target with six argument slots, EAX/EDX/ECX = r and EBX/ESI/EDI =
 * regs over 3,840 bytes of stack filled with `fill`; out receives EAX, EDX
 * and ECX after the call. */
__attribute__((naked,noinline)) static void tyl_invoke(void *target,const uint32_t *a,const uint32_t *r,const uint32_t *regs,uint32_t fill,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 24(%ebp),%eax\n\tmovl $1024,%ecx\n\tcld\n\trep stosl\n\taddl $3840,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 20(%esi)\n\tpushl 16(%esi)\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 20(%ebp),%edi\n\tmovl (%edi),%ebx\n\tmovl 4(%edi),%esi\n\tmovl 8(%edi),%edi\n\t"
        "movl 16(%ebp),%eax\n\tmovl 4(%eax),%edx\n\tmovl 8(%eax),%ecx\n\tmovl (%eax),%eax\n\tcall *8(%ebp)\n\t"
        "movl 28(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* The core with the adapter's graph and scratch; FS:0 restored to the
 * record's saved head as the adapter does. */
static uint64_t tyl_core(unsigned index,const uint32_t *a,const uint32_t *r,uint32_t ebx,uint32_t *ecx){
    const es2_guest_memory *m=&math_native_memory;uint64_t v;
    BYTE scratch[ES2_LOD_REMAP_SCRATCH]={0};
    es2_weapon_graph g=weapons_graph(mapped,scratch,index);
    if(TYL_IS(mech_types)||TYL_IS(mech_type_load)||TYL_IS(gun_list_load)) {
        const uint32_t frame=TYL_IS(mech_types)?ES2_MECH_TYPES_FRAME:TYL_IS(mech_type_load)?ES2_MECH_TYPE_LOAD_FRAME:ES2_GUN_LIST_LOAD_FRAME;
        weapons_impact_context c={(uint32_t)(uintptr_t)mapped,(uint32_t)(uintptr_t)(scratch+frame)};
        g.call=weapons_impact_guest_call;g.context=&c;
        if(TYL_IS(mech_types)){v=es2_mech_types_registers(m,&g,r,ecx);__asm__ volatile("movl %0,%%fs:0"::"r"((uint32_t)v):"memory");}
        else if(TYL_IS(mech_type_load)){v=es2_mech_type_load_registers(m,&g,a[0],r,ecx);__asm__ volatile("movl %0,%%fs:0"::"r"((uint32_t)v):"memory");}
        else {v=es2_gun_list_load_registers(m,&g,a[0],a[1],ebx,r,ecx);__asm__ volatile("movl %0,%%fs:0"::"r"((uint32_t)(v>>32)):"memory");}
        return v;
    }
    const uint32_t b=(uint32_t)(uintptr_t)mapped;
    if(TYL_IS(lod_remap))return es2_lod_remap_registers(m,&g,a[0],a[1],r,ecx);
    if(TYL_IS(node_remap))return es2_node_remap_registers(m,&g,a[0],a[1],a[2],r,ecx);
    if(TYL_IS(remap_word))return es2_remap_word_registers(m,a[0],a[1],r,ecx);
    if(TYL_IS(mech_type_select))return es2_mech_type_select_registers(m,&g,a[0],r,ecx);
    if(TYL_IS(gun_classes))return es2_gun_classes_registers(m,a[0],a[1],r,ecx);
    if(TYL_IS(node_list))return es2_node_list_registers(m,&g,a[0],a[1],a[2],a[3],r,ecx);
    if(TYL_IS(gun_slot_base))return es2_gun_slot_base_registers(m,b,a[0],r,ecx);
    if(TYL_IS(col_slot_base))return es2_col_slot_base_registers(m,b,a[0],r,ecx);
    if(TYL_IS(dmg_slot_base))return es2_dmg_slot_base_registers(m,b,a[0],r,ecx);
    if(TYL_IS(flyer_types))return es2_flyer_types_registers(m,&g,r,ecx);
    if(TYL_IS(flyer_type_load))return es2_flyer_type_load_registers(m,&g,a[0],r,ecx);
    return es2_flyer_type_select_registers(m,&g,a[0],r,ecx);
}
/* Two logs agree when every word before raw is equal, except that a
 * classified word (argument, entry register, the stub's EAX) may differ
 * when its raw value is the same in both runs. tyl_rel takes stack values
 * relative to each run's own frame, so a data word in the stack range (an
 * EAX the original computes from the records, say) can fall into one
 * run's window only; equal raw words are the same value, and a frame
 * address of the original's frame does not recur raw in the core's run. */
static int tyl_same(const tyl_record *expected,const tyl_record *actual,unsigned calls){
    enum { WORDS=(unsigned)(offsetof(tyl_record,raw)/4u) };
    for(unsigned j=0;j<calls;++j){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(actual+j);
        for(unsigned k=0;k<WORDS;++k){
            if(e[k]==f[k])continue;
            const unsigned raw=k>=2&&k<11?k-2:k==12?9u:10u;
            if(raw==10||expected[j].raw[raw]!=actual[j].raw[raw])return 0;
        }
    }
    return 1;
}
static int tyl_report(const char *what,unsigned n,const uint32_t want[3],const uint32_t got[3],unsigned calls,const tyl_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x calls %u/%u\n",current_function,n,what,
        want[0],got[0],want[1],got[1],want[2],got[2],calls,tyl_calls);
    for(unsigned j=0;j<calls&&j<tyl_calls&&expected;++j)if(!tyl_same(expected+j,tyl_log+j,1)){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(tyl_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<TYL_SIZE;++j)if(tyl_after[j]!=tyl_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,tyl_after[j],tyl_arena[j]);break;}
    return 1;
}
/* Callee counter hits of the logged stub calls (virtuals by their slot). */
static unsigned tyl_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<tyl_calls;++j){
        const unsigned id=tyl_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?tyl_slot_keys[id]&&tyl_slot_keys[id]==c->key:tyl_addresses[id]==c->key)++hits;
    }
    return hits;
}
/* One direct case: the original, the core and (exact inputs) the adapter. */
static int tyl_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r,const uint32_t *regs,uint32_t fill,int exact,int check_ecx){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[TYL_GLOBALS],after[TYL_GLOBALS],actual[TYL_GLOBALS];static tyl_record expected[TYL_MAX];uint32_t want[3],got[3];
    const uint32_t head=tyl_fs0();
    memcpy(tyl_before,tyl_arena,TYL_SIZE);tyl_globals(before,0);
    tyl_reset();tyl_invoke(mapped+d->rva,a,r,regs,fill,want);const unsigned calls=tyl_calls;memcpy(expected,tyl_log,calls*sizeof(*expected));
    memcpy(tyl_after,tyl_arena,TYL_SIZE);tyl_globals(after,0);
    if(tyl_fs0()!=head)return fail("type_loads original left FS:0 changed");
    for(unsigned j=0;j<calls;++j)++tyl_paths[expected[j].id];
    memcpy(tyl_arena,tyl_before,TYL_SIZE);tyl_globals(before,1);tyl_reset();
    got[2]=~want[2];
    uint64_t pair=tyl_core(index,a,r,regs[0],&got[2]);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);tyl_globals(actual,0);
    if(tyl_fs0()!=head)return fail("type_loads core left FS:0 changed");
    if(want[0]!=got[0]||want[1]!=got[1]||(check_ecx&&want[2]!=got[2])||calls!=tyl_calls||!tyl_same(expected,tyl_log,calls)||
       memcmp(tyl_after,tyl_arena,TYL_SIZE)||tyl_globals_differ(after,actual))
        return tyl_report("core",n,want,got,calls,expected);
    if(!exact){tyl_globals(before,1);return 0;}
    /* the adapter (the framework's entry: six argument slots) */
    memcpy(tyl_arena,tyl_before,TYL_SIZE);tyl_globals(before,1);tyl_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];
    if(count>16)return fail("type_loads counter table too long");
    for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair=x->replacement_context(a,d->user,r);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);got[2]=want[2];
    tyl_globals(actual,0);
    if(tyl_fs0()!=head)return fail("type_loads adapter left FS:0 changed");
    /* node_remap's recursion runs unhooked original code: the stubs log its
     * calls, the adapter's counters do not see them (no delta check). */
    if(n<4096&&!TYL_IS(node_remap))for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=tyl_expected_hits(c+i))return fail("type_loads callee counter delta");
    if(want[0]!=got[0]||want[1]!=got[1]||calls!=tyl_calls||!tyl_same(expected,tyl_log,calls)||
       memcmp(tyl_after,tyl_arena,TYL_SIZE)||tyl_globals_differ(after,actual))
        return tyl_report("adapter",n,want,got,calls,expected);
    tyl_globals(before,1);
    return 0;
}
static int tyl_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],regs[3],fill,want[3]={0},got[3]={0},pattern=poison?0xa7a7a7a7u:0x31313131u;
        tyl_build(index,n*37u+11u,a,r,regs,&fill,1,0);
        const uint32_t entry[3]={pattern,pattern,pattern};const uint32_t head=tyl_fs0();
        BYTE before[TYL_GLOBALS],after[TYL_GLOBALS],actual[TYL_GLOBALS],expected_fpu[108],actual_fpu[108];static tyl_record expected[TYL_MAX];
        memcpy(tyl_before,tyl_arena,TYL_SIZE);tyl_globals(before,0);
        tyl_reset();tyl_invoke(mapped+d->rva,a,entry,regs,pattern,want);es2_hook_fpu_save(expected_fpu);
        const unsigned calls=tyl_calls;memcpy(expected,tyl_log,calls*sizeof(*expected));memcpy(tyl_after,tyl_arena,TYL_SIZE);tyl_globals(after,0);
        /* node_remap's recursion enters the installed hook again: one entry
         * per node visited, and each visit asks the node's class once */
        unsigned entries=1;
        if(TYL_IS(node_remap)){entries=0;for(unsigned j=0;j<calls;++j)entries+=expected[j].id==33;}
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("type_loads poison install");
        memcpy(tyl_arena,tyl_before,TYL_SIZE);tyl_globals(before,1);tyl_reset();tyl_invoke(mapped+d->rva,a,entry,regs,pattern,got);
        es2_hook_fpu_save(actual_fpu);tyl_globals(actual,0);
        if(tyl_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||calls!=tyl_calls||!tyl_same(expected,tyl_log,calls)||
           memcmp(tyl_after,tyl_arena,TYL_SIZE)||tyl_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||
           memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=entries)
            status=tyl_report(fast?"installed fast":"installed preserving",n,want,got,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("type_loads poison uninstall");tyl_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, pair/trace/arena/globals/FS:0\n",d->name,total);
    return status;
}
/* ---- real callees ---- */
enum { TYL_NESTED=8 };
static unsigned tyl_nested(unsigned index,unsigned nested[TYL_NESTED]){
    unsigned count=0;
#define TYL_N(name) nested[count++]=ES2_HOOK_INDEX_##name
    if(TYL_IS(mech_types)){TYL_N(mech_type_load);TYL_N(lod_remap);TYL_N(node_remap);TYL_N(remap_word);TYL_N(gun_list_load);
        TYL_N(gun_classes);TYL_N(node_list);TYL_N(multiply_q10);}
    else if(TYL_IS(mech_type_load)){TYL_N(lod_remap);TYL_N(node_remap);TYL_N(remap_word);TYL_N(gun_list_load);TYL_N(gun_classes);
        TYL_N(node_list);TYL_N(multiply_q10);}
    else if(TYL_IS(lod_remap)){TYL_N(node_remap);TYL_N(remap_word);}
    else if(TYL_IS(node_remap)){TYL_N(remap_word);}
    else if(TYL_IS(mech_type_select)){TYL_N(gun_slot_base);TYL_N(col_slot_base);TYL_N(dmg_slot_base);}
    else if(TYL_IS(flyer_type_select)){TYL_N(col_slot_base);TYL_N(dmg_slot_base);}
    else if(TYL_IS(flyer_types)){TYL_N(flyer_type_load);TYL_N(dmg_load);TYL_N(collision_load);TYL_N(dmg_read);TYL_N(collision_read);}
    else if(TYL_IS(flyer_type_load)){TYL_N(dmg_load);TYL_N(collision_load);TYL_N(dmg_read);TYL_N(collision_read);}
#undef TYL_N
    return count;
}
static int tyl_has_real(unsigned index){
    return !(TYL_IS(remap_word)||TYL_IS(gun_classes)||TYL_IS(gun_slot_base)||TYL_IS(col_slot_base)||TYL_IS(dmg_slot_base));
}
static int tyl_real_pass(unsigned index){
    unsigned nested[TYL_NESTED];const unsigned count=tyl_nested(index,nested);
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[TYL_NESTED+1]={0};
    BYTE saved[TYL_IDS][5];int active[TYL_IDS];
    if(tyl_patch(saved,active,tyl_real_which(index),1))return 1;
    static tyl_record expected[TYL_MAX];
    const unsigned modes=2u<<count,per=4096u/modes?4096u/modes:1u;
    for(unsigned mode=0;mode<modes&&!status;++mode)for(unsigned n=0;n<per&&!status;++n){
        uint32_t a[6],r[3],regs[3],fill,want[3]={0},got[3]={0},pattern=n&1?0xa7a7a7a7u:0x31313131u;
        tyl_build(index,n*13u+mode,a,r,regs,&fill,1,1);
        const uint32_t entry[3]={pattern,pattern,pattern};const uint32_t head=tyl_fs0();
        BYTE before[TYL_GLOBALS],after[TYL_GLOBALS],actual[TYL_GLOBALS];
        memcpy(tyl_before,tyl_arena,TYL_SIZE);tyl_globals(before,0);
        es2_hook hk[TYL_NESTED+1];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(mode>>(1+j)&1)?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,function_hook_extension(nested[j])))return fail("type_loads nested install");}
        tyl_reset();tyl_invoke(mapped+d->rva,a,entry,regs,pattern,want);
        const unsigned calls=tyl_calls;memcpy(expected,tyl_log,calls*sizeof(*expected));
        memcpy(tyl_after,tyl_arena,TYL_SIZE);tyl_globals(after,0);
        o.mode=mode&1?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(hk+TYL_NESTED,d,&o,function_hook_extension(index)))return fail("type_loads caller install");
        memcpy(tyl_arena,tyl_before,TYL_SIZE);tyl_globals(before,1);tyl_reset();
        tyl_invoke(mapped+d->rva,a,entry,regs,pattern,got);tyl_globals(actual,0);
        if(tyl_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||calls!=tyl_calls||!tyl_same(expected,tyl_log,calls)||
           memcmp(tyl_after,tyl_arena,TYL_SIZE)||tyl_globals_differ(after,actual)){
            fprintf(stderr,"real mode=%u\n",mode);status=tyl_report("real",n,want,got,calls,expected);
        }
        for(unsigned j=0;j<=TYL_NESTED;++j){if(!hk[j].descriptor)continue;entered[j]+=hk[j].calls;
            if(hk[j].mismatches)status=fail("type_loads real compare mismatch");
            if(!es2_hook_uninstall(hk+j))return fail("type_loads real uninstall");}
        tyl_globals(before,1);++total;
    }
    if(tyl_patch(saved,active,0,0))return 1;
    tyl_real=0;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&!entered[TYL_NESTED]){fprintf(stderr,"FAIL: %s never entered\n",d->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, %u nested hooks original/replaced, hook original/replaced, 31/a7 stack, pair, calls, arena, globals and FS:0\n",
        d->name,total,count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
/* Compare probes (the slot bases, remap_word): the exact inputs. */
static void tyl_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    uint32_t regs[3],fill;tyl_build(index,n,a,r,regs,&fill,1,0);
}
static void tyl_compare_state(BYTE *s,int restore){
    /* the arena's first 0x3000 bytes hold every object the four touch, plus the globals */
    if(restore){memcpy(tyl_arena,s,0x3000);tyl_globals(s+0x3000,1);}else{memcpy(s,tyl_arena,0x3000);tyl_globals(s+0x3000,0);}
}
static int type_loads_cases(unsigned index,unsigned limit){
    current_function=function_hook_descriptor(index)->name;
    BYTE old_globals[TYL_GLOBALS];tyl_globals(old_globals,0);
    if(limit&&compare_probe_installed(index)){
        const int status=compare_probe_run(index,limit,tyl_compare_build,tyl_compare_state,0x3000+TYL_GLOBALS);
        tyl_globals(old_globals,1);return status;
    }
    const uint64_t which=tyl_which(index);
    BYTE saved[TYL_IDS][5];int active[TYL_IDS];
    if(tyl_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_TYPE_LOADS_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);memset(tyl_paths,0,sizeof(tyl_paths));
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3],regs[3],fill;const int exact=limit||!(n&1);
        tyl_build(index,n,a,r,regs,&fill,exact,0);status=tyl_compare(index,n,a,r,regs,fill,exact&&!limit,!limit);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    tyl_globals(old_globals,1);
    if(!limit&&!status)status=tyl_installed_probes(index,64);
    if(tyl_patch(saved,active,which,0))return 1;
    tyl_globals(old_globals,1);
    if(!limit&&!status&&tyl_has_real(index))status=tyl_real_pass(index);
    tyl_globals(old_globals,1);
    if(!limit&&!status&&compare_probe_capable(index))status=compare_probe_run(index,0,tyl_compare_build,tyl_compare_state,0x3000+TYL_GLOBALS);
    tyl_globals(old_globals,1);
    if(!status){
        printf("PASS: %-20s %u cases (stub calls",function_hook_descriptor(index)->name,cases);
        for(unsigned i=0;i<TYL_IDS;++i)if(tyl_paths[i])printf(" %u:%u",i,tyl_paths[i]);
        printf("); full return pair and ECX, ordered calls with entry registers, slots and context word, arena/globals/FS:0\n");
    }
    return status;
}
