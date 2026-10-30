/* flyer_ai group: flyer_route 4224c4, flyer_altitude 422108, flyer_pitch 422098
 * and flyer_throttle 422260 (docs/reverse-engineering/flyer.md, "the FLYER AI
 * helpers `0x4224c4`, `0x422108`, `0x422098`, `0x422260`"). Direct cases run
 * the original body from an own caller (patterned stack; for flyer_route
 * distinct residue bytes planted at the shim's entry offsets 259h, 26Dh, 23Dh,
 * 251h and mirrored in the entry AL/AH/DL/DH) and the core against logging
 * stubs for the game callees of the hook under test (route_point 423b0c,
 * dist2d_point 4928d0, route_advance 42313c, 422108, heading_point 492934 and
 * flyer_steer 4222fc under 4224c4; multiply_q8 47e0a0, vector_angle 49290c and
 * 422098 under 422108; heading_cos 47dff0 under 422098 and 422260): ordered
 * calls with every argument slot and the entry EAX/EDX/ECX, the four residue
 * bytes the flyer_steer stub finds below its entry, the full return pair and
 * the arena. Outside the branch-trap window the original's flyer_steer stub
 * must see exactly the planted bytes (the depth 30h of the contract). The stubs
 * return values on the edges (route points 0 or not, distances
 * 14999/15000/15001, cosines 0 and +-4000h) and change what the originals read
 * again after a call ([A+45h], the route index, A+0Ch, A+10h, A+21Ch, A+233h,
 * L+233h, A+25Bh). 47e08d and 47e0c0 run as original code. Then 256 installed
 * 31/a7 cases per hook (fast and preserving dispatch, through flyer_route's
 * entry shim); a real-callee pass under the bridging hook flyer_formation
 * 422a50 (the leader path to 4224c4, the wingman path to flyer_follow 422598,
 * which calls 422108 and 422260) with the group's hooks, flyer_formation,
 * flyer_follow, route_point, dist2d_point, route_advance, heading_point,
 * vector_angle, multiply_q8, heading_cos, point_offset, dist_gate and
 * angle_ratio in rotating modes (flyer_steer and the actor virtual +78h stay
 * stubs); and the installed compare cases of compare_probes.h for
 * flyer_altitude, flyer_pitch and flyer_throttle on real callees. */
typedef struct fa_record { uint32_t id,args[5],regs[3],residue,eax,edx; } fa_record;
enum { FA_SIZE=0x1000, FA_STRIDE=0x300, FA_ACTORS=3, FA_GRP=0x900, FA_MEM=0xa00, FA_LIST=0xa40, FA_ARRAY=0xa60,
    FA_POINTS=8, FA_PT=0xb00, FA_VT=0xc00, FA_MAX=32, FA_IDS=11 };
#define FA_BASE ((uint32_t)(uintptr_t)fa_arena)
#define FA_ACTOR(i) (FA_BASE+FA_STRIDE*(uint32_t)(i))
#define FA_GROUP(j) (FA_BASE+FA_GRP+0x80u*(uint32_t)(j))
static BYTE fa_arena[FA_SIZE],fa_before[FA_SIZE],fa_after[FA_SIZE];
static fa_record fa_log[FA_MAX];
static unsigned fa_calls,fa_route_calls;
static uint32_t fa_seed,fa_stream,fa_actor,fa_leader,fa_plant_seed;
static int fa_real,fa_force_cos;
__attribute__((used)) static uint32_t fa_ecx,fa_tmp;
__attribute__((used)) static BYTE fa_res[4];
/* 0 route_point 423b0c, 1 dist2d_point 4928d0, 2 route_advance 42313c,
 * 3 422108, 4 heading_point 492934, 5 flyer_steer 4222fc (reads the
 * residue), 6 multiply_q8 47e0a0, 7 vector_angle 49290c, 8 422098,
 * 9 heading_cos 47dff0, 10 the actor virtual +78h (arena vtable, real
 * pass only, never patched). */
static const uint32_t fa_addresses[FA_IDS]={0x423b0c,0x4928d0,0x42313c,0x422108,0x492934,0x4222fc,0x47e0a0,0x49290c,
    0x422098,0x47dff0,0};
