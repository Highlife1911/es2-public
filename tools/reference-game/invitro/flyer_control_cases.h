/* Flyer control (flyer.md, "flyer control `0x41bb9c`, `0x4221a8`, `0x42215c`,
 * `0x4222fc`" section): flyer_input 41bb9c, flyer_command 4221a8, flyer_bank
 * 42215c, flyer_steer 4222fc. Direct cases run the original body and the core
 * against logging stubs for the game callees of the function under test
 * (terrain_height, heading_cos, flight_model, 42215c, 4221a8, patched in the
 * private image): ordered argument slots, entry EAX/EDX/ECX, the records behind
 * the frame pointers by content (stack values masked) and the two stack residue
 * bytes the stub finds at the offsets where the flight model, 42215c or 4221a8
 * read them. The original is called from an own caller that fills the stack
 * with a pattern and plants distinct residue bytes at the entry offsets of the
 * shim; the replacement receives them in the shim's registers. While the branch
 * traps are active (first 16,384 cases) the residue is not compared: the trap's
 * exception dispatch writes below the original's ESP. Otherwise the original's
 * stubs must see exactly the planted bytes, which checks the depth table of the
 * contract. Stubs return edge words and change words the original rereads
 * (S+1Eh, A+0Eh, A+25Dh, ...) between calls. Then installed 31/a7 probes
 * through the entry shim with planted bytes, and a real-callee pass (real
 * flight_model, 42215c, 4221a8 and heading_cos in rotating modes, real
 * 477904). */
typedef struct fc_record { uint32_t id,args[5],regs[3],payload[9],residue,eax,edx; } fc_record;
enum { FC_SIZE=0x900, FC_A=0x000, FC_T=0x340, FC_OBJ=0x540, FC_PARTS=0x5c0, FC_R=0x5e0, FC_CFG=0x620,
    FC_MAX=16, FC_IDS=5 };
#define FC_BASE ((uint32_t)(uintptr_t)fc_arena)
static BYTE fc_arena[FC_SIZE],fc_before[FC_SIZE],fc_after[FC_SIZE];
static fc_record fc_log[FC_MAX];
static unsigned fc_calls,fc_hook;
static uint32_t fc_seed,fc_stream;
__attribute__((used)) static uint32_t fc_ecx;
__attribute__((used)) static BYTE fc_res0,fc_res1;
/* 0 terrain_height, 1 heading_cos, 2 flight_model, 3 flyer_bank 42215c,
 * 4 flyer_command 4221a8. */
