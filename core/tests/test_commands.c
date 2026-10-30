#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/commands.h"
/* Synthetic guest for the command handler: the image globals live at their own
 * addresses (image base 0x400000, so VA(x) = x) in a block covering
 * 0x490000..0x4dffff plus the counter pointer 0x6cbee4; objects sit in a 64 KiB
 * arena at OBJ. Game callees are logged with their arguments, entry registers
 * and the EBX/ESI/EDI the core stored for the bridge, and return planned
 * registers; 0x49630c writes a known FS:0 head into the record at
 * f-0x38. */
#define IMAGE UINT32_C(0x400000)
#define LOW UINT32_C(0x490000)
#define LOW_SIZE 0x50000u
#define OBJ UINT32_C(0x90000000)
#define ARENA 0x10000u
static uint8_t low[LOW_SIZE],arena[ARENA],counter_ptr[4];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"commands line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;
    if(a-LOW<LOW_SIZE)return low+(a-LOW);
    if(a-OBJ<ARENA)return arena+(a-OBJ);
    if(a-0x6cbee4u<4u)return counter_ptr+(a-0x6cbee4u);
    fprintf(stderr,"commands: unmapped %08x\n",a);exit(1);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static void w8(uint32_t a,uint8_t v){*(uint8_t *)host(0,a)=v;}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
typedef struct logged { uint32_t f,a[4],r[3],cs[3]; unsigned n,slot; } logged;
static logged log_[48];static unsigned calls,observed,last_slot;
static uint32_t ret[48][3];static const uint32_t head_value=0xdeadbeefu;
static es2_commands_saved saved;
/* The stack-bridge frame (registers and return address) per call. */
static const es2_weapon_frame *frame_seen;static uint32_t rets[48];
#define SCRATCH (OBJ+0xe000u)
#define FRAME (SCRATCH+ES2_COMMANDS_FRAME)
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<48);logged *l=log_+calls;l->f=f;l->n=n;l->slot=last_slot;
    memset(l->a,0,sizeof(l->a));for(unsigned i=0;i<n&&i<4;++i)l->a[i]=a[i];
    l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    l->cs[0]=saved.ebx;l->cs[1]=saved.esi;l->cs[2]=saved.edi;
    if(frame_seen){l->cs[0]=frame_seen->ebx;l->cs[1]=frame_seen->esi;l->cs[2]=frame_seen->edi;rets[calls]=frame_seen->ret-IMAGE+0x400000u;}
    if(f==0x49630cu)w32(0,FRAME-0x38u,head_value);
    r->eax=ret[calls][0];r->edx=ret[calls][1];r->ecx=ret[calls][2];++calls;
}
static void observe(void *c,uint32_t f,unsigned slot){(void)c;(void)f;CHECK(observed==calls);last_slot=slot;++observed;}
static es2_weapon_graph graph(void){es2_weapon_graph g={IMAGE,SCRATCH,0,0,0,call,0,observe,0};return g;}
static const uint32_t entry[3]={0x1111aaaau,0x2222bbbbu,0x3333ccccu};
static void reset(void){calls=observed=last_slot=0;memset(ret,0,sizeof(ret));memset(log_,0,sizeof(log_));
    memset(low,0,sizeof(low));memset(arena,0,sizeof(arena));memset(&saved,0,sizeof(saved));
    w16(0,0x4d2564u,1);w32(0,0x6cbee4u,OBJ+0xf000u);}

#define H (OBJ+0x100u)      /* cockpit [0x49b088] */
#define P (OBJ+0x1000u)     /* player [0x4d24e6] */
#define S (OBJ+0x1400u)     /* view subject [0x4d2684] */
#define C (OBJ+0x1800u)     /* camera [0x4d24ea] */
#define D (OBJ+0x1900u)     /* display [0x4d24c0] */
#define VT (OBJ+0x2000u)    /* actor vtable */
#define W (OBJ+0x2400u)     /* weapon system */
#define GRP (OBJ+0x2800u)   /* group */
#define LIST (OBJ+0x3000u)  /* actor array */
static void world(void){
    w32(0,0x49b088u,H);w32(0,0x4d24e6u,P);w32(0,0x4d2684u,S);w32(0,0x4d24eau,C);w32(0,0x4d24c0u,D);
    w32(0,P,VT);w32(0,S,VT);w32(0,P+0x202u,W);
    w32(0,VT+0x2cu,0x77770001u);w32(0,VT+0x3cu,0x77770002u);w32(0,VT+0x74u,0x77770003u);
}
/* The playback check (call 2) returns AX = 0 so the case table runs; its
 * EDX becomes the case handlers' EDX. */
static uint64_t run(uint32_t code,uint32_t *hd){
    es2_weapon_graph g=graph();
    return es2_command_key_registers(&memory,&g,&saved,code,entry,hd);
}

