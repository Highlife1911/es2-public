/* control_record group: control_record 45a810 (docs/reverse-
 * engineering/input.md, "control record 45a810"). Direct cases run the original
 * body and the core against logging stubs for all eighteen callees (the pump
 * 464c78, the device poll 45baa8, the key read 45bbc4, the joystick query
 * 45c524/477904, the keyboard axes 45a4cc, the command handler 45feb8, the
 * event list 452d40, the playback helpers 45fe6c, 453078, 486ece, the file name
 * 492b90, the error report 479c78 and the runtime thunks memset 49681a, fopen
 * 496802, fread 49684a, fwrite 496832, fclose 496838): ordered calls with their
 * arguments, entry EAX/EDX/ECX (stack values masked: the frame is the
 * original's or the adapter's scratch), the key word the reader receives, the
 * 14-byte event and up to 32 bytes of every written block by content. The stubs
 * change what the original rereads (the device block pointer, its key word, the
 * queued-key count, the view and projectile words, the keyboard-axis block).
 * The record, the device block, the pointer tables, the flag words and the
 * tables the function reads are compared as windows and restored; EAX, EDX and
 * ECX (the latter from the adapter, which the entry shim returns) are compared.
 * Then installed 31/a7 poison probes through the entry shim (EAX/EDX/ECX) and a
 * real-callee pass with the real 45c524, 477904, 45a4cc, 486ece and 453078, the
 * hook original or replaced. */
typedef struct ctl_record { uint32_t id,args[6],regs[3],eax,length;BYTE content[32]; uint32_t count; } ctl_record;
enum { CTL_SIZE=0x1000, CTL_MAX=96, CTL_IDS=18, CTL_DEVICE2=0x40, CTL_JOY=0x100, CTL_JOY2=0x110, CTL_LIST=0x200, CTL_LIST_DATA=0x240,
    CTL_TARGETS=0x300, CTL_NAME=0x400, CTL_WINDOWS=10 };
#define CTL_BASE ((uint32_t)(uintptr_t)ctl_arena)
static BYTE ctl_arena[CTL_SIZE],ctl_before[CTL_SIZE],ctl_after[CTL_SIZE];
static ctl_record ctl_log[CTL_MAX];
static unsigned ctl_calls,ctl_changes;
static uint32_t ctl_seed,ctl_stream;
__attribute__((used)) static uint32_t ctl_ecx;
static const uint32_t ctl_addresses[CTL_IDS]={0x464c78,0x49681a,0x45baa8,0x45bbc4,0x477904,0x49684a,0x486ece,0x45fe6c,0x496838,
    0x453078,0x45c524,0x45a4cc,0x45feb8,0x452d40,0x492b90,0x496802,0x479c78,0x496832};
enum { KS_PUMP, KS_MEMSET, KS_POLL, KS_KEY, KS_JOYSTICK, KS_FREAD, KS_ADDRESS, KS_PLAYKEY, KS_FCLOSE, KS_EVENT, KS_DEVICE, KS_AXES,
    KS_COMMAND, KS_EVENTS, KS_NAME, KS_FOPEN, KS_ERROR, KS_FWRITE };
static const unsigned ctl_counts[CTL_IDS]={0,3,0,2,0,4,1,1,1,2,1,2,1,0,2,2,6,4};
/* Global windows (RVA, length): mapping table, the 4d1dd6 byte, the key
 * queue, record through the device block, the flag words, the invert and
 * count dwords, the 49ebbc bytes and static block, the time scale. */
