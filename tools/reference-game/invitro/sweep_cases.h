/* Synthetic swept-query graphs for 426528. Only the private, hash-checked
 * mapping executes original code; actors, lists, matrices and every callee
 * result are generated here. Frame buffers handed to callees are compared by
 * content, never by stack address. */
typedef struct sw_record { uint32_t id,args[5],input[8],state[8],eax,edx; } sw_record;
enum { SW_SIZE=0x1200, SW_P=0x20, SW_SEG=0x40, SW_MAT=0x80, SW_MAT2=0xa0, SW_EFFECTS=0xc0,
    SW_COUNTER=0xd0, SW_EFFECT=0xe0, SW_DEFS=0x100, SW_LIST=0x200, SW_LIST2=0x240, SW_TOKENS=0x300,
    SW_ACTORS=8, SW_SLOTS=12 };
#define SW_DEF(k) (SW_DEFS+0x20u*(k))
#define SW_ACTOR(k) (0x400u+0x1c0u*(k))
#define SW_BASE ((uint32_t)(uintptr_t)sw_arena)
enum { SW_F_OWNER=1, SW_F_MATRIX=2, SW_F_TRANSLATE=4, SW_F_LEN=8, SW_F_SUPPRESS=16, SW_F_COUNTER=32,
    SW_F_COUNT=64, SW_F_LIST=128, SW_F_VTABLE=256, SW_F_TARGET=512, SW_F_DEF=1024, SW_F_FLAG=2048 };
static BYTE sw_arena[SW_SIZE],sw_before[SW_SIZE],sw_after[SW_SIZE];
static sw_record sw_log[96];
static unsigned sw_case,sw_calls,sw_transforms;
static uint32_t sw_seed,sw_stream,sw_flags,sw_current,sw_heading,sw_vtables[2][32];
__attribute__((used)) static uint32_t sw_ecx;
/* Direct callees 0..16, then V20/V50/V64 through vtable A (17..19) and B (20..22). */
static const uint32_t sw_addresses[]={0x49630c,0x426b04,0x48043c,0x47df19,0x480304,0x427d98,0x428048,0x427da0,
    0x423430,0x423974,0x49288c,0x41f790,0x471b30,0x492ee0,0x407f1c,0x4928d0,0x492934};
static const unsigned sw_counts[]={1,2,3,1,3,0,2,0,1,2,2,1,1,1,5,2,2,2,3,2,2,3,2};
/* Mixed output: stubs select boundaries from low bits, which a bare LCG
 * would correlate between consecutive calls. */
