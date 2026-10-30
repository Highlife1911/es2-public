/* base_flatten group: base_flatten 47129c, flood_sum 470fe8, flood_fill 471128,
 * mark_raster 47136c and cell_flag_get 470e00
 * (docs/reverse-engineering/terrain.md, "base flattening and the base-mark
 * raster").
 *
 * Direct cases run the original body (EAX/EDX/ECX at entry from the case, five
 * argument slots, over a stack filled with a case pattern) and the core (with
 * the ECX it reports) against logging stubs for every game callee of the hook
 * under test, patched in the private image: cell_flag_get 470e00, cell_flag_set
 * 470ddc, flood_sum 470fe8, flood_fill 471128, the shading pass 46c2e0 and
 * cell_mark_set 470d74. The floods' own recursion stays original code in the
 * original run (only their callees are stubs), so the stubs play the marks for
 * real: 470e00 answers the mark byte of an existing cell (0 outside the grid),
 * 470ddc stores it; that ends the recursion as in the game and keeps every
 * dereferenced cell in the arena. Compared: the full return pair, ECX, the
 * ordered calls with argument slots (and the bridge's slot count) and entry
 * EAX/EDX/ECX (stack addresses masked), the 64 KiB arena (grid object, two cell
 * arrays, marks, material records, the raster's rows) and the image globals
 * 6B5470h..77h (sum, count) and the word 4D2680h (-l). In odd cases the stubs
 * change what the originals reread (heights, the cell array pointer, the sum
 * and count, new marks; for the scan the shift words and the cell pointer; for
 * the raster [G+100h], [G+104h], [G+0ECh], the row bytes and the read-once
 * fields), add noise above the mark byte, and wild shifts reach the negative
 * and out-of-range cell indices with the stubs answering "unmarked". In even
 * cases (game-like inputs) the adapter is compared too, with the callee counter
 * deltas. base_flatten's IDIV faults (zero count, 80000000h / -1) are probed:
 * original at 471333, core with *fault and the operands, the adapter faulting
 * itself.
 *
 * Then installed 31/a7 poison probes and real-callee passes: the floods over
 * the real cell_flag_get and cell_flag_set, base_flatten over the real
 * cell_flag_get, flood_sum, flood_fill and cell_flag_set (46c2e0 stays a
 * stub), mark_raster over the real cell_mark_set; the nested hooks in all
 * original/replaced combinations, the hook under test original or replaced,
 * against a run without it. cell_flag_get accepts compare and runs the
 * compare probes of compare_probes.h. Identifiers use the prefix bf_/BF_. */
typedef struct bf_record { uint32_t id,slots,args[5],regs[3],eax,edx; } bf_record;
enum {
    BF_SIZE=0x10000, BF_MAX=12288, BF_IDS=6, BF_PATH=65536,
    BF_G=0x100, BF_TYPES=0x300, BF_P=0x400, BF_ROWS=0x440, BF_BYTES=0x500,
    BF_FLAGS=0x800, BF_CELLS=0x1000, BF_CELLS2=0x5000, BF_COMPARE=0x1000
};
#define BF_B ((uint32_t)(uintptr_t)bf_arena)
#define BF_A(o) (BF_B+(uint32_t)(o))
#define BF_BIT(i) ((uint32_t)1<<(i))
static BYTE bf_arena[BF_SIZE],bf_before[BF_SIZE],bf_after[BF_SIZE];
static bf_record bf_log[BF_MAX],bf_expected[BF_MAX];
static unsigned bf_calls,bf_effects,bf_wild,bf_index,bf_fault,bf_paths[BF_IDS];
static uint32_t bf_seed,bf_stream,bf_cells;   /* bf_cells: the cells (and mark bytes) that exist */
static uint8_t bf_path[2][BF_PATH];
static uint32_t bf_depth;                      /* deepest flood path of the core */
__attribute__((used)) static uint32_t bf_ecx;
/* 0 cell_flag_get 470e00, 1 cell_flag_set 470ddc, 2 flood_sum 470fe8, 3
 * flood_fill 471128, 4 the shading pass 46c2e0, 5 cell_mark_set 470d74; the
 * slots each original call site pushes. */
