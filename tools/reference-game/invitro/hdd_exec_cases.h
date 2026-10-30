/* hdd_exec group: the HDD button executor 44a1a0 (hdd_execute)
 * (docs/reverse-engineering/cockpit.md, "HDD button executor"). An 8 KiB arena
 * holds the HDD gauge H (command +528h, the gate byte +76h, page +109h, page
 * object +101h, page +105h, cockpit +14h, the selected wingman +517h, the lamp
 * byte +51Fh, the rectangle +F1h..+100h, the wingman +524h, the mark bytes
 * +B0h/+10Eh..+110h), two map objects M/M2 (order word +5Dh/+81h, the order
 * records +85h+2Ah*e for e = -1..2, the bytes +103h/+104h/+144h/+380h/+381h,
 * the wingman index +105h, +15Ch), three wingmen (bytes +99h, +A3h), two
 * graphics contexts X1/X2 and the map holder. The image globals 6c5f70 (the
 * context), the lamp sounds 4d3b96/4d3b9a, the wingman table 4d03c8 with the
 * order record 4d03d4 and the holder 49d6b0 are set per case, compared and
 * restored. Per case the command (-1, 0..14, above, random), the gate byte, the
 * page (0, 1, 2, random), the selected wingman (-1..2, random), the table
 * entries (a wingman or 0) and their dead and lamp bytes, the lamp byte, the
 * sounds (also 0), the bytes +103h/+104h and the context vary.
 *
 * Direct cases run the original body and the adapter against logging
 * stubs for its 20 callees (patched in the private image). A record keeps
 * the callee, the bridge's slot count, the arguments and the entry
 * EAX/EDX/ECX, the locals the original has written by then ([f-10h] and
 * [f-8], for the clip and the draw also [f-4], for the draw the rectangle
 * copy f-20h..f-14h), and for X1 and X2 whether [X+22Ch] holds f-8 and the
 * record {[p], [p+4]} behind it. The wingman getters 44ba08/44b9b0 return
 * a wingman or 0; the other stubs return values from a per-case stream and
 * in about one call of four change what the original rereads (the
 * command, the page, the selected wingman, a table entry, a wingman's
 * bytes, the lamp byte, the bytes +103h/+104h, the page object [H+101h],
 * the order word, the context pointer, the cockpit, the sounds, the
 * rectangle, M's wingman index or an order record). Frame-valued registers
 * and arguments (f-8, f-20h) are compared relative to the run's frame: the
 * original's EBP (from the stubs), the adapter's plain scratch frame
 * (hdd_exec_plain_frame) or, installed, the slot address - 8. The full
 * EDX:EAX pair, the call log, the arena and the globals are compared, the
 * callee counters for the first 4,096 cases. The jump table 44a1f7 is
 * redirected through counting trampolines for the direct pass, and every
 * entry must be taken.
 *
 * Installed probes run the hook through its entry shim (fast and
 * preserving dispatch, 31/a7 stack, EAX..EDI distinct patterns): the stubs
 * also record a hash of the 2Ch bytes below f (the locals, the residue
 * word f-0Ch and the saved EBX/ESI/EDI) and, when 44a1a0 calls them
 * itself, EBX/ESI/EDI, EBP and ESP relative to the caller's slot and a
 * hash of the 256 bytes below their ESP, and
 * one-slot stubs overwrite their argument slot in one call of four. The
 * real-callee chain runs 44a1a0 over the real 44db9c, 44db4c, order_cancel
 * 44dc10, wingman_select 44da98 and map_mode 44d9f4 (their callees 433e3c,
 * 44dc6c, 44dd14, 44ede0, 44ee00 and 44ee24 stubbed), the four chain hooks
 * hdd_execute, order_cancel, wingman_select and map_mode uninstalled or
 * replaced at random per case, against the all-uninstalled run on the same
 * filled stack. Identifiers: hxc_/HXC_. */
enum { HXC_SIZE=0x2000, HXC_H=0x100, HXC_M=0x800, HXC_M2=0xc00, HXC_W=0x1000, HXC_ACT=0x1100, HXC_X1=0x1400, HXC_X2=0x1700,
    HXC_HOLD=0x1a00, HXC_IDS=26, HXC_MAX=48, HXC_DIRECT=20, HXC_MAIN=(1u<<20)-1u,
    HXC_REAL=((1u<<26)-1u)&~(1u<<15|1u<<16|1u<<18|1u<<20|1u<<21), HXC_TABLE=15 };
