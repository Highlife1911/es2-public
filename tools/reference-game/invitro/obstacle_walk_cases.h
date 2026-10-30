/* obstacle_walk group: collision_walk 427c68, shape_cell 427360 and
 * convoy_leader 46a5bc (docs/reverse-engineering/mission_runtime.md, "collision
 * walk 427c68 and shape cell 427360", and convoy.md, "46a5bc
 * convoy_leader(B)"). Direct cases run the original body and the core
 * against logging stubs for the game callees of the hook under test
 * (dist2d_point 4928d0, euler_matrix 47ebb8, the swap 47df04, rotate_2d 47ffe8
 * and 427360 under 427c68; cell 427238 under 427360): ordered calls with their
 * arguments and entry EAX/EDX/ECX (stack addresses masked, frame pointers also
 * as offsets from D or Q), the frame payloads D, M, R and Q by content, the
 * full return pair and the arena. The stubs change what the original reads
 * again after a call (the shape radius word, the matrix flag and words, P,
 * later list entries). convoy_leader has no callee: original against core over
 * random groups whose counted members reach an alive one (or four dead ones).
 * Then installed 31/a7 poison probes; a real-callee pass with the nested hooks
 * in rotating modes (collision_walk and shape_cell under obstacle_test over a
 * real pool with pool_prev/pool_next, dist2d_point, euler_matrix, rotate_2d and
 * cell nested; convoy_leader under convoy_drive and convoy_follow with the
 * convoy hooks nested), reusing the obstacle and convoy builders; and the
 * installed compare cases of compare_probes.h on real callees. */
typedef struct ow_record { uint32_t id,args[3],regs[3],input[12],eax,edx; } ow_record;
enum { OW_SIZE=0x1400, OW_LIST=0, OW_LISTED=16, OW_POINT=0x40, OW_MGR=0x60, OW_SHAPE=0x100, OW_SHAPE2=0x600, OW_ROWS=0xa80,
    OW_ROWDATA=0xb00, OW_ENTRY=0xc00, OW_ESTRIDE=0x40, OW_ENTRIES=12, OW_GROUP=0xf00, OW_MEMBERS=0xf40, OW_MEMBER=0xf80,
    OW_MSTRIDE=0xb0, OW_OBJECT=0x1280, OW_R=0x1300, OW_MAX=96, OW_IDS=6 };
#define OW_BASE ((uint32_t)(uintptr_t)ow_arena)
static BYTE ow_arena[OW_SIZE],ow_before[OW_SIZE],ow_after[OW_SIZE];
static ow_record ow_log[OW_MAX];
static unsigned ow_calls;
static uint32_t ow_seed,ow_stream;
__attribute__((used)) static uint32_t ow_ecx;
/* 0 dist2d_point 4928d0, 1 euler_matrix 47ebb8, 2 swap 47df04, 3 rotate_2d
 * 47ffe8, 4 shape_cell 427360 (under 427c68), 5 cell 427238 (under 427360). */
