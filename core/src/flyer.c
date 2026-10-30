#include "es2/core/flyer.h"
#include "fixed.h"

/* Flyer flight model 466b58 with its private leaves 466a88, 466ae0, 466b20,
 * 467b28 and the pure helpers 47e08d, 47e0c0 and 47f058.
 * (docs/reverse-engineering/flyer.md). The register struct r follows
 * EAX/EDX/ECX through every path so that each game call sees the original's
 * entry registers; lo() is a 16-bit load or operation that keeps the register's
 * high word. Guest memory is read and written in the original's order; frame
 * locals whose address reaches a game call live in the scratch frame at the
 * original EBP offsets. */
static uint64_t pair(uint32_t a,uint32_t d) { return (uint64_t)d<<32|a; }
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
static int32_t i32(uint32_t x) { return (int32_t)fx_s32(x); }
static int32_t s16(uint32_t x) { return fx_s16(x); }
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static void fm_call(const es2_weapon_graph *g,uint32_t target,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,0);
    g->call(g->context,target,a,n,r);
}
#define CALL(va,n) fm_call(g,g->image+(va)-0x400000u,a,n,r)
#define GLOBAL(va) (g->image+(va)-0x400000u)
/* One cdecl argument word whose slot the caller pops into ECX afterwards. */
static void fm_call1(const es2_weapon_graph *g,uint32_t va,uint32_t slot,es2_weapon_registers *r) {
    uint32_t a[1]={slot};CALL(va,1);r->ecx=slot;
}

/* 47e08d(x, y): EDX:EAX = x * y, EAX = bits 16..47; ECX unchanged. */
static void fm_q16(es2_weapon_registers *r,uint32_t x,uint32_t y) {
    const uint64_t p=(uint64_t)(fx_s32(x)*fx_s32(y));r->eax=(uint32_t)(p>>16);r->edx=(uint32_t)(p>>32);
}
/* 47e0c0(x, w): EDX:EAX = x * s16(w), EAX = bits 14..45; ECX = s16(w). */
static void fm_q14(es2_weapon_registers *r,uint32_t x,uint32_t w) {
    const uint64_t p=(uint64_t)(fx_s32(x)*fx_s16(w));r->eax=(uint32_t)(p>>14);r->edx=(uint32_t)(p>>32);r->ecx=sx(w);
}
/* Original: DBSIM.EXE 0x467b28. Moves the dword *p toward t by step and
 * returns the gap left (0 when t is reached or already equal). */
static void fm_approach(const es2_guest_memory *m,es2_weapon_registers *r,uint32_t p,uint32_t t,uint32_t step) {
    r->ecx=step;r->edx=t;r->eax=p;
    if(i32(t)<i32(R32(p))) {
        W32(p,R32(p)-step);r->ecx=R32(p);
        if(i32(t)<i32(r->ecx)) { r->eax=R32(p)-t;return; }
        W32(p,t);r->eax=0;return;
    }
    if(i32(t)<=i32(R32(p))) { r->eax=0;return; }
    W32(p,R32(p)+step);r->ecx=R32(p);
    if(i32(t)>i32(r->ecx)) { r->edx=t-R32(p);r->eax=r->edx;return; }
    W32(p,t);r->eax=0;
}
/* Original: DBSIM.EXE 0x466ae0. movement_step of the low word of each
 * dword of v, stored back sign-extended. */
static void fm_step_dwords(const es2_guest_memory *m,const es2_weapon_graph *g,es2_weapon_registers *r,uint32_t v) {
    r->eax=lo(r->eax,R16(v));fm_call1(g,0x467924u,r->eax,r);
    r->edx=sx(r->eax);W32(v,r->edx);
    r->ecx=lo(r->ecx,R16(v+4u));fm_call1(g,0x467924u,r->ecx,r);
    r->eax=sx(r->eax);W32(v+4u,r->eax);
    r->edx=lo(r->edx,R16(v+8u));fm_call1(g,0x467924u,r->edx,r);
    r->ecx=sx(r->eax);W32(v+8u,r->ecx);
    r->eax=v;
}
/* Original: DBSIM.EXE 0x466b20. movement_step of each word of v. */
static void fm_step_words(const es2_guest_memory *m,const es2_weapon_graph *g,es2_weapon_registers *r,uint32_t v) {
    r->eax=lo(r->eax,R16(v));fm_call1(g,0x467924u,r->eax,r);W16(v,r->eax);
    r->edx=lo(r->edx,R16(v+2u));fm_call1(g,0x467924u,r->edx,r);W16(v+2u,r->eax);
    r->ecx=lo(r->ecx,R16(v+4u));fm_call1(g,0x467924u,r->ecx,r);W16(v+4u,r->eax);
    r->eax=v;
}
/* Original: DBSIM.EXE 0x466a88. Per-word mean of a and b into the static
 * 4d3b58 (the guard byte 4a087c is set once); returns the static. */
static void fm_mean(const es2_guest_memory *m,const es2_weapon_graph *g,es2_weapon_registers *r,uint32_t a,uint32_t b) {
    const uint32_t st=GLOBAL(0x4d3b58u),guard=GLOBAL(0x4a087cu);
    r->edx=b;r->eax=a;
    if(!R8(guard))W8(guard,R8(guard)+1u);
    r->ecx=fx_sar(sx(R16(a))+sx(R16(b)),1);W16(st,r->ecx);
    r->ecx=fx_sar(sx(R16(a+2u))+sx(R16(b+2u)),1);W16(st+2u,r->ecx);
    r->eax=sx(R16(a+4u));r->edx=sx(R16(b+4u));r->eax=fx_sar(r->eax+r->edx,1);W16(st+4u,r->eax);
    r->eax=st;
}

