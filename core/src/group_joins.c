#include "es2/core/group_joins.h"
#include "fixed.h"
/* The group joins and the building footprint marks
 * (docs/reverse-engineering/mission_load.md, section "group joins and the
 * building footprint"). Every guest read and write is done in the original's
 * order, and the register struct r follows EAX/EDX/ECX so that each game call
 * sees the original's entry registers. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define GVA(x) (g->image+(uint32_t)(x)-0x400000u)
static uint64_t pair(uint32_t a,uint32_t d) { return (uint64_t)d<<32|a; }
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static void ecx_out(uint32_t *ecx,uint32_t value) { if(ecx)*ecx=value; }
/* SHL r32,CL: the count is CL's low five bits (SAR r32,CL is fx_sar). */
static uint32_t shl_cl(uint32_t x,uint32_t cl) { return x<<(cl&31u); }
/* One game call: the observer first (reference VA, slot 0), then the bridge
 * with the entry registers in r; a virtual call reports its target and
 * slot. */
static void gj_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,GVA(va),a,n,r);
}
static void gj_virtual(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,
    es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}
/* The common head of the three joins: the group and slot stores. */
static void gj_head(const es2_guest_memory *m,es2_weapon_registers *r,uint32_t A,uint32_t G,uint32_t slot) {
    r->eax=G;W32(A+0x45u,r->eax);
    r->edx=lo(r->edx,slot);W16(A+0x49u,r->edx);
}
/* The angle words of the MECH and FLYER joins (417acc, 421f0c): the staged
 * dword [EBP-8] is 0, so EDX = the heading word zero-extended. */
static void gj_angles(const es2_guest_memory *m,es2_weapon_registers *r,uint32_t A) {
    r->ecx=R32(A+0x45u);
    r->eax=lo(r->eax,R16(r->ecx+0xau));
    const uint32_t heading=r->eax&0xffffu;
    r->eax=A+0xcu;
    r->edx=0;W32(r->eax,r->edx);
    r->edx=heading;W16(r->eax+4u,r->edx);
    W16(r->eax+0x26u,0);
}
/* The point of the MECH and FLYER joins (417b06, 421f42) into v: the
 * group's point or route_point's. */
static void gj_point(const es2_guest_memory *m,const es2_weapon_graph *g,es2_weapon_registers *r,uint32_t A,uint32_t v) {
    uint32_t a[2];
    r->eax=R32(A+0x45u);
    r->edx=R32(r->eax+0x18u);
    if(r->edx) {
        r->eax=r->edx;
        r->edx=R32(r->eax+8u);W32(v+8u,r->edx);
        r->ecx=R32(r->eax);W32(v,r->ecx);
        r->eax=R32(r->eax+4u);W32(v+4u,r->eax);
    } else {
        r->eax+=4u;r->edx=lo(r->edx,R16(r->eax));
        a[0]=r->eax;a[1]=r->edx;gj_call(g,0x423b0cu,a,2,r);
        r->ecx=R32(r->eax+8u);W32(v+8u,r->ecx);
        r->edx=R32(r->eax);W32(v,r->edx);
        r->eax=R32(r->eax+4u);W32(v+4u,r->eax);
    }
}
/* A+26h = v (417b7c, 421f8a): +8, +0, +4. */
static void gj_store(const es2_guest_memory *m,es2_weapon_registers *r,uint32_t A,uint32_t v) {
    r->eax=A+0x26u;
    r->edx=R32(v+8u);W32(r->eax+8u,r->edx);
    r->ecx=R32(v);W32(r->eax,r->ecx);
    r->edx=R32(v+4u);W32(r->eax+4u,r->edx);
}

/* Original: DBSIM.EXE 0x417aa8. */
uint64_t es2_mech_join_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t actor,uint32_t group,
    uint32_t slot,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t A=actor,v=g->scratch;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[2];
    gj_head(m,&r,A,group,slot);
    if(R16(A+0xcu)==0x8000u)gj_angles(m,&r,A);
    if(R32(A+0x2eu)==0xffff8000u) {
        gj_point(m,g,&r,A,v);
        r.ecx=R32(GVA(0x4a0be0u));
        a[0]=r.ecx;a[1]=v;gj_call(g,0x46e180u,a,2,&r);
        W32(v+8u,r.eax);
        r.eax=R32(A+0x1f2u);
        r.edx=sx(R16(r.eax+0x16u));
        W32(v+8u,R32(v+8u)+r.edx);
        if(R16(r.eax+0x50u))W32(v+8u,R32(v+8u)+0x1388u);
        r.ecx=R32(A);
        a[0]=A;a[1]=v;gj_virtual(g,R32(r.ecx+0x78u),0x78u,a,2,&r);
        gj_store(m,&r,A,v);
    }
    ecx_out(ecx,r.ecx);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x421ee8. */
