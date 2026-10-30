/* Actor hit tests of vtable slot +20h: base_hit_test 405038, mech_hit_test
 * 418ba8, flyer_hit_test 421c8c (docs/reverse-engineering/hit_tests.md,
 * section "hit tests"). Direct cases run the original body and the
 * core against logging stubs for every game callee (ordered calls with their
 * arguments and entry EAX/EDX/ECX, frame payloads by content: the query list,
 * the composed matrix handed to col_query and shield_geometry, the shield's
 * damage word, every point handed to transform_point, explosion_create,
 * debris_burst and the part hit, the debris word; stack addresses masked as in
 * base_cases.h); the runtime frame setup 49630c is the record stub of
 * base_cases.h (real FS:0 head at record+0, context word zeroed); the slots
 * +10h and +74h are stubs in the arena vtable. The stubs answer near the
 * thresholds the caller tests (the reach around the distance, AX zero or not,
 * the debris draw around 400h/401h, the shield's depth and the damage it
 * leaves, hit lengths -1 and 0) and poke what the originals reread after a call
 * (the pose, configuration, COL, flag and table pointers, the dead, player and
 * warning bytes, the shield words, the hit record, the damage words, the owner,
 * the default damage word and the explosion pool). Then installed 31/a7 poison
 * probes and a real-callee pass: the real slot +10h leaf 46b910, a real COL
 * (part -1 groups with spheres), a real pool and RNG, and the nested hooks in
 * rotating modes. */
typedef struct ht_record { uint32_t id,args[5],regs[3],input[8],eax,edx,slots; } ht_record;
enum { HT_SIZE=0x1000, HT_ACTOR=0x080, HT_VTABLE=0x340, HT_PACKET=0x3c0, HT_SEGMENT=0x3e0, HT_POSE=0x420, HT_CONFIG=0x440,
    HT_CONFIG2=0x480, HT_PARTS=0x4c0, HT_HIT=0x530, HT_RECORD=0x540, HT_RECORD2=0x550,
    HT_COUNTER=0x560, HT_EXPLOSION=0x580, HT_POOL=0x600, HT_INSTANCE=0x620, HT_NODE=0x640, HT_FLAGS=0x680, HT_COL=0x700,
    HT_GROUPS=0x710, HT_SHAPES=0x740, HT_SPHERES=0x7c0, HT_NODES=0x880, HT_MAX=48, HT_IDS=17, HT_GLOBALS=170, HT_LOOKUP=18,
    HT_TABLES=0x40 };
#define HT_BASE ((uint32_t)(uintptr_t)ht_arena)
/* The arena straddles a 64-KiB boundary at its vtable (the actor below it,
 * the packet and the rest above), and the two effect tables lie in a third
 * 64-KiB region, so the high words of the lo(R, w) registers (B+92h, P,
 * [P+0Ah]) tell their sources apart. */
static BYTE ht_space[0x31000],*ht_arena,*ht_tables,ht_before[HT_SIZE],ht_after[HT_SIZE],ht_tables_before[HT_TABLES],
    ht_tables_after[HT_TABLES];
static void ht_place(void){
    uintptr_t base=((uintptr_t)ht_space+0xffffu)&~(uintptr_t)0xffffu;
    ht_arena=(BYTE *)(base+0x10000u-HT_VTABLE);ht_tables=(BYTE *)(base+0x20000u);
}
static void ht_save(void){memcpy(ht_before,ht_arena,HT_SIZE);memcpy(ht_tables_before,ht_tables,HT_TABLES);}
static void ht_restore(void){memcpy(ht_arena,ht_before,HT_SIZE);memcpy(ht_tables,ht_tables_before,HT_TABLES);}
static void ht_keep(void){memcpy(ht_after,ht_arena,HT_SIZE);memcpy(ht_tables_after,ht_tables,HT_TABLES);}
static int ht_memory_differs(void){return memcmp(ht_after,ht_arena,HT_SIZE)||memcmp(ht_tables_after,ht_tables,HT_TABLES);}
static ht_record ht_log[HT_MAX];
static unsigned ht_calls,ht_index;
static int ht_real;
static uint32_t ht_seed,ht_stream,ht_r10;
/* ht_frame: the EBP the record stub saw (the original's frame, or the
 * replacement's synthetic EBP), so frame addresses compare by offset. */
__attribute__((used)) static uint32_t ht_ecx,ht_frame;
/* 49630c (record), 427da8, slot +10h, 49288c, 47ebb8, 47fa20, 40c9d4,
 * 48043c, slot +74h, 471b30, 492ee0, 407f1c, 4089bc, 408530, 413cc4,
 * 420a68, 4188c8 (the slots are in the arena vtable, never patched). */
