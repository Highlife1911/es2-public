#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/vehicle.h"
/* Synthetic checks of the core on an own arena with scripted callees:
 * the condition table and ratio, the sight check with and without a
 * pending timer, the group levels, a bearing record and the three paths of
 * part_position. Values are hand-derived; the in-vitro harness compares
 * against the original. */
#define BASE UINT32_C(0x90000000)
#define IMAGE UINT32_C(0x00400000)
static uint8_t arena[0x20000];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"vehicle line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
static unsigned calls;static uint32_t functions[32],args[32][4];static es2_weapon_registers entries[32];
static uint32_t part_values[8],record_a;
/* Scripted callees: the image base of the current graph (direct
 * targets are normalized to their VA), the ratio part_ratio returns, the
 * angle words aim_angles writes per call, weapon_keys' result, the ESI the
 * bridge would load and the 4801d4 output switch. */
static uint32_t mock_image=IMAGE,ratio,keys_eax,*bridge_esi,esi_seen[32],aim_words[8][3];static unsigned aim_calls;static int write_out;
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<32);if(f-0x7000u>=0x100u)f=f-mock_image+IMAGE;
    functions[calls]=f;entries[calls]=*r;esi_seen[calls]=bridge_esi?*bridge_esi:0;for(unsigned i=0;i<n&&i<4;++i)args[calls][i]=a[i];++calls;
    switch(f){
    case 0x40cf10:CHECK(n==2);r->eax=0xabcd0001u;r->edx=0x4242u;r->ecx=0x5353u;break;
    case 0x49681a:CHECK(n==3&&a[1]==0);memset(host(0,a[0]),0,a[2]);r->eax=a[0];r->edx=a[2];r->ecx=0x1234u;break;
    case 0x47e0b0:CHECK(n==2);r->eax=(uint32_t)(((int64_t)(int32_t)a[0]*(int32_t)a[1])>>10);r->edx=0;r->ecx=0x0c0c0000u;break;
    case 0x40dbc0:CHECK(n==2);r->eax=0xfedc0000u|ratio;r->ecx=0x0d0d0000u;break;
    case 0x413ab8:CHECK(n==2);w16(0,a[0]+6,(uint16_t)a[1]);r->eax=a[0];r->edx=(r->edx&0xffff0000u)|(uint16_t)a[1];break;
    case 0x7058:CHECK(n==3);w32(0,a[2],a[1]);w32(0,a[2]+4,0);w32(0,a[2]+8,0);break; /* V58 */
    case 0x492990:CHECK(n==3&&aim_calls<8);for(unsigned j=0;j<3;++j)w16(0,a[0]+2*j,(uint16_t)aim_words[aim_calls][j]);++aim_calls;
        r->edx=0x0e0e0000u;r->ecx=0x0f0f0000u;break;
    case 0x410f78:CHECK(n==2);r->eax=keys_eax;r->edx=0x41410000u;r->ecx=0x42420000u;break;
    case 0x438d9c:CHECK(n==3&&a[2]==1);r->edx=0x438d9c;break;
    case 0x492f24:CHECK(n==2&&a[0]==IMAGE+0xd2599u&&a[1]==1000);r->eax=0x12340010u;break;
    case 0x7024:CHECK(n==1);r->eax=a[0]==BASE+0x1000?record_a:0;r->edx=0x5555u;break; /* V24 */
    case 0x4280f4:CHECK(n==4&&a[3]==0);r->eax=r32(0,a[1]+8)==1000u+1000u&&r32(0,a[2]+8)==2000u+500u?0x77770000u:1u;break;
    case 0x7040:CHECK(n==1);r->eax=0xabcd0000u|part_values[calls%8];break; /* V40 */
    case 0x492934:CHECK(n==2);r->eax=0x00011000u;r->ecx=0x22220000u;break;
    case 0x703c:CHECK(n==1);r->eax=0x20;break; /* V3c */
    case 0x49288c:CHECK(n==2);r->eax=5000;r->edx=0x99990000u;break;
    case 0x7020:CHECK(n==2&&a[1]==7);r->eax=BASE+0x900;break; /* V20 */
    case 0x4801d4:if(write_out){CHECK(n==3);w32(0,a[2],10);w32(0,a[2]+4,20);w32(0,a[2]+8,30);r->edx=0x48480000u;}break;
    case 0x47ebb8:break;
    case 0x48043c:CHECK(n==3);r->eax=0x1234;r->edx=0x5678;break;
    default:fprintf(stderr,"unexpected callee %08x\n",(unsigned)f);CHECK(0);
    }
}
static void tables(uint32_t P){
    uint32_t values=BASE+0x100,groups=BASE+0x180,records=BASE+0x200,maxima=BASE+0x480,tabs=BASE+0x4c0;
    w32(0,P,values);w32(0,P+4,groups);w32(0,P+0xc,tabs);w32(0,tabs,records);w32(0,tabs+4,maxima);
    for(unsigned i=0;i<29;++i)w16(0,records+18*i,100);
    for(unsigned i=0;i<22;++i)w16(0,maxima+2*i,200);
    w16(0,groups,50);w16(0,groups+2,0xffff);w16(0,groups+38,0xffff);
    w16(0,values,100);w16(0,maxima+2,0);w16(0,values+4,0xfffb);w16(0,values+22,7);w16(0,values+24,300);
}
int main(void){
    const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
    /* condition_table. */
    uint32_t P=BASE,out=BASE+0x600;tables(P);
    uint64_t r=es2_condition_table_registers(&memory,P,out);
    CHECK(r16(0,out+2)==128&&r16(0,out+4)==0x100&&r16(0,out+6)==0); /* groups 0..2 */
    CHECK(r16(0,out+0x40)==341&&r16(0,out+0x42)==0);                /* group 19 + part 12: (100+300)<<8 / 300 */
    CHECK(r16(0,out+0x28)==128&&r16(0,out+0x2a)==0&&r16(0,out+0x2c)==0x100&&r16(0,out+0x3e)==8);
    CHECK(r==((uint64_t)192<<32|8));                                  /* part 11: 7 << 8 / 200 */
    /* condition_ratio. */
    CHECK(es2_condition_ratio_registers(&memory,P,0,0,entry)==128);
    CHECK(es2_condition_ratio_registers(&memory,P,0xffff0000u,0xabcd0002u,entry)==((uint64_t)0xabcd0002u<<32|((r32(0,P)+4u)&0xffff0000u)|0x100u));
    CHECK(es2_condition_ratio_registers(&memory,P,1,0,entry)==128);
    CHECK(es2_condition_ratio_registers(&memory,P,2,0,entry)==0x100);
    w16(0,BASE+0x700,30);w16(0,BASE+0x702,60);
    const uint32_t other[3]={BASE+0x702,0,BASE+0x700};
    CHECK(es2_condition_ratio_registers(&memory,P,3,0,other)==128);
    /* sight_check: viewer record height 1000, target null (500). */
    uint32_t A=BASE+0x1000,B=BASE+0x1400,rec=BASE+0x1800,scratch=BASE+0x1900;
    es2_weapon_graph g={IMAGE,scratch,0,0,0,call,NULL,NULL,NULL};
    memset(arena+0x3000,0,0x200);
    /* Tables live in the arena; their slots hold the scripted callee ids. */
    w32(0,A,BASE+0x3000);w32(0,B,BASE+0x3100);w32(0,BASE+0x3024,0x7024);w32(0,BASE+0x3124,0x7024);
    w32(0,A+0x2e,1000);w32(0,B+0x2e,2000);w16(0,B+0x4b,5);w16(0,B+0x1e3,0);w32(0,rec+0x1c,1000);record_a=rec;
    calls=0;r=es2_sight_check_registers(&memory,&g,A,B,entry);
    CHECK(calls==4&&functions[0]==0x492f24&&functions[3]==0x4280f4&&entries[0].eax==entry[0]&&entries[0].ecx==entry[2]);
    CHECK(r16(0,A+0x1e3)==0x1398&&arena[0x1000+0x132+5]==1&&r==((uint64_t)(A+0x132)<<32|1));
    CHECK(entries[1].eax==2000&&entries[1].edx==BASE+0x3000&&entries[2].eax==rec&&entries[2].ecx==BASE+0x3100);
    w16(0,B+0x1e3,1);arena[0x1000+0x132+5]=0xff;calls=0;
    CHECK(es2_sight_check_registers(&memory,&g,A,B,entry)==((uint64_t)(A+0x132)<<32|0xffffffffu)&&!calls);
    /* group_state: members at 2000h/2400h with table 3200h (V40). */
    uint32_t G=BASE+0x1c00,list=BASE+0x1c40,M0=BASE+0x2000,M1=BASE+0x2400;
    w32(0,G+0xc,list);w32(0,list,M0);w32(0,list+4,M1);w16(0,G+0x10,2);
    w32(0,M0,BASE+0x3200);w32(0,M1,BASE+0x3200);w32(0,BASE+0x3240,0x7040);
    part_values[1]=100;part_values[2]=200;calls=0; /* avg 150 > 128: level 2 */
    CHECK(es2_group_state_registers(&memory,&g,G,entry)==2&&calls==2&&entries[0].ecx==0&&entries[0].edx==M0+0x92);
    arena[0x2400+0xa4]=1;part_values[1]=part_values[2]=10;calls=0; /* q = 512 >= 250: lo(512, 2) */
    CHECK(es2_group_state_registers(&memory,&g,G,entry)==2&&entries[1].ecx==1&&entries[1].edx==list);
    arena[0x2000+0x99]=1;calls=0;CHECK(es2_group_state_registers(&memory,&g,G,entry)==4);
    arena[0x2000+0x99]=0;arena[0x2400+0xa4]=0;part_values[1]=part_values[2]=60;calls=0;
    CHECK(es2_group_state_registers(&memory,&g,G,entry)==1);
    part_values[1]=part_values[2]=10;calls=0;CHECK(es2_group_state_registers(&memory,&g,G,entry)==0);
    /* target_bearing with the default target A+1A4h. */
    uint32_t T=BASE+0x2800,o=BASE+0x2c00;w32(0,A+0x1a4,T);w32(0,T,BASE+0x3300);w32(0,BASE+0x333c,0x703c);
    w16(0,A+0x10,0x3000);w16(0,T+0x10,0x500);calls=0;
    r=es2_target_bearing_registers(&memory,&g,A,o,0,entry);
    CHECK(calls==3&&args[0][0]==T+0x26&&args[0][1]==A+0x26&&entries[0].ecx==entry[2]);
    CHECK(r16(0,o)==0x1000&&r16(0,o+2)==0xe000&&r16(0,o+4)==0x2000&&r16(0,o+6)==0x8b20&&r16(0,o+8)==0x74e0);
    CHECK(r32(0,o+0xa)==5000&&r16(0,o+0xe)==0&&r32(0,o+0x10)==15000&&r32(0,o+0x14)==30000);
    CHECK(entries[1].ecx==0x2222e000u&&entries[2].ecx==T+0x26&&r==((uint64_t)0x2000<<32|0x8b20));
    /* part_position: part 1 of a record table at 3400h. */
    uint32_t V=BASE+0x3800,recs=BASE+0x3400,S=BASE+0x3600,obj=BASE+0x3680,node=BASE+0x900,p=BASE+0x3c00;
    w32(0,V+0x1fa,BASE+0x3500);w32(0,BASE+0x3500,recs);w32(0,V+0x26,1);w32(0,V+0x2a,2);w32(0,V+0x2e,3);
    w32(0,recs+18+0xe,0);calls=0;r=es2_part_position_registers(&memory,&g,V,1,p,entry);
    CHECK(!calls&&r==((uint64_t)3<<32|2)&&r32(0,p)==1&&r32(0,p+8)==3);
    w32(0,recs+18+0xe,BASE+0x3a00);w16(0,recs+18+0xc,7);w32(0,V+0x34,S);w32(0,S+4,obj);w32(0,obj,BASE+0x3700);
    w32(0,BASE+0x3720,0x7020);w32(0,S+0x16,BASE+0x4000);w16(0,node+4,2);w16(0,V+0x32,0);calls=0;
    r=es2_part_position_registers(&memory,&g,V,1,p,entry);
    CHECK(calls==4&&functions[1]==0x4801d4&&args[1][1]==BASE+0x4040&&functions[2]==0x47ebb8&&functions[3]==0x48043c);
    CHECK(entries[0].edx==S&&entries[0].ecx==obj&&r==((uint64_t)0x5678<<32|0x1234)&&r16(0,V+0x32)==1);
    w16(0,recs+18+0xc,0);calls=0;r=es2_part_position_registers(&memory,&g,V,1,p,entry);
    CHECK(calls==1&&functions[0]==0x4801d4&&args[0][1]==V+0x12&&args[0][2]==p&&entries[0].ecx==entry[2]&&entries[0].edx==BASE+0x3a00);
    /* shield_max_set, damage_score, shield_capacity, facing_part,
     * vehicle_keys on a cleared arena. */
    memset(arena,0,sizeof(arena));
    uint32_t S2=BASE+0x5000;
    CHECK(es2_shield_max_set_registers(&memory,S2,0xabcd1234u,entry)==((uint64_t)0x33331234u<<32|S2)&&r16(0,S2+6)==0x1234);
    /* damage_score: weights at img+499fe0/49a006 = BASE+2000h/2026h. */
    uint32_t img=BASE+0x2000-0x99fe0u,values=BASE+0x100,groups=BASE+0x180,flags=BASE+0x1c0,tabs=BASE+0x4c0,records=BASE+0x200,
        maxima=BASE+0x480;
    P=BASE;w32(0,P,values);w32(0,P+4,groups);w32(0,P+8,flags);w32(0,P+0xc,tabs);w32(0,tabs,records);w32(0,tabs+4,maxima);
    w16(0,flags,1);w16(0,groups,50);w16(0,records,100);w16(0,BASE+0x2000,0x400);          /* (100-50)*400h/100 = 512 */
    w16(0,flags+4,1);w16(0,groups+4,0x96);w16(0,flags+10,1);w16(0,groups+10,0xffff);      /* out of range: skipped */
    w16(0,maxima,200);w16(0,values,50);w16(0,BASE+0x2026,300);                            /* 150*300/200 = 225 */
    w16(0,maxima+2,200);w16(0,values+2,0xffff);
    w16(0,maxima+6,0xff9c);w16(0,values+6,10);w16(0,BASE+0x2026+6,7);                     /* -770/-100 = 7 rem -70 */
    CHECK(es2_damage_score_registers(&memory,img,P,entry)==((uint64_t)0xffffffbau<<32|744u));
    w16(0,flags,0);w16(0,flags+4,0);w16(0,flags+10,0);w16(0,maxima,0);w16(0,maxima+2,0);w16(0,maxima+6,0);
    CHECK(es2_damage_score_registers(&memory,img,P,entry)==(uint64_t)entry[1]<<32);
    /* shield_capacity: part 4 = 100/200 (q = 128), base 3500, slot byte 2. */
    A=BASE+0x1000;uint32_t C=BASE+0x1600,X=BASE+0x1700,D=BASE+0x1780;
    w32(0,A+0x206,values);w32(0,A+0x206+0xc,tabs);w16(0,values+8,100);w16(0,maxima+8,200);
    w32(0,A+0x1f2,C);w16(0,C+0xc0,3500);w32(0,A+0x30f,X);w32(0,X+0x18,D);arena[0x1780+0x17]=2;ratio=0x66;calls=0;
    r=es2_shield_capacity_registers(&memory,&g,A,entry);
    CHECK(calls==4&&functions[0]==0x47e0b0&&args[0][0]==3500&&args[0][1]==0x400&&entries[0].eax==0x400&&entries[0].edx==3500);
    CHECK(functions[1]==0x40dbc0&&args[1][0]==A+0x206&&args[1][1]==0x15&&entries[1].eax==A+0x206&&entries[1].edx==D&&
        entries[1].ecx==0x0c0c0000u);
    CHECK(functions[2]==0x47e0b0&&args[2][0]==616&&args[2][1]==3500&&entries[2].eax==3500&&entries[2].ecx==616); /* 400h-204*2 */
    CHECK(functions[3]==0x413ab8&&args[3][0]==A+0x222&&args[3][1]==0x0c0c15e5u&&r==((uint64_t)0x15e5<<32|(A+0x222))&&
        r16(0,A+0x228)==5605);
    w16(0,values+8,0xffff);w32(0,A+0x30f,0);calls=0;   /* q = 100h: k = 400h - 102*5 = 514; no component */
    r=es2_shield_capacity_registers(&memory,&g,A,entry);
    CHECK(calls==2&&args[0][1]==514&&entries[1].eax==0&&args[1][1]==0x0c0c06dcu&&r==((uint64_t)0x6dc<<32|(A+0x222)));
    w16(0,values+8,10);w32(0,A+0x30f,X);arena[0x1780+0x17]=0xfe;ratio=0xe1;calls=0; /* q = 12; ratio E1h: no second product */
    r=es2_shield_capacity_registers(&memory,&g,A,entry);
    CHECK(calls==2&&functions[0]==0x40dbc0&&args[0][1]==0xffff0011u&&entries[0].ecx==200&&entries[1].eax==0xfedc00e1u&&
        entries[1].edx==0xe1&&args[1][1]==0x0d0d0dacu&&r==((uint64_t)0xdac<<32|(A+0x222)));
    /* facing_part: parts 3 and 7 present; T angles (100h, 5, 200h). */
    uint32_t A2=BASE+0x5100,T2=BASE+0x5800,frame=BASE+0x5900;
    es2_weapon_graph gf={IMAGE,frame,0,0,0,call,NULL,NULL,NULL};
    w32(0,A2,BASE+0x5600);w32(0,BASE+0x5658,0x7058);w32(0,A2+0x20e,BASE+0x5700);w16(0,BASE+0x5706,1);w16(0,BASE+0x570e,1);
    w16(0,T2,0x100);w16(0,T2+2,5);w16(0,T2+4,0x200);
    aim_words[0][0]=0x150;aim_words[0][1]=9;aim_words[0][2]=0x150;aim_words[1][0]=0x8100;aim_words[1][1]=9;aim_words[1][2]=0x200;
    aim_calls=0;calls=0;r=es2_facing_part_registers(&memory,&gf,A2,T2,entry);
    CHECK(calls==4&&functions[0]==0x7058&&args[0][0]==A2&&args[0][1]==3&&args[0][2]==frame&&entries[0].eax==frame&&
        entries[0].edx==A2&&entries[0].ecx==BASE+0x5600);
    CHECK(functions[1]==0x492990&&args[1][0]==frame+0xc&&args[1][1]==frame&&args[1][2]==T2+0x1a&&entries[1].eax==T2+0x1a);
    CHECK(args[2][1]==7&&r==3&&r16(0,frame+0xe)==4&&r16(0,frame+0xc)==0x8000);
    w16(0,BASE+0x5706,0);w16(0,BASE+0x570e,0);calls=0;
    CHECK(es2_facing_part_registers(&memory,&gf,A2,T2,entry)==((uint64_t)entry[1]<<32|(A2&0xffff0000u)|0xffffu)&&!calls);
    /* vehicle_keys with an image whose globals fall into the arena. */
    uint32_t img2=BASE+0x8000-0x9b088u,Vk=BASE+0x6000,W=BASE+0x6400,H=BASE+0x6800,O=BASE+0x7000,fk=BASE+0x7400,esi=0;
    es2_weapon_graph gk={img2,fk,0,0,0,call,NULL,NULL,NULL};mock_image=img2;bridge_esi=&esi;
    w32(0,img2+0xa9b84u,Vk);w32(0,Vk+0x202,W);w16(0,img2+0xa9cccu,0x100);w16(0,img2+0xa9cceu,0x40);
    w32(0,img2+0x9b088u,H);w32(0,H+0x1e9,BASE+0x6900);w16(0,img2+0x9941cu+0xc0a,1500);w16(0,img2+0x9942eu+0xc0a,0x80);
    keys_eax=0x12340002u;calls=0;r=es2_vehicle_keys_registers(&memory,&gk,O,0x20211u,entry,&esi);
    CHECK(calls==1&&functions[0]==0x410f78&&args[0][0]==W&&args[0][1]==0x20211u&&entries[0].eax==Vk&&entries[0].edx==W&&
        entries[0].ecx==entry[2]&&esi_seen[0]==entry[0]&&r==((uint64_t)0x41410000u<<32|0x12340001u));
    keys_eax=0x77770000u;calls=0;r=es2_vehicle_keys_registers(&memory,&gk,O,0x1a,entry,&esi);
    CHECK(calls==2&&functions[1]==0x438d9c&&args[1][0]==BASE+0x6900&&args[1][1]==1&&entries[1].eax==H&&entries[1].ecx==1&&
        esi_seen[1]==0x11110001u&&r==((uint64_t)0x438d9c<<32|0x11110001u));
    calls=0;es2_vehicle_keys_registers(&memory,&gk,O,0xffff001bu,entry,&esi);CHECK(args[1][1]==0&&entries[1].ecx==0);
    w16(0,Vk+0x290,0xffff);calls=0;r=es2_vehicle_keys_registers(&memory,&gk,O,0x4c,entry,&esi);
    CHECK(calls==1&&r16(0,Vk+0x290)==0&&arena[0x6093]==1&&r==((uint64_t)0x41410000u<<32|0x11110001u));
    w32(0,img2+0x9ef38u,1);write_out=1;calls=0;r=es2_vehicle_keys_registers(&memory,&gk,O,0x24d,entry,&esi);
    CHECK(calls==3&&functions[1]==0x47ebb8&&args[1][0]==O+0xc&&args[1][1]==O+0x12&&entries[1].eax==O+0x12&&
        entries[1].ecx==0x42420000u&&functions[2]==0x4801d4&&args[2][0]==fk+0x24&&args[2][1]==O+0x12&&args[2][2]==fk);
    CHECK(entries[2].edx==(((O+0xc)&0xffff0000u)|0x100u)&&entries[2].eax==fk+0x24&&entries[2].ecx==fk&&r16(0,fk+0x24)==0x100&&r16(0,fk+0x26)==0);
    CHECK(r32(0,O+0x26)==10&&r32(0,O+0x2a)==20&&r32(0,O+0x2e)==30&&r16(0,O+0x32)==1&&r==((uint64_t)20<<32|0x11110001u));
    calls=0;es2_vehicle_keys_registers(&memory,&gk,O,0x250,entry,&esi);
    CHECK(calls==2&&args[1][0]==fk+0xc&&r16(0,fk+0xe)==0xff00&&entries[1].edx==0x4141ff00u);
    w16(0,O+0x10,0x1000);calls=0;r=es2_vehicle_keys_registers(&memory,&gk,O,0x44b,entry,&esi);
    CHECK(calls==1&&r16(0,O+0x10)==0x1040&&r16(0,O+0x32)==0&&r==((uint64_t)0x41411040u<<32|0x11110001u));
    es2_vehicle_keys_registers(&memory,&gk,O,0x44d,entry,&esi);CHECK(r16(0,O+0x10)==0x1000);
    r=es2_vehicle_keys_registers(&memory,&gk,O,0x605,entry,&esi);
    CHECK(r16(0,img2+0xa9cccu)==1500&&r16(0,img2+0xa9cceu)==0x80&&r==((uint64_t)(0x41410000u|1500u)<<32|0x11110001u));
    CHECK(es2_vehicle_keys_registers(&memory,&gk,O,0x601,entry,&esi)==(uint64_t)0x41410000u<<32);
    w32(0,img2+0x9ef38u,0);calls=0;r=es2_vehicle_keys_registers(&memory,&gk,O,0x248,entry,&esi);
    CHECK(calls==1&&r==((uint64_t)0x41410000u<<32|0x11110001u));
    /* Helpers on the tables of tables(): record 2 lists parts 0, 2
     * and 11 (values 100, -5, 7; maxima 200 each). */
    {   uint32_t values=BASE+0x100,records=BASE+0x200,list=BASE+0x700,rec2=records+36,img=BASE+0x8000u-0xa9e00u;
        es2_weapon_graph gh={IMAGE,BASE+0x7000,0,0,0,call,NULL,NULL,NULL};mock_image=IMAGE;bridge_esi=NULL;
        tables(P);w16(0,rec2+6,3);w32(0,rec2+8,list);w16(0,list+2,0);w16(0,list+6,2);w16(0,list+10,11);
        CHECK(es2_part_group_max_registers(&memory,P,0xffff0002u)==((uint64_t)3<<32|700u));
        CHECK(es2_part_list_dead_registers(&memory,rec2,values)==((uint64_t)0<<32|0u));
        w32(0,rec2+8,list+4);w16(0,rec2+6,1);
        CHECK(es2_part_list_dead_registers(&memory,rec2,values)==((uint64_t)1<<32|((list+8)&0xffff0000u)|1u));
        w16(0,rec2+6,0x8000);
        CHECK(es2_part_list_dead_registers(&memory,rec2,values)==(((list+4)&0xffff0000u)|1u));
        calls=0;CHECK(es2_part_group_dead_registers(&memory,&gh,P,0x12340000u)==(uint64_t)0x12340000u<<32&&calls==0);
        CHECK(es2_part_group_dead_registers(&memory,&gh,P,0xabcd0001u)==((uint64_t)0x4242u<<32|0xabcd0001u));
        CHECK(calls==1&&functions[0]==0x40cf10&&args[0][0]==records+18&&args[0][1]==values);
        CHECK(entries[0].eax==18&&entries[0].edx==records+18&&entries[0].ecx==1);
        w16(0,img+0xa9e52u,0);w16(0,img+0xa9e5au,1);w16(0,img+0xa9e5cu,4);
        CHECK(es2_mission_guard_registers(&memory,img,entry)==((uint64_t)entry[1]<<32|1u));
        w16(0,img+0xa9e5au,0x101);CHECK(es2_mission_guard_registers(&memory,img,entry)==(uint64_t)entry[1]<<32);
        w16(0,img+0xa9e52u,0x100);CHECK(es2_mission_guard_registers(&memory,img,entry)==(uint64_t)entry[1]<<32);
        w16(0,img+0xa9e5cu,0);CHECK(es2_mission_guard_registers(&memory,img,entry)==((uint64_t)entry[1]<<32|1u));
        const uint32_t first=BASE+0x900,second=BASE+0x910;for(unsigned i=0;i<32;++i)arena[0x900+i]=0xee;
        calls=0;CHECK(es2_actor_offsets_registers(&memory,&gh,first,second,entry)==((uint64_t)6<<32|second));
        CHECK(calls==2&&args[0][0]==first&&args[0][2]==6&&args[1][0]==second&&args[1][2]==6);
        CHECK(entries[0].eax==first&&entries[0].edx==entry[1]&&entries[0].ecx==entry[2]);
        CHECK(entries[1].eax==first&&entries[1].edx==second&&entries[1].ecx==0x1234u);
        CHECK(r32(0,first)==0&&r16(0,first+4)==0&&arena[0x906]==0xee&&r32(0,second)==0&&r16(0,second+4)==0&&arena[0x916]==0xee);
        puts("PASS: part_list_dead, part_group_dead, part_group_max, mission_guard and actor_offsets on a synthetic arena");
    }
    puts("PASS: shield_max_set, damage_score, shield_capacity, facing_part and vehicle_keys on a synthetic arena");
    puts("PASS: condition_table, condition_ratio, sight_check, group_state, target_bearing and part_position on a synthetic arena");
    printf("%u checks\n",checks);return 0;
}
