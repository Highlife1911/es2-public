#include "es2/core/mech_fly.h"
#include "fixed.h"
/* MECH "player fly" movement (docs/reverse-engineering/hit_tests.md, the section
 * "MECH player fly movement"). r holds what the original's
 * EAX/EDX/ECX hold at the next game call, edi the terrain height of the current
 * block; lo() is a 16-bit load or operation that keeps the register's high
 * word. Every frame local lives in the scratch frame at its original EBP offset
 * (f = the synthetic EBP) and is reread where the original rereads it: the
 * segment f-114h (+0 the pose pointer, +4 the length, +8 the radius word), the
 * pose copy f-DCh, the probe points f-8Ch (part 8), f-98h (part 7), f-BCh (part
 * 5), f-B0h (part 4), f-A4h (nose), the work point f-80h, the region flag
 * pointer f-8 and the per-block temporaries. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define VA(x) (g->image+(x)-0x400000u)
static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
static int above(uint32_t height,uint32_t z) { return fx_s32(height)>fx_s32(z); }
/* Calls name the original VA (observer key); the bridge gets the address
 * in this image. Registers are updated in place. */
static void fly_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,g->image+va-0x400000u,a,n,r);
}
/* transform_point(source, f-DCh, out) */
static void fly_transform(const es2_weapon_graph *g,uint32_t source,uint32_t f,uint32_t out,es2_weapon_registers *r) {
    const uint32_t a[3]={source,f-0xdcu,out};fly_call(g,0x48043cu,a,3,r);
}
/* terrain_height(map, f-80h) -> the height */
static uint32_t fly_terrain(const es2_weapon_graph *g,uint32_t map,uint32_t f,es2_weapon_registers *r) {
    const uint32_t a[2]={map,f-0x80u};fly_call(g,0x46e180u,a,2,r);return r->eax;
}
/* sweep(0x49a170); POP ECX reloads the packet address. */
static uint32_t fly_sweep(const es2_weapon_graph *g,es2_weapon_registers *r) {
    const uint32_t a[1]={VA(0x49a170u)};fly_call(g,0x426528u,a,1,r);r->ecx=a[0];return r->eax;
}
static void fly_q10(const es2_weapon_graph *g,uint32_t x,uint32_t y,es2_weapon_registers *r) {
    const uint32_t a[2]={x,y};fly_call(g,0x47e0b0u,a,2,r);
}
/* mech_part_hit(A, part, 0x49a170, point) */
static void fly_part(const es2_weapon_graph *g,uint32_t A,uint32_t part,uint32_t point,es2_weapon_registers *r) {
    const uint32_t a[4]={A,part,VA(0x49a170u),point};fly_call(g,0x4188c8u,a,4,r);
}
/* The debris after a cleared region flag: debris_table(&w) with w = 3 at
 * word f-k, then debris_burst([T] + 10*s16(w), [T+6], f-80h, 0); the
 * record T is kept at f-t (t = 0 for the variant that keeps it in EAX).
 * [T+6], the word w and [T] are read in that order. variant: 0 = EAX the
 * offset, EDX the table (41a0c8, 41a288); 1 = EAX the table, EDX the
 * offset (41a0fc). */
static void fly_debris(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t f,uint32_t k,uint32_t t,int variant,
    es2_weapon_registers *r) {
    uint32_t a[4];
    W16(f-k,3u);
    a[0]=f-k;fly_call(g,0x4089bcu,a,1,r);
    r->ecx=f-k;
    const uint32_t record=r->eax;
    if(t)W32(f-t,record);
    const uint32_t shapes=R32((t?R32(f-t):record)+6u);
    r->ecx=sx(R16(f-k));
    const uint32_t offset=r->ecx*10u,table=R32(t?R32(f-t):record)+offset;
    if(variant){r->eax=table;r->edx=offset;}else{r->eax=offset;r->edx=table;}
    a[0]=table;a[1]=shapes;a[2]=f-0x80u;a[3]=0;fly_call(g,0x408530u,a,4,r);
}

