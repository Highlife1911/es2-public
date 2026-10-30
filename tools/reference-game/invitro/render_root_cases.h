/* render_root group: the render root 45fca8 (render_root), the stage dispatcher
 * 401d94 (stage_dispatch) and the vector copy 40707c (vector_copy)
 * (docs/reverse-engineering/main_loop.md, "the render root as own code"). A 12
 * KiB arena holds two cockpits H (+1F5h the HUD, +20Bh the EventPort), 16
 * objects (each [obj] = one of two arena vtables whose +8 and +10h point at
 * four slot stubs), two cameras, two graphics contexts (+220h/+224h the canvas
 * origin), four jolt points, four light entries, two players and a 4 KiB vector
 * region; the image globals the root reads and writes (detail bytes
 * 4d1f41/4d1f42 and their words in the view block, the context, player and
 * camera pointers, the debug byte 4d24c5, the map 4a0be0, the light table
 * 4a9608, the cockpit 49b088, the cursor 4d22c8/4d22cc, the stage count 497074
 * and 48 entries of the stage table 4a804c) are set per case, compared and
 * restored.
 *
 * The root's callees 401d94, 45d490, 45d50c, 42d870, 48c2e4, 46cb9c, 4327ac,
 * 42841c, 42db18, 40707c, 48c444, 42e700, 42883c, 41b130, 433158, 434520,
 * the four slot targets and the twelve stage-5/6 callbacks (405ea4 ...
 * 46b188, 401e5c, patched at their own VAs, so the table holds their real
 * addresses) are logging stubs: ordered calls with the bridge's slot
 * count, the arguments (434520's pointer argument, which ECX also holds,
 * as the 8 bytes it points at, the address only relative to the caller in
 * the tracked runs) and the entry EAX/EDX/ECX; the callbacks also record EBX/ESI/EDI (the table
 * entry, index and stage). Results come from a per-case stream (42d870
 * returns a jolt point, 42db18 AL zero in half the calls, 433158 an
 * object), and one call of four applies a side effect the original reads
 * again (detail bytes and words, camera, context, player, cockpit, its
 * objects, a vtable, the light and map pointers, the cursor, the debug
 * byte, the stage count, a later table entry). Direct cases compare
 * EDX:EAX, the call log, the arena and the globals, and for the first
 * 4,096 cases the callee counters. The frame probe runs the root through
 * its entry shim (fast and preserving dispatch, 31/a7 stack, EAX..EDI
 * distinct patterns) and compares EAX/EDX/ECX, every stub's EBX/ESI/EDI,
 * EBP and ESP relative to the caller and a hash of the 256 bytes below its
 * ESP, the x87 environment and the 1,024 bytes below the caller's ESP;
 * one-slot stubs change their argument slot in one call of four. The depth
 * probe paints 640 KiB below the caller and measures the deepest stack
 * write of the shimmed root (gap plus host frames); the nested budget
 * measures cockpit_sync through its own shim over the cockpit_sync group's
 * stubbed world and checks that it stays above the root's host frames. The real-callee pass
 * runs the root over the real 401d94 and 40707c (callbacks stubbed) with
 * render_root, stage_dispatch and vector_copy original/replaced in all
 * eight combinations against the all-original run. stage_dispatch and
 * vector_copy have own direct and installed 31/a7 cases; vector_copy runs
 * the compare probe. Identifiers: rr_/RR_. */
enum { RR_SIZE=0x3000, RR_H0=0x100, RR_H1=0x400, RR_OBJ=0x700, RR_OBJECTS=16, RR_VT=0xb00, RR_CAM0=0xc00, RR_CAM1=0xc40,
    RR_CTX0=0xd00, RR_CTX1=0xf80, RR_JOLT=0x1200, RR_LIGHT=0x1300, RR_P0=0x1400, RR_P1=0x1440, RR_COPY=0x2000, RR_COPY_SIZE=0x1000,
    RR_IDS=32, RR_MAX=96, RR_TABLE=48, RR_GLOBALS=1+1+2+2+2+2+4+4+4+1+4+4+4+4+4+2+6*RR_TABLE,
    RR_CALLBACK=20, RR_DEPTH=0xa0000 };
#define RR_BASE ((uint32_t)(uintptr_t)rr_arena)
#define RR_ADDR(o) (RR_BASE+(uint32_t)(o))
static BYTE rr_arena[RR_SIZE],rr_before[RR_SIZE],rr_after[RR_SIZE];
typedef struct rr_record { uint32_t id,slots,a[3],regs[3],cs[3],frame[2],window,extra[2]; } rr_record;
static rr_record rr_log[RR_MAX];
static unsigned rr_calls,rr_track,rr_hash_on,rr_clobbers,rr_paths[RR_IDS];
static uint32_t rr_seed,rr_stream;
/* Captured by the stubs at entry: EAX, EDX, ECX, EBX, ESI, EDI, EBP, ESP;
 * the 256 bytes below the entry ESP; the ECX a stub returns; the caller's
 * ESP before the CALL of a tracked run; the 1,024 bytes below it after
 * return; the deepest write of the depth probe. */
__attribute__((used)) static uint32_t rr_cap[8],rr_ecx,rr_origin,rr_deepest;
__attribute__((used)) static BYTE rr_window[256],rr_frame_copy[1024];
/* ids 0..15 the root's direct callees, 16/17 slot +8 of vtable 0/1, 18/19
 * slot +10h, 20..31 the stage callbacks. */
static const uint32_t rr_addr[RR_IDS]={0x401d94,0x45d490,0x45d50c,0x42d870,0x48c2e4,0x46cb9c,0x4327ac,0x42841c,0x42db18,0x40707c,
    0x48c444,0x42e700,0x42883c,0x41b130,0x433158,0x434520,0,0,0,0,
    0x405ea4,0x4077e8,0x407b3c,0x4083e0,0x408f9c,0x409904,0x409a1c,0x40a108,0x41544c,0x4215a8,0x46b188,0x401e5c};
static const unsigned rr_arity[RR_IDS]={1,1,1,0,1,3,1,0,0,2,1,1,2,1,0,2,1,1,1,1};
static const struct { uint32_t rva,size; } rr_global_list[]={
    {0xd1f41,1},{0xd1f42,1},{0xd24d2,2},{0xd24d4,2},{0xd256d,2},{0xd256f,2},{0xd24ce,4},{0xd24e6,4},{0xd24ea,4},{0xd24c5,1},
    {0xa0be0,4},{0xa9608,4},{0x9b088,4},{0xd22c8,4},{0xd22cc,4},{0x97074,2},{0xa804c,6*RR_TABLE}};
