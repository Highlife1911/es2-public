/* weapon_methods group: the weapon vtables' slot bodies +2Ch ... +60h
 * (docs/reverse-engineering/weapon_classes.md, "the weapon-class methods").
 *
 * Direct cases run the original body, the adapter (pair) and the core (pair
 * and ECX) over a 2 KiB arena: the weapon W (flag bytes, the ammunition,
 * charge and limit words, the counters, the gauge pointer), its definition
 * D (thresholds, capacity, ID 16h/19h or other), type record T, the slot-30h
 * record R, an output word P (or a null pointer), and elf_step's frame
 * object (pointer [W+10h] or 0, the counter word [P+24h], the frame word Q
 * = [W+14h], the count word [[P+20h]] >= 1). The original runs with EBX = 0
 * (beam_ready's EDX high word; the adapter passes 0). The direct callees
 * weapon_step 40ef94, timer_word_step 467a48, beam_step 40f00c and the
 * button latch 45b734 are patched to logging stubs (arguments, entry
 * EAX/EDX/ECX; random registers back, timer results with a zero low word in
 * half the calls, one call in three rewrites W's word +7Bh, +7Dh or +7Fh).
 * Compared: the pair, ECX (core; not with the hook installed), the ordered
 * calls, the arena, the two latch bytes and the callee-counter deltas of
 * the first 4,096 cases. Then 256 installed 31/a7 cases (fast and
 * preserving dispatch, nonempty x87); real-callee passes for the steps
 * (weapon_step, timer_word_step and beam_step nested in rotating modes) and
 * laew_trigger (the real latch); the compare probe for the 25 hooks that
 * accept compare. Identifiers: wm_/WMC_ (WM_ is the window-message prefix). */
typedef struct wm_record { uint32_t id,args[3],regs[3],eax,edx; } wm_record;
enum { WMC_SIZE=0x800, WMC_W=0x100, WMC_D=0x300, WMC_T=0x380, WMC_R=0x3c0, WMC_P=0x3e0, WMC_FP=0x400,
    WMC_FQ=0x440, WMC_FC=0x460, WMC_IDS=4, WMC_MAX=8, WMC_GLOBALS=4 };
#define WMC_BASE ((uint32_t)(uintptr_t)wm_arena)
static BYTE wm_arena[WMC_SIZE],wm_before[WMC_SIZE],wm_after[WMC_SIZE];
static wm_record wm_log[WMC_MAX],wm_expected[WMC_MAX];
static unsigned wm_calls;
static uint32_t wm_seed,wm_stream;
__attribute__((used)) static uint32_t wm_ecx;
/* 0 weapon_step 40ef94, 1 timer_word_step 467a48, 2 beam_step 40f00c,
 * 3 the button latch 45b734. */
