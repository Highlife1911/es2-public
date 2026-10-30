#include "es2/core/weapon_classes.h"
#include "fixed.h"
/* Weapon-class methods (docs/reverse-engineering/weapon_classes.md, section
 * "the weapon-class methods"). lo() is a 16-bit load or 16-bit arithmetic that
 * keeps the register's high word. Memory is read again wherever the original
 * reads it again. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define VA(va) (g->image+(uint32_t)(va)-0x400000u)

static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static uint32_t sx8(uint8_t b) { return (uint32_t)(int32_t)(int8_t)b; }
static void wc_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,VA(va),a,n,r);
}

/* Original: DBSIM.EXE 0x40ecd4, 0x40e678, 0x4111e2, 0x40df00 (xor eax,eax). */
uint64_t es2_weapon_zero_registers(const uint32_t entry[3],uint32_t *ecx) {
    *ecx=entry[2];return pair(0,entry[1]);
}
/* Original: DBSIM.EXE 0x40f8a4, 0x4111e9 (mov ax,1). */
uint64_t es2_weapon_one_registers(const uint32_t entry[3],uint32_t *ecx) {
    *ecx=entry[2];return pair(lo(entry[0],1),entry[1]);
}
/* Original: DBSIM.EXE 0x40f8ad */
uint64_t es2_weapon_trigger_registers(const es2_guest_memory *m,uint32_t W,uint32_t R,
    const uint32_t entry[3],uint32_t *ecx) {
    (void)W;*ecx=entry[2];return pair(R8(R+0xdu),entry[1]);
}
/* Original: DBSIM.EXE 0x40df08, 0x40df1c */
uint64_t es2_gauge_match_registers(const es2_guest_memory *m,uint32_t W,uint32_t x,
    const uint32_t entry[3],uint32_t *ecx) {
    *ecx=entry[2];return pair(x==R32(W+0x77u)?1u:0u,x);
}
/* Original: DBSIM.EXE 0x40ed34 */
uint64_t es2_beam_spent_registers(const es2_guest_memory *m,uint32_t W,const uint32_t entry[3],uint32_t *ecx) {
    (void)entry;const uint32_t v=sx8(R8(W+0x49u));*ecx=v;return pair(v,W+0x43u);
}
/* Original: DBSIM.EXE 0x40ed48 */
uint64_t es2_gun_spent_registers(const es2_guest_memory *m,uint32_t W,const uint32_t entry[3],uint32_t *ecx) {
    *ecx=entry[2];
    if(!R16(W+0x7bu))return pair(1,entry[1]);   /* 40ed53 */
    return pair(R8(W+0x49u)?1u:0u,W+0x43u);     /* 40ed5c */
}
/* Original: DBSIM.EXE 0x40f520 */
uint64_t es2_gun_stock_registers(const es2_guest_memory *m,uint32_t W,const uint32_t entry[3],uint32_t *ecx) {
    (void)entry;
    const uint32_t edx=(uint32_t)fx_s16(R16(W+0x7bu));
    const uint32_t c=fx_sar((uint32_t)fx_s16(R16(R32(W+0x1cu)+0x3au)),3);
    *ecx=c;return pair(fx_s32(edx)>=fx_s32(c)?1u:0u,edx);
}
/* Original: DBSIM.EXE 0x40f504 */
uint64_t es2_beam_priority_registers(const es2_guest_memory *m,uint32_t W,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t c=lo(entry[2],R8(W+0x43u)?0x2710u:R16(W+0x7bu));   /* 40f510 */
    *ecx=c;return pair(c,W+0x43u);
}
/* Original: DBSIM.EXE 0x4115f8 */
uint64_t es2_gun_clear_registers(const es2_guest_memory *m,uint32_t W,const uint32_t entry[3],uint32_t *ecx) {
    W16(W+0x7bu,0);*ecx=entry[2];return pair(W,entry[1]);
}
/* Original: DBSIM.EXE 0x40f4f0 */
uint64_t es2_beam_fill_registers(const es2_guest_memory *m,uint32_t W,const uint32_t entry[3],uint32_t *ecx) {
    W8(W+0x43u,1);W16(W+0x7bu,0x4b0u);*ecx=entry[2];return pair(W,W+0x43u);
}
/* Original: DBSIM.EXE 0x40f4d8 */
uint64_t es2_beam_preset_registers(const es2_guest_memory *m,uint32_t W,const uint32_t entry[3],uint32_t *ecx) {
    if(!R8(W+0x49u))W16(W+0x7bu,0x334u);        /* 40f4e5 */
    *ecx=entry[2];return pair(W,W+0x43u);
}
/* Original: DBSIM.EXE 0x4116ad */
uint64_t es2_beam_drain_registers(const es2_guest_memory *m,uint32_t W,const uint32_t entry[3],uint32_t *ecx) {
    W16(W+0x7bu,0);W8(W+0x43u,0);W8(W+0x34u,1);W8(W+0x3cu,1);
    *ecx=entry[2];return pair(W+0x3bu,W+0x43u);
}
/* Original: DBSIM.EXE 0x4116cf */
uint64_t es2_beam_clear_registers(const es2_guest_memory *m,uint32_t W,const uint32_t entry[3],uint32_t *ecx) {
    W16(W+0x7du,0);W16(W+0x7bu,0);W8(W+0x34u,1);W8(W+0x3cu,1);
    *ecx=entry[2];return pair(W+0x3bu,0);
}
/* Original: DBSIM.EXE 0x40e62c */
uint64_t es2_weapon_ammo_registers(const es2_guest_memory *m,uint32_t W,uint32_t p,
    const uint32_t entry[3],uint32_t *ecx) {
    (void)W;if(p)W16(p,0);                       /* 40e634 */
    *ecx=entry[2];return pair(lo(p,5),entry[1]);
}
/* Original: DBSIM.EXE 0x40e644 */
uint64_t es2_gun_ammo_registers(const es2_guest_memory *m,uint32_t W,uint32_t p,
    const uint32_t entry[3],uint32_t *ecx) {
    (void)entry;const uint32_t T=R32(W+0x20u);
    if(R16(T)) {                                 /* 40e654: 40e62c(W, p) */
        const uint32_t inner[3]={W,p,T};
        return es2_weapon_ammo_registers(m,W,p,inner,ecx);
    }
    uint32_t c=T;
    if(p) { c=lo(T,R16(W+0x7bu));W16(p,c); }     /* 40e658 */
    const uint32_t again=R32(W+0x20u);
    *ecx=c;return pair(lo(again,R16(again+2u)),p);
}
/* Original: DBSIM.EXE 0x40ed6c */
uint64_t es2_gun_ready_registers(const es2_guest_memory *m,uint32_t W,const uint32_t entry[3],uint32_t *ecx) {
    if(R8(W+0x49u)) { *ecx=entry[2];return pair(0,W+0x43u); }   /* 40ed79 */
    const uint32_t c=R16(W+0x31u)?0u:1u;*ecx=c;
    if(!c)return pair(0,W+0x43u);                               /* 40ed89 */
    return pair(R16(W+0x7bu)?1u:0u,W+0x43u);                    /* 40ed90 */
}
/* Original: DBSIM.EXE 0x40ecdc */
uint64_t es2_beam_ready_registers(const es2_guest_memory *m,uint32_t W,const uint32_t entry[3],
    uint32_t ebx_high,uint32_t *ecx) {
    uint32_t D=R32(W+0x1cu),edx;
    const uint32_t c=lo(entry[2],R16(D+0x36u));
    if(fx_s16(c)<fx_s16(R16(D+0x38u))) {                        /* 40ecee */
        /* BX = W+7Bh (the caller's EBX high word stays); 40ecf9: EBX = EDX
           unless DX < BX; then EDX = EBX. */
        const uint32_t ebx=lo(ebx_high<<16,R16(W+0x7bu));
        edx=fx_s16(c)<fx_s16(ebx)?ebx:c;
    } else {
        D=R32(W+0x1cu);edx=lo(D,R16(D+0x38u));
    }
    if(R8(W+0x49u)) { *ecx=W+0x43u;return pair(0,edx); }        /* 40ed0f */
    const uint32_t f=R16(W+0x31u)?0u:1u;*ecx=f;
    if(!f)return pair(0,edx);                                   /* 40ed1f */
    return pair(fx_s16(edx)<=fx_s16(R16(W+0x7du))?1u:0u,edx);   /* 40ed25 */
}
/* Original: DBSIM.EXE 0x40eda0 */
uint64_t es2_elf_ready_registers(const es2_guest_memory *m,uint32_t W,const uint32_t entry[3],uint32_t *ecx) {
    if(R8(W+0x49u)) { *ecx=entry[2];return pair(0,W+0x43u); }   /* 40edad */
    const uint32_t D=R32(W+0x1cu);
    uint32_t c=lo(entry[2],R16(W+0x7bu)),edx=lo(D,R16(D+0x36u));
    if(!(fx_s16(edx)<fx_s16(c)))c=edx;                          /* 40edbd */
    if(fx_s16(c)<=fx_s16(R16(W+0x7du))) { *ecx=c;return pair(lo(W,1),edx); }   /* 40edc5 */
    edx=W+0x33u;
    if(!R8(W+0x33u)) { *ecx=c;return pair(0,edx); }             /* 40edcd */
    c=R32(W+0x1cu);edx=lo(edx,R16(c+0x38u));*ecx=c;
    if(fx_s16(edx)>fx_s16(R16(W+0x7du)))return pair(0,edx);     /* 40edda */
    return pair(lo(W,1),edx);
}
/* Original: DBSIM.EXE 0x40e680 */
uint64_t es2_elf_trigger_registers(const es2_guest_memory *m,uint32_t W,uint32_t R,
    const uint32_t entry[3],uint32_t *ecx) {
    uint32_t c=entry[2],edx;
    if(R16(R32(W+0x1cu)+0x56u)==0x16u) { c=W+0x43u;W8(W+0x47u,1);W8(W+0x48u,1); }   /* 40e693 */
    if(R8(W+0x48u)) {                                           /* 40e6ad */
        edx=R8(R+0xdu);
        if(!(uint16_t)edx) {                                    /* 40e6b7 */
            if(R32(W+0x10u)) { c=R32(W+0x14u);W16(c,0); }        /* 40e6bd */
            W8(W+0x48u,0);
        }
    } else {
        c=sx8(R8(W+0x47u));edx=0;
        if(R8(R+0xdu)) {                                        /* 40e6dd */
            if(!(uint16_t)c) { c=W+0x43u;W8(W+0x47u,1);W16(W+0x84u,0); }   /* 40e6e2 */
        } else if((uint16_t)c&&R32(W+0x10u)) {                  /* 40e6f8, 40e6fe */
            c=R32(W+0x14u);W16(c,0);W8(W+0x47u,0);              /* 40e704 falls through */
        }
    }
    *ecx=c;return pair(edx,edx);
}
/* Original: DBSIM.EXE 0x40f48c */
uint64_t es2_beam_adjust_registers(const es2_guest_memory *m,uint32_t W,uint32_t d,
    const uint32_t entry[3],uint32_t *ecx) {
    uint32_t edx=lo(entry[1],0x50u);
    if(!(uint16_t)d)edx+=0xffffff60u;                           /* 40f49b */
    W16(W+0x7bu,R16(W+0x7bu)+edx);
    edx=lo(edx,R16(W+0x7bu));
    uint32_t c;
    if(fx_s16(edx)>=0x4b0)c=lo(entry[2],0x4b0u);                /* 40f4b0 */
    else if(fx_s16(edx)>0)c=edx;                                /* 40f4bb */
    else c=0;
    W16(W+0x7bu,c);W8(W+0x34u,1);W8(W+0x3cu,1);
    *ecx=c;return pair(W+0x3bu,edx);
}
/* Original: DBSIM.EXE 0x40e71c */
uint64_t es2_laew_trigger_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t W,
    uint32_t R,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={W,R,entry[2]};
    if(!R8(W+0x46u)) {                                          /* 40e733 */
        if(R8(R+0xdu)) { r.edx=W+0x43u;W8(W+0x46u,1);W32(W+0x81u,0x4e20u); }   /* 40e739 */
        *ecx=r.ecx;return pair(0,r.edx);
    }
    if(R8(R+0xdu)) {                                            /* 40e751 */
        W32(W+0x81u,R32(W+0x81u)+1000u);r.eax=R32(W+0x81u);
        if(fx_s32(r.eax)<=100000) { *ecx=r.ecx;return pair(0,r.edx); }   /* 40e768 */
    }
    if(R8(R+0xdu)) {                                            /* 40e772 */
        const uint32_t a[2]={1,1};wc_call(g,0x45b734u,a,2,&r);
    }
    *ecx=r.ecx;return pair(1,r.edx);
}
/* Original: DBSIM.EXE 0x40f00c */
uint64_t es2_beam_step_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t W,
    uint32_t a,uint32_t f,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={0,W+0x43u,entry[2]};
    if(R8(W+0x49u)) { *ecx=r.ecx;return pair(a,r.edx); }      /* 40f024 */
    { const uint32_t args[3]={W,a,f};wc_call(g,0x40ef94u,args,3,&r); }
    uint32_t edx=(uint16_t)f?0u:lo(r.edx,R16(W+0x7bu));        /* 40f034 */
    edx=lo(edx,edx-R16(W+0x7du));
    uint32_t eax=R32(W+0x1cu);
    if(R16(eax+0x56u)==0x19u)edx+=edx;                         /* 40f04a */
    if(fx_s16(edx)>0) {                                        /* 40f051 */
        eax=lo(eax,R16(W+0x7fu));
        if(fx_s16(a)<fx_s16(eax))eax=a;                        /* 40f05a */
        if(fx_s16(edx)<fx_s16(eax))eax=edx;                    /* 40f061 */
    } else {
        W8(W+0x43u,0);eax=lo(W+0x43u,0xfffbu);
        if(fx_s16(edx)>fx_s16(eax))eax=edx;                    /* 40f074 */
    }
    W16(W+0x7du,R16(W+0x7du)+eax);
    edx=R32(W+0x1cu);
    if(R16(edx+0x56u)==0x19u) {                                /* 40f084 */
        r.ecx=fx_sar((uint32_t)fx_s16(eax),1);W16(W+0x7du,R16(W+0x7du)-r.ecx);
    }
    *ecx=r.ecx;return pair(lo(a,a-eax),edx);
}
/* Original: DBSIM.EXE 0x40f0d0 */
uint64_t es2_turbo_step_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t W,
    uint32_t a,uint32_t f,const uint32_t entry[3],uint32_t *ecx) {
    uint32_t si=0;
    if(R32(W+0x81u)) {                                         /* 40f0e5 */
        W32(W+0x7du,R32(W+0x7du)-0x23u);
        if(!(fx_s32(R32(W+0x7du))>0)) { W32(W+0x81u,0);W32(W+0x7du,0); }   /* 40f0ef */
    }
    if(R8(W+0x49u)) { *ecx=entry[2];return pair(lo(a,a),entry[1]); }   /* 40f103 */
    es2_weapon_registers r={W+0x43u,lo(entry[1],f),entry[2]};
    { const uint32_t args[3]={W,a,lo(entry[1],f)};wc_call(g,0x40ef94u,args,3,&r); }
    const uint32_t eax=lo(r.eax,0x7d0u-R16(W+0x7du));
    if(fx_s16(eax)>0) {                                        /* 40f11f */
        si=0x14u;
        if(fx_s16(a)<fx_s16(si))si=a;                          /* 40f128 */
        if(fx_s16(eax)<fx_s16(si))si=eax;                      /* 40f12f */
        W32(W+0x7du,R32(W+0x7du)+(uint32_t)fx_s16(si));
    }
    *ecx=r.ecx;return pair(lo(a,a-si),r.edx);
}
/* Original: DBSIM.EXE 0x40f3d8 */
uint64_t es2_elf_step_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t W,
    uint32_t a,uint32_t f,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={a,W+0x43u,entry[2]};
    if(R8(W+0x49u)) { *ecx=r.ecx;return pair(r.eax,r.edx); }  /* 40f3ef */
    r.eax=W+0x43u;
    if(R8(W+0x47u)) {                                          /* 40f3fb */
        r.eax=W+0x83u;
        { const uint32_t args[1]={W+0x83u};wc_call(g,0x467a48u,args,1,&r);r.ecx=args[0]; }
        r.edx=(uint16_t)r.eax?0u:1u;
        if(r.edx) {                                            /* 40f416 */
            uint32_t P=R32(W+0x10u);r.eax=P;
            int done=!P||!R16(P+0x24u);                        /* 40f41d, 40f424 */
            if(!done) {
                r.edx=R32(W+0x14u);r.eax=R32(P+0x20u);
                r.ecx=R16(r.edx);r.edx=(uint32_t)R16(r.eax)-1u;
                done=r.ecx==r.edx;                             /* 40f435 */
            }
            if(done) {
                r.edx=W+0x43u;r.eax=r.edx;W8(W+0x48u,1);W8(W+0x47u,0);
            } else {                                           /* 40f448; 40f44c falls through */
                P=R32(W+0x10u);const uint32_t Q=R32(W+0x14u);
                const uint32_t n=(uint32_t)R16(Q)+1u,count=R16(R32(P+0x20u));
                r.eax=n/count;r.edx=n%count;r.ecx=W+0x10u;
                W16(Q,r.edx);r.eax=R32(W+0x14u);
                W16(W+0x84u,0);
            }
        }
    }
    r.ecx=lo(r.ecx,f);                                         /* 40f476: mov cx,[ebp+10h] */
    const uint32_t args[3]={W,a,r.ecx};
    wc_call(g,0x40f00cu,args,3,&r);
    *ecx=r.ecx;return pair(r.eax,r.edx);
}

