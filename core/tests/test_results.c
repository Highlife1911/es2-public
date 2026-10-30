#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/results.h"
/* Synthetic state for the core (result_conditions 423d68). A team
 * with its member array, two actors, a stream object with two tables whose
 * slot +14h is a logging model of the write, and a model of
 * condition_record 41b4ec that fills the 84-byte record. All values are
 * synthetic. */
#define BASE UINT32_C(0x90000000)
#define IMAGE UINT32_C(0x00400000)
#define TEAM (BASE+0x000u)
#define LIST (BASE+0x040u)
#define ACTOR0 (BASE+0x100u)
#define ACTOR1 (BASE+0x400u)
#define STREAM (BASE+0x700u)
#define TABLE (BASE+0x740u)
#define TABLE2 (BASE+0x780u)
#define WRITE UINT32_C(0x0047b2b8)
#define SCRATCH (BASE+0x800u)
static uint8_t arena[0x900];static unsigned checks,ncalls,record_calls,lower_at,swap_at;
typedef struct call_log { uint32_t f,n,args[3],regs[3];uint8_t data[6]; } call_log;
static call_log log_[128];
static int16_t condition[2][41];
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"results line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(ncalls<128);call_log *v=log_+ncalls++;memset(v,0,sizeof(*v));v->f=f;v->n=n;
    for(unsigned j=0;j<n;++j)v->args[j]=a[j];
    v->regs[0]=r->eax;v->regs[1]=r->edx;v->regs[2]=r->ecx;
    if(f==IMAGE+0x41b4ecu-0x400000u) {
        /* condition_record: word +0, then the 41 condition words. */
        CHECK(n==2);const unsigned k=a[0]==ACTOR1;++record_calls;
        w16(0,a[1],0x77);for(unsigned i=0;i<41;++i)w16(0,a[1]+2u+2u*i,(uint16_t)condition[k][i]);
        r->eax=0xa1u;r->edx=0xd1u;r->ecx=0xc1u;return;
    }
    CHECK(f==WRITE&&n==3&&a[0]==STREAM&&(a[1]==2||a[1]==6));
    memcpy(v->data,host(0,a[2]),a[1]);
    if(ncalls==lower_at)w16(0,TEAM+0x10,1);
    if(ncalls==swap_at)w32(0,STREAM,TABLE2);
    r->eax=0xa0000000u+ncalls;r->edx=0xd0000000u+ncalls;r->ecx=0xc0000000u+ncalls;
}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
/* ((100h - c) * 100) >> 8 with an arithmetic shift (floor), low word. */
static uint16_t expect(int16_t c) { int32_t v=(0x100-(int32_t)c)*100;return (uint16_t)(v<0?-((-v+255)/256):v/256); }
static uint16_t data16(const call_log *c) { return (uint16_t)(c->data[0]|c->data[1]<<8); }
static void run(uint16_t count) { ncalls=record_calls=0;w16(0,TEAM+0x10,count);w32(0,STREAM,TABLE); }
int main(void){
    es2_weapon_graph g={IMAGE,SCRATCH,0,0,0,call,0,0,0};
    w32(0,TEAM+0x0c,LIST);w32(0,LIST,ACTOR1);w32(0,LIST+4,ACTOR0);
    w32(0,TABLE+0x14,WRITE);w32(0,TABLE2+0x14,WRITE);
    for(unsigned i=0;i<6;++i){((uint8_t *)host(0,ACTOR0+0x2a4u))[i]=(uint8_t)(0x10+i);((uint8_t *)host(0,ACTOR1+0x2a4u))[i]=(uint8_t)(0x20+i);}
    for(unsigned i=0;i<41;++i){condition[0][i]=(int16_t)(i*7);condition[1][i]=(int16_t)(0x100-(int)i*13);}
    condition[1][0]=-0x8000;condition[1][1]=0x7fff;condition[1][2]=0x100;condition[1][3]=0;condition[1][4]=-1;
    /* No member (count 0 and a negative count): no call, EAX keeps its
     * high word, EDX is the entry EDX. */
    run(0);uint64_t v=es2_result_conditions_registers(&memory,&g,TEAM,STREAM,entry);
    CHECK(ncalls==0&&(uint32_t)v==0x11110000u&&(uint32_t)(v>>32)==0x33334444u);
    run(0xfffe);v=es2_result_conditions_registers(&memory,&g,TEAM,STREAM,entry);
    CHECK(ncalls==0&&(uint32_t)v==0x1111fffeu);
    /* Two members: 2 x (1 + 33 + 1) calls. */
    run(2);v=es2_result_conditions_registers(&memory,&g,TEAM,STREAM,entry);
    CHECK(ncalls==70&&record_calls==2);
    const uint32_t frame=SCRATCH+0x80u,out=frame-0x64u;
    /* Spans: record words 0..12 (+2), 19..28 (+28h), 31..40 (+40h). */
    static const unsigned first[3]={0,19,31},count[3]={13,10,10},local[3]={0xa,0xc,0xe};
    for(unsigned m=0;m<2;++m) {
        const call_log *c=log_+35*m;const unsigned k=m==0;const uint32_t actor=k?ACTOR1:ACTOR0;
        CHECK(c->f==0x41b4ecu&&c->args[0]==actor&&c->args[1]==out);
        CHECK(c->regs[0]==actor&&c->regs[1]==out&&c->regs[2]==actor);
        ++c;
        for(unsigned s=0;s<3;++s)for(unsigned j=0;j<count[s];++j,++c) {
            const uint16_t want=expect(condition[k][first[s]+j]);
            CHECK(c->f==WRITE&&c->args[1]==2&&c->args[2]==frame-local[s]);
            CHECK(data16(c)==want);
            CHECK(c->regs[0]==frame-local[s]&&c->regs[1]==TABLE&&c->regs[2]==(uint32_t)(int32_t)(int16_t)want);
            /* The transformed word also replaces the record word. */
            if(m==1)CHECK(r16(0,out+2u+2u*(first[s]+j))==want);
        }
        /* 6 bytes at actor+2A4h; ECX is what the previous write returned. */
        CHECK(c->f==WRITE&&c->args[1]==6&&c->args[2]==actor+0x2a4u&&c->regs[0]==actor+0x2a4u&&c->regs[1]==TABLE);
        CHECK(c->regs[2]==0xc0000000u+(unsigned)(c-log_));
        CHECK(memcmp(c->data,host(0,actor+0x2a4u),6)==0);
    }
    /* Untouched record words (13..18, 29, 30) keep condition_record's values. */
    CHECK(r16(0,out)==0x77&&(int16_t)r16(0,out+2u+2u*13u)==condition[0][13]&&(int16_t)r16(0,out+2u+2u*30u)==condition[0][30]);
    /* Spot values: c = -8000h gives 12900, 7FFFh gives -12700 (floor), 100h
     * gives 0, 0 gives 100, -1 gives 100. */
    CHECK(data16(log_+1)==12900u&&data16(log_+2)==(uint16_t)-12700&&data16(log_+3)==0&&data16(log_+4)==100&&data16(log_+5)==100);
    CHECK((uint32_t)v==((0xa0000000u+70u)&0xffff0000u)+2u&&(uint32_t)(v>>32)==0xd0000000u+70u);
    /* The count is reread at every test: a write that lowers it ends the
     * loop after the current member. The table is reread before every
     * write. */
    lower_at=10;swap_at=20;run(2);v=es2_result_conditions_registers(&memory,&g,TEAM,STREAM,entry);
    CHECK(ncalls==35&&log_[19].regs[1]==TABLE&&log_[20].regs[1]==TABLE2&&log_[34].regs[1]==TABLE2);
    CHECK((uint32_t)v==((0xa0000000u+35u)&0xffff0000u)+1u);
    printf("results: %u checks passed\n",checks);
    return 0;
}