static void rr_globals(BYTE *out,int restore){
    BYTE *p=out;
    for(unsigned i=0;i<sizeof(rr_global_list)/sizeof(rr_global_list[0]);++i){
        if(restore)memcpy(mapped+rr_global_list[i].rva,p,rr_global_list[i].size);
        else memcpy(p,mapped+rr_global_list[i].rva,rr_global_list[i].size);
        p+=rr_global_list[i].size;
    }
}
static uint32_t rr_next(void){
    uint32_t z=rr_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static uint32_t rr_pick(uint32_t bound){return rr_next()%bound;}
static void rr_reset(void){rr_calls=0;rr_stream=rr_seed;}
static uint32_t rr_hash(const BYTE *p,unsigned n){uint32_t h=2166136261u;for(unsigned i=0;i<n;++i)h=(h^p[i])*16777619u;return h;}
static uint32_t rr_object(void){return RR_ADDR(RR_OBJ+0x40u*rr_pick(RR_OBJECTS));}
static uint32_t rr_cockpit(void){return RR_ADDR(rr_pick(2)?RR_H1:RR_H0);}
/* A stage word: mostly 4, 5 or 6, sometimes any. */
static uint16_t rr_stage_word(void){const uint32_t k=rr_pick(8);return (uint16_t)(k<7?4u+k%3u:rr_next());}
static void rr_table_entry(unsigned i){
    BYTE *e=mapped+0xa804cu+6u*i;put32(e,rr_addr[RR_CALLBACK+rr_pick(12)]);put16(e+4,rr_stage_word());
}
/* The count word: 0..48, negative in one case of 16. */
static uint16_t rr_count(void){return (uint16_t)(rr_pick(16)==0?0x8000u|rr_next():rr_pick(RR_TABLE+1));}
/* One callee side effect (about one call in four), drawn from the stream. */
static void rr_effect(void){
    const uint32_t pick=rr_next();if(pick%4)return;
    BYTE *H=rr_arena+(get32(mapped+0x9b088)-RR_BASE);
    switch(pick/4%15){
    case 0:mapped[0xd1f41]=(BYTE)rr_pick(4);break;
    case 1:mapped[0xd1f42]=(BYTE)rr_pick(4);break;
    case 2:{static const uint32_t w[4]={0xd24d2,0xd24d4,0xd256d,0xd256f};put16(mapped+w[rr_pick(4)],(uint16_t)rr_pick(4));break;}
    case 3:put32(mapped+0xd24ea,RR_ADDR(rr_pick(2)?RR_CAM1:RR_CAM0));break;
    case 4:put32(mapped+0xd24ce,RR_ADDR(rr_pick(2)?RR_CTX1:RR_CTX0));break;
    case 5:put32(mapped+0xd24e6,RR_ADDR(rr_pick(2)?RR_P1:RR_P0));break;
    case 6:put32(mapped+0x9b088,rr_cockpit());break;
    case 7:put32(H+(rr_pick(2)?0x1f5:0x20b),rr_object());break;
    case 8:put32((BYTE *)(uintptr_t)rr_object(),RR_ADDR(RR_VT+0x40u*rr_pick(2)));break;
    case 9:put32(mapped+0xa9608,RR_ADDR(RR_LIGHT+0x20u*rr_pick(4)));put32(mapped+0xa0be0,rr_next());break;
    case 10:put32(mapped+0xd22c8+4u*rr_pick(2),rr_next());break;
    case 11:mapped[0xd24c5]=(BYTE)rr_next();break;
    case 12:put16(mapped+0x97074,rr_count());break;
    case 13:rr_table_entry(rr_pick(RR_TABLE));break;
    default:{BYTE *c=rr_arena+(rr_pick(2)?RR_CTX1:RR_CTX0);put32(c+0x220+4*rr_pick(2),rr_next());break;}
    }
}
__attribute__((used,noinline)) static uint64_t __cdecl rr_log_impl(unsigned id){
    if(rr_calls==RR_MAX){fail("render_root synthetic call overflow");ExitProcess(1);}
    uint32_t *a=(uint32_t *)(uintptr_t)(rr_cap[7]+4u);
    rr_record *v=rr_log+rr_calls++;memset(v,0,sizeof(*v));v->id=id;v->slots=weapons_bridge_slots(a,rr_arity[id]);
    for(unsigned j=0;j<rr_arity[id];++j)v->a[j]=a[j];
    for(unsigned j=0;j<3;++j)v->regs[j]=rr_cap[j];
    if(id==15){v->extra[0]=get32((const BYTE *)(uintptr_t)a[1]);v->extra[1]=get32((const BYTE *)(uintptr_t)a[1]+4);
        v->regs[2]-=a[1];v->a[1]=rr_track?a[1]-rr_origin:0;}   /* the frame copy (also in ECX): its bytes, its place only relative to the caller */
    if(id>=RR_CALLBACK||rr_track)for(unsigned j=0;j<3;++j)v->cs[j]=rr_cap[3+j];
    if(rr_track&&id<RR_CALLBACK){v->frame[0]=rr_cap[6]-rr_origin;v->frame[1]=rr_cap[7]-rr_origin;if(rr_hash_on)v->window=rr_hash(rr_window,256);}
    uint32_t eax=rr_next(),edx=rr_next();rr_ecx=rr_next();
    if(id==3)eax=RR_ADDR(RR_JOLT+0x10u*rr_pick(4));            /* 42d870: the jolt point */
    if(id==8&&rr_pick(2))eax&=0xffffff00u;                     /* 42db18: AL zero skips the 3D view */
    if(id==14)eax=rr_object();                                 /* 433158: the message port */
    if(rr_track&&rr_arity[id]==1&&rr_pick(4)==0){a[0]=rr_next();++rr_clobbers;}   /* the callee changes its slot */
    rr_effect();
    return (uint64_t)edx<<32|eax;
}
#define RR_STUB(n) __attribute__((naked)) static void rr_stub_##n(void){__asm__ volatile( \
    "movl %eax,_rr_cap\n\tmovl %edx,_rr_cap+4\n\tmovl %ecx,_rr_cap+8\n\tmovl %ebx,_rr_cap+12\n\t" \
    "movl %esi,_rr_cap+16\n\tmovl %edi,_rr_cap+20\n\tmovl %ebp,_rr_cap+24\n\tmovl %esp,_rr_cap+28\n\t" \
    "leal -256(%esp),%esi\n\tmovl $_rr_window,%edi\n\tmovl $64,%ecx\n\tcld\n\trep movsl\n\t" \
    "movl _rr_cap+16,%esi\n\tmovl _rr_cap+20,%edi\n\t" \
    "pushl $" #n "\n\tcalll _rr_log_impl\n\taddl $4,%esp\n\tmovl _rr_ecx,%ecx\n\tret\n\t");}
RR_STUB(0) RR_STUB(1) RR_STUB(2) RR_STUB(3) RR_STUB(4) RR_STUB(5) RR_STUB(6) RR_STUB(7) RR_STUB(8) RR_STUB(9)
RR_STUB(10) RR_STUB(11) RR_STUB(12) RR_STUB(13) RR_STUB(14) RR_STUB(15) RR_STUB(16) RR_STUB(17) RR_STUB(18) RR_STUB(19)
RR_STUB(20) RR_STUB(21) RR_STUB(22) RR_STUB(23) RR_STUB(24) RR_STUB(25) RR_STUB(26) RR_STUB(27) RR_STUB(28) RR_STUB(29)
RR_STUB(30) RR_STUB(31)
#undef RR_STUB
static void *rr_stubs[RR_IDS]={rr_stub_0,rr_stub_1,rr_stub_2,rr_stub_3,rr_stub_4,rr_stub_5,rr_stub_6,rr_stub_7,rr_stub_8,rr_stub_9,
    rr_stub_10,rr_stub_11,rr_stub_12,rr_stub_13,rr_stub_14,rr_stub_15,rr_stub_16,rr_stub_17,rr_stub_18,rr_stub_19,
    rr_stub_20,rr_stub_21,rr_stub_22,rr_stub_23,rr_stub_24,rr_stub_25,rr_stub_26,rr_stub_27,rr_stub_28,rr_stub_29,
    rr_stub_30,rr_stub_31};
#define RR_ALL 0xffffffffu
#define RR_REAL (RR_ALL&~((1u<<0)|(1u<<9)))                  /* the real 401d94 and 40707c */
#define RR_CALLBACKS (0xfff00000u)                           /* the stage callbacks only */
static int rr_patch(BYTE saved[RR_IDS][5],int active[RR_IDS],uint32_t which,int on){
    for(unsigned i=0;i<RR_IDS;++i){
        if(!rr_addr[i]){active[i]=0;continue;}
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(rr_addr[i]-0x400000u,rr_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(rr_addr[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
/* One world: the arena from the case stream, the objects and pointers the
 * root and the real callees read, the stage table; r = entry EAX/EDX/ECX. */
static void rr_build(unsigned n,uint32_t r[3]){
    rr_seed=random32();rr_stream=rr_seed^n;
    for(unsigned i=0;i<RR_SIZE;i+=4)put32(rr_arena+i,rr_next());
    for(unsigned i=0;i<3;++i)r[i]=rr_next();
    for(unsigned v=0;v<2;++v){BYTE *t=rr_arena+RR_VT+0x40u*v;
        put32(t+8,(uint32_t)(uintptr_t)rr_stubs[16+v]);put32(t+0x10,(uint32_t)(uintptr_t)rr_stubs[18+v]);}
    for(unsigned k=0;k<RR_OBJECTS;++k)put32(rr_arena+RR_OBJ+0x40u*k,RR_ADDR(RR_VT+0x40u*rr_pick(2)));
    for(unsigned h=0;h<2;++h){BYTE *H=rr_arena+(h?RR_H1:RR_H0);put32(H+0x1f5,rr_object());put32(H+0x20b,rr_object());}
    mapped[0xd1f41]=(BYTE)rr_pick(4);mapped[0xd1f42]=(BYTE)rr_pick(4);
    put16(mapped+0xd24d4,(uint16_t)(rr_pick(16)?rr_pick(4):rr_next()));put16(mapped+0xd256f,(uint16_t)(rr_pick(16)?rr_pick(4):rr_next()));
    put16(mapped+0xd24d2,(uint16_t)rr_next());put16(mapped+0xd256d,(uint16_t)rr_next());
    put32(mapped+0xd24ce,RR_ADDR(rr_pick(2)?RR_CTX1:RR_CTX0));put32(mapped+0xd24e6,RR_ADDR(rr_pick(2)?RR_P1:RR_P0));
    put32(mapped+0xd24ea,RR_ADDR(rr_pick(2)?RR_CAM1:RR_CAM0));mapped[0xd24c5]=(BYTE)rr_next();
    put32(mapped+0xa0be0,rr_next());put32(mapped+0xa9608,RR_ADDR(RR_LIGHT+0x20u*rr_pick(4)));put32(mapped+0x9b088,rr_cockpit());
    put32(mapped+0xd22c8,rr_next());put32(mapped+0xd22cc,rr_next());
    for(unsigned i=0;i<RR_TABLE;++i)rr_table_entry(i);
    put16(mapped+0x97074,rr_count());
    rr_stream=rr_seed^~n;rr_seed=rr_stream;   /* the stubs' stream restarts here in both runs */
}
static unsigned rr_hits(const callee_counter *c){
    unsigned h=0;
    for(unsigned j=0;j<rr_calls;++j){const unsigned id=rr_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?(c->key==8?id==16||id==17:id==18||id==19):(rr_addr[id]==c->key))++h;}
    return h;
}
static int rr_report(const char *what,unsigned n,const uint32_t want[3],const uint32_t got[3],unsigned calls,const rr_record *e,
    const BYTE *globals_want,const BYTE *globals_got){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x calls %u/%u\n",current_function,n,what,
        want[0],got[0],want[1],got[1],want[2],got[2],calls,rr_calls);
    for(unsigned j=0;j<calls&&j<rr_calls&&e;++j)if(memcmp(e+j,rr_log+j,sizeof(*e))){
        const uint32_t *x=(const uint32_t*)(e+j),*y=(const uint32_t*)(rr_log+j);
        for(unsigned k=0;k<sizeof(*e)/4;++k)if(x[k]!=y[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,e[j].id,k,x[k],y[k]);break;}
    for(unsigned j=0;j<RR_SIZE;++j)if(rr_after[j]!=rr_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,rr_after[j],rr_arena[j]);break;}
    for(unsigned j=0;globals_want&&j<RR_GLOBALS;++j)if(globals_want[j]!=globals_got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,globals_want[j],globals_got[j]);break;}
    return 1;
}
/* Direct case: the original (weapons_original) and the adapter's plain
 * path over the same stubs; a = the arguments, r = entry registers. */
static int rr_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[RR_GLOBALS],after[RR_GLOBALS],actual[RR_GLOBALS];static rr_record expected[RR_MAX];
    memcpy(rr_before,rr_arena,RR_SIZE);rr_globals(before,0);
    rr_reset();const uint64_t want=weapons_original(index,a,r);
    const unsigned calls=rr_calls;memcpy(expected,rr_log,calls*sizeof(*expected));memcpy(rr_after,rr_arena,RR_SIZE);rr_globals(after,0);
    for(unsigned j=0;j<calls;++j)rr_paths[expected[j].id]++;
    memcpy(rr_arena,rr_before,RR_SIZE);rr_globals(before,1);rr_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[32]={0};
    for(unsigned i=0;i<count&&i<32;++i)counts[i]=c[i].count;
    const uint64_t got=x->replacement_context(a,d->user,r);rr_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count&&i<32;++i)if(c[i].count-counts[i]!=rr_hits(c+i))return fail("render_root callee counter delta");
    if(want!=got||calls!=rr_calls||memcmp(expected,rr_log,calls*sizeof(*expected))||memcmp(rr_after,rr_arena,RR_SIZE)||memcmp(after,actual,RR_GLOBALS)){
        const uint32_t w[3]={(uint32_t)want,(uint32_t)(want>>32),0},g[3]={(uint32_t)got,(uint32_t)(got>>32),0};
        return rr_report("direct",n,w,g,calls,expected,after,actual);
    }
    rr_globals(before,1);
    return 0;
}
/* Calls target (no argument slot) over a 4-KiB fill with EAX..EDI distinct
 * pattern words; rr_origin = the caller's ESP before the CALL; EAX, EDX,
 * ECX into out; the 1,024 bytes below the origin after return into
 * rr_frame_copy. */
__attribute__((naked,noinline)) static void rr_track_run(void *target,uint32_t pattern,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 12(%ebp),%eax\n\tmovl $1024,%ecx\n\tcld\n\trep stosl\n\taddl $4096,%esp\n\t"
        "movl %esp,_rr_origin\n\tmovl 12(%ebp),%eax\n\t"
        "movl %eax,%edx\n\txorl $0x01010101,%edx\n\tmovl %eax,%ecx\n\txorl $0x02020202,%ecx\n\tmovl %eax,%ebx\n\txorl $0x04040404,%ebx\n\t"
        "movl %eax,%esi\n\txorl $0x08080808,%esi\n\tmovl %eax,%edi\n\txorl $0x10101010,%edi\n\tcall *8(%ebp)\n\t"
        "movl 16(%ebp),%ebx\n\tmovl %eax,(%ebx)\n\tmovl %edx,4(%ebx)\n\tmovl %ecx,8(%ebx)\n\t"
        "leal -1024(%esp),%esi\n\tmovl $_rr_frame_copy,%edi\n\tmovl $256,%ecx\n\tcld\n\trep movsl\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* The depth probe: fills RR_DEPTH bytes below the caller's ESP with the
 * pattern (one dword at a time downward, so every guard page is touched in
 * order), calls target (no argument slot) and finds the lowest dword that
 * no longer holds the pattern; rr_deepest = its distance below the
 * caller's ESP before the CALL. */
__attribute__((naked,noinline)) static void rr_depth_run(void *target,uint32_t pattern){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl %esp,_rr_origin\n\tmovl %esp,%edi\n\tmovl 12(%ebp),%eax\n\tmovl $0x28000,%ecx\n\t"
        "1: subl $4,%edi\n\tmovl %eax,(%edi)\n\tdecl %ecx\n\tjnz 1b\n\t"
        "call *8(%ebp)\n\t"
        "movl _rr_origin,%edi\n\tsubl $0xa0000,%edi\n\tmovl 12(%ebp),%eax\n\tmovl $0x28000,%ecx\n\tcld\n\trepe scasl\n\t"
        "movl _rr_origin,%eax\n\tsubl %edi,%eax\n\taddl $4,%eax\n\tmovl %eax,_rr_deepest\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* rr_depth_run with one argument slot (pushed after the fill). */
__attribute__((naked,noinline)) static void rr_depth_run1(void *target,uint32_t arg,uint32_t pattern){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl %esp,_rr_origin\n\tmovl %esp,%edi\n\tmovl 16(%ebp),%eax\n\tmovl $0x28000,%ecx\n\t"
        "1: subl $4,%edi\n\tmovl %eax,(%edi)\n\tdecl %ecx\n\tjnz 1b\n\t"
        "pushl 12(%ebp)\n\tcall *8(%ebp)\n\taddl $4,%esp\n\t"
        "movl _rr_origin,%edi\n\tsubl $0xa0000,%edi\n\tmovl 16(%ebp),%eax\n\tmovl $0x28000,%ecx\n\tcld\n\trepe scasl\n\t"
        "movl _rr_origin,%eax\n\tsubl %edi,%eax\n\taddl $4,%eax\n\tmovl %eax,_rr_deepest\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* The nested budget: cockpit_sync, the root's deepest callee, installed
 * replaced through its own entry shim over the cockpit_sync group's
 * stubbed world (cockpit_sync_cases.h). Its deepest write below the ESP
 * its caller had before pushing the argument, D, must leave the render
 * root's host frames alone: under the root that ESP is the root's
 * outgoing ESP (entry ESP - 14h) and the root's host frames start below
 * its PUSH EBX and its gap (the context first), so D + 14h must stay
 * within 4 + gap (the margin is printed). */
static int rr_nested_budget(void){
    const unsigned index=ES2_HOOK_INDEX_cockpit_sync;const es2_hook_descriptor *d=function_hook_descriptor(index);
    BYTE csaved[CSY_IDS][5];int cactive[CSY_IDS];BYTE cglobals[CSY_GLOBALS];csy_globals(cglobals,0);
    if(csy_patch(csaved,cactive,CSY_ALL,1))return 1;
    int status=0;uint32_t deepest[2]={0,0};csy_track=0;csy_fill=0;
    for(unsigned fast=0;fast<2&&!status;++fast){
        uint32_t a[6],r[3];csy_build(fast*5u+11u,a,r);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("render_root nested install");
        csy_reset();rr_depth_run1(mapped+d->rva,a[0],0x5ca1ab1eu);deepest[fast]=rr_deepest;
        if(!es2_hook_uninstall(&h))return fail("render_root nested uninstall");
        if(h.calls!=1||!csy_calls){fprintf(stderr,"FAIL: %s nested budget: cockpit_sync not run through its replacement\n",current_function);status=1;}
    }
    es2_hook_set_fast_path(TRUE);
    if(csy_patch(csaved,cactive,CSY_ALL,0))return 1;
    csy_globals(cglobals,1);
    const uint32_t host=4u+RENDER_ROOT_PAGES*4096u,worst=(deepest[0]>deepest[1]?deepest[0]:deepest[1])+0x14u;
    if(!status&&worst>host){fprintf(stderr,"FAIL: %s nested budget: cockpit_sync reaches %u bytes below the root's entry, its host frames start at %u\n",current_function,worst,host);status=1;}
    if(!status)printf("PASS: render_root nested budget: cockpit_sync through its shim writes %u (fast) / %u (preserving) bytes below its caller's ESP (argument, return address, PUSH EBP, 112-page gap + %u / %u context and host bytes); under the root %u bytes below the root's entry ESP, %u bytes above the root's host frames\n",
        deepest[1],deepest[0],deepest[1]-12u-COCKPIT_SYNC_PAGES*4096u,deepest[0]-12u-COCKPIT_SYNC_PAGES*4096u,worst,host-worst);
    return status;
}
/* Floor probe. rr_floor_run sets _es2_machine_x86_floor to its own ESP minus
 * `flooroff` (a stand-in for an outer suspended host frame, as a switched
 * machine call sets it under the logic frame), paints RR_DEPTH bytes
 * below its ESP with the pattern (the shims' OR-0 gap probe preserves it),
 * calls `target` with one argument and EAX the inverted pattern (so a stray
 * store of EAX shows) and clears the floor. It reports the deepest written
 * (non-pattern) dword below its ESP (rr_deepest) and, in rr_floor_canary, the
 * number of the 64 dwords at and just above the floor that are no longer the
 * pattern (0 = the inner shim wrote only below the floor, so an outer host
 * frame there would survive). When flooroff exceeds the shim's own gap the shim
 * must relocate its context to the floor (deepest ~= flooroff); when it is
 * smaller the shim keeps its own deeper gap (deepest ~= the gap); either way
 * nothing at or above the floor is written. The pattern must differ from the
 * shim's guard value (OR 0) only in being a value the shim never stores; a
 * plain marker suffices. */
__attribute__((used)) static uint32_t rr_floor_canary;
__attribute__((naked,noinline)) static void rr_floor_run(void *target,uint32_t arg,uint32_t flooroff,uint32_t pattern){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl %esp,_rr_origin\n\tmovl %esp,%eax\n\tsubl 16(%ebp),%eax\n\tmovl %eax,_es2_machine_x86_floor\n\t"
        "movl %esp,%edi\n\tmovl 20(%ebp),%eax\n\tmovl $0x28000,%ecx\n\t"
        "1: subl $4,%edi\n\tmovl %eax,(%edi)\n\tdecl %ecx\n\tjnz 1b\n\tnotl %eax\n\t"
        "pushl 12(%ebp)\n\tcall *8(%ebp)\n\taddl $4,%esp\n\tmovl $0,_es2_machine_x86_floor\n\t"
        "movl _rr_origin,%edi\n\tsubl $0xa0000,%edi\n\tmovl 20(%ebp),%eax\n\tmovl $0x28000,%ecx\n\tcld\n\trepe scasl\n\t"
        "movl _rr_origin,%eax\n\tsubl %edi,%eax\n\taddl $4,%eax\n\tmovl %eax,_rr_deepest\n\t"
        "movl _rr_origin,%edi\n\tsubl 16(%ebp),%edi\n\tmovl 20(%ebp),%eax\n\tmovl $16,%ecx\n\tcld\n\trepe scasl\n\t"
        "movl %ecx,_rr_floor_canary\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* The reachable gapped-shim pairs where the inner shim is called from game code
 * that an outer shim's replacement reached, with the inner shim's gap (pages).
 * The deepest reachable chain is the F12/Alt+P dialog (logic_frame ->
 * player_input -> command_key -> 0x45cff0 -> render_root -> cockpit_sync);
 * every inner shim's gap starts at a shallow game coordinate inside the outer
 * gap, so without the floor the inner host frames would overwrite the outer
 * ones. With the floor (es2_machine_floor) the inner host frames pack below every suspended
 * outer host frame, so the peak is the deepest single gap plus the few host
 * frames stacked at the floor, not the sum of the gaps. The table drives the
 * budget assertion below. Pages are the shim gaps of main_loop.md, "shim stack
 * budget". */
typedef struct { const char *outer,*inner; unsigned inner_pages; } rr_pair;
static const rr_pair rr_reach_pairs[]={
    {"logic_frame","player_input",64},{"logic_frame","command_key",48},{"logic_frame","hud_key",32},
    {"logic_frame","render_root",116},{"logic_frame","panel_frame",48},{"logic_frame","cockpit_sync",112},
    {"logic_frame","actor_step",84},{"logic_frame","torso_aim",48},
    {"player_input","command_key",48},{"player_input","hud_key",32},{"player_input","render_root",116},
    {"player_input","panel_frame",48},{"player_input","cockpit_sync",112},{"player_input","torso_aim",48},
    {"command_key","hud_key",32},{"command_key","render_root",116},{"command_key","panel_frame",48},
    {"command_key","cockpit_sync",112},{"render_root","panel_frame",48},{"render_root","cockpit_sync",112},
};
/* The budget: the deepest reachable chain's gaps, outermost first, every
 * shim called at a shallow game coordinate inside the previous gap. With
 * the floor the peak is max(gap_i) plus the host frames stacked at the
 * floor; a generous host bound per shim keeps the assertion robust. */
#define RR_MAX_HOST 8192u
static const unsigned rr_f12_gap_pages[]={112,64,48,116,112}; /* logic_frame,player_input,command_key,render_root,cockpit_sync */
static int rr_floor_budget(void){
    const unsigned index=ES2_HOOK_INDEX_cockpit_sync;const es2_hook_descriptor *d=function_hook_descriptor(index);
    const uint32_t gap=COCKPIT_SYNC_PAGES*4096u;
    BYTE csaved[CSY_IDS][5];int cactive[CSY_IDS];BYTE cglobals[CSY_GLOBALS];csy_globals(cglobals,0);
    if(csy_patch(csaved,cactive,CSY_ALL,1))return 1;
    int status=0;csy_track=0;csy_fill=0;
    /* floor deeper than the gap: honoured, context relocates to the floor. */
    const uint32_t deep=gap+0x20000u;        /* 128 KiB below the gap bottom */
    const uint32_t shallow=gap-0x20000u;      /* 128 KiB above the gap bottom */
    /* floors right below the probe's end (gap+4 .. gap+256 below rr_origin,
     * dword steps, so one of them lies just below the shim's ESP after the
     * probe whatever the entry path pushed): the macro itself must not write
     * the slot below the probe's end, which then lies in the outer host frame
     * above the floor. */
    const uint32_t edge=gap+4u;
    uint32_t honoured=0,kept=0,edged=0,canary_deep=1,canary_shallow=1,canary_none=1,canary_edge=0;
    es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
    es2_hook_set_fast_path(TRUE);
    if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("render_root floor install");
    uint32_t a[6],r[3];csy_build(23u,a,r);
    csy_reset();rr_floor_run(mapped+d->rva,a[0],deep,0x5ca1ab1eu);honoured=rr_deepest;canary_deep=rr_floor_canary;
    csy_reset();rr_floor_run(mapped+d->rva,a[0],shallow,0x5ca1ab1eu);kept=rr_deepest;canary_shallow=rr_floor_canary;
    for(uint32_t k=0;k<64u;++k){
        csy_reset();rr_floor_run(mapped+d->rva,a[0],edge+4u*k,0x5ca1ab1eu);
        if(rr_deepest>edged)edged=rr_deepest;
        if(rr_floor_canary)++canary_edge;
    }
    csy_reset();rr_floor_run(mapped+d->rva,a[0],0u,0x5ca1ab1eu);canary_none=(rr_deepest>=gap&&rr_deepest<gap+RR_MAX_HOST)?0:1;
    if(!es2_hook_uninstall(&h))return fail("render_root floor uninstall");
    if(csy_patch(csaved,cactive,CSY_ALL,0))return 1;
    csy_globals(cglobals,1);
    /* honoured: context relocated to the floor (deepest ~ deep), not to the
     * gap bottom; kept: floor shallower than the gap, shim keeps the gap;
     * no sentinel disturbed in either, so outer host frames survive. */
    if(!(honoured>=deep&&honoured<deep+RR_MAX_HOST)){
        fprintf(stderr,"FAIL: render_root floor budget: floor %u not honoured, deepest write %u\n",deep,honoured);status=1;}
    if(!(kept>=gap&&kept<gap+RR_MAX_HOST)){
        fprintf(stderr,"FAIL: render_root floor budget: shallow floor %u changed the gap, deepest write %u\n",shallow,kept);status=1;}
    if(!(edged>=edge&&edged<edge+256u+RR_MAX_HOST)){
        fprintf(stderr,"FAIL: render_root floor budget: edge floors from %u not honoured, deepest write %u\n",edge,edged);status=1;}
    if(canary_deep||canary_shallow||canary_none||canary_edge){
        fprintf(stderr,"FAIL: render_root floor budget: outer host frame overwritten (canary deep %u shallow %u none %u edge %u)\n",
            canary_deep,canary_shallow,canary_none,canary_edge);status=1;}
    /* F12/Alt+P chain peak: deepest gap plus the stacked host frames, well
     * inside the 1 MiB main-thread reserve (SizeOfStackReserve 0x100000). */
    unsigned maxgap=0,n=sizeof(rr_f12_gap_pages)/sizeof(rr_f12_gap_pages[0]);
    for(unsigned i=0;i<n;++i)if(rr_f12_gap_pages[i]*4096u>maxgap)maxgap=rr_f12_gap_pages[i]*4096u;
    const unsigned peak=maxgap+(n+2u)*RR_MAX_HOST;         /* + the main loop and a margin */
    if(peak>=0x100000u){fprintf(stderr,"FAIL: render_root floor budget: F12 chain peak %u >= 1 MiB\n",peak);status=1;}
    if(!status)printf("PASS: render_root floor budget: %u reachable gapped-shim pairs; cockpit_sync relocates its context to a floor %u below its caller (deepest write %u) and keeps its %u gap under a shallow floor %u (deepest %u) and honours 64 floors from %u right below its probe's end (deepest %u); the %u-byte sentinel above the floor survived all 67 runs; F12/Alt+P chain peak ~%u bytes, under the 1 MiB reserve\n",
        (unsigned)(sizeof(rr_reach_pairs)/sizeof(rr_reach_pairs[0])),deep,honoured,gap,shallow,kept,edge,edged,64u,peak);
    return status;
}
/* Frame probe through the entry shim: the original 45fca8 and the
 * installed replacement agree in EAX/EDX/ECX, the records (EBX/ESI/EDI,
 * EBP/ESP relative to the caller's ESP, the residue hash below each
 * callee), the arena, the globals, the x87 environment and the 1,024 bytes
 * below the caller's ESP; then the depth probe. */
static int rr_frame_probe(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    static rr_record expected[RR_MAX];static BYTE expected_frame[1024];
    rr_track=1;rr_hash_on=1;rr_clobbers=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t r[3],want[3],got[3];const uint32_t pattern=poison?0xa7a7a7a7u:0x31313131u;rr_build(n*41u+3u,r);
        BYTE before[RR_GLOBALS],after[RR_GLOBALS],actual[RR_GLOBALS],efpu[108],afpu[108];
        memcpy(rr_before,rr_arena,RR_SIZE);rr_globals(before,0);
        rr_reset();rr_track_run(mapped+d->rva,pattern,want);es2_hook_fpu_save(efpu);memcpy(expected_frame,rr_frame_copy,1024);
        const unsigned calls=rr_calls;memcpy(expected,rr_log,calls*sizeof(*expected));memcpy(rr_after,rr_arena,RR_SIZE);rr_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("render_root frame install");
        memcpy(rr_arena,rr_before,RR_SIZE);rr_globals(before,1);rr_reset();
        rr_track_run(mapped+d->rva,pattern,got);es2_hook_fpu_save(afpu);rr_globals(actual,0);
        if(want[0]!=got[0]||want[1]!=got[1]||want[2]!=got[2]||calls!=rr_calls||memcmp(expected,rr_log,calls*sizeof(*expected))||
           memcmp(rr_after,rr_arena,RR_SIZE)||memcmp(after,actual,RR_GLOBALS)||memcmp(efpu,afpu,12)||memcmp(efpu+28,afpu+28,80)||h.calls!=1)
            status=rr_report(fast?"frame fast":"frame preserving",n,want,got,calls,expected,after,actual);
        else if(memcmp(expected_frame,rr_frame_copy,1024)){
            fprintf(stderr,"FAIL: %s case %u (frame window)\n",current_function,n);
            for(unsigned j=0;j<1024;++j)if(expected_frame[j]!=rr_frame_copy[j]){fprintf(stderr," below-%u: %02x/%02x\n",1024-j,expected_frame[j],rr_frame_copy[j]);break;}
            status=1;}
        if(!es2_hook_uninstall(&h))return fail("render_root frame uninstall");rr_globals(before,1);++total;
    }
    rr_track=0;rr_hash_on=0;
    /* Depth: the shimmed root's deepest stack write, both dispatch paths. */
    uint32_t deepest[2]={0,0};
    for(unsigned fast=0;fast<2&&!status;++fast){
        uint32_t r[3];rr_build(fast+7u,r);BYTE before[RR_GLOBALS];rr_globals(before,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("render_root depth install");
        rr_reset();rr_depth_run(mapped+d->rva,0x5ca1ab1eu);deepest[fast]=rr_deepest;
        if(!es2_hook_uninstall(&h))return fail("render_root depth uninstall");rr_globals(before,1);
        const uint32_t gap=RENDER_ROOT_PAGES*4096u+8u;   /* return address, PUSH EBX, the gap */
        if(h.calls!=1||rr_deepest<=gap||rr_deepest-gap>8192u){
            fprintf(stderr,"FAIL: %s depth probe (%s): deepest write %u bytes below the caller, gap %u\n",current_function,fast?"fast":"preserving",rr_deepest,gap);status=1;}
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status&&!rr_clobbers){fprintf(stderr,"FAIL: %s frame probe: no changed argument slot was observed\n",current_function);status=1;}
    if(!status)printf("PASS: %s %u frame cases through the entry shim, fast/preserving dispatch, 31/a7 stack, per-call EAX..EDI, EBP/ESP, residue below each callee, changed argument slots (%u in both runs), EAX/EDX/ECX, nonempty x87, 1024 bytes below the caller; deepest stack write %u (fast) / %u (preserving) bytes below the caller = return address, PUSH EBX, %u-page gap + %u / %u context and host bytes\n",
        d->name,total,rr_clobbers,deepest[1],deepest[0],RENDER_ROOT_PAGES,deepest[1]-RENDER_ROOT_PAGES*4096u-8u,deepest[0]-RENDER_ROOT_PAGES*4096u-8u);
    return status;
}
/* Real-callee pass: 45fca8 over the real 401d94 and 40707c (the callbacks
 * and the rest stubbed), render_root, stage_dispatch and vector_copy
 * original/replaced in all eight combinations against the all-original
 * run. The callbacks' EBP/ESP depend on where 401d94 runs (they are not
 * compared; their EBX/ESI/EDI are). */
static int rr_real(unsigned index){
    const unsigned set[3]={index,ES2_HOOK_INDEX_stage_dispatch,ES2_HOOK_INDEX_vector_copy};
    BYTE saved[RR_IDS][5];int active[RR_IDS];if(rr_patch(saved,active,RR_REAL,1))return 1;
    int status=0;unsigned total=0,callbacks=0;uint64_t entered[3]={0,0,0};static rr_record expected[RR_MAX];
    rr_track=1;
    for(unsigned mode=0;mode<8&&!status;++mode)for(unsigned n=0;n<128&&!status;++n){
        uint32_t r[3],want[3],got[3];const uint32_t pattern=n&1?0xa7a7a7a7u:0x31313131u;rr_build(n*67u+mode*7u,r);
        BYTE before[RR_GLOBALS],after[RR_GLOBALS],actual[RR_GLOBALS];
        memcpy(rr_before,rr_arena,RR_SIZE);rr_globals(before,0);
        rr_reset();rr_track_run(mapped+function_hook_descriptor(index)->rva,pattern,want);
        const unsigned calls=rr_calls;memcpy(expected,rr_log,calls*sizeof(*expected));memcpy(rr_after,rr_arena,RR_SIZE);rr_globals(after,0);
        es2_hook hk[3];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<3;++j){o.mode=(mode>>j&1)?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(set[j]),&o,function_hook_extension(set[j])))return fail("render_root real install");}
        memcpy(rr_arena,rr_before,RR_SIZE);rr_globals(before,1);rr_reset();
        rr_track_run(mapped+function_hook_descriptor(index)->rva,pattern,got);rr_globals(actual,0);
        if(want[0]!=got[0]||want[1]!=got[1]||want[2]!=got[2]||calls!=rr_calls||memcmp(expected,rr_log,calls*sizeof(*expected))||
           memcmp(rr_after,rr_arena,RR_SIZE)||memcmp(after,actual,RR_GLOBALS)){
            fprintf(stderr,"real mode=%u\n",mode);status=rr_report("real",n,want,got,calls,expected,after,actual);}
        for(unsigned j=0;j<calls;++j)if(expected[j].id>=RR_CALLBACK)++callbacks;
        for(unsigned j=0;j<3;++j){if(mode>>j&1)entered[j]+=hk[j].calls;
            if(!es2_hook_uninstall(hk+j))return fail("render_root real uninstall");}
        rr_globals(before,1);++total;
    }
    rr_track=0;
    if(rr_patch(saved,active,0,0))return 1;
    if(!status&&(!entered[0]||!entered[1]||!entered[2]||!callbacks)){
        fprintf(stderr,"FAIL: %s real pass: entered %llu/%llu/%llu callbacks %u\n",current_function,(unsigned long long)entered[0],
            (unsigned long long)entered[1],(unsigned long long)entered[2],callbacks);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases over the real 401d94 and 40707c (render_root, stage_dispatch, vector_copy original/replaced in all 8 combinations; replaced entries %llu/%llu/%llu, %u stage callbacks), 31/a7 stack, EAX/EDX/ECX, calls with EBX/ESI/EDI and EBP/ESP, arena, globals\n",
        current_function,total,(unsigned long long)entered[0],(unsigned long long)entered[1],(unsigned long long)entered[2],callbacks);
    return status;
}
static int rr_root_cases(unsigned index,unsigned limit){
    BYTE old_globals[RR_GLOBALS];rr_globals(old_globals,0);
    BYTE saved[RR_IDS][5];int active[RR_IDS];
    if(rr_patch(saved,active,RR_ALL,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_RENDER_ROOT_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);memset(rr_paths,0,sizeof(rr_paths));rr_track=0;rr_hash_on=0;
    const uint32_t none[6]={0};
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t r[3];rr_build(n,r);status=rr_compare(index,n,none,r);}
    if(!limit)coverage_enable(0);current_case_valid=0;
    rr_globals(old_globals,1);
    if(!limit&&!status)status=rr_frame_probe(index,256);
    rr_globals(old_globals,1);
    if(rr_patch(saved,active,RR_ALL,0))return 1;
    if(!limit&&!status)status=rr_nested_budget();
    if(!limit&&!status)status=rr_floor_budget();
    if(!limit&&!status)status=rr_real(index);
    rr_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases (stub calls 401d94 %u, 45d490 %u, 45d50c %u, 42d870 %u, 48c2e4 %u, 46cb9c %u, 4327ac %u, 42841c %u, 42db18 %u, 40707c %u, 48c444 %u, 42e700 %u, 42883c %u, 41b130 %u, 433158 %u, 434520 %u, slot +8 %u, slot +10h %u); full return pair, ordered calls with entry registers, arena and globals\n",
        function_hook_descriptor(index)->name,cases,rr_paths[0],rr_paths[1],rr_paths[2],rr_paths[3],rr_paths[4],rr_paths[5],rr_paths[6],
        rr_paths[7],rr_paths[8],rr_paths[9],rr_paths[10],rr_paths[11],rr_paths[12],rr_paths[13],rr_paths[14],rr_paths[15],
        rr_paths[16]+rr_paths[17],rr_paths[18]+rr_paths[19]);
    return status;
}
/* stage_dispatch: the stage (low word 4, 5 or 6 in seven cases of eight,
 * the high word random in one of four), the table and count of rr_build. */
static void rr_dispatch_build(unsigned n,uint32_t a[6],uint32_t r[3]){
    rr_build(n,r);memset(a,0,24);
    const uint32_t k=random32();a[0]=(k%8?4u+k/8%3u:k>>3&0xffffu)|(k%4==0?random32()&0xffff0000u:0u);
}
static int rr_dispatch_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;static rr_record expected[RR_MAX];
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],want[2],got[2],pattern=poison?0xa7a7a7a7u:0x31313131u;rr_dispatch_build(n*41u+3u,a,r);
        BYTE before[RR_GLOBALS],after[RR_GLOBALS],actual[RR_GLOBALS],efpu[108],afpu[108];
        memcpy(rr_before,rr_arena,RR_SIZE);rr_globals(before,0);
        rr_reset();sg_poison_call(mapped+d->rva,a,pattern,want);es2_hook_fpu_save(efpu);
        const unsigned calls=rr_calls;memcpy(expected,rr_log,calls*sizeof(*expected));memcpy(rr_after,rr_arena,RR_SIZE);rr_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("stage_dispatch poison install");
        memcpy(rr_arena,rr_before,RR_SIZE);rr_globals(before,1);rr_reset();
        sg_poison_call(mapped+d->rva,a,pattern,got);es2_hook_fpu_save(afpu);rr_globals(actual,0);
        if(memcmp(want,got,8)||calls!=rr_calls||memcmp(expected,rr_log,calls*sizeof(*expected))||memcmp(rr_after,rr_arena,RR_SIZE)||
           memcmp(after,actual,RR_GLOBALS)||memcmp(efpu,afpu,12)||memcmp(efpu+28,afpu+28,80)||h.calls!=1){
            const uint32_t w[3]={want[0],want[1],0},g[3]={got[0],got[1],0};
            status=rr_report(fast?"installed fast":"installed preserving",n,w,g,calls,expected,after,actual);}
        if(!es2_hook_uninstall(&h))return fail("stage_dispatch poison uninstall");rr_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair, callbacks with EBX/ESI/EDI, table and globals\n",d->name,total);
    return status;
}
static int rr_dispatch_cases(unsigned index,unsigned limit){
    BYTE old_globals[RR_GLOBALS];rr_globals(old_globals,0);
    BYTE saved[RR_IDS][5];int active[RR_IDS];
    if(rr_patch(saved,active,RR_CALLBACKS,1))return 1;
    unsigned cases=case_budget(limit,1001024);
    int status=0;if(!limit)coverage_enable(1);memset(rr_paths,0,sizeof(rr_paths));rr_track=0;rr_hash_on=0;
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];rr_dispatch_build(n,a,r);status=rr_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);current_case_valid=0;
    rr_globals(old_globals,1);
    if(!limit&&!status)status=rr_dispatch_probes(index,256);
    rr_globals(old_globals,1);
    if(rr_patch(saved,active,RR_CALLBACKS,0))return 1;
    unsigned total=0;for(unsigned i=RR_CALLBACK;i<RR_IDS;++i)total+=rr_paths[i];
    if(!status)printf("PASS: %-20s %u cases (%u callback calls); full return pair, ordered callbacks with EAX..EDI, table and globals\n",
        function_hook_descriptor(index)->name,cases,total);
    return status;
}
/* vector_copy: D and S anywhere in the 4-KiB region (S within 16 bytes of
 * D in half the cases, any alignment); for the compare probe disjoint. */
