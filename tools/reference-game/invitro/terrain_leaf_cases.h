/* Callees of the terrain walk: slope_test 46ff4c, normalize 46c23c,
 * diagonal 46fdb8, edge_test 470468, face_test 46ff90, face_cross 470080,
 * contact 470798, terrain_line 4280f4. Generated grids and points; game
 * callees are logging stubs that also record their entry registers, then a
 * real-callee pass on generated terrain under mixed nested modes. */
typedef struct tl_record { uint32_t id,args[6],regs[3],input[10],eax,edx; } tl_record;
enum { TL_SIZE=0x2000, TL_MAP=0x40, TL_PREV=0x180, TL_CUR=0x190, TL_OUT=0x1a0, TL_VEC=0x1b0, TL_NORMAL=0x1c0,
    TL_HIT=0x1d0, TL_START=0x1e0, TL_END=0x1f0, TL_CELLS=0x600, TL_MAX=64, TL_IDS=5 };
#define TL_BASE ((uint32_t)(uintptr_t)tl_arena)
static BYTE tl_arena[TL_SIZE],tl_before[TL_SIZE],tl_after[TL_SIZE];
static tl_record tl_log[TL_MAX];
static unsigned tl_calls,tl_big;
static uint32_t tl_seed,tl_stream,tl_length;
__attribute__((used)) static uint32_t tl_ecx;
/* 47de72, 46ff4c, 47e610, 46e980, 49288c. */
static const uint32_t tl_addresses[TL_IDS]={0x47de72,0x46ff4c,0x47e610,0x46e980,0x49288c};
static const unsigned tl_counts[TL_IDS]={3,3,3,6,2};
static uint32_t tl_next(void){
    uint32_t z=tl_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *tl_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int tl_inside(uint32_t a){return a-TL_BASE<TL_SIZE;}
static int tl_stack_address(uint32_t value){
    /* Fixed window (the worker's 16 MiB stack reservation): a window around
     * the caller's own frame moved between the two runs and masked a random
     * value near its edge on one side only (normalize case 457380). */
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
__attribute__((used,noinline)) static uint64_t __cdecl tl_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(tl_calls==TL_MAX){fail("terrain leaf synthetic call overflow");ExitProcess(1);}
    tl_record *v=tl_log+tl_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,4*tl_counts[id]);memcpy(v->regs,regs,12);
    /* Entry registers that hold frame addresses differ by construction. */
    for(unsigned j=0;j<3;++j)if(tl_stack_address(v->regs[j]))v->regs[j]=0;
    uint32_t eax=tl_next(),edx=tl_next(),pick=tl_next();tl_ecx=tl_next();
    switch(id){
    case 0: {
        /* Length words around the 7d00h gate; the final one must be a valid divisor. */
        if(tl_big<12&&pick%3==0){++tl_big;eax=(eax&0xffff0000u)|(0x7d01u+pick%0x200u);break;}
        uint16_t w=(uint16_t)(pick%4==0?0x7d00u:pick%4==1?1u+(pick>>8)%0x7d00u:(uint16_t)(pick>>8));
        if(!w||(w==0xffffu&&(uint16_t)tl_length==0x8000u))w=1;
        if(w>0x7d00u&&w<0x8000u)w=0x7d00u;
        eax=(eax&0xffff0000u)|w;break; }
    case 1:
        v->regs[0]=regs[0];v->args[1]=tl_inside(a[1])?a[1]:0;
        if(tl_inside(a[2]))for(unsigned j=0;j<3;++j)v->input[j]=get16(tl_host(a[2])+2*j);
        eax=pick%3?eax&0xffffff00u:eax|1u;break;
    case 2:
        v->args[0]=v->args[1]=0;
        for(unsigned j=0;j<4;++j)v->input[j]=get32(tl_host(a[0])+4*j);
        for(unsigned j=0;j<6;++j)v->input[4+j]=get32(tl_host(a[1])+4*j);
        if(pick%3)for(unsigned j=0;j<12;++j)tl_host(a[2])[j]=(BYTE)tl_next();
        /* Contacts on the triangle borders: x = y (flags 2) and small x + y (flags 0). */
        if(pick%3&&(pick>>8)%3==0)put32(tl_host(a[2])+4,get32(tl_host(a[2])));
        if(pick%3&&(pick>>8)%3==1){put32(tl_host(a[2]),tl_next()%4096);put32(tl_host(a[2])+4,tl_next()%4096);}
        eax=pick%3?eax|1u:eax&0xffffff00u;break;
    case 3:
        v->args[4]=0;
        if(pick%3){eax|=1u;for(unsigned j=0;j<3;++j)put32(tl_host(a[4])+4*j,tl_next());} else eax&=0xffffff00u;
        break;
    case 4:v->args[1]=0;for(unsigned j=0;j<3;++j)v->input[j]=get32(tl_host(a[1])+4*j);break;
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
/* Pushes the entry EAX/EDX/ECX for the record before the slot pointer. */
#define TL_STUB(n) __attribute__((naked)) static void tl_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _tl_stub_impl\n\taddl $24,%esp\n\tmovl _tl_ecx,%ecx\n\tret\n\t");}
TL_STUB(0) TL_STUB(1) TL_STUB(2) TL_STUB(3) TL_STUB(4)
#undef TL_STUB
static void *tl_stubs[TL_IDS]={tl_stub_0,tl_stub_1,tl_stub_2,tl_stub_3,tl_stub_4};
#define TL_GLOBALS 21
static void tl_globals(BYTE *out,int restore){
    static const unsigned offsets[]={0xa0c4c,0x2b505c,0xa0be0};
    static const unsigned sizes[]={1,16,4};
    for(unsigned i=0;i<3;++i){if(restore)memcpy(mapped+offsets[i],out,sizes[i]);else memcpy(out,mapped+offsets[i],sizes[i]);out+=sizes[i];}
}
static void tl_reset(void){tl_calls=tl_big=0;tl_stream=tl_seed;}
/* Up to 16 x 16 cells; flags 3 only where the hook under test has no
 * residue dependence. Coordinates around the grid, on cell edges and inside. */
static void tl_grid(unsigned n,int real,int flags3){
    uint32_t b=TL_BASE;BYTE *map=tl_arena+TL_MAP;
    uint32_t w=real?random32()%4:random32()%5,h=real?random32()%4:random32()%5,s=real?6+random32()%6:random32()%4?random32()%13:random32()%32;
    /* Exponent sums that wrap past 31: (row << 31) and the limit
     * 1 << ((columns + rows) & 31) then disagree, which reaches the
     * negative neighbour indices. */
    if(!real&&n%17==5){static const uint8_t wrap[][2]={{31,1},{31,4},{31,9},{30,2},{30,6},{28,4},{2,31},{4,30}};
        unsigned k=random32()%8;w=wrap[k][0];h=wrap[k][1];}
    put32(map+0x100,n%11==0&&!real?w|(random32()&0xffffffe0u):w);put32(map+0x104,h);put32(map+0x108,n%13==0&&!real?s|(random32()&0xffffffe0u):s);
    put32(map+0xec,b+TL_CELLS);put32(map+0x110,random32()%4096);put32(map+0x118,random32()%4?random32()%64:random32());
    if(real)put32(map+0x118,random32()%64);
    uint32_t cells=1u<<((w+h)&31u);if(cells>256)cells=256;
    for(unsigned c=0;c<cells;++c){
        BYTE *cell=tl_arena+TL_CELLS+16*c;
        cell[15]=(BYTE)((random32()&0xfcu)|(flags3?random32()%4:random32()%3));
    }
}
static uint32_t tl_coordinate(uint32_t cells,uint32_t s){
    s&=31u;uint32_t cell=1u<<s,index=(uint32_t)(random32()%(cells+4u))-2u;
    switch(random32()%6){case 0:return index<<s;case 1:return (index<<s)+cell-1u;case 2:return random32();default:return (index<<s)+(s?random32()%cell:0);}
}
static void tl_point(uint32_t at,int real){
    BYTE *map=tl_arena+TL_MAP;uint32_t w=1u<<(get32(map+0x100)&31u),h=1u<<(get32(map+0x104)&31u),s=get32(map+0x108);
    if(w>16)w=1;if(h>16)h=1;
    put32(tl_arena+at,tl_coordinate(w,s));put32(tl_arena+at+4,tl_coordinate(h,s));
    put32(tl_arena+at+8,real?random32()%24000:random32()%4?random32()%8192:random32());
}
static uint32_t tl_index(uint32_t cells){return random32()%8?random32()%(cells+2u)-1u:random32();}
static void tl_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    uint32_t b=TL_BASE;tl_seed=random32();random_bytes(tl_arena,TL_SIZE);
    memset(a,0,24);for(unsigned i=0;i<3;++i)r[i]=random32();
    tl_grid(n,real,index!=ES2_HOOK_INDEX_face_test||real==2);
    BYTE *map=tl_arena+TL_MAP;uint32_t w=1u<<(get32(map+0x100)&31u),h=1u<<(get32(map+0x104)&31u);
    if(w>16)w=1;if(h>16)h=1;
    tl_point(TL_PREV,real);tl_point(TL_CUR,real);tl_point(TL_START,real);tl_point(TL_END,real);
    uint32_t column=tl_index(w),row=tl_index(h);
    if(index==ES2_HOOK_INDEX_slope_test){
        static const uint16_t gates[]={0x60d,0x60e,0x60f,0x60e,0x8000,0x7fff};
        put16(tl_arena+TL_NORMAL+4,n<4096?gates[n%6]:(uint16_t)(random32()%4?0x600+random32()%32:random32()));
        if(random32()%2){put16(tl_arena+TL_NORMAL,0);put16(tl_arena+TL_NORMAL+2,0);put16(tl_arena+TL_NORMAL+4,0x60e);
            put16(tl_arena+TL_VEC,(uint16_t)random32());put16(tl_arena+TL_VEC+2,(uint16_t)random32());put16(tl_arena+TL_VEC+4,(uint16_t)(-5162+(int)(random32()%3)));}
        a[0]=b+TL_MAP;a[1]=b+TL_VEC;a[2]=b+TL_NORMAL+(n%2?0:2*(random32()%2));
    } else if(index==ES2_HOOK_INDEX_normalize){
        static const uint16_t lengths[]={0x800,0x7fff,1,0x8000,0};
        tl_length=n<1024?lengths[n%5]:random32();a[0]=b+TL_VEC;a[1]=(random32()&0xffff0000u)|(uint16_t)tl_length;
        if(real)for(unsigned j=0;j<3;++j)put16(tl_arena+TL_VEC+2*j,(uint16_t)(j==0?1+random32()%30000:random32()%60001-30000));
        if(real)a[1]=0x800;
    } else if(index==ES2_HOOK_INDEX_diagonal){
        column=random32()%w;row=random32()%h;
        /* Contract: the cell exists (wrapped exponent sums can shrink the limit). */
        {uint32_t i=(row<<(get32(map+0x100)&31u))+column;if((int32_t)i<0||(int32_t)i>=(int32_t)(1u<<((get32(map+0x100)+get32(map+0x104))&31u)))column=row=0;}
        a[0]=b+TL_MAP;a[1]=b+TL_PREV;a[2]=b+TL_CUR;a[3]=column;a[4]=row;a[5]=b+TL_OUT;
        uint32_t s=get32(map+0x108)&31u,cell=1u<<s;
        for(unsigned p=0;p<2;++p)for(unsigned j=0;j<2;++j){uint32_t base=(j?row:column)<<s;
            put32(tl_arena+(p?TL_CUR:TL_PREV)+4*j,random32()%4?base+random32()%(cell+cell/2+1)-cell/4:random32());}
    } else if(index==ES2_HOOK_INDEX_edge_test||index==ES2_HOOK_INDEX_face_cross){
        a[0]=b+TL_MAP;a[1]=column;a[2]=row;
        uint32_t direction=random32()%8?random32()%4:random32()%3?4+random32()%4:random32();
        if(index==ES2_HOOK_INDEX_edge_test){
            a[3]=b+TL_CUR;a[4]=direction;
            uint32_t scale=get32(map+0x118),offset=get32(map+0x110),pickz=random32()%6;
            uint32_t h0=(uint32_t)tl_arena[TL_CELLS+16*(random32()%(w*h))]*scale+offset;
            put32(tl_arena+TL_CUR+8,pickz<2?h0+pickz-1:pickz==2?offset+random32()%(256*scale+1):get32(tl_arena+TL_CUR+8));
        } else {a[3]=b+TL_VEC;a[4]=direction;}
    } else if(index==ES2_HOOK_INDEX_face_test){
        a[0]=b+TL_MAP;a[1]=b+TL_VEC;a[2]=b+TL_CUR;
    } else if(index==ES2_HOOK_INDEX_contact){
        a[0]=b+TL_MAP;a[1]=b+TL_PREV;a[2]=b+TL_CUR;a[3]=random32();a[4]=column;a[5]=row;
        mapped[0xa0c4c]=(BYTE)(random32()%3?0:random32());
    } else {
        a[0]=b+TL_OUT;a[1]=b+TL_START;a[2]=b+TL_END;a[3]=random32();
        put32(mapped+0xa0be0,b+TL_MAP);
        if(real)a[3]=random32()%512;
    }
}
static int tl_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const tl_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,tl_calls);
    for(unsigned j=0;j<calls&&j<tl_calls&&expected;++j)if(memcmp(expected+j,tl_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(tl_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<TL_SIZE;++j)if(tl_after[j]!=tl_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,tl_after[j],tl_arena[j]);break;}
    return 1;
}
static unsigned tl_expected_hits(uint32_t key){
    unsigned hits=0;for(unsigned j=0;j<tl_calls;++j)if(tl_addresses[tl_log[j].id]==key)++hits;return hits;
}
static int tl_same_pair(unsigned index,uint64_t want,uint64_t got){
    if(want==got)return 1;
    /* edge_test's at-or-below path returns the frame address of the lower
     * corner height in EDX; it is dead at the only caller. */
    return index==ES2_HOOK_INDEX_edge_test&&(uint32_t)want==(uint32_t)got&&tl_stack_address((uint32_t)(want>>32));
}
static int tl_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[TL_GLOBALS],after[TL_GLOBALS],actual[TL_GLOBALS];tl_record expected[TL_MAX];
    memcpy(tl_before,tl_arena,TL_SIZE);tl_globals(before,0);
    tl_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=tl_calls;memcpy(expected,tl_log,calls*sizeof(*expected));
    memcpy(tl_after,tl_arena,TL_SIZE);tl_globals(after,0);
    memcpy(tl_arena,tl_before,TL_SIZE);tl_globals(before,1);tl_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[4];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);tl_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=tl_expected_hits(c[i].key))return fail("terrain leaf callee counter delta");
    if(!tl_same_pair(index,pair[0],pair[1])||calls!=tl_calls||memcmp(expected,tl_log,calls*sizeof(*expected))||memcmp(tl_after,tl_arena,TL_SIZE)||memcmp(after,actual,TL_GLOBALS)){
        for(unsigned j=0;j<TL_GLOBALS;++j)if(after[j]!=actual[j])fprintf(stderr," global byte %u %02x/%02x\n",j,after[j],actual[j]);
        return tl_report("direct",n,pair,calls,expected);
    }
    return 0;
}
static int tl_patch(BYTE saved[TL_IDS][5],int active[TL_IDS],unsigned index,int on){
    static const unsigned mask[]={0,1,0,0,2,0,0,4,24};
    unsigned which=index==ES2_HOOK_INDEX_normalize?1:index==ES2_HOOK_INDEX_face_test||index==ES2_HOOK_INDEX_face_cross?2:
        index==ES2_HOOK_INDEX_contact?4:index==ES2_HOOK_INDEX_terrain_line?24:0;
    (void)mask;
    for(unsigned i=0;i<TL_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(tl_addresses[i]-0x400000u,tl_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(tl_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static int tl_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;tl_build(index,n,a,r,0);
        if(index==ES2_HOOK_INDEX_face_test&&n%2)tl_grid(n,2,1);
        BYTE before[TL_GLOBALS],after[TL_GLOBALS],actual[TL_GLOBALS],expected_fpu[108],actual_fpu[108];tl_record expected[TL_MAX];
        memcpy(tl_before,tl_arena,TL_SIZE);tl_globals(before,0);
        tl_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=tl_calls;memcpy(expected,tl_log,calls*sizeof(*expected));memcpy(tl_after,tl_arena,TL_SIZE);tl_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("terrain leaf poison install");
        memcpy(tl_arena,tl_before,TL_SIZE);tl_globals(before,1);tl_reset();sg_poison_call(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);tl_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(!tl_same_pair(index,pair[0],pair[1])||calls!=tl_calls||memcmp(expected,tl_log,calls*sizeof(*expected))||memcmp(tl_after,tl_arena,TL_SIZE)||
           memcmp(after,actual,TL_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=tl_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("terrain leaf poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state\n",d->name,total);
    return status;
}
/* Real callees on terrain of up to 8 x 8 cells, flags 0..2, nested hooks in
 * rotating modes; EAX and all memory are compared (real callees may leave
 * frame addresses in EDX, which is dead at every caller of these hooks). */
static int tl_real(unsigned index){
    static const unsigned nested[]={ES2_HOOK_INDEX_dist3d_delta,ES2_HOOK_INDEX_slope_test,ES2_HOOK_INDEX_segment,ES2_HOOK_INDEX_terrain_height,
        ES2_HOOK_INDEX_dist_gate,ES2_HOOK_INDEX_transform_point};
    enum { NESTED=sizeof(nested)/sizeof(nested[0]) };
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0,ones=0;uint64_t entered[NESTED]={0};
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;tl_build(index,n,a,r,1);
        if(index==ES2_HOOK_INDEX_diagonal){BYTE *map=tl_arena+TL_MAP;a[3]%=1u<<get32(map+0x100);a[4]%=1u<<get32(map+0x104);}
        BYTE before[TL_GLOBALS],after[TL_GLOBALS],actual[TL_GLOBALS];
        memcpy(tl_before,tl_arena,TL_SIZE);tl_globals(before,0);
        es2_hook hk[NESTED+1];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<NESTED;++j){if(nested[j]==index)continue;o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("terrain leaf nested install");}
        sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        memcpy(tl_after,tl_arena,TL_SIZE);tl_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+NESTED,d,&o,function_hook_extension(index)))return fail("terrain leaf caller install");
        memcpy(tl_arena,tl_before,TL_SIZE);tl_globals(before,1);
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);tl_globals(actual,0);
        if(expected_pair[0]!=actual_pair[0]||memcmp(tl_after,tl_arena,TL_SIZE)||memcmp(after,actual,TL_GLOBALS)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            fprintf(stderr,"real mode=%u\n",mode);tl_calls=0;status=tl_report("real",n,pair,0,NULL);
        }
        ones+=(uint8_t)expected_pair[0]!=0;
        for(unsigned j=0;j<=NESTED;++j){if(j<NESTED)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("terrain leaf nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("terrain leaf nested uninstall");}
        tl_globals(before,1);++total;
    }
    if(!status)printf("PASS: %s %u real-callee cases on generated terrain (%u nonzero AL), mixed nested modes, 31/a7 stack, EAX and memory\n",d->name,total,ones);
    (void)entered;return status;
}
static int terrain_leaf_cases(unsigned index,unsigned limit){
    BYTE saved[TL_IDS][5],old_globals[TL_GLOBALS];int active[TL_IDS];
    tl_globals(old_globals,0);if(tl_patch(saved,active,index,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_TERRAIN_LEAF_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];tl_build(index,n,a,r,0);status=tl_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    /* The installed probes are the only cases with flags 3 for face_test
     * (the shim supplies the residue), so they count for coverage too. */
    if(!limit&&!status){coverage_enable(1);status=tl_installed_probes(index,64);coverage_enable(0);}
    if(tl_patch(saved,active,index,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=tl_real(index);
    tl_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, frame payloads by content, out/arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
