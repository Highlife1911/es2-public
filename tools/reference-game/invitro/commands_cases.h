/* Commands group: the command handler 45feb8 (command_key) and its playback key
 * check 45fe6c (playback_key) (docs/reverse-engineering/input.md, "command
 * handler as own code"). A 16 KiB arena holds the cockpit, the display objects,
 * the camera, the player, eight list actors with their groups, the actor list,
 * the vtables, the dialog, lamp, gauge and joystick blocks and the runtime
 * counter; the image globals the two functions read and write live in the
 * mapped image's DATA (saved and restored around every case). Every game callee
 * is a logging stub patched into the private image (49630c links a SEH record
 * into FS:0 as the original does), the actor virtuals +2Ch/+3Ch/+74h and the
 * dialog virtual +8 go through arena vtables that point at slot stubs. A stub
 * logs its arguments, the entry EAX/EDX/ECX, EBX/ESI/EDI and the number of
 * argument slots the bridge pushed. Direct cases run the original body and the
 * adapter against the stubs and compare EDX:EAX, the ordered calls, the arena,
 * the globals, the callee-counter deltas and FS:0; the jump table 45ff50 is
 * routed through counting trampolines and every entry must be taken. Then
 * installed 31/a7 poison probes (random EBX/ESI/EDI; through playback_key's
 * entry shim), and a real-callee pass: command_key over the real hud_key
 * 432bc8, playback_key 45fe6c, dist_gate 49288c, status_eval 413180,
 * cockpit_nearest 4333c8 and the key method vehicle_keys 4157c8 (all seven
 * hooks original/replaced in the 128 combinations; their own callees stay
 * stubs). Then command_key's frame probe (its entry shim: EAX..EDI and EBP/ESP
 * per stub, the 1024 bytes below the slot). Identifiers: ck_/CK_. */
enum { CK_SIZE=0x4000, CK_H=0x100, CK_D=0x500, CK_DH=0x580, CK_C=0x600, CK_P=0x700, CK_ACT=0xa00,
    CK_W=0x2200, CK_GRP=0x2300, CK_LIST=0x2400, CK_VT=0x2500, CK_VT1=0x2580, CK_DLG=0x2600, CK_DVT=0x2680,
    CK_LAMP=0x2700, CK_GAUGE=0x2800, CK_JOY=0x2900, CK_COUNTER=0x2a00, CK_TOBJ=0x2d00,
    CK_ACTORS=8, CK_IDS=40, CK_MAX=64, CK_TABLE=29 };
#define CK_BASE ((uint32_t)(uintptr_t)ck_arena)
#define CK_ADDR(o) (CK_BASE+(uint32_t)(o))
#define CK_ACTOR(k) (CK_ACT+0x300u*(uint32_t)(k))
static BYTE ck_arena[CK_SIZE],ck_before[CK_SIZE],ck_after[CK_SIZE];
typedef struct ck_record { uint32_t id,a[4],r[3],cs[3],slots,frame[2]; } ck_record;
static ck_record ck_log[CK_MAX];
static unsigned ck_calls,ck_walk,ck_walk0;
static uint32_t ck_seed,ck_stream,ck_status;
__attribute__((used)) static uint32_t ck_ecx,ck_first_record;
/* Frame probe: EBP at the last stub entry, the slot address of the tracked call
 * (frame[] of a record = EBP and ESP at entry relative to it, while ck_track is
 * set) and the 1024 bytes below the slot after return. */
__attribute__((used)) static uint32_t ck_entry_ebp,ck_origin;
__attribute__((used)) static BYTE ck_window[1024];
static int ck_track;
/* ids 0..25 and 30..39 are patched callees (VA); 26..29 are arena-vtable
 * slots (+74h, +2Ch, +3Ch, dialog +8). 30..39 are only reached from the
 * real hud_key, vehicle_keys, status_eval and cockpit_nearest of the
 * real-callee pass. */
static const uint32_t ck_addr[CK_IDS]={0x49630c,0x432bc8,0x45fe6c,0x401148,0x49288c,0x411e40,0x411e60,0x42a3f4,
    0x45df34,0x45fe38,0x429828,0x4610ad,0x441fa4,0x442120,0x413180,0x47749c,0x455950,0x4543b0,0x4561dc,0x45cff0,
    0x477904,0x4333c8,0x45f070,0x4669c4,0x4667c8,0x467ad8,0,0,0,0,0x4332dc,0x434974,0x410f78,0x438d9c,0x47ebb8,0x4801d4,
    0x4135e8,0x433174,0x43dbb0,0x43d9b4};
static const unsigned ck_arity[CK_IDS]={0,2,1,2,2,2,2,2,1,0,1,0,1,1,2,1,4,2,4,0,0,1,0,1,0,0,4,2,1,1,2,0,2,3,2,3,3,2,1,2};
static const unsigned ck_slotkey[CK_IDS]={[26]=0x74,[27]=0x2c,[28]=0x3c,[29]=0x8};
/* Registers a call's comparison ignores: bit 0..2 EAX/EDX/ECX, bit 3..5
 * EBX/ESI/EDI. 49630c reads only EAX and EBP (the exception bridge keeps
 * its own EBX/ESI/EDI); 432bc8 gets the caller's EBX, which it saves before
 * writing; 4667c8 and 467ad8 (playback_key's callees, plain bridge) save
 * or never read EBX/ESI/EDI; ids 30..39 are the nested hooks' own callees,
 * whose registers those hooks' groups check. */
