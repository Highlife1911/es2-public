#include "es2/core/groups.h"
#include "fixed.h"
#include <stddef.h>
/* Group activation and current-order update
 * (docs/reverse-engineering/mission_runtime.md, section "Group activation
 * and current-order update"). Registers are tracked as the original
 * leaves them, because they become the entry registers of the next game
 * call and the returned EDX. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
/* A DBSIM VA (globals, direct callees) inside the image at g->image. */
#define GR_VA(va) (g->image+(uint32_t)(va)-0x400000u)
static uint64_t gr_pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
/* A 16-bit register write: the high word stays. */
static uint32_t gr_word(uint32_t high,uint32_t low) { return (high&0xffff0000u)|(uint16_t)low; }
static uint32_t gr_sx16(uint32_t x) { return (uint32_t)fx_s16(x); }
static int gr_less(uint32_t a,uint32_t b) { return fx_s32(a)<fx_s32(b); }
/* A direct call names the original VA (observed for the callee counters);
 * a virtual call is observed by its slot. Registers come back in place. */
static void gr_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,GR_VA(va),a,n,r);
}
static void gr_virtual(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}
/* The current order: [group + 4*[group+6C] + 44h], 32-bit arithmetic. */
static uint32_t gr_order(const es2_guest_memory *m,uint32_t group,uint32_t index) {
    return R32(group+4u*index+0x44u);
}

/* Original: DBSIM.EXE 0x4238a0 */
uint64_t es2_order_actor_registers(const es2_guest_memory *m,uint32_t group) {
    uint32_t index=R32(group+0x6cu),order=gr_order(m,group,index);
    if(!order)return gr_pair(0,index);
    uint32_t scope=gr_sx16(R16(order+0xcu));
    if(scope==0)return gr_pair(R32(R32(R32(order+0xeu)+0xcu)),index);
    if(scope-1u<3u)return gr_pair(R32(order+0xeu),index);
    return gr_pair(0,index);
}

/* Original: DBSIM.EXE 0x4238d4 */
uint64_t es2_order_point_registers(const es2_guest_memory *m,uint32_t group) {
    uint32_t order=gr_order(m,group,R32(group+0x6cu));
    if(order) {
        uint32_t scope=gr_sx16(R16(order+0xcu));
        if(scope==0) { uint32_t p=R32(R32(R32(order+0xeu)+0xcu))+0x26u; return gr_pair(p,p); }
        if(scope-1u<3u)return gr_pair(R32(order+0xeu)+0x26u,order);
    }
    uint32_t route=R32(group+6u);
    return gr_pair(R32(R32(route+2u)),route);
}

/* Original: DBSIM.EXE 0x41dbcc */
uint64_t es2_actor_destination_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t actor,const uint32_t entry[3]) {
    uint32_t state=gr_sx16(R16(actor+0x23eu));
    if(state==3u)return gr_pair(R32(actor+0x24cu)+0x26u,0);
    if(state==5u)return gr_pair(R32(actor+0x248u)+0x26u,0);
    if(state==6u) {
        uint32_t target=R32(actor+0x24cu);
        if(target)return gr_pair(target+0x26u,target+0x26u);
        return gr_pair(actor+0x240u,0);
    }
    uint32_t a[1]={R32(actor+0x45u)};
    es2_weapon_registers r={actor,a[0],entry[2]};
    gr_call(g,0x4238d4u,a,1,&r);
    return gr_pair(r.eax,r.edx);
}

/* 41b804 or 411cec for every member: EAX = member, EDX = the count
 * re-read before each call, ECX = the previous member after the first. */
static void gr_members(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t group,uint32_t va,es2_weapon_registers *r) {
    for(uint32_t i=0;;++i) {
        r->edx=gr_sx16(R16(group+0x10u));
        if(!gr_less(i,r->edx))break;
        uint32_t a[1]={R32(R32(group+0xcu)+4u*i)};
        r->eax=a[0];gr_call(g,va,a,1,r);r->ecx=a[0];
    }
}