#define HXC_BASE ((uint32_t)(uintptr_t)hxc_arena)
#define HXC_ADDR(o) (HXC_BASE+(uint32_t)(o))
static BYTE hxc_arena[HXC_SIZE],hxc_before[HXC_SIZE],hxc_after[HXC_SIZE];
typedef struct hxc_record { uint32_t id,slots,a[3],regs[3],cs[3],frame[2],window,locals,loc[7],ctx[2][3]; } hxc_record;
static hxc_record hxc_log[HXC_MAX];
static unsigned hxc_calls,hxc_track,hxc_clobbers,hxc_paths[HXC_IDS];
/* hxc_run: 0 the original (frame = the stubs' EBP), 1 the adapter's plain
 * path (hdd_exec_plain_frame), 2 installed (slot address - 8). */
static unsigned hxc_run;
/* Set in the real pass while one of the hdd_orders hooks (order_cancel,
 * wingman_select, map_mode) is replaced: they do not reproduce ECX (dead after
 * each of their call sites, cockpit.md, "HDD map order controls"), so the ECX
 * of the callees below them (ids from HXC_DIRECT) is not compared then. */
static unsigned hxc_nested_ecx;
static uint32_t hxc_seed,hxc_stream;
/* Captured by the stubs at entry: EAX, EDX, ECX, EBX, ESI, EDI, EBP, ESP;
 * the 256 bytes below the entry ESP; the ECX a stub returns. */
__attribute__((used)) static uint32_t hxc_cap[8],hxc_ecx,hxc_origin;
__attribute__((used)) static BYTE hxc_window[256];
static const uint32_t hxc_addr[HXC_IDS]={0x44a60c,0x44d0c0,0x450bf4,0x44d124,0x450c18,0x44cff8,0x44ba08,0x44d05c,0x44b9b0,
    0x486704,0x43122c,0x44cf90,0x44cfc4,0x44a748,0x433e3c,0x44db9c,0x44db4c,0x431610,0x44dc10,0x44dc6c,
    0x44da98,0x44d9f4,0x44ede0,0x44ee00,0x44ee24,0x44dd14};
