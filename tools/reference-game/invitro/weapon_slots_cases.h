/* weapon_slots group (weapons.md, "player weapon system"): weapon_frame 410b40,
 * ready_step 410a3c, selected_ready 410a04, chain_ready 410970, latch_set
 * 410f04, link_toggle 410f14, member_toggle 410cd0, gauge_sync 4420c8,
 * gauge_state 442120, gauge_link 442054, gauge_button 442154 and gauge_mode
 * 441f90. One arena holds a weapon system W with two slot lists (index -8..31,
 * the same empty slots, every partner slot i + definition byte filled), six
 * weapons on two vtables with definitions and carriers, four group tables, the
 * range gauge H with four buttons, the HUD root X ([49B088h] = X, [X+1E1h] = H,
 * [[X+203h]+202h] = W) and the gauge copy. Direct cases run the original body
 * and the core against logging stubs for the hook's game callees (the functions
 * patched in the private image, the weapon virtuals +2Ch/+50h/+60h through the
 * arena vtables; chain_ready's own recursion runs real): ordered arguments, the
 * bridge's slot count, entry EAX/EDX/ECX and EBX/ESI/EDI. The stubs return edge
 * values and change what the originals read again (W+31h, vtables, link bytes,
 * the list pointer, the count, the selection, H+B4h). EAX, EDX, ECX (from the
 * exit-shim value), the call log, the arena and the globals
 * 49b088/4d1a5c/4d2520.. are compared. The original's gauge record is a stack
 * local: where the record's address reaches a stub (4420c8's slot and EDX,
 * 441fa4's ESI and the EDX derived from it) the stubs log the record's
 * contents, and stack-range values compare by their low byte. Then installed
 * 31/a7 probes through the shims, and real-callee passes: weapon_frame over the
 * real 410a3c, 410a04, 410970, 442120, 410f04, 410f14, 4420c8, 442054, 441f90
 * and the hooks select_step, fire_group, weapon_select (thirteen
 * hooks in random original/replaced modes); gauge_button over gauge_mode,
 * gauge_link and link_toggle; the weapon_system group_toggle 4110ac over
 * member_toggle. Stubs stay for the virtuals, weapon_range 40e5f8 and the lamp
 * 441fa4 (H3). */
typedef struct wsl_record { uint32_t id,args[4],regs[6],slots,extra; } wsl_record;
enum { WSL_SIZE=0x1200, WSL_W=0x100, WSL_LIST=0x300, WSL_LIST2=0x3a0, WSL_T=0x450, WSL_TSTRIDE=0x60, WSL_E=0x600,
    WSL_D=0x900, WSL_C=0x9c0, WSL_VT1=0xc00, WSL_VT2=0xc80, WSL_H=0xd00, WSL_X=0xdc0, WSL_P=0xfd0, WSL_COPY=0x11e0,
    WSL_REC=0x11f0, WSL_WEAPONS=6, WSL_SLOTS=24, WSL_MAX=512, WSL_IDS=24, WSL_PATCHED=14, WSL_GLOBALS=28 };
#define WSL_BASE ((uint32_t)(uintptr_t)wsl_arena)
#define WSL_ADDR(o) (WSL_BASE+(uint32_t)(o))
static BYTE wsl_arena[WSL_SIZE],wsl_before[WSL_SIZE],wsl_after[WSL_SIZE];
static wsl_record wsl_log[WSL_MAX];
static unsigned wsl_calls;
static int wsl_real;                 /* real-callee pass: only side effects that keep select_step's domain */
/* Real pass with fire_group replaced: its return ECX is the framework's entry
 * ECX, not the original's (dead, weapons.md, "weapon-system routines below the
 * weapon keys"); it reaches the lamp 441fa4 and weapon_frame's return
 * through 4420c8. */
static int wsl_fire_replaced;
static uint32_t wsl_seed,wsl_stream;
__attribute__((used)) static uint32_t wsl_ecx;
/* 0..13 patched: 41074c 410a3c 442120 410ae4 410f04 410f14 4420c8 410708
 * 410970 410a04 40e5f8 441f90 441fa4 442054; 14..23 the vtable slots
 * +2Ch, +60h, +50h, +48h, +3Ch of VT1/VT2. */
static const uint32_t wsl_addresses[WSL_PATCHED]={0x41074c,0x410a3c,0x442120,0x410ae4,0x410f04,0x410f14,0x4420c8,0x410708,
    0x410970,0x410a04,0x40e5f8,0x441f90,0x441fa4,0x442054};
