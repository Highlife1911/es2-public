/* Graph nodes: terrain_pose 4029d8, shield_geometry 413cc4,
 * debris_heading 4089e4. Direct cases run the original body and the core
 * against logging stubs for every game callee (entry registers recorded,
 * frame arguments compared by content), then installed 31/a7 poison probes,
 * then a real-callee pass with the nested hooks in rotating modes (real
 * terrain, matrices and RNG). */
typedef struct nd_record { uint32_t id,args[5],regs[3],input[12],eax,edx; } nd_record;
enum { ND_SIZE=0x1000, ND_VTABLE=0x40, ND_OBJECT=0x100, ND_SHIELDS=0x200, ND_VEHICLE=0x300, ND_DEF=0x600,
    ND_MATRIX=0x680, ND_DAMAGE=0x6c0, ND_VECTOR=0x700, ND_MAP=0x800, ND_CELLS=0xa00, ND_MAX=24, ND_IDS=11, ND_GLOBALS=124 };
#define ND_BASE ((uint32_t)(uintptr_t)nd_arena)
static BYTE nd_arena[ND_SIZE],nd_before[ND_SIZE],nd_after[ND_SIZE];
static nd_record nd_log[ND_MAX];
static unsigned nd_calls;
static int nd_real;
static uint32_t nd_seed,nd_stream,nd_range,nd_def,nd_object,nd_shields,nd_damage;
__attribute__((used)) static uint32_t nd_ecx;
/* 47ebb8, 46e180, 47d32c, V10 (virtual, not patched), 4801d4, 47de4c,
 * 47df19, 480304, 47dff0, 492ee0, 47e0b0. */
