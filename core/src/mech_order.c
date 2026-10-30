#include "es2/core/mech_order.h"
#include "fixed.h"
/* The MECH order handler 420AD4 and the wingman order 431610
 * (docs/reverse-engineering/mech_order.md). The register struct r follows EAX,
 * EDX and ECX wherever a game call or the result sees them; EDI (the answer
 * code, of which the original often sets only the low word) and ESI (the
 * result) are kept as full dwords. Memory is read again wherever the original
 * reads it again, because the bridged callees may change it. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
/* A DBSIM VA (globals, direct callees, pushed script tables) inside the
 * image at g->image. */
#define VA(va) (g->image+(uint32_t)(va)-0x400000u)

static uint64_t mo_pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
/* A 16-bit register write: the high word stays. */
static uint32_t mo_lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static int mo_less(uint32_t a,uint32_t b) { return fx_s32(a)<fx_s32(b); }
/* A direct call names the original VA; a virtual call is observed by slot. */
static void mo_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,VA(va),0);
    g->call(g->context,VA(va),a,n,r);
}
static void mo_virtual(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}
/* The disabled test the original inlines nine times: the first of the
 * bytes X+A5h, X+A4h, X+99h that is set (1, 2, 3), or 0. Each copy leaves
 * its own register values; the callers set them. */
static unsigned mo_down(const es2_guest_memory *m,uint32_t X) {
    if(R8(X+0xa5u))return 1;
    if(R8(X+0xa4u))return 2;
    return R8(X+0x99u)?3u:0u;
}
/* The order point: A+240h..247h <- R+6..R+0Dh, two dwords. */
static void mo_point(const es2_guest_memory *m,uint32_t A,uint32_t R,es2_weapon_registers *r) {
    r->eax=A+0x240u;r->edx=R+6u;r->ecx=R32(R+6u);W32(A+0x240u,r->ecx);
    r->ecx=R32(R+10u);W32(A+0x244u,r->ecx);
}
/* 411CB4 (target) into A+250h, then 41C0F4 (A, target); the target is
 * passed in by the caller and re-read for the second call where the
 * original re-reads it. */
static void mo_class(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,uint32_t target,es2_weapon_registers *r) {
    uint32_t a[1]={target};
    mo_call(g,0x411cb4u,a,1,r);r->ecx=target;  /* POP ECX */
    W16(A+0x250u,r->eax);
}

