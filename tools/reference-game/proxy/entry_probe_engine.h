#ifndef ES2_ENTRY_PROBE_ENGINE_H
#define ES2_ENTRY_PROBE_ENGINE_H
#include "live_coverage_engine.h"

/* Up to 512 sites per run (the DBSIM map's batches; the INI list of
 * 512 plain entries is about 3,600 characters of the 8,192 accepted). */
#define EP_MAX_ENTRIES 512u
/* 128 histogram slots, enough for the 64 direct call sites of the
 * clock wrapper 0x467828 under a return-address key (EP_RETADDR). */
#define EP_RECEIVERS 128u
#define EP_CODE_MAX 12u
/* Receiver keys: the vtable at [esp+4] or at a register, or (EP_RETADDR)
 * the return address at [esp] itself, not dereferenced. */
enum { EP_NONE, EP_ESP4, EP_EAX, EP_ECX, EP_EDX, EP_EBX, EP_ESP, EP_EBP, EP_ESI, EP_EDI, EP_RETADDR };
/* Probe kinds: the instruction at the site that the handler emulates after
 * counting (the site's first byte is INT3, every other byte stays original).
 * EP_PUSH is the one-byte PUSH r32 of the first inventory table; the others
 * are the first instructions of known function starts of the second table
 * (probe_instruction_inventory.py ENTRY_KINDS, dbsim_map.md "entry
 * kinds"). Byte patterns are checked again by ep_kind_size before patching. */
enum { EP_PUSH, EP_ENTER, EP_CALL_REL32, EP_JMP_REL32, EP_JMP_ABS, EP_MOV_EAX_ABS, EP_MOV_AL_ABS,
       EP_MOV_AX_ABS, EP_MOV_R32_ABS, EP_MOV_R32_IMM, EP_MOV_R16_IMM, EP_MOV_M8_IMM, EP_MOVSX_R32_M8,
       EP_PUSH_IMM8, EP_PUSH_IMM32, EP_RET, EP_CMP_M32_IMM8, EP_CMP_M8_IMM8, EP_ADD_ESP_IMM8,
       EP_MOVSX_R32_M16, EP_CMP_M16_IMM8, EP_INC_M32, EP_DEC_M32, EP_MOV_R32_R32, EP_XOR_R32_SELF,
       EP_ADD_M8_IMM8, EP_MOV_M16_IMM, EP_KINDS };
static const BYTE ep_kind_length[EP_KINDS]={1,4,5,5,6,5,5,6,6,5,4,7,7,2,5,1,7,7,3,7,8,6,6,2,2,7,9};
typedef struct __attribute__((aligned(8))) ep_receiver {
    volatile LONG vtable;
    volatile LONG64 count;
} ep_receiver;
typedef struct __attribute__((aligned(8))) ep_site {
    lc_site patch;
    unsigned receiver;
    volatile LONG64 count, overflow;
    ep_receiver receivers[EP_RECEIVERS];
    unsigned kind, size;            /* EP_PUSH (zero) or an emulated kind; size set by ep_validate */
    BYTE code[EP_CODE_MAX];         /* the original instruction, copied by ep_validate */
} ep_site;
static ep_site *ep_sites;
static unsigned ep_count, ep_mode;
static LONG64 ep_limit;
static PVOID ep_handler;
static volatile LONG ep_active, ep_in_handler, ep_error, ep_first_thread, ep_other_thread, ep_lock;

