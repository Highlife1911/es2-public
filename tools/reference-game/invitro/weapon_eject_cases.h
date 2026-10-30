/* Weapon eject 40f57c and the weapon damage slot +68h (40ee0c, 40ee38, 40ee90,
 * 40ef6c; docs/reverse-engineering/weapons.md, "weapon eject"). Direct cases
 * run the original body and the core against logging stubs for every direct
 * game callee of the hook under test: for the eject 49630c (record stub as in
 * base_cases.h), euler_matrix, the node lookup slot +20h and the actor slot
 * +24h (arena vtables), matrix_compose, mount_offset, 4801d4, transform_point,
 * aim_angles, pool_alloc, debris_init, debris_heading and matrix_angles; for
 * the slot bodies 40f57c, 40ee0c, multiply_q10 and rng_next. Ordered calls with
 * their arguments and entry EAX/EDX/ECX (stack addresses masked); the frame
 * buffers by content (the composed matrix, the mount point, the aim point, the
 * angles, the record's context word). The stubs write the output buffers and
 * change what the originals reread (the definition, record and type pointers,
 * the pose word A+32h, the vtable of A, the shape pointer that must not be
 * reread, the globals 4a9642, 4a98f4, 4a9868, 6cbee4, the gate word W+7Bh, the
 * type words, W+7Fh). Compared: the pair, the arena, FS:0, the globals and RNG
 * stream A, the callee counters. Then installed 31/a7 poison probes and
 * real-callee passes with the nested hooks in rotating modes. Identifiers:
 * wej_/WEJ_. */
typedef struct wej_record { uint32_t id,args[5],regs[3],input[4],eax,edx; } wej_record;
enum { WEJ_SIZE=0x1000, WEJ_W=0x100, WEJ_D1=0x240, WEJ_D2=0x260, WEJ_C1=0x280, WEJ_C2=0x2b0, WEJ_T1=0x2e0, WEJ_T2=0x2f0,
    WEJ_A=0x300, WEJ_VTA1=0x340, WEJ_VTA2=0x380, WEJ_S1=0x3c0, WEJ_S2=0x3e0, WEJ_R=0x400, WEJ_VTR=0x420, WEJ_N1=0x460,
    WEJ_N2=0x470, WEJ_NODES=0x480, WEJ_OBJECT=0x600, WEJ_TABLE1=0x700, WEJ_TABLE2=0x740, WEJ_COUNTER=0x780,
    WEJ_COUNTER2=0x790, WEJ_AIM=0x7a0, WEJ_MOUNT=0x7d0, WEJ_POOL=0x800, WEJ_POOL_NODES=0x840, WEJ_MAX=48, WEJ_IDS=17,
    WEJ_GLOBALS=0x82 };
#define WEJ_BASE ((uint32_t)(uintptr_t)wej_arena)
static BYTE wej_arena[WEJ_SIZE],wej_before[WEJ_SIZE],wej_after[WEJ_SIZE];
static wej_record wej_log[WEJ_MAX];
static unsigned wej_calls,wej_draws;
static int wej_real;
static uint32_t wej_seed,wej_stream;
__attribute__((used)) static uint32_t wej_ecx;
/* 0 49630c, 1 euler_matrix, 2 slot +20h, 3 matrix_compose, 4 mount_offset,
 * 5 4801d4, 6 slot +24h, 7 transform_point, 8 aim_angles, 9 pool_alloc,
 * 10 debris_init, 11 debris_heading, 12 matrix_angles, 13 40f57c,
 * 14 40ee0c, 15 multiply_q10, 16 rng_next. */
static const uint32_t wej_addresses[WEJ_IDS]={0x49630c,0x47ebb8,0,0x47fa20,0x40f540,0x4801d4,0,0x48043c,0x492990,0x471b30,
    0x408940,0x4089e4,0x47f9a0,0x40f57c,0x40ee0c,0x47e0b0,0x492ee0};
