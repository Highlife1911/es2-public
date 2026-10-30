#include "es2/core/flyer_part.h"
#include "fixed.h"
/* FLYER part damage (docs/reverse-engineering/hit_tests.md, the
 * section "FLYER part damage"). r holds what the original's EAX/EDX/ECX hold at
 * the next game call; lo() is a 16-bit load that keeps the register's high
 * word. The original has no frame locals (EBX = F, ESI = part, EDI = source,
 * all saved). */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define VA(g,x) ((g)->image+(x)-0x400000u)
static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
/* Direct calls name the original VA (observer key); the bridge gets the
 * address in this image. A virtual call passes its resolved target and the
 * vtable offset. Registers are updated in place. */
static void fp_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,VA(g,va),a,n,r);
}
static void fp_virtual(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,
    es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}

/* Original: DBSIM.EXE 0x421bb4 */
uint64_t es2_flyer_part_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t F,uint32_t part,uint32_t damage,uint32_t source,const uint32_t entry[3]) {
    const uint32_t velocity=VA(g,0x4a9660u);
    uint32_t a[3];
    /* 421bba: DX = the damage word over the entry EDX; the dword is pushed. */
    es2_weapon_registers r={F+0x24fu,lo(entry[1],damage),F+0x200u};
    W32(velocity,F+0x24fu);
    a[0]=F+0x200u;a[1]=part;a[2]=r.edx;fp_call(g,0x40da38u,a,3,&r);
    if((uint16_t)r.eax) { /* 421be6: a region was destroyed */
        r.eax=(uint32_t)fx_s16(part)-1u;
        if(!fx_s16(part)) { /* 421bf2: SUB EAX,1 / JAE leaves only part word 0 */
            if(source) { /* 421bfa */
                r.eax=R32(VA(g,0x4d24e6u));
                if(source==r.eax&&F==R32(r.eax+0x1a4u)) { /* 421c03, 421c0b: the player killed its target */
                    a[0]=0x2eu;fp_call(g,0x420a68u,a,1,&r);r.ecx=0x2eu;
                }
                r.edx=R32(source);
                a[0]=source;a[1]=F;a[2]=0;fp_virtual(g,R32(r.edx+0x60u),0x60u,a,3,&r);
            }
            /* 421c21: dead, script state */
            r.edx=F+0x92u;
            W8(F+0x99u,1);W8(F+0x96u,0);
            r.eax=F+0x38u;W8(F+0x38u,1);
            W32(F+0x2eu,0xfffe7960u);
            a[0]=F;fp_call(g,0x411bc8u,a,1,&r);r.ecx=F;
            r.eax=R32(F+0x1b6u);
            if(r.eax) { /* 421c4f */
                const uint32_t trigger=r.eax;
                a[0]=trigger;fp_call(g,0x423430u,a,1,&r);r.ecx=trigger;
            }
            r.edx=F+0x4du;
            a[0]=F+0x4du;a[1]=VA(g,0x499e60u);fp_call(g,0x413e50u,a,2,&r);
            if(R16(F+6u)) { /* 421c6e */
                a[0]=0x31u;fp_call(g,0x462ac4u,a,1,&r);
                W16(F+6u,0);
            }
        }
    }
    /* 421c7e: ECX = 0 (returned by the adapter's entry shim). */
    W32(velocity,0);
    return pair(r.eax,r.edx);
}