static void test_disabled(void){
    reset();w16(0,0x4d2564u,0);uint32_t hd;uint64_t v=run(0x10,&hd);
    CHECK(calls==1&&log_[0].f==0x49630cu&&log_[0].r[0]==0x49f648u);
    CHECK(log_[0].r[1]==entry[1]&&log_[0].r[2]==entry[2]);
    CHECK((uint32_t)v==0&&(v>>32)==head_value&&hd==head_value);
}

static void test_hud_handled(void){
    reset();world();ret[1][0]=0x12345601u;uint32_t hd;uint64_t v=run(0x3b,&hd);
    CHECK(calls==2&&log_[1].f==0x432bc8u&&log_[1].a[0]==H&&log_[1].a[1]==0x3b);
    CHECK(log_[1].r[0]==H&&log_[1].cs[1]==0x3b&&log_[1].cs[2]==0);   /* ESI = code, EDI = 0 */
    CHECK((uint32_t)v==1&&(v>>32)==head_value);                      /* EAX = BL = 1 */
}

static void test_playback_handled(void){
    reset();world();ret[2][0]=0x00050001u;ret[2][1]=0x9999u;uint32_t hd;uint64_t v=run(0x21c,&hd);
    CHECK(calls==3&&log_[2].f==0x45fe6cu&&log_[2].a[0]==0x21c&&log_[2].cs[0]==0);   /* EBX = 0 */
    CHECK((uint32_t)v==0x00050001u&&(v>>32)==head_value);
}

static void test_default(void){
    reset();world();w32(0,0x49ef34u,1);                    /* player receives the key */
    ret[1][1]=0x4444u;ret[2][0]=0x00070000u;ret[2][1]=0x5555u;ret[3][0]=0x00120001u;
    uint32_t hd;uint64_t v=run(0x00ab0005u,&hd);           /* code 5: no case */
    CHECK(calls==4&&log_[3].slot==0x2c&&log_[3].f==0x77770001u);
    CHECK(log_[3].a[0]==P&&log_[3].a[1]==0x00ab0005u&&log_[3].n==2);
    CHECK(log_[3].r[0]==P&&log_[3].r[1]==VT&&log_[3].r[2]==0x00ab0005u);   /* ECX = popped code */
    CHECK(log_[3].cs[0]==0x00070001u&&log_[3].cs[1]==0x00ab0005u&&log_[3].cs[2]==0);  /* EBX = hi|1 */
    CHECK((uint32_t)v==0x00120001u);                       /* EBX = the method's EAX, EDI = 0 */
    reset();world();w32(0,0x49ef34u,0);
    v=run(0x1d,&hd);                                       /* above the table: default, view subject */
    CHECK(calls==4&&log_[3].a[0]==S);
}

static void test_alert(void){
    reset();world();uint32_t obj=OBJ+0x4000u,dvt=OBJ+0x4100u;
    w32(0,obj,dvt);w32(0,dvt+8u,0x66660008u);w32(0,OBJ+0xf000u,100);
    w16(0,0x49f5c0u+2u*3u,7);                              /* table[3] = 7 */
    ret[3][0]=0x00010003u;                                 /* 413180 status 3 (high word ignored by movsx) */
    ret[4][0]=obj;                                         /* 47749c */
    ret[6][0]=7;                                           /* modal result 7 = table[3] */
    uint32_t hd;uint64_t v=run(0x10,&hd);
    CHECK(calls==8);
    CHECK(log_[3].f==0x413180u&&log_[3].a[0]==P&&log_[3].a[1]==1&&log_[3].r[0]==0x10);
    CHECK(log_[4].f==0x47749cu&&log_[4].a[0]==0x57e&&log_[4].cs[1]==0x00010003u&&log_[4].cs[2]==3);
    CHECK(log_[5].f==0x455950u&&log_[5].a[0]==obj&&log_[5].a[1]==3&&log_[5].a[2]==1&&log_[5].a[3]==0&&log_[5].r[2]==0x57e);
    CHECK(log_[6].slot==8&&log_[6].f==0x66660008u&&log_[6].a[0]==obj&&log_[6].r[2]==dvt);
    CHECK(log_[7].f==0x4543b0u&&log_[7].a[0]==obj&&log_[7].a[1]==3&&log_[7].r[1]==OBJ+0xf000u&&log_[7].cs[2]==1);
    CHECK(r32(0,OBJ+0xf000u)==100-10+9);                   /* -0Ah after the constructor, +9 before destroy */
    CHECK(r16(0,FRAME-0x28u)==0x20&&r32(0,FRAME-4u)==obj&&r32(0,FRAME-8u)==obj&&r32(0,FRAME-0x5cu)==obj);
    CHECK((uint32_t)v==2);                                 /* EDI (match) + EBX (1) */
    w16(0,0x4a9e68u,5);reset();world();w32(0,obj,dvt);w32(0,dvt+8u,0x66660008u);w32(0,OBJ+0xf000u,100);
    ret[3][0]=3;ret[4][0]=obj;ret[6][0]=6;w16(0,0x4a9e68u,5);
    v=run(0x10,&hd);
    CHECK((uint32_t)v==1&&r16(0,0x4a9e68u)==0);
}

