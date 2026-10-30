/* pool_query group: pool_query 404bc0 (ai.md, "pool object query 404bc0").
 * Direct cases run the original body and the core against logging stubs for the
 * two game callees (the pool step 471c70 with a planned sequence, the object
 * query 427da8): ordered calls with their arguments and entry EAX/EDX/ECX
 * (stack addresses masked, as in obstacle_cases.h), and the list 427da8
 * receives by content. Objects use a node stride of 8 + 238h. One case in 128
 * (and cases 16..47) list 139 to 310 objects, so the list overflows: the 141st
 * append overwrites the cursor slot EBP-10h and the later ones land in the
 * 141st object and the memory after it; 427da8's view of entries 0..142 must
 * match (entry 142's low word is the original's uninitialized EBP-8 and is
 * masked). Then installed 31/a7 poison probes and a real-callee pass with the
 * nested hooks in rotating modes over a real pool, real grids and a segment
 * record built by the original math functions; half of its cases enter through
 * object_line 404ca0, the only caller. */
typedef struct pqy_record { uint32_t id,args[4],regs[3],eax,edx; } pqy_record;
enum { PQY_HEAD=0x1000, PQY_STRIDE=0x240, PQY_OBJECTS=312, PQY_SIZE=PQY_HEAD+PQY_STRIDE*PQY_OBJECTS,
    PQY_POOL=0x40, PQY_CFG=0x60, PQY_REC=0x160, PQY_INFO=0x1a0, PQY_OUT=0x2a0, PQY_SEG=0x2c0, PQY_POSE=0x300, PQY_ANGLES=0x320,
    PQY_BACK=0x330, PQY_POINTS=0x340, PQY_HOLD=0x360, PQY_GRID=0x3a0, PQY_GRID_SIZE=0x4a0, PQY_MAX=320, PQY_IDS=2,
    PQY_GLOBALS=8, PQY_LISTED=143 };
