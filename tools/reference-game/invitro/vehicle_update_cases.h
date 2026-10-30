/* Per-vehicle update: combat_rating 41edd8, condition_record 41b4ec,
 * device_state 41aa10, device_word 41aa44, ready_tone 41b0bc, weapon_tally
 * 40fbdc and vehicle_update 41aa5c (docs/reverse-engineering/vehicle.md,
 * "Per-vehicle update"). Direct cases run the original body and the core
 * against logging stubs for every game callee and virtual (ordered argument
 * slots, entry EAX/EDX/ECX, payloads by content; values in the worker's stack
 * reservation are masked). The two device readers run without stubs. Then
 * installed 31/a7 poison probes and a real-callee pass with the nested hooks in
 * rotating modes: for vehicle_update every hooked callee and the six leaves are
 * real (memset is the harness's own emulation), only the virtuals, sight_check,
 * condition_table, the clock and the sound call stay stubs and their inputs are
 * compared. */
typedef struct vu_record { uint32_t id,args[3],regs[3],input[6],eax,edx; } vu_record;
enum { VU_SIZE=0x1400, VU_A=0x100, VU_T=0x480, VU_C=0x680, VU_G=0x740, VU_LIST=0x780, VU_W=0x800, VU_WREC=0x980,
    VU_VT=0xa00, VU_GRP=0xa80, VU_D1=0xb00, VU_D2=0xb90, VU_TABS=0xc40, VU_RECS=0xc80, VU_VALUES=0xec0,
    VU_GROUPS=0xf00, VU_MAXIMA=0xf40, VU_OUT=0x1000, VU_T2=0x1080, VU_MAX=96, VU_IDS=22, VU_RNG=0x72 };
#define VU_BASE ((uint32_t)(uintptr_t)vu_arena)
static BYTE vu_arena[VU_SIZE],vu_before[VU_SIZE],vu_after[VU_SIZE];
static vu_record vu_log[VU_MAX];
static unsigned vu_calls;
static int vu_real;
static uint32_t vu_seed,vu_stream;
__attribute__((used)) static uint32_t vu_ecx;
/* 0 467924, 1 V0, 2 413b38, 3 467aa8, 4 467a48, 5 41edd8, 6 41aa44, 7 41aa10,
 * 8 40fbdc, 9 492ee0, 10 49288c, 11 492934, 12 412608, 13 V60, 14 41b0bc
 * (vehicle_update); 15 41b4ec, 16 V54, 17 47e0a0 (combat_rating); 18 4151a4
 * (condition_record); 19 49681a (weapon_tally); 20 467828, 21 462830
 * (ready_tone). Virtuals have no address; one table serves every object. */
static const uint32_t vu_addresses[VU_IDS]={0x467924,0,0x413b38,0x467aa8,0x467a48,0x41edd8,0x41aa44,0x41aa10,0x40fbdc,
    0x492ee0,0x49288c,0x492934,0x412608,0,0x41b0bc,0x41b4ec,0,0x47e0a0,0x4151a4,0x49681a,0x467828,0x462830};
static const unsigned vu_counts[VU_IDS]={1,3,2,1,1,1,1,1,2,1,2,2,2,2,1,2,1,2,2,3,0,1},
    vu_slots[VU_IDS]={0,0,0,0,0,0,0,0,0,0,0,0,0,0x60,0,0,0x54,0,0,0,0,0};