static const uint32_t wsl_slot_of[WSL_IDS]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x2c,0x2c,0x60,0x60,0x50,0x50,0x48,0x48,0x3c,0x3c};
static const unsigned wsl_counts[WSL_IDS]={2,1,1,2,1,1,2,2,4,3,2,2,1,2,1,1,2,2,4,4,1,1,1,1};
static uint32_t wsl_next(void){
    uint32_t z=wsl_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static void wsl_reset(void){wsl_calls=0;wsl_stream=wsl_seed;}
static int wsl_inside(uint32_t a,uint32_t n){return a-WSL_BASE<=(uint32_t)WSL_SIZE-n;}
static BYTE *wsl_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static uint32_t wsl_e(unsigned k){return WSL_ADDR(WSL_E+0x80u*k);}
static uint32_t wsl_table(unsigned k){return WSL_ADDR(WSL_T+WSL_TSTRIDE*k);}
static BYTE *wsl_w(void){return wsl_arena+WSL_W;}
static void wsl_globals(BYTE *out,int restore){
    if(restore){memcpy(mapped+0x9b088,out,4);memcpy(mapped+0xd1a5c,out+4,8);memcpy(mapped+0xd2520,out+12,16);}
    else {memcpy(out,mapped+0x9b088,4);memcpy(out+4,mapped+0xd1a5c,8);memcpy(out+12,mapped+0xd2520,16);}
}
/* A value in this thread's stack range (the original's gauge record and
 * the adapter's scratch both live there). The range is the whole stack
 * reservation, fixed once from the allocation that holds the current stack
 * pointer, so a stack value is recognised the same way before, during and
 * after the thread's stack has grown (the committed limit moves; the
 * reservation does not). */
static int wsl_stackish(uint32_t v){
    static uint32_t low,top;
    if(!top){
        MEMORY_BASIC_INFORMATION mbi;volatile char here=0;
        const uint32_t base=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;
        if(!VirtualQuery((const void *)&here,&mbi,sizeof(mbi))||(uint32_t)(uintptr_t)mbi.AllocationBase>=base){
            fail("weapon_slots stack range");ExitProcess(1);}
        low=(uint32_t)(uintptr_t)mbi.AllocationBase;top=base;
    }
    return v-low<top-low;
}
/* gauge_sync (ESI) and weapon_frame (the record) read a stack value's contents, which differ
 * per side for a data word; wsl_alias guards that none reaches them. */
static uint32_t wsl_alias_value,wsl_alias_call;static int wsl_alias_hit;
static uint32_t wsl_normal(uint32_t v){return wsl_stackish(v)?0x5ac00000u|(v&0xffu):v;}
/* A readable dword in the committed part of this thread's stack. */
static int wsl_committed(uint32_t v){
    const NT_TIB *tib=(const NT_TIB *)NtCurrentTeb();
    return v-(uint32_t)(uintptr_t)tib->StackLimit<=(uint32_t)((uintptr_t)tib->StackBase-(uintptr_t)tib->StackLimit)-8u;
}
/* The weapon in slot i of the current list (the list is always one of
 * the two arena lists). */
static uint32_t wsl_slot(int i){return get32(wsl_host(get32(wsl_w()+4)+4u*(uint32_t)i));}
static int16_t wsl_count(void){return (int16_t)get16(wsl_w()+8);}
/* A selection the originals can follow: -1 or a filled slot below the
 * count. */
static void wsl_select(uint32_t pick){
    const int16_t count=wsl_count();
    if(pick%8==0||count<=0){wsl_w()[0x1d]=0xff;return;}
    for(unsigned k=0;k<WSL_SLOTS;++k){int i=(int)((pick/8+k)%(unsigned)count);if(i<WSL_SLOTS&&wsl_slot(i)){wsl_w()[0x1d]=(BYTE)i;return;}}
    wsl_w()[0x1d]=0xff;
}
static void wsl_swap_list(void){
    put32(wsl_w()+4,get32(wsl_w()+4)==WSL_ADDR(WSL_LIST)?WSL_ADDR(WSL_LIST2):WSL_ADDR(WSL_LIST));
}
static void wsl_swap_vtable(uint32_t e,uint32_t pick){
    if(wsl_inside(e,4)&&e-WSL_ADDR(WSL_E)<0x80u*WSL_WEAPONS&&!((e-WSL_ADDR(WSL_E))&0x7f))
        put32(wsl_host(e),pick&1?WSL_ADDR(WSL_VT2):WSL_ADDR(WSL_VT1));
}
static void wsl_flip_link(uint32_t pick){wsl_arena[WSL_E+0x80u*(pick%WSL_WEAPONS)+0x4b]^=1;}
/* Side effects of the stubs in direct passes: reread state. */
static void wsl_side_effect(unsigned id,const uint32_t *a,uint32_t pick){
    if(wsl_real){
        /* Real callees: keep the count, the list and the selection. */
        if(id>=14&&pick%4==0)wsl_flip_link(pick>>2);
        if(id>=14&&id<16&&pick%8==1)put32(wsl_w()+0x31,pick%3?0:stack_clean(wsl_next()));
        if(id>=14&&id<18&&pick%8==2)wsl_swap_vtable(a[0],pick>>3);
        if(id==12&&pick%2)wsl_arena[WSL_H+0xb4]^=1;
        return;
    }
    switch(id){
    case 0:case 1:case 7:if(pick%2)wsl_select(pick>>1);break;
    case 3:if(pick%4==0)wsl_w()[0x1c]=(BYTE)(pick/4%3);if(pick%4==1)wsl_select(pick>>2);break;
    case 6:case 18:case 19:
        switch(pick%8){
        case 0:put16(wsl_w()+8,(uint16_t)(pick/8%(WSL_SLOTS+1)));break;
        case 1:wsl_swap_list();break;
        case 2:wsl_w()[0x1c]=(BYTE)(pick/8%3);break;
        case 3:wsl_select(pick>>3);break;
        case 4:wsl_flip_link(pick>>3);break;
        case 5:put16(wsl_host(wsl_table(pick/8%4)+2u*(pick/32%WSL_SLOTS)),(uint16_t)wsl_next());break;
        default:break;
        }
        break;
    case 9:if(pick%4==0)wsl_swap_list();break;
    case 14:case 15:
        if(pick%8==1)put32(wsl_w()+0x31,pick%3?0:stack_clean(wsl_next()));
        if(pick%8==2)wsl_swap_vtable(a[0],pick>>3);
        if(pick%8==3)wsl_swap_list();
        if(pick%8==4)wsl_flip_link(pick>>3);
        break;
    case 16:case 17:if(pick%8==1)wsl_swap_list();if(pick%8==2)wsl_flip_link(pick>>3);break;
    case 12:if(pick%2)wsl_arena[WSL_H+0xb4]^=1;break;
    default:break;
    }
}
__attribute__((used,noinline)) static uint64_t __cdecl wsl_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(wsl_calls==WSL_MAX){fail("weapon_slots synthetic call overflow");ExitProcess(1);}
    uint32_t eax=stack_clean(wsl_next()),edx=stack_clean(wsl_next()),pick=wsl_next();wsl_ecx=stack_clean(wsl_next());
    wsl_record *v=wsl_log+wsl_calls++;memset(v,0,sizeof(*v));v->id=id;
    for(unsigned k=0;k<wsl_counts[id];++k)v->args[k]=a[k];
    for(unsigned k=0;k<6;++k)v->regs[k]=wsl_normal(regs[k]);
    if(!wsl_alias_hit&&(id==6||id==12)&&stack_uncommitted(id==6?a[1]:regs[4])){wsl_alias_hit=1;wsl_alias_value=id==6?a[1]:regs[4];wsl_alias_call=wsl_calls-1u;}
    v->slots=weapons_bridge_slots(a,wsl_counts[id]);
    if(id==6&&!wsl_inside(a[1],8)&&wsl_committed(a[1])){
        /* weapon_frame's record: its contents, and EDX = its address. */
        v->args[1]=get32(wsl_host(a[1]));v->extra=wsl_host(a[1])[4]|(regs[1]==a[1]?0x100u:0);v->regs[1]=0;
    }
    /* gauge_sync's ESI = the record: its contents. */
    if(id==12&&wsl_stackish(regs[4]))v->regs[4]=wsl_committed(regs[4])?get32(wsl_host(regs[4])):0x5ac0ffffu;
    if(id==12&&wsl_fire_replaced)v->regs[2]=0;
    /* V48/V3C come only from group_toggle 4110ac (member_toggle's real pass),
     * whose replaced bridge does not load EBX/ESI/EDI. */
    if(id>=20)v->regs[3]=v->regs[4]=v->regs[5]=0;
    /* Real passes: select_step's return ECX (high word = its local `found`)
     * reaches 410970's index slot through 410a3c and 410a04 (`mov cx`), hence
     * the ECX and EDI of the weapon calls below 410970 and the recursion's
     * index (EDX at its V2C); a replaced select_step returns its entry ECX
     * (dead, weapons.md, "player weapon system"), so only the low words compare
     * there. */
    if(wsl_real&&(id==10||(id>=14&&id<18))){v->regs[2]&=0xffffu;v->regs[5]&=0xffffu;if(id>=14&&id<16)v->regs[1]&=0xffffu;}
    switch(id){
    case 2:eax=pick%4==0?(uint32_t)(uintptr_t)(mapped+0xd1a5c):WSL_ADDR(WSL_COPY);break;
    case 9:case 10:case 14:case 15:if(pick%2)eax&=0xffff0000u;else if(!(uint16_t)eax)eax|=1u;break;
    case 16:case 17:{
        static const uint16_t values[]={5,3,0,1,2,4,0xffff,7,0xfffb,40};
        eax=(eax&0xffff0000u)|values[pick%10];break;}
    case 20:case 21:if(pick%2)eax&=0xffff0000u;break;
    default:break;
    }
    wsl_side_effect(id,a,pick>>4);
    return (uint64_t)edx<<32|eax;
}
/* EAX, EDX, ECX, EBX, ESI, EDI pushed below the return address; the
 * callee-saved registers are restored, ECX comes from the stream. */
