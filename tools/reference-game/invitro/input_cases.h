/* Input children: fire_selected 410dbc, fire_line 415608, aim_point 41b728 and
 * leg_input 4160dc (input.md). Direct cases run the original body and the core
 * against logging stubs for every game callee (direct calls patched in the
 * private image, the weapon/vehicle/target virtuals through an arena vtable):
 * ordered arguments, entry EAX/EDX/ECX, payloads by content; values in the
 * worker's stack reservation are masked. Stubs change the state the originals
 * reread after a call (target pointer, heading, set point, lever, matrix flag).
 * Then installed 31/a7 poison probes and, for the three hooks with hooked
 * callees, a real-callee pass with the nested hooks in rotating modes
 * (fire_selected and dist_gate under fire_line; dist_gate, euler_matrix and
 * transform_point under aim_point; multiply_q8 and multiply_q10 under
 * leg_input). Only 40e4dc, 416a04 and the virtuals stay stubs there; their
 * inputs are compared. */
typedef struct ip_record { uint32_t id,args[6],regs[3],input[8],eax,edx; } ip_record;
enum { IP_SIZE=0x900, IP_W=0x40, IP_LIST=0x90, IP_WEAPONS=0x100, IP_DEFS=0x280, IP_VTABLE=0x300, IP_P=0x380, IP_T=0x6c0,
    IP_T2=0x700, IP_R=0x740, IP_OUT=0x780, IP_P3=0x7a0, IP_P4=0x7a4, IP_NODE=0x7c0, IP_CFG=0x800, IP_MAX=64, IP_IDS=14,
    IP_REAL_MAX=4096, IP_GLOBALS=0x150 };
#define IP_BASE ((uint32_t)(uintptr_t)ip_arena)
#define IP_LIST_PTR (IP_BASE+IP_LIST+16u)
static BYTE ip_arena[IP_SIZE],ip_before[IP_SIZE],ip_after[IP_SIZE];
static ip_record ip_log[IP_MAX];
static uint32_t ip_real_log[IP_REAL_MAX];
static unsigned ip_calls,ip_real_count;
static int ip_real;
static uint32_t ip_seed,ip_stream;
__attribute__((used)) static uint32_t ip_ecx;
/* 0 410dbc, 1 49288c, 2 40e4dc, 3 47ebb8, 4 48043c, 5 47e0a0, 6 47e0b0,
 * 7 416a04; virtual slots 8 +2Ch, 9 +30h, 10 +28h, 11 +60h (weapons),
 * 12 +3Ch (vehicle twist), 13 +24h (target node). */
