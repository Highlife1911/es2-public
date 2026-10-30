/* actor_virtuals group: base_aim_node 403548, base_offsets 40351c,
 * base_condition 4052b4, actor_attacked 411aec, base_part_position 406808,
 * mech_aim_node 417b98, mech_offsets 4155c4, mech_armour 4154d0,
 * mech_target_scale 415498, mech_engage_roll 41cabc and mech_link_position
 * 41dd2c (docs/reverse-engineering/vtables.md, "actor virtuals"). Direct cases
 * run the original body and the adapter against logging stubs for every game
 * callee (memset 49681a, 411a74, contact_share 412704, euler_matrix 47ebb8,
 * 4801d4, the shape's slot +20h in an arena vtable, multiply_q10 47e0b0,
 * rng_next 492ee0): ordered calls with arguments, entry EAX/EDX/ECX and a hash
 * of the arena and the node-record window at the call (so every write before or
 * after a call is ordered). The stubs return registers from a per-case stream
 * (planned draws for rng_next, planned node pointers for the slot) and, in some
 * cases, poke a word the original reads again after the call (the configuration
 * pointer, the words A+29Eh/T+29Eh, the shape's node base) or writes after it
 * (the matrix-valid word). Pointers alias each other and the configuration in
 * some cases. EDX:EAX, the arena, the image windows (node record 4a95cc, guard
 * 497424, actor list 4a9af8/4a9afe, RNG stream A) and the callee counter deltas
 * are compared; base_condition's IDIV faults are compared by exception code.
 * Then installed 31/a7 poison probes and a real-callee pass (contact_share with
 * dist_gate, euler_matrix, multiply_q10, rng_next nested in rotating modes;
 * 411a74 and 4801d4 as original code), the hook original or replaced. Prefix
 * avr_. */
typedef struct avr_record { uint32_t id,args[3],regs[3],hash; uint32_t count; } avr_record;
enum { AVR_SIZE=0x1000, AVR_A=0x000, AVR_T=0x340, AVR_C=0x680, AVR_C2=0x6c0, AVR_GA=0x700, AVR_GT=0x720, AVR_P=0x740, AVR_Q=0x820,
    AVR_S=0xa80, AVR_L=0xaa0, AVR_VT=0xab0, AVR_NODE=0xae0, AVR_OUT=0xb00, AVR_LIST=0xb40, AVR_FREE=0xb60, AVR_MAX=4, AVR_IDS=8,
    AVR_GLOBALS=40+1+8+0x72 };
#define AVR_BASE ((uint32_t)(uintptr_t)avr_arena)
static BYTE avr_arena[AVR_SIZE],avr_before[AVR_SIZE],avr_after[AVR_SIZE];
static avr_record avr_log[AVR_MAX];
static unsigned avr_calls,avr_poke_call,avr_draws,avr_faults;
static uint32_t avr_seed,avr_stream,avr_poke_address,avr_poke_value,avr_poke_size,avr_draw[2],avr_node_return;
__attribute__((used)) static uint32_t avr_ecx;
/* Stub ids: 0 memset 49681a, 1 411a74, 2 contact_share 412704, 3
 * euler_matrix 47ebb8, 4 4801d4, 5 the shape's slot +20h (arena vtable), 6
 * multiply_q10 47e0b0, 7 rng_next 492ee0. */
static const uint32_t avr_addresses[AVR_IDS]={0x49681a,0x411a74,0x412704,0x47ebb8,0x4801d4,0,0x47e0b0,0x492ee0};
static const unsigned avr_argc[AVR_IDS]={3,3,2,2,3,2,2,1};
/* Image windows: node record 4a95cc (32 bytes) with 4 around, the guard
 * byte 497424, the actor list pointer and count 4a9af8..4a9aff, RNG stream
 * A 4d2599 (72h bytes). */
