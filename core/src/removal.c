#include "es2/core/removal.h"
#include "fixed.h"
/* Object removal (docs/reverse-engineering/main_loop.md, section "object
 * removal"). r holds what the original's EAX/EDX/ECX hold at the next game
 * call. The flush's frame lives in the scratch at the original EBP offsets (f =
 * the synthetic EBP): the entry slot f-4, the runtime record at f-24h with its
 * context word at f-14h. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define VA(g,x) ((g)->image+(x)-0x400000u)
static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
/* Direct calls name the original VA (observer key); the bridge gets the
 * address in this image. The destructor passes its resolved target and
 * the vtable offset. */
static void rm_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,VA(g,va),a,n,r);
}
static void rm_virtual(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,
    es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}

/* Original: DBSIM.EXE 0x478644 */
uint64_t es2_removal_flush_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t L,uint32_t P,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_REMOVAL_FRAME;
    uint32_t a[2];
    es2_weapon_registers r={VA(g,0x4a1478u),entry[1],entry[2]};
    rm_call(g,0x49630cu,NULL,0,&r);
    uint32_t item=R32(L); /* 47865a: EBX, read once */
    for(uint32_t i=0;fx_s16(i)<fx_s16(R16(L+4u));++i,item+=4u) { /* 4786ae: count reread */
        r.eax=R32(item);W32(f-4u,r.eax);
        if(R32(f-4u)) { /* 478669 */
            r.edx=R32(f-4u);a[0]=r.edx;a[1]=0;rm_call(g,0x496406u,a,2,&r);
            r.ecx=R32(VA(g,0x6cbee4u));W32(r.ecx,R32(r.ecx)+r.eax);
            W16(f-0x14u,0x14u);
            r.eax=R32(f-4u);a[0]=r.eax;a[1]=2;r.edx=R32(r.eax);
            rm_virtual(g,R32(r.edx+8u),8u,a,2,&r);
            W16(f-0x14u,8u);
        }
        r.ecx=R32(item);r.eax=P; /* 47869b: the entry reread */
        a[0]=P;a[1]=r.ecx;rm_call(g,0x471bc8u,a,2,&r);
    }
    W16(L+4u,0);
    r.edx=R32(f-0x24u);
    *ecx=r.ecx;
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x471bc8 */
uint64_t es2_pool_free_registers(const es2_guest_memory *m,uint32_t P,uint32_t O,uint32_t *ecx) {
    W16(P+2u,R16(P+2u)-1u);W16(P,R16(P)+1u);
    const uint32_t N=O-8u;
    uint32_t c=R32(N+4u);
    if(c)W32(c,R32(N)); /* 471be1 */
    else W32(P+0xeu,R32(N));
    c=R32(N);
    if(c)W32(c+4u,R32(N+4u)); /* 471bf2 */
    W32(N,R32(P+0xau));W32(P+0xau,N);
    c=R32(N);
    if(c)W32(c+4u,N); /* 471c08 */
    W32(N+4u,0);
    *ecx=c;
    return pair(N,0);
}

/* Original: DBSIM.EXE 0x478628 */
uint64_t es2_removal_append_registers(const es2_guest_memory *m,uint32_t L,uint32_t v,uint32_t *ecx) {
    const uint32_t count=R16(L+4u);
    W16(L+4u,count+1u);
    const uint32_t index=(uint32_t)fx_s16(count),items=R32(L);
    W32(items+4u*index,v);
    *ecx=index;
    return pair(items,v);
}
