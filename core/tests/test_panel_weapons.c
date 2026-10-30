#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/panel_weapons.h"
/* Synthetic guest: objects at 90000000h, the image global 4d2528 at
 * B+D2528h. The game callees (410708, 4106ac, the weapon virtual +58h) are
 * logged with their arguments and entry registers and return planned
 * registers. */
#define OBJ UINT32_C(0x90000000)
#define B UINT32_C(0x10000000)
#define WORD (B+0xd2528u)
static uint8_t arena[4096],word[2];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"panel_weapons line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-WORD<sizeof(word))return word+(a-WORD);
    fprintf(stderr,"panel_weapons: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
typedef struct logged { uint32_t f,a[2],r[3]; unsigned n,slot; } logged;
static logged log_[16];static unsigned calls,observed;
static uint32_t ret[16][3],last_slot;
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<16&&n==2);logged *l=log_+calls;l->f=f;l->n=n;l->slot=last_slot;memcpy(l->a,a,8);
    l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    r->eax=ret[calls][0];r->edx=ret[calls][1];r->ecx=ret[calls][2];++calls;
}
static void observe(void *c,uint32_t f,unsigned slot){(void)c;(void)f;CHECK(observed==calls);last_slot=slot;++observed;}
static es2_weapon_graph graph(void){es2_weapon_graph g={B,0,0,0,0,call,0,observe,0};return g;}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
static void reset(void){calls=observed=0;memset(ret,0,sizeof(ret));memset(log_,0,sizeof(log_));memset(arena,0,sizeof(arena));}
#define W (OBJ+0x100)
#define LIST (OBJ+0x200)
#define E0 (OBJ+0x400)
#define E1 (OBJ+0x500)
#define D0 (OBJ+0x700)
#define VT (OBJ+0x800)

static void test_select(void){
    uint32_t ecx;reset();
    w32(0,W+4,LIST);w32(0,LIST+8,E0);w32(0,E0+0x18,D0);arena[W-OBJ+0x1d]=2;w16(0,WORD,0x1234);
    /* Already selected: nothing written, ECX = s16 slot. */
    uint64_t r=es2_weapon_select_registers(&memory,B,W,0xabcd0002u,&ecx);
    CHECK((uint32_t)r==W&&r>>32==0xabcd0002u&&ecx==2&&r16(0,WORD)==0x1234);
    /* E+4Bh = 0: ECX = E, slot stored. */
    arena[W-OBJ+0x1d]=0xff;
    r=es2_weapon_select_registers(&memory,B,W,0xabcd0002u,&ecx);
    CHECK((uint32_t)r==W&&r>>32==0xabcd0002u&&ecx==E0&&arena[W-OBJ+0x1d]==2&&r16(0,WORD)==0);
    /* Negative definition byte moves the low word only. */
    arena[W-OBJ+0x1d]=0;arena[E0-OBJ+0x4b]=1;arena[D0-OBJ+0x16]=0xfd;w16(0,WORD,7);
    r=es2_weapon_select_registers(&memory,B,W,0xabcd0002u,&ecx);
    CHECK(r>>32==0xabcdffffu&&ecx==0xfffffffdu&&arena[W-OBJ+0x1d]==0xff&&r16(0,WORD)==0);
    /* Positive definition byte: ECX = the byte, slot kept. */
    arena[W-OBJ+0x1d]=0;arena[D0-OBJ+0x16]=5;
    r=es2_weapon_select_registers(&memory,B,W,0x00010002u,&ecx);
    CHECK(r>>32==0x00010002u&&ecx==5&&arena[W-OBJ+0x1d]==2);
    /* s16 slot -1 equals s8 FFh: nothing to do. */
    arena[W-OBJ+0x1d]=0xff;r=es2_weapon_select_registers(&memory,B,W,0x7777ffffu,&ecx);
    CHECK(ecx==0xffffffffu&&r>>32==0x7777ffffu);
}