/* Original: DBSIM.EXE 0x4198f4 */
uint64_t es2_mech_fly_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,const uint32_t entry[3]) {
    const uint32_t f=g->scratch+ES2_MECH_FLY_FRAME,R=A+0x2b9u,P=VA(0x49a170u),map=VA(0x4a0be0u);
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t a[3],edi;
    /* 4198fd: the five probe offsets, stored once (guard bytes). */
    if(!R8(VA(0x49a194u))) { /* 419910 */
        W32(VA(0x49a188u),1000u);W32(VA(0x49a18cu),0xfffffd44u);W32(VA(0x49a190u),0xffffff9cu);
        W8(VA(0x49a194u),R8(VA(0x49a194u))+1u);
    }
    if(!R8(VA(0x49a1a1u))) { /* 41993d */
        W32(VA(0x49a195u),0xfffffc18u);W32(VA(0x49a199u),0xfffffd44u);W32(VA(0x49a19du),0xffffff9cu);
        W8(VA(0x49a1a1u),R8(VA(0x49a1a1u))+1u);
    }
    if(!R8(VA(0x49a1aeu))) { /* 41996a */
        W32(VA(0x49a1a2u),0x1c2u);r.eax=0;W32(VA(0x49a1a6u),0xfffffe0cu);W32(VA(0x49a1aau),r.eax);
        W8(VA(0x49a1aeu),R8(VA(0x49a1aeu))+1u);
    }
    if(!R8(VA(0x49a1bbu))) { /* 419994 */
        W32(VA(0x49a1afu),0xfffffe3eu);r.edx=0;W32(VA(0x49a1b3u),0xfffffe0cu);W32(VA(0x49a1b7u),r.edx);
        W8(VA(0x49a1bbu),R8(VA(0x49a1bbu))+1u);
    }
    if(!R8(VA(0x49a1c8u))) { /* 4199bf */
        r.ecx=0;r.eax=0;W32(VA(0x49a1bcu),r.ecx);W32(VA(0x49a1c0u),1000u);W32(VA(0x49a1c4u),r.eax);
        W8(VA(0x49a1c8u),R8(VA(0x49a1c8u))+1u);
    }
    /* 4199e0: the lazy matrix euler_matrix(A+0Ch, A+12h). */
    edi=A+0xcu;
    if(!R16(edi+0x26u)) { /* 4199e8 */
        W32(f-4u,edi);r.eax=edi+6u;r.edx=R32(f-4u);
        a[0]=r.edx;a[1]=r.eax;fly_call(g,0x47ebb8u,a,2,&r);
        W16(edi+0x26u,1u);
    }
    /* 419a03: copy the 32-byte pose (bytes 12h/13h included) to f-DCh. */
    r.eax=edi+6u;
    for(uint32_t k=0;k<0x12u;k+=2u)W16(f-0xdcu+k,R16(r.eax+k));
    W8(f-0xcau,R8(r.eax+0x12u));W8(f-0xc9u,R8(r.eax+0x13u));
    r.eax+=0x14u;
    W32(f-0xc8u,R32(r.eax));r.edx=R32(r.eax+4u);W32(f-0xc4u,r.edx);W32(f-0xc0u,R32(r.eax+8u));
    /* 419a85: the five probes through the copy. */
    r.eax=f-0xdcu;r.ecx=f-0x8cu;fly_transform(g,VA(0x49a188u),f,f-0x8cu,&r);
    r.edx=f-0x98u;r.ecx=f-0xdcu;fly_transform(g,VA(0x49a195u),f,f-0x98u,&r);
    r.eax=f-0xbcu;r.edx=f-0xdcu;fly_transform(g,VA(0x49a1a2u),f,f-0xbcu,&r);
    r.ecx=f-0xb0u;r.eax=f-0xdcu;fly_transform(g,VA(0x49a1afu),f,f-0xb0u,&r);
    r.edx=f-0xa4u;r.ecx=f-0xdcu;fly_transform(g,VA(0x49a1bcu),f,f-0xa4u,&r);
    /* 419b1e: the velocity integration. */
    r.eax=A+0x92u;
    if(!R8(r.eax+0x12u)) { /* 419b28 */
        r.eax=R+0xcu;
        r.edx=R32(r.eax);W32(f-0xe8u,r.edx);
        r.ecx=R32(r.eax+4u);W32(f-0xe4u,r.ecx);
        r.eax=R32(r.eax+8u);W32(f-0xe0u,r.eax);
        r.edx=lo(r.edx,R16(f-0xe8u));
        a[0]=r.edx;fly_call(g,0x467924u,a,1,&r);
        r.ecx=sx(r.eax);W32(f-0xe8u,r.ecx);
        r.eax=lo(r.eax,R16(f-0xe4u));
        a[0]=r.eax;fly_call(g,0x467924u,a,1,&r);
        r.edx=sx(r.eax);r.ecx=a[0];W32(f-0xe4u,r.edx);
        r.ecx=lo(r.ecx,R16(f-0xe0u));
        a[0]=r.ecx;fly_call(g,0x467924u,a,1,&r);
        r.ecx=a[0];r.eax=sx(r.eax);W32(f-0xe0u,r.eax);
        r.eax=A+0x26u;
        r.edx=R32(f-0xe0u);W32(r.eax+8u,R32(r.eax+8u)+r.edx);
        r.ecx=R32(f-0xe8u);W32(r.eax,R32(r.eax)+r.ecx);
        r.edx=R32(f-0xe4u);W32(r.eax+4u,R32(r.eax+4u)+r.edx);
    }
    /* 419ba9: the segment and the static sweep packet. */
    r.eax=f-0xdcu;r.edx=f-0x114u;W32(f-0x114u,r.eax);
    r.eax=0;r.ecx=R32(R+4u);
    W16(P+4u,8000u);W32(f-0x110u,r.ecx);W16(P+6u,8000u);W16(P+8u,0);
    W32(P+0xeu,r.eax);W16(P+0x12u,5u);W32(P+0x14u,A);W32(P,r.edx);
    r.ecx=R32(A+0x20eu);W32(f-8u,r.ecx);
    /* 419c04: part 8 (region word +10h, roll rate down). */
    r.eax=R32(f-8u);
    if(R16(r.eax+0x10u)) { /* 419c0c */
        r.edx=f-0x80u;r.ecx=f-0xdcu;fly_transform(g,VA(0x49a188u),f,f-0x80u,&r);
        r.eax=f-0x80u;r.edx=R32(map);edi=fly_terrain(g,r.edx,f,&r);
        r.eax=R32(f-0x84u);W32(f-0xc0u,r.eax);
        r.edx=R32(f-0x8cu);W32(f-0xc8u,r.edx);
        r.ecx=R32(f-0x88u);W32(f-0xc4u,r.ecx);
        W16(f-0x10cu,300u);
        if(above(edi,R32(f-0x78u))||fly_sweep(g,&r)) { /* 419c6f, 419c7e */
            if(above(edi,R32(f-0x78u))) { /* 419c87: terrain contact */
                r.eax=edi-R32(f-0x78u);W32(f-0xcu,r.eax);
                r.edx=R32(f-0xcu);fly_q10(g,4000u,r.edx,&r);
                W16(R+0x1au,R16(R+0x1au)-r.eax);
                r.ecx=R32(R+4u);W32(f-0x10u,r.ecx);
                r.eax=R32(f-0x10u);fly_q10(g,r.eax,500u,&r);
                W16(P+4u,r.eax);
            } else {
                W16(P+4u,1000u);W16(R+0x1au,R16(R+0x1au)+0xf060u);
            }
            r.edx=lo(r.edx,R16(A+0xeu));r.edx=lo(r.edx,r.edx+R16(R+0x1au));W16(f-0x12u,r.edx);
            r.eax=A+0xcu;r.edx=lo(r.edx,R16(f-0x12u));W16(r.eax+2u,r.edx);W16(r.eax+0x26u,0);
            if(above(edi,R32(f-0x78u)))W32(f-0x78u,edi); /* 419cf4 */
            r.ecx=f-0x80u;fly_part(g,A,8u,f-0x80u,&r);
        }
    }
    /* 419d0d: part 7 (region word +0Eh, roll rate up). */
    r.eax=R32(f-8u);
    if(R16(r.eax+0xeu)) { /* 419d15 */
        r.edx=f-0x80u;r.ecx=f-0xdcu;fly_transform(g,VA(0x49a195u),f,f-0x80u,&r);
        r.eax=f-0x80u;r.edx=R32(map);edi=fly_terrain(g,r.edx,f,&r);
        r.eax=R32(f-0x90u);W32(f-0xc0u,r.eax);
        r.edx=R32(f-0x98u);W32(f-0xc8u,r.edx);
        r.ecx=R32(f-0x94u);W32(f-0xc4u,r.ecx);
        r.eax=R32(R+4u);W32(f-0x110u,r.eax);
        W16(f-0x10cu,300u);
        if(above(edi,R32(f-0x78u))||fly_sweep(g,&r)) { /* 419d81, 419d90 */
            if(above(edi,R32(f-0x78u))) { /* 419d99 */
                r.edx=edi-R32(f-0x78u);W32(f-0x18u,r.edx);
                r.ecx=R32(f-0x18u);fly_q10(g,4000u,r.ecx,&r);
                W16(R+0x1au,R16(R+0x1au)+r.eax);
                r.eax=R32(R+4u);W32(f-0x1cu,r.eax);
                r.edx=R32(f-0x1cu);fly_q10(g,r.edx,500u,&r);
                W16(P+4u,r.eax);
            } else {
                W16(R+0x1au,R16(R+0x1au)+0xfa0u);W16(P+4u,1000u);
            }
            r.ecx=lo(r.ecx,R16(A+0xeu));r.ecx=lo(r.ecx,r.ecx+R16(R+0x1au));W16(f-0x1eu,r.ecx);
            r.eax=A+0xcu;r.edx=lo(r.edx,R16(f-0x1eu));W16(r.eax+2u,r.edx);W16(r.eax+0x26u,0);
            if(above(edi,R32(f-0x78u)))W32(f-0x78u,edi); /* 419e06 */
            r.ecx=f-0x80u;fly_part(g,A,7u,f-0x80u,&r);
        }
    }
    /* 419e1f: part 4 (region word +8, sweep only). */
    r.eax=R32(f-8u);
    if(R16(r.eax+8u)) { /* 419e27 */
        r.edx=R32(f-0xa8u);W32(f-0xc0u,r.edx);
        r.ecx=R32(f-0xb0u);W32(f-0xc8u,r.ecx);
        r.eax=R32(f-0xacu);W32(f-0xc4u,r.eax);
        r.edx=R32(R+4u);W32(f-0x110u,r.edx);
        W16(f-0x10cu,150u);
        if(fly_sweep(g,&r)) { /* 419e70 */
            W16(R+0x1au,R16(R+0x1au)+0x1f40u);W16(P+4u,5000u);
            r.eax=A+0xcu;
            r.ecx=lo(r.ecx,R16(A+0xeu));r.ecx=lo(r.ecx,r.ecx+R16(R+0x1au));W16(f-0x20u,r.ecx);
            W32(f-0x24u,r.eax);
            r.ecx=lo(r.ecx,R16(f-0x20u));r.edx=R32(f-0x24u);W16(r.edx+2u,r.ecx);
            r.edx=f-0xb0u;r.eax=R32(f-0x24u);W16(r.eax+0x26u,0);
            fly_part(g,A,4u,f-0xb0u,&r);
        }
    }
    /* 419ebe: part 5 (region word +0Ah); the part hit gets f-B0h (419f3d). */
    r.ecx=R32(f-8u);
    if(R16(r.ecx+0xau)) { /* 419ec6 */
        r.eax=R32(f-0xb4u);W32(f-0xc0u,r.eax);
        r.edx=R32(f-0xbcu);W32(f-0xc8u,r.edx);
        r.ecx=R32(f-0xb8u);W32(f-0xc4u,r.ecx);
        r.eax=R32(R+4u);W32(f-0x110u,r.eax);
        W16(f-0x10cu,150u);
        if(fly_sweep(g,&r)) { /* 419f0f */
            W16(R+0x1au,R16(R+0x1au)+0xe0c0u);W16(P+4u,5000u);
            r.ecx=A+0xcu;
            r.edx=lo(r.edx,R16(A+0xeu));r.edx=lo(r.edx,r.edx+R16(R+0x1au));W16(f-0x26u,r.edx);
            W32(f-0x2cu,r.ecx);
            r.edx=lo(r.edx,R16(f-0x26u));r.eax=R32(f-0x2cu);W16(r.eax+2u,r.edx);
            r.eax=f-0xb0u;r.ecx=R32(f-0x2cu);W16(r.ecx+0x26u,0);
            fly_part(g,A,5u,f-0xb0u,&r);
        }
    }
    /* 419f5d: the nose (part 0, region word +0, pitch rate). */
    r.edx=R32(f-8u);
    if(R16(r.edx)) { /* 419f64 */
        r.ecx=f-0x80u;r.eax=f-0xdcu;fly_transform(g,VA(0x49a1bcu),f,f-0x80u,&r);
        r.edx=f-0x80u;r.ecx=R32(map);edi=fly_terrain(g,r.ecx,f,&r);
        r.eax=R32(f-0x9cu);W32(f-0xc0u,r.eax);
        r.edx=R32(f-0xa4u);W32(f-0xc8u,r.edx);
        r.ecx=R32(f-0xa0u);W32(f-0xc4u,r.ecx);
        r.eax=R32(R+4u);W32(f-0x110u,r.eax);
        W16(f-0x10cu,200u);
        if(above(edi,R32(f-0x78u))||fly_sweep(g,&r)) { /* 419fd0, 419fdf */
            if(fx_s16(R16(R+0x18u))<0)W16(R+0x18u,0); /* 419fea */
            if(above(edi,R32(f-0x78u))) { /* 419ff5 */
                r.edx=edi-R32(f-0x78u);W32(f-0x30u,r.edx);
                r.ecx=R32(f-0x30u);fly_q10(g,2000u,r.ecx,&r);
                W16(R+0x18u,R16(R+0x18u)+r.eax);
                r.eax=R32(R+4u);W32(f-0x34u,r.eax);
                r.edx=R32(f-0x34u);fly_q10(g,r.edx,1000u,&r);
                W16(P+4u,r.eax);
            } else {
                W16(R+0x18u,R16(R+0x18u)+0x7d0u);W16(P+4u,5000u);
            }
            r.ecx=lo(r.ecx,R16(A+0xcu));r.ecx=lo(r.ecx,r.ecx+R16(R+0x18u));W16(f-0x36u,r.ecx);
            r.eax=A+0xcu;r.edx=lo(r.edx,R16(f-0x36u));W16(r.eax,r.edx);W16(r.eax+0x26u,0);
            r.ecx=0;W32(f-0x3cu,r.ecx);
            if(above(edi,R32(f-0x78u))) { W32(f-0x78u,edi);W32(f-0x3cu,1u); } /* 41a066 */
            r.eax=f-0x80u;fly_part(g,A,0u,f-0x80u,&r);
            r.edx=R32(f-8u);
            if(!R16(r.edx)) { /* 41a08d: the nose region is gone */
                if(R32(f-0x3cu)) { /* 41a093 */
                    r.ecx=f-0x3eu;fly_debris(m,g,f,0x3eu,0x44u,0,&r); /* EDX = [f-8] */
                } else {
                    r.edx=f-0x46u;fly_debris(m,g,f,0x46u,0,1,&r);
                }
                r.eax=A+0x92u;W8(r.eax+0x12u,1u);
            }
        }
    }
    /* 41a10e: the slope (regions +8, +0Ah and +0 all present). */
    r.ecx=R32(f-8u);
    if(R16(r.ecx+8u)&&(r.eax=R32(f-8u),R16(r.eax+0xau))&&(r.edx=R32(f-8u),R16(r.edx))) { /* 41a116, 41a124, 41a131 */
        r.ecx=0;r.eax=f-0x80u;W32(f-0x120u,r.ecx);
        r.edx=f-0xdcu;W32(f-0x11cu,15000u);W32(f-0x118u,0xfffffa24u);
        r.ecx=f-0x120u;fly_transform(g,f-0x120u,f,f-0x80u,&r);
        r.eax=f-0x80u;r.edx=R32(map);edi=fly_terrain(g,r.edx,f,&r);
        if(above(edi,R32(f-0x78u))) { /* 41a185 */
            if(fx_s16(R16(R+0x18u))<0)W16(R+0x18u,0); /* 41a18c */
            edi-=R32(f-0x78u);W32(f-0x4cu,edi);
            r.eax=R32(f-0x4cu);fly_q10(g,20u,r.eax,&r);
            W16(R+0x18u,R16(R+0x18u)+r.eax);
            r.edx=lo(r.edx,R16(A+0xcu));r.ecx=A+0xcu;r.edx=lo(r.edx,r.edx+R16(R+0x18u));W16(f-0x4eu,r.edx);
            W32(f-0x54u,r.ecx);
            r.edx=lo(r.edx,R16(f-0x4eu));r.eax=R32(f-0x54u);W16(r.eax,r.edx);
            r.ecx=R32(f-0x54u);W16(r.ecx+0x26u,0);
        }
    }
    /* 41a1d1: the ground clamp (part 6). */
    r.eax=A+0x26u;
    r.edx=R32(r.eax+8u);W32(f-0x78u,r.edx);
    r.edx=f-0x80u;
    r.ecx=R32(r.eax);W32(f-0x80u,r.ecx);
    r.ecx=R32(map);
    r.eax=R32(r.eax+4u);W32(f-0x7cu,r.eax);
    edi=fly_terrain(g,r.ecx,f,&r);
    if(above(edi,R32(f-0x78u))) { /* 41a1fd */
        W32(f-0x78u,edi);
        r.eax=A+0x26u;
        r.edx=R32(f-0x78u);W32(r.eax+8u,r.edx);
        r.ecx=R32(f-0x80u);W32(r.eax,r.ecx);
        r.edx=R32(f-0x7cu);W32(r.eax+4u,r.edx);
        r.eax=R32(R+4u);W32(f-0x58u,r.eax);
        r.ecx=R32(f-0x58u);fly_q10(g,r.ecx,5000u,&r);
        W16(P+4u,r.eax);
        r.eax=f-0x80u;fly_part(g,A,6u,f-0x80u,&r);
        r.edx=R32(f-8u);
        if(!R16(r.edx+0xcu)) { /* 41a253: the leg region is gone */
            r.ecx=f-0x5au;fly_debris(m,g,f,0x5au,0x60u,0,&r);
            r.edx=A+0x92u;W32(f-0x64u,r.edx);r.ecx=R32(f-0x64u);W8(r.ecx+0x12u,1u);
        }
    }
    /* 41a2a0: the player's flight sound (SOS calls, original). */
    r.eax=A+0x92u;
    if(R8(r.eax+0x11u)) { /* 41a2aa */
        r.eax=A+0x92u;
        if(R8(r.eax+7u)) { /* 41a2b6: dead */
            a[0]=0x2du;fly_call(g,0x462ac4u,a,1,&r);r.ecx=a[0];
        } else {
            edi=R32(R+8u);
            r.edx=R32(R+4u);W32(f-0x68u,r.edx);
            r.ecx=R32(R);W32(f-0x6cu,r.ecx);
            r.eax=R32(f-0x68u);r.edx=R32(f-0x6cu);
            a[0]=r.edx;a[1]=r.eax;a[2]=edi;fly_call(g,0x47de72u,a,3,&r);
            r.ecx=r.eax;r.eax=(r.ecx<<4)+0x6d60u;
            r.edx=fx_s32(r.eax)>=0xffff?0xffffu:r.eax; /* 41a2f2 */
            if(fx_s32(r.edx)<=0)r.edx=0; /* 41a2ff */
            W32(f-0x70u,r.edx);r.eax=R32(f-0x70u);
            a[0]=0x2du;a[1]=r.eax;fly_call(g,0x463114u,a,2,&r);
            r.ecx=A+0x26u;
            a[0]=0x2du;a[1]=r.ecx;fly_call(g,0x46297cu,a,2,&r);
        }
    }
    /* 41a322: weapon_converge(A, dist_gate(A+26h, target+26h) or 0). */
    if(R32(A+0x1a4u)) { /* 41a329 */
        r.eax=R32(A+0x1a4u);W32(f-0x74u,r.eax);
        r.edx=R32(f-0x74u)+0x26u;r.ecx=A+0x26u;
        a[0]=r.ecx;a[1]=r.edx;fly_call(g,0x49288cu,a,2,&r);
    } else r.eax=0;
    a[0]=A;a[1]=r.eax;fly_call(g,0x41a74cu,a,2,&r);
    r.eax=lo(r.eax,1u);
    return pair(r.eax,r.edx);
}
