/* Actor-list pass (vehicle.md, "Actor-list pass"): actor_pass 4123ac,
 * contact_expire 41251c, contact_scan 4128f8, contact_share 412704. Direct
 * cases: every game callee of the hook under test is a logging stub
 * (timer_word_step, rng_bounded, the two helpers below actor_pass,
 * dist2d_point, sight_check, dist_gate, heading_point, trigger_fire,
 * contact_share; the actors' virtuals +3Ch/+44h/+48h through a generated
 * vtable) that records its ordered arguments and entry registers, returns
 * distances around every threshold and predicates of every flavour, and changes
 * the list, its count, groups, sides, slot words, contact tables, flags and
 * triggers between calls, so every re-read is checked. The arena and the
 * globals (actor list and count, RNG state 4d2599h, time scale) are compared
 * byte for byte. Then installed 31/a7 poison cases, and a real-callee pass: the
 * nested hooks (timers, rng_bounded, dist2d_point, dist_gate, heading_point,
 * sight_check and the group's own helpers) in rotating modes; trigger_fire,
 * terrain_line and the virtuals (+24h for sight_check) stay stubs. actor_pass's
 * EDX high word is uninitialized stack in the original (dead at its caller) and
 * masked. */
typedef struct ac_record { uint32_t id,args[4],regs[3],input[6],eax,edx,slots; } ac_record;
enum { AC_SIZE=0x1400, AC_ACTORS=0x0000, AC_STRIDE=0x200, AC_NACTORS=8, AC_GROUPS=0x1000, AC_NGROUPS=4,
    AC_MEMBERS=0x1200, AC_LIST=0x1300, AC_NLIST=10, AC_VTABLE=0x1380, AC_RECORD=0x13e0, AC_MAX=512, AC_IDS=15,
    AC_GLOBALS=8+0x72+2 };
#define AC_BASE ((uint32_t)(uintptr_t)ac_arena)
static BYTE ac_arena[AC_SIZE],ac_before[AC_SIZE],ac_after[AC_SIZE];
static ac_record ac_log[AC_MAX];
static unsigned ac_calls;
static int ac_real;
static uint32_t ac_seed,ac_stream;
__attribute__((used)) static uint32_t ac_ecx;
/* ids: 0 timer_word_step, 1 rng_bounded, 2 contact_expire, 3 contact_scan,
 * 4 dist2d_point, 5 sight_check, 6 dist_gate, 7 heading_point,
 * 8 trigger_fire, 9 contact_share, 10 V3c, 11 V44, 12 V48,
 * 13 terrain_line (below a real sight_check), 14 V24 (likewise). */
static const uint32_t ac_addresses[AC_IDS]={0x467a48,0x492f24,0x41251c,0x4128f8,0x4928d0,0x412608,0x49288c,0x492934,
    0x423430,0x412704,0,0,0,0x4280f4,0};