#define WSL_STUB(n) __attribute__((naked)) static void wsl_stub_##n(void){__asm__ volatile( \
    "pushl %edi\n\tpushl %esi\n\tpushl %ebx\n\tpushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 28(%esp),%eax\n\tpushl %eax\n\t" \
    "leal 4(%esp),%eax\n\tpushl %eax\n\tpushl $" #n "\n\tcalll _wsl_stub_impl\n\taddl $12,%esp\n\t" \
    "popl %ecx\n\tpopl %ecx\n\tpopl %ecx\n\tpopl %ebx\n\tpopl %esi\n\tpopl %edi\n\tmovl _wsl_ecx,%ecx\n\tret\n\t");}
WSL_STUB(0) WSL_STUB(1) WSL_STUB(2) WSL_STUB(3) WSL_STUB(4) WSL_STUB(5) WSL_STUB(6) WSL_STUB(7) WSL_STUB(8) WSL_STUB(9)
WSL_STUB(10) WSL_STUB(11) WSL_STUB(12) WSL_STUB(13) WSL_STUB(14) WSL_STUB(15) WSL_STUB(16) WSL_STUB(17) WSL_STUB(18)
WSL_STUB(19) WSL_STUB(20) WSL_STUB(21) WSL_STUB(22) WSL_STUB(23)
#undef WSL_STUB
static void *wsl_stubs[WSL_IDS]={wsl_stub_0,wsl_stub_1,wsl_stub_2,wsl_stub_3,wsl_stub_4,wsl_stub_5,wsl_stub_6,wsl_stub_7,
    wsl_stub_8,wsl_stub_9,wsl_stub_10,wsl_stub_11,wsl_stub_12,wsl_stub_13,wsl_stub_14,wsl_stub_15,wsl_stub_16,wsl_stub_17,
    wsl_stub_18,wsl_stub_19,wsl_stub_20,wsl_stub_21,wsl_stub_22,wsl_stub_23};
/* Calls a target with n slots and chosen EAX/EDX/ECX/EBX/ESI/EDI; returns
 * EAX, EDX and ECX. With a nonzero pattern the stack below is filled with
 * it first (4 KiB). */
