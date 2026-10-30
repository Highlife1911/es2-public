/* Keyboard group: key_message 477bec (docs/reverse-engineering/input.md,
 * "keyboard message handler 477bec"). The hook is stdcall with four slots; the
 * original runs through es2_hook_machine_call as STDCALL. Direct cases run the
 * original and the core against logging stubs: GetKeyState through its IAT slot
 * 6cc438 (stdcall stub, reached through the real thunk 49695e), the focus
 * helper 4669b4 (patched) and four cdecl hot-key callbacks placed in the
 * entries 6bb8b8. Logged per call: the stub, its argument, entry EAX/EDX/ECX
 * and the returned EAX/EDX. GetKeyState returns random words (bit 15 decides
 * each modifier); the callbacks return 1, 0, 2 or random dwords and may toggle
 * an entry, move a list, move either ring index (the write index up to 13Fh,
 * beyond the ring) or the device pointer, all of which the original rereads.
 * Compared: EAX and EDX, the trace, the arena (lists, device dwords) and the
 * windows ring + indices + entries 6bb6b0..6bb92f, the device pointer 6c5eb8
 * and the key table 4a10ec (shipped, or random with 0, the special codes and
 * F10 in a quarter of the cases). ECX after the call is not part of the
 * contract (the hook returns the entry ECX, the original its loop counter on
 * the hot-key path; dead at 466056). Then installed 31/a7 poison probes and a
 * real-callee pass with the real callbacks 45a324 (flight keys), 45a498
 * (command-key queue) and 477a7c (empty) on their shipped lists 49eb0b, 49eb35
 * and 49eabc, the hook original or replaced. */
typedef struct kb_record { uint32_t id,arg,regs[3],eax,edx; } kb_record;
enum { KB_SIZE=0x200, KB_LISTS=0, KB_DEV1=0x100, KB_DEV2=0x110, KB_MAX=16, KB_IDS=6,
    KB_RING=0x2bb6b0, KB_RING_LEN=0x280, KB_WRITE=0x200, KB_READ=0x204, KB_ENTRIES=0x208,
    KB_DEVICE=0x2c5eb8, KB_TABLE=0xa10ec, KB_TABLE_LEN=0xdf*4, KB_IAT=0x2cc438, KB_FOCUS=0x669b4,
    KB_GLOBALS=KB_RING_LEN+4+KB_TABLE_LEN };