/* Original: DBSIM.EXE 0x423f08 */
uint64_t es2_group_assess_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t group,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    gr_members(m,g,group,0x41b804u,&r);
    return gr_pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x423a74 */
uint64_t es2_order_update_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t group,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    r.eax=R32(group+0x6cu);
    uint32_t order=gr_order(m,group,r.eax),done=0;
    if(order) {
        uint32_t a[2]={group,order};
        gr_call(g,0x4239fcu,a,2,&r);
        if(r.eax) {
            r.edx=R32(group+0x6cu);done=1;r.eax=1;
            W8(group+r.edx+0x70u,1);
        } else {
            r.eax=R32(order+0x12u);
            if(r.eax&&R16(r.eax+0xau))done=1;
        }
    }
    if(done&&gr_less(R32(group+0x6cu),9)) {
        r.eax=R32(group+0x6cu);
        if(R32(group+4u*r.eax+0x48u)) {
            W32(group+0x6cu,R32(group+0x6cu)+1u);
            for(uint32_t i=0;;) {
                r.ecx=gr_sx16(R16(group+0x10u));
                if(!gr_less(i,r.ecx))break;
                r.edx=0;r.eax=R32(R32(group+0xcu)+4u*i);++i;
                W32(r.eax+0x52u,r.edx);
            }
        }
    }
    gr_members(m,g,group,0x411cecu,&r);
    return gr_pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x42354c. The frame f holds v at f-2Ch, w at f-18h
 * and m at f-8, as in the original. */
