#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/sfx.h"
/* Synthetic guest at image base I: the context and gate words
 * 0x49f8ec/0x49f8f4/0x49f8f8, the row table 0x4d2b78 and handle table
 * 0x4d2c78 (16 entries each), rows of 9 bytes at OBJ and a 12-byte
 * scratch for place's view-space position. Every game call is logged with
 * its arguments and entry registers; the stubs return planned registers
 * (rng_bounded: the planned draw in AX over a marked high word). The
 * expectations are read from the reference listing (audio.md, section
 * "sound/clock isolation"). */
#define OBJ UINT32_C(0x90000000)
#define SCRATCH UINT32_C(0x90000800)
#define I UINT32_C(0x10000000)
#define G(va) (I+(va)-0x400000u)
static uint8_t arena[0x900],flags[0x80],tables[0x380];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"sfx line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-G(0x49f8ec)<sizeof(flags))return flags+(a-G(0x49f8ec));
    if(a-G(0x4d2b78)<sizeof(tables))return tables+(a-G(0x4d2b78));
    fprintf(stderr,"sfx: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
#define CTX UINT32_C(0x5c5c0000)
#define ROW(k) (OBJ+0x10u*(k))
typedef struct logged { uint32_t f,a[3],r[3]; unsigned n; } logged;
static logged log_[8];static unsigned calls;
static uint16_t draw,angle;static uint32_t distance,audible,clock_value,lip_word;
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<8);logged *l=log_+calls++;l->f=f;l->n=n;if(n)memcpy(l->a,a,4*n);l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    const uint32_t k=calls;
    r->eax=0xe0000000u|k;r->edx=0xd0000000u|k;r->ecx=0xc0000000u|k;
    if(f==G(0x492f24)){CHECK(n==2);r->eax=0xabcd0000u|draw;}
    else if(f==G(0x46299c)){CHECK(n==2);r->eax=0xe0000000u|audible;}
    else if(f==G(0x48c57c)){CHECK(n==2&&a[1]==SCRATCH);w32(0,SCRATCH,0x111u);w32(0,SCRATCH+4,0x222u);w32(0,SCRATCH+8,0x333u);}
    else if(f==G(0x47de72)){CHECK(n==3);r->eax=distance;}
    else if(f==G(0x47d32c)){CHECK(n==2);r->eax=0x55550000u|angle;}
    else if(f==G(0x467828)){CHECK(n==0);r->eax=clock_value;}
    else if(f==G(0x4634b0)){CHECK(n==1);w16(0,a[0]+8,(uint16_t)lip_word);}
    else if(f!=G(0x464618)&&f!=G(0x464038)&&f!=G(0x48c444)&&f!=G(0x4646dc)&&f!=G(0x464118)&&f!=G(0x46466c)){
        fprintf(stderr,"sfx: unexpected call %08x\n",f);exit(1);}
}
static es2_weapon_graph graph(void){es2_weapon_graph g={I,SCRATCH,0,0,0,call,0,0,0};return g;}
static const uint32_t entry[3]={0x11110001u,0x22220002u,0x33330003u};
/* Rows k = 0..15: volume 80, near 2, far 10 (units of 1024), one
 * variant, volume percent 50; row 12 has three variants. */
