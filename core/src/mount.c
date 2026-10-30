#include "es2/core/mount.h"
#include "es2/core/movement.h"
#include "fixed.h"

/* Trig leaves 47eb30/47ffe8 and the weapon mount helper 40f540/40f904.
 * (player_movement.md, section "trig leaves 47eb30 and 47ffe8", and
 * weapons.md). */
static uint64_t pair(uint32_t a,uint32_t d) { return (uint64_t)d<<32|a; }
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }

/* Original: DBSIM.EXE 0x47eb30. The two table lookups are heading_cos and
 * heading_sin; EDX is the sign of the second rounded index (CDQ). */
uint64_t es2_sincos_pair_registers(const es2_guest_memory *m,uint32_t angle,uint32_t matrix,const void *quarter) {
    const uint32_t c=es2_heading_cos((uint16_t)angle,quarter);
    W16(matrix,c);W16(matrix+6u,c);
    const uint32_t s=es2_heading_sin((uint16_t)angle,quarter);
    W16(matrix+2u,s);W16(matrix+4u,0u-s);
    const int32_t shifted=fx_s16((uint16_t)angle-0x4000u);
    return pair(0u-s,shifted< -8?0xffffffffu:0);
}

/* One row of 47ffe8: s32 x * s16 a + s32 y * s16 b + 2000h on 64 bits. */
static uint64_t row(uint32_t x,uint32_t a,uint32_t y,uint32_t b) {
    return (uint64_t)(fx_s32(x)*fx_s16(a))+(uint64_t)(fx_s32(y)*fx_s16(b))+0x2000u;
}
/* Original: DBSIM.EXE 0x47ffe8. v and m are read again after the first
 * store, as in the original (out may overlap them). */
uint64_t es2_rotate_2d_registers(const es2_guest_memory *m,uint32_t v,uint32_t matrix,uint32_t out) {
    const uint32_t x=R32(v),y=R32(v+4u);
    uint64_t sum=row(x,R16(matrix),y,R16(matrix+4u));
    W32(out,(uint32_t)(sum>>14));
    sum=row(R32(v),R16(matrix+2u),R32(v+4u),R16(matrix+6u));
    W32(out+4u,(uint32_t)(sum>>14));
    return pair(out,(uint32_t)(sum>>32));
}

/* Original: DBSIM.EXE 0x40f904. V = 4a9af0 (x, y, z words); the guard
 * byte 498f34 is set once. Word M+4Ah is read after V.x/V.y are stored. */
uint64_t es2_mount_base_registers(const es2_guest_memory *m,uint32_t image,uint32_t M,uint32_t side) {
    const uint32_t guard=image+0x98f34u,V=image+0xa9af0u;
    if(!R8(guard))W8(guard,1);
    uint32_t edx=R32(M+0x46u);W32(V,edx);
    edx=lo(edx,R16(M+0x4au));W16(V+4u,edx);
    W16(V+2u,0);
    switch(fx_s16(side)) {
    case 1:W16(V+4u,0u-R16(V+4u));W16(V,0);break;
    case 2:W16(V,0u-R16(V));W16(V+4u,0);break;
    case 3:W16(V+4u,0);break;
    case 0:W16(V,0);break;
    case 4:W16(V+4u,0);W16(V,0);break;
    default:break;
    }
    return pair(V,edx);
}

/* Original: DBSIM.EXE 0x40f540. 40f904 goes through the graph's guest
 * bridge; its EAX is the vector the offsets are added to. */
uint64_t es2_mount_offset_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t W,
    const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[2];
    const uint32_t definition=R32(W+0x18u);r.eax=definition;
    r.edx=(uint32_t)(int32_t)(int8_t)R8(r.eax+6u);a[1]=r.edx;
    r.ecx=R32(W+0x1cu);a[0]=r.ecx;
    const uint32_t target=g->image+0x40f904u-0x400000u;
    if(g->observe)g->observe(g->observer_context,target,0);
    g->call(g->context,target,a,2,&r);
    const uint32_t V=r.eax,C=R32(W+0x18u)+0x10u;
    W16(V+4u,R16(V+4u)+R16(C+4u));
    W16(V,R16(V)+R16(C));
    const uint32_t y=R16(C+2u);
    W16(V+2u,R16(V+2u)+y);
    return pair(V,lo(C,y));
}