uint64_t es2_spawn_point_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t out,uint32_t distance,uint32_t angle,uint32_t check,const uint32_t entry[3]) {
    const uint32_t f=g->scratch+0x30u,v=f-0x2cu,w=f-0x18u,mat=f-8u;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t a[4],index=0,cursor,grid,esi;
    W32(v,0);W32(v+4u,distance);W32(v+8u,0);
    r.eax=R32(GR_VA(0x4d24e6u))+0x26u;
    cursor=R32(GR_VA(0x4a9af8u));
    r.ecx=R32(r.eax+8u);W32(out+8u,r.ecx);
    r.ecx=R32(r.eax);W32(out,r.ecx);
    r.eax=R32(r.eax+4u);W32(out+4u,r.eax);
    r.eax=0;W32(out+8u,r.eax);
    r.ecx=R32(GR_VA(0x4d24e6u));
    r.edx=gr_word(angle,angle+R16(r.ecx+0x10u));
    r.eax=r.edx;r.edx=mat;
    a[0]=r.eax;a[1]=mat;gr_call(g,0x47eb30u,a,2,&r);
    W16(w,0);W16(w+2u,0);W16(w+4u,0);
    r.ecx=R32(GR_VA(0x4a0be0u));grid=r.ecx;
    do {
        esi=0;
        r.eax=out;r.edx=mat;r.ecx=v;
        a[0]=v;a[1]=mat;a[2]=out;gr_call(g,0x47ffe8u,a,3,&r);
        r.eax=R32(GR_VA(0x4d24e6u))+0x26u;
        r.edx=R32(r.eax+8u);W32(out+8u,R32(out+8u)+r.edx);
        r.ecx=R32(r.eax);W32(out,R32(out)+r.ecx);
        r.eax=R32(r.eax+4u);W32(out+4u,R32(out+4u)+r.eax);
        W32(v+4u,R32(v+4u)+2000u);
        if(check) {
            for(;;) {
                r.eax=gr_word(r.eax,index);
                uint32_t taken=index;index=(uint16_t)(index+1u);
                uint32_t actor=0;
                if(fx_s16(taken)<fx_s16(R16(GR_VA(0x4a9afeu)))) { r.edx=cursor;cursor+=4u;actor=R32(r.edx); }
                if(!actor||esi)break;
                r.edx=R32(actor+0x45u);
                r.ecx=R32(r.edx+0x14u)!=0;
                if(r.ecx)continue;
                a[0]=actor;r.eax=R32(actor);
                gr_virtual(g,R32(r.eax+0x7cu),0x7c,a,1,&r);r.ecx=a[0];
                uint32_t radius=(uint16_t)r.eax;
                if(!radius)continue;
                a[0]=actor+0x26u;a[1]=out;gr_call(g,0x49288cu,a,2,&r);
                r.edx=(uint32_t)fx_s16(radius)+5000u;
                if(gr_less(r.eax,r.edx))esi=1;
            }
            index=0;r.eax=R32(GR_VA(0x4a9af8u));cursor=r.eax;
        }
        if(!esi) {
            a[0]=out;a[1]=5000;a[2]=0;gr_call(g,0x404ae4u,a,3,&r);
            esi=gr_sx16(r.eax);
        }
        if(!esi) {
            r.eax=w;r.edx=grid;
            a[0]=grid;a[1]=w;a[2]=out;gr_call(g,0x46ff90u,a,3,&r);
            esi=r.eax&0xffu;
        }
    } while(esi);
    return gr_pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x4236c4. The synthetic frame f = g->scratch+208:
 * runtime record f-24h (context word f-14h), allocated object f-4,
 * placements f-30h (modes 2/3) and f-3Ch (modes 4/5), copy f-48h. */
uint64_t es2_group_activate_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t group,uint32_t esi_high,const uint32_t entry[3]) {
    const uint32_t f=g->scratch+208u,p=f-0x30u,q=f-0x3cu,c=f-0x48u;
    es2_weapon_registers r={GR_VA(0x49a6bcu),entry[1],entry[2]};
    uint32_t a[4],mode;
    gr_call(g,0x49630cu,NULL,0,&r);
    r.edx=R32(group+0x14u);
    if(!R16(r.edx+0xau))goto out;
    r.ecx=R32(group+0x14u);
    mode=R16(r.ecx+2u);r.eax=gr_word(r.eax,mode);
    if(mode==2u||mode==3u) {
        if(R8(group+0x13u))goto out;
        W8(group+0x13u,1);
        r.edx=R32(group+0x14u);
        uint32_t mask=0x1fffu,base=0x1000u;
        if(R16(r.edx+2u)==2u) { mask=0x7fffu;base=0x4000u; }
        a[0]=GR_VA(0x4d2599u);gr_call(g,0x492ee0u,a,1,&r);
        r.eax=gr_word(r.eax,r.eax&mask);r.ecx=a[0];
        r.edx=r.eax;r.eax=gr_word(r.eax,base-r.edx);
        a[0]=p;a[1]=150000u;a[2]=r.eax;a[3]=0;r.ecx=p;
        gr_call(g,0x42354cu,a,4,&r);
        r.eax=R32(GR_VA(0x4a96aau));
        a[0]=r.eax;gr_call(g,0x471b30u,a,1,&r);r.ecx=a[0];
        uint32_t object=r.eax;W32(f-4u,object);
        if(!object)goto out;
        W16(f-0x14u,0x14);
        a[0]=object;a[1]=p;a[2]=group;r.eax=p;r.edx=object;
        gr_call(g,0x409b44u,a,3,&r);
        r.ecx=R32(GR_VA(0x6cbee4u));W32(r.ecx,R32(r.ecx)-4u);
        W16(f-0x14u,8);
        goto out;
    }
    r.eax=R32(group+0x14u);
    mode=R16(r.eax+2u);r.eax=gr_word(r.eax,mode);
    if(mode==4u||mode==5u) {
        uint32_t distance=150000u,esi;
        a[0]=GR_VA(0x4d2599u);gr_call(g,0x492ee0u,a,1,&r);
        if(mode==4u) {
            r.eax=gr_word(r.eax,r.eax&0x1fffu);r.ecx=a[0];
            esi=gr_word(esi_high<<16,0x1000u-r.eax);distance=90000u;esi=gr_word(esi,esi+0x8000u);
        } else {
            r.eax=gr_word(r.eax,r.eax&0x3fffu);r.ecx=a[0];
            esi=gr_word(esi_high<<16,0x2000u-r.eax);
        }
        a[0]=q;a[1]=distance;a[2]=esi;a[3]=1;r.eax=q;
        gr_call(g,0x42354cu,a,4,&r);
        r.edx=R32(group+0xcu);r.eax=R32(r.edx)+0x26u;
        r.edx=R32(q+8u);W32(r.eax+8u,r.edx);
        r.ecx=R32(q);W32(r.eax,r.ecx);
        r.edx=R32(q+4u);W32(r.eax+4u,r.edx);
        r.edx=esi-0x8000u;
        r.eax=R32(R32(group+0xcu))+0xcu;
        W16(r.eax+4u,r.edx);W16(r.eax+0x26u,0);
        for(uint32_t i=1;;++i) {
            r.eax=gr_sx16(R16(group+0x10u));
            if(!gr_less(i,r.eax))break;
            r.eax=R32(q+8u);W32(c+8u,r.eax);r.eax=c;
            r.edx=R32(q);W32(c,r.edx);
            r.ecx=R32(q+4u);W32(c+4u,r.ecx);
            r.edx=R32(group+0xcu);r.ecx=R32(r.edx+4u*i);
            a[0]=r.ecx;a[1]=c;r.eax=R32(r.ecx);
            gr_virtual(g,R32(r.eax+0x78u),0x78,a,2,&r);
            r.edx=R32(group+0xcu);r.eax=R32(r.edx+4u*i)+0x26u;
            r.edx=R32(c+8u);W32(r.eax+8u,r.edx);
            r.ecx=R32(c);W32(r.eax,r.ecx);
            r.edx=R32(c+4u);W32(r.eax+4u,r.edx);
        }
    }
    r.ecx=0;W32(group+0x14u,0);
out:
    r.eax=R32(f-0x24u);
    return gr_pair(r.eax,r.edx);
}

