/* damage_data group: dmg_load 40d160, dmg_read 40d09c, dmg_region_read 40cff8,
 * dmg_region_copy 40d050, dmg_capacity_set 40d128, dmg_region_set 40d240,
 * dmg_part_store 40d284, col_part_collect 40cb84, col_part_gather 40cafc,
 * damage_state_ctor 40d2cc and damage_state_bind 40d354
 * (docs/reverse-engineering/mission_load.md, "Damage data and part tables").
 *
 * Direct cases run the original body (six argument slots, EAX/EDX/ECX at
 * entry from the case, over a stack filled with a case pattern), the
 * adapter (with the callee counter deltas) and the core (with the ECX it
 * reports) against logging stubs for every direct game callee of the hook
 * under test: 49630c (a record stub: the real FS:0 head at record+0,
 * context word +10h zeroed, the record linked into FS:0), the stream
 * constructor 472f14 (the stream's table, a state that is nonzero one time
 * in four), the error report 479c78, dmg_read, stream_destroy 472f64,
 * new[] 4774f0, dmg_region_read, new 47749c (0 one time in four for
 * damage_state_bind), _memset 49681a and _memcpy 49680e (both fill/copy
 * inside the arena), dmg_region_copy, col_part_gather and two arena tables
 * whose slot +18h is a read stub (bounded data and count words; it may
 * switch the stream's table). Frame addresses (dmg_load's stream at f-30h)
 * are logged relative to the hook's record (FS:0), so the original's stack
 * frame and the core's scratch frame compare equal. Compared: the full
 * return pair, ECX, FS:0, the ordered calls with their argument slots (and
 * the bridge's slot count), entry EAX/EDX/ECX and the record's context
 * word, the arena and the globals 498854, 4a9828, 498864 and 6cbee4. The
 * stubs change what the originals read after a call (count words and the
 * stream's table after a read or new[], the region count after
 * dmg_region_read, the region array after dmg_region_copy, the list count
 * after col_part_gather), new[] and new return blocks over the object under
 * construction, and the outputs overlap the inputs (capacities over the
 * index or value list, the part stores over the word and dword lists, the
 * gathered ids over the COL record's owner and count words or over a later
 * entry, the owners over the entries pointer), so the order of reads and
 * writes is observable.
 *
 * Then installed 31/a7 poison probes and, for the hooks with callees, a
 * real-callee pass: the real 49630c, _memcpy and _memset, a bump allocator
 * for new and new[], the stream constructor, the error report and
 * stream_destroy as stubs, the group's nested hooks (dmg_read and
 * dmg_region_read under dmg_load, dmg_region_read under dmg_read,
 * dmg_region_copy under dmg_region_set, col_part_gather under
 * col_part_collect) in rotating original/replaced modes, the hook under
 * test original or replaced against a run without it. Identifiers use the
 * prefix dmd_/DMD_. */
typedef struct dmd_record { uint32_t id,slots,args[6],regs[3],context,eax,edx; } dmd_record;
enum {
    DMD_SIZE=0x4000, DMD_MAX=160, DMD_IDS=14,
    DMD_STREAM=0x100, DMD_VT1=0x140, DMD_VT2=0x180, DMD_NAME=0x1c0, DMD_COUNTER=0x1f0, DMD_SHARED=0x200,
    DMD_SLOTS=0x300, DMD_TABLE=0x600, DMD_DATA=0x800, DMD_ROWS=0x1000, DMD_ROWS2=0x1200, DMD_SRC=0x1400,
    DMD_LINKS=0x1600, DMD_ENTRIES=0x1800, DMD_OUT=0x1c00, DMD_BUMP=0x2000, DMD_FIXED=0x3c00,
    DMD_GLOBALS=2+4+4+4
};
#define DMD_B ((uint32_t)(uintptr_t)dmd_arena)
#define DMD_A(o) (DMD_B+(uint32_t)(o))
static BYTE dmd_arena[DMD_SIZE],dmd_before[DMD_SIZE],dmd_after[DMD_SIZE];
static dmd_record dmd_log[DMD_MAX];
static unsigned dmd_calls,dmd_real,dmd_index,dmd_paths[DMD_IDS];
static uint32_t dmd_seed,dmd_stream,dmd_bump,dmd_head,dmd_table,dmd_scribble,dmd_special;
__attribute__((used)) static uint32_t dmd_ecx;
/* 0 49630c, 1 472f14, 2 479c78, 3 dmg_read 40d09c, 4 stream_destroy
 * 472f64, 5 new[] 4774f0, 6 dmg_region_read 40cff8, 7 new 47749c, 8
 * _memset 49681a, 9 _memcpy 49680e, 10 dmg_region_copy 40d050, 11
 * col_part_gather 40cafc, 12/13 the read method of the two arena tables
 * (slot +18h); the slots each original call site pushes. */
