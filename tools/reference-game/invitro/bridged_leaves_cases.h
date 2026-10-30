/* bridged_leaves group: the original leaves own code still reached through the
 * guest bridge (docs/reverse-engineering/hud.md, "the bridged leaves"). An 8
 * KiB arena holds the weapon W and target T, the list L (table around +900h)
 * and object X, the player P, the cockpit H with the EventPort E and its two
 * vtables, the ThrottleGauge G and the camera C; the image globals 49b088 (H),
 * 49f8ec (the sound flag sfx_play tests), 4d24e6/4d24ea (player, camera) and
 * 4d2518..4d2521 (the view words) are set per case, compared and restored.
 *
 * Direct cases run the original body (three argument slots, EAX/EDX/ECX
 * at entry from the case, over 2 KiB of zeroed stack so that
 * r_key_toggle's message residue is 0 as in the adapter's zeroed scratch),
 * the adapter (pair) and the core (pair and its ECX) and compare the
 * pair, ECX (core; and the original's when the hook is not installed),
 * the arena, the globals, the callee counter deltas and, for
 * r_key_toggle, the ordered calls. r_key_toggle's callees are logging
 * stubs: the sound row 462830 and the cancel 435ac8 patched into the
 * private image, the EventPort's slot 0 through two arena vtables (a
 * stub may repoint [E] between them or change P+96h, which the original
 * reads before its calls only); a record keeps the slot count, the
 * arguments, the entry EAX/EDX/ECX and, for the slot, the 24 bytes of the
 * message record (the residue included). The constructors run their real
 * callees (pure). Then installed 31/a7 poison probes (r_key_toggle through
 * its entry shim: per-call EBX/ESI/EDI, EBP/ESP relative to the caller's
 * slot, a hash of the 256 bytes below each callee's ESP, the sound stub
 * overwriting its argument slot in one call of four), real-callee passes
 * (r_key_toggle over the real sfx_play with the sound flag clear, sfx_play
 * nested in rotating modes; matrix_ctor over the real 495194/49514f and
 * rotation_ctor over the real 49514f with the nested hooks rotating) and
 * the compare probes of compare_probes.h for the nine hooks that accept
 * compare. Identifiers: blv_/BLV_. */
enum { BLV_SIZE=0x2000, BLV_W=0x100, BLV_T=0x400, BLV_L=0x600, BLV_X=0x640, BLV_TABLE=0x900, BLV_P=0xa00, BLV_P2=0xb00,
    BLV_H=0xc00, BLV_E=0xf00, BLV_VT0=0xf40, BLV_VT1=0xf80, BLV_G=0x1000, BLV_C=0x1200, BLV_IDS=4, BLV_MAX=8,
    BLV_GLOBALS=4+4+8+10 };
#define BLV_BASE ((uint32_t)(uintptr_t)blv_arena)
#define BLV_ADDR(o) (BLV_BASE+(uint32_t)(o))
static BYTE blv_arena[BLV_SIZE],blv_before[BLV_SIZE],blv_after[BLV_SIZE];
typedef struct blv_record { uint32_t id,slots,a[3],regs[3],cs[3],frame[2],window;BYTE message[24]; } blv_record;
static blv_record blv_log[BLV_MAX];
static unsigned blv_calls,blv_track,blv_clobbers,blv_hits[8];
static uint32_t blv_seed,blv_stream;
/* Captured by the stubs at entry: EAX, EDX, ECX, EBX, ESI, EDI, EBP, ESP;
 * the 256 bytes below the entry ESP; the ECX a stub returns. */
