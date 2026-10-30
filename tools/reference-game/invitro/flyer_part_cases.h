/* FLYER part damage 421bb4, slot +74h of FLYER
 * (docs/reverse-engineering/hit_tests.md, section "FLYER part
 * damage"). Direct cases run the original body and the core against logging
 * stubs for every game callee: 40da38 (answers a planned AX: zero or not),
 * 420a68, the source's slot +60h (two arena vtables), death_actions 411bc8,
 * trigger_fire 423430, script_start 413e50 and the sound 462ac4; ordered calls
 * with their arguments, entry EAX/EDX/ECX and the function's own stores as seen
 * at each call (the velocity global 4a9660, the death bytes F+99h/96h/38h, the
 * dword F+2Eh, the word F+6). The stubs change what the original reads after
 * them (the player 4d24e6 and its target +1A4h, the source's vtable, the
 * trigger pointer F+1B6h, the word F+6). Compared: EAX/EDX, the original's ECX
 * (always 0), the arena, the globals and the callee counters. Then installed
 * 31/a7 poison probes through the entry shim (EAX/EDX/ECX) and a real-callee
 * pass with death_actions, trigger_fire and script_start in rotating modes, the
 * real 462ac4 and the real ACTOR slot +60h 411b2c. */
typedef struct flp_record { uint32_t id,args[3],regs[3],state[5],eax,edx; } flp_record;
enum { FLP_SIZE=0x1000, FLP_F=0x100, FLP_SRC=0x380, FLP_SRC2=0x540, FLP_VT=0x700, FLP_VT2=0x790, FLP_TRIG=0x820,
    FLP_TRIG2=0x870, FLP_MAX=16, FLP_IDS=7, FLP_GLOBALS=46 };
#define FLP_BASE ((uint32_t)(uintptr_t)flp_arena)
static BYTE flp_arena[FLP_SIZE],flp_before[FLP_SIZE],flp_after[FLP_SIZE];
static flp_record flp_log[FLP_MAX];
static unsigned flp_calls;
static int flp_real;
static uint32_t flp_seed,flp_stream;
__attribute__((used)) static uint32_t flp_ecx;
/* 40da38, 420a68, slot +60h (arena vtables, never patched), death_actions,
 * trigger_fire, script_start, 462ac4. */
