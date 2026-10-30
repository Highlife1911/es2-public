/* vehicle_aux group: shield_max_set 413ab8, shield_capacity 417bec,
 * damage_score 41537c, facing_part 41b534 and vehicle_keys 4157c8
 * (docs/reverse-engineering/vehicle.md, "In-vitro evidence"). Direct cases run
 * the original body and the core against logging stubs for every game callee of
 * the hook under test (multiply_q10, part_ratio and shield_max_set under
 * shield_capacity; the vehicle slot +58h and aim_angles under facing_part;
 * weapon_keys, euler_matrix, 4801d4 and 438d9c under vehicle_keys): ordered
 * arguments, entry EAX/EDX/ECX (stack values masked), payloads by content, and
 * for vehicle_keys' callees the ESI they receive. The originals are called with
 * a chosen ESI (4157c8 returns its high word). Stubs return edge values and
 * change what the originals reread after a call (the component pointer and its
 * slot byte, the part words, the pose angles, the player pointer, the cheat
 * flag, the step words, the cache word and the heading). The globals 4a9b84,
 * 49ef38, 4a9ccc/4a9cce, 49b088 and the weight/step tables 499fe0..49a043
 * (randomized in a quarter of the cases) are compared and restored. Then
 * installed 31/a7 poison probes (vehicle_keys through its entry shim) and
 * real-callee passes with the nested hooks in rotating modes: multiply_q10,
 * part_ratio and shield_max_set under shield_capacity; part_position (the real
 * slot +58h, with euler_matrix and 4801d4 below it) and aim_angles under
 * facing_part; weapon_keys and euler_matrix (4801d4 real) under vehicle_keys,
 * where only 438d9c stays a stub. */
typedef struct vx_record { uint32_t id,args[3],regs[3],input[10],eax,edx; } vx_record;
enum { VX_SIZE=0x1000, VX_A=0x040, VX_PL=0x380, VX_G=0x618, VX_VT=0x810, VX_VAL=0x870, VX_GRP=0x8a0, VX_FLG=0x8e0,
    VX_MAX=0x920, VX_TAB=0x950, VX_REC=0x960, VX_CFG=0xb70, VX_X=0xc40, VX_XDEF=0xc60, VX_T=0xc80, VX_W=0xcb0,
    VX_GAUGE=0xcd0, VX_OFFS=0xce0, VX_PREC=0xdd0, VX_PPTR=0xfe0, VX_ITEMS=0xfe8,
    VX_LOG=64, VX_IDS=9, VX_GLOBALS=4*8+0x64 };
#define VX_BASE ((uint32_t)(uintptr_t)vx_arena)
static BYTE vx_arena[VX_SIZE],vx_before[VX_SIZE],vx_after[VX_SIZE];
static vx_record vx_log[VX_LOG];
static unsigned vx_calls;
static int vx_real;
static uint32_t vx_seed,vx_stream;
static BYTE vx_pristine[4*8+0x64];   /* the globals before the group ran */
__attribute__((used)) static uint32_t vx_ecx,vx_esi;
/* 0 47e0b0 multiply_q10, 1 40dbc0 part_ratio, 2 413ab8 shield_max_set,
 * 3 410f78 weapon_keys, 4 47ebb8 euler_matrix, 5 4801d4, 6 438d9c,
 * 7 492990 aim_angles; 8 the vehicle slot +58h (arena vtable). */
static const uint32_t vx_addresses[VX_IDS]={0x47e0b0,0x40dbc0,0x413ab8,0x410f78,0x47ebb8,0x4801d4,0x438d9c,0x492990,0};
static const uint32_t vx_slots[VX_IDS]={0,0,0,0,0,0,0,0,0x58};
static const unsigned vx_counts[VX_IDS]={2,2,2,2,2,3,3,3,3};
/* Globals: the player pointer, the cheat flag, the step words, the cockpit
 * pointer and the weight and step tables (one window). */
