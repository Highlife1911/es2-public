/* Flyer flight model 466b58 (flyer.md). Direct cases run the original body and
 * the core against logging stubs for every game callee (477904 and the eight
 * hooked leaves, patched in the private image): ordered argument slots, entry
 * EAX/EDX/ECX, payloads by content (the matrix_angles payload includes byte 19
 * of the composed matrix N); values in the worker's stack window are masked.
 * The private leaves 466a88, 466ae0, 466b20, 467b28 and the helpers 47e08d,
 * 47e0c0, 47f058 run as original code there. Stubs return results of every
 * flavour around the clamp limits and the zero tests, and between calls change
 * the words the original rereads (the pose cache word, the pose pointer S+20h,
 * words of C, S, M, F and the pose). The original is called from an own caller
 * that fills the stack with a pattern and plants two distinct residue bytes at
 * entry ESP-191h/-1A5h; the replacement receives them in CL/CH as from the
 * entry shim (the original gets the same ECX, which only 477904 sees). Then
 * installed 31/a7 poison probes through the shim and a real-callee pass (real
 * 477904 on randomized device globals, real time scale) with the eight nested
 * hooks in rotating modes. */
typedef struct fl_record { uint32_t id,args[3],regs[3],input[12],eax,edx; } fl_record;
enum { FL_SIZE=0x300, FL_C=0x20, FL_S=0x40, FL_M=0xa0, FL_F=0xe0, FL_A=0x100, FL_O=FL_A+0xc, FL_A2=0x180, FL_O2=FL_A2+0xc,
    FL_DEV=0x200, FL_MAX=96, FL_IDS=9, FL_GLOBALS=31 };
#define FL_BASE ((uint32_t)(uintptr_t)fl_arena)
static BYTE fl_arena[FL_SIZE],fl_before[FL_SIZE],fl_after[FL_SIZE];
static fl_record fl_log[FL_MAX];
static unsigned fl_calls;
static uint32_t fl_seed,fl_stream;
__attribute__((used)) static uint32_t fl_ecx;
/* 0 477904, 1 movement_step, 2 multiply_q8, 3 multiply_q10, 4 heading_cos,
 * 5 euler_matrix, 6 rotate_point, 7 matrix_transpose, 8 matrix_angles. */