static const uint32_t ip_addresses[IP_IDS]={0x410dbc,0x49288c,0x40e4dc,0x47ebb8,0x48043c,0x47e0a0,0x47e0b0,0x416a04,0,0,0,0,0,0};
static const uint32_t ip_slots[IP_IDS]={0,0,0,0,0,0,0,0,0x2c,0x30,0x28,0x60,0x3c,0x24};
static const unsigned ip_counts[IP_IDS]={3,2,6,2,3,2,2,4,1,2,3,2,1,1};
static uint32_t ip_next(void){
    uint32_t z=ip_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *ip_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int ip_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
/* Target distances around the thresholds: 30,000 (41b754) and the line
 * count d >> 12 clamped to 1..40 (415661, 415674). */
static uint32_t ip_distance(uint32_t pick){
    switch(pick%8){
    case 0:return ip_next();
    case 1:return 29990u+pick/8%21u;
    case 2:return pick/8%0x30000u;
    case 3:return 0u-pick/8%0x3000u;
    case 4:return 0x27000u+pick/8%0x3000u;
    case 5:return pick/8%0x1800u;
    case 6:return pick&8?0x7fffffffu:0x80000000u;
    default:return pick/8%60000u;
    }
}
static uint16_t ip_word(uint32_t pick,const uint16_t *edges,unsigned count){
    return pick%3?edges[pick/3%count]:(uint16_t)(pick>>12);
}
static void ip_target_switch(uint32_t pick,int allow_null){
    BYTE *p=ip_arena+IP_P;
    if(pick%16==3)put32(p+0x1a4,IP_BASE+IP_T2);
    else if(allow_null&&pick%16==7)put32(p+0x1a4,0);
}
static uint64_t ip_real_stub(unsigned id,const uint32_t *a,uint32_t eax,uint32_t edx,uint32_t pick){
    if(ip_real_count+16>IP_REAL_MAX){fail("input real stub overflow");ExitProcess(1);}
    uint32_t *o=ip_real_log+ip_real_count;o[0]=id;unsigned k=1;
    for(unsigned j=0;j<ip_counts[id];++j)o[k++]=ip_stack(a[j])?0:a[j];
    switch(id){
    case 2:if(pick%2)for(unsigned j=0;j<12;j+=4)put32(ip_host(a[2])+j,ip_next());break;
    case 7:eax=ip_next();break;
    case 8:case 9:eax=pick%4==0?eax&0xffff0000u:eax|1u;break;
    case 11:if(pick%3==0)eax=(eax&0xffff0000u)|3u;break;
    case 12:o[k++]=get16(ip_arena+IP_P+0x10);if(pick%4==0)put16(ip_arena+IP_P+0x10,(uint16_t)ip_next());break;
    case 13:eax=pick%3==0?0:IP_BASE+IP_NODE;ip_target_switch(pick,0);
        if(pick%8==5)put16(ip_arena+IP_T+0x32,0);break;
    case 10:break;
    default:fail("input real stub id");ExitProcess(1);
    }
    ip_real_count+=k;return (uint64_t)edx<<32|eax;
}
__attribute__((used,noinline)) static uint64_t __cdecl ip_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    uint32_t eax=ip_next(),edx=ip_next(),pick=ip_next();ip_ecx=ip_next();
    if(ip_real)return ip_real_stub(id,a,eax,edx,pick);
    if(ip_calls==IP_MAX){fail("input synthetic call overflow");ExitProcess(1);}
    ip_record *v=ip_log+ip_calls++;memset(v,0,sizeof(*v));v->id=id;
    for(unsigned k=0;k<ip_counts[id];++k)v->args[k]=ip_stack(a[k])?0:a[k];
    for(unsigned j=0;j<3;++j)v->regs[j]=ip_stack(regs[j])?0:regs[j];
    BYTE *p=ip_arena+IP_P;
    switch(id){
    case 0: /* 410dbc: fired (EAX tested whole) or not; may retarget. */
        eax=pick%4==0?0:pick%4==1?eax:1u;ip_target_switch(pick>>4,1);
        break;
    case 1: /* dist_gate: both points by content. */
        for(unsigned j=0;j<3;++j){v->input[j]=get32(ip_host(a[0])+4*j);v->input[3+j]=get32(ip_host(a[1])+4*j);}
        eax=ip_distance(pick);ip_target_switch(pick>>5,0);
        break;
    case 2:if(pick%2)for(unsigned j=0;j<12;j+=4)put32(ip_host(a[2])+j,ip_next());break;
    case 3: /* Euler: angles in, nine coefficient words out. */
        for(unsigned j=0;j<3;++j)v->input[j]=get16(ip_host(a[0])+2*j);
        if(pick%3)for(unsigned j=0;j<9;++j)put16(ip_host(a[1])+2*j,(uint16_t)ip_next());
        break;
    case 4: /* transform_point: point and matrix by content, point out. */
        for(unsigned j=0;j<3;++j)v->input[j]=get32(ip_host(a[0])+4*j);
        for(unsigned j=0;j<5;++j)v->input[3+j]=get32(ip_host(a[1])+4*j);
        for(unsigned j=0;j<12;j+=4)put32(ip_host(a[2])+j,ip_next());
        break;
    case 5: /* multiply_q8: zero or small low word; may move the set point or lever. */
        eax=pick%3==0?eax&0xffff0000u:pick%3==1?(eax&0xffff0000u)|(uint16_t)(ip_next()%1400u-700u):eax;
        if(pick%8==1)put16(p+0x290,(uint16_t)(ip_next()%2400u-1200u));
        if(pick%16==2)put16(mapped+0x9a06e,(uint16_t)(ip_next()%3u-1u));
        break;
    case 8:case 9: /* readiness guards: AX zero or not, any high word */
        eax=pick%4==0?eax&0xffff0000u:eax|1u;
        if(pick%8==3)ip_arena[IP_W+0x18]=(BYTE)ip_next();
        break;
    case 11:if(pick%3==0)eax=(eax&0xffff0000u)|3u;break;
    case 12: /* twist: the heading word is reread after the call */
        v->input[0]=get16(p+0x10);if(pick%4==0)put16(p+0x10,(uint16_t)ip_next());
        if(pick%4==1)put32(p+0x26,ip_next());
        break;
    case 13: /* target node or none; the target pointer and matrix flag are reread */
        eax=pick%3==0?0:IP_BASE+IP_NODE;ip_target_switch(pick>>2,0);
        if(pick%8==5)put16(ip_arena+IP_T+0x32,0);
        break;
    default:break;
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define IP_STUB(n) __attribute__((naked)) static void ip_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _ip_stub_impl\n\taddl $24,%esp\n\tmovl _ip_ecx,%ecx\n\tret\n\t");}
IP_STUB(0) IP_STUB(1) IP_STUB(2) IP_STUB(3) IP_STUB(4) IP_STUB(5) IP_STUB(6) IP_STUB(7) IP_STUB(8) IP_STUB(9)
IP_STUB(10) IP_STUB(11) IP_STUB(12) IP_STUB(13)
#undef IP_STUB
static void *ip_stubs[IP_IDS]={ip_stub_0,ip_stub_1,ip_stub_2,ip_stub_3,ip_stub_4,ip_stub_5,ip_stub_6,ip_stub_7,ip_stub_8,
    ip_stub_9,ip_stub_10,ip_stub_11,ip_stub_12,ip_stub_13};
/* Fire line and count, fire flag words, mode words written by +60h, view
 * word, throttle lever. */
static void ip_globals(BYTE *out,int restore){
    static const unsigned offsets[]={0xa9b88,0xa9e52,0xa9e58,0xd2526,0xd24f0,0x9a06e},sizes[]={0x144,2,2,4,2,2};
    for(unsigned i=0;i<6;++i){if(restore)memcpy(mapped+offsets[i],out,sizes[i]);else memcpy(out,mapped+offsets[i],sizes[i]);out+=sizes[i];}
}
static void ip_reset(void){ip_calls=ip_real_count=0;ip_stream=ip_seed;}
/* Weapon system W: list [W+4] with entries -4..15 (null 1/8), selection
 * s8 W+1Dh in -3..9 (and 80h), four weapons with a definition offset s8
 * def+16h in -3..3, so the partner index stays inside the list. */
static void ip_weapons(unsigned n){
    uint32_t b=IP_BASE;BYTE *w=ip_arena+IP_W;
    put32(w+4,IP_LIST_PTR);
    static const BYTE selections[]={0,1,2,3,9,0xff,0xfe,0x80,5,7};
    BYTE sel=n<64?selections[n%10]:random32()%6?(BYTE)(random32()%10):selections[random32()%10];
    w[0x1d]=sel;
    for(int i=-4;i<16;++i)put32(ip_host(IP_LIST_PTR+4u*(uint32_t)i),random32()%8?b+IP_WEAPONS+0x60u*(random32()%4):0);
    if((int8_t)sel>=0)put32(ip_host(IP_LIST_PTR+4u*(uint32_t)(int8_t)sel),b+IP_WEAPONS+0x60u*(random32()%4));
    for(unsigned k=0;k<4;++k){
        BYTE *s=ip_arena+IP_WEAPONS+0x60*k;put32(s,b+IP_VTABLE);put32(s+0x18,b+IP_DEFS+0x20u*(random32()%4));
        s[0x4b]=(BYTE)(random32()%2?0:1+random32()%255);
        ip_arena[IP_DEFS+0x20*k+0x16]=(BYTE)(random32()%7-3);
    }
    static const uint16_t flags[]={0,0,0,1,2,0x8000};
    put16(mapped+0xa9e52,flags[random32()%6]);put16(mapped+0xa9e58,random32()%2?1:flags[random32()%6]);
}
static void ip_target(BYTE *t){
    put32(t,IP_BASE+IP_VTABLE);
    put16(t+0x32,random32()%2?0:(uint16_t)(1+random32()%0xffff));
}
static void ip_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    uint32_t b=IP_BASE;ip_seed=random32();random_bytes(ip_arena,IP_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    for(unsigned k=0;k<IP_IDS;++k)if(ip_slots[k])put32(ip_arena+IP_VTABLE+ip_slots[k],(uint32_t)(uintptr_t)ip_stubs[k]);
    random_bytes(mapped+0xa9b88,0x144);put32(mapped+0xd2526,random32());
    BYTE *p=ip_arena+IP_P;put32(p,b+IP_VTABLE);
    ip_target(ip_arena+IP_T);ip_target(ip_arena+IP_T2);
    static const uint16_t views[]={0,0,1,0xffff};
    put16(mapped+0xd24f0,random32()%3?views[random32()%4]:(uint16_t)random32());
    static const uint16_t levers[]={0,0,1,0xffff,0x8000,0x7fff};
    put16(mapped+0x9a06e,random32()%3?levers[random32()%6]:(uint16_t)random32());
    if(index==ES2_HOOK_INDEX_fire_selected||index==ES2_HOOK_INDEX_fire_line){
        ip_weapons(n);put32(p+0x202,b+IP_W);
        if(index==ES2_HOOK_INDEX_fire_selected){a[0]=b+IP_W;a[1]=b+IP_R;a[2]=b+IP_P;return;}
        put32(p+0x1a4,random32()%4?b+IP_T:0);
        a[0]=b+IP_P;a[1]=b+IP_R;return;
    }
    if(index==ES2_HOOK_INDEX_aim_point){
        put32(p+0x30b,random32()%3?random32()|1u:0);put32(p+0x1a4,b+IP_T);
        a[0]=b+IP_P;a[1]=b+IP_OUT;a[2]=random32()%3?b+IP_P3:0;a[3]=random32()%2?b+IP_P4:random32();
        return;
    }
    /* leg_input */
    static const uint16_t throttles[]={0x100,0x100+49,0x100+50,0x100-50,0x100-49,0x3c,0x3d,0xffc4,0xffc3,0x8000,0,0x7fff,0x200,0xff00};
    static const uint16_t sets[]={0,1,0xffff,0x400,0xfc00,0x3ff,0xfc01,0x8000,0x7fff,100,0xff9c};
    put32(p+0x1f2,b+IP_CFG);put16(p+0x290,ip_word(random32(),sets,11));
    put16(ip_arena+IP_CFG+4,(uint16_t)(random32()%4?random32()%0x800:random32()));
    put16(ip_arena+IP_CFG+6,(uint16_t)(random32()%4?random32()%0x800:random32()));
    a[0]=b+IP_P;a[1]=random32();a[2]=(random32()&0xffff0000u)|ip_word(random32(),throttles,14);
    if(random32()%4==0)a[2]=(random32()&0xffff0000u)|(uint16_t)(random32()%700u-350u);
    (void)n;
}
static int ip_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const ip_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,ip_calls);
    for(unsigned j=0;j<calls&&j<ip_calls&&expected;++j)if(memcmp(expected+j,ip_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(ip_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<IP_SIZE;++j)if(ip_after[j]!=ip_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,ip_after[j],ip_arena[j]);break;}
    return 1;
}
static unsigned ip_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<ip_calls;++j){unsigned id=ip_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?ip_slots[id]&&ip_slots[id]==c->key:!ip_slots[id]&&ip_addresses[id]==c->key)++hits;}
    return hits;
}
static int ip_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[IP_GLOBALS],after[IP_GLOBALS],actual[IP_GLOBALS];static ip_record expected[IP_MAX];
    memcpy(ip_before,ip_arena,IP_SIZE);ip_globals(before,0);
    ip_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=ip_calls;memcpy(expected,ip_log,calls*sizeof(*expected));
    memcpy(ip_after,ip_arena,IP_SIZE);ip_globals(after,0);
    memcpy(ip_arena,ip_before,IP_SIZE);ip_globals(before,1);ip_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);ip_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=ip_expected_hits(c+i))return fail("input callee counter delta");
    if(pair[0]!=pair[1]||calls!=ip_calls||memcmp(expected,ip_log,calls*sizeof(*expected))||memcmp(ip_after,ip_arena,IP_SIZE)||
       memcmp(after,actual,IP_GLOBALS)){
        for(unsigned j=0;j<IP_GLOBALS;++j)if(after[j]!=actual[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,after[j],actual[j]);break;}
        return ip_report("direct",n,pair,calls,expected);
    }
    return 0;
}
static int ip_patch(BYTE saved[IP_IDS][5],int active[IP_IDS],unsigned which,int on){
    for(unsigned i=0;i<IP_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!ip_addresses[i])continue;if(actor_patch(ip_addresses[i]-0x400000u,ip_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(ip_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
static int ip_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;ip_build(index,n,a,r);
        BYTE before[IP_GLOBALS],after[IP_GLOBALS],actual[IP_GLOBALS],expected_fpu[108],actual_fpu[108];static ip_record expected[IP_MAX];
        memcpy(ip_before,ip_arena,IP_SIZE);ip_globals(before,0);
        ip_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=ip_calls;memcpy(expected,ip_log,calls*sizeof(*expected));memcpy(ip_after,ip_arena,IP_SIZE);ip_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("input poison install");
        memcpy(ip_arena,ip_before,IP_SIZE);ip_globals(before,1);ip_reset();sg_poison_call(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);ip_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=ip_calls||memcmp(expected,ip_log,calls*sizeof(*expected))||memcmp(ip_after,ip_arena,IP_SIZE)||
           memcmp(after,actual,IP_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=ip_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("input poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state\n",d->name,total);
    return status;
}
/* transform_point is a legacy (EAX-only) hook: in compare mode its EDX is
 * the dispatcher's, a stack address that depends on the caller's depth.
 * aim_point returns that EDX on its node path; the pair is dead at both
 * callers (input.md), so only there may EDX differ, and only between two
 * stack-window values. */
static int ip_same_pair(unsigned index,uint64_t x,uint64_t y){
    if(x==y)return 1;
    return index==ES2_HOOK_INDEX_aim_point&&(uint32_t)x==(uint32_t)y&&ip_stack((uint32_t)(x>>32))&&ip_stack((uint32_t)(y>>32));
}
/* Real callees with the nested hooks in rotating original/replaced/compare
 * modes and a patterned stack; the stubs' inputs are compared in order. */
static int ip_real_pass(unsigned index){
    static const unsigned line_nested[]={ES2_HOOK_INDEX_fire_selected,ES2_HOOK_INDEX_dist_gate};
    static const unsigned aim_nested[]={ES2_HOOK_INDEX_dist_gate,ES2_HOOK_INDEX_euler_matrix,ES2_HOOK_INDEX_transform_point};
    static const unsigned leg_nested[]={ES2_HOOK_INDEX_multiply_q8,ES2_HOOK_INDEX_multiply_q10};
    const unsigned *nested;unsigned count,stubs;
    if(index==ES2_HOOK_INDEX_fire_line){nested=line_nested;count=2;stubs=0;}
    else if(index==ES2_HOOK_INDEX_aim_point){nested=aim_nested;count=3;stubs=1u<<2;}
    else {nested=leg_nested;count=2;stubs=1u<<7;}
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[4]={0};
    BYTE saved[IP_IDS][5];int active[IP_IDS];if(ip_patch(saved,active,stubs,1))return 1;
    static uint32_t expected_log[IP_REAL_MAX];
    ip_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<1024&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;ip_build(index,n,a,r);
        BYTE before[IP_GLOBALS],after[IP_GLOBALS],actual[IP_GLOBALS];
        memcpy(ip_before,ip_arena,IP_SIZE);ip_globals(before,0);
        es2_hook hk[4];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("input nested install");}
        ip_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        unsigned logged=ip_real_count;memcpy(expected_log,ip_real_log,4*logged);
        memcpy(ip_after,ip_arena,IP_SIZE);ip_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+count,d,&o,function_hook_extension(index)))return fail("input caller install");
        memcpy(ip_arena,ip_before,IP_SIZE);ip_globals(before,1);ip_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);ip_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(!ip_same_pair(index,pair[0],pair[1])||logged!=ip_real_count||memcmp(expected_log,ip_real_log,4*logged)||
           memcmp(ip_after,ip_arena,IP_SIZE)||memcmp(after,actual,IP_GLOBALS)){
            for(unsigned j=0;j<IP_GLOBALS;++j)if(after[j]!=actual[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,after[j],actual[j]);break;}
            fprintf(stderr,"real mode=%u stub log %u/%u\n",mode,logged,ip_real_count);ip_calls=0;status=ip_report("real",n,pair,0,NULL);
        }
        for(unsigned j=0;j<=count;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("input nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("input nested uninstall");}
        ip_globals(before,1);++total;
    }
    ip_real=0;
    if(ip_patch(saved,active,0,0))return 1;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){
        fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, 31/a7 stack, pair, memory, globals and stub inputs\n",
        d->name,total,count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
static int input_cases(unsigned index,unsigned limit){
    unsigned which=index==ES2_HOOK_INDEX_fire_selected?0:index==ES2_HOOK_INDEX_fire_line?0x3u:
        index==ES2_HOOK_INDEX_aim_point?0x1eu:0xe0u;
    BYTE saved[IP_IDS][5],old_globals[IP_GLOBALS];int active[IP_IDS];
    ip_globals(old_globals,0);if(ip_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_INPUT_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];ip_build(index,n,a,r);status=ip_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=ip_installed_probes(index,64);
    if(ip_patch(saved,active,0,0))return 1;
    current_case_valid=0;
    if(!limit&&!status&&index!=ES2_HOOK_INDEX_fire_selected)status=ip_real_pass(index);
    ip_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, payloads by content, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
