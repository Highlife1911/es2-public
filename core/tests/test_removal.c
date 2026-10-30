#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/removal.h"
/* Synthetic guest: an arena at 90000000h (list, item array, pool, nodes,
 * vtables, counter), a frame scratch at A0000000h, an image base I with
 * the counter pointer 0x6cbee4. Every game call of the flush is logged
 * with its arguments, its entry registers and the record's context word
 * f-14h; the stubs answer from small plans. Expectations from the
 * reference listing (main_loop.md, section "object removal"). */
#define OBJ UINT32_C(0x90000000)
#define FRAME UINT32_C(0xa0000000)
#define I UINT32_C(0x10000000)
#define G(va) (I+(va)-0x400000u)
#define F(x) (FRAME+ES2_REMOVAL_FRAME-(x))
static uint8_t arena[0x1000],scratch[ES2_REMOVAL_SCRATCH],counter_slot[4];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"removal line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-FRAME<sizeof(scratch))return scratch+(a-FRAME);
    if(a-G(0x6cbee4)<sizeof(counter_slot))return counter_slot+(a-G(0x6cbee4));
    fprintf(stderr,"removal: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
#define L (OBJ+0x100)
#define ITEMS (OBJ+0x140)
#define P (OBJ+0x200)
#define NODE(i) (OBJ+0x300+0x40u*(i))
#define O(i) (NODE(i)+8)
#define VT1 (OBJ+0x800)
#define VT2 (OBJ+0x840)
#define DTOR1 UINT32_C(0x55500001)
#define DTOR2 UINT32_C(0x55500002)
#define COUNTER (OBJ+0x900)
#define HEAD UINT32_C(0x0012ff00)
typedef struct logged { uint32_t f,a[2],r[3],context; unsigned n; } logged;
static logged log_[64];static unsigned calls,frees;
static uint32_t grow_on,swap_on; /* destructor plans: append O(5) to L; replace the entry and the array */
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<64);logged *l=log_+calls++;memset(l,0,sizeof(*l));l->f=f;l->n=n;if(n)memcpy(l->a,a,4*n);
    l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    if(f!=G(0x49630c))l->context=r16(0,F(0x14));
    if(f==G(0x49630c)){ /* the record at EBP+[descriptor+4] = f-24h */
        CHECK(n==0&&r->eax==G(0x4a1478));w32(0,F(0x24),HEAD);w16(0,F(0x14),0);w16(0,F(0x12),0);r->eax=HEAD;}
    else if(f==G(0x496406)){CHECK(n==2&&a[1]==0);r->eax=0x10u+(a[0]&0xff);r->edx=0;}
    else if(f==DTOR1||f==DTOR2){
        CHECK(n==2&&a[1]==2);
        if(a[0]==grow_on){uint16_t k=r16(0,L+4);w32(0,ITEMS+4u*k,O(5));w16(0,L+4,(uint16_t)(k+1));}
        if(a[0]==swap_on){w32(0,ITEMS,O(6));w32(0,L,OBJ+0xa00);}
        r->eax=0xd0d0d001u;r->edx=0xd0d0d002u;r->ecx=0xd0d0d003u;}
    else if(f==G(0x471bc8)){CHECK(n==2&&a[0]==P);++frees;r->eax=a[1]-8;r->edx=0;r->ecx=0xf3ee0000u+frees;}
    else {fprintf(stderr,"removal: unexpected call %08x\n",f);exit(1);}
}
static es2_weapon_graph graph(void){es2_weapon_graph g={I,FRAME,0,0,0,call,0,0,0};return g;}
static const uint32_t entry[3]={0x11110001u,0x22220002u,0x33330003u};
static void reset(void){
    memset(arena,0,sizeof(arena));memset(scratch,0x5a,sizeof(scratch));calls=0;frees=0;grow_on=0;swap_on=0;
    w32(0,G(0x6cbee4),COUNTER);w32(0,COUNTER,100);
    for(unsigned i=0;i<8;++i)w32(0,O(i),i&1?VT2:VT1);
    w32(0,VT1+8,DTOR1);w32(0,VT2+8,DTOR2);w32(0,L,ITEMS);
}
/* Three entries (the middle one null), then the empty list. */
static void flush_checks(void){
    es2_weapon_graph g=graph();reset();uint32_t ecx=0;
    w32(0,ITEMS,O(0));w32(0,ITEMS+4,0);w32(0,ITEMS+8,O(3));w16(0,L+4,3);
    uint64_t v=es2_removal_flush_registers(&memory,&g,L,P,entry,&ecx);
    CHECK(calls==8&&r16(0,L+4)==0&&r32(0,L)==ITEMS);
    CHECK(log_[0].f==G(0x49630c)&&log_[0].r[1]==entry[1]&&log_[0].r[2]==entry[2]);
    CHECK(log_[1].f==G(0x496406)&&log_[1].a[0]==O(0)&&log_[1].r[0]==O(0)&&log_[1].r[1]==O(0)&&log_[1].r[2]==entry[2]&&log_[1].context==0);
    CHECK(log_[2].f==DTOR1&&log_[2].a[0]==O(0)&&log_[2].r[0]==O(0)&&log_[2].r[1]==VT1&&log_[2].r[2]==COUNTER&&log_[2].context==0x14);
    CHECK(log_[3].f==G(0x471bc8)&&log_[3].a[1]==O(0)&&log_[3].r[0]==P&&log_[3].r[1]==0xd0d0d002u&&log_[3].r[2]==O(0)&&log_[3].context==8);
    /* null entry: no RTTI call, no destructor; EAX = 0 is overwritten by P */
    CHECK(log_[4].f==G(0x471bc8)&&log_[4].a[1]==0&&log_[4].r[0]==P&&log_[4].r[1]==0&&log_[4].r[2]==0);
    CHECK(log_[5].f==G(0x496406)&&log_[5].r[2]==0xf3ee0002u&&log_[6].f==DTOR2&&log_[6].r[1]==VT2);
    CHECK(log_[7].f==G(0x471bc8)&&log_[7].a[1]==O(3));
    CHECK(r32(0,COUNTER)==100+0x10+(O(0)&0xff)+0x10+(O(3)&0xff)&&r16(0,F(0x14))==8);
    CHECK(v==((uint64_t)HEAD<<32|(O(3)-8))&&ecx==0xf3ee0003u);
    /* empty list: EAX = the head from 49630c, ECX = the entry ECX */
    reset();w16(0,L+4,0);
    v=es2_removal_flush_registers(&memory,&g,L,P,entry,&ecx);
    CHECK(calls==1&&v==((uint64_t)HEAD<<32|HEAD)&&ecx==entry[2]);
    /* a negative count (8000h): no round, the count is still cleared */
    reset();w32(0,ITEMS,O(0));w16(0,L+4,0x8000);
    v=es2_removal_flush_registers(&memory,&g,L,P,entry,&ecx);
    CHECK(calls==1&&r16(0,L+4)==0);
}
/* The count is reread each round (a destructor appends to the list), the
 * array pointer is not, and the entry is reread after the destructor. */