static const uint32_t flp_addresses[FLP_IDS]={0x40da38,0x420a68,0,0x411bc8,0x423430,0x413e50,0x462ac4};
static const unsigned flp_slots[FLP_IDS]={0,0,0x60,0,0,0,0};
static const unsigned flp_counts[FLP_IDS]={3,1,3,1,1,2,1};
static uint32_t flp_next(void){
    uint32_t z=flp_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static int flp_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static BYTE *flp_f(void){return flp_arena+FLP_F;}
/* The function's own stores, as a callee sees them. */
static void flp_state(uint32_t out[5]){
    BYTE *f=flp_f();out[0]=get32(mapped+0xa9660);out[1]=get32(f+0x2e);
    out[2]=(uint32_t)f[0x99]|(uint32_t)f[0x96]<<8|(uint32_t)f[0x38]<<16;out[3]=get16(f+6);out[4]=get32(f+0x1b6);
}
static uint32_t flp_source_pick(uint32_t pick){
    static const uint32_t offsets[]={FLP_SRC,FLP_SRC2,FLP_F,0};
    uint32_t k=pick%4;return k==3?(pick&0x10?0:0x12345678u):FLP_BASE+offsets[k];
}
static uint32_t flp_trigger_pick(uint32_t pick){uint32_t k=pick%3;return k==0?0:FLP_BASE+(k==1?FLP_TRIG:FLP_TRIG2);}
/* Rereads between the calls (not in the real pass). */
static void flp_touch(unsigned id,uint32_t pick){
    if(flp_real)return;
    BYTE *f=flp_f();uint32_t b=FLP_BASE;
    switch(id){
    case 0: /* 40da38: the player, its target and the vtables are read after it. */
        if(pick%3==0)put32(mapped+0xd24e6,flp_source_pick(pick>>4));
        if(pick%5==0)put32(flp_arena+FLP_SRC+0x1a4,pick>>8&1?b+FLP_F:b+FLP_SRC2);
        if(pick%7==0)put32(flp_arena+FLP_SRC,pick>>9&1?b+FLP_VT:b+FLP_VT2);
        break;
    case 1: /* 420a68: the source's vtable is read after it. */
        if(pick%2==0)put32(flp_arena+FLP_SRC,pick>>9&1?b+FLP_VT:b+FLP_VT2);
        break;
    case 3: /* death_actions: the trigger pointer and F+6 are read after it. */
        if(pick%3==0)put32(f+0x1b6,flp_trigger_pick(pick>>4));
        if(pick%5==0)put16(f+6,(uint16_t)(pick>>8&1?0:pick>>12));
        break;
    case 4: case 5: /* trigger_fire, script_start: F+6 is read after them. */
        if(pick%3==0)put16(f+6,(uint16_t)(pick>>8&1?0:pick>>12));
        break;
    }
}
__attribute__((used,noinline)) static uint64_t __cdecl flp_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(flp_calls==FLP_MAX){fail("flyer part synthetic call overflow");ExitProcess(1);}
    flp_record *v=flp_log+flp_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,4*flp_counts[id]);memcpy(v->regs,regs,12);
    for(unsigned j=0;j<3;++j)if(flp_stack(v->regs[j]))v->regs[j]=0;
    for(unsigned j=0;j<flp_counts[id];++j)if(flp_stack(v->args[j]))v->args[j]=0;
    flp_state(v->state);
    uint32_t eax=flp_next(),edx=flp_next(),pick=flp_next();flp_ecx=flp_next();
    if(id==0){ /* 40da38: AX = 0 in about three of eight calls. */
        static const uint16_t ax[]={0,0,0,1,1,0x8000,0xffff,0x100};
        uint16_t w=ax[pick%8];if(pick>>3&1&&w)w=(uint16_t)(pick>>16|1u);
        eax=(eax&0xffff0000u)|w;
    }
    flp_touch(id,pick>>5);
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
/* Pushes the entry EAX/EDX/ECX for the record before the slot pointer. */
#define FLP_STUB(n) __attribute__((naked)) static void flp_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _flp_stub_impl\n\taddl $24,%esp\n\tmovl _flp_ecx,%ecx\n\tret\n\t");}
FLP_STUB(0) FLP_STUB(1) FLP_STUB(2) FLP_STUB(3) FLP_STUB(4) FLP_STUB(5) FLP_STUB(6)
#undef FLP_STUB
static void *flp_stubs[FLP_IDS]={flp_stub_0,flp_stub_1,flp_stub_2,flp_stub_3,flp_stub_4,flp_stub_5,flp_stub_6};
/* Calls target with n argument slots, EAX/EDX/ECX/EBX from regs; with
 * pattern != 0 the 4 KiB below are filled first. EAX/EDX/ECX after RET. */
__attribute__((naked,noinline)) static void flp_invoke(void *target,const uint32_t *a,unsigned n,const uint32_t regs[4],uint32_t pattern,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "cmpl $0,24(%ebp)\n\tje 1f\n\tsubl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 24(%ebp),%eax\n\tmovl $1024,%ecx\n\tcld\n\trep stosl\n\taddl $4096,%esp\n\t"
        "1: movl 12(%ebp),%esi\n\tmovl 16(%ebp),%ecx\n\t"
        "2: testl %ecx,%ecx\n\tjz 3f\n\tdecl %ecx\n\tpushl (%esi,%ecx,4)\n\tjmp 2b\n\t"
        "3: movl 20(%ebp),%esi\n\tmovl (%esi),%eax\n\tmovl 4(%esi),%edx\n\tmovl 8(%esi),%ecx\n\tmovl 12(%esi),%ebx\n\tcall *8(%ebp)\n\t"
        "movl 28(%ebp),%ebx\n\tmovl %eax,(%ebx)\n\tmovl %edx,4(%ebx)\n\tmovl %ecx,8(%ebx)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* Velocity 4a9660, player 4d24e6, the results words 4a9e70..4a9e8f (real
 * death_actions/trigger_fire), the script counter 4a9b70 (real
 * script_start) and the sound device 49f8ec (real 462ac4). */