static void rr_copy_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    (void)index;rr_seed=random32()^n;rr_stream=rr_seed;memset(a,0,24);
    for(unsigned i=0;i<RR_COPY_SIZE;i+=4)put32(rr_arena+RR_COPY+i,rr_next());
    for(unsigned i=0;i<3;++i)r[i]=rr_next();
    const uint32_t d=RR_COPY+0x20u+rr_pick(RR_COPY_SIZE-0x40u);
    a[0]=RR_ADDR(d);a[1]=n%2?RR_ADDR(d-16u+rr_pick(33)):RR_ADDR(RR_COPY+0x10u+rr_pick(RR_COPY_SIZE-0x20u));
}
static void rr_copy_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    rr_copy_build(index,n,a,r);a[0]=RR_ADDR(RR_COPY+rr_pick(0x7f0u));a[1]=RR_ADDR(RR_COPY+0x800u+rr_pick(0x7f0u));
}
static void rr_copy_state(BYTE *state,int restore){
    if(restore)memcpy(rr_arena+RR_COPY,state,RR_COPY_SIZE);else memcpy(state,rr_arena+RR_COPY,RR_COPY_SIZE);
}
static int rr_copy_cases(unsigned index,unsigned limit){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    if(limit&&compare_probe_installed(index))return compare_probe_run(index,limit,rr_copy_compare_build,rr_copy_state,RR_COPY_SIZE);
    static BYTE want_state[RR_COPY_SIZE],start[RR_COPY_SIZE];
    unsigned cases=case_budget(limit,1001024),overlaps=0;int status=0;
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;
        uint32_t a[6],r[3];rr_copy_build(index,n,a,r);memcpy(start,rr_arena+RR_COPY,RR_COPY_SIZE);
        const uint64_t want=weapons_original(index,a,r);memcpy(want_state,rr_arena+RR_COPY,RR_COPY_SIZE);
        memcpy(rr_arena+RR_COPY,start,RR_COPY_SIZE);
        const uint64_t got=x->replacement_context(a,d->user,r);
        if(a[1]-a[0]+11u<23u)++overlaps;
        if(want!=got||memcmp(want_state,rr_arena+RR_COPY,RR_COPY_SIZE)){
            fprintf(stderr,"FAIL: %s case %u D %08x S %08x pair %016llx/%016llx\n",d->name,n,a[0],a[1],(unsigned long long)want,(unsigned long long)got);status=1;}}
    current_case_valid=0;
    if(!limit&&!status){
        BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);__asm__ volatile("fninit; fld1; fldpi");unsigned total=0;
        for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<256&&!status;++n){
            uint32_t a[6],r[3],want[2],got[2],pattern=poison?0xa7a7a7a7u:0x31313131u;rr_copy_build(index,n,a,r);
            BYTE efpu[108],afpu[108];memcpy(start,rr_arena+RR_COPY,RR_COPY_SIZE);
            sg_poison_call(mapped+d->rva,a,pattern,want);es2_hook_fpu_save(efpu);memcpy(want_state,rr_arena+RR_COPY,RR_COPY_SIZE);
            es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
            es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,x))return fail("vector_copy poison install");
            memcpy(rr_arena+RR_COPY,start,RR_COPY_SIZE);
            sg_poison_call(mapped+d->rva,a,pattern,got);es2_hook_fpu_save(afpu);
            if(memcmp(want,got,8)||memcmp(want_state,rr_arena+RR_COPY,RR_COPY_SIZE)||memcmp(efpu,afpu,12)||memcmp(efpu+28,afpu+28,80)||h.calls!=1){
                fprintf(stderr,"FAIL: %s installed case %u pair %08x:%08x/%08x:%08x\n",d->name,n,want[1],want[0],got[1],got[0]);status=1;}
            if(!es2_hook_uninstall(&h))return fail("vector_copy poison uninstall");++total;
        }
        es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
        if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair and region\n",d->name,total);
    }
    if(!limit&&!status&&compare_probe_capable(index))status=compare_probe_run(index,0,rr_copy_compare_build,rr_copy_state,RR_COPY_SIZE);
    if(!status)printf("PASS: %-20s %u cases (%u overlapping); full return pair and region\n",d->name,cases,overlaps);
    return status;
}
static int render_root_cases(unsigned index,unsigned limit){
    unsigned bytes=0;for(unsigned i=0;i<sizeof(rr_global_list)/sizeof(rr_global_list[0]);++i)bytes+=rr_global_list[i].size;
    if(bytes!=RR_GLOBALS)return fail("render_root global list size");
    if(index==ES2_HOOK_INDEX_render_root)return rr_root_cases(index,limit);
    if(index==ES2_HOOK_INDEX_stage_dispatch)return rr_dispatch_cases(index,limit);
    return rr_copy_cases(index,limit);
}
