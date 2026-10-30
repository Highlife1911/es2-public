/* Own synthetic PE/stubs and independent mathematical models only.
 * No original binary, asset or instruction body is loaded or executed. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
static WCHAR config_path[MAX_PATH];
static unsigned events, checks, model_calls;
static void log_event(const char *event) { (void)event; ++events; }
#include "../rng_hooks.h"

#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"simulation line %d: %s\n",__LINE__,#x); return 1; } } while(0)
typedef uint32_t (__cdecl *call1)(uint32_t);
typedef uint32_t (__cdecl *call2)(uint32_t,uint32_t);
static BYTE *model_image;
static call2 nested_inner;
static uint32_t readn(const void *address, unsigned n) {
    const BYTE *p=address; uint32_t v=0;
    for(unsigned i=0;i<n;++i) v|=(uint32_t)p[i]<<(8*i);
    return v;
}
static void putn(void *address,uint32_t v,unsigned n) {
    BYTE *p=address; for(unsigned i=0;i<n;++i) p[i]=(BYTE)(v>>(8*i));
}
static int64_t signed32(uint32_t x) { return x<0x80000000u ? (int64_t)x : (int64_t)x-INT64_C(4294967296); }
static int64_t signed16(uint32_t x) { x&=65535; return x<32768 ? x : (int64_t)x-65536; }
static int64_t abs_model(uint32_t x) {
    int64_t s=signed32(x); if(s<0) s=-s;
    return s==INT64_C(2147483648) ? -s : s;
}
static int64_t floor_div(int64_t x,int64_t divisor) {
    return x>=0 ? x/divisor : -((-x+divisor-1)/divisor);
}
static uint32_t metric(uint32_t x,uint32_t y) {
    int64_t a=abs_model(x),b=abs_model(y);
    return (uint32_t)(a>b ? a+floor_div(b,2) : b+floor_div(a,2));
}
static uint32_t __cdecl model_delta(uint32_t x,uint32_t y) { ++model_calls; return metric(x,y); }
static uint32_t __cdecl model_vector(uint32_t a) {
    ++model_calls; const BYTE *p=(const BYTE *)(uintptr_t)a;
    return metric(readn(p,4),readn(p+4,4));
}
static uint32_t __cdecl model_points(uint32_t a,uint32_t b) {
    ++model_calls; const BYTE *p=(const BYTE *)(uintptr_t)a,*q=(const BYTE *)(uintptr_t)b;
    uint32_t x=readn(p,4)-readn(q,4),y=readn(p+4,4)-readn(q+4,4);
    return nested_inner ? nested_inner(x,y) : metric(x,y);
}
static uint32_t __cdecl model_gate(uint32_t a,uint32_t b) {
    ++model_calls; const BYTE *p=(const BYTE *)(uintptr_t)a,*q=(const BYTE *)(uintptr_t)b;
    int64_t values[3];
    for(unsigned i=0;i<3;++i) values[i]=abs_model(readn(p+4*i,4)-readn(q+4*i,4));
    int64_t low=values[0],high=values[0],sum=values[0]+values[1]+values[2];
    for(unsigned i=1;i<3;++i) { if(values[i]<low) low=values[i]; if(values[i]>high) high=values[i]; }
    int64_t middle=sum-low-high;
    return (uint32_t)(high+floor_div(middle,4)+floor_div(middle,16)+floor_div(middle,32)+floor_div(low,4));
}
static uint32_t __cdecl model_scale(uint32_t reference,uint32_t elapsed) {
    ++model_calls;
    int64_t q=signed16(elapsed)*256/signed16(reference), low=signed16((uint32_t)q);
    if(low<64) low=64; if(low>450) low=450;
    return ((uint32_t)q&0xffff0000u)|(uint32_t)low;
}
static uint32_t __cdecl model_word(uint32_t a) {
    ++model_calls; BYTE *p=(BYTE *)(uintptr_t)a;
    uint32_t value=(uint32_t)((int64_t)readn(p+1,2)-signed16(readn(model_image+0xd3b64,2)))&65535;
    if(value>=32768) value=0;
    putn(p+1,value,2); return (a&0xffff0000u)|value;
}
static uint32_t __cdecl model_dword(uint32_t a) {
    ++model_calls; BYTE *p=(BYTE *)(uintptr_t)a;
    uint32_t value=(uint32_t)((int64_t)readn(p+1,4)-signed16(readn(model_image+0xd3b64,2)));
    if(value>=0x80000000u) value=0;
    putn(p+1,value,4); return value;
}
static uint32_t __cdecl model_arm(uint32_t a,uint32_t duration) {
    ++model_calls; putn((BYTE *)(uintptr_t)a+1,(uint32_t)(signed16(duration)*2048),4); return a;
}

static BYTE *make_image(void) {
    BYTE *image=VirtualAlloc(NULL,RNG_HOOK_IMAGE_SIZE,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(!image) return NULL;
    putn(image,0x5a4d,2); putn(image+0x3c,0x80,4); putn(image+0x80,0x4550,4);
    putn(image+0x84,0x14c,2); putn(image+0x98,0x10b,2);
    putn(image+0x98+28,(uint32_t)(uintptr_t)image,4); putn(image+0x98+56,RNG_HOOK_IMAGE_SIZE,4);
    const uintptr_t models[]={(uintptr_t)model_points,(uintptr_t)model_vector,(uintptr_t)model_delta,
        (uintptr_t)model_delta,(uintptr_t)model_gate,(uintptr_t)model_scale,(uintptr_t)model_word,
        (uintptr_t)model_dword,(uintptr_t)model_arm};
    for(unsigned i=2;i<11;++i) {
        const es2_hook_descriptor *d=function_hook_descriptor(i);
        BYTE *p=image+d->rva;
        /* Emit only the short validated prologue form, then our own epilogue
         * and tail jump to a C model. No original function body is a fixture. */
        memcpy(p,d->expected,d->expected_size); p+=d->expected_size;
        if(i==3) *p++=0x5b; /* vector prologue saves EBX */
        *p++=0xc9; *p++=0xe9;
        putn(p,(uint32_t)models[i-2]-(uint32_t)(uintptr_t)(p+4),4);
    }
    DWORD previous;
    if(!VirtualProtect(image,RNG_HOOK_IMAGE_SIZE,PAGE_EXECUTE_READWRITE,&previous) ||
       !FlushInstructionCache(GetCurrentProcess(),image,RNG_HOOK_IMAGE_SIZE)) return NULL;
    model_image=image;
    for(unsigned i=0;i<TIMING_HOOK_COUNT;++i) timing_hook_descriptors[i].user=image;
    return image;
}
static es2_hook_options options(BYTE *image,es2_hook_mode mode) {
    es2_hook_options o={0}; o.image=image; o.expected_base=(uintptr_t)image;
    o.expected_image_size=RNG_HOOK_IMAGE_SIZE; o.mode=mode; return o;
}
static uint32_t invoke(BYTE *image,unsigned index,uint32_t a,uint32_t b) {
    const es2_hook_descriptor *d=function_hook_descriptor(index);
    if(d->arg_count==1) return ((call1)(uintptr_t)(image+d->rva))(a);
    return ((call2)(uintptr_t)(image+d->rva))(a,b);
}
static int descriptors_case(es2_hook_mode mode) {
    BYTE *image=make_image(); CHECK(image);
    es2_hook_options o=options(image,mode);
    for(unsigned i=2;i<11;++i) CHECK(es2_hook_install(&rng_hooks[i],function_hook_descriptor(i),&o));
    static const uint32_t inputs[]={0,1,2,31,32,0x7fffffff,0x80000000,0x80000001,0xffffffff};
    for(unsigned k=0;k<sizeof(inputs)/sizeof(inputs[0]);++k) {
        BYTE a[14],b[14],timer[8],expected[8]; memset(a,0x55,sizeof(a)); memset(b,0xaa,sizeof(b));
        putn(a+1,inputs[k],4); putn(a+5,inputs[(k+1)%9],4); putn(a+9,inputs[(k+2)%9],4);
        putn(b+1,inputs[(k+3)%9],4); putn(b+5,0,4); putn(b+9,0,4);
        uint32_t pa=(uint32_t)(uintptr_t)(a+1),pb=(uint32_t)(uintptr_t)(b+1);
        uint32_t results[]={model_points(pa,pb),model_vector(pa),model_delta(inputs[k],inputs[(k+1)%9]),
            model_delta(inputs[k],inputs[(k+1)%9]),model_gate(pa,pb),model_scale(125,inputs[k])};
        unsigned before=model_calls;
        for(unsigned i=2;i<=7;++i) {
            uint32_t x=i==7 ? 125 : (i==4 || i==5) ? inputs[k] : pa;
            uint32_t y=i==7 ? inputs[k] : (i==4 || i==5) ? inputs[(k+1)%9] : pb;
            CHECK(invoke(image,i,x,y)==results[i-2]);
        }
        CHECK(model_calls==before+(mode==ES2_HOOK_COMPARE ? 6u : 0u));
        putn(image+0xd3b64,inputs[(k+1)%9],2);
        for(unsigned i=8;i<11;++i) {
            memset(timer,0x93,sizeof(timer)); putn(timer+2,inputs[k],4); memcpy(expected,timer,8);
            uint32_t pt=(uint32_t)(uintptr_t)(timer+1),pe=(uint32_t)(uintptr_t)(expected+1);
            uint32_t wanted=i==8 ? model_word(pe) : i==9 ? model_dword(pe) : model_arm(pe,inputs[k]);
            if(i==8) wanted=(pt&0xffff0000u)|(wanted&65535);
            if(i==10) wanted=pt;
            before=model_calls;
            CHECK(invoke(image,i,pt,inputs[k])==wanted);
            CHECK(!memcmp(timer,expected,8));
            CHECK(model_calls==before+(mode==ES2_HOOK_COMPARE ? 1u : 0u));
        }
    }
    for(unsigned i=2;i<11;++i) {
        CHECK(rng_hooks[i].calls==9 && !rng_hooks[i].mismatches && !rng_hooks[i].skipped && !rng_hooks[i].bypassed);
        CHECK(es2_hook_uninstall(&rng_hooks[i]));
        CHECK(!memcmp(image+function_hook_descriptor(i)->rva,function_hook_descriptor(i)->expected,function_hook_descriptor(i)->expected_size));
    }
    CHECK(VirtualFree(image,0,MEM_RELEASE)); return 0;
}
static uint32_t __cdecl wrong_inner(const uint32_t *a,void *user) { (void)a;(void)user; return 0x12345678; }
static int nested_case(es2_hook_mode inner_mode) {
    BYTE *image=make_image(); CHECK(image);
    es2_hook_descriptor inner=geometry_hook_descriptors[3]; inner.replacement=wrong_inner;
    es2_hook_options outer_options=options(image,ES2_HOOK_COMPARE),inner_options=options(image,inner_mode);
    CHECK(es2_hook_install(&rng_hooks[5],&inner,&inner_options));
    CHECK(es2_hook_install(&rng_hooks[2],&geometry_hook_descriptors[0],&outer_options));
    nested_inner=(call2)(uintptr_t)(image+inner.rva);
    BYTE a[12]={0},b[12]={0}; putn(a,3,4);putn(a+4,4,4);
    for(unsigned i=0;i<4;++i) CHECK(invoke(image,2,(uint32_t)(uintptr_t)a,(uint32_t)(uintptr_t)b)==5);
    CHECK(rng_hooks[2].calls==4 && !rng_hooks[2].mismatches && !rng_hooks[2].skipped);
    CHECK(rng_hooks[5].calls==4 && rng_hooks[5].bypassed==4 && !rng_hooks[5].mismatches && !rng_hooks[5].skipped);
    CHECK(nested_inner(3,4)==(inner_mode==ES2_HOOK_COMPARE ? 5u : 0x12345678u));
    CHECK(rng_hooks[5].mismatches==(inner_mode==ES2_HOOK_COMPARE ? 1u : 0u));
    CHECK(es2_hook_uninstall(&rng_hooks[2])); CHECK(es2_hook_uninstall(&rng_hooks[5]));
    CHECK(VirtualFree(image,0,MEM_RELEASE)); return 0;
}
/* Bazel test sharding (shard_count of the configuration test in BUILD.bazel): hook i-2 of the loop below
 * belongs to shard (i-2) % total, the closing part to shard zero. Without the variables one shard does all. */
