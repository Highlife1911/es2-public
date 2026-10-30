#include "es2/core/projectile.h"
#include "es2/core/geometry.h"
#include "es2/core/trig.h"
#include "fixed.h"
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(uint8_t *)m->host(m->context,(a)))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
/* A 16-bit register load or arithmetic keeps the upper half. */
static uint32_t word(uint32_t high,uint32_t low) { return (high&0xffff0000u)|(uint16_t)low; }
static uint32_t sbyte(uint8_t x) { return (uint32_t)(int32_t)(int8_t)x; }
static uint32_t magnitude(uint32_t x) { return x&0x80000000u?0u-x:x; }

/* Original: DBSIM.EXE 0x49290c (with the forwarder 0x492ac8 and angle_ratio
 * 0x47d32c computed here). */
uint64_t es2_vector_angle_registers(uint32_t x,uint32_t y,const void *corrections) {
    if(!x&&!y)x=1;
    return es2_angle_ratio_registers(x,y,corrections);
}
uint32_t es2_vector_angle_ecx(uint32_t x,uint32_t y) {
    if(!(x|y))x=1;
    uint32_t a=magnitude(x),b=magnitude(y);
    return b&&fx_s32(b)>fx_s32(a)?b:a;
}

/* Original: DBSIM.EXE 0x492990 (49290c twice, 492a9a -> 47de4c computed
 * here). The z delta is read after out+4 and out+2 are written. */
uint64_t es2_aim_angles_registers(const es2_guest_memory *m,uint32_t out,uint32_t end,uint32_t start,
    const void *corrections) {
    uint32_t dx=R32(end)-R32(start);
    uint32_t dy=R32(end+4u)-R32(start+4u);
    uint64_t h=es2_vector_angle_registers(dx,dy,corrections);
    W16(out+4u,(uint32_t)h+0xc000u);W16(out+2u,0);
    uint32_t dz=R32(end+8u)-R32(start+8u);
    uint64_t p=es2_vector_angle_registers(es2_distance2(dx,dy),dz,corrections);
    W16(out,(uint32_t)p);
    return pair(out,(uint32_t)(p>>32));
}

/* Direct calls name the original VA; virtual calls pass the target read
 * from the receiver's vtable with the slot offset. */
static void pj_call(const es2_weapon_graph *g,uint32_t function,unsigned slot,
    const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    uint32_t f=slot?function:g->image+function-0x400000u;
    if(g->observe)g->observe(g->observer_context,f,slot);
    g->call(g->context,f,a,n,r);
}
#define L(x) (f-(x))
/* Steps shared by both bodies: the target point from V24 (R nonzero:
 * cached Euler matrix and 48043c; zero: copy of the target's position). */
static void pj_attachment(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t o,unsigned field,
    uint32_t f,es2_weapon_registers *r) {
    r->edx=R32(o+field);r->ecx=R32(r->edx);uint32_t a[3]={r->edx};
    pj_call(g,R32(r->ecx+0x24u),0x24,a,1,r);r->ecx=a[0];
    uint32_t res=r->eax;
    if(res) {
        uint32_t c=R32(o+field)+0xcu;
        if(!R16(c+0x26u)) {
            W32(L(0x1c),c);r->eax=c+6u;r->edx=R32(L(0x1c));
            a[0]=r->edx;a[1]=r->eax;pj_call(g,0x47ebb8,0,a,2,r);W16(c+0x26u,1);
        }
        r->eax=c+6u;r->edx=res+0x14u;r->ecx=L(0x28);
        a[0]=r->edx;a[1]=r->eax;a[2]=r->ecx;pj_call(g,0x48043c,0,a,3,r);
    } else {
        r->eax=R32(o+field)+0x26u;
        r->edx=R32(r->eax+8u);W32(L(0x20),r->edx);
        r->ecx=R32(r->eax);W32(L(0x28),r->ecx);
        r->eax=R32(r->eax+4u);W32(L(0x24),r->eax);
    }
}
static void pj_aim(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t o,uint32_t f,es2_weapon_registers *r) {
    r->edx=o+0x26u;r->ecx=L(0x28);r->eax=L(0x10);
    uint32_t a[3]={r->eax,r->ecx,r->edx};pj_call(g,0x492990,0,a,3,r);
    r->edx=word(r->edx,R16(L(4)));W16(L(0xc),R16(L(0xc))-r->edx);
    r->ecx=word(r->ecx,R16(L(8)));W16(L(0x10),R16(L(0x10))-r->ecx);
}
static uint64_t pj_commit(const es2_guest_memory *m,uint32_t o,uint32_t f,es2_weapon_registers *r) {
    r->eax=o+0xcu;r->edx=R32(L(8));W32(r->eax,r->edx);
    r->edx=word(r->edx,R16(L(4)));W16(r->eax+4u,r->edx);W16(r->eax+0x26u,0);
    return pair(r->eax,r->edx);
}

