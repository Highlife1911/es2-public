#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/mount.h"
/* Synthetic state for the cores. The image base is placed so that the
 * guard byte 498f34 and the static vector 4a9af0 fall inside the arena;
 * objects live above them. The quarter table is synthetic and monotone
 * (16384 - 8i), not the game's. 40f904 resolves to the mount_base core. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE-0x98000u)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define OBJECTS (BASE+0x20000u)
static uint8_t arena[0x24000],table[2050];static unsigned checks,calls;
static uint32_t seen_args[2],seen_regs[3];
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"mount line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(f==VA(0x40f904)&&n==2);++calls;
    seen_args[0]=a[0];seen_args[1]=a[1];seen_regs[0]=r->eax;seen_regs[1]=r->edx;seen_regs[2]=r->ecx;
    uint64_t p=es2_mount_base_registers(&memory,IMAGE,a[0],a[1]);r->eax=(uint32_t)p;r->edx=(uint32_t)(p>>32);
}
static uint16_t quarter(unsigned i){return (uint16_t)(16384u-8u*i);}
/* The table word for a word angle, written independently of the core: the
 * index rounds (x + 8) / 16 down, folds at 400h and negates above it. */
static uint16_t cosine(uint16_t angle){
    int32_t x=angle<0x8000u?(int32_t)angle:(int32_t)angle-65536,i=x+8>=0?(x+8)/16:-((-(x+8)+15)/16);
    if(i<0)i=-i;
    return i>=0x400?(uint16_t)(0u-quarter((unsigned)(0x800-i))):quarter((unsigned)i);
}
static void test_sincos(void){
    const uint32_t mat=OBJECTS;
    for(uint32_t angle=0;angle<0x10000u;++angle){
        uint32_t slot=angle|0xabcd0000u;memset(host(0,mat-4),0x5a,16);
        uint64_t p=es2_sincos_pair_registers(&memory,slot,mat,table);
        uint16_t c=cosine((uint16_t)angle),s=cosine((uint16_t)(angle-0x4000u));
        CHECK(r16(0,mat)==c&&r16(0,mat+6)==c&&r16(0,mat+2)==s&&r16(0,mat+4)==(uint16_t)(0u-s));
        CHECK(r32(0,mat-4)==0x5a5a5a5au&&r32(0,mat+8)==0x5a5a5a5au);
        /* EAX: the negated sine dword (movzx or its negation, then NEG). */
        int32_t x=(int32_t)(int16_t)(uint16_t)(angle-0x4000u);
        uint32_t want=(uint16_t)s;int32_t i=x+8>=0?(x+8)/16:-((-(x+8)+15)/16);
        if((i<0?-i:i)<0x400)want=0u-want;
        else want=(uint32_t)(uint16_t)(0u-s);
        CHECK((uint32_t)p==want);
        CHECK((uint32_t)(p>>32)==(x< -8?0xffffffffu:0u));
    }
    /* Edges: 0, quarter turns, the half turn and the -8/-9 sign change. */
    es2_sincos_pair_registers(&memory,0,mat,table);CHECK(r16(0,mat)==16384&&r16(0,mat+2)==(uint16_t)(0u-quarter(0x400)));
    CHECK((uint32_t)(es2_sincos_pair_registers(&memory,0x3ff8u,mat,table)>>32)==0);
    CHECK((uint32_t)(es2_sincos_pair_registers(&memory,0x3ff7u,mat,table)>>32)==0xffffffffu);
}
static void test_rotate(void){
    const uint32_t v=OBJECTS,m=OBJECTS+0x10,out=OBJECTS+0x20;
    w32(0,v,1000);w32(0,v+4,0);w16(0,m,0x4000);w16(0,m+2,0);w16(0,m+4,0);w16(0,m+6,0x4000);
    uint64_t p=es2_rotate_2d_registers(&memory,v,m,out);
    CHECK(r32(0,out)==1000&&r32(0,out+4)==0&&(uint32_t)p==out&&(uint32_t)(p>>32)==0);
    /* A negative second sum: EDX is its high dword. */
    w32(0,v,0xffffffffu);w16(0,m+2,0x4000);
    p=es2_rotate_2d_registers(&memory,v,m,out);
    CHECK(r32(0,out)==0xffffffffu&&r32(0,out+4)==0xffffffffu&&(uint32_t)(p>>32)==0xffffffffu);
    /* Large products: bits 14..45 of the 64-bit sum. */
    w32(0,v,0x7fffffffu);w32(0,v+4,0x80000000u);w16(0,m,0x7fff);w16(0,m+2,0x8000);w16(0,m+4,0x8000);w16(0,m+6,0x7fff);
    p=es2_rotate_2d_registers(&memory,v,m,out);
    int64_t s0=(int64_t)0x7fffffff*32767+(int64_t)INT32_MIN*-32768+0x2000,s1=(int64_t)0x7fffffff*-32768+(int64_t)INT32_MIN*32767+0x2000;
    CHECK(r32(0,out)==(uint32_t)((uint64_t)s0>>14)&&r32(0,out+4)==(uint32_t)((uint64_t)s1>>14));
    CHECK((uint32_t)(p>>32)==(uint32_t)((uint64_t)s1>>32));
    /* In place: the second row reads the new v0. */
    w32(0,v,100);w32(0,v+4,200);w16(0,m,0x4000);w16(0,m+2,0x4000);w16(0,m+4,0);w16(0,m+6,0);
    p=es2_rotate_2d_registers(&memory,v,m,v);
    CHECK(r32(0,v)==100&&r32(0,v+4)==100&&(uint32_t)p==v);
    w32(0,v,100);w32(0,v+4,200);w16(0,m,0);w16(0,m+2,0x4000);w16(0,m+4,0x4000);w16(0,m+6,0);
    es2_rotate_2d_registers(&memory,v,m,v);
    CHECK(r32(0,v)==200&&r32(0,v+4)==200);
}
static void test_mount(void){
    const uint32_t M=OBJECTS+0x100,W=OBJECTS+0x200,C=OBJECTS+0x300,V=VA(0x4a9af0),guard=VA(0x498f34);
    for(int side=-2;side<=6;++side){
        arena[guard-BASE]=0;w32(0,V,0x11112222u);w16(0,V+4,0x3333);
        w32(0,M+0x46,0x7777fff0u);w16(0,M+0x4a,0x0005);
        uint64_t p=es2_mount_base_registers(&memory,IMAGE,M,(uint32_t)side|0x12340000u);
        CHECK(arena[guard-BASE]==1&&(uint32_t)p==V&&(uint32_t)(p>>32)==0x77770005u&&r16(0,V+2)==0);
        uint16_t x=r16(0,V),z=r16(0,V+4);
        switch(side){
        case 0:CHECK(x==0&&z==5);break;
        case 1:CHECK(x==0&&z==0xfffb);break;
        case 2:CHECK(x==0x10&&z==0);break;
        case 3:CHECK(x==0xfff0&&z==0);break;
        case 4:CHECK(x==0&&z==0);break;
        default:CHECK(x==0xfff0&&z==5);break;
        }
    }
    arena[guard-BASE]=7;es2_mount_base_registers(&memory,IMAGE,M,5);CHECK(arena[guard-BASE]==7);
    /* M+4Ah over V.x: the word is read after the dword store. */
    w32(0,V-0x46+0x46,0);w32(0,V,0);
    const uint32_t alias=V-0x4a;w32(0,alias+0x46,0xaaaa5555u);
    uint64_t p=es2_mount_base_registers(&memory,IMAGE,alias,7);
    CHECK((uint32_t)(p>>32)==0xaaaa5555u&&r16(0,V+4)==0x5555);
    /* 40f540: offsets added to V in the order z, x, y; EDX from C+10h. */
    w32(0,W+0x18,C);w32(0,W+0x1c,M);arena[C+6-BASE]=0xfe;
    w16(0,C+0x10,3);w16(0,C+0x12,0x8001);w16(0,C+0x14,0xffff);
    w32(0,M+0x46,0x7777fff0u);w16(0,M+0x4a,0x0005);
    const uint32_t entry[3]={0x1111u,0x2222u,0x3333u};
    es2_weapon_graph g={IMAGE,0,0,0,0,call,0,0,0};
    calls=0;p=es2_mount_offset_registers(&memory,&g,W,entry);
    CHECK(calls==1&&seen_args[0]==M&&seen_args[1]==0xfffffffeu);
    CHECK(seen_regs[0]==C&&seen_regs[1]==0xfffffffeu&&seen_regs[2]==M);
    CHECK((uint32_t)p==V&&(uint32_t)(p>>32)==((C+0x10u)&0xffff0000u)+0x8001u);
    CHECK(r16(0,V)==0xfff3&&r16(0,V+2)==0x8001&&r16(0,V+4)==4);
    /* Side 1 from s8 C+6 = 1. */
    arena[C+6-BASE]=1;p=es2_mount_offset_registers(&memory,&g,W,entry);
    CHECK(seen_args[1]==1&&r16(0,V)==3&&r16(0,V+4)==0xfffa);
}
int main(void){
    for(unsigned i=0;i<=0x400;++i){table[2*i]=(uint8_t)quarter(i);table[2*i+1]=(uint8_t)(quarter(i)>>8);}
    test_sincos();test_rotate();test_mount();
    printf("mount: %u checks passed\n",checks);
    return 0;
}
