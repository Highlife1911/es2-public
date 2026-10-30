#include "es2/core/destructors.h"
#include "fixed.h"
#include <stddef.h>
/* Object destructors (docs/reverse-engineering/main_loop.md, section "object
 * destructors"). r holds what the original's EAX/EDX/ECX hold at the next game
 * call. The frame lives in the scratch at the original EBP offsets (f = the
 * synthetic EBP): the runtime record at f-24h with its context word at f-14h,
 * the shape slot f-4. */
#define R32(a) m->read32(m->context,(a))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define VA(g,x) ((g)->image+(x)-0x400000u)
static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
/* Direct calls name the original VA (observer key); the bridge gets the
 * address in this image. The virtual call passes its resolved target and
 * the vtable offset. */
static void dt_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,VA(g,va),a,n,r);
}
static void dt_virtual(const es2_weapon_graph *g,uint32_t target,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,0x18u);
    g->call(g->context,target,a,n,r);
}
/* dec dword [[6cbee4h]]: the pointer is read first; it stays in the
 * register the original loads it into. */
static uint32_t dt_down(const es2_guest_memory *m,const es2_weapon_graph *g) {
    const uint32_t counter=R32(VA(g,0x6cbee4u));
    W32(counter,R32(counter)-1u);
    return counter;
}
/* Register variants of the TS_OBJ tail (main_loop.md, "TS_OBJ tail"). */
enum { DT_A, DT_B, DT_C };
static const struct { uint32_t descriptor,vtable; unsigned variant; } dt_classes[ES2_DTOR_KINDS]={
    {0x497c28u,0x497d4cu,DT_A}, /* EXPLOSION 407e48 */
    {0x4a0af1u,0x4a0ba0u,DT_A}, /* FIRE 46b574 */
    {0x49867cu,0x4987c4u,DT_A}, /* BEAM 40ba94 */
    {0x498a18u,0,DT_B},         /* DEBRIS 40f7e6 */
    {0x49a206u,0,DT_B},         /* FlatObj 41bd46 */
    {0x49a6ecu,0,DT_B},         /* METEOR 424034 */
    {0x498579u,0,DT_C},         /* BULLET 40b64d */
    {0x4983a0u,0,DT_C},         /* ROCKET 40ab8d */
};

/* The TS_OBJ part shared by the eight destructors (407ea9, 46b5b7,
 * 40bad8, 40f80e, 41bd6e, 42405c, 40b677, 40abb7). */
static void dt_tail(const es2_guest_memory *m,const es2_weapon_graph *g,unsigned variant,uint32_t o,uint32_t flag,
    uint32_t f,es2_weapon_registers *r) {
    uint32_t a[2],target;
    W32(o,VA(g,0x4a0b80u));
    const uint32_t s=R32(o+0x34u);
    if(s) { /* 407eb4 / 40f818 / 40b681 */
        if(variant==DT_A) { W32(f-4u,s);r->eax=R32(f-4u);a[0]=r->eax; }
        else if(variant==DT_B) { r->eax=R32(o+0x34u);W32(f-4u,r->eax);r->edx=R32(f-4u);a[0]=r->edx; }
        else { r->edx=R32(o+0x34u);W32(f-4u,r->edx);r->ecx=R32(f-4u);a[0]=r->ecx; }
        /* cmp dword [f-4],0 (407ebd / 40f824 / 40b68d): nonzero, PROVED */
        a[1]=0;dt_call(g,0x496406u,a,2,r);
        const uint32_t counter=R32(VA(g,0x6cbee4u));
        if(variant==DT_B)r->ecx=counter;else r->edx=counter;
        W32(counter,R32(counter)+r->eax);
        W16(f-0x14u,0x14u);
        if(variant==DT_A) { r->ecx=R32(f-4u);r->eax=R32(r->ecx);a[0]=r->ecx;target=R32(r->eax+0x18u); }
        else if(variant==DT_B) { r->eax=R32(f-4u);r->edx=R32(r->eax);a[0]=r->eax;target=R32(r->edx+0x18u); }
        else { r->eax=R32(f-4u);r->ecx=R32(r->eax);a[0]=r->eax;target=R32(r->ecx+0x18u); }
        a[1]=3;dt_virtual(g,target,a,2,r);
        W16(f-0x14u,8u);
    }
    if(variant==DT_A) { r->edx=dt_down(m,g);r->ecx=dt_down(m,g); }
    else if(variant==DT_B) { r->ecx=dt_down(m,g);r->eax=dt_down(m,g); }
    else { r->eax=dt_down(m,g);r->edx=dt_down(m,g); }
    if(flag&1u) { /* 407f03 / 40f869 / 40b6c7 */
        a[0]=o;dt_call(g,0x477548u,a,1,r);r->ecx=o;
    }
}