static const uint32_t wm_direct[WMC_IDS]={0x40ef94,0x467a48,0x40f00c,0x45b734};
static const unsigned wm_counts[WMC_IDS]={3,1,3,2};
static uint32_t wm_next(void){
    uint32_t z=wm_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
__attribute__((used,noinline)) static uint64_t __cdecl wm_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(wm_calls==WMC_MAX){fail("weapon_methods synthetic call overflow");ExitProcess(1);}
    wm_record *v=wm_log+wm_calls++;memset(v,0,sizeof(*v));v->id=id;
    for(unsigned k=0;k<wm_counts[id];++k)v->args[k]=a[k];
    for(unsigned j=0;j<3;++j)v->regs[j]=regs[j];
    uint32_t eax=wm_next(),edx=wm_next(),pick=wm_next();wm_ecx=wm_next();
    if(id==1&&pick%2)eax&=0xffff0000u;                 /* timer expired */
    if((id==0||id==2)&&pick%3==0){                      /* rewrite a word the caller reads again */
        static const unsigned offs[3]={0x7b,0x7d,0x7f};
        put16(wm_arena+WMC_W+offs[(pick>>4)%3],(uint16_t)wm_next());
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define WMC_STUB(n) __attribute__((naked)) static void wm_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _wm_stub_impl\n\taddl $24,%esp\n\tmovl _wm_ecx,%ecx\n\tret\n\t");}
WMC_STUB(0) WMC_STUB(1) WMC_STUB(2) WMC_STUB(3)
#undef WMC_STUB
static void *wm_stubs[WMC_IDS]={wm_stub_0,wm_stub_1,wm_stub_2,wm_stub_3};
static unsigned wm_which(unsigned index){
    if(index==ES2_HOOK_INDEX_beam_step||index==ES2_HOOK_INDEX_turbo_step)return 1u<<0;
    if(index==ES2_HOOK_INDEX_elf_step)return 1u<<1|1u<<2;
    if(index==ES2_HOOK_INDEX_laew_trigger)return 1u<<3;
    return 0;
}
static int wm_patch(BYTE saved[WMC_IDS][5],int active[WMC_IDS],unsigned which,int on){
    for(unsigned i=0;i<WMC_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(wm_direct[i]-0x400000u,wm_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(wm_direct[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static void wm_reset(void){wm_calls=0;wm_stream=wm_seed;}
/* The latch bytes 4D22F6h and 4D22FEh (45b734(1, 1)). */
static void wm_globals(BYTE *out,int restore){
    static const unsigned off[2]={0xd22f6,0xd22fe};
    for(unsigned i=0;i<2;++i){if(restore)mapped[off[i]]=out[i];else out[i]=mapped[off[i]];}
    out[2]=out[3]=0;
}
static uint16_t wm_word(const uint16_t *v,unsigned n){return random32()%4?v[random32()%n]:(uint16_t)random32();}
static uint32_t wm_slot(uint16_t w){return (random32()%3?random32()&0xffff0000u:0)|w;}
static void wm_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    const uint32_t b=WMC_BASE,W=b+WMC_W;(void)n;
    wm_seed=random32();random_bytes(wm_arena,WMC_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    static const uint16_t words[]={0,1,2,5,10,0x14,0x13,0x15,0x334,0x4af,0x4b0,0x4b1,0x7fff,0x8000,0xffff,0xfffb,0xfffa};
    static const uint16_t small[]={0,1,2,3,5,9,10,11,20};
    BYTE *w=wm_arena+WMC_W;
    put32(w+0x1c,b+WMC_D);put32(w+0x20,b+WMC_T);put32(w+0x14,b+WMC_FQ);
    put32(w+0x10,real||random32()%4?b+WMC_FP:0);
    put32(wm_arena+WMC_FP+0x20,b+WMC_FC);
    put16(wm_arena+WMC_FC,(uint16_t)(random32()%2?1u+random32()%6:1u+random32()%0xffffu));
    { const uint16_t count=get16(wm_arena+WMC_FC);const uint32_t pick=random32()%4;
      put16(wm_arena+WMC_FQ,(uint16_t)(pick==0?count-1u:pick==1?random32()%count:random32())); }
    put16(wm_arena+WMC_FP+0x24,(uint16_t)(random32()%4?1u+random32()%0xffffu:0));
    static const BYTE flags[]={0,0,0,1,0x80,0xff};
    w[0x49]=random32()%3?0:flags[random32()%6];
    w[0x43]=(BYTE)(random32()%3?random32()%2:random32());
    w[0x33]=(BYTE)(random32()%2?0:random32());
    w[0x46]=(BYTE)(random32()%2?0:1u+random32()%255u);
    w[0x47]=(BYTE)(random32()%2?0:random32()%3?1:random32());
    w[0x48]=(BYTE)(random32()%2?0:1);
    put16(w+0x31,(uint16_t)(random32()%2?0:random32()));
    const int smallish=random32()%2;
    put16(w+0x7b,smallish?wm_word(small,9):wm_word(words,17));
    put16(w+0x7d,smallish?wm_word(small,9):wm_word(words,17));
    put16(w+0x7f,smallish?wm_word(small,9):wm_word(words,17));
    put16(wm_arena+WMC_D+0x36,smallish?wm_word(small,9):wm_word(words,17));
    put16(wm_arena+WMC_D+0x38,smallish?wm_word(small,9):wm_word(words,17));
    put16(wm_arena+WMC_D+0x3a,wm_word(words,17));
    { static const uint16_t ids[]={0x16,0x19,0x16,0x19,0x06,0x0a};put16(wm_arena+WMC_D+0x56,wm_word(ids,6)); }
    put16(wm_arena+WMC_T,(uint16_t)(random32()%2?0:random32()));
    wm_arena[WMC_R+0xd]=(BYTE)(random32()%2?0:random32()%3?1:random32());
    if(index==ES2_HOOK_INDEX_turbo_step){
        static const uint32_t charges[]={0,1,0x23,0x24,0x7c0,0x7cf,0x7d0,0x7d1,0x80000000u,0x7fffffffu,0x80000022u};
        put32(w+0x7d,random32()%4?charges[random32()%11]:random32());
        put32(w+0x81,random32()%2?0:random32()%2?1u:random32());
    } else if(index==ES2_HOOK_INDEX_laew_trigger){
        static const uint32_t counters[]={0,20000,98999,99000,99001,100000,0x7ffffc17u,0x7fffffffu,0x80000000u};
        put32(w+0x81,random32()%4?counters[random32()%9]:random32());
    }
    const uint32_t widget=random32();put32(w+0x77,widget);
    a[0]=W;
    if(index==ES2_HOOK_INDEX_weapon_trigger||index==ES2_HOOK_INDEX_elf_trigger||index==ES2_HOOK_INDEX_laew_trigger||
       index==ES2_HOOK_INDEX_equip_trigger)a[1]=b+WMC_R;
    else if(index==ES2_HOOK_INDEX_weapon_ammo||index==ES2_HOOK_INDEX_gun_ammo)a[1]=random32()%4?b+WMC_P:0;
    else if(index==ES2_HOOK_INDEX_beam_gauge_match||index==ES2_HOOK_INDEX_gun_gauge_match||index==ES2_HOOK_INDEX_weapon_gauge_match)
        a[1]=random32()%2?widget:random32()%2?widget^1u:random32();
    else if(index==ES2_HOOK_INDEX_beam_adjust)a[1]=wm_slot((uint16_t)(random32()%2?0:random32()));
    else if(index==ES2_HOOK_INDEX_beam_step||index==ES2_HOOK_INDEX_turbo_step||index==ES2_HOOK_INDEX_elf_step){
        a[1]=wm_slot(wm_word(words,17));a[2]=wm_slot((uint16_t)(random32()%2?0:random32()));
    } else a[1]=random32();
    if(real){                                          /* weapon_step's frame walk needs [W+10h] */
        w[0x44]=(BYTE)(random32()%2);
    }
}
/* The original with four argument slots, EAX/EDX/ECX = r and EBX = 0;
 * out receives EAX, EDX and ECX after the call. */
__attribute__((naked,noinline)) static void wm_invoke(void *target,const uint32_t *a,const uint32_t *r,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl 12(%ebp),%esi\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\txorl %ebx,%ebx\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* sg_poison_call with EBX = 0: six slots over 3840 bytes of patterned stack. */
__attribute__((naked)) static void wm_poison_call(void *target,const uint32_t *a,uint32_t pattern,uint32_t pair[2]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 16(%ebp),%eax\n\tmovl $1024,%ecx\n\trep stosl\n\taddl $3840,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 20(%esi)\n\tpushl 16(%esi)\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%eax\n\tmovl %eax,%edx\n\tmovl %eax,%ecx\n\txorl %ebx,%ebx\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%ecx\n\tmovl %eax,(%ecx)\n\tmovl %edx,4(%ecx)\n\tleal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static uint64_t wm_core(unsigned index,const uint32_t *a,const uint32_t *r,uint32_t *ecx){
    const es2_guest_memory *m=&math_native_memory;
    es2_weapon_graph g={(uint32_t)(uintptr_t)mapped,0,0,0,0,weapons_guest_call,NULL,NULL,NULL};
    switch(index){
    case ES2_HOOK_INDEX_equip_ready:case ES2_HOOK_INDEX_equip_trigger:case ES2_HOOK_INDEX_weapon_priority:
    case ES2_HOOK_INDEX_weapon_gauge_match:return es2_weapon_zero_registers(r,ecx);
    case ES2_HOOK_INDEX_weapon_spent:case ES2_HOOK_INDEX_weapon_stock:return es2_weapon_one_registers(r,ecx);
    case ES2_HOOK_INDEX_weapon_trigger:return es2_weapon_trigger_registers(m,a[0],a[1],r,ecx);
    case ES2_HOOK_INDEX_beam_gauge_match:case ES2_HOOK_INDEX_gun_gauge_match:return es2_gauge_match_registers(m,a[0],a[1],r,ecx);
    case ES2_HOOK_INDEX_beam_spent:return es2_beam_spent_registers(m,a[0],r,ecx);
    case ES2_HOOK_INDEX_gun_spent:return es2_gun_spent_registers(m,a[0],r,ecx);
    case ES2_HOOK_INDEX_gun_stock:return es2_gun_stock_registers(m,a[0],r,ecx);
    case ES2_HOOK_INDEX_beam_priority:return es2_beam_priority_registers(m,a[0],r,ecx);
    case ES2_HOOK_INDEX_gun_clear:return es2_gun_clear_registers(m,a[0],r,ecx);
    case ES2_HOOK_INDEX_beam_fill:return es2_beam_fill_registers(m,a[0],r,ecx);
    case ES2_HOOK_INDEX_beam_preset:return es2_beam_preset_registers(m,a[0],r,ecx);
    case ES2_HOOK_INDEX_beam_drain:return es2_beam_drain_registers(m,a[0],r,ecx);
    case ES2_HOOK_INDEX_beam_clear:return es2_beam_clear_registers(m,a[0],r,ecx);
    case ES2_HOOK_INDEX_weapon_ammo:return es2_weapon_ammo_registers(m,a[0],a[1],r,ecx);
    case ES2_HOOK_INDEX_gun_ammo:return es2_gun_ammo_registers(m,a[0],a[1],r,ecx);
    case ES2_HOOK_INDEX_gun_ready:return es2_gun_ready_registers(m,a[0],r,ecx);
    case ES2_HOOK_INDEX_beam_ready:return es2_beam_ready_registers(m,a[0],r,0,ecx);
    case ES2_HOOK_INDEX_elf_ready:return es2_elf_ready_registers(m,a[0],r,ecx);
    case ES2_HOOK_INDEX_elf_trigger:return es2_elf_trigger_registers(m,a[0],a[1],r,ecx);
    case ES2_HOOK_INDEX_beam_adjust:return es2_beam_adjust_registers(m,a[0],a[1],r,ecx);
    case ES2_HOOK_INDEX_laew_trigger:return es2_laew_trigger_registers(m,&g,a[0],a[1],r,ecx);
    case ES2_HOOK_INDEX_beam_step:return es2_beam_step_registers(m,&g,a[0],a[1],a[2],r,ecx);
    case ES2_HOOK_INDEX_turbo_step:return es2_turbo_step_registers(m,&g,a[0],a[1],a[2],r,ecx);
    case ES2_HOOK_INDEX_elf_step:return es2_elf_step_registers(m,&g,a[0],a[1],a[2],r,ecx);
    default:fail("weapon_methods: no core for this hook");ExitProcess(1);
    }
}
static unsigned wm_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<wm_calls;++j)if(c->event==ES2_COLLIDE_CALL&&c->key==wm_direct[wm_log[j].id])++hits;
    return hits;
}
static int wm_report(const char *what,unsigned n,const uint32_t *want,const uint32_t *got,unsigned calls,const wm_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x calls %u/%u\n",current_function,n,what,
        want[0],got[0],want[1],got[1],want[2],got[2],calls,wm_calls);
    for(unsigned j=0;j<calls&&j<wm_calls&&expected;++j)if(memcmp(expected+j,wm_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(wm_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<WMC_SIZE;++j)if(wm_after[j]!=wm_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,wm_after[j],wm_arena[j]);break;}
    return 1;
}
static int wm_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[WMC_GLOBALS],after[WMC_GLOBALS],actual[WMC_GLOBALS];uint32_t want[3],got[3];
    memcpy(wm_before,wm_arena,WMC_SIZE);wm_globals(before,0);
    wm_reset();wm_invoke(mapped+d->rva,a,r,want);unsigned calls=wm_calls;
    memcpy(wm_expected,wm_log,calls*sizeof(*wm_expected));memcpy(wm_after,wm_arena,WMC_SIZE);wm_globals(after,0);
    /* The adapter, with the callee-counter deltas. */
    memcpy(wm_arena,wm_before,WMC_SIZE);wm_globals(before,1);wm_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[4]={0};
    for(unsigned i=0;i<count&&i<4;++i)counts[i]=c[i].count;
    const uint64_t pair=x->replacement_context(a,d->user,r);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);got[2]=want[2];
    wm_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count&&i<4;++i)if(c[i].count-counts[i]!=wm_expected_hits(c+i))return fail("weapon_methods callee counter delta");
    if(want[0]!=got[0]||want[1]!=got[1]||calls!=wm_calls||memcmp(wm_expected,wm_log,calls*sizeof(*wm_expected))||
       memcmp(wm_after,wm_arena,WMC_SIZE)||memcmp(after,actual,WMC_GLOBALS))
        return wm_report("adapter",n,want,got,calls,wm_expected);
    /* The core, with the original's ECX (not when the hook is installed:
       the "original" is then the hook, whose ECX is the framework's). */
    memcpy(wm_arena,wm_before,WMC_SIZE);wm_globals(before,1);wm_reset();
    uint32_t ecx;const uint64_t core=wm_core(index,a,r,&ecx);got[0]=(uint32_t)core;got[1]=(uint32_t)(core>>32);got[2]=ecx;
    wm_globals(actual,0);
    const int ecx_known=!rng_hooks[index].installed;
    if(core!=pair||(ecx_known&&ecx!=want[2])||calls!=wm_calls||memcmp(wm_expected,wm_log,calls*sizeof(*wm_expected))||
       memcmp(wm_after,wm_arena,WMC_SIZE)||memcmp(after,actual,WMC_GLOBALS))
        return wm_report("core",n,want,got,calls,wm_expected);
    return 0;
}
static int wm_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],want[3]={0},got[3]={0},pattern=poison?0xa7a7a7a7u:0x31313131u;wm_build(index,n,a,r,0);
        BYTE before[WMC_GLOBALS],after[WMC_GLOBALS],actual[WMC_GLOBALS],expected_fpu[108],actual_fpu[108];
        memcpy(wm_before,wm_arena,WMC_SIZE);wm_globals(before,0);
        wm_reset();wm_poison_call(mapped+d->rva,a,pattern,want);es2_hook_fpu_save(expected_fpu);
        unsigned calls=wm_calls;memcpy(wm_expected,wm_log,calls*sizeof(*wm_expected));memcpy(wm_after,wm_arena,WMC_SIZE);wm_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("weapon_methods poison install");
        memcpy(wm_arena,wm_before,WMC_SIZE);wm_globals(before,1);wm_reset();
        wm_poison_call(mapped+d->rva,a,pattern,got);es2_hook_fpu_save(actual_fpu);wm_globals(actual,0);
        if(want[0]!=got[0]||want[1]!=got[1]||calls!=wm_calls||memcmp(wm_expected,wm_log,calls*sizeof(*wm_expected))||
           memcmp(wm_after,wm_arena,WMC_SIZE)||memcmp(after,actual,WMC_GLOBALS)||
           memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=wm_report(fast?"installed fast":"installed preserving",n,want,got,calls,wm_expected);
        if(!es2_hook_uninstall(&h))return fail("weapon_methods poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, pair/trace/arena\n",d->name,total);
    return status;
}
/* Real callees: beam_step and turbo_step nest weapon_step (with its
 * timer_word_step), elf_step nests timer_word_step, beam_step and
 * weapon_step, all in rotating original/replaced/compare modes; laew_trigger
 * runs the real latch 45b734 (no hook). The hook under test runs original
 * (three rotations) and replaced (three rotations) over a patterned stack;
 * pair, arena and latch bytes against the all-original run's. */
static unsigned wm_nested(unsigned index,unsigned nested[3]){
    if(index==ES2_HOOK_INDEX_beam_step||index==ES2_HOOK_INDEX_turbo_step){
        nested[0]=ES2_HOOK_INDEX_weapon_step;nested[1]=ES2_HOOK_INDEX_timer_word_step;return 2;}
    if(index==ES2_HOOK_INDEX_elf_step){nested[0]=ES2_HOOK_INDEX_timer_word_step;nested[1]=ES2_HOOK_INDEX_beam_step;
        nested[2]=ES2_HOOK_INDEX_weapon_step;return 3;}
    return 0;
}
static int wm_real_pass(unsigned index){
    unsigned nested[3],count=wm_nested(index,nested);
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    int status=0;unsigned total=0;uint64_t entered[4]={0};
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned nn=0;nn<256&&!status;++nn){
        current_case=nn;current_case_valid=1;
        uint32_t a[6],r[3],want[3]={0},got[3]={0},pattern=nn&1?0xa7a7a7a7u:0x31313131u;wm_build(index,nn,a,r,1);
        BYTE before[WMC_GLOBALS],after[WMC_GLOBALS],actual[WMC_GLOBALS];void *target=mapped+d->rva;
        memcpy(wm_before,wm_arena,WMC_SIZE);wm_globals(before,0);
        es2_hook hk[4];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!nested_install_hook(hk+j,nested[j],&o,e))return fail("weapon_methods nested install");}
        wm_poison_call(target,a,pattern,want);
        memcpy(wm_after,wm_arena,WMC_SIZE);wm_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+3,d,&o,x))return fail("weapon_methods hook install");
        memcpy(wm_arena,wm_before,WMC_SIZE);wm_globals(before,1);
        wm_poison_call(target,a,pattern,got);wm_globals(actual,0);
        if(want[0]!=got[0]||want[1]!=got[1]||memcmp(wm_after,wm_arena,WMC_SIZE)||memcmp(after,actual,WMC_GLOBALS)){
            fprintf(stderr,"real mode=%u\n",mode);wm_calls=0;status=wm_report("real",nn,want,got,0,NULL);
        }
        for(unsigned j=0;j<4;++j){if(j<count||j==3)entered[j]+=hk[j].calls;
            if(hk[j].mismatches){fprintf(stderr,"FAIL: %s case %u (real mode=%u) compare mismatch in %s\n",d->name,nn,mode,hk[j].descriptor->name);status=1;}
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("weapon_methods nested uninstall");}
        wm_globals(before,1);++total;
    }
    current_case_valid=0;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&!entered[3]){fprintf(stderr,"FAIL: %s never entered\n",d->name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases (%llu hook calls), %u nested hook(s) in rotating modes, 31/a7 stack, pair, arena, latch bytes\n",
        d->name,total,(unsigned long long)entered[3],count);
    return status;
}
static void wm_compare_state(BYTE *s,int restore){if(restore)memcpy(wm_arena,s,WMC_SIZE);else memcpy(s,wm_arena,WMC_SIZE);}
/* The writers' compare probes need p (weapon_ammo, gun_ammo) non-null (a null
 * span pointer makes the framework skip the comparison); beam_ready compares
 * EAX and DX only (the EDX high word is EBX's on one path). */