static const uint32_t fc_addresses[FC_IDS]={0x46e180,0x47dff0,0x466b58,0x42215c,0x4221a8};
static const unsigned fc_counts[FC_IDS]={2,1,5,3,2};
static uint32_t fc_next(void){
    uint32_t z=fc_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static uint32_t fc_mask(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u?0:value;
}
static BYTE *fc_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
/* Words the originals reread after a call. */
static void fc_perturb(uint32_t pick){
    static const unsigned words[]={FC_A+0xe,FC_A+0x10,FC_A+0x25d,FC_A+0x287,FC_A+0x2b9+0x1e,FC_A+0x290,FC_T+0x10,FC_T+0x1f8,
        FC_OBJ+0xe,FC_R,FC_R+4,FC_A+0x1fa};
    const unsigned at=words[pick%12];
    if(at==FC_A+0x1fa)return; /* the model pointer stays inside the arena */
    put16(fc_arena+at,(uint16_t)(pick&0x100?fc_next():fc_next()%0x1000u-0x800u));
}
__attribute__((used,noinline)) static uint64_t __cdecl fc_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    uint32_t eax=fc_next(),edx=fc_next(),pick=fc_next();fc_ecx=fc_next();
    if(fc_calls==FC_MAX){fail("flyer control synthetic call overflow");ExitProcess(1);}
    fc_record *v=fc_log+fc_calls++;memset(v,0,sizeof(*v));v->id=id;
    for(unsigned k=0;k<fc_counts[id];++k)v->args[k]=fc_mask(a[k]);
    for(unsigned j=0;j<3;++j)v->regs[j]=fc_mask(regs[j]);
    if(id>=2&&!coverage_active)v->residue=fc_res0|(uint32_t)fc_res1<<8;
    switch(id){
    case 0: /* terrain height */
        if(pick%3==0)eax=fc_next()%40000u;
        break;
    case 1: /* heading_cos: 0, +-4000h or anything in the low word */
        if(pick%3==0){static const uint16_t c[]={0,0x4000,0xc000,1,0xffff};eax=(eax&0xffff0000u)|c[pick/3%5];}
        break;
    case 2: /* flight_model: C (5 dwords) and F (4 dwords) by content */
        for(unsigned j=0;j<5;++j)v->payload[j]=get32(fc_host(a[0])+4*j);
        for(unsigned j=0;j<4;++j)v->payload[5+j]=get32(fc_host(a[3])+4*j);
        break;
    case 3: /* 42215c: record +4..+10h (+0 is still uninitialized) */
        for(unsigned j=0;j<4;++j)v->payload[j]=get32(fc_host(a[2])+4+4*j);
        break;
    case 4: /* 4221a8: record +0..+10h */
        for(unsigned j=0;j<5;++j)v->payload[j]=get32(fc_host(a[1])+4*j);
        break;
    default:fail("flyer control stub id");ExitProcess(1);
    }
    if(pick%4==0)fc_perturb(fc_next());
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
/* The stub reads its two residue bytes (entry ESP-o1, ESP-o2) before
 * anything else uses the stack: after the three register pushes they lie
 * at 12-o1(%esp) and 12-o2(%esp). */
#define FC_STUB(n,d1,d2) __attribute__((naked)) static void fc_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\t" \
    "movb " #d1 "(%esp),%al\n\tmovb %al,_fc_res0\n\tmovb " #d2 "(%esp),%al\n\tmovb %al,_fc_res1\n\tmovl (%esp),%eax\n\t" \
    "leal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _fc_stub_impl\n\taddl $24,%esp\n\tmovl _fc_ecx,%ecx\n\tret\n\t");}