#define KB_BASE ((uint32_t)(uintptr_t)kb_arena)
static BYTE kb_arena[KB_SIZE],kb_before[KB_SIZE],kb_after[KB_SIZE];
static kb_record kb_log[KB_MAX];
static unsigned kb_calls;
static uint32_t kb_seed,kb_stream;
__attribute__((used)) static uint32_t kb_ecx;
static uint32_t kb_next(void){
    uint32_t z=kb_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
__attribute__((used,noinline)) static uint64_t __cdecl kb_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(kb_calls==KB_MAX){fail("keyboard synthetic call overflow");ExitProcess(1);}
    kb_record *v=kb_log+kb_calls++;memset(v,0,sizeof(*v));v->id=id;v->arg=id==1?0:a[0];memcpy(v->regs,regs,12);
    uint32_t eax=kb_next(),edx=kb_next(),pick=kb_next();kb_ecx=kb_next();
    if(id>=2){
        static const uint32_t results[3]={1,0,2};
        if((pick&3)<3)eax=results[pick&3];
        BYTE *e=mapped+KB_RING+KB_ENTRIES+12*((pick>>8)%10);
        if(pick%5==0)put32(e,pick>>12&1?0:1);
        if(pick%7==0)put32(e+4,KB_BASE+KB_LISTS+16*((pick>>13)%10));
        if(pick%11==0)put32(mapped+KB_RING+KB_WRITE,kb_next()%(pick>>17&1?0x140u:256u));
        if(pick%13==0)put32(mapped+KB_RING+KB_READ,kb_next()&0xffu);
        if(pick%17==0)put32(mapped+KB_DEVICE,KB_BASE+(pick>>18&1?KB_DEV2:KB_DEV1));
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define KB_STUB(n,ret) __attribute__((naked)) static void kb_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _kb_stub_impl\n\taddl $24,%esp\n\tmovl _kb_ecx,%ecx\n\t" ret "\n\t");}
KB_STUB(0,"ret $4") KB_STUB(1,"ret") KB_STUB(2,"ret") KB_STUB(3,"ret") KB_STUB(4,"ret") KB_STUB(5,"ret")
#undef KB_STUB
static void *kb_stubs[KB_IDS]={kb_stub_0,kb_stub_1,kb_stub_2,kb_stub_3,kb_stub_4,kb_stub_5};
/* The original VA a counter is keyed by (callbacks: the stub's address). */
static uint32_t kb_key(uint32_t id){
    return id==0?0x49695eu:id==1?0x4669b4u:(uint32_t)(uintptr_t)kb_stubs[id];
}
static void kb_globals(BYTE *out,int restore){
    BYTE *p[3]={mapped+KB_RING,mapped+KB_DEVICE,mapped+KB_TABLE};
    const unsigned len[3]={KB_RING_LEN,4,KB_TABLE_LEN};
    for(unsigned i=0,at=0;i<3;at+=len[i++]){if(restore)memcpy(p[i],out+at,len[i]);else memcpy(out+at,p[i],len[i]);}
}
static int kb_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<KB_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
static void kb_reset(void){kb_calls=0;kb_stream=kb_seed;}
static BYTE kb_shipped[KB_TABLE_LEN];
static const uint32_t kb_key_messages[4]={0x100,0x101,0x104,0x105};
/* The ring with random contents; the write index mostly inside it, else up
 * to 13Fh (the original's unmasked slot then lands on the indices or the
 * entries); the read index one ahead (full) in a quarter of the cases. */
static void kb_ring(void){
    random_bytes(mapped+KB_RING,0x200);
    uint32_t w=random32()%10?random32()%256:256+random32()%0x40;put32(mapped+KB_RING+KB_WRITE,w);
    put32(mapped+KB_RING+KB_READ,random32()%4==0?(w+1)&0xffu:random32()%8?random32()%256:random32());
    put32(mapped+KB_DEVICE,KB_BASE+(random32()%2?KB_DEV1:KB_DEV2));
}
static void kb_build(unsigned n,uint32_t a[6],uint32_t r[3]){
    (void)n;BYTE *table=mapped+KB_TABLE;
    kb_seed=random32();random_bytes(kb_arena,KB_SIZE);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    if(random32()%4==0){
        static const uint32_t codes[]={0x0f,0x01,0x44,0x48,0x1c,0x20f,0x401,0x201,0x1ff44};
        for(unsigned k=0;k<0xdf;++k){uint32_t x=random32();put32(table+4*k,x%3==0?0:x%3==1?codes[(x>>8)%9]:random32());}
    }
    else memcpy(table,kb_shipped,KB_TABLE_LEN);
    static const uint32_t others[8]={0xff,0x102,0x103,0x106,0x107,0,0x200,0x1100};
    uint32_t pick=random32();
    a[0]=random32();a[3]=random32();a[4]=random32();a[5]=random32();
    a[1]=pick%8<6?kb_key_messages[pick>>3&3]:random32()%4?others[random32()%8]:random32();
    const uint32_t k=random32()%16;
    a[2]=k==0?0x09:k==1?0x1b:k==2?0x79:k==3?(random32()%2?0xdf+random32()%0x40:random32()):random32()%0xdf;
    kb_ring();
    /* Lists of up to seven bytes that hold the code's low byte (with 80h
     * for key-up) in a third of their places. */
    const uint32_t code=a[2]<0xdf?get32(table+4*a[2]):0;
    const BYTE low=(BYTE)(code|(a[1]==0x101||a[1]==0x105?0x80u:0u));
    for(unsigned i=0;i<10;++i){
        BYTE *list=kb_arena+KB_LISTS+16*i;unsigned len=random32()%8;
        for(unsigned j=0;j<len;++j){BYTE b=random32()%3==0?low:(BYTE)random32();list[j]=b?b:0x48;}
        list[len]=0;
        BYTE *e=mapped+KB_RING+KB_ENTRIES+12*i;uint32_t x=random32();
        put32(e,x%5<2?0:x%5<4?1:random32());put32(e+4,KB_BASE+KB_LISTS+16*(random32()%10));
        put32(e+8,(uint32_t)(uintptr_t)kb_stubs[2+random32()%4]);
    }
}
static int kb_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const kb_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,kb_calls);
    for(unsigned j=0;j<calls&&j<kb_calls&&expected;++j)if(memcmp(expected+j,kb_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(kb_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<KB_SIZE;++j)if(kb_after[j]!=kb_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,kb_after[j],kb_arena[j]);break;}
    return 1;
}
static uint64_t kb_original(unsigned index,const uint32_t *a,const uint32_t *registers){
    es2_hook_result r;es2_hook_call c={mapped+function_hook_descriptor(index)->rva,ES2_ABI_STDCALL,4,a,initial_fpu,&r,registers};
    es2_hook_machine_call(&c);
    if(get16(r.fpu)!=get16(initial_fpu)||get16(r.fpu+4)!=get16(initial_fpu+4)||get16(r.fpu+8)!=get16(initial_fpu+8)){
        fail("keyboard changed x87 control/status/tags");ExitProcess(1);
    }
    return (uint64_t)r.edx<<32|r.eax;
}
static unsigned kb_expected_hits(const callee_counter *c){
    unsigned hits=0;for(unsigned j=0;j<kb_calls;++j)if(kb_key(kb_log[j].id)==c->key)++hits;return hits;
}
static int kb_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[KB_GLOBALS],after[KB_GLOBALS],actual[KB_GLOBALS];static kb_record expected[KB_MAX];
    memcpy(kb_before,kb_arena,KB_SIZE);kb_globals(before,0);
    kb_reset();uint64_t pair[2];pair[0]=kb_original(index,a,r);unsigned calls=kb_calls;memcpy(expected,kb_log,calls*sizeof(*expected));
    memcpy(kb_after,kb_arena,KB_SIZE);kb_globals(after,0);
    memcpy(kb_arena,kb_before,KB_SIZE);kb_globals(before,1);kb_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];for(unsigned i=0;i<count&&i<8;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);kb_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count&&i<8;++i)if(c[i].count-counts[i]!=kb_expected_hits(c+i))return fail("keyboard callee counter delta");
    if(pair[0]!=pair[1]||calls!=kb_calls||memcmp(expected,kb_log,calls*sizeof(*expected))||memcmp(kb_after,kb_arena,KB_SIZE)||
       kb_globals_differ(after,actual))
        return kb_report("direct",n,pair,calls,expected);
    return 0;
}
static int kb_patch(BYTE saved_focus[5],uint32_t *saved_iat,int on){
    if(on){*saved_iat=get32(mapped+KB_IAT);put32(mapped+KB_IAT,(uint32_t)(uintptr_t)kb_stubs[0]);
        return actor_patch(KB_FOCUS,kb_stubs[1],saved_focus,TRUE);}
    put32(mapped+KB_IAT,*saved_iat);return actor_patch(KB_FOCUS,NULL,saved_focus,FALSE);
}
static int kb_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;kb_build(n,a,r);
        BYTE before[KB_GLOBALS],after[KB_GLOBALS],actual[KB_GLOBALS],expected_fpu[108],actual_fpu[108];static kb_record expected[KB_MAX];
        memcpy(kb_before,kb_arena,KB_SIZE);kb_globals(before,0);
        kb_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=kb_calls;memcpy(expected,kb_log,calls*sizeof(*expected));memcpy(kb_after,kb_arena,KB_SIZE);kb_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("keyboard poison install");
        memcpy(kb_arena,kb_before,KB_SIZE);kb_globals(before,1);kb_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);es2_hook_fpu_save(actual_fpu);kb_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=kb_calls||memcmp(expected,kb_log,calls*sizeof(*expected))||memcmp(kb_after,kb_arena,KB_SIZE)||
           kb_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=kb_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("keyboard poison uninstall");kb_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, stdcall RET 10h, nonempty x87, EAX/EDX/trace/arena/globals\n",d->name,total);
    return status;
}
/* State the real callbacks read and write: the flight-key arrays 4d2394,
 * 4d23a3, 4d23b2 and the Alt word 4d23d6; the pause words 4d2530/4d2532;
 * the byte 4d2571; the queue index 49eaa0, the queue flag 49eaa4, the
 * capture flag 49eaa5; the cheat flag 49ef38; the queue 4d20c4. */
