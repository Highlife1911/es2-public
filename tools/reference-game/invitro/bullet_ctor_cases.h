/* Bullet constructor: bullet_create 40b43c, bullet_init 40af6c and def_lookup
 * 40ffc8 (docs/reverse-engineering/weapons.md, "bullet constructor"). Direct
 * cases run the original body and the core against logging stubs for every game
 * callee of the hook under test (ordered argument slots, entry EAX/EDX/ECX,
 * buffers by content, stack addresses masked); the runtime frame setup 49630c
 * is a record stub that keeps the real FS:0 head, as in lc_base_cases.h. The
 * stubs change what the originals reread (the spread word [D+0Ah] around each
 * draw, the definition pointer [O+4Eh], the division flag and divisor, the
 * sound word, the runtime counter pointer). def_lookup is a leaf: random tables
 * and counts (negative, zero, small, 7FFFh with a planted hit). Then installed
 * 31/a7 poison probes; for bullet_create (entry shim) the stubs also record
 * EBX, ESI, EDI, EBP, ESP and stack arguments relative to the caller's argument
 * slots, the returned ECX is compared and the bytes below the slots (the frame
 * and the callees' area) are compared after return. Then a real-callee pass
 * with the nested hooks in rotating modes. Identifiers use the prefix
 * bcc_/BCC_. */
typedef struct bcc_record { uint32_t id,args[3],regs[3],input[3],eax,edx,saved[5]; } bcc_record;
enum { BCC_SIZE=0x1000, BCC_OWNER=0x100, BCC_VT=0x180, BCC_POS=0x200, BCC_ANG=0x210, BCC_DEFS=0x300,
    BCC_SHAPES=0x500, BCC_REC=0x600, BCC_RECS=16, BCC_COUNTER=0x880, BCC_COUNTER2=0x884, BCC_POOL=0x890,
    BCC_NODES=0xa00, BCC_D=0xc00, BCC_MAX=32, BCC_IDS=10, BCC_GLOBALS=136, BCC_WINDOW=0x200 };
#define BCC_BASE ((uint32_t)(uintptr_t)bcc_arena)
#define BCC_OBJ(i) (BCC_BASE+BCC_NODES+8u+0x80u*(uint32_t)(i))
#define BCC_RECORD(i) (BCC_BASE+BCC_REC+0x24u*(uint32_t)(i))
static BYTE bcc_arena[BCC_SIZE],bcc_before[BCC_SIZE],bcc_after[BCC_SIZE];
static bcc_record bcc_log[BCC_MAX];
static unsigned bcc_calls;
static int bcc_real,bcc_track;
static uint32_t bcc_seed,bcc_stream;
__attribute__((used)) static uint32_t bcc_ecx;
/* EBX, ESI, EDI, EBP, ESP at the last stub entry; bcc_invoke's ESP before
 * its call; the bytes below the argument slots after its call. */
__attribute__((used)) static uint32_t bcc_entry[5],bcc_origin;
__attribute__((used)) static BYTE bcc_frame_copy[BCC_WINDOW];
/* 0 49630c (record), 1 471b30 pool_alloc, 2 40af6c bullet_init, 3 40adc0
 * bullet_def, 4 492ee0 rng_next, 5 49681a memset, 6 4628e0 sound, 7
 * 402188 object_init, 8 40ffc8 def_lookup; 9 the owner's virtual +38h
 * (arena vtable, never patched). */
