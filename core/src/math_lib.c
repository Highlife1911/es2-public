#include "es2/core/math_lib.h"
#include "fixed.h"
#define R32(a) m->read32(m->context,(a))
#define W32(a,v) m->write32(m->context,(a),(v))
#define VA(b,x) ((b)+(x)-0x400000u)
static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static int32_t s32(uint32_t x) { return (int32_t)fx_s32(x); }
/* 47e068: (a << 16) sign-extended to 64 bits, IDIV by b, low dword. A zero
 * divisor or a quotient outside 32 bits faults in the original and is
 * outside the contract. */
static uint32_t q16_div(uint32_t a,uint32_t b) { return (uint32_t)(int64_t)(fx_s32(a)*65536/fx_s32(b)); }
/* CDQ/IDIV quotient and remainder (truncation toward zero, remainder with
 * the dividend's sign); -2^31 / -1 faults in the original. */
static uint32_t idiv_quot(uint32_t a,uint32_t b) { return (uint32_t)(int64_t)(fx_s32(a)/fx_s32(b)); }
static uint32_t idiv_rest(uint32_t a,uint32_t b) { return (uint32_t)(int64_t)(fx_s32(a)%fx_s32(b)); }

/* Original: DBSIM.EXE 0x48aaac */
uint64_t es2_tex_edge_registers(const es2_guest_memory *m,uint32_t b,uint32_t e,uint32_t from,uint32_t edx) {
    const uint32_t end=VA(b,0x6c5f80u),count=VA(b,0x6c5f84u),screen=VA(b,0x6c5f88u),texture=VA(b,0x6c5f8cu);
    uint32_t to,dy;
    for(;;) {
        if(from==R32(end)) return pair(0,edx);
        to=R32(e)+from;
        uint32_t n=R32(count);
        if(!(s32(to)<s32(n))) to=0;
        else if(s32(to)<0) to=n-1u;
        uint32_t p=R32(screen);
        edx=R32(p+8u*from+4u);
        dy=R32(p+8u*to+4u)-edx;
        W32(e+4u,dy);
        if(dy) break;
        from=to;
    }
    const uint32_t scaled=dy<<16;
    W32(e+8u,to);
    W32(e+0x0cu,R32(R32(texture)+8u*from)<<16);
    W32(e+0x10u,R32(R32(texture)+8u*from+4u)<<16);
    uint32_t a=(R32(R32(texture)+8u*to)<<16)-R32(e+0x0cu);
    W32(e+0x14u,q16_div(a,scaled));
    a=(R32(R32(texture)+8u*to+4u)<<16)-R32(e+0x10u);
    W32(e+0x18u,q16_div(a,scaled));
    W32(e+0x1cu,R32(R32(screen)+8u*from));
    uint32_t p=R32(screen),x=R32(p+8u*from),dx=R32(p+8u*to)-x;
    if(s32(dx)<0) {
        W32(e+0x24u,0xffffffffu);
        dx=0u-dx;
        W32(e+0x28u,1u-R32(e+4u));
        W32(e+0x20u,0u-idiv_quot(dx,R32(e+4u)));
    } else {
        W32(e+0x24u,1u);
        W32(e+0x28u,0u);
        W32(e+0x20u,idiv_quot(dx,R32(e+4u)));
    }
    const uint32_t rest=idiv_rest(dx,R32(e+4u));
    W32(e+0x2cu,rest);
    W32(e+0x30u,R32(e+4u));
    return pair(1u,rest);
}
