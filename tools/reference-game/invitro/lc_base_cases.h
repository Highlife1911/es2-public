/* LC_BASE update: lc_base_update 4045c8, projectile_create 40a9c4 and
 * weapon_record 40fe84 (docs/reverse-engineering/vtables.md, "lc_base_update:
 * contract"). Direct cases run the original body and the core against logging
 * stubs for every game callee of the hook under test (ordered argument slots,
 * entry EAX/EDX/ECX, buffers by content, stack addresses masked); the runtime
 * frame setup 49630c is a record stub that keeps the real FS:0 head, as in
 * base_cases.h. The stubs change what the originals reread (the disabled byte,
 * the heading, the configuration, the phase words, the bank targets, the link
 * B+1A4h, the owner's player byte and target, the counter pointer). Then
 * installed 31/a7 poison probes; for the two shim hooks the stubs also record
 * EBX, ESI, EDI, EBP, ESP and stack arguments relative to the caller's argument
 * slots, impact's uninitialized packet dword at its entry ESP-88h and the
 * matrix byte 13h, and the bytes below the slots (the frame and the callees'
 * area) are compared after return. Then a real-callee pass with the nested
 * hooks in rotating modes. Identifiers use the prefix lcb_/LCB_. */
typedef struct lcb_record { uint32_t id,args[5],regs[3],input[10],eax,edx,saved[5]; } lcb_record;
enum { LCB_SIZE=0x1800, LCB_B=0x100, LCB_C=0x380, LCB_C2=0x3a0, LCB_Q=0x3c0, LCB_P=0x4d0, LCB_VT=0x540, LCB_W=0x5c0,
    LCB_POS=0x610, LCB_ANG=0x620, LCB_DEF=0x630, LCB_COUNTER=0x660, LCB_COUNTER2=0x664, LCB_POOL=0x670, LCB_ACT=0x700,
    LCB_ASTRIDE=0x1c0, LCB_ACTORS=3, LCB_NODES=0xc40, LCB_REC=0xe40, LCB_RECS=12, LCB_PARTS=9, LCB_MAX=128, LCB_IDS=22,
    LCB_GLOBALS=144, LCB_WINDOW=0x200, LCB_BANKS=0x209 };
#define LCB_BASE ((uint32_t)(uintptr_t)lcb_arena)
#define LCB_ACTOR(i) (LCB_BASE+LCB_ACT+LCB_ASTRIDE*(uint32_t)(i))
static BYTE lcb_arena[LCB_SIZE],lcb_before[LCB_SIZE],lcb_after[LCB_SIZE],lcb_shipped[8];
static lcb_record lcb_log[LCB_MAX];
static unsigned lcb_calls;
static int lcb_real,lcb_track;
static uint32_t lcb_seed,lcb_stream;
__attribute__((used)) static uint32_t lcb_ecx,lcb_tmp,lcb_residue;
/* EBX, ESI, EDI, EBP, ESP at the last stub entry; lcb_invoke's ESP before
 * its call; the bytes below the argument slots after its call. */
__attribute__((used)) static uint32_t lcb_entry[5],lcb_origin;
__attribute__((used)) static BYTE lcb_frame_copy[LCB_WINDOW];
/* 0 403ca8 base_update, 1 403914 base_parts, 2 467a48 timer_word_step,
 * 3 411fa0 target_select, 4 49288c dist_gate, 5 492990 aim_angles,
 * 6 492f24 rng_bounded, 7 47eb30 sincos_pair, 8 47ffe8 rotate_2d,
 * 9 40a9c4 projectile_create, 10 40fe84 weapon_record, 11 47ebb8
 * euler_matrix, 12 40bf74 impact; under projectile_create: 13 49630c
 * (record), 14 471b30 pool_alloc, 15 40a948 constructor, 16 49681a memset,
 * 17 40a234 projectile_def, 18 4628e0 sound; 19..21 the virtuals +30h,
 * +6Ch, +54h (arena vtable, never patched). */
static const uint32_t lcb_addresses[LCB_IDS]={0x403ca8,0x403914,0x467a48,0x411fa0,0x49288c,0x492990,0x492f24,0x47eb30,
    0x47ffe8,0x40a9c4,0x40fe84,0x47ebb8,0x40bf74,0x49630c,0x471b30,0x40a948,0x49681a,0x40a234,0x4628e0,0,0,0};