static const struct { uint32_t rva,size; } flp_ranges[]={{0xa9660,4},{0xd24e6,4},{0xa9e70,0x20},{0xa9b70,2},{0x9f8ec,4}};
static void flp_globals(BYTE *out,int restore){
    for(unsigned i=0;i<sizeof(flp_ranges)/sizeof(flp_ranges[0]);++i){
        if(restore)memcpy(mapped+flp_ranges[i].rva,out,flp_ranges[i].size);else memcpy(out,mapped+flp_ranges[i].rva,flp_ranges[i].size);
        out+=flp_ranges[i].size;
    }
}
static int flp_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<FLP_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
static void flp_reset(void){flp_calls=0;flp_stream=flp_seed;}
/* A trigger record for the real trigger_fire: the fired word +0Ah, ten
 * result indices +0Ch (-1 or 0..15) with kinds +20h (5 clears, 6 counts),
 * ten message words +34h always -1 (no cockpit call). */
static void flp_trigger(BYTE *t){
    static const uint16_t kinds[]={5,6,6,5,0,7,0xffff};
    put16(t+0xa,(uint16_t)(random32()%4?0:1));
    for(unsigned i=0;i<10;++i){put16(t+0xc+2*i,(uint16_t)(random32()%3?random32()%16:0xffffu));
        put16(t+0x20+2*i,kinds[random32()%7]);put16(t+0x34+2*i,0xffffu);}
}
static void flp_build(unsigned n,uint32_t a[4],uint32_t r[3],int real){
    uint32_t b=FLP_BASE;BYTE *f=flp_f();
    flp_seed=random32();random_bytes(flp_arena,FLP_SIZE);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    put32(flp_arena+FLP_SRC,b+(random32()%4?FLP_VT:FLP_VT2));put32(flp_arena+FLP_SRC2,b+(random32()%4?FLP_VT2:FLP_VT));
    /* The FLYER may be a source of itself (its own vtable is the arena's). */
    put32(f,b+FLP_VT);
    uint32_t slot=real?(uint32_t)(uintptr_t)(mapped+0x11b2c):(uint32_t)(uintptr_t)flp_stubs[2];
    put32(flp_arena+FLP_VT+0x60,slot);put32(flp_arena+FLP_VT2+0x60,slot);
    /* The player and its target. */
    put32(mapped+0xd24e6,flp_source_pick(random32()));
    put32(flp_arena+FLP_SRC+0x1a4,random32()%3?b+FLP_F:b+FLP_SRC2);
    put32(flp_arena+FLP_SRC2+0x1a4,random32()%3?b+FLP_F:random32());
    put32(f+0x1a4,random32()%2?b+FLP_F:b+FLP_SRC);
    put32(f+0x1b6,flp_trigger_pick(random32()));
    put16(f+6,(uint16_t)(random32()%3?random32():0));
    put32(mapped+0xa9660,random32());
    /* Parts: low word 0 in half of the cases (incl. 10000h, FFFF0000h). */
    static const uint32_t parts[]={0,0,0x10000u,0xffff0000u,1,2,0xffff,0x8000,0x10001u,0x7fff};
    uint32_t part=n<1024?parts[n%10]:random32()%2?parts[random32()%10]:random32();
    uint32_t source=random32()%5==0?0:flp_source_pick(random32()%3);
    if(real){
        /* death_actions without a group ([F+45h] = 0); its result words
         * index 4a9e70 with -1 or 0..15 and kinds 1, 2, 0Dh..10h, others. */
        static const uint16_t kinds[]={1,2,13,14,15,16,0,3,17,0xffff};
        put32(f+0x45,0);
        for(unsigned i=0;i<10;++i){put16(f+0x1ba+2*i,(uint16_t)(random32()%3?random32()%16:0xffffu));put16(f+0x1ceu+2*i,kinds[random32()%10]);}
        flp_trigger(flp_arena+FLP_TRIG);flp_trigger(flp_arena+FLP_TRIG2);
        random_bytes(mapped+0xa9e70,0x20);put16(mapped+0xa9b70,(uint16_t)random32());put32(mapped+0x9f8ec,0);
        if(source==0x12345678u)source=0;
    }
    if(source==0x12345678u)source=b+FLP_SRC;
    a[0]=b+FLP_F;a[1]=part;a[2]=random32();a[3]=source;
}
static int flp_report(const char *what,unsigned n,const uint32_t want[3],const uint32_t got[3],unsigned calls,const flp_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x calls %u/%u\n",current_function,n,what,
        want[0],got[0],want[1],got[1],want[2],got[2],calls,flp_calls);
    for(unsigned j=0;j<calls&&j<flp_calls&&expected;++j)if(memcmp(expected+j,flp_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(flp_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<FLP_SIZE;++j)if(flp_after[j]!=flp_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,flp_after[j],flp_arena[j]);break;}
    return 1;
}
static unsigned flp_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<flp_calls;++j){unsigned id=flp_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?flp_slots[id]==c->key:!flp_slots[id]&&flp_addresses[id]==c->key)++hits;}
    return hits;
}
static int flp_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[FLP_GLOBALS],after[FLP_GLOBALS],actual[FLP_GLOBALS];static flp_record expected[FLP_MAX];
    uint32_t regs[4]={r[0],r[1],r[2],0},want[3],got[3];
    memcpy(flp_before,flp_arena,FLP_SIZE);flp_globals(before,0);
    flp_reset();flp_invoke(mapped+d->rva,a,4,regs,0,want);unsigned calls=flp_calls;memcpy(expected,flp_log,calls*sizeof(*expected));
    memcpy(flp_after,flp_arena,FLP_SIZE);flp_globals(after,0);
    memcpy(flp_arena,flp_before,FLP_SIZE);flp_globals(before,1);flp_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[FLP_IDS];
    for(unsigned i=0;i<count&&i<FLP_IDS;++i)counts[i]=c[i].count;
    uint64_t pair=x->replacement_context(a,d->user,r);flp_globals(actual,0);
    /* The adapter's ECX is the shim's constant 0. */
    got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);got[2]=0;
    if(n<4096)for(unsigned i=0;i<count&&i<FLP_IDS;++i)if(c[i].count-counts[i]!=flp_expected_hits(c+i))return fail("flyer part callee counter delta");
    if(memcmp(want,got,12)||calls!=flp_calls||memcmp(expected,flp_log,calls*sizeof(*expected))||memcmp(flp_after,flp_arena,FLP_SIZE)||
       flp_globals_differ(after,actual))
        return flp_report("direct",n,want,got,calls,expected);
    flp_globals(before,1);
    return 0;
}
static int flp_patch(BYTE saved[FLP_IDS][5],int active[FLP_IDS],unsigned which,int on){
    for(unsigned i=0;i<FLP_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!flp_addresses[i])continue;if(actor_patch(flp_addresses[i]-0x400000u,flp_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(flp_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
static int flp_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[4],r[3],want[3],got[3],pattern=poison?0xa7a7a7a7u:0x31313131u,regs[4]={pattern,pattern,pattern,pattern};flp_build(n,a,r,0);
        BYTE before[FLP_GLOBALS],after[FLP_GLOBALS],actual[FLP_GLOBALS],expected_fpu[108],actual_fpu[108];static flp_record expected[FLP_MAX];
        memcpy(flp_before,flp_arena,FLP_SIZE);flp_globals(before,0);
        flp_reset();flp_invoke(mapped+d->rva,a,4,regs,pattern,want);es2_hook_fpu_save(expected_fpu);
        unsigned calls=flp_calls;memcpy(expected,flp_log,calls*sizeof(*expected));memcpy(flp_after,flp_arena,FLP_SIZE);flp_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("flyer part poison install");
        memcpy(flp_arena,flp_before,FLP_SIZE);flp_globals(before,1);flp_reset();
        flp_invoke(mapped+d->rva,a,4,regs,pattern,got);es2_hook_fpu_save(actual_fpu);flp_globals(actual,0);
        if(want[2]||memcmp(want,got,12)||calls!=flp_calls||memcmp(expected,flp_log,calls*sizeof(*expected))||memcmp(flp_after,flp_arena,FLP_SIZE)||
           flp_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=flp_report(fast?"installed fast":"installed preserving",n,want,got,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("flyer part poison uninstall");flp_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, EAX/EDX/ECX through the entry shim, trace/arena/globals\n",d->name,total);
    return status;
}
/* Real death_actions, trigger_fire and script_start as nested hooks in
 * rotating original/replaced/compare modes (compare falls back to replaced
 * where refused), the real 462ac4 (no sound device) and the real ACTOR
 * slot +60h 411b2c; 40da38 and 420a68 stay stubs. The hook original
 * (through its shim) or replaced, against a run with only the nested
 * hooks: EAX/EDX/ECX, arena and globals. */
static int flp_real_pass(unsigned index){
    static const unsigned keep=1u|1u<<1;
    const unsigned nested[3]={ES2_HOOK_INDEX_death_actions,ES2_HOOK_INDEX_trigger_fire,ES2_HOOK_INDEX_script_start};
    const unsigned count=3;
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[3]={0};
    BYTE saved[FLP_IDS][5];int active[FLP_IDS];
    if(flp_patch(saved,active,keep,1))return 1;
    flp_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[4],r[3],want[3],got[3],pattern=n&1?0xa7a7a7a7u:0x31313131u,regs[4]={pattern,pattern,pattern,pattern};flp_build(n,a,r,1);
        BYTE before[FLP_GLOBALS],after[FLP_GLOBALS],actual[FLP_GLOBALS];
        memcpy(flp_before,flp_arena,FLP_SIZE);flp_globals(before,0);
        es2_hook hk[4];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("flyer part nested install");}
        flp_reset();flp_invoke(mapped+d->rva,a,4,regs,pattern,want);
        memcpy(flp_after,flp_arena,FLP_SIZE);flp_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+3,d,&o,function_hook_extension(index)))return fail("flyer part caller install");
        memcpy(flp_arena,flp_before,FLP_SIZE);flp_globals(before,1);flp_reset();
        flp_invoke(mapped+d->rva,a,4,regs,pattern,got);flp_globals(actual,0);
        if(want[2]||memcmp(want,got,12)||memcmp(flp_after,flp_arena,FLP_SIZE)||flp_globals_differ(after,actual)||(mode>=3&&hk[3].calls!=1)){
            fprintf(stderr,"real mode=%u\n",mode);flp_calls=0;status=flp_report("real",n,want,got,0,NULL);
        }
        for(unsigned j=0;j<4;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("flyer part nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("flyer part nested uninstall");}
        flp_globals(before,1);++total;
    }
    flp_real=0;
    if(flp_patch(saved,active,0,0))return 1;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, real 462ac4 and slot +60h, 31/a7 stack, EAX/EDX/ECX, memory\n",d->name,total,count);
    return status;
}
static int flyer_part_cases(unsigned index,unsigned limit){
    const unsigned which=(1u<<FLP_IDS)-1u;
    BYTE saved[FLP_IDS][5],old_globals[FLP_GLOBALS];int active[FLP_IDS];
    flp_globals(old_globals,0);
    if(flp_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_FLYER_PART_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[4],r[3];flp_build(n,a,r,0);status=flp_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=flp_installed_probes(index,64);
    if(flp_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=flp_real_pass(index);
    flp_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; EAX/EDX and the original's ECX, ordered calls with entry registers and own stores, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
