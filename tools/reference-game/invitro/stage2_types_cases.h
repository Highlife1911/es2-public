/* stage2_types group: the stage-2 callbacks with type data weapon_types 40fc8c,
 * rocket_types 40a818, bullet_types 40ade0, beam_types 40b6e0, mech_forms
 * 41fdb0, flyer_forms 422d90 and the weapon-list entry weapon_entry 40f8bc
 * (docs/reverse-engineering/mission_load.md, "Stage-2 callbacks with type
 * data").
 *
 * Direct cases run the original body (six argument slots, EAX/EDX/ECX at
 * entry from the case, over a stack filled with a case pattern), the
 * adapter (with the callee counter deltas) and the core (with the ECX it
 * reports) against logging stubs for every direct game callee of the hook
 * under test: 49630c (a record stub: the real FS:0 head at record+0,
 * context word +10h zeroed, the record linked into FS:0), the path builder
 * 492bec, the bitmap load 45cdf4, its descriptor 46a03c, the bitmap release
 * 48349c, the shape loader 474cd8 (stores one of two arena shape lists and
 * returns a count with a random high word), the stream constructor 472f14
 * (the stream's table, a state that is nonzero one time in four), the error
 * report 479c78, stream_read 4735b8 and two arena tables whose slot +18h is
 * a read stub (bounded data, count words for the 2-byte reads, sometimes a
 * switch of the stream's table), the array construction 49667c, weapon_entry
 * (under weapon_types), stream_close 47307c, stream_open 4730e8 (may switch
 * the table), stream_destroy 472f64, new[] 4774f0, _memset 49681a (fills
 * inside the arena and the compared globals), dmg_region_read 40cff8 and
 * collision_volume 40cc14 (under weapon_entry; may switch the table). Frame
 * addresses (the stream at f-30h, beam_types' count word at f-52h) are
 * logged relative to the hook's record (FS:0), so the original's stack frame
 * and the core's scratch frame compare equal. Compared: the full return
 * pair, ECX, FS:0, the ordered calls with their argument slots (and the
 * bridge's slot count), entry EAX/EDX/ECX and the record's context word, the
 * arena and the image globals the seven read or write (the type tables and
 * counts 4a96d0..4a970d, 4a9712..4a980b, 4a98ec..4a9903, 4a9ae8, 4a9d06..
 * 4a9da9, 4a9dca..4a9dff, the flag 498514, the beam steps 498640, the name
 * tables 498eb0 and 49a360, the directory pointers 4a09f0..4a0a13, the
 * construction counter pointer 6cbee4; all randomized per case, every
 * random register value and pointer-table dword outside the thread's stack
 * reservation so that it can never alias a frame address). The stubs
 * change what the originals reread (the count word last read, after new[],
 * 49667c or weapon_entry; the stream's table after a read, 4730e8, 40cff8 or
 * 40cc14; the bitmap slots 4a9ae8 and 4a9d72+8k and the shape list 4a9700
 * after 46a03c; the counter pointer after 48349c).
 *
 * Then installed 31/a7 poison probes and a real-callee pass: the real
 * 49630c, 49667c with its element constructors and _memset, a bump
 * allocator for new[], the other callees stubbed; weapon_entry nested under
 * weapon_types, and dmg_region_read, collision_volume and collision_points
 * nested under weapon_entry, in rotating original/replaced modes, the hook
 * under test original or replaced against a run without it. Identifiers use
 * the prefix st2_/ST2_. */
