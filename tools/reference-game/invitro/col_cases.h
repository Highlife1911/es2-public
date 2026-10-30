/* COL traversal: col_query 40c9d4, col_group 40c8fc, col_shape 40c8c8 and
 * col_bound 40c4c4 (docs/reverse-engineering/hit_tests.md, section "COL
 * traversal"). Direct cases run the original body
 * and the core against logging stubs for every game callee of the hook under
 * test (the group's own nested functions included): full argument slots and
 * entry EAX/EDX/ECX, the matrix that 4a980c points to when col_shape is called
 * (by content), the center that 4801d4 receives. The stubs answer from a
 * per-case stream, near the thresholds that the caller tests (AX zero or not,
 * the unsigned Y bound, the signed distance bound, node null / index -1), and
 * poke words the original reads later (group and shape counts, the shapes
 * pointer, region words, flag words, 4a9810/4a9814, the node table pointer) or
 * must not reread (the groups pointer, [actor+34h]). Stack values (the
 * original's frame addresses, the adapter's scratch) are compared only as "a
 * stack address", also the dword 4a980c in the compared globals 4a980c..4a9823.
 * Then installed 31/a7 poison probes and a real-callee pass: the real node
 * lookups 47622c/476850 over an own node tree, the real 4801d4, and the nested
 * hooks (the group's own, matrix_compose, sphere_hits, sphere_test,
 * dist_octagonal) in rotating modes, the hook original or replaced. col_bound's
 * unsigned Y bound (JBE, site 40c4f2) is an ordinary BRANCH line of
 * branch_sites.h: the coverage window of the original runs must show both
 * outcomes (the fall-through edge is the call of 47de4c). */
typedef struct cq_record { uint32_t id,args[4],regs[3],input[10]; } cq_record;
enum { CQ_SIZE=0x800, CQ_COL=0x000, CQ_GROUPS=0x020, CQ_ACTOR=0x080, CQ_INSTANCE=0x0c0, CQ_LOOKUP=0x0e0,
    CQ_CHILD0=0x100, CQ_CHILD1=0x110, CQ_CHILDREN=0x120, CQ_VTABLE=0x130, CQ_VTLEAF=0x160, CQ_NODE=0x190,
    CQ_SEGMENT=0x1a0, CQ_CENTER=0x1b0, CQ_MATRIX=0x1c0, CQ_NODES=0x200, CQ_SHAPES=0x300, CQ_FLAGS=0x480,
    CQ_SPHERES=0x500, CQ_MAX=48, CQ_IDS=8, CQ_GLOBALS=24 };