__attribute__((used)) static uint32_t blv_cap[8],blv_ecx,blv_origin;
__attribute__((used)) static BYTE blv_window[256];
/* ids 0 sound row 462830, 1 cancel 435ac8 (patched), 2/3 slot 0 of VT0/VT1. */
static const uint32_t blv_addr[BLV_IDS]={0x462830,0x435ac8,0,0};
static const unsigned blv_arity[BLV_IDS]={1,3,2,2};
static uint32_t blv_next(void){
    uint32_t z=blv_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static void blv_reset(void){blv_calls=0;blv_stream=blv_seed;}
static void blv_globals(BYTE *out,int restore){
    static const struct { uint32_t rva,size; } g[]={{0x9b088,4},{0x9f8ec,4},{0xd24e6,8},{0xd2518,10}};
    BYTE *p=out;for(unsigned i=0;i<4;++i){if(restore)memcpy(mapped+g[i].rva,p,g[i].size);else memcpy(p,mapped+g[i].rva,g[i].size);p+=g[i].size;}
}
static uint32_t blv_hash(const BYTE *p,unsigned n){uint32_t h=2166136261u;for(unsigned i=0;i<n;++i)h=(h^p[i])*16777619u;return h;}
__attribute__((used,noinline)) static uint64_t __cdecl blv_log_impl(unsigned id){
    if(blv_calls==BLV_MAX){fail("bridged_leaves synthetic call overflow");ExitProcess(1);}
    uint32_t *a=(uint32_t *)(uintptr_t)(blv_cap[7]+4u);
    blv_record *v=blv_log+blv_calls++;memset(v,0,sizeof(*v));v->id=id;v->slots=weapons_bridge_slots(a,blv_arity[id]);
    for(unsigned j=0;j<blv_arity[id];++j)v->a[j]=a[j];
    for(unsigned j=0;j<3;++j)v->regs[j]=blv_cap[j];
    if(id>=2)memcpy(v->message,(const BYTE *)(uintptr_t)a[1],24);
    if(blv_track){for(unsigned j=0;j<3;++j)v->cs[j]=blv_cap[3+j];
        v->frame[0]=blv_cap[6]-blv_origin;v->frame[1]=blv_cap[7]-blv_origin;v->window=blv_hash(blv_window,256);}
    /* The message pointer (slot 2 and EAX of the slot call) is a frame
     * address: relative to the caller's slot through the shim, else
     * masked (the original's frame and the adapter's scratch differ). */
    if(id>=2){v->a[1]=blv_track?v->a[1]-blv_origin:0;v->regs[0]=blv_track?v->regs[0]-blv_origin:0;}
    uint32_t eax=blv_next(),edx=blv_next(),pick=blv_next();blv_ecx=blv_next();
    if(id<2&&pick%4==0)put32(blv_arena+BLV_E,BLV_ADDR(pick>>8&1?BLV_VT1:BLV_VT0));   /* [E] is read after the cancels */
    if(id<2&&pick%7==1)blv_arena[BLV_P+0x96]^=1;                                      /* not reread */
    if(blv_track&&id==0&&blv_next()%4==0){a[0]=blv_next();++blv_clobbers;}             /* the callee changes its slot */
    return (uint64_t)edx<<32|eax;
}
#define BLV_STUB(n) __attribute__((naked)) static void blv_stub_##n(void){__asm__ volatile( \
    "movl %eax,_blv_cap\n\tmovl %edx,_blv_cap+4\n\tmovl %ecx,_blv_cap+8\n\tmovl %ebx,_blv_cap+12\n\t" \
    "movl %esi,_blv_cap+16\n\tmovl %edi,_blv_cap+20\n\tmovl %ebp,_blv_cap+24\n\tmovl %esp,_blv_cap+28\n\t" \
    "leal -256(%esp),%esi\n\tmovl $_blv_window,%edi\n\tmovl $64,%ecx\n\tcld\n\trep movsl\n\t" \
    "movl _blv_cap+16,%esi\n\tmovl _blv_cap+20,%edi\n\t" \
    "pushl $" #n "\n\tcalll _blv_log_impl\n\taddl $4,%esp\n\tmovl _blv_ecx,%ecx\n\tret\n\t");}
BLV_STUB(0) BLV_STUB(1) BLV_STUB(2) BLV_STUB(3)
#undef BLV_STUB
static void *blv_stubs[BLV_IDS]={blv_stub_0,blv_stub_1,blv_stub_2,blv_stub_3};
static int blv_patch(BYTE saved[BLV_IDS][5],int active[BLV_IDS],unsigned which,int on){
    for(unsigned i=0;i<BLV_IDS;++i){
        if(!blv_addr[i]){active[i]=0;continue;}
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(blv_addr[i]-0x400000u,blv_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(blv_addr[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static uint16_t blv_word(const uint16_t *values,unsigned count){return random32()%3?values[random32()%count]:(uint16_t)random32();}
/* One state for hook index (case n): a[0..2] the argument slots, r the
 * entry EAX/EDX/ECX. compare: bounds for the compare probes (the throttle
 * walk stays inside the declared 0C2h bytes). */
static void blv_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int compare){
    static const uint16_t edges[]={0,1,2,0x7fff,0x8000,0xffff,0xfffe};
    blv_seed=random32();blv_stream=blv_seed^n;random_bytes(blv_arena,BLV_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    for(unsigned i=0;i<3;++i)a[i]=random32();
    BYTE *H=blv_arena+BLV_H,*P=blv_arena+BLV_P,*G=blv_arena+BLV_G,*C=blv_arena+BLV_C;
    put32(mapped+0x9b088,BLV_ADDR(BLV_H));put32(mapped+0x9f8ec,0);
    put32(mapped+0xd24ea,BLV_ADDR(BLV_C));put32(mapped+0xd24e6,random32()%2?get32(C+0x32):random32());
    put16(mapped+0xd2518,blv_word(edges,7));
    for(unsigned i=0;i<8;++i)mapped[0xd251a+i]=(BYTE)random32();
    if(index==ES2_HOOK_INDEX_weapon_target_word){
        a[0]=BLV_ADDR(BLV_W);a[1]=n%4==0?0:BLV_ADDR(BLV_T);
        put16(blv_arena+BLV_T+0x1a8,random32()%2?0:blv_word(edges,7));
    } else if(index==ES2_HOOK_INDEX_list_next||index==ES2_HOOK_INDEX_list_prev){
        /* A nonzero count of at most 63 either sign, so every remainder
         * lands within 252 bytes of the table pointer. */
        static const int16_t counts[]={1,2,5,63,-1,-5,-63};
        const int16_t count=random32()%2?counts[random32()%7]:(int16_t)((random32()%2?1:-1)*(int16_t)(1+random32()%63));
        a[0]=BLV_ADDR(BLV_L);a[1]=BLV_ADDR(BLV_X);
        put32(blv_arena+BLV_L,BLV_ADDR(BLV_TABLE));put16(blv_arena+BLV_L+6,(uint16_t)count);
        const int32_t c=count<0?-count:count;
        static const int16_t steps[]={0,-1,1};
        put16(blv_arena+BLV_X+0x4b,random32()%2?(uint16_t)(c*(int32_t)(random32()%3)-1+steps[random32()%3]):blv_word(edges,7));
    } else if(index==ES2_HOOK_INDEX_r_key_toggle){
        a[0]=BLV_ADDR(BLV_P);
        P[0xa3]=(BYTE)(random32()%2?0:random32()%2?1:random32());
        P[0x96]=(BYTE)(random32()%2?0:random32()%2?1:random32());
        put32(H+0x20b,BLV_ADDR(BLV_E));put32(blv_arena+BLV_E,BLV_ADDR(random32()%2?BLV_VT1:BLV_VT0));
        put32(blv_arena+BLV_VT0,(uint32_t)(uintptr_t)blv_stubs[2]);put32(blv_arena+BLV_VT1,(uint32_t)(uintptr_t)blv_stubs[3]);
    } else if(index==ES2_HOOK_INDEX_nav_marker_set){
        put32(H+0x203,BLV_ADDR(random32()%2?BLV_P:BLV_P2));
    } else if(index==ES2_HOOK_INDEX_throttle_handler){
        static const uint32_t limits[]={0,1,2,3,5,8,0xffffffffu,0x80000000u};
        const uint32_t count=random32()%2?limits[random32()%8]:random32()%(compare?40u:60u);
        put32(G+0x68,count);
        a[0]=BLV_ADDR(BLV_G);
        if(random32()%8==0)a[1]=0;
        const uint32_t reach=count>0x7fffffffu?0:count>(compare?39u:59u)?(compare?39u:59u):count;
        /* Entry 20 is the count dword +68h itself: left alone. */
        for(unsigned k=0;k<reach&&0x18u+4u*k<0xb4u;++k)if(k!=20&&random32()%4==0)put32(G+0x18+4*k,a[1]);
        if(reach&&random32()%2){const unsigned k=random32()%reach;if(k!=20)put32(G+0x18+4*k,a[1]);}
        if(random32()%4==0)put32(G+0x18,a[1]);
        G[0xc1]=(BYTE)(random32()%2?0:random32()%2?1:random32());
        static const uint32_t values[]={0,1,0xffffffffu,0x80000000u,0x7fffffffu};
        if(random32()%3==0)a[2]=values[random32()%5];
    } else if(index==ES2_HOOK_INDEX_view_pref_toggle){
        if(random32()%2)put32(C+0x32,get32(mapped+0xd24e6));
    } else if(index==ES2_HOOK_INDEX_matrix_ctor||index==ES2_HOOK_INDEX_rotation_ctor||index==ES2_HOOK_INDEX_rotation_base_ctor){
        if(random32()%2)a[0]=BLV_ADDR(0x1800+(random32()%64));
    }
    blv_stream=blv_seed;   /* the stubs' stream restarts here in both runs */
}
/* Calls target with three argument slots and EAX/EDX/ECX = r over 2 KiB of
 * zeroed stack; out receives EAX, EDX and ECX after the call. */
__attribute__((naked,noinline)) static void blv_invoke(void *target,const uint32_t *a,const uint32_t *r,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $2048,%esp\n\tmovl %esp,%edi\n\txorl %eax,%eax\n\tmovl $512,%ecx\n\tcld\n\trep stosl\n\taddl $2048,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static uint64_t blv_core(unsigned index,const uint32_t *a,const uint32_t *r,uint32_t *ecx){
    const es2_guest_memory *m=&math_native_memory;
    es2_weapon_graph g=weapons_graph(mapped,NULL,index);const uint32_t image=(uint32_t)(uintptr_t)mapped;
    if(index==ES2_HOOK_INDEX_weapon_target_word)return es2_weapon_target_word_registers(m,a[0],a[1],r,ecx);
    if(index==ES2_HOOK_INDEX_list_next)return es2_list_next_registers(m,a[0],a[1],r,ecx);
    if(index==ES2_HOOK_INDEX_list_prev)return es2_list_prev_registers(m,a[0],a[1],r,ecx);
    if(index==ES2_HOOK_INDEX_r_key_toggle){
        BYTE scratch[0x20]={0};
        return es2_r_key_toggle_registers(m,&g,NULL,(uint32_t)(uintptr_t)(scratch+0x18),0,a[0],r,ecx);
    }
    if(index==ES2_HOOK_INDEX_nav_marker_set)return es2_nav_marker_set_registers(m,image,r,ecx);
    if(index==ES2_HOOK_INDEX_throttle_handler)return es2_throttle_handler_registers(m,a[0],a[1],a[2],r,ecx);
    if(index==ES2_HOOK_INDEX_view_pref_toggle)return es2_view_pref_toggle_registers(m,image,r,ecx);
    if(index==ES2_HOOK_INDEX_matrix_ctor)return es2_matrix_ctor_registers(&g,a[0],r,ecx);
    if(index==ES2_HOOK_INDEX_rotation_ctor)return es2_rotation_ctor_registers(&g,a[0],r,ecx);
    return es2_rotation_base_ctor_registers(a[0],r,ecx);
}
/* Expected callee counter delta: r_key_toggle's from the stub log, one per
 * callee for the constructors. */
static uint64_t blv_expected(unsigned index,const callee_counter *c){
    if(index!=ES2_HOOK_INDEX_r_key_toggle)return 1;
    uint64_t want=0;
    for(unsigned j=0;j<blv_calls;++j){const unsigned id=blv_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?id>=2:id<2&&blv_addr[id]==c->key)++want;}
    return want;
}
static int blv_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const blv_record *e,const BYTE *want,const BYTE *got){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,blv_calls);
    for(unsigned j=0;j<calls&&j<blv_calls&&e;++j)if(memcmp(e+j,blv_log+j,sizeof(*e))){
        const uint32_t *x=(const uint32_t*)(e+j),*y=(const uint32_t*)(blv_log+j);
        for(unsigned k=0;k<sizeof(*e)/4;++k)if(x[k]!=y[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,e[j].id,k,x[k],y[k]);break;}
    for(unsigned j=0;j<BLV_SIZE;++j)if(blv_after[j]!=blv_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,blv_after[j],blv_arena[j]);break;}
    for(unsigned j=0;want&&j<BLV_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);break;}
    return 1;
}
static int blv_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[BLV_GLOBALS],after[BLV_GLOBALS],actual[BLV_GLOBALS];static blv_record expected[BLV_MAX];
    uint32_t want[3],ecx=0;
    memcpy(blv_before,blv_arena,BLV_SIZE);blv_globals(before,0);
    blv_reset();blv_invoke(mapped+d->rva,a,r,want);
    const unsigned calls=blv_calls;memcpy(expected,blv_log,calls*sizeof(*expected));memcpy(blv_after,blv_arena,BLV_SIZE);blv_globals(after,0);
    for(unsigned j=0;j<calls;++j)blv_hits[expected[j].id]++;
    memcpy(blv_arena,blv_before,BLV_SIZE);blv_globals(before,1);blv_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[4]={0};
    for(unsigned i=0;i<count&&i<4;++i)counts[i]=c[i].count;
    uint64_t pair[2];pair[0]=(uint64_t)want[1]<<32|want[0];
    pair[1]=x->replacement_context(a,d->user,r);blv_globals(actual,0);
    for(unsigned i=0;i<count&&i<4;++i)if(c[i].count-counts[i]!=blv_expected(index,c+i))return fail("bridged_leaves callee counter delta");
    if(pair[0]!=pair[1]||calls!=blv_calls||memcmp(expected,blv_log,calls*sizeof(*expected))||memcmp(blv_after,blv_arena,BLV_SIZE)||
       memcmp(after,actual,BLV_GLOBALS))
        return blv_report("adapter",n,pair,calls,expected,after,actual);
    memcpy(blv_arena,blv_before,BLV_SIZE);blv_globals(before,1);blv_reset();
    const uint64_t core=blv_core(index,a,r,&ecx);blv_globals(actual,0);
    /* With the hook installed (integration pass) the "original" run is the
       hook, whose ECX is the framework's; ECX is compared otherwise. */
    const int ecx_known=!rng_hooks[index].installed;
    if(core!=pair[0]||(ecx_known&&ecx!=want[2])||calls!=blv_calls||memcmp(expected,blv_log,calls*sizeof(*expected))||
       memcmp(blv_after,blv_arena,BLV_SIZE)||memcmp(after,actual,BLV_GLOBALS)){
        fprintf(stderr,"core ecx %08x/%08x\n",want[2],ecx);
        const uint64_t both[2]={pair[0],core};return blv_report("core",n,both,calls,expected,after,actual);
    }
    blv_globals(before,1);
    return 0;
}
/* Calls target with one slot over a 4-KiB fill, EAX..EDI distinct pattern
 * words; keeps the slot address in blv_origin and returns EDX:EAX in out. */
__attribute__((naked,noinline)) static void blv_track_run(void *target,const uint32_t *a,uint32_t pattern,uint32_t out[2]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 16(%ebp),%eax\n\tmovl $1024,%ecx\n\tcld\n\trep stosl\n\taddl $4092,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl (%esi)\n\tmovl %esp,_blv_origin\n\tmovl 16(%ebp),%eax\n\t"
        "movl %eax,%edx\n\txorl $0x01010101,%edx\n\tmovl %eax,%ecx\n\txorl $0x02020202,%ecx\n\tmovl %eax,%ebx\n\txorl $0x04040404,%ebx\n\t"
        "movl %eax,%esi\n\txorl $0x08080808,%esi\n\tmovl %eax,%edi\n\txorl $0x10101010,%edi\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%ebx\n\tmovl %eax,(%ebx)\n\tmovl %edx,4(%ebx)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* Installed probes. r_key_toggle through its entry shim: the original
 * 41b468 and the installed replacement agree in EDX:EAX, the records
 * (with EBX/ESI/EDI, EBP/ESP relative to the slot, the residue hash below
 * each callee's ESP and the message record with its residue), the arena
 * and the globals. The others: 31/a7 poison stacks, pair, arena and
 * globals. */
static int blv_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    const int tracked=index==ES2_HOOK_INDEX_r_key_toggle;
    blv_track=tracked;blv_clobbers=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],want[2],got[2],pattern=poison?0xa7a7a7a7u:0x31313131u;blv_build(index,n*41u+3u,a,r,0);
        BYTE before[BLV_GLOBALS],after[BLV_GLOBALS],actual[BLV_GLOBALS],expected_fpu[108],actual_fpu[108];static blv_record expected[BLV_MAX];
        memcpy(blv_before,blv_arena,BLV_SIZE);blv_globals(before,0);
        blv_reset();
        if(tracked)blv_track_run(mapped+d->rva,a,pattern,want);else sg_poison_call(mapped+d->rva,a,pattern,want);
        es2_hook_fpu_save(expected_fpu);
        const unsigned calls=blv_calls;memcpy(expected,blv_log,calls*sizeof(*expected));memcpy(blv_after,blv_arena,BLV_SIZE);blv_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("bridged_leaves poison install");
        memcpy(blv_arena,blv_before,BLV_SIZE);blv_globals(before,1);blv_reset();
        if(tracked)blv_track_run(mapped+d->rva,a,pattern,got);else sg_poison_call(mapped+d->rva,a,pattern,got);
        es2_hook_fpu_save(actual_fpu);blv_globals(actual,0);
        uint64_t pair[2]={(uint64_t)want[1]<<32|want[0],(uint64_t)got[1]<<32|got[0]};
        if(pair[0]!=pair[1]||calls!=blv_calls||memcmp(expected,blv_log,calls*sizeof(*expected))||memcmp(blv_after,blv_arena,BLV_SIZE)||
           memcmp(after,actual,BLV_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=blv_report(fast?"installed fast":"installed preserving",n,pair,calls,expected,after,actual);
        if(!es2_hook_uninstall(&h))return fail("bridged_leaves poison uninstall");blv_globals(before,1);++total;
    }
    blv_track=0;
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status&&tracked&&!blv_clobbers){fprintf(stderr,"FAIL: r_key_toggle installed probes: no changed argument slot was observed\n");status=1;}
    if(!status&&tracked)printf("PASS: %s %u installed cases through the entry shim, fast/preserving dispatch, 31/a7 stack, per-call EBX/ESI/EDI, EBP/ESP, residue below each callee and in the message record, changed argument slots (%u in both runs), nonempty x87, full pair/calls/arena/globals\n",
        d->name,total,blv_clobbers);
    else if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair, arena, globals\n",d->name,total);
    return status;
}
/* Real-callee passes against the all-original run on the same state:
 * r_key_toggle over the real sfx_play (sound flag 49f8ec clear; the cancel
 * and the slots stay stubs), sfx_play nested in rotating modes;
 * matrix_ctor over the real 495194/49514f and rotation_ctor over the real
 * 49514f, the nested hooks rotating. This hook original or replaced. */
static int blv_real_pass(unsigned index){
    const es2_hook_descriptor *d=function_hook_descriptor(index);
    unsigned nested[2],count=0;
    if(index==ES2_HOOK_INDEX_r_key_toggle)nested[count++]=ES2_HOOK_INDEX_sfx_play;
    else if(index==ES2_HOOK_INDEX_matrix_ctor){nested[count++]=ES2_HOOK_INDEX_rotation_ctor;nested[count++]=ES2_HOOK_INDEX_rotation_base_ctor;}
    else nested[count++]=ES2_HOOK_INDEX_rotation_base_ctor;
    BYTE saved[BLV_IDS][5];int active[BLV_IDS];
    if(index==ES2_HOOK_INDEX_r_key_toggle&&blv_patch(saved,active,1u<<1,1))return 1;
    int status=0;unsigned total=0;uint64_t entered[2]={0},hooked=0,replaced=0;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<256&&!status;++n){
        uint32_t a[6],r[3],want[2],got[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;blv_build(index,n*67u+mode*7u,a,r,0);
        BYTE before[BLV_GLOBALS],after[BLV_GLOBALS],actual[BLV_GLOBALS];static blv_record expected[BLV_MAX];
        memcpy(blv_before,blv_arena,BLV_SIZE);blv_globals(before,0);
        blv_reset();sg_poison_call(mapped+d->rva,a,pattern,want);
        const unsigned calls=blv_calls;memcpy(expected,blv_log,calls*sizeof(*expected));memcpy(blv_after,blv_arena,BLV_SIZE);blv_globals(after,0);
        es2_hook hk[3];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if((!e||e->compare_unsupported)&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("bridged_leaves nested install");}
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+2,d,&o,function_hook_extension(index)))return fail("bridged_leaves real install");
        memcpy(blv_arena,blv_before,BLV_SIZE);blv_globals(before,1);blv_reset();
        sg_poison_call(mapped+d->rva,a,pattern,got);blv_globals(actual,0);
        uint64_t pair[2]={(uint64_t)want[1]<<32|want[0],(uint64_t)got[1]<<32|got[0]};
        if(pair[0]!=pair[1]||calls!=blv_calls||memcmp(expected,blv_log,calls*sizeof(*expected))||memcmp(blv_after,blv_arena,BLV_SIZE)||
           memcmp(after,actual,BLV_GLOBALS)){fprintf(stderr,"real mode=%u\n",mode);status=blv_report("real",n,pair,calls,expected,after,actual);}
        for(unsigned j=0;j<count;++j){entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("bridged_leaves nested compare mismatch");}
        hooked+=hk[2].calls;replaced+=mode>=3;
        if(!es2_hook_uninstall(hk+2))return fail("bridged_leaves real uninstall");
        for(unsigned j=0;j<count;++j)if(!es2_hook_uninstall(hk+j))return fail("bridged_leaves nested uninstall");
        blv_globals(before,1);++total;
    }
    if(index==ES2_HOOK_INDEX_r_key_toggle&&blv_patch(saved,active,1u<<1,0))return 1;
    if(!status&&hooked!=replaced){fprintf(stderr,"FAIL: %s real pass entered %llu of %llu\n",d->name,(unsigned long long)hooked,(unsigned long long)replaced);status=1;}
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes (entered %llu), hook original/replaced (%llu replaced), 31/a7 stack, pair/calls/arena/globals\n",
        d->name,total,count,(unsigned long long)entered[0],(unsigned long long)replaced);
    return status;
}
static void blv_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){blv_build(index,n,a,r,1);}
static void blv_compare_state(BYTE *s,int restore){
    if(restore){memcpy(blv_arena,s,BLV_SIZE);blv_globals(s+BLV_SIZE,1);}else{memcpy(s,blv_arena,BLV_SIZE);blv_globals(s+BLV_SIZE,0);}
}
static int bridged_leaves_cases(unsigned index,unsigned limit){
    current_function=function_hook_descriptor(index)->name;
    BYTE old_globals[BLV_GLOBALS];blv_globals(old_globals,0);
    if(limit&&compare_probe_installed(index)){
        const int status=compare_probe_run(index,limit,blv_compare_build,blv_compare_state,BLV_SIZE+BLV_GLOBALS);
        blv_globals(old_globals,1);return status;
    }
    const int stubs=index==ES2_HOOK_INDEX_r_key_toggle;
    BYTE saved[BLV_IDS][5];int active[BLV_IDS];
    if(stubs&&blv_patch(saved,active,3u,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_BRIDGED_LEAVES_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;memset(blv_hits,0,sizeof(blv_hits));if(!limit)coverage_enable(1);blv_track=0;
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];blv_build(index,n,a,r,0);status=blv_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;blv_globals(old_globals,1);
    if(!limit&&!status)status=blv_installed_probes(index,stubs?256:64);
    blv_globals(old_globals,1);
    if(stubs&&blv_patch(saved,active,3u,0))return 1;
    if(!limit&&!status&&(stubs||index==ES2_HOOK_INDEX_matrix_ctor||index==ES2_HOOK_INDEX_rotation_ctor))status=blv_real_pass(index);
    blv_globals(old_globals,1);
    if(!limit&&!status&&!stubs)status=compare_probe_run(index,0,blv_compare_build,blv_compare_state,BLV_SIZE+BLV_GLOBALS);
    blv_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases (stub calls 462830 %u, 435ac8 %u, slot 0 %u); full return pair, ECX (core), ordered calls with entry registers and the message record, arena, globals, callee counters\n",
        function_hook_descriptor(index)->name,cases,blv_hits[0],blv_hits[1],blv_hits[2]+blv_hits[3]);
    return status;
}
