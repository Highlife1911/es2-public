/* input_dispatch group: torso_center 41e8d4, torso_aim 41ef14 and player_input
 * 460870 (input.md, "torso helpers and the input dispatcher"). Direct cases run
 * the original body and the core against logging stubs for every game callee of
 * the hook under test (direct calls patched in the private image, the virtuals
 * through arena vtables): ordered arguments, entry EAX/EDX/ECX, for
 * player_input also EBX/ESI/EDI (its bridge loads them), payloads by content;
 * values in the worker's stack reservation are masked, frame pointers passed as
 * arguments are recorded relative to the call's first frame argument. Stubs
 * return edge values and change what the originals reread after a call (view,
 * player, align and centering words, buttons, the action table, target,
 * matrices, angle words, servo words). The jump table of 460870 is redirected
 * through counting trampolines during the direct cases; every entry must be
 * reached. Then installed 31/a7 poison probes and real-callee passes with the
 * nested hooks in rotating modes: multiply_q10 and both servos under
 * torso_center; euler_matrix, matrix_transpose, rotate_point, transform_point,
 * aim_angles, multiply_q10, multiply_q8, both servos and dist3d_delta under
 * torso_aim; dist_gate, multiply_q10, both servos, aim_point, torso_aim,
 * torso_center and weapon_keys under player_input. Only track_seek, the sound
 * latches, 40f540, 40e4dc, the weapon-system routines, the non-hook callees of
 * 460870 and the virtuals stay stubs there; their inputs are compared. */
/* Adds torso_aim's residue probe (original vs replaced with its entry shim:
 * per-call EBX/ESI/EDI/EBP/ESP and the 1024 bytes below the caller's slots) and
 * player_input's fire-path probe (the bytes below fire_line 415608 after a real
 * torso_aim, original vs replaced); then player_input's frame probe (its
 * entry shim: per-call registers and coordinates, the window below the command
 * handler) and the order-chain probe over the real command handler, HUD key
 * handler and MFD group order (the live Alt+O difference). */
typedef struct ix_record { uint32_t id,args[6],regs[6],input[12],eax,edx,frame[2]; } ix_record;
enum { IX_SIZE=0x1300, IX_P=0x40, IX_T=0x360, IX_CFG=0x680, IX_W=0x700, IX_LIST=0x740, IX_S=0x780, IX_VT_P=0x8c0,
    IX_VT_SUB=0x900, IX_VT_OBJ=0x940, IX_VT_WPN=0x980, IX_R=0x9c0, IX_WORDS=0x9f0, IX_H=0xa00, IX_SUB=0xc20,
    IX_PANEL=0xc30, IX_DEV=0xc40, IX_M=0xc50, IX_OBJ=0xc70, IX_NODE=0xc80, IX_MATS=0xc90, IX_POINT=0xd10, IX_MOUNT=0xd20,
    IX_ANODE=0xd30, IX_MAX=96, IX_IDS=48, IX_REAL_MAX=8192, IX_SLOTS=16, IX_GLOBALS=360, IX_TABLE=21,
    /* Order-chain probe: the MFD gadget, its menu, the display object of
     * 429820, the player's group with three members and their vtable, the
     * runtime counter of 49630c and the camera block of 4d24c0; the drag
     * rectangle [H+1F5h] lies outside H (+203h, +20Fh are read). */
    IX_MFD=0xd80, IX_MENU=0xe80, IX_D=0xec0, IX_GROUP=0xee0, IX_MEMBERS=0xf00, IX_MEMBER=0xf20, IX_VT_MEMBER=0x1220,
    IX_RECT=0x1260, IX_COUNTER=0x1280, IX_CAM=0x1290 };
#define IX_BASE ((uint32_t)(uintptr_t)ix_arena)
static BYTE ix_arena[IX_SIZE],ix_before[IX_SIZE],ix_after[IX_SIZE];
static ix_record ix_log[IX_MAX];
static uint32_t ix_real_log[IX_REAL_MAX];
static unsigned ix_calls,ix_real_count,ix_index;
static int ix_real;
static uint32_t ix_seed,ix_stream;
__attribute__((used)) static uint32_t ix_ecx;
/* EBP and ESP at the last stub entry, the tracked invoke's ESP at its call
 * (argument slots), the 1024 bytes below the slots after return and below
 * 415608's entry; while ix_track is set every stub record also keeps
 * EBX/ESI/EDI and EBP/ESP relative to ix_origin. */
__attribute__((used)) static uint32_t ix_entry[2],ix_origin,ix_tmp[3];
__attribute__((used)) static BYTE ix_window[1024],ix_fire_window[1024];
__attribute__((used)) static unsigned ix_fire_hits;
static int ix_track;
/* Callee ids. Direct calls: 0 45feb8, 1 432b14, 2 45c524, 3 477904,
 * 4 401c74, 5 41bb9c, 6 49288c, 7 47e0b0, 8 4160dc, 9 41a550, 10 41a808,
 * 11 41b728, 12 41ef14, 13 41e8d4, 14 415608, 15 49681a, 16 45b734,
 * 17 43349c, 18 4333c8, 19 429820, 20 432bc8, 21 42a3f4, 22 446e3c,
 * 23 410f78, 24 47ebb8, 25 47df19, 26 480304, 27 48043c, 28 492990,
 * 29 47e0a0, 30 47de72; virtuals 31 P+3Ch, 32 P+38h, 33 P+2Ch, 34 the
 * cockpit sub-object +0Ch, 35 the model +20h; real passes only: 36 479344,
 * 37 41a6d0, 38 41a994, 39 40f540, 40 40e4dc, 41 the target +24h,
 * 42 410d04, 43 410ae4, 44 41074c, 45 4110ac, 46 the weapon +38h;
 * order-chain probe only: 47 a group member's order method +28h. */
static const uint32_t ix_addresses[IX_IDS]={0x45feb8,0x432b14,0x45c524,0x477904,0x401c74,0x41bb9c,0x49288c,0x47e0b0,
    0x4160dc,0x41a550,0x41a808,0x41b728,0x41ef14,0x41e8d4,0x415608,0x49681a,0x45b734,0x43349c,0x4333c8,0x429820,0x432bc8,
    0x42a3f4,0x446e3c,0x410f78,0x47ebb8,0x47df19,0x480304,0x48043c,0x492990,0x47e0a0,0x47de72,0,0,0,0,0,
    0x479344,0x41a6d0,0x41a994,0x40f540,0x40e4dc,0,0x410d04,0x410ae4,0x41074c,0x4110ac,0,0};
static const uint32_t ix_slots[IX_IDS]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0x3c,0x38,0x2c,0x0c,0x20,0,0,0,0,0,0x24,0,0,0,0,0x38,0x28};
static const unsigned ix_counts[IX_IDS]={1,2,1,0,4,5,2,2,3,4,5,4,2,2,2,3,2,1,1,0,2,2,1,2,2,1,3,3,3,2,3,1,1,2,2,2,
    3,2,2,1,6,1,1,2,2,2,2,3};
