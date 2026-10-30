#include "es2/core/ai_order.h"
#include "fixed.h"
/* The AI order helpers 411CB4, 413E50, 41C0F4 and 41EB34
 * (docs/reverse-engineering/mech_order.md, section "AI order helpers"). The
 * register struct r follows EAX, EDX and ECX wherever a game call or the result
 * sees them. Memory is read again wherever the original reads it again. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
/* A DBSIM VA (globals, direct callees, pushed script tables) inside the
 * image at g->image. */
#define VA(va) (g->image+(uint32_t)(va)-0x400000u)

static uint64_t ao_pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
/* A 16-bit register write: the high word stays. */
static uint32_t ao_lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static void ao_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,VA(va),0);
    g->call(g->context,VA(va),a,n,r);
}

/* Original: DBSIM.EXE 0x411cb4 */
uint64_t es2_target_class_registers(const es2_guest_memory *m,uint32_t target,const uint32_t entry[3]) {
    const uint32_t P=R32(target+0x4du);
    if(!P)return ao_pair(0,entry[1]);
    const uint32_t S=P+8u;
    if(R8(S+5u))return ao_pair(ao_lo(target,2),S);
    if(R8(S+4u))return ao_pair(ao_lo(target,1),S);
    return ao_pair(0,S);
}

/* Original: DBSIM.EXE 0x413e50 */
uint64_t es2_script_start_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t record,uint32_t table) {
    const uint32_t S=record,stagger=VA(0x4a9b70u);
    es2_weapon_registers r;uint32_t a[3];
    W32(S,table);W32(S+9u,0);
    const uint32_t start=R32(R32(S)+4u);
    W16(stagger,R16(stagger)+0xdu);
    const uint32_t c=R16(stagger)&0xfu;
    W32(S+5u,start+c);
    r.eax=S+0x35u;r.edx=c;r.ecx=c;
    a[0]=S+0x35u;a[1]=0;a[2]=0x10;ao_call(g,0x49681au,a,3,&r);
    r.eax=S+0x35u;W8(S+0x36u,1);
    a[0]=S+0xdu;a[1]=0;a[2]=0x28;ao_call(g,0x49681au,a,3,&r);
    return ao_pair(r.eax,r.edx);
}

/* 413E50 (A+4Dh, table) with the registers of the call site. */
static void ao_script(const es2_weapon_graph *g,uint32_t A,uint32_t table,es2_weapon_registers *r) {
    uint32_t a[2]={A+0x4du,table};
    ao_call(g,0x413e50u,a,2,r);
}

/* Original: DBSIM.EXE 0x41c0f4 */
uint64_t es2_attack_target_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t vehicle,uint32_t target) {
    const uint32_t A=vehicle,T=target;
    es2_weapon_registers r={A,T,T};uint32_t a[1];
    if(T!=R32(A+0x1a4u)) {
        if(R32(A+0x1a4u)) { const uint32_t old=R32(A+0x1a4u);W16(old+0x1a2u,R16(old+0x1a2u)-1u); }
        W32(A+0x1a4u,T);
        if(T) { r.edx=R32(A+0x1a4u);W16(r.edx+0x1a2u,R16(r.edx+0x1a2u)+1u); }
        W8(A+0x9du,1);
    }
    const uint32_t v=(uint32_t)fx_s16(R16(T+0x1a8u));
    switch(v) {
    case 0: /* 41D5EC: the AI function 41CF18 */
        r.edx=A+0x92u;W8(A+0xacu,1);
        a[0]=A;ao_call(g,0x41d5ecu,a,1,&r);r.ecx=A;  /* POP ECX */
        break;
    case 1:case 3:
        r.edx=0;r.eax=A+0x4du;ao_script(g,A,VA(0x499518u),&r);
        break;
    case 2:
        r.edx=0;r.eax=A+0x4du;ao_script(g,A,VA(0x499556u),&r);
        break;
    default: /* SUB 1, DEC, DEC */
        r.edx=v-3u;
        break;
    }
    return ao_pair(r.eax,r.edx);
}

