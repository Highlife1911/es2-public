#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/transform_helpers.h"
/* Synthetic state for the cores: a small arena at a fixed guest
 * base. The expected values are written out per formula (32-bit wrap,
 * arithmetic shift), independently of the core's row loop. */
#define BASE UINT32_C(0x90000000)
static uint8_t arena[0x400];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"transform_helpers line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
static uint32_t state=0x12345678u;
static uint32_t next(void){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
static int32_t s16(uint16_t w){return (int16_t)w;}
/* (sum + 2000h) SAR 14 on a wrapped 32-bit sum. */
static uint32_t q14(uint32_t sum){int32_t s=(int32_t)(sum+0x2000u);return (uint32_t)(s<0?~(~s>>14):s>>14);}
static uint16_t word(void){
    static const uint16_t edges[]={0,1,0xffff,0x4000,0xc000,0x7fff,0x8000,0x8001,0x2000,0xe000};
    return next()%2?edges[next()%10]:(uint16_t)next();
}
static void test_rotate(void){
    const uint32_t v=BASE,M=BASE+0x10,out=BASE+0x20;
    /* Identity matrix: (x, y) unchanged; EAX = out, EDX = y * 4000h, ECX = 4000h. */
    w16(0,v,1000);w16(0,v+2,0xfc18);w16(0,M,0x4000);w16(0,M+2,0);w16(0,M+4,0);w16(0,M+6,0x4000);
    uint32_t ecx=0;uint64_t p=es2_rotate_words_registers(&memory,v,M,out,&ecx);
    CHECK(r16(0,out)==1000&&r16(0,out+2)==0xfc18&&(uint32_t)p==out&&(uint32_t)(p>>32)==(uint32_t)(-1000*0x4000)&&ecx==0x4000);
    /* Rounding: -1 * 2000h + 2000h = 0; -1 * 1fffh + 2000h = 1, SAR 14 = 0. */
    w16(0,v,0xffff);w16(0,v+2,0);w16(0,M,0x2000);w16(0,M+2,0x1fff);w16(0,M+6,0xffff);
    es2_rotate_words_registers(&memory,v,M,out,&ecx);
    CHECK(r16(0,out)==0&&r16(0,out+2)==0&&ecx==0xffffffffu);
    /* Random words against the formulas, out apart. */
    for(unsigned n=0;n<200000;++n){
        uint16_t x=word(),y=word(),m0=word(),m1=word(),m2=word(),m3=word();
        w16(0,v,x);w16(0,v+2,y);w16(0,M,m0);w16(0,M+2,m1);w16(0,M+4,m2);w16(0,M+6,m3);
        p=es2_rotate_words_registers(&memory,v,M,out,&ecx);
        uint32_t e0=q14((uint32_t)(s16(x)*s16(m0))+(uint32_t)(s16(y)*s16(m2)));
        uint32_t e1=q14((uint32_t)(s16(x)*s16(m1))+(uint32_t)(s16(y)*s16(m3)));
        CHECK(r16(0,out)==(uint16_t)e0&&r16(0,out+2)==(uint16_t)e1);
        CHECK((uint32_t)p==out&&(uint32_t)(p>>32)==(uint32_t)(s16(y)*s16(m3))&&ecx==(uint32_t)s16(m3));
    }
    /* The extreme sum wraps: 2 * 8000h * 8000h = 2^31. */
    w16(0,v,0x8000);w16(0,v+2,0x8000);w16(0,M,0x8000);w16(0,M+4,0x8000);
    es2_rotate_words_registers(&memory,v,M,out,&ecx);
    CHECK(r16(0,out)==(uint16_t)q14(0x80000000u));
    /* In place over v: the second row reads the new x. */
    w16(0,v,100);w16(0,v+2,200);w16(0,M,0);w16(0,M+2,0x4000);w16(0,M+4,0x4000);w16(0,M+6,0);
    es2_rotate_words_registers(&memory,v,M,v,&ecx);
    CHECK(r16(0,v)==200&&r16(0,v+2)==200);
    /* out over m3: the first store changes nothing read later except m. */
    w16(0,v,0x4000);w16(0,v+2,0x4000);w16(0,M,0x4000);w16(0,M+2,0);w16(0,M+4,0);w16(0,M+6,0x4000);
    es2_rotate_words_registers(&memory,v,M,M+2,&ecx);
    /* out0 = 4000h lands on m1; row 1 = 4000h * 4000h + 4000h * 4000h. */
    CHECK(r16(0,M+2)==0x4000&&r16(0,M+4)==(uint16_t)q14(0x4000u*0x4000u+0x4000u*0x4000u));
}
static void matrix(uint32_t M,const uint16_t w[9],uint8_t mode,const uint32_t t[3]){
    for(unsigned i=0;i<9;++i)w16(0,M+2*i,w[i]);
    arena[M-BASE+0x12]=mode;arena[M-BASE+0x13]=(uint8_t)next();
    for(unsigned i=0;i<3;++i)w32(0,M+0x14+4*i,t[i]);
}
static void test_point(void){
    const uint32_t v=BASE+0x100,M=BASE+0x120,out=BASE+0x160;
    static const uint8_t modes[]={0,1,2,3,0x7f,0x80,0xff,0xfe};
    for(unsigned n=0;n<300000;++n){
        uint16_t w[9],x=word(),y=word(),z=word();uint32_t t[3],entry[3],ecx=0;
        for(unsigned i=0;i<9;++i)w[i]=word();
        for(unsigned i=0;i<3;++i){t[i]=next()%4?next()%0x20000u-0x10000u:next();entry[i]=next();}
        uint8_t mode=next()%4?modes[next()%8]:(uint8_t)next();
        matrix(M,w,mode,t);w16(0,v,x);w16(0,v+2,y);w16(0,v+4,z);
        uint64_t p=es2_matrix_point_registers(&memory,v,M,out,entry,&ecx);
        int32_t X=s16(x),Y=s16(y),Z=s16(z);uint32_t e[3],eax,edx,c;
        if((int8_t)mode<1){
            e[0]=(uint32_t)X+t[0];e[1]=(uint32_t)Y+t[1];e[2]=(uint32_t)Z+t[2];eax=e[2];edx=entry[1];c=entry[2];
        } else if(mode==1){
            e[0]=q14((uint32_t)(X*s16(w[0]))+(uint32_t)(Y*s16(w[2])))+t[0];
            e[1]=q14((uint32_t)(X*s16(w[1]))+(uint32_t)(Y*s16(w[3])))+t[1];
            e[2]=(uint32_t)Z+t[2];eax=out;edx=(uint32_t)(Y*s16(w[3]));c=(uint32_t)s16(w[3]);
        } else {
            e[0]=q14((uint32_t)(X*s16(w[0]))+(uint32_t)(Y*s16(w[2]))+(uint32_t)(Z*s16(w[6])))+t[0];
            e[1]=q14((uint32_t)(X*s16(w[1]))+(uint32_t)(Y*s16(w[3]))+(uint32_t)(Z*s16(w[7])))+t[1];
            e[2]=q14((uint32_t)(X*s16(w[4]))+(uint32_t)(Y*s16(w[5]))+(uint32_t)(Z*s16(w[8])))+t[2];
            eax=out;edx=(uint32_t)(Z*s16(w[8]));c=(uint32_t)s16(w[8]);
        }
        CHECK(r32(0,out)==e[0]&&r32(0,out+4)==e[1]&&r32(0,out+8)==e[2]);
        CHECK((uint32_t)p==eax&&(uint32_t)(p>>32)==edx&&ecx==c);
    }
    /* In place over v (general): rows 1 and 2 read the new x, y words. */
    static const uint16_t id[9]={0x4000,0,0,0x4000,0,0,0,0,0x4000};static const uint32_t t0[3]={0,0,0};
    uint32_t entry[3]={1,2,3},ecx=0;
    matrix(M,id,2,t0);w16(0,v,10);w16(0,v+2,20);w16(0,v+4,30);
    es2_matrix_point_registers(&memory,v,M,v,entry,&ecx);
    /* out0 = 10 (dword over x, y = 10, 0); row 1 uses x = 10, y = 0 -> 0;
     * row 2 reads x, y from out1 (0, 0) and z = the high word of out1 = 0. */
    CHECK(r32(0,v)==10&&r32(0,v+4)==0&&r32(0,v+8)==0);
    /* The mode byte's high neighbour is not read as part of the mode. */
    matrix(M,id,0x80,t0);arena[M-BASE+0x13]=1;w16(0,v,5);w16(0,v+2,6);w16(0,v+4,7);
    uint64_t p=es2_matrix_point_registers(&memory,v,M,out,entry,&ecx);
    CHECK(r32(0,out)==5&&r32(0,out+4)==6&&r32(0,out+8)==7&&(uint32_t)p==7&&(uint32_t)(p>>32)==2&&ecx==3);
}
int main(void){
    test_rotate();test_point();
    printf("transform_helpers: %u checks\n",checks);
    return 0;
}