FC_STUB(0,0,0) FC_STUB(1,0,0) FC_STUB(2,-389,-409) FC_STUB(3,-469,-489) FC_STUB(4,-445,-465)
#undef FC_STUB
static void *fc_stubs[FC_IDS]={fc_stub_0,fc_stub_1,fc_stub_2,fc_stub_3,fc_stub_4};
/* Callees of each hook (bit per stub id). */
static unsigned fc_callees(unsigned index){
    switch(index){
    case ES2_HOOK_INDEX_flyer_input:return 1u<<0|1u<<2;
    case ES2_HOOK_INDEX_flyer_command:return 1u<<2;
    case ES2_HOOK_INDEX_flyer_bank:return 1u<<4;
    default:return 1u<<0|1u<<1|1u<<3|1u<<4;
    }
}
/* Residue planted by the own caller: four (offset below entry ESP, byte). */
typedef struct fc_plant { uint32_t offset[4]; BYTE byte[4]; } fc_plant;
static void fc_reset(void){fc_calls=0;fc_stream=fc_seed;}
static uint32_t fc_pick(const uint32_t *values,unsigned count){return values[random32()%count];}
static uint16_t fc_word(const uint16_t *values,unsigned count){return random32()%4?values[random32()%count]:(uint16_t)random32();}
/* A model block (54 bytes) like RAZOR/SKIMMER with spread. */
static void fc_model(BYTE *m){
    for(unsigned j=0;j<12;j+=2)put16(m+j,(uint16_t)(random32()%8?random32()%1600u:random32()));
    put32(m+0xe,random32()%0x40000u);put32(m+0x12,random32()%1200u);
    for(unsigned j=0x16;j<0x22;j+=4)put32(m+j,random32()%20u);
    put32(m+0x22,random32()%200000u);put32(m+0x26,random32()%20000u);
    put32(m+0x2a,random32()%3000u);put32(m+0x2e,random32()%1000u);put32(m+0x32,random32()%1024u);
}
static void fc_build(unsigned index,uint32_t a[6],uint32_t r[3],fc_plant *p,int real){
    const uint32_t b=FC_BASE;fc_seed=random32();random_bytes(fc_arena,FC_SIZE);memset(a,0,24);
    BYTE *A=fc_arena+FC_A;
    for(unsigned i=0;i<3;++i)r[i]=random32();
    put32(A+0x1fa,b+FC_OBJ);put32(A+0x20e,b+FC_PARTS);put32(A+0x1f2,b+FC_CFG);
    put32(mapped+0xa0be0,random32());
    /* Roll, heading and the steering words around the thresholds. */
    const uint16_t goal=fc_word((const uint16_t[]){0,0x5dc,0xbb8,0x200,0xfc00,0x8000,0x7fff,0x5db,0x5dd},9);
    put16(fc_arena+FC_OBJ+0xe,goal);
    {const uint16_t lo=(uint16_t)(goal-0x5dcu),hi=(uint16_t)(0x5dcu-goal);
     const uint16_t rolls[]={lo,(uint16_t)(lo-1u),(uint16_t)(lo+1u),hi,(uint16_t)(hi-1u),(uint16_t)(hi+1u),0x320,0x31f,0x321,0,0x8000};
     put16(A+0xe,fc_word(rolls,11));}
    put16(A+0x25d,fc_word((const uint16_t[]){0,1,0xffff,0x100,0xff00,0x8000,0x7fff},7));
    put16(A+0x287,fc_word((const uint16_t[]){0,1,0xffff,0x400,0xfc00,0x8000,0x7fff},7));
    if(random32()%2)put16(fc_arena+FC_T+0x1f8,(uint16_t)(random32()%0x4000u-0x2000u));
    BYTE *parts=fc_arena+FC_PARTS;for(unsigned j=0;j<0x14;j+=2)if(random32()%2)put16(parts+j,0);
    uint32_t record=b+FC_R;
    static const uint32_t shapes[]={0,1,0xffffffffu,0x10,0x11,0xfffffff0u,0xffffffefu,0xff,0x100,0x101,0xffffff00u,0xffffff01u,
        0xfffffeffu,0xb504,0xb505,0xffff4afbu,0xffff4afcu,0x10000,0x80000000u,0x7fffffffu};
    put32(fc_arena+FC_R,random32()%4?fc_pick(shapes,20):random32());put32(fc_arena+FC_R+4,random32()%4?fc_pick(shapes,20):random32());
    switch(index){
    case ES2_HOOK_INDEX_flyer_input:{
        static const uint32_t axes[]={0,0,1,0xffffffffu,0x80,0xffffff80u,0x100,0xffffff00u};
        a[0]=b+FC_A;for(unsigned j=1;j<5;++j)a[j]=random32()%3?fc_pick(axes,8):random32();
        if(random32()%3==0)a[3]=0;
        p->offset[0]=p->offset[2]=0x1dd;p->offset[1]=p->offset[3]=0x1f1;r[2]=(r[2]&0xffff0000u)|(random32()&0xffffu);
        p->byte[0]=p->byte[2]=(BYTE)r[2];p->byte[1]=p->byte[3]=(BYTE)(r[2]>>8);
        break;}
    case ES2_HOOK_INDEX_flyer_command:
        if(!real&&random32()%8==0)record=b+FC_A+0x1fa-4u; /* R+4 is the model pointer */
        if(record!=b+FC_R){put32(fc_host(record),get32(fc_arena+FC_R));put32(fc_host(record)+4,get32(fc_arena+FC_R+4));}
        a[0]=b+FC_A;a[1]=record;
        p->offset[0]=p->offset[2]=0x1c9;p->offset[1]=p->offset[3]=0x1dd;r[2]=(r[2]&0xffff0000u)|(random32()&0xffffu);
        p->byte[0]=p->byte[2]=(BYTE)r[2];p->byte[1]=p->byte[3]=(BYTE)(r[2]>>8);
        break;
    case ES2_HOOK_INDEX_flyer_bank:
        if(!real&&random32()%8==0)record=b+FC_A+0x25d; /* R+0 is the roll rate reread after the first store */
        a[0]=b+FC_A;a[1]=random32()%2?(random32()&0xffff0000u)|(uint16_t)(get16(A+0xe)+random32()%3-1u):random32();a[2]=record;
        p->offset[0]=p->offset[2]=0x1e1;p->offset[1]=p->offset[3]=0x1f5;r[2]=(r[2]&0xffff0000u)|(random32()&0xffffu);
        p->byte[0]=p->byte[2]=(BYTE)r[2];p->byte[1]=p->byte[3]=(BYTE)(r[2]>>8);
        break;
    default:{
        static const uint16_t yaws[]={0,1,0xffff,0x8000,0x7fff,0x7d0,0x7cf,0x7d1,0xf830,0xf831,0xf82f,0x100,0xff00};
        a[0]=b+FC_A;a[1]=(random32()&0xffff0000u)|fc_word(yaws,13);a[2]=random32()%3?random32()%0x200u-0x100u:random32();
        a[3]=random32()%3?b+FC_T:0;
        p->offset[0]=0x229;p->offset[1]=0x23d;p->offset[2]=0x20d;p->offset[3]=0x221;
        r[0]=(r[0]&0xffff0000u)|(random32()&0xffffu);r[1]=(r[1]&0xffff0000u)|(random32()&0xffffu);
        p->byte[0]=(BYTE)r[0];p->byte[1]=(BYTE)(r[0]>>8);p->byte[2]=(BYTE)r[1];p->byte[3]=(BYTE)(r[1]>>8);
        break;}
    }
    if(real){
        /* Real flight model: S with the pose at A+0Ch, model blocks. */
        const uint32_t S=index==ES2_HOOK_INDEX_flyer_input?FC_A+0x2b9:FC_A+0x243;
        BYTE *s=fc_arena+S,*o=A+0xc;
        for(unsigned j=0;j<12;j+=4)put32(s+j,random32()%4000u-2000u);
        put32(s+4,random32()%3000u);
        for(unsigned j=0x18;j<0x1e;j+=2)put16(s+j,(uint16_t)(random32()%2000u-1000u));
        put16(s+0x1e,(uint16_t)(random32()%0x800u-0x400u));put32(s+0x20,b+FC_A+0xc);
        for(unsigned j=0;j<18;j+=2)put16(o+6+j,(uint16_t)(random32()%0x8001u-0x4000u));
        static const BYTE modes[]={0,1,2,2};o[6+18]=modes[random32()%4];put16(o+0x26,(uint16_t)(random32()%2));
        put16(o,(uint16_t)(random32()%0x2000u-0x1000u));
        fc_model(fc_arena+FC_OBJ+0x3a);fc_model(fc_arena+FC_CFG+0x1dc);
    }
}
/* Own caller: a patterned stack, five slots, four planted residue bytes,
 * entry EAX/EDX/ECX; EAX/EDX come back in registers[0..1]. */