/* ---- the weapon constructors (weapon_classes.md, section
 * "the weapon constructors"), from the reference bytes. The
 * register struct r follows EAX/EDX/ECX so that each game call sees the
 * original's entry registers; the bridge returns the callee's. ---- */
/* count +1: the pointer [6CBEE4h] read, then [pointer] += 1; returns the
 * pointer. */
static uint32_t wc_count(const es2_guest_memory *m,const es2_weapon_graph *g) {
    const uint32_t p=R32(VA(0x6cbee4u));W32(p,R32(p)+1u);return p;
}
static void wc_memset(const es2_weapon_graph *g,uint32_t to,uint32_t size,es2_weapon_registers *r) {
    const uint32_t a[3]={to,0,size};wc_call(g,0x49681au,a,3,r);
}
/* Original: DBSIM.EXE 0x40df30. EBX = W, EDI = A, ESI = M. */
uint64_t es2_weapon_ctor_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t W,
    uint32_t A,uint32_t M,uint32_t C,uint32_t P,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[2];
    r.eax=sx8(R8(M+6u));
    if((int32_t)r.eax>3)r.eax=0;                              /* 40df46 */
    else {
        r.edx=sx8(R8(M+6u));r.ecx=C;
        a[0]=C;a[1]=r.edx;wc_call(g,0x40fab0u,a,2,&r);
    }
    const uint32_t shape=r.eax;
    W32(W,VA(0x4a69f0u));W32(W,VA(0x4a6a14u));
    r.edx=wc_count(m,g);
    W32(W,VA(0x4a6250u));
    r.ecx=wc_count(m,g);
    r.eax=W+0x10u;a[0]=r.eax;a[1]=shape;wc_call(g,0x402fc0u,a,2,&r);
    W32(W,VA(0x49a494u));
    r.eax=wc_count(m,g);
    W32(W,VA(0x498e44u));
    W32(W+0x18u,M);
    r.edx=C;W32(W+0x1cu,r.edx);
    r.ecx=P;W32(W+0x20u,r.ecx);
    if(fx_s16(R16(M+2u))>=0) {                                 /* 40dfb3 */
        r.eax=lo(r.eax,R16(M+2u));a[0]=A;a[1]=r.eax;wc_call(g,0x402374u,a,2,&r);
    } else r.eax=0;
    W32(W+0x5bu,r.eax);
    if(fx_s16(R16(M+4u))>=0) {                                 /* 40dfcf */
        r.edx=lo(r.edx,R16(M+4u));a[0]=A;a[1]=r.edx;wc_call(g,0x402374u,a,2,&r);
    } else r.eax=0;
    W32(W+0x5fu,r.eax);
    const uint32_t S=R32(W+0x10u);
    if(S) {                                                    /* 40dfeb */
        a[0]=W;wc_call(g,0x40f540u,a,1,&r);
        r.ecx=W;                                               /* POP ECX */
        a[0]=S;a[1]=r.eax;wc_call(g,0x40dd4cu,a,2,&r);
    }
    r.eax=W+0x24u;wc_memset(g,r.eax,6,&r);
    r.edx=W+0x2au;wc_memset(g,r.edx,6,&r);
    W16(W+0x31u,0);
    wc_memset(g,W+0x33u,8,&r);
    r.eax=W+0x3bu;wc_memset(g,r.eax,8,&r);
    r.eax=W+0x43u;wc_memset(g,r.eax,0x18u,&r);
    W8(W+0x4cu,1);
    W16(W+0x63u,0x400u);
    r.edx=wc_count(m,g);
    *ecx=r.ecx;return pair(W,r.edx);
}
/* The base call of beam_ctor and gun_ctor: (W, A, M, C, P) with EAX = A,
 * EDX = C, ECX = M. */
