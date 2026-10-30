#include "es2/core/obstacle.h"
#include "fixed.h"
/* Static-obstacle test and convoy collision test
 * (docs/reverse-engineering/mission_runtime.md, section "Static-obstacle list
 * 404ae4", and convoy.md, section "collision test 0x46a614"). r holds what the
 * original's EAX/EDX/ECX hold at the next game call; ob_lo() is a 16-bit load
 * that keeps the register's high word. Memory is read again wherever the
 * original reads it again. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
/* A DBSIM VA (globals) inside the image at g->image. */
#define VA(va) (g->image+(uint32_t)(va)-0x400000u)

static uint64_t ob_pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static uint32_t ob_lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static uint32_t ob_sx(uint32_t x) { return (uint32_t)fx_s16(x); }
/* Direct calls name the original VA (observer key); the bridge gets the
 * address in this image. A virtual call passes its resolved target. */
static void ob_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,VA(va),a,n,r);
}
static void ob_virtual(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,
    es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}
/* 0x47df04 computed here (its only callers are 427c68, 46aa60 and
 * 41fd37, all own code): the words M+2 and M+4 exchanged (MOV CX,[M+2];
 * XCHG [M+4],CX; MOV [M+2],CX); EAX = M, CX = the old [M+4] over ECX's
 * high word, EDX unchanged. The call is only observed (call_47df04). */
static void ob_swap(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t M,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,0x47df04u,0);
    const uint32_t w1=R16(M+2u),w2=R16(M+4u);
    W16(M+4u,w1);W16(M+2u,w2);
    r->eax=M;r->ecx=ob_lo(r->ecx,w2);
}

/* Original: DBSIM.EXE 0x404ae4. f is the original's EBP: the list at
 * f-23Ch, the cursor slot f-0Ch, the count word f-6, the iterator f-4. */
