/* Entirely synthetic PE image and code. */
#include "hook/hook.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr,"extended line %d: %s\n",__LINE__,#c); return 1; } } while (0)
static unsigned checks, kind;
static unsigned wide_count;
static BYTE *image;
static const BYTE signature[] = {0x90,0x90,0x90,0x90,0x90};
static const BYTE lengths[] = {1,1,1,1,1};
static void put32(BYTE *p, uint32_t x) { for (unsigned i=0;i<4;++i) p[i]=(BYTE)(x>>(8*i)); }
static uint32_t word(const BYTE *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static uint32_t __cdecl sum(const uint32_t *a, void *u) { (void)u; return a[0]+a[1]+a[2]+a[3]+kind; }
static uint32_t __cdecl weighted(const uint32_t *a,void *u) {
    (void)u; uint32_t value=0;
    for(unsigned i=0;i<wide_count;++i) value=value*17+a[i];
    return value+kind;
}
static uint32_t __cdecl chained(const uint32_t *a,void *u) {
    (void)u; BYTE *p=(BYTE *)(uintptr_t)a[0];
    for(unsigned i=0;i<wide_count;++i) p=(BYTE *)(uintptr_t)word(p);
    if(kind) {
        BYTE *slot=(BYTE *)(uintptr_t)a[0];
        for(unsigned hop=1;hop<kind;++hop)slot=(BYTE *)(uintptr_t)word(slot);
        put32(slot,0);
    }
    return word(p);
}
static uint64_t __cdecl pair(const uint32_t *a, void *u) { (void)u; return (uint64_t)a[0]*a[1]+((uint64_t)kind<<32); }
static uint64_t __cdecl context_pair(const uint32_t *a,void *u,const uint32_t *regs) {
    (void)a;(void)u;return ((uint64_t)(regs[1]^kind)<<32)|(regs[0]+regs[2]);
}
static long double __cdecl floating(const uint32_t *a, void *u) {
    (void)u; int32_t n; memcpy(&n,a,4);
    if (kind==2) { unsigned short cw=0x027f; __asm__ volatile("fldcw %0"::"m"(cw)); }
    if (kind==3) __asm__ volatile("fld1");
    if (kind==4) __asm__ volatile("fldz; ftst; fstp %st(0)");
    return (long double)n+(kind==1 ? 1.0L : 0.0L);
}
static uint32_t __cdecl global(const uint32_t *a, void *u) {
    (void)u; uint32_t x=word(image+0x3000)+a[0]+kind; put32(image+0x3000,x); return x;
}
static uint32_t __cdecl indirect(const uint32_t *a, void *u) {
    (void)u; BYTE *p=(BYTE *)(uintptr_t)word((BYTE *)(uintptr_t)a[0]); uint32_t n=word(p);
    if (kind==2) put32((BYTE *)(uintptr_t)a[0],0);
    else if (kind) put32(p,n+kind);
    return n;
}
static uint32_t __cdecl nested(const uint32_t *a, void *u) {
    (void)u; typedef uint32_t (__cdecl *f)(uint32_t,uint32_t,uint32_t,uint32_t);
    return ((f)(image+0x1100))(a[0],a[1],a[2],a[3]);
}
static es2_hook_descriptor desc(unsigned rva, unsigned argc, es2_hook_replacement fn) {
    es2_hook_descriptor d={"extended",rva,signature,5,5,lengths,5,argc,fn,NULL,NULL,NULL,0}; return d;
}
static es2_hook_options options(void) {
    es2_hook_options o={0}; o.image=image; o.expected_base=(uintptr_t)image;
    o.expected_image_size=0x4000; o.mode=ES2_HOOK_COMPARE; return o;
}
static void code(unsigned rva,const BYTE *tail,size_t n) {
    memcpy(image+rva,signature,5); memcpy(image+rva+5,tail,n);
    FlushInstructionCache(GetCurrentProcess(),image+rva,n+5);
}
static es2_hook_result invoke(unsigned rva,es2_hook_abi abi,unsigned argc,const uint32_t *args) {
    BYTE initial[108]; unsigned short cw=0x1332;
    __asm__ volatile("fninit; fldcw %0"::"m"(cw)); es2_hook_fpu_save(initial);
    es2_hook_result r; es2_hook_call c={image+rva,abi,argc,args,initial,&r,NULL};
    es2_hook_machine_call(&c); __asm__ volatile("fninit"); return r;
}
static BOOL WINAPI reject_flush(HANDLE p,LPCVOID a,SIZE_T n) { (void)p;(void)a;(void)n; return FALSE; }

int main(void) {
    image=VirtualAlloc(NULL,0x4000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE); CHECK(image);
    put32(image,0x5a4d); put32(image+0x3c,0x80); put32(image+0x80,0x4550);
    image[0x84]=0x4c; image[0x85]=1; image[0x98]=0x0b; image[0x99]=1;
    put32(image+0xb4,(uint32_t)(uintptr_t)image); put32(image+0xd0,0x4000);
    es2_hook_options o=options();
    const BYTE stack_sum[]={0x8b,0x44,0x24,4,0x03,0x44,0x24,8,0x03,0x44,0x24,12,0x03,0x44,0x24,16,0xc2,16,0};
    const BYTE reg_sum[]={0x01,0xd0,0x01,0xc8,0x03,0x44,0x24,4,0xc2,4,0};
    const uint32_t a[ES2_HOOK_MAX_ARGS]={0xfffffffeu,7,11,19,23,29};
    for (unsigned abi=1;abi<=2;++abi) {
        code(0x1000,abi==1?stack_sum:reg_sum,abi==1?sizeof(stack_sum):sizeof(reg_sum));
        es2_hook h={0}; es2_hook_descriptor d=desc(0x1000,4,sum); es2_hook_extension e={0}; e.abi=(es2_hook_abi)abi;
        CHECK(es2_hook_install_ex(&h,&d,&o,&e));
        for (kind=0;kind<=1;++kind) for (unsigned i=0;i<100;++i) CHECK(invoke(0x1000,e.abi,4,a).eax==35);
        CHECK(h.calls==200 && h.mismatches==100 && !h.skipped); CHECK(es2_hook_uninstall(&h));
    }
    kind=0;
    /* Noncommutative arithmetic detects incorrect ordering of stack slots.
     * Each own machine-code fixture also verifies the incoming stack directly
     * through the machine bridge, then through original/replaced/compare. */
    for(wide_count=5;wide_count<=6;++wide_count) for(unsigned abi=0;abi<=2;++abi) {
        BYTE body[128]; unsigned pos=0;
        if(abi==2) {
            body[pos++]=0x6b;body[pos++]=0xc0;body[pos++]=17; /* imul eax,17 */
            body[pos++]=0x01;body[pos++]=0xd0;
            body[pos++]=0x6b;body[pos++]=0xc0;body[pos++]=17;
            body[pos++]=0x01;body[pos++]=0xc8;
        } else { body[pos++]=0x8b;body[pos++]=0x44;body[pos++]=0x24;body[pos++]=4; }
        for(unsigned i=abi==2?3:1;i<wide_count;++i) {
            body[pos++]=0x6b;body[pos++]=0xc0;body[pos++]=17;
            body[pos++]=0x03;body[pos++]=0x44;body[pos++]=0x24;
            body[pos++]=(BYTE)(4*(abi==2?wide_count-i:i+1));
        }
        body[pos++]=abi?0xc2:0xc3;
        if(abi) { body[pos++]=(BYTE)(4*(wide_count-(abi==2?3:0)));body[pos++]=0; }
        code(0x1000,body,pos);uint32_t expected=weighted(a,NULL);
        CHECK(invoke(0x1000,(es2_hook_abi)abi,wide_count,a).eax==expected);
        for(unsigned mode=0;mode<=2;++mode) {
            es2_hook wide={0};es2_hook_descriptor wd=desc(0x1000,wide_count,weighted);es2_hook_extension we={0};we.abi=(es2_hook_abi)abi;
            es2_hook_options wo=options();wo.mode=(es2_hook_mode)mode;
            CHECK(es2_hook_install_ex(&wide,&wd,&wo,&we));
            for(unsigned repeat=0;repeat<16;++repeat) CHECK(invoke(0x1000,we.abi,wide_count,a).eax==expected);
            CHECK(!wide.mismatches&&!wide.skipped);CHECK(es2_hook_uninstall(&wide));
        }
        if(!abi) { /* Exercise legacy cdecl dispatch as well. */
            es2_hook wide={0};es2_hook_descriptor wd=desc(0x1000,wide_count,weighted);
            CHECK(es2_hook_install(&wide,&wd,&o));CHECK(invoke(0x1000,0,wide_count,a).eax==expected);
            CHECK(!wide.mismatches);CHECK(es2_hook_uninstall(&wide));
        }
    }
    { es2_hook bad={0};es2_hook_descriptor bd=desc(0x1000,ES2_HOOK_MAX_ARGS+1,sum);
      CHECK(!es2_hook_install(&bad,&bd,&o));CHECK(bad.error==ES2_HOOK_ERROR_ARGUMENT); }
    /* Context-dependent scratch values: preserve entry EDX and consume both
     * entry EAX/ECX, including a deliberate upper-result mismatch. */
    const BYTE context_body[]={0x01,0xc8,0xc3};code(0x1000,context_body,sizeof(context_body));
    for(unsigned mode=0;mode<=2;++mode) {
        es2_hook ch={0};es2_hook_descriptor cd=desc(0x1000,0,sum);es2_hook_extension ce={0};
        ce.result_kind=ES2_RESULT_PAIR;ce.replacement_context=context_pair;
        es2_hook_options co=options();co.mode=(es2_hook_mode)mode;CHECK(es2_hook_install_ex(&ch,&cd,&co,&ce));
        for(kind=0;kind<2;++kind) {
            BYTE fpu[108];es2_hook_fpu_save(fpu);es2_hook_result cr;
            const uint32_t regs[]={0x13572468,0xabcdef01,0x10203040};
            es2_hook_call call={image+0x1000,ES2_ABI_CDECL,0,a,fpu,&cr,regs};es2_hook_machine_call(&call);
            CHECK(cr.eax==regs[0]+regs[2]);CHECK(cr.edx==(regs[1]^(mode==ES2_HOOK_REPLACED?kind:0)));
        }
        CHECK(ch.mismatches==(mode==ES2_HOOK_COMPARE?1u:0u));CHECK(es2_hook_uninstall(&ch));
    }
    /* A compare-unsupported hook installs in original/replaced mode only. */
    for(unsigned mode=0;mode<=2;++mode) {
        es2_hook uh={0};es2_hook_descriptor ud=desc(0x1000,0,sum);es2_hook_extension ue={0};
        ue.result_kind=ES2_RESULT_PAIR;ue.replacement_context=context_pair;ue.compare_unsupported=TRUE;
        es2_hook_options uo=options();uo.mode=(es2_hook_mode)mode;
        BOOL installed=es2_hook_install_ex(&uh,&ud,&uo,&ue);
        CHECK(installed==(mode!=ES2_HOOK_COMPARE));
        if(installed) CHECK(es2_hook_uninstall(&uh)); else CHECK(uh.error==ES2_HOOK_ERROR_ARGUMENT);
    }
    kind=0;
    const BYTE multiply[]={0x8b,0x44,0x24,4,0xf7,0x64,0x24,8,0xc3}; code(0x1000,multiply,sizeof(multiply));
    es2_hook h={0}; es2_hook_descriptor d=desc(0x1000,2,sum); es2_hook_extension e={0};
    e.result_kind=ES2_RESULT_PAIR; e.replacement64=pair; CHECK(es2_hook_install_ex(&h,&d,&o,&e));
    for (kind=0;kind<=1;++kind) { es2_hook_result r=invoke(0x1000,0,2,a); CHECK(r.eax==0xfffffff2u && r.edx==6); }
    CHECK(h.mismatches==1); CHECK(es2_hook_uninstall(&h));
    const BYTE float_load[]={0xdb,0x44,0x24,4,0xc3}; code(0x1000,float_load,sizeof(float_load));
    memset(&h,0,sizeof(h)); e.result_kind=ES2_RESULT_ST0; e.replacement80=floating;
    CHECK(es2_hook_install_ex(&h,&d,&o,&e));
    /* x87 reports reserved bit 6 set when loading Borland's 0x1332. */
    for (kind=0;kind<=4;++kind) { es2_hook_result r=invoke(0x1000,0,2,a); CHECK(r.fpu[0]==0x72 && r.fpu[1]==0x13); }
    CHECK(h.mismatches==4 && !h.skipped); CHECK(es2_hook_uninstall(&h));
    /* Global isolation, mismatch rollback and writable-page validation. */
    BYTE global_tail[]={0xa1,0,0,0,0,0x03,0x44,0x24,4,0xa3,0,0,0,0,0xc3};
    put32(global_tail+1,(uint32_t)(uintptr_t)(image+0x3000)); put32(global_tail+10,(uint32_t)(uintptr_t)(image+0x3000));
    code(0x1000,global_tail,sizeof(global_tail)); d=desc(0x1000,1,global); memset(&e,0,sizeof(e));
    es2_hook_region region={ES2_REGION_RVA,0,0x3000,0,0,4,FALSE,0,{0}}; e.regions=&region; e.region_count=1;
    memset(&h,0,sizeof(h)); CHECK(es2_hook_install_ex(&h,&d,&o,&e));
    const uint32_t add[ES2_HOOK_MAX_ARGS]={5,0,0,0}; put32(image+0x3000,10);
    for(kind=0;kind<=1;++kind) { CHECK(invoke(0x1000,0,1,add).eax==15+5*kind); CHECK(word(image+0x3000)==15+5*kind); }
    CHECK(h.mismatches==1 && !h.skipped); CHECK(es2_hook_uninstall(&h));
    /* Argument and global pointer traversal; unchanged RO contract enforced. */
    const BYTE read_indirect[]={0x8b,0x44,0x24,4,0x8b,0x00,0x8b,0x00,0xc3}; code(0x1000,read_indirect,sizeof(read_indirect));
    BYTE pointed[4]; put32(pointed,123); BYTE pointer[4]; put32(pointer,(uint32_t)(uintptr_t)pointed);
    uint32_t pa[ES2_HOOK_MAX_ARGS]={(uint32_t)(uintptr_t)pointer,0,0,0}; d=desc(0x1000,1,indirect);
    for(unsigned source=1;source<=2;++source) {
        region.source=(es2_hook_region_source)source; region.read_only=TRUE;
        put32(image+0x3000,(uint32_t)(uintptr_t)pointed);
        memset(&h,0,sizeof(h)); CHECK(es2_hook_install_ex(&h,&d,&o,&e));
        for(kind=0;kind<=1;++kind) { CHECK(invoke(0x1000,0,1,pa).eax==123); CHECK(word(pointed)==123); }
        CHECK(h.mismatches==1 && !h.skipped); CHECK(es2_hook_uninstall(&h));
    }
    region.source=ES2_REGION_ARG_INDIRECT;
    memset(&h,0,sizeof(h)); CHECK(es2_hook_install_ex(&h,&d,&o,&e)); kind=2;
    CHECK(invoke(0x1000,0,1,pa).eax==123); CHECK(word(pointer)==(uint32_t)(uintptr_t)pointed);
    CHECK(h.mismatches==1); CHECK(es2_hook_uninstall(&h));
    /* Two/three-hop RO graph, changed original/replacement slot, null and
     * PAGE_NOACCESS targets. An invalid comparison region cannot crash a
     * perfectly valid original function that does not use that region. */
    for(wide_count=2;wide_count<=3;++wide_count) {
        BYTE nodes[4][4];put32(nodes[3],456);
        for(unsigned j=0;j<3;++j) put32(nodes[j],(uint32_t)(uintptr_t)nodes[j+1]);
        uint32_t ca[ES2_HOOK_MAX_ARGS]={(uint32_t)(uintptr_t)nodes[3-wide_count]};
        BYTE tail[24]={0x8b,0x44,0x24,4};unsigned length=4;
        for(unsigned j=0;j<=wide_count;++j) { tail[length++]=0x8b;tail[length++]=0x00; }tail[length++]=0xc3;
        code(0x1000,tail,length);d=desc(0x1000,1,chained);
        region.source=ES2_REGION_ARG_INDIRECT;region.pointer_hops=wide_count;region.read_only=TRUE;
        memset(&h,0,sizeof(h));CHECK(es2_hook_install_ex(&h,&d,&o,&e));
        kind=0;CHECK(invoke(0x1000,0,1,ca).eax==456);CHECK(!h.mismatches&&!h.skipped);
        for(kind=1;kind<=wide_count;++kind) {
            CHECK(invoke(0x1000,0,1,ca).eax==456);CHECK(h.mismatches==kind&&!h.skipped);
            CHECK(word(nodes[2-wide_count+kind])==(uint32_t)(uintptr_t)nodes[3-wide_count+kind]);
        }
        CHECK(es2_hook_uninstall(&h));
        const BYTE constant[]={0xb8,7,0,0,0,0xc3};code(0x1000,constant,sizeof(constant));
        for(unsigned invalid=0;invalid<2;++invalid) {
            BYTE *target=invalid?VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS):NULL;
            put32(nodes[2],(uint32_t)(uintptr_t)target);
            memset(&h,0,sizeof(h));CHECK(es2_hook_install_ex(&h,&d,&o,&e));
            CHECK(invoke(0x1000,0,1,ca).eax==7);CHECK(h.skipped==1&&!h.mismatches);CHECK(es2_hook_uninstall(&h));
            if(target) CHECK(VirtualFree(target,0,MEM_RELEASE));
        }
        put32(nodes[2],(uint32_t)(uintptr_t)nodes[3]);
        BYTE change[]={0x8b,0x44,0x24,4,0xc7,0x00,0,0,0,0,0xb8,7,0,0,0,0xc3};code(0x1000,change,sizeof(change));
        memset(&h,0,sizeof(h));CHECK(es2_hook_install_ex(&h,&d,&o,&e));
        CHECK(invoke(0x1000,0,1,ca).eax==7);CHECK(h.skipped==1&&!h.mismatches);CHECK(es2_hook_uninstall(&h));
    }
    region.pointer_hops=4;CHECK(!es2_hook_install_ex(&h,&d,&o,&e));
    region.pointer_hops=2;region.read_only=FALSE;CHECK(!es2_hook_install_ex(&h,&d,&o,&e));
    region.read_only=TRUE;region.next_pointer_offsets[0]=ES2_HOOK_MAX_STATE_BYTES+1;
    CHECK(!es2_hook_install_ex(&h,&d,&o,&e));region.next_pointer_offsets[0]=0;
    region.pointer_hops=0;
    region.source=ES2_REGION_RVA; region.rva=0x3fff; region.read_only=FALSE;
    CHECK(!es2_hook_install_ex(&h,&d,&o,&e)); CHECK(h.error==ES2_HOOK_ERROR_ARGUMENT && !h.code);
    region.rva=0x3000; region.size=ES2_HOOK_MAX_STATE_BYTES+1;
    CHECK(!es2_hook_install_ex(&h,&d,&o,&e)); CHECK(!memcmp(image+0x1000,signature,5));
    region.size=4; region.read_only=TRUE;
    /* Null indirect region: original only, no private phase and no dereference. */
    code(0x1000,stack_sum,sizeof(stack_sum)-3); image[0x1000+5+sizeof(stack_sum)-3]=0xc3;
    d=desc(0x1000,4,sum); region.source=ES2_REGION_ARG_INDIRECT; region.arg_index=0;
    memset(&h,0,sizeof(h)); CHECK(es2_hook_install_ex(&h,&d,&o,&e)); kind=0;
    CHECK(invoke(0x1000,0,4,a).eax==35); CHECK(h.skipped==1); CHECK(es2_hook_uninstall(&h));
    /* Cross-ABI nested call: original phase bypasses the inner hook once. */
    BYTE outer[]={0xff,0x74,0x24,4,0x51,0x52,0x50,0xe8,0,0,0,0,0x83,0xc4,16,0xc2,4,0};
    put32(outer+8,0x1100-(0x1000+5+12)); code(0x1000,outer,sizeof(outer));
    code(0x1100,stack_sum,sizeof(stack_sum)-3); image[0x1100+5+sizeof(stack_sum)-3]=0xc3;
    es2_hook inner={0}; es2_hook_descriptor di=desc(0x1100,4,sum); CHECK(es2_hook_install(&inner,&di,&o));
    d=desc(0x1000,4,nested); memset(&e,0,sizeof(e)); e.abi=ES2_ABI_BORLAND;
    memset(&h,0,sizeof(h)); CHECK(es2_hook_install_ex(&h,&d,&o,&e));
    CHECK(invoke(0x1000,2,4,a).eax==35); CHECK(!h.mismatches && inner.calls==2 && inner.bypassed==1 && !inner.mismatches);
    CHECK(es2_hook_uninstall(&h)); CHECK(es2_hook_uninstall(&inner));
    /* Refusal is transactional; failed cache rollback retains a usable context. */
    e.abi=(es2_hook_abi)-1; CHECK(!es2_hook_install_ex(&h,&d,&o,&e)); CHECK(!memcmp(image+0x1000,signature,5));
    e.abi=ES2_ABI_BORLAND; e.region_count=5; CHECK(!es2_hook_install_ex(&h,&d,&o,&e));
    e.region_count=0; o.patch_api.flush=reject_flush; CHECK(!es2_hook_install_ex(&h,&d,&o,&e));
    CHECK(h.error==ES2_HOOK_ERROR_ROLLBACK && h.code && !memcmp(image+0x1000,signature,5));
    h.options.patch_api.flush=NULL; CHECK(es2_hook_uninstall(&h));
    VirtualFree(image,0,MEM_RELEASE); printf("PASS: extended framework %u checks\n",checks); return 0;
}