static const uint32_t dmd_addresses[DMD_IDS]={0x49630c,0x472f14,0x479c78,0x40d09c,0x472f64,0x4774f0,0x40cff8,0x47749c,
    0x49681a,0x49680e,0x40d050,0x40cafc,0,0};
static const unsigned dmd_counts[DMD_IDS]={0,2,6,2,2,1,2,1,3,3,2,4,3,3};
/* Never a word on the thread's stack (16 MB below StackBase): dmd_rel turns values just above
 * the FS:0 record into frame offsets, and the records of the two phases differ. */
static uint32_t dmd_next(void){
    const uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;uint32_t z;
    do {
        z=dmd_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;z^=z>>16;
    } while(top-z-1u<0x1000000u);
    return z;
}
static BYTE *dmd_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static uint32_t dmd_fs0(void){uint32_t v;__asm__ volatile("movl %%fs:0,%0":"=r"(v));return v;}
static void dmd_reset(void){dmd_calls=0;dmd_stream=dmd_seed;dmd_bump=DMD_A(DMD_BUMP);dmd_head=dmd_fs0();}
/* An arena range [a, a+size). */
static int dmd_in(uint32_t a,uint32_t size){return a-DMD_B<DMD_SIZE&&size<=DMD_SIZE&&a-DMD_B+size<=DMD_SIZE;}
/* Values inside the hook's own frame (FS:0 is its record) as offsets. */
static uint32_t dmd_rel(uint32_t v){
    const uint32_t fs=dmd_fs0();
    return fs!=dmd_head&&v-fs<0x60u?0xf0000000u|(v-fs):v;
}
/* A count word: 0, 1..5 or (not in real runs) a small negative. */
static uint16_t dmd_count(uint32_t pick){
    const unsigned k=(pick>>12)%8;
    if(k==0)return 0;
    if(k==1&&!dmd_real)return (uint16_t)(0x10000u-1u-(pick>>16)%3);
    return (uint16_t)(1u+(pick>>16)%5);
}
static uint32_t dmd_alloc(uint32_t size){
    if(size>0x200u)return DMD_A(DMD_FIXED);
    const uint32_t p=dmd_bump;dmd_bump+=(size+7u)&~3u;
    return dmd_bump>DMD_A(DMD_FIXED)?DMD_A(DMD_FIXED):p;
}
/* The read method: bounded data into the buffer; count words for the
 * 2-byte reads and the 8-byte region prefix; sometimes the stream's table
 * switches (the callers reread it). */
