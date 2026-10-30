#include "es2/core/animation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { BASE=0x12340000, T=BASE+0x40, R=BASE+0x100, L=BASE+0x180,
       C=BASE+0x200, V=BASE+0x300, F=BASE+0x380, R2=BASE+0x400 };
static unsigned char arena[0x500];
static unsigned checks, calls, writes, offsets[16];
static uint32_t frame_slot, mutated_clip, mutated_frame;
#define CHECK(x) do { ++checks; if(!(x)){fprintf(stderr,"track_clip line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static void *host(void *c,uint32_t a) { (void)c;CHECK(a>=BASE && a-BASE<sizeof(arena));return arena+(a-BASE); }
static uint16_t r16(void *c,uint32_t a) { unsigned char *p=host(c,a);return (uint16_t)(p[0]|p[1]<<8); }
static uint32_t r32(void *c,uint32_t a) { return r16(c,a)|((uint32_t)r16(c,a+2)<<16); }
static void raw16(uint32_t a,uint16_t v) { unsigned char *p=host(NULL,a);p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8); }
static void raw32(uint32_t a,uint32_t v) { raw16(a,(uint16_t)v);raw16(a+2,(uint16_t)(v>>16)); }
static void w16(void *c,uint32_t a,uint16_t v) { (void)c;CHECK(writes<16);offsets[writes++]=a-T;raw16(a,v); }
static void w32(void *c,uint32_t a,uint32_t v) { (void)c;raw32(a,v); }
static const es2_guest_memory memory={NULL,r32,r16,w32,w16,host};
static uint32_t sx(uint32_t n) { return (n&0x8000u)?n|0xffff0000u:n&0xffffu; }
static uint64_t callback(void *c,uint32_t f,const uint32_t *a,unsigned n) {
    (void)c;++calls;
    if(calls==1) {
        CHECK(f==1 && n==2 && a[0]==C && a[1]==frame_slot);
        CHECK(writes==3 && r16(NULL,T+4)==r16(NULL,T+8));
        /* A new resource and signed indices must be read after this call. */
        raw32(T,R2);raw16(T+4,(uint16_t)mutated_clip);raw16(T+6,(uint16_t)mutated_frame);
        raw32(R2+6,L-4u*sx(mutated_clip));raw32(C+0xc,F-6u*sx(mutated_frame));
        return UINT64_C(0xabcdef015432fedc);
    }
    CHECK(calls==2 && f==2 && n==3 && a[0]==T && a[1]==sx(mutated_clip) && a[2]==sx(mutated_frame));
    CHECK(writes==6 && r16(NULL,T+0xa)==0xfedc && r16(NULL,T+0x1c)==0x4567 && r16(NULL,T+0x1e)==0x9876);
    raw16(T+0x20,0xcafe);
    return UINT64_C(0x76543210fedcba98);
}
int main(void) {
    static const uint16_t edges[]={0,1,0x7fff,0x8000,0xffff};
    static const unsigned expected[]={4,6,8,0xa,0x1c,0x1e,0x18,0x16,0x1a,0xc,0x50};
    for(unsigned i=0;i<5;++i)for(unsigned j=0;j<5;++j) {
        memset(arena,0xa5,sizeof(arena));calls=writes=0;
        raw32(T,R);raw32(R+6,L-4u*sx(edges[i]));raw32(L,C);raw32(C,V);raw32(V+0x20,1);raw16(F,0x9876);
        mutated_clip=edges[j];mutated_frame=edges[i];frame_slot=0xabcd0000u|edges[j];
        CHECK(es2_track_clip_registers(&memory,T,0x11110000u|edges[i],0x22220000u|edges[j],0x87654567u,0xabcd1357u,2,callback,NULL)==UINT64_C(0x76543210fedcba98));
        CHECK(calls==2 && writes==11 && !memcmp(offsets,expected,sizeof(expected)));
        CHECK(r16(NULL,T+0x20)==0xcafe && r16(NULL,T+0x50)==1 && r16(NULL,T+0x1a)==0);
        CHECK(r16(NULL,T+0x18)==0xffff && r16(NULL,T+0x16)==0xffff && r16(NULL,T+0xc)==0xffff);
    }
    printf("track_clip: %u checks, 25 signed boundary cases passed\n",checks);return 0;
}
