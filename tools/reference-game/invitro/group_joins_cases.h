/* group_joins group: mech_join 417aa8, flyer_join 421ee8, building_join 405c3c,
 * footprint_marks 470ed4 and cell_flag_set 470ddc
 * (docs/reverse-engineering/mission_load.md, "Group joins and the building
 * footprint").
 *
 * Direct cases run the original body (EAX/EDX/ECX at entry from the case,
 * four argument slots, over a stack filled with a case pattern) and the core
 * (with the ECX it reports) against logging stubs for every game callee of
 * the hook under test: route_point 423b0c, terrain_height 46e180, dist_gate
 * 49288c, cell_flag_set 470ddc, footprint_marks 470ed4 (patched in the
 * private image) and the virtual slots +78h and +10h (stubs in two arena
 * vtables). Stack addresses in logged registers and slots are masked (the
 * original's frame and the core's scratch differ), and the 12-byte vector a
 * stub receives is logged by content. Compared: the full return pair, ECX,
 * the ordered calls with argument slots (and the bridge's slot count), entry
 * EAX/EDX/ECX, the arena and the image globals (the terrain pointer, the
 * base layouts, the formation tables and their static buffers). The stubs
 * change what the originals reread (the vector after terrain_height, the
 * slot +78h and footprint_marks, the vtable pointer, the type record and
 * its words, the terrain pointer, the group's layout word, the terrain's
 * shift [T+108h] after dist_gate and cell_flag_set), so the order of reads
 * and writes is observable. In even cases the adapter is compared as well,
 * with the callee counter deltas.
 *
 * Then installed 31/a7 poison probes and real-callee passes: the joins over
 * the real route_point, terrain_height (a real 8 x 8 grid), the formation
 * slots formation_mech/formation_flyer/formation_base (their tables,
 * leader and static buffers in the arena; euler_matrix and rotate_2d run
 * as image code), the building join also over shape_word, footprint_marks,
 * dist_gate and cell_flag_set (cell flags in the arena); footprint_marks
 * over the real dist_gate and cell_flag_set; the nested hooks in all
 * original/replaced combinations, the hook under test original or
 * replaced, against a run without it. Identifiers use the prefix gj_/GJ_. */
typedef struct gj_record { uint32_t id,slots,args[4],regs[3],extra[3],eax,edx; } gj_record;
enum {
    GJ_SIZE=0x4000, GJ_MAX=160, GJ_IDS=9,
    GJ_OBJ=0x100, GJ_GROUP=0x400, GJ_VT=0x480, GJ_VT2=0x4c0, GJ_TYPE=0x500, GJ_TYPE2=0x560, GJ_POINT=0x5c0, GJ_Q=0x5e0,
    GJ_LAY=0x640, GJ_WORDS=0x800, GJ_TER=0x900, GJ_FLAGS=0xa40, GJ_HEIGHTS=0xb00, GJ_P=0xf40, GJ_ROUTE=0xf60, GJ_RPTS=0xf80,
    GJ_RPOINTS=0xfc0, GJ_MEMBERS=0x1040, GJ_LEADER=0x1100, GJ_FTAB=0x1400, GJ_SHAPEI=0x1600, GJ_CELLS=0x2000
};
#define GJ_B ((uint32_t)(uintptr_t)gj_arena)
#define GJ_A(o) (GJ_B+(uint32_t)(o))
#define GJ_BIT(i) ((uint32_t)1<<(i))
static BYTE gj_arena[GJ_SIZE],gj_before[GJ_SIZE],gj_after[GJ_SIZE];
static gj_record gj_log[GJ_MAX];
static unsigned gj_calls,gj_effects,gj_index,gj_paths[GJ_IDS];
static uint32_t gj_seed,gj_stream,gj_obj;
__attribute__((used)) static uint32_t gj_ecx;
/* 0 route_point 423b0c, 1 terrain_height 46e180, 2 dist_gate 49288c, 3
 * cell_flag_set 470ddc, 4 footprint_marks 470ed4 (patched); 5/7 the slot
 * +78h and 6/8 the slot +10h of the first/second arena vtable. The slots
 * each original call site pushes. */