uint64_t es2_flyer_join_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t flyer,uint32_t group,
    uint32_t slot,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t F=flyer,v=g->scratch;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[2];
    gj_head(m,&r,F,group,slot);
    if(R16(F+0xcu)==0x8000u)gj_angles(m,&r,F);
    if(R32(F+0x2eu)==0xffff8000u) {
        gj_point(m,g,&r,F,v);
        r.ecx=R32(F);
        a[0]=F;a[1]=v;gj_virtual(g,R32(r.ecx+0x78u),0x78u,a,2,&r);
        gj_store(m,&r,F,v);
    }
    if(!R32(F+0x2eu))W32(F+0x2eu,0x1388u);
    ecx_out(ecx,r.ecx);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x405c3c. */
uint64_t es2_building_join_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t building,
    uint32_t group,uint32_t slot,uint32_t snap,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t B=building,v=g->scratch;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[3];
    gj_head(m,&r,B,group,slot);
    if(R16(B+0xcu)==0x8000u) {
        r.ecx=R32(B+0x45u);
        r.eax=sx(R16(r.ecx+0xau));
        if((uint16_t)r.edx) {                        /* TEST DX,DX: the slot word */
            r.ecx=sx(R16(r.ecx+2u));
            r.edx=sx(r.edx);
            const uint32_t esi=r.ecx;
            r.ecx<<=3;r.edx*=5u;r.ecx-=esi;          /* 7 x the group word, 5 x the slot */
            const uint32_t layouts=R32(GVA(0x4a95acu));
            r.ecx=R32(layouts+4u*r.ecx+4u);
            r.ecx=sx(R16(r.ecx+2u*r.edx-2u));
            r.eax+=r.ecx;
        }
        const uint32_t angle=r.eax&0xffffu;
        r.eax=B+0xcu;
        r.edx=0;W32(r.eax,r.edx);
        r.edx=angle;W16(r.eax+4u,r.edx);
        W16(r.eax+0x26u,0);
    }
    if(R32(B+0x2eu)==0xffff8000u) {
        r.eax=R32(B+0x45u);
        r.edx=R32(r.eax+0x18u);
        if(r.edx) {
            r.eax=r.edx;
            r.ecx=R32(r.eax+8u);W32(v+8u,r.ecx);
            r.edx=R32(r.eax);W32(v,r.edx);
            r.eax=R32(r.eax+4u);W32(v+4u,r.eax);
        } else {
            r.eax+=4u;r.ecx=lo(r.ecx,R16(r.eax));
            a[0]=r.eax;a[1]=r.ecx;gj_call(g,0x423b0cu,a,2,&r);
            r.edx=R32(r.eax+8u);W32(v+8u,r.edx);
            r.ecx=R32(r.eax);W32(v,r.ecx);
            r.eax=R32(r.eax+4u);W32(v+4u,r.eax);
        }
        if(snap) {
            /* 405d05: the base layout's grid. EDX:EAX = [L+10h] x s (IMUL,
             * one operand); [EBP-0Ch] holds x' meanwhile. */
            r.eax=R32(B+0x45u);
            r.edx=sx(R16(r.eax+2u));
            r.eax=((r.edx<<3)-r.edx)<<2;
            r.eax+=R32(GVA(0x4a95acu));
            r.edx=R32(r.eax+0x14u)<<5;
            r.ecx=(r.edx<<8)-1u;
            uint32_t esi=~r.ecx&R32(v);
            const uint32_t edi=R32(r.eax+0xcu)*r.edx;
            const uint32_t x=esi+edi;
            esi=~r.ecx&R32(v+4u);
            r.ecx=r.ecx+esi+1u;
            r.eax=R32(r.eax+0x10u);
            const int64_t product=fx_s32(r.eax)*fx_s32(r.edx);
            r.eax=(uint32_t)product;r.edx=(uint32_t)((uint64_t)product>>32);
            r.ecx-=r.eax;r.eax=r.ecx;
            r.edx=x;W32(v,r.edx);W32(v+4u,r.eax);
        }
        r.ecx=v;r.eax=R32(B);
        a[0]=B;a[1]=v;gj_virtual(g,R32(r.eax+0x78u),0x78u,a,2,&r);
        r.edx=R32(B);
        a[0]=B;gj_virtual(g,R32(r.edx+0x10u),0x10u,a,1,&r);
        r.ecx=a[0];                                  /* POP ECX */
        r.ecx=sx(r.eax);
        r.eax=v;r.edx=R32(GVA(0x4a0be0u));
        a[0]=r.edx;a[1]=v;a[2]=r.ecx;gj_call(g,0x470ed4u,a,3,&r);
        r.ecx=v;r.eax=R32(GVA(0x4a0be0u));
        a[0]=r.eax;a[1]=v;gj_call(g,0x46e180u,a,2,&r);
        const uint32_t z=r.eax;
        r.eax=B+0x26u;
        W32(v+8u,z);W32(r.eax+8u,z);
        r.edx=R32(v);W32(r.eax,r.edx);
        r.ecx=R32(v+4u);W32(r.eax+4u,r.ecx);
    }
    ecx_out(ecx,r.ecx);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x470ed4. The cell bounds [EBP-4] .. [EBP-10h] reach
 * no callee and are kept in variables; the point (x, y, z) at EBP-1Ch is
 * the scratch. */