static const uint32_t ctl_window_rva[CTL_WINDOWS]={0xd1f38,0xd1dd0,0xd20c4,0xd22c6,0xd24d0,0x9ea90,0x9ebb8,0xd3b64,0xd1e00,0xa3620};
static const unsigned ctl_window_len[CTL_WINDOWS]={0x108,0x10,0x40,0x17a,0xb0,0x30,0x38,4,0x10,8};
#define CTL_GLOBALS (0x108+0x10+0x40+0x17a+0xb0+0x30+0x38+4+0x10+8)
#define CTL_VA(va) (mapped+(uint32_t)(va)-0x400000u)
static uint32_t ctl_next(void){
    uint32_t z=ctl_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static int ctl_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
/* A second device block in the arena; the stubs may switch 4d2390 to it. */
static uint32_t ctl_device(int second){return second?CTL_BASE+CTL_DEVICE2:0x4d23f6u;}
static void ctl_content(ctl_record *v,uint32_t p,uint32_t n){
    v->length=n;if(n>32)n=32;if(n&&p)memcpy(v->content,(const void *)(uintptr_t)p,n);
}
__attribute__((used,noinline)) static uint64_t __cdecl ctl_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(ctl_calls==CTL_MAX){fail("control record synthetic call overflow");ExitProcess(1);}
    ctl_record *v=ctl_log+ctl_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,4*ctl_counts[id]);memcpy(v->regs,regs,12);
    v->count=weapons_bridge_slots(a,ctl_counts[id]);
    for(unsigned j=0;j<3;++j)if(ctl_stack(v->regs[j]))v->regs[j]=0;
    for(unsigned j=0;j<ctl_counts[id];++j)if(ctl_stack(v->args[j]))v->args[j]=0;
    uint32_t eax=ctl_next(),edx=ctl_next(),pick=ctl_next(),b=CTL_BASE;ctl_ecx=ctl_next();
    switch(id){
    case KS_PUMP:if(pick%2)eax=0;break;
    case KS_MEMSET:memset((void *)(uintptr_t)a[0],(int)a[1],a[2]);eax=a[0];break;
    case KS_POLL:eax=ctl_device(pick%4==0);
        /* A fresh key word: empty low byte, zero, 412h or random. */
        {BYTE *d=(BYTE *)(uintptr_t)eax;unsigned k=pick>>4&7;
         put16(d,(uint16_t)(k==0?0:k==1?(ctl_next()&0xff00):k==2?0x412:k==3?0x100:ctl_next()));}
        break;
    case KS_KEY:ctl_content(v,a[0],2);
        if(ctl_calls<6&&pick%4)put16((BYTE *)(uintptr_t)a[0],(uint16_t)(pick>>8&1?ctl_next()&0xff00:ctl_next()));
        eax=pick%5==0?(eax&0xffffff00u):(eax|1);break;
    case KS_JOYSTICK:eax=b+(pick%2?CTL_JOY:CTL_JOY2);break;
    case KS_FREAD:{uint32_t n=a[1]*a[2];BYTE *p=(BYTE *)(uintptr_t)a[0];
        if(n<=64)for(uint32_t j=0;j<n;++j)p[j]=(BYTE)ctl_next();
        /* Counts: the event count (frame +0Ch) and the queued keys. */
        if(n==4&&a[0]==0x49eaa0u)put32(p,(uint32_t)((int32_t)(ctl_next()%9)-2));
        else if(n==4)put32(p,(uint32_t)((int32_t)(ctl_next()%5)-1));
        if(n==0x10&&pick%8==0)put16(p,0x412);
        eax=pick%16==0?0:pick%16==1?ctl_next():1;break;}
    case KS_ADDRESS:for(unsigned j=0;j<8;++j)((BYTE *)(uintptr_t)a[0])[j]=(BYTE)ctl_next();break;
    case KS_PLAYKEY:
        if(pick%7==0)put32(CTL_VA(0x4d2390),ctl_device(pick>>8&1));
        if(pick%11==0)put16((BYTE *)(uintptr_t)get32(CTL_VA(0x4d2390)),(uint16_t)(pick>>12&1?0x412:0));
        break;
    case KS_EVENT:ctl_content(v,a[0],14);break;
    case KS_DEVICE:if(pick%2)eax=0;break;
    case KS_AXES:if(pick%3==0)for(unsigned j=0;j<0x14;++j)CTL_VA(0x4d23c1)[j]=(BYTE)(ctl_next()%3==0?ctl_next():0);
        if(pick%13==0)put32(CTL_VA(0x4d2390),ctl_device(pick>>8&1));
        break;
    case KS_COMMAND:
        if(ctl_changes<2&&pick%8==0){++ctl_changes;put32(CTL_VA(0x49eaa0),(uint32_t)((int32_t)(ctl_next()%10)-1));}
        if(pick%9==0)put16(CTL_VA(0x4d24f0),(uint16_t)(pick>>8&1));
        if(pick%10==0)put16(CTL_VA(0x4d2526),(uint16_t)(pick>>9&1));
        if(pick%17==0)put16(CTL_VA(0x4d24d6),(uint16_t)(pick>>10&1));
        break;
    case KS_EVENTS:eax=b+CTL_LIST;put32(ctl_arena+CTL_LIST,pick%4);put32(ctl_arena+CTL_LIST+4,b+CTL_LIST_DATA);break;
    case KS_NAME:eax=b+CTL_NAME;break;
    case KS_FOPEN:if(pick%4==0)eax=0;break;
    case KS_FWRITE:{uint32_t n=a[1]*a[2];ctl_content(v,a[0],n<=64?n:0);v->length=n;break;}
    default:break;
    }
    v->eax=eax;return (uint64_t)edx<<32|eax;
}
#define CTL_STUB(n) __attribute__((naked)) static void ctl_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _ctl_stub_impl\n\taddl $24,%esp\n\tmovl _ctl_ecx,%ecx\n\tret\n\t");}
CTL_STUB(0) CTL_STUB(1) CTL_STUB(2) CTL_STUB(3) CTL_STUB(4) CTL_STUB(5) CTL_STUB(6) CTL_STUB(7) CTL_STUB(8)
CTL_STUB(9) CTL_STUB(10) CTL_STUB(11) CTL_STUB(12) CTL_STUB(13) CTL_STUB(14) CTL_STUB(15) CTL_STUB(16) CTL_STUB(17)
#undef CTL_STUB
static void *ctl_stubs[CTL_IDS]={ctl_stub_0,ctl_stub_1,ctl_stub_2,ctl_stub_3,ctl_stub_4,ctl_stub_5,ctl_stub_6,ctl_stub_7,ctl_stub_8,
    ctl_stub_9,ctl_stub_10,ctl_stub_11,ctl_stub_12,ctl_stub_13,ctl_stub_14,ctl_stub_15,ctl_stub_16,ctl_stub_17};
