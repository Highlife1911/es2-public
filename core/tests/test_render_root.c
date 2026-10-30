#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/render_root.h"
/* Synthetic guest: objects at 90000000h, the frame and its argument slots
 * on a stack page at A0000000h, the image data at B (globals relocated).
 * Calls are logged with their target, slot, arguments, entry registers and
 * the frame the core names; they return planned registers and may change
 * what the core reads next. */
#define OBJ UINT32_C(0x90000000)
#define STACK UINT32_C(0xa0000000)
#define B UINT32_C(0x10000000)
#define G(va) (B+(va)-0x400000u)
#define IMAGE_LO UINT32_C(0x497000)
#define IMAGE_HI UINT32_C(0x4d3000)
#define H (OBJ+0x100)
#define CAM (OBJ+0x400)
#define CTX (OBJ+0x600)
#define CTX2 (OBJ+0x700)
#define JOLT (OBJ+0x900)
#define HUDO (OBJ+0xa00)
#define PORT (OBJ+0xa40)
#define EVT (OBJ+0xa80)
#define VT (OBJ+0xc00)
static uint8_t arena[8192],stack[256],image[IMAGE_HI-IMAGE_LO],fire_globals[16];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"render_root line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-STACK<sizeof(stack))return stack+(a-STACK);
    if(a-G(IMAGE_LO)<sizeof(image))return image+(a-G(IMAGE_LO));
    if(a-G(0x6b4f30)<sizeof(fire_globals))return fire_globals+(a-G(0x6b4f30));
    fprintf(stderr,"render_root: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static uint8_t *b8(uint32_t va){return host(0,G(va));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
typedef struct logged { uint32_t f,a[3],r[3],fr[4],extra[2]; unsigned n,slot; } logged;
static logged log_[48];static unsigned calls,observed;
static es2_render_root *active;static es2_weapon_frame *dispatch_frame;
static uint32_t view_al,slot_after;
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<48);logged *l=log_+calls;l->f=f;l->n=n;if(n)memcpy(l->a,a,4*n);
    l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    if(active){l->fr[0]=active->call.ebx;l->fr[1]=active->call.esi;l->fr[2]=active->call.edi;l->fr[3]=active->call.ret;}
    if(dispatch_frame){l->fr[0]=dispatch_frame->ebx;l->fr[1]=dispatch_frame->esi;l->fr[2]=dispatch_frame->edi;l->fr[3]=dispatch_frame->ret;}
    if(active&&active->top&&n==1)w32(0,active->top-4,slot_after+calls);
    r->eax=0x1000u+calls;r->edx=0x2000u+calls;r->ecx=0x3000u+calls;
    if(f==G(0x42d870))r->eax=JOLT;
    if(f==G(0x42db18))r->eax=view_al;
    if(f==G(0x433158))r->eax=PORT;
    if(f==G(0x434520)){l->extra[0]=r32(0,a[1]);l->extra[1]=r32(0,a[1]+4);}
    /* callee effects seen by the next reads */
    if(f==G(0x45d490))w16(0,G(0x4d24d2),0x7777);
    if(f==G(0x41b130))w32(0,G(0x4d24ce),CTX2);
    if(f==G(0x46cb9c)&&calls>8)w32(0,G(0x4d24ea),CAM+0x40);
    ++calls;
}
static void observe(void *c,uint32_t f,unsigned slot){(void)c;(void)f;CHECK(observed==calls);log_[observed++].slot=slot;}
static es2_weapon_graph graph(uint32_t scratch){es2_weapon_graph g={B,scratch,0,0,0,call,0,observe,0};return g;}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
static void reset(void){calls=observed=0;memset(log_,0,sizeof(log_));}
static void build(void){
    memset(arena,0,sizeof(arena));memset(image,0,sizeof(image));memset(stack,0,sizeof(stack));
    *b8(0x4d1f41)=3;w16(0,G(0x4d24d4),3);          /* detail 1 unchanged */
    *b8(0x4d1f42)=2;w16(0,G(0x4d256f),9);          /* detail 2 changed */
    w32(0,G(0x4d24ce),CTX);w32(0,CTX+0x220,1000);w32(0,CTX+0x224,2000);w32(0,CTX2+0x220,50);w32(0,CTX2+0x224,60);
    w32(0,JOLT,7);w32(0,JOLT+4,0xfffffffdu);
    w32(0,G(0x4d24ea),CAM);w32(0,G(0x4d24e6),OBJ+0x800);w32(0,G(0x4a0be0),0x4a0b0000u);w32(0,G(0x4a9608),OBJ+0xb00);
    *b8(0x4d24c5)=0x81;
    w32(0,G(0x49b088),H);w32(0,H+0x1f5,HUDO);w32(0,H+0x20b,EVT);
    w32(0,HUDO,VT);w32(0,PORT,VT+0x20);w32(0,EVT,VT+0x40);
    w32(0,VT+8,0xc0000008u);w32(0,VT+0x20+0x10,0xc0000010u);w32(0,VT+0x40+0x10,0xc0000020u);
    w32(0,G(0x4d22c8),0x12345678u);w32(0,G(0x4d22cc),0x9abcdef0u);
    w16(0,G(0x497074),0);
}
static void render_root(void){
    uint32_t ecx;es2_weapon_graph g;uint64_t p;es2_render_root fr={{0,0,0,0},0};
    /* Plain bridge, the 3D view on: 401d94, 45d50c (only detail 2 changed),
     * 42d870, 48c2e4, 46cb9c, 4327ac, 42841c, 42db18, 48c2e4, 40707c,
     * 46cb9c, 48c444, 42e700, 42883c, 41b130, three virtuals, 434520. */
    build();reset();view_al=0x12345601u;active=&fr;g=graph(OBJ+0x1000);
    p=es2_render_root_registers(&memory,&g,&fr,0x77,0x88,entry,&ecx);active=NULL;
    CHECK(calls==20);
    CHECK(log_[0].f==G(0x401d94)&&log_[0].n==1&&log_[0].a[0]==5&&log_[0].r[0]==entry[0]&&log_[0].r[1]==entry[1]&&log_[0].r[2]==entry[2]);
    CHECK(log_[0].fr[0]==G(0x4d24bc)&&log_[0].fr[1]==0x77&&log_[0].fr[2]==0x88&&log_[0].fr[3]==G(0x45fcb8));
    CHECK(r16(0,G(0x4d24d2))==3&&r16(0,G(0x4d24d4))==3&&r16(0,G(0x4d256d))==2&&r16(0,G(0x4d256f))==2);
    CHECK(log_[1].f==G(0x45d50c)&&log_[1].a[0]==2&&log_[1].r[0]==2&&log_[1].r[1]==0x2000u&&log_[1].r[2]==5);
    CHECK(log_[2].f==G(0x42d870)&&log_[2].n==0&&log_[2].r[0]==0x1001u&&log_[2].r[1]==((0x2001u&0xffff0000u)|2u)&&log_[2].r[2]==2);
    CHECK(r32(0,OBJ+0x1000)==7&&r32(0,OBJ+0x1004)==0xfffffffdu);
    CHECK(log_[3].f==G(0x48c2e4)&&log_[3].a[0]==CAM&&log_[3].r[0]==CAM&&log_[3].r[1]==7&&log_[3].r[2]==0xfffffffdu);
    CHECK(log_[4].f==G(0x46cb9c)&&log_[4].n==3&&log_[4].a[0]==0x4a0b0000u&&log_[4].a[1]==CAM&&log_[4].a[2]==0&&log_[4].r[0]==0x1003u&&log_[4].r[1]==CAM&&log_[4].r[2]==0x4a0b0000u);
    CHECK(log_[5].f==G(0x4327ac)&&log_[5].a[0]==H&&log_[5].r[0]==H&&log_[5].r[1]==0x2004u&&log_[5].r[2]==0x3004u);
    CHECK(log_[6].f==G(0x42841c)&&log_[6].r[0]==0x1005u&&log_[6].r[2]==H);
    CHECK(log_[7].f==G(0x42db18));
    CHECK(log_[8].f==G(0x48c2e4)&&log_[8].a[0]==CAM&&log_[8].r[0]==0x12345601u&&log_[8].r[1]==CAM&&log_[8].r[2]==0x3007u);
    CHECK(log_[9].f==G(0x40707c)&&log_[9].n==2&&log_[9].a[0]==OBJ+0xb00&&log_[9].a[1]==CAM+4&&log_[9].r[0]==CAM+4&&log_[9].r[1]==OBJ+0xb00&&log_[9].r[2]==CAM);
    CHECK(log_[10].f==G(0x46cb9c)&&log_[10].a[0]==0x4a0b0000u&&log_[10].a[1]==CAM&&log_[10].r[0]==0x4a0b0000u&&log_[10].r[1]==0x2009u&&log_[10].r[2]==CAM);
    /* 46cb9c moved the camera (call 10): the later reads see it. */
    CHECK(log_[11].f==G(0x48c444)&&log_[11].a[0]==0&&log_[11].r[0]==0x100au&&log_[11].r[2]==0x300au);
    CHECK(log_[12].f==G(0x42e700)&&log_[12].a[0]==CAM+0x40&&log_[12].r[1]==CAM+0x40&&log_[12].r[2]==0);
    CHECK(log_[13].f==G(0x42883c)&&log_[13].a[0]==CAM+0x40&&log_[13].a[1]==0x81&&log_[13].r[0]==CAM+0x40&&log_[13].r[1]==0x200cu&&log_[13].r[2]==0x81);
    CHECK(log_[14].f==G(0x41b130)&&log_[14].a[0]==OBJ+0x800&&log_[14].r[1]==OBJ+0x800&&log_[14].fr[3]==G(0x45fdc4));
    CHECK(log_[15].f==0xc0000008u&&log_[15].slot==8&&log_[15].a[0]==HUDO&&log_[15].r[0]==HUDO&&log_[15].r[1]==VT&&log_[15].r[2]==H);
    CHECK(log_[16].f==G(0x433158)&&log_[16].r[2]==HUDO);
    CHECK(log_[17].f==0xc0000010u&&log_[17].slot==0x10&&log_[17].a[0]==PORT&&log_[17].r[0]==PORT&&log_[17].r[1]==0x2010u&&log_[17].r[2]==VT+0x20);
    CHECK(log_[18].f==0xc0000020u&&log_[18].slot==0x10&&log_[18].a[0]==EVT&&log_[18].r[0]==H&&log_[18].r[1]==EVT&&log_[18].r[2]==VT+0x40);
    /* The jolt went to the first context and was taken off the second
     * (41b130 switched it); the cursor copy reaches 434520. */
    CHECK(r32(0,CTX+0x220)==1007u&&r32(0,CTX+0x224)==1997u&&r32(0,CTX2+0x220)==43u&&r32(0,CTX2+0x224)==63u);
    CHECK(log_[18].fr[3]==G(0x45fdf5));
    CHECK(r32(0,OBJ+0x1008)==0x12345678u&&r32(0,OBJ+0x100c)==0x9abcdef0u);
    CHECK(log_[19].f==G(0x434520)&&p==((uint64_t)(0x2013u)<<32|0x1013u)&&ecx==0x3013u);
    reset();build();view_al=0x100u;active=&fr;
    p=es2_render_root_registers(&memory,&g,&fr,0,0,entry,&ecx);active=NULL;
    /* The 3D view off (AL 0): 401d94, 45d50c, 42d870, 48c2e4, 46cb9c,
     * 4327ac, 42841c, 42db18, 41b130, three virtuals, 434520. */
    CHECK(calls==14&&log_[8].f==G(0x41b130)&&log_[8].r[0]==0x100u&&log_[8].r[2]==0x3007u);
    CHECK(log_[13].f==G(0x434520)&&log_[13].n==2&&log_[13].a[0]==H&&log_[13].a[1]==OBJ+0x1008&&log_[13].r[0]==H&&log_[13].r[1]==0x9abcdef0u&&log_[13].r[2]==OBJ+0x1008);
    CHECK(log_[13].extra[0]==0x12345678u&&log_[13].extra[1]==0x9abcdef0u&&log_[13].fr[3]==G(0x45fe2d));
    /* Detail 1 changed: 45d490 rewrites the word it is copied from. */
    reset();build();*b8(0x4d1f41)=4;*b8(0x4d1f42)=9;view_al=0;active=&fr;
    p=es2_render_root_registers(&memory,&g,&fr,0,0,entry,&ecx);active=NULL;
    CHECK(calls==14&&log_[1].f==G(0x45d490)&&log_[1].a[0]==4&&log_[1].r[0]==4&&r16(0,G(0x4d24d4))==0x7777);
    CHECK(log_[2].f==G(0x42d870)&&log_[2].r[0]==9&&log_[2].r[1]==((0x2001u&0xffff0000u)|0x7777u)&&log_[2].r[2]==4);
    /* At the original coordinate: ECX after a one-slot call is the slot the
     * callee left, and the frame is the original's locals. */
    reset();build();view_al=1;fr.top=STACK+0x80;g=graph(STACK+0x80);slot_after=0x5a000000u;active=&fr;
    p=es2_render_root_registers(&memory,&g,&fr,0x77,0x88,entry,&ecx);active=NULL;
    CHECK(calls==20&&log_[1].r[2]==slot_after+0&&log_[3].r[2]==0xfffffffdu&&log_[4].r[0]==0x1003u);
    CHECK(log_[6].r[2]==slot_after+5&&log_[9].r[2]==slot_after+8&&log_[12].r[2]==slot_after+11&&log_[13].r[1]==0x200cu);
    CHECK(log_[16].r[2]==slot_after+15&&r32(0,STACK+0x80)==7&&r32(0,STACK+0x88)==0x12345678u&&log_[19].a[1]==STACK+0x88);
    CHECK(ecx==0x3013u);
}
static void dispatch(void){
    es2_weapon_frame fr={0,0,0,0};es2_weapon_graph g=graph(0);uint64_t p;
    /* Five entries: stages 5, 6, 5 (low word of 10005h), 4, 5; the count
     * word 5. Stage 10005h matches the 5s by its low word. */
    build();reset();
    static const uint16_t st[5]={5,6,5,4,5};
    for(unsigned i=0;i<5;++i){w32(0,G(0x4a804c)+6*i,0x400000u+0x1000u*i);w16(0,G(0x4a804c)+6*i+4,st[i]);}
    w16(0,G(0x497074),5);dispatch_frame=&fr;
    p=es2_stage_dispatch_registers(&memory,&g,&fr,0x10005u,entry);
    CHECK(calls==3&&log_[0].f==0x400000u&&log_[1].f==0x402000u&&log_[2].f==0x404000u&&log_[0].n==0);
    CHECK(log_[0].r[0]==entry[0]&&log_[0].r[1]==entry[1]&&log_[0].r[2]==entry[2]&&log_[1].r[0]==0x1000u&&log_[2].r[2]==0x3001u);
    CHECK(log_[0].fr[0]==G(0x4a804c)&&log_[0].fr[1]==0&&log_[0].fr[2]==0x10005u&&log_[0].fr[3]==G(0x401dae));
    CHECK(log_[2].fr[0]==G(0x4a804c)+24&&log_[2].fr[1]==4);
    CHECK(p==((uint64_t)0x2002u<<32|0x1002u));
    /* No match: the entry pair. A negative count runs nothing. */
    reset();p=es2_stage_dispatch_registers(&memory,&g,&fr,7u,entry);
    CHECK(calls==0&&p==((uint64_t)entry[1]<<32|entry[0]));
    w16(0,G(0x497074),0x8000u);reset();p=es2_stage_dispatch_registers(&memory,&g,&fr,5u,entry);
    CHECK(calls==0);
    dispatch_frame=NULL;
}
static void copy(void){
    uint32_t ecx;memset(arena,0,sizeof(arena));
    for(unsigned i=0;i<6;++i)w32(0,OBJ+4*i,0x100u+i);
    /* Overlap: D = S+4 copies +8 first, then +0, then +4 (reading the
     * stored +4... which is D+0). */
    uint64_t p=es2_vector_copy_registers(&memory,OBJ+4,OBJ,&ecx);
    CHECK((uint32_t)p==OBJ+4&&ecx==0x100u&&(uint32_t)(p>>32)==0x100u);
    CHECK(r32(0,OBJ+4)==0x100u&&r32(0,OBJ+8)==0x100u&&r32(0,OBJ+12)==0x102u);
    p=es2_vector_copy_registers(&memory,OBJ+0x40,OBJ+16,&ecx);
    CHECK(r32(0,OBJ+0x40)==0x104u&&r32(0,OBJ+0x44)==0x105u&&r32(0,OBJ+0x48)==0u&&ecx==0x104u&&(uint32_t)(p>>32)==0x105u);
}
/* The twelve flushes. Pool k holds 0x500+k, its list 0x600+k;
 * the logged calls name removal_flush(list, pool) with EAX = pool, EDX =
 * list and the entry ECX; 408f9c then calls 408f84 with the flush's
 * registers; 46b188 then writes 6b4f3c. */
