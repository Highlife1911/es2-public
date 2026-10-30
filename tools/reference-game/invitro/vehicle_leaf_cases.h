/* Vehicle leaves: condition_table 4151a4, condition_ratio 4152ec, sight_check
 * 412608, group_state 412c8c, target_bearing 41e758 and part_position 41b60c
 * (docs/reverse-engineering/vehicle.md). Direct cases run the original body and
 * the core against logging stubs for every game callee and virtual (ordered
 * arguments, entry EAX/EDX/ECX, payloads by content; values in the worker's
 * stack reservation are masked). The two pure condition functions run on
 * generated part tables; condition_ratio is called with a controlled EBX for
 * the kinds that read it. Then installed 31/a7 poison probes (through the entry
 * shim for 4152ec) and a real-callee pass with the nested hooks in rotating
 * modes (real RNG, heading/distance, Euler, transform and 4801d4); only
 * terrain_line and the virtuals stay stubs there, and their inputs are
 * compared. */
typedef struct vl_record { uint32_t id,args[4],regs[3],input[12],eax,edx; } vl_record;
enum { VL_SIZE=0x2000, VL_P=0x40, VL_VALUES=0x80, VL_GROUPS=0x100, VL_TABS=0x1a0, VL_RECORDS=0x1c0, VL_MAXIMA=0x5c0,
    VL_OUT=0x640, VL_X=0x6a0, VL_Y=0x6a4, VL_A=0x700, VL_B=0x940, VL_T2=0xb80, VL_VT=0xdc0, VL_REC1=0xe20, VL_REC2=0xe40,
    VL_G=0xe60, VL_LIST=0xe80, VL_M=0xec0, VL_S=0x1ac0, VL_OBJ=0x1ae0, VL_NODE=0x1af0, VL_VEC=0x1b00, VL_PTR=0x1b10,
    VL_RECS=0x1b20, VL_NODES=0x1d00, VL_MEMBERS=12, VL_MAX=96, VL_IDS=11, VL_GLOBALS=0x72 };
#define VL_BASE ((uint32_t)(uintptr_t)vl_arena)
static BYTE vl_arena[VL_SIZE],vl_before[VL_SIZE],vl_after[VL_SIZE];
static vl_record vl_log[VL_MAX];
static unsigned vl_calls,vl_band;
static int vl_real;
static uint32_t vl_seed,vl_stream,vl_out;
__attribute__((used)) static uint32_t vl_ecx;
/* 0 492f24, 1 4280f4, 2 V24 (sight_check); 3 V40 (group_state); 4 492934,
 * 5 49288c, 6 V3c (target_bearing); 7 V20, 8 4801d4, 9 47ebb8, 10 48043c
 * (part_position). Virtuals have no address; one table serves every object. */
