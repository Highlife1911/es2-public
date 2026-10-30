/* Sound API calls (docs/reverse-engineering/audio.md, "sound/clock isolation"):
 * sfx_play 462830, sfx_play_at 4628e0, sfx_place 46299c, sfx_refresh 46297c,
 * sfx_stop 462ac4, sfx_rate 463114.
 *
 * Direct cases run the original body and the core against logging stubs
 * patched over every game callee: rng_bounded 492f24 (answers a planned
 * draw in AX over a random high word, also out of 0..n-1 and negative),
 * the SFX manager 464618/464038/4646dc/464118/46466c, place 46299c (for
 * play_at and refresh; a planned AL), the view helpers 48c444/48c57c (the
 * latter writes a planned view-space position), dist3d_delta 47de72
 * (answers around the row's near and far distances) and angle_ratio
 * 47d32c; ordered calls with arguments and entry EAX/EDX/ECX (stack
 * addresses masked) and the context and gate words at each call. Rows,
 * row pointers, handles, the context and the gate words are random; the
 * index may carry 40000000h multiples (the scaled index wraps, the gate's
 * signed test does not). The stubs change what the original rereads after
 * them (the context, the gate words, the row pointer, the row's volume
 * bytes, the handle). Compared: EDX:EAX, the ECX at return (the adapter's
 * exit value), the call log, the arena, the globals (context and gate
 * words, both tables, RNG stream B) and the callee counters.
 *
 * Then installed 31/a7 poison probes through the exit shim (EAX/EDX/ECX),
 * and a real-callee pass: sequences of eight sound calls on one synthetic
 * SFX world (context, sample slots, cached file records, driver
 * structures, master volume, status, driver handle word) through the real
 * SFX manager, SOS glue and SOS stubs into recording fake driver slots,
 * the real gate 462784, product 47e08d and RNG stream B; the hook under
 * test original, then replaced, with rng_bounded, dist3d_delta,
 * angle_ratio and the other five sfx hooks in rotating modes. After every
 * call: EAX/EDX/ECX, the 114 bytes of stream B and the driver command
 * count; at the end the driver command log, the arena and the globals.
 *
 * The timeline hooks sfx_voice 463400, sfx_lip 4634b0 and sfx_lip_due
 * 4634a0 add two stubs (the clock wrapper 467828 with a planned clock and
 * 4634b0), a voice channel with its script and the event queue, and their
 * own real-callee pass through the real clock wrapper into a fake
 * GetTickCount (sx_voice_real_pass). Identifiers carry the prefix
 * sx_/SX_. */
typedef struct sx_record { uint32_t id,args[3],regs[3],state[3],eax,edx; } sx_record;
enum { SX_SIZE=0x2000, SX_ROWS=0x100, SX_CTX=0x600, SX_SLOTS=0x700, SX_RECS=0x1100, SX_SAMPLES=0x1980, SX_POS=0x1c00,
    SX_MAX=16, SX_IDS=13, SX_KINDS=9, SX_STEPS=8, SX_DRIVER_MAX=64,
    SX_CHAN=0x1d00, SX_SCRIPT=0x1e00 };
#define SX_BASE ((uint32_t)(uintptr_t)sx_arena)
static BYTE sx_arena[SX_SIZE],sx_before[SX_SIZE],sx_after[SX_SIZE];
static sx_record sx_log[SX_MAX];
static unsigned sx_calls;
static int sx_real;
static uint32_t sx_seed,sx_stream,sx_fi,sx_plan_draw,sx_plan_audible,sx_plan_dist,sx_plan_angle,sx_plan_clock;
__attribute__((used)) static uint32_t sx_ecx;
/* rng_bounded, 464618, 464038, 46299c, 48c444, 48c57c, dist3d_delta,
 * angle_ratio, 4646dc, 464118, 46466c. */
static const uint32_t sx_addresses[SX_IDS]={0x492f24,0x464618,0x464038,0x46299c,0x48c444,0x48c57c,0x47de72,0x47d32c,0x4646dc,
    0x464118,0x46466c,0x467828,0x4634b0};
static const unsigned sx_counts[SX_IDS]={2,3,2,2,1,2,3,2,3,2,3,0,1};
/* Per hook (play, play_at, place, refresh, stop, rate): the stubbed
 * callees and the argument count. */
static const unsigned sx_masks[SX_KINDS]={0x007,0x00d,0x1f2,0x008,0x200,0x400,0x1804,0x0800,0x1000};
static const unsigned sx_argc[SX_KINDS]={1,2,2,2,1,2,1,1,1};
static uint32_t *const sx_exit_ecx[SX_KINDS]={&sfx_play_ecx,&sfx_play_at_ecx,&sfx_place_ecx,&sfx_refresh_ecx,&sfx_stop_ecx,&sfx_rate_ecx,
    &sfx_voice_ecx,&sfx_lip_ecx,&sfx_lip_due_ecx};
static uint32_t *const sx_exit_valid[SX_KINDS]={&sfx_play_ecx_valid,&sfx_play_at_ecx_valid,&sfx_place_ecx_valid,
    &sfx_refresh_ecx_valid,&sfx_stop_ecx_valid,&sfx_rate_ecx_valid,&sfx_voice_ecx_valid,&sfx_lip_ecx_valid,&sfx_lip_due_ecx_valid};