static const uint32_t avr_window_rva[]={0xa95c8,0x97424,0xa9af8,0xd2599};
static const unsigned avr_window_size[]={40,1,8,0x72};
static uint32_t avr_next(void){
    uint32_t z=avr_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static uint32_t avr_hash(void){
    uint32_t h=0x811c9dc5u;
    for(unsigned i=0;i<AVR_SIZE;i+=4)h=(h^get32(avr_arena+i))*0x01000193u;
    for(unsigned i=0;i<40;++i)h=(h^mapped[0xa95c8+i])*0x01000193u;
    return h;
}
__attribute__((used,noinline)) static uint64_t __cdecl avr_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(avr_calls==AVR_MAX){fail("actor virtuals synthetic call overflow");ExitProcess(1);}
    avr_record *v=avr_log+avr_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,4*avr_argc[id]);memcpy(v->regs,regs,12);
    v->count=weapons_bridge_slots(a,avr_argc[id]);
    v->hash=avr_hash();
    uint32_t eax=avr_next(),edx=avr_next();avr_ecx=avr_next();
    if(id==0)for(uint32_t i=0;i<a[2]&&i<64;++i)((BYTE *)(uintptr_t)a[0])[i]=(BYTE)avr_next();
    else if(id==1)for(unsigned i=0;i<6;++i){((BYTE *)(uintptr_t)a[1])[i]=(BYTE)avr_next();((BYTE *)(uintptr_t)a[2])[i]=(BYTE)avr_next();}
    else if(id==5)eax=avr_node_return;
    else if(id==7)eax=(eax&0xffff0000u)|(avr_draws<2?avr_draw[avr_draws++]:0u);
    if(avr_poke_address&&avr_poke_call==id){
        if(avr_poke_size==2)put16((BYTE *)(uintptr_t)avr_poke_address,(uint16_t)avr_poke_value);
        else put32((BYTE *)(uintptr_t)avr_poke_address,avr_poke_value);
    }
    return (uint64_t)edx<<32|eax;
}
#define AVR_STUB(n) __attribute__((naked)) static void avr_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _avr_stub_impl\n\taddl $24,%esp\n\tmovl _avr_ecx,%ecx\n\tret\n\t");}
AVR_STUB(0)
AVR_STUB(1)
AVR_STUB(2)
AVR_STUB(3)
AVR_STUB(4)
AVR_STUB(5)
AVR_STUB(6)
AVR_STUB(7)
#undef AVR_STUB
static void (*const avr_stubs[AVR_IDS])(void)={avr_stub_0,avr_stub_1,avr_stub_2,avr_stub_3,avr_stub_4,avr_stub_5,avr_stub_6,avr_stub_7};
static void avr_globals(BYTE *out,int restore){
    unsigned at=0;
    for(unsigned i=0;i<4;++i){
        if(restore)memcpy(mapped+avr_window_rva[i],out+at,avr_window_size[i]);else memcpy(out+at,mapped+avr_window_rva[i],avr_window_size[i]);
        at+=avr_window_size[i];
    }
}
static void avr_reset(void){avr_calls=0;avr_draws=0;avr_stream=avr_seed;}
static uint16_t avr_word(const uint16_t *values,unsigned count){return random32()%4?values[random32()%count]:(uint16_t)random32();}
static uint32_t avr_in(uint32_t offset,uint32_t span){return AVR_BASE+offset+random32()%span;}
/* Patch every callee with a fixed address (keep[i] leaves one unpatched;
 * memset, an import thunk, is always patched). */
static int avr_patch(BYTE saved[AVR_IDS][5],int on,const int *keep){
    for(unsigned i=0;i<AVR_IDS;++i){
        if(!avr_addresses[i]||(keep&&keep[i]&&i))continue;
        if(actor_patch(avr_addresses[i]-0x400000u,on?(void *)avr_stubs[i]:NULL,saved[i],on?TRUE:FALSE))return 1;
    }
    return 0;
}
/* One state for hook index (case n). real: valid inputs for the real
 * callees and no pokes; safe: base_condition without a faulting IDIV. */
