/* Vehicle AI hooks object_line 404ca0 and obstacle_steer 416274 (ai.md).
 * Only the private, hash-checked mapping runs original code. Direct cases:
 * every game callee is a logging stub that records its ordered arguments,
 * entry registers and the frame payloads it receives, by content (frame
 * addresses, outputs the caller has not written and the matrix residue
 * byte 19 are masked). Then installed 31/a7 poison cases, and real-callee
 * cases under mixed nested modes on generated terrain and a generated object
 * pool; there only the shape query 4273c8 and the vehicle virtual +7Ch stay
 * stubs, and their inputs are compared. */
typedef struct ai_record { uint32_t id,args[6],regs[3],input[20],eax,edx,slots; } ai_record;
enum { AI_SIZE=0x1e00, AI_MAP=0x40, AI_CELLS=0x400, AI_ACTOR_BLOCK=0xa00, AI_ACTOR=0xa08, AI_BLOCKS=0xe00, AI_BLOCK=0x240,
    AI_OBJECTS=6, AI_INFO=0x1900, AI_LINK=0x1a00, AI_TEAM=0x1a20, AI_CONFIG=0x1b00, AI_HOLDER=0x1b80, AI_SHAPE=0x1bc0,
    AI_WIDE=0x1c40, AI_LIST=0x1c80, AI_POOL=0x1cc0, AI_VTABLE=0x1d00, AI_TURN=0x1d80, AI_SPEED=0x1d84, AI_OUT=0x1d90,
    AI_START=0x1da0, AI_END=0x1db0, AI_MAX=256, AI_IDS=15, AI_REAL_MAX=2048, AI_STEER_STUBS=0x1fc6, AI_LINE_STUBS=0x3f,
    AI_REAL_STUBS=0x4000 };
#define AI_BASE ((uint32_t)(uintptr_t)ai_arena)
static BYTE ai_arena[AI_SIZE],ai_before[AI_SIZE],ai_after[AI_SIZE];
static ai_record ai_log[AI_MAX];
static uint32_t ai_real_log[AI_REAL_MAX];
static unsigned ai_calls,ai_real_count,ai_quiet,ai_real;
static uint32_t ai_seed,ai_stream;
__attribute__((used)) static uint32_t ai_ecx;
/* Callees: 0..5 of 404ca0, 6..12 of 416274 (with 1 and 2), 13 the vehicle
 * virtual +7Ch (reached through the generated vtable), 14 the shape query
 * 4273c8 below 427da8 (stubbed only in the real-callee pass). */
static const uint32_t ai_addresses[AI_IDS]={0x492990,0x47ebb8,0x49288c,0x47df19,0x480304,0x404bc0,0x46e180,0x48043c,
    0x404ca0,0x46e980,0x4928d0,0x47e0b0,0x492934,0,0x4273c8};
