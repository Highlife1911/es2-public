#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/flyer_part.h"
/* Synthetic guest: the FLYER, two sources with their vtables and a
 * trigger record at 90000000h, an image base I with the globals 0x4a9660
 * and 0x4d24e6. Every game call is logged with its arguments, entry
 * registers and the value of [0x4a9660] at the call; the stubs return
 * planned registers. The expectations are read from the reference listing
 * (hit_tests.md, "FLYER part damage"). */
#define OBJ UINT32_C(0x90000000)
#define I UINT32_C(0x10000000)
#define G(va) (I+(va)-0x400000u)
static uint8_t arena[0x1000],globals1[0x10],globals2[0x10];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"flyer_part line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-G(0x4a9660)<sizeof(globals1))return globals1+(a-G(0x4a9660));
    if(a-G(0x4d24e0)<sizeof(globals2))return globals2+(a-G(0x4d24e0));
    fprintf(stderr,"flyer_part: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
#define F (OBJ+0x100)
#define SRC (OBJ+0x400)
#define SRC2 (OBJ+0x600)
#define VT (OBJ+0x800)
#define VT2 (OBJ+0x880)
#define TRIG (OBJ+0x900)
#define S60 UINT32_C(0x50000060)
#define S60B UINT32_C(0x51000060)
typedef struct logged { uint32_t f,a[3],r[3],velocity; unsigned n; uint8_t dead,flag96,flag38; uint32_t f2e; } logged;
static logged log_[16];static unsigned calls;
static uint16_t region_result;
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<16);logged *l=log_+calls++;l->f=f;l->n=n;if(n)memcpy(l->a,a,4*n);l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    l->velocity=r32(0,G(0x4a9660));l->dead=arena[F-OBJ+0x99];l->flag96=arena[F-OBJ+0x96];l->flag38=arena[F-OBJ+0x38];l->f2e=r32(0,F+0x2e);
    if(f==G(0x40da38)){CHECK(n==3);r->eax=0xda310000u|region_result;r->edx=0xda32u;r->ecx=0xda33u;}
    else if(f==G(0x420a68)){CHECK(n==1);r->eax=0x0a61u;r->edx=0x0a62u;r->ecx=0x0a63u;}
    else if(f==S60||f==S60B){CHECK(n==3);r->eax=0x6001u;r->edx=0x6002u;r->ecx=0x6003u;}
    else if(f==G(0x411bc8)){CHECK(n==1);r->eax=0x1bc1u;r->edx=0x1bc2u;r->ecx=0x1bc3u;
        /* The trigger pointer and F+6 are read after this call. */
        w32(0,F+0x1b6,TRIG);}
    else if(f==G(0x423430)){CHECK(n==1);r->eax=0x3431u;r->edx=0x3432u;r->ecx=0x3433u;}
    else if(f==G(0x413e50)){CHECK(n==2);r->eax=0x3e51u;r->edx=0x3e52u;r->ecx=0x3e53u;w16(0,F+6,0x0101);}
    else if(f==G(0x462ac4)){CHECK(n==1);r->eax=0x2ac1u;r->edx=0x2ac2u;r->ecx=0x2ac3u;}
    else {fprintf(stderr,"flyer_part: unexpected call %08x\n",f);exit(1);}
}
static es2_weapon_graph graph(void){es2_weapon_graph g={I,0,0,0,0,call,0,0,0};return g;}
static const uint32_t entry[3]={0x11110001u,0x22220002u,0x33330003u};
static void reset(void){
    memset(arena,0,sizeof(arena));memset(globals1,0x77,sizeof(globals1));memset(globals2,0,sizeof(globals2));calls=0;
    w32(0,SRC,VT);w32(0,VT+0x60,S60);w32(0,SRC2,VT2);w32(0,VT2+0x60,S60B);
    w32(0,G(0x4d24e6),SRC);w32(0,SRC+0x1a4,F);w32(0,F+0x2e,0x12345678u);arena[F-OBJ+0x96]=7;
}
/* No destroyed region: one call, its EDX:EAX, the velocity global cleared. */
static void survive_checks(void){
    es2_weapon_graph g=graph();reset();region_result=0;
    uint64_t v=es2_flyer_part_registers(&memory,&g,F,0,0xabcd0123u,SRC,entry);
    CHECK(calls==1&&log_[0].f==G(0x40da38));
    CHECK(log_[0].a[0]==F+0x200&&log_[0].a[1]==0&&log_[0].a[2]==0x22220123u);
    CHECK(log_[0].r[0]==F+0x24f&&log_[0].r[1]==0x22220123u&&log_[0].r[2]==F+0x200&&log_[0].velocity==F+0x24f);
    CHECK(v==((uint64_t)0xda32u<<32|0xda310000u)&&r32(0,G(0x4a9660))==0&&arena[F-OBJ+0x99]==0);
}
/* A destroyed region of part 1: EAX = s16(part) - 1, nothing else. */
static void other_part_checks(void){
    es2_weapon_graph g=graph();reset();region_result=1;
    uint64_t v=es2_flyer_part_registers(&memory,&g,F,0x7fff0001u,5,SRC,entry);
    CHECK(calls==1&&log_[0].a[1]==0x7fff0001u&&log_[0].a[2]==0x22220005u);
    CHECK(v==((uint64_t)0xda32u<<32|0)&&r32(0,G(0x4a9660))==0);
    reset();region_result=0x8000;
    v=es2_flyer_part_registers(&memory,&g,F,0xffff,5,SRC,entry);
    CHECK(calls==1&&v==((uint64_t)0xda32u<<32|0xfffffffeu));
}
/* The player kills its target (part dword 10000h, low word 0): message
 * 2Eh, slot +60h, the death bytes, death_actions, trigger_fire (pointer set
 * by the death_actions stub), script_start, the sound (F+6 set by the
 * script_start stub). */
