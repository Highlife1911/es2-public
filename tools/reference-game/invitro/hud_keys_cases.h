/* hud_keys group: the HUD key handler 432bc8 (hud_key), the button press 438d9c
 * (button_press) and the button-release scheduler 4530bc (button_release)
 * (docs/reverse-engineering/hud.md, "HUD key handler as own code"). A 64 KiB
 * arena holds the cockpit H, the display object D, the MFD, the panel gadgets
 * with their vtables, the weapon system and the panel/release objects; the
 * image globals 49b088 (H pointer), 4cf99c (D pointer), 6cbee4 (RTTI counter
 * pointer) and 4d2538 live in the mapped image's DATA. Every game callee is a
 * logging stub patched into the private image (49630c links a SEH record into
 * FS:0 as the original does, like meteor), and the panel gadget +0xc / button
 * +8 virtuals go through arena vtables that point at slot stubs. Direct cases
 * run the original body (hk_machine) and the adapter against the stubs and
 * compare EDX:EAX, the ordered calls with their arguments and entry registers,
 * the arena, the image globals, the callee-counter deltas and FS:0. Then
 * installed 31/a7 poison probes, and real-callee passes: hud_key over the real
 * 438d9c/4530bc (button_press and button_release in rotating modes; only the
 * clock 467828 and the deeper callees stay stubs), and button_press over the
 * real 4530bc. Then hud_key's frame probe (its entry shim: EBX/ESI/EDI
 * and EBP/ESP per stub, the 1024 bytes below the slots, the getter 429820 as a
 * stub). Identifiers: hk_/HK_. */
/* Objects are spaced so 4530bc's release array (P+0x356 .. +8*count), the
 * gadget fields (MFD +0xc1, the button arrays) and the weapon fields never
 * overlap a neighbour. Each gadget is a 0x100-byte object whose [+0] points
 * at a 0x40-byte vtable (slot +0xc = the key handler, +8 = the press). */
enum { HK_SIZE=0x10000, HK_H=0x100, HK_D=0x1000, HK_PANEL=0x2000, HK_REL=0x2400,
    HK_WS=0x2800, HK_COUNTER=0x2c00, HK_VT=0x3000, HK_GAD=0x4000, HK_DLG=HK_GAD+0x800,
    HK_MFD=HK_GAD+0x700, HK_ELEM=0x5000, HK_BTN=0x6000, HK_BVT=0x6800,
    HK_IDS=18, HK_MAX=48, HK_GLOBALS=12 };
#define HK_BASE ((uint32_t)(uintptr_t)hk_arena)
#define HK_ADDR(o) (HK_BASE+(uint32_t)(o))
static BYTE hk_arena[HK_SIZE],hk_before[HK_SIZE],hk_after[HK_SIZE];
typedef struct hk_record { uint32_t id,a[3],regs[3],cs[3],frame[2]; } hk_record;
static hk_record hk_log[HK_MAX];
static unsigned hk_calls;
static uint32_t hk_seed,hk_stream;
__attribute__((used)) static uint32_t hk_ecx;
/* Frame probe: EBP at the last stub entry and the slot address of the tracked
 * call; while hk_track is set a record also keeps EBX/ESI/EDI (cs) and EBP/ESP
 * at entry relative to the slots (frame); the 1024 bytes below the slots after
 * return. */
__attribute__((used)) static uint32_t hk_entry_ebp,hk_origin;
__attribute__((used)) static BYTE hk_window[1024];
static int hk_track;
/* ids 0..13,16 are patched callees (VA); 14,15 are arena-vtable slots; 17
 * (the display-object getter 429820) is patched only by the frame probe. */
static const uint32_t hk_addr[HK_IDS]={0x49630c,0x432a50,0x438d9c,0x4530bc,0x42a3f4,0x43349c,0x4332dc,0x40e4ac,
    0x41b468,0x446c38,0x434974,0x47749c,0x457538,0x4543b0,0,0,0x467828,0x429820};