static const uint32_t vl_addresses[VL_IDS]={0x492f24,0x4280f4,0,0,0x492934,0x49288c,0,0,0x4801d4,0x47ebb8,0x48043c};
static const unsigned vl_counts[VL_IDS]={2,4,1,1,2,2,1,2,3,2,3},vl_slots[VL_IDS]={0,0,0x24,0x40,0,0,0x3c,0x20,0,0,0};
static uint32_t vl_next(void){
    uint32_t z=vl_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *vl_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int vl_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static uint16_t vl_signed(uint32_t limit){return (uint16_t)(random32()%(2*limit+1)-limit);}
static void vl_vector(uint32_t at,uint32_t *in){for(unsigned j=0;j<3;++j)in[j]=get32(vl_host(at)+4*j);}
static uint16_t vl_node_word(uint32_t pick){
    static const uint16_t words[]={0xffff,0xfffe,0x8000,0,1,2,3,7,15};
    return pick%3?words[(pick>>4)%9]:(uint16_t)(vl_real?(pick>>8)%16:pick>>8);
}
__attribute__((used,noinline)) static uint64_t __cdecl vl_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    uint32_t eax=vl_next(),edx=vl_next(),pick=vl_next();vl_ecx=vl_next();
    if(vl_calls==VL_MAX){fail("vehicle leaf call overflow");ExitProcess(1);}
    vl_record *v=vl_log+vl_calls++;memset(v,0,sizeof(*v));v->id=id;
    for(unsigned k=0;k<vl_counts[id];++k)v->args[k]=vl_stack(a[k])?0:a[k];
    /* Real callees may leave frame addresses in registers; compare entry
     * registers only in the direct pass. */
    if(!vl_real)for(unsigned j=0;j<3;++j)v->regs[j]=vl_stack(regs[j])?0:regs[j];
    switch(id){
    case 1: /* terrain_line: both vectors; AX zero means a clear line (also
             * AL zero with AH set, which TEST AX,AX must not take as clear). */
        vl_vector(a[1],v->input);vl_vector(a[2],v->input+3);
        eax=pick%3==0?eax&0xffff0000u:pick%3==1?(eax&0xffff0000u)|((1u+(pick>>4)%255u)<<8):eax|1u;
        if(pick&0x100)put32(vl_host(a[0]),vl_next());
        break;
    case 2: { /* V24: a record or null; may move an object afterwards. */
        static const unsigned records[]={0,VL_REC1,VL_REC2};
        eax=records[pick%3]?VL_BASE+records[pick%3]:0;
        if(pick%7==0)put32(vl_host(VL_BASE+(pick&0x100?VL_A:VL_B)+0x26+4*((pick>>9)%3)),vl_next());
        break; }
    case 3: { /* V40: a part condition in the case's band; may change the group. */
        static const uint32_t bands[][2]={{0,60},{40,120},{100,130},{0,300}};
        uint32_t value=vl_band<4?bands[vl_band][0]+vl_next()%bands[vl_band][1]:vl_next();
        eax=(eax&0xffff0000u)|(uint16_t)value;
        if(pick%16==0)put16(vl_arena+VL_G+0x10,(uint16_t)(1+vl_next()%VL_MEMBERS));
        if(pick%16==1)vl_arena[VL_M+0x100*(vl_next()%VL_MEMBERS)+(pick&0x100?0x99:0xa4)]^=1;
        break; }
    case 4: /* heading_point(T+26h, A+26h); sometimes makes out+2 = 8000h. */
        vl_vector(a[0],v->input);vl_vector(a[1],v->input+3);
        if(pick%8==0)eax=(eax&0xffff0000u)|(uint16_t)(get16(vl_host(a[1]-0x26+0x10))+0x8000u);
        break;
    case 5:vl_vector(a[0],v->input);vl_vector(a[1],v->input+3);break;
    case 6: /* V3c; sometimes makes out+6 = 8000h. */
        if(pick%8==0)eax=(eax&0xffff0000u)|(uint16_t)(get16(vl_host(a[0]+0x10))-get16(vl_host(vl_out)));
        break;
    case 7: /* V20: node record or null; may make the computed record zero. */
        v->input[0]=a[1];eax=pick%4==0?0:VL_BASE+VL_NODE;
        put16(vl_arena+VL_NODE+4,vl_node_word(vl_next()));
        if(pick%8==1)put32(vl_arena+VL_S+0x16,0u-32u*(uint32_t)(int32_t)(int16_t)get16(vl_arena+VL_NODE+4));
        break;
    case 8: /* 4801d4 always writes its output vector (read by 48043c next). */
        vl_vector(a[0],v->input);for(unsigned j=0;j<3;++j)put32(vl_host(a[2])+4*j,vl_next());(void)pick;break;
    case 9:for(unsigned j=0;j<3;++j)v->input[j]=get16(vl_host(a[0])+2*j);
        if(pick%3)for(unsigned j=0;j<9;++j)put16(vl_host(a[1])+2*j,(uint16_t)vl_next());
        break;
    case 10:vl_vector(a[0],v->input);for(unsigned j=0;j<9;++j)v->input[3+j]=get16(vl_host(a[1])+2*j);
        for(unsigned j=0;j<3;++j)put32(vl_host(a[2])+4*j,vl_next());
        break;
    default:break;
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define VL_STUB(n) __attribute__((naked)) static void vl_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _vl_stub_impl\n\taddl $24,%esp\n\tmovl _vl_ecx,%ecx\n\tret\n\t");}
VL_STUB(0) VL_STUB(1) VL_STUB(2) VL_STUB(3) VL_STUB(4) VL_STUB(5) VL_STUB(6) VL_STUB(7) VL_STUB(8) VL_STUB(9) VL_STUB(10)
#undef VL_STUB
static void *vl_stubs[VL_IDS]={vl_stub_0,vl_stub_1,vl_stub_2,vl_stub_3,vl_stub_4,vl_stub_5,vl_stub_6,vl_stub_7,vl_stub_8,
    vl_stub_9,vl_stub_10};
/* Calls target with the six slots, EAX/EDX/ECX/EBX from regs and a
 * patterned stack below the arguments; returns EDX:EAX in pair. */
__attribute__((naked,noinline)) static void vl_call(void *target,const uint32_t *a,const uint32_t regs[4],uint32_t pattern,uint32_t pair[2]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 20(%ebp),%eax\n\tmovl $1024,%ecx\n\trep stosl\n\taddl $3840,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 20(%esi)\n\tpushl 16(%esi)\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%esi\n\tmovl (%esi),%eax\n\tmovl 4(%esi),%edx\n\tmovl 8(%esi),%ecx\n\tmovl 12(%esi),%ebx\n\tcall *8(%ebp)\n\t"
        "movl 24(%ebp),%ecx\n\tmovl %eax,(%ecx)\n\tmovl %edx,4(%ecx)\n\tleal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static void vl_globals(BYTE *out,int restore){if(restore)memcpy(mapped+0xd2599,out,VL_GLOBALS);else memcpy(out,mapped+0xd2599,VL_GLOBALS);}
static void vl_reset(void){vl_calls=0;vl_stream=vl_seed;}
/* Part tables P (vehicle +206h): values [P] and groups [P+4] with room for
 * indices -8..47, 18-byte group records and part maxima. Divisors that
 * would fault in loops 1 and 2 of 4151a4 are made nonzero. */
static void vl_tables(uint32_t P){
    uint32_t b=VL_BASE;BYTE *p=vl_host(P);
    put32(p,b+VL_VALUES+16);put32(p+4,b+VL_GROUPS+16);put32(p+0xc,b+VL_TABS);
    put32(vl_arena+VL_TABS,b+VL_RECORDS+144);put32(vl_arena+VL_TABS+4,b+VL_MAXIMA+16);
    for(int i=-8;i<48;++i){
        put16(vl_arena+VL_GROUPS+16+2*i,random32()%4?(uint16_t)(random32()%3000):vl_signed(3000));
        put16(vl_arena+VL_RECORDS+144+18*i,random32()%6?(uint16_t)(1+random32()%2000):random32()%2?vl_signed(500):0);
    }
    for(int i=-8;i<40;++i){
        put16(vl_arena+VL_VALUES+16+2*i,random32()%4?(uint16_t)(random32()%3000):vl_signed(3000));
        put16(vl_arena+VL_MAXIMA+16+2*i,random32()%6?(uint16_t)(1+random32()%3000):random32()%2?0:vl_signed(300));
    }
    for(int i=0;i<19;++i)if(!get16(vl_arena+VL_RECORDS+144+18*i))put16(vl_arena+VL_RECORDS+144+18*i,1);
    for(int i=0;i<10;++i){
        BYTE *rec=vl_arena+VL_RECORDS+144+18*(19+i);
        if((uint16_t)(get16(rec)+get16(vl_arena+VL_MAXIMA+16+2*(12+i)))==0)put16(rec,(uint16_t)(get16(rec)+1));
        if(!(uint16_t)(get16(rec)+get16(vl_arena+VL_MAXIMA+16+2*(12+i))))put16(rec,(uint16_t)(get16(rec)+1));
    }
}
static void vl_position(uint32_t at){for(unsigned j=0;j<3;++j)put32(vl_host(at)+4*j,random32()%0x80000u-0x40000u);}
static void vl_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    uint32_t b=VL_BASE;vl_seed=random32();random_bytes(vl_arena,VL_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    for(unsigned s=0;s<0x60;s+=4)put32(vl_arena+VL_VT+s,0);
    for(unsigned id=0;id<VL_IDS;++id)if(vl_slots[id])put32(vl_arena+VL_VT+vl_slots[id],(uint32_t)(uintptr_t)vl_stubs[id]);
    if(real){BYTE *state=mapped+0xd2599;random_bytes(state,112);state[112]=(BYTE)(random32()%56);state[113]=(BYTE)(random32()%56);}
    uint32_t A=b+VL_A;
    if(index==ES2_HOOK_INDEX_condition_table){vl_tables(b+VL_P);a[0]=b+VL_P;a[1]=b+VL_OUT;return;}
    if(index==ES2_HOOK_INDEX_condition_ratio){
        vl_tables(b+VL_P);
        /* Kinds 0..2 (index -4..24) and others with EBX/ECX on two words. */
        static const uint16_t others[]={3,4,0xffff,0x8000,0x7fff,0x100};
        uint16_t kind=n%5<3?(uint16_t)(n%5):others[random32()%6];
        a[0]=b+VL_P;a[1]=(random32()&0xffff0000u)|kind;a[2]=(random32()&0xffff0000u)|(uint16_t)(random32()%29-4);
        put16(vl_arena+VL_X,random32()%4?(uint16_t)(random32()%3000):vl_signed(3000));
        put16(vl_arena+VL_Y,random32()%5?(uint16_t)(random32()%3000):random32()%2?0:vl_signed(300));
        r[0]=b+VL_Y;r[2]=b+VL_X;return;
    }
    put32(vl_arena+VL_A,b+VL_VT);
    if(index==ES2_HOOK_INDEX_sight_check){
        uint32_t B=random32()%16?b+VL_B:A;put32(vl_host(B),b+VL_VT);
        vl_position(A+0x26);vl_position(B+0x26);
        put16(vl_host(B+0x1e3),random32()%2?0:(uint16_t)(1+random32()%0xffff));
        put16(vl_host(B+0x4b),(uint16_t)(random32()%48-8));
        put32(vl_arena+VL_REC1+0x1c,random32()%2000);put32(vl_arena+VL_REC2+0x1c,random32());
        a[0]=A;a[1]=B;return;
    }
    if(index==ES2_HOOK_INDEX_group_state){
        uint32_t G=b+VL_G;put32(vl_host(G+0xc),b+VL_LIST);
        static const uint16_t counts[]={1,2,3,4,5,8,12,0xffff,0xfffd};
        put16(vl_host(G+0x10),counts[random32()%9]);
        for(unsigned i=0;i<16;++i)put32(vl_arena+VL_LIST+4*i,b+VL_M+0x100*(i%VL_MEMBERS));
        unsigned down=random32()%4;
        for(unsigned i=0;i<VL_MEMBERS;++i){BYTE *M=vl_arena+VL_M+0x100*i;put32(M,b+VL_VT);
            M[0xa4]=(BYTE)(random32()%4<down?1+random32()%255:0);M[0x99]=(BYTE)(random32()%4<down?1+random32()%255:0);}
        vl_band=random32()%5;a[0]=G;return;
    }
    if(index==ES2_HOOK_INDEX_target_bearing){
        uint32_t T=random32()%2?b+VL_B:0,D=b+VL_T2;
        put32(vl_host(A+0x1a4),D);put32(vl_host(b+VL_B),b+VL_VT);put32(vl_host(D),b+VL_VT);
        vl_position(A+0x26);vl_position(b+VL_B+0x26);vl_position(D+0x26);
        vl_out=b+VL_OUT;a[0]=A;a[1]=vl_out;a[2]=T;return;
    }
    /* part_position */
    uint32_t S=b+VL_S;
    put32(vl_host(A+0x1fa),b+VL_PTR);put32(vl_arena+VL_PTR,b+VL_RECS+72);put32(vl_host(A+0x34),S);
    put32(vl_host(S+4),b+VL_OBJ);put32(vl_arena+VL_OBJ,b+VL_VT);put32(vl_host(S+0x16),real?b+VL_NODES:random32());
    for(int i=-4;i<21;++i){BYTE *rec=vl_arena+VL_RECS+72+18*i;
        put32(rec+0xe,random32()%4?b+VL_VEC:0);
        put16(rec+0xc,random32()%4?(uint16_t)(1+random32()%300):random32()%2?0:vl_signed(300));}
    put16(vl_host(A+0x32),random32()%2?0:(uint16_t)(1+random32()%0xffff));
    for(unsigned j=0;j<3;++j)put16(vl_host(A+0xc)+2*j,(uint16_t)random32());
    vl_position(A+0x26);
    a[0]=A;a[1]=(random32()&0xffff0000u)|(uint16_t)(random32()%25-4);a[2]=b+VL_OUT;(void)n;
}
static int vl_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const vl_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,vl_calls);
    for(unsigned j=0;j<calls&&j<vl_calls&&expected;++j)if(memcmp(expected+j,vl_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(vl_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<VL_SIZE;++j)if(vl_after[j]!=vl_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,vl_after[j],vl_arena[j]);break;}
    return 1;
}
static unsigned vl_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<vl_calls;++j){unsigned id=vl_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?vl_slots[id]==c->key:!vl_slots[id]&&vl_addresses[id]==c->key)++hits;}
    return hits;
}
/* Real 4801d4/48043c leave a frame address in EDX (their own, or derived
 * from part_position's frame vector), which differs between the original
 * and the replacement's stack depth; EDX is dead at all nine V58 call sites
 * (vehicle.md). Only the real pass can produce such a value. */