static const uint32_t ht_addresses[HT_IDS]={0x49630c,0x427da8,0,0x49288c,0x47ebb8,0x47fa20,0x40c9d4,0x48043c,0,0x471b30,
    0x492ee0,0x407f1c,0x4089bc,0x408530,0x413cc4,0x420a68,0x4188c8};
static const unsigned ht_slots[HT_IDS]={0,0,0x10,0,0,0,0,0,0x74,0,0,0,0,0,0,0,0};
static const unsigned ht_counts[HT_IDS]={0,4,1,2,2,3,5,3,4,1,1,5,1,4,5,1,4};
static uint32_t ht_next(void){
    uint32_t z=ht_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *ht_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int ht_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
/* Recorded argument and register words of two runs (frames fe, fa) are
 * equal when they are the same value, or both frame addresses
 * (f-100h..f+0Fh) at the same offset from their frame, or both other
 * stack addresses. A frame address never matches anything else, so a
 * register that names the wrong frame local is caught; a random value
 * that happens to lie near a frame is the same value in both runs. */
static int ht_near(uint32_t value,uint32_t frame){return frame&&value-(frame-0x100u)<0x110u;}
static int ht_word_equal(uint32_t e,uint32_t a,uint32_t fe,uint32_t fa){
    if(e==a)return 1;
    int ne=ht_near(e,fe),na=ht_near(a,fa);
    if(ne||na)return ne&&na&&e-fe==a-fa;
    return ht_stack(e)&&ht_stack(a);
}
static int ht_record_equal(const ht_record *e,const ht_record *a,uint32_t fe,uint32_t fa){
    if(e->id!=a->id||memcmp(e->input,a->input,sizeof(e->input))||e->eax!=a->eax||e->edx!=a->edx)return 0;
    for(unsigned j=0;j<5;++j)if(!ht_word_equal(e->args[j],a->args[j],fe,fa))return 0;
    for(unsigned j=0;j<3;++j)if(!ht_word_equal(e->regs[j],a->regs[j],fe,fa))return 0;
    return 1;
}
static int ht_trace_differs(const ht_record *expected,unsigned calls,uint32_t fe){
    if(calls!=ht_calls)return 1;
    for(unsigned j=0;j<calls;++j)if(!ht_record_equal(expected+j,ht_log+j,fe,ht_frame))return 1;
    return 0;
}
/* A value whose high word is that of a stack address (col_query's EDX
 * keeps a region word in its low half). */
static int ht_stackish(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;
    return top+0x10000u-(value&0xffff0000u)-1u<0x1010000u;
}
static uint32_t ht_s16(uint32_t x){return (uint32_t)(int32_t)(int16_t)x;}
static void ht_content(ht_record *v,uint32_t address,unsigned dwords){for(unsigned j=0;j<dwords;++j)v->input[j]=get32(ht_host(address)+4*j);}
static uint32_t ht_table(uint32_t pick){return (uint32_t)(uintptr_t)ht_tables+(pick&1?0x20u:0u);}
static uint32_t ht_config(uint32_t pick){return HT_BASE+(pick&1?HT_CONFIG2:HT_CONFIG);}
/* Pointers the hook under test reads only after euler_matrix or
 * matrix_compose: BASE the configuration (reread for col_query) and the
 * flag array; MECH the flag array, the COL and the length [S+4] (the
 * shield's argument); FLYER the flag array and the definition. */
static void ht_poke_pointers(uint32_t pick){
    BYTE *o=ht_arena+HT_ACTOR;
    if(ht_index==ES2_HOOK_INDEX_base_hit_test){
        if(pick%3==0)put32(o+0x1f2,ht_config(pick>>4));
        if(pick%5==0)put32(o+0x201,ht_next());
    } else if(ht_index==ES2_HOOK_INDEX_mech_hit_test){
        if(pick%3==0)put32(o+0x20e,ht_next());
        if(pick%5==0)put32(o+0x1f6,ht_next());
        if(pick%7==0)put32(ht_arena+HT_SEGMENT+4,ht_next());
    } else {
        if(pick%3==0)put32(o+0x208,ht_next());
        if(pick%5==0)put32(o+0x1fa,ht_config(pick>>4));
    }
}
__attribute__((used,noinline)) static uint64_t __cdecl ht_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(ht_calls==HT_MAX){fail("hit_test synthetic call overflow");ExitProcess(1);}
    ht_record *v=ht_log+ht_calls++;memset(v,0,sizeof(*v));v->id=id;v->slots=weapons_bridge_slots(a,ht_counts[id]);memcpy(v->args,a,4*ht_counts[id]);memcpy(v->regs,regs,12);
    uint32_t eax=ht_next(),edx=ht_next(),pick=ht_next(),b=HT_BASE;ht_ecx=ht_next();
    BYTE *o=ht_arena+HT_ACTOR,*p=ht_arena+HT_PACKET,*s=ht_arena+HT_SEGMENT;
    switch(id){
    case 0: /* 49630c record stub returns the real FS:0 head for ht_stub_0. */
        __asm__ volatile("movl %%fs:0,%0":"=r"(eax));break;
    case 1: { /* object_query: the list element by content; AX zero or not;
               * the length (-1, 0, small, any) at a[0]; pokes of the
               * default damage word, the pose, the dead byte, the owner. */
        static const uint32_t lengths[]={0xffffffffu,0,1,2,0x7fffffffu,0x80000000u};
        v->input[0]=get32(ht_host(a[2]));
        if(pick%2)eax&=0xffff0000u;else eax|=1u<<(pick>>1&15);
        put32(ht_host(a[0]),(pick>>8)%3?ht_next()%20000u:lengths[(pick>>10)%6]);
        if(ht_real)break;
        if(pick%5==0)put16(mapped+0xa95f2,(uint16_t)ht_next());
        if(pick%7==0)put32(s,ht_next());
        if(pick%9==0)o[0x99]=(BYTE)(o[0x99]?0:1+pick%255);
        if(pick%11==0)put32(p+0xe,ht_next());
        break; }
    case 2: /* slot +10h: the radius word, small or any; pokes of [S+4] and
             * S+8, which BASE and FLYER read after it. */
        eax=(eax&0xffff0000u)|(uint16_t)((pick>>4)%4?ht_next()%3000u:ht_next());
        ht_r10=ht_s16(eax);
        if(ht_real)break;
        if(pick%5==0)put32(s+4,(pick>>8)%2?ht_next()%40000u:ht_next());
        if(pick%7==0)put16(s+8,(uint16_t)ht_next());
        break;
    case 3: { /* dist_gate: around the reach the caller compares with (from
               * memory as the caller reads it), far or any. MECH reads the
               * reach after the call: pokes of its parts first. */
        uint32_t reach;
        if(ht_index==ES2_HOOK_INDEX_mech_hit_test){
            if(!ht_real){
                if(pick%7==0)put16(s+8,(uint16_t)ht_next());
                if(pick%11==0)put16(ht_host(get32(o+0x1f2))+0x1a,(uint16_t)ht_next());
                if(pick%13==0)put32(s+4,ht_next());
                if(pick%17==0)put32(o+0x1f2,ht_config(pick>>8));
            }
            reach=ht_s16(get16(s+8))+ht_s16(get16(ht_host(get32(o+0x1f2))+0x1a))+get32(s+4);
        } else reach=ht_r10+ht_s16(get16(s+8))+get32(s+4);
        switch((pick>>4)%6){case 0:eax=reach-1u;break;case 1:eax=reach;break;case 2:eax=reach+1u;break;
            case 3:eax=reach-1000u;break;case 4:eax=reach+0x80000000u;break;default:break;}
        break; }
    case 4: /* euler_matrix: the rotation out; pokes of the pointers read
             * after it. */
        if(pick%3)for(unsigned j=0;j<20;++j)ht_host(a[1])[j]=(BYTE)ht_next();
        if(!ht_real)ht_poke_pointers(pick>>8);
        break;
    case 5: /* matrix_compose: the composed matrix out (all 32 bytes); the
             * same pokes. */
        for(unsigned j=0;j<32;++j)ht_host(a[2])[j]=(BYTE)ht_next();
        if(!ht_real)ht_poke_pointers(pick>>8);
        break;
    case 6: { /* col_query: the matrix by content; the hit record or 0, its
               * length -1, 0, small or any and its region word; pokes of
               * the damage word, the pose, the dead byte, the owner, the
               * effect table. */
        static const uint32_t lengths[]={0xffffffffu,0,1,0x7fffffffu,0x80000000u};
        ht_content(v,a[3],8);
        eax=pick%3?b+HT_HIT:0;
        put32(ht_arena+HT_HIT,(pick>>2)%3?ht_next()%20000u:lengths[(pick>>4)%5]);
        put16(ht_arena+HT_HIT+4,(uint16_t)((pick>>8)%2?ht_next()%40u:ht_next()));
        if(ht_real)break;
        if(pick%5==0)put16(p+4,(uint16_t)ht_next());
        if(pick%7==0)put32(s,ht_next());
        if(pick%9==0)o[0x99]=(BYTE)(o[0x99]?0:1);
        if(pick%11==0)put32(p+0xe,ht_next());
        if(pick%13==0)put32(p+0xa,ht_table(pick>>12));
        break; }
    case 7: /* transform_point: the point by content, a planned point out;
             * pokes of the dead byte, the owner, the effect table, the
             * explosion pool and the hit record's region word (MECH reads
             * it after the call). */
        ht_content(v,a[0],3);
        for(unsigned j=0;j<3;++j)put32(ht_host(a[2])+4*j,ht_next());
        if(ht_real)break;
        if(pick%5==0)o[0x99]=(BYTE)(o[0x99]?0:1+pick%255);
        if(pick%7==0)put32(p+0xe,ht_next());
        if(pick%11==0)put32(p+0xa,ht_table(pick>>12));
        if(pick%13==0)put32(mapped+0xa961e,ht_next());
        if(pick%17==0)put16(ht_arena+HT_HIT+4,(uint16_t)ht_next());
        break;
    case 8: /* slot +74h: pokes of the dead byte (FLYER reads it after the
             * call) and the explosion pool. */
        if(ht_real)break;
        if(pick%3==0)o[0x99]=(BYTE)(o[0x99]?0:1+pick%255);
        if(pick%7==0)put32(mapped+0xa961e,ht_next());
        break;
    case 9: /* pool_alloc: the explosion object or 0. */
        eax=pick%3?b+HT_EXPLOSION:0;
        break;
    case 10: { /* rng_next: s16 & FFFh around the debris threshold 400h/401h
                * or any; pokes of the effect table, the owner and the hit
                * record (FLYER reads them after the call). */
        static const uint16_t draws[]={0x3ff,0x400,0x401,0x402,0,0xfff};
        if((pick>>4)%3)eax=(eax&0xfffff000u)|draws[(pick>>6)%6];
        if(ht_real)break;
        if(pick%5==0)put32(p+0xa,ht_table(pick>>12));
        if(pick%7==0)put32(ht_arena+HT_HIT,ht_next());
        if(pick%11==0)put16(ht_arena+HT_HIT+4,(uint16_t)ht_next());
        if(pick%13==0)put32(p+0xe,ht_next());
        break; }
    case 11: case 13: /* explosion_create, debris_burst: the point by
                       * content. */
        ht_content(v,a[2],3);
        break;
    case 12: /* debris_table: the word by content, lowered in a third of the
              * calls; one of two records. */
        v->input[0]=get16(ht_host(a[0]));
        if(pick%3==0)put16(ht_host(a[0]),(uint16_t)(v->input[0]-(pick>>8&3u)));
        eax=b+(pick>>4&1?HT_RECORD2:HT_RECORD);
        break;
    case 14: { /* shield_geometry: the matrix and the damage word by
                * content; the word it leaves (0, kept, halved, any) and the
                * depth (0, small, any); pokes of the player, warning and
                * shield words, flags, damage, absorbed sum, pose, table. */
        ht_content(v,a[2],7);v->input[7]=get16(ht_host(a[4]));
        uint16_t left;switch((pick>>2)%4){case 0:left=0;break;case 1:left=(uint16_t)v->input[7];break;
            case 2:left=(uint16_t)(v->input[7]/2u);break;default:left=(uint16_t)ht_next();}
        put16(ht_host(a[4]),left);
        eax=(pick>>5)%4==0?0:(pick>>7)%2?ht_next()%5000u:ht_next();
        if(ht_real)break;
        if(pick%5==0)o[0xa3]=(BYTE)(o[0xa3]?0:1);
        if(pick%7==0)o[0xb0]=(BYTE)(o[0xb0]?0:1);
        if(pick%11==0){put16(o+0x222,(uint16_t)(ht_next()%600u));put16(o+0x224,(uint16_t)(ht_next()%600u));}
        if(pick%13==0)put32(o+0x20e,ht_next());
        if(pick%17==0)put16(p+4,(uint16_t)ht_next());
        if(pick%19==0)put32(o+0x288,ht_next());
        if(pick%23==0)put32(s,ht_next());
        if(pick%29==0)put32(p+0xa,ht_table(pick>>12));
        break; }
    case 16: /* 4188c8: the point by content. */
        ht_content(v,a[3],3);
        break;
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
/* Pushes the entry EAX/EDX/ECX for the record before the slot pointer. */
#define HT_STUB(n) __attribute__((naked)) static void ht_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _ht_stub_impl\n\taddl $24,%esp\n\tmovl _ht_ecx,%ecx\n\tret\n\t");}
HT_STUB(1) HT_STUB(2) HT_STUB(3) HT_STUB(4) HT_STUB(5) HT_STUB(6) HT_STUB(7) HT_STUB(8) HT_STUB(9) HT_STUB(10) HT_STUB(11)
HT_STUB(12) HT_STUB(13) HT_STUB(14) HT_STUB(15) HT_STUB(16)
#undef HT_STUB
/* 49630c: captures the entry registers like the other stubs, then links a
 * record at EBP + [descriptor+4] into FS:0 with the saved head at record+0
 * and a zero context word at record+16, as the original does. EBP (the
 * hook's frame) is kept in ht_frame first. */
__attribute__((naked)) static void ht_stub_0(void){__asm__ volatile(
    "movl %ebp,_ht_frame\n\t"
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
    "pushl $0\n\tcalll _ht_stub_impl\n\taddl $12,%esp\n\t"
    "popl %ecx\n\tmovl 4(%ecx),%ecx\n\taddl %ebp,%ecx\n\tmovl %eax,(%ecx)\n\tmovw $0,16(%ecx)\n\tmovl %ecx,%fs:0\n\t"
    "popl %edx\n\taddl $4,%esp\n\tmovl _ht_ecx,%ecx\n\tret\n\t");}
static void *ht_stubs[HT_IDS]={ht_stub_0,ht_stub_1,ht_stub_2,ht_stub_3,ht_stub_4,ht_stub_5,ht_stub_6,ht_stub_7,ht_stub_8,ht_stub_9,
    ht_stub_10,ht_stub_11,ht_stub_12,ht_stub_13,ht_stub_14,ht_stub_15,ht_stub_16};
/* Direct callees of each hook (bit i = ht_addresses[i]; the slots are in
 * the arena vtable and never patched). */
static unsigned ht_which(unsigned index){
    const unsigned common=1u|1u<<3|1u<<4|1u<<5|1u<<6|1u<<7|1u<<9|1u<<10|1u<<11;
    if(index==ES2_HOOK_INDEX_base_hit_test)return common|1u<<1|1u<<12|1u<<13;
    if(index==ES2_HOOK_INDEX_mech_hit_test)return common|1u<<14|1u<<15|1u<<16;
    return common|1u<<12|1u<<13;
}
/* Counter pointer, explosion pool, default damage word, the view-subject
 * flag and subject, the COL statics 4a980c..4a9823, the second debris list
 * and the default list record 4a964c..4a9659, RNG stream A. */
static const struct { uint32_t rva,size; } ht_ranges[]={{0x2cbee4,4},{0xa961e,4},{0xa95f2,2},{0x9ef34,4},{0xd2684,4},
    {0xa980c,24},{0xa964c,14},{0xd2599,0x72}};
static void ht_globals(BYTE *out,int restore){
    for(unsigned i=0;i<sizeof(ht_ranges)/sizeof(ht_ranges[0]);++i){
        if(restore)memcpy(mapped+ht_ranges[i].rva,out,ht_ranges[i].size);else memcpy(out,mapped+ht_ranges[i].rva,ht_ranges[i].size);
        out+=ht_ranges[i].size;
    }
}
/* The COL matrix pointer 4a980c (offset 18) compares as "a stack address":
 * the original col_group stores its frame address, the replaced one its
 * scratch. */
static int ht_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<HT_GLOBALS;++j){
        if(j>=HT_LOOKUP&&j<HT_LOOKUP+4u&&ht_stack(get32(want+HT_LOOKUP))&&ht_stack(get32(got+HT_LOOKUP)))continue;
        if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    }
    return 0;
}
static void ht_reset(void){ht_calls=0;ht_stream=ht_seed;ht_frame=0;}
/* Pool header (free count +0, used count +2, free head +0Ah, used head
 * +0Eh) with count free nodes of 80h bytes; objects start at node+8. */