static void dmd_read(const uint32_t *a,uint32_t pick){
    const uint32_t size=a[1],buf=a[2];
    if(!dmd_real&&pick%9==0&&dmd_in(a[0],4))put32(dmd_host(a[0]),DMD_A(pick&0x10?DMD_VT1:DMD_VT2));
    if(size>0x40u||!dmd_in(buf,size))return;
    for(uint32_t i=0;i<size;++i)dmd_host(buf)[i]=(BYTE)dmd_next();
    if(size==2)put16(dmd_host(buf),dmd_count(pick));
    else if(size==8)put16(dmd_host(buf+6),dmd_count(pick));
}
/* What the stubs answer and change. */
static uint32_t dmd_answer(unsigned id,const uint32_t *a,uint32_t eax,uint32_t pick){
    switch(id){
    case 1: /* 472f14: the stream's table and its state (+4) */
        put32(dmd_host(a[0]),DMD_A(pick&1?DMD_VT2:DMD_VT1));
        put32(dmd_host(a[0]+4),pick%4==0?(pick>>8)|1:0);
        return a[0];
    case 3: /* dmg_read: scribbles on the slot */
        if(dmd_in(a[0],4))put32(dmd_host(a[0]),dmd_next());
        return eax;
    case 5: /* new[] */
        if(!dmd_real&&dmd_scribble&&pick%6==1)put16(dmd_host(dmd_scribble),(uint16_t)((pick>>8)%7));
        if(!dmd_real&&dmd_special&&pick%8==0&&dmd_index!=ES2_HOOK_INDEX_damage_state_bind)return dmd_special+4u*((pick>>8)%3);
        return dmd_alloc(a[0]);
    case 6: /* dmg_region_read: the descriptor's region count may change */
        if(!dmd_real&&pick%5==0)put16(dmd_host(dmd_table+8),(uint16_t)((pick>>8)%5));
        if(dmd_in(a[0]+6,2)&&pick%3==0)put16(dmd_host(a[0]+6),(uint16_t)dmd_next());
        return eax;
    case 7: /* new: 0 one time in four, or a block over the state */
        if(!dmd_real&&pick%4==0)return 0;
        if(!dmd_real&&pick%8==1)return dmd_table+0x10u;
        return dmd_alloc(a[0]);
    case 8: /* _memset */
        if(a[2]<=0x400u&&dmd_in(a[0],a[2]))memset(dmd_host(a[0]),(int)(BYTE)a[1],a[2]);
        return eax;
    case 9: /* _memcpy */
        if(a[2]<=0x400u&&dmd_in(a[0],a[2])&&dmd_in(a[1],a[2]))memmove(dmd_host(a[0]),dmd_host(a[1]),a[2]);
        return eax;
    case 10: /* dmg_region_copy: the region array may move */
        if(pick%5==0)put32(dmd_host(dmd_table),DMD_A(DMD_ROWS2));
        return eax;
    case 11: /* col_part_gather: a gathered count; the list count may change */
        if(pick%7==0)put16(dmd_host(dmd_table),(uint16_t)((pick>>8)%4));
        return pick%4?(eax&0xffff0000u)|((pick>>4)%5):eax;
    case 12: case 13:
        dmd_read(a,pick);return eax;
    }
    return eax;
}
__attribute__((used,noinline)) static uint64_t __cdecl dmd_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(dmd_calls==DMD_MAX){fail("damage_data call overflow");ExitProcess(1);}
    dmd_record *v=dmd_log+dmd_calls++;memset(v,0,sizeof(*v));v->id=id;
    v->slots=weapons_bridge_slots(a,dmd_counts[id]);
    for(unsigned k=0;k<dmd_counts[id];++k)v->args[k]=dmd_rel(a[k]);
    for(unsigned j=0;j<3;++j)v->regs[j]=dmd_rel(regs[j]);
    if(id&&dmd_fs0()!=dmd_head)v->context=get16(dmd_host(dmd_fs0()+16u));
    uint32_t eax=dmd_next(),edx=dmd_next(),pick=dmd_next();dmd_ecx=dmd_next();
    if(id==0)eax=dmd_fs0(); /* the record stub returns the real FS:0 head */
    else eax=dmd_answer(id,a,eax,pick);
    v->eax=dmd_rel(eax);v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define DMD_STUB(n) __attribute__((naked)) static void dmd_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _dmd_stub_impl\n\taddl $24,%esp\n\tmovl _dmd_ecx,%ecx\n\tret\n\t");}
DMD_STUB(1) DMD_STUB(2) DMD_STUB(3) DMD_STUB(4) DMD_STUB(5) DMD_STUB(6) DMD_STUB(7) DMD_STUB(8) DMD_STUB(9)
DMD_STUB(10) DMD_STUB(11) DMD_STUB(12) DMD_STUB(13)
#undef DMD_STUB
/* 49630c: logs like the other stubs (EDX and ECX come back unchanged, as
 * the original's do), then links a record at EBP + [descriptor+4] into
 * FS:0 with the saved head at record+0 and a zero context word. */
__attribute__((naked)) static void dmd_stub_0(void){__asm__ volatile(
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
    "pushl $0\n\tcalll _dmd_stub_impl\n\taddl $12,%esp\n\t"
    "popl %ecx\n\tmovl 4(%ecx),%ecx\n\taddl %ebp,%ecx\n\tmovl %eax,(%ecx)\n\tmovw $0,16(%ecx)\n\tmovl %ecx,%fs:0\n\t"
    "popl %edx\n\tpopl %ecx\n\tret\n\t");}
static void *dmd_stubs[DMD_IDS]={dmd_stub_0,dmd_stub_1,dmd_stub_2,dmd_stub_3,dmd_stub_4,dmd_stub_5,dmd_stub_6,dmd_stub_7,
    dmd_stub_8,dmd_stub_9,dmd_stub_10,dmd_stub_11,dmd_stub_12,dmd_stub_13};
