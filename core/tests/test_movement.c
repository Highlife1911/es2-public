#include "es2/core/movement.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c) do { ++checks; if(!(c)) { fprintf(stderr,"movement line %d: %s\n",__LINE__,#c); return 1; } } while(0)
static unsigned checks;
static void put(uint8_t *p,uint32_t x,unsigned n) { for(unsigned i=0;i<n;++i) p[i]=(uint8_t)(x>>(8*i)); }
int main(void) {
    uint8_t table[2050]; for(unsigned i=0;i<=1024;++i) put(table+2*i,17000-i,2);
    CHECK(es2_heading_cos(0,table)==17000); CHECK(es2_heading_cos(7,table)==17000);
    CHECK(es2_heading_cos(8,table)==16999); CHECK(es2_heading_cos(0xfff8,table)==17000);
    CHECK(es2_heading_cos(0xfff7,table)==16999); CHECK(es2_heading_cos(0x4000,table)==0u-15976u);
    for(unsigned a=0;a<65536;++a) CHECK(es2_heading_sin((uint16_t)a,table)==es2_heading_cos((uint16_t)(a-16384),table));
    CHECK(es2_multiply_q8(0xffffffffu,1)==0xffffffffu);
    CHECK(es2_multiply_q8(0x80000000u,0x80000000u)==0);
    CHECK(es2_movement_step(0x8000,256)==0xffff8001u);
    CHECK(es2_movement_step(0x7fff,0x7fff)==32767);
    uint8_t a[0x2c1]={0},c[0x20a]={0}; put(a+0x28e,0xfffd,2); put(c+0xc2,10,2); put(c+6,4,2);
    CHECK(es2_hud_speed(a,c)==0u-7u); c[0x50]=1; put(a+0x2bd,3,4); put(c+0x206,4,4);
    CHECK(es2_hud_speed(a,c)==8); put(c+0xc2,0xfff6,2); CHECK(es2_hud_speed(a,c)==0u-8u);
    uint8_t matrix[32]={0},p[16]={0}; put(p,1,4); put(p+4,2,4); put(p+8,3,4);
    put(matrix+20,4,4); put(matrix+24,5,4); put(matrix+28,6,4);
    es2_transform_point(p,matrix,p); CHECK(p[0]==5 && p[4]==7 && p[8]==9);
    /* Partial alias: the Y read observes the earlier X write. */
    es2_transform_point(p,matrix,p+4); CHECK(p[4]==9 && p[8]==14 && p[12]==20);
    memset(matrix,0,32); matrix[18]=2; put(matrix,0x4000,2); put(matrix+6,0x4000,2); put(matrix+16,0x4000,2);
    es2_transform_point(p,matrix,p); CHECK(p[0]==5 && p[4]==9 && p[8]==14);
    es2_vector_subtract(p+4,p); CHECK(p[4]==4 && p[8]==10);
    printf("PASS: movement %u checks\n",checks); return 0;
}
