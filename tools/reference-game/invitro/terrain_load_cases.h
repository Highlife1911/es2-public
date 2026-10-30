/* terrain_load group: zone_load 42789c, grid_ctor 46befc, height_cache_ctor
 * 495268, terrain_materials 46c754, terrain_cells 46c4c4 and cell_mark_set
 * 470d74 (docs/reverse-engineering/terrain.md, "the terrain zone load").
 *
 * Direct cases run the original body (EAX/EDX/ECX, EBX, ESI and EDI at entry
 * from the case, six argument slots, over a stack filled with a case
 * pattern) and the core against logging stubs for every game callee of the
 * hook under test: 49630c (a record stub: the real FS:0 head at record+0,
 * context word +10h zeroed, the record linked into FS:0), _itoa 496820 (it
 * writes the digits), the path builder 492bec (an arena path), the stream
 * constructor 472f14 (it writes the stream's vtable and status), the
 * assertion 479c78, 472f64, new 47749c, new[] 4774f0, 49667c, the stream
 * reads (the arena vtables' slot +18h: a scripted dword) and stream_read
 * 4735b8 (a material record), _strchr 4967a2 and _stricmp 496868 (computed
 * like the runtime's, or skewed), dts_load 47a144 (an object whose first
 * bitmap is the arena bitmap), 496406, the bitmap's slot +18h, _fopen
 * 496802, _fscanf 4967b4 (it writes its conversions), rng_next 492ee0,
 * _fclose 496838, 4952e8, 4952fc, euler_matrix 47ebb8 and the other hooks
 * of the group. Stack addresses in logged registers and slots are masked
 * (the original's frame against the core's scratch); the strings and words
 * the frame passes are compared by content (a hash). Compared: the full
 * return pair, ECX, the ordered calls with argument slots (and the bridge's
 * slot count), entry EAX/EDX/ECX and the record's context word, the arena,
 * the image globals and FS:0. The stubs change what the originals reread
 * (the grid pointer 49AAC8h, the stream's and the bitmap's vtables, the
 * count pointer 6CBEE4h, the origin, the material count and table, the
 * level, the cell shift, the maximum, the scale, the cell, mark and pixel
 * arrays); in even cases the inputs are the real callers' and the adapter is
 * compared as well, with the callee counter deltas.
 *
 * Then installed 31/a7 poison probes and real-callee passes with the
 * nested hooks in rotating original/replaced modes, the hook under test
 * original or replaced: zone_load over the real 49630c, grid_ctor,
 * height_cache_ctor, 49667c with its element constructors, terrain_materials
 * (the ".dba" path), terrain_cells, cell_mark_set, euler_matrix, rng_next
 * (stream A), 4952e8 and 4952fc; terrain_materials over the real 49630c,
 * terrain_cells, cell_mark_set, 49667c, rng_next, 4952e8, 4952fc (both
 * paths); terrain_cells over the real cell_mark_set, rng_next, 49667c,
 * 4952e8, 4952fc; grid_ctor over the real height_cache_ctor and 49667c;
 * height_cache_ctor over the real 49667c. new and new[] are a bump
 * allocator there; the file, string and path functions stay stubs (their
 * imports are traps in the harness). Identifiers use the prefix tlc_/TLC_. */
typedef struct tlc_record { uint32_t id,slots,args[6],regs[3],context,content,eax,edx; } tlc_record;
enum {
    TLC_SIZE=0x6000, TLC_MAX=1400, TLC_IDS=28,
    TLC_G=0x100, TLC_G2=0x280, TLC_VT=0x400, TLC_VT2=0x440, TLC_BVT=0x480, TLC_BVT2=0x4c0,
    TLC_B=0x500, TLC_O=0x540, TLC_ARR=0x560, TLC_NAME=0x580, TLC_PATH=0x5c0, TLC_PATH2=0x600, TLC_FILE=0x640,
    TLC_COUNTER=0x660, TLC_COUNTER2=0x668, TLC_C=0x680, TLC_MAT=0x700, TLC_MAT2=0x780,
    TLC_PIX=0x800, TLC_PIX2=0x900, TLC_MARKS=0xa00, TLC_MARKS2=0xb00, TLC_CACHE=0xc00,
    TLC_CELLS=0x1000, TLC_CELLS2=0x2000, TLC_BUMP=0x3000
};
#define TLC_B0 ((uint32_t)(uintptr_t)tlc_arena)
#define TLC_ADDR(o) (TLC_B0+(uint32_t)(o))
#define TLC_IMG(va) ((uint32_t)(uintptr_t)(mapped+((uint32_t)(va)-0x400000u)))
#define TLC_BIT(i) ((uint32_t)1<<(i))
static BYTE tlc_arena[TLC_SIZE],tlc_before[TLC_SIZE],tlc_after[TLC_SIZE];
static tlc_record tlc_log[TLC_MAX];
static unsigned tlc_calls,tlc_real,tlc_effects,tlc_exact,tlc_index,tlc_reads,tlc_paths[TLC_IDS];
static uint32_t tlc_seed,tlc_stream,tlc_bump,tlc_head,tlc_obj,tlc_bitmap;
__attribute__((used)) static uint32_t tlc_ecx;
/* 0 49630c, 1 _itoa 496820, 2 492bec, 3 472f14, 4 479c78, 5 the stream
 * read (slot +18h; not patched: the arena vtables point to its stub), 6 new
 * 47749c, 7 46befc, 8 46c754, 9 euler_matrix 47ebb8, 10 472f64, 11 new[]
 * 4774f0, 12 stream_read 4735b8, 13 _strchr 4967a2, 14 _stricmp 496868, 15
 * dts_load 47a144, 16 46c4c4, 17 496406, 18 the bitmap's slot +18h (not
 * patched), 19 _fopen 496802, 20 _fscanf 4967b4, 21 49667c, 22 rng_next
 * 492ee0, 23 _fclose 496838, 24 4952e8, 25 4952fc, 26 470d74, 27 495268.
 * The slots each original call site pushes (_fscanf: 6 or 3 by format). */
