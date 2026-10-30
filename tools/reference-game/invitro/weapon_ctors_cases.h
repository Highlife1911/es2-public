/* weapon_ctors group: weapon_ctor 40df30, beam_ctor 40e074, gun_ctor 40e140,
 * equip_ctor 40e234, ecm_ctor 40e274, turbo_ctor 40e2bc, targ_ctor 40e308,
 * shield_ctor 40e344 and energy_ctor 40e380
 * (docs/reverse-engineering/weapon_classes.md, "the weapon constructors").
 *
 * Direct cases run the original body (EAX/EDX/ECX, EBX, ESI and EDI at entry
 * from the case, six argument slots, over a stack filled with a case
 * pattern) and the core against logging stubs for every game callee of the
 * hook under test: the weapon shape 40fab0, shape_word_block 402fc0,
 * object_track 402374, mount_offset 40f540, the mesh offset 40dd4c, the
 * CW3220 memset thunk 49681a (a stub that fills), weapon_ctor 40df30,
 * multiply_q10 47e0b0 and equip_ctor 40e234. Compared: the full return
 * pair, ECX, the ordered calls with argument slots (and the bridge's slot
 * count) and entry EAX/EDX/ECX, the arena and the image globals (the counter
 * pointer 6CBEE4h, the mount offset 4A9AF0h, the flag byte 498F34h). The
 * stubs change what the originals read again (the counter pointer after
 * every call, the block's shape [W+10h] and the track word M+4 after
 * object_track, [W+10h] through shape_word_block, [W+1Ch] after
 * weapon_ctor), and in odd cases the mount record lies inside the weapon or
 * the counter inside the weapon, so the order of reads and writes is
 * observable. In even cases the inputs are the factory's (separate objects)
 * and the adapter is compared as well, with the callee counter deltas.
 *
 * Then installed 31/a7 poison probes and real-callee passes: weapon_ctor
 * over the real shape_word_block (47a288 a bump allocator, memcpy the
 * harness's), object_track (the real 49630c, new a bump allocator, its
 * thread callees 478e48/4792ac/4796d8/478a3c stubs), mount_offset and
 * mount_base; beam_ctor, gun_ctor and equip_ctor over the real weapon_ctor
 * (beam_ctor also the real multiply_q10), the five equipment classes over
 * the real equip_ctor and weapon_ctor (weapon_ctor's callees stubs); the
 * nested hooks in rotating original/replaced modes, the hook under test
 * original or replaced against a run without it. Identifiers use the
 * prefix wct_/WCT_. */
typedef struct wct_record { uint32_t id,slots,args[5],regs[3],eax,edx; } wct_record;
enum {
    WCT_SIZE=0x2000, WCT_MAX=32, WCT_IDS=16,
    WCT_OBJ=0x100, WCT_MOUNT=0x200, WCT_DEF=0x240, WCT_DEF2=0x2a0, WCT_ACTOR=0x300, WCT_INST=0x380, WCT_SHAPE=0x400,
    WCT_SHAPE2=0x440, WCT_WORDS=0x500, WCT_COUNTER=0x600, WCT_COUNTER2=0x610, WCT_BUMP=0x1000
};
#define WCT_B ((uint32_t)(uintptr_t)wct_arena)
#define WCT_A(o) (WCT_B+(uint32_t)(o))
#define WCT_BIT(i) ((uint32_t)1<<(i))
static BYTE wct_arena[WCT_SIZE],wct_before[WCT_SIZE],wct_after[WCT_SIZE];
static wct_record wct_log[WCT_MAX];
static unsigned wct_calls,wct_real,wct_effects,wct_index,wct_paths[WCT_IDS];
static uint32_t wct_seed,wct_stream,wct_bump,wct_obj,wct_mount;
__attribute__((used)) static uint32_t wct_ecx;
/* 0 weapon shape 40fab0, 1 shape_word_block 402fc0, 2 object_track 402374,
 * 3 mount_offset 40f540, 4 mesh offset 40dd4c, 5 memset 49681a, 6
 * weapon_ctor 40df30, 7 multiply_q10 47e0b0, 8 equip_ctor 40e234; real
 * passes only: 9 47a288, 10 memcpy 49680e, 11 new 47749c, 12 thread_ctor
 * 478e48, 13 track_clip 4792ac, 14 track_rate 4796d8, 15 thread_add 478a3c.
 * The slots each original call site pushes. */