#define CQ_B ((uint32_t)(uintptr_t)cq_arena)
static BYTE cq_arena[CQ_SIZE],cq_before[CQ_SIZE],cq_after[CQ_SIZE];
static cq_record cq_log[CQ_MAX],cq_expected[CQ_MAX];
static unsigned cq_calls;
static uint32_t cq_seed,cq_stream,cq_radius;
__attribute__((used)) static uint32_t cq_ecx;
/* 40c8fc, V20 (node lookup), 47fa20, 40c8c8, 40c4c4, 40c524, 4801d4, 47de4c. */
static const uint32_t cq_addresses[CQ_IDS]={0x40c8fc,0,0x47fa20,0x40c8c8,0x40c4c4,0x40c524,0x4801d4,0x47de4c};
static const unsigned cq_counts[CQ_IDS]={4,2,3,1,2,2,3,2};
static uint32_t cq_next(void){
    uint32_t z=cq_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *cq_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
/* The worker's 16 MiB stack reservation: frame addresses of both runs. */
static int cq_window(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
/* A value whose high word is that of a stack address (the low word may be
 * a region word, col_query's EDX). */
static int cq_stackish(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;
    return top+0x10000u-(value&0xffff0000u)-1u<0x1010000u;
}
static uint32_t cq_mask(uint32_t v){return cq_window(v)?0x57ac57acu:v;}
static int cq_region(void){return (int)(random32()%64u)-16;}
static uint32_t cq_in_arena(uint32_t a,unsigned size){return a-CQ_B<=CQ_SIZE-size;}
__attribute__((used,noinline)) static uint64_t __cdecl cq_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(cq_calls==CQ_MAX){fail("col synthetic call overflow");ExitProcess(1);}
    cq_record *v=cq_log+cq_calls++;memset(v,0,sizeof(*v));v->id=id;
    for(unsigned j=0;j<cq_counts[id];++j)v->args[j]=cq_mask(a[j]);
    for(unsigned j=0;j<3;++j)v->regs[j]=cq_mask(regs[j]);
    uint32_t eax=cq_next(),edx=cq_next(),pick=cq_next();cq_ecx=cq_next();
    const uint32_t b=CQ_B;
    switch(id){
    case 0: /* col_group: may record a hit, change the count (reread) or
             * the groups pointer (read once). */
        if(pick%3==0)put32(mapped+0xa9818,(pick>>8)%2?0:cq_next());
        if(pick%5==0)put16(mapped+0xa981c,(uint16_t)cq_next());
        if(pick%7==0)put16(cq_arena+CQ_COL,(uint16_t)(cq_next()%4u));
        if(pick%11==0)put32(cq_arena+CQ_COL+2,cq_next());
        break;
    case 1: { /* node lookup: null or the node, its index word around -1;
               * may change the node table pointer (read after the call)
               * or [actor+34h] (read before, not again). */
        static const uint16_t indexes[]={0xffff,0xfffe,0x8000,0,1,3,0x7fff};
        eax=pick%4==0?0:b+CQ_NODE;
        put16(cq_arena+CQ_NODE+4,(pick>>4)%8?indexes[(pick>>8)%7]:(uint16_t)cq_next());
        if(pick%5==0)put32(cq_arena+CQ_INSTANCE+0x16,cq_next());
        if(pick%7==0)put32(cq_arena+CQ_ACTOR+0x34,cq_next());
        break; }
    case 2: { /* matrix_compose: the output by stream; may move the shapes
               * pointer or the shape count (both read after the call). */
        BYTE *out=cq_host(a[2]);for(unsigned j=0;j<32;j+=4)put32(out+j,cq_next());
        if(pick%5==0)put32(cq_arena+CQ_GROUPS+6,b+CQ_SHAPES+0x10u*(cq_next()%4u));
        if(pick%7==0)put16(cq_arena+CQ_GROUPS+4,(uint16_t)(cq_next()%5u));
        break; }
    case 3: { /* col_shape: the matrix 4a980c points to, by content; result
               * zero, high word only, or any; may change the shape count,
               * a later region word or a flag word. */
        uint32_t m=get32(mapped+0xa980c);v->input[8]=cq_mask(m);
        for(unsigned j=0;j<8;++j)v->input[j]=get32(cq_host(m)+4*j);
        eax=pick%3==0?0:pick%3==1?(pick&0xffff0000u)|0x10000u:eax;
        if(pick%5==0)put16(cq_arena+CQ_GROUPS+4,(uint16_t)(cq_next()%6u));
        if(pick%7==0)put16(cq_arena+CQ_SHAPES+0x10u*(cq_next()%6u)+6,(uint16_t)(cq_next()%64u-16u));
        if(pick%11==0)put16(cq_arena+CQ_FLAGS+2*((int)(cq_next()%64u)-16),(uint16_t)(cq_next()%2?0:cq_next()));
        break; }
    case 4: /* col_bound: AX zero (high word set or not) or nonzero. */
        eax=pick%3==0?eax&0xffff0000u:pick%3==1?0:eax|((pick>>8)%2?1u:0x8000u);
        break;
    case 5: /* sphere_hits: zero or a length + 1. */
        if(pick%3==0)eax=0;
        break;
    case 6: { /* 4801d4: the center by content; may change the length or the
               * probe radius (read after the call); Y around the bound. */
        if(cq_in_arena(a[0],6))for(unsigned j=0;j<3;++j)v->input[j]=get16(cq_host(a[0])+2*j);
        if(pick%13==0)put32(mapped+0xa9810,cq_next());
        if(pick%17==0)put16(mapped+0xa9814,(uint16_t)cq_next());
        uint32_t bound=(uint32_t)(int32_t)(int16_t)cq_radius+get32(mapped+0xa9810),y;
        switch((pick>>4)%7){case 0:y=bound-1u;break;case 1:y=bound;break;case 2:y=bound+1u;break;
            case 3:y=cq_next()%(bound|1u);break;case 4:y=0x80000000u|cq_next();break;default:y=cq_next();}
        BYTE *out=cq_host(a[2]);
        put32(out,(pick>>8)&1?cq_next():cq_next()%8192u-4096u);put32(out+4,y);put32(out+8,(pick>>9)&1?cq_next():cq_next()%8192u-4096u);
        break; }
    case 7: { /* dist_octagonal: around r + R (signed), negative values; may
               * change the probe radius (read after the call). */
        if(pick%13==0)put16(mapped+0xa9814,(uint16_t)cq_next());
        uint32_t sum=(uint32_t)(int32_t)(int16_t)cq_radius+(uint32_t)(int32_t)(int16_t)get16(mapped+0xa9814);
        static const uint32_t fixed[]={0,0x7fff,0x8000,0x80000000u,0xffffffffu,0x10000u};
        switch((pick>>4)%6){case 0:eax=sum-1u;break;case 1:eax=sum;break;case 2:eax=sum+1u;break;
            case 3:eax=fixed[(pick>>8)%6];break;case 4:eax=cq_next()%0x10000u;break;default:break;}
        break; }
    }
    return (uint64_t)edx<<32|eax;
}
/* Pushes the entry EAX/EDX/ECX for the record before the slot pointer. */
#define CQ_STUB(n) __attribute__((naked)) static void cq_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _cq_stub_impl\n\taddl $24,%esp\n\tmovl _cq_ecx,%ecx\n\tret\n\t");}
CQ_STUB(0) CQ_STUB(1) CQ_STUB(2) CQ_STUB(3) CQ_STUB(4) CQ_STUB(5) CQ_STUB(6) CQ_STUB(7)
#undef CQ_STUB
static void *cq_stubs[CQ_IDS]={cq_stub_0,cq_stub_1,cq_stub_2,cq_stub_3,cq_stub_4,cq_stub_5,cq_stub_6,cq_stub_7};
/* The statics 4a980c..4a9823; the matrix pointer as "a stack address". */
static void cq_globals(BYTE *out,int restore){
    if(restore)memcpy(mapped+0xa980c,out,CQ_GLOBALS);
    else {memcpy(out,mapped+0xa980c,CQ_GLOBALS);if(cq_window(get32(out)))put32(out,0x57ac57acu);}
}
static void cq_reset(void){cq_calls=0;cq_stream=cq_seed;}
static uint16_t cq_word(const uint16_t *values,unsigned count){return random32()%4?values[random32()%count]:(uint16_t)random32();}
/* A realistic matrix: small coefficients, mode byte 0..2, translation. */
static void cq_matrix(BYTE *mx){
    for(unsigned j=0;j<9;++j)put16(mx+2*j,(uint16_t)(random32()%32769u-16384u));
    mx[18]=(BYTE)(random32()%3);
    for(unsigned j=0;j<3;++j)put32(mx+20+4*j,random32()%4096u-2048u);
}
/* Shape k of the pool for the real pass: spheres, region 0..3, bound. */
static void cq_real_shape(unsigned k){
    BYTE *s=cq_arena+CQ_SHAPES+0x10u*k;uint32_t spheres=CQ_B+CQ_SPHERES+0x18u*k;
    put16(s,(uint16_t)(random32()%4));put32(s+2,spheres);put16(s+6,(uint16_t)(random32()%4));
    for(unsigned j=0;j<3;++j)put16(s+8+2*j,(uint16_t)(random32()%2048u-1024u));put16(s+14,(uint16_t)(random32()%3000));
    for(unsigned i=0;i<3;++i){BYTE *p=cq_host(spheres+8u*i);
        for(unsigned j=0;j<3;++j)put16(p+2*j,(uint16_t)(random32()%1024u-512u));put16(p+6,(uint16_t)(random32()%1500));}
}
/* Group g of the real pass: part word (id or negative), shapes k..k+n-1. */
static void cq_real_group(unsigned g,unsigned k,unsigned n){
    static const uint16_t parts[]={0,1,2,3,0xffff,0x8000};
    BYTE *p=cq_arena+CQ_GROUPS+10u*g;put16(p,parts[random32()%6]);put16(p+4,(uint16_t)n);put32(p+6,CQ_B+CQ_SHAPES+0x10u*k);
    for(unsigned i=0;i<n;++i)cq_real_shape(k+i);
}
static void cq_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    const uint32_t b=CQ_B;
    cq_seed=random32();random_bytes(cq_arena,CQ_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    random_bytes(mapped+0xa980c,CQ_GLOBALS);
    put32(cq_arena+CQ_ACTOR+0x34,b+CQ_INSTANCE);put32(cq_arena+CQ_INSTANCE+4,b+CQ_LOOKUP);
    put32(cq_arena+CQ_LOOKUP,b+CQ_VTABLE);
    if(real){
        /* The node tree: the lookup object L (group class 476850) with up
         * to two leaf children (47622c); ids 0..3, node index words -1..3. */
        put32(cq_arena+CQ_VTABLE+0x20,(uint32_t)(uintptr_t)(mapped+0x76850));
        put32(cq_arena+CQ_VTLEAF+0x20,(uint32_t)(uintptr_t)(mapped+0x7622c));
        put16(cq_arena+CQ_LOOKUP+4,(uint16_t)(random32()%5u-1u));put16(cq_arena+CQ_LOOKUP+6,(uint16_t)(random32()%4));
        put16(cq_arena+CQ_LOOKUP+0x10,(uint16_t)(random32()%3));put32(cq_arena+CQ_LOOKUP+0x12,b+CQ_CHILDREN);
        put32(cq_arena+CQ_CHILDREN,b+CQ_CHILD0);put32(cq_arena+CQ_CHILDREN+4,b+CQ_CHILD1);
        for(unsigned c=0;c<2;++c){BYTE *child=cq_arena+(c?CQ_CHILD1:CQ_CHILD0);
            put32(child,b+CQ_VTLEAF);put16(child+4,(uint16_t)(random32()%5u-1u));put16(child+6,(uint16_t)(random32()%4));}
        put32(cq_arena+CQ_INSTANCE+0x16,b+CQ_NODES);
        for(unsigned j=0;j<4;++j)cq_matrix(cq_arena+CQ_NODES+32*j);
        cq_matrix(cq_arena+CQ_MATRIX);
        for(int j=-16;j<48;++j)put16(cq_arena+CQ_FLAGS+2*j,(uint16_t)(random32()%3==0?0:1));
        put32(mapped+0xa980c,b+CQ_MATRIX);put32(mapped+0xa9810,100+random32()%20000);put16(mapped+0xa9814,(uint16_t)(random32()%2000));
        put32(mapped+0xa9818,0);put16(mapped+0xa981c,0xffff); /* as col_query leaves them for col_group */
    } else {
        put32(cq_arena+CQ_VTABLE+0x20,(uint32_t)(uintptr_t)cq_stubs[1]);
        for(int j=-16;j<48;++j)put16(cq_arena+CQ_FLAGS+2*j,(uint16_t)(random32()%2?0:random32()));
    }
    if(index==ES2_HOOK_INDEX_col_query){
        uint32_t count=n%17==0?0:n%29==0?(0x8000u|random32()):n%31==0?0xffffu:1+random32()%5;
        if(real){count=1+random32()%3;unsigned k=0;for(unsigned g=0;g<count;++g){unsigned m=1+random32()%3;cq_real_group(g,k,m);k+=m;}}
        put16(cq_arena+CQ_COL,(uint16_t)count);put32(cq_arena+CQ_COL+2,b+CQ_GROUPS);
        static const uint32_t lengths[]={0,1,0xffffffffu,0x80000000u,0x7fffffffu};
        put32(cq_arena+CQ_SEGMENT+4,real?100+random32()%20000:random32()%3?random32():lengths[random32()%5]);
        put16(cq_arena+CQ_SEGMENT+8,(uint16_t)(real?random32()%2000:random32()));
        a[0]=b+CQ_COL;a[1]=b+CQ_ACTOR;a[2]=b+CQ_SEGMENT;a[3]=real||random32()%4?b+CQ_MATRIX:random32();
        a[4]=real||random32()%4?b+CQ_FLAGS:random32();
    } else if(index==ES2_HOOK_INDEX_col_group){
        static const uint16_t parts[]={0xffff,0x8000,0xfffe,0,1,2,7,0x7fff};
        static const uint16_t counts[]={0,1,2,3,4,5,0x8000,0xffff};
        if(real)cq_real_group(0,0,1+random32()%4);
        else {
            put16(cq_arena+CQ_GROUPS,cq_word(parts,8));put16(cq_arena+CQ_GROUPS+4,n%13==0?counts[random32()%8]:(uint16_t)(random32()%5));
            put32(cq_arena+CQ_GROUPS+6,b+CQ_SHAPES);
            for(unsigned k=0;k<8;++k)put16(cq_arena+CQ_SHAPES+0x10u*k+6,(uint16_t)cq_region());
        }
        a[0]=b+CQ_GROUPS;a[1]=b+CQ_ACTOR;a[2]=b+CQ_MATRIX;a[3]=b+CQ_FLAGS;
    } else if(index==ES2_HOOK_INDEX_col_shape){
        if(real)cq_real_shape(0);
        a[0]=b+CQ_SHAPES;
    } else {
        static const uint16_t radii[]={0,1,0x7fff,0x8000,0xffff,0xfffe};
        uint16_t radius;
        if(real){cq_real_shape(0);radius=get16(cq_arena+CQ_SHAPES+14);}
        else {
            radius=random32()%3?(uint16_t)(random32()%3000):cq_word(radii,6);
            static const uint32_t lengths[]={0,1,0xffffffffu,0x80000000u,0x7fffffffu,0xffff8000u};
            put32(mapped+0xa9810,random32()%3?random32()%40000:lengths[random32()%6]);
            put16(mapped+0xa9814,(uint16_t)(random32()%2?random32()%2000:random32()));
            put32(mapped+0xa980c,random32()%4?b+CQ_MATRIX:random32());
        }
        a[0]=real?b+CQ_SHAPES+8:b+CQ_CENTER;a[1]=(random32()&0xffff0000u)|radius;cq_radius=a[1];
    }
}
static int cq_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const cq_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,cq_calls);
    for(unsigned j=0;j<calls&&j<cq_calls&&expected;++j)if(memcmp(expected+j,cq_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(cq_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<CQ_SIZE;++j)if(cq_after[j]!=cq_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,cq_after[j],cq_arena[j]);break;}
    return 1;
}
static unsigned cq_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<cq_calls;++j){unsigned id=cq_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?id==1&&c->key==0x20:id!=1&&cq_addresses[id]==c->key)++hits;}
    return hits;
}
static int cq_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE raw[CQ_GLOBALS],after[CQ_GLOBALS],actual[CQ_GLOBALS];
    memcpy(cq_before,cq_arena,CQ_SIZE);memcpy(raw,mapped+0xa980c,CQ_GLOBALS);
    cq_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=cq_calls;memcpy(cq_expected,cq_log,calls*sizeof(*cq_expected));
    memcpy(cq_after,cq_arena,CQ_SIZE);cq_globals(after,0);
    memcpy(cq_arena,cq_before,CQ_SIZE);cq_globals(raw,1);cq_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[4]={0};for(unsigned i=0;i<count&&i<4;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);cq_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count&&i<4;++i)if(c[i].count-counts[i]!=cq_expected_hits(c+i))return fail("col callee counter delta");
    if(pair[0]!=pair[1]||calls!=cq_calls||memcmp(cq_expected,cq_log,calls*sizeof(*cq_expected))||memcmp(cq_after,cq_arena,CQ_SIZE)||
       memcmp(after,actual,CQ_GLOBALS)){
        for(unsigned j=0;j<CQ_GLOBALS;++j)if(after[j]!=actual[j])fprintf(stderr," global byte %u %02x/%02x\n",j,after[j],actual[j]);
        return cq_report("direct",n,pair,calls,cq_expected);
    }
    return 0;
}
static int cq_patch(BYTE saved[CQ_IDS][5],int active[CQ_IDS],unsigned which,int on){
    for(unsigned i=0;i<CQ_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!cq_addresses[i])continue;if(actor_patch(cq_addresses[i]-0x400000u,cq_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(cq_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static int cq_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;cq_build(index,n,a,r,0);
        BYTE raw[CQ_GLOBALS],after[CQ_GLOBALS],actual[CQ_GLOBALS],expected_fpu[108],actual_fpu[108];
        memcpy(cq_before,cq_arena,CQ_SIZE);memcpy(raw,mapped+0xa980c,CQ_GLOBALS);
        cq_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=cq_calls;memcpy(cq_expected,cq_log,calls*sizeof(*cq_expected));memcpy(cq_after,cq_arena,CQ_SIZE);cq_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("col poison install");
        memcpy(cq_arena,cq_before,CQ_SIZE);cq_globals(raw,1);cq_reset();sg_poison_call(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);cq_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=cq_calls||memcmp(cq_expected,cq_log,calls*sizeof(*cq_expected))||memcmp(cq_after,cq_arena,CQ_SIZE)||
           memcmp(after,actual,CQ_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=cq_report(fast?"installed fast":"installed preserving",n,pair,calls,cq_expected);
        if(!es2_hook_uninstall(&h))return fail("col poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, pair/trace/arena/globals\n",d->name,total);
    return status;
}
/* EAX exact; EDX exact or both a stack value (sphere residues), then for
 * col_query its low word (the region word) still equal. */
static int cq_real_equal(unsigned index,const uint32_t e[2],const uint32_t f[2]){
    if(e[0]!=f[0])return 0;
    if(e[1]==f[1])return 1;
    if(!cq_stackish(e[1])||!cq_stackish(f[1]))return 0;
    return index!=ES2_HOOK_INDEX_col_query||(uint16_t)e[1]==(uint16_t)f[1];
}
/* Real callees: the node lookups 476850/47622c over the own tree, 4801d4,
 * and the nested hooks in rotating original/replaced modes (all of them
 * refuse compare), the hook original (modes 0-2) or replaced (3-5), over a
 * patterned stack. */
static int cq_real_pass(unsigned index){
    static const unsigned query_set[]={ES2_HOOK_INDEX_col_group,ES2_HOOK_INDEX_col_shape,ES2_HOOK_INDEX_col_bound,
        ES2_HOOK_INDEX_matrix_compose,ES2_HOOK_INDEX_sphere_hits,ES2_HOOK_INDEX_sphere_test,ES2_HOOK_INDEX_dist_octagonal};
    static const unsigned shape_set[]={ES2_HOOK_INDEX_col_bound,ES2_HOOK_INDEX_sphere_hits,
        ES2_HOOK_INDEX_sphere_test,ES2_HOOK_INDEX_dist_octagonal};
    const unsigned *nested=index==ES2_HOOK_INDEX_col_query?query_set:index==ES2_HOOK_INDEX_col_group?query_set+1:
        index==ES2_HOOK_INDEX_col_shape?shape_set:query_set+6;
    unsigned nests=index==ES2_HOOK_INDEX_col_query?7:index==ES2_HOOK_INDEX_col_group?6:index==ES2_HOOK_INDEX_col_shape?4:1;
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[8]={0},hooked=0,replaced=0,hits=0;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;cq_build(index,n,a,r,1);
        BYTE raw[CQ_GLOBALS],after[CQ_GLOBALS],actual[CQ_GLOBALS];
        memcpy(cq_before,cq_arena,CQ_SIZE);memcpy(raw,mapped+0xa980c,CQ_GLOBALS);
        es2_hook hk[8];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<nests;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("col nested install");}
        cq_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        memcpy(cq_after,cq_arena,CQ_SIZE);cq_globals(after,0);
        /* A hit: the best-hit dword 4a9818 (query, group) or EAX (shape, bound). */
        hits+=index==ES2_HOOK_INDEX_col_query||index==ES2_HOOK_INDEX_col_group?get32(after+12)!=0:expected_pair[0]!=0;
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+nests,d,&o,function_hook_extension(index)))return fail("col caller install");
        memcpy(cq_arena,cq_before,CQ_SIZE);cq_globals(raw,1);cq_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);cq_globals(actual,0);
        if(!cq_real_equal(index,expected_pair,actual_pair)||memcmp(cq_after,cq_arena,CQ_SIZE)||memcmp(after,actual,CQ_GLOBALS)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            for(unsigned j=0;j<CQ_GLOBALS;++j)if(after[j]!=actual[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,after[j],actual[j]);break;}
            fprintf(stderr,"real mode=%u\n",mode);cq_calls=0;status=cq_report("real",n,pair,0,NULL);
        }
        for(unsigned j=0;j<=nests;++j){if(j<nests)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("col nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("col nested uninstall");}
        hooked+=hk[nests].calls;replaced+=mode>=3;
        cq_globals(raw,1);++total;
    }
    if(!status&&!hits){fprintf(stderr,"FAIL: %s real pass without a hit\n",d->name);status=1;}
    if(!status&&hooked!=replaced){fprintf(stderr,"FAIL: %s real pass entered %llu of %llu\n",d->name,(unsigned long long)hooked,(unsigned long long)replaced);status=1;}
    for(unsigned j=0;j<nests&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases (%llu hits), real node lookups and 4801d4, %u nested hooks in rotating modes, 31/a7 stack, pair and memory\n",
            d->name,total,(unsigned long long)hits,nests);
        for(unsigned j=0;j<nests;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
static int col_cases(unsigned index,unsigned limit){
    unsigned which=index==ES2_HOOK_INDEX_col_query?0x1u:index==ES2_HOOK_INDEX_col_group?0xcu:index==ES2_HOOK_INDEX_col_shape?0x30u:0xc0u;
    BYTE saved[CQ_IDS][5],old_globals[CQ_GLOBALS];int active[CQ_IDS];
    memcpy(old_globals,mapped+0xa980c,CQ_GLOBALS);if(cq_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_COL_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];cq_build(index,n,a,r,0);status=cq_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=cq_installed_probes(index,64);
    if(cq_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=cq_real_pass(index);
    memcpy(mapped+0xa980c,old_globals,CQ_GLOBALS);
    if(!status)printf("PASS: %-20s %u cases; return pair, ordered calls with entry registers and inputs, arena/globals (4a980c as a stack address)\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
