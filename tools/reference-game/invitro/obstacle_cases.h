/* Obstacle group: obstacle_test 404ae4 and convoy_collide 46a614
 * (docs/reverse-engineering/mission_runtime.md, section "Static-obstacle list
 * 404ae4", and convoy.md, "collision test 0x46a614"). Direct cases run the
 * original body and the core against logging stubs for the game callees of the
 * hook under test (the pool step 471c70 with a planned sequence, the collision
 * walk 427c68, dist_gate, 404ae4 and the actor virtuals +5Ch/+7Ch/+68h through
 * the arena vtable): ordered calls with their arguments and entry EAX/EDX/ECX
 * (stack addresses masked, as in convoy_cases.h), and the list 427c68 receives
 * by content. The pool objects use the original node stride (8 + 26Dh). One
 * case in 128 (and part of the boundary prefix) lists 139 to 310 objects, so
 * the list overflows: the 141st append overwrites the cursor slot and the later
 * ones land in the 141st object and the node after it; the arena and 427c68's
 * view of entries 0..142 must match (entry 141's low word is the original's
 * uninitialized EBP-8 and is masked). Then installed 31/a7 poison probes and a
 * real-callee pass with the nested hooks in rotating modes over a real pool
 * (real 471c70, 427c68, 427360, 47df04 and the building virtuals
 * 4035a4/4035b8/42200c). */
typedef struct ob_record { uint32_t id,args[4],regs[3],eax,edx; } ob_record;
enum { OB_HEAD=0x1000, OB_STRIDE=0x275, OB_OBJECTS=312, OB_SIZE=OB_HEAD+OB_STRIDE*OB_OBJECTS,
    OB_POOL=0x40, OB_CFG=0x60, OB_REC=0x160, OB_VT=0x1a0, OB_GRP=0x220, OB_LIST=0x260, OB_POINT=0x2e0, OB_MGR=0x300,
    OB_ROWS=0x320, OB_ROWDATA=0x360, OB_SHAPE=0x500, OB_SHAPE2=0xa00, OB_MAX=320, OB_IDS=7, OB_GLOBALS=12,
    OB_LISTED=143 };
#define OB_BASE ((uint32_t)(uintptr_t)ob_arena)
/* Object i with its 8-byte pool node header in front of it. */
#define OB_OBJECT(i) (OB_BASE+OB_HEAD+OB_STRIDE*(uint32_t)(i)+8u)
static BYTE ob_arena[OB_SIZE],ob_before[OB_SIZE],ob_after[OB_SIZE];
static ob_record ob_log[OB_MAX];
static unsigned ob_calls,ob_cursor,ob_count,ob_used,ob_listed,ob_objects;
static uint32_t ob_seed,ob_stream,ob_seq[OB_OBJECTS],ob_list[OB_LISTED],ob_radius,ob_s;
__attribute__((used)) static uint32_t ob_ecx;
/* 0 471c70 pool step, 1 427c68 collision walk, 2 virtual +5Ch, 3 virtual
 * +7Ch, 4 49288c dist_gate, 5 virtual +68h, 6 404ae4 obstacle_test (2, 3
 * and 5 are reached through the arena vtable, never patched). */
