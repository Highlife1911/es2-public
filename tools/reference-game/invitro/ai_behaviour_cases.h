/* AI behaviours around the torso servos: weapon_range 40e5f8, weapon_choose
 * 41f358, aim_fire 41f5a0, aim_target 41e984, target_search 41e0e0, hunt
 * 41e224, line_blocked 41dc24, clear_shot 41dd64 and group_move 41d598
 * (docs/reverse-engineering/ai.md, "Stack residue under aim_fire"). Direct
 * cases run the original body and the core against logging stubs for the game
 * callees of the hook under test (ordered argument slots, entry EAX/EDX/ECX,
 * frame points by content, stack addresses masked in the fixed window of
 * node_cases.h); the actor and weapon virtuals go through the arena vtable.
 * Pair, arena and globals are compared. Then installed 31/a7 poison probes
 * (weapon_range's ECX through its entry shim as well) and a real-callee pass
 * with the nested hooks in rotating modes. aim_fire's installed and real passes
 * also compare the callee-saved registers and the stack coordinate at every
 * stub entry and the 64 bytes below the caller's argument slots after return
 * (the original's frame and the last call's slots); its residue probe compares
 * the 512 bytes below the +28h call after a torso_aim stub that leaves
 * register- and address-dependent bytes where the weapon bodies read
 * uninitialized stack (ai.md, "stack residue under aim_fire"). */
typedef struct ab_record { uint32_t id,args[6],regs[3],input[6],eax,edx,saved[5],slots; } ab_record;
enum { AB_SIZE=0x2000, AB_ACT=0x100, AB_STRIDE=0x340, AB_ACTORS=6, AB_GRP=0x1500, AB_MEM=0x1600, AB_ORD=0x1640,
    AB_CFG=0x1700, AB_VT=0x1800, AB_LIST=0x1880, AB_WS=0x18a0, AB_WPN=0x1900, AB_CAR=0x1a80, AB_DEF=0x1cc0,
    AB_PT=0x1d40, AB_V=0x1d80, AB_R=0x1da0, AB_TBL=0x1e00, AB_WEAPONS=6, AB_MAX=96, AB_IDS=41, AB_DIRECT=32,
    AB_GLOBALS=4+4+4+2+2+0x72+2+2 };
#define AB_BASE ((uint32_t)(uintptr_t)ab_arena)
/* Actor i with its 8-byte list node header in front of it. */
#define AB_ACTOR(i) (AB_BASE+AB_ACT+AB_STRIDE*(uint32_t)(i)+8u)
#define AB_GROUP(i) (AB_BASE+AB_GRP+0x80u*(uint32_t)(i))
#define AB_WEAPON(i) (AB_BASE+AB_WPN+0x40u*(uint32_t)(i))
static BYTE ab_arena[AB_SIZE],ab_before[AB_SIZE],ab_after[AB_SIZE];
static ab_record ab_log[AB_MAX];
static unsigned ab_calls,ab_cursor;
static int ab_real;
static uint32_t ab_seed,ab_stream,ab_seq[AB_ACTORS+1];
__attribute__((used)) static uint32_t ab_ecx;
/* EBX, ESI, EDI, EBP, ESP at the last stub entry; ab_invoke's ESP before
 * its call; the residue window of the +28h stub; the 64 bytes below the
 * argument slots after ab_invoke's call; scratch for the window copy. */
__attribute__((used)) static uint32_t ab_entry[5],ab_origin,ab_tmp[3];
__attribute__((used)) static BYTE ab_window[512],ab_frame_copy[64];
static int ab_track;
/* +5Ch never reports a ready weapon while set. */
static int ab_busy;
__attribute__((used)) static unsigned ab_window_hits;
/* The real-fire probe's weapon (weapon_arena of weapons_cases.h) and its
 * vtable; weapon_choose's stub returns it while it is set. */
static uint32_t ab_fire_weapon,ab_fire_vt[32];
/* 0 41fac4 move to a point, 1 41fb60 leader route, 2 41fbb8 formation_steer,
 * 3 41c3c8, 4 41e8d4 torso_center, 5 46e980 segment, 6 404ca0 object_line,
 * 7 4280f4 terrain_line, 8 492934 heading_point, 9 467aa8 timer_dword_step,
 * 10 41dc24 line_blocked, 11 413e50 script_start, 12 4928d0 dist2d_point,
 * 13 4929fc point_offset, 14 416a04 vehicle_control, 15 411e80 target_valid,
 * 16 471c70 list step, 17 41dbcc actor_destination, 18 467a48
 * timer_word_step, 19 41e0e0 target_search, 20 41ce08 part_pick, 21 49288c
 * dist_gate, 22 47ebb8 euler_matrix, 23 48043c transform_point, 24 41f5a0
 * aim_fire, 25 47df48 scalar_interpolate, 26 40e5f8 weapon_range, 27 47e0b0
 * multiply_q10, 28 492f24 rng_bounded, 29 423430 trigger_fire, 30 41ef14
 * torso_aim, 31 41f358 weapon_choose; 32..40 the virtuals +24h, +28h,
 * +2Ch, +34h, +38h, +58h, +5Ch, +60h, +78h (arena vtable, never patched;
 * +78h only for formation_steer in the real pass). */
static const uint32_t ab_addresses[AB_IDS]={0x41fac4,0x41fb60,0x41fbb8,0x41c3c8,0x41e8d4,0x46e980,0x404ca0,0x4280f4,
    0x492934,0x467aa8,0x41dc24,0x413e50,0x4928d0,0x4929fc,0x416a04,0x411e80,0x471c70,0x41dbcc,0x467a48,0x41e0e0,
    0x41ce08,0x49288c,0x47ebb8,0x48043c,0x41f5a0,0x47df48,0x40e5f8,0x47e0b0,0x492f24,0x423430,0x41ef14,0x41f358,
    0,0,0,0,0,0,0,0,0};
static const unsigned ab_slots[AB_IDS]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0x24,0x28,0x2c,0x34,0x38,0x58,0x5c,0x60,0x78};
static const unsigned ab_counts[AB_IDS]={2,1,1,1,2,6,5,4,2,1,1,2,2,3,4,3,2,1,1,2,1,2,2,3,4,5,2,2,2,1,2,4,
    1,3,1,2,1,3,1,2,2};
static const struct { uint32_t rva,size; } ab_ranges[]={{0xa9b84,4},{0xa9b7a,4},{0xa0be0,4},{0xa9e5c,2},{0xa9e63,2},
    {0xd2599,0x72},{0xd3b64,2},{0xa9b70,2}};