static uint32_t vu_next(void){
    uint32_t z=vu_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *vu_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int vu_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static void vu_vector(uint32_t at,uint32_t *in){for(unsigned j=0;j<3;++j)in[j]=get32(vu_host(at)+4*j);}
/* A word with all interesting signs: zero, small, negative, 8000h. */
static uint16_t vu_word(uint32_t pick){
    static const uint16_t words[]={0,0,1,0xffff,0x8000,0x7fff,0x40,0xb4,0xb5,0x100};
    return pick%3?words[(pick>>4)%10]:(uint16_t)(pick>>8);
}
__attribute__((used,noinline)) static uint64_t __cdecl vu_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    uint32_t eax=vu_next(),edx=vu_next(),pick=vu_next();vu_ecx=vu_next();
    if(vu_calls==VU_MAX){fail("vehicle update call overflow");ExitProcess(1);}
    vu_record *v=vu_log+vu_calls++;memset(v,0,sizeof(*v));v->id=id;
    for(unsigned k=0;k<vu_counts[id];++k)v->args[k]=vu_stack(a[k])?0:a[k];
    if(!vu_real)for(unsigned j=0;j<3;++j)v->regs[j]=vu_stack(regs[j])?0:regs[j];
    switch(id){
    case 2: { /* energy_accumulate: AX near both clamp limits of 41aac4/41aacf. */
        static const uint16_t limits[]={0x251c,0x251b,0x251d,0xfe0c,0xfe0d,0xfe0b,0};
        if(pick%3==0)eax=(eax&0xffff0000u)|limits[(pick>>4)%7];
        break; }
    case 3:if(pick%3==0)eax=0;break;                                  /* timer_dword_step: EAX 0 or not */
    case 4:if(pick%2)eax&=0xffff0000u;break;                          /* timer_word_step: AX 0 or not */
    case 6:case 7:case 16:if(pick%2)eax&=0xffff0000u;break;           /* AX tested */
    case 8: /* weapon_tally writes the five tally words. */
        for(unsigned j=0;j<5;++j)put16(vu_host(a[1])+2*j,vu_next()%3?0:(uint16_t)(1+vu_next()%0xffff));
        break;
    case 10: /* dist_gate: sometimes beyond the 7FFFh clamp, sometimes negative. */
        vu_vector(a[0],v->input);vu_vector(a[1],v->input+3);
        eax=pick%3==0?eax:pick%3==1?eax%0x20000u:0x1fffcu+(pick>>8)%16;
        break;
    case 11: /* heading_point: often inside the +-3000h cone; may switch the target. */
        vu_vector(a[0],v->input);vu_vector(a[1],v->input+3);
        if(pick%2){uint32_t A=VU_BASE+VU_A;
            eax=(eax&0xffff0000u)|(uint16_t)(get16(vu_host(A+0x10))-get16(vu_host(A+0x298))+vu_next()%0x6400u-0x3200u);}
        if(pick%16==3)put32(vu_host(VU_BASE+VU_A+0x1a4),get32(vu_host(VU_BASE+VU_A+0x1a4))==VU_BASE+VU_T?VU_BASE+VU_T2:VU_BASE+VU_T);
        break;
    case 12: /* sight_check: AX zero, AL zero with AH set, or nonzero. */
        eax=pick%3==0?eax&0xffff0000u:pick%3==1?(eax&0xffff0000u)|((1u+(pick>>4)%255u)<<8):eax|1u;
        break;
    case 13: { /* V60: writes the word through a non-null pointer; index 0..5 in
                * the real pass (weapon_tally's frame), -4..9 otherwise. */
        if(a[1])put16(vu_host(a[1]),(uint16_t)vu_next());
        int32_t k=vu_real?(int32_t)(pick%6):(int32_t)(pick%14)-4;if(pick%3==0)k=5;
        eax=(eax&0xffff0000u)|(uint16_t)k;
        break; }
    case 15: /* condition_record: the 42-word record. */
        for(unsigned j=0;j<42;++j)put16(vu_host(a[1])+2*j,vu_word(vu_next()));
        break;
    case 18: /* condition_table: words 1..41. */
        for(unsigned j=1;j<42;++j)put16(vu_host(a[1])+2*j,vu_word(vu_next()));
        break;
    case 19: /* memset: the zeroed span is the payload. */
        v->input[0]=a[2];if(a[2]<=64)memset(vu_host(a[0]),(int)a[1],a[2]);
        break;
    case 20:if(pick%2)eax^=0x40u;break;
    default:break;
    }
    if(id==16&&pick%8==1)put16(vu_host(VU_BASE+VU_G+8),(uint16_t)(vu_next()%11)); /* V54 may change the count */
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define VU_STUB(n) __attribute__((naked)) static void vu_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _vu_stub_impl\n\taddl $24,%esp\n\tmovl _vu_ecx,%ecx\n\tret\n\t");}
VU_STUB(0) VU_STUB(1) VU_STUB(2) VU_STUB(3) VU_STUB(4) VU_STUB(5) VU_STUB(6) VU_STUB(7) VU_STUB(8) VU_STUB(9) VU_STUB(10)
VU_STUB(11) VU_STUB(12) VU_STUB(13) VU_STUB(14) VU_STUB(15) VU_STUB(16) VU_STUB(17) VU_STUB(18) VU_STUB(19) VU_STUB(20)
VU_STUB(21)
#undef VU_STUB
static void *vu_stubs[VU_IDS]={vu_stub_0,vu_stub_1,vu_stub_2,vu_stub_3,vu_stub_4,vu_stub_5,vu_stub_6,vu_stub_7,vu_stub_8,
    vu_stub_9,vu_stub_10,vu_stub_11,vu_stub_12,vu_stub_13,vu_stub_14,vu_stub_15,vu_stub_16,vu_stub_17,vu_stub_18,vu_stub_19,
    vu_stub_20,vu_stub_21};
/* Calls target with the six slots, EAX/EDX/ECX/EBX from regs and a
 * patterned stack below the arguments; returns EDX:EAX in pair. */
__attribute__((naked,noinline)) static void vu_call(void *target,const uint32_t *a,const uint32_t regs[4],uint32_t pattern,uint32_t pair[2]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 20(%ebp),%eax\n\tmovl $1024,%ecx\n\trep stosl\n\taddl $3840,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 20(%esi)\n\tpushl 16(%esi)\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%esi\n\tmovl (%esi),%eax\n\tmovl 4(%esi),%edx\n\tmovl 8(%esi),%ecx\n\tmovl 12(%esi),%ebx\n\tcall *8(%ebp)\n\t"
        "movl 24(%ebp),%ecx\n\tmovl %eax,(%ecx)\n\tmovl %edx,4(%ecx)\n\tleal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* Globals: the RNG state (rng_next in the real pass), the two tone bytes
 * and the time scale word (read by the real timers and movement_step). */
static void vu_globals(BYTE *out,int restore){
    if(restore){memcpy(mapped+0xd2599,out,VU_RNG);memcpy(mapped+0x9a1d0,out+VU_RNG,2);memcpy(mapped+0xd3b64,out+VU_RNG+2,2);}
    else{memcpy(out,mapped+0xd2599,VU_RNG);memcpy(out+VU_RNG,mapped+0x9a1d0,2);memcpy(out+VU_RNG+2,mapped+0xd3b64,2);}
}
static void vu_reset(void){vu_calls=0;vu_stream=vu_seed;}
static void vu_position(uint32_t at){for(unsigned j=0;j<3;++j)put32(vu_host(at)+4*j,random32()%0x80000u-0x40000u);}
/* Part tables for the real condition_table (P = A+206h, tabs = A+212h):
 * nonzero divisors in loops 1 and 2 (a zero divisor faults in both). */
static void vu_tables(uint32_t A){
    uint32_t b=VU_BASE,P=A+0x206;
    put32(vu_host(P),b+VU_VALUES);put32(vu_host(P+4),b+VU_GROUPS);put32(vu_host(P+0xc),b+VU_TABS);
    put32(vu_arena+VU_TABS,b+VU_RECS);put32(vu_arena+VU_TABS+4,b+VU_MAXIMA);
    for(unsigned i=0;i<29;++i){put16(vu_arena+VU_GROUPS+2*i,random32()%4?(uint16_t)(random32()%3000):(uint16_t)(0u-random32()%3000));
        put16(vu_arena+VU_RECS+18*i,(uint16_t)(1+random32()%2000));}
    for(unsigned i=0;i<22;++i){put16(vu_arena+VU_VALUES+2*i,random32()%4?(uint16_t)(random32()%3000):(uint16_t)(0u-random32()%3000));
        put16(vu_arena+VU_MAXIMA+2*i,random32()%6?(uint16_t)(1+random32()%3000):0);}
    for(unsigned i=0;i<10;++i)put16(vu_arena+VU_MAXIMA+2*(12+i),(uint16_t)(1+random32()%3000));
}
/* One vehicle A with config C, weapon group G (up to 12 weapons, count <= 10
 * by the combat_rating precondition), targets T/T2, devices D1/D2 and the
 * group object at A+45h. */
static void vu_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    uint32_t b=VU_BASE,A=b+VU_A,T=b+VU_T,C=b+VU_C,G=b+VU_G;
    vu_seed=random32();random_bytes(vu_arena,VU_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    for(unsigned s=0;s<0x64;s+=4)put32(vu_arena+VU_VT+s,0);
    for(unsigned id=0;id<VU_IDS;++id)if(vu_slots[id])put32(vu_arena+VU_VT+vu_slots[id],(uint32_t)(uintptr_t)vu_stubs[id]);
    put32(vu_arena+VU_VT,(uint32_t)(uintptr_t)vu_stubs[1]);
    if(real){BYTE *state=mapped+0xd2599;random_bytes(state,112);state[112]=(BYTE)(random32()%56);state[113]=(BYTE)(random32()%56);
        put16(mapped+0xd3b64,(uint16_t)(random32()%4?random32()%0x300:random32()));}
    mapped[0x9a1d0]=(BYTE)(random32()%2?0:1+random32()%255);mapped[0x9a1d1]=(BYTE)(random32()%2?0:1+random32()%255);
    /* Flags of A, T, T2 with zero and nonzero values. */
    static const unsigned flags[]={0x94,0x96,0x9b,0x9c,0x9d,0xa1,0xa3};
    for(unsigned i=0;i<7;++i){vu_host(A)[flags[i]]=(BYTE)(random32()%2?0:1+random32()%255);
        vu_host(T)[flags[i]]=(BYTE)(random32()%2?0:1+random32()%255);vu_host(b+VU_T2)[flags[i]]=(BYTE)(random32()%2?0:1+random32()%255);}
    put16(vu_host(T+0x1a8),random32()%2?0:(uint16_t)random32());put16(vu_host(b+VU_T2+0x1a8),random32()%2?0:(uint16_t)random32());
    vu_position(A+0x26);vu_position(T+0x26);vu_position(b+VU_T2+0x26);
    put32(vu_host(A+0x1f2),C);put32(vu_host(A+0x202),G);put32(vu_host(A+0x45),b+VU_GRP);
    put32(vu_host(A+0x1a4),random32()%4==0?0:random32()%16==0?A:T);
    put32(vu_host(A+0x307),random32()%4?b+VU_D1:0);put32(vu_host(A+0x30b),random32()%3?b+VU_D2:0);
    put16(vu_host(b+VU_D1+0x7d),vu_word(random32()));put16(vu_host(b+VU_D1+0x7f),vu_word(random32()));
    put16(vu_host(b+VU_D2+0x7f),(uint16_t)(random32()%3?random32()%0x66:random32()));
    vu_arena[VU_GRP+0x12]=(BYTE)(random32()%2?0:1+random32()%255);
    put16(vu_host(C+0x50),random32()%2?0:(uint16_t)random32());
    put16(vu_host(A+0x222),(uint16_t)(random32()%2?random32()%1600:random32()));put16(vu_host(A+0x224),(uint16_t)(random32()%2?random32()%1600:random32()));
    /* Group: list, count <= 10 (combat_rating's precondition; negative only
     * where the loop is signed), selected byte -3..12 whose weapon exists
     * (the original calls its V60 unconditionally); a null list only for
     * weapon_tally, which stops at a null slot address. */
    put32(vu_host(G),b+VU_VT);put32(vu_host(G+4),index==ES2_HOOK_INDEX_weapon_tally&&random32()%32==0?0:b+VU_LIST);
    put16(vu_host(G+8),(uint16_t)(index==ES2_HOOK_INDEX_combat_rating&&random32()%8==0?0u-random32()%3:random32()%11));
    vu_arena[VU_G+0x1d]=(BYTE)(random32()%3?random32()%13:(uint32_t)-(int32_t)(1+random32()%3));
    for(int i=-3;i<16;++i)put32(vu_arena+VU_LIST+4*i,random32()%5?b+VU_W+0x20*(uint32_t)((i+3)%12):0);
    if((int8_t)vu_arena[VU_G+0x1d]>=0)put32(vu_arena+VU_LIST+4*vu_arena[VU_G+0x1d],b+VU_W);
    for(unsigned i=0;i<12;++i){put32(vu_arena+VU_W+0x20*i,b+VU_VT);put32(vu_arena+VU_W+0x20*i+0x1c,b+VU_WREC);}
    put32(vu_host(A+0x212),b+VU_TABS);put32(vu_arena+VU_TABS,b+VU_RECS);
    if(real)vu_tables(A);
    if(index==ES2_HOOK_INDEX_condition_record){a[0]=A;a[1]=b+VU_OUT;return;}
    if(index==ES2_HOOK_INDEX_weapon_tally){a[0]=G;a[1]=b+VU_OUT;return;}
    if(index==ES2_HOOK_INDEX_device_state||index==ES2_HOOK_INDEX_device_word){
        if(n%7==0)put32(vu_host(A+0x307),0);a[0]=A;return;}
    a[0]=A;
}
static int vu_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const vu_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,vu_calls);
    for(unsigned j=0;j<calls&&j<vu_calls&&expected;++j)if(memcmp(expected+j,vu_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(vu_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<VU_SIZE;++j)if(vu_after[j]!=vu_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,vu_after[j],vu_arena[j]);break;}
    for(unsigned j=0;j<calls&&expected;++j)fprintf(stderr," trace %u id%u args %08x %08x %08x eax %08x\n",j,expected[j].id,
        expected[j].args[0],expected[j].args[1],expected[j].args[2],expected[j].eax);
    return 1;
}
static unsigned vu_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<vu_calls;++j){unsigned id=vu_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?vu_slots[id]==c->key:!vu_slots[id]&&vu_addresses[id]==c->key)++hits;}
    return hits;
}
static int vu_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[VU_RNG+4],after[VU_RNG+4],actual[VU_RNG+4];static vu_record expected[VU_MAX];
    memcpy(vu_before,vu_arena,VU_SIZE);vu_globals(before,0);
    vu_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=vu_calls;memcpy(expected,vu_log,calls*sizeof(*expected));
    memcpy(vu_after,vu_arena,VU_SIZE);vu_globals(after,0);
    memcpy(vu_arena,vu_before,VU_SIZE);vu_globals(before,1);vu_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[24];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);vu_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=vu_expected_hits(c+i))return fail("vehicle update callee counter delta");
    if(pair[0]!=pair[1]||calls!=vu_calls||memcmp(expected,vu_log,calls*sizeof(*expected))||memcmp(vu_after,vu_arena,VU_SIZE)||
       memcmp(after,actual,sizeof(after)))
        return vu_report("direct",n,pair,calls,expected);
    return 0;
}
static int vu_patch(BYTE saved[VU_IDS][5],int active[VU_IDS],uint32_t which,int on){
    for(unsigned i=0;i<VU_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!vu_addresses[i])continue;if(actor_patch(vu_addresses[i]-0x400000u,vu_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(vu_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
/* Direct-call stubs per hook (bit i = callee id i). */
static uint32_t vu_direct_stubs(unsigned index){
    if(index==ES2_HOOK_INDEX_vehicle_update)return 0x7fffu;
    if(index==ES2_HOOK_INDEX_combat_rating)return (1u<<15)|(1u<<17);
    if(index==ES2_HOOK_INDEX_condition_record)return 1u<<18;
    if(index==ES2_HOOK_INDEX_weapon_tally)return 1u<<19;
    if(index==ES2_HOOK_INDEX_ready_tone)return (1u<<20)|(1u<<21);
    return 0;
}
static int vu_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],regs[4],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;vu_build(index,n,a,r,0);
        regs[0]=regs[1]=regs[2]=regs[3]=pattern;
        BYTE before[VU_RNG+4],after[VU_RNG+4],actual[VU_RNG+4],expected_fpu[108],actual_fpu[108];static vu_record expected[VU_MAX];
        memcpy(vu_before,vu_arena,VU_SIZE);vu_globals(before,0);
        vu_reset();vu_call(mapped+d->rva,a,regs,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=vu_calls;memcpy(expected,vu_log,calls*sizeof(*expected));memcpy(vu_after,vu_arena,VU_SIZE);vu_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("vehicle update poison install");
        memcpy(vu_arena,vu_before,VU_SIZE);vu_globals(before,1);vu_reset();vu_call(mapped+d->rva,a,regs,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);vu_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=vu_calls||memcmp(expected,vu_log,calls*sizeof(*expected))||memcmp(vu_after,vu_arena,VU_SIZE)||
           memcmp(after,actual,sizeof(after))||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=vu_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("vehicle update poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state\n",d->name,total);
    return status;
}
/* Real callees with the nested hooks in rotating original/replaced/compare
 * modes and a patterned stack; the remaining stubs' arguments and payloads
 * are compared in order (entry registers only in the direct pass). */
static int vu_real_pass(unsigned index){
    static const unsigned update_nested[]={ES2_HOOK_INDEX_movement_step,ES2_HOOK_INDEX_energy_accumulate,ES2_HOOK_INDEX_timer_dword_step,
        ES2_HOOK_INDEX_timer_word_step,ES2_HOOK_INDEX_rng_next,ES2_HOOK_INDEX_dist_gate,ES2_HOOK_INDEX_heading_point,
        ES2_HOOK_INDEX_combat_rating,ES2_HOOK_INDEX_condition_record,ES2_HOOK_INDEX_multiply_q8,ES2_HOOK_INDEX_device_word,
        ES2_HOOK_INDEX_device_state,ES2_HOOK_INDEX_weapon_tally,ES2_HOOK_INDEX_ready_tone};
    static const unsigned rating_nested[]={ES2_HOOK_INDEX_condition_record,ES2_HOOK_INDEX_multiply_q8};
    static const unsigned record_nested[]={ES2_HOOK_INDEX_condition_table};
    const unsigned *nested=NULL;unsigned count=0;uint32_t stubs=0;
    if(index==ES2_HOOK_INDEX_vehicle_update){nested=update_nested;count=14;stubs=(1u<<12)|(1u<<18)|(1u<<20)|(1u<<21);}
    else if(index==ES2_HOOK_INDEX_combat_rating){nested=rating_nested;count=2;stubs=1u<<18;}
    else if(index==ES2_HOOK_INDEX_condition_record){nested=record_nested;count=1;stubs=0;}
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[16]={0};
    BYTE saved[VU_IDS][5];int active[VU_IDS];if(vu_patch(saved,active,stubs,1))return 1;
    static vu_record expected[VU_MAX];
    vu_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<1024&&!status;++n){
        uint32_t a[6],r[3],regs[4],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;vu_build(index,n,a,r,1);
        regs[0]=regs[1]=regs[2]=regs[3]=pattern;
        BYTE before[VU_RNG+4],after[VU_RNG+4],actual[VU_RNG+4];
        memcpy(vu_before,vu_arena,VU_SIZE);vu_globals(before,0);
        es2_hook hk[16];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("vehicle update nested install");}
        vu_reset();vu_call(mapped+d->rva,a,regs,pattern,expected_pair);
        unsigned calls=vu_calls;memcpy(expected,vu_log,calls*sizeof(*expected));
        memcpy(vu_after,vu_arena,VU_SIZE);vu_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+count,d,&o,function_hook_extension(index)))return fail("vehicle update caller install");
        memcpy(vu_arena,vu_before,VU_SIZE);vu_globals(before,1);vu_reset();
        vu_call(mapped+d->rva,a,regs,pattern,actual_pair);vu_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=vu_calls||memcmp(expected,vu_log,calls*sizeof(*expected))||
           memcmp(vu_after,vu_arena,VU_SIZE)||memcmp(after,actual,sizeof(after))){
            fprintf(stderr,"real mode=%u\n",mode);status=vu_report("real",n,pair,calls,expected);
        }
        for(unsigned j=0;j<=count;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("vehicle update nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("vehicle update nested uninstall");}
        vu_globals(before,1);++total;
    }
    vu_real=0;
    if(vu_patch(saved,active,0,0))return 1;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){
        fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, 31/a7 stack, pair, memory, globals and stub inputs\n",
        d->name,total,count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
static int vehicle_update_cases(unsigned index,unsigned limit){
    BYTE saved[VU_IDS][5],old_globals[VU_RNG+4];int active[VU_IDS];
    vu_globals(old_globals,0);if(vu_patch(saved,active,vu_direct_stubs(index),1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_VEHICLE_UPDATE_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];vu_build(index,n,a,r,0);status=vu_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=vu_installed_probes(index,64);
    if(vu_patch(saved,active,0,0))return 1;
    current_case_valid=0;
    if(!limit&&!status&&(index==ES2_HOOK_INDEX_vehicle_update||index==ES2_HOOK_INDEX_combat_rating||
        index==ES2_HOOK_INDEX_condition_record||index==ES2_HOOK_INDEX_weapon_tally))
        status=vu_real_pass(index);
    vu_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, payloads by content, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