/* Original: DBSIM.EXE 0x420ad4 */
uint64_t es2_mech_order_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t vehicle,uint32_t record,uint32_t mode,const uint32_t entry[3]) {
    const uint32_t A=vehicle,R=record,B=A+0x92u;
    es2_weapon_registers r={R,R16(R),entry[2]};
    uint32_t edi=0xffffffffu,esi=0,a[3];
    unsigned k;
    switch(r.edx) {
    case 0x0: { /* attack the cockpit target */
        r.ecx=R32(VA(0x49b088u));edi=R32(r.ecx+0x210u);W8(B+8u,0);r.eax=B;
        k=mo_down(m,A);
        if(k==1)r.ecx=1;
        else if(k) { r.edx=1;r.ecx=1; }
        else { r.ecx=0;r.edx=0; }
        if(k) { edi=mo_lo(edi,0x1b);break; }
        r.eax=R32(A+0x4du)+8u;r.edx=r.eax;
        if(R8(r.edx+4u)) { edi=mo_lo(edi,0xd);break; }
        if(!edi) { edi=0x12u;break; }
        const uint32_t T=edi;
        r.eax=T+0x92u;r.edx=r.eax;
        if(R8(T+0xa4u))r.edx=1;
        else { r.ecx=T+0x92u;r.eax=r.ecx;r.edx=R8(T+0x99u)?1u:0u; }
        if((uint16_t)r.edx) { edi=mo_lo(edi,0x12);break; }
        if(R16(A+0x23eu)==4u&&T==R32(A+0x248u)) { edi=mo_lo(edi,0xf);break; }
        W16(A+0x23eu,4);W32(A+0x248u,T);
        mo_class(m,g,A,T,&r);
        a[0]=A;a[1]=T;mo_call(g,0x41c0f4u,a,2,&r);
        edi=mo_lo(edi,0x11);esi=1;r.eax=0;W32(A+0x281u,0);
        break; }
    case 0x1: { /* break off: the cockpit target, if it is A's target */
        r.eax=R32(VA(0x49b088u));r.edx=B;r.eax=R32(r.eax+0x210u);r.ecx=B;
        k=mo_down(m,A);
        if(k==1)r.ecx=1;
        else if(k) { r.edx=1;r.ecx=1; }
        else { r.edx=0;r.ecx=0; }
        if(k) { edi=mo_lo(edi,0x1e);break; }
        const uint32_t T=r.eax;
        if(!T) { edi=mo_lo(edi,0x12);break; }
        if(R16(A+0x23eu)==4u&&T==R32(A+0x248u))W16(A+0x23eu,0);
        r.edx=B;r.ecx=B;
        if(R8(B+8u)) { edi=mo_lo(edi,0xf);break; }
        edi=mo_lo(edi,0x16);esi=1;W8(B+8u,1);
        r.edx=R32(A+0x4du)+8u;r.ecx=r.edx;
        if(!R8(r.ecx+1u))break;
        if(T!=R32(A+0x1a4u))break;
        r.eax=R32(A+0x4du)+8u;
        if(R8(r.eax+2u)) { r.edx=0;W32(A+0x52u,0);break; }
        a[0]=A;a[1]=1;a[2]=0;mo_call(g,0x411fa0u,a,3,&r);
        const uint32_t t=r.eax;
        if(t) { r.edx=t;a[0]=A;a[1]=t;mo_call(g,0x41c0f4u,a,2,&r); }
        else { a[0]=A;mo_call(g,0x41eb34u,a,1,&r);r.ecx=A; }
        break; }
    case 0x2: { /* engage the nearest attacker of the player [4A9B84h] */
        r.ecx=B;r.eax=B;
        k=mo_down(m,A);
        if(k==1)r.eax=1;
        else if(k) { r.edx=B;r.ecx=1;r.eax=1; }
        else { r.edx=B;r.ecx=0;r.eax=0; }
        if(k) { edi=mo_lo(edi,0x1b);break; }
        r.eax=0;r.edx=0;
        uint32_t cursor=0,best=0,nearest=60000u;
        for(;;) {
            r.eax=cursor;r.edx=cursor;r.ecx=R32(VA(0x4a9b7au));
            a[0]=r.ecx;a[1]=cursor;mo_call(g,0x471c70u,a,2,&r);
            cursor=r.eax;edi=r.eax;
            if(!cursor)break;
            const uint32_t o=cursor;
            r.eax=o+0x92u;
            k=mo_down(m,o);
            if(k==1) { r.ecx=1;continue; }
            if(k==2) { r.edx=1;r.ecx=1;continue; }
            if(k==3) { r.ecx=1;r.edx=1;continue; }
            r.edx=0;r.ecx=0;
            r.eax=R32(o+0x1a4u);
            if(r.eax!=R32(VA(0x4a9b84u)))continue;
            r.ecx=R32(VA(0x4a9b84u))+0x26u;r.edx=o+0x26u;
            a[0]=r.ecx;a[1]=r.edx;mo_call(g,0x49288cu,a,2,&r);
            if(mo_less(r.eax,nearest)) { nearest=r.eax;best=o; }
        }
        if(!best) { edi=0x1du;break; }
        r.eax=R32(A+0x1a4u);
        if(r.eax==best) {
            r.edx=R32(A+0x4du)+8u;r.ecx=r.edx;
            if(R8(r.ecx+1u)) { edi=0x1au;break; }
        }
        W16(A+0x23eu,4);r.eax=best;esi=1;W32(A+0x248u,best);edi=0xbu;r.edx=best;
        mo_class(m,g,A,best,&r);
        a[0]=A;a[1]=best;mo_call(g,0x41c0f4u,a,2,&r);
        r.eax=0;W32(A+0x281u,0);
        break; }
    case 0x4:case 0x10: /* hold */
        r.edx=B;r.ecx=B;
        if(R8(B+4u))edi=mo_lo(edi,0xf);
        else { edi=mo_lo(edi,0x26);r.eax=B;r.edx=B;W8(B+4u,1); }
        esi=1;r.eax=B;r.edx=B;W8(B+0x20u,1);
        break;
    case 0x7:case 0x11: /* release the hold */
        r.eax=B;r.edx=B;
        if(R8(B+4u)) { r.ecx=B;W8(B+4u,0); }
        esi=1;edi=mo_lo(edi,0x28);W8(B+0x20u,0);
        break;
    case 0x5: { /* engage at will */
        r.edx=B;r.eax=B;r.ecx=B;W8(B+8u,0);r.edx=B;
        k=mo_down(m,A);
        if(k==1)r.edx=1;
        else if(k) { r.eax=1;r.edx=1; }
        else { r.eax=0;r.edx=0; }
        if(k) { edi=mo_lo(edi,0x1b);break; }
        r.eax=R32(A+0x4du)+8u;r.edx=r.eax;
        if(R8(r.edx+1u)) { r.eax=B;esi=1;r.edx=B;edi=mo_lo(edi,0x20);W8(B+0x24u,1);break; }
        a[0]=A;a[1]=1;a[2]=0;mo_call(g,0x411fa0u,a,3,&r);
        const uint32_t t=r.eax;
        if(t) {
            int resumed=0;
            const uint32_t G=R32(A+0x45u);r.eax=G;r.edx=R32(G+0x6cu);
            if(R32(G+4u*r.edx+0x44u)) { r.ecx=R32(G+0x6cu);r.eax=R32(G+4u*r.ecx+0x44u);r.edx=mo_lo(r.edx,R16(r.eax)); }
            else r.edx=mo_lo(r.edx,0xb);
            r.eax=(uint16_t)r.edx==2u;
            if(r.eax) {
                r.ecx=R32(A+0x45u);a[0]=r.ecx;mo_call(g,0x4238a0u,a,1,&r);r.ecx=a[0];
                const uint32_t X=r.eax;r.eax=X+0x26u;r.edx=A+0x26u;
                a[0]=A+0x26u;a[1]=X+0x26u;mo_call(g,0x49288cu,a,2,&r);
                if(mo_less(r.eax,100000u)) {
                    W16(A+0x23eu,0);a[0]=A;mo_call(g,0x41eb34u,a,1,&r);r.ecx=A;resumed=1;
                }
            }
            if(!resumed) { W16(A+0x23eu,0);r.ecx=t;a[0]=A;a[1]=t;mo_call(g,0x41c0f4u,a,2,&r); }
        }
        r.eax=B;edi=mo_lo(edi,0x1c);r.edx=B;esi=1;W8(B+0x24u,1);
        break; }
    case 0x8: /* stop engaging at will */
        r.eax=R32(A+0x4du)+8u;r.edx=r.eax;
        if(R8(r.edx+1u)) { a[0]=A;mo_call(g,0x41eb34u,a,1,&r);r.ecx=A;W16(A+0x23eu,0); }
        r.eax=B;r.ecx=0;edi=mo_lo(edi,0xc);r.edx=B;esi=1;W8(B+0x24u,0);W32(A+0x281u,0);
        break;
    case 0xb: { /* attack the record's target R+12h */
        r.eax=B;r.ecx=B;r.edx=B;W8(B+8u,0);
        k=mo_down(m,A);
        if(k==1)r.eax=1;
        else if(k) { r.ecx=1;r.eax=1; }
        else { r.ecx=0;r.eax=0; }
        if(k) { edi=mo_lo(edi,0x1b);break; }
        if(R16(A+0x23eu)==4u) {
            r.eax=R;r.edx=R32(R+0x12u);
            if(r.edx==R32(A+0x248u)) { edi=mo_lo(edi,0xf);break; }
        }
        const uint32_t T=R32(R+0x12u);
        r.eax=T;r.edx=T+0x92u;r.ecx=r.edx;
        if(R8(T+0xa4u))r.ecx=1;
        else { r.eax=T+0x92u;r.edx=r.eax;r.ecx=R8(T+0x99u)?1u:0u; }
        if((uint16_t)r.ecx) { edi=mo_lo(edi,0x1d);break; }
        W16(A+0x23eu,4);r.eax=R;r.edx=R32(R+0x12u);W32(A+0x248u,r.edx);
        r.ecx=R;r.eax=R32(R+0x12u);
        mo_class(m,g,A,r.eax,&r);
        r.edx=R;r.ecx=R32(R+0x12u);a[0]=A;a[1]=r.ecx;mo_call(g,0x41c0f4u,a,2,&r);
        edi=mo_lo(edi,0x11);esi=1;r.eax=0;W32(A+0x281u,0);
        break; }
    case 0xe: /* move to the point R+6 (state 1) */
    case 0xd: { /* the same with state 2 */
        const uint32_t state=r.edx==0xeu?1u:2u;
        r.eax=B;
        if(state==1u)r.edx=B;
        if(R8(B+0x12u))edi=mo_lo(edi,0x1b);
        else {
            int near=0;
            if(R16(A+0x23eu)==state) {
                r.eax=A+0x26u;r.edx=R+6u;a[0]=R+6u;a[1]=A+0x26u;mo_call(g,0x49288cu,a,2,&r);
                near=mo_less(r.eax,2000u);
            }
            if(near)edi=mo_lo(edi,0x20);
            else {
                r.eax=A+0x4du;a[0]=A+0x4du;a[1]=VA(0x499594u);mo_call(g,0x413e50u,a,2,&r);
                W16(A+0x23eu,state);edi=mo_lo(edi,0x2a);esi=1;
            }
        }
        mo_point(m,A,R,&r);
        break; }
    case 0xc: { /* guard the record's object R+12h, or the point R+6 */
        r.eax=0;
        int decided=0;
        if(R32(A+0x24cu)) {
            r.edx=R;
            if(R32(R+0x12u)) { r.eax=R;r.ecx=R32(R+0x12u);r.eax=r.ecx==R32(A+0x24cu);decided=1; }
        }
        if(!decided&&!R32(A+0x24cu)) {
            r.edx=R;
            if(!R32(R+0x12u)) {
                r.ecx=R+6u;r.eax=A+0x240u;a[0]=A+0x240u;a[1]=R+6u;mo_call(g,0x4928d0u,a,2,&r);
                r.eax=mo_less(r.eax,1000u);
            }
        }
        const uint32_t same=r.eax;
        r.edx=B;r.ecx=B;
        k=mo_down(m,A);
        if(k==1)r.ecx=1;
        else if(k) { r.edx=1;r.ecx=1; }
        else { r.edx=0;r.ecx=0; }
        if(k)edi=mo_lo(edi,0x1b);
        else if(R16(A+0x23eu)==6u&&same)edi=mo_lo(edi,0x20);
        else {
            r.eax=A+0x4du;a[0]=A+0x4du;a[1]=VA(0x499746u);mo_call(g,0x413e50u,a,2,&r);
            W16(A+0x23eu,6);edi=mo_lo(edi,0x17);esi=1;
        }
        mo_point(m,A,R,&r);
        r.eax=R32(R+0x12u);W32(A+0x24cu,r.eax);
        break; }
    case 0x3:case 0xa:case 0xf: { /* rejoin the group leader */
        r.edx=B;r.ecx=B;
        if(R8(B+0x12u)) { edi=mo_lo(edi,0x1b);break; }
        r.eax=R32(A+0x45u);r.edx=R32(r.eax+0xcu);r.ecx=R32(r.edx);
        r.eax=r.ecx+0x26u;r.edx=A+0x26u;
        a[0]=A+0x26u;a[1]=r.eax;mo_call(g,0x49288cu,a,2,&r);
        edi=mo_lo(edi,mo_less(r.eax,25000u)?0x1eu:0x14u);
        r.eax=A+0x4du;a[0]=A+0x4du;a[1]=VA(0x499594u);mo_call(g,0x413e50u,a,2,&r);
        W16(A+0x23eu,0);esi=1;r.edx=0;W32(A+0x281u,0);
        break; }
    default: /* 6, 9 and above 11h: nothing */
        break;
    }
    /* The answer: always for mode 0, for mode 1 only if the order was taken. */
    const uint16_t md=(uint16_t)mode;
    if(md==0u||(md==1u&&(uint16_t)esi)) { a[0]=A;a[1]=edi;a[2]=0;mo_call(g,0x420a98u,a,3,&r); }
    return mo_pair(esi,r.edx);
}

/* Original: DBSIM.EXE 0x431610 */
uint64_t es2_wingman_order_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t cockpit,uint32_t record,uint32_t slot,const uint32_t entry[3]) {
    const uint32_t H=cockpit,R=record;
    es2_weapon_registers r={(uint32_t)fx_s16(slot),R,entry[2]};uint32_t a[3];
    const uint32_t w=R32(VA(0x4d03c8u)+4u*r.eax);
    if(w&&w!=R32(H+0x203u)) {
        r.eax=w+0x92u;
        if(!R8(w+0x99u)) {
            r.ecx=R32(H+0x203u);W32(R+2u,r.ecx);
            r.edx=R32(w);a[0]=w;a[1]=R;a[2]=0;mo_virtual(g,R32(r.edx+0x28u),0x28u,a,3,&r);
        }
    }
    r.eax=R32(H+0x207u);a[0]=r.eax;a[1]=w;a[2]=0x22u;mo_call(g,0x435ac8u,a,3,&r);
    return mo_pair(r.eax,r.edx);
}