static unsigned sx_kind(unsigned index){
    static const unsigned hooks[SX_KINDS]={ES2_HOOK_INDEX_sfx_play,ES2_HOOK_INDEX_sfx_play_at,ES2_HOOK_INDEX_sfx_place,
        ES2_HOOK_INDEX_sfx_refresh,ES2_HOOK_INDEX_sfx_stop,ES2_HOOK_INDEX_sfx_rate,ES2_HOOK_INDEX_sfx_voice,ES2_HOOK_INDEX_sfx_lip,
        ES2_HOOK_INDEX_sfx_lip_due};
    for(unsigned k=0;k<SX_KINDS;++k)if(hooks[k]==index)return k;
    return 0;
}
static uint32_t sx_next(void){
    uint32_t z=sx_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static int sx_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static uint32_t sx_mask(uint32_t value){return sx_stack(value)?0:value;}
static int32_t sx_s16(uint32_t x){x&=0xffffu;return x<0x8000u?(int32_t)x:(int32_t)x-0x10000;}
static BYTE *sx_row_bytes(uint32_t k){return sx_arena+SX_ROWS+16u*(k&63u);}
static uint32_t sx_row_address(uint32_t k){return SX_BASE+SX_ROWS+16u*(k&63u);}
/* The row a table entry points to (always one of the 64 arena rows). */
static BYTE *sx_row_of(uint32_t k){return (BYTE *)(uintptr_t)get32(mapped+0xd2b78+4u*(k&63u));}
/* Rereads of the original between the calls (not in the real pass). */
static void sx_touch(uint32_t pick){
    if(sx_real)return;
    if(pick%7==0)put32(mapped+0x9f8ec,pick>>8&1?0:sx_next()|1u);
    if(pick%5==0)put32(mapped+0x9f8f4,pick>>9&1?0:sx_next());
    if(pick%5==1)put32(mapped+0x9f8f8,pick>>10&1?0:sx_next());
    if(pick%11==0)put32(mapped+0xd2b78+4u*sx_fi,sx_row_address(sx_next()));
    if(pick%3==0){BYTE *p=sx_row_of(sx_fi);p[1]=(BYTE)sx_next();p[8]=(BYTE)sx_next();}
    if(pick%13==0)put32(mapped+0xd2c78+4u*sx_fi,sx_next());
    /* The voice channel and the event queue (count kept below 10, the
     * pointer at 0 on a record). */
    BYTE *ch=sx_arena+SX_CHAN;
    if(pick%17==0)put32(ch+4,pick>>14&1?sx_plan_clock+(sx_next()%5u)-2u:sx_next());
    if(pick%19==0)put16(ch+8,(uint16_t)(pick>>15&1?0:sx_next()));
    if(pick%23==0)put32(ch,pick>>16&1?0xffffffffu:sx_next());
    if(pick%29==0)mapped[0x9f966]=(BYTE)(pick>>17&1?0:sx_next());
    if(pick%31==0)put16(mapped+0x9f964,(uint16_t)(sx_next()%10u));
    if(pick%37==0)put32(mapped+0xd2ec8,0x4d2e76u+8u*(sx_next()%10u));
    if(pick%41==0)put32(mapped+0xd2e76+8u*(sx_next()%10u),sx_next());
}
/* The fake SOS driver of the real pass: every slot call with its
 * arguments and the registers it returns. */
typedef struct sx_command { uint32_t slot,args[3],eax,edx,ecx; } sx_command;
static sx_command sx_driver_log[SX_DRIVER_MAX];
static unsigned sx_driver_calls;
static uint32_t sx_driver_stream;
__attribute__((used,noinline)) static uint32_t __cdecl sx_driver_impl(unsigned slot,unsigned count,const uint32_t *a,uint32_t out[2]){
    if(sx_driver_calls==SX_DRIVER_MAX){fail("sfx synthetic driver overflow");ExitProcess(1);}
    sx_command *c=sx_driver_log+sx_driver_calls++;memset(c,0,sizeof(*c));c->slot=slot;
    for(unsigned j=0;j<count;++j)c->args[j]=sx_mask(a[j]);
    uint32_t z=sx_driver_stream+=0x7f4a7c15u;z=(z^(z>>15))*0x2c1b3c6du;z^=z>>12;
    /* StartSample (6cbe28) and the MIDI start (6cbe7c) write the sample
     * handle word through their last argument. */
    if(slot==0x6cbe28u)put16((BYTE *)(uintptr_t)a[2],(uint16_t)(z>>8));
    if(slot==0x6cbe7cu)put16((BYTE *)(uintptr_t)a[1],(uint16_t)(z>>8));
    c->eax=z&3u?0:z;c->edx=0xd0000000u|sx_driver_calls;c->ecx=0xc0000000u^z;
    out[0]=c->edx;out[1]=c->ecx;return c->eax;
}
/* stdcall with count dword arguments: EAX from the logger, EDX/ECX from
 * out[]. */
#define SX_DRIVER(slot,count) __attribute__((naked)) static void sx_driver_##slot(void){__asm__ volatile( \
    "subl $8,%esp\n\tmovl %esp,%eax\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tpushl $" #count "\n\tpushl $0x" #slot "\n\t" \
    "calll _sx_driver_impl\n\taddl $16,%esp\n\tmovl (%esp),%edx\n\tmovl 4(%esp),%ecx\n\taddl $8,%esp\n\tret $(" #count "*4)\n\t");}
SX_DRIVER(6cbe28,3) SX_DRIVER(6cbe2c,2) SX_DRIVER(6cbe68,1) SX_DRIVER(6cbe6c,1) SX_DRIVER(6cbe7c,2) SX_DRIVER(6cbe80,1)
SX_DRIVER(6cbdec,3) SX_DRIVER(6cbe00,3) SX_DRIVER(6cbe08,3) SX_DRIVER(6cbe98,1)
#undef SX_DRIVER
static const struct { uint32_t slot; void *fake; } sx_driver_slots[]={
    {0x6cbe28,sx_driver_6cbe28},{0x6cbe2c,sx_driver_6cbe2c},{0x6cbe68,sx_driver_6cbe68},{0x6cbe6c,sx_driver_6cbe6c},
    {0x6cbe7c,sx_driver_6cbe7c},{0x6cbe80,sx_driver_6cbe80},{0x6cbdec,sx_driver_6cbdec},{0x6cbe00,sx_driver_6cbe00},
    {0x6cbe08,sx_driver_6cbe08},{0x6cbe98,sx_driver_6cbe98}};
__attribute__((used,noinline)) static uint64_t __cdecl sx_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(sx_calls==SX_MAX){fail("sfx synthetic call overflow");ExitProcess(1);}
    sx_record *v=sx_log+sx_calls++;memset(v,0,sizeof(*v));v->id=id;
    for(unsigned j=0;j<sx_counts[id];++j)v->args[j]=sx_mask(a[j]);
    for(unsigned j=0;j<3;++j)v->regs[j]=sx_mask(regs[j]);
    v->state[0]=get32(mapped+0x9f8ec);v->state[1]=get32(mapped+0x9f8f4);v->state[2]=get32(mapped+0x9f8f8);
    uint32_t eax=sx_next(),edx=sx_next(),pick=sx_next();sx_ecx=sx_next();
    switch(id){
    case 0:eax=(eax&0xffff0000u)|(uint16_t)sx_plan_draw;break;
    case 3:eax=(eax&0xffffff00u)|(BYTE)sx_plan_audible;break;
    case 5:{BYTE *out=(BYTE *)(uintptr_t)a[1];for(unsigned j=0;j<3;++j)put32(out+4*j,sx_next());break;}
    case 6:eax=sx_plan_dist;break;
    case 7:eax=(eax&0xffff0000u)|(uint16_t)sx_plan_angle;break;
    case 11:eax=sx_plan_clock;break;
    }
    sx_touch(pick>>5);
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
/* Pushes the entry EAX/EDX/ECX for the record before the slot pointer. */
#define SX_STUB(n) __attribute__((naked)) static void sx_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _sx_stub_impl\n\taddl $24,%esp\n\tmovl _sx_ecx,%ecx\n\tret\n\t");}
SX_STUB(0) SX_STUB(1) SX_STUB(2) SX_STUB(3) SX_STUB(4) SX_STUB(5) SX_STUB(6) SX_STUB(7) SX_STUB(8) SX_STUB(9) SX_STUB(10)
SX_STUB(11) SX_STUB(12)
#undef SX_STUB
static void *sx_stubs[SX_IDS]={sx_stub_0,sx_stub_1,sx_stub_2,sx_stub_3,sx_stub_4,sx_stub_5,sx_stub_6,sx_stub_7,sx_stub_8,sx_stub_9,sx_stub_10,
    sx_stub_11,sx_stub_12};
/* Calls target with n argument slots, EAX/EDX/ECX/EBX from regs; with
 * pattern != 0 the 4 KiB below are filled first. EAX/EDX/ECX after RET. */
__attribute__((naked,noinline)) static void sx_invoke(void *target,const uint32_t *a,unsigned n,const uint32_t regs[4],uint32_t pattern,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "cmpl $0,24(%ebp)\n\tje 1f\n\tsubl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 24(%ebp),%eax\n\tmovl $1024,%ecx\n\tcld\n\trep stosl\n\taddl $4096,%esp\n\t"
        "1: movl 12(%ebp),%esi\n\tmovl 16(%ebp),%ecx\n\t"
        "2: testl %ecx,%ecx\n\tjz 3f\n\tdecl %ecx\n\tpushl (%esi,%ecx,4)\n\tjmp 2b\n\t"
        "3: movl 20(%ebp),%esi\n\tmovl (%esi),%eax\n\tmovl 4(%esi),%edx\n\tmovl 8(%esi),%ecx\n\tmovl 12(%esi),%ebx\n\tcall *8(%ebp)\n\t"
        "movl 28(%ebp),%ebx\n\tmovl %eax,(%ebx)\n\tmovl %edx,4(%ebx)\n\tmovl %ecx,8(%ebx)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* The context and gate words 49f8ec..49f8fb, the row pointers 4d2b78
 * and handles 4d2c78 (64 each), RNG stream B 4d260b; for the real pass
 * also the master volume 4a0e30, the status 4a0e50, the driver handle
 * word 6b566e and the 97 SOS slots 6cbd60..6cbee3. */
static const struct { uint32_t rva,size; } sx_ranges[]={{0x9f8ec,0x10},{0xd2b78,0x100},{0xd2c78,0x100},{0xd260b,0x72},
    {0xa0e30,4},{0xa0e50,4},{0x2b566e,2},{0x2cbd60,0x184},{0xd2e76,0x7a},{0x9f964,4},{0xd3b78,4},{0x2cc3e8,4}};
enum { SX_GLOBALS=0x10+0x100+0x100+0x72+4+4+2+0x184+0x7a+4+4+4 };
static void sx_globals(BYTE *out,int restore){
    for(unsigned i=0;i<sizeof(sx_ranges)/sizeof(sx_ranges[0]);++i){
        if(restore)memcpy(mapped+sx_ranges[i].rva,out,sx_ranges[i].size);else memcpy(out,mapped+sx_ranges[i].rva,sx_ranges[i].size);
        out+=sx_ranges[i].size;
    }
}
static int sx_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<SX_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
__attribute__((used)) static uint32_t sx_ticks;
static void sx_reset(void){sx_calls=0;sx_stream=sx_seed;sx_driver_calls=0;sx_driver_stream=sx_seed^0x5bd1e995u;sx_ticks=0;}
static uint32_t sx_pick(const uint32_t *values,unsigned count){return values[random32()%count];}
/* Stream B: 56 random words and two index bytes below 56. */
static void sx_stream_b(void){
    BYTE *s=mapped+0xd260b;random_bytes(s,112);s[112]=(BYTE)(random32()%56);s[113]=(BYTE)(random32()%56);
}
/* Rows: volume +1, near/far +4/+5, variants +6, percent +8, the rest
 * random. Table entries point to their own row or to another one. */
static void sx_rows(void){
    static const uint32_t volumes[]={0,1,80,100,255,0x40,0x7f},distances[]={0,1,2,5,10,100,255,0x80},
        variants[]={0,1,1,1,1,2,3,4,255,0x80},percents[]={0,1,50,100,255,0x99};
    for(uint32_t k=0;k<64;++k){
        BYTE *p=sx_row_bytes(k);random_bytes(p,16);
        p[1]=(BYTE)(random32()%4?sx_pick(volumes,7):random32());p[4]=(BYTE)sx_pick(distances,8);p[5]=(BYTE)sx_pick(distances,8);
        p[6]=(BYTE)sx_pick(variants,10);p[8]=(BYTE)(random32()%4?sx_pick(percents,6):random32());
        put32(mapped+0xd2b78+4u*k,sx_row_address(random32()%4?k:random32()));
        put32(mapped+0xd2c78+4u*k,random32());
    }
}
/* The index before the draw, the planned draw (also out of range and
 * negative) and the index after it (sx_fi, 0..63); the argument may carry
 * multiples of 40000000h. */
static uint32_t sx_index(unsigned n){
    static const uint32_t edges[]={0,9,10,11,12,63,1,2};
    uint32_t i0=n<64?edges[n%8]:random32()%64;
    uint32_t count=sx_row_of(i0)[6],draw=0;
    if(count>1){
        draw=random32()%count;
        switch(random32()%8){case 0:draw=0xffff;break;case 1:draw=0xfffe;break;case 2:draw=count;break;case 3:draw=random32();break;}
    }
    int32_t fi=(int32_t)i0+sx_s16(draw);
    if(fi<0||fi>63){draw=0;fi=(int32_t)i0;}
    sx_plan_draw=(random32()&0xffff0000u)|(uint16_t)draw;sx_fi=(uint32_t)fi;
    static const uint32_t wraps[]={0,0,0,0,0x40000000u,0x80000000u,0xc0000000u,0};
    return i0+wraps[n<64?(n/8)%8:random32()%8];
}
/* The distance around the near and far distances of the final row (no
 * 0/0 division: a planned distance 0 never meets a row with near = far =
 * 0). */
static void sx_distance(void){
    const BYTE *p=sx_row_of(sx_fi);int32_t inner=(int32_t)p[4]<<10,outer=(int32_t)p[5]<<10;
    const uint32_t picks[]={(uint32_t)inner-1u,(uint32_t)inner,(uint32_t)inner+1u,(uint32_t)outer-1u,(uint32_t)outer,(uint32_t)outer+1u,
        0,0xffffffffu,0x80000000u,0x7fffffffu,random32()%((uint32_t)outer+2048u),random32()};
    sx_plan_dist=picks[random32()%12];
    if(!sx_plan_dist)for(uint32_t k=0;k<64;++k){BYTE *q=sx_row_bytes(k);if(!q[4]&&!q[5])q[5]=1;}
}
/* The voice channel at SX_CHAN with its script at SX_SCRIPT (128 bytes,
 * lip values FFh now and then, the last 16 bytes all FFh so every step
 * loop ends inside), the event queue (count 0..9, ten record pointers,
 * record times also small enough to meet the step comparison), the voice
 * byte and the planned clock around the event time. */
static void sx_voice_build(void){
    BYTE *ch=sx_arena+SX_CHAN,*script=sx_arena+SX_SCRIPT;
    random_bytes(script,0x80);for(unsigned j=0;j<0x70;++j)if(random32()%16==0)script[j]=0xff;
    memset(script+0x70,0xff,0x10);
    put32(ch+0xa,SX_BASE+SX_SCRIPT+random32()%0x40);put32(ch+0x2f,SX_BASE+SX_SCRIPT+random32()%0x40);
    uint32_t t=random32()%4?random32()%0x10000u:random32();put32(ch+4,t);
    put16(ch+8,(uint16_t)(random32()%4?random32():0));
    put32(ch,random32()%4?random32():0xffffffffu);
    put16(mapped+0x9f964,(uint16_t)(random32()%10));mapped[0x9f966]=(BYTE)(random32()%3?random32()|1u:0);
    static const uint32_t times[]={0,1,0x7f,0x80,0xffffff80u,0x14};
    for(unsigned k=0;k<10;++k){
        put32(mapped+0xd2ec8+4u*k,0x4d2e76u+8u*(random32()%10));
        put32(mapped+0xd2e76+8u*k,random32()%3?sx_pick(times,6):random32());put32(mapped+0xd2e7a+8u*k,SX_BASE+SX_CHAN);
    }
    const uint32_t clocks[]={t-1u,t,t+1u,t+random32()%0x200,random32(),0,0xffffffffu};
    sx_plan_clock=clocks[random32()%7];
}
static void sx_build(unsigned kind,unsigned n,uint32_t a[2],uint32_t r[3]){
    sx_seed=random32();random_bytes(sx_arena,SX_SIZE);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    sx_rows();sx_stream_b();
    put32(mapped+0x9f8ec,random32()%8?random32()|1u:0);put32(mapped+0x9f8f0,random32());
    put32(mapped+0x9f8f4,random32()%2?random32():0);put32(mapped+0x9f8f8,random32()%2?random32():0);
    static const uint32_t angles[]={0,0x7fff,0x8000,0xffff,0x4000,0xc000};
    sx_plan_angle=random32()%2?sx_pick(angles,6):random32();
    sx_plan_audible=random32()%3?random32()%2:random32();
    a[0]=sx_index(n);
    if(kind==2||kind==3){sx_fi=a[0]&63u;sx_plan_draw=0;} /* place and refresh draw nothing */
    if(kind>=4)sx_fi=a[0]&63u;
    sx_distance();
    a[1]=kind==5?random32():SX_BASE+SX_POS+4u*(random32()%16);
    sx_voice_build();
    if(kind>=6)a[0]=SX_BASE+SX_CHAN;
}
static int sx_report(const char *what,unsigned n,const uint32_t want[3],const uint32_t got[3],unsigned calls,const sx_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x calls %u/%u\n",current_function,n,what,
        want[0],got[0],want[1],got[1],want[2],got[2],calls,sx_calls);
    for(unsigned j=0;j<calls&&j<sx_calls&&expected;++j)if(memcmp(expected+j,sx_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(sx_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<SX_SIZE;++j)if(sx_after[j]!=sx_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,sx_after[j],sx_arena[j]);break;}
    return 1;
}
static unsigned sx_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<sx_calls;++j)if(c->event==ES2_COLLIDE_CALL&&sx_addresses[sx_log[j].id]==c->key)++hits;
    return hits;
}
static int sx_compare(unsigned index,unsigned kind,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    static BYTE before[SX_GLOBALS],after[SX_GLOBALS],actual[SX_GLOBALS];static sx_record expected[SX_MAX];
    uint32_t regs[4]={r[0],r[1],r[2],0},want[3],got[3];
    memcpy(sx_before,sx_arena,SX_SIZE);sx_globals(before,0);
    sx_reset();sx_invoke(mapped+d->rva,a,sx_argc[kind],regs,0,want);unsigned calls=sx_calls;memcpy(expected,sx_log,calls*sizeof(*expected));
    memcpy(sx_after,sx_arena,SX_SIZE);sx_globals(after,0);
    memcpy(sx_arena,sx_before,SX_SIZE);sx_globals(before,1);sx_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];
    for(unsigned i=0;i<count&&i<8;++i)counts[i]=c[i].count;
    *sx_exit_valid[kind]=0;
    uint64_t pair=x->replacement_context(a,d->user,r);sx_globals(actual,0);
    got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);got[2]=*sx_exit_ecx[kind];
    if(*sx_exit_valid[kind]!=1)return fail("sfx adapter did not hand out its ECX");
    *sx_exit_valid[kind]=0;
    if(n<4096)for(unsigned i=0;i<count&&i<8;++i)if(c[i].count-counts[i]!=sx_expected_hits(c+i))return fail("sfx callee counter delta");
    if(memcmp(want,got,12)||calls!=sx_calls||memcmp(expected,sx_log,calls*sizeof(*expected))||memcmp(sx_after,sx_arena,SX_SIZE)||
       sx_globals_differ(after,actual))
        return sx_report("direct",n,want,got,calls,expected);
    sx_globals(before,1);
    return 0;
}
static int sx_patch(BYTE saved[SX_IDS][5],int active[SX_IDS],unsigned which,int on){
    for(unsigned i=0;i<SX_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(sx_addresses[i]-0x400000u,sx_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(sx_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
static int sx_installed_probes(unsigned index,unsigned kind,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    static BYTE before[SX_GLOBALS],after[SX_GLOBALS],actual[SX_GLOBALS];static sx_record expected[SX_MAX];
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[2],r[3],want[3],got[3],pattern=poison?0xa7a7a7a7u:0x31313131u,regs[4]={pattern,pattern,pattern,pattern};
        sx_build(kind,n,a,r);
        /* Entry registers matter without a context: the poison and a
         * second set. */
        if(n&1){regs[0]=r[0];regs[1]=r[1];regs[2]=r[2];}
        BYTE expected_fpu[108],actual_fpu[108];
        memcpy(sx_before,sx_arena,SX_SIZE);sx_globals(before,0);
        sx_reset();sx_invoke(mapped+d->rva,a,sx_argc[kind],regs,pattern,want);es2_hook_fpu_save(expected_fpu);
        unsigned calls=sx_calls;memcpy(expected,sx_log,calls*sizeof(*expected));memcpy(sx_after,sx_arena,SX_SIZE);sx_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("sfx poison install");
        memcpy(sx_arena,sx_before,SX_SIZE);sx_globals(before,1);sx_reset();
        sx_invoke(mapped+d->rva,a,sx_argc[kind],regs,pattern,got);es2_hook_fpu_save(actual_fpu);sx_globals(actual,0);
        if(memcmp(want,got,12)||calls!=sx_calls||memcmp(expected,sx_log,calls*sizeof(*expected))||memcmp(sx_after,sx_arena,SX_SIZE)||
           sx_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1||
           *sx_exit_valid[kind])
            status=sx_report(fast?"installed fast":"installed preserving",n,want,got,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("sfx poison uninstall");sx_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, EAX/EDX/ECX through the exit shim, trace/arena/globals\n",d->name,total);
    return status;
}
/* The synthetic SFX world of the real pass: the context (records +34h,
 * slots +38h, playing counts +3Ch/+40h), 64 sample slots (handle,
 * payload: file handle, flags without the stream bit 1000h, callback,
 * volume, loops, pan, rate, driver word), four cached file records with
 * their driver sample structures, row handles that are valid, -1, of an
 * old generation or random (slot numbers stay below 64), the driver
 * handle word, master volume and status, and the fake driver slots. */
static void sx_world(void){
    uint32_t b=SX_BASE,ctx=b+SX_CTX;
    put32(sx_arena+SX_CTX+0x34,b+SX_RECS);put32(sx_arena+SX_CTX+0x38,b+SX_SLOTS);
    static const uint32_t flags[]={0,0x100,0x101,1,0x180,0x500,0x900,0x1ff,0x2100,0x100};
    for(uint32_t s=0;s<64;++s){
        BYTE *slot=sx_arena+SX_SLOTS+40u*s;uint32_t handle=(random32()&0x7fff0000u)|s;
        put32(slot,handle);
        uint32_t r=random32()%4,file=(uint32_t)(r+1u)<<16|r;
        put32(slot+4,random32()%8?file:random32()%2?0xffffffffu:(random32()&0xffff0000u)|r);
        put32(slot+8,sx_pick(flags,10)&~0x1000u);
        uint32_t k=random32()%8;
        put32(mapped+0xd2c78+4u*s,k<5?handle:k==5?0xffffffffu:k==6?(handle^0x10000u):(random32()&0xffff0000u)|(random32()%64));
    }
    /* At most four variants and indices below 60: the real draw keeps
     * the final index inside the 64-entry tables. */
    for(uint32_t k=0;k<64;++k)sx_row_bytes(k)[6]%=5u;
    for(uint32_t r=0;r<4;++r){
        BYTE *rec=sx_arena+SX_RECS+540u*r;put32(rec,(r+1u)<<16|r);
        put32(rec+4+0x208,random32()|1u);put32(rec+4+0x210,b+SX_SAMPLES+0x80u*r);
    }
    put32(mapped+0x9f8ec,random32()%8?ctx:0);
    put16(mapped+0x2b566e,(uint16_t)random32());put32(mapped+0xa0e30,random32()%101);put32(mapped+0xa0e50,random32()%4?0:random32());
    memset(mapped+0x2cbd60,0,0x184);
    for(unsigned i=0;i<sizeof(sx_driver_slots)/sizeof(sx_driver_slots[0]);++i)
        put32(mapped+sx_driver_slots[i].slot-0x400000u,(uint32_t)(uintptr_t)sx_driver_slots[i].fake);
}
/* One planned call of a sequence: the entry, its arguments, registers. */
typedef struct sx_step { unsigned kind; uint32_t a[2],r[4]; } sx_step;
static const uint32_t sx_rvas[SX_KINDS+1]={0x62830,0x628e0,0x6299c,0x6297c,0x62ac4,0x63114,0x63400,0x634b0,0x634a0,0x632c4};
static void sx_plan(sx_step *s,unsigned kind,unsigned k){
    s->kind=random32()%2?kind:random32()%6u;
    uint32_t i=random32()%60;
    static const uint32_t wraps[]={0,0,0,0,0,0x40000000u,0x80000000u,0xc0000000u};
    s->a[0]=i+wraps[random32()%8];
    s->a[1]=s->kind==5?random32():SX_BASE+SX_POS+4u*(random32()%16);
    for(unsigned j=0;j<3;++j)s->r[j]=random32();
    s->r[3]=k;
}
/* Real callees: sequences of SX_STEPS sound calls on one world, the hook
 * under test original, then replaced; the other sfx hooks and
 * rng_bounded, dist3d_delta, angle_ratio in rotating modes (compare falls
 * back to replaced where refused). 48c444/48c57c stay logging stubs (the
 * 3D view library, not sound). */
static int sx_real_pass(unsigned index,unsigned kind){
    const unsigned nested[]={ES2_HOOK_INDEX_rng_bounded,ES2_HOOK_INDEX_dist3d_delta,ES2_HOOK_INDEX_angle_ratio,
        ES2_HOOK_INDEX_sfx_play,ES2_HOOK_INDEX_sfx_play_at,ES2_HOOK_INDEX_sfx_place,ES2_HOOK_INDEX_sfx_refresh,
        ES2_HOOK_INDEX_sfx_stop,ES2_HOOK_INDEX_sfx_rate};
    enum { NESTED=sizeof(nested)/sizeof(nested[0]) };
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0,sounds=0,draws=0;uint64_t entered[NESTED]={0};
    BYTE saved[SX_IDS][5];int active[SX_IDS];
    static BYTE before[SX_GLOBALS],after[SX_GLOBALS],actual[SX_GLOBALS],stream[SX_STEPS][0x72];
    static sx_command commands[SX_DRIVER_MAX];
    uint32_t results[SX_STEPS][3];unsigned driver_counts[SX_STEPS];
    if(sx_patch(saved,active,1u<<4|1u<<5,1))return 1;
    sx_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<128&&!status;++n){
        uint32_t ignored_a[2],ignored_r[3];sx_build(kind,n,ignored_a,ignored_r);sx_world();
        sx_step steps[SX_STEPS];for(unsigned k=0;k<SX_STEPS;++k)sx_plan(steps+k,kind,k);
        uint32_t pattern=n&1?0xa7a7a7a7u:0x31313131u;
        memcpy(sx_before,sx_arena,SX_SIZE);sx_globals(before,0);
        for(unsigned phase=0;phase<2&&!status;++phase){
            es2_hook hk[NESTED+1];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
            for(unsigned j=0;j<NESTED;++j){
                if(nested[j]==index)continue;
                o.mode=(es2_hook_mode)((mode+j+n)%3);
                const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
                if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("sfx nested install");
            }
            o.mode=phase?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+NESTED,d,&o,function_hook_extension(index)))return fail("sfx caller install");
            memcpy(sx_arena,sx_before,SX_SIZE);sx_globals(before,1);sx_reset();
            uint64_t calls_before=hk[NESTED].calls;
            for(unsigned k=0;k<SX_STEPS&&!status;++k){
                const sx_step *s=steps+k;uint32_t out[3];
                current_case=n*SX_STEPS+k;
                sx_invoke(mapped+sx_rvas[s->kind],s->a,sx_argc[s->kind],s->r,pattern,out);
                if(!phase){memcpy(results[k],out,12);memcpy(stream[k],mapped+0xd260b,0x72);driver_counts[k]=sx_driver_calls;}
                else if(memcmp(results[k],out,12)||memcmp(stream[k],mapped+0xd260b,0x72)||driver_counts[k]!=sx_driver_calls){
                    fprintf(stderr,"FAIL: %s case %u (real mode=%u step %u kind %u) eax %08x/%08x edx %08x/%08x ecx %08x/%08x stream-B %s driver %u/%u\n",
                        current_function,current_case,mode,k,s->kind,results[k][0],out[0],results[k][1],out[1],results[k][2],out[2],
                        memcmp(stream[k],mapped+0xd260b,0x72)?"differs":"same",driver_counts[k],sx_driver_calls);
                    status=1;
                }
            }
            if(!phase){memcpy(sx_after,sx_arena,SX_SIZE);sx_globals(after,0);memcpy(commands,sx_driver_log,sizeof(commands));}
            else {
                sx_globals(actual,0);
                if(!status&&(memcmp(commands,sx_driver_log,sx_driver_calls*sizeof(*commands))||memcmp(sx_after,sx_arena,SX_SIZE)||
                   sx_globals_differ(after,actual))){
                    fprintf(stderr,"real mode=%u\n",mode);sx_calls=0;status=sx_report("real",n,results[SX_STEPS-1],results[SX_STEPS-1],0,NULL);
                }
                unsigned own=0;for(unsigned k=0;k<SX_STEPS;++k)own+=steps[k].kind==kind;
                if(!status&&hk[NESTED].calls-calls_before<own)status=fail("sfx hook under test not entered");
                sounds+=sx_driver_calls;draws+=memcmp(mapped+0xd260b,before+0x10+0x100+0x100,0x72)!=0;
            }
            for(unsigned j=0;j<=NESTED;++j){if(j<NESTED)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("sfx nested compare mismatch");
                if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("sfx nested uninstall");}
        }
        sx_globals(before,1);++total;
    }
    sx_real=0;
    if(sx_patch(saved,active,0,0))return 1;
    for(unsigned j=0;j<NESTED&&!status;++j)if(nested[j]!=index&&!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&(!sounds||!draws))status=fail("sfx real pass reached no driver command or no stream-B draw");
    if(!status)printf("PASS: %s %u real sequences of %u calls, %u driver commands, %u with stream-B draws; hook original vs replaced, nested hooks rotating\n",
        d->name,total,SX_STEPS,sounds,draws);
    return status;
}
/* The fake GetTickCount behind the real clock wrapper 467828 in the
 * voice pass (stdcall, no arguments): base + 16 per call, EDX = the call
 * count, ECX derived; the wrapper turns it into clock t0 + calls. */
__attribute__((used)) static uint32_t sx_tick_base;
__attribute__((naked)) static void sx_tick_fake(void){__asm__ volatile(
    "incl _sx_ticks\n\tmovl _sx_ticks,%edx\n\tmovl %edx,%eax\n\tshll $4,%eax\n\taddl _sx_tick_base,%eax\n\t"
    "movl %eax,%ecx\n\txorl $0x5a5a,%ecx\n\tret\n\t");}
/* The voice world: the SFX world, three channels with scripts (lip
 * values, mostly small time steps, a 32-byte FFh tail), the queue with
 * two or fewer events on records of these channels, the voice byte, the
 * fake clock in the import slot 6cc3e8 and the base 4d3b78. */
static void sx_voice_world(void){
    sx_world();
    put32(mapped+0x2cc3e8,(uint32_t)(uintptr_t)sx_tick_fake);
    sx_tick_base=random32();const uint32_t t0=random32()%0x10000u+100u;
    put32(mapped+0xd3b78,sx_tick_base-16u*t0);
    for(unsigned c=0;c<3;++c){
        BYTE *ch=sx_arena+SX_CHAN+0x48u*c,*script=sx_arena+SX_SCRIPT+0x60u*c;
        random_bytes(ch,0x48);random_bytes(script,0x40);
        for(unsigned j=0;j<0x40;j+=2)if(random32()%12==0)script[j]=0xff;
        for(unsigned j=1;j<0x40;j+=2)script[j]=(BYTE)(random32()%8?random32()%16:random32()|0xf0u);
        memset(script+0x40,0xff,0x20);
        put32(ch+0x2f,SX_BASE+SX_SCRIPT+0x60u*c);put32(ch+0xa,SX_BASE+SX_SCRIPT+0x60u*c+random32()%0x20);
        put32(ch+4,t0+random32()%64-32u);put16(ch+8,(uint16_t)random32());
        put32(ch,random32()%4?get32(mapped+0xd2c78+4u*(random32()%64)):0xffffffffu);
    }
    put16(mapped+0x9f964,(uint16_t)(random32()%3));mapped[0x9f966]=(BYTE)(random32()%4?1:0);
    for(unsigned k=0;k<10;++k){
        put32(mapped+0xd2ec8+4u*k,0x4d2e76u+8u*(random32()%10));
        put32(mapped+0xd2e76+8u*k,t0+random32()%64-16u);put32(mapped+0xd2e7a+8u*k,SX_BASE+SX_CHAN+0x48u*(random32()%3));
    }
}
/* Real callees of the timeline: sequences of SX_STEPS calls of voice,
 * lip, lip_due and the per-frame queue pass 4632c4 (original; it cannot
 * be hooked: three bytes before its first call) on three channels, through
 * the real clock wrapper 467828 into the fake GetTickCount and the real
 * SFX manager, glue and SOS stubs into the fake driver; the hook under
 * test original, then replaced, the other two timeline hooks rotating.
 * After every call: EAX/EDX/ECX, the number of clock reads, the queue
 * (records, pointers, count) and the three channels. */
static int sx_voice_real_pass(unsigned index,unsigned kind){
    const unsigned nested[]={ES2_HOOK_INDEX_sfx_voice,ES2_HOOK_INDEX_sfx_lip,ES2_HOOK_INDEX_sfx_lip_due};
    enum { NESTED=sizeof(nested)/sizeof(nested[0]), SX_STATE=0x7a+4+0xd8 };
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0,reads=0,plays=0;uint64_t entered[NESTED]={0};
    static BYTE before[SX_GLOBALS],after[SX_GLOBALS],actual[SX_GLOBALS],state[SX_STEPS][SX_STATE],now[SX_STATE];
    static sx_command commands[SX_DRIVER_MAX];
    uint32_t results[SX_STEPS][3];unsigned ticks[SX_STEPS];
    sx_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<128&&!status;++n){
        uint32_t ignored_a[2],ignored_r[3];sx_build(kind,n,ignored_a,ignored_r);sx_voice_world();
        sx_step steps[SX_STEPS];
        for(unsigned k=0;k<SX_STEPS;++k){
            sx_step *s=steps+k;s->kind=random32()%2?kind:6u+random32()%4u;
            s->a[0]=SX_BASE+SX_CHAN+0x48u*(random32()%3);s->a[1]=0;for(unsigned j=0;j<3;++j)s->r[j]=random32();s->r[3]=k;
        }
        uint32_t pattern=n&1?0xa7a7a7a7u:0x31313131u;
        memcpy(sx_before,sx_arena,SX_SIZE);sx_globals(before,0);
        for(unsigned phase=0;phase<2&&!status;++phase){
            es2_hook hk[NESTED+1];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
            for(unsigned j=0;j<NESTED;++j){
                if(nested[j]==index)continue;
                o.mode=(mode+j+n)%2?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
                if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,function_hook_extension(nested[j])))return fail("sfx timeline nested install");
            }
            o.mode=phase?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+NESTED,d,&o,function_hook_extension(index)))return fail("sfx timeline caller install");
            memcpy(sx_arena,sx_before,SX_SIZE);sx_globals(before,1);sx_reset();
            for(unsigned k=0;k<SX_STEPS&&!status;++k){
                const sx_step *s=steps+k;uint32_t out[3];
                current_case=n*SX_STEPS+k;
                sx_invoke(mapped+sx_rvas[s->kind],s->a,s->kind==9?0:1,s->r,pattern,out);
                memcpy(now,mapped+0xd2e76,0x7a);memcpy(now+0x7a,mapped+0x9f964,4);memcpy(now+0x7e,sx_arena+SX_CHAN,0xd8);
                if(!phase){memcpy(results[k],out,12);ticks[k]=sx_ticks;memcpy(state[k],now,SX_STATE);}
                else if(memcmp(results[k],out,12)||ticks[k]!=sx_ticks||memcmp(state[k],now,SX_STATE)){
                    fprintf(stderr,"FAIL: %s case %u (timeline mode=%u step %u kind %u) eax %08x/%08x edx %08x/%08x ecx %08x/%08x clock reads %u/%u state %s\n",
                        current_function,current_case,mode,k,s->kind,results[k][0],out[0],results[k][1],out[1],results[k][2],out[2],
                        ticks[k],sx_ticks,memcmp(state[k],now,SX_STATE)?"differs":"same");
                    status=1;
                }
            }
            if(!phase){memcpy(sx_after,sx_arena,SX_SIZE);sx_globals(after,0);memcpy(commands,sx_driver_log,sizeof(commands));}
            else {
                sx_globals(actual,0);
                if(!status&&(memcmp(commands,sx_driver_log,sx_driver_calls*sizeof(*commands))||memcmp(sx_after,sx_arena,SX_SIZE)||
                   sx_globals_differ(after,actual))){
                    fprintf(stderr,"timeline mode=%u\n",mode);sx_calls=0;status=sx_report("timeline",n,results[SX_STEPS-1],results[SX_STEPS-1],0,NULL);
                }
                unsigned own=0;for(unsigned k=0;k<SX_STEPS;++k)own+=steps[k].kind==kind;
                if(!status&&hk[NESTED].calls<own)status=fail("sfx timeline hook under test not entered");
                reads+=sx_ticks;plays+=sx_driver_calls;
            }
            for(unsigned j=0;j<=NESTED;++j){if(j<NESTED)entered[j]+=hk[j].calls;
                if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("sfx timeline nested uninstall");}
        }
        sx_globals(before,1);++total;
    }
    sx_real=0;
    for(unsigned j=0;j<NESTED&&!status;++j)if(nested[j]!=index&&!entered[j]){fprintf(stderr,"FAIL: %s timeline callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&(!reads||!plays))status=fail("sfx timeline pass read no clock or played no voice");
    if(!status)printf("PASS: %s %u timeline sequences of %u calls, %u clock reads, %u driver commands; hook original vs replaced, nested timeline hooks rotating\n",
        d->name,total,SX_STEPS,reads,plays);
    return status;
}
static int sfx_cases(unsigned index,unsigned limit){
    const unsigned kind=sx_kind(index);
    BYTE saved[SX_IDS][5];int active[SX_IDS];static BYTE old_globals[SX_GLOBALS];static BYTE old_arena[SX_SIZE];
    sx_globals(old_globals,0);memcpy(old_arena,sx_arena,SX_SIZE);
    if(sx_patch(saved,active,sx_masks[kind],1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_SFX_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[2],r[3];sx_build(kind,n,a,r);status=sx_compare(index,kind,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=sx_installed_probes(index,kind,64);
    if(sx_patch(saved,active,sx_masks[kind],0))return 1;
    current_case_valid=0;
    if(!limit&&!status){current_case_valid=1;status=kind<6?sx_real_pass(index,kind):sx_voice_real_pass(index,kind);current_case_valid=0;}
    sx_globals(old_globals,1);memcpy(sx_arena,old_arena,SX_SIZE);
    if(!status)printf("PASS: %-20s %u cases; EDX:EAX and the ECX at return, ordered calls with entry registers, arena/globals incl. stream B\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