static const uint32_t bcc_addresses[BCC_IDS]={0x49630c,0x471b30,0x40af6c,0x40adc0,0x492ee0,0x49681a,0x4628e0,0x402188,0x40ffc8,0};
static const unsigned bcc_slots[BCC_IDS]={0,0,0,0,0,0,0,0,0,0x38};
static const unsigned bcc_counts[BCC_IDS]={0,1,2,1,1,3,2,2,2,1};
static uint32_t bcc_next(void){
    uint32_t z=bcc_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *bcc_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int bcc_in(uint32_t a,uint32_t n){return a-BCC_BASE<BCC_SIZE&&BCC_SIZE-(a-BCC_BASE)>=n;}
static int bcc_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static uint32_t bcc_mask(uint32_t value){return bcc_stack(value)?(bcc_track?value-bcc_origin:0):value;}
static void bcc_dwords(bcc_record *v,uint32_t p,unsigned n){
    if(bcc_in(p,4u*n))for(unsigned j=0;j<n;++j)v->input[j]=get32(bcc_host(p)+4*j);
}
/* The two definitions the bullet_def stub returns: +2 divisor (never 0),
 * +8 sound word, +0Ah spread word, +0Ch division flag. */
static uint32_t bcc_def(unsigned k){return BCC_BASE+BCC_D+0x10u*k;}
static uint16_t bcc_spread(uint32_t pick){
    static const uint16_t words[]={0,1,0x7f,0x80,0x81,0xff,0x100,0x3fff,0x4000,0x7fff,0x8000,0xffff};
    return pick%3?words[(pick>>2)%12]:(uint16_t)(pick>>8);
}
static uint16_t bcc_divisor(uint32_t pick){
    static const uint16_t words[]={1,0xffff,2,3,0xfffd,0x400,0x401,0x7fff,0x8000,0xfc00};
    uint16_t w=pick%3?words[(pick>>2)%10]:(uint16_t)(pick>>8);return w?w:7;
}
__attribute__((used,noinline)) static uint64_t __cdecl bcc_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(bcc_calls==BCC_MAX){fail("bullet_ctor call overflow");ExitProcess(1);}
    bcc_record *v=bcc_log+bcc_calls++;memset(v,0,sizeof(*v));v->id=id;
    if(bcc_track){for(unsigned j=0;j<3;++j)v->saved[j]=bcc_mask(bcc_entry[j]);v->saved[3]=bcc_entry[3]-bcc_origin;v->saved[4]=bcc_entry[4]-bcc_origin;}
    for(unsigned k=0;k<bcc_counts[id];++k)v->args[k]=bcc_mask(a[k]);
    for(unsigned j=0;j<3;++j)v->regs[j]=bcc_mask(regs[j]);
    uint32_t eax=bcc_next(),edx=bcc_next(),pick=bcc_next();bcc_ecx=bcc_next();
    /* Perturbations of what the originals reread (direct and installed
     * cases only: the real passes keep the game's order). */
    const int perturb=!bcc_real;
    switch(id){
    case 0: /* 49630c record stub returns the real FS:0 head. */
        __asm__ volatile("movl %%fs:0,%0":"=r"(eax));break;
    case 1: eax=BCC_OBJ(pick%4); break; /* pool_alloc: never null */
    case 2: /* bullet_init: the definition pointer and the counter pointer. */
        if(perturb&&pick%3==0&&bcc_in(a[0],0x64))put32(bcc_host(a[0])+0x4e,BCC_RECORD((pick>>4)%BCC_RECS));
        if(perturb&&pick%5==1)put32(mapped+0x2cbee4,BCC_BASE+(pick>>8&1?BCC_COUNTER2:BCC_COUNTER));
        break;
    case 3: eax=bcc_def(pick%2); break; /* bullet_def */
    case 4: /* rng_next: the spread words may change around the draw. */
        if(pick%4==0)eax&=0xffff0000u;else if(pick%4==1)eax|=0xffffu;
        if(perturb&&pick%5==0)for(unsigned k=0;k<2;++k)put16(bcc_host(bcc_def(k))+0xa,bcc_spread(bcc_next()));
        break;
    case 5: /* memset into the arena. */
        if(bcc_in(a[0],a[2]&0xfffu)&&a[2]<=0x100u)memset(bcc_host(a[0]),(int)a[1],a[2]);
        eax=a[0];
        break;
    case 6: bcc_dwords(v,a[1],3); break; /* the sound: position by content */
    case 7: case 8: /* object_init, def_lookup: the counter pointer may move. */
        if(perturb&&pick%5==0)put32(mapped+0x2cbee4,BCC_BASE+(pick>>8&1?BCC_COUNTER2:BCC_COUNTER));
        break;
    case 9: { /* +38h: AX; the definition pointer, the flag, the divisor and
               * the sound word may change (read after). */
        static const uint32_t values[]={0,1,0xffff,0x8000,0x7fff,0xffff0000u};
        if(pick%3)eax=values[(pick>>2)%6];
        if(perturb&&pick%5==0)for(unsigned k=0;k<4;++k)put32(bcc_host(BCC_OBJ(k))+0x4e,BCC_RECORD((pick>>8)%BCC_RECS));
        if(perturb&&pick%7==1)for(unsigned k=0;k<2;++k){BYTE *d=bcc_host(bcc_def(k));put16(d+0xc,(uint16_t)(get16(d+0xc)?0:1+bcc_next()%0xffff));put16(d+2,bcc_divisor(bcc_next()));put16(d+8,(uint16_t)bcc_next());}
        break; }
    default:break;
    }
    v->eax=bcc_mask(eax);v->edx=edx;return (uint64_t)edx<<32|eax;
}
/* Every stub keeps EBX/ESI/EDI/EBP/ESP, then pushes the entry EAX/EDX/ECX. */
#define BCC_STUB(n) __attribute__((naked)) static void bcc_stub_##n(void){__asm__ volatile( \
    "movl %ebx,_bcc_entry\n\tmovl %esi,_bcc_entry+4\n\tmovl %edi,_bcc_entry+8\n\tmovl %ebp,_bcc_entry+12\n\tmovl %esp,_bcc_entry+16\n\t" \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _bcc_stub_impl\n\taddl $24,%esp\n\tmovl _bcc_ecx,%ecx\n\tret\n\t");}