/* Column-major 3 x 3 Q14 layout with the Z entries after the XY block. */
static const unsigned fm_offset[3][3]={{0,2,8},{4,6,10},{12,14,16}};
static void fm_dot(const es2_guest_memory *m,uint32_t a,uint32_t b,uint32_t out,unsigned col,unsigned row,unsigned terms) {
    uint32_t sum=0;
    for(unsigned k=0;k<terms;++k)sum+=sx(R16(a+fm_offset[col][k]))*sx(R16(b+fm_offset[k][row]));
    W16(out+fm_offset[col][row],fx_sar(sum+0x2000u,14));
}
/* Original: DBSIM.EXE 0x47f058. */
void es2_compose_rotation(const es2_guest_memory *m,uint32_t a,uint32_t b,uint32_t out) {
    const int8_t ma=(int8_t)R8(a+18u),mb=(int8_t)R8(b+18u);
    if(ma<1||mb<1) {
        const uint32_t from=ma<1?b:a;
        for(uint32_t i=0;i<20u;i+=4u)W32(out+i,R32(from+i));
        return;
    }
    if(mb==1) {
        if(ma==1) {
            for(unsigned c=0;c<2;++c)for(unsigned w=0;w<2;++w)fm_dot(m,a,b,out,c,w,2);
            W16(out+8u,0);W16(out+10u,0);W16(out+12u,0);W16(out+14u,0);W16(out+16u,0x4000);W8(out+18u,1);
            return;
        }
        for(unsigned c=0;c<3;++c)for(unsigned w=0;w<2;++w)fm_dot(m,a,b,out,c,w,2);
        W16(out+8u,R16(a+8u));W16(out+10u,R16(a+10u));W16(out+16u,R16(a+16u));W8(out+18u,2);
        return;
    }
    if(ma==1) {
        for(unsigned c=0;c<2;++c)for(unsigned w=0;w<3;++w)fm_dot(m,a,b,out,c,w,2);
        W16(out+12u,R16(b+12u));W16(out+14u,R16(b+14u));W16(out+16u,R16(b+16u));W8(out+18u,2);
        return;
    }
    for(unsigned c=0;c<3;++c)for(unsigned w=0;w<3;++w)fm_dot(m,a,b,out,c,w,3);
    W8(out+18u,2);
}

/* A word clamped to [-limit, limit] as the original's compare chains do:
 * value >= limit gives limit, value > -limit the value, else -limit (all
 * signed 16-bit; -limit is the 16-bit negation). */
static int fm_at_or_above(uint32_t value,uint32_t limit) { return s16(value)>=s16(limit); }
static int fm_above(uint32_t value,uint32_t limit) { return s16(value)>s16(limit); }