static const unsigned lcb_slots[LCB_IDS]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x30,0x6c,0x54};
static const unsigned lcb_counts[LCB_IDS]={1,1,1,3,2,3,2,2,3,5,1,2,5,0,1,2,3,2,2,3,2,2};
/* W ranges and the distances around them. */
static const uint32_t lcb_range_values[]={1000,5000,0x30000,0x7fffffffu,0,0xffffffffu};
static const uint32_t lcb_dists[]={999,1000,1001,4999,5000,5001,0x2ffff,0x30000,0x30001,0,0x7fffffffu,0x80000000u};
static uint32_t lcb_next(void){
    uint32_t z=lcb_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *lcb_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int lcb_in(uint32_t a,uint32_t n){return a-LCB_BASE<LCB_SIZE&&LCB_SIZE-(a-LCB_BASE)>=n;}
static int lcb_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static uint32_t lcb_mask(uint32_t value){return lcb_stack(value)?(lcb_track?value-lcb_origin:0):value;}
static int lcb_ok(uint32_t p,uint32_t n){
    const NT_TIB *t=(const NT_TIB *)NtCurrentTeb();uint32_t low=(uint32_t)(uintptr_t)t->StackLimit,top=(uint32_t)(uintptr_t)t->StackBase;
    return lcb_in(p,n)||(p>=low&&p<top&&top-p>=n);
}
static void lcb_dwords(lcb_record *v,unsigned at,uint32_t p,unsigned n){
    if(lcb_ok(p,4u*n))for(unsigned j=0;j<n;++j)v->input[at+j]=get32(lcb_host(p)+4*j);
}
static uint32_t lcb_actor_or_zero(uint32_t pick){return pick%4==0?0:LCB_ACTOR(pick%LCB_ACTORS);}
/* The bank and offset of a timer argument (K+18h, S+3 or S), or -1. */
static int lcb_bank(uint32_t p,uint32_t *offset){
    uint32_t rel=p-(LCB_BASE+LCB_B+LCB_BANKS);if(rel>=3u*0x1fu)return -1;*offset=rel%0x1fu;return (int)(rel/0x1fu);
}
__attribute__((used,noinline)) static uint64_t __cdecl lcb_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(lcb_calls==LCB_MAX){fail("lc_base call overflow");ExitProcess(1);}
    lcb_record *v=lcb_log+lcb_calls++;memset(v,0,sizeof(*v));v->id=id;
    if(lcb_track){for(unsigned j=0;j<3;++j)v->saved[j]=lcb_mask(lcb_entry[j]);v->saved[3]=lcb_entry[3]-lcb_origin;v->saved[4]=lcb_entry[4]-lcb_origin;}
    for(unsigned k=0;k<lcb_counts[id];++k)v->args[k]=lcb_mask(a[k]);
    for(unsigned j=0;j<3;++j)v->regs[j]=lcb_mask(regs[j]);
    uint32_t eax=lcb_next(),edx=lcb_next(),pick=lcb_next(),b=LCB_BASE;lcb_ecx=lcb_next();
    BYTE *o=lcb_host(b+LCB_B);
    /* Perturbations of what the originals reread (direct and installed
     * cases only: the real passes keep the game's order). */
    const int perturb=!lcb_real;
    switch(id){
    case 1: /* base_parts: the disabled byte, the heading, the configuration. */
        if(perturb&&pick%5==0)o[0xa5]=(BYTE)(o[0xa5]?0:1);
        if(perturb&&pick%7==0)put16(o+0x10,(uint16_t)lcb_next());
        if(perturb&&pick%11==0)put32(o+0x1f2,b+(pick>>8&1?LCB_C2:LCB_C));
        break;
    case 2: { /* timer_word_step: AX zero half of the time; around the
               * weapons the phase, the bank target, the link and the
               * heading may change. */
        if(pick%2)eax&=0xffff0000u;else if(!(uint16_t)eax)eax|=1u;
        uint32_t off;int k=lcb_bank(a[0],&off);
        if(perturb&&k>=0&&off!=0x18u){
            BYTE *K=o+LCB_BANKS+0x1fu*(uint32_t)k,*S=K+(off&~7u);
            if(pick%7==1)put16(S+6,(uint16_t)(pick>>8&1?0:pick>>9&3));
            if(pick%9==2)put32(K+0x1b,lcb_actor_or_zero(pick>>12));
            if(pick%11==3)put32(o+0x1a4,lcb_actor_or_zero(pick>>14));
            if(pick%13==4)put16(o+0x10,(uint16_t)lcb_next());
        }
        break; }
    case 3: eax=lcb_actor_or_zero(pick); break; /* target_select */
    case 4: /* dist_gate: around the W ranges. */
        if(pick%4)eax=lcb_dists[(pick>>4)%(sizeof(lcb_dists)/4)];
        break;
    case 5: /* aim_angles: both points by content; three angle words out. */
        lcb_dwords(v,0,a[1],3);lcb_dwords(v,3,a[2],3);
        if(lcb_ok(a[0],6))for(unsigned j=0;j<3;++j)put16(lcb_host(a[0])+2*j,(uint16_t)(pick>>(j+3)&1?lcb_next():lcb_next()%0x2001u-0x1000u));
        break;
    case 7: /* sincos_pair: four words out. */
        if(lcb_ok(a[1],8))for(unsigned j=0;j<4;++j)put16(lcb_host(a[1])+2*j,(uint16_t)lcb_next());
        break;
    case 8: /* rotate_2d: the offset and the pair by content; two dwords out. */
        lcb_dwords(v,0,a[0],3);lcb_dwords(v,3,a[1],2);
        if(lcb_ok(a[2],8)){put32(lcb_host(a[2]),lcb_next()%20001u-10000u);put32(lcb_host(a[2])+4,lcb_next()%20001u-10000u);}
        break;
    case 9: /* projectile_create: the muzzle by content; the link may be gone. */
        lcb_dwords(v,0,a[1],3);
        if(perturb&&pick%5==0)put32(o+0x1a4,0);
        if(perturb&&pick%7==1)put32(o+0x1a4,LCB_ACTOR((pick>>8)%LCB_ACTORS));
        break;
    case 10: eax=b+LCB_REC+0x58u*(pick%LCB_RECS); break; /* weapon_record */
    case 11: /* euler_matrix: the angles by content; nine words and the mode
              * byte out (never byte 13h). */
        lcb_dwords(v,0,a[0],1);if(lcb_ok(a[0],6))v->input[1]=get16(lcb_host(a[0])+4);
        if(lcb_ok(a[1],0x13)){for(unsigned j=0;j<9;++j)put16(lcb_host(a[1])+2*j,(uint16_t)lcb_next());lcb_host(a[1])[0x12]=(BYTE)(pick>>4&3);}
        break;
    case 12: /* impact: the matrix by content (byte 13h and the packet dword
              * at the entry ESP-88h only where the stack is the original's). */
        if(lcb_ok(a[1],0x20)){BYTE *mx=lcb_host(a[1]);
            for(unsigned j=0;j<5;++j)v->input[j]=get32(mx+4*j);if(!lcb_track)v->input[4]&=0x00ffffffu;
            for(unsigned j=0;j<3;++j)v->input[5+j]=get32(mx+0x14+4*j);}
        if(lcb_track)v->input[8]=lcb_mask(lcb_residue);
        break;
    case 13: /* 49630c record stub returns the real FS:0 head. */
        __asm__ volatile("movl %%fs:0,%0":"=r"(eax));break;
    case 14: eax=b+LCB_NODES+8u+0x80u*(pick%4); break; /* pool_alloc: never null */
    case 15: /* constructor: the counter pointer may move (reread after). */
        if(perturb&&pick%7==0)put32(mapped+0x2cbee4,b+(pick>>8&1?LCB_COUNTER2:LCB_COUNTER));
        break;
    case 16: /* memset into the arena. */
        if(lcb_in(a[0],a[2]&0xfffu)&&a[2]<=0x100u)memset(lcb_host(a[0]),(int)a[1],a[2]);
        eax=a[0];
        break;
    case 17: eax=b+LCB_DEF+0x10u*(pick%2); break; /* projectile_def */
    case 18: lcb_dwords(v,0,a[1],3); break; /* the sound: position by content */
    case 19: /* +30h: writes both 6-byte records. */
        for(unsigned k=1;k<3;++k)if(lcb_ok(a[k],6))for(unsigned j=0;j<3;++j)put16(lcb_host(a[k])+2*j,(uint16_t)(lcb_next()%4001u-2000u));
        break;
    case 20: { /* +6Ch: zero, one, a sign word or only the high word; the
                * owner's target and player byte may change (reread). */
        static const uint32_t flags[]={0,1,0x8000,0xffff0000u,0x10000,0xffff};
        eax=pick%5?flags[(pick>>3)%6]:eax;
        if(perturb&&pick%7==0)put32(o+0x1a4,lcb_actor_or_zero(pick>>8));
        if(perturb&&pick%9==0)o[0xa3]=(BYTE)(o[0xa3]?0:1);
        break; }
    case 21: if(perturb&&pick%5==0)o[0xa3]=(BYTE)(o[0xa3]?0:1); break; /* +54h */
    default:break;
    }
    v->eax=lcb_mask(eax);v->edx=edx;return (uint64_t)edx<<32|eax;
}
/* Every stub first keeps the dword at its entry ESP-88h (impact's packet
 * slot), then EBX/ESI/EDI/EBP/ESP, then pushes the entry EAX/EDX/ECX. */