static void avr_build(unsigned index,unsigned n,int real,int safe,uint32_t a[6]){
    static const uint16_t parts[]={0,1,2,3,0x7fff,0x8000,0xffff,0xfffe};
    static const uint16_t angles[]={0,1,0x3fff,0x4000,0xbfff,0xc000,0x7fff,0x8000,0xffff};
    static const uint16_t node_words[]={0,1,0x7fff,0x8000,0xffff,0xfffe,3};
    const uint32_t b=AVR_BASE;
    avr_seed=random32();random_bytes(avr_arena,AVR_SIZE);
    BYTE *A=avr_arena+AVR_A,*T=avr_arena+AVR_T,*C=avr_arena+AVR_C;
    put32(A+0x1f2,b+AVR_C);put32(T+0x1f2,b+AVR_C);
    put32(A+0x45,b+AVR_GA);put32(T+0x45,b+AVR_GT);
    /* Node record window and guard: random, the guard 0, 1 or other. */
    random_bytes(mapped+0xa95c8,40);mapped[0x97424]=(BYTE)(random32()%4==0?0:random32()%2?1:random32());
    avr_poke_address=0;avr_poke_call=0;avr_poke_value=random32();avr_poke_size=random32()%2?2:4;
    avr_node_return=0;avr_draw[0]=avr_draw[1]=0;
    for(unsigned i=0;i<6;++i)a[i]=random32();
    a[0]=b+AVR_A;
    if(index==ES2_HOOK_INDEX_base_aim_node){
        if(!real&&n%4==1){avr_poke_call=0;avr_poke_address=random32()%2?b+AVR_A+0x1f2:b+AVR_C+0x2c;
            if(avr_poke_address==b+AVR_A+0x1f2){avr_poke_value=b+AVR_C2;avr_poke_size=4;}}
    } else if(index==ES2_HOOK_INDEX_base_offsets){
        a[1]=avr_in(AVR_OUT,0x20);a[2]=n%8==3?avr_in(AVR_C+0x20,0x14):avr_in(AVR_OUT,0x30);
        if(!real&&n%4==1){avr_poke_call=1;avr_poke_address=random32()%2?b+AVR_A+0x1f2:b+AVR_C+0x2c;
            if(avr_poke_address==b+AVR_A+0x1f2){avr_poke_value=b+AVR_C2;avr_poke_size=4;}}
    } else if(index==ES2_HOOK_INDEX_base_condition){
        int count=(int)(random32()%23)-2;
        if(safe&&count<1)count=1+(int)(random32()%20);
        put16(C+0x12,(uint16_t)(!safe&&n%32==7?0x8000u|random32():(uint32_t)count));
        put32(A+0x205,b+AVR_P);put32(C+0x14,b+AVR_Q);
        for(unsigned i=0;i<20;++i){
            put16(avr_arena+AVR_P+11*i,avr_word(parts,8));
            put16(avr_arena+AVR_Q+30*i,safe?(uint16_t)(1+random32()%1000):n%16==5?(uint16_t)(i%2?1:0xffff):avr_word(parts,8));
        }
        if(!safe&&n%32==9)put16(C+0x12,20);
    } else if(index==ES2_HOOK_INDEX_actor_attacked){
        a[1]=b+AVR_T;
        BYTE *ga=avr_arena+AVR_GA,*gt=avr_arena+AVR_GT;
        ga[0x12]=(BYTE)(random32()%3);gt[0x12]=random32()%2?ga[0x12]:(BYTE)(random32()%3);
        put32(ga+0x14,random32()%2?0:1);put32(gt+0x14,0);
        /* Actor list of the real contact_share: A, T and one more. */
        put32(avr_arena+AVR_LIST,b+AVR_A);put32(avr_arena+AVR_LIST+4,b+AVR_T);put32(avr_arena+AVR_LIST+8,b+AVR_A);
        put32(mapped+0xa9af8,b+AVR_LIST);put16(mapped+0xa9afe,(uint16_t)(random32()%4));
        put32(A+0x1b2,0);put32(T+0x1b2,0);put16(T+0x4b,(uint16_t)(random32()%8));
        for(unsigned i=0;i<3;++i){put32(A+0x26+4*i,random32()%60000u);put32(T+0x26+4*i,random32()%60000u);}
    } else if(index==ES2_HOOK_INDEX_base_part_position){
        const uint16_t part=avr_word(parts,8);a[1]=(random32()&0xffff0000u)|part;
        a[2]=avr_in(AVR_OUT,0x30);
        put16(A+0x32,(uint16_t)(random32()%3==0?0:random32()%2?1:random32()));
        /* The record Q + 30 * part + 10h lands in the arena (real 4801d4). */
        const uint32_t record=avr_in(AVR_Q,0x40);put32(C+0x14,record-30u*(uint32_t)(int32_t)(int16_t)part-0x10u);
        put32(avr_arena+AVR_C2+0x14,random32());
        if(real){avr_arena[AVR_A+0x24]=(BYTE)(random32()%3);}
        if(!real&&n%4==1){avr_poke_call=random32()%2?3:4;const unsigned k=random32()%3;
            avr_poke_address=k==0?b+AVR_A+0x1f2:k==1?b+AVR_A+0x32:b+AVR_C+0x14;
            if(k==0){avr_poke_value=b+AVR_C2;avr_poke_size=4;}}
    } else if(index==ES2_HOOK_INDEX_mech_aim_node){
        put32(A+0x34,b+AVR_S);put32(avr_arena+AVR_S+4,b+AVR_L);put32(avr_arena+AVR_L,b+AVR_VT);
        put32(avr_arena+AVR_VT+0x20,(uint32_t)(uintptr_t)avr_stub_5);
        const unsigned k=random32()%4;
        avr_node_return=k==0?0:k==1?b+AVR_NODE:avr_in(AVR_FREE,0x400);
        put16((BYTE *)(uintptr_t)(avr_node_return?avr_node_return:b+AVR_NODE)+4,avr_word(node_words,7));
        if(!real&&n%4==1){avr_poke_call=5;avr_poke_address=random32()%2?b+AVR_S+0x16:b+AVR_A+0x34;avr_poke_size=4;}
    } else if(index==ES2_HOOK_INDEX_mech_offsets){
        a[1]=n%8==1?avr_in(AVR_C+0x60,0x10):avr_in(AVR_OUT,0x20);
        a[2]=n%8==2?avr_in(AVR_C+0x60,0x10):n%8==3?a[1]+random32()%12u-6u:avr_in(AVR_OUT,0x30);
    } else if(index==ES2_HOOK_INDEX_mech_armour){
        a[1]=(random32()&(random32()%2?0xffff0000u:0u))|avr_word(angles,9);
    } else if(index==ES2_HOOK_INDEX_mech_target_scale){
        put16(avr_arena+AVR_C+0x50,(uint16_t)(random32()%2?0:random32()%2?1:random32()));
    } else if(index==ES2_HOOK_INDEX_mech_engage_roll){
        a[1]=b+AVR_T;
        put16(T+0x1a8,(uint16_t)(random32()%4==0?random32()|1u:0));
        avr_draw[0]=random32()&0xffffu;avr_draw[1]=random32()&0xffffu;
        const uint16_t wa=(uint16_t)random32();uint16_t d;
        const unsigned k=random32()%4;
        d=k==0?(uint16_t)random32():k==1?0x8000u:(uint16_t)(random32()%800u-400u+(random32()%2?0u:0u));
        const uint16_t wt=(uint16_t)((avr_draw[0]&0x3e8u)+wa-(avr_draw[1]&0x3e8u)-d);
        put16(A+0x29e,wa);put16(T+0x29e,wt);
        if(real){/* Valid stream indices for the real rng_next. */
            for(unsigned i=0;i<0x70;++i)mapped[0xd2599+i]=(BYTE)random32();
            mapped[0xd2599+0x70]=(BYTE)(random32()%0x38);mapped[0xd2599+0x71]=(BYTE)(random32()%0x38);}
        if(!real&&n%4==1){avr_poke_call=7;avr_poke_size=2;avr_poke_address=random32()%2?b+AVR_A+0x29e:b+AVR_T+0x29e;}
    } else {
        /* mech_link_position: no link, T, or a link whose position overlaps
         * A+31Eh..A+329h or the byte A+ADh. */
        const unsigned k=random32()%4;
        put32(A+0x1a4,k==0?0:k==1?b+AVR_T:k==2?b+AVR_A+0x31eu - 0x26u+random32()%25u-12u:b+AVR_A+0xad-0x26-random32()%12u);
    }
}
static int avr_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const avr_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,avr_calls);
    for(unsigned j=0;j<calls&&j<avr_calls&&expected;++j)if(memcmp(expected+j,avr_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(avr_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u word%u %08x/%08x\n",j,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<AVR_SIZE;++j)if(avr_after[j]!=avr_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,avr_after[j],avr_arena[j]);break;}
    return 1;
}
/* base_condition's IDIV faults: a vectored handler in front of the
 * harness's own records the exception code and leaves through longjmp. */
static jmp_buf avr_divide_return;
static volatile int avr_expect_divide;
static volatile DWORD avr_divide_code;
static LONG CALLBACK avr_divide_handler(EXCEPTION_POINTERS *x){
    const DWORD code=x->ExceptionRecord->ExceptionCode;
    if(avr_expect_divide&&(code==EXCEPTION_INT_DIVIDE_BY_ZERO||code==EXCEPTION_INT_OVERFLOW)){
        avr_expect_divide=0;avr_divide_code=code;longjmp(avr_divide_return,1);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
/* Runs the original (replacement=0) or the adapter; returns the fault code
 * (0 without a fault) and the pair. */
static DWORD avr_run(unsigned index,int replacement,const uint32_t *a,const uint32_t *r,uint64_t *pair){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    if(index!=ES2_HOOK_INDEX_base_condition){*pair=replacement?x->replacement_context(a,d->user,r):weapons_original(index,a,r);return 0;}
    avr_divide_code=0;
    if(!setjmp(avr_divide_return)){
        avr_expect_divide=1;*pair=replacement?x->replacement_context(a,d->user,r):weapons_original(index,a,r);avr_expect_divide=0;return 0;
    }
    *pair=0;return avr_divide_code;
}
static int avr_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    BYTE before[AVR_GLOBALS],after[AVR_GLOBALS],actual[AVR_GLOBALS];static avr_record expected[AVR_MAX];
    memcpy(avr_before,avr_arena,AVR_SIZE);avr_globals(before,0);
    avr_reset();uint64_t pair[2];DWORD code[2];code[0]=avr_run(index,0,a,r,pair);unsigned calls=avr_calls;memcpy(expected,avr_log,calls*sizeof(*expected));
    memcpy(avr_after,avr_arena,AVR_SIZE);avr_globals(after,0);
    memcpy(avr_arena,avr_before,AVR_SIZE);avr_globals(before,1);avr_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[2]={0};
    for(unsigned i=0;i<count&&i<2;++i)counts[i]=c[i].count;
    code[1]=avr_run(index,1,a,r,pair+1);avr_globals(actual,0);
    if(code[0]!=code[1]){fprintf(stderr,"FAIL: %s case %u fault %08lx/%08lx\n",current_function,n,(unsigned long)code[0],(unsigned long)code[1]);return 1;}
    avr_faults+=code[0]!=0;
    for(unsigned i=0;i<count&&i<2;++i){
        uint64_t want=0;
        for(unsigned j=0;j<calls;++j)if(c[i].event==ES2_COLLIDE_SLOT?expected[j].id==5:avr_addresses[expected[j].id]==c[i].key)++want;
        if(c[i].count-counts[i]!=want)return fail("actor virtuals callee counter delta");
    }
    if(pair[0]!=pair[1]||calls!=avr_calls||memcmp(expected,avr_log,calls*sizeof(*expected))||memcmp(avr_after,avr_arena,AVR_SIZE)||
       memcmp(after,actual,AVR_GLOBALS))
    {
        for(unsigned j=0;j<AVR_GLOBALS;++j)if(after[j]!=actual[j]){fprintf(stderr," globals +%u %02x/%02x\n",j,after[j],actual[j]);break;}
        return avr_report("direct",n,pair,calls,expected);
    }
    return 0;
}
static int avr_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6]={0},expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;avr_build(index,n,0,1,a);
        BYTE before[AVR_GLOBALS],after[AVR_GLOBALS],actual[AVR_GLOBALS],expected_fpu[108],actual_fpu[108];static avr_record expected[AVR_MAX];
        memcpy(avr_before,avr_arena,AVR_SIZE);avr_globals(before,0);
        avr_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=avr_calls;memcpy(expected,avr_log,calls*sizeof(*expected));memcpy(avr_after,avr_arena,AVR_SIZE);avr_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("actor virtuals poison install");
        memcpy(avr_arena,avr_before,AVR_SIZE);avr_globals(before,1);avr_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);es2_hook_fpu_save(actual_fpu);avr_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=avr_calls||memcmp(expected,avr_log,calls*sizeof(*expected))||memcmp(avr_after,avr_arena,AVR_SIZE)||
           memcmp(after,actual,AVR_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=avr_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("actor virtuals poison uninstall");avr_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, EDX:EAX/trace/arena/globals\n",d->name,total);
    return status;
}
/* Nested hooks of the real pass (the callees with fixed addresses that are
 * hooks); the other callees run as original code except memset (import
 * thunk) and the shape's slot (arena vtable), which stay stubs. */
static unsigned avr_nested(unsigned index,unsigned out[2]){
    if(index==ES2_HOOK_INDEX_actor_attacked){out[0]=ES2_HOOK_INDEX_contact_share;out[1]=ES2_HOOK_INDEX_dist_gate;return 2;}
    if(index==ES2_HOOK_INDEX_base_part_position){out[0]=ES2_HOOK_INDEX_euler_matrix;return 1;}
    if(index==ES2_HOOK_INDEX_mech_target_scale){out[0]=ES2_HOOK_INDEX_multiply_q10;return 1;}
    if(index==ES2_HOOK_INDEX_mech_engage_roll){out[0]=ES2_HOOK_INDEX_rng_next;return 1;}
    return 0;
}
static int avr_real_pass(unsigned index){
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[2]={0},hooked=0,replaced=0;
    unsigned nested[2];const unsigned count=avr_nested(index,nested);
    static const int keep[AVR_IDS]={0,1,1,1,1,1,1,1};BYTE saved[AVR_IDS][5];
    if(avr_patch(saved,1,keep))return 1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6]={0},expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;avr_build(index,n,1,1,a);
        BYTE before[AVR_GLOBALS],after[AVR_GLOBALS],actual[AVR_GLOBALS];
        memcpy(avr_before,avr_arena,AVR_SIZE);avr_globals(before,0);
        avr_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        memcpy(avr_after,avr_arena,AVR_SIZE);avr_globals(after,0);
        es2_hook hk[3];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if((!e||e->compare_unsupported)&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("actor virtuals nested install");}
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+2,d,&o,function_hook_extension(index)))return fail("actor virtuals real install");
        memcpy(avr_arena,avr_before,AVR_SIZE);avr_globals(before,1);avr_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);avr_globals(actual,0);
        if(memcmp(expected_pair,actual_pair,8)||memcmp(avr_after,avr_arena,AVR_SIZE)||memcmp(after,actual,AVR_GLOBALS)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            fprintf(stderr,"real mode=%u\n",mode);avr_calls=0;status=avr_report("real",n,pair,0,NULL);
            for(unsigned j=0;j<AVR_GLOBALS;++j)if(after[j]!=actual[j]){fprintf(stderr," globals +%u %02x/%02x\n",j,after[j],actual[j]);break;}
        }
        for(unsigned j=0;j<3;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("actor virtuals nested compare mismatch");
            if((j<count||j==2)&&!es2_hook_uninstall(hk+j))return fail("actor virtuals real uninstall");}
        hooked+=hk[2].calls;replaced+=mode>=3;
        avr_globals(before,1);++total;
    }
    if(avr_patch(saved,0,keep))return 1;
    /* The hook counts its calls only when non-original. */
    if(!status&&hooked!=replaced){fprintf(stderr,"FAIL: %s real pass entered %llu of %llu\n",d->name,(unsigned long long)hooked,(unsigned long long)replaced);status=1;}
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, %u nested hook(s) in rotating modes, hook original/replaced, 31/a7 stack, EDX:EAX, arena and globals\n",d->name,total,count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
/* Open point: the caller 41f39f passes a front flag 0/1 (SETB of its own sector
 * test) where 4154d0 tests an angle; 0 and 1 (any high word) lie in [-4000h,
 * 4000h), so the original always returns the front word +222h from that site.
 * Checked on the original body. */