static const uint32_t bf_addresses[BF_IDS]={0x470e00,0x470ddc,0x470fe8,0x471128,0x46c2e0,0x470d74};
static const unsigned bf_counts[BF_IDS]={3,4,3,5,1,3};
static uint32_t bf_next(void){
    uint32_t z=bf_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *bf_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int bf_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static void bf_reset(void){bf_calls=0;bf_stream=bf_seed;}
static void bf_random(unsigned offset,unsigned size){for(unsigned i=0;i<size;++i)bf_arena[offset+i]=(BYTE)bf_next();}
static BYTE *bf_g(void){return bf_arena+BF_G;}
/* The mark index of (x, y) as 470e00 computes it, and whether that cell
 * exists: 0 <= i < 2^(n+m) (signed, as the originals test it), one of the
 * bf_cells allocated, and the marks are the arena's. */
static int bf_cell(uint32_t x,uint32_t y,uint32_t *index){
    const BYTE *G=bf_g();const uint32_t n=get32(G+0x100),m=get32(G+0x104);
    *index=(y<<(n&31u))+x;
    const int32_t limit=(int32_t)(1u<<((n+m)&31u));
    return get32(G+0xf0)==BF_A(BF_FLAGS)&&(int32_t)*index>=0&&(int32_t)*index<limit&&*index<bf_cells;
}
static int bf_flood(void){return bf_index==ES2_HOOK_INDEX_flood_sum||bf_index==ES2_HOOK_INDEX_flood_fill;}
/* The floods' stub effects: a height, the cell array (two of equal size), the
 * sum or count, a new mark or visit bit. */
static void bf_flood_effect(uint32_t pick){
    BYTE *G=bf_g();
    switch(pick>>8&3){
    case 0: bf_host(get32(G+0xec))[16u*(bf_next()%bf_cells)]=(BYTE)bf_next();break;
    case 1: put32(G+0xec,get32(G+0xec)==BF_A(BF_CELLS)?BF_A(BF_CELLS2):BF_A(BF_CELLS));break;
    case 2: put32(mapped+0x2b5470+4*(bf_next()%2),bf_next());break;
    default: bf_arena[BF_FLAGS+bf_next()%bf_cells]|=(BYTE)(pick&0x400?1:bf_index==ES2_HOOK_INDEX_flood_fill?4:2);break;
    }
}
/* The scan's stub effects (base_flatten): the shift words and the cell
 * pointer the row computation rereads, marks and visit bits. */
static void bf_scan_effect(uint32_t pick){
    BYTE *G=bf_g();
    switch(pick>>8&3){
    case 0: put32(G+0x100,(28u+bf_next()%4u)|(pick&0x400?bf_next()&0xffffffe0u:0));break;
    case 1: put32(G+0x104,pick&0x400?bf_next():bf_next()%4u);break;
    case 2: put32(G+0xec,bf_next());break;
    default: bf_arena[BF_FLAGS+bf_next()%bf_cells]|=(BYTE)(pick&0x400?1:2);break;
    }
}
/* The raster's stub effects (odd cases, option -l set): the grid words it
 * rereads per cell, a row byte ahead, and the fields it reads once. */
static void bf_raster_effect(uint32_t pick){
    BYTE *G=bf_g();
    switch(pick>>8&7){
    case 0: put32(G+0x100,bf_next()%6u|(pick&0x800?bf_next()&0xffffffe0u:0));break;
    case 1: put32(G+0x104,bf_next()%6u);break;
    case 2: put32(G+0xec,bf_next());break;
    case 3: case 4: bf_arena[BF_BYTES+bf_next()%0x100u]^=1u;break;
    case 5: put32(G+0x108,bf_next());break;
    case 6: put32(G+0x121,bf_next());break;
    default: put32(bf_arena+BF_TYPES+4+8*(bf_next()%16u),bf_next());break;
    }
}
/* What the stubs answer and change. */
static uint32_t bf_answer(unsigned id,const uint32_t *a,uint32_t eax,uint32_t pick){
    const int effect=bf_effects&&pick%4==0;
    switch(id){
    case 0: {   /* cell_flag_get: the mark byte of an existing cell */
        uint32_t i,v;const int exists=bf_cell(a[1],a[2],&i);
        if(bf_flood())v=!bf_wild&&exists?bf_arena[BF_FLAGS+i]:0u;
        else v=exists?bf_arena[BF_FLAGS+i]:bf_next()&0xffu;
        if(bf_effects&&pick&0x10000)v|=bf_next()&0xffffff00u;
        if(effect){if(bf_flood())bf_flood_effect(pick);else bf_scan_effect(pick);}
        return v;
    }
    case 1: {   /* cell_flag_set: the store */
        uint32_t i;if(bf_cell(a[1],a[2],&i))bf_arena[BF_FLAGS+i]=(BYTE)a[3];
        if(effect)bf_flood_effect(pick);
        return eax;
    }
    case 2: {   /* flood_sum: the sum and count base_flatten divides */
        uint32_t sum=bf_next(),count=pick&0x20000?bf_next():1u+bf_next()%9u;
        if(pick&0x40000)sum%=4096u;
        if(!count)count=1;
        if(count==0xffffffffu&&sum==0x80000000u)sum=0;
        if(bf_fault==1)count=0;
        if(bf_fault==2){sum=0x80000000u;count=0xffffffffu;}
        put32(mapped+0x2b5470,sum);put32(mapped+0x2b5474,count);
        if(effect)bf_scan_effect(pick);
        return eax;
    }
    case 3:     /* flood_fill */
        if(effect)bf_scan_effect(pick);
        return eax;
    case 5:     /* cell_mark_set */
        if(effect)bf_raster_effect(pick);
        return eax;
    }
    return eax;
}
__attribute__((used,noinline)) static uint64_t __cdecl bf_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(bf_calls==BF_MAX){fail("base_flatten call overflow");ExitProcess(1);}
    bf_record *v=bf_log+bf_calls++;memset(v,0,sizeof(*v));v->id=id;
    v->slots=weapons_bridge_slots(a,bf_counts[id]);
    for(unsigned k=0;k<bf_counts[id];++k)v->args[k]=bf_stack(a[k])?0:a[k];
    for(unsigned j=0;j<3;++j)v->regs[j]=bf_stack(regs[j])?0:regs[j];
    uint32_t eax=bf_next(),edx=bf_next(),pick=bf_next();bf_ecx=bf_next();
    eax=bf_answer(id,a,eax,pick);
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define BF_STUB(n) __attribute__((naked)) static void bf_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _bf_stub_impl\n\taddl $24,%esp\n\tmovl _bf_ecx,%ecx\n\tret\n\t");}
BF_STUB(0) BF_STUB(1) BF_STUB(2) BF_STUB(3) BF_STUB(4) BF_STUB(5)
#undef BF_STUB
static void *bf_stubs[BF_IDS]={bf_stub_0,bf_stub_1,bf_stub_2,bf_stub_3,bf_stub_4,bf_stub_5};
static int bf_patch(BYTE saved[BF_IDS][5],int active[BF_IDS],uint32_t which,int on){
    for(unsigned i=0;i<BF_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(bf_addresses[i]-0x400000u,bf_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(bf_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
/* Direct callees of each hook (bit i = bf_addresses[i]). */
static uint32_t bf_which(unsigned index){
    if(index==ES2_HOOK_INDEX_base_flatten)return BF_BIT(0)|BF_BIT(2)|BF_BIT(3)|BF_BIT(4);
    if(index==ES2_HOOK_INDEX_flood_sum||index==ES2_HOOK_INDEX_flood_fill)return BF_BIT(0)|BF_BIT(1);
    if(index==ES2_HOOK_INDEX_mark_raster)return BF_BIT(5);
    return 0;
}
/* The image globals: the sum and count 6B5470h/6B5474h, the word 4D2680h. */
static const struct { uint32_t rva,size; } bf_ranges[]={{0x2b5470,8},{0xd2680,2}};
enum { BF_GLOBALS=8+2 };
static void bf_globals(BYTE *out,int restore){
    for(unsigned i=0;i<sizeof(bf_ranges)/sizeof(bf_ranges[0]);++i){
        if(restore)memcpy(mapped+bf_ranges[i].rva,out,bf_ranges[i].size);else memcpy(out,mapped+bf_ranges[i].rva,bf_ranges[i].size);
        out+=bf_ranges[i].size;
    }
}
static int bf_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<BF_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
/* The grid for the floods and the scan: 2^n columns, 2^m rows (n + m <= 10),
 * marks with bit 0 at a chosen density (bits 1/2 rarely preset, bits 3..7
 * random), random heights in both cell arrays. The floods' fill needs
 * m >= n (a write-through neighbour below the last row would be the null
 * cell); the sum also runs with fewer rows than columns. */
static void bf_grid(int real,int big){
    BYTE *G=bf_g();
    uint32_t n=bf_next()%5u;if(big)n=5;
    uint32_t m=bf_index==ES2_HOOK_INDEX_flood_sum&&!real?bf_next()%6u:n+bf_next()%2u;
    while(n+m>10u)--m;
    bf_cells=1u<<(n+m);
    put32(G+0x100,n);put32(G+0x104,m);put32(G+0xec,BF_A(BF_CELLS));put32(G+0xf0,BF_A(BF_FLAGS));
    const unsigned density=(unsigned[]){2,5,8,14}[bf_next()%4];
    for(uint32_t i=0;i<bf_cells;++i){
        BYTE v=(BYTE)(bf_next()&0xf8u);
        if(bf_next()%16u<density)v|=1u;
        if(!real&&bf_next()%16u==0)v|=(BYTE)(bf_next()%2?2:4);
        bf_arena[BF_FLAGS+i]=v;
    }
}
/* A start cell: mostly a marked one, sometimes any, sometimes outside. */
static void bf_start(uint32_t *x,uint32_t *y,int outside){
    const uint32_t n=get32(bf_g()+0x100),size=1u<<n;
    *x=bf_next()%size;*y=bf_next()%size;
    for(unsigned k=0;k<4&&bf_next()%4;++k){
        const uint32_t i=bf_next()%bf_cells;
        if((bf_arena[BF_FLAGS+i]&1)&&(i>>n)<size){*x=i&(size-1u);*y=i>>n;break;}
    }
    if(outside)switch(bf_next()%4){
    case 0: *x=size;break;
    case 1: *y=0xffffffffu;break;
    case 2: *x=0xffffffffu;break;
    default: *y=size+bf_next()%3u;break;
    }
}
/* The raster's inputs. exact: every cell of the box exists (0 <= index <
 * 2^(n+m)), the word -l 0 or 1; otherwise wild values with -l set (the
 * original stores at [cell+0Fh] only without it). */
static void bf_raster(uint32_t a[5],int exact){
    BYTE *G=bf_g();
    for(;;){
        const uint32_t n=bf_next()%6u,m=bf_next()%6u,s=bf_next()%3u;
        bf_cells=1u<<(n+m);
        put32(G+0x100,n);put32(G+0x104,m);put32(G+0xec,BF_A(BF_CELLS));put32(G+0xf0,BF_A(BF_FLAGS));put32(G+0x108,13u+s);
        int32_t count=1+(int32_t)(bf_next()%8u);
        if(!exact&&bf_next()%4==0)count=(int32_t)(bf_next()%4u)-2;
        uint32_t k=bf_next()%16u;
        put32(G+0x121,BF_A(BF_TYPES));
        uint32_t limit=n<m?n:m;if(limit>8u-s)limit=8u-s;
        const uint32_t B=bf_next()%(limit+1u),t=8u-s-B,A=21u-t;
        put32(bf_arena+BF_TYPES+8*k+4,t);
        const uint32_t span=count>0?(uint32_t)(count-1)>>s:0;
        /* bx = q << B with bx + span < 2^n; by = ((r + 1) << B) - 1 with
         * by - span >= 0 and by < 2^m */
        const int32_t room=(int32_t)(1u<<n)-1-(int32_t)span;
        const uint32_t low=(span+1u+(1u<<B)-1u)>>B,high=(1u<<m)>>B;
        int possible=room>=0&&low<=high&&high>=1u;
        uint32_t q=0,r1=1;
        if(possible){q=bf_next()%(((uint32_t)room>>B)+1u);r1=low<1u?1u:low;r1+=bf_next()%(high-r1+1u);}
        if(exact&&!possible)continue;   /* count 1 with B = 0 always fits */
        put32(bf_arena+BF_P,(q<<A)|(bf_next()&((1u<<A)-1u)));
        put32(bf_arena+BF_P+4,((r1-1u)<<A)|(bf_next()&((1u<<A)-1u)));
        put16(mapped+0xd2680,(uint16_t)(exact&&possible?bf_next()%2u:1u+bf_next()%0xfffeu));
        if(!exact) {
            if(bf_next()%2)put32(G+0x108,bf_next());
            if(bf_next()%2){k=bf_next();put32(G+0x121,BF_A(BF_TYPES)+8u*(bf_next()%16u)-(k<<3));}
            if(bf_next()%2)put32(bf_arena+BF_TYPES+4+8*(bf_next()%16u),bf_next()%32u);
            if(bf_next()%2){put32(bf_arena+BF_P,bf_next());put32(bf_arena+BF_P+4,bf_next());}
            if(bf_next()%4==0)put32(G+0x100,bf_next());
        }
        for(unsigned j=0;j<16;++j){
            put32(bf_arena+BF_ROWS+4*j,BF_A(BF_BYTES+16*j));
            for(unsigned i=0;i<16;++i)bf_arena[BF_BYTES+16*j+i]=(BYTE)(bf_next()%2?0:bf_next()%2?1:bf_next());
        }
        a[0]=BF_A(BF_G);a[1]=BF_A(BF_P);a[2]=k;a[3]=BF_A(BF_ROWS);a[4]=(uint32_t)count;
        return;
    }
}
/* The case data. exact: game-like inputs; real: data for the real-callee
 * passes. */
static void bf_build(unsigned index,unsigned n,uint32_t a[5],uint32_t r[3],uint32_t *fill,int exact,int real){
    bf_seed=random32();bf_stream=bf_seed^n;bf_effects=!real&&!exact;bf_wild=0;bf_index=index;bf_fault=0;
    for(unsigned i=0;i<3;++i)r[i]=bf_next();
    bf_random(0,BF_SIZE);memset(a,0,20);
    *fill=!exact&&!real&&bf_next()%2?bf_next():0;
    put32(mapped+0x2b5470,bf_next());put32(mapped+0x2b5474,bf_next());put16(mapped+0xd2680,0);
    BYTE *G=bf_g();
    a[0]=BF_A(BF_G);
    if(index==ES2_HOOK_INDEX_cell_flag_get){
        /* any shift; the marks pointer chosen so that the byte read lies in
         * the arena's mark block */
        const uint32_t c=exact?bf_next()%9u:bf_next()%32u|(bf_next()%4==0?bf_next()&0xffffffe0u:0);
        const uint32_t x=exact?bf_next()%(1u<<c):bf_next()%2?bf_next():bf_next()%64u;
        const uint32_t y=exact?bf_next()%(1u<<c):bf_next()%2?bf_next():bf_next()%64u;
        put32(G+0x100,c);put32(G+0xf0,BF_A(BF_FLAGS)+bf_next()%0x400u-((y<<(c&31u))+x));
        a[1]=x;a[2]=y;
    } else if(index==ES2_HOOK_INDEX_mark_raster){
        bf_raster(a,exact||real);
    } else {
        bf_grid(real,!exact&&!real&&bf_next()%16==0);
        if(index==ES2_HOOK_INDEX_base_flatten){
            if(!exact&&!real&&bf_next()%8==0)
                put32(G+0x100,(bf_next()%2?31u:get32(G+0x100))|(bf_next()&0xffffffe0u));
            if(!exact&&!real&&bf_next()%8==0)put32(G+0x104,bf_next());
        } else {
            uint32_t x,y;bf_start(&x,&y,bf_next()%8==0);
            a[1]=x;a[2]=y;
            if(index==ES2_HOOK_INDEX_flood_fill){a[3]=bf_next();a[4]=bf_next()%2?0:real||exact?1:bf_next();}
            if(!exact&&!real&&bf_next()%8==0){
                /* wild shifts: a top-level call only (the stubs answer
                 * "unmarked"), no write-through */
                bf_wild=1;
                const uint32_t c=16u+bf_next()%15u;
                put32(G+0x100,bf_next()%2?c:bf_next());put32(G+0x104,bf_next()%2?bf_next()%4u:bf_next());
                const uint32_t size=1u<<(get32(G+0x100)&31u);
                a[1]=bf_next()%2&&(int32_t)size>0?bf_next()%size:bf_next();
                a[2]=bf_next()%2&&(int32_t)size>0?bf_next()%size:bf_next();
                a[4]=0;
            }
        }
    }
    bf_stream=bf_seed;   /* the stubs' stream restarts here in every run */
}
/* Calls target with five argument slots, EAX/EDX/ECX = r over 3,840 bytes
 * of stack filled with `fill`; out receives EAX, EDX and ECX after the
 * call. */
__attribute__((naked,noinline)) static void bf_invoke(void *target,const uint32_t *a,const uint32_t *r,uint32_t fill,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 20(%ebp),%eax\n\tmovl $1024,%ecx\n\tcld\n\trep stosl\n\taddl $3840,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 16(%esi)\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%eax\n\tmovl 4(%eax),%edx\n\tmovl 8(%eax),%ecx\n\tmovl (%eax),%eax\n\tcall *8(%ebp)\n\t"
        "movl 24(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* The core with the adapter's graph (fault and divide for base_flatten). */
static uint64_t bf_core(unsigned index,const uint32_t *a,const uint32_t *r,uint32_t *ecx,int *fault,uint32_t divide[2]){
    const es2_guest_memory *m=&math_native_memory;es2_weapon_graph g=weapons_graph(mapped,NULL,index);
    *fault=0;
    if(index==ES2_HOOK_INDEX_base_flatten)return es2_base_flatten_registers(m,&g,a[0],r,divide,fault,ecx);
    if(index==ES2_HOOK_INDEX_flood_sum||index==ES2_HOOK_INDEX_flood_fill){
        es2_flood_stack s={bf_path[index==ES2_HOOK_INDEX_flood_fill],BF_PATH,0,0};
        const uint64_t p=index==ES2_HOOK_INDEX_flood_sum?es2_flood_sum_registers(m,&g,a[0],a[1],a[2],r,&s,ecx):
            es2_flood_fill_registers(m,&g,a[0],a[1],a[2],a[3],a[4],r,&s,ecx);
        if(s.overflow){fail("base_flatten flood path overflow");ExitProcess(1);}
        if(s.depth_max>bf_depth)bf_depth=s.depth_max;
        return p;
    }
    if(index==ES2_HOOK_INDEX_mark_raster)return es2_mark_raster_registers(m,&g,a[0],a[1],a[2],a[3],a[4],r,ecx);
    return es2_cell_flag_get_registers(m,a[0],a[1],a[2],ecx);
}
static int bf_report(const char *what,unsigned n,const uint32_t want[3],const uint32_t got[3],unsigned calls,const bf_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x calls %u/%u\n",current_function,n,what,
        want[0],got[0],want[1],got[1],want[2],got[2],calls,bf_calls);
    for(unsigned j=0;j<calls&&j<bf_calls&&expected;++j)if(memcmp(expected+j,bf_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(bf_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<BF_SIZE;++j)if(bf_after[j]!=bf_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,bf_after[j],bf_arena[j]);break;}
    return 1;
}
/* The callee counter key of a stub id. */
static unsigned bf_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<bf_calls;++j)if(c->event==ES2_COLLIDE_CALL&&c->key==bf_addresses[bf_log[j].id])++hits;
    return hits;
}
static int bf_same(unsigned calls,const BYTE *after,const BYTE *actual){
    return calls==bf_calls&&!memcmp(bf_expected,bf_log,calls*sizeof(*bf_expected))&&!memcmp(bf_after,bf_arena,BF_SIZE)&&
        !bf_globals_differ(after,actual);
}
/* One direct case: the original, the core and (exact inputs) the adapter. */
static int bf_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r,uint32_t fill,int exact,int check_ecx){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[BF_GLOBALS],after[BF_GLOBALS],actual[BF_GLOBALS];uint32_t want[3],got[3],divide[2];int fault=0;
    memcpy(bf_before,bf_arena,BF_SIZE);bf_globals(before,0);
    bf_reset();bf_invoke(mapped+d->rva,a,r,fill,want);const unsigned calls=bf_calls;memcpy(bf_expected,bf_log,calls*sizeof(*bf_expected));
    memcpy(bf_after,bf_arena,BF_SIZE);bf_globals(after,0);
    for(unsigned j=0;j<calls;++j)++bf_paths[bf_expected[j].id];
    memcpy(bf_arena,bf_before,BF_SIZE);bf_globals(before,1);bf_reset();
    got[2]=~want[2];
    uint64_t pair=bf_core(index,a,r,&got[2],&fault,divide);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);bf_globals(actual,0);
    if(fault||want[0]!=got[0]||want[1]!=got[1]||(check_ecx&&want[2]!=got[2])||!bf_same(calls,after,actual))
        return bf_report("core",n,want,got,calls,bf_expected);
    if(!exact){bf_globals(before,1);return 0;}
    /* the adapter (the framework's entry: five argument slots) */
    memcpy(bf_arena,bf_before,BF_SIZE);bf_globals(before,1);bf_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];
    if(count>8)return fail("base_flatten counter table too long");
    for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair=x->replacement_context(a,d->user,r);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);got[2]=want[2];
    bf_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=bf_expected_hits(c+i))return fail("base_flatten callee counter delta");
    if(want[0]!=got[0]||want[1]!=got[1]||!bf_same(calls,after,actual))return bf_report("adapter",n,want,got,calls,bf_expected);
    bf_globals(before,1);
    return 0;
}
/* base_flatten's IDIV faults: a zero count, then 80000000h / -1. The
 * original faults at 471333; the core stops with *fault and the operands
 * after the same calls and stores; the adapter faults itself. */