/* Original: DBSIM.EXE 0x466b58. */
uint64_t es2_flight_model_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t C,uint32_t S,
    uint32_t M,uint32_t F,const uint32_t entry[3]) {
    es2_weapon_registers regs={entry[0],entry[1],entry[2]},*r=&regs;uint32_t a[3];
    const uint32_t f=g->scratch+0x1c0u; /* EBP */
    /* Bytes 19 of the local matrices N (EBP-1A0h) and D (EBP-1B4h): never
     * written by this function or its callees, so they keep the entry
     * stack residue. */
    W8(f-0x18du,g->padding);W8(f-0x1a1u,g->composed_padding);

    /* 1. Throttle: lever position (device byte +4 and controls byte 1Ah =
     * 2) or the frame-scaled throttle rate. */
    CALL(0x477904u,0);
    if(R8(r->eax+4u)&&R8(GLOBAL(0x4d1f52u))==2) {
        r->eax=C;r->edx=lo(r->edx,R16(C+0xcu));r->edx<<=3;
        const uint16_t v=(uint16_t)r->edx;
        if(fm_at_or_above(v,0x400))r->ecx=lo(r->ecx,0x400);
        else if(fm_above(v,0xfc00))r->ecx=lo(r->ecx,v);
        else r->ecx=lo(r->ecx,0xfc00);
        W16(S+0x1eu,r->ecx);
    } else {
        r->eax=C;r->edx=R32(C+0xcu);r->eax=r->edx;
        a[0]=100;a[1]=r->edx;CALL(0x47e0a0u,2);
        uint16_t d=(uint16_t)r->eax;
        if(d) {
            r->edx=lo(r->edx,d);fm_call1(g,0x467924u,r->edx,r);
            d=(uint16_t)r->eax;
            r->ecx=lo(r->ecx,R16(S+0x1eu));r->ecx=lo(r->ecx,r->ecx+d);
            const uint16_t v=(uint16_t)r->ecx;
            if(fm_at_or_above(v,0x400))r->eax=lo(r->eax,0x400);
            else if(fm_above(v,0xfc00))r->eax=lo(r->eax,v);
            else r->eax=lo(r->eax,0xfc00);
            W16(S+0x1eu,r->eax);
        }
    }

    /* 2. Forward speed toward the throttle's target, less the pitch term. */
    const uint32_t O=R32(S+0x20u); /* EDI until the roll command */
    r->eax=R32(S+0x20u)+0x1au;const uint32_t position=r->eax;
    r->edx=R32(M+0x2au)-R32(M+0x2eu);
    r->ecx=fx_sar(sx(R16(S+0x1eu))+0x400u,1);r->eax=r->ecx;
    a[0]=r->edx;a[1]=r->ecx;CALL(0x47e0b0u,2);
    r->eax+=R32(M+0x2eu);uint32_t target=r->eax;
    if(s16(R16(O))<0) { r->ecx=sx(R16(O));r->eax=r->ecx;a[0]=0xfa;a[1]=r->ecx;CALL(0x47e0b0u,2); }
    else { r->edx=sx(R16(O));r->ecx=r->edx;a[0]=0x3e;a[1]=r->ecx;CALL(0x47e0b0u,2); }
    target-=r->eax;
    r->edx=lo(r->edx,R16(M+0xau));fm_call1(g,0x467924u,r->edx,r);
    r->ecx=sx(r->eax);r->eax=target;r->edx=S+4u;
    fm_approach(m,r,S+4u,target,r->ecx);

    /* 3. Lateral drag in the pose frame, back through the inverse. */
    const uint32_t O2=R32(S+0x20u);r->ecx=O2;r->eax=O2;
    if(!R16(O2+0x26u)) {
        r->edx=O2;r->ecx=O2+6u;r->eax=O2;a[0]=O2;a[1]=O2+6u;CALL(0x47ebb8u,2);
        r->edx=O2;W16(O2+0x26u,1);
    }
    r->eax=S;const uint32_t R=O2+6u;r->ecx=R;
    r->edx=R32(S);W32(f-0x174u,r->edx);r->edx=0;
    r->ecx=R32(S+4u);W32(f-0x170u,r->ecx);
    r->eax=R32(S+8u);W32(f-0x16cu,r->eax);
    W32(f-0x170u,r->edx);
    r->ecx=R;r->eax=f-0x180u;r->edx=R;r->ecx=f-0x174u;
    a[0]=f-0x174u;a[1]=R;a[2]=f-0x180u;CALL(0x480304u,3);
    r->eax=R32(M+0x32u);r->edx=R32(f-0x180u);r->ecx=r->edx;
    a[0]=r->eax;a[1]=r->ecx;CALL(0x47e0b0u,2);W32(f-0x180u,r->eax);
    r->edx=R32(M+0x32u);r->ecx=R32(f-0x17cu);r->eax=r->ecx;
    a[0]=r->edx;a[1]=r->eax;CALL(0x47e0b0u,2);W32(f-0x17cu,r->eax);
    const uint32_t inverse=S+0x24u; /* EBP-4Ch */
    r->ecx=inverse;r->eax=inverse;r->edx=f-0x174u;r->eax=f-0x180u;
    a[0]=f-0x180u;a[1]=inverse;a[2]=f-0x174u;CALL(0x480304u,3);
    r->edx=f-0x174u;fm_step_dwords(m,g,r,f-0x174u);r->ecx=f-0x174u;
    const uint32_t B=r->eax;
    r->ecx=R32(B+8u);W32(S+8u,R32(S+8u)-r->ecx);
    r->ecx=S+0xcu;
    r->edx=R32(B);W32(S,R32(S)-r->edx);
    r->eax=R32(B+4u);W32(S+4u,R32(S+4u)-r->eax);
    r->eax=R;r->edx=S+0xcu;r->ecx=R;r->eax=S;
    a[0]=S;a[1]=R;a[2]=S+0xcu;CALL(0x480304u,3);

    /* 4. Pitch command and its damping; yaw command. */
    W16(f-0x64u,0);W16(f-0x62u,0);W16(f-0x60u,0);
    r->eax=S+0x18u;
    r->edx=lo(r->edx,R16(S+0x18u));W16(f-0x74u,r->edx);
    r->ecx=lo(r->ecx,R16(S+0x1au));W16(f-0x72u,r->ecx);
    r->eax=lo(r->eax,R16(S+0x1cu));W16(f-0x70u,r->eax);
    r->edx=C;r->ecx=R32(C+4u);uint32_t pitch=r->ecx;
    r->eax=R32(M+0xeu);r->edx=R32(S+4u)-R32(M+0x2eu);
    fm_q16(r,r->edx,r->eax);
    r->eax+=R32(M+0x26u);const uint32_t ceiling=r->eax;
    r->ecx=F;
    int level=R32(F+0xcu)!=0;
    if(!level) { r->eax=F;level=R32(F+8u)!=0; }
    if(level) {
        r->edx=lo(r->edx,R16(O+2u));r->ecx=lo(r->ecx,(uint16_t)r->edx);
        fm_call1(g,0x47dff0u,r->ecx,r);
        r->eax=fx_sar(0u-sx(r->eax),6);pitch=r->eax;
    }
    uint32_t Pc;
    if(pitch) {
        r->edx=sx(R16(M));r->ecx=pitch;r->eax=pitch;
        a[0]=r->edx;a[1]=pitch;CALL(0x47e0a0u,2);Pc=r->eax;
    } else {
        if(s16(R16(O))>0) { r->ecx=R32(M+0x16u);r->eax=fx_sar(0u-sx(R16(O)),r->ecx);Pc=r->eax; }
        else { r->ecx=R32(M+0x16u);r->edx=fx_sar(0u-sx(R16(O)),r->ecx);Pc=r->edx; }
        r->eax=R32(M+0x12u);r->edx=sx(R16(S+0x18u));r->ecx=r->edx;
        a[0]=r->eax;a[1]=r->ecx;CALL(0x47e0b0u,2);
        r->eax=lo(r->eax,0u-r->eax);W16(f-0x64u,r->eax);
    }
    r->edx=sx(R16(M+4u));r->ecx=C;r->eax=0u-R32(C+8u);r->edx=r->eax;r->ecx=sx(R16(M+4u));
    a[0]=r->ecx;a[1]=r->edx;CALL(0x47e0a0u,2);
    uint32_t Yc=r->eax;

    /* 5. Ceiling: above it, pitch down and yaw by the bank. */
    r->eax=position;r->ecx=C;r->edx=R32(position+8u)-R32(C+0x10u);
    const uint32_t altitude=r->edx;r->eax=altitude;
    if(i32(altitude)>i32(ceiling)) {
        r->edx=altitude-ceiling;r->ecx=r->edx;a[0]=10;a[1]=r->ecx;CALL(0x47e0b0u,2);
        r->eax=0u-r->eax;const uint16_t e=(uint16_t)r->eax;
        r->edx=lo(r->edx,R16(O+2u));r->ecx=lo(r->ecx,(uint16_t)r->edx);r->ecx=lo(r->ecx,r->ecx+0xc000u);
        fm_call1(g,0x47dff0u,r->ecx,r);
        const uint16_t c1=(uint16_t)r->eax;
        r->eax=lo(r->eax,e);r->edx=sx(c1);fm_q14(r,r->edx,r->eax);
        r->ecx=sx(r->eax);Yc=r->ecx;
        r->eax=lo(r->eax,e);r->edx=lo(r->edx,R16(O+2u));r->ecx=lo(r->ecx,(uint16_t)r->edx);
        fm_call1(g,0x47dff0u,r->ecx,r);
        const uint16_t c2=(uint16_t)r->eax;
        r->eax=lo(r->eax,e);r->edx=sx(c2);fm_q14(r,r->edx,r->eax);
        r->ecx=sx(r->eax);const uint32_t limit=r->ecx;r->eax=limit;
        if(i32(limit)<i32(Pc)) { r->edx=limit;Pc=limit; }
    } else if(!pitch) {
        r->ecx=R32(M+0x12u);r->eax=sx(R16(S+0x18u));r->edx=r->eax;
        a[0]=r->ecx;a[1]=r->edx;CALL(0x47e0b0u,2);
        r->eax=lo(r->eax,0u-r->eax);W16(f-0x64u,r->eax);
    }

    /* 6. Roll command, failure overrides, bank-to-turn rate. */
    r->eax=C;r->edx=R32(C);uint32_t roll_in=r->edx;
    r->ecx=i32(roll_in)<0;
    r->eax=s16(R16(O+2u))<0;
    r->edx=(uint8_t)r->eax==(uint8_t)r->ecx; /* EBP-0C4h, never read */
    r->eax=lo(r->eax,R16(O+2u));
    const uint16_t bank=(uint16_t)r->eax;
    if(bank==0x8000u)r->ecx=lo(r->ecx,0x7fff);
    else if(s16(bank)>=0)r->ecx=r->eax;
    else { r->ecx=r->eax;r->ecx=lo(r->ecx,0u-r->ecx); }
    r->eax=sx(r->ecx);const uint32_t magnitude=r->eax;
    r->edx=F;
    if(R32(F+0xcu))roll_in=0x100;
    else {
        r->eax=F;
        if(R32(F+8u))roll_in=0xffffff00u;
        else {
            r->edx=F;
            if(R32(F+4u)) {
                if(s16(R16(O+2u))<0x1000) {
                    r->ecx=lo(r->ecx,0x1000u-R16(O+2u));r->eax=lo(r->eax,(uint16_t)r->ecx);
                    fm_q14(r,0x14,r->eax);r->edx=sx(r->eax);roll_in+=r->edx;
                }
            } else {
                r->ecx=F;
                if(R32(F)&&s16(R16(O+2u))>-0x1000) {
                    r->eax=lo(r->eax,R16(O+2u));r->eax=lo(r->eax,r->eax+0x1000u);r->edx=lo(r->edx,(uint16_t)r->eax);
                    fm_q14(r,0x14,r->edx);r->ecx=sx(r->eax);roll_in-=r->ecx;
                }
            }
        }
    }
    uint32_t T;
    if(i32(magnitude)<0x4000) { r->ecx=R32(M+0x1eu);r->eax=fx_sar(0u-sx(R16(O+2u)),r->ecx);T=r->eax; }
    else {
        r->eax=lo(r->eax,R16(O+2u));r->ecx=R32(M+0x1eu);r->eax=lo(r->eax,r->eax+0x8000u);
        r->edx=fx_sar(sx(r->eax),r->ecx);T=r->edx;
    }
    uint32_t Rc; /* EDI */
    if(roll_in) {
        r->eax=sx(R16(M+2u));r->edx=roll_in;r->ecx=roll_in;a[0]=r->eax;a[1]=roll_in;CALL(0x47e0a0u,2);Rc=r->eax;
        if((i32(roll_in)>0&&s16(R16(S+0x1au))<0)||(i32(roll_in)<0&&s16(R16(S+0x1au))>0)) {
            r->eax=R32(M+0x12u);r->edx=sx(R16(S+0x1au));r->ecx=r->edx;a[0]=r->eax;a[1]=r->ecx;CALL(0x47e0b0u,2);
            r->eax=lo(r->eax,0u-r->eax);W16(f-0x62u,r->eax);
        }
    } else {
        r->edx=0u-sx(R16(O+2u));r->ecx=R32(M+0x1au);r->eax=R32(M+0x12u);r->edx=fx_sar(r->edx,r->ecx);Rc=r->edx;
        r->edx=sx(R16(S+0x1au));r->ecx=r->edx;a[0]=r->eax;a[1]=r->ecx;CALL(0x47e0b0u,2);
        r->eax=lo(r->eax,0u-r->eax);W16(f-0x62u,r->eax);
        r->eax=lo(r->eax,R16(S+0x1au));r->eax=lo(r->eax,r->eax+R16(f-0x62u));
        const uint16_t sum=(uint16_t)r->eax;
        if(sum==0x8000u)r->edx=lo(r->edx,0x7fff);
        else if(s16(sum)>=0)r->edx=r->eax;
        else { r->edx=r->eax;r->edx=lo(r->edx,0u-r->edx); }
        r->eax=sx(r->edx);
        if(i32(r->eax)>i32(magnitude)) {
            r->eax=lo(r->eax,R16(S+0x1au));Rc=0;r->eax=lo(r->eax,0u-r->eax);r->eax=lo(r->eax,r->eax-magnitude);
            W16(f-0x62u,r->eax);
        }
    }

    /* 7. Rate increments: commands clamped by the acceleration limits,
     * stepped, added, then the rates clamped. */
    r->edx=lo(r->edx,R16(M+6u));const uint16_t l6=(uint16_t)r->edx;
    r->ecx=lo(r->ecx,R16(M+6u));r->ecx=lo(r->ecx,0u-r->ecx);const uint16_t n6=(uint16_t)r->ecx;
    r->eax=lo(r->eax,(uint16_t)Pc);
    if(fm_at_or_above(r->eax,l6))r->edx=lo(r->edx,l6);
    else { r->ecx=lo(r->ecx,(uint16_t)Pc);r->edx=lo(r->edx,fm_above(r->ecx,n6)?(uint16_t)Pc:n6); }
    W16(f-0x64u,R16(f-0x64u)+r->edx);
    r->eax=lo(r->eax,R16(M+8u));const uint16_t l8=(uint16_t)r->eax;
    r->ecx=lo(r->ecx,R16(M+8u));r->ecx=lo(r->ecx,0u-r->ecx);const uint16_t n8=(uint16_t)r->ecx;
    if(fm_at_or_above(Rc,l8))r->eax=lo(r->eax,l8);
    else if(fm_above(Rc,n8))r->eax=Rc;
    else r->eax=lo(r->eax,n8);
    W16(f-0x62u,R16(f-0x62u)+r->eax);
    r->edx=lo(r->edx,R16(M+8u));const uint16_t l8y=(uint16_t)r->edx;
    r->ecx=lo(r->ecx,R16(M+8u));r->ecx=lo(r->ecx,0u-r->ecx);const uint16_t n8y=(uint16_t)r->ecx;
    r->eax=lo(r->eax,(uint16_t)Yc);
    if(fm_at_or_above(r->eax,l8y))r->edx=lo(r->edx,l8y);
    else { r->ecx=lo(r->ecx,(uint16_t)Yc);r->edx=lo(r->edx,fm_above(r->ecx,n8y)?(uint16_t)Yc:n8y); }
    W16(f-0x60u,R16(f-0x60u)+r->edx);
    r->eax=R32(M+0x12u);r->ecx=sx(R16(S+0x1cu));r->eax=r->ecx;r->edx=R32(M+0x12u);
    a[0]=r->edx;a[1]=r->eax;CALL(0x47e0b0u,2);
    W16(f-0x60u,R16(f-0x60u)-r->eax);
    r->ecx=f-0x64u;fm_step_words(m,g,r,f-0x64u);r->ecx=f-0x64u;
    r->edx=r->eax;r->eax=S+0x18u;
    r->ecx=lo(r->ecx,R16(r->edx+4u));W16(S+0x1cu,R16(S+0x1cu)+r->ecx);
    r->ecx=lo(r->ecx,R16(r->edx));W16(S+0x18u,R16(S+0x18u)+r->ecx);
    r->edx=lo(r->edx,R16(r->edx+2u));W16(S+0x1au,R16(S+0x1au)+r->edx);
    r->edx=lo(r->edx,R16(M));const uint16_t l0=(uint16_t)r->edx;
    r->ecx=lo(r->ecx,R16(M));r->ecx=lo(r->ecx,0u-r->ecx);const uint16_t n0=(uint16_t)r->ecx;
    r->eax=lo(r->eax,R16(S+0x18u));
    if(fm_at_or_above(r->eax,l0))r->edx=lo(r->edx,l0);
    else if(fm_above(r->eax,n0))r->edx=r->eax;
    else r->edx=lo(r->edx,n0);
    W16(S+0x18u,r->edx);
    r->ecx=lo(r->ecx,R16(M+2u));const uint16_t l2=(uint16_t)r->ecx;
    r->edx=lo(r->edx,R16(M+2u));r->edx=lo(r->edx,0u-r->edx);const uint16_t n2=(uint16_t)r->edx;
    r->eax=lo(r->eax,R16(S+0x1au));
    if(fm_at_or_above(r->eax,l2))r->ecx=lo(r->ecx,l2);
    else if(fm_above(r->eax,n2))r->ecx=r->eax;
    else r->ecx=lo(r->ecx,n2);
    W16(S+0x1au,r->ecx);
    r->edx=lo(r->edx,R16(M+4u));const uint16_t l4=(uint16_t)r->edx;
    r->edx=lo(r->edx,R16(M+4u));r->edx=lo(r->edx,0u-r->edx);const uint16_t n4=(uint16_t)r->edx;
    r->eax=lo(r->eax,R16(S+0x1cu));
    if(fm_at_or_above(r->eax,l4))r->ecx=lo(r->ecx,l4);
    else if(fm_above(r->eax,n4))r->ecx=r->eax;
    else r->ecx=lo(r->ecx,n4);
    W16(S+0x1cu,r->ecx);

    /* 8. Orientation: mean rates stepped into angle increments, composed
     * onto the pose, heading turned by the bank, inverse rebuilt. */
    r->eax=f-0x74u;r->edx=S+0x18u;fm_mean(m,g,r,S+0x18u,f-0x74u);
    r->edx=lo(r->edx,R16(r->eax));W16(f-0x10cu,r->edx);
    r->ecx=lo(r->ecx,R16(r->eax+2u));W16(f-0x10au,r->ecx);
    r->eax=lo(r->eax,R16(r->eax+4u));W16(f-0x108u,r->eax);
    r->eax=f-0x10cu;fm_step_words(m,g,r,f-0x10cu);
    r->edx=R32(r->eax);r->ecx=f-0x10cu;W32(f-0x10cu,r->edx);
    r->ecx=f-0x1a0u;r->edx=lo(r->edx,R16(r->eax+4u));r->eax=f-0x1b4u;W16(f-0x108u,r->edx);
    r->edx=f-0x10cu;a[0]=f-0x10cu;a[1]=f-0x1b4u;CALL(0x47ebb8u,2);
    r->ecx=f-0x1a0u;
    const uint32_t O3=R32(S+0x20u);
    if(!R16(O3+0x26u)) {
        r->eax=O3+6u;r->edx=O3;a[0]=O3;a[1]=O3+6u;CALL(0x47ebb8u,2);
        W16(O3+0x26u,1);
    }
    r->eax=f-0x1a0u;r->edx=O3+6u;r->ecx=f-0x1b4u;
    es2_compose_rotation(m,f-0x1b4u,O3+6u,f-0x1a0u);
    r->eax=f-0x1a0u; /* 47f058's EDX and ECX are overwritten before use */
    r->eax=R32(S+0x20u)+0x1au;
    r->edx=R32(r->eax+8u);W32(f-0x184u,r->edx);
    r->edx=f-0x1a0u;
    r->ecx=R32(r->eax);W32(f-0x18cu,r->ecx);
    r->eax=R32(r->eax+4u);W32(f-0x188u,r->eax);
    r->eax=f-0x124u;
    const uint32_t O4=R32(S+0x20u);
    a[0]=f-0x1a0u;a[1]=f-0x124u;CALL(0x47f9a0u,2);
    r->ecx=R32(f-0x124u);W32(O4,r->ecx);
    r->eax=O4+6u;r->ecx=lo(r->ecx,R16(f-0x120u));W16(O4+4u,r->ecx);
    W16(O4+0x26u,0);
    for(uint32_t i=0;i<20u;i+=4u)W32(O4+6u+i,R32(f-0x1a0u+i));
    r->ecx=0;r->edx=r->eax+0x14u;
    r->ecx=R32(f-0x184u);W32(r->edx+8u,r->ecx);
    r->ecx=R32(f-0x18cu);W32(r->edx,r->ecx);
    r->ecx=R32(f-0x188u);W32(r->edx+4u,r->ecx);
    W16(O4+0x26u,1);
    r->eax=lo(r->eax,(uint16_t)T);fm_call1(g,0x467924u,r->eax,r);
    r->edx=R32(S+0x20u);r->eax=lo(r->eax,r->eax+R16(r->edx+4u));
    r->edx=R32(S+0x20u);r->ecx=r->edx;W16(r->ecx+4u,r->eax);W16(r->edx+0x26u,0);
    r->ecx=T;W32(S+0x44u,T);
    const uint32_t O5=R32(S+0x20u);
    if(!R16(O5+0x26u)) {
        r->eax=O5+6u;r->edx=O5;a[0]=O5;a[1]=O5+6u;CALL(0x47ebb8u,2);
        W16(O5+0x26u,1);
    }
    const uint32_t R5=O5+6u;
    r->eax=inverse;r->ecx=inverse;r->edx=R5;
    for(uint32_t i=0;i<20u;i+=4u)W32(inverse+i,R32(R5+i));
    r->ecx=0;
    r->eax=inverse+0x14u;r->edx=R5+0x14u;r->ecx=R32(r->edx+8u);W32(r->eax+8u,r->ecx);
    r->ecx=R32(r->edx);W32(r->eax,r->ecx);
    r->ecx=R32(r->edx+4u);W32(r->eax+4u,r->ecx);
    fm_call1(g,0x47df19u,inverse,r);
    r->eax=inverse+0x14u;r->edx=inverse+0x14u;
    W32(r->eax+8u,0u-R32(r->eax+8u));W32(r->eax,0u-R32(r->eax));W32(r->eax+4u,0u-R32(r->eax+4u));
    r->eax=R32(r->edx);W32(f-0x1c0u,r->eax);
    r->ecx=R32(r->edx+4u);W32(f-0x1bcu,r->ecx);
    r->ecx=inverse+0x14u;r->edx=R32(inverse+0x1cu);r->eax=f-0x1c0u;W32(f-0x1b8u,r->edx);
    a[0]=f-0x1c0u;a[1]=inverse;a[2]=inverse+0x14u;CALL(0x480304u,3);

    /* 9. Re-express the kept world velocity in the new body frame; part of
     * a forward speed lost by the turn comes back. */
    const uint16_t v0=R16(S+4u);r->edx=lo(r->edx,v0);
    r->ecx=inverse;r->eax=S+0xcu;r->edx=S;
    a[0]=S+0xcu;a[1]=inverse;a[2]=S;CALL(0x480304u,3);
    r->edx=sx(v0);
    if(i32(r->edx)>i32(R32(S+4u))) {
        r->ecx=sx(v0)-R32(S+4u);r->eax=r->ecx;a[0]=0x384;a[1]=r->eax;CALL(0x47e0b0u,2);
        W32(S+4u,R32(S+4u)+r->eax);
        r->edx=S+0xcu;
        const uint32_t O6=R32(S+0x20u);r->ecx=O6;r->eax=O6;
        if(!R16(O6+0x26u)) {
            r->edx=O6;r->ecx=O6+6u;r->eax=O6;a[0]=O6;a[1]=O6+6u;CALL(0x47ebb8u,2);
            r->edx=O6;W16(O6+0x26u,1);
        }
        r->ecx=O6+6u;r->eax=S+0xcu;r->edx=O6+6u;r->ecx=S;
        a[0]=S;a[1]=O6+6u;a[2]=S+0xcu;CALL(0x480304u,3);
    }
    return pair(r->eax,r->edx);
}

