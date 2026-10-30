#include "es2/core/torso.h"
#include "fixed.h"

/* Torso servos 41a550/41a808, weapon convergence 41a74c and the weapon keys
 * 410f78. Contract: input.md. The register struct r follows EAX/EDX/ECX through
 * every path so that each game call sees the original's entry registers; lo()
 * is a 16-bit load or operation that keeps the register's high word. */
static uint64_t pair(uint32_t a,uint32_t d) { return (uint64_t)d<<32|a; }
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
static int32_t s16(uint32_t x) { return fx_s16(x); }
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
static uint32_t sx8(uint32_t x) { return (uint32_t)(int32_t)(int8_t)(uint8_t)x; }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static void ts_call(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,
                    es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}
#define CALL(va,n) ts_call(g,g->image+(va)-0x400000u,0,a,n,&r)
#define VCALL(target,slot,n) ts_call(g,(target),(slot),a,n,&r)

/* The word |x| as the servos compute it: 8000h gives 7FFFh in the low word
 * of `keep`, otherwise the source register is copied whole and its low word
 * negated when negative. */
static uint32_t ts_magnitude(uint32_t source,uint32_t keep) {
    if((uint16_t)source==0x8000u)return lo(keep,0x7fff);
    if(s16(source)<0)return lo(source,0u-source);
    return source;
}

/* The fields that differ between the twist and the pitch servo. */
typedef struct ts_servo {
    uint32_t scale,step,rate,angle,track,clip,latch;
    int pitch;
} ts_servo;

static uint64_t ts_servo_run(const es2_guest_memory *m,const es2_weapon_graph *g,const ts_servo *v,uint32_t P,
    uint32_t rate,uint32_t distance,uint32_t target,uint32_t snap,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[3];
    const uint32_t C=R32(P+0x1f2u)+2u;
    uint32_t si=sx(rate);
    r.eax=sx(R16(C+v->scale));r.edx=r.eax;
    a[0]=si;a[1]=r.edx;CALL(0x47e0a0,2);
    r.ecx=lo(r.ecx,R16(P+v->rate));si=r.eax;
    uint16_t before=(uint16_t)r.ecx;
    r.eax=lo(r.eax,R16(P+v->angle));
    const uint16_t old_angle=(uint16_t)r.eax;
    r.edx=ts_magnitude(si,r.edx);
    r.eax=lo(r.eax,R16(P+v->rate));
    r.ecx=ts_magnitude(r.eax,r.ecx);
    if(s16(r.edx)<=s16(r.ecx)) { before=(uint16_t)si;W16(P+v->rate,si); }
    else {
        /* Step limited approach of the rate to the scaled input. */
        r.eax=lo(r.eax,R16(C+v->step));a[0]=r.eax;CALL(0x467924,1);r.ecx=a[0];
        a[0]=P+v->rate;a[1]=si;a[2]=r.eax;r.edx=P+v->rate;CALL(0x467adc,3);
    }
    /* Integrate the mean of the old and the new rate. */
    r.eax=sx(before);r.edx=sx(R16(P+v->rate));si=lo(si,R16(P+v->angle));
    r.eax=fx_sar(r.eax+r.edx,1);
    r.ecx=lo(r.ecx,r.eax);a[0]=r.ecx;CALL(0x467924,1);
    si=lo(si,si+r.eax);r.ecx=a[0];
    if(v->pitch) {
        r.eax=lo(r.eax,R16(C+0x28u));r.edx=lo(r.edx,R16(C+0x2au));
    } else {
        r.edx=lo(r.edx,0u-R16(C+0x20u));r.eax=lo(r.eax,R16(C+0x20u));
    }
    if(s16(r.eax)>s16(si))r.eax=s16(r.edx)<s16(si)?si:r.edx;
    W16(P+v->angle,r.eax);
    if((uint16_t)snap) {
        /* Stop at the target when the step crossed it, or reached it from above. */
        r.eax=lo(r.eax,R16(P+v->angle)-target);r.edx=lo(r.edx,old_angle-target);
        const int32_t now=s16(r.eax),was=s16(r.edx);
        if((now<0&&was>0)||(now>=0&&was<0)||(now==0&&was>0)) {
            r.ecx=lo(r.ecx,target);W16(P+v->angle,target);W16(P+v->rate,0);
        }
    }
    r.eax=R32(P+v->track);
    if(r.eax) {
        r.edx=(uint32_t)R16(P+v->angle)>>2;r.ecx=lo(r.ecx,R16(C+v->clip));
        a[0]=r.eax;a[1]=r.ecx;a[2]=r.edx;CALL(0x479344,3);
    }
    if(v->pitch) { r.eax=distance;a[0]=P;a[1]=distance;CALL(0x41a74c,2); }
    r.eax=P+0x92u;
    if(R8(P+0xa3u)) {
        /* Sound latch (original code); its rate slot is the whole EDX. */
        r.edx=lo(r.edx,rate);a[0]=P;a[1]=r.edx;CALL(v->latch,2);
    }
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x41a550 */
uint64_t es2_torso_twist_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t P,uint32_t rate,
    uint32_t target,uint32_t snap,const uint32_t entry[3]) {
    static const ts_servo twist={0x1c,0x1e,0x294,0x298,0x230,0x1a,0x41a6d0,0};
    return ts_servo_run(m,g,&twist,P,rate,0,target,snap,entry);
}

