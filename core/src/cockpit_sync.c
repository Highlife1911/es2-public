#include "es2/core/cockpit_sync.h"
#include "es2/core/divide.h"
#include "fixed.h"
/* The cockpit<->player synchronisation and its three small callees
 * (docs/reverse-engineering/hud.md, section "cockpit<->player synchronisation
 * as own code"). Every guest read and write happens where the original does it
 * (the callees may change the vehicle, the cockpit and the frame), and r holds
 * what the original's EAX/EDX/ECX hold at the next game call, including the
 * halves that 16-bit loads keep. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define VA(x) (g->image+(uint32_t)(x)-0x400000u)
#define LO16(reg,v) ((reg)=((reg)&0xffff0000u)|(uint16_t)(v))
#define LO8(reg,v) ((reg)=((reg)&0xffffff00u)|(uint8_t)(v))

/* The cockpit [49B088h] and its sub-objects; the vehicle fields. */
enum { H_POOL=0x1e5u, H_THROTTLE=0x1f1u, H_HUD=0x1f5u, H_TARGET=0x210u };
enum { P_HEADING=0x10u, P_POSITION=0x26u, P_FLAGS=0x92u, P_SETPOINT_NEW=0x93u, P_TARGET_SAME=0x9bu,
    P_TARGET_NEW=0x9du, P_HUD_OWN=0xa4u, P_TARGET=0x1a4u, P_CHASSIS=0x1f2u, P_WEAPONS=0x202u,
    P_SHIELDS=0x222u, P_SPEED=0x28eu, P_SETPOINT=0x290u, P_POOL=0x292u, P_TWIST=0x298u, P_PITCH=0x29au,
    P_FLYER_SETPOINT=0x2d7u, P_WEAPON=0x30bu, P_COUNTDOWN=0x31bu };
/* The frame, relative to its EBP: the aim point, the HUD record copy, the
 * aim words b and a, the throttle pair L. */
enum { F_AIM=0x40u, F_RECORD=0x34u, F_B=0xcu, F_A=0xau, F_RATIO=8u, F_SET=4u };

typedef struct cs_state {
    const es2_guest_memory *m;
    const es2_weapon_graph *g;
    es2_cockpit_sync *frame;
    uint32_t ebx, esi, edi;
    es2_weapon_registers r;
} cs_state;

/* A direct call of the original VA `va` whose CALL returns to `ret`. */
static void cs_call(cs_state *s, uint32_t va, uint32_t ret, const uint32_t *a, unsigned n) {
    const es2_weapon_graph *g = s->g;
    if(s->frame) {
        s->frame->call.ebx = s->ebx; s->frame->call.esi = s->esi; s->frame->call.edi = s->edi;
        s->frame->call.ret = VA(ret);
    }
    if(g->observe) g->observe(g->observer_context, VA(va), 0);
    g->call(g->context, VA(va), a, n, &s->r);
}
/* POP ECX after a one-slot call: the slot as the callee left it at the
 * original coordinate, else the pushed argument. */
static void cs_pop(cs_state *s, uint32_t arg) {
    const es2_guest_memory *m = s->m;
    s->r.ecx = s->frame && s->frame->top ? R32(s->frame->top - 4u) : arg;
}