static const uint32_t nd_addresses[ND_IDS]={0x47ebb8,0x46e180,0x47d32c,0,0x4801d4,0x47de4c,0x47df19,0x480304,0x47dff0,0x492ee0,0x47e0b0};
static const unsigned nd_counts[ND_IDS]={2,2,2,1,3,2,1,3,1,1,2};
static uint32_t nd_next(void){
    uint32_t z=nd_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *nd_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
/* A fixed window (the worker's 16 MiB stack reservation), so equal values
 * are masked identically in both runs; frame addresses always fall inside. */
static int nd_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
__attribute__((used,noinline)) static uint64_t __cdecl nd_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(nd_calls==ND_MAX){fail("node synthetic call overflow");ExitProcess(1);}
    nd_record *v=nd_log+nd_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,4*nd_counts[id]);memcpy(v->regs,regs,12);
    /* Frame addresses differ by construction; their contents are recorded. */
    for(unsigned j=0;j<3;++j)if(nd_stack(v->regs[j]))v->regs[j]=0;
    for(unsigned j=0;j<nd_counts[id];++j)if(nd_stack(v->args[j]))v->args[j]=0;
    uint32_t eax=nd_next(),edx=nd_next(),pick=nd_next();nd_ecx=nd_next();
    BYTE *o=nd_host(nd_object);
    switch(id){
    case 0: /* Euler: angles in, coefficients out; may also set the cache word. */
        for(unsigned j=0;j<3;++j)v->input[j]=get16(nd_host(a[0])+2*j);
        if(pick%3)for(unsigned j=0;j<9;++j)put16(nd_host(a[1])+2*j,(uint16_t)nd_next());
        if(pick%5==0)put16(o+0x32,(uint16_t)nd_next());
        break;
    case 1: /* Terrain height of a frame point; may move the object or swap the map. */
        v->input[0]=get32(nd_host(a[1]));v->input[1]=get32(nd_host(a[1])+4);
        if(pick%7==0)put32(o+0x26+4*(pick>>8&1),nd_next());
        if(pick%11==0)put32(mapped+0xa0be0,nd_next());
        if(pick%4==0)eax%=4096;
        break;
    case 3: /* V10 size query; may change the cache word and the matrix. */
        if(nd_real){eax=(eax&0xffff0000u)|(uint16_t)(pick%5==0?0u-pick%3000u:pick%3000u);break;}
        if(pick%4==0)put16(o+0x32,pick%8==0?0:(uint16_t)nd_next());
        if(pick%6==0)put16(o+0x12+2*(pick>>8&3),(uint16_t)nd_next());
        if(pick%3==0){static const uint16_t sizes[]={0,1,0xffff,0x7fff,0x8000,0x4000,0xc000};eax=(eax&0xffff0000u)|sizes[(pick>>12)%7];}
        break;
    case 4: { /* 4801d4: vector by content; Y around the range test. */
        for(unsigned j=0;j<3;++j)v->input[j]=get16(nd_host(a[0])+2*j);
        BYTE *out=nd_host(a[2]);uint32_t y;
        switch(pick%6){case 0:y=nd_range-1u;break;case 1:y=nd_range;break;case 2:y=nd_range+1u;break;
            case 3:y=nd_next()%(nd_range|1u);break;default:y=nd_next();}
        put32(out,pick>>8&1?nd_next():nd_next()%4096u-2048u);put32(out+4,y);put32(out+8,pick>>9&1?nd_next():nd_next()%4096u-2048u);
        break; }
    case 5: { /* 47de4c: distance around the radius word (wrapped 16-bit test). */
        uint32_t radius=get16(nd_host(nd_def+0x18));static const int deltas[]={-2,-1,0,1,2};
        switch(pick%5){case 0:eax=nd_next();break;case 1:eax=(eax&0xffff0000u)|(uint16_t)(radius+(uint32_t)deltas[(pick>>8)%5]);break;
            case 2:eax=(uint16_t)(radius+(uint32_t)deltas[(pick>>8)%5]);break;case 3:eax=nd_next()%(2*radius+1);break;
            default:eax=(eax&0xffff0000u)|(uint16_t)(nd_next()%(radius+1));}
        if(pick%13==0)put16(nd_host(nd_def+0x18),(uint16_t)nd_next());
        break; }
    case 6: { /* 47df19: local matrix by content; transposes, may replace the translation. */
        BYTE *l=nd_host(a[0]);static const unsigned pairs[3][2]={{1,3},{2,6},{5,7}};
        for(unsigned j=0;j<8;++j)v->input[j]=get32(l+4*j);
        for(unsigned j=0;j<3;++j){uint16_t t=get16(l+2*pairs[j][0]);put16(l+2*pairs[j][0],get16(l+2*pairs[j][1]));put16(l+2*pairs[j][1],t);}
        if(pick%3==0)for(unsigned j=0;j<3;++j)put32(l+20+4*j,nd_next());
        break; }
    case 7: { /* 480304: point and matrix by content, output beside the matrix. */
        BYTE *p=nd_host(a[0]),*l=nd_host(a[1]),*out=nd_host(a[2]);
        static const uint32_t ys[]={0,1,0xffffffffu,0x80000000u,0x7fffffffu};
        for(unsigned j=0;j<3;++j)v->input[j]=get32(p+4*j);
        for(unsigned j=0;j<8;++j)v->input[3+j]=get32(l+4*j);
        v->input[11]=a[2]-a[1];
        put32(out,nd_next());put32(out+4,pick%3?((pick>>8)%2?nd_next()%4096u:0u-nd_next()%4096u):ys[(pick>>8)%5]);put32(out+8,nd_next());
        if(pick%7==0)put16(nd_host(nd_shields+2*(pick>>12&1)),(uint16_t)nd_next());
        if(pick%11==0)put16(nd_host(nd_damage),(uint16_t)nd_next());
        if(pick%13==0)put16(nd_host(nd_def+0x18),(uint16_t)nd_next());
        break; }
    case 8: /* heading_cos: may change the motion words between the calls. */
        if(pick%6==0)put16(o+0x4a+2*((pick>>8)%3),(uint16_t)nd_next());
        if(pick%2)eax=(uint32_t)((int32_t)(nd_next()%32769u)-16384);
        break;
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
/* Pushes the entry EAX/EDX/ECX for the record before the slot pointer. */
#define ND_STUB(n) __attribute__((naked)) static void nd_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _nd_stub_impl\n\taddl $24,%esp\n\tmovl _nd_ecx,%ecx\n\tret\n\t");}
ND_STUB(0) ND_STUB(1) ND_STUB(2) ND_STUB(3) ND_STUB(4) ND_STUB(5) ND_STUB(6) ND_STUB(7) ND_STUB(8) ND_STUB(9) ND_STUB(10)
#undef ND_STUB
static void *nd_stubs[ND_IDS]={nd_stub_0,nd_stub_1,nd_stub_2,nd_stub_3,nd_stub_4,nd_stub_5,nd_stub_6,nd_stub_7,nd_stub_8,nd_stub_9,nd_stub_10};
/* Terrain map pointer, motion vector pointer, speed word, RNG stream A. */
static void nd_globals(BYTE *out,int restore){
    static const unsigned offsets[]={0xa0be0,0xa9660,0xa9668,0xd2599},sizes[]={4,4,2,0x72};
    for(unsigned i=0;i<4;++i){if(restore)memcpy(mapped+offsets[i],out,sizes[i]);else memcpy(out,mapped+offsets[i],sizes[i]);out+=sizes[i];}
}
static void nd_reset(void){nd_calls=0;nd_stream=nd_seed;}
static uint32_t nd_pick(const uint32_t *values,unsigned count){return values[random32()%count];}
static void nd_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    uint32_t b=ND_BASE;nd_seed=random32();random_bytes(nd_arena,ND_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    nd_object=b+ND_OBJECT;BYTE *o=nd_arena+ND_OBJECT;
    static const uint32_t words[]={0,1,0xffff,0x7fff,0x8000,0x4000,0x100,0x2000};
    if(index==ES2_HOOK_INDEX_terrain_pose){
        put32(o,b+ND_VTABLE);put32(nd_arena+ND_VTABLE+0x10,(uint32_t)(uintptr_t)nd_stubs[3]);
        if(n%2)put16(o+0x32,0);else if(n%4==0)put16(o+0x32,1);
        put32(mapped+0xa0be0,random32());
        if(real){
            /* Up to 8 x 8 cells, flags 0..2; positions around the grid. */
            BYTE *map=nd_arena+ND_MAP;uint32_t cols=1+random32()%3,rows=1+random32()%3,s=6+random32()%5;
            put32(map+0xec,b+ND_CELLS);put32(map+0x100,cols);put32(map+0x104,rows);put32(map+0x108,s);
            put32(map+0x110,random32()%4096);put32(map+0x118,random32()%48);
            for(unsigned c=0;c<(1u<<(cols+rows));++c){BYTE *cell=nd_arena+ND_CELLS+16*c;cell[15]=(BYTE)((random32()&0xfcu)|random32()%3);}
            put32(mapped+0xa0be0,b+ND_MAP);
            uint32_t w=(1u<<cols)<<s,h=(1u<<rows)<<s;
            put32(o+0x26,random32()%(w+w/2)-w/4);put32(o+0x2a,random32()%(h+h/2)-h/4);
        }
        a[0]=b+ND_OBJECT;
    } else if(index==ES2_HOOK_INDEX_shield_geometry){
        uint32_t def=b+ND_DEF+(n%5==0?random32()%8:0);
        put32(nd_arena+ND_VEHICLE+0x1f2,def);nd_def=def+2u;
        put16(nd_host(nd_def+0x16),random32()%4?(uint16_t)random32():(uint16_t)nd_pick(words,8));
        put16(nd_host(nd_def+0x18),random32()%3?(uint16_t)(random32()%5000):(uint16_t)nd_pick(words,8));
        nd_shields=b+ND_SHIELDS+(n%7==0);
        nd_damage=n%9==0?nd_shields+2u*(random32()%2):n%9==1?nd_shields+1u:b+ND_DAMAGE;
        uint16_t damage=(uint16_t)(random32()%8==0?0:random32()%4?random32()%2000:random32());
        static const int deltas[]={-1,0,1};
        for(unsigned j=0;j<2;++j){uint32_t k=random32()%6;
            put16(nd_host(nd_shields+2*j),k<3?(uint16_t)(damage+(uint32_t)deltas[k]):k==3?0:(uint16_t)random32());}
        if(nd_damage==b+ND_DAMAGE||random32()%2)put16(nd_host(nd_damage),damage);
        static const uint32_t ranges[]={0,1,0x7fffffffu,0x80000000u,0xffffffffu};
        a[0]=nd_shields;a[1]=b+ND_VEHICLE;a[2]=b+ND_MATRIX;a[3]=random32()%4?random32():nd_pick(ranges,5);a[4]=nd_damage;
        if(real){
            BYTE *mx=nd_arena+ND_MATRIX;static const BYTE modes[]={0,1,2,3,0x80,0xff};
            for(unsigned j=0;j<9;++j)put16(mx+2*j,(uint16_t)(random32()%32769u-16384u));
            mx[18]=modes[random32()%6];
            for(unsigned j=0;j<3;++j)put32(mx+20+4*j,random32()%16384u-8192u);
            put16(nd_host(nd_def+0x16),(uint16_t)(random32()%4000));put16(nd_host(nd_def+0x18),(uint16_t)(1000+random32()%20000));
            a[3]=random32()%4?random32()%12000:random32();
        }
        nd_range=a[3];
    } else {
        static const uint32_t divisors[]={1,0xffffffffu,1000,1200,0x7fffffffu,0x80000000u,2,0xfffffffeu,0x10000,3};
        static const uint16_t angles[]={0,0x4000,0x8000,0xc000,0x3fff,0xffff,0x2000,1};
        uint32_t divisor=n<1024?divisors[n%10]:random32()%3?random32()%4000+1:random32();
        if(!divisor)divisor=1;
        put16(mapped+0xa9668,n<1024?(uint16_t)words[(n/10)%8]:(uint16_t)random32());
        a[0]=b+ND_OBJECT;a[1]=divisor;
        a[2]=n<1024?(random32()&0xffff0000u)|angles[(n/80)%8]:random32();
        a[3]=n<1024?(random32()&0xffff0000u)|angles[(n/10)%8]:random32();
        /* Null, separate, or aliasing the motion words that are added to. */
        static const uint32_t vectors[]={0,0,ND_VECTOR,ND_OBJECT+0x46,ND_OBJECT+0x42,ND_OBJECT+0x4a};
        uint32_t k=random32()%6;put32(mapped+0xa9660,k<2?0:b+vectors[k]);
        if(real){BYTE *state=mapped+0xd2599;random_bytes(state,112);state[112]=(BYTE)(random32()%56);state[113]=(BYTE)(random32()%56);}
    }
}
static int nd_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const nd_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,nd_calls);
    for(unsigned j=0;j<calls&&j<nd_calls&&expected;++j)if(memcmp(expected+j,nd_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(nd_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<ND_SIZE;++j)if(nd_after[j]!=nd_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,nd_after[j],nd_arena[j]);break;}
    return 1;
}
static unsigned nd_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<nd_calls;++j){unsigned id=nd_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?id==3&&c->key==0x10:nd_addresses[id]==c->key)++hits;}
    return hits;
}
static int nd_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[ND_GLOBALS],after[ND_GLOBALS],actual[ND_GLOBALS];nd_record expected[ND_MAX];
    memcpy(nd_before,nd_arena,ND_SIZE);nd_globals(before,0);
    nd_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=nd_calls;memcpy(expected,nd_log,calls*sizeof(*expected));
    memcpy(nd_after,nd_arena,ND_SIZE);nd_globals(after,0);
    memcpy(nd_arena,nd_before,ND_SIZE);nd_globals(before,1);nd_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[4];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);nd_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=nd_expected_hits(c+i))return fail("node callee counter delta");
    if(pair[0]!=pair[1]||calls!=nd_calls||memcmp(expected,nd_log,calls*sizeof(*expected))||memcmp(nd_after,nd_arena,ND_SIZE)||memcmp(after,actual,ND_GLOBALS)){
        for(unsigned j=0;j<ND_GLOBALS;++j)if(after[j]!=actual[j])fprintf(stderr," global byte %u %02x/%02x\n",j,after[j],actual[j]);
        return nd_report("direct",n,pair,calls,expected);
    }
    return 0;
}
static int nd_patch(BYTE saved[ND_IDS][5],int active[ND_IDS],unsigned which,int on){
    for(unsigned i=0;i<ND_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!nd_addresses[i])continue;if(actor_patch(nd_addresses[i]-0x400000u,nd_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(nd_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static int nd_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;nd_build(index,n,a,r,0);
        BYTE before[ND_GLOBALS],after[ND_GLOBALS],actual[ND_GLOBALS],expected_fpu[108],actual_fpu[108];nd_record expected[ND_MAX];
        memcpy(nd_before,nd_arena,ND_SIZE);nd_globals(before,0);
        nd_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=nd_calls;memcpy(expected,nd_log,calls*sizeof(*expected));memcpy(nd_after,nd_arena,ND_SIZE);nd_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("node poison install");
        memcpy(nd_arena,nd_before,ND_SIZE);nd_globals(before,1);nd_reset();sg_poison_call(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);nd_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=nd_calls||memcmp(expected,nd_log,calls*sizeof(*expected))||memcmp(nd_after,nd_arena,ND_SIZE)||
           memcmp(after,actual,ND_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=nd_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("node poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state\n",d->name,total);
    return status;
}
/* Real callees (4801d4 runs unhooked) with the nested hooks in rotating
 * original/replaced/compare modes and a patterned stack. terrain_pose and
 * debris_heading compare the full pair; shield_geometry compares EAX, because
 * its dead EDX can carry 480304's ECX or a 4801d4 frame address. */
static int nd_real_pass(unsigned index){
    static const unsigned nested_sets[3][3]={
        {ES2_HOOK_INDEX_euler_matrix,ES2_HOOK_INDEX_terrain_height,ES2_HOOK_INDEX_angle_ratio},
        {ES2_HOOK_INDEX_dist_octagonal,ES2_HOOK_INDEX_matrix_transpose,ES2_HOOK_INDEX_rotate_point},
        {ES2_HOOK_INDEX_heading_cos,ES2_HOOK_INDEX_rng_next,ES2_HOOK_INDEX_multiply_q10}};
    enum { NESTED=3 };
    unsigned set=index==ES2_HOOK_INDEX_terrain_pose?0:index==ES2_HOOK_INDEX_shield_geometry?1:2;
    const unsigned *nested=nested_sets[set];int full=set!=1;
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[NESTED]={0};
    nd_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;nd_build(index,n,a,r,1);
        BYTE before[ND_GLOBALS],after[ND_GLOBALS],actual[ND_GLOBALS];
        memcpy(nd_before,nd_arena,ND_SIZE);nd_globals(before,0);
        es2_hook hk[NESTED+1];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<NESTED;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("node nested install");}
        nd_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        memcpy(nd_after,nd_arena,ND_SIZE);nd_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+NESTED,d,&o,function_hook_extension(index)))return fail("node caller install");
        memcpy(nd_arena,nd_before,ND_SIZE);nd_globals(before,1);nd_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);nd_globals(actual,0);
        if(expected_pair[0]!=actual_pair[0]||(full&&expected_pair[1]!=actual_pair[1])||memcmp(nd_after,nd_arena,ND_SIZE)||memcmp(after,actual,ND_GLOBALS)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            for(unsigned j=0;j<ND_GLOBALS;++j)if(after[j]!=actual[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,after[j],actual[j]);break;}
            fprintf(stderr,"real mode=%u\n",mode);nd_calls=0;status=nd_report("real",n,pair,0,NULL);
        }
        for(unsigned j=0;j<=NESTED;++j){if(j<NESTED)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("node nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("node nested uninstall");}
        nd_globals(before,1);++total;
    }
    nd_real=0;
    for(unsigned j=0;j<NESTED&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases, nested %s/%s/%s in rotating modes, 31/a7 stack, %s and memory\n",d->name,total,
        function_hook_descriptor(nested[0])->name,function_hook_descriptor(nested[1])->name,function_hook_descriptor(nested[2])->name,full?"full pair":"EAX");
    return status;
}
static int node_cases(unsigned index,unsigned limit){
    unsigned which=index==ES2_HOOK_INDEX_terrain_pose?0x7u:index==ES2_HOOK_INDEX_shield_geometry?0xf0u:0x700u;
    BYTE saved[ND_IDS][5],old_globals[ND_GLOBALS];int active[ND_IDS];
    nd_globals(old_globals,0);if(nd_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_NODE_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];nd_build(index,n,a,r,0);status=nd_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=nd_installed_probes(index,64);
    if(nd_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=nd_real_pass(index);
    nd_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, frame payloads by content, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