static const unsigned hk_arity[HK_IDS]={0,2,3,2,2,1,2,2,1,2,0,1,3,2,2,1,0,0};
static const unsigned hk_slotkey[HK_IDS]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0xc,0x8,0,0};
static uint32_t hk_next(void){
    uint32_t z=hk_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static void hk_reset(void){hk_calls=0;hk_stream=hk_seed;}
static uint32_t hk_fs0(void){uint32_t v;__asm__ volatile("movl %%fs:0,%0":"=r"(v));return v;}
static void hk_globals(BYTE *out,int restore){
    static const struct { uint32_t rva,size; } r[]={{0x9b088,4},{0xcf99c,4},{0x2cbee4,4}};
    BYTE *p=out;for(unsigned i=0;i<3;++i){if(restore)memcpy(mapped+r[i].rva,p,r[i].size);else memcpy(p,mapped+r[i].rva,r[i].size);p+=r[i].size;}
}
__attribute__((used,noinline)) static uint64_t __cdecl hk_log_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(hk_calls==HK_MAX){fail("hud_keys synthetic call overflow");ExitProcess(1);}
    (void)regs;   /* The compared entry registers (regs) stay 0: the cdecl callees
                   * read stack arguments, and the hooked callees overwrite the
                   * forwarded ECX, so no callee observes an entry register. The
                   * trace compares the call id and its arguments. */
    hk_record *v=hk_log+hk_calls++;memset(v,0,sizeof(*v));v->id=id;
    for(unsigned j=0;j<hk_arity[id]&&j<3;++j)v->a[j]=a[j];
    if(hk_track){for(unsigned j=0;j<3;++j)v->cs[j]=regs[3+j];
        v->frame[0]=hk_entry_ebp-hk_origin;v->frame[1]=(uint32_t)(uintptr_t)a-4u-hk_origin;}
    uint32_t eax=hk_next(),edx=hk_next();hk_ecx=hk_next();
    if(id==11)eax=HK_ADDR(HK_DLG);          /* 47749c: the F11 dialog object (the null edge is PROVED) */
    if(id==17)eax=get32(mapped+0xcf99c);    /* 429820: [4CF99Ch] (EDX/ECX as the stub leaves them) */
    if((id==9||id==14)&&(hk_next()&1))eax&=0xffffff00u;   /* exercise the "key not handled" (AL=0) edges of 446c38 and the gadget offers */
    return (uint64_t)edx<<32|eax;
}
/* Pushes entry EDI/ESI/EBX and ECX/EDX/EAX for the record (regs[0..5] = EAX,
 * EDX, ECX, EBX, ESI, EDI) before the slot pointer and keeps EBP; a 49630c stub
 * additionally links the SEH record. */