/* Original: DBSIM.EXE 0x41b130 */
uint64_t es2_cockpit_sync_registers(const es2_guest_memory *m, const es2_weapon_graph *g, es2_cockpit_sync *frame,
    uint32_t caller_esi, uint32_t caller_edi, uint32_t player, const uint32_t entry[3]) {
    const uint32_t P = player, f = g->scratch + ES2_COCKPIT_SYNC_FRAME;
    cs_state s = {m, g, frame, P, caller_esi, caller_edi, {entry[0], entry[1], entry[2]}};
    es2_weapon_registers *r = &s.r;
    uint32_t H;
    /* 41b13b: the energy pool in Q10 (signed IDIV by 10000), the
     * EnergyPoolGauge. */
    {
        const int32_t pool = fx_s16(R16(P + P_POOL)) * 1024;
        r->eax = (uint32_t)(pool / 10000); r->edx = (uint32_t)(pool % 10000); r->ecx = 10000u;
        W32(VA(0x4a9ce8u), r->eax);
        r->eax = H = R32(VA(0x49b088u));
        r->edx = R32(H + H_POOL);
        const uint32_t a[2] = {r->edx, VA(0x4a9ce8u)};
        cs_call(&s, 0x4450f8u, 0x41b168u, a, 2);
    }
    /* 41b16b: shield balance, POP ECX, EAX <- 0. */
    {
        r->ecx = P + P_SHIELDS;
        const uint32_t a[1] = {r->ecx};
        cs_call(&s, 0x413bc8u, 0x41b177u, a, 1);
        cs_pop(&s, a[0]);
        r->eax = 0;
    }
    /* 41b17a: the target distance; ESI <- target+26h on both paths. */
    if(R32(P + P_TARGET)) {
        uint32_t ret;
        if(R32(VA(0x49ef34u)) && P == R32(VA(0x4d24e6u))) {
            s.esi = R32(P + P_TARGET) + P_POSITION;
            r->eax = R32(VA(0x4d2684u)) + P_POSITION;
            ret = 0x41b1acu;
        } else {
            s.esi = R32(P + P_TARGET) + P_POSITION;
            r->eax = P + P_POSITION;
            ret = 0x41b1c4u;
        }
        const uint32_t a[2] = {r->eax, s.esi};
        cs_call(&s, 0x49288cu, ret, a, 2);
    }
    /* 41b1c7: the weapon system with the distance (0 without a target). */
    {
        r->edx = R32(P + P_WEAPONS);
        const uint32_t a[2] = {r->edx, r->eax};
        cs_call(&s, 0x410b40u, 0x41b1d4u, a, 2);
    }
    /* 41b1d7: the throttle record into L. */
    {
        r->ecx = H = R32(VA(0x49b088u));
        r->eax = R32(H + H_THROTTLE);
        const uint32_t a[1] = {r->eax};
        cs_call(&s, 0x447df8u, 0x41b1e9u, a, 1);
        cs_pop(&s, a[0]);
        const uint32_t record = r->eax;
        r->edx = R32(record); W32(f - F_RATIO, r->edx);
        r->edx = R32(record + 4u); W32(f - F_SET, r->edx);
    }
    /* 41b1f5: the input's set point goes to the gauge, or the gauge's set
     * point comes back (a flyer chassis also keeps it in P+2D7h). */
    r->edx = P + P_FLAGS; r->eax = r->edx;
    if(R8(P + P_SETPOINT_NEW)) {
        r->eax = (uint32_t)fx_s16(R16(P + P_SETPOINT));
        W32(f - F_SET, r->eax);
        r->eax = r->edx;
        W8(P + P_SETPOINT_NEW, 0);
    } else {
        r->edx = R32(P + P_CHASSIS);
        if(R16(r->edx + 0x50u)) {
            LO16(r->ecx, R16(f - F_SET));
            W16(P + P_FLYER_SETPOINT, r->ecx);
        }
        LO16(r->eax, R16(f - F_SET));
        W16(P + P_SETPOINT, r->eax);
    }
    /* 41b238: the speed ratio (speed << 10) / the chassis limit, the
     * reverse limit negated as a word for a negative speed. */
    if(fx_s16(R16(P + P_SPEED)) < 0) {
        r->edx = R32(P + P_CHASSIS);
        LO16(r->ecx, 0u - R16(r->edx + 4u));
    } else {
        r->eax = R32(P + P_CHASSIS);
        LO16(r->ecx, R16(r->eax + 6u));
    }
    {
        const int32_t speed = fx_s16(R16(P + P_SPEED)) * 1024, limit = fx_s16(r->ecx);
        r->edx = r->ecx = (uint32_t)limit;
        /* CDQ; IDIV 0x41b26b: a zero limit is the original's fault. */
        r->eax = es2_divide_or_raise((uint64_t)(int64_t)speed, (uint32_t)limit, 1, &r->edx);
        W32(f - F_RATIO, r->eax);
    }
    /* 41b270: the throttle gauge takes L. */
    {
        r->eax = H = R32(VA(0x49b088u));
        r->edx = R32(H + H_THROTTLE);
        const uint32_t a[2] = {r->edx, f - F_RATIO};
        cs_call(&s, 0x447da8u, 0x41b285u, a, 2);
    }
    /* 41b288: the HUD record, copied in the listing's order and patched. */
    {
        r->ecx = H = R32(VA(0x49b088u));
        r->eax = R32(H + H_HUD);
        const uint32_t a[1] = {r->eax};
        cs_call(&s, 0x43dbb0u, 0x41b29au, a, 1);
        const uint32_t h = r->eax;
        LO16(r->edx, R16(h));
        cs_pop(&s, a[0]);
        W16(f - F_RECORD, r->edx);
        LO16(r->ecx, R16(h + 2u)); W16(f - F_RECORD + 2u, r->ecx);
        LO16(r->edx, R16(h + 4u)); W16(f - F_RECORD + 4u, r->edx);
        r->ecx = R32(h + 6u); W32(f - F_RECORD + 6u, r->ecx);
        LO16(r->edx, R16(h + 0x16u)); W16(f - F_RECORD + 0x16u, r->edx);
        LO16(r->ecx, R16(h + 0x18u)); W16(f - F_RECORD + 0x18u, r->ecx);
        LO16(r->edx, R16(h + 0x1au)); W16(f - F_RECORD + 0x1au, r->edx);
        r->ecx = R32(h + 0x1cu); W32(f - F_RECORD + 0x1cu, r->ecx);
        r->edx = R32(h + 0x20u); W32(f - F_RECORD + 0x20u, r->edx);
        LO8(r->ecx, R8(h + 0x24u)); W8(f - F_RECORD + 0x24u, r->ecx);
        LO8(r->edx, R8(h + 0x25u));
        r->eax = h + 0xau;
        W8(f - F_RECORD + 0x25u, r->edx);
        r->ecx = R32(r->eax + 8u); W32(f - F_RECORD + 0x12u, r->ecx);
        r->edx = R32(r->eax); W32(f - F_RECORD + 0xau, r->edx);
        r->edx = P + P_FLAGS;
        r->eax = R32(r->eax + 4u); W32(f - F_RECORD + 0xeu, r->eax);
        r->eax = r->edx;
        LO16(r->ecx, R16(P + P_PITCH)); W16(f - F_RECORD + 4u, r->ecx);
        if(!R8(P + P_HUD_OWN)) {
            LO16(r->eax, R16(P + P_HEADING)); W16(f - F_RECORD, r->eax);
            LO16(r->ecx, R16(P + P_TWIST)); W16(f - F_RECORD + 2u, r->ecx);
        }
    }
    /* 41b328: the cockpit target; a change moves the reference counts. */
    r->eax = H = R32(VA(0x49b088u));
    const uint32_t target = R32(H + H_TARGET);
    s.esi = target;
    if(target != R32(P + P_TARGET)) {
        r->eax = r->edx;
        W8(P + P_TARGET_SAME, 0);
        r->eax = target;
        /* 41b347: always unequal (the byte write cannot change the dword). */
        if(r->eax != R32(P + P_TARGET)) {
            if(R32(P + P_TARGET)) {
                r->ecx = R32(P + P_TARGET);
                W16(r->ecx + 0x1a2u, R16(r->ecx + 0x1a2u) - 1u);
            }
            W32(P + P_TARGET, r->eax);
            if(r->eax) {
                r->ecx = R32(P + P_TARGET);
                W16(r->ecx + 0x1a2u, R16(r->ecx + 0x1a2u) + 1u);
            }
            r->edx = P + P_FLAGS;
            W8(P + P_TARGET_NEW, 1u);
        }
        if(target && R32(P + P_WEAPON)) {
            r->eax = R32(P + P_WEAPON);
            const uint32_t a[2] = {r->eax, target};
            cs_call(&s, 0x40e484u, 0x41b3a0u, a, 2);
        }
        if(!R32(P + P_TARGET)) {
            r->eax = R32(P + P_WEAPONS) + 0x14u;
            if(R8(r->eax)) W16(P + P_COUNTDOWN + 1u, 4500u);
        }
    }
    /* 41b3c3: without a target the countdown runs; at 0 the torso
     * centering word 4D2504h is set. */
    if(!R32(P + P_TARGET)) {
        r->eax = R32(P + P_WEAPONS) + 0x14u;
        if(R8(r->eax)) {
            r->edx = P + P_COUNTDOWN;
            const uint32_t a[1] = {r->edx};
            cs_call(&s, 0x467a48u, 0x41b3e6u, a, 1);
            const int zero = (uint16_t)r->eax == 0;
            cs_pop(&s, a[0]);
            LO8(r->ecx, zero);
            r->ecx &= 1u;
            if((uint16_t)r->ecx) W16(VA(0x4d2504u), 1u);
        }
    }
    /* 41b3fe: the record's target and flag; the HUD record set. */
    {
        r->eax = P + P_FLAGS;
        W32(f - F_RECORD + 6u, s.esi);
        r->ecx = f - F_RECORD;
        LO8(r->edx, R8(P + P_TARGET_SAME));
        r->eax = H = R32(VA(0x49b088u));
        W8(f - F_RECORD + 0x25u, r->edx);
        r->edx = R32(H + H_HUD);
        const uint32_t a[2] = {r->edx, r->ecx};
        cs_call(&s, 0x43d9b4u, 0x41b422u, a, 2);
    }
    /* 41b425: the aim point with a target. */
    if(R32(P + P_TARGET)) {
        r->ecx = f - F_B; r->eax = f - F_A; r->edx = f - F_AIM;
        const uint32_t a[4] = {P, r->edx, r->eax, r->ecx};
        cs_call(&s, 0x41b728u, 0x41b440u, a, 4);
    }
    /* 41b443: the HUD's target and aim point (a and b as words). */
    {
        LO16(r->ecx, R16(f - F_B));
        r->edx = f - F_AIM;
        const uint32_t b = r->ecx;
        LO16(r->eax, R16(f - F_A));
        r->ecx = R32(P + P_TARGET);
        const uint32_t a[4] = {r->ecx, r->edx, r->eax, b};
        cs_call(&s, 0x434a24u, 0x41b45du, a, 4);
    }
    return (uint64_t)r->edx << 32 | r->eax;
}