/* Original: DBSIM.EXE 0x41a808 */
uint64_t es2_torso_pitch_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t P,uint32_t rate,
    uint32_t distance,uint32_t target,uint32_t snap,const uint32_t entry[3]) {
    static const ts_servo pitch={0x24,0x26,0x296,0x29a,0x234,0x22,0x41a994,1};
    return ts_servo_run(m,g,&pitch,P,rate,distance,target,snap,entry);
}

/* Original: DBSIM.EXE 0x41a74c. The frame lives in guest scratch: f is the
 * synthetic EBP; B = (0, d, 0) at f-18h, A at f-24h, out at f-0Ch. */
uint64_t es2_weapon_converge_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t P,
    uint32_t distance,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[3];
    const uint32_t f=g->scratch+ES2_WEAPON_CONVERGE_SCRATCH;
    r.eax=R32(P+0x1f2u)+2u;W32(f-4u,r.eax);
    for(uint32_t i=0;;++i) {
        r.edx=R32(P+0x202u);
        if(s16(i)>=s16(R16(r.edx+8u)))break;
        r.eax=sx(i);r.edx=R32(P+0x202u);r.ecx=R32(r.edx+4u);
        const uint32_t S=R32(r.ecx+4u*r.eax);
        if(!S)continue;
        if(distance) {
            r.eax=distance;r.edx=0;W32(f-0x18u,0);W32(f-0x14u,distance);r.ecx=0;W32(f-0x10u,0);
            a[0]=S;CALL(0x40f540,1);
            r.edx=sx(R16(r.eax+4u));r.ecx=S;W32(f-0x1cu,r.edx);
            r.ecx=sx(R16(r.eax));W32(f-0x24u,r.ecx);r.ecx=f-0x24u;
            r.eax=sx(R16(r.eax+2u));W32(f-0x20u,r.eax);
            r.eax=R32(f-4u);r.edx=sx(R16(r.eax+0x64u));W32(f-0x1cu,R32(f-0x1cu)-r.edx);
            /* aim_angles(out, B, A) */
            r.eax=f-0x18u;r.edx=f-0xcu;a[0]=r.edx;a[1]=r.eax;a[2]=r.ecx;CALL(0x492990,3);
        } else {
            r.ecx=0;W16(f-8u,0);W16(f-0xau,0);W16(f-0xcu,0);
        }
        r.eax=R32(f-0xcu);W32(S+0x24u,r.eax);
        r.eax=lo(r.eax,R16(f-8u));W16(S+0x28u,r.eax);
    }
    return pair(r.eax,r.edx);
}

/* -/= and Num-/Num+: the selected weapon's virtual +38h with the flag. */
static uint64_t ts_selected(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t W,uint32_t flag,uint32_t si,
    es2_weapon_registers r) {
    uint32_t a[2];
    r.eax=flag;r.edx=R8(W+0x1du);
    r.ecx=sx8(r.edx)+1u;
    if(fx_s32(r.ecx)<=0)return pair(si,r.edx);
    r.eax=sx8(r.edx);r.edx=R32(W+4u);r.ecx=R32(r.edx+4u*r.eax);
    a[0]=r.ecx;a[1]=flag;r.eax=R32(r.ecx);VCALL(R32(r.eax+0x38u),0x38,2);
    return pair(si,r.edx);
}

/* Original: DBSIM.EXE 0x410f78. The compare chain leaves its remainder in
 * EDX; esi is the caller's ESI (entry shim). */
uint64_t es2_weapon_keys_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t W,uint32_t code,
    uint32_t esi,const uint32_t entry[3]) {
    es2_weapon_registers r={code,(uint16_t)code,entry[2]};uint32_t a[2];
    const uint32_t si=lo(esi,1),c=r.edx;
    if(c>0x4au) {
        r.edx=c-0x4eu;
        if(!r.edx)return ts_selected(m,g,W,(uint16_t)code==0x4eu,si,r);
        r.edx=c-0x202u-0xau;
        if(c-0x202u<0xau) {
            r.eax=lo(r.eax,code+0xfdfeu);a[0]=W;a[1]=r.eax;CALL(0x4110ac,2);
            return pair(si,r.edx);
        }
        r.edx-=5u;
        if(!r.edx) {
            a[0]=W;a[1]=0xffffffffu;CALL(0x41074c,2);r.eax=W+0x14u;W8(W+0x18u,0);
            return pair(si,r.edx);
        }
        return pair(0,r.edx);
    }
    if(c==0x4au)return ts_selected(m,g,W,(uint16_t)code==0x4eu,si,r);
    r.edx=c-0xcu-2u;
    if(c-0xcu<2u)return ts_selected(m,g,W,(uint16_t)code==0xdu,si,r);
    r.edx-=3u;
    if(!r.edx) {
        a[0]=W;a[1]=1;CALL(0x41074c,2);r.eax=W+0x14u;W8(W+0x18u,0);
        return pair(si,r.edx);
    }
    r.edx-=0x1bu;
    if(!r.edx) { a[0]=W;CALL(0x410d04,1);r.ecx=W;return pair(si,r.edx); }
    r.edx-=4u;
    if(!r.edx) {
        /* Fire mode: 0, 1, 2 and back to 0. */
        W8(W+0x1cu,R8(W+0x1cu)+1u);
        r.eax=(r.eax&0xffffff00u)|R8(W+0x1cu);r.ecx=sx8(r.eax);
        if(fx_s32(r.ecx)>2)W8(W+0x1cu,0);
        r.eax=sx8(R8(W+0x1cu));a[0]=W;a[1]=r.eax;CALL(0x410ae4,2);
        r.eax=W+0x14u;W8(W+0x16u,1);
        return pair(si,r.edx);
    }
    return pair(0,r.edx);
}
