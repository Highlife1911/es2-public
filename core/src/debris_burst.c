#include "es2/core/debris_burst.h"
#include "fixed.h"
/* Debris burst (docs/reverse-engineering/hit_tests.md, section "debris burst").
 * r holds what the original's EAX/EDX/ECX hold at the next game call; lo() is a
 * 16-bit load or operation that keeps the register's high word. Frame locals
 * whose address reaches a game call live in the scratch frame at the original
 * EBP offsets (f = the synthetic EBP). */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define VA(g,x) ((g)->image+(x)-0x400000u)
static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
/* Direct calls name the original VA (observer key); the bridge gets the
 * address in this image. Registers are updated in place. */
static void db_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,VA(g,va),a,n,r);
}

/* Original: DBSIM.EXE 0x4089bc. The comparison is a signed 32-bit one of
 * the two sign-extended words; the index is lowered with a 16-bit SUB. */
uint64_t es2_debris_table_registers(const es2_guest_memory *m,uint32_t image,uint32_t word) {
    uint32_t eax=image+0xa9650u,edx=sx(R16(image+0xa9654u));
    if(fx_s32(edx)>fx_s16(R16(word)))return pair(eax,edx);
    eax=R32(image+0xa964cu);
    W16(word,R16(word)-edx);
    return pair(eax,edx);
}

/* Original: DBSIM.EXE 0x408530. Frame f: the matrix f-20h (rotation words
 * f-20h..f-0Fh, mode byte f-0Eh, translation f-0Ch, f-8, f-4). */
