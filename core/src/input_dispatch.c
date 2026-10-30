#include "es2/core/input_dispatch.h"
#include "fixed.h"
#include <stddef.h>

/* Torso centering 41e8d4, torso aim 41ef14 and the player input dispatcher
 * 460870. Contract: input.md. The register struct r follows EAX/EDX/ECX through
 * every path so that each game call sees the original's entry registers; lo()
 * is a 16-bit load or operation that keeps the register's high word. Frame
 * locals live in guest scratch below the synthetic EBP f. */
static uint64_t pair(uint32_t a,uint32_t d) { return (uint64_t)d<<32|a; }
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define G(va) (g->image+(uint32_t)(va)-0x400000u)
static int32_t s16(uint32_t x) { return fx_s16(x); }
static int64_t s32(uint32_t x) { return fx_s32(x); }
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static void id_call(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,
                    es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}
#define CALL(va,n) id_call(g,g->image+(va)-0x400000u,0,a,n,&r)
#define VCALL(target,slot,n) id_call(g,(target),(slot),a,n,&r)

/* The clamp to [-100h, 100h] of 41e8d4 and 41ef14 (signed dwords). */
static uint32_t id_clamp(uint32_t x) {
    if(s32(x)>=0x100)return 0x100u;
    if(s32(x)>-0x100)return x;
    return 0xffffff00u;
}
/* |word| as 41ef14 computes it into `into`: 8000h gives 7FFFh in the low
 * word of `into`, otherwise the source register is copied whole and its
 * low word negated when negative. */
static uint32_t id_magnitude(uint32_t source,uint32_t into) {
    if((uint16_t)source==0x8000u)return lo(into,0x7fff);
    if(s16(source)<0)return lo(source,0u-source);
    return source;
}

/* Original: DBSIM.EXE 0x41e8d4 */
uint64_t es2_torso_center_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t P,uint32_t d,
    const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[5];
    uint32_t edi=sx(R16(P+0x298u));
    a[0]=0xfa;a[1]=edi;CALL(0x47e0b0,2);
    r.eax=id_clamp(r.eax);
    r.edx=sx(R16(P+0x29au));
    r.eax=lo(r.eax,0u-r.eax);
    edi=r.eax;r.ecx=r.edx;          /* EBP-4 holds the pitch word, popped into ECX at the end */
    a[0]=0xfa;a[1]=r.ecx;CALL(0x47e0b0,2);
    r.eax=id_clamp(r.eax);
    r.eax=lo(r.eax,0u-r.eax);
    const uint32_t esi=r.eax;
    a[0]=P;a[1]=edi;a[2]=0;a[3]=1;CALL(0x41a550,4);
    r.edx=d;a[0]=P;a[1]=esi;a[2]=d;a[3]=0;a[4]=1;CALL(0x41a808,5);
    return pair(r.eax,r.edx);
}

/* Copies the 20h-byte matrix block (nine words, two bytes, three dwords) as
 * the original does; returns with EDX = +14h, ECX = +18h, EAX = +1Ch (the
 * first copy in 41ef14 loads ECX and EDX the other way round). */
static void id_matrix_copy(const es2_guest_memory *m,uint32_t to,uint32_t from,es2_weapon_registers *r) {
    for(uint32_t k=0;k<0x14u;++k)W8(to+k,R8(from+k));
    r->edx=R32(from+0x14u);W32(to+0x14u,r->edx);
    r->ecx=R32(from+0x18u);W32(to+0x18u,r->ecx);
    r->eax=R32(from+0x1cu);W32(to+0x1cu,r->eax);
}

/* Original: DBSIM.EXE 0x41ef14. The frame lives in guest scratch: f is the
 * synthetic EBP; the two matrices at f-74h and f-94h, the transformed
 * points at f-48h and f-3Ch, the zero start point at f-54h. ebx and edi
 * are the original's EBX and EDI at every call (ESI = P throughout); TA
 * reports them with the return address of the next CALL. */
