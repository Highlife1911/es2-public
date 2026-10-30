#include "es2/core/pool_iter.h"
#include "fixed.h"
/* Pool iterator (docs/reverse-engineering/main_loop.md, section "pool iterator
 * 471c70 / 471c18"). Guest memory is read in the original's order. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define VA(va) (g->image+(uint32_t)(va)-0x400000u)

static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
/* A link value becomes an object pointer: L+8, or 0 for L = 0. */
static uint32_t object_of(uint32_t link) { return link?link+8u:0u; }
static void pq_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,VA(va),a,n,r);
}

/* Original: DBSIM.EXE 0x471c18 */
uint64_t es2_pool_next_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t pool,uint32_t object,
    const uint32_t entry[3]) {
    (void)entry;
    if(!object)return pair(object_of(R32(pool+0xeu)),0u);
    const uint32_t node=object-8u;
    uint32_t edx=node;
    if(node<0x100000u) {
        /* 471c3b: the debug report; pushes [P+4], [P+2], [P] (sign-extended
         * words) and the dword [P+6], in that order of reads. */
        uint32_t a[6];
        a[5]=sx(R16(pool+4u));
        a[4]=sx(R16(pool+2u));
        a[3]=sx(R16(pool));
        a[2]=R32(pool+6u);
        a[1]=VA(0x4a0cd8u);
        a[0]=g->scratch;
        es2_weapon_registers r={a[0],a[4],a[3]};
        pq_call(g,0x496796u,a,6,&r);
        edx=r.edx;
    }
    return pair(object_of(R32(node)),edx);
}

/* Original: DBSIM.EXE 0x471c70 */
uint64_t es2_pool_prev_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t pool,uint32_t object,
    const uint32_t entry[3]) {
    if(object)return pair(object_of(R32(object-8u+4u)),entry[1]);
    /* 471c84: step from the head until a step returns 0; ESI keeps the
     * last nonzero result. */
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t last=0,cursor=0;
    for(;;) {
        const uint32_t a[2]={pool,cursor};
        pq_call(g,0x471c18u,a,2,&r);
        cursor=r.eax;
        if(!cursor)break;
        last=cursor;
    }
    return pair(last,r.edx);
}
