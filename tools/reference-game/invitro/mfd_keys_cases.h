/* mfd_keys group: the MFD's key handlers (docs/reverse-engineering/cockpit.md,
 * "MFD keys"). A 4 KiB arena holds the MFDGauge G (mode +C1h, menu +D1h,
 * selection +B1h, range +C5h, order slot +FBh, the flag bytes +E5h.., the panel
 * +EBh) and two menus (flag bytes +2Ch..+31h, entry dword +32h, old word +37h);
 * the enable table 49cbd8 (13 bytes per MFD mode, modes 0..7) is set per case,
 * compared and restored.
 *
 * Direct cases run the original body and the adapter against logging stubs
 * for every game callee of the hook under test, patched in the private
 * image (panel_visible 431410, button_press 438d9c, mfd_range 446ff0, the
 * entry steps 44703c/447070, mfd_key_select 447158, menu_select 43f9e0,
 * menu_code 43f9c0, mfd_order 447248, mfd_entry 4470a4). A record keeps the
 * callee, the bridge's slot count, the arguments and the entry EAX/EDX/ECX
 * (stack values masked). The stubs return values from a per-case stream
 * (panel_visible AL 0 in one call of four, menu_code 4 or 7 in two of
 * three) and change in about one call of four what the originals reread or
 * keep (the mode [G+C1h], the slot [G+FBh], the menu pointer [G+D1h], a
 * flag byte, the selection). The full EDX:EAX pair, ECX (menu_select; not
 * with the hook installed), the call log, the arena and the table are
 * compared, the callee counters for the first 4,096 cases.
 *
 * Then installed probes through the entry shims (the original and the
 * replaced hook over a 31/a7-filled stack, every stub also recording
 * EBX/ESI/EDI, EBP/ESP relative to the caller's slots and a hash of the 256
 * bytes below its entry, the 1,024 bytes below the slots compared after
 * return, fast and preserving dispatch) and a real-callee chain: each hook
 * but menu_select over the real 446ff0/447014, 44703c, 447070, 447158,
 * 4470a4, 43f9e0 and 43f9c0 (panel_visible, button_press and mfd_order
 * stubbed), the nine chain hooks (this group's six and mfd_orders'
 * mfd_entry, menu_code, mfd_range) uninstalled or replaced at random per
 * case, against the all-uninstalled run on the same filled stack; and the
 * compare probes of compare_probes.h for menu_select. mfd_orders_cases.h's
 * mf_invoke, mf_track_run, mf_stack, mf_origin and mf_below are reused (the
 * header precedes this one). Identifiers: mkc_/MKC_. */
enum { MKC_SIZE=0x1000, MKC_G=0x100, MKC_MENU=0x400, MKC_MENU2=0x480, MKC_IDS=10, MKC_MAX=64,
    MKC_TABLE=0x9cbd8, MKC_GLOBALS=104, MKC_COMPARE=0x500 };
