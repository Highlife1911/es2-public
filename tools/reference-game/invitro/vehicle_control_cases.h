/* Vehicle control: part_ratio 40dbc0, slope_speed 41693c, random_damage 417a04
 * and vehicle_control 416a04 (ai.md). Direct cases run the original body and
 * the core against logging stubs for every game callee (ordered arguments,
 * entry EAX/EDX/ECX, frame payloads by content; values in the worker's stack
 * reservation are masked), part_ratio on generated part tables without stubs.
 * Then installed 31/a7 poison probes and a real-callee pass with the nested
 * hooks in rotating modes (real RNG, timer, multiplies, approach, part tables,
 * slope rotation); only the terrain face query, obstacle steering, the two
 * track calls, sound and the vehicle virtual +74h stay stubs there, and their
 * inputs are compared. slope_speed accepts compare; compare_probes.h's
 * installed compare cases run it with the real terrain_face on camera_cases.h's
 * generated grids and the real euler_matrix. */
typedef struct vc_record { uint32_t id,args[4],regs[3],input[12],eax,edx; } vc_record;
enum { VC_SIZE=0x1400, VC_ACTOR=0x40, VC_CONFIG=0x480, VC_TRACK=0x500, VC_TRACK2=0x560, VC_DAMAGE=0x5c0, VC_PART_INFO=0x660,
    VC_FACE=0x680, VC_VTABLE=0x690, VC_PARTS=0x710, VC_SPEED=0x750, VC_OBJ=0x760, VC_TABLE=0x780, VC_ENTRIES=0x7c0,
    VC_ITEMS=0xb80, VC_MAXIMA=0xc40, VC_VALUES=0xcc0, VC_GROUPS=0xd40, VC_GROUP_LOW=-4, VC_GROUP_HIGH=40, VC_PART_LOW=-8,
    VC_PART_HIGH=40, VC_MAX=160, VC_IDS=17, VC_REAL_MAX=4096, VC_GLOBALS=124 };
#define VC_BASE ((uint32_t)(uintptr_t)vc_arena)
static BYTE vc_arena[VC_SIZE],vc_before[VC_SIZE],vc_after[VC_SIZE];
static vc_record vc_log[VC_MAX];
static uint32_t vc_real_log[VC_REAL_MAX];
static unsigned vc_calls,vc_real_count;
static int vc_real;
static uint32_t vc_seed,vc_stream;
__attribute__((used)) static uint32_t vc_ecx;
/* 0 46e498, 1 47ebb8 (slope_speed); 2 492ee0, 3 492f24, 4 40dc58, 5 47e0a0,
 * 6 the vehicle virtual +74h (random_damage; 47e0a0 also in 416a04);
 * 7 467aa8, 8 41693c, 9 416274, 10 47e0b0, 11 40dbc0, 12 467adc, 13 4792ac,
 * 14 47967c, 15 4628e0, 16 417a04 (vehicle_control). */
static const uint32_t vc_addresses[VC_IDS]={0x46e498,0x47ebb8,0x492ee0,0x492f24,0x40dc58,0x47e0a0,0,0x467aa8,0x41693c,
    0x416274,0x47e0b0,0x40dbc0,0x467adc,0x4792ac,0x47967c,0x4628e0,0x417a04};
