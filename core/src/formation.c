#include "es2/core/formation.h"
#include "fixed.h"
/* Formation-slot virtual +78h (docs/reverse-engineering/ai.md, "Formation slot
 * virtual +78h"). Registers are tracked as the original leaves them, because
 * they become the entry registers of euler_matrix/rotate_2d and the returned
 * pair. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define VA(va) (g->image+(uint32_t)(va)-0x400000u)
/* Actor: group pointer +45h, slot word +49h. Group: type word +2, member
 * list +0Ch. Leader L: angles +0Ch, cached heading matrix +12h, cache flag
 * word +32h (relative to the angles: +6 and +26h). */
enum { A_GROUP=0x45u, A_SLOT=0x49u, G_TYPE=2u, G_MEMBERS=0xcu, L_ANGLES=0xcu, ANG_MATRIX=6u, ANG_CACHED=0x26u };
/* Scratch: R (411d64's EBP-10h, 2 dwords) and Q (417898/421e98's EBP-0Ch,
 * 3 dwords). */
enum { SCRATCH_R=0x00u, SCRATCH_Q=0x20u };
static uint64_t fm_pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
/* A 16-bit register write: the high word stays. */
static uint32_t fm_word(uint32_t high,uint32_t low) { return (high&0xffff0000u)|(uint16_t)low; }
static uint32_t fm_sx16(uint32_t x) { return (uint32_t)fx_s16(x); }
static void fm_note(const es2_weapon_graph *g,uint32_t va) { if(g->observe)g->observe(g->observer_context,VA(va),0); }
static void fm_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    fm_note(g,va);g->call(g->context,VA(va),a,n,r);
}

/* Original: DBSIM.EXE 0x411d64 (A, Q, P): P += (R(Q), Q.z), R the rotation
 * of Q.x/Q.y by the leader's heading matrix (built on first use). */
static void fm_place(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t actor,uint32_t q,uint32_t point,
    es2_weapon_registers *r) {
    fm_note(g,0x411d64u);
    const uint32_t rot=g->scratch+SCRATCH_R;
    uint32_t a[3];
    r->edx=R32(q+8u);const uint32_t z=r->edx;
    r->eax=R32(actor+A_GROUP);
    r->ecx=R32(r->eax+G_MEMBERS);
    const uint32_t angles=R32(r->ecx)+L_ANGLES;
    if(!R16(angles+ANG_CACHED)) {
        r->eax=angles+ANG_MATRIX;r->edx=angles;
        a[0]=angles;a[1]=angles+ANG_MATRIX;fm_call(g,0x47ebb8u,a,2,r);W16(angles+ANG_CACHED,1);
    }
    r->eax=angles+ANG_MATRIX;r->edx=q;r->ecx=rot;
    a[0]=q;a[1]=angles+ANG_MATRIX;a[2]=rot;fm_call(g,0x47ffe8u,a,3,r);
    r->eax=z;W32(point+8u,R32(point+8u)+r->eax);
    r->edx=R32(rot);W32(point,R32(point)+r->edx);
    r->ecx=R32(rot+4u);W32(point+4u,R32(point+4u)+r->ecx);
}

/* Original: DBSIM.EXE 0x405b9c (A, type, slot): x and y of the slot's
 * 10-byte entry into the static buffer 4975d6 (zeroed on the first call;
 * z is never written again). Returns the buffer address. */
static uint32_t fm_base_slot(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t type,uint32_t slot,
    es2_weapon_registers *r) {
    fm_note(g,0x405b9cu);
    const uint32_t buffer=VA(0x4975d6u),first=VA(0x4975e2u);
    if(!R8(first)) {
        r->eax=0;r->edx=0;W32(buffer,r->eax);r->ecx=0;W32(buffer+4u,r->edx);W32(buffer+8u,r->ecx);
        W8(first,R8(first)+1u);
    }
    const uint32_t t=fm_sx16(type),s=fm_sx16(slot);
    r->eax=R32(R32(VA(0x4a95acu))+4u*(7u*t)+4u);
    r->ecx=10u*s;
    r->eax+=r->ecx-10u;
    r->edx=R32(r->eax);W32(buffer,r->edx);
    W32(buffer+4u,R32(r->eax+4u));
    r->eax=buffer;
    return buffer;
}

