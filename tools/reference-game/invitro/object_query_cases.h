/* Sweep receiver object query: object_query 427da8, grid_sweep 4273c8,
 * set_length 4927f0 (weapons.md). Only the private, hash-checked mapping runs
 * original code. Direct cases: the game callees of the hook under test are
 * logging stubs that record their ordered arguments, entry registers and the
 * frame payloads they receive, by content (values inside the worker's stack
 * reservation, i.e. frame addresses, are masked); set_length runs its own
 * original callees and the harness memset. Then installed 31/a7 poison cases,
 * and real-callee cases for object_query and grid_sweep on generated height
 * grids, objects and a segment record built by the original math functions,
 * with every nested hook in rotating modes. The three accept compare;
 * compare_probes.h's installed compare cases run on the real-callee inputs
 * (set_length on its direct inputs). */
#include "es2/core/geometry.h"
typedef struct oq_record { uint32_t id,args[4],regs[3],input[12],eax,edx; } oq_record;
enum { OQ_SIZE=0xd00, OQ_OUT=0x00, OQ_SEG=0x20, OQ_POSE=0x60, OQ_POINTS=0x80, OQ_ANGLES=0x98, OQ_LIST=0xa0, OQ_HOLD=0xc0,
    OQ_VEC=0xf0, OQ_BACK=0x120, OQ_OBJ=0x140, OQ_OBJECTS=6, OQ_GRID=0x300, OQ_GRID_SIZE=0x4a0, OQ_MAX=128, OQ_IDS=9,
    OQ_GLOBALS=4, OQ_QUERY_STUBS=0x3f, OQ_SWEEP_STUBS=0x1c0 };