static const uint32_t ob_addresses[OB_IDS]={0x471c70,0x427c68,0,0,0x49288c,0,0x404ae4};
static const unsigned ob_slots[OB_IDS]={0,0,0x5c,0x7c,0,0x68,0};
static const unsigned ob_counts[OB_IDS]={2,4,1,1,2,1,3};
static uint32_t ob_next(void){
    uint32_t z=ob_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *ob_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
/* Same fixed stack window as node_cases.h: frame addresses are masked. */
static int ob_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static uint32_t ob_small(uint32_t pick,uint32_t range){return (uint32_t)((int32_t)(pick%(2u*range+1u))-(int32_t)range);}
static void ob_globals(BYTE *out,int restore){
    if(restore){memcpy(mapped+0xa95a0,out,4);memcpy(mapped+0xa9af8,out+4,8);}
    else {memcpy(out,mapped+0xa95a0,4);memcpy(out+4,mapped+0xa9af8,8);}
}
__attribute__((used,noinline)) static uint64_t __cdecl ob_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(ob_calls==OB_MAX){fail("obstacle synthetic call overflow");ExitProcess(1);}
    ob_record *v=ob_log+ob_calls++;memset(v,0,sizeof(*v));v->id=id;
    for(unsigned k=0;k<ob_counts[id];++k)v->args[k]=ob_stack(a[k])?0:a[k];
    for(unsigned j=0;j<3;++j)v->regs[j]=ob_stack(regs[j])?0:regs[j];
    uint32_t eax=ob_next(),edx=ob_next(),pick=ob_next();ob_ecx=ob_next();
    switch(id){
    case 0: /* pool step: the planned objects, then 0; the pool pointer and
             * a later object's filter fields may change. */
        eax=ob_cursor<ob_count?ob_seq[ob_cursor]:0;if(eax)++ob_cursor;
        if(pick%13==0)put32(mapped+0xa95a0,OB_BASE+OB_POOL+(pick>>8&4));
        if(eax&&pick%7==1&&ob_cursor<ob_count){BYTE *o=ob_host(ob_seq[ob_cursor]);
            if(pick>>8&1)o[0x99]^=1;else put32(o+0x1f2,OB_BASE+OB_CFG+0x20u*(pick>>12&7));}
        break;
    case 1: { /* collision walk: the count word (its high word is the last
               * pool step's EDX, never read) and the list by content;
               * entry 141's low word is the uninitialized EBP-8. */
        v->args[3]&=0xffffu;
        unsigned n=(uint16_t)a[3];if(n>OB_LISTED)n=OB_LISTED;ob_listed=n;
        for(unsigned j=0;j<n;++j){uint32_t e=get32(ob_host(a[2])+4*j);if(j==141)e&=0xffff0000u;ob_list[j]=ob_stack(e)?0:e;}
        if(pick%2)eax=0;
        break; }
    case 2: { /* +5Ch: B's radius word. */
        static const uint16_t edges[]={0,1,0x7fff,0x8000,0xffff,0x100};
        eax=(eax&0xffff0000u)|(pick%3?(uint16_t)(pick>>8&0x1fff):edges[(pick>>8)%6]);ob_radius=eax;
        break; }
    case 3: { /* +7Ch: A's radius word, zero a quarter of the time; the
               * count word and a later list entry may change. */
        static const uint16_t edges[]={0,1,0x7fff,0x8000,0xffff,0x200};
        eax=(eax&0xffff0000u)|(pick%4==0?0:pick%4==1?edges[(pick>>8)%6]:(uint16_t)(pick>>8&0x1fff));ob_s=eax;
        if(pick%11==0)put16(mapped+0xa9afe,(uint16_t)(pick>>16)%10u);
        if(pick%11==1)put32(ob_arena+OB_LIST+4*((pick>>16)%10),pick>>20&1||!ob_objects?0:OB_OBJECT((pick>>24)%ob_objects));
        if(pick%11==2)put32(ob_arena+OB_GRP+0x14+0x20*(pick>>16&1),pick>>20&1);
        break; }
    case 4: /* dist_gate: around the combined radius. */
        if(pick%2)eax=(uint32_t)(int32_t)(int16_t)ob_radius+(uint32_t)(int32_t)(int16_t)ob_s+ob_small(pick>>4,2);
        break;
    case 5: /* +68h: the flag byte is the original's; nothing to model. */
        break;
    case 6: /* 404ae4: AX zero half of the time. */
        if(pick%2)eax&=0xffff0000u;
        break;
    }
    v->eax=ob_stack(eax)?0:eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define OB_STUB(n) __attribute__((naked)) static void ob_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _ob_stub_impl\n\taddl $24,%esp\n\tmovl _ob_ecx,%ecx\n\tret\n\t");}
OB_STUB(0) OB_STUB(1) OB_STUB(2) OB_STUB(3) OB_STUB(4) OB_STUB(5) OB_STUB(6)
#undef OB_STUB
static void *ob_stubs[OB_IDS]={ob_stub_0,ob_stub_1,ob_stub_2,ob_stub_3,ob_stub_4,ob_stub_5,ob_stub_6};
static unsigned ob_which(unsigned index){return index==ES2_HOOK_INDEX_obstacle_test?3u:1u<<4|1u<<6;}
static void ob_reset(void){ob_calls=0;ob_cursor=0;ob_listed=0;ob_stream=ob_seed;ob_radius=ob_s=0;}
static uint16_t ob_word(const uint16_t *values,unsigned count){return values[random32()%count];}
/* Filter fields: configurations (+4, +6, +12h, radius +2Ah), records
 * (+5) and the dead byte +99h around their thresholds. */
static void ob_configs(int real){
    static const uint16_t levels[]={0,1,2,0xffff,0x8000,0x7fff,3};
    for(unsigned i=0;i<8;++i){BYTE *c=ob_arena+OB_CFG+0x20*i;
        put16(c+6,i==0?0:random32()%2?0:(uint16_t)(random32()|1u));
        put16(c+4,i==0?0:random32()%3==0?0xffff:(uint16_t)random32());
        put16(c+0x12,random32()%3?ob_word(levels,7):(uint16_t)random32());
        put16(c+0x2a,(uint16_t)(real?random32()%0x3000:random32()));}
    for(unsigned i=0;i<4;++i)put16(ob_arena+OB_REC+0x10*i+5,i==0?1:random32()%3?ob_word(levels,7):(uint16_t)random32());
}
static void ob_filter(BYTE *o,int take){
    put32(o+0x1f2,OB_BASE+OB_CFG+(take?0:0x20u*(random32()%8)));
    put32(o+0x205,OB_BASE+OB_REC+(take?0:0x10u*(random32()%4)));
    o[0x99]=(BYTE)(take||random32()%2?0:random32()%2?1:random32());
}
/* Real-pass shapes: a radius word +8, a 4 x 4 grid of byte cells over a
 * 256-entry height map, offsets and a shift that put about half of the
 * rotated deltas on the grid. */
static void ob_shapes(void){
    for(unsigned s=0;s<2;++s){uint32_t base=OB_BASE+(s?OB_SHAPE2:OB_SHAPE);BYTE *x=ob_host(base);
        put16(x+8,(uint16_t)(0x800+random32()%0x2000));
        put16(x+0x2a,(uint16_t)(random32()%5?4:0));put16(x+0x2c,4);put16(x+0x2e,2);put16(x+0x30,2);put16(x+0x32,(uint16_t)(10+random32()%3));
        for(unsigned j=0;j<256;++j)put32(x+0x34+4*j,random32()%2?0:random32());
        put32(x+0x434,OB_BASE+OB_ROWS+0x20*s);
        for(unsigned j=0;j<4;++j){put32(ob_arena+OB_ROWS+0x20*s+4*j,OB_BASE+OB_ROWDATA+0x40*s+8*j);
            for(unsigned k=0;k<8;++k)ob_arena[OB_ROWDATA+0x40*s+8*j+k]=(BYTE)random32();}
        put32(ob_arena+OB_MGR+0x10*s+4,base);}
}
/* A pool of count nodes in random memory order linked into the used list;
 * the sequence the original's walk returns is the used list backwards. */
static void ob_pool(unsigned count){
    unsigned order[OB_OBJECTS];for(unsigned i=0;i<count;++i)order[i]=i;
    for(unsigned i=count;i>1;--i){unsigned j=random32()%i,t=order[i-1];order[i-1]=order[j];order[j]=t;}
    BYTE *p=ob_arena+OB_POOL;put16(p,0);put16(p+2,(uint16_t)count);put16(p+4,0x26d);
    put32(p+6,OB_BASE+OB_HEAD);put32(p+0xa,0);put32(p+0xe,count?OB_OBJECT(order[0])-8u:0);
    for(unsigned i=0;i<count;++i){BYTE *node=ob_host(OB_OBJECT(order[i])-8u);
        put32(node,i+1<count?OB_OBJECT(order[i+1])-8u:0);put32(node+4,i?OB_OBJECT(order[i-1])-8u:0);
        ob_seq[count-1-i]=OB_OBJECT(order[i]);}
}
static void ob_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    uint32_t b=OB_BASE;
    /* Objects: a few, or (one case in 128, and cases 16..47 of the
     * prefix) 139..310 objects that nearly all pass the filter. */
    unsigned objects,wide=index==ES2_HOOK_INDEX_obstacle_test&&!real&&((n>=16&&n<48)||n%128==127);
    if(wide)objects=n>=16&&n<48?139+(n-16)%8+(n>=40?160:0):139+random32()%172;
    else objects=real||index==ES2_HOOK_INDEX_convoy_collide?1+random32()%10:random32()%10;
    ob_used=OB_HEAD+OB_STRIDE*(objects+1u<OB_OBJECTS?objects+1u:OB_OBJECTS);
    ob_seed=random32();random_bytes(ob_arena,ob_used);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    ob_configs(real);
    for(unsigned i=0;i<objects;++i){BYTE *o=ob_host(OB_OBJECT(i));ob_filter(o,wide&&(n<48||random32()%16));
        put32(o,b+OB_VT);put32(o+0x45,b+OB_GRP+0x20u*(random32()%2));}
    put32(ob_arena+OB_GRP+0x14,random32()%3==0?random32()|1u:0);put32(ob_arena+OB_GRP+0x34,random32()%2);
    ob_pool(objects);ob_count=ob_objects=objects;
    put32(mapped+0xa95a0,b+OB_POOL);
    if(!real){
        /* The planned walk: the used list backwards, sometimes shortened
         * or with an object repeated. */
        if(!wide&&objects&&random32()%5==0)ob_seq[random32()%objects]=ob_seq[random32()%objects];
        if(!wide&&random32()%7==0)ob_count=random32()%(objects+1u);
        put32(ob_arena+OB_VT+0x5c,(uint32_t)(uintptr_t)ob_stubs[2]);put32(ob_arena+OB_VT+0x7c,(uint32_t)(uintptr_t)ob_stubs[3]);
        put32(ob_arena+OB_VT+0x68,(uint32_t)(uintptr_t)ob_stubs[5]);
    } else {
        /* Real building virtuals; shapes, bounded positions near P, the
         * matrix flag and angles for the real collision walk. */
        put32(ob_arena+OB_VT+0x5c,(uint32_t)(uintptr_t)(mapped+0x35a4));put32(ob_arena+OB_VT+0x7c,(uint32_t)(uintptr_t)(mapped+0x35b8));
        put32(ob_arena+OB_VT+0x68,(uint32_t)(uintptr_t)(mapped+0x2200c));
        ob_shapes();
        for(unsigned i=0;i<objects;++i){BYTE *o=ob_host(OB_OBJECT(i));
            put32(o+0x34,b+OB_MGR+0x10u*(random32()%2));
            for(unsigned j=0;j<3;++j)put32(o+0x26+4*j,ob_small(random32(),random32()%2?0x1800:0x6000));
            put16(o+0x32,(uint16_t)(random32()%2));}
        for(unsigned j=0;j<3;++j)put32(ob_arena+OB_POINT+4*j,ob_small(random32(),0x400));
    }
    /* Actor list for 46a614: B, objects, a null entry; the count around
     * the planned length; entry 31 is always 0. */
    unsigned m=random32()%10;
    for(unsigned j=0;j<32;++j)put32(ob_arena+OB_LIST+4*j,j<31&&objects&&(j<m||random32()%3)?OB_OBJECT(random32()%objects):0);
    if(objects&&random32()%2)put32(ob_arena+OB_LIST+4*(random32()%(m+1)),OB_OBJECT(0));
    if(!real&&random32()%6==0)put32(ob_arena+OB_LIST+4*(random32()%(m+1)),0);
    put32(mapped+0xa9af8,b+OB_LIST);
    put16(mapped+0xa9afe,(uint16_t)(real?m:random32()%8==0?(uint16_t)random32():m+ob_small(random32(),2)));
    if(index==ES2_HOOK_INDEX_obstacle_test){
        a[0]=b+OB_POINT;a[1]=random32();
        unsigned pick=random32()%4;a[2]=pick==0||!objects?(pick==0?0:random32()):OB_OBJECT(random32()%objects);
        if(real)a[2]=pick<2||!objects?0:OB_OBJECT(random32()%objects);
    } else a[0]=OB_OBJECT(0);
}
static int ob_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const ob_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,ob_calls);
    for(unsigned j=0;j<calls&&j<ob_calls&&expected;++j)if(memcmp(expected+j,ob_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(ob_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<ob_used;++j)if(ob_after[j]!=ob_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,ob_after[j],ob_arena[j]);break;}
    return 1;
}
static unsigned ob_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<ob_calls;++j){unsigned id=ob_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?ob_slots[id]==c->key:!ob_slots[id]&&ob_addresses[id]==c->key)++hits;}
    return hits;
}
static int ob_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[OB_GLOBALS],after[OB_GLOBALS],actual[OB_GLOBALS];static ob_record expected[OB_MAX];
    static uint32_t list[OB_LISTED];
    memcpy(ob_before,ob_arena,ob_used);ob_globals(before,0);
    ob_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=ob_calls,listed=ob_listed;
    memcpy(expected,ob_log,calls*sizeof(*expected));memcpy(list,ob_list,listed*4);
    memcpy(ob_after,ob_arena,ob_used);ob_globals(after,0);
    memcpy(ob_arena,ob_before,ob_used);ob_globals(before,1);ob_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];for(unsigned i=0;i<count&&i<8;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);ob_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count&&i<8;++i)if(c[i].count-counts[i]!=ob_expected_hits(c+i))return fail("obstacle callee counter delta");
    if(listed!=ob_listed||memcmp(list,ob_list,listed*4)){
        for(unsigned j=0;j<listed&&j<ob_listed;++j)if(list[j]!=ob_list[j]){fprintf(stderr," list entry %u %08x/%08x\n",j,list[j],ob_list[j]);break;}
        return ob_report("listed",n,pair,calls,expected);
    }
    if(pair[0]!=pair[1]||calls!=ob_calls||memcmp(expected,ob_log,calls*sizeof(*expected))||memcmp(ob_after,ob_arena,ob_used)||
       memcmp(after,actual,OB_GLOBALS))
        return ob_report("direct",n,pair,calls,expected);
    return 0;
}
static int ob_patch(BYTE saved[OB_IDS][5],int active[OB_IDS],unsigned which,int on){
    for(unsigned i=0;i<OB_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!ob_addresses[i])continue;if(actor_patch(ob_addresses[i]-0x400000u,ob_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(ob_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static int ob_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;ob_build(index,n,a,r,0);
        BYTE before[OB_GLOBALS],after[OB_GLOBALS],actual[OB_GLOBALS],expected_fpu[108],actual_fpu[108];
        static ob_record expected[OB_MAX];static uint32_t list[OB_LISTED];
        memcpy(ob_before,ob_arena,ob_used);ob_globals(before,0);
        ob_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=ob_calls,listed=ob_listed;memcpy(expected,ob_log,calls*sizeof(*expected));memcpy(list,ob_list,listed*4);
        memcpy(ob_after,ob_arena,ob_used);ob_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("obstacle poison install");
        memcpy(ob_arena,ob_before,ob_used);ob_globals(before,1);ob_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);es2_hook_fpu_save(actual_fpu);ob_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=ob_calls||memcmp(expected,ob_log,calls*sizeof(*expected))||memcmp(ob_after,ob_arena,ob_used)||
           listed!=ob_listed||memcmp(list,ob_list,listed*4)||memcmp(after,actual,OB_GLOBALS)||
           memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=ob_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("obstacle poison uninstall");ob_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/list/arena/globals\n",d->name,total);
    return status;
}
/* Real callees with the nested hooks in rotating original/replaced/compare
 * modes (compare falls back to replaced where refused) and the hook itself
 * original or replaced: the real pool step 471c70 over a real used list,
 * the real collision walk 427c68 with 427360/47df04 and the nested
 * dist2d_point, euler_matrix, rotate_2d and cell; convoy_collide adds the
 * real building virtuals, dist_gate and obstacle_test itself. Pair, arena
 * and globals against a run with only the nested hooks installed. */
static int ob_real_pass(unsigned index){
    unsigned nested[6],count=0;
    if(index==ES2_HOOK_INDEX_convoy_collide){nested[count++]=ES2_HOOK_INDEX_dist_gate;nested[count++]=ES2_HOOK_INDEX_obstacle_test;}
    nested[count++]=ES2_HOOK_INDEX_dist2d_point;nested[count++]=ES2_HOOK_INDEX_euler_matrix;
    nested[count++]=ES2_HOOK_INDEX_rotate_2d;nested[count++]=ES2_HOOK_INDEX_cell;
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[6]={0};
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;ob_build(index,n,a,r,1);
        BYTE before[OB_GLOBALS],after[OB_GLOBALS],actual[OB_GLOBALS];
        memcpy(ob_before,ob_arena,ob_used);ob_globals(before,0);
        es2_hook hk[7];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("obstacle nested install");}
        ob_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        memcpy(ob_after,ob_arena,ob_used);ob_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+6,d,&o,function_hook_extension(index)))return fail("obstacle caller install");
        memcpy(ob_arena,ob_before,ob_used);ob_globals(before,1);ob_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);ob_globals(actual,0);
        if(expected_pair[0]!=actual_pair[0]||expected_pair[1]!=actual_pair[1]||memcmp(ob_after,ob_arena,ob_used)||memcmp(after,actual,OB_GLOBALS)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            fprintf(stderr,"real mode=%u\n",mode);ob_calls=0;status=ob_report("real",n,pair,0,NULL);
        }
        for(unsigned j=0;j<7;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("obstacle nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("obstacle nested uninstall");}
        ob_globals(before,1);++total;
    }
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, 31/a7 stack, full pair, arena and globals\n",d->name,total,count);
    return status;
}
static int obstacle_cases(unsigned index,unsigned limit){
    unsigned which=ob_which(index);
    BYTE saved[OB_IDS][5],old_globals[OB_GLOBALS];int active[OB_IDS];
    ob_globals(old_globals,0);if(ob_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_OBSTACLE_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];ob_build(index,n,a,r,0);status=ob_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=ob_installed_probes(index,64);
    if(ob_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=ob_real_pass(index);
    ob_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, the listed entries by content, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
