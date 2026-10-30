#include "es2/core/map_pick.h"
#include "fixed.h"
/* HDD map pick (docs/reverse-engineering/cockpit.md, "map
 * pick 44d888"). Every guest read and write is done in the original's
 * order, so actor records or a view object that overlap the stored point
 * map+374h..+37Fh read what the original reads. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define W32(a,v) m->write32(m->context,(a),(v))
#define VA(x) (image+(uint32_t)(x)-0x400000u)
static int32_t s32(uint32_t x) { return (int32_t)fx_s32(x); }
/* MOV AX,5 / SHL AX,CL / MOVSX / SAR 1: the 16-bit shift is masked to five
 * bits, counts 16..31 shift every bit out. */
static uint32_t half_size(uint32_t word) {
    unsigned count=word&31u;
    uint32_t ax=count<16u?(5u<<count)&0xffffu:0u;
    return fx_sar((uint32_t)fx_s16(ax),1);
}

/* Original: DBSIM.EXE 0x44d288 (inlined): the point relative to the centre
 * of the map's screen rectangle map+71h..+80h; reads in the original's
 * order, the results stay in the caller's frame. */
static void rect_offset(const es2_guest_memory *m,uint32_t map,uint32_t point,uint32_t r[2]) {
    r[0]=R32(point);
    r[1]=R32(point+4u);
    r[0]-=R32(map+0x71u);
    uint32_t w=R32(map+0x79u);
    r[0]-=fx_sar(w-R32(map+0x71u),1);
    r[1]-=R32(map+0x75u);
    w=R32(map+0x7du);
    r[1]-=fx_sar(w-R32(map+0x75u),1);
}

/* Original: DBSIM.EXE 0x44d888 */
uint64_t es2_map_pick_registers(const es2_guest_memory *m,uint32_t image,uint32_t map,uint32_t point) {
    uint32_t r[2];
    rect_offset(m,map,point,r);
    r[1]=0u-r[1];
    /* Scale map+20h and shift count (low byte of the view's dword +1Ah). */
    uint32_t view=R32(map+0x14u),k=R32(map+0x20u),cl=R32(view+0x1au);
    W32(map+0x374u,fx_sar(r[0]*k,cl));
    W32(map+0x378u,fx_sar(r[1]*k,cl));
    W32(map+0x37cu,0);
    /* Plus the map centre map+18h..+23h, in place, in the order z, x, y. */
    uint32_t c=R32(map+0x20u);W32(map+0x37cu,R32(map+0x37cu)+c);
    c=R32(map+0x18u);W32(map+0x374u,R32(map+0x374u)+c);
    c=R32(map+0x1cu);W32(map+0x378u,R32(map+0x378u)+c);
    /* The pick square: the world x of the point moved left by half of
     * 5 << [4d253a]; h is its distance from the stored x. The matching y
     * and z of the original are never used. */
    r[0]-=half_size(R16(VA(0x4d253au)));
    (void)R16(VA(0x4d253cu));
    uint32_t left=fx_sar(r[0]*k,cl);
    (void)R32(map+0x20u);
    uint32_t cursor=R32(VA(0x4a9af8u)),edx=cursor;
    left+=R32(map+0x18u);
    (void)R32(map+0x1cu);
    /* The actor walk: a signed word counter against [4a9afe], a null entry
     * ends it. */
    uint16_t counter=0;
    for(;;) {
        uint16_t old=counter++;
        edx=(edx&0xffff0000u)|old;
        if(!(fx_s16(old)<fx_s16(R16(VA(0x4a9afeu)))))break;
        uint32_t actor=R32(cursor);cursor+=4u;
        if(!actor)break;
        /* Bounds x-h, y-h, x+h, y+h (wrapping), then four signed tests. */
        uint32_t h=R32(map+0x374u)-left;
        uint32_t x=R32(actor+0x26u);
        uint32_t below=R32(actor+0x2au)-h;
        uint32_t above=h+R32(actor+0x2au);
        edx=above;
        uint32_t px=R32(map+0x374u);
        if(!(s32(px)>s32(x-h)))continue;
        edx=R32(map+0x378u);
        if(!(s32(edx)>s32(below)))continue;
        if(!(s32(px)<s32(x+h)))continue;
        if(!(s32(R32(map+0x378u))<s32(above)))continue;
        return (uint64_t)edx<<32|actor;
    }
    return (uint64_t)edx<<32;
}