/* The tail the original inlines four times: drop A's target. */
static void ao_clear(const es2_guest_memory *m,uint32_t A,es2_weapon_registers *r) {
    r->edx=R32(A+0x1a4u);
    if(!r->edx)return;
    r->ecx=R32(A+0x1a4u);W16(r->ecx+0x1a2u,R16(r->ecx+0x1a2u)-1u);
    r->edx=0;W32(A+0x1a4u,0);
    r->eax=A+0x92u;W8(A+0x9du,1);
}

/* Original: DBSIM.EXE 0x41eb34 */
uint64_t es2_order_resume_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t vehicle,const uint32_t entry[3]) {
    const uint32_t A=vehicle;
    es2_weapon_registers r={A+0x92u,entry[1],entry[2]};uint32_t a[2];
    if(R8(A+0xa3u)) { /* the vehicle's own flag B+11h */
        r.edx=R32(A+0x1f2u);
        r.edx=R16(r.edx+0x50u)?VA(0x499420u):VA(0x4993e2u);
        r.eax=r.edx;r.ecx=A+0x4du;ao_script(g,A,r.edx,&r);
        ao_clear(m,A,&r);
        return ao_pair(ao_lo(r.eax,1),r.edx);
    }
    r.eax=R32(A+0x45u);r.ecx=R32(r.eax+0xcu);r.eax=R32(r.ecx);
    if(r.eax==R32(VA(0x4a9b84u))&&R16(A+0x23eu)) { /* the player leads: its order state */
        r.edx=(uint32_t)fx_s16(R16(A+0x23eu));
        switch(r.edx) {
        case 1:case 2:
            r.ecx=A+0x4du;ao_script(g,A,VA(0x499594u),&r);ao_clear(m,A,&r);
            break;
        case 4: {
            r.eax=R32(A+0x248u)+0x92u;
            if(R8(r.eax+7u)) {
                W16(A+0x23eu,0);a[0]=A;ao_call(g,0x41eb34u,a,1,&r);r.ecx=A;  /* POP ECX */
            } else {
                r.edx=R32(A+0x248u);a[0]=A;a[1]=r.edx;ao_call(g,0x41c0f4u,a,2,&r);
            }
            break; }
        case 6:
            r.eax=A+0x4du;ao_script(g,A,VA(0x499746u),&r);ao_clear(m,A,&r);
            break;
        default: /* 3, 5, above 6 or negative (0 took the other path) */
            break;
        }
        return ao_pair(ao_lo(r.eax,1),r.edx);
    }
    /* The group's current order type, 0Bh for an empty slot. */
    r.eax=R32(A+0x45u);r.ecx=R32(r.eax+0x6cu);
    if(!R32(r.eax+4u*r.ecx+0x44u))r.eax=ao_lo(r.eax,0xb);
    else { r.ecx=R32(r.eax+0x6cu);r.eax=R32(r.eax+4u*r.ecx+0x44u);r.eax=ao_lo(r.eax,R16(r.eax)); }
    r.ecx=(uint32_t)fx_s16(r.eax);
    switch(r.ecx) {
    case 0:r.edx=VA(0x49968cu);break;
    case 1:r.edx=VA(0x4997c2u);break;
    case 2:r.edx=VA(0x499746u);break;
    case 3:r.edx=VA(0x499594u);break;
    case 4:r.edx=VA(0x4996cau);break;
    case 5:r.eax=R32(A+0x1f2u);r.edx=fx_s16(R16(r.eax+0x22u))>0x7d00?VA(0x49964eu):VA(0x4995d2u);break;
    case 6:r.edx=VA(0x499610u);break;
    default:break; /* the entry EDX */
    }
    r.eax=r.edx;r.ecx=A+0x4du;ao_script(g,A,r.edx,&r);
    ao_clear(m,A,&r);
    return ao_pair(ao_lo(r.eax,1),r.edx);
}
