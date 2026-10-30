/* weapon_syncs group: the gauge syncs of the weapon vtables' slot +50h
 * (equip_sync 40f144, ecm_sync 40f184, turbo_sync 40f1f0, beam_sync 40f288,
 * gun_sync 40f330, laew_sync 40f3b8) and the LAEW emission laew_fire 40ea04
 * (docs/reverse-engineering/weapon_classes.md, "the weapon gauge syncs, the
 * LAEW emission and the empty bodies").
 *
 * Direct cases run the original body, the adapter (pair) and the core (pair
 * and ECX) over a 2 KiB arena: the weapon W, its gauge widget (object, table
 * and the two records the widget hands out), the type record T. The widget
 * slots +18h, +14h and +10h are logging stubs in the arena's table; the
 * direct callees (cockpit message 420a68, sfx_play 462830, the gauge record
 * getters 440e9c and 4414cc, movement_step 467924, weapon_prepare 40e788 and
 * the emission 40acac) are patched to logging stubs (arguments, entry
 * EAX/EDX/ECX with the local record's address normalised, the bytes the
 * widget slots receive; random registers back, timer-like words at the
 * boundary values). Compared: the pair, ECX (core; not with the hook
 * installed), the ordered calls, the arena and the callee-counter deltas of
 * the first 4,096 cases. Then 256 installed 31/a7 cases (fast and
 * preserving dispatch, nonempty x87; laew_fire over distinct residue bytes
 * through the entry shim, the preparation stub logging the two bytes it
 * finds below its entry ESP), a real-callee pass (movement_step nested in
 * rotating modes for gun_sync and laew_sync; weapon_prepare real with its
 * own hook in the four original/replaced combinations for laew_fire).
 * The six empty bodies (weapon_none_*) reuse slot_leaves' leaf cases
 * (slv_leaf_cases: direct cases with random registers, installed 31/a7
 * probes, compare probes). Identifiers: wsy_/WSY_. */
typedef struct wsy_record { uint32_t id,args[5],regs[3],eax,edx,extra[6]; } wsy_record;
enum { WSY_SIZE=0x800, WSY_W=0x100, WSY_T=0x380, WSY_WIDGET=0x400, WSY_VT=0x440, WSY_REC8=0x480, WSY_REC23=0x4a0,
    WSY_IDS=10, WSY_MAX=8 };
#define WSY_BASE ((uint32_t)(uintptr_t)wsy_arena)
static BYTE wsy_arena[WSY_SIZE],wsy_before[WSY_SIZE],wsy_after[WSY_SIZE];
static wsy_record wsy_log[WSY_MAX],wsy_expected[WSY_MAX];
static unsigned wsy_calls;
static uint32_t wsy_seed,wsy_stream;
__attribute__((used)) static uint32_t wsy_ecx;
__attribute__((used)) static BYTE wsy_residue[2];
static int wsy_log_residue;
/* 0..2 the widget's slots +18h, +14h, +10h; 3 cockpit message 420a68;
 * 4 sfx_play 462830; 5 record getter 440e9c; 6 record getter 4414cc;
 * 7 movement_step 467924; 8 weapon_prepare 40e788; 9 emission 40acac. */