typedef struct st2_record { uint32_t id,slots,args[6],regs[3],context,eax,edx; } st2_record;
enum {
    ST2_SIZE=0x4000, ST2_MAX=160, ST2_IDS=20,
    ST2_VT1=0x40, ST2_VT2=0x80, ST2_COUNTER=0xc0, ST2_COUNTER2=0xd0, ST2_STREAM=0x100, ST2_ENTRY=0x180,
    ST2_SHAPES=0x400, ST2_SHAPES2=0x440, ST2_OBJECTS=0x500, ST2_BUMP=0x900, ST2_FIXED=0x3c00,
    ST2_GLOBALS=663   /* the sizes of st2_ranges */
};
#define ST2_B ((uint32_t)(uintptr_t)st2_arena)
#define ST2_A(o) (ST2_B+(uint32_t)(o))
static BYTE st2_arena[ST2_SIZE],st2_before[ST2_SIZE],st2_after[ST2_SIZE];
static st2_record st2_log[ST2_MAX];
static unsigned st2_calls,st2_real,st2_index,st2_paths[ST2_IDS];
static uint32_t st2_seed,st2_stream,st2_bump,st2_head,st2_last;
__attribute__((used)) static uint32_t st2_ecx;
/* 0 49630c, 1 492bec, 2 45cdf4, 3 46a03c, 4 48349c, 5 474cd8, 6 472f14,
 * 7 479c78, 8 stream_read 4735b8, 9 49667c, 10 weapon_entry 40f8bc,
 * 11 stream_close 47307c, 12 stream_open 4730e8, 13 stream_destroy 472f64,
 * 14 new[] 4774f0, 15 _memset 49681a, 16 dmg_region_read 40cff8,
 * 17 collision_volume 40cc14, 18/19 the read method of the two arena tables
 * (slot +18h); the slots each original call site pushes. */
static const uint32_t st2_addresses[ST2_IDS]={0x49630c,0x492bec,0x45cdf4,0x46a03c,0x48349c,0x474cd8,0x472f14,0x479c78,
    0x4735b8,0x49667c,0x40f8bc,0x47307c,0x4730e8,0x472f64,0x4774f0,0x49681a,0x40cff8,0x40cc14,0,0};
static const unsigned st2_counts[ST2_IDS]={0,2,1,1,2,4,2,6,3,5,2,1,2,2,1,3,2,2,3,3};
/* The image ranges the seven read or write (rva, size). */
static const struct { uint32_t rva,size; } st2_ranges[]={
    {0x98514,1},{0x98640,8},{0x98eb0,0x18},{0x9a360,0x1c},{0xa09f0,0x24},{0xa96d0,0x3e},{0xa9712,0xfa},
    {0xa98ec,0x18},{0xa9ae8,8},{0xa9d06,0xa4},{0xa9dca,0x36},{0x2cbee4,4}};
