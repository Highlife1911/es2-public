#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/logic_frame.h"
/* Synthetic guest: the image at B (globals relocated, a 2.9 MB buffer), the
 * objects at OBJ, the logic frame's stack page at STACK. Calls are logged
 * with their target, slot, arguments, entry registers and the EBX/ESI/EDI
 * and return address the core names; they return planned registers. The
 * pool iterator walks a per-pool object list, the updates request a
 * removal for every second object. */
#define B UINT32_C(0x10000000)
#define G(va) (B+(uint32_t)(va)-0x400000u)
#define OBJ UINT32_C(0x90000000)
#define STACK UINT32_C(0xa0000000)
#define EBPV (STACK+0x180u)
static uint8_t image[0x2d8000],arena[0x4000],stack[0x200];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"logic_frame line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-B<sizeof(image))return image+(a-B);
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-STACK<sizeof(stack))return stack+(a-STACK);
    fprintf(stderr,"logic_frame: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
typedef struct logged { uint32_t f,a[4],r[3],fr[4]; unsigned n,slot; } logged;
#define MAXLOG 96
static logged log_[MAXLOG];static unsigned calls,observed;
static es2_logic_frame *active;
static uint32_t player_eax,death_eax,status_eax,modal_eax,slot_after,assess_kill;
/* Pools: header addresses and their object lists (oldest first). */
#define POOL(k) (OBJ+0x100u+0x10u*(k))
#define OBJECT(k,i) (OBJ+0x400u+0x100u*(k)+0x40u*(i))
static unsigned pool_count[9];
static const uint32_t pool_global[9]={0x4a961e,0x4a966e,0x4a9684,0x6b4f30,0x4a9642,0x4a96aa,0x4a96c2,0x4a95a0,0x4a9b7a};
static uint32_t walk(uint32_t pool,uint32_t cursor){
    for(unsigned k=0;k<9;++k)if(pool==POOL(k)){
        if(!cursor)return pool_count[k]?OBJECT(k,0):0;
        for(unsigned i=0;i+1<pool_count[k];++i)if(cursor==OBJECT(k,i))return OBJECT(k,i+1);
        return 0;
    }
    return 0;
}
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<MAXLOG);logged *l=log_+calls;l->f=f;l->n=n;if(n)memcpy(l->a,a,4*n);
    l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    l->fr[0]=active->call.ebx;l->fr[1]=active->call.esi;l->fr[2]=active->call.edi;l->fr[3]=active->call.ret;
    if(active->top&&n==1)w32(0,active->top-4,slot_after+calls);
    const uint32_t eax_in=r->eax;
    r->eax=0x1000u+calls;r->edx=0x2000u+calls;r->ecx=0x3000u+calls;
    if(f==G(0x49630c)){w32(0,EBPV-0x30,0xfeedf00du);r->eax=0x0badbeefu;r->edx=l->r[1];r->ecx=l->r[2];}
    else if(f==G(0x45a810))r->eax=OBJ+0x3000u;              /* R */
    else if(f==G(0x471c70))r->eax=walk(a[0],a[1]);
    else if(f==G(0x460870))r->eax=player_eax;
    else if(f==G(0x45fa84))r->eax=death_eax;
    else if(f==G(0x4131ac))r->eax=status_eax;
    else if(f==G(0x47749c))r->eax=OBJ+0x3800u;              /* the dialog */
    else if(f==0xc0000008u)r->eax=modal_eax;                /* dialog +8 */
    else if(f==G(0x423f08)&&assess_kill)arena[0x2000+0x99]=1;   /* group_assess kills the player */
    else if(f==G(0x40813c)||f==G(0x408bd8)||f==0xc0000014u)r->eax=(eax_in&0x40u)?0x10000u:0x10001u;  /* every second: removal */
    ++calls;
}
static void observe(void *c,uint32_t f,unsigned slot){(void)c;(void)f;CHECK(observed==calls);log_[observed++].slot=slot;}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
static void reset(void){calls=observed=0;memset(log_,0,sizeof(log_));}
static void build(void){
    memset(image,0,sizeof(image));memset(arena,0,sizeof(arena));memset(stack,0x5a,sizeof(stack));
    memset(pool_count,0,sizeof(pool_count));
    for(unsigned k=0;k<9;++k)w32(0,G(pool_global[k]),POOL(k));
    w32(0,G(0x4d24e6),OBJ+0x2000u);                          /* the player */
    w32(0,OBJ+0x2000u+0x45,OBJ+0x2400u);                     /* its group */
    w32(0,G(0x6cbee4),OBJ+0x3f00u);w32(0,OBJ+0x3f00u,100);   /* the counter */
    w32(0,OBJ+0x3800u,OBJ+0x3900u);w32(0,OBJ+0x3908u,0xc0000008u);   /* dialog vtable +8 */
    w16(0,OBJ+0x3000u+0x2e,0x1234);                          /* R+2Eh */
    w16(0,G(0x49f5c0)+2*5,7);                                /* expected button of status 5 */
    player_eax=0x77770000u;death_eax=0;status_eax=0xffffu;modal_eax=0;slot_after=0x5a000000u;
}
static es2_weapon_graph graph(void){es2_weapon_graph g={B,EBPV-ES2_LOGIC_FRAME_SIZE,0,0,0,call,0,observe,0};return g;}
static uint32_t ret_of(unsigned j){return log_[j].fr[3];}
int main(void){
    es2_weapon_graph g=graph();uint64_t p;uint32_t ecx;es2_logic_frame fr;
    /* Frozen frame, tape playback: record, time scale, input, view, outcome -1. */
    build();w16(0,G(0x4d24f2),1);w16(0,G(0x4d24d6),1);reset();
    memset(&fr,0,sizeof(fr));active=&fr;
    p=es2_logic_frame_registers(&memory,&g,&fr,0xb0b0b0b0u,0x5151u,0xd1d1u,entry,&ecx);
    CHECK(calls==5);
    CHECK(log_[0].f==G(0x49630c)&&log_[0].n==0&&log_[0].r[0]==G(0x49f558)&&log_[0].r[1]==entry[1]&&log_[0].r[2]==entry[2]);
    CHECK(log_[0].fr[0]==0xb0b0b0b0u&&log_[0].fr[1]==0x5151u&&log_[0].fr[2]==0xd1d1u&&ret_of(0)==G(0x45f583));
    CHECK(log_[1].f==G(0x45a810)&&log_[1].r[0]==0x0badbeefu&&log_[1].r[1]==entry[1]&&ret_of(1)==G(0x45f588));
    CHECK(r16(0,G(0x4d3b64))==0x1234&&r32(0,EBPV-0x34)==OBJ+0x3000u&&r16(0,EBPV-0x36)==0x1234);
    CHECK(log_[2].f==G(0x460870)&&log_[2].n==1&&log_[2].a[0]==OBJ+0x3000u&&log_[2].r[1]==OBJ+0x3000u);
    CHECK(log_[2].r[0]==((OBJ+0x3000u)&0xffff0000u)+0x1234u&&log_[2].r[2]==((0x3001u&0xffff0000u)|0x1234u));
    CHECK(log_[2].fr[0]==0xb0b0b0b0u&&log_[2].fr[1]==0&&log_[2].fr[2]==0xd1d1u&&ret_of(2)==G(0x45f83d));
    CHECK(log_[3].f==G(0x45de30)&&log_[3].r[0]==player_eax&&log_[3].r[2]==OBJ+0x3000u&&ret_of(3)==G(0x45f877));
    CHECK(log_[4].f==G(0x4131ac)&&log_[4].a[0]==OBJ+0x2000u&&log_[4].r[0]==OBJ+0x2000u&&log_[4].r[1]==OBJ+0x2092u&&log_[4].r[2]==OBJ+0x2092u);
    CHECK(r32(0,EBPV-0x68)==OBJ+0x2092u);
    CHECK(p==((uint64_t)0xfeedf00du<<32)&&ecx==OBJ+0x2000u);
    /* Running frame: pools, removals, buildings, groups, timers, MECH loop,
     * outcome status 5 with the alert (the button matches). */
    build();reset();
    pool_count[0]=2;pool_count[4]=1;pool_count[6]=2;pool_count[7]=2;pool_count[8]=2;
    for(unsigned i=0;i<2;++i){
        w32(0,OBJECT(6,i),OBJ+0x3a00u);w32(0,OBJ+0x3a14u,0xc0000014u);   /* projectile vtable */
        w32(0,OBJECT(7,i),OBJ+0x3b00u);w32(0,OBJ+0x3b18u,0xc0000018u);   /* building vtable */
        w32(0,OBJECT(7,i)+0x45,OBJ+0x2500u+0x20u*i);w32(0,OBJ+0x2500u+0x20u*i+0x14,i);   /* the second pending */
        w32(0,OBJECT(8,i)+0x45,OBJ+0x2600u);arena[OBJECT(8,i)-OBJ+0x99]=(uint8_t)i;      /* the second skipped */
    }
    w32(0,G(0x4a9e00),OBJ+0x2800u);w16(0,G(0x4a9e04),2);w32(0,OBJ+0x2800u+0x14,1);  /* group 0 pending */
    w32(0,G(0x4a9622),0x1111u);w32(0,G(0x4a9646),0x4444u);w32(0,G(0x4a96c6),0x6666u);
    w16(0,G(0x4d2526),0xffff);w16(0,G(0x4d24fc),1);
    status_eax=0x00050005u;modal_eax=7;
    p=es2_logic_frame_registers(&memory,&g,&fr,1,2,3,entry,&ecx);
    unsigned j=2;
    /* explosions: walk(0) -> obj0, update (AX 1: append), walk -> obj1, update (AX 0), walk -> 0 */
    CHECK(log_[j].f==G(0x471c70)&&log_[j].a[0]==POOL(0)&&log_[j].a[1]==0&&log_[j].r[0]==0&&log_[j].r[2]==POOL(0)&&log_[j].fr[0]==1&&log_[j].fr[1]==0&&log_[j].fr[2]==0);
    CHECK(log_[j+1].f==G(0x40813c)&&log_[j+1].a[0]==OBJECT(0,0)&&log_[j+1].r[0]==OBJECT(0,0)&&log_[j+1].r[1]==0x2000u+j&&log_[j+1].fr[0]==OBJECT(0,0)&&ret_of(j+1)==G(0x45f5c7));
    CHECK(log_[j+2].f==G(0x478628)&&log_[j+2].a[0]==0x1111u&&log_[j+2].a[1]==OBJECT(0,0)&&log_[j+2].r[0]==OBJECT(0,0)&&log_[j+2].r[1]==0x1111u&&log_[j+2].r[2]==OBJECT(0,0));
    CHECK(log_[j+3].f==G(0x471c70)&&log_[j+3].a[1]==OBJECT(0,0)&&log_[j+3].fr[2]==OBJECT(0,0)&&ret_of(j+3)==G(0x45f5ef));
    CHECK(log_[j+4].f==G(0x40813c)&&log_[j+4].a[0]==OBJECT(0,1));
    CHECK(log_[j+5].f==G(0x471c70)&&log_[j+5].a[1]==OBJECT(0,1));
    j+=6;
    /* smoke balls, smoke, fires: empty pools (EDX = 0 at each first call). */
    for(unsigned k=1;k<4;++k){CHECK(log_[j].f==G(0x471c70)&&log_[j].a[0]==POOL(k)&&log_[j].a[1]==0&&log_[j].r[1]==0&&log_[j].r[0]==0);++j;}
    /* debris: one object, removal requested */
    CHECK(log_[j].a[0]==POOL(4)&&log_[j+1].f==G(0x408bd8)&&log_[j+2].f==G(0x478628)&&log_[j+2].a[0]==0x4444u&&log_[j+3].f==G(0x471c70));
    j+=4;
    CHECK(log_[j].a[0]==POOL(5));++j;
    CHECK(r16(0,G(0x4d2526))==0);
    /* projectiles: the virtual +14h (ECX = the vtable). */
    CHECK(log_[j].a[0]==POOL(6)&&log_[j+1].f==0xc0000014u&&log_[j+1].slot==0x14&&log_[j+1].a[0]==OBJECT(6,0)&&log_[j+1].r[2]==OBJ+0x3a00u&&log_[j+1].r[0]==OBJECT(6,0));
    CHECK(ret_of(j+1)==G(0x45f756));
    j+=2;
    while(log_[j].f!=G(0x471c70)||log_[j].a[0]!=POOL(7))++j;
    /* buildings: the first's group is not pending (EAX 0, ECX the group), the second's is. */
    CHECK(log_[j+1].f==0xc0000018u&&log_[j+1].slot==0x18&&log_[j+1].r[0]==0&&log_[j+1].r[1]==OBJ+0x3b00u&&log_[j+1].r[2]==OBJ+0x2500u&&ret_of(j+1)==G(0x45f7a9));
    CHECK(log_[j+2].f==G(0x471c70)&&log_[j+3].f==G(0x471c70)&&log_[j+3].a[1]==OBJECT(7,1));
    j+=4;
    /* groups: group 0 pending -> 4236c4 (EDX 1, ECX [4a9e00]), group 1 -> 423a74 */
    CHECK(log_[j].f==G(0x4236c4)&&log_[j].a[0]==OBJ+0x2800u&&log_[j].r[0]==OBJ+0x2800u&&log_[j].r[1]==1&&log_[j].r[2]==OBJ+0x2800u&&ret_of(j)==G(0x45f7f1));
    CHECK(log_[j+1].f==G(0x423a74)&&log_[j+1].a[0]==OBJ+0x2800u+0x7a&&log_[j+1].r[1]==0&&log_[j+1].r[2]==OBJ+0x2800u&&ret_of(j+1)==G(0x45f7fa));
    CHECK(r16(0,EBPV-0x5c)==0xffffu&&r32(0,EBPV-0x60)==OBJ+0x2800u+2*0x7a);
    j+=2;
    /* step request: frozen afterwards, so no timers and no MECH loop. */
    CHECK(r16(0,G(0x4d24fc))==0&&r16(0,G(0x4d24f2))==1);
    CHECK(log_[j].f==G(0x460870)&&log_[j].r[0]==0&&log_[j].fr[0]==0&&log_[j].fr[2]==OBJECT(7,1));
    CHECK(log_[j+1].f==G(0x45de30));
    CHECK(log_[j+2].f==G(0x4131ac)&&log_[j+2].fr[0]==0);
    /* status 5: the alert with EBX = 5, the modal result 7 matches the table. */
    CHECK(log_[j+3].f==G(0x47749c)&&log_[j+3].a[0]==0x57e&&log_[j+3].fr[0]==5&&log_[j+3].fr[2]==0x00050005u&&ret_of(j+3)==G(0x45f9ea));
    CHECK(log_[j+4].f==G(0x455950)&&log_[j+4].n==4&&log_[j+4].a[0]==OBJ+0x3800u&&log_[j+4].a[1]==5&&log_[j+4].a[2]==1&&log_[j+4].a[3]==0);
    CHECK(log_[j+4].r[2]==0x57e&&log_[j+4].r[0]==OBJ+0x3800u&&log_[j+4].r[1]==OBJ+0x3800u);
    CHECK(log_[j+5].f==0xc0000008u&&log_[j+5].slot==8&&log_[j+5].r[2]==OBJ+0x3900u&&log_[j+5].r[1]==OBJ+0x3800u&&ret_of(j+5)==G(0x45fa2c));
    CHECK(log_[j+6].f==G(0x4543b0)&&log_[j+6].a[0]==OBJ+0x3800u&&log_[j+6].a[1]==3&&log_[j+6].r[1]==OBJ+0x3f00u&&ret_of(j+6)==G(0x45fa68));
    CHECK(calls==j+7&&r32(0,OBJ+0x3f00u)==99&&r16(0,EBPV-0x20)==0x50);
    CHECK(p==((uint64_t)0xfeedf00du<<32|1u)&&ecx==0x3000u+j+6);
    /* Unfrozen to the end: timers, triggers, actor pass and the MECH loop
     * (the second MECH object has +99h set); player dead, death sequence
     * returns 1: the alert with status 2; shim mode re-reads the slots. */
    build();reset();
    pool_count[8]=2;for(unsigned i=0;i<2;++i){w32(0,OBJECT(8,i)+0x45,OBJ+0x2600u);arena[OBJECT(8,i)-OBJ+0x99]=(uint8_t)i;}
    arena[0x2000+0x99]=1;death_eax=1;
    fr.top=EBPV-0x80u;
    p=es2_logic_frame_registers(&memory,&g,&fr,7,8,9,entry,&ecx);
    fr.top=0;
    for(j=0;log_[j].f!=G(0x460870);++j){}
    CHECK(log_[j+1].f==G(0x426b48)&&log_[j+1].r[2]==slot_after+j&&ret_of(j+1)==G(0x45f865));
    CHECK(log_[j+2].f==G(0x426b70)&&log_[j+2].a[0]==OBJ+0x2000u&&log_[j+2].r[2]==OBJ+0x2000u);
    CHECK(log_[j+3].f==G(0x45de30)&&log_[j+3].r[2]==slot_after+j+2);
    CHECK(log_[j+4].f==G(0x4123ac)&&ret_of(j+4)==G(0x45f886));
    CHECK(log_[j+5].f==G(0x471c70)&&log_[j+5].a[0]==POOL(8)&&log_[j+5].r[1]==POOL(8)&&log_[j+5].r[0]==0);
    CHECK(log_[j+6].f==G(0x41aa5c)&&log_[j+6].a[0]==OBJECT(8,0)&&log_[j+6].r[0]==OBJECT(8,0)+0x92u&&log_[j+6].r[1]==OBJ+0x2600u&&log_[j+6].r[2]==0);
    CHECK(log_[j+7].f==G(0x471c70)&&log_[j+7].r[2]==slot_after+j+6&&log_[j+8].f==G(0x471c70)&&log_[j+8].a[1]==OBJECT(8,1));
    CHECK(log_[j+9].f==G(0x45fa84)&&log_[j+9].r[1]==OBJ+0x2092u&&log_[j+9].r[2]==OBJ+0x2092u&&log_[j+9].fr[0]==0&&log_[j+9].fr[2]==OBJECT(8,1));
    CHECK(log_[j+10].f==G(0x47749c)&&log_[j+10].r[0]==1&&ret_of(j+10)==G(0x45f90f));
    CHECK(log_[j+11].f==G(0x455950)&&log_[j+11].a[1]==2&&log_[j+11].r[2]==slot_after+j+10);
    CHECK(log_[j+12].f==0xc0000008u&&log_[j+13].f==G(0x4543b0)&&ret_of(j+13)==G(0x45f979));
    CHECK(calls==j+14&&r32(0,OBJ+0x3f00u)==99&&r16(0,EBPV-0x20)==0x20);
    CHECK(p==((uint64_t)0xfeedf00du<<32|1u)&&ecx==0x3000u+j+13);
    /* player_input AX != 0 ends the frame with AX = 1 (high word kept). */
    build();w16(0,G(0x4d24f2),1);reset();player_eax=0xabcd0002u;
    p=es2_logic_frame_registers(&memory,&g,&fr,0,0,0,entry,&ecx);
    CHECK(calls==3&&p==((uint64_t)0xfeedf00du<<32|0xabcd0001u)&&ecx==OBJ+0x3000u);
    /* Status 3: group_assess; the player alive keeps status 3 (button
     * matches), the player killed by it gives the alert status 12h while
     * the table is indexed by the original status (button refused). */
    build();w16(0,G(0x4d24f2),1);reset();status_eax=3;modal_eax=0;
    w16(0,G(0x49f5c0)+2*3,0);
    p=es2_logic_frame_registers(&memory,&g,&fr,0,0,0,entry,&ecx);
    CHECK(log_[4].f==G(0x4131ac)&&log_[5].f==G(0x423f08)&&log_[5].a[0]==OBJ+0x2400u&&log_[5].r[0]==OBJ+0x2000u&&ret_of(5)==G(0x45f9c2));
    CHECK(log_[5].r[2]==OBJ+0x2000u&&log_[5].r[1]==OBJ+0x2400u);
    CHECK(log_[6].f==G(0x47749c)&&log_[6].fr[0]==3&&log_[6].r[0]==OBJ+0x2092u&&log_[6].r[2]==OBJ+0x2092u);
    CHECK(log_[7].f==G(0x455950)&&log_[7].a[1]==3);
    CHECK(p==((uint64_t)0xfeedf00du<<32|1u));                /* modal 0 == table word 0 */
    build();w16(0,G(0x4d24f2),1);reset();status_eax=3;modal_eax=1;assess_kill=1;
    w16(0,G(0x49f5c0)+2*3,0);
    p=es2_logic_frame_registers(&memory,&g,&fr,0,0,0,entry,&ecx);
    assess_kill=0;
    CHECK(log_[6].fr[0]==0x12&&log_[7].f==G(0x455950)&&log_[7].a[1]==0x12&&r32(0,EBPV-0x70)==OBJ+0x2092u);
    CHECK(p==((uint64_t)0xfeedf00du<<32));                   /* modal 1 != table word of status 3 */
    /* Status 2 and -1 return 0 without an alert. */
    build();w16(0,G(0x4d24f2),1);reset();status_eax=0x80000002u;
    p=es2_logic_frame_registers(&memory,&g,&fr,0,0,0,entry,&ecx);
    CHECK(calls==5&&(uint32_t)p==0);
    printf("PASS: logic_frame %u checks\n",checks);
    return 0;
}