static int vl_same_pair(unsigned index,uint64_t x,uint64_t y){
    if(x==y)return 1;
    return vl_real&&index==ES2_HOOK_INDEX_part_position&&(uint32_t)x==(uint32_t)y&&vl_stack((uint32_t)(x>>32))&&vl_stack((uint32_t)(y>>32));
}
static uint64_t vl_original(unsigned index,const uint32_t *a,const uint32_t *r){
    if(index!=ES2_HOOK_INDEX_condition_ratio)return weapons_original(index,a,r);
    uint32_t regs[4]={r[1],r[1],r[2],r[0]},pair[2];vl_call(mapped+function_hook_descriptor(index)->rva,a,regs,0x31313131u,pair);
    return (uint64_t)pair[1]<<32|pair[0];
}
static int vl_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[VL_GLOBALS],after[VL_GLOBALS],actual[VL_GLOBALS];static vl_record expected[VL_MAX];
    memcpy(vl_before,vl_arena,VL_SIZE);vl_globals(before,0);
    vl_reset();uint64_t pair[2];pair[0]=vl_original(index,a,r);unsigned calls=vl_calls;memcpy(expected,vl_log,calls*sizeof(*expected));
    memcpy(vl_after,vl_arena,VL_SIZE);vl_globals(after,0);
    memcpy(vl_arena,vl_before,VL_SIZE);vl_globals(before,1);vl_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);vl_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=vl_expected_hits(c+i))return fail("vehicle leaf callee counter delta");
    if(pair[0]!=pair[1]||calls!=vl_calls||memcmp(expected,vl_log,calls*sizeof(*expected))||memcmp(vl_after,vl_arena,VL_SIZE)||
       memcmp(after,actual,VL_GLOBALS))
        return vl_report("direct",n,pair,calls,expected);
    return 0;
}
static int vl_patch(BYTE saved[VL_IDS][5],int active[VL_IDS],unsigned which,int on){
    for(unsigned i=0;i<VL_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!vl_addresses[i])continue;if(actor_patch(vl_addresses[i]-0x400000u,vl_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(vl_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
static void vl_entry(unsigned index,const uint32_t *r,uint32_t pattern,uint32_t regs[4]){
    regs[0]=regs[1]=regs[2]=pattern;regs[3]=pattern;
    if(index==ES2_HOOK_INDEX_condition_ratio){regs[2]=r[2];regs[3]=r[0];}
}
static int vl_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],regs[4],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;vl_build(index,n,a,r,0);
        vl_entry(index,r,pattern,regs);
        BYTE before[VL_GLOBALS],after[VL_GLOBALS],actual[VL_GLOBALS],expected_fpu[108],actual_fpu[108];static vl_record expected[VL_MAX];
        memcpy(vl_before,vl_arena,VL_SIZE);vl_globals(before,0);
        vl_reset();vl_call(mapped+d->rva,a,regs,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=vl_calls;memcpy(expected,vl_log,calls*sizeof(*expected));memcpy(vl_after,vl_arena,VL_SIZE);vl_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("vehicle leaf poison install");
        memcpy(vl_arena,vl_before,VL_SIZE);vl_globals(before,1);vl_reset();vl_call(mapped+d->rva,a,regs,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);vl_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=vl_calls||memcmp(expected,vl_log,calls*sizeof(*expected))||memcmp(vl_after,vl_arena,VL_SIZE)||
           memcmp(after,actual,VL_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=vl_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("vehicle leaf poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state\n",d->name,total);
    return status;
}
/* Real callees with the nested hooks in rotating original/replaced/compare
 * modes and a patterned stack; the stubs' arguments and payloads are
 * compared in order (entry registers only in the direct pass). */
static int vl_real_pass(unsigned index){
    static const unsigned sight_nested[]={ES2_HOOK_INDEX_rng_bounded};
    static const unsigned bearing_nested[]={ES2_HOOK_INDEX_heading_point,ES2_HOOK_INDEX_dist_gate};
    static const unsigned position_nested[]={ES2_HOOK_INDEX_euler_matrix,ES2_HOOK_INDEX_transform_point};
    const unsigned *nested;unsigned count,stubs;
    if(index==ES2_HOOK_INDEX_sight_check){nested=sight_nested;count=1;stubs=1u<<1;}
    else if(index==ES2_HOOK_INDEX_target_bearing){nested=bearing_nested;count=2;stubs=0;}
    else {nested=position_nested;count=2;stubs=0;}
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[4]={0};
    BYTE saved[VL_IDS][5];int active[VL_IDS];if(vl_patch(saved,active,stubs,1))return 1;
    static vl_record expected[VL_MAX];
    vl_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<1024&&!status;++n){
        uint32_t a[6],r[3],regs[4],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;vl_build(index,n,a,r,1);
        vl_entry(index,r,pattern,regs);
        BYTE before[VL_GLOBALS],after[VL_GLOBALS],actual[VL_GLOBALS];
        memcpy(vl_before,vl_arena,VL_SIZE);vl_globals(before,0);
        es2_hook hk[4];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("vehicle leaf nested install");}
        vl_reset();vl_call(mapped+d->rva,a,regs,pattern,expected_pair);
        unsigned calls=vl_calls;memcpy(expected,vl_log,calls*sizeof(*expected));
        memcpy(vl_after,vl_arena,VL_SIZE);vl_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+count,d,&o,function_hook_extension(index)))return fail("vehicle leaf caller install");
        memcpy(vl_arena,vl_before,VL_SIZE);vl_globals(before,1);vl_reset();
        vl_call(mapped+d->rva,a,regs,pattern,actual_pair);vl_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(!vl_same_pair(index,pair[0],pair[1])||calls!=vl_calls||memcmp(expected,vl_log,calls*sizeof(*expected))||
           memcmp(vl_after,vl_arena,VL_SIZE)||memcmp(after,actual,VL_GLOBALS)){
            fprintf(stderr,"real mode=%u\n",mode);status=vl_report("real",n,pair,calls,expected);
        }
        for(unsigned j=0;j<=count;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("vehicle leaf nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("vehicle leaf nested uninstall");}
        vl_globals(before,1);++total;
    }
    vl_real=0;
    if(vl_patch(saved,active,0,0))return 1;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){
        fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, 31/a7 stack, pair, memory, globals and stub inputs\n",
        d->name,total,count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
static int vehicle_cases(unsigned index,unsigned limit){
    unsigned which=index==ES2_HOOK_INDEX_sight_check?0x3u:index==ES2_HOOK_INDEX_target_bearing?0x30u:
        index==ES2_HOOK_INDEX_part_position?0x700u:0;
    BYTE saved[VL_IDS][5],old_globals[VL_GLOBALS];int active[VL_IDS];
    vl_globals(old_globals,0);if(vl_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_VEHICLE_LEAF_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];vl_build(index,n,a,r,0);status=vl_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=vl_installed_probes(index,64);
    if(vl_patch(saved,active,0,0))return 1;
    current_case_valid=0;
    if(!limit&&!status&&(index==ES2_HOOK_INDEX_sight_check||index==ES2_HOOK_INDEX_target_bearing||index==ES2_HOOK_INDEX_part_position))
        status=vl_real_pass(index);
    vl_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, payloads by content, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