static const uint32_t wsy_direct[WSY_IDS]={0,0,0,0x420a68,0x462830,0x440e9c,0x4414cc,0x467924,0x40e788,0x40acac};
static const unsigned wsy_counts[WSY_IDS]={1,2,2,1,1,1,1,1,3,5};
static uint32_t wsy_next(void){
    uint32_t z=wsy_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
__attribute__((used,noinline)) static uint64_t __cdecl wsy_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(wsy_calls==WSY_MAX){fail("weapon_syncs synthetic call overflow");ExitProcess(1);}
    wsy_record *v=wsy_log+wsy_calls++;memset(v,0,sizeof(*v));v->id=id;
    for(unsigned k=0;k<wsy_counts[id];++k)v->args[k]=a[k];
    for(unsigned j=0;j<3;++j)v->regs[j]=id==8?0:(id==1||id==2)&&regs[j]==a[1]?0xa5a5a5a5u:regs[j];
    if(id==1||id==2)v->args[1]=0;                      /* the local record's address: its bytes are logged */
    if(id==1)memcpy(v->extra,(const void *)(uintptr_t)a[1],8);
    if(id==2)memcpy(v->extra,(const void *)(uintptr_t)a[1],23);   /* the 24th byte is uninitialised */
    if(id==8&&wsy_log_residue)v->extra[0]=wsy_residue[0]|(uint32_t)wsy_residue[1]<<8;
    uint32_t eax=wsy_next(),edx=wsy_next(),pick=wsy_next();wsy_ecx=wsy_next();
    if(id==0)eax=WSY_BASE+WSY_REC8;
    if(id==5||id==6)eax=WSY_BASE+WSY_REC23;
    if(id==7){ static const uint16_t words[]={0,1,2,0xfa,0x1ff,0x7fff,0x8000,0xffff,0xff00};
        if(pick%4)eax=(eax&0xffff0000u)|words[(pick>>3)%9]; }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define WSY_STUB(n) __attribute__((naked)) static void wsy_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _wsy_stub_impl\n\taddl $24,%esp\n\tmovl _wsy_ecx,%ecx\n\tret\n\t");}
WSY_STUB(0) WSY_STUB(1) WSY_STUB(2) WSY_STUB(3) WSY_STUB(4) WSY_STUB(5) WSY_STUB(6) WSY_STUB(7) WSY_STUB(9)
#undef WSY_STUB
/* The preparation stub also keeps the two residue bytes below its entry ESP
 * (40e788's entry ESP - 33 and - 65; the entry shim hands them over). */
__attribute__((naked)) static void wsy_stub_8(void){__asm__ volatile(
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tmovzbl -21(%esp),%ecx\n\tmovb %cl,_wsy_residue\n\t"
    "movzbl -53(%esp),%ecx\n\tmovb %cl,_wsy_residue+1\n\t"
    "leal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
    "pushl $8\n\tcalll _wsy_stub_impl\n\taddl $24,%esp\n\tmovl _wsy_ecx,%ecx\n\tret\n\t");}
static void *wsy_stubs[WSY_IDS]={wsy_stub_0,wsy_stub_1,wsy_stub_2,wsy_stub_3,wsy_stub_4,wsy_stub_5,wsy_stub_6,wsy_stub_7,wsy_stub_8,wsy_stub_9};
static unsigned wsy_which(unsigned index){
    if(index==ES2_HOOK_INDEX_ecm_sync)return 1u<<3;
    if(index==ES2_HOOK_INDEX_turbo_sync)return 1u<<4;
    if(index==ES2_HOOK_INDEX_beam_sync)return 1u<<5;
    if(index==ES2_HOOK_INDEX_gun_sync||index==ES2_HOOK_INDEX_laew_sync)return 1u<<6|1u<<7;
    if(index==ES2_HOOK_INDEX_laew_fire)return 1u<<8|1u<<9;
    return 0;
}
static int wsy_patch(BYTE saved[WSY_IDS][5],int active[WSY_IDS],unsigned which,int on){
    for(unsigned i=3;i<WSY_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(wsy_direct[i]-0x400000u,wsy_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(wsy_direct[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static void wsy_reset(void){wsy_calls=0;wsy_stream=wsy_seed;}
static uint16_t wsy_word(const uint16_t *v,unsigned n){return random32()%4?v[random32()%n]:(uint16_t)random32();}
static void wsy_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    const uint32_t b=WSY_BASE,W=b+WSY_W;(void)n;
    wsy_seed=random32();random_bytes(wsy_arena,WSY_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    static const uint16_t words[]={0,1,2,5,0xfa,0x100,0x258,0x4af,0x4b0,0x4b1,0x7fff,0x8000,0xffff,0xfffb,0xff00,0x00ff};
    BYTE *w=wsy_arena+WSY_W;
    put32(wsy_arena+WSY_WIDGET,b+WSY_VT);
    put32(wsy_arena+WSY_VT+0x10,(uint32_t)(uintptr_t)wsy_stub_2);
    put32(wsy_arena+WSY_VT+0x14,(uint32_t)(uintptr_t)wsy_stub_1);
    put32(wsy_arena+WSY_VT+0x18,(uint32_t)(uintptr_t)wsy_stub_0);
    /* EQUIP-like classes keep the widget at the dword W+79h (word W+77h is their code); BEAM, ELF and GUN keep it at the dword W+77h. */
    put32(w+0x20,b+WSY_T);
    w[0x34]=(BYTE)(random32()%2?0:random32()%3?1:random32());
    w[0x43]=(BYTE)random32();w[0x46]=(BYTE)(random32()%2?0:random32());w[0x49]=(BYTE)(random32()%3?0:random32());
    put16(w+0x7b,wsy_word(words,16));
    const uint16_t shown=wsy_word(words,16);
    put16(w+0x7d,shown);put16(w+0x7f,wsy_word(words,16));
    BYTE *rec8=wsy_arena+WSY_REC8;
    a[0]=W;
    if(index==ES2_HOOK_INDEX_ecm_sync){
        rec8[0]=(BYTE)(random32()%3==0?0:random32()%3==0?(BYTE)shown:random32());
        if(random32()%4==0)put16(w+0x7d,(uint16_t)rec8[0]|(random32()%2?0:0xff00u));
    } else if(index==ES2_HOOK_INDEX_turbo_sync){
        static const uint32_t charges[]={0,1,0x23,0x24,0x257,0x258,0x259,0x25a,0x7c0,0xffffffffu,0x80000000u,0x7fffffffu,0x80000022u};
        put32(w+0x7d,random32()%4?charges[random32()%13]:random32());
        put32(w+0x81,random32()%2?0:random32()%2?1u:random32());
        rec8[0]=(BYTE)(random32()%3?random32()%2?1u:random32():0);
    } else if(index==ES2_HOOK_INDEX_gun_sync||index==ES2_HOOK_INDEX_laew_sync){
        static const int32_t deltas[]={-70000,-300,-2,-1,0,1,2,300,70000};
        const uint32_t target=(uint32_t)(int32_t)(int16_t)get16(w+0x7b)<<8;
        switch(random32()%4){
        case 0:put32(w+0x7d,target+(uint32_t)deltas[random32()%9]);break;
        case 1:put32(w+0x7d,target);break;
        case 2:put32(w+0x7d,random32());break;
        default:{static const uint32_t extreme[]={0,0x7fffffffu,0x80000000u,0xffffffffu,1};put32(w+0x7d,extreme[random32()%5]);}
        }
    } else if(index==ES2_HOOK_INDEX_beam_sync){
        if(random32()%3==0)put32(wsy_arena+WSY_REC23+4,random32()%2?0x7fffffffu:0x80000000u);
    }
    if(index==ES2_HOOK_INDEX_beam_sync||index==ES2_HOOK_INDEX_gun_sync||index==ES2_HOOK_INDEX_laew_sync){
        a[1]=random32();a[2]=random32();a[3]=random32();
        if(random32()%2){a[1]&=0xffffu;a[2]&=0xffu;a[3]&=0xffu;}
    }
    /* Set last: the dword W+79h overlaps the word W+7Bh, the dword W+77h the bytes W+78h..7Ah. */
    if(index==ES2_HOOK_INDEX_beam_sync||index==ES2_HOOK_INDEX_gun_sync||index==ES2_HOOK_INDEX_laew_sync)put32(w+0x77,b+WSY_WIDGET);
    else if(index!=ES2_HOOK_INDEX_laew_fire)put32(w+0x79,b+WSY_WIDGET);
    if(index==ES2_HOOK_INDEX_laew_fire){
        a[1]=random32();a[2]=random32()%2?(random32()&0xffff0000u):random32();
        put32(w+0x81,random32()%3?random32():0);
        put16(wsy_arena+WSY_T+2,(uint16_t)random32());
    }
}
static uint64_t wsy_core(unsigned index,const uint32_t *a,const uint32_t *r,uint32_t *ecx){
    const es2_guest_memory *m=&math_native_memory;BYTE scratch[128]={0};
    es2_weapon_graph g={(uint32_t)(uintptr_t)mapped,(uint32_t)(uintptr_t)scratch,0,0,0,weapons_guest_call,NULL,NULL,NULL};
    switch(index){
    case ES2_HOOK_INDEX_equip_sync:return es2_equip_sync_registers(m,&g,a[0],r,ecx);
    case ES2_HOOK_INDEX_ecm_sync:return es2_ecm_sync_registers(m,&g,a[0],r,ecx);
    case ES2_HOOK_INDEX_turbo_sync:return es2_turbo_sync_registers(m,&g,a[0],r,ecx);
    case ES2_HOOK_INDEX_beam_sync:return es2_beam_sync_registers(m,&g,a[0],a[1],a[2],a[3],r,ecx);
    case ES2_HOOK_INDEX_gun_sync:return es2_gun_sync_registers(m,&g,a[0],a[1],a[2],a[3],r,ecx);
    case ES2_HOOK_INDEX_laew_sync:return es2_laew_sync_registers(m,&g,a[0],a[1],a[2],a[3],r,ecx);
    case ES2_HOOK_INDEX_laew_fire:return es2_laew_fire_registers(m,&g,a[0],a[1],a[2],r,ecx);
    default:fail("weapon_syncs: no core for this hook");ExitProcess(1);
    }
}
static unsigned wsy_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<wsy_calls;++j)if(c->event==ES2_COLLIDE_CALL&&wsy_log[j].id>=3&&c->key==wsy_direct[wsy_log[j].id])++hits;
    return hits;
}
static int wsy_report(const char *what,unsigned n,const uint32_t *want,const uint32_t *got,unsigned calls,const wsy_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x calls %u/%u\n",current_function,n,what,
        want[0],got[0],want[1],got[1],want[2],got[2],calls,wsy_calls);
    for(unsigned j=0;j<calls&&j<wsy_calls&&expected;++j)if(memcmp(expected+j,wsy_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(wsy_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<WSY_SIZE;++j)if(wsy_after[j]!=wsy_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,wsy_after[j],wsy_arena[j]);break;}
    return 1;
}
static int wsy_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    uint32_t want[3],got[3];
    memcpy(wsy_before,wsy_arena,WSY_SIZE);
    wsy_reset();wm_invoke(mapped+d->rva,a,r,want);unsigned calls=wsy_calls;
    memcpy(wsy_expected,wsy_log,calls*sizeof(*wsy_expected));memcpy(wsy_after,wsy_arena,WSY_SIZE);
    /* The adapter, with the callee-counter deltas. */
    memcpy(wsy_arena,wsy_before,WSY_SIZE);wsy_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[4]={0};
    for(unsigned i=0;i<count&&i<4;++i)counts[i]=c[i].count;
    const uint64_t pair=x->replacement_context(a,d->user,r);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);got[2]=want[2];
    if(n<4096)for(unsigned i=0;i<count&&i<4;++i)if(c[i].count-counts[i]!=wsy_expected_hits(c+i))return fail("weapon_syncs callee counter delta");
    if(want[0]!=got[0]||want[1]!=got[1]||calls!=wsy_calls||memcmp(wsy_expected,wsy_log,calls*sizeof(*wsy_expected))||
       memcmp(wsy_after,wsy_arena,WSY_SIZE))
        return wsy_report("adapter",n,want,got,calls,wsy_expected);
    /* The core, with the original's ECX (not when the hook is installed:
       the "original" is then the hook, whose ECX is the framework's). */
    memcpy(wsy_arena,wsy_before,WSY_SIZE);wsy_reset();
    uint32_t ecx;const uint64_t core=wsy_core(index,a,r,&ecx);got[0]=(uint32_t)core;got[1]=(uint32_t)(core>>32);got[2]=ecx;
    const int ecx_known=!rng_hooks[index].installed;
    if(core!=pair||(ecx_known&&ecx!=want[2])||calls!=wsy_calls||memcmp(wsy_expected,wsy_log,calls*sizeof(*wsy_expected))||
       memcmp(wsy_after,wsy_arena,WSY_SIZE))
        return wsy_report("core",n,want,got,calls,wsy_expected);
    return 0;
}
/* The installed hook against the original over a patterned stack. */
static void wsy_poison_call(unsigned index,const uint32_t *a,uint32_t pattern,uint32_t pair[2]){
    if(index==ES2_HOOK_INDEX_laew_fire)r2_fire_poison_call(mapped+function_hook_descriptor(index)->rva,a[0],a[1],a[2],pattern,pair);
    else wm_poison_call(mapped+function_hook_descriptor(index)->rva,a,pattern,pair);
}
static int wsy_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    wsy_log_residue=index==ES2_HOOK_INDEX_laew_fire;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],want[3]={0},got[3]={0};
        uint32_t pattern=index==ES2_HOOK_INDEX_laew_fire?(poison?0x5252a731u:n&1?0xa7a7a7a7u:0x31313131u):poison?0xa7a7a7a7u:0x31313131u;
        wsy_build(index,n,a,r);
        BYTE expected_fpu[108],actual_fpu[108];
        memcpy(wsy_before,wsy_arena,WSY_SIZE);
        wsy_reset();wsy_poison_call(index,a,pattern,want);es2_hook_fpu_save(expected_fpu);
        unsigned calls=wsy_calls;memcpy(wsy_expected,wsy_log,calls*sizeof(*wsy_expected));memcpy(wsy_after,wsy_arena,WSY_SIZE);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("weapon_syncs poison install");
        memcpy(wsy_arena,wsy_before,WSY_SIZE);wsy_reset();
        wsy_poison_call(index,a,pattern,got);es2_hook_fpu_save(actual_fpu);
        if(want[0]!=got[0]||want[1]!=got[1]||calls!=wsy_calls||memcmp(wsy_expected,wsy_log,calls*sizeof(*wsy_expected))||
           memcmp(wsy_after,wsy_arena,WSY_SIZE)||
           memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=wsy_report(fast?"installed fast":"installed preserving",n,want,got,calls,wsy_expected);
        if(!es2_hook_uninstall(&h))return fail("weapon_syncs poison uninstall");++total;
    }
    wsy_log_residue=0;
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, pair/trace/arena%s\n",d->name,total,
        index==ES2_HOOK_INDEX_laew_fire?", residue bytes through the entry shim":"");
    return status;
}
/* movement_step real and nested in rotating modes under gun_sync and
 * laew_sync; the other callees stay stubs (the getters read the gauge
 * tables, sfx_play the sound system, the widget is synthetic). */