static void flushes(void){
    es2_weapon_graph g=graph(0);uint32_t ecx;
    build();memset(fire_globals,0,sizeof(fire_globals));
    for(unsigned k=0;k<ES2_STAGE_FLUSHES;++k){
        const uint32_t pool=es2_stage_flush_pool(k);
        w32(0,G(pool),0x500u+k);w32(0,G(pool+4u),0x600u+k);
    }
    CHECK(es2_stage_flush_va(0)==0x405ea4u&&es2_stage_flush_va(11)==0x46b188u&&es2_stage_flush_pool(4)==0x4a966eu);
    for(unsigned k=0;k<ES2_STAGE_FLUSHES;++k){
        reset();uint64_t p=es2_stage_flush_registers(&memory,&g,k,entry,&ecx);
        const uint32_t va=es2_stage_flush_va(k);
        CHECK(log_[0].f==G(0x478644)&&log_[0].n==2&&log_[0].a[0]==0x600u+k&&log_[0].a[1]==0x500u+k&&log_[0].slot==0);
        CHECK(log_[0].r[0]==0x500u+k&&log_[0].r[1]==0x600u+k&&log_[0].r[2]==entry[2]);
        if(va==0x408f9cu){
            CHECK(calls==2&&log_[1].f==G(0x408f84)&&log_[1].n==0&&log_[1].r[0]==0x1000u&&log_[1].r[1]==0x2000u&&log_[1].r[2]==0x3000u);
            CHECK(p==((uint64_t)0x2001u<<32|0x1001u)&&ecx==0x3001u);
        } else {
            CHECK(calls==1&&p==((uint64_t)0x2000u<<32|0x1000u)&&ecx==0x3000u);
        }
        CHECK(r32(0,G(0x6b4f3c))==(va==0x46b188u?0x7fffffffu:0u));
    }
    reset();uint64_t p=es2_stage_flush_registers(&memory,&g,12,entry,&ecx);
    CHECK(calls==0&&p==((uint64_t)entry[1]<<32|entry[0])&&ecx==entry[2]);
}
int main(void){
    render_root();dispatch();copy();flushes();
    printf("PASS: render_root %u checks\n",checks);
    return 0;
}