#define TA(at) do { if(frame) { frame->ebx=ebx;frame->esi=P;frame->edi=edi;frame->ret=G(at);frame->pushed=0; } } while(0)
uint64_t es2_torso_aim_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t P,uint32_t point,
    const uint32_t entry[3]) {
    return es2_torso_aim_frame_registers(m,g,NULL,NULL,P,point,entry);
}
uint64_t es2_torso_aim_frame_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_input_call *frame,
    uint32_t *ecx,uint32_t P,uint32_t point,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[5];
    const uint32_t f=g->scratch+ES2_TORSO_AIM_SCRATCH;
    const uint32_t angles=G(0x4a9cfc),angle_z=G(0x4a9d00);
    if(!R8(G(0x49a340)))W8(G(0x49a340),R8(G(0x49a340))+1u);
    uint32_t edi=f-0x94u,ebx=f-0x74u;
    W32(f-0x54u,0);W32(f-0x50u,0);W32(f-0x4cu,0);
    const uint32_t B=P+0xcu;
    W32(f-4u,B);
    r.eax=B;r.edx=B;r.ecx=0;
    if(!R16(B+0x26u)) {
        /* The pose matrix of B is stale: rebuild it from the angles. */
        W32(f-8u,B);r.ecx=B;r.eax=B+6u;r.edx=B;a[0]=B;a[1]=B+6u;TA(0x41ef70);CALL(0x47ebb8,2);
        r.ecx=R32(f-4u);W16(r.ecx+0x26u,1);
    }
    id_matrix_copy(m,ebx,R32(f-4u)+6u,&r);
    { const uint32_t t=r.edx;r.edx=r.ecx;r.ecx=t; }
    r.ecx=R32(P+0x1f2u);r.edx=R32(P+0x34u);
    r.eax=lo(r.eax,R16(r.ecx+0xcu));W32(f-0xcu,r.edx);
    const uint32_t node=r.eax;
    r.ecx=R32(f-0xcu);r.eax=R32(r.ecx+4u);r.edx=R32(r.eax);
    a[0]=r.eax;a[1]=node;TA(0x41f005);VCALL(R32(r.edx+0x20u),0x20,2);
    if(r.eax&&s16(R16(r.eax+4u))>-1) {
        r.eax=sx(R16(r.eax+4u))<<5;r.ecx=R32(f-0xcu);r.edx=R32(r.ecx+0x16u)+r.eax;
    } else r.edx=G(0x6c56a8);
    r.eax=r.edx;
    id_matrix_copy(m,edi,r.eax,&r);
    /* Invert both matrices: transpose, then -R^T t. */
    for(unsigned k=0;k<2;++k) {
        const uint32_t M=k?edi:ebx,t=k?f-0xacu:f-0xa0u;
        a[0]=M;TA(k?0x41f0db:0x41f097);CALL(0x47df19,1);r.ecx=a[0];
        W32(M+0x1cu,0u-R32(M+0x1cu));W32(M+0x14u,0u-R32(M+0x14u));
        r.eax=0u-R32(M+0x18u);W32(M+0x18u,r.eax);
        r.edx=R32(M+0x14u);W32(t,r.edx);r.edx=M+0x14u;
        r.ecx=R32(M+0x18u);W32(t+4u,r.ecx);r.ecx=t;
        r.eax=R32(M+0x1cu);W32(t+8u,r.eax);
        a[0]=t;a[1]=M;a[2]=M+0x14u;TA(k?0x41f116:0x41f0d2);CALL(0x480304,3);
    }
    r.eax=point;r.edx=f-0x48u;a[0]=point;a[1]=ebx;a[2]=f-0x48u;TA(0x41f127);CALL(0x48043c,3);
    r.ecx=f-0x3cu;r.eax=f-0x48u;a[0]=f-0x48u;a[1]=edi;a[2]=f-0x3cu;TA(0x41f138);CALL(0x48043c,3);
    r.edx=R32(P+0x1f2u);r.ecx=sx(R16(r.edx+0x66u));W32(f-0x34u,R32(f-0x34u)-r.ecx);
    r.eax=f-0x54u;r.edx=f-0x3cu;a[0]=angles;a[1]=f-0x3cu;a[2]=f-0x54u;TA(0x41f15a);CALL(0x492990,3);
    ebx=sx(R16(angle_z));a[0]=0xfa;a[1]=ebx;TA(0x41f16f);CALL(0x47e0b0,2);ebx=r.eax;
    edi=sx(R16(angles));a[0]=0xfa;a[1]=edi;TA(0x41f186);CALL(0x47e0b0,2);edi=r.eax;
    r.eax=lo(r.eax,R16(angles));r.edx=id_magnitude(r.eax,r.edx);W16(f-0xeu,r.edx);
    r.eax=lo(r.eax,R16(angle_z));r.ecx=id_magnitude(r.eax,r.ecx);
    r.eax=r.ecx;
    /* Far points (y > 50000) with a small yaw or pitch error: dead zone,
     * otherwise a stronger gain. */
    if(s32(R32(f-0x38u))>50000&&s16(r.eax)<1000) {
        if(s16(r.eax)<50)ebx=0;
        else { r.eax=sx(ebx);W32(f-0x14u,r.eax);r.edx=r.eax;a[0]=0x2bc;a[1]=r.edx;TA(0x41f1fe);CALL(0x47e0b0,2);ebx=r.eax; }
    }
    if(s32(R32(f-0x38u))>50000&&s16(R16(f-0xeu))<1000) {
        if(s16(R16(f-0xeu))<0x55)edi=0;
        else { r.eax=sx(edi);W32(f-0x18u,r.eax);r.edx=r.eax;a[0]=0x2bc;a[1]=r.edx;TA(0x41f233);CALL(0x47e0b0,2);edi=r.eax; }
    }
    r.eax=sx(ebx);r.edx=id_clamp(r.eax);r.edx=lo(r.edx,0u-r.edx);ebx=r.edx;
    r.eax=sx(edi);r.ecx=id_clamp(r.eax);edi=r.ecx;
    /* Squared rates with the sign kept: multiply_q8(|v|, v). */
    r.eax=id_magnitude(ebx,r.eax);
    r.edx=sx(r.eax);r.ecx=sx(ebx);W32(f-0x1cu,r.edx);W32(f-0x20u,r.ecx);
    r.eax=R32(f-0x20u);r.edx=R32(f-0x1cu);a[0]=r.edx;a[1]=r.eax;TA(0x41f2b5);CALL(0x47e0a0,2);ebx=r.eax;
    r.ecx=id_magnitude(edi,r.ecx);
    r.eax=sx(r.ecx);r.edx=sx(edi);W32(f-0x24u,r.eax);W32(f-0x28u,r.edx);
    r.eax=R32(f-0x28u);r.edx=R32(f-0x24u);a[0]=r.edx;a[1]=r.eax;TA(0x41f2ee);CALL(0x47e0a0,2);edi=r.eax;
    r.ecx=lo(r.ecx,R16(P+0x298u)-R16(angle_z));
    a[0]=P;a[1]=ebx;a[2]=r.ecx;a[3]=1;TA(0x41f30b);CALL(0x41a550,4);
    r.eax=lo(r.eax,R16(P+0x29au)+R16(angles));
    const uint32_t target=r.eax;
    ebx=R32(f-0x34u);r.edx=R32(f-0x38u);W32(f-0x2cu,r.edx);r.ecx=R32(f-0x3cu);W32(f-0x30u,r.ecx);
    r.eax=R32(f-0x2cu);r.edx=R32(f-0x30u);
    a[0]=r.edx;a[1]=r.eax;a[2]=ebx;TA(0x41f33c);
    if(frame) { frame->pushed=2;frame->temp[0]=1;frame->temp[1]=target; }   /* 41f30e, 41f31e */
    CALL(0x47de72,3);
    a[0]=P;a[1]=edi;a[2]=r.eax;a[3]=target;a[4]=1;TA(0x41f347);CALL(0x41a808,5);
    if(ecx)*ecx=r.ecx;
    return pair(angles,r.edx);
}
#undef TA

