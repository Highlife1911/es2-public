/* mouse_events group: mouse_dispatch 452d40 and mouse_queue 453078
 * (docs/reverse-engineering/input.md, "mouse event queue 452d40 / 453078").
 * Prefix mev_.
 *
 * mouse_dispatch: direct cases run the original body and the core against
 * logging stubs patched over the six direct callees (486e7c, 452ab8,
 * 4523b0, 45297c, 4527c8, 452898) and arena vtables whose slots +8, +10h,
 * +18h (and the rest) are stubs: ordered calls with arguments and entry
 * EAX/EDX/ECX (stack values masked: the drag point lives in the original's
 * frame or the adapter's scratch, so its 8 bytes are logged by content)
 * and a hash of the arena and the global windows at each call, so the
 * order of every store relative to the calls is compared. The stubs return
 * planned registers (the hit test's AL zero or not) and, in one call of
 * four, change what the original reads again: the active screen, the
 * captured index, the drag/follow/playback flags, the button states, the
 * double-click time, the read list's count (at most twice) or pointer, the
 * current event's fields, a gadget's vtable or array entry, the screen
 * origin. Compared: EAX/EDX, ECX (the adapter's, which the entry shim
 * returns), the arena, the windows 49db6c..49dbef, 4d1dcc..4d1e0f and
 * 4d24d6, and the callee counter deltas (first 4,096 cases). Then
 * installed 31/a7 poison probes through the entry shim and a real-callee
 * pass with the real 486e7c, 452ab8, 4523b0, 45297c, 4527c8, 452898 (and
 * 452a28 inside), the gadget virtuals stubs, the hook original or
 * replaced.
 *
 * mouse_queue: direct cases against the original (EAX/EDX/ECX, the arena
 * with the list, the slot and the event, also overlapping, and the window
 * 4d1e04), then installed probes (EAX/EDX; ECX is dead at both callers). */
typedef struct mev_record { uint32_t id,args[2],regs[3],eax,content[2],hash; } mev_record;
enum { MEV_SIZE=0x1000, MEV_MAX=96, MEV_IDS=10, MEV_HA=0x000, MEV_HB=0x010, MEV_EA=0x040, MEV_EB=0x140, MEV_S1=0x240,
    MEV_S2=0x500, MEV_GAD=0x800, MEV_V1=0xa00, MEV_V2=0xa40, MEV_WINDOWS=4, MEV_EVENTS=12 };
#define MEV_BASE ((uint32_t)(uintptr_t)mev_arena)
#define MEV_VA(va) (mapped+(uint32_t)(va)-0x400000u)
static BYTE mev_arena[MEV_SIZE],mev_before[MEV_SIZE],mev_after[MEV_SIZE];
static mev_record mev_log[MEV_MAX];
static unsigned mev_calls,mev_changes;
static int mev_real;
static uint32_t mev_seed,mev_stream;
__attribute__((used)) static uint32_t mev_ecx;
/* 0 486e7c, 1 452ab8, 2 4523b0, 3 45297c, 4 4527c8, 5 452898, 6 slot
 * +18h, 7 slot +10h, 8 slot +8, 9 every other slot (+4 behind the real
 * 452ab8 in the real pass). */
static const uint32_t mev_addresses[MEV_IDS]={0x486e7c,0x452ab8,0x4523b0,0x45297c,0x4527c8,0x452898,0,0,0,0};
static const uint32_t mev_slots[MEV_IDS]={0,0,0,0,0,0,0x18,0x10,0x08,0};
static const unsigned mev_counts[MEV_IDS]={1,2,2,2,2,2,2,1,2,1};
/* Global windows (VA, length): event word through the screen globals,
 * positions/states/cursor/list pointers, the playback word, the software
 * cursor 4a361c..4a3651 (written by the real 486e7c). */