static const unsigned ac_slots[AC_IDS]={0,0,0,0,0,0,0,0,0,0,0x3c,0x44,0x48,0,0x24};
static const unsigned ac_counts[AC_IDS]={1,2,1,1,2,2,2,2,1,2,1,2,2,4,1};
static uint32_t ac_next(void){
    uint32_t z=ac_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *ac_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int ac_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static uint32_t ac_actor(unsigned k){return AC_BASE+AC_ACTORS+AC_STRIDE*k;}
static uint32_t ac_group(unsigned k){return AC_BASE+AC_GROUPS+0x7au*k;}
static uint32_t ac_pick(const uint32_t *values,unsigned count,uint32_t pick){return values[pick%count];}
/* Slot words: mostly 0..15, sometimes any index that keeps +C2h+slot (and
 * +132h+slot of a real sight_check) inside the arena. */
static uint16_t ac_slot(uint32_t pick){
    if(ac_real||pick%16)return (uint16_t)(pick/16%16);
    return (uint16_t)((int32_t)(pick/16%0x17e)-0x40);
}
static BYTE ac_byte(uint32_t pick){static const uint32_t v[]={1,0x80,0xff,2};return (BYTE)(pick%4?0:v[pick/4%4]);}
/* Predicates: zero, one, a nonzero high word over AX = 0, or anything. */
static uint32_t ac_flag(uint32_t pick){
    switch(pick%7){case 0:case 1:return 0;case 2:case 3:return 1;case 4:return 0x10000u|(ac_next()&0xffff0000u);
        case 5:return (ac_next()&0xffff0000u)|1u;default:return ac_next();}
}
/* Distances on and around 50000, 80000, 100000, 140000, 200000, small,
 * negative or anything. */
static uint32_t ac_distance(uint32_t pick){
    static const uint32_t edges[]={50000,80000,100000,140000,200000};
    switch(pick%8){case 0:case 1:case 2:return edges[pick/8%5]+(uint32_t)((int)(pick/64%3)-1);
        case 3:case 4:return ac_next()%250000u;case 5:return 0u-ac_next()%20000u;case 6:return ac_next()%60000u;default:return ac_next();}
}
/* A stub's change to the shared state (direct pass only). */
static void ac_mutate(uint32_t pick){
    BYTE *o=ac_host(ac_actor(ac_next()%AC_NACTORS));
    switch(pick%9){
    case 0:put16(mapped+0xa9afe,(uint16_t)(ac_next()%(AC_NLIST+3u)));break;
    case 1:put32(ac_arena+AC_LIST+4*(ac_next()%AC_NLIST),ac_next()%5?ac_actor(ac_next()%AC_NACTORS):0);break;
    case 2:{static const unsigned f[]={0x95,0x96,0x99,0x9e,0xa2,0xa3,0xa4};o[f[ac_next()%7]]=(BYTE)(ac_next()%2?0:1);break;}
    case 3:{BYTE *g=ac_host(ac_group(ac_next()%AC_NGROUPS));if(ac_next()%2)put32(g+0x14,ac_next()%3?0:1);else g[0x12]=(BYTE)(ac_next()%3);break;}
    case 4:put32(o+0x45,ac_group(ac_next()%AC_NGROUPS));break;
    case 5:put16(o+0x4b,ac_slot(ac_next()));break;
    case 6:o[0xc2+ac_next()%16]=(BYTE)(ac_next()%2);break;
    case 7:put32(o+0x1b2,ac_next()%2?0:ac_next());break;
    default:put32(o+0x26,ac_next());break;
    }
}
/* Snapshot probe: a pure stub answers from the case seed and its inputs only
 * (terrain_line's vectors by content, stack addresses as 0), leaves the stream
 * and the log alone and changes nothing, so the original and the replacement
 * phase of a compare see the same answers. */
static int ac_pure;
static uint32_t ac_pure_mix(uint32_t z,uint32_t v){z^=v;z*=0x9e3779b1u;return z^(z>>15);}
static uint32_t ac_pure_seed(unsigned id,const uint32_t *a){
    uint32_t z=ac_pure_mix(ac_seed,id+1u);
    for(unsigned j=0;j<ac_counts[id];++j)z=ac_pure_mix(z,ac_stack(a[j])?0:a[j]);
    if(id==13)for(unsigned j=0;j<3;++j)z=ac_pure_mix(ac_pure_mix(z,get32(ac_host(a[1])+4*j)),get32(ac_host(a[2])+4*j));
    return z;
}
__attribute__((used,noinline)) static uint64_t __cdecl ac_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    ac_record pure_record;const uint32_t saved_stream=ac_stream;
    if(ac_pure)ac_stream=ac_pure_seed(id,a);
    else if(ac_calls==AC_MAX){fail("actor pass synthetic call overflow");ExitProcess(1);}
    ac_record *v=ac_pure?&pure_record:ac_log+ac_calls++;memset(v,0,sizeof(*v));v->id=id;v->slots=weapons_bridge_slots(a,ac_counts[id]);
    for(unsigned j=0;j<ac_counts[id];++j)v->args[j]=ac_stack(a[j])?0:a[j];
    /* Entry registers in the direct pass only: under nested replaced hooks
     * ECX after a call is outside the contract (vehicle.md). */
    if(!ac_real)for(unsigned j=0;j<3;++j)v->regs[j]=ac_stack(regs[j])?0:regs[j];
    uint32_t eax=ac_next(),edx=ac_next(),pick=ac_next();ac_ecx=ac_next();
    switch(id){
    case 0: /* timer_word_step: AX = 0 about half the time; may write its word */
        if(pick%2)eax&=0xffff0000u;
        if(!ac_real&&pick%5==0)put16(ac_host(a[0]+1),(uint16_t)ac_next());
        break;
    case 4: case 6: eax=ac_distance(pick>>3);break;
    case 5: case 11: eax=ac_flag(pick>>3);break;
    case 13: /* terrain_line: the two vectors by content, AX = 0 or not */
        for(unsigned j=0;j<3;++j){v->input[j]=get32(ac_host(a[1])+4*j);v->input[3+j]=get32(ac_host(a[2])+4*j);}
        eax=ac_flag(pick>>3);break;
    case 14: eax=pick%3?0:AC_BASE+AC_RECORD;break;
    default: break;
    }
    if(!ac_real&&id!=13&&id!=14&&pick%3==0)ac_mutate(ac_next());
    if(ac_pure)ac_stream=saved_stream;
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define AC_STUB(n) __attribute__((naked)) static void ac_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _ac_stub_impl\n\taddl $24,%esp\n\tmovl _ac_ecx,%ecx\n\tret\n\t");}
AC_STUB(0) AC_STUB(1) AC_STUB(2) AC_STUB(3) AC_STUB(4) AC_STUB(5) AC_STUB(6) AC_STUB(7) AC_STUB(8) AC_STUB(9)
AC_STUB(10) AC_STUB(11) AC_STUB(12) AC_STUB(13) AC_STUB(14)
#undef AC_STUB
static void *ac_stubs[AC_IDS]={ac_stub_0,ac_stub_1,ac_stub_2,ac_stub_3,ac_stub_4,ac_stub_5,ac_stub_6,ac_stub_7,ac_stub_8,
    ac_stub_9,ac_stub_10,ac_stub_11,ac_stub_12,ac_stub_13,ac_stub_14};
/* Globals: the actor list and its count, the RNG state 4d2599h, the time
 * scale 4d3b64h. */
static const struct { uint32_t rva,size; } ac_ranges[]={{0xa9af8,8},{0xd2599,0x72},{0xd3b64,2}};
static void ac_globals(BYTE *out,int restore){
    unsigned at=0;
    for(unsigned i=0;i<sizeof(ac_ranges)/sizeof(ac_ranges[0]);++i){
        if(restore)memcpy(mapped+ac_ranges[i].rva,out+at,ac_ranges[i].size);else memcpy(out+at,mapped+ac_ranges[i].rva,ac_ranges[i].size);
        at+=ac_ranges[i].size;
    }
}
static void ac_reset(void){ac_calls=0;ac_stream=ac_seed;}
static void ac_build(unsigned index,uint32_t a[6],uint32_t r[3],int real){
    uint32_t b=AC_BASE;ac_seed=random32();random_bytes(ac_arena,AC_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    for(unsigned id=0;id<AC_IDS;++id)if(ac_slots[id])put32(ac_arena+AC_VTABLE+ac_slots[id],(uint32_t)(uintptr_t)ac_stubs[id]);
    put32(ac_arena+AC_RECORD+0x1c,real?random32()%2000:random32());
    static const uint32_t sides[]={0,1,0,1,1,0,2,0xff};
    for(unsigned k=0;k<AC_NGROUPS;++k){BYTE *g=ac_arena+AC_GROUPS+0x7a*k;
        put32(g+0xc,b+AC_MEMBERS+0x20*k);g[0x12]=(BYTE)ac_pick(sides,8,random32());
        put32(g+0x14,random32()%8?0:random32()%2?1:random32()|1u);
        for(unsigned j=0;j<8;++j)put32(ac_arena+AC_MEMBERS+0x20*k+4*j,ac_actor(random32()%AC_NACTORS));
    }
    /* Real passes keep the groups apart in the pending/side space more often. */
    if(real&&random32()%2){ac_arena[AC_GROUPS+0x12]=0;ac_arena[AC_GROUPS+0x7a+0x12]=1;}
    for(unsigned k=0;k<AC_NACTORS;++k){BYTE *o=ac_arena+AC_ACTORS+AC_STRIDE*k;
        put32(o,b+AC_VTABLE);put32(o+0x45,ac_group(random32()%AC_NGROUPS));
        put16(o+0x4b,real?(uint16_t)k:ac_slot(random32()));
        static const unsigned flags[]={0x95,0x96,0x99,0x9e,0xa2,0xa3,0xa4};
        for(unsigned j=0;j<7;++j)o[flags[j]]=ac_byte(random32());
        for(unsigned j=0;j<16;++j)o[0xc2+j]=(BYTE)(random32()%2?0:random32()%4?1:random32()|1u);
        put32(o+0x1b2,random32()%3?0:random32());
        if(real){
            for(unsigned j=0;j<2;++j)put32(o+0x26+4*j,random32()%240001u-120000u);
            put32(o+0x2e,random32()%2001u-1000u);
            put16(o+0x1e3,(uint16_t)(random32()%2?0:1+random32()%300));
            put16(o+0x1e6,(uint16_t)(random32()%400));
            for(unsigned j=0;j<16;++j)o[0x132+j]=(BYTE)(random32()%2);
        }
    }
    for(unsigned k=0;k<AC_NLIST;++k)put32(ac_arena+AC_LIST+4*k,random32()%12?ac_actor(random32()%AC_NACTORS):0);
    put32(ac_arena+AC_LIST+4*AC_NLIST,0);
    put32(mapped+0xa9af8,b+AC_LIST);
    put16(mapped+0xa9afe,(uint16_t)(random32()%16?random32()%(AC_NLIST+2u):random32()%2?0:0x8000u|random32()));
    if(real){
        BYTE *state=mapped+0xd2599;random_bytes(state,112);state[112]=(BYTE)(random32()%56);state[113]=(BYTE)(random32()%56);
        put16(mapped+0xd3b64,(uint16_t)(1+random32()%300));
    }
    switch(index){
    case ES2_HOOK_INDEX_contact_expire: case ES2_HOOK_INDEX_contact_scan: a[0]=ac_actor(random32()%AC_NACTORS);break;
    case ES2_HOOK_INDEX_contact_share: a[0]=ac_actor(random32()%AC_NACTORS);a[1]=ac_actor(random32()%AC_NACTORS);break;
    default: break;
    }
    /* contact_scan's side-0 argument and side-1 actors are the common case. */
    if(index==ES2_HOOK_INDEX_contact_scan&&random32()%2){
        uint32_t own=get32(ac_host(a[0]+0x45));ac_host(own)[0x12]=0;
        for(unsigned k=0;k<AC_NGROUPS;++k)if(ac_group(k)!=own&&random32()%2)ac_arena[AC_GROUPS+0x7a*k+0x12]=1;
    }
}
/* EDX's high word of actor_pass is uninitialized stack (vehicle.md). */
static uint64_t ac_mask(unsigned index){return index==ES2_HOOK_INDEX_actor_pass?0x0000ffffffffffffull:~0ull;}
static int ac_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const ac_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,ac_calls);
    for(unsigned j=0;j<calls&&j<ac_calls&&expected;++j)if(memcmp(expected+j,ac_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(ac_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<AC_SIZE;++j)if(ac_after[j]!=ac_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,ac_after[j],ac_arena[j]);break;}
    return 1;
}
static void ac_global_diff(const BYTE *expected,const BYTE *actual){
    for(unsigned j=0;j<AC_GLOBALS;++j)if(expected[j]!=actual[j]){fprintf(stderr," global byte %x %02x/%02x\n",j,expected[j],actual[j]);break;}
}
static unsigned ac_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<ac_calls;++j){unsigned id=ac_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?ac_slots[id]==c->key:!ac_slots[id]&&ac_addresses[id]==c->key)++hits;}
    return hits;
}
static int ac_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    static BYTE before[AC_GLOBALS],after[AC_GLOBALS],actual[AC_GLOBALS];static ac_record expected[AC_MAX];
    memcpy(ac_before,ac_arena,AC_SIZE);ac_globals(before,0);
    ac_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=ac_calls;memcpy(expected,ac_log,calls*sizeof(*expected));
    memcpy(ac_after,ac_arena,AC_SIZE);ac_globals(after,0);
    memcpy(ac_arena,ac_before,AC_SIZE);ac_globals(before,1);ac_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);ac_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=ac_expected_hits(c+i))return fail("actor pass callee counter delta");
    if(((pair[0]^pair[1])&ac_mask(index))||calls!=ac_calls||memcmp(expected,ac_log,calls*sizeof(*expected))||
       memcmp(ac_after,ac_arena,AC_SIZE)||memcmp(after,actual,AC_GLOBALS)){
        ac_global_diff(after,actual);return ac_report("direct",n,pair,calls,expected);
    }
    return 0;
}
/* Callee stubs patched per hook (bit = id): direct and real passes. */
static unsigned ac_which(unsigned index,int real){
    switch(index){
    case ES2_HOOK_INDEX_actor_pass:return real?0x2100u:0x000fu;
    case ES2_HOOK_INDEX_contact_expire:return real?0x2000u:0x0030u;
    case ES2_HOOK_INDEX_contact_scan:return real?0x2100u:0x03e0u;
    default:return real?0x0100u:0x0140u;
    }
}
static int ac_patch(BYTE saved[AC_IDS][5],int active[AC_IDS],unsigned which,int on){
    for(unsigned i=0;i<AC_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!ac_addresses[i])continue;if(actor_patch(ac_addresses[i]-0x400000u,ac_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(ac_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static int ac_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;ac_build(index,a,r,0);
        static BYTE before[AC_GLOBALS],after[AC_GLOBALS],actual[AC_GLOBALS];BYTE expected_fpu[108],actual_fpu[108];static ac_record expected[AC_MAX];
        memcpy(ac_before,ac_arena,AC_SIZE);ac_globals(before,0);
        ac_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=ac_calls;memcpy(expected,ac_log,calls*sizeof(*expected));memcpy(ac_after,ac_arena,AC_SIZE);ac_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("actor pass poison install");
        memcpy(ac_arena,ac_before,AC_SIZE);ac_globals(before,1);ac_reset();sg_poison_call(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);ac_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(((pair[0]^pair[1])&ac_mask(index))||calls!=ac_calls||memcmp(expected,ac_log,calls*sizeof(*expected))||memcmp(ac_after,ac_arena,AC_SIZE)||
           memcmp(after,actual,AC_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1){
            ac_global_diff(after,actual);status=ac_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        }
        if(!es2_hook_uninstall(&h))return fail("actor pass poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state\n",d->name,total);
    return status;
}
/* Real callees with the nested hooks in rotating original/replaced/compare
 * modes (compare falls back to replaced where refused) and a patterned
 * stack; trigger_fire, terrain_line and the virtuals stay stubs and are
 * compared in order by arguments (and terrain_line's vectors by content). */
static int ac_real_pass(unsigned index){
    static const unsigned sets[4][9]={
        {ES2_HOOK_INDEX_timer_word_step,ES2_HOOK_INDEX_rng_bounded,ES2_HOOK_INDEX_contact_expire,ES2_HOOK_INDEX_contact_scan,
         ES2_HOOK_INDEX_contact_share,ES2_HOOK_INDEX_sight_check,ES2_HOOK_INDEX_dist2d_point,ES2_HOOK_INDEX_dist_gate,ES2_HOOK_INDEX_heading_point},
        {ES2_HOOK_INDEX_dist2d_point,ES2_HOOK_INDEX_sight_check,ES2_HOOK_INDEX_rng_bounded},
        {ES2_HOOK_INDEX_dist_gate,ES2_HOOK_INDEX_sight_check,ES2_HOOK_INDEX_heading_point,ES2_HOOK_INDEX_contact_share,ES2_HOOK_INDEX_rng_bounded},
        {ES2_HOOK_INDEX_dist_gate}};
    static const unsigned sizes[4]={9,3,5,1};
    unsigned set=index==ES2_HOOK_INDEX_actor_pass?0:index==ES2_HOOK_INDEX_contact_expire?1:index==ES2_HOOK_INDEX_contact_scan?2:3;
    unsigned nested_count=sizes[set];const unsigned *nested=sets[set];
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0,stubbed=0;uint64_t entered[9]={0};
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    BYTE saved[AC_IDS][5];int active[AC_IDS];if(ac_patch(saved,active,ac_which(index,1),1))return 1;
    ac_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<1024&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;ac_build(index,a,r,1);
        static BYTE before[AC_GLOBALS],after[AC_GLOBALS],actual[AC_GLOBALS];static ac_record expected[AC_MAX];
        memcpy(ac_before,ac_arena,AC_SIZE);ac_globals(before,0);
        es2_hook hk[10];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<nested_count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("actor pass nested install");}
        ac_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        unsigned calls=ac_calls;memcpy(expected,ac_log,calls*sizeof(*expected));stubbed+=calls;
        memcpy(ac_after,ac_arena,AC_SIZE);ac_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+nested_count,d,&o,function_hook_extension(index)))return fail("actor pass caller install");
        memcpy(ac_arena,ac_before,AC_SIZE);ac_globals(before,1);ac_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);ac_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(((pair[0]^pair[1])&ac_mask(index))||calls!=ac_calls||memcmp(expected,ac_log,calls*sizeof(*expected))||
           memcmp(ac_after,ac_arena,AC_SIZE)||memcmp(after,actual,AC_GLOBALS)){
            ac_global_diff(after,actual);fprintf(stderr,"real mode=%u\n",mode);status=ac_report("real",n,pair,calls,expected);
        }
        for(unsigned j=0;j<=nested_count;++j){if(j<nested_count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("actor pass nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("actor pass nested uninstall");}
        ac_globals(before,1);++total;
    }
    ac_real=0;
    if(ac_patch(saved,active,0,0))return 1;
    for(unsigned j=0;j<nested_count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    for(unsigned i=0;i<count&&!status;++i)if(c[i].count==counts[i]){fprintf(stderr,"FAIL: %s real pass never called %s\n",d->name,c[i].name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, %u stub calls, 31/a7 stack, pair, trace and memory\n",
        d->name,total,nested_count,stubbed);
        for(unsigned j=0;j<nested_count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
/* Pilot: contact_scan in compare mode with its snapshot regions (the arena as
 * the block of a synthetic building pool [4a95a0], header 10 used objects of
 * 1F8h bytes; the MECH and FLYER pools absent) and RNG stream A. Real callees:
 * sight_check, contact_share, dist_gate, heading_point and rng_bounded as
 * original code (own run) or as installed hooks in their compare-pass modes
 * (installed pass); terrain_line and the virtuals are pure stubs; trigger_fire
 * is the real hook (replaced) on a latched trigger record at +1B2h in a quarter
 * of the cases, so it is entered (a guard) and changes nothing; mech_contact is
 * installed as the second guard (V48 stays a stub). Own run: per case the
 * unhooked original against the call through the compare hook (pair, arena,
 * globals; one call, no bypass, compared or guarded, never refused, exhausted
 * or mismatched), then the same with a skewed replacement that flips actor A's
 * byte +95h after the exact one: every compared call counts one mismatch and
 * the caller keeps the original's state. Installed pass: the trampoline (nested
 * hooks in their modes) against the hooked call, no mismatch. */
static BYTE ac_pool_header[18];
static uint64_t __cdecl ac_skewed_scan(const uint32_t *a,void *u,const uint32_t *r){
    const uint64_t result=function_hook_extension(ES2_HOOK_INDEX_contact_scan)->replacement_context(a,u,r);
    ac_host(a[0])[0x95]^=1u;return result;
}
static int ac_snapshot_probe(unsigned index,unsigned limit){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    if(x->compare_unsupported)return fail("contact_scan snapshot declaration");
    static const unsigned guards[2]={ES2_HOOK_INDEX_mech_contact,ES2_HOOK_INDEX_trigger_fire};
    es2_hook guard_hooks[2];memset(guard_hooks,0,sizeof(guard_hooks));
    es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
    for(unsigned j=0;j<2;++j)if(!rng_hooks[guards[j]].installed&&
        !weapons_install_hook(guard_hooks+j,function_hook_descriptor(guards[j]),&o,function_hook_extension(guards[j])))return fail("contact_scan guard install");
    BYTE saved[AC_IDS][5],pools[12];int active[AC_IDS];if(ac_patch(saved,active,0x2000u,1))return 1;
    memcpy(pools,mapped+0xa95a0,4);memcpy(pools+4,mapped+0xa9b7a,4);memcpy(pools+8,mapped+0xa9db9,4);
    memset(ac_pool_header,0,sizeof(ac_pool_header));put16(ac_pool_header+2,10);put16(ac_pool_header+4,AC_STRIDE-8);
    put32(ac_pool_header+6,AC_BASE);put32(mapped+0xa95a0,(uint32_t)(uintptr_t)ac_pool_header);
    memset(mapped+0xa9b7a,0,4);memset(mapped+0xa9db9,0,4);
    es2_hook_extension skewed=*x;skewed.replacement_context=ac_skewed_scan;
    if(!es2_hook_bind_snapshot(&skewed,&actor_pass_contact_scan_snapshot))return fail("contact_scan skewed binding");
    ac_real=1;ac_pure=1;int status=0;unsigned cases=limit?limit:256,compared=0,guarded=0;uint64_t skew_mismatches=0;
    const uint64_t installed_mismatches=limit?rng_hooks[index].mismatches:0;
    for(unsigned k=0;k<(limit?1u:2u)&&!status;++k)for(unsigned n=0;n<cases&&!status;++n){
        uint32_t a[6],r[3];current_case=n;current_case_valid=1;ac_build(index,a,r,1);
        for(unsigned i=0;i<AC_NACTORS;++i){BYTE *o=ac_arena+AC_ACTORS+AC_STRIDE*i;put32(o+0x1b2,n%4?0:AC_BASE+AC_RECORD);
            /* Every eighth case: everyone close, active and in mutual contact,
             * the argument's group on side 0, the others on side 1, so the
             * mutual path reaches trigger_fire. */
            if(n%8==0){for(unsigned j=0;j<3;++j)put32(o+0x26+4*j,random32()%20001u-10000u);
                memset(o+0xc2,1,16);o[0xa2]=o[0xa4]=o[0x99]=0;}}
        if(n%8==0)for(unsigned g=0;g<AC_NGROUPS;++g){BYTE *grp=ac_arena+AC_GROUPS+0x7a*g;put32(grp+0x14,0);
            grp[0x12]=(BYTE)(ac_group(g)!=get32(ac_host(a[0]+0x45)));}
        ac_arena[AC_RECORD+0x0a]=1;
        static BYTE before[AC_GLOBALS],after[AC_GLOBALS],actual[AC_GLOBALS];
        memcpy(ac_before,ac_arena,AC_SIZE);ac_globals(before,0);
        const uint64_t expected=compare_probe_call(limit?rng_hooks[index].trampoline:mapped+d->rva,index,a,r);
        memcpy(ac_after,ac_arena,AC_SIZE);ac_globals(after,0);memcpy(ac_arena,ac_before,AC_SIZE);ac_globals(before,1);
        es2_hook h={0};es2_hook *used=limit?&rng_hooks[index]:&h;const es2_hook counts=*used;
        if(!limit){o.mode=ES2_HOOK_COMPARE;if(!weapons_install_hook(&h,d,&o,k?&skewed:x))return fail("contact_scan compare install");}
        if(!used->snapshot||!used->snapshot_memory)return fail("contact_scan installed without its snapshot binding");
        const uint64_t got=compare_probe_call(mapped+d->rva,index,a,r);ac_globals(actual,0);
        const uint64_t c=used->snapshot_compared-counts.snapshot_compared,gd=used->snapshot_guarded-counts.snapshot_guarded;
        compared+=(unsigned)c;guarded+=(unsigned)gd;
        int bad=got!=expected||memcmp(ac_after,ac_arena,AC_SIZE)||memcmp(after,actual,AC_GLOBALS)||used->calls-counts.calls!=1||
            c+gd!=1||used->skipped-counts.skipped!=gd||used->bypassed!=counts.bypassed||
            used->mismatches-counts.mismatches!=(k?c:0)||used->snapshot_refused!=counts.snapshot_refused;
        skew_mismatches+=k?used->mismatches-counts.mismatches:0;
        if(!limit&&!es2_hook_uninstall(&h))return fail("contact_scan compare uninstall");
        if(bad){ac_global_diff(after,actual);
            fprintf(stderr,"FAIL: contact_scan snapshot case %u (%s) pair %016llx/%016llx compared %llu guarded %llu refused %llu mismatches %llu\n",
                n,limit?"installed pass":k?"skewed":"exact",(unsigned long long)expected,(unsigned long long)got,(unsigned long long)c,
                (unsigned long long)gd,(unsigned long long)(used->snapshot_refused-counts.snapshot_refused),
                (unsigned long long)(used->mismatches-counts.mismatches));
            status=ac_report("snapshot",n,(uint64_t[2]){expected,got},0,NULL);}
        ac_globals(before,1);
    }
    ac_real=0;ac_pure=0;current_case_valid=0;
    memcpy(mapped+0xa95a0,pools,4);memcpy(mapped+0xa9b7a,pools+4,4);memcpy(mapped+0xa9db9,pools+8,4);
    (void)es2_hook_bind_snapshot(&skewed,NULL);
    if(ac_patch(saved,active,0,0))return 1;
    for(unsigned j=0;j<2;++j)if(guard_hooks[j].descriptor&&!es2_hook_uninstall(guard_hooks+j))return fail("contact_scan guard uninstall");
    if(!status&&(!compared||!guarded))status=fail("contact_scan snapshot probe never compared or never guarded");
    if(!status&&limit&&rng_hooks[index].mismatches!=installed_mismatches)status=fail("contact_scan installed compare mismatch");
    if(!status)printf("PASS: contact_scan %u snapshot compare cases (%s): %u compared, %u guarded, skewed replacement %llu mismatches; original pair and state kept\n",
        limit?cases:2*cases,limit?"installed pass":"own run",compared,guarded,(unsigned long long)skew_mismatches);
    return status;
}
static int actor_pass_cases(unsigned index,unsigned limit){
    if(index==ES2_HOOK_INDEX_contact_scan&&limit&&compare_probe_installed(index))return ac_snapshot_probe(index,limit);
    BYTE saved[AC_IDS][5],old_arena[AC_SIZE];int active[AC_IDS];static BYTE old_globals[AC_GLOBALS];
    ac_globals(old_globals,0);memcpy(old_arena,ac_arena,AC_SIZE);ac_real=0;
    if(ac_patch(saved,active,ac_which(index,0),1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_ACTOR_PASS_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];ac_build(index,a,r,0);status=ac_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=ac_installed_probes(index,64);
    if(ac_patch(saved,active,0,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=ac_real_pass(index);
    if(!limit&&!status&&index==ES2_HOOK_INDEX_contact_scan)status=ac_snapshot_probe(index,0);
    ac_globals(old_globals,1);memcpy(ac_arena,old_arena,AC_SIZE);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