__attribute__((naked)) static void wsl_machine(void *target,const uint32_t *a,unsigned n,const uint32_t r[6],uint32_t out[3],
    uint32_t pattern){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl 28(%ebp),%eax\n\ttestl %eax,%eax\n\tjz 3f\n\tsubl $4096,%esp\n\tmovl %esp,%edi\n\tmovl $1024,%ecx\n\trep stosl\n\taddl $3840,%esp\n\t"
        "3: movl 12(%ebp),%esi\n\tmovl 16(%ebp),%ecx\n\ttestl %ecx,%ecx\n\tjz 2f\n\t1: decl %ecx\n\tpushl (%esi,%ecx,4)\n\ttestl %ecx,%ecx\n\tjnz 1b\n\t"
        "2: movl 20(%ebp),%eax\n\tmovl 4(%eax),%edx\n\tmovl 8(%eax),%ecx\n\tmovl 12(%eax),%ebx\n\tmovl 16(%eax),%esi\n\tmovl 20(%eax),%edi\n\t"
        "movl (%eax),%eax\n\tcall *8(%ebp)\n\t"
        "movl 24(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* Fills empty slots below WSL_SLOTS that a listed weapon names as its
 * partner (slot + definition byte), in both lists, until no new one is
 * needed; empty slots stay empty in both lists or in neither. */
static void wsl_fill_partners(void){
    for(int changed=1;changed;){
        changed=0;
        for(unsigned l=0;l<2;++l){BYTE *list=wsl_arena+(l?WSL_LIST2:WSL_LIST);
            for(int i=0;i<WSL_SLOTS;++i){
                uint32_t e=get32(list+4*i);if(!e)continue;
                int j=i+(int8_t)wsl_host(get32(wsl_host(e+0x18)))[0x16];
                if(j<-8||j>31){fail("weapon_slots partner range");ExitProcess(1);}
                if(!get32(wsl_arena+WSL_LIST+4*j)||!get32(wsl_arena+WSL_LIST2+4*j)){
                    if(!get32(wsl_arena+WSL_LIST+4*j))put32(wsl_arena+WSL_LIST+4*j,wsl_e(random32()%WSL_WEAPONS));
                    if(!get32(wsl_arena+WSL_LIST2+4*j))put32(wsl_arena+WSL_LIST2+4*j,wsl_e(random32()%WSL_WEAPONS));
                    changed=1;
                }
            }
        }
    }
}
/* One state for hook `index`: a = the slots, r = EAX/EDX/ECX/EBX/ESI/EDI. */
static void wsl_build(unsigned index,uint32_t a[4],uint32_t r[6]){
    wsl_seed=random32();random_bytes(wsl_arena,WSL_SIZE);stack_clean_bytes(wsl_arena,WSL_SIZE);
    /* Random registers outside the stack range (stack values are the
     * record's and compare by contents or low byte). A draw that lands in
     * the stack is replaced by a fixed value, not redrawn: the number of
     * draws, and with it every later case, must not depend on where this
     * run's stack happens to sit (ASLR). */
    for(unsigned i=0;i<6;++i){r[i]=random32();if(wsl_stackish(r[i]))r[i]=0;}
    a[0]=a[1]=a[2]=a[3]=0;
    for(unsigned v=0;v<2;++v){BYTE *t=wsl_arena+(v?WSL_VT2:WSL_VT1);
        put32(t+0x2c,(uint32_t)(uintptr_t)wsl_stubs[14+v]);put32(t+0x60,(uint32_t)(uintptr_t)wsl_stubs[16+v]);
        put32(t+0x50,(uint32_t)(uintptr_t)wsl_stubs[18+v]);put32(t+0x48,(uint32_t)(uintptr_t)wsl_stubs[20+v]);
        put32(t+0x3c,(uint32_t)(uintptr_t)wsl_stubs[22+v]);}
    static const BYTE flags[]={0,1,0xff,0x80};
    static const int8_t links[]={0,0,1,-1,2,-2,3,-4,4};
    static const uint16_t words[]={5,6,0x8000,5};
    static const uint32_t lives[]={1,0,0xffffffffu,0x7fffffffu,0x80000000u,2};
    for(unsigned k=0;k<WSL_WEAPONS;++k){
        BYTE *p=wsl_arena+WSL_E+0x80u*k,*d=wsl_arena+WSL_D+0x20u*k,*c=wsl_arena+WSL_C+0x60u*k;
        put32(p,random32()%2?WSL_ADDR(WSL_VT1):WSL_ADDR(WSL_VT2));put32(p+0x18,WSL_ADDR(WSL_D+0x20u*k));
        put32(p+0x1c,WSL_ADDR(WSL_C+0x60u*k));
        p[0x49]=random32()%2?0:flags[random32()%4];p[0x4b]=random32()%2?0:flags[random32()%4];p[0x4c]=random32()%3?flags[1+random32()%3]:0;
        d[0x16]=(BYTE)links[random32()%9];d[7]=(BYTE)(random32()%6);
        put16(c+0x56,random32()%4?words[random32()%4]:(uint16_t)random32());put32(c+0x30,lives[random32()%6]);
    }
    /* Lists: slots outside 0..23 always filled; inside, the same slots
     * empty in both lists. */
    for(int i=-8;i<32;++i){
        const int empty=i>=0&&i<WSL_SLOTS&&random32()%4==0;
        put32(wsl_arena+WSL_LIST+4*i,empty?0:wsl_e(random32()%WSL_WEAPONS));
        put32(wsl_arena+WSL_LIST2+4*i,empty?0:wsl_e(random32()%WSL_WEAPONS));
    }
    wsl_fill_partners();
    for(unsigned k=0;k<4;++k)for(int i=-8;i<32;++i){
        static const uint16_t members[]={1,0,0x8000,0xffff,0x100};
        put16(wsl_host(wsl_table(k)+2u*(uint32_t)i),random32()%2?0:random32()%4?members[random32()%5]:(uint16_t)random32());
    }
    BYTE *w=wsl_w();
    put32(w+4,random32()%4?WSL_ADDR(WSL_LIST):WSL_ADDR(WSL_LIST2));
    static const uint16_t counts[]={0,1,2,3,6,12,24,0xffff,0x8000};
    put16(w+8,random32()%2?(uint16_t)(1u+random32()%WSL_SLOTS):counts[random32()%9]);
    w[0x1c]=(BYTE)(random32()%3);
    for(unsigned g=0;g<3;++g)put32(w+0x1f+6*g,wsl_table(random32()%4));
    wsl_select(random32());
    w[0x14]=flags[random32()%4];w[0x16]=random32()%2?0:flags[random32()%4];w[0x18]=random32()%2?0:flags[random32()%4];
    put32(w+0x31,random32()%2?0:random32());
    /* The gauge: mode word 0..2 (or another), lamp and link bytes, four
     * buttons, the count 4. */
    BYTE *h=wsl_arena+WSL_H;
    static const uint16_t modes[]={0,1,2,2,0xffff,3};
    put16(h+0xb1,random32()%4?modes[random32()%6]:(uint16_t)random32());
    h[0xb3]=random32()%2?0:flags[random32()%4];h[0xb4]=random32()%2?0:flags[random32()%4];
    put32(h+0x68,random32()%8?4:(uint32_t)((int)(random32()%8)-1));
    put32(wsl_arena+WSL_X+0x1e1,WSL_ADDR(WSL_H));put32(wsl_arena+WSL_X+0x203,WSL_ADDR(WSL_P));put32(wsl_arena+WSL_P+0x202,WSL_ADDR(WSL_W));
    /* The gauge copy (stub 442120 returns it or 4d1a5c): record bytes 2/3
     * zero or not; a mode word the real fire_group can follow. */
    BYTE *copy=wsl_arena+WSL_COPY;
    put16(copy,random32()%4?(uint16_t)(random32()%3):(uint16_t)(random32()%3)|((uint16_t)random32()&0xff00u));
    copy[2]=random32()%2?0:flags[random32()%4];copy[3]=random32()%2?0:flags[random32()%4];
    random_bytes(mapped+0xd1a5c,8);put16(mapped+0xd1a5c,(uint16_t)(random32()%3));
    put32(mapped+0x9b088,WSL_ADDR(WSL_X));
    put16(mapped+0xd2528,random32()%2?0:(uint16_t)random32());put16(mapped+0xd2526,random32()%2?0:(uint16_t)random32());
    a[0]=WSL_ADDR(WSL_W);
    switch(index){
    case ES2_HOOK_INDEX_weapon_frame:a[1]=stack_clean(random32());break;
    case ES2_HOOK_INDEX_selected_ready:
        a[1]=random32()%8?wsl_e(random32()%WSL_WEAPONS):stack_clean(random32());a[2]=stack_clean(random32());break;
    case ES2_HOOK_INDEX_chain_ready:{
        /* E = a filled slot's weapon (its partner slot is filled). */
        int i=(int)(random32()%WSL_SLOTS);
        for(unsigned k=0;k<WSL_SLOTS&&!wsl_slot(i);++k)i=(i+1)%WSL_SLOTS;
        if(!wsl_slot(i)){fail("weapon_slots no filled slot");ExitProcess(1);}
        a[1]=wsl_slot(i);a[2]=(random32()%2?random32()&0xffff0000u:0)|(uint32_t)i;
        static const uint32_t fs[]={0,0,1,0x10000,0xffff};a[3]=random32()%4?fs[random32()%5]:random32();break;}
    case ES2_HOOK_INDEX_link_toggle:{
        if(random32()%8==0){static const BYTE odd[]={0x80,0xfe,0xff};w[0x1d]=odd[random32()%3];}
        /* An empty partner slot (410f14 tests it; the other hooks need it
         * filled). */
        const int sel=(int8_t)w[0x1d];
        if(sel>=0&&random32()%3==0){
            const int j=sel+(int8_t)wsl_host(get32(wsl_host(wsl_slot(sel)+0x18)))[0x16];
            if(j!=sel)put32(wsl_host(get32(w+4)+4u*(uint32_t)j),0);
        }
        break;}
    case ES2_HOOK_INDEX_member_toggle:
        a[1]=(random32()%2?random32()&0xffff0000u:0)|(uint16_t)(int16_t)((int)(random32()%40u)-8);break;
    case ES2_HOOK_INDEX_gauge_sync:{
        BYTE *rec=wsl_arena+WSL_REC;a[0]=WSL_ADDR(WSL_H);a[1]=WSL_ADDR(WSL_REC);
        if(random32()%2)put16(rec,get16(h+0xb1));
        if(random32()%2)rec[2]=h[0xb3];
        if(random32()%2)rec[3]=h[0xb4];
        break;}
    case ES2_HOOK_INDEX_gauge_state:a[0]=WSL_ADDR(WSL_H);break;
    case ES2_HOOK_INDEX_gauge_link:a[0]=WSL_ADDR(WSL_H);a[1]=random32()%2?random32():random32()%2;break;
    case ES2_HOOK_INDEX_gauge_button:
        a[0]=WSL_ADDR(WSL_H);for(unsigned k=0;k<4;++k)put32(h+0x18+4*k,WSL_ADDR(0x1100+0x20*k));
        a[1]=random32()%5?WSL_ADDR(0x1100+0x20*(random32()%4)):random32();break;
    case ES2_HOOK_INDEX_gauge_mode:a[0]=WSL_ADDR(WSL_H);a[1]=random32();break;
    default:break;
    }
}
static int wsl_shimmed(unsigned index){return index!=ES2_HOOK_INDEX_latch_set&&index!=ES2_HOOK_INDEX_gauge_mode;}
static int wsl_report(const char *what,unsigned n,const uint32_t want[3],const uint32_t got[3],unsigned calls,const wsl_record *expected,
    const BYTE *globals_want,const BYTE *globals_got){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x calls %u/%u\n",current_function,n,what,
        want[0],got[0],want[1],got[1],want[2],got[2],calls,wsl_calls);
    for(unsigned j=0;j<calls&&j<wsl_calls&&expected;++j)if(memcmp(expected+j,wsl_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(wsl_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        for(unsigned i=0;i<=j;++i){const uint32_t *q=(const uint32_t *)(expected+i);fprintf(stderr,"  log%u:",i);
            for(unsigned k=0;k<sizeof(*expected)/4;++k)fprintf(stderr," %08x",q[k]);fprintf(stderr,"\n");}
        break;
    }
    for(unsigned j=0;j<WSL_SIZE;++j)if(wsl_after[j]!=wsl_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,wsl_after[j],wsl_arena[j]);break;}
    for(unsigned j=0;j<WSL_GLOBALS&&globals_want;++j)if(globals_want[j]!=globals_got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,globals_want[j],globals_got[j]);break;}
    return 1;
}
/* Expected counter hits from the log; chain_ready counts only its own
 * calls (those before the recursion's V2C) and the recursion itself. */
