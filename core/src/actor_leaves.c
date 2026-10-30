#include "es2/core/actor_leaves.h"
#include "fixed.h"
/* Actor slot leaves (docs/reverse-engineering/vtables.md, section "actor slot
 * leaves"). Reads follow the original's order (a fault on an unmapped pointer
 * happens at the same read). */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define W32(a,v) m->write32(m->context,(a),(v))
static uint64_t al_pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
/* A 16-bit load into a register keeps its high word. */
static uint32_t al_lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(word&0xffffu); }
static uint32_t al_sx16(uint32_t x) { return (uint32_t)fx_s16(x); }
/* EAX = value, EDX and ECX = entry. */
static uint64_t al_const(uint32_t eax,const uint32_t entry[3],uint32_t *ecx) {
    *ecx=entry[2];
    return al_pair(eax,entry[1]);
}

/* Original: DBSIM.EXE 0x406cfe, 0x411a4c, 0x411a54, 0x411a5c, 0x411a64,
 * 0x411a6c, 0x411a9c, 0x411aa4, 0x411aac, 0x4786cb (XOR EAX,EAX) and
 * 0x4215c0 (MOV AX,[EBP+0Ch] then XOR EAX,EAX). */
uint64_t es2_base_decide_registers(const uint32_t entry[3],uint32_t *ecx) { return al_const(0u,entry,ecx); }
uint64_t es2_actor_armour_registers(const uint32_t entry[3],uint32_t *ecx) { return al_const(0u,entry,ecx); }
uint64_t es2_actor_target_scale_registers(const uint32_t entry[3],uint32_t *ecx) { return al_const(0u,entry,ecx); }
uint64_t es2_actor_angle_registers(const uint32_t entry[3],uint32_t *ecx) { return al_const(0u,entry,ecx); }
uint64_t es2_actor_order_registers(const uint32_t entry[3],uint32_t *ecx) { return al_const(0u,entry,ecx); }
uint64_t es2_actor_keys_registers(const uint32_t entry[3],uint32_t *ecx) { return al_const(0u,entry,ecx); }
uint64_t es2_actor_aim_node_registers(const uint32_t entry[3],uint32_t *ecx) { return al_const(0u,entry,ecx); }
uint64_t es2_actor_radius_registers(const uint32_t entry[3],uint32_t *ecx) { return al_const(0u,entry,ecx); }
uint64_t es2_actor_sight_radius_registers(const uint32_t entry[3],uint32_t *ecx) { return al_const(0u,entry,ecx); }
uint64_t es2_object_zero_registers(const uint32_t entry[3],uint32_t *ecx) { return al_const(0u,entry,ecx); }
uint64_t es2_flyer_keys_registers(const uint32_t entry[3],uint32_t *ecx) { return al_const(0u,entry,ecx); }
/* Original: DBSIM.EXE 0x405308, 0x411ab4, 0x411ac0, 0x411b04 (MOV AX,1). */
uint64_t es2_base_in_front_registers(const uint32_t entry[3],uint32_t *ecx) { return al_const(al_lo(entry[0],1u),entry,ecx); }
uint64_t es2_actor_move_registers(const uint32_t entry[3],uint32_t *ecx) { return al_const(al_lo(entry[0],1u),entry,ecx); }
uint64_t es2_actor_engage_roll_registers(const uint32_t entry[3],uint32_t *ecx) { return al_const(al_lo(entry[0],1u),entry,ecx); }
uint64_t es2_actor_mount_word_registers(const uint32_t entry[3],uint32_t *ecx) { return al_const(al_lo(entry[0],1u),entry,ecx); }
/* Original: DBSIM.EXE 0x411b10 (MOV EAX,1). */
uint64_t es2_actor_part_word_registers(const uint32_t entry[3],uint32_t *ecx) { return al_const(1u,entry,ecx); }

/* Original: DBSIM.EXE 0x403594, 0x415488 (the object's own word). */
uint64_t es2_gun_angle_registers(const es2_guest_memory *m,uint32_t object,const uint32_t entry[3],uint32_t *ecx) {
    return al_const(al_lo(object,R16(object+0x20fu)),entry,ecx);
}
uint64_t es2_mech_angle_registers(const es2_guest_memory *m,uint32_t object,const uint32_t entry[3],uint32_t *ecx) {
    return al_const(al_lo(object,R16(object+0x298u)),entry,ecx);
}
/* Original: DBSIM.EXE 0x4035a4, 0x415518, 0x41552c (a word of C). */
uint64_t es2_base_radius_registers(const es2_guest_memory *m,uint32_t object,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t c=R32(object+0x1f2u);
    return al_const(al_lo(c,R16(c+0x2au)),entry,ecx);
}
uint64_t es2_mech_radius_registers(const es2_guest_memory *m,uint32_t object,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t c=R32(object+0x1f2u);
    return al_const(al_lo(c,R16(c+0x70u)),entry,ecx);   /* 415524 */
}
uint64_t es2_mech_sight_radius_registers(const es2_guest_memory *m,uint32_t object,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t c=R32(object+0x1f2u);
    return al_const(al_lo(c,R16(c+0x70u)),entry,ecx);   /* 415538 */
}