static const uint32_t mev_window_va[MEV_WINDOWS]={0x49db6c,0x4d1dcc,0x4d24d6,0x4a361c};
static const unsigned mev_window_len[MEV_WINDOWS]={0x84,0x44,2,0x36};
#define MEV_GLOBALS (0x84+0x44+2+0x36)
static void mev_globals(BYTE *out,int restore){
    for(unsigned i=0,at=0;i<MEV_WINDOWS;at+=mev_window_len[i++]){
        BYTE *p=MEV_VA(mev_window_va[i]);
        if(restore)memcpy(p,out+at,mev_window_len[i]);else memcpy(out+at,p,mev_window_len[i]);
    }
}
static int mev_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<MEV_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
static uint32_t mev_next(void){
    uint32_t z=mev_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static int mev_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static BYTE *mev_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static uint32_t mev_hash(void){
    uint32_t h=0x811c9dc5u;
    for(unsigned j=0;j<MEV_SIZE;++j)h=(h^mev_arena[j])*0x01000193u;
    for(unsigned i=0;i<MEV_WINDOWS;++i){const BYTE *p=MEV_VA(mev_window_va[i]);for(unsigned j=0;j<mev_window_len[i];++j)h=(h^p[j])*0x01000193u;}
    return h;
}
static uint32_t mev_screen(unsigned k){return MEV_BASE+(k?MEV_S2:MEV_S1);}
static uint32_t mev_gadget(unsigned k){return MEV_BASE+MEV_GAD+0x40u*(k%8u);}
static uint16_t mev_index(uint32_t pick){return (uint16_t)(pick%3==0?0xffffu:(pick>>2)%8u);}
/* One poke of a value the original reads again after a call. */
static void mev_poke(uint32_t pick,const uint32_t *a,unsigned id){
    const uint32_t q=pick>>5;
    switch(pick%16){
    case 0: put32(MEV_VA(0x49dbd8),mev_screen(q&1));break;
    /* Not -1: the release path rereads the index after 452ab8 without a test. */
    case 1: put16(MEV_VA(0x49dbdc),(uint16_t)(q%8));break;
    case 2: MEV_VA(0x49dbde)[0]=(BYTE)(q%3==0?0:1+q%5);break;
    case 3: MEV_VA(0x49dbdf)[0]=(BYTE)(q&1);put16(MEV_VA(0x4d24d6),(uint16_t)(q>>1&1));break;
    case 4: MEV_VA(0x4d1dd4)[q&1]=(BYTE)((q>>1)%4);break;
    case 5: put32(MEV_VA(0x4d1dec),(q>>1)%64);break;
    case 6: if(mev_changes<2){++mev_changes;put32(mev_host(get32(MEV_VA(0x4d1e08))),(uint32_t)((int32_t)(q%12)-2));}break;
    case 7: put32(MEV_VA(0x4d1e08),MEV_BASE+(q&1?MEV_HB:MEV_HA));break;
    case 8: case 9: {
        /* The event the callee received (486e7c: first argument). */
        uint32_t E=id==0?a[0]:id>=2&&id<=5?a[1]:0;
        if(E&&E-MEV_BASE<MEV_SIZE-16u){
            if(q&1)put16(mev_host(E+8),(uint16_t)(q>>1&3));else put32(mev_host(E+(q&2?4:0)),(q>>2)%6);
            if(q&4)put32(mev_host(E+10),get32(mev_host(E+10))+(q>>4)%40-20);
        }
        break; }
    case 10: put32(mev_host(mev_gadget(q)+0x17),MEV_BASE+(q&8?MEV_V2:MEV_V1));break;
    case 11: put32(mev_host(get32(MEV_VA(0x49dbd8))+0x256+4*(q%8)),mev_gadget(q>>3));break;
    case 12: put32(mev_host(get32(MEV_VA(0x49dbd8))+0x22b+(q&4)),(q>>3)%8);break;
    case 13: put32(MEV_VA(0x49dbe4)+4*(q&1),q>>1);break;
    default: break;
    }
}
__attribute__((used,noinline)) static uint64_t __cdecl mev_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(mev_calls==MEV_MAX){fail("mouse events synthetic call overflow");ExitProcess(1);}
    mev_record *v=mev_log+mev_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,4*mev_counts[id]);memcpy(v->regs,regs,12);
    for(unsigned j=0;j<3;++j)if(mev_stack(v->regs[j]))v->regs[j]=0;
    for(unsigned j=0;j<mev_counts[id];++j)if(mev_stack(v->args[j]))v->args[j]=0;
    if(id==6){v->content[0]=get32(mev_host(a[1]));v->content[1]=get32(mev_host(a[1]+4));}
    v->hash=mev_hash();
    uint32_t eax=mev_next(),edx=mev_next(),pick=mev_next();mev_ecx=mev_next();
    if(id==2)eax=pick%2?eax&0xffffff00u:eax|1u;
    if(id==7)eax=pick%4?(eax&0xffff0000u)|(pick>>4&3):eax;
    if(!mev_real&&pick%4==0){mev_poke(mev_next(),a,id);if(pick%8==0)mev_poke(mev_next(),a,id);}
    v->eax=eax;return (uint64_t)edx<<32|eax;
}
#define MEV_STUB(n) __attribute__((naked)) static void mev_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _mev_stub_impl\n\taddl $24,%esp\n\tmovl _mev_ecx,%ecx\n\tret\n\t");}
MEV_STUB(0) MEV_STUB(1) MEV_STUB(2) MEV_STUB(3) MEV_STUB(4) MEV_STUB(5) MEV_STUB(6) MEV_STUB(7) MEV_STUB(8) MEV_STUB(9)
#undef MEV_STUB
static void *mev_stubs[MEV_IDS]={mev_stub_0,mev_stub_1,mev_stub_2,mev_stub_3,mev_stub_4,mev_stub_5,mev_stub_6,mev_stub_7,
    mev_stub_8,mev_stub_9};