static void wm_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    wm_build(index,n,a,r,0);
    if((index==ES2_HOOK_INDEX_weapon_ammo||index==ES2_HOOK_INDEX_gun_ammo)&&!a[1])a[1]=WMC_BASE+WMC_P;
}
static int wm_compare_ready_same(unsigned index,uint64_t want,uint64_t got){
    (void)index;return (uint32_t)want==(uint32_t)got&&(uint16_t)(want>>32)==(uint16_t)(got>>32);
}
static int wm_compare_run(unsigned index,unsigned limit){
    if(index==ES2_HOOK_INDEX_beam_ready)compare_probe_same=wm_compare_ready_same;
    const int status=compare_probe_run(index,limit,wm_compare_build,wm_compare_state,WMC_SIZE);
    compare_probe_same=NULL;return status;
}
static int weapon_methods_cases(unsigned index,unsigned limit){
    current_function=function_hook_descriptor(index)->name;
    BYTE old_globals[WMC_GLOBALS];wm_globals(old_globals,0);
    if(limit&&compare_probe_installed(index))return wm_compare_run(index,limit);
    const unsigned which=wm_which(index);BYTE saved[WMC_IDS][5];int active[WMC_IDS];
    if(wm_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_WEAPON_METHODS_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];wm_build(index,n,a,r,0);status=wm_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    if(!limit&&!status)status=wm_installed_probes(index,64);
    if(wm_patch(saved,active,which,0))return 1;
    wm_globals(old_globals,1);
    if(!limit&&!status&&(wm_nested(index,(unsigned[3]){0})||index==ES2_HOOK_INDEX_laew_trigger))status=wm_real_pass(index);
    wm_globals(old_globals,1);
    if(!limit&&!status&&compare_probe_capable(index))status=wm_compare_run(index,0);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ECX (core), ordered calls with entry registers, arena, latch bytes\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
