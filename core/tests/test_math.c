#include "es2/core/matrix.h"
#include "es2/core/pose.h"
#include "es2/core/trig.h"
#include "es2/core/energy.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"math line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static void w16(uint8_t *p,uint32_t x) {p[0]=(uint8_t)x;p[1]=(uint8_t)(x>>8);}
static void w32(uint8_t *p,uint32_t x) {for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(x>>(8*i));}
static uint16_t r16(const uint8_t *p) {return (uint16_t)((uint16_t)p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
int main(void) {
    uint8_t table[2050],large[16386],small[898],correction[513];
    /* Synthetic linear ramps deliberately differ from the game's tables. */
    for(unsigned i=0;i<=1024;++i)w16(table+2*i,16384-16*i);
    for(unsigned i=0;i<=8192;++i)w16(large+2*i,i);
    for(unsigned i=0;i<=448;++i)w16(small+2*i,3*i);
    for(unsigned i=0;i<=512;++i)correction[i]=(uint8_t)(i%17);
    uint8_t angles[12]={0},m[32],q[32],copy[32],out[32],point[12];
    memset(m,0xa5,sizeof(m));es2_matrix_euler(angles,m,table);
    CHECK(m[18]==0&&m[19]==0xa5&&m[20]==0xa5);
    CHECK(r16(m)==16384&&r16(m+6)==16384&&r16(m+16)==16384);
    CHECK(es2_matrix_euler_registers(angles,m,table,0xabcdef01)==0xabcdef01);
    for(unsigned n=0;n<65536;++n) {
        w16(angles+4,n);es2_matrix_euler(angles,m,table);
        memcpy(copy,m,32);es2_matrix_transpose(m);es2_matrix_transpose(m);CHECK(!memcmp(m,copy,32));
    }
    memset(angles,0,sizeof(angles));es2_matrix_euler(angles,m,table);memset(m+20,0,12);
    memcpy(q,m,32);w32(q+20,10);w32(q+24,20);w32(q+28,30);memset(out,0,32);
    es2_matrix_compose(m,q,out);CHECK(!memcmp(out,q,32));
    w32(point,0x80000000u);w32(point+4,0x7fffffffu);w32(point+8,0xffffffffu);
    es2_matrix_rotate(point,m,point);CHECK(r32(point)==0x80000000u&&r32(point+4)==0x7fffffffu&&r32(point+8)==0xffffffffu);
    uint8_t first[12]={0},second[12]={0},pose[12];w16(second,0x8000);w16(second+6,1024);
    es2_pose_interpolate(first,second,512,pose);CHECK(r16(pose)==0xc000&&r16(pose+6)==512);
    es2_pose_interpolate(first,second,1024,pose);CHECK(!memcmp(pose,second,12));
    CHECK(es2_scalar_interpolate(3,0,10,0,100)==30);
    CHECK(es2_scalar_interpolate(1,0,3,0,10)==4);
    CHECK(es2_scalar_interpolate(1,0,3,10,0)==6);
    CHECK(es2_scalar_interpolate(0x7fffffffu,0,1,0,1)==0x7fffffffu);
    CHECK(es2_scalar_interpolate(0x80000000u,0,1,0,1)==0x80000000u);
    CHECK(es2_inverse_sine(0,large,small)==0);
    CHECK(es2_inverse_sine(0x8000,large,small)==57344);
    CHECK(es2_inverse_sine(16384,large,small)==4096);
    CHECK(es2_inverse_sine(65504,large,small)==0xfffffffdu);
    CHECK(es2_angle_ratio(1,0,correction)==0);
    CHECK(es2_angle_ratio(0,1,correction)==16384);
    CHECK(es2_angle_ratio(0xffffffffu,0,correction)==32768);
    CHECK(es2_angle_ratio(0,0xffffffffu,correction)==49152);
    CHECK(es2_angle_ratio_valid(0x80000000u,0));CHECK(!es2_angle_ratio_valid(0x80000000u,1));CHECK(!es2_angle_ratio_valid(1,0x80000000u));
    CHECK(es2_multiply_q10(0xffffffffu,1)==0xffffffffu);
    CHECK(es2_multiply_q10(1024,1024)==1024);
    CHECK(es2_multiply_q10_registers(0xffffffffu,1)==UINT64_C(0xffffffffffffffff));
    uint8_t state[8]={0};w16(state,100);CHECK(es2_approach_word(state,90,65)==0&&r16(state)==90);
    w16(state,0);CHECK(es2_approach_word(state,0x12340064,1)==0x12340063&&r16(state)==1);
    w16(state,0x8000);CHECK(es2_approach_word(state,0x7fff,1)==65534&&r16(state)==0x8001);
    for(unsigned request=0;request<1000;++request) {
        w16(state,100);w16(state+2,200);w16(state+4,512);w16(state+6,1000);
        uint32_t result=es2_energy_accumulate(state,(uint16_t)request);unsigned accepted=request<5?request:5;
        CHECK((uint16_t)(r16(state)+r16(state+2))==300+accepted);
        CHECK((uint16_t)result==request-accepted);
    }
    puts("PASS: synthetic matrices, aliases, interpolation, trig domains, full registers and energy invariants");printf("%u checks\n",checks);return 0;
}
