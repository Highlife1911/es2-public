#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/fire.h"
/* Synthetic guest: an arena at 90000000h (fires, owner, offset point,
 * kind table, counter) and an image base I with the globals 0x6b4f28
 * (kind table), 0x6b4f30 (pool), 0x6b4f34 (removal list), 0x6b4f38
 * (burning count), 0x4a9e4e (world word) and 0x6cbee4 (counter pointer).
 * Every game call is logged with its arguments and entry registers; the
 * stubs answer from a small plan. Expectations from the reference listing
 * (main_loop.md, section "fire allocation 46b430, fire constructor 46b48c, fire
 * release 46b62c"). */
#define ARENA UINT32_C(0x90000000)
#define I UINT32_C(0x10000000)
#define G(va) (I+(va)-0x400000u)
static uint8_t arena[0x1000],globals[6][4];
static const uint32_t global_va[6]={0x6b4f28,0x6b4f30,0x6b4f34,0x6b4f38,0x4a9e4e,0x6cbee4};
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"fire line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-ARENA<sizeof(arena))return arena+(a-ARENA);
    for(unsigned i=0;i<6;++i)if(a-G(global_va[i])<4)return globals[i]+(a-G(global_va[i]));
    fprintf(stderr,"fire: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
#define FIRE(i) (ARENA+0x100+0x60*(i))
#define OWNER (ARENA+0x800)
#define POINT (ARENA+0x900)
#define TABLE (ARENA+0xa00)
#define COUNTER (ARENA+0xb00)
#define POOL (ARENA+0xc00)
#define LIST (ARENA+0xd00)
typedef struct logged { uint32_t f,a[3],r[3]; unsigned n; } logged;
static logged log_[32];static unsigned calls;
static uint32_t alloc_result,walk[8];static unsigned walk_length,walk_at;
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<32);logged *l=log_+calls++;memset(l,0,sizeof(*l));l->f=f;l->n=n;if(n)memcpy(l->a,a,4*n);
    l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    uint32_t eax=0xe0e00000u+calls,edx=0xd0d00000u+calls,ecx=0xc0c00000u+calls;
    if(f==G(0x471b30)){CHECK(n==1&&a[0]==POOL);eax=alloc_result;}
    else if(f==G(0x471c70)){CHECK(n==2&&a[0]==POOL&&a[1]==(walk_at?walk[walk_at-1]:0)&&walk_at<=walk_length);eax=walk_at<walk_length?walk[walk_at]:0;++walk_at;}
    else if(f==G(0x402188)){CHECK(n==2);eax=a[0];}
    else if(f==G(0x49681a)){CHECK(n==3);memset(host(0,a[0]),(int)a[1],a[2]);eax=a[0];}
    else if(f==G(0x462830)){CHECK(n==1&&a[0]==0x33);}
    else if(f==G(0x46b678)){CHECK(n==1);}
    else if(f==G(0x478628)){CHECK(n==2&&a[0]==LIST);eax=a[0];edx=a[1];}
    else {fprintf(stderr,"fire: unexpected call %08x\n",f);exit(1);}
    r->eax=eax;r->edx=edx;r->ecx=ecx;
}
static es2_weapon_graph graph(void){es2_weapon_graph g={I,0,0,0,0,call,0,0,0};return g;}
static const uint32_t entry[3]={0x11110001u,0x22220002u,0x33330003u};
static void reset(void){
    memset(arena,0x5a,sizeof(arena));memset(globals,0,sizeof(globals));calls=0;walk_length=walk_at=0;
    w32(0,G(0x6b4f28),TABLE);w32(0,G(0x6b4f30),POOL);w32(0,G(0x6b4f34),LIST);w32(0,G(0x6cbee4),COUNTER);w32(0,COUNTER,100);
    for(unsigned i=0;i<4;++i)w32(0,TABLE+4*i,0x77000000u+i);
    w32(0,TABLE-4,0x7700ffffu);
    w32(0,POINT,0xabcd1234u);w16(0,POINT+4,0x5678);
}
static void alloc_checks(void){
    es2_weapon_graph g=graph();uint32_t ecx=0;
    /* a free object: one call, entry registers passed on */
    reset();alloc_result=FIRE(5);
    uint64_t v=es2_fire_alloc_registers(&memory,&g,entry,&ecx);
    CHECK(calls==1&&v==((uint64_t)0xd0d00001u<<32|FIRE(5))&&ecx==entry[2]);
    CHECK(log_[0].f==G(0x471b30)&&log_[0].r[0]==POOL&&log_[0].r[1]==entry[1]&&log_[0].r[2]==entry[2]);
    /* full pool: the smallest lifetime below 10000, the first of equals */
    reset();alloc_result=0;walk_length=4;
    walk[0]=FIRE(0);walk[1]=FIRE(1);walk[2]=FIRE(2);walk[3]=FIRE(3);
    w16(0,FIRE(0)+0x59,100);w16(0,FIRE(1)+0x59,20);w16(0,FIRE(2)+0x59,20);w16(0,FIRE(3)+0x59,0x2710);
    v=es2_fire_alloc_registers(&memory,&g,entry,&ecx);
    CHECK(calls==6&&v==((uint64_t)0xd0d00006u<<32|FIRE(1))&&ecx==0);
    CHECK(log_[1].f==G(0x471c70)&&log_[1].r[0]==POOL&&log_[1].r[1]==0xd0d00001u&&log_[1].r[2]==POOL);
    CHECK(log_[2].r[0]==POOL&&log_[2].r[1]==0xd0d00002u&&log_[2].r[2]==0xc0c00002u);
    /* negative lifetimes count (signed), 10000 and above never */
    reset();alloc_result=0;walk_length=2;walk[0]=FIRE(0);walk[1]=FIRE(1);
    w16(0,FIRE(0)+0x59,0x2710);w16(0,FIRE(1)+0x59,0xfff0);
    v=es2_fire_alloc_registers(&memory,&g,entry,&ecx);
    CHECK(calls==4&&(uint32_t)v==FIRE(1));
    reset();alloc_result=0;walk_length=2;walk[0]=FIRE(0);walk[1]=FIRE(1);
    w16(0,FIRE(0)+0x59,0x2710);w16(0,FIRE(1)+0x59,0x7fff);
    v=es2_fire_alloc_registers(&memory,&g,entry,&ecx);
    CHECK(calls==4&&v==((uint64_t)0xd0d00004u<<32)&&ecx==0);
}
static void create_checks(void){
    es2_weapon_graph g=graph();uint32_t ecx=0;
    /* world 0, kind 2, offset point, first fire: sound and update */
    reset();
    uint64_t v=es2_fire_create_registers(&memory,&g,FIRE(1),OWNER,0x1234ffffu,POINT,0xabcd0002u,entry,&ecx);
    CHECK(calls==4&&v==((uint64_t)0xd0d00004u<<32|FIRE(1))&&ecx==COUNTER&&r32(0,COUNTER)==101&&r32(0,G(0x6b4f38))==1);
    CHECK(log_[0].f==G(0x402188)&&log_[0].a[0]==FIRE(1)&&log_[0].a[1]==0x77000002u&&log_[0].r[0]==2&&log_[0].r[1]==TABLE&&log_[0].r[2]==0x77000002u);
    CHECK(log_[1].f==G(0x49681a)&&log_[1].a[0]==FIRE(1)+0x41&&log_[1].a[1]==0&&log_[1].a[2]==8&&log_[1].r[0]==FIRE(1)+0x41&&log_[1].r[1]==0xd0d00001u&&log_[1].r[2]==0xc0c00001u);
    CHECK(log_[2].f==G(0x462830)&&log_[2].r[0]==POINT&&log_[2].r[1]==0xabcd001eu&&log_[2].r[2]==0xc0c0ffffu);
    CHECK(log_[3].f==G(0x46b678)&&log_[3].a[0]==FIRE(1)&&log_[3].r[0]==0xe0e00003u&&log_[3].r[1]==0xd0d00003u&&log_[3].r[2]==0x33);
    CHECK(r32(0,FIRE(1))==G(0x4a0ba0)&&r32(0,FIRE(1)+0x4a)==OWNER&&r16(0,FIRE(1)+0x4e)==0xffff&&r32(0,FIRE(1)+0x50)==0xabcd1234u&&r16(0,FIRE(1)+0x54)==0x5678);
    CHECK(r16(0,FIRE(1)+4)==4&&r16(0,FIRE(1)+0x57)==0x40&&r16(0,FIRE(1)+0x59)==0x1e&&arena[FIRE(1)-ARENA+0x41]==0&&arena[FIRE(1)-ARENA+0x48]==0);
    /* world 4, kind 1, no point, a fire already burns: no sound */
    reset();w16(0,G(0x4a9e4e),4);w32(0,G(0x6b4f38),3);
    v=es2_fire_create_registers(&memory,&g,FIRE(2),OWNER,7,0,1,entry,&ecx);
    CHECK(calls==3&&v==((uint64_t)0xd0d00003u<<32|FIRE(2))&&r32(0,G(0x6b4f38))==4);
    CHECK(log_[2].f==G(0x46b678)&&log_[2].r[0]==FIRE(2)+0x41&&log_[2].r[1]==((OWNER&0xffff0000u)|5u)&&log_[2].r[2]==0xc0c00007u);
    CHECK(arena[FIRE(2)-ARENA+0x41]==1&&r16(0,FIRE(2)+0x59)==5&&r16(0,FIRE(2)+0x4e)==7);
    /* world 4, kind 3 burns; kind FFFFh reads the table below its base */
    reset();w16(0,G(0x4a9e4e),4);
    v=es2_fire_create_registers(&memory,&g,FIRE(2),OWNER,7,0,0x10003u,entry,&ecx);
    CHECK(calls==4&&log_[0].a[1]==0x77000003u);
    reset();
    v=es2_fire_create_registers(&memory,&g,FIRE(2),OWNER,7,0,0xffffu,entry,&ecx);
    CHECK(calls==4&&log_[0].a[1]==0x7700ffffu&&log_[0].r[0]==0xffffffffu);
    /* world 4, kind 2: counted and discarded at once */
    reset();w16(0,G(0x4a9e4e),4);memset(arena+(FIRE(3)-ARENA),0x5a,0x60);
    v=es2_fire_create_registers(&memory,&g,FIRE(3),OWNER,7,POINT,2,entry,&ecx);
    CHECK(calls==2&&v==((uint64_t)FIRE(3)<<32|FIRE(3))&&r32(0,G(0x6b4f38))==1&&r32(0,COUNTER)==101&&ecx==COUNTER);
    CHECK(log_[1].f==G(0x478628)&&log_[1].a[1]==FIRE(3)&&log_[1].r[0]==FIRE(3)&&log_[1].r[1]==LIST&&log_[1].r[2]==0xc0c00001u);
    CHECK(r32(0,FIRE(3))==G(0x4a0ba0)&&r32(0,FIRE(3)+0x4a)==0x5a5a5a5au&&r16(0,FIRE(3)+0x59)==0x5a5a);
}
static void release_checks(void){
    es2_weapon_graph g=graph();uint32_t ecx=1;
    reset();walk_length=3;walk[0]=FIRE(0);walk[1]=FIRE(1);walk[2]=FIRE(2);
    w32(0,FIRE(0)+0x4a,OWNER);w32(0,FIRE(1)+0x4a,OWNER+1);w32(0,FIRE(2)+0x4a,OWNER);
    uint64_t v=es2_fire_release_registers(&memory,&g,OWNER,entry,&ecx);
    CHECK(calls==6&&v==((uint64_t)0xd0d00006u<<32)&&ecx==0);
    CHECK(log_[0].f==G(0x471c70)&&log_[0].r[0]==0&&log_[0].r[1]==entry[1]&&log_[0].r[2]==POOL);
    CHECK(log_[1].f==G(0x478628)&&log_[1].a[1]==FIRE(0)&&log_[1].r[0]==FIRE(0)&&log_[1].r[1]==LIST&&log_[1].r[2]==0xc0c00001u);
    CHECK(log_[2].f==G(0x471c70)&&log_[2].r[0]==LIST&&log_[2].r[1]==FIRE(0)&&log_[2].r[2]==POOL);
    CHECK(log_[3].f==G(0x471c70)&&log_[3].r[0]==FIRE(1)&&log_[3].r[1]==0xd0d00003u);
    CHECK(log_[4].f==G(0x478628)&&log_[4].a[1]==FIRE(2));
    /* empty pool: one call */
    reset();
    v=es2_fire_release_registers(&memory,&g,OWNER,entry,&ecx);
    CHECK(calls==1&&v==((uint64_t)0xd0d00001u<<32));
}
/* The object base methods 46b905 and 46b910 (an object O, its
 * shape instance S and record Q in the arena). */