/* Frame-pointer arguments per id (bit k = argument k). */
static unsigned ix_frame_args(unsigned id,const uint32_t *a);
static uint32_t ix_next(void){
    uint32_t z=ix_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *ix_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static BYTE *ix_global(uint32_t va){return mapped+(va-0x400000u);}
static int ix_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static unsigned ix_frame_args(unsigned id,const uint32_t *a){
    switch(id){
    case 15:case 25:return 1u;
    case 11:case 12:return 2u;
    case 26:return 7u;
    case 27:return ix_stack(a[0])?7u:6u;
    case 28:return 6u;
    default:return 0;
    }
}
static int ix_player(void){return ix_index==ES2_HOOK_INDEX_player_input;}
static uint32_t ix_P(void){return IX_BASE+IX_P;}
static uint32_t ix_T(void){return IX_BASE+IX_T;}
/* Globals of the three functions (saved, compared and restored). */
static const struct { uint32_t va,size; } ix_ranges[]={{0x4d24e6,14},{0x49b088,4},{0x4d2504,2},{0x4d2a70,14},{0x49f6e0,11},
    {0x4d2577,1},{0x4d1f38,256},{0x49a06e,2},{0x4d2534,2},{0x4d24c0,4},{0x4d251a,2},{0x4d257b,4},{0x4a9cfc,6},{0x49a340,1},
    {0x6c56a8,32}};
static void ix_globals(BYTE *out,int restore){
    unsigned at=0;
    for(unsigned i=0;i<sizeof(ix_ranges)/sizeof(ix_ranges[0]);++i){
        if(restore)memcpy(ix_global(ix_ranges[i].va),out+at,ix_ranges[i].size);
        else memcpy(out+at,ix_global(ix_ranges[i].va),ix_ranges[i].size);
        at+=ix_ranges[i].size;
    }
}
/* A button value: mostly 0 or 1, then actions' range, then out of range. */
static BYTE ix_button(uint32_t pick){
    switch(pick%10){case 0:case 1:case 2:case 3:case 4:return 0;case 5:case 6:return 1;
        case 7:case 8:return (BYTE)(2u+(pick>>4)%19u);default:return (BYTE)(pick>>4&1?0x15u:pick>>8);}
}
static BYTE ix_action(uint32_t pick){return pick%8?(BYTE)((pick>>3)%21u):(BYTE)(pick>>3);}
/* State the dispatcher rereads after its calls (player_input only). */
static void ix_shake(uint32_t pick){
    if(!ix_player())return;
    BYTE *p=ix_arena+IX_P,*r=ix_arena+IX_R;
    switch(pick%16){
    case 0:put16(ix_global(0x4d24f0),(uint16_t)(pick&16?0:pick&32?1:ix_next()));break;
    case 1:put32(ix_global(0x4d24e6),pick&16?ix_P():ix_T());break;
    case 2:put32(ix_global(0x4d2a70),pick&16?0:pick&32?1:ix_next());break;
    case 3:put16(ix_global(0x4d2504),(uint16_t)(pick&16?0:pick&32?1:ix_next()));break;
    case 4:put16(ix_global(0x4d24f2),(uint16_t)(pick&16?0:1));break;
    case 5:r[0x16+ix_next()%8u]=ix_button(ix_next());break;
    case 6:r[0xb]^=1;break;
    case 7:p[0x39]^=1;break;
    case 8:if(!ix_real)put32(p+0x1a4,pick&16?ix_T():0);break;
    case 9:ix_arena[IX_W+0x14]^=1;break;
    case 10:ix_arena[IX_T+0x99]^=1;break;
    case 11:put16(p+0x10,(uint16_t)ix_next());break;
    case 12:*ix_global(0x4d2577)=(BYTE)(pick&16?13:ix_next());break;
    case 13:ix_global(0x4d1f38)[ix_next()%256u]=ix_action(ix_next());break;
    case 14:put16(ix_global(0x49a06e),(uint16_t)(ix_next()%3u-1u));break;
    default:put16(ix_global(0x4d24ee),(uint16_t)(ix_next()%6u));break;
    }
}
/* Angle words at the dead-zone and clamp edges of 41ef14. */
static uint16_t ix_angle(uint32_t pick){
    static const uint16_t edges[]={0x8000,49,50,0xffcf,0xffce,84,85,0xffac,0xffab,999,1000,0xfc19,0xfc18,0,1,0xffff,0x7fff,0x8001};
    return pick%4?edges[(pick>>2)%18]:(uint16_t)ix_next();
}
static uint32_t ix_mq10(uint32_t pick,uint32_t eax){
    if(ix_player()){
        static const uint32_t edges[]={87,88,0xffffffa9u,0xffffffa8u,50,51,0xffffffceu,0xffffffcdu,0,1,0xffffffffu,0x8000};
        return pick%3?(eax&0xffff0000u)|(uint16_t)edges[(pick>>2)%12]:eax;
    }
    static const uint32_t edges[]={0x100,0xff,0x101,0xffffff00u,0xffffff01u,0xfffffeffu,0,0x7fffffffu,0x80000000u,0x10100u,0xffff0050u};
    return pick%3?edges[(pick>>2)%11]:pick%2?(eax%0x400u-0x200u):eax;
}
static void ix_perturb_servo(uint32_t pick){
    BYTE *p=ix_arena+IX_P;
    switch(pick%8){
    case 1:put16(p+0x298,(uint16_t)ix_next());break;
    case 2:put16(p+0x29a,(uint16_t)ix_next());break;
    case 3:put16(ix_global(0x4a9cfc),ix_angle(ix_next()));break;
    case 4:put16(ix_global(0x4a9d00),ix_angle(ix_next()));break;
    default:break;
    }
}
__attribute__((used,noinline)) static uint64_t __cdecl ix_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    uint32_t eax=ix_next(),edx=ix_next(),pick=ix_next();ix_ecx=ix_next();
    uint32_t input[12]={0},args[6]={0};
    /* Arguments: frame pointers relative to the first one, other stack
     * values masked; then payloads by content before the stub's effects. */
    unsigned frames=ix_frame_args(id,a),anchor=frames?(unsigned)__builtin_ctz(frames):0;
    for(unsigned k=0;k<ix_counts[id];++k)args[k]=frames>>k&1?a[k]-a[anchor]:ix_stack(a[k])?0:a[k];
    switch(id){
    case 12:for(unsigned j=0;j<3;++j)input[j]=get32(ix_host(a[1])+4*j);break;
    case 25:for(unsigned j=0;j<8;++j)input[j]=get32(ix_host(a[0])+4*j);break;
    case 26:for(unsigned j=0;j<3;++j)input[j]=get32(ix_host(a[2])+4*j);
        for(unsigned j=0;j<5;++j)input[3+j]=get32(ix_host(a[1])+4*j);break;
    case 27:for(unsigned j=0;j<3;++j)input[j]=get32(ix_host(a[0])+4*j);
        for(unsigned j=0;j<8;++j)input[3+j]=get32(ix_host(a[1])+4*j);break;
    case 28:for(unsigned j=0;j<3;++j){input[j]=get32(ix_host(a[1])+4*j);input[3+j]=get32(ix_host(a[2])+4*j);}break;
    default:break;
    }
    switch(id){
    case 0:if(pick%4==0)eax=(eax&0xffff0000u)|2u;if(pick%3==0)ix_shake(ix_next());break;
    case 2:eax=pick%3==0?0:pick%3==1?0x10000u:eax|1u;break;
    case 3:eax=IX_BASE+IX_DEV;ix_arena[IX_DEV+4]=(BYTE)(pick%2?0:1u+ix_next()%255u);break;
    case 6:if(pick%2)eax=ix_next()%0x40000u;break;
    case 7:eax=ix_mq10(pick,eax);if(pick%16==5)ix_shake(ix_next());break;
    case 9:case 10:ix_perturb_servo(pick>>3);if(pick%2)ix_shake(ix_next());break;
    case 11:if(!ix_real)for(unsigned j=0;j<12;j+=4)put32(ix_host(a[1])+j,ix_next());if(pick%2)ix_shake(ix_next());break;
    case 15:memset(ix_host(a[0]),(int)a[1],a[2]);if(pick%2)ix_shake(ix_next());break;
    case 19:eax=pick%2?0:pick%4==1?0x10000u:eax|1u;break;
    case 24:if(pick%2)for(unsigned j=0;j<32;j+=4)put32(ix_host(a[1])+j,ix_next());break;
    case 25:if(pick%2)put32(ix_host(a[0])+4*(ix_next()%8u),ix_next());break;
    case 26:for(unsigned j=0;j<12;j+=4)put32(ix_host(a[0])+j,ix_next());break;
    case 27:{ static const uint32_t ys[]={50000,50001,49999,0,0xffff3cb0u,0x7fffffffu};
        put32(ix_host(a[2]),ix_next());put32(ix_host(a[2])+4,pick%3?ys[(pick>>2)%6]:ix_next());put32(ix_host(a[2])+8,ix_next());break; }
    case 28:for(unsigned j=0;j<6;j+=2)put16(ix_host(a[0])+j,ix_angle(ix_next()));break;
    case 31:if(pick%2)ix_shake(ix_next());break;
    case 32:if(pick%4==0)eax|=0x8000u;break;
    case 35:{ static const uint16_t nodes[]={0xffff,0,1,2,3,0x8000,0xfffe};
        eax=pick%5==0?0:IX_BASE+IX_NODE;put16(ix_arena+IX_NODE+4,nodes[(pick>>3)%7]);break; }
    case 39:{ eax=IX_BASE+IX_MOUNT;for(unsigned j=0;j<6;j+=2)put16(ix_arena+IX_MOUNT+j,(uint16_t)(ix_next()%0x400u-0x200u));break; }
    case 40:if(pick%2)for(unsigned j=0;j<12;j+=4)put32(ix_host(a[2])+j,ix_next()%0x20000u-0x10000u);break;
    case 41:eax=pick%3==0?0:IX_BASE+IX_ANODE;break;
    case 1:case 4:case 5:case 8:case 13:case 14:case 16:case 17:case 18:case 20:case 21:case 22:case 23:case 33:case 34:
        if(pick%2)ix_shake(ix_next());break;
    default:break;
    }
    if(ix_real){
        if(ix_real_count+16>IX_REAL_MAX){fail("input_dispatch real stub overflow");ExitProcess(1);}
        uint32_t *o=ix_real_log+ix_real_count;o[0]=id;unsigned k=1;
        for(unsigned j=0;j<ix_counts[id];++j)o[k++]=args[j];
        ix_real_count+=k;
    } else {
        if(ix_calls==IX_MAX){fail("input_dispatch synthetic call overflow");ExitProcess(1);}
        ix_record *v=ix_log+ix_calls++;memset(v,0,sizeof(*v));v->id=id;
        memcpy(v->args,args,sizeof(args));memcpy(v->input,input,sizeof(input));
        for(unsigned j=0;j<3;++j)v->regs[j]=ix_stack(regs[j])?0:regs[j];
        if(ix_player()||ix_track)for(unsigned j=3;j<6;++j)v->regs[j]=regs[j];
        if(ix_track){v->frame[0]=ix_entry[0]-ix_origin;v->frame[1]=ix_entry[1]-ix_origin;}
        v->eax=eax;v->edx=edx;
    }
    return (uint64_t)edx<<32|eax;
}
/* Saves EAX/EDX/ECX/EBX/ESI/EDI for the record (and EBP/ESP for the residue
 * tracking); the C body preserves EBX, ESI and EDI, so the callee-saved
 * registers come back unchanged. */