static const uint32_t ow_addresses[OW_IDS]={0x4928d0,0x47ebb8,0x47df04,0x47ffe8,0x427360,0x427238};
static const unsigned ow_counts[OW_IDS]={2,2,1,3,3,3};
static uint32_t ow_next(void){
    uint32_t z=ow_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *ow_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
/* Same fixed stack window as node_cases.h: frame addresses are masked. */
static int ow_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static int ow_in(uint32_t p,uint32_t n){return p-OW_BASE<OW_SIZE&&OW_SIZE-(p-OW_BASE)>=n;}
static uint32_t ow_small(uint32_t pick,uint32_t range){return (uint32_t)((int32_t)(pick%(2u*range+1u))-(int32_t)range);}
__attribute__((used,noinline)) static uint64_t __cdecl ow_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(ow_calls==OW_MAX){fail("obstacle_walk synthetic call overflow");ExitProcess(1);}
    ow_record *v=ow_log+ow_calls++;memset(v,0,sizeof(*v));v->id=id;
    for(unsigned k=0;k<ow_counts[id];++k)v->args[k]=ow_stack(a[k])?0:a[k];
    for(unsigned j=0;j<3;++j)v->regs[j]=ow_stack(regs[j])?0:regs[j];
    uint32_t eax=ow_next(),edx=ow_next(),pick=ow_next();ow_ecx=ow_next();
    switch(id){
    case 0: { /* dist2d_point: around E's shape radius (E = a[1] - 26h);
               * the radius word is read after the call, the flag word,
               * P and a later list entry may change. */
        const uint32_t e=a[1]-0x26u;
        if(ow_in(e+0x34u,4)){const uint32_t g=get32(ow_host(e+0x34u));
            if(ow_in(g+4u,4)){const uint32_t s=get32(ow_host(g+4u));
                if(ow_in(s+8u,2)){const uint32_t radius=(uint32_t)(int32_t)(int16_t)get16(ow_host(s+8u));
                    if(pick%4==0)eax=radius+ow_small(pick>>4,1);
                    else if(pick%4==1){static const uint32_t edges[]={0,1,0x7fffffffu,0x80000000u,0xffffffffu,0x8000u,0xffff8000u};eax=edges[(pick>>4)%7];}
                    if(pick%17==0)put16(ow_host(s+8u),(uint16_t)ow_next());}}}
        if(pick%13==0&&ow_in(e+0x32u,2))put16(ow_host(e+0x32u),(uint16_t)(pick>>8&1));
        if(pick%19==0)put32(ow_arena+OW_POINT+4*(pick>>8&1),ow_next());
        break; }
    case 1: /* euler_matrix: rewrites the four matrix words read after it. */
        if(pick%3)for(unsigned j=0;j<4;++j)put16(ow_host(a[1])+2*j,(uint16_t)ow_next());
        break;
    case 2: /* 47df04: M by content (ECX as an offset from M), rewritten;
             * EAX = M as in the original. */
        for(unsigned j=0;j<4;++j)v->input[j]=get16(ow_host(a[0])+2*j);
        v->input[4]=regs[2]-a[0];
        if(pick%2)for(unsigned j=0;j<4;++j)put16(ow_host(a[0])+2*j,(uint16_t)ow_next());
        eax=a[0];
        break;
    case 3: /* rotate_2d: D and M by content, M, R and the registers as
             * offsets from D; R written; P or a later entry may change. */
        for(unsigned j=0;j<2;++j)v->input[j]=get32(ow_host(a[0])+4*j);
        for(unsigned j=0;j<4;++j)v->input[2+j]=get16(ow_host(a[1])+2*j);
        v->input[6]=a[1]-a[0];v->input[7]=a[2]-a[0];
        for(unsigned j=0;j<3;++j)v->input[8+j]=regs[j]-a[0];
        put32(ow_host(a[2]),ow_next());put32(ow_host(a[2])+4,ow_next());
        if(pick%11==0)put32(ow_arena+OW_POINT+4*(pick>>8&1),ow_next());
        if(pick%7==0)put32(ow_arena+OW_LIST+4*((pick>>8)%OW_LISTED),OW_BASE+OW_ENTRY+OW_ESTRIDE*((pick>>16)%OW_ENTRIES));
        break;
    case 4: /* 427360: R by content; zero two thirds of the time, a high
             * word only now and then (the walk tests the whole EAX). */
        for(unsigned j=0;j<2;++j)v->input[j]=get32(ow_host(a[1])+4*j);
        v->input[2]=a[1]-regs[0];
        eax=pick%3?0:pick>>8&1?eax&0xffff0000u:eax;
        break;
    case 5: /* cell: Q and the two shifted origin words above it by
             * content, ECX as an offset from Q. */
        for(unsigned j=0;j<2;++j)v->input[j]=get32(ow_host(a[1])+4*j);
        v->input[2]=get16(ow_host(a[1])+8);v->input[3]=get16(ow_host(a[1])+10);
        v->input[4]=regs[2]-a[1];
        break;
    }
    v->eax=ow_stack(eax)?0:eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define OW_STUB(n) __attribute__((naked)) static void ow_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _ow_stub_impl\n\taddl $24,%esp\n\tmovl _ow_ecx,%ecx\n\tret\n\t");}
OW_STUB(0) OW_STUB(1) OW_STUB(2) OW_STUB(3) OW_STUB(4) OW_STUB(5)
#undef OW_STUB
static void *ow_stubs[OW_IDS]={ow_stub_0,ow_stub_1,ow_stub_2,ow_stub_3,ow_stub_4,ow_stub_5};
static unsigned ow_which(unsigned index){
    /* The swap 47df04 (id 2) runs as real code; collision_walk computes it in
     * core and only counts it. */
    if(index==ES2_HOOK_INDEX_collision_walk)return 0x1bu;
    if(index==ES2_HOOK_INDEX_shape_cell)return 1u<<5;
    return 0;
}
static void ow_reset(void){ow_calls=0;ow_stream=ow_seed;}
static uint16_t ow_word(const uint16_t *values,unsigned count){return values[random32()%count];}
/* Two shapes: radius word +8, grid 4 x 4 (+2Ah/+2Ch), origin words
 * +2Eh/+30h, shift +32h, a 256-entry height map +34h and the row array
 * +434h. Real: a valid grid that about half of the rotated points hit;
 * direct: any words, including a zero +2Ah and shifts of 16 and more. */
static void ow_shapes(int real){
    static const uint16_t shifts[]={0,1,15,16,17,31,32,33,0x24,0xffff};
    for(unsigned s=0;s<2;++s){BYTE *x=ow_arena+(s?OW_SHAPE2:OW_SHAPE);
        put16(x+8,(uint16_t)(real||random32()%2?0x800+random32()%0x2000:random32()));
        put16(x+0x2a,(uint16_t)(real?(random32()%5?4:0):random32()%3?(uint16_t)random32():0));put16(x+0x2c,4);
        put16(x+0x2e,(uint16_t)(real?2:random32()));put16(x+0x30,(uint16_t)(real?2:random32()));
        put16(x+0x32,(uint16_t)(real?10+random32()%3:random32()%2?ow_word(shifts,10):random32()));
        for(unsigned j=0;j<256;++j)put32(x+0x34+4*j,random32()%2?0:random32());
        put32(x+0x434,OW_BASE+OW_ROWS+0x20*s);
        for(unsigned j=0;j<4;++j){put32(ow_arena+OW_ROWS+0x20*s+4*j,OW_BASE+OW_ROWDATA+0x40*s+8*j);
            for(unsigned k=0;k<8;++k)ow_arena[OW_ROWDATA+0x40*s+8*j+k]=(BYTE)random32();}
        put32(ow_arena+OW_MGR+0x10*s+4,OW_BASE+(s?OW_SHAPE2:OW_SHAPE));}
}
/* The convoy group: object B, group G (list +0Ch, signed count +10h), four
 * distinct members first in the list (then any). The members before the
 * chosen one are dead by +0A4h, by +99h or by both; the chosen one alive;
 * with a count of four or more sometimes all four dead. A member index at
 * or above the count would be a null member (the original faults), so
 * the count always reaches the chosen member. */
static void ow_group(unsigned n){
    uint32_t b=OW_BASE;unsigned order[4]={0,1,2,3};
    for(unsigned i=3;i>0;--i){unsigned j=random32()%(i+1),t=order[i];order[i]=order[j];order[j]=t;}
    put32(ow_arena+OW_OBJECT+0x45,b+OW_GROUP);put32(ow_arena+OW_GROUP+0xc,b+OW_MEMBERS);
    for(unsigned i=0;i<8;++i)put32(ow_arena+OW_MEMBERS+4*i,b+OW_MEMBER+OW_MSTRIDE*(i<4?order[i]:random32()%4));
    unsigned count=n<16?1+n%4:random32()%4?1+random32()%4:5+random32()%0x7ffb,limit=count<4?count:4;
    put16(ow_arena+OW_GROUP+0x10,(uint16_t)count);
    unsigned chosen=count>=4&&(n%16==15||random32()%4==0)?4:random32()%limit;
    if(n<16)chosen=n<8?(n>=4?4u:n%limit):chosen;
    for(unsigned i=0;i<4;++i){BYTE *m=ow_host(get32(ow_arena+OW_MEMBERS+4*i));
        if(i<chosen){unsigned how=random32()%3;m[0xa4]=(BYTE)(how==1?0:1+random32()%255);m[0x99]=(BYTE)(how==0?random32()%2*(1+random32()%255):1+random32()%255);}
        else if(i==chosen){m[0xa4]=0;m[0x99]=0;}}
    if(chosen==4&&count<4)put16(ow_arena+OW_GROUP+0x10,4);
}
static void ow_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    uint32_t b=OW_BASE;
    ow_seed=random32();random_bytes(ow_arena,OW_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    ow_shapes(real);
    for(unsigned i=0;i<OW_ENTRIES;++i){BYTE *e=ow_arena+OW_ENTRY+OW_ESTRIDE*i;
        put32(e+0x34,b+OW_MGR+0x10u*(random32()%2));
        if(real)for(unsigned j=0;j<3;++j)put32(e+0x26+4*j,ow_small(random32(),random32()%2?0x1800:0x6000));
        put16(e+0x32,(uint16_t)(random32()%2?0:random32()%2?1:random32()));}
    for(unsigned i=0;i<OW_LISTED;++i)put32(ow_arena+OW_LIST+4*i,b+OW_ENTRY+OW_ESTRIDE*(random32()%OW_ENTRIES));
    if(real)for(unsigned j=0;j<3;++j)put32(ow_arena+OW_POINT+4*j,ow_small(random32(),0x400));
    ow_group(n);
    if(index==ES2_HOOK_INDEX_collision_walk){
        static const uint32_t counts[]={0,0x8000,0xffff,1,16,0x10000,0x7fff0002u,0x8001};
        uint32_t c=random32()%5==0?0x8000u|random32():random32()%(OW_LISTED+1);
        if(n<16)c=counts[n%8];
        else if(real)c=random32()%(OW_LISTED+1);
        a[0]=b+OW_POINT;a[1]=random32();a[2]=b+OW_LIST;a[3]=(random32()&0xffff0000u)|(c&0xffffu);
        if(n<16&&n>=8)a[3]=c;
    } else if(index==ES2_HOOK_INDEX_shape_cell){
        /* Real: a radius word below the grid threshold (cell's single-cell path). */
        a[0]=b+(random32()%2?OW_SHAPE2:OW_SHAPE);a[1]=b+OW_R;a[2]=real?(random32()&0xffff0000u)|random32()%0x200u:random32();
        for(unsigned j=0;j<2;++j)put32(ow_arena+OW_R+4*j,real?ow_small(random32(),0x1000):random32());
    } else a[0]=b+OW_OBJECT;
}
static int ow_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const ow_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,ow_calls);
    for(unsigned j=0;j<calls&&j<ow_calls&&expected;++j)if(memcmp(expected+j,ow_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(ow_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<OW_SIZE;++j)if(ow_after[j]!=ow_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,ow_after[j],ow_arena[j]);break;}
    return 1;
}
/* call_47df04 (not stubbed) counts the swaps, each directly
 * followed by rotate_2d 47ffe8 (id 3) in collision_walk. */