static void ctl_globals(BYTE *out,int restore){
    for(unsigned i=0,at=0;i<CTL_WINDOWS;at+=ctl_window_len[i++]){
        BYTE *p=mapped+ctl_window_rva[i];
        if(restore)memcpy(p,out+at,ctl_window_len[i]);else memcpy(out+at,p,ctl_window_len[i]);
    }
}
static int ctl_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<CTL_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
static void ctl_reset(void){ctl_calls=0;ctl_changes=0;ctl_stream=ctl_seed;}
/* A pointer for the tables 4d2310..4d238c: the game's target or, in one
 * case of four, another compared location (aliasing the record, the
 * device blocks, the keyboard-axis block or the arena). */
static uint32_t ctl_pointer(uint32_t usual){
    if(random32()%4)return usual;
    static const uint32_t places[]={0x4d22c6,0x4d22d0,0x4d23c1,0x4d23f6,0x4d2400,0x4d22f6};
    unsigned k=random32()%8;
    if(k<6)return places[k]+random32()%8;
    return CTL_BASE+(k==6?CTL_DEVICE2:CTL_TARGETS)+random32()%0x40;
}
static uint32_t ctl_dword(void){unsigned k=random32()%4;return k==0?0:k==1?random32()%3:k==2?random32():0x8000u+random32()%0x10000u;}
static void ctl_block(BYTE *d){
    /* Device block: key word, cursor dwords, mouse bytes, four axis
     * dwords, eight buttons and four hat bytes (mostly 0/1). */
    put16(d,(uint16_t)(random32()%4==0?random32()&0xff00:random32()));
    put32(d+2,random32());put32(d+6,random32());
    for(unsigned j=0;j<3;++j)d[0xa+j]=(BYTE)(random32()%2);
    for(unsigned j=0;j<4;++j)put32(d+0xd+4*j,ctl_dword());
    for(unsigned j=0;j<12;++j)d[0x1d+j]=(BYTE)(random32()%3?random32()%2:random32());
}
static void ctl_build(unsigned n,uint32_t r[3]){
    uint32_t b=CTL_BASE;(void)n;
    ctl_seed=random32();random_bytes(ctl_arena,CTL_SIZE);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    for(unsigned i=0;i<CTL_WINDOWS-2;++i)random_bytes(mapped+ctl_window_rva[i],ctl_window_len[i]);
    /* Mapping bytes 0..3 (some random), the set index near 0 or 0FCh.. */
    for(unsigned j=0;j<0x108;++j)CTL_VA(0x4d1f38)[j]=(BYTE)(random32()%8?random32()%4:random32());
    CTL_VA(0x4d2577)[0]=(BYTE)(random32()%16?random32()%0x20:0xfc+random32()%4);
    CTL_VA(0x4d2571)[0]=(BYTE)(random32()%2);
    put16(CTL_VA(0x4d24d6),(uint16_t)(random32()%4==0?1+random32()%3:0));
    put16(CTL_VA(0x4d24d8),(uint16_t)(random32()%4==0?1:0));
    put16(CTL_VA(0x4d24f0),(uint16_t)(random32()%2?0:random32()%3?1:random32()));
    put16(CTL_VA(0x4d2526),(uint16_t)(random32()%4?0:1));
    put16(CTL_VA(0x4d2530),(uint16_t)(random32()%2));
    put32(CTL_VA(0x49eaa0),(uint32_t)((int32_t)(random32()%8)-1));
    put16(CTL_VA(0x49eab8),(uint16_t)(random32()%4?0xffff:random32()));
    for(unsigned j=0;j<4;++j)put32(CTL_VA(0x49ea90)+4*j,random32()%2?0:random32()%3?1:random32());
    CTL_VA(0x49ebbc)[0]=(BYTE)(random32()%3==0);CTL_VA(0x49ebbd)[0]=(BYTE)(random32()%3);
    for(unsigned j=0;j<16;++j)CTL_VA(0x4d22f6)[j]=(BYTE)(random32()%2);
    for(unsigned j=0;j<0x10;++j)put32(CTL_VA(0x4d23c1)+4*j,random32()%3?0:ctl_dword());
    ctl_block(CTL_VA(0x4d23f6));ctl_block(ctl_arena+CTL_DEVICE2);
    put32(CTL_VA(0x4d2390),ctl_device(random32()%4==0));
    /* The pointer tables as 45a6a7.. sets them up, with aliasing. */
    for(unsigned j=0;j<4;++j)put32(CTL_VA(0x4d2310)+4*j,ctl_pointer(0x4d22d4u+2*j));
    for(unsigned j=0;j<8;++j)put32(CTL_VA(0x4d2320)+4*j,ctl_pointer(0x4d22dcu+j));
    for(unsigned j=0;j<4;++j)put32(CTL_VA(0x4d2340)+4*j,ctl_pointer(0x4d23c1u+4*j));
    for(unsigned j=0;j<4;++j)put32(CTL_VA(0x4d2350)+4*j,ctl_pointer(0x4d2403u+4*j));
    for(unsigned j=0;j<4;++j)put32(CTL_VA(0x4d2360)+4*j,ctl_pointer(0x4d23d1u+j));
    for(unsigned j=0;j<8;++j)put32(CTL_VA(0x4d2370)+4*j,ctl_pointer(0x4d2413u+j));
    for(unsigned j=0;j<4;++j)CTL_VA(0x4d23d1)[j]=(BYTE)(random32()%2);
    for(unsigned j=0;j<0x40;++j)ctl_arena[CTL_TARGETS+j]=(BYTE)(random32()%3?random32()%2:random32());
    /* Joystick summaries: word +0 (rarely 0), flags +4/+5. */
    for(unsigned k=0;k<2;++k){BYTE *j=ctl_arena+(k?CTL_JOY2:CTL_JOY);put16(j,(uint16_t)(random32()%8?1+random32()%2:0));
        j[4]=(BYTE)(random32()%2);j[5]=(BYTE)(random32()%2);}
    put32(CTL_VA(0x4d3b64),random32());(void)b;
}
static int ctl_report(const char *what,unsigned n,const uint32_t want[3],const uint32_t got[3],unsigned calls,const ctl_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x calls %u/%u\n",current_function,n,what,
        want[0],got[0],want[1],got[1],want[2],got[2],calls,ctl_calls);
    for(unsigned j=0;j<calls&&j<ctl_calls&&expected;++j)if(memcmp(expected+j,ctl_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(ctl_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<CTL_SIZE;++j)if(ctl_after[j]!=ctl_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,ctl_after[j],ctl_arena[j]);break;}
    return 1;
}
static unsigned ctl_expected_hits(const callee_counter *c){
    unsigned hits=0;for(unsigned j=0;j<ctl_calls;++j)if(ctl_addresses[ctl_log[j].id]==c->key)++hits;return hits;
}
/* The original with entry EAX/EDX/ECX and no arguments; all three after
 * RET. With pattern != 0 the 4 KiB below and EBX are filled first. */