/* Flyer control (flyer.md, section "flyer control"): the player's flyer input
 * 41bb9c and the AI flyer updates 4221a8, 42215c and 4222fc. Every game
 * call (terrain_height, heading_cos, flight_model and the group's own
 * 42215c/4221a8) goes through the guest bridge with the original's
 * EAX/EDX/ECX; 47e08d/47e0c0 are computed (fm_q16/fm_q14). g->scratch is
 * the original's frame (EBP = scratch + frame size). The stack residue the
 * flight model reads is planted by the adapter's guest call. */
#define FC_CALL(va,n) fm_call(g,g->image+(va)-0x400000u,a,n,r)
/* Original: DBSIM.EXE 0x41bb9c. */
uint64_t es2_flyer_input_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t P,uint32_t turn,
    uint32_t throttle,uint32_t pitch,uint32_t twist,const uint32_t entry[3]) {
    es2_weapon_registers regs={entry[0],entry[1],entry[2]},*r=&regs;uint32_t a[5];
    const uint32_t f=g->scratch+0x24u,S=P+0x2b9u;
    r->eax=R32(P+0x20eu);const uint32_t parts=r->eax;
    r->edx=R16(parts+0xeu)==0;W32(f-0x24u,r->edx);
    r->ecx=R16(parts+0x10u)==0;W32(f-0x20u,r->ecx);
    r->edx=R16(parts+8u)==0;W32(f-0x1cu,r->edx);
    r->eax=R16(parts+0xau)==0;W32(f-0x18u,r->eax);
    r->ecx=turn;W32(f-0x14u,turn);r->ecx=P+0x26u;
    r->eax=throttle;W32(f-0x10u,throttle);
    r->eax=R32(GLOBAL(0x4a0be0u));r->edx=twist;W32(f-0xcu,twist);
    a[0]=r->eax;a[1]=P+0x26u;FC_CALL(0x46e180u,2);
    W32(f-4u,r->eax);W32(f-8u,pitch);
    r->edx=f-0x24u;r->ecx=R32(P+0x1f2u)+0x1dcu;r->eax=f-0x14u;
    a[0]=f-0x14u;a[1]=S;a[2]=r->ecx;a[3]=f-0x24u;a[4]=1;FC_CALL(0x466b58u,5);
    if(pitch) {
        r->eax=sx(R16(S+0x1eu));W16(P+0x290u,r->eax);
        r->eax=P+0x92u;W8(r->eax+1u,1);
    }
    return pair(r->eax,r->edx);
}
/* sgn(v) * (low32(v*v) SAR 8) clamped to [-100h, 100h] (4221a8). */
static uint32_t fc_shape(uint32_t v) {
    uint32_t q=fx_sar(v*v,8);
    if(i32(v)<0)q=0u-q;
    if(i32(q)>=0x100)return 0x100;
    if(i32(q)<=-0x100)return 0xffffff00u;
    return q;
}
/* Original: DBSIM.EXE 0x4221a8. */
uint64_t es2_flyer_command_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,uint32_t R,
    const uint32_t entry[3]) {
    es2_weapon_registers regs={entry[0],entry[1],entry[2]},*r=&regs;uint32_t a[5];
    const uint32_t f=g->scratch+0x14u;
    r->ecx=R;r->eax=A+0x243u;W32(f-4u,r->eax);
    const uint32_t x=R32(R),y=R32(R+4u);
    r->eax=fc_shape(x);r->edx=fc_shape(y);
    W32(R,r->eax);W32(R+4u,r->edx);
    r->eax=0;W32(f-0x14u,0);r->edx=0;W32(f-0x10u,0);W32(f-8u,0);W32(f-0xcu,0);
    r->eax=R32(A+0x1fau)+0x3au;r->edx=R32(f-4u);
    a[0]=R;a[1]=r->edx;a[2]=r->eax;a[3]=f-0x14u;a[4]=1;FC_CALL(0x466b58u,5);
    return pair(r->eax,r->edx);
}
/* Original: DBSIM.EXE 0x42215c. */
uint64_t es2_flyer_bank_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,uint32_t goal,
    uint32_t R,const uint32_t entry[3]) {
    es2_weapon_registers regs={entry[0],entry[1],entry[2]},*r=&regs;uint32_t a[2];
    r->edx=sx(R16(A+0xeu));r->eax=sx(goal)-r->edx;
    fm_q16(r,r->eax,0x9c4u);W32(R,r->eax);
    r->ecx=sx(R16(A+0x25du));fm_q16(r,r->ecx,0xffffec78u);W32(R,R32(R)+r->eax);
    a[0]=A;a[1]=R;FC_CALL(0x4221a8u,2);
    return pair(r->eax,r->edx);
}
/* Original: DBSIM.EXE 0x4222fc. */
uint64_t es2_flyer_steer_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,uint32_t yaw,
    uint32_t pitch,uint32_t T,const uint32_t entry[3]) {
    es2_weapon_registers regs={entry[0],entry[1],entry[2]},*r=&regs;uint32_t a[3];
    const uint32_t f=g->scratch+0x28u,R=f-0x28u;
    r->eax=0u-sx(yaw);fm_q16(r,r->eax,0x9c4u);uint32_t si=r->eax;
    r->eax=R32(A+0x1fau);r->edx=lo(r->edx,R16(r->eax+0xeu));W16(f-2u,r->edx);
    r->ecx=lo(r->ecx,R16(A+0xeu));W16(f-4u,r->ecx);
    r->eax=lo(r->eax,R16(A+0x287u));W16(f-6u,r->eax);
    if(T) {
        r->edx=T;r->ecx=lo(r->ecx,R16(A+0x10u));r->ecx=lo(r->ecx,r->ecx-R16(T+0x10u));W16(f-0xau,r->ecx);
        r->eax=T;r->edx=sx(R16(T+0x1f8u));W32(f-0x10u,r->edx);
        r->ecx=lo(r->ecx,R16(f-0xau));fm_call1(g,0x47dff0u,r->ecx,r);
        W16(f-0x12u,r->eax);r->eax=lo(r->eax,R16(f-0x12u));
        r->edx=sx(R16(f-0x10u));fm_q14(r,r->edx,r->eax);
        r->ecx=sx(r->eax);W16(f-8u,r->ecx);
        r->eax=sx(R16(f-8u));fm_q16(r,r->eax,0xc8u);
        W16(f-6u,R16(f-6u)-r->eax);
    }
    r->edx=sx(R16(f-6u));fm_q16(r,r->edx,0x7d00u);si=lo(si,si+r->eax);
    r->ecx=sx(R16(A+0x25du));fm_q16(r,r->ecx,0xffffec78u);si=lo(si,si+r->eax);
    r->eax=pitch;W32(f-0x24u,pitch);r->edx=0;W32(f-0x20u,0);r->ecx=0;W32(f-0x1cu,0);
    r->eax=A+0x26u;r->edx=R32(GLOBAL(0x4a0be0u));
    a[0]=r->edx;a[1]=A+0x26u;FC_CALL(0x46e180u,2);
    W32(f-0x18u,r->eax);
    r->ecx=sx(R16(f-4u));r->eax=sx(R16(f-2u))+0xfffffa24u;
    if(i32(r->ecx)>=i32(r->eax)&&s16(si)>0) {
        r->edx=R;r->ecx=lo(r->ecx,R16(f-2u));
        a[0]=A;a[1]=r->ecx;a[2]=R;FC_CALL(0x42215cu,3);
        return pair(r->eax,r->edx);
    }
    r->eax=sx(R16(f-4u));r->edx=sx(R16(f-2u))+0xfffffa24u;r->edx=0u-r->edx;
    if(i32(r->eax)<=i32(r->edx)&&s16(si)<0) {
        r->ecx=R;r->eax=lo(r->eax,R16(f-2u));r->eax=lo(r->eax,0u-r->eax);
        a[0]=A;a[1]=r->eax;a[2]=R;FC_CALL(0x42215cu,3);
        return pair(r->eax,r->edx);
    }
    if((uint16_t)yaw==0x8000u)r->edx=lo(r->edx,0x7fff);
    else if(s16(yaw)>=0)r->edx=yaw;
    else { r->edx=yaw;r->edx=lo(r->edx,0u-r->edx); }
    if(s16(r->edx)<0x7d0) {
        if(s16(R16(A+0xeu))<0x320) { r->eax=0u-sx(yaw);fm_q16(r,r->eax,0xfa0u);W32(f-0x20u,r->eax); }
        r->ecx=R;a[0]=A;a[1]=0;a[2]=R;FC_CALL(0x42215cu,3);
        return pair(r->eax,r->edx);
    }
    r->eax=sx(si);W32(R,r->eax);r->edx=0;W32(f-0x20u,0);r->ecx=R;
    a[0]=A;a[1]=R;FC_CALL(0x4221a8u,2);
    return pair(r->eax,r->edx);
}
#undef FC_CALL