/* Original: DBSIM.EXE 0x4035b8 */
uint64_t es2_base_sight_radius_registers(const es2_guest_memory *m,uint32_t object,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t c=R32(object+0x1f2u);
    if(!R16(c+6u)) { *ecx=entry[2];return al_pair(0,c); }     /* 4035ca */
    const uint32_t again=R32(object+0x1f2u);                  /* 4035cc: reread */
    *ecx=again;
    uint32_t edx=0;                                            /* 4035f2 */
    if(R16(again+4u)!=0xffffu) {                               /* 4035d7 */
        const uint32_t b=R32(object+0x205u);
        if(fx_s16(R16(b+5u))<2) {                              /* 4035e4 */
            const uint8_t flag=*(const uint8_t *)m->host(m->context,object+0x99u);
            if(flag)edx=1u;                                    /* 4035f0 -> 4035f6 */
        }
    }
    if(edx&0xffffu)return al_pair(0,edx);                     /* 4035fe -> 403607 */
    return al_pair(al_lo(object,R16(again+0x2au)),edx);
}

/* Original: DBSIM.EXE 0x40360c */
uint64_t es2_base_part_word_registers(const es2_guest_memory *m,uint32_t object,uint32_t index,const uint32_t entry[3],uint32_t *ecx) {
    (void)entry;
    const uint32_t table=R32(object+0x201u);
    const uint32_t word=al_sx16(R16(table+2u*index));
    *ecx=word;
    return al_pair(word,index);
}
/* Original: DBSIM.EXE 0x415540 */
uint64_t es2_mech_part_word_registers(const es2_guest_memory *m,uint32_t object,uint32_t index,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t table=R32(object+0x20eu);
    *ecx=entry[2];
    return al_pair(al_sx16(R16(table+2u*index)),index);
}
/* Original: DBSIM.EXE 0x4155ac */
uint64_t es2_mech_mount_word_registers(const es2_guest_memory *m,uint32_t object,uint32_t word,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t k=al_sx16(word);
    const uint32_t p=R32(object+0x202u);
    *ecx=entry[2];
    return al_pair(al_lo(p,R16(p+2u*k+0x0au)),k);
}

/* Original: DBSIM.EXE 0x403624 */
uint64_t es2_base_next_part_registers(const es2_guest_memory *m,uint32_t object,uint32_t index,uint32_t out,
    const uint32_t entry[3],uint32_t *ecx) {
    (void)entry;
    const uint32_t start=(int32_t)index<0?0u:index;           /* 403630 */
    uint32_t k=start;
    for(;;) {
        ++k;
        const uint32_t count=al_sx16(R16(R32(object+0x1f2u)+0x12u));
        if(k==count)k=0;                                       /* 403643 */
        if(start==k)break;                                     /* 403649 */
        if(R16(R32(object+0x201u)+2u*k))break;                 /* 403656 */
    }
    if(start==k)k=0xffffffffu;                                 /* 40365a */
    W32(out,k);
    *ecx=start;
    return al_pair(k,out);
}
/* Original: DBSIM.EXE 0x415558 */
uint64_t es2_mech_next_part_registers(const es2_guest_memory *m,uint32_t image,uint32_t object,uint32_t index,uint32_t out,
    const uint32_t entry[3],uint32_t *ecx) {
    (void)entry;
    const uint32_t slots=image+0x49a060u-0x400000u;
    const uint32_t words=R32(object+0x20eu);
    const uint32_t start=(int32_t)index<0?0u:index;           /* 41556a */
    uint32_t k=start;
    for(;;) {
        ++k;
        if(k==7u)k=0;                                          /* 415574 */
        if(start==k)break;                                     /* 41557a */
        const uint32_t part=al_sx16(R16(slots+2u*k));
        if(R16(words+2u*part))break;                           /* 415589 */
    }
    if(start==k)k=0xffffffffu;                                 /* 41558d */
    const uint32_t edx=k==0xffffffffu?0xffffffffu:al_sx16(R16(slots+2u*k)); /* 415595 */
    W32(out,edx);
    *ecx=out;
    return al_pair(k,edx);
}
/* Original: DBSIM.EXE 0x411b1c */
uint64_t es2_actor_next_part_registers(const es2_guest_memory *m,uint32_t out,const uint32_t entry[3],uint32_t *ecx) {
    W32(out,0xffffffffu);
    *ecx=entry[2];
    return al_pair(0xffffffffu,0xffffffffu);
}

/* Original: DBSIM.EXE 0x411acc */
uint64_t es2_actor_in_front_registers(const es2_guest_memory *m,uint32_t object,uint32_t angle,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t dx=(angle-R16(object+0x10u)+0x3800u)&0xffffu;
    *ecx=entry[2];
    return al_pair(dx<0x7000u?1u:0u,al_lo(entry[1],dx));
}

/* Original: DBSIM.EXE 0x42200c */
uint64_t es2_actor_mark_registers(const es2_guest_memory *m,uint32_t object,const uint32_t entry[3],uint32_t *ecx) {
    *(uint8_t *)m->host(m->context,object+0xb1u)=1u;
    *ecx=entry[2];
    return al_pair(object+0x92u,entry[1]);
}

