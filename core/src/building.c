#include "es2/core/building.h"
#include "fixed.h"
/* Building logic below the turret (docs/reverse-engineering/weapons.md,
 * "Building logic: target selection and destruction actions"). Registers are
 * tracked as the original leaves them, because they become the entry
 * registers of the next game call and the returned EDX. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
/* A DBSIM VA (globals, tables, direct callees) inside the image at g->image. */
#define BLD_VA(va) (g->image+(uint32_t)(va)-0x400000u)
static uint64_t bld_pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
/* A 16-bit register write: the high word stays. */
static uint32_t bld_word(uint32_t high,uint32_t low) { return (high&0xffff0000u)|(uint16_t)low; }
static uint32_t bld_sx16(uint32_t x) { return (uint32_t)fx_s16(x); }
static uint32_t bld_sx8(uint32_t x) { return (uint32_t)(int32_t)(int8_t)(uint8_t)x; }
static int bld_less(uint32_t a,uint32_t b) { return fx_s32(a)<fx_s32(b); }
/* A direct call names the original VA; a virtual call is observed by slot. */
static void bld_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,BLD_VA(va),a,n,r);
}
static void bld_virtual(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}

/* Ten variable actions: index words at `indices`, action words at `actions`.
 * Action 1 clears the variable, 2 increments it, 13..16 store action-12. */
static void bld_actions(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t indices,uint32_t actions) {
    uint32_t bank=BLD_VA(0x4a9e70u);
    for(uint32_t i=0;i<10u;++i,indices+=2u,actions+=2u) {
        uint32_t index=bld_sx16(R16(indices));
        if(fx_s16(index)<0)continue;
        uint32_t action=bld_sx16(R16(actions)),cell=bank+2u*index;
        if(action==1u)W16(cell,0);
        else if(action==2u)W16(cell,R16(cell)+1u);
        else if(action-13u<4u)W16(cell,R16(actions)-12u);
    }
}

/* Original: DBSIM.EXE 0x423f30 */
uint64_t es2_group_death_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t group,uint32_t actor,const uint32_t entry[3]) {
    (void)entry;
    uint32_t member=R32(group+0xcu);
    for(uint32_t i=0;bld_less(i,bld_sx16(R16(group+0x10u)));++i,member+=4u) {
        uint32_t other=R32(member);
        if(other==actor)continue;
        /* Another living member: nothing happens. */
        if(!R8(other+0xa4u)&&!R8(other+0x99u))return bld_pair(0,i);
    }
    bld_actions(m,g,group+0x1cu,group+0x30u);
    return bld_pair(group+0x30u,10);
}

/* Original: DBSIM.EXE 0x411bc8 */
uint64_t es2_death_actions_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t actor,const uint32_t entry[3]) {
    uint32_t group=R32(actor+0x45u);
    if(group) {
        es2_weapon_registers r={group,entry[1],entry[2]};
        uint32_t a[2]={group,actor};bld_call(g,0x423f30u,a,2,&r);
    }
    bld_actions(m,g,actor+0x1bau,actor+0x1ceu);
    return bld_pair(actor+0x1ceu,actor+0x1e2u);
}

/* Original: DBSIM.EXE 0x411e80 */
uint64_t es2_target_valid_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t actor,uint32_t candidate,uint32_t flags,const uint32_t entry[3]) {
    (void)entry;
    uint32_t edx=bld_sx8(R8(R32(candidate+0x45u)+0x12u));
    if((uint16_t)edx==(uint16_t)bld_sx8(R8(R32(actor+0x45u)+0x12u)))return bld_pair(0,edx);
    if(R8(candidate+0x99u)||R8(candidate+0xb4u))return bld_pair(0,edx);
    es2_weapon_registers r={candidate+0x92u,edx,R32(actor+0x45u)};
    uint32_t a[2]={actor,candidate};bld_call(g,0x411c58u,a,2,&r);
    if(!(uint16_t)r.eax||R8(candidate+0xb7u))return bld_pair(0,r.edx);
    if((flags&0x10u)&&R16(candidate+0x1a8u)==R16(actor+0x1a8u))return bld_pair(0,r.edx);
    /* Type 2 candidates must be alive. */
    if(R16(candidate+0x1a8u)==2u&&(R8(candidate+0xa5u)||R8(candidate+0xa4u)||R8(candidate+0x99u)))return bld_pair(0,1);
    uint32_t group=R32(actor+0x45u),leader=R32(R32(group+0xcu));
    edx=leader+0x92u;
    if(R8(leader+0xa3u)&&R16(BLD_VA(0x4a9e54u))==3u) {
        /* The current order's target is excluded. */
        r.eax=leader;r.edx=leader+0x92u;a[0]=group;bld_call(g,0x4238a0u,a,1,&r);
        if(r.eax==candidate)return bld_pair(0,r.edx);
        edx=r.edx;
    }
    if(R8(actor+0x9au)) {
        uint32_t cockpit=R32(BLD_VA(0x49b088u));
        if(R32(cockpit+0x210u)==candidate)return bld_pair(0,cockpit);
        edx=cockpit;
    }
    return bld_pair(bld_word(actor+0x92u,1),edx);
}