static void flush_rereads(void){
    es2_weapon_graph g=graph();reset();uint32_t ecx=0;
    w32(0,ITEMS,O(1));w32(0,ITEMS+4,O(2));w16(0,L+4,2);grow_on=O(2);
    uint64_t v=es2_removal_flush_registers(&memory,&g,L,P,entry,&ecx);
    CHECK(calls==10&&frees==3&&log_[9].f==G(0x471bc8)&&log_[9].a[1]==O(5)&&log_[8].f==DTOR2&&log_[8].a[0]==O(5));
    CHECK(v==((uint64_t)HEAD<<32|(O(5)-8))&&r16(0,L+4)==0);
    /* the destructor of O(1) replaces its entry (reread: pool_free gets
       O(6)) and the array pointer (not reread: round 2 reads ITEMS+4) */
    reset();w32(0,ITEMS,O(1));w32(0,ITEMS+4,O(2));w32(0,OBJ+0xa04,O(7));w16(0,L+4,2);swap_on=O(1);
    v=es2_removal_flush_registers(&memory,&g,L,P,entry,&ecx);
    CHECK(calls==7&&log_[3].a[1]==O(6)&&log_[4].a[0]==O(2)&&log_[6].a[1]==O(2)&&r32(0,L)==OBJ+0xa00);
}
static void free_checks(void){
    uint32_t ecx=0;reset();
    /* used list 0 <-> 1 <-> 2 (head 0), free list 4; free the middle */
    w16(0,P,1);w16(0,P+2,3);w32(0,P+0xe,NODE(0));w32(0,P+0xa,NODE(4));
    w32(0,NODE(0),NODE(1));w32(0,NODE(1),NODE(2));w32(0,NODE(1)+4,NODE(0));w32(0,NODE(2)+4,NODE(1));
    w32(0,NODE(4),0);w32(0,NODE(4)+4,0);
    uint64_t v=es2_pool_free_registers(&memory,P,O(1),&ecx);
    CHECK(v==NODE(1)&&ecx==NODE(4)&&r16(0,P)==2&&r16(0,P+2)==2);
    CHECK(r32(0,NODE(0))==NODE(2)&&r32(0,NODE(2)+4)==NODE(0)&&r32(0,P+0xe)==NODE(0));
    CHECK(r32(0,P+0xa)==NODE(1)&&r32(0,NODE(1))==NODE(4)&&r32(0,NODE(1)+4)==0&&r32(0,NODE(4)+4)==NODE(1));
    /* the head with no next, empty free list: P+0Eh <- 0, ECX = 0 */
    reset();w16(0,P,0);w16(0,P+2,0);w32(0,P+0xe,NODE(2));w32(0,P+0xa,0);w32(0,NODE(2),0);w32(0,NODE(2)+4,0);
    v=es2_pool_free_registers(&memory,P,O(2),&ecx);
    CHECK(v==NODE(2)&&ecx==0&&r16(0,P)==1&&r16(0,P+2)==0xffff&&r32(0,P+0xe)==0&&r32(0,P+0xa)==NODE(2)&&r32(0,NODE(2))==0);
    /* aliasing: prev = N (the first store hits N+0 itself), next = N */
    reset();w32(0,NODE(3),NODE(3));w32(0,NODE(3)+4,NODE(3));w32(0,P+0xa,NODE(3));
    v=es2_pool_free_registers(&memory,P,O(3),&ecx);
    CHECK(v==NODE(3)&&ecx==NODE(3)&&r32(0,NODE(3))==NODE(3)&&r32(0,NODE(3)+4)==0&&r32(0,P+0xa)==NODE(3));
}
static void append_checks(void){
    uint32_t ecx=0;reset();w16(0,L+4,2);
    uint64_t v=es2_removal_append_registers(&memory,L,0xabcd1234u,&ecx);
    CHECK(v==((uint64_t)0xabcd1234u<<32|ITEMS)&&ecx==2&&r16(0,L+4)==3&&r32(0,ITEMS+8)==0xabcd1234u);
    reset();w16(0,L+4,0xffff);v=es2_removal_append_registers(&memory,L,7,&ecx);
    CHECK(ecx==0xffffffffu&&r16(0,L+4)==0&&r32(0,ITEMS-4)==7&&v==((uint64_t)7<<32|ITEMS));
    /* the array is the header itself: the store at index 1 lands on the
       incremented count, so the array pointer is read after the count */
    reset();w32(0,L,L);w16(0,L+4,1);v=es2_removal_append_registers(&memory,L,0x00050009u,&ecx);
    CHECK(ecx==1&&r32(0,L+4)==0x00050009u&&v==((uint64_t)0x00050009u<<32|L));
    reset();w32(0,L,L+2);w16(0,L+4,0);v=es2_removal_append_registers(&memory,L,0x44443333u,&ecx);
    CHECK(ecx==0&&r32(0,L)==0x33330000u+(uint32_t)((L+2)&0xffff)&&r16(0,L+4)==0x4444&&v==((uint64_t)0x44443333u<<32|(L+2)));
}
int main(void){
    flush_checks();flush_rereads();free_checks();append_checks();
    printf("PASS: removal %u checks\n",checks);return 0;
}
