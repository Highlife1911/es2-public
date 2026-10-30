#include "es2/core/trig.h"
#include "fixed.h"
uint32_t es2_inverse_sine(uint16_t value,const void *large_table,const void *small_table) {
    int32_t signed_value=fx_s16(value); uint32_t magnitude=(uint32_t)(signed_value<0?-signed_value:signed_value);
    unsigned index=(magnitude+2)/4;
    if(index<=3583) {
        uint32_t angle=fx_r16((const uint8_t *)small_table+2*((index+4)/8));
        return signed_value<0 ? 0u-angle : angle;
    }
    uint32_t angle=fx_r16((const uint8_t *)large_table+2*index);
    return signed_value<0 ? (uint16_t)(0u-angle) : angle;
}
static uint64_t ratio_numerator(uint32_t smaller,uint32_t larger) {
    uint64_t shifted=(uint64_t)fx_s32(smaller)<<9;
    /* The original ADD of half the divisor does not carry into EDX. */
    return (shifted&UINT64_C(0xffffffff00000000)) | ((uint32_t)shifted+fx_sar(larger,1));
}
int es2_angle_ratio_valid(uint32_t x,uint32_t y) {
    uint32_t a=(x&UINT32_C(0x80000000))?0u-x:x,b=(y&UINT32_C(0x80000000))?0u-y:y;
    if(!b) return 1;
    if(fx_s32(b)>fx_s32(a)) { uint32_t t=a; a=b; b=t; }
    if(!a) return 0;
    uint64_t numerator=ratio_numerator(b,a);
    return (numerator>>32)<a && numerator/a<=512;
}
uint64_t es2_angle_ratio_registers(uint32_t x,uint32_t y,const void *corrections) {
    uint32_t a=(x&UINT32_C(0x80000000))?0u-x:x,b=(y&UINT32_C(0x80000000))?0u-y:y,angle=0;
    uint32_t edx=0;
    if(b) {
        int exchange=fx_s32(b)>fx_s32(a);
        uint32_t divisor=exchange?b:a,small=exchange?a:b;
        uint64_t numerator=ratio_numerator(small,divisor);
        uint32_t index=(uint32_t)(numerator/divisor);
        edx=index?((const uint8_t *)corrections)[index]:(uint32_t)(numerator%divisor);
        angle=index ? 4*index+((const uint8_t *)corrections)[index] : 0;
        if(exchange) angle=4096-angle;
    }
    if(x&UINT32_C(0x80000000)) angle=8192-angle;
    if(y&UINT32_C(0x80000000)) angle=16384-angle;
    return ((uint64_t)edx<<32)|((angle<<2)&65535);
}
uint32_t es2_matrix_angles_registers(const void *matrix,void *angles,const void *large,const void *small,const void *corrections) {
    const uint8_t *m=matrix; uint8_t *a=angles;
    uint32_t pitch=es2_inverse_sine(fx_r16(m+10),large,small); fx_w16(a,pitch);
    uint64_t last;
    if((uint16_t)pitch==16384 || (uint16_t)pitch==49152) {
        fx_w16(a+2,0);
        last=es2_angle_ratio_registers((uint32_t)fx_s16(fx_r16(m)),(uint32_t)fx_s16(fx_r16(m+2)),corrections);
        fx_w16(a+4,(uint32_t)last);
    } else {
        fx_w16(a+2,es2_angle_ratio((uint32_t)fx_s16(fx_r16(m+16)),0u-(uint32_t)fx_s16(fx_r16(m+8)),corrections));
        last=es2_angle_ratio_registers((uint32_t)fx_s16(fx_r16(m+6)),0u-(uint32_t)fx_s16(fx_r16(m+4)),corrections);
        fx_w16(a+4,(uint32_t)last);
    }
    return (uint32_t)(last>>32);
}

uint32_t es2_angle_ratio(uint32_t x,uint32_t y,const void *c) { return (uint32_t)es2_angle_ratio_registers(x,y,c); }
void es2_matrix_angles(const void *m,void *a,const void *l,const void *s,const void *c) { (void)es2_matrix_angles_registers(m,a,l,s,c); }
