/* panel_frame group: the cockpit panel frame 4327ac (panel_frame)
 * (docs/reverse-engineering/hud.md, "cockpit panel frame as own code"). A 16
 * KiB arena holds the cockpit H, 24 panel objects (each [obj] = one of two
 * arena vtables whose +8 and +14h point at two pairs of slot stubs), the
 * targets T/T2, the subjects S/S2 and their side records; the image globals
 * 49b104 (byte), 49ef34, 4d2684, 4d1c78 and 4a9e5e (word) are set per case,
 * compared and restored. Per case the skip byte, the init/startup/panel-off
 * bytes, the ten elements (null in one of four), the six sub-objects, the port,
 * the target (0, T or T2), [H] and the entry EAX/EDX/ECX vary. The callees
 * 433770, 432924, 43408c, 434194, 4311e0, 431210, 433c54, 433174
 * (cockpit_valid), 4530fc, 4349ac, 49288c (dist_gate, under the real 433174
 * only) and the four slot targets are logging stubs (ordered calls with the
 * bridge's slot count, arguments and entry EAX/EDX/ECX; results from a per-case
 * stream, AL of 433174 zero in half the calls) that apply a side effect from
 * the same stream to H, an object's vtable or a global in about one call of
 * four, so a replacement that caches a value the original re-reads differs.
 * Direct cases compare the full EDX:EAX pair, the call log, the arena and the
 * globals, and for the first 4,096 cases the callee counters. Installed probes
 * run the hook through its entry shim (fast and preserving dispatch, 31/a7
 * stack, EAX..EDI distinct patterns): every stub also records EBX/ESI/EDI, EBP
 * and ESP relative to the caller's slot and a hash of the 256 bytes below its
 * ESP (the residue a callee would read), and one-slot stubs overwrite their
 * argument slot in one call of four (the original's POP ECX then reads it). The
 * real-callee pass runs 4327ac over the real 433174 with panel_frame and
 * cockpit_valid installed original/replaced in all four combinations against
 * the all-original run. Identifiers: pf_/PF_. */
enum { PF_SIZE=0x4000, PF_H=0x100, PF_OBJ=0x800, PF_OBJECTS=24, PF_VT=0x1000, PF_T=0x1200, PF_T2=0x1300,
    PF_S=0x1400, PF_S2=0x1500, PF_X=0x1600, PF_IDS=15, PF_MAX=48, PF_GLOBALS=1+4+4+4+2,
    PF_ALL=0x7fff, PF_REAL=PF_ALL&~(1u<<7) };
#define PF_BASE ((uint32_t)(uintptr_t)pf_arena)
#define PF_ADDR(o) (PF_BASE+(uint32_t)(o))
static BYTE pf_arena[PF_SIZE],pf_before[PF_SIZE],pf_after[PF_SIZE];
typedef struct pf_record { uint32_t id,slots,a[2],regs[3],cs[3],frame[2],window; } pf_record;
static pf_record pf_log[PF_MAX];
static unsigned pf_calls,pf_track,pf_clobbers,pf_paths[PF_IDS];
static uint32_t pf_seed,pf_stream;
/* Captured by the stubs at entry: EAX, EDX, ECX, EBX, ESI, EDI, EBP, ESP;
 * the 256 bytes below the entry ESP; the ECX a stub returns. */
__attribute__((used)) static uint32_t pf_cap[8],pf_ecx,pf_origin;
__attribute__((used)) static BYTE pf_window[256];
/* ids 0..10 patched callees; 11/12 slot +8 of vtable 0/1, 13/14 slot +14h. */
static const uint32_t pf_addr[PF_IDS]={0x433770,0x432924,0x43408c,0x434194,0x4311e0,0x431210,0x433c54,0x433174,
    0x4530fc,0x4349ac,0x49288c,0,0,0,0};