uint64_t es2_debris_burst_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t t,uint32_t shapes,uint32_t pos,uint32_t flag,const uint32_t entry[3]) {
    uint32_t f=g->scratch+ES2_DEBRIS_FRAME,a[4];
    es2_weapon_registers r={f-0x20u,entry[1],entry[2]};
    a[0]=f-0x20u;a[1]=0;a[2]=0x20u;db_call(g,0x49681au,a,3,&r);
    r.edx=lo(r.edx,0x4000u);
    W16(f-0x10u,r.edx);W16(f-0x1au,r.edx);W16(f-0x20u,r.edx);
    r.eax=pos;
    r.ecx=R32(pos+8u);W32(f-4u,r.ecx);
    r.edx=R32(pos);W32(f-0xcu,r.edx);
    r.eax=R32(pos+4u);W32(f-8u,r.eax);
    r.eax=f-0x20u;r.ecx=flag;r.edx=shapes;
    a[0]=t;a[1]=shapes;a[2]=f-0x20u;a[3]=flag;db_call(g,0x4086bcu,a,4,&r);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x4086bc. The loop counter is compared as a signed
 * word (DI); a negative count picks nothing. The weight walk stops at the
 * first piece whose weight word is not below the remaining draw (signed
 * 16-bit); it has no end check, exactly as the original. */
uint64_t es2_debris_pick_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t t,uint32_t shapes,uint32_t matrix,uint32_t flag,const uint32_t entry[3]) {
    uint32_t a[4];
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    const uint32_t count=R16(t);
    r.eax=lo(r.eax,count);
    if(count) {
        for(uint32_t i=0;fx_s16(i)<fx_s16(count);++i) {
            r.eax=lo(r.eax,R16(t+8u));
            a[0]=VA(g,0x4d2599u);a[1]=r.eax;db_call(g,0x492f24u,a,2,&r);
            r.edx=r.eax;r.eax=R32(t+4u);
            for(;;) {
                r.ecx=lo(r.ecx,R16(r.eax+2u));
                if(!(fx_s16(r.edx)>fx_s16(r.ecx)))break;
                r.edx=lo(r.edx,r.edx-r.ecx);r.eax+=0xeu;
            }
            const uint32_t piece=r.eax;
            r.edx=matrix;r.eax=shapes;
            a[0]=shapes;a[1]=piece;a[2]=matrix;a[3]=flag;db_call(g,0x408588u,a,4,&r);
        }
    } else {
        uint32_t piece=R32(t+4u);
        for(uint32_t i=0;fx_s16(i)<fx_s16(R16(t+2u));++i,piece+=0xeu) {
            r.eax=flag;r.edx=matrix;r.ecx=shapes;
            a[0]=shapes;a[1]=piece;a[2]=matrix;a[3]=flag;db_call(g,0x408588u,a,4,&r);
        }
    }
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x408588 (the fourth argument is not read). Frame f:
 * object slot f-4, context word f-14h, runtime record f-24h, angles
 * f-2Ch..f-27h, Euler input f-34h, rotation f-48h, composed rotation
 * f-5Ch. */
uint64_t es2_debris_piece_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t shapes,uint32_t piece,uint32_t matrix,uint32_t flag,const uint32_t entry[3]) {
    uint32_t f=g->scratch+ES2_DEBRIS_FRAME,a[5];
    (void)flag;
    es2_weapon_registers r={VA(g,0x497d70u),entry[1],entry[2]};
    db_call(g,0x49630cu,NULL,0,&r);
    const uint32_t pool=R32(VA(g,0x4a9642u));
    r.edx=pool;a[0]=pool;db_call(g,0x471b30u,a,1,&r);r.ecx=pool;
    W32(f-4u,r.eax);
    if(r.eax) {
        W16(f-0x14u,0x14u);
        a[4]=sx(R16(piece+6u));r.eax=a[4];
        r.ecx=matrix+0x14u;a[2]=r.ecx;
        r.edx=sx(R16(piece+4u));a[3]=r.edx;
        r.eax=sx(R16(piece));r.edx=shapes;r.ecx=R32(shapes+4u*r.eax);a[1]=r.ecx;
        r.eax=R32(f-4u);a[0]=r.eax;
        db_call(g,0x408940u,a,5,&r);
        r.edx=R32(VA(g,0x6cbee4u));W32(r.edx,R32(r.edx)-4u);
        W16(f-0x14u,8u);
    }
    r.ecx=R32(f-4u);
    const uint32_t object=r.ecx;
    if(!object)return pair(r.eax,r.edx);
    r.eax=lo(r.eax,R16(piece+8u));
    if((uint16_t)r.eax!=0xffffu||R16(piece+0xau)!=0xffffu) {
        W16(f-0x34u,0);W16(f-0x32u,0);W16(f-0x30u,r.eax);
        r.edx=f-0x48u;r.eax=f-0x34u;a[0]=f-0x34u;a[1]=f-0x48u;db_call(g,0x47ebb8u,a,2,&r);
        r.edx=f-0x5cu;r.eax=matrix;r.ecx=f-0x48u;
        a[0]=f-0x48u;a[1]=matrix;a[2]=f-0x5cu;db_call(g,0x47f058u,a,3,&r);
        r.eax=f-0x2cu;r.edx=f-0x5cu;a[0]=f-0x5cu;a[1]=f-0x2cu;db_call(g,0x47f9a0u,a,2,&r);
    }
    r.eax=lo(r.eax,R16(piece+0xau));
    if((uint16_t)r.eax!=0xffffu) {
        r.edx=lo(r.edx,R16(f-0x28u));
        r.edx=lo(r.edx,r.edx+r.eax);
        r.edx=lo(r.edx,r.edx-R16(piece+8u));
        r.eax=r.edx;
        r.ecx=sx(R16(piece+0xcu));
        a[0]=object;a[1]=r.ecx;a[2]=r.eax;db_call(g,0x408b74u,a,3,&r);
    } else {
        r.eax=sx(R16(piece+0xcu));
        a[0]=object;a[1]=r.eax;db_call(g,0x408bb4u,a,2,&r);
    }
    if(R16(piece+8u)==0xffffu)return pair(r.eax,r.edx);
    r.eax=object+0xcu;
    r.edx=R32(f-0x2cu);W32(r.eax,r.edx);
    r.edx=lo(r.edx,R16(f-0x28u));W16(r.eax+4u,r.edx);
    W16(r.eax+0x26u,0);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x408940 */
uint64_t es2_debris_init_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t o,uint32_t shape,uint32_t pos,uint32_t b,uint32_t w,const uint32_t entry[3]) {
    uint32_t a[2];
    es2_weapon_registers r={shape,entry[1],entry[2]};
    a[0]=o;a[1]=shape;db_call(g,0x402188u,a,2,&r);
    W32(o,VA(g,0x497ed8u));W16(o+4u,1u);
    r.edx=pos;r.eax=o+0x26u;
    r.ecx=R32(pos+8u);W32(o+0x2eu,r.ecx);
    r.ecx=R32(pos);W32(o+0x26u,r.ecx);
    r.edx=R32(pos+4u);W32(o+0x2au,r.edx);
    r.eax=0;W32(o+0x50u,0);
    r.eax=(uint8_t)b;W8(o+0x41u,b);
    r.edx=lo(r.edx,w);W16(o+0x59u,w);
    a[0]=VA(g,0x4d2599u);a[1]=0x7530u;db_call(g,0x492f24u,a,2,&r);
    r.eax=sx(r.eax)+0x7d0u;W32(o+0x55u,r.eax);
    r.edx=R32(VA(g,0x4a964cu));W32(o+0x5du,r.edx);
    r.ecx=R32(VA(g,0x6cbee4u));W32(r.ecx,R32(r.ecx)+1u);
    return pair(o,r.edx);
}

/* Original: DBSIM.EXE 0x408b74 */
uint64_t es2_debris_spin_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t o,uint32_t speed,uint32_t heading,const uint32_t entry[3]) {
    uint32_t a[4];
    const uint32_t low=R16(VA(g,0x4a9664u));
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    r.eax=lo(r.eax,R16(VA(g,0x4a9666u)));
    r.eax=lo(r.eax,r.eax-low);
    a[0]=VA(g,0x4d2599u);a[1]=r.eax;db_call(g,0x492f24u,a,2,&r);
    r.eax=lo(r.eax,r.eax+low);
    r.edx=lo(r.edx,heading);
    a[0]=o;a[1]=speed;a[2]=r.eax;a[3]=r.edx;r.eax=speed;
    db_call(g,0x4089e4u,a,4,&r);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x408bb4 */
uint64_t es2_debris_spin_any_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t o,uint32_t speed,const uint32_t entry[3]) {
    (void)m;
    uint32_t a[3];
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    a[0]=VA(g,0x4d2599u);db_call(g,0x492ee0u,a,1,&r);r.ecx=a[0];
    const uint32_t heading=r.eax;
    r.eax=speed;
    a[0]=o;a[1]=speed;a[2]=heading;db_call(g,0x408b74u,a,3,&r);
    return pair(r.eax,r.edx);
}
