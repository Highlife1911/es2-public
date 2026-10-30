#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/input_dispatch.h"
/* Synthetic checks of the core on an own arena: torso centering's
 * clamp and servo calls, torso aim's frame (matrix copies, inverted
 * translations, transforms, angle words, squared rates) and one cockpit
 * frame of the input dispatcher with a button action. The in-vitro harness
 * compares against the original; these values are hand-derived. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE-0x90000u)
#define OBJ (BASE+0x48000u)
#define GV(va) (IMAGE+(uint32_t)(va)-0x400000u)
static uint8_t arena[0x50000];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"input_dispatch line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static void w8(uint32_t a,uint8_t v){*(uint8_t *)host(0,a)=v;}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
static unsigned calls;static uint32_t functions[64],args[64][6],entry[64][3],results[64],saved_at[64][3];
static uint32_t out_words[3];
/* torso_aim's per-call register frame, read at every call. */
static const es2_input_call *frame_seen;static uint32_t rets[64],pushes[64][3];
/* Virtual targets are 7700h + slot; direct calls name their original VA. */
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    CHECK(calls<64);
    uint32_t va=f>=0x7700u&&f<0x7800u?f:f-IMAGE+0x400000u;
    functions[calls]=va;entry[calls][0]=r->eax;entry[calls][1]=r->edx;entry[calls][2]=r->ecx;
    if(frame_seen){saved_at[calls][0]=frame_seen->ebx;saved_at[calls][1]=frame_seen->esi;saved_at[calls][2]=frame_seen->edi;
        rets[calls]=frame_seen->ret-IMAGE+0x400000u;pushes[calls][0]=frame_seen->pushed;
        pushes[calls][1]=frame_seen->temp[0];pushes[calls][2]=frame_seen->temp[1];}
    else if(c){const es2_input_saved *s=c;saved_at[calls][0]=s->ebx;saved_at[calls][1]=s->esi;saved_at[calls][2]=s->edi;}
    for(unsigned i=0;i<n&&i<6;++i)args[calls][i]=a[i];
    r->eax=results[calls];r->edx=0x5eed0000u+calls;r->ecx=0xc0de0000u+calls;++calls;
    if(va==0x492990u)for(unsigned i=0;i<3;++i)w16(0,a[0]+2*i,(uint16_t)out_words[i]);
    if(va==0x49681au)memset(host(0,a[0]),(int)a[1],a[2]);
}
static void reset(void){calls=0;memset(results,0,sizeof(results));}
#define PAIR(a,d) ((uint64_t)(d)<<32|(uint32_t)(a))
int main(void){
    es2_weapon_graph g={IMAGE,0,0,0,0,call,NULL,NULL,NULL};
    const uint32_t regs[3]={0xaaaa0001u,0xbbbb0002u,0xcccc0003u};
    const uint32_t P=OBJ,CFG=OBJ+0x400,M=OBJ+0x500,obj=OBJ+0x540,node=OBJ+0x580,mats=OBJ+0x600,vt=OBJ+0xa00,
        point=OBJ+0xb00,R=OBJ+0xc00,H=OBJ+0xd00,W=OBJ+0xe00;
    memset(arena,0,sizeof(arena));
    for(unsigned k=0;k<0x80;k+=4)w32(0,vt+k,0x7700u+k);
    w32(0,P,vt);w32(0,P+0x1f2,CFG);
    /* torso_center: 123h clamps to 100h, its negation FF00h keeps EAX's
     * high word 0; -200h clamps to FFFFFF00h, negated to FFFF0100h. */
    w16(0,P+0x298,0x40);w16(0,P+0x29a,0xffe0);
    reset();results[0]=0x123;results[1]=0xfffffe00u;results[2]=0x2222;results[3]=0x3333;
    uint64_t p=es2_torso_center_registers(&memory,&g,P,0x7777,regs);
    CHECK(calls==4&&functions[0]==0x47e0b0u&&args[0][0]==0xfa&&args[0][1]==0x40&&entry[0][0]==regs[0]&&entry[0][2]==regs[2]);
    CHECK(functions[1]==0x47e0b0u&&args[1][1]==0xffffffe0u&&entry[1][0]==0xff00u&&entry[1][1]==0xffffffe0u&&entry[1][2]==0xffffffe0u);
    CHECK(functions[2]==0x41a550u&&args[2][0]==P&&args[2][1]==0xff00u&&args[2][2]==0&&args[2][3]==1&&entry[2][0]==0xffff0100u);
    CHECK(entry[2][1]==0x5eed0001u&&entry[2][2]==0xc0de0001u);
    CHECK(functions[3]==0x41a808u&&args[3][1]==0xffff0100u&&args[3][2]==0x7777&&args[3][3]==0&&args[3][4]==1);
    CHECK(entry[3][0]==0x2222&&entry[3][1]==0x7777&&entry[3][2]==0xc0de0002u&&p==PAIR(0x3333,0x5eed0003u));
    /* torso_aim: stale pose (P+32h = 0) rebuilt, node 2 of the model. */
    g.scratch=OBJ+0x2000;const uint32_t f=g.scratch+ES2_TORSO_AIM_SCRATCH;
    w32(0,P+0x34,M);w32(0,M+4,obj);w32(0,obj,vt);w32(0,M+0x16,mats);w16(0,node+4,2);
    w16(0,CFG+0xc,7);w16(0,CFG+0x66,5);
    w32(0,P+0x26,100);w32(0,P+0x2a,200);w32(0,P+0x2e,300);w16(0,P+0x32,0);
    w32(0,mats+0x54,10);w32(0,mats+0x58,20);w32(0,mats+0x5c,30);
    out_words[0]=0x100;out_words[1]=0;out_words[2]=0xff00;
    reset();results[1]=node;results[9]=0x30;results[10]=0x20;results[11]=0xfffffff7u;results[12]=4;
    results[13]=0x11110000u;results[14]=0x9000;results[15]=0x4444;
    p=es2_torso_aim_registers(&memory,&g,P,point,regs);
    CHECK(calls==16&&functions[0]==0x47ebb8u&&args[0][0]==P+0xc&&args[0][1]==P+0x12&&entry[0][0]==P+0x12&&entry[0][2]==P+0xc);
    CHECK(r16(0,P+0x32)==1&&functions[1]==0x7720u&&args[1][0]==obj&&args[1][1]==7&&entry[1][0]==obj&&entry[1][1]==vt&&entry[1][2]==M);
    CHECK(functions[2]==0x47df19u&&args[2][0]==f-0x74&&entry[2][0]==30&&entry[2][1]==10&&entry[2][2]==20);
    CHECK(functions[3]==0x480304u&&args[3][0]==f-0xa0&&args[3][1]==f-0x74&&args[3][2]==f-0x60&&entry[3][0]==0u-300u);
    CHECK(r32(0,f-0xa0)==0u-100u&&r32(0,f-0x9c)==0u-200u&&r32(0,f-0x98)==0u-300u&&r32(0,f-0x58)==0u-300u);
    CHECK(functions[5]==0x480304u&&args[5][0]==f-0xac&&r32(0,f-0xac)==0u-10u&&r32(0,f-0x78)==0u-30u);
    CHECK(functions[6]==0x48043cu&&args[6][0]==point&&args[6][1]==f-0x74&&args[6][2]==f-0x48&&entry[6][1]==f-0x48);
    CHECK(functions[7]==0x48043cu&&args[7][0]==f-0x48&&args[7][1]==f-0x94&&args[7][2]==f-0x3c&&entry[7][2]==f-0x3c);
    CHECK(functions[8]==0x492990u&&args[8][0]==GV(0x4a9cfc)&&args[8][1]==f-0x3c&&args[8][2]==f-0x54&&entry[8][2]==5u);
    CHECK(r32(0,f-0x34)==0u-5u&&args[9][1]==0xffffff00u&&args[10][1]==0x100);
    CHECK(functions[11]==0x47e0a0u&&args[11][0]==0x30&&args[11][1]==0xffffffd0u&&entry[11][0]==0xffffffd0u&&entry[11][1]==0x30);
    CHECK(functions[12]==0x47e0a0u&&args[12][0]==0x20&&args[12][1]==0x20);
    CHECK(functions[13]==0x41a550u&&args[13][1]==0xfffffff7u&&args[13][2]==0xc0de0140u&&args[13][3]==1&&entry[13][0]==4);
    CHECK(functions[14]==0x47de72u&&args[14][0]==0&&args[14][1]==0&&args[14][2]==0u-5u);
    CHECK(functions[15]==0x41a808u&&args[15][1]==4&&args[15][2]==0x9000&&args[15][3]==0x111100e0u&&args[15][4]==1);
    CHECK(p==PAIR(GV(0x4a9cfc),0x5eed000fu)&&arena[GV(0x49a340)-BASE]==1);
    /* A valid pose (P+32h = 1) skips euler_matrix. */
    reset();results[0]=node;
    es2_torso_aim_registers(&memory,&g,P,point,regs);CHECK(calls==15&&functions[0]==0x7720u&&functions[14]==0x41a808u);
    /* The stale-pose path again with the register frame: ESI = P
     * at every call, EBX/EDI the matrices up to aim_angles, then the rates,
     * and the return address after each CALL. */
    {   es2_input_call fr={0,0,0,0,0,{0,0}};frame_seen=&fr;w16(0,P+0x32,0);
        reset();results[1]=node;results[9]=0x30;results[10]=0x20;results[11]=0xfffffff7u;results[12]=4;
        results[13]=0x11110000u;results[14]=0x9000;results[15]=0x4444;
        uint32_t ecx=0;p=es2_torso_aim_frame_registers(&memory,&g,&fr,&ecx,P,point,regs);frame_seen=NULL;
        CHECK(ecx==0xc0de000fu);
        static const uint32_t expect_ret[16]={0x41ef70,0x41f005,0x41f097,0x41f0d2,0x41f0db,0x41f116,0x41f127,0x41f138,0x41f15a,
            0x41f16f,0x41f186,0x41f2b5,0x41f2ee,0x41f30b,0x41f33c,0x41f347};
        CHECK(calls==16&&p==PAIR(GV(0x4a9cfc),0x5eed000fu));
        for(unsigned k=0;k<16;++k)CHECK(rets[k]==expect_ret[k]&&saved_at[k][1]==P&&pushes[k][0]==(k==14?2u:0u));
        CHECK(pushes[14][1]==1&&pushes[14][2]==args[15][3]);
        for(unsigned k=0;k<9;++k)CHECK(saved_at[k][0]==f-0x74&&saved_at[k][2]==f-0x94);
        CHECK(saved_at[9][0]==0xffffff00u&&saved_at[9][2]==f-0x94&&saved_at[10][0]==0x30&&saved_at[10][2]==0x100);
        CHECK(saved_at[11][0]==0xffd0u&&saved_at[11][2]==0x20&&saved_at[12][0]==0xfffffff7u&&saved_at[12][2]==0x20);
        CHECK(saved_at[13][0]==0xfffffff7u&&saved_at[13][2]==4&&saved_at[14][0]==r32(0,f-0x34)&&saved_at[15][0]==saved_at[14][0]&&saved_at[15][2]==4);
    }
    /* player_input: cockpit view, ground chassis, twist input, button 0
     * with action 7 (stop); 45feb8 returns 2. */
    g.scratch=OBJ+0x3000;es2_input_saved saved={0,0,0};g.context=&saved;
    const uint32_t pf=g.scratch+ES2_PLAYER_INPUT_SCRATCH;
    w32(0,GV(0x4d24e6),P);w32(0,GV(0x49b088),H);w16(0,GV(0x4d24f0),0);w8(GV(0x49f6e8),1);w8(GV(0x49f6e9),1);
    w8(P+0x39,1);w16(0,CFG+0x50,0);w32(0,P+0x1a4,0);w32(0,GV(0x4d2a70),0);w32(0,P+0x202,W);
    w16(0,R,0x10);w16(0,R+0xe,0x11);w16(0,R+0x10,0x22);w16(0,R+0x12,5);w16(0,R+0x14,0x33);w8(R+0xb,0);w8(R+0x16,1);
    w8(GV(0x4d2577),13);w8(GV(0x4d1f38)+17,7);w16(0,P+0x290,0x55);w8(P+0x93,0);
    reset();results[0]=2;
    p=es2_player_input_registers(&memory,&g,&saved,R,regs);
    CHECK(calls==9&&functions[0]==0x45feb8u&&args[0][0]==0xaaaa0010u&&entry[0][1]==regs[1]&&saved_at[0][0]==P&&saved_at[0][1]==R);
    CHECK(functions[1]==0x432b14u&&args[1][0]==H&&args[1][1]==R&&entry[1][1]==1&&entry[1][2]==H);
    CHECK(functions[2]==0x401c74u&&(uint16_t)args[2][1]==0&&(uint16_t)args[2][2]==0&&(uint16_t)args[2][3]==0);
    CHECK(functions[3]==0x4160dcu&&(uint16_t)args[3][1]==0x11&&(uint16_t)args[3][2]==0x22&&saved_at[3][2]==5);
    CHECK(functions[4]==0x41a550u&&args[4][1]==5&&args[4][2]==0xffffffffu&&args[4][3]==0);
    CHECK(functions[5]==0x41a808u&&(uint16_t)args[5][1]==0x33&&args[5][2]==0&&r16(0,GV(0x4d2504))==0);
    CHECK(functions[6]==0x415608u&&args[6][0]==P&&args[6][1]==R);
    CHECK(functions[7]==0x49681au&&args[7][0]==pf-0x8c&&args[7][2]==0x15);
    CHECK(functions[8]==0x45b734u&&args[8][0]==1&&args[8][1]==1&&saved_at[8][0]==0&&saved_at[8][1]==0&&saved_at[8][2]==R+0x16);
    CHECK(r16(0,P+0x290)==0&&arena[P+0x93-BASE]==1&&p==PAIR(((P+0x92)&0xffff0000u)|1u,P));
    /* Action 16 through 410f78 with ESI = the button index. */
    w8(R+0x16,0);w8(R+0x18,3);w8(GV(0x4d1f38)+19,16);
    reset();p=es2_player_input_registers(&memory,&g,&saved,R,regs);
    CHECK(calls==10&&functions[8]==0x45b734u&&args[8][0]==3&&functions[9]==0x410f78u&&args[9][0]==W&&args[9][1]==0x202);
    CHECK(saved_at[9][0]==19&&saved_at[9][1]==2&&saved_at[9][2]==R+0x18&&entry[9][0]==W&&entry[9][2]==P);
    CHECK(p==PAIR(results[9]&0xffff0000u,0x5eed0009u));
    /* The stack-bridge frame of the same case: the caller's EDI at
     * 45feb8/432b14, then the twist word; the CALLs' return addresses. */
    {   es2_input_call fr;memset(&fr,0,sizeof(fr));frame_seen=&fr;g.context=NULL;
        reset();p=es2_player_input_frame_registers(&memory,&g,&fr,0xed1ed1u,R,regs);
        static const uint32_t expect_ret[10]={0x460894,0x4608b0,0x460ab7,0x460c34,0x460ccd,0x460ce3,0x460cf7,0x460d0a,0x460d58,0x46100a};
        CHECK(calls==10&&p==PAIR(results[9]&0xffff0000u,0x5eed0009u));
        for(unsigned k=0;k<10;++k)CHECK(rets[k]==expect_ret[k]&&pushes[k][0]==0);
        CHECK(saved_at[0][0]==P&&saved_at[0][1]==R&&saved_at[0][2]==0xed1ed1u&&saved_at[1][2]==0xed1ed1u);
        CHECK(saved_at[2][2]==5&&saved_at[9][0]==19&&saved_at[9][1]==2&&saved_at[9][2]==R+0x18);
        frame_seen=NULL;
    }
    printf("PASS: input_dispatch core %u checks\n",checks);
    return 0;
}
