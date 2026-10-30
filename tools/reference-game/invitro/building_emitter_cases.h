/* Synthetic building graph. All payloads are generated, not extracted. */
static uint32_t be_tables[3][32],be_distance,be_pick,be_speed,be_angle[2],be_flags;
static unsigned be_random_calls,be_padding;
static const uint32_t be_addresses[]={0x403ca8,0x403914,0x467a48,0x411fa0,0x49288c,0x40ffb0,0x4929fc,0x403eec,0x492ee0,0x47ebb8,0x47fa20,0x47f9a0,0x4801d4,0x40a9c4,0x40b43c};
static const unsigned be_argc[]={1,1,1,3,2,1,3,2,1,2,3,2,3,5,4,3,1,2};
__attribute__((used,noinline)) static uint64_t __cdecl be_stub_impl(unsigned id,const uint32_t *a) {
    if(r2_calls>=40){fail("building call overflow");ExitProcess(1);}
    r2_record *v=r2_log+r2_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,4*be_argc[id]);
    uint32_t b=(uint32_t)(uintptr_t)r2_arena;
    v->state[0]=get32(r2_arena+0x1a4);v->state[1]=get16(r2_arena+0x1f7);v->state[2]=get16(r2_arena+0x21b);
    v->state[3]=get16(r2_arena+0x5a2);v->state[4]=get16(r2_arena+0x7a2);v->state[5]=r2_arena[0x9d];
    v->state[6]=get32(r2_arena+0x212);v->state[7]=get16(r2_arena+0x32);
    (void)es2_rng_next(r2_arena+0xe80+0x80*(id&1));uint32_t eax=r2_next(),edx=r2_next();r2_ecx=r2_next();
    if(id==2){unsigned bit=a[0]==b+0x21d?8:a[0]==b+0x1f6?9:a[0]==b+0x218?10:11;eax=(eax&0xffff0000u)|((be_flags>>bit)&1);}
    if(id==3)eax=be_pick;
    if(id==4){eax=be_distance;if((be_flags&0x40000u))put32(r2_arena+0x1a4,0);}
    if(id==5)eax=b+0xd00;
    if(id==6||id==7){unsigned arg=id==6?0:1;for(unsigned j=0;j<3;++j)v->input[j]=get32((BYTE *)(uintptr_t)a[arg]+4*j);v->args[arg]=0;
        if(id==6)for(unsigned j=0;j<3;++j)put32((BYTE *)(uintptr_t)a[0]+4*j,v->input[j]+j+1);else eax=b+0xd20;}
    if(id==8){eax=(eax&~31u)|((be_flags>>(12+be_random_calls++))&1);}
    if(id==9){for(unsigned j=0;j<3;++j)v->input[j]=get16((BYTE *)(uintptr_t)a[0]+2*j);for(unsigned j=0;j<18;++j)((BYTE *)(uintptr_t)a[1])[j]=(BYTE)r2_next();((BYTE *)(uintptr_t)a[1])[18]=3;}
    if(id==10){for(unsigned j=0;j<8;++j){v->input[j]=get32((BYTE *)(uintptr_t)a[0]+4*j);v->input[8+j]=get32((BYTE *)(uintptr_t)a[1]+4*j);}v->args[2]=0;
        be_padding=((BYTE *)(uintptr_t)a[2])[19];v->input[16]=be_padding;
        for(unsigned j=0;j<32;++j)if(j!=19)((BYTE *)(uintptr_t)a[2])[j]=(BYTE)r2_next();}
    if(id==11){for(unsigned j=0;j<8;++j)v->input[j]=get32((BYTE *)(uintptr_t)a[0]+4*j);v->args[0]=v->args[1]=0;for(unsigned j=0;j<3;++j)put16((BYTE *)(uintptr_t)a[1]+2*j,(uint16_t)r2_next());}
    if(id==12){for(unsigned j=0;j<3;++j)v->input[j]=get16((BYTE *)(uintptr_t)a[0]+2*j);for(unsigned j=0;j<8;++j)v->input[j+3]=get32((BYTE *)(uintptr_t)a[1]+4*j);
        v->args[0]=v->args[1]=v->args[2]=0;for(unsigned j=0;j<3;++j)put32((BYTE *)(uintptr_t)a[2]+4*j,r2_next());}
    if(id==13||id==14){for(unsigned j=0;j<3;++j){v->input[j]=get32((BYTE *)(uintptr_t)a[1]+4*j);v->input[j+3]=get16((BYTE *)(uintptr_t)a[2]+2*j);}v->args[1]=v->args[2]=0;
        if(be_flags&0x20000u)put16(r2_arena+0x32,0);eax=be_flags&0x80000u?0:eax;}
    if(id==15){v->args[1]=v->args[2]=0;for(unsigned j=0;j<3;++j){put16((BYTE *)(uintptr_t)a[1]+2*j,(uint16_t)r2_next());put16((BYTE *)(uintptr_t)a[2]+2*j,(uint16_t)r2_next());}
        if(be_flags&0x10000u){put32(r2_arena+0x1a4,b+0x600);put16(r2_arena+0x82e,get16(r2_arena+0x82e)==2?1:2);}}
    if(id==16)eax=(eax&0xffff0000u)|(uint16_t)be_speed;
    if(id==17){unsigned state=(be_flags>>14)%4;eax=state?b+0xd40:0;put16(r2_arena+0xd44,state==1?0xffff:state==2?0:1);
        if(be_flags&0x40000u)put32(r2_arena+0x916,b+0xc40);}
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define BE_STUB(n) __attribute__((naked)) static void be_stub_##n(void) {__asm__ volatile( \
    "leal 4(%esp),%eax\n\tpushl %eax\n\tpushl $" #n "\n\tcalll _be_stub_impl\n\taddl $8,%esp\n\tmovl _r2_ecx,%ecx\n\tret\n\t");}