/* Original: DBSIM.EXE 0x407e48, 0x46b574, 0x40ba94, 0x40f7e6, 0x41bd46,
 * 0x424034, 0x40b64d, 0x40ab8d (by kind) */
uint64_t es2_object_dtor_registers(const es2_guest_memory *m,const es2_weapon_graph *g,unsigned kind,
    uint32_t o,uint32_t flag,const uint32_t entry[3],uint32_t *ecx,uint32_t *head) {
    const uint32_t f=g->scratch+ES2_DESTRUCTORS_FRAME;
    const unsigned variant=dt_classes[kind].variant;
    uint32_t a[2];
    es2_weapon_registers r={VA(g,dt_classes[kind].descriptor),entry[1],entry[2]};
    dt_call(g,0x49630cu,NULL,0,&r);
    if(variant==DT_A) {
        r.edx=dt_down(m,g);
        if(o) { /* 407e67 / 46b593 / 40bab3 */
            W32(o,VA(g,dt_classes[kind].vtable));
            if(kind==ES2_DTOR_EXPLOSION) {
                r.eax=R32(o+0x4fu);
                if(r.eax) { /* 407e78 */
                    r.edx=R32(VA(g,0x4a9691u));a[0]=r.edx;a[1]=r.eax;dt_call(g,0x478628u,a,2,&r);
                }
                r.eax=R32(o+0x53u); /* read after the first append */
                if(r.eax) { /* 407e8f */
                    r.edx=R32(VA(g,0x4a9602u));a[0]=r.edx;a[1]=r.eax;dt_call(g,0x478628u,a,2,&r);
                }
                r.ecx=dt_down(m,g);
            } else if(kind==ES2_DTOR_FIRE) {
                const uint32_t fires=R32(VA(g,0x6b4f38u))-1u;
                W32(VA(g,0x6b4f38u),fires);
                if(!fires) { /* 46b5a5 */
                    a[0]=0x33u;dt_call(g,0x462ac4u,a,1,&r);r.ecx=0x33u;
                }
                r.ecx=dt_down(m,g);
            } else {
                r.ecx=R32(o+0x58u);a[0]=r.ecx;dt_call(g,0x47758cu,a,1,&r);r.ecx=a[0];
                r.eax=dt_down(m,g);
                r.edx=dt_down(m,g);
            }
            dt_tail(m,g,variant,o,flag,f,&r);
        }
        r.eax=R32(f-0x24u);*head=r.eax;
    } else if(variant==DT_B) {
        if(o) { /* 40f7fc / 41bd5c / 42404a */
            r.edx=dt_down(m,g);
            r.ecx=dt_down(m,g);
            dt_tail(m,g,variant,o,flag,f,&r);
        }
        r.edx=R32(f-0x24u);*head=r.edx;
    } else {
        if(o) { /* 40b669 / 40aba9 */
            r.edx=dt_down(m,g);
            r.ecx=dt_down(m,g);
            r.eax=dt_down(m,g);
            dt_tail(m,g,variant,o,flag,f,&r);
        }
        r.ecx=R32(f-0x24u);*head=r.ecx;
    }
    *ecx=r.ecx;
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x490bdc */
uint64_t es2_shape_dtor_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t s,uint32_t flag,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={0,entry[1],entry[2]};
    uint32_t a[2];
    r.eax=dt_down(m,g);
    if(s) { /* 490bec */
        W32(s,VA(g,0x4a62edu));
        const uint32_t block=R32(s+8u);
        r.edx=block;a[0]=block;dt_call(g,0x47758cu,a,1,&r);r.ecx=block;
        a[0]=s;a[1]=0;dt_call(g,0x48be20u,a,2,&r);
        if(flag&1u) { /* 490c0d */
            a[0]=s;dt_call(g,0x477548u,a,1,&r);r.ecx=s;
        }
    }
    *ecx=r.ecx;
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x48be20 */
uint64_t es2_shape_base_dtor_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t s,uint32_t flag,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={s,0,entry[2]};
    uint32_t a[1];
    r.edx=dt_down(m,g);
    if(s) { /* 48be30 */
        W32(s,VA(g,0x4a6a14u));
        if(flag&1u) { /* 48be3c */
            a[0]=s;dt_call(g,0x477548u,a,1,&r);r.ecx=s;
        }
    }
    *ecx=r.ecx;
    return pair(r.eax,r.edx);
}
