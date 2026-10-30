/* MECH part hit 4188c8 (docs/reverse-engineering/hit_tests.md, section
 * "MECH part hit"). Direct cases run the original body and the core against
 * logging stubs for every game callee but the two leaves the core computes
 * (410670 and 426ac8 run as original code in the original run): 49630c (record
 * stub as in base_cases.h), multiply_q10, part_ratio, the slots +74h/+70h in
 * the arena vtable, 434010, 420a68, rng_next, 40f57c, 40da38, pool_alloc,
 * explosion_create, debris_table and debris_burst; ordered calls with their
 * arguments and entry EAX/EDX/ECX (stack addresses masked), by content the
 * debris word and the points. part_ratio answers a planned condition before and
 * after (bracket boundaries, 100h, 100), rng_next answers at the chance
 * threshold 41*n. The stubs change what the original rereads (the player byte,
 * the configuration and definition pointers, the gate word C+56h, the side
 * byte, the owner, the effect table, the region flag pointer, [W+1Ch], the
 * debris word, the pool pointer) and what it must not reread (the damage and
 * splash words P+4/P+8). Compared: the pair, the arena, FS:0, the globals
 * (counter pointer, pool pointer, results list 4a9e6a..4a9e6f, debris records
 * 4a964c..4a9659, RNG stream A) and the callee counters. Then installed 31/a7
 * poison probes and a real-callee pass with multiply_q10, rng_next, pool_alloc
 * and debris_table in rotating modes. */
typedef struct pth_record { uint32_t id,args[5],regs[3],input[3],eax,edx; uint32_t count; } pth_record;
enum { PTH_SIZE=0x1000, PTH_A=0x100, PTH_VTABLE=0x3c0, PTH_DEF=0x440, PTH_DEF2=0x458, PTH_C=0x470, PTH_C2=0x4d0,
    PTH_P=0x540, PTH_FX=0x560, PTH_FX2=0x580, PTH_POINT=0x5a0, PTH_FLAGS=0x5c0, PTH_FLAGS2=0x640, PTH_SYS=0x6c0, PTH_SLOTS=0x6d0,
    PTH_WP=0x700, PTH_WD=0x780, PTH_WN=0x7e0, PTH_LIST=0x800, PTH_COUNTER=0x900, PTH_POOL=0x920, PTH_POOL2=0x934,
    PTH_NODES=0x940, PTH_OBJECT=0xac0, PTH_RECORD=0xb40, PTH_RECORD2=0xb50, PTH_MAX=48, PTH_IDS=14, PTH_GLOBALS=142 };
#define PTH_BASE ((uint32_t)(uintptr_t)pth_arena)
static BYTE pth_arena[PTH_SIZE],pth_before[PTH_SIZE],pth_after[PTH_SIZE];
static pth_record pth_log[PTH_MAX];
static unsigned pth_calls,pth_ratio_index;
static int pth_real,pth_found;
static uint32_t pth_seed,pth_stream,pth_ratio[2];
__attribute__((used)) static uint32_t pth_ecx;
/* 49630c (record), multiply_q10, part_ratio, slots +74h/+70h (arena
 * vtable, never patched), 434010, 420a68, rng_next, 40f57c, 40da38,
 * pool_alloc, explosion_create, debris_table, debris_burst. */
static const uint32_t pth_addresses[PTH_IDS]={0x49630c,0x47e0b0,0x40dbc0,0,0,0x434010,0x420a68,0x492ee0,0x40f57c,0x40da38,
    0x471b30,0x407f1c,0x4089bc,0x408530};