static unsigned wsl_expected_hits(unsigned index,const callee_counter *c){
    unsigned hits=0,end=wsl_calls,v2c=0;
    if(index==ES2_HOOK_INDEX_chain_ready)
        for(unsigned j=0;j<wsl_calls;++j)if(wsl_log[j].id==14||wsl_log[j].id==15){if(++v2c==2){end=j;break;}}
    /* Integration mode: the recursion enters the installed hook, whose
     * calls count as well. */
    if(index==ES2_HOOK_INDEX_chain_ready&&mapped[0x10970]!=0x55)end=wsl_calls;
    if(index==ES2_HOOK_INDEX_chain_ready&&c->event==ES2_COLLIDE_CALL&&c->key==0x410970)return v2c==2;
    for(unsigned j=0;j<end;++j){unsigned id=wsl_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?wsl_slot_of[id]==c->key:id<WSL_PATCHED&&wsl_addresses[id]==c->key)++hits;}
    return hits;
}
/* The pair of a weapon_frame run: EDX may hold the record address's high
 * bytes (4420c8's EDX when the loop does not run). */
static void wsl_normalize(unsigned index,uint32_t v[3]){
    if(index==ES2_HOOK_INDEX_weapon_frame)v[1]=wsl_normal(v[1]);
}
static int wsl_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    static wsl_record expected[WSL_MAX];BYTE before[WSL_GLOBALS],after[WSL_GLOBALS],actual[WSL_GLOBALS];
    memcpy(wsl_before,wsl_arena,WSL_SIZE);wsl_globals(before,0);wsl_alias_hit=0;
    uint32_t want[3],got[3];wsl_reset();wsl_machine(mapped+d->rva,a,d->arg_count,r,want,0);
    unsigned calls=wsl_calls;memcpy(expected,wsl_log,calls*sizeof(*expected));memcpy(wsl_after,wsl_arena,WSL_SIZE);wsl_globals(after,0);
    memcpy(wsl_arena,wsl_before,WSL_SIZE);wsl_globals(before,1);wsl_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[12]={0};
    for(unsigned i=0;i<count&&i<12;++i)counts[i]=c[i].count;
    weapon_slots_ebx=r[3];weapon_slots_esi=r[4];weapon_slots_edi=r[5];weapon_slots_ecx_valid=0;
    uint64_t pair=x->replacement_context(a,d->user,r);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);
    if(wsl_alias_hit){fprintf(stderr,"FAIL: %s case %u call %u: data word %08x lies in the worker stack reservation\n",current_function,n,wsl_alias_call,wsl_alias_value);return 1;}
    if(wsl_shimmed(index)){
        if(!weapon_slots_ecx_valid)return fail("weapon_slots exit ECX not set");
        got[2]=weapon_slots_ecx;
    } else {
        if(weapon_slots_ecx_valid)return fail("weapon_slots exit ECX set by an unshimmed hook");
        got[2]=r[2];
    }
    weapon_slots_ecx_valid=0;wsl_globals(actual,0);
    wsl_normalize(index,want);wsl_normalize(index,got);
    if(n<4096)for(unsigned i=0;i<count&&i<12;++i)if(c[i].count-counts[i]!=wsl_expected_hits(index,c+i))
        return fail("weapon_slots callee counter delta");
    if(memcmp(want,got,sizeof(want))||calls!=wsl_calls||memcmp(expected,wsl_log,calls*sizeof(*expected))||memcmp(wsl_after,wsl_arena,WSL_SIZE)||
       memcmp(after,actual,WSL_GLOBALS))
        return wsl_report("direct",n,want,got,calls,expected,after,actual);
    return 0;
}
/* The patched callees of each hook's direct cases (bit = stub id). */
static unsigned wsl_which(unsigned index){
    switch(index){
    case ES2_HOOK_INDEX_weapon_frame:return 0x1ffu;
    case ES2_HOOK_INDEX_ready_step:return 1u<<9|1u<<0|1u<<7;
    case ES2_HOOK_INDEX_selected_ready:return 1u<<8;
    case ES2_HOOK_INDEX_chain_ready:return 1u<<10;
    case ES2_HOOK_INDEX_gauge_sync:case ES2_HOOK_INDEX_gauge_button:return 1u<<11|1u<<12|1u<<13;
    case ES2_HOOK_INDEX_gauge_link:return 1u<<5;
    default:return 0;
    }
}
static int wsl_patch(BYTE saved[WSL_PATCHED][5],unsigned which,int on){
    for(unsigned i=0;i<WSL_PATCHED;++i)if(which>>i&1&&actor_patch(wsl_addresses[i]-0x400000u,on?wsl_stubs[i]:NULL,saved[i],on?TRUE:FALSE))return 1;
    return 0;
}
static es2_hook_options wsl_options(es2_hook_mode mode){
    es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=mode;return o;
}
/* Installed probes: the original from a 31/a7 stack against the installed
 * hook (through its exit shim); EAX, EDX, ECX, the trace, arena, globals. */