static int dmd_patch(BYTE saved[DMD_IDS][5],int active[DMD_IDS],uint32_t which,int on){
    for(unsigned i=0;i<DMD_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!dmd_addresses[i])continue;if(actor_patch(dmd_addresses[i]-0x400000u,dmd_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(dmd_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
/* Direct callees of each hook (bit i = dmd_addresses[i]; the read method
 * is reached through the arena tables). */
static uint32_t dmd_which(unsigned index){
    if(index==ES2_HOOK_INDEX_dmg_load)return 0x1fu;
    if(index==ES2_HOOK_INDEX_dmg_read)return 0x60u;
    if(index==ES2_HOOK_INDEX_dmg_region_read)return 0x20u;
    if(index==ES2_HOOK_INDEX_dmg_region_copy)return 0x220u;
    if(index==ES2_HOOK_INDEX_dmg_region_set)return 0x400u;
    if(index==ES2_HOOK_INDEX_col_part_collect)return 0x800u;
    if(index==ES2_HOOK_INDEX_damage_state_ctor)return 0x120u;
    if(index==ES2_HOOK_INDEX_damage_state_bind)return 0xa1u;
    return 0;   /* dmg_capacity_set, dmg_part_store, col_part_gather: no calls */
}
/* The slot counter word, the slot array, the shared block, the
 * construction counter pointer. */
static const struct { uint32_t rva,size; } dmd_ranges[]={{0x98854,2},{0xa9828,4},{0x98864,4},{0x2cbee4,4}};
static void dmd_globals(BYTE *out,int restore){
    for(unsigned i=0;i<sizeof(dmd_ranges)/sizeof(dmd_ranges[0]);++i){
        if(restore)memcpy(mapped+dmd_ranges[i].rva,out,dmd_ranges[i].size);else memcpy(out,mapped+dmd_ranges[i].rva,dmd_ranges[i].size);
        out+=dmd_ranges[i].size;
    }
}
static int dmd_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<DMD_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
static void dmd_random(unsigned offset,unsigned size){for(unsigned i=0;i<size;++i)dmd_arena[offset+i]=(BYTE)dmd_next();}
/* A loop count word with a random high word: 0, negative, or 1..max. */
static uint32_t dmd_loop(unsigned max){
    const unsigned k=dmd_next()%10;const uint32_t high=dmd_next()%2?dmd_next()<<16:0;
    if(k==0)return high;
    if(k==1&&!dmd_real)return high|(uint16_t)(0x10000u-1u-dmd_next()%3);
    return high|(1u+dmd_next()%max);
}
static uint16_t dmd_small(int lo,int hi){return (uint16_t)(int16_t)(lo+(int)(dmd_next()%(unsigned)(hi-lo+1)));}
/* The case data. real: inputs whose sizes the real _memcpy/_memset and the
 * bump allocator can serve (nonnegative counts, arena pointers). */
static void dmd_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    dmd_seed=random32();dmd_stream=dmd_seed^n;dmd_real=real;dmd_index=index;
    for(unsigned i=0;i<3;++i)r[i]=dmd_next();
    dmd_random(0,DMD_SIZE);
    dmd_scribble=0;dmd_special=0;
    put32(dmd_arena+DMD_VT1+0x18,(uint32_t)(uintptr_t)dmd_stub_12);
    put32(dmd_arena+DMD_VT2+0x18,(uint32_t)(uintptr_t)dmd_stub_13);
    put32(dmd_arena+DMD_STREAM,DMD_A(dmd_next()%2?DMD_VT1:DMD_VT2));
    put32(mapped+0x2cbee4,DMD_A(DMD_COUNTER));
    put16(mapped+0x98854,dmd_small(-8,15));
    put32(mapped+0xa9828,DMD_A(DMD_SLOTS));
    put32(mapped+0x98864,dmd_next()%2?0:DMD_A(DMD_SHARED));
    const uint32_t T=DMD_A(DMD_TABLE+2u*(n%3));dmd_table=T;
    memset(a,0,24);
    if(index==ES2_HOOK_INDEX_dmg_load){
        a[0]=dmd_next()%4?DMD_A(DMD_NAME):dmd_next();
    } else if(index==ES2_HOOK_INDEX_dmg_read||index==ES2_HOOK_INDEX_dmg_region_read){
        a[0]=T;a[1]=DMD_A(DMD_STREAM);
        if(!real)dmd_scribble=index==ES2_HOOK_INDEX_dmg_read?T+0xau:T+6u;
    } else if(index==ES2_HOOK_INDEX_dmg_region_copy){
        const uint32_t R=DMD_A(DMD_SRC);
        put16(dmd_arena+DMD_SRC+6,real?dmd_small(0,5):dmd_small(-2,6));
        put32(dmd_arena+DMD_SRC+8,real||dmd_next()%8?DMD_A(DMD_LINKS):dmd_next());
        a[0]=!real&&dmd_next()%4==0?R+2u:T;a[1]=R;
        if(!real){dmd_scribble=a[0]+6u;dmd_special=a[0]+0xcu;}
    } else if(index==ES2_HOOK_INDEX_dmg_capacity_set){
        const uint32_t I=DMD_A(DMD_DATA),V=DMD_A(DMD_DATA+0x80);const unsigned variant=dmd_next()%4;
        for(unsigned k=0;k<16;++k){put16(dmd_arena+DMD_DATA+2*k,dmd_small(-8,30));
            put16(dmd_arena+DMD_DATA+0x80+2*k,variant==0?dmd_small(-8,30):(uint16_t)dmd_next());}
        put32(dmd_host(T+4),variant==0?I-4u:variant==1?V-6u:DMD_A(DMD_DATA+0x100));
        a[0]=T;a[1]=I;a[2]=V;a[3]=dmd_loop(12);
    } else if(index==ES2_HOOK_INDEX_dmg_region_set){
        const uint32_t I=DMD_A(DMD_DATA),P=DMD_A(DMD_DATA+0x80);
        put32(dmd_host(T),DMD_A(DMD_ROWS));
        for(unsigned k=0;k<8;++k){
            put16(dmd_arena+DMD_DATA+2*k,real?dmd_small(0,8):dmd_small(-3,8));
            put32(dmd_arena+DMD_DATA+0x80+4*k,real||dmd_next()%4?DMD_A(DMD_SRC+0x20*k):dmd_next());
            put16(dmd_arena+DMD_SRC+0x20*k+6,dmd_small(0,4));put32(dmd_arena+DMD_SRC+0x20*k+8,DMD_A(DMD_LINKS+4*k));
        }
        a[0]=T;a[1]=I;a[2]=P;a[3]=dmd_loop(8);
    } else if(index==ES2_HOOK_INDEX_dmg_part_store){
        const uint32_t I=DMD_A(DMD_DATA);const unsigned variant=dmd_next()%3;
        put32(dmd_host(T),DMD_A(DMD_ROWS));
        for(unsigned k=0;k<8;++k)put16(dmd_arena+DMD_DATA+2*k,dmd_small(-3,8));
        a[0]=T;a[1]=dmd_loop(8);a[2]=I;
        a[3]=variant==0?DMD_A(DMD_ROWS+18*2+2):DMD_A(DMD_DATA+0x40);
        a[4]=variant==1?DMD_A(DMD_ROWS+18*4):variant==2?DMD_A(DMD_ROWS+18*3+2):DMD_A(DMD_DATA+0x80);
    } else if(index==ES2_HOOK_INDEX_col_part_gather){
        const uint32_t E=DMD_A(DMD_DATA+0x300);const unsigned variant=dmd_next()%6;
        put16(dmd_host(T+4),(uint16_t)dmd_loop(12));put32(dmd_host(T+6),E);
        if(variant<=1)for(unsigned k=0;k<12;++k)put16(dmd_arena+DMD_DATA+0x300+16*k+6,dmd_small(0,12));
        a[0]=T;a[1]=DMD_A(DMD_OUT);a[2]=DMD_A(DMD_OUT+0x40);a[3]=DMD_A(DMD_OUT+0x80);
        if(variant==0)a[1]=T;
        else if(variant==1)a[1]=T+4u;
        else if(variant==2)a[2]=T+6u;
        else if(variant==3)a[1]=E+0x26u;
    } else if(index==ES2_HOOK_INDEX_col_part_collect){
        put16(dmd_host(T),(uint16_t)dmd_loop(6));put32(dmd_host(T+2),DMD_A(DMD_DATA+0x300));
        for(unsigned k=0;k<6;++k){
            BYTE *C=dmd_arena+DMD_DATA+0x300+10*k;
            put16(C+4,real?dmd_small(0,4):(uint16_t)dmd_next());put32(C+6,DMD_A(DMD_ENTRIES+64*k));
        }
        a[0]=T;
        a[1]=real||dmd_next()%2?DMD_A(DMD_OUT):dmd_next();a[2]=real||dmd_next()%2?DMD_A(DMD_OUT+0x40):dmd_next();
        a[3]=real||dmd_next()%2?DMD_A(DMD_OUT+0x80):dmd_next();
    } else if(index==ES2_HOOK_INDEX_damage_state_ctor){
        a[0]=T;
        a[1]=(dmd_next()%2?dmd_next()<<16:0)|(real||dmd_next()%8?dmd_small(0,40):dmd_small(-3,-1));
        a[2]=(dmd_next()%2?dmd_next()<<16:0)|(real||dmd_next()%8?dmd_small(0,40):dmd_small(-3,-1));
        if(!real)dmd_special=T;
    } else {   /* damage_state_bind */
        a[0]=T;a[1]=dmd_next();a[2]=dmd_next();a[3]=dmd_next();
    }
    dmd_stream=dmd_seed;   /* the stubs' stream restarts here in every run */
}
/* Calls target with six argument slots, EAX/EDX/ECX = r, over 3,840 bytes
 * of stack filled with `fill`; out receives EAX, EDX and ECX after it. */
__attribute__((naked,noinline)) static void dmd_invoke(void *target,const uint32_t *a,const uint32_t *r,uint32_t fill,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 20(%ebp),%eax\n\tmovl $1024,%ecx\n\tcld\n\trep stosl\n\taddl $3840,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 20(%esi)\n\tpushl 16(%esi)\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tcall *8(%ebp)\n\t"
        "movl 24(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* The core with the adapter's graph and frame; FS:0 restored to the
 * record's saved head (EDX) as the adapters do. */
static uint64_t dmd_core(unsigned index,const uint32_t *a,const uint32_t *r,uint32_t *ecx){
    const es2_guest_memory *m=&math_native_memory;
    if(index==ES2_HOOK_INDEX_dmg_load||index==ES2_HOOK_INDEX_damage_state_bind){
        BYTE scratch[ES2_DMG_LOAD_SCRATCH]={0};es2_weapon_graph g=weapons_graph(mapped,scratch,index);
        const int load=index==ES2_HOOK_INDEX_dmg_load;
        weapons_impact_context c={(uint32_t)(uintptr_t)mapped,(uint32_t)(uintptr_t)(scratch+(load?ES2_DMG_LOAD_FRAME:ES2_DAMAGE_BIND_FRAME))};
        g.call=weapons_impact_guest_call;g.context=&c;
        const uint64_t v=load?es2_dmg_load_registers(m,&g,a[0],r,ecx):es2_damage_state_bind_registers(m,&g,a[0],a[1],a[2],a[3],r,ecx);
        uint32_t head=(uint32_t)(v>>32);__asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");return v;
    }
    es2_weapon_graph g=weapons_graph(mapped,NULL,index);
    switch(index){
    case ES2_HOOK_INDEX_dmg_read:return es2_dmg_read_registers(m,&g,a[0],a[1],r,ecx);
    case ES2_HOOK_INDEX_dmg_region_read:return es2_dmg_region_read_registers(m,&g,a[0],a[1],r,ecx);
    case ES2_HOOK_INDEX_dmg_region_copy:return es2_dmg_region_copy_registers(m,&g,a[0],a[1],r,ecx);
    case ES2_HOOK_INDEX_dmg_capacity_set:return es2_dmg_capacity_set_registers(m,a[0],a[1],a[2],a[3],r,ecx);
    case ES2_HOOK_INDEX_dmg_region_set:return es2_dmg_region_set_registers(m,&g,a[0],a[1],a[2],a[3],r,ecx);
    case ES2_HOOK_INDEX_dmg_part_store:return es2_dmg_part_store_registers(m,a[0],a[1],a[2],a[3],a[4],r,ecx);
    case ES2_HOOK_INDEX_col_part_collect:return es2_col_part_collect_registers(m,&g,a[0],a[1],a[2],a[3],r,ecx);
    case ES2_HOOK_INDEX_col_part_gather:return es2_col_part_gather_registers(m,a[0],a[1],a[2],a[3],r,ecx);
    default:return es2_damage_state_ctor_registers(m,&g,a[0],a[1],a[2],r,ecx);
    }
}
static int dmd_report(const char *what,unsigned n,const uint32_t want[3],const uint32_t got[3],unsigned calls,const dmd_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x calls %u/%u\n",current_function,n,what,
        want[0],got[0],want[1],got[1],want[2],got[2],calls,dmd_calls);
    for(unsigned j=0;j<calls&&j<dmd_calls&&expected;++j)if(memcmp(expected+j,dmd_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(dmd_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<DMD_SIZE;++j)if(dmd_after[j]!=dmd_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,dmd_after[j],dmd_arena[j]);break;}
    return 1;
}
static unsigned dmd_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<dmd_calls;++j){
        const unsigned id=dmd_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?(id==12||id==13)&&c->key==0x18:dmd_addresses[id]&&dmd_addresses[id]==c->key)++hits;
    }
    return hits;
}
/* One direct case: the original, the adapter and the core. */
static int dmd_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r,uint32_t fill){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[DMD_GLOBALS],after[DMD_GLOBALS],actual[DMD_GLOBALS];static dmd_record expected[DMD_MAX];uint32_t want[3],got[3];
    const uint32_t head=dmd_fs0();
    memcpy(dmd_before,dmd_arena,DMD_SIZE);dmd_globals(before,0);
    dmd_reset();dmd_invoke(mapped+d->rva,a,r,fill,want);const unsigned calls=dmd_calls;memcpy(expected,dmd_log,calls*sizeof(*expected));
    memcpy(dmd_after,dmd_arena,DMD_SIZE);dmd_globals(after,0);
    if(dmd_fs0()!=head)return fail("damage_data original left FS:0 changed");
    for(unsigned j=0;j<calls;++j)++dmd_paths[expected[j].id];
    /* the adapter (the framework's entry: six argument slots) */
    memcpy(dmd_arena,dmd_before,DMD_SIZE);dmd_globals(before,1);dmd_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];
    if(count>8)return fail("damage_data counter table too long");
    for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    const uint64_t pair=x->replacement_context(a,d->user,r);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);got[2]=want[2];
    dmd_globals(actual,0);
    if(dmd_fs0()!=head)return fail("damage_data adapter left FS:0 changed");
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=dmd_expected_hits(c+i))return fail("damage_data callee counter delta");
    if(want[0]!=got[0]||want[1]!=got[1]||calls!=dmd_calls||memcmp(expected,dmd_log,calls*sizeof(*expected))||
       memcmp(dmd_after,dmd_arena,DMD_SIZE)||dmd_globals_differ(after,actual))
        return dmd_report("adapter",n,want,got,calls,expected);
    /* the core with the ECX it reports */
    memcpy(dmd_arena,dmd_before,DMD_SIZE);dmd_globals(before,1);dmd_reset();
    got[2]=~want[2];
    const uint64_t core=dmd_core(index,a,r,&got[2]);got[0]=(uint32_t)core;got[1]=(uint32_t)(core>>32);dmd_globals(actual,0);
    if(dmd_fs0()!=head)return fail("damage_data core left FS:0 changed");
    /* With the hook installed (integration pass) the "original" run is the
       hook, whose ECX is the framework's; ECX is compared otherwise. */
    const int ecx_known=!rng_hooks[index].installed;
    if(want[0]!=got[0]||want[1]!=got[1]||(ecx_known&&want[2]!=got[2])||calls!=dmd_calls||memcmp(expected,dmd_log,calls*sizeof(*expected))||
       memcmp(dmd_after,dmd_arena,DMD_SIZE)||dmd_globals_differ(after,actual))
        return dmd_report("core ECX",n,want,got,calls,expected);
    dmd_globals(before,1);
    return 0;
}
static int dmd_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],want[3]={0},got[3]={0},pattern=poison?0xa7a7a7a7u:0x31313131u;const uint32_t head=dmd_fs0();dmd_build(index,n*37u+11u,a,r,0);
        BYTE before[DMD_GLOBALS],after[DMD_GLOBALS],actual[DMD_GLOBALS],expected_fpu[108],actual_fpu[108];static dmd_record expected[DMD_MAX];
        memcpy(dmd_before,dmd_arena,DMD_SIZE);dmd_globals(before,0);
        dmd_reset();sg_poison_call(mapped+d->rva,a,pattern,want);es2_hook_fpu_save(expected_fpu);
        const unsigned calls=dmd_calls;memcpy(expected,dmd_log,calls*sizeof(*expected));memcpy(dmd_after,dmd_arena,DMD_SIZE);dmd_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("damage_data poison install");
        memcpy(dmd_arena,dmd_before,DMD_SIZE);dmd_globals(before,1);dmd_reset();sg_poison_call(mapped+d->rva,a,pattern,got);
        es2_hook_fpu_save(actual_fpu);dmd_globals(actual,0);
        if(dmd_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||calls!=dmd_calls||memcmp(expected,dmd_log,calls*sizeof(*expected))||
           memcmp(dmd_after,dmd_arena,DMD_SIZE)||dmd_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||
           memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=dmd_report(fast?"installed fast":"installed preserving",n,want,got,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("damage_data poison uninstall");dmd_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, pair/trace/arena/globals/FS:0\n",d->name,total);
    return status;
}
/* ---- real callees ---- */
static unsigned dmd_nested(unsigned index,unsigned nested[2]){
    if(index==ES2_HOOK_INDEX_dmg_load){nested[0]=ES2_HOOK_INDEX_dmg_read;nested[1]=ES2_HOOK_INDEX_dmg_region_read;return 2;}
    if(index==ES2_HOOK_INDEX_dmg_read){nested[0]=ES2_HOOK_INDEX_dmg_region_read;return 1;}
    if(index==ES2_HOOK_INDEX_dmg_region_set){nested[0]=ES2_HOOK_INDEX_dmg_region_copy;return 1;}
    if(index==ES2_HOOK_INDEX_col_part_collect){nested[0]=ES2_HOOK_INDEX_col_part_gather;return 1;}
    return 0;
}
/* Stubbed in the real-callee pass: 472f14, 479c78, stream_destroy, new[]
 * and new (a bump allocator); 49630c, _memset, _memcpy and the nested
 * hooks run for real. */
#define DMD_REAL_WHICH 0xb6u
static int dmd_real_pass(unsigned index){
    unsigned nested[2];const unsigned count=dmd_nested(index,nested);
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[3]={0};
    BYTE saved[DMD_IDS][5];int active[DMD_IDS];
    if(dmd_patch(saved,active,DMD_REAL_WHICH,1))return 1;
    static dmd_record expected[DMD_MAX];
    const unsigned modes=2u<<count;
    for(unsigned mode=0;mode<modes&&!status;++mode)for(unsigned n=0;n<2048u/modes&&!status;++n){
        uint32_t a[6],r[3],want[3]={0},got[3]={0},pattern=n&1?0xa7a7a7a7u:0x31313131u;
        dmd_build(index,n*13u+mode,a,r,1);
        const uint32_t regs[3]={pattern,pattern,pattern};const uint32_t head=dmd_fs0();
        BYTE before[DMD_GLOBALS],after[DMD_GLOBALS],actual[DMD_GLOBALS];
        memcpy(dmd_before,dmd_arena,DMD_SIZE);dmd_globals(before,0);
        es2_hook hk[3];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(mode>>(1+j)&1)?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,function_hook_extension(nested[j])))return fail("damage_data nested install");}
        dmd_reset();dmd_invoke(mapped+d->rva,a,regs,pattern,want);
        const unsigned calls=dmd_calls;memcpy(expected,dmd_log,calls*sizeof(*expected));
        memcpy(dmd_after,dmd_arena,DMD_SIZE);dmd_globals(after,0);
        o.mode=mode&1?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(hk+2,d,&o,function_hook_extension(index)))return fail("damage_data caller install");
        memcpy(dmd_arena,dmd_before,DMD_SIZE);dmd_globals(before,1);dmd_reset();
        dmd_invoke(mapped+d->rva,a,regs,pattern,got);dmd_globals(actual,0);
        if(dmd_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||calls!=dmd_calls||memcmp(expected,dmd_log,calls*sizeof(*expected))||
           memcmp(dmd_after,dmd_arena,DMD_SIZE)||dmd_globals_differ(after,actual)){
            fprintf(stderr,"real mode=%u\n",mode);status=dmd_report("real",n,want,got,calls,expected);
        }
        for(unsigned j=0;j<3;++j){if(!hk[j].descriptor)continue;entered[j]+=hk[j].calls;
            if(hk[j].mismatches)status=fail("damage_data real compare mismatch");
            if(!es2_hook_uninstall(hk+j))return fail("damage_data real uninstall");}
        dmd_globals(before,1);++total;
    }
    if(dmd_patch(saved,active,0,0))return 1;
    dmd_real=0;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&!entered[2]){fprintf(stderr,"FAIL: %s never entered\n",d->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, real 49630c/_memcpy/_memset, %u nested hooks original/replaced, hook original/replaced, "
        "31/a7 stack, pair, calls, arena, globals and FS:0\n",d->name,total,count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
static int damage_data_cases(unsigned index,unsigned limit){
    current_function=function_hook_descriptor(index)->name;
    const uint32_t which=dmd_which(index);
    BYTE saved[DMD_IDS][5],old_globals[DMD_GLOBALS];int active[DMD_IDS];
    dmd_globals(old_globals,0);
    if(dmd_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_DAMAGE_DATA_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);memset(dmd_paths,0,sizeof(dmd_paths));
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];dmd_build(index,n,a,r,0);const uint32_t fill=n%3==0?0:dmd_next();
        status=dmd_compare(index,n,a,r,fill);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    dmd_globals(old_globals,1);
    if(!limit&&!status)status=dmd_installed_probes(index,64);
    if(dmd_patch(saved,active,which,0))return 1;
    dmd_globals(old_globals,1);
    if(!limit&&!status&&which)status=dmd_real_pass(index);
    dmd_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases (stub calls 49630c %u, 472f14 %u, 479c78 %u, dmg_read %u, stream_destroy %u, new[] %u, "
        "dmg_region_read %u, new %u, memset %u, memcpy %u, dmg_region_copy %u, col_part_gather %u, read %u/%u); "
        "full return pair and ECX, ordered calls with entry registers, slots and context word, arena/globals/FS:0\n",
        function_hook_descriptor(index)->name,cases,dmd_paths[0],dmd_paths[1],dmd_paths[2],dmd_paths[3],dmd_paths[4],dmd_paths[5],
        dmd_paths[6],dmd_paths[7],dmd_paths[8],dmd_paths[9],dmd_paths[10],dmd_paths[11],dmd_paths[12],dmd_paths[13]);
    return status;
}