static void test_damage_near(void){
    reset();world();w32(0,0x49ef38u,1);
    uint32_t a1=OBJ+0x5000u,a2=OBJ+0x5400u;
    w32(0,0x4a9af8u,LIST);w16(0,0x4a9afeu,3);w32(0,LIST,a1);w32(0,LIST+4,a2);w32(0,LIST+8,0);
    w32(0,a1+0x45u,GRP);w32(0,a2+0x45u,GRP);w32(0,a2,VT);w8(GRP+0x12u,1);
    w8(a1+0x99u,1);                                        /* a1 destroyed: skipped */
    ret[3][0]=99999u;                                      /* dist_gate below 100000 */
    uint32_t hd;uint64_t v=run(0x631,&hd);
    CHECK(calls==5&&log_[3].f==0x49288cu&&log_[3].a[0]==P+0x26u&&log_[3].a[1]==a2+0x26u);
    CHECK(log_[3].cs[1]==a2&&log_[3].r[1]==1);             /* ESI = actor, EDX = side */
    CHECK(log_[4].slot==0x74&&log_[4].a[0]==a2&&log_[4].a[1]==0&&log_[4].a[2]==0x7d00u&&log_[4].a[3]==P);
    CHECK(log_[4].r[0]==99999u&&log_[4].r[1]==P&&log_[4].r[2]==VT);
    CHECK((uint32_t)v==1&&r16(0,FRAME-0x3cu)==2);
    reset();world();w32(0,0x49ef38u,1);
    w32(0,0x4a9af8u,LIST);w16(0,0x4a9afeu,2);w32(0,LIST,a2);w32(0,LIST+4,a2);
    w32(0,a2+0x45u,GRP);w8(GRP+0x12u,1);ret[3][0]=100000u;ret[4][0]=100000u;
    v=run(0x631,&hd);                                      /* both too far: count ends the loop */
    CHECK(calls==5&&log_[4].f==0x49288cu&&(uint32_t)v==1);
}

static void test_damage_view(void){
    reset();world();w32(0,0x49ef38u,1);w16(0,0x4d2500u,7);ret[2][1]=0xabcd1234u;
    uint32_t hd;run(0x620,&hd);
    CHECK(calls==4&&log_[3].slot==0x74&&log_[3].a[0]==S&&log_[3].a[1]==0xabcd0007u);
    CHECK(log_[3].a[2]==0x12cu&&log_[3].a[3]==P&&log_[3].r[0]==VT&&log_[3].r[2]==S);
}

static void test_view(void){
    reset();world();w16(0,0x4d24eeu,3);ret[2][1]=0x1234u;
    uint32_t hd;run(0x2f,&hd);
    CHECK(calls==4&&log_[3].f==0x42a3f4u&&log_[3].a[0]==D&&log_[3].a[1]==3);
    CHECK(log_[3].r[0]==C&&log_[3].r[1]==0xfffffffeu&&log_[3].r[2]==D);
    CHECK(r16(0,0x4d251au)==2);
    reset();world();w16(0,0x4d24eeu,2);run(0x2f,&hd);
    CHECK(calls==4&&log_[3].a[1]==2&&log_[3].r[0]==D&&log_[3].r[1]==0&&r16(0,0x4d251au)==3);
    reset();world();w16(0,0x4d24eeu,5);run(0x2f,&hd);
    CHECK(calls==3);                                       /* mode 5: nothing */
}

static void test_cheat_words(void){
    uint32_t hd;
    reset();world();w32(0,0x49ef38u,1);w16(0,0x4d2500u,0x1d);run(0x634,&hd);CHECK(r16(0,0x4d2500u)==0x1d);
    reset();world();w32(0,0x49ef38u,1);w16(0,0x4d2500u,4);run(0x634,&hd);CHECK(r16(0,0x4d2500u)==5);
    reset();world();w32(0,0x49ef38u,1);w16(0,0x4d2500u,0);run(0x633,&hd);CHECK(r16(0,0x4d2500u)==0);
    reset();world();w32(0,0x49ef38u,0);w16(0,0x4d2500u,4);run(0x633,&hd);CHECK(r16(0,0x4d2500u)==4);
    reset();world();w32(0,0x49ef38u,1);w16(0,0x4d24f2u,1);run(0x24e,&hd);
    CHECK(r16(0,0x4d24fcu)==1&&r16(0,0x4d24f2u)==0);
    reset();world();w16(0,0x4d24d8u,1);run(0x21f,&hd);CHECK(r16(0,0x4d24f2u)==1);   /* recording */
    reset();world();run(0x21f,&hd);CHECK(r16(0,0x4d24f2u)==0);                     /* no flag */
    reset();world();w32(0,0x49ef38u,1);run(0x414,&hd);CHECK(r16(0,0x4d24f0u)==1);
}