/* score = 47e0b0(table[index], s16 score): one Q10 factor (multiply_q10). */
static uint32_t bld_factor(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t table,uint32_t index,
    uint32_t score,es2_weapon_registers *r) {
    uint32_t value=bld_sx16(R16(BLD_VA(table)+2u*index)),a[2];
    r->eax=bld_sx16(score);r->edx=r->eax;r->ecx=value;
    a[0]=value;a[1]=r->eax;bld_call(g,0x47e0b0u,a,2,r);
    return r->eax;
}

/* Original: DBSIM.EXE 0x411fa0 */
uint64_t es2_target_select_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t actor,uint32_t flags,uint32_t arc,const uint32_t entry[3]) {
    (void)entry;
    uint32_t cursor=R32(BLD_VA(0x4a9af8u)),best=0,best_score=0,best_class=1,i=0,edi;
    uint32_t catflag=R8(R32(actor+0x45u)+0x12u)==0;
    uint32_t group=R32(actor+0x45u),index=R32(group+0x6cu),order=R32(group+4u*index+0x44u);
    es2_weapon_registers r;
    r.edx=bld_word(index,order?R16(order):0xbu);r.eax=(uint16_t)r.edx==3u;r.ecx=group;
    /* A type-3 order lets every class compete. */
    if(r.eax)best_class=0;
    for(;;) {
        uint32_t old=i,candidate=0,a[3];
        i=(uint16_t)(i+1u);r.eax=bld_word(r.eax,old);
        if(fx_s16(old)<fx_s16(R16(BLD_VA(0x4a9afeu)))) { r.edx=cursor;cursor+=4u;candidate=R32(r.edx); }
        if(!candidate)return bld_pair(best,r.edx);
        r.ecx=bld_word(r.ecx,flags);
        a[0]=actor;a[1]=candidate;a[2]=r.ecx;bld_call(g,0x411e80u,a,3,&r);
        if(!(uint16_t)r.eax)continue;
        a[0]=R32(actor+0x45u);r.eax=candidate;r.edx=a[0];bld_call(g,0x423918u,a,2,&r);
        /* matched: the candidate is the order's subject and the order is type 0. */
        uint32_t matched=0;
        if((uint16_t)r.eax) {
            group=R32(actor+0x45u);r.ecx=R32(group+0x6cu);
            if(R32(group+4u*r.ecx+0x44u)) {
                r.eax=R32(group+0x6cu);r.edx=R32(group+4u*r.eax+0x44u);r.ecx=bld_word(r.ecx,R16(r.edx));
            } else r.ecx=bld_word(r.ecx,0xbu);
            matched=(uint16_t)r.ecx==0;
        }
        r.eax=matched;
        uint32_t dead=R8(candidate+0xa5u)||R8(candidate+0xa4u)||R8(candidate+0x99u);
        r.edx=dead;r.ecx=!dead;
        uint32_t cls=(matched||!(flags&1u)?2u:0u)+r.ecx;
        if(!matched||catflag) { r.edx=candidate+0x92u;if(R8(candidate+0xa4u))continue; }
        r.ecx=cls;
        if(fx_s16(cls)<fx_s16(best_class))continue;
        edi=matched?1000000u:100000u;
        a[0]=actor+0x26u;a[1]=candidate+0x26u;r.eax=a[1];r.edx=a[0];bld_call(g,0x49288cu,a,2,&r);
        uint32_t distance=r.eax;
        if(bld_less(edi,distance))continue;
        if(flags&0x20u)r.eax=0;
        else {
            a[0]=candidate+0x26u;a[1]=actor+0x26u;r.eax=a[0];r.edx=a[0];r.ecx=a[1];bld_call(g,0x492934u,a,2,&r);
            edi=bld_word(r.eax,r.eax-R16(actor+0x10u));
            /* |DI| as a word, 8000h saturating to 7FFFh. */
            uint32_t di=(uint16_t)edi;
            r.eax=di==0x8000u?bld_word(r.eax,0x7fffu):fx_s16(di)>=0?edi:bld_word(edi,0u-di);
            if((uint16_t)arc) {
                r.edx=r.eax;
                if(fx_s16(r.eax)>fx_s16(arc))continue;
            }
        }
        r.eax=fx_sar(bld_sx16(r.eax),3);edi=bld_word(edi,0x1000u-r.eax);
        r.eax=bld_sx16(edi);r.edx=r.eax;a[0]=0x46u;a[1]=r.eax;bld_call(g,0x47e0b0u,a,2,&r);
        r.ecx=r.eax;
        if(bld_less(distance,60000u)) {
            /* Close candidates: the facing factor over the distance in 4096s. */
            uint32_t q=fx_sar(distance,12);
            int32_t num=fx_s16(edi);
            edi=bld_less(q,1u)?1u:q;
            r.edx=edi;r.eax=(uint32_t)(num/(int32_t)edi);r.edx=(uint32_t)(num%(int32_t)edi);
            if(fx_s16(r.ecx)<fx_s16(r.eax))r.ecx=bld_word(r.ecx,r.eax);
        }
        /* The attacker's virtual +4Ch classifies the candidate (k). */
        edi=r.ecx;r.eax=R32(actor);a[0]=actor;a[1]=candidate;
        bld_virtual(g,R32(r.eax+0x4cu),0x4cu,a,2,&r);
        uint32_t kind=bld_sx16(r.eax);
        if(!(flags&2u)&&actor==R32(candidate+0x1a4u))edi=bld_factor(m,g,0x49933cu,kind,edi,&r);
        if(R32(candidate+0x4du)) {
            r.eax=R32(candidate+0x4du)+8u;r.edx=r.eax;
            if(!R8(r.edx+1u)) {
                r.ecx=candidate+0x92u;r.eax=r.ecx;
                if(!R8(candidate+0xa3u))edi=bld_factor(m,g,0x499342u,kind,edi,&r);
            }
        }
        r.eax=R32(candidate+0x1a4u);
        if(r.eax&&r.eax!=actor)edi=bld_factor(m,g,0x499348u,kind,edi,&r);
        uint32_t type=bld_sx16(R16(candidate+0x1a8u));r.eax=type;
        if(actor==R32(candidate+0x1a4u)&&type==1u) { r.edx=0;type=0; }
        r.edx=bld_sx16(edi);r.eax=bld_sx16(R16(BLD_VA(0x49934eu)+2u*type));r.ecx=r.edx;
        a[0]=r.eax;a[1]=r.edx;bld_call(g,0x47e0b0u,a,2,&r);edi=r.eax;
        /* Shared by other attackers: divide by their number plus one. */
        uint32_t same=candidate==R32(actor+0x1a4u);
        r.edx=same;r.ecx=bld_word(r.ecx,R16(candidate+0x1a2u)-same);
        if(!(flags&4u)&&(uint16_t)r.ecx) {
            int32_t divisor=fx_s16(r.ecx)+1,num=fx_s16(edi);
            /* divisor 0 faults in the original; outside the contract. */
            if(divisor) { r.ecx=(uint32_t)divisor;r.eax=(uint32_t)(num/divisor);r.edx=(uint32_t)(num%divisor);edi=r.eax; }
        }
        r.eax=bld_sx16(edi);
        if(!bld_less(best_score,r.eax)) {
            r.eax=bld_word(r.eax,cls);
            if(fx_s16(cls)<=fx_s16(best_class))continue;
        }
        r.edx=bld_sx16(edi);best_score=r.edx;r.ecx=bld_word(r.ecx,cls);best_class=cls;best=candidate;
    }
}

