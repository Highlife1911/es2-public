#include "es2/core/actor_virtuals.h"
#include "fixed.h"
/* Small actor virtuals (docs/reverse-engineering/vtables.md, section "actor
 * virtuals"). Registers are tracked where the original leaves them for a callee
 * or for the returned pair; the high words of 16-bit loads stay as the original
 * keeps them. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define VA(va) (g->image+(uint32_t)(va)-0x400000u)
/* Actor fields: angles +0Ch, matrix +12h, matrix-valid word +32h, shape
 * instance +34h, link +1A4h, configuration +1F2h, runtime part records
 * +205h (stride 0Bh). Configuration: configuration part records +14h
 * (stride 1Eh), part count +12h. */
enum { A_ANGLES=0xcu, A_MATRIX=0x12u, A_CACHED=0x32u, A_SHAPE=0x34u, A_LINK=0x1a4u, A_CONFIG=0x1f2u, A_PARTS=0x205u,
    C_PARTS=0x14u, C_COUNT=0x12u };
static uint64_t av_pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
/* A 16-bit register write: the high word stays. */
static uint32_t av_word(uint32_t high,uint32_t low) { return (high&0xffff0000u)|(uint16_t)low; }
static uint32_t av_sx16(uint32_t x) { return (uint32_t)fx_s16(x); }
static void av_call(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,
    es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}

/* Original: DBSIM.EXE 0x403548 (BASE +24h): the static 32-byte node record
 * 4a95cc, cleared (memset 49681a) on every call; words +0, +6 and +10h =
 * 4000h, dword +1Ch = the configuration's word +2Ch. The guard byte 497424
 * is raised to 1 on the first call and never read otherwise. */
uint64_t es2_base_aim_node_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t actor,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    const uint32_t node=VA(0x4a95ccu),guard=VA(0x497424u);
    if(!R8(guard))W8(guard,1u);
    uint32_t a[3]={node,0,0x20u};
    av_call(g,VA(0x49681au),0,a,3,&r);
    r.eax=av_word(r.eax,0x4000u);
    W16(node+0x10u,r.eax);W16(node+6u,r.eax);W16(node,r.eax);
    r.eax=node;
    r.edx=R32(actor+A_CONFIG);
    r.ecx=av_sx16(R16(r.edx+0x2cu));W32(node+0x1cu,r.ecx);
    return av_pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x40351c (BASE +30h): 411a74 (ACTOR +30h, clears
 * both 6-byte vectors), then second+4 = the configuration's word +2Ch. */