static int avr_front_flag_probe(void){
    for(unsigned n=0;n<4096;++n){
        uint32_t a[6]={0},r[3]={random32(),random32(),random32()};avr_build(ES2_HOOK_INDEX_mech_armour,n,1,1,a);
        put16(avr_arena+AVR_A+0x222,(uint16_t)random32());put16(avr_arena+AVR_A+0x224,(uint16_t)(get16(avr_arena+AVR_A+0x222)^0x5a5au));
        a[1]=(random32()&0xffff0000u)|(n&1u);
        const uint64_t pair=weapons_original(ES2_HOOK_INDEX_mech_armour,a,r);
        if((uint16_t)pair!=get16(avr_arena+AVR_A+0x222)||(uint16_t)(pair>>32)!=(uint16_t)pair){current_case=n;current_case_valid=1;return fail("mech_armour front flag");}
        a[1]=0x8000u|(n&0x7fffu);a[1]=(uint16_t)(a[1]+0x4000u)<0x8000u?a[1]^0x8000u:a[1];
        if((uint16_t)weapons_original(ES2_HOOK_INDEX_mech_armour,a,r)!=get16(avr_arena+AVR_A+0x224)){current_case=n;current_case_valid=1;return fail("mech_armour rear word");}
    }
    puts("PASS: mech_armour 4096 front-flag probes: flags 0/1 from 41f39f (any high word) always select the front word +222h; +224h only outside [-4000h,4000h)");
    return 0;
}
/* base_condition: the summed maxima 0 (no parts, or maxima cancelling)
 * and 80000000h / -1 (256 parts at 8000h over maxima summing to -1) fault
 * with the same exception code in the original and the adapter. */