#define HK_STUB(n) __attribute__((naked)) static void hk_stub_##n(void){__asm__ volatile( \
    "movl %ebp,_hk_entry_ebp\n\tpushl %edi\n\tpushl %esi\n\tpushl %ebx\n\t" \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 28(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _hk_log_impl\n\taddl $36,%esp\n\tmovl _hk_ecx,%ecx\n\tret\n\t");}
HK_STUB(1) HK_STUB(2) HK_STUB(3) HK_STUB(4) HK_STUB(5) HK_STUB(6) HK_STUB(7) HK_STUB(8) HK_STUB(9)
HK_STUB(10) HK_STUB(11) HK_STUB(12) HK_STUB(13) HK_STUB(14) HK_STUB(15) HK_STUB(16) HK_STUB(17)
#undef HK_STUB
__attribute__((naked)) static void hk_stub_0(void){__asm__ volatile(
    "movl %ebp,_hk_entry_ebp\n\tpushl %edi\n\tpushl %esi\n\tpushl %ebx\n\t"
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 28(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
    "pushl $0\n\tcalll _hk_log_impl\n\taddl $12,%esp\n\t"
    "popl %ecx\n\tmovl 4(%ecx),%ecx\n\taddl %ebp,%ecx\n\tmovl %fs:0,%eax\n\tmovl %eax,(%ecx)\n\t"
    "movw $0,16(%ecx)\n\tmovl %ecx,%fs:0\n\tpopl %edx\n\taddl $16,%esp\n\tmovl _hk_ecx,%ecx\n\tret\n\t");}
static void *hk_stubs[HK_IDS]={hk_stub_0,hk_stub_1,hk_stub_2,hk_stub_3,hk_stub_4,hk_stub_5,hk_stub_6,hk_stub_7,
    hk_stub_8,hk_stub_9,hk_stub_10,hk_stub_11,hk_stub_12,hk_stub_13,hk_stub_14,hk_stub_15,hk_stub_16,hk_stub_17};
/* Runs the original with a zeroed frame (so its one uninitialised local, the
 * mode-5/6 display request, reads 0 as the adapter's zeroed scratch does),
 * chosen entry EAX/EDX/ECX (dead: the result is AL and the saved FS:0 head)
 * and three slots; returns EDX:EAX in out[0]/out[1]. */
__attribute__((naked,noinline)) static void hk_run(void *target,const uint32_t *a,uint32_t eax,uint32_t edx,uint32_t ecx,uint32_t out[2]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $2048,%esp\n\tmovl %esp,%edi\n\txorl %eax,%eax\n\tmovl $512,%ecx\n\trep stosl\n\taddl $2048,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%eax\n\tmovl 20(%ebp),%edx\n\tmovl 24(%ebp),%ecx\n\tcall *8(%ebp)\n\t"
        "movl 28(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* The vtable of gadget idx: +0xc the key handler (stub 14), +8 the press
 * (stub 15). */
static void hk_vtable(unsigned idx){
    uint32_t vt=HK_VT+0x40u*idx;
    put32(hk_arena+(vt+0xc),(uint32_t)(uintptr_t)hk_stub_14);
    put32(hk_arena+(vt+0x8),(uint32_t)(uintptr_t)hk_stub_15);
}
/* A gadget object at HK_GAD+0x100*idx with its vtable, pointed to by [H+slot]. */
static void hk_gadget(uint32_t slot,unsigned idx){
    uint32_t gad=HK_GAD+0x100u*idx;
    hk_vtable(idx);
    put32(hk_arena+gad,HK_ADDR(HK_VT+0x40u*idx));
    put32(hk_arena+slot,HK_ADDR(gad));
}
static void hk_set_globals(void){
    put32(mapped+0x9b088,HK_ADDR(HK_H));put32(mapped+0xcf99c,HK_ADDR(HK_D));put32(mapped+0x2cbee4,HK_ADDR(HK_COUNTER));
}
/* Build a complete, safe cockpit, then a per-code variation. Codes cover the
 * whole handled set plus defaults. */
static const uint16_t hk_codes[]={0,1,2,5,0xb,0xc,0xf,0x10,0x13,0x1c,0x20,0x27,0x28,0x3b,0x40,0x41,0x42,0x43,0x44,0x45,0x57,0x220,0x30,0x100};
static uint32_t hk_build(unsigned index,unsigned n,uint32_t a[3],uint32_t r[3]){
    hk_seed=random32();random_bytes(hk_arena,HK_SIZE);for(unsigned i=0;i<3;++i)r[i]=random32();memset(a,0,12);
    hk_set_globals();
    const uint32_t H=HK_ADDR(HK_H),P=HK_ADDR(HK_WS);
    /* H gadgets (the offer loop and Tab dereference [gadget+0]); the MFD is
     * gadget 7 (needs +0xc1 mode and the button array), the F11 dialog is
     * gadget 8. */
    hk_gadget(HK_H+0x1e1,0);hk_gadget(HK_H+0x1e5,1);hk_gadget(HK_H+0x1e9,2);
    hk_gadget(HK_H+0x1f1,3);hk_gadget(HK_H+0x1f5,4);hk_gadget(HK_H+0x1f9,5);
    hk_gadget(HK_H+0x1ed,7);                                  /* MFD = gadget 7 */
    hk_vtable(8);put32(hk_arena+HK_DLG,HK_ADDR(HK_VT+0x40u*8));  /* F11 dialog object */
    hk_gadget(HK_H+0x1d8,6);put32(hk_arena+HK_H+0x1d8,random32()%2?HK_ADDR(HK_GAD+0x100u*6):0);  /* null-checked */
    put32(hk_arena+HK_H+0,HK_ADDR(HK_PANEL));                 /* [H+0] panel for 4530bc */
    put32(hk_arena+HK_H+0x203,P);                            /* player */
    put32(hk_arena+HK_WS+0x30b,random32()%2?HK_ADDR(HK_WS+0x40):0);  /* weapon (Tab) */
    put32(hk_arena+HK_WS+0x1a4,HK_ADDR(HK_WS+0x80));         /* target */
    hk_arena[HK_H+0x20f]=0;                                  /* panel on */
    put32(hk_arena+HK_D+0x14,n<64?(n%8):(random32()%8));     /* display mode 0..7 */
    put32(hk_arena+HK_MFD+0xc1,n<64?(n%6):(random32()%6));   /* MFD mode */
    for(unsigned k=0;k<10;++k)put32(hk_arena+HK_H+0x70+4*k,random32()%3?HK_ADDR(HK_ELEM+0x40*k):0);
    *(mapped+0xd2538)=(BYTE)(random32()%3);                  /* 4d2538 */
    put32(hk_arena+HK_COUNTER,random32());                   /* RTTI counter */
    if(index==ES2_HOOK_INDEX_hud_key){
        a[0]=H;
        uint32_t kc=n<sizeof(hk_codes)/sizeof(hk_codes[0])?hk_codes[n]:
            (random32()%4?hk_codes[random32()%(sizeof(hk_codes)/sizeof(hk_codes[0]))]:(uint16_t)random32());
        if(random32()%64==0)hk_arena[HK_H+0x20f]=1;          /* panel-off */
        a[1]=(random32()&0xffff0000u)|kc;                    /* high word is dead (movzx di) */
        return kc;
    }
    if(index==ES2_HOOK_INDEX_button_press){
        uint32_t idx=n<8?n:random32()%8;                     /* button index */
        a[0]=HK_ADDR(HK_BTN);a[1]=idx;a[2]=random32()&1;     /* (G, index, flag) */
        uint32_t B=HK_ADDR(HK_BTN+0x200+0x40*idx);
        put32(hk_arena+HK_BTN+0x18+4*idx,B);
        put16(hk_arena+(B-HK_BASE)+0x1b,n%3==0?2:(uint16_t)random32());   /* state (2 -> skip) */
        put32(hk_arena+(B-HK_BASE)+0x17,HK_ADDR(HK_BVT));                 /* [B+0x17] = button vtable */
        put32(hk_arena+HK_BVT+0x8,(uint32_t)(uintptr_t)hk_stub_15);       /* +8 = the press callback */
        return idx;
    }
    /* button_release(P, obj). */
    a[0]=HK_ADDR(HK_REL);a[1]=random32();
    put16(hk_arena+HK_REL+0x396,n<10?(uint16_t)n:(uint16_t)(random32()%10));   /* pending count */
    return 0;
}
static unsigned hk_hits(const callee_counter *c){
    unsigned h=0;for(unsigned j=0;j<hk_calls;++j){unsigned id=hk_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?(hk_slotkey[id]==c->key):(hk_slotkey[id]==0&&hk_addr[id]==c->key))++h;}
    return h;
}
static int hk_report(const char *what,unsigned n,const uint32_t want[3],const uint32_t got[3],unsigned calls,const hk_record *e){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x calls %u/%u\n",current_function,n,what,
        want[0],got[0],want[1],got[1],calls,hk_calls);
    for(unsigned j=0;j<calls&&j<hk_calls&&e;++j)if(memcmp(e+j,hk_log+j,sizeof(*e))){
        const uint32_t *x=(const uint32_t*)(e+j),*y=(const uint32_t*)(hk_log+j);
        for(unsigned k=0;k<sizeof(*e)/4;++k)if(x[k]!=y[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,e[j].id,k,x[k],y[k]);break;}
    for(unsigned j=0;j<HK_SIZE;++j)if(hk_after[j]!=hk_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,hk_after[j],hk_arena[j]);break;}
    return 1;
}
static int hk_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[HK_GLOBALS],after[HK_GLOBALS],actual[HK_GLOBALS];static hk_record expected[HK_MAX];
    uint32_t head=hk_fs0(),want[2],got[2];
    memcpy(hk_before,hk_arena,HK_SIZE);hk_globals(before,0);
    hk_reset();hk_run(mapped+d->rva,a,r[0],r[1],r[2],want);
    unsigned calls=hk_calls;memcpy(expected,hk_log,calls*sizeof(*expected));memcpy(hk_after,hk_arena,HK_SIZE);hk_globals(after,0);
    if(hk_fs0()!=head)return fail("hud_keys original left FS:0 changed");
    memcpy(hk_arena,hk_before,HK_SIZE);hk_globals(before,1);hk_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];for(unsigned i=0;i<count&&i<16;++i)counts[i]=c[i].count;
    uint64_t pair=x->replacement_context(a,d->user,r);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);
    hk_globals(actual,0);
    if(hk_fs0()!=head)return fail("hud_keys adapter left FS:0 changed");
    if(n<4096)for(unsigned i=0;i<count&&i<16;++i)if(c[i].count-counts[i]!=hk_hits(c+i))return fail("hud_keys callee counter delta");
    if(want[0]!=got[0]||want[1]!=got[1]||calls!=hk_calls||memcmp(expected,hk_log,calls*sizeof(*expected))||
       memcmp(hk_after,hk_arena,HK_SIZE)||memcmp(after,actual,HK_GLOBALS))
        return hk_report("direct",n,want,got,calls,expected);
    hk_globals(before,1);
    return 0;
}
static int hk_patch(BYTE saved[HK_IDS][5],int active[HK_IDS],unsigned which,int on){
    for(unsigned i=0;i<HK_IDS;++i){
        if(!hk_addr[i]){active[i]=0;continue;}
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(hk_addr[i]-0x400000u,hk_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(hk_addr[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static unsigned hk_which(unsigned index){
    /* hud_key patches every callee it makes; button_press/release patch the
     * callees they make. */
    if(index==ES2_HOOK_INDEX_hud_key)return 0x13fffu;        /* ids 0..13,16 (14,15 are arena slots) */
    if(index==ES2_HOOK_INDEX_button_press)return 1u<<3;      /* 4530bc (the +8 is an arena slot) */
    return 1u<<16;                                           /* button_release: 467828 */
}
static int hk_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6]={0},r[3],want[2],got[2],pattern=poison?0xa7a7a7a7u:0x31313131u,head=hk_fs0();hk_build(index,n,a,r);
        BYTE before[HK_GLOBALS],after[HK_GLOBALS],actual[HK_GLOBALS],efpu[108],afpu[108];static hk_record expected[HK_MAX];
        memcpy(hk_before,hk_arena,HK_SIZE);hk_globals(before,0);
        hk_reset();hk_run(mapped+d->rva,a,pattern,pattern,pattern,want);es2_hook_fpu_save(efpu);
        unsigned calls=hk_calls;memcpy(expected,hk_log,calls*sizeof(*expected));memcpy(hk_after,hk_arena,HK_SIZE);hk_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("hud_keys poison install");
        memcpy(hk_arena,hk_before,HK_SIZE);hk_globals(before,1);hk_reset();
        hk_run(mapped+d->rva,a,pattern,pattern,pattern,got);es2_hook_fpu_save(afpu);hk_globals(actual,0);
        uint32_t w3[3]={want[0],want[1],0},g3[3]={got[0],got[1],0};
        if(hk_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||calls!=hk_calls||memcmp(expected,hk_log,calls*sizeof(*expected))||
           memcmp(hk_after,hk_arena,HK_SIZE)||memcmp(after,actual,HK_GLOBALS)||memcmp(efpu,afpu,12)||memcmp(efpu+28,afpu+28,80)||h.calls!=1)
            status=hk_report(fast?"installed fast":"installed preserving",n,w3,g3,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("hud_keys poison uninstall");hk_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, EAX/EDX, trace, arena, globals, FS:0\n",d->name,total);
    return status;
}
/* Real-callee pass. hud_key/button_press over the real in-group callees: the
 * three hooks installed in the eight original/replaced combinations (bit 0
 * hud_key, bit 1 button_press, bit 2 button_release), the clock and the other
 * callees stubbed. Compared against the all-original-body run. */
static int hk_real(unsigned index){
    static const unsigned hooks[3]={ES2_HOOK_INDEX_hud_key,ES2_HOOK_INDEX_button_press,ES2_HOOK_INDEX_button_release};
    const unsigned mine=index==hooks[0]?0u:index==hooks[1]?1u:2u;
    int status=0;unsigned total=0;uint64_t entered=0,head0;
    /* Patch everything except 438d9c(2), 4530bc(3): those become real hooks. */
    BYTE saved[HK_IDS][5];int active[HK_IDS];
    if(hk_patch(saved,active,0x13ff3u,1))return 1;           /* ids 0..13,16 minus 438d9c(2), 4530bc(3) */
    for(unsigned mode=0;mode<8&&!status;++mode)for(unsigned n=0;n<256&&!status;++n){
        uint32_t a[6]={0},r[3],want[3],got[3],pattern=n&1?0xa7a7a7a7u:0x31313131u;head0=hk_fs0();
        /* Use an F1 key so hud_key reaches button_press -> button_release.
         * Set up the MFD button 0 and the panel release list the real
         * 438d9c/4530bc walk. */
        hk_build(ES2_HOOK_INDEX_hud_key,n,a,r);a[1]=(a[1]&0xffff0000u)|0x3bu;
        put32(hk_arena+HK_MFD+0x18+0,HK_ADDR(HK_BTN+0x200));
        put16(hk_arena+HK_BTN+0x200+0x1b,n%3?1:2);           /* button state (sometimes 2 = skip) */
        put32(hk_arena+HK_BTN+0x200+0x17,HK_ADDR(HK_BVT));
        put32(hk_arena+HK_BVT+0x8,(uint32_t)(uintptr_t)hk_stub_15);
        put16(hk_arena+HK_PANEL+0x396,(uint16_t)(n%6));      /* release count small */
        BYTE before[HK_GLOBALS],after[HK_GLOBALS],actual[HK_GLOBALS];static hk_record expected[HK_MAX];
        memcpy(hk_before,hk_arena,HK_SIZE);hk_globals(before,0);
        hk_reset();hk_run(mapped+0x32bc8,a,pattern,pattern,pattern,want);
        unsigned calls=hk_calls;memcpy(expected,hk_log,calls*sizeof(*expected));memcpy(hk_after,hk_arena,HK_SIZE);hk_globals(after,0);
        es2_hook hk[3];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<3;++j){o.mode=(mode>>j&1)?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(hooks[j]),&o,function_hook_extension(hooks[j])))return fail("hud_keys real install");}
        memcpy(hk_arena,hk_before,HK_SIZE);hk_globals(before,1);hk_reset();
        hk_run(mapped+0x32bc8,a,pattern,pattern,pattern,got);hk_globals(actual,0);
        uint32_t w3[3]={want[0],want[1],0},g3[3]={got[0],got[1],0};
        if(hk_fs0()!=head0||want[0]!=got[0]||want[1]!=got[1]||calls!=hk_calls||memcmp(expected,hk_log,calls*sizeof(*expected))||
           memcmp(hk_after,hk_arena,HK_SIZE)||memcmp(after,actual,HK_GLOBALS)){
            fprintf(stderr,"real mode=%u\n",mode);status=hk_report("real",n,w3,g3,calls,expected);}
        if(mode>>mine&1)entered+=hk[mine].calls;
        for(unsigned j=0;j<3;++j){if(hk[j].mismatches)status=fail("hud_keys real compare mismatch");
            if(!es2_hook_uninstall(hk+j))return fail("hud_keys real uninstall");}
        hk_globals(before,1);++total;
    }
    if(hk_patch(saved,active,0,0))return 1;
    if(!status&&!entered){fprintf(stderr,"FAIL: %s real pass: hook %u never entered\n",current_function,mine);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases (F1 over the real 438d9c/4530bc, three hooks original/replaced; %llu replaced entries), 31/a7 stack, EAX/EDX/calls/arena/globals/FS:0\n",
        current_function,total,(unsigned long long)entered);
    return status;
}
/* Calls target with the two slots over a 4-KiB fill, EAX..EDI distinct pattern
 * words; keeps the slot address in hk_origin, returns EDX:EAX in out and copies
 * the 1024 bytes below the slots after return. */
__attribute__((naked,noinline)) static void hk_track_run(void *target,const uint32_t *a,uint32_t pattern,uint32_t out[2]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 16(%ebp),%eax\n\tmovl $1024,%ecx\n\trep stosl\n\taddl $4088,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 4(%esi)\n\tpushl (%esi)\n\tmovl %esp,_hk_origin\n\tmovl 16(%ebp),%eax\n\t"
        "movl %eax,%edx\n\txorl $0x01010101,%edx\n\tmovl %eax,%ecx\n\txorl $0x02020202,%ecx\n\tmovl %eax,%ebx\n\txorl $0x04040404,%ebx\n\t"
        "movl %eax,%esi\n\txorl $0x08080808,%esi\n\tmovl %eax,%edi\n\txorl $0x10101010,%edi\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%ebx\n\tmovl %eax,(%ebx)\n\tmovl %edx,4(%ebx)\n\t"
        "leal -1024(%esp),%esi\n\tmovl $_hk_window,%edi\n\tmovl $256,%ecx\n\tcld\n\trep movsl\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* hud_key's frame probe: every callee a logging stub that also records
 * EBX/ESI/EDI and EBP/ESP relative to the caller's slots (so the ESI steps of
 * the display-mode dispatch, the caller's ESI at 49630c and the getter, and the
 * slot of the request pushed above the getter are checked); the getter 429820
 * is a logging stub here too, so its call 4 bytes below the pushed request is
 * seen. The original 432bc8 and the installed replacement (its entry shim; fast
 * and preserving dispatch; 31/a7 stack, which display modes 5..7 read as their
 * request word) must agree in the records, EDX:EAX, arena, globals, FS:0 and
 * the 1024 bytes below the slots. Codes: the direct cases' list and the keys of
 * c3_09-wingman-orders. An adapter without the entry shim fails at the
 * first call. */
static int hk_frame_probe(void){
    static const uint16_t script[]={0x218,0x22e,0x212,0x221,0x223,0x222,0x21e,0x01,0x02,0x03,0x04,0x20,0x1e,0x21,0x14,
        0x22,0x18,0x2e,0x12,0x1c,0x0f,0x2d,0x48,0x50};
    const unsigned index=ES2_HOOK_INDEX_hud_key,ncodes=sizeof(hk_codes)/sizeof(hk_codes[0]),nscript=sizeof(script)/sizeof(script[0]);
    const es2_hook_descriptor *d=function_hook_descriptor(index);
    int status=0;unsigned total=0;static hk_record expected[HK_MAX];static BYTE expected_window[1024];
    BYTE gsaved[5];if(actor_patch(0x29820,hk_stub_17,gsaved,TRUE))return 1;
    hk_track=1;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned n=0;n<1024&&!status;++n){
        uint32_t a[3],r[3],want[2],got[2],pattern=n&1?0xa7a7a7a7u:0x31313131u,head=hk_fs0();hk_build(index,n,a,r);
        const unsigned k=n>>1;a[1]=(a[1]&0xffff0000u)|(k%2?hk_codes[(k>>1)%ncodes]:script[(k>>1)%nscript]);
        BYTE before[HK_GLOBALS],after[HK_GLOBALS],actual[HK_GLOBALS];
        memcpy(hk_before,hk_arena,HK_SIZE);hk_globals(before,0);
        hk_reset();hk_track_run(mapped+d->rva,a,pattern,want);memcpy(expected_window,hk_window,1024);
        unsigned calls=hk_calls;memcpy(expected,hk_log,calls*sizeof(*expected));memcpy(hk_after,hk_arena,HK_SIZE);hk_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("hud_keys frame install");
        memcpy(hk_arena,hk_before,HK_SIZE);hk_globals(before,1);hk_reset();
        hk_track_run(mapped+d->rva,a,pattern,got);hk_globals(actual,0);
        uint32_t w3[3]={want[0],want[1],0},g3[3]={got[0],got[1],0};
        if(hk_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||calls!=hk_calls||memcmp(expected,hk_log,calls*sizeof(*expected))||
           memcmp(hk_after,hk_arena,HK_SIZE)||memcmp(after,actual,HK_GLOBALS)||h.calls!=1)
            status=hk_report(fast?"frame fast":"frame preserving",n,w3,g3,calls,expected);
        else if(memcmp(expected_window,hk_window,1024)){
            fprintf(stderr,"FAIL: hud_key case %u (frame window)\n",n);
            for(unsigned j=0;j<1024;++j)if(expected_window[j]!=hk_window[j]){fprintf(stderr," below-%u: %02x/%02x\n",1024-j,expected_window[j],hk_window[j]);break;}
            status=1;}
        if(!es2_hook_uninstall(&h))return fail("hud_keys frame uninstall");hk_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);hk_track=0;
    if(actor_patch(0x29820,NULL,gsaved,FALSE))return 1;
    if(!status)printf("PASS: hud_key %u frame cases (direct-case codes and the c3_09-wingman-orders keys, display modes 0..7), fast/preserving dispatch, 31/a7 stack, per-call EBX/ESI/EDI and EBP/ESP, FS:0, 1024 bytes below the slots\n",total);
    return status;
}
static int hud_keys_cases(unsigned index,unsigned limit){
    BYTE old_globals[HK_GLOBALS];hk_globals(old_globals,0);
    BYTE saved[HK_IDS][5];int active[HK_IDS];
    if(hk_patch(saved,active,hk_which(index),1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_HUD_KEYS_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[3],r[3];hk_build(index,n,a,r);status=hk_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);current_case_valid=0;
    if(!limit&&!status)status=hk_installed_probes(index,64);
    if(!limit&&!status&&index==ES2_HOOK_INDEX_hud_key)status=hk_frame_probe();
    if(hk_patch(saved,active,hk_which(index),0))return 1;
    if(!limit&&!status)status=hk_real(index);
    hk_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; EAX/EDX, ordered calls with entry registers, arena, globals, FS:0\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