static const uint32_t wct_addresses[WCT_IDS]={0x40fab0,0x402fc0,0x402374,0x40f540,0x40dd4c,0x49681a,0x40df30,0x47e0b0,0x40e234,
    0x47a288,0x49680e,0x47749c,0x478e48,0x4792ac,0x4796d8,0x478a3c};
static const unsigned wct_counts[WCT_IDS]={2,2,2,1,2,3,5,2,5,2,3,1,2,4,2,2};
static uint32_t wct_next(void){
    uint32_t z=wct_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *wct_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static uint32_t wct_fs0(void){uint32_t v;__asm__ volatile("movl %%fs:0,%0":"=r"(v));return v;}
static int wct_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static void wct_reset(void){wct_calls=0;wct_stream=wct_seed;wct_bump=WCT_A(WCT_BUMP);}
static uint32_t wct_alloc(uint32_t size){
    const uint32_t p=wct_bump;size=(size+15u)&~7u;
    if(size>0x400u||p+size>WCT_A(WCT_SIZE)){fail("weapon_ctors bump allocator overflow");ExitProcess(1);}
    memset(wct_host(p),0,size);wct_bump+=size;return p;
}
static int wct_range(uint32_t p,uint32_t n){return p-WCT_B<WCT_SIZE&&n<=WCT_SIZE-(p-WCT_B);}
/* The counter pointer after a call (effects): the second counter, or a
 * dword inside the weapon. */
static void wct_counter(uint32_t pick){
    put32(mapped+0x2cbee4,pick&0x100?WCT_A(WCT_COUNTER2):pick&0x200?wct_obj+0x70u+(pick>>12)%0x18u:WCT_A(WCT_COUNTER));
}
/* What the stubs answer and change. */
static uint32_t wct_answer(unsigned id,const uint32_t *a,uint32_t eax,uint32_t pick){
    const int effect=wct_effects&&pick%4==0;
    switch(id){
    case 0:   /* the weapon shape: a shape, or 0; the track words reread */
        if(wct_real)return pick%4?WCT_A(WCT_SHAPE):0;
        if(effect){if(pick&0x400)put16(wct_host(wct_mount+2),(uint16_t)wct_next());else wct_counter(pick);}
        return pick%8==1?0:eax;
    case 1:   /* shape_word_block: R+0 = the shape (another one or 0 as an effect) */
        put32(wct_host(a[0]),effect&&(pick&0x400)?(pick&0x800?0:wct_next()):a[1]);
        if(effect&&!(pick&0x400))wct_counter(pick);
        return eax;
    case 2:   /* object_track: a thread; the second track word or the block's shape reread */
        if(effect)switch(pick>>10&3){
        case 0:put16(wct_host(wct_mount+4),(uint16_t)wct_next());break;
        case 1:put32(wct_host(wct_obj+0x10),pick&0x1000?0:wct_next());break;
        default:wct_counter(pick);break;
        }
        return eax;
    case 5:   /* memset */
        if(!wct_range(a[0],a[2])){fail("weapon_ctors memset outside the arena");ExitProcess(1);}
        memset(wct_host(a[0]),(int)(a[1]&0xff),a[2]);return eax;
    case 6:   /* weapon_ctor: W+1Ch = C (gun_ctor rereads it; another record as an effect) */
        put32(wct_host(a[0]+0x1c),effect&&(pick&0x400)?WCT_A(WCT_DEF2):a[3]);
        if(effect&&!(pick&0x400))wct_counter(pick);
        return wct_real?a[0]:eax;
    case 3:case 4:case 7:case 8:
        if(effect)wct_counter(pick);
        return id==8&&wct_real?a[0]:eax;
    case 9:   /* 47a288: the copy block */
        return wct_alloc(a[0]);
    case 10:  /* memcpy */
        if(!wct_range(a[0],a[2])||!wct_range(a[1],a[2])){fail("weapon_ctors memcpy outside the arena");ExitProcess(1);}
        memmove(wct_host(a[0]),wct_host(a[1]),a[2]);return eax;
    case 11:  /* new: the thread */
        return wct_alloc(a[0]);
    }
    return eax;
}
__attribute__((used,noinline)) static uint64_t __cdecl wct_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(wct_calls==WCT_MAX){fail("weapon_ctors call overflow");ExitProcess(1);}
    wct_record *v=wct_log+wct_calls++;memset(v,0,sizeof(*v));v->id=id;
    v->slots=weapons_bridge_slots(a,wct_counts[id]);
    for(unsigned k=0;k<wct_counts[id];++k)v->args[k]=wct_stack(a[k])?0:a[k];
    /* ECX after a nested replaced hook is outside the contract: the real
     * passes log EAX and EDX only. */
    for(unsigned j=0;j<(wct_real?2u:3u);++j)v->regs[j]=wct_stack(regs[j])?0:regs[j];
    uint32_t eax=wct_next(),edx=wct_next(),pick=wct_next();wct_ecx=wct_next();
    eax=wct_answer(id,a,eax,pick);
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define WCT_STUB(n) __attribute__((naked)) static void wct_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _wct_stub_impl\n\taddl $24,%esp\n\tmovl _wct_ecx,%ecx\n\tret\n\t");}
WCT_STUB(0) WCT_STUB(1) WCT_STUB(2) WCT_STUB(3) WCT_STUB(4) WCT_STUB(5) WCT_STUB(6) WCT_STUB(7) WCT_STUB(8) WCT_STUB(9)
WCT_STUB(10) WCT_STUB(11) WCT_STUB(12) WCT_STUB(13) WCT_STUB(14) WCT_STUB(15)
#undef WCT_STUB
static void *wct_stubs[WCT_IDS]={wct_stub_0,wct_stub_1,wct_stub_2,wct_stub_3,wct_stub_4,wct_stub_5,wct_stub_6,wct_stub_7,wct_stub_8,
    wct_stub_9,wct_stub_10,wct_stub_11,wct_stub_12,wct_stub_13,wct_stub_14,wct_stub_15};
static int wct_patch(BYTE saved[WCT_IDS][5],int active[WCT_IDS],uint32_t which,int on){
    for(unsigned i=0;i<WCT_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(wct_addresses[i]-0x400000u,wct_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(wct_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
static const uint32_t wct_base_callees=WCT_BIT(0)|WCT_BIT(1)|WCT_BIT(2)|WCT_BIT(3)|WCT_BIT(4)|WCT_BIT(5);
/* Direct callees of each hook (bit i = wct_addresses[i]). */
static uint32_t wct_which(unsigned index){
    if(index==ES2_HOOK_INDEX_weapon_ctor)return wct_base_callees;
    if(index==ES2_HOOK_INDEX_beam_ctor)return WCT_BIT(6)|WCT_BIT(7);
    if(index==ES2_HOOK_INDEX_gun_ctor||index==ES2_HOOK_INDEX_equip_ctor)return WCT_BIT(6);
    return WCT_BIT(8);
}
/* Stubbed in the real-callee passes (the others run for real). */
static uint32_t wct_real_which(unsigned index){
    if(index==ES2_HOOK_INDEX_weapon_ctor)return WCT_BIT(0)|WCT_BIT(4)|WCT_BIT(5)|WCT_BIT(9)|WCT_BIT(10)|WCT_BIT(11)|
        WCT_BIT(12)|WCT_BIT(13)|WCT_BIT(14)|WCT_BIT(15);
    return wct_base_callees;
}
/* The image globals: the counter pointer, the mount offset and its flag
 * byte (written by the real mount_base/mount_offset). */
static const struct { uint32_t rva,size; } wct_ranges[]={{0x2cbee4,4},{0xa9af0,6},{0x98f34,1}};
enum { WCT_GLOBALS=4+6+1 };
static void wct_globals(BYTE *out,int restore){
    for(unsigned i=0;i<sizeof(wct_ranges)/sizeof(wct_ranges[0]);++i){
        if(restore)memcpy(mapped+wct_ranges[i].rva,out,wct_ranges[i].size);else memcpy(out,mapped+wct_ranges[i].rva,wct_ranges[i].size);
        out+=wct_ranges[i].size;
    }
}
static int wct_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<WCT_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
/* A small signed track word: negative (no track) or a track index. */
static uint16_t wct_track(void){return (uint16_t)(wct_next()%2?wct_next()%4:0x8000u|wct_next());}
/* The case data. exact: the factory's inputs; real: data for the
 * real-callee passes. */
static void wct_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],uint32_t regs[3],uint32_t *fill,int exact,int real){
    wct_seed=random32();wct_stream=wct_seed^n;wct_real=real;wct_effects=!real&&!exact;wct_index=index;
    for(unsigned i=0;i<3;++i){r[i]=wct_next();regs[i]=wct_next();}
    for(unsigned i=0;i<WCT_SIZE;++i)wct_arena[i]=(BYTE)wct_next();
    put32(mapped+0x2cbee4,WCT_A(WCT_COUNTER));
    for(unsigned i=0;i<6;++i)mapped[0xa9af0+i]=(BYTE)wct_next();
    mapped[0x98f34]=(BYTE)(wct_next()%2);
    *fill=!exact&&!real&&wct_next()%2?wct_next():0;
    const uint32_t W=WCT_A(WCT_OBJ+wct_next()%4);wct_obj=W;
    uint32_t M=WCT_A(WCT_MOUNT+wct_next()%4);
    /* odd cases: the mount record inside the weapon, the counter inside it */
    if(!exact&&!real&&wct_next()%4==0)M=W+wct_next()%0x80u;
    if(!exact&&!real&&wct_next()%4==0)put32(mapped+0x2cbee4,W+0x60u+wct_next()%0x28u);
    wct_mount=M;
    if(exact||real){
        wct_host(M)[6]=(BYTE)(wct_next()%8u-2u);put16(wct_host(M+2),wct_track());put16(wct_host(M+4),wct_track());
    }
    a[0]=W;a[1]=WCT_A(WCT_ACTOR);a[2]=M;a[3]=WCT_A(WCT_DEF);a[4]=wct_next();a[5]=wct_next();
    if(real){
        /* object_track: [A+34h] the instance, its shape chain; shape_word_block: the shape's words */
        put32(wct_arena+WCT_ACTOR+0x34,WCT_A(WCT_INST));put32(wct_arena+WCT_INST+4,WCT_A(WCT_SHAPE2));
        put32(wct_arena+WCT_SHAPE+0x1c,WCT_A(WCT_WORDS));put16(wct_arena+WCT_SHAPE+0x24,(uint16_t)(wct_next()%5));
    }
    wct_stream=wct_seed;   /* the stubs' stream restarts here in every run */
}
/* Calls target with six argument slots, EAX/EDX/ECX = r and EBX/ESI/EDI =
 * regs over 3,840 bytes of stack filled with `fill`; out receives EAX, EDX
 * and ECX after the call. */
__attribute__((naked,noinline)) static void wct_invoke(void *target,const uint32_t *a,const uint32_t *r,const uint32_t *regs,uint32_t fill,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 24(%ebp),%eax\n\tmovl $1024,%ecx\n\tcld\n\trep stosl\n\taddl $3840,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 20(%esi)\n\tpushl 16(%esi)\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 20(%ebp),%edi\n\tmovl (%edi),%ebx\n\tmovl 4(%edi),%esi\n\tmovl 8(%edi),%edi\n\t"
        "movl 16(%ebp),%eax\n\tmovl 4(%eax),%edx\n\tmovl 8(%eax),%ecx\n\tmovl (%eax),%eax\n\tcall *8(%ebp)\n\t"
        "movl 28(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* The core with the adapter's graph. */
static uint64_t wct_core(unsigned index,const uint32_t *a,const uint32_t *r,uint32_t *ecx){
    const es2_guest_memory *m=&math_native_memory;es2_weapon_graph g=weapons_graph(mapped,NULL,index);
    if(index==ES2_HOOK_INDEX_weapon_ctor)return es2_weapon_ctor_registers(m,&g,a[0],a[1],a[2],a[3],a[4],r,ecx);
    if(index==ES2_HOOK_INDEX_beam_ctor)return es2_beam_ctor_registers(m,&g,a[0],a[1],a[2],a[3],a[4],r,ecx);
    if(index==ES2_HOOK_INDEX_gun_ctor)return es2_gun_ctor_registers(m,&g,a[0],a[1],a[2],a[3],a[4],r,ecx);
    if(index==ES2_HOOK_INDEX_equip_ctor)return es2_equip_ctor_registers(m,&g,a[0],a[1],a[2],a[3],r,ecx);
    if(index==ES2_HOOK_INDEX_ecm_ctor)return es2_ecm_ctor_registers(m,&g,a[0],a[1],a[2],a[3],r,ecx);
    if(index==ES2_HOOK_INDEX_turbo_ctor)return es2_turbo_ctor_registers(m,&g,a[0],a[1],a[2],a[3],r,ecx);
    if(index==ES2_HOOK_INDEX_targ_ctor)return es2_targ_ctor_registers(m,&g,a[0],a[1],a[2],a[3],r,ecx);
    if(index==ES2_HOOK_INDEX_shield_ctor)return es2_shield_ctor_registers(m,&g,a[0],a[1],a[2],a[3],r,ecx);
    return es2_energy_ctor_registers(m,&g,a[0],a[1],a[2],a[3],r,ecx);
}
static int wct_report(const char *what,unsigned n,const uint32_t want[3],const uint32_t got[3],unsigned calls,const wct_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x calls %u/%u\n",current_function,n,what,
        want[0],got[0],want[1],got[1],want[2],got[2],calls,wct_calls);
    for(unsigned j=0;j<calls&&j<wct_calls&&expected;++j)if(memcmp(expected+j,wct_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(wct_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<WCT_SIZE;++j)if(wct_after[j]!=wct_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,wct_after[j],wct_arena[j]);break;}
    return 1;
}
static unsigned wct_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<wct_calls;++j)if(wct_addresses[wct_log[j].id]==c->key)++hits;
    return hits;
}
/* One direct case: the original, the core and (exact inputs) the adapter. */
static int wct_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r,const uint32_t *regs,uint32_t fill,int exact,int check_ecx){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[WCT_GLOBALS],after[WCT_GLOBALS],actual[WCT_GLOBALS];static wct_record expected[WCT_MAX];uint32_t want[3],got[3];
    memcpy(wct_before,wct_arena,WCT_SIZE);wct_globals(before,0);
    wct_reset();wct_invoke(mapped+d->rva,a,r,regs,fill,want);const unsigned calls=wct_calls;memcpy(expected,wct_log,calls*sizeof(*expected));
    memcpy(wct_after,wct_arena,WCT_SIZE);wct_globals(after,0);
    for(unsigned j=0;j<calls;++j)++wct_paths[expected[j].id];
    memcpy(wct_arena,wct_before,WCT_SIZE);wct_globals(before,1);wct_reset();
    got[2]=~want[2];
    uint64_t pair=wct_core(index,a,r,&got[2]);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);wct_globals(actual,0);
    if(want[0]!=got[0]||want[1]!=got[1]||(check_ecx&&want[2]!=got[2])||calls!=wct_calls||memcmp(expected,wct_log,calls*sizeof(*expected))||
       memcmp(wct_after,wct_arena,WCT_SIZE)||wct_globals_differ(after,actual))
        return wct_report("core",n,want,got,calls,expected);
    if(!exact){wct_globals(before,1);return 0;}
    /* the adapter (the framework's entry: six argument slots) */
    memcpy(wct_arena,wct_before,WCT_SIZE);wct_globals(before,1);wct_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];
    if(count>16)return fail("weapon_ctors counter table too long");
    for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair=x->replacement_context(a,d->user,r);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);got[2]=want[2];
    wct_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=wct_expected_hits(c+i))return fail("weapon_ctors callee counter delta");
    if(want[0]!=got[0]||want[1]!=got[1]||calls!=wct_calls||memcmp(expected,wct_log,calls*sizeof(*expected))||
       memcmp(wct_after,wct_arena,WCT_SIZE)||wct_globals_differ(after,actual))
        return wct_report("adapter",n,want,got,calls,expected);
    wct_globals(before,1);
    return 0;
}
static int wct_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],regs[3],fill,want[3]={0},got[3]={0},pattern=poison?0xa7a7a7a7u:0x31313131u;
        wct_build(index,n*37u+11u,a,r,regs,&fill,1,0);
        const uint32_t entry[3]={pattern,pattern,pattern};
        BYTE before[WCT_GLOBALS],after[WCT_GLOBALS],actual[WCT_GLOBALS],expected_fpu[108],actual_fpu[108];static wct_record expected[WCT_MAX];
        memcpy(wct_before,wct_arena,WCT_SIZE);wct_globals(before,0);
        wct_reset();wct_invoke(mapped+d->rva,a,entry,regs,pattern,want);es2_hook_fpu_save(expected_fpu);
        const unsigned calls=wct_calls;memcpy(expected,wct_log,calls*sizeof(*expected));memcpy(wct_after,wct_arena,WCT_SIZE);wct_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("weapon_ctors poison install");
        memcpy(wct_arena,wct_before,WCT_SIZE);wct_globals(before,1);wct_reset();wct_invoke(mapped+d->rva,a,entry,regs,pattern,got);
        es2_hook_fpu_save(actual_fpu);wct_globals(actual,0);
        if(want[0]!=got[0]||want[1]!=got[1]||calls!=wct_calls||memcmp(expected,wct_log,calls*sizeof(*expected))||
           memcmp(wct_after,wct_arena,WCT_SIZE)||wct_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||
           memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=wct_report(fast?"installed fast":"installed preserving",n,want,got,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("weapon_ctors poison uninstall");wct_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, pair/trace/arena/globals\n",d->name,total);
    return status;
}
/* ---- real callees ---- */
enum { WCT_NESTED=4 };
static unsigned wct_nested(unsigned index,unsigned nested[WCT_NESTED]){
    unsigned count=0;
#define WCT_N(name) nested[count++]=ES2_HOOK_INDEX_##name
    if(index==ES2_HOOK_INDEX_weapon_ctor){WCT_N(shape_word_block);WCT_N(object_track);WCT_N(mount_offset);WCT_N(mount_base);}
    else if(index==ES2_HOOK_INDEX_beam_ctor){WCT_N(weapon_ctor);WCT_N(multiply_q10);}
    else if(index==ES2_HOOK_INDEX_gun_ctor||index==ES2_HOOK_INDEX_equip_ctor){WCT_N(weapon_ctor);}
    else {WCT_N(equip_ctor);WCT_N(weapon_ctor);}
#undef WCT_N
    return count;
}
static int wct_real_pass(unsigned index){
    unsigned nested[WCT_NESTED];const unsigned count=wct_nested(index,nested);
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[WCT_NESTED+1]={0};
    BYTE saved[WCT_IDS][5];int active[WCT_IDS];
    if(wct_patch(saved,active,wct_real_which(index),1))return 1;
    static wct_record expected[WCT_MAX];
    const unsigned modes=2u<<count;
    for(unsigned mode=0;mode<modes&&!status;++mode)for(unsigned n=0;n<4096u/modes&&!status;++n){
        uint32_t a[6],r[3],regs[3],fill,want[3]={0},got[3]={0},pattern=n&1?0xa7a7a7a7u:0x31313131u;
        wct_build(index,n*13u+mode,a,r,regs,&fill,1,1);
        const uint32_t entry[3]={pattern,pattern,pattern};const uint32_t head=wct_fs0();
        BYTE before[WCT_GLOBALS],after[WCT_GLOBALS],actual[WCT_GLOBALS];
        memcpy(wct_before,wct_arena,WCT_SIZE);wct_globals(before,0);
        es2_hook hk[WCT_NESTED+1];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(mode>>(1+j)&1)?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,function_hook_extension(nested[j])))return fail("weapon_ctors nested install");}
        wct_reset();wct_invoke(mapped+d->rva,a,entry,regs,pattern,want);
        const unsigned calls=wct_calls;memcpy(expected,wct_log,calls*sizeof(*expected));
        memcpy(wct_after,wct_arena,WCT_SIZE);wct_globals(after,0);
        o.mode=mode&1?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(hk+WCT_NESTED,d,&o,function_hook_extension(index)))return fail("weapon_ctors caller install");
        memcpy(wct_arena,wct_before,WCT_SIZE);wct_globals(before,1);wct_reset();
        wct_invoke(mapped+d->rva,a,entry,regs,pattern,got);wct_globals(actual,0);
        if(wct_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||calls!=wct_calls||memcmp(expected,wct_log,calls*sizeof(*expected))||
           memcmp(wct_after,wct_arena,WCT_SIZE)||wct_globals_differ(after,actual)){
            fprintf(stderr,"real mode=%u\n",mode);status=wct_report("real",n,want,got,calls,expected);
        }
        for(unsigned j=0;j<=WCT_NESTED;++j){if(!hk[j].descriptor)continue;entered[j]+=hk[j].calls;
            if(hk[j].mismatches)status=fail("weapon_ctors real compare mismatch");
            if(!es2_hook_uninstall(hk+j))return fail("weapon_ctors real uninstall");}
        wct_globals(before,1);++total;
    }
    if(wct_patch(saved,active,0,0))return 1;
    wct_real=0;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&!entered[WCT_NESTED]){fprintf(stderr,"FAIL: %s never entered\n",d->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, %u nested hooks original/replaced, hook original/replaced, 31/a7 stack, pair, calls, arena and globals\n",
        d->name,total,count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
static int weapon_ctors_cases(unsigned index,unsigned limit){
    current_function=function_hook_descriptor(index)->name;
    const uint32_t which=wct_which(index);
    BYTE saved[WCT_IDS][5],old_globals[WCT_GLOBALS];int active[WCT_IDS];
    wct_globals(old_globals,0);
    if(wct_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_WEAPON_CTORS_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);memset(wct_paths,0,sizeof(wct_paths));
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3],regs[3],fill;const int exact=limit||!(n&1);
        wct_build(index,n,a,r,regs,&fill,exact,0);status=wct_compare(index,n,a,r,regs,fill,exact&&!limit,!limit);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    wct_globals(old_globals,1);
    if(!limit&&!status)status=wct_installed_probes(index,64);
    if(wct_patch(saved,active,which,0))return 1;
    wct_globals(old_globals,1);
    if(!limit&&!status)status=wct_real_pass(index);
    wct_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases (stub calls 40fab0 %u, 402fc0 %u, 402374 %u, 40f540 %u, 40dd4c %u, memset %u, 40df30 %u, "
        "47e0b0 %u, 40e234 %u); full return pair and ECX, ordered calls with entry registers and slots, arena/globals\n",
        function_hook_descriptor(index)->name,cases,wct_paths[0],wct_paths[1],wct_paths[2],wct_paths[3],wct_paths[4],wct_paths[5],
        wct_paths[6],wct_paths[7],wct_paths[8]);
    return status;
}