static const unsigned ai_counts[AI_IDS]={3,2,2,1,3,3,2,3,5,6,2,2,2,1,4};
static uint32_t ai_next(void){
    uint32_t z=ai_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *ai_host(uint32_t address){return (BYTE *)(uintptr_t)address;}
/* Frame addresses differ between the original and the replacement run, so
 * values inside the worker thread's whole stack reservation are masked; the
 * fixed range keeps the mask identical in both runs. */
static uint32_t ai_stack_low,ai_stack_high;
static void ai_stack_range(void){
    MEMORY_BASIC_INFORMATION info={0};VirtualQuery(&info,&info,sizeof(info));
    ai_stack_low=(uint32_t)(uintptr_t)info.AllocationBase;ai_stack_high=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;
}
static int ai_stack_address(uint32_t value){return value-ai_stack_low<ai_stack_high-ai_stack_low;}
static uint32_t ai_object(unsigned k){return AI_BASE+AI_BLOCKS+AI_BLOCK*k+8u;}
/* `bytes` bytes at a guest address packed into dwords; byte `hole` (stack
 * residue in the original) is masked. */
static unsigned ai_content(ai_record *v,unsigned at,uint32_t address,unsigned bytes,unsigned hole){
    const BYTE *p=ai_host(address);
    for(unsigned j=0;j<bytes;j+=4){
        uint32_t w=0;for(unsigned k=0;k<4&&j+k<bytes;++k)if(j+k!=hole)w|=(uint32_t)p[j+k]<<(8*k);
        if(at==20){fail("ai payload overflow");ExitProcess(1);}
        v->input[at++]=w;
    }
    return at;
}
/* Distances and scaled distances around both thresholds (22000, 3000), plus
 * negative and full-width values. Quiet cases find nothing. */
static uint32_t ai_distance(uint32_t pick){
    if(ai_quiet)return 0x55f0u+(pick>>8)%3000u;
    switch(pick%10){
    case 0:case 1:case 2:return (pick>>8)%30000u;
    case 3:return 0x55f0u-4u+(pick>>8)%9u;
    case 4:case 5:return (pick>>8)%3600u;
    case 6:return 0u-(pick>>8)%5000u;
    case 7:return ai_next();
    default:return 2990u+(pick>>8)%21u;
    }
}
/* Heading results relative to the actor heading word: cone edges, the
 * half turn and both signs. */
static uint32_t ai_heading(uint32_t eax,uint32_t pick){
    static const uint16_t deltas[]={0x8000,0x1fff,0x2000,0xe001,0xe000,0x2fff,0x3000,0xd001,0xd000,0,1,0xffff,0x7fff,0x8001};
    uint16_t delta=pick%16<14?deltas[pick%16]:(uint16_t)(pick>>8);
    return (eax&0xffff0000u)|(uint16_t)(get16(ai_arena+AI_ACTOR+0x10)+delta);
}
static uint64_t ai_real_stub(unsigned id,const uint32_t *a,uint32_t eax,uint32_t edx,uint32_t pick){
    if(ai_real_count+10>AI_REAL_MAX){fail("ai real stub overflow");ExitProcess(1);}
    ai_real_log[ai_real_count++]=id;ai_real_log[ai_real_count++]=a[0];
    if(id==13)eax=pick%3?eax|1u<<((pick>>8)%16):eax&0xffff0000u;
    else {
        /* 4273c8(shape, out, segment of two points, radius word). */
        for(unsigned j=0;j<6;++j)ai_real_log[ai_real_count++]=get32(ai_host(a[2])+4*j);
        ai_real_log[ai_real_count++]=(uint16_t)a[3];
        if(pick%3==0){eax=(eax&0xffff0000u)|1u;for(unsigned j=0;j<16;++j)ai_host(a[1])[j]=(BYTE)ai_next();}
        else eax&=0xffff0000u;
    }
    return (uint64_t)edx<<32|eax;
}
__attribute__((used,noinline)) static uint64_t __cdecl ai_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    uint32_t eax=ai_next(),edx=ai_next(),pick=ai_next();ai_ecx=ai_next();
    if(ai_real)return ai_real_stub(id,a,eax,edx,pick);
    if(ai_calls==AI_MAX){fail("ai synthetic call overflow");ExitProcess(1);}
    ai_record *v=ai_log+ai_calls++;memset(v,0,sizeof(*v));v->id=id;v->slots=weapons_bridge_slots(a,ai_counts[id]);
    for(unsigned k=0;k<ai_counts[id];++k)v->args[k]=ai_stack_address(a[k])?0:a[k];
    for(unsigned j=0;j<3;++j)v->regs[j]=ai_stack_address(regs[j])?0:regs[j];
    unsigned at=0;BYTE *actor=ai_arena+AI_ACTOR;
    switch(id){
    case 0: /* 492990(angles out, end, start): six bytes out; may touch start. */
        at=ai_content(v,at,a[1],12,99);at=ai_content(v,at,a[2],12,99);
        for(unsigned j=0;j<6;++j)ai_host(a[0])[j]=(BYTE)ai_next();
        if(pick%8==0)put32(ai_host(a[2])+4*((pick>>8)%3),ai_next());
        break;
    case 1: /* 47ebb8(angles, matrix): nine words and the mode byte, never byte 19. */
        at=ai_content(v,at,a[0],6,99);for(unsigned j=0;j<19;++j)ai_host(a[1])[j]=(BYTE)ai_next();
        break;
    case 2:at=ai_content(v,at,a[0],12,99);at=ai_content(v,at,a[1],12,99);eax=ai_distance(pick);break;
    case 3:at=ai_content(v,at,a[0],20,19);if(pick%2)for(unsigned j=0;j<18;++j)ai_host(a[0])[j]=(BYTE)ai_next();break;
    case 4:
        at=ai_content(v,at,a[0],12,99);at=ai_content(v,at,a[1],20,19);at=ai_content(v,at,a[2],12,99);
        for(unsigned j=0;j<12;++j)ai_host(a[2])[j]=(BYTE)ai_next();
        break;
    case 5: /* 404bc0(out, record, filter): the record after its frame pointer to
             * the forward pose, then that pose. */
        at=ai_content(v,at,a[1]+4,0x26,0x19);at=ai_content(v,at,get32(ai_host(a[1])),32,19);
        if(pick%2)for(unsigned j=0;j<8;++j)ai_host(a[0])[j]=(BYTE)ai_next();
        break;
    case 6:at=ai_content(v,at,a[1],12,99);break;
    case 7: /* 48043c(vector, actor matrix, out): may clear the matrix-valid word. */
        at=ai_content(v,at,a[0],12,99);at=ai_content(v,at,a[1],32,99);
        for(unsigned j=0;j<12;++j)ai_host(a[2])[j]=(BYTE)ai_next();
        if(pick%4==0)put16(actor+0x32,0);
        break;
    case 8: /* 404ca0: a hit writes {distance, object}. */
        at=ai_content(v,at,a[1],12,99);at=ai_content(v,at,a[2],12,99);
        if(ai_quiet||pick%2)eax&=0xffff0000u;
        else {eax|=1u<<((pick>>8)%16);put32(ai_host(a[0]),ai_distance(ai_next()));put32(ai_host(a[0])+4,ai_next());}
        if((pick>>4)%4==0)put16(actor+0x32,0);
        break;
    case 9: /* 46e980 mode 1: a contact writes the out point. */
        at=ai_content(v,at,a[1],12,99);at=ai_content(v,at,a[2],12,99);
        if(ai_quiet||pick%2)eax&=0xffffff00u;
        else {eax|=1u<<((pick>>8)%8);for(unsigned j=0;j<12;++j)ai_host(a[4])[j]=(BYTE)ai_next();}
        break;
    case 10:at=ai_content(v,at,a[0],8,99);at=ai_content(v,at,a[1],8,99);eax=ai_distance(pick);break;
    case 11:eax=ai_distance(pick);break;
    case 12:at=ai_content(v,at,a[0],8,99);at=ai_content(v,at,a[1],8,99);eax=ai_heading(eax,pick);break;
    case 13:eax=pick%4?eax|1u<<((pick>>8)%16):eax&0xffff0000u;break;
    default:fail("ai stub id");ExitProcess(1);
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
/* Pushes the entry EAX/EDX/ECX for the record before the slot pointer. */
#define AI_STUB(n) __attribute__((naked)) static void ai_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _ai_stub_impl\n\taddl $24,%esp\n\tmovl _ai_ecx,%ecx\n\tret\n\t");}
AI_STUB(0) AI_STUB(1) AI_STUB(2) AI_STUB(3) AI_STUB(4) AI_STUB(5) AI_STUB(6) AI_STUB(7) AI_STUB(8) AI_STUB(9)
AI_STUB(10) AI_STUB(11) AI_STUB(12) AI_STUB(13) AI_STUB(14)
#undef AI_STUB
static void *ai_stubs[AI_IDS]={ai_stub_0,ai_stub_1,ai_stub_2,ai_stub_3,ai_stub_4,ai_stub_5,ai_stub_6,ai_stub_7,
    ai_stub_8,ai_stub_9,ai_stub_10,ai_stub_11,ai_stub_12,ai_stub_13,ai_stub_14};
/* 4a0be0 map, 4a9af8 vehicle list and count word, 4a9b88 points and their
 * count 4a9cc8, 4a95a0 object pool, 4a0c4c and 6b505c (470798), 4aaad0
 * (427da8's nearest distance). */
#define AI_GLOBALS 361
static void ai_globals(BYTE *out,int restore){
    static const unsigned offsets[]={0xa0be0,0xa9af8,0xa9b88,0xa95a0,0xa0c4c,0x2b505c,0xaaad0};
    static const unsigned sizes[]={4,8,0x144,4,1,16,4};
    for(unsigned i=0;i<7;++i){if(restore)memcpy(mapped+offsets[i],out,sizes[i]);else memcpy(out,mapped+offsets[i],sizes[i]);out+=sizes[i];}
}
static void ai_reset(void){ai_calls=ai_real_count=0;ai_stream=ai_seed;}
static int ai_patch(BYTE saved[AI_IDS][5],int active[AI_IDS],unsigned mask,int on){
    for(unsigned i=0;i<AI_IDS;++i){
        if(on){active[i]=0;if(!(mask>>i&1)||!ai_addresses[i])continue;if(actor_patch(ai_addresses[i]-0x400000u,ai_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(ai_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
static uint16_t ai_word(void){
    static const uint16_t words[]={0,1,0xffff,0x7fff,0x8000,2,0xfffe};
    return random32()%3?(uint16_t)(random32()%7<4?random32()%600-300:random32()):words[random32()%7];
}
/* Shared links: actor info 6 (team byte at [[info+0Ch]]+0A3h), configs,
 * vtable slot +7Ch, list and point table. */
static void ai_links(uint32_t actor,int real){
    uint32_t b=AI_BASE;BYTE *p=ai_host(actor);
    put32(p+0x45,b+AI_INFO+0x20*6);put32(ai_arena+AI_INFO+0x20*6+0xc,b+AI_LINK);put32(ai_arena+AI_LINK,b+AI_TEAM);
    ai_arena[AI_TEAM+0xa3]=(BYTE)(random32()%3==0?0:1+random32()%255);
    p[0xae]=(BYTE)(random32()%2?0:1+random32()%255);put16(p+0x254,ai_word());
    put32(p+0x1f2,b+AI_CONFIG+0x10*6);put32(p+0x34,b+AI_HOLDER+8*6);put32(p+0x205,b+AI_WIDE+8*6);
    put32(ai_arena+AI_VTABLE+0x7c,(uint32_t)(uintptr_t)ai_stubs[13]);
    for(unsigned k=0;k<7;++k){
        put32(ai_arena+AI_HOLDER+8*k+4,b+AI_SHAPE+0x10*k);put16(ai_arena+AI_SHAPE+0x10*k+8,(uint16_t)(random32()%2000));
        put32(ai_arena+AI_INFO+0x20*k+0x14,random32()%3?0:random32()%4?1u:random32());
        ai_arena[AI_INFO+0x20*k+0x12]=(BYTE)(random32()%4);
        put16(ai_arena+AI_CONFIG+0x10*k+6,(uint16_t)(random32()%2?0:random32()));
        put16(ai_arena+AI_CONFIG+0x10*k+4,(uint16_t)(random32()%4?random32():0xffff));
        put16(ai_arena+AI_WIDE+8*k+5,(uint16_t)(random32()%4));
    }
    /* Vehicle list: objects, the actor itself and null entries; count words
     * around the list length, negative and 7fffh (slot 12 stays null). */
    unsigned length=random32()%13;
    for(unsigned k=0;k<13;++k){
        uint32_t entry=random32()%10==0?actor:ai_object(random32()%AI_OBJECTS);
        if(k>=length||random32()%16==0)entry=0;
        put32(ai_arena+AI_LIST+4*k,entry);
    }
    put32(ai_arena+AI_LIST+48,0);
    uint16_t count=(uint16_t)(random32()%16<14?random32()%15-1:random32()%2?0x7fff:0x8000);
    put32(mapped+0xa9af8,b+AI_LIST);put16(mapped+0xa9afe,count);
    put32(mapped+0xa9cc8,random32()%16?random32()%13-1:random32()%2?0x80000000u:0xffffff00u);
    (void)real;
}
static void ai_line_build(unsigned n,uint32_t a[6],uint32_t r[3]){
    uint32_t b=AI_BASE;ai_seed=random32();random_bytes(ai_arena,AI_SIZE);ai_quiet=0;
    memset(a,0,24);for(unsigned i=0;i<3;++i)r[i]=random32();
    a[0]=b+AI_OUT;a[1]=b+AI_START;a[2]=b+AI_END;
    a[3]=n%4==0?0:random32();a[4]=n%4==1?0xffffffffu:random32();
}
static void ai_steer_build(unsigned n,uint32_t a[6],uint32_t r[3]){
    uint32_t b=AI_BASE,actor=b+AI_ACTOR;ai_seed=random32();random_bytes(ai_arena,AI_SIZE);ai_quiet=random32()%4==0;
    memset(a,0,24);for(unsigned i=0;i<3;++i)r[i]=random32();
    BYTE *p=ai_arena+AI_ACTOR;
    put16(p+0x28e,(uint16_t)(random32()%4==0?0:random32()));put32(p+0x26e,random32()%4==0?0:random32());
    put16(p+0x32,(uint16_t)(random32()%2?0:1+random32()%0xffff));
    ai_links(actor,0);
    for(unsigned k=0;k<AI_OBJECTS;++k){uint32_t o=ai_object(k);put32(ai_host(o),b+AI_VTABLE);put32(ai_host(o)+0x45,b+AI_INFO+0x20*k);}
    put16(ai_arena+AI_SPEED,(uint16_t)(random32()%5==0?0:random32()%5==0?0x8000u|random32():random32()%0x8000));
    a[0]=actor;a[1]=b+AI_TURN;a[2]=b+AI_SPEED;a[3]=(random32()&~1u)|(random32()%4==0);
    put32(mapped+0xa0be0,random32());
    random_bytes(mapped+0xa9b88,0x140);
    (void)n;
}
static void ai_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    if(index==ES2_HOOK_INDEX_object_line)ai_line_build(n,a,r);else ai_steer_build(n,a,r);
}
static int ai_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const ai_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,ai_calls);
    for(unsigned j=0;j<calls&&j<ai_calls&&expected;++j)if(memcmp(expected+j,ai_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(ai_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<AI_SIZE;++j)if(ai_after[j]!=ai_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,ai_after[j],ai_arena[j]);break;}
    return 1;
}
static unsigned ai_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<ai_calls;++j){unsigned id=ai_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?id==13:id!=13&&ai_addresses[id]==c->key)++hits;}
    return hits;
}
static int ai_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[AI_GLOBALS],after[AI_GLOBALS],actual[AI_GLOBALS];static ai_record expected[AI_MAX];
    memcpy(ai_before,ai_arena,AI_SIZE);ai_globals(before,0);
    ai_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=ai_calls;memcpy(expected,ai_log,calls*sizeof(*expected));
    memcpy(ai_after,ai_arena,AI_SIZE);ai_globals(after,0);
    memcpy(ai_arena,ai_before,AI_SIZE);ai_globals(before,1);ai_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);ai_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=ai_expected_hits(c+i))return fail("ai callee counter delta");
    if(pair[0]!=pair[1]||calls!=ai_calls||memcmp(expected,ai_log,calls*sizeof(*expected))||memcmp(ai_after,ai_arena,AI_SIZE)||memcmp(after,actual,AI_GLOBALS)){
        for(unsigned j=0;j<AI_GLOBALS;++j)if(after[j]!=actual[j])fprintf(stderr," global byte %u %02x/%02x\n",j,after[j],actual[j]);
        return ai_report("direct",n,pair,calls,expected);
    }
    return 0;
}
static int ai_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;ai_build(index,n,a,r);
        BYTE before[AI_GLOBALS],after[AI_GLOBALS],actual[AI_GLOBALS],expected_fpu[108],actual_fpu[108];static ai_record expected[AI_MAX];
        memcpy(ai_before,ai_arena,AI_SIZE);ai_globals(before,0);
        ai_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=ai_calls;memcpy(expected,ai_log,calls*sizeof(*expected));memcpy(ai_after,ai_arena,AI_SIZE);ai_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("ai poison install");
        memcpy(ai_arena,ai_before,AI_SIZE);ai_globals(before,1);ai_reset();sg_poison_call(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);ai_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=ai_calls||memcmp(expected,ai_log,calls*sizeof(*expected))||memcmp(ai_after,ai_arena,AI_SIZE)||
           memcmp(after,actual,AI_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=ai_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("ai poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state\n",d->name,total);
    return status;
}
/* Real callees. Terrain of up to 8 x 8 cells (flags 0..2, margins around the
 * array), the actor and six objects near each other, the pool 4a95a0 linking
 * a subset of them for 404bc0, vehicles and avoidance points around the
 * actor. Everything except 4273c8 and the +7Ch virtual is original code or a
 * nested hook. */
