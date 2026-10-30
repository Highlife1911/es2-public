#include "es2/core/col.h"
#include "fixed.h"
/* COL traversal (docs/reverse-engineering/hit_tests.md, section "COL
 * traversal"). r holds what the original's
 * EAX/EDX/ECX hold at the next game call and at the return; lo() is a 16-bit
 * load that keeps the register's high word.
 *
 * Runtime COL layout (docs/formats/collision.md): descriptor {word group
 * count +0, dword groups +2}; group 10 bytes {word part +0, word shape count
 * +4, dword shapes +6}; shape 16 bytes {word sphere count +0, dword spheres
 * +2, word region +6, bounding center x/y/z words +8, radius word +0Eh}. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define VA(g,x) ((g)->image+(uint32_t)(x)-0x400000u)
/* The statics of the traversal (image addresses). */
enum { COL_MATRIX=0x4a980cu, COL_LENGTH=0x4a9810u, COL_RADIUS=0x4a9814u, COL_HIT=0x4a9818u, COL_REGION=0x4a981cu,
    COL_RESULT=0x4a981eu, COL_RESULT_REGION=0x4a9822u, COL_NO_NODE=0x6c56a8u };
static uint64_t col_pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static uint32_t col_lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static uint32_t col_sx(uint32_t x) { return (uint32_t)fx_s16(x); }
/* Direct calls name the original VA (observer key); the bridge gets the
 * address in this image. The node lookup passes its resolved target and
 * the vtable offset. Registers are updated in place. */
static void col_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,VA(g,va),a,n,r);
}
static void col_virtual(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,
    es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}

/* Original: DBSIM.EXE 0x40c9d4 */
uint64_t es2_col_query_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t col,uint32_t actor,
    uint32_t s,uint32_t matrix,uint32_t flags,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],0,entry[2]};
    uint32_t group=R32(col+2u);
    W16(VA(g,COL_REGION),0xffffu);
    W32(VA(g,COL_HIT),r.edx);
    r.edx=R32(s+4u);W32(VA(g,COL_LENGTH),r.edx);
    r.eax=col_lo(s,R16(s+8u));W16(VA(g,COL_RADIUS),r.eax);
    /* SI counts; the word count is reread and compared signed. */
    for(uint32_t i=0;fx_s16(i)<fx_s16(R16(col));++i,group+=0xau) {
        r.eax=flags;r.edx=matrix;r.ecx=actor;
        const uint32_t a[4]={group,actor,matrix,flags};
        col_call(g,0x40c8fcu,a,4,&r);
    }
    r.eax=R32(VA(g,COL_HIT));
    W32(VA(g,COL_RESULT),r.eax);
    r.edx=col_lo(r.edx,R16(VA(g,COL_REGION)));
    W16(VA(g,COL_RESULT_REGION),r.edx);
    r.ecx=r.eax?VA(g,COL_RESULT):0u;
    r.eax=r.ecx;
    return col_pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x40c8fc. The composed matrix (EBP-24h) lives at
 * g->scratch; its address goes to 0x4a980c like the original's frame
 * address. */
uint64_t es2_col_group_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t group,uint32_t actor,
    uint32_t matrix,uint32_t flags,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    const uint32_t frame=g->scratch;
    r.edx=matrix;
    r.eax=col_lo(group,R16(group));
    if(fx_s16(r.eax)<0) {
        /* rep movsd of the first 20 bytes, then the translation in the
         * order +1Ch, +14h, +18h. */
        r.eax=matrix;
        for(uint32_t k=0;k<20u;k+=4u)W32(frame+k,R32(matrix+k));
        r.ecx=0;r.eax=matrix+0x14u;
        r.edx=R32(r.eax+8u);W32(frame+0x1cu,r.edx);
        r.ecx=R32(r.eax);W32(frame+0x14u,r.ecx);
        r.eax=R32(r.eax+4u);W32(frame+0x18u,r.eax);
    } else {
        /* n = L.V20(L, lo(G, part)) on L = [H+4], H = [actor+34h]; the
         * entry ECX stays. */
        const uint32_t part=r.eax,instance=R32(actor+0x34u);
        r.edx=actor;
        r.eax=R32(instance+4u);
        r.edx=R32(r.eax);
        const uint32_t a[2]={r.eax,part};
        col_virtual(g,R32(r.edx+0x20u),0x20u,a,2,&r);
        if(r.eax&&fx_s16(R16(r.eax+4u))>-1) {
            r.eax=col_sx(R16(r.eax+4u))<<5;
            r.ecx=R32(instance+0x16u)+r.eax;
        } else r.ecx=VA(g,COL_NO_NODE);
        r.eax=r.ecx;r.edx=frame;
        const uint32_t c[3]={r.eax,matrix,frame};
        col_call(g,0x47fa20u,c,3,&r);
    }
    r.edx=group;
    r.eax=frame;W32(VA(g,COL_MATRIX),r.eax);
    uint32_t shape=R32(group+6u);
    for(uint32_t i=0;r.ecx=group,fx_s16(i)<fx_s16(R16(group+4u));++i,shape+=0x10u) {
        const uint32_t region=R16(shape+6u);
        r.eax=col_sx(region);
        r.edx=flags;
        if(!R16(flags+2u*r.eax))continue;
        const uint32_t a[1]={shape};
        col_call(g,0x40c8c8u,a,1,&r);
        r.ecx=shape;
        if(!r.eax)continue;
        r.edx=r.eax;
        W32(VA(g,COL_HIT),r.edx);
        W16(VA(g,COL_REGION),region);
    }
    r.eax=0;
    return col_pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x40c8c8 */
uint64_t es2_col_shape_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t shape,
    const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t hits=0;
    r.eax=col_lo(r.eax,R16(shape+0xeu));
    r.edx=shape+8u;
    const uint32_t a[2]={r.edx,r.eax};
    col_call(g,0x40c4c4u,a,2,&r);
    if((uint16_t)r.eax) {
        const uint32_t b[2]={shape,0};
        col_call(g,0x40c524u,b,2,&r);
        hits=r.eax;
    }
    r.eax=hits;
    return col_pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x40c4c4. The transformed center (EBP-0Ch) lives at
 * g->scratch. */
uint64_t es2_col_bound_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t center,uint32_t radius,
    const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    const uint32_t local=g->scratch;
    r.eax=R32(VA(g,COL_MATRIX));r.ecx=local;r.edx=center;
    const uint32_t a[3]={center,r.eax,local};
    col_call(g,0x4801d4u,a,3,&r);
    r.eax=col_sx(radius)+R32(VA(g,COL_LENGTH));
    /* JBE: the bound against the local Y, unsigned. */
    if(r.eax<=R32(local+4u))return col_pair(0,r.edx);
    const uint32_t z=R32(local+8u),x=R32(local);
    const uint32_t b[2]={x,z};
    col_call(g,0x47de4cu,b,2,&r);
    const uint32_t distance=r.eax;
    r.edx=col_sx(radius);
    r.ecx=col_sx(R16(VA(g,COL_RADIUS)));
    r.edx+=r.ecx;
    if(fx_s32(distance)>=fx_s32(r.edx))r.eax=0;
    else r.eax=col_lo(distance,1);
    return col_pair(r.eax,r.edx);
}