BE_STUB(0) BE_STUB(1) BE_STUB(2) BE_STUB(3) BE_STUB(4) BE_STUB(5) BE_STUB(6) BE_STUB(7) BE_STUB(8)
BE_STUB(9) BE_STUB(10) BE_STUB(11) BE_STUB(12) BE_STUB(13) BE_STUB(14) BE_STUB(15) BE_STUB(16) BE_STUB(17)
#undef BE_STUB
static void *be_stubs[]={be_stub_0,be_stub_1,be_stub_2,be_stub_3,be_stub_4,be_stub_5,be_stub_6,be_stub_7,be_stub_8,be_stub_9,be_stub_10,be_stub_11,be_stub_12,be_stub_13,be_stub_14};
static void be_build(unsigned n,uint32_t a[6],uint32_t r[3]) {
    static const uint32_t distances[]={0,39999,40000,40001,59999,60000,60001,0xffffffffu,0x80000000u,0x7fffffffu};
    static const uint16_t angles[]={0,999,1000,64536,64535,32768};
    static const uint16_t speed[]={0,1,0xffff,0x7fff,0x8000};
    be_flags=n<65536?(n | (n%23==0?0x40000u:0u) | (n%29==0?0x10000u:0u) | (n%31==0?0x20000u:0u) | (n%41==0?0x80000u:0u)):random32();r2_case=n;r2_seed=random32();random_bytes(r2_arena,sizeof(r2_arena));memset(a,0,24);
    uint32_t b=(uint32_t)(uintptr_t)r2_arena;a[0]=b;for(unsigned j=0;j<3;++j)r[j]=random32();
    for(unsigned j=0;j<3;++j){be_tables[j][12]=(uint32_t)(uintptr_t)be_stub_15;be_tables[j][14]=(uint32_t)(uintptr_t)be_stub_16;be_tables[j][8]=(uint32_t)(uintptr_t)be_stub_17;}
    put32(r2_arena,(uint32_t)(uintptr_t)be_tables[0]);put32(r2_arena+0x400,(uint32_t)(uintptr_t)be_tables[1]);put32(r2_arena+0x600,(uint32_t)(uintptr_t)be_tables[2]);
    r2_arena[0x99]=n%97==0;put32(r2_arena+0x1a4,n%3?b+(n%3==1?0x400:0x600):0);be_pick=(n/3)%3?b+((n/3)%3==1?0x400:0x600):0;
    put32(r2_arena+0x1f2,b+0x800);put32(r2_arena+0x34,b+0x900);put32(r2_arena+0x904,b+0xa00);put32(r2_arena+0x908,b+0xb00);put32(r2_arena+0x916,b+0xc00);
    put32(r2_arena+0xa00,(uint32_t)(uintptr_t)be_tables[2]);put32(r2_arena+0xa20,b+0xb40);
    put16(r2_arena+0x824,n%8);put16(r2_arena+0x826,(uint16_t)random32());put16(r2_arena+0x82e,(n/7)%3);
    for(unsigned j=0;j<8;++j){put16(r2_arena+0xb00+2*j,(uint16_t)random32());put16(r2_arena+0xb40+2*j,(uint16_t)(1+(n+j)%65535));}
    put16(r2_arena+0x1f7,(n/13)%2);put16(r2_arena+0x21b,(n/17)%2);put16(r2_arena+0x32,(n/19)%2);
    be_distance=distances[n%10];be_speed=speed[(n/5)%5];put16(r2_arena+0xd0a,speed[(n/11)%5]);
    be_angle[0]=angles[(n/31)%6];be_angle[1]=angles[(n/37)%6];put16(r2_arena+0xd20,(uint16_t)be_angle[0]);put16(r2_arena+0xd24,(uint16_t)be_angle[1]);
    r2_arena[0xef0]=r2_arena[0xf70]=0;r2_arena[0xef1]=r2_arena[0xf71]=17;
}
static int be_compare(unsigned n,int installed,uint32_t poison) {
    uint32_t a[6],r[3];be_build(n,a,r);memcpy(r2_before,r2_arena,sizeof(r2_arena));r2_calls=0;r2_stream=r2_seed;be_random_calls=0;
    BYTE expected_fpu[108],actual_fpu[108];
    uint32_t pair_a[2],pair_b[2];uint64_t expected_pair;
    if(installed){es2_probe_call(mapped+0x4100,a[0],0,0,poison,pair_a);expected_pair=(uint64_t)pair_a[1]<<32|pair_a[0];}
    else expected_pair=weapons_original(ES2_HOOK_INDEX_turret,a,r);
    es2_hook_fpu_save(expected_fpu);
    unsigned calls=r2_calls;r2_record expected[40];memcpy(expected,r2_log,calls*sizeof(*expected));memcpy(r2_after,r2_arena,sizeof(r2_arena));r[2]=be_padding;
    memcpy(r2_arena,r2_before,sizeof(r2_arena));r2_calls=0;r2_stream=r2_seed;be_random_calls=0;
    const es2_hook_descriptor *d=function_hook_descriptor(ES2_HOOK_INDEX_turret);const es2_hook_extension *x=function_hook_extension(ES2_HOOK_INDEX_turret);
    unsigned count;callee_counter *c=weapons_graph_counters(ES2_HOOK_INDEX_turret,&count);uint64_t before[32];for(unsigned j=0;j<count;++j)before[j]=c[j].count;
    uint64_t actual;
    if(installed){es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        if(!weapons_install_hook(&h,d,&o,x))return fail("building install");es2_probe_call(mapped+0x4100,a[0],0,0,poison,pair_b);actual=(uint64_t)pair_b[1]<<32|pair_b[0];
        if(!es2_hook_uninstall(&h))return fail("building uninstall");if(h.calls!=1)return fail("building installed count");}
    else actual=x->replacement_context(a,d->user,r);
    es2_hook_fpu_save(actual_fpu);
    if(memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80))return fail("turret full x87 state");
    if(n<1024)for(unsigned j=0;j<count;++j){unsigned expected_count=0;for(unsigned k=0;k<r2_calls;++k){unsigned id=r2_log[k].id;uint32_t key=id<15?be_addresses[id]:id==15?0x30:id==16?0x38:0x20;if(c[j].key==key)++expected_count;}
        if(c[j].count-before[j]!=expected_count)return fail("building counter delta");}
    if(expected_pair!=actual||calls!=r2_calls||memcmp(expected,r2_log,calls*sizeof(*expected))||memcmp(r2_after,r2_arena,sizeof(r2_arena))){
        fprintf(stderr,"FAIL: turret case %u pair %016llx/%016llx calls%u/%u\n",n,(unsigned long long)expected_pair,(unsigned long long)actual,calls,r2_calls);
        for(unsigned k=0;k<calls&&k<r2_calls;++k)if(memcmp(expected+k,r2_log+k,sizeof(*expected))){uint32_t *e=(uint32_t *)(expected+k),*f=(uint32_t *)(r2_log+k);for(unsigned j=0;j<sizeof(*expected)/4;++j)if(e[j]!=f[j])fprintf(stderr," call%u id%u word%u %x/%x\n",k,expected[k].id,j,e[j],f[j]);break;}
        for(unsigned j=0;j<sizeof(r2_arena);++j)if(r2_after[j]!=r2_arena[j]){fprintf(stderr," arena %x %x/%x\n",j,r2_after[j],r2_arena[j]);break;}return 1;}
    return 0;
}
#include "building_emitter_modes.h"
static int turret_cases(unsigned limit) {
    BYTE saved[15][5];for(unsigned i=0;i<15;++i)if(actor_patch(be_addresses[i]-0x400000u,be_stubs[i],saved[i],TRUE))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_BUILDING_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==65536)coverage_enable(0);status=be_compare(n,0,0);}
    if(!limit)coverage_enable(0);
    BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);__asm__ volatile("fninit; fld1; fldpi");
    if(!limit&&!status)for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned p=0;p<2&&!status;++p){es2_hook_set_fast_path(fast);for(unsigned n=1;n<513&&!status;++n)status=be_compare(n,1,p?0xa7a7a7a7u:0x31313131u);}
    __asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!limit&&!status)status=building_emit_modes(saved);
    es2_hook_set_fast_path(TRUE);for(unsigned i=0;i<15;++i)if(actor_patch(be_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;current_case_valid=0;
    if(!status)printf("PASS: turret %u cases; full pair/x87, ordered calls/slots, callbacks, state/RNG and 2048 installed poison cases\n",cases);return status;
}
