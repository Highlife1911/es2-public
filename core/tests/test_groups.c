#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/groups.h"
/* Synthetic group state for the cores. Objects live in an arena at
 * BASE; the image window (globals 4a0be0..6cbee4) is a separate buffer at
 * IMAGE. Game callees are stubs that record their slots and registers; the
 * order leaves resolve to the cores themselves. */
#define BASE UINT32_C(0x90000000)
#define IMAGE UINT32_C(0x00400000)
#define VA(x) ((uint32_t)(x))
static uint8_t arena[0x4000],image[0x2d8000];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"groups line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;
    if(a-BASE<sizeof(arena))return arena+(a-BASE);
    CHECK(a-IMAGE<sizeof(image));return image+(a-IMAGE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
/* Layout: groups 0x000 (stride 0x7a), orders 0x300 (0x16), triggers 0x400,
 * member arrays 0x480, routes 0x500, point pointers 0x540, actors 0x800
 * (stride 0x260), vtable 0x2000, out 0x2100, counter 0x2140, objects
 * 0x2180, frame scratch 0x3000. */
#define GROUP(k) (BASE+0x7au*(k))
#define ORDER(k) (BASE+0x300u+0x16u*(k))
#define TRIGGER(k) (BASE+0x400u+0x10u*(k))
#define ACTOR(k) (BASE+0x800u+0x260u*(k))
#define VTABLE (BASE+0x2000u)
#define OUT (BASE+0x2100u)
#define COUNTER (BASE+0x2140u)
#define SCRATCH (BASE+0x3000u)
typedef struct call_log { uint32_t f,a[4],r[3]; } call_log;
static call_log calls_seen[64];static unsigned ncalls;
static unsigned rejects_404,rejects_face,near_calls;static uint32_t rng_value,order_done_result,radius_word;
static es2_weapon_graph graph(void);
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;es2_weapon_graph g=graph();uint64_t p=0;
    CHECK(ncalls<64);call_log *l=calls_seen+ncalls++;memset(l,0,sizeof(*l));l->f=f;memcpy(l->a,a,4*n);l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    if(f==VA(0x4238d4)){CHECK(n==1);p=es2_order_point_registers(&memory,a[0]);}
    else if(f==VA(0x41b804)||f==VA(0x411cec)){CHECK(n==1);p=0x0badf00d00000000ull|a[0];}
    else if(f==VA(0x4239fc)){CHECK(n==2);p=order_done_result;}
    else if(f==VA(0x49630c)){CHECK(!n);w32(0,SCRATCH+208-0x24,0xfeedbeefu);p=0xfeedbeefu;}
    else if(f==VA(0x492ee0)){CHECK(n==1&&a[0]==VA(0x4d2599));p=(uint64_t)0x7777u<<32|rng_value;}
    else if(f==VA(0x42354c)){CHECK(n==4);uint32_t g0=g.scratch;(void)g0;
        w32(0,a[0],1000+a[1]);w32(0,a[0]+4,(uint16_t)a[2]);w32(0,a[0]+8,a[3]);p=0x5555000044440000ull;}
    else if(f==VA(0x471b30)){CHECK(n==1);p=r32(0,a[0])?BASE+0x2180u:0;}
    else if(f==VA(0x409b44)){CHECK(n==3&&a[0]==BASE+0x2180u);w32(0,COUNTER,r32(0,COUNTER)+10);}
    else if(f==0x7800u){CHECK(n==2);w32(0,a[1],r32(0,a[1])+r32(0,a[0]+0x10));}   /* +78: offset c by the member's tag */
    else if(f==0x7c00u){CHECK(n==1);p=radius_word;}
    else if(f==VA(0x47eb30)){CHECK(n==2);w16(0,a[1],0x4000);w16(0,a[1]+2,0);w16(0,a[1]+4,0);w16(0,a[1]+6,0x4000);}
    else if(f==VA(0x47ffe8)){CHECK(n==3);w32(0,a[2],r32(0,a[0]));w32(0,a[2]+4,r32(0,a[0]+4));p=a[2];}
    else if(f==VA(0x49288c)){CHECK(n==2);++near_calls;p=near_calls==1?100u:100000u;}
    else if(f==VA(0x404ae4)){CHECK(n==3&&a[1]==5000&&a[2]==0);p=rejects_404?(--rejects_404,0xabcd0001u):0xabcd0000u;}
    else if(f==VA(0x46ff90)){CHECK(n==3);p=rejects_face?(--rejects_face,0x0101u):0x0100u;p|=(uint64_t)0x46ffu<<32;}
    else CHECK(0);
    r->eax=(uint32_t)p;r->edx=(uint32_t)(p>>32);r->ecx=0x0c0c0c0cu;
}
static es2_weapon_graph graph(void){es2_weapon_graph g={IMAGE,SCRATCH,0,0,0,call,0,0,0};return g;}
/* member_assess callees: actor_safe returns ma_safe, pool_prev walks
 * the pool's object list backwards (the pool dword is its index 1..3),
 * dist_gate returns the planned distances in order, random_damage counts. */
