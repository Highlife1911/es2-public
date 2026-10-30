/* The emulated probe kinds of entry_probe_engine.h against the
 * processor. Own code only: each case assembles one instruction of a kind
 * between a register/flags setup and a capture stub, runs it natively, then
 * again with an entry probe on the instruction, and requires the same eight
 * registers, EFLAGS, top-of-stack word and data block, and exactly one count.
 * No original code or data is used. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../entry_probe_engine.h"
#define CHECK(c) do {if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);exit(1);}}while(0)

typedef struct snapshot { DWORD reg[8], flags, top; BYTE data[64]; } snapshot;
static BYTE *code, *site_at;
static DWORD *saved_esp, *slot;
static snapshot *out;
static BYTE *data;
static unsigned length, cases, kinds_seen[EP_KINDS];
static const DWORD pattern[8]={0x11223344,0x8899aabb,0xf0e0d0c0,0x01020304,0,0x5a5aa5a5,0x7f00ff80,0x80000001};

static void emit(const BYTE *b, unsigned n) { memcpy(code+length,b,n); length+=n; }
static void emit1(BYTE b) { emit(&b,1); }
static void emit32(DWORD v) { BYTE b[4]={(BYTE)v,(BYTE)(v>>8),(BYTE)(v>>16),(BYTE)(v>>24)}; emit(b,4); }
static void emit_abs(BYTE op, BYTE modrm, const void *p) { emit1(op); emit1(modrm); emit32((DWORD)(uintptr_t)p); }

/* pushad; mov [saved],esp; push flags; popfd; mov r,pattern (not esp);
 * [push capture]; <instruction>; capture: store eight registers, flags and
 * [esp]; mov esp,[saved]; popad; ret. Returns the capture address. */
static BYTE *assemble(const BYTE *instruction, unsigned n, DWORD flags, BOOL push_capture, unsigned *target_offset) {
    length=0;
    emit1(0x60); emit_abs(0x89,0x25,saved_esp);
    emit1(0x68); emit32(flags); emit1(0x9d);
    for(unsigned r=0;r<8;++r) if(r!=4) { emit1((BYTE)(0xb8+r)); emit32(pattern[r]); }
    unsigned push_at=length; if(push_capture) { emit1(0x68); emit32(0); }
    site_at=code+length; emit(instruction,n);
    BYTE *capture=code+length;
    for(unsigned r=0;r<8;++r) emit_abs(0x89,(BYTE)(0x05|(r<<3)),&out->reg[r]);
    emit1(0x9c); emit_abs(0x8f,0x05,&out->flags);
    BYTE top[3]={0x8b,0x04,0x24}; emit(top,3); emit_abs(0x89,0x05,&out->top);
    emit_abs(0x8b,0x25,saved_esp); emit1(0x61); emit1(0xc3);
    if(push_capture) { DWORD a=(DWORD)(uintptr_t)capture; memcpy(code+push_at+1,&a,4); }
    if(target_offset) { /* rel32 of a call/jmp at the site, or the jmp [slot] target */
        DWORD rel=(DWORD)(uintptr_t)capture-((DWORD)(uintptr_t)site_at+5); memcpy(site_at+*target_offset,&rel,4);
    }
    *slot=(DWORD)(uintptr_t)capture;
    CHECK(length<4096 && FlushInstructionCache(GetCurrentProcess(),code,length));
    return capture;
}
static void run(snapshot *result, const BYTE *memory) {
    memcpy(data,memory,64); memset(out,0,sizeof(*out));
    ((void (__cdecl *)(void))code)();
    *result=*out; memcpy(result->data,data,64);
}
/* One instruction of a kind: native and probed runs must agree exactly. */
static void compare(unsigned kind, const BYTE *instruction, unsigned n, DWORD flags, BOOL push_capture,
                    unsigned rel_at, const BYTE *memory) {
    assemble(instruction,n,flags,push_capture,rel_at?&rel_at:NULL);
    snapshot native, probed;
    run(&native,memory);
    ep_site s; memset(&s,0,sizeof(s)); s.patch.address=(uintptr_t)site_at; s.patch.length=1; s.kind=kind;
    if(kind==EP_PUSH) s.patch.saved[0]=instruction[0];
    CHECK(ep_install(&s,1,code,4096,NULL,0,LC_COUNT,1) && *site_at==0xcc && s.size==n);
    run(&probed,memory);
    CHECK(ep_uninstall() && *site_at==instruction[0]);
    /* A frame that ENTER or ADD ESP allocates is not written by the
     * instruction: its top word is whatever lay below ESP (the exception
     * dispatch of any INT3 probe writes there), so it is not compared. */
    if((kind==EP_ENTER && (instruction[1]|instruction[2])) || (kind==EP_ADD_ESP_IMM8 && instruction[2])) probed.top=native.top;
    if(s.count!=1 || memcmp(&native,&probed,sizeof(native))) {
        fprintf(stderr,"kind %u bytes",kind); for(unsigned i=0;i<n;++i) fprintf(stderr," %02x",instruction[i]);
        fprintf(stderr," flags %08lx count %lld\n",(unsigned long)flags,(long long)s.count);
        for(unsigned r=0;r<8;++r) fprintf(stderr," r%u %08lx/%08lx",r,(unsigned long)native.reg[r],(unsigned long)probed.reg[r]);
        fprintf(stderr,"\n flags %08lx/%08lx top %08lx/%08lx data %d\n",(unsigned long)native.flags,(unsigned long)probed.flags,
            (unsigned long)native.top,(unsigned long)probed.top,memcmp(native.data,probed.data,64));
        exit(1);
    }
    ++cases; ++kinds_seen[kind];
}
static void put(BYTE *b, unsigned at, DWORD v) { memcpy(b+at,&v,4); }