__attribute__((naked)) static void fc_call(void *target,const uint32_t *a,uint32_t *registers,const fc_plant *plant,uint32_t pattern){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 24(%ebp),%eax\n\tmovl $1024,%ecx\n\trep stosl\n\taddl $3840,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 16(%esi)\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 20(%ebp),%esi\n\t"
        "movl %esp,%eax\n\tsubl 0(%esi),%eax\n\tmovb 16(%esi),%bl\n\tmovb %bl,-4(%eax)\n\t"
        "movl %esp,%eax\n\tsubl 4(%esi),%eax\n\tmovb 17(%esi),%bl\n\tmovb %bl,-4(%eax)\n\t"
        "movl %esp,%eax\n\tsubl 8(%esi),%eax\n\tmovb 18(%esi),%bl\n\tmovb %bl,-4(%eax)\n\t"
        "movl %esp,%eax\n\tsubl 12(%esi),%eax\n\tmovb 19(%esi),%bl\n\tmovb %bl,-4(%eax)\n\t"
        "movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tcall *8(%ebp)\n\t"
        "movl 16(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static int fc_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const fc_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,fc_calls);
    for(unsigned j=0;j<calls&&j<fc_calls&&expected;++j)if(memcmp(expected+j,fc_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(fc_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<FC_SIZE;++j)if(fc_after[j]!=fc_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,fc_after[j],fc_arena[j]);break;}
    return 1;
}
/* In the original, the residue callee must see the planted bytes. */
static int fc_check_residue(unsigned n,const fc_plant *p){
    if(coverage_active)return 0;
    for(unsigned j=0;j<fc_calls;++j){
        const fc_record *v=fc_log+j;if(v->id<2)continue;
        const unsigned pair=fc_hook==ES2_HOOK_INDEX_flyer_steer&&v->id==4?2:0;
        const uint32_t want=p->byte[pair]|(uint32_t)p->byte[pair+1]<<8;
        if(v->residue!=want){fprintf(stderr,"FAIL: %s case %u residue at stub %u %04x, planted %04x\n",current_function,n,v->id,v->residue,want);return 1;}
    }
    return 0;
}
static unsigned fc_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<fc_calls;++j)if(fc_addresses[fc_log[j].id]==c->key)++hits;
    return hits;
}
static int fc_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r,const fc_plant *p){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    static fc_record expected[FC_MAX];const uint32_t world=get32(mapped+0xa0be0);
    memcpy(fc_before,fc_arena,FC_SIZE);
    uint32_t regs[3]={r[0],r[1],r[2]};
    fc_reset();fc_call(mapped+d->rva,a,regs,p,random32());
    uint64_t pair[2];pair[0]=(uint64_t)regs[1]<<32|regs[0];unsigned calls=fc_calls;memcpy(expected,fc_log,calls*sizeof(*expected));
    if(fc_check_residue(n,p))return 1;
    memcpy(fc_after,fc_arena,FC_SIZE);memcpy(fc_arena,fc_before,FC_SIZE);fc_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=fc_expected_hits(c+i))return fail("flyer control callee counter delta");
    if(pair[0]!=pair[1]||calls!=fc_calls||memcmp(expected,fc_log,calls*sizeof(*expected))||memcmp(fc_after,fc_arena,FC_SIZE)||
       get32(mapped+0xa0be0)!=world)
        return fc_report("direct",n,pair,calls,expected);
    return 0;
}
static int fc_patch(unsigned mask,BYTE saved[FC_IDS][5],int active[FC_IDS],int on){
    for(unsigned i=0;i<FC_IDS;++i){
        if(on){active[i]=0;if(!(mask>>i&1))continue;if(actor_patch(fc_addresses[i]-0x400000u,fc_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(fc_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
/* Installed through the entry shim: the own caller plants distinct bytes
 * in a 31/a7 stack; the shim must read them at the contract's offsets. */
static int fc_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3];fc_plant p;fc_build(index,a,r,&p,0);const uint32_t pattern=poison?0xa7a7a7a7u:0x31313131u;
        BYTE expected_fpu[108],actual_fpu[108];static fc_record expected[FC_MAX];
        memcpy(fc_before,fc_arena,FC_SIZE);uint32_t er[3]={r[0],r[1],r[2]},ar[3]={r[0],r[1],r[2]};
        fc_reset();fc_call(mapped+d->rva,a,er,&p,pattern);es2_hook_fpu_save(expected_fpu);
        unsigned calls=fc_calls;memcpy(expected,fc_log,calls*sizeof(*expected));memcpy(fc_after,fc_arena,FC_SIZE);
        if(fc_check_residue(n,&p))return 1;
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("flyer control poison install");
        memcpy(fc_arena,fc_before,FC_SIZE);fc_reset();fc_call(mapped+d->rva,a,ar,&p,pattern);es2_hook_fpu_save(actual_fpu);
        uint64_t pair[2]={(uint64_t)er[1]<<32|er[0],(uint64_t)ar[1]<<32|ar[0]};
        if(pair[0]!=pair[1]||calls!=fc_calls||memcmp(expected,fc_log,calls*sizeof(*expected))||memcmp(fc_after,fc_arena,FC_SIZE)||
           memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=fc_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("flyer control poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases through the entry shim with planted residue, fast/preserving dispatch, nonempty x87\n",
        d->name,total);
    return status;
}
/* Real callees: flight_model with its real leaves and 477904, the group's
 * own 42215c/4221a8 and heading_cos, each not installed, original or
 * replaced (rotating); terrain_height stays a stub. Pair, arena and the
 * flight-model globals are compared in full, including the pose byte
 * A+25h and S+37h that come from the stack residue. The pass counts the
 * cases in which that pose byte of the all-original run is one of the
 * planted bytes (the chain from the outer entry to the flight model is
 * exercised) and requires some. */
static int fc_real_pass(unsigned index){
    static const unsigned nested[]={ES2_HOOK_INDEX_flight_model,ES2_HOOK_INDEX_flyer_command,ES2_HOOK_INDEX_flyer_bank,ES2_HOOK_INDEX_heading_cos};
    enum { COUNT=4 };
    unsigned use=1u; /* flight_model always */
    if(index==ES2_HOOK_INDEX_flyer_bank)use|=2u;
    if(index==ES2_HOOK_INDEX_flyer_steer)use|=2u|4u|8u;
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0,reached=0;uint64_t entered[COUNT]={0};
    BYTE saved[FC_IDS][5];int active[FC_IDS];
    const unsigned stubs=fc_callees(index)&1u; /* terrain_height */
    if(fc_patch(stubs,saved,active,1))return 1;
    for(unsigned mode=0;mode<2&&!status;++mode)for(unsigned n=0;n<1536&&!status;++n){
        uint32_t a[6],r[3];fc_plant p;fc_build(index,a,r,&p,1);const uint32_t pattern=n&1?0xa7a7a7a7u:0x31313131u;
        put16(mapped+0xd3b64,(uint16_t)(random32()%3?0x100u+random32()%0x200u:random32()));
        put32(mapped+0x2bb6a4,random32()%2);put32(mapped+0x2bb3b4,random32()%12u);put32(mapped+0x2bb548,random32()%12u);
        mapped[0x2bb3d8]=(BYTE)random32();
        BYTE before[FL_GLOBALS],after[FL_GLOBALS],actual[FL_GLOBALS];
        memcpy(fc_before,fc_arena,FC_SIZE);fl_globals(before,0);
        es2_hook hk[COUNT+1];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<COUNT;++j){
            if(!(use>>j&1))continue;
            static const unsigned digits[]={1,3,9,27};
            const unsigned m=(n/2/digits[j]+mode)%3; /* 0 not installed, 1 original, 2 replaced */
            if(!m)continue;
            o.mode=m==1?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,function_hook_extension(nested[j])))return fail("flyer control nested install");
        }
        uint32_t er[3]={r[0],r[1],r[2]},ar[3]={r[0],r[1],r[2]};
        fc_reset();fc_call(mapped+d->rva,a,er,&p,pattern);
        memcpy(fc_after,fc_arena,FC_SIZE);fl_globals(after,0);
        for(unsigned j=0;j<4;++j)if(fc_after[FC_A+0x25]==p.byte[j]&&p.byte[j]!=(BYTE)pattern&&fc_before[FC_A+0x25]!=p.byte[j]){++reached;break;}
        o.mode=mode?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(hk+COUNT,d,&o,function_hook_extension(index)))return fail("flyer control caller install");
        memcpy(fc_arena,fc_before,FC_SIZE);fl_globals(before,1);
        fc_reset();fc_call(mapped+d->rva,a,ar,&p,pattern);fl_globals(actual,0);
        uint64_t pair[2]={(uint64_t)er[1]<<32|er[0],(uint64_t)ar[1]<<32|ar[0]};
        if(pair[0]!=pair[1]||memcmp(fc_after,fc_arena,FC_SIZE)||memcmp(after,actual,FL_GLOBALS)){
            for(unsigned j=0;j<FL_GLOBALS;++j)if(after[j]!=actual[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,after[j],actual[j]);break;}
            fprintf(stderr,"real mode=%u\n",mode);fc_calls=0;status=fc_report("real",n,pair,0,NULL);
        }
        for(unsigned j=0;j<=COUNT;++j){if(j<COUNT)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("flyer control nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("flyer control nested uninstall");}
        fl_globals(before,1);++total;
    }
    if(fc_patch(stubs,saved,active,0))return 1;
    for(unsigned j=0;j<COUNT&&!status;++j)if(use>>j&1&&!entered[j]){
        fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&!reached)status=fail("flyer control real pass never carried a planted residue byte into the pose");
    if(!status){printf("PASS: %s %u real-callee cases (%u with a planted residue byte in the pose), nested hooks in rotating modes, pair, memory, globals\n",
        d->name,total,reached);
        for(unsigned j=0;j<COUNT;++j)if(use>>j&1)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
static int flyer_control_cases(unsigned index,unsigned limit){
    BYTE saved[FC_IDS][5];int active[FC_IDS];const uint32_t world=get32(mapped+0xa0be0);fc_hook=index;
    if(fc_patch(fc_callees(index),saved,active,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_FLYER_CONTROL_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];fc_plant p;fc_build(index,a,r,&p,0);status=fc_compare(index,n,a,r,&p);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=fc_installed_probes(index,64);
    if(fc_patch(0,saved,active,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=fc_real_pass(index);
    put32(mapped+0xa0be0,world);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, records by content, residue, arena\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