static void ab_globals(BYTE *out,int restore){
    unsigned at=0;
    for(unsigned i=0;i<sizeof(ab_ranges)/sizeof(ab_ranges[0]);++i){BYTE *g=mapped+ab_ranges[i].rva;
        if(restore)memcpy(g,out+at,ab_ranges[i].size);else memcpy(out+at,g,ab_ranges[i].size);at+=ab_ranges[i].size;}
}
static uint32_t ab_next(void){
    uint32_t z=ab_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *ab_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int ab_in(uint32_t a,uint32_t n){return a-AB_BASE<AB_SIZE&&AB_SIZE-(a-AB_BASE)>=n;}
/* Same fixed stack window as node_cases.h: frame addresses are masked. */
static int ab_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
/* Readable payload: inside the arena or the committed stack. */
static int ab_ok(uint32_t p,uint32_t n){
    const NT_TIB *t=(const NT_TIB *)NtCurrentTeb();uint32_t low=(uint32_t)(uintptr_t)t->StackLimit,top=(uint32_t)(uintptr_t)t->StackBase;
    return ab_in(p,n)||(p>=low&&p<top&&top-p>=n);
}
static void ab_dwords(ab_record *v,unsigned at,uint32_t p,unsigned n){
    if(ab_ok(p,4u*n))for(unsigned j=0;j<n;++j)v->input[at+j]=get32(ab_host(p)+4*j);
}
static uint32_t ab_small(uint32_t pick,uint32_t range){return (uint32_t)((int32_t)(pick%(2u*range+1u))-(int32_t)range);}
static uint16_t ab_count_word(uint32_t pick){
    static const uint16_t counts[]={0,1,2,3,7,0x7ffe,0x8000,1,0};uint16_t c=counts[pick%9];return c;
}
__attribute__((used,noinline)) static uint64_t __cdecl ab_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(ab_calls==AB_MAX){fail("ai behaviour call overflow");ExitProcess(1);}
    ab_record *v=ab_log+ab_calls++;memset(v,0,sizeof(*v));v->id=id;v->slots=weapons_bridge_slots(a,ab_counts[id]);
    if(ab_track){for(unsigned j=0;j<3;++j)v->saved[j]=ab_entry[j];v->saved[3]=ab_entry[3]-ab_origin;v->saved[4]=ab_entry[4]-ab_origin;}
    for(unsigned k=0;k<ab_counts[id];++k)v->args[k]=ab_stack(a[k])?0:a[k];
    for(unsigned j=0;j<3;++j)v->regs[j]=ab_stack(regs[j])?0:regs[j];
    uint32_t eax=ab_next(),edx=ab_next(),pick=ab_next();ab_ecx=ab_next();
    BYTE *o=ab_host(AB_ACTOR(0));
    const uint32_t T=get32(o+0x1a4);
    switch(id){
    case 5: /* segment: AL zero half of the time; S and E by content. */
        ab_dwords(v,0,a[1],3);ab_dwords(v,3,a[2],3);
        if(pick%2)eax&=0xffffff00u;else if(!(eax&0xffu))eax|=1u;
        break;
    case 6: /* object_line: on a hit the out pair {distance, object}; the
             * target pointer may change (it is reread). */
        ab_dwords(v,0,a[1],3);ab_dwords(v,3,a[2],3);
        if(pick%2)eax&=0xffff0000u;
        else {
            if(!(uint16_t)eax)eax|=1u;
            uint32_t object=(pick>>4)%4==0?(T?T+0xcu:0):(pick>>4)%4==1?AB_ACTOR((pick>>8)%AB_ACTORS)+0xcu:(pick>>4)%4==2?0:ab_next();
            if(ab_ok(a[0],8)){put32(ab_host(a[0]),ab_next());put32(ab_host(a[0])+4,object);}
        }
        if(pick%11==0)put32(o+0x1a4,pick>>12&1?0:AB_ACTOR((pick>>16)%AB_ACTORS));
        break;
    case 7: /* terrain_line: AX zero half of the time. */
        ab_dwords(v,0,a[1],3);ab_dwords(v,3,a[2],3);
        if(pick%2)eax&=0xffff0000u;else if(!(uint16_t)eax)eax|=1u;
        break;
    case 8: { /* heading_point: AX on the cone edges around A's heading;
               * the heading is reread. */
        ab_dwords(v,0,a[0],2);ab_dwords(v,2,a[1],2);
        BYTE *cfg=ab_in(get32(o+0x1f2),0x40)?ab_host(get32(o+0x1f2)):NULL;
        uint32_t c=cfg?get16(cfg+0x22):0x1000u;
        const uint32_t deltas[]={0u-c,0u-c-1u,c-1u,c,0,0x8000,0x7fff,1};
        if(pick%4)eax=(eax&0xffff0000u)|(uint16_t)(get16(o+0x10)+deltas[(pick>>4)%8]);
        if(pick%7==0)put16(o+0x10,(uint16_t)ab_next());
        break; }
    case 9: /* timer_dword_step: EAX zero half of the time. */
        if(pick%2)eax=0;else if(!eax)eax=1;
        break;
    case 10: { /* line_blocked: AL 0, 1, 2 or other. */
        static const uint8_t results[]={0,1,2,0x80};
        eax=(eax&0xffffff00u)|results[pick%4];
        break; }
    case 11: /* script_start: may clear or set the behaviour state B+7. */
        if(pick%5==0)o[0x61]=(BYTE)((pick>>4)%3);
        break;
    case 12: { /* dist2d_point: the thresholds 10000, 30000 and the two
                * search ranges; points by content. */
        ab_dwords(v,0,a[0],2);ab_dwords(v,2,a[1],2);
        static const uint32_t ds[]={30000,30001,29999,10000,9999,10001,89999,90000,90001,979999,980000,980001,0,1};
        if(pick%4)eax=ds[(pick>>4)%14];else if(pick%8==0)eax=ab_next()%200000u;
        break; }
    case 13: /* point_offset: the point by content, moved in place. */
        ab_dwords(v,0,a[0],2);
        if(pick%2&&ab_ok(a[0],8)){put32(ab_host(a[0]),ab_next());put32(ab_host(a[0])+4,ab_next());}
        break;
    case 15: { /* target_valid: AX zero a third of the time; E's flags and
                * attacker count are reread. */
        if(pick%3==0)eax&=0xffff0000u;else if(!(uint16_t)eax)eax|=1u;
        BYTE *e=ab_in(a[1],0x340)?ab_host(a[1]):NULL;
        if(e&&pick%7==1)e[(pick>>4)%3==0?0xa4:(pick>>4)%3==1?0xa5:0x99]=(BYTE)(pick>>8&1?0:1+(pick>>12)%255);
        if(e&&pick%7==2)put16(e+0x1a2,ab_count_word(pick>>8));
        break; }
    case 16: /* list step: the planned entries, then 0; the head and the
              * returned entry's flags may change. */
        eax=ab_seq[ab_cursor];if(eax)++ab_cursor;
        if(pick%9==0)put32(mapped+0xa9b7a,ab_next());
        if(eax&&pick%9==1)ab_host(eax)[0xa5]=(BYTE)(pick>>8&1);
        break;
    case 17: /* actor_destination: a point (sometimes another one). */
        eax=pick%5?AB_BASE+AB_PT+0x10u:AB_ACTOR((pick>>4)%AB_ACTORS)+0x240u;
        break;
    case 18: /* timer_word_step: AX zero half of the time; the order state
              * and the order target are reread. */
        if(pick%2)eax&=0xffff0000u;else if(!(uint16_t)eax)eax|=1u;
        if(pick%9==0)put16(o+0x23e,(uint16_t)((pick>>4)%3==0?4:(pick>>4)%3==1?0:1));
        if(pick%9==1)put32(o+0x248,pick>>8&1?0:AB_ACTOR((pick>>12)%AB_ACTORS));
        /* A target set again behind hunt's back (41e304 rereads it). */
        if(pick%9==2)put32(o+0x1a4,pick>>8&1?get32(o+0x248):AB_ACTOR((pick>>12)%AB_ACTORS));
        break;
    case 19: /* target_search: 0, the current target or another actor. */
        eax=pick%3==0?0:pick%3==1?T:AB_ACTOR((pick>>4)%AB_ACTORS);
        break;
    case 20: { /* part_pick: the leader's byte +0A3h and A+0B2h are reread. */
        uint32_t L=get32(ab_host(get32(ab_host(get32(o+0x45)+0xcu))));
        if(pick%5==0&&ab_in(L,0x340))ab_host(L)[0xa3]^=1;
        if(pick%5==1)o[0xb2]^=1;
        break; }
    case 21: { /* dist_gate: the part window 5000..19999; the part word and
                * T's matrix word are reread. */
        ab_dwords(v,0,a[0],3);ab_dwords(v,3,a[1],3);
        static const uint32_t ds[]={5000,4999,5001,19999,20000,20001,0,0xffffffffu};
        if(pick%4)eax=ds[(pick>>4)%8];
        static const uint16_t parts[]={0xffff,0,3,0x7fff,0x8000};
        if(pick%9==0)put16(o+0x2a2,parts[(pick>>8)%5]);
        if(pick%9==1&&ab_in(a[1]-0x26u,0x340))put16(ab_host(a[1]-0x26u)+0x32,(uint16_t)(pick>>12&1));
        break; }
    case 23: /* transform_point: writes the aim point (all 12 bytes). */
        if(ab_ok(a[2],12))for(unsigned j=0;j<3;++j)put32(ab_host(a[2])+4*j,ab_next());
        break;
    case 24: /* aim_fire: the aim point by content. */
        ab_dwords(v,0,a[1],3);
        break;
    case 25: /* scalar_interpolate: the starting best score. */
        if(pick%2)eax=(eax&0xffff0000u)|(uint16_t)ab_small(pick>>4,300);
        break;
    case 26: /* weapon_range: AX zero half of the time; the kept weapon,
              * A+96h and A+26Bh are reread. */
        if(pick%2)eax&=0xffff0000u;else if(!(uint16_t)eax)eax|=1u;
        if(pick%9==0)put32(o+0x2ac,pick>>8&1?0:AB_WEAPON((pick>>12)%AB_WEAPONS));
        if(pick%9==1)o[0x96]^=1;
        if(pick%9==2)put16(o+0x26b,(uint16_t)(pick>>8&1));
        break;
    case 27: /* multiply_q10: score terms. */
        if(pick%2)eax=ab_small(pick>>4,500);
        break;
    case 28: /* rng_bounded: small draws. */
        eax=(eax&0xffff0000u)|(uint16_t)ab_small(pick>>4,40);
        break;
    case 29: /* trigger_fire: the word 4a9e63 may change. */
        if(pick%3==0)put16(mapped+0xa9e63,(uint16_t)(999u+(pick>>4)%3));
        break;
    case 30: { /* torso_aim: the aim point by content; returns the angle
                * record V with words on the +-1000 window edges. */
        ab_dwords(v,0,a[1],3);
        static const uint16_t edges[]={0xfc17,0xfc18,0x03e7,0x03e8,0,0x8000,0x7fff};
        eax=pick%8?AB_BASE+AB_V:AB_BASE+AB_V+8u;
        put16(ab_host(eax),edges[(pick>>4)%7]);put16(ab_host(eax)+4,edges[(pick>>8)%7]);
        break; }
    case 31: /* weapon_choose: none or a weapon. */
        eax=pick%3==0?0:ab_fire_weapon?ab_fire_weapon:AB_WEAPON((pick>>4)%AB_WEAPONS);
        break;
    case 32: /* virtual +24h: the model record (0 only in direct cases). */
        eax=pick%4==0&&!ab_real?0:AB_BASE+AB_R;
        break;
    case 33: { /* virtual +28h (fire): the weapon's type word may change. */
        BYTE *w=ab_in(a[0],0x40)?ab_host(a[0]):NULL;
        static const uint16_t types[]={6,0x16,1,0x17};
        if(w&&pick%5==0&&ab_in(get32(w+0x1c),0x60))put16(ab_host(get32(w+0x1c))+0x56,types[(pick>>4)%4]);
        break; }
    case 34: /* virtual +2Ch: AX zero a third of the time; the kept weapon
              * is reread. */
        if(pick%3==0)eax&=0xffff0000u;else if(!(uint16_t)eax)eax|=1u;
        if(pick%9==0)put32(o+0x2ac,pick>>8&1?0:AB_WEAPON((pick>>12)%AB_WEAPONS));
        break;
    case 35: /* virtual +34h: AX zero half of the time. */
        if(pick%2)eax&=0xffff0000u;
        break;
    case 36: { /* virtual +38h: speeds around 0. */
        static const uint16_t speeds[]={0,1,0xffff,0x100,0x8000};
        if(pick%4)eax=(eax&0xffff0000u)|speeds[(pick>>4)%5];
        break; }
    case 37: /* virtual +58h: writes the part position. */
        if(ab_ok(a[2],12))for(unsigned j=0;j<3;++j)put32(ab_host(a[2])+4*j,ab_next());
        break;
    case 38: /* virtual +5Ch: AX zero two thirds of the time (never while
              * ab_busy: the trigger_fire residue probe). */
        if(ab_busy){if(!(uint16_t)eax)eax|=1u;}
        else if(pick%3)eax&=0xffff0000u;
        break;
    case 39: { /* virtual +60h: the weapon state 0..15 (5 often). */
        static const uint16_t states[]={5,5,3,0,1,2,4,6,15,9};
        eax=(eax&0xffff0000u)|states[(pick>>4)%10];
        break; }
    case 40: /* virtual +78h: the formation point by content. */
        ab_dwords(v,0,a[1],3);
        break;
    default:break;
    }
    v->eax=ab_stack(eax)?0:eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define AB_STUB(n) __attribute__((naked)) static void ab_stub_##n(void){__asm__ volatile( \
    "movl %ebx,_ab_entry\n\tmovl %esi,_ab_entry+4\n\tmovl %edi,_ab_entry+8\n\tmovl %ebp,_ab_entry+12\n\tmovl %esp,_ab_entry+16\n\t" \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _ab_stub_impl\n\taddl $24,%esp\n\tmovl _ab_ecx,%ecx\n\tret\n\t");}
AB_STUB(0) AB_STUB(1) AB_STUB(2) AB_STUB(3) AB_STUB(4) AB_STUB(5) AB_STUB(6) AB_STUB(7) AB_STUB(8) AB_STUB(9)
AB_STUB(10) AB_STUB(11) AB_STUB(12) AB_STUB(13) AB_STUB(14) AB_STUB(15) AB_STUB(16) AB_STUB(17) AB_STUB(18) AB_STUB(19)
AB_STUB(20) AB_STUB(21) AB_STUB(22) AB_STUB(23) AB_STUB(24) AB_STUB(25) AB_STUB(26) AB_STUB(27) AB_STUB(28) AB_STUB(29)
AB_STUB(30) AB_STUB(31) AB_STUB(32) AB_STUB(33) AB_STUB(34) AB_STUB(35) AB_STUB(36) AB_STUB(37) AB_STUB(38) AB_STUB(39)
AB_STUB(40)
#undef AB_STUB
/* torso_aim for the residue probe: the logging stub, then (below the
 * returned ESP, i.e. in the dead callee area) the caller's EBX/EBP/ESI/EDI
 * where a replaced torso_aim's fast entry saves them (pushad at T-1Ch..
 * -2Ch, dispatch_fast at T-0A4h..-0B0h, T = entry ESP), ESP at T-20h, and
 * register bytes at the positions the weapon bodies read through
 * preparation: T-45h/-65h (fire), T-4Dh/-6Dh (emitter 40ea58),
 * T-41h/-61h (alternate 40ec64) and the exclusion dwords T-0B0h/-0BCh. */
__attribute__((naked)) static void ab_stub_framed(void){__asm__ volatile(
    "movl %ebx,_ab_entry\n\tmovl %esi,_ab_entry+4\n\tmovl %edi,_ab_entry+8\n\tmovl %ebp,_ab_entry+12\n\tmovl %esp,_ab_entry+16\n\t"
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
    "pushl $30\n\tcalll _ab_stub_impl\n\taddl $24,%esp\n\tmovl _ab_ecx,%ecx\n\t"
    "movl %ebx,-28(%esp)\n\tmovl %esp,-32(%esp)\n\tmovl %ebp,-36(%esp)\n\tmovl %esi,-40(%esp)\n\tmovl %edi,-44(%esp)\n\t"
    "movl %ebp,-69(%esp)\n\tmovl %esi,-101(%esp)\n\tmovl %edi,-77(%esp)\n\tmovl %ebx,-109(%esp)\n\tmovl %esp,-65(%esp)\n\tmovl %ebp,-97(%esp)\n\t"
    "movl %ebp,-164(%esp)\n\tmovl %ebx,-168(%esp)\n\tmovl %edi,-172(%esp)\n\tmovl %esi,-176(%esp)\n\tmovl %esi,-188(%esp)\n\tret\n\t");}
/* The weapon's +28h for the residue probe: copy the 512 bytes below the
 * entry ESP before anything writes there, then the logging stub. */
__attribute__((naked)) static void ab_stub_window(void){__asm__ volatile(
    "movl %esi,_ab_tmp\n\tmovl %edi,_ab_tmp+4\n\tmovl %ecx,_ab_tmp+8\n\t"
    "leal -512(%esp),%esi\n\tmovl $_ab_window,%edi\n\tmovl $128,%ecx\n\tcld\n\trep movsl\n\t"
    "movl _ab_tmp,%esi\n\tmovl _ab_tmp+4,%edi\n\tmovl _ab_tmp+8,%ecx\n\tincl _ab_window_hits\n\tjmp _ab_stub_33\n\t");}
/* The same, then 192 KiB of stack below the entry probed and filled with
 * 5Ah, as deep as an emitter's 128-KiB gap plus 64 KiB of its frames: a
 * replaced aim_fire must keep its suspended host frames below that. */
__attribute__((used)) static uint32_t ab_deep_esp;
__attribute__((naked)) static void ab_stub_deep(void){__asm__ volatile(
    "movl %esi,_ab_tmp\n\tmovl %edi,_ab_tmp+4\n\tmovl %ecx,_ab_tmp+8\n\t"
    "leal -512(%esp),%esi\n\tmovl $_ab_window,%edi\n\tmovl $128,%ecx\n\tcld\n\trep movsl\n\t"
    "movl %eax,%esi\n\tmovl %esp,_ab_deep_esp\n\tmovl $48,%ecx\n\t1: subl $4096,%esp\n\torl $0,(%esp)\n\tloop 1b\n\t"
    "movl %esp,%edi\n\tmovl $0x5a5a5a5a,%eax\n\tmovl $49152,%ecx\n\trep stosl\n\tmovl _ab_deep_esp,%esp\n\tmovl %esi,%eax\n\t"
    "movl _ab_tmp,%esi\n\tmovl _ab_tmp+4,%edi\n\tmovl _ab_tmp+8,%ecx\n\tincl _ab_window_hits\n\tjmp _ab_stub_33\n\t");}
static void *ab_stubs[AB_IDS]={ab_stub_0,ab_stub_1,ab_stub_2,ab_stub_3,ab_stub_4,ab_stub_5,ab_stub_6,ab_stub_7,ab_stub_8,
    ab_stub_9,ab_stub_10,ab_stub_11,ab_stub_12,ab_stub_13,ab_stub_14,ab_stub_15,ab_stub_16,ab_stub_17,ab_stub_18,ab_stub_19,
    ab_stub_20,ab_stub_21,ab_stub_22,ab_stub_23,ab_stub_24,ab_stub_25,ab_stub_26,ab_stub_27,ab_stub_28,ab_stub_29,
    ab_stub_30,ab_stub_31,ab_stub_32,ab_stub_33,ab_stub_34,ab_stub_35,ab_stub_36,ab_stub_37,ab_stub_38,ab_stub_39,ab_stub_40};
#define AB_BIT(i) (1u<<(i))
/* Direct callees of each hook (bit i = ab_addresses[i], i < 32). */
static uint32_t ab_which(unsigned index){
    if(index==ES2_HOOK_INDEX_weapon_choose)return AB_BIT(25)|AB_BIT(26)|AB_BIT(27)|AB_BIT(28)|AB_BIT(29);
    if(index==ES2_HOOK_INDEX_aim_fire)return AB_BIT(12)|AB_BIT(26)|AB_BIT(31)|AB_BIT(13)|AB_BIT(28)|AB_BIT(30);
    if(index==ES2_HOOK_INDEX_aim_target)return AB_BIT(8)|AB_BIT(4)|AB_BIT(21)|AB_BIT(22)|AB_BIT(23)|AB_BIT(24);
    if(index==ES2_HOOK_INDEX_target_search)return AB_BIT(16)|AB_BIT(15)|AB_BIT(12);
    if(index==ES2_HOOK_INDEX_hunt)return AB_BIT(17)|AB_BIT(18)|AB_BIT(19)|AB_BIT(11)|AB_BIT(20)|AB_BIT(12)|AB_BIT(4)|
        AB_BIT(3)|AB_BIT(0)|AB_BIT(8)|AB_BIT(14)|AB_BIT(2);
    if(index==ES2_HOOK_INDEX_line_blocked)return AB_BIT(5)|AB_BIT(6)|AB_BIT(7);
    if(index==ES2_HOOK_INDEX_clear_shot)return AB_BIT(8)|AB_BIT(9)|AB_BIT(10)|AB_BIT(11)|AB_BIT(12)|AB_BIT(13)|AB_BIT(14)|AB_BIT(4);
    if(index==ES2_HOOK_INDEX_group_move)return AB_BIT(0)|AB_BIT(1)|AB_BIT(2)|AB_BIT(3)|AB_BIT(4);
    return 0;
}
static void ab_reset(void){ab_calls=0;ab_cursor=0;ab_stream=ab_seed;}
static uint16_t ab_word(const uint16_t *values,unsigned count){return random32()%4?values[random32()%count]:(uint16_t)random32();}
static BYTE ab_flag(void){return (BYTE)(random32()%4?0:random32()%2?1:random32());}
/* One actor (random bytes otherwise). */
static void ab_actor(unsigned i,int real){
    BYTE *p=ab_host(AB_ACTOR(i));const uint32_t b=AB_BASE;
    put32(p,b+AB_VT);
    static const uint16_t flags16[]={0,1};
    put16(p+0x32,ab_word(flags16,2));
    put32(p+0x45,AB_GROUP(random32()%2));
    put32(p+0x5a,real||random32()%2?b+AB_TBL+0x20u*(random32()%4):random32());
    static const uint16_t first[]={0,1};
    put16(p+0x5e,ab_word(first,2));p[0x60]=ab_flag();
    static const BYTE states[]={0,1,2,1};p[0x61]=random32()%4?states[random32()%4]:(BYTE)random32();
    BYTE *f=p+0x92;
    static const unsigned bytes[]={0x4,0x7,0xb,0x11,0x12,0x13,0x1b,0x20,0x23,0x24};
    for(unsigned k=0;k<10;++k)f[bytes[k]]=ab_flag();
    uint16_t count=ab_count_word(random32());put16(p+0x1a2,random32()%4?count:(uint16_t)(random32()%0xffffu));
    put32(p+0x1a4,AB_ACTOR(random32()%AB_ACTORS));
    static const uint16_t kinds[]={0,1,2};
    put16(p+0x1a8,ab_word(kinds,3));
    put32(p+0x1b6,random32()%2?0:b+AB_TBL+0x40u);
    put32(p+0x1f2,b+AB_CFG+0x40u*(random32()%4));
    put32(p+0x202,b+AB_WS);
    static const uint16_t orders[]={0,4,1,2,0,4};
    put16(p+0x23e,ab_word(orders,6));
    put32(p+0x248,random32()%3==0?0:AB_ACTOR(random32()%AB_ACTORS));
    put16(p+0x26b,ab_word(flags16,2));
    static const uint16_t parts[]={0xffff,0,3,0x7fff,0x8000,1};
    put16(p+0x2a2,ab_word(parts,6));
    put32(p+0x2ac,random32()%3==0?0:AB_WEAPON(random32()%AB_WEAPONS));
    /* Real callees: positions clustered (distances 0..40000, so the range
     * windows, the formation band and the hunt thresholds are reached) and
     * the behaviour timers often near expiry. */
    if(real){for(unsigned j=0;j<3;++j){const uint32_t s=j<2?12000u:300u;put32(p+0x26+4*j,ab_small(random32(),s));
        if(j<2)put32(p+0x240+4*j,ab_small(random32(),s));put32(p+4*j+0x31e,ab_small(random32(),s));}
        if(random32()%2)put16(p+0x5b,(uint16_t)(random32()%300));
        if(random32()%2)put32(p+0x63,random32()%300);}
}
/* The arena for hook index; a[] and r[] are the slots and entry registers. */
static void ab_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    const uint32_t b=AB_BASE,A=AB_ACTOR(0);
    ab_seed=random32();random_bytes(ab_arena,AB_SIZE);memset(a,0,24);ab_real=real;
    for(unsigned i=0;i<3;++i)r[i]=random32();
    for(unsigned i=0;i<AB_ACTORS;++i)ab_actor(i,real);
    /* Real callees: consistent attacker counts (each actor counts the
     * actors that target it, plus 0..2), so hunt's decrement never makes a
     * count FFFFh that target_search would divide by (the original faults). */
    if(real)for(unsigned i=0;i<AB_ACTORS;++i){unsigned c=random32()%3;
        for(unsigned j=0;j<AB_ACTORS;++j)c+=get32(ab_host(AB_ACTOR(j))+0x1a4)==AB_ACTOR(i);put16(ab_host(AB_ACTOR(i))+0x1a2,(uint16_t)c);}
    static const unsigned slots[]={0x24,0x28,0x2c,0x34,0x38,0x58,0x5c,0x60,0x78};
    for(unsigned k=0;k<9;++k)put32(ab_arena+AB_VT+slots[k],(uint32_t)(uintptr_t)ab_stubs[32+k]);
    /* Groups: member list (+0Ch, the first entry is the leader), byte +12h,
     * the current order index +6Ch and four order slots. */
    for(unsigned g=0;g<2;++g){BYTE *p=ab_host(AB_GROUP(g));
        put32(p+0xc,b+AB_MEM+0x10u*g);put32(ab_arena+AB_MEM+0x10*g,AB_ACTOR(random32()%3==0?0:random32()%AB_ACTORS));
        p[0x12]=(BYTE)(random32()%2?0:random32()%2?1:random32());put32(p+0x6c,random32()%4);
        for(unsigned s=0;s<4;++s)put32(p+0x44+4*s,b+AB_ORD+0x10u*(random32()%8));}
    for(unsigned k=0;k<8;++k)put32(ab_arena+AB_ORD+0x10*k+0xe,random32()%2?0:random32());
    static const uint16_t cones[]={0x1000,0x2000,0x4000,0,0x8000,0x0800};
    for(unsigned k=0;k<4;++k)put16(ab_arena+AB_CFG+0x40*k+0x22,ab_word(cones,6));
    /* Weapon system: list, count, state table; weapons, carriers, definitions. */
    BYTE *ws=ab_arena+AB_WS;put32(ws+4,b+AB_WS+0x40u);
    static const uint16_t wcounts[]={0,1,2,4,8,0xffff,0x8000,3};
    uint16_t wc=wcounts[random32()%8];put16(ws+8,wc);
    for(unsigned k=0;k<16;++k)put16(ws+0xa+2*k,(uint16_t)(random32()%3?random32()%2:random32()));
    for(unsigned k=0;k<8;++k)put32(ws+0x40+4*k,random32()%4?AB_WEAPON(random32()%AB_WEAPONS):0);
    for(unsigned k=0;k<AB_WEAPONS;++k){BYTE *w=ab_host(AB_WEAPON(k)),*c=ab_arena+AB_CAR+0x60*k,*d=ab_arena+AB_DEF+0x10*k;
        put32(w,b+AB_VT);put32(w+0x1c,b+AB_CAR+0x60u*k);put32(w+0x20,b+AB_DEF+0x10u*k);
        int32_t low=real?(int32_t)ab_small(random32(),3000):(int32_t)ab_small(random32(),20000);put32(c+0x2c,(uint32_t)low);
        put32(c+0x30,(uint32_t)(low+(int32_t)(real?20000u+random32()%30000u:random32()%40000u)));
        static const uint16_t costs[]={0,1,100,600,0x8000,0x7fff};put16(c+0x34,ab_word(costs,6));
        static const uint16_t types[]={6,0x16,1,0x17,0};put16(c+0x56,ab_word(types,5));
        static const uint16_t ranks[]={0,100,10000,0x7fff,0x8000,0xffff};put16(d+4,ab_word(ranks,6));put16(d+6,ab_word(ranks,6));
        static const uint16_t speeds[]={0,1,0x100,0x8000,0x7fff,0xffff};put16(d+0xa,ab_word(speeds,6));}
    /* The model record of virtual +24h (height +1Ch) and the script tables. */
    if(real)for(unsigned j=0;j<3;++j)put32(ab_arena+AB_R+0x14+4*j,ab_small(random32(),1u<<12));
    /* The MECH list: a random subset, linked through the node headers
     * (+0 next, +4 previous); header +0Eh = the first node. The stub
     * returns it tail first, as 471c70 does. */
    unsigned order[AB_ACTORS],count=random32()%(AB_ACTORS+1);
    for(unsigned i=0;i<AB_ACTORS;++i)order[i]=i;
    for(unsigned i=AB_ACTORS-1;i>0;--i){unsigned j=random32()%(i+1),x=order[i];order[i]=order[j];order[j]=x;}
    put32(ab_arena+AB_LIST+0xe,count?AB_ACTOR(order[0])-8u:0);
    for(unsigned i=0;i<count;++i){BYTE *node=ab_host(AB_ACTOR(order[i])-8u);
        put32(node,i+1<count?AB_ACTOR(order[i+1])-8u:0);put32(node+4,i?AB_ACTOR(order[i-1])-8u:0);}
    for(unsigned i=0;i<count;++i)ab_seq[i]=AB_ACTOR(order[count-1-i]);ab_seq[count]=0;
    /* Globals: player, list head, map, difficulty, the trigger word. */
    put32(mapped+0xa9b84,random32()%3==0?get32(ab_arena+AB_MEM+0x10*(random32()%2)):AB_ACTOR(random32()%AB_ACTORS));
    put32(mapped+0xa9b7a,b+AB_LIST);put32(mapped+0xa0be0,random32());
    static const uint16_t levels[]={0,1,2,3,4,5,0xffff};put16(mapped+0xa9e5c,levels[random32()%7]);
    static const uint16_t words[]={999,1000,1001,0,0x8000,0x7fff};put16(mapped+0xa9e63,ab_word(words,6));
    if(real)put16(mapped+0xd3b64,(uint16_t)(1+random32()%200));
    /* The points P, D and Pt near A for the real callees. */
    if(real)for(unsigned k=0;k<3;++k)for(unsigned j=0;j<3;++j)
        put32(ab_arena+AB_PT+0x10*k+4*j,get32(ab_host(A)+0x26+4*j)+ab_small(random32(),j<2?30000u:300u));
    a[0]=A;
    if(index==ES2_HOOK_INDEX_weapon_range){
        const unsigned k=random32()%AB_WEAPONS;a[0]=AB_WEAPON(k);
        const BYTE *c=ab_arena+AB_CAR+0x60*k;const uint32_t low=get32(c+0x2c),high=get32(c+0x30);
        const uint32_t edges[]={low-1u,low,low+1u,high-1u,high,high+1u,0x80000000u,0x7fffffffu};
        a[1]=random32()%4?edges[random32()%8]:random32();
    } else if(index==ES2_HOOK_INDEX_weapon_choose){
        static const uint16_t angles[]={0x3fff,0x4000,0xbfff,0xc000,0,0x8000,0x7fff};
        a[1]=(random32()&0xffff0000u)|ab_word(angles,7);a[2]=random32()%2?random32()%60000u:random32();
        a[3]=AB_ACTOR(random32()%AB_ACTORS);
    } else if(index==ES2_HOOK_INDEX_aim_fire){
        a[1]=b+AB_PT;a[2]=random32();a[3]=AB_ACTOR(random32()%AB_ACTORS);
    } else if(index==ES2_HOOK_INDEX_aim_target){
        a[1]=random32();a[2]=AB_ACTOR(random32()%AB_ACTORS);
    } else if(index==ES2_HOOK_INDEX_target_search){
        a[1]=b+AB_PT+0x20u;
    } else if(index==ES2_HOOK_INDEX_hunt){
        if(random32()%3==0)put32(ab_host(A)+0x1a4,0);
    }
    (void)n;
}
static int ab_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const ab_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,ab_calls);
    for(unsigned j=0;j<calls&&j<ab_calls&&expected;++j)if(memcmp(expected+j,ab_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(ab_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<AB_SIZE;++j)if(ab_after[j]!=ab_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,ab_after[j],ab_arena[j]);break;}
    return 1;
}
static int ab_frame_report(const char *what,unsigned n,const BYTE expected[64]){
    fprintf(stderr,"FAIL: %s case %u (%s)\n",current_function,n,what);
    for(unsigned j=0;j<64;++j)if(expected[j]!=ab_frame_copy[j]){fprintf(stderr," below the arguments -%u: %02x/%02x\n",64-j,expected[j],ab_frame_copy[j]);break;}
    return 1;
}
static unsigned ab_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<ab_calls;++j){unsigned id=ab_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?ab_slots[id]==c->key:!ab_slots[id]&&ab_addresses[id]==c->key)++hits;}
    return hits;
}
static int ab_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[AB_GLOBALS],after[AB_GLOBALS],actual[AB_GLOBALS];static ab_record expected[AB_MAX];
    memcpy(ab_before,ab_arena,AB_SIZE);ab_globals(before,0);
    ab_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=ab_calls;memcpy(expected,ab_log,calls*sizeof(*expected));
    memcpy(ab_after,ab_arena,AB_SIZE);ab_globals(after,0);
    memcpy(ab_arena,ab_before,AB_SIZE);ab_globals(before,1);ab_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];
    if(count>16)return fail("ai behaviour counter table too long");
    for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);ab_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=ab_expected_hits(c+i))return fail("ai behaviour callee counter delta");
    if(pair[0]!=pair[1]||calls!=ab_calls||memcmp(expected,ab_log,calls*sizeof(*expected))||memcmp(ab_after,ab_arena,AB_SIZE)||
       memcmp(after,actual,AB_GLOBALS))
        return ab_report("direct",n,pair,calls,expected);
    ab_globals(before,1);
    return 0;
}
static int ab_patch(BYTE saved[AB_DIRECT][5],int active[AB_DIRECT],uint32_t which,int on){
    for(unsigned i=0;i<AB_DIRECT;++i){
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(ab_addresses[i]-0x400000u,ab_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(ab_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
/* Calls target with four slots, EAX/EDX/ECX/EBX = pattern and a patterned
 * stack below the arguments; returns EAX, EDX and ECX in out. */
__attribute__((naked,noinline)) static void ab_invoke(void *target,const uint32_t *a,uint32_t pattern,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 16(%ebp),%eax\n\tmovl $1024,%ecx\n\trep stosl\n\taddl $4080,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl %esp,_ab_origin\n\t"
        "movl 16(%ebp),%eax\n\tmovl %eax,%edx\n\tmovl %eax,%ecx\n\tmovl %eax,%ebx\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%ebx\n\tmovl %eax,(%ebx)\n\tmovl %edx,4(%ebx)\n\tmovl %ecx,8(%ebx)\n\t"
        "leal -64(%esp),%esi\n\tmovl $_ab_frame_copy,%edi\n\tmovl $16,%ecx\n\tcld\n\trep movsl\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static int ab_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    ab_track=index==ES2_HOOK_INDEX_aim_fire||index==ES2_HOOK_INDEX_weapon_choose;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_out[3],actual_out[3],pattern=poison?0xa7a7a7a7u:0x31313131u;ab_build(index,n,a,r,0);BYTE expected_frame[64];
        BYTE before[AB_GLOBALS],after[AB_GLOBALS],actual[AB_GLOBALS],expected_fpu[108],actual_fpu[108];static ab_record expected[AB_MAX];
        memcpy(ab_before,ab_arena,AB_SIZE);ab_globals(before,0);
        ab_reset();ab_invoke(mapped+d->rva,a,pattern,expected_out);es2_hook_fpu_save(expected_fpu);memcpy(expected_frame,ab_frame_copy,64);
        unsigned calls=ab_calls;memcpy(expected,ab_log,calls*sizeof(*expected));memcpy(ab_after,ab_arena,AB_SIZE);ab_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("ai behaviour poison install");
        memcpy(ab_arena,ab_before,AB_SIZE);ab_globals(before,1);ab_reset();ab_invoke(mapped+d->rva,a,pattern,actual_out);
        es2_hook_fpu_save(actual_fpu);ab_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_out[1]<<32|expected_out[0],(uint64_t)actual_out[1]<<32|actual_out[0]};
        /* weapon_range returns the original's ECX through its entry shim. */
        int ecx_bad=index==ES2_HOOK_INDEX_weapon_range&&expected_out[2]!=actual_out[2];
        if(pair[0]!=pair[1]||ecx_bad||calls!=ab_calls||memcmp(expected,ab_log,calls*sizeof(*expected))||memcmp(ab_after,ab_arena,AB_SIZE)||
           memcmp(after,actual,AB_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=ab_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        else if(ab_track&&memcmp(expected_frame,ab_frame_copy,64))status=ab_frame_report(fast?"installed fast frame":"installed preserving frame",n,expected_frame);
        if(!es2_hook_uninstall(&h))return fail("ai behaviour poison uninstall");ab_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));ab_track=0;
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state%s\n",d->name,total,
        index==ES2_HOOK_INDEX_weapon_range?", ECX through the entry shim":
        index==ES2_HOOK_INDEX_aim_fire||index==ES2_HOOK_INDEX_weapon_choose?
            ", EBX/ESI/EDI/EBP/ESP at every callee entry and the frame below the arguments":"");
    return status;
}
/* aim_fire's residue probe (ai.md, "stack residue under aim_fire"): torso_aim
 * is ab_stub_framed and the weapon's +28h ab_stub_window, so the 512 bytes
 * below the +28h call hold what a framed callee at aim_fire's torso_aim call
 * left there (caller registers and addresses) and what the logging stubs of the
 * earlier calls left. The original aim_fire and the installed replacement (fast
 * and preserving dispatch, 31/a7 stack) must leave them equal, and the per-call
 * registers and the frame below the arguments as well. Half of the cases use
 * ab_stub_deep, which checks the 256-KiB gap budget. */
static int ab_residue_probe(void){
    const unsigned index=ES2_HOOK_INDEX_aim_fire;const es2_hook_descriptor *d=function_hook_descriptor(index);
    BYTE saved[5];if(actor_patch(0x1ef14,ab_stub_framed,saved,TRUE))return 1;
    int status=0;unsigned total=0,fired=0;ab_track=1;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_out[3],actual_out[3],pattern=poison?0xa7a7a7a7u:0x31313131u;ab_build(index,n,a,r,0);
        put32(ab_arena+AB_VT+0x28,(uint32_t)(uintptr_t)(n%2?ab_stub_deep:ab_stub_window));
        BYTE before[AB_GLOBALS],after[AB_GLOBALS],actual[AB_GLOBALS],expected_window[512],expected_frame[64];static ab_record expected[AB_MAX];
        memcpy(ab_before,ab_arena,AB_SIZE);ab_globals(before,0);
        ab_reset();ab_window_hits=0;memset(ab_window,0,512);ab_invoke(mapped+d->rva,a,pattern,expected_out);
        const unsigned hits=ab_window_hits;memcpy(expected_window,ab_window,512);memcpy(expected_frame,ab_frame_copy,64);
        unsigned calls=ab_calls;memcpy(expected,ab_log,calls*sizeof(*expected));memcpy(ab_after,ab_arena,AB_SIZE);ab_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("ai behaviour residue install");
        memcpy(ab_arena,ab_before,AB_SIZE);ab_globals(before,1);ab_reset();ab_window_hits=0;memset(ab_window,0,512);
        ab_invoke(mapped+d->rva,a,pattern,actual_out);ab_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_out[1]<<32|expected_out[0],(uint64_t)actual_out[1]<<32|actual_out[0]};
        if(pair[0]!=pair[1]||calls!=ab_calls||memcmp(expected,ab_log,calls*sizeof(*expected))||memcmp(ab_after,ab_arena,AB_SIZE)||
           memcmp(after,actual,AB_GLOBALS)||hits!=ab_window_hits||h.calls!=1)
            status=ab_report(fast?"residue fast":"residue preserving",n,pair,calls,expected);
        else if(memcmp(expected_window,ab_window,512)){
            fprintf(stderr,"FAIL: %s case %u (residue window below +28h)\n",current_function,n);
            for(unsigned j=0;j<512;++j)if(expected_window[j]!=ab_window[j]){fprintf(stderr," entry-%u: %02x/%02x\n",512-j,expected_window[j],ab_window[j]);break;}
            /* The bytes the weapon bodies read: fire, emitter, alternate
             * padding sources and the two exclusion dwords. */
            static const unsigned pads[]={65,97,73,105,61,93};
            for(unsigned j=0;j<6;++j)fprintf(stderr," entry-%u byte %02x/%02x\n",pads[j],expected_window[512-pads[j]],ab_window[512-pads[j]]);
            fprintf(stderr," entry-184 %08x/%08x entry-172 %08x/%08x\n",get32(expected_window+512-184),get32(ab_window+512-184),
                get32(expected_window+512-172),get32(ab_window+512-172));
            status=1;
        }
        else if(memcmp(expected_frame,ab_frame_copy,64))status=ab_frame_report("residue frame",n,expected_frame);
        if(!es2_hook_uninstall(&h))return fail("ai behaviour residue uninstall");ab_globals(before,1);++total;fired+=hits;
    }
    es2_hook_set_fast_path(TRUE);ab_track=0;
    if(actor_patch(0x1ef14,NULL,saved,FALSE))return 1;
    if(!status&&fired<total/8)status=fail("ai behaviour residue probe rarely reached the +28h call");
    if(!status)printf("PASS: aim_fire %u residue cases (%u through +28h, half of them filling 192 KiB below it), fast/preserving dispatch, 31/a7 stack, 512 bytes below +28h, per-call EBX/ESI/EDI/EBP/ESP, frame below the arguments\n",total,fired);
    return status;
}
/* trigger_fire 423430 for weapon_choose's residue probe: copy the 512 bytes
 * below its entry ESP (its record bytes are entry ESP-16h..-7) before anything
 * writes there, then the logging stub. */
