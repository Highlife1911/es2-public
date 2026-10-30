#ifndef ES2_LIVE_COVERAGE_ENGINE_H
#define ES2_LIVE_COVERAGE_ENGINE_H
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
enum { LC_JE,LC_JNE,LC_JL,LC_JLE,LC_JG,LC_JGE,LC_JB,LC_JA,LC_JNS,LC_JAE,LC_JBE,LC_JS,LC_JP,LC_JNP,LC_JO,LC_JNO,LC_CONDITIONS };
enum { LC_SATURATE,LC_COUNT,LC_EDGE };
static const BYTE lc_opcode[LC_CONDITIONS]={4,5,12,14,15,13,2,7,9,3,6,8,10,11,0,1};
static const char *const lc_conditions[LC_CONDITIONS]={"je","jne","jl","jle","jg","jge","jb","ja","jns","jae","jbe","js","jp","jnp","jo","jno"};
typedef struct __attribute__((aligned(8))) lc_site {
    unsigned owner; uintptr_t address; unsigned length,condition; uintptr_t target;
    volatile LONG64 fall,taken;
    volatile LONG64 first_fall_tick,first_taken_tick;
    volatile LONG installed,restored;
    DWORD protection;
    BYTE saved[6];
    /* Edge mode inputs: exclusive destinations ([0] fall, [1] taken), the
     * rel8 redirect byte (0 none) and outcomes a baseline already knows. */
    BYTE exclusive[2],known[2]; uintptr_t trap;
    /* Edge mode state (lc_edge_*): armed roles, point indices, redirect. */
    volatile LONG roles; int point[3]; uintptr_t redirect; BYTE redirect_saved[4];
} lc_site;
typedef struct lc_range { uintptr_t start; size_t size; } lc_range;
static lc_site *lc_sites;
static unsigned lc_count,lc_mode;
static BOOL lc_track_ticks;
static __attribute__((aligned(8))) volatile LONG64 lc_last_tick=-1;
static PVOID lc_handler;
static volatile LONG lc_active,lc_error,lc_patch_lock,lc_first_thread,lc_other_thread,lc_in_handler;
static BOOL (WINAPI *lc_protect)(LPVOID,SIZE_T,DWORD,PDWORD)=VirtualProtect;
static BOOL (WINAPI *lc_flush)(HANDLE,LPCVOID,SIZE_T)=FlushInstructionCache;
static LONG64 lc_read64(volatile LONG64 *v) { return InterlockedCompareExchange64(v,0,0); }
static BOOL lc_readable(uintptr_t p,size_t length) {
    if(!p || length>UINTPTR_MAX-p) return FALSE;
    uintptr_t end=p+length;
    while(p<end) { MEMORY_BASIC_INFORMATION m; if(!VirtualQuery((void *)p,&m,sizeof(m)) || m.State!=MEM_COMMIT || (m.Protect&(PAGE_NOACCESS|PAGE_GUARD))) return FALSE;
        uintptr_t next=(uintptr_t)m.BaseAddress+m.RegionSize;if(next<=p)return FALSE;p=next; }
    return TRUE;
}
static uint32_t lc_u32(const BYTE *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static BOOL lc_decode(const lc_site *s,const BYTE *bytes) {
    if(s->condition>=LC_CONDITIONS) return FALSE;
    uint32_t displacement;
    if(s->length==2 && bytes[0]==0x70+lc_opcode[s->condition]) displacement=bytes[1]<128?bytes[1]:(uint32_t)bytes[1]-256u;
    else if(s->length==6 && bytes[0]==0x0f && bytes[1]==0x80+lc_opcode[s->condition]) displacement=lc_u32(bytes+2);
    else return FALSE;
    return (uint32_t)(s->address+s->length+displacement)==s->target;
}
static BOOL lc_validate(lc_site *sites,unsigned count,BYTE *image,size_t size,const lc_range *ranges,unsigned nranges) {
    uintptr_t base=(uintptr_t)image;
    for(unsigned i=0;i<count;++i) { lc_site *s=&sites[i];
        if(s->address<base || s->address-base>=size || s->length>size-(s->address-base) ||
           s->target<base || s->target-base>=size || !lc_readable(s->address,s->length) || !lc_decode(s,(BYTE *)s->address)) return FALSE;
        for(unsigned j=0;j<i;++j) if(s->address<sites[j].address+sites[j].length && sites[j].address<s->address+s->length) return FALSE;
        for(unsigned j=0;j<nranges;++j) if(s->address<ranges[j].start+ranges[j].size && ranges[j].start<s->address+s->length) return FALSE;
    }
    return TRUE;
}
static unsigned lc_taken(unsigned condition,DWORD flags) {
    unsigned z=(flags>>6)&1,less=((flags>>7)^(flags>>11))&1,c=flags&1,sign=(flags>>7)&1,p=(flags>>2)&1,o=(flags>>11)&1;
    switch(condition) {case LC_JE:return z;case LC_JNE:return !z;case LC_JL:return less;case LC_JLE:return less||z;
    case LC_JG:return !less&&!z;case LC_JGE:return !less;case LC_JB:return c;case LC_JA:return !c&&!z;case LC_JNS:return !sign;case LC_JAE:return !c;
    case LC_JBE:return c||z;case LC_JS:return sign;case LC_JP:return p;case LC_JNP:return !p;case LC_JO:return o;case LC_JNO:return !o;default:return 0;}
}
/* Caller serializes byte/protection changes. Verify the entire saved instruction
 * except its patched first byte; never overwrite another instrumenter's edit. */
static BOOL lc_restore(lc_site *s) {
    if(!s->installed) return TRUE;
    BYTE *p=(BYTE *)s->address;
    if(s->restored) {if(memcmp(p,s->saved,s->length)) {lc_error=2;return FALSE;}return TRUE;}
    if((*p!=0xcc && *p!=s->saved[0]) || memcmp(p+1,s->saved+1,s->length-1)) {InterlockedExchange(&lc_error,2);return FALSE;}
    DWORD old,ignored;
    if(!lc_protect(p,1,PAGE_EXECUTE_READWRITE,&old)) {InterlockedExchange(&lc_error,3);return FALSE;}
    *p=s->saved[0];
    BOOL flushed=lc_flush(GetCurrentProcess(),p,1);
    BOOL protected=lc_protect(p,1,s->protection,&ignored);
    if(!flushed || !protected) {InterlockedExchange(&lc_error,4);return FALSE;}
    InterlockedExchange(&s->restored,1);return TRUE;
}
static LONG CALLBACK lc_exception(EXCEPTION_POINTERS *e) {
    if(!lc_active || e->ExceptionRecord->ExceptionCode!=EXCEPTION_BREAKPOINT) return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t pc=(uintptr_t)e->ExceptionRecord->ExceptionAddress;
    for(unsigned i=0;i<lc_count;++i) { lc_site *s=&lc_sites[i];
        if(pc!=s->address || !s->installed || (*(BYTE *)pc!=0xcc && !(s->restored && *(BYTE *)pc==s->saved[0]))) continue;
        DWORD error=GetLastError(); InterlockedIncrement(&lc_in_handler);
        LONG tid=(LONG)GetCurrentThreadId(); LONG first=InterlockedCompareExchange(&lc_first_thread,tid,0);
        if(first && first!=tid) InterlockedCompareExchange(&lc_other_thread,tid,0);
        unsigned taken=lc_taken(s->condition,e->ContextRecord->EFlags);
        LONG64 observed=InterlockedIncrement64(taken?&s->taken:&s->fall);
        if(lc_track_ticks && observed==1)InterlockedExchange64(taken?&s->first_taken_tick:&s->first_fall_tick,lc_read64(&lc_last_tick));
        if(lc_mode==LC_SATURATE && lc_read64(&s->fall) && lc_read64(&s->taken) && !s->restored && !InterlockedCompareExchange(&lc_patch_lock,1,0)) {
            lc_restore(s);InterlockedExchange(&lc_patch_lock,0);
        }
        e->ContextRecord->Eip=(DWORD)(taken?s->target:s->address+s->length);
        InterlockedDecrement(&lc_in_handler);SetLastError(error);return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
static BOOL lc_edge_uninstall(void);
static BOOL lc_edge_install(lc_site *sites,unsigned count,BYTE *image,size_t size,const lc_range *ranges,unsigned nranges);
/* Both operations require quiescence: no other thread entering selected code. */
static BOOL lc_uninstall(void) {
    if(lc_mode==LC_EDGE) return lc_edge_uninstall();
    if(lc_in_handler || InterlockedCompareExchange(&lc_patch_lock,1,0)) {lc_error=5;return FALSE;}
    BOOL ok=TRUE;
    for(unsigned i=0;i<lc_count;++i) if(!lc_restore(&lc_sites[i])) ok=FALSE;
    InterlockedExchange(&lc_patch_lock,0);
    if(ok) {if(lc_handler && !RemoveVectoredExceptionHandler(lc_handler)) {lc_error=8;return FALSE;}lc_handler=NULL;lc_active=0;}
    return ok;
}
static BOOL lc_install(lc_site *sites,unsigned count,BYTE *image,size_t size,const lc_range *ranges,unsigned nranges,unsigned mode) {
    if(mode==LC_EDGE) return lc_edge_install(sites,count,image,size,ranges,nranges);
    if(lc_active || lc_handler || mode>LC_COUNT || !lc_validate(sites,count,image,size,ranges,nranges)) return FALSE;
    lc_sites=sites;lc_count=count;lc_mode=mode;lc_error=0;lc_first_thread=lc_other_thread=0;lc_last_tick=-1;
    for(unsigned i=0;i<count;++i) {memcpy(sites[i].saved,(BYTE *)sites[i].address,sites[i].length);sites[i].fall=sites[i].taken=0;sites[i].first_fall_tick=sites[i].first_taken_tick=INT64_MIN;sites[i].installed=sites[i].restored=0;}
    lc_handler=AddVectoredExceptionHandler(1,lc_exception);if(!lc_handler)return FALSE;lc_active=1;
    for(unsigned i=0;i<count;++i) { lc_site *s=&sites[i];DWORD old,ignored;
        if(!lc_protect((void *)s->address,1,PAGE_EXECUTE_READWRITE,&old)) {lc_error=3;lc_uninstall();return FALSE;}
        s->protection=old;s->installed=1;*(BYTE *)s->address=0xcc;
        BOOL flushed=lc_flush(GetCurrentProcess(),(void *)s->address,1);
        BOOL protected=lc_protect((void *)s->address,1,old,&ignored);
        if(!flushed || !protected) {lc_error=4;lc_uninstall();return FALSE;}
    }
    return TRUE;
}

/* ---- Edge mode (LC_EDGE) -------------------------------------------------
 * Each missing outcome of a site is observed once, by the cheapest role the
 * site allows, instead of trapping its Jcc on every execution:
 *   redirect  the taken displacement points at a byte that faults by itself
 *             (site->trap for rel8, a slot of an own INT3 page for rel32);
 *             the handler records taken, restores the displacement and
 *             continues at the target;
 *   fall, taken  a one-shot INT3 on an exclusive destination
 *             (site->exclusive, scripts/branch_edges.py): reaching it proves
 *             the edge; the handler restores the byte and re-executes it;
 *   jcc       the INT3 of the other modes (the Jcc is emulated), kept only
 *             while a missing outcome has no cheaper role.
 * Outcomes in site->known (a baseline) are never armed. Roles only ever
 * disarm, so the original bytes come back one by one. A role whose bytes do
 * not pass the install checks falls back to the jcc role. One executor: a
 * second thread is reported (lc_other_thread) as in the other modes. */
enum { LC_ROLE_JCC=1,LC_ROLE_REDIRECT=2,LC_ROLE_FALL=4,LC_ROLE_TAKEN=8 };
enum { LC_POINT_JCC,LC_POINT_FALL,LC_POINT_TAKEN };
/* INT3, HLT, IN/OUT (imm8, DX), INS/OUTS: fault in user mode before executing. */
static const BYTE lc_trap_bytes[]={0xcc,0xf4,0xe4,0xe5,0xe6,0xe7,0xec,0xed,0xee,0xef,0x6c,0x6d,0x6e,0x6f};
typedef struct lc_point { uintptr_t address; int jcc,dest; unsigned taken; DWORD protection; BYTE saved; volatile LONG armed; } lc_point;
typedef struct lc_redirect_entry { uintptr_t trap; unsigned site; } lc_redirect_entry;
static lc_point *lc_points;
static unsigned lc_point_count,lc_capacity;
static lc_redirect_entry *lc_redirects;
static unsigned lc_redirect_count;
static BYTE *lc_stub_pages;
static size_t lc_stub_bytes;
static unsigned lc_edge_armed[4]; /* roles armed at install: jcc, redirect, fall, taken */
static BOOL lc_is_trap_byte(BYTE b) { for(unsigned i=0;i<sizeof(lc_trap_bytes);++i) if(b==lc_trap_bytes[i]) return TRUE; return FALSE; }
static BOOL lc_in_ranges(uintptr_t a,size_t n,const lc_range *ranges,unsigned nranges) {
    for(unsigned j=0;j<nranges;++j) if(a<ranges[j].start+ranges[j].size && ranges[j].start<a+n) return TRUE;
    return FALSE;
}
/* 0 outside every site, 1 at a site's first byte, 2 inside a site's bytes. */
static unsigned lc_site_byte(const lc_site *sites,unsigned count,uintptr_t a) {
    for(unsigned i=0;i<count;++i) if(a>=sites[i].address && a<sites[i].address+sites[i].length) return a==sites[i].address?1:2;
    return 0;
}
static BOOL lc_in_image(uintptr_t a,size_t n,BYTE *image,size_t size) {
    return a>=(uintptr_t)image && a-(uintptr_t)image<size && n<=size-(a-(uintptr_t)image) && lc_readable(a,n);
}
/* A destination INT3 needs an instruction start the proxy does not patch otherwise. */
static BOOL lc_destination_ok(const lc_site *sites,unsigned count,uintptr_t a,BYTE *image,size_t size,const lc_range *ranges,unsigned nranges) {
    return lc_in_image(a,1,image,size) && !lc_in_ranges(a,1,ranges,nranges) && lc_site_byte(sites,count,a)!=2 && !lc_is_trap_byte(*(BYTE *)a);
}
static void lc_record(lc_site *s,unsigned taken) {
    LONG64 observed=InterlockedIncrement64(taken?&s->taken:&s->fall);
    if(lc_track_ticks && observed==1)InterlockedExchange64(taken?&s->first_taken_tick:&s->first_fall_tick,lc_read64(&lc_last_tick));
}
/* Roles a site still needs; monotone: fewer missing outcomes, fewer roles. */
static LONG lc_edge_needed(lc_site *s) {
    LONG roles=0;
    if(!s->known[1] && !lc_read64(&s->taken)) roles|=s->redirect?LC_ROLE_REDIRECT:s->point[LC_POINT_TAKEN]>=0?LC_ROLE_TAKEN:LC_ROLE_JCC;
    if(!s->known[0] && !lc_read64(&s->fall)) roles|=s->point[LC_POINT_FALL]>=0?LC_ROLE_FALL:LC_ROLE_JCC;
    return roles;
}
static BOOL lc_write(uintptr_t a,const BYTE *bytes,size_t n,DWORD *protection) {
    DWORD old,ignored;
    if(!lc_protect((void *)a,n,PAGE_EXECUTE_READWRITE,&old)) {InterlockedExchange(&lc_error,3);return FALSE;}
    if(protection)*protection=old;
    memcpy((void *)a,bytes,n);
    BOOL flushed=lc_flush(GetCurrentProcess(),(void *)a,n),protected=lc_protect((void *)a,n,old,&ignored);
    if(!flushed || !protected) {InterlockedExchange(&lc_error,4);return FALSE;}
    return TRUE;
}
static BOOL lc_point_wanted(const lc_point *p) {
    return (p->jcc>=0 && (lc_sites[p->jcc].roles&LC_ROLE_JCC)) ||
           (p->dest>=0 && (lc_sites[p->dest].roles&(p->taken?LC_ROLE_TAKEN:LC_ROLE_FALL)));
}
/* Restore a point no role wants; verify it holds our INT3 or the original byte. */
static BOOL lc_point_release(lc_point *p) {
    if(!p->armed || lc_point_wanted(p)) return TRUE;
    BYTE now=*(BYTE *)p->address;
    if(now!=0xcc && now!=p->saved) {InterlockedExchange(&lc_error,2);return FALSE;}
    if(now==0xcc && !lc_write(p->address,&p->saved,1,NULL)) return FALSE;
    InterlockedExchange(&p->armed,0);return TRUE;
}
static unsigned lc_redirect_length(const lc_site *s) { return s->length==6?4:1; }
static uintptr_t lc_redirect_at(const lc_site *s) { return s->address+(s->length==6?2:1); }
static void lc_redirect_bytes(const lc_site *s,BYTE *out) {
    uint32_t d=(uint32_t)(s->redirect-(s->address+s->length));
    if(s->length==6) {out[0]=(BYTE)d;out[1]=(BYTE)(d>>8);out[2]=(BYTE)(d>>16);out[3]=(BYTE)(d>>24);} else out[0]=(BYTE)d;
}
static BOOL lc_redirect_release(lc_site *s) {
    if(!(s->roles&LC_ROLE_REDIRECT)) return TRUE;
    BYTE patched[4];lc_redirect_bytes(s,patched);unsigned n=lc_redirect_length(s);
    BYTE *at=(BYTE *)lc_redirect_at(s);
    if(memcmp(at,patched,n) && memcmp(at,s->redirect_saved,n)) {InterlockedExchange(&lc_error,2);return FALSE;}
    if(!memcmp(at,patched,n) && !lc_write((uintptr_t)at,s->redirect_saved,n,NULL)) return FALSE;
    InterlockedAnd(&s->roles,~LC_ROLE_REDIRECT);return TRUE;
}
/* Drop the roles a site no longer needs and restore what nothing wants. */
static BOOL lc_edge_update(lc_site *s) {
    LONG keep=lc_edge_needed(s);BOOL ok=TRUE;
    if((s->roles&LC_ROLE_REDIRECT) && !(keep&LC_ROLE_REDIRECT)) ok=lc_redirect_release(s);
    InterlockedAnd(&s->roles,keep|(s->roles&LC_ROLE_REDIRECT));
    for(unsigned k=0;k<3;++k) if(s->point[k]>=0 && !lc_point_release(&lc_points[s->point[k]])) ok=FALSE;
    return ok;
}
static lc_point *lc_point_find(uintptr_t a) {
    unsigned lo=0,hi=lc_point_count;
    while(lo<hi) {unsigned mid=lo+(hi-lo)/2;if(lc_points[mid].address<a)lo=mid+1;else hi=mid;}
    return lo<lc_point_count && lc_points[lo].address==a?&lc_points[lo]:NULL;
}
static lc_redirect_entry *lc_redirect_find(uintptr_t a) {
    unsigned lo=0,hi=lc_redirect_count;
    while(lo<hi) {unsigned mid=lo+(hi-lo)/2;if(lc_redirects[mid].trap<a)lo=mid+1;else hi=mid;}
    return lo<lc_redirect_count && lc_redirects[lo].trap==a?&lc_redirects[lo]:NULL;
}
static void lc_patch_acquire(void) { while(InterlockedCompareExchange(&lc_patch_lock,1,0)) YieldProcessor(); }
static LONG CALLBACK lc_edge_exception(EXCEPTION_POINTERS *e) {
    DWORD code=e->ExceptionRecord->ExceptionCode;
    if(!lc_active || lc_mode!=LC_EDGE || (code!=EXCEPTION_BREAKPOINT && code!=EXCEPTION_PRIV_INSTRUCTION)) return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t pc=(uintptr_t)e->ExceptionRecord->ExceptionAddress;
    lc_redirect_entry *r=lc_redirect_find(pc);
    lc_point *p=r?NULL:lc_point_find(pc);
    /* A point is ours while it holds our INT3, or holds its byte again after a
     * release (an INT3 another thread had already fetched). */
    if(!r && (!p || code!=EXCEPTION_BREAKPOINT || (*(BYTE *)pc!=0xcc && *(BYTE *)pc!=p->saved))) return EXCEPTION_CONTINUE_SEARCH;
    DWORD error=GetLastError();InterlockedIncrement(&lc_in_handler);
    LONG tid=(LONG)GetCurrentThreadId();LONG first=InterlockedCompareExchange(&lc_first_thread,tid,0);
    if(first && first!=tid) InterlockedCompareExchange(&lc_other_thread,tid,0);
    lc_patch_acquire();
    if(r) {
        /* Only the redirected Jcc arrives here: the byte starts no reached instruction. */
        lc_site *s=&lc_sites[r->site];lc_record(s,1);lc_edge_update(s);
        e->ContextRecord->Eip=(DWORD)s->target;
    } else {
        lc_site *dest=p->dest>=0?&lc_sites[p->dest]:NULL,*jcc=p->jcc>=0?&lc_sites[p->jcc]:NULL;
        /* One observation per destination role; a later arrival proves nothing new. */
        if(dest && (dest->roles&(p->taken?LC_ROLE_TAKEN:LC_ROLE_FALL))) lc_record(dest,p->taken);
        DWORD eip=(DWORD)pc;
        if(jcc) {unsigned taken=lc_taken(jcc->condition,e->ContextRecord->EFlags);lc_record(jcc,taken);eip=(DWORD)(taken?jcc->target:jcc->address+jcc->length);}
        if(dest) lc_edge_update(dest);
        if(jcc) lc_edge_update(jcc);
        /* Re-executing the original instruction needs its byte back. */
        if(!jcc && p->armed) InterlockedExchange(&lc_error,10);
        e->ContextRecord->Eip=eip;
    }
    InterlockedExchange(&lc_patch_lock,0);
    InterlockedDecrement(&lc_in_handler);SetLastError(error);return EXCEPTION_CONTINUE_EXECUTION;
}
static int lc_point_order(const void *a,const void *b) {
    uintptr_t x=((const lc_point *)a)->address,y=((const lc_point *)b)->address;return x<y?-1:x>y;
}
static int lc_redirect_order(const void *a,const void *b) {
    uintptr_t x=((const lc_redirect_entry *)a)->trap,y=((const lc_redirect_entry *)b)->trap;return x<y?-1:x>y;
}
static BOOL lc_edge_uninstall(void) {
    if(lc_in_handler || InterlockedCompareExchange(&lc_patch_lock,1,0)) {lc_error=5;return FALSE;}
    BOOL ok=TRUE;
    for(unsigned i=0;i<lc_count;++i) if(!lc_redirect_release(&lc_sites[i])) ok=FALSE;
    for(unsigned i=0;i<lc_count;++i) lc_sites[i].roles&=LC_ROLE_REDIRECT;
    for(unsigned i=0;i<lc_point_count;++i) if(!lc_point_release(&lc_points[i])) ok=FALSE;
    if(ok) for(unsigned i=0;i<lc_count;++i) InterlockedExchange(&lc_sites[i].restored,1);
    InterlockedExchange(&lc_patch_lock,0);
    if(ok) {if(lc_handler && !RemoveVectoredExceptionHandler(lc_handler)) {lc_error=8;return FALSE;}lc_handler=NULL;lc_active=0;}
    return ok;
}
/* The stub page and the tables stay allocated after uninstall: nothing jumps
 * there once the displacements are back, and freeing gains nothing. */
static BOOL lc_edge_install(lc_site *sites,unsigned count,BYTE *image,size_t size,const lc_range *ranges,unsigned nranges) {
    if(lc_active || lc_handler || !lc_validate(sites,count,image,size,ranges,nranges)) return FALSE;
    lc_sites=sites;lc_count=count;lc_mode=LC_EDGE;lc_error=0;lc_first_thread=lc_other_thread=0;lc_last_tick=-1;
    memset(lc_edge_armed,0,sizeof(lc_edge_armed));lc_point_count=lc_redirect_count=0;
    HANDLE heap=GetProcessHeap();unsigned wide=0;
    for(unsigned i=0;i<count;++i) wide+=sites[i].length==6;
    if(count>lc_capacity || !lc_points) {
        if(lc_points)HeapFree(heap,0,lc_points);if(lc_redirects)HeapFree(heap,0,lc_redirects);
        lc_points=HeapAlloc(heap,HEAP_ZERO_MEMORY,(3*(size_t)count+1)*sizeof(lc_point));
        lc_redirects=HeapAlloc(heap,HEAP_ZERO_MEMORY,((size_t)count+1)*sizeof(lc_redirect_entry));
        lc_capacity=lc_points && lc_redirects?count:0;
    }
    if(!lc_points || !lc_redirects) {lc_error=11;return FALSE;}
    if(wide>lc_stub_bytes) {
        /* Older pages are never freed; nothing jumps there once their displacements are back. */
        size_t bytes=((size_t)wide+4095)&~(size_t)4095;DWORD old;
        BYTE *pages=VirtualAlloc(NULL,bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        if(!pages) {lc_error=11;return FALSE;}
        memset(pages,0xcc,bytes);
        if(!VirtualProtect(pages,bytes,PAGE_EXECUTE_READ,&old)) {lc_error=11;return FALSE;}
        lc_stub_pages=pages;lc_stub_bytes=bytes;
    }
    unsigned slot=0;
    for(unsigned i=0;i<count;++i) { lc_site *s=&sites[i];
        s->fall=s->taken=0;s->first_fall_tick=s->first_taken_tick=INT64_MIN;s->installed=s->restored=0;s->roles=0;s->redirect=0;
        for(unsigned k=0;k<3;++k)s->point[k]=-1;
        /* Availability; s->point[] marks a usable destination until the points exist. */
        if(s->exclusive[0] && lc_destination_ok(sites,count,s->address+s->length,image,size,ranges,nranges)) s->point[LC_POINT_FALL]=-2;
        if(s->exclusive[1] && lc_destination_ok(sites,count,s->target,image,size,ranges,nranges)) s->point[LC_POINT_TAKEN]=-2;
        if(s->length==6) s->redirect=(uintptr_t)lc_stub_pages+slot++;
        else if(s->trap) { intptr_t d=(intptr_t)s->trap-(intptr_t)(s->address+2);
            if(d>=-128 && d<=127 && lc_in_image(s->trap,1,image,size) && !lc_in_ranges(s->trap,1,ranges,nranges) &&
               !lc_site_byte(sites,count,s->trap) && lc_is_trap_byte(*(BYTE *)s->trap)) s->redirect=s->trap; }
        if(s->redirect) {lc_redirects[lc_redirect_count].trap=s->redirect;lc_redirects[lc_redirect_count++].site=i;}
    }
    /* A redirect byte serves one site: drop duplicates (the generator emits none). */
    qsort(lc_redirects,lc_redirect_count,sizeof(*lc_redirects),lc_redirect_order);
    for(unsigned i=0;i+1<lc_redirect_count;++i) if(lc_redirects[i].trap==lc_redirects[i+1].trap) {sites[lc_redirects[i].site].redirect=sites[lc_redirects[i+1].site].redirect=0;}
    for(unsigned i=0;i<count;++i) { lc_site *s=&sites[i];
        /* Initial roles; availability is "point >= -2" before the points exist. */
        LONG roles=0;
        if(!s->known[1]) roles|=s->redirect?LC_ROLE_REDIRECT:s->point[LC_POINT_TAKEN]==-2?LC_ROLE_TAKEN:LC_ROLE_JCC;
        if(!s->known[0]) roles|=s->point[LC_POINT_FALL]==-2?LC_ROLE_FALL:LC_ROLE_JCC;
        if(!(roles&LC_ROLE_REDIRECT)) s->redirect=0;
        if(!(roles&LC_ROLE_FALL)) s->point[LC_POINT_FALL]=-1;
        if(!(roles&LC_ROLE_TAKEN)) s->point[LC_POINT_TAKEN]=-1;
        uintptr_t where[3]={s->address,s->address+s->length,s->target};LONG need[3]={LC_ROLE_JCC,LC_ROLE_FALL,LC_ROLE_TAKEN};
        for(unsigned k=0;k<3;++k) if(roles&need[k]) {
            lc_point *p=&lc_points[lc_point_count++];memset(p,0,sizeof(*p));
            p->address=where[k];p->jcc=k==LC_POINT_JCC?(int)i:-1;p->dest=k==LC_POINT_JCC?-1:(int)i;p->taken=k==LC_POINT_TAKEN;
        }
        s->roles=roles;
        lc_edge_armed[0]+=!!(roles&LC_ROLE_JCC);lc_edge_armed[1]+=!!(roles&LC_ROLE_REDIRECT);
        lc_edge_armed[2]+=!!(roles&LC_ROLE_FALL);lc_edge_armed[3]+=!!(roles&LC_ROLE_TAKEN);
    }
    /* Merge points at one address: at most one jcc and one destination role. */
    qsort(lc_points,lc_point_count,sizeof(*lc_points),lc_point_order);
    unsigned n=0;
    for(unsigned i=0;i<lc_point_count;++i) {
        if(n && lc_points[n-1].address==lc_points[i].address) { lc_point *m=&lc_points[n-1],*q=&lc_points[i];
            if((m->jcc>=0 && q->jcc>=0) || (m->dest>=0 && q->dest>=0)) {lc_error=12;return FALSE;}
            if(q->jcc>=0) m->jcc=q->jcc; else {m->dest=q->dest;m->taken=q->taken;}
        } else lc_points[n++]=lc_points[i];
    }
    lc_point_count=n;
    for(unsigned i=0;i<n;++i) { lc_point *p=&lc_points[i];
        if(p->jcc>=0) sites[p->jcc].point[LC_POINT_JCC]=(int)i;
        if(p->dest>=0) sites[p->dest].point[p->taken?LC_POINT_TAKEN:LC_POINT_FALL]=(int)i;
    }
    /* Keep only the redirects that are armed, sorted by trap byte. */
    unsigned r=0;
    for(unsigned i=0;i<lc_redirect_count;++i) if(sites[lc_redirects[i].site].redirect) lc_redirects[r++]=lc_redirects[i];
    lc_redirect_count=r;
    lc_handler=AddVectoredExceptionHandler(1,lc_edge_exception);if(!lc_handler) return FALSE;
    lc_active=1;
    /* Read every original byte before writing any: points may share a site's bytes. */
    for(unsigned i=0;i<n;++i) lc_points[i].saved=*(BYTE *)lc_points[i].address;
    for(unsigned i=0;i<count;++i) if(sites[i].roles&LC_ROLE_REDIRECT) memcpy(sites[i].redirect_saved,(BYTE *)lc_redirect_at(&sites[i]),lc_redirect_length(&sites[i]));
    for(unsigned i=0;i<n;++i) { static const BYTE int3=0xcc;
        if(!lc_write(lc_points[i].address,&int3,1,&lc_points[i].protection)) {lc_edge_uninstall();return FALSE;}
        InterlockedExchange(&lc_points[i].armed,1);
    }
    for(unsigned i=0;i<count;++i) if(sites[i].roles&LC_ROLE_REDIRECT) { BYTE patched[4];lc_redirect_bytes(&sites[i],patched);
        if(!lc_write(lc_redirect_at(&sites[i]),patched,lc_redirect_length(&sites[i]),NULL)) {lc_edge_uninstall();return FALSE;}
    }
    for(unsigned i=0;i<count;++i) InterlockedExchange(&sites[i].installed,1);
    return TRUE;
}
#endif