static int wsy_real_pass(unsigned index){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE saved[WSY_IDS][5];int active[WSY_IDS];
    if(wsy_patch(saved,active,1u<<6,1))return 1;
    int status=0;unsigned total=0;uint64_t entered=0,inner=0;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned nn=0;nn<256&&!status;++nn){
        current_case=nn;current_case_valid=1;
        uint32_t a[6],r[3],want[3]={0},got[3]={0},pattern=nn&1?0xa7a7a7a7u:0x31313131u;wsy_build(index,nn,a,r);
        void *target=mapped+d->rva;
        memcpy(wsy_before,wsy_arena,WSY_SIZE);
        es2_hook hk[2];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        o.mode=(es2_hook_mode)(mode%3);
        const es2_hook_extension *e=function_hook_extension(ES2_HOOK_INDEX_movement_step);
        if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
        if(!nested_install_hook(hk,ES2_HOOK_INDEX_movement_step,&o,e))return fail("weapon_syncs nested install");
        wsy_reset();wm_poison_call(target,a,pattern,want);
        memcpy(wsy_after,wsy_arena,WSY_SIZE);unsigned calls=wsy_calls;memcpy(wsy_expected,wsy_log,calls*sizeof(*wsy_expected));
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+1,d,&o,x))return fail("weapon_syncs hook install");
        memcpy(wsy_arena,wsy_before,WSY_SIZE);wsy_reset();
        wm_poison_call(target,a,pattern,got);
        if(want[0]!=got[0]||want[1]!=got[1]||memcmp(wsy_after,wsy_arena,WSY_SIZE)||calls!=wsy_calls||
           memcmp(wsy_expected,wsy_log,calls*sizeof(*wsy_expected))){
            fprintf(stderr,"real mode=%u\n",mode);status=wsy_report("real",nn,want,got,calls,wsy_expected);
        }
        entered+=hk[1].calls;inner+=hk[0].calls;
        for(unsigned j=0;j<2;++j){
            if(hk[j].mismatches){fprintf(stderr,"FAIL: %s case %u (real mode=%u) compare mismatch in %s\n",d->name,nn,mode,hk[j].descriptor->name);status=1;}
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("weapon_syncs nested uninstall");
        }
        ++total;
    }
    current_case_valid=0;
    if(wsy_patch(saved,active,1u<<6,0))return 1;
    if(!status&&!inner)status=fail("weapon_syncs real callee movement_step never entered");
    if(!status&&!entered)status=fail("weapon_syncs real pass never entered the hook");
    if(!status)printf("PASS: %s %u real-callee cases (%llu hook calls, %llu movement_step), movement_step in rotating modes, 31/a7 stack, pair, arena, calls\n",
        d->name,total,(unsigned long long)entered,(unsigned long long)inner);
    return status;
}
/* laew_fire over the real weapon_prepare (own hook in the four original or
 * replaced combinations) with distinct residue bytes, as weapon_fire's own
 * real pass: the emission and the offset lookup of preparation are stubs. */
