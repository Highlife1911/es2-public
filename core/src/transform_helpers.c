#include "es2/core/transform_helpers.h"
#include "fixed.h"

/* The enter-prologue helpers 47ff8c and 4801d4. (player_movement.md, section
 * "enter-prologue helpers 47ff8c and 4801d4"). */
static uint64_t pair(uint32_t a,uint32_t d) { return (uint64_t)d<<32|a; }
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
/* MOVSX of a word as a 32-bit register value. */
static uint32_t sx(uint16_t w) { return (uint32_t)fx_s16(w); }

/* Original: DBSIM.EXE 0x47ff8c. Each row rereads v and m after the
 * previous store (out may overlap them). IMUL keeps the low dwords. */
uint64_t es2_rotate_words_registers(const es2_guest_memory *m,uint32_t v,uint32_t M,uint32_t out,uint32_t *ecx) {
    uint32_t eax=sx(R16(v))*sx(R16(M)),edx=sx(R16(v+2u))*sx(R16(M+4u));
    W16(out,fx_sar(eax+edx+0x2000u,14));
    eax=sx(R16(v))*sx(R16(M+2u));
    const uint32_t m3=sx(R16(M+6u));edx=sx(R16(v+2u))*m3;
    W16(out+2u,fx_sar(eax+edx+0x2000u,14));
    *ecx=m3;
    return pair(out,edx);
}

/* One row of 4801d4: count products of the words v0, v1, v2 with the
 * matrix words at the given byte offsets, + 2000h, SAR 14, + the
 * translation dword; stored to out + 4 row. *last_edx / *last_ecx get the
 * registers of the last product. */
static void row(const es2_guest_memory *m,uint32_t v,uint32_t M,uint32_t out,unsigned r,const unsigned *offsets,
    unsigned count,uint32_t *last_edx,uint32_t *last_ecx) {
    uint32_t eax=sx(R16(v))*sx(R16(M+offsets[0]));
    for(unsigned k=1;k<count;++k) {
        const uint32_t c=sx(R16(M+offsets[k]));
        *last_edx=sx(R16(v+2u*k))*c;*last_ecx=c;
        eax+=*last_edx;
    }
    eax=fx_sar(eax+0x2000u,14)+R32(M+0x14u+4u*r);
    W32(out+4u*r,eax);
}

/* Original: DBSIM.EXE 0x4801d4. The mode byte is read once (CMP with 1,
 * JL/JE: signed). */
uint64_t es2_matrix_point_registers(const es2_guest_memory *m,uint32_t v,uint32_t M,uint32_t out,
    const uint32_t entry[3],uint32_t *ecx) {
    static const unsigned rows[3][3]={{0u,4u,0xcu},{2u,6u,0xeu},{8u,0xau,0x10u}};
    const int mode=(int8_t)*(const uint8_t *)m->host(m->context,M+0x12u);
    uint32_t edx=entry[1],c=entry[2],eax=0;
    if(mode<1) {
        /* 4802e3: translation only; EAX is the last stored dword. */
        for(unsigned r=0;r<3;++r) {
            eax=sx(R16(v+2u*r))+R32(M+0x14u+4u*r);
            W32(out+4u*r,eax);
        }
        *ecx=c;
        return pair(eax,edx);
    }
    if(mode==1) {
        /* 48028c: two products in rows 0 and 1; row 2 is z + T2. */
        row(m,v,M,out,0,rows[0],2,&edx,&c);
        row(m,v,M,out,1,rows[1],2,&edx,&c);
        W32(out+8u,sx(R16(v+4u))+R32(M+0x1cu));
    } else {
        for(unsigned r=0;r<3;++r)row(m,v,M,out,r,rows[r],3,&edx,&c);
    }
    *ecx=c;
    return pair(out,edx);
}