static void test_find(void){
    es2_weapon_graph g=graph();uint32_t ecx;reset();
    w32(0,W+4,LIST);w16(0,W+8,3);w32(0,LIST+4,E0);w32(0,LIST+8,E1);w32(0,E0,VT);w32(0,E1,VT);w32(0,VT+0x58,0x40df08u);
    arena[E1-OBJ+0x4c]=1;
    ret[0][0]=0xffff0000u;ret[0][1]=0x77u;ret[0][2]=0x88u;      /* E0: AX = 0 */
    ret[1][0]=0x00000001u;ret[1][1]=0x99u;ret[1][2]=0xaau;      /* E1: accepted */
    ret[2][0]=0x5u;ret[2][1]=0xbbu;ret[2][2]=0xccu;             /* 410708 */
    uint64_t r=es2_weapon_find_registers(&memory,&g,W,0x4242u,entry,&ecx);
    CHECK(calls==3&&log_[0].f==0x40df08u&&log_[0].slot==0x58&&log_[0].a[0]==E0&&log_[0].a[1]==0x4242u);
    CHECK(log_[0].r[0]==VT&&log_[0].r[1]==LIST&&log_[0].r[2]==0x4242u);
    CHECK(log_[2].f==B+0x10708u&&log_[2].slot==0&&log_[2].a[0]==W&&log_[2].a[1]==2);
    CHECK(log_[2].r[0]==E1+0x43u&&log_[2].r[1]==0x99u&&log_[2].r[2]==0xaau);
    CHECK((uint32_t)r==(((W+0x14u)&0xffff0000u)|1u)&&r>>32==0xbbu&&ecx==0xccu&&arena[W-OBJ+0x18]==1);
    /* A match whose E+4Ch is 0 ends the search with EAX = 0. */
    reset();w32(0,W+4,LIST);w16(0,W+8,3);w32(0,LIST+4,E0);w32(0,LIST+8,E1);w32(0,E0,VT);w32(0,E1,VT);w32(0,VT+0x58,0x40df08u);
    ret[0][0]=0x10000u|1u;ret[0][1]=0x55u;ret[0][2]=0x66u;
    r=es2_weapon_find_registers(&memory,&g,W,1,entry,&ecx);
    CHECK(calls==1&&r==(uint64_t)0x55u<<32&&ecx==0x66u&&arena[W-OBJ+0x18]==0);
    /* No slot visited: EDX/ECX from entry; a null last slot: EDX = list. */
    reset();w32(0,W+4,LIST);w16(0,W+8,0x8000u);
    r=es2_weapon_find_registers(&memory,&g,W,1,entry,&ecx);
    CHECK(calls==0&&r==(uint64_t)entry[1]<<32&&ecx==entry[2]);
    w16(0,W+8,2);r=es2_weapon_find_registers(&memory,&g,W,1,entry,&ecx);
    CHECK(calls==0&&r==(uint64_t)LIST<<32&&ecx==entry[2]);
}

static void test_panel(void){
    es2_weapon_graph g=graph();uint32_t ecx;reset();
    const uint32_t H=OBJ+0x10,P=OBJ+0x30;
    w32(0,H+0x203,P);w32(0,P+0x202,W);
    ret[0][0]=0x12340100u;ret[0][1]=0x5u;ret[0][2]=0x6u;
    uint64_t r=es2_panel_weapon_registers(&memory,&g,H,0x77u,entry,&ecx);
    CHECK(calls==1&&log_[0].f==B+0x106acu&&log_[0].a[0]==W&&log_[0].a[1]==0x77u);
    CHECK(log_[0].r[0]==P&&log_[0].r[1]==W&&log_[0].r[2]==entry[2]);
    CHECK((uint32_t)r==0x12340101u&&r>>32==5u&&ecx==6u);
    reset();w32(0,H+0x203,P);w32(0,P+0x202,W);ret[0][0]=0x12340000u;
    r=es2_panel_weapon_registers(&memory,&g,H,0x77u,entry,&ecx);
    CHECK((uint32_t)r==0);
}

int main(void){
    test_select();test_find();test_panel();
    printf("panel_weapons: %u checks passed\n",checks);
    return 0;
}