#define OQ_BASE ((uint32_t)(uintptr_t)oq_arena)
static BYTE oq_arena[OQ_SIZE],oq_before[OQ_SIZE],oq_after[OQ_SIZE];
static oq_record oq_log[OQ_MAX];
static unsigned oq_calls;
static uint32_t oq_seed,oq_stream,oq_step;
__attribute__((used)) static uint32_t oq_ecx;
/* Stub ids: 0..5 the callees of 427da8, 6..8 those of 4273c8. */
static const uint32_t oq_addresses[OQ_IDS]={0x49288c,0x48043c,0x47ebb8,0x47df19,0x480304,0x4273c8,0x4927f0,0x47de72,0x427238};
static const unsigned oq_counts[OQ_IDS]={2,3,2,1,3,4,2,3,3};
static uint32_t oq_next(void){
    uint32_t z=oq_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *oq_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static uint32_t oq_s16(uint16_t w){return (uint32_t)(int32_t)(int16_t)w;}
static unsigned oq_content(oq_record *v,unsigned at,uint32_t address,unsigned bytes){
    const BYTE *p=oq_host(address);
    for(unsigned j=0;j<bytes;j+=4){
        uint32_t w=0;for(unsigned k=0;k<4&&j+k<bytes;++k)w|=(uint32_t)p[j+k]<<(8*k);
        if(at==12){fail("object_query payload overflow");ExitProcess(1);}
        v->input[at++]=w;
    }
    return at;
}
/* 427da8's radius sum and reach for the current record (shape 0's radius
 * word; most cases give every shape the same word). */
static uint32_t oq_radius(void){return oq_s16(get16(oq_arena+OQ_GRID+8))+oq_s16(get16(oq_arena+OQ_SEG+8));}
static uint32_t oq_reach(void){return get32(oq_arena+OQ_SEG+4)+oq_radius();}
static uint32_t oq_around(uint32_t edge,uint32_t pick){return edge+pick%3u-1u;}
static uint32_t oq_distance(uint32_t pick){
    switch(pick%8){
    case 0:case 1:return oq_around(oq_reach(),pick>>4);
    case 2:return oq_around(get32(mapped+0xaaad0),pick>>4);
    case 3:return oq_around(500,pick>>4);
    case 4:return (pick>>4)%3000u;
    case 5:return oq_next();
    default:return oq_reach()-(pick>>4)%2000u;
    }
}
/* Segment-space x and y around the four box edges of 427e62..427e84. */
static void oq_local(uint32_t out,uint32_t pick){
    uint32_t r=oq_radius(),reach=oq_reach(),x,y;
    switch(pick%8){case 0:x=r;break;case 1:x=r-1u;break;case 2:x=0u-r;break;case 3:x=1u-r;break;
        case 4:x=r+1u;break;case 5:x=oq_next();break;default:x=(pick>>8)%3u-1u;}
    switch((pick>>3)%8){case 0:y=0u-r;break;case 1:y=1u-r;break;case 2:y=reach;break;case 3:y=reach-1u;break;
        case 4:y=reach+1u;break;case 5:y=oq_next();break;default:y=reach/2u;}
    put32(oq_host(out),x);put32(oq_host(out)+4,y);put32(oq_host(out)+8,oq_next());
}
/* 47de72 lengths for 4273c8: a few steps of the scaled step, the remainder
 * edges, and the word wrap of the sample count (0xffff and 0x10001 steps). */
static uint32_t oq_length(uint32_t pick){
    int32_t step=(int16_t)oq_step;
    switch(pick%10){
    case 0:return (uint32_t)step*0xffffu;
    case 1:return (uint32_t)step*0x10001u;
    case 2:return (uint32_t)(step*(int32_t)((pick>>8)%3)-1);
    case 3:return (uint32_t)(-step*(int32_t)((pick>>8)%3));
    default:return (uint32_t)(step*(int32_t)((pick>>8)%22-2)+(int32_t)((pick>>16)%(uint32_t)(step<0?-step:step)));
    }
}
__attribute__((used,noinline)) static uint64_t __cdecl oq_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    uint32_t eax=oq_next(),edx=oq_next(),pick=oq_next();oq_ecx=oq_next();
    if(oq_calls==OQ_MAX){fail("object_query synthetic call overflow");ExitProcess(1);}
    oq_record *v=oq_log+oq_calls++;memset(v,0,sizeof(*v));v->id=id;
    for(unsigned k=0;k<oq_counts[id];++k)v->args[k]=ai_stack_address(a[k])?0:a[k];
    for(unsigned j=0;j<3;++j)v->regs[j]=ai_stack_address(regs[j])?0:regs[j];
    unsigned at=0;
    switch(id){
    case 0: /* 49288c(point, point) */
        at=oq_content(v,at,a[0],12);at=oq_content(v,at,a[1],12);eax=oq_distance(pick);break;
    case 1: /* 48043c(point, pose, out) */
        at=oq_content(v,at,a[0],12);at=oq_content(v,at,a[1],32);oq_local(a[2],pick);break;
    case 2: /* 47ebb8(angles, matrix): nine words and the mode byte. */
        at=oq_content(v,at,a[0],6);for(unsigned j=0;j<19;++j)oq_host(a[1])[j]=(BYTE)oq_next();break;
    case 3: /* 47df19(matrix) in place */
        at=oq_content(v,at,a[0],20);if(pick%2)for(unsigned j=0;j<18;++j)oq_host(a[0])[j]=(BYTE)oq_next();break;
    case 4: /* 480304(source, matrix, destination) */
        at=oq_content(v,at,a[0],12);at=oq_content(v,at,a[1],20);for(unsigned j=0;j<12;++j)oq_host(a[2])[j]=(BYTE)oq_next();break;
    case 5: /* 4273c8(shape, out, segment, radius slot): a hit writes out+4..+0Fh. */
        at=oq_content(v,at,a[2],24);
        if(pick%2){eax|=1u<<((pick>>8)%16);for(unsigned j=4;j<16;++j)oq_host(a[1])[j]=(BYTE)oq_next();}
        else eax&=0xffff0000u;
        break;
    case 6: /* 4927f0(vector, step slot): a new step vector. */
        at=oq_content(v,at,a[0],12);
        for(unsigned j=0;j<3;++j)put32(oq_host(a[0])+4*j,pick%4?(uint32_t)((int32_t)(oq_next()%201u)-100):oq_next());
        if(pick%3==0)eax=a[0];
        break;
    case 7:eax=oq_length(pick);break;
    case 8: /* 427238(shape, point, radius slot): heights around the sample z. */
        {uint32_t z=get32(oq_host(a[1])+8);at=oq_content(v,at,a[1],12);
        switch(pick%6){case 0:case 1:eax=0;break;case 2:eax=z;break;case 3:eax=z+1u;break;case 4:eax=z-1u;break;default:eax=oq_next();}}
        break;
    default:fail("object_query stub id");ExitProcess(1);
    }
    v->eax=ai_stack_address(eax)?0:eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define OQ_STUB(n) __attribute__((naked)) static void oq_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _oq_stub_impl\n\taddl $24,%esp\n\tmovl _oq_ecx,%ecx\n\tret\n\t");}
OQ_STUB(0) OQ_STUB(1) OQ_STUB(2) OQ_STUB(3) OQ_STUB(4) OQ_STUB(5) OQ_STUB(6) OQ_STUB(7) OQ_STUB(8)
#undef OQ_STUB
static void *oq_stubs[OQ_IDS]={oq_stub_0,oq_stub_1,oq_stub_2,oq_stub_3,oq_stub_4,oq_stub_5,oq_stub_6,oq_stub_7,oq_stub_8};
static int oq_patch(BYTE saved[OQ_IDS][5],int active[OQ_IDS],unsigned mask,int on){
    for(unsigned i=0;i<OQ_IDS;++i){
        if(on){active[i]=0;if(!(mask>>i&1))continue;if(actor_patch(oq_addresses[i]-0x400000u,oq_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(oq_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
/* 4aaad0, 427da8's nearest out-of-reach distance. */
static void oq_globals(BYTE *out,int restore){if(restore)memcpy(mapped+0xaaad0,out,4);else memcpy(out,mapped+0xaaad0,4);}
static void oq_reset(void){oq_calls=0;oq_stream=oq_seed;}
static uint16_t oq_word(void){
    static const uint16_t words[]={0,1,0xffff,0x7fff,0x8000,2,0xfffe,100,0xff9c};
    return random32()%3?(uint16_t)(random32()%600-300):words[random32()%9];
}
static uint32_t oq_pick(const uint32_t *values,unsigned count){return values[random32()%count];}
static void oq_query_build(unsigned n,uint32_t a[6],uint32_t r[3]){
    uint32_t b=OQ_BASE,p=b+OQ_SEG;random_bytes(oq_arena,OQ_SIZE);memset(a,0,24);for(unsigned i=0;i<3;++i)r[i]=random32();
    int common=random32()%4!=0;uint16_t radius=oq_word();
    for(unsigned k=0;k<OQ_OBJECTS;++k){
        uint32_t o=b+OQ_OBJ+0x40*k,holder=b+OQ_HOLD+8*k,shape=b+OQ_GRID+0x10*k;
        put32(oq_host(o)+0x34,holder);put32(oq_host(holder)+4,shape);put16(oq_host(shape)+8,common?radius:oq_word());
        put16(oq_host(o)+0x32,(uint16_t)(random32()%2?0:random32()|1u));
    }
    static const uint32_t lengths[]={0,1,499,500,501,0xffffffffu,0x7fffffffu,0x80000000u};
    put32(oq_host(p),b+OQ_POSE);
    put32(oq_host(p)+4,random32()%4?random32()%20000u:random32()%2?oq_pick(lengths,8):random32());
    put16(oq_host(p)+8,oq_word());
    for(unsigned j=0;j<8;++j)put32(oq_arena+OQ_LIST+4*j,b+OQ_OBJ+0x40*(random32()%OQ_OBJECTS));
    static const uint16_t counts[]={0,1,2,3,4,6,8,0xffff,0x8000,0x8008};
    uint16_t count=n<1024?counts[n%10]:random32()%8?(uint16_t)(1+random32()%8):counts[random32()%10];
    static const uint32_t nearest[]={0,1,0x7fffffffu,0x80000000u,500};
    put32(mapped+0xaaad0,random32()%3?random32()%30000u:random32()%2?oq_pick(nearest,5):random32());
    a[0]=b+OQ_OUT;a[1]=p;a[2]=b+OQ_LIST;a[3]=(random32()&0xffff0000u)|count;
}
static void oq_sweep_build(unsigned n,uint32_t a[6],uint32_t r[3]){
    uint32_t b=OQ_BASE,s=b+OQ_GRID,pts=b+OQ_POINTS;random_bytes(oq_arena,OQ_SIZE);memset(a,0,24);for(unsigned i=0;i<3;++i)r[i]=random32();
    BYTE *g=oq_arena+OQ_GRID;
    put16(g+0x2a,(uint16_t)(n%16==0?0:random32()%8?1+random32()%16:random32()));
    static const uint16_t shifts[]={0,1,4,6,8,12,15,16,17,31,32,0xffff};
    put16(g+0x32,(uint16_t)(random32()%2?random32()%13:shifts[random32()%12]));
    put16(g+0x2e,random32()%2?(uint16_t)(random32()%16):(uint16_t)random32());
    put16(g+0x30,random32()%2?(uint16_t)(random32()%16):(uint16_t)random32());
    uint32_t top=random32()%2?random32()%4000u:random32();put32(g+0x430,top);
    for(unsigned j=0;j<2;++j){
        uint32_t z;switch(random32()%5){case 0:z=top;break;case 1:z=top+1u;break;case 2:z=top-1u;break;case 3:z=random32();break;default:z=top-random32()%3000u;}
        put32(oq_host(pts)+12*j,random32()%2?random32()%4096u-2048u:random32());
        put32(oq_host(pts)+12*j+4,random32()%2?random32()%4096u-2048u:random32());
        put32(oq_host(pts)+12*j+8,z);
    }
    uint16_t radius=oq_word();unsigned k=get16(g+0x32)&31u;
    if((uint16_t)((1u<<k)+radius)==0)++radius;
    oq_step=(uint16_t)((1u<<k)+radius);
    a[0]=s;a[1]=b+OQ_OUT;a[2]=pts;a[3]=(random32()&0xffff0000u)|radius;
}
static void oq_length_build(unsigned n,uint32_t a[6],uint32_t r[3]){
    uint32_t b=OQ_BASE;random_bytes(oq_arena,OQ_SIZE);memset(a,0,24);for(unsigned i=0;i<3;++i)r[i]=random32();
    uint32_t v=b+OQ_VEC+random32()%16;
    static const uint32_t parts[]={0,1,0xffffffffu,0x7fffffffu,0x80000000u,0x10000u,0xffff0000u};
    for(unsigned j=0;j<3;++j){uint32_t c;
        switch((n%8==0)?0:1+random32()%6){case 0:c=0;break;case 1:c=random32()%10001u-5000u;break;
            case 2:c=random32()%2?random32()%0x100000u:0u-random32()%0x100000u;break;case 3:c=random32();break;default:c=oq_pick(parts,7);}
        put32(oq_host(v)+4*j,c);}
    static const uint16_t lengths[]={0,1,0xffff,100,1000,0x7fff,0x8000,0x400};
    uint16_t length=random32()%3?lengths[random32()%8]:(uint16_t)random32();
    /* (-32768 << 16) / -1 faults in the original (weapons.md). */
    if(length==0x8000&&es2_distance3(get32(oq_host(v)),get32(oq_host(v)+4),get32(oq_host(v)+8))==0xffffffffu)length=0x7fff;
    a[0]=v;a[1]=(random32()&0xffff0000u)|length;
}
static void oq_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    oq_seed=random32();
    if(index==ES2_HOOK_INDEX_object_query)oq_query_build(n,a,r);
    else if(index==ES2_HOOK_INDEX_grid_sweep)oq_sweep_build(n,a,r);
    else oq_length_build(n,a,r);
}
static int oq_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const oq_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,oq_calls);
    for(unsigned j=0;j<calls&&j<oq_calls&&expected;++j)if(memcmp(expected+j,oq_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(oq_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<OQ_SIZE;++j)if(oq_after[j]!=oq_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,oq_after[j],oq_arena[j]);break;}
    return 1;
}
/* Counter deltas: stub log entries, or for set_length the memset of a zero
 * vector (its callees run as original code). */
static unsigned oq_expected_hits(unsigned index,const callee_counter *c,const uint32_t *a){
    if(index==ES2_HOOK_INDEX_set_length){const BYTE *v=oq_before+(a[0]-OQ_BASE);
        return c->key==0x49681a&&!es2_distance3(get32(v),get32(v+4),get32(v+8));}
    unsigned hits=0;for(unsigned j=0;j<oq_calls;++j)hits+=oq_addresses[oq_log[j].id]==c->key;return hits;
}
static int oq_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[OQ_GLOBALS],after[OQ_GLOBALS],actual[OQ_GLOBALS];static oq_record expected[OQ_MAX];
    memcpy(oq_before,oq_arena,OQ_SIZE);oq_globals(before,0);
    oq_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=oq_calls;memcpy(expected,oq_log,calls*sizeof(*expected));
    memcpy(oq_after,oq_arena,OQ_SIZE);oq_globals(after,0);
    memcpy(oq_arena,oq_before,OQ_SIZE);oq_globals(before,1);oq_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);oq_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=oq_expected_hits(index,c+i,a))return fail("object_query callee counter delta");
    if(pair[0]!=pair[1]||calls!=oq_calls||memcmp(expected,oq_log,calls*sizeof(*expected))||memcmp(oq_after,oq_arena,OQ_SIZE)||memcmp(after,actual,OQ_GLOBALS)){
        for(unsigned j=0;j<OQ_GLOBALS;++j)if(after[j]!=actual[j])fprintf(stderr," global byte %u %02x/%02x\n",j,after[j],actual[j]);
        return oq_report("direct",n,pair,calls,expected);
    }
    return 0;
}
static int oq_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;oq_build(index,n,a,r);
        BYTE before[OQ_GLOBALS],after[OQ_GLOBALS],actual[OQ_GLOBALS],expected_fpu[108],actual_fpu[108];static oq_record expected[OQ_MAX];
        memcpy(oq_before,oq_arena,OQ_SIZE);oq_globals(before,0);
        oq_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=oq_calls;memcpy(expected,oq_log,calls*sizeof(*expected));memcpy(oq_after,oq_arena,OQ_SIZE);oq_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("object_query poison install");
        memcpy(oq_arena,oq_before,OQ_SIZE);oq_globals(before,1);oq_reset();sg_poison_call(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);oq_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        /* The grid_sweep width-0 exit returns the entry EDX, which is the
         * poison pattern in both runs. */
        if(pair[0]!=pair[1]||calls!=oq_calls||memcmp(expected,oq_log,calls*sizeof(*expected))||memcmp(oq_after,oq_arena,OQ_SIZE)||
           memcmp(after,actual,OQ_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=oq_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("object_query poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state\n",d->name,total);
    return status;
}
/* Real callees. Height grids of 2..8 x 2..8 cells (shift 7..9, origin at
 * the centre, heights 0..2499 through a 256-entry table), objects around a
 * segment whose record (forward pose, length, inverse pose) is built by the
 * original 492990, 47ebb8, 49288c, 47df19 and 480304 before any hook is
 * installed. */
static void oq_real_grid(uint32_t g){
    BYTE *p=oq_host(g);unsigned cols=2+random32()%7,rows=2+random32()%7,shift=7+random32()%3;
    put16(p+8,(uint16_t)(random32()%300));put16(p+0x2a,(uint16_t)cols);put16(p+0x2c,(uint16_t)rows);
    put16(p+0x2e,(uint16_t)(cols/2));put16(p+0x30,(uint16_t)(rows/2));put16(p+0x32,(uint16_t)shift);
    for(unsigned j=0;j<256;++j)put32(p+0x34+4*j,random32()%8?random32()%2500u:0u-random32()%500u);
    put32(p+0x430,random32()%4?2500u:random32()%3000u);put32(p+0x434,g+0x438);
    for(unsigned y=0;y<8;++y){put32(p+0x438+4*y,g+0x460+8*y);for(unsigned x=0;x<8;++x)p[0x460+8*y+x]=(BYTE)random32();}
}
static uint32_t oq_signed(uint32_t range){return random32()%(2*range+1)-range;}
static void oq_real_segment(uint32_t start,uint32_t end,uint16_t radius){
    uint32_t b=OQ_BASE,p=b+OQ_SEG,m=b+OQ_POSE,angles=b+OQ_ANGLES,back=b+OQ_BACK;
    ((uint64_t (__cdecl *)(uint32_t,uint32_t,uint32_t))(void *)(mapped+0x92990))(angles,end,start);
    put32(oq_host(m)+0x14,get32(oq_host(start)));put32(oq_host(m)+0x18,get32(oq_host(start)+4));put32(oq_host(m)+0x1c,get32(oq_host(start)+8));
    ((uint64_t (__cdecl *)(uint32_t,uint32_t))(void *)(mapped+0x7ebb8))(angles,m);
    put32(oq_host(p),m);
    put32(oq_host(p)+4,(uint32_t)((uint64_t (__cdecl *)(uint32_t,uint32_t))(void *)(mapped+0x9288c))(end,start));
    put16(oq_host(p)+8,radius);
    memcpy(oq_host(p)+0xa,oq_host(m),20);
    for(unsigned j=0;j<3;++j)put32(oq_host(p)+0x1eu+4*j,get32(oq_host(start)+4*j));
    ((uint64_t (__cdecl *)(uint32_t))(void *)(mapped+0x7df19))(p+0xa);
    for(unsigned j=0;j<3;++j){uint32_t v=0u-get32(oq_host(p)+0x1eu+4*j);put32(oq_host(p)+0x1eu+4*j,v);put32(oq_host(back)+4*j,v);}
    ((uint64_t (__cdecl *)(uint32_t,uint32_t,uint32_t))(void *)(mapped+0x80304))(back,p+0xa,p+0x1e);
}
static void oq_real_build(unsigned index,uint32_t a[6],uint32_t r[3]){
    uint32_t b=OQ_BASE,start=b+OQ_POINTS,end=b+OQ_POINTS+12;random_bytes(oq_arena,OQ_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    for(unsigned j=0;j<2;++j)oq_real_grid(b+OQ_GRID+OQ_GRID_SIZE*j);
    uint32_t sx=oq_signed(20000),sy=oq_signed(20000),sz=random32()%3000;
    uint32_t ex=sx+oq_signed(6000),ey=sy+oq_signed(6000),ez=random32()%3?sz:sz+oq_signed(1500);
    put32(oq_host(start),sx);put32(oq_host(start)+4,sy);put32(oq_host(start)+8,sz);
    put32(oq_host(end),ex);put32(oq_host(end)+4,ey);put32(oq_host(end)+8,ez);
    if(index==ES2_HOOK_INDEX_grid_sweep){
        /* Object-space points within and around the grid extent. */
        uint32_t g=b+OQ_GRID+OQ_GRID_SIZE*(random32()%2);unsigned shift=get16(oq_host(g)+0x32);
        uint32_t w=get16(oq_host(g)+0x2a)<<shift,h=get16(oq_host(g)+0x2c)<<shift;
        for(unsigned j=0;j<2;++j){put32(oq_host(start)+12*j,oq_signed(w));put32(oq_host(start)+12*j+4,oq_signed(h));
            put32(oq_host(start)+12*j+8,random32()%3000u-200u);}
        a[0]=g;a[1]=b+OQ_OUT;a[2]=start;a[3]=(random32()&0xffff0000u)|(random32()%200);return;
    }
    oq_real_segment(start,end,(uint16_t)(random32()%400));
    for(unsigned k=0;k<OQ_OBJECTS;++k){
        uint32_t o=b+OQ_OBJ+0x40*k,holder=b+OQ_HOLD+8*k;BYTE *q=oq_host(o);
        uint32_t t=random32()%1025;
        put32(q+0x26,sx+(uint32_t)((int32_t)(ex-sx)*(int32_t)t/1024)+oq_signed(1500));
        put32(q+0x2a,sy+(uint32_t)((int32_t)(ey-sy)*(int32_t)t/1024)+oq_signed(1500));
        put32(q+0x2e,sz+oq_signed(1500));
        put16(q+0xc,(uint16_t)(random32()%4?0:oq_signed(2000)));put16(q+0xe,(uint16_t)(random32()%4?0:oq_signed(2000)));
        put16(q+0x10,(uint16_t)random32());
        put16(q+0x32,0);
        if(random32()%4==0){((uint64_t (__cdecl *)(uint32_t,uint32_t))(void *)(mapped+0x7ebb8))(o+0xc,o+0x12);put16(q+0x32,1);}
        put32(q+0x34,holder);put32(oq_host(holder)+4,b+OQ_GRID+OQ_GRID_SIZE*(random32()%2));
    }
    unsigned count=random32()%8==0?0:1+random32()%OQ_OBJECTS;
    for(unsigned j=0;j<8;++j)put32(oq_arena+OQ_LIST+4*j,b+OQ_OBJ+0x40*(random32()%OQ_OBJECTS));
    put32(mapped+0xaaad0,random32()%2?random32()%30000u:random32());
    a[0]=b+OQ_OUT;a[1]=b+OQ_SEG;a[2]=b+OQ_LIST;a[3]=(random32()&0xffff0000u)|count;
}
static int oq_real_cases(unsigned index){
    static const unsigned query_nested[]={ES2_HOOK_INDEX_grid_sweep,ES2_HOOK_INDEX_set_length,ES2_HOOK_INDEX_cell,
        ES2_HOOK_INDEX_dist3d_delta,ES2_HOOK_INDEX_transform_point,ES2_HOOK_INDEX_dist_gate,ES2_HOOK_INDEX_matrix_transpose,
        ES2_HOOK_INDEX_rotate_point,ES2_HOOK_INDEX_euler_matrix};
    static const unsigned sweep_nested[]={ES2_HOOK_INDEX_set_length,ES2_HOOK_INDEX_cell,ES2_HOOK_INDEX_dist3d_delta};
    const int query=index==ES2_HOOK_INDEX_object_query;
    const unsigned *nested=query?query_nested:sweep_nested,count=query?9:3;
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0,hits=0;uint64_t entered[10]={0};
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<1024&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;oq_real_build(index,a,r);
        BYTE before[OQ_GLOBALS],after[OQ_GLOBALS],actual[OQ_GLOBALS];
        memcpy(oq_before,oq_arena,OQ_SIZE);oq_globals(before,0);
        es2_hook hk[11];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("object_query nested install");}
        sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        memcpy(oq_after,oq_arena,OQ_SIZE);oq_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+count,d,&o,function_hook_extension(index)))return fail("object_query caller mode install");
        memcpy(oq_arena,oq_before,OQ_SIZE);oq_globals(before,1);
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);oq_globals(actual,0);
        /* object_query's EDX can be a callee's frame-address residue (dead at
         * both callers, weapons.md); EAX and grid_sweep's pair are data. */
        int same=expected_pair[0]==actual_pair[0]&&(expected_pair[1]==actual_pair[1]||
            (query&&ai_stack_address(expected_pair[1])&&ai_stack_address(actual_pair[1])));
        if(!same||memcmp(oq_after,oq_arena,OQ_SIZE)||memcmp(after,actual,OQ_GLOBALS)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            fprintf(stderr,"real mode=%u\n",mode);oq_calls=0;status=oq_report("real",n,pair,0,NULL);
        }
        hits+=(uint16_t)expected_pair[0]!=0;
        for(unsigned j=0;j<=count;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("object_query nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("object_query nested uninstall");}
        oq_globals(before,1);++total;
    }
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){
        fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&(!hits||hits==total))status=fail("object_query real cases without both outcomes");
    if(!status){printf("PASS: %s %u real-callee cases on generated grids%s (%u hits), mixed nested modes, 31/a7 stack, %s, memory and globals\n",
        d->name,total,query?", objects and segment records":"",hits,query?"EAX (EDX unless frame residue)":"EDX:EAX");
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
/* Compare cases (compare_probes.h) with real callees. object_query's EAX high
 * half and EDX can be callee or frame residue that moves with the stack depth
 * of the original (both callers test AX only). */
static void oq_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    oq_seed=random32();if(index==ES2_HOOK_INDEX_set_length)oq_length_build(n,a,r);else oq_real_build(index,a,r);
}
static void oq_compare_state(BYTE *s,int restore){
    if(restore){memcpy(oq_arena,s,OQ_SIZE);oq_globals(s+OQ_SIZE,1);}else{memcpy(s,oq_arena,OQ_SIZE);oq_globals(s+OQ_SIZE,0);}
}
static int oq_residue(uint32_t x){return ai_stack_address(x)||ai_stack_address((x&0xffff0000u)|0xffffu);}
static int oq_compare_same(unsigned index,uint64_t want,uint64_t got){
    if(want==got)return 1;
    if(index!=ES2_HOOK_INDEX_object_query||(uint16_t)want!=(uint16_t)got)return 0;
    const uint32_t a=(uint32_t)want>>16,b=(uint32_t)got>>16,c=(uint32_t)(want>>32),d=(uint32_t)(got>>32);
    return (a==b||(oq_residue(a<<16)&&oq_residue(b<<16)))&&(c==d||(oq_residue(c)&&oq_residue(d)));
}
static int oq_compare_run(unsigned index,unsigned limit){
    compare_probe_same=oq_compare_same;
    int status=compare_probe_run(index,limit,oq_compare_build,oq_compare_state,OQ_SIZE+OQ_GLOBALS);
    compare_probe_same=NULL;return status;
}
static int object_query_cases(unsigned index,unsigned limit){
    BYTE saved[OQ_IDS][5],old_globals[OQ_GLOBALS];int active[OQ_IDS];ai_stack_range();
    if(limit&&compare_probe_installed(index)){oq_globals(old_globals,0);int status=oq_compare_run(index,limit);oq_globals(old_globals,1);return status;}
    unsigned mask=index==ES2_HOOK_INDEX_object_query?OQ_QUERY_STUBS:index==ES2_HOOK_INDEX_grid_sweep?OQ_SWEEP_STUBS:0;
    oq_globals(old_globals,0);if(oq_patch(saved,active,mask,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_OBJECT_QUERY_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];oq_build(index,n,a,r);status=oq_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=oq_installed_probes(index,64);
    if(oq_patch(saved,active,0,0))return 1;
    current_case_valid=0;
    if(!limit&&!status&&mask)status=oq_real_cases(index);
    if(!limit&&!status)status=oq_compare_run(index,0);
    oq_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, frame payloads by content, arena/global\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
