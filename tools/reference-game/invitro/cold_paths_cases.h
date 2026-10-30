/* cold_paths group: targ_aim 40e4dc, targ_next 40e4ac, missile_steer 40a488,
 * ally_nearest 423974, turbo_engage 40f09c, member_assess 41b804. No suite
 * enters any of them (live: 0 calls in every suite), so the
 * in-vitro proof is the whole evidence. Direct cases run the original body and
 * the core against the same logging stubs for the hook's game callees (the
 * target's virtuals +24h/+58h/+80h/+84h through a synthetic vtable on T; the
 * direct calls 40e4ac, timer_dword_step 467aa8, euler_matrix 47ebb8,
 * transform_point 48043c, multiply_q8 47e0a0, movement_step 467924, dist_gate
 * 49288c, sfx_play 462830, actor_safe 4137b4, pool_prev 471c70, random_damage
 * 417a04, patched to stubs): the full return pair, the ordered calls with their
 * arguments and entry EAX/EDX/ECX, the arena, the member objects and the three
 * pool pointers. Then installed 31/a7 poison cases; real-callee passes that
 * nest the registered callees in rotating modes; and the compare cases of
 * compare_probes.h for ally_nearest. Member objects live in their own buffers
 * (member_assess writes no object memory); the pool_prev stub and the real pool
 * walk read them. */
typedef struct cp_record { uint32_t id,args[6],regs[3],eax,edx; } cp_record;
enum { CP_SIZE=0x2000, CP_W=0x40, CP_T=0x200, CP_VT=0x300, CP_OUT=0x480, CP_P5=0x4c0, CP_P6=0x4c8,
    CP_B=0x500, CP_NODE=0x600, CP_R=0x700, CP_AX=0x780, CP_AY=0x788, CP_M=0x800, CP_GROUP=0xa00,
    CP_GROUP2=0xa80, CP_LIST=0xb00, CP_OBJ=0xc00, CP_IDS=15, CP_MAX=96, CP_GLOBALS=12, CP_NOBJ=12 };
#define CP_BASE ((uint32_t)(uintptr_t)cp_arena)
static BYTE cp_arena[CP_SIZE],cp_before[CP_SIZE],cp_after[CP_SIZE];
static BYTE cp_mobj[CP_NOBJ][0x200],cp_mchassis[CP_NOBJ][0x60];
static cp_record cp_log[CP_MAX];
static unsigned cp_calls;
static uint32_t cp_seed,cp_stream;
__attribute__((used)) static uint32_t cp_ecx;
/* 0 v24, 1 v58, 2 v84, 3 v80, 4 40e4ac, 5 467aa8, 6 47ebb8, 7 48043c,
 * 8 47e0a0, 9 467924, 10 49288c, 11 462830, 12 4137b4, 13 471c70,
 * 14 417a04. Virtuals 0..3 have key 0; direct calls their VA. */