static void test_align_and_lever(void){
    uint32_t hd;
    reset();world();w16(0,P+0x10u,0x1000);ret[3][0]=0x00ff0100u;
    run(0x2b,&hd);
    CHECK(calls==4&&log_[3].slot==0x3c&&log_[3].a[0]==P&&log_[3].r[0]==W+0x14u&&log_[3].cs[1]==P);
    CHECK(r32(0,0x4d2a70u)==1&&r16(0,0x4d2a74u)==0x0f00);
    reset();world();uint32_t J=OBJ+0x6000u;w8(J+4u,1);w8(0x4d1f46u,1);w16(0,0x49a06eu,0);ret[3][0]=J;
    run(0x402,&hd);CHECK(calls==4&&log_[3].f==0x477904u&&r16(0,0x49a06eu)==1);
    reset();world();w8(J+4u,1);w8(0x4d1f46u,1);w16(0,0x49a06eu,1);ret[3][0]=J;
    run(0x402,&hd);CHECK(r16(0,0x49a06eu)==0xffffu);
}

static void test_playback_key(void){
    es2_weapon_graph g=graph();
    reset();uint64_t v=es2_playback_key_registers(&memory,&g,0x0001021cu,0x004d22c6u,entry);
    CHECK(calls==1&&log_[0].f==0x4667c8u&&log_[0].r[0]==0x11110000u&&log_[0].r[1]==entry[1]);
    CHECK((uint32_t)v==0x004d0001u&&(v>>32)==ret[0][1]);
    reset();w16(0,0x4d24d6u,1);w16(0,0x4d252cu,1);
    v=es2_playback_key_registers(&memory,&g,0x21c,0,entry);
    CHECK(calls==0&&(uint32_t)v==1&&(v>>32)==entry[1]);    /* playback with the option: no switch */
    reset();w16(0,0x4d252cu,1);v=es2_playback_key_registers(&memory,&g,0x430,0,entry);
    CHECK(calls==1&&log_[0].f==0x467ad8u&&(uint32_t)v==1);
    reset();v=es2_playback_key_registers(&memory,&g,0x430,0x00030000u,entry);
    CHECK(calls==0&&(uint32_t)v==0x00030001u);
    reset();v=es2_playback_key_registers(&memory,&g,0x1c,0xffff0000u,entry);
    CHECK(calls==0&&(uint32_t)v==0&&(v>>32)==entry[1]);
}

/* The frame variant: the caller's EBX and EDI at 49630c and
 * 432bc8, EDI = 0 from 45fee9, EBX = AL then hi|1 from 45ff19; the return
 * addresses after each CALL (the player's key method: 460844). */
static void test_frame(void){
    reset();world();w32(0,0x49ef34u,1);es2_weapon_frame fr;memset(&fr,0,sizeof(fr));frame_seen=&fr;
    ret[2][0]=0x00070000u;uint32_t hd;es2_weapon_graph g=graph();
    es2_command_key_frame_registers(&memory,&g,&fr,0xeb0eb0u,0xed1ed1u,0x00ab0005u,entry,&hd);
    CHECK(calls==4&&rets[0]==0x45feceu&&rets[1]==0x45fef7u&&rets[2]==0x45ff0du&&rets[3]==0x460844u);
    CHECK(log_[0].cs[0]==0xeb0eb0u&&log_[0].cs[1]==0x00ab0005u&&log_[0].cs[2]==0xed1ed1u);
    CHECK(log_[1].cs[0]==0xeb0eb0u&&log_[1].cs[2]==0&&log_[2].cs[0]==0&&log_[3].cs[0]==0x00070001u);
    reset();world();w32(0,0x49ef34u,0);memset(&fr,0,sizeof(fr));
    es2_command_key_frame_registers(&memory,&g,&fr,0,0,0x1d,entry,&hd);
    CHECK(calls==4&&rets[3]==0x460857u&&log_[3].a[0]==S);
    frame_seen=NULL;
}

int main(void){
    test_disabled();
    test_hud_handled();
    test_playback_handled();
    test_default();
    test_alert();
    test_damage_near();
    test_damage_view();
    test_view();
    test_cheat_words();
    test_align_and_lever();
    test_playback_key();
    test_frame();
    printf("commands: %u checks passed\n",checks);
    return 0;
}