static const unsigned fa_counts[FA_IDS]={2,2,1,2,2,4,2,2,2,1,2};
static uint32_t fa_next(void){
    uint32_t z=fa_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *fa_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int fa_in(uint32_t a,uint32_t n){return a-FA_BASE<FA_SIZE&&FA_SIZE-(a-FA_BASE)>=n;}
/* Stack addresses (the frame P of flyer_follow in the real pass) are
 * masked; no other logged value is one. */
static uint32_t fa_mask(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u?0:value;
}
static uint32_t fa_small(uint32_t pick,uint32_t range){return (uint32_t)((int32_t)(pick%(2u*range+1u))-(int32_t)range);}
__attribute__((used,noinline)) static uint64_t __cdecl fa_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(fa_calls==FA_MAX){fail("flyer_ai call overflow");ExitProcess(1);}
    fa_record *v=fa_log+fa_calls++;memset(v,0,sizeof(*v));v->id=id;
    for(unsigned k=0;k<fa_counts[id];++k)v->args[k]=fa_mask(a[k]);
    for(unsigned j=0;j<3;++j)v->regs[j]=fa_mask(regs[j]);
    if(id==5&&!coverage_active)v->residue=fa_res[0]|(uint32_t)fa_res[1]<<8|(uint32_t)fa_res[2]<<16|(uint32_t)fa_res[3]<<24;
    uint32_t eax=fa_next(),edx=fa_next(),pick=fa_next();fa_ecx=fa_next();
    BYTE *o=fa_host(fa_actor);
    if(fa_real){
        /* The virtual +78h writes the formation point P near A (or far). */
        if(id==10)for(unsigned j=0;j<3;++j)put32(fa_host(a[1])+4*j,get32(o+0x26+4*j)+fa_small(fa_next(),pick%3?8000:40000));
        v->eax=fa_mask(eax);v->edx=edx;return (uint64_t)edx<<32|eax;
    }
    switch(id){
    case 0: /* route_point: the first call returns 0 a third of the time;
             * [A+45h] and the route index may change behind the caller. */
        eax=fa_route_calls++==0?(pick%3==0?0:eax|1u):eax;
        if(pick%7==1)put32(o+0x45,get32(o+0x45)==FA_GROUP(0)?FA_GROUP(1):FA_GROUP(0));
        if(pick%7==2){const uint32_t g=get32(o+0x45);if(fa_in(g+4,2))put16(fa_host(g+4),(uint16_t)fa_next());}
        break;
    case 1: { /* dist2d_point: the 15000 edge; [A+45h] is read again. */
        static const uint32_t ds[]={14999,15000,15001,0,0xffffffffu,0x7fffffffu,0x80000000u,0x3a98ffffu};
        if(pick%4)eax=ds[(pick>>4)%8];
        if(pick%5==0)put32(o+0x45,get32(o+0x45)==FA_GROUP(0)?FA_GROUP(1):FA_GROUP(0));
        break; }
    case 4: { /* heading_point: around A's heading; A+10h is read after it. */
        static const uint16_t deltas[]={0,1,0x7fff,0x8000,0xffff,0x4000,0xc000,0x8001};
        if(pick%4)eax=(eax&0xffff0000u)|(uint16_t)(get16(o+0x10)+deltas[(pick>>4)%8]);
        if(pick%5==0)put16(o+0x10,(uint16_t)fa_next());
        break; }
    case 5: /* flyer_steer: the original writes A+21Ch after it. */
        if(pick%3==0)put16(o+0x21c,(uint16_t)fa_next());
        break;
    case 6: /* multiply_q8: A+0Ch is read after it. */
        if(pick%3==0)put16(o+0xc,(uint16_t)fa_next());
        break;
    case 9: { /* heading_cos: quarter turns; what 422098/422260 read after
               * it (A+25Bh, A+21Ch, A+233h, L+233h) or before it may change. */
        static const uint16_t cs[]={0,0x4000,0xc000,0x3fff,0xc001,0x8000,0x7fff,1};
        if(pick%4)eax=(eax&0xffff0000u)|cs[(pick>>4)%8];
        if(fa_force_cos){eax&=0xffff0000u;break;}
        static const unsigned words[]={0x25b,0x21c,0x233,0xc,0xe,0x10};
        if(pick%3==0)put16(o+words[(pick>>8)%6],(uint16_t)fa_next());
        if(pick%5==0&&fa_in(fa_leader+0x233,2))put16(fa_host(fa_leader+(pick>>8&1?0x233:0x10)),(uint16_t)fa_next());
        break; }
    default:break;
    }
    v->eax=fa_mask(eax);v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define FA_BODY(n) "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _fa_stub_impl\n\taddl $24,%esp\n\tmovl _fa_ecx,%ecx\n\tret\n\t"
#define FA_STUB(n) __attribute__((naked)) static void fa_stub_##n(void){__asm__ volatile(FA_BODY(n));}
#define FA_RB(i,o) "movb -" #o "(%esp),%al\n\tmovb %al,_fa_res+" #i "\n\t"
FA_STUB(0) FA_STUB(1) FA_STUB(2) FA_STUB(3) FA_STUB(4) FA_STUB(6) FA_STUB(7) FA_STUB(8) FA_STUB(9) FA_STUB(10)
/* flyer_steer reads its four residue bytes before anything uses the stack. */
__attribute__((naked)) static void fa_stub_5(void){__asm__ volatile("movl %eax,_fa_tmp\n\t"
    FA_RB(0,0x229) FA_RB(1,0x23d) FA_RB(2,0x20d) FA_RB(3,0x221) "movl _fa_tmp,%eax\n\t" FA_BODY(5));}
#undef FA_STUB
#undef FA_RB
static void *fa_stubs[FA_IDS]={fa_stub_0,fa_stub_1,fa_stub_2,fa_stub_3,fa_stub_4,fa_stub_5,fa_stub_6,fa_stub_7,fa_stub_8,
    fa_stub_9,fa_stub_10};
/* Direct callees of each hook (bits = stub ids, patched). */
static unsigned fa_which(unsigned index){
    if(index==ES2_HOOK_INDEX_flyer_route)return 0x3fu;
    if(index==ES2_HOOK_INDEX_flyer_altitude)return 1u<<6|1u<<7|1u<<8;
    return 1u<<9;
}
static int fa_patch(unsigned mask,BYTE saved[FA_IDS][5],int active[FA_IDS],int on){
    for(unsigned i=0;i<FA_IDS;++i){
        if(on){active[i]=0;if(!(mask>>i&1)||!fa_addresses[i])continue;if(actor_patch(fa_addresses[i]-0x400000u,fa_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(fa_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
/* A planted byte (distinct per offset, never a 31/a7 pattern byte). */
static BYTE fa_byte(uint32_t offset){
    BYTE b=(BYTE)(fa_plant_seed>>(offset%3*8)^offset*0x9du^offset>>3);
    return b==0x31||b==0xa7?(BYTE)(b^0x5a):b;
}
/* Own caller: a patterned stack, five slots, planted residue bytes below
 * the callee's entry ESP, entry EAX/EDX/ECX; EAX/EDX come back in
 * registers[0..1]. */
typedef struct fa_plant { uint32_t offset,byte; } fa_plant;
__attribute__((naked)) static void fa_call(void *target,const uint32_t *a,uint32_t *registers,const fa_plant *plant,
    unsigned plants,uint32_t pattern){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 28(%ebp),%eax\n\tmovl $1024,%ecx\n\tcld\n\trep stosl\n\taddl $3840,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 16(%esi)\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 20(%ebp),%esi\n\tmovl 24(%ebp),%ecx\n\ttestl %ecx,%ecx\n\tjz 2f\n\t"
        "1: movl %esp,%eax\n\tsubl (%esi),%eax\n\tmovb 4(%esi),%bl\n\tmovb %bl,-4(%eax)\n\taddl $8,%esi\n\tdecl %ecx\n\tjnz 1b\n\t"
        "2: movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tcall *8(%ebp)\n\t"
        "movl 16(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* flyer_route's residue: the shim's offsets with the bytes of the entry
 * AL, AH, DL, DH (the direct replacement reads them there). */
static unsigned fa_plants(unsigned index,const uint32_t r[3],fa_plant p[4]){
    if(index!=ES2_HOOK_INDEX_flyer_route)return 0;
    static const uint32_t offsets[4]={0x259,0x26d,0x23d,0x251};
    const BYTE b[4]={(BYTE)r[0],(BYTE)(r[0]>>8),(BYTE)r[1],(BYTE)(r[1]>>8)};
    for(unsigned j=0;j<4;++j){p[j].offset=offsets[j];p[j].byte=b[j];}
    return 4;
}
static uint32_t fa_planted(const fa_plant *p){return p[0].byte|p[1].byte<<8|p[2].byte<<16|(uint32_t)p[3].byte<<24;}
static void fa_reset(void){fa_calls=0;fa_route_calls=0;fa_stream=fa_seed;}
static uint16_t fa_word(const uint16_t *values,unsigned count){return random32()%4?values[random32()%count]:(uint16_t)random32();}
static const uint16_t fa_edges[]={0,1,0x7fff,0x8000,0xffff,0xfffe,0x100,0xff00,0x4000,0xc000};
/* Three actors (stride 300h) with their angle, rate and throttle words,
 * two groups whose route records (+4 index word, +6 list pointer) point at
 * one list (+0 count word, +2 array) of point pointers, the vtable with
 * the +78h stub. */
static void fa_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    fa_seed=random32();fa_plant_seed=random32();random_bytes(fa_arena,FA_SIZE);memset(a,0,24);fa_force_cos=0;
    for(unsigned i=0;i<3;++i)r[i]=random32();
    put32(fa_arena+FA_VT+0x78,(uint32_t)(uintptr_t)fa_stubs[10]);
    for(unsigned j=0;j<2;++j){BYTE *g=fa_host(FA_GROUP(j));put16(g+4,fa_word(fa_edges,10));put32(g+6,FA_BASE+FA_LIST);}
    for(unsigned i=0;i<FA_ACTORS;++i){BYTE *p=fa_host(FA_ACTOR(i));
        put32(p,FA_BASE+FA_VT);put32(p+0x45,FA_GROUP(random32()%2));
        static const unsigned words[]={0xc,0xe,0x10,0x21c,0x233,0x23c,0x25b};
        for(unsigned k=0;k<7;++k)put16(p+words[k],fa_word(fa_edges,10));
        static const BYTE flags[]={0,0,1,0x80,0xff};p[0xae]=flags[random32()%5];}
    const uint32_t A=FA_ACTOR(random32()%2);fa_actor=A;fa_leader=0;a[0]=A;
    if(index==ES2_HOOK_INDEX_flyer_route){
        /* AL, AH, DL, DH carry the residue bytes the caller plants. */
        r[0]=(r[0]&0xffff0000u)|fa_byte(0x259)|(uint32_t)fa_byte(0x26d)<<8;
        r[1]=(r[1]&0xffff0000u)|fa_byte(0x23d)|(uint32_t)fa_byte(0x251)<<8;
        if(n<16)put16(fa_host(get32(fa_host(A)+0x45)+4),fa_edges[n%10]);
    } else if(index==ES2_HOOK_INDEX_flyer_altitude){
        static const uint32_t heights[]={0,30000,0x80000000u,0x7fffffffu,0xffffffffu,10000};
        a[1]=random32()%3?heights[random32()%6]:random32();
        if(random32()%4==0)put32(fa_host(A)+0x2e,random32()%3?a[1]-fa_small(random32(),2):random32());
    } else if(index==ES2_HOOK_INDEX_flyer_pitch){
        a[1]=(random32()&0xffff0000u)|fa_word(fa_edges,10);
    } else {
        const uint32_t L=random32()%8?FA_ACTOR(2):A;fa_leader=L;
        a[1]=L;a[2]=(random32()&0xffff0000u)|fa_word(fa_edges,10);a[3]=(random32()&0xffff0000u)|fa_word(fa_edges,10);
        a[4]=random32()%2?0x2000:(random32()&0xffff0000u)|fa_word(fa_edges,10);
        /* Clamp edges: cosine 0, no spacing, equal speed words, so that
         * A+21Ch alone decides (100h, 8Ch and their neighbours). */
        if(n<64||random32()%16==0){
            static const uint16_t sums[]={0xff,0x100,0x101,0x8b,0x8c,0x8d,0x7fff,0x8000,0xffff,0};
            fa_force_cos=1;a[4]&=0xffff0000u;put16(fa_host(L)+0x233,get16(fa_host(A)+0x233));
            put16(fa_host(A)+0x21c,sums[n<64?n%10:random32()%10]);
        }
    }
}
static int fa_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const fa_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,fa_calls);
    for(unsigned j=0;j<calls&&j<fa_calls&&expected;++j)if(memcmp(expected+j,fa_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(fa_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<FA_SIZE;++j)if(fa_after[j]!=fa_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,fa_after[j],fa_arena[j]);break;}
    return 1;
}
/* The original's flyer_steer stub must find the planted bytes (outside
 * the trap window, whose exception dispatch writes below ESP). */
static int fa_check_residue(unsigned n,const fa_plant *p,unsigned plants){
    if(coverage_active||!plants)return 0;
    for(unsigned j=0;j<fa_calls;++j)if(fa_log[j].id==5&&fa_log[j].residue!=fa_planted(p)){
        fprintf(stderr,"FAIL: %s case %u residue at flyer_steer %08x, planted %08x\n",current_function,n,fa_log[j].residue,fa_planted(p));
        return 1;
    }
    return 0;
}
static unsigned fa_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<fa_calls;++j)if(fa_addresses[fa_log[j].id]==c->key)++hits;
    return hits;
}
static int fa_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    static fa_record expected[FA_MAX];fa_plant p[4];const unsigned plants=fa_plants(index,r,p);
    memcpy(fa_before,fa_arena,FA_SIZE);
    uint32_t regs[3]={r[0],r[1],r[2]};
    fa_reset();fa_call(mapped+d->rva,a,regs,p,plants,random32());
    uint64_t pair[2];pair[0]=(uint64_t)regs[1]<<32|regs[0];unsigned calls=fa_calls;memcpy(expected,fa_log,calls*sizeof(*expected));
    if(fa_check_residue(n,p,plants))return 1;
    memcpy(fa_after,fa_arena,FA_SIZE);memcpy(fa_arena,fa_before,FA_SIZE);fa_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];for(unsigned i=0;i<count&&i<8;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);
    if(n<4096)for(unsigned i=0;i<count&&i<8;++i)if(c[i].count-counts[i]!=fa_expected_hits(c+i))return fail("flyer_ai callee counter delta");
    if(pair[0]!=pair[1]||calls!=fa_calls||memcmp(expected,fa_log,calls*sizeof(*expected))||memcmp(fa_after,fa_arena,FA_SIZE))
        return fa_report("direct",n,pair,calls,expected);
    return 0;
}
/* Installed through the entry shim (flyer_route) or the plain dispatch:
 * the own caller fills the stack with 31/a7 and plants the residue. */
static int fa_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3];fa_build(index,n,a,r);const uint32_t pattern=poison?0xa7a7a7a7u:0x31313131u;
        fa_plant p[4];const unsigned plants=fa_plants(index,r,p);
        BYTE expected_fpu[108],actual_fpu[108];static fa_record expected[FA_MAX];
        memcpy(fa_before,fa_arena,FA_SIZE);uint32_t er[3]={r[0],r[1],r[2]},ar[3]={r[0],r[1],r[2]};
        fa_reset();fa_call(mapped+d->rva,a,er,p,plants,pattern);es2_hook_fpu_save(expected_fpu);
        unsigned calls=fa_calls;memcpy(expected,fa_log,calls*sizeof(*expected));memcpy(fa_after,fa_arena,FA_SIZE);
        if(fa_check_residue(n,p,plants))return 1;
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("flyer_ai poison install");
        memcpy(fa_arena,fa_before,FA_SIZE);fa_reset();fa_call(mapped+d->rva,a,ar,p,plants,pattern);es2_hook_fpu_save(actual_fpu);
        uint64_t pair[2]={(uint64_t)er[1]<<32|er[0],(uint64_t)ar[1]<<32|ar[0]};
        if(pair[0]!=pair[1]||calls!=fa_calls||memcmp(expected,fa_log,calls*sizeof(*expected))||memcmp(fa_after,fa_arena,FA_SIZE)||
           memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=fa_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("flyer_ai poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases%s, fast/preserving dispatch, nonempty x87, full pair/trace/arena\n",
        d->name,total,index==ES2_HOOK_INDEX_flyer_route?" through the entry shim with planted residue":"");
    return status;
}
/* Real callees under the bridging hooks: flyer_formation 422a50 is called
 * from the own caller with the eight bytes of its shim offsets planted
 * (flyer_states_capture); A is the group's leader (4224c4: route_point,
 * dist2d_point, route_advance, 422108, heading_point over a valid route)
 * or a wingman (flyer_follow: point_offset, the stubbed virtual +78h,
 * dist_gate, heading_point, 422108, then 422260 when the point is within
 * 15000). Run 1 has the nested hooks in rotating modes (not installed,
 * original, replaced, compare where accepted), run 2 adds the hook under
 * test (original, replaced, compare where accepted). Pair, arena and
 * flyer_steer's log (ECX at the stubs not compared: a replaced nested hook
 * returns its entry ECX, flyer.md) must match. flyer_steer's residue must
 * be the planted bytes of its path whenever its caller (4224c4 or 422598)
 * runs as a replaced hook, which plants them after its callees, or no hook
 * of the tree runs replaced; otherwise the nested dispatches may write
 * there (raw-residue parity across call trees is not claimed). */
static void fa_real_build(unsigned n,uint32_t a[6],uint32_t r[3]){
    fa_seed=random32();fa_plant_seed=random32();random_bytes(fa_arena,FA_SIZE);memset(a,0,24);fa_force_cos=0;
    for(unsigned i=0;i<3;++i)r[i]=random32();
    put32(fa_arena+FA_VT+0x78,(uint32_t)(uintptr_t)fa_stubs[10]);
    const uint32_t G=FA_GROUP(0);BYTE *g=fa_host(G);const unsigned count=1+random32()%FA_POINTS;
    put16(g+4,(uint16_t)(random32()%count));put32(g+6,FA_BASE+FA_LIST);put32(g+0xc,FA_BASE+FA_MEM);put16(g+0x10,FA_ACTORS);
    put16(fa_arena+FA_LIST,(uint16_t)count);put32(fa_arena+FA_LIST+2,FA_BASE+FA_ARRAY);
    for(unsigned k=0;k<FA_POINTS;++k){put32(fa_arena+FA_ARRAY+4*k,FA_BASE+FA_PT+12u*(random32()%FA_POINTS));
        for(unsigned j=0;j<3;++j)put32(fa_arena+FA_PT+12*k+4*j,fa_small(random32(),20000));}
    for(unsigned i=0;i<FA_ACTORS;++i){BYTE *p=fa_host(FA_ACTOR(i));
        put32(fa_arena+FA_MEM+4*i,FA_ACTOR(i));put32(p,FA_BASE+FA_VT);put32(p+0x45,G);
        for(unsigned j=0;j<3;++j)put32(p+0x26+4*j,fa_small(random32(),20000));
        put16(p+0xc,(uint16_t)fa_small(random32(),0x2000));put16(p+0xe,(uint16_t)fa_small(random32(),0x2000));put16(p+0x10,(uint16_t)random32());
        p[0xae]=(BYTE)(random32()%2?0:1+random32()%255);
        put16(p+0x23c,(uint16_t)fa_small(random32(),0x200));put16(p+0x25b,(uint16_t)fa_small(random32(),0x200));
        put16(p+0x233,(uint16_t)(random32()%0x200u));put16(p+0x21c,(uint16_t)(random32()%0x200u));}
    const unsigned who=n&1?1+random32()%2:0;const uint32_t A=FA_ACTOR(who);a[0]=A;fa_actor=A;fa_leader=FA_ACTOR(0);
    /* The leader near its next waypoint half of the time (route_advance). */
    if(!who&&random32()%2){
        const unsigned i=get16(g+4)+1u<count?get16(g+4)+1u:get16(g+4);const uint32_t pt=get32(fa_arena+FA_ARRAY+4*i);
        for(unsigned j=0;j<2;++j)put32(fa_host(A)+0x26+4*j,get32(fa_host(pt)+4*j)+fa_small(random32(),12000));
    }
}
/* The bytes of flyer_formation's shim offsets: 4224c4's path at entry
 * -269h, -27Dh, -24Dh, -261h, 422598's at -271h, -285h, -255h, -269h. */
static unsigned fa_formation_plants(fa_plant p[8]){
    static const uint32_t offsets[8]={0x269,0x27d,0x24d,0x261,0x271,0x285,0x255,0x269};
    for(unsigned j=0;j<8;++j){p[j].offset=offsets[j];p[j].byte=fa_byte(offsets[j]);}
    return 8;
}
static uint32_t fa_path_residue(int leader){
    const uint32_t *o=leader?(const uint32_t[]){0x269,0x27d,0x24d,0x261}:(const uint32_t[]){0x271,0x285,0x255,0x269};
    return fa_byte(o[0])|fa_byte(o[1])<<8|fa_byte(o[2])<<16|(uint32_t)fa_byte(o[3])<<24;
}
static const unsigned fa_tree[]={ES2_HOOK_INDEX_flyer_formation,ES2_HOOK_INDEX_flyer_follow,ES2_HOOK_INDEX_flyer_route,
    ES2_HOOK_INDEX_flyer_altitude,ES2_HOOK_INDEX_flyer_pitch,ES2_HOOK_INDEX_flyer_throttle,ES2_HOOK_INDEX_route_point,
    ES2_HOOK_INDEX_dist2d_point,ES2_HOOK_INDEX_route_advance,ES2_HOOK_INDEX_heading_point,ES2_HOOK_INDEX_vector_angle,
    ES2_HOOK_INDEX_multiply_q8,ES2_HOOK_INDEX_heading_cos,ES2_HOOK_INDEX_point_offset,ES2_HOOK_INDEX_dist_gate,
    ES2_HOOK_INDEX_angle_ratio};
enum { FA_TREE=sizeof(fa_tree)/sizeof(fa_tree[0]) };
/* The residue rule of one run: 1 must equal the planted bytes, 0 unchecked. */
static int fa_residue_claimed(const es2_hook *hk,const unsigned *index,unsigned count,int leader){
    const unsigned caller=leader?ES2_HOOK_INDEX_flyer_route:ES2_HOOK_INDEX_flyer_follow;int any=0;
    for(unsigned j=0;j<count;++j)if(hk[j].descriptor&&hk[j].options.mode!=ES2_HOOK_ORIGINAL){
        if(index[j]==caller)return 1;
        any=1;
    }
    return !any;
}
static int fa_real_pass(unsigned index){
    unsigned nested[FA_TREE],count=0;
    for(unsigned i=0;i<FA_TREE;++i)if(fa_tree[i]!=index)nested[count++]=fa_tree[i];
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0,carried=0,checked=0;
    uint64_t entered[FA_TREE+1]={0};const unsigned modes=compare_probe_capable(index)?3u:2u;
    BYTE saved[5];if(actor_patch(0x222fcu,fa_stubs[5],saved,TRUE))return 1;
    fa_real=1;
    for(unsigned mode=0;mode<modes&&!status;++mode)for(unsigned n=0;n<1024&&!status;++n){
        uint32_t a[6],r[3];fa_real_build(n,a,r);const uint32_t pattern=n&2?0xa7a7a7a7u:0x31313131u;const int leader=a[0]==FA_ACTOR(0);
        fa_plant p[8];const unsigned plants=fa_formation_plants(p);static fa_record expected[FA_MAX];
        memcpy(fa_before,fa_arena,FA_SIZE);
        es2_hook hk[FA_TREE+1];unsigned hooked[FA_TREE+1];memset(hk,0,sizeof(hk));
        es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        unsigned digit=n/2+mode*7;
        for(unsigned j=0;j<count;++j){
            hooked[j]=nested[j];
            const unsigned m=(digit+j)%4;digit/=4;if(!digit)digit=n*3+j+mode;
            if(!m)continue;
            const es2_hook_extension *e=function_hook_extension(nested[j]);
            o.mode=m==1?ES2_HOOK_ORIGINAL:m==2||(e&&e->compare_unsupported)?ES2_HOOK_REPLACED:ES2_HOOK_COMPARE;
            if(!nested_install_hook(hk+j,nested[j],&o,e))return fail("flyer_ai nested install");
        }
        uint32_t er[3]={r[0],r[1],r[2]},ar[3]={r[0],r[1],r[2]};
        fa_reset();fa_call(mapped+0x22a50u,a,er,p,plants,pattern);
        unsigned calls=fa_calls;memcpy(expected,fa_log,calls*sizeof(*expected));memcpy(fa_after,fa_arena,FA_SIZE);
        const uint32_t want=fa_path_residue(leader);
        if(fa_residue_claimed(hk,hooked,count,leader)){
            for(unsigned j=0;j<calls;++j)if(expected[j].id==5&&expected[j].residue!=want){
                fprintf(stderr,"FAIL: %s case %u (real mode=%u, run 1) residue %08x, planted %08x\n",d->name,n,mode,expected[j].residue,want);status=1;}
            ++checked;
        }
        o.mode=mode==0?ES2_HOOK_ORIGINAL:mode==1?ES2_HOOK_REPLACED:ES2_HOOK_COMPARE;hooked[count]=index;
        if(!weapons_install_hook(hk+count,d,&o,function_hook_extension(index)))return fail("flyer_ai hook install");
        memcpy(fa_arena,fa_before,FA_SIZE);fa_reset();fa_call(mapped+0x22a50u,a,ar,p,plants,pattern);
        if(fa_residue_claimed(hk,hooked,count+1,leader)){
            for(unsigned j=0;j<fa_calls;++j)if(fa_log[j].id==5){
                if(fa_log[j].residue!=want){
                    fprintf(stderr,"FAIL: %s case %u (real mode=%u, run 2) residue %08x, planted %08x\n",d->name,n,mode,fa_log[j].residue,want);status=1;}
                else if(o.mode!=ES2_HOOK_ORIGINAL&&index==(leader?ES2_HOOK_INDEX_flyer_route:ES2_HOOK_INDEX_flyer_follow))++carried;
            }
            ++checked;
        }
        for(unsigned j=0;j<calls;++j){expected[j].regs[2]=0;expected[j].residue=0;}
        for(unsigned j=0;j<fa_calls;++j){fa_log[j].regs[2]=0;fa_log[j].residue=0;}
        uint64_t pair[2]={(uint64_t)er[1]<<32|er[0],(uint64_t)ar[1]<<32|ar[0]};
        if(!status&&(pair[0]!=pair[1]||calls!=fa_calls||memcmp(expected,fa_log,calls*sizeof(*expected))||memcmp(fa_after,fa_arena,FA_SIZE))){
            fprintf(stderr,"real mode=%u leader=%d modes:",mode,leader);
            for(unsigned j=0;j<=count;++j)fprintf(stderr," %s=%d",function_hook_descriptor(hooked[j])->name,hk[j].descriptor?(int)hk[j].options.mode:-1);
            fprintf(stderr,"\n");status=fa_report("real",n,pair,calls,expected);
        }
        for(unsigned j=0;j<=count;++j){entered[j]+=hk[j].calls;
            if(hk[j].mismatches){fprintf(stderr,"FAIL: %s case %u (real mode=%u) compare mismatch in %s\n",d->name,n,mode,function_hook_descriptor(hooked[j])->name);status=1;}
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("flyer_ai nested uninstall");}
        ++total;
    }
    fa_real=0;
    if(actor_patch(0x222fcu,NULL,saved,FALSE))return 1;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&!entered[count]){fprintf(stderr,"FAIL: %s never entered under flyer_formation\n",d->name);status=1;}
    if(!status&&index==ES2_HOOK_INDEX_flyer_route&&!carried)status=fail("flyer_ai real pass: the replaced flyer_route never carried the residue");
    if(!status){printf("PASS: %s %u real-callee cases under flyer_formation (%llu entries, %u residue checks, %u carried by the hook), "
        "%u nested hooks in rotating modes, pair, stub log, arena\n",d->name,total,(unsigned long long)entered[count],checked,carried,count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
/* Compare cases (compare_probes.h): flyer_altitude, flyer_pitch and
 * flyer_throttle on the real arena with real callees (multiply_q8,
 * vector_angle, 422098, heading_cos). */
static void fa_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    fa_real_build(n,a,r);
    if(index==ES2_HOOK_INDEX_flyer_altitude){static const uint32_t h[]={30000,0,10000,0x7fffffffu};a[1]=n%2?h[n/2%4]:random32();}
    else if(index==ES2_HOOK_INDEX_flyer_pitch)a[1]=random32();
    else if(index==ES2_HOOK_INDEX_flyer_throttle){a[0]=FA_ACTOR(1);a[1]=FA_ACTOR(n%2?0:2);a[2]=random32()%0x10000u;
        a[3]=random32()%0x10000u;a[4]=n%2?0x2000:random32()%0x10000u;}
}
static void fa_compare_state(BYTE *s,int restore){if(restore)memcpy(fa_arena,s,FA_SIZE);else memcpy(s,fa_arena,FA_SIZE);}
static int flyer_ai_cases(unsigned index,unsigned limit){
    if(limit&&compare_probe_installed(index))return compare_probe_run(index,limit,fa_compare_build,fa_compare_state,FA_SIZE);
    BYTE saved[FA_IDS][5];int active[FA_IDS];
    if(fa_patch(fa_which(index),saved,active,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_FLYER_AI_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];fa_build(index,n,a,r);status=fa_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=fa_installed_probes(index,64);
    if(fa_patch(0,saved,active,0))return 1;
    current_case_valid=0;fa_force_cos=0;
    if(!limit&&!status)status=fa_real_pass(index);
    if(!limit&&!status&&compare_probe_capable(index))status=compare_probe_run(index,0,fa_compare_build,fa_compare_state,FA_SIZE);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, residue, arena\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