__attribute__((naked)) static void ab_stub_trigger_window(void){__asm__ volatile(
    "movl %esi,_ab_tmp\n\tmovl %edi,_ab_tmp+4\n\tmovl %ecx,_ab_tmp+8\n\t"
    "leal -512(%esp),%esi\n\tmovl $_ab_window,%edi\n\tmovl $128,%ecx\n\tcld\n\trep movsl\n\t"
    "movl _ab_tmp,%esi\n\tmovl _ab_tmp+4,%edi\n\tmovl _ab_tmp+8,%ecx\n\tincl _ab_window_hits\n\tjmp _ab_stub_29\n\t");}
/* weapon_choose's residue probe: trigger_fire is the window stub, the other
 * callees are the logging stubs. Half of the cases keep every weapon busy (+5Ch
 * nonzero, so none = 1) with the latch A+0A5h clear and a trigger at A+1B6h, so
 * trigger_fire is reached after 47df48, +34h and the +5Ch calls; the other half
 * are random. The original weapon_choose and the installed replacement (fast
 * and preserving dispatch, 31/a7 stack) must leave the same 512 bytes below
 * trigger_fire's entry (the record bytes the cockpit message queue receives and
 * everything below), the same per-call EBX/ESI/EDI/EBP/ESP and the same 64
 * bytes below the arguments after return. An adapter on the host-stack
 * bridge fails at case 0. */