static void wc_weapon_base(const es2_weapon_graph *g,uint32_t W,uint32_t A,uint32_t M,uint32_t C,uint32_t P,
    es2_weapon_registers *r) {
    const uint32_t a[5]={W,A,M,C,P};r->eax=A;r->edx=C;r->ecx=M;wc_call(g,0x40df30u,a,5,r);
}
/* Original: DBSIM.EXE 0x40e074 */
uint64_t es2_beam_ctor_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t W,
    uint32_t A,uint32_t M,uint32_t C,uint32_t P,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    wc_weapon_base(g,W,A,M,C,P,&r);
    W32(W,VA(0x498dccu));
    { const uint32_t a[2]={0x334u,0x4b0u};wc_call(g,0x47e0b0u,a,2,&r); }
    const uint32_t level=r.eax;                                /* ESI */
    W16(W+0x7bu,level);W16(W+0x7du,level);
    W8(W+0x34u,1);W8(W+0x3cu,1);
    W32(W+0x77u,0);W32(W+0x7fu,0x14u);
    *ecx=wc_count(m,g);return pair(W,0);
}
/* Original: DBSIM.EXE 0x40e140 */
uint64_t es2_gun_ctor_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t W,
    uint32_t A,uint32_t M,uint32_t C,uint32_t P,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    wc_weapon_base(g,W,A,M,C,P,&r);
    W32(W,VA(0x498d54u));
    const uint32_t x=R16(R32(W+0x1cu)+0x3au);
    const uint32_t edx=(uint32_t)fx_s16(x)<<8;
    W16(W+0x7bu,x);W32(W+0x7du,edx);W32(W+0x77u,0);
    (void)wc_count(m,g);
    *ecx=0;return pair(W,edx);
}
/* Original: DBSIM.EXE 0x40e234 */
uint64_t es2_equip_ctor_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t W,
    uint32_t A,uint32_t M,uint32_t C,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={C,M,A};(void)entry;
    { const uint32_t a[5]={W,A,M,C,0};wc_call(g,0x40df30u,a,5,&r); }
    W32(W,VA(0x498cdcu));
    W8(W+0x4cu,0);W16(W+0x77u,0xffffu);
    const uint32_t edx=wc_count(m,g);
    *ecx=r.ecx;return pair(W,edx);
}
/* The base call of the five equipment classes: (W, A, M, C, 0) with EAX =
 * C, EDX = M, ECX = A; then the vtable. */