static const unsigned hxc_arity[HXC_IDS]={2,1,1,1,1,1,1,1,1,1,3,1,1,2,1,1,2,3,1,3,2,2,2,1,1,1};
static uint32_t hxc_next(void){
    uint32_t z=hxc_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static uint32_t hxc_value(void){return stack_clean(hxc_next());}
static void hxc_reset(void){hxc_calls=0;hxc_stream=hxc_seed;}
enum { HXC_GLOBALS=4+6+0x22+4 };
static void hxc_globals(BYTE *out,int restore){
    static const struct { uint32_t rva,size; } r[]={{0x2c5f70,4},{0xd3b96,6},{0xd03c8,0x22},{0x9d6b0,4}};
    BYTE *p=out;for(unsigned i=0;i<4;++i){if(restore)memcpy(mapped+r[i].rva,p,r[i].size);else memcpy(p,mapped+r[i].rva,r[i].size);p+=r[i].size;}
}
static uint32_t hxc_actor(void){return HXC_ADDR(HXC_ACT+0x100*(hxc_next()%3));}
static uint32_t hxc_wingman(void){return hxc_next()%4?hxc_actor():0u;}
static uint32_t hxc_selected(void){return hxc_next()%8==0?hxc_value():hxc_next()%4?hxc_next()%3:0xffffffffu;}
static uint32_t hxc_page(void){switch(hxc_next()%8){case 0:return 2;case 1:return hxc_value();case 2:case 3:case 4:return 1;default:return 0;}}
static uint32_t hxc_index(void){return hxc_next()%4?hxc_next()%3:0xffffffffu;}
static uint32_t hxc_frame_base(void){
    return hxc_run==0?hxc_cap[6]:hxc_run==1?hdd_exec_plain_frame:hxc_origin-8u;
}
/* A frame-valued word relative to the run's frame. */
static uint32_t hxc_alias_value,hxc_alias_call;static int hxc_alias_hit;
static uint32_t hxc_rel(uint32_t v,uint32_t f){
    if(!hxc_alias_hit&&stack_uncommitted(v)){hxc_alias_hit=1;hxc_alias_value=v;hxc_alias_call=hxc_calls-1u;}
    if(v==f-8u)return 0xffff0008u;
    if(v==f-0x20u)return 0xffff0020u;
    return v;
}
static uint32_t hxc_word(uint32_t a){return get32((const BYTE *)(uintptr_t)a);}
static uint32_t hxc_hash(const BYTE *p,unsigned n){uint32_t h=2166136261u;for(unsigned i=0;i<n;++i)h=(h^p[i])*16777619u;return h;}
/* One map object (M or M2). */
static void hxc_map(BYTE *M){
    put32(M+0x5d,hxc_next()%4?hxc_next()%0x14:0xffffffffu);put32(M+0x81,hxc_next()%0x14);
    M[0x103]=(BYTE)(hxc_next()%2);M[0x104]=(BYTE)(hxc_next()%2);put32(M+0x105,hxc_index());
    for(int e=-1;e<3;++e){BYTE *R=M+0x85+0x2a*e;
        put32(R+0x22,hxc_next()%4?hxc_next()%2:hxc_value());put32(R+0x26,hxc_next()%4?hxc_next()%2:hxc_value());}
}
/* One callee side effect (about one call in four), drawn from the stream. */
static void hxc_effect(void){
    const uint32_t pick=hxc_next();if(pick%4)return;
    BYTE *H=hxc_arena+HXC_H;
    switch(pick/4%16){
    case 0:H[0x528]=(BYTE)(10+hxc_next()%3);break;
    case 1:put32(H+0x109,hxc_page());break;
    case 2:put32(H+0x517,hxc_selected());break;
    case 3:put32(mapped+0xd03c8+4*(hxc_next()%3),hxc_wingman());break;
    case 4:hxc_arena[HXC_ACT+0x100*(hxc_next()%3)+(hxc_next()%2?0x99:0xa3)]=(BYTE)(hxc_next()%2);break;
    case 5:H[0x51f]=(BYTE)(hxc_next()%2);break;
    case 6:hxc_arena[(hxc_next()%2?HXC_M:HXC_M2)+(hxc_next()%2?0x103:0x104)]=(BYTE)(hxc_next()%2);break;
    case 7:put32(H+0x101,HXC_ADDR(hxc_next()%2?HXC_M:HXC_M2));break;
    case 8:put16(hxc_arena+(hxc_next()%2?HXC_M:HXC_M2)+0x5d,(uint16_t)hxc_value());break;
    case 9:put32(mapped+0x2c5f70,HXC_ADDR(hxc_next()%2?HXC_X1:HXC_X2));break;
    case 10:put32(H+0x14,hxc_value());break;
    case 11:put16(mapped+0xd3b96+4*(hxc_next()%2),(uint16_t)(hxc_next()%2?hxc_value():0u));break;
    case 12:put32(H+0xf1+4*(hxc_next()%4),hxc_value());break;
    case 13:put32(hxc_arena+(hxc_next()%2?HXC_M:HXC_M2)+0x105,hxc_index());break;
    case 14:put32(H+0x105,HXC_ADDR(HXC_W+hxc_next()%64));break;
    default:{BYTE *R=hxc_arena+(hxc_next()%2?HXC_M:HXC_M2)+0x85+0x2a*(int)(hxc_next()%4)-0x2a;
        put32(R+(hxc_next()%2?0x22:0x26),hxc_next()%2);break;}
    }
}
__attribute__((used,noinline)) static uint64_t __cdecl hxc_log_impl(unsigned id){
    if(hxc_calls==HXC_MAX){fail("hdd_execute synthetic call overflow");ExitProcess(1);}
    uint32_t *a=(uint32_t *)(uintptr_t)(hxc_cap[7]+4u);
    const uint32_t f=hxc_frame_base();
    hxc_record *v=hxc_log+hxc_calls++;memset(v,0,sizeof(*v));v->id=id;v->slots=weapons_bridge_slots(a,hxc_arity[id]);
    for(unsigned j=0;j<hxc_arity[id];++j)v->a[j]=hxc_rel(a[j],f);
    for(unsigned j=0;j<3;++j)v->regs[j]=hxc_rel(hxc_cap[j],f);
    if(hxc_nested_ecx&&id>=HXC_DIRECT)v->regs[2]=0;
    if(id<HXC_DIRECT||hxc_run==2){
        v->loc[4]=hxc_word(f-0x10u);v->loc[5]=hxc_word(f-8u);
        if(id==9||id==10)v->loc[6]=hxc_word(f-4u);
        if(id==10)for(unsigned j=0;j<4;++j)v->loc[j]=hxc_word(f-0x20u+4u*j);
    }
    for(unsigned c=0;c<2;++c){
        const uint32_t p=get32(hxc_arena+(c?HXC_X2:HXC_X1)+0x22c);
        if(p==f-8u){v->ctx[c][0]=1;v->ctx[c][1]=hxc_word(p);v->ctx[c][2]=hxc_word(p+4u);}
        else v->ctx[c][1]=p;
    }
    if(hxc_track){
        /* The callee-saved registers, the stack position and the residue
         * only for 44a1a0's own callees (ids below HXC_DIRECT, entry ESP
         * within 10h bytes below the outgoing ESP f-2Ch): below a nested
         * hook they are that hook's. */
        if(id<HXC_DIRECT&&(f-0x2cu)-hxc_cap[7]-1u<0x10u){for(unsigned j=0;j<3;++j)v->cs[j]=hxc_cap[3+j];
            v->frame[0]=hxc_cap[6]-hxc_origin;v->frame[1]=hxc_cap[7]-hxc_origin;v->window=hxc_hash(hxc_window,256);}
        v->locals=hxc_hash((const BYTE *)(uintptr_t)(f-0x2cu),0x2c);}
    uint32_t eax=hxc_value(),edx=hxc_value();hxc_ecx=hxc_value();
    if(id==6||id==8)eax=hxc_wingman();
    if(hxc_track&&hxc_arity[id]==1&&hxc_next()%4==0){a[0]=hxc_value();++hxc_clobbers;}   /* the callee changes its slot */
    hxc_effect();
    return (uint64_t)edx<<32|eax;
}
#define HXC_STUB(n) __attribute__((naked)) static void hxc_stub_##n(void){__asm__ volatile( \
    "movl %eax,_hxc_cap\n\tmovl %edx,_hxc_cap+4\n\tmovl %ecx,_hxc_cap+8\n\tmovl %ebx,_hxc_cap+12\n\t" \
    "movl %esi,_hxc_cap+16\n\tmovl %edi,_hxc_cap+20\n\tmovl %ebp,_hxc_cap+24\n\tmovl %esp,_hxc_cap+28\n\t" \
    "leal -256(%esp),%esi\n\tmovl $_hxc_window,%edi\n\tmovl $64,%ecx\n\tcld\n\trep movsl\n\t" \
    "movl _hxc_cap+16,%esi\n\tmovl _hxc_cap+20,%edi\n\t" \
    "pushl $" #n "\n\tcalll _hxc_log_impl\n\taddl $4,%esp\n\tmovl _hxc_ecx,%ecx\n\tret\n\t");}
HXC_STUB(0) HXC_STUB(1) HXC_STUB(2) HXC_STUB(3) HXC_STUB(4) HXC_STUB(5) HXC_STUB(6) HXC_STUB(7) HXC_STUB(8) HXC_STUB(9)
HXC_STUB(10) HXC_STUB(11) HXC_STUB(12) HXC_STUB(13) HXC_STUB(14) HXC_STUB(15) HXC_STUB(16) HXC_STUB(17) HXC_STUB(18)
HXC_STUB(19) HXC_STUB(20) HXC_STUB(21) HXC_STUB(22) HXC_STUB(23) HXC_STUB(24) HXC_STUB(25)
#undef HXC_STUB
static void *hxc_stubs[HXC_IDS]={hxc_stub_0,hxc_stub_1,hxc_stub_2,hxc_stub_3,hxc_stub_4,hxc_stub_5,hxc_stub_6,hxc_stub_7,
    hxc_stub_8,hxc_stub_9,hxc_stub_10,hxc_stub_11,hxc_stub_12,hxc_stub_13,hxc_stub_14,hxc_stub_15,hxc_stub_16,hxc_stub_17,
    hxc_stub_18,hxc_stub_19,hxc_stub_20,hxc_stub_21,hxc_stub_22,hxc_stub_23,hxc_stub_24,hxc_stub_25};
static int hxc_patch(BYTE saved[HXC_IDS][5],int active[HXC_IDS],unsigned which,int on){
    for(unsigned i=0;i<HXC_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(hxc_addr[i]-0x400000u,hxc_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(hxc_addr[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
/* Jump table 44a1f7 (15 entries) redirected through counting trampolines. */
__attribute__((used)) static uint32_t hxc_case_targets[HXC_TABLE],hxc_case_hits[HXC_TABLE];
#define HXC_CASE(n) __attribute__((naked)) static void hxc_case_##n(void){__asm__ volatile( \
    "incl _hxc_case_hits+" #n "*4\n\tjmp *_hxc_case_targets+" #n "*4\n\t");}
HXC_CASE(0) HXC_CASE(1) HXC_CASE(2) HXC_CASE(3) HXC_CASE(4) HXC_CASE(5) HXC_CASE(6) HXC_CASE(7) HXC_CASE(8) HXC_CASE(9)
HXC_CASE(10) HXC_CASE(11) HXC_CASE(12) HXC_CASE(13) HXC_CASE(14)
#undef HXC_CASE
static void *hxc_cases_entry[HXC_TABLE]={hxc_case_0,hxc_case_1,hxc_case_2,hxc_case_3,hxc_case_4,hxc_case_5,hxc_case_6,
    hxc_case_7,hxc_case_8,hxc_case_9,hxc_case_10,hxc_case_11,hxc_case_12,hxc_case_13,hxc_case_14};
static int hxc_table(int install){
    BYTE *t=mapped+0x4a1f7u;DWORD old,ignored;
    if(!VirtualProtect(t,4*HXC_TABLE,PAGE_EXECUTE_READWRITE,&old))return fail("hdd_execute table protection");
    for(unsigned k=0;k<HXC_TABLE;++k){
        if(install){hxc_case_targets[k]=get32(t+4*k);hxc_case_hits[k]=0;put32(t+4*k,(uint32_t)(uintptr_t)hxc_cases_entry[k]);}
        else put32(t+4*k,hxc_case_targets[k]);
    }
    if(!VirtualProtect(t,4*HXC_TABLE,old,&ignored))return fail("hdd_execute table restore protection");
    FlushInstructionCache(GetCurrentProcess(),t,4*HXC_TABLE);return 0;
}
/* Builds one state; a[0] = H. */
static void hxc_build(unsigned n,uint32_t a[6],uint32_t r[3]){
    hxc_seed=random32();hxc_stream=hxc_seed^n;random_bytes(hxc_arena,HXC_SIZE);stack_clean_bytes(hxc_arena,HXC_SIZE);memset(a,0,24);for(unsigned i=0;i<3;++i)r[i]=hxc_value();
    BYTE *H=hxc_arena+HXC_H;
    switch(hxc_next()%20){case 0:H[0x528]=0xff;break;case 1:H[0x528]=(BYTE)(15+hxc_next()%4);break;
        case 2:H[0x528]=(BYTE)hxc_value();break;default:H[0x528]=(BYTE)(hxc_next()%15);}
    H[0x76]=(BYTE)(hxc_next()%4==0?hxc_value()|1u:0u);
    put32(H+0x109,hxc_page());put32(H+0x101,HXC_ADDR(hxc_next()%8?HXC_M:HXC_M2));put32(H+0x105,HXC_ADDR(HXC_W+hxc_next()%64));
    put32(H+0x14,hxc_value());put32(H+0x517,hxc_selected());H[0x51f]=(BYTE)(hxc_next()%2);
    hxc_map(hxc_arena+HXC_M);hxc_map(hxc_arena+HXC_M2);
    for(unsigned i=0;i<3;++i){hxc_arena[HXC_ACT+0x100*i+0x99]=(BYTE)(hxc_next()%3==0);hxc_arena[HXC_ACT+0x100*i+0xa3]=(BYTE)(hxc_next()%2);}
    for(unsigned k=0;k<3;++k)put32(mapped+0xd03c8+4*k,hxc_wingman());
    random_bytes(mapped+0xd03d4,0x16);stack_clean_bytes(mapped+0xd03d4,0x16);
    for(unsigned k=0;k<2;++k)put16(mapped+0xd3b96+4*k,(uint16_t)(hxc_next()%3?hxc_value():0u));
    put32(mapped+0x2c5f70,HXC_ADDR(hxc_next()%2?HXC_X1:HXC_X2));
    put32(mapped+0x9d6b0,HXC_ADDR(HXC_HOLD));put32(hxc_arena+HXC_HOLD+8,HXC_ADDR(HXC_H));
    a[0]=HXC_ADDR(HXC_H);
    hxc_stream=hxc_seed;   /* the stubs' stream restarts here in both runs */
}
/* [X+22Ch] relative to the run's frame (the arena compare). */
static void hxc_normalise(uint32_t f){
    for(unsigned c=0;c<2;++c){BYTE *p=hxc_arena+(c?HXC_X2:HXC_X1)+0x22c;if(get32(p)==f-8u)put32(p,0xffff0008u);}
}
static unsigned hxc_hits(unsigned id){unsigned h=0;for(unsigned j=0;j<hxc_calls;++j)if(hxc_log[j].id==id)++h;return h;}
static int hxc_counter_check(const callee_counter *c,uint64_t delta){
    uint64_t want=0;for(unsigned i=0;i<HXC_DIRECT;++i)if(hxc_addr[i]==c->key)want=hxc_hits(i);
    return delta!=want;
}
static int hxc_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const hxc_record *e,const BYTE *want,const BYTE *got){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,hxc_calls);
    for(unsigned j=0;j<calls&&j<hxc_calls&&e;++j)if(memcmp(e+j,hxc_log+j,sizeof(*e))){
        const uint32_t *x=(const uint32_t*)(e+j),*y=(const uint32_t*)(hxc_log+j);
        for(unsigned k=0;k<sizeof(*e)/4;++k)if(x[k]!=y[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,e[j].id,k,x[k],y[k]);break;}
    for(unsigned j=0;j<HXC_SIZE;++j)if(hxc_after[j]!=hxc_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,hxc_after[j],hxc_arena[j]);break;}
    for(unsigned j=0;j<HXC_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);break;}
    return 1;
}
static int hxc_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[HXC_GLOBALS],after[HXC_GLOBALS],actual[HXC_GLOBALS];static hxc_record expected[HXC_MAX];
    memcpy(hxc_before,hxc_arena,HXC_SIZE);hxc_globals(before,0);hxc_alias_hit=0;
    hxc_run=0;hxc_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);
    /* The original's EBP: from a stub, or (no call) the slot address - 8 is
     * not known here; [X+22Ch] is only stored before a call. */
    if(hxc_calls)hxc_normalise(hxc_cap[6]);
    unsigned calls=hxc_calls;memcpy(expected,hxc_log,calls*sizeof(*expected));memcpy(hxc_after,hxc_arena,HXC_SIZE);hxc_globals(after,0);
    for(unsigned j=0;j<calls;++j)hxc_paths[expected[j].id]++;
    memcpy(hxc_arena,hxc_before,HXC_SIZE);hxc_globals(before,1);hxc_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[HXC_DIRECT]={0};
    for(unsigned i=0;i<count&&i<HXC_DIRECT;++i)counts[i]=c[i].count;
    hxc_run=1;pair[1]=x->replacement_context(a,d->user,r);if(hxc_calls)hxc_normalise(hdd_exec_plain_frame);hxc_globals(actual,0);
    if(hxc_alias_hit){fprintf(stderr,"FAIL: %s case %u call %u: data word %08x lies in the worker stack reservation\n",current_function,n,hxc_alias_call,hxc_alias_value);return 1;}
    if(n<4096)for(unsigned i=0;i<count&&i<HXC_DIRECT;++i)if(hxc_counter_check(c+i,c[i].count-counts[i]))return fail("hdd_execute callee counter delta");
    if(pair[0]!=pair[1]||calls!=hxc_calls||memcmp(expected,hxc_log,calls*sizeof(*expected))||memcmp(hxc_after,hxc_arena,HXC_SIZE)||
       memcmp(after,actual,HXC_GLOBALS))
        return hxc_report("direct",n,pair,calls,expected,after,actual);
    hxc_globals(before,1);
    return 0;
}
/* Calls target with one slot over a 4-KiB fill, EAX..EDI distinct pattern
 * words; keeps the slot address in hxc_origin and returns EDX:EAX in out. */
__attribute__((naked,noinline)) static void hxc_track_run(void *target,const uint32_t *a,uint32_t pattern,uint32_t out[2]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 16(%ebp),%eax\n\tmovl $1024,%ecx\n\tcld\n\trep stosl\n\taddl $4092,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl (%esi)\n\tmovl %esp,_hxc_origin\n\tmovl 16(%ebp),%eax\n\t"
        "movl %eax,%edx\n\txorl $0x01010101,%edx\n\tmovl %eax,%ecx\n\txorl $0x02020202,%ecx\n\tmovl %eax,%ebx\n\txorl $0x04040404,%ebx\n\t"
        "movl %eax,%esi\n\txorl $0x08080808,%esi\n\tmovl %eax,%edi\n\txorl $0x10101010,%edi\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%ebx\n\tmovl %eax,(%ebx)\n\tmovl %edx,4(%ebx)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* Installed probes through the entry shim: the original 44a1a0 and the
 * installed replacement must agree in EDX:EAX, the records (with
 * EBX/ESI/EDI, EBP/ESP relative to the slot, the residue hash below each
 * callee's ESP, the hash of the frame below f and the record behind
 * [X+22Ch]), the arena (the stored frame pointer relative to the slot) and
 * the globals. */
static int hxc_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0,stored=0;
    hxc_track=1;hxc_clobbers=0;hxc_run=2;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],want[2],got[2],pattern=poison?0xa7a7a7a7u:0x31313131u;hxc_build(n*41u+3u,a,r);
        BYTE before[HXC_GLOBALS],after[HXC_GLOBALS],actual[HXC_GLOBALS],expected_fpu[108],actual_fpu[108];static hxc_record expected[HXC_MAX];
        memcpy(hxc_before,hxc_arena,HXC_SIZE);hxc_globals(before,0);
        hxc_reset();hxc_track_run(mapped+d->rva,a,pattern,want);es2_hook_fpu_save(expected_fpu);hxc_normalise(hxc_origin-8u);
        unsigned calls=hxc_calls;memcpy(expected,hxc_log,calls*sizeof(*expected));memcpy(hxc_after,hxc_arena,HXC_SIZE);hxc_globals(after,0);
        for(unsigned c=0;c<2;++c)if(get32(hxc_after+(c?HXC_X2:HXC_X1)+0x22c)==0xffff0008u)++stored;
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("hdd_execute poison install");
        memcpy(hxc_arena,hxc_before,HXC_SIZE);hxc_globals(before,1);hxc_reset();
        hxc_track_run(mapped+d->rva,a,pattern,got);es2_hook_fpu_save(actual_fpu);hxc_normalise(hxc_origin-8u);hxc_globals(actual,0);
        uint64_t pair[2]={(uint64_t)want[1]<<32|want[0],(uint64_t)got[1]<<32|got[0]};
        if(pair[0]!=pair[1]||calls!=hxc_calls||memcmp(expected,hxc_log,calls*sizeof(*expected))||memcmp(hxc_after,hxc_arena,HXC_SIZE)||
           memcmp(after,actual,HXC_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=hxc_report(fast?"installed fast":"installed preserving",n,pair,calls,expected,after,actual);
        if(!es2_hook_uninstall(&h))return fail("hdd_execute poison uninstall");hxc_globals(before,1);++total;
    }
    hxc_track=0;hxc_run=0;
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status&&(!hxc_clobbers||!stored)){fprintf(stderr,"FAIL: hdd_execute installed probes: changed argument slots %u, stored frame pointers %u\n",
        hxc_clobbers,stored);status=1;}
    if(!status)printf("PASS: %s %u installed cases through the entry shim, fast/preserving dispatch, 31/a7 stack, per-call EBX/ESI/EDI, EBP/ESP, residue below each callee, the frame below f and the record behind [X+22Ch] (stored in %u runs), changed argument slots (%u in both runs), nonempty x87, full pair/calls/arena/globals\n",
        d->name,total,stored,hxc_clobbers);
    return status;
}
/* Real-callee pass: 44a1a0 over the real 44db9c, 44db4c, order_cancel,
 * wingman_select and map_mode (their callees stubbed), the four chain hooks
 * installed original/replaced at random per case, against the all-original
 * run; the frame values are compared relative to the slot. */
