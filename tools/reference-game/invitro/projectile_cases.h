/* Projectile and bullet steering: vector_angle 49290c, aim_angles 492990,
 * projectile_steer 40a254, bullet_steer 40aff0.
 *
 * vector_angle / aim_angles: pure helpers. Direct cases compare the
 * original's EAX, EDX and ECX (the entry shims make ECX part of the hooks'
 * contract) and the arena (out may overlap the points); generated inputs
 * stay in the angle_ratio domain. Installed 31/a7 poison probes compare all
 * three registers through the shims. A real-caller pass runs the original
 * callers 492934, 49295c and 492990 with vector_angle installed.
 *
 * projectile_steer / bullet_steer: direct cases run the original body and
 * the core against logging stubs for every callee (virtual +24/+58 in a
 * synthetic vtable, 47ebb8, 48043c, 492990, 467adc, 467924): entry
 * registers and argument slots in call order, the target point by content.
 * Frame addresses differ between the runs, so a value inside the worker's
 * stack reservation is recorded relative to the frame base (the first
 * frame address a callee receives). Stub results never point into the
 * stack. Then installed poison probes and a real-callee pass: the original
 * or replaced steering body over the real 492990/47ebb8/48043c/467adc/
 * 467924 with aim_angles, vector_angle, euler_matrix, transform_point,
 * approach_word and movement_step nested in rotating modes, against a
 * reference run with nothing installed. */
#include "es2/core/trig.h"
typedef struct pj_record { uint32_t id,args[3],regs[3],input[3],eax,edx; } pj_record;
enum { PJ_SIZE=0x800, PJ_O=0x40, PJ_T=0x140, PJ_T2=0x240, PJ_OWNER=0x340, PJ_VT=0x500, PJ_ATT=0x580,
    PJ_END=0x600, PJ_START=0x640, PJ_OUT=0x680, PJ_MAX=16, PJ_IDS=7 };