#define MKC_ADDR(o) ((uint32_t)(uintptr_t)mkc_arena+(uint32_t)(o))
static BYTE mkc_arena[MKC_SIZE],mkc_before[MKC_SIZE],mkc_after[MKC_SIZE];
typedef struct mkc_record { uint32_t id,slots,a[3],regs[3],cs[3],frame[2],window; } mkc_record;
static mkc_record mkc_log[MKC_MAX];
static unsigned mkc_calls,mkc_track,mkc_hits[MKC_IDS];
static uint32_t mkc_seed,mkc_stream;
__attribute__((used)) static uint32_t mkc_cap[8],mkc_ecx;
__attribute__((used)) static BYTE mkc_window[256];
static const uint32_t mkc_addr[MKC_IDS]={0x431410,0x438d9c,0x446ff0,0x44703c,0x447070,0x447158,0x43f9e0,0x43f9c0,0x447248,0x4470a4};
static const unsigned mkc_arity[MKC_IDS]={1,3,1,1,1,3,2,1,1,2};
static uint32_t mkc_next(void){
    uint32_t z=mkc_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static void mkc_reset(void){mkc_calls=0;mkc_stream=mkc_seed;}
static void mkc_globals(BYTE *out,int restore){if(restore)memcpy(mapped+MKC_TABLE,out,MKC_GLOBALS);else memcpy(out,mapped+MKC_TABLE,MKC_GLOBALS);}
static uint32_t mkc_hash(const BYTE *p,unsigned n){uint32_t h=2166136261u;for(unsigned i=0;i<n;++i)h=(h^p[i])*16777619u;return h;}
static uint32_t mkc_mask(uint32_t v){return mf_stack(v)?(mkc_track?v-mf_origin:0u):v;}
/* One callee side effect (about one call in four), drawn from the stream. */
static void mkc_effect(void){
    const uint32_t pick=mkc_next();if(pick%4)return;
    BYTE *G=mkc_arena+MKC_G;
    switch(pick/4%5){
    case 0:put32(G+0xc1,mkc_next()%3?1u:mkc_next()%8);break;
    case 1:put32(G+0xfb,mkc_next()%4);break;
    case 2:put32(G+0xd1,MKC_ADDR(mkc_next()%2?MKC_MENU:MKC_MENU2));break;
    case 3:{const unsigned i=mkc_next()%6;BYTE *M=mkc_arena+(mkc_next()%2?MKC_MENU:MKC_MENU2);
        /* never all six bits: an entry step would loop for ever */
        M[0x2c+i]^=1;if((M[0x2c]&M[0x2d]&M[0x2e]&M[0x2f]&M[0x30]&M[0x31]&1u))M[0x2c+i]&=0xfeu;break;}
    default:put32(G+0xb1,mkc_next()%6);break;
    }
}
__attribute__((used,noinline)) static uint64_t __cdecl mkc_log_impl(unsigned id){
    if(mkc_calls==MKC_MAX){fail("mfd_keys synthetic call overflow");ExitProcess(1);}
    uint32_t *a=(uint32_t *)(uintptr_t)(mkc_cap[7]+4u);
    mkc_record *v=mkc_log+mkc_calls++;memset(v,0,sizeof(*v));v->id=id;v->slots=weapons_bridge_slots(a,mkc_arity[id]);
    for(unsigned j=0;j<mkc_arity[id];++j)v->a[j]=mkc_mask(a[j]);
    for(unsigned j=0;j<3;++j)v->regs[j]=mkc_mask(mkc_cap[j]);
    if(mkc_track){for(unsigned j=0;j<3;++j)v->cs[j]=mkc_mask(mkc_cap[3+j]);
        v->frame[0]=mkc_cap[6]-mf_origin;v->frame[1]=mkc_cap[7]-mf_origin;v->window=mkc_hash(mkc_window,256);}
    mkc_hits[id]++;
    uint32_t eax=mkc_next(),edx=mkc_next();mkc_ecx=mkc_next();
    if(id==0)eax=mkc_next()%4?(eax&0xffffff00u)|(mkc_next()%2?1u:0x80u):eax&0xffffff00u;
    if(id==7){const uint32_t k=mkc_next()%3;eax=k==0?4u:k==1?7u:mkc_next()%3?mkc_next()%8:eax;}
    mkc_effect();
    return (uint64_t)edx<<32|eax;
}
#define MKC_STUB(n) __attribute__((naked)) static void mkc_stub_##n(void){__asm__ volatile( \
    "movl %eax,_mkc_cap\n\tmovl %edx,_mkc_cap+4\n\tmovl %ecx,_mkc_cap+8\n\tmovl %ebx,_mkc_cap+12\n\t" \
    "movl %esi,_mkc_cap+16\n\tmovl %edi,_mkc_cap+20\n\tmovl %ebp,_mkc_cap+24\n\tmovl %esp,_mkc_cap+28\n\t" \
    "leal -256(%esp),%esi\n\tmovl $_mkc_window,%edi\n\tmovl $64,%ecx\n\tcld\n\trep movsl\n\t" \
    "movl _mkc_cap+16,%esi\n\tmovl _mkc_cap+20,%edi\n\t" \
    "pushl $" #n "\n\tcalll _mkc_log_impl\n\taddl $4,%esp\n\tmovl _mkc_ecx,%ecx\n\tret\n\t");}
MKC_STUB(0) MKC_STUB(1) MKC_STUB(2) MKC_STUB(3) MKC_STUB(4) MKC_STUB(5) MKC_STUB(6) MKC_STUB(7) MKC_STUB(8) MKC_STUB(9)
#undef MKC_STUB
static void *mkc_stubs[MKC_IDS]={mkc_stub_0,mkc_stub_1,mkc_stub_2,mkc_stub_3,mkc_stub_4,mkc_stub_5,mkc_stub_6,mkc_stub_7,mkc_stub_8,mkc_stub_9};
/* The patched callees of each hook's direct cases (bit = stub id). */
static unsigned mkc_patch_set(unsigned index){
    if(index==ES2_HOOK_INDEX_mfd_key)return 0x3fu;
    if(index==ES2_HOOK_INDEX_mfd_alt_key)return 1u<<5|1u<<6|1u<<7|1u<<8;
    if(index==ES2_HOOK_INDEX_menu_select)return 0;
    return 1u<<9;
}
static int mkc_patch(BYTE saved[MKC_IDS][5],int active[MKC_IDS],unsigned which,int on){
    for(unsigned i=0;i<MKC_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(mkc_addr[i]-0x400000u,mkc_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(mkc_addr[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static uint32_t mkc_edge(void){
    static const uint32_t e[]={0,1,0xffffffffu,0x7fffffffu,0x80000000u,0x7ffffffeu,0x80000001u,0xffffu,0x10000u};
    return mkc_next()%3?e[mkc_next()%9]:mkc_next();
}
/* A key word for 4469e8 (the low word decides) or an order code for 446c38. */
static uint32_t mkc_key(int alt){
    static const uint16_t named[]={0x20,0x2d,0x33,0x34,0x213,0x214};
    static const uint16_t close_by[]={0,0x11,0x12,0x2e,0x2f,0x32,0x35,0x211,0x212,0x215,0x22e,0x22f,0xffff};
    const uint32_t high=mkc_next()%2?0u:mkc_next()<<16;
    uint32_t low;
    switch(mkc_next()%(alt?5:8)){
    case 0:low=alt?0x212u+mkc_next()%29:named[mkc_next()%6];break;
    case 1:low=close_by[mkc_next()%13];break;
    case 2:low=mkc_next()&0xffffu;break;
    case 3:low=0x212u+mkc_next()%29;break;
    case 4:low=alt?0x212u+mkc_next()%29:0x12u+mkc_next()%29;break;
    default:low=alt?0x12u+mkc_next()%29:mkc_next()%2?named[mkc_next()%6]:0x12u+mkc_next()%29;break;
    }
    return high|low;
}
/* Builds one state; a[] the hook's slots, r the entry EAX/EDX/ECX. */
static void mkc_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    mkc_seed=random32();mkc_stream=mkc_seed^n;random_bytes(mkc_arena,MKC_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=mkc_next();
    BYTE *G=mkc_arena+MKC_G;
    put32(G+0xc1,mkc_next()%2?1u:mkc_next()%8);
    put32(G+0xd1,MKC_ADDR(mkc_next()%4?MKC_MENU:MKC_MENU2));
    put32(G+0xfb,mkc_next()%4);
    put32(G+0xc5,mkc_next()%5-1u);
    /* The selection: 0..5; for the steps also the wrapping starts. */
    put32(G+0xb1,mkc_next()%6);
    if(index==ES2_HOOK_INDEX_mfd_entry_next&&mkc_next()%6==0)put32(G+0xb1,0xffffffffu);
    if(index==ES2_HOOK_INDEX_mfd_entry_prev&&mkc_next()%6==0)put32(G+0xb1,6u);
    /* The menus: bit 0 set in about one byte of two (at least one clear),
     * the entry dword and old word. */
    for(unsigned k=0;k<2;++k){BYTE *M=mkc_arena+(k?MKC_MENU2:MKC_MENU);
        for(unsigned i=0;i<6;++i)M[0x2c+i]=(BYTE)((M[0x2c+i]&0xfeu)|(mkc_next()%(mkc_next()%4?2:6)?1u:0u));
        M[0x2c+mkc_next()%6]&=0xfeu;
        put32(M+0x32,mkc_next()%4?mkc_next()%8:mkc_next());}
    /* The enable table: modes 0..7, about one byte of two set. */
    for(unsigned i=0;i<MKC_GLOBALS;++i)mapped[MKC_TABLE+i]=(BYTE)(mkc_next()%2?0:mkc_next()%4?1:mkc_next());
    const uint32_t Ga=MKC_ADDR(MKC_G);
    if(index==ES2_HOOK_INDEX_mfd_key){a[0]=Ga;a[1]=mkc_next()%16?mkc_key(0):mkc_edge();}
    else if(index==ES2_HOOK_INDEX_mfd_alt_key){a[0]=Ga;a[1]=mkc_next()%16?mkc_key(1):mkc_edge();}
    else if(index==ES2_HOOK_INDEX_mfd_key_select){a[0]=Ga;a[1]=mkc_next()%3?get32(G+0xfb):mkc_next()%3?mkc_next()%4:mkc_edge();
        a[2]=mkc_next()%4?mkc_next()%6:mkc_edge();}
    else if(index==ES2_HOOK_INDEX_menu_select){a[0]=MKC_ADDR(mkc_next()%2?MKC_MENU:MKC_MENU2);a[1]=mkc_next()%3?mkc_next()%6:mkc_edge();}
    else a[0]=Ga;
    mkc_stream=mkc_seed;   /* the stubs' stream restarts here in both runs */
}
static int mkc_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const mkc_record *e,const BYTE *want,const BYTE *got){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,mkc_calls);
    for(unsigned j=0;j<calls&&j<mkc_calls&&e;++j)if(memcmp(e+j,mkc_log+j,sizeof(*e))){
        const uint32_t *x=(const uint32_t*)(e+j),*y=(const uint32_t*)(mkc_log+j);
        for(unsigned k=0;k<sizeof(*e)/4;++k)if(x[k]!=y[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,e[j].id,k,x[k],y[k]);break;}
    for(unsigned j=0;j<MKC_SIZE;++j)if(mkc_after[j]!=mkc_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,mkc_after[j],mkc_arena[j]);break;}
    for(unsigned j=0;want&&j<MKC_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," table byte %u %02x/%02x\n",j,want[j],got[j]);break;}
    return 1;
}
/* One direct case: the original against the adapter (and menu_select's core
 * with its ECX). */
static int mkc_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[MKC_GLOBALS],after[MKC_GLOBALS],actual[MKC_GLOBALS];static mkc_record expected[MKC_MAX];
    memcpy(mkc_before,mkc_arena,MKC_SIZE);mkc_globals(before,0);
    mkc_reset();uint64_t pair[2];uint32_t out[3];
    mf_invoke(mapped+d->rva,a,r,out);pair[0]=(uint64_t)out[1]<<32|out[0];
    const unsigned calls=mkc_calls;memcpy(expected,mkc_log,calls*sizeof(*expected));memcpy(mkc_after,mkc_arena,MKC_SIZE);mkc_globals(after,0);
    memcpy(mkc_arena,mkc_before,MKC_SIZE);mkc_globals(before,1);mkc_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8]={0};
    for(unsigned i=0;i<count&&i<8;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);mkc_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count&&i<8;++i){uint64_t want=0;
        for(unsigned j=0;j<mkc_calls;++j)want+=mkc_addr[mkc_log[j].id]==c[i].key;
        if(c[i].count-counts[i]!=want)return fail("mfd_keys callee counter delta");}
    if(pair[0]!=pair[1]||calls!=mkc_calls||memcmp(expected,mkc_log,calls*sizeof(*expected))||memcmp(mkc_after,mkc_arena,MKC_SIZE)||
       memcmp(after,actual,MKC_GLOBALS))
        return mkc_report("adapter",n,pair,calls,expected,after,actual);
    if(index==ES2_HOOK_INDEX_menu_select){
        memcpy(mkc_arena,mkc_before,MKC_SIZE);
        uint32_t core_ecx;const uint64_t core=es2_menu_select_registers(&math_native_memory,a[0],a[1],r,&core_ecx);
        if(core!=pair[0]||(!rng_hooks[index].installed&&core_ecx!=out[2])||memcmp(mkc_after,mkc_arena,MKC_SIZE)){
            fprintf(stderr,"core ecx %08x/%08x\n",out[2],core_ecx);const uint64_t both[2]={pair[0],core};
            return mkc_report("core",n,both,calls,expected,after,actual);
        }
    }
    mkc_globals(before,1);
    return 0;
}
/* Installed probes through the entry shims: the original and the replaced
 * hook over the same filled stack. */
