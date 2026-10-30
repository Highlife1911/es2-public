#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/math_lib.h"
/* Synthetic state for the core. The image base is placed so that the
 * globals 6c5f80..6c5f8f fall at the start of the arena; the edge record
 * and the point arrays live above them. Expected values are worked out by
 * hand from the contract in player_movement.md. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE-0x2c5f80u)
#define EDGE (BASE+0x100u)
#define SCREEN (BASE+0x200u)
#define TEXTURE (BASE+0x300u)
static uint8_t arena[0x400];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"math_lib line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
static uint32_t field(unsigned offset){return r32(0,EDGE+offset);}
/* A polygon of n vertices: screen (x, y) and texture (u, v) per vertex. */
static void polygon(unsigned n,const int32_t (*xy)[2],const int32_t (*uv)[2],uint32_t end,int32_t direction){
    memset(arena,0xcc,sizeof(arena));
    w32(0,BASE,end);w32(0,BASE+4,n);w32(0,BASE+8,SCREEN);w32(0,BASE+12,TEXTURE);
    for(unsigned i=0;i<n;++i){
        w32(0,SCREEN+8*i,(uint32_t)xy[i][0]);w32(0,SCREEN+8*i+4,(uint32_t)xy[i][1]);
        w32(0,TEXTURE+8*i,(uint32_t)uv[i][0]);w32(0,TEXTURE+8*i+4,(uint32_t)uv[i][1]);
    }
    w32(0,EDGE,(uint32_t)direction);
}
static void test_right_edge(void){
    /* Quad with top vertex 0; the right walk (+1) from 0 reaches 1. */
    static const int32_t xy[4][2]={{10,0},{20,4},{12,9},{0,5}},uv[4][2]={{0,0},{64,0},{64,64},{0,64}};
    polygon(4,xy,uv,2,1);
    uint64_t p=es2_tex_edge_registers(&memory,IMAGE,EDGE,0,0x12345678u);
    CHECK((uint32_t)p==1);
    CHECK(field(4)==4&&field(8)==1);
    CHECK(field(0x0c)==0&&field(0x10)==0);
    CHECK(field(0x14)==(64u<<16)/4u&&field(0x18)==0);
    CHECK(field(0x1c)==10&&field(0x24)==1&&field(0x28)==0);
    CHECK(field(0x20)==2&&field(0x2c)==2&&field(0x30)==4);       /* dx 10 = 2 * 4 + 2 */
    CHECK((uint32_t)(p>>32)==2);
    CHECK(r32(0,EDGE+0x34)==0xccccccccu);
}
static void test_left_edge(void){
    /* The left walk (-1) from 0 wraps to the last vertex 3; dx = -10. */
    static const int32_t xy[4][2]={{10,0},{20,4},{12,9},{0,3}},uv[4][2]={{0,0},{64,0},{64,64},{0,-30}};
    polygon(4,xy,uv,2,-1);
    uint64_t p=es2_tex_edge_registers(&memory,IMAGE,EDGE,0,0);
    CHECK((uint32_t)p==1&&field(4)==3&&field(8)==3);
    CHECK(field(0x14)==0&&field(0x18)==(uint32_t)(-(30<<16)/3));
    CHECK(field(0x24)==0xffffffffu&&field(0x28)==(uint32_t)(1-3));
    CHECK(field(0x20)==(uint32_t)-3&&field(0x2c)==1&&field(0x30)==3); /* 10 = 3 * 3 + 1 */
    CHECK((uint32_t)(p>>32)==1);
}
static void test_skip_and_end(void){
    /* Vertices 0 and 1 share y: the edge 0-1 is skipped, 1-2 is set up. */
    static const int32_t xy[3][2]={{0,5},{8,5},{2,12}},uv[3][2]={{0,0},{7,0},{0,7}};
    polygon(3,xy,uv,2,1);
    uint64_t p=es2_tex_edge_registers(&memory,IMAGE,EDGE,0,0);
    CHECK((uint32_t)p==1&&field(4)==7&&field(8)==2&&field(0x1c)==8);
    CHECK(field(0x24)==0xffffffffu&&field(0x20)==0&&field(0x2c)==6);   /* |dx| 6 < 7 */
    CHECK(field(0x14)==(uint32_t)(-(7<<16)/7)&&field(0x0c)==(7u<<16));
    /* Starting at the end vertex: EAX 0, EDX the entry EDX, nothing written. */
    memset(arena+0x104,0xcc,0x30);
    p=es2_tex_edge_registers(&memory,IMAGE,EDGE,2,0xdeadbeefu);
    CHECK(p==((uint64_t)0xdeadbeefu<<32)&&field(4)==0xccccccccu);
    /* All edges flat up to the end: EAX 0, EDX the y of the last vertex left, +4 = 0. */
    static const int32_t flat[3][2]={{0,5},{8,5},{2,5}};
    polygon(3,flat,uv,2,1);
    p=es2_tex_edge_registers(&memory,IMAGE,EDGE,0,0);
    CHECK(p==((uint64_t)5<<32)&&field(4)==0&&field(8)==0xccccccccu);
}
static void test_wraps(void){
    /* Direction +1 from the last vertex wraps to 0 (JL not taken). */
    static const int32_t xy[3][2]={{0,9},{4,0},{7,3}},uv[3][2]={{0,0},{0,0},{0,0}};
    polygon(3,xy,uv,1,1);
    CHECK((uint32_t)es2_tex_edge_registers(&memory,IMAGE,EDGE,2,0)==1&&field(8)==0&&field(4)==6);
    /* A direction of -5 from 1 goes below zero and wraps to n - 1. */
    polygon(3,xy,uv,0,-5);
    CHECK((uint32_t)es2_tex_edge_registers(&memory,IMAGE,EDGE,1,0)==1&&field(8)==2&&field(4)==3);
    /* A negative height divides with the IDIV signs: dy -6, dx 7. */
    polygon(3,xy,uv,1,-1);
    uint64_t p=es2_tex_edge_registers(&memory,IMAGE,EDGE,0,0);
    CHECK((uint32_t)p==1&&field(8)==2&&field(4)==(uint32_t)-6);
    CHECK(field(0x24)==1&&field(0x20)==(uint32_t)-1&&field(0x2c)==1&&(uint32_t)(p>>32)==1);
}
static void test_overlap(void){
    /* The record over the screen array: E+4 is vertex 0's y, so the
     * second step reads the height written by the first (a flat edge
     * writes 0 there). */
    static const int32_t xy[3][2]={{0,4},{3,4},{9,9}},uv[3][2]={{0,0},{0,0},{0,0}};
    polygon(3,xy,uv,2,1);
    const uint32_t e=SCREEN;w32(0,e,1);         /* direction over vertex 0's x */
    uint64_t p=es2_tex_edge_registers(&memory,IMAGE,e,0,0);
    /* 0->1 flat ([e+4], vertex 0's y, becomes 0); 1->2: dy 5. Then E+8 = 2
     * lands on vertex 1's x, E+10 = 0 on vertex 2's x, so x and dx are
     * read back as 2 and 0 - 2. */
    CHECK((uint32_t)p==1&&r32(0,e+4)==5&&r32(0,e+8)==2&&r32(0,e+0x1c)==2);
    CHECK(r32(0,e+0x24)==0xffffffffu&&r32(0,e+0x20)==0&&r32(0,e+0x2c)==2&&r32(0,e+0x30)==5&&(uint32_t)(p>>32)==2);
}
int main(void){
    test_right_edge();test_left_edge();test_skip_and_end();test_wraps();test_overlap();
    printf("PASS: math_lib core (%u checks)\n",checks);
    return 0;
}