static int avr_divide_probes(void){
    static BYTE big[256*11+256*30+64];const uint32_t p=(uint32_t)(uintptr_t)big,q=p+256*11;
    for(unsigned k=0;k<3;++k){
        uint32_t a[6]={0},r[3]={0};avr_build(ES2_HOOK_INDEX_base_condition,1,1,1,a);
        if(k==0)put16(avr_arena+AVR_C+0x12,0);
        else if(k==1){put16(avr_arena+AVR_C+0x12,2);put16(avr_arena+AVR_Q,5);put16(avr_arena+AVR_Q+30,0xfffb);}
        else {
            put32(avr_arena+AVR_A+0x205,p);put32(avr_arena+AVR_C+0x14,q);put16(avr_arena+AVR_C+0x12,256);
            for(unsigned i=0;i<256;++i){put16(big+11*i,0x8000);put16(big+256*11+30*i,i?0:0xffff);}
        }
        uint64_t pair[2];const DWORD want=k<2?EXCEPTION_INT_DIVIDE_BY_ZERO:EXCEPTION_INT_OVERFLOW;
        DWORD original=avr_run(ES2_HOOK_INDEX_base_condition,0,a,r,pair),replacement=avr_run(ES2_HOOK_INDEX_base_condition,1,a,r,pair+1);
        if(original!=want||replacement!=want){
            fprintf(stderr,"FAIL: base_condition divide probe %u: %08lx/%08lx\n",k,(unsigned long)original,(unsigned long)replacement);return 1;
        }
    }
    puts("PASS: base_condition IDIV faults kept: no parts and cancelling maxima (#DE divide by zero), 256 parts 8000h over -1 (#DE overflow), original and adapter");
    return 0;
}
/* Compare cases (compare_probes.h) for base_condition (no faulting IDIV),
 * mech_armour and mech_target_scale (real multiply_q10). */