/* Original: DBSIM.EXE 0x447da8 */
uint64_t es2_throttle_gauge_registers(const es2_guest_memory *m, const es2_weapon_graph *g, uint32_t gauge,
    uint32_t record, const uint32_t entry[3]) {
    const uint32_t G = gauge, L = record;
    es2_weapon_registers r = {entry[0], entry[1], entry[2]};
    /* 447db3: a new set point moves the slider: its class table [S+17h],
     * slot +14h, with the set point word in DX. */
    r.eax = R32(L + 4u);
    if(r.eax != R32(G + 0xb5u)) {
        LO16(r.edx, R16(L + 4u));
        r.ecx = R32(G + 0x18u);
        r.eax = R32(r.ecx + 0x17u);
        const uint32_t target = R32(r.eax + 0x14u);
        const uint32_t a[2] = {r.ecx, r.edx};
        if(g->observe) g->observe(g->observer_context, target, 0x14u);
        g->call(g->context, target, a, 2, &r);
    }
    /* 447dd0: a new ratio marks the slider for redraw; then the record. */
    r.edx = R32(L);
    if(r.edx != R32(G + 0xb1u)) {
        r.ecx = R32(G + 0x18u);
        W8(r.ecx + 0x1fu, 2u);
    }
    r.eax = R32(L); W32(G + 0xb1u, r.eax);
    r.eax = R32(L + 4u); W32(G + 0xb5u, r.eax);
    return (uint64_t)r.edx << 32 | r.eax;
}

/* Original: DBSIM.EXE 0x447df8 */
uint64_t es2_throttle_record_registers(uint32_t gauge, const uint32_t entry[3]) {
    return (uint64_t)entry[1] << 32 | (uint32_t)(gauge + 0xb1u);
}

/* Original: DBSIM.EXE 0x43dbb0 */
uint64_t es2_hud_record_registers(uint32_t hud, const uint32_t entry[3]) {
    return (uint64_t)entry[1] << 32 | (uint32_t)(hud + 0xb1u);
}
