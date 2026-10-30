/* Terrain segment walk 46e980 and its sweep wrapper 428048. Only the private,
 * hash-checked mapping executes original code. Grids, points and every callee
 * result are generated here; frame buffers handed to callees are compared by
 * content, never by stack address. A second pass runs the real callees on
 * small generated terrain under poisoned stacks and mixed hook modes. */
typedef struct sg_record { uint32_t id,args[6],input[8],eax,edx; } sg_record;
enum { SG_SIZE=0x1400, SG_MAP=0x40, SG_START=0x180, SG_END=0x190, SG_OUT=0x1a0, SG_HIT=0x1c0, SG_SEG=0x1e0,
    SG_MAT=0x200, SG_WOUT=0x240, SG_CELLS=0x800, SG_MAX=640, SG_IDS=10 };
#define SG_BASE ((uint32_t)(uintptr_t)sg_arena)
static BYTE sg_arena[SG_SIZE],sg_before[SG_SIZE],sg_after[SG_SIZE];
static sg_record sg_log[SG_MAX];
static unsigned sg_calls;
static uint32_t sg_seed,sg_stream;
__attribute__((used)) static uint32_t sg_ecx;
/* Walk callees 0..6, then the wrapper's 7..9. */
static const uint32_t sg_addresses[SG_IDS]={0x46c23c,0x46ff90,0x46fdb8,0x470080,0x46e180,0x470468,0x470798,0x48043c,0x46e980,0x49288c};
static const unsigned sg_counts[SG_IDS]={2,3,6,5,2,5,6,3,6,2};
/* Frame pointers per callee: bits 0..5 are recorded by content (bits 8..13
 * mark the six-byte direction words), bits 16..21 are outputs the caller has
 * not initialized and are only masked. */