int main(void) {
    BYTE *page=VirtualAlloc(NULL,3*4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE); CHECK(page);
    code=page; data=page+4096; saved_esp=(DWORD *)(page+8192); slot=saved_esp+1; out=(snapshot *)(page+8192+64);
    static const DWORD flag_patterns[]={0x202,0xad7,0x203,0x2c6,0xa82};
    static const DWORD values[]={0,1,0x0f,0x10,0x7f,0x80,0xff,0x100,0x7fff,0x8000,0xffff,0x7fffffff,0x80000000,0xfffffff0,0xffffffff,0x12345678};
    static const BYTE imms[]={0,1,0x0f,0x10,0x7f,0x80,0xf0,0xff};
    BYTE memory[64]; for(unsigned i=0;i<64;++i) memory[i]=(BYTE)(0x9d*i+0x31);
    BYTE in[16];
    /* Flag-free kinds, every flags pattern. */
    for(unsigned f=0;f<sizeof(flag_patterns)/sizeof(*flag_patterns);++f) {
        DWORD fl=flag_patterns[f];
        for(unsigned op=0x50;op<=0x57;++op) if(op!=0x54) { in[0]=(BYTE)op; compare(EP_PUSH,in,1,fl,FALSE,0,memory); }
        static const WORD frames[]={0,0x10,0x15c};
        for(unsigned i=0;i<3;++i) { BYTE b[4]={0xc8,(BYTE)frames[i],(BYTE)(frames[i]>>8),0}; compare(EP_ENTER,b,4,fl,FALSE,0,memory); }
        { BYTE b[5]={0xe8}; compare(EP_CALL_REL32,b,5,fl,FALSE,1,memory); }
        { BYTE b[5]={0xe9}; compare(EP_JMP_REL32,b,5,fl,FALSE,1,memory); }
        { BYTE b[6]={0xff,0x25}; put(b,2,(DWORD)(uintptr_t)slot); compare(EP_JMP_ABS,b,6,fl,FALSE,0,memory); }
        { BYTE b[5]={0xa1}; put(b,1,(DWORD)(uintptr_t)(data+4)); compare(EP_MOV_EAX_ABS,b,5,fl,FALSE,0,memory); }
        { BYTE b[5]={0xa0}; put(b,1,(DWORD)(uintptr_t)(data+9)); compare(EP_MOV_AL_ABS,b,5,fl,FALSE,0,memory); }
        { BYTE b[6]={0x66,0xa1}; put(b,2,(DWORD)(uintptr_t)(data+13)); compare(EP_MOV_AX_ABS,b,6,fl,FALSE,0,memory); }
        for(unsigned r=0;r<8;++r) if(r!=4) {
            BYTE b[6]={0x8b,(BYTE)(0x05|(r<<3))}; put(b,2,(DWORD)(uintptr_t)(data+20)); compare(EP_MOV_R32_ABS,b,6,fl,FALSE,0,memory);
            BYTE m[5]={(BYTE)(0xb8+r)}; put(m,1,0xdeadbeef^r); compare(EP_MOV_R32_IMM,m,5,fl,FALSE,0,memory);
            BYTE w[4]={0x66,(BYTE)(0xb8+r),0x34,(BYTE)(0x12+r)}; compare(EP_MOV_R16_IMM,w,4,fl,FALSE,0,memory);
            for(unsigned byte=0;byte<2;++byte) {
                BYTE x[7]={0x0f,(BYTE)(0xbe + byte),(BYTE)(0x05|(r<<3))}; put(x,3,(DWORD)(uintptr_t)(data+24+byte));
                compare(byte?EP_MOVSX_R32_M16:EP_MOVSX_R32_M8,x,7,fl,FALSE,0,memory);
            }
            for(unsigned src=0;src<8;++src) { BYTE v[2]={0x8b,(BYTE)(0xc0|(r<<3)|src)}; compare(EP_MOV_R32_R32,v,2,fl,FALSE,0,memory); }
            BYTE z[2]={0x33,(BYTE)(0xc0|(r<<3)|r)}; compare(EP_XOR_R32_SELF,z,2,fl,FALSE,0,memory);
        }
        { BYTE b[7]={0xc6,0x05}; put(b,2,(DWORD)(uintptr_t)(data+30)); b[6]=0x5a; compare(EP_MOV_M8_IMM,b,7,fl,FALSE,0,memory); }
        { BYTE b[9]={0x66,0xc7,0x05}; put(b,3,(DWORD)(uintptr_t)(data+33)); b[7]=0x34; b[8]=0x92; compare(EP_MOV_M16_IMM,b,9,fl,FALSE,0,memory); }
        for(unsigned i=0;i<sizeof(imms);++i) { BYTE b[2]={0x6a,imms[i]}; compare(EP_PUSH_IMM8,b,2,fl,FALSE,0,memory); }
        { BYTE b[5]={0x68}; put(b,1,0x80706050); compare(EP_PUSH_IMM32,b,5,fl,FALSE,0,memory); }
        { BYTE b[1]={0xc3}; compare(EP_RET,b,1,fl,TRUE,0,memory); }
    }
    /* Movsx sign: both signs of the byte and word operand. */
    for(unsigned v=0;v<sizeof(values)/sizeof(*values);++v) {
        put(memory,24,values[v]);
        for(unsigned byte=0;byte<2;++byte) {
            BYTE x[7]={0x0f,(BYTE)(0xbe + byte),0x0d}; put(x,3,(DWORD)(uintptr_t)(data+24));
            compare(byte?EP_MOVSX_R32_M16:EP_MOVSX_R32_M8,x,7,0x202,FALSE,0,memory);
        }
    }
    /* Arithmetic kinds: every value, immediate and flags pattern. */
    for(unsigned f=0;f<sizeof(flag_patterns)/sizeof(*flag_patterns);++f) for(unsigned i=0;i<sizeof(imms);++i) {
        DWORD fl=flag_patterns[f];
        BYTE c32[7]={0x83,0x3d}; put(c32,2,(DWORD)(uintptr_t)(data+40)); c32[6]=imms[i];
        BYTE c16[8]={0x66,0x83,0x3d}; put(c16,3,(DWORD)(uintptr_t)(data+40)); c16[7]=imms[i];
        BYTE c8[7]={0x80,0x3d}; put(c8,2,(DWORD)(uintptr_t)(data+40)); c8[6]=imms[i];
        BYTE a8[7]={0x80,0x05}; put(a8,2,(DWORD)(uintptr_t)(data+40)); a8[6]=imms[i];
        for(unsigned v=0;v<sizeof(values)/sizeof(*values);++v) {
            put(memory,40,values[v]);
            compare(EP_CMP_M32_IMM8,c32,7,fl,FALSE,0,memory);
            compare(EP_CMP_M16_IMM8,c16,8,fl,FALSE,0,memory);
            compare(EP_CMP_M8_IMM8,c8,7,fl,FALSE,0,memory);
            compare(EP_ADD_M8_IMM8,a8,7,fl,FALSE,0,memory);
            if(!i) {
                BYTE inc[6]={0xff,0x05},dec[6]={0xff,0x0d}; put(inc,2,(DWORD)(uintptr_t)(data+40)); put(dec,2,(DWORD)(uintptr_t)(data+40));
                compare(EP_INC_M32,inc,6,fl,FALSE,0,memory); compare(EP_DEC_M32,dec,6,fl,FALSE,0,memory);
            }
        }
        /* add esp, imm8 with imm8 <= 0 (a positive one would release the capture's stack). */
        if(imms[i]==0||imms[i]>=0x80) { BYTE b[3]={0x83,0xc4,imms[i]}; compare(EP_ADD_ESP_IMM8,b,3,fl,FALSE,0,memory); }
    }
    for(unsigned k=0;k<EP_KINDS;++k) CHECK(kinds_seen[k]);
    /* Refusals: a wrong kind for the bytes, an ENTER with a nesting level, a
     * movsx/mov through a register instead of [abs32], ESP destinations, and
     * an instruction that overlaps an exclusion beyond its first byte. */
    ep_site s; memset(&s,0,sizeof(s)); s.patch.length=1;
    BYTE bad[][4]={{0xc8,0x10,0x00,0x01},{0x8b,0x01},{0x0f,0xbe,0x01},{0xbc},{0x8b,0xe0},{0x33,0xe4},{0x33,0xc1}};
    unsigned bad_kind[]={EP_ENTER,EP_MOV_R32_ABS,EP_MOVSX_R32_M8,EP_MOV_R32_IMM,EP_MOV_R32_R32,EP_XOR_R32_SELF,EP_XOR_R32_SELF};
    for(unsigned i=0;i<sizeof(bad_kind)/sizeof(*bad_kind);++i) {
        memset(code,0,16); memcpy(code,bad[i],4); s.patch.address=(uintptr_t)code; s.kind=bad_kind[i];
        CHECK(!ep_install(&s,1,code,4096,NULL,0,LC_COUNT,1) && code[0]==bad[i][0]);
    }
    memcpy(code,"\xa1\0\0\0\0",5); s.kind=EP_PUSH; s.patch.saved[0]=0xa1; CHECK(!ep_install(&s,1,code,4096,NULL,0,LC_COUNT,1));
    s.kind=EP_KINDS; CHECK(!ep_install(&s,1,code,4096,NULL,0,LC_COUNT,1));
    s.kind=EP_MOV_EAX_ABS; lc_range tail={(uintptr_t)code+4,1}; CHECK(!ep_install(&s,1,code,4096,&tail,1,LC_COUNT,1));
    CHECK(!ep_install(&s,1,code,4,NULL,0,LC_COUNT,1));          /* instruction runs past the image */
    ep_site two[2]; memset(two,0,sizeof(two)); two[0].patch.length=two[1].patch.length=1;
    two[0].patch.address=(uintptr_t)code; two[0].kind=EP_MOV_EAX_ABS; two[1].patch.address=(uintptr_t)code+2; two[1].kind=EP_PUSH;
    code[2]=0x50; CHECK(!ep_install(two,2,code,4096,NULL,0,LC_COUNT,1)); /* a site inside another site's instruction */
    /* Inaccessible operand: counted, the site restored, the original
     * instruction resumes at the site (and faults there by itself). */
    BYTE *guard=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_NOACCESS); CHECK(guard);
    memcpy(code,"\xa1",1); put(code,1,(DWORD)(uintptr_t)guard); s.kind=EP_MOV_EAX_ABS;
    CHECK(ep_install(&s,1,code,4096,NULL,0,LC_COUNT,1));
    CONTEXT context; memset(&context,0x5a,sizeof(context)); CONTEXT expected=context;
    EXCEPTION_RECORD record={0}; record.ExceptionCode=EXCEPTION_BREAKPOINT; record.ExceptionAddress=code; EXCEPTION_POINTERS e={&record,&context};
    expected.Eip=(DWORD)(uintptr_t)code;
    CHECK(ep_exception(&e)==EXCEPTION_CONTINUE_EXECUTION && !memcmp(&context,&expected,sizeof(context)) && s.count==1 && s.patch.restored && code[0]==0xa1);
    CHECK(ep_uninstall());
    memcpy(code,"\xc6\x05",2); put(code,2,(DWORD)(uintptr_t)(page+8192+4000)); code[6]=1; s.kind=EP_MOV_M8_IMM;
    DWORD old; CHECK(VirtualProtect(page+8192,4096,PAGE_READONLY,&old));
    CHECK(ep_install(&s,1,code,4096,NULL,0,LC_COUNT,1)); context=expected;
    CHECK(ep_exception(&e)==EXCEPTION_CONTINUE_EXECUTION && context.Eip==(DWORD)(uintptr_t)code && s.patch.restored && code[0]==0xc6);
    CHECK(ep_uninstall() && VirtualProtect(page+8192,4096,old,&old));
    VirtualFree(guard,0,MEM_RELEASE); VirtualFree(page,0,MEM_RELEASE);
    printf("PASS probe kinds: %u native/probed cases equal (registers, EFLAGS, stack top, data) over all %u kinds; refusals; inaccessible operands restore\n",cases,(unsigned)EP_KINDS);
    return 0;
}