static int ab_choose_residue_probe(void){
    const unsigned index=ES2_HOOK_INDEX_weapon_choose;const es2_hook_descriptor *d=function_hook_descriptor(index);
    BYTE saved[AB_DIRECT][5],tsaved[5];int active[AB_DIRECT];
    if(ab_patch(saved,active,ab_which(index)&~AB_BIT(29),1))return 1;
    if(actor_patch(0x23430,ab_stub_trigger_window,tsaved,TRUE))return 1;
    int status=0;unsigned total=0,fired=0;ab_track=1;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_out[3],actual_out[3],pattern=poison?0xa7a7a7a7u:0x31313131u;ab_build(index,n,a,r,0);
        ab_busy=n%2==0;
        if(ab_busy){BYTE *o=ab_host(a[0]);o[0xa5]=0;put32(o+0x1b6,AB_BASE+AB_TBL+0x40u);}
        BYTE before[AB_GLOBALS],after[AB_GLOBALS],actual[AB_GLOBALS],expected_window[512],expected_frame[64];static ab_record expected[AB_MAX];
        memcpy(ab_before,ab_arena,AB_SIZE);ab_globals(before,0);
        ab_reset();ab_window_hits=0;memset(ab_window,0,512);ab_invoke(mapped+d->rva,a,pattern,expected_out);
        const unsigned hits=ab_window_hits;memcpy(expected_window,ab_window,512);memcpy(expected_frame,ab_frame_copy,64);
        unsigned calls=ab_calls;memcpy(expected,ab_log,calls*sizeof(*expected));memcpy(ab_after,ab_arena,AB_SIZE);ab_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("ai behaviour choose residue install");
        memcpy(ab_arena,ab_before,AB_SIZE);ab_globals(before,1);ab_reset();ab_window_hits=0;memset(ab_window,0,512);
        ab_invoke(mapped+d->rva,a,pattern,actual_out);ab_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_out[1]<<32|expected_out[0],(uint64_t)actual_out[1]<<32|actual_out[0]};
        if(pair[0]!=pair[1]||calls!=ab_calls||memcmp(expected,ab_log,calls*sizeof(*expected))||memcmp(ab_after,ab_arena,AB_SIZE)||
           memcmp(after,actual,AB_GLOBALS)||hits!=ab_window_hits||h.calls!=1)
            status=ab_report(fast?"choose residue fast":"choose residue preserving",n,pair,calls,expected);
        else if(memcmp(expected_window,ab_window,512)){
            fprintf(stderr,"FAIL: %s case %u (residue window below trigger_fire)\n",current_function,n);
            for(unsigned j=0;j<512;++j)if(expected_window[j]!=ab_window[j]){fprintf(stderr," entry-%u: %02x/%02x\n",512-j,expected_window[j],ab_window[j]);break;}
            fprintf(stderr," record bytes entry-16h..-7:");
            for(unsigned j=512-0x16;j<=512-7;++j)fprintf(stderr," %02x/%02x",expected_window[j],ab_window[j]);
            fprintf(stderr,"\n");status=1;
        }
        else if(memcmp(expected_frame,ab_frame_copy,64))status=ab_frame_report("choose residue frame",n,expected_frame);
        if(!es2_hook_uninstall(&h))return fail("ai behaviour choose residue uninstall");ab_globals(before,1);++total;fired+=hits;
    }
    es2_hook_set_fast_path(TRUE);ab_track=0;ab_busy=0;
    if(actor_patch(0x23430,NULL,tsaved,FALSE))return 1;
    if(ab_patch(saved,active,0,0))return 1;
    if(!status&&fired<total/4)status=fail("ai behaviour choose residue probe rarely reached trigger_fire");
    if(!status)printf("PASS: weapon_choose %u residue cases (%u through trigger_fire), fast/preserving dispatch, 31/a7 stack, 512 bytes below trigger_fire (record bytes entry-16h..-7), per-call EBX/ESI/EDI/EBP/ESP, frame below the arguments\n",total,fired);
    return status;
}
/* The torso servos 41a550/41a808 under the real torso_aim of the real-fire
 * probe: a pair that depends on every slot. */