static const unsigned vc_counts[VC_IDS]={2,2,1,2,2,2,4,1,2,4,2,2,3,4,4,2,3};
static uint32_t vc_next(void){
    uint32_t z=vc_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *vc_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
/* A fixed window (the worker's 16 MiB stack reservation), so equal values
 * are masked identically in both runs; frame addresses always fall inside. */
static int vc_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
/* Speed, turn and rate words around the function's thresholds. */
static uint16_t vc_speed_word(uint32_t pick){
    switch(pick%10){
    case 0:return 0;
    case 1:return (uint16_t)(40u+pick/16%12u);
    case 2:return (uint16_t)(0u-(40u+pick/16%12u));
    case 3:return pick/16%7==0?0x8000u:(uint16_t)(pick>>12);
    case 4:case 5:return (uint16_t)(pick/16%400u);
    case 6:return (uint16_t)(0u-pick/16%400u);
    default:return (uint16_t)(pick/16%1200u-600u);
    }
}
static uint16_t vc_turn_word(uint32_t pick){
    switch(pick%8){
    case 0:return (uint16_t)(0xfcu+pick/8%9u);
    case 1:return (uint16_t)(0u-(0xfcu+pick/8%9u));
    case 2:return (uint16_t)(47u+pick/8%7u);
    case 3:return (uint16_t)(0u-(47u+pick/8%7u));
    case 4:return pick/8%5==0?0x8000u:(uint16_t)(pick>>13);
    case 5:return 0;
    default:return (uint16_t)(pick/8%0x400u-0x200u);
    }
}
static uint16_t vc_rate_word(uint32_t pick){
    static const uint16_t rates[]={0,1,0xffff,0x3b,0x3c,0x3d,0xffc5,0xffc4,0xffc3,0x8000,0x7fff,0x64,0xff9c};
    return pick%3?rates[pick/3%13]:(uint16_t)(pick>>8);
}
static uint64_t vc_real_stub(unsigned id,const uint32_t *a,uint32_t eax,uint32_t edx,uint32_t pick){
    if(vc_real_count+12>VC_REAL_MAX){fail("vehicle real stub overflow");ExitProcess(1);}
    uint32_t *o=vc_real_log+vc_real_count;o[0]=id;o[1]=vc_stack(a[0])?0:a[0];unsigned k=2;
    switch(id){
    case 0:for(unsigned j=0;j<3;++j)o[k++]=get32(vc_host(a[1])+4*j);eax=pick%4==0?0:VC_BASE+VC_FACE;break;
    case 6:o[k++]=a[1];o[k++]=a[2];o[k++]=a[3];break;
    case 9: /* Only bit 0 of the flags slot is read (4162d0, 4165c4); its
             * high word can be a frame address of the real 41693c. */
        o[k++]=(uint16_t)a[3];o[k++]=get16(vc_host(a[1]));o[k++]=get16(vc_host(a[2]));
        if(pick%3==0)put16(vc_host(a[1]),vc_turn_word(vc_next()));
        if(pick%5==0)put16(vc_host(a[2]),vc_speed_word(vc_next()));
        break;
    case 13:case 14:o[k++]=a[1];o[k++]=a[2];o[k++]=a[3];break;
    case 15:o[k++]=a[0];for(unsigned j=0;j<3;++j)o[k++]=get32(vc_host(a[1])+4*j);break;
    default:fail("vehicle real stub id");ExitProcess(1);
    }
    vc_real_count+=k;return (uint64_t)edx<<32|eax;
}
__attribute__((used,noinline)) static uint64_t __cdecl vc_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    uint32_t eax=vc_next(),edx=vc_next(),pick=vc_next();vc_ecx=vc_next();
    if(vc_real)return vc_real_stub(id,a,eax,edx,pick);
    if(vc_calls==VC_MAX){fail("vehicle synthetic call overflow");ExitProcess(1);}
    vc_record *v=vc_log+vc_calls++;memset(v,0,sizeof(*v));v->id=id;
    for(unsigned k=0;k<vc_counts[id];++k)v->args[k]=vc_stack(a[k])?0:a[k];
    for(unsigned j=0;j<3;++j)v->regs[j]=vc_stack(regs[j])?0:regs[j];
    BYTE *p=vc_arena+VC_ACTOR;
    switch(id){
    case 0: /* Terrain face of the position: the face record or null. */
        for(unsigned j=0;j<3;++j)v->input[j]=get32(vc_host(a[1])+4*j);
        eax=pick%4==0?0:VC_BASE+VC_FACE;
        break;
    case 1: /* Euler: angles in, coefficients out. */
        for(unsigned j=0;j<3;++j)v->input[j]=get16(vc_host(a[0])+2*j);
        if(pick%3)for(unsigned j=0;j<9;++j)put16(vc_host(a[1])+2*j,(uint16_t)vc_next());
        break;
    case 4:if(pick%2)eax=(eax&0xffff0000u)|vc_next()%3000u;break;
    case 6: /* Part damage: may change later part words (re-read per part). */
        if(pick%4==0)put16(vc_host(get32(p+0x20e)+2u*(vc_next()%29u)),(uint16_t)(pick&0x100?0:1+vc_next()%9));
        break;
    case 7:v->input[0]=get32(vc_host(a[0]));eax=pick%2?0:eax|1u;break;
    case 8:v->input[0]=get16(vc_host(a[1]));if(pick%2)put16(vc_host(a[1]),vc_speed_word(vc_next()));break;
    case 9:
        v->input[0]=get16(vc_host(a[1]));v->input[1]=get16(vc_host(a[2]));
        if(pick%3==0)put16(vc_host(a[1]),vc_turn_word(vc_next()));
        if(pick%5==0)put16(vc_host(a[2]),vc_speed_word(vc_next()));
        break;
    case 10:if(pick%2)eax=(eax&0xffff0000u)|(uint16_t)(vc_next()%600u-300u);break;
    case 11: {
        static const uint16_t ratios[]={0xe0,0xe1,0xe2,0,1,50,51,0xff,0x100,0xffff,0x8000,0x7fff,0xff00,0xffcd};
        eax=(eax&0xffff0000u)|(pick%4?ratios[(pick>>8)%14]:(uint16_t)vc_next());
        break; }
    case 12: { /* approach_word: the speed word (first call) or the turn rate. */
        BYTE *w=vc_host(a[0]);uint16_t now=get16(w);v->input[0]=now;
        if(a[0]==VC_BASE+VC_ACTOR+0x28e) {
            static const int deltas[]={1,-1,5,-5,0};
            switch(pick%9){
            case 0:case 1:break;
            case 2:put16(w,0);break;
            case 3:put16(w,(uint16_t)(0u-now));break;
            case 4:case 5:put16(w,(uint16_t)(now+(uint32_t)deltas[(pick>>8)%5]));break;
            case 6:put16(w,(uint16_t)a[1]);break;
            case 7:put16(w,vc_speed_word(vc_next()));break;
            default:put16(w,(pick>>8)%3==0?0x8000u:(uint16_t)(vc_next()%60u));break;
            }
        } else if(pick%2)put16(w,(uint16_t)vc_next());
        break; }
    case 13:case 14: /* Track calls: the track's clip words; may switch [A+22Ch]. */
        for(unsigned j=0;j<6;++j)v->input[j]=get16(vc_host(a[0])+4+2*j);
        if(pick%16==0)put32(p+0x22c,VC_BASE+(pick&0x100?VC_TRACK2:VC_TRACK));
        if(pick%8==1)put16(vc_host(a[0])+6,(uint16_t)vc_next());
        break;
    case 15:for(unsigned j=0;j<3;++j)v->input[j]=get32(vc_host(a[1])+4*j);break;
    case 16:
        if(pick%4==0)put16(p+0x28e,vc_speed_word(vc_next()));
        if(pick%4==1)put16(p+0x2a0,vc_rate_word(vc_next()));
        break;
    default:break;
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
/* Pushes the entry EAX/EDX/ECX for the record before the slot pointer. */
#define VC_STUB(n) __attribute__((naked)) static void vc_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _vc_stub_impl\n\taddl $24,%esp\n\tmovl _vc_ecx,%ecx\n\tret\n\t");}
VC_STUB(0) VC_STUB(1) VC_STUB(2) VC_STUB(3) VC_STUB(4) VC_STUB(5) VC_STUB(6) VC_STUB(7) VC_STUB(8) VC_STUB(9)
VC_STUB(10) VC_STUB(11) VC_STUB(12) VC_STUB(13) VC_STUB(14) VC_STUB(15) VC_STUB(16)
#undef VC_STUB
static void *vc_stubs[VC_IDS]={vc_stub_0,vc_stub_1,vc_stub_2,vc_stub_3,vc_stub_4,vc_stub_5,vc_stub_6,vc_stub_7,vc_stub_8,
    vc_stub_9,vc_stub_10,vc_stub_11,vc_stub_12,vc_stub_13,vc_stub_14,vc_stub_15,vc_stub_16};
/* Terrain map pointer, player vehicle, time scale word, RNG stream A. */
static void vc_globals(BYTE *out,int restore){
    static const unsigned offsets[]={0xa0be0,0xa9b84,0xd3b64,0xd2599},sizes[]={4,4,2,0x72};
    for(unsigned i=0;i<4;++i){if(restore)memcpy(mapped+offsets[i],out,sizes[i]);else memcpy(out,mapped+offsets[i],sizes[i]);out+=sizes[i];}
}
static void vc_reset(void){vc_calls=vc_real_count=0;vc_stream=vc_seed;}
static uint16_t vc_signed_word(uint32_t limit){return (uint16_t)(random32()%(2*limit+1)-limit);}
/* Part tables below obj (40dbc0, 40dc58): [obj] part values, [obj+4] group
 * values, [obj+0Ch] -> {18-byte group records, part maxima}. Groups -4..39
 * and parts -8..39 stay inside the arena; denominators can be checked with
 * vc_divisor. */
static void vc_tables(uint32_t obj,int real){
    uint32_t b=VC_BASE;BYTE *o=vc_host(obj);
    put32(o,b+VC_VALUES+16);put32(o+4,b+VC_GROUPS+8);put32(o+0xc,b+VC_TABLE);
    put32(vc_arena+VC_TABLE,b+VC_ENTRIES+72);put32(vc_arena+VC_TABLE+4,b+VC_MAXIMA+16);
    for(int p=VC_PART_LOW;p<VC_PART_HIGH;++p){
        put16(vc_arena+VC_MAXIMA+16+2*p,random32()%8?(uint16_t)(1+random32()%3000):vc_signed_word(real?100:2000));
        put16(vc_arena+VC_VALUES+16+2*p,random32()%4?(uint16_t)(random32()%3000):vc_signed_word(3000));
    }
    for(int k=VC_GROUP_LOW;k<VC_GROUP_HIGH;++k){
        BYTE *e=vc_arena+VC_ENTRIES+72+18*k;
        put16(e,random32()%8?(uint16_t)(1+random32()%2000):vc_signed_word(500));
        put16(e+6,random32()%6?(uint16_t)(random32()%9):random32()%2?vc_signed_word(3):(uint16_t)(0x8000u|random32()));
        put32(e+8,b+VC_ITEMS+4*(random32()%24));
        put16(vc_arena+VC_GROUPS+8+2*k,random32()%3?(uint16_t)(random32()%2000):vc_signed_word(2000));
    }
    for(unsigned j=0;j<32;++j){put16(vc_arena+VC_ITEMS+4*j,(uint16_t)random32());
        put16(vc_arena+VC_ITEMS+4*j+2,(uint16_t)(random32()%(VC_PART_HIGH-VC_PART_LOW)+VC_PART_LOW));}
}
/* The group's denominator (base word plus the listed maxima); zero or an
 * IDIV overflow would fault in the original, so generators avoid them. */
static int vc_divisor_ok(uint32_t obj,uint16_t group){
    BYTE *o=vc_host(obj);uint32_t tab=get32(o+0xc),entry=get32(vc_host(tab))+18u*(uint32_t)(int32_t)(int16_t)group;
    uint32_t maxima=get32(vc_host(tab)+4),items=get32(vc_host(entry)+8);
    int32_t max=(int16_t)get16(vc_host(entry)),cur=(int16_t)get16(vc_host(get32(o+4)+2u*(uint32_t)(int32_t)(int16_t)group));
    if(cur<0)cur=max;
    for(int i=0;i<(int16_t)get16(vc_host(entry)+6);++i){
        int32_t p=(int16_t)get16(vc_host(items+4u*(uint32_t)i+2));int32_t x=(int16_t)get16(vc_host(get32(o)+2u*(uint32_t)p));
        int32_t top=(int16_t)get16(vc_host(maxima+2u*(uint32_t)p));
        if(x<0)x=top;
        cur+=x;max+=top;
    }
    return max!=0&&!(max==-1&&(uint32_t)cur<<8==0x80000000u);
}
static uint16_t vc_clip_ids[6];
static uint16_t vc_clip(void){return random32()%8?vc_clip_ids[random32()%6]:(uint16_t)(11+random32()%10);}
static void vc_config(BYTE *s){
    for(unsigned j=0;j<6;++j)vc_clip_ids[j]=(uint16_t)(1+random32()%(random32()%4?10:4));
    static const unsigned clip_offsets[]={0xc,0xe,0x10,0x12,0x44,0x7a};
    for(unsigned j=0;j<6;++j)put16(s+clip_offsets[j],vc_clip_ids[j]);
    put16(s,random32()%6?(uint16_t)(random32()%3000):(uint16_t)random32());
    put16(s+2,random32()%3?(uint16_t)(random32()%120):vc_signed_word(200));
    uint16_t top=random32()%6?(uint16_t)(random32()%600):random32()%2?vc_signed_word(60):(uint16_t)random32();
    if(top==45||top==46)top=47; /* a zero tail divisor faults (ai.md) */
    put16(s+4,top);put16(s+6,(uint16_t)(random32()%200));put16(s+8,(uint16_t)(random32()%400));
    put16(s+0x2c,random32()%6?(uint16_t)(random32()%300):vc_signed_word(300));put16(s+0x6c,vc_speed_word(random32()));
}
static void vc_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    uint32_t b=VC_BASE,A=b+VC_ACTOR;vc_seed=random32();random_bytes(vc_arena,VC_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    BYTE *p=vc_arena+VC_ACTOR;
    put32(mapped+0xa0be0,random32());
    if(index==ES2_HOOK_INDEX_part_ratio){
        uint32_t obj=b+VC_OBJ;vc_tables(obj,0);
        uint16_t group=n<64?(uint16_t)(n%44-4):(uint16_t)(random32()%(VC_GROUP_HIGH-VC_GROUP_LOW)+VC_GROUP_LOW);
        while(!vc_divisor_ok(obj,group))put16(vc_host(get32(vc_arena+VC_TABLE)+18u*(uint32_t)(int32_t)(int16_t)group),(uint16_t)(1+random32()%2000));
        a[0]=obj;a[1]=(random32()&0xffff0000u)|group;
        return;
    }
    put16(p+0x32,(uint16_t)(random32()%2?0:1+random32()%0xffff));
    for(unsigned j=0;j<9;++j)put16(p+0x12+2*j,(uint16_t)(random32()%4?random32()%32769u-16384u:random32()));
    if(index==ES2_HOOK_INDEX_slope_speed){
        BYTE *f=vc_arena+VC_FACE;put16(f,(uint16_t)random32());put16(f+2,(uint16_t)random32());
        put16(vc_arena+VC_SPEED,vc_speed_word(random32()));
        a[0]=A;a[1]=b+VC_SPEED;return;
    }
    put32(p,b+VC_VTABLE);put32(vc_arena+VC_VTABLE+0x74,(uint32_t)(uintptr_t)vc_stubs[6]);
    put32(p+0x20e,b+VC_PARTS);
    for(unsigned j=0;j<29;++j)put16(vc_arena+VC_PARTS+2*j,random32()%2?0:(uint16_t)(1+random32()%0xffff));
    if(real)vc_tables(A+0x206,1);
    if(index==ES2_HOOK_INDEX_random_damage){
        static const uint16_t amounts[]={0,1,2,3,150,0xffff,0x8000,0x7fff};
        a[0]=A;a[1]=(random32()&0xffff0000u)|(random32()%2?amounts[random32()%8]:(uint16_t)(random32()%400));
        a[2]=(random32()&0xffff0000u)|(random32()%4?(uint16_t)(random32()%300):(uint16_t)random32());
        if(real){BYTE *state=mapped+0xd2599;random_bytes(state,112);state[112]=(BYTE)(random32()%56);state[113]=(BYTE)(random32()%56);
            a[1]=(random32()&0xffff0000u)|(random32()%8?150u:random32()%400u);a[2]=(random32()&0xffff0000u)|(random32()%257u);}
        return;
    }
    /* vehicle_control */
    BYTE *s=vc_arena+VC_CONFIG+2;put32(p+0x1f2,b+VC_CONFIG);vc_config(s);
    for(unsigned k=0;k<2;++k){
        BYTE *t=vc_arena+(k?VC_TRACK2:VC_TRACK);
        uint16_t cur=vc_clip();put16(t+4,cur);put16(t+8,random32()%8?cur:vc_clip());put16(t+0x16,vc_clip());
        uint16_t frame=(uint16_t)(random32()%8);put16(t+6,frame);put16(t+0xa,random32()%2?frame:(uint16_t)(random32()%8));
    }
    put32(p+0x22c,b+VC_TRACK);
    BYTE *e=p+0x92;
    e[0x12]=(BYTE)(random32()%4?0:1+random32()%255);
    for(unsigned j=0x16;j<=0x19;++j)e[j]=(BYTE)(random32()%6?0:1+random32()%255);
    e[0x1c]=(BYTE)(random32()%2?0:1+random32()%255);e[0x22]=(BYTE)(random32()%2?0:1+random32()%255);
    switch(random32()%4){
    case 0:put32(p+0x317,0);break;
    case 1:put32(p+0x317,b+VC_DAMAGE);put32(vc_arena+VC_DAMAGE+0x81,0);break;
    default:put32(p+0x317,b+VC_DAMAGE);put32(vc_arena+VC_DAMAGE+0x81,random32()|1u);break;
    }
    put32(vc_arena+VC_DAMAGE+0x18,b+VC_PART_INFO);
    vc_arena[VC_PART_INFO+0x17]=(BYTE)(real?random32()%44-23:random32());
    if(real){ /* The leg group must have a usable denominator. */
        uint16_t group=(uint16_t)((int8_t)vc_arena[VC_PART_INFO+0x17]+19);
        while(!vc_divisor_ok(A+0x206,group))put16(vc_host(get32(vc_arena+VC_TABLE)+18u*(uint32_t)(int32_t)(int16_t)group),(uint16_t)(1+random32()%2000));
    }
    put16(p+0x28e,vc_speed_word(random32()));put16(p+0x2a0,vc_rate_word(random32()));put16(p+0x28c,(uint16_t)random32());
    put32(mapped+0xa9b84,random32()%4?random32():A);
    a[0]=A;a[1]=(random32()&0xffff0000u)|vc_turn_word(random32());a[2]=(random32()&0xffff0000u)|vc_speed_word(random32());
    a[3]=random32();
    if(real){
        put16(mapped+0xd3b64,(uint16_t)(random32()%4?random32()%512:random32()));
        put32(p+0x26d,random32()%3?random32()%600:random32());
        BYTE *state=mapped+0xd2599;random_bytes(state,112);state[112]=(BYTE)(random32()%56);state[113]=(BYTE)(random32()%56);
    }
    (void)n;
}
static int vc_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const vc_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,vc_calls);
    for(unsigned j=0;j<calls&&j<vc_calls&&expected;++j)if(memcmp(expected+j,vc_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(vc_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<VC_SIZE;++j)if(vc_after[j]!=vc_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,vc_after[j],vc_arena[j]);break;}
    return 1;
}
static unsigned vc_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<vc_calls;++j){unsigned id=vc_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?id==6&&c->key==0x74:id!=6&&vc_addresses[id]==c->key)++hits;}
    return hits;
}
/* slope_speed returns its frame address EBP-0Ch when there is no face; both
 * runs then return an address inside the stack window. */