static const unsigned sg_frames[SG_IDS]={0x101,0x206,6,0x808,2,8,6,0x40001,0x100004,2};
static uint32_t sg_next(void){
    uint32_t z=sg_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *sg_host(uint32_t address){return (BYTE *)(uintptr_t)address;}
static int sg_inside(uint32_t address){return address-SG_BASE<SG_SIZE;}
__attribute__((used,noinline)) static uint64_t __cdecl sg_stub_impl(unsigned id,const uint32_t *a){
    if(sg_calls==SG_MAX){fail("segment synthetic call overflow");ExitProcess(1);}
    sg_record *v=sg_log+sg_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,4*sg_counts[id]);
    unsigned at=0;
    for(unsigned k=0;k<6;++k)if(sg_frames[id]>>(16+k)&1)v->args[k]=0;
    for(unsigned k=0;k<6;++k)if(sg_frames[id]>>k&1){
        BYTE *p=sg_host(a[k]);v->args[k]=0;
        if(sg_frames[id]>>(8+k)&1){v->input[at++]=get32(p);v->input[at++]=get16(p+4);}
        else for(unsigned j=0;j<3;++j)v->input[at++]=get32(p+4*j);
    }
    uint32_t eax=sg_next(),edx=sg_next(),pick=sg_next();sg_ecx=sg_next();
    switch(id){
    case 0:if(pick%2)for(unsigned j=0;j<3;++j)put16(sg_host(a[0])+2*j,(uint16_t)sg_next());break;
    case 1:
        eax=pick%4?eax&0xffffff00u:eax|1u<<(pick>>8)%8;
        if((pick>>12)%4==0)put32(sg_host(a[2])+4*((pick>>14)%3),sg_next());
        break;
    case 2:if(pick%2&&sg_inside(a[5]))for(unsigned j=0;j<3;++j)put32(sg_host(a[5])+4*j,sg_next());break;
    case 3: {
        unsigned mode=pick%16;eax=mode<12?0:mode==12?2:mode==13?0x100u:mode==14?1:eax;break; }
    case 4: {
        static const uint32_t deltas[]={0,1,0xffffffffu,0x80000000u,0x7fffffffu};
        uint32_t z=get32(sg_host(a[1])+8);eax=pick%6<5?z+deltas[pick%6]:eax;break; }
    case 5: {
        unsigned mode=pick%8;eax=mode<5?eax&0xffffff00u:mode==5?0x100u:eax|1u<<(pick>>8)%8;break; }
    case 6:
        eax=pick%4==0?0:SG_BASE+SG_HIT+(pick>>4)%4;
        for(unsigned j=0;j<16;++j)sg_arena[SG_HIT+j]=(BYTE)sg_next();
        if((pick>>8)%4==0)put32(sg_host(a[1+(pick>>10)%2])+4*((pick>>12)%3),sg_next());
        break;
    case 7:for(unsigned j=0;j<3;++j)put32(sg_host(a[2])+4*j,sg_next());break;
    case 8:
        eax=pick%3?eax&0xffffff00u:eax|1u<<(pick>>8)%8;
        if((uint8_t)eax)for(unsigned j=0;j<3;++j)put32(sg_host(a[4])+4*j,sg_next());
        break;
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define SG_STUB(n) __attribute__((naked)) static void sg_stub_##n(void){__asm__ volatile( \
    "leal 4(%esp),%eax\n\tpushl %eax\n\tpushl $" #n "\n\tcalll _sg_stub_impl\n\taddl $8,%esp\n\tmovl _sg_ecx,%ecx\n\tret\n\t");}
SG_STUB(0) SG_STUB(1) SG_STUB(2) SG_STUB(3) SG_STUB(4) SG_STUB(5) SG_STUB(6) SG_STUB(7) SG_STUB(8) SG_STUB(9)
#undef SG_STUB
static void *sg_stubs[SG_IDS]={sg_stub_0,sg_stub_1,sg_stub_2,sg_stub_3,sg_stub_4,sg_stub_5,sg_stub_6,sg_stub_7,sg_stub_8,sg_stub_9};
/* 49ab6f init byte, 4a0be0 map, 4aaaee..4aaaf9 contact point, 4a0c4c and
 * 6b505c..6b5067 (470798's init byte and result point). */
#define SG_GLOBALS 34
static void sg_globals(BYTE *out,int restore){
    static const unsigned offsets[]={0x9ab6f,0xa0be0,0xaaaee,0xa0c4c,0x2b505c};
    static const unsigned sizes[]={1,4,12,1,16};
    for(unsigned i=0;i<5;++i){if(restore)memcpy(mapped+offsets[i],out,sizes[i]);else memcpy(out,mapped+offsets[i],sizes[i]);out+=sizes[i];}
}
static void sg_reset(void){sg_calls=0;sg_stream=sg_seed;}
static uint32_t sg_coordinate(uint32_t cells,uint32_t shift){
    uint32_t s=shift&31u,cell=1u<<s,index=(uint32_t)(random32()%(cells+4u))-2u;
    switch(random32()%8){
    case 0:return index<<s;
    case 1:return (index<<s)+cell-1u;
    case 2:return random32();
    default:return (index<<s)+(s?random32()%cell:0);
    }
}
/* Grids of at most 64 x 64 cells keep the walk finite. Dimension 31 makes
 * the limit negative, so every start fails the bounds test at once. */
static uint32_t sg_dimension(unsigned n){
    uint32_t d=n%23==0?31u:random32()%7;
    return n%8==0?d|(random32()&0xffffffe0u):d;
}
static void sg_points(uint32_t map,uint32_t start,uint32_t end){
    uint32_t w=1u<<(get32(sg_host(map+0x100))&31u),h=1u<<(get32(sg_host(map+0x104))&31u),s=get32(sg_host(map+0x108));
    if(w>64)w=1;if(h>64)h=1;
    for(unsigned j=0;j<2;++j)put32(sg_host(start+4*j),sg_coordinate(j?h:w,s));
    put32(sg_host(start+8),random32()%4?random32()%4096:random32());
    unsigned shape=random32()%12;
    for(unsigned j=0;j<3;++j)put32(sg_host(end+4*j),j<2?sg_coordinate(j?h:w,s):random32()%3?get32(sg_host(start+8))+random32()%8192-4096:random32());
    uint32_t x=get32(sg_host(start)),y=get32(sg_host(start+4)),d=random32()%(s%32<12?(4u<<(s%32)):65536u);
    switch(shape){
    case 0:put32(sg_host(end),x);break;
    case 1:put32(sg_host(end+4),y);break;
    case 2:put32(sg_host(end),x+d);put32(sg_host(end+4),y+(random32()%2?d:0u-d));break;
    case 3:put32(sg_host(end),x-d);put32(sg_host(end+4),y+(random32()%2?d:0u-d));break;
    case 4:put32(sg_host(end),x);put32(sg_host(end+4),y);break;
    case 5:put32(sg_host(end+8),get32(sg_host(start+8))+(random32()%2?40000u:0u-40000u)+random32()%1024);break;
    default:break;
    }
}
static void sg_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    uint32_t b=SG_BASE;sg_seed=random32();random_bytes(sg_arena,SG_SIZE);
    memset(a,0,24);for(unsigned i=0;i<3;++i)r[i]=random32();
    BYTE *map=sg_arena+SG_MAP;
    put32(map+0x100,sg_dimension(n));put32(map+0x104,sg_dimension(n/3));
    uint32_t shift=random32()%4==0?random32()%32:random32()%13;
    put32(map+0x108,n%16==0?shift|(random32()&0xffffffe0u):shift);
    if(index==ES2_HOOK_INDEX_segment){
        sg_points(b+SG_MAP,b+SG_START,b+SG_END);
        a[0]=b+SG_MAP;a[1]=b+SG_START;a[2]=b+SG_END;a[3]=random32();a[4]=random32()%8?b+SG_OUT:0;
        a[5]=n%3==0?0:n%3==1?1:random32()|(random32()%2?0x100u:0x80000000u);
        return;
    }
    static const uint8_t init[]={0,1,0x80,0xff};
    put32(sg_arena+SG_SEG,b+SG_MAT);put32(sg_arena+SG_SEG+4,n<64?(uint32_t)n-32u:random32()%3?random32()%60000:random32());
    put32(mapped+0xa0be0,random32()%8?b+SG_MAP:random32());mapped[0x9ab6f]=init[random32()%4];
    a[0]=b+SG_WOUT;a[1]=b+SG_SEG;
}
static int sg_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const sg_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,sg_calls);
    for(unsigned j=0;j<calls&&j<sg_calls;++j)if(memcmp(expected+j,sg_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(sg_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<SG_SIZE;++j)if(sg_after[j]!=sg_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,sg_after[j],sg_arena[j]);break;}
    return 1;
}
static unsigned sg_expected_hits(uint32_t key){
    unsigned hits=0;
    for(unsigned j=0;j<sg_calls;++j)if(sg_addresses[sg_log[j].id]==key)++hits;
    return hits;
}
static int sg_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[SG_GLOBALS],after[SG_GLOBALS],actual[SG_GLOBALS];static sg_record expected[SG_MAX];
    memcpy(sg_before,sg_arena,SG_SIZE);sg_globals(before,0);
    sg_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=sg_calls;memcpy(expected,sg_log,calls*sizeof(*expected));
    memcpy(sg_after,sg_arena,SG_SIZE);sg_globals(after,0);
    memcpy(sg_arena,sg_before,SG_SIZE);sg_globals(before,1);sg_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);sg_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=sg_expected_hits(c[i].key))return fail("segment callee counter delta");
    if(pair[0]!=pair[1]||calls!=sg_calls||memcmp(expected,sg_log,calls*sizeof(*expected))||memcmp(sg_after,sg_arena,SG_SIZE)||memcmp(after,actual,SG_GLOBALS)){
        for(unsigned j=0;j<SG_GLOBALS;++j)if(after[j]!=actual[j])fprintf(stderr," global byte %u %02x/%02x\n",j,after[j],actual[j]);
        return sg_report("direct",n,pair,calls,expected);
    }
    return 0;
}
static int sg_patch_all(BYTE saved[SG_IDS][5],int active[SG_IDS],const int *keep){
    for(unsigned i=0;i<SG_IDS;++i){active[i]=0;if(keep&&keep[i])continue;if(actor_patch(sg_addresses[i]-0x400000u,sg_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
    return 0;
}
static int sg_unpatch_all(BYTE saved[SG_IDS][5],const int active[SG_IDS]){
    for(unsigned i=SG_IDS;i-->0;)if(active[i]&&actor_patch(sg_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    return 0;
}
/* A six-slot caller over 3840 bytes of patterned stack. */
__attribute__((naked)) static void sg_poison_call(void *target,const uint32_t *a,uint32_t pattern,uint32_t pair[2]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 16(%ebp),%eax\n\tmovl $1024,%ecx\n\trep stosl\n\taddl $3840,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 20(%esi)\n\tpushl 16(%esi)\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%eax\n\tmovl %eax,%edx\n\tmovl %eax,%ecx\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%ecx\n\tmovl %eax,(%ecx)\n\tmovl %edx,4(%ecx)\n\tleal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static int sg_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;sg_build(index,n,a,r);
        BYTE before[SG_GLOBALS],after[SG_GLOBALS],actual[SG_GLOBALS],expected_fpu[108],actual_fpu[108];static sg_record expected[SG_MAX];
        memcpy(sg_before,sg_arena,SG_SIZE);sg_globals(before,0);
        sg_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=sg_calls;memcpy(expected,sg_log,calls*sizeof(*expected));memcpy(sg_after,sg_arena,SG_SIZE);sg_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("segment poison install");
        memcpy(sg_arena,sg_before,SG_SIZE);sg_globals(before,1);sg_reset();sg_poison_call(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);sg_globals(actual,0);
        if(memcmp(expected_pair,actual_pair,8)||calls!=sg_calls||memcmp(expected,sg_log,calls*sizeof(*expected))||memcmp(sg_after,sg_arena,SG_SIZE)||
           memcmp(after,actual,SG_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            status=sg_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        }
        if(!es2_hook_uninstall(&h))return fail("segment poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state\n",d->name,total);
    return status;
}
/* Real callees on a terrain of up to 8 x 8 cells (16-byte cells with a
 * margin around the array). Nested hooks run in rotating modes; the hook
 * under test runs original, then replaced, both on a patterned stack. */
static void sg_real_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    uint32_t b=SG_BASE;sg_seed=random32();random_bytes(sg_arena,SG_SIZE);
    memset(a,0,24);for(unsigned i=0;i<3;++i)r[i]=random32();
    BYTE *map=sg_arena+SG_MAP;uint32_t w=random32()%4,h=random32()%4,shift=6+random32()%6;
    put32(map+0x100,w);put32(map+0x104,h);put32(map+0x108,shift);
    put32(map+0xec,b+SG_CELLS+0x200);put32(map+0x110,random32()%4096);put32(map+0x118,random32()%64);
    /* The terrain builders store flags & 3 as 0, 1 or 2 only (46c13b, 46c1bc;
     * later writers keep bit 1). Flags 3 would make 46ff90 read its
     * uninitialized local -18h as a pointer. */
    for(unsigned j=0;j<(16u<<(w+h));++j)if(j%16==15)sg_arena[SG_CELLS+0x200+j]=(BYTE)((random32()&0xfcu)|random32()%3);
    sg_points(b+SG_MAP,b+SG_START,b+SG_END);
    /* Heights are byte*scale+offset, at most about 20,000. */
    put32(sg_arena+SG_START+8,random32()%24000);if(random32()%4)put32(sg_arena+SG_END+8,random32()%24000);
    if(index==ES2_HOOK_INDEX_segment){
        a[0]=b+SG_MAP;a[1]=b+SG_START;a[2]=b+SG_END;a[3]=random32()%2?random32()%512:random32();a[4]=random32()%8?b+SG_OUT:0;a[5]=n%2;
        return;
    }
    /* The wrapper walks from the matrix translation along its Y axis. */
    for(unsigned j=0;j<9;++j)put16(sg_arena+SG_MAT+2*j,(uint16_t)(random32()%3?random32()%0x8000-0x4000:random32()));
    memcpy(sg_arena+SG_MAT+0x14,sg_arena+SG_START,12);
    put32(sg_arena+SG_SEG,b+SG_MAT);put32(sg_arena+SG_SEG+4,random32()%(64u<<shift));put16(sg_arena+SG_SEG+8,(uint16_t)random32());
    put32(mapped+0xa0be0,b+SG_MAP);mapped[0x9ab6f]=(BYTE)(random32()%2);
    a[0]=b+SG_WOUT;a[1]=b+SG_SEG;
}
static int sg_real(unsigned index){
    static const unsigned nested[]={ES2_HOOK_INDEX_terrain_height,ES2_HOOK_INDEX_dist3d_delta,ES2_HOOK_INDEX_transform_point,
        ES2_HOOK_INDEX_dist_gate,ES2_HOOK_INDEX_segment};
    enum { NESTED=sizeof(nested)/sizeof(nested[0]) };
    /* Nested hooks this caller must reach: the walk needs terrain height,
     * the wrapper additionally its transform, distance and the walk. */
    static const int required[2][NESTED]={{1,0,0,0,0},{1,0,1,1,1}};
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0,hits=0;uint64_t entered[NESTED]={0};
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<2048&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;sg_real_build(index,n,a,r);
        BYTE before[SG_GLOBALS],after[SG_GLOBALS],actual[SG_GLOBALS];
        memcpy(sg_before,sg_arena,SG_SIZE);sg_globals(before,0);
        es2_hook hk[NESTED+1];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<NESTED;++j){if(nested[j]==index)continue;o.mode=(es2_hook_mode)((mode+j)%3);
            if(nested[j]==ES2_HOOK_INDEX_segment&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,function_hook_extension(nested[j])))return fail("segment nested install");}
        sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        memcpy(sg_after,sg_arena,SG_SIZE);sg_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+NESTED,d,&o,function_hook_extension(index)))return fail("segment caller mode install");
        memcpy(sg_arena,sg_before,SG_SIZE);sg_globals(before,1);
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);sg_globals(actual,0);
        /* Real callees can leave EDX pointing into their own frames, so it
         * depends on stack depth; EDX is dead at every caller (weapons.md). */
        if(expected_pair[0]!=actual_pair[0]||memcmp(sg_after,sg_arena,SG_SIZE)||memcmp(after,actual,SG_GLOBALS)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            fprintf(stderr,"real mode=%u\n",mode);sg_calls=0;status=sg_report("real",n,pair,0,NULL);
        }
        hits+=(uint8_t)expected_pair[0]!=0;
        for(unsigned j=0;j<=NESTED;++j){if(j<NESTED)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("segment nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("segment nested uninstall");}
        sg_globals(before,1);++total;
    }
    for(unsigned j=0;j<NESTED&&!status;++j)if(required[index==ES2_HOOK_INDEX_sweep_terrain][j]&&!entered[j]){
        fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&(!hits||hits==total))status=fail("segment real cases without both outcomes");
    if(!status)printf("PASS: %s %u real-callee cases on generated terrain (%u contacts), mixed nested modes, 31/a7 stack, EAX and memory\n",d->name,total,hits);
    return status;
}
static int segment_cases(unsigned index,unsigned limit){
    BYTE saved[SG_IDS][5],old_globals[SG_GLOBALS];int active[SG_IDS];
    /* The walk itself is callee 8 of the wrapper. */
    static const int keep_walk[SG_IDS]={0,0,0,0,0,0,0,0,1,0};
    sg_globals(old_globals,0);if(sg_patch_all(saved,active,index==ES2_HOOK_INDEX_segment?keep_walk:NULL))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_SEGMENT_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];sg_build(index,n,a,r);status=sg_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=sg_installed_probes(index,64);
    if(sg_unpatch_all(saved,active))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=sg_real(index);
    sg_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered full-slot calls, frame payloads by content, out/arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