static uint64_t __cdecl ab_servo_twist(uint32_t p,uint32_t a,uint32_t b,uint32_t c) {
    return (uint64_t)(a*3u+b)<<32|(p^c^0x1a550u);
}
static uint64_t __cdecl ab_servo_pitch(uint32_t p,uint32_t a,uint32_t b,uint32_t c,uint32_t e) {
    return (uint64_t)(a+b*5u+e)<<32|(p^c^0x1a808u);
}
/* aim_fire through the real weapon_fire 40e964 and preparation 40e788 (ai.md,
 * "stack residue under aim_fire"): the weapon is weapons_cases.h's fire weapon
 * with a vtable whose +28h is 40e964 (+2Ch and +60h stay logging stubs), the
 * shooter A gets the model, record and matrix of weapon_fire_prepare_modes, the
 * constructors 40a9c4/40b43c and 40f540 are stubs, the math is the original's.
 * Preparation's two padding bytes (fire entry ESP-41h/-61h = T-45h/-65h for
 * torso_aim's entry ESP T) reach 4a9847 on the local-matrix paths. torso_aim is
 * (0) ab_stub_framed, whose bytes there are caller EBP/ESI bytes, (1) the
 * original 41ef14 (its frame: byte 7 of the transformed point at EBP-48h and
 * byte 19 of the matrix at EBP-74h, a copy of A+25h) or (2) the replaced
 * torso_aim hook (its entry's untouched FPU reservation) -- the live bisect's
 * combinations, with the servos stubbed and the rest of torso_aim's callees
 * original; (3) the original torso_aim in the reference run and the replaced
 * one with its entry shim in the compared run, under the original aim_fire (n/2
 * even) or the replaced one: the replaced torso_aim must leave the original's
 * frame bytes, so 4a9847 is the same (without the shim it is the older fill or
 * x87 bytes, ai.md "stack residue under aim_fire"). Both weapon_prepare and
 * weapon_fire run in all four original/replaced combinations with their own
 * entry shims. Compared: pair, both logs, both arenas, ab_globals, the
 * preparation globals 4a9834.., torso_aim's angles 4a9cfc and flag 49a340. The
 * per-call register records are off here: the outputs alone must expose a wrong
 * stack. */