BCC_STUB(1) BCC_STUB(2) BCC_STUB(3) BCC_STUB(4) BCC_STUB(5) BCC_STUB(6) BCC_STUB(7) BCC_STUB(8) BCC_STUB(9)
#undef BCC_STUB
/* 49630c: captures like the other stubs, then links a record at EBP +
 * [descriptor+4] into FS:0 with the saved head at record+0 and a zero
 * context word at record+16, as the original does. */
__attribute__((naked)) static void bcc_stub_0(void){__asm__ volatile(
    "movl %ebx,_bcc_entry\n\tmovl %esi,_bcc_entry+4\n\tmovl %edi,_bcc_entry+8\n\tmovl %ebp,_bcc_entry+12\n\tmovl %esp,_bcc_entry+16\n\t"
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
    "pushl $0\n\tcalll _bcc_stub_impl\n\taddl $12,%esp\n\t"
    "popl %ecx\n\tmovl 4(%ecx),%ecx\n\taddl %ebp,%ecx\n\tmovl %eax,(%ecx)\n\tmovw $0,16(%ecx)\n\tmovl %ecx,%fs:0\n\t"
    "popl %edx\n\taddl $4,%esp\n\tmovl _bcc_ecx,%ecx\n\tret\n\t");}
static void *bcc_stubs[BCC_IDS]={bcc_stub_0,bcc_stub_1,bcc_stub_2,bcc_stub_3,bcc_stub_4,bcc_stub_5,bcc_stub_6,bcc_stub_7,
    bcc_stub_8,bcc_stub_9};