static int wsy_fire_real_pass(void){
    BYTE saved[2][5],old_globals[80];r2_global_copy(old_globals,0);
    const unsigned rvas[]={0xacac,0xf540};void *stubs[]={weapon_projectile_stub,fire_prep_offset};
    for(unsigned i=0;i<2;++i)if(actor_patch(rvas[i],stubs[i],saved[i],TRUE))return 1;
    const unsigned index=ES2_HOOK_INDEX_laew_fire;
    int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned modes=0;modes<4&&!status;++modes)for(unsigned n=0;n<128&&!status;++n) {
        uint32_t a[6],r[3];weapon_build(ES2_HOOK_INDEX_weapon_fire,n,a,r);uint32_t b=(uint32_t)(uintptr_t)weapon_arena;
        put32(weapon_arena+0x3c,b+0x200);put32(weapon_arena+0x154,b+0xa00);put32(weapon_arena+0xa04,b+0xa40);
        put32(weapon_arena+0xa40,(uint32_t)(uintptr_t)fire_prep_table);fire_prep_table[8]=(uint32_t)(uintptr_t)fire_prep_lookup;
        put16(weapon_arena+0xac4,0);memset(weapon_arena+0xad0,0,6);
        put32(weapon_arena+0xa16,b+0xb00);memset(weapon_arena+0xb00,0,32);
        put16(weapon_arena+0xb00,16384);put16(weapon_arena+0xb08,16384);put16(weapon_arena+0xb10,16384);weapon_arena[0xb12]=(BYTE)(n%3);
        put32(weapon_arena+0x7b,n%4<2?0:b+0xad0);put32(weapon_arena+0x7f,n%4%2?0:b+0xad0);put16(weapon_arena+0x152,0);memset(weapon_arena+0x12c,0,6);put32(mapped+0x9ef34,0);
        BYTE before_globals[80],after_globals[80],actual_globals[80];memcpy(weapon_before,weapon_arena,sizeof(weapon_arena));r2_global_copy(before_globals,0);
        weapon_reset();uint32_t expected_pair[2],actual_pair[2],pattern=n%5==0?0x5252a731u:n&1?0xa7a7a7a7u:0x31313131u;
        r2_fire_poison_call(mapped+function_hook_descriptor(index)->rva,a[0],a[1],a[2],pattern,expected_pair);
        unsigned calls=weapon_logged;weapon_record expected[48];memcpy(expected,weapon_log,sizeof(expected));
        memcpy(weapon_after,weapon_arena,sizeof(weapon_arena));r2_global_copy(after_globals,0);
        es2_hook_set_fast_path(fast);es2_hook h[2]={{0}};unsigned indices[]={ES2_HOOK_INDEX_weapon_prepare,index};
        for(unsigned j=0;j<2;++j){es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=(es2_hook_mode)((modes>>j)&1);
            if(!nested_install_hook(h+j,indices[j],&o,function_hook_extension(indices[j])))return fail("laew_fire prepare nested install");}
        memcpy(weapon_arena,weapon_before,sizeof(weapon_arena));r2_global_copy(before_globals,1);weapon_reset();
        r2_fire_poison_call(mapped+function_hook_descriptor(index)->rva,a[0],a[1],a[2],pattern,actual_pair);r2_global_copy(actual_globals,0);
        if(memcmp(expected_pair,actual_pair,8)||calls!=weapon_logged||memcmp(expected,weapon_log,sizeof(expected))||memcmp(weapon_arena,weapon_after,sizeof(weapon_arena))||memcmp(after_globals,actual_globals,74)){
            fprintf(stderr,"FAIL: laew_fire real preparation modes=%u case=%u pair %08x:%08x/%08x:%08x calls %u/%u\n",modes,n,expected_pair[1],expected_pair[0],actual_pair[1],actual_pair[0],calls,weapon_logged);
            status=1;
        }
        for(unsigned j=0;j<2;++j){
            if(((modes>>j)&1) && h[j].calls!=1)status=fail("laew_fire/preparation replaced mode not entered");
            if(!es2_hook_uninstall(h+j))return fail("laew_fire prepare nested uninstall");
        }
        ++total;
    }
    es2_hook_set_fast_path(TRUE);for(unsigned i=0;i<2;++i)if(actor_patch(rvas[i],NULL,saved[i],FALSE))return 1;r2_global_copy(old_globals,1);
    if(!status)printf("PASS: laew_fire + real weapon_prepare %u cases, all four independent original/replaced modes, local/resource matrices, uniform/distinct 31/a7 poison, preserving/fast dispatch\n",total);
    return status;
}
static int weapon_syncs_cases(unsigned index,unsigned limit){
    current_function=function_hook_descriptor(index)->name;
    if(index==ES2_HOOK_INDEX_weapon_none_28||index==ES2_HOOK_INDEX_weapon_none_38||index==ES2_HOOK_INDEX_weapon_none_3c||
       index==ES2_HOOK_INDEX_weapon_none_40||index==ES2_HOOK_INDEX_weapon_none_44||index==ES2_HOOK_INDEX_weapon_none_4c)
        return slv_leaf_cases(index,limit);    /* the empty bodies: slot_leaves' direct, installed and compare cases */
    const unsigned which=wsy_which(index);BYTE saved[WSY_IDS][5];int active[WSY_IDS];
    if(wsy_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_WEAPON_SYNCS_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];wsy_build(index,n,a,r);status=wsy_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    if(!limit&&!status)status=wsy_installed_probes(index,64);
    if(wsy_patch(saved,active,which,0))return 1;
    if(!limit&&!status&&(index==ES2_HOOK_INDEX_gun_sync||index==ES2_HOOK_INDEX_laew_sync))status=wsy_real_pass(index);
    if(!limit&&!status&&index==ES2_HOOK_INDEX_laew_fire)status=wsy_fire_real_pass();
    if(!status)printf("PASS: %-20s %u cases; full return pair, ECX (core), ordered calls with entry registers and widget bytes, arena, callee counters\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