#define LCB_STUB(n) __attribute__((naked)) static void lcb_stub_##n(void){__asm__ volatile( \
    "movl %ecx,_lcb_tmp\n\tmovl -136(%esp),%ecx\n\tmovl %ecx,_lcb_residue\n\tmovl _lcb_tmp,%ecx\n\t" \
    "movl %ebx,_lcb_entry\n\tmovl %esi,_lcb_entry+4\n\tmovl %edi,_lcb_entry+8\n\tmovl %ebp,_lcb_entry+12\n\tmovl %esp,_lcb_entry+16\n\t" \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _lcb_stub_impl\n\taddl $24,%esp\n\tmovl _lcb_ecx,%ecx\n\tret\n\t");}
LCB_STUB(0) LCB_STUB(1) LCB_STUB(2) LCB_STUB(3) LCB_STUB(4) LCB_STUB(5) LCB_STUB(6) LCB_STUB(7) LCB_STUB(8) LCB_STUB(9)
LCB_STUB(10) LCB_STUB(11) LCB_STUB(12) LCB_STUB(14) LCB_STUB(15) LCB_STUB(16) LCB_STUB(17) LCB_STUB(18) LCB_STUB(19)
LCB_STUB(20) LCB_STUB(21)
#undef LCB_STUB
/* 49630c: captures like the other stubs, then links a record at EBP +
 * [descriptor+4] into FS:0 with the saved head at record+0 and a zero
 * context word at record+16, as the original does. */
