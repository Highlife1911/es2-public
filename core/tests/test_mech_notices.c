#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/mech_notices.h"
/* Synthetic guest for the core: objects at 90000000h, image base B
 * with a window 499000h..4b9000h for the globals. The callees are logged
 * with their address, slots and entry registers and return planned values;
 * one call may poke a dword. All values are synthetic. */
#define OBJ UINT32_C(0x90000000)
#define B UINT32_C(0x10000000)
#define G(va) (B+(va)-0x400000u)
#define WIN 0x499000u
static uint8_t arena[0x4000],image[0x20000];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"mech_notices line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-G(WIN)<sizeof(image))return image+(a-G(WIN));
    fprintf(stderr,"mech_notices: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static void w8(uint32_t a,uint8_t v){*(uint8_t *)host(0,a)=v;}
static uint8_t r8(uint32_t a){return *(uint8_t *)host(0,a);}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
typedef struct logged { uint32_t f,a[3],r[3]; unsigned n; } logged;
static logged log_[16];static unsigned calls,observed;
static uint32_t ret[16][3],notes[16];
static unsigned poke_at=99;static uint32_t poke_addr,poke_value;
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<16&&n<=3);logged *l=log_+calls;l->f=f;l->n=n;memcpy(l->a,a,4*n);l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    if(calls==poke_at)w32(0,poke_addr,poke_value);
    r->eax=ret[calls][0];r->edx=ret[calls][1];r->ecx=ret[calls][2];++calls;
}
static void observe(void *c,uint32_t f,unsigned slot){(void)c;CHECK(slot==0&&observed==calls&&observed<16);notes[observed++]=f;}
static es2_weapon_graph graph(void){es2_weapon_graph g={B,0,0,0,0,call,0,observe,0};return g;}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
static void reset(void){calls=observed=0;poke_at=99;memset(ret,0,sizeof(ret));memset(log_,0,sizeof(log_));
    memset(arena,0,sizeof(arena));memset(image,0,sizeof(image));}