static unsigned ow_expected_hits(const callee_counter *c){
    unsigned hits=0;const uint32_t key=c->key==0x47df04u?0x47ffe8u:c->key;
    for(unsigned j=0;j<ow_calls;++j)if(ow_addresses[ow_log[j].id]==key)++hits;
    return hits;
}
static int ow_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    static ow_record expected[OW_MAX];
    memcpy(ow_before,ow_arena,OW_SIZE);
    ow_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=ow_calls;
    memcpy(expected,ow_log,calls*sizeof(*expected));memcpy(ow_after,ow_arena,OW_SIZE);
    memcpy(ow_arena,ow_before,OW_SIZE);ow_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];for(unsigned i=0;i<count&&i<8;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);
    if(n<4096)for(unsigned i=0;i<count&&i<8;++i)if(c[i].count-counts[i]!=ow_expected_hits(c+i))return fail("obstacle_walk callee counter delta");
    if(pair[0]!=pair[1]||calls!=ow_calls||memcmp(expected,ow_log,calls*sizeof(*expected))||memcmp(ow_after,ow_arena,OW_SIZE))
        return ow_report("direct",n,pair,calls,expected);
    return 0;
}
static int ow_patch(BYTE saved[OW_IDS][5],int active[OW_IDS],unsigned which,int on){
    for(unsigned i=0;i<OW_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(ow_addresses[i]-0x400000u,ow_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(ow_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static int ow_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;ow_build(index,n,a,r,0);
        BYTE expected_fpu[108],actual_fpu[108];static ow_record expected[OW_MAX];
        memcpy(ow_before,ow_arena,OW_SIZE);
        ow_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=ow_calls;memcpy(expected,ow_log,calls*sizeof(*expected));memcpy(ow_after,ow_arena,OW_SIZE);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("obstacle_walk poison install");
        memcpy(ow_arena,ow_before,OW_SIZE);ow_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);es2_hook_fpu_save(actual_fpu);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=ow_calls||memcmp(expected,ow_log,calls*sizeof(*expected))||memcmp(ow_after,ow_arena,OW_SIZE)||
           memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=ow_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("obstacle_walk poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/payloads/arena\n",d->name,total);
    return status;
}
/* Real callees: the hook under test runs under the existing hooks that
 * call it. collision_walk and shape_cell: obstacle_test 404ae4 over the
 * obstacle group's real pool (ob_build), with obstacle_test, the other of
 * the two, pool_prev, pool_next, dist2d_point, euler_matrix, rotate_2d and
 * cell nested; convoy_leader: convoy_drive 46a810 (even cases) or
 * convoy_follow 46aa60 (odd) over the convoy group's real arena (cv_build)
 * with the convoy hooks, euler_matrix, rotate_2d and transform_point
 * nested. Nine modes: the hook under test original, replaced or compare,
 * each with three rotations of the nested modes (compare falls back to
 * replaced where refused). Pair and memory against a run without the
 * hook under test; every nested hook and the hook itself must be entered,
 * and no compare may mismatch. */
static unsigned ow_nested(unsigned index,unsigned nested[8]){
    static const unsigned obstacle[]={ES2_HOOK_INDEX_obstacle_test,ES2_HOOK_INDEX_collision_walk,ES2_HOOK_INDEX_shape_cell,
        ES2_HOOK_INDEX_pool_prev,ES2_HOOK_INDEX_pool_next,ES2_HOOK_INDEX_dist2d_point,ES2_HOOK_INDEX_euler_matrix,
        ES2_HOOK_INDEX_rotate_2d,ES2_HOOK_INDEX_cell};
    static const unsigned convoy[]={ES2_HOOK_INDEX_convoy_drive,ES2_HOOK_INDEX_convoy_follow,ES2_HOOK_INDEX_convoy_lead,
        ES2_HOOK_INDEX_convoy_seek,ES2_HOOK_INDEX_convoy_steer,ES2_HOOK_INDEX_euler_matrix,ES2_HOOK_INDEX_rotate_2d,
        ES2_HOOK_INDEX_transform_point};
    unsigned count=0;
    if(index==ES2_HOOK_INDEX_convoy_leader){for(unsigned i=0;i<8;++i)nested[count++]=convoy[i];return count;}
    for(unsigned i=0;i<9;++i)if(obstacle[i]!=index)nested[count++]=obstacle[i];
    return count;
}
static int ow_real_pass(unsigned index){
    unsigned nested[8],count=ow_nested(index,nested);const int convoy=index==ES2_HOOK_INDEX_convoy_leader;
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[9]={0};
    if(convoy)cv_real=1;
    for(unsigned mode=0;mode<9&&!status;++mode)for(unsigned n=0;n<256&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;
        const unsigned caller=convoy?(n&1?ES2_HOOK_INDEX_convoy_follow:ES2_HOOK_INDEX_convoy_drive):ES2_HOOK_INDEX_obstacle_test;
        BYTE *target=mapped+function_hook_descriptor(caller)->rva;
        BYTE before[OB_GLOBALS+CV_GLOBALS],after[OB_GLOBALS+CV_GLOBALS],actual[OB_GLOBALS+CV_GLOBALS];
        size_t size;
        /* cv_build places the convoy arena anew (cv_place): take it after. */
        if(convoy){cv_build(caller,n,a,r,1);size=CV_SIZE;cv_globals(before,0);}
        else {ob_build(caller,n,a,r,1);size=ob_used;ob_globals(before,0);}
        BYTE *arena=convoy?cv_arena:ob_arena,*saved=convoy?cv_before:ob_before,*post=convoy?cv_after:ob_after;
        memcpy(saved,arena,size);
        es2_hook hk[9];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("obstacle_walk nested install");}
        if(convoy){cv_reset();cv_poison_call(target,a,pattern,expected_pair,a[0]+0x26u);cv_globals(after,0);}
        else {ob_reset();sg_poison_call(target,a,pattern,expected_pair);ob_globals(after,0);}
        memcpy(post,arena,size);
        o.mode=(es2_hook_mode)(mode/3);
        if(!weapons_install_hook(hk+8,d,&o,function_hook_extension(index)))return fail("obstacle_walk hook install");
        memcpy(arena,saved,size);
        if(convoy){cv_globals(before,1);cv_reset();cv_poison_call(target,a,pattern,actual_pair,a[0]+0x26u);cv_globals(actual,0);}
        else {ob_globals(before,1);ob_reset();sg_poison_call(target,a,pattern,actual_pair);ob_globals(actual,0);}
        /* collision_walk in compare mode: the caller gets the original
         * phase's pair, run with the nested hooks bypassed; after a walk
         * without a hit EDX's high word is then the original callee's,
         * not the nested hook's (dead, see the adapter): EAX and DX. */
        const uint32_t edx_mask=index==ES2_HOOK_INDEX_collision_walk&&mode/3==2?0xffffu:0xffffffffu;
        if(expected_pair[0]!=actual_pair[0]||((expected_pair[1]^actual_pair[1])&edx_mask)||memcmp(post,arena,size)||
           memcmp(after,actual,convoy?CV_GLOBALS:OB_GLOBALS)){
            fprintf(stderr,"FAIL: %s case %u (real mode=%u) pair %08x%08x/%08x%08x\n",d->name,n,mode,expected_pair[1],expected_pair[0],actual_pair[1],actual_pair[0]);
            for(size_t j=0;j<size;++j)if(post[j]!=arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",(unsigned)j,post[j],arena[j]);break;}
            status=1;
        }
        for(unsigned j=0;j<9;++j){if(j<count||j==8)entered[j]+=hk[j].calls;
            if(hk[j].mismatches){fprintf(stderr,"FAIL: %s case %u (real mode=%u) compare mismatch in %s\n",d->name,n,mode,hk[j].descriptor->name);status=1;}
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("obstacle_walk nested uninstall");}
        if(convoy)cv_globals(before,1);else ob_globals(before,1);
        ++total;
    }
    if(convoy)cv_real=0;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&!entered[8]){fprintf(stderr,"FAIL: %s never entered under its callers\n",d->name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases (%llu calls under %s), %u nested hooks in rotating modes, hook original/replaced/compare, 31/a7 stack, full pair and memory\n",
        d->name,total,(unsigned long long)entered[8],convoy?"convoy_drive/convoy_follow":"obstacle_test",count);
    return status;
}
/* Compare cases (compare_probes.h): all three hooks accept compare; real
 * callees (dist2d_point, euler_matrix, 47df04, rotate_2d, 427360 and
 * cell) on the real-mode arena. */
static void ow_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){ow_build(index,n,a,r,1);}
static void ow_compare_state(BYTE *s,int restore){if(restore)memcpy(ow_arena,s,OW_SIZE);else memcpy(s,ow_arena,OW_SIZE);}
static int obstacle_walk_cases(unsigned index,unsigned limit){
    if(limit&&compare_probe_installed(index))return compare_probe_run(index,limit,ow_compare_build,ow_compare_state,OW_SIZE);
    unsigned which=ow_which(index);
    BYTE saved[OW_IDS][5];int active[OW_IDS];
    if(ow_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_OBSTACLE_WALK_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];ow_build(index,n,a,r,0);status=ow_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=ow_installed_probes(index,64);
    if(ow_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=ow_real_pass(index);
    if(!limit&&!status)status=compare_probe_run(index,0,ow_compare_build,ow_compare_state,OW_SIZE);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, frame payloads by content, arena\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