static int ck_real_active;
static unsigned ck_mask(unsigned id){
    if(id==0||id==24||id==25)return 0x38u;
    if(id==1)return 0x08u;
    if(id>=30)return 0x3fu;
    /* Real pass: a replaced status_eval returns the framework's entry ECX,
     * the original 4135e8's; 47749c writes ECX before reading it (476edc)
     * or hands it to the runtime (47747c). */
    if(id==15&&ck_real_active)return 0x04u;
    return 0;
}
static uint32_t ck_next(void){
    uint32_t z=ck_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static void ck_reset(void){ck_calls=0;ck_stream=ck_seed;ck_walk=ck_walk0;ck_status=0;ck_first_record=0;}
static uint32_t ck_fs0(void){uint32_t v;__asm__ volatile("movl %%fs:0,%0":"=r"(v));return v;}
/* Image globals read or written by the two functions, their stubs' callers
 * and the real callees of the real-callee pass. */
static const struct { uint32_t rva,size; } ck_global_list[]={
    {0x9b088,4},{0xcf99c,4},{0x2cbee4,4},{0xd2538,1},{0xd2564,2},{0x9ef38,4},{0x9ef34,4},{0xd24e6,4},{0xd2684,4},
    {0xd24ea,4},{0xd24c0,4},{0xa9af8,8},{0xd2500,2},{0xd24fc,2},{0xd24f2,2},{0xd251c,4},{0xd24ee,2},{0xd251a,2},
    {0xd2520,2},{0xd24f0,2},{0xd2534,2},{0xd24d6,2},{0xd24d8,2},{0xd2a70,4},{0xd2504,2},{0xd2a74,2},{0xa9e68,2},
    {0xd24fe,2},{0xd1f46,1},{0x9a06e,2},{0xd252c,2},{0xa9b84,4},{0xa9ccc,4},{0xa9e54,2},{0xa9e4c,2}};
enum { CK_GLOBALS=100 };
static void ck_globals(BYTE *out,int restore){
    BYTE *p=out;
    for(unsigned i=0;i<sizeof(ck_global_list)/sizeof(ck_global_list[0]);++i){
        if(restore)memcpy(mapped+ck_global_list[i].rva,p,ck_global_list[i].size);
        else memcpy(p,mapped+ck_global_list[i].rva,ck_global_list[i].size);
        p+=ck_global_list[i].size;
    }
}
__attribute__((used,noinline)) static uint64_t __cdecl ck_log_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(ck_calls==CK_MAX){
        fprintf(stderr,"commands call ids:");for(unsigned j=0;j<CK_MAX;++j)fprintf(stderr," %u",ck_log[j].id);fputc('\n',stderr);
        fail("commands synthetic call overflow");ExitProcess(1);
    }
    ck_record *v=ck_log+ck_calls++;memset(v,0,sizeof(*v));v->id=id;
    for(unsigned j=0;j<ck_arity[id]&&j<4;++j)v->a[j]=a[j];
    for(unsigned j=0;j<3;++j){v->r[j]=regs[j];v->cs[j]=regs[3+j];}
    v->slots=weapons_bridge_slots(a,ck_arity[id]);
    if(ck_track){v->frame[0]=ck_entry_ebp-ck_origin;v->frame[1]=(uint32_t)(uintptr_t)a-4u-ck_origin;}
    uint32_t eax=ck_next(),edx=ck_next(),x=ck_next();ck_ecx=ck_next();
    switch(id){
    case 1:if(x%16)eax&=0xffffff00u;break;                  /* 432bc8: AL = 0 in 15 of 16 */
    case 2:if(x%16)eax&=0xffff0000u;break;                  /* 45fe6c: AX = 0 in 15 of 16 */
    case 4:if(x%4)eax=100000u+(x>>8)%9u-4u;break;           /* dist_gate around 100000 */
    case 5:case 6:eax=CK_ADDR(CK_ACTOR(ck_walk%CK_ACTORS));++ck_walk;break;   /* the list walk */
    case 8:{uint32_t cam=get32(mapped+0xd24ea);                /* 45df34 may change the camera (N rereads +4Ah) */
        if((x&1)&&cam-CK_BASE<CK_SIZE-0x50u)put32(ck_arena+(cam-CK_BASE)+0x4a,(x&2)?x:0);break;}
    case 11:eax=CK_ADDR(CK_LAMP);break;                     /* 4610ad: the lamp gadget */
    case 12:if(x&1)eax&=0xffffff00u;break;                  /* 441fa4: toggled (AL) or refused */
    case 13:eax=CK_ADDR(CK_GAUGE);break;                    /* 442120: the gauge state */
    case 14:if(x%4)eax=(eax&0xffff0000u)|(((x>>8)%14u-3u)&0xffffu);ck_status=eax;break;   /* 413180: status */
    case 15:eax=CK_ADDR(CK_DLG);break;                      /* 47749c: never null (PROVED) */
    case 20:eax=CK_ADDR(CK_JOY);break;                      /* 477904: the joystick summary */
    case 29:if(x&1)eax=(uint32_t)(int32_t)(int16_t)get16(mapped+(uint32_t)(0x9f5c0+2*(int32_t)(int16_t)ck_status));
        else if(x&2)eax=x>>2&1u;break;                      /* modal: the status match, 0 or 1 */
    case 35:for(unsigned j=0;j<12;++j)((BYTE *)(uintptr_t)a[2])[j]=(BYTE)ck_next();break;   /* 4801d4 writes its point */
    case 36:if(x%4)eax=(eax&0xffff0000u)|(((x>>8)%14u-3u)&0xffffu);ck_status=eax;break;   /* 4135e8: status_eval's status */
    case 37:eax=(eax&0xffffff00u)|(x&1u);break;             /* 433174: candidate test AL 0/1 */
    default:break;
    }
    return (uint64_t)edx<<32|eax;
}
/* Pushes EDI, ESI, EBX, ECX, EDX, EAX (the register block) before the
 * argument pointer, the block pointer and the id. */
#define CK_STUB(n) __attribute__((naked)) static void ck_stub_##n(void){__asm__ volatile( \
    "movl %ebp,_ck_entry_ebp\n\tpushl %edi\n\tpushl %esi\n\tpushl %ebx\n\tpushl %ecx\n\tpushl %edx\n\tpushl %eax\n\t" \
    "leal 28(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\tpushl $" #n "\n\t" \
    "calll _ck_log_impl\n\taddl $36,%esp\n\tmovl _ck_ecx,%ecx\n\tret\n\t");}