static uint32_t sw_next(void){
    uint32_t z=sw_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *sw_host(uint32_t address){return (BYTE *)(uintptr_t)address;}
static int sw_inside(uint32_t address){return address-SW_BASE<SW_SIZE;}
static void sw_copy(sw_record *v,unsigned at,uint32_t address,unsigned count){
    for(unsigned i=0;i<count;++i)v->input[at+i]=get32(sw_host(address)+4*i);
}
__attribute__((used,noinline)) static uint64_t __cdecl sw_stub_impl(unsigned id,const uint32_t *a){
    if(sw_calls==96){fail("sweep synthetic call overflow");ExitProcess(1);}
    sw_record *v=sw_log+sw_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,4*sw_counts[id]);
    uint32_t b=SW_BASE,owner=get32(sw_arena+SW_P+0xe);
    v->state[0]=get32(sw_arena+SW_SEG+4);v->state[1]=get32(sw_arena+SW_P+4);
    v->state[2]=get32(mapped+0xaaa68);v->state[3]=get32(mapped+0xaaa6c);
    for(unsigned k=0;k<SW_ACTORS;++k)v->state[4]|=(uint32_t)(sw_arena[SW_ACTOR(k)+0x9e]!=0)<<k;
    v->state[5]=get32(sw_arena+SW_COUNTER);v->state[6]=get32(sw_arena+SW_SEG+0xa);v->state[7]=get32(sw_arena+SW_SEG+0x26);
    uint32_t eax=sw_next(),edx=sw_next(),pick=sw_next();sw_ecx=sw_next();
    switch(id){
    case 0:__asm__ volatile("movl %%fs:0,%0":"=r"(eax));break;
    case 1:
        v->input[0]=get16(sw_host(a[0]));put16(sw_host(a[0]),(uint16_t)(v->input[0]+a[1]+1u));
        if((sw_flags&SW_F_OWNER)&&a[0]==b+SW_P+6)put32(sw_arena+SW_P+0xe,b+SW_ACTOR(pick%SW_ACTORS));
        break;
    case 2:
        sw_copy(v,0,a[0],3);v->args[0]=v->args[2]=0;
        for(unsigned j=0;j<3;++j)put32(sw_host(a[2])+4*j,sw_next());
        if((sw_flags&SW_F_MATRIX)&&!sw_transforms)put32(sw_arena+SW_SEG,b+SW_MAT2);
        ++sw_transforms;break;
    case 3:
        sw_copy(v,0,a[0],8);for(unsigned j=0;j<20;++j)sw_host(a[0])[j]=(BYTE)sw_next();
        if(sw_flags&SW_F_TRANSLATE)put32(sw_host(a[0])+0x14+4*(pick%3),sw_next());
        break;
    case 4:
        sw_copy(v,0,a[0],3);v->args[0]=0;for(unsigned j=0;j<3;++j)put32(sw_host(a[2])+4*j,sw_next());
        break;
    case 6: {
        BYTE *segment=sw_host(a[1]);v->args[0]=0;
        v->input[0]=get32(segment);v->input[1]=get32(segment+4);v->input[2]=get16(segment+8);
        unsigned mode=pick%8;uint32_t length=get32(segment+4);
        eax=mode<3?eax&0xffff0000u:eax|0x10000u|(1u<<(pick>>28));
        if(mode>=3)eax|=1u<<((pick>>8)%16);
        uint32_t distance=mode==3?length-1u:mode==4?length:mode==5?length+1u:mode==6?0x80000000u:sw_next();
        put32(sw_host(a[0]),distance);put32(sw_host(a[0])+4,(pick>>12)%5?b+SW_TOKENS+(pick>>20)%64:0);
        if(sw_flags&SW_F_LEN)put32(segment+4,sw_next());
        break; }
    case 8:if((sw_flags&SW_F_SUPPRESS)&&sw_current)sw_arena[sw_current+0x99]=(BYTE)(1+pick%255);break;
    case 9:eax=pick%3?b+SW_ACTOR((pick>>8)%SW_ACTORS):0;break;
    case 10: {
        static const uint32_t gates[]={29999,30000,30001,0,0xffffffffu,0x80000000u,0x7fffffffu};
        eax=pick%8<7?gates[pick%8]:sw_next();break; }
    case 12:eax=pick%3?b+SW_EFFECT:0;break;
    case 14: {
        sw_copy(v,0,a[2],3);v->args[2]=0;uint32_t head;__asm__ volatile("movl %%fs:0,%0":"=r"(head));
        v->input[3]=get16(sw_host(head)+16);v->input[4]=get32(sw_host(head)+32);
        put32(sw_arena+SW_COUNTER,get32(sw_arena+SW_COUNTER)+1);
        if(sw_flags&SW_F_COUNTER)put32(mapped+0x2cbee4,b+SW_COUNTER+4);
        break; }
    case 15: {
        static const uint32_t reach[]={0,1,30000,0x7fffffffu,0x80000000u,0xffffffffu};
        sw_copy(v,0,a[1],3);v->args[1]=0;eax=pick%7<6?reach[pick%7]:sw_next();break; }
    case 16: {
        /* The second heading is chosen so that the difference hits the
         * signed 0x2000 gate and its neighbours exactly. */
        static const uint16_t turns[]={0,0x1fff,0x2000,0x2001,0x8000,0x7fff,0xe000};
        if(!sw_inside(a[0])){sw_copy(v,0,a[0],3);v->args[0]=0;sw_heading=eax;}
        else if(pick%8<7)eax=(eax&0xffff0000u)|(uint16_t)(sw_heading-turns[pick%8]);
        break; }
    case 17:case 20: {
        static const uint32_t distances[]={1,499,500,501,0x7fffffffu,0xffffffffu,0x80000000u,2};
        unsigned mode=pick%12;sw_current=a[0]-b;
        eax=mode<4?0:mode<11?distances[mode-4]:sw_next();if(mode==10)eax=get32(sw_arena+SW_SEG+4);
        BYTE *actor=sw_host(a[0]);
        if((sw_flags&SW_F_COUNT)&&(pick>>8)%4==0)put16(mapped+0xa9afe,(uint16_t)((pick>>12)%(SW_SLOTS+1)));
        if((sw_flags&SW_F_LIST)&&(pick>>10)%2)put32(mapped+0xa9af8,b+SW_LIST2);
        if((sw_flags&SW_F_VTABLE)&&(pick>>14)%2)put32(actor,(uint32_t)(uintptr_t)sw_vtables[(pick>>15)&1]);
        if((sw_flags&SW_F_TARGET)&&owner&&(pick>>16)%3==0)put32(sw_host(owner)+0x1a4,a[0]);
        if((sw_flags&SW_F_DEF)&&(pick>>18)%3==0)put32(actor+0x45,b+SW_DEF((pick>>20)%8));
        if((sw_flags&SW_F_FLAG)&&(pick>>23)%4==0)actor[0x99+((pick>>25)%2)*12]=(BYTE)(pick>>26|1);
        /* The hit tests may lower the accumulator [4aaad0h] that 427da0 reads
         * after the loop (427da8 under the slot +20h). */
        if(edx%3==0)put32(mapped+0xaaad0,edx^eax);
        break; }
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define SW_STUB(n) __attribute__((naked)) static void sw_stub_##n(void){__asm__ volatile( \
    "leal 4(%esp),%eax\n\tpushl %eax\n\tpushl $" #n "\n\tcalll _sw_stub_impl\n\taddl $8,%esp\n\tmovl _sw_ecx,%ecx\n\tret\n\t");}
SW_STUB(1) SW_STUB(2) SW_STUB(3) SW_STUB(4) SW_STUB(6) SW_STUB(8) SW_STUB(9) SW_STUB(10)
SW_STUB(11) SW_STUB(12) SW_STUB(13) SW_STUB(14) SW_STUB(15) SW_STUB(16) SW_STUB(17) SW_STUB(18) SW_STUB(19)
SW_STUB(20) SW_STUB(21) SW_STUB(22)
#undef SW_STUB
/* 427d98 and 427da0 are computed in sweep's core, which only reports them to
 * the observer. Here they are counting stand-ins that do what the leaves do
 * (dword [4aaad0h] = EAX = 0; EAX = [4aaad0h]), keep EDX/ECX and log nothing;
 * sw_leaf_expected keeps the original's counts for the callee counter check. */
__attribute__((used)) static uint32_t sw_leaf_hits[2],sw_leaf_slot;
static uint32_t sw_leaf_expected[2];
__attribute__((naked)) static void sw_stub_5(void){__asm__ volatile(
    "incl _sw_leaf_hits\n\txorl %eax,%eax\n\tpushl %ecx\n\tmovl _sw_leaf_slot,%ecx\n\tmovl %eax,(%ecx)\n\tpopl %ecx\n\tret\n\t");}
__attribute__((naked)) static void sw_stub_7(void){__asm__ volatile(
    "incl _sw_leaf_hits+4\n\tmovl _sw_leaf_slot,%eax\n\tmovl (%eax),%eax\n\tret\n\t");}
/* Caller-frame contract of the runtime initializer, not an exception runtime:
 * the record lives at EBP plus the descriptor's frame offset, its first dword
 * keeps the real thread head and FS:0 points at it until the epilogue. */
__attribute__((naked)) static void sw_stub_0(void){__asm__ volatile(
    "pushl %eax\n\tmovl %esp,%eax\n\tpushl %eax\n\tpushl $0\n\tcalll _sw_stub_impl\n\taddl $8,%esp\n\tpopl %ecx\n\t"
    "movl 4(%ecx),%ecx\n\taddl %ebp,%ecx\n\tmovl %eax,(%ecx)\n\tmovw $0,16(%ecx)\n\tmovl %ecx,%fs:0\n\t"
    "movl _sw_ecx,%ecx\n\tret\n\t");}
static void *sw_stubs[]={sw_stub_0,sw_stub_1,sw_stub_2,sw_stub_3,sw_stub_4,sw_stub_5,sw_stub_6,sw_stub_7,sw_stub_8,
    sw_stub_9,sw_stub_10,sw_stub_11,sw_stub_12,sw_stub_13,sw_stub_14,sw_stub_15,sw_stub_16};
static void sw_tables(void){
    sw_leaf_slot=(uint32_t)(uintptr_t)(mapped+0xaaad0);
    void *a[3]={sw_stub_17,sw_stub_18,sw_stub_19},*b[3]={sw_stub_20,sw_stub_21,sw_stub_22};
    memset(sw_vtables,0,sizeof(sw_vtables));
    sw_vtables[0][8]=(uint32_t)(uintptr_t)a[0];sw_vtables[0][20]=(uint32_t)(uintptr_t)a[1];sw_vtables[0][25]=(uint32_t)(uintptr_t)a[2];
    sw_vtables[1][8]=(uint32_t)(uintptr_t)b[0];sw_vtables[1][20]=(uint32_t)(uintptr_t)b[1];sw_vtables[1][25]=(uint32_t)(uintptr_t)b[2];
}
#define SW_GLOBALS 28
static void sw_globals(BYTE *out,int restore){
    static const unsigned offsets[]={0xa9af8,0xa9afe,0xaaa68,0xaaa6c,0xaaad0,0xa961e,0x2cbee4,0xa9e5c};
    static const unsigned sizes[]={4,2,4,4,4,4,4,2};
    for(unsigned i=0;i<8;++i){if(restore)memcpy(mapped+offsets[i],out,sizes[i]);else memcpy(out,mapped+offsets[i],sizes[i]);out+=sizes[i];}
}
static void sw_reset(void){sw_calls=sw_transforms=0;sw_leaf_hits[0]=sw_leaf_hits[1]=0;sw_current=sw_heading=0;sw_stream=sw_seed;}
static void sw_build(unsigned n,uint32_t a[6],uint32_t r[3]){
    static const uint32_t lengths[]={20000,1,499,500,501,0x7fffffffu,0x80000000u,0};
    static const uint16_t counts[]={0,1,3,6,SW_SLOTS,0xffff,0x8000,9};
    uint32_t b=SW_BASE;sw_case=n;sw_seed=random32();sw_flags=random32();random_bytes(sw_arena,SW_SIZE);
    memset(a,0,24);for(unsigned i=0;i<3;++i)r[i]=random32();a[0]=b+SW_P;
    BYTE *p=sw_arena+SW_P,*segment=sw_arena+SW_SEG;
    unsigned owner_index=random32()%(SW_ACTORS+2);
    put32(p,b+SW_SEG);put32(p+0xa,b+SW_EFFECTS);put32(p+0xe,owner_index<SW_ACTORS?b+SW_ACTOR(owner_index):0);
    put32(p+0x14,random32()%3?b+SW_ACTOR(random32()%SW_ACTORS):random32());
    put32(segment,b+SW_MAT);put32(segment+4,n<1024?lengths[(n/3)%8]:random32()%4?random32()%60000:random32());
    for(unsigned k=0;k<8;++k){
        BYTE *d=sw_arena+SW_DEF(k);d[0x12]=(BYTE)(random32()%3+0xff);
        put32(d+0x14,random32()%6?0:random32()|1);
    }
    for(unsigned k=0;k<SW_ACTORS;++k){
        BYTE *x=sw_arena+SW_ACTOR(k);
        put32(x,(uint32_t)(uintptr_t)sw_vtables[0]);put32(x+0x45,b+SW_DEF(random32()%8));
        x[0x99]=random32()%4?0:(BYTE)(random32()|1);x[0x9e]=(BYTE)(random32()%2?0:random32());
        x[0xa2]=random32()%3?0:(BYTE)(random32()|1);x[0xa3]=random32()%2?0:(BYTE)(random32()|1);
        x[0xa4]=random32()%4?0:(BYTE)(random32()|1);x[0xa5]=random32()%4?0:(BYTE)(random32()|1);
        put32(x+0x1a4,random32()%3?b+SW_ACTOR(random32()%SW_ACTORS):0);
        put32(x+0x1b2,random32()%2?b+SW_TOKENS+0x40+k:0);
    }
    for(unsigned list=0;list<2;++list)for(unsigned i=0;i<SW_SLOTS;++i)
        put32(sw_arena+(list?SW_LIST2:SW_LIST)+4*i,random32()%8?b+SW_ACTOR(random32()%SW_ACTORS):0);
    put32(mapped+0xa9af8,b+SW_LIST);put16(mapped+0xa9afe,counts[n<1024?n%8:random32()%8]);
    put32(mapped+0xa961e,random32());put32(mapped+0x2cbee4,b+SW_COUNTER);
    put32(mapped+0xaaa68,random32());put32(mapped+0xaaa6c,random32());put32(mapped+0xaaad0,random32());
    put16(mapped+0xa9e5c,(uint16_t)(random32()%4));
}
static int sw_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const sw_record *expected){
    fprintf(stderr,"FAIL: sweep case %u (%s) pair %016llx/%016llx calls %u/%u\n",n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,sw_calls);
    for(unsigned j=0;j<calls&&j<sw_calls;++j)if(memcmp(expected+j,sw_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(sw_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<SW_SIZE;++j)if(sw_after[j]!=sw_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,sw_after[j],sw_arena[j]);break;}
    return 1;
}
static unsigned sw_expected_hits(uint32_t key,unsigned event){
    unsigned hits=event==ES2_COLLIDE_CALL&&key==0x427d98u?sw_leaf_expected[0]:event==ES2_COLLIDE_CALL&&key==0x427da0u?sw_leaf_expected[1]:0;
    for(unsigned j=0;j<sw_calls;++j){
        unsigned id=sw_log[j].id;
        if(event==ES2_COLLIDE_CALL&&id<17&&sw_addresses[id]==key)++hits;
        if(event==ES2_COLLIDE_SLOT&&id>=17&&(key==0x20u?id%3==2:key==0x50u?id%3==0:id%3==1))++hits;
    }
    return hits;
}
static int sw_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[SW_GLOBALS],after[SW_GLOBALS],actual[SW_GLOBALS];sw_record expected[96];uint32_t head,head_after;
    memcpy(sw_before,sw_arena,SW_SIZE);sw_globals(before,0);
    __asm__ volatile("movl %%fs:0,%0":"=r"(head));
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[24];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    sw_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=sw_calls;memcpy(expected,sw_log,calls*sizeof(*expected));
    /* In the installed pass the replaced hook stands in for the original
     * and computes the two leaves itself: take its observer's counts. */
    memcpy(sw_leaf_expected,sw_leaf_hits,sizeof(sw_leaf_expected));
    if(rng_hooks[index].installed)for(unsigned i=0;i<count;++i)if(c[i].event==ES2_COLLIDE_CALL&&(c[i].key==0x427d98u||c[i].key==0x427da0u))
        sw_leaf_expected[c[i].key==0x427da0u]=(uint32_t)(c[i].count-counts[i]);
    memcpy(sw_after,sw_arena,SW_SIZE);sw_globals(after,0);
    memcpy(sw_arena,sw_before,SW_SIZE);sw_globals(before,1);sw_reset();
    for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);sw_globals(actual,0);
    __asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
    if(head_after!=head)return fail("sweep left FS:0 changed");
    if(n<1024)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=sw_expected_hits(c[i].key,c[i].event))return fail("sweep callee counter delta");
    if(pair[0]!=pair[1]||calls!=sw_calls||memcmp(expected,sw_log,calls*sizeof(*expected))||memcmp(sw_after,sw_arena,SW_SIZE)||memcmp(after,actual,SW_GLOBALS)){
        for(unsigned j=0;j<SW_GLOBALS;++j)if(after[j]!=actual[j])fprintf(stderr," global byte %u %02x/%02x\n",j,after[j],actual[j]);
        return sw_report("direct",n,pair,calls,expected);
    }
    return 0;
}
static int sw_patch_all(BYTE saved[17][5],int active[17],const int *keep){
    for(unsigned i=0;i<17;++i){active[i]=0;if(keep&&keep[i])continue;if(actor_patch(sw_addresses[i]-0x400000u,sw_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
    return 0;
}
static int sw_unpatch_all(BYTE saved[17][5],const int active[17]){
    for(unsigned i=17;i-->0;)if(active[i]&&actor_patch(sw_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    return 0;
}
static int sw_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;sw_build(n,a,r);
        BYTE before[SW_GLOBALS],after[SW_GLOBALS],actual[SW_GLOBALS],expected_fpu[108],actual_fpu[108];sw_record expected[96];
        memcpy(sw_before,sw_arena,SW_SIZE);sw_globals(before,0);
        sw_reset();r3_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=sw_calls;memcpy(expected,sw_log,calls*sizeof(*expected));memcpy(sw_after,sw_arena,SW_SIZE);sw_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("sweep poison install");
        memcpy(sw_arena,sw_before,SW_SIZE);sw_globals(before,1);sw_reset();r3_poison_call(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);sw_globals(actual,0);
        if(memcmp(expected_pair,actual_pair,8)||calls!=sw_calls||memcmp(expected,sw_log,calls*sizeof(*expected))||memcmp(sw_after,sw_arena,SW_SIZE)||
           memcmp(after,actual,SW_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            status=sw_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        }
        if(!es2_hook_uninstall(&h))return fail("sweep poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: sweep %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state\n",total);
    return status;
}
/* Real leaf arithmetic, real runtime setup and real RNG under every mode
 * combination; only the graph, terrain, pool and effect callees stay synthetic. */
static int sw_modes(unsigned index){
    static const unsigned nested[]={ES2_HOOK_INDEX_transform_point,ES2_HOOK_INDEX_matrix_transpose,ES2_HOOK_INDEX_rotate_point,
        ES2_HOOK_INDEX_dist_gate,ES2_HOOK_INDEX_dist2d_point,ES2_HOOK_INDEX_heading_point,ES2_HOOK_INDEX_rng_next,ES2_HOOK_INDEX_multiply_q10};
    enum { NESTED=sizeof(nested)/sizeof(nested[0]) };
    static const int real[17]={1,1,1,1,1,1,0,1,0,0,1,0,0,1,0,1,1};
    BYTE saved[17][5],rng[0x72];int active[17];if(sw_patch_all(saved,active,real))return 1;
    memcpy(rng,mapped+0xd2599,sizeof(rng));
    int status=0;unsigned total=0;uint64_t entered[NESTED]={0};
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<128&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;sw_build(n,a,r);
        BYTE before[SW_GLOBALS],after[SW_GLOBALS],actual[SW_GLOBALS],rng_before[0x72],rng_after[0x72];sw_record expected[96];
        memcpy(sw_before,sw_arena,SW_SIZE);sw_globals(before,0);memcpy(rng_before,mapped+0xd2599,0x72);
        es2_hook h[NESTED+1];memset(h,0,sizeof(h));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<NESTED;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            if(!weapons_install_hook(h+j,function_hook_descriptor(nested[j]),&o,function_hook_extension(nested[j])))return fail("sweep nested install");}
        sw_reset();r3_poison_call(mapped+function_hook_descriptor(index)->rva,a,pattern,expected_pair);
        unsigned calls=sw_calls;memcpy(expected,sw_log,calls*sizeof(*expected));memcpy(sw_after,sw_arena,SW_SIZE);sw_globals(after,0);memcpy(rng_after,mapped+0xd2599,0x72);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(h+NESTED,function_hook_descriptor(index),&o,function_hook_extension(index)))return fail("sweep caller mode install");
        memcpy(sw_arena,sw_before,SW_SIZE);sw_globals(before,1);memcpy(mapped+0xd2599,rng_before,0x72);sw_reset();
        r3_poison_call(mapped+function_hook_descriptor(index)->rva,a,pattern,actual_pair);sw_globals(actual,0);
        if(memcmp(expected_pair,actual_pair,8)||calls!=sw_calls||memcmp(expected,sw_log,calls*sizeof(*expected))||memcmp(sw_after,sw_arena,SW_SIZE)||
           memcmp(after,actual,SW_GLOBALS)||memcmp(rng_after,mapped+0xd2599,0x72)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            fprintf(stderr,"mixed mode=%u\n",mode);status=sw_report("mixed",n,pair,calls,expected);
        }
        for(unsigned j=0;j<=NESTED;++j){if(j<NESTED)entered[j]+=h[j].calls;if(h[j].mismatches)status=fail("sweep nested compare mismatch");
            if(h[j].descriptor&&!es2_hook_uninstall(h+j))return fail("sweep nested uninstall");}
        ++total;
    }
    memcpy(mapped+0xd2599,rng,sizeof(rng));
    if(sw_unpatch_all(saved,active))return 1;
    for(unsigned j=0;j<NESTED&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: sweep mixed callee %s never entered\n",function_hook_descriptor(nested[j])->name);status=1;}
    if(!status)printf("PASS: sweep %u real transform/transpose/rotate/distance/heading/RNG/scaling mixed-mode cases with 31/a7 stack\n",total);
    return status;
}
static int sweep_cases(unsigned limit){
    unsigned index=ES2_HOOK_INDEX_sweep;BYTE saved[17][5],old_globals[SW_GLOBALS];int active[17];
    sw_tables();sw_globals(old_globals,0);if(sw_patch_all(saved,active,NULL))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_SWEEP_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];sw_build(n,a,r);status=sw_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=sw_installed_probes(index,64);
    if(sw_unpatch_all(saved,active))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=sw_modes(index);
    sw_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered full-slot calls, frame payloads by content, graph/state/globals and FS:0\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