#define PQY_BASE ((uint32_t)(uintptr_t)pqy_arena)
/* Object i with its 8-byte pool node header in front of it. */
#define PQY_OBJECT(i) (PQY_BASE+PQY_HEAD+PQY_STRIDE*(uint32_t)(i)+8u)
static BYTE pqy_arena[PQY_SIZE],pqy_before[PQY_SIZE],pqy_after[PQY_SIZE];
static pqy_record pqy_log[PQY_MAX];
static unsigned pqy_calls,pqy_cursor,pqy_count,pqy_used,pqy_listed,pqy_objects;
static uint32_t pqy_seed,pqy_stream,pqy_seq[PQY_OBJECTS],pqy_list[PQY_LISTED];
__attribute__((used)) static uint32_t pqy_ecx;
/* 0 471c70 pool step, 1 427da8 object query. */
static const uint32_t pqy_addresses[PQY_IDS]={0x471c70,0x427da8};
static const unsigned pqy_counts[PQY_IDS]={2,4};
static uint32_t pqy_next(void){
    uint32_t z=pqy_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *pqy_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static void pqy_globals(BYTE *out,int restore){
    if(restore){memcpy(mapped+0xa95a0,out,4);memcpy(mapped+0xaaad0,out+4,4);}
    else {memcpy(out,mapped+0xa95a0,4);memcpy(out+4,mapped+0xaaad0,4);}
}
/* Team bytes around the signed-word filter: small, 7Fh/80h/FFh. */
static BYTE pqy_team(uint32_t pick){static const BYTE teams[]={0,1,2,3,0x7f,0x80,0xff,0x82};return pick%4?teams[pick%8]:(BYTE)(pick>>8);}
__attribute__((used,noinline)) static uint64_t __cdecl pqy_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(pqy_calls==PQY_MAX){fail("pool_query synthetic call overflow");ExitProcess(1);}
    pqy_record *v=pqy_log+pqy_calls++;memset(v,0,sizeof(*v));v->id=id;
    for(unsigned k=0;k<pqy_counts[id];++k)v->args[k]=ai_stack_address(a[k])?0:a[k];
    for(unsigned j=0;j<3;++j)v->regs[j]=ai_stack_address(regs[j])?0:regs[j];
    uint32_t eax=pqy_next(),edx=pqy_next(),pick=pqy_next();pqy_ecx=pqy_next();
    if(id==0){
        /* Pool step: the planned objects, then 0; the pool pointer and a
         * later object's filter fields may change. */
        eax=pqy_cursor<pqy_count?pqy_seq[pqy_cursor]:0;if(eax)++pqy_cursor;
        if(pick%13==0)put32(mapped+0xa95a0,PQY_BASE+PQY_POOL+(pick>>8&4));
        if(eax&&pick%7==1&&pqy_cursor<pqy_count){BYTE *o=pqy_host(pqy_seq[pqy_cursor]);
            switch(pick>>8&3){
            case 0:o[0x99]^=1;break;
            case 1:put32(o+0x1f2,PQY_BASE+PQY_CFG+0x20u*(pick>>12&7));break;
            default:pqy_arena[PQY_INFO+0x20u*(pick>>12&7)+0x12]=pqy_team(pick>>16);break;
            }}
    } else {
        /* Object query: out, record, the list by content, the count slot
         * (its high word is the last pool step's EDX); entry 142's low word
         * is the uninitialized EBP-8. A hit writes out and sets AX. */
        unsigned n=(uint16_t)a[3];if(n>PQY_LISTED)n=PQY_LISTED;pqy_listed=n;
        for(unsigned j=0;j<n;++j){uint32_t e=get32(pqy_host(a[2])+4*j);if(j==142)e&=0xffff0000u;pqy_list[j]=ai_stack_address(e)?0:e;}
        if(pick%2){eax=(eax&0xffff0000u)|(pick>>8&0xffffu?pick>>8&0xffffu:1u);put32(pqy_host(a[0]),pqy_next());put32(pqy_host(a[0])+4,pqy_next());}
        else eax&=0xffff0000u;
    }
    v->eax=ai_stack_address(eax)?0:eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define PQY_STUB(n) __attribute__((naked)) static void pqy_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _pqy_stub_impl\n\taddl $24,%esp\n\tmovl _pqy_ecx,%ecx\n\tret\n\t");}
PQY_STUB(0) PQY_STUB(1)
#undef PQY_STUB
static void *pqy_stubs[PQY_IDS]={pqy_stub_0,pqy_stub_1};
static void pqy_reset(void){pqy_calls=0;pqy_cursor=0;pqy_listed=0;pqy_stream=pqy_seed;}
static uint16_t pqy_word(const uint16_t *values,unsigned count){return values[random32()%count];}
/* Filter fields: configurations (+4, +6), records (+5), team bytes (+12h
 * of the info block at +45h) and the dead byte +99h around their tests. */
static void pqy_configs(void){
    static const uint16_t levels[]={0,1,2,0xffff,0x8000,0x7fff,3};
    for(unsigned i=0;i<8;++i){BYTE *c=pqy_arena+PQY_CFG+0x20*i;
        put16(c+6,i==0?0:random32()%2?0:(uint16_t)(random32()|1u));
        put16(c+4,i==0?0:random32()%3==0?0xffff:(uint16_t)random32());}
    for(unsigned i=0;i<4;++i)put16(pqy_arena+PQY_REC+0x10*i+5,i==0?1:random32()%3?pqy_word(levels,7):(uint16_t)random32());
    for(unsigned i=0;i<8;++i)pqy_arena[PQY_INFO+0x20*i+0x12]=pqy_team(random32());
}
static void pqy_filter(BYTE *o,int take){
    put32(o+0x1f2,PQY_BASE+PQY_CFG+(take?0:0x20u*(random32()%8)));
    put32(o+0x205,PQY_BASE+PQY_REC+(take?0:0x10u*(random32()%4)));
    put32(o+0x45,PQY_BASE+PQY_INFO+0x20u*(random32()%8));
    o[0x99]=(BYTE)(take||random32()%2?0:random32()%2?1:random32());
}
/* The filter slot: a negative word (every team), a team value, or random;
 * the high word is never read. */
static uint32_t pqy_filter_slot(int wide){
    uint32_t high=random32()&0xffff0000u;
    if(wide)return high|0x8000u|random32();
    switch(random32()%4){
    case 0:return high|0x8000u|random32();
    case 1:return high|0xffffu;
    case 2:return high|(uint16_t)(BYTE)pqy_team(random32());
    default:return random32();
    }
}
/* A pool of count nodes in random memory order linked into the used list;
 * the sequence the original's walk returns is the used list backwards. */
static void pqy_pool(unsigned count){
    unsigned order[PQY_OBJECTS];for(unsigned i=0;i<count;++i)order[i]=i;
    for(unsigned i=count;i>1;--i){unsigned j=random32()%i,t=order[i-1];order[i-1]=order[j];order[j]=t;}
    BYTE *p=pqy_arena+PQY_POOL;put16(p,0);put16(p+2,(uint16_t)count);put16(p+4,PQY_STRIDE-8);
    put32(p+6,PQY_BASE+PQY_HEAD);put32(p+0xa,0);put32(p+0xe,count?PQY_OBJECT(order[0])-8u:0);
    for(unsigned i=0;i<count;++i){BYTE *node=pqy_host(PQY_OBJECT(order[i])-8u);
        put32(node,i+1<count?PQY_OBJECT(order[i+1])-8u:0);put32(node+4,i?PQY_OBJECT(order[i-1])-8u:0);
        pqy_seq[count-1-i]=PQY_OBJECT(order[i]);}
}
static void pqy_build(unsigned n,uint32_t a[6],uint32_t r[3]){
    uint32_t b=PQY_BASE;
    unsigned objects,wide=(n>=16&&n<48)||n%128==127;
    if(wide)objects=n>=16&&n<48?139+(n-16)%8+(n>=40?160:0):139+random32()%172;
    else objects=random32()%10;
    pqy_used=PQY_HEAD+PQY_STRIDE*(objects+1u<PQY_OBJECTS?objects+1u:PQY_OBJECTS);
    pqy_seed=random32();random_bytes(pqy_arena,pqy_used);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    pqy_configs();
    for(unsigned i=0;i<objects;++i)pqy_filter(pqy_host(PQY_OBJECT(i)),wide&&(n<48||random32()%16));
    pqy_pool(objects);pqy_count=pqy_objects=objects;
    put32(mapped+0xa95a0,b+PQY_POOL);put32(mapped+0xaaad0,random32());
    /* The planned walk: the used list backwards, sometimes shortened or
     * with an object repeated. */
    if(!wide&&objects&&random32()%5==0)pqy_seq[random32()%objects]=pqy_seq[random32()%objects];
    if(!wide&&random32()%7==0)pqy_count=random32()%(objects+1u);
    a[0]=b+PQY_OUT;a[1]=b+PQY_SEG;a[2]=pqy_filter_slot(wide);
}
static int pqy_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const pqy_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,pqy_calls);
    for(unsigned j=0;j<calls&&j<pqy_calls&&expected;++j)if(memcmp(expected+j,pqy_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(pqy_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<pqy_used;++j)if(pqy_after[j]!=pqy_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,pqy_after[j],pqy_arena[j]);break;}
    return 1;
}
static unsigned pqy_expected_hits(const callee_counter *c){
    unsigned hits=0;for(unsigned j=0;j<pqy_calls;++j)hits+=pqy_addresses[pqy_log[j].id]==c->key;return hits;
}
static int pqy_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[PQY_GLOBALS],after[PQY_GLOBALS],actual[PQY_GLOBALS];static pqy_record expected[PQY_MAX];
    static uint32_t list[PQY_LISTED];
    memcpy(pqy_before,pqy_arena,pqy_used);pqy_globals(before,0);
    pqy_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=pqy_calls,listed=pqy_listed;
    memcpy(expected,pqy_log,calls*sizeof(*expected));memcpy(list,pqy_list,listed*4);
    memcpy(pqy_after,pqy_arena,pqy_used);pqy_globals(after,0);
    memcpy(pqy_arena,pqy_before,pqy_used);pqy_globals(before,1);pqy_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[4];for(unsigned i=0;i<count&&i<4;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);pqy_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count&&i<4;++i)if(c[i].count-counts[i]!=pqy_expected_hits(c+i))return fail("pool_query callee counter delta");
    if(listed!=pqy_listed||memcmp(list,pqy_list,listed*4)){
        for(unsigned j=0;j<listed&&j<pqy_listed;++j)if(list[j]!=pqy_list[j]){fprintf(stderr," list entry %u %08x/%08x\n",j,list[j],pqy_list[j]);break;}
        return pqy_report("listed",n,pair,calls,expected);
    }
    if(pair[0]!=pair[1]||calls!=pqy_calls||memcmp(expected,pqy_log,calls*sizeof(*expected))||memcmp(pqy_after,pqy_arena,pqy_used)||
       memcmp(after,actual,PQY_GLOBALS))
        return pqy_report("direct",n,pair,calls,expected);
    return 0;
}
static int pqy_patch(BYTE saved[PQY_IDS][5],int active[PQY_IDS],int on){
    for(unsigned i=0;i<PQY_IDS;++i){
        if(on){active[i]=0;if(actor_patch(pqy_addresses[i]-0x400000u,pqy_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(pqy_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static int pqy_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;pqy_build(n,a,r);
        BYTE before[PQY_GLOBALS],after[PQY_GLOBALS],actual[PQY_GLOBALS],expected_fpu[108],actual_fpu[108];
        static pqy_record expected[PQY_MAX];static uint32_t list[PQY_LISTED];
        memcpy(pqy_before,pqy_arena,pqy_used);pqy_globals(before,0);
        pqy_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=pqy_calls,listed=pqy_listed;memcpy(expected,pqy_log,calls*sizeof(*expected));memcpy(list,pqy_list,listed*4);
        memcpy(pqy_after,pqy_arena,pqy_used);pqy_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("pool_query poison install");
        memcpy(pqy_arena,pqy_before,pqy_used);pqy_globals(before,1);pqy_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);es2_hook_fpu_save(actual_fpu);pqy_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=pqy_calls||memcmp(expected,pqy_log,calls*sizeof(*expected))||memcmp(pqy_after,pqy_arena,pqy_used)||
           listed!=pqy_listed||memcmp(list,pqy_list,listed*4)||memcmp(after,actual,PQY_GLOBALS)||
           memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=pqy_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("pool_query poison uninstall");pqy_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/list/arena/globals\n",d->name,total);
    return status;
}
/* Real pass. The segment record of 404ca0 (forward pose, length, radius
 * word, inverse pose) built by the original 492990, 47ebb8, 49288c, 47df19
 * and 480304 before any hook is installed; objects near the segment with
 * real height grids (oq_real_grid), a real pool, the filter fields above. */
static uint32_t pqy_signed(uint32_t range){return random32()%(2*range+1)-range;}
static void pqy_real_segment(uint32_t start,uint32_t end,uint16_t radius){
    uint32_t b=PQY_BASE,p=b+PQY_SEG,m=b+PQY_POSE,angles=b+PQY_ANGLES,back=b+PQY_BACK;
    ((uint64_t (__cdecl *)(uint32_t,uint32_t,uint32_t))(void *)(mapped+0x92990))(angles,end,start);
    for(unsigned j=0;j<3;++j)put32(pqy_host(m)+0x14+4*j,get32(pqy_host(start)+4*j));
    ((uint64_t (__cdecl *)(uint32_t,uint32_t))(void *)(mapped+0x7ebb8))(angles,m);
    put32(pqy_host(p),m);
    put32(pqy_host(p)+4,(uint32_t)((uint64_t (__cdecl *)(uint32_t,uint32_t))(void *)(mapped+0x9288c))(end,start));
    put16(pqy_host(p)+8,radius);
    memcpy(pqy_host(p)+0xa,pqy_host(m),20);
    for(unsigned j=0;j<3;++j)put32(pqy_host(p)+0x1eu+4*j,get32(pqy_host(start)+4*j));
    ((uint64_t (__cdecl *)(uint32_t))(void *)(mapped+0x7df19))(p+0xa);
    for(unsigned j=0;j<3;++j){uint32_t v=0u-get32(pqy_host(p)+0x1eu+4*j);put32(pqy_host(p)+0x1eu+4*j,v);put32(pqy_host(back)+4*j,v);}
    ((uint64_t (__cdecl *)(uint32_t,uint32_t,uint32_t))(void *)(mapped+0x80304))(back,p+0xa,p+0x1e);
}
static void pqy_real_build(int line,uint32_t a[6],uint32_t r[3]){
    uint32_t b=PQY_BASE,start=b+PQY_POINTS,end=b+PQY_POINTS+12;unsigned objects=random32()%9;
    pqy_used=PQY_HEAD+PQY_STRIDE*(objects+1u);random_bytes(pqy_arena,pqy_used);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    pqy_configs();
    for(unsigned j=0;j<2;++j)oq_real_grid(b+PQY_GRID+PQY_GRID_SIZE*j);
    uint32_t sx=pqy_signed(20000),sy=pqy_signed(20000),sz=random32()%3000;
    uint32_t ex=sx+pqy_signed(6000),ey=sy+pqy_signed(6000),ez=random32()%3?sz:sz+pqy_signed(1500);
    put32(pqy_host(start),sx);put32(pqy_host(start)+4,sy);put32(pqy_host(start)+8,sz);
    put32(pqy_host(end),ex);put32(pqy_host(end)+4,ey);put32(pqy_host(end)+8,ez);
    uint16_t radius=(uint16_t)(random32()%2?0:random32()%400);
    if(!line)pqy_real_segment(start,end,radius);
    for(unsigned k=0;k<objects;++k){
        uint32_t o=PQY_OBJECT(k),holder=b+PQY_HOLD+8*k;BYTE *q=pqy_host(o);
        pqy_filter(q,random32()%3!=0);
        uint32_t t=random32()%1025;
        put32(q+0x26,sx+(uint32_t)((int32_t)(ex-sx)*(int32_t)t/1024)+pqy_signed(1500));
        put32(q+0x2a,sy+(uint32_t)((int32_t)(ey-sy)*(int32_t)t/1024)+pqy_signed(1500));
        put32(q+0x2e,sz+pqy_signed(1500));
        put16(q+0xc,(uint16_t)(random32()%4?0:pqy_signed(2000)));put16(q+0xe,(uint16_t)(random32()%4?0:pqy_signed(2000)));
        put16(q+0x10,(uint16_t)random32());put16(q+0x32,0);
        if(random32()%4==0){((uint64_t (__cdecl *)(uint32_t,uint32_t))(void *)(mapped+0x7ebb8))(o+0xc,o+0x12);put16(q+0x32,1);}
        put32(q+0x34,holder);put32(pqy_host(holder)+4,b+PQY_GRID+PQY_GRID_SIZE*(random32()%2));
    }
    pqy_pool(objects);put32(mapped+0xa95a0,b+PQY_POOL);
    put32(mapped+0xaaad0,random32()%2?random32()%30000u:random32());
    uint32_t filter=random32()%2?0xffffffffu:pqy_filter_slot(0);
    if(line){a[0]=b+PQY_OUT;a[1]=start;a[2]=end;a[3]=(random32()&0xffff0000u)|radius;a[4]=filter;}
    else {a[0]=b+PQY_OUT;a[1]=b+PQY_SEG;a[2]=filter;}
}
static int pqy_real_pass(unsigned index){
    static const unsigned nested[]={ES2_HOOK_INDEX_pool_prev,ES2_HOOK_INDEX_pool_next,ES2_HOOK_INDEX_object_query,
        ES2_HOOK_INDEX_grid_sweep,ES2_HOOK_INDEX_set_length,ES2_HOOK_INDEX_cell,ES2_HOOK_INDEX_dist3d_delta,
        ES2_HOOK_INDEX_transform_point,ES2_HOOK_INDEX_dist_gate,ES2_HOOK_INDEX_matrix_transpose,ES2_HOOK_INDEX_rotate_point,
        ES2_HOOK_INDEX_euler_matrix,ES2_HOOK_INDEX_dist2d_delta,ES2_HOOK_INDEX_object_line};
    enum { COUNT=sizeof(nested)/sizeof(nested[0]) };
    /* The direct callees every pass must reach. */
    static const unsigned required=0x5;
    const es2_hook_descriptor *d=function_hook_descriptor(index),*line_d=function_hook_descriptor(ES2_HOOK_INDEX_object_line);
    int status=0;unsigned total=0,hits[2]={0},cases[2]={0};uint64_t entered[COUNT]={0},self=0;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<1024&&!status;++n){
        const int line=(int)(n&2);
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;pqy_real_build(line,a,r);
        BYTE before[PQY_GLOBALS],after[PQY_GLOBALS],actual[PQY_GLOBALS];
        BYTE *entry=mapped+(line?line_d->rva:d->rva);
        memcpy(pqy_before,pqy_arena,pqy_used);pqy_globals(before,0);
        es2_hook hk[COUNT+1];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<COUNT;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("pool_query nested install");}
        sg_poison_call(entry,a,pattern,expected_pair);
        memcpy(pqy_after,pqy_arena,pqy_used);pqy_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+COUNT,d,&o,function_hook_extension(index)))return fail("pool_query caller mode install");
        memcpy(pqy_arena,pqy_before,pqy_used);pqy_globals(before,1);
        sg_poison_call(entry,a,pattern,actual_pair);pqy_globals(actual,0);
        /* EDX can be a frame address of 427da8 (or 471c18's tail node when
         * nothing was listed, which is data): dead at object_line's callers. */
        int same=expected_pair[0]==actual_pair[0]&&(expected_pair[1]==actual_pair[1]||
            (ai_stack_address(expected_pair[1])&&ai_stack_address(actual_pair[1])));
        if(!same||memcmp(pqy_after,pqy_arena,pqy_used)||memcmp(after,actual,PQY_GLOBALS)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            fprintf(stderr,"real mode=%u entry=%s\n",mode,line?"object_line":"pool_query");pqy_calls=0;status=pqy_report("real",n,pair,0,NULL);
        }
        hits[line!=0]+=(uint16_t)expected_pair[0]!=0;++cases[line!=0];self+=hk[COUNT].calls;
        for(unsigned j=0;j<=COUNT;++j){if(j<COUNT)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("pool_query nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("pool_query nested uninstall");}
        pqy_globals(before,1);++total;
    }
    for(unsigned j=0;j<COUNT&&!status;++j)if(required>>j&1&&!entered[j]){
        fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    for(unsigned k=0;k<2&&!status;++k)if(!hits[k]||hits[k]==cases[k])status=fail("pool_query real cases without both outcomes");
    if(!status&&!self)status=fail("pool_query real cases never entered the hook");
    if(!status){printf("PASS: %s %u real-callee cases (%u direct with %u hits, %u through object_line with %u hits), mixed nested modes, 31/a7 stack, "
        "EAX (EDX unless frame residue), memory and globals; hook entered %llu times\n",d->name,total,cases[0],hits[0],cases[1],hits[1],(unsigned long long)self);
        for(unsigned j=0;j<COUNT;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
static int pool_query_cases(unsigned index,unsigned limit){
    BYTE saved[PQY_IDS][5],old_globals[PQY_GLOBALS];int active[PQY_IDS];ai_stack_range();
    pqy_globals(old_globals,0);if(pqy_patch(saved,active,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_POOL_QUERY_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];pqy_build(n,a,r);status=pqy_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=pqy_installed_probes(index,64);
    if(pqy_patch(saved,active,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=pqy_real_pass(index);
    pqy_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, the listed entries by content, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