static uint32_t st2_next(void){
    uint32_t z=st2_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *st2_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
/* A random dword outside the thread's 16 MiB stack reservation (bit 31
 * flipped), so that no random value can fall into the frame window of
 * st2_rel in one run and not in the other (the original's frame and the
 * core's scratch both lie on this stack). */
static uint32_t st2_value(void){
    uint32_t base;__asm__ volatile("movl %%fs:4,%0":"=r"(base));
    const uint32_t v=st2_next();return v-(base-0x1000000u)<0x1000000u?v^0x80000000u:v;
}
static uint32_t st2_fs0(void){uint32_t v;__asm__ volatile("movl %%fs:0,%0":"=r"(v));return v;}
static void st2_reset(void){st2_calls=0;st2_stream=st2_seed;st2_bump=ST2_A(ST2_BUMP);st2_head=st2_fs0();st2_last=0;}
static int st2_in(uint32_t a,uint32_t size){return a-ST2_B<ST2_SIZE&&size<=ST2_SIZE&&a-ST2_B+size<=ST2_SIZE;}
/* Inside the hook's own frame (FS:0 is its record at f-50h): f-60h..f+10h. */
static int st2_frame(uint32_t a,uint32_t size){
    const uint32_t fs=st2_fs0();
    return fs!=st2_head&&a-fs+0x10u<0x70u&&size<=0x70u&&a-fs+0x10u+size<=0x70u;
}
static int st2_writable(uint32_t a,uint32_t size){
    if(st2_in(a,size)||st2_frame(a,size))return 1;
    for(unsigned i=0;i<sizeof(st2_ranges)/sizeof(st2_ranges[0]);++i){
        const uint32_t base=(uint32_t)(uintptr_t)(mapped+st2_ranges[i].rva);
        if(a-base<st2_ranges[i].size&&size<=st2_ranges[i].size&&a-base+size<=st2_ranges[i].size)return 1;
    }
    return 0;
}
/* Values inside the hook's own frame as offsets from its record. */
static uint32_t st2_rel(uint32_t v){
    const uint32_t fs=st2_fs0();
    return fs!=st2_head&&v-fs+0x10u<0x70u?0xf0000000u|(v-fs+0x10u):v;
}
/* A count word: 0, 1..5 or (not in real runs) a small negative. */
static uint16_t st2_count(uint32_t pick){
    const unsigned k=(pick>>12)%8;
    if(k==0)return 0;
    if(k==1&&!st2_real)return (uint16_t)(0x10000u-1u-(pick>>16)%3);
    return (uint16_t)(1u+(pick>>16)%5);
}
static uint32_t st2_alloc(uint32_t size){
    if(size>0x200u)return ST2_A(ST2_FIXED);
    const uint32_t p=st2_bump;st2_bump+=(size+7u)&~3u;
    return st2_bump>ST2_A(ST2_FIXED)?ST2_A(ST2_FIXED):p;
}
/* The read: bounded data into the buffer; the 2-byte reads get a count
 * word (remembered for the scribbles), the 8-byte region prefix its link
 * count; sometimes the stream's table switches (the callers reread it). */
static void st2_read(const uint32_t *a,uint32_t pick){
    const uint32_t size=a[1],buf=a[2];
    if(!st2_real&&pick%9==0&&st2_writable(a[0],4))put32(st2_host(a[0]),ST2_A(pick&0x10?ST2_VT1:ST2_VT2));
    if(size==2&&st2_writable(buf,2)){put16(st2_host(buf),st2_count(pick));st2_last=buf;return;}
    if(size>0x200u||!st2_writable(buf,size))return;
    for(uint32_t i=0;i<size;++i)st2_host(buf)[i]=(BYTE)st2_next();
    if(size==8)put16(st2_host(buf+6),st2_count(pick));
}
/* What the stubs answer and change. */
static uint32_t st2_answer(unsigned id,const uint32_t *a,uint32_t eax,uint32_t pick){
    switch(id){
    case 3: /* 46a03c: the bitmap slots and the bullet shape list may change */
        if(!st2_real&&pick%3==0)put32(mapped+0xa9ae8,st2_value());
        if(!st2_real&&pick%5==1)put32(mapped+0xa9d72+8*((pick>>8)%7),st2_value());
        if(!st2_real&&pick%7==2)put32(mapped+0xa9700,ST2_A(ST2_SHAPES2));
        return eax;
    case 4: /* 48349c: the counter pointer may move */
        if(!st2_real&&pick%3==0)put32(mapped+0x2cbee4,ST2_A(pick&0x100?ST2_COUNTER2:ST2_COUNTER));
        return eax;
    case 5: /* 474cd8: one of the shape lists and a count */
        if(st2_writable(a[0],4))put32(st2_host(a[0]),ST2_A(pick&1?ST2_SHAPES:ST2_SHAPES2));
        return (st2_value()&0xffff0000u)|st2_count(pick);
    case 6: /* 472f14: the stream's table and its state (+4) */
        if(st2_writable(a[0],8)){put32(st2_host(a[0]),ST2_A(pick&1?ST2_VT2:ST2_VT1));put32(st2_host(a[0]+4),pick%4==0?(pick>>8)|1:0);}
        return a[0];
    case 8: case 18: case 19:
        st2_read(a,pick);return eax;
    case 9: /* 49667c: a block (enough for five weapon entries); the count may change */
        if(st2_real)return st2_alloc(a[1]*a[2]);
        if(st2_last&&pick%6==1)put16(st2_host(st2_last),st2_count(pick>>8));
        return st2_alloc(0x1c0u);
    case 10: /* weapon_entry: the weapon count may change */
        if(!st2_real&&pick%5==0)put16(mapped+0xa98f0,st2_count(pick>>8));
        if(st2_writable(a[0]+0x48u,2))put16(st2_host(a[0]+0x48u),(uint16_t)st2_next());
        return eax;
    case 12: /* 4730e8: may switch the stream's table */
        if(!st2_real&&pick%4==0&&st2_writable(a[0],4))put32(st2_host(a[0]),ST2_A(pick&0x10?ST2_VT1:ST2_VT2));
        return eax;
    case 14: /* new[]: the count word last read may change */
        if(!st2_real&&st2_last&&pick%6==1)put16(st2_host(st2_last),st2_count(pick>>8));
        return st2_alloc(a[0]);
    case 15: /* _memset */
        if(a[2]<=0x400u&&st2_writable(a[0],a[2]))memset(st2_host(a[0]),(int)(BYTE)a[1],a[2]);
        return eax;
    case 16: case 17: /* the entry readers: may switch the stream's table */
        if(!st2_real&&pick%4==0&&st2_writable(a[1],4))put32(st2_host(a[1]),ST2_A(pick&0x10?ST2_VT1:ST2_VT2));
        return eax;
    }
    return eax;
}
__attribute__((used,noinline)) static uint64_t __cdecl st2_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(st2_calls==ST2_MAX){fail("stage2_types call overflow");ExitProcess(1);}
    st2_record *v=st2_log+st2_calls++;memset(v,0,sizeof(*v));v->id=id;
    v->slots=weapons_bridge_slots(a,st2_counts[id]);
    for(unsigned k=0;k<st2_counts[id];++k)v->args[k]=st2_rel(a[k]);
    for(unsigned j=0;j<3;++j)v->regs[j]=st2_rel(regs[j]);
    if(id&&st2_fs0()!=st2_head)v->context=get16(st2_host(st2_fs0()+16u));
    uint32_t eax=st2_value(),edx=st2_value(),pick=st2_next();st2_ecx=st2_value();
    if(id==0)eax=st2_fs0(); /* the record stub returns the real FS:0 head */
    else eax=st2_answer(id,a,eax,pick);
    v->eax=st2_rel(eax);v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define ST2_STUB(n) __attribute__((naked)) static void st2_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _st2_stub_impl\n\taddl $24,%esp\n\tmovl _st2_ecx,%ecx\n\tret\n\t");}
ST2_STUB(1) ST2_STUB(2) ST2_STUB(3) ST2_STUB(4) ST2_STUB(5) ST2_STUB(6) ST2_STUB(7) ST2_STUB(8) ST2_STUB(9)
ST2_STUB(10) ST2_STUB(11) ST2_STUB(12) ST2_STUB(13) ST2_STUB(14) ST2_STUB(15) ST2_STUB(16) ST2_STUB(17)
ST2_STUB(18) ST2_STUB(19)
#undef ST2_STUB
/* 49630c: logs like the other stubs (EDX and ECX come back unchanged, as
 * the original's do), then links a record at EBP + [descriptor+4] into
 * FS:0 with the saved head at record+0 and a zero context word. */
__attribute__((naked)) static void st2_stub_0(void){__asm__ volatile(
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
    "pushl $0\n\tcalll _st2_stub_impl\n\taddl $12,%esp\n\t"
    "popl %ecx\n\tmovl 4(%ecx),%ecx\n\taddl %ebp,%ecx\n\tmovl %eax,(%ecx)\n\tmovw $0,16(%ecx)\n\tmovl %ecx,%fs:0\n\t"
    "popl %edx\n\tpopl %ecx\n\tret\n\t");}
static void *st2_stubs[ST2_IDS]={st2_stub_0,st2_stub_1,st2_stub_2,st2_stub_3,st2_stub_4,st2_stub_5,st2_stub_6,st2_stub_7,
    st2_stub_8,st2_stub_9,st2_stub_10,st2_stub_11,st2_stub_12,st2_stub_13,st2_stub_14,st2_stub_15,st2_stub_16,st2_stub_17,
    st2_stub_18,st2_stub_19};
static int st2_patch(BYTE saved[ST2_IDS][5],int active[ST2_IDS],uint32_t which,int on){
    for(unsigned i=0;i<ST2_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!st2_addresses[i])continue;if(actor_patch(st2_addresses[i]-0x400000u,st2_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(st2_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
/* Direct callees of each hook (bit i = st2_addresses[i]; the read method is
 * reached through the arena tables). */
static uint32_t st2_which(unsigned index){
    if(index==ES2_HOOK_INDEX_weapon_types)return 0x7fffu;
    if(index==ES2_HOOK_INDEX_weapon_entry)return 0x30000u;
    if(index==ES2_HOOK_INDEX_rocket_types)return 0x63e3u;
    if(index==ES2_HOOK_INDEX_bullet_types)return 0x63efu;
    if(index==ES2_HOOK_INDEX_beam_types)return 0x61dfu;
    if(index==ES2_HOOK_INDEX_mech_forms)return 0xa3dfu;
    return 0xa3c3u;   /* flyer_forms */
}
static void st2_globals(BYTE *out,int restore){
    for(unsigned i=0;i<sizeof(st2_ranges)/sizeof(st2_ranges[0]);++i){
        if(restore)memcpy(mapped+st2_ranges[i].rva,out,st2_ranges[i].size);else memcpy(out,mapped+st2_ranges[i].rva,st2_ranges[i].size);
        out+=st2_ranges[i].size;
    }
}
static int st2_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<ST2_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
/* The case data: the arena (tables, shape lists over shape objects, the
 * stream at ST2_STREAM), the image ranges randomized (the flag byte 0 one
 * time in two), the counter pointer into the arena. */
static void st2_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    st2_seed=random32();st2_stream=st2_seed^n;st2_real=real;st2_index=index;
    for(unsigned i=0;i<3;++i)r[i]=st2_value();
    for(unsigned i=0;i<ST2_SIZE;++i)st2_arena[i]=(BYTE)st2_next();
    put32(st2_arena+ST2_VT1+0x18,(uint32_t)(uintptr_t)st2_stub_18);
    put32(st2_arena+ST2_VT2+0x18,(uint32_t)(uintptr_t)st2_stub_19);
    for(unsigned k=0;k<32;++k)put32(st2_arena+ST2_SHAPES+4*k,ST2_A(ST2_OBJECTS+0x40*((k*7u)%16u)));   /* both lists */
    put32(st2_arena+ST2_STREAM,ST2_A(st2_next()%2?ST2_VT1:ST2_VT2));
    for(unsigned i=0;i<sizeof(st2_ranges)/sizeof(st2_ranges[0]);++i)
        for(unsigned j=0;j<st2_ranges[i].size;++j)mapped[st2_ranges[i].rva+j]=(BYTE)st2_next();
    /* the pointer tables the hooks pass on to logged calls */
    static const struct { uint32_t rva,size; } pointers[]={{0x98eb0,0x18},{0x9a360,0x1c},{0xa09f0,0x24},{0xa9ae8,8}};
    for(unsigned i=0;i<sizeof(pointers)/sizeof(pointers[0]);++i)
        for(unsigned j=0;j<pointers[i].size;j+=4)put32(mapped+pointers[i].rva+j,st2_value());
    if(st2_next()%2)mapped[0x98514]=0;
    put32(mapped+0x2cbee4,ST2_A(ST2_COUNTER));
    for(unsigned k=0;k<6;++k)a[k]=st2_value();
    if(index==ES2_HOOK_INDEX_weapon_entry){a[0]=ST2_A(ST2_ENTRY+2u*(n%3));a[1]=ST2_A(ST2_STREAM);}
    st2_stream=st2_seed;   /* the stubs' stream restarts here in every run */
}
/* Calls target with six argument slots, EAX/EDX/ECX = r, over 3,840 bytes
 * of stack filled with `fill`; out receives EAX, EDX and ECX after it. */
__attribute__((naked,noinline)) static void st2_invoke(void *target,const uint32_t *a,const uint32_t *r,uint32_t fill,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 20(%ebp),%eax\n\tmovl $1024,%ecx\n\tcld\n\trep stosl\n\taddl $3840,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 20(%esi)\n\tpushl 16(%esi)\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tcall *8(%ebp)\n\t"
        "movl 24(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* The core with the adapter's graph and frame; FS:0 restored to the
 * record's saved head (EAX or EDX) as the adapters do. */
static uint64_t st2_core(unsigned index,const uint32_t *a,const uint32_t *r,uint32_t *ecx){
    const es2_guest_memory *m=&math_native_memory;
    if(index==ES2_HOOK_INDEX_weapon_entry){
        es2_weapon_graph g=weapons_graph(mapped,NULL,index);
        return es2_weapon_entry_registers(m,&g,a[0],a[1],r,ecx);
    }
    BYTE scratch[ES2_STAGE2_TYPES_SCRATCH]={0};es2_weapon_graph g=weapons_graph(mapped,scratch,index);
    weapons_impact_context c={(uint32_t)(uintptr_t)mapped,(uint32_t)(uintptr_t)(scratch+ES2_STAGE2_TYPES_FRAME)};
    g.call=weapons_impact_guest_call;g.context=&c;
    uint64_t v;unsigned shift=32;
    switch(index){
    case ES2_HOOK_INDEX_weapon_types:v=es2_weapon_types_registers(m,&g,r,ecx);shift=0;break;
    case ES2_HOOK_INDEX_rocket_types:v=es2_rocket_types_registers(m,&g,r,ecx);shift=0;break;
    case ES2_HOOK_INDEX_bullet_types:v=es2_bullet_types_registers(m,&g,r,ecx);shift=0;break;
    case ES2_HOOK_INDEX_beam_types:v=es2_beam_types_registers(m,&g,r,ecx);break;
    case ES2_HOOK_INDEX_mech_forms:v=es2_mech_forms_registers(m,&g,r,ecx);break;
    default:v=es2_flyer_forms_registers(m,&g,r,ecx);break;
    }
    uint32_t head=(uint32_t)(v>>shift);__asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");return v;
}
static int st2_report(const char *what,unsigned n,const uint32_t want[3],const uint32_t got[3],unsigned calls,const st2_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x calls %u/%u\n",current_function,n,what,
        want[0],got[0],want[1],got[1],want[2],got[2],calls,st2_calls);
    for(unsigned j=0;j<calls&&j<st2_calls&&expected;++j)if(memcmp(expected+j,st2_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(st2_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<ST2_SIZE;++j)if(st2_after[j]!=st2_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,st2_after[j],st2_arena[j]);break;}
    return 1;
}
static unsigned st2_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<st2_calls;++j){
        const unsigned id=st2_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?(id==18||id==19)&&c->key==0x18:st2_addresses[id]&&st2_addresses[id]==c->key)++hits;
    }
    return hits;
}
/* One direct case: the original, the adapter and the core. */
static int st2_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r,uint32_t fill){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[ST2_GLOBALS],after[ST2_GLOBALS],actual[ST2_GLOBALS];static st2_record expected[ST2_MAX];uint32_t want[3],got[3];
    const uint32_t head=st2_fs0();
    memcpy(st2_before,st2_arena,ST2_SIZE);st2_globals(before,0);
    st2_reset();st2_invoke(mapped+d->rva,a,r,fill,want);const unsigned calls=st2_calls;memcpy(expected,st2_log,calls*sizeof(*expected));
    memcpy(st2_after,st2_arena,ST2_SIZE);st2_globals(after,0);
    if(st2_fs0()!=head)return fail("stage2_types original left FS:0 changed");
    for(unsigned j=0;j<calls;++j)++st2_paths[expected[j].id];
    /* the adapter (the framework's entry: six argument slots) */
    memcpy(st2_arena,st2_before,ST2_SIZE);st2_globals(before,1);st2_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];
    if(count>16)return fail("stage2_types counter table too long");
    for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    const uint64_t pair=x->replacement_context(a,d->user,r);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);got[2]=want[2];
    st2_globals(actual,0);
    if(st2_fs0()!=head)return fail("stage2_types adapter left FS:0 changed");
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=st2_expected_hits(c+i))return fail("stage2_types callee counter delta");
    if(want[0]!=got[0]||want[1]!=got[1]||calls!=st2_calls||memcmp(expected,st2_log,calls*sizeof(*expected))||
       memcmp(st2_after,st2_arena,ST2_SIZE)||st2_globals_differ(after,actual))
        return st2_report("adapter",n,want,got,calls,expected);
    /* the core with the ECX it reports */
    memcpy(st2_arena,st2_before,ST2_SIZE);st2_globals(before,1);st2_reset();
    got[2]=~want[2];
    const uint64_t core=st2_core(index,a,r,&got[2]);got[0]=(uint32_t)core;got[1]=(uint32_t)(core>>32);st2_globals(actual,0);
    if(st2_fs0()!=head)return fail("stage2_types core left FS:0 changed");
    /* With the hook installed (integration pass) the "original" run is the
       hook, whose ECX is the framework's; ECX is compared otherwise. */
    const int ecx_known=!rng_hooks[index].installed;
    if(want[0]!=got[0]||want[1]!=got[1]||(ecx_known&&want[2]!=got[2])||calls!=st2_calls||memcmp(expected,st2_log,calls*sizeof(*expected))||
       memcmp(st2_after,st2_arena,ST2_SIZE)||st2_globals_differ(after,actual))
        return st2_report("core ECX",n,want,got,calls,expected);
    st2_globals(before,1);
    return 0;
}
static int st2_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],want[3]={0},got[3]={0},pattern=poison?0xa7a7a7a7u:0x31313131u;const uint32_t head=st2_fs0();st2_build(index,n*37u+11u,a,r,0);
        BYTE before[ST2_GLOBALS],after[ST2_GLOBALS],actual[ST2_GLOBALS],expected_fpu[108],actual_fpu[108];static st2_record expected[ST2_MAX];
        memcpy(st2_before,st2_arena,ST2_SIZE);st2_globals(before,0);
        st2_reset();sg_poison_call(mapped+d->rva,a,pattern,want);es2_hook_fpu_save(expected_fpu);
        const unsigned calls=st2_calls;memcpy(expected,st2_log,calls*sizeof(*expected));memcpy(st2_after,st2_arena,ST2_SIZE);st2_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("stage2_types poison install");
        memcpy(st2_arena,st2_before,ST2_SIZE);st2_globals(before,1);st2_reset();sg_poison_call(mapped+d->rva,a,pattern,got);
        es2_hook_fpu_save(actual_fpu);st2_globals(actual,0);
        if(st2_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||calls!=st2_calls||memcmp(expected,st2_log,calls*sizeof(*expected))||
           memcmp(st2_after,st2_arena,ST2_SIZE)||st2_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||
           memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=st2_report(fast?"installed fast":"installed preserving",n,want,got,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("stage2_types poison uninstall");st2_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, pair/trace/arena/globals/FS:0\n",d->name,total);
    return status;
}
/* ---- real callees ---- */
static unsigned st2_nested(unsigned index,unsigned nested[3]){
    if(index==ES2_HOOK_INDEX_weapon_types){nested[0]=ES2_HOOK_INDEX_weapon_entry;return 1;}
    if(index==ES2_HOOK_INDEX_weapon_entry){nested[0]=ES2_HOOK_INDEX_dmg_region_read;nested[1]=ES2_HOOK_INDEX_collision_volume;
        nested[2]=ES2_HOOK_INDEX_collision_points;return 3;}
    return 0;
}
/* Stubbed in the real-callee pass: every callee except 49630c, 49667c,
 * _memset and the nested hooks (weapon_entry under weapon_types;
 * dmg_region_read and collision_volume under weapon_entry); new[] is a
 * bump allocator. */
static uint32_t st2_real_which(unsigned index){
    if(index==ES2_HOOK_INDEX_weapon_entry)return 1u<<14;
    return 0x379feu;   /* ids 1..8, 11..14, 16, 17 */
}
static int st2_real_pass(unsigned index){
    unsigned nested[3];const unsigned count=st2_nested(index,nested);
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[4]={0};
    BYTE saved[ST2_IDS][5];int active[ST2_IDS];
    if(st2_patch(saved,active,st2_real_which(index),1))return 1;
    static st2_record expected[ST2_MAX];
    const unsigned modes=2u<<count;
    for(unsigned mode=0;mode<modes&&!status;++mode)for(unsigned n=0;n<2048u/modes&&!status;++n){
        uint32_t a[6],r[3],want[3]={0},got[3]={0},pattern=n&1?0xa7a7a7a7u:0x31313131u;
        st2_build(index,n*13u+mode,a,r,1);
        const uint32_t regs[3]={pattern,pattern,pattern};const uint32_t head=st2_fs0();
        BYTE before[ST2_GLOBALS],after[ST2_GLOBALS],actual[ST2_GLOBALS];
        memcpy(st2_before,st2_arena,ST2_SIZE);st2_globals(before,0);
        es2_hook hk[4];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(mode>>(1+j)&1)?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,function_hook_extension(nested[j])))return fail("stage2_types nested install");}
        st2_reset();st2_invoke(mapped+d->rva,a,regs,pattern,want);
        const unsigned calls=st2_calls;memcpy(expected,st2_log,calls*sizeof(*expected));
        memcpy(st2_after,st2_arena,ST2_SIZE);st2_globals(after,0);
        o.mode=mode&1?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(hk+3,d,&o,function_hook_extension(index)))return fail("stage2_types caller install");
        memcpy(st2_arena,st2_before,ST2_SIZE);st2_globals(before,1);st2_reset();
        st2_invoke(mapped+d->rva,a,regs,pattern,got);st2_globals(actual,0);
        if(st2_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||calls!=st2_calls||memcmp(expected,st2_log,calls*sizeof(*expected))||
           memcmp(st2_after,st2_arena,ST2_SIZE)||st2_globals_differ(after,actual)){
            fprintf(stderr,"real mode=%u\n",mode);status=st2_report("real",n,want,got,calls,expected);
        }
        for(unsigned j=0;j<4;++j){if(!hk[j].descriptor)continue;entered[j]+=hk[j].calls;
            if(hk[j].mismatches)status=fail("stage2_types real compare mismatch");
            if(!es2_hook_uninstall(hk+j))return fail("stage2_types real uninstall");}
        st2_globals(before,1);++total;
    }
    if(st2_patch(saved,active,0,0))return 1;
    st2_real=0;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&!entered[3]){fprintf(stderr,"FAIL: %s never entered\n",d->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, real 49630c/49667c/_memset, %u nested hooks original/replaced, hook original/replaced, "
        "31/a7 stack, pair, calls, arena, globals and FS:0\n",d->name,total,count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
static int stage2_types_cases(unsigned index,unsigned limit){
    current_function=function_hook_descriptor(index)->name;
    const uint32_t which=st2_which(index);
    BYTE saved[ST2_IDS][5],old_globals[ST2_GLOBALS];int active[ST2_IDS];
    st2_globals(old_globals,0);
    if(st2_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_STAGE2_TYPES_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);memset(st2_paths,0,sizeof(st2_paths));
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];st2_build(index,n,a,r,0);const uint32_t fill=n%3==0?0:st2_next();
        status=st2_compare(index,n,a,r,fill);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    st2_globals(old_globals,1);
    if(!limit&&!status)status=st2_installed_probes(index,64);
    if(st2_patch(saved,active,which,0))return 1;
    st2_globals(old_globals,1);
    if(!limit&&!status)status=st2_real_pass(index);
    st2_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases (stub calls 49630c %u, 492bec %u, 45cdf4 %u, 46a03c %u, 48349c %u, 474cd8 %u, 472f14 %u, "
        "479c78 %u, stream_read %u, 49667c %u, weapon_entry %u, stream_close %u, stream_open %u, stream_destroy %u, new[] %u, memset %u, "
        "dmg_region_read %u, collision_volume %u, read %u/%u); "
        "full return pair and ECX, ordered calls with entry registers, slots and context word, arena/globals/FS:0\n",
        function_hook_descriptor(index)->name,cases,st2_paths[0],st2_paths[1],st2_paths[2],st2_paths[3],st2_paths[4],st2_paths[5],
        st2_paths[6],st2_paths[7],st2_paths[8],st2_paths[9],st2_paths[10],st2_paths[11],st2_paths[12],st2_paths[13],st2_paths[14],
        st2_paths[15],st2_paths[16],st2_paths[17],st2_paths[18],st2_paths[19]);
    return status;
}
