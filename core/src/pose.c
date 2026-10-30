#include "es2/core/pose.h"
#include "fixed.h"
uint32_t es2_pose_interpolate_registers(const void *first,const void *second,uint32_t fraction,void *output) {
    const uint8_t *a=first,*b=second; uint8_t *q=output;
    uint32_t edx=0;
    for(unsigned i=0;i<12;i+=2) {
        /* Both angular shortest-difference (including the negative half-turn
         * tie) and translation subtraction have the same signed low word. */
        uint32_t delta=(uint32_t)fx_s16((uint32_t)fx_r16(b+i)-fx_r16(a+i));
        edx=(uint32_t)((uint64_t)(fx_s32(delta)*fx_s32(fraction))>>32);
        fx_w16(q+i,fx_sar(delta*fraction,10)+fx_r16(a+i));
    }
    return edx;
}
void es2_pose_interpolate(const void *a,const void *b,uint32_t f,void *o) { (void)es2_pose_interpolate_registers(a,b,f,o); }
uint64_t es2_scalar_interpolate_registers(uint32_t x,uint32_t x0,uint32_t x1,uint32_t y0,uint32_t y1) {
    uint32_t divisor=x1-x0,dy=y1-y0;
    uint32_t absolute=(divisor&UINT32_C(0x80000000)) ? 0u-divisor : divisor;
    int64_t bias=fx_s32(absolute-1u);
    /* The JGE follows SUB, so its signed comparison uses the pre-subtraction
     * operands, including overflow, rather than testing the wrapped sign. */
    if(fx_s32(y1)<fx_s32(y0)) bias=-bias;
    int64_t numerator=fx_s32(x-x0)*fx_s32(dy)+bias,den=fx_s32(divisor);
    return ((uint64_t)(uint32_t)(numerator%den)<<32)|((uint32_t)(numerator/den)+y0);
}
uint32_t es2_track_count(const void *manager) {
    const uint8_t *m=manager; unsigned count=0;
    while(count<10 && fx_r32(m+0x1a+4*count)) ++count;
    return count;
}

uint32_t es2_scalar_interpolate(uint32_t x,uint32_t x0,uint32_t x1,uint32_t y0,uint32_t y1) { return (uint32_t)es2_scalar_interpolate_registers(x,x0,x1,y0,y1); }

uint64_t es2_clip_pose_registers(const es2_guest_memory *m,uint32_t track,uint32_t clip,uint32_t frame) {
    void *c=m->context;
    uint32_t resource=m->read32(c,track);
    uint32_t selected=m->read32(c,m->read32(c,resource+6u)+4u*clip);
    if(!m->read16(c,selected+8u)) {
        m->write16(c,track+0x20u,0);
        return ((uint64_t)clip<<32)|track;
    }
    /* Index table and pose base are read before any write; the original's
     * extra reads of resource+6 at 0x478f16/0x478f26 discard their values. */
    uint32_t indices=m->read32(c,selected+0x16u);
    uint32_t index=frame*(uint32_t)fx_s16(m->read16(c,selected+0x10u));
    uint32_t pose=(uint32_t)fx_s16(m->read16(c,indices+2u*index));
    uint32_t offset=12u*pose;
    uint32_t base=m->read32(c,m->read32(c,track)+0x12u);
    m->write32(c,track+0x22u,m->read32(c,base+offset+6u));
    m->write16(c,track+0x26u,m->read16(c,base+offset+10u));
    /* Reread after the translation writes, as the original does. */
    base=m->read32(c,m->read32(c,track)+0x12u);
    m->write32(c,track+0x28u,m->read32(c,base+offset));
    m->write16(c,track+0x2cu,m->read16(c,base+offset+4u));
    m->write16(c,track+0x20u,1);
    return ((uint64_t)(3u*pose)<<32)|track;
}