static void wc_equip_base(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t W,uint32_t A,uint32_t M,
    uint32_t C,uint32_t vtable,es2_weapon_registers *r) {
    const uint32_t a[5]={W,A,M,C,0};r->eax=C;r->edx=M;r->ecx=A;wc_call(g,0x40e234u,a,5,r);
    W32(W,VA(vtable));
}
/* Original: DBSIM.EXE 0x40e274 */
uint64_t es2_ecm_ctor_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t W,
    uint32_t A,uint32_t M,uint32_t C,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    wc_equip_base(m,g,W,A,M,C,0x498c64u,&r);
    W16(W+0x7fu,0);W16(W+0x7fu,0);W32(W+0x79u,0);W16(W+0x77u,0x12u);
    *ecx=wc_count(m,g);return pair(W,0);
}
/* Original: DBSIM.EXE 0x40e2bc */
uint64_t es2_turbo_ctor_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t W,
    uint32_t A,uint32_t M,uint32_t C,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    wc_equip_base(m,g,W,A,M,C,0x498becu,&r);
    W32(W+0x79u,0);W16(W+0x77u,0x1fu);W32(W+0x81u,0);W32(W+0x7du,0x7d0u);
    *ecx=wc_count(m,g);return pair(W,0);
}
/* 40e308, 40e344, 40e380: dword W+79h = 0, the code word, count +1. */
static uint64_t wc_equip_class(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t W,uint32_t A,
    uint32_t M,uint32_t C,uint32_t vtable,uint32_t code,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    wc_equip_base(m,g,W,A,M,C,vtable,&r);
    W32(W+0x79u,0);W16(W+0x77u,code);
    const uint32_t edx=wc_count(m,g);
    *ecx=r.ecx;return pair(W,edx);
}
/* Original: DBSIM.EXE 0x40e308 */
uint64_t es2_targ_ctor_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t W,
    uint32_t A,uint32_t M,uint32_t C,const uint32_t entry[3],uint32_t *ecx) {
    return wc_equip_class(m,g,W,A,M,C,0x498b74u,0x1du,entry,ecx);
}
/* Original: DBSIM.EXE 0x40e344 */
uint64_t es2_shield_ctor_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t W,
    uint32_t A,uint32_t M,uint32_t C,const uint32_t entry[3],uint32_t *ecx) {
    return wc_equip_class(m,g,W,A,M,C,0x498afcu,0x1eu,entry,ecx);
}
/* Original: DBSIM.EXE 0x40e380 */
uint64_t es2_energy_ctor_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t W,
    uint32_t A,uint32_t M,uint32_t C,const uint32_t entry[3],uint32_t *ecx) {
    return wc_equip_class(m,g,W,A,M,C,0x498a84u,0x20u,entry,ecx);
}