#define BCC_BIT(i) (1u<<(i))
/* Direct callees of each hook (bit i = bcc_addresses[i]). */
static uint32_t bcc_which(unsigned index){
    if(index==ES2_HOOK_INDEX_bullet_create)return 0x7fu;
    if(index==ES2_HOOK_INDEX_bullet_init)return BCC_BIT(3)|BCC_BIT(7)|BCC_BIT(8);
    return 0;
}
static int bcc_patch(BYTE saved[BCC_IDS][5],int active[BCC_IDS],uint32_t which,int on){
    for(unsigned i=0;i<BCC_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!bcc_addresses[i])continue;if(actor_patch(bcc_addresses[i]-0x400000u,bcc_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(bcc_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
/* Pool, definition table, shape table, lookup table and count, counter
 * pointer, RNG stream A. */
static const struct { uint32_t rva,size; } bcc_ranges[]={{0xa96c2,4},{0xa96f8,4},{0xa9700,4},{0xa98fc,4},{0xa9900,2},
    {0x2cbee4,4},{0xd2599,0x72}};
static void bcc_globals(BYTE *out,int restore){
    for(unsigned i=0;i<sizeof(bcc_ranges)/sizeof(bcc_ranges[0]);++i){
        if(restore)memcpy(mapped+bcc_ranges[i].rva,out,bcc_ranges[i].size);else memcpy(out,mapped+bcc_ranges[i].rva,bcc_ranges[i].size);
        out+=bcc_ranges[i].size;
    }
}
static int bcc_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<BCC_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
static void bcc_reset(void){bcc_calls=0;bcc_stream=bcc_seed;}
/* Pool header (free count +0, used count +2, free head +0Ah, used head
 * +0Eh) with count free nodes of 80h bytes; objects start at node+8. */
static void bcc_pool(unsigned count){
    uint32_t pool=BCC_BASE+BCC_POOL,nodes=BCC_BASE+BCC_NODES;
    put16(bcc_host(pool),(uint16_t)count);put16(bcc_host(pool)+2,0);put32(bcc_host(pool)+0xe,0);
    for(unsigned i=0;i<count;++i){uint32_t n=nodes+0x80u*i;put32(bcc_host(n),i+1<count?n+0x80u:0);put32(bcc_host(n)+4,i?n-0x80u:0);}
    put32(bcc_host(pool)+0xa,count?nodes:0);
}
/* A small signed index for the definition and shape tables. */
static uint16_t bcc_index(void){return (uint16_t)((int32_t)(random32()%17u)-4);}
/* The arena: the owner with its vtable, position and angles, the bullet
 * definition table [4a96f8h] (records of 14 bytes, word +0 the shape index
 * in -4..12), the shape table [4a9700h], the lookup records [4a98fch]
 * (stride 24h), the counters, the pool and four node objects whose +4Eh
 * points to a lookup record, the two bullet_def stub definitions. real:
 * every shape is 0 (object_init allocates nothing), the record (2, type)
 * exists, a real pool, a seeded RNG. */
static void bcc_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    const uint32_t b=BCC_BASE;
    bcc_seed=random32();random_bytes(bcc_arena,BCC_SIZE);memset(a,0,24);bcc_real=real;
    for(unsigned i=0;i<3;++i)r[i]=random32();
    put32(bcc_arena+BCC_OWNER,b+BCC_VT);put32(bcc_arena+BCC_VT+0x38,(uint32_t)(uintptr_t)bcc_stubs[9]);
    for(int k=-4;k<=12;++k){BYTE *d=bcc_arena+BCC_DEFS+14*k;put16(d,real?0:bcc_index());
        if(real){put16(d+2,bcc_divisor(random32()));put16(d+0xa,bcc_spread(random32()));put16(d+0xc,(uint16_t)(random32()%2?0:random32()));}}
    for(int k=-4;k<=12;++k)put32(bcc_arena+BCC_SHAPES+4*k,real?0:random32());
    for(unsigned k=0;k<2;++k){BYTE *d=bcc_host(bcc_def(k));put16(d+2,bcc_divisor(random32()));put16(d+0xa,bcc_spread(random32()));
        put16(d+0xc,(uint16_t)(random32()%2?0:random32()%3?1:random32()));if(random32()%3==0)put16(d+8,(uint16_t)(random32()%40));}
    for(unsigned k=0;k<4;++k)put32(bcc_host(BCC_OBJ(k))+0x4e,BCC_RECORD(random32()%BCC_RECS));
    /* Lookup records: categories and types from small sets so that hits
     * happen; a count from the boundary set. */
    static const uint16_t keys[]={0,2,3,4,1,0xffff};
    for(unsigned k=0;k<BCC_RECS;++k){BYTE *e=bcc_host(BCC_RECORD(k));put16(e,keys[random32()%6]);put16(e+2,(uint16_t)(random32()%8));}
    static const uint16_t counts[]={0,1,2,3,5,16,0xffff,0x8000,0x7fff};
    uint16_t count=n<1024?counts[n%9]:random32()%4?(uint16_t)(random32()%17):counts[random32()%9];
    put32(mapped+0xa96c2,b+BCC_POOL);put32(mapped+0xa96f8,b+BCC_DEFS);put32(mapped+0xa9700,b+BCC_SHAPES);
    put32(mapped+0xa98fc,b+BCC_REC);put32(mapped+0x2cbee4,b+BCC_COUNTER);
    if(index==ES2_HOOK_INDEX_bullet_create){
        static const uint32_t types[]={2,3,0,1,0x10002,0xffff0003u,0xffff};
        a[0]=real?(random32()&0xffff0000u)|random32()%4u:random32()%4?types[random32()%7]:random32();
        a[1]=b+BCC_POS;a[2]=b+BCC_ANG;a[3]=b+BCC_OWNER;
        if(real){for(unsigned j=0;j<3;++j)put32(bcc_arena+BCC_POS+4*j,(uint32_t)((int32_t)(random32()%40001u)-20000));
            BYTE *state=mapped+0xd2599;random_bytes(state,112);state[112]=(BYTE)(random32()%56);state[113]=(BYTE)(random32()%56);
            bcc_pool(4);}
    } else if(index==ES2_HOOK_INDEX_bullet_init){
        a[0]=BCC_OBJ(random32()%4);a[1]=(real?random32()%4u:bcc_index())|(random32()%2?random32()&0xffff0000u:0);
    } else {
        a[0]=keys[random32()%6]|(random32()%2?random32()&0xffff0000u:0);a[1]=random32()%8|(random32()%2?random32()&0xffff0000u:0);
        if(random32()%2)a[1]=random32();
        if(count==0x7fff){unsigned k=random32()%BCC_RECS;put16(bcc_host(BCC_RECORD(k)),(uint16_t)a[0]);put16(bcc_host(BCC_RECORD(k))+2,(uint16_t)a[1]);}
        else if((int16_t)count>BCC_RECS)count=BCC_RECS;
    }
    if(real){
        /* bullet_init's lookup (2, type) must exist within the count. */
        uint32_t type=index==ES2_HOOK_INDEX_bullet_init?a[1]:a[0];count=(uint16_t)(1+random32()%BCC_RECS);
        unsigned k=random32()%count;put16(bcc_host(BCC_RECORD(k)),2);put16(bcc_host(BCC_RECORD(k))+2,(uint16_t)type);
        if(index==ES2_HOOK_INDEX_bullet_init)a[0]=BCC_OBJ(random32()%4);
    }
    put16(mapped+0xa9900,index==ES2_HOOK_INDEX_def_lookup||real?count:(uint16_t)BCC_RECS);
}
static int bcc_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const bcc_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,bcc_calls);
    for(unsigned j=0;j<calls&&j<bcc_calls&&expected;++j)if(memcmp(expected+j,bcc_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(bcc_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<BCC_SIZE;++j)if(bcc_after[j]!=bcc_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,bcc_after[j],bcc_arena[j]);break;}
    return 1;
}
static unsigned bcc_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<bcc_calls;++j){unsigned id=bcc_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?bcc_slots[id]==c->key:!bcc_slots[id]&&bcc_addresses[id]==c->key)++hits;}
    return hits;
}
static int bcc_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[BCC_GLOBALS],after[BCC_GLOBALS],actual[BCC_GLOBALS];static bcc_record expected[BCC_MAX];uint32_t head,head_after;
    memcpy(bcc_before,bcc_arena,BCC_SIZE);bcc_globals(before,0);
    __asm__ volatile("movl %%fs:0,%0":"=r"(head));
    bcc_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=bcc_calls;memcpy(expected,bcc_log,calls*sizeof(*expected));
    memcpy(bcc_after,bcc_arena,BCC_SIZE);bcc_globals(after,0);
    memcpy(bcc_arena,bcc_before,BCC_SIZE);bcc_globals(before,1);bcc_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];
    if(count>16)return fail("bullet_ctor counter table too long");
    for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);bcc_globals(actual,0);
    __asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
    if(head_after!=head)return fail("bullet_ctor left FS:0 changed");
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=bcc_expected_hits(c+i))return fail("bullet_ctor callee counter delta");
    if(pair[0]!=pair[1]||calls!=bcc_calls||memcmp(expected,bcc_log,calls*sizeof(*expected))||memcmp(bcc_after,bcc_arena,BCC_SIZE)||
       bcc_globals_differ(after,actual))
        return bcc_report("direct",n,pair,calls,expected);
    bcc_globals(before,1);
    return 0;
}
/* Calls target with six slots, EAX/EDX/ECX/EBX = pattern, ESI and EDI two
 * other values derived from it and a patterned stack below the slots;
 * returns EAX, EDX and ECX in out and copies the BCC_WINDOW bytes below the
 * slots after the call. */
