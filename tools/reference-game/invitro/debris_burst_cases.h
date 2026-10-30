/* Debris burst: debris_table 4089bc, debris_burst 408530, debris_pick 4086bc,
 * debris_piece 408588, debris_init 408940, debris_spin 408b74, debris_spin_any
 * 408bb4 (docs/reverse-engineering/hit_tests.md, "debris burst"). Direct cases
 * run the original body and the core against logging stubs for every game
 * callee (ordered calls with their arguments and entry EAX/EDX/ECX, frame
 * payloads by content: the matrix handed to debris_pick, the position handed to
 * debris_init, the Euler angles, the rotation handed to 47f058 and the composed
 * rotation handed to matrix_angles; stack addresses masked as in base_cases.h);
 * the runtime frame setup 49630c is the record stub of base_cases.h (real FS:0
 * head at record+0, context word zeroed). The stubs change what the originals
 * reread (the table's bound, pieces pointer and second count, the piece's
 * orientation and speed words) and the count word that debris_pick reads only
 * once. The weight walk ends at a sentinel piece of weight 7FFFh (the original
 * has no end check). Then installed 31/a7 poison probes and a real-callee pass
 * with the nested hooks in rotating modes. */
typedef struct db_record { uint32_t id,args[5],regs[3],input[8],eax,edx; uint32_t count; } db_record;
enum { DB_SIZE=0x1000, DB_TABLE=0x100, DB_PIECES=0x200, DB_PIECE_COUNT=16, DB_SHAPES=0x340, DB_POS=0x3c0, DB_MATRIX=0x3e0,
    DB_OBJECT=0x400, DB_COUNTER=0x700, DB_POOL=0x740, DB_NODES=0x800, DB_VECTOR=0x9c0, DB_WORD=0x9e0, DB_LIST=0xa00,
    DB_MAX=96, DB_IDS=15, DB_GLOBALS=158 };
#define DB_BASE ((uint32_t)(uintptr_t)db_arena)
static BYTE db_arena[DB_SIZE],db_before[DB_SIZE],db_after[DB_SIZE];
static db_record db_log[DB_MAX];
static unsigned db_calls;
static int db_real;
static uint32_t db_seed,db_stream,db_piece_word;
__attribute__((used)) static uint32_t db_ecx;
/* 49630c (record), 49681a memset, 4086bc, 492f24, 408588, 471b30, 408940,
 * 47ebb8, 47f058, 47f9a0, 408b74, 408bb4, 402188, 4089e4, 492ee0. */
static const uint32_t db_addresses[DB_IDS]={0x49630c,0x49681a,0x4086bc,0x492f24,0x408588,0x471b30,0x408940,0x47ebb8,0x47f058,
    0x47f9a0,0x408b74,0x408bb4,0x402188,0x4089e4,0x492ee0};