/* ---- the weapon gauge syncs (slot +50h) and the LAEW emission (slot
 * +28h) (weapon_classes.md, section "the weapon gauge syncs, the LAEW emission
 * and the empty bodies"). The widget slots are reached through the widget's own
 * table (g->call with the guest address read from memory at the time of the
 * call; no observer key); the direct callees go through wc_call with their
 * original VA. The original's local record lives in g->scratch (guest
 * addressable, at least 24 bytes): its address is what the widget receives. The
 * registers into every game call and the registers out follow the original.
 * ---- */
static void wc_slot(const es2_weapon_graph *g,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    g->call(g->context,f,a,n,r);
}
/* mov eax,[W+79h]; push eax; mov edx,[eax]; call [edx+18h]; pop ecx:
 * returns the widget's 8-byte state record (EAX). */
static uint32_t wc_widget_read(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t W,es2_weapon_registers *r) {
    uint32_t widget=R32(W+0x79u);
    r->eax=widget;r->edx=R32(widget);
    wc_slot(g,R32(r->edx+0x18u),&widget,1,r);
    r->ecx=widget;return r->eax;
}
/* The widget is read again: mov eax,[W+79h]; ... call [edx+14h] with the
 * local record (ECX, argument 2). */
static void wc_widget_write(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t W,es2_weapon_registers *r) {
    uint32_t a[2];a[0]=R32(W+0x79u);a[1]=g->scratch;
    r->eax=a[0];r->edx=R32(a[0]);r->ecx=g->scratch;
    wc_slot(g,R32(r->edx+0x14u),a,2,r);
}
/* rep movsd x5, movsw, movsb: the 17h-byte gauge record into the local. */
static void wc_copy_record(const es2_guest_memory *m,uint32_t to,uint32_t from) {
    for(unsigned i=0;i<20u;i+=4u)W32(to+i,R32(from+i));
    W16(to+20u,R16(from+20u));W8(to+22u,R8(from+22u));
}
/* Original: DBSIM.EXE 0x40f144 (slot +50h of EQUIP, TARG, SHIELD, ENERGY). */
uint64_t es2_equip_sync_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t W,
    const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};const uint32_t s=g->scratch;
    const uint32_t rec=wc_widget_read(m,g,W,&r);
    W32(s,R32(rec));W32(s+4u,R32(rec+4u));
    W8(s+1u,R8(W+0x49u));                                      /* 40f16c */
    wc_widget_write(m,g,W,&r);
    *ecx=R32(s+4u);return pair(r.eax,r.edx);
}
/* Original: DBSIM.EXE 0x40f184 (slot +50h of ECM). */
uint64_t es2_ecm_sync_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t W,
    const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};const uint32_t s=g->scratch;
    const uint32_t rec=wc_widget_read(m,g,W,&r);
    W32(s,R32(rec));W32(s+4u,R32(rec+4u));
    const uint32_t shown=(uint32_t)fx_s16(R16(W+0x7du));        /* 40f1a3 */
    const uint8_t toggle=R8(s);
    if(shown!=toggle) {                                        /* 40f1ae */
        r.eax=(rec&0xffffff00u)|toggle;r.ecx=toggle;
        r.edx=0x2au;if(!toggle)++r.edx;
        uint32_t code=r.edx;wc_call(g,0x420a68u,&code,1,&r);r.ecx=code;   /* 40f1bd */
    }
    r.eax=R8(s);W16(W+0x7du,r.eax);W16(W+0x7fu,r.eax);          /* 40f1cb */
    W8(s+1u,R8(W+0x49u));
    wc_widget_write(m,g,W,&r);
    *ecx=R32(s+4u);return pair(r.eax,r.edx);
}
/* Original: DBSIM.EXE 0x40f1f0 (slot +50h of TURBO). */
uint64_t es2_turbo_sync_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t W,
    const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};const uint32_t s=g->scratch;
    const uint32_t rec=wc_widget_read(m,g,W,&r);
    W32(s,R32(rec));W32(s+4u,R32(rec+4u));r.ecx=R32(rec+4u);
    if(R8(s)) {                                                /* 40f20f */
        if(R32(W+0x81u)) {                                     /* 40f215 */
            if(fx_s32(R32(W+0x7du))<=0) { W32(W+0x81u,0);W32(W+0x7du,0); }   /* 40f222 */
        } else if(fx_s32(R32(W+0x7du))>0x258) {               /* 40f231 */
            W32(W+0x81u,1);
            uint32_t sound=0x2cu;wc_call(g,0x462830u,&sound,1,&r);r.ecx=sound;   /* 40f246 */
        }
        if(!R32(W+0x81u))W8(s,0);                              /* 40f24c */
    } else W32(W+0x81u,0);                                     /* 40f25b */
    W32(s+4u,R32(W+0x7du));W8(s+1u,R8(W+0x49u));              /* 40f266 */
    wc_widget_write(m,g,W,&r);
    *ecx=R32(s+4u);return pair(r.eax,r.edx);
}
/* Original: DBSIM.EXE 0x40f288 (slot +50h of BEAM and ELF). b1, b2, b3 are
 * the low bytes of the arguments at [ebp+0Ch], [ebp+10h], [ebp+14h]. */