static void avr_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    avr_build(index,n,1,1,a);for(unsigned i=0;i<3;++i)r[i]=random32();
}
static void avr_compare_state(BYTE *s,int restore){
    if(restore){memcpy(avr_arena,s,AVR_SIZE);avr_globals(s+AVR_SIZE,1);}else{memcpy(s,avr_arena,AVR_SIZE);avr_globals(s+AVR_SIZE,0);}
}
static int actor_virtuals_cases(unsigned index,unsigned limit){
    BYTE saved[AVR_IDS][5],old_globals[AVR_GLOBALS];
    avr_globals(old_globals,0);
    if(limit&&compare_probe_installed(index)){int status=compare_probe_run(index,limit,avr_compare_build,avr_compare_state,AVR_SIZE+AVR_GLOBALS);
        avr_globals(old_globals,1);return status;}
    PVOID handler=AddVectoredExceptionHandler(1,avr_divide_handler);if(!handler)return fail("actor virtuals handler");
    if(avr_patch(saved,1,NULL)){RemoveVectoredExceptionHandler(handler);return 1;}
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_ACTOR_VIRTUALS_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;avr_faults=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6]={0},r[3];avr_build(index,n,0,limit!=0,a);for(unsigned i=0;i<3;++i)r[i]=random32();status=avr_compare(index,n,a,r);avr_globals(old_globals,1);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    if(!limit&&!status&&index==ES2_HOOK_INDEX_mech_armour)status=avr_front_flag_probe();
    if(!limit&&!status&&index==ES2_HOOK_INDEX_base_condition)status=avr_divide_probes();
    if(!limit&&!status)status=avr_installed_probes(index,64);
    if(avr_patch(saved,0,NULL)){RemoveVectoredExceptionHandler(handler);return 1;}
    current_case_valid=0;
    if(!limit&&!status)status=avr_real_pass(index);
    if(!limit&&!status&&compare_probe_capable(index))status=compare_probe_run(index,0,avr_compare_build,avr_compare_state,AVR_SIZE+AVR_GLOBALS);
    avr_globals(old_globals,1);RemoveVectoredExceptionHandler(handler);
    if(!status)printf("PASS: %-20s %u cases (%u IDIV faults); EDX:EAX, callee calls with entry registers and arena hash, arena/globals\n",
        function_hook_descriptor(index)->name,cases,avr_faults);
    return status;
}
