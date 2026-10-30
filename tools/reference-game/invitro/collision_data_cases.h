/* collision_data group: collision_load 40cd88, collision_read 40ccf8,
 * collision_entry 40cc50, collision_volume 40cc14, collision_points 40c7c4,
 * collision_bounds 40ccc8, collision_radius 40c5d0, collision_scan 40cec0 and
 * collision_match 40ce78 (docs/reverse-engineering/mission_load.md, "Collision
 * data").
 *
 * Direct cases run the original body (EAX/EDX/ECX, EBX, ESI and EDI at entry
 * from the case, six argument slots, over a stack filled with a case
 * pattern) and the core against logging stubs for every game callee of the
 * hook under test: 49630c (a record stub: the real FS:0 head at record+0,
 * context word +10h zeroed, the record linked into FS:0), the stream
 * constructor 472f14 (it writes the stream's vtable and status), the
 * assertion 479c78, 472f64, new 4774f0, new[] 49667c, dist3d_delta 47de72,
 * the original volume match 40ce28, the other hooks of the group, and the
 * stream read: the arena stream's two vtables hold the read stub in slot
 * +18h, and the stub writes a scripted word (2-byte reads) or bytes into the
 * buffer. Stack addresses in logged registers and slots are masked (the
 * original's frame against the core's scratch). Compared: the full return
 * pair, ECX, the ordered calls with argument slots (and the bridge's slot
 * count), entry EAX/EDX/ECX and the record's context word, the arena, the
 * image globals (the slot counter 4987deh, the slot table 4a9824h) and FS:0.
 * The stubs change what the originals reread (the counts after 4774f0,
 * 40cc50, 40ccc8, 49667c, 40cc14, 40c5d0, 40ce78 and 40ce28, the stream's
 * vtable after a read, the counter and the table after 472f14 and 479c78),
 * and the centre may overlap the spheres, so the order of reads and writes
 * is observable. In even cases the inputs are the real callers' (status 0,
 * small counts, the centre at V+8, n = 1) and the adapter is compared as
 * well, with the callee counter deltas.
 *
 * Then installed 31/a7 poison probes and real-callee passes with the
 * nested hooks in rotating original/replaced modes, the hook under test
 * original or replaced: the reader chain over a scripted stream (small
 * counts), the real 49667c with its element constructors and a bump
 * allocator for 4774f0 (and 472f14/472f64/479c78 stubs, 49630c real, for
 * collision_load); the bounds over the real dist3d_delta; the scan and the
 * match over the real 40ce28 and 40c834. collision_radius, which accepts
 * compare, also runs the compare probes of compare_probes.h. Identifiers
 * use the prefix cdc_/CDC_. */
typedef struct cdc_record { uint32_t id,slots,args[6],regs[3],context,eax,edx; } cdc_record;
enum {
    CDC_SIZE=0x2000, CDC_MAX=256, CDC_IDS=16,
    CDC_S=0x100, CDC_VT=0x180, CDC_VT2=0x1c0, CDC_D=0x200, CDC_G=0x300, CDC_V=0x400, CDC_P=0x600, CDC_A=0x900, CDC_B=0x940,
    CDC_W=0x980, CDC_P2=0xa00, CDC_OFF=0xb00, CDC_NAME=0xb80, CDC_COUNTER=0xbf0, CDC_TABLE=0xc00, CDC_BUMP=0x1000
};
#define CDC_B0 ((uint32_t)(uintptr_t)cdc_arena)
#define CDC_ADDR(o) (CDC_B0+(uint32_t)(o))
#define CDC_BIT(i) ((uint32_t)1<<(i))
static BYTE cdc_arena[CDC_SIZE],cdc_before[CDC_SIZE],cdc_after[CDC_SIZE];
static cdc_record cdc_log[CDC_MAX];
static unsigned cdc_calls,cdc_real,cdc_effects,cdc_exact,cdc_index,cdc_paths[CDC_IDS];
static uint32_t cdc_seed,cdc_stream,cdc_bump,cdc_head,cdc_obj;
__attribute__((used)) static uint32_t cdc_ecx;
/* 0 49630c, 1 472f14, 2 479c78, 3 40ccf8, 4 472f64, 5 new 4774f0, 6 40cc50,
 * 7 40ccc8, 8 the stream read (slot +18h, not patched: the arena vtables
 * point to its stub), 9 new[] 49667c, 10 40cc14, 11 40c7c4, 12 dist3d_delta
 * 47de72, 13 40c5d0, 14 40ce78, 15 40ce28. The slots each original call
 * site pushes. */
static const uint32_t cdc_addresses[CDC_IDS]={0x49630c,0x472f14,0x479c78,0x40ccf8,0x472f64,0x4774f0,0x40cc50,0x40ccc8,0,
    0x49667c,0x40cc14,0x40c7c4,0x47de72,0x40c5d0,0x40ce78,0x40ce28};