static DWORD ep_register(const CONTEXT *c, unsigned reg) {
    switch(reg) {
    case 0:return c->Eax; case 1:return c->Ecx; case 2:return c->Edx; case 3:return c->Ebx;
    case 4:return c->Esp; case 5:return c->Ebp; case 6:return c->Esi; default:return c->Edi;
    }
}
static void ep_set_register(CONTEXT *c, unsigned reg, DWORD value) {
    switch(reg) {
    case 0:c->Eax=value;break; case 1:c->Ecx=value;break; case 2:c->Edx=value;break; case 3:c->Ebx=value;break;
    case 4:c->Esp=value;break; case 5:c->Ebp=value;break; case 6:c->Esi=value;break; default:c->Edi=value;break;
    }
}
static uint32_t ep_u32(const BYTE *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static BOOL ep_abs_modrm(BYTE modrm) { return (modrm&0xc7)==0x05 && ((modrm>>3)&7)!=4; }
/* Size of the instruction at c if it is of the given kind, else 0 (the same
 * byte tests as ENTRY_KINDS of probe_instruction_inventory.py). */
static unsigned ep_kind_size(unsigned kind, const BYTE *c) {
    BOOL ok;
    switch(kind) {
    case EP_PUSH: ok=c[0]>=0x50&&c[0]<=0x57&&c[0]!=0x54; break;
    case EP_ENTER: ok=c[0]==0xc8&&c[3]==0; break;
    case EP_CALL_REL32: ok=c[0]==0xe8; break;
    case EP_JMP_REL32: ok=c[0]==0xe9; break;
    case EP_JMP_ABS: ok=c[0]==0xff&&c[1]==0x25; break;
    case EP_MOV_EAX_ABS: ok=c[0]==0xa1; break;
    case EP_MOV_AL_ABS: ok=c[0]==0xa0; break;
    case EP_MOV_AX_ABS: ok=c[0]==0x66&&c[1]==0xa1; break;
    case EP_MOV_R32_ABS: ok=c[0]==0x8b&&ep_abs_modrm(c[1]); break;
    case EP_MOV_R32_IMM: ok=c[0]>=0xb8&&c[0]<=0xbf&&c[0]!=0xbc; break;
    case EP_MOV_R16_IMM: ok=c[0]==0x66&&c[1]>=0xb8&&c[1]<=0xbf&&c[1]!=0xbc; break;
    case EP_MOV_M8_IMM: ok=c[0]==0xc6&&c[1]==0x05; break;
    case EP_MOVSX_R32_M8: ok=c[0]==0x0f&&c[1]==0xbe&&ep_abs_modrm(c[2]); break;
    case EP_PUSH_IMM8: ok=c[0]==0x6a; break;
    case EP_PUSH_IMM32: ok=c[0]==0x68; break;
    case EP_RET: ok=c[0]==0xc3; break;
    case EP_CMP_M32_IMM8: ok=c[0]==0x83&&c[1]==0x3d; break;
    case EP_CMP_M8_IMM8: ok=c[0]==0x80&&c[1]==0x3d; break;
    case EP_ADD_ESP_IMM8: ok=c[0]==0x83&&c[1]==0xc4; break;
    case EP_MOVSX_R32_M16: ok=c[0]==0x0f&&c[1]==0xbf&&ep_abs_modrm(c[2]); break;
    case EP_CMP_M16_IMM8: ok=c[0]==0x66&&c[1]==0x83&&c[2]==0x3d; break;
    case EP_INC_M32: ok=c[0]==0xff&&c[1]==0x05; break;
    case EP_DEC_M32: ok=c[0]==0xff&&c[1]==0x0d; break;
    case EP_MOV_R32_R32: ok=c[0]==0x8b&&c[1]>=0xc0&&((c[1]>>3)&7)!=4; break;
    case EP_XOR_R32_SELF: ok=c[0]==0x33&&c[1]>=0xc0&&((c[1]>>3)&7)==(c[1]&7)&&(c[1]&7)!=4; break;
    case EP_ADD_M8_IMM8: ok=c[0]==0x80&&c[1]==0x05; break;
    case EP_MOV_M16_IMM: ok=c[0]==0x66&&c[1]==0xc7&&c[2]==0x05; break;
    default: return 0;
    }
    return ok?ep_kind_length[kind]:0;
}
/* Arithmetic flags (CF PF AF ZF SF OF, mask 0x8d5) of a+b or a-b in `bits`
 * bits; keep_cf for INC/DEC. Every other EFLAGS bit is kept. */
static DWORD ep_arith_flags(DWORD flags, uint32_t a, uint32_t b, unsigned bits, BOOL sub, BOOL keep_cf, uint32_t *result) {
    uint32_t mask=bits==32?0xffffffffu:(1u<<bits)-1u, msb=1u<<(bits-1);
    a&=mask;b&=mask;uint32_t r=(sub?a-b:a+b)&mask;
    BOOL cf=sub?a<b:r<a, of=((sub?(a^b):~(a^b))&(a^r)&msb)!=0;
    unsigned ones=0;for(unsigned i=0;i<8;++i)ones+=(r>>i)&1;
    DWORD out=flags&~(keep_cf?0x8d4u:0x8d5u);
    if(!keep_cf&&cf)out|=0x001;
    if(!(ones&1))out|=0x004;
    if((a^b^r)&0x10)out|=0x010;
    if(!r)out|=0x040;
    if(r&msb)out|=0x080;
    if(of)out|=0x800;
    *result=r;return out;
}
static BOOL ep_writable(uintptr_t p, size_t length) {
    if(!lc_readable(p,length))return FALSE;
    uintptr_t end=p+length;
    while(p<end) {MEMORY_BASIC_INFORMATION m;if(!VirtualQuery((void *)p,&m,sizeof(m)))return FALSE;
        if(!(m.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return FALSE;
        p=(uintptr_t)m.BaseAddress+m.RegionSize;}
    return TRUE;
}
static void ep_push(CONTEXT *c, DWORD value) { c->Esp-=4; *(DWORD *)(uintptr_t)c->Esp=value; }
/* The original instruction's effect on the context and memory, as if it had
 * run at pc. FALSE (nothing changed) when its memory operand is not
 * accessible: the caller then lets the original instruction run and fault. */
static BOOL ep_emulate(const ep_site *s, CONTEXT *c, uintptr_t pc) {
    const BYTE *k=s->code;DWORD next=(DWORD)(pc+s->size);uint32_t a,r;
    switch(s->kind) {
    case EP_PUSH: ep_push(c,ep_register(c,k[0]-0x50u)); break;
    case EP_ENTER: ep_push(c,c->Ebp); c->Ebp=c->Esp; c->Esp-=(DWORD)(k[1]|(k[2]<<8)); break;
    case EP_CALL_REL32: ep_push(c,next); next+=ep_u32(k+1); break;
    case EP_JMP_REL32: next+=ep_u32(k+1); break;
    case EP_JMP_ABS: a=ep_u32(k+2); if(!lc_readable(a,4))return FALSE; next=*(volatile DWORD *)(uintptr_t)a; break;
    case EP_MOV_EAX_ABS: a=ep_u32(k+1); if(!lc_readable(a,4))return FALSE; c->Eax=*(volatile DWORD *)(uintptr_t)a; break;
    case EP_MOV_AL_ABS: a=ep_u32(k+1); if(!lc_readable(a,1))return FALSE; c->Eax=(c->Eax&~0xffu)|*(volatile BYTE *)(uintptr_t)a; break;
    case EP_MOV_AX_ABS: a=ep_u32(k+2); if(!lc_readable(a,2))return FALSE; c->Eax=(c->Eax&~0xffffu)|*(volatile WORD *)(uintptr_t)a; break;
    case EP_MOV_R32_ABS: a=ep_u32(k+2); if(!lc_readable(a,4))return FALSE; ep_set_register(c,(k[1]>>3)&7,*(volatile DWORD *)(uintptr_t)a); break;
    case EP_MOV_R32_IMM: ep_set_register(c,k[0]-0xb8u,ep_u32(k+1)); break;
    case EP_MOV_R16_IMM: ep_set_register(c,k[1]-0xb8u,(ep_register(c,k[1]-0xb8u)&~0xffffu)|k[2]|(k[3]<<8)); break;
    case EP_MOV_M8_IMM: a=ep_u32(k+2); if(!ep_writable(a,1))return FALSE; *(volatile BYTE *)(uintptr_t)a=k[6]; break;
    case EP_MOVSX_R32_M8: a=ep_u32(k+3); if(!lc_readable(a,1))return FALSE; ep_set_register(c,(k[2]>>3)&7,(DWORD)(int32_t)*(volatile int8_t *)(uintptr_t)a); break;
    case EP_MOVSX_R32_M16: a=ep_u32(k+3); if(!lc_readable(a,2))return FALSE; ep_set_register(c,(k[2]>>3)&7,(DWORD)(int32_t)*(volatile int16_t *)(uintptr_t)a); break;
    case EP_PUSH_IMM8: ep_push(c,(DWORD)(int32_t)(int8_t)k[1]); break;
    case EP_PUSH_IMM32: ep_push(c,ep_u32(k+1)); break;
    case EP_RET: next=*(DWORD *)(uintptr_t)c->Esp; c->Esp+=4; break;
    case EP_CMP_M32_IMM8: a=ep_u32(k+2); if(!lc_readable(a,4))return FALSE;
        c->EFlags=ep_arith_flags(c->EFlags,*(volatile DWORD *)(uintptr_t)a,(uint32_t)(int32_t)(int8_t)k[6],32,TRUE,FALSE,&r); break;
    case EP_CMP_M16_IMM8: a=ep_u32(k+3); if(!lc_readable(a,2))return FALSE;
        c->EFlags=ep_arith_flags(c->EFlags,*(volatile WORD *)(uintptr_t)a,(uint32_t)(int32_t)(int8_t)k[7],16,TRUE,FALSE,&r); break;
    case EP_CMP_M8_IMM8: a=ep_u32(k+2); if(!lc_readable(a,1))return FALSE;
        c->EFlags=ep_arith_flags(c->EFlags,*(volatile BYTE *)(uintptr_t)a,k[6],8,TRUE,FALSE,&r); break;
    case EP_ADD_ESP_IMM8: c->EFlags=ep_arith_flags(c->EFlags,c->Esp,(uint32_t)(int32_t)(int8_t)k[2],32,FALSE,FALSE,&r); c->Esp=r; break;
    case EP_INC_M32: case EP_DEC_M32: a=ep_u32(k+2); if(!ep_writable(a,4))return FALSE;
        c->EFlags=ep_arith_flags(c->EFlags,*(volatile DWORD *)(uintptr_t)a,1,32,s->kind==EP_DEC_M32,TRUE,&r); *(volatile DWORD *)(uintptr_t)a=r; break;
    case EP_MOV_R32_R32: ep_set_register(c,(k[1]>>3)&7,ep_register(c,k[1]&7)); break;
    /* XOR r,r: CF OF SF 0, ZF PF 1; AF is architecturally undefined and
     * cleared by the processors (the engine fixture checks it natively). */
    case EP_XOR_R32_SELF: ep_set_register(c,k[1]&7,0); c->EFlags=(c->EFlags&~0x8d5u)|0x044u; break;
    case EP_ADD_M8_IMM8: a=ep_u32(k+2); if(!ep_writable(a,1))return FALSE;
        c->EFlags=ep_arith_flags(c->EFlags,*(volatile BYTE *)(uintptr_t)a,k[6],8,FALSE,FALSE,&r); *(volatile BYTE *)(uintptr_t)a=(BYTE)r; break;
    case EP_MOV_M16_IMM: a=ep_u32(k+3); if(!ep_writable(a,2))return FALSE; *(volatile WORD *)(uintptr_t)a=(WORD)(k[7]|(k[8]<<8)); break;
    default: return FALSE;
    }
    c->Eip=next;return TRUE;
}
static BOOL ep_copy_word(uintptr_t address, DWORD *value) {
    SIZE_T got = 0;
    return address && address <= UINTPTR_MAX-4 &&
        ReadProcessMemory(GetCurrentProcess(), (void *)address, value, 4, &got) && got == 4;
}
static void ep_receive(ep_site *s, const CONTEXT *c) {
    if (!s->receiver) return;
    DWORD receiver = 0, vtable = 0;
    BOOL ok = s->receiver == EP_RETADDR ? ep_copy_word(c->Esp, &vtable) :
        (s->receiver == EP_ESP4 ? ep_copy_word(c->Esp + 4u, &receiver) :
         (receiver = ep_register(c, s->receiver-EP_EAX), TRUE)) && ep_copy_word(receiver, &vtable);
    if (ok && vtable) {
        for (unsigned i=0; i<EP_RECEIVERS; ++i) {
            LONG key = InterlockedCompareExchange(&s->receivers[i].vtable, (LONG)vtable, 0);
            if (!key || (DWORD)key == vtable) { InterlockedIncrement64(&s->receivers[i].count); return; }
        }
    }
    InterlockedIncrement64(&s->overflow);
}
static BOOL ep_restore(ep_site *s) {
    /* Reuse coverage's tamper-aware, byte-only restoration and patch APIs. */
    BOOL ok = lc_restore(&s->patch);
    if (!ok) ep_error=2;
    return ok;
}
static LONG CALLBACK ep_exception(EXCEPTION_POINTERS *e) {
    if (!ep_active || e->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT) return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t pc=(uintptr_t)e->ExceptionRecord->ExceptionAddress;
    for(unsigned i=0;i<ep_count;++i) {
        ep_site *s=&ep_sites[i];lc_site *p=&s->patch;
        if(pc!=p->address || !p->installed || (*(BYTE *)pc!=0xcc && !(p->restored && *(BYTE *)pc==p->saved[0])))continue;
        DWORD error=GetLastError();InterlockedIncrement(&ep_in_handler);
        LONG tid=(LONG)GetCurrentThreadId(), first=InterlockedCompareExchange(&ep_first_thread,tid,0);
        if(first && first!=tid)InterlockedCompareExchange(&ep_other_thread,tid,0);
        BOOL counted=TRUE;
        if(ep_mode==LC_COUNT)InterlockedIncrement64(&s->count);
        else {
            LONG64 n=lc_read64(&s->count);
            for(;;) {
                if(n>=ep_limit){counted=FALSE;break;}
                LONG64 previous=InterlockedCompareExchange64(&s->count,n+1,n);
                if(previous==n)break;n=previous;
            }
        }
        if(counted)ep_receive(s,e->ContextRecord);
        if(ep_mode==LC_SATURATE && lc_read64(&s->count)>=ep_limit && !p->restored &&
           !InterlockedCompareExchange(&ep_lock,1,0)) {ep_restore(s);InterlockedExchange(&ep_lock,0);}
        /* The site's own instruction is emulated (ep_emulate): no x87 effect
         * and no execute-out-of-line or unpatched single-step window. If its
         * memory operand is inaccessible, the site is restored and the
         * original instruction runs (and faults) itself. */
        LONG result=EXCEPTION_CONTINUE_EXECUTION;
        if(!ep_emulate(s,e->ContextRecord,pc)) {
            BOOL restored=p->restored;
            if(!restored && !InterlockedCompareExchange(&ep_lock,1,0)) {restored=ep_restore(s);InterlockedExchange(&ep_lock,0);}
            if(restored)e->ContextRecord->Eip=(DWORD)pc;else {ep_error=10;result=EXCEPTION_CONTINUE_SEARCH;}
        }
        InterlockedDecrement(&ep_in_handler);SetLastError(error);return result;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
/* Caller supplies verified instruction starts/signatures from a hash-bound
 * inventory (or an own-code fixture); arbitrary-byte decoding is insufficient. */
static BOOL ep_validate(ep_site *sites,unsigned count,BYTE *image,size_t size,
                        const lc_range *excluded,unsigned nexcluded) {
    if(!count||count>EP_MAX_ENTRIES)return FALSE;
    uintptr_t base=(uintptr_t)image;
    for(unsigned i=0;i<count;++i) {
        ep_site *s=&sites[i];lc_site *p=&s->patch;
        /* Only the first byte is patched; the whole instruction is checked
         * against its kind and copied (the handler emulates the copy). */
        if(p->length!=1 || s->kind>=EP_KINDS || p->address<base || p->address-base>=size ||
           ep_kind_length[s->kind]>size-(p->address-base) || s->receiver>EP_RETADDR ||
           !lc_readable(p->address,ep_kind_length[s->kind]))return FALSE;
        const BYTE *code=(const BYTE *)p->address;unsigned n=ep_kind_size(s->kind,code);
        if(!n || (s->kind==EP_PUSH && code[0]!=p->saved[0]))return FALSE;
        p->saved[0]=code[0];s->size=n;memset(s->code,0,EP_CODE_MAX);memcpy(s->code,code,n);
        for(unsigned j=0;j<i;++j)if(p->address<sites[j].patch.address+sites[j].size && sites[j].patch.address<p->address+n)return FALSE;
        for(unsigned j=0;j<nexcluded;++j)if(p->address<excluded[j].start+excluded[j].size && excluded[j].start<p->address+n)return FALSE;
    }
    return TRUE;
}
static BOOL ep_uninstall(void) {
    if(ep_in_handler || InterlockedCompareExchange(&ep_lock,1,0)){ep_error=5;return FALSE;}
    BOOL ok=TRUE;
    for(unsigned i=0;i<ep_count;++i)if(!ep_restore(&ep_sites[i]))ok=FALSE;
    InterlockedExchange(&ep_lock,0);
    if(ok) {
        if(ep_handler && !RemoveVectoredExceptionHandler(ep_handler)){ep_error=8;return FALSE;}
        ep_handler=NULL;ep_active=0;
    }
    return ok;
}
static BOOL ep_install(ep_site *sites,unsigned count,BYTE *image,size_t size,
        const lc_range *excluded,unsigned nexcluded,unsigned mode,LONG64 limit) {
    if(ep_active||ep_handler||mode>LC_COUNT||limit<1||!ep_validate(sites,count,image,size,excluded,nexcluded))return FALSE;
    ep_sites=sites;ep_count=count;ep_mode=mode;ep_limit=limit;
    ep_error=ep_first_thread=ep_other_thread=0;
    for(unsigned i=0;i<count;++i) {
        sites[i].count=sites[i].overflow=0;memset(sites[i].receivers,0,sizeof(sites[i].receivers));
        sites[i].patch.installed=sites[i].patch.restored=0;
    }
    ep_handler=AddVectoredExceptionHandler(1,ep_exception);if(!ep_handler)return FALSE;ep_active=1;
    for(unsigned i=0;i<count;++i) {
        lc_site *p=&sites[i].patch;DWORD old,ignored;
        if(!lc_protect((void *)p->address,1,PAGE_EXECUTE_READWRITE,&old)){ep_error=3;ep_uninstall();return FALSE;}
        p->protection=old;p->installed=1;*(BYTE *)p->address=0xcc;
        BOOL flushed=lc_flush(GetCurrentProcess(),(void *)p->address,1);
        BOOL protected=lc_protect((void *)p->address,1,old,&ignored);
        if(!flushed||!protected){ep_error=4;ep_uninstall();return FALSE;}
    }
    return TRUE;
}
#endif