static const unsigned wej_slots[WEJ_IDS]={0,0,0x20,0,0,0,0x24,0,0,0,0,0,0,0,0,0,0};
static const unsigned wej_counts[WEJ_IDS]={0,2,2,3,1,3,1,3,3,1,5,4,2,3,4,2,1};
/* The patched callees per hook (slots 2 and 6 live in the arena vtables). */
static const unsigned wej_eject_callees=1u|1u<<1|1u<<3|1u<<4|1u<<5|1u<<7|1u<<8|1u<<9|1u<<10|1u<<11|1u<<12;
static unsigned wej_callees(unsigned index){
    if(index==ES2_HOOK_INDEX_weapon_eject)return wej_eject_callees;
    if(index==ES2_HOOK_INDEX_weapon_damage)return 1u<<13;
    if(index==ES2_HOOK_INDEX_beam_damage)return 1u<<14|1u<<15;
    if(index==ES2_HOOK_INDEX_gun_damage)return 1u<<13|1u<<14|1u<<16;
    return 1u<<14;
}
static uint32_t wej_next(void){
    uint32_t z=wej_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *wej_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int wej_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static uint32_t wej_hash(uint32_t address,unsigned size){
    uint32_t h=0x811c9dc5u;for(unsigned j=0;j<size;++j)h=(h^wej_host(address)[j])*0x01000193u;return h;
}
static void wej_fill(uint32_t address,unsigned size){for(unsigned j=0;j<size;++j)wej_host(address)[j]=(BYTE)wej_next();}
static BYTE *wej_at(unsigned offset){return wej_arena+offset;}
/* Rereads of the eject's state between its calls (direct passes only). */
static void wej_touch(uint32_t pick){
    if(wej_real)return;
    uint32_t b=WEJ_BASE;
    if(pick%5==0)put32(wej_at(WEJ_W+0x18),b+(pick>>8&1?WEJ_D2:WEJ_D1));
    if(pick%7==0)put16(wej_at(WEJ_A+0x32),(uint16_t)(pick>>9&1?0:pick>>10&1?1:pick>>11));
    if(pick%11==0)put32(wej_at(WEJ_W+0x1c),b+(pick>>12&1?WEJ_C2:WEJ_C1));
    if(pick%13==0)put32(wej_at(WEJ_A+0x34),b+(pick>>13&1?WEJ_S2:WEJ_S1));
    if(pick%17==0)put32(wej_at(WEJ_A),b+(pick>>14&1?WEJ_VTA2:WEJ_VTA1));
    if(pick%3==0)put32(mapped+0xa9868,wej_next());
    if(pick%19==0)put32(mapped+0xa98f4,b+(pick>>15&1?WEJ_TABLE2:WEJ_TABLE1));
    if(pick%23==0)put32(mapped+0x2cbee4,b+(pick>>16&1?WEJ_COUNTER2:WEJ_COUNTER));
    if(pick%29==0)put32(mapped+0xa9642,wej_next());
}
/* Rereads of the slot bodies' state after 40ee0c (direct passes only). */
static void wej_slot_touch(uint32_t pick){
    if(wej_real)return;
    uint32_t b=WEJ_BASE;static const uint16_t types[]={0,0,2,2,1,3,0xffff,0x200};
    if(pick%3==0)put16(wej_at(WEJ_W+0x7b),(uint16_t)(pick>>8&1?0:pick>>9));
    if(pick%5==0)put32(wej_at(WEJ_W+0x20),b+(pick>>10&1?WEJ_T2:WEJ_T1));
    if(pick%7==0)put16(wej_at(pick>>11&1?WEJ_T2:WEJ_T1),types[pick>>12&7]);
    if(pick%11==0)put32(wej_at(WEJ_W+0x7f),wej_next());
}
__attribute__((used,noinline)) static uint64_t __cdecl wej_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(wej_calls==WEJ_MAX){fail("weapon eject synthetic call overflow");ExitProcess(1);}
    wej_record *v=wej_log+wej_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,4*wej_counts[id]);memcpy(v->regs,regs,12);
    for(unsigned j=0;j<3;++j)if(wej_stack(v->regs[j]))v->regs[j]=0;
    for(unsigned j=0;j<wej_counts[id];++j)if(wej_stack(v->args[j]))v->args[j]=0;
    uint32_t eax=wej_next(),edx=wej_next(),pick=wej_next(),b=WEJ_BASE;wej_ecx=wej_next();
    switch(id){
    case 0: /* 49630c record stub returns the real FS:0 head for wej_stub_0. */
        __asm__ volatile("movl %%fs:0,%0":"=r"(eax));break;
    case 2: { /* node lookup: N1, N2 or 0, with the index word planned */
        static const uint16_t words[]={0xffff,0,1,2,3,0x7fff,0x8000,0xfffe,7};
        uint32_t node=b+(pick>>4&1?WEJ_N2:WEJ_N1);
        if(!wej_real)put16(wej_host(node)+4,words[pick>>8&7u]);
        eax=pick%5==0?0:node;wej_touch(pick>>1);break; }
    case 3: /* matrix_compose: the composed matrix f-78h */
        wej_fill(a[2],32);wej_touch(pick);break;
    case 4: /* mount_offset: the mount point */
        eax=b+WEJ_MOUNT+(pick>>4&1?12u:0u);wej_touch(pick>>1);break;
    case 5: /* 4801d4: the matrix and the point by content; writes f-58h */
        v->input[0]=wej_hash(a[1],32);for(unsigned j=0;j<3;++j)v->input[1+j]=get32(wej_host(a[0])+4*j);
        wej_fill(a[2],12);wej_touch(pick);break;
    case 6: /* A.V24: the aim node */
        eax=b+WEJ_AIM+(pick>>4&1?8u:0u);wej_touch(pick>>1);break;
    case 7: /* transform_point: writes f-84h */
        for(unsigned j=0;j<3;++j)v->input[j]=get32(wej_host(a[0])+4*j);
        wej_fill(a[2],12);wej_touch(pick);break;
    case 8: /* aim_angles: both points by content; writes the angles */
        v->input[0]=wej_hash(a[1],12);v->input[1]=wej_hash(a[2],12);wej_fill(a[0],6);wej_touch(pick);break;
    case 9: /* pool_alloc: the object or 0 */
        eax=pick%3?b+WEJ_OBJECT:0;wej_touch(pick>>1);break;
    case 10: /* debris_init: the position and the record's context word f-14h */
        for(unsigned j=0;j<3;++j)v->input[j]=get32(wej_host(a[2])+4*j);
        v->input[3]=get16(wej_host(a[2])+0x44);wej_touch(pick);break;
    case 11: case 1: wej_touch(pick);break;
    case 12: /* matrix_angles: the matrix by content; writes the angles */
        v->input[0]=wej_hash(a[0],32);wej_fill(a[1],6);break;
    case 14: wej_slot_touch(pick);break;
    case 15: /* multiply_q10: W+7Fh is reread after it */
        if(!wej_real&&pick%3==0)put32(wej_at(WEJ_W+0x7f),wej_next());
        break;
    case 16: { /* rng_next: AX & 3FFh around 12Ch; a hit by the 20th draw */
        static const uint16_t lows[]={0x12b,0x12c,0x12d,0,0x3ff,0x12a,0x200};
        uint32_t low=pick%8==7?pick>>8&0x3ffu:lows[(pick>>4&7u)%7u];
        if(++wej_draws>=20)low=0x12b;
        eax=(eax&0xfffffc00u)|low;break; }
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
/* Pushes the entry EAX/EDX/ECX for the record before the slot pointer. */
#define WEJ_STUB(n) __attribute__((naked)) static void wej_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _wej_stub_impl\n\taddl $24,%esp\n\tmovl _wej_ecx,%ecx\n\tret\n\t");}
WEJ_STUB(1) WEJ_STUB(2) WEJ_STUB(3) WEJ_STUB(4) WEJ_STUB(5) WEJ_STUB(6) WEJ_STUB(7) WEJ_STUB(8) WEJ_STUB(9) WEJ_STUB(10)
WEJ_STUB(11) WEJ_STUB(12) WEJ_STUB(13) WEJ_STUB(14) WEJ_STUB(15) WEJ_STUB(16)
#undef WEJ_STUB
/* 49630c: captures the entry registers like the other stubs, then links a
 * record at EBP + [descriptor+4] into FS:0 with the saved head at record+0
 * and a zero context word at record+16, as the original does. */
__attribute__((naked)) static void wej_stub_0(void){__asm__ volatile(
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
    "pushl $0\n\tcalll _wej_stub_impl\n\taddl $12,%esp\n\t"
    "popl %ecx\n\tmovl 4(%ecx),%ecx\n\taddl %ebp,%ecx\n\tmovl %eax,(%ecx)\n\tmovw $0,16(%ecx)\n\tmovl %ecx,%fs:0\n\t"
    "popl %edx\n\taddl $4,%esp\n\tmovl _wej_ecx,%ecx\n\tret\n\t");}
static void *wej_stubs[WEJ_IDS]={wej_stub_0,wej_stub_1,wej_stub_2,wej_stub_3,wej_stub_4,wej_stub_5,wej_stub_6,wej_stub_7,wej_stub_8,
    wej_stub_9,wej_stub_10,wej_stub_11,wej_stub_12,wej_stub_13,wej_stub_14,wej_stub_15,wej_stub_16};
/* Counter pointer, pool pointer, the value 4a9868, the shape table
 * pointer and RNG stream A. */
static const struct { uint32_t rva,size; } wej_ranges[]={{0x2cbee4,4},{0xa9642,4},{0xa9868,4},{0xa98f4,4},{0xd2599,0x72}};
static void wej_globals(BYTE *out,int restore){
    for(unsigned i=0;i<sizeof(wej_ranges)/sizeof(wej_ranges[0]);++i){
        if(restore)memcpy(mapped+wej_ranges[i].rva,out,wej_ranges[i].size);else memcpy(out,mapped+wej_ranges[i].rva,wej_ranges[i].size);
        out+=wej_ranges[i].size;
    }
}
static int wej_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<WEJ_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
static void wej_reset(void){wej_calls=0;wej_draws=0;wej_stream=wej_seed;}
/* Pool header (free count +0, used count +2, free head +0Ah, used head
 * +0Eh) with count free nodes of 80h bytes; objects start at node+8. */
static void wej_pool(uint32_t pool,uint32_t nodes,unsigned count){
    put16(wej_host(pool),(uint16_t)count);put16(wej_host(pool)+2,0);put32(wej_host(pool)+0xe,0);
    for(unsigned i=0;i<count;++i){uint32_t n=nodes+0x80u*i;put32(wej_host(n),i+1<count?n+0x80u:0);put32(wej_host(n)+4,i?n-0x80u:0);}
    put32(wej_host(pool)+0xa,count?nodes:0);
}
static uint32_t wej_pick32(const uint32_t *values,unsigned count){return values[random32()%count];}
/* The arena: the weapon W with two definitions, two records and two type
 * words; the vehicle A with two vtables, two shapes, the node tree root R
 * and two nodes; the debris object, two shape tables, two counters, the
 * aim nodes, the mount points and a pool. Arguments: (W, A, flag) for the
 * eject, (W, A, x, d) for the slot bodies. */
static void wej_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    uint32_t b=WEJ_BASE;
    wej_seed=random32();random_bytes(wej_arena,WEJ_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    static const BYTE ejected[]={0,0,0,1,0x80,0xff};
    wej_arena[WEJ_W+0x49]=n<64?(BYTE)(n%2):random32()%2?ejected[random32()%6]:(BYTE)random32();
    put32(wej_at(WEJ_W+0x18),b+(random32()%2?WEJ_D2:WEJ_D1));put32(wej_at(WEJ_W+0x1c),b+(random32()%2?WEJ_C2:WEJ_C1));
    put32(wej_at(WEJ_W+0x20),b+(random32()%2?WEJ_T2:WEJ_T1));
    static const uint16_t gates[]={0,1,0x334,0x8000};put16(wej_at(WEJ_W+0x7b),random32()%4?gates[random32()%4]:(uint16_t)random32());
    static const BYTE classes[]={0,1,2,3,4,5,0x7f,0x80,0xff,3,2,0};
    wej_arena[WEJ_D1+6]=classes[random32()%12];wej_arena[WEJ_D2+6]=classes[random32()%12];
    for(unsigned i=0;i<2;++i){uint16_t k=(uint16_t)(random32()%20u-4u);put16(wej_at((i?WEJ_C2:WEJ_C1)+0x26),k);}
    static const uint16_t types[]={0,0,0,2,2,1,3,0xffff};
    put16(wej_at(WEJ_T1),types[random32()%8]);put16(wej_at(WEJ_T2),types[random32()%8]);
    put32(wej_at(WEJ_A),b+(random32()%2?WEJ_VTA2:WEJ_VTA1));
    static const uint16_t poses[]={0,0,1,0x100};put16(wej_at(WEJ_A+0x32),random32()%4?poses[random32()%4]:(uint16_t)random32());
    put32(wej_at(WEJ_A+0x34),b+WEJ_S1);put32(wej_at(WEJ_S1+4),b+WEJ_R);put32(wej_at(WEJ_S2+4),b+WEJ_R);
    put32(wej_at(WEJ_S1+0x16),b+WEJ_NODES);put32(wej_at(WEJ_S2+0x16),b+WEJ_NODES+0x20);put32(wej_at(WEJ_R),b+WEJ_VTR);
    put32(wej_at(WEJ_VTR+0x20),(uint32_t)(uintptr_t)wej_stubs[2]);
    put32(wej_at(WEJ_VTA1+0x24),(uint32_t)(uintptr_t)wej_stubs[6]);put32(wej_at(WEJ_VTA2+0x24),(uint32_t)(uintptr_t)wej_stubs[6]);
    static const uint16_t nodes[]={0xffff,0,1,2,3,0x8000};
    for(unsigned i=0;i<2;++i)put16(wej_at((i?WEJ_N2:WEJ_N1)+4),real?nodes[random32()%5]:(uint16_t)random32());
    put32(mapped+0x2cbee4,b+WEJ_COUNTER);put32(mapped+0xa98f4,b+WEJ_TABLE1);random_bytes(mapped+0xa9868,4);
    put32(mapped+0xa9642,real?b+WEJ_POOL:random32());
    if(real){
        BYTE *state=mapped+0xd2599;random_bytes(state,112);state[112]=(BYTE)(random32()%56);state[113]=(BYTE)(random32()%56);
        wej_pool(b+WEJ_POOL,b+WEJ_POOL_NODES,random32()%3);
    }
    a[0]=b+WEJ_W;a[1]=b+WEJ_A;
    if(index==ES2_HOOK_INDEX_weapon_eject){
        static const uint32_t flags[]={0,1,0x10000,0xffff0000u,0xffff,2};
        a[2]=random32()%4?flags[random32()%6]:random32();
        return;
    }
    /* Conditions: 100h, the 80h boundary, 25-point steps, extremes. */
    static const uint32_t ds[]={0x100,0x100,0xff,0x101,0x80,0x81,0x7f,0x99,0x9a,0x98,0xb2,0xe4,0x7fffffff,0x80000000u,
        0xffffff00u,0x10100,0,0x400};
    static const uint32_t xs[]={0,0x80,0x7f,0x81,0x67,0x66,0x99,0x100,0xffffff00u,0x80000000u,0x7fffffff,0x48};
    uint32_t d=wej_pick32(ds,18),x=wej_pick32(xs,12);
    if(random32()%4==0)d=0x80u+random32()%800u;
    if(random32()%4==0)x=random32()%2?d-random32()%200u:random32();
    if(real&&index==ES2_HOOK_INDEX_gun_damage){d=random32()%2?0x100:0x60u+random32()%800u;x=0x40u+random32()%800u;}
    a[2]=x;a[3]=d;
}
static int wej_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const wej_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,wej_calls);
    for(unsigned j=0;j<calls&&j<wej_calls&&expected;++j)if(memcmp(expected+j,wej_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(wej_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<WEJ_SIZE;++j)if(wej_after[j]!=wej_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,wej_after[j],wej_arena[j]);break;}
    return 1;
}
static unsigned wej_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<wej_calls;++j){unsigned id=wej_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?wej_slots[id]==c->key:!wej_slots[id]&&wej_addresses[id]==c->key)++hits;}
    return hits;
}
static int wej_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[WEJ_GLOBALS],after[WEJ_GLOBALS],actual[WEJ_GLOBALS];static wej_record expected[WEJ_MAX];uint32_t head,head_after;
    memcpy(wej_before,wej_arena,WEJ_SIZE);wej_globals(before,0);
    __asm__ volatile("movl %%fs:0,%0":"=r"(head));
    wej_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=wej_calls;memcpy(expected,wej_log,calls*sizeof(*expected));
    memcpy(wej_after,wej_arena,WEJ_SIZE);wej_globals(after,0);
    memcpy(wej_arena,wej_before,WEJ_SIZE);wej_globals(before,1);wej_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];for(unsigned i=0;i<count&&i<16;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);wej_globals(actual,0);
    __asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
    if(head_after!=head)return fail("weapon eject left FS:0 changed");
    if(n<4096)for(unsigned i=0;i<count&&i<16;++i)if(c[i].count-counts[i]!=wej_expected_hits(c+i))return fail("weapon eject callee counter delta");
    if(pair[0]!=pair[1]||calls!=wej_calls||memcmp(expected,wej_log,calls*sizeof(*expected))||memcmp(wej_after,wej_arena,WEJ_SIZE)||
       wej_globals_differ(after,actual))
        return wej_report("direct",n,pair,calls,expected);
    wej_globals(before,1);
    return 0;
}
static int wej_patch(BYTE saved[WEJ_IDS][5],int active[WEJ_IDS],unsigned which,int on){
    for(unsigned i=0;i<WEJ_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!wej_addresses[i])continue;if(actor_patch(wej_addresses[i]-0x400000u,wej_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(wej_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static int wej_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;uint32_t head,head_after;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;wej_build(index,n,a,r,0);
        BYTE before[WEJ_GLOBALS],after[WEJ_GLOBALS],actual[WEJ_GLOBALS],expected_fpu[108],actual_fpu[108];static wej_record expected[WEJ_MAX];
        memcpy(wej_before,wej_arena,WEJ_SIZE);wej_globals(before,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head));
        wej_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=wej_calls;memcpy(expected,wej_log,calls*sizeof(*expected));memcpy(wej_after,wej_arena,WEJ_SIZE);wej_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("weapon eject poison install");
        memcpy(wej_arena,wej_before,WEJ_SIZE);wej_globals(before,1);wej_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);wej_globals(actual,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(head_after!=head||pair[0]!=pair[1]||calls!=wej_calls||memcmp(expected,wej_log,calls*sizeof(*expected))||memcmp(wej_after,wej_arena,WEJ_SIZE)||
           wej_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=wej_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("weapon eject poison uninstall");wej_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state/FS:0\n",d->name,total);
    return status;
}
/* Real callees with the nested hooks in rotating original/replaced/compare
 * modes (compare falls back to replaced where refused). The eject: real
 * euler_matrix, matrix_compose, transform_point, aim_angles, matrix_angles
 * and pool_alloc (a real pool of 0..2 nodes) and the original 4801d4; the
 * record stub, mount_offset, debris_init, debris_heading and the two slots
 * stay stubs. The slot bodies: real weapon_eject (its callees stubs),
 * weapon_damage, multiply_q10 and rng_next. Pair, ordered stub calls,
 * arena, globals and FS:0 are compared against a run with only the nested
 * hooks. */
/* A result register that holds a frame address (0x4801d4 leaves one in
 * EDX on the class exit) differs between the original frame and the
 * adapter scratch; both must then be stack addresses. */
static int wej_same(uint32_t want,uint32_t got){return want==got||(wej_stack(want)&&wej_stack(got));}
static int wej_real_pass(unsigned index){
    const int eject=index==ES2_HOOK_INDEX_weapon_eject;
    const unsigned keep=eject?(1u|1u<<4|1u<<10|1u<<11):wej_eject_callees|1u;
    const unsigned eject_nested[6]={ES2_HOOK_INDEX_euler_matrix,ES2_HOOK_INDEX_matrix_compose,ES2_HOOK_INDEX_transform_point,
        ES2_HOOK_INDEX_aim_angles,ES2_HOOK_INDEX_matrix_angles,ES2_HOOK_INDEX_pool_alloc};
    unsigned nested[6],count=0;
    if(eject)for(unsigned j=0;j<6;++j)nested[count++]=eject_nested[j];
    else{
        nested[count++]=ES2_HOOK_INDEX_weapon_eject;
        if(index!=ES2_HOOK_INDEX_weapon_damage)nested[count++]=ES2_HOOK_INDEX_weapon_damage;
        if(index==ES2_HOOK_INDEX_beam_damage)nested[count++]=ES2_HOOK_INDEX_multiply_q10;
        if(index==ES2_HOOK_INDEX_gun_damage)nested[count++]=ES2_HOOK_INDEX_rng_next;
    }
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[6]={0};
    BYTE saved[WEJ_IDS][5];int active[WEJ_IDS];static wej_record expected[WEJ_MAX];
    if(wej_patch(saved,active,keep,1))return 1;
    wej_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u,head,head_after;wej_build(index,n,a,r,1);
        BYTE before[WEJ_GLOBALS],after[WEJ_GLOBALS],actual[WEJ_GLOBALS];
        memcpy(wej_before,wej_arena,WEJ_SIZE);wej_globals(before,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head));
        es2_hook hk[7];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("weapon eject nested install");}
        wej_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        unsigned calls=wej_calls;memcpy(expected,wej_log,calls*sizeof(*expected));
        memcpy(wej_after,wej_arena,WEJ_SIZE);wej_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+6,d,&o,function_hook_extension(index)))return fail("weapon eject caller install");
        memcpy(wej_arena,wej_before,WEJ_SIZE);wej_globals(before,1);wej_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);wej_globals(actual,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
        if(head_after!=head||!wej_same(expected_pair[0],actual_pair[0])||!wej_same(expected_pair[1],actual_pair[1])||calls!=wej_calls||
           memcmp(expected,wej_log,calls*sizeof(*expected))||memcmp(wej_after,wej_arena,WEJ_SIZE)||wej_globals_differ(after,actual)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            fprintf(stderr,"real mode=%u head %08x/%08x\n",mode,head,head_after);status=wej_report("real",n,pair,calls,expected);
        }
        for(unsigned j=0;j<7;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("weapon eject nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("weapon eject nested uninstall");}
        wej_globals(before,1);++total;
    }
    wej_real=0;
    if(wej_patch(saved,active,0,0))return 1;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, 31/a7 stack, full pair, calls, memory and FS:0\n",d->name,total,count);
    return status;
}
static int weapon_eject_cases(unsigned index,unsigned limit){
    const unsigned which=wej_callees(index)|(index==ES2_HOOK_INDEX_weapon_eject?1u:0u);
    BYTE saved[WEJ_IDS][5],old_globals[WEJ_GLOBALS];int active[WEJ_IDS];
    wej_globals(old_globals,0);
    if(wej_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_WEAPON_EJECT_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];wej_build(index,n,a,r,0);status=wej_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=wej_installed_probes(index,64);
    if(wej_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=wej_real_pass(index);
    wej_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, frame buffers by content, arena/globals/FS:0\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