enum { KR_WINDOWS=6 };
static const uint32_t kr_rva[KR_WINDOWS]={0xd2394,0xd2530,0xd2571,0x9eaa0,0x9ef38,0xd20c4};
static const unsigned kr_len[KR_WINDOWS]={0x44,4,1,8,4,0x40};
#define KR_BYTES (0x44+4+1+8+4+0x40)
static void kr_windows(BYTE *out,int restore){
    for(unsigned i=0,at=0;i<KR_WINDOWS;at+=kr_len[i++]){
        if(restore)memcpy(mapped+kr_rva[i],out+at,kr_len[i]);else memcpy(out+at,mapped+kr_rva[i],kr_len[i]);
    }
}
static void kr_build(uint32_t a[6],uint32_t r[3]){
    kb_seed=random32();random_bytes(kb_arena,KB_SIZE);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    memcpy(mapped+KB_TABLE,kb_shipped,KB_TABLE_LEN);
    static const uint32_t keys[]={0x25,0x26,0x27,0x28,0x20,0x0d,0x6b,0x6d,0xbd,0xbb,0x1b,0x09,0x79,0x21,0x22,0x24};
    a[0]=random32();a[3]=random32();a[4]=random32();a[5]=random32();
    a[1]=random32()%8?kb_key_messages[random32()%4]:0x102;
    a[2]=random32()%4?keys[random32()%16]:random32()%0xdf;
    kb_ring();
    BYTE *entries=mapped+KB_RING+KB_ENTRIES;memset(entries,0,120);
    static const uint32_t lists[3]={0x49eabc,0x49eb0b,0x49eb35},callbacks[3]={0x477a7c,0x45a324,0x45a498};
    unsigned slot[3];slot[0]=random32()%10;do slot[1]=random32()%10;while(slot[1]==slot[0]);
    do slot[2]=random32()%10;while(slot[2]==slot[0]||slot[2]==slot[1]);
    for(unsigned i=0;i<3;++i){BYTE *e=entries+12*slot[i];put32(e,random32()%4?1:0);put32(e+4,lists[i]);put32(e+8,callbacks[i]);}
    for(unsigned i=0;i<0x2d;++i)mapped[0xd2394+i]=(BYTE)(random32()%3==0);
    put16(mapped+0xd23d6,(uint16_t)random32());
    put16(mapped+0xd2530,(uint16_t)(random32()%4?0:random32()));put16(mapped+0xd2532,(uint16_t)random32());
    mapped[0xd2571]=(BYTE)(random32()%4==0);
    put32(mapped+0x9eaa0,random32()%31);mapped[0x9eaa4]=(BYTE)(random32()%2);mapped[0x9eaa5]=(BYTE)(random32()%4!=0);
    put32(mapped+0x9ef38,random32()%2);random_bytes(mapped+0xd20c4,0x40);
}
static int kb_real_pass(unsigned index){
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t start[8];
    for(unsigned i=0;i<count&&i<8;++i)start[i]=c[i].count;
    for(unsigned mode=0;mode<2&&!status;++mode)for(unsigned n=0;n<1536&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;kr_build(a,r);
        BYTE before[KB_GLOBALS],after[KB_GLOBALS],actual[KB_GLOBALS],wb[KR_BYTES],wa[KR_BYTES],wx[KR_BYTES];
        memcpy(kb_before,kb_arena,KB_SIZE);kb_globals(before,0);kr_windows(wb,0);
        kb_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        memcpy(kb_after,kb_arena,KB_SIZE);kb_globals(after,0);kr_windows(wa,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        o.mode=mode?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("keyboard real install");
        memcpy(kb_arena,kb_before,KB_SIZE);kb_globals(before,1);kr_windows(wb,1);kb_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);kb_globals(actual,0);kr_windows(wx,0);
        if(memcmp(expected_pair,actual_pair,8)||memcmp(kb_after,kb_arena,KB_SIZE)||kb_globals_differ(after,actual)||memcmp(wa,wx,KR_BYTES)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            fprintf(stderr,"real mode=%u\n",mode);kb_calls=0;status=kb_report("real",n,pair,0,NULL);
        }
        if(!es2_hook_uninstall(&h))return fail("keyboard real uninstall");
        kb_globals(before,1);kr_windows(wb,1);++total;
    }
    /* The replaced runs reached each real callback (counters call_45a324,
     * call_45a498, call_477a7c). */
    for(unsigned i=0;i<count&&i<8&&!status;++i)if(c[i].key!=0x4669b4u&&c[i].count==start[i]){
        fprintf(stderr,"FAIL: %s real callee %s never counted\n",d->name,c[i].name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases with the callbacks 45a324, 45a498, 477a7c on their shipped lists, EAX/EDX, ring/entries and callback state\n",d->name,total);
    return status;
}
static int keyboard_cases(unsigned index,unsigned limit){
    BYTE saved_focus[5],old_globals[KB_GLOBALS],old_windows[KR_BYTES];uint32_t saved_iat;
    kb_globals(old_globals,0);kr_windows(old_windows,0);memcpy(kb_shipped,mapped+KB_TABLE,KB_TABLE_LEN);
    if(kb_patch(saved_focus,&saved_iat,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_KEYBOARD_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];kb_build(n,a,r);status=kb_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=kb_installed_probes(index,64);
    current_case_valid=0;
    if(!limit&&!status)status=kb_real_pass(index);
    if(kb_patch(saved_focus,&saved_iat,0))return 1;
    kb_globals(old_globals,1);kr_windows(old_windows,1);
    if(!status)printf("PASS: %-20s %u cases; EAX/EDX, ordered calls with entry registers, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