/* One game call; the observer key is the callee's address in this image. */
static void al_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    const uint32_t target=g->image+va-0x400000u;
    if(g->observe)g->observe(g->observer_context,target,0);
    g->call(g->context,target,a,n,r);
}
/* part_condition 0x40db2c(P) with EAX = P and the entry EDX/ECX. */
static uint64_t al_condition(const es2_weapon_graph *g,uint32_t pointer,const uint32_t entry[3]) {
    const uint32_t a[1]={pointer};
    es2_weapon_registers r={pointer,entry[1],entry[2]};
    al_call(g,0x40db2cu,a,1,&r);
    return al_pair(r.eax,r.edx);
}
/* Original: DBSIM.EXE 0x415504; POP ECX takes the argument back (the
 * callee never writes its argument slot). */
uint64_t es2_mech_condition_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t object,
    const uint32_t entry[3],uint32_t *ecx) {
    (void)m;
    const uint64_t result=al_condition(g,object+0x206u,entry);
    *ecx=object+0x206u;   /* 415515 */
    return result;
}
/* Original: DBSIM.EXE 0x421e84 */
uint64_t es2_flyer_condition_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t object,
    const uint32_t entry[3],uint32_t *ecx) {
    (void)m;
    const uint64_t result=al_condition(g,object+0x200u,entry);
    *ecx=object+0x200u;   /* 421e95 */
    return result;
}
/* Original: DBSIM.EXE 0x421584 */
uint64_t es2_flyer_target_scale_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t object,
    const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t a[2]={0x3e8u,al_sx16(R16(object+0x233u))};
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    al_call(g,0x47e0b0u,a,2,&r);
    *ecx=r.ecx;
    return al_pair(r.eax,r.edx);
}

/* Not hookable (prologue whitelist): 0x411a3c, 0x411b2c, 0x411b34,
 * 0x4127f8 (PUSH EBP; MOV EBP,ESP; POP EBP; RET) and 0x411a44 (OR EAX,-1). */
uint64_t es2_actor_empty_registers(const uint32_t entry[3],uint32_t *ecx) { return al_const(entry[0],entry,ecx); }
uint64_t es2_actor_no_part_registers(const uint32_t entry[3],uint32_t *ecx) { return al_const(0xffffffffu,entry,ecx); }

/* A virtual call: the resolved target and the vtable offset go to the
 * observer, the arguments and registers to the bridge. */
static void al_virtual(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}

/* Original: DBSIM.EXE 0x411b3c (ACTOR and FLYER slot +70h: the default
 * radial hit). Order of the reads, calls and registers as in the listing
 * 0x411b3c..0x411b8c; EBX = O, ESI = reach, EDI = s16(reach). */
uint64_t es2_actor_radial_hit_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t object,
    uint32_t damage,uint32_t point,uint32_t reach,uint32_t source,const uint32_t entry[3],uint32_t *ecx) {
    uint32_t a[4];
    /* 411b49: slot +5Ch(O) through the vtable; EAX = the vtable, EDX and
     * ECX stay the entry values; POP ECX at 411b4e takes the argument back. */
    uint32_t vtable=R32(object);
    es2_weapon_registers r={vtable,entry[1],entry[2]};
    a[0]=object;al_virtual(g,R32(vtable+0x5cu),0x5cu,a,1,&r);
    /* 411b4f: EDX = s16(AX) is also left on the stack under the call's two
     * arguments (popped at 411b68 into EDX again). */
    const uint32_t radius=al_sx16(r.eax);
    /* 411b56: dist_gate(point, O+26h) with EAX = point, EDX = radius, ECX = O+26h. */
    r.eax=point;r.edx=radius;r.ecx=object+0x26u;
    a[0]=point;a[1]=object+0x26u;al_call(g,0x49288cu,a,2,&r);
    /* 411b63..411b76: ECX = dist - radius; EAX = s16(reach) - ECX; EAX *= s16(damage)
     * (IMUL r/m32, the low dword; CDQ discards the high one); IDIV s16(reach). */
    const uint32_t reach16=al_sx16(reach);
    const uint32_t scaled=(reach16-(r.eax-radius))*al_sx16(damage);
    const int64_t dividend=fx_s32(scaled),divisor=fx_s32(reach16);
    /* Original IDIV: nonzero divisor (reach), quotient within 32 bits. */
    const uint32_t quotient=(uint32_t)(dividend/divisor),remainder=(uint32_t)(dividend%divisor);
    /* 411b7b..411b82: slot +74h(O, 0, quotient, source), EAX = the vtable (reread),
     * EDX = the remainder, ECX = source; its EDX:EAX and ECX are the result. */
    vtable=R32(object);
    r.eax=vtable;r.edx=remainder;r.ecx=source;
    a[0]=object;a[1]=0;a[2]=quotient;a[3]=source;
    al_virtual(g,R32(vtable+0x74u),0x74u,a,4,&r);
    *ecx=r.ecx;
    return al_pair(r.eax,r.edx);
}