static void ht_pool(uint32_t pool,uint32_t nodes,unsigned count){
    put16(ht_host(pool),(uint16_t)count);put16(ht_host(pool)+2,0);put32(ht_host(pool)+0xe,0);
    for(unsigned i=0;i<count;++i){uint32_t n=nodes+0x80u*i;put32(ht_host(n),i+1<count?n+0x80u:0);put32(ht_host(n)+4,i?n-0x80u:0);}
    put32(ht_host(pool)+0xa,count?nodes:0);
}
/* A realistic pose: small rotation words, mode byte 0..2, translation. */
static void ht_matrix(BYTE *mx){
    for(unsigned j=0;j<9;++j)put16(mx+2*j,(uint16_t)(random32()%32769u-16384u));
    mx[18]=(BYTE)(random32()%3);
    for(unsigned j=0;j<3;++j)put32(mx+20+4*j,random32()%4096u-2048u);
}
/* Real COL: one to three groups with part word -1 (the matrix is copied, no
 * node lookup), one or two shapes each, three spheres per shape, regions
 * 0..3, flag words 0 a third of the time. */
static void ht_real_col(void){
    const uint32_t b=HT_BASE;unsigned groups=1+random32()%3,k=0;
    put16(ht_arena+HT_COL,(uint16_t)groups);put32(ht_arena+HT_COL+2,b+HT_GROUPS);
    for(unsigned g=0;g<groups;++g){
        BYTE *gp=ht_arena+HT_GROUPS+10u*g;unsigned m=1+random32()%2;
        put16(gp,0xffff);put16(gp+4,(uint16_t)m);put32(gp+6,b+HT_SHAPES+0x10u*k);
        for(unsigned i=0;i<m;++i,++k){
            BYTE *sh=ht_arena+HT_SHAPES+0x10u*k;uint32_t spheres=b+HT_SPHERES+0x18u*k;
            put16(sh,(uint16_t)(1+random32()%3));put32(sh+2,spheres);put16(sh+6,(uint16_t)(random32()%4));
            for(unsigned j=0;j<3;++j)put16(sh+8+2*j,(uint16_t)(random32()%2048u-1024u));put16(sh+14,(uint16_t)(1000+random32()%3000));
            for(unsigned q=0;q<3;++q){BYTE *sp=ht_host(spheres+8u*q);
                for(unsigned j=0;j<3;++j)put16(sp+2*j,(uint16_t)(random32()%1024u-512u));put16(sp+6,(uint16_t)(200+random32()%1500));}
        }
    }
    for(int j=-16;j<48;++j)put16(ht_arena+HT_FLAGS+0x20+2*j,(uint16_t)(random32()%3==0?0:1));
}
static uint16_t ht_word(const uint16_t *values,unsigned count){return random32()%4?values[random32()%count]:(uint16_t)random32();}
static void ht_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    const uint32_t b=HT_BASE;BYTE *o=ht_arena+HT_ACTOR,*p=ht_arena+HT_PACKET,*s=ht_arena+HT_SEGMENT;
    ht_seed=random32();random_bytes(ht_arena,HT_SIZE);memset(a,0,24);ht_index=index;
    for(unsigned i=0;i<3;++i)r[i]=random32();
    put32(o,b+HT_VTABLE);
    put32(ht_arena+HT_VTABLE+0x10,real?(uint32_t)(uintptr_t)(mapped+0x6b910):(uint32_t)(uintptr_t)ht_stubs[2]);
    put32(ht_arena+HT_VTABLE+0x74,(uint32_t)(uintptr_t)ht_stubs[8]);
    /* Packet: the segment, damage words (0 a quarter of the time), the
     * effect table, the owner, the weapon kind 0..3 or any. */
    put32(p,b+HT_SEGMENT);
    put16(p+4,(uint16_t)(random32()%4?random32()%400:0));put16(p+6,(uint16_t)(random32()%4?random32()%400:0));
    random_bytes(ht_tables,HT_TABLES);put32(p+0xa,ht_table(random32()));put32(p+0xe,random32());
    put16(p+0x12,(uint16_t)(random32()%4?random32()%4:random32()));
    /* Segment: the forward pose, length, radius word, inverse pose. */
    put32(s,b+HT_POSE);
    put32(s+4,real?100+random32()%20000:random32()%3?random32()%40000:random32());
    put16(s+8,(uint16_t)(real||random32()%3?random32()%2000:random32()));
    if(real){
        ht_matrix(s+0xa);ht_matrix(ht_arena+HT_POSE);ht_matrix(o+0x12);
        for(unsigned j=0;j<3;++j)put16(o+0xc+2*j,(uint16_t)random32());
        ht_real_col();
        /* Slot +10h 46b910: no shape instance, no node or a node radius. */
        uint32_t k=random32()%3;put32(o+0x34,k?b+HT_INSTANCE:0);put32(ht_arena+HT_INSTANCE+4,k==1?0:b+HT_NODE);
        put16(ht_arena+HT_NODE+8,(uint16_t)(random32()%3000));
    }
    o[0x99]=(BYTE)(random32()%3?0:1+random32()%255);
    put16(o+0x32,(uint16_t)(random32()%2?0:random32()%3?1:random32()));
    put32(o+0x1f2,b+HT_CONFIG);
    static const uint16_t parts[]={0,1,2,3,0xffff,0x8000};
    if(index==ES2_HOOK_INDEX_base_hit_test){
        put32(o+0x205,b+HT_PARTS);put16(ht_arena+HT_PARTS+5,ht_word(parts,6));
        for(unsigned c=0;c<2;++c){BYTE *cf=ht_arena+(c?HT_CONFIG2:HT_CONFIG);
            put16(cf+4,(uint16_t)(random32()%2?0xffff:random32()%8));put32(cf+0x38,random32()%3?b+HT_COL:0);}
        put32(o+0x201,b+HT_FLAGS+0x20);
    } else if(index==ES2_HOOK_INDEX_mech_hit_test){
        for(unsigned c=0;c<2;++c)put16(ht_arena+(c?HT_CONFIG2:HT_CONFIG)+0x1a,(uint16_t)(real||random32()%3?random32()%3000:random32()));
        o[0xa3]=(BYTE)(random32()%2);o[0xb0]=(BYTE)(random32()%2?0:1+random32()%255);
        put16(o+0x26b,(uint16_t)(random32()%2?0:random32()));
        /* Shield words: their sum at the warning bound 500 (-1, 0, +1) half
         * of the time. */
        uint16_t front=(uint16_t)(random32()%600);put16(o+0x222,front);
        put16(o+0x224,(uint16_t)(random32()%2?499u+random32()%3u-front:random32()%600));
        put32(o+0x1f6,b+HT_COL);put32(o+0x20e,b+HT_FLAGS+0x20);
    } else {
        put32(o+0x1fa,b+HT_CONFIG);put32(ht_arena+HT_CONFIG+0x32,b+HT_COL);put32(ht_arena+HT_CONFIG2+0x32,b+HT_COL);
        put32(o+0x208,b+HT_FLAGS+0x20);
    }
    /* Globals: counter, explosion pool, default damage word, the view
     * subject (FLYER: set half of the time, the actor half of that). */
    put32(mapped+0x2cbee4,b+HT_COUNTER);put32(mapped+0xa961e,b+HT_POOL);
    put16(mapped+0xa95f2,(uint16_t)random32());
    put32(mapped+0x9ef34,random32()%2?0:random32()|1u);put32(mapped+0xd2684,random32()%2?b+HT_ACTOR:random32());
    random_bytes(mapped+0xa980c,24);random_bytes(mapped+0xa964c,14);put32(mapped+0xa964c,b+HT_RECORD2);
    if(real){
        put16(mapped+0xa9654,(uint16_t)(random32()%2?random32()%5:random32()));
        BYTE *state=mapped+0xd2599;random_bytes(state,112);state[112]=(BYTE)(random32()%56);state[113]=(BYTE)(random32()%56);
        ht_pool(b+HT_POOL,b+HT_NODES,random32()%4);
        put32(mapped+0xa9818,0); /* the best-hit dword counts real COL hits */
    }
    (void)n;a[0]=b+HT_ACTOR;a[1]=b+HT_PACKET;
}
static int ht_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const ht_record *expected,uint32_t fe){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u frames %08x/%08x\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,ht_calls,fe,ht_frame);
    for(unsigned j=0;j<calls&&j<ht_calls&&expected;++j)if(!ht_record_equal(expected+j,ht_log+j,fe,ht_frame)){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(ht_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<HT_SIZE;++j)if(ht_after[j]!=ht_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,ht_after[j],ht_arena[j]);break;}
    return 1;
}
static unsigned ht_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<ht_calls;++j){unsigned id=ht_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?ht_slots[id]==c->key:!ht_slots[id]&&ht_addresses[id]==c->key)++hits;}
    return hits;
}
static int ht_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[HT_GLOBALS],after[HT_GLOBALS],actual[HT_GLOBALS];static ht_record expected[HT_MAX];uint32_t head,head_after;
    ht_save();ht_globals(before,0);
    __asm__ volatile("movl %%fs:0,%0":"=r"(head));
    ht_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=ht_calls;memcpy(expected,ht_log,calls*sizeof(*expected));
    const uint32_t frame=ht_frame;
    ht_keep();ht_globals(after,0);
    ht_restore();ht_globals(before,1);ht_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];for(unsigned i=0;i<count&&i<16;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);ht_globals(actual,0);
    __asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
    if(head_after!=head)return fail("hit_test left FS:0 changed");
    if(n<4096)for(unsigned i=0;i<count&&i<16;++i)if(c[i].count-counts[i]!=ht_expected_hits(c+i))return fail("hit_test callee counter delta");
    if(pair[0]!=pair[1]||ht_trace_differs(expected,calls,frame)||ht_memory_differs()||ht_globals_differ(after,actual))
        return ht_report("direct",n,pair,calls,expected,frame);
    ht_globals(before,1);
    return 0;
}
static int ht_patch(BYTE saved[HT_IDS][5],int active[HT_IDS],unsigned which,int on){
    for(unsigned i=0;i<HT_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!ht_addresses[i])continue;if(actor_patch(ht_addresses[i]-0x400000u,ht_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(ht_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static int ht_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;uint32_t head,head_after;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;ht_build(index,n,a,r,0);
        BYTE before[HT_GLOBALS],after[HT_GLOBALS],actual[HT_GLOBALS],expected_fpu[108],actual_fpu[108];static ht_record expected[HT_MAX];
        ht_save();ht_globals(before,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head));
        ht_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=ht_calls;memcpy(expected,ht_log,calls*sizeof(*expected));ht_keep();ht_globals(after,0);
        const uint32_t frame=ht_frame;
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("hit_test poison install");
        ht_restore();ht_globals(before,1);ht_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);ht_globals(actual,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(head_after!=head||pair[0]!=pair[1]||ht_trace_differs(expected,calls,frame)||ht_memory_differs()||
           ht_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=ht_report(fast?"installed fast":"installed preserving",n,pair,calls,expected,frame);
        if(!es2_hook_uninstall(&h))return fail("hit_test poison uninstall");ht_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state/FS:0\n",d->name,total);
    return status;
}
/* EAX exact; EDX exact or both a stack value with the same low word
 * (col_query's EDX on BASE's COL miss can carry a residue of the sphere
 * tests). */
static int ht_real_equal(const uint32_t e[2],const uint32_t f[2]){
    if(e[0]!=f[0])return 0;
    if(e[1]==f[1])return 1;
    return ht_stackish(e[1])&&ht_stackish(f[1])&&(uint16_t)e[1]==(uint16_t)f[1];
}
/* Real callees with the nested hooks in rotating original/replaced/compare
 * modes (compare falls back to replaced where refused): dist_gate, the
 * lazy matrix, matrix_compose, the COL traversal with its sphere test,
 * transform_point, pool_alloc on a real pool of 0..3 nodes, rng_next on
 * stream A and debris_table; slot +10h is the real leaf 46b910. The runtime
 * frame setup, object_query, explosion_create, debris_burst,
 * shield_geometry, the message post, the part hit and slot +74h stay
 * stubs. Pair, arena, globals and FS:0 are compared against a run with
 * only the nested hooks. */
static unsigned ht_nested(unsigned index,unsigned nested[16]){
    static const unsigned list[]={ES2_HOOK_INDEX_dist_gate,ES2_HOOK_INDEX_euler_matrix,ES2_HOOK_INDEX_matrix_compose,
        ES2_HOOK_INDEX_col_query,ES2_HOOK_INDEX_col_group,ES2_HOOK_INDEX_col_shape,ES2_HOOK_INDEX_col_bound,
        ES2_HOOK_INDEX_sphere_hits,ES2_HOOK_INDEX_transform_point,ES2_HOOK_INDEX_pool_alloc,ES2_HOOK_INDEX_rng_next,
        ES2_HOOK_INDEX_debris_table};
    unsigned count=index==ES2_HOOK_INDEX_mech_hit_test?11:12;
    for(unsigned j=0;j<count;++j)nested[j]=list[j];
    return count;
}
static int ht_real_pass(unsigned index){
    unsigned nested[16],count=ht_nested(index,nested);
    const unsigned keep=1u|1u<<1|1u<<11|1u<<13|1u<<14|1u<<15|1u<<16;
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[16]={0},hits=0;
    BYTE saved[HT_IDS][5];int active[HT_IDS];
    if(ht_patch(saved,active,keep,1))return 1;
    ht_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u,head,head_after;ht_build(index,n,a,r,1);
        BYTE before[HT_GLOBALS],after[HT_GLOBALS],actual[HT_GLOBALS];
        ht_save();ht_globals(before,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head));
        es2_hook hk[17];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("hit_test nested install");}
        ht_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        ht_keep();ht_globals(after,0);
        hits+=get32(mapped+0xa9818)!=0;
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+16,d,&o,function_hook_extension(index)))return fail("hit_test caller install");
        ht_restore();ht_globals(before,1);ht_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);ht_globals(actual,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
        if(head_after!=head||!ht_real_equal(expected_pair,actual_pair)||ht_memory_differs()||ht_globals_differ(after,actual)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            fprintf(stderr,"real mode=%u head %08x/%08x\n",mode,head,head_after);ht_calls=0;status=ht_report("real",n,pair,0,NULL,0);
        }
        for(unsigned j=0;j<17;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("hit_test nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("hit_test nested uninstall");}
        ht_globals(before,1);++total;
    }
    ht_real=0;
    if(ht_patch(saved,active,0,0))return 1;
    if(!status&&!hits){fprintf(stderr,"FAIL: %s real pass without a COL hit\n",d->name);status=1;}
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases (%llu COL hits), real 46b910, COL, pool and RNG, %u nested hooks in rotating modes, 31/a7 stack, pair, memory and FS:0\n",
            d->name,total,(unsigned long long)hits,count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
static int hit_test_cases(unsigned index,unsigned limit){
    unsigned which=ht_which(index);
    BYTE saved[HT_IDS][5],old_globals[HT_GLOBALS];int active[HT_IDS];
    ht_place();ht_globals(old_globals,0);
    if(ht_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_HIT_TEST_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];ht_build(index,n,a,r,0);status=ht_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=ht_installed_probes(index,64);
    if(ht_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=ht_real_pass(index);
    ht_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, frame payloads by content, arena/globals/FS:0\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