static int mkc_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    mkc_track=1;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],want[2],got[2],pattern=poison?0xa7a7a7a7u:0x31313131u;mkc_build(index,n*41u+3u,a,r);
        BYTE before[MKC_GLOBALS],after[MKC_GLOBALS],actual[MKC_GLOBALS],expected_fpu[108],actual_fpu[108],below[1024];
        static mkc_record expected[MKC_MAX];
        memcpy(mkc_before,mkc_arena,MKC_SIZE);mkc_globals(before,0);
        mkc_reset();mf_track_run(mapped+d->rva,a,pattern,want);
        es2_hook_fpu_save(expected_fpu);memcpy(below,mf_below,1024);
        const unsigned calls=mkc_calls;memcpy(expected,mkc_log,calls*sizeof(*expected));memcpy(mkc_after,mkc_arena,MKC_SIZE);mkc_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("mfd_keys poison install");
        memcpy(mkc_arena,mkc_before,MKC_SIZE);mkc_globals(before,1);mkc_reset();
        mf_track_run(mapped+d->rva,a,pattern,got);
        es2_hook_fpu_save(actual_fpu);mkc_globals(actual,0);
        uint64_t pair[2]={(uint64_t)want[1]<<32|want[0],(uint64_t)got[1]<<32|got[0]};
        if(pair[0]!=pair[1]||calls!=mkc_calls||memcmp(expected,mkc_log,calls*sizeof(*expected))||memcmp(mkc_after,mkc_arena,MKC_SIZE)||
           memcmp(after,actual,MKC_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=mkc_report(fast?"installed fast":"installed preserving",n,pair,calls,expected,after,actual);
        else if(memcmp(below,mf_below,1024)){
            for(unsigned j=0;j<1024;++j)if(below[j]!=mf_below[j]){fprintf(stderr,"FAIL: %s case %u installed: stack byte origin-%u %02x/%02x\n",
                d->name,n,1024-j,below[j],mf_below[j]);break;}
            status=1;
        }
        if(!es2_hook_uninstall(&h))return fail("mfd_keys poison uninstall");mkc_globals(before,1);++total;
    }
    mkc_track=0;
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed cases through the entry shim, fast/preserving dispatch, 31/a7 stack, per-call EBX/ESI/EDI, EBP/ESP and the residue below each callee, the 1,024 bytes below the slots after return, nonempty x87, full pair/calls/arena/table\n",
        d->name,total);
    return status;
}
/* The real-callee chain from the hook under test: only panel_visible,
 * button_press and mfd_order are stubs; the chain hooks are uninstalled or
 * replaced at random per case. */
