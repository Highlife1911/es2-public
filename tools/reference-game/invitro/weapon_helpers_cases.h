/* Weapon group and collision-sphere helpers: group_fire 4107e4, weapon_ready
 * 40ede8, part_condition 40db2c, sphere_hits 40c524, sphere_test 40c428. Direct
 * cases run the original body and the core against logging stubs for every game
 * callee (entry registers and full argument slots recorded, frame payloads by
 * content), then installed 31/a7 poison probes, then a real-callee pass with
 * the nested hooks in rotating modes. Stack values: both runs keep their locals
 * in different frames of the same worker stack, so a value inside the stack
 * reservation is compared by its low word (group_fire, where the original
 * replaces the low word of a frame address with live data) or only as "a stack
 * address" (full frame addresses passed to 4801d4 and 40c428; EDX residues). */
typedef struct wph_record { uint32_t id,args[3],regs[3],input[6],eax,edx; } wph_record;
enum { WPH_SIZE=0x4000, WPH_GROUP=0x40, WPH_LIST=0x60, WPH_OWNER=0xa0, WPH_VT0=0x180, WPH_VT1=0x200,
    WPH_WEAPONS=0x300, WPH_ANIM=0x800, WPH_REGION=0x40, WPH_SPHERES=0x100, WPH_OFFSET=0x200, WPH_MATRIX=0x240,
    WPH_PARTS=0x40, WPH_PTABLE=0x80, WPH_CURRENT=0x100, WPH_SECOND=0x400, WPH_LIMITS=0x700, WPH_TABLE=0x1000,
    WPH_MAX=256, WPH_IDS=7, WPH_GLOBALS=18 };
#define WPH_B ((uint32_t)(uintptr_t)wph_arena)
static BYTE wph_arena[WPH_SIZE],wph_before[WPH_SIZE],wph_after[WPH_SIZE];
static wph_record wph_log[WPH_MAX],wph_expected[WPH_MAX];
static unsigned wph_calls,wph_size=WPH_SIZE; /* 16 KiB only for part_condition */
static uint32_t wph_seed,wph_stream,wph_radius;
__attribute__((used)) static uint32_t wph_ecx;
/* V48, V34, 40ede8, V28 (virtual slots of the weapon vtables), 4801d4,
 * 40c428, 47de4c. */