static void mev_reset(void){mev_calls=0;mev_changes=0;mev_stream=mev_seed;}
/* Calls target with n argument slots, EAX/EDX/ECX/EBX from regs; with
 * pattern != 0 the 4 KiB below are filled first. EAX/EDX/ECX after RET. */
__attribute__((naked,noinline)) static void mev_invoke(void *target,const uint32_t *a,unsigned n,const uint32_t regs[4],uint32_t pattern,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "cmpl $0,24(%ebp)\n\tje 1f\n\tsubl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 24(%ebp),%eax\n\tmovl $1024,%ecx\n\tcld\n\trep stosl\n\taddl $4096,%esp\n\t"
        "1: movl 12(%ebp),%esi\n\tmovl 16(%ebp),%ecx\n\t"
        "2: testl %ecx,%ecx\n\tjz 3f\n\tdecl %ecx\n\tpushl (%esi,%ecx,4)\n\tjmp 2b\n\t"
        "3: movl 20(%ebp),%esi\n\tmovl (%esi),%eax\n\tmovl 4(%esi),%edx\n\tmovl 8(%esi),%ecx\n\tmovl 12(%esi),%ebx\n\tcall *8(%ebp)\n\t"
        "movl 28(%ebp),%ebx\n\tmovl %eax,(%ebx)\n\tmovl %edx,4(%ebx)\n\tmovl %ecx,8(%ebx)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static uint32_t mev_small(void){unsigned k=random32()%8;return k<5?random32()%6:k<7?random32()%0x200:random32();}
/* One event: positions often repeat, buttons mostly bits 0/1, times near
 * the release times. */
static void mev_event(BYTE *e,uint32_t time){
    put32(e,mev_small());put32(e+4,mev_small());
    put16(e+8,(uint16_t)(random32()%8?random32()%4:random32()));
    put32(e+10,time+random32()%64);
}
/* Gadget k: rectangle, round flag, centre, radius, vtable, state, drag. */
static void mev_gadget_build(unsigned k){
    BYTE *g=mev_host(mev_gadget(k));uint32_t x=random32()%8,y=random32()%8;
    put32(g,x);put32(g+4,y);put32(g+8,x+random32()%8);put32(g+0xc,y+random32()%8);
    g[0x10]=(BYTE)(random32()%4==0);put16(g+0x11,(uint16_t)(random32()%8));put16(g+0x13,(uint16_t)(random32()%8));
    put16(g+0x15,(uint16_t)(random32()%10));put32(g+0x17,MEV_BASE+(random32()%2?MEV_V2:MEV_V1));
    put16(g+0x1b,(uint16_t)(random32()%4?random32()%3:random32()));put16(g+0x1d,(uint16_t)(random32()%3?random32()%2:random32()));
}
static void mev_screen_build(unsigned k){
    BYTE *s=mev_host(mev_screen(k));uint32_t x=random32()%4,y=random32()%4;
    put32(s,x);put32(s+4,y);put32(s+8,x+random32()%8);put32(s+0xc,y+random32()%8);s[0x10]=(BYTE)(random32()%4==0);
    put16(s+0x11,(uint16_t)(random32()%8));put16(s+0x13,(uint16_t)(random32()%8));put16(s+0x15,(uint16_t)(random32()%12));
    put32(s+0x22b,mev_small());put32(s+0x22f,mev_small());
    put16(s+0x254,(uint16_t)(random32()%9));
    for(unsigned j=0;j<8;++j)put32(s+0x256+4*j,mev_gadget(random32()));
}
static uint32_t mev_count(void){
    unsigned k=random32()%16;return k<3?0:k<12?1+random32()%4:k<14?5+random32()%6:k==14?(uint32_t)-(int32_t)(1+random32()%2):0x80000000u;
}
static void mev_build(void){
    const uint32_t b=MEV_BASE;
    mev_seed=random32();random_bytes(mev_arena,MEV_SIZE);
    put32(mev_arena+MEV_HA,mev_count());put32(mev_arena+MEV_HA+4,b+MEV_EA);
    put32(mev_arena+MEV_HB,mev_count());put32(mev_arena+MEV_HB+4,b+MEV_EB);
    unsigned w=random32()%2;
    put32(MEV_VA(0x4d1e04),b+(w?MEV_HB:MEV_HA));put32(MEV_VA(0x4d1e08),b+(random32()%16==0?(w?MEV_HB:MEV_HA):(w?MEV_HA:MEV_HB)));
    uint32_t time=random32()%4?random32()%1000:random32();
    for(unsigned k=0;k<MEV_EVENTS;++k){mev_event(mev_arena+MEV_EA+14*k,time);mev_event(mev_arena+MEV_EB+14*k,time);}
    for(unsigned k=0;k<2;++k)mev_screen_build(k);
    for(unsigned k=0;k<8;++k)mev_gadget_build(k);
    for(unsigned v=0;v<2;++v)for(unsigned j=0;j<8;++j){
        const unsigned slot=4*j,id=slot==0x18?6:slot==0x10?7:slot==8?8:9;
        put32(mev_arena+(v?MEV_V2:MEV_V1)+slot,(uint32_t)(uintptr_t)mev_stubs[id]);
    }
    /* Globals: the last position often the first event's, states 0/1/other. */
    const uint32_t first=get32(MEV_VA(0x4d1e04));const BYTE *e=mev_host(get32(mev_host(first+4)));
    if(random32()%3==0){put32(MEV_VA(0x4d1dcc),get32(e));put32(MEV_VA(0x4d1dd0),random32()%4?get32(e+4):mev_small());}
    else {put32(MEV_VA(0x4d1dcc),mev_small());put32(MEV_VA(0x4d1dd0),mev_small());}
    for(unsigned j=0;j<2;++j)MEV_VA(0x4d1dd4)[j]=(BYTE)(random32()%8?random32()%2:random32());
    random_bytes(MEV_VA(0x4d1de3),8);
    put32(MEV_VA(0x4d1dec),random32()%4?30:random32()%4?random32()%100:random32());
    put16(MEV_VA(0x49db6c),(uint16_t)random32());
    put32(MEV_VA(0x49dbd8),mev_screen(random32()%4==0));
    put16(MEV_VA(0x49dbdc),mev_index(random32()));
    MEV_VA(0x49dbde)[0]=(BYTE)(random32()%3==0?1+random32()%3:0);
    MEV_VA(0x49dbdf)[0]=(BYTE)(random32()%2);MEV_VA(0x49dbe0)[0]=(BYTE)random32();
    for(unsigned j=0;j<2;++j)put32(MEV_VA(0x49dbe4)+4*j,random32()%4?time+random32()%64-32:random32());
    put16(MEV_VA(0x4d24d6),(uint16_t)(random32()%4==0?1+random32()%2:0));
    MEV_VA(0x49dbec)[0]=(BYTE)(random32()%2);
}
static int mev_report(const char *what,unsigned n,const uint32_t want[3],const uint32_t got[3],unsigned calls,const mev_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x calls %u/%u\n",current_function,n,what,
        want[0],got[0],want[1],got[1],want[2],got[2],calls,mev_calls);
    for(unsigned j=0;j<calls&&j<mev_calls&&expected;++j)if(memcmp(expected+j,mev_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(mev_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<calls&&expected&&j<12;++j)fprintf(stderr," trace %u id%u args %08x %08x regs %08x %08x %08x\n",j,expected[j].id,
        expected[j].args[0],expected[j].args[1],expected[j].regs[0],expected[j].regs[1],expected[j].regs[2]);
    for(unsigned j=0;j<MEV_SIZE;++j)if(mev_after[j]!=mev_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,mev_after[j],mev_arena[j]);break;}
    return 1;
}
static unsigned mev_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<mev_calls;++j){const unsigned id=mev_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?mev_slots[id]==c->key:mev_addresses[id]==c->key)++hits;}
    return hits;
}
static int mev_patch(BYTE saved[MEV_IDS][5],int active[MEV_IDS],uint32_t which,int on){
    for(unsigned i=0;i<MEV_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!mev_addresses[i])continue;if(actor_patch(mev_addresses[i]-0x400000u,mev_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(mev_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
static int mev_dispatch_compare(unsigned index,unsigned n,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    static BYTE before[MEV_GLOBALS],after[MEV_GLOBALS],actual[MEV_GLOBALS];static mev_record expected[MEV_MAX];
    uint32_t regs[4]={r[0],r[1],r[2],0},a[2]={0,0},want[3],got[3];
    memcpy(mev_before,mev_arena,MEV_SIZE);mev_globals(before,0);
    mev_reset();mev_invoke(mapped+d->rva,a,0,regs,0,want);unsigned calls=mev_calls;memcpy(expected,mev_log,calls*sizeof(*expected));
    memcpy(mev_after,mev_arena,MEV_SIZE);mev_globals(after,0);
    memcpy(mev_arena,mev_before,MEV_SIZE);mev_globals(before,1);mev_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[MEV_IDS];
    for(unsigned i=0;i<count&&i<MEV_IDS;++i)counts[i]=c[i].count;
    mouse_events_ecx_valid=0;uint64_t pair=x->replacement_context(a,d->user,r);mev_globals(actual,0);
    got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);got[2]=mouse_events_ecx;
    if(!mouse_events_ecx_valid)return fail("mouse dispatch adapter left no ECX");
    mouse_events_ecx_valid=0;
    if(n<4096)for(unsigned i=0;i<count&&i<MEV_IDS;++i)if(c[i].count-counts[i]!=mev_expected_hits(c+i))return fail("mouse dispatch callee counter delta");
    if(memcmp(want,got,12)||calls!=mev_calls||memcmp(expected,mev_log,calls*sizeof(*expected))||memcmp(mev_after,mev_arena,MEV_SIZE)||
       mev_globals_differ(after,actual))
        return mev_report("direct",n,want,got,calls,expected);
    return 0;
}
static int mev_dispatch_installed(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t want[3],got[3],a[2]={0,0},pattern=poison?0xa7a7a7a7u:0x31313131u,regs[4]={pattern,pattern,pattern,pattern};mev_build();
        static BYTE before[MEV_GLOBALS],after[MEV_GLOBALS],actual[MEV_GLOBALS];BYTE expected_fpu[108],actual_fpu[108];static mev_record expected[MEV_MAX];
        memcpy(mev_before,mev_arena,MEV_SIZE);mev_globals(before,0);
        mev_reset();mev_invoke(mapped+d->rva,a,0,regs,pattern,want);es2_hook_fpu_save(expected_fpu);
        unsigned calls=mev_calls;memcpy(expected,mev_log,calls*sizeof(*expected));memcpy(mev_after,mev_arena,MEV_SIZE);mev_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("mouse dispatch poison install");
        memcpy(mev_arena,mev_before,MEV_SIZE);mev_globals(before,1);mev_reset();
        mev_invoke(mapped+d->rva,a,0,regs,pattern,got);es2_hook_fpu_save(actual_fpu);mev_globals(actual,0);
        if(memcmp(want,got,12)||calls!=mev_calls||memcmp(expected,mev_log,calls*sizeof(*expected))||memcmp(mev_after,mev_arena,MEV_SIZE)||
           mev_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1||mouse_events_ecx_valid)
            status=mev_report(fast?"installed fast":"installed preserving",n,want,got,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("mouse dispatch poison uninstall");mev_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, EAX/EDX/ECX through the entry shim, trace/arena/globals\n",d->name,total);
    return status;
}
/* Real 486e7c, 452ab8, 4523b0, 45297c, 4527c8, 452898 (452a28 inside);
 * the gadget virtuals stay stubs. The software cursor's lock pointer
 * 4a361c is 0, so 486e7c never reaches an import. The hook original
 * (through its shim) or replaced, against a run without the hook. */
static int mev_dispatch_real(unsigned index){
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t presses=0,releases=0,moves=0;
    static mev_record expected[MEV_MAX];
    mev_real=1;
    for(unsigned mode=0;mode<2&&!status;++mode)for(unsigned n=0;n<1536&&!status;++n){
        uint32_t want[3],got[3],a[2]={0,0},pattern=n&1?0xa7a7a7a7u:0x31313131u,regs[4]={pattern,pattern,pattern,pattern};mev_build();
        put32(MEV_VA(0x4a361c),0);put32(MEV_VA(0x4a3646),random32()%2);
        /* Valid captured index for the real handlers (-1 or 0..7). */
        static BYTE before[MEV_GLOBALS],after[MEV_GLOBALS],actual[MEV_GLOBALS];
        memcpy(mev_before,mev_arena,MEV_SIZE);mev_globals(before,0);
        mev_reset();mev_invoke(mapped+d->rva,a,0,regs,pattern,want);
        unsigned calls=mev_calls;memcpy(expected,mev_log,calls*sizeof(*expected));
        memcpy(mev_after,mev_arena,MEV_SIZE);mev_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        o.mode=mode?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("mouse dispatch real install");
        unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[MEV_IDS];
        for(unsigned i=0;i<count&&i<MEV_IDS;++i)counts[i]=c[i].count;
        memcpy(mev_arena,mev_before,MEV_SIZE);mev_globals(before,1);mev_reset();
        mev_invoke(mapped+d->rva,a,0,regs,pattern,got);mev_globals(actual,0);
        if(mode){presses+=c[3].count-counts[3];releases+=c[4].count-counts[4];moves+=c[2].count-counts[2];}
        if(memcmp(want,got,12)||calls!=mev_calls||memcmp(expected,mev_log,calls*sizeof(*expected))||memcmp(mev_after,mev_arena,MEV_SIZE)||
           mev_globals_differ(after,actual)||(mode&&h.calls!=1)||mouse_events_ecx_valid){
            fprintf(stderr,"real mode=%u calls=%llu\n",mode,(unsigned long long)h.calls);status=mev_report("real",n,want,got,calls,expected);
        }
        if(!es2_hook_uninstall(&h))return fail("mouse dispatch real uninstall");mev_globals(before,1);++total;
    }
    mev_real=0;
    if(!status&&(!presses||!releases||!moves)){fprintf(stderr,"FAIL: %s real pass presses %llu releases %llu moves %llu\n",d->name,
        (unsigned long long)presses,(unsigned long long)releases,(unsigned long long)moves);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases (486e7c, 452ab8, 4523b0, 45297c, 4527c8, 452898 real; %llu presses, %llu releases, %llu moves), hook original or replaced, EAX/EDX/ECX, virtual calls, arena and globals\n",
        d->name,total,(unsigned long long)presses,(unsigned long long)releases,(unsigned long long)moves);
    return status;
}
static int mev_dispatch_cases(unsigned index,unsigned limit){
    BYTE saved[MEV_IDS][5];static BYTE old_globals[MEV_GLOBALS];int active[MEV_IDS];
    mev_globals(old_globals,0);
    if(mev_patch(saved,active,0x3fu,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_MOUSE_EVENTS_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t r[3];mev_build();for(unsigned j=0;j<3;++j)r[j]=random32();status=mev_dispatch_compare(index,n,r);mev_globals(old_globals,1);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=mev_dispatch_installed(index,64);
    if(mev_patch(saved,active,0,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=mev_dispatch_real(index);
    mev_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; EAX/EDX/ECX, ordered calls with entry registers, store order (hash per call), arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
/* mouse_queue: the write list header at +0, entries at +20h.. (a slot
 * offset of up to 0Fh), the event at +0C00h or overlapping the slot. */
static void mev_queue_build(uint32_t a[2],uint32_t r[3]){
    const uint32_t b=MEV_BASE;random_bytes(mev_arena,MEV_SIZE);
    unsigned k=random32()%8;uint32_t n=k==0?0x63:k==1?0x62+random32()%3:random32()%0x80;
    uint32_t header=random32()%4?0:0x10;
    put32(mev_arena+header,n);put32(mev_arena+header+4,b+0x20+random32()%16);
    put32(MEV_VA(0x4d1e04),b+header);
    const uint32_t slot=get32(mev_arena+header+4)+14*n;
    if(random32()%4==0&&n<0x80){a[0]=slot+random32()%25-12;}
    else a[0]=b+0xc00+random32()%16;
    a[1]=random32();
    for(unsigned j=0;j<3;++j)r[j]=random32();
}
static int mev_queue_cases(unsigned index,unsigned limit){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE saved_window[4];memcpy(saved_window,MEV_VA(0x4d1e04),4);
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_MOUSE_EVENTS_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[2],r[3],want[3],got[3];mev_queue_build(a,r);uint32_t regs[4]={r[0],r[1],r[2],0};
        memcpy(mev_before,mev_arena,MEV_SIZE);BYTE window[4];memcpy(window,MEV_VA(0x4d1e04),4);
        mev_invoke(mapped+d->rva,a,2,regs,0,want);memcpy(mev_after,mev_arena,MEV_SIZE);
        memcpy(mev_arena,mev_before,MEV_SIZE);
        uint64_t pair=x->replacement_context(a,d->user,r);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);got[2]=mouse_events_queue_ecx;
        /* Installed (the harness integration pass), the "original" is the
         * hook, which returns EDX:EAX only: ECX is compared uninstalled. */
        if(memcmp(want,got,rng_hooks[index].installed?8:12)||memcmp(mev_after,mev_arena,MEV_SIZE)||memcmp(window,MEV_VA(0x4d1e04),4)){
            mev_calls=0;status=mev_report("direct",n,want,got,0,NULL);}
    }
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    /* Installed: EAX/EDX after RET (ECX is not part of the hook's result). */
    if(!limit&&!status){
        BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);__asm__ volatile("fninit; fld1; fldpi");unsigned total=0;
        for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<64&&!status;++n){
            uint32_t a[2],r[3],want[3],got[3],pattern=poison?0xa7a7a7a7u:0x31313131u,regs[4]={pattern,pattern,pattern,pattern};
            BYTE expected_fpu[108],actual_fpu[108];mev_queue_build(a,r);
            memcpy(mev_before,mev_arena,MEV_SIZE);
            mev_invoke(mapped+d->rva,a,2,regs,pattern,want);es2_hook_fpu_save(expected_fpu);memcpy(mev_after,mev_arena,MEV_SIZE);
            es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
            es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,x))return fail("mouse queue poison install");
            memcpy(mev_arena,mev_before,MEV_SIZE);mev_invoke(mapped+d->rva,a,2,regs,pattern,got);es2_hook_fpu_save(actual_fpu);
            if(memcmp(want,got,8)||memcmp(mev_after,mev_arena,MEV_SIZE)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1){
                mev_calls=0;status=mev_report(fast?"installed fast":"installed preserving",n,want,got,0,NULL);}
            if(!es2_hook_uninstall(&h))return fail("mouse queue poison uninstall");++total;
        }
        es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
        if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, EAX/EDX, list/slot/event\n",d->name,total);
    }
    memcpy(MEV_VA(0x4d1e04),saved_window,4);
    if(!status)printf("PASS: %-20s %u cases; EAX/EDX/ECX, list, slot and event (also overlapping)\n",d->name,cases);
    return status;
}
static int mouse_events_cases(unsigned index,unsigned limit){
    return index==ES2_HOOK_INDEX_mouse_dispatch?mev_dispatch_cases(index,limit):mev_queue_cases(index,limit);
}