uint64_t es2_footprint_marks_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t terrain,
    uint32_t point,uint32_t radius,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t T=terrain,P=point,pt=g->scratch;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[4];
    r.edx=R32(P);
    r.eax=R32(T+0x108u);
    r.ecx=r.eax;
    const uint32_t x0=fx_sar(r.edx-radius,r.ecx),x1=fx_sar(r.edx+radius,r.ecx);
    uint32_t ebx=R32(P+4u);
    const uint32_t y0=fx_sar(ebx-radius,r.ecx),y1=fx_sar(ebx+radius,r.ecx);
    r.ecx=R32(P+8u);W32(pt+8u,r.ecx);
    r.ecx=r.eax;
    ebx=fx_sar(ebx,r.ecx);
    r.edx=fx_sar(r.edx,r.ecx);
    a[0]=T;a[1]=r.edx;a[2]=ebx;a[3]=1;gj_call(g,0x470ddcu,a,4,&r);
    for(uint32_t y=y0;(int32_t)y<=(int32_t)y1;++y) {
        r.ecx=R32(T+0x108u);
        r.eax=shl_cl(y,r.ecx);W32(pt+4u,r.eax);
        for(uint32_t x=x0;(int32_t)x<=(int32_t)x1;++x) {
            r.ecx=R32(T+0x108u);
            r.eax=shl_cl(x,r.ecx);W32(pt,r.eax);
            r.edx=P;r.eax=pt;
            a[0]=pt;a[1]=P;gj_call(g,0x49288cu,a,2,&r);
            if((int32_t)r.eax>(int32_t)radius)continue;
            a[0]=T;a[1]=x;a[2]=y;a[3]=1;gj_call(g,0x470ddcu,a,4,&r);
            if((int32_t)x>0) {
                r.edx=x-1u;
                a[0]=T;a[1]=r.edx;a[2]=y;a[3]=1;gj_call(g,0x470ddcu,a,4,&r);
                if((int32_t)y>0) {
                    r.ecx=y-1u;r.eax=x-1u;
                    a[0]=T;a[1]=r.eax;a[2]=r.ecx;a[3]=1;gj_call(g,0x470ddcu,a,4,&r);
                }
            }
            if((int32_t)y>0) {
                r.edx=y-1u;
                a[0]=T;a[1]=x;a[2]=r.edx;a[3]=1;gj_call(g,0x470ddcu,a,4,&r);
            }
        }
    }
    ecx_out(ecx,r.ecx);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x470ddc. */
uint64_t es2_cell_flag_set_registers(const es2_guest_memory *m,uint32_t terrain,uint32_t x,uint32_t y,
    uint32_t value,const uint32_t entry[3],uint32_t *ecx) {
    (void)entry;
    uint32_t c=R32(terrain+0x100u);
    const uint32_t cells=R32(terrain+0xf0u);
    const uint32_t index=shl_cl(y,c)+x;
    c=(c&0xffffff00u)|(value&0xffu);
    W8(cells+index,c);
    ecx_out(ecx,c);
    return pair(cells,index);
}