/* The view switch of button actions 12 and 15: 42a3f4(camera, mode) and
 * the view state word 4D251Ah. */
static void id_view(const es2_guest_memory *m,const es2_weapon_graph *g,es2_input_saved *s,uint32_t mode,
    uint32_t state,int camera_in_eax,es2_weapon_registers *rp) {
    es2_weapon_registers r=*rp;uint32_t a[2];
    if(camera_in_eax==1)r.eax=R32(G(0x4d24c0));
    else if(camera_in_eax==2)r.ecx=R32(G(0x4d24c0));
    else r.edx=R32(G(0x4d24c0));
    a[0]=camera_in_eax==1?r.eax:camera_in_eax==2?r.ecx:r.edx;a[1]=mode;
    (void)s;CALL(0x42a3f4,2);
    W16(G(0x4d251a),state);
    *rp=r;
}

/* What the original holds in EBX/ESI/EDI at the next call, for the plain
 * bridge (*s) or the stack bridge (*c, with the CALL's return address). */
static void id_save(es2_input_saved *s,es2_input_call *c,uint32_t ebx,uint32_t esi,uint32_t edi,uint32_t ret) {
    if(s) { s->ebx=ebx;s->esi=esi;s->edi=edi; }
    if(c) { c->ebx=ebx;c->esi=esi;c->edi=edi;c->ret=ret;c->pushed=0; }
}