__attribute__((naked)) static void lcb_stub_13(void){__asm__ volatile(
    "movl %ebx,_lcb_entry\n\tmovl %esi,_lcb_entry+4\n\tmovl %edi,_lcb_entry+8\n\tmovl %ebp,_lcb_entry+12\n\tmovl %esp,_lcb_entry+16\n\t"
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
    "pushl $13\n\tcalll _lcb_stub_impl\n\taddl $12,%esp\n\t"
    "popl %ecx\n\tmovl 4(%ecx),%ecx\n\taddl %ebp,%ecx\n\tmovl %eax,(%ecx)\n\tmovw $0,16(%ecx)\n\tmovl %ecx,%fs:0\n\t"
    "popl %edx\n\taddl $4,%esp\n\tmovl _lcb_ecx,%ecx\n\tret\n\t");}
static void *lcb_stubs[LCB_IDS]={lcb_stub_0,lcb_stub_1,lcb_stub_2,lcb_stub_3,lcb_stub_4,lcb_stub_5,lcb_stub_6,lcb_stub_7,
    lcb_stub_8,lcb_stub_9,lcb_stub_10,lcb_stub_11,lcb_stub_12,lcb_stub_13,lcb_stub_14,lcb_stub_15,lcb_stub_16,lcb_stub_17,
    lcb_stub_18,lcb_stub_19,lcb_stub_20,lcb_stub_21};
