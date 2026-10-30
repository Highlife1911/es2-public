#include "es2/core/damage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Guest addresses deliberately differ from the host, including on x64. */
enum { BASE=0x12340000, A=BASE+0x101, C=BASE+0x500, C2=BASE+0x550,
       FLAGS=BASE+0x600, FLAGS2=BASE+0x640, STATE=BASE+0x680,
       V=BASE+0x700, V2=BASE+0x780, P=BASE+0x880, OUT=BASE+0x900 };
static unsigned checks,calls,observed,ids[128];static unsigned char arena[0xa00];
#define CHECK(x) do { ++checks;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} }while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a>=BASE&&a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){unsigned char *p=host(c,a);return (uint16_t)(p[0]|(p[1]<<8));}
static uint32_t r32(void *c,uint32_t a){return (uint32_t)r16(c,a)|((uint32_t)r16(c,a+2)<<16);}
static void w16(void *c,uint32_t a,uint16_t x){unsigned char *p=host(c,a);p[0]=(unsigned char)x;p[1]=(unsigned char)(x>>8);}
static void w32(void *c,uint32_t a,uint32_t x){w16(c,a,(uint16_t)x);w16(c,a+2,(uint16_t)(x>>16));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
static uint32_t heading,front,remaining,random_value,distance_value,expected_damage;
static int change_graph,building;
static uint64_t callback(void *c,uint32_t f,const uint32_t *x,unsigned n) {
    (void)c;CHECK(calls<128);ids[calls++]=f;
    switch(f) {
    case 1:CHECK(n==2&&x[0]==P&&x[1]==A+0x26);return UINT64_C(0xabcdef0123450000)|heading;
    case 2:CHECK(n==3&&x[0]==A+0x222&&x[1]==front&&x[2]==0x23458000u);return UINT64_C(0x9876543200000000)|remaining;
    case 3:CHECK(n==1&&x[0]==99);return UINT64_C(0x1234567800000000)|random_value;
    case 4:CHECK(n==2&&x[0]==OUT&&x[1]==P);CHECK(r32(0,OUT)==101&&r32(0,OUT+8)==303);return UINT64_C(0x1234abcd00000000)|distance_value;
    case 5:
        CHECK(n==3&&x[0]==A&&x[1]==0&&x[2]==OUT);
        w32(0,OUT,101);w32(0,OUT+4,202);w32(0,OUT+8,303);
        if(change_graph){w32(0,A,V2);w32(0,A+0x201,FLAGS2);w32(0,A+0x20e,FLAGS2);}
        return UINT64_MAX;
    case 6: case 7:
        CHECK(n==4&&x[0]==A&&x[1]==0&&x[2]==expected_damage&&x[3]==0x87654321u);
        if(change_graph){CHECK(f==7);w32(0,A+0x1f2,C2);}
        return UINT64_C(0xdeadbeefcafebabe);
    default:CHECK(0);return 0;
    }
}
static void observer(void *c,es2_damage_event event,uint32_t key) {
    (void)c;CHECK(observed==calls);++observed;
    CHECK((event==ES2_DAMAGE_CALL&&key>=1&&key<=4)||(event==ES2_DAMAGE_SLOT&&(key==0x58||key==0x74)));
}
static es2_damage_context g={99,1,2,3,4,OUT,callback,0,observer,0};
static void reset(void) {
    memset(arena,0,sizeof(arena));calls=observed=0;heading=0;front=1;remaining=1000;random_value=0;distance_value=200;expected_damage=833;change_graph=building=0;
    w32(0,A,V);w32(0,A+0x1f2,C);w32(0,A+0x201,FLAGS);w32(0,A+0x20e,FLAGS);w32(0,A+0x205,STATE);
    w32(0,V+0x58,5);w32(0,V+0x74,6);w32(0,V2+0x58,5);w32(0,V2+0x74,7);
    w16(0,C+4,0xffff);w16(0,C+0x12,2);w32(0,C+0x38,1);w16(0,C2+0x12,0);
    w16(0,FLAGS,1);w16(0,FLAGS2+2,1);
}
static uint64_t run(uint32_t range) {
    uint64_t r=building?es2_building_hit_registers(&memory,&g,A,0x12348000u,P,range,0x87654321u,0x89abcdefu):
        es2_vehicle_hit_registers(&memory,&g,A,0x12348000u,P,range,0x87654321u);
    CHECK(calls==observed);return r;
}
static uint32_t part_status;
static uint64_t part_callback(void *c,uint32_t f,const uint32_t *a,unsigned n) {
    (void)c;ids[calls++]=f;
    if(f==17){CHECK(n==1&&a[0]==A);CHECK(r16(0,STATE)==100&&r16(0,FLAGS)==0);return UINT64_C(0x12345678abcd0000)|part_status;}
    if(f==14){CHECK(n==1&&a[0]==0x2e);return 99;}
    if(f==18){CHECK(n==3&&a[0]==BASE+0x50&&a[1]==A&&a[2]==0);return 999;}
    if(f==15){CHECK(n==1&&a[0]==A);CHECK(*(unsigned char *)host(0,A+0x99)==1&&*(unsigned char *)host(0,A+0x96)==0);w32(0,A+0x1b6,P+8);return UINT64_C(0x76543210abcdef12);}
    if(f==16){CHECK(n==1&&a[0]==P+8);return UINT64_C(0xaabbccdd1234abcd);}
    CHECK(0);return 0;
}
static uint64_t part_register_callback(void *c,uint32_t f,const uint32_t *a,unsigned n,const uint32_t r[3]) {
    CHECK(f==16&&r[0]==P+8&&r[1]==0x76543210u&&r[2]==A);return part_callback(c,f,a,n);
}
static void portable_part(void) {
    es2_building_part_context pg={P,OUT,11,12,13,99,14,15,16,part_callback,0,part_register_callback};
    reset();w32(0,C+0x14,C2);w16(0,C2,100);w16(0,C2+4,0xffff);w32(0,V+0x40,17);
    w16(0,C+0x1e,1);CHECK(es2_building_part_registers(&memory,&pg,A,0,10,0,0xabcdef01)==(((uint64_t)C<<32)|0xabcdef01u));
    w16(0,C+0x1e,0);CHECK(es2_building_part_registers(&memory,&pg,A,0,10,0,0)==10);CHECK(r16(0,STATE)==10&&calls==0);
    part_status=0x101;CHECK(es2_building_part_registers(&memory,&pg,A,0xffff,100,0,0)==UINT64_C(0x12345678abcdffff));CHECK(calls==1);
    w16(0,FLAGS,1);w16(0,C2+4,1);w16(0,OUT+2,777);part_status=0x100;
    w32(0,P,BASE+0x50);w32(0,BASE+0x50,V2);w32(0,BASE+0x50+0x1a4,A);w32(0,V2+0x60,18);calls=0;
    CHECK(es2_building_part_registers(&memory,&pg,A,0,100,BASE+0x50,0)==UINT64_C(0x0000000112340001));
    CHECK(calls==5&&ids[0]==17&&ids[1]==14&&ids[2]==18&&ids[3]==15&&ids[4]==16);
    CHECK(r16(0,STATE+3)==300&&r16(0,STATE+5)==777&&r32(0,STATE+7)==BASE+0x50);
}
int main(void) {
    CHECK(es2_distance3_registers(1,2,3)==3);
    CHECK(es2_distance3_registers(0,0,0x80000000u)==UINT64_C(0xffffffffe0000000));
    CHECK(es2_distance3_registers(0x80000000u,0,0)==UINT64_C(0x00000000e0000000));
    unsigned char s[8]={0xa5,0,0,0,0,0x5a,0,0};
    s[1]=100;CHECK(es2_shield_damage_registers(s+1,1,100)==0 && s[1]==3 && s[3]==0);
    CHECK(es2_shield_damage_registers(s+1,0x12340000u,100)==UINT64_C(0x0000014800000063));
    CHECK(s[0]==0xa5&&s[5]==0x5a&&s[1]==3&&s[3]==0);
    reset();CHECK(run(1200)==UINT64_C(0xdeadbeefcafebabe));CHECK(calls==6);
    static const uint16_t angles[]={0x3fff,0x4000,0x4001,0x7fff,0x8000,0x8001,0xbfff,0xc000,0xc001,0xffff};
    for(unsigned i=0;i<sizeof(angles)/sizeof(angles[0]);++i){reset();heading=angles[i];front=i==0||i>=8;remaining=0;CHECK(run(1200)==UINT64_C(0x9876543200000000));CHECK(calls==2);}
    reset();random_value=0x802;CHECK(run(1200)==0x802 && calls==3);
    reset();random_value=0x801;CHECK(run(1200)==UINT64_C(0xdeadbeefcafebabe));
    reset();distance_value=1200;CHECK(run(1200)==UINT64_C(0x1234abcd000004b0)&&calls==5);
    reset();distance_value=0x80000000u;remaining=32767;expected_damage=(uint32_t)((INT64_C(-2147483648)+1200*32767)/1200);CHECK(run(1200)==UINT64_C(0xdeadbeefcafebabe));
    reset();change_graph=1;CHECK(run(1200)==UINT64_C(0xdeadbeefcafebabe)&&calls==6);
    reset();building=1;w16(0,C+0x1e,1);CHECK(run(1200)==(((uint64_t)0x89abcdefu<<32)|C)&&calls==0);
    reset();building=1;w16(0,C+4,0);*(unsigned char *)host(0,A+0x99)=1;CHECK(run(1200)==(((uint64_t)C<<32)|(A+0x92))&&calls==0);
    reset();building=1;w32(0,C+0x38,0);CHECK(run(1200)==(((uint64_t)C<<32)|C)&&calls==0);
    reset();building=1;expected_damage=(uint32_t)(INT64_C(-32768000)/1200);random_value=0xfff;
    CHECK(run(1200)==(((uint64_t)2<<32)|C));CHECK(calls==4&&ids[0]==3);
    reset();building=change_graph=1;expected_damage=(uint32_t)(INT64_C(-32768000)/1200);
    CHECK(run(1200)==C2&&calls==4);
    portable_part();
    printf("PASS: damage portable guest memory, strict boundaries, return pairs and callback ordering (%u checks)\n",checks);return 0;
}