__attribute__((naked,noinline)) static void bcc_invoke(void *target,const uint32_t *a,uint32_t pattern,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 16(%ebp),%eax\n\tmovl $1024,%ecx\n\trep stosl\n\taddl $4072,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 20(%esi)\n\tpushl 16(%esi)\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl %esp,_bcc_origin\n\t"
        "movl 16(%ebp),%eax\n\tmovl %eax,%edx\n\tmovl %eax,%ecx\n\tmovl %eax,%ebx\n\tmovl %eax,%esi\n\txorl $0x5a5a5a5a,%esi\n\t"
        "movl %eax,%edi\n\txorl $0x3c3c3c3c,%edi\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%ebx\n\tmovl %eax,(%ebx)\n\tmovl %edx,4(%ebx)\n\tmovl %ecx,8(%ebx)\n\t"
        "leal -512(%esp),%esi\n\tmovl $_bcc_frame_copy,%edi\n\tmovl $128,%ecx\n\tcld\n\trep movsl\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static int bcc_shimmed(unsigned index){return index==ES2_HOOK_INDEX_bullet_create;}
/* The compared part of the window: the frame up to the return address
 * (the real passes, where nested replaced hooks push their framework's
 * flags below the outgoing ESP) or 80h bytes (stubs only). */
static unsigned bcc_window(int real){return real?0x40u:0x80u;}
static int bcc_frame_report(const char *what,unsigned n,const BYTE *expected,unsigned window){
    fprintf(stderr,"FAIL: %s case %u (%s)\n",current_function,n,what);
    for(unsigned j=BCC_WINDOW-window;j<BCC_WINDOW;++j)if(expected[j]!=bcc_frame_copy[j])fprintf(stderr," below the arguments -%u: %02x/%02x\n",BCC_WINDOW-j,expected[j],bcc_frame_copy[j]);
    return 1;
}
static int bcc_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;bcc_track=bcc_shimmed(index);
    const unsigned window=bcc_window(0);uint32_t head,head_after;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_out[3],actual_out[3],pattern=poison?0xa7a7a7a7u:0x31313131u;bcc_build(index,n,a,r,0);
        static BYTE expected_frame[BCC_WINDOW];
        BYTE before[BCC_GLOBALS],after[BCC_GLOBALS],actual[BCC_GLOBALS],expected_fpu[108],actual_fpu[108];static bcc_record expected[BCC_MAX];
        memcpy(bcc_before,bcc_arena,BCC_SIZE);bcc_globals(before,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head));
        bcc_reset();bcc_invoke(mapped+d->rva,a,pattern,expected_out);es2_hook_fpu_save(expected_fpu);memcpy(expected_frame,bcc_frame_copy,BCC_WINDOW);
        unsigned calls=bcc_calls;memcpy(expected,bcc_log,calls*sizeof(*expected));memcpy(bcc_after,bcc_arena,BCC_SIZE);bcc_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("bullet_ctor poison install");
        memcpy(bcc_arena,bcc_before,BCC_SIZE);bcc_globals(before,1);bcc_reset();bcc_invoke(mapped+d->rva,a,pattern,actual_out);
        es2_hook_fpu_save(actual_fpu);bcc_globals(actual,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
        uint64_t pair[2]={(uint64_t)expected_out[1]<<32|expected_out[0],(uint64_t)actual_out[1]<<32|actual_out[0]};
        if(head_after!=head||pair[0]!=pair[1]||calls!=bcc_calls||memcmp(expected,bcc_log,calls*sizeof(*expected))||memcmp(bcc_after,bcc_arena,BCC_SIZE)||
           bcc_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1||
           (bcc_track&&expected_out[2]!=actual_out[2]))
            status=bcc_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        else if(bcc_track&&memcmp(expected_frame+BCC_WINDOW-window,bcc_frame_copy+BCC_WINDOW-window,window))
            status=bcc_frame_report(fast?"installed fast frame":"installed preserving frame",n,expected_frame,window);
        if(!es2_hook_uninstall(&h))return fail("bullet_ctor poison uninstall");bcc_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));bcc_track=0;
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state/FS:0%s\n",d->name,total,
        bcc_shimmed(index)?", ECX, EBX/ESI/EDI/EBP/ESP and stack arguments at every callee entry and the bytes below the arguments":"");
    return status;
}
/* The real-callee pass: nested hooks in rotating original/replaced/compare
 * modes (compare falls back to replaced where refused), the hook under test
 * original or replaced; keep = the callees that stay logging stubs (the
 * memset thunk 49681a runs as the harness's runtime memset). */