/* (docs/reverse-engineering/mission_runtime.md, section "member assessment
 * 41b804"). The liveness test of each pool object A, with the registers it
 * leaves: A+0A5h (EAX = A+92h, ECX = 1), A+0A4h (EDX = 1, ECX = 1), A+99h (EAX
 * = ECX = A+92h, then EDX = ECX = 1); a live object leaves EAX = A+92h and EDX
 * = ECX = 0. Then the pending activation of its group (EAX = [A+45h], EDX =
 * ([EAX+14h] != 0)). */
static int gr_assess_skip(const es2_guest_memory *m,uint32_t A,es2_weapon_registers *r) {
    r->eax=A+0x92u;
    if(R8(A+0xa5u)) { r->ecx=1;return 1; }
    r->edx=A+0x92u;
    if(R8(A+0xa4u)) { r->edx=1;r->ecx=1;return 1; }
    r->ecx=A+0x92u;
    if(R8(A+0x99u)) { r->edx=1;r->ecx=1;return 1; }
    r->edx=0;r->ecx=0;
    r->eax=R32(A+0x45u);r->edx=R32(r->eax+0x14u)?1u:0u;
    return r->edx!=0;
}
/* Same side: the signed category bytes [[A+45h]+12h] and [[M+45h]+12h]
 * (A's group read first); EAX = [M+45h], ECX/EDX the two bytes (with
 * swap = 1 the building pass's order: EAX/ECX). */
