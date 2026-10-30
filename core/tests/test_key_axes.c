#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/key_axes.h"
/* Synthetic state for the core (key_axes 45a4cc). The image base is
 * placed so that the tables 49eb3d/49eb45 and the key arrays 4d2394..
 * 4d23d7 fall inside one arena. Expected values are worked out by hand
 * from the contract in input.md with the tables as 45b8a4 writes
 * them. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE-0x9eb00u)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define DOWN VA(0x4d2394)
#define RELEASED VA(0x4d23a3)
#define SEEN VA(0x4d23b2)
#define BLOCK VA(0x4d23c1)
#define OTHER VA(0x4d2500)
static uint8_t arena[0x34000],saved[0x34000];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"key_axes line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
static uint8_t *at(uint32_t a){return host(0,a);}
/* The tables: exclusive flags 1,0,1,0,0,1,0,1 (shipped) and the pairs of
 * 45b8a4. */
static void setup(void){
    static const int8_t pairs[28]={-1,-1,0,-1,1,-1,-1,0,1,0,-1,1,0,1,1,1,0,-1,-1,0,1,0,0,1,0,-1,0,1};
    static const uint8_t exclusive[8]={1,0,1,0,0,1,0,1};
    memset(arena,0xcc,sizeof(arena));
    memcpy(at(VA(0x49eb3d)),exclusive,8);memcpy(at(VA(0x49eb45)),pairs,28);
    memset(at(DOWN),0,0x2d);w16(0,VA(0x4d23d6),0);
}
static uint32_t axis(unsigned k){return r32(0,BLOCK+4u*k);}
static void run(uint32_t block,uint32_t flag){
    uint64_t v=es2_key_axes_registers(&memory,IMAGE,block,flag);
    CHECK((uint32_t)v==15u&&(uint32_t)(v>>32)==RELEASED);
}
/* The same state with another flag and Alt word gives the same bytes. */
static void same_for_flag_and_alt(void){
    uint8_t first[sizeof(arena)];memcpy(saved,arena,sizeof(arena));
    run(BLOCK,0);memcpy(first,arena,sizeof(arena));
    for(unsigned k=0;k<3;++k){
        memcpy(arena,saved,sizeof(arena));w16(0,VA(0x4d23d6),k?0x200:0);
        run(BLOCK,k==2?0xffffffffu:1u);
        w16(0,VA(0x4d23d6),0);CHECK(!memcmp(first,arena,sizeof(arena)));
    }
}
int main(void){
    /* No key: the block's 20 bytes are cleared, the byte after it stays. */
    setup();run(BLOCK,0);
    for(unsigned j=0;j<0x14;++j)CHECK(at(BLOCK)[j]==0);
    CHECK(at(BLOCK)[0x14]==0xcc);
    for(unsigned j=0;j<15;++j)CHECK(at(SEEN)[j]==0);
    /* Num8 (key 1, (0,-1)): throttle -128, seen. */
    setup();at(DOWN)[1]=1;run(BLOCK,0);
    CHECK(axis(0)==0&&axis(1)==0xffffff80u&&axis(2)==0&&axis(3)==0&&at(SEEN)[1]==1);
    /* Num8 and Num4 (keys 1 and 3, neither exclusive) add up. */
    setup();at(DOWN)[1]=1;at(DOWN)[3]=7;run(BLOCK,0);
    CHECK(axis(0)==0xffffff80u&&axis(1)==0xffffff80u&&at(SEEN)[1]==1&&at(SEEN)[3]==1);
    /* Num7 (key 0, exclusive, (-1,-1)) ends the pad scan: Num8 is neither
     * added nor marked seen. */
    setup();at(DOWN)[0]=1;at(DOWN)[1]=1;run(BLOCK,0);
    CHECK(axis(0)==0xffffff80u&&axis(1)==0xffffff80u&&at(SEEN)[0]==1&&at(SEEN)[1]==0);
    /* Num4 then Num1 (key 5, exclusive, (-1,1)): the exclusive key
     * restarts the sum. */
    setup();at(DOWN)[3]=1;at(DOWN)[5]=1;run(BLOCK,0);
    CHECK(axis(0)==0xffffff80u&&axis(1)==0x80u&&at(SEEN)[3]==1&&at(SEEN)[5]==1);
    /* J and K (keys 9, 10): only the first counts, K stays unseen. */
    setup();at(DOWN)[9]=1;at(DOWN)[10]=1;run(BLOCK,0);
    CHECK(axis(0)==0&&axis(1)==0&&axis(2)==0xffffff80u&&axis(3)==0&&at(SEEN)[9]==1&&at(SEEN)[10]==0);
    /* Num6 and I (keys 4, 11): turn +128, pitch +128. */
    setup();at(DOWN)[4]=1;at(DOWN)[11]=1;run(BLOCK,0);
    CHECK(axis(0)==0x80u&&axis(1)==0&&axis(2)==0&&axis(3)==0x80u);
    /* Num3 (key 7, exclusive) and M (key 8): the exclusive key ends only
     * the pad; M still sets pitch -128. */
    setup();at(DOWN)[7]=1;at(DOWN)[8]=1;run(BLOCK,0);
    CHECK(axis(0)==0x80u&&axis(1)==0x80u&&axis(3)==0xffffff80u&&at(SEEN)[8]==1);
    /* Signed table bytes: 80h gives -4000h, 7Fh gives 3F80h. */
    setup();at(VA(0x49eb45))[2]=0x80;at(VA(0x49eb45))[3]=0x7f;at(DOWN)[1]=1;run(BLOCK,0);
    CHECK(axis(0)==0xffffc000u&&axis(1)==0x3f80u);
    /* Space (D[14]) sets +10h; the first released byte (U[0]) sets +11h. */
    setup();at(DOWN)[14]=1;run(BLOCK,0);CHECK(at(BLOCK)[0x10]==1&&at(BLOCK)[0x11]==0);
    setup();at(RELEASED)[0]=3;run(BLOCK,0);CHECK(at(BLOCK)[0x10]==0&&at(BLOCK)[0x11]==1);
    /* +12h/+13h are cleared, whatever was there. */
    CHECK(at(BLOCK)[0x12]==0&&at(BLOCK)[0x13]==0);
    /* A tap: key 2 down and released before seen is counted, then the
     * down and seen bytes are cleared, the released byte stays. Key 14
     * (not visited by the axis loop) drops its seen byte too; a released
     * key that is up keeps its seen byte. */
    setup();at(DOWN)[2]=1;at(RELEASED)[2]=1;at(DOWN)[14]=1;at(RELEASED)[14]=1;at(SEEN)[14]=1;
    at(RELEASED)[6]=1;at(SEEN)[6]=1;run(BLOCK,0);
    CHECK(axis(0)==0x80u&&axis(1)==0xffffff80u&&at(BLOCK)[0x10]==1);
    CHECK(at(DOWN)[2]==0&&at(SEEN)[2]==0&&at(RELEASED)[2]==1);
    CHECK(at(DOWN)[14]==0&&at(SEEN)[14]==0&&at(RELEASED)[14]==1);
    CHECK(at(SEEN)[6]==1&&at(RELEASED)[6]==1);
    /* The flag and the Alt word change nothing. */
    setup();at(DOWN)[1]=1;at(DOWN)[9]=1;at(DOWN)[2]=1;at(RELEASED)[2]=1;same_for_flag_and_alt();
    /* Another block: only its 20 bytes change besides the key arrays. */
    setup();at(DOWN)[4]=1;memcpy(saved,arena,sizeof(arena));run(OTHER,5);
    CHECK(r32(0,OTHER)==0x80u&&at(OTHER)[0x14]==0xcc&&at(SEEN)[4]==1);
    for(unsigned j=0;j<0x14;++j)CHECK(at(BLOCK)[j]==0xcc);
    /* A block over the down array: the clearing makes every key up first
     * (D[0..14], U[0] by the dwords, U[1]/U[2] by +10h/+11h), so nothing
     * is added, and +12h/+13h clear U[3]/U[4]; U[5] stays. */
    setup();memset(at(DOWN),1,15);memset(at(RELEASED),1,15);run(DOWN,0);
    for(unsigned j=0;j<15;++j)CHECK(at(DOWN)[j]==0);
    for(unsigned j=0;j<5;++j)CHECK(at(RELEASED)[j]==0);
    CHECK(at(RELEASED)[5]==1&&at(SEEN)[0]==0);
    printf("key_axes: %u checks passed\n",checks);
    return 0;
}