static const unsigned pf_arity[PF_IDS]={1,1,1,1,0,0,1,2,1,0,2,1,1,1,1};
static const uint32_t pf_subs[6]={0x1e1,0x1e5,0x1e9,0x1f1,0x1ed,0x1f9};
static uint32_t pf_next(void){
    uint32_t z=pf_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static void pf_reset(void){pf_calls=0;pf_stream=pf_seed;}
static void pf_globals(BYTE *out,int restore){
    static const struct { uint32_t rva,size; } r[]={{0x9b104,1},{0x9ef34,4},{0xd2684,4},{0xd1c78,4},{0xa9e5e,2}};
    BYTE *p=out;for(unsigned i=0;i<5;++i){if(restore)memcpy(mapped+r[i].rva,p,r[i].size);else memcpy(p,mapped+r[i].rva,r[i].size);p+=r[i].size;}
}
static uint32_t pf_object(void){return PF_ADDR(PF_OBJ+0x40u*(pf_next()%PF_OBJECTS));}
static uint32_t pf_target(void){switch(pf_next()%4){case 0:return 0;case 1:return PF_ADDR(PF_T2);default:return PF_ADDR(PF_T);}}
/* One callee side effect (about one call in four), drawn from the stream. */
static void pf_effect(void){
    uint32_t pick=pf_next();if(pick%4)return;
    BYTE *H=pf_arena+PF_H;
    switch(pick/4%10){
    case 0:H[0x245]=(BYTE)(pf_next()%2);break;
    case 1:H[0x20f]=(BYTE)(pf_next()%3==0);break;
    case 2:put32(H+0x70+4*(pf_next()%10),pf_next()%3?pf_object():0);break;
    case 3:put32(H+pf_subs[pf_next()%6],pf_object());break;
    case 4:put32(H+0x207,pf_object());break;
    case 5:put32((BYTE *)(uintptr_t)pf_object(),PF_ADDR(PF_VT+0x40u*(pf_next()%2)));break;
    case 6:put32(H+0x210,pf_target());break;
    case 7:put32(H,pf_next());break;
    case 8:mapped[0x9b104]=(BYTE)pf_next();break;
    default:H[0x23e]=(BYTE)pf_next();break;
    }
}
static uint32_t pf_hash(const BYTE *p,unsigned n){uint32_t h=2166136261u;for(unsigned i=0;i<n;++i)h=(h^p[i])*16777619u;return h;}
__attribute__((used,noinline)) static uint64_t __cdecl pf_log_impl(unsigned id){
    if(pf_calls==PF_MAX){fail("panel_frame synthetic call overflow");ExitProcess(1);}
    uint32_t *a=(uint32_t *)(uintptr_t)(pf_cap[7]+4u);
    pf_record *v=pf_log+pf_calls++;memset(v,0,sizeof(*v));v->id=id;v->slots=weapons_bridge_slots(a,pf_arity[id]);
    for(unsigned j=0;j<pf_arity[id];++j)v->a[j]=a[j];
    for(unsigned j=0;j<3;++j)v->regs[j]=pf_cap[j];
    if(pf_track){for(unsigned j=0;j<3;++j)v->cs[j]=pf_cap[3+j];
        v->frame[0]=pf_cap[6]-pf_origin;v->frame[1]=pf_cap[7]-pf_origin;v->window=pf_hash(pf_window,256);}
    uint32_t eax=pf_next(),edx=pf_next();pf_ecx=pf_next();
    if(id==7&&pf_next()%2)eax&=0xffffff00u;   /* cockpit_valid: AL zero clears the target */
    if(pf_track&&pf_arity[id]==1&&pf_next()%4==0){a[0]=pf_next();++pf_clobbers;}   /* the callee changes its slot */
    pf_effect();
    return (uint64_t)edx<<32|eax;
}
#define PF_STUB(n) __attribute__((naked)) static void pf_stub_##n(void){__asm__ volatile( \
    "movl %eax,_pf_cap\n\tmovl %edx,_pf_cap+4\n\tmovl %ecx,_pf_cap+8\n\tmovl %ebx,_pf_cap+12\n\t" \
    "movl %esi,_pf_cap+16\n\tmovl %edi,_pf_cap+20\n\tmovl %ebp,_pf_cap+24\n\tmovl %esp,_pf_cap+28\n\t" \
    "leal -256(%esp),%esi\n\tmovl $_pf_window,%edi\n\tmovl $64,%ecx\n\tcld\n\trep movsl\n\t" \
    "movl _pf_cap+16,%esi\n\tmovl _pf_cap+20,%edi\n\t" \
    "pushl $" #n "\n\tcalll _pf_log_impl\n\taddl $4,%esp\n\tmovl _pf_ecx,%ecx\n\tret\n\t");}
PF_STUB(0) PF_STUB(1) PF_STUB(2) PF_STUB(3) PF_STUB(4) PF_STUB(5) PF_STUB(6) PF_STUB(7) PF_STUB(8) PF_STUB(9)
PF_STUB(10) PF_STUB(11) PF_STUB(12) PF_STUB(13) PF_STUB(14)
#undef PF_STUB
static void *pf_stubs[PF_IDS]={pf_stub_0,pf_stub_1,pf_stub_2,pf_stub_3,pf_stub_4,pf_stub_5,pf_stub_6,pf_stub_7,
    pf_stub_8,pf_stub_9,pf_stub_10,pf_stub_11,pf_stub_12,pf_stub_13,pf_stub_14};
static int pf_patch(BYTE saved[PF_IDS][5],int active[PF_IDS],unsigned which,int on){
    for(unsigned i=0;i<PF_IDS;++i){
        if(!pf_addr[i]){active[i]=0;continue;}
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(pf_addr[i]-0x400000u,pf_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(pf_addr[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
/* A target or subject record: position, side record +45h, the index word
 * +4Bh (0..15, so S+C2h+index stays in the record), the flag bytes. */
static void pf_record_object(uint32_t o,uint32_t side){
    BYTE *p=pf_arena+o;
    put32(p+0x45,PF_ADDR(side));pf_arena[side+0x12]=(BYTE)(pf_next()%2);
    put16(p+0x4b,(uint16_t)(pf_next()%16));
    p[0xa4]=(BYTE)(pf_next()%4==0);p[0x99]=(BYTE)(pf_next()%4==0);p[0x95]=(BYTE)(pf_next()%2);
    for(unsigned i=0;i<16;++i)p[0xc2+i]=(BYTE)(pf_next()%2);
}
/* Builds one state; a[0] = H. */
static void pf_build(unsigned n,uint32_t a[6],uint32_t r[3]){
    pf_seed=random32();pf_stream=pf_seed^n;random_bytes(pf_arena,PF_SIZE);memset(a,0,24);for(unsigned i=0;i<3;++i)r[i]=pf_next();
    for(unsigned v=0;v<2;++v){BYTE *t=pf_arena+PF_VT+0x40u*v;
        put32(t+8,(uint32_t)(uintptr_t)pf_stubs[11+v]);put32(t+0x14,(uint32_t)(uintptr_t)pf_stubs[13+v]);}
    for(unsigned k=0;k<PF_OBJECTS;++k)put32(pf_arena+PF_OBJ+0x40u*k,PF_ADDR(PF_VT+0x40u*(pf_next()%2)));
    BYTE *H=pf_arena+PF_H;
    mapped[0x9b104]=(BYTE)(pf_next()%16==0?pf_next()|1u:0u);
    put32(mapped+0x9ef34,pf_next()%2?0:pf_next());put32(mapped+0xd2684,PF_ADDR(PF_S2));
    put32(mapped+0xd1c78,pf_next());put16(mapped+0xa9e5e,(uint16_t)(pf_next()%2?1:pf_next()));
    H[0x23e]=(BYTE)(pf_next()%3?pf_next()|1u:0u);H[0x245]=(BYTE)(pf_next()%3?pf_next()|1u:0u);
    H[0x20f]=(BYTE)(pf_next()%8==0?pf_next()|1u:0u);
    for(unsigned i=0;i<10;++i)put32(H+0x70+4*i,pf_next()%4?pf_object():0);
    for(unsigned i=0;i<6;++i)put32(H+pf_subs[i],pf_object());
    put32(H+0x207,pf_object());put32(H+0x210,pf_target());put32(H+0x203,PF_ADDR(PF_S));
    pf_record_object(PF_T,PF_X);pf_record_object(PF_T2,PF_X+0x40);pf_record_object(PF_S,PF_X+0x80);pf_record_object(PF_S2,PF_X+0xc0);
    a[0]=PF_ADDR(PF_H);
    pf_stream=pf_seed;   /* the stubs' stream restarts here in both runs */
}
static unsigned pf_hits(unsigned lo,unsigned hi){
    unsigned h=0;for(unsigned j=0;j<pf_calls;++j)if(pf_log[j].id>=lo&&pf_log[j].id<=hi)++h;
    return h;
}
static int pf_counter_check(const callee_counter *c,uint64_t delta){
    uint64_t want=0;
    if(c->event==ES2_COLLIDE_SLOT)want=c->key==8?pf_hits(11,12):pf_hits(13,14);
    else for(unsigned i=0;i<11;++i)if(pf_addr[i]==c->key)want=pf_hits(i,i);
    return delta!=want;
}
static int pf_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const pf_record *e,const BYTE *want,const BYTE *got){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,pf_calls);
    for(unsigned j=0;j<calls&&j<pf_calls&&e;++j)if(memcmp(e+j,pf_log+j,sizeof(*e))){
        const uint32_t *x=(const uint32_t*)(e+j),*y=(const uint32_t*)(pf_log+j);
        for(unsigned k=0;k<sizeof(*e)/4;++k)if(x[k]!=y[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,e[j].id,k,x[k],y[k]);break;}
    for(unsigned j=0;j<PF_SIZE;++j)if(pf_after[j]!=pf_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,pf_after[j],pf_arena[j]);break;}
    for(unsigned j=0;j<PF_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);break;}
    return 1;
}
static int pf_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[PF_GLOBALS],after[PF_GLOBALS],actual[PF_GLOBALS];static pf_record expected[PF_MAX];
    memcpy(pf_before,pf_arena,PF_SIZE);pf_globals(before,0);
    pf_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);
    unsigned calls=pf_calls;memcpy(expected,pf_log,calls*sizeof(*expected));memcpy(pf_after,pf_arena,PF_SIZE);pf_globals(after,0);
    for(unsigned j=0;j<calls;++j)pf_paths[expected[j].id]++;
    memcpy(pf_arena,pf_before,PF_SIZE);pf_globals(before,1);pf_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16]={0};
    for(unsigned i=0;i<count&&i<16;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);pf_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count&&i<16;++i)if(pf_counter_check(c+i,c[i].count-counts[i]))return fail("panel_frame callee counter delta");
    if(pair[0]!=pair[1]||calls!=pf_calls||memcmp(expected,pf_log,calls*sizeof(*expected))||memcmp(pf_after,pf_arena,PF_SIZE)||
       memcmp(after,actual,PF_GLOBALS))
        return pf_report("direct",n,pair,calls,expected,after,actual);
    pf_globals(before,1);
    return 0;
}
/* Calls target with one slot over a 4-KiB fill, EAX..EDI distinct pattern
 * words; keeps the slot address in pf_origin and returns EDX:EAX in out. */