static void player_kill_checks(void){
    es2_weapon_graph g=graph();reset();region_result=0x0100;
    uint64_t v=es2_flyer_part_registers(&memory,&g,F,0x10000u,0xffff,SRC,entry);
    CHECK(calls==7);
    CHECK(log_[1].f==G(0x420a68)&&log_[1].a[0]==0x2e&&log_[1].r[0]==SRC&&log_[1].r[1]==0xda32u&&log_[1].r[2]==0xda33u);
    CHECK(log_[2].f==S60&&log_[2].a[0]==SRC&&log_[2].a[1]==F&&log_[2].a[2]==0);
    CHECK(log_[2].r[0]==0x0a61u&&log_[2].r[1]==VT&&log_[2].r[2]==0x2e&&log_[2].dead==0&&log_[2].velocity==F+0x24f);
    CHECK(log_[3].f==G(0x411bc8)&&log_[3].a[0]==F&&log_[3].r[0]==F+0x38&&log_[3].r[1]==F+0x92&&log_[3].r[2]==0x6003u);
    CHECK(log_[3].dead==1&&log_[3].flag96==0&&log_[3].flag38==1&&log_[3].f2e==0xfffe7960u);
    CHECK(log_[4].f==G(0x423430)&&log_[4].a[0]==TRIG&&log_[4].r[0]==TRIG&&log_[4].r[1]==0x1bc2u&&log_[4].r[2]==F);
    CHECK(log_[5].f==G(0x413e50)&&log_[5].a[0]==F+0x4d&&log_[5].a[1]==G(0x499e60));
    CHECK(log_[5].r[0]==0x3431u&&log_[5].r[1]==F+0x4d&&log_[5].r[2]==TRIG);
    CHECK(log_[6].f==G(0x462ac4)&&log_[6].a[0]==0x31&&log_[6].r[0]==0x3e51u&&log_[6].r[1]==0x3e52u&&log_[6].r[2]==0x3e53u);
    CHECK(r16(0,F+6)==0&&r32(0,G(0x4a9660))==0);
    CHECK(v==((uint64_t)0x2ac2u<<32|0x2ac1u));
}
/* No source: no message, no slot; death_actions sees 40da38's ECX. The
 * stubs set the trigger pointer and F+6, so trigger_fire and the sound
 * run; the result is the sound's EDX:EAX. */
static void sourceless_checks(void){
    es2_weapon_graph g=graph();reset();region_result=1;
    uint64_t v=es2_flyer_part_registers(&memory,&g,F,0,7,0,entry);
    CHECK(calls==5&&log_[1].f==G(0x411bc8)&&log_[1].r[0]==F+0x38&&log_[1].r[1]==F+0x92&&log_[1].r[2]==0xda33u);
    CHECK(log_[2].f==G(0x423430)&&log_[3].f==G(0x413e50)&&log_[4].f==G(0x462ac4));
    CHECK(v==((uint64_t)0x2ac2u<<32|0x2ac1u));
}
/* Another source: no message (not the player), slot +60h of its own vtable
 * with EAX = the player pointer and ECX = 40da38's. The player whose target
 * is not F: no message either. */
static void other_source_checks(void){
    es2_weapon_graph g=graph();reset();region_result=1;
    es2_flyer_part_registers(&memory,&g,F,0,7,SRC2,entry);
    CHECK(log_[1].f==S60B&&log_[1].a[0]==SRC2&&log_[1].r[0]==SRC&&log_[1].r[1]==VT2&&log_[1].r[2]==0xda33u);
    reset();region_result=1;w32(0,SRC+0x1a4,SRC2);
    es2_flyer_part_registers(&memory,&g,F,0,7,SRC,entry);
    CHECK(log_[1].f==S60&&log_[1].r[0]==SRC&&log_[1].r[1]==VT&&log_[1].r[2]==0xda33u);
}
int main(void){
    survive_checks();other_part_checks();player_kill_checks();sourceless_checks();other_source_checks();
    printf("flyer_part: %u checks passed\n",checks);
    return 0;
}
