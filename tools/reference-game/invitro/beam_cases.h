/* Beam group: beam_create 40b804, the BEAM constructor below impact
 * (docs/reverse-engineering/weapons.md, "beam constructor 40b804"). Direct
 * cases run the original body and the core against logging stubs for every game
 * callee (object_init 402188, dist_gate 49288c, the runtime's array helper
 * 49667c, set_length 4927f0, rng_next 492ee0): ordered calls with their
 * arguments and entry EAX/EDX/ECX (stack values masked; the first RNG call's
 * ECX keeps its low word when it derives from the frame), the points and the
 * step vector by content. The stubs return edge distances (negative and wrapped
 * section counts included) and change what the original rereads after a call:
 * the count byte O+54h, the array pointer O+58h and the height-table pointer
 * 4a9804. F and T are separate, inside the array or inside the object, so the
 * order of every read against the writes is compared. The runtime counter
 * pointer 6cbee4, the table pointer and the RNG state 4d2599 are compared and
 * restored. Then installed 31/a7 poison probes and a real-callee pass with
 * object_init, dist_gate, set_length and rng_next nested as hooks in rotating
 * modes (49667c stays a stub that returns an arena array; object_init's runtime
 * frame setup 49630c is a record stub that keeps the real FS:0 head). */
typedef struct bm_record { uint32_t id,args[5],regs[3],input[6],eax,edx; } bm_record;
enum { BM_SIZE=0x2000, BM_O=0x100, BM_F=0x200, BM_T=0x220, BM_A=0x400, BM_A2=0x1000, BM_COUNTER=0x1d00,
    BM_TABLE=0x1d40, BM_TABLE2=0x1d80, BM_LOG=512, BM_IDS=6, BM_STATE=0xd2599, BM_GLOBALS=4+4+114 };
#define BM_BASE ((uint32_t)(uintptr_t)bm_arena)
static BYTE bm_arena[BM_SIZE],bm_before[BM_SIZE],bm_after[BM_SIZE];
static bm_record bm_log[BM_LOG];
static unsigned bm_calls;
static int bm_real;
static uint32_t bm_seed,bm_stream,bm_array;
__attribute__((used)) static uint32_t bm_ecx;
/* 0 402188 object_init, 1 49288c dist_gate, 2 49667c array helper,
 * 3 4927f0 set_length, 4 492ee0 rng_next, 5 49630c runtime frame setup
 * (reached only through the real object_init). */