static void ai_real_place(BYTE *p,uint32_t x,uint32_t y,uint32_t z,int flag){
    put32(p+0x26,x);put32(p+0x2a,y);put32(p+0x2e,z);
    for(unsigned j=0;j<3;++j)put16(p+0xc+2*j,(uint16_t)(random32()%3?random32():0));
    put16(p+0x32,(uint16_t)(flag?1:0));
}
static void ai_real_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    uint32_t b=AI_BASE,actor=b+AI_ACTOR;ai_seed=random32();random_bytes(ai_arena,AI_SIZE);ai_quiet=0;
    memset(a,0,24);for(unsigned i=0;i<3;++i)r[i]=random32();
    BYTE *map=ai_arena+AI_MAP;uint32_t w=random32()%4,h=random32()%4,shift=6+random32()%6;
    put32(map+0x100,w);put32(map+0x104,h);put32(map+0x108,shift);
    put32(map+0xec,b+AI_CELLS);put32(map+0x110,random32()%4096);put32(map+0x118,random32()%64);
    /* Game terrain stores flags & 3 as 0, 1 or 2 only (weapons.md). */
    for(unsigned j=0;j<(16u<<(w+h));++j)if(j%16==15)ai_arena[AI_CELLS+j]=(BYTE)((random32()&0xfcu)|random32()%3);
    int32_t width=(int32_t)(1u<<(w+shift)),height=(int32_t)(1u<<(h+shift));
    int32_t ax=(int32_t)(random32()%(uint32_t)(width+width/2))-width/4,ay=(int32_t)(random32()%(uint32_t)(height+height/2))-height/4;
    uint32_t az=random32()%24000;
    BYTE *p=ai_arena+AI_ACTOR;ai_real_place(p,(uint32_t)ax,(uint32_t)ay,az,random32()%4==0);
    put16(p+0x28e,(uint16_t)(random32()%8?1+random32()%0x7fff:0));put32(p+0x26e,random32()%8?1:0);
    put16(p+0x10,(uint16_t)random32());p[0x99]=(BYTE)(random32()%2);
    ai_links(actor,1);
    for(unsigned k=0;k<AI_OBJECTS;++k){
        uint32_t o=ai_object(k);BYTE *q=ai_host(o);
        ai_real_place(q,(uint32_t)(ax+(int32_t)(random32()%24000)-12000),(uint32_t)(ay+(int32_t)(random32()%24000)-12000),
            az+random32()%4000-2000,random32()%2);
        put32(q,b+AI_VTABLE);put32(q+0x45,b+AI_INFO+0x20*k);put32(q+0x1f2,b+AI_CONFIG+0x10*k);
        put32(q+0x34,b+AI_HOLDER+8*k);put32(q+0x205,b+AI_WIDE+8*k);q[0x99]=(BYTE)(random32()%3?1:0);
    }
    /* Pool: header +0 next, +4 previous, object at +8; 471c70 walks it from
     * the last header back. The actor may be a member. */
    uint32_t headers[AI_OBJECTS+1],count=0,members=random32()%(AI_OBJECTS+2);
    for(unsigned k=0;k<members&&k<AI_OBJECTS;++k)headers[count++]=ai_object(k)-8u;
    if(random32()%3==0)headers[count++]=actor-8u;
    for(unsigned k=0;k<count;++k){put32(ai_host(headers[k]),k+1<count?headers[k+1]:0);put32(ai_host(headers[k])+4,k?headers[k-1]:0);}
    put32(ai_arena+AI_POOL+0xe,count?headers[0]:0);put32(mapped+0xa95a0,b+AI_POOL);
    for(unsigned k=0;k<40;++k){put32(mapped+0xa9b88+8*k,(uint32_t)(ax+(int32_t)(random32()%40000)-20000));
        put32(mapped+0xa9b8c+8*k,(uint32_t)(ay+(int32_t)(random32()%40000)-20000));}
    put32(mapped+0xa9cc8,random32()%8);
    put32(mapped+0xa0be0,b+AI_MAP);mapped[0xa0c4c]=(BYTE)(random32()%2);put32(mapped+0xaaad0,random32());
    if(index==ES2_HOOK_INDEX_object_line){
        /* From one generated point to another, like the feelers. */
        put32(ai_arena+AI_START,(uint32_t)(ax+(int32_t)(random32()%8000)-4000));put32(ai_arena+AI_START+4,(uint32_t)(ay+(int32_t)(random32()%8000)-4000));
        put32(ai_arena+AI_START+8,az);
        put32(ai_arena+AI_END,(uint32_t)(ax+(int32_t)(random32()%50000)-25000));put32(ai_arena+AI_END+4,(uint32_t)(ay+(int32_t)(random32()%50000)-25000));
        put32(ai_arena+AI_END+8,random32()%3?az:az+random32()%8000-4000);
        a[0]=b+AI_OUT;a[1]=b+AI_START;a[2]=b+AI_END;a[3]=random32()%512;a[4]=random32()%2?0xffffffffu:random32()%4;
        return;
    }
    put16(ai_arena+AI_SPEED,(uint16_t)(random32()%8?1+random32()%0x7fff:0));
    a[0]=actor;a[1]=b+AI_TURN;a[2]=b+AI_SPEED;a[3]=(random32()&~1u)|(random32()%8==0);
    (void)n;
}
static int ai_real_cases(unsigned index){
    static const unsigned line_nested[]={ES2_HOOK_INDEX_euler_matrix,ES2_HOOK_INDEX_dist_gate,ES2_HOOK_INDEX_matrix_transpose,
        ES2_HOOK_INDEX_rotate_point,ES2_HOOK_INDEX_dist2d_delta,ES2_HOOK_INDEX_transform_point};
    static const unsigned steer_nested[]={ES2_HOOK_INDEX_terrain_height,ES2_HOOK_INDEX_euler_matrix,ES2_HOOK_INDEX_transform_point,
        ES2_HOOK_INDEX_object_line,ES2_HOOK_INDEX_segment,ES2_HOOK_INDEX_dist2d_point,ES2_HOOK_INDEX_dist_gate,
        ES2_HOOK_INDEX_multiply_q10,ES2_HOOK_INDEX_heading_point,ES2_HOOK_INDEX_face_test,ES2_HOOK_INDEX_face_cross,
        ES2_HOOK_INDEX_normalize,ES2_HOOK_INDEX_matrix_transpose,ES2_HOOK_INDEX_rotate_point};
    /* Nested hooks every pass must reach (the direct callees). */
    static const unsigned line_required=0xf,steer_required=0x1dd;
    const int line=index==ES2_HOOK_INDEX_object_line;
    const unsigned *nested=line?line_nested:steer_nested,count=line?6:14,required=line?line_required:steer_required;
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0,hits=0;uint64_t entered[16]={0},contacts=0;
    BYTE saved[AI_IDS][5];int active[AI_IDS];if(ai_patch(saved,active,AI_REAL_STUBS,1))return 1;
    static uint32_t expected_log[AI_REAL_MAX];
    ai_real=1;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<1024&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;ai_real_build(index,n,a,r);
        BYTE before[AI_GLOBALS],after[AI_GLOBALS],actual[AI_GLOBALS];
        memcpy(ai_before,ai_arena,AI_SIZE);ai_globals(before,0);
        es2_hook hk[17];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("ai nested install");}
        ai_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        unsigned logged=ai_real_count;memcpy(expected_log,ai_real_log,4*logged);
        memcpy(ai_after,ai_arena,AI_SIZE);ai_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+count,d,&o,function_hook_extension(index)))return fail("ai caller mode install");
        memcpy(ai_arena,ai_before,AI_SIZE);ai_globals(before,1);
        ai_reset();sg_poison_call(mapped+d->rva,a,pattern,actual_pair);ai_globals(actual,0);
        /* 404bc0 can leave a frame address in EDX (dead at both callers of
         * 404ca0); the steering pair is path data and compared in full. */
        int same=expected_pair[0]==actual_pair[0]&&(line||expected_pair[1]==actual_pair[1]);
        if(!same||logged!=ai_real_count||memcmp(expected_log,ai_real_log,4*logged)||memcmp(ai_after,ai_arena,AI_SIZE)||memcmp(after,actual,AI_GLOBALS)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            fprintf(stderr,"real mode=%u stub log %u/%u\n",mode,logged,ai_real_count);ai_calls=0;status=ai_report("real",n,pair,0,NULL);
        }
        hits+=line?(uint16_t)expected_pair[0]!=0:get16(ai_before+AI_TURN)!=get16(ai_after+AI_TURN);
        /* Feeler terrain contacts: 4928d0 runs once per contact and once per
         * avoidance point; counted where dist2d_point is hooked (both runs). */
        if(!line&&hk[5].installed&&(get16(ai_before+AI_ACTOR+0x28e)||get16(ai_before+AI_SPEED)))
            contacts+=hk[5].calls-2u*(ai_arena[AI_TEAM+0xa3]?get32(mapped+0xa9cc8):0u);
        for(unsigned j=0;j<=count;++j){if(j<count)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("ai nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("ai nested uninstall");}
        ai_globals(before,1);++total;
    }
    ai_real=0;
    if(ai_patch(saved,active,0,0))return 1;
    for(unsigned j=0;j<count&&!status;++j)if(required>>j&1&&!entered[j]){
        fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status&&(!hits||hits==total))status=fail("ai real cases without both outcomes");
    if(!status&&!line&&!contacts)status=fail("ai real cases without a feeler terrain contact");
    if(!status){printf("PASS: %s %u real-callee cases on generated terrain and pool (%u %s), mixed nested modes, 31/a7 stack, %s, memory and stub inputs\n",
        d->name,total,hits,line?"object hits":"turn changes",line?"EAX":"EDX:EAX");
        if(!line)printf("REAL %s feeler terrain contacts %llu (twice per hooked case)\n",d->name,(unsigned long long)contacts);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
static int ai_cases(unsigned index,unsigned limit){
    BYTE saved[AI_IDS][5],old_globals[AI_GLOBALS];int active[AI_IDS];
    const int line=index==ES2_HOOK_INDEX_object_line;ai_stack_range();
    ai_globals(old_globals,0);if(ai_patch(saved,active,line?AI_LINE_STUBS:AI_STEER_STUBS,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_AI_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];ai_build(index,n,a,r);status=ai_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=ai_installed_probes(index,64);
    if(ai_patch(saved,active,0,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=ai_real_cases(index);
    ai_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, frame payloads by content, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