static int ab_real_fire_probe(void){
    const unsigned index=ES2_HOOK_INDEX_aim_fire;const es2_hook_descriptor *d=function_hook_descriptor(index);
    BYTE fsaved[5],wsaved[5][5],old_r2[80],old_torso[7],psaved[AB_DIRECT][5];int pactive[AB_DIRECT];
    r2_global_copy(old_r2,0);memcpy(old_torso,mapped+0xa9cfc,6);old_torso[6]=mapped[0x9a340];
    const unsigned rvas[]={0xa9c4,0xb43c,0xf540,0x1a550,0x1a808};
    void *stubs[]={weapon_projectile_stub,weapon_bullet_stub,fire_prep_offset,ab_servo_twist,ab_servo_pitch};
    for(unsigned i=0;i<5;++i)if(actor_patch(rvas[i],stubs[i],wsaved[i],TRUE))return 1;
    int status=0;unsigned total=0,fired=0,residue=0;
    for(unsigned torso=0;torso<4&&!status;++torso){
        if(ab_patch(psaved,pactive,ab_which(index)&~AB_BIT(30),1))return 1;
        if(!torso&&actor_patch(0x1ef14,ab_stub_framed,fsaved,TRUE))return 1;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned modes=0;modes<4&&!status;++modes)for(unsigned n=0;n<128&&!status;++n){
        uint32_t a[6],r[3],wa[6],wr[3],expected_out[3],actual_out[3],pattern=n&1?0xa7a7a7a7u:0x31313131u;
        ab_build(index,n,a,r,0);weapon_build(ES2_HOOK_INDEX_weapon_fire,n,wa,wr);
        const uint32_t b=(uint32_t)(uintptr_t)weapon_arena,W=weapon_w,A=AB_ACTOR(0);
        put32(weapon_arena+0xa04,b+0xa40);put32(weapon_arena+0xa40,(uint32_t)(uintptr_t)fire_prep_table);fire_prep_table[8]=(uint32_t)(uintptr_t)fire_prep_lookup;
        put16(weapon_arena+0xac4,0);memset(weapon_arena+0xad0,0,6);
        put32(weapon_arena+0xa16,b+0xb00);memset(weapon_arena+0xb00,0,32);
        put16(weapon_arena+0xb00,16384);put16(weapon_arena+0xb08,16384);put16(weapon_arena+0xb10,16384);weapon_arena[0xb12]=(BYTE)(n%3);
        put32(weapon_arena+0x7b,n%4<2?0:b+0xad0);put32(weapon_arena+0x7f,n%4%2?0:b+0xad0);put32(mapped+0x9ef34,0);
        memset(ab_fire_vt,0,sizeof(ab_fire_vt));ab_fire_vt[0x28/4]=(uint32_t)(uintptr_t)(mapped+0xe964);
        ab_fire_vt[0x2c/4]=(uint32_t)(uintptr_t)ab_stubs[34];ab_fire_vt[0x60/4]=(uint32_t)(uintptr_t)ab_stubs[39];
        put32((BYTE *)(uintptr_t)W,(uint32_t)(uintptr_t)ab_fire_vt);ab_fire_weapon=W;
        put32(ab_host(A)+0x34,b+0xa00);put16(ab_host(A)+0x32,(uint16_t)(n/2%2));put32(ab_host(A)+0x2ac,W);
        BYTE before[AB_GLOBALS],after[AB_GLOBALS],actual[AB_GLOBALS],r2_before[80],r2_after[80],r2_actual[80],t_before[7],t_after[7];
        static ab_record expected[AB_MAX];static weapon_record wexpected[48];
        memcpy(ab_before,ab_arena,AB_SIZE);memcpy(weapon_before,weapon_arena,sizeof(weapon_arena));ab_globals(before,0);r2_global_copy(r2_before,0);
        memcpy(t_before,mapped+0xa9cfc,6);t_before[6]=mapped[0x9a340];
        es2_hook h[4];memset(h,0,sizeof(h));es2_hook_set_fast_path(fast);
        const unsigned nested[]={ES2_HOOK_INDEX_weapon_prepare,ES2_HOOK_INDEX_weapon_fire,ES2_HOOK_INDEX_torso_aim};
        for(unsigned j=0;j<3;++j){es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
            o.mode=j<2?(es2_hook_mode)((modes>>j)&1):torso==2?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!nested_install_hook(h+j,nested[j],&o,function_hook_extension(nested[j])))return fail("ai behaviour real fire nested install");}
        const int aim_original=torso==3&&n/2%2==0;
        ab_reset();weapon_reset();ab_invoke(mapped+d->rva,a,pattern,expected_out);
        const unsigned calls=ab_calls,wcalls=weapon_logged;memcpy(expected,ab_log,calls*sizeof(*expected));memcpy(wexpected,weapon_log,sizeof(wexpected));
        memcpy(ab_after,ab_arena,AB_SIZE);memcpy(weapon_after,weapon_arena,sizeof(weapon_arena));ab_globals(after,0);r2_global_copy(r2_after,0);
        memcpy(t_after,mapped+0xa9cfc,6);t_after[6]=mapped[0x9a340];
        es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        if(torso==3){if(!es2_hook_uninstall(h+2))return fail("ai behaviour real fire torso uninstall");memset(h+2,0,sizeof(h[2]));
            if(!weapons_install_hook(h+2,function_hook_descriptor(ES2_HOOK_INDEX_torso_aim),&o,function_hook_extension(ES2_HOOK_INDEX_torso_aim)))
                return fail("ai behaviour real fire torso install");}
        o.mode=aim_original?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(h+3,d,&o,function_hook_extension(index)))return fail("ai behaviour real fire install");
        memcpy(ab_arena,ab_before,AB_SIZE);memcpy(weapon_arena,weapon_before,sizeof(weapon_arena));ab_globals(before,1);r2_global_copy(r2_before,1);
        memcpy(mapped+0xa9cfc,t_before,6);mapped[0x9a340]=t_before[6];
        ab_reset();weapon_reset();ab_invoke(mapped+d->rva,a,pattern,actual_out);ab_globals(actual,0);r2_global_copy(r2_actual,0);
        uint64_t pair[2]={(uint64_t)expected_out[1]<<32|expected_out[0],(uint64_t)actual_out[1]<<32|actual_out[0]};
        if(pair[0]!=pair[1]||calls!=ab_calls||memcmp(expected,ab_log,calls*sizeof(*expected))||memcmp(ab_after,ab_arena,AB_SIZE)||
           memcmp(after,actual,AB_GLOBALS)||wcalls!=weapon_logged||memcmp(wexpected,weapon_log,sizeof(wexpected))||
           memcmp(weapon_after,weapon_arena,sizeof(weapon_arena))||memcmp(r2_after,r2_actual,74)||memcmp(t_after,mapped+0xa9cfc,6)||
           t_after[6]!=mapped[0x9a340]||h[3].calls!=(aim_original?0u:1u)||(torso>=2&&!h[2].calls)){
            fprintf(stderr,"real fire torso=%u fast=%u modes=%u\n",torso,fast,modes);status=ab_report("real fire",n,pair,calls,expected);
            for(unsigned j=0;j<74;++j)if(r2_after[j]!=r2_actual[j])fprintf(stderr," preparation global byte %u %02x/%02x\n",j,r2_after[j],r2_actual[j]);
        }
        for(unsigned j=0;j<4;++j)if(!es2_hook_uninstall(h+j))return fail("ai behaviour real fire uninstall");
        ab_globals(before,1);r2_global_copy(r2_before,1);memcpy(mapped+0xa9cfc,t_before,6);mapped[0x9a340]=t_before[6];++total;
        if(wcalls){++fired;if(n%4!=2)++residue;}
    }
        if(!torso&&actor_patch(0x1ef14,NULL,fsaved,FALSE))return 1;
        if(ab_patch(psaved,pactive,0,0))return 1;
    }
    es2_hook_set_fast_path(TRUE);ab_fire_weapon=0;r2_global_copy(old_r2,1);memcpy(mapped+0xa9cfc,old_torso,6);mapped[0x9a340]=old_torso[6];
    for(unsigned i=0;i<5;++i)if(actor_patch(rvas[i],NULL,wsaved[i],FALSE))return 1;
    if(!status&&residue<total/8)status=fail("ai behaviour real fire probe rarely reached a local-matrix preparation");
    if(!status)printf("PASS: aim_fire %u real-fire cases (%u fired, %u on a local-matrix preparation path), torso_aim framed stub/original/replaced/original against replaced, weapon_fire/weapon_prepare in all four modes, fast/preserving dispatch, 4a9834.., 4a9cfc and both logs\n",
        total,fired,residue);
    return status;
}
/* Real callees with the nested hooks in rotating original/replaced/compare
 * modes (compare falls back to replaced where refused). Stubs that stay
 * (keep): torso_center, torso_aim, trigger_fire, vehicle_control,
 * actor_destination, target_valid, part_pick, 41fac4, 41fb60, and under
 * clear_shot the terrain and object walks of line_blocked; the virtuals
 * stay arena stubs. line_blocked has no real pass (its three callees are
 * the terrain and object walks, covered by their own groups), weapon_range
 * is a leaf. */