__attribute__((naked,noinline)) static void ctl_invoke(void *target,const uint32_t *registers,uint32_t pattern,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "cmpl $0,16(%ebp)\n\tje 1f\n\tsubl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 16(%ebp),%eax\n\tmovl $1024,%ecx\n\tcld\n\trep stosl\n\taddl $4096,%esp\n\t"
        "1: movl 16(%ebp),%ebx\n\tmovl 12(%ebp),%eax\n\tmovl 4(%eax),%edx\n\tmovl 8(%eax),%ecx\n\tmovl (%eax),%eax\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%ebx\n\tmovl %eax,(%ebx)\n\tmovl %edx,4(%ebx)\n\tmovl %ecx,8(%ebx)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static int ctl_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    static BYTE before[CTL_GLOBALS],after[CTL_GLOBALS],actual[CTL_GLOBALS];static ctl_record expected[CTL_MAX];
    memcpy(ctl_before,ctl_arena,CTL_SIZE);ctl_globals(before,0);
    uint32_t want[3],got[3];
    ctl_reset();ctl_invoke(mapped+d->rva,r,0,want);unsigned calls=ctl_calls;memcpy(expected,ctl_log,calls*sizeof(*expected));
    memcpy(ctl_after,ctl_arena,CTL_SIZE);ctl_globals(after,0);
    memcpy(ctl_arena,ctl_before,CTL_SIZE);ctl_globals(before,1);ctl_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[CTL_IDS];
    for(unsigned i=0;i<count&&i<CTL_IDS;++i)counts[i]=c[i].count;
    control_record_ecx_valid=0;uint64_t pair=x->replacement_context(a,d->user,r);ctl_globals(actual,0);
    got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);got[2]=control_record_ecx;
    if(!control_record_ecx_valid)return fail("control record adapter left no ECX");
    control_record_ecx_valid=0;
    if(n<4096)for(unsigned i=0;i<count&&i<CTL_IDS;++i)if(c[i].count-counts[i]!=ctl_expected_hits(c+i))return fail("control record callee counter delta");
    if(memcmp(want,got,12)||calls!=ctl_calls||memcmp(expected,ctl_log,calls*sizeof(*expected))||memcmp(ctl_after,ctl_arena,CTL_SIZE)||
       ctl_globals_differ(after,actual))
        return ctl_report("direct",n,want,got,calls,expected);
    return 0;
}
static int ctl_patch(BYTE saved[CTL_IDS][5],int active[CTL_IDS],uint32_t which,int on){
    for(unsigned i=0;i<CTL_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(ctl_addresses[i]-0x400000u,ctl_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(ctl_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static int ctl_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t r[3],want[3],got[3],pattern=poison?0xa7a7a7a7u:0x31313131u;ctl_build(n,r);
        static BYTE before[CTL_GLOBALS],after[CTL_GLOBALS],actual[CTL_GLOBALS];BYTE expected_fpu[108],actual_fpu[108];static ctl_record expected[CTL_MAX];
        memcpy(ctl_before,ctl_arena,CTL_SIZE);ctl_globals(before,0);
        ctl_reset();ctl_invoke(mapped+d->rva,r,pattern,want);es2_hook_fpu_save(expected_fpu);
        unsigned calls=ctl_calls;memcpy(expected,ctl_log,calls*sizeof(*expected));memcpy(ctl_after,ctl_arena,CTL_SIZE);ctl_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("control record poison install");
        memcpy(ctl_arena,ctl_before,CTL_SIZE);ctl_globals(before,1);ctl_reset();
        ctl_invoke(mapped+d->rva,r,pattern,got);es2_hook_fpu_save(actual_fpu);ctl_globals(actual,0);
        if(memcmp(want,got,12)||calls!=ctl_calls||memcmp(expected,ctl_log,calls*sizeof(*expected))||memcmp(ctl_after,ctl_arena,CTL_SIZE)||
           ctl_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1||control_record_ecx_valid)
            status=ctl_report(fast?"installed fast":"installed preserving",n,want,got,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("control record poison uninstall");ctl_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, EAX/EDX/ECX through the entry shim, trace/arena/globals\n",d->name,total);
    return status;
}
/* Real 45c524, 477904, 45a4cc, 486ece and 453078 (the rest stay stubs),
 * the hook original (through its shim) or replaced, against a run without
 * the hook. The joystick globals 6bb3b4/6bb3d8/6bb548/6bb6a4 and the
 * device table 4d2468 are set per case; 453078 appends to the list at
 * [4d1e04] in the arena. EAX/EDX/ECX, arena and globals (plus the
 * joystick summary 6bb6a8) are compared. */
static int ctl_real_pass(unsigned index){
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;
    const uint32_t which=0x3ffffu&~(1u<<KS_JOYSTICK|1u<<KS_ADDRESS|1u<<KS_EVENT|1u<<KS_DEVICE|1u<<KS_AXES);
    BYTE saved[CTL_IDS][5];int active[CTL_IDS];BYTE joy_saved[0x400],table_saved[0x40];
    memcpy(joy_saved,mapped+0x2bb3a0,sizeof(joy_saved));memcpy(table_saved,mapped+0xd2460,sizeof(table_saved));
    if(ctl_patch(saved,active,which,1))return 1;
    for(unsigned mode=0;mode<2&&!status;++mode)for(unsigned n=0;n<1536&&!status;++n){
        uint32_t r[3],want[3],got[3],pattern=n&1?0xa7a7a7a7u:0x31313131u;ctl_build(n,r);
        put32(mapped+0x2bb3b4,random32()%6);put32(mapped+0x2bb548,random32()%6);mapped[0x2bb3d8]=(BYTE)random32();
        put32(mapped+0x2bb6a4,random32()%2);
        put32(mapped+0xd2468+12,random32()%2?0:CTL_BASE+CTL_TARGETS+0x1f);
        put32(CTL_VA(0x4d1e04),CTL_BASE+CTL_LIST_DATA+0x80);put32(ctl_arena+CTL_LIST_DATA+0x80,random32()%8==0?0x63:random32()%0x60);
        put32(ctl_arena+CTL_LIST_DATA+0x84,CTL_BASE+CTL_LIST_DATA+0x100);
        static BYTE before[CTL_GLOBALS],after[CTL_GLOBALS],actual[CTL_GLOBALS];BYTE joy_before[16],joy_after[16];
        memcpy(ctl_before,ctl_arena,CTL_SIZE);ctl_globals(before,0);memcpy(joy_before,mapped+0x2bb6a8,16);
        ctl_reset();ctl_invoke(mapped+d->rva,r,pattern,want);
        memcpy(ctl_after,ctl_arena,CTL_SIZE);ctl_globals(after,0);memcpy(joy_after,mapped+0x2bb6a8,16);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        o.mode=mode?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("control record real install");
        memcpy(ctl_arena,ctl_before,CTL_SIZE);ctl_globals(before,1);memcpy(mapped+0x2bb6a8,joy_before,16);ctl_reset();
        ctl_invoke(mapped+d->rva,r,pattern,got);ctl_globals(actual,0);
        if(memcmp(want,got,12)||memcmp(ctl_after,ctl_arena,CTL_SIZE)||ctl_globals_differ(after,actual)||memcmp(joy_after,mapped+0x2bb6a8,16)||(mode&&h.calls!=1)){
            fprintf(stderr,"real mode=%u calls=%llu joystick=%d\n",mode,(unsigned long long)h.calls,memcmp(joy_after,mapped+0x2bb6a8,16)!=0);ctl_calls=0;status=ctl_report("real",n,want,got,0,NULL);
        }
        if(!es2_hook_uninstall(&h))return fail("control record real uninstall");ctl_globals(before,1);++total;
    }
    if(ctl_patch(saved,active,which,0))return 1;
    memcpy(mapped+0x2bb3a0,joy_saved,sizeof(joy_saved));memcpy(mapped+0xd2460,table_saved,sizeof(table_saved));
    if(!status)printf("PASS: %s %u real-callee cases (45c524, 477904, 45a4cc, 486ece, 453078), hook original or replaced, EAX/EDX/ECX, arena and globals\n",d->name,total);
    return status;
}
static int control_record_cases(unsigned index,unsigned limit){
    const uint32_t which=0x3ffffu;
    BYTE saved[CTL_IDS][5];static BYTE old_globals[CTL_GLOBALS];int active[CTL_IDS];
    ctl_globals(old_globals,0);
    if(ctl_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_CONTROL_RECORD_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6]={0},r[3];ctl_build(n,r);status=ctl_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=ctl_installed_probes(index,64);
    if(ctl_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=ctl_real_pass(index);
    ctl_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; EAX/EDX/ECX, ordered calls with entry registers and contents, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