CK_STUB(1) CK_STUB(2) CK_STUB(3) CK_STUB(4) CK_STUB(5) CK_STUB(6) CK_STUB(7) CK_STUB(8) CK_STUB(9) CK_STUB(10)
CK_STUB(11) CK_STUB(12) CK_STUB(13) CK_STUB(14) CK_STUB(15) CK_STUB(16) CK_STUB(17) CK_STUB(18) CK_STUB(19) CK_STUB(20)
CK_STUB(21) CK_STUB(22) CK_STUB(23) CK_STUB(24) CK_STUB(25) CK_STUB(26) CK_STUB(27) CK_STUB(28) CK_STUB(29) CK_STUB(30)
CK_STUB(31) CK_STUB(32) CK_STUB(33) CK_STUB(34) CK_STUB(35) CK_STUB(36) CK_STUB(37) CK_STUB(38) CK_STUB(39)
#undef CK_STUB
/* 49630c: log, then link the record at EBP+[EAX+4] into FS:0 (saved head,
 * try-state 0) as the runtime does; EAX = the old head, EDX/ECX kept. The
 * first record of a run (command_key's) is remembered for the real pass. */
__attribute__((naked)) static void ck_stub_0(void){__asm__ volatile(
    "movl %ebp,_ck_entry_ebp\n\tpushl %edi\n\tpushl %esi\n\tpushl %ebx\n\tpushl %ecx\n\tpushl %edx\n\tpushl %eax\n\t"
    "leal 28(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\tpushl $0\n\t"
    "calll _ck_log_impl\n\taddl $12,%esp\n\t"
    "movl (%esp),%ecx\n\tmovl 4(%ecx),%ecx\n\taddl %ebp,%ecx\n\t"
    "cmpl $0,_ck_first_record\n\tjne 1f\n\tmovl %ecx,_ck_first_record\n\t1:\n\t"
    "movl %fs:0,%eax\n\tmovl %eax,(%ecx)\n\tmovw $0,16(%ecx)\n\tmovl %ecx,%fs:0\n\t"
    "movl 4(%esp),%edx\n\tmovl 8(%esp),%ecx\n\taddl $24,%esp\n\tret\n\t");}
static void *ck_stubs[CK_IDS]={ck_stub_0,ck_stub_1,ck_stub_2,ck_stub_3,ck_stub_4,ck_stub_5,ck_stub_6,ck_stub_7,
    ck_stub_8,ck_stub_9,ck_stub_10,ck_stub_11,ck_stub_12,ck_stub_13,ck_stub_14,ck_stub_15,ck_stub_16,ck_stub_17,
    ck_stub_18,ck_stub_19,ck_stub_20,ck_stub_21,ck_stub_22,ck_stub_23,ck_stub_24,ck_stub_25,ck_stub_26,ck_stub_27,
    ck_stub_28,ck_stub_29,ck_stub_30,ck_stub_31,ck_stub_32,ck_stub_33,ck_stub_34,ck_stub_35,ck_stub_36,ck_stub_37,
    ck_stub_38,ck_stub_39};
/* Calls target with one argument slot and chosen EAX, EDX, ECX, EBX, ESI,
 * EDI (regs[0..5]) over 2 KiB of stack filled with a per-case byte, so a
 * read of an unwritten local would differ from the adapter's zeroed
 * scratch; EDX:EAX into out. */
