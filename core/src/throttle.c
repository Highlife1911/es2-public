#include "es2/core/throttle.h"
#include "fixed.h"
#include <stddef.h>
/* The throttle slider event (docs/reverse-engineering/cockpit.md, "throttle
 * slider event and shield step"). Guest reads happen in the original's order
 * and are repeated where the original repeats them. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define VA(va) (g->image+(uint32_t)(va)-0x400000u)
static uint64_t th_pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static void th_call(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,
    es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}

/* Original: DBSIM.EXE 0x43904c. The object at G+42h receives the event
 * (its virtual 0 takes the object, G and the event), then G's class table
 * G+17h (virtual 4, G) and the sub-object at G+3Eh (virtual 8, its own
 * address) run. EAX/EDX/ECX at each call are those the previous instructions
 * leave; the pop of the last argument leaves ECX = G+3Eh (not returned). */
uint64_t es2_slider_base_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t gadget,uint32_t event,const uint32_t entry[3]) {
    es2_weapon_registers r={event,entry[1],entry[2]};
    const uint32_t handler=R32(gadget+0x42u);
    const uint32_t handler_table=R32(handler);
    r.edx=handler;r.ecx=handler_table;
    const uint32_t first[3]={handler,gadget,event};
    th_call(g,R32(handler_table),0x42u,first,3,&r);
    const uint32_t table=R32(gadget+0x17u);
    r.eax=table;
    const uint32_t second[1]={gadget};
    th_call(g,R32(table+4u),0x17u,second,1,&r);
    const uint32_t sub=gadget+0x3eu;
    r.edx=sub;
    const uint32_t sub_table=R32(sub);
    r.ecx=sub_table;
    const uint32_t third[1]={sub};
    th_call(g,R32(sub_table+8u),0x3eu,third,1,&r);
    return th_pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x4483a0 (slot +8 of the throttle slider's table). */
uint64_t es2_slider_event_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t gadget,uint32_t event,const uint32_t entry[3]) {
    es2_weapon_registers r={gadget,entry[1],entry[2]};
    const uint32_t base[2]={gadget,event};
    th_call(g,VA(0x43904cu),0,base,2,&r);
    /* The pop after the last virtual of 43904c leaves ECX = G+3Eh whatever
     * the nested hook returns. */
    r.ecx=gadget+0x3eu;
    if(!(R8(VA(0x49db6cu))&1u))return th_pair(r.eax,r.edx);
    th_call(g,VA(0x477904u),0,NULL,0,&r);
    const uint32_t summary=r.eax;
    if(!R8(summary+4u))return th_pair(r.eax,r.edx);
    if(R8(VA(0x4d1f46u))!=1u)return th_pair(r.eax,r.edx);
    /* MOV AX,1 keeps the high word of P; ADD EAX,-2 borrows from it. */
    uint32_t eax=(summary&0xffff0000u)|1u;
    if(fx_s32(event)>0)eax-=2u;
    W16(VA(0x49a06eu),eax);
    return th_pair(eax,r.edx);
}

/* Original: DBSIM.EXE 0x413af8. DX = 66h, plus 0FF34h (high word borrowed
 * from EDX) for a zero word `up`; the sum is stored, DX reloads it, and the
 * clamp stores 400h (>= 400h as a signed word), the sum (> 0) or 0. */
uint64_t es2_shield_step_registers(const es2_guest_memory *m,uint32_t s,uint32_t up,const uint32_t entry[3]) {
    uint32_t edx=(entry[1]&0xffff0000u)|0x66u;
    if(!(uint16_t)up)edx+=0xffffff34u;
    W16(s+4u,R16(s+4u)+(uint16_t)edx);
    edx=(edx&0xffff0000u)|R16(s+4u);
    const int32_t sum=fx_s16(edx);
    W16(s+4u,sum>=0x400?0x400u:sum>0?(uint32_t)sum:0u);
    return th_pair(s,edx);
}