static unsigned ab_nested(unsigned index,unsigned nested[8],uint32_t *keep){
    unsigned count=0;*keep=0;
#define AB_N(name) nested[count++]=ES2_HOOK_INDEX_##name
    if(index==ES2_HOOK_INDEX_weapon_choose){AB_N(scalar_interpolate);AB_N(weapon_range);AB_N(multiply_q10);AB_N(rng_bounded);
        *keep=AB_BIT(29);}
    else if(index==ES2_HOOK_INDEX_aim_fire){AB_N(dist2d_point);AB_N(weapon_range);AB_N(weapon_choose);AB_N(point_offset);
        AB_N(rng_bounded);AB_N(multiply_q10);*keep=AB_BIT(29)|AB_BIT(30);}
    else if(index==ES2_HOOK_INDEX_aim_target){AB_N(heading_point);AB_N(dist_gate);AB_N(euler_matrix);AB_N(transform_point);
        AB_N(aim_fire);AB_N(weapon_choose);AB_N(weapon_range);*keep=AB_BIT(4)|AB_BIT(29)|AB_BIT(30);}
    else if(index==ES2_HOOK_INDEX_target_search){AB_N(dist2d_point);*keep=AB_BIT(15);}
    else if(index==ES2_HOOK_INDEX_hunt){AB_N(timer_word_step);AB_N(target_search);AB_N(script_start);AB_N(dist2d_point);
        AB_N(heading_point);*keep=AB_BIT(0)|AB_BIT(2)|AB_BIT(4)|AB_BIT(14)|AB_BIT(15)|AB_BIT(17)|AB_BIT(20);}
    else if(index==ES2_HOOK_INDEX_clear_shot){AB_N(heading_point);AB_N(timer_dword_step);AB_N(line_blocked);AB_N(script_start);
        AB_N(dist2d_point);AB_N(point_offset);*keep=AB_BIT(4)|AB_BIT(5)|AB_BIT(6)|AB_BIT(7)|AB_BIT(14);}
    else if(index==ES2_HOOK_INDEX_group_move){AB_N(formation_steer);AB_N(dist2d_point);AB_N(heading_point);AB_N(euler_matrix);
        *keep=AB_BIT(0)|AB_BIT(1)|AB_BIT(4)|AB_BIT(14);}
#undef AB_N
    return count;
}
static int ab_real_pass(unsigned index){
    unsigned nested[8];uint32_t keep;unsigned count=ab_nested(index,nested,&keep);
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[8]={0};
    BYTE saved[AB_DIRECT][5];int active[AB_DIRECT];
    if(ab_patch(saved,active,keep,1))return 1;
    static ab_record expected[AB_MAX];
    ab_track=index==ES2_HOOK_INDEX_aim_fire||index==ES2_HOOK_INDEX_weapon_choose;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_out[3],actual_out[3],pattern=n&1?0xa7a7a7a7u:0x31313131u;ab_build(index,n,a,r,1);BYTE expected_frame[64];
        BYTE before[AB_GLOBALS],after[AB_GLOBALS],actual[AB_GLOBALS];
        memcpy(ab_before,ab_arena,AB_SIZE);ab_globals(before,0);
        es2_hook hk[9];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!nested_install_hook(hk+j,nested[j],&o,e))return fail("ai behaviour nested install");}
        ab_reset();ab_invoke(mapped+d->rva,a,pattern,expected_out);memcpy(expected_frame,ab_frame_copy,64);
        unsigned calls=ab_calls;memcpy(expected,ab_log,calls*sizeof(*expected));
        memcpy(ab_after,ab_arena,AB_SIZE);ab_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+8,d,&o,function_hook_extension(index)))return fail("ai behaviour caller install");
        memcpy(ab_arena,ab_before,AB_SIZE);ab_globals(before,1);ab_reset();
        ab_invoke(mapped+d->rva,a,pattern,actual_out);ab_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_out[1]<<32|expected_out[0],(uint64_t)actual_out[1]<<32|actual_out[0]};
        if(pair[0]!=pair[1]||calls!=ab_calls||memcmp(expected,ab_log,calls*sizeof(*expected))||memcmp(ab_after,ab_arena,AB_SIZE)||
           memcmp(after,actual,AB_GLOBALS)){
            fprintf(stderr,"real mode=%u\n",mode);status=ab_report("real",n,pair,calls,expected);
        }
        else if(ab_track&&memcmp(expected_frame,ab_frame_copy,64)){fprintf(stderr,"real mode=%u\n",mode);status=ab_frame_report("real frame",n,expected_frame);}
        for(unsigned j=0;j<9;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("ai behaviour nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("ai behaviour nested uninstall");}
        ab_globals(before,1);++total;
    }
    if(ab_patch(saved,active,0,0))return 1;
    ab_real=0;ab_track=0;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, 31/a7 stack, pair, memory, globals and stub inputs\n",
        d->name,total,count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
/* Compare cases (compare_probes.h) for weapon_range (its entry shim hands both
 * phases the ECX [W+1Ch]). */
static void ab_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){ab_build(index,n,a,r,1);}
static void ab_compare_state(BYTE *s,int restore){
    if(restore){memcpy(ab_arena,s,AB_SIZE);ab_globals(s+AB_SIZE,1);}else{memcpy(s,ab_arena,AB_SIZE);ab_globals(s+AB_SIZE,0);}
}
static int ai_behaviour_cases(unsigned index,unsigned limit){
    uint32_t which=ab_which(index);
    BYTE saved[AB_DIRECT][5],old_globals[AB_GLOBALS];int active[AB_DIRECT];
    ab_globals(old_globals,0);
    if(limit&&compare_probe_installed(index)){int status=compare_probe_run(index,limit,ab_compare_build,ab_compare_state,AB_SIZE+AB_GLOBALS);
        ab_globals(old_globals,1);return status;}
    if(ab_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_AI_BEHAVIOUR_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];ab_build(index,n,a,r,0);status=ab_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=ab_installed_probes(index,64);
    if(!limit&&!status&&index==ES2_HOOK_INDEX_aim_fire)status=ab_residue_probe();
    if(ab_patch(saved,active,which,0))return 1;
    if(!limit&&!status&&index==ES2_HOOK_INDEX_weapon_choose)status=ab_choose_residue_probe();
    if(!limit&&!status&&index==ES2_HOOK_INDEX_aim_fire)status=ab_real_fire_probe();
    current_case_valid=0;
    if(!limit&&!status&&index!=ES2_HOOK_INDEX_weapon_range&&index!=ES2_HOOK_INDEX_line_blocked)status=ab_real_pass(index);
    if(!limit&&!status&&compare_probe_capable(index))status=compare_probe_run(index,0,ab_compare_build,ab_compare_state,AB_SIZE+AB_GLOBALS);
    ab_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, frame payloads by content, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