__attribute__((naked,noinline)) static void ck_run(void *target,const uint32_t *a,const uint32_t *regs,uint32_t out[2],uint32_t fill){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $2048,%esp\n\tmovl %esp,%edi\n\tmovl 24(%ebp),%eax\n\tmovl $512,%ecx\n\trep stosl\n\taddl $2048,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%eax\n\tmovl 12(%eax),%ebx\n\tmovl 16(%eax),%esi\n\tmovl 20(%eax),%edi\n\t"
        "movl 4(%eax),%edx\n\tmovl 8(%eax),%ecx\n\tmovl (%eax),%eax\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* Jump table 45FF50h (29 entries) redirected through counting trampolines. */
__attribute__((used)) static uint32_t ck_case_targets[CK_TABLE],ck_case_hits[CK_TABLE];
#define CK_CASE(n) __attribute__((naked)) static void ck_case_##n(void){__asm__ volatile( \
    "incl _ck_case_hits+" #n "*4\n\tjmp *_ck_case_targets+" #n "*4\n\t");}
CK_CASE(0) CK_CASE(1) CK_CASE(2) CK_CASE(3) CK_CASE(4) CK_CASE(5) CK_CASE(6) CK_CASE(7) CK_CASE(8) CK_CASE(9)
CK_CASE(10) CK_CASE(11) CK_CASE(12) CK_CASE(13) CK_CASE(14) CK_CASE(15) CK_CASE(16) CK_CASE(17) CK_CASE(18)
CK_CASE(19) CK_CASE(20) CK_CASE(21) CK_CASE(22) CK_CASE(23) CK_CASE(24) CK_CASE(25) CK_CASE(26) CK_CASE(27) CK_CASE(28)
#undef CK_CASE
static void *ck_cases_entry[CK_TABLE]={ck_case_0,ck_case_1,ck_case_2,ck_case_3,ck_case_4,ck_case_5,ck_case_6,ck_case_7,
    ck_case_8,ck_case_9,ck_case_10,ck_case_11,ck_case_12,ck_case_13,ck_case_14,ck_case_15,ck_case_16,ck_case_17,
    ck_case_18,ck_case_19,ck_case_20,ck_case_21,ck_case_22,ck_case_23,ck_case_24,ck_case_25,ck_case_26,ck_case_27,ck_case_28};
static int ck_table(int install){
    BYTE *t=mapped+0x5ff50u;DWORD old,ignored;
    if(!VirtualProtect(t,4*CK_TABLE,PAGE_EXECUTE_READWRITE,&old))return fail("commands table protection");
    for(unsigned k=0;k<CK_TABLE;++k){
        if(install){ck_case_targets[k]=get32(t+4*k);ck_case_hits[k]=0;put32(t+4*k,(uint32_t)(uintptr_t)ck_cases_entry[k]);}
        else put32(t+4*k,ck_case_targets[k]);
    }
    if(!VirtualProtect(t,4*CK_TABLE,old,&ignored))return fail("commands table restore protection");
    FlushInstructionCache(GetCurrentProcess(),t,4*CK_TABLE);return 0;
}
/* The codes of the case table and their neighbours, the compare chain's
 * boundaries, and every entry of the jump table. */
static const uint16_t ck_codes[]={0,1,2,3,4,5,6,7,8,9,0xa,0xb,0xc,0xd,0xe,0xf,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,
    0x19,0x1a,0x1b,0x1c,0x1d,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2e,0x2f,0x30,0x31,0x32,0x34,0x35,0x36,0x57,0x58,0x59,
    0x218,0x219,0x21a,0x21c,0x21e,0x21f,0x220,0x24d,0x24e,0x24f,0x401,0x402,0x403,0x40f,0x410,0x411,0x413,0x414,0x415,
    0x418,0x419,0x41a,0x420,0x421,0x422,0x430,0x431,0x432,0x61f,0x620,0x621,0x630,0x631,0x632,0x633,0x634,0x635,
    0x82e,0x82f,0x830,0x834,0x835,0x836,0xffff};
enum { CK_CODES=sizeof(ck_codes)/sizeof(ck_codes[0]) };
/* Every jump-table code, the codes with a handler, and a few defaults
 * (above the table, between the chains). */
static const uint16_t ck_handled[]={0,1,2,3,4,5,6,7,8,9,0xa,0xb,0xc,0xd,0xe,0xf,0x10,0x11,0x12,0x13,0x14,0x15,0x16,
    0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x28,0x2b,0x2f,0x30,0x31,0x35,0x58,0x100,0x219,0x21f,0x24e,0x402,0x410,0x414,
    0x419,0x421,0x431,0x620,0x631,0x633,0x634,0x82f,0x835};
enum { CK_HANDLED=sizeof(ck_handled)/sizeof(ck_handled[0]) };
static const uint16_t ck_cheats[]={0x21f,0x24e,0x414,0x419,0x421,0x431,0x620,0x631,0x633,0x634};
static uint32_t ck_pick(uint32_t bound){return random32()%bound;}
static void ck_vtables(int real){
    put32(ck_arena+CK_VT+0x2c,(uint32_t)(uintptr_t)ck_stub_27);put32(ck_arena+CK_VT+0x3c,(uint32_t)(uintptr_t)ck_stub_28);
    put32(ck_arena+CK_VT+0x74,(uint32_t)(uintptr_t)ck_stub_26);
    put32(ck_arena+CK_VT1+0x2c,real?(uint32_t)(uintptr_t)(mapped+0x157c8):(uint32_t)(uintptr_t)ck_stub_27);
    put32(ck_arena+CK_VT1+0x3c,(uint32_t)(uintptr_t)ck_stub_28);put32(ck_arena+CK_VT1+0x74,(uint32_t)(uintptr_t)ck_stub_26);
    put32(ck_arena+CK_DLG,CK_ADDR(CK_DVT));put32(ck_arena+CK_DVT+8,(uint32_t)(uintptr_t)ck_stub_29);
}
/* A complete, safe world, then the code. Returns the low word of the code.
 * real selects the real-callee layout (vehicle_keys behind +2Ch, hud_key's
 * display object, the panel byte that keeps hud_key out of its gadgets). */
static uint32_t ck_build(unsigned index,unsigned n,int real,uint32_t a[2],uint32_t regs[6]){
    ck_seed=random32();ck_walk0=random32()%CK_ACTORS;random_bytes(ck_arena,CK_SIZE);
    for(unsigned i=0;i<6;++i)regs[i]=random32();
    a[0]=a[1]=0;
    const uint32_t vt=CK_ADDR(real?CK_VT1:CK_VT);
    ck_vtables(real);
    /* Globals. */
    put32(mapped+0x9b088,CK_ADDR(CK_H));put32(mapped+0xcf99c,CK_ADDR(CK_DH));put32(mapped+0x2cbee4,CK_ADDR(CK_COUNTER));
    mapped[0xd2538]=(BYTE)ck_pick(3);
    put16(mapped+0xd2564,ck_pick(32)?(uint16_t)(random32()|1u):0);
    put32(mapped+0x9ef38,ck_pick(2)?(ck_pick(4)?1u:0x80000000u):0);
    put32(mapped+0x9ef34,ck_pick(2)?random32()|1u:0);
    put32(mapped+0xd24e6,CK_ADDR(CK_P));put32(mapped+0xa9b84,CK_ADDR(CK_P));
    {   /* view subject: the player, any actor, the last actor (it ends both
         * list walks) or the walk's first actor */
        const uint32_t pick=ck_pick(4),k=pick==1?ck_pick(CK_ACTORS):pick==2?CK_ACTORS-1u:ck_walk0;
        put32(mapped+0xd2684,pick==0?CK_ADDR(CK_P):CK_ADDR(CK_ACTOR(k)));
    }
    put32(mapped+0xd24ea,CK_ADDR(CK_C));put32(mapped+0xd24c0,CK_ADDR(CK_D));
    put32(mapped+0xa9af8,CK_ADDR(CK_LIST));put16(mapped+0xa9afc,(uint16_t)random32());
    put16(mapped+0xa9afe,ck_pick(10)?(uint16_t)ck_pick(CK_ACTORS+1):0xffffu);
    static const int16_t slots[]={-2,-1,0,1,0x1c,0x1d,0x1e};
    put16(mapped+0xd2500,(uint16_t)(ck_pick(2)?slots[ck_pick(7)]:(int16_t)random32()));
    put16(mapped+0xd24ee,(uint16_t)(ck_pick(3)==0?3u:ck_pick(8)?ck_pick(7):random32()));   /* N needs mode 3 */
    put16(mapped+0xd24d6,ck_pick(3)?0:(uint16_t)(random32()|1u));put16(mapped+0xd24d8,ck_pick(3)?0:(uint16_t)(random32()|1u));
    put16(mapped+0xd252c,ck_pick(2)?0:(uint16_t)(random32()|1u));
    put16(mapped+0xd24f2,(uint16_t)(ck_pick(2)?ck_pick(2):random32()));put16(mapped+0xd24f0,(uint16_t)(ck_pick(2)?ck_pick(2):random32()));
    mapped[0xd1f46]=(BYTE)(ck_pick(2)?1:random32());
    put16(mapped+0x9a06e,(uint16_t)(ck_pick(2)?ck_pick(3)-1u:random32()));
    put16(mapped+0xd251a,(uint16_t)random32());put16(mapped+0xd2520,(uint16_t)random32());put16(mapped+0xd2534,(uint16_t)random32());
    put32(mapped+0xd251c,random32());put16(mapped+0xd24fc,(uint16_t)random32());put32(mapped+0xd2a70,random32());
    put16(mapped+0xd2504,(uint16_t)random32());put16(mapped+0xd2a74,(uint16_t)random32());put16(mapped+0xa9e68,(uint16_t)random32());
    put16(mapped+0xd24fe,(uint16_t)random32());put32(mapped+0xa9ccc,random32());
    /* Objects. */
    put32(ck_arena+CK_D+0x14,ck_pick(2)?4u:ck_pick(8));put32(ck_arena+CK_DH+0x14,ck_pick(8));
    put32(ck_arena+CK_C+0x4a,ck_pick(4)?0:random32()|1u);put16(ck_arena+CK_C+0x36,(uint16_t)(ck_pick(2)?1:random32()));
    put32(ck_arena+CK_C+0x32,ck_pick(2)?CK_ADDR(CK_P):random32());
    for(unsigned g=0;g<4;++g){BYTE *G=ck_arena+CK_GRP+0x40*g;G[0x12]=(BYTE)(ck_pick(2)?1:random32());put32(G+0x14,ck_pick(4)?0:random32()|1u);}
    const uint32_t pg=CK_ADDR(CK_GRP+0x40u*ck_pick(4));
    BYTE *P=ck_arena+CK_P;put32(P,vt);put32(P+0x45,pg);put32(P+0x202,CK_ADDR(CK_W));P[0x99]=0;
    if(real)put32(P+0x2e,ck_pick(2000)-1000u);              /* z near 0: the actors' z are the loops' test values */
    for(unsigned k=0;k<CK_ACTORS;++k){
        BYTE *A=ck_arena+CK_ACTOR(k);put32(A,vt);
        put32(A+0x45,k==CK_ACTORS-1||ck_pick(2)?pg:CK_ADDR(CK_GRP+0x40u*ck_pick(4)));
        A[0x99]=(BYTE)(ck_pick(4)?0:1);
        put32(A+0x2e,k==CK_ACTORS-1?ck_pick(1000):(ck_pick(2)?(uint32_t)(-99000-1+(int32_t)ck_pick(3)):(uint32_t)((int32_t)ck_pick(200000)-100000)));
        if(real)for(unsigned j=0;j<2;++j)put32(A+0x26+4*j,get32(P+0x26+4*j)+ck_pick(140000)-70000u);   /* dist_gate both sides; +2Eh (z) stays the loops' test value */
        put32(ck_arena+CK_LIST+4*k,ck_pick(8)?CK_ADDR(CK_ACTOR(k)):0);
    }
    ck_arena[CK_W+0x14]=(BYTE)random32();
    ck_arena[CK_LAMP+0xb3]=(BYTE)(ck_pick(2)?0:random32());ck_arena[CK_GAUGE+2]=(BYTE)(ck_pick(2)?0:random32());
    ck_arena[CK_JOY+4]=(BYTE)(ck_pick(2)?0:random32());
    /* hud_key (real pass): the panel byte keeps it out of its gadgets except
     * for the codes it answers without them (0, ';', Alt+D). */
    ck_arena[CK_H+0x20f]=(BYTE)(random32()|1u);
    if(real){                                               /* cockpit_nearest: player, marker owner, candidates */
        put32(ck_arena+CK_H+0x203,CK_ADDR(CK_P));put32(ck_arena+CK_H+0x1f5,CK_ADDR(CK_TOBJ));
        for(unsigned k=0;k<CK_ACTORS;++k)put16(ck_arena+CK_ACTOR(k)+0x1a8,(uint16_t)(ck_pick(2)?ck_pick(3):random32()));
    }
    if(index==ES2_HOOK_INDEX_playback_key){
        static const uint16_t pk[]={0x21c,0x430,0x1c,0x21d,0x42f,0x431,0x41c,0x61c,0,0x821c,0x8430}; /* 821Ch/8430h: bit 15 of the key word */
        uint32_t kc=n<11?pk[n]:(ck_pick(4)?pk[ck_pick(11)]:(uint16_t)random32());
        a[0]=(random32()&0xffff0000u)|kc;return kc;
    }
    uint32_t kc;
    if(real){
        static const uint16_t rk[]={0,0x27,0x220};
        kc=ck_pick(4)?ck_codes[ck_pick(CK_CODES)]:rk[ck_pick(3)];
        if(kc==0x620)kc=0x621;                              /* the record's high word is a slot argument there */
        if(kc==0||kc==0x27||kc==0x220)ck_arena[CK_H+0x20f]=(BYTE)ck_pick(2);
    } else if(n<CK_CODES)kc=ck_codes[n];
    else if(n<16384u)kc=ck_handled[n%CK_HANDLED];          /* every handler about 300 times early in the window */
    else kc=ck_pick(4)?ck_codes[ck_pick(CK_CODES)]:(uint16_t)random32();
    /* Code-dependent bias: only the coverage margin of the conjunctions a
     * handler tests (cheat flag, camera +4Ah, display +14h, view mode). */
    switch(kc){
    case 0x31:if(ck_pick(4))put16(mapped+0xd24ee,3);if(ck_pick(4))put32(ck_arena+CK_D+0x14,4);break;
    case 0x2f:case 0x421:if(ck_pick(2))put16(mapped+0xd24ee,(uint16_t)ck_pick(6));break;
    default:break;
    }
    for(unsigned i=0;i<sizeof(ck_cheats)/sizeof(ck_cheats[0]);++i)
        if(kc==ck_cheats[i]&&ck_pick(2))put32(mapped+0x9ef38,ck_pick(8)?1u:0x80000000u);
    a[0]=(random32()&0xffff0000u)|kc;
    return kc;
}
static unsigned ck_hits(const callee_counter *c){
    unsigned h=0;for(unsigned j=0;j<ck_calls;++j){unsigned id=ck_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?(ck_slotkey[id]==c->key):(ck_slotkey[id]==0&&ck_addr[id]==c->key))++h;}
    return h;
}
static int ck_same(const ck_record *x,const ck_record *y){
    if(x->id!=y->id||memcmp(x->a,y->a,sizeof(x->a))||x->slots!=y->slots)return 0;
    unsigned mask=ck_mask(x->id);
    for(unsigned j=0;j<3;++j)if(!(mask>>j&1)&&x->r[j]!=y->r[j])return 0;
    for(unsigned j=0;j<3;++j)if(!(mask>>(3+j)&1)&&x->cs[j]!=y->cs[j])return 0;
    return 1;
}
static int ck_report(const char *what,unsigned n,const uint32_t want[2],const uint32_t got[2],unsigned calls,const ck_record *e){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x calls %u/%u\n",current_function,n,what,
        want[0],got[0],want[1],got[1],calls,ck_calls);
    for(unsigned j=0;j<calls&&j<ck_calls&&e;++j)if(!ck_same(e+j,ck_log+j)){
        const uint32_t *x=(const uint32_t*)(e+j),*y=(const uint32_t*)(ck_log+j);
        for(unsigned k=0;k<sizeof(*e)/4;++k)if(x[k]!=y[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,e[j].id,k,x[k],y[k]);break;}
    for(unsigned j=0;j<CK_SIZE;++j)if(ck_after[j]!=ck_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,ck_after[j],ck_arena[j]);break;}
    return 1;
}
static int ck_logs_equal(const ck_record *e,unsigned calls){
    if(calls!=ck_calls)return 0;
    for(unsigned j=0;j<calls;++j)if(!ck_same(e+j,ck_log+j))return 0;
    return 1;
}
/* The adapter's entry registers: playback_key's shim moves the caller's EBX
 * high word into AX. */