static int wsl_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[4],r[6],want[3],got[3],pattern=poison?0xa7a7a7a7u:0x31313131u;wsl_build(index,a,r);
        BYTE before[WSL_GLOBALS],after[WSL_GLOBALS],actual[WSL_GLOBALS],expected_fpu[108],actual_fpu[108];static wsl_record expected[WSL_MAX];
        memcpy(wsl_before,wsl_arena,WSL_SIZE);wsl_globals(before,0);
        wsl_reset();wsl_machine(mapped+d->rva,a,d->arg_count,r,want,pattern);es2_hook_fpu_save(expected_fpu);
        unsigned calls=wsl_calls;memcpy(expected,wsl_log,calls*sizeof(*expected));memcpy(wsl_after,wsl_arena,WSL_SIZE);wsl_globals(after,0);
        es2_hook h={0};es2_hook_options o=wsl_options(ES2_HOOK_REPLACED);
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("weapon_slots poison install");
        memcpy(wsl_arena,wsl_before,WSL_SIZE);wsl_globals(before,1);wsl_reset();
        wsl_machine(mapped+d->rva,a,d->arg_count,r,got,pattern);es2_hook_fpu_save(actual_fpu);wsl_globals(actual,0);
        wsl_normalize(index,want);wsl_normalize(index,got);
        /* chain_ready's recursion enters the installed hook again. */
        unsigned entries=1;
        if(index==ES2_HOOK_INDEX_chain_ready){entries=0;for(unsigned j=0;j<calls;++j)entries+=expected[j].id==14||expected[j].id==15;}
        if(memcmp(want,got,sizeof(want))||calls!=wsl_calls||memcmp(expected,wsl_log,calls*sizeof(*expected))||memcmp(wsl_after,wsl_arena,WSL_SIZE)||
           memcmp(after,actual,WSL_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=entries||
           weapon_slots_ecx_valid)
            status=wsl_report(fast?"installed fast":"installed preserving",n,want,got,calls,expected,after,actual);
        if(!es2_hook_uninstall(&h))return fail("weapon_slots poison uninstall");wsl_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, EAX/EDX/ECX%s, trace with EBX/ESI/EDI, arena, globals\n",
        d->name,total,wsl_shimmed(index)?" (exit shim)":"");
    return status;
}
/* A real-callee pass: `outer` called from a 31/a7 stack over real callees
 * with the hooks `hooks` in the modes of a random mask per case (cases 0
 * and 1 all original / all replaced), against the all-original run. */