static const uint32_t gj_addresses[5]={0x423b0c,0x46e180,0x49288c,0x470ddc,0x470ed4};
static const unsigned gj_counts[GJ_IDS]={2,2,2,4,3,2,1,2,1};
static uint32_t gj_next(void){
    uint32_t z=gj_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *gj_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int gj_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static void gj_reset(void){gj_calls=0;gj_stream=gj_seed;}
static void gj_random(unsigned offset,unsigned size){for(unsigned i=0;i<size;++i)gj_arena[offset+i]=(BYTE)gj_next();}
/* What the stubs answer and change. */
static uint32_t gj_answer(unsigned id,const uint32_t *a,uint32_t eax,uint32_t pick){
    const int effect=gj_effects&&pick%4==0;
    const unsigned what=pick>>8&3;
    switch(id){
    case 0:   /* route_point: a point in the arena */
        if(effect)switch(what){
        case 0:put32(mapped+0xa0be0,gj_next());break;
        case 1:put32(gj_host(gj_obj+0x1f2),GJ_A(GJ_TYPE2));break;
        case 2:put32(gj_host(gj_obj),GJ_A(GJ_VT2));break;
        default:put16(gj_arena+GJ_GROUP+2,(uint16_t)(gj_next()%6));break;
        }
        return GJ_A(GJ_Q);
    case 1:case 4:case 5:case 7:   /* terrain_height, footprint_marks, the slot +78h: the vector, the vtable, type, terrain */
        if(effect)switch(what){
        case 0:put32(gj_host(a[1]+4*(gj_next()%3)),gj_next());break;
        case 1:put32(gj_host(gj_obj),GJ_A(GJ_VT2));break;
        case 2:put32(gj_host(gj_obj+0x1f2),GJ_A(GJ_TYPE2));break;
        default:if(id==1)put16(gj_arena+GJ_TYPE+0x50,(uint16_t)(gj_next()%2));else put32(mapped+0xa0be0,gj_next());break;
        }
        return id==1?gj_next()%5==0?0xffff8000u:gj_next():eax;
    case 6:case 8:   /* shape_word: the terrain pointer */
        if(effect)put32(mapped+0xa0be0,gj_next());
        return pick&0x1000?gj_next():gj_next()%300u;
    case 2:   /* dist_gate: about r; the terrain shift */
        if(effect)put32(gj_arena+GJ_TER+0x108,gj_next()%13u|(pick&0x400?gj_next()&0xffffffe0u:0));
        return pick&0x800?gj_next():get32(gj_arena+GJ_P+0xc)+gj_next()%5u-2u;
    case 3:   /* cell_flag_set: the terrain shift */
        if(effect)put32(gj_arena+GJ_TER+0x108,gj_next()%13u);
        return eax;
    }
    return eax;
}
__attribute__((used,noinline)) static uint64_t __cdecl gj_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(gj_calls==GJ_MAX){fail("group_joins call overflow");ExitProcess(1);}
    gj_record *v=gj_log+gj_calls++;memset(v,0,sizeof(*v));v->id=id;
    v->slots=weapons_bridge_slots(a,gj_counts[id]);
    for(unsigned k=0;k<gj_counts[id];++k)v->args[k]=gj_stack(a[k])?0:a[k];
    for(unsigned j=0;j<3;++j)v->regs[j]=gj_stack(regs[j])?0:regs[j];
    const uint32_t vector=id==2?a[0]:id==1||id==4||id==5||id==7?a[1]:0;   /* the vector, by content */
    if(vector)for(unsigned j=0;j<3;++j)v->extra[j]=get32(gj_host(vector+4*j));
    uint32_t eax=gj_next(),edx=gj_next(),pick=gj_next();gj_ecx=gj_next();
    eax=gj_answer(id,a,eax,pick);
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define GJ_STUB(n) __attribute__((naked)) static void gj_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _gj_stub_impl\n\taddl $24,%esp\n\tmovl _gj_ecx,%ecx\n\tret\n\t");}
GJ_STUB(0) GJ_STUB(1) GJ_STUB(2) GJ_STUB(3) GJ_STUB(4) GJ_STUB(5) GJ_STUB(6) GJ_STUB(7) GJ_STUB(8)
#undef GJ_STUB
static void *gj_stubs[GJ_IDS]={gj_stub_0,gj_stub_1,gj_stub_2,gj_stub_3,gj_stub_4,gj_stub_5,gj_stub_6,gj_stub_7,gj_stub_8};
static int gj_patch(BYTE saved[5][5],int active[5],uint32_t which,int on){
    for(unsigned i=0;i<5;++i){
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(gj_addresses[i]-0x400000u,gj_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(gj_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
/* Direct callees of each hook (bit i = gj_addresses[i]); the slots are
 * reached through the arena vtables. */
static uint32_t gj_which(unsigned index){
    if(index==ES2_HOOK_INDEX_mech_join)return GJ_BIT(0)|GJ_BIT(1);
    if(index==ES2_HOOK_INDEX_flyer_join)return GJ_BIT(0);
    if(index==ES2_HOOK_INDEX_building_join)return GJ_BIT(0)|GJ_BIT(1)|GJ_BIT(4);
    if(index==ES2_HOOK_INDEX_footprint_marks)return GJ_BIT(2)|GJ_BIT(3);
    return 0;
}
/* The image globals: the terrain pointer, the base layouts, the MECH and
 * FLYER formation tables, and the formation slots' static buffers (MECH
 * 49a430..437, BASE 4975d6..4975e2). */
static const struct { uint32_t rva,size; } gj_ranges[]={{0xa0be0,4},{0xa95ac,4},{0xa9d6c,4},{0xa9dfc,4},{0x9a430,8},{0x975d6,13}};
enum { GJ_GLOBALS=4+4+4+4+8+13 };
static void gj_globals(BYTE *out,int restore){
    for(unsigned i=0;i<sizeof(gj_ranges)/sizeof(gj_ranges[0]);++i){
        if(restore)memcpy(mapped+gj_ranges[i].rva,out,gj_ranges[i].size);else memcpy(out,mapped+gj_ranges[i].rva,gj_ranges[i].size);
        out+=gj_ranges[i].size;
    }
}
static int gj_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<GJ_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
static uint32_t gj_real_va(uint32_t va){return (uint32_t)(uintptr_t)(mapped+(va-0x400000u));}
/* A coordinate inside the real 8 x 8 grid of 100h-unit cells, cells 2..5. */
static uint32_t gj_grid(void){return 0x200u+gj_next()%0x400u;}
/* The real terrain at GJ_TER: 8 x 8 cells of 16 bytes (heights), the cell
 * flags, log2 cell size 8; the rest of the object random. */
static void gj_real_terrain(void){
    BYTE *T=gj_arena+GJ_TER;
    put32(T+0xec,GJ_A(GJ_HEIGHTS));put32(T+0xf0,GJ_A(GJ_FLAGS));put32(T+0x100,3);put32(T+0x104,3);put32(T+0x108,8);
    put32(T+0x110,gj_next()%4096);put32(T+0x118,gj_next()%64);
    put32(mapped+0xa0be0,GJ_A(GJ_TER));
}
/* The case data. exact: the real callers' inputs (slot words 0..19, snap 0
 * or 1, the building's vector reachable); real: data for the real-callee
 * passes. */
static void gj_build(unsigned index,unsigned n,uint32_t a[4],uint32_t r[3],uint32_t *fill,int exact,int real){
    gj_seed=random32();gj_stream=gj_seed^n;gj_effects=!real&&!exact;gj_index=index;
    for(unsigned i=0;i<3;++i)r[i]=gj_next();
    gj_random(0,GJ_SIZE);memset(a,0,16);
    *fill=!exact&&!real&&gj_next()%2?gj_next():0;
    put32(mapped+0xa0be0,gj_next());put32(mapped+0xa95ac,GJ_A(GJ_LAY));
    put32(mapped+0xa9d6c,GJ_A(GJ_FTAB));put32(mapped+0xa9dfc,GJ_A(GJ_FTAB+0x100));
    if(index==ES2_HOOK_INDEX_mech_join||index==ES2_HOOK_INDEX_flyer_join||index==ES2_HOOK_INDEX_building_join){
        const uint32_t A=GJ_A(GJ_OBJ+(real?0:gj_next()%4)),G=GJ_A(GJ_GROUP);gj_obj=A;BYTE *P=gj_host(A);
        const int building=index==ES2_HOOK_INDEX_building_join;
        put32(P,GJ_A(GJ_VT));put32(P+0x1f2,GJ_A(GJ_TYPE));
        put16(P+0xc,gj_next()%3?0x8000:(uint16_t)gj_next());
        put32(P+0x2e,gj_next()%4?0xffff8000u:gj_next()%3?0:gj_next());
        put32(gj_arena+GJ_VT+0x78,(uint32_t)(uintptr_t)gj_stub_5);put32(gj_arena+GJ_VT+0x10,(uint32_t)(uintptr_t)gj_stub_6);
        put32(gj_arena+GJ_VT2+0x78,(uint32_t)(uintptr_t)gj_stub_7);put32(gj_arena+GJ_VT2+0x10,(uint32_t)(uintptr_t)gj_stub_8);
        put16(gj_arena+GJ_TYPE+0x50,gj_next()%2?0:(uint16_t)gj_next());put16(gj_arena+GJ_TYPE2+0x50,(uint16_t)(gj_next()%2));
        BYTE *g=gj_arena+GJ_GROUP;
        put32(g+0x18,gj_next()%2?GJ_A(GJ_POINT):0);
        put16(g+2,(uint16_t)(gj_next()%7u-1u));          /* -1 .. 5 */
        /* The base layouts: [L+4] the words of the slot entries (10 bytes per
         * slot, x and y dwords and the angle word), the grid at +0Ch..+14h. */
        for(int k=-1;k<6;++k){
            BYTE *L=gj_arena+GJ_LAY+28*k;
            put32(L+4,GJ_A(GJ_WORDS+0x40+0x10*(gj_next()%4)));
            if(gj_next()%3==0)put32(L+0x14,gj_next()%3);
        }
        a[0]=A;a[1]=G;
        /* the slot word: 0 .. 19 in the game; -8 .. 23 otherwise (the layout
         * word stays inside the arena), the high word random */
        a[2]=(exact||real?0:gj_next()&0xffff0000u)|(uint16_t)(exact||real?gj_next()%20u:gj_next()%32u-8u);
        a[3]=building?(gj_next()%2?0:exact||real?1:gj_next()):gj_next();
        if(real){
            gj_real_terrain();
            /* route_point: [G+6] the route (count word, point table) */
            put32(g+6,GJ_A(GJ_ROUTE));put16(g+4,(uint16_t)(gj_next()%4));
            put16(gj_arena+GJ_ROUTE,4);put32(gj_arena+GJ_ROUTE+2,GJ_A(GJ_RPTS));
            for(unsigned i=0;i<4;++i){
                put32(gj_arena+GJ_RPTS+4*i,GJ_A(GJ_RPOINTS+12*i));
                put32(gj_arena+GJ_RPOINTS+12*i,gj_grid());put32(gj_arena+GJ_RPOINTS+12*i+4,gj_grid());put32(gj_arena+GJ_RPOINTS+12*i+8,gj_next());
            }
            put32(gj_arena+GJ_POINT,gj_grid());put32(gj_arena+GJ_POINT+4,gj_grid());
            put16(g+2,(uint16_t)(gj_next()%4));
            /* the formation slot: the real method of the class */
            const uint32_t method=index==ES2_HOOK_INDEX_mech_join?0x417898u:index==ES2_HOOK_INDEX_flyer_join?0x421e98u:0x405c04u;
            put32(gj_arena+GJ_VT+0x78,gj_real_va(method));put32(gj_arena+GJ_VT+0x10,gj_real_va(0x46b910u));
            /* the leader [[G+0Ch]] (A itself or another actor), its cache flag */
            put32(g+0xc,GJ_A(GJ_MEMBERS));put32(gj_arena+GJ_MEMBERS,gj_next()%2?A:GJ_A(GJ_LEADER));
            put16(gj_arena+GJ_LEADER+0x32,0);put16(P+0x32,0);   /* euler_matrix builds the leader's matrix */
            /* formation tables: small offsets */
            for(unsigned i=0;i<0x200;i+=2)put16(gj_arena+GJ_FTAB+i,(uint16_t)(gj_next()%201u-100u));
            for(int k=-1;k<6;++k){
                BYTE *L=gj_arena+GJ_LAY+28*k;
                put32(L+4,GJ_A(GJ_WORDS));put32(L+0x14,1);put32(L+0xc,16u+gj_next()%25u);put32(L+0x10,216u+gj_next()%25u);
            }
            for(unsigned i=0;i<0x100;i+=10){
                put32(gj_arena+GJ_WORDS+i,gj_next()%201u-100u);put32(gj_arena+GJ_WORDS+i+4,gj_next()%201u-100u);
            }
            /* shape_word: [[B+34h]+4] word +8, or no shape instance */
            put32(P+0x34,gj_next()%4?GJ_A(GJ_SHAPEI):0);put32(gj_arena+GJ_SHAPEI+4,gj_next()%4?GJ_A(GJ_SHAPEI+0x10):0);
            put16(gj_arena+GJ_SHAPEI+0x18,(uint16_t)(gj_next()%0x100u));
        }
    } else if(index==ES2_HOOK_INDEX_footprint_marks){
        BYTE *T=gj_arena+GJ_TER,*Q=gj_arena+GJ_P;
        unsigned s=gj_next()%13u;
        put32(T+0x108,s|(gj_next()%4==0?gj_next()&0xffffffe0u:0));
        uint32_t radius=(uint32_t)((int32_t)(gj_next()%(4u<<s))-(int32_t)(2u<<s));   /* -2 .. 2 cells */
        if(gj_next()%8==0)radius=gj_next()%3u-1u;
        for(unsigned axis=0;axis<2;++axis){
            /* any coordinate only with r >= 0: a negative r with a wrapping
             * x - r would make the box span most of the 32-bit range */
            const unsigned k=(int32_t)radius<0?1u+gj_next()%3u:gj_next()%4;
            put32(Q+4*axis,k==0?gj_next():k==1?(uint32_t)((int32_t)(gj_next()%(8u<<s))-(int32_t)(4u<<s)):gj_next()%(64u<<s));
        }
        put32(Q+8,gj_next());put32(Q+0xc,radius);   /* +0Ch: r for the dist_gate stub */
        a[0]=GJ_A(GJ_TER);a[1]=GJ_A(GJ_P);a[2]=radius;
        if(real){
            gj_real_terrain();
            put32(Q,gj_grid());put32(Q+4,gj_grid());a[2]=gj_next()%0x100u;
        }
    } else {
        BYTE *T=gj_arena+GJ_TER;
        const unsigned c=gj_next()%8u;
        put32(T+0x100,c|(gj_next()%4==0?gj_next()&0xffffffe0u:0));
        const uint32_t y=gj_next()%8u,x=gj_next()%128u-64u;
        put32(T+0xf0,gj_next()%8==0?GJ_A(GJ_TER+0x100)-(y<<c)-x+gj_next()%8u:GJ_A(GJ_CELLS+0x40));   /* sometimes over T itself */
        a[0]=GJ_A(GJ_TER);a[1]=x;a[2]=y;a[3]=gj_next();
    }
    gj_stream=gj_seed;   /* the stubs' stream restarts here in every run */
}
/* Calls target with four argument slots, EAX/EDX/ECX = r over 3,840 bytes
 * of stack filled with `fill`; out receives EAX, EDX and ECX after the
 * call. */
__attribute__((naked,noinline)) static void gj_invoke(void *target,const uint32_t *a,const uint32_t *r,uint32_t fill,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 20(%ebp),%eax\n\tmovl $1024,%ecx\n\tcld\n\trep stosl\n\taddl $3840,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%eax\n\tmovl 4(%eax),%edx\n\tmovl 8(%eax),%ecx\n\tmovl (%eax),%eax\n\tcall *8(%ebp)\n\t"
        "movl 24(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* The core with the adapter's graph and scratch. */
static uint64_t gj_core(unsigned index,const uint32_t *a,const uint32_t *r,uint32_t *ecx){
    const es2_guest_memory *m=&math_native_memory;
    BYTE scratch[ES2_GROUP_JOINS_SCRATCH]={0};es2_weapon_graph g=weapons_graph(mapped,scratch,index);
    if(index==ES2_HOOK_INDEX_mech_join)return es2_mech_join_registers(m,&g,a[0],a[1],a[2],r,ecx);
    if(index==ES2_HOOK_INDEX_flyer_join)return es2_flyer_join_registers(m,&g,a[0],a[1],a[2],r,ecx);
    if(index==ES2_HOOK_INDEX_building_join)return es2_building_join_registers(m,&g,a[0],a[1],a[2],a[3],r,ecx);
    if(index==ES2_HOOK_INDEX_footprint_marks)return es2_footprint_marks_registers(m,&g,a[0],a[1],a[2],r,ecx);
    return es2_cell_flag_set_registers(m,a[0],a[1],a[2],a[3],r,ecx);
}
static int gj_report(const char *what,unsigned n,const uint32_t want[3],const uint32_t got[3],unsigned calls,const gj_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x calls %u/%u\n",current_function,n,what,
        want[0],got[0],want[1],got[1],want[2],got[2],calls,gj_calls);
    for(unsigned j=0;j<calls&&j<gj_calls&&expected;++j)if(memcmp(expected+j,gj_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(gj_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<GJ_SIZE;++j)if(gj_after[j]!=gj_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,gj_after[j],gj_arena[j]);break;}
    return 1;
}
/* The callee counter key of a stub id. */
static int gj_counts_for(unsigned id,const callee_counter *c){
    if(id<5)return c->event==ES2_COLLIDE_CALL&&c->key==gj_addresses[id];
    return c->event==ES2_COLLIDE_SLOT&&c->key==(id==5||id==7?0x78u:0x10u);
}
static unsigned gj_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<gj_calls;++j)if(gj_counts_for(gj_log[j].id,c))++hits;
    return hits;
}
/* One direct case: the original, the core and (exact inputs) the adapter. */
static int gj_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r,uint32_t fill,int exact,int check_ecx){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[GJ_GLOBALS],after[GJ_GLOBALS],actual[GJ_GLOBALS];static gj_record expected[GJ_MAX];uint32_t want[3],got[3];
    memcpy(gj_before,gj_arena,GJ_SIZE);gj_globals(before,0);
    gj_reset();gj_invoke(mapped+d->rva,a,r,fill,want);const unsigned calls=gj_calls;memcpy(expected,gj_log,calls*sizeof(*expected));
    memcpy(gj_after,gj_arena,GJ_SIZE);gj_globals(after,0);
    for(unsigned j=0;j<calls;++j)++gj_paths[expected[j].id];
    memcpy(gj_arena,gj_before,GJ_SIZE);gj_globals(before,1);gj_reset();
    got[2]=~want[2];
    uint64_t pair=gj_core(index,a,r,&got[2]);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);gj_globals(actual,0);
    if(want[0]!=got[0]||want[1]!=got[1]||(check_ecx&&want[2]!=got[2])||calls!=gj_calls||memcmp(expected,gj_log,calls*sizeof(*expected))||
       memcmp(gj_after,gj_arena,GJ_SIZE)||gj_globals_differ(after,actual))
        return gj_report("core",n,want,got,calls,expected);
    if(!exact){gj_globals(before,1);return 0;}
    /* the adapter (the framework's entry: four argument slots) */
    memcpy(gj_arena,gj_before,GJ_SIZE);gj_globals(before,1);gj_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];
    if(count>8)return fail("group_joins counter table too long");
    for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair=x->replacement_context(a,d->user,r);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);got[2]=want[2];
    gj_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=gj_expected_hits(c+i))return fail("group_joins callee counter delta");
    if(want[0]!=got[0]||want[1]!=got[1]||calls!=gj_calls||memcmp(expected,gj_log,calls*sizeof(*expected))||
       memcmp(gj_after,gj_arena,GJ_SIZE)||gj_globals_differ(after,actual))
        return gj_report("adapter",n,want,got,calls,expected);
    gj_globals(before,1);
    return 0;
}
static int gj_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[4],r[3],fill,want[3]={0},got[3]={0},pattern=poison?0xa7a7a7a7u:0x31313131u;
        gj_build(index,n*37u+11u,a,r,&fill,1,0);
        const uint32_t entry[3]={pattern,pattern,pattern};
        BYTE before[GJ_GLOBALS],after[GJ_GLOBALS],actual[GJ_GLOBALS],expected_fpu[108],actual_fpu[108];static gj_record expected[GJ_MAX];
        memcpy(gj_before,gj_arena,GJ_SIZE);gj_globals(before,0);
        gj_reset();gj_invoke(mapped+d->rva,a,entry,pattern,want);es2_hook_fpu_save(expected_fpu);
        const unsigned calls=gj_calls;memcpy(expected,gj_log,calls*sizeof(*expected));memcpy(gj_after,gj_arena,GJ_SIZE);gj_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("group_joins poison install");
        memcpy(gj_arena,gj_before,GJ_SIZE);gj_globals(before,1);gj_reset();gj_invoke(mapped+d->rva,a,entry,pattern,got);
        es2_hook_fpu_save(actual_fpu);gj_globals(actual,0);
        if(want[0]!=got[0]||want[1]!=got[1]||calls!=gj_calls||memcmp(expected,gj_log,calls*sizeof(*expected))||
           memcmp(gj_after,gj_arena,GJ_SIZE)||gj_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||
           memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=gj_report(fast?"installed fast":"installed preserving",n,want,got,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("group_joins poison uninstall");gj_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, pair/trace/arena/globals\n",d->name,total);
    return status;
}
/* ---- real callees ---- */
enum { GJ_NESTED=7 };
static unsigned gj_nested(unsigned index,unsigned nested[GJ_NESTED]){
    unsigned count=0;
#define GJ_N(name) nested[count++]=ES2_HOOK_INDEX_##name
    if(index==ES2_HOOK_INDEX_mech_join){GJ_N(route_point);GJ_N(terrain_height);GJ_N(formation_mech);}
    else if(index==ES2_HOOK_INDEX_flyer_join){GJ_N(route_point);GJ_N(formation_flyer);}
    else if(index==ES2_HOOK_INDEX_building_join){GJ_N(route_point);GJ_N(terrain_height);GJ_N(formation_base);GJ_N(shape_word);
        GJ_N(footprint_marks);GJ_N(dist_gate);GJ_N(cell_flag_set);}
    else if(index==ES2_HOOK_INDEX_footprint_marks){GJ_N(dist_gate);GJ_N(cell_flag_set);}
#undef GJ_N
    return count;
}
static int gj_real_pass(unsigned index){
    unsigned nested[GJ_NESTED];const unsigned count=gj_nested(index,nested);
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[GJ_NESTED+1]={0};
    static gj_record expected[GJ_MAX];
    const unsigned modes=2u<<count;
    for(unsigned mode=0;mode<modes&&!status;++mode)for(unsigned n=0;n<4096u/modes&&!status;++n){
        uint32_t a[4],r[3],fill,want[3]={0},got[3]={0},pattern=n&1?0xa7a7a7a7u:0x31313131u;
        gj_build(index,n*13u+mode,a,r,&fill,1,1);
        const uint32_t entry[3]={pattern,pattern,pattern};
        BYTE before[GJ_GLOBALS],after[GJ_GLOBALS],actual[GJ_GLOBALS];
        memcpy(gj_before,gj_arena,GJ_SIZE);gj_globals(before,0);
        es2_hook hk[GJ_NESTED+1];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(mode>>(1+j)&1)?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,function_hook_extension(nested[j])))return fail("group_joins nested install");}
        gj_reset();gj_invoke(mapped+d->rva,a,entry,pattern,want);
        const unsigned calls=gj_calls;memcpy(expected,gj_log,calls*sizeof(*expected));
        memcpy(gj_after,gj_arena,GJ_SIZE);gj_globals(after,0);
        o.mode=mode&1?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(hk+GJ_NESTED,d,&o,function_hook_extension(index)))return fail("group_joins caller install");
        memcpy(gj_arena,gj_before,GJ_SIZE);gj_globals(before,1);gj_reset();
        gj_invoke(mapped+d->rva,a,entry,pattern,got);gj_globals(actual,0);
        if(want[0]!=got[0]||want[1]!=got[1]||calls!=gj_calls||memcmp(expected,gj_log,calls*sizeof(*expected))||
           memcmp(gj_after,gj_arena,GJ_SIZE)||gj_globals_differ(after,actual)){
            fprintf(stderr,"real mode=%u\n",mode);status=gj_report("real",n,want,got,calls,expected);
        }
        for(unsigned j=0;j<=GJ_NESTED;++j){if(!hk[j].descriptor)continue;entered[j]+=hk[j].calls;
            if(hk[j].mismatches)status=fail("group_joins real compare mismatch");
            if(!es2_hook_uninstall(hk+j))return fail("group_joins real uninstall");}
        gj_globals(before,1);++total;
    }
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&!entered[GJ_NESTED]){fprintf(stderr,"FAIL: %s never entered\n",d->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, %u nested hooks original/replaced, hook original/replaced, 31/a7 stack, pair, calls, arena, globals\n",
        d->name,total,count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
static int group_joins_cases(unsigned index,unsigned limit){
    current_function=function_hook_descriptor(index)->name;
    const uint32_t which=gj_which(index);
    BYTE saved[5][5],old_globals[GJ_GLOBALS];int active[5];
    gj_globals(old_globals,0);
    if(gj_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_GROUP_JOINS_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);memset(gj_paths,0,sizeof(gj_paths));
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[4],r[3],fill;const int exact=limit||!(n&1);
        gj_build(index,n,a,r,&fill,exact,0);status=gj_compare(index,n,a,r,fill,exact&&!limit,!limit);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    gj_globals(old_globals,1);
    if(!limit&&!status)status=gj_installed_probes(index,64);
    if(gj_patch(saved,active,which,0))return 1;
    gj_globals(old_globals,1);
    if(!limit&&!status&&gj_nested(index,(unsigned[GJ_NESTED]){0}))status=gj_real_pass(index);
    gj_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases (stub calls route_point %u, terrain_height %u, dist_gate %u, cell_flag_set %u, "
        "footprint_marks %u, slot 78h %u/%u, slot 10h %u/%u); full return pair and ECX, ordered calls with entry registers, slots and "
        "vectors, arena/globals\n",function_hook_descriptor(index)->name,cases,gj_paths[0],gj_paths[1],gj_paths[2],gj_paths[3],gj_paths[4],
        gj_paths[5],gj_paths[7],gj_paths[6],gj_paths[8]);
    return status;
}