enum { BCC_NESTED=8 };
static unsigned bcc_nested(unsigned index,unsigned nested[BCC_NESTED],uint32_t *keep){
    unsigned count=0;*keep=BCC_BIT(0);
#define BCC_N(name) nested[count++]=ES2_HOOK_INDEX_##name
    if(index==ES2_HOOK_INDEX_bullet_create){
        *keep|=BCC_BIT(6);
        BCC_N(pool_alloc);BCC_N(bullet_init);BCC_N(rng_next);
    }
    BCC_N(object_init);BCC_N(def_lookup);BCC_N(bullet_def);
#undef BCC_N
    return count;
}
static int bcc_real_pass(unsigned index){
    unsigned nested[BCC_NESTED];uint32_t keep;unsigned count=bcc_nested(index,nested,&keep);
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[BCC_NESTED]={0};
    BYTE saved[BCC_IDS][5];int active[BCC_IDS];uint32_t head,head_after;
    if(bcc_patch(saved,active,keep,1))return 1;
    static bcc_record expected[BCC_MAX];static BYTE expected_frame[BCC_WINDOW];
    const int shimmed=bcc_shimmed(index);bcc_track=shimmed;const unsigned window=bcc_window(1);
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_out[3],actual_out[3],pattern=n&1?0xa7a7a7a7u:0x31313131u;bcc_build(index,n,a,r,1);
        BYTE before[BCC_GLOBALS],after[BCC_GLOBALS],actual[BCC_GLOBALS];
        memcpy(bcc_before,bcc_arena,BCC_SIZE);bcc_globals(before,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head));
        es2_hook hk[BCC_NESTED+1];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("bullet_ctor nested install");}
        bcc_reset();bcc_invoke(mapped+d->rva,a,pattern,expected_out);memcpy(expected_frame,bcc_frame_copy,BCC_WINDOW);
        unsigned calls=bcc_calls;memcpy(expected,bcc_log,calls*sizeof(*expected));
        memcpy(bcc_after,bcc_arena,BCC_SIZE);bcc_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+BCC_NESTED,d,&o,function_hook_extension(index)))return fail("bullet_ctor caller install");
        memcpy(bcc_arena,bcc_before,BCC_SIZE);bcc_globals(before,1);bcc_reset();
        bcc_invoke(mapped+d->rva,a,pattern,actual_out);bcc_globals(actual,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
        uint64_t pair[2]={(uint64_t)expected_out[1]<<32|expected_out[0],(uint64_t)actual_out[1]<<32|actual_out[0]};
        if(head_after!=head||pair[0]!=pair[1]||calls!=bcc_calls||memcmp(expected,bcc_log,calls*sizeof(*expected))||memcmp(bcc_after,bcc_arena,BCC_SIZE)||
           bcc_globals_differ(after,actual)||(shimmed&&expected_out[2]!=actual_out[2])){
            fprintf(stderr,"real mode=%u head %08x/%08x ecx %08x/%08x\n",mode,head,head_after,expected_out[2],actual_out[2]);
            status=bcc_report("real",n,pair,calls,expected);
        }
        else if(shimmed&&memcmp(expected_frame+BCC_WINDOW-window,bcc_frame_copy+BCC_WINDOW-window,window)){
            fprintf(stderr,"real mode=%u\n",mode);status=bcc_frame_report("real frame",n,expected_frame,window);}
        for(unsigned j=0;j<=BCC_NESTED;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("bullet_ctor nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("bullet_ctor nested uninstall");}
        bcc_globals(before,1);++total;
    }
    if(bcc_patch(saved,active,0,0))return 1;
    bcc_real=0;bcc_track=0;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, 31/a7 stack, pair, memory, globals, FS:0, stub inputs%s\n",
        d->name,total,count,shimmed?", ECX, per-call EBX/ESI/EDI/EBP/ESP, stack arguments and the frame":"");
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
/* Compare cases (compare_probes.h) for def_lookup, the group's compare-capable
 * member; the state is the arena and the globals. */
static void bcc_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){bcc_build(index,n,a,r,0);}
static void bcc_compare_state(BYTE *s,int restore){
    if(restore){memcpy(bcc_arena,s,BCC_SIZE);bcc_globals(s+BCC_SIZE,1);}else{memcpy(s,bcc_arena,BCC_SIZE);bcc_globals(s+BCC_SIZE,0);}
}
static int bullet_ctor_cases(unsigned index,unsigned limit){
    if(limit&&compare_probe_installed(index))return compare_probe_run(index,limit,bcc_compare_build,bcc_compare_state,BCC_SIZE+BCC_GLOBALS);
    uint32_t which=bcc_which(index);
    BYTE saved[BCC_IDS][5],old_globals[BCC_GLOBALS];int active[BCC_IDS];
    bcc_globals(old_globals,0);
    if(bcc_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_BULLET_CTOR_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];bcc_build(index,n,a,r,0);status=bcc_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=bcc_installed_probes(index,64);
    if(bcc_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status&&index!=ES2_HOOK_INDEX_def_lookup)status=bcc_real_pass(index);
    bcc_globals(old_globals,1);
    if(!limit&&!status&&compare_probe_capable(index))status=compare_probe_run(index,0,bcc_compare_build,bcc_compare_state,BCC_SIZE+BCC_GLOBALS);
    bcc_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, buffers by content, arena/globals/FS:0\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