static const uint32_t wph_addresses[WPH_IDS]={0,0,0x40ede8,0,0x4801d4,0x40c428,0x47de4c};
static const unsigned wph_counts[WPH_IDS]={1,3,1,3,3,3,2},wph_slots[WPH_IDS]={0x48,0x34,0,0x28,0,0,0};
static uint32_t wph_next(void){
    uint32_t z=wph_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *wph_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static uint32_t wph_weapon(unsigned i){return WPH_B+WPH_WEAPONS+0x80u*i;}
/* The worker's 16 MiB stack reservation: a fixed window for both runs. */
static int wph_window(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static uint32_t wph_mask_low(uint32_t v){return wph_window(v)?0x57ac0000u|(uint16_t)v:v;}
static uint32_t wph_mask_full(uint32_t v){return wph_window(v)?0x57ac57acu:v;}
static uint32_t wph_mask(unsigned id,uint32_t v){return id<4?wph_mask_low(v):wph_mask_full(v);}
static int wph_pair_equal(unsigned index,uint32_t oa,uint32_t od,uint32_t ca,uint32_t cd){
    if(index==ES2_HOOK_INDEX_group_fire){oa=wph_mask_low(oa);ca=wph_mask_low(ca);}
    else {oa=wph_mask_full(oa);ca=wph_mask_full(ca);}
    return oa==ca&&wph_mask_full(od)==wph_mask_full(cd);
}
/* Count -1..10, list entries, a weapon's vtable. */
static void wph_mutate_group(uint32_t pick){
    if(pick%11==0)put16(wph_arena+WPH_GROUP+8,(uint16_t)(wph_next()%12u-1u));
    if(pick%7==0){unsigned k=wph_next()%12;put32(wph_arena+WPH_LIST+4*k,wph_next()%5?wph_weapon(wph_next()%10):0);}
    if(pick%13==0){BYTE *w=wph_host(wph_weapon(wph_next()%10));put32(w,WPH_B+(get32(w)==WPH_B+WPH_VT0?WPH_VT1:WPH_VT0));}
}
__attribute__((used,noinline)) static uint64_t __cdecl wph_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(wph_calls==WPH_MAX){fail("weapon helper synthetic call overflow");ExitProcess(1);}
    wph_record *v=wph_log+wph_calls++;memset(v,0,sizeof(*v));v->id=id;
    for(unsigned j=0;j<wph_counts[id];++j)v->args[j]=wph_mask(id,a[j]);
    for(unsigned j=0;j<3;++j)v->regs[j]=wph_mask(id,regs[j]);
    uint32_t eax=wph_next(),edx=wph_next(),pick=wph_next();wph_ecx=wph_next();
    switch(id){
    case 0: /* V48 priority: small words make ties. */
        if(pick%3)eax=(eax&0xffff0000u)|(uint16_t)((pick>>8)%5u-2u);
        wph_mutate_group(pick>>4);
        break;
    case 1: { /* V34 maintenance: new value word; may change readiness. */
        BYTE *w=wph_host(a[0]);
        if(pick%3==0)put16(w+0x31,(pick>>8)%2?0:(uint16_t)wph_next());
        if(pick%5==0)w[0x4d]=(BYTE)((pick>>9)%4?wph_next():0);
        if(pick%7==0)w[0x43]=(BYTE)((pick>>10)%2?wph_next():0);
        wph_mutate_group(pick>>5);
        break; }
    case 2: /* 40ede8: AX 0, 1 or another word. */
        eax=(eax&0xffff0000u)|(pick%3==0?0u:pick%3==1?1u:(uint16_t)(pick>>8));
        break;
    case 3: { /* V28 emission: may set the any byte and change the group. */
        BYTE *w=wph_host(a[0]);
        if(pick%3==0)w[0x43]=(BYTE)((pick>>8)%2?1+wph_next()%255:0);
        wph_mutate_group(pick>>6);
        break; }
    case 4: { /* 4801d4: point by content, frame layout, 12 output bytes. */
        BYTE *p=wph_host(a[0]),*out=wph_host(a[2]);
        for(unsigned j=0;j<3;++j)v->input[j]=get16(p+2*j);
        v->input[3]=a[2]-a[0];v->input[4]=regs[1]==a[2];v->input[5]=regs[2]==a[0];
        uint32_t range=get32(mapped+0xa9810),y;
        switch(pick%6){case 0:y=range-1u;break;case 1:y=range;break;case 2:y=range+1u;break;
            case 3:y=wph_next()%(range|1u);break;default:y=wph_next();}
        put32(out,(pick>>8)&1?wph_next():wph_next()%4096u-2048u);put32(out+4,y);put32(out+8,(pick>>9)&1?wph_next():wph_next()%4096u-2048u);
        if(pick%9==0)put32(mapped+0xa980c,wph_next());
        break; }
    case 5: { /* 40c428: output by content; may shorten the range, change the region. */
        BYTE *out=wph_host(a[0]);
        for(unsigned j=0;j<3;++j)v->input[j]=get32(out+4*j);
        v->input[3]=regs[1]==a[0];
        eax=(eax&0xffff0000u)|(pick%2?0u:(uint16_t)(1u+(pick>>8)%0xffffu));
        if(pick%5==0)put32(mapped+0xa9810,wph_next());
        if(pick%7==0)put16(wph_arena+WPH_REGION,(uint16_t)(wph_next()%14u-1u));
        if(pick%11==0)put16(wph_arena+WPH_SPHERES+2*(wph_next()%64),(uint16_t)wph_next());
        break; }
    case 6: { /* 47de4c: around 7FFFh and around the wrapped radius test. */
        static const uint32_t fixed[]={0x7ffe,0x7fff,0x8000,0,0xffffffffu,0x80000000u};
        uint32_t radius=(uint16_t)(get16(mapped+0xa9814)+wph_radius);
        switch(pick%5){case 0:eax=fixed[(pick>>8)%6];break;case 1:eax=radius+(pick>>8)%5u-2u;break;
            case 2:eax=wph_next()%(2*radius+1u);break;case 3:eax=wph_next()%0x7fffu;break;default:break;}
        if(pick%13==0)put32(mapped+0xa9810,wph_next());
        if(pick%17==0)put16(mapped+0xa9814,(uint16_t)wph_next());
        if(pick%19==0)put32(wph_arena+WPH_REGION+4,wph_next());
        break; }
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
/* Pushes the entry EAX/EDX/ECX for the record before the slot pointer. */
#define WPH_STUB(n) __attribute__((naked)) static void wph_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _wph_stub_impl\n\taddl $24,%esp\n\tmovl _wph_ecx,%ecx\n\tret\n\t");}
WPH_STUB(0) WPH_STUB(1) WPH_STUB(2) WPH_STUB(3) WPH_STUB(4) WPH_STUB(5) WPH_STUB(6)
#undef WPH_STUB
static void *wph_stubs[WPH_IDS]={wph_stub_0,wph_stub_1,wph_stub_2,wph_stub_3,wph_stub_4,wph_stub_5,wph_stub_6};
/* Probe range, bias and matrix pointer (4a9810, 4a9814, 4a980c); the
 * free-shot words 4a9e52 and 4a9e58. */
static void wph_globals(BYTE *out,int restore){
    static const unsigned offsets[]={0xa980c,0xa9e52},sizes[]={10,8};
    for(unsigned i=0;i<2;++i){if(restore)memcpy(mapped+offsets[i],out,sizes[i]);else memcpy(out,mapped+offsets[i],sizes[i]);out+=sizes[i];}
}
static void wph_reset(void){wph_calls=0;wph_stream=wph_seed;}
static uint32_t wph_part_sum(uint32_t *maximum){
    BYTE *x=wph_arena+WPH_PARTS;uint32_t sum=0,limit;*maximum=0;
    for(uint32_t k=0;(int16_t)k<(int16_t)get16(x+0x18);++k){
        limit=(uint32_t)(int32_t)(int16_t)get16(wph_arena+WPH_TABLE+18*k);int16_t c=(int16_t)get16(wph_arena+WPH_CURRENT+2*k);
        sum+=c<0?limit:(uint32_t)(int32_t)c;*maximum+=limit;
    }
    for(uint32_t k=0;(int16_t)k<(int16_t)get16(x+0x1a);++k){
        limit=(uint32_t)(int32_t)(int16_t)get16(wph_arena+WPH_LIMITS+2*k);int16_t c=(int16_t)get16(wph_arena+WPH_SECOND+2*k);
        sum+=c<0?limit:(uint32_t)(int32_t)c;*maximum+=limit;
    }
    return sum;
}
static void wph_matrix(void){
    BYTE *mx=wph_arena+WPH_MATRIX;static const BYTE modes[]={0,1,2,3,0x80,0xff};
    for(unsigned j=0;j<9;++j)put16(mx+2*j,(uint16_t)(random32()%32769u-16384u));
    mx[18]=modes[random32()%6];
    for(unsigned j=0;j<3;++j)put32(mx+20+4*j,random32()%16384u-8192u);
}
static void wph_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    uint32_t b=WPH_B;wph_size=index==ES2_HOOK_INDEX_part_condition?WPH_SIZE:0x1000u;
    /* Above 0x1000 only part_condition's table lives; its read words are all written. */
    wph_seed=random32();random_bytes(wph_arena,0x1000);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    if(index==ES2_HOOK_INDEX_group_fire){
        BYTE *g=wph_arena+WPH_GROUP;
        uint32_t count=n%13==0?0:n%29==0?(0x8000u|random32()):n%31==0?0xffffu:1+random32()%10;
        put32(g+4,b+WPH_LIST);put16(g+8,(uint16_t)count);
        for(unsigned k=0;k<12;++k)put32(wph_arena+WPH_LIST+4*k,random32()%6?wph_weapon(random32()%10):0);
        for(unsigned i=0;i<10;++i){
            BYTE *w=wph_host(wph_weapon(i));
            put32(w,b+(random32()%2?WPH_VT0:WPH_VT1));
            put16(w+0x31,random32()%2?0:(uint16_t)random32());
            w[0x4d]=(BYTE)(random32()%2?0:random32()|1u);w[0x43]=(BYTE)(random32()%2?0:random32()|1u);
            if(real){w[0x49]=(BYTE)(random32()%2?0:1u+random32()%255u);w[0x44]=(BYTE)(random32()%3==0);
                put32(w+0x10,b+WPH_ANIM);put32(w+0x14,b+WPH_ANIM+0x30);}
        }
        if(real){put32(wph_arena+WPH_ANIM+0x20,b+WPH_ANIM+0x40);put16(wph_arena+WPH_ANIM+0x40,(uint16_t)(1+random32()%40));
            put16(wph_arena+WPH_ANIM+0x30,(uint16_t)(random32()%50));}
        for(unsigned v=0;v<2;++v){
            BYTE *t=wph_arena+(v?WPH_VT1:WPH_VT0);
            if(real){put32(t+0x48,(uint32_t)(uintptr_t)(mapped+(v?0xf504:0x111e2)));put32(t+0x34,(uint32_t)(uintptr_t)(mapped+0xef94));
                put32(t+0x28,(uint32_t)(uintptr_t)(mapped+0xe95c));}
            else {put32(t+0x48,(uint32_t)(uintptr_t)wph_stubs[0]);put32(t+0x34,(uint32_t)(uintptr_t)wph_stubs[1]);put32(t+0x28,(uint32_t)(uintptr_t)wph_stubs[3]);}
        }
        wph_arena[WPH_OWNER+0xa3]=(BYTE)(random32()%3==0?0:random32()|1u);
        put16(mapped+0xa9e52,random32()%2?0:(uint16_t)(random32()|1u));
        put16(mapped+0xa9e58,random32()%2?1:(uint16_t)random32());
        uint16_t preferred=n%4==0?0xffff:n%4==1?(uint16_t)(random32()%10):n%4==2?(uint16_t)(0x8000u|random32()):(uint16_t)(random32()%10);
        a[0]=b+WPH_GROUP;a[1]=random32();a[2]=(random32()&0xffff0000u)|preferred;a[3]=b+WPH_OWNER;
    } else if(index==ES2_HOOK_INDEX_weapon_ready){
        BYTE *w=wph_host(wph_weapon(0));
        put16(w+0x31,n%2?0:n%4==2?1:(uint16_t)random32());w[0x4d]=(BYTE)(n%3==0?0:random32());
        a[0]=wph_weapon(0);
    } else if(index==ES2_HOOK_INDEX_part_condition){
        BYTE *x=wph_arena+WPH_PARTS;int big=n%16==0;
        put32(x+4,b+WPH_CURRENT);put32(x,b+WPH_SECOND);put32(x+0xc,b+WPH_PTABLE);
        put32(wph_arena+WPH_PTABLE,b+WPH_TABLE);put32(wph_arena+WPH_PTABLE+4,b+WPH_LIMITS);
        uint32_t c1=big?200+random32()%100:random32()%41,c2=big?200+random32()%100:random32()%41;
        if(n%23==0)c1=0x8000u|random32();
        if(n%19==0)c2=0;
        if(n%37==0)c1=0;
        put16(x+0x18,(uint16_t)c1);put16(x+0x1a,(uint16_t)c2);
        /* Limits and current values; a negative current word selects the
         * limit. Large words with long tables wrap the sum shifted by 8. */
        for(unsigned k=0;k<(big?300u:41u);++k)for(unsigned table=0;table<2;++table){
            uint16_t limit=(uint16_t)(big?0x6000u+random32()%0x2000u:random32()%4?random32()%2000:random32());
            uint16_t current=(uint16_t)(random32()%3==0?0x8000u|random32():big?0x6000u+random32()%0x2000u:random32()%4?random32()%2000:random32()&0x7fffu);
            if(big&&n%32==0){limit=(uint16_t)(0u-limit);if(random32()%2)current=(uint16_t)(0u-current);}
            put16(wph_arena+(table?WPH_LIMITS+2*k:WPH_TABLE+18*k),limit);
            put16(wph_arena+(table?WPH_SECOND:WPH_CURRENT)+2*k,current);
        }
        /* IDIV domain: nonzero divisor, no quotient overflow. */
        for(;;){
            uint32_t maximum,sum=wph_part_sum(&maximum);
            if(maximum&&!((sum<<8)==0x80000000u&&maximum==0xffffffffu))break;
            if((int16_t)get16(x+0x18)<=0&&(int16_t)get16(x+0x1a)<=0)put16(x+0x18,1);
            if((int16_t)get16(x+0x18)>0)put16(wph_arena+WPH_TABLE,(uint16_t)(get16(wph_arena+WPH_TABLE)+1u));
            else put16(wph_arena+WPH_LIMITS,(uint16_t)(get16(wph_arena+WPH_LIMITS)+1u));
        }
        a[0]=b+WPH_PARTS;
    } else if(index==ES2_HOOK_INDEX_sphere_hits){
        BYTE *p=wph_arena+WPH_REGION;
        uint32_t count=n%11==0?0:n%37==0?(0x8000u|random32()):1+random32()%12;
        put16(p,(uint16_t)count);put32(p+2,b+WPH_SPHERES);
        if(real)for(unsigned k=0;k<16;++k){BYTE *s=wph_arena+WPH_SPHERES+8*k;
            for(unsigned j=0;j<3;++j)put16(s+2*j,(uint16_t)(random32()%4096u-2048u));put16(s+6,(uint16_t)(random32()%3000));}
        a[1]=n%3==0?0:b+WPH_OFFSET;
        if(real)for(unsigned j=0;j<3;++j)put16(wph_arena+WPH_OFFSET+2*j,(uint16_t)(random32()%2048u-1024u));
        wph_matrix();put32(mapped+0xa980c,b+WPH_MATRIX);
        put32(mapped+0xa9810,real?100+random32()%20000:random32()%3?random32()%40000:random32());
        put16(mapped+0xa9814,(uint16_t)(real?random32()%2000:random32()));
        a[0]=b+WPH_REGION;
    } else {
        BYTE *c=wph_arena+WPH_REGION;
        uint32_t range=n%7==0?0:n%7==1?0xffffffffu:real?100+random32()%20000:random32()%3?random32()%40000:random32();
        put32(mapped+0xa9810,range);put16(mapped+0xa9814,(uint16_t)(random32()%2?random32()%2000:random32()));
        uint32_t y;switch(random32()%5){case 0:y=range-1u;break;case 1:y=range;break;case 2:y=range+1u;break;
            case 3:y=random32()%(range|1u);break;default:y=random32();}
        put32(c+4,y);
        for(unsigned j=0;j<2;++j)put32(c+8*j,real||random32()%2?random32()%8192u-4096u:random32());
        wph_radius=random32()%3?random32()%3000:random32();
        a[0]=b+WPH_REGION;a[1]=(random32()&0xffff0000u)|(uint16_t)wph_radius;
        a[2]=n%3==0?random32()&0xffff0000u:n%3==1?1:random32()|1u;
    }
}
static int wph_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const wph_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,wph_calls);
    for(unsigned j=0;j<calls&&j<wph_calls&&expected;++j)if(memcmp(expected+j,wph_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(wph_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<wph_size;++j)if(wph_after[j]!=wph_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,wph_after[j],wph_arena[j]);break;}
    return 1;
}
static unsigned wph_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<wph_calls;++j){unsigned id=wph_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?wph_slots[id]==c->key:!wph_slots[id]&&wph_addresses[id]==c->key)++hits;}
    return hits;
}
static int wph_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[WPH_GLOBALS],after[WPH_GLOBALS],actual[WPH_GLOBALS];
    memcpy(wph_before,wph_arena,wph_size);wph_globals(before,0);
    wph_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=wph_calls;memcpy(wph_expected,wph_log,calls*sizeof(*wph_expected));
    memcpy(wph_after,wph_arena,wph_size);wph_globals(after,0);
    memcpy(wph_arena,wph_before,wph_size);wph_globals(before,1);wph_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[4];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);wph_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=wph_expected_hits(c+i))return fail("weapon helper callee counter delta");
    if(!wph_pair_equal(index,(uint32_t)pair[0],(uint32_t)(pair[0]>>32),(uint32_t)pair[1],(uint32_t)(pair[1]>>32))||calls!=wph_calls||
       memcmp(wph_expected,wph_log,calls*sizeof(*wph_expected))||memcmp(wph_after,wph_arena,wph_size)||memcmp(after,actual,WPH_GLOBALS)){
        for(unsigned j=0;j<WPH_GLOBALS;++j)if(after[j]!=actual[j])fprintf(stderr," global byte %u %02x/%02x\n",j,after[j],actual[j]);
        return wph_report("direct",n,pair,calls,wph_expected);
    }
    return 0;
}
static int wph_patch(BYTE saved[WPH_IDS][5],int active[WPH_IDS],unsigned which,int on){
    for(unsigned i=0;i<WPH_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!wph_addresses[i])continue;if(actor_patch(wph_addresses[i]-0x400000u,wph_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(wph_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static int wph_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;wph_build(index,n,a,r,0);
        BYTE before[WPH_GLOBALS],after[WPH_GLOBALS],actual[WPH_GLOBALS],expected_fpu[108],actual_fpu[108];
        memcpy(wph_before,wph_arena,wph_size);wph_globals(before,0);
        wph_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=wph_calls;memcpy(wph_expected,wph_log,calls*sizeof(*wph_expected));memcpy(wph_after,wph_arena,wph_size);wph_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("weapon helper poison install");
        memcpy(wph_arena,wph_before,wph_size);wph_globals(before,1);wph_reset();sg_poison_call(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);wph_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(!wph_pair_equal(index,expected_pair[0],expected_pair[1],actual_pair[0],actual_pair[1])||calls!=wph_calls||
           memcmp(wph_expected,wph_log,calls*sizeof(*wph_expected))||memcmp(wph_after,wph_arena,wph_size)||
           memcmp(after,actual,WPH_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=wph_report(fast?"installed fast":"installed preserving",n,pair,calls,wph_expected);
        if(!es2_hook_uninstall(&h))return fail("weapon helper poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, pair/trace/state\n",d->name,total);
    return status;
}
/* Real callees with the nested hooks in rotating original/replaced/compare
 * modes over a patterned stack: group_fire with the real V48 bodies 4111e2
 * and 40f504, weapon_step 40ef94 as V34 and the no-op V28 40e95c;
 * sphere_hits with the real 4801d4 (all mode-byte classes); sphere_test
 * with the real dist_octagonal. */
static int wph_real_pass(unsigned index){
    static const unsigned group_set[]={ES2_HOOK_INDEX_weapon_ready,ES2_HOOK_INDEX_weapon_step,ES2_HOOK_INDEX_timer_word_step};
    static const unsigned hits_set[]={ES2_HOOK_INDEX_sphere_test,ES2_HOOK_INDEX_dist_octagonal};
    static const unsigned test_set[]={ES2_HOOK_INDEX_dist_octagonal};
    const unsigned *nested=index==ES2_HOOK_INDEX_group_fire?group_set:index==ES2_HOOK_INDEX_sphere_hits?hits_set:test_set;
    unsigned nests=index==ES2_HOOK_INDEX_group_fire?3:index==ES2_HOOK_INDEX_sphere_hits?2:1;
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered[3]={0};
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;wph_build(index,n,a,r,1);
        BYTE before[WPH_GLOBALS],after[WPH_GLOBALS],actual[WPH_GLOBALS];
        memcpy(wph_before,wph_arena,wph_size);wph_globals(before,0);
        es2_hook hk[4];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<nests;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("weapon helper nested install");}
        wph_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        memcpy(wph_after,wph_arena,wph_size);wph_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+nests,d,&o,function_hook_extension(index)))return fail("weapon helper caller install");
        memcpy(wph_arena,wph_before,wph_size);wph_globals(before,1);wph_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);wph_globals(actual,0);
        if(!wph_pair_equal(index,expected_pair[0],expected_pair[1],actual_pair[0],actual_pair[1])||memcmp(wph_after,wph_arena,wph_size)||memcmp(after,actual,WPH_GLOBALS)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            for(unsigned j=0;j<WPH_GLOBALS;++j)if(after[j]!=actual[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,after[j],actual[j]);break;}
            fprintf(stderr,"real mode=%u\n",mode);wph_calls=0;status=wph_report("real",n,pair,0,NULL);
        }
        for(unsigned j=0;j<=nests;++j){if(j<nests)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("weapon helper nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("weapon helper nested uninstall");}
        wph_globals(before,1);++total;
    }
    for(unsigned j=0;j<nests&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, 31/a7 stack, pair and memory\n",d->name,total,nests);
    return status;
}
static int weapon_helpers_cases(unsigned index,unsigned limit){
    unsigned which=index==ES2_HOOK_INDEX_group_fire?0x4u:index==ES2_HOOK_INDEX_sphere_hits?0x30u:index==ES2_HOOK_INDEX_sphere_test?0x40u:0;
    BYTE saved[WPH_IDS][5],old_globals[WPH_GLOBALS];int active[WPH_IDS];
    wph_globals(old_globals,0);if(wph_patch(saved,active,which,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_WEAPON_HELPERS_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==4096)coverage_enable(0);
        uint32_t a[6],r[3];wph_build(index,n,a,r,0);status=wph_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=wph_installed_probes(index,64);
    if(wph_patch(saved,active,which,0))return 1;
    current_case_valid=0;
    if(!limit&&!status&&which)status=wph_real_pass(index);
    wph_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; return pair (stack residues by window), ordered calls with entry registers, frame payloads, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
