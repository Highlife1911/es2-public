#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/projectile.h"
/* Synthetic guest for the steering functions: objects at 90000000h,
 * the image base B for direct callees. Expected values are worked out by
 * hand from docs/reverse-engineering/weapons.md ("Projectile and bullet
 * steering"); exact equivalence with the original is checked in vitro
 * (projectile_cases.h). The correction table is zero here. */
#define OBJ UINT32_C(0x90000000)
#define B UINT32_C(0x00400000)
enum { O=0x000, T=0x200, OWNER=0x400, VT=0x600, SCR=0x800, ATT=0x900, OUT=0xa00, END=0xa40, START=0xa80 };
static uint8_t arena[4096],corrections[513];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"projectile line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    fprintf(stderr,"projectile: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
static uint64_t pair(uint32_t eax,uint32_t edx){return (uint64_t)edx<<32|eax;}
#define V58 UINT32_C(0x77000058)
#define V24 UINT32_C(0x77000024)
typedef struct logged { uint32_t f,a[3],r[3]; } logged;
static logged log_[16];static unsigned calls;
static uint32_t v24_result;static uint16_t aim[3];
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<16);logged *l=log_+calls++;l->f=f;memcpy(l->a,a,4*n);
    l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    if(f==V58){for(unsigned i=0;i<3;++i)w32(0,a[2]+4*i,100*(i+1));r->eax=r->edx=r->ecx=0;}
    else if(f==V24)r->eax=v24_result;
    else if(f==0x492990u){ /* 492990: fixed angle words */
        for(unsigned i=0;i<3;++i)w16(0,a[0]+2*i,aim[i]);r->eax=a[0];r->edx=0x1234;r->ecx=0x5678;
    } else if(f==B+0x67adc){w16(0,a[0],(uint16_t)a[1]);r->eax=0;}           /* approach: jump to the target */
    else if(f==B+0x67924)r->eax=(uint32_t)(int32_t)(int16_t)a[0];            /* movement_step: identity */
    else if(f!=B+0x7ebb8&&f!=B+0x8043c){fprintf(stderr,"unexpected call %08x\n",f);exit(1);}
}
static void reset(void){
    memset(arena,0,sizeof(arena));calls=0;
    w32(0,OBJ+O+0x56,OBJ+T);w32(0,OBJ+O+0x5b,OBJ+T);w32(0,OBJ+O+0x4a,OBJ+OWNER);
    w32(0,OBJ+T,OBJ+VT);w32(0,OBJ+VT+0x58,V58);w32(0,OBJ+VT+0x24,V24);
    arena[T+0x96]=1;w16(0,OBJ+O+0xc,0x100);w16(0,OBJ+O+0xe,0x22);w16(0,OBJ+O+0x10,0x200);w16(0,OBJ+O+0x32,1);
}
static es2_weapon_graph graph(void){es2_weapon_graph g={B,OBJ+SCR,0,0,0,call,NULL,NULL,NULL};return g;}
static void vector_checks(void){
    CHECK(es2_vector_angle_registers(0,0,corrections)==es2_vector_angle_registers(1,0,corrections));
    CHECK(es2_vector_angle_registers(1,0,corrections)==0);
    CHECK((uint32_t)es2_vector_angle_registers(0,5,corrections)==0x4000);   /* exchanged: 4096 << 2 */
    CHECK((uint32_t)es2_vector_angle_registers(0xfffffffdu,0,corrections)==0x8000);
    CHECK(es2_vector_angle_ecx(0,0)==1&&es2_vector_angle_ecx(0,5)==5&&es2_vector_angle_ecx(0xfffffffdu,0)==3);
    CHECK(es2_vector_angle_ecx(7,7)==7&&es2_vector_angle_ecx(7,0xfffffff8u)==8&&es2_vector_angle_ecx(9,0)==9);
}
static void aim_checks(void){
    memset(arena,0,sizeof(arena));
    /* Straight along +Y: heading 4000h + C000h wraps to 0, pitch 0. */
    w32(0,OBJ+END+4,5);memset(arena+OUT,0xee,6);
    CHECK(es2_aim_angles_registers(&memory,OBJ+OUT,OBJ+END,OBJ+START,corrections)==pair(OBJ+OUT,0));
    CHECK(r16(0,OBJ+OUT)==0&&r16(0,OBJ+OUT+2)==0&&r16(0,OBJ+OUT+4)==0);
    /* Along -X: heading 8000h + C000h = 4000h. */
    memset(arena,0,sizeof(arena));w32(0,OBJ+END,0xfffffffdu);
    es2_aim_angles_registers(&memory,OBJ+OUT,OBJ+END,OBJ+START,corrections);CHECK(r16(0,OBJ+OUT+4)==0x4000);
    /* out over end+6: out+4/out+2 overwrite end.z's halves before it is read
     * (end.z becomes 0 and the pitch is 0, not the 2920h of z = 7). */
    memset(arena,0,sizeof(arena));w32(0,OBJ+END+4,5);w32(0,OBJ+END+8,7);
    es2_aim_angles_registers(&memory,OBJ+END+6,OBJ+END,OBJ+START,corrections);
    CHECK(r16(0,OBJ+END+6)==0&&r32(0,OBJ+END+8)==0);
}
static void projectile_checks(void){
    es2_weapon_graph g=graph();const uint32_t entry[3]={0x11111111u,0x22222222u,0x33333333u};
    /* No target: the entry pair. */
    reset();w32(0,OBJ+O+0x56,0);
    CHECK(es2_projectile_steer_registers(&memory,&g,OBJ+O,entry)==pair(0x11111111u,0x22222222u)&&!calls);
    /* Owner a3 set and locked on another target. */
    reset();arena[OWNER+0xa3]=1;w32(0,OBJ+OWNER+0x1a4,OBJ+0x300);
    CHECK(es2_projectile_steer_registers(&memory,&g,OBJ+O,entry)==pair(OBJ+OWNER+0x92,OBJ+OWNER)&&!calls);
    /* Type 2 without either target flag. */
    reset();arena[T+0x96]=0;arena[O+0x41]=2;
    CHECK(es2_projectile_steer_registers(&memory,&g,OBJ+O,entry)==pair(0,2)&&!calls);
    /* V58 path, no push: deltas 1000h/3000h fully applied. */
    reset();w16(0,OBJ+O+0x5a,1);aim[0]=0x1100;aim[1]=0;aim[2]=0x3200;
    CHECK(es2_projectile_steer_registers(&memory,&g,OBJ+O,entry)==pair(OBJ+O+0xc,0x00223200u));
    CHECK(r16(0,OBJ+O+0xc)==0x1100&&r16(0,OBJ+O+0xe)==0x22&&r16(0,OBJ+O+0x10)==0x3200&&r16(0,OBJ+O+0x32)==0);
    CHECK(calls==6&&log_[0].f==V58&&log_[0].a[0]==OBJ+T&&log_[0].a[1]==0x90000001u&&log_[0].a[2]==OBJ+SCR);
    CHECK(log_[0].r[0]==OBJ+VT&&log_[0].r[1]==OBJ+SCR&&log_[0].r[2]==OBJ+T);
    CHECK(log_[1].a[0]==OBJ+SCR+0x18&&log_[1].a[1]==OBJ+SCR&&log_[1].a[2]==OBJ+O+0x26);
    CHECK(log_[2].a[0]==OBJ+SCR+0x10&&(uint16_t)log_[2].a[1]==0x1000&&log_[2].a[2]==0x500&&log_[2].r[0]==OBJ+SCR+0x10);
    CHECK(log_[3].a[0]==OBJ+SCR+0x14&&(uint16_t)log_[3].a[1]==0x3000&&log_[3].r[2]==OBJ+SCR+0x14);
    CHECK(log_[4].r[2]==OBJ+SCR+0x14&&log_[5].r[2]==log_[4].a[0]);
    /* Near-zero push: heading +100h becomes -B00h, pitch -100h becomes +B00h. */
    reset();w16(0,OBJ+O+0x5a,1);arena[OWNER+0x9c]=1;aim[0]=0x0000;aim[2]=0x300;
    es2_projectile_steer_registers(&memory,&g,OBJ+O,entry);
    CHECK(r16(0,OBJ+O+0x10)==(uint16_t)(0x200+0x100-0xc00)&&r16(0,OBJ+O+0xc)==(uint16_t)(0x100-0x100+0xc00));
    /* Type 3 sets owner+B5h and skips the push. */
    reset();w16(0,OBJ+O+0x5a,1);arena[O+0x41]=3;arena[OWNER+0x9c]=1;
    es2_projectile_steer_registers(&memory,&g,OBJ+O,entry);
    CHECK(arena[OWNER+0xb5]==1&&r16(0,OBJ+O+0x10)==0x300);
    /* V24 path with an attachment and an empty matrix cache. */
    reset();w16(0,OBJ+O+0x5a,0x8000);v24_result=OBJ+ATT;w16(0,OBJ+T+0xc+0x26,0);aim[0]=0x100;aim[2]=0x200;
    es2_projectile_steer_registers(&memory,&g,OBJ+O,entry);
    CHECK(log_[0].f==V24&&log_[0].r[0]==0x90008000u&&log_[1].f==B+0x7ebb8&&log_[1].a[0]==OBJ+T+0xc&&log_[1].r[2]==OBJ+T);
    CHECK(r16(0,OBJ+T+0x32)==1&&log_[2].f==B+0x8043c&&log_[2].a[0]==OBJ+ATT+0x14&&log_[2].a[2]==OBJ+SCR);
}
static void bullet_checks(void){
    es2_weapon_graph g=graph();const uint32_t entry[3]={0x11111111u,0x22222222u,0x33333333u};
    /* V24 returns 0: the target position is copied into the frame. */
    reset();v24_result=0;w32(0,OBJ+T+0x26,7);w32(0,OBJ+T+0x2a,8);w32(0,OBJ+T+0x2e,9);aim[0]=0x1100;aim[2]=0x3200;
    CHECK(es2_bullet_steer_registers(&memory,&g,OBJ+O,entry)==pair(0x90003000u,0x00223200u));
    CHECK(r32(0,OBJ+SCR)==7&&r32(0,OBJ+SCR+4)==8&&r32(0,OBJ+SCR+8)==9&&r16(0,OBJ+O+0x32)==0);
    CHECK(calls==6&&log_[0].f==V24&&log_[0].r[0]==0x90000200u&&log_[0].r[1]==OBJ+T&&log_[0].r[2]==OBJ+VT);
    CHECK(log_[2].a[2]==0x280&&log_[2].r[2]==OBJ+SCR+0x10&&log_[3].r[1]==OBJ+SCR+0x14);
}
int main(void){
    vector_checks();aim_checks();projectile_checks();bullet_checks();
    printf("projectile: %u checks, 0 failures\n",checks);return 0;
}