static int wsl_real_pass(const char *what,unsigned outer,const unsigned *hooks,unsigned hook_count,unsigned cases,
    void (*prepare)(uint32_t a[4])){
    const es2_hook_descriptor *d=function_hook_descriptor(outer);
    int status=0;unsigned total=0;uint64_t entered=0,replaced=0;static wsl_record expected[WSL_MAX];
    BYTE saved[WSL_PATCHED][5];if(wsl_patch(saved,1u<<10|1u<<12,1))return 1;
    wsl_real=1;
    for(unsigned n=0;n<cases&&!status;++n){
        uint32_t a[4],r[6],want[3],got[3],pattern=n&1?0xa7a7a7a7u:0x31313131u;wsl_build(outer,a,r);prepare(a);
        const uint32_t mask=n==0?0u:n==1?0xffffffffu:random32();
        wsl_fire_replaced=0;
        for(unsigned j=0;j<hook_count;++j)if(hooks[j]==ES2_HOOK_INDEX_fire_group&&mask>>j&1)wsl_fire_replaced=1;
        BYTE before[WSL_GLOBALS],after[WSL_GLOBALS],actual[WSL_GLOBALS];
        memcpy(wsl_before,wsl_arena,WSL_SIZE);wsl_globals(before,0);
        wsl_reset();wsl_machine(mapped+d->rva,a,d->arg_count,r,want,pattern);
        unsigned calls=wsl_calls;memcpy(expected,wsl_log,calls*sizeof(*expected));memcpy(wsl_after,wsl_arena,WSL_SIZE);wsl_globals(after,0);
        es2_hook hk[16];memset(hk,0,sizeof(hk));
        for(unsigned j=0;j<hook_count;++j){es2_hook_options o=wsl_options(mask>>j&1?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL);
            if(!weapons_install_hook(hk+j,function_hook_descriptor(hooks[j]),&o,function_hook_extension(hooks[j]))){
                wsl_real=0;return fail("weapon_slots real install");}}
        memcpy(wsl_arena,wsl_before,WSL_SIZE);wsl_globals(before,1);wsl_reset();
        wsl_machine(mapped+d->rva,a,d->arg_count,r,got,pattern);wsl_globals(actual,0);
        if(wsl_fire_replaced)got[2]=want[2];
        wsl_normalize(outer,want);wsl_normalize(outer,got);
        if(memcmp(want,got,sizeof(want))||calls!=wsl_calls||memcmp(expected,wsl_log,calls*sizeof(*expected))||memcmp(wsl_after,wsl_arena,WSL_SIZE)||
           memcmp(after,actual,WSL_GLOBALS)||weapon_slots_ecx_valid){
            fprintf(stderr,"%s mask=%08x\n",what,mask);status=wsl_report("real",n,want,got,calls,expected,after,actual);}
        for(unsigned j=0;j<hook_count;++j){entered+=hk[j].calls;if(mask>>j&1)replaced+=hk[j].calls;}
        for(unsigned j=0;j<hook_count;++j)if(!es2_hook_uninstall(hk+j)){wsl_real=0;return fail("weapon_slots real uninstall");}
        wsl_globals(before,1);++total;
    }
    wsl_real=0;wsl_fire_replaced=0;
    if(wsl_patch(saved,1u<<10|1u<<12,0))return 1;
    if(!status&&!replaced){fprintf(stderr,"FAIL: %s %s never entered a replaced hook\n",current_function,what);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases (%s, %u hooks in random modes; %llu hook entries, %llu replaced), 31/a7 stack, EAX/EDX/ECX/calls/arena/globals\n",
        current_function,total,what,hook_count,(unsigned long long)entered,(unsigned long long)replaced);
    return status;
}
static void wsl_prepare_frame(uint32_t a[4]){
    /* A count the real select_step can step over (sel < count) and a
     * gauge mode the real fire_group can follow (0..2). */
    BYTE *w=wsl_w();put16(wsl_arena+WSL_H+0xb1,(uint16_t)(random32()%3));if(wsl_count()<1)put16(w+8,(uint16_t)(1u+random32()%WSL_SLOTS));
    if((int8_t)w[0x1d]>=wsl_count())wsl_select(random32()|1u);
    a[0]=WSL_ADDR(WSL_W);a[1]=random32();
}
static void wsl_prepare_button(uint32_t a[4]){
    BYTE *h=wsl_arena+WSL_H;a[0]=WSL_ADDR(WSL_H);
    for(unsigned k=0;k<4;++k)put32(h+0x18+4*k,WSL_ADDR(0x1100+0x20*k));
    a[1]=random32()%5?WSL_ADDR(0x1100+0x20*(random32()%4)):random32();
}
static void wsl_prepare_toggle(uint32_t a[4]){
    static const uint16_t wanted[]={0,1,2,3,4,5,0xffff};
    a[0]=WSL_ADDR(WSL_W);a[1]=(random32()&0xffff0000u)|wanted[random32()%7];
}
static int wsl_real_passes(unsigned index){
    static const unsigned frame_hooks[]={ES2_HOOK_INDEX_weapon_frame,ES2_HOOK_INDEX_ready_step,ES2_HOOK_INDEX_selected_ready,
        ES2_HOOK_INDEX_chain_ready,ES2_HOOK_INDEX_latch_set,ES2_HOOK_INDEX_link_toggle,ES2_HOOK_INDEX_gauge_sync,
        ES2_HOOK_INDEX_gauge_state,ES2_HOOK_INDEX_gauge_link,ES2_HOOK_INDEX_gauge_mode,ES2_HOOK_INDEX_select_step,
        ES2_HOOK_INDEX_fire_group,ES2_HOOK_INDEX_weapon_select};
    static const unsigned button_hooks[]={ES2_HOOK_INDEX_gauge_button,ES2_HOOK_INDEX_gauge_mode,ES2_HOOK_INDEX_gauge_link,
        ES2_HOOK_INDEX_link_toggle};
    static const unsigned toggle_hooks[]={ES2_HOOK_INDEX_member_toggle,ES2_HOOK_INDEX_group_toggle};
    /* Diagnostics only: a larger real-callee budget for hunting rare states. */
    const char *more=getenv("ES2_INVITRO_WEAPON_SLOTS_REAL");const unsigned scale=more&&*more?(unsigned)strtoul(more,NULL,10):1u;
    if(index==ES2_HOOK_INDEX_gauge_button)return wsl_real_pass("442154 over 441f90/442054/410f14",index,button_hooks,4,1024*scale,wsl_prepare_button);
    if(index==ES2_HOOK_INDEX_member_toggle)return wsl_real_pass("weapon_system group_toggle 4110ac over 410cd0",
        ES2_HOOK_INDEX_group_toggle,toggle_hooks,2,1024*scale,wsl_prepare_toggle);
    for(unsigned j=0;j<sizeof(frame_hooks)/sizeof(frame_hooks[0]);++j)if(frame_hooks[j]==index)
        return wsl_real_pass("410b40 over the real weapon system and gauge",ES2_HOOK_INDEX_weapon_frame,frame_hooks,13,2048*scale,wsl_prepare_frame);
    return 0;
}
/* Compare probes (compare_probes.h) for latch_set and gauge_mode: the
 * direct-case inputs (neither makes a call), arena and globals. */