static int gr_assess_side(const es2_guest_memory *m,uint32_t A,uint32_t M,int swap,es2_weapon_registers *r) {
    const uint32_t ga=R32(A+0x45u),gm=R32(M+0x45u);
    const uint32_t ca=(uint32_t)(int32_t)(int8_t)R8(ga+0x12u),cm=(uint32_t)(int32_t)(int8_t)R8(gm+0x12u);
    if(swap) { r->ecx=ga;r->edx=gm;r->eax=ca;r->ecx=cm; }
    else { r->edx=ga;r->eax=gm;r->ecx=ca;r->edx=cm; }
    return (uint16_t)ca==(uint16_t)cm;
}
/* Original: DBSIM.EXE 0x41b804 (M), the member assessment of group_assess
 * (status 3, the player out of action). actor_safe(M) nonzero (AX):
 * return. Else sum the ratings of the live objects around M by side: MECH
 * pool [4a9b7a] within 45,000 by the chassis word [A+1F2h]+54h, FLYER pool
 * [4a9db9] within 50,000 and building pool [4a95a0] within 35,000 (those
 * whose chassis word +2Eh is set) by 500 each; friends F (minus 600, at
 * least 0) against hostiles H: while F < H, random_damage(M, 40h, 40h)
 * and H -= 400. Every game call through the bridge with the original's
 * registers; the cursors and sums are the original's frame slots. */
uint64_t es2_member_assess_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t M,
    const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[3];
    a[0]=M;gr_call(g,0x4137b4u,a,1,&r);r.ecx=a[0];
    if((uint16_t)r.eax)return gr_pair(r.eax,r.edx);
    uint32_t hostile=0,friendly=0,cursor=0;
    r.eax=0;r.edx=0;
    for(;;) {
        r.eax=cursor;r.edx=cursor;r.ecx=R32(GR_VA(0x4a9b7au));a[0]=r.ecx;a[1]=cursor;gr_call(g,0x471c70u,a,2,&r);
        cursor=r.eax;if(!cursor)break;const uint32_t A=cursor;
        if(gr_assess_skip(m,A,&r))continue;
        r.eax=M+0x26u;r.ecx=A+0x26u;a[0]=M+0x26u;a[1]=A+0x26u;gr_call(g,0x49288cu,a,2,&r);
        if(fx_s32(r.eax)>=45000)continue;
        if(gr_assess_side(m,A,M,0,&r)) { r.edx=R32(A+0x1f2u);r.ecx=gr_sx16(R16(r.edx+0x54u));friendly+=r.ecx; }
        else { r.ecx=R32(A+0x1f2u);r.eax=gr_sx16(R16(r.ecx+0x54u));hostile+=r.eax; }
    }
    r.edx=0;cursor=0;
    for(;;) {
        r.ecx=cursor;r.eax=cursor;r.edx=R32(GR_VA(0x4a9db9u));a[0]=r.edx;a[1]=cursor;gr_call(g,0x471c70u,a,2,&r);
        cursor=r.eax;if(!cursor)break;const uint32_t A=cursor;
        if(gr_assess_skip(m,A,&r))continue;
        r.eax=M+0x26u;r.ecx=A+0x26u;a[0]=M+0x26u;a[1]=A+0x26u;gr_call(g,0x49288cu,a,2,&r);
        if(fx_s32(r.eax)>=50000)continue;
        if(gr_assess_side(m,A,M,0,&r))friendly+=500u;else hostile+=500u;
    }
    r.edx=0;cursor=0;
    for(;;) {
        r.eax=cursor;r.edx=cursor;r.ecx=R32(GR_VA(0x4a95a0u));a[0]=r.ecx;a[1]=cursor;gr_call(g,0x471c70u,a,2,&r);
        cursor=r.eax;if(!cursor)break;const uint32_t A=cursor;
        if(gr_assess_skip(m,A,&r))continue;
        r.ecx=R32(A+0x1f2u);if(!R16(r.ecx+0x2eu))continue;
        r.eax=A+0x26u;r.edx=M+0x26u;a[0]=M+0x26u;a[1]=A+0x26u;gr_call(g,0x49288cu,a,2,&r);
        if(fx_s32(r.eax)>=35000)continue;
        if(gr_assess_side(m,A,M,1,&r))friendly+=500u;else hostile+=500u;
    }
    friendly-=600u;if(fx_s32(friendly)<0)friendly=0;
    while(fx_s32(friendly)<fx_s32(hostile)) {
        a[0]=M;a[1]=0x40;a[2]=0x40;gr_call(g,0x417a04u,a,3,&r);
        hostile-=400u;
    }
    return gr_pair(r.eax,r.edx);
}