/* Original: DBSIM.EXE 0x4205cc (type, slot): x and y words of the slot's
 * 4-byte entry into the static buffer 49a430 (zeroed on the first call;
 * z is never written again). Returns the buffer address. */
static uint32_t fm_mech_slot(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t type,uint32_t slot,
    es2_weapon_registers *r) {
    fm_note(g,0x4205ccu);
    const uint32_t buffer=VA(0x49a430u),first=VA(0x49a436u);
    if(!R8(first)) {
        W16(buffer,0);W16(buffer+2u,0);W16(buffer+4u,0);
        W8(first,R8(first)+1u);
    }
    const uint32_t t=fm_sx16(type),s=fm_sx16(slot);
    r->edx=t;
    const uint32_t entry=28u*t+R32(VA(0x4a9d6cu))+4u*s-4u;
    r->ecx=4u*s;
    r->edx=fm_word(r->edx,R16(entry));W16(buffer,r->edx);
    r->eax=fm_word(entry,R16(entry+2u));W16(buffer+2u,r->eax);
    r->eax=buffer;
    return buffer;
}

/* Original: DBSIM.EXE 0x423044 (type, slot): the slot's 6-byte entry in
 * the table itself. */
static uint32_t fm_flyer_slot(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t type,uint32_t slot,
    es2_weapon_registers *r) {
    fm_note(g,0x423044u);
    const uint32_t t=fm_sx16(type),s=fm_sx16(slot);
    r->edx=s;r->ecx=6u*s;
    r->eax=18u*t+R32(VA(0x4a9dfcu))+r->ecx-6u;
    return r->eax;
}

/* Original: DBSIM.EXE 0x405c04 */
uint64_t es2_formation_base_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t actor,uint32_t point,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    const uint32_t slot=R16(actor+A_SLOT);
    if(!slot)return fm_pair(r.eax,r.edx);
    r.eax=point;
    r.edx=R32(actor+A_GROUP);
    r.ecx=fm_word(r.ecx,R16(r.edx+G_TYPE));
    const uint32_t q=fm_base_slot(m,g,r.ecx,slot,&r);
    fm_place(m,g,actor,q,point,&r);
    return fm_pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x417898 (MECH) and 0x421e98 (FLYER): the helper's
 * x, y, z words, sign-extended into Q (read order +4, +0, +2). */
static uint64_t fm_word_slot(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t actor,uint32_t point,
    const uint32_t entry[3],int flyer) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    r.eax=fm_word(r.eax,R16(actor+A_SLOT));
    if(!(uint16_t)r.eax)return fm_pair(r.eax,r.edx);
    r.edx=R32(actor+A_GROUP);
    r.ecx=fm_word(r.ecx,R16(r.edx+G_TYPE));
    const uint32_t h=flyer?fm_flyer_slot(m,g,r.ecx,r.eax,&r):fm_mech_slot(m,g,r.ecx,r.eax,&r);
    const uint32_t q=g->scratch+SCRATCH_Q;
    r.edx=fm_sx16(R16(h+4u));W32(q+8u,r.edx);
    r.ecx=fm_sx16(R16(h));W32(q,r.ecx);
    r.edx=q;
    r.eax=fm_sx16(R16(h+2u));W32(q+4u,r.eax);
    r.eax=point;
    fm_place(m,g,actor,q,point,&r);
    return fm_pair(r.eax,r.edx);
}
uint64_t es2_formation_mech_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t actor,uint32_t point,const uint32_t entry[3]) {
    return fm_word_slot(m,g,actor,point,entry,0);
}
uint64_t es2_formation_flyer_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t actor,uint32_t point,const uint32_t entry[3]) {
    return fm_word_slot(m,g,actor,point,entry,1);
}