/* Turret target and part facing (weapons.md, "building turret
 * target 403eec and part facing 406868"). The frame locals live in
 * g->scratch at the original's offsets from the frame base. */
#define W32(a,v) m->write32(m->context,(a),(v))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
/* A 32-byte matrix (nine words, two bytes, three dwords). */
static void bld_matrix_copy(const es2_guest_memory *m,uint32_t to,uint32_t from) {
    for(uint32_t k=0;k<0x12u;k+=2u)W16(to+k,R16(from+k));
    W8(to+0x12u,R8(from+0x12u));W8(to+0x13u,R8(from+0x13u));
    for(uint32_t k=0x14u;k<0x20u;k+=4u)W32(to+k,R32(from+k));
}
/* Inverts M's translation: negate +1C, +14, +18 in place, copy it to T and
 * rotate it by M into M+14 (after 47df19 transposed M; ECX = M). */
static void bld_invert(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t mat,uint32_t t,es2_weapon_registers *r) {
    uint32_t a[3]={mat};
    bld_call(g,0x47df19u,a,1,r);r->ecx=mat;
    W32(mat+0x1cu,0u-R32(mat+0x1cu));W32(mat+0x14u,0u-R32(mat+0x14u));
    r->eax=0u-R32(mat+0x18u);W32(mat+0x18u,r->eax);
    r->edx=R32(mat+0x14u);W32(t,r->edx);W32(t+4u,r->eax);r->ecx=R32(mat+0x1cu);W32(t+8u,r->ecx);
    r->eax=mat+0x14u;r->edx=t;
    a[0]=t;a[1]=mat;a[2]=mat+0x14u;bld_call(g,0x480304u,a,3,r);
}