static const unsigned db_counts[DB_IDS]={0,3,4,2,4,1,5,2,3,2,3,2,2,4,1};
static uint32_t db_next(void){
    uint32_t z=db_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *db_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int db_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static uint16_t db_pick(const uint16_t *values,unsigned count,uint32_t pick){return values[pick%count];}
static void db_content(db_record *v,uint32_t address,unsigned bytes){for(unsigned j=0;j<bytes/4;++j)v->input[j]=get32(db_host(address)+4*j);}
static void db_fill(uint32_t address,unsigned bytes){for(unsigned j=0;j<bytes;++j)db_host(address)[j]=(BYTE)db_next();}
/* The piece words the originals reread after a call (+8, +0Ah, +0Ch);
 * -1 half of the time for the two orientation words. After the angle
 * decision (late) a -1 angle word stays -1: the angles are computed only
 * when one of the two words is not -1, and the original's final store of
 * f-2Ch would read an unwritten local if a callee changed the table there
 * (the tables are static data in the game). */
static void db_touch_piece(uint32_t pick,int late){
    if(!db_piece_word||db_real)return;
    if(pick%5==0&&(!late||get16(db_host(db_piece_word+8))!=0xffff))put16(db_host(db_piece_word+8),(uint16_t)(pick>>8&1?0xffff:pick>>9));
    if(pick%7==0)put16(db_host(db_piece_word+10),(uint16_t)(pick>>12&1?0xffff:pick>>13));
    if(pick%11==0)put16(db_host(db_piece_word+12),(uint16_t)(pick>>16));
}
/* The table words: bound and pieces pointer (reread per pick), second
 * count (reread per piece), count (read once). */
static void db_touch_table(uint32_t pick){
    if(db_real)return;
    BYTE *t=db_arena+DB_TABLE;
    if(pick%5==0)put16(t+8,(uint16_t)(pick>>8));
    if(pick%7==0)put32(t+4,DB_BASE+DB_PIECES+14u*(pick>>9&3u));
    if(pick%9==0)put16(t+2,(uint16_t)(pick>>12&3u));
    if(pick%11==0)put16(t,(uint16_t)(pick>>14&3u));
}
__attribute__((used,noinline)) static uint64_t __cdecl db_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(db_calls==DB_MAX){fail("debris synthetic call overflow");ExitProcess(1);}
    db_record *v=db_log+db_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,4*db_counts[id]);memcpy(v->regs,regs,12);
    v->count=weapons_bridge_slots(a,db_counts[id]);
    for(unsigned j=0;j<3;++j)if(db_stack(v->regs[j]))v->regs[j]=0;
    for(unsigned j=0;j<db_counts[id];++j)if(db_stack(v->args[j]))v->args[j]=0;
    uint32_t eax=db_next(),edx=db_next(),pick=db_next();db_ecx=db_next();
    switch(id){
    case 0: /* 49630c record stub returns the real FS:0 head for db_stub_0. */
        __asm__ volatile("movl %%fs:0,%0":"=r"(eax));break;
    case 1: /* memset: done, so the frame is the same in both runs. */
        if(a[2]<=0x40)memset(db_host(a[0]),(int)a[1],a[2]);
        break;
    case 2: /* debris_pick: the matrix by content. */
        db_content(v,a[2],32);break;
    case 3: /* rng_bounded: small draws that walk the weights, or full. */
        if(pick%3)eax=pick>>4&0x3fu;
        db_touch_table(pick);break;
    case 4: /* debris_piece */
        db_touch_table(pick);break;
    case 5: /* pool_alloc: the object or 0. */
        eax=pick%3?DB_BASE+DB_OBJECT:0;break;
    case 6: /* debris_init: the position by content. */
        db_content(v,a[2],12);db_touch_piece(pick,0);break;
    case 7: /* euler_matrix: angles by content; the 20-byte rotation out. */
        v->input[0]=get32(db_host(a[0]));v->input[1]=get16(db_host(a[0])+4);db_fill(a[1],20);break;
    case 8: /* 47f058: the rotation by content; the composed rotation out. */
        db_content(v,a[0],20);db_fill(a[2],20);break;
    case 9: /* matrix_angles: the composed rotation by content; angles out. */
        db_content(v,a[0],20);db_fill(a[1],6);db_touch_piece(pick,1);break;
    case 10: case 11: /* debris_spin, debris_spin_any */
        db_touch_piece(pick,1);break;
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
/* Pushes the entry EAX/EDX/ECX for the record before the slot pointer. */
#define DB_STUB(n) __attribute__((naked)) static void db_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _db_stub_impl\n\taddl $24,%esp\n\tmovl _db_ecx,%ecx\n\tret\n\t");}
DB_STUB(1) DB_STUB(2) DB_STUB(3) DB_STUB(4) DB_STUB(5) DB_STUB(6) DB_STUB(7) DB_STUB(8) DB_STUB(9) DB_STUB(10) DB_STUB(11)
DB_STUB(12) DB_STUB(13) DB_STUB(14)
#undef DB_STUB
/* 49630c: captures the entry registers like the other stubs, then links a
 * record at EBP + [descriptor+4] into FS:0 with the saved head at record+0
 * and a zero context word at record+16, as the original does. */
__attribute__((naked)) static void db_stub_0(void){__asm__ volatile(
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
    "pushl $0\n\tcalll _db_stub_impl\n\taddl $12,%esp\n\t"
    "popl %ecx\n\tmovl 4(%ecx),%ecx\n\taddl %ebp,%ecx\n\tmovl %eax,(%ecx)\n\tmovw $0,16(%ecx)\n\tmovl %ecx,%fs:0\n\t"
    "popl %edx\n\taddl $4,%esp\n\tmovl _db_ecx,%ecx\n\tret\n\t");}
static void *db_stubs[DB_IDS]={db_stub_0,db_stub_1,db_stub_2,db_stub_3,db_stub_4,db_stub_5,db_stub_6,db_stub_7,db_stub_8,db_stub_9,
    db_stub_10,db_stub_11,db_stub_12,db_stub_13,db_stub_14};
/* Direct callees of each hook (bit i = db_addresses[i]). */
static unsigned db_which(unsigned index){
    if(index==ES2_HOOK_INDEX_debris_burst)return 1u<<1|1u<<2;
    if(index==ES2_HOOK_INDEX_debris_pick)return 1u<<3|1u<<4;
    if(index==ES2_HOOK_INDEX_debris_piece)return 1u|1u<<5|1u<<6|1u<<7|1u<<8|1u<<9|1u<<10|1u<<11;
    if(index==ES2_HOOK_INDEX_debris_init)return 1u<<12|1u<<3;
    if(index==ES2_HOOK_INDEX_debris_spin)return 1u<<3|1u<<13;
    if(index==ES2_HOOK_INDEX_debris_spin_any)return 1u<<14|1u<<10;
    return 0;
}
/* Counter pointer, the debris pool and list globals 4a9642..4a9669 (pool,
 * second list, default list record with its count word 4a9654, the
 * heading vector pointer 4a9660, the speed range 4a9664/4a9666, the
 * heading scale 4a9668) and RNG stream A. */
static const struct { uint32_t rva,size; } db_ranges[]={{0x2cbee4,4},{0xa9642,0x28},{0xd2599,0x72}};
static void db_globals(BYTE *out,int restore){
    for(unsigned i=0;i<sizeof(db_ranges)/sizeof(db_ranges[0]);++i){
        if(restore)memcpy(mapped+db_ranges[i].rva,out,db_ranges[i].size);else memcpy(out,mapped+db_ranges[i].rva,db_ranges[i].size);
        out+=db_ranges[i].size;
    }
}
static int db_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<DB_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
static void db_reset(void){db_calls=0;db_stream=db_seed;}
/* Pool header (free count +0, used count +2, free head +0Ah, used head
 * +0Eh) with count free nodes of 80h bytes; objects start at node+8. */
static void db_pool(uint32_t pool,uint32_t nodes,unsigned count){
    put16(db_host(pool),(uint16_t)count);put16(db_host(pool)+2,0);put32(db_host(pool)+0xe,0);
    for(unsigned i=0;i<count;++i){uint32_t n=nodes+0x80u*i;put32(db_host(n),i+1<count?n+0x80u:0);put32(db_host(n)+4,i?n-0x80u:0);}
    put32(db_host(pool)+0xa,count?nodes:0);
}
/* Pieces: shape index -8..15 (inside the shape table window), weights
 * mostly small, orientation words -1 half of the time; the last piece is
 * the sentinel of weight 7FFFh. Real cases keep the speed nonzero (it is
 * debris_heading's divisor). */
static void db_pieces(int real){
    static const uint16_t weights[]={0,1,2,3,4,5,7,8,15,0xffff,0xfffe,0x8000,0x7fff};
    for(unsigned i=0;i<DB_PIECE_COUNT;++i){
        BYTE *p=db_arena+DB_PIECES+14u*i;
        put16(p,(uint16_t)(random32()%24u-8u));
        put16(p+2,i+1u==DB_PIECE_COUNT?0x7fff:random32()%4?(uint16_t)(random32()%12):db_pick(weights,13,random32()));
        put16(p+4,(uint16_t)random32());put16(p+6,(uint16_t)random32());
        put16(p+8,random32()%2?0xffff:(uint16_t)random32());put16(p+10,random32()%2?0xffff:(uint16_t)random32());
        uint16_t speed=(uint16_t)(random32()%4?random32()%4000:random32());if(real&&!speed)speed=1;put16(p+12,speed);
    }
}
static void db_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    uint32_t b=DB_BASE;
    db_seed=random32();random_bytes(db_arena,DB_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    db_pieces(real);
    BYTE *t=db_arena+DB_TABLE;
    static const uint16_t counts[]={0,1,2,3,4,0xffff,0x8000,5},counts2[]={0,1,2,3,4,0xffff,0x8000,5};
    if(real){
        /* Non-degenerate tables: a count of 1..3, or 0 with 1..3 pieces. */
        uint16_t count=(uint16_t)(random32()%4);put16(t,count);put16(t+2,(uint16_t)(count?random32():1+random32()%3));
        uint16_t bound=(uint16_t)(random32()%2?1+random32()%40:random32());if(!bound)bound=7;put16(t+8,bound);
    } else {
        put16(t,n<1024?counts[n%8]:db_pick(counts,8,random32()));put16(t+2,n<1024?counts2[n/8%8]:db_pick(counts2,8,random32()));
        put16(t+8,(uint16_t)(random32()%2?1+random32()%40:random32()));
    }
    put32(t+4,b+DB_PIECES+(random32()%4?0:14u*(random32()%4)));
    for(unsigned j=0;j<3;++j)put32(db_arena+DB_POS+4*j,random32()%2?random32():random32()%65536u-32768u);
    /* Matrix: mode byte 0 (identity, as 408530 builds it), 1, 2 or other. */
    static const BYTE modes[]={0,1,2,0x80,0xff,3};db_arena[DB_MATRIX+18]=modes[random32()%6];
    if(real)for(unsigned j=0;j<9;++j)put16(db_arena+DB_MATRIX+2*j,(uint16_t)(random32()%32769u-16384u));
    /* Globals: counter, pool, lists, heading vector, speed range, scale. */
    put32(mapped+0x2cbee4,b+DB_COUNTER);put32(mapped+0xa9642,b+DB_POOL);
    random_bytes(mapped+0xa9646,0x24);
    put32(mapped+0xa964c,b+DB_LIST);
    static const uint16_t list_counts[]={0,1,2,3,4,0xffff,0x8000,0x7fff};
    put16(mapped+0xa9654,n<1024?list_counts[n/64%8]:db_pick(list_counts,8,random32()));
    uint32_t k=random32()%3;put32(mapped+0xa9660,k?b+DB_VECTOR+(k-1u)*4u:0);
    if(real){
        uint16_t low=(uint16_t)(random32()%2?3000:random32()),span=(uint16_t)(random32()%2?5000:random32());if(!span)span=1;
        put16(mapped+0xa9664,low);put16(mapped+0xa9666,(uint16_t)(low+span));
        BYTE *state=mapped+0xd2599;random_bytes(state,112);state[112]=(BYTE)(random32()%56);state[113]=(BYTE)(random32()%56);
        db_pool(b+DB_POOL,b+DB_NODES,random32()%4);
    }
    db_piece_word=0;
    if(index==ES2_HOOK_INDEX_debris_table){
        static const uint16_t words[]={0,1,2,3,4,5,0xffff,0x8000,0x7fff,0xfffe};
        put16(db_arena+DB_WORD,n<1024?words[n%10]:db_pick(words,10,random32()));a[0]=b+DB_WORD;
    } else if(index==ES2_HOOK_INDEX_debris_burst){a[0]=b+DB_TABLE;a[1]=b+DB_SHAPES;a[2]=b+DB_POS;a[3]=random32();}
    else if(index==ES2_HOOK_INDEX_debris_pick){a[0]=b+DB_TABLE;a[1]=b+DB_SHAPES;a[2]=b+DB_MATRIX;a[3]=random32();}
    else if(index==ES2_HOOK_INDEX_debris_piece){
        a[0]=b+DB_SHAPES;a[1]=b+DB_PIECES+14u*(random32()%(DB_PIECE_COUNT-1u));a[2]=b+DB_MATRIX;a[3]=random32();db_piece_word=a[1];
        static const uint16_t words[]={0xffff,0,0x4000};
        if(n<1024){put16(db_host(a[1])+8,words[n%3]==0?(uint16_t)random32():0xffff);put16(db_host(a[1])+10,n/3%2?0xffff:(uint16_t)random32());}
    } else if(index==ES2_HOOK_INDEX_debris_init){
        a[0]=b+DB_OBJECT;a[1]=random32();a[2]=b+DB_POS;a[3]=random32();a[4]=random32();
    } else {
        static const uint32_t speeds[]={1,0xffffffffu,1000,0x7fffffffu,0x80000000u,3};
        a[0]=b+DB_OBJECT;a[1]=real?speeds[random32()%6]:random32();a[2]=random32();
    }
}
static int db_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const db_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,db_calls);
    for(unsigned j=0;j<calls&&j<db_calls&&expected;++j)if(memcmp(expected+j,db_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(db_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<DB_SIZE;++j)if(db_after[j]!=db_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,db_after[j],db_arena[j]);break;}
    return 1;
}
static unsigned db_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<db_calls;++j)if(db_addresses[db_log[j].id]==c->key)++hits;
    return hits;
}
static int db_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[DB_GLOBALS],after[DB_GLOBALS],actual[DB_GLOBALS];static db_record expected[DB_MAX];uint32_t head,head_after;
    memcpy(db_before,db_arena,DB_SIZE);db_globals(before,0);
    __asm__ volatile("movl %%fs:0,%0":"=r"(head));
    db_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=db_calls;memcpy(expected,db_log,calls*sizeof(*expected));
    memcpy(db_after,db_arena,DB_SIZE);db_globals(after,0);
    memcpy(db_arena,db_before,DB_SIZE);db_globals(before,1);db_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];for(unsigned i=0;i<count&&i<16;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);db_globals(actual,0);
    __asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
    if(head_after!=head)return fail("debris left FS:0 changed");
    if(n<4096)for(unsigned i=0;i<count&&i<16;++i)if(c[i].count-counts[i]!=db_expected_hits(c+i))return fail("debris callee counter delta");
    if(pair[0]!=pair[1]||calls!=db_calls||memcmp(expected,db_log,calls*sizeof(*expected))||memcmp(db_after,db_arena,DB_SIZE)||
       db_globals_differ(after,actual))
        return db_report("direct",n,pair,calls,expected);
    db_globals(before,1);
    return 0;
}
static int db_patch(BYTE saved[DB_IDS][5],int active[DB_IDS],unsigned which,int on){
    for(unsigned i=0;i<DB_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(db_addresses[i]-0x400000u,db_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(db_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static int db_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;uint32_t head,head_after;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;db_build(index,n,a,r,0);
        BYTE before[DB_GLOBALS],after[DB_GLOBALS],actual[DB_GLOBALS],expected_fpu[108],actual_fpu[108];static db_record expected[DB_MAX];
        memcpy(db_before,db_arena,DB_SIZE);db_globals(before,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head));
        db_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=db_calls;memcpy(expected,db_log,calls*sizeof(*expected));memcpy(db_after,db_arena,DB_SIZE);db_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("debris poison install");
        memcpy(db_arena,db_before,DB_SIZE);db_globals(before,1);db_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);db_globals(actual,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(head_after!=head||pair[0]!=pair[1]||calls!=db_calls||memcmp(expected,db_log,calls*sizeof(*expected))||memcmp(db_after,db_arena,DB_SIZE)||
           db_globals_differ(after,actual)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=db_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("debris poison uninstall");db_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state/FS:0\n",d->name,total);
    return status;
}
/* Real callees with the nested hooks in rotating original/replaced/compare
 * modes (compare falls back to replaced where refused). The runtime frame
 * setup 49630c, memset and object_init stay stubs (object_init's heap
 * constructors are not reached); 47f058 and debris_heading's leaves run as
 * original code. Pair, arena, globals and FS:0 are compared against a run
 * with only the nested hooks. */
static unsigned db_nested(unsigned index,unsigned nested[12],unsigned *keep){
    unsigned count=0;*keep=1u|1u<<1|1u<<12;
    if(index==ES2_HOOK_INDEX_debris_burst)nested[count++]=ES2_HOOK_INDEX_debris_pick;
    if(index==ES2_HOOK_INDEX_debris_burst||index==ES2_HOOK_INDEX_debris_pick)nested[count++]=ES2_HOOK_INDEX_debris_piece;
    if(index==ES2_HOOK_INDEX_debris_burst||index==ES2_HOOK_INDEX_debris_pick||index==ES2_HOOK_INDEX_debris_piece){
        nested[count++]=ES2_HOOK_INDEX_pool_alloc;nested[count++]=ES2_HOOK_INDEX_debris_init;nested[count++]=ES2_HOOK_INDEX_euler_matrix;
        nested[count++]=ES2_HOOK_INDEX_matrix_angles;nested[count++]=ES2_HOOK_INDEX_debris_spin_any;}
    if(index==ES2_HOOK_INDEX_debris_init){nested[count++]=ES2_HOOK_INDEX_rng_bounded;return count;}
    if(index!=ES2_HOOK_INDEX_debris_spin)nested[count++]=ES2_HOOK_INDEX_debris_spin;
    if(index!=ES2_HOOK_INDEX_debris_spin)nested[count++]=ES2_HOOK_INDEX_rng_next;
    nested[count++]=ES2_HOOK_INDEX_rng_bounded;nested[count++]=ES2_HOOK_INDEX_debris_heading;
    return count;
}
static int db_real_pass(unsigned index){
    unsigned nested[12],keep,count=db_nested(index,nested,&keep);
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[12]={0};
    BYTE saved[DB_IDS][5];int active[DB_IDS];
    if(db_patch(saved,active,keep,1))return 1;
    db_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u,head,head_after;db_build(index,n,a,r,1);
        BYTE before[DB_GLOBALS],after[DB_GLOBALS],actual[DB_GLOBALS];
        memcpy(db_before,db_arena,DB_SIZE);db_globals(before,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head));
        es2_hook hk[13];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("debris nested install");}
        db_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        memcpy(db_after,db_arena,DB_SIZE);db_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+12,d,&o,function_hook_extension(index)))return fail("debris caller install");
        memcpy(db_arena,db_before,DB_SIZE);db_globals(before,1);db_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);db_globals(actual,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
        if(head_after!=head||expected_pair[0]!=actual_pair[0]||expected_pair[1]!=actual_pair[1]||memcmp(db_after,db_arena,DB_SIZE)||
           db_globals_differ(after,actual)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            fprintf(stderr,"real mode=%u head %08x/%08x\n",mode,head,head_after);db_calls=0;status=db_report("real",n,pair,0,NULL);
        }
        for(unsigned j=0;j<13;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("debris nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("debris nested uninstall");}
        db_globals(before,1);++total;
    }
    db_real=0;
    if(db_patch(saved,active,0,0))return 1;
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, 31/a7 stack, full pair, memory and FS:0\n",d->name,total,count);
    return status;
}
/* Compare cases (compare_probes.h) for debris_table, the group's
 * compare-capable member; the state is the arena and the globals. */
static void db_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){db_build(index,n,a,r,0);}
static void db_compare_state(BYTE *s,int restore){
    if(restore){memcpy(db_arena,s,DB_SIZE);db_globals(s+DB_SIZE,1);}else{memcpy(s,db_arena,DB_SIZE);db_globals(s+DB_SIZE,0);}
}
static int debris_burst_cases(unsigned index,unsigned limit){
    if(limit&&compare_probe_installed(index))return compare_probe_run(index,limit,db_compare_build,db_compare_state,DB_SIZE+DB_GLOBALS);
    unsigned which=db_which(index);
    BYTE saved[DB_IDS][5],old_globals[DB_GLOBALS];int active[DB_IDS];
    db_globals(old_globals,0);
    if(db_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_DEBRIS_BURST_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];db_build(index,n,a,r,0);status=db_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=db_installed_probes(index,64);
    if(db_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status&&index!=ES2_HOOK_INDEX_debris_table)status=db_real_pass(index);
    db_globals(old_globals,1);
    if(!limit&&!status&&compare_probe_capable(index))status=compare_probe_run(index,0,db_compare_build,db_compare_state,DB_SIZE+DB_GLOBALS);
    db_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, frame payloads by content, arena/globals/FS:0\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