static const uint32_t bm_addresses[BM_IDS]={0x402188,0x49288c,0x49667c,0x4927f0,0x492ee0,0x49630c};
static const unsigned bm_counts[BM_IDS]={2,2,5,2,1,0};
static uint32_t bm_next(void){
    uint32_t z=bm_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *bm_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int bm_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static int bm_ok(uint32_t p,uint32_t n){
    const NT_TIB *t=(const NT_TIB *)NtCurrentTeb();uint32_t low=(uint32_t)(uintptr_t)t->StackLimit,top=(uint32_t)(uintptr_t)t->StackBase;
    return (p-BM_BASE<BM_SIZE&&BM_SIZE-(p-BM_BASE)>=n)||(p>=low&&p<top&&top-p>=n);
}
static void bm_dwords(bm_record *v,unsigned at,uint32_t p,unsigned n){
    if(bm_ok(p,4u*n))for(unsigned j=0;j<n;++j)v->input[at+j]=get32(bm_host(p)+4*j);
}
__attribute__((used,noinline)) static uint64_t __cdecl bm_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(bm_calls==BM_LOG){fail("beam synthetic call overflow");ExitProcess(1);}
    bm_record *v=bm_log+bm_calls++;memset(v,0,sizeof(*v));v->id=id;
    for(unsigned k=0;k<bm_counts[id];++k)v->args[k]=bm_stack(a[k])?0:a[k];
    if(!bm_real){
        for(unsigned j=0;j<3;++j)v->regs[j]=bm_stack(regs[j])?0:regs[j];
        /* rng_next's first call per section: ECX = 2 * (P's high word : i);
         * with P = &cur the frame differs, the low word (= 2i) does not. */
        if(id==4&&bm_stack(regs[2]>>1))v->regs[2]=regs[2]&0xffffu;
    }
    uint32_t eax=bm_next(),edx=bm_next(),pick=bm_next();bm_ecx=bm_next();
    BYTE *o=bm_arena+BM_O;const uint32_t b=BM_BASE;
    switch(id){
    case 0: /* object_init: rewrites the head of O (as the real one does). */
        if(!bm_real&&pick%3==0)for(unsigned j=0;j<0x40;++j)o[j]=(BYTE)bm_next();
        break;
    case 1: { /* dist_gate: F and T by content; distances around the
               * section-count edges, the byte wrap and negative values. */
        bm_dwords(v,0,a[0],3);bm_dwords(v,3,a[1],3);
        static const uint32_t edges[]={0,1,1023,1024,1025,2047,2048,5000,20000,126u*1024u+1023u,127u*1024u,128u*1024u-1u,
            128u*1024u,255u*1024u,256u*1024u,257u*1024u+5u,0x7fffffffu,0x80000000u,0xffffffffu,0xfffffc00u,0xfffff800u};
        unsigned k=pick%8;
        if(k<4)eax=(pick>>4)%(24u*1024u);
        else if(k<7)eax=edges[(pick>>4)%21];
        break; }
    case 2: /* array helper: an arena array with room for 256 elements. */
        eax=bm_array;
        break;
    case 3: /* set_length: the step vector by content and rewritten; the
             * count byte O+54h may change (the loop rereads it). */
        bm_dwords(v,0,a[0],3);
        if(!bm_real&&bm_ok(a[0],12))for(unsigned j=0;j<3;++j)put32(bm_host(a[0])+4*j,pick>>20&1?bm_next():(uint32_t)((int32_t)(bm_next()%4096u)-2048));
        if(!bm_real&&pick%7==0)o[0x54]=(BYTE)(pick>>8&1?bm_next():bm_next()%8u);
        break;
    case 4: /* rng_next: edge low words; O+54h, O+58h and the table
             * pointer may change between sections. */
        if(pick%4==0)eax=(eax&0xffff0000u)|(uint16_t)(pick>>8&1?0x7f:0xff80u);
        if(!bm_real)switch(pick>>12&31){
        case 1:o[0x54]=(BYTE)(bm_next()%6u);break;
        case 2:put32(o+0x58,b+(pick>>20&1?BM_A2:BM_A));break;
        case 3:put32(mapped+0xa9804,b+(pick>>20&1?BM_TABLE2:BM_TABLE));break;
        default:break;
        }
        break;
    case 5: /* 49630c: the record stub returns the real FS:0 head. */
        __asm__ volatile("movl %%fs:0,%0":"=r"(eax));
        break;
    }
    v->eax=bm_stack(eax)?0:eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define BM_STUB(n) __attribute__((naked)) static void bm_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _bm_stub_impl\n\taddl $24,%esp\n\tmovl _bm_ecx,%ecx\n\tret\n\t");}
BM_STUB(0) BM_STUB(1) BM_STUB(2) BM_STUB(3) BM_STUB(4)
#undef BM_STUB
/* 49630c: logs like the others, then links a record at EBP + [EAX+4]
 * (EAX = the frame descriptor at entry) with the FS:0 head at record+0 and
 * a zero context word at record+16, and makes it the FS:0 head. */
__attribute__((naked)) static void bm_stub_5(void){__asm__ volatile(
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
    "pushl $5\n\tcalll _bm_stub_impl\n\taddl $12,%esp\n\t"
    "popl %ecx\n\tmovl 4(%ecx),%ecx\n\taddl %ebp,%ecx\n\tmovl %eax,(%ecx)\n\tmovw $0,16(%ecx)\n\tmovl %ecx,%fs:0\n\t"
    "popl %edx\n\taddl $4,%esp\n\tmovl _bm_ecx,%ecx\n\tret\n\t");}
static void *bm_stubs[BM_IDS]={bm_stub_0,bm_stub_1,bm_stub_2,bm_stub_3,bm_stub_4,bm_stub_5};
static void bm_reset(void){bm_calls=0;bm_stream=bm_seed;}
static void bm_globals(BYTE *out,int restore){
    static const uint32_t rva[3]={0x2cbee4,0xa9804,BM_STATE},size[3]={4,4,114};
    for(unsigned i=0,at=0;i<3;at+=size[i++]){
        if(restore)memcpy(mapped+rva[i],out+at,size[i]);else memcpy(out+at,mapped+rva[i],size[i]);
    }
}
static int bm_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<BM_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
/* A point: separate (F 200h, T 220h), inside the array region (any
 * alignment) or inside the object (O+20h..O+60h, overlapping the fields
 * the constructor writes). real: always separate. */
static uint32_t bm_point(uint32_t separate,int real){
    unsigned k=random32()%8;const uint32_t b=BM_BASE;
    if(real||k<5)return b+separate;
    if(k<7)return b+BM_A+random32()%0x60u;
    return b+BM_O+0x20u+random32()%0x41u;
}
/* Object, points, array base, counter, height tables, RNG state. The type
 * word is 1 or 7 in most cases (any high word), else an edge or random
 * word. real: coordinates bounded so dist_gate gives up to about 200
 * sections (the byte wrap included). */
static void bm_build(unsigned n,uint32_t a[6],uint32_t r[3],int real){
    const uint32_t b=BM_BASE;
    bm_seed=random32();random_bytes(bm_arena,BM_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    put32(mapped+0x2cbee4,b+BM_COUNTER);put32(mapped+0xa9804,b+BM_TABLE);
    random_bytes(mapped+BM_STATE,112);mapped[BM_STATE+112]=(BYTE)(random32()%56);mapped[BM_STATE+113]=(BYTE)(random32()%56);
    bm_array=b+BM_A+(random32()%4?0:4u*(random32()%3));
    static const uint16_t types[]={1,7,1,7,0,2,6,8,0xffff,0x8001,0x8007,0x101,0x107,3};
    uint16_t t=n<1024?types[n%14]:random32()%4?(random32()%2?1:7):random32()%3?types[random32()%14]:(uint16_t)random32();
    a[0]=b+BM_O;a[1]=(random32()&0xffff0000u)|t;
    a[2]=bm_point(BM_F,real);a[3]=bm_point(BM_T,real);a[4]=random32();
    if(real){
        uint32_t span=random32()%4?40000u:120000u;
        for(unsigned j=0;j<3;++j){put32(bm_host(a[2])+4*j,random32()%(2u*span+1u)-span);put32(bm_host(a[3])+4*j,random32()%(2u*span+1u)-span);}
    }
}
static int bm_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const bm_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,bm_calls);
    for(unsigned j=0;j<calls&&j<bm_calls&&expected;++j)if(memcmp(expected+j,bm_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(bm_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<BM_SIZE;++j)if(bm_after[j]!=bm_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,bm_after[j],bm_arena[j]);break;}
    return 1;
}
static unsigned bm_expected_hits(const callee_counter *c){
    unsigned hits=0;for(unsigned j=0;j<bm_calls;++j)hits+=bm_addresses[bm_log[j].id]==c->key;return hits;
}
static int bm_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[BM_GLOBALS],after[BM_GLOBALS],actual[BM_GLOBALS];static bm_record expected[BM_LOG];
    memcpy(bm_before,bm_arena,BM_SIZE);bm_globals(before,0);
    bm_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=bm_calls;memcpy(expected,bm_log,calls*sizeof(*expected));
    memcpy(bm_after,bm_arena,BM_SIZE);bm_globals(after,0);
    memcpy(bm_arena,bm_before,BM_SIZE);bm_globals(before,1);bm_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];for(unsigned i=0;i<count&&i<8;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);bm_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count&&i<8;++i)if(c[i].count-counts[i]!=bm_expected_hits(c+i))return fail("beam callee counter delta");
    if(pair[0]!=pair[1]||calls!=bm_calls||memcmp(expected,bm_log,calls*sizeof(*expected))||memcmp(bm_after,bm_arena,BM_SIZE)||
       bm_globals_differ(after,actual))
        return bm_report("direct",n,pair,calls,expected);
    return 0;
}
static int bm_patch(BYTE saved[BM_IDS][5],int active[BM_IDS],unsigned which,int on){
    for(unsigned i=0;i<BM_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(bm_addresses[i]-0x400000u,bm_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(bm_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
static int bm_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;bm_build(n,a,r,0);
        BYTE before[BM_GLOBALS],after[BM_GLOBALS],actual[BM_GLOBALS],expected_fpu[108],actual_fpu[108];static bm_record expected[BM_LOG];
        memcpy(bm_before,bm_arena,BM_SIZE);bm_globals(before,0);
        bm_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=bm_calls;memcpy(expected,bm_log,calls*sizeof(*expected));memcpy(bm_after,bm_arena,BM_SIZE);bm_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("beam poison install");
        memcpy(bm_arena,bm_before,BM_SIZE);bm_globals(before,1);bm_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);es2_hook_fpu_save(actual_fpu);bm_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=bm_calls||memcmp(expected,bm_log,calls*sizeof(*expected))||memcmp(bm_after,bm_arena,BM_SIZE)||
           bm_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=bm_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("beam poison uninstall");bm_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/arena/globals\n",d->name,total);
    return status;
}
/* Real callees: object_init (with the 49630c record stub), dist_gate,
 * set_length and rng_next nested as hooks in rotating
 * original/replaced/compare modes (compare falls back to replaced where
 * refused), the hook itself original or replaced; pair, arena, globals
 * (RNG state included) and the stub log (arguments, payloads; registers
 * are not recorded because nested replacements may return other ECX
 * values) against a run with only the nested hooks installed. */
static int bm_real_pass(unsigned index){
    static const unsigned nested[]={ES2_HOOK_INDEX_object_init,ES2_HOOK_INDEX_dist_gate,ES2_HOOK_INDEX_set_length,ES2_HOOK_INDEX_rng_next};
    const unsigned count=4,stubs=(1u<<2)|(1u<<5);
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[4]={0};
    BYTE saved[BM_IDS][5],old_globals[BM_GLOBALS];int active[BM_IDS];if(bm_patch(saved,active,stubs,1))return 1;
    static bm_record expected[BM_LOG];
    bm_globals(old_globals,0);bm_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;bm_build(n,a,r,1);
        BYTE before[BM_GLOBALS],after[BM_GLOBALS],actual[BM_GLOBALS];
        memcpy(bm_before,bm_arena,BM_SIZE);bm_globals(before,0);
        es2_hook hk[5];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("beam nested install");}
        bm_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        unsigned calls=bm_calls;memcpy(expected,bm_log,calls*sizeof(*expected));
        memcpy(bm_after,bm_arena,BM_SIZE);bm_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+count,d,&o,function_hook_extension(index)))return fail("beam caller install");
        memcpy(bm_arena,bm_before,BM_SIZE);bm_globals(before,1);bm_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);bm_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=bm_calls||memcmp(expected,bm_log,calls*sizeof(*expected))||memcmp(bm_after,bm_arena,BM_SIZE)||
           bm_globals_differ(after,actual)){
            fprintf(stderr,"real mode=%u\n",mode);status=bm_report("real",n,pair,calls,expected);
        }
        for(unsigned j=0;j<=count;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("beam nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("beam nested uninstall");}
        bm_globals(before,1);++total;
    }
    bm_real=0;bm_globals(old_globals,1);
    if(bm_patch(saved,active,0,0))return 1;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){
        fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, 31/a7 stack, pair, arena, globals and stub log\n",
        d->name,total,count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
static int beam_cases(unsigned index,unsigned limit){
    bm_real=0;
    BYTE saved[BM_IDS][5],old_globals[BM_GLOBALS];int active[BM_IDS];
    bm_globals(old_globals,0);if(bm_patch(saved,active,0x1fu,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_BEAM_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];bm_build(n,a,r,0);status=bm_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    bm_globals(old_globals,1);
    if(!limit&&!status)status=bm_installed_probes(index,64);
    bm_globals(old_globals,1);
    if(bm_patch(saved,active,0,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=bm_real_pass(index);
    bm_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, payloads by content, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