/* Original: DBSIM.EXE 0x460870. SAVE(at) names the address after the CALL
 * (the original's return address). */
static uint64_t id_player_input(const es2_guest_memory *m,const es2_weapon_graph *g,es2_input_saved *s,
    es2_input_call *fr,uint32_t caller_edi,uint32_t R,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[5];
    const uint32_t f=g->scratch+ES2_PLAYER_INPUT_SCRATCH;
    uint32_t ebx=R32(G(0x4d24e6)),esi=R,edi=caller_edi;
#define SAVE(at) id_save(s,fr,ebx,esi,edi,G(at))
    W16(f-4u,1);
    /* 1. The record's command code through the command handler. */
    r.eax=lo(r.eax,R16(esi));
    SAVE(0x460894);a[0]=r.eax;CALL(0x45feb8,1);
    r.ecx=a[0];r.edx=(uint16_t)r.eax==2u;
    r.ecx=R32(G(0x49b088));W16(f-2u,r.edx);
    SAVE(0x4608b0);a[0]=r.ecx;a[1]=esi;CALL(0x432b14,2);
    /* 2. Axes by view: cockpit or outside camera. */
    W16(f-6u,0);W16(f-8u,0);W16(f-0xau,0);W16(f-0xcu,0);W16(f-0xeu,0);edi=0;W16(f-0x10u,0);
    if(R16(G(0x4d24f0))) {
        r.eax=R32(esi+0x22u);r.edx=lo(r.edx,R16(r.eax));W16(f-0xau,r.edx);
        r.ecx=R32(esi+0x26u);r.eax=lo(r.eax,R16(r.ecx));W16(f-0xcu,r.eax);
        r.edx=R8(esi+0xdu);W16(f-0xeu,r.edx);
        SAVE(0x460907);a[0]=3;CALL(0x45c524,1);r.ecx=a[0];
        if(r.eax) {
            SAVE(0x460911);CALL(0x477904,0);W32(f-0x14u,r.eax);r.ecx=R32(f-0x14u);
            if(R8(r.ecx+4u)) { r.eax=lo(r.eax,R16(esi+0x14u));W16(f-0x10u,r.eax); }
        }
    } else {
        edi=lo(edi,R16(esi+0x12u));r.eax=lo(r.eax,R16(esi+0x14u));W16(f-0x10u,r.eax);
        r.edx=lo(r.edx,R16(esi+0xeu));W16(f-6u,r.edx);r.ecx=lo(r.ecx,R16(esi+0x10u));W16(f-8u,r.ecx);
    }
    /* 3. Mouse drag (function statics with init guards). */
    if(!R8(G(0x49f6e8))) {
        r.eax=0;r.edx=0;W32(G(0x49f6e0),0);W32(G(0x49f6e4),0);W8(G(0x49f6e8),R8(G(0x49f6e8))+1u);
    }
    if(!R8(G(0x49f6e9)))W8(G(0x49f6e9),R8(G(0x49f6e9))+1u);
    if(!R8(esi+0xbu)) {
        W8(G(0x49f6ea),0);r.eax=0;W32(G(0x49f6e4),0);W32(G(0x49f6e0),0);
    } else if(R8(G(0x49f6ea))) {
        r.ecx=R32(G(0x4d2a76));r.eax=R32(G(0x4d2a7a));W32(f-0x1cu,r.ecx);W32(f-0x18u,r.eax);
        r.edx=esi+2u;W32(f-0x20u,r.edx);r.ecx=R32(f-0x20u);r.eax=R32(r.ecx);W32(f-0x1cu,R32(f-0x1cu)-r.eax);
        r.edx=R32(f-0x20u);r.ecx=R32(r.edx+4u);W32(f-0x18u,R32(f-0x18u)-r.ecx);
        r.eax=R32(f-0x1cu);
        { const int64_t p=s32(r.eax)*s32(R32(f-0x1cu));r.eax=(uint32_t)p;r.edx=(uint32_t)((uint64_t)p>>32); }
        r.eax=fx_sar(r.eax,6);W32(G(0x49f6e0),r.eax);
        if(s32(R32(f-0x1cu))>0) { r.edx=0u-R32(G(0x49f6e0));W32(G(0x49f6e0),r.edx); }
        r.ecx=R32(f-0x18u);r.ecx*=R32(f-0x18u);r.ecx=fx_sar(r.ecx,5);W32(G(0x49f6e4),r.ecx);
        if(s32(R32(f-0x18u))<0) { r.eax=0u-R32(G(0x49f6e4));W32(G(0x49f6e4),r.eax); }
    } else {
        /* Start a drag when the cursor lies in the cockpit rectangle. */
        r.edx=R32(G(0x49b088));r.eax=R32(r.edx+0x1f5u)+4u;
        r.edx=R32(r.eax);W32(f-0x68u,r.edx);r.ecx=R32(r.eax+4u);r.eax+=8u;W32(f-0x64u,r.ecx);
        r.edx=R32(r.eax);W32(f-0x60u,r.edx);r.ecx=R32(r.eax+4u);W32(f-0x5cu,r.ecx);
        r.eax=R32(esi+2u);
        if(s32(r.eax)>=s32(R32(f-0x68u))) {
            r.edx=R32(esi+2u);
            if(s32(r.edx)<=s32(R32(f-0x60u))) {
                r.ecx=R32(esi+6u);
                if(s32(r.ecx)>=s32(R32(f-0x64u))) {
                    r.eax=R32(esi+6u);
                    if(s32(r.eax)<=s32(R32(f-0x5cu))) {
                        r.edx=esi+2u;W8(G(0x49f6ea),1);W32(f-0x24u,r.edx);r.ecx=R32(f-0x24u);r.edx=R32(f-0x24u);
                        r.eax=R32(r.ecx);W32(G(0x4d2a76),r.eax);r.ecx=R32(r.edx+4u);W32(G(0x4d2a7a),r.ecx);
                    }
                }
            }
        }
    }
    if(R32(G(0x49f6e0))||R32(G(0x49f6e4))) {
        edi=lo(edi,edi+R16(G(0x49f6e0)));r.edx=lo(r.edx,R16(G(0x49f6e4)));W16(f-0x10u,R16(f-0x10u)+r.edx);
    }
    /* 4. Outside camera control (zeros in the cockpit view). */
    r.ecx=lo(r.ecx,R16(f-0xeu));a[3]=r.ecx;r.eax=lo(r.eax,R16(f-0xcu));a[2]=r.eax;
    r.edx=lo(r.edx,R16(f-0xau));a[1]=r.edx;r.ecx=R32(G(0x4d24ea));a[0]=r.ecx;
    SAVE(0x460ab7);CALL(0x401c74,4);
    /* 5. Movement input while byte P+39h is set. */
    r.eax=ebx+0x38u;
    if(R8(r.eax+1u)) {
        r.edx=R32(ebx+0x1f2u);
        if(R16(r.edx+0x50u)) {
            if(!R16(G(0x4d24f2))) {
                r.ecx=sx(edi);a[4]=r.ecx;r.eax=sx(R16(f-0x10u));a[3]=r.eax;r.edx=sx(R16(f-8u));a[2]=r.edx;
                r.ecx=sx(R16(f-6u));a[1]=r.ecx;a[0]=ebx;SAVE(0x460afb);CALL(0x41bb9c,5);
            }
        } else {
            r.eax=R32(ebx+0x1a4u);W32(f-0x28u,r.eax);
            if(R32(f-0x28u)) {
                r.edx=R32(f-0x28u);W32(f-0x30u,r.edx);r.ecx=R32(f-0x30u)+0x26u;r.eax=ebx+0x26u;
                a[0]=r.eax;a[1]=r.ecx;SAVE(0x460b28);CALL(0x49288c,2);
            } else r.eax=0;
            W32(f-0x2cu,r.eax);
            if(R32(G(0x4d2a70))) {
                /* Align mode: turn the legs and the torso to the stored heading. */
                edi=lo(edi,R16(ebx+0x10u));r.eax=edi;r.eax=lo(r.eax,r.eax-R16(G(0x4d2a74)));W16(f-0x32u,r.eax);
                a[0]=ebx;r.edx=R32(ebx);SAVE(0x460b56);VCALL(R32(r.edx+0x3cu),0x3c,1);r.ecx=a[0];
                r.ecx=sx(R16(f-0x32u));edi=lo(edi,edi-r.eax);W32(f-0x38u,r.ecx);edi=lo(edi,edi-R16(G(0x4d2a74)));
                r.eax=R32(f-0x38u);a[0]=0x64;a[1]=r.eax;SAVE(0x460b73);CALL(0x47e0b0,2);
                r.edx=sx(edi);W16(f-6u,r.eax);W32(f-0x3cu,r.edx);r.ecx=R32(f-0x3cu);
                a[0]=0x46;a[1]=r.ecx;SAVE(0x460b8b);CALL(0x47e0b0,2);edi=r.eax;
                r.eax=sx(R16(f-6u));r.edx=sx(R16(f-6u));
                { const int64_t p=s32(r.eax)*s32(r.edx);r.eax=(uint32_t)p;r.edx=(uint32_t)((uint64_t)p>>32); }
                r.eax=fx_sar(r.eax,8);W32(f-0x40u,r.eax);
                r.ecx=sx(edi);r.eax=sx(edi);r.ecx*=r.eax;r.ecx=fx_sar(r.ecx,8);W32(f-0x44u,r.ecx);
                if(s32(R32(f-0x40u))<30&&s32(R32(f-0x44u))<10) { r.edx=0;W32(G(0x4d2a70),0); }
                if(s16(R16(f-6u))<0)W32(f-0x40u,0u-R32(f-0x40u));
                if(s16(edi)<0)W32(f-0x44u,0u-R32(f-0x44u));
                a[0]=ebx;r.ecx=R32(ebx);SAVE(0x460bdb);VCALL(R32(r.ecx+0x38u),0x38,1);r.ecx=a[0];
                if(s16(r.eax)<0)W32(f-0x40u,0u-R32(f-0x40u));
                r.eax=lo(r.eax,R16(f-8u));a[2]=r.eax;r.edx=lo(r.edx,R16(f-0x40u));a[1]=r.edx;a[0]=ebx;SAVE(0x460bf4);CALL(0x4160dc,3);
                r.ecx=lo(r.ecx,R16(f-0x44u));a[0]=ebx;a[1]=r.ecx;a[2]=0xffffffffu;a[3]=0;SAVE(0x460c06);CALL(0x41a550,4);
                r.eax=R32(f-0x2cu);r.edx=lo(r.edx,R16(f-0x10u));
                a[0]=ebx;a[1]=r.edx;a[2]=r.eax;a[3]=0xffffffffu;a[4]=0;SAVE(0x460c1c);CALL(0x41a808,5);
            } else {
                r.ecx=lo(r.ecx,R16(f-8u));a[2]=r.ecx;r.eax=lo(r.eax,R16(f-6u));a[1]=r.eax;a[0]=ebx;SAVE(0x460c34);CALL(0x4160dc,3);
                r.eax=0;
                if((uint16_t)edi||R16(f-0x10u))W16(G(0x4d2504),0);
                else {
                    /* Tracking: no torso input, a live target and W+14h set. */
                    r.edx=R32(ebx+0x202u)+0x14u;
                    if(R8(r.edx)&&R32(f-0x28u)) {
                        r.ecx=R32(f-0x28u)+0x92u;W32(f-0x48u,r.ecx);r.edx=R32(f-0x48u);
                        if(!R8(r.edx+7u)) {
                            r.eax=f-0x74u;a[0]=ebx;a[1]=r.eax;a[2]=0;a[3]=0;SAVE(0x460c87);CALL(0x41b728,4);
                            r.ecx=f-0x74u;a[0]=ebx;a[1]=r.ecx;SAVE(0x460c94);CALL(0x41ef14,2);
                            W16(G(0x4d2504),0);r.eax=lo(r.eax,1);
                        }
                    }
                }
                if(R16(G(0x4d2504))) { r.edx=R32(f-0x2cu);a[0]=ebx;a[1]=r.edx;SAVE(0x460cb8);CALL(0x41e8d4,2); }
                else if(!(uint16_t)r.eax) {
                    a[0]=ebx;a[1]=edi;a[2]=0xffffffffu;a[3]=0;SAVE(0x460ccd);CALL(0x41a550,4);
                    r.ecx=R32(f-0x2cu);r.eax=lo(r.eax,R16(f-0x10u));
                    a[0]=ebx;a[1]=r.eax;a[2]=r.ecx;a[3]=0xffffffffu;a[4]=0;SAVE(0x460ce3);CALL(0x41a808,5);
                }
            }
        }
    }
    /* 6. Fire in the cockpit view. */
    if(!R16(G(0x4d24f0))) { a[0]=ebx;a[1]=esi;SAVE(0x460cf7);CALL(0x415608,2); }
    /* 7. Button loop: each button value acts once per call. */
    r.edx=f-0x8cu;a[0]=r.edx;a[1]=0;a[2]=0x15;SAVE(0x460d0a);CALL(0x49681a,3);
    edi=esi+0x16u;esi=0;
    do {
        if(!R8(edi))continue;
        r.eax=(r.eax&0xffffff00u)|R8(edi);
        if((uint8_t)r.eax>=0x15u)continue;
        r.edx=(uint8_t)r.eax;
        if(R8(f-0x8cu+r.edx))continue;
        r.ecx=(uint8_t)r.eax;W8(f-0x8cu+r.ecx,1);
        r.eax=R32(G(0x49b088));ebx=esi;W32(f-0x50u,r.eax);r.eax=ebx+1u;
        a[0]=r.eax;a[1]=1;SAVE(0x460d58);CALL(0x45b734,2);
        ebx=(ebx&0xffffff00u)|(uint8_t)(ebx+R8(G(0x4d2577)));
        ebx=(ebx&0xffffff00u)|(uint8_t)(ebx+4u);
        r.edx=(uint8_t)ebx;r.ecx=sx(r.edx);r.eax=R8(G(0x4d1f38)+r.ecx);
        if(r.eax>0x14u)continue;
        switch(r.eax) {
        case 2: r.edx=R32(f-0x50u);a[0]=r.edx;SAVE(0x460de5);CALL(0x43349c,1);r.ecx=a[0];break;
        case 3:
            W32(G(0x4d2a70),1);W16(G(0x4d2504),0);r.ecx=R32(G(0x4d24e6));a[0]=r.ecx;r.eax=R32(r.ecx);
            SAVE(0x460e0a);VCALL(R32(r.eax+0x3cu),0x3c,1);r.ecx=a[0];
            r.edx=R32(G(0x4d24e6));r.ecx=lo(r.ecx,R16(r.edx+0x10u)-r.eax);W16(G(0x4d2a74),r.ecx);break;
        case 4: W16(G(0x4d2504),1);r.eax=0;W32(G(0x4d2a70),0);break;
        case 5:
            SAVE(0x460e3e);CALL(0x477904,0);W32(f-0x4cu,r.eax);r.edx=R32(f-0x4cu);
            if(R8(r.edx+4u)&&R8(G(0x4d1f46))==1u)W16(G(0x49a06e),s16(R16(G(0x49a06e)))>0?0xffffu:1u);
            break;
        case 6: a[0]=0x14;SAVE(0x460e88);CALL(0x45feb8,1);r.ecx=a[0];break;
        case 7:
            r.eax=R32(G(0x4d24e6));W32(f-0x54u,r.eax);r.edx=R32(f-0x54u);W16(r.edx+0x290u,0);
            r.ecx=R32(f-0x54u)+0x92u;W32(f-0x58u,r.ecx);r.eax=R32(f-0x58u);W8(r.eax+1u,1);break;
        case 8: r.ecx=R32(f-0x50u);a[0]=r.ecx;SAVE(0x460e97);CALL(0x4333c8,1);r.ecx=a[0];break;
        case 9: r.eax=R32(G(0x4d24e6));a[0]=r.eax;a[1]=0x1a;r.edx=R32(r.eax);SAVE(0x460eaa);VCALL(R32(r.edx+0x2cu),0x2c,2);break;
        case 10: r.ecx=R32(G(0x4d24e6));a[0]=r.ecx;a[1]=0x1b;r.eax=R32(r.ecx);SAVE(0x460ec0);VCALL(R32(r.eax+0x2cu),0x2c,2);break;
        case 11:
            SAVE(0x460ecd);CALL(0x429820,0);
            { const uint32_t at=r.eax?0x460eefu:0x460edcu;
              if(!r.eax) { r.edx=R32(f-0x50u);a[0]=r.edx;a[1]=0x41; }
              else { r.ecx=R32(f-0x50u);a[0]=r.ecx;a[1]=1; }
              SAVE(at);CALL(0x432bc8,2); }
            break;
        case 12:
            W16(G(0x4d2534),0);r.eax=sx(R16(G(0x4d24ee)));
            if(!--r.eax) { SAVE(0x460f26);id_view(m,g,s,3,2,0,&r);break; }
            if(!--r.eax) { SAVE(0x460f45);id_view(m,g,s,2,3,2,&r);break; }
            --r.eax;
            { const uint32_t before=r.eax;r.eax-=2u;if(before<2u) { SAVE(0x460f26);id_view(m,g,s,3,2,0,&r); } }
            break;
        case 13:
            r.eax=R32(G(0x49b088));r.edx=R32(r.eax+0x1e1u);a[0]=r.edx;a[1]=0x26;r.ecx=R32(r.edx);
            SAVE(0x460f69);VCALL(R32(r.ecx+0xcu),0xc,2);break;
        case 14: r.eax=R32(G(0x49b088));r.edx=R32(r.eax+0x1edu);a[0]=r.edx;SAVE(0x460f82);CALL(0x446e3c,1);r.ecx=a[0];break;
        case 15:
            if(s32(R32(G(0x4d257b)))<50)break;
            W16(G(0x4d2534),0);
            { /* Modes 1 and 4: view 3, state 2; mode 2: view 2, state 4;
               * mode 3: state 4 only. The DEC chain leaves ECX = 0 on a
               * match, else mode - 4. */
              const uint32_t mode=sx(R16(G(0x4d24ee)));
              if(mode==1u||mode==4u) { r.ecx=0;SAVE(0x460fc3);id_view(m,g,s,3,2,1,&r); }
              else if(mode==2u) { r.ecx=0;SAVE(0x460fe2);id_view(m,g,s,2,4,0,&r); }
              else if(mode==3u) { r.ecx=0;W16(G(0x4d251a),4); }
              else r.ecx=mode-4u; }
            break;
        case 16: r.ecx=R32(G(0x4d24e6));r.eax=R32(r.ecx+0x202u);a[0]=r.eax;a[1]=0x202;SAVE(0x46100a);CALL(0x410f78,2);break;
        case 17: r.edx=R32(f-0x50u);a[0]=r.edx;a[1]=1;SAVE(0x46101d);CALL(0x432bc8,2);break;
        case 18:
            r.ecx=R32(G(0x49b088));r.eax=R32(r.ecx+0x1e1u);a[0]=r.eax;a[1]=0x29;r.edx=R32(r.eax);
            SAVE(0x461036);VCALL(R32(r.edx+0xcu),0xc,2);break;
        case 19: r.ecx=R32(G(0x4d24e6));r.eax=R32(r.ecx+0x202u);a[0]=r.eax;a[1]=0x11;SAVE(0x46104f);CALL(0x410f78,2);break;
        case 20: r.edx=R32(G(0x4d24e6));r.ecx=R32(r.edx+0x202u);a[0]=r.ecx;a[1]=0x211;SAVE(0x46106b);CALL(0x410f78,2);break;
        default: break;   /* 0 and 1: no action here */
        }
    } while(++edi,s32(++esi)<8);
#undef SAVE
    r.eax=lo(r.eax,R16(f-2u));
    return pair(r.eax,r.edx);
}
uint64_t es2_player_input_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_input_saved *saved,
    uint32_t R,const uint32_t entry[3]) {
    return id_player_input(m,g,saved,NULL,0,R,entry);
}
uint64_t es2_player_input_frame_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_input_call *frame,
    uint32_t caller_edi,uint32_t R,const uint32_t entry[3]) {
    return id_player_input(m,g,NULL,frame,caller_edi,R,entry);
}