/* The HDD map click 44d6e0 (docs/reverse-engineering/cockpit.md, "map click
 * 44d6e0"). The map pick 44d888, the sound 462830 and the label update 44dd14
 * are called through g->call with the original's entry EAX/EDX/ECX (the first
 * two are hooks of their own and keep their modes). Memory is read again
 * wherever the original reads it, so record stores that alias M+5Dh, M+105h or
 * the stored point are seen as by the original. */
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define GVA(x) (g->image+(uint32_t)(x)-0x400000u)
static void mc_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,GVA(va),a,n,r);
}
/* MOVSX r32, byte. */
static uint32_t sx8(uint32_t b) { b&=0xffu; return b<0x80u?b:b|0xffffff00u; }

/* Original: DBSIM.EXE 0x44d6e0 */
uint64_t es2_map_click_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t map,const uint32_t entry[3]) {
    /* 44d6e7: map_pick(M, M+11Dh) with EAX = M+11Dh, EDX/ECX from entry. */
    const uint32_t pick[2]={map,map+0x11du};
    es2_weapon_registers r={map+0x11du,entry[1],entry[2]};
    mc_call(g,0x44d888u,pick,2,&r);
    const uint32_t actor=r.eax;
    /* 44d6f7: mode M+5Dh: 0Bh/0Ch the actor tests, 0Dh/0Eh the point,
     * else nothing. EDX keeps the subtractions. */
    const uint32_t mode=R32(map+0x5du);
    uint32_t edx=mode-0xbu-2u;
    int path=0; /* 0 nothing, 1 store the actor, 2 store the point */
    if(mode-0xbu<2u) {
        if(actor) {
            /* 44d718: side byte [[T+45h]+12h]; mode 0Ch wants side 0, mode
             * 0Bh side 1 (16-bit compares of the sign-extended byte). */
            const uint32_t sides=R32(actor+0x45u);
            const uint32_t again=R32(map+0x5du);
            edx=sx8(R8(sides+0x12u));
            if(again==0xcu&&!(edx&0xffffu))path=1;
            else if(R32(map+0x5du)==0xbu&&(edx&0xffffu)==1u)path=1;
        }
        /* 44d79e: mode 0Bh without a fitting actor does nothing; 0Ch
         * stores the point. */
        if(!path&&R32(map+0x5du)!=0xbu)path=2;
    } else {
        edx-=2u;
        if(mode-0xdu<2u)path=2;
    }
    if(path==1) {
        /* 44d736: M+15Ch, record +1Ch = T, kind +26h = 1, +21h = 1; the
         * index M+105h is read for every store. */
        W32(map+0x15cu,actor);
        const uint32_t i1=R32(map+0x105u);
        W32(map+42u*i1+0xa1u,actor);
        const uint32_t i2=R32(map+0x105u);
        W32(map+42u*i2+0xabu,1u);
        const uint32_t i3=R32(map+0x105u);
        W8(map+42u*i3+0xa6u,1u);
        W8(map+0x104u,1u);
        r.eax=21u*i3;r.edx=i3;r.ecx=i2;
    } else if(path==2) {
        /* 44d7a4: the stored point M+374h..+37Fh into record +0Ch..+17h
         * (dwords +8, +0, +4, each read then written), kind +26h = 0,
         * +21h = 1. */
        const uint32_t point=map+0x374u;
        const uint32_t i1=R32(map+0x105u),record=42u*i1+map+0x91u;
        W32(record+8u,R32(point+8u));W32(record,R32(point));W32(record+4u,R32(point+4u));
        const uint32_t i2=R32(map+0x105u);
        W32(map+42u*i2+0xabu,0u);
        const uint32_t i3=R32(map+0x105u);
        W8(map+42u*i3+0xa6u,1u);
        W8(map+0x104u,1u);
        r.eax=21u*i3;r.edx=i3;r.ecx=0u;
    }
    uint32_t eax=actor;
    if(path) {
        /* 44d78d/44d809: sound row 14h, then 44dd14(M) with the sound's
         * EAX/EDX and ECX = 14h (POP ECX); EDX:EAX are 44dd14's. */
        const uint32_t row[1]={0x14u},self[1]={map};
        mc_call(g,0x462830u,row,1,&r);
        r.ecx=0x14u;
        mc_call(g,0x44dd14u,self,1,&r);
        eax=r.eax;edx=r.edx;
    }
    /* 44d818: click handled, map redraw flag 49d6ae. */
    W8(map+0x11cu,0u);
    W8(GVA(0x49d6aeu),1u);
    return (uint64_t)edx<<32|eax;
}