__attribute__((naked,noinline)) static void pf_track_run(void *target,const uint32_t *a,uint32_t pattern,uint32_t out[2]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 16(%ebp),%eax\n\tmovl $1024,%ecx\n\tcld\n\trep stosl\n\taddl $4092,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl (%esi)\n\tmovl %esp,_pf_origin\n\tmovl 16(%ebp),%eax\n\t"
        "movl %eax,%edx\n\txorl $0x01010101,%edx\n\tmovl %eax,%ecx\n\txorl $0x02020202,%ecx\n\tmovl %eax,%ebx\n\txorl $0x04040404,%ebx\n\t"
        "movl %eax,%esi\n\txorl $0x08080808,%esi\n\tmovl %eax,%edi\n\txorl $0x10101010,%edi\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%ebx\n\tmovl %eax,(%ebx)\n\tmovl %edx,4(%ebx)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* Installed probes through the entry shim: the original 4327ac and the
 * installed replacement must agree in EDX:EAX, the records (with
 * EBX/ESI/EDI, EBP/ESP relative to the slot and the residue hash below
 * each callee's ESP), the arena and the globals. */
static int pf_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    pf_track=1;pf_clobbers=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],want[2],got[2],pattern=poison?0xa7a7a7a7u:0x31313131u;pf_build(n*41u+3u,a,r);
        BYTE before[PF_GLOBALS],after[PF_GLOBALS],actual[PF_GLOBALS],expected_fpu[108],actual_fpu[108];static pf_record expected[PF_MAX];
        memcpy(pf_before,pf_arena,PF_SIZE);pf_globals(before,0);
        pf_reset();pf_track_run(mapped+d->rva,a,pattern,want);es2_hook_fpu_save(expected_fpu);
        unsigned calls=pf_calls;memcpy(expected,pf_log,calls*sizeof(*expected));memcpy(pf_after,pf_arena,PF_SIZE);pf_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("panel_frame poison install");
        memcpy(pf_arena,pf_before,PF_SIZE);pf_globals(before,1);pf_reset();
        pf_track_run(mapped+d->rva,a,pattern,got);es2_hook_fpu_save(actual_fpu);pf_globals(actual,0);
        uint64_t pair[2]={(uint64_t)want[1]<<32|want[0],(uint64_t)got[1]<<32|got[0]};
        if(pair[0]!=pair[1]||calls!=pf_calls||memcmp(expected,pf_log,calls*sizeof(*expected))||memcmp(pf_after,pf_arena,PF_SIZE)||
           memcmp(after,actual,PF_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=pf_report(fast?"installed fast":"installed preserving",n,pair,calls,expected,after,actual);
        if(!es2_hook_uninstall(&h))return fail("panel_frame poison uninstall");pf_globals(before,1);++total;
    }
    pf_track=0;
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status&&!pf_clobbers){fprintf(stderr,"FAIL: panel_frame installed probes: no changed argument slot was observed\n");status=1;}
    if(!status)printf("PASS: %s %u installed cases through the entry shim, fast/preserving dispatch, 31/a7 stack, per-call EBX/ESI/EDI, EBP/ESP, residue below each callee, changed argument slots (%u in both runs), nonempty x87, full pair/calls/arena/globals\n",
        d->name,total,pf_clobbers);
    return status;
}
/* Real-callee pass: 4327ac over the real 433174 (49288c and the rest
 * stubbed), panel_frame and cockpit_valid installed original/replaced in
 * all four combinations, against the all-original run. */