#define PJ_B ((uint32_t)(uintptr_t)pj_arena)
static BYTE pj_arena[PJ_SIZE],pj_before[PJ_SIZE],pj_after[PJ_SIZE];
static pj_record pj_log[PJ_MAX],pj_expected[PJ_MAX];
static unsigned pj_calls;static int pj_real;
static uint32_t pj_seed,pj_stream,pj_base;
__attribute__((used)) static uint32_t pj_ecx;
/* V24, V58 (slots of the target's vtable), 47ebb8, 48043c, 492990, 467adc, 467924. */
static const uint32_t pj_addresses[PJ_IDS]={0,0,0x47ebb8,0x48043c,0x492990,0x467adc,0x467924};
static const unsigned pj_counts[PJ_IDS]={1,3,2,3,3,3,1},pj_slots[PJ_IDS]={0x24,0x58,0,0,0,0,0};
static uint32_t pj_next(void){
    uint32_t z=pj_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *pj_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int pj_window(uint32_t v){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-v-1u<0x1000000u;
}
/* A stub result must not look like a stack address, even after a 16-bit
 * load replaces its low word. */
static uint32_t pj_clean(uint32_t v){return pj_window(v&0xffff0000u)||pj_window(v|0xffffu)?v^0x80000000u:v;}
static uint32_t pj_mask(uint32_t v){
    if(!pj_window(v))return v;
    return pj_base?0x5ac00000u|((v-pj_base)&0xffffu):0x5acfffffu;
}
static uint32_t pj_small(void){return pj_next()%0x200001u-0x100000u;}
static uint16_t pj_delta(uint32_t pick){
    switch(pick%7){case 0:return (uint16_t)(pj_next()%0x1800u-0xc00u);case 1:return 0xf400;case 2:return 0xf3ff;
        case 3:return 0x0bff;case 4:return 0x0c00;case 5:return (uint16_t)(pj_next()%0x1800u-0xc00u);default:return (uint16_t)pj_next();}
}
__attribute__((used,noinline)) static uint64_t __cdecl pj_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(pj_calls==PJ_MAX){fail("projectile synthetic call overflow");ExitProcess(1);}
    if(!pj_base){if(id==1||id==3)pj_base=a[2]+0x28u;else if(id==4)pj_base=a[0]+0x10u;}
    pj_record *v=pj_log+pj_calls++;memset(v,0,sizeof(*v));v->id=id;
    for(unsigned j=0;j<pj_counts[id];++j)v->args[j]=pj_mask(a[j]);
    for(unsigned j=0;j<3;++j)v->regs[j]=pj_mask(regs[j]);
    uint32_t eax=pj_clean(pj_next()),edx=pj_clean(pj_next()),pick=pj_next();pj_ecx=pj_clean(pj_next());
    switch(id){
    case 0: /* V24: no attachment, the attachment record, or (stubbed callees only) any value. */
        eax=pick%3==0?0:pick%3==1||pj_real?PJ_B+PJ_ATT:eax;
        if(!pj_real&&pick%7==0){BYTE *t=pj_host(a[0]);put16(t+0x32,(uint16_t)((pick>>8)%2?0:1+pj_next()%9));}
        if(!pj_real&&pick%11==0){BYTE *o=pj_arena+PJ_O;uint32_t other=(pick>>9)%2?PJ_B+PJ_T2:PJ_B+PJ_T;put32(o+0x56,other);put32(o+0x5b,other);}
        break;
    case 1: case 3: { /* V58 / 48043c: the 12-byte target point. */
        BYTE *out=pj_host(a[2]);for(unsigned j=0;j<3;++j)put32(out+4*j,pj_real||pick%2?pj_small():pj_next());
        if(id==1)v->input[0]=a[1];
        break; }
    case 2: /* 47ebb8: may scribble on the cache word the caller sets afterwards. */
        if(pick%5==0)put16(pj_host(a[0])+0x26,(uint16_t)pj_next());
        break;
    case 4: { /* 492990: target point by content; angles near the push band. */
        BYTE *end=pj_host(a[1]),*o=pj_host(a[2]-0x26u),*out=pj_host(a[0]);
        for(unsigned j=0;j<3;++j)v->input[j]=get32(end+4*j);
        put16(out,(uint16_t)(get16(o+0xc)+pj_delta(pick)));put16(out+2,(uint16_t)pj_next());
        put16(out+4,(uint16_t)(get16(o+0x10)+pj_delta(pick>>4)));
        break; }
    case 5: { /* 467adc: the word before, then a new value. */
        BYTE *p=pj_host(a[0]);v->input[0]=get16(p);put16(p,(uint16_t)(pick%2?a[1]:pj_next()));
        break; }
    default: break;
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define PJ_STUB(n) __attribute__((naked)) static void pj_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _pj_stub_impl\n\taddl $24,%esp\n\tmovl _pj_ecx,%ecx\n\tret\n\t");}
PJ_STUB(0) PJ_STUB(1) PJ_STUB(2) PJ_STUB(3) PJ_STUB(4) PJ_STUB(5) PJ_STUB(6)
#undef PJ_STUB
static void *pj_stubs[PJ_IDS]={pj_stub_0,pj_stub_1,pj_stub_2,pj_stub_3,pj_stub_4,pj_stub_5,pj_stub_6};
static void pj_reset(void){pj_calls=0;pj_stream=pj_seed;pj_base=0;}
/* A six-slot caller over patterned stack that also returns ECX. */
__attribute__((naked)) static void pj_poison_call(void *target,const uint32_t *a,uint32_t pattern,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 16(%ebp),%eax\n\tmovl $1024,%ecx\n\trep stosl\n\taddl $3840,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 20(%esi)\n\tpushl 16(%esi)\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%eax\n\tmovl %eax,%edx\n\tmovl %eax,%ecx\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%ebx\n\tmovl %eax,(%ebx)\n\tmovl %edx,4(%ebx)\n\tmovl %ecx,8(%ebx)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static const BYTE *pj_corrections(void){return mapped+0xa1c34;}
static uint32_t pj_value(unsigned n){
    static const uint32_t edges[]={0,1,0xffffffffu,0x80000000u,0x7fffffffu,0x10000,0xffff0000u,0x4000,0xffffc000u,2,0xfffffffeu};
    switch(n<512?n%5:random32()%6){
    case 0:return edges[random32()%11];
    case 1:return random32()%0x8001u-0x4000u;
    case 2:return random32()%0x2000001u-0x1000000u;
    case 3:return random32()%2?0:random32()%64u-32u;
    default:return random32();
    }
}
static int pj_angle_valid(uint32_t x,uint32_t y){return es2_angle_ratio_valid(x||y?x:1u,y);}
/* Both angle_ratio calls of 492990 in their domain, z read after the writes. */
static int pj_aim_valid(const uint32_t *a){
    BYTE *out=pj_host(a[0]),*end=pj_host(a[1]),*start=pj_host(a[2]);
    uint32_t dx=get32(end)-get32(start),dy=get32(end+4)-get32(start+4);
    if(!pj_angle_valid(dx,dy))return 0;
    uint64_t h=es2_vector_angle_registers(dx,dy,pj_corrections());
    memcpy(pj_after,pj_arena,PJ_SIZE);put16(out+4,(uint16_t)((uint32_t)h+0xc000u));put16(out+2,0);
    uint32_t dz=get32(end+8)-get32(start+8);memcpy(pj_arena,pj_after,PJ_SIZE);
    return pj_angle_valid(es2_distance2(dx,dy),dz);
}
static void pj_build_helper(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    random_bytes(pj_arena,PJ_SIZE);memset(a,0,24);for(unsigned i=0;i<3;++i)r[i]=random32();
    if(index==ES2_HOOK_INDEX_vector_angle){
        for(unsigned tries=0;;++tries){
            a[0]=pj_value(n);a[1]=pj_value(n/5);
            if(n%13==0)a[0]=0;
            if(n%17==0)a[1]=0;
            if(n%29==0)a[0]=a[1]=0;
            if(pj_angle_valid(a[0],a[1]))break;
            if(tries>8){a[0]=random32()%0x10000u;a[1]=random32()%0x10000u-0x8000u;}
        }
        return;
    }
    uint32_t b=PJ_B;
    for(unsigned tries=0;;++tries){
        for(unsigned j=0;j<3;++j){
            uint32_t s=tries>8?random32()%4096u:pj_value(n),d=tries>8?random32()%4096u-2048u:pj_value(n/7);
            if(n%19==j)d=0;
            put32(pj_arena+PJ_START+4*j,s);put32(pj_arena+PJ_END+4*j,s+d);
        }
        if(n%23==0)for(unsigned j=0;j<3;++j)put32(pj_arena+PJ_END+4*j,get32(pj_arena+PJ_START+4*j));
        a[1]=b+PJ_END;a[2]=b+PJ_START;a[0]=b+PJ_OUT;
        /* out on the end or start point (the z read follows two writes), or unaligned. */
        unsigned overlap=random32()%16;
        if(overlap<3)a[0]=b+PJ_END+random32()%11;
        else if(overlap<5)a[0]=b+PJ_START+random32()%11;
        else if(overlap==5)a[0]=b+PJ_END+6;
        else if(overlap==6)a[0]=b+PJ_OUT+1+random32()%3;
        if(pj_aim_valid(a))break;
    }
}
static void pj_build_steer(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    uint32_t b=PJ_B;pj_seed=random32();random_bytes(pj_arena,PJ_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    static const BYTE types[]={0,1,2,3,4,9,0x80,0xff,2,3,2,3};
    BYTE *o=pj_arena+PJ_O,*owner=pj_arena+PJ_OWNER;
    for(unsigned k=0;k<2;++k){
        BYTE *t=pj_arena+(k?PJ_T2:PJ_T);
        put32(t,b+PJ_VT);t[0x96]=(BYTE)(random32()%2?0:random32()|1u);t[0xa1]=(BYTE)(random32()%2?0:random32()|1u);
        put16(t+0x32,(uint16_t)(random32()%2?0:1u+random32()%9u));
        if(pj_real){
            for(unsigned j=0;j<9;++j)put16(t+0x12+2*j,(uint16_t)(random32()%32769u-16384u));
            static const BYTE modes[]={0,1,2,0x80,0xff};t[0x24]=modes[random32()%5];
            for(unsigned j=0;j<3;++j)put32(t+0x26+4*j,random32()%0x200001u-0x100000u);
        }
    }
    put32(pj_arena+PJ_VT+0x24,(uint32_t)(uintptr_t)pj_stubs[0]);put32(pj_arena+PJ_VT+0x58,(uint32_t)(uintptr_t)pj_stubs[1]);
    uint32_t target=random32()%5==0?b+PJ_T2:b+PJ_T;
    /* O+5Ah (the projectile's part word) shares a byte with O+5Bh (the
     * bullet's target): each class writes only its own field. */
    if(index==ES2_HOOK_INDEX_projectile_steer){put32(o+0x56,n%8==0?0:target);put16(o+0x5a,(uint16_t)random32());}
    else put32(o+0x5b,target);
    put32(o+0x4a,b+PJ_OWNER);
    owner[0xa3]=(BYTE)(random32()%2?0:random32()|1u);
    put32(owner+0x1a4,random32()%2?get32(o+0x56):random32()%2?b+PJ_T2:random32());
    owner[0x9c]=(BYTE)(random32()%3==0?0:random32()|1u);
    o[0x41]=types[random32()%12];
    if(pj_real){
        for(unsigned j=0;j<3;++j){put32(o+0x26+4*j,random32()%0x200001u-0x100000u);
            put32(pj_arena+PJ_ATT+0x14+4*j,random32()%0x20001u-0x10000u);}
    }
    a[0]=b+PJ_O;
}
static int pj_report(const char *what,unsigned n,const uint32_t *want,const uint32_t *got,unsigned regs,unsigned calls,const pj_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s)",current_function,n,what);
    for(unsigned j=0;j<regs;++j)fprintf(stderr," %08x/%08x",want[j],got[j]);
    fprintf(stderr," calls %u/%u\n",calls,pj_calls);
    for(unsigned j=0;j<calls&&j<pj_calls&&expected;++j)if(memcmp(expected+j,pj_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(pj_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<PJ_SIZE;++j)if(pj_after[j]!=pj_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,pj_after[j],pj_arena[j]);break;}
    return 1;
}
/* vector_angle / aim_angles: original EAX/EDX/ECX against the core. */
static int pj_helper_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    memcpy(pj_before,pj_arena,PJ_SIZE);
    es2_weapon_registers o={r[0],r[1],r[2]};weapons_machine_call((uint32_t)(uintptr_t)(mapped+d->rva),a,d->arg_count,&o);
    memcpy(pj_after,pj_arena,PJ_SIZE);memcpy(pj_arena,pj_before,PJ_SIZE);
    uint32_t ecx=index==ES2_HOOK_INDEX_vector_angle?es2_vector_angle_ecx(a[0],a[1]):get32(pj_host(a[1]))-get32(pj_host(a[2]));
    uint64_t p=x->replacement_context(a,d->user,r);
    uint32_t want[3]={o.eax,o.edx,o.ecx},got[3]={(uint32_t)p,(uint32_t)(p>>32),ecx};
    if(memcmp(want,got,12)||memcmp(pj_after,pj_arena,PJ_SIZE))return pj_report("direct",n,want,got,3,0,NULL);
    /* The pair through the framework's own call path as well. */
    memcpy(pj_arena,pj_before,PJ_SIZE);uint64_t q=weapons_original(index,a,r);memcpy(pj_arena,pj_after,PJ_SIZE);
    if(q!=p)return pj_report("direct pair",n,want,got,3,0,NULL);
    return 0;
}
static unsigned pj_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<pj_calls;++j){unsigned id=pj_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?pj_slots[id]==c->key:!pj_slots[id]&&pj_addresses[id]==c->key)++hits;}
    return hits;
}
static int pj_steer_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    memcpy(pj_before,pj_arena,PJ_SIZE);
    pj_reset();uint64_t want=weapons_original(index,a,r);unsigned calls=pj_calls;memcpy(pj_expected,pj_log,calls*sizeof(*pj_expected));
    memcpy(pj_after,pj_arena,PJ_SIZE);memcpy(pj_arena,pj_before,PJ_SIZE);pj_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    uint64_t got=x->replacement_context(a,d->user,r);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=pj_expected_hits(c+i))return fail("projectile callee counter delta");
    if(want!=got||calls!=pj_calls||memcmp(pj_expected,pj_log,calls*sizeof(*pj_expected))||memcmp(pj_after,pj_arena,PJ_SIZE)){
        uint32_t w[2]={(uint32_t)want,(uint32_t)(want>>32)},g[2]={(uint32_t)got,(uint32_t)(got>>32)};
        return pj_report("direct",n,w,g,2,calls,pj_expected);
    }
    return 0;
}
static int pj_patch(BYTE saved[PJ_IDS][5],int active[PJ_IDS],int on){
    for(unsigned i=0;i<PJ_IDS;++i){
        if(on){active[i]=0;if(!pj_addresses[i])continue;if(actor_patch(pj_addresses[i]-0x400000u,pj_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(pj_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static int pj_steer(unsigned index){return index==ES2_HOOK_INDEX_projectile_steer||index==ES2_HOOK_INDEX_bullet_steer;}
static int pj_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],want[3],got[3],pattern=poison?0xa7a7a7a7u:0x31313131u;
        if(pj_steer(index))pj_build_steer(index,n,a,r);else pj_build_helper(index,n,a,r);
        BYTE expected_fpu[108],actual_fpu[108];
        memcpy(pj_before,pj_arena,PJ_SIZE);pj_reset();pj_poison_call(mapped+d->rva,a,pattern,want);es2_hook_fpu_save(expected_fpu);
        unsigned calls=pj_calls;memcpy(pj_expected,pj_log,calls*sizeof(*pj_expected));memcpy(pj_after,pj_arena,PJ_SIZE);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("projectile poison install");
        memcpy(pj_arena,pj_before,PJ_SIZE);pj_reset();pj_poison_call(mapped+d->rva,a,pattern,got);es2_hook_fpu_save(actual_fpu);
        /* The steering hooks' ECX is dead at both callers (not reproduced). */
        unsigned regs=pj_steer(index)?2:3;
        if(memcmp(want,got,4*regs)||calls!=pj_calls||memcmp(pj_expected,pj_log,calls*sizeof(*pj_expected))||memcmp(pj_after,pj_arena,PJ_SIZE)||
           memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=pj_report(fast?"installed fast":"installed preserving",n,want,got,regs,calls,pj_expected);
        if(!es2_hook_uninstall(&h))return fail("projectile poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, %s\n",d->name,total,
        pj_steer(index)?"pair/trace/arena":"EAX/EDX/ECX and arena");
    return status;
}
/* vector_angle under its real callers: heading_point 492934 (P, Q),
 * 49295c (P, Q; returns 49290c's ECX) and 492990 (out, end, start), with
 * the hook replaced or in compare mode against a reference run with
 * nothing installed. EAX, EDX, ECX and the arena are compared. */
static int pj_vector_real(unsigned index){
    static const uint32_t callers[]={0x92934,0x9295c,0x92990};
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered=0;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],want[3],got[3],pattern=n&1?0xa7a7a7a7u:0x31313131u,caller=callers[mode%3];
        for(;;){
            pj_build_helper(ES2_HOOK_INDEX_aim_angles,n,a,r);
            if(caller==0x92990u)break;
            /* 49295c reads z without the intervening writes of 492990. */
            const BYTE *e=pj_host(a[1]),*s=pj_host(a[2]);
            if(pj_angle_valid(es2_distance2(get32(e)-get32(s),get32(e+4)-get32(s+4)),get32(e+8)-get32(s+8))){a[0]=a[1];a[1]=a[2];break;}
        }
        memcpy(pj_before,pj_arena,PJ_SIZE);pj_poison_call(mapped+caller,a,pattern,want);memcpy(pj_after,pj_arena,PJ_SIZE);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        o.mode=mode<3?ES2_HOOK_REPLACED:ES2_HOOK_COMPARE;
        if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("vector_angle real install");
        memcpy(pj_arena,pj_before,PJ_SIZE);pj_poison_call(mapped+caller,a,pattern,got);
        if(memcmp(want,got,12)||memcmp(pj_after,pj_arena,PJ_SIZE)||h.mismatches){
            fprintf(stderr,"real caller %x mode %u mismatches %llu\n",caller+0x400000u,mode,(unsigned long long)h.mismatches);
            pj_calls=0;status=pj_report("real",n,want,got,3,0,NULL);
        }
        entered+=h.calls;if(!es2_hook_uninstall(&h))return fail("vector_angle real uninstall");++total;
    }
    if(!status&&!entered){fprintf(stderr,"FAIL: vector_angle real callers never entered the hook\n");status=1;}
    if(!status)printf("PASS: %s %u real-caller cases (492934, 49295c, 492990; replaced/compare, %llu entries), EAX/EDX/ECX and arena\n",
        d->name,total,(unsigned long long)entered);
    return status;
}
/* Steering bodies over their real callees; virtual +24/+58 stay stubs. */
static int pj_steer_real(unsigned index){
    static const unsigned nested[]={ES2_HOOK_INDEX_aim_angles,ES2_HOOK_INDEX_vector_angle,ES2_HOOK_INDEX_euler_matrix,
        ES2_HOOK_INDEX_transform_point,ES2_HOOK_INDEX_approach_word,ES2_HOOK_INDEX_movement_step};
    enum { NESTS=6 };
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[NESTS]={0};
    pj_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],want[3],got[3],pattern=n&1?0xa7a7a7a7u:0x31313131u;pj_build_steer(index,n,a,r);
        memcpy(pj_before,pj_arena,PJ_SIZE);pj_reset();pj_poison_call(mapped+d->rva,a,pattern,want);
        unsigned calls=pj_calls;memcpy(pj_expected,pj_log,calls*sizeof(*pj_expected));memcpy(pj_after,pj_arena,PJ_SIZE);
        es2_hook hk[NESTS+1];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<NESTS;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("projectile nested install");}
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+NESTS,d,&o,function_hook_extension(index)))return fail("projectile caller install");
        memcpy(pj_arena,pj_before,PJ_SIZE);pj_reset();pj_poison_call(mapped+d->rva,a,pattern,got);
        if(memcmp(want,got,8)||calls!=pj_calls||memcmp(pj_expected,pj_log,calls*sizeof(*pj_expected))||memcmp(pj_after,pj_arena,PJ_SIZE)){
            fprintf(stderr,"real mode=%u\n",mode);status=pj_report("real",n,want,got,2,calls,pj_expected);
        }
        for(unsigned j=0;j<=NESTS;++j){if(j<NESTS)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("projectile nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("projectile nested uninstall");}
        ++total;
    }
    pj_real=0;
    /* vector_angle is entered only while aim_angles runs the original 492990. */
    for(unsigned j=0;j<NESTS&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, 31/a7 stack, pair/trace/arena\n",d->name,total,(unsigned)NESTS);
    return status;
}
static int projectile_cases(unsigned index,unsigned limit){
    const char *quick=getenv("ES2_INVITRO_PROJECTILE_CASES");
    unsigned cases=case_budget(limit,1001024);if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int steer=pj_steer(index),status=0;BYTE saved[PJ_IDS][5];int active[PJ_IDS]={0};
    if(steer&&pj_patch(saved,active,1))return 1;
    if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];
        if(steer){pj_build_steer(index,n,a,r);status=pj_steer_compare(index,n,a,r);}
        else {pj_build_helper(index,n,a,r);status=pj_helper_compare(index,n,a,r);}
    }
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=pj_installed_probes(index,64);
    if(steer&&pj_patch(saved,active,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=index==ES2_HOOK_INDEX_vector_angle?pj_vector_real(index):steer?pj_steer_real(index):0;
    if(!status)printf("PASS: %-20s %u cases; %s\n",function_hook_descriptor(index)->name,cases,
        steer?"return pair, ordered calls with entry registers (frame addresses relative), target point by content, arena":
        "EAX/EDX/ECX against the original, arena incl. overlapping out");
    return status;
}