/* Original: DBSIM.EXE 0x403eec */
uint64_t es2_turret_target_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t b,uint32_t p,const uint32_t entry[3]) {
    (void)entry;
    uint32_t f=g->scratch,t2=f,t1=f+0xcu,m2=f+0x18u,m1=f+0x38u,z=f+0x58u,v1=f+0x64u,v2=f+0x70u;
    uint32_t angles=BLD_VA(0x4a95ecu),guard=BLD_VA(0x4974d5u),a[3];
    if(!R8(guard))W8(guard,1);
    es2_weapon_registers r={0,0,0};
    W32(z,0);W32(z+4u,0);W32(z+8u,0);
    if(!R16(b+0x32u)) {
        r.eax=b+0x12u;r.edx=b+0xcu;a[0]=b+0xcu;a[1]=b+0x12u;bld_call(g,0x47ebb8u,a,2,&r);
        W16(b+0x32u,1);
    }
    bld_matrix_copy(m,m1,b+0x12u);
    /* The turret part's matrix record from the shape manager's slot +20. */
    uint32_t manager=R32(b+0x34u),object=R32(manager+4u);
    r.ecx=b;r.eax=object;r.edx=R32(object);a[0]=object;a[1]=2;
    bld_virtual(g,R32(r.edx+0x20u),0x20,a,2,&r);
    uint32_t record;
    if(r.eax&&fx_s16(R16(r.eax+4u))>-1)record=R32(manager+0x16u)+(uint32_t)fx_s16(R16(r.eax+4u))*32u;
    else record=BLD_VA(0x6c56a8u);
    bld_matrix_copy(m,m2,record);
    r.edx=R32(record+0x14u);r.ecx=R32(record+0x18u);r.eax=R32(record+0x1cu);
    bld_invert(m,g,m1,t1,&r);
    bld_invert(m,g,m2,t2,&r);
    /* World point into the building frame, then into the part frame. */
    r.eax=p;r.edx=v1;a[0]=p;a[1]=m1;a[2]=v1;bld_call(g,0x48043cu,a,3,&r);
    r.ecx=v2;r.eax=v1;a[0]=v1;a[1]=m2;a[2]=v2;bld_call(g,0x48043cu,a,3,&r);
    r.edx=z;r.ecx=v2;a[0]=angles;a[1]=v2;a[2]=z;bld_call(g,0x492990u,a,3,&r);
    r.eax=b;a[0]=b;a[1]=angles;bld_call(g,0x403d5cu,a,2,&r);
    return bld_pair(angles,r.edx);
}

/* |s16 w| as a word, 8000h saturating to 7FFFh. */
static uint32_t bld_abs16(uint32_t w) {
    w&=0xffffu;
    if(w==0x8000u)return 0x7fffu;
    return fx_s16(w)<0?0x10000u-w:w;
}

/* Original: DBSIM.EXE 0x406868 */
uint64_t es2_part_facing_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t b,uint32_t p,const uint32_t entry[3]) {
    uint32_t point=g->scratch,angles=g->scratch+0xcu,a[3];
    uint32_t best=0x1000000u,best_index=0xffffu,eax=entry[0],flag=R32(b+0x201u);
    for(uint32_t i=0;;++i,flag+=2u) {
        uint32_t def=R32(b+0x1f2u);
        if(fx_s16(i)>=fx_s16(R16(def+0x12u)))break;
        if(!R16(flag))continue;
        if(!p)return bld_pair(i,b);
        es2_weapon_registers r={point,b,R32(b)};
        a[0]=b;a[1]=i;a[2]=point;bld_virtual(g,R32(r.ecx+0x58u),0x58,a,3,&r);
        r.eax=p+0x1au;r.edx=point;r.ecx=angles;a[0]=angles;a[1]=point;a[2]=p+0x1au;bld_call(g,0x492990u,a,3,&r);
        W16(angles+4u,R16(angles+4u)-R16(p+4u));
        W16(angles,R16(angles)-R16(p));
        W16(angles+2u,R16(angles+2u)-R16(p+2u));
        eax=bld_abs16(R16(angles))+bld_abs16(R16(angles+4u));
        if(fx_s32(eax)<fx_s32(best)) { best=eax;best_index=i; }
    }
    return bld_pair(bld_word(eax,best_index),b);
}
#undef W32
#undef W8