uint64_t es2_obstacle_test_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t point,uint32_t radius,uint32_t exclude,const uint32_t entry[3]) {
    const uint32_t list=g->scratch,f=list+ES2_OBSTACLE_FRAME;
    es2_weapon_registers r={0,list,entry[2]};
    uint32_t a[4];
    W32(f-4u,0);W16(f-6u,0);W32(f-0xcu,list);
    for(;;) {
        a[1]=R32(f-4u);a[0]=R32(VA(0x4a95a0u));r.ecx=a[0];
        ob_call(g,0x471c70u,a,2,&r);
        W32(f-4u,r.eax);
        const uint32_t o=r.eax;
        if(!o)break;
        if(exclude==o)continue;
        r.ecx=R32(o+0x1f2u);
        if(R16(r.ecx+6u)) {
            r.edx=R32(o+0x1f2u);
            if(R16(r.edx+4u)==0xffffu)r.eax=0;
            else {
                r.ecx=R32(o+0x205u);
                if(fx_s16(R16(r.ecx+5u))>=2)r.eax=0;
                else { r.eax=o+0x92u;r.eax=R8(r.eax+7u)?1u:0u; }
            }
            if(!(uint16_t)r.eax)continue;
        }
        r.edx=R32(o+0x1f2u);
        if(R16(r.edx+4u)==0xffffu) {
            r.eax=o+0x92u;
            if(R8(r.eax+7u)&&fx_s16(R16(r.edx+0x12u))<=1)continue;
        }
        /* Through the cursor slot: the 141st append overwrites it. */
        r.edx=R32(f-0xcu);W32(r.edx,o);
        W16(f-6u,R16(f-6u)+1u);
        W32(f-0xcu,R32(f-0xcu)+4u);
    }
    r.edx=ob_lo(r.edx,R16(f-6u));r.ecx=list;
    r.eax=ob_lo(r.eax,radius);
    a[0]=point;a[1]=r.eax;a[2]=list;a[3]=r.edx;
    r.edx=point;
    ob_call(g,0x427c68u,a,4,&r);
    return ob_pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x46a614 */
uint64_t es2_convoy_collide_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t object,const uint32_t entry[3]) {
    es2_weapon_registers r={R32(VA(0x4a9af8u)),0,entry[2]};
    uint32_t a[3],cursor=r.eax,index=0;
    r.edx=R32(object);a[0]=object;
    ob_virtual(g,R32(r.edx+0x5cu),0x5c,a,1,&r);r.ecx=object;
    const uint32_t radius=(uint16_t)r.eax;
    for(;;) {
        const uint32_t old=index;
        r.edx=ob_lo(r.edx,old);index=(uint16_t)(index+1u);
        uint32_t actor=0;
        if(fx_s16(old)<fx_s16(R16(VA(0x4a9afeu)))) { r.ecx=cursor;cursor+=4u;actor=R32(r.ecx); }
        if(!actor)break;
        if(object==actor)continue;
        r.ecx=R32(actor+0x45u);
        r.eax=R32(r.ecx+0x14u)!=0;
        if(r.eax)continue;
        a[0]=actor;r.edx=R32(actor);
        ob_virtual(g,R32(r.edx+0x7cu),0x7c,a,1,&r);r.ecx=actor;
        const uint32_t s=r.eax;
        if(!(uint16_t)s)continue;
        r.eax=actor+0x26u;r.edx=object+0x26u;
        a[0]=object+0x26u;a[1]=actor+0x26u;
        ob_call(g,0x49288cu,a,2,&r);
        r.ecx=ob_sx(radius)+ob_sx(s);r.edx=ob_sx(s);
        if(fx_s32(r.eax)>=fx_s32(r.ecx))continue;
        a[0]=actor;r.eax=R32(actor);
        ob_virtual(g,R32(r.eax+0x68u),0x68,a,1,&r);
        return ob_pair(ob_lo(r.eax,1),r.edx);
    }
    r.eax=ob_lo(r.eax,radius);
    a[0]=object+0x26u;a[1]=r.eax;a[2]=object;
    ob_call(g,0x404ae4u,a,3,&r);
    if((uint16_t)r.eax)r.eax=ob_lo(r.eax,1);
    return ob_pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x404bc0 (ai.md, section "pool object
 * query 404bc0"). f is the original's EBP: the list at f-240h, the cursor
 * slot f-10h, the result dword f-0Ch, the never-written word f-8, the
 * count word f-6, the iterator f-4. Only DI of the filter slot is read. */
uint64_t es2_pool_query_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t out,uint32_t record,uint32_t filter,const uint32_t entry[3]) {
    const uint32_t list=g->scratch,f=list+ES2_POOL_QUERY_FRAME;
    const uint16_t di=(uint16_t)filter;
    es2_weapon_registers r={0,0,list};
    uint32_t a[4];
    (void)entry;
    W32(f-4u,0);W16(f-6u,0);W32(f-0xcu,0);W32(f-0x10u,list);
    for(;;) {
        a[1]=R32(f-4u);a[0]=R32(VA(0x4a95a0u));r.ecx=a[0];
        ob_call(g,0x471c70u,a,2,&r);
        W32(f-4u,r.eax);
        const uint32_t o=r.eax;
        if(!o)break;
        if(!(di&0x8000u)) {
            r.eax=R32(o+0x45u);r.edx=(uint32_t)(int32_t)(int8_t)R8(r.eax+0x12u);
            if(di!=(uint16_t)r.edx)continue;
        }
        r.ecx=R32(o+0x1f2u);
        if(R16(r.ecx+6u)) {
            r.edx=R32(o+0x1f2u);
            if(R16(r.edx+4u)==0xffffu)r.eax=0;
            else {
                r.ecx=R32(o+0x205u);
                if(fx_s16(R16(r.ecx+5u))>=2)r.eax=0;
                else { r.eax=o+0x92u;r.eax=R8(r.eax+7u)?1u:0u; }
            }
            if(!(uint16_t)r.eax)continue;
        }
        /* Through the cursor slot: the 141st append overwrites it. */
        r.edx=R32(f-0x10u);W32(r.edx,o);
        W16(f-6u,R16(f-6u)+1u);
        W32(f-0x10u,R32(f-0x10u)+4u);
    }
    if(R16(f-6u)) {
        r.edx=ob_lo(r.edx,R16(f-6u));r.ecx=list;
        r.eax=record;
        a[0]=out;a[1]=record;a[2]=list;a[3]=r.edx;
        r.edx=out;
        ob_call(g,0x427da8u,a,4,&r);
        if((uint16_t)r.eax)W32(f-0xcu,1);
    }
    r.eax=ob_lo(r.eax,R16(f-0xcu));
    return ob_pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x427c68 (mission_runtime.md, section "collision walk
 * 427c68 and shape cell 427360"). f is the original's EBP:
 * D f-24h, R f-1Ch, M f-0Ch; the index word f-2 is kept in i (no callee
 * gets its address). The count and P are argument slots, read as words /
 * dwords where the original reads them. */
uint64_t es2_collision_walk_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t point,uint32_t radius,uint32_t list,uint32_t count,const uint32_t entry[3]) {
    const uint32_t f=g->scratch+ES2_COLLISION_WALK_FRAME,d=f-0x24u,rv=f-0x1cu,mv=f-0xcu;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t a[3],cursor=list;
    uint16_t i=0;
    (void)radius;
    if(fx_s16(i)>=fx_s16(count))return ob_pair(0,r.edx);
    do {
        const uint32_t e=R32(cursor);
        uint32_t s=0;
        if(R32(e+0x34u)) { r.eax=R32(e+0x34u);s=R32(r.eax+4u); }
        r.eax=e+0x26u;r.edx=point;
        a[0]=point;a[1]=e+0x26u;ob_call(g,0x4928d0u,a,2,&r);
        r.ecx=ob_sx(R16(s+8u));
        if(fx_s32(r.eax)<fx_s32(r.ecx)) {
            const uint32_t c=e+0xcu;
            r.edx=c;
            if(!R16(c+0x26u)) {
                r.ecx=c;r.eax=c+6u;
                a[0]=c;a[1]=c+6u;ob_call(g,0x47ebb8u,a,2,&r);
                r.ecx=c;W16(c+0x26u,1);
            }
            r.eax=c+6u;
            r.edx=ob_lo(r.edx,R16(r.eax));W16(mv,r.edx);
            r.ecx=ob_lo(r.ecx,R16(r.eax+2u));W16(mv+2u,r.ecx);
            r.edx=ob_lo(r.edx,R16(r.eax+4u));W16(mv+4u,r.edx);
            r.eax=ob_lo(r.eax,R16(r.eax+6u));W16(mv+6u,r.eax);
            r.ecx=mv;
            ob_swap(m,g,mv,&r);
            r.eax=point;r.ecx=mv;
            r.edx=R32(r.eax);W32(d,r.edx);
            r.ecx=R32(r.eax+4u);r.eax=e+0x26u;W32(d+4u,r.ecx);
            r.edx=R32(r.eax);W32(d,R32(d)-r.edx);
            r.edx=mv;r.ecx=R32(r.eax+4u);r.eax=rv;W32(d+4u,R32(d+4u)-r.ecx);
            r.ecx=d;
            a[0]=d;a[1]=mv;a[2]=rv;ob_call(g,0x47ffe8u,a,3,&r);
            r.eax=rv;
            a[0]=s;a[1]=rv;a[2]=0;ob_call(g,0x427360u,a,3,&r);
            if(r.eax)return ob_pair(r.eax,r.edx);
        }
        ++i;cursor+=4u;
        r.edx=ob_lo(r.edx,i);
    } while(fx_s16(i)<fx_s16(count));
    return ob_pair(0,r.edx);
}

/* Original: DBSIM.EXE 0x427360. f is the original's EBP: Q f-0Ch,
 * the shifted origin words f-4 and f-2. Only the low word of the third
 * slot is read (DX at 4273b1); the pushed dword keeps EDX's high word. */
uint64_t es2_shape_cell_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t shape,uint32_t point,uint32_t word,const uint32_t entry[3]) {
    const uint32_t f=g->scratch+ES2_SHAPE_CELL_FRAME,q=f-0xcu;
    es2_weapon_registers r={shape,entry[1],entry[2]};
    uint32_t a[3];
    if(!R16(shape+0x2au))return ob_pair(0,r.edx);
    r.ecx=R32(point);W32(q,r.ecx);
    r.edx=R32(point+4u);W32(q+4u,r.edx);
    r.edx=ob_lo(r.edx,R16(shape+0x32u));
    const unsigned n=r.edx&31u;
    const uint16_t x=(uint16_t)((uint32_t)R16(shape+0x2eu)<<n);W16(f-4u,x);
    const uint16_t y=(uint16_t)((uint32_t)R16(shape+0x30u)<<n);W16(f-2u,y);
    r.edx=ob_sx(R16(f-4u));r.ecx=ob_sx(y);
    W32(q,R32(q)+r.edx);W32(q+4u,R32(q+4u)+r.ecx);
    r.ecx=q;r.edx=ob_lo(r.edx,word);
    a[0]=shape;a[1]=q;a[2]=r.edx;ob_call(g,0x427238u,a,3,&r);
    return ob_pair(r.eax,r.edx);
}