static const uint32_t cp_direct[CP_IDS]={0,0,0,0,0x40e4ac,0x467aa8,0x47ebb8,0x48043c,0x47e0a0,0x467924,0x49288c,0x462830,0x4137b4,0x471c70,0x417a04};
static const unsigned cp_counts[CP_IDS]={1,3,2,3,2,1,2,3,2,1,2,1,1,2,3};
static uint32_t cp_pool_pointer[3];
static uint32_t cp_pool_list[3][8];static unsigned cp_pool_len[3];
static uint32_t cp_dist[16];static unsigned cp_dists;
static BYTE *cp_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int cp_in(uint32_t p,uint32_t n){return p-CP_BASE<CP_SIZE&&CP_SIZE-(p-CP_BASE)>=n;}
static uint32_t cp_next(void){
    uint32_t z=cp_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
/* The pool_prev stub (id 13): the next object in the pool's list, 0 at the
 * end; a forward walk driven by member_assess's cursor. */
static uint32_t cp_pool_next(uint32_t pool,uint32_t cur){
    for(unsigned p=0;p<3;++p){
        if(cp_pool_pointer[p]!=pool)continue;
        if(!cur)return cp_pool_len[p]?cp_pool_list[p][0]:0;
        for(unsigned i=0;i<cp_pool_len[p];++i)if(cp_pool_list[p][i]==cur)return i+1<cp_pool_len[p]?cp_pool_list[p][i+1]:0;
        return 0;
    }
    return 0;
}
__attribute__((used,noinline)) static uint64_t __cdecl cp_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(cp_calls==CP_MAX){fail("cold_paths synthetic call overflow");ExitProcess(1);}
    cp_record *v=cp_log+cp_calls++;memset(v,0,sizeof(*v));v->id=id;
    for(unsigned k=0;k<cp_counts[id];++k)v->args[k]=a[k];
    for(unsigned j=0;j<3;++j)v->regs[j]=regs[j];
    uint32_t eax=cp_next(),edx=cp_next(),pick=cp_next();cp_ecx=cp_next();
    if(id==2)eax=pick%2?0:1u+cp_next();            /* v84 liveness: 0 drives the 40e4ac path */
    else if(id==0)eax=pick%2?0:CP_BASE+CP_NODE;    /* v24 node: 0 drives the position path */
    else if(id==1&&cp_in(a[2],12))memset(cp_host(a[2]),0,12); /* v58 writes out */
    else if(id==5)eax=pick%2?0:1u+cp_next();        /* timer_dword_step: 0 expires the computer */
    else if(id==7&&cp_in(a[2],12))memset(cp_host(a[2]),0,12); /* transform_point writes out */
    else if(id==10)eax=cp_dists<16?cp_dist[cp_dists++]:pick; /* dist_gate: planned distance */
    else if(id==12)eax=pick%4?0:1u+cp_next();       /* actor_safe: mostly 0 (continue) */
    else if(id==13)eax=cp_pool_next(a[0],a[1]);     /* pool_prev walk */
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define CP_STUB(n) __attribute__((naked)) static void cp_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _cp_stub_impl\n\taddl $24,%esp\n\tmovl _cp_ecx,%ecx\n\tret\n\t");}
CP_STUB(0) CP_STUB(1) CP_STUB(2) CP_STUB(3) CP_STUB(4) CP_STUB(5) CP_STUB(6) CP_STUB(7)
CP_STUB(8) CP_STUB(9) CP_STUB(10) CP_STUB(11) CP_STUB(12) CP_STUB(13) CP_STUB(14)
#undef CP_STUB
static void *cp_stubs[CP_IDS]={cp_stub_0,cp_stub_1,cp_stub_2,cp_stub_3,cp_stub_4,cp_stub_5,cp_stub_6,cp_stub_7,
    cp_stub_8,cp_stub_9,cp_stub_10,cp_stub_11,cp_stub_12,cp_stub_13,cp_stub_14};
/* The direct-call ids each hook patches (virtuals go through the arena
 * vtable on T, not patched). */
static unsigned cp_which(unsigned index){
    if(index==ES2_HOOK_INDEX_targ_aim)return 1u<<4|1u<<5|1u<<6|1u<<7;
    if(index==ES2_HOOK_INDEX_missile_steer)return 1u<<8|1u<<9;
    if(index==ES2_HOOK_INDEX_ally_nearest)return 1u<<10;
    if(index==ES2_HOOK_INDEX_turbo_engage)return 1u<<11;
    if(index==ES2_HOOK_INDEX_member_assess)return 1u<<10|1u<<12|1u<<13|1u<<14;
    return 0; /* targ_next: only the virtual +80h */
}
static void cp_reset(void){cp_calls=0;cp_stream=cp_seed;cp_dists=0;}
static void cp_globals(BYTE *out,int restore){
    static const unsigned off[3]={0xa9b7a,0xa9db9,0xa95a0};
    for(unsigned i=0;i<3;++i){if(restore)memcpy(mapped+off[i],out+4*i,4);else memcpy(out+4*i,mapped+off[i],4);}
}
static uint16_t cp_word(const uint16_t *v,unsigned n){return random32()%4?v[random32()%n]:(uint16_t)random32();}
static void cp_vtable(void){
    put32(cp_arena+CP_VT+0x24,(uint32_t)(uintptr_t)cp_stubs[0]);
    put32(cp_arena+CP_VT+0x58,(uint32_t)(uintptr_t)cp_stubs[1]);
    put32(cp_arena+CP_VT+0x80,(uint32_t)(uintptr_t)cp_stubs[3]);
    put32(cp_arena+CP_VT+0x84,(uint32_t)(uintptr_t)cp_stubs[2]);
}
static void cp_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    const uint32_t b=CP_BASE;(void)n;
    cp_seed=random32();random_bytes(cp_arena,CP_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    cp_vtable();put32(cp_arena+CP_T,b+CP_VT);
    for(unsigned p=0;p<3;++p)cp_pool_pointer[p]=0;
    if(index==ES2_HOOK_INDEX_targ_aim) {
        static const uint16_t locks[]={0,1,2,0xffff,0xfffe,0x7fff,0x8000};
        static const uint16_t health[]={0,0x68,0x69,0x9a,0x9b,0x9c,0xff};
        put16(cp_arena+CP_W+0x7d,cp_word(locks,7));put16(cp_arena+CP_W+0x7f,cp_word(health,7));
        put32(cp_arena+CP_W+0x86,random32());put16(cp_arena+CP_T+0x26,0);
        put16(cp_arena+CP_T+0x32,random32()%2?0:1); /* B = T+0Ch, so the lazy-matrix guard [B+26h] is T+32h */
        a[0]=b+CP_W;a[1]=b+CP_T;a[2]=b+CP_OUT;a[3]=random32();
        a[4]=random32()%4?b+CP_P5:0;a[5]=random32()%4?b+CP_P6:0;
    } else if(index==ES2_HOOK_INDEX_targ_next) {
        static const uint16_t locks[]={0,1,0xffff,0x8000,0x7fff};put16(cp_arena+CP_W+0x7d,cp_word(locks,5));
        put32(cp_arena+CP_W+0x86,random32());
        a[0]=b+CP_W;a[1]=random32()%4?b+CP_T:0;
    } else if(index==ES2_HOOK_INDEX_missile_steer) {
        a[0]=b+CP_M;a[1]=b+CP_R;
        cp_arena[CP_R+0xd]=real?1:(BYTE)(random32()%2);
        put32(cp_arena+CP_R+0x22,b+CP_AX);put32(cp_arena+CP_R+0x26,b+CP_AY);
        put16(cp_arena+CP_AX,(uint16_t)random32());put16(cp_arena+CP_AY,(uint16_t)random32());
        put16(cp_arena+CP_M+0xc,(uint16_t)random32());put16(cp_arena+CP_M+0xe,(uint16_t)random32());
        put16(cp_arena+CP_M+0x10,(uint16_t)random32());put16(cp_arena+CP_M+0x32,random32()%2);
    } else if(index==ES2_HOOK_INDEX_turbo_engage) {
        static const uint32_t charge[]={0,600,601,0x7fffffffu,0x80000000u,1000};
        put32(cp_arena+CP_W+0x81,random32()%2?0:1u+random32());
        put32(cp_arena+CP_W+0x7d,random32()%4?charge[random32()%6]:random32());
        put32(cp_arena+CP_W+0x79,random32()%2?0:1u+random32());
        a[0]=b+CP_W;
    } else if(index==ES2_HOOK_INDEX_ally_nearest) {
        unsigned count=random32()%6;
        /* The count word matches the filled list (a positive count beyond
         * the list would read an invalid member and fault, as on retail);
         * a negative count exercises the loop's no-run edge. */
        put16(cp_arena+CP_GROUP+0x10,random32()%5?(uint16_t)count:(random32()%2?0x8000:0xffff));
        put32(cp_arena+CP_GROUP+0xc,b+CP_LIST);
        for(unsigned i=0;i<count&&i<6;++i){uint32_t off=CP_OBJ+0x100u*i;put32(cp_arena+CP_LIST+4u*i,b+off);
            cp_arena[off+0xa4]=random32()%4?0:1;cp_arena[off+0x99]=random32()%4?0:1;}
        a[0]=b+CP_GROUP;a[1]=random32()%4?b+CP_OBJ:b+CP_OBJ+0x100u*(random32()%(count?count:1));
        for(unsigned i=0;i<16;++i)cp_dist[i]=random32()%2?random32()%200000:random32();
    } else { /* member_assess */
        const uint32_t M=b+CP_M,G=b+CP_GROUP;a[0]=M;
        put32(cp_arena+CP_M+0x45,G);cp_arena[CP_GROUP+0x12]=1;cp_arena[CP_GROUP2+0x12]=2;
        /* The group-pending word [group+14h] is mostly 0 (objects pass the
         * skip) but sometimes nonzero (the pending-skip edge). */
        put32(cp_arena+CP_GROUP+0x14,random32()%4?0:1u+random32());
        put32(cp_arena+CP_GROUP2+0x14,random32()%4?0:1u+random32());
        cp_pool_pointer[0]=b+0x0e0;cp_pool_pointer[1]=b+0x0e8;cp_pool_pointer[2]=b+0x0f0;
        put32(mapped+0xa9b7a,cp_pool_pointer[0]);put32(mapped+0xa9db9,cp_pool_pointer[1]);put32(mapped+0xa95a0,cp_pool_pointer[2]);
        for(unsigned i=0;i<16;++i)cp_dist[i]=random32()%2?random32()%60000:random32();
        unsigned obj=0;
        for(unsigned p=0;p<3;++p){unsigned k=random32()%5;cp_pool_len[p]=k;
            for(unsigned i=0;i<k&&obj<CP_NOBJ;++i,++obj){BYTE *A=cp_mobj[obj];uint32_t Aa=(uint32_t)(uintptr_t)A;
                cp_pool_list[p][i]=Aa;memset(A,0,0x200);
                A[0xa5]=random32()%5?0:1;A[0xa4]=random32()%5?0:1;A[0x99]=random32()%5?0:1;
                put32(A+0x45,random32()%2?G:b+CP_GROUP2);
                put32(A+0x1f2,(uint32_t)(uintptr_t)cp_mchassis[obj]);
                put16(cp_mchassis[obj]+0x54,(uint16_t)(random32()%2000));
                put16(cp_mchassis[obj]+0x2e,random32()%2);
            }
            for(unsigned i=k;i<8;++i)cp_pool_list[p][i]=0;
        }
    }
    (void)real;
}
static int cp_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const cp_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,cp_calls);
    for(unsigned j=0;j<calls&&j<cp_calls&&expected;++j)if(memcmp(expected+j,cp_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(cp_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<CP_SIZE;++j)if(cp_after[j]!=cp_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,cp_after[j],cp_arena[j]);break;}
    return 1;
}
static unsigned cp_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<cp_calls;++j){const uint32_t k=cp_log[j].id<4?0u:cp_direct[cp_log[j].id];
        if(c->event==ES2_COLLIDE_CALL&&c->key==k&&k)++hits;}
    /* Virtual slots: the slot offset keys +24/+58/+80/+84. */
    if(c->event==ES2_COLLIDE_SLOT)for(unsigned j=0;j<cp_calls;++j){
        unsigned id=cp_log[j].id;uint32_t slot=id==0?0x24:id==1?0x58:id==2?0x84:id==3?0x80:0;
        if(slot==c->key)++hits;}
    return hits;
}
static int cp_patch(BYTE saved[CP_IDS][5],int active[CP_IDS],unsigned which,int on){
    for(unsigned i=0;i<CP_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!cp_direct[i])continue;if(actor_patch(cp_direct[i]-0x400000u,cp_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(cp_direct[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static int cp_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[CP_GLOBALS],after[CP_GLOBALS],actual[CP_GLOBALS];static cp_record expected[CP_MAX];
    memcpy(cp_before,cp_arena,CP_SIZE);cp_globals(before,0);
    cp_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=cp_calls;
    memcpy(expected,cp_log,calls*sizeof(*expected));memcpy(cp_after,cp_arena,CP_SIZE);cp_globals(after,0);
    memcpy(cp_arena,cp_before,CP_SIZE);cp_globals(before,1);cp_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8]={0};for(unsigned i=0;i<count&&i<8;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);cp_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count&&i<8;++i)if(c[i].count-counts[i]!=cp_expected_hits(c+i))return fail("cold_paths callee counter delta");
    if(pair[0]!=pair[1]||calls!=cp_calls||memcmp(expected,cp_log,calls*sizeof(*expected))||memcmp(cp_after,cp_arena,CP_SIZE)||memcmp(after,actual,CP_GLOBALS))
        return cp_report("direct",n,pair,calls,expected);
    return 0;
}
static int cp_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;cp_build(index,n,a,r,0);
        BYTE after[CP_GLOBALS],actual[CP_GLOBALS],before[CP_GLOBALS],expected_fpu[108],actual_fpu[108];static cp_record expected[CP_MAX];
        memcpy(cp_before,cp_arena,CP_SIZE);cp_globals(before,0);
        cp_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=cp_calls;memcpy(expected,cp_log,calls*sizeof(*expected));memcpy(cp_after,cp_arena,CP_SIZE);cp_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("cold_paths poison install");
        memcpy(cp_arena,cp_before,CP_SIZE);cp_globals(before,1);cp_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);es2_hook_fpu_save(actual_fpu);cp_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=cp_calls||memcmp(expected,cp_log,calls*sizeof(*expected))||memcmp(cp_after,cp_arena,CP_SIZE)||
           memcmp(after,actual,CP_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=cp_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("cold_paths poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/arena/pointers\n",d->name,total);
    return status;
}
/* Real callees. targ_aim nests targ_next (same group), timer_dword_step,
 * euler_matrix and transform_point; missile_steer nests multiply_q8 and
 * movement_step; ally_nearest nests dist_gate; turbo_engage nests sfx_play
 * (whose SFX manager stays original behind the bridge); member_assess
 * nests actor_safe, pool_prev, dist_gate and random_damage. The virtuals
 * of targ_aim/targ_next stay stubs through the arena vtable; the directs
 * that are not registered callees stay stubs too. The hook under test runs
 * original, replaced or compare (where accepted), each with three rotations
 * of the nested modes. Pair, arena, objects and pointers against a run
 * without the hook under test; the hook and every nested hook must be
 * entered, no compare may mismatch. */
static unsigned cp_nested(unsigned index,unsigned nested[4]){
    if(index==ES2_HOOK_INDEX_targ_aim){nested[0]=ES2_HOOK_INDEX_targ_next;nested[1]=ES2_HOOK_INDEX_timer_dword_step;
        nested[2]=ES2_HOOK_INDEX_euler_matrix;nested[3]=ES2_HOOK_INDEX_transform_point;return 4;}
    if(index==ES2_HOOK_INDEX_missile_steer){nested[0]=ES2_HOOK_INDEX_multiply_q8;nested[1]=ES2_HOOK_INDEX_movement_step;return 2;}
    if(index==ES2_HOOK_INDEX_ally_nearest){nested[0]=ES2_HOOK_INDEX_dist_gate;return 1;}
    if(index==ES2_HOOK_INDEX_member_assess){nested[0]=ES2_HOOK_INDEX_dist_gate;return 1;}
    /* targ_next (only the virtual +80h) and turbo_engage (sfx_play's SFX
     * manager subtree stays original, as in production) have no real-callee
     * pass: their callees are covered by the direct and installed cases. */
    return 0;
}
/* The stub ids kept patched during the real pass (the callees not nested
 * as real hooks): member_assess keeps actor_safe, pool_prev and
 * random_damage as stubs so the pool walk stays finite and the damage loop
 * writes nothing, while dist_gate runs as a real nested hook. */
static unsigned cp_real_stubs(unsigned index){
    if(index==ES2_HOOK_INDEX_member_assess)return 1u<<12|1u<<13|1u<<14;
    return 0;
}
static int cp_real_pass(unsigned index){
    unsigned nested[4],count=cp_nested(index,nested);
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    int status=0;unsigned total=0;uint64_t entered[5]={0};
    for(unsigned mode=0;mode<9&&!status;++mode)for(unsigned nn=0;nn<256&&!status;++nn){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=nn&1?0xa7a7a7a7u:0x31313131u;cp_build(index,nn,a,r,1);
        BYTE before[CP_GLOBALS],after[CP_GLOBALS],actual[CP_GLOBALS];void *target=mapped+d->rva;
        memcpy(cp_before,cp_arena,CP_SIZE);cp_globals(before,0);
        /* Place the three pool pointers for pool_prev's real walk; keep the
         * member objects as the pool's used list the real 471c70 reads. */
        es2_hook hk[5];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!nested_install_hook(hk+j,nested[j],&o,e))return fail("cold_paths nested install");}
        unsigned which=cp_real_stubs(index);BYTE saved[CP_IDS][5];int active[CP_IDS];
        if(cp_patch(saved,active,which,1))return 1;
        cp_reset();sg_poison_call(target,a,pattern,expected_pair);
        memcpy(cp_after,cp_arena,CP_SIZE);cp_globals(after,0);
        o.mode=(es2_hook_mode)(mode/3);if(x->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+4,d,&o,x))return fail("cold_paths hook install");
        memcpy(cp_arena,cp_before,CP_SIZE);cp_globals(before,1);
        cp_reset();sg_poison_call(target,a,pattern,actual_pair);cp_globals(actual,0);
        if(cp_patch(saved,active,which,0))return 1;
        /* targ_aim's EDX is the frame address euler_matrix/transform_point
         * leave on the node path (dead at aim_point's callers); it moves
         * with the depth the original runs at, so the real pass checks EAX
         * and memory for it and EDX too for the others. */
        const int edx_matters=index!=ES2_HOOK_INDEX_targ_aim;
        if(expected_pair[0]!=actual_pair[0]||(edx_matters&&expected_pair[1]!=actual_pair[1])||
           memcmp(cp_after,cp_arena,CP_SIZE)||memcmp(after,actual,CP_GLOBALS)){
            fprintf(stderr,"FAIL: %s case %u (real mode=%u) pair %08x%08x/%08x%08x\n",d->name,nn,mode,expected_pair[1],expected_pair[0],actual_pair[1],actual_pair[0]);
            status=1;
        }
        for(unsigned j=0;j<5;++j){if(j<count||j==4)entered[j]+=hk[j].calls;
            if(hk[j].mismatches){fprintf(stderr,"FAIL: %s case %u (real mode=%u) compare mismatch in %s\n",d->name,nn,mode,hk[j].descriptor->name);status=1;}
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("cold_paths nested uninstall");}
        ++total;
    }
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&!entered[4]){fprintf(stderr,"FAIL: %s never entered\n",d->name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases (%llu hook calls), %u nested hook(s) in rotating modes, hook original/replaced/compare, 31/a7 stack, pair and memory\n",
        d->name,total,(unsigned long long)entered[4],count);
    return status;
}
static void cp_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){cp_build(index,n,a,r,1);}
static void cp_compare_state(BYTE *s,int restore){
    if(restore){memcpy(cp_arena,s,CP_SIZE);cp_globals(s+CP_SIZE,1);}else{memcpy(s,cp_arena,CP_SIZE);cp_globals(s+CP_SIZE,0);}
}
static int cold_paths_cases(unsigned index,unsigned limit){
    BYTE old_globals[CP_GLOBALS];cp_globals(old_globals,0);
    if(limit&&compare_probe_installed(index)){int status=compare_probe_run(index,limit,cp_compare_build,cp_compare_state,CP_SIZE+CP_GLOBALS);
        cp_globals(old_globals,1);return status;}
    unsigned which=cp_which(index);
    BYTE saved[CP_IDS][5];int active[CP_IDS];
    if(cp_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_COLD_PATHS_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];cp_build(index,n,a,r,0);status=cp_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=cp_installed_probes(index,64);
    if(cp_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    { unsigned n4[4]; if(!limit&&!status&&cp_nested(index,n4))status=cp_real_pass(index); }
    if(!limit&&!status&&compare_probe_capable(index))status=compare_probe_run(index,0,cp_compare_build,cp_compare_state,CP_SIZE+CP_GLOBALS);
    cp_globals(old_globals,1);
    if(!status)printf("PASS: %-16s %u cases; full return pair, ordered calls with entry registers, arena/objects/pointers\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