/* Original: DBSIM.EXE 0x40a254 */
uint64_t es2_projectile_steer_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t o,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t f=g->scratch+0x28u,a[3];
    /* Steps 1-3: target present, owner's lock, type 2 needs a flag. */
    if(!R32(o+0x56u))return pair(r.eax,r.edx);
    r.edx=R32(o+0x4au);r.eax=r.edx+0x92u;
    if(R8(r.eax+0x11u)) {
        r.ecx=R32(r.edx+0x1a4u);
        if(r.ecx!=R32(o+0x56u))return pair(r.eax,r.edx);
    }
    uint32_t s=R32(o+0x56u)+0x92u;r.eax=s;
    if(R8(s+4u))r.eax=1;
    else { r.edx=s;r.eax=R8(s+0xfu)?1u:0u; }
    r.edx=sbyte(R8(o+0x41u));
    if(r.edx==2&&!(uint16_t)r.eax)return pair(r.eax,r.edx);
    /* Step 4: the object's angles into the frame. */
    r.eax=o+0xcu;
    r.edx=word(r.edx,R16(r.eax));W16(L(8),r.edx);
    r.ecx=word(r.ecx,R16(r.eax+2u));W16(L(6),r.ecx);
    r.eax=word(r.eax,R16(r.eax+4u));W16(L(4),r.eax);
    W16(L(0x18),0);W16(L(0x16),0);W16(L(0x14),0);
    r.eax=word(r.eax,R16(o+0x5au));
    /* Step 5: the target point. */
    if(fx_s16(r.eax)>=0) {
        r.edx=L(0x28);uint32_t part=r.eax;r.ecx=R32(o+0x56u);r.eax=R32(r.ecx);
        a[0]=r.ecx;a[1]=part;a[2]=r.edx;pj_call(g,R32(r.eax+0x58u),0x58,a,3,&r);
    } else pj_attachment(m,g,o,0x56u,f,&r);
    /* Step 6: angles to the target, minus the object's. */
    pj_aim(m,g,o,f,&r);
    r.eax=word(r.eax,R16(L(6)));W16(L(0xe),R16(L(0xe))-r.eax);
    /* Step 7: type gates and the near-zero push. */
    r.edx=sbyte(R8(o+0x41u));
    if(r.edx==3) { r.eax=R32(o+0x4au)+0x92u;W8(r.eax+0x23u,1); }
    else {
        r.edx=sbyte(R8(o+0x41u));
        if(r.edx!=2) {
            r.eax=R32(o+0x4au)+0x92u;
            if(R8(r.eax+0xau)) {
                r.eax=word(r.eax,R16(L(0xc)));r.edx=word(r.eax,r.eax+0xc00u);
                r.ecx=(uint16_t)r.edx<0x1800u;
                if((uint16_t)r.ecx) {
                    r.edx=word(r.edx,0xc00u);if(fx_s16(r.eax)>=0)r.edx+=0xffffe800u;
                    W16(L(0xc),R16(L(0xc))+r.edx);
                }
                r.eax=word(r.eax,R16(L(0x10)));r.ecx=word(r.eax,r.eax+0xc00u);
                r.edx=(uint16_t)r.ecx<0x1800u;
                if((uint16_t)r.edx) {
                    r.ecx=word(r.ecx,0xc00u);if(fx_s16(r.eax)>=0)r.ecx+=0xffffe800u;
                    W16(L(0x10),R16(L(0x10))+r.ecx);
                }
            }
        }
    }
    /* Step 8: approach the deltas, scale, add to the angles. */
    r.eax=word(r.eax,R16(L(0x10)));a[1]=r.eax;r.eax=L(0x18);a[0]=r.eax;a[2]=0x500;
    pj_call(g,0x467adc,0,a,3,&r);
    r.edx=word(r.edx,R16(L(0xc)));r.ecx=L(0x14);a[0]=r.ecx;a[1]=r.edx;a[2]=0x500;
    pj_call(g,0x467adc,0,a,3,&r);
    r.eax=word(r.eax,R16(L(0x18)));a[0]=r.eax;pj_call(g,0x467924,0,a,1,&r);r.ecx=a[0];
    W16(L(8),R16(L(8))+r.eax);
    r.edx=word(r.edx,R16(L(0x14)));a[0]=r.edx;pj_call(g,0x467924,0,a,1,&r);r.ecx=a[0];
    W16(L(4),R16(L(4))+r.eax);
    return pj_commit(m,o,f,&r);
}

/* Original: DBSIM.EXE 0x40aff0 */
uint64_t es2_bullet_steer_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t o,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t f=g->scratch+0x28u,a[3];
    r.eax=o+0xcu;
    r.edx=word(r.edx,R16(r.eax));W16(L(8),r.edx);
    r.ecx=word(r.ecx,R16(r.eax+2u));W16(L(6),r.ecx);
    r.eax=word(r.eax,R16(r.eax+4u));W16(L(4),r.eax);
    W16(L(0x18),0);W16(L(0x16),0);W16(L(0x14),0);
    pj_attachment(m,g,o,0x5bu,f,&r);
    pj_aim(m,g,o,f,&r);
    r.ecx=L(0x18);
    r.eax=word(r.eax,R16(L(6)));W16(L(0xe),R16(L(0xe))-r.eax);
    r.edx=word(r.edx,R16(L(0x10)));a[0]=r.ecx;a[1]=r.edx;a[2]=0x280;
    pj_call(g,0x467adc,0,a,3,&r);
    r.eax=word(r.eax,R16(L(0xc)));r.edx=L(0x14);a[0]=r.edx;a[1]=r.eax;a[2]=0x280;
    pj_call(g,0x467adc,0,a,3,&r);
    r.ecx=word(r.ecx,R16(L(0x18)));a[0]=r.ecx;pj_call(g,0x467924,0,a,1,&r);r.ecx=a[0];
    W16(L(8),R16(L(8))+r.eax);
    r.eax=word(r.eax,R16(L(0x14)));a[0]=r.eax;pj_call(g,0x467924,0,a,1,&r);r.ecx=a[0];
    W16(L(4),R16(L(4))+r.eax);
    pj_commit(m,o,f,&r);
    return pair(word(r.eax,R16(L(0xc))),r.edx);
}