#define LCB_BIT(i) (1u<<(i))
/* Direct callees of each hook (bit i = lcb_addresses[i]). */
static uint32_t lcb_which(unsigned index){
    if(index==ES2_HOOK_INDEX_lc_base_update)return 0x1fffu;
    if(index==ES2_HOOK_INDEX_projectile_create)return LCB_BIT(13)|LCB_BIT(14)|LCB_BIT(15)|LCB_BIT(16)|LCB_BIT(17)|LCB_BIT(18);
    return 0;
}
static int lcb_patch(BYTE saved[LCB_IDS][5],int active[LCB_IDS],uint32_t which,int on){
    for(unsigned i=0;i<LCB_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!lcb_addresses[i])continue;if(actor_patch(lcb_addresses[i]-0x400000u,lcb_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(lcb_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
/* W pointer, weapon records, projectile pool, counter pointer, the player's
 * projectile, the phase tables 4973f4..4973fb, the frame time, RNG stream A. */
static const struct { uint32_t rva,size; } lcb_ranges[]={{0xa95bc,4},{0xa98ec,4},{0xa96c2,4},{0x2cbee4,4},{0x9c394,4},
    {0x973f4,8},{0xd3b64,2},{0xd2599,0x72}};
static void lcb_globals(BYTE *out,int restore){
    for(unsigned i=0;i<sizeof(lcb_ranges)/sizeof(lcb_ranges[0]);++i){
        if(restore)memcpy(mapped+lcb_ranges[i].rva,out,lcb_ranges[i].size);else memcpy(out,mapped+lcb_ranges[i].rva,lcb_ranges[i].size);
        out+=lcb_ranges[i].size;
    }
}
static int lcb_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<LCB_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
static void lcb_reset(void){lcb_calls=0;lcb_stream=lcb_seed;}
static uint16_t lcb_word(const uint16_t *values,unsigned count){return random32()%4?values[random32()%count]:(uint16_t)random32();}
/* Pool header (free count +0, used count +2, free head +0Ah, used head
 * +0Eh) with count free nodes of 80h bytes; objects start at node+8. */
static void lcb_pool(unsigned count){
    uint32_t pool=LCB_BASE+LCB_POOL,nodes=LCB_BASE+LCB_NODES;
    put16(lcb_host(pool),(uint16_t)count);put16(lcb_host(pool)+2,0);put32(lcb_host(pool)+0xe,0);
    for(unsigned i=0;i<count;++i){uint32_t n=nodes+0x80u*i;put32(lcb_host(n),i+1<count?n+0x80u:0);put32(lcb_host(n)+4,i?n-0x80u:0);}
    put32(lcb_host(pool)+0xa,count?nodes:0);
}
/* The arena: the building B (also the owner of projectile_create) with two
 * configurations, part records, three banks; three actors; the mount and
 * weapon records; the projectile pool and nodes; the position, angles and
 * projectile definition buffers; the vtable. real: plausible geometry, wide
 * cones and long ranges, small timers, a real pool. */
static void lcb_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    const uint32_t b=LCB_BASE;BYTE *o=lcb_host(b+LCB_B);
    lcb_seed=random32();random_bytes(lcb_arena,LCB_SIZE);memset(a,0,24);lcb_real=real;
    for(unsigned i=0;i<3;++i)r[i]=random32();
    static const unsigned slots[]={0x30,0x6c,0x54};
    for(unsigned k=0;k<3;++k)put32(lcb_arena+LCB_VT+slots[k],(uint32_t)(uintptr_t)lcb_stubs[19+k]);
    put32(o,b+LCB_VT);put32(o+0x1f2,b+LCB_C);put32(o+0x205,b+LCB_P);
    o[0x99]=(BYTE)(random32()%8?0:1+random32()%255);o[0xa5]=(BYTE)(random32()%4?0:1+random32()%255);
    o[0xa3]=(BYTE)(random32()%3?0:random32()%2?1:random32());
    put32(o+0x1a4,lcb_actor_or_zero(random32()));
    if(real)for(unsigned j=0;j<3;++j)put32(o+0x26+4*j,(uint32_t)((int32_t)(random32()%40001u)-20000));
    static const uint16_t counts[]={0,1,2,3,5,7,8,0xffff,0x8000};
    for(unsigned c=0;c<2;++c){BYTE *cfg=lcb_arena+(c?LCB_C2:LCB_C);put32(cfg+0x14,b+LCB_Q+0x1eu*c);
        put16(cfg+0x12,real?(uint16_t)(7u+random32()%2):n<1024?counts[(n>>c)%9]:lcb_word(counts,9));}
    /* Parts: mostly destroyed (runtime word = maximum) or one below/above. */
    const int destroyed=random32()%3==0;
    for(unsigned i=0;i<LCB_PARTS;++i){
        BYTE *q=lcb_arena+LCB_Q+0x1eu*i,*p=lcb_arena+LCB_P+11u*i;
        uint16_t max=(uint16_t)(random32()%4?random32()%8:random32());put16(q,max);
        uint32_t c=random32()%8;put16(p,(uint16_t)(real?max-1u-random32()%3:destroyed&&c?max+c%2:c<3?max:c<6?max-1u:max+1u));
    }
    /* Banks: the target, the phase word, timers (small in the real pass). */
    static const uint16_t phases[]={0,1,1,0,2,0xffff};
    for(unsigned k=0;k<3;++k){BYTE *K=o+LCB_BANKS+0x1fu*k;
        put32(K+0x1b,real?LCB_ACTOR(random32()%LCB_ACTORS):lcb_actor_or_zero(random32()));
        for(unsigned j=0;j<3;++j){BYTE *S=K+8u*j;put16(S+6,real?(uint16_t)(random32()%2):lcb_word(phases,6));
            if(real){put16(S+1,(uint16_t)(random32()%40));put16(S+4,(uint16_t)(random32()%40));}}
        if(real)put16(K+0x19,(uint16_t)(random32()%40));
    }
    for(unsigned i=0;i<LCB_ACTORS;++i){BYTE *t=lcb_host(LCB_ACTOR(i));put32(t,b+LCB_VT);
        if(real)for(unsigned j=0;j<3;++j)put32(t+0x26+4*j,(uint32_t)((int32_t)(random32()%40001u)-20000));
        static const uint16_t links[]={0,1,2,3};put16(t+0x1a2,lcb_word(links,4));}
    /* Mount records: half-windows, range, offset, re-arm. */
    static const uint16_t windows[]={0x1000,0x2000,0x4000,0x7fff,0x8000,0,0xffff};
    for(unsigned j=0;j<3;++j){BYTE *w=lcb_arena+LCB_W+0x16u*j;
        put16(w,real?0x7fff:lcb_word(windows,7));put16(w+2,real?0x7fff:lcb_word(windows,7));
        put32(w+4,real?0x7fffffffu:random32()%4?lcb_range_values[random32()%6]:random32());
        for(unsigned k=0;k<3;++k)put32(w+8+4*k,random32()%4?random32()%2001u-1000u:random32());}
    /* Projectile definitions: the sound word at +0Ch. */
    for(unsigned k=0;k<2;++k)put16(lcb_arena+LCB_DEF+0x10*k+0xc,(uint16_t)(random32()%2?random32()%40:random32()));
    put32(mapped+0xa95bc,b+LCB_W);put32(mapped+0xa98ec,b+LCB_REC);put32(mapped+0xa96c2,b+LCB_POOL);
    put32(mapped+0x2cbee4,b+LCB_COUNTER);put32(mapped+0x9c394,random32());
    if(random32()%4==0)random_bytes(mapped+0x973f4,8);else memcpy(mapped+0x973f4,lcb_shipped,8);
    if(real){
        put16(mapped+0xd3b64,(uint16_t)(1+random32()%40));
        BYTE *state=mapped+0xd2599;random_bytes(state,112);state[112]=(BYTE)(random32()%56);state[113]=(BYTE)(random32()%56);
        lcb_pool(4);
    }
    if(index==ES2_HOOK_INDEX_lc_base_update)a[0]=b+LCB_B;
    else if(index==ES2_HOOK_INDEX_projectile_create){
        static const uint32_t types[]={3,0,2,0x10003,0xffff0003u,0xffff0000u,4};
        a[0]=random32()%4?types[random32()%7]:random32();a[1]=b+LCB_POS;a[2]=b+LCB_ANG;a[3]=b+LCB_B;
        a[4]=random32()%2?random32()%64:random32();
    } else {
        static const uint32_t records[]={0,1,8,0x7fff,0x8000,0xffff,0x12340008u};
        a[0]=random32()%2?records[random32()%7]:random32();
        put32(mapped+0xa98ec,random32()%4?b+LCB_REC:random32());
    }
}
static int lcb_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const lcb_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,lcb_calls);
    for(unsigned j=0;j<calls&&j<lcb_calls&&expected;++j)if(memcmp(expected+j,lcb_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(lcb_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<LCB_SIZE;++j)if(lcb_after[j]!=lcb_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,lcb_after[j],lcb_arena[j]);break;}
    return 1;
}
static unsigned lcb_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<lcb_calls;++j){unsigned id=lcb_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?lcb_slots[id]==c->key:!lcb_slots[id]&&lcb_addresses[id]==c->key)++hits;}
    return hits;
}
static int lcb_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[LCB_GLOBALS],after[LCB_GLOBALS],actual[LCB_GLOBALS];static lcb_record expected[LCB_MAX];uint32_t head,head_after;
    memcpy(lcb_before,lcb_arena,LCB_SIZE);lcb_globals(before,0);
    __asm__ volatile("movl %%fs:0,%0":"=r"(head));
    lcb_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=lcb_calls;memcpy(expected,lcb_log,calls*sizeof(*expected));
    memcpy(lcb_after,lcb_arena,LCB_SIZE);lcb_globals(after,0);
    memcpy(lcb_arena,lcb_before,LCB_SIZE);lcb_globals(before,1);lcb_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];
    if(count>16)return fail("lc_base counter table too long");
    for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);lcb_globals(actual,0);
    __asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
    if(head_after!=head)return fail("lc_base left FS:0 changed");
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=lcb_expected_hits(c+i))return fail("lc_base callee counter delta");
    if(pair[0]!=pair[1]||calls!=lcb_calls||memcmp(expected,lcb_log,calls*sizeof(*expected))||memcmp(lcb_after,lcb_arena,LCB_SIZE)||
       lcb_globals_differ(after,actual))
        return lcb_report("direct",n,pair,calls,expected);
    lcb_globals(before,1);
    return 0;
}
/* Calls target with six slots, EAX/EDX/ECX/EBX = pattern, ESI and EDI two
 * other values derived from it and a patterned stack below the slots;
 * returns EAX, EDX and ECX in out and copies the LCB_WINDOW bytes below the
 * slots after the call. */