static void wsl_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    (void)n;uint32_t b[4],s[6];wsl_build(index,b,s);
    for(unsigned i=0;i<6;++i)a[i]=i<4?b[i]:0;
    for(unsigned i=0;i<3;++i)r[i]=s[i];
}
static void wsl_compare_state(BYTE *s,int restore){
    if(restore){memcpy(wsl_arena,s,WSL_SIZE);wsl_globals(s+WSL_SIZE,1);}else{memcpy(s,wsl_arena,WSL_SIZE);wsl_globals(s+WSL_SIZE,0);}
}
static int weapon_slots_cases(unsigned index,unsigned limit){
    BYTE saved_globals[WSL_GLOBALS];wsl_globals(saved_globals,0);
    /* Diagnostics only: another random stream for hunting rare states. */
    {const char *seed=getenv("ES2_INVITRO_WEAPON_SLOTS_SEED");if(seed&&*seed){random_state^=(uint32_t)strtoul(seed,NULL,0);if(!random_state)random_state=1;}}
    if(limit&&compare_probe_installed(index)){
        const int status=compare_probe_run(index,limit,wsl_compare_build,wsl_compare_state,WSL_SIZE+WSL_GLOBALS);
        wsl_globals(saved_globals,1);return status;
    }
    BYTE saved[WSL_PATCHED][5];if(wsl_patch(saved,wsl_which(index),1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_WEAPON_SLOTS_CASES");
    if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[4],r[6];wsl_build(index,a,r);status=wsl_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    wsl_globals(saved_globals,1);
    if(!limit&&!status)status=wsl_installed_probes(index,64);
    wsl_globals(saved_globals,1);
    if(wsl_patch(saved,wsl_which(index),0))return 1;
    if(!limit&&!status)status=wsl_real_passes(index);
    wsl_globals(saved_globals,1);
    if(!limit&&!status&&compare_probe_capable(index))status=compare_probe_run(index,0,wsl_compare_build,wsl_compare_state,WSL_SIZE+WSL_GLOBALS);
    wsl_globals(saved_globals,1);
    if(!status)printf("PASS: %-20s %u cases; EAX/EDX/ECX, ordered calls with entry EAX/EDX/ECX/EBX/ESI/EDI and slot counts, arena, globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