static const uint32_t vx_window_rva[5]={0xa9b80,0x9ef34,0xa9cc8,0x9b084,0x99fe0},vx_window_size[5]={8,8,8,8,0x64};
static uint32_t vx_next(void){
    uint32_t z=vx_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *vx_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int vx_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static int vx_ok(uint32_t p,uint32_t n){
    const NT_TIB *t=(const NT_TIB *)NtCurrentTeb();uint32_t low=(uint32_t)(uintptr_t)t->StackLimit,top=(uint32_t)(uintptr_t)t->StackBase;
    uint32_t image=(uint32_t)(uintptr_t)mapped;
    return (p-VX_BASE<VX_SIZE&&VX_SIZE-(p-VX_BASE)>=n)||(p>=low&&p<top&&top-p>=n)||(p-image<0x2d8000u&&0x2d8000u-(p-image)>=n);
}
static void vx_dwords(vx_record *v,unsigned at,uint32_t p,unsigned n){
    if(vx_ok(p,4u*n))for(unsigned j=0;j<n;++j)v->input[at+j]=get32(vx_host(p)+4*j);
}
static void vx_words(vx_record *v,unsigned at,uint32_t p,unsigned n){
    if(vx_ok(p,2u*n))for(unsigned j=0;j<n;++j)v->input[at+j]=get16(vx_host(p)+2*j);
}
static void vx_fill(uint32_t p,unsigned n){if(vx_ok(p,n))for(unsigned j=0;j<n;++j)vx_host(p)[j]=(BYTE)vx_next();}
__attribute__((used,noinline)) static uint64_t __cdecl vx_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(vx_calls==VX_LOG){fail("vehicle_aux synthetic call overflow");ExitProcess(1);}
    vx_record *v=vx_log+vx_calls++;memset(v,0,sizeof(*v));v->id=id;
    for(unsigned k=0;k<vx_counts[id];++k)v->args[k]=vx_stack(a[k])?0:a[k];
    if(!vx_real)for(unsigned j=0;j<3;++j)v->regs[j]=vx_stack(regs[j])?0:regs[j];
    /* vehicle_keys' callees: the whole ESI (the caller's, SI = 1 after 410f78). */
    if(id>=3&&id<=6)v->input[9]=vx_esi;
    uint32_t eax=vx_next(),edx=vx_next(),pick=vx_next();vx_ecx=vx_next();
    BYTE *A=vx_arena+VX_A;const uint32_t b=VX_BASE;
    switch(id){
    case 0: /* multiply_q10: the component pointer, its slot byte and the
             * configured base (read once before) may change. */
        if(pick%8==1)put32(A+0x30f,pick>>8&1?0:b+VX_X);
        if(pick%8==2)vx_arena[VX_XDEF+0x17]=(BYTE)vx_next();
        if(pick%8==3)put16(vx_arena+VX_CFG+0xc0,(uint16_t)vx_next());
        break;
    case 1: { /* part_ratio: AX around E1h and the multiples of 51. */
        static const uint16_t words[]={0xe0,0xe1,0xe2,0,1,0xffff,0x8000,0x7fff,0x33,0x32,0x34,0xffcd,0xffcc,0x66,0xdf};
        if(pick%4)eax=(eax&0xffff0000u)|words[(pick>>2)%15];
        if(pick%16==5)put16(vx_arena+VX_CFG+0xc0,(uint16_t)vx_next());
        break; }
    case 2:break;
    case 3: /* weapon_keys: AX zero in three of four cases; the globals and
             * the object words that 4157c8 reads afterwards may change. */
        if(pick%4)eax&=0xffff0000u;
        switch(pick>>4&15){
        case 1:put32(mapped+0x9ef38,vx_next()%2?0:vx_next()|1u);break;
        case 2:put32(mapped+0xa9b84,b+(pick>>8&1?VX_A:VX_PL));break;
        case 3:put16(mapped+0xa9ccc,(uint16_t)vx_next());put16(mapped+0xa9cce,(uint16_t)vx_next());break;
        case 4:put16(A+0x32,pick>>8&1?0:(uint16_t)vx_next());break;
        case 5:put16(A+0x10,(uint16_t)vx_next());break;
        case 6:put32(mapped+0x9b088,b+VX_G+(pick>>8&1?8u:0u));break;
        default:break;
        }
        break;
    case 4: /* euler_matrix: angles by content; 20 matrix bytes rewritten;
             * the move step may change. */
        vx_words(v,0,a[0],3);vx_fill(a[1],20);
        if(pick%5==0)put16(mapped+0xa9ccc,(uint16_t)vx_next());
        break;
    case 5: /* 4801d4: the offset vector by content, out written. */
        vx_words(v,0,a[0],3);
        if(vx_ok(a[2],12))for(unsigned j=0;j<3;++j)put32(vx_host(a[2])+4*j,vx_next());
        break;
    case 6:break;
    case 7: { /* aim_angles: end and start by content; three words at T's
               * angles plus edge offsets; T's words may change after. */
        static const uint16_t edges[]={0x8000,0x7fff,0x8001,0,1,0xffff,0x4000,0xc000};
        vx_dwords(v,0,a[1],3);vx_dwords(v,3,a[2],3);
        const uint32_t pose=a[2]-0x1au;
        if(vx_ok(a[0],6))for(unsigned j=0;j<3;++j){
            uint16_t base=vx_ok(pose,6)?get16(vx_host(pose)+2*j):0,x=vx_next()%3?edges[vx_next()%8]:(uint16_t)vx_next();
            put16(vx_host(a[0])+2*j,(uint16_t)(base+x));}
        if(pick%7==0)put16(vx_arena+VX_T+2*((pick>>8)%3),(uint16_t)vx_next());
        break; }
    case 8: /* slot +58h: out written; later part words and the part word
             * pointer (read once by 41b534) may change. */
        if(vx_ok(a[2],12))for(unsigned j=0;j<3;++j)put32(vx_host(a[2])+4*j,vx_next());
        if(pick%5==0)put16(vx_arena+VX_FLG+2*((a[1]+1u+(pick>>8)%3)%29u),(uint16_t)(pick>>16&1?0:vx_next()|1u));
        if(pick%11==0)put32(A+0x20e,b+VX_FLG+2u);
        break;
    }
    v->eax=vx_stack(eax)?0:eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define VX_STUB(n) __attribute__((naked)) static void vx_stub_##n(void){__asm__ volatile( \
    "movl %esi,_vx_esi\n\tpushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _vx_stub_impl\n\taddl $24,%esp\n\tmovl _vx_ecx,%ecx\n\tret\n\t");}
