/* Pool updates: explosion_update 40813c, light_level 4076a0, fire_update
 * 46b678. Direct cases run the original body and the core against logging stubs
 * for every game callee (entry registers recorded, frame arguments compared by
 * content); the pure leaves 407b20, 407048, 40735c and 47df9c run as original
 * code. Then installed 31/a7 poison probes and a real-callee pass with the
 * nested hooks in rotating modes. fire_update's run also compares the core
 * square root with 47df9c. */
typedef struct lg_record { uint32_t id,args[3],regs[3],input[6],eax,edx; } lg_record;
enum { LG_SIZE=0x1000, LG_OBJECT=0x100, LG_TYPES=0x280, LG_ANIMS=0x300, LG_SHAPE=0x380, LG_NODE=0x390, LG_FRAMES=0x3c0,
    LG_COUNTS=0x3d0, LG_LIGHT=0x400, LG_LINKED=0x440, LG_SHAPE2=0x480, LG_NODE2=0x490, LG_COUNTS2=0x4c0, LG_FRAMES2=0x4d0,
    LG_TABLE=0x500, LG_OWNER=0x600, LG_OWNER2=0x640, LG_OWNER_SHAPE=0x680, LG_NODE_OBJECT=0x6a0, LG_VTABLE=0x6c0,
    LG_RECORD=0x6f0, LG_MATRICES=0x700, LG_CAMERA=0x800, LG_MAX=16, LG_IDS=7, LG_GLOBALS=58 };