__attribute__((naked,noinline)) static void lcb_invoke(void *target,const uint32_t *a,uint32_t pattern,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 16(%ebp),%eax\n\tmovl $1024,%ecx\n\trep stosl\n\taddl $4072,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 20(%esi)\n\tpushl 16(%esi)\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl %esp,_lcb_origin\n\t"
        "movl 16(%ebp),%eax\n\tmovl %eax,%edx\n\tmovl %eax,%ecx\n\tmovl %eax,%ebx\n\tmovl %eax,%esi\n\txorl $0x5a5a5a5a,%esi\n\t"
        "movl %eax,%edi\n\txorl $0x3c3c3c3c,%edi\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%ebx\n\tmovl %eax,(%ebx)\n\tmovl %edx,4(%ebx)\n\tmovl %ecx,8(%ebx)\n\t"
        "leal -512(%esp),%esi\n\tmovl $_lcb_frame_copy,%edi\n\tmovl $128,%ecx\n\tcld\n\trep movsl\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static int lcb_shimmed(unsigned index){return index==ES2_HOOK_INDEX_lc_base_update||index==ES2_HOOK_INDEX_projectile_create;}
/* The compared part of the window: the frame up to the return address
 * (the real passes, where nested replaced hooks push their framework's
 * flags below the outgoing ESP) or the whole window (stubs only). */
static unsigned lcb_window(unsigned index,int real){
    if(!real)return index==ES2_HOOK_INDEX_lc_base_update?LCB_WINDOW:0x80u;
    return index==ES2_HOOK_INDEX_lc_base_update?0xd4u:0x3cu;
}
static int lcb_frame_report(const char *what,unsigned n,const BYTE *expected,unsigned window){
    fprintf(stderr,"FAIL: %s case %u (%s)\n",current_function,n,what);
    for(unsigned j=LCB_WINDOW-window;j<LCB_WINDOW;++j)if(expected[j]!=lcb_frame_copy[j])fprintf(stderr," below the arguments -%u: %02x/%02x\n",LCB_WINDOW-j,expected[j],lcb_frame_copy[j]);
    return 1;
}
static int lcb_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;lcb_track=lcb_shimmed(index);
    const unsigned window=lcb_window(index,0);uint32_t head,head_after;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_out[3],actual_out[3],pattern=poison?0xa7a7a7a7u:0x31313131u;lcb_build(index,n,a,r,0);
        static BYTE expected_frame[LCB_WINDOW];
        BYTE before[LCB_GLOBALS],after[LCB_GLOBALS],actual[LCB_GLOBALS],expected_fpu[108],actual_fpu[108];static lcb_record expected[LCB_MAX];
        memcpy(lcb_before,lcb_arena,LCB_SIZE);lcb_globals(before,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head));
        lcb_reset();lcb_invoke(mapped+d->rva,a,pattern,expected_out);es2_hook_fpu_save(expected_fpu);memcpy(expected_frame,lcb_frame_copy,LCB_WINDOW);
        unsigned calls=lcb_calls;memcpy(expected,lcb_log,calls*sizeof(*expected));memcpy(lcb_after,lcb_arena,LCB_SIZE);lcb_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("lc_base poison install");
        memcpy(lcb_arena,lcb_before,LCB_SIZE);lcb_globals(before,1);lcb_reset();lcb_invoke(mapped+d->rva,a,pattern,actual_out);
        es2_hook_fpu_save(actual_fpu);lcb_globals(actual,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
        uint64_t pair[2]={(uint64_t)expected_out[1]<<32|expected_out[0],(uint64_t)actual_out[1]<<32|actual_out[0]};
        if(head_after!=head||pair[0]!=pair[1]||calls!=lcb_calls||memcmp(expected,lcb_log,calls*sizeof(*expected))||memcmp(lcb_after,lcb_arena,LCB_SIZE)||
           lcb_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=lcb_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        else if(lcb_track&&memcmp(expected_frame+LCB_WINDOW-window,lcb_frame_copy+LCB_WINDOW-window,window))
            status=lcb_frame_report(fast?"installed fast frame":"installed preserving frame",n,expected_frame,window);
        if(!es2_hook_uninstall(&h))return fail("lc_base poison uninstall");lcb_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));lcb_track=0;
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state/FS:0%s\n",d->name,total,
        lcb_shimmed(index)?", EBX/ESI/EDI/EBP/ESP, stack arguments and impact's packet residue at every callee entry and the bytes below the arguments":"");
    return status;
}
/* The real-callee pass: nested hooks in rotating original/replaced/compare
 * modes (compare falls back to replaced where refused), the hook under test
 * original or replaced; keep = the callees that stay logging stubs (the
 * memset thunk 49681a runs as the harness's runtime memset). */