static const uint32_t fl_addresses[FL_IDS]={0x477904,0x467924,0x47e0a0,0x47e0b0,0x47dff0,0x47ebb8,0x480304,0x47df19,0x47f9a0};
static const unsigned fl_counts[FL_IDS]={0,1,2,2,1,2,3,1,2};
static uint32_t fl_next(void){
    uint32_t z=fl_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *fl_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int fl_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static uint32_t fl_mask(uint32_t value){return fl_stack(value)?0:value;}
/* A word near the clamp limits of the model block or the zero tests. */
static uint16_t fl_edge_word(uint32_t pick){
    static const unsigned limits[]={0,2,4,6,8};
    const uint16_t l=get16(fl_arena+FL_M+limits[pick%5]);
    switch(pick/5%8){
    case 0:return l;case 1:return (uint16_t)(l-1u);case 2:return (uint16_t)(0u-l);case 3:return (uint16_t)(1u-l);
    case 4:return 0;case 5:return 0x8000;case 6:return 0x7fff;default:return (uint16_t)(fl_next()%2400u-1200u);
    }
}
/* Between calls: change a word the original rereads later. The pose
 * pointer switches only between the two poses; S+20h..23h is never
 * written otherwise. */
static void fl_perturb(uint32_t pick){
    switch(pick%8){
    case 0:put16(fl_arena+FL_O+0x26,pick&8?0:1);put16(fl_arena+FL_O2+0x26,pick&16?0:1);break;
    case 1:put32(fl_arena+FL_S+0x20,FL_BASE+(pick&8?FL_O2:FL_O));break;
    case 2:put16(fl_arena+FL_C+2*(pick/8%10),(uint16_t)fl_next());break;
    case 3:put16(fl_arena+FL_M+2*(pick/8%27),(uint16_t)(fl_next()%3000u));break;
    case 4:put32(fl_arena+FL_F+4*(pick/8%4),pick&0x100?fl_next():0);break;
    case 5:{unsigned k=pick/8%39;if(k>=16&&k<18)k+=2;put16(fl_arena+FL_S+2*k,(uint16_t)fl_next());break;}
    case 6:put16(fl_arena+(pick&8?FL_O2:FL_O)+2*(pick/16%3),fl_edge_word(pick>>8));break;
    default:put32(fl_arena+FL_O+0x22,get32(fl_arena+FL_O+0x22)+fl_next()%8000u-4000u);break;
    }
}
__attribute__((used,noinline)) static uint64_t __cdecl fl_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    uint32_t eax=fl_next(),edx=fl_next(),pick=fl_next();fl_ecx=fl_next();
    if(fl_calls==FL_MAX){fail("flyer synthetic call overflow");ExitProcess(1);}
    fl_record *v=fl_log+fl_calls++;memset(v,0,sizeof(*v));v->id=id;
    for(unsigned k=0;k<fl_counts[id];++k)v->args[k]=fl_mask(a[k]);
    for(unsigned j=0;j<3;++j)v->regs[j]=fl_mask(regs[j]);
    switch(id){
    case 0: /* device state: the arena block */
        eax=FL_BASE+FL_DEV;
        break;
    case 1: /* movement_step: the word, an edge word or anything */
        eax=pick%4<2?(eax&0xffff0000u)|(uint16_t)a[0]:pick%4==2?(eax&0xffff0000u)|fl_edge_word(pick>>2):eax;
        break;
    case 2:case 3: /* Q8/Q10 products: zero low word, the product, edges, the 8000h roll sum */
        switch(pick%6){
        case 0:eax&=0xffff0000u;break;
        case 1:eax=(uint32_t)((int64_t)(int32_t)a[0]*(int32_t)a[1]>>(id==2?8:10));break;
        case 2:eax=(uint32_t)(int32_t)(int16_t)fl_edge_word(pick>>3);break;
        case 3:eax=(eax&0xffff0000u)|(uint16_t)(get16(fl_arena+FL_S+0x1a)-0x8000u);break;
        case 4:eax=fl_next()%6000u-3000u;break;
        default:break;
        }
        break;
    case 4: /* heading_cos */
        if(pick%3==0)eax=(eax&0xffff0000u)|(pick&8?0x4000u:0xc000u);
        break;
    case 5: /* Euler: angles in; nine words and a mode byte out, never byte 19 */
        for(unsigned j=0;j<3;++j)v->input[j]=get16(fl_host(a[0])+2*j);
        for(unsigned j=0;j<18;j+=2)put16(fl_host(a[1])+j,(uint16_t)(pick%3?fl_next()%0x8001u-0x4000u:fl_next()));
        {static const BYTE modes[]={0,1,2,2,0xff,3};fl_host(a[1])[18]=modes[pick/3%6];}
        break;
    case 6: /* rotate_point: point and matrix by content; point out */
        for(unsigned j=0;j<3;++j)v->input[j]=get32(fl_host(a[0])+4*j);
        for(unsigned j=0;j<5;++j)v->input[3+j]=get32(fl_host(a[1])+4*j);
        {uint32_t out[3];for(unsigned j=0;j<3;++j)out[j]=pick%4==0?v->input[j]:pick%4==1?fl_next()%4000u-2000u:fl_next();
         for(unsigned j=0;j<3;++j)put32(fl_host(a[2])+4*j,out[j]);}
        break;
    case 7: /* transpose: matrix by content; some words changed */
        for(unsigned j=0;j<5;++j)v->input[j]=get32(fl_host(a[0])+4*j);
        if(pick%2)put16(fl_host(a[0])+2*(pick/2%9),(uint16_t)fl_next());
        break;
    case 8: /* matrix_angles: the 20 bytes of N (byte 19 = residue), three words out */
        for(unsigned j=0;j<5;++j)v->input[j]=get32(fl_host(a[0])+4*j);
        for(unsigned j=0;j<3;++j)put16(fl_host(a[1])+2*j,(uint16_t)(pick%4?fl_next():fl_edge_word(fl_next())));
        break;
    default:fail("flyer stub id");ExitProcess(1);
    }
    if(pick%16>=12)fl_perturb(fl_next());
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define FL_STUB(n) __attribute__((naked)) static void fl_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _fl_stub_impl\n\taddl $24,%esp\n\tmovl _fl_ecx,%ecx\n\tret\n\t");}
FL_STUB(0) FL_STUB(1) FL_STUB(2) FL_STUB(3) FL_STUB(4) FL_STUB(5) FL_STUB(6) FL_STUB(7) FL_STUB(8)
#undef FL_STUB
static void *fl_stubs[FL_IDS]={fl_stub_0,fl_stub_1,fl_stub_2,fl_stub_3,fl_stub_4,fl_stub_5,fl_stub_6,fl_stub_7,fl_stub_8};
/* Globals: the mean static 4d3b58 and its guard 4a087c, the controls byte
 * 4d1f52, the time scale 4d3b64, the device state 6bb6a8 and the inputs of
 * 477904 (6bb6a4, 6bb3b4, 6bb548, 6bb3d8). */
static void fl_globals(BYTE *out,int restore){
    static const unsigned offsets[]={0xd3b58,0xa087c,0xd1f52,0xd3b64,0x2bb6a8,0x2bb6a4,0x2bb3b4,0x2bb548,0x2bb3d8},
        sizes[]={6,1,1,2,8,4,4,4,1};
    for(unsigned i=0;i<9;++i){if(restore)memcpy(mapped+offsets[i],out,sizes[i]);else memcpy(out,mapped+offsets[i],sizes[i]);out+=sizes[i];}
}
static void fl_reset(void){fl_calls=0;fl_stream=fl_seed;}
static uint16_t fl_pick_word(const uint16_t *edges,unsigned count){
    return random32()%3?edges[random32()%count]:(uint16_t)random32();
}
/* A pose: angles at the roll/pitch edges, a matrix of any mode class, the
 * cache word, a height around the ceiling of the model block. */
static void fl_pose(BYTE *o,uint32_t ground){
    static const uint16_t pitches[]={0,1,0xffff,0x200,0xfe00,0x4000,0xc000,0x8000,0x7fff};
    static const uint16_t rolls[]={0,1,0xffff,0x8000,0x7fff,0x1000,0x0fff,0xf000,0xf001,0x4000,0x3fff,0xc000,0xc001,0x300,0xfd00};
    put16(o,fl_pick_word(pitches,9));put16(o+2,fl_pick_word(rolls,15));put16(o+4,(uint16_t)random32());
    for(unsigned j=0;j<18;j+=2)put16(o+6+j,(uint16_t)(random32()%2?random32()%0x8001u-0x4000u:random32()));
    static const BYTE modes[]={0,1,2,0xff,3,0x80};o[6+18]=modes[random32()%6];
    static const uint16_t caches[]={0,1,0,1,0x100,0xffff};put16(o+0x26,caches[random32()%6]);
    const int32_t ceiling=(int32_t)get32(fl_arena+FL_M+0x26);
    put32(o+0x22,ground+(uint32_t)ceiling+(random32()%4?random32()%6000u-3000u:random32()%200000u-100000u));
}
static void fl_build(unsigned n,uint32_t a[6],uint32_t r[3],uint32_t *residue){
    uint32_t b=FL_BASE;fl_seed=random32();random_bytes(fl_arena,FL_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    *residue=random32();r[2]=(r[2]&0xffff0000u)|(*residue&0xffffu);
    BYTE *c=fl_arena+FL_C,*s=fl_arena+FL_S,*m=fl_arena+FL_M,*f=fl_arena+FL_F;
    /* Model block: rate limits and gains, shifts 0..40, speeds, slope. */
    for(unsigned j=0;j<12;j+=2)put16(m+j,(uint16_t)(random32()%8?random32()%1600u:random32()));
    put32(m+0xe,random32()%3?random32()%0x40000u:random32());
    put32(m+0x12,random32()%4?random32()%1200u:random32());
    for(unsigned j=0x16;j<0x22;j+=4)put32(m+j,random32()%4?random32()%20u:random32()%2?random32()%64u:random32());
    put32(m+0x22,random32()%200000u);put32(m+0x26,random32()%4?random32()%20000u:random32());
    put32(m+0x2a,random32()%4?random32()%3000u:random32());put32(m+0x2e,random32()%4?random32()%1000u:random32());
    put32(m+0x32,random32()%4?random32()%1024u:random32());
    /* Control record: axes zero, small or anything; lever words that clamp or wrap. */
    static const uint32_t axes[]={0,0,1,0xffffffffu,0x80,0xffffff80u,0x100,0xffffff00u};
    for(unsigned j=0;j<12;j+=4)put32(c+j,random32()%3?axes[random32()%8]:random32()%2?random32()%600u-300u:random32());
    static const uint32_t levers[]={0,0,0x7f,0x80,0x81,0xffffff80u,0xffffff7fu,0x1000,0x1080,0xffff0090u,1,0xffffffffu};
    put32(c+0xc,random32()%3?levers[random32()%12]:random32()%2?random32()%0x2000u-0x1000u:random32());
    const uint32_t ground=random32()%3?random32()%20000u:random32();put32(c+0x10,ground);
    /* Failure flags: mostly clear, any nonzero bit pattern otherwise. */
    for(unsigned j=0;j<16;j+=4)put32(f+j,random32()%4?0:random32()%2?0x10000u:random32()|1u);
    /* Flight state: velocities, rates, throttle, the pose pointer. */
    for(unsigned j=0;j<12;j+=4)put32(s+j,random32()%3?random32()%4000u-2000u:random32());
    if(random32()%4==0)put32(s+4,random32()%3000u);
    static const uint16_t rates[]={0,1,0xffff,0x8000,0x7fff,0x200,0xfe00,0x10,0xfff0};
    for(unsigned j=0x18;j<0x1e;j+=2)put16(s+j,random32()%3?fl_pick_word(rates,9):(uint16_t)(random32()%2000u-1000u));
    static const uint16_t throttles[]={0,0x200,0x400,0xfc00,0x3ff,0xfc01,0x8000,0x7fff};
    put16(s+0x1e,fl_pick_word(throttles,8));
    put32(s+0x20,b+(random32()%8?FL_O:FL_O2));
    fl_pose(fl_arena+FL_O,ground);fl_pose(fl_arena+FL_O2,ground);
    /* Device byte +4 and controls byte 1Ah (lever mode = both). */
    static const BYTE devices[]={0,1,0x80};fl_arena[FL_DEV+4]=devices[random32()%3];
    static const BYTE controls[]={2,2,0,3,0x82};mapped[0xd1f52]=controls[random32()%5];
    static const BYTE guards[]={0,1,0xff};mapped[0xa087c]=guards[random32()%3];
    random_bytes(mapped+0xd3b58,6);
    a[0]=b+FL_C;a[1]=b+FL_S;a[2]=b+FL_M;a[3]=b+FL_F;a[4]=1;
    (void)n;
}
/* Own caller: a patterned stack with the two residue bytes planted at the
 * entry ESP-191h and ESP-1A5h, five argument slots, entry EAX/EDX/ECX. */
__attribute__((naked)) static void fl_call(void *target,const uint32_t *a,uint32_t *registers,uint32_t residue,uint32_t pattern){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 24(%ebp),%eax\n\tmovl $1024,%ecx\n\trep stosl\n\taddl $3840,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 16(%esi)\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 20(%ebp),%eax\n\tmovb %al,-405(%esp)\n\tmovb %ah,-425(%esp)\n\t"
        "movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tcall *8(%ebp)\n\t"
        "movl 16(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static int fl_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const fl_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,fl_calls);
    for(unsigned j=0;j<calls&&j<fl_calls&&expected;++j)if(memcmp(expected+j,fl_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(fl_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<FL_SIZE;++j)if(fl_after[j]!=fl_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,fl_after[j],fl_arena[j]);break;}
    return 1;
}
static unsigned fl_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<fl_calls;++j)if(fl_addresses[fl_log[j].id]==c->key)++hits;
    return hits;
}
static int fl_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r,uint32_t residue){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[FL_GLOBALS],after[FL_GLOBALS],actual[FL_GLOBALS];static fl_record expected[FL_MAX];
    memcpy(fl_before,fl_arena,FL_SIZE);fl_globals(before,0);
    uint32_t regs[3]={r[0],r[1],r[2]};
    fl_reset();fl_call(mapped+d->rva,a,regs,residue,random32());
    uint64_t pair[2];pair[0]=(uint64_t)regs[1]<<32|regs[0];unsigned calls=fl_calls;memcpy(expected,fl_log,calls*sizeof(*expected));
    memcpy(fl_after,fl_arena,FL_SIZE);fl_globals(after,0);
    memcpy(fl_arena,fl_before,FL_SIZE);fl_globals(before,1);fl_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);fl_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=fl_expected_hits(c+i))return fail("flyer callee counter delta");
    if(pair[0]!=pair[1]||calls!=fl_calls||memcmp(expected,fl_log,calls*sizeof(*expected))||memcmp(fl_after,fl_arena,FL_SIZE)||
       memcmp(after,actual,FL_GLOBALS)){
        for(unsigned j=0;j<FL_GLOBALS;++j)if(after[j]!=actual[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,after[j],actual[j]);break;}
        return fl_report("direct",n,pair,calls,expected);
    }
    return 0;
}
static int fl_patch(BYTE saved[FL_IDS][5],int active[FL_IDS],int on){
    for(unsigned i=0;i<FL_IDS;++i){
        if(on){active[i]=0;if(actor_patch(fl_addresses[i]-0x400000u,fl_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(fl_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
/* Installed through the entry shim: the poison caller leaves the pattern
 * byte at both residue offsets, the shim reads them into CL/CH. */
static int fl_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],residue,expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;fl_build(n,a,r,&residue);
        BYTE before[FL_GLOBALS],after[FL_GLOBALS],actual[FL_GLOBALS],expected_fpu[108],actual_fpu[108];static fl_record expected[FL_MAX];
        memcpy(fl_before,fl_arena,FL_SIZE);fl_globals(before,0);
        fl_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=fl_calls;memcpy(expected,fl_log,calls*sizeof(*expected));memcpy(fl_after,fl_arena,FL_SIZE);fl_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("flyer poison install");
        memcpy(fl_arena,fl_before,FL_SIZE);fl_globals(before,1);fl_reset();sg_poison_call(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);fl_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=fl_calls||memcmp(expected,fl_log,calls*sizeof(*expected))||memcmp(fl_after,fl_arena,FL_SIZE)||
           memcmp(after,actual,FL_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=fl_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("flyer poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases through the entry shim, fast/preserving dispatch, nonempty x87, full pair/trace/state\n",
        d->name,total);
    return status;
}
/* Real callees: 477904 on randomized device globals, the hooked leaves
 * with their hooks in rotating original/replaced/compare modes, a random
 * time scale; the 31/a7 stack gives the residue. Pair, arena and globals
 * are compared. */
static int fl_real_pass(unsigned index){
    static const unsigned nested[]={ES2_HOOK_INDEX_movement_step,ES2_HOOK_INDEX_multiply_q8,ES2_HOOK_INDEX_multiply_q10,
        ES2_HOOK_INDEX_heading_cos,ES2_HOOK_INDEX_euler_matrix,ES2_HOOK_INDEX_rotate_point,ES2_HOOK_INDEX_matrix_transpose,
        ES2_HOOK_INDEX_matrix_angles};
    enum { COUNT=sizeof(nested)/sizeof(nested[0]) };
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[COUNT]={0};
    unsigned lever=0;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<1024&&!status;++n){
        uint32_t a[6],r[3],residue,expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;fl_build(n,a,r,&residue);
        put16(mapped+0xd3b64,(uint16_t)(random32()%3?0x100u+random32()%0x200u:random32()));
        put32(mapped+0x2bb6a4,random32()%2);put32(mapped+0x2bb3b4,random32()%12u);put32(mapped+0x2bb548,random32()%12u);
        mapped[0x2bb3d8]=(BYTE)random32();
        BYTE before[FL_GLOBALS],after[FL_GLOBALS],actual[FL_GLOBALS];
        memcpy(fl_before,fl_arena,FL_SIZE);fl_globals(before,0);
        es2_hook hk[COUNT+1];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<COUNT;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("flyer nested install");}
        sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        memcpy(fl_after,fl_arena,FL_SIZE);fl_globals(after,0);
        lever+=mapped[0x2bb6ac]&&mapped[0xd1f52]==2;
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+COUNT,d,&o,function_hook_extension(index)))return fail("flyer caller install");
        memcpy(fl_arena,fl_before,FL_SIZE);fl_globals(before,1);
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);fl_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||memcmp(fl_after,fl_arena,FL_SIZE)||memcmp(after,actual,FL_GLOBALS)){
            for(unsigned j=0;j<FL_GLOBALS;++j)if(after[j]!=actual[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,after[j],actual[j]);break;}
            fprintf(stderr,"real mode=%u\n",mode);fl_calls=0;status=fl_report("real",n,pair,0,NULL);
        }
        for(unsigned j=0;j<=COUNT;++j){if(j<COUNT)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("flyer nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("flyer nested uninstall");}
        fl_globals(before,1);++total;
    }
    for(unsigned j=0;j<COUNT&&!status;++j)if(!entered[j]){
        fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&!lever)status=fail("flyer real pass never reached lever mode");
    if(!status){printf("PASS: %s %u real-callee cases (%u in lever mode), %u nested hooks in rotating modes, 31/a7 stack, pair, memory, globals\n",
        d->name,total,lever,(unsigned)COUNT);
        for(unsigned j=0;j<COUNT;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
static int flyer_cases(unsigned index,unsigned limit){
    BYTE saved[FL_IDS][5],old_globals[FL_GLOBALS];int active[FL_IDS];
    fl_globals(old_globals,0);if(fl_patch(saved,active,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_FLYER_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3],residue;fl_build(n,a,r,&residue);status=fl_compare(index,n,a,r,residue);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=fl_installed_probes(index,64);
    if(fl_patch(saved,active,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=fl_real_pass(index);
    fl_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, payloads by content, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