static const uint32_t tlc_addresses[TLC_IDS]={0x49630c,0x496820,0x492bec,0x472f14,0x479c78,0,0x47749c,0x46befc,0x46c754,0x47ebb8,
    0x472f64,0x4774f0,0x4735b8,0x4967a2,0x496868,0x47a144,0x46c4c4,0x496406,0,0x496802,0x4967b4,0x49667c,0x492ee0,0x496838,
    0x4952e8,0x4952fc,0x470d74,0x495268};
static const unsigned tlc_counts[TLC_IDS]={0,3,2,2,6,3,1,2,3,2,2,1,3,2,2,2,3,2,2,2,6,5,1,1,3,1,3,2};
static uint32_t tlc_next(void){
    uint32_t z=tlc_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *tlc_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static uint32_t tlc_fs0(void){uint32_t v;__asm__ volatile("movl %%fs:0,%0":"=r"(v));return v;}
static int tlc_on_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static int tlc_in_arena(uint32_t a,uint32_t n){return a-TLC_B0<TLC_SIZE&&n<=TLC_SIZE-(a-TLC_B0);}
static void tlc_reset(void){tlc_calls=0;tlc_reads=0;tlc_stream=tlc_seed;tlc_bump=TLC_ADDR(TLC_BUMP);tlc_head=tlc_fs0();}
static void tlc_random(unsigned offset,unsigned size){for(unsigned i=0;i<size;++i)tlc_arena[offset+i]=(BYTE)tlc_next();}
static uint32_t tlc_alloc(uint32_t size){
    const uint32_t p=tlc_bump;size=(size+15u)&~7u;
    if(size>0x1400u||p+size>TLC_ADDR(TLC_SIZE)){fail("terrain_load bump allocator overflow");ExitProcess(1);}
    memset(tlc_host(p),0,size);tlc_bump+=size;return p;
}
/* FNV-1a over a NUL-terminated string (at most 96 bytes) or n bytes. */
static uint32_t tlc_hash(uint32_t p,unsigned n,int string){
    uint32_t h=0x811c9dc5u;
    for(unsigned i=0;i<n;++i){const BYTE b=tlc_host(p)[i];h=(h^b)*0x01000193u;if(string&&!b)break;}
    return h;
}
static uint32_t tlc_content(unsigned id,const uint32_t *a){
    switch(id){
    case 2:case 13:case 14:case 19:return tlc_hash(a[0],96,1);
    case 3:return tlc_hash(a[1],96,1);
    case 4:return tlc_hash(a[5],96,1);
    case 9:return tlc_hash(a[0],6,0);
    }
    return 0;
}
/* The grid the effects work on: the hook's G, or zone_load's [49AAC8h]. */
static uint32_t tlc_grid(void){
    if(tlc_index!=ES2_HOOK_INDEX_zone_load)return tlc_obj;
    const uint32_t g=get32(mapped+0x9aac8);return g==TLC_ADDR(TLC_G)||g==TLC_ADDR(TLC_G2)?g:0;
}
static uint32_t tlc_other(uint32_t value,unsigned a,unsigned b){return value==TLC_ADDR(a)?TLC_ADDR(b):TLC_ADDR(a);}
/* Effects of the cell loops (terrain_cells; terrain_materials' text grid):
 * the fields and arrays the originals reread per cell. */
static void tlc_cell_effect(uint32_t pick){
    const uint32_t G=tlc_obj;
    if(tlc_index==ES2_HOOK_INDEX_terrain_cells){
        switch(pick%7){
        case 0:put32(tlc_host(G+0x121),tlc_other(get32(tlc_host(G+0x121)),TLC_MAT,TLC_MAT2));break;
        case 1:put32(tlc_host(get32(tlc_host(G+0x121))+4),tlc_next()%10);break;
        case 2:put32(tlc_host(G+0x108),10+tlc_next()%7);break;
        case 3:put32(tlc_host(G+0x114),tlc_next()%3?tlc_next()%300:tlc_next());break;
        case 4:put32(tlc_host(G+0xec),tlc_other(get32(tlc_host(G+0xec)),TLC_CELLS,TLC_CELLS2));break;
        case 5:put32(tlc_host(tlc_bitmap+0xe),tlc_other(get32(tlc_host(tlc_bitmap+0xe)),TLC_PIX,TLC_PIX2));break;
        default:put32(tlc_host(G+0x118),tlc_next());break;
        }
    } else {
        switch(pick%3){
        case 0:put32(tlc_host(G+0x11d),tlc_next()%9);break;
        case 1:put32(tlc_host(G+0xec),tlc_other(get32(tlc_host(G+0xec)),TLC_CELLS,TLC_CELLS2));break;
        default:put32(tlc_host(G+0xf0),tlc_other(get32(tlc_host(G+0xf0)),TLC_MARKS,TLC_MARKS2));break;
        }
    }
}
/* The dword a stream read delivers: zone_load's four fields, or the
 * material count of terrain_materials. */
static uint32_t tlc_read_value(void){
    const unsigned k=tlc_reads++;
    if(tlc_index==ES2_HOOK_INDEX_terrain_materials||(tlc_index==ES2_HOOK_INDEX_zone_load&&k>=4)){
        if(tlc_exact)return 1+tlc_next()%(tlc_real?5:8);
        return tlc_next()%8?tlc_next()%17:(uint32_t)-(int32_t)(tlc_next()%3);
    }
    if(k==2)return tlc_exact?13+tlc_next()%4:(tlc_next()%2?10+tlc_next()%10:tlc_next());
    return tlc_exact&&k<2?7+tlc_next()%2:tlc_next();
}
static int tlc_lower(int c){return c>='A'&&c<='Z'?c+32:c;}
/* What the stubs answer and change. */
static uint32_t tlc_answer(unsigned id,const uint32_t *a,uint32_t eax,uint32_t pick){
    const int effect=tlc_effects&&pick%4==0;
    switch(id){
    case 1: { /* _itoa(value, buffer, 10): the digits */
        char text[16];snprintf(text,sizeof text,"%d",(int)a[0]);
        for(unsigned k=0;k<=strlen(text);++k)tlc_host(a[1])[k]=(BYTE)text[k];
        return eax;
    }
    case 2:   /* the path builder: an arena path ("dba" gets the bitmap path) */
        if(tlc_exact)return a[1]==TLC_IMG(0x49aba0)?TLC_ADDR(TLC_PATH2):TLC_ADDR(TLC_PATH);
        return pick&0x100?TLC_ADDR(TLC_PATH):TLC_ADDR(TLC_PATH2);
    case 3:   /* the stream constructor: vtable and status */
        put32(tlc_host(a[0]),TLC_ADDR(TLC_VT));
        put32(tlc_host(a[0]+4),tlc_exact||tlc_next()%4?0:tlc_next()|1u);
        return a[0];
    case 5: { /* the read (S, n, buffer) */
        if(a[1]==4)put32(tlc_host(a[2]),tlc_read_value());
        if(tlc_index==ES2_HOOK_INDEX_zone_load&&!get32(mapped+0x9aac8))put32(mapped+0x9aac8,TLC_ADDR(TLC_G2));
        else if(effect&&tlc_index==ES2_HOOK_INDEX_zone_load&&(pick&0x200))
            put32(mapped+0x9aac8,tlc_other(get32(mapped+0x9aac8),TLC_G,TLC_G2));
        else if(effect)put32(tlc_host(a[0]),get32(tlc_host(a[0]))==TLC_ADDR(TLC_VT)?TLC_ADDR(TLC_VT2):TLC_ADDR(TLC_VT));
        return eax;
    }
    case 6:   /* new: the grid */
        if(tlc_real)return tlc_alloc(a[0]);
        if(tlc_exact)return TLC_ADDR(TLC_G);
        return pick%6==0?0:pick&0x100?TLC_ADDR(TLC_G):TLC_ADDR(TLC_G2);
    case 7:case 27:   /* 46befc, 495268: the count pointer reread */
        if(effect)put32(mapped+0x2cbee4,tlc_other(get32(mapped+0x2cbee4),TLC_COUNTER,TLC_COUNTER2));
        return eax;
    case 8: { /* 46c754: the origin reread for the transform */
        const uint32_t g=tlc_grid();
        if(effect&&g)for(unsigned k=0;k<3;++k)put32(tlc_host(g+0xf4+4*k),tlc_next());
        return eax;
    }
    case 11:  /* new[]: the materials, the marks, the cache bytes */
        if(tlc_real)return tlc_alloc(a[0]);
        if(tlc_index==ES2_HOOK_INDEX_terrain_materials)return pick&0x100?TLC_ADDR(TLC_MAT):TLC_ADDR(TLC_MAT2);
        if(tlc_index==ES2_HOOK_INDEX_terrain_cells)return pick&0x100?TLC_ADDR(TLC_MARKS):TLC_ADDR(TLC_MARKS2);
        if(effect)put32(tlc_host(tlc_obj+0xc),tlc_next());
        return eax;
    case 12: { /* stream_read(S, 8, record): {texture, level} */
        if(tlc_in_arena(a[2],8)){
            put32(tlc_host(a[2]),tlc_exact?tlc_next()%24:tlc_next());
            put32(tlc_host(a[2]+4),tlc_exact?(tlc_real?5+tlc_next()%2:3+tlc_next()%5):tlc_next());
        }
        if(effect)switch(pick>>9&1){
        case 0:put32(tlc_host(tlc_obj+0x11d),tlc_next()%17);break;
        default:put32(tlc_host(tlc_obj+0x121),tlc_other(get32(tlc_host(tlc_obj+0x121)),TLC_MAT,TLC_MAT2));break;
        }
        return eax;
    }
    case 13: { /* _strchr(s, '.'): the first dot, or (skewed) none or another position inside s */
        uint32_t p=0,length=0;
        for(uint32_t k=0;;++k){const BYTE b=tlc_host(a[0])[k];if(b==(BYTE)a[1]&&!p)p=a[0]+k;if(!b){length=k;break;}}
        if(effect){const uint32_t k=(pick>>10)%4;p=pick&0x200?0:a[0]+(k<length?k:length);}
        return p;
    }
    case 14: { /* _stricmp(s, ".dba") */
        const char *s=(const char *)tlc_host(a[0]),*t=(const char *)tlc_host(a[1]);uint32_t r=0;
        for(unsigned k=0;;++k){const int x=tlc_lower((unsigned char)s[k]),y=tlc_lower((unsigned char)t[k]);
            if(x!=y){r=x<y?0xffffffffu:1u;break;}if(!x)break;}
        if(effect)r=pick&0x200?0:pick|1u;
        return r;
    }
    case 15:  /* dts_load: the object whose [[+8]] is the bitmap */
        return TLC_ADDR(TLC_O);
    case 16:  /* 46c4c4: the count pointer and the bitmap's vtable reread */
        if(effect&&(pick&0x200))put32(mapped+0x2cbee4,tlc_other(get32(mapped+0x2cbee4),TLC_COUNTER,TLC_COUNTER2));
        else if(effect&&a[1]==TLC_ADDR(TLC_B))put32(tlc_host(a[1]),tlc_other(get32(tlc_host(a[1])),TLC_BVT,TLC_BVT2));
        return eax;
    case 17:  /* 496406: a count */
        return tlc_exact?1+tlc_next()%3:eax%7;
    case 19:  /* _fopen */
        return TLC_ADDR(TLC_FILE);
    case 20:  /* _fscanf: the four fields (exponents, top, bottom) or one height */
        if(a[1]==TLC_IMG(0x4a0c59)){
            uint32_t c=tlc_next()%4,r=tlc_next()%(5-c);
            if(!tlc_exact&&pick%5==0){static const uint32_t big[4][2]={{31,1},{16,16},{33,0},{36,2}};c=big[pick>>8&3][0];r=big[pick>>8&3][1];}
            put32(tlc_host(a[2]),c);put32(tlc_host(a[3]),r);put32(tlc_host(a[4]),tlc_next());put32(tlc_host(a[5]),tlc_next());
        } else put32(tlc_host(a[2]),tlc_exact?tlc_next()%2048:tlc_next());
        return eax;
    case 21:  /* 49667c: the cells (or any value) */
        if(tlc_index==ES2_HOOK_INDEX_terrain_cells||tlc_index==ES2_HOOK_INDEX_terrain_materials)
            return pick&0x100?TLC_ADDR(TLC_CELLS):TLC_ADDR(TLC_CELLS2);
        if(effect)put32(mapped+0x2cbee4,tlc_other(get32(mapped+0x2cbee4),TLC_COUNTER,TLC_COUNTER2));
        return eax;
    case 22:case 26:   /* rng_next, 470d74: the per-cell rereads */
        if(effect)tlc_cell_effect(pick>>8);
        return eax;
    }
    return eax;
}
__attribute__((used,noinline)) static uint64_t __cdecl tlc_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(tlc_calls==TLC_MAX){fail("terrain_load call overflow");ExitProcess(1);}
    tlc_record *v=tlc_log+tlc_calls++;memset(v,0,sizeof(*v));v->id=id;
    const unsigned original=id==20?(a[1]==TLC_IMG(0x4a0c59)?6u:3u):tlc_counts[id];
    v->slots=weapons_bridge_slots(a,original);
    for(unsigned k=0;k<original;++k)v->args[k]=tlc_on_stack(a[k])?0:a[k];
    for(unsigned j=0;j<3;++j)v->regs[j]=tlc_on_stack(regs[j])?0:regs[j];
    if(id&&tlc_fs0()!=tlc_head)v->context=get16(tlc_host(tlc_fs0()+16u));   /* a hook's own record */
    v->content=tlc_content(id,a);
    uint32_t eax=tlc_next(),edx=tlc_next(),pick=tlc_next();tlc_ecx=tlc_next();
    if(id==0)eax=tlc_fs0();
    else eax=tlc_answer(id,a,eax,pick);
    v->eax=tlc_on_stack(eax)?0:eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define TLC_STUB(n) __attribute__((naked)) static void tlc_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _tlc_stub_impl\n\taddl $24,%esp\n\tmovl _tlc_ecx,%ecx\n\tret\n\t");}
TLC_STUB(1) TLC_STUB(2) TLC_STUB(3) TLC_STUB(4) TLC_STUB(5) TLC_STUB(6) TLC_STUB(7) TLC_STUB(8) TLC_STUB(9) TLC_STUB(10)
TLC_STUB(11) TLC_STUB(12) TLC_STUB(13) TLC_STUB(14) TLC_STUB(15) TLC_STUB(16) TLC_STUB(17) TLC_STUB(18) TLC_STUB(19)
TLC_STUB(20) TLC_STUB(21) TLC_STUB(22) TLC_STUB(23) TLC_STUB(24) TLC_STUB(25) TLC_STUB(26) TLC_STUB(27)
#undef TLC_STUB
/* 49630c: logs like the other stubs (EDX and ECX come back unchanged, as the
 * original's do), then links a record at EBP + [descriptor+4] into FS:0
 * with the saved head at record+0 and a zero context word. */
__attribute__((naked)) static void tlc_stub_0(void){__asm__ volatile(
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
    "pushl $0\n\tcalll _tlc_stub_impl\n\taddl $12,%esp\n\t"
    "popl %ecx\n\tmovl 4(%ecx),%ecx\n\taddl %ebp,%ecx\n\tmovl %eax,(%ecx)\n\tmovw $0,16(%ecx)\n\tmovl %ecx,%fs:0\n\t"
    "popl %edx\n\tpopl %ecx\n\tret\n\t");}
static void *tlc_stubs[TLC_IDS]={tlc_stub_0,tlc_stub_1,tlc_stub_2,tlc_stub_3,tlc_stub_4,tlc_stub_5,tlc_stub_6,tlc_stub_7,
    tlc_stub_8,tlc_stub_9,tlc_stub_10,tlc_stub_11,tlc_stub_12,tlc_stub_13,tlc_stub_14,tlc_stub_15,tlc_stub_16,tlc_stub_17,
    tlc_stub_18,tlc_stub_19,tlc_stub_20,tlc_stub_21,tlc_stub_22,tlc_stub_23,tlc_stub_24,tlc_stub_25,tlc_stub_26,tlc_stub_27};
static int tlc_patch(BYTE saved[TLC_IDS][5],int active[TLC_IDS],uint32_t which,int on){
    for(unsigned i=0;i<TLC_IDS;++i){
        if(!tlc_addresses[i])continue;
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(tlc_addresses[i]-0x400000u,tlc_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(tlc_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
/* Direct callees of each hook (bit i = tlc_addresses[i]; the two vtable
 * slots are never patched). */
static uint32_t tlc_which(unsigned index){
    if(index==ES2_HOOK_INDEX_zone_load)
        return TLC_BIT(0)|TLC_BIT(1)|TLC_BIT(2)|TLC_BIT(3)|TLC_BIT(4)|TLC_BIT(6)|TLC_BIT(7)|TLC_BIT(8)|TLC_BIT(9)|TLC_BIT(10);
    if(index==ES2_HOOK_INDEX_grid_ctor)return TLC_BIT(21)|TLC_BIT(27);
    if(index==ES2_HOOK_INDEX_height_cache_ctor)return TLC_BIT(11)|TLC_BIT(21);
    if(index==ES2_HOOK_INDEX_terrain_materials)
        return TLC_BIT(0)|TLC_BIT(2)|TLC_BIT(3)|TLC_BIT(10)|TLC_BIT(11)|TLC_BIT(12)|TLC_BIT(13)|TLC_BIT(14)|TLC_BIT(15)|TLC_BIT(16)|
            TLC_BIT(17)|TLC_BIT(19)|TLC_BIT(20)|TLC_BIT(21)|TLC_BIT(22)|TLC_BIT(23)|TLC_BIT(24)|TLC_BIT(25);
    if(index==ES2_HOOK_INDEX_terrain_cells)return TLC_BIT(11)|TLC_BIT(21)|TLC_BIT(22)|TLC_BIT(24)|TLC_BIT(25)|TLC_BIT(26);
    return 0;
}
/* Stubbed in the real-callee passes (the rest runs for real). */
static uint32_t tlc_real_which(unsigned index){
    if(index==ES2_HOOK_INDEX_zone_load)
        return TLC_BIT(1)|TLC_BIT(2)|TLC_BIT(3)|TLC_BIT(4)|TLC_BIT(6)|TLC_BIT(10)|TLC_BIT(11)|TLC_BIT(12)|TLC_BIT(13)|TLC_BIT(14)|
            TLC_BIT(15)|TLC_BIT(17)|TLC_BIT(19)|TLC_BIT(20)|TLC_BIT(23);
    if(index==ES2_HOOK_INDEX_terrain_materials)
        return TLC_BIT(2)|TLC_BIT(3)|TLC_BIT(10)|TLC_BIT(11)|TLC_BIT(12)|TLC_BIT(13)|TLC_BIT(14)|TLC_BIT(15)|TLC_BIT(17)|
            TLC_BIT(19)|TLC_BIT(20)|TLC_BIT(23);
    return TLC_BIT(11);
}
/* The image globals: the grid copy, 49AAD0h, the first-use flag and the
 * name template (49AAC8h..49AAF9h), the object table, the zone transform
 * and the detail words (4AAA74h..4AAAAFh), the terrain pointer 4A0BE0h, the
 * extension pointer 4A09F0h, the prefs byte 4D1F40h, the count pointer
 * 6CBEE4h and stream A (4D2599h, 72h bytes). */
static const struct { uint32_t rva,size; } tlc_ranges[]={{0x9aac8,0x32},{0xaaa74,0x3c},{0xa0be0,4},{0xa09f0,4},{0xd1f40,1},
    {0x2cbee4,4},{0xd2599,0x72}};
enum { TLC_GLOBALS=0x32+0x3c+4+4+1+4+0x72 };
static void tlc_globals(BYTE *out,int restore){
    for(unsigned i=0;i<sizeof(tlc_ranges)/sizeof(tlc_ranges[0]);++i){
        if(restore)memcpy(mapped+tlc_ranges[i].rva,out,tlc_ranges[i].size);else memcpy(out,mapped+tlc_ranges[i].rva,tlc_ranges[i].size);
        out+=tlc_ranges[i].size;
    }
}
static int tlc_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<TLC_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
static void tlc_string(unsigned offset,const char *s){memcpy(tlc_arena+offset,s,strlen(s)+1);}
/* A grid of 2^c x 2^r cells as 46c4c4 derives it: the signed word whose
 * exponent is k (for k = 0 also 0, 1 and negative words). */
static uint16_t tlc_word(unsigned k,int exact){
    if(!k){const unsigned j=tlc_next()%(exact?2:4);return j<2?(uint16_t)j:(uint16_t)(0x8000u|tlc_next());}
    const unsigned low=(1u<<(k-1))+1u,high=1u<<k;
    return (uint16_t)(exact?high:low+tlc_next()%(high-low+1));
}
/* The case data. exact: the real callers' inputs; real: data for the
 * real-callee passes. */
static void tlc_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],uint32_t regs[3],uint32_t *fill,int exact,int real){
    tlc_seed=random32();tlc_stream=tlc_seed^n;tlc_real=real;tlc_exact=exact||real;tlc_effects=!real&&!exact;tlc_index=index;
    for(unsigned i=0;i<3;++i){r[i]=tlc_next();regs[i]=tlc_next();}
    tlc_random(0,TLC_SIZE);memset(a,0,24);
    *fill=!exact&&!real&&tlc_next()%2?tlc_next():0;
    const uint32_t G=TLC_ADDR(TLC_G);tlc_obj=G;tlc_bitmap=TLC_ADDR(TLC_B);
    /* the vtables (slot +18h: the read and the bitmap's destructor stubs) */
    put32(tlc_arena+TLC_VT+0x18,(uint32_t)(uintptr_t)tlc_stub_5);put32(tlc_arena+TLC_VT2+0x18,(uint32_t)(uintptr_t)tlc_stub_5);
    put32(tlc_arena+TLC_BVT+0x18,(uint32_t)(uintptr_t)tlc_stub_18);put32(tlc_arena+TLC_BVT2+0x18,(uint32_t)(uintptr_t)tlc_stub_18);
    put32(mapped+0x2cbee4,TLC_ADDR(TLC_COUNTER));
    put32(mapped+0xa09f0,exact||real?TLC_IMG(0x4a0a36):tlc_next());
    mapped[0xd1f40]=(BYTE)tlc_next();
    put32(mapped+0x9aac8,tlc_next()%2?G:TLC_ADDR(TLC_G2));
    mapped[0x9aaf0]=(BYTE)(tlc_next()%2?0:tlc_next());
    /* paths, the bitmap and its object, the material tables */
    tlc_string(TLC_PATH,"dat\\zone39.dat");tlc_string(TLC_PATH2,"dba\\zone39.dba");
    const unsigned c=tlc_next()%5,rr=tlc_next()%(exact||real?5-c:7-c);
    put32(tlc_arena+TLC_B,TLC_ADDR(TLC_BVT));put16(tlc_arena+TLC_B+4,tlc_word(rr,exact||real));put16(tlc_arena+TLC_B+6,tlc_word(c,exact||real));
    put32(tlc_arena+TLC_B+0xe,TLC_ADDR(TLC_PIX));
    put32(tlc_arena+TLC_G+0xdc,0x20);put32(tlc_arena+TLC_G+0xe0,TLC_ADDR(TLC_CACHE));
    put32(tlc_arena+TLC_G+0xe4,TLC_ADDR(TLC_CACHE+0x40));put32(tlc_arena+TLC_G+0xe8,TLC_ADDR(TLC_CACHE+0x40));
    put32(tlc_arena+TLC_O+8,TLC_ADDR(TLC_ARR));put32(tlc_arena+TLC_ARR,!exact&&!real&&tlc_next()%5==0?0:TLC_ADDR(TLC_B));
    if(index==ES2_HOOK_INDEX_zone_load){
        const int16_t zone=(int16_t)(exact||real?(tlc_next()%8?(int)(tlc_next()%1000):-(int)(1+tlc_next()%5)):(int)tlc_next());
        a[0]=(tlc_next()&0xffff0000u)|(uint16_t)zone;
        if(real&&zone<0)a[0]=(a[0]&0xffff0000u)|(uint16_t)(tlc_next()%40);
    } else if(index==ES2_HOOK_INDEX_grid_ctor){
        a[0]=exact||real?G:TLC_ADDR(0x100+8*(tlc_next()%32));a[1]=tlc_next();tlc_obj=a[0];
    } else if(index==ES2_HOOK_INDEX_height_cache_ctor){
        a[0]=exact||real?TLC_ADDR(TLC_C):TLC_ADDR(0x600+4*(tlc_next()%32));a[1]=exact||real?0x100:tlc_next();tlc_obj=a[0];
        if(real)a[1]=0x100;
    } else if(index==ES2_HOOK_INDEX_terrain_materials){
        char name[64];const unsigned zone=tlc_next()%2000,kind=exact||real?tlc_next()%(real?4:3):tlc_next()%9;
        static const char *const forms[9]={"dba\\zone%u.dba","DBA\\ZONE%u.DBA","dba\\zone%u.Dba","grid%u","dat\\zone%u.dat",
            "a.b%u.dba","dba\\zone%u.dbax","%u.","x\\%u.dba.dba"};
        snprintf(name,sizeof name,real&&kind==3?"grid%u":forms[kind],zone);
        if(!exact&&!real&&tlc_next()%7==0){memset(name,'q',40+tlc_next()%20);name[60]=0;memcpy(name+40,".dba",5);}
        tlc_string(TLC_NAME,name);
        a[0]=G;a[1]=TLC_ADDR(TLC_NAME);a[2]=exact||real?(tlc_next()&0xffffff00u):tlc_next();
        put32(tlc_arena+TLC_G+0x108,exact||real?13+tlc_next()%4:tlc_next()%2?10+tlc_next()%10:tlc_next());
        put32(tlc_arena+TLC_G+0xf0,TLC_ADDR(TLC_MARKS));
    } else if(index==ES2_HOOK_INDEX_terrain_cells){
        a[0]=G;a[1]=TLC_ADDR(TLC_B);a[2]=exact||real?tlc_next()%4:tlc_next();
        put32(tlc_arena+TLC_G+0x121,TLC_ADDR(TLC_MAT));
        put32(tlc_arena+TLC_MAT+4,exact||real?4+tlc_next()%3:tlc_next()%3?tlc_next()%12:tlc_next());
        put32(tlc_arena+TLC_MAT2+4,tlc_next()%10);
        put32(tlc_arena+TLC_G+0x108,exact||real?13+tlc_next()%3:tlc_next()%2?10+tlc_next()%8:tlc_next());
        put32(tlc_arena+TLC_G+0x118,exact||real?tlc_next()%300:tlc_next());
    } else {
        /* cell_mark_set: the cell array, the marks, a cell; the byte lands
         * in the arena, also from a cell below the base (unsigned shift) */
        const uint32_t base=TLC_ADDR(TLC_CELLS+16*(tlc_next()%64)),k=tlc_next()%3;
        uint32_t cell=base+16*(tlc_next()%256)+(exact?0:tlc_next()%16),marks=TLC_ADDR(TLC_MARKS+(tlc_next()%64));
        if(!exact&&k==0){cell=base-16*(1+tlc_next()%4)+(tlc_next()%16);marks=TLC_ADDR(TLC_MARKS+0x80)-((cell-base)>>4);}
        if(!exact&&k==1)marks=TLC_ADDR(TLC_G+0xd0+tlc_next()%0x40)-((cell-base)>>4);   /* the mark over the grid's fields */
        put32(tlc_arena+TLC_G+0xec,base);put32(tlc_arena+TLC_G+0xf0,marks);
        a[0]=G;a[1]=cell;a[2]=exact?tlc_next()%2:tlc_next();
    }
    tlc_stream=tlc_seed;   /* the stubs' stream restarts here in every run */
}
/* Calls target with six argument slots, EAX/EDX/ECX = r and EBX/ESI/EDI =
 * regs over 3,840 bytes of stack filled with `fill`; out receives EAX, EDX
 * and ECX after the call. */
__attribute__((naked,noinline)) static void tlc_invoke(void *target,const uint32_t *a,const uint32_t *r,const uint32_t *regs,uint32_t fill,uint32_t out[3]){
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
static uint64_t tlc_core(unsigned index,const uint32_t *a,const uint32_t *r,uint32_t *ecx){
    const es2_guest_memory *m=&math_native_memory;
    if(index==ES2_HOOK_INDEX_zone_load){
        BYTE scratch[ES2_ZONE_LOAD_SCRATCH]={0};es2_weapon_graph g=weapons_graph(mapped,scratch,index);
        weapons_impact_context c={(uint32_t)(uintptr_t)mapped,(uint32_t)(uintptr_t)(scratch+ES2_ZONE_LOAD_FRAME)};
        g.call=weapons_impact_guest_call;g.context=&c;
        const uint64_t v=es2_zone_load_registers(m,&g,a[0],r,ecx);
        const uint32_t head=*ecx;__asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");
        return v;
    }
    if(index==ES2_HOOK_INDEX_terrain_materials){
        BYTE scratch[ES2_TERRAIN_MATERIALS_SCRATCH]={0};es2_weapon_graph g=weapons_graph(mapped,scratch,index);
        weapons_impact_context c={(uint32_t)(uintptr_t)mapped,(uint32_t)(uintptr_t)(scratch+ES2_TERRAIN_MATERIALS_FRAME)};
        g.call=weapons_impact_guest_call;g.context=&c;
        const uint64_t v=es2_terrain_materials_registers(m,&g,a[0],a[1],a[2],r,ecx);
        const uint32_t head=(uint32_t)(v>>32);__asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");
        return v;
    }
    es2_weapon_graph g=weapons_graph(mapped,NULL,index);
    if(index==ES2_HOOK_INDEX_grid_ctor)return es2_grid_ctor_registers(m,&g,a[0],r,ecx);
    if(index==ES2_HOOK_INDEX_height_cache_ctor)return es2_height_cache_ctor_registers(m,&g,a[0],a[1],r,ecx);
    if(index==ES2_HOOK_INDEX_terrain_cells)return es2_terrain_cells_registers(m,&g,a[0],a[1],a[2],r,ecx);
    return es2_cell_mark_set_registers(m,a[0],a[1],a[2],r,ecx);
}
static int tlc_report(const char *what,unsigned n,const uint32_t want[3],const uint32_t got[3],unsigned calls,const tlc_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x calls %u/%u\n",current_function,n,what,
        want[0],got[0],want[1],got[1],want[2],got[2],calls,tlc_calls);
    for(unsigned j=0;j<calls&&j<tlc_calls&&expected;++j)if(memcmp(expected+j,tlc_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(tlc_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<TLC_SIZE;++j)if(tlc_after[j]!=tlc_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,tlc_after[j],tlc_arena[j]);break;}
    return 1;
}
/* Callee counter hits of the logged stub calls (the slot +18h of the reads
 * and of the bitmap's destructor). */
static unsigned tlc_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<tlc_calls;++j){
        const unsigned id=tlc_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?(id==5||id==18)&&c->key==0x18u:tlc_addresses[id]==c->key)++hits;
    }
    return hits;
}
/* One direct case: the original, the core and (exact inputs) the adapter. */
static int tlc_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r,const uint32_t *regs,uint32_t fill,int exact,int check_ecx){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[TLC_GLOBALS],after[TLC_GLOBALS],actual[TLC_GLOBALS];static tlc_record expected[TLC_MAX];uint32_t want[3],got[3];
    const uint32_t head=tlc_fs0();
    memcpy(tlc_before,tlc_arena,TLC_SIZE);tlc_globals(before,0);
    tlc_reset();tlc_invoke(mapped+d->rva,a,r,regs,fill,want);const unsigned calls=tlc_calls;memcpy(expected,tlc_log,calls*sizeof(*expected));
    memcpy(tlc_after,tlc_arena,TLC_SIZE);tlc_globals(after,0);
    if(tlc_fs0()!=head)return fail("terrain_load original left FS:0 changed");
    for(unsigned j=0;j<calls;++j)++tlc_paths[expected[j].id];
    memcpy(tlc_arena,tlc_before,TLC_SIZE);tlc_globals(before,1);tlc_reset();
    got[2]=~want[2];
    uint64_t pair=tlc_core(index,a,r,&got[2]);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);tlc_globals(actual,0);
    if(tlc_fs0()!=head)return fail("terrain_load core left FS:0 changed");
    if(want[0]!=got[0]||want[1]!=got[1]||(check_ecx&&want[2]!=got[2])||calls!=tlc_calls||memcmp(expected,tlc_log,calls*sizeof(*expected))||
       memcmp(tlc_after,tlc_arena,TLC_SIZE)||tlc_globals_differ(after,actual))
        return tlc_report("core",n,want,got,calls,expected);
    if(!exact){tlc_globals(before,1);return 0;}
    /* the adapter (the framework's entry: six argument slots) */
    memcpy(tlc_arena,tlc_before,TLC_SIZE);tlc_globals(before,1);tlc_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[24];
    if(count>24)return fail("terrain_load counter table too long");
    for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair=x->replacement_context(a,d->user,r);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);got[2]=want[2];
    tlc_globals(actual,0);
    if(tlc_fs0()!=head)return fail("terrain_load adapter left FS:0 changed");
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=tlc_expected_hits(c+i))return fail("terrain_load callee counter delta");
    if(want[0]!=got[0]||want[1]!=got[1]||calls!=tlc_calls||memcmp(expected,tlc_log,calls*sizeof(*expected))||
       memcmp(tlc_after,tlc_arena,TLC_SIZE)||tlc_globals_differ(after,actual))
        return tlc_report("adapter",n,want,got,calls,expected);
    tlc_globals(before,1);
    return 0;
}
static int tlc_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],regs[3],fill,want[3]={0},got[3]={0},pattern=poison?0xa7a7a7a7u:0x31313131u;
        tlc_build(index,n*37u+11u,a,r,regs,&fill,1,0);
        const uint32_t entry[3]={pattern,pattern,pattern};const uint32_t head=tlc_fs0();
        BYTE before[TLC_GLOBALS],after[TLC_GLOBALS],actual[TLC_GLOBALS],expected_fpu[108],actual_fpu[108];static tlc_record expected[TLC_MAX];
        memcpy(tlc_before,tlc_arena,TLC_SIZE);tlc_globals(before,0);
        tlc_reset();tlc_invoke(mapped+d->rva,a,entry,regs,pattern,want);es2_hook_fpu_save(expected_fpu);
        const unsigned calls=tlc_calls;memcpy(expected,tlc_log,calls*sizeof(*expected));memcpy(tlc_after,tlc_arena,TLC_SIZE);tlc_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("terrain_load poison install");
        memcpy(tlc_arena,tlc_before,TLC_SIZE);tlc_globals(before,1);tlc_reset();tlc_invoke(mapped+d->rva,a,entry,regs,pattern,got);
        es2_hook_fpu_save(actual_fpu);tlc_globals(actual,0);
        if(tlc_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||calls!=tlc_calls||memcmp(expected,tlc_log,calls*sizeof(*expected))||
           memcmp(tlc_after,tlc_arena,TLC_SIZE)||tlc_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||
           memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=tlc_report(fast?"installed fast":"installed preserving",n,want,got,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("terrain_load poison uninstall");tlc_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, pair/trace/arena/globals/FS:0\n",d->name,total);
    return status;
}
/* ---- real callees ---- */
enum { TLC_NESTED=7 };
static unsigned tlc_nested(unsigned index,unsigned nested[TLC_NESTED]){
    unsigned count=0;
#define TLC_N(name) nested[count++]=ES2_HOOK_INDEX_##name
    if(index==ES2_HOOK_INDEX_zone_load){TLC_N(grid_ctor);TLC_N(height_cache_ctor);TLC_N(terrain_materials);TLC_N(terrain_cells);
        TLC_N(cell_mark_set);TLC_N(euler_matrix);TLC_N(rng_next);}
    else if(index==ES2_HOOK_INDEX_grid_ctor){TLC_N(height_cache_ctor);}
    else if(index==ES2_HOOK_INDEX_terrain_materials){TLC_N(terrain_cells);TLC_N(cell_mark_set);TLC_N(rng_next);}
    else if(index==ES2_HOOK_INDEX_terrain_cells){TLC_N(cell_mark_set);TLC_N(rng_next);}
#undef TLC_N
    return count;
}
static int tlc_real_pass(unsigned index){
    unsigned nested[TLC_NESTED];const unsigned count=tlc_nested(index,nested);
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[TLC_NESTED+1]={0};
    BYTE saved[TLC_IDS][5];int active[TLC_IDS];
    if(tlc_patch(saved,active,tlc_real_which(index),1))return 1;
    static tlc_record expected[TLC_MAX];
    const unsigned modes=2u<<count;
    for(unsigned mode=0;mode<modes&&!status;++mode)for(unsigned n=0;n<4096u/modes&&!status;++n){
        uint32_t a[6],r[3],regs[3],fill,want[3]={0},got[3]={0},pattern=n&1?0xa7a7a7a7u:0x31313131u;
        tlc_build(index,n*13u+mode,a,r,regs,&fill,1,1);
        const uint32_t entry[3]={pattern,pattern,pattern};const uint32_t head=tlc_fs0();
        BYTE before[TLC_GLOBALS],after[TLC_GLOBALS],actual[TLC_GLOBALS];
        memcpy(tlc_before,tlc_arena,TLC_SIZE);tlc_globals(before,0);
        es2_hook hk[TLC_NESTED+1];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(mode>>(1+j)&1)?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!nested_install_hook(hk+j,nested[j],&o,function_hook_extension(nested[j])))return fail("terrain_load nested install");}
        tlc_reset();tlc_invoke(mapped+d->rva,a,entry,regs,pattern,want);
        const unsigned calls=tlc_calls;memcpy(expected,tlc_log,calls*sizeof(*expected));
        memcpy(tlc_after,tlc_arena,TLC_SIZE);tlc_globals(after,0);
        o.mode=mode&1?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(hk+TLC_NESTED,d,&o,function_hook_extension(index)))return fail("terrain_load caller install");
        memcpy(tlc_arena,tlc_before,TLC_SIZE);tlc_globals(before,1);tlc_reset();
        tlc_invoke(mapped+d->rva,a,entry,regs,pattern,got);tlc_globals(actual,0);
        if(tlc_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||calls!=tlc_calls||memcmp(expected,tlc_log,calls*sizeof(*expected))||
           memcmp(tlc_after,tlc_arena,TLC_SIZE)||tlc_globals_differ(after,actual)){
            fprintf(stderr,"real mode=%u\n",mode);status=tlc_report("real",n,want,got,calls,expected);
        }
        for(unsigned j=0;j<=TLC_NESTED;++j){if(!hk[j].descriptor)continue;entered[j]+=hk[j].calls;
            if(hk[j].mismatches)status=fail("terrain_load real compare mismatch");
            if(!es2_hook_uninstall(hk+j))return fail("terrain_load real uninstall");}
        tlc_globals(before,1);++total;
    }
    if(tlc_patch(saved,active,0,0))return 1;
    tlc_real=0;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&!entered[TLC_NESTED]){fprintf(stderr,"FAIL: %s never entered\n",d->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, %u nested hooks original/replaced, hook original/replaced, 31/a7 stack, pair, calls, arena, globals and FS:0\n",
        d->name,total,count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
static int terrain_load_cases(unsigned index,unsigned limit){
    current_function=function_hook_descriptor(index)->name;
    BYTE old_globals[TLC_GLOBALS];tlc_globals(old_globals,0);
    const uint32_t which=tlc_which(index);
    BYTE saved[TLC_IDS][5];int active[TLC_IDS];
    if(tlc_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_TERRAIN_LOAD_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);memset(tlc_paths,0,sizeof(tlc_paths));
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3],regs[3],fill;const int exact=limit||!(n&1);
        tlc_build(index,n,a,r,regs,&fill,exact,0);status=tlc_compare(index,n,a,r,regs,fill,exact&&!limit,!limit);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    tlc_globals(old_globals,1);
    if(!limit&&!status)status=tlc_installed_probes(index,64);
    if(tlc_patch(saved,active,which,0))return 1;
    tlc_globals(old_globals,1);
    if(!limit&&!status&&index!=ES2_HOOK_INDEX_cell_mark_set)status=tlc_real_pass(index);
    tlc_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases (stub calls 49630c %u, itoa %u, 492bec %u, 472f14 %u, 479c78 %u, read %u, new %u, 46befc %u, "
        "46c754 %u, euler %u, 472f64 %u, new[] %u, stream_read %u, strchr %u, stricmp %u, dts_load %u, 46c4c4 %u, 496406 %u, dtor %u, "
        "fopen %u, fscanf %u, 49667c %u, rng_next %u, fclose %u, 4952e8 %u, 4952fc %u, 470d74 %u, 495268 %u); "
        "full return pair and ECX, ordered calls with entry registers, slots, contents and context word, arena/globals/FS:0\n",
        function_hook_descriptor(index)->name,cases,tlc_paths[0],tlc_paths[1],tlc_paths[2],tlc_paths[3],tlc_paths[4],tlc_paths[5],
        tlc_paths[6],tlc_paths[7],tlc_paths[8],tlc_paths[9],tlc_paths[10],tlc_paths[11],tlc_paths[12],tlc_paths[13],tlc_paths[14],
        tlc_paths[15],tlc_paths[16],tlc_paths[17],tlc_paths[18],tlc_paths[19],tlc_paths[20],tlc_paths[21],tlc_paths[22],tlc_paths[23],
        tlc_paths[24],tlc_paths[25],tlc_paths[26],tlc_paths[27]);
    return status;
}
