/* weapon_leaves group: proj_record 40ffb0, pitch_point 49295c, damage_words
 * 40501c, bullet_charge 40b5a0 and rocket_init 40a948
 * (docs/reverse-engineering/weapons.md, "weapon and projectile helpers").
 * Direct cases run the original body (EAX/EDX/ECX at entry from the case,
 * EAX/EDX/ECX after it captured) and the core (with the ECX it reports) against
 * logging stubs for the game callees of the hook under test (dist2d_delta
 * 492a9a and vector_angle 49290c under 49295c, bullet_create 40b43c under
 * 40b5a0, object_init 402188 and def_lookup 40ffc8 under 40a948): ordered calls
 * with their argument slots (and the bridge's slot count), entry EAX/EDX/ECX,
 * the full return pair, ECX, the arena and the globals. The stubs change what
 * the originals read after a call ([W+1A4h] after bullet_create, the counter
 * pointer after object_init and def_lookup); objects may alias the owner's
 * target slot and the counter may point into the new object, so the order of
 * reads and writes is observable. Then installed 31/a7 poison probes; a
 * real-callee pass with the nested hooks in rotating modes (pitch_point over
 * the real dist2d_delta, dist_octagonal, vector_angle and angle_ratio;
 * bullet_charge over the real bullet_create graph and rocket_init over the real
 * object_init and def_lookup, both on the arena of bullet_ctor_cases.h with its
 * 49630c record and sound stubs); and the compare cases of compare_probes.h for
 * proj_record, pitch_point and damage_words. Identifiers use the prefix
 * wl_/WL_. */
typedef struct wl_record { uint32_t id,slots,args[4],regs[3],eax,edx; } wl_record;
enum { WL_SIZE=0x1000, WL_P=0x40, WL_Q=0x60, WL_OBJ=0x100, WL_OBJ2=0x180, WL_OWNER=0x200, WL_ALIAS=0x34e,
    WL_DEFS=0x400, WL_SHAPES=0x500, WL_COUNTER=0x600, WL_COUNTER2=0x604, WL_MAX=8, WL_IDS=5, WL_GLOBALS=20,
    WL_STATE=WL_SIZE+WL_GLOBALS };
#define WL_B ((uint32_t)(uintptr_t)wl_arena)
#define WL_BIT(i) (1u<<(i))
static BYTE wl_arena[WL_SIZE],wl_before[WL_SIZE],wl_after[WL_SIZE];
static wl_record wl_log[WL_MAX];
static unsigned wl_calls;
static int wl_perturb;
static uint32_t wl_seed,wl_stream;
__attribute__((used)) static uint32_t wl_ecx;
/* 0 dist2d_delta 492a9a, 1 vector_angle 49290c, 2 bullet_create 40b43c,
 * 3 object_init 402188, 4 def_lookup 40ffc8; the slots each original call
 * site pushes. */