uint64_t es2_base_offsets_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t actor,uint32_t first,uint32_t second,const uint32_t entry[3]) {
    es2_weapon_registers r={first,entry[1],entry[2]};
    uint32_t a[3]={actor,first,second};
    av_call(g,VA(0x411a74u),0,a,3,&r);
    r.edx=R32(actor+A_CONFIG);
    r.ecx=av_word(r.ecx,R16(r.edx+0x2cu));W16(second+4u,r.ecx);
    return av_pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x4052b4 (BASE +40h): over the parts i < s16(C+12h)
 * (C and the count reread per step) the current words P_i+0 and the maximum
 * words Q_i+0, sign-extended and summed in 32 bits; EAX = (current << 8) /
 * maxima (IDIV), EDX = the remainder. */
uint64_t es2_base_condition_registers(const es2_guest_memory *m,uint32_t actor,uint32_t operands[2],int *fault) {
    uint32_t current=0,maxima=0;
    uint32_t q=R32(R32(actor+A_CONFIG)+C_PARTS),p=R32(actor+A_PARTS);
    for(uint32_t i=0;(int32_t)i<fx_s16(R16(R32(actor+A_CONFIG)+C_COUNT));++i,p+=0xbu,q+=0x1eu) {
        current+=av_sx16(R16(p));
        maxima+=av_sx16(R16(q));
    }
    const int32_t dividend=(int32_t)fx_s32(current<<8),divisor=(int32_t)fx_s32(maxima);
    operands[0]=(uint32_t)dividend;operands[1]=(uint32_t)divisor;
    *fault=divisor==0||(dividend==INT32_MIN&&divisor==-1);
    if(*fault)return 0;
    return av_pair((uint32_t)(dividend/divisor),(uint32_t)(dividend%divisor));
}

/* Original: DBSIM.EXE 0x411aec (ACTOR +50h): contact_share 412704(actor,
 * attacker) with EAX = actor, EDX = attacker; its EDX:EAX. */
uint64_t es2_actor_attacked_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t actor,uint32_t attacker,const uint32_t entry[3]) {
    (void)m;
    es2_weapon_registers r={actor,attacker,entry[2]};
    uint32_t a[2]={actor,attacker};
    av_call(g,VA(0x412704u),0,a,2,&r);
    return av_pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x406808 (BASE +58h): the heading matrix is rebuilt
 * (euler_matrix) while the word A+32h is 0, then 4801d4 moves the
 * configuration part record's offset vector (Q_part + 10h) through it into
 * out. */
uint64_t es2_base_part_position_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t actor,uint32_t part,uint32_t out,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    const uint32_t angles=actor+A_ANGLES,matrix=actor+A_MATRIX;
    uint32_t a[3];
    if(!R16(actor+A_CACHED)) {
        r.eax=matrix;r.edx=angles;
        a[0]=angles;a[1]=matrix;av_call(g,VA(0x47ebb8u),0,a,2,&r);
        W16(actor+A_CACHED,1u);
    }
    r.ecx=30u*av_sx16(part);
    r.edx=R32(R32(actor+A_CONFIG)+C_PARTS)+r.ecx+0x10u;
    r.eax=matrix;
    a[0]=r.edx;a[1]=matrix;a[2]=out;av_call(g,VA(0x4801d4u),0,a,3,&r);
    return av_pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x417b98 (MECH +24h): the shape's slot +20h finds
 * the node of the configuration word +0Ch (pushed with the entry ECX's high
 * word); a node with s16(node+4) > -1 gives the record S+16h + 32 * that
 * index, otherwise the default record 6c56a8. */
uint64_t es2_mech_aim_node_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t actor,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    r.eax=R32(actor+A_CONFIG);
    const uint32_t word=R16(r.eax+0xcu);
    r.edx=av_word(r.edx,word);r.ecx=av_word(r.ecx,word);
    const uint32_t shape=R32(actor+A_SHAPE);
    r.eax=R32(shape+4u);
    r.edx=R32(r.eax);
    uint32_t a[2]={r.eax,r.ecx};
    av_call(g,R32(r.edx+0x20u),0x20u,a,2,&r);
    const uint32_t node=r.eax;
    if(node&&fx_s16(R16(node+4u))>-1) {
        r.edx=av_sx16(R16(node+4u))<<5;
        r.eax=R32(shape+0x16u)+r.edx;
    } else r.eax=VA(0x6c56a8u);
    return av_pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x4155c4 (MECH +30h): first = (0, C+64h, C+66h),
 * second = (0, C+68h, C+6Ah), stored in the original's order. */
uint64_t es2_mech_offsets_registers(const es2_guest_memory *m,uint32_t actor,uint32_t first,uint32_t second) {
    const uint32_t e=R32(actor+A_CONFIG)+2u;
    W16(second,0);W16(first,0);
    W16(first+2u,R16(e+0x62u));
    W16(first+4u,R16(e+0x64u));
    const uint32_t edx=av_word(first,R16(e+0x66u));W16(second+2u,edx);
    const uint32_t eax=av_word(e,R16(e+0x68u));W16(second+4u,eax);
    return av_pair(eax,edx);
}

/* Original: DBSIM.EXE 0x4154d0 (MECH +34h): the armour word +222h for an
 * angle within [-4000h, 4000h) (low word), else +224h. EAX = EDX = the
 * actor's high word over the armour word. */
uint64_t es2_mech_armour_registers(const es2_guest_memory *m,uint32_t actor,uint32_t angle) {
    const uint32_t armour=actor+0x222u;
    const int front=(uint16_t)(angle+0x4000u)<0x8000u;
    const uint32_t edx=av_word(actor,front?R16(armour):R16(armour+2u));
    return av_pair(edx,edx);
}

/* Original: DBSIM.EXE 0x415498 (MECH +38h): a nonzero configuration word
 * +50h gives the dword A+2BDh (EDX the entry EDX); else multiply_q10(2000,
 * s16(A+28Eh)) with EAX = the configuration. */
uint64_t es2_mech_target_scale_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t actor,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    r.eax=R32(actor+A_CONFIG);
    if(R16(r.eax+0x50u))return av_pair(R32(actor+0x2bdu),r.edx);
    uint32_t a[2]={0x7d0u,av_sx16(R16(actor+0x28eu))};
    av_call(g,VA(0x47e0b0u),0,a,2,&r);
    return av_pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x41cabc (MECH +4Ch, actor A against target T): 0
 * without a draw when T's word +1A8h is nonzero. Else two rng_next draws on
 * stream A 4d2599: a = (draw1 & 3E8h) + A+29Eh, d = (draw2 & 3E8h) + T+29Eh
 * (16-bit); |a - d| < 400 (8000h counts as 7FFFh) gives 1, d > a gives 2,
 * else 0. Results keep the original's high words. */
uint64_t es2_mech_engage_roll_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t actor,uint32_t target,const uint32_t entry[3]) {
    es2_weapon_registers r={target,entry[1],entry[2]};
    if(R16(target+0x1a8u))return av_pair(0,r.edx);
    const uint32_t stream=VA(0x4d2599u);
    uint32_t a[1]={stream};
    av_call(g,VA(0x492ee0u),0,a,1,&r);
    r.eax=av_word(r.eax,r.eax&0x3e8u);
    r.ecx=stream;
    r.eax=av_word(r.eax,r.eax+R16(actor+0x29eu));
    const uint32_t first=r.eax;
    av_call(g,VA(0x492ee0u),0,a,1,&r);
    r.ecx=stream;
    r.edx=av_word(r.eax,r.eax&0x3e8u);
    r.edx=av_word(r.edx,r.edx+R16(target+0x29eu));
    r.eax=av_word(first,first-r.edx);
    uint32_t spread;
    if((uint16_t)r.eax==0x8000u)spread=0x7fffu;
    else if(fx_s16(r.eax)>=0)spread=r.eax;
    else spread=0u-r.eax;
    if(fx_s16(spread)<0x190)return av_pair(av_word(r.eax,1u),r.edx);
    if(fx_s16(r.edx)<=fx_s16(first))return av_pair(0,r.edx);
    return av_pair(av_word(r.eax,2u),r.edx);
}

/* Original: DBSIM.EXE 0x41dd2c (MECH +64h): with a link A+1A4h, its
 * position (+26h..+31h) is copied to A+31Eh (z first, then x, y) and the
 * byte A+ADh set. EAX = A+92h, EDX = A+31Eh; without a link EAX = A, EDX = 0. */
uint64_t es2_mech_link_position_registers(const es2_guest_memory *m,uint32_t actor,const uint32_t entry[3]) {
    (void)entry;
    const uint32_t link=R32(actor+A_LINK);
    if(!link)return av_pair(actor,0);
    const uint32_t source=link+0x26u,target=actor+0x31eu;
    W32(target+8u,R32(source+8u));
    W32(target,R32(source));
    W32(target+4u,R32(source+4u));
    W8(actor+0x92u+0x1bu,1u);
    return av_pair(actor+0x92u,target);
}