#define IX_STUB(n) __attribute__((naked)) static void ix_stub_##n(void){__asm__ volatile( \
    "movl %ebp,_ix_entry\n\tmovl %esp,_ix_entry+4\n\t" \
    "pushl %edi\n\tpushl %esi\n\tpushl %ebx\n\tpushl %ecx\n\tpushl %edx\n\tpushl %eax\n\t" \
    "leal 28(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _ix_stub_impl\n\taddl $36,%esp\n\tmovl _ix_ecx,%ecx\n\tret\n\t");}
IX_STUB(0) IX_STUB(1) IX_STUB(2) IX_STUB(3) IX_STUB(4) IX_STUB(5) IX_STUB(6) IX_STUB(7) IX_STUB(8) IX_STUB(9)
IX_STUB(10) IX_STUB(11) IX_STUB(12) IX_STUB(13) IX_STUB(14) IX_STUB(15) IX_STUB(16) IX_STUB(17) IX_STUB(18) IX_STUB(19)
IX_STUB(20) IX_STUB(21) IX_STUB(22) IX_STUB(23) IX_STUB(24) IX_STUB(25) IX_STUB(26) IX_STUB(27) IX_STUB(28) IX_STUB(29)
IX_STUB(30) IX_STUB(31) IX_STUB(32) IX_STUB(33) IX_STUB(34) IX_STUB(35) IX_STUB(36) IX_STUB(37) IX_STUB(38) IX_STUB(39)
IX_STUB(40) IX_STUB(41) IX_STUB(42) IX_STUB(43) IX_STUB(44) IX_STUB(45) IX_STUB(46) IX_STUB(47)
#undef IX_STUB
static void *ix_stubs[IX_IDS]={ix_stub_0,ix_stub_1,ix_stub_2,ix_stub_3,ix_stub_4,ix_stub_5,ix_stub_6,ix_stub_7,ix_stub_8,
    ix_stub_9,ix_stub_10,ix_stub_11,ix_stub_12,ix_stub_13,ix_stub_14,ix_stub_15,ix_stub_16,ix_stub_17,ix_stub_18,ix_stub_19,
    ix_stub_20,ix_stub_21,ix_stub_22,ix_stub_23,ix_stub_24,ix_stub_25,ix_stub_26,ix_stub_27,ix_stub_28,ix_stub_29,ix_stub_30,
    ix_stub_31,ix_stub_32,ix_stub_33,ix_stub_34,ix_stub_35,ix_stub_36,ix_stub_37,ix_stub_38,ix_stub_39,ix_stub_40,ix_stub_41,
    ix_stub_42,ix_stub_43,ix_stub_44,ix_stub_45,ix_stub_46,ix_stub_47};
/* Jump table 460D83h (21 entries) redirected through counting trampolines. */
__attribute__((used)) static uint32_t ix_case_targets[IX_TABLE],ix_case_hits[IX_TABLE];
#define IX_CASE(n) __attribute__((naked)) static void ix_case_##n(void){__asm__ volatile( \
    "incl _ix_case_hits+" #n "*4\n\tjmp *_ix_case_targets+" #n "*4\n\t");}
IX_CASE(0) IX_CASE(1) IX_CASE(2) IX_CASE(3) IX_CASE(4) IX_CASE(5) IX_CASE(6) IX_CASE(7) IX_CASE(8) IX_CASE(9) IX_CASE(10)
IX_CASE(11) IX_CASE(12) IX_CASE(13) IX_CASE(14) IX_CASE(15) IX_CASE(16) IX_CASE(17) IX_CASE(18) IX_CASE(19) IX_CASE(20)
#undef IX_CASE
static void *ix_cases_entry[IX_TABLE]={ix_case_0,ix_case_1,ix_case_2,ix_case_3,ix_case_4,ix_case_5,ix_case_6,ix_case_7,
    ix_case_8,ix_case_9,ix_case_10,ix_case_11,ix_case_12,ix_case_13,ix_case_14,ix_case_15,ix_case_16,ix_case_17,ix_case_18,
    ix_case_19,ix_case_20};
static int ix_table(int install){
    BYTE *t=mapped+0x60d83u;DWORD old,ignored;
    if(!VirtualProtect(t,4*IX_TABLE,PAGE_EXECUTE_READWRITE,&old))return fail("input_dispatch table protection");
    for(unsigned k=0;k<IX_TABLE;++k){
        if(install){ix_case_targets[k]=get32(t+4*k);ix_case_hits[k]=0;put32(t+4*k,(uint32_t)(uintptr_t)ix_cases_entry[k]);}
        else put32(t+4*k,ix_case_targets[k]);
    }
    if(!VirtualProtect(t,4*IX_TABLE,old,&ignored))return fail("input_dispatch table restore protection");
    FlushInstructionCache(GetCurrentProcess(),t,4*IX_TABLE);return 0;
}
static void ix_reset(void){ix_calls=ix_real_count=0;ix_stream=ix_seed;}
static uint16_t ix_word(const uint16_t *edges,unsigned count){
    return random32()%4?edges[random32()%count]:(uint16_t)random32();
}
static int32_t ix_small(uint32_t range){return (int32_t)(random32()%(2u*range+1u))-(int32_t)range;}
/* A matrix block: nine words of a plausible rotation (or random words),
 * two padding bytes, a translation. */