static uint32_t ma_safe,ma_list[4][4],ma_dist[16];static unsigned ma_count[4],ma_dists,ma_damage;
static void ma_call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(ncalls<64);call_log *l=calls_seen+ncalls++;memset(l,0,sizeof(*l));l->f=f;memcpy(l->a,a,4*n);l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    uint32_t eax=0;
    if(f==VA(0x4137b4)){CHECK(n==1);eax=ma_safe;}
    else if(f==VA(0x471c70)){CHECK(n==2&&a[0]>=1&&a[0]<=3);const uint32_t *list=ma_list[a[0]];unsigned k=ma_count[a[0]];
        if(!a[1])eax=k?list[k-1]:0;else{for(unsigned j=0;j<k;++j)if(list[j]==a[1])eax=j?list[j-1]:0;}}
    else if(f==VA(0x49288c)){CHECK(n==2&&ma_dists<16);eax=ma_dist[ma_dists++];}
    else if(f==VA(0x417a04)){CHECK(n==3&&a[1]==0x40&&a[2]==0x40);++ma_damage;eax=0x4170000u+ma_damage;}
    else CHECK(0);
    r->eax=eax;r->edx=0xedd00000u+ncalls;r->ecx=0xecc00000u+ncalls;
}
static es2_weapon_graph ma_graph(void){es2_weapon_graph g={IMAGE,SCRATCH,0,0,0,ma_call,0,0,0};return g;}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
static uint32_t order(unsigned k,uint16_t type,uint16_t scope,uint32_t subject,uint32_t trigger){
    uint32_t o=ORDER(k);w16(0,o,type);w16(0,o+0xc,scope);w32(0,o+0xe,subject);w32(0,o+0x12,trigger);return o;
}
static void members(uint32_t group,uint32_t array,unsigned first,unsigned count){
    w32(0,group+0xc,array);w16(0,group+0x10,(uint16_t)count);
    for(unsigned i=0;i<count;++i){uint32_t a=ACTOR(first+i);w32(0,array+4*i,a);w32(0,a,VTABLE);w32(0,a+0x45,group);w32(0,a+0x10,i);}
}
int main(void){
    es2_weapon_graph g=graph();
    w32(0,VTABLE+0x78,0x7800u);w32(0,VTABLE+0x7c,0x7c00u);
    uint32_t g0=GROUP(0),g1=GROUP(1);members(g0,BASE+0x480,0,3);members(g1,BASE+0x4a0,6,1);
    /* order_actor / order_point: group scope, actor scope, other scope, null. */
    w32(0,g0+0x6c,2);w32(0,g0+0x44+8,order(0,3,0,g1,0));
    CHECK(es2_order_actor_registers(&memory,g0)==((uint64_t)2<<32|ACTOR(6)));
    CHECK(es2_order_point_registers(&memory,g0)==((uint64_t)(ACTOR(6)+0x26)<<32|(ACTOR(6)+0x26)));
    w16(0,ORDER(0)+0xc,2);w32(0,ORDER(0)+0xe,ACTOR(2));
    CHECK(es2_order_actor_registers(&memory,g0)==((uint64_t)2<<32|ACTOR(2)));
    CHECK(es2_order_point_registers(&memory,g0)==((uint64_t)ORDER(0)<<32|(ACTOR(2)+0x26)));
    w32(0,g0+6,BASE+0x500);w32(0,BASE+0x502,BASE+0x540);w32(0,BASE+0x540,BASE+0x5c0);
    w16(0,ORDER(0)+0xc,0xffff);CHECK(es2_order_actor_registers(&memory,g0)==(uint64_t)2<<32);
    CHECK(es2_order_point_registers(&memory,g0)==((uint64_t)(BASE+0x500)<<32|(BASE+0x5c0)));
    w32(0,g0+0x6c,0x40000002u);w16(0,ORDER(0)+0xc,4);   /* 4*index wraps to slot 2 */
    CHECK(es2_order_actor_registers(&memory,g0)==((uint64_t)0x40000002u<<32));
    w32(0,g0+0x6c,3);CHECK(es2_order_actor_registers(&memory,g0)==((uint64_t)3<<32));
    CHECK(es2_order_point_registers(&memory,g0)==((uint64_t)(BASE+0x500)<<32|(BASE+0x5c0)));
    /* actor_destination by state. */
    uint32_t a0=ACTOR(0);w32(0,a0+0x248,ACTOR(1));w32(0,a0+0x24c,ACTOR(2));
    w16(0,a0+0x23e,3);CHECK(es2_actor_destination_registers(&memory,&g,a0,entry)==ACTOR(2)+0x26);
    w16(0,a0+0x23e,5);CHECK(es2_actor_destination_registers(&memory,&g,a0,entry)==ACTOR(1)+0x26);
    w16(0,a0+0x23e,6);CHECK(es2_actor_destination_registers(&memory,&g,a0,entry)==((uint64_t)(ACTOR(2)+0x26)<<32|(ACTOR(2)+0x26)));
    w32(0,a0+0x24c,0);CHECK(es2_actor_destination_registers(&memory,&g,a0,entry)==a0+0x240);
    w16(0,a0+0x23e,4);ncalls=0;
    CHECK(es2_actor_destination_registers(&memory,&g,a0,entry)==((uint64_t)(BASE+0x500)<<32|(BASE+0x5c0)));
    CHECK(ncalls==1&&calls_seen[0].f==VA(0x4238d4)&&calls_seen[0].a[0]==g0&&calls_seen[0].r[0]==a0&&calls_seen[0].r[1]==g0&&calls_seen[0].r[2]==entry[2]);
    /* group_assess: one call per member, ECX the previous member. */
    ncalls=0;uint64_t r=es2_group_assess_registers(&memory,&g,g0,entry);
    CHECK(ncalls==3&&calls_seen[1].r[2]==ACTOR(0)&&calls_seen[2].r[0]==ACTOR(2)&&calls_seen[0].r[1]==3);
    CHECK((uint32_t)r==ACTOR(2)&&(uint32_t)(r>>32)==3);
    ncalls=0;CHECK(es2_group_assess_registers(&memory,&g,GROUP(2),entry)==entry[0]);  /* empty: EAX entry, EDX 0 */
    /* order_update: done → completion byte, advance, countdowns cleared. */
    memset(host(0,g0+0x44),0,40);w32(0,g0+0x6c,1);w32(0,g0+0x48,ORDER(1));w32(0,g0+0x4c,ORDER(2));order(1,3,0,g0,0);
    for(unsigned i=0;i<3;++i)w32(0,ACTOR(i)+0x52,77);
    order_done_result=1;ncalls=0;r=es2_order_update_registers(&memory,&g,g0,entry);
    CHECK(*(uint8_t *)host(0,g0+0x71)==1&&r32(0,g0+0x6c)==2&&!r32(0,ACTOR(0)+0x52)&&!r32(0,ACTOR(2)+0x52));
    CHECK(ncalls==4&&calls_seen[0].f==VA(0x4239fc)&&calls_seen[0].a[0]==g0&&calls_seen[0].a[1]==ORDER(1)&&calls_seen[0].r[0]==1);
    CHECK(calls_seen[1].f==VA(0x411cec)&&calls_seen[1].r[2]==3);   /* ECX = the count of the clearing loop */
    /* Not done, latched trigger: advance only when the next slot exists. */
    order_done_result=0;w32(0,ORDER(2)+0x12,TRIGGER(0));w16(0,TRIGGER(0)+0xa,1);w32(0,ACTOR(1)+0x52,5);
    ncalls=0;r=es2_order_update_registers(&memory,&g,g0,entry);CHECK(r32(0,g0+0x6c)==2&&r32(0,ACTOR(1)+0x52)==5&&ncalls==4);
    w32(0,g0+0x50,ORDER(1));r=es2_order_update_registers(&memory,&g,g0,entry);CHECK(r32(0,g0+0x6c)==3&&!r32(0,ACTOR(1)+0x52));
    w32(0,g0+0x6c,9);w32(0,g0+0x68,ORDER(2));r=es2_order_update_registers(&memory,&g,g0,entry);CHECK(r32(0,g0+0x6c)==9);
    /* spawn_point: the player at (100,200,300), heading 10; identity
     * rotation; one actor rejects once (d = 100 < 300 + 5000), then 404ae4
     * twice and face_test once: five candidates. */
    uint32_t player=ACTOR(5);w32(0,VA(0x4d24e6),player);w32(0,player+0x26,100);w32(0,player+0x2a,200);w32(0,player+0x2e,300);w16(0,player+0x10,10);
    w32(0,VA(0x4a9af8),BASE+0x600);w16(0,VA(0x4a9afe),2);w32(0,BASE+0x600,ACTOR(3));w32(0,BASE+0x604,ACTOR(4));
    for(unsigned k=3;k<5;++k){w32(0,ACTOR(k),VTABLE);w32(0,ACTOR(k)+0x45,g1);}
    w32(0,g1+0x14,0);radius_word=0x1234012cu;w32(0,VA(0x4a0be0),0x6060u);
    near_calls=0;rejects_404=2;rejects_face=1;ncalls=0;
    r=es2_spawn_point_registers(&memory,&g,OUT,1000,0x12340005u,1,entry);
    CHECK(r==((uint64_t)0x46ffu<<32|0x0100u));
    CHECK(calls_seen[0].f==VA(0x47eb30)&&calls_seen[0].a[0]==0x1234000fu&&calls_seen[0].r[2]==player);
    CHECK(r32(0,OUT)==100&&r32(0,OUT+4)==1000+4*2000+200&&r32(0,OUT+8)==5*300);
    CHECK(r32(0,SCRATCH+0x30-0x28)==1000+5*2000&&near_calls==9);
    /* group_activate mode 2: rng, placement, allocation, constructor. */
    uint32_t t=TRIGGER(1);w32(0,g1+0x14,t);w16(0,t+2,2);w16(0,t+0xa,0);
    ncalls=0;CHECK(es2_group_activate_registers(&memory,&g,g1,0,entry)==((uint64_t)t<<32|0xfeedbeefu)&&ncalls==1);
    w16(0,t+0xa,1);rng_value=0xabcd8123u;w32(0,VA(0x4a96aa),BASE+0x2170);w32(0,BASE+0x2170,1);
    w32(0,VA(0x6cbee4),COUNTER);w32(0,COUNTER,100);*(uint8_t *)host(0,g1+0x13)=0;
    ncalls=0;r=es2_group_activate_registers(&memory,&g,g1,0,entry);
    CHECK(ncalls==5&&calls_seen[2].f==VA(0x42354c)&&calls_seen[2].a[1]==150000&&calls_seen[2].a[2]==((0xabcd0000u)|(uint16_t)(0x4000-0x0123)));
    CHECK(r32(0,COUNTER)==106&&r32(0,g1+0x14)==t&&*(uint8_t *)host(0,g1+0x13)==1&&r16(0,SCRATCH+208-0x14)==8);
    ncalls=0;r=es2_group_activate_registers(&memory,&g,g1,0,entry);CHECK(ncalls==1);   /* started: nothing */
    /* Mode 3 with an empty pool: started, still pending. */
    w16(0,t+2,3);*(uint8_t *)host(0,g1+0x13)=0;w32(0,BASE+0x2170,0);ncalls=0;r=es2_group_activate_registers(&memory,&g,g1,0,entry);
    CHECK(ncalls==4&&calls_seen[2].a[2]==(0xabcd0000u|(uint16_t)(0x1000-0x0123))&&r32(0,COUNTER)==106&&r32(0,g1+0x14)==t);
    /* Mode 4: three members, the first placed and turned, the others via +78. */
    w32(0,g0+0x14,t);w16(0,t+2,4);ncalls=0;r=es2_group_activate_registers(&memory,&g,g0,0xbeefu,entry);
    uint32_t angle=0xbeef0000u|(uint16_t)(0x1000-0x0123+0x8000);
    CHECK(calls_seen[2].a[1]==90000&&calls_seen[2].a[2]==angle&&calls_seen[2].a[3]==1);
    CHECK(r32(0,ACTOR(0)+0x26)==91000&&r16(0,ACTOR(0)+0x10)==(uint16_t)(angle-0x8000)&&r16(0,ACTOR(0)+0x32)==0);
    CHECK(r32(0,ACTOR(1)+0x26)==91000+1&&r32(0,ACTOR(2)+0x26)==91000+2&&r32(0,ACTOR(2)+0x2a)==(uint16_t)angle&&r32(0,ACTOR(2)+0x2e)==1);
    CHECK(!r32(0,g0+0x14)&&(uint32_t)r==0xfeedbeefu);
    /* Mode 5 with one member: EDX = the angle word over the caller's ESI high word, minus 8000h. */
    w32(0,g1+0x14,t);w16(0,t+2,5);r=es2_group_activate_registers(&memory,&g,g1,0x1234u,entry);
    CHECK((uint32_t)(r>>32)==((0x12340000u|(uint16_t)(0x2000-0x0123))-0x8000u)&&!r32(0,g1+0x14));
    /* Other modes clear the pending pointer. */
    w32(0,g1+0x14,t);w16(0,t+2,1);ncalls=0;es2_group_activate_registers(&memory,&g,g1,0,entry);CHECK(!r32(0,g1+0x14)&&ncalls==1);
    /* member_assess: safe -> nothing; else MECH pool [A0 friend 44999 r700,
     * A1 hostile 100 r900, A2 down], FLYER [F hostile 49999], buildings
     * [B0 hostile 34999 with +2Eh, B1 without +2Eh]: F = 700 - 600 = 100,
     * H = 900 + 500 + 500 = 1900, five random_damage calls. */
    {const es2_weapon_graph mg=ma_graph();const uint32_t M=BASE+0x3100,GF=BASE+0x2800,GH=BASE+0x2880;
    #define MO(k) (BASE+0x3280u+0x180u*(k))
    #define MC(k) (BASE+0x2400u+0x60u*(k))
    memset(host(0,BASE+0x2400),0,0x1c00);*(uint8_t *)host(0,GF+0x12)=1;*(uint8_t *)host(0,GH+0x12)=2;
    w32(0,M+0x45,GF);for(unsigned k=0;k<6;++k){w32(0,MO(k)+0x45,k==0?GF:GH);w32(0,MO(k)+0x1f2,MC(k));}
    w16(0,MC(0)+0x54,700);w16(0,MC(1)+0x54,900);*(uint8_t *)host(0,MO(2)+0xa5)=1;w16(0,MC(4)+0x2e,1);
    ma_list[1][0]=MO(2);ma_list[1][1]=MO(1);ma_list[1][2]=MO(0);ma_count[1]=3;ma_list[2][0]=MO(3);ma_count[2]=1;
    ma_list[3][0]=MO(5);ma_list[3][1]=MO(4);ma_count[3]=2;w32(0,VA(0x4a9b7a),1);w32(0,VA(0x4a9db9),2);w32(0,VA(0x4a95a0),3);
    ma_safe=0x10000u;ncalls=0;ma_dists=0;ma_damage=0;ma_dist[0]=44999;ma_dist[1]=100;ma_dist[2]=49999;ma_dist[3]=34999;
    r=es2_member_assess_registers(&memory,&mg,M,entry);
    CHECK(calls_seen[0].f==VA(0x4137b4)&&calls_seen[0].a[0]==M&&calls_seen[1].f==VA(0x471c70)&&calls_seen[1].a[0]==1&&calls_seen[1].a[1]==0);
    CHECK(calls_seen[2].f==VA(0x49288c)&&calls_seen[2].a[0]==M+0x26&&calls_seen[2].a[1]==MO(0)+0x26&&calls_seen[2].r[0]==M+0x26&&calls_seen[2].r[1]==0&&calls_seen[2].r[2]==MO(0)+0x26);
    CHECK(calls_seen[3].f==VA(0x471c70)&&calls_seen[3].a[1]==MO(0)&&calls_seen[3].r[0]==MO(0)&&calls_seen[3].r[1]==MO(0));
    CHECK(ma_dists==4&&ma_damage==5&&calls_seen[ncalls-1].f==VA(0x417a04)&&calls_seen[ncalls-1].a[0]==M);
    CHECK(r==((uint64_t)(0xedd00000u+ncalls)<<32|(0x4170000u+5)));
    /* The building pass: EAX = A+26h, EDX = M+26h, ECX = the chassis. */
    {unsigned k=0;while(k<ncalls&&!(calls_seen[k].f==VA(0x49288c)&&calls_seen[k].a[1]==MO(4)+0x26))++k;
    CHECK(k<ncalls&&calls_seen[k].r[0]==MO(4)+0x26&&calls_seen[k].r[1]==M+0x26&&calls_seen[k].r[2]==MC(4));}
    /* Friends 1300 - 600 = 700 >= 1900? No: with A1 friendly (F = 1600 - 600 = 1000, H = 1000) nothing. */
    w32(0,MO(1)+0x45,GF);ncalls=0;ma_dists=0;ma_damage=0;r=es2_member_assess_registers(&memory,&mg,M,entry);
    CHECK(!ma_damage&&(uint32_t)r==0);
    /* actor_safe nonzero: its pair, nothing else. */
    ma_safe=0x12340001u;ncalls=0;r=es2_member_assess_registers(&memory,&mg,M,entry);
    CHECK(ncalls==1&&r==((uint64_t)0xedd00001u<<32|0x12340001u));
    #undef MO
    #undef MC
    }
    puts("PASS: groups: order leaves (scopes, null, wrapped index), destinations by state, member passes, order completion/advance, spawn retries with Z accumulation, activation modes 2-5");
    printf("%u checks\n",checks);return 0;
}