static int pf_real(unsigned index){
    const unsigned set[2]={index,ES2_HOOK_INDEX_cockpit_valid};
    BYTE saved[PF_IDS][5];int active[PF_IDS];if(pf_patch(saved,active,PF_REAL,1))return 1;
    int status=0;unsigned total=0,valid=0;uint64_t entered=0,nested=0;
    for(unsigned mode=0;mode<4&&!status;++mode)for(unsigned n=0;n<256&&!status;++n){
        uint32_t a[6],r[3],want[2],got[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;pf_build(n*67u+mode*7u,a,r);
        BYTE before[PF_GLOBALS],after[PF_GLOBALS],actual[PF_GLOBALS];static pf_record expected[PF_MAX];
        memcpy(pf_before,pf_arena,PF_SIZE);pf_globals(before,0);
        pf_reset();sg_poison_call(mapped+function_hook_descriptor(index)->rva,a,pattern,want);
        unsigned calls=pf_calls;memcpy(expected,pf_log,calls*sizeof(*expected));memcpy(pf_after,pf_arena,PF_SIZE);pf_globals(after,0);
        es2_hook hk[2];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<2;++j){o.mode=(mode>>j&1)?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(set[j]),&o,function_hook_extension(set[j])))return fail("panel_frame real install");}
        memcpy(pf_arena,pf_before,PF_SIZE);pf_globals(before,1);pf_reset();
        sg_poison_call(mapped+function_hook_descriptor(index)->rva,a,pattern,got);pf_globals(actual,0);
        uint64_t pair[2]={(uint64_t)want[1]<<32|want[0],(uint64_t)got[1]<<32|got[0]};
        if(pair[0]!=pair[1]||calls!=pf_calls||memcmp(expected,pf_log,calls*sizeof(*expected))||memcmp(pf_after,pf_arena,PF_SIZE)||
           memcmp(after,actual,PF_GLOBALS)){
            fprintf(stderr,"real mode=%u\n",mode);status=pf_report("real",n,pair,calls,expected,after,actual);}
        if(mode&1)entered+=hk[0].calls;
        if(mode&2)nested+=hk[1].calls;
        for(unsigned j=0;j<calls;++j)if(expected[j].id==10)++valid;
        for(unsigned j=0;j<2;++j)if(!es2_hook_uninstall(hk+j))return fail("panel_frame real uninstall");
        pf_globals(before,1);++total;
    }
    if(pf_patch(saved,active,0,0))return 1;
    if(!status&&(!entered||!nested||!valid)){fprintf(stderr,"FAIL: %s real pass: entered %llu nested %llu dist_gate %u\n",current_function,
        (unsigned long long)entered,(unsigned long long)nested,valid);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases over the real 433174 (panel_frame and cockpit_valid original/replaced in all 4 combinations; %llu replaced entries, %llu replaced cockpit_valid entries, %u dist_gate calls), 31/a7 stack, pair/calls/arena/globals\n",
        current_function,total,(unsigned long long)entered,(unsigned long long)nested,valid);
    return status;
}
static int panel_frame_cases(unsigned index,unsigned limit){
    BYTE old_globals[PF_GLOBALS];pf_globals(old_globals,0);
    BYTE saved[PF_IDS][5];int active[PF_IDS];
    if(pf_patch(saved,active,PF_ALL,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_PANEL_FRAME_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);memset(pf_paths,0,sizeof(pf_paths));pf_track=0;
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];pf_build(n,a,r);status=pf_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);current_case_valid=0;
    pf_globals(old_globals,1);
    if(!limit&&!status)status=pf_installed_probes(index,256);
    pf_globals(old_globals,1);
    if(pf_patch(saved,active,PF_ALL,0))return 1;
    if(!limit&&!status)status=pf_real(index);
    pf_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases (stub calls 433770 %u, 432924 %u, 43408c %u, 434194 %u, 4311e0 %u, 431210 %u, 433c54 %u, 433174 %u, 4530fc %u, 4349ac %u, slot +8 %u, slot +14h %u); full return pair, ordered calls with entry registers, arena and globals\n",
        function_hook_descriptor(index)->name,cases,pf_paths[0],pf_paths[1],pf_paths[2],pf_paths[3],pf_paths[4],pf_paths[5],pf_paths[6],pf_paths[7],
        pf_paths[8],pf_paths[9],pf_paths[11]+pf_paths[12],pf_paths[13]+pf_paths[14]);
    return status;
}