#define LG_BASE ((uint32_t)(uintptr_t)lg_arena)
static BYTE lg_arena[LG_SIZE],lg_before[LG_SIZE],lg_after[LG_SIZE];
static lg_record lg_log[LG_MAX];
static unsigned lg_calls;
static int lg_real;
static uint32_t lg_seed,lg_stream,lg_plan_p[3],lg_plan_q[3],lg_record_word;
__attribute__((used)) static uint32_t lg_ecx;
/* 467a48, 4076a0, node virtual +20 (not patched), 4801d4, 47ebb8, 48043c, 46297c. */
static const uint32_t lg_addresses[LG_IDS]={0x467a48,0x4076a0,0,0x4801d4,0x47ebb8,0x48043c,0x46297c};
static const unsigned lg_counts[LG_IDS]={1,2,2,3,2,3,2};
static uint32_t lg_next(void){
    uint32_t z=lg_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *lg_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
/* Same fixed stack window as node_cases.h: frame addresses are masked. */
static int lg_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static uint32_t lg_root(uint32_t value){return ((uint32_t (__cdecl *)(uint32_t))(void *)(mapped+0x7df9c))(value);}
__attribute__((used,noinline)) static uint64_t __cdecl lg_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(lg_calls==LG_MAX){fail("logic synthetic call overflow");ExitProcess(1);}
    lg_record *v=lg_log+lg_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,4*lg_counts[id]);memcpy(v->regs,regs,12);
    for(unsigned j=0;j<3;++j)if(lg_stack(v->regs[j]))v->regs[j]=0;
    for(unsigned j=0;j<lg_counts[id];++j)if(lg_stack(v->args[j]))v->args[j]=0;
    uint32_t eax=lg_next(),edx=lg_next(),pick=lg_next();lg_ecx=lg_next();
    BYTE *o=lg_arena+LG_OBJECT;
    switch(id){
    case 0: /* timer_word_step: expired (AX = 0) half of the time. */
        if(pick%2)eax&=0xffff0000u;
        if(pick%9==0)put16(o+0x59,(uint16_t)(pick>>8&3));
        break;
    case 1: /* light_level: may change what the explosion reads afterwards. */
        if(pick%5==0)put32(o+0x4f,pick>>8&1?0:LG_BASE+LG_LINKED);
        if(pick%7==0)o[0x41]=(BYTE)(lg_next()%6u-3u);
        if(pick%11==0)put16(lg_arena+LG_COUNTS2,(uint16_t)(1+lg_next()%5));
        break;
    case 2: /* node virtual +20: record per plan; may swap the owner. */
        v->input[0]=get16(o+0x4e);
        if(lg_real){eax=pick%4?LG_BASE+LG_RECORD:0;break;}
        eax=pick%4?LG_BASE+LG_RECORD:0;
        if(pick%6==0)put32(o+0x4a,LG_BASE+(pick>>8&1?LG_OWNER2:LG_OWNER));
        if(pick%10==0)o[0x41]^=1;
        if(pick%13==0)put32(lg_arena+LG_OWNER_SHAPE+0x16,LG_BASE+LG_MATRICES+32*(pick>>12&1));
        break;
    case 3: { /* 4801d4: offset words by content, planned local point out. */
        for(unsigned j=0;j<3;++j)v->input[j]=get16(lg_host(a[0])+2*j);
        for(unsigned j=0;j<3;++j)put32(lg_host(a[2])+4*j,lg_plan_p[j]);
        if(pick%8==0)put16(lg_arena+LG_OWNER+0x32,0);
        break; }
    case 4: /* euler_matrix: angles by content; rewrites matrix words. */
        for(unsigned j=0;j<3;++j)v->input[j]=get16(lg_host(a[0])+2*j);
        if(pick%3)for(unsigned j=0;j<9;++j)put16(lg_host(a[1])+2*j,(uint16_t)lg_next());
        break;
    case 5: /* transform_point: point by content, planned world point out. */
        for(unsigned j=0;j<3;++j)v->input[j]=get32(lg_host(a[0])+4*j);
        for(unsigned j=0;j<3;++j)put32(lg_host(a[2])+4*j,lg_plan_q[j]);
        break;
    case 6: /* 46297c: Q by content; may disturb the minimum and the lifetime. */
        for(unsigned j=0;j<3;++j)v->input[j]=get32(lg_host(a[1])+4*j);
        if(pick%5==0)put32(mapped+0x2b4f3c,lg_next());
        if(pick%7==0)put16(o+0x59,(uint16_t)(pick>>8&1));
        break;
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
/* Pushes the entry EAX/EDX/ECX for the record before the slot pointer. */
#define LG_STUB(n) __attribute__((naked)) static void lg_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _lg_stub_impl\n\taddl $24,%esp\n\tmovl _lg_ecx,%ecx\n\tret\n\t");}
LG_STUB(0) LG_STUB(1) LG_STUB(2) LG_STUB(3) LG_STUB(4) LG_STUB(5) LG_STUB(6)
#undef LG_STUB
static void *lg_stubs[LG_IDS]={lg_stub_0,lg_stub_1,lg_stub_2,lg_stub_3,lg_stub_4,lg_stub_5,lg_stub_6};
/* Type table, animation table, light table, camera, minimum, default node
 * record, sound flag, time scale. */
static void lg_globals(BYTE *out,int restore){
    static const unsigned offsets[]={0xa9628,0xa963c,0xa9608,0xd24ea,0x2b4f3c,0x2c56a8,0x9f8ec,0xd3b64},sizes[]={4,4,4,4,4,32,4,2};
    for(unsigned i=0;i<8;++i){if(restore)memcpy(mapped+offsets[i],out,sizes[i]);else memcpy(out,mapped+offsets[i],sizes[i]);out+=sizes[i];}
}
static void lg_reset(void){lg_calls=0;lg_stream=lg_seed;}
static uint16_t lg_word(const uint16_t *values,unsigned count){return values[random32()%count];}
static void lg_explosion(unsigned n,int real){
    uint32_t b=LG_BASE;BYTE *o=lg_arena+LG_OBJECT;
    static const uint16_t anims[]={0,1,2,3,4,5,6,7,0xffff};
    static const uint16_t words[]={0,1,2,3,4,5,6,7,0,1,0xffff,0x8000,0x8123};
    static const uint16_t counts[]={1,2,3,4,5,9,0x8000,0xffff};
    o[0x41]=(BYTE)(random32()%6u-3u);
    for(unsigned k=0;k<6;++k){BYTE *t=lg_arena+LG_TYPES+40*k-120;put16(t,lg_word(anims,9));}
    put32(mapped+0xa9628,b+LG_TYPES);put32(mapped+0xa963c,b+LG_ANIMS);put32(mapped+0xa9608,b+LG_TABLE);
    for(unsigned k=0;k<9;++k)put16(lg_arena+LG_ANIMS-4+4*k,n<512?words[(n+k)%13]:lg_word(words,13));
    put32(o+0x34,b+LG_SHAPE);put32(lg_arena+LG_SHAPE+4,b+LG_NODE);put32(lg_arena+LG_SHAPE+8,b+LG_FRAMES);put32(lg_arena+LG_NODE+0x20,b+LG_COUNTS);
    for(unsigned k=0;k<8;++k){uint16_t c=lg_word(counts,8);put16(lg_arena+LG_COUNTS+2*k,c);
        put16(lg_arena+LG_FRAMES+2*k,(uint16_t)(random32()%3==0&&c<16?c-1:random32()%11));}
    put32(o+0x53,random32()%3?b+LG_LIGHT:0);put16(lg_arena+LG_LIGHT+0xc,(uint16_t)(random32()%6u-2u));
    put32(o+0x4f,random32()%3?b+LG_LINKED:0);
    put32(lg_arena+LG_LINKED+0x34,random32()%4?b+LG_SHAPE2:b+LG_SHAPE);
    put32(lg_arena+LG_SHAPE2+4,b+LG_NODE2);put32(lg_arena+LG_SHAPE2+8,b+LG_FRAMES2);put32(lg_arena+LG_NODE2+0x20,b+LG_COUNTS2);
    put16(lg_arena+LG_COUNTS2,lg_word(counts,8));put16(lg_arena+LG_FRAMES2,(uint16_t)(random32()%11));
    if(real){o[0x4a]=(BYTE)random32();put16(o+0x4b,(uint16_t)(random32()%400));put16(mapped+0xd3b64,(uint16_t)(1+random32()%200));}
}
static void lg_light(unsigned n,uint32_t a[6]){
    uint32_t b=LG_BASE;
    static const uint32_t scales[]={0,1,0x04000000u,0xffffffffu,0x7fffffffu,0x80000000u,0x00666666u,0x10};
    static const BYTE levels[]={0,1,0x7f,0x80,0xff};
    put32(mapped+0xa9608,b+LG_TABLE+(n%7==0?random32()%16:0));
    uint32_t light=b+LG_LIGHT+(n%5==0?random32()%32:0);put16(lg_host(light)+0xc,(uint16_t)(random32()%6u-2u));
    for(int k=-2;k<4;++k){BYTE *e=lg_arena+LG_TABLE+0x6c+35*k;
        if(n<1024){put32(e+0x13,scales[(n+(unsigned)k)%8]);put32(e+0xf,scales[(n/8+(unsigned)k)%8]);}}
    a[0]=light;a[1]=n<1024?(random32()&0xffffff00u)|levels[n%5]:random32();
}
static void lg_fire(unsigned n,int real){
    uint32_t b=LG_BASE;BYTE *o=lg_arena+LG_OBJECT;
    static const uint16_t nodes[]={0xffff,0x8000,0,1,2,7,0x7fff,0xfffe};
    static const uint16_t records[]={0,1,2,7,0xffff,0x8000};
    static const uint16_t counts[]={1,2,3,9,0x8000,0xffff};
    put32(o+0x34,b+LG_SHAPE);put32(lg_arena+LG_SHAPE+4,b+LG_NODE);put32(lg_arena+LG_SHAPE+8,b+LG_FRAMES);put32(lg_arena+LG_NODE+0x20,b+LG_COUNTS);
    uint16_t c=lg_word(counts,6);put16(lg_arena+LG_COUNTS,c);put16(lg_arena+LG_FRAMES,(uint16_t)(random32()%2&&c<16?c-1:random32()%11));
    o[0x41]=(BYTE)(random32()%2?0:random32()%3?1:random32());
    put32(o+0x4a,b+(random32()%5?LG_OWNER:LG_OWNER2));
    put16(o+0x4e,n<1024?nodes[n%8]:lg_word(nodes,8));
    static const uint16_t lives[]={0,1,2,0xffff};put16(o+0x59,random32()%2?lives[random32()%4]:(uint16_t)random32());
    for(unsigned k=0;k<2;++k){BYTE *owner=lg_arena+(k?LG_OWNER2:LG_OWNER);
        put32(owner+0x34,b+LG_OWNER_SHAPE);put16(owner+0x32,random32()%3?(uint16_t)(random32()%2):(uint16_t)random32());
        if(real)for(unsigned j=0;j<3;++j)put32(owner+0x26+4*j,random32()%65536u-32768u);}
    put32(lg_arena+LG_OWNER_SHAPE+4,b+LG_NODE_OBJECT);put32(lg_arena+LG_OWNER_SHAPE+0x16,b+LG_MATRICES);
    put32(lg_arena+LG_NODE_OBJECT,b+LG_VTABLE);put32(lg_arena+LG_VTABLE+0x20,(uint32_t)(uintptr_t)lg_stubs[2]);
    put16(lg_arena+LG_RECORD+4,lg_word(records,6));
    random_bytes(mapped+0x2c56a8,32);
    put32(mapped+0xd24ea,b+LG_CAMERA);
    for(unsigned j=0;j<3;++j){lg_plan_p[j]=random32();lg_plan_q[j]=random32()%4?random32()%8192u-4096u:random32();}
    /* The world point is known in advance (owner position or the planned
     * transform output), so the camera can sit at a chosen distance and the
     * minimum next to its root. */
    uint32_t q[3];int owner_path=(int16_t)get16(o+0x4e)<0&&o[0x41];
    BYTE *owner=lg_arena+(get32(o+0x4a)-b);
    for(unsigned j=0;j<3;++j)q[j]=owner_path?get32(owner+0x26+4*j):lg_plan_q[j];
    uint32_t sum=0;
    for(unsigned j=0;j<3;++j){uint32_t d=random32()%4?random32()%101u-50u:random32();put32(lg_arena+LG_CAMERA+4+4*j,q[j]+d);sum+=d*d;}
    uint32_t root=real?random32()%40000u:lg_root(sum);
    static const uint32_t extremes[]={0x7fffffffu,0x80000000u,0,0xffffffffu};
    uint32_t k=random32()%7;put32(mapped+0x2b4f3c,k<3?root+k-1u:k<5?extremes[random32()%4]:random32());
    if(real){put32(mapped+0x9f8ec,0);o[0x56]=(BYTE)random32();put16(o+0x57,(uint16_t)(random32()%400));put16(mapped+0xd3b64,(uint16_t)(1+random32()%200));
        for(unsigned j=0;j<3;++j)put16(o+0x50+2*j,(uint16_t)(random32()%2048u-1024u));}
}
static void lg_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    lg_seed=random32();random_bytes(lg_arena,LG_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    a[0]=LG_BASE+LG_OBJECT;
    if(index==ES2_HOOK_INDEX_explosion_update)lg_explosion(n,real);
    else if(index==ES2_HOOK_INDEX_light_level)lg_light(n,a);
    else lg_fire(n,real);
}
static int lg_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const lg_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,lg_calls);
    for(unsigned j=0;j<calls&&j<lg_calls&&expected;++j)if(memcmp(expected+j,lg_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(lg_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<LG_SIZE;++j)if(lg_after[j]!=lg_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,lg_after[j],lg_arena[j]);break;}
    return 1;
}
static unsigned lg_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<lg_calls;++j){unsigned id=lg_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?id==2&&c->key==0x20:lg_addresses[id]==c->key)++hits;}
    return hits;
}
static int lg_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[LG_GLOBALS],after[LG_GLOBALS],actual[LG_GLOBALS];lg_record expected[LG_MAX];
    memcpy(lg_before,lg_arena,LG_SIZE);lg_globals(before,0);
    lg_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=lg_calls;memcpy(expected,lg_log,calls*sizeof(*expected));
    memcpy(lg_after,lg_arena,LG_SIZE);lg_globals(after,0);
    memcpy(lg_arena,lg_before,LG_SIZE);lg_globals(before,1);lg_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);lg_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=lg_expected_hits(c+i))return fail("logic callee counter delta");
    if(pair[0]!=pair[1]||calls!=lg_calls||memcmp(expected,lg_log,calls*sizeof(*expected))||memcmp(lg_after,lg_arena,LG_SIZE)||memcmp(after,actual,LG_GLOBALS)){
        for(unsigned j=0;j<LG_GLOBALS;++j)if(after[j]!=actual[j])fprintf(stderr," global byte %u %02x/%02x\n",j,after[j],actual[j]);
        return lg_report("direct",n,pair,calls,expected);
    }
    lg_globals(before,1);
    return 0;
}
static int lg_patch(BYTE saved[LG_IDS][5],int active[LG_IDS],unsigned which,int on){
    for(unsigned i=0;i<LG_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!lg_addresses[i])continue;if(actor_patch(lg_addresses[i]-0x400000u,lg_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(lg_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static int lg_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;lg_build(index,n,a,r,0);
        BYTE before[LG_GLOBALS],after[LG_GLOBALS],actual[LG_GLOBALS],expected_fpu[108],actual_fpu[108];lg_record expected[LG_MAX];
        memcpy(lg_before,lg_arena,LG_SIZE);lg_globals(before,0);
        lg_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=lg_calls;memcpy(expected,lg_log,calls*sizeof(*expected));memcpy(lg_after,lg_arena,LG_SIZE);lg_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("logic poison install");
        memcpy(lg_arena,lg_before,LG_SIZE);lg_globals(before,1);lg_reset();sg_poison_call(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);lg_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=lg_calls||memcmp(expected,lg_log,calls*sizeof(*expected))||memcmp(lg_after,lg_arena,LG_SIZE)||
           memcmp(after,actual,LG_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=lg_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("logic poison uninstall");lg_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state\n",d->name,total);
    return status;
}
/* Real callees with the nested hooks in rotating original/replaced/compare
 * modes: explosion_update over the real timer and light_level (and the
 * 407b20/407048/40735c leaves), fire_update over the real timer, Euler,
 * transform and 4801d4 (the node virtual stays a stub; 46297c runs with the
 * sound flag 0, so no SOS import is reached). The full pair is compared. */
static int lg_real_pass(unsigned index){
    unsigned nested[3];unsigned count;
    if(index==ES2_HOOK_INDEX_explosion_update){nested[0]=ES2_HOOK_INDEX_timer_word_step;nested[1]=ES2_HOOK_INDEX_light_level;count=2;}
    else {nested[0]=ES2_HOOK_INDEX_timer_word_step;nested[1]=ES2_HOOK_INDEX_euler_matrix;nested[2]=ES2_HOOK_INDEX_transform_point;count=3;}
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[3]={0};
    lg_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;lg_build(index,n,a,r,1);
        BYTE before[LG_GLOBALS],after[LG_GLOBALS],actual[LG_GLOBALS];
        memcpy(lg_before,lg_arena,LG_SIZE);lg_globals(before,0);
        es2_hook hk[4];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("logic nested install");}
        lg_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        memcpy(lg_after,lg_arena,LG_SIZE);lg_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+3,d,&o,function_hook_extension(index)))return fail("logic caller install");
        memcpy(lg_arena,lg_before,LG_SIZE);lg_globals(before,1);lg_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);lg_globals(actual,0);
        if(expected_pair[0]!=actual_pair[0]||expected_pair[1]!=actual_pair[1]||memcmp(lg_after,lg_arena,LG_SIZE)||memcmp(after,actual,LG_GLOBALS)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            for(unsigned j=0;j<LG_GLOBALS;++j)if(after[j]!=actual[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,after[j],actual[j]);break;}
            fprintf(stderr,"real mode=%u\n",mode);lg_calls=0;status=lg_report("real",n,pair,0,NULL);
        }
        for(unsigned j=0;j<4;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("logic nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("logic nested uninstall");}
        lg_globals(before,1);++total;
    }
    lg_real=0;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, 31/a7 stack, full pair and memory\n",d->name,total,count);
    return status;
}
/* 47df9c against the core root: every square and its neighbours up to
 * 2^32 - 1, the bit-pair boundaries and one million random values. */
static int lg_root_probe(void){
    unsigned total=0;
    for(uint32_t k=0;k<=65536u;++k){uint32_t s=k*k;for(int d=-1;d<=1;++d){uint32_t x=s+(uint32_t)d;++total;
        if(lg_root(x)!=es2_logic_root(x)){fprintf(stderr,"FAIL: fire_update square root %08x %08x/%08x\n",x,lg_root(x),es2_logic_root(x));return 1;}}}
    for(unsigned k=0;k<32;++k){uint32_t x=1u<<k;for(int d=-1;d<=1;++d){++total;if(lg_root(x+(uint32_t)d)!=es2_logic_root(x+(uint32_t)d))return fail("fire_update square root boundary");}}
    for(unsigned k=0;k<1000000;++k){uint32_t x=random32()>>(k%32);++total;if(lg_root(x)!=es2_logic_root(x))return fail("fire_update square root random");}
    printf("PASS: fire_update square root 47df9c = core on %u values (squares +-1, powers of two +-1, random)\n",total);
    return 0;
}
static int logic_cases(unsigned index,unsigned limit){
    unsigned which=index==ES2_HOOK_INDEX_explosion_update?0x3u:index==ES2_HOOK_INDEX_light_level?0:0x79u;
    BYTE saved[LG_IDS][5],old_globals[LG_GLOBALS];int active[LG_IDS];
    lg_globals(old_globals,0);if(lg_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_LOGIC_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];lg_build(index,n,a,r,0);status=lg_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=lg_installed_probes(index,64);
    if(lg_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status&&index!=ES2_HOOK_INDEX_light_level)status=lg_real_pass(index);
    if(!limit&&!status&&index==ES2_HOOK_INDEX_fire_update)status=lg_root_probe();
    lg_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, frame payloads by content, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
