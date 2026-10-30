#include "es2/core/movement.h"
#include "es2/core/divide.h"
#include "fixed.h"

static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static uint16_t read16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8)); }
static void write32(uint8_t *p,uint32_t x) { for(unsigned i=0;i<4;++i) p[i]=(uint8_t)(x>>(8*i)); }
static int32_t signed16(uint16_t x) { return x<0x8000u ? (int32_t)x : (int32_t)x-65536; }
static int64_t signed32(uint32_t x) { return x<0x80000000u ? (int64_t)x : (int64_t)x-INT64_C(4294967296); }

uint32_t es2_heading_cos(uint16_t angle,const void *quarter_table) {
    int32_t rounded=signed16(angle)+8;
    int32_t index=rounded>=0 ? rounded/16 : -((-rounded+15)/16);
    if(index<0) index=-index;
    unsigned negative=index>=1024;
    if(negative) index=2048-index;
    uint32_t value=read16((const uint8_t *)quarter_table+2*(unsigned)index);
    return negative ? 0u-value : value;
}
uint32_t es2_heading_sin(uint16_t angle,const void *quarter_table) {
    return es2_heading_cos((uint16_t)(angle-0x4000u),quarter_table);
}
uint32_t es2_multiply_q8(uint32_t a,uint32_t b) {
    return (uint32_t)((uint64_t)(signed32(a)*signed32(b))>>8);
}
uint32_t es2_movement_step(uint16_t value,uint16_t scale) {
    uint32_t bits=es2_multiply_q8((uint32_t)signed16(scale),(uint32_t)signed16(value));
    int64_t n=signed32(bits);
    return (uint32_t)(n < -32767 ? -32767 : n > 32767 ? 32767 : n);
}
uint32_t es2_hud_speed(const void *actor,const void *config) {
    const uint8_t *a=actor,*c=config;
    int64_t maximum=signed16(read16(c+0xc2));
    if(!read16(c+0x50))
        return es2_divide_or_raise((uint64_t)(signed16(read16(a+0x28e))*maximum),(uint32_t)signed16(read16(c+6)),1,NULL);
    uint32_t divisor_bits=read32(c+0x206);
    uint32_t absolute=(divisor_bits&0x80000000u) ? 0u-divisor_bits : divisor_bits;
    int64_t bias=signed32(absolute-1u);
    if(maximum<0) bias=-bias;
    return es2_divide_or_raise((uint64_t)(signed32(read32(a+0x2bd))*maximum+bias),divisor_bits,1,NULL);   /* 0x47df48's IDIV */
}
void es2_transform_point(const void *input,const void *matrix,void *output) {
    const uint8_t *p=input,*m=matrix; uint8_t *q=output;
    static const unsigned coefficients[3][3]={{0,4,12},{2,6,14},{8,10,16}};
    int mode=m[18]<128 ? (int)m[18] : (int)m[18]-256;
    for(unsigned row=0;row<3;++row) {
        uint32_t value;
        if(mode<1 || (mode==1 && row==2)) value=read32(p+4*row);
        else {
            int64_t sum=8192;
            unsigned count=mode==1 ? 2 : 3;
            for(unsigned col=0;col<count;++col)
                sum+=signed32(read32(p+4*col))*signed16(read16(m+coefficients[row][col]));
            value=(uint32_t)((uint64_t)sum>>14);
        }
        write32(q+4*row,value+read32(m+20+4*row));
    }
}
void es2_vector_subtract(void *destination,const void *source) {
    uint8_t *p=destination; const uint8_t *q=source;
    write32(p,read32(p)-read32(q));
    write32(p+4,read32(p+4)-read32(q+4));
}