static const unsigned cdc_counts[CDC_IDS]={0,2,6,2,2,1,2,1,3,5,2,2,3,2,4,4};
static uint32_t cdc_next(void){
    uint32_t z=cdc_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *cdc_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static uint32_t cdc_fs0(void){uint32_t v;__asm__ volatile("movl %%fs:0,%0":"=r"(v));return v;}
static int cdc_on_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static void cdc_reset(void){cdc_calls=0;cdc_stream=cdc_seed;cdc_bump=CDC_ADDR(CDC_BUMP);cdc_head=cdc_fs0();}
static void cdc_random(unsigned offset,unsigned size){for(unsigned i=0;i<size;++i)cdc_arena[offset+i]=(BYTE)cdc_next();}
static uint32_t cdc_alloc(uint32_t size){
    const uint32_t p=cdc_bump;size=(size+15u)&~7u;
    if(size>0x1000u||p+size>CDC_ADDR(CDC_SIZE)){fail("collision_data bump allocator overflow");ExitProcess(1);}
    memset(cdc_host(p),0,size);cdc_bump+=size;return p;
}
/* A count word: 0 .. 4 for exact and real data, else also negative ones. */
static uint16_t cdc_small(int exact){
    const unsigned k=cdc_next()%8;
    if(!exact&&k==0)return 0xffff;
    if(!exact&&k==1)return (uint16_t)(0x8000u|cdc_next());
    return (uint16_t)(cdc_next()%5);
}
/* The sphere count collision_points reads: also the words whose 8-fold
 * wraps (1000h: the size 8000h, 2000h: 0, 2001h, 1FFFh). */
static uint16_t cdc_sphere_count(void){
    static const uint16_t wild[]={0x1000,0x1fff,0x2000,0x2001,0xffff,0x8001,0x0fff,0x4000};
    const unsigned k=cdc_next()%4;
    return k==0?wild[cdc_next()%8]:(uint16_t)(cdc_next()%6);
}
static uint32_t cdc_vtable_of(uint32_t stream){return get32(cdc_host(stream));}
/* What the stubs answer and change. */
static uint32_t cdc_answer(unsigned id,const uint32_t *a,uint32_t eax,uint32_t pick){
    const int effect=cdc_effects&&pick%4==0;
    switch(id){
    case 1:   /* 472f14: the stream object; the counter or the table reread after it */
        put32(cdc_host(a[0]),CDC_ADDR(CDC_VT));
        put32(cdc_host(a[0]+4),cdc_exact||cdc_real||cdc_next()%3?0:cdc_next()|1u);
        /* fall through */
    case 2:   /* 479c78 */
        if(effect){if(pick&0x100)put16(mapped+0x987de,(uint16_t)cdc_next());else put32(mapped+0xa9824,CDC_ADDR(CDC_TABLE+6*(cdc_next()%4)));}
        return id==1?a[0]:eax;
    case 5:   /* new: the groups; the descriptor's count reread */
        if(cdc_real)return cdc_alloc(a[0]);
        if(effect)put16(cdc_arena+CDC_D,cdc_small(0));
        return CDC_ADDR(CDC_G);
    case 6:case 7:case 14:   /* 40cc50, 40ccc8, 40ce78: the descriptor's (table's) count reread */
        if(effect)put16(cdc_arena+CDC_D,cdc_small(0));
        if(id==14)return pick&0x200?eax|1u:eax&0xffff0000u;
        return eax;
    case 8: { /* the stream read (S, size, buffer) */
        const uint32_t size=a[1];
        if(size==2)put16(cdc_host(a[2]),cdc_index==ES2_HOOK_INDEX_collision_points&&!cdc_exact&&!cdc_real?cdc_sphere_count():
            cdc_small(cdc_exact||cdc_real));
        else if(size<=0x100)for(uint32_t i=0;i<size;++i)cdc_host(a[2])[i]=(BYTE)cdc_next();
        if(effect&&(pick&0x100)&&!cdc_on_stack(a[0]))
            put32(cdc_host(a[0]),cdc_vtable_of(a[0])==CDC_ADDR(CDC_VT)?CDC_ADDR(CDC_VT2):CDC_ADDR(CDC_VT));
        return cdc_real||(pick>>10)%8?eax|1u:eax&0xffffff00u;
    }
    case 9:   /* new[]: the volumes (collision_entry, count reread) or the spheres */
        if(cdc_index==ES2_HOOK_INDEX_collision_entry){if(effect)put16(cdc_host(cdc_obj+4),cdc_small(0));return CDC_ADDR(CDC_V);}
        return CDC_ADDR(CDC_P);
    case 10:case 13:case 15:   /* 40cc14, 40c5d0, 40ce28: the group's volume count reread */
        if(effect)put16(cdc_host(cdc_obj+4),cdc_small(0));
        if(id==15)return pick&0x200?eax|0x10000u|(pick>>11&1):eax&0xffff0000u;
        return eax;
    }
    return eax;
}
__attribute__((used,noinline)) static uint64_t __cdecl cdc_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(cdc_calls==CDC_MAX){fail("collision_data call overflow");ExitProcess(1);}
    cdc_record *v=cdc_log+cdc_calls++;memset(v,0,sizeof(*v));v->id=id;
    v->slots=weapons_bridge_slots(a,cdc_counts[id]);
    for(unsigned k=0;k<cdc_counts[id];++k)v->args[k]=cdc_on_stack(a[k])?0:a[k];
    for(unsigned j=0;j<3;++j)v->regs[j]=cdc_on_stack(regs[j])?0:regs[j];
    if(id&&cdc_fs0()!=cdc_head)v->context=get16(cdc_host(cdc_fs0()+16u));   /* a hook's own record */
    uint32_t eax=cdc_next(),edx=cdc_next(),pick=cdc_next();cdc_ecx=cdc_next();
    if(id==0)eax=cdc_fs0();
    else eax=cdc_answer(id,a,eax,pick);
    v->eax=cdc_on_stack(eax)?0:eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define CDC_STUB(n) __attribute__((naked)) static void cdc_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _cdc_stub_impl\n\taddl $24,%esp\n\tmovl _cdc_ecx,%ecx\n\tret\n\t");}
CDC_STUB(1) CDC_STUB(2) CDC_STUB(3) CDC_STUB(4) CDC_STUB(5) CDC_STUB(6) CDC_STUB(7) CDC_STUB(8) CDC_STUB(9) CDC_STUB(10)
CDC_STUB(11) CDC_STUB(12) CDC_STUB(13) CDC_STUB(14) CDC_STUB(15)
#undef CDC_STUB
/* 49630c: logs like the other stubs (EDX and ECX come back unchanged, as the
 * original's do), then links a record at EBP + [descriptor+4] into FS:0
 * with the saved head at record+0 and a zero context word. */
__attribute__((naked)) static void cdc_stub_0(void){__asm__ volatile(
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
    "pushl $0\n\tcalll _cdc_stub_impl\n\taddl $12,%esp\n\t"
    "popl %ecx\n\tmovl 4(%ecx),%ecx\n\taddl %ebp,%ecx\n\tmovl %eax,(%ecx)\n\tmovw $0,16(%ecx)\n\tmovl %ecx,%fs:0\n\t"
    "popl %edx\n\tpopl %ecx\n\tret\n\t");}
static void *cdc_stubs[CDC_IDS]={cdc_stub_0,cdc_stub_1,cdc_stub_2,cdc_stub_3,cdc_stub_4,cdc_stub_5,cdc_stub_6,cdc_stub_7,cdc_stub_8,
    cdc_stub_9,cdc_stub_10,cdc_stub_11,cdc_stub_12,cdc_stub_13,cdc_stub_14,cdc_stub_15};
static int cdc_patch(BYTE saved[CDC_IDS][5],int active[CDC_IDS],uint32_t which,int on){
    for(unsigned i=0;i<CDC_IDS;++i){
        if(!cdc_addresses[i])continue;
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(cdc_addresses[i]-0x400000u,cdc_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(cdc_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
/* Direct callees of each hook (bit i = cdc_addresses[i]; the read is never
 * patched). */
static uint32_t cdc_which(unsigned index){
    if(index==ES2_HOOK_INDEX_collision_load)return CDC_BIT(0)|CDC_BIT(1)|CDC_BIT(2)|CDC_BIT(3)|CDC_BIT(4);
    if(index==ES2_HOOK_INDEX_collision_read)return CDC_BIT(2)|CDC_BIT(5)|CDC_BIT(6)|CDC_BIT(7);
    if(index==ES2_HOOK_INDEX_collision_entry)return CDC_BIT(9)|CDC_BIT(10);
    if(index==ES2_HOOK_INDEX_collision_volume)return CDC_BIT(11);
    if(index==ES2_HOOK_INDEX_collision_points)return CDC_BIT(9);
    if(index==ES2_HOOK_INDEX_collision_bounds)return CDC_BIT(13);
    if(index==ES2_HOOK_INDEX_collision_radius)return CDC_BIT(12);
    if(index==ES2_HOOK_INDEX_collision_scan)return CDC_BIT(7)|CDC_BIT(14);
    return CDC_BIT(15);
}
/* Stubbed in the real-callee passes (the rest runs for real: 49630c, the
 * nested hooks, 49667c with 40c8be/40cf04, dist3d_delta, 40ce28/40c834). */
static uint32_t cdc_real_which(unsigned index){
    if(index==ES2_HOOK_INDEX_collision_load)return CDC_BIT(1)|CDC_BIT(2)|CDC_BIT(4)|CDC_BIT(5);
    if(index==ES2_HOOK_INDEX_collision_bounds||index==ES2_HOOK_INDEX_collision_radius)return 0;
    if(index==ES2_HOOK_INDEX_collision_scan||index==ES2_HOOK_INDEX_collision_match)return CDC_BIT(5);
    return CDC_BIT(2)|CDC_BIT(5);
}
/* The image globals: the slot counter word, the slot table pointer and the
 * construction-count pointer (the real 49630c reads it). */
static const struct { uint32_t rva,size; } cdc_ranges[]={{0x987de,2},{0xa9824,4},{0x2cbee4,4}};
enum { CDC_GLOBALS=2+4+4 };
static void cdc_globals(BYTE *out,int restore){
    for(unsigned i=0;i<sizeof(cdc_ranges)/sizeof(cdc_ranges[0]);++i){
        if(restore)memcpy(mapped+cdc_ranges[i].rva,out,cdc_ranges[i].size);else memcpy(out,mapped+cdc_ranges[i].rva,cdc_ranges[i].size);
        out+=cdc_ranges[i].size;
    }
}
static int cdc_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<CDC_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
/* Spheres at `offset`: game-like (coordinates -1500..1750, radii 40..600)
 * or any words. */
static void cdc_spheres(unsigned offset,unsigned count,int exact){
    for(unsigned i=0;i<count;++i){
        BYTE *p=cdc_arena+offset+8*i;
        for(unsigned k=0;k<3;++k)put16(p+2*k,exact?(uint16_t)(cdc_next()%3251u-1500u):(uint16_t)cdc_next());
        put16(p+6,exact?(uint16_t)(40u+cdc_next()%561u):(uint16_t)cdc_next());
    }
}
/* Volumes at `offset`: count words, sphere pointers into P (or P2), region
 * words 0..5. */
static void cdc_volumes(unsigned offset,unsigned count,unsigned points,int exact){
    for(unsigned i=0;i<count;++i){
        BYTE *v=cdc_arena+offset+16*i;const unsigned spheres=cdc_next()%4;
        put16(v,(uint16_t)spheres);put32(v+2,CDC_ADDR(points+32*(i%8)));put16(v+6,(uint16_t)(cdc_next()%6));
        cdc_spheres(points+32*(i%8),spheres,exact);
    }
}
/* The case data. exact: the real callers' inputs; real: data for the
 * real-callee passes. */
static void cdc_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],uint32_t regs[3],uint32_t *fill,int exact,int real){
    cdc_seed=random32();cdc_stream=cdc_seed^n;cdc_real=real;cdc_exact=exact||real;cdc_effects=!real&&!exact;cdc_index=index;
    for(unsigned i=0;i<3;++i){r[i]=cdc_next();regs[i]=cdc_next();}
    cdc_random(0,CDC_SIZE);memset(a,0,24);
    *fill=!exact&&!real&&cdc_next()%2?cdc_next():0;
    /* the stream and its two vtables (slot +18h: the read stub) */
    put32(cdc_arena+CDC_S,CDC_ADDR(CDC_VT));put32(cdc_arena+CDC_S+4,exact||real||cdc_next()%3?0:cdc_next()|1u);
    put32(cdc_arena+CDC_VT+0x18,(uint32_t)(uintptr_t)cdc_stub_8);put32(cdc_arena+CDC_VT2+0x18,(uint32_t)(uintptr_t)cdc_stub_8);
    put16(mapped+0x987de,exact||real?(uint16_t)(cdc_next()%8):(uint16_t)cdc_next());
    put32(mapped+0xa9824,CDC_ADDR(CDC_TABLE));put32(mapped+0x2cbee4,CDC_ADDR(CDC_COUNTER));
    const uint32_t S=CDC_ADDR(CDC_S);
    if(index==ES2_HOOK_INDEX_collision_load){
        a[0]=exact||real?CDC_ADDR(CDC_NAME):cdc_next();
    } else if(index==ES2_HOOK_INDEX_collision_read){
        a[0]=CDC_ADDR(CDC_D);a[1]=S;
    } else if(index==ES2_HOOK_INDEX_collision_entry){
        a[0]=CDC_ADDR(CDC_G+10*(cdc_next()%4));a[1]=S;cdc_obj=a[0];
    } else if(index==ES2_HOOK_INDEX_collision_volume||index==ES2_HOOK_INDEX_collision_points){
        a[0]=CDC_ADDR(CDC_V+16*(cdc_next()%4));a[1]=S;cdc_obj=a[0];
    } else if(index==ES2_HOOK_INDEX_collision_bounds){
        const unsigned count=real?cdc_next()%5:cdc_small(exact);
        a[0]=CDC_ADDR(CDC_G);cdc_obj=a[0];put16(cdc_arena+CDC_G+4,(uint16_t)count);put32(cdc_arena+CDC_G+6,CDC_ADDR(CDC_V));
        if(real)cdc_volumes(CDC_V,count,CDC_P,1);
    } else if(index==ES2_HOOK_INDEX_collision_radius){
        const uint32_t V=CDC_ADDR(CDC_V+16*(cdc_next()%4));const unsigned k=cdc_next()%8;
        const uint16_t count=exact||real?(uint16_t)(cdc_next()%7):k==0?(uint16_t)(0x8000u|cdc_next()):(uint16_t)(cdc_next()%9);
        put16(cdc_host(V),count);put32(cdc_host(V+2),CDC_ADDR(CDC_P));
        cdc_spheres(CDC_P,(uint16_t)count<9?count:0,exact||real||cdc_next()%2);
        /* the centre: V+8 (the real caller), or over the spheres, or V+2 */
        const unsigned w=cdc_next()%4;
        a[0]=V;a[1]=exact||real||w==0?V+8:w==1?CDC_ADDR(CDC_P+2*(cdc_next()%20)):w==2?V+2:CDC_ADDR(CDC_A+cdc_next()%32);
        cdc_obj=V;
    } else {
        /* scan: the table at D, groups at G; match: one group at G */
        const int scan=index==ES2_HOOK_INDEX_collision_scan;
        const unsigned groups=real?1+cdc_next()%4:cdc_small(exact);
        if(scan){put16(cdc_arena+CDC_D,(uint16_t)groups);put32(cdc_arena+CDC_D+2,CDC_ADDR(CDC_G));}
        const unsigned shown=scan?((uint16_t)groups<8?groups:0):1;
        for(unsigned i=0;i<shown;++i){
            const unsigned volumes=real?cdc_next()%4:cdc_small(exact);
            put16(cdc_arena+CDC_G+10*i+4,(uint16_t)volumes);put32(cdc_arena+CDC_G+10*i+6,CDC_ADDR(CDC_V+0x40*i));
            if(real)cdc_volumes(CDC_V+0x40*i,(uint16_t)volumes<4?volumes:0,CDC_P,1);
        }
        const unsigned pairs=real?1+cdc_next()%3:1;
        a[0]=scan?CDC_ADDR(CDC_D):CDC_ADDR(CDC_G);a[1]=CDC_ADDR(CDC_A);a[2]=CDC_ADDR(CDC_B);
        a[3]=exact||real?(real?pairs:1u):cdc_next();cdc_obj=CDC_ADDR(CDC_G);
        if(real){
            /* weapon volumes W (spheres P2) and offset vectors */
            cdc_volumes(CDC_W,pairs,CDC_P2,1);
            for(unsigned i=0;i<pairs;++i){
                put32(cdc_arena+CDC_A+4*i,CDC_ADDR(CDC_W+16*i));put32(cdc_arena+CDC_B+4*i,CDC_ADDR(CDC_OFF+8*i));
                for(unsigned k=0;k<3;++k)put16(cdc_arena+CDC_OFF+8*i+2*k,(uint16_t)(cdc_next()%601u-300u));
            }
        }
    }
    cdc_stream=cdc_seed;   /* the stubs' stream restarts here in every run */
}
/* Calls target with six argument slots, EAX/EDX/ECX = r and EBX/ESI/EDI =
 * regs over 3,840 bytes of stack filled with `fill`; out receives EAX, EDX
 * and ECX after the call. */
__attribute__((naked,noinline)) static void cdc_invoke(void *target,const uint32_t *a,const uint32_t *r,const uint32_t *regs,uint32_t fill,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 24(%ebp),%eax\n\tmovl $1024,%ecx\n\tcld\n\trep stosl\n\taddl $3840,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 20(%esi)\n\tpushl 16(%esi)\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 20(%ebp),%edi\n\tmovl (%edi),%ebx\n\tmovl 4(%edi),%esi\n\tmovl 8(%edi),%edi\n\t"
        "movl 16(%ebp),%eax\n\tmovl 4(%eax),%edx\n\tmovl 8(%eax),%ecx\n\tmovl (%eax),%eax\n\tcall *8(%ebp)\n\t"
        "movl 28(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* The core with the adapter's graph and scratch; FS:0 restored to the
 * record's saved head as the adapter does. */
static uint64_t cdc_core(unsigned index,const uint32_t *a,const uint32_t *r,uint32_t *ecx){
    const es2_guest_memory *m=&math_native_memory;
    if(index==ES2_HOOK_INDEX_collision_load){
        BYTE scratch[ES2_COLLISION_LOAD_SCRATCH]={0};es2_weapon_graph g=weapons_graph(mapped,scratch,index);
        weapons_impact_context c={(uint32_t)(uintptr_t)mapped,(uint32_t)(uintptr_t)(scratch+ES2_COLLISION_LOAD_FRAME)};
        g.call=weapons_impact_guest_call;g.context=&c;
        const uint64_t v=es2_collision_load_registers(m,&g,a[0],r,ecx);
        const uint32_t head=(uint32_t)(v>>32);__asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");
        return v;
    }
    es2_weapon_graph g=weapons_graph(mapped,NULL,index);
    if(index==ES2_HOOK_INDEX_collision_read)return es2_collision_read_registers(m,&g,a[0],a[1],r,ecx);
    if(index==ES2_HOOK_INDEX_collision_entry)return es2_collision_entry_registers(m,&g,a[0],a[1],r,ecx);
    if(index==ES2_HOOK_INDEX_collision_volume)return es2_collision_volume_registers(m,&g,a[0],a[1],r,ecx);
    if(index==ES2_HOOK_INDEX_collision_points)return es2_collision_points_registers(m,&g,a[0],a[1],r,ecx);
    if(index==ES2_HOOK_INDEX_collision_bounds)return es2_collision_bounds_registers(m,&g,a[0],r,ecx);
    if(index==ES2_HOOK_INDEX_collision_radius)return es2_collision_radius_registers(m,&g,a[0],a[1],r,ecx);
    if(index==ES2_HOOK_INDEX_collision_scan)return es2_collision_scan_registers(m,&g,a[0],a[1],a[2],a[3],r,ecx);
    return es2_collision_match_registers(m,&g,a[0],a[1],a[2],a[3],r,ecx);
}
static int cdc_report(const char *what,unsigned n,const uint32_t want[3],const uint32_t got[3],unsigned calls,const cdc_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x calls %u/%u\n",current_function,n,what,
        want[0],got[0],want[1],got[1],want[2],got[2],calls,cdc_calls);
    for(unsigned j=0;j<calls&&j<cdc_calls&&expected;++j)if(memcmp(expected+j,cdc_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(cdc_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<CDC_SIZE;++j)if(cdc_after[j]!=cdc_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,cdc_after[j],cdc_arena[j]);break;}
    return 1;
}
/* Callee counter hits of the logged stub calls (the read by its slot). */
static unsigned cdc_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<cdc_calls;++j){
        const unsigned id=cdc_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?id==8&&c->key==0x18u:cdc_addresses[id]==c->key)++hits;
    }
    return hits;
}
/* One direct case: the original, the core and (exact inputs) the adapter. */
static int cdc_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r,const uint32_t *regs,uint32_t fill,int exact,int check_ecx){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[CDC_GLOBALS],after[CDC_GLOBALS],actual[CDC_GLOBALS];static cdc_record expected[CDC_MAX];uint32_t want[3],got[3];
    const uint32_t head=cdc_fs0();
    memcpy(cdc_before,cdc_arena,CDC_SIZE);cdc_globals(before,0);
    cdc_reset();cdc_invoke(mapped+d->rva,a,r,regs,fill,want);const unsigned calls=cdc_calls;memcpy(expected,cdc_log,calls*sizeof(*expected));
    memcpy(cdc_after,cdc_arena,CDC_SIZE);cdc_globals(after,0);
    if(cdc_fs0()!=head)return fail("collision_data original left FS:0 changed");
    for(unsigned j=0;j<calls;++j)++cdc_paths[expected[j].id];
    memcpy(cdc_arena,cdc_before,CDC_SIZE);cdc_globals(before,1);cdc_reset();
    got[2]=~want[2];
    uint64_t pair=cdc_core(index,a,r,&got[2]);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);cdc_globals(actual,0);
    if(cdc_fs0()!=head)return fail("collision_data core left FS:0 changed");
    if(want[0]!=got[0]||want[1]!=got[1]||(check_ecx&&want[2]!=got[2])||calls!=cdc_calls||memcmp(expected,cdc_log,calls*sizeof(*expected))||
       memcmp(cdc_after,cdc_arena,CDC_SIZE)||cdc_globals_differ(after,actual))
        return cdc_report("core",n,want,got,calls,expected);
    if(!exact){cdc_globals(before,1);return 0;}
    /* the adapter (the framework's entry: six argument slots) */
    memcpy(cdc_arena,cdc_before,CDC_SIZE);cdc_globals(before,1);cdc_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];
    if(count>16)return fail("collision_data counter table too long");
    for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair=x->replacement_context(a,d->user,r);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);got[2]=want[2];
    cdc_globals(actual,0);
    if(cdc_fs0()!=head)return fail("collision_data adapter left FS:0 changed");
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=cdc_expected_hits(c+i))return fail("collision_data callee counter delta");
    if(want[0]!=got[0]||want[1]!=got[1]||calls!=cdc_calls||memcmp(expected,cdc_log,calls*sizeof(*expected))||
       memcmp(cdc_after,cdc_arena,CDC_SIZE)||cdc_globals_differ(after,actual))
        return cdc_report("adapter",n,want,got,calls,expected);
    cdc_globals(before,1);
    return 0;
}
static int cdc_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],regs[3],fill,want[3]={0},got[3]={0},pattern=poison?0xa7a7a7a7u:0x31313131u;
        cdc_build(index,n*37u+11u,a,r,regs,&fill,1,0);
        const uint32_t entry[3]={pattern,pattern,pattern};const uint32_t head=cdc_fs0();
        BYTE before[CDC_GLOBALS],after[CDC_GLOBALS],actual[CDC_GLOBALS],expected_fpu[108],actual_fpu[108];static cdc_record expected[CDC_MAX];
        memcpy(cdc_before,cdc_arena,CDC_SIZE);cdc_globals(before,0);
        cdc_reset();cdc_invoke(mapped+d->rva,a,entry,regs,pattern,want);es2_hook_fpu_save(expected_fpu);
        const unsigned calls=cdc_calls;memcpy(expected,cdc_log,calls*sizeof(*expected));memcpy(cdc_after,cdc_arena,CDC_SIZE);cdc_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("collision_data poison install");
        memcpy(cdc_arena,cdc_before,CDC_SIZE);cdc_globals(before,1);cdc_reset();cdc_invoke(mapped+d->rva,a,entry,regs,pattern,got);
        es2_hook_fpu_save(actual_fpu);cdc_globals(actual,0);
        if(cdc_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||calls!=cdc_calls||memcmp(expected,cdc_log,calls*sizeof(*expected))||
           memcmp(cdc_after,cdc_arena,CDC_SIZE)||cdc_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||
           memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=cdc_report(fast?"installed fast":"installed preserving",n,want,got,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("collision_data poison uninstall");cdc_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, pair/trace/arena/globals/FS:0\n",d->name,total);
    return status;
}
/* ---- real callees ---- */
enum { CDC_NESTED=6 };
static unsigned cdc_nested(unsigned index,unsigned nested[CDC_NESTED]){
    unsigned count=0;
#define CDC_N(name) nested[count++]=ES2_HOOK_INDEX_##name
    if(index==ES2_HOOK_INDEX_collision_load){CDC_N(collision_read);CDC_N(collision_entry);CDC_N(collision_volume);CDC_N(collision_points);
        CDC_N(collision_bounds);CDC_N(collision_radius);}
    else if(index==ES2_HOOK_INDEX_collision_read){CDC_N(collision_entry);CDC_N(collision_volume);CDC_N(collision_points);
        CDC_N(collision_bounds);CDC_N(collision_radius);CDC_N(dist3d_delta);}
    else if(index==ES2_HOOK_INDEX_collision_entry){CDC_N(collision_volume);CDC_N(collision_points);}
    else if(index==ES2_HOOK_INDEX_collision_volume){CDC_N(collision_points);}
    else if(index==ES2_HOOK_INDEX_collision_bounds){CDC_N(collision_radius);CDC_N(dist3d_delta);}
    else if(index==ES2_HOOK_INDEX_collision_radius){CDC_N(dist3d_delta);}
    else if(index==ES2_HOOK_INDEX_collision_scan){CDC_N(collision_match);CDC_N(collision_bounds);CDC_N(collision_radius);CDC_N(dist3d_delta);}
#undef CDC_N
    return count;
}
static int cdc_real_pass(unsigned index){
    unsigned nested[CDC_NESTED];const unsigned count=cdc_nested(index,nested);
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[CDC_NESTED+1]={0};
    BYTE saved[CDC_IDS][5];int active[CDC_IDS];
    if(cdc_patch(saved,active,cdc_real_which(index),1))return 1;
    static cdc_record expected[CDC_MAX];
    const unsigned modes=2u<<count;
    for(unsigned mode=0;mode<modes&&!status;++mode)for(unsigned n=0;n<4096u/modes&&!status;++n){
        uint32_t a[6],r[3],regs[3],fill,want[3]={0},got[3]={0},pattern=n&1?0xa7a7a7a7u:0x31313131u;
        cdc_build(index,n*13u+mode,a,r,regs,&fill,1,1);
        const uint32_t entry[3]={pattern,pattern,pattern};const uint32_t head=cdc_fs0();
        BYTE before[CDC_GLOBALS],after[CDC_GLOBALS],actual[CDC_GLOBALS];
        memcpy(cdc_before,cdc_arena,CDC_SIZE);cdc_globals(before,0);
        es2_hook hk[CDC_NESTED+1];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(mode>>(1+j)&1)?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,function_hook_extension(nested[j])))return fail("collision_data nested install");}
        cdc_reset();cdc_invoke(mapped+d->rva,a,entry,regs,pattern,want);
        const unsigned calls=cdc_calls;memcpy(expected,cdc_log,calls*sizeof(*expected));
        memcpy(cdc_after,cdc_arena,CDC_SIZE);cdc_globals(after,0);
        o.mode=mode&1?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(hk+CDC_NESTED,d,&o,function_hook_extension(index)))return fail("collision_data caller install");
        memcpy(cdc_arena,cdc_before,CDC_SIZE);cdc_globals(before,1);cdc_reset();
        cdc_invoke(mapped+d->rva,a,entry,regs,pattern,got);cdc_globals(actual,0);
        if(cdc_fs0()!=head||want[0]!=got[0]||want[1]!=got[1]||calls!=cdc_calls||memcmp(expected,cdc_log,calls*sizeof(*expected))||
           memcmp(cdc_after,cdc_arena,CDC_SIZE)||cdc_globals_differ(after,actual)){
            fprintf(stderr,"real mode=%u\n",mode);status=cdc_report("real",n,want,got,calls,expected);
        }
        for(unsigned j=0;j<=CDC_NESTED;++j){if(!hk[j].descriptor)continue;entered[j]+=hk[j].calls;
            if(hk[j].mismatches)status=fail("collision_data real compare mismatch");
            if(!es2_hook_uninstall(hk+j))return fail("collision_data real uninstall");}
        cdc_globals(before,1);++total;
    }
    if(cdc_patch(saved,active,0,0))return 1;
    cdc_real=0;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&!entered[CDC_NESTED]){fprintf(stderr,"FAIL: %s never entered\n",d->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, %u nested hooks original/replaced, hook original/replaced, 31/a7 stack, pair, calls, arena, globals and FS:0\n",
        d->name,total,count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
/* Compare probes (collision_radius): game-like volumes, the centre at V+8,
 * dist3d_delta real. */
static void cdc_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    uint32_t regs[3],fill;cdc_build(index,n,a,r,regs,&fill,1,1);
}
static void cdc_compare_state(BYTE *s,int restore){
    if(restore){memcpy(cdc_arena,s,CDC_SIZE);cdc_globals(s+CDC_SIZE,1);}else{memcpy(s,cdc_arena,CDC_SIZE);cdc_globals(s+CDC_SIZE,0);}
}
static int collision_data_cases(unsigned index,unsigned limit){
    current_function=function_hook_descriptor(index)->name;
    BYTE old_globals[CDC_GLOBALS];cdc_globals(old_globals,0);
    if(limit&&compare_probe_installed(index)){
        const int status=compare_probe_run(index,limit,cdc_compare_build,cdc_compare_state,CDC_SIZE+CDC_GLOBALS);
        cdc_globals(old_globals,1);return status;
    }
    const uint32_t which=cdc_which(index);
    BYTE saved[CDC_IDS][5];int active[CDC_IDS];
    if(cdc_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_COLLISION_DATA_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);memset(cdc_paths,0,sizeof(cdc_paths));
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3],regs[3],fill;const int exact=limit||!(n&1);
        cdc_build(index,n,a,r,regs,&fill,exact,0);status=cdc_compare(index,n,a,r,regs,fill,exact&&!limit,!limit);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    cdc_globals(old_globals,1);
    if(!limit&&!status)status=cdc_installed_probes(index,64);
    if(cdc_patch(saved,active,which,0))return 1;
    cdc_globals(old_globals,1);
    if(!limit&&!status)status=cdc_real_pass(index);
    cdc_globals(old_globals,1);
    if(!limit&&!status&&compare_probe_capable(index))status=compare_probe_run(index,0,cdc_compare_build,cdc_compare_state,CDC_SIZE+CDC_GLOBALS);
    cdc_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases (stub calls 49630c %u, 472f14 %u, 479c78 %u, 40ccf8 %u, 472f64 %u, new %u, 40cc50 %u, 40ccc8 %u, "
        "read %u, new[] %u, 40cc14 %u, 40c7c4 %u, dist3d %u, 40c5d0 %u, 40ce78 %u, 40ce28 %u); "
        "full return pair and ECX, ordered calls with entry registers, slots and context word, arena/globals/FS:0\n",
        function_hook_descriptor(index)->name,cases,cdc_paths[0],cdc_paths[1],cdc_paths[2],cdc_paths[3],cdc_paths[4],cdc_paths[5],cdc_paths[6],
        cdc_paths[7],cdc_paths[8],cdc_paths[9],cdc_paths[10],cdc_paths[11],cdc_paths[12],cdc_paths[13],cdc_paths[14],cdc_paths[15]);
    return status;
}