static int mkc_chain_pass(unsigned index){
    static const unsigned chain[]={ES2_HOOK_INDEX_mfd_key,ES2_HOOK_INDEX_mfd_alt_key,ES2_HOOK_INDEX_mfd_key_select,ES2_HOOK_INDEX_menu_select,
        ES2_HOOK_INDEX_mfd_entry_next,ES2_HOOK_INDEX_mfd_entry_prev,ES2_HOOK_INDEX_mfd_entry,ES2_HOOK_INDEX_menu_code,ES2_HOOK_INDEX_mfd_range};
    enum { CHAIN=sizeof(chain)/sizeof(chain[0]) };
    BYTE saved[MKC_IDS][5];int active[MKC_IDS];
    if(mkc_patch(saved,active,1u<<0|1u<<1|1u<<8,1))return 1;
    const es2_hook_descriptor *d=function_hook_descriptor(index);
    int status=0;unsigned total=0;uint64_t entered[CHAIN]={0};
    mkc_track=1;
    for(unsigned n=0;n<4096&&!status;++n){
        uint32_t a[6],r[3],want[2],got[2],pattern=n&4?0xa7a7a7a7u:0x31313131u;mkc_build(index,n*67u+5u,a,r);
        BYTE before[MKC_GLOBALS],after[MKC_GLOBALS],actual[MKC_GLOBALS],below[1024];static mkc_record expected[MKC_MAX];
        memcpy(mkc_before,mkc_arena,MKC_SIZE);mkc_globals(before,0);
        mkc_reset();mf_track_run(mapped+d->rva,a,pattern,want);memcpy(below,mf_below,1024);
        const unsigned calls=mkc_calls;memcpy(expected,mkc_log,calls*sizeof(*expected));memcpy(mkc_after,mkc_arena,MKC_SIZE);mkc_globals(after,0);
        es2_hook hk[CHAIN];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        o.mode=ES2_HOOK_REPLACED;const uint32_t mask=random32()|(n%7==0?0x1ffu:0u);
        for(unsigned j=0;j<CHAIN;++j)if(mask>>j&1&&!weapons_install_hook(hk+j,function_hook_descriptor(chain[j]),&o,function_hook_extension(chain[j])))
            return fail("mfd_keys chain install");
        es2_hook_set_fast_path(n%3!=0);
        memcpy(mkc_arena,mkc_before,MKC_SIZE);mkc_globals(before,1);mkc_reset();
        mf_track_run(mapped+d->rva,a,pattern,got);mkc_globals(actual,0);
        es2_hook_set_fast_path(TRUE);
        uint64_t pair[2]={(uint64_t)want[1]<<32|want[0],(uint64_t)got[1]<<32|got[0]};
        if(pair[0]!=pair[1]||calls!=mkc_calls||memcmp(expected,mkc_log,calls*sizeof(*expected))||memcmp(mkc_after,mkc_arena,MKC_SIZE)||
           memcmp(after,actual,MKC_GLOBALS)){fprintf(stderr,"chain mask %03x\n",mask&0x1ffu);
            status=mkc_report("real chain",n,pair,calls,expected,after,actual);}
        else if(memcmp(below,mf_below,1024)){
            for(unsigned j=0;j<1024;++j)if(below[j]!=mf_below[j]){fprintf(stderr,"FAIL: %s case %u chain mask %03x: stack byte origin-%u %02x/%02x\n",
                current_function,n,mask&0x1ffu,1024-j,below[j],mf_below[j]);break;}
            status=1;
        }
        for(unsigned j=0;j<CHAIN;++j){entered[j]+=hk[j].calls;if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("mfd_keys chain uninstall");}
        mkc_globals(before,1);++total;
    }
    mkc_track=0;
    if(mkc_patch(saved,active,0,0))return 1;
    if(!status)printf("PASS: %s %u real-chain cases (the chain hooks uninstalled or replaced at random, fast/preserving dispatch, 31/a7 stack; replaced entries key %llu, alt %llu, select %llu, menu %llu, next %llu, prev %llu, entry %llu, code %llu, range %llu), pair/calls/arena/table and the 1,024 bytes below the slots\n",
        current_function,total,(unsigned long long)entered[0],(unsigned long long)entered[1],(unsigned long long)entered[2],
        (unsigned long long)entered[3],(unsigned long long)entered[4],(unsigned long long)entered[5],(unsigned long long)entered[6],
        (unsigned long long)entered[7],(unsigned long long)entered[8]);
    return status;
}
static void mkc_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){mkc_build(index,n,a,r);}
static void mkc_compare_state(BYTE *s,int restore){if(restore)memcpy(mkc_arena,s,MKC_COMPARE);else memcpy(s,mkc_arena,MKC_COMPARE);}
static int mfd_keys_cases(unsigned index,unsigned limit){
    current_function=function_hook_descriptor(index)->name;
    BYTE old_globals[MKC_GLOBALS];mkc_globals(old_globals,0);
    if(limit&&compare_probe_installed(index)){
        const int status=compare_probe_run(index,limit,mkc_compare_build,mkc_compare_state,MKC_COMPARE);
        mkc_globals(old_globals,1);return status;
    }
    BYTE saved[MKC_IDS][5];int active[MKC_IDS];
    if(mkc_patch(saved,active,mkc_patch_set(index),1))return 1;
    unsigned cases=case_budget(limit,1000448);const char *quick=getenv("ES2_INVITRO_MFD_KEYS_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;memset(mkc_hits,0,sizeof(mkc_hits));if(!limit)coverage_enable(1);mkc_track=0;
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];mkc_build(index,n,a,r);status=mkc_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;mkc_globals(old_globals,1);
    if(!limit&&!status)status=mkc_installed_probes(index,256);
    mkc_globals(old_globals,1);
    if(mkc_patch(saved,active,0,0))return 1;
    if(!limit&&!status&&index!=ES2_HOOK_INDEX_menu_select)status=mkc_chain_pass(index);
    mkc_globals(old_globals,1);
    if(!limit&&!status&&index==ES2_HOOK_INDEX_menu_select)status=compare_probe_run(index,0,mkc_compare_build,mkc_compare_state,MKC_COMPARE);
    mkc_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases (stub calls: panel %u, button_press %u, range %u, next %u, prev %u, key select %u, menu select %u, code %u, order %u, entry %u); full return pair, ordered calls with entry registers, arena, enable table\n",
        function_hook_descriptor(index)->name,cases,mkc_hits[0],mkc_hits[1],mkc_hits[2],mkc_hits[3],mkc_hits[4],mkc_hits[5],mkc_hits[6],
        mkc_hits[7],mkc_hits[8],mkc_hits[9]);
    return status;
}