static void reset(void){
    memset(arena,0,sizeof(arena));memset(flags,0,sizeof(flags));memset(tables,0,sizeof(tables));calls=0;
    w32(0,G(0x49f8ec),CTX);w32(0,G(0x49f8f4),1);w32(0,G(0x49f8f8),1);
    for(uint32_t k=0;k<16;++k){
        uint8_t *p=host(0,ROW(k));p[1]=80;p[4]=2;p[5]=10;p[6]=1;p[8]=50;
        w32(0,G(0x4d2b78)+4*k,ROW(k));w32(0,G(0x4d2c78)+4*k,0x00010000u|k);
    }
    arena[0x10*12+6]=3;
}
#define PAIR(eax,edx) ((uint64_t)(edx)<<32|(eax))
/* 80*65000>>16 = 79; 79*50/100 = 39. */
static void play_checks(void){
    es2_weapon_graph g=graph();uint32_t ecx;reset();
    uint64_t v=es2_sfx_play_registers(&memory,&g,5,entry,&ecx);
    CHECK(calls==2&&log_[0].f==G(0x464618)&&log_[0].a[0]==CTX&&log_[0].a[1]==0x10005u&&log_[0].a[2]==39);
    CHECK(log_[0].r[0]==CTX&&log_[0].r[1]==50&&log_[0].r[2]==100);
    CHECK(log_[1].f==G(0x464038)&&log_[1].a[0]==CTX&&log_[1].a[1]==0x10005u);
    CHECK(log_[1].r[0]==0xe0000001u&&log_[1].r[1]==0x10005u&&log_[1].r[2]==CTX);
    CHECK(v==PAIR(0xe0000002u,0xd0000002u)&&ecx==0xc0000002u);
    /* Row 12: one draw from stream B before the gate; draw 2 selects row
     * 14 (its handle and parameters). The gate for row 14 >= 10 is
     * [0x49f8f8]; closed, the volume is 0 but the play still happens. */
    reset();draw=2;w32(0,G(0x49f8f8),0);arena[0x10*14+8]=100;
    v=es2_sfx_play_registers(&memory,&g,12,entry,&ecx);
    CHECK(calls==3&&log_[0].f==G(0x492f24)&&log_[0].a[0]==G(0x4d260b)&&log_[0].a[1]==3);
    CHECK(log_[0].r[0]==ROW(12)&&log_[0].r[1]==entry[1]&&log_[0].r[2]==entry[2]);
    CHECK(log_[1].a[1]==0x1000eu&&log_[1].a[2]==0&&log_[1].r[1]==0&&log_[2].a[1]==0x1000eu);
    CHECK(v==PAIR(0xe0000003u,0xd0000003u)&&ecx==0xc0000003u);
    /* A negative draw (AX = FFFFh) goes one row back; the index below 10
     * uses [0x49f8f4]. */
    reset();draw=0xffff;arena[0x10*12+6]=3;w32(0,G(0x4d2b78)+4*12,ROW(12));
    es2_sfx_play_registers(&memory,&g,12,entry,&ecx);
    CHECK(calls==3&&log_[1].a[1]==0x1000bu&&log_[1].a[2]==39);
    /* Signed index test of the gate: 40000005h wraps to row 5 but is not
     * below 10, so [0x49f8f8] decides (closed). */
    reset();w32(0,G(0x49f8f8),0);
    es2_sfx_play_registers(&memory,&g,0x40000005u,entry,&ecx);
    CHECK(calls==2&&log_[0].a[1]==0x10005u&&log_[0].a[2]==0);
    /* No context: nothing is called, the entry registers come back. */
    reset();w32(0,G(0x49f8ec),0);
    v=es2_sfx_play_registers(&memory,&g,12,entry,&ecx);
    CHECK(calls==0&&v==PAIR(entry[0],entry[1])&&ecx==entry[2]);
}
static void play_at_checks(void){
    es2_weapon_graph g=graph();uint32_t ecx;reset();audible=1;
    uint64_t v=es2_sfx_play_at_registers(&memory,&g,3,0x7777u,entry,&ecx);
    CHECK(calls==2&&log_[0].f==G(0x46299c)&&log_[0].a[0]==3&&log_[0].a[1]==0x7777u);
    CHECK(log_[0].r[0]==ROW(3)&&log_[0].r[1]==0x7777u&&log_[0].r[2]==entry[2]);
    CHECK(log_[1].f==G(0x464038)&&log_[1].a[0]==CTX&&log_[1].a[1]==0x10003u);
    CHECK(log_[1].r[0]==CTX&&log_[1].r[1]==0xd0000001u&&log_[1].r[2]==0x10003u);
    CHECK(v==PAIR(0xe0000002u,0xd0000002u)&&ecx==0xc0000002u);
    /* Not audible (AL = 0 over a nonzero high word): place's registers. */
    reset();audible=0x100;draw=1;
    v=es2_sfx_play_at_registers(&memory,&g,12,0x7777u,entry,&ecx);
    CHECK(calls==2&&log_[1].f==G(0x46299c)&&log_[1].a[0]==13&&log_[1].r[0]==1&&log_[1].r[2]==0xc0000001u);
    CHECK(v==PAIR(0xe0000100u,0xd0000002u)&&ecx==0xc0000002u);
    reset();w32(0,G(0x49f8ec),0);
    v=es2_sfx_play_at_registers(&memory,&g,12,0x7777u,entry,&ecx);
    CHECK(calls==0&&v==PAIR(entry[0],entry[1])&&ecx==entry[2]);
}
/* place: view rebuild, transform, gate, distance, volume, pan. */
static void place_checks(void){
    es2_weapon_graph g=graph();uint32_t ecx;reset();
    /* Between near (2048) and far (10240): 79*(10240-6144)/10240 = 31,
     * 31*50/100 = 15. Angle 1000h < 8000h: pan = -2000h = E000h. */
    distance=6144;angle=0x1000;
    uint64_t v=es2_sfx_place_registers(&memory,&g,4,0x7777u,entry,&ecx);
    CHECK(calls==6&&log_[0].f==G(0x48c444)&&log_[0].a[0]==0&&log_[0].r[0]==entry[0]&&log_[0].r[1]==entry[1]&&log_[0].r[2]==entry[2]);
    CHECK(log_[1].f==G(0x48c57c)&&log_[1].a[0]==0x7777u&&log_[1].r[0]==0xe0000001u&&log_[1].r[1]==SCRATCH&&log_[1].r[2]==0x7777u);
    CHECK(log_[2].f==G(0x47de72)&&log_[2].a[0]==0x111u&&log_[2].a[1]==0x222u&&log_[2].a[2]==0x333u);
    CHECK(log_[2].r[0]==0x111u&&log_[2].r[1]==0x111u&&log_[2].r[2]==4);
    CHECK(log_[3].f==G(0x464618)&&log_[3].a[0]==CTX&&log_[3].a[1]==0x10004u&&log_[3].a[2]==15);
    CHECK(log_[3].r[0]==0x10004u&&log_[3].r[1]==CTX&&log_[3].r[2]==100);
    CHECK(log_[4].f==G(0x47d32c)&&log_[4].a[0]==0x111u&&log_[4].a[1]==0x222u);
    CHECK(log_[4].r[0]==0x111u&&log_[4].r[1]==0xd0000004u&&log_[4].r[2]==0x111u);
    CHECK(log_[5].f==G(0x4646dc)&&log_[5].a[0]==CTX&&log_[5].a[1]==0x10004u&&log_[5].a[2]==0xe000u);
    CHECK(log_[5].r[0]==CTX&&log_[5].r[1]==0xe000u&&log_[5].r[2]==0x10004u);
    CHECK(v==PAIR(1,0xd0000006u)&&ecx==0xc0000006u);
    /* Angle C000h >= 8000h: pan = 8000h (doubled, no negation). Inside
     * the near distance: the full 79 -> 39. */
    reset();distance=100;angle=0xc000;
    es2_sfx_place_registers(&memory,&g,4,0x7777u,entry,&ecx);
    CHECK(calls==6&&log_[3].a[2]==39&&log_[5].a[2]==0x8000u);
    /* Exactly at near: the scaled branch, (10240-2048)*79/10240 = 63 -> 31. */
    reset();distance=2048;angle=0;
    es2_sfx_place_registers(&memory,&g,4,0x7777u,entry,&ecx);
    CHECK(calls==6&&log_[3].a[2]==31&&log_[5].a[2]==0);
    /* Beyond far: volume 0, no pan, EAX = 0, the volume call's EDX/ECX. */
    reset();distance=10241;
    v=es2_sfx_place_registers(&memory,&g,4,0x7777u,entry,&ecx);
    CHECK(calls==4&&log_[3].a[2]==0&&v==PAIR(0,0xd0000004u)&&ecx==0xc0000004u);
    /* Exactly at far: audible with level 0 -> no pan either. */
    reset();distance=10240;
    v=es2_sfx_place_registers(&memory,&g,4,0x7777u,entry,&ecx);
    CHECK(calls==4&&v==PAIR(0,0xd0000004u));
    /* Gate closed: no distance, the volume call with 0. */
    reset();w32(0,G(0x49f8f4),0);
    v=es2_sfx_place_registers(&memory,&g,4,0x7777u,entry,&ecx);
    CHECK(calls==3&&log_[2].f==G(0x464618)&&log_[2].a[2]==0&&log_[2].r[2]==100&&v==PAIR(0,0xd0000003u));
    /* No context: EAX = 0, entry EDX/ECX. */
    reset();w32(0,G(0x49f8ec),0);
    v=es2_sfx_place_registers(&memory,&g,4,0x7777u,entry,&ecx);
    CHECK(calls==0&&v==PAIR(0,entry[1])&&ecx==entry[2]);
}
static void wrapper_checks(void){
    es2_weapon_graph g=graph();uint32_t ecx;reset();audible=1;
    uint64_t v=es2_sfx_refresh_registers(&memory,&g,0x2d,0x7777u,entry,&ecx);
    CHECK(calls==1&&log_[0].f==G(0x46299c)&&log_[0].a[0]==0x2d&&log_[0].a[1]==0x7777u);
    CHECK(log_[0].r[0]==0x7777u&&log_[0].r[1]==0x2d&&log_[0].r[2]==entry[2]);
    CHECK(v==PAIR(0xe0000001u,0xd0000001u)&&ecx==0xc0000001u);
    reset();
    v=es2_sfx_stop_registers(&memory,&g,7,entry,&ecx);
    CHECK(calls==1&&log_[0].f==G(0x464118)&&log_[0].a[0]==CTX&&log_[0].a[1]==0x10007u);
    CHECK(log_[0].r[0]==CTX&&log_[0].r[1]==7&&log_[0].r[2]==0x10007u&&v==PAIR(0xe0000001u,0xd0000001u)&&ecx==0xc0000001u);
    reset();
    v=es2_sfx_rate_registers(&memory,&g,7,0xa410u,entry,&ecx);
    CHECK(calls==1&&log_[0].f==G(0x46466c)&&log_[0].a[0]==CTX&&log_[0].a[1]==0x10007u&&log_[0].a[2]==0xa410u);
    CHECK(log_[0].r[0]==CTX&&log_[0].r[1]==0x10007u&&log_[0].r[2]==7&&ecx==0xc0000001u);
    /* No context: refresh returns the entry registers, stop and rate
     * EAX = 0 (the context) with the entry EDX/ECX. */
    reset();w32(0,G(0x49f8ec),0);
    v=es2_sfx_refresh_registers(&memory,&g,0x2d,0x7777u,entry,&ecx);
    CHECK(calls==0&&v==PAIR(entry[0],entry[1])&&ecx==entry[2]);
    v=es2_sfx_stop_registers(&memory,&g,7,entry,&ecx);
    CHECK(calls==0&&v==PAIR(0,entry[1])&&ecx==entry[2]);
    v=es2_sfx_rate_registers(&memory,&g,7,1,entry,&ecx);
    CHECK(calls==0&&v==PAIR(0,entry[1])&&ecx==entry[2]);
}
/* The lip timeline: channel CH, script S, queue records 0x4d2e76. */
#define CH (OBJ+0x400)
#define S (OBJ+0x500)
#define RECORD(k) (G(0x4d2e76)+8u*(k))
static void lip_reset(void){
    reset();w32(0,CH+0xa,S);w32(0,CH+4,100);w32(0,CH+0x2f,S);w32(0,CH,0x00010003u);
    static const uint8_t script[]={5,10,6,20,7,0xf0,0xff,0xff};memcpy(host(0,S),script,sizeof(script));
}
static void lip_checks(void){
    es2_weapon_graph g=graph();uint32_t ecx;lip_reset();clock_value=125;
    /* 5/+10 -> 110 < 125, 6/+20 -> 130: stop. Empty queue: record 0. */
    uint64_t v=es2_sfx_lip_registers(&memory,&g,CH,entry,&ecx);
    CHECK(calls==1&&log_[0].f==G(0x467828)&&log_[0].r[0]==entry[0]&&log_[0].r[1]==entry[1]&&log_[0].r[2]==entry[2]);
    CHECK(r32(0,CH+0xa)==S+4&&r32(0,CH+4)==130&&r16(0,CH+8)==6);
    CHECK(r32(0,RECORD(0))==130&&r32(0,RECORD(0)+4)==CH&&r32(0,G(0x4d2ec8))==RECORD(0)&&r16(0,G(0x49f964))==1);
    CHECK(v==PAIR(RECORD(0),CH)&&ecx==0);
    /* Two queued events, the first at time 50: the step 20 is below it
     * (the original compares the step, not the time), so the first entry
     * is dropped (count 1, entry 1 moves to 0) and record 1 is written. */
    lip_reset();w16(0,G(0x49f964),2);w32(0,G(0x4d2ec8),RECORD(4));w32(0,G(0x4d2ec8)+4,RECORD(5));w32(0,RECORD(4),50);
    v=es2_sfx_lip_registers(&memory,&g,CH,entry,&ecx);
    CHECK(r16(0,G(0x49f964))==2&&r32(0,G(0x4d2ec8))==RECORD(1)&&r32(0,G(0x4d2ec8)+4)==RECORD(5));
    CHECK(r32(0,RECORD(1))==130&&r32(0,RECORD(1)+4)==CH&&v==PAIR(RECORD(1),CH)&&ecx==0);
    /* A step not below the first event's time: no removal, record 2. */
    lip_reset();w16(0,G(0x49f964),2);w32(0,G(0x4d2ec8),RECORD(4));w32(0,G(0x4d2ec8)+4,RECORD(5));w32(0,RECORD(4),20);
    es2_sfx_lip_registers(&memory,&g,CH,entry,&ecx);
    CHECK(r16(0,G(0x49f964))==3&&r32(0,G(0x4d2ec8))==RECORD(2)&&r32(0,G(0x4d2ec8)+4)==RECORD(5)&&r32(0,RECORD(2))==130);
    /* The clock not ahead after the first step: 5/+10 -> 110 >= 100. */
    lip_reset();clock_value=110;
    es2_sfx_lip_registers(&memory,&g,CH,entry,&ecx);
    CHECK(r32(0,CH+0xa)==S+2&&r16(0,CH+8)==5&&r32(0,RECORD(0))==110);
    /* End of script: lip value -1, the pointer one byte on, no event. */
    lip_reset();clock_value=125;w32(0,CH+0xa,S+6);
    v=es2_sfx_lip_registers(&memory,&g,CH,entry,&ecx);
    CHECK(r16(0,CH+8)==0xffff&&r32(0,CH+0xa)==S+7&&r16(0,G(0x49f964))==0&&v==PAIR(CH,0xffffffffu)&&ecx==125);
}
static void voice_checks(void){
    es2_weapon_graph g=graph();uint32_t ecx;lip_reset();clock_value=300;lip_word=7;w32(0,CH+0xa,0);
    flags[0x7a]=1; /* [0x49f966] */
    uint64_t v=es2_sfx_voice_registers(&memory,&g,CH,entry,&ecx);
    CHECK(calls==3&&log_[0].f==G(0x467828)&&log_[0].r[0]==S&&log_[0].r[1]==5&&log_[0].r[2]==entry[2]);
    CHECK(log_[1].f==G(0x4634b0)&&log_[1].a[0]==CH&&log_[1].r[0]==300&&log_[1].r[1]==0xd0000001u&&log_[1].r[2]==0xc0000001u);
    CHECK(log_[2].f==G(0x464038)&&log_[2].a[0]==CTX&&log_[2].a[1]==0x00010003u);
    CHECK(log_[2].r[0]==0x00010003u&&log_[2].r[1]==0xd0000002u&&log_[2].r[2]==CH);
    CHECK(r32(0,CH+0xa)==S&&r32(0,CH+4)==300&&v==PAIR(0xe0000003u,0xd0000003u)&&ecx==0xc0000003u);
    /* Voice byte off: no play, AL = 1 over the lip step's EAX, ECX = CH. */
    lip_reset();flags[0x7a]=0;
    v=es2_sfx_voice_registers(&memory,&g,CH,entry,&ecx);
    CHECK(calls==2&&v==PAIR(0xe0000001u,0xd0000002u)&&ecx==CH);
    /* Lip value 0 or handle -1: no play either. */
    lip_reset();flags[0x7a]=1;lip_word=0;
    es2_sfx_voice_registers(&memory,&g,CH,entry,&ecx);
    CHECK(calls==2);
    lip_reset();lip_word=7;w32(0,CH,0xffffffffu);
    es2_sfx_voice_registers(&memory,&g,CH,entry,&ecx);
    CHECK(calls==2);
    /* A script starting with FFh still reads the clock (MOVSX never 0FFh). */
    lip_reset();*(uint8_t *)host(0,S)=0xff;
    es2_sfx_voice_registers(&memory,&g,CH,entry,&ecx);
    CHECK(calls>=2&&log_[0].f==G(0x467828)&&log_[0].r[1]==0xffffffffu);
}
static void lip_due_checks(void){
    es2_weapon_graph g=graph();uint32_t ecx;lip_reset();lip_word=0;
    uint64_t v=es2_sfx_lip_due_registers(&memory,&g,CH,entry,&ecx);
    CHECK(calls==1&&log_[0].f==G(0x4634b0)&&log_[0].a[0]==CH&&log_[0].r[0]==CH&&log_[0].r[1]==entry[1]&&log_[0].r[2]==entry[2]);
    CHECK(v==PAIR(0xe0000001u,0xd0000001u)&&ecx==CH);
}
int main(void){
    play_checks();play_at_checks();place_checks();wrapper_checks();lip_checks();voice_checks();lip_due_checks();
    printf("sfx: %u checks passed\n",checks);
    return 0;
}