static int vc_same_pair(unsigned index,uint64_t x,uint64_t y){
    if(x==y)return 1;
    return index==ES2_HOOK_INDEX_slope_speed&&(x>>32)==(y>>32)&&vc_stack((uint32_t)x)&&vc_stack((uint32_t)y);
}
static int vc_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[VC_GLOBALS],after[VC_GLOBALS],actual[VC_GLOBALS];static vc_record expected[VC_MAX];
    memcpy(vc_before,vc_arena,VC_SIZE);vc_globals(before,0);
    vc_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=vc_calls;memcpy(expected,vc_log,calls*sizeof(*expected));
    memcpy(vc_after,vc_arena,VC_SIZE);vc_globals(after,0);
    memcpy(vc_arena,vc_before,VC_SIZE);vc_globals(before,1);vc_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);vc_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=vc_expected_hits(c+i))return fail("vehicle callee counter delta");
    if(!vc_same_pair(index,pair[0],pair[1])||calls!=vc_calls||memcmp(expected,vc_log,calls*sizeof(*expected))||memcmp(vc_after,vc_arena,VC_SIZE)||
       memcmp(after,actual,VC_GLOBALS)){
        for(unsigned j=0;j<VC_GLOBALS;++j)if(after[j]!=actual[j])fprintf(stderr," global byte %u %02x/%02x\n",j,after[j],actual[j]);
        return vc_report("direct",n,pair,calls,expected);
    }
    return 0;
}
static int vc_patch(BYTE saved[VC_IDS][5],int active[VC_IDS],unsigned which,int on){
    for(unsigned i=0;i<VC_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!vc_addresses[i])continue;if(actor_patch(vc_addresses[i]-0x400000u,vc_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(vc_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
static int vc_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;vc_build(index,n,a,r,0);
        BYTE before[VC_GLOBALS],after[VC_GLOBALS],actual[VC_GLOBALS],expected_fpu[108],actual_fpu[108];static vc_record expected[VC_MAX];
        memcpy(vc_before,vc_arena,VC_SIZE);vc_globals(before,0);
        vc_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=vc_calls;memcpy(expected,vc_log,calls*sizeof(*expected));memcpy(vc_after,vc_arena,VC_SIZE);vc_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("vehicle poison install");
        memcpy(vc_arena,vc_before,VC_SIZE);vc_globals(before,1);vc_reset();sg_poison_call(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);vc_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(!vc_same_pair(index,pair[0],pair[1])||calls!=vc_calls||memcmp(expected,vc_log,calls*sizeof(*expected))||memcmp(vc_after,vc_arena,VC_SIZE)||
           memcmp(after,actual,VC_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=vc_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("vehicle poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state\n",d->name,total);
    return status;
}
/* Real callees with the nested hooks in rotating original/replaced/compare
 * modes and a patterned stack; the stubs' inputs are compared in order. */
static int vc_real_pass(unsigned index){
    static const unsigned slope_nested[]={ES2_HOOK_INDEX_euler_matrix};
    static const unsigned damage_nested[]={ES2_HOOK_INDEX_rng_next,ES2_HOOK_INDEX_rng_bounded,ES2_HOOK_INDEX_multiply_q8};
    static const unsigned control_nested[]={ES2_HOOK_INDEX_timer_dword_step,ES2_HOOK_INDEX_slope_speed,ES2_HOOK_INDEX_multiply_q10,
        ES2_HOOK_INDEX_part_ratio,ES2_HOOK_INDEX_approach_word,ES2_HOOK_INDEX_random_damage,ES2_HOOK_INDEX_multiply_q8,
        ES2_HOOK_INDEX_euler_matrix,ES2_HOOK_INDEX_rng_next,ES2_HOOK_INDEX_rng_bounded};
    const unsigned *nested;unsigned count,stubs;
    if(index==ES2_HOOK_INDEX_slope_speed){nested=slope_nested;count=1;stubs=0x1u;}
    else if(index==ES2_HOOK_INDEX_random_damage){nested=damage_nested;count=3;stubs=0;}
    else {nested=control_nested;count=10;stubs=(1u<<0)|(1u<<9)|(1u<<13)|(1u<<14)|(1u<<15);}
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[12]={0};
    BYTE saved[VC_IDS][5];int active[VC_IDS];if(vc_patch(saved,active,stubs,1))return 1;
    static uint32_t expected_log[VC_REAL_MAX];
    vc_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<1024&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;vc_build(index,n,a,r,1);
        BYTE before[VC_GLOBALS],after[VC_GLOBALS],actual[VC_GLOBALS];
        memcpy(vc_before,vc_arena,VC_SIZE);vc_globals(before,0);
        es2_hook hk[12];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            /* Under vehicle_control the terrain face query 46e498 stays a stub
             * drawing from a stream, so a compared slope_speed would see
             * another face in its second phase: replaced there. */
            if(nested[j]==ES2_HOOK_INDEX_slope_speed&&(stubs&1u)&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("vehicle nested install");}
        vc_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        unsigned logged=vc_real_count;memcpy(expected_log,vc_real_log,4*logged);
        memcpy(vc_after,vc_arena,VC_SIZE);vc_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+count,d,&o,function_hook_extension(index)))return fail("vehicle caller install");
        memcpy(vc_arena,vc_before,VC_SIZE);vc_globals(before,1);vc_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);vc_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(!vc_same_pair(index,pair[0],pair[1])||logged!=vc_real_count||memcmp(expected_log,vc_real_log,4*logged)||
           memcmp(vc_after,vc_arena,VC_SIZE)||memcmp(after,actual,VC_GLOBALS)){
            for(unsigned j=0;j<VC_GLOBALS;++j)if(after[j]!=actual[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,after[j],actual[j]);break;}
            fprintf(stderr,"real mode=%u stub log %u/%u\n",mode,logged,vc_real_count);vc_calls=0;status=vc_report("real",n,pair,0,NULL);
        }
        for(unsigned j=0;j<=count;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("vehicle nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("vehicle nested uninstall");}
        vc_globals(before,1);++total;
    }
    vc_real=0;
    if(vc_patch(saved,active,0,0))return 1;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){
        fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, 31/a7 stack, pair, memory, globals and stub inputs\n",
        d->name,total,count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
/* Compare cases (compare_probes.h) for slope_speed with the real 46e498 (a grid
 * of camera_cases.h, the actor's position on or off it, so both the face and
 * the face-0 path occur) and the real 47ebb8. The face-0 path returns the frame
 * address EBP-0Ch in EAX, which the comparator ignores: the skew flips EDX
 * there. */
static void vc_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    vc_build(index,n,a,r,0);
    BYTE *p=vc_arena+VC_ACTOR,*map=cm_arena+CM_MAP;random_bytes(map,0x120);cm_grid(1);
    const uint32_t s=get32(map+0x108);
    put32(p+0x26,cm_coordinate(1u<<(get32(map+0x100)&31u),s));put32(p+0x2a,cm_coordinate(1u<<(get32(map+0x104)&31u),s));
    put32(p+0x2e,random32()%8192u);put32(mapped+0xa0be0,CM_BASE+CM_MAP);
}
static void vc_compare_state(BYTE *s,int restore){
    if(restore){memcpy(vc_arena,s,VC_SIZE);vc_globals(s+VC_SIZE,1);}else{memcpy(s,vc_arena,VC_SIZE);vc_globals(s+VC_SIZE,0);}
}
static int vc_compare_same(unsigned index,uint64_t want,uint64_t got){return vc_same_pair(index,want,got);}
static int vc_compare_run(unsigned index,unsigned limit){
    compare_probe_same=vc_compare_same;compare_probe_skew_edx=1;
    int status=compare_probe_run(index,limit,vc_compare_build,vc_compare_state,VC_SIZE+VC_GLOBALS);
    compare_probe_same=NULL;compare_probe_skew_edx=0;return status;
}
static int vehicle_control_cases(unsigned index,unsigned limit){
    unsigned which=index==ES2_HOOK_INDEX_part_ratio?0:index==ES2_HOOK_INDEX_slope_speed?0x3u:
        index==ES2_HOOK_INDEX_random_damage?0x3cu:0x1ffa0u;
    BYTE saved[VC_IDS][5],old_globals[VC_GLOBALS];int active[VC_IDS];
    vc_globals(old_globals,0);
    if(limit&&index==ES2_HOOK_INDEX_slope_speed&&compare_probe_installed(index)){
        int status=vc_compare_run(index,limit);vc_globals(old_globals,1);return status;}if(vc_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_VEHICLE_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];vc_build(index,n,a,r,0);status=vc_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=vc_installed_probes(index,64);
    if(vc_patch(saved,active,0,0))return 1;
    current_case_valid=0;
    if(!limit&&!status&&index!=ES2_HOOK_INDEX_part_ratio)status=vc_real_pass(index);
    if(!limit&&!status&&index==ES2_HOOK_INDEX_slope_speed)status=vc_compare_run(index,0);
    vc_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, frame payloads by content, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