VX_STUB(0) VX_STUB(1) VX_STUB(2) VX_STUB(3) VX_STUB(4) VX_STUB(5) VX_STUB(6) VX_STUB(7) VX_STUB(8)
#undef VX_STUB
static void *vx_stubs[VX_IDS]={vx_stub_0,vx_stub_1,vx_stub_2,vx_stub_3,vx_stub_4,vx_stub_5,vx_stub_6,vx_stub_7,vx_stub_8};
static void vx_reset(void){vx_calls=0;vx_stream=vx_seed;}
static void vx_globals(BYTE *out,int restore){
    for(unsigned i=0,at=0;i<5;at+=vx_window_size[i++]){
        if(restore)memcpy(mapped+vx_window_rva[i],out+at,vx_window_size[i]);else memcpy(out+at,mapped+vx_window_rva[i],vx_window_size[i]);
    }
}
static uint16_t vx_pick(const uint16_t *edges,unsigned count){return random32()%4?edges[random32()%count]:(uint16_t)random32();}
static uint32_t vx_small(uint32_t range){return (uint32_t)((int32_t)(random32()%(2u*range+1u))-(int32_t)range);}
/* Part tables P = A+206h: values VX_VAL, group values VX_GRP, part words
 * VX_FLG (P+8 = A+20Eh), records VX_REC (18 bytes) and maxima VX_MAX. */