static int bf_divide_probe(unsigned index){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    for(unsigned kind=1;kind<=2;++kind)for(unsigned n=0;n<16;++n){
        uint32_t a[5],r[3],fill,want[3]={0},got[3]={0},divide[2]={0,0};int fault=0;
        BYTE before[BF_GLOBALS],after[BF_GLOBALS],actual[BF_GLOBALS];
        bf_build(index,n*29u+kind,a,r,&fill,1,0);bf_fault=kind;
        bf_arena[BF_FLAGS]=(BYTE)((bf_arena[BF_FLAGS]|1u)&0xf9u);   /* (0,0) starts a region */
        memcpy(bf_before,bf_arena,BF_SIZE);bf_globals(before,0);
        bf_reset();expecting_divide=1;divide_site=0;
        if(!setjmp(divide_return)){bf_invoke(mapped+d->rva,a,r,fill,want);expecting_divide=0;return fail("base_flatten IDIV did not fault");}
        if(divide_site!=0x471333u)return fail("base_flatten IDIV fault site");
        const unsigned calls=bf_calls;memcpy(bf_expected,bf_log,calls*sizeof(*bf_expected));
        memcpy(bf_after,bf_arena,BF_SIZE);bf_globals(after,0);
        const uint32_t sum=get32(mapped+0x2b5470),count=get32(mapped+0x2b5474);
        memcpy(bf_arena,bf_before,BF_SIZE);bf_globals(before,1);bf_reset();
        (void)bf_core(index,a,r,&got[2],&fault,divide);bf_globals(actual,0);
        if(!fault||divide[0]!=sum||divide[1]!=count||!bf_same(calls,after,actual))return bf_report("core fault",n,want,got,calls,bf_expected);
        memcpy(bf_arena,bf_before,BF_SIZE);bf_globals(before,1);bf_reset();expecting_divide=1;divide_site=0;
        if(!setjmp(divide_return)){(void)x->replacement_context(a,d->user,r);expecting_divide=0;return fail("base_flatten adapter IDIV did not fault");}
        bf_globals(actual,0);
        if(!bf_same(calls,after,actual))return bf_report("adapter fault",n,want,got,calls,bf_expected);
        bf_globals(before,1);
    }
    printf("PASS: %s zero count and 80000000h / -1: original IDIV fault at 471333, core fault with the operands, adapter fault (32 cases)\n",d->name);
    return 0;
}
static int bf_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[5],r[3],fill,want[3]={0},got[3]={0},pattern=poison?0xa7a7a7a7u:0x31313131u;
        bf_build(index,n*37u+11u,a,r,&fill,1,0);
        const uint32_t entry[3]={pattern,pattern,pattern};
        BYTE before[BF_GLOBALS],after[BF_GLOBALS],actual[BF_GLOBALS],expected_fpu[108],actual_fpu[108];
        memcpy(bf_before,bf_arena,BF_SIZE);bf_globals(before,0);
        bf_reset();bf_invoke(mapped+d->rva,a,entry,pattern,want);es2_hook_fpu_save(expected_fpu);
        const unsigned calls=bf_calls;memcpy(bf_expected,bf_log,calls*sizeof(*bf_expected));memcpy(bf_after,bf_arena,BF_SIZE);bf_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("base_flatten poison install");
        memcpy(bf_arena,bf_before,BF_SIZE);bf_globals(before,1);bf_reset();bf_invoke(mapped+d->rva,a,entry,pattern,got);
        es2_hook_fpu_save(actual_fpu);bf_globals(actual,0);
        if(want[0]!=got[0]||want[1]!=got[1]||!bf_same(calls,after,actual)||memcmp(expected_fpu,actual_fpu,12)||
           memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=bf_report(fast?"installed fast":"installed preserving",n,want,got,calls,bf_expected);
        if(!es2_hook_uninstall(&h))return fail("base_flatten poison uninstall");bf_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, pair/trace/arena/globals\n",d->name,total);
    return status;
}
/* ---- real callees ---- */
enum { BF_NESTED=4 };
static unsigned bf_nested(unsigned index,unsigned nested[BF_NESTED]){
    unsigned count=0;
#define BF_N(name) nested[count++]=ES2_HOOK_INDEX_##name
    if(index==ES2_HOOK_INDEX_base_flatten){BF_N(cell_flag_get);BF_N(flood_sum);BF_N(flood_fill);BF_N(cell_flag_set);}
    else if(index==ES2_HOOK_INDEX_flood_sum||index==ES2_HOOK_INDEX_flood_fill){BF_N(cell_flag_get);BF_N(cell_flag_set);}
    else if(index==ES2_HOOK_INDEX_mark_raster){BF_N(cell_mark_set);}
#undef BF_N
    return count;
}
static int bf_real_pass(unsigned index){
    unsigned nested[BF_NESTED];const unsigned count=bf_nested(index,nested);
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[BF_NESTED+1]={0};
    BYTE saved[BF_IDS][5];int active[BF_IDS];
    /* the shading pass (H3) stays a stub under base_flatten */
    if(bf_patch(saved,active,index==ES2_HOOK_INDEX_base_flatten?BF_BIT(4):0,1))return 1;
    const unsigned modes=2u<<count;
    for(unsigned mode=0;mode<modes&&!status;++mode)for(unsigned n=0;n<4096u/modes&&!status;++n){
        uint32_t a[5],r[3],fill,want[3]={0},got[3]={0},pattern=n&1?0xa7a7a7a7u:0x31313131u;
        bf_build(index,n*13u+mode,a,r,&fill,1,1);
        const uint32_t entry[3]={pattern,pattern,pattern};
        BYTE before[BF_GLOBALS],after[BF_GLOBALS],actual[BF_GLOBALS];
        memcpy(bf_before,bf_arena,BF_SIZE);bf_globals(before,0);
        es2_hook hk[BF_NESTED+1];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(mode>>(1+j)&1)?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,function_hook_extension(nested[j])))return fail("base_flatten nested install");}
        bf_reset();bf_invoke(mapped+d->rva,a,entry,pattern,want);
        const unsigned calls=bf_calls;memcpy(bf_expected,bf_log,calls*sizeof(*bf_expected));
        memcpy(bf_after,bf_arena,BF_SIZE);bf_globals(after,0);
        o.mode=mode&1?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(hk+BF_NESTED,d,&o,function_hook_extension(index)))return fail("base_flatten caller install");
        memcpy(bf_arena,bf_before,BF_SIZE);bf_globals(before,1);bf_reset();
        bf_invoke(mapped+d->rva,a,entry,pattern,got);bf_globals(actual,0);
        if(want[0]!=got[0]||want[1]!=got[1]||!bf_same(calls,after,actual)){
            fprintf(stderr,"real mode=%u\n",mode);status=bf_report("real",n,want,got,calls,bf_expected);
        }
        for(unsigned j=0;j<=BF_NESTED;++j){if(!hk[j].descriptor)continue;entered[j]+=hk[j].calls;
            if(hk[j].mismatches)status=fail("base_flatten real compare mismatch");
            if(!es2_hook_uninstall(hk+j))return fail("base_flatten real uninstall");}
        bf_globals(before,1);++total;
    }
    if(bf_patch(saved,active,0,0))return 1;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&!entered[BF_NESTED]){fprintf(stderr,"FAIL: %s never entered\n",d->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, %u nested hooks original/replaced, hook original/replaced, 31/a7 stack, pair, calls, arena, globals\n",
        d->name,total,count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
/* Compare probes (cell_flag_get): game-like reads; the state is the grid
 * object, the marks and the globals. */
static void bf_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    uint32_t fill;bf_build(index,n,a,r,&fill,1,1);
}
static void bf_compare_state(BYTE *s,int restore){
    if(restore){memcpy(bf_arena,s,BF_COMPARE);bf_globals(s+BF_COMPARE,1);}else{memcpy(s,bf_arena,BF_COMPARE);bf_globals(s+BF_COMPARE,0);}
}
static int base_flatten_cases(unsigned index,unsigned limit){
    current_function=function_hook_descriptor(index)->name;
    BYTE old_globals[BF_GLOBALS];bf_globals(old_globals,0);
    if(limit&&compare_probe_installed(index)){
        const int status=compare_probe_run(index,limit,bf_compare_build,bf_compare_state,BF_COMPARE+BF_GLOBALS);
        bf_globals(old_globals,1);return status;
    }
    const uint32_t which=bf_which(index);
    BYTE saved[BF_IDS][5];int active[BF_IDS];
    if(bf_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_BASE_FLATTEN_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);memset(bf_paths,0,sizeof(bf_paths));bf_depth=0;
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[5],r[3],fill;const int exact=limit||!(n&1);
        bf_build(index,n,a,r,&fill,exact,0);status=bf_compare(index,n,a,r,fill,exact&&!limit,!limit);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    bf_globals(old_globals,1);
    if(!limit&&!status&&index==ES2_HOOK_INDEX_base_flatten)status=bf_divide_probe(index);
    bf_globals(old_globals,1);
    if(!limit&&!status)status=bf_installed_probes(index,64);
    if(bf_patch(saved,active,which,0))return 1;
    bf_globals(old_globals,1);
    if(!limit&&!status&&bf_nested(index,(unsigned[BF_NESTED]){0}))status=bf_real_pass(index);
    bf_globals(old_globals,1);
    if(!limit&&!status&&compare_probe_capable(index))status=compare_probe_run(index,0,bf_compare_build,bf_compare_state,BF_COMPARE+BF_GLOBALS);
    bf_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases (stub calls 470e00 %u, 470ddc %u, 470fe8 %u, 471128 %u, 46c2e0 %u, 470d74 %u; "
        "deepest core flood path %u); full return pair and ECX, ordered calls with entry registers and slots, arena/globals\n",
        function_hook_descriptor(index)->name,cases,bf_paths[0],bf_paths[1],bf_paths[2],bf_paths[3],bf_paths[4],bf_paths[5],
        (unsigned)bf_depth);
    return status;
}