static int called(unsigned i,uint32_t va,uint32_t a0,uint32_t a1,uint32_t a2,uint32_t eax,uint32_t edx,uint32_t ecx){
    const logged *l=log_+i;
    return i<calls&&l->f==G(va)&&notes[i]==va&&l->a[0]==a0&&(l->n<2||l->a[1]==a1)&&(l->n<3||l->a[2]==a2)&&
        l->r[0]==eax&&l->r[1]==edx&&l->r[2]==ecx;
}
static uint64_t pr(uint32_t eax,uint32_t edx){return (uint64_t)edx<<32|eax;}
#define A (OBJ+0x100)
#define O (OBJ+0x600)
#define P (OBJ+0xb00)
#define L (OBJ+0x1000)
#define N (OBJ+0x1500)
#define G1 (OBJ+0x2000)
#define G2 (OBJ+0x2080)
#define LIST (OBJ+0x2100)
#define SR (OBJ+0x2200)
static void actors(void){
    w32(0,A+0x45,G1);w32(0,O+0x45,G2);w32(0,P+0x45,G1);w32(0,L+0x45,G1);w32(0,N+0x45,G2);
    w8(G1+0x12,0);w8(G2+0x12,1);w32(0,G1+0xc,LIST);w32(0,LIST,L);w32(0,A+0x4d,SR-8);
    w32(0,G(0x4a9b84),P);
}
static void contact(void){
    const es2_weapon_graph g=graph();
    /* Held back (A+0A3h): only the seen byte of B's index, first-call byte. */
    reset();actors();w8(A+0xa3,1);w16(0,O+0x4b,0xfffe);
    uint64_t v=es2_mech_contact_registers(&memory,&g,A,O,entry);
    CHECK(v==pr(A+0x92,0xfffffffeu)&&calls==0&&r8(G(0x4a9afe))==1&&r8(G(0x499358))==1);
    w8(G(0x499358),7);v=es2_mech_contact_registers(&memory,&g,A,O,entry);CHECK(r8(G(0x499358))==7);
    /* Full path: timer, all tests pass, timer word 0 -> 420a98(A, 1, 0), word = 10000. */
    reset();actors();w16(0,O+0x4b,3);w16(0,O+0x1a8,2);ret[0][0]=0xaaaa0000u;ret[0][1]=0xbbbb0000u;ret[0][2]=0xcccc0000u;
    ret[1][0]=0x01;ret[1][1]=0x02;ret[1][2]=0x03;
    v=es2_mech_contact_registers(&memory,&g,A,O,entry);
    CHECK(calls==2&&called(0,0x467a48,G(0x4a9b64),0,0,A+0x92,entry[1],entry[2]));
    CHECK(called(1,0x420a98,A,1,0,3,P+0xc2,3)&&v==pr(1,2)&&r8(G(0x4a9b03))==1&&r16(0,G(0x4a9b65))==10000);
    /* Timer still running: no message; EAX = index, EDX = P+0C2h. */
    reset();actors();w16(0,O+0x4b,0xffc0);w16(0,G(0x4a9b65),5);ret[0][2]=0x12345678u;
    v=es2_mech_contact_registers(&memory,&g,A,O,entry);
    CHECK(calls==1&&v==pr(0xffffffc0u,P+0xc2)&&r8(G(0x4a9b00)-0x40)==1&&r16(0,G(0x4a9b65))==5);
    /* B of type 1: return the timer's pair. */
    reset();actors();w16(0,O+0x1a8,1);ret[0][0]=7;ret[0][1]=8;
    CHECK(es2_mech_contact_registers(&memory,&g,A,O,entry)==pr(7,8)&&calls==1);
    /* Another group than the player's: EAX = the player's group. */
    reset();actors();w32(0,P+0x45,G2);ret[0][1]=8;
    CHECK(es2_mech_contact_registers(&memory,&g,A,O,entry)==pr(G2,8));
    /* Already seen, B noticed (+95h), A knows B, the player knows B. */
    reset();actors();w16(0,O+0x4b,4);w8(G(0x4a9b04),1);ret[0][1]=8;
    CHECK(es2_mech_contact_registers(&memory,&g,A,O,entry)==pr((G1&0xffff0000u)|4,4));
    reset();actors();w16(0,O+0x4b,4);w8(O+0x95,1);
    CHECK(es2_mech_contact_registers(&memory,&g,A,O,entry)==pr((G1&0xffff0000u)|4,O+0x92));
    reset();actors();w16(0,O+0x4b,4);w8(A+0xc6,1);
    CHECK(es2_mech_contact_registers(&memory,&g,A,O,entry)==pr((G1&0xffff0000u)|4,A+0xc2));
    reset();actors();w16(0,O+0x4b,4);w8(P+0xc6,1);
    CHECK(es2_mech_contact_registers(&memory,&g,A,O,entry)==pr((G1&0xffff0000u)|4,P+0xc2)&&r8(G(0x4a9b04))==0);
}
static void kill(void){
    const es2_weapon_graph g=graph();
    /* w = 0, K in the player's group (not the player), V another side:
     * K's call 420a98(K, 2, 0), the kill word +2A4h[type], byte +0A6h. */
    reset();actors();w16(0,O+0x1a8,2);ret[0][0]=0x10;ret[0][1]=0x20;ret[0][2]=0x30;
    uint64_t v=es2_mech_kill_registers(&memory,&g,A,O,0xffff0000u,entry);
    CHECK(calls==1&&called(0,0x420a98,A,2,0,0,P,1));
    CHECK(r16(0,A+0x2a8)==1&&r8(A+0xa6)==1&&v==pr(P,G1));
    /* Negative type word; K the player: no call to K, the counter 4a9e84 stays (V not in the group). */
    reset();actors();w32(0,G(0x4a9b84),A);w16(0,O+0x1a8,0xffff);w16(0,A+0x2a4-2,5);
    v=es2_mech_kill_registers(&memory,&g,A,O,0,entry);CHECK(calls==0&&r16(0,A+0x2a2)==6&&v==pr(A,G1));
    /* V a wingman of the player (same group, not the player): V's call with
     * 25h when V is dead (+99h), else 4; the player's kill counter. */
    reset();actors();w32(0,O+0x45,G1);w32(0,G(0x4a9b84),A);w8(O+0x99,1);ret[0][0]=0x77;ret[0][1]=0x88;
    v=es2_mech_kill_registers(&memory,&g,A,O,1,entry);
    CHECK(calls==1&&called(0,0x420a98,O,(entry[2]&0xffff0000u)|0x25,1,O,(entry[2]&0xffff0000u)|0x25,(entry[2]&0xffff0000u)|0x25));
    CHECK(v==pr(0x77,0x88)&&r16(0,G(0x4a9e84))==1);
    reset();actors();w32(0,O+0x45,G1);ret[0][2]=0;
    v=es2_mech_kill_registers(&memory,&g,A,O,1,entry);
    CHECK(calls==1&&log_[0].a[1]==((entry[2]&0xffff0000u)|4)&&r16(0,G(0x4a9e84))==0);
    /* Same sides with w = 0: no credit; the player itself as V: no call. */
    reset();actors();w32(0,O+0x45,G1);w32(0,G(0x4a9b84),O);
    v=es2_mech_kill_registers(&memory,&g,A,O,0,entry);CHECK(calls==0&&r8(A+0xa6)==0&&v==pr(O,G1));
}
static void alert(void){
    const es2_weapon_graph g=graph();
    reset();ret[0][0]=5;ret[0][1]=6;
    uint64_t v=es2_mech_alert_registers(&memory,&g,A,entry);
    CHECK(calls==1&&called(0,0x420a98,A,8,0,A,entry[1],entry[2])&&r32(0,A+0x278)==40000&&v==pr(5,6));
    v=es2_mech_alert_registers(&memory,&g,A,entry);CHECK(calls==1&&v==pr(A,entry[1]));
}
static void attacked(void){
    const es2_weapon_graph g=graph();
    /* S out: EAX = S+92h, EDX 1 for +0A4h/+99h. */
    reset();actors();w8(O+0xa5,1);CHECK(es2_mech_attacked_registers(&memory,&g,A,O,0,entry)==pr(O+0x92,G1)&&calls==0);
    reset();actors();w8(O+0x99,1);CHECK(es2_mech_attacked_registers(&memory,&g,A,O,0,entry)==pr(O+0x92,1));
    /* Same side: EAX/EDX the sign-extended side bytes. */
    reset();actors();w8(G2+0x12,0xff);w8(G1+0x12,0xff);CHECK(es2_mech_attacked_registers(&memory,&g,A,O,0,entry)==pr(~0u,~0u)&&calls==0);
    /* The player attacks a member of its own group: the alert 41f790
     * (EAX = the player, EDX = its group, ECX = 1), then the side test ends
     * it (same group, same side). */
    reset();actors();
    uint64_t v=es2_mech_attacked_registers(&memory,&g,A,P,7,entry);
    CHECK(calls==1&&called(0,0x41f790,A,0,0,P,G1,1)&&v==pr(0,0));
    /* S = O (other side), f = 1 (A in the player's group): sum below 8000. */
    reset();actors();w32(0,A+0x281,7990);ret[0][0]=0x44;
    v=es2_mech_attacked_registers(&memory,&g,A,O,0xffff0009u,entry);
    CHECK(calls==1&&called(0,0x411aec,A,O,0,0,1,G1)&&r32(0,A+0x27d)==30000&&r32(0,A+0x281)==7999&&v==pr(0x44,9));
    /* f = 0, the leader holds and S is not a building: radio 3, byte A+9Ah
     * cleared (S is the leader's target); flag +0Ah: 41dbcc, target_search
     * (no result), the switch to S, script_start and part_pick. */
    reset();actors();w32(0,P+0x45,G2);w8(L+0xa3,1);w32(0,L+0x1a4,O);w8(SR+2,1);w8(A+0x9a,9);
    w32(0,A+0x1a4,N);w16(0,N+0x1a2,4);w16(0,O+0x1a2,1);
    ret[0][0]=0x50;ret[0][1]=0x51;ret[0][2]=0x52;ret[1][0]=0x60;ret[1][1]=0x61;ret[1][2]=0x62;ret[2][0]=0;ret[2][1]=0x71;ret[2][2]=0x72;
    ret[3][0]=0;ret[3][1]=0x81;ret[3][2]=0x82;ret[4][0]=0x90;ret[4][1]=0x91;ret[4][2]=0x92;ret[5][0]=0xa0;ret[5][1]=0xa1;
    v=es2_mech_attacked_registers(&memory,&g,A,O,0,entry);
    CHECK(calls==6&&called(1,0x420a98,A,3,0,L+0x92,G1,LIST)&&called(2,0x41dbcc,A,0,0,SR,0x61,0x62));
    CHECK(called(3,0x41e0e0,A,0,0,0,0x71,A)&&r8(A+0x9a)==0&&r32(0,A+0x273)==10000);
    CHECK(r32(0,A+0x1a4)==O&&r16(0,N+0x1a2)==3&&r16(0,O+0x1a2)==2&&r8(A+0x9d)==1);
    CHECK(called(4,0x413e50,A+0x4d,G(0x499784),0,A+0x4d,A+0x92,O)&&called(5,0x41ce08,A,0,0,0x90,0x91,0x92)&&v==pr(0xa0,0xa1));
    /* A building attacker (+1A8h nonzero): attack_target(A, S). */
    reset();actors();w32(0,P+0x45,G2);w8(SR+2,1);w16(0,O+0x1a8,2);w32(0,A+0x1a4,O);
    ret[2][0]=0;ret[3][0]=0x33;ret[3][1]=0x34;
    v=es2_mech_attacked_registers(&memory,&g,A,O,0,entry);
    CHECK(calls==4&&called(3,0x41c0f4,A,O,0,O,0,0)&&v==pr(0x33,0x34)&&r8(A+0x9d)==0);
    /* Reaction timer running, or S already the target with +9 set: nothing more. */
    reset();actors();w32(0,P+0x45,G2);w32(0,A+0x273,1);CHECK(es2_mech_attacked_registers(&memory,&g,A,O,0,entry)==pr(A+0x92,0)&&calls==1);
    reset();actors();w32(0,P+0x45,G2);w32(0,A+0x1a4,O);w8(SR+1,1);
    CHECK(es2_mech_attacked_registers(&memory,&g,A,O,0,entry)==pr(SR,0)&&calls==1&&r32(0,A+0x273)==0);
}
static void attacked_select(void){
    const es2_weapon_graph g=graph();
    /* Flag +0Ah clear, +0Bh clear: target_select(A, 24h, 0); its result
     * attacks someone else, order state 4 with a live order target: take it. */
    reset();actors();w32(0,P+0x45,G2);w16(0,A+0x23e,4);w32(0,A+0x248,N);w32(0,A+0x1a4,0);w16(0,O+0x1a8,1);
    ret[0][0]=0x10;ret[1][0]=O;ret[1][1]=0x22;ret[1][2]=0x23;ret[2][0]=0x99;ret[2][1]=0x98;
    uint64_t v=es2_mech_attacked_registers(&memory,&g,A,O,0,entry);
    CHECK(calls==3&&called(1,0x411fa0,A,0x24,0,SR,G1,LIST)&&r32(0,A+0x1a4)==N&&r16(0,N+0x1a2)==1);
    CHECK(called(2,0x41d5ec,A,0,0,A+0x92,N,A+0x92)&&r8(A+0xac)==1&&v==pr(0x99,0x98)&&r16(0,A+0x23e)==4);
    /* The order target is out: state 0, no target, the old target resumes. */
    reset();actors();w32(0,P+0x45,G2);w16(0,A+0x23e,4);w32(0,A+0x248,N);w8(N+0x99,1);w32(0,A+0x1a4,O);w16(0,O+0x1a2,1);w16(0,O+0x1a8,1);
    ret[1][0]=0;ret[1][1]=0x22;ret[2][0]=0x31;ret[2][1]=0x32;
    v=es2_mech_attacked_registers(&memory,&g,A,O,0,entry);
    CHECK(r16(0,A+0x23e)==0&&r32(0,A+0x1a4)==0&&r16(0,O+0x1a2)==0&&calls==3&&called(2,0x41eb34,A,0,0,A+0x92,0,A+0x92)&&v==pr(0x31,0x32));
    /* +0Bh set: nothing after 411aec. */
    reset();actors();w32(0,P+0x45,G2);w8(SR+3,1);ret[0][0]=0x10;
    CHECK(es2_mech_attacked_registers(&memory,&g,A,O,0,entry)==pr(SR,G1)&&calls==1);
}
int main(void){
    contact();kill();alert();attacked();attacked_select();
    printf("mech_notices: %u checks\n",checks);
    return 0;
}