uint64_t es2_beam_sync_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t W,
    uint32_t b1,uint32_t b2,uint32_t b3,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};const uint32_t s=g->scratch;
    uint32_t handle=R32(W+0x77u);r.eax=handle;
    wc_call(g,0x440e9cu,&handle,1,&r);                        /* 40f298 */
    wc_copy_record(m,s,r.eax);
    if(R8(W+0x34u))W32(s+4u,(uint32_t)((int32_t)fx_s16(R16(W+0x7bu))*1024/0x4b0));   /* 40f2b0 */
    else W16(W+0x7bu,fx_sar(R32(s+4u)*16u*3u*5u*5u,10));       /* 40f2ca */
    W32(s,(uint32_t)((int32_t)fx_s16(R16(W+0x7du))*1024/0x4b0));
    W8(s+0x12u,R8(W+0x43u));W8(s+0x11u,R8(W+0x49u));          /* 40f2fd, 40f304 */
    W8(s+0x15u,b2);W8(s+0x14u,b1);W8(s+0x16u,b3);
    uint32_t a[2];a[0]=R32(W+0x77u);a[1]=s;
    r.eax=a[0];r.edx=R32(a[0]);r.ecx=s;
    wc_slot(g,R32(r.edx+0x10u),a,2,&r);                        /* 40f320 */
    *ecx=r.ecx;return pair(r.eax,r.edx);
}
/* Original: DBSIM.EXE 0x40f330 (slot +50h of GUN). */
uint64_t es2_gun_sync_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t W,
    uint32_t b1,uint32_t b2,uint32_t b3,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};const uint32_t s=g->scratch;
    uint32_t handle=R32(W+0x77u);r.eax=handle;
    wc_call(g,0x4414ccu,&handle,1,&r);                        /* 40f340 */
    wc_copy_record(m,s,r.eax);r.ecx=0;
    const uint32_t target=(uint32_t)fx_s16(R16(W+0x7bu))<<8;  /* 40f355 */
    if(fx_s32(target)<fx_s32(R32(W+0x7du))) {                 /* 40f35c */
        uint32_t step=0xfau;wc_call(g,0x467924u,&step,1,&r);r.ecx=step;   /* 40f366 */
        W32(W+0x7du,R32(W+0x7du)-(uint32_t)fx_s16(r.eax));
    }
    if(fx_s32(target)>fx_s32(R32(W+0x7du)))W32(W+0x7du,target);   /* 40f372 */
    W32(s,fx_sar(R32(W+0x7du),8));
    W8(s+0x15u,b2);W8(s+0x11u,R8(W+0x49u));W8(s+0x14u,b1);W8(s+0x16u,b3);
    uint32_t a[2];a[1]=s;a[0]=R32(W+0x77u);
    r.ecx=a[0];r.eax=R32(a[0]);r.edx=s;
    wc_slot(g,R32(r.eax+0x10u),a,2,&r);                        /* 40f3a8 */
    *ecx=r.ecx;return pair(r.eax,r.edx);
}
/* Original: DBSIM.EXE 0x40f3b8 (slot +50h of LAEW): gun_sync with the three
 * argument words; the pushed dwords keep the high words of the entry EDX
 * and ECX, which gun_sync's getter call sees in EDX and ECX. */