static int hxc_real(unsigned index){
    const unsigned set[4]={index,ES2_HOOK_INDEX_order_cancel,ES2_HOOK_INDEX_wingman_select,ES2_HOOK_INDEX_map_mode};
    BYTE saved[HXC_IDS][5];int active[HXC_IDS];if(hxc_patch(saved,active,HXC_REAL,1))return 1;
    int status=0;unsigned total=0;uint64_t entered[4]={0};
    hxc_run=2;
    for(unsigned n=0;n<4096&&!status;++n){
        uint32_t a[6],r[3],want[2],got[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;hxc_build(n*67u+5u,a,r);
        const unsigned mode=(unsigned)hxc_next()%16;
        if(n<16)hxc_arena[HXC_H+0x528]=(BYTE)(n%2?14:13);   /* the order paths first */
        hxc_stream=hxc_seed;hxc_nested_ecx=(mode&14u)!=0;
        BYTE before[HXC_GLOBALS],after[HXC_GLOBALS],actual[HXC_GLOBALS];static hxc_record expected[HXC_MAX];
        memcpy(hxc_before,hxc_arena,HXC_SIZE);hxc_globals(before,0);
        hxc_reset();hxc_track_run(mapped+function_hook_descriptor(index)->rva,a,pattern,want);hxc_normalise(hxc_origin-8u);
        unsigned calls=hxc_calls;memcpy(expected,hxc_log,calls*sizeof(*expected));memcpy(hxc_after,hxc_arena,HXC_SIZE);hxc_globals(after,0);
        es2_hook hk[4];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<4;++j){o.mode=(mode>>j&1)?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(set[j]),&o,function_hook_extension(set[j])))return fail("hdd_execute real install");}
        memcpy(hxc_arena,hxc_before,HXC_SIZE);hxc_globals(before,1);hxc_reset();
        hxc_track_run(mapped+function_hook_descriptor(index)->rva,a,pattern,got);hxc_normalise(hxc_origin-8u);hxc_globals(actual,0);
        uint64_t pair[2]={(uint64_t)want[1]<<32|want[0],(uint64_t)got[1]<<32|got[0]};
        if(pair[0]!=pair[1]||calls!=hxc_calls||memcmp(expected,hxc_log,calls*sizeof(*expected))||memcmp(hxc_after,hxc_arena,HXC_SIZE)||
           memcmp(after,actual,HXC_GLOBALS)){
            fprintf(stderr,"real mode=%u\n",mode);status=hxc_report("real",n,pair,calls,expected,after,actual);}
        for(unsigned j=0;j<4;++j)if(mode>>j&1)entered[j]+=hk[j].calls;
        for(unsigned j=0;j<4;++j)if(!es2_hook_uninstall(hk+j))return fail("hdd_execute real uninstall");
        hxc_globals(before,1);++total;
    }
    hxc_track=0;hxc_run=0;hxc_nested_ecx=0;
    if(hxc_patch(saved,active,0,0))return 1;
    if(!status&&(!entered[0]||!entered[1]||!entered[2]||!entered[3])){fprintf(stderr,"FAIL: %s real pass: replaced entries %llu/%llu/%llu/%llu\n",
        current_function,(unsigned long long)entered[0],(unsigned long long)entered[1],(unsigned long long)entered[2],(unsigned long long)entered[3]);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases over the real 44db9c, 44db4c, order_cancel, wingman_select and map_mode (the four hooks original/replaced at random; replaced entries hdd_execute %llu, order_cancel %llu, wingman_select %llu, map_mode %llu), 31/a7 stack, pair/calls/arena/globals\n",
        current_function,total,(unsigned long long)entered[0],(unsigned long long)entered[1],(unsigned long long)entered[2],(unsigned long long)entered[3]);
    return status;
}
static int hdd_exec_cases(unsigned index,unsigned limit){
    BYTE old_globals[HXC_GLOBALS];hxc_globals(old_globals,0);
    BYTE saved[HXC_IDS][5];int active[HXC_IDS];
    if(hxc_patch(saved,active,HXC_MAIN,1))return 1;
    const int table=!limit;
    if(table&&hxc_table(1))return 1;
    unsigned cases=case_budget(limit,1000448);const char *quick=getenv("ES2_INVITRO_HDD_EXEC_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);memset(hxc_paths,0,sizeof(hxc_paths));hxc_track=0;
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];hxc_build(n,a,r);status=hxc_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);current_case_valid=0;
    if(table){
        if(hxc_table(0))return 1;
        for(unsigned k=0;k<HXC_TABLE;++k){
            printf("JUMPTABLE hdd_execute 44a1f7 entry %2u target %08x hits %u\n",k,hxc_case_targets[k],hxc_case_hits[k]);
            if(!status&&!hxc_case_hits[k]){fprintf(stderr,"FAIL: hdd_execute jump-table entry %u never taken\n",k);status=1;}
        }
    }
    hxc_globals(old_globals,1);
    if(!limit&&!status)status=hxc_installed_probes(index,256);
    hxc_globals(old_globals,1);
    if(hxc_patch(saved,active,HXC_MAIN,0))return 1;
    if(!limit&&!status)status=hxc_real(index);
    hxc_globals(old_globals,1);
    if(!status){
        printf("PASS: %-20s %u cases (stub calls",function_hook_descriptor(index)->name,cases);
        for(unsigned i=0;i<HXC_DIRECT;++i)printf(" %06x %u%s",hxc_addr[i],hxc_paths[i],i+1<HXC_DIRECT?",":"");
        printf("); full return pair, ordered calls with entry registers and the written locals, the record behind [X+22Ch], arena and globals\n");
    }
    return status;
}