static unsigned shard_total=1,shard_which;
static int configure_shard(void) {
    const char *total=getenv("TEST_TOTAL_SHARDS"),*index=getenv("TEST_SHARD_INDEX"),*status=getenv("TEST_SHARD_STATUS_FILE");
    if(!total && !index) return 0;
    if(!total || !index) return 1;
    char *end; unsigned long t=strtoul(total,&end,10); if(*end || !t) return 1;
    unsigned long w=strtoul(index,&end,10); if(*end || w>=t) return 1;
    if(status && *status) { FILE *file=fopen(status,"wb"); if(!file) return 1; fclose(file); }
    shard_total=(unsigned)t; shard_which=(unsigned)w; return 0;
}
static int configuration_case(void) {
    CHECK(install_rng_hooks()); CHECK(!events && !rng_hooks_configured && !rng_hook_log.file);
    for(unsigned i=2;i<FUNCTION_HOOK_COUNT;++i) {
        if((i-2)%shard_total!=shard_which) continue;
        WCHAR key[64]; const char *name=function_hook_descriptor(i)->name;
        for(unsigned j=0;j<=strlen(name);++j) key[j]=(WCHAR)name[j];
        CHECK(WritePrivateProfileStringW(L"hooks",key,L"compare",config_path));
        es2_hook_mode mode; CHECK(es2_hook_read_mode(config_path,name,&mode) && mode==ES2_HOOK_COMPARE);
        CHECK(!install_rng_hooks()); /* Fixture identity is not DBSIM.EXE. */
        CHECK(!rng_hook_log.file && !rng_hooks[i].installed);
        CHECK(WritePrivateProfileStringW(L"hooks",key,L"original",config_path));
    }
    if(shard_which) return 0;
    CHECK(install_rng_hooks());
    rng_hooks_configured=TRUE;
    static char output[262144]; /* TICK_LOG_LINE_MAX (tick_log.h); 90 hooks outgrew 16 KiB, 163 hooks 32 KiB, 271 hooks 64 KiB, 472 hooks 128 KiB */
    for(unsigned i=0;i<FUNCTION_HOOK_COUNT;++i) {
        rng_hooks[i].options.mode=ES2_HOOK_REPLACED;
        rng_hooks[i].calls=UINT64_MAX; rng_hooks[i].mismatches=0;
        rng_hooks[i].skipped=0; rng_hooks[i].bypassed=UINT64_MAX;
    }
    CHECK(rng_hooks_tick_fields(output,sizeof(output))==(int)strlen(output));
    for(unsigned i=0;i<FUNCTION_HOOK_COUNT;++i) CHECK(strstr(output,function_hook_descriptor(i)->name));
    CHECK(strlen(output)+2505<sizeof(output)); /* full worst-case tick is tested by path_counters_bound_test */
    rng_hooks_shutdown(); return 0;
}
static int comparators_case(void) {
    BYTE real[5]={0},copy[5]={0};
    es2_hook_comparison v={0}; v.span_count=1;v.original[0]=real+1;v.replacement[0]=copy+1;v.sizes[0]=4;
    v.original_result=(uint32_t)(uintptr_t)real;v.replacement_result=(uint32_t)(uintptr_t)copy;
    CHECK(timing_arm_equal(&v,NULL)); ++v.replacement_result; CHECK(!timing_arm_equal(&v,NULL));
    v.original_result=((uint32_t)(uintptr_t)real&0xffff0000u)|12;
    v.replacement_result=((uint32_t)(uintptr_t)copy&0xffff0000u)|12;
    CHECK(timing_word_equal(&v,NULL));v.replacement_result^=0x10000;CHECK(!timing_word_equal(&v,NULL));
    v.replacement_result^=0x10000;v.replacement_result^=1;CHECK(!timing_word_equal(&v,NULL));
    v.replacement_result^=1;copy[1]=1;CHECK(!timing_word_equal(&v,NULL));return 0;
}
int wmain(int argc,WCHAR **argv) {
    if(argc!=2 && argc!=3) return 2;
    WCHAR dir[MAX_PATH];
    if(argc==3) { if(wcslen(argv[2])>=MAX_PATH) return 2;wcscpy(dir,argv[2]); }
    else if(!GetEnvironmentVariableW(L"TEST_TMPDIR",dir,MAX_PATH)) return 2;
    if(swprintf(config_path,MAX_PATH,L"%ls\\simulation-%lu.ini",dir,(unsigned long)GetCurrentProcessId())<0) return 2;
    int result;
    if(configure_shard()) return 2;
    if(!wcscmp(argv[1],L"compare")) result=descriptors_case(ES2_HOOK_COMPARE);
    else if(!wcscmp(argv[1],L"replaced")) result=descriptors_case(ES2_HOOK_REPLACED);
    else if(!wcscmp(argv[1],L"nested_compare")) result=nested_case(ES2_HOOK_COMPARE);
    else if(!wcscmp(argv[1],L"nested_replaced")) result=nested_case(ES2_HOOK_REPLACED);
    else if(!wcscmp(argv[1],L"configuration")) result=configuration_case();
    else if(!wcscmp(argv[1],L"comparators")) result=comparators_case();
    else return 2;
    if(!result) printf("simulation hooks: %u checks passed\n",checks);
    return result;
}