static const uint32_t wl_addresses[WL_IDS]={0x492a9a,0x49290c,0x40b43c,0x402188,0x40ffc8};
static const unsigned wl_counts[WL_IDS]={3,2,4,2,2};
static uint32_t wl_next(void){
    uint32_t z=wl_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *wl_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int wl_in(uint32_t a,uint32_t n){return a-WL_B<WL_SIZE&&WL_SIZE-(a-WL_B)>=n;}
__attribute__((used,noinline)) static uint64_t __cdecl wl_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(wl_calls==WL_MAX){fail("weapon_leaves call overflow");ExitProcess(1);}
    wl_record *v=wl_log+wl_calls++;memset(v,0,sizeof(*v));v->id=id;
    v->slots=weapons_bridge_slots(a,wl_counts[id]);
    for(unsigned k=0;k<wl_counts[id];++k)v->args[k]=a[k];
    for(unsigned j=0;j<3;++j)v->regs[j]=regs[j];
    uint32_t eax=wl_next(),edx=wl_next(),pick=wl_next();wl_ecx=wl_next();
    switch(id){
    case 2: /* bullet_create: an object in the arena (one aliases the owner's
             * target slot: O+56h = W+1A4h); the target may change. */
        eax=WL_B+(pick%3==0?WL_OBJ:pick%3==1?WL_OBJ2:WL_ALIAS+(pick>>2)%5u-2u);
        if(wl_perturb&&(pick>>5)%2&&wl_in(a[3]+0x1a4u,4))put32(wl_host(a[3]+0x1a4u),wl_next());
        break;
    case 3: case 4: /* object_init, def_lookup: the counter pointer may move. */
        if(wl_perturb&&pick%3==0)put32(mapped+0x2cbee4,WL_B+((pick>>4)&1?WL_COUNTER2:WL_COUNTER));
        break;
    default:break;
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define WL_STUB(n) __attribute__((naked)) static void wl_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _wl_stub_impl\n\taddl $24,%esp\n\tmovl _wl_ecx,%ecx\n\tret\n\t");}
WL_STUB(0) WL_STUB(1) WL_STUB(2) WL_STUB(3) WL_STUB(4)
#undef WL_STUB
static void *wl_stubs[WL_IDS]={wl_stub_0,wl_stub_1,wl_stub_2,wl_stub_3,wl_stub_4};
/* Direct callees of each hook (bit i = wl_addresses[i]). */
static uint32_t wl_which(unsigned index){
    if(index==ES2_HOOK_INDEX_pitch_point)return WL_BIT(0)|WL_BIT(1);
    if(index==ES2_HOOK_INDEX_bullet_charge)return WL_BIT(2);
    if(index==ES2_HOOK_INDEX_rocket_init)return WL_BIT(3)|WL_BIT(4);
    return 0;
}
static int wl_patch(BYTE saved[WL_IDS][5],int active[WL_IDS],uint32_t which,int on){
    for(unsigned i=0;i<WL_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(wl_addresses[i]-0x400000u,wl_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(wl_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
/* The damage words, the projectile definition and shape tables, the PROJ
 * lookup table and the counter pointer. */
static const struct { uint32_t rva,size; } wl_ranges[]={{0xa95f2,4},{0xa96d0,4},{0xa96d8,4},{0xa98fc,4},{0x2cbee4,4}};
static void wl_globals(BYTE *out,int restore){
    for(unsigned i=0;i<sizeof(wl_ranges)/sizeof(wl_ranges[0]);++i){
        if(restore)memcpy(mapped+wl_ranges[i].rva,out,wl_ranges[i].size);else memcpy(out,mapped+wl_ranges[i].rva,wl_ranges[i].size);
        out+=wl_ranges[i].size;
    }
}
static int wl_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<WL_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
static void wl_reset(void){wl_calls=0;wl_stream=wl_seed;}
/* A coordinate: boundaries, small, or anything. */
static uint32_t wl_coordinate(void){
    static const uint32_t edges[]={0,1,0xffffffffu,0x7fffffffu,0x80000000u,0x8000u,0xffff8000u,0x10000u};
    unsigned k=random32()%4;return k==0?edges[random32()%8]:k==1?(uint32_t)((int32_t)(random32()%200001u)-100000):random32();
}
/* valid: pitch_point's points keep vector_angle in its domain (the real
 * passes and the compare cases). */
static void wl_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int valid){
    const uint32_t b=WL_B;
    wl_seed=random32();random_bytes(wl_arena,WL_SIZE);memset(a,0,24);wl_perturb=1;
    for(unsigned i=0;i<3;++i)r[i]=random32();
    put32(mapped+0x2cbee4,b+WL_COUNTER);
    if(index==ES2_HOOK_INDEX_proj_record){
        static const uint32_t indices[]={0,1,2,0x7fff,0x8000,0xffff,0xfffe,0x10002u,0xffff7fffu,0x12340005u};
        a[0]=n<1024?indices[n%10]:random32()%2?indices[random32()%10]:random32();
        put32(mapped+0xa98fc,n%7==0?0:n%7==1?0xfffffff0u:random32());
    } else if(index==ES2_HOOK_INDEX_damage_words){
        static const uint32_t words[]={0,1,0xffff,0x8000,0x10000u,0xffffffffu};
        a[0]=random32()%3?random32():words[random32()%6];a[1]=random32()%3?random32():words[random32()%6];
    } else if(index==ES2_HOOK_INDEX_pitch_point){
        /* P and Q apart, the same point, or overlapping by one or two dwords. */
        unsigned layout=random32()%8;
        a[0]=b+WL_P;a[1]=layout==0?a[0]:layout==1?a[0]+4u:layout==2?a[0]+8u:layout==3?a[0]-4u:b+WL_Q;
        for(unsigned tries=0;;++tries){
            for(unsigned j=0;j<3;++j){put32(wl_host(a[0])+4*j,wl_coordinate());if(a[1]!=a[0])put32(wl_host(a[1])+4*j,wl_coordinate());}
            if(!valid)break;
            BYTE *p=wl_host(a[0]),*q=wl_host(a[1]);
            uint32_t dx=get32(p)-get32(q),dy=get32(p+4)-get32(q+4),dz=get32(p+8)-get32(q+8),d=es2_distance2(dx,dy);
            if(es2_angle_ratio_valid(d||dz?d:1u,dz))break;
            if(tries>64){a[1]=a[0];break;}
        }
    } else if(index==ES2_HOOK_INDEX_bullet_charge){
        static const uint32_t types[]={9,0x10009u,0xffff0009u,8,10,0x90000u,0x0109u};
        a[0]=random32()%3?types[random32()%7]:random32();a[1]=random32();a[2]=random32();
        a[3]=random32()%4?b+WL_OWNER:b+WL_OWNER+(random32()%9u)*4u;a[4]=random32();
    } else {
        /* rocket_init: definition s16(n) in -4..12 of 14-byte records, its
         * shape index in -4..12; the counter in its slot or inside O. */
        static const uint32_t into[]={0,4,0x40,0x4c,0x4e,0x52,0x54,0x56,0x58};
        a[0]=b+(random32()%2?WL_OBJ:WL_OBJ2);
        a[1]=(uint32_t)((int32_t)(random32()%17u)-4)&0xffffu;if(random32()%2)a[1]|=random32()&0xffff0000u;
        for(int k=-4;k<=12;++k)put16(wl_arena+WL_DEFS+0x38+14*k,(uint16_t)((int32_t)(random32()%17u)-4));
        put32(mapped+0xa96d0,b+WL_DEFS+0x38);put32(mapped+0xa96d8,b+WL_SHAPES+0x10);
        if(random32()%4==0)put32(mapped+0x2cbee4,a[0]+into[random32()%9]);
    }
}
/* Calls target with five argument slots and EAX/EDX/ECX = r; out receives
 * EAX, EDX and ECX after the call. */
__attribute__((naked,noinline)) static void wl_invoke(void *target,const uint32_t *a,const uint32_t *r,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl 12(%ebp),%esi\n\tpushl 16(%esi)\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* The core with the graph of the adapter (observer = the hook's counters)
 * and the ECX the original returns. */
static uint64_t wl_core(unsigned index,const uint32_t *a,const uint32_t *r,uint32_t *ecx){
    const uint32_t image=(uint32_t)(uintptr_t)mapped;es2_weapon_graph g=weapons_graph(mapped,NULL,index);
    *ecx=r[2];
    if(index==ES2_HOOK_INDEX_proj_record)return es2_proj_record_registers(&math_native_memory,image,a[0],r);
    if(index==ES2_HOOK_INDEX_damage_words)return es2_damage_words_registers(&math_native_memory,image,a[0],a[1],r);
    if(index==ES2_HOOK_INDEX_pitch_point)return es2_pitch_point_registers(&math_native_memory,&g,a[0],a[1],r,ecx);
    if(index==ES2_HOOK_INDEX_bullet_charge)return es2_bullet_charge_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],a[4],r,ecx);
    return es2_rocket_init_registers(&math_native_memory,&g,a[0],a[1],r,ecx);
}
static int wl_report(const char *what,unsigned n,const uint32_t want[3],const uint32_t got[3],unsigned calls,const wl_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x calls %u/%u\n",current_function,n,what,
        want[0],got[0],want[1],got[1],want[2],got[2],calls,wl_calls);
    for(unsigned j=0;j<calls&&j<wl_calls&&expected;++j)if(memcmp(expected+j,wl_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(wl_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<WL_SIZE;++j)if(wl_after[j]!=wl_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,wl_after[j],wl_arena[j]);break;}
    return 1;
}
static unsigned wl_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<wl_calls;++j)if(c->event==ES2_COLLIDE_CALL&&wl_addresses[wl_log[j].id]==c->key)++hits;
    return hits;
}
/* check_ecx: the core's ECX against the original's (not in the installed
 * pass, where the replaced hook stands in for the original and the
 * framework returns the entry ECX). */
static int wl_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r,int check_ecx){
    const es2_hook_descriptor *d=function_hook_descriptor(index);
    BYTE before[WL_GLOBALS],after[WL_GLOBALS],actual[WL_GLOBALS];wl_record expected[WL_MAX];uint32_t want[3],got[3];
    memcpy(wl_before,wl_arena,WL_SIZE);wl_globals(before,0);
    wl_reset();wl_invoke(mapped+d->rva,a,r,want);unsigned calls=wl_calls;memcpy(expected,wl_log,calls*sizeof(*expected));
    memcpy(wl_after,wl_arena,WL_SIZE);wl_globals(after,0);
    memcpy(wl_arena,wl_before,WL_SIZE);wl_globals(before,1);wl_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[4];
    if(count>4)return fail("weapon_leaves counter table too long");
    for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    const uint64_t pair=wl_core(index,a,r,&got[2]);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);wl_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=wl_expected_hits(c+i))return fail("weapon_leaves callee counter delta");
    if(want[0]!=got[0]||want[1]!=got[1]||(check_ecx&&want[2]!=got[2])||calls!=wl_calls||memcmp(expected,wl_log,calls*sizeof(*expected))||
       memcmp(wl_after,wl_arena,WL_SIZE)||wl_globals_differ(after,actual))
        return wl_report("direct",n,want,got,calls,expected);
    wl_globals(before,1);
    return 0;
}
static int wl_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],want[3]={0},got[3]={0},pattern=poison?0xa7a7a7a7u:0x31313131u;wl_build(index,n,a,r,0);
        BYTE before[WL_GLOBALS],after[WL_GLOBALS],actual[WL_GLOBALS],expected_fpu[108],actual_fpu[108];wl_record expected[WL_MAX];
        memcpy(wl_before,wl_arena,WL_SIZE);wl_globals(before,0);
        wl_reset();r3_poison_call(mapped+d->rva,a,pattern,want);es2_hook_fpu_save(expected_fpu);
        unsigned calls=wl_calls;memcpy(expected,wl_log,calls*sizeof(*expected));memcpy(wl_after,wl_arena,WL_SIZE);wl_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("weapon_leaves poison install");
        memcpy(wl_arena,wl_before,WL_SIZE);wl_globals(before,1);wl_reset();r3_poison_call(mapped+d->rva,a,pattern,got);
        es2_hook_fpu_save(actual_fpu);wl_globals(actual,0);
        if(want[0]!=got[0]||want[1]!=got[1]||calls!=wl_calls||memcmp(expected,wl_log,calls*sizeof(*expected))||memcmp(wl_after,wl_arena,WL_SIZE)||
           wl_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=wl_report(fast?"installed fast":"installed preserving",n,want,got,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("weapon_leaves poison uninstall");wl_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, pair/trace/arena/globals\n",d->name,total);
    return status;
}
/* Real callees: the hook under test original (modes 0-2) or replaced (3-5)
 * against a run without it, the nested hooks in rotating
 * original/replaced/compare modes (compare falls back to replaced where
 * refused). pitch_point: the real distance and angle chain on wl_arena.
 * bullet_charge and rocket_init: bullet_ctor_cases.h's real arena (pool,
 * definitions, lookup records, RNG; shapes 0), its 49630c record stub and,
 * for bullet_create, its sound stub; the trace of those stubs, the pair,
 * the arena, both global sets and FS:0 are compared. */
enum { WL_NESTED=7 };
static unsigned wl_nested(unsigned index,unsigned nested[WL_NESTED]){
    unsigned count=0;
#define WL_N(name) nested[count++]=ES2_HOOK_INDEX_##name
    if(index==ES2_HOOK_INDEX_pitch_point){WL_N(dist2d_delta);WL_N(dist_octagonal);WL_N(vector_angle);WL_N(angle_ratio);}
    else if(index==ES2_HOOK_INDEX_bullet_charge){WL_N(bullet_create);WL_N(pool_alloc);WL_N(bullet_init);WL_N(rng_next);
        WL_N(object_init);WL_N(def_lookup);WL_N(bullet_def);}
    else {WL_N(object_init);WL_N(def_lookup);}
#undef WL_N
    return count;
}
/* bullet_charge on bcc's arena: type 9 in every other case (its planted
 * lookup record (2, t) follows the type), the charge random. rocket_init:
 * the projectile tables point at bcc's definitions and (zero) shapes; in
 * every other case the planted record becomes (0, n). */
static void wl_real_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    if(index==ES2_HOOK_INDEX_pitch_point){wl_build(index,n,a,r,1);return;}
    if(index==ES2_HOOK_INDEX_bullet_charge){
        bcc_build(ES2_HOOK_INDEX_bullet_create,n,a,r,1);
        if(n&1){const uint16_t old=(uint16_t)a[0];a[0]=(a[0]&0xffff0000u)|9u;
            for(unsigned k=0;k<BCC_RECS;++k){BYTE *e=bcc_host(BCC_RECORD(k));if(get16(e)==2&&get16(e+2)==old)put16(e+2,9);}}
        a[4]=random32();
        return;
    }
    bcc_build(ES2_HOOK_INDEX_bullet_init,n,a,r,1);
    put32(mapped+0xa96d0,BCC_BASE+BCC_DEFS);put32(mapped+0xa96d8,BCC_BASE+BCC_SHAPES);
    if(n&1)for(unsigned k=0;k<BCC_RECS;++k){BYTE *e=bcc_host(BCC_RECORD(k));if(get16(e)==2&&get16(e+2)==(uint16_t)a[1])put16(e,0);}
}
static int wl_real_pass(unsigned index){
    unsigned nested[WL_NESTED];const unsigned count=wl_nested(index,nested);const int pitch=index==ES2_HOOK_INDEX_pitch_point;
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[WL_NESTED+1]={0};
    BYTE saved[BCC_IDS][5];int active[BCC_IDS];uint32_t head,head_after;
    if(!pitch&&bcc_patch(saved,active,index==ES2_HOOK_INDEX_bullet_charge?BCC_BIT(0)|BCC_BIT(6):BCC_BIT(0),1))return 1;
    static bcc_record expected[BCC_MAX];
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],want[2],got[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;wl_real_build(index,n,a,r);
        BYTE wl_before_globals[WL_GLOBALS],wl_after_globals[WL_GLOBALS],wl_actual_globals[WL_GLOBALS];
        BYTE before[BCC_GLOBALS],after[BCC_GLOBALS],actual[BCC_GLOBALS];
        BYTE *arena=pitch?wl_arena:bcc_arena,*pre=pitch?wl_before:bcc_before,*post=pitch?wl_after:bcc_after;const size_t size=pitch?WL_SIZE:BCC_SIZE;
        memcpy(pre,arena,size);wl_globals(wl_before_globals,0);if(!pitch)bcc_globals(before,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head));
        es2_hook hk[WL_NESTED+1];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("weapon_leaves nested install");}
        bcc_reset();r3_poison_call(mapped+d->rva,a,pattern,want);
        const unsigned calls=bcc_calls;if(!pitch)memcpy(expected,bcc_log,calls*sizeof(*expected));
        memcpy(post,arena,size);wl_globals(wl_after_globals,0);if(!pitch)bcc_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+WL_NESTED,d,&o,function_hook_extension(index)))return fail("weapon_leaves caller install");
        memcpy(arena,pre,size);wl_globals(wl_before_globals,1);if(!pitch)bcc_globals(before,1);bcc_reset();
        r3_poison_call(mapped+d->rva,a,pattern,got);wl_globals(wl_actual_globals,0);if(!pitch)bcc_globals(actual,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
        if(head_after!=head||want[0]!=got[0]||want[1]!=got[1]||memcmp(post,arena,size)||wl_globals_differ(wl_after_globals,wl_actual_globals)||
           (!pitch&&(calls!=bcc_calls||memcmp(expected,bcc_log,calls*sizeof(*expected))||bcc_globals_differ(after,actual)))){
            fprintf(stderr,"FAIL: %s case %u (real mode=%u) pair %08x%08x/%08x%08x calls %u/%u head %08x/%08x\n",d->name,n,mode,
                want[1],want[0],got[1],got[0],calls,bcc_calls,head,head_after);
            for(size_t j=0;j<size;++j)if(post[j]!=arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",(unsigned)j,post[j],arena[j]);break;}
            status=1;
        }
        for(unsigned j=0;j<=WL_NESTED;++j){if(j<count||j==WL_NESTED)entered[j]+=hk[j].calls;
            if(hk[j].mismatches){fprintf(stderr,"FAIL: %s case %u (real mode=%u) compare mismatch in %s\n",d->name,n,mode,hk[j].descriptor->name);status=1;}
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("weapon_leaves nested uninstall");}
        wl_globals(wl_before_globals,1);if(!pitch)bcc_globals(before,1);
        ++total;
    }
    if(!pitch){if(bcc_patch(saved,active,0,0))return 1;bcc_real=0;}
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&!entered[WL_NESTED]){fprintf(stderr,"FAIL: %s never entered\n",d->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, hook original/replaced, 31/a7 stack, pair, memory, globals, FS:0%s\n",
        d->name,total,count,pitch?"":", stub trace");
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
/* Compare cases (compare_probes.h): proj_record, pitch_point (real
 * callees, valid angle inputs) and damage_words; the state is the arena
 * and the globals. */
static void wl_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){wl_build(index,n,a,r,1);wl_perturb=0;}
static void wl_compare_state(BYTE *s,int restore){
    if(restore){memcpy(wl_arena,s,WL_SIZE);wl_globals(s+WL_SIZE,1);}else{memcpy(s,wl_arena,WL_SIZE);wl_globals(s+WL_SIZE,0);}
}
static int weapon_leaves_cases(unsigned index,unsigned limit){
    if(limit&&compare_probe_installed(index))return compare_probe_run(index,limit,wl_compare_build,wl_compare_state,WL_STATE);
    current_function=function_hook_descriptor(index)->name;
    uint32_t which=wl_which(index);
    BYTE saved[WL_IDS][5],old_globals[WL_GLOBALS];int active[WL_IDS];
    wl_globals(old_globals,0);
    if(wl_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_WEAPON_LEAVES_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];wl_build(index,n,a,r,0);status=wl_compare(index,n,a,r,!limit);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=wl_installed_probes(index,64);
    if(wl_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    wl_globals(old_globals,1);
    if(!limit&&!status&&wl_which(index))status=wl_real_pass(index);
    wl_globals(old_globals,1);
    if(!limit&&!status&&compare_probe_capable(index))status=compare_probe_run(index,0,wl_compare_build,wl_compare_state,WL_STATE);
    wl_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair and ECX, ordered calls with entry registers and slots, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