enum { LCB_NESTED=12 };
static unsigned lcb_nested(unsigned index,unsigned nested[LCB_NESTED],uint32_t *keep){
    unsigned count=0;*keep=LCB_BIT(13)|LCB_BIT(15)|LCB_BIT(17)|LCB_BIT(18);
#define LCB_N(name) nested[count++]=ES2_HOOK_INDEX_##name
    if(index==ES2_HOOK_INDEX_lc_base_update){
        *keep|=LCB_BIT(0)|LCB_BIT(1)|LCB_BIT(3)|LCB_BIT(12);
        LCB_N(timer_word_step);LCB_N(dist_gate);LCB_N(aim_angles);LCB_N(rng_bounded);LCB_N(sincos_pair);LCB_N(rotate_2d);
        LCB_N(projectile_create);LCB_N(weapon_record);LCB_N(euler_matrix);
    }
    LCB_N(pool_alloc);
#undef LCB_N
    return count;
}
static int lcb_real_pass(unsigned index){
    unsigned nested[LCB_NESTED];uint32_t keep;unsigned count=lcb_nested(index,nested,&keep);
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[LCB_NESTED]={0};
    BYTE saved[LCB_IDS][5];int active[LCB_IDS];uint32_t head,head_after;
    if(lcb_patch(saved,active,keep,1))return 1;
    static lcb_record expected[LCB_MAX];static BYTE expected_frame[LCB_WINDOW];
    lcb_track=1;const unsigned window=lcb_window(index,1);
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_out[3],actual_out[3],pattern=n&1?0xa7a7a7a7u:0x31313131u;lcb_build(index,n,a,r,1);
        BYTE before[LCB_GLOBALS],after[LCB_GLOBALS],actual[LCB_GLOBALS];
        memcpy(lcb_before,lcb_arena,LCB_SIZE);lcb_globals(before,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head));
        es2_hook hk[LCB_NESTED+1];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!nested_install_hook(hk+j,nested[j],&o,e))return fail("lc_base nested install");}
        lcb_reset();lcb_invoke(mapped+d->rva,a,pattern,expected_out);memcpy(expected_frame,lcb_frame_copy,LCB_WINDOW);
        unsigned calls=lcb_calls;memcpy(expected,lcb_log,calls*sizeof(*expected));
        memcpy(lcb_after,lcb_arena,LCB_SIZE);lcb_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+LCB_NESTED,d,&o,function_hook_extension(index)))return fail("lc_base caller install");
        memcpy(lcb_arena,lcb_before,LCB_SIZE);lcb_globals(before,1);lcb_reset();
        lcb_invoke(mapped+d->rva,a,pattern,actual_out);lcb_globals(actual,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
        uint64_t pair[2]={(uint64_t)expected_out[1]<<32|expected_out[0],(uint64_t)actual_out[1]<<32|actual_out[0]};
        if(head_after!=head||pair[0]!=pair[1]||calls!=lcb_calls||memcmp(expected,lcb_log,calls*sizeof(*expected))||memcmp(lcb_after,lcb_arena,LCB_SIZE)||
           lcb_globals_differ(after,actual)){
            fprintf(stderr,"real mode=%u head %08x/%08x\n",mode,head,head_after);status=lcb_report("real",n,pair,calls,expected);
        }
        else if(memcmp(expected_frame+LCB_WINDOW-window,lcb_frame_copy+LCB_WINDOW-window,window)){
            fprintf(stderr,"real mode=%u\n",mode);status=lcb_frame_report("real frame",n,expected_frame,window);}
        for(unsigned j=0;j<=LCB_NESTED;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("lc_base nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("lc_base nested uninstall");}
        lcb_globals(before,1);++total;
    }
    if(lcb_patch(saved,active,0,0))return 1;
    lcb_real=0;lcb_track=0;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, 31/a7 stack, pair, memory, globals, FS:0, stub inputs, per-call EBX/ESI/EDI/EBP/ESP, stack arguments and the frame\n",
        d->name,total,count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
/* Compare cases (compare_probes.h) for weapon_record, the group's
 * compare-capable member; the state is the arena and the globals. */
static void lcb_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){lcb_build(index,n,a,r,0);}
static void lcb_compare_state(BYTE *s,int restore){
    if(restore){memcpy(lcb_arena,s,LCB_SIZE);lcb_globals(s+LCB_SIZE,1);}else{memcpy(s,lcb_arena,LCB_SIZE);lcb_globals(s+LCB_SIZE,0);}
}
static int lc_base_cases(unsigned index,unsigned limit){
    if(limit&&compare_probe_installed(index))return compare_probe_run(index,limit,lcb_compare_build,lcb_compare_state,LCB_SIZE+LCB_GLOBALS);
    uint32_t which=lcb_which(index);
    BYTE saved[LCB_IDS][5],old_globals[LCB_GLOBALS];int active[LCB_IDS];
    lcb_globals(old_globals,0);memcpy(lcb_shipped,mapped+0x973f4,8);
    if(lcb_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_LC_BASE_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];lcb_build(index,n,a,r,0);status=lcb_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=lcb_installed_probes(index,64);
    if(lcb_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status&&index!=ES2_HOOK_INDEX_weapon_record)status=lcb_real_pass(index);
    lcb_globals(old_globals,1);
    if(!limit&&!status&&compare_probe_capable(index))status=compare_probe_run(index,0,lcb_compare_build,lcb_compare_state,LCB_SIZE+LCB_GLOBALS);
    lcb_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, buffers by content, arena/globals/FS:0\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