static void ix_matrix(BYTE *m,int real){
    for(unsigned j=0;j<18;j+=2)put16(m+j,real?(uint16_t)ix_small(0x2000):(uint16_t)random32());
    m[18]=(BYTE)random32();m[19]=(BYTE)random32();
    for(unsigned j=0;j<3;++j)put32(m+20+4*j,real?(uint32_t)ix_small(0x4000):random32());
}
static void ix_vehicle(BYTE *v,int real){
    uint32_t b=IX_BASE;
    put32(v,b+IX_VT_P);put32(v+0x1f2,b+IX_CFG);put32(v+0x202,b+IX_W);put32(v+0x34,b+IX_M);
    v[0x39]=(BYTE)(random32()%5?1:0);
    for(unsigned j=0;j<3;++j)put32(v+0x26+4*j,real?(uint32_t)ix_small(1<<18):random32());
    if(real||random32()%2)put16(v+0x32,0);
    if(real)put32(v+0x30b,random32()%2?0:b+IX_S);
    put32(v+0x230,random32()%3?0:random32()|1u);put32(v+0x234,random32()%3?0:random32()|1u);
    v[0xa3]=(BYTE)(random32()%3?0:1);v[0x99]=(BYTE)(random32()%3?0:1);
    static const uint16_t words[]={0,1,0xffff,0x8000,0x7fff,0x100,0xff00,0x40,0xffc0};
    for(unsigned k=0;k<4;++k)put16(v+0x294+2*k,ix_word(words,9));
}
static void ix_build(unsigned index,uint32_t a[6],uint32_t r[3],int real){
    uint32_t b=IX_BASE;ix_seed=random32();random_bytes(ix_arena,IX_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    for(unsigned k=0;k<0x40;k+=4){put32(ix_arena+IX_VT_P+k,0);put32(ix_arena+IX_VT_SUB+k,0);put32(ix_arena+IX_VT_OBJ+k,0);put32(ix_arena+IX_VT_WPN+k,0);}
    put32(ix_arena+IX_VT_P+0x3c,(uint32_t)(uintptr_t)ix_stubs[31]);put32(ix_arena+IX_VT_P+0x38,(uint32_t)(uintptr_t)ix_stubs[32]);
    put32(ix_arena+IX_VT_P+0x2c,(uint32_t)(uintptr_t)ix_stubs[33]);put32(ix_arena+IX_VT_SUB+0x0c,(uint32_t)(uintptr_t)ix_stubs[34]);
    put32(ix_arena+IX_VT_OBJ+0x20,(uint32_t)(uintptr_t)ix_stubs[35]);put32(ix_arena+IX_VT_P+0x24,(uint32_t)(uintptr_t)ix_stubs[41]);
    put32(ix_arena+IX_VT_WPN+0x38,(uint32_t)(uintptr_t)ix_stubs[46]);
    BYTE *p=ix_arena+IX_P,*t=ix_arena+IX_T,*c=ix_arena+IX_CFG,*w=ix_arena+IX_W;
    ix_vehicle(p,real);ix_vehicle(t,real);
    put32(p+0x1a4,real||random32()%3?b+IX_T:0);put32(t+0x1a4,b+IX_P);
    /* Servo configuration (C = CFG+2), chassis word +50h, node word +0Ch, mount z +66h. */
    static const uint16_t scales[]={0x100,0x80,0x200,0x40,0,1,0x7fff,0x8000,0xffff};
    static const uint16_t bounds[]={0x1000,0x2000,0x800,0,1,0x7fff,0x8000,0xffff,0xf000,0xe000};
    for(unsigned k=0;k<2;++k){put16(c+2+0x1c+8*k,ix_word(scales,9));put16(c+2+0x1e + 8*k,(uint16_t)(random32()%2?random32()%0x400u:random32()));}
    put16(c+2+0x20,ix_word(bounds,10));put16(c+2+0x28,ix_word(bounds,10));put16(c+2+0x2a,ix_word(bounds,10));
    put16(c+0x50,(uint16_t)(random32()%4?0:random32()|1u));put16(c+0x66,(uint16_t)(random32()%2?random32()%0x100u:random32()));
    /* Weapons: list of IX_SLOTS (dense in the real passes), count, selection. */
    put32(w+4,b+IX_LIST);
    for(unsigned i=0;i<IX_SLOTS;++i)put32(ix_arena+IX_LIST+4*i,!real&&random32()%4==0?0:b+IX_S+0x50u*(random32()%4));
    for(unsigned k=0;k<4;++k)put32(ix_arena+IX_S+0x50*k,b+IX_VT_WPN);
    put16(w+8,(uint16_t)(random32()%(IX_SLOTS+1u)));
    w[0x1d]=(BYTE)(random32()%3?random32()%IX_SLOTS:0xff);w[0x14]=(BYTE)(random32()%2);
    /* Model, node matrices, cockpit, record. */
    put32(ix_arena+IX_M+4,b+IX_OBJ);put32(ix_arena+IX_M+0x16,b+IX_MATS);put32(ix_arena+IX_OBJ,b+IX_VT_OBJ);
    for(unsigned k=0;k<4;++k)ix_matrix(ix_arena+IX_MATS+0x20*k,real);
    if(real){for(unsigned j=0;j<3;++j)put32(ix_arena+IX_ANODE+0x14+4*j,(uint32_t)ix_small(0x4000));}
    for(unsigned j=0;j<3;++j)put32(ix_arena+IX_POINT+4*j,real?(uint32_t)ix_small(1<<18):random32());
    BYTE *h=ix_arena+IX_H,*rc=ix_arena+IX_RECT,*rr=ix_arena+IX_R;
    put32(h+0x1e1,b+IX_SUB);put32(h+0x1ed,b+IX_PANEL);put32(h+0x1f5,b+IX_RECT);put32(ix_arena+IX_SUB,b+IX_VT_SUB);
    int32_t x0=ix_small(1000),y0=ix_small(1000),x1=x0+(int32_t)(random32()%600u)-50,y1=y0+(int32_t)(random32()%600u)-50;
    put32(rc+4,(uint32_t)x0);put32(rc+8,(uint32_t)y0);put32(rc+0xc,(uint32_t)x1);put32(rc+0x10,(uint32_t)y1);
    static const int32_t offsets[]={-1,0,1};
    int32_t x=random32()%3==0?x0+offsets[random32()%3]:random32()%2?x1+offsets[random32()%3]:x0+ix_small(400);
    int32_t y=random32()%3==0?y0+offsets[random32()%3]:random32()%2?y1+offsets[random32()%3]:y0+ix_small(400);
    put32(rr+2,(uint32_t)x);put32(rr+6,(uint32_t)y);
    put32(rr+0x22,b+IX_WORDS);put32(rr+0x26,b+IX_WORDS+2);
    rr[0xb]=(BYTE)(random32()%3?random32()%2:random32());
    static const uint16_t axes[]={0,0,0,1,0xffff,0x100,0xff00,0x7fff,0x8000};
    for(unsigned k=0;k<4;++k)put16(rr+0xe + 2*k,ix_word(axes,9));
    for(unsigned k=0;k<8;++k)rr[0x16+k]=ix_button(random32());
    /* Globals. */
    put32(ix_global(0x4d24e6),random32()%8?b+IX_P:b+IX_T);put32(ix_global(0x49b088),b+IX_H);
    put16(ix_global(0x4d24f0),(uint16_t)(random32()%5<3?0:random32()%2?1:random32()));
    put32(ix_global(0x4d24ea),random32());put16(ix_global(0x4d24f2),(uint16_t)(random32()%3?0:random32()%2?1:random32()));
    put16(ix_global(0x4d24ee),(uint16_t)(random32()%4?random32()%6u:random32()));
    put16(ix_global(0x4d2504),(uint16_t)(random32()%3?0:random32()%2?1:random32()));
    put32(ix_global(0x4d2a70),random32()%2?0:random32()%2?1:random32());put16(ix_global(0x4d2a74),(uint16_t)random32());
    put32(ix_global(0x4d2a76),(uint32_t)x+(random32()%4?(uint32_t)ix_small(2000):random32()));
    put32(ix_global(0x4d2a7a),(uint32_t)y+(random32()%4?(uint32_t)ix_small(2000):random32()));
    for(unsigned k=0;k<2;++k)put32(ix_global(0x49f6e0+4*k),random32()%2?0:random32()%2?(uint32_t)ix_small(300):random32());
    *ix_global(0x49f6e8)=(BYTE)(random32()%2);*ix_global(0x49f6e9)=(BYTE)(random32()%2);*ix_global(0x49f6ea)=(BYTE)(random32()%2);
    *ix_global(0x4d2577)=(BYTE)(random32()%4?13:random32());
    for(unsigned k=0;k<256;++k)ix_global(0x4d1f38)[k]=ix_action(random32());
    *ix_global(0x4d1f46)=(BYTE)(random32()%3?1:random32());
    put16(ix_global(0x49a06e),(uint16_t)(random32()%4?random32()%3u-1u:random32()));
    static const uint32_t frames[]={49,50,51,0,0x80000000u};
    put32(ix_global(0x4d257b),random32()%4?frames[random32()%5]:random32());
    for(unsigned j=0;j<6;j+=2)put16(ix_global(0x4a9cfc)+j,(uint16_t)random32());
    *ix_global(0x49a340)=(BYTE)(random32()%2);
    ix_matrix(ix_global(0x6c56a8),real);
    if(index==ES2_HOOK_INDEX_torso_center){
        static const uint16_t angles[]={0,1,0xffff,0x100,0xff00,0x7fff,0x8000,0x40,0xffc0};
        put16(p+0x298,ix_word(angles,9));put16(p+0x29a,ix_word(angles,9));
        static const uint32_t distances[]={0,1,0x5000,0xffffffffu};
        a[0]=b+IX_P;a[1]=random32()%2?distances[random32()%4]:random32();
        if(real)a[1]=(uint32_t)((int32_t)(a[1]<<7)>>7);
        return;
    }
    if(index==ES2_HOOK_INDEX_torso_aim){a[0]=b+IX_P;a[1]=b+IX_POINT;return;}
    a[0]=b+IX_R;
}
/* Calls the original with chosen EAX/EDX/ECX (or a 31/a7 stack and
 * registers), EBX = ESI = EDI = 0; five argument slots. */
__attribute__((naked)) static void ix_machine(void *target,const uint32_t *a,const uint32_t *registers,uint32_t pattern,
    uint32_t pair[2]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl 20(%ebp),%eax\n\ttestl %eax,%eax\n\tjz 1f\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl $1024,%ecx\n\trep stosl\n\taddl $3840,%esp\n\t"
        "1: movl 12(%ebp),%esi\n\tpushl 16(%esi)\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\t"
        "xorl %ebx,%ebx\n\txorl %esi,%esi\n\txorl %edi,%edi\n\tcall *8(%ebp)\n\t"
        "movl 24(%ebp),%ecx\n\tmovl %eax,(%ecx)\n\tmovl %edx,4(%ecx)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static int ix_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const ix_record *expected,
    const BYTE *globals_expected,const BYTE *globals_actual){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,ix_calls);
    for(unsigned j=0;j<calls&&j<ix_calls&&expected;++j)if(memcmp(expected+j,ix_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(ix_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<IX_SIZE;++j)if(ix_after[j]!=ix_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,ix_after[j],ix_arena[j]);break;}
    if(globals_expected)for(unsigned j=0;j<IX_GLOBALS;++j)if(globals_expected[j]!=globals_actual[j]){
        fprintf(stderr," global byte %u %02x/%02x\n",j,globals_expected[j],globals_actual[j]);break;}
    return 1;
}
static unsigned ix_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<ix_calls;++j){unsigned id=ix_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?ix_slots[id]&&ix_slots[id]==c->key:!ix_slots[id]&&ix_addresses[id]==c->key)++hits;}
    return hits;
}
static BYTE ix_g_before[IX_GLOBALS],ix_g_after[IX_GLOBALS],ix_g_actual[IX_GLOBALS];
static int ix_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    static ix_record expected[IX_MAX];
    memcpy(ix_before,ix_arena,IX_SIZE);ix_globals(ix_g_before,0);
    uint32_t got[2];ix_reset();ix_machine(mapped+d->rva,a,r,0,got);
    uint64_t pair[2];pair[0]=(uint64_t)got[1]<<32|got[0];unsigned calls=ix_calls;memcpy(expected,ix_log,calls*sizeof(*expected));
    memcpy(ix_after,ix_arena,IX_SIZE);ix_globals(ix_g_after,0);
    memcpy(ix_arena,ix_before,IX_SIZE);ix_globals(ix_g_before,1);ix_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[32];
    for(unsigned i=0;i<count&&i<32;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);ix_globals(ix_g_actual,0);
    if(n<4096)for(unsigned i=0;i<count&&i<32;++i)if(c[i].count-counts[i]!=ix_expected_hits(c+i))return fail("input_dispatch callee counter delta");
    if(pair[0]!=pair[1]||calls!=ix_calls||memcmp(expected,ix_log,calls*sizeof(*expected))||memcmp(ix_after,ix_arena,IX_SIZE)||
       memcmp(ix_g_after,ix_g_actual,IX_GLOBALS))
        return ix_report("direct",n,pair,calls,expected,ix_g_after,ix_g_actual);
    return 0;
}
static uint64_t ix_which(unsigned index){
    if(index==ES2_HOOK_INDEX_torso_center)return 1ull<<7|1ull<<9|1ull<<10;
    if(index==ES2_HOOK_INDEX_torso_aim)return 1ull<<7|1ull<<9|1ull<<10|0x7full<<24;
    return 0xffffffull;
}
static int ix_patch(BYTE saved[IX_IDS][5],int active[IX_IDS],uint64_t which,int on){
    for(unsigned i=0;i<IX_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!ix_addresses[i])continue;if(actor_patch(ix_addresses[i]-0x400000u,ix_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(ix_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
static int ix_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;ix_build(index,a,r,0);
        BYTE expected_fpu[108],actual_fpu[108];static ix_record expected[IX_MAX];
        memcpy(ix_before,ix_arena,IX_SIZE);ix_globals(ix_g_before,0);
        ix_reset();ix_machine(mapped+d->rva,a,r,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=ix_calls;memcpy(expected,ix_log,calls*sizeof(*expected));memcpy(ix_after,ix_arena,IX_SIZE);ix_globals(ix_g_after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("input_dispatch poison install");
        memcpy(ix_arena,ix_before,IX_SIZE);ix_globals(ix_g_before,1);ix_reset();ix_machine(mapped+d->rva,a,r,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);ix_globals(ix_g_actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=ix_calls||memcmp(expected,ix_log,calls*sizeof(*expected))||memcmp(ix_after,ix_arena,IX_SIZE)||
           memcmp(ix_g_after,ix_g_actual,IX_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=ix_report(fast?"installed fast":"installed preserving",n,pair,calls,expected,ix_g_after,ix_g_actual);
        if(!es2_hook_uninstall(&h))return fail("input_dispatch poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state\n",d->name,total);
    return status;
}
/* Calls target with five slots over a 4-KiB 31/a7 fill, EAX/EDX/ECX and
 * EBX/ESI/EDI set to distinct pattern words; keeps the slot address in
 * ix_origin, returns EAX/EDX/ECX in out and copies the 1024 bytes below the
 * slots after return (the callee's frame and what its callees left). */
__attribute__((naked,noinline)) static void ix_track_invoke(void *target,const uint32_t *a,uint32_t pattern,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 16(%ebp),%eax\n\tmovl $1024,%ecx\n\trep stosl\n\taddl $4080,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 16(%esi)\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl %esp,_ix_origin\n\tmovl 16(%ebp),%eax\n\t"
        "movl %eax,%edx\n\txorl $0x01010101,%edx\n\tmovl %eax,%ecx\n\txorl $0x02020202,%ecx\n\tmovl %eax,%ebx\n\txorl $0x04040404,%ebx\n\t"
        "movl %eax,%esi\n\txorl $0x08080808,%esi\n\tmovl %eax,%edi\n\txorl $0x10101010,%edi\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%ebx\n\tmovl %eax,(%ebx)\n\tmovl %edx,4(%ebx)\n\tmovl %ecx,8(%ebx)\n\t"
        "leal -1024(%esp),%esi\n\tmovl $_ix_window,%edi\n\tmovl $256,%ecx\n\tcld\n\trep movsl\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* fire_line 415608 for the player_input probe: copy the 1024 bytes below
 * its entry before anything writes there, then the logging stub. */
__attribute__((naked)) static void ix_stub_fire_window(void){__asm__ volatile(
    "movl %esi,_ix_tmp\n\tmovl %edi,_ix_tmp+4\n\tmovl %ecx,_ix_tmp+8\n\t"
    "leal -1024(%esp),%esi\n\tmovl $_ix_fire_window,%edi\n\tmovl $256,%ecx\n\tcld\n\trep movsl\n\t"
    "movl _ix_tmp,%esi\n\tmovl _ix_tmp+4,%edi\n\tmovl _ix_tmp+8,%ecx\n\tincl _ix_fire_hits\n\tjmp _ix_stub_14\n\t");}
static int ix_window_report(const char *what,unsigned n,const BYTE *expected,const BYTE *actual){
    fprintf(stderr,"FAIL: %s case %u (%s)\n",current_function,n,what);
    for(unsigned j=0;j<1024;++j)if(expected[j]!=actual[j]){fprintf(stderr," below-%u: %02x/%02x\n",1024-j,expected[j],actual[j]);break;}
    return 1;
}
/* torso_aim's residue probe: its callees are the logging stubs, which record
 * EBX/ESI/EDI and EBP/ESP (relative to the caller's slots) at every entry. The
 * original 41ef14 and the installed replacement (fast and preserving dispatch,
 * 31/a7 stack, distinct caller registers) must leave the same 1024 bytes below
 * the caller's slots: torso_aim's frame (the bytes the weapon bodies under
 * aim_fire and player_input's fire path read, ai.md, "residue shims for
 * torso_aim and weapon_choose") and the stubs' frames at the original
 * coordinate. An adapter on the host-stack bridge with its own frame fails
 * at case 0. */
static int ix_torso_residue_probe(void){
    const unsigned index=ES2_HOOK_INDEX_torso_aim;const es2_hook_descriptor *d=function_hook_descriptor(index);
    int status=0;unsigned total=0;static ix_record expected[IX_MAX];static BYTE expected_window[1024];ix_track=1;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<256&&!status;++n){
        uint32_t a[6],r[3],expected_out[3],actual_out[3],pattern=poison?0xa7a7a7a7u:0x31313131u;ix_build(index,a,r,0);
        memcpy(ix_before,ix_arena,IX_SIZE);ix_globals(ix_g_before,0);
        ix_reset();ix_track_invoke(mapped+d->rva,a,pattern,expected_out);memcpy(expected_window,ix_window,1024);
        unsigned calls=ix_calls;memcpy(expected,ix_log,calls*sizeof(*expected));memcpy(ix_after,ix_arena,IX_SIZE);ix_globals(ix_g_after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("input_dispatch residue install");
        memcpy(ix_arena,ix_before,IX_SIZE);ix_globals(ix_g_before,1);ix_reset();ix_track_invoke(mapped+d->rva,a,pattern,actual_out);
        ix_globals(ix_g_actual,0);
        uint64_t pair[2]={(uint64_t)expected_out[1]<<32|expected_out[0],(uint64_t)actual_out[1]<<32|actual_out[0]};
        if(pair[0]!=pair[1]||expected_out[2]!=actual_out[2]||calls!=ix_calls||memcmp(expected,ix_log,calls*sizeof(*expected))||
           memcmp(ix_after,ix_arena,IX_SIZE)||memcmp(ix_g_after,ix_g_actual,IX_GLOBALS)||h.calls!=1)
            status=ix_report(fast?"residue fast":"residue preserving",n,pair,calls,expected,ix_g_after,ix_g_actual);
        else if(memcmp(expected_window,ix_window,1024))status=ix_window_report("torso_aim frame below the slots",n,expected_window,ix_window);
        if(!es2_hook_uninstall(&h))return fail("input_dispatch residue uninstall");ix_globals(ix_g_before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);ix_track=0;
    if(!status)printf("PASS: torso_aim %u residue cases, fast/preserving dispatch, 31/a7 stack, per-call EBX/ESI/EDI/EBP/ESP, ECX at return, 1024 bytes below the slots\n",total);
    return status;
}
/* player_input's fire path over torso_aim: the original 460870 with its callees
 * as logging stubs, except the real 41ef14 (its callees stubbed) and 415608,
 * whose stub copies the 1024 bytes below its entry: on the tracking path 460c8f
 * -> 460cf2 fire_line is entered at the same coordinate as torso_aim, and below
 * it 410dbc and the weapon bodies read torso_aim's frame bytes (ai.md, "residue
 * shims for torso_aim and weapon_choose"). The reference run uses the original
 * torso_aim, the compared run the installed replacement (fast and preserving
 * dispatch); the window below 415608, the stub records with registers and
 * coordinates, pair, arena and globals must be equal. */
/* State for the tracking path in half of the cases: player P in cockpit
 * view (4D24F0h = 0) with zero twist/pitch axes (R+12h, R+14h) and no
 * mouse step (R+0Bh = 0), ground chassis (CFG+50h = 0) with P+39h set, no
 * pending turn (4D2A70h = 0), a target whose byte +99h is clear and the
 * tracking byte W+14h set: 460c50 -> 41b728, 41ef14, then 415608. */
static void ix_fire_path(void){
    BYTE *p=ix_arena+IX_P,*rr=ix_arena+IX_R;
    put32(ix_global(0x4d24e6),ix_P());put16(ix_global(0x4d24f0),0);put32(ix_global(0x4d2a70),0);
    put16(rr+0x12,0);put16(rr+0x14,0);rr[0xb]=0;p[0x39]=1;put16(ix_arena+IX_CFG+0x50,0);
    put32(p+0x1a4,ix_T());ix_arena[IX_T+0x99]=0;ix_arena[IX_W+0x14]=1;
}
static int ix_player_residue_probe(void){
    const unsigned index=ES2_HOOK_INDEX_player_input,torso=ES2_HOOK_INDEX_torso_aim;
    const es2_hook_descriptor *d=function_hook_descriptor(index);
    BYTE saved[IX_IDS][5],fsaved[5];int active[IX_IDS];
    if(ix_patch(saved,active,(ix_which(index)|ix_which(torso))&~(1ull<<12|1ull<<14),1))return 1;
    if(actor_patch(0x15608,ix_stub_fire_window,fsaved,TRUE))return 1;
    int status=0;unsigned total=0,fired=0;static ix_record expected[IX_MAX];static BYTE expected_window[1024];ix_track=1;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned n=0;n<2048&&!status;++n){
        uint32_t a[6],r[3],expected_out[3],actual_out[3],pattern=n&1?0xa7a7a7a7u:0x31313131u;ix_build(index,a,r,0);
        if(n%4<2)ix_fire_path();
        memcpy(ix_before,ix_arena,IX_SIZE);ix_globals(ix_g_before,0);
        ix_reset();ix_fire_hits=0;memset(ix_fire_window,0,1024);ix_track_invoke(mapped+d->rva,a,pattern,expected_out);
        const unsigned hits=ix_fire_hits;memcpy(expected_window,ix_fire_window,1024);
        unsigned calls=ix_calls;memcpy(expected,ix_log,calls*sizeof(*expected));memcpy(ix_after,ix_arena,IX_SIZE);ix_globals(ix_g_after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);
        if(!weapons_install_hook(&h,function_hook_descriptor(torso),&o,function_hook_extension(torso)))return fail("input_dispatch fire install");
        memcpy(ix_arena,ix_before,IX_SIZE);ix_globals(ix_g_before,1);ix_reset();ix_fire_hits=0;memset(ix_fire_window,0,1024);
        ix_track_invoke(mapped+d->rva,a,pattern,actual_out);ix_globals(ix_g_actual,0);
        uint64_t pair[2]={(uint64_t)expected_out[1]<<32|expected_out[0],(uint64_t)actual_out[1]<<32|actual_out[0]};
        int torso_seen=0;for(unsigned j=0;j<calls;++j)torso_seen|=expected[j].id==24||expected[j].id==35;
        if(pair[0]!=pair[1]||calls!=ix_calls||memcmp(expected,ix_log,calls*sizeof(*expected))||memcmp(ix_after,ix_arena,IX_SIZE)||
           memcmp(ix_g_after,ix_g_actual,IX_GLOBALS)||hits!=ix_fire_hits||(torso_seen&&!h.calls))
            status=ix_report(fast?"fire path fast":"fire path preserving",n,pair,calls,expected,ix_g_after,ix_g_actual);
        else if(memcmp(expected_window,ix_fire_window,1024))status=ix_window_report("below fire_line after torso_aim",n,expected_window,ix_fire_window);
        if(!es2_hook_uninstall(&h))return fail("input_dispatch fire uninstall");ix_globals(ix_g_before,1);++total;
        if(hits&&h.calls)++fired;
    }
    es2_hook_set_fast_path(TRUE);ix_track=0;
    if(actor_patch(0x15608,NULL,fsaved,FALSE))return 1;
    if(ix_patch(saved,active,0,0))return 1;
    if(!status&&fired<total/8){fprintf(stderr,"FAIL: player_input fire path after torso_aim reached %u times\n",fired);status=1;}
    if(!status)printf("PASS: player_input %u fire-path cases (%u with torso_aim then fire_line), original vs replaced torso_aim, fast/preserving dispatch, 1024 bytes below fire_line, stub registers and coordinates\n",total,fired);
    return status;
}
/* The key codes of the live suite c3_09-wingman-orders (Alt+O/C/E/ F/H/G/A,
 * Esc, 1/2/3, the map letters D/A/F/T/G/O/C/E, Enter, Tab, X, the cursor keys
 * Up/Down) and no key. */
static const uint16_t ix_script_codes[]={0x218,0x22e,0x212,0x221,0x223,0x222,0x21e,0x01,0x02,0x03,0x04,0x20,0x1e,
    0x21,0x14,0x22,0x18,0x2e,0x12,0x1c,0x0f,0x2d,0x48,0x50,0};
/* The command handler stub for the frame probe: copy the 1024 bytes below
 * its first entry (the stack its callees find, among them 4231a4's
 * uninitialized priority word at entry-12Ch on the MFD chain 432bc8 ->
 * 446c38 -> 447248 -> 43166c), then the logging stub. */
__attribute__((used)) static BYTE ix_command_window[1024];
__attribute__((used)) static unsigned ix_command_hits;
__attribute__((naked)) static void ix_stub_command_window(void){__asm__ volatile(
    "cmpl $0,_ix_command_hits\n\tjne 1f\n\t"
    "movl %esi,_ix_tmp\n\tmovl %edi,_ix_tmp+4\n\tmovl %ecx,_ix_tmp+8\n\t"
    "leal -1024(%esp),%esi\n\tmovl $_ix_command_window,%edi\n\tmovl $256,%ecx\n\tcld\n\trep movsl\n\t"
    "movl _ix_tmp,%esi\n\tmovl _ix_tmp+4,%edi\n\tmovl _ix_tmp+8,%ecx\n\t"
    "1: incl _ix_command_hits\n\tjmp _ix_stub_0\n\t");}
/* player_input's frame probe: every callee a logging stub that records
 * EBX/ESI/EDI and EBP/ESP relative to the caller's slots, the command handler's
 * stub also the 1024 bytes below its first entry; the record code is one of the
 * script's keys in three of four cases. The original 460870 and the installed
 * replacement (its entry shim; fast and preserving dispatch; 31/a7 stack;
 * distinct caller EBX/ESI/EDI, so the caller's EDI must reach 45feb8 and
 * 432b14) must agree in the records, the pair and ECX, arena, globals, the
 * window below 45feb8 and the 1024 bytes below the caller's slots (460870's
 * frame and the stubs' frames). An adapter on the host-stack bridge (EDI = 0)
 * fails at the first call. */
static int ix_player_frame_probe(void){
    const unsigned index=ES2_HOOK_INDEX_player_input;const es2_hook_descriptor *d=function_hook_descriptor(index);
    BYTE saved[IX_IDS][5],csaved[5];int active[IX_IDS];
    if(ix_patch(saved,active,ix_which(index)&~1ull,1))return 1;
    if(actor_patch(0x5feb8,ix_stub_command_window,csaved,TRUE))return 1;
    int status=0;unsigned total=0,coded=0;static ix_record expected[IX_MAX];static BYTE expected_window[1024],expected_command[1024];
    ix_track=1;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned n=0;n<1024&&!status;++n){
        uint32_t a[6],r[3],expected_out[3],actual_out[3],pattern=n&1?0xa7a7a7a7u:0x31313131u;ix_build(index,a,r,0);
        if(n%4!=3){put16(ix_arena+IX_R,ix_script_codes[(n>>2)%(sizeof(ix_script_codes)/2)]);++coded;}
        memcpy(ix_before,ix_arena,IX_SIZE);ix_globals(ix_g_before,0);
        ix_reset();ix_command_hits=0;memset(ix_command_window,0,1024);ix_track_invoke(mapped+d->rva,a,pattern,expected_out);
        const unsigned hits=ix_command_hits;memcpy(expected_window,ix_window,1024);memcpy(expected_command,ix_command_window,1024);
        unsigned calls=ix_calls;memcpy(expected,ix_log,calls*sizeof(*expected));memcpy(ix_after,ix_arena,IX_SIZE);ix_globals(ix_g_after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("input_dispatch frame install");
        memcpy(ix_arena,ix_before,IX_SIZE);ix_globals(ix_g_before,1);ix_reset();ix_command_hits=0;memset(ix_command_window,0,1024);
        ix_track_invoke(mapped+d->rva,a,pattern,actual_out);ix_globals(ix_g_actual,0);
        uint64_t pair[2]={(uint64_t)expected_out[1]<<32|expected_out[0],(uint64_t)actual_out[1]<<32|actual_out[0]};
        if(pair[0]!=pair[1]||calls!=ix_calls||memcmp(expected,ix_log,calls*sizeof(*expected))||memcmp(ix_after,ix_arena,IX_SIZE)||
           memcmp(ix_g_after,ix_g_actual,IX_GLOBALS)||hits!=ix_command_hits||h.calls!=1)
            status=ix_report(fast?"frame fast":"frame preserving",n,pair,calls,expected,ix_g_after,ix_g_actual);
        else if(memcmp(expected_command,ix_command_window,1024))status=ix_window_report("below the command handler",n,expected_command,ix_command_window);
        else if(memcmp(expected_window,ix_window,1024))status=ix_window_report("player_input frame below the slots",n,expected_window,ix_window);
        if(!es2_hook_uninstall(&h))return fail("input_dispatch frame uninstall");ix_globals(ix_g_before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);ix_track=0;
    if(actor_patch(0x5feb8,NULL,csaved,FALSE))return 1;
    if(ix_patch(saved,active,0,0))return 1;
    if(!status)printf("PASS: player_input %u frame cases (%u with the c3_09-wingman-orders key codes), fast/preserving dispatch, 31/a7 stack, per-call EBX/ESI/EDI/EBP/ESP (caller's EDI at 45feb8/432b14), 1024 bytes below 45feb8 and below the slots\n",total,coded);
    return status;
}
/* player_input over the real command handler down to the group order (the live
 * Alt+O difference): 45feb8, its runtime frame 49630c (counter [6CBEE4h] in the
 * arena), the HUD key handler 432bc8 with 429820, the playback key check
 * 45fe6c, the MFD key method 446c38 with the menu helpers 43f9e0/43f9c0/43f9f8,
 * 447248, 447158, 43166c and the group order 4231a4 run as original code;
 * dist_gate, memset, the gadget offers (+0Ch), the key method (+2Ch), the
 * members' order method +28h (id 47) and the other callees of 460870 are
 * logging stubs. 4231a4 reads its priority word EBP-1Ch before writing it for
 * codes 3, 6 and 9..0Eh (byte table 423239h), i.e. for Alt+O (item 3: code 3,
 * or 6 with the item's flag bit 1): that word lies 1D4h below the caller's
 * slots and holds the 31/a7 fill, so the original gives the order with 3131h
 * and refuses it with A7A7h (< -1). The replacement must do the same with
 * player_input, command_key and hud_key in each of the seven replaced
 * combinations (all three: the live configuration): records with registers and
 * coordinates, pair, arena (the MFD, the menu, the members), globals (the order
 * record 4D03D4h), FS:0 and the 1024 bytes below the slots. Keys: the script's,
 * except T (14h), whose lamp path in 45feb8 needs the commands group's stubs
 * (commands_cases.h); Alt+O in half of the cases. */
static const struct { uint32_t va,size; } ix_chain_ranges[]={{0x4d2564,2},{0x4cf99c,4},{0x49b0f8,4},{0x4d03d4,6},{0x49ef34,4},
    {0x4d2684,4},{0x4d2538,1},{0x6cbee4,4},{0x4d24d6,2},{0x4d252c,2}};
enum { IX_CHAIN_GLOBALS=33 };
static void ix_chain_globals(BYTE *out,int restore){
    unsigned at=0;
    for(unsigned i=0;i<sizeof(ix_chain_ranges)/sizeof(ix_chain_ranges[0]);++i){
        if(restore)memcpy(ix_global(ix_chain_ranges[i].va),out+at,ix_chain_ranges[i].size);
        else memcpy(out+at,ix_global(ix_chain_ranges[i].va),ix_chain_ranges[i].size);
        at+=ix_chain_ranges[i].size;
    }
}
static uint32_t ix_chain_build(unsigned n){
    const uint32_t b=IX_BASE;BYTE *h=ix_arena+IX_H,*mfd=ix_arena+IX_MFD,*menu=ix_arena+IX_MENU;
    static const uint16_t chain_codes[]={0x22e,0x212,0x221,0x223,0x222,0x21e,0x01,0x02,0x03,0x04,0x20,0x1e,0x21,0x22,
        0x18,0x2e,0x12,0x1c,0x0f,0x2d,0x48,0x50,0};
    const uint32_t code=n%2?0x218u:chain_codes[(n>>1)%(sizeof(chain_codes)/2)];
    put16(ix_arena+IX_R,(uint16_t)code);
    /* Cockpit: active, the MFD, the gadgets (all with the +0Ch stub), no
     * panel weapons, the player at +203h without a lock weapon. */
    h[0x20f]=0;put32(h+0x1ed,b+IX_MFD);put32(h+0x1d8,random32()%2?0:b+IX_SUB);
    put32(h+0x1e5,b+IX_SUB);put32(h+0x1e9,b+IX_SUB);put32(h+0x1f1,b+IX_SUB);put32(h+0x1f9,b+IX_SUB);
    put32(ix_arena+IX_RECT,b+IX_VT_SUB);memset(h+0x70,0,40);put32(h+0x203,b+IX_P);put32(ix_arena+IX_P+0x30b,0);
    /* MFD: mode +C1h not 1/3/4 (447158 idle, Enter -> 43349c), menu, owner. */
    put32(mfd+0x14,b+IX_H);put32(mfd+0xc1,random32()%2?0:2);put32(mfd+0xd1,b+IX_MENU);
    for(unsigned k=0;k<6;++k)menu[0x2c+k]=(BYTE)random32();
    put32(menu+0x32,random32()%9);
    put32(ix_arena+IX_D+0x14,random32()%5);put32(ix_arena+IX_CAM+0x14,0);
    /* The player's group: up to three members, mostly eligible. */
    put32(ix_arena+IX_GROUP+0xc,b+IX_MEMBERS);put16(ix_arena+IX_GROUP+0x10,(uint16_t)(random32()%4));
    for(unsigned k=0;k<3;++k){BYTE *x=ix_arena+IX_MEMBER+0x100*k;
        put32(ix_arena+IX_MEMBERS+4*k,b+IX_MEMBER+0x100u*k);put32(x,b+IX_VT_MEMBER);put32(x+0x4d,b+IX_MEMBER+0x100u*k+0xe0u);
        x[0x99]=(BYTE)(random32()%4==0);}
    memset(ix_arena+IX_VT_MEMBER,0,0x40);put32(ix_arena+IX_VT_MEMBER+0x28,(uint32_t)(uintptr_t)ix_stubs[47]);
    put16(ix_global(0x4d2564),1);put32(ix_global(0x4cf99c),b+IX_D);put32(ix_global(0x49b0f8),b+IX_GROUP);
    put32(ix_global(0x49ef34),random32()%2);put32(ix_global(0x4d2684),b+IX_T);*ix_global(0x4d2538)=(BYTE)(random32()%3);
    put32(ix_global(0x6cbee4),b+IX_COUNTER);put16(ix_global(0x4d24d6),0);put16(ix_global(0x4d252c),0);
    put32(ix_global(0x4d24c0),b+IX_CAM);
    return code;
}
static int ix_order_chain_probe(void){
    const unsigned index=ES2_HOOK_INDEX_player_input;const es2_hook_descriptor *d=function_hook_descriptor(index);
    BYTE saved[IX_IDS][5];int active[IX_IDS];
    /* 45feb8, 429820 and 432bc8 run as original code here. */
    if(ix_patch(saved,active,ix_which(index)&~(1ull|1ull<<19|1ull<<20),1))return 1;
    static BYTE chain_old[IX_CHAIN_GLOBALS],chain_before[IX_CHAIN_GLOBALS],chain_after[IX_CHAIN_GLOBALS],chain_actual[IX_CHAIN_GLOBALS];
    ix_chain_globals(chain_old,0);
    int status=0;unsigned total=0,given[2]={0,0},refused[2]={0,0};static ix_record expected[IX_MAX];static BYTE expected_window[1024];
    uint64_t entered[3]={0,0,0};
    ix_track=1;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned n=0;n<1024&&!status;++n){
        uint32_t a[6],r[3],expected_out[3],actual_out[3],pattern=n&2?0xa7a7a7a7u:0x31313131u;ix_build(index,a,r,0);
        const uint32_t code=ix_chain_build(n);
        memcpy(ix_before,ix_arena,IX_SIZE);ix_globals(ix_g_before,0);ix_chain_globals(chain_before,0);
        uint32_t head;__asm__ volatile("movl %%fs:0,%0":"=r"(head));
        ix_reset();ix_track_invoke(mapped+d->rva,a,pattern,expected_out);memcpy(expected_window,ix_window,1024);
        unsigned calls=ix_calls;memcpy(expected,ix_log,calls*sizeof(*expected));memcpy(ix_after,ix_arena,IX_SIZE);
        ix_globals(ix_g_after,0);ix_chain_globals(chain_after,0);
        uint32_t head_after;__asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
        /* Replaced: player_input (bit 0), command_key (bit 1), hud_key (bit 2);
         * mode 7 is the live configuration. */
        static const unsigned chain_hooks[3]={ES2_HOOK_INDEX_player_input,ES2_HOOK_INDEX_command_key,ES2_HOOK_INDEX_hud_key};
        const unsigned mode=1u+(n>>2)%7u;
        es2_hook hk[3];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        es2_hook_set_fast_path(fast);
        for(unsigned j=0;j<3;++j){o.mode=mode>>j&1?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(chain_hooks[j]),&o,function_hook_extension(chain_hooks[j])))return fail("input_dispatch chain install");}
        memcpy(ix_arena,ix_before,IX_SIZE);ix_globals(ix_g_before,1);ix_chain_globals(chain_before,1);ix_reset();
        ix_track_invoke(mapped+d->rva,a,pattern,actual_out);ix_globals(ix_g_actual,0);ix_chain_globals(chain_actual,0);
        uint32_t head_actual;__asm__ volatile("movl %%fs:0,%0":"=r"(head_actual));
        uint64_t pair[2]={(uint64_t)expected_out[1]<<32|expected_out[0],(uint64_t)actual_out[1]<<32|actual_out[0]};
        unsigned orders=0,eligible=0;for(unsigned j=0;j<calls;++j)orders+=expected[j].id==47;
        for(unsigned k=0;k<get16(ix_before+IX_GROUP+0x10)&&k<3;++k)eligible+=!ix_before[IX_MEMBER+0x100*k+0x99];
        if(head_after!=head||head_actual!=head)status=fail("input_dispatch chain FS:0 not restored");
        else if(pair[0]!=pair[1]||calls!=ix_calls||memcmp(expected,ix_log,calls*sizeof(*expected))||memcmp(ix_after,ix_arena,IX_SIZE)||
           memcmp(ix_g_after,ix_g_actual,IX_GLOBALS)||memcmp(chain_after,chain_actual,IX_CHAIN_GLOBALS)||(mode&1&&hk[0].calls!=1)){
            fprintf(stderr,"order chain replaced mode %u (bit 0 player_input, 1 command_key, 2 hud_key)\n",mode);
            status=ix_report(fast?"order chain fast":"order chain preserving",n,pair,calls,expected,ix_g_after,ix_g_actual);}
        else if(memcmp(expected_window,ix_window,1024))status=ix_window_report("below the slots after the order chain",n,expected_window,ix_window);
        for(unsigned j=0;j<3;++j){entered[j]+=hk[j].calls;if(!es2_hook_uninstall(hk+j))return fail("input_dispatch chain uninstall");}
        ix_globals(ix_g_before,1);ix_chain_globals(chain_before,1);++total;
        if(code==0x218u&&eligible){if(orders)++given[pattern==0xa7a7a7a7u];else ++refused[pattern==0xa7a7a7a7u];}
    }
    es2_hook_set_fast_path(TRUE);ix_track=0;ix_chain_globals(chain_old,1);
    if(ix_patch(saved,active,0,0))return 1;
    /* The original's Alt+O follows the residue: the order is given over the
     * 3131h fill and refused over the A7A7h fill. */
    if(!status&&(!given[0]||given[1]||!refused[1])){
        fprintf(stderr,"FAIL: player_input order chain: Alt+O given %u/%u, refused %u/%u (31/a7 fill)\n",given[0],given[1],refused[0],refused[1]);status=1;}
    if(!status&&(!entered[0]||!entered[1]||!entered[2])){fprintf(stderr,"FAIL: player_input order chain: a hook never entered\n");status=1;}
    if(!status)printf("PASS: player_input %u order-chain cases over the real 45feb8/432bc8/446c38/447248/4231a4 with player_input, command_key and hud_key in the seven replaced combinations (entries %llu/%llu/%llu), fast/preserving dispatch; Alt+O with members: 31 fill %u given/%u refused, a7 fill %u given/%u refused\n",
        total,(unsigned long long)entered[0],(unsigned long long)entered[1],(unsigned long long)entered[2],given[0],refused[0],given[1],refused[1]);
    return status;
}
/* Real callees with the nested hooks in rotating original/replaced/compare
 * modes (compare falls back to replaced where refused) and a patterned
 * stack; the remaining stubs' inputs are compared in order. */
static int ix_real_pass(unsigned index){
    static const unsigned center_nested[]={ES2_HOOK_INDEX_multiply_q10,ES2_HOOK_INDEX_torso_twist,ES2_HOOK_INDEX_torso_pitch};
    static const unsigned aim_nested[]={ES2_HOOK_INDEX_euler_matrix,ES2_HOOK_INDEX_matrix_transpose,ES2_HOOK_INDEX_rotate_point,
        ES2_HOOK_INDEX_transform_point,ES2_HOOK_INDEX_aim_angles,ES2_HOOK_INDEX_multiply_q10,ES2_HOOK_INDEX_multiply_q8,
        ES2_HOOK_INDEX_torso_twist,ES2_HOOK_INDEX_dist3d_delta,ES2_HOOK_INDEX_torso_pitch};
    static const unsigned player_nested[]={ES2_HOOK_INDEX_dist_gate,ES2_HOOK_INDEX_multiply_q10,ES2_HOOK_INDEX_torso_twist,
        ES2_HOOK_INDEX_torso_pitch,ES2_HOOK_INDEX_aim_point,ES2_HOOK_INDEX_torso_aim,ES2_HOOK_INDEX_torso_center,
        ES2_HOOK_INDEX_weapon_keys};
    const unsigned *nested;unsigned count;uint64_t stubs=0xfull<<36;
    if(index==ES2_HOOK_INDEX_torso_center){nested=center_nested;count=3;}
    else if(index==ES2_HOOK_INDEX_torso_aim){nested=aim_nested;count=10;}
    else {nested=player_nested;count=8;stubs|=0x3full|1ull<<8|0x7fc000ull|1ull<<40|0xfull<<42;}
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[10]={0};
    BYTE saved[IX_IDS][5];int active[IX_IDS];if(ix_patch(saved,active,stubs,1))return 1;
    static uint32_t expected_log[IX_REAL_MAX];
    ix_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;ix_build(index,a,r,1);
        memcpy(ix_before,ix_arena,IX_SIZE);ix_globals(ix_g_before,0);
        es2_hook hk[11];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("input_dispatch nested install");}
        ix_reset();ix_machine(mapped+d->rva,a,r,pattern,expected_pair);
        unsigned logged=ix_real_count;memcpy(expected_log,ix_real_log,4*logged);
        memcpy(ix_after,ix_arena,IX_SIZE);ix_globals(ix_g_after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+count,d,&o,function_hook_extension(index)))return fail("input_dispatch caller install");
        memcpy(ix_arena,ix_before,IX_SIZE);ix_globals(ix_g_before,1);ix_reset();
        ix_machine(mapped+d->rva,a,r,pattern,actual_pair);ix_globals(ix_g_actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||logged!=ix_real_count||memcmp(expected_log,ix_real_log,4*logged)||memcmp(ix_after,ix_arena,IX_SIZE)||
           memcmp(ix_g_after,ix_g_actual,IX_GLOBALS)){
            fprintf(stderr,"real mode=%u stub log %u/%u\n",mode,logged,ix_real_count);ix_calls=0;
            status=ix_report("real",n,pair,0,NULL,ix_g_after,ix_g_actual);
        }
        for(unsigned j=0;j<=count;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("input_dispatch nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("input_dispatch nested uninstall");}
        ++total;
    }
    ix_real=0;
    if(ix_patch(saved,active,0,0))return 1;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){
        fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, 31/a7 stack, pair, memory and stub inputs\n",
        d->name,total,count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
static int input_dispatch_cases(unsigned index,unsigned limit){
    ix_index=index;ix_real=0;
    static BYTE old_globals[IX_GLOBALS];ix_globals(old_globals,0);
    BYTE saved[IX_IDS][5];int active[IX_IDS];
    if(ix_patch(saved,active,ix_which(index),1))return 1;
    int player=index==ES2_HOOK_INDEX_player_input,status=0;
    if(player&&!limit&&ix_table(1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_INPUT_DISPATCH_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];ix_build(index,a,r,0);status=ix_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(player&&!limit){
        if(ix_table(0))return 1;
        for(unsigned k=0;k<IX_TABLE;++k){
            printf("JUMPTABLE player_input 460d83 entry %2u target %08x hits %u\n",k,ix_case_targets[k],ix_case_hits[k]);
            if(!status&&!ix_case_hits[k]){fprintf(stderr,"FAIL: player_input jump-table entry %u never taken\n",k);status=1;}
        }
    }
    if(!limit&&!status)status=ix_installed_probes(index,64);
    if(!limit&&!status&&index==ES2_HOOK_INDEX_torso_aim)status=ix_torso_residue_probe();
    if(ix_patch(saved,active,0,0))return 1;
    current_case_valid=0;
    if(!limit&&!status&&player)status=ix_player_residue_probe();
    if(!limit&&!status&&player)status=ix_player_frame_probe();
    if(!limit&&!status&&player)status=ix_order_chain_probe();
    if(!limit&&!status)status=ix_real_pass(index);
    ix_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, payloads by content, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