static void vx_tables(int real){
    const uint32_t b=VX_BASE;BYTE *A=vx_arena+VX_A;
    put32(A+0x206,b+VX_VAL);put32(A+0x20a,b+VX_GRP);put32(A+0x20e,b+VX_FLG);put32(A+0x212,b+VX_TAB);
    put32(vx_arena+VX_TAB,b+VX_REC);put32(vx_arena+VX_TAB+4,b+VX_MAX);
    static const uint16_t values[]={0,1,0x95,0x96,0x97,0xffff,0x8000,0x7fff,0x50,0x10};
    static const uint16_t bases[]={1,0xffff,0x7fff,0x8000,100,0x96,2,0xff00};
    unsigned density=random32()%4;
    for(unsigned i=0;i<29;++i)put16(vx_arena+VX_FLG+2*i,(uint16_t)(random32()%4<density?0:random32()|1u));
    for(unsigned i=0;i<32;++i)put16(vx_arena+VX_GRP+2*i,random32()%2?(uint16_t)(random32()%0x96u):vx_pick(values,10));
    for(unsigned i=0;i<24;++i){
        put16(vx_arena+VX_VAL+2*i,random32()%2?(uint16_t)(random32()%0x96u):vx_pick(values,10));
        put16(vx_arena+VX_MAX+2*i,(uint16_t)(random32()%4==0?0:real?random32()%0x8000u:vx_pick(bases,8)));
    }
    for(unsigned i=0;i<29;++i){
        BYTE *rec=vx_arena+VX_REC+18*i;uint16_t base=real?(uint16_t)(1u+random32()%0x7fffu):vx_pick(bases,8);
        put16(rec,base?base:1u);
        if(real){put16(rec+6,(uint16_t)(random32()%5));put32(rec+8,b+VX_ITEMS);}
    }
    for(unsigned k=0;k<4;++k)put16(vx_arena+VX_ITEMS+4*k+2,(uint16_t)(random32()%12));
}
static void vx_build(unsigned index,unsigned n,uint32_t a[4],uint32_t r[3],uint32_t *esi,int real){
    const uint32_t b=VX_BASE;BYTE *A=vx_arena+VX_A;
    vx_globals(vx_pristine,1);vx_seed=random32();random_bytes(vx_arena,VX_SIZE);memset(a,0,16);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    *esi=random32();
    if(random32()%4==0)random_bytes(mapped+0x99fe0,0x64);
    put32(A,b+VX_VT);put32(vx_arena+VX_VT+0x58,real?(uint32_t)(uintptr_t)(mapped+0x1b60c):(uint32_t)(uintptr_t)vx_stubs[8]);
    vx_tables(real&&index==ES2_HOOK_INDEX_shield_capacity);
    (void)n;
    if(index==ES2_HOOK_INDEX_shield_max_set){a[0]=b+VX_A+0x222u-(random32()%4==0?random32()%0x20u:0u);a[1]=random32();return;}
    if(index==ES2_HOOK_INDEX_damage_score){
        /* Zero bases only where loop 1 never divides (precondition). */
        for(unsigned i=0;i<19;++i){
            BYTE *rec=vx_arena+VX_REC+18*i;int16_t v=(int16_t)get16(vx_arena+VX_GRP+2*i);
            if(!get16(rec)&&get16(vx_arena+VX_FLG+2*i)&&v>=0&&v<0x96)put16(rec,1);
        }
        a[0]=b+VX_A+0x206u;return;
    }
    if(index==ES2_HOOK_INDEX_shield_capacity){
        static const uint16_t maxima[]={1,2,0x7fff,0x8000,0xffff,0xff00,100,200,256,512,3,25};
        uint16_t m=random32()%3?maxima[random32()%12]:(uint16_t)random32();if(!m)m=1;
        if(real&&(int16_t)m<=0)m=(uint16_t)(1u+random32()%0x7fffu);
        uint16_t v;
        switch(random32()%6){
        case 0:v=(uint16_t)(0x8000u|random32());break;
        case 1:case 2:v=(uint16_t)((int32_t)(int16_t)m/2+(int32_t)vx_small(3));break;   /* q near 80h */
        case 3:v=(uint16_t)(random32()%0x200u);break;
        case 4:v=0;break;
        default:v=(uint16_t)random32();break;
        }
        put16(vx_arena+VX_VAL+8,v);put16(vx_arena+VX_MAX+8,m);
        static const uint16_t bases[]={0,1,0x7fff,0x8000,3500,0xffff,0x100};
        put32(A+0x1f2,b+VX_CFG);put16(vx_arena+VX_CFG+0xc0,vx_pick(bases,7));
        put32(A+0x30f,random32()%4==0?0:b+VX_X);put32(vx_arena+VX_X+0x18,b+VX_XDEF);
        static const BYTE slots[]={0,1,9,0x7f,0x80,0xff,0xfe,0xed,0xec};
        vx_arena[VX_XDEF+0x17]=real?(BYTE)(random32()%10):random32()%4?slots[random32()%9]:(BYTE)random32();
        a[0]=b+VX_A;return;
    }
    if(index==ES2_HOOK_INDEX_facing_part){
        if(real){
            /* part_position's paths without the shape manager: offset null
             * (position copy) or a node word <= 0 (euler_matrix + 4801d4). */
            static const uint16_t nodes[]={0,0xffff,0x8000,0xfff0};
            put32(A+0x1fa,b+VX_PPTR);put32(vx_arena+VX_PPTR,b+VX_PREC);
            for(unsigned i=0;i<29;++i){
                BYTE *rec=vx_arena+VX_PREC+18*i;put16(rec+0xc,nodes[random32()%4]);
                put32(rec+0xe,random32()%4==0?0:b+VX_OFFS+8u*i);
                for(unsigned j=0;j<3;++j)put16(vx_arena+VX_OFFS+8*i+2*j,(uint16_t)vx_small(1u<<12));
            }
            put16(A+0x32,(uint16_t)(random32()%2));
            for(unsigned j=0;j<3;++j){put32(A+0x26+4*j,vx_small(1u<<20));put32(vx_arena+VX_T+0x1a+4*j,vx_small(1u<<20));}
        }
        a[0]=b+VX_A;a[1]=b+VX_T;return;
    }
    /* vehicle_keys */
    static const uint16_t codes[]={0x1a,0x1b,0x4c,0x248,0x24b,0x24d,0x250,0x44b,0x44d,0x602,0x603,0x606,0x60a,
        0x19,0x1c,0x4b,0x4d,0x247,0x249,0x24a,0x24c,0x24e,0x24f,0x251,0x44a,0x44c,0x44e,0x601,0x60b,0,0xffff,0x8000,0x11,0x0c};
    uint32_t code=random32()%8?codes[random32()%34]:random32()%2?(uint16_t)random32():(uint16_t)(random32()%0x700u);
    if(real&&(code==0x11||code==0x211||code==0x2c||code==0x30||code-0x202u<10u))code=0x0c;
    a[0]=b+VX_A;a[1]=(random32()%2?random32()&0xffff0000u:0)|code;
    put32(mapped+0xa9b84,b+(random32()%8?VX_PL:VX_A));
    put32(vx_arena+VX_PL+0x202,b+VX_W);put32(A+0x202,b+VX_W);vx_arena[VX_W+0x1d]=0xff;
    static const uint32_t flags[]={0,0,1,0x80000000u,0x100};
    put32(mapped+0x9ef38,random32()%3?flags[random32()%5]:random32());
    static const uint16_t steps[]={0,1,0x7fff,0x8000,0xffff,500,9000};
    put16(mapped+0xa9ccc,vx_pick(steps,7));put16(mapped+0xa9cce,vx_pick(steps,7));
    put32(mapped+0x9b088,b+VX_G);put32(vx_arena+VX_G+0x1e9,b+VX_GAUGE);put32(vx_arena+VX_G+8+0x1e9,b+VX_GAUGE+4);
    put16(A+0x32,(uint16_t)(random32()%2?0:random32()%2?1:random32()));
}
/* Calls the original with chosen EAX/EDX/ECX and ESI; three argument slots. */
__attribute__((naked)) static void vx_machine(void *target,const uint32_t *a,const uint32_t *registers,uint32_t esi,uint32_t pair[2]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl 12(%ebp),%esi\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tmovl 20(%ebp),%esi\n\t"
        "call *8(%ebp)\n\tmovl 24(%ebp),%ecx\n\tmovl %eax,(%ecx)\n\tmovl %edx,4(%ecx)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* The same from a stack filled with a pattern, EAX = EDX = ECX = pattern. */
__attribute__((naked)) static void vx_poison_call(void *target,const uint32_t *a,uint32_t pattern,uint32_t esi,uint32_t pair[2]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 16(%ebp),%eax\n\tmovl $1024,%ecx\n\trep stosl\n\taddl $3840,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%eax\n\tmovl %eax,%edx\n\tmovl %eax,%ecx\n\tmovl 20(%ebp),%esi\n\tcall *8(%ebp)\n\t"
        "movl 24(%ebp),%ecx\n\tmovl %eax,(%ecx)\n\tmovl %edx,4(%ecx)\n\tleal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static int vx_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const vx_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,vx_calls);
    for(unsigned j=0;j<calls&&j<vx_calls&&expected;++j)if(memcmp(expected+j,vx_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(vx_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<VX_SIZE;++j)if(vx_after[j]!=vx_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,vx_after[j],vx_arena[j]);break;}
    return 1;
}
static int vx_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<VX_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
static unsigned vx_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<vx_calls;++j){unsigned id=vx_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?vx_slots[id]&&vx_slots[id]==c->key:!vx_slots[id]&&vx_addresses[id]==c->key)++hits;}
    return hits;
}
static int vx_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r,uint32_t esi){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[VX_GLOBALS],after[VX_GLOBALS],actual[VX_GLOBALS];static vx_record expected[VX_LOG];
    memcpy(vx_before,vx_arena,VX_SIZE);vx_globals(before,0);
    uint32_t got[2];vx_reset();vx_machine(mapped+d->rva,a,r,esi,got);
    uint64_t pair[2];pair[0]=(uint64_t)got[1]<<32|got[0];unsigned calls=vx_calls;memcpy(expected,vx_log,calls*sizeof(*expected));
    memcpy(vx_after,vx_arena,VX_SIZE);vx_globals(after,0);
    memcpy(vx_arena,vx_before,VX_SIZE);vx_globals(before,1);vx_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];for(unsigned i=0;i<count&&i<8;++i)counts[i]=c[i].count;
    /* vehicle_keys' adapter takes the caller's ESI in the EAX slot (shim). */
    const uint32_t entry[3]={index==ES2_HOOK_INDEX_vehicle_keys?esi:r[0],r[1],r[2]};
    pair[1]=x->replacement_context(a,d->user,entry);vx_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count&&i<8;++i)if(c[i].count-counts[i]!=vx_expected_hits(c+i))return fail("vehicle_aux callee counter delta");
    if(pair[0]!=pair[1]||calls!=vx_calls||memcmp(expected,vx_log,calls*sizeof(*expected))||memcmp(vx_after,vx_arena,VX_SIZE)||
       vx_globals_differ(after,actual))
        return vx_report("direct",n,pair,calls,expected);
    vx_globals(before,1);
    return 0;
}
static unsigned vx_which(unsigned index){
    switch(index){
    case ES2_HOOK_INDEX_shield_capacity:return 0x7u;
    case ES2_HOOK_INDEX_facing_part:return 0x80u;
    case ES2_HOOK_INDEX_vehicle_keys:return 0x78u;
    default:return 0;
    }
}
static int vx_patch(BYTE saved[VX_IDS][5],int active[VX_IDS],unsigned which,int on){
    for(unsigned i=0;i<VX_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!vx_addresses[i])continue;if(actor_patch(vx_addresses[i]-0x400000u,vx_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(vx_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
static int vx_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[4],r[3],esi,expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;vx_build(index,n,a,r,&esi,0);
        BYTE before[VX_GLOBALS],after[VX_GLOBALS],actual[VX_GLOBALS],expected_fpu[108],actual_fpu[108];static vx_record expected[VX_LOG];
        memcpy(vx_before,vx_arena,VX_SIZE);vx_globals(before,0);
        vx_reset();vx_poison_call(mapped+d->rva,a,pattern,esi,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=vx_calls;memcpy(expected,vx_log,calls*sizeof(*expected));memcpy(vx_after,vx_arena,VX_SIZE);vx_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("vehicle_aux poison install");
        memcpy(vx_arena,vx_before,VX_SIZE);vx_globals(before,1);vx_reset();
        vx_poison_call(mapped+d->rva,a,pattern,esi,actual_pair);es2_hook_fpu_save(actual_fpu);vx_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=vx_calls||memcmp(expected,vx_log,calls*sizeof(*expected))||memcmp(vx_after,vx_arena,VX_SIZE)||
           vx_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=vx_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("vehicle_aux poison uninstall");vx_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/arena/globals\n",d->name,total);
    return status;
}
/* Real callees with the nested hooks in rotating original/replaced/compare
 * modes (compare falls back to replaced where refused) and the hook itself
 * original or replaced; pair, arena, globals and the remaining stubs'
 * inputs (438d9c under vehicle_keys) against a run with only the nested
 * hooks installed. */
static int vx_real_pass(unsigned index){
    static const unsigned capacity_nested[]={ES2_HOOK_INDEX_multiply_q10,ES2_HOOK_INDEX_part_ratio,ES2_HOOK_INDEX_shield_max_set};
    static const unsigned facing_nested[]={ES2_HOOK_INDEX_part_position,ES2_HOOK_INDEX_euler_matrix,ES2_HOOK_INDEX_aim_angles};
    static const unsigned keys_nested[]={ES2_HOOK_INDEX_weapon_keys,ES2_HOOK_INDEX_euler_matrix};
    const unsigned *nested;unsigned count,stubs=0;
    if(index==ES2_HOOK_INDEX_shield_capacity){nested=capacity_nested;count=3;}
    else if(index==ES2_HOOK_INDEX_facing_part){nested=facing_nested;count=3;}
    else {nested=keys_nested;count=2;stubs=1u<<6;}
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[4]={0};
    BYTE saved[VX_IDS][5],old_globals[VX_GLOBALS];int active[VX_IDS];if(vx_patch(saved,active,stubs,1))return 1;
    static vx_record expected[VX_LOG];
    vx_globals(old_globals,0);vx_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[4],r[3],esi,expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;vx_build(index,n,a,r,&esi,1);
        BYTE before[VX_GLOBALS],after[VX_GLOBALS],actual[VX_GLOBALS];
        memcpy(vx_before,vx_arena,VX_SIZE);vx_globals(before,0);
        es2_hook hk[4];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("vehicle_aux nested install");}
        vx_reset();vx_poison_call(mapped+d->rva,a,pattern,esi,expected_pair);
        unsigned calls=vx_calls;memcpy(expected,vx_log,calls*sizeof(*expected));
        memcpy(vx_after,vx_arena,VX_SIZE);vx_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+count,d,&o,function_hook_extension(index)))return fail("vehicle_aux caller install");
        memcpy(vx_arena,vx_before,VX_SIZE);vx_globals(before,1);vx_reset();
        vx_poison_call(mapped+d->rva,a,pattern,esi,actual_pair);vx_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=vx_calls||memcmp(expected,vx_log,calls*sizeof(*expected))||memcmp(vx_after,vx_arena,VX_SIZE)||
           vx_globals_differ(after,actual)){
            fprintf(stderr,"real mode=%u\n",mode);status=vx_report("real",n,pair,calls,expected);
        }
        for(unsigned j=0;j<=count;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("vehicle_aux nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("vehicle_aux nested uninstall");}
        vx_globals(before,1);++total;
    }
    vx_real=0;vx_globals(old_globals,1);
    if(vx_patch(saved,active,0,0))return 1;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){
        fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, 31/a7 stack, pair, arena, globals and stub inputs\n",
        d->name,total,count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
static int vehicle_aux_cases(unsigned index,unsigned limit){
    vx_real=0;
    BYTE saved[VX_IDS][5],old_globals[VX_GLOBALS];int active[VX_IDS];
    vx_globals(old_globals,0);vx_globals(vx_pristine,0);if(vx_patch(saved,active,vx_which(index),1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_VEHICLE_AUX_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[4],r[3],esi;vx_build(index,n,a,r,&esi,0);status=vx_compare(index,n,a,r,esi);}
    if(!limit)coverage_enable(0);
    vx_globals(old_globals,1);
    if(!limit&&!status)status=vx_installed_probes(index,64);
    vx_globals(old_globals,1);
    if(vx_patch(saved,active,0,0))return 1;
    current_case_valid=0;
    if(!limit&&!status&&vx_which(index))status=vx_real_pass(index);
    vx_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers and ESI, payloads by content, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