static void ck_entry(unsigned index,const uint32_t regs[6],uint32_t r[3]){
    r[0]=index==ES2_HOOK_INDEX_playback_key?(regs[0]&0xffff0000u)|(regs[3]>>16):regs[0];r[1]=regs[1];r[2]=regs[2];
}
static int ck_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *regs){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[CK_GLOBALS],after[CK_GLOBALS],actual[CK_GLOBALS];static ck_record expected[CK_MAX];
    uint32_t head=ck_fs0(),want[2],got[2],r[3];
    memcpy(ck_before,ck_arena,CK_SIZE);ck_globals(before,0);
    ck_reset();ck_run(mapped+d->rva,a,regs,want,0x01010101u*(n&0xff));
    unsigned calls=ck_calls;memcpy(expected,ck_log,calls*sizeof(*expected));memcpy(ck_after,ck_arena,CK_SIZE);ck_globals(after,0);
    if(ck_fs0()!=head)return fail("commands original left FS:0 changed");
    memcpy(ck_arena,ck_before,CK_SIZE);ck_globals(before,1);ck_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[32];for(unsigned i=0;i<count&&i<32;++i)counts[i]=c[i].count;
    ck_entry(index,regs,r);
    uint64_t pair=x->replacement_context(a,d->user,r);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);
    ck_globals(actual,0);
    if(ck_fs0()!=head)return fail("commands adapter left FS:0 changed");
    if(n<4096)for(unsigned i=0;i<count&&i<32;++i)if(c[i].count-counts[i]!=ck_hits(c+i))return fail("commands callee counter delta");
    if(want[0]!=got[0]||want[1]!=got[1]||!ck_logs_equal(expected,calls)||memcmp(ck_after,ck_arena,CK_SIZE)||memcmp(after,actual,CK_GLOBALS))
        return ck_report("direct",n,want,got,calls,expected);
    ck_globals(before,1);
    return 0;
}
static int ck_patch(BYTE saved[CK_IDS][5],int active[CK_IDS],uint64_t which,int on){
    for(unsigned i=0;i<CK_IDS;++i){
        if(!ck_addr[i]){active[i]=0;continue;}
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(ck_addr[i]-0x400000u,ck_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(ck_addr[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
/* command_key patches ids 0..23 (24/25 are playback_key's callees);
 * playback_key patches 24/25. The real pass keeps 432bc8 (1), 45fe6c (2)
 * and 49288c (4) real and adds the nested hooks' callees 30..39 (413180
 * and 4333c8 are real there too: their ids 14 and 21 stay patched only in
 * the direct cases). 43dbb0 (38) stays real: a getter (marker record
 * [H+1F5h]+0B1h) that cockpit_nearest's core computes inline. */
#define CK_DIRECT_IDS UINT64_C(0x0000000000ffffff)
#define CK_PLAYBACK_IDS UINT64_C(0x0000000003000000)
#define CK_REAL_IDS ((UINT64_C(0x3ffffff)&~UINT64_C(0x204016))|UINT64_C(0xbfc0000000))
static uint64_t ck_which(unsigned index){return index==ES2_HOOK_INDEX_playback_key?CK_PLAYBACK_IDS:CK_DIRECT_IDS;}
static int ck_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[2],regs[6],want[2],got[2],pattern=poison?0xa7a7a7a7u:0x31313131u,head=ck_fs0();ck_build(index,n,0,a,regs);
        regs[0]=regs[1]=regs[2]=pattern;
        BYTE before[CK_GLOBALS],after[CK_GLOBALS],actual[CK_GLOBALS],efpu[108],afpu[108];static ck_record expected[CK_MAX];
        memcpy(ck_before,ck_arena,CK_SIZE);ck_globals(before,0);
        ck_reset();ck_run(mapped+d->rva,a,regs,want,pattern);es2_hook_fpu_save(efpu);
        unsigned calls=ck_calls;memcpy(expected,ck_log,calls*sizeof(*expected));memcpy(ck_after,ck_arena,CK_SIZE);ck_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("commands poison install");
        memcpy(ck_arena,ck_before,CK_SIZE);ck_globals(before,1);ck_reset();
        ck_run(mapped+d->rva,a,regs,got,pattern);es2_hook_fpu_save(afpu);ck_globals(actual,0);
        if(ck_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||!ck_logs_equal(expected,calls)||
           memcmp(ck_after,ck_arena,CK_SIZE)||memcmp(after,actual,CK_GLOBALS)||memcmp(efpu,afpu,12)||memcmp(efpu+28,afpu+28,80)||h.calls!=1)
            status=ck_report(fast?"installed fast":"installed preserving",n,want,got,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("commands poison uninstall");ck_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, EAX/EDX, trace with EBX/ESI/EDI, arena, globals, FS:0\n",d->name,total);
    return status;
}
/* Real-callee pass: the record address of command_key's frame (stack or
 * the adapter's scratch) is the same value in both runs once normalized;
 * it reaches the callees as hud_key's EDX. */
static void ck_normalize(ck_record *e,unsigned calls,uint32_t record){
    for(unsigned j=0;j<calls;++j){uint32_t *w=(uint32_t *)(e+j);
        for(unsigned k=1;k<sizeof(*e)/4;++k)if(w[k]==record)w[k]=0xf00df00du;}
}
static int ck_real(unsigned index){
    static const unsigned hooks[7]={ES2_HOOK_INDEX_command_key,ES2_HOOK_INDEX_hud_key,ES2_HOOK_INDEX_playback_key,
        ES2_HOOK_INDEX_vehicle_keys,ES2_HOOK_INDEX_dist_gate,ES2_HOOK_INDEX_status_eval,ES2_HOOK_INDEX_cockpit_nearest};
    const unsigned mine=index==ES2_HOOK_INDEX_command_key?0u:2u;
    int status=0;unsigned total=0;uint64_t entered=0;
    BYTE saved[CK_IDS][5];int active[CK_IDS];
    if(ck_patch(saved,active,CK_REAL_IDS,1))return 1;
    ck_real_active=1;
    for(unsigned mode=0;mode<128&&!status;++mode)for(unsigned n=0;n<48&&!status;++n){
        uint32_t a[2],regs[6],want[2],got[2],pattern=n&1?0xa7a7a7a7u:0x31313131u,head=ck_fs0();
        ck_build(ES2_HOOK_INDEX_command_key,n,1,a,regs);
        if(mine==2u&&ck_pick(2)){uint32_t kc=ck_pick(2)?0x21cu:0x430u;a[0]=(a[0]&0xffff0000u)|kc;
            ck_arena[CK_H+0x20f]=(BYTE)(random32()|1u);}       /* playback_key's own codes */
        BYTE before[CK_GLOBALS],after[CK_GLOBALS],actual[CK_GLOBALS];static ck_record expected[CK_MAX];
        memcpy(ck_before,ck_arena,CK_SIZE);ck_globals(before,0);
        ck_reset();ck_run(mapped+0x5feb8,a,regs,want,pattern);
        unsigned calls=ck_calls;uint32_t rec0=ck_first_record;memcpy(expected,ck_log,calls*sizeof(*expected));
        memcpy(ck_after,ck_arena,CK_SIZE);ck_globals(after,0);
        es2_hook hk[7];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<7;++j){o.mode=(mode>>j&1)?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(hooks[j]),&o,function_hook_extension(hooks[j])))return fail("commands real install");}
        memcpy(ck_arena,ck_before,CK_SIZE);ck_globals(before,1);ck_reset();
        ck_run(mapped+0x5feb8,a,regs,got,pattern);ck_globals(actual,0);
        ck_normalize(expected,calls,rec0);ck_normalize(ck_log,ck_calls,ck_first_record);
        if(want[1]==rec0)want[1]=0xf00df00du;
        if(got[1]==ck_first_record)got[1]=0xf00df00du;
        if(ck_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||!ck_logs_equal(expected,calls)||
           memcmp(ck_after,ck_arena,CK_SIZE)||memcmp(after,actual,CK_GLOBALS)){
            fprintf(stderr,"real mode=%u\n",mode);status=ck_report("real",n,want,got,calls,expected);}
        if(mode>>mine&1)entered+=hk[mine].calls;
        for(unsigned j=0;j<7;++j){if(hk[j].mismatches)status=fail("commands real compare mismatch");
            if(!es2_hook_uninstall(hk+j))return fail("commands real uninstall");}
        ck_globals(before,1);++total;
    }
    ck_real_active=0;
    if(ck_patch(saved,active,0,0))return 1;
    if(!status&&!entered){fprintf(stderr,"FAIL: %s real pass: hook %u never entered\n",current_function,mine);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases (over the real hud_key/playback_key/dist_gate/vehicle_keys/status_eval/cockpit_nearest, seven hooks original/replaced; %llu replaced entries), 31/a7 stack, EAX/EDX/calls with EBX/ESI/EDI/arena/globals/FS:0\n",
        current_function,total,(unsigned long long)entered);
    return status;
}
/* Calls target with one slot over a 4-KiB fill, EAX..EDI set to distinct
 * pattern words; keeps the slot address in ck_origin, returns EDX:EAX in out
 * and copies the 1024 bytes below the slot after return. */
__attribute__((naked,noinline)) static void ck_track_run(void *target,const uint32_t *a,uint32_t pattern,uint32_t out[2]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 16(%ebp),%eax\n\tmovl $1024,%ecx\n\trep stosl\n\taddl $4092,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl (%esi)\n\tmovl %esp,_ck_origin\n\tmovl 16(%ebp),%eax\n\t"
        "movl %eax,%edx\n\txorl $0x01010101,%edx\n\tmovl %eax,%ecx\n\txorl $0x02020202,%ecx\n\tmovl %eax,%ebx\n\txorl $0x04040404,%ebx\n\t"
        "movl %eax,%esi\n\txorl $0x08080808,%esi\n\tmovl %eax,%edi\n\txorl $0x10101010,%edi\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%ebx\n\tmovl %eax,(%ebx)\n\tmovl %edx,4(%ebx)\n\t"
        "leal -1024(%esp),%esi\n\tmovl $_ck_window,%edi\n\tmovl $256,%ecx\n\tcld\n\trep movsl\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* command_key's frame probe: every callee a logging stub that records EAX..EDI
 * (EBX/ESI/EDI unmasked, so the caller's EBX and EDI must reach 49630c and
 * 432bc8) and EBP/ESP relative to the caller's slot; the stub of 49630c links
 * the runtime record at EBP+[EAX+4]. The original 45feb8 and the installed
 * replacement (its entry shim; fast and preserving dispatch; 31/a7 stack) must
 * agree in the records, EDX:EAX, arena, globals, FS:0 and the 1024 bytes below
 * the slot (45feb8's frame with the record, the stubs' frames). Codes: the
 * direct cases' list, then every handler twice. An adapter with host frames and
 * a synthetic EBP fails at the first call. */
static int ck_frame_probe(void){
    const unsigned index=ES2_HOOK_INDEX_command_key;const es2_hook_descriptor *d=function_hook_descriptor(index);
    int status=0;unsigned total=0;static ck_record expected[CK_MAX];static BYTE expected_window[1024];
    ck_track=1;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned n=0;n<2*CK_HANDLED+CK_CODES&&!status;++n){
        uint32_t a[2],regs[6],want[2],got[2],pattern=n&1?0xa7a7a7a7u:0x31313131u,head=ck_fs0();
        ck_build(index,n<CK_CODES?n:CK_CODES+n,0,a,regs);
        if(n>=CK_CODES)a[0]=(a[0]&0xffff0000u)|ck_handled[n%CK_HANDLED];
        BYTE before[CK_GLOBALS],after[CK_GLOBALS],actual[CK_GLOBALS];
        memcpy(ck_before,ck_arena,CK_SIZE);ck_globals(before,0);
        ck_reset();ck_track_run(mapped+d->rva,a,pattern,want);memcpy(expected_window,ck_window,1024);
        unsigned calls=ck_calls;memcpy(expected,ck_log,calls*sizeof(*expected));memcpy(ck_after,ck_arena,CK_SIZE);ck_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("commands frame install");
        memcpy(ck_arena,ck_before,CK_SIZE);ck_globals(before,1);ck_reset();
        ck_track_run(mapped+d->rva,a,pattern,got);ck_globals(actual,0);
        if(ck_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||calls!=ck_calls||memcmp(expected,ck_log,calls*sizeof(*expected))||
           memcmp(ck_after,ck_arena,CK_SIZE)||memcmp(after,actual,CK_GLOBALS)||h.calls!=1)
            status=ck_report(fast?"frame fast":"frame preserving",n,want,got,calls,expected);
        else if(memcmp(expected_window,ck_window,1024)){
            fprintf(stderr,"FAIL: command_key case %u (frame window)\n",n);
            for(unsigned j=0;j<1024;++j)if(expected_window[j]!=ck_window[j]){fprintf(stderr," below-%u: %02x/%02x\n",1024-j,expected_window[j],ck_window[j]);break;}
            status=1;}
        if(!es2_hook_uninstall(&h))return fail("commands frame uninstall");ck_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);ck_track=0;
    if(!status)printf("PASS: command_key %u frame cases (every code and handler), fast/preserving dispatch, 31/a7 stack, per-call EAX..EDI and EBP/ESP (caller's EBX/EDI at 49630c/432bc8), FS:0, 1024 bytes below the slot\n",total);
    return status;
}
static int commands_cases(unsigned index,unsigned limit){
    unsigned bytes=0;for(unsigned i=0;i<sizeof(ck_global_list)/sizeof(ck_global_list[0]);++i)bytes+=ck_global_list[i].size;
    if(bytes!=CK_GLOBALS)return fail("commands global list size");
    BYTE old_globals[CK_GLOBALS];ck_globals(old_globals,0);
    BYTE saved[CK_IDS][5];int active[CK_IDS];
    if(ck_patch(saved,active,ck_which(index),1))return 1;
    const int table=index==ES2_HOOK_INDEX_command_key&&!limit;
    if(table&&ck_table(1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_COMMANDS_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==32768)coverage_enable(0);
        uint32_t a[2],regs[6];ck_build(index,n,0,a,regs);status=ck_compare(index,n,a,regs);}
    if(!limit)coverage_enable(0);current_case_valid=0;
    if(table){
        if(ck_table(0))return 1;
        for(unsigned k=0;k<CK_TABLE;++k){
            printf("JUMPTABLE command_key 45ff50 entry %2u target %08x hits %u\n",k,ck_case_targets[k],ck_case_hits[k]);
            if(!status&&!ck_case_hits[k]){fprintf(stderr,"FAIL: command_key jump-table entry %u never taken\n",k);status=1;}
        }
    }
    if(!limit&&!status)status=ck_installed_probes(index,64);
    if(!limit&&!status&&index==ES2_HOOK_INDEX_command_key)status=ck_frame_probe();
    if(ck_patch(saved,active,ck_which(index),0))return 1;
    if(!limit&&!status)status=ck_real(index);
    ck_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; EAX/EDX, ordered calls with entry EAX/EDX/ECX/EBX/ESI/EDI and slot counts, arena, globals, FS:0\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
