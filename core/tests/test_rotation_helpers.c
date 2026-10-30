#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/matrix.h"
/* Rotation helpers 47f058 and 47f4f4 against a reference written
 * independently of the core's loops: out(c,r) = sum over k of a(c,k) *
 * b(k,r) with the words at the layout offsets below, the sum wrapped to 32
 * bits, + 2000h, arithmetic shift by 14; per mode pair the terms, the
 * copied words and the constants of the listing. */
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"rotation_helpers line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint32_t state=0x9e3779b9u;
static uint32_t next(void){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
static uint16_t r16(const uint8_t *p){return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static void w16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static int32_t s16(uint16_t w){return (int16_t)w;}
static unsigned at(unsigned c,unsigned r){static const unsigned o[3][3]={{0,2,8},{4,6,10},{12,14,16}};return o[c][r];}
static uint16_t word(void){
    static const uint16_t edges[]={0,1,0xffff,0x4000,0xc000,0x7fff,0x8000,0x8001,0x2000,0xe000,0x1fff};
    return next()%2?edges[next()%11]:(uint16_t)next();
}
static uint8_t mode(void){static const uint8_t m[]={0,1,2,3,0x7f,0x80,0xff,0x81};return m[next()%8];}
/* (sum + 2000h) SAR 14 of a wrapped sum. */
static uint16_t q14(uint32_t sum){int32_t s=(int32_t)(sum+0x2000u);return (uint16_t)(s<0?~(~s>>14):s>>14);}
/* Reference: expected out bytes, EDX and ECX (inputs apart from out). */
static void reference(const uint8_t *a,const uint8_t *b,uint8_t *out,uint32_t edx,int place,uint32_t *redx,uint32_t *recx){
    if(place)memcpy(out+20,b+20,12);
    const int8_t ma=(int8_t)a[18],mb=(int8_t)b[18];
    if(ma<1||mb<1){memcpy(out,ma<1?b:a,20);*redx=edx;*recx=0;return;}
    const unsigned cols=ma==1?2:3,rows=mb==1?2:3,terms=ma==1||mb==1?2:3;
    for(unsigned c=0;c<cols;++c)for(unsigned r=0;r<rows;++r){
        uint32_t sum=0;
        for(unsigned k=0;k<terms;++k)sum+=(uint32_t)(s16(r16(a+at(c,k)))*s16(r16(b+at(k,r))));
        w16(out+at(c,r),q14(sum));
    }
    const unsigned lc=cols-1,lr=rows-1,lk=terms-1;
    *redx=(uint32_t)(s16(r16(a+at(lc,lk)))*s16(r16(b+at(lk,lr))));*recx=(uint32_t)s16(r16(b+at(lk,lr)));
    if(ma==1&&mb==1){for(unsigned i=8;i<16;i+=2)w16(out+i,0);w16(out+16,0x4000);out[18]=1;return;}
    if(mb==1){w16(out+8,r16(a+8));w16(out+10,r16(a+10));w16(out+16,r16(a+16));}
    if(ma==1){w16(out+12,r16(b+12));w16(out+14,r16(b+14));w16(out+16,r16(b+16));}
    out[18]=2;
}
static void fill(uint8_t *m){for(unsigned i=0;i<32;i+=2)w16(m+i,word());m[18]=mode();m[19]=(uint8_t)next();}
static void test_random(int place){
    uint8_t a[32],b[32],out[32],want[32];unsigned paths[6]={0};
    for(unsigned n=0;n<400000;++n){
        fill(a);fill(b);for(unsigned i=0;i<32;++i)out[i]=(uint8_t)next();memcpy(want,out,32);
        uint32_t entry=next(),ecx=next(),wedx,wecx;
        reference(a,b,want,entry,place,&wedx,&wecx);
        uint32_t edx=place?es2_matrix_rotation_place_registers(a,b,out,entry,&ecx):es2_matrix_rotation_compose_registers(a,b,out,entry,&ecx);
        CHECK(!memcmp(out,want,32)&&edx==wedx&&ecx==wecx);
        const int8_t ma=(int8_t)a[18],mb=(int8_t)b[18];
        ++paths[ma<1?0:mb<1?1:ma==1&&mb==1?2:mb==1?3:ma==1?4:5];
    }
    for(unsigned i=0;i<6;++i)CHECK(paths[i]>1000);
}
static void test_fixed(void){
    uint8_t a[32],b[32],out[32];uint32_t ecx=7;
    /* Identity a: b's 20 bytes, byte 19 included; translation untouched by 47f058. */
    memset(a,0,32);for(unsigned i=0;i<32;++i){b[i]=(uint8_t)(0x40+i);out[i]=0xee;}
    a[18]=0;CHECK(es2_matrix_rotation_compose_registers(a,b,out,0x1234u,&ecx)==0x1234u&&ecx==0);
    CHECK(!memcmp(out,b,20)&&out[20]==0xee&&out[31]==0xee);
    /* 47f4f4: the translation of b, then the copy of a (b's mode 0x80 < 1). */
    for(unsigned i=0;i<32;++i){a[i]=(uint8_t)(0x90+i);out[i]=0xee;}a[18]=1;b[18]=0x80;
    CHECK(es2_matrix_rotation_place_registers(a,b,out,5,&ecx)==5&&ecx==0&&!memcmp(out,a,20)&&!memcmp(out+20,b+20,12));
    /* Planar x planar: 90-degree rotations, 2x2 block, zeros, 4000h, mode 1; byte 19 kept. */
    memset(a,0,32);memset(b,0,32);memset(out,0x5a,32);
    w16(a,0);w16(a+2,0x4000);w16(a+4,0xc000);w16(a+6,0);a[18]=1;
    w16(b,0);w16(b+2,0x4000);w16(b+4,0xc000);w16(b+6,0);b[18]=1;
    uint32_t edx=es2_matrix_rotation_compose_registers(a,b,out,0,&ecx);
    CHECK(r16(out)==0xc000&&r16(out+2)==0&&r16(out+4)==0&&r16(out+6)==0xc000);
    CHECK(r16(out+8)==0&&r16(out+14)==0&&r16(out+16)==0x4000&&out[18]==1&&out[19]==0x5a&&out[20]==0x5a);
    CHECK(edx==0&&ecx==0);
    /* The general sum wraps: three 8000h x 8000h products = C0000000h. */
    for(unsigned i=0;i<18;i+=2){w16(a+i,0x8000);w16(b+i,0x8000);}a[18]=2;b[18]=3;
    edx=es2_matrix_rotation_compose_registers(a,b,out,0,&ecx);
    CHECK(r16(out+16)==q14(0xc0000000u)&&r16(out+16)==0&&edx==0x40000000u&&ecx==0xffff8000u&&out[18]==2);
    /* Rounding: 1 x 2000h + 2000h = 4000h -> 1; -1 x 2000h + 2000h = 0. */
    memset(a,0,32);memset(b,0,32);a[18]=b[18]=1;w16(a,1);w16(b,0x2000);w16(a+4,0xffff);
    es2_matrix_rotation_compose_registers(a,b,out,0,&ecx);
    CHECK(r16(out)==1&&r16(out+4)==0);
    /* In place: out = a; dots reread a after earlier stores. */
    uint8_t m[32];for(unsigned i=0;i<18;i+=2){w16(m+i,word());w16(b+i,word());}m[18]=2;b[18]=2;m[19]=0x33;
    uint8_t copy[32];memcpy(copy,m,32);uint32_t c2;
    /* Reference with the stores feeding later reads: step through in the listing's order. */
    uint32_t sum;
    for(unsigned c=0;c<3;++c)for(unsigned r=0;r<3;++r){
        sum=0;for(unsigned k=0;k<3;++k)sum+=(uint32_t)(s16(r16(copy+at(c,k)))*s16(r16(b+at(k,r))));
        w16(copy+at(c,r),q14(sum));
    }
    copy[18]=2;
    const uint32_t e2=(uint32_t)(s16(r16(m+16))*s16(r16(b+16)));
    edx=es2_matrix_rotation_compose_registers(m,b,m,0,&c2);
    CHECK(!memcmp(m,copy,32)&&edx==e2&&c2==(uint32_t)s16(r16(b+16)));
}
int main(void){
    test_fixed();test_random(0);test_random(1);
    printf("rotation_helpers: %u checks\n",checks);return 0;
}