static void object_checks(void){
    const uint32_t O=ARENA+0x100,S=ARENA+0x200,Q=ARENA+0x300,e[3]={0x11112222u,0x33334444u,0x55556666u};
    uint32_t ecx=0;uint64_t v;
    memset(arena,0,sizeof(arena));
    v=es2_object_position_registers(O,e,&ecx);
    CHECK(v==((uint64_t)e[1]<<32|(O+0x26u))&&ecx==e[2]);
    /* no shape: EAX 0, ECX 0, EDX entry */
    v=es2_shape_word_registers(&memory,O,e,&ecx);
    CHECK(v==((uint64_t)e[1]<<32)&&ecx==0);
    /* shape without record: EAX 0, ECX 0, EDX S */
    w32(0,O+0x34,S);
    v=es2_shape_word_registers(&memory,O,e,&ecx);
    CHECK(v==((uint64_t)S<<32)&&ecx==0);
    /* the word: EAX = high word of S : word Q+8, EDX = Q, ECX = Q */
    w32(0,S+4,Q);w16(0,Q+8,0xfedc);
    v=es2_shape_word_registers(&memory,O,e,&ecx);
    CHECK(v==((uint64_t)Q<<32|((S&0xffff0000u)|0xfedcu))&&ecx==Q);
}
int main(void){
    object_checks();
    alloc_checks();
    create_checks();
    release_checks();
    printf("PASS: fire %u checks\n",checks);return 0;
}