static const unsigned pth_slots[PTH_IDS]={0,0,0,0x74,0x70,0,0,0,0,0,0,0,0,0};
static const unsigned pth_counts[PTH_IDS]={0,2,2,4,5,1,1,1,3,3,1,5,1,4};
static uint32_t pth_next(void){
    uint32_t z=pth_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *pth_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int pth_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static uint16_t pth_pick(const uint16_t *values,unsigned count,uint32_t pick){return values[pick%count];}
static void pth_point(pth_record *v,uint32_t address){for(unsigned j=0;j<3;++j)v->input[j]=get32(pth_host(address)+4*j);}
/* The definition the MECH points to now. While the weapon lookup of this
 * case finds nothing, its side byte must not become 1 (the original reads
 * [W+1Ch] with W = 0 there). */
static BYTE *pth_def(void){return pth_host(get32(pth_arena+PTH_A+0x45));}
static void pth_side(uint32_t pick){
    static const BYTE sides[]={0,1,1,2,0xff,0x81};BYTE s=sides[pick%6];
    if(!pth_found&&s==1)s=2;pth_def()[0x12]=s;
}
/* Rereads of the MECH's state between the calls. */
static void pth_touch(uint32_t pick){
    if(pth_real)return;
    BYTE *a=pth_arena+PTH_A;uint32_t b=PTH_BASE;
    if(pick%5==0)a[0xa3]=(BYTE)(pick>>8&1?0:1+(pick>>9&0x7f));
    if(pick%7==0)put32(a+0x1f2,b+(pick>>10&1?PTH_C2:PTH_C));
    if(pick%11==0){put32(a+0x45,b+(pick>>11&1?PTH_DEF2:PTH_DEF));if(!pth_found&&pth_def()[0x12]==1)pth_def()[0x12]=0;}
    if(pick%13==0)put16(pth_host(get32(a+0x1f2))+0x56,(uint16_t)(pick>>12&1?0:pick>>13));
    if(pick%17==0)pth_side(pick>>14);
    if(pick%3==0)put32(pth_arena+PTH_P+0xe,pth_next());
    if(pick%19==0){put16(pth_arena+PTH_P+4,(uint16_t)pth_next());put16(pth_arena+PTH_P+8,(uint16_t)pth_next());}
}
__attribute__((used,noinline)) static uint64_t __cdecl pth_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(pth_calls==PTH_MAX){fail("part hit synthetic call overflow");ExitProcess(1);}
    pth_record *v=pth_log+pth_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,4*pth_counts[id]);memcpy(v->regs,regs,12);
    v->count=weapons_bridge_slots(a,pth_counts[id]);
    for(unsigned j=0;j<3;++j)if(pth_stack(v->regs[j]))v->regs[j]=0;
    for(unsigned j=0;j<pth_counts[id];++j)if(pth_stack(v->args[j]))v->args[j]=0;
    uint32_t eax=pth_next(),edx=pth_next(),pick=pth_next(),b=PTH_BASE;pth_ecx=pth_next();
    switch(id){
    case 0: /* 49630c record stub returns the real FS:0 head for pth_stub_0. */
        __asm__ volatile("movl %%fs:0,%0":"=r"(eax));break;
    case 1: /* multiply_q10: the splash is 0 in a quarter (DI = 0). */
        if(pick%4==0)eax&=0xffff0000u;
        break;
    case 2: /* part_ratio: the planned condition before, then after. */
        eax=(eax&0xffff0000u)|pth_ratio[pth_ratio_index++&1u];
        pth_touch(pick);break;
    case 3: case 4: case 5: case 6: /* the slots, 434010, 420a68 */
        pth_touch(pick);break;
    case 7: { /* rng_next: AX & FFFh at the threshold 41*n, n from the side
               * byte read just before; the effect table is reread. */
        uint32_t t=pth_def()[0x12]?410u:123u;
        uint32_t w=pick%5==4?pick>>4&0xfffu:t-pick%4u+(pick>>2&1u);
        eax=(eax&0xfffff000u)|(w&0xfffu);
        if(!pth_real&&pick%7==0)put32(pth_arena+PTH_P+0xa,b+(pick>>9&1?PTH_FX2:PTH_FX));
        break; }
    case 8: /* 40f57c: the region flag pointer, the side byte and [W+1Ch]
             * are reread. */
        if(pth_real)break;
        if(pick%3==0)put32(pth_arena+PTH_A+0x20e,b+(pick>>8&1?PTH_FLAGS2:PTH_FLAGS));
        if(pick%5==0)pth_side(pick>>9);
        if(!pth_found&&pth_def()[0x12]==1)pth_def()[0x12]=0;
        if(pick%7==0&&a[0])put32(pth_host(a[0])+0x1c,b+PTH_WN+4u*(pick>>12&3u)-0x56u);
        break;
    case 9: /* 40da38: the pool pointer is read after it. */
        if(!pth_real&&pick%3==0)put32(mapped+0xa961e,b+(pick>>8&1?PTH_POOL2:PTH_POOL));
        break;
    case 10: /* pool_alloc: the object or 0. */
        eax=pick%3?b+PTH_OBJECT:0;break;
    case 11: case 13: /* explosion_create, debris_burst: the point. */
        pth_point(v,a[2]);break;
    case 12: /* debris_table: the word by content, lowered in a third; one
              * of two arena records. */
        v->input[0]=get16(pth_host(a[0]));
        if(pick%3==0)put16(pth_host(a[0]),(uint16_t)(v->input[0]-(pick>>8&7u)));
        eax=b+(pick>>4&1?PTH_RECORD:PTH_RECORD2);
        break;
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
/* Pushes the entry EAX/EDX/ECX for the record before the slot pointer. */
#define PTH_STUB(n) __attribute__((naked)) static void pth_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _pth_stub_impl\n\taddl $24,%esp\n\tmovl _pth_ecx,%ecx\n\tret\n\t");}
PTH_STUB(1) PTH_STUB(2) PTH_STUB(3) PTH_STUB(4) PTH_STUB(5) PTH_STUB(6) PTH_STUB(7) PTH_STUB(8) PTH_STUB(9) PTH_STUB(10)
PTH_STUB(11) PTH_STUB(12) PTH_STUB(13)
#undef PTH_STUB
/* 49630c: captures the entry registers like the other stubs, then links a
 * record at EBP + [descriptor+4] into FS:0 with the saved head at record+0
 * and a zero context word at record+16, as the original does. */
__attribute__((naked)) static void pth_stub_0(void){__asm__ volatile(
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
    "pushl $0\n\tcalll _pth_stub_impl\n\taddl $12,%esp\n\t"
    "popl %ecx\n\tmovl 4(%ecx),%ecx\n\taddl %ebp,%ecx\n\tmovl %eax,(%ecx)\n\tmovw $0,16(%ecx)\n\tmovl %ecx,%fs:0\n\t"
    "popl %edx\n\taddl $4,%esp\n\tmovl _pth_ecx,%ecx\n\tret\n\t");}
static void *pth_stubs[PTH_IDS]={pth_stub_0,pth_stub_1,pth_stub_2,pth_stub_3,pth_stub_4,pth_stub_5,pth_stub_6,pth_stub_7,pth_stub_8,
    pth_stub_9,pth_stub_10,pth_stub_11,pth_stub_12,pth_stub_13};
/* Counter pointer, explosion pool pointer, results list (count word and
 * pointer), debris records 4a964c..4a9659 and RNG stream A. */
static const struct { uint32_t rva,size; } pth_ranges[]={{0x2cbee4,4},{0xa961e,4},{0xa9e6a,6},{0xa964c,14},{0xd2599,0x72}};
static void pth_globals(BYTE *out,int restore){
    for(unsigned i=0;i<sizeof(pth_ranges)/sizeof(pth_ranges[0]);++i){
        if(restore)memcpy(mapped+pth_ranges[i].rva,out,pth_ranges[i].size);else memcpy(out,mapped+pth_ranges[i].rva,pth_ranges[i].size);
        out+=pth_ranges[i].size;
    }
}
static int pth_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<PTH_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
static void pth_reset(void){pth_calls=0;pth_ratio_index=0;pth_stream=pth_seed;}
/* Pool header (free count +0, used count +2, free head +0Ah, used head
 * +0Eh) with count free nodes of 80h bytes; objects start at node+8. */
static void pth_pool(uint32_t pool,uint32_t nodes,unsigned count){
    put16(pth_host(pool),(uint16_t)count);put16(pth_host(pool)+2,0);put32(pth_host(pool)+0xe,0);
    for(unsigned i=0;i<count;++i){uint32_t n=nodes+0x80u*i;put32(pth_host(n),i+1<count?n+0x80u:0);put32(pth_host(n)+4,i?n-0x80u:0);}
    put32(pth_host(pool)+0xa,count?nodes:0);
}
/* The weapon system: up to 8 slots, null or one of four weapons whose
 * definition byte +17h is a small kind; found = whether 410670 finds the
 * kind word of this case's part (scanned as the original does). */
static void pth_weapons(uint32_t part){
    uint32_t b=PTH_BASE;static const BYTE kinds[]={0,1,2,3,4,6,0xff,0x80};
    static const uint16_t counts[]={0,1,2,3,4,5,6,7,8,8,8,0xffff,0x8000};
    put32(pth_arena+PTH_SYS+4,b+PTH_SLOTS);uint16_t count=pth_pick(counts,13,random32());put16(pth_arena+PTH_SYS+8,count);
    for(unsigned i=0;i<4;++i){
        BYTE *w=pth_arena+PTH_WP+0x20u*i;put32(w+0x18,b+PTH_WD+0x10u*i);put32(w+0x1c,b+PTH_WN+4u*i-0x56u);
        pth_arena[PTH_WD+0x10u*i+0x17]=kinds[random32()%8];
    }
    for(unsigned i=0;i<8;++i)put32(pth_arena+PTH_SLOTS+4*i,random32()%4?b+PTH_WP+0x20u*(random32()%4):0);
    pth_found=0;
    for(unsigned i=0;(int16_t)i<(int16_t)count;++i){uint32_t w=get32(pth_arena+PTH_SLOTS+4*i);
        if(w&&(uint16_t)(int16_t)(int8_t)pth_host(get32(pth_host(w)+0x18))[0x17]==(uint16_t)(part-19u)){pth_found=1;break;}}
}
static void pth_build(unsigned n,uint32_t a[6],uint32_t r[3],int real){
    uint32_t b=PTH_BASE;BYTE *A=pth_arena+PTH_A;
    pth_seed=random32();random_bytes(pth_arena,PTH_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    put32(A,b+PTH_VTABLE);put32(pth_arena+PTH_VTABLE+0x74,(uint32_t)(uintptr_t)pth_stubs[3]);
    put32(pth_arena+PTH_VTABLE+0x70,(uint32_t)(uintptr_t)pth_stubs[4]);
    put32(A+0x45,b+(random32()%2?PTH_DEF:PTH_DEF2));put32(A+0x1f2,b+(random32()%2?PTH_C:PTH_C2));
    put32(A+0x202,b+PTH_SYS);put32(A+0x20e,b+PTH_FLAGS);
    A[0xa3]=(BYTE)(random32()%2?0:1+random32()%255);
    put16(pth_arena+PTH_C+0x56,(uint16_t)(random32()%4?1+random32()%0xffff:0));
    put16(pth_arena+PTH_C2+0x56,(uint16_t)(random32()%4?1+random32()%0xffff:0));
    /* Parts: 0/1 (the panel), small, the mount parts from 19, a dword
     * whose low word is a mount part, negative dwords. */
    static const uint32_t parts[]={0,1,0,1,2,5,18,19,20,21,22,23,25,0x10013u,0x10015u,0xffffffffu,0x80000013u,19,20,21};
    uint32_t part=parts[n<1024?n%20:random32()%20];
    pth_weapons(part);
    BYTE *def=pth_arena+PTH_DEF,*def2=pth_arena+PTH_DEF2;
    static const BYTE sides[]={0,1,1,2,0xff,0x81};def[0x12]=sides[random32()%6];def2[0x12]=sides[random32()%6];
    if(!pth_found){if(def[0x12]==1)def[0x12]=0;if(def2[0x12]==1)def2[0x12]=2;}
    /* The condition before and after: bracket boundaries (>> 5), 100,
     * 100h; after = before in a quarter, 100h in an eighth. */
    static const uint16_t ratios[]={0,1,31,32,63,64,95,96,99,100,101,0xff,0x100,0x101,0x7fff,0x8000,0xffe0,0xffff};
    uint16_t before=random32()%4?pth_pick(ratios,18,random32()):(uint16_t)random32(),after;
    uint32_t k=random32()%8;
    after=k<2?before:k==2?0x100:random32()%2?pth_pick(ratios,18,random32()):(uint16_t)random32();
    pth_ratio[0]=before;pth_ratio[1]=after;
    /* The packet: damage, splash factor, effect table, owner. */
    put16(pth_arena+PTH_P+4,(uint16_t)random32());put16(pth_arena+PTH_P+8,(uint16_t)(random32()%2?random32()%2048:random32()));
    put32(pth_arena+PTH_P+0xa,b+PTH_FX);put32(pth_arena+PTH_P+0xe,random32());
    /* Globals: counter, pool pointer, results list (index -4..15 around
     * the middle of the list block), debris records, RNG. */
    put32(mapped+0x2cbee4,b+PTH_COUNTER);put32(mapped+0xa961e,b+PTH_POOL);
    put16(mapped+0xa9e6a,(uint16_t)(random32()%20-4u));put32(mapped+0xa9e6c,b+PTH_LIST+0x20);
    random_bytes(mapped+0xa964c,14);put32(mapped+0xa964c,b+PTH_RECORD2);
    if(real){
        put16(mapped+0xa9654,(uint16_t)(random32()%2?random32()%4:random32()));
        BYTE *state=mapped+0xd2599;random_bytes(state,112);state[112]=(BYTE)(random32()%56);state[113]=(BYTE)(random32()%56);
        pth_pool(b+PTH_POOL,b+PTH_NODES,random32()%3);
    }
    a[0]=b+PTH_A;a[1]=part;a[2]=b+PTH_P;a[3]=b+PTH_POINT;
}
static int pth_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const pth_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,pth_calls);
    for(unsigned j=0;j<calls&&j<pth_calls&&expected;++j)if(memcmp(expected+j,pth_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(pth_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<PTH_SIZE;++j)if(pth_after[j]!=pth_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,pth_after[j],pth_arena[j]);break;}
    return 1;
}
static unsigned pth_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<pth_calls;++j){unsigned id=pth_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?pth_slots[id]==c->key:!pth_slots[id]&&pth_addresses[id]==c->key)++hits;}
    return hits;
}
static int pth_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[PTH_GLOBALS],after[PTH_GLOBALS],actual[PTH_GLOBALS];static pth_record expected[PTH_MAX];uint32_t head,head_after;
    memcpy(pth_before,pth_arena,PTH_SIZE);pth_globals(before,0);
    __asm__ volatile("movl %%fs:0,%0":"=r"(head));
    pth_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=pth_calls;memcpy(expected,pth_log,calls*sizeof(*expected));
    memcpy(pth_after,pth_arena,PTH_SIZE);pth_globals(after,0);
    memcpy(pth_arena,pth_before,PTH_SIZE);pth_globals(before,1);pth_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];for(unsigned i=0;i<count&&i<16;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);pth_globals(actual,0);
    __asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
    if(head_after!=head)return fail("part hit left FS:0 changed");
    if(n<4096)for(unsigned i=0;i<count&&i<16;++i)if(c[i].count-counts[i]!=pth_expected_hits(c+i))return fail("part hit callee counter delta");
    if(pair[0]!=pair[1]||calls!=pth_calls||memcmp(expected,pth_log,calls*sizeof(*expected))||memcmp(pth_after,pth_arena,PTH_SIZE)||
       pth_globals_differ(after,actual))
        return pth_report("direct",n,pair,calls,expected);
    pth_globals(before,1);
    return 0;
}
static int pth_patch(BYTE saved[PTH_IDS][5],int active[PTH_IDS],unsigned which,int on){
    for(unsigned i=0;i<PTH_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!pth_addresses[i])continue;if(actor_patch(pth_addresses[i]-0x400000u,pth_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(pth_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static int pth_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;uint32_t head,head_after;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;pth_build(n,a,r,0);
        BYTE before[PTH_GLOBALS],after[PTH_GLOBALS],actual[PTH_GLOBALS],expected_fpu[108],actual_fpu[108];static pth_record expected[PTH_MAX];
        memcpy(pth_before,pth_arena,PTH_SIZE);pth_globals(before,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head));
        pth_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=pth_calls;memcpy(expected,pth_log,calls*sizeof(*expected));memcpy(pth_after,pth_arena,PTH_SIZE);pth_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("part hit poison install");
        memcpy(pth_arena,pth_before,PTH_SIZE);pth_globals(before,1);pth_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);pth_globals(actual,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(head_after!=head||pair[0]!=pair[1]||calls!=pth_calls||memcmp(expected,pth_log,calls*sizeof(*expected))||memcmp(pth_after,pth_arena,PTH_SIZE)||
           pth_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=pth_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("part hit poison uninstall");pth_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state/FS:0\n",d->name,total);
    return status;
}
/* Real callees with the nested hooks in rotating original/replaced/compare
 * modes (compare falls back to replaced where refused): multiply_q10,
 * rng_next, pool_alloc over a real pool of 0..2 nodes and debris_table
 * over the image's debris records. The record stub, part_ratio, the slots,
 * 434010, 420a68, 40f57c, 40da38, explosion_create and debris_burst stay
 * stubs. Pair, arena, globals and FS:0 are compared against a run with
 * only the nested hooks. */
static int pth_real_pass(unsigned index){
    static const unsigned keep=1u|1u<<2|1u<<5|1u<<6|1u<<8|1u<<9|1u<<11|1u<<13;
    const unsigned nested[4]={ES2_HOOK_INDEX_multiply_q10,ES2_HOOK_INDEX_rng_next,ES2_HOOK_INDEX_pool_alloc,ES2_HOOK_INDEX_debris_table};
    const unsigned count=4;
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[4]={0};
    BYTE saved[PTH_IDS][5];int active[PTH_IDS];
    if(pth_patch(saved,active,keep,1))return 1;
    pth_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u,head,head_after;pth_build(n,a,r,1);
        BYTE before[PTH_GLOBALS],after[PTH_GLOBALS],actual[PTH_GLOBALS];
        memcpy(pth_before,pth_arena,PTH_SIZE);pth_globals(before,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head));
        es2_hook hk[5];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("part hit nested install");}
        pth_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        memcpy(pth_after,pth_arena,PTH_SIZE);pth_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+4,d,&o,function_hook_extension(index)))return fail("part hit caller install");
        memcpy(pth_arena,pth_before,PTH_SIZE);pth_globals(before,1);pth_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);pth_globals(actual,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
        if(head_after!=head||expected_pair[0]!=actual_pair[0]||expected_pair[1]!=actual_pair[1]||memcmp(pth_after,pth_arena,PTH_SIZE)||
           pth_globals_differ(after,actual)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            fprintf(stderr,"real mode=%u head %08x/%08x\n",mode,head,head_after);pth_calls=0;status=pth_report("real",n,pair,0,NULL);
        }
        for(unsigned j=0;j<5;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("part hit nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("part hit nested uninstall");}
        pth_globals(before,1);++total;
    }
    pth_real=0;
    if(pth_patch(saved,active,0,0))return 1;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, 31/a7 stack, full pair, memory and FS:0\n",d->name,total,count);
    return status;
}
static int mech_part_hit_cases(unsigned index,unsigned limit){
    const unsigned which=(1u<<PTH_IDS)-1u;
    BYTE saved[PTH_IDS][5],old_globals[PTH_GLOBALS];int active[PTH_IDS];
    pth_globals(old_globals,0);
    if(pth_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_MECH_PART_HIT_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];pth_build(n,a,r,0);status=pth_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=pth_installed_probes(index,64);
    if(pth_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=pth_real_pass(index);
    pth_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, frame payloads by content, arena/globals/FS:0\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