uint64_t es2_laew_sync_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t W,
    uint32_t w1,uint32_t w2,uint32_t w3,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t e[3]={W,lo(entry[1],w1),lo(entry[2],w2)};
    return es2_gun_sync_registers(m,g,W,w1&0xffu,w2&0xffu,w3&0xffu,e,ecx);
}
/* Original: DBSIM.EXE 0x40ea04 (slot +28h of LAEW): preparation 0x40e788,
 * charge, flag and the emission 0x40acac (the position and angle blocks
 * 0x4a9854 and 0x4a9860, as weapon_fire passes them). */
uint64_t es2_laew_fire_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t W,
    uint32_t owner,uint32_t free_word,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    { const uint32_t a[3]={W,owner,free_word};wc_call(g,0x40e788u,a,3,&r); }
    if(!(uint16_t)free_word)W16(W+0x7bu,R16(W+0x7bu)-1u);      /* 40ea21 */
    W8(W+0x46u,0);                                              /* 40ea2a */
    r.edx=R32(W+0x81u);r.ecx=R32(W+0x20u);
    r.eax=lo(W+0x43u,R16(r.ecx+2u));
    { const uint32_t a[5]={r.eax,VA(0x4a9854u),VA(0x4a9860u),owner,r.edx};wc_call(g,0x40acacu,a,5,&r); }
    *ecx=r.ecx;return pair(r.eax,r.edx);
}
