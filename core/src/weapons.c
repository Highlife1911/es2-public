#include "es2/core/weapons.h"
#include "fixed.h"

static uint64_t pair(uint32_t a,uint32_t d) { return (uint64_t)d<<32 | a; }
static uint32_t word(uint32_t a,uint32_t w) { return (a&0xffff0000u)|(uint16_t)w; }
static int32_t signed_byte(uint8_t x) { return x<128 ? x : (int32_t)x-256; }
static uint64_t definition(uint32_t base,uint8_t index) {
    uint32_t p=base+14u*(uint32_t)signed_byte(index); return pair(p,p);
}
uint64_t es2_projectile_def_registers(uint32_t base,uint8_t index) { return definition(base,index); }
uint64_t es2_bullet_def_registers(uint32_t base,uint8_t index) { return definition(base,index); }
uint64_t es2_mask_merge_registers(const es2_guest_memory *m,uint32_t dst,uint32_t src) {
    for(unsigned i=0;i<8;++i) {
        uint8_t value=*(uint8_t *)m->host(m->context,src+i);
        uint8_t *out=m->host(m->context,dst+i);
        *out=(uint8_t)(*out&value);
    }
    return pair(src+8u,dst+8u);
}
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(uint8_t *)m->host(m->context,(a)))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
static void invoke(const es2_weapon_context *g,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    g->call(g->context,f,a,n,r);
}
static void slot(const es2_weapon_context *g,unsigned offset) {
    if(g->observe)g->observe(g->observer_context,offset);
}
uint64_t es2_radius_hit_registers(const es2_guest_memory *m,const es2_weapon_context *g,
    uint32_t point,uint32_t range,uint32_t damage,uint32_t source,uint32_t excluded,const uint32_t entry[3]) {
    uint32_t cursor=R32(g->list_slot);
    es2_weapon_registers r={cursor,entry[1],entry[2]};
    uint16_t index=0,hit=0;
    for(;;) {
        uint16_t old=index++; r.ecx=word(r.ecx,old);
        if(fx_s16(old)>=fx_s16(R16(g->count_slot)))break;
        r.eax=cursor;uint32_t actor=R32(cursor);cursor+=4u;
        if(!actor)break;
        r.edx=R32(actor+0x45u);r.ecx=R32(r.edx+0x14u)!=0;
        if(r.ecx || actor==excluded)continue;
        r.eax=R32(actor);slot(g,0x5c);
        invoke(g,R32(r.eax+0x5cu),&actor,1,&r);
        uint32_t radius=(uint32_t)fx_s16(r.eax);
        r.edx=radius;r.ecx=actor+0x26u;
        uint32_t distance_args[2]={r.ecx,point};
        invoke(g,g->distance,distance_args,2,&r);
        r.edx=radius;r.eax-=radius;
        if(fx_s32(r.eax)>=fx_s32(range))continue;
        r.eax=word(source,damage);r.ecx=word(r.ecx,range);
        uint32_t args[5]={actor,r.eax,point,r.ecx,source};
        r.edx=R32(actor);slot(g,0x70);
        invoke(g,R32(r.edx+0x70u),args,5,&r);hit=1;
    }
    return pair(word(r.eax,hit),r.edx);
}
uint64_t es2_weapon_step_registers(const es2_guest_memory *m,const es2_weapon_context *g,
    uint32_t w,uint32_t result,const uint32_t entry[3]) {
    es2_weapon_registers r={w+0x43u,entry[1],entry[2]};
    if(!R8(w+0x49u)) {
        uint32_t args[2]={w+0x33u,w+0x3bu};
        invoke(g,g->merge,args,2,&r);
        for(unsigned i=0;i<8;++i)W8(w+0x3bu+i,0);
        r.eax=w+0x3bu;r.edx=w+0x30u;r.ecx=0;
        uint32_t timer=w+0x30u;
        invoke(g,g->timer,&timer,1,&r);
        r.ecx=timer;r.eax=w+0x43u;
        if(R8(w+0x44u)) {
            uint32_t p=R32(w+0x10u),q=R32(w+0x14u);
            uint32_t numerator=(uint32_t)R16(q)+1u,divisor=R16(R32(p+0x20u));
            /* Original IDIV: nonzero unsigned-word divisor, numerator <=65536. */
            r.eax=numerator/divisor;r.edx=numerator%divisor;
            W16(q,r.edx);
            if(!r.edx) { r.eax=w+0x43u;W8(w+0x44u,0); }
        }
    }
    return pair(word(r.eax,result),r.edx);
}
uint64_t es2_weapon_fire_registers(const es2_guest_memory *m,const es2_weapon_context *g,
    uint32_t w,uint32_t owner,uint32_t free_word,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t prep[3]={w,owner,free_word};invoke(g,g->prepare,prep,3,&r);
    if(!(uint16_t)free_word) {
        r.eax=R32(w+0x1cu);r.edx=word(r.edx,R16(r.eax+0x38u));
        W16(w+0x7bu,R16(w+0x7bu)-r.edx);r.ecx=word(r.ecx,R16(w+0x7bu));
        if(fx_s16(r.ecx)<=0) { W16(w+0x7bu,0);r.eax=w+0x43u;W8(w+0x4cu,0); }
    }
    uint32_t type=R32(w+0x20u);
    if(!R16(type)) {
        r.eax=R32(owner);invoke(g,R32(r.eax+0x38u),&owner,1,&r);
        r.ecx=owner;r.edx=word(r.edx,R16(type+2u));
        uint32_t args[5]={r.edx,g->position,g->angles,owner,r.eax};
        invoke(g,g->projectile,args,5,&r);
    } else {
        r.ecx=R32(w+0x20u);r.eax=word(r.eax,R16(r.ecx+2u));
        uint32_t args[4]={r.eax,g->position,g->angles,owner};
        invoke(g,g->bullet,args,4,&r);
        r.edx=R32(w+0x18u);r.ecx=(uint32_t)signed_byte(R8(r.edx+6u));
        if(fx_s32(r.ecx)<4) { r.eax=w+0x43u;W8(w+0x44u,1); }
    }
    return pair(r.eax,r.edx);
}

#define W32(a,v) m->write32(m->context,(a),(v))
static void graph_call(const es2_weapon_graph *g,uint32_t f,unsigned slot_id,
                       const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,f,slot_id);
    g->call(g->context,f,a,n,r);
}
#define GC(va,args,n) graph_call(g,g->image+(va)-0x400000u,0,args,n,&r)
static void graph_matrix_copy(const es2_guest_memory *m,uint32_t out,uint32_t in) {
    for(unsigned i=0;i<20;i+=4)W32(out+i,R32(in+i));
    W32(out+28,R32(in+28));W32(out+20,R32(in+20));W32(out+24,R32(in+24));
}
uint64_t es2_weapon_prepare_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t w,uint32_t owner,const uint32_t entry[3]) {
    uint32_t b=g->image,s=g->scratch,manager=R32(owner+0x34u),definition=R32(w+0x18u);
    es2_weapon_registers r={R32(R32(manager+4u)),manager,R32(manager+4u)};
    uint32_t lookup[2]={r.ecx,word(definition,R16(definition))};
    (void)entry;
    graph_call(g,R32(r.eax+0x20u),0x20,lookup,2,&r);
    uint32_t record=b+0x2c56a8u;
    if(r.eax && fx_s16(R16(r.eax+4u))>=0)record=R32(manager+0x16u)+32u*(uint32_t)fx_s16(R16(r.eax+4u));
    if(R32(b+0x9ef34u) && owner==R32(b+0xd24e6u)) {
        graph_matrix_copy(m,s,record);W32(s+28,R32(s+28)-1500u);record=s;
    }
    uint32_t yaw=R32(w+0x5fu);
    if(!yaw || !R32(w+0x5bu)) {
        W16(s+96,0);W16(s+98,0);W16(s+100,0);
        if(!yaw)W16(s+100,R16(w+0x28u));
        if(!R32(w+0x5bu))W16(s+96,R16(w+0x24u));
        W8(s+64+19,g->padding);W8(s+32+19,g->composed_padding);
        r.eax=s+64;r.edx=s+96;
        uint32_t euler[2]={r.edx,r.eax};GC(0x47ebb8,euler,2);
        r.eax=record;r.edx=s+32;r.ecx=s+64;
        uint32_t rotate[3]={r.ecx,r.eax,r.edx};GC(0x47f4f4,rotate,3);record=s+32;
    }
    if(R32(b+0x9ef34u) && owner==R32(b+0xd24e6u))owner=R32(b+0xd2684u);
    if(!R16(owner+0x32u)) {
        r.eax=owner+0x12u;r.edx=owner+0xcu;
        uint32_t euler[2]={r.edx,r.eax};GC(0x47ebb8,euler,2);W16(owner+0x32u,1);
    }
    r.eax=owner+0x12u;r.edx=record;
    uint32_t compose[3]={record,r.eax,b+0xa9834u};GC(0x47fa20,compose,3);
    uint32_t config=R32(w+0x1cu);
    W16(s+104,R16(config+0x40u));W16(s+106,R16(config+0x42u));W16(s+108,R16(config+0x44u));
    r.eax=word(config+0x40u,R16(config+0x44u));
    r.edx=word(r.edx,R16(config+0x40u));r.ecx=word(r.ecx,R16(config+0x42u));
    GC(0x40f540,&w,1);
    uint32_t offset=r.eax;
    W16(s+108,R16(s+108)+R16(offset+4));r.edx=s+104;
    r.ecx=word(w,R16(offset));W16(s+104,R16(s+104)+r.ecx);
    r.eax=word(offset,R16(offset+2));W16(s+106,R16(s+106)+r.eax);
    uint32_t point[3]={s+104,b+0xa9834u,b+0xa9854u};GC(0x4801d4,point,3);
    uint32_t angles[2]={b+0xa9834u,b+0xa9860u};GC(0x47f9a0,angles,2);
    r.eax=R32(w+0x1cu);
    uint32_t cooldown[2]={(uint32_t)fx_s16(R16(w+0x63u)),(uint32_t)fx_s16(R16(r.eax+0x4cu))};
    GC(0x47e0b0,cooldown,2);W16(w+0x31u,r.eax);W8(w+0x33u,1);W8(w+0x3bu,1);
    return pair(w+0x3bu,r.edx);
}
/* The two cache tests are separate: a guest transform may invalidate it. */
static void flight_cache(const es2_guest_memory *m,const es2_weapon_graph *g,
                         uint32_t o,es2_weapon_registers *regs) {
    if(!R16(o+0x32u)) {
        regs->eax=o+0x12u;regs->edx=o+0xcu;
        uint32_t a[2]={regs->edx,regs->eax};
        graph_call(g,g->image+0x7ebb8u,0,a,2,regs);W16(o+0x32u,1);
    }
}
static void flight_position(const es2_guest_memory *m,uint32_t o,uint32_t p,es2_weapon_registers *r) {
    r->eax=o+0x26u;r->edx=R32(p+8);W32(o+0x2eu,r->edx);
    r->ecx=R32(p);W32(o+0x26u,r->ecx);r->edx=R32(p+4);W32(o+0x2au,r->edx);
}
uint64_t es2_bullet_flight_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t o,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t s=g->scratch;uint16_t done=0,guidance=0;
    GC(0x40adc0,&o,1);r.ecx=o;uint32_t d=r.eax;
    if(R16(d+6u)) {
        r.eax=o+0x58u;uint32_t t=r.eax;GC(0x467a48,&t,1);r.ecx=t;r.edx=!(uint16_t)r.eax;
        if(r.edx) {
            W16(o+0x59u,R16(d+6u));r.ecx=R32(o+0x34u);
            uint32_t p=R32(r.ecx+8u),den=R16(R32(R32(r.ecx+4u)+0x20u));
            uint32_t n=(uint32_t)R16(p)+1u;r.eax=n/den;r.edx=n%den;W16(p,r.edx);
            r.eax=R32(r.ecx+8u);
            if(!R16(r.eax)){r.ecx=R32(o+0x34u);r.eax=R32(r.ecx+8u);W16(r.eax,0);}
        }
    }
    uint32_t amount=512;GC(0x467924,&amount,1);r.ecx=amount;
    W16(o+0x54u,R16(o+0x54u)+r.eax);r.edx=word(r.edx,R16(o+0x54u));
    r.ecx=(uint32_t)fx_s16(r.edx);r.eax=(uint32_t)fx_s16(R16(d+2u))<<9;
    if(fx_s32(r.ecx)>fx_s32(r.eax))return pair(word(r.eax,1),r.edx);
    if(R32(o+0x5bu)){GC(0x40aff0,&o,1);r.ecx=o;guidance=(uint16_t)r.eax;}
    W16(s,0);W16(s+2,0);W16(s+4,0);
    r.edx=word(r.edx,R16(o+0x52u));amount=r.edx;GC(0x467924,&amount,1);r.ecx=amount;W16(s+2,r.eax);
    flight_cache(m,g,o,&r);r.eax=o+0x12u;r.edx=s+8;r.ecx=s;
    uint32_t transform[3]={s,o+0x12u,s+8};GC(0x4801d4,transform,3);
    flight_cache(m,g,o,&r);
    W32(s+24,o+0x12u);r.eax=(uint32_t)fx_s16(R16(s+2));W32(s+28,r.eax);
    r.edx=word(r.edx,R16(d+4u));W16(s+32,r.edx);
    r.ecx=R32(o+0x4eu);r.eax=word(r.eax,R16(r.ecx+6u));W16(s+96,r.eax);
    r.edx=R32(o+0x4eu);uint32_t second=R16(r.edx+4u);
    if(R16(o+0x56u)) {
        uint32_t a[2]={(uint32_t)fx_s16(R16(o+0x56u)),(uint32_t)fx_s16(R16(s+96))};
        r.eax=r.edx=a[1];GC(0x47e0b0,a,2);W16(s+96,r.eax);
        a[0]=(uint32_t)fx_s16(R16(o+0x56u));a[1]=(uint32_t)fx_s16(second);
        r.eax=r.edx=a[1];GC(0x47e0b0,a,2);second=r.eax;
    }
    r.eax=s+24;W32(s+72,r.eax);r.edx=R32(o+0x4au);W32(s+86,r.edx);
    r.ecx=word(r.ecx,R16(s+96));W16(s+76,r.ecx);W16(s+78,second);
    r.eax=R32(o+0x4eu);r.edx=word(r.edx,R16(r.eax+8u));r.eax+=12u;
    W16(s+80,r.edx);W32(s+82,r.eax);W16(s+90,5);W32(s+92,g->excluded);
    r.eax=(uint32_t)signed_byte(R8(o+0x41u));uint32_t contact=0;
    if(r.eax==9) {
        if(R32(o+0x5bu)) {
            int32_t magnitude=guidance==0x8000u?32767:fx_s16(guidance);
            if(magnitude<0)magnitude=-magnitude;r.edx=word(r.edx,(uint32_t)magnitude);
            if(magnitude>16384) {
                r.eax=R32(o+0x5bu)+0x26u;r.ecx=o+0x26u;
                uint32_t a[2]={r.ecx,r.eax};GC(0x49288c,a,2);contact=fx_s32(r.eax)<2000;
            }
        }
        r.eax=word(r.eax,R16(s+78));r.edx=word(r.edx,R16(s+76));
        uint32_t a[2]={r.edx,r.eax};GC(0x40501c,a,2);r.ecx=0;
        W16(s+80,0);W16(s+78,0);W16(s+76,0);
        r.eax=R32(s+86);r.edx=R32(r.eax+0x45u);r.eax=(uint32_t)signed_byte(R8(r.edx+0x12u));r.edx=r.eax;r.ecx=s+96;
        a[0]=r.ecx;a[1]=r.edx;GC(0x426b04,a,2);
        if(!contact){r.eax=s+72;GC(0x426528,&r.eax,1);r.ecx=s+72;contact=r.eax!=0;}
        if(contact) {
            r.ecx=o+0x26u;r.eax=R32(s+86);r.edx=(uint32_t)fx_s16(R16(s+96));
            uint32_t radius[5]={r.ecx,4000,r.edx,r.eax,0};GC(0x426a20,radius,5);
        }
    } else {
        r.eax=s+72;uint32_t impact=r.eax;GC(0x426528,&impact,1);r.ecx=impact;contact=r.eax;
    }
    if(contact)done=1;else flight_position(m,o,s+8,&r);
    return pair(word(r.eax,done),r.edx);
}
uint64_t es2_projectile_flight_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t o,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t b=g->image,s=g->scratch;uint16_t done=0;
    GC(0x40a234,&o,1);r.ecx=o;uint32_t d=r.eax;
    if(R16(d+8u)) {
        r.eax=o+0x5cu;uint32_t t=r.eax;GC(0x467a48,&t,1);r.ecx=t;r.edx=!(uint16_t)r.eax;
        if(r.edx) {
            W16(o+0x5du,R16(d+8u));r.ecx=word(r.ecx,R16(d+10u));uint32_t index=(uint16_t)r.ecx;
            uint32_t manager=R32(o+0x34u),state=R32(manager+8u),counts=R32(R32(manager+4u)+0x20u);
            uint32_t n=(uint32_t)R16(state+2u*index)+1u,den=R16(counts+2u*index);
            r.edx=n%den;r.eax=index;W16(R32(manager+8u)+2u*index,r.edx);
            r.edx=manager;r.ecx=R32(manager+8u);
            if(!R16(r.ecx+2u*index)) {
                r.eax=word(r.eax,R16(d+10u));r.edx=(uint16_t)r.eax;r.ecx=R32(o+0x34u);r.eax=R32(r.ecx+8u);
                W16(r.eax+2u*r.edx,0);
            }
        }
    }
    W16(o+0x54u,R16(o+0x54u)+1u);r.edx=word(r.edx,R16(o+0x54u));
    if(fx_s16(r.edx)>fx_s16(R16(d+2u)))done=1;
    else {
        int32_t old=fx_s16(R16(o+0x52u));r.eax=word(r.eax,R16(d+4u));uint32_t amount=r.eax;
        GC(0x467924,&amount,1);r.ecx=amount;W16(o+0x52u,R16(o+0x52u)+r.eax);
        r.eax=fx_sar((uint32_t)(fx_s16(R16(o+0x52u))+old),1);W16(o+0x52u,r.eax);
        r.edx=R32(o+0x4eu);r.edx=word(r.edx,R16(r.edx+10u));
        if(fx_s16(r.eax)>fx_s16(r.edx))W16(o+0x52u,r.edx);
        r.eax=R32(o+0x4au)+0x92u;
        if(R8(r.eax+0x11u) && signed_byte(R8(o+0x41u))==3) {
            r.ecx=3;uint32_t a[2]={o,b+0xd22c6u};GC(0x40a488,a,2);W16(b+0xd2526u,1);
        } else {if(R8(r.eax+0x11u))r.ecx=(uint32_t)signed_byte(R8(o+0x41u));GC(0x40a254,&o,1);r.ecx=o;}
        W16(s,0);W16(s+2,0);W16(s+4,0);r.eax=word(r.eax,R16(o+0x52u));amount=r.eax;
        GC(0x467924,&amount,1);r.ecx=amount;W16(s+2,r.eax);
        flight_cache(m,g,o,&r);r.eax=o+0x12u;r.edx=s+8;r.ecx=s;
        uint32_t a[3]={s,o+0x12u,s+8};GC(0x4801d4,a,3);flight_cache(m,g,o,&r);
        W32(s+24,o+0x12u);W32(s+28,(uint32_t)fx_s16(R16(s+2)));W16(s+32,R16(d+6u));
        W32(s+72,s+24);W32(s+86,R32(o+0x4au));r.eax=R32(o+0x4eu);
        W16(s+76,R16(r.eax+6u));W16(s+78,R16(r.eax+4u));W16(s+80,R16(r.eax+8u));
        r.edx=word(r.edx,R16(r.eax+8u));r.ecx=r.eax+12u;W32(s+82,r.ecx);W16(s+90,R16(r.eax+2u));W32(s+92,g->excluded);
        r.eax=s+72;uint32_t impact=r.eax;GC(0x426528,&impact,1);r.ecx=impact;
        if(r.eax)done=1;else flight_position(m,o,s+8,&r);
    }
    if(done) {
        r.eax=R32(o+0x4au)+0x92u;
        if(R8(r.eax+0x11u) && signed_byte(R8(o+0x41u))==3) {
            r.ecx=3;uint32_t a[2]={1,1};GC(0x45b734,a,2);W8(b+0xd22d3u,0);
        } else if(R8(r.eax+0x11u))r.ecx=(uint32_t)signed_byte(R8(o+0x41u));
        if(o==R32(b+0x9c394u)) {
            r.eax=word(r.eax,R16(o+0x54u));
            if(fx_s16(r.eax)<fx_s16(R16(d+2u)))W8(b+0x9c398u,1);
        }
    }
    r.eax=R32(o+0x4au)+0x92u;
    if(!R8(r.eax+0x11u)) {
        uint32_t camera=R32(b+0xd24eau)+4u;
        uint32_t a[3]={R32(o+0x26u)-R32(camera),R32(o+0x2au)-R32(camera+4u),R32(o+0x2eu)-R32(camera+8u)};
        r.eax=R32(camera+4u);r.edx=r.ecx=a[0];GC(0x47de72,a,3);
        if(fx_s32(r.eax)>=40000)W16(o+6u,0);
        else if(!R16(o+6u)) {uint32_t sound=50;GC(0x462830,&sound,1);r.ecx=sound;W16(o+6u,1);}
    }
    return pair(word(r.eax,done),r.edx);
}

/* Targeted actor emission also owns its steering decisions. The saved control
 * value is an explicit input for paths that never assign the original local. */
uint64_t es2_target_emit_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t actor,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t s=g->scratch,a[5],target=R32(actor+0x1a4u),control=g->excluded;
#define EC(va,n) GC(va,a,n)
    W32(s,R32(target+0x26u));W32(s+4,R32(target+0x2au));W32(s+8,R32(target+0x2eu));
    r.edx=R32(s);r.ecx=R32(s+4);r.eax=s;a[0]=s;a[1]=actor+0x26u;EC(0x492934,2);
    uint32_t yaw=word(r.eax,r.eax-R16(actor+0x10u));
    r.eax=s;EC(0x4928d0,2);uint32_t distance=r.eax;
    r.edx=R32(actor);a[0]=actor;graph_call(g,R32(r.edx+0x38u),0x38,a,1,&r);r.ecx=actor;
    int32_t speed=fx_s16(r.eax);target=R32(actor+0x1a4u);r.ecx=target;r.eax=R32(target);
    a[0]=target;graph_call(g,R32(r.eax+0x38u),0x38,a,1,&r);r.ecx=target;
    int32_t target_speed=fx_s16(r.eax);r.edx=fx_s32(distance)<200000?distance:200000u;uint32_t capped=r.edx;
    if(R16(actor+0x1feu)==1 && fx_s32(distance)<65000){
        a[0]=2;EC(0x40ffb0,1);r.eax=word(r.eax,R16(r.eax+10));r.ecx=2;speed=fx_s16(r.eax);
    }
    if(speed>0 && target_speed){
        uint32_t product=(uint32_t)((int64_t)fx_s16(fx_sar(capped,3))*(target_speed*8));
        r.edx=(uint32_t)(fx_s32(product)%speed);r.eax=(uint32_t)fx_s16((uint32_t)(fx_s32(product)/speed));
        uint32_t lead=r.eax;r.ecx=R32(actor+0x1a4u);r.eax=word(r.eax,R16(r.ecx+0x10u));r.edx=s;
        a[0]=s;a[1]=r.eax;a[2]=lead;EC(0x4929fc,3);
        r.ecx=actor+0x26u;r.eax=s;a[0]=s;a[1]=r.ecx;EC(0x492934,2);yaw=word(r.eax,r.eax-R16(actor+0x10u));
    }
    if(!R16(actor+0x1feu)){
        r.eax=actor+0x5au;yaw=word(yaw,yaw+32768u);W16(r.eax,0);
        if(fx_s32(distance)>90000)W16(actor+0x1feu,1);
        r.edx=R32(s+8)+10000u;a[0]=actor;a[1]=r.edx;EC(0x422108,2);control=r.eax;
    }else if(R16(actor+0x1feu)==1){
        if(fx_s32(distance)<65000){
            r.eax=actor+0x92u;
            if(R8(actor+0xaeu)){a[0]=actor;a[1]=0;EC(0x422108,2);}
            else{r.edx=actor+0x26u;r.ecx=s;a[0]=s;a[1]=r.edx;EC(0x49295c,2);a[0]=actor;a[1]=r.eax;EC(0x422098,2);}
            control=r.eax;r.eax=word(yaw,yaw+8192u);r.edx=(uint16_t)r.eax<16384u;
            int reset=!r.edx;
            if(!reset){r.ecx=R32(actor+0x2eu)-R32(s+8);reset=fx_s32(r.ecx)<3000 || fx_s32(distance)<10000;}
            if(reset)W16(actor+0x1feu,0);
        }else{r.eax=R32(s+8)+10000u;a[0]=actor;a[1]=r.eax;EC(0x422108,2);control=r.eax;}
    }
    if(R16(actor+0x1feu) && fx_s32(distance)<65000){
        r.edx=word(yaw,yaw+1000u);r.ecx=(uint16_t)r.edx<2000u;
        if(r.ecx){
            r.eax=actor+0x26u;r.edx=s;a[0]=s;a[1]=r.eax;EC(0x49295c,2);
            r.eax=word(r.eax,r.eax-R16(actor+0xcu)+1000u);r.ecx=(uint16_t)r.eax<2000u;
            if(r.ecx){
                r.eax=word(r.eax,R16(actor+0x1f4u)+10u);r.edx=(uint16_t)r.eax<20u;
                if(r.edx){
                    r.ecx=actor+0x21eu;a[0]=r.ecx;EC(0x467a48,1);r.ecx=actor+0x21eu;r.eax=!(uint16_t)r.eax;
                    if(r.eax){
                        W16(s+16,500);W16(s+18,200);W16(s+20,(uint16_t)-100);
                        uint32_t angles=actor+0xcu;r.edx=r.ecx=angles;
                        if(!R16(actor+0x32u)){r.eax=angles;r.edx=angles+6;r.ecx=angles;a[0]=angles;a[1]=r.edx;EC(0x47ebb8,2);r.eax=angles;W16(actor+0x32u,1);}
                        r.eax=angles+6;r.edx=s+24;r.ecx=s+16;a[0]=s+16;a[1]=angles+6;a[2]=s+24;EC(0x4801d4,3);
                        r.eax=actor+0x5au;
                        if(!R16(r.eax) && fx_s32(distance)<30000){
                            r.edx=word(r.edx,R16(actor+0x233u));r.ecx=angles;r.eax=s+24;
                            a[0]=0;a[1]=s+24;a[2]=angles;a[3]=actor;a[4]=r.edx;EC(0x40a9c4,5);r.edx=actor+0x5au;W16(r.edx,1);
                        }else{
                            r.ecx=angles;r.eax=s+24;a[0]=2;a[1]=s+24;a[2]=angles;a[3]=actor;EC(0x40b43c,4);
                            W16(s+16,0u-R16(s+16));
                            if(!R16(actor+0x32u)){r.eax=angles+6;r.edx=angles;a[0]=angles;a[1]=angles+6;EC(0x47ebb8,2);W16(actor+0x32u,1);}
                            r.eax=angles+6;r.edx=s+24;r.ecx=s+16;a[0]=s+16;a[1]=angles+6;a[2]=s+24;EC(0x4801d4,3);
                            r.eax=angles;r.edx=s+24;a[0]=2;a[1]=s+24;a[2]=angles;a[3]=actor;EC(0x40b43c,4);
                        }
                        W16(actor+0x21fu,1500);
                    }
                }
            }
        }
    }
    if(fx_s16(R16(actor+0x1feu))<2){W16(actor+0x21cu,180);r.ecx=control;a[0]=actor;a[1]=yaw;a[2]=control;a[3]=0;EC(0x4222fc,4);}
    return pair(r.eax,r.edx);
#undef EC
}

/* Native adapters explicitly supply all caller-frame residues. */
uint64_t es2_emit_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t w,uint32_t owner,uint32_t free_word,const uint32_t entry[3]) {
    uint32_t b=g->image,s=g->scratch,a[5];
    es2_weapon_registers r={word(entry[0],free_word),entry[1],entry[2]};
    a[0]=w;a[1]=owner;a[2]=r.eax;GC(0x40e788,a,3);
    r.edx=R32(w+0x18u);r.ecx=(uint32_t)signed_byte(R8(r.edx+6u));
    if(fx_s32(r.ecx)<4){r.eax=w+0x43u;W8(w+0x44u,1);}
    r.edx=R32(w+0x20u);
    if(R16(r.edx)==4){
        r.ecx=R32(b+0xa985cu);W32(b+0xa9850u,r.ecx);
        r.eax=R32(b+0xa9854u);W32(b+0xa9848u,r.eax);
        r.edx=R32(b+0xa9858u);W32(b+0xa984cu,r.edx);
        r.ecx=R32(w+0x1cu);r.edx=word(r.edx,R16(w+0x7du));r.eax=word(r.eax,R16(r.ecx+0x38u));
        uint32_t amount=fx_s16(r.eax)<=fx_s16(r.edx)?r.eax:r.edx;
        r.eax=R32(w+0x1cu);r.edx=R32(r.eax+0x30u);r.ecx=R32(w+0x20u);r.eax=word(r.eax,R16(r.ecx+2u));
        a[0]=r.eax;a[1]=b+0xa9834u;a[2]=r.edx;a[3]=owner;a[4]=amount;GC(0x40bf74,a,5);
        W16(w+0x7du,R16(w+0x7du)-amount);
    }else{
        r.edx=R32(w+0x1cu);r.eax=word(r.eax,R16(w+0x7du));r.ecx=word(r.ecx,R16(r.edx+0x38u));
        if(fx_s16(r.ecx)<=fx_s16(r.eax)){
            r.edx=R32(w+0x1cu);r.ecx=word(r.ecx,R16(r.edx+0x38u));W16(w+0x7du,R16(w+0x7du)-r.ecx);
            r.eax=R32(w+0x20u);r.edx=word(r.edx,R16(r.eax+2u));
            a[0]=r.edx;a[1]=b+0xa9854u;a[2]=b+0xa9860u;a[3]=owner;GC(0x40b43c,a,4);
        }else{
            a[4]=r.eax;r.edx=R32(w+0x20u);r.ecx=word(r.ecx,R16(r.edx+2u));
            a[0]=r.ecx;a[1]=b+0xa9854u;a[2]=b+0xa9860u;a[3]=owner;GC(0x40b5a0,a,5);
            r.eax=R32(w+0x1cu);
            if(R16(r.eax+0x3cu)==3){
                for(unsigned side=0;side<2;++side){
                    if(side)r.eax=R32(w+0x1cu);
                    r.edx=R32(r.eax+0x40u);W32(s,r.edx);r.edx=word(r.edx,R16(r.eax+0x44u));W16(s+4,r.edx);
                    W16(s,side?0u-R16(s):0);a[0]=w;GC(0x40f540,a,1);r.ecx=w;
                    r.ecx=word(r.ecx,R16(r.eax+4u));W16(s+4,R16(s+4)+r.ecx);
                    r.edx=word(r.edx,R16(r.eax));W16(s,R16(s)+r.edx);
                    r.eax=word(r.eax,R16(r.eax+2u));W16(s+2,R16(s+2)+r.eax);r.ecx=s;
                    a[0]=s;a[1]=b+0xa9834u;a[2]=b+0xa9854u;GC(0x4801d4,a,3);
                    r.eax=word(r.eax,R16(w+0x7du));a[4]=r.eax;r.edx=R32(w+0x20u);r.ecx=word(r.ecx,R16(r.edx+2u));
                    a[0]=r.ecx;a[1]=b+0xa9854u;a[2]=b+0xa9860u;a[3]=owner;GC(0x40b5a0,a,5);
                }
            }
            r.edx=R32(w+0x1cu);
            if(R16(r.edx+0x3eu)==19 && !R8(w+0x4du)){
                r.eax=fx_sar((uint32_t)fx_s16(R16(r.edx+0x4cu)),2);W16(w+0x31u,r.eax);W8(w+0x4du,1);
            }else{W8(w+0x4du,0);W16(w+0x7du,0);}
            r.eax=w+0x43u;
        }
    }
    return pair(r.eax,r.edx);
}
uint64_t es2_alt_emit_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t w,uint32_t owner,uint32_t free_word,const uint32_t entry[3]) {
    uint32_t b=g->image,a[5];es2_weapon_registers r={word(entry[0],free_word),entry[1],entry[2]};
    a[0]=w;a[1]=owner;a[2]=r.eax;GC(0x40e788,a,3);
    r.edx=R32(w+0x1cu);r.ecx=word(r.ecx,R16(r.edx+0x38u));W16(w+0x7du,R16(w+0x7du)-r.ecx);
    r.eax=R32(b+0xa985cu);W32(b+0xa9850u,r.eax);r.edx=R32(b+0xa9854u);W32(b+0xa9848u,r.edx);
    r.ecx=R32(b+0xa9858u);W32(b+0xa984cu,r.ecx);
    r.eax=R32(w+0x1cu);r.edx=R32(r.eax+0x30u);r.ecx=R32(w+0x20u);r.eax=word(r.eax,R16(r.ecx+2u));
    a[0]=r.eax;a[1]=b+0xa9834u;a[2]=r.edx;a[3]=owner;a[4]=1200;GC(0x40bf74,a,5);
    return pair(r.eax,r.edx);
}
uint64_t es2_impact_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t type,uint32_t matrix,uint32_t range,uint32_t owner,uint32_t scale,const uint32_t entry[3]) {
    uint32_t b=g->image,s=g->scratch,a[5];es2_weapon_registers r={b+0x986ccu,entry[1],entry[2]};
    GC(0x49630c,NULL,0);
    /* Locals use their original distances from the synthetic frame s+208.
     * This keeps the full segment, packet and runtime exception frame apart. */
    uint32_t segment=s+104,packet=s+56,vector=s+92,endpoint=s+80,current=s,step=s+12,rotation=s+24,long_vector=s+44;
    W32(segment,matrix);W32(segment+4,range);W16(segment+8,200);
    W32(vector,0);W32(vector+4,0);W32(vector+8,0);r.eax=0;r.edx=0;r.ecx=(uint16_t)type;
    a[0]=4;a[1]=r.ecx;GC(0x40ffc8,a,2);uint32_t definition=r.eax;
    r.ecx=(uint32_t)fx_s16(scale);r.eax=packet;W32(packet,segment);r.edx=owner;W32(packet+14,owner);
    r.eax=(uint32_t)fx_s16(R16(definition+6u));r.edx=r.eax;
    a[0]=(uint32_t)fx_s16(scale);a[1]=r.edx;r.ecx=a[0];GC(0x47e0b0,a,2);W16(packet+4,r.eax);
    r.eax=(uint32_t)fx_s16(scale);r.edx=(uint32_t)fx_s16(R16(definition+4u));r.ecx=r.edx;
    a[0]=r.eax;a[1]=r.edx;GC(0x47e0b0,a,2);W16(packet+6,r.eax);
    r.edx=word(r.edx,R16(definition+8u));W16(packet+8,r.edx);W32(packet+10,definition+12u);W16(packet+18,5);W32(packet+20,g->excluded);
    r.eax=packet;GC(0x426528,&packet,1);r.ecx=packet;W32(vector+4,r.eax);
    if(!r.eax){r.edx=range;W32(vector+4,range);}
    r.ecx=matrix+20u;a[0]=11;a[1]=r.ecx;GC(0x4628e0,a,2);
    r.edx=endpoint;r.eax=matrix;r.ecx=vector;a[0]=vector;a[1]=matrix;a[2]=endpoint;GC(0x48043c,a,3);
    r.eax=(uint32_t)fx_s16(type)-1u;
    int single=!r.eax;if(!single){r.eax-=6u;single=!r.eax;}
    if(single){
        r.edx=R32(b+0xa96c2u);a[0]=r.edx;GC(0x471b30,a,1);r.ecx=a[0];uint32_t object=r.eax;W32(s+204,object);
        if(object){
            W16(s+180,20);r.eax=word(owner,type);r.edx=object;
            a[0]=object;a[1]=r.eax;a[2]=matrix+20u;a[3]=endpoint;a[4]=owner;GC(0x40b804,a,5);
            r.ecx=R32(b+0x2cbee4u);W32(r.ecx,R32(r.ecx)-5u);W16(s+180,8);
        }
    }else{
        W32(long_vector,0);W32(long_vector+4,5000);W32(long_vector+8,0);
        for(unsigned i=0;i<18;i+=2)W16(rotation+i,R16(matrix+i));W8(rotation+18,R8(matrix+18));W8(rotation+19,R8(matrix+19));
        W32(current,R32(matrix+20));W32(current+4,R32(matrix+24));W32(current+8,R32(matrix+28));
        r.edx=rotation;r.ecx=long_vector;r.eax=step;uint32_t remaining=R32(vector+4);
        a[0]=long_vector;a[1]=rotation;a[2]=step;GC(0x480304,a,3);
        while(fx_s32(remaining)>5000){
            r.eax=R32(step+8);W32(current+8,R32(current+8)+r.eax);r.edx=R32(step);W32(current,R32(current)+r.edx);r.ecx=R32(step+4);W32(current+4,R32(current+4)+r.ecx);
            r.eax=R32(b+0xa96c2u);a[0]=r.eax;GC(0x471b30,a,1);r.ecx=a[0];uint32_t object=r.eax;W32(s+200,object);
            if(object){
                W16(s+180,44);r.eax=word(owner,type);r.edx=object;r.ecx=matrix+20u;
                a[0]=object;a[1]=r.eax;a[2]=r.ecx;a[3]=current;a[4]=owner;GC(0x40b804,a,5);
                r.ecx=R32(b+0x2cbee4u);W32(r.ecx,R32(r.ecx)-5u);W16(s+180,32);
            }
            r.eax=matrix+20u;r.edx=R32(current+8);remaining-=5000;W32(r.eax+8,r.edx);r.ecx=R32(current);W32(r.eax,r.ecx);r.edx=R32(current+4);W32(r.eax+4,r.edx);
        }
        r.eax=R32(b+0xa96c2u);a[0]=r.eax;GC(0x471b30,a,1);r.ecx=a[0];uint32_t object=r.eax;W32(s+196,object);
        if(object){
            W16(s+180,68);r.eax=word(owner,type);r.edx=object;
            a[0]=object;a[1]=r.eax;a[2]=matrix+20u;a[3]=endpoint;a[4]=owner;GC(0x40b804,a,5);
            r.ecx=R32(b+0x2cbee4u);W32(r.ecx,R32(r.ecx)-5u);W16(s+180,56);
        }
    }
    return pair(R32(s+164),r.edx);
}

/* The hit-accumulator leaves 427d98 (dword [4aaad0h] = EAX = 0) and
 * 427da0 (EAX = [4aaad0h]) are computed here; each site is still reported
 * to the observer (the callee counters), the original is not called.
 * EDX and ECX are unchanged by both (weapons.md, "weapon and projectile
 * helpers"). */
static void sweep_leaf(const es2_weapon_graph *g,uint32_t va) {
    if(g->observe)g->observe(g->observer_context,g->image+va-0x400000u,0);
}
#define SWEEP_CLEAR() do{sweep_leaf(g,0x427d98u);r.eax=0;W32(b+0xaaad0u,0);}while(0)
#define SWEEP_READ() do{sweep_leaf(g,0x427da0u);r.eax=R32(b+0xaaad0u);}while(0)
/* Swept terrain and object query 426528. The synthetic EBP is scratch+208:
 * the runtime places its exception record at frame-36 (context word -20,
 * object slot -4), and every buffer passed to a callee keeps its original
 * frame distance. Locals the original keeps only in registers or never
 * hands out stay in C variables. */
uint64_t es2_sweep_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t p,const uint32_t entry[3]) {
    uint32_t b=g->image,f=g->scratch+208u,a[5];
    es2_weapon_registers r={b+0x9a909u,entry[1],entry[2]};
    GC(0x49630c,NULL,0);
    uint16_t index=0,contact=0,object=0;
    uint32_t cursor=R32(b+0xa9af8u);r.edx=cursor;
    uint32_t seg=R32(p);r.ecx=seg;
    if(R32(p+0xeu)) {
        r.eax=R32(p+0xeu);r.edx=R32(r.eax+0x45u);r.ecx=(uint32_t)signed_byte(R8(r.edx+0x12u));r.eax=r.ecx;
        r.edx=p+6u;a[0]=r.edx;a[1]=r.eax;GC(0x426b04,a,2);
        r.ecx=R32(p+0xeu);r.eax=R32(r.ecx+0x45u);r.edx=(uint32_t)signed_byte(R8(r.eax+0x12u));r.ecx=r.edx;
        r.eax=p+4u;a[0]=r.eax;a[1]=r.ecx;GC(0x426b04,a,2);
    }
    /* The first endpoint is computed for its callee effects only. */
    r.eax=R32(seg+4u);r.edx=0;W32(f-0x94u,0);W32(f-0x90u,r.eax);r.ecx=0;r.edx=f-0x88u;W32(f-0x8cu,0);
    r.ecx=f-0x94u;r.eax=R32(seg);a[0]=r.ecx;a[1]=r.eax;a[2]=r.edx;GC(0x48043c,a,3);
    /* World-to-segment transform: copy the matrix, transpose it and rotate
     * the negated translation, in the original dword order. */
    uint32_t matrix=R32(seg),inverse=seg+0xau;r.edx=matrix;
    for(unsigned i=0;i<20;i+=4)W32(inverse+i,R32(matrix+i));
    r.ecx=matrix+0x14u;r.edx=inverse+0x14u;r.eax=inverse;
    W32(r.edx+8u,R32(r.ecx+8u));W32(r.edx,R32(r.ecx));r.ecx=R32(r.ecx+4u);W32(r.edx+4u,r.ecx);
    a[0]=inverse;GC(0x47df19,a,1);r.ecx=inverse;
    r.edx=inverse+0x14u;r.eax=r.edx;
    W32(r.eax+8u,0u-R32(r.eax+8u));W32(r.eax,0u-R32(r.eax));W32(r.eax+4u,0u-R32(r.eax+4u));
    r.ecx=R32(r.eax);W32(f-0xa0u,r.ecx);r.ecx=R32(r.eax+4u);W32(f-0x9cu,r.ecx);r.eax=R32(r.eax+8u);W32(f-0x98u,r.eax);
    a[0]=f-0xa0u;a[1]=inverse;a[2]=r.edx;r.edx=a[0];GC(0x480304,a,3);
    r.eax=0;uint32_t hit=0,owner=R32(p+0xeu);
    SWEEP_CLEAR();
    r.eax=seg;r.edx=f-0x40u;a[0]=r.edx;a[1]=r.eax;GC(0x428048,a,2);
    if((uint16_t)r.eax) {
        r.ecx=seg;r.eax=R32(seg+4u);
        if(fx_s32(r.eax)>fx_s32(R32(f-0x40u))) {
            r.edx=seg;r.ecx=R32(f-0x40u);W32(seg+4u,r.ecx);r.eax=R32(f-0x3cu);hit=r.eax;contact=1;
        }
    }
    SWEEP_READ();W32(b+0xaaa6cu,r.eax);
    SWEEP_CLEAR();
    for(;;) {
        uint16_t old=index++;uint32_t actor=0;r.eax=word(r.eax,old);
        if(fx_s16(old)<fx_s16(R16(b+0xa9afeu))) { r.edx=cursor;cursor+=4u;actor=R32(r.edx); }
        if(!actor)break;
        if(actor==owner || actor==R32(p+0x14u))continue;
        r.edx=R32(actor+0x45u);r.ecx=R32(r.edx+0x14u)!=0;
        if(r.ecx)continue;
        r.eax=R32(actor);a[0]=actor;a[1]=p;graph_call(g,R32(r.eax+0x20u),0x20,a,2,&r);
        uint32_t distance=r.eax;
        if(!distance)continue;
        if(owner) {
            r.edx=R32(actor+0x45u);r.eax=R32(owner+0x45u);
            r.ecx=(uint32_t)signed_byte(R8(r.edx+0x12u));r.edx=(uint32_t)signed_byte(R8(r.eax+0x12u));
            r.ecx=(uint16_t)r.ecx==(uint16_t)r.edx;uint32_t same=r.ecx;
            /* actor is never null here; the original's null test cannot fire. */
            if(R32(owner+0x1a4u)!=actor) { r.eax=actor+0xcu;hit=r.eax; }
            else {
                r.eax=owner+0x92u;W8(r.eax+0xcu,1);r.edx=R32(actor+0x1b2u);
                if(r.edx) {
                    r.eax=actor+0x92u;
                    if(!R8(r.eax+0x10u)) { a[0]=r.edx;GC(0x423430,a,1);r.ecx=a[0]; }
                }
                r.edx=0;hit=0;
            }
            r.eax=actor+0x92u;
            int notify=!R8(r.eax+7u);
            if(notify && !same) {
                /* +99 was zero just above, so only +a5/+a4 can suppress. */
                if(R8(r.eax+0x13u)) { r.ecx=1;notify=0; }
                else {
                    r.edx=actor+0x92u;
                    if(R8(r.edx+0x12u))r.edx=1;else { r.ecx=actor+0x92u;r.eax=r.ecx;r.edx=0; }
                    if((uint16_t)r.edx) { r.ecx=1;notify=0; } else r.ecx=0;
                }
            }
            if(notify) {
                r.edx=R32(owner+0x45u);r.eax=owner+0x92u;
                if(R8(r.eax+0x11u) && same && r.edx!=R32(actor+0x45u)) {
                    a[0]=r.edx;a[1]=owner;GC(0x423974,a,2);uint32_t ally=r.eax;
                    if(ally) {
                        r.ecx=owner+0x26u;r.eax=ally+0x26u;a[0]=r.eax;a[1]=r.ecx;GC(0x49288c,a,2);
                        if(fx_s32(r.eax)<30000) { r.edx=ally;a[0]=ally;GC(0x41f790,a,1);r.ecx=a[0]; }
                    }
                }
                r.ecx=word(r.ecx,R16(p+4u));r.eax=R32(actor);
                a[0]=actor;a[1]=owner;a[2]=r.ecx;graph_call(g,R32(r.eax+0x50u),0x50,a,3,&r);
            }
        }
        r.edx=seg;r.ecx=distance;W32(seg+4u,distance);contact=1;object=1;
        if(fx_s32(distance)<500)break;
    }
    SWEEP_READ();W32(b+0xaaa68u,r.eax);
    if(contact) {
        if(!object) {
            r.eax=seg;r.ecx=0;r.edx=R32(seg+4u);W32(f-0xb8u,0);r.eax=r.edx;r.edx=0;W32(f-0xb4u,r.eax);W32(f-0xb0u,0);
            r.edx=f-0xacu;r.eax=R32(seg);r.ecx=r.eax;r.eax=f-0xb8u;
            a[0]=r.eax;a[1]=r.ecx;a[2]=r.edx;GC(0x48043c,a,3);
            r.edx=R32(b+0xa961eu);a[0]=r.edx;GC(0x471b30,a,1);r.ecx=a[0];
            uint32_t effect=r.eax;W32(f-4u,effect);
            if(effect) {
                W16(f-0x14u,0x14);r.edx=f-0xacu;a[0]=b+0xd2599u;GC(0x492ee0,a,1);
                r.eax=word(r.eax,r.eax&3u);r.ecx=(uint32_t)fx_s16(r.eax);r.eax=R32(p+0xau);
                r.edx=word(r.edx,R16(r.eax+8u+2u*r.ecx));r.ecx=R32(f-4u);
                a[0]=r.ecx;a[1]=r.edx;a[2]=f-0xacu;a[3]=0;a[4]=0;GC(0x407f1c,a,5);
                r.eax=R32(b+0x2cbee4u);W32(r.eax,R32(r.eax)-4u);W16(f-0x14u,8);
            }
        }
        uint32_t target=hit && owner?R32(owner+0x1a4u):0;
        if(target) {
            r.eax=seg;r.ecx=R32(seg+4u);r.eax=0;W32(f-0xc4u,0);r.edx=r.ecx;r.ecx=0;W32(f-0xc0u,r.edx);W32(f-0xbcu,0);
            r.ecx=f-0xd0u;r.edx=R32(seg);r.eax=r.edx;r.edx=f-0xc4u;
            a[0]=r.edx;a[1]=r.eax;a[2]=r.ecx;GC(0x48043c,a,3);
            r.ecx=f-0xd0u;r.eax=owner+0x26u;a[0]=r.eax;a[1]=r.ecx;GC(0x4928d0,a,2);
            uint32_t reach=r.eax;
            r.eax=target;r.ecx=owner+0x26u;r.edx=target+0x26u;a[0]=r.ecx;a[1]=r.edx;GC(0x49288c,a,2);
            if(fx_s32(reach)<fx_s32(r.eax)) {
                r.eax=owner+0x26u;r.edx=f-0xd0u;a[0]=r.edx;a[1]=r.eax;GC(0x492934,a,2);
                uint32_t turn=word(r.eax,r.eax-R16(owner+0x10u));
                r.eax=target;r.edx=target+0x26u;r.ecx=owner+0x26u;r.eax=r.edx;a[0]=r.eax;a[1]=r.ecx;GC(0x492934,a,2);
                r.eax=word(r.eax,r.eax-R16(owner+0x10u));turn=word(turn,turn-r.eax);
                if(fx_s16(turn)<0x2000) {
                    r.edx=hit;r.ecx=R32(owner);a[0]=owner;a[1]=hit;graph_call(g,R32(r.ecx+0x64u),0x64,a,2,&r);
                }
            }
        }
    }
    r.eax=contact?R32(seg+4u)+1u:0;
    return pair(r.eax,R32(f-0x24u));
}

/* 426b04: scale a signed damage word by the side/difficulty table entry. The
 * table index is the raw signed difficulty word, unclamped. EAX/EDX come from
 * the delegated Q10 multiply; its incoming EAX and EDX both hold the old word. */
uint64_t es2_side_scale_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t destination,uint32_t side,const uint32_t entry[3]) {
    uint32_t b=g->image,table=side?b+0x9a744u:b+0x9a73cu;
    uint32_t factor=(uint32_t)fx_s16(R16(table+2u*(uint32_t)fx_s16(R16(b+0xa9e5cu))));
    uint32_t old=(uint32_t)fx_s16(R16(destination));
    es2_weapon_registers r={old,old,entry[2]};
    uint32_t a[2]={factor,old};GC(0x47e0b0,a,2);
    W16(destination,r.eax);
    return pair(r.eax,r.edx);
}

/* 427238: highest grid cell height around a point. Shift counts use only
 * their low five bits; coordinates and loop counters are signed words that
 * wrap like the original's. Callers must keep the traversal finite (no upper
 * bound of 32767) and every in-bounds row/cell readable. EDX is the resource. */
uint64_t es2_cell_registers(const es2_guest_memory *m,uint32_t resource,uint32_t point,uint32_t radius_slot) {
    uint32_t shift=R16(resource+0x32u),threshold=1u<<(((uint32_t)fx_s16(shift)-1u)&31u);
    uint32_t radius=(uint32_t)fx_s16(radius_slot),x=R32(point);
    if(fx_s32(threshold)>=fx_s32(radius)) {
        uint16_t column=(uint16_t)fx_sar(x,shift),row=(uint16_t)fx_sar(R32(point+4u),shift);
        if(fx_s16(column)<0 || fx_s16(column)>=fx_s16(R16(resource+0x2au)) ||
           fx_s16(row)<0 || fx_s16(row)>=fx_s16(R16(resource+0x2cu)))return pair(0,resource);
        uint32_t cells=R32(R32(resource+0x434u)+4u*(uint32_t)fx_s16(row));
        return pair(R32(resource+0x34u+4u*R8(cells+(uint32_t)fx_s16(column))),resource);
    }
    uint16_t x0=(uint16_t)fx_sar(x-radius,shift),y0=(uint16_t)fx_sar(R32(point+4u)-radius,shift);
    uint16_t x1=(uint16_t)fx_sar(x+radius,shift),y1=(uint16_t)fx_sar(R32(point+4u)+radius,shift);
    uint32_t best=0,rows=R32(resource+0x434u)+4u*(uint32_t)fx_s16(y0);
    for(uint16_t y=y0;fx_s16(y)<=fx_s16(y1);++y,rows+=4u) {
        for(uint16_t column=x0;fx_s16(x1)>=fx_s16(column);++column) {
            if(fx_s16(column)<0 || fx_s16(column)>=fx_s16(R16(resource+0x2au)) ||
               fx_s16(y)<0 || fx_s16(y)>=fx_s16(R16(resource+0x2cu)))continue;
            uint32_t height=R32(resource+0x34u+4u*R8(R32(rows)+(uint32_t)fx_s16(column)));
            if(fx_s32(height)>fx_s32(best))best=height;
        }
    }
    return pair(best,resource);
}

/* 46e980: grid walk along the segment start..end (cdecl map, start, end,
 * radius, out, mode). The 47e068/47e08d Q16 helpers are pure and computed
 * here; octants 2 and 3 multiply inline with a 32-bit product instead.
 * Callees receive pointers to the direction words and to the previous and
 * current points, so those live in scratch at their original EBP offsets.
 * The step count is bounded by the grid; callers keep it small. */
static uint32_t segment_div(uint32_t a,uint32_t b) { return (uint32_t)(int32_t)(fx_s32(a)*65536/fx_s32(b)); }
static uint32_t segment_mul(uint32_t a,uint32_t b) { return (uint32_t)((uint64_t)(fx_s32(a)*fx_s32(b))>>16); }
static uint32_t segment_imul(uint32_t a,uint32_t b) { return fx_sar(a*b,16); }
static uint32_t segment_low(uint32_t x) { return (x&0xffffff00u)|1u; }
typedef struct segment_octant { uint8_t major,primary_down,secondary_down,inline_first,inline_cross; } segment_octant;
static const segment_octant segment_octants[8]={
    {0,0,0,0,0},{1,0,0,0,0},{1,0,1,0,1},{0,1,0,1,1},{0,1,1,0,0},{1,1,1,0,0},{1,1,0,0,0},{0,0,1,0,0}};
static void segment_copy(const es2_guest_memory *m,uint32_t to,uint32_t from) {
    W32(to+8u,R32(from+8u));W32(to,R32(from));W32(to+4u,R32(from+4u));
}
static int segment_outside(const es2_guest_memory *m,uint32_t map,uint32_t x,uint32_t y) {
    return fx_s32(x)<0||fx_s32(x)>=fx_s32(1u<<(R32(map+0x100u)&31u))||fx_s32(y)<0||fx_s32(1u<<(R32(map+0x104u)&31u))<=fx_s32(y);
}
uint64_t es2_segment_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t map,uint32_t start,
    uint32_t end,uint32_t radius,uint32_t out,uint32_t mode,const uint32_t entry[3]) {
    uint32_t f=g->scratch+0xb0u,vec=f-0x28u,prev=f-0x90u,cur=f-0x9cu,saved=f-0xa8u,a[6];
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t dx=R32(end)-R32(start),dy=R32(end+4u)-R32(start+4u),dz=R32(end+8u)-R32(start+8u);
    while(fx_s32(dx)>32000||fx_s32(dx)<-32000||fx_s32(dy)>32000||fx_s32(dy)<-32000||fx_s32(dz)>32000||fx_s32(dz)<-32000)
        { dx=fx_sar(dx,1);dy=fx_sar(dy,1);dz=fx_sar(dz,1); }
    if(mode) { W16(vec,dx);W16(vec+2u,dy);W16(vec+4u,dz);a[0]=vec;a[1]=0x800;GC(0x46c23c,a,2); }
    /* Q16 slopes Y/X, Z/X, X/Y, Z/Y; 1.0 when the divisor is zero. */
    uint32_t yx=dx?segment_div(dy,dx):0x10000u,zx=dx?segment_div(dz,dx):0x10000u;
    uint32_t xy=dy?segment_div(dx,dy):0x10000u,zy=dy?segment_div(dz,dy):0x10000u;
    int64_t sx=fx_s32(dx),sy=fx_s32(dy);
    unsigned octant=sx>=0?(sy>=0?(sy<sx?0:1):(-sy>sx?6:7)):(sy>=0?(sy>-sx?2:3):(sy>sx?4:5));
    const segment_octant *o=segment_octants+octant;
    segment_copy(m,prev,start);
    uint32_t cell=1u<<(R32(map+0x108u)&31u);
    uint32_t cx=fx_sar(R32(prev),R32(map+0x108u)),cy=fx_sar(R32(prev+4u),R32(map+0x108u)),px=cx,py=cy;
    if(fx_s32(px)<0)return pair(0,cell);
    if(segment_outside(m,map,px,py))return pair(0,1u<<(R32(map+0x100u)&31u));
    uint32_t bound[2]={px<<(R32(map+0x108u)&31u),py<<(R32(map+0x108u)&31u)},*count[2]={&cx,&cy};
    if(octant<2||octant>5)bound[0]+=cell;
    if(octant<4)bound[1]+=cell;
    uint32_t loop_edx=bound[1],direction=0,step[2]={0,0},step_z=0;
    uint8_t saved_flag=0,done=0,reached=0,first=1;
    /* Direction codes: X+ 1, Y+ 2, X- 0, Y- 3. */
    unsigned major=o->major,minor=!major;
    uint32_t primary_code=major?(o->primary_down?3u:2u):(o->primary_down?0u:1u);
    uint32_t secondary_code=minor?(o->secondary_down?3u:2u):(o->secondary_down?0u:1u);
    uint32_t major_slope=major?xy:yx,major_z=major?zy:zx,minor_slope=major?yx:xy,minor_z=major?zx:zy;
    for(;;) {
        if(segment_outside(m,map,px,py))return pair(0,loop_edx);
        if(saved_flag) {
            saved_flag=0;segment_copy(m,cur,saved);
            *count[major]+=o->primary_down?~0u:1u;bound[major]+=o->primary_down?0u-cell:cell;direction=primary_code;
        } else {
            uint32_t edge=bound[major],target=R32(end+4u*major);
            if(o->primary_down?fx_s32(target)>=fx_s32(edge):fx_s32(target)<=fx_s32(edge)) { reached=1;segment_copy(m,cur,end); }
            else if(first) {
                uint32_t t=edge-R32(prev+4u*major);W32(cur+4u*major,edge);
                W32(cur+4u*minor,(o->inline_first?segment_imul(t,major_slope):segment_mul(t,major_slope))+R32(prev+4u*minor));
                W32(cur+8u,(o->inline_first?segment_imul(t,major_z):segment_mul(t,major_z))+R32(prev+8u));
                step[minor]=segment_mul(cell,major_slope);step_z=segment_mul(cell,major_z);
                if(o->primary_down) { step[minor]=0u-step[minor];step_z=0u-step_z; }
            } else {
                W32(cur+4u*major,edge);W32(cur+4u*minor,R32(prev+4u*minor)+step[minor]);W32(cur+8u,R32(prev+8u)+step_z);
            }
            uint32_t value=R32(cur+4u*minor),side=bound[minor];
            if(o->secondary_down?fx_s32(value)<fx_s32(side):fx_s32(value)>fx_s32(side)) {
                segment_copy(m,saved,cur);saved_flag=1;
                uint32_t t=side-R32(prev+4u*minor);W32(cur+4u*minor,side);
                W32(cur+4u*major,(o->inline_cross?segment_imul(t,minor_slope):segment_mul(t,minor_slope))+R32(prev+4u*major));
                W32(cur+8u,(o->inline_cross?segment_imul(t,minor_z):segment_mul(t,minor_z))+R32(prev+8u));
                direction=secondary_code;
                *count[minor]+=o->secondary_down?~0u:1u;bound[minor]+=o->secondary_down?0u-cell:cell;
            } else {
                direction=primary_code;
                *count[major]+=o->primary_down?~0u:1u;bound[major]+=o->primary_down?0u-cell:cell;
            }
        }
        done=reached==1&&!saved_flag;
        if(mode) {
            if(first) {
                a[0]=map;a[1]=vec;a[2]=prev;GC(0x46ff90,a,3);
                if((uint8_t)r.eax) {
                    if(out) { r.eax=R32(prev+8u);W32(out+8u,r.eax);r.edx=R32(prev);W32(out,r.edx);W32(out+4u,R32(prev+4u)); }
                    return pair(segment_low(r.eax),r.edx);
                }
            }
            if(done) {
                a[0]=map;a[1]=vec;a[2]=cur;GC(0x46ff90,a,3);
                if(!(uint8_t)r.eax)return pair(0,r.edx);
                if(out) { a[0]=map;a[1]=prev;a[2]=cur;a[3]=px;a[4]=py;a[5]=out;GC(0x46fdb8,a,6); }
                return pair(segment_low(r.eax),r.edx);
            }
            a[0]=map;a[1]=px;a[2]=py;a[3]=vec;a[4]=direction;GC(0x470080,a,5);
            if(r.eax) {
                if(r.eax==2) { if(out) { a[0]=map;a[1]=prev;a[2]=cur;a[3]=px;a[4]=py;a[5]=out;GC(0x46fdb8,a,6); } }
                else if(out) { r.edx=R32(cur+8u);W32(out+8u,r.edx);W32(out,R32(cur));r.eax=R32(cur+4u);W32(out+4u,r.eax); }
                return pair(segment_low(r.eax),r.edx);
            }
        } else {
            unsigned test;
            if(done) {
                a[0]=map;a[1]=cur;GC(0x46e180,a,2);
                if(fx_s32(r.eax)>=fx_s32(R32(cur+8u)))test=1;
                else if(first) { a[0]=map;a[1]=prev;GC(0x46e180,a,2);test=fx_s32(r.eax)>=fx_s32(R32(prev+8u)); }
                else return pair(0,r.edx);
            } else {
                a[0]=map;a[1]=px;a[2]=py;a[3]=cur;a[4]=direction;GC(0x470468,a,5);test=(uint8_t)r.eax!=0;
            }
            if(test) {
                a[0]=map;a[1]=prev;a[2]=cur;a[3]=radius;a[4]=px;a[5]=py;GC(0x470798,a,6);
                uint32_t p=r.eax;
                if(p) {
                    /* The two original copies leave different registers. */
                    if(out&&done) { r.edx=R32(p+8u);W32(out+8u,r.edx);W32(out,R32(p));r.eax=R32(p+4u);W32(out+4u,r.eax); }
                    else if(out) { r.edx=R32(p+8u);W32(out+8u,r.edx);W32(out,R32(p));r.edx=R32(p+4u);W32(out+4u,r.edx); }
                    return pair(segment_low(r.eax),r.edx);
                }
            }
            if(done)return pair(0,r.edx);
        }
        r.edx=R32(cur+8u);loop_edx=r.edx;segment_copy(m,prev,cur);px=cx;py=cy;first=0;
    }
}

/* 428048: terrain contact of a swept segment S (matrix S+0, length S+4,
 * radius word S+8). Out receives {distance, 4aaad4}; the contact point goes
 * to 4aaaee. EAX is 0/1; without contact EDX is the walk's EDX. */
uint64_t es2_sweep_terrain_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t out,uint32_t s,const uint32_t entry[3]) {
    uint32_t b=g->image,f=g->scratch+0x30u,a[6];
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    if(!R8(b+0x9ab6fu))W8(b+0x9ab6fu,1);
    uint32_t length=R32(s+4u);W32(f-0x24u,0);W32(f-0x20u,length);W32(f-0x1cu,0);
    uint32_t matrix=R32(s);a[0]=f-0x24u;a[1]=matrix;a[2]=f-0x18u;GC(0x48043c,a,3);
    a[0]=R32(b+0xa0be0u);a[1]=matrix+0x14u;a[2]=f-0x18u;a[3]=(uint32_t)fx_s16(R16(s+8u));a[4]=f-0xcu;a[5]=0;GC(0x46e980,a,6);
    if(!(uint8_t)r.eax)return pair(0,r.edx);
    a[0]=matrix+0x14u;a[1]=f-0xcu;GC(0x49288c,a,2);
    W32(out,r.eax);W32(out+4u,b+0xaaad4u);W32(b+0xaaaf6u,R32(f-4u));
    uint32_t x=R32(f-0xcu);W32(b+0xaaaeeu,x);W32(b+0xaaaf2u,R32(f-8u));
    return pair(1,x);
}

/* Cell (x, y) of the terrain grid: row-major index (y << columns) + x, valid
 * for 0 <= index < 1 << (columns + rows) (signed, low five shift bits);
 * outside the result is 0. */
static uint32_t terrain_cell(const es2_guest_memory *m,uint32_t map,uint32_t x,uint32_t y) {
    uint32_t index=(y<<(R32(map+0x100u)&31u))+x;
    if(fx_s32(index)<0||fx_s32(index)>=fx_s32(1u<<((R32(map+0x100u)+R32(map+0x104u))&31u)))return 0;
    return R32(map+0xecu)+16u*index;
}
static uint32_t terrain_height(const es2_guest_memory *m,uint32_t map,uint32_t cell) {
    return (uint32_t)R8(cell)*R32(map+0x118u)+R32(map+0x110u);
}

/* 46ff4c: is the slope of the triangle normal n (three words) steep, seen
 * along the direction words? Below z 60eh yes (AL = 1 over the entry EAX
 * high word and n.z's high byte), above no, at exactly 60eh the 47dce7 dot
 * product decides against -7a1200h. */
uint64_t es2_slope_test_registers(const es2_guest_memory *m,uint32_t vector,uint32_t normal,const uint32_t entry[3]) {
    uint32_t z=R16(normal+4u);
    if(fx_s16(z)<0x60e)return pair((entry[0]&0xffff0000u)|(z&0xff00u)|1u,entry[1]);
    if(fx_s16(z)>0x60e)return pair(0,entry[1]);
    uint32_t dot=(uint32_t)fx_s16(R16(normal))*(uint32_t)fx_s16(R16(vector))+
        (uint32_t)fx_s16(R16(normal+2u))*(uint32_t)fx_s16(R16(vector+2u))+
        (uint32_t)fx_s16(R16(normal+4u))*(uint32_t)fx_s16(R16(vector+4u));
    uint32_t below=fx_s32(dot)<-0x7a1200;
    return pair(below,below);
}

/* 46c23c: scale the three direction words to length `length`. While the
 * 47de72 length exceeds 7d00h all words are halved (Z, X, Y); then each word
 * becomes 47e08d(word, (length << 16) / length2). The caller guarantees a
 * nonzero vector (the original divides by its length). */
uint64_t es2_normalize_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t v,uint32_t length,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[3];
    for(;;) {
        a[0]=(uint32_t)fx_s16(R16(v));a[1]=(uint32_t)fx_s16(R16(v+2u));a[2]=(uint32_t)fx_s16(R16(v+4u));
        r.eax=a[0];r.edx=a[0];GC(0x47de72,a,3);
        if(fx_s16(r.eax)<=0x7d00)break;
        W16(v+4u,fx_sar((uint32_t)fx_s16(R16(v+4u)),1));W16(v,fx_sar((uint32_t)fx_s16(R16(v)),1));
        W16(v+2u,fx_sar((uint32_t)fx_s16(R16(v+2u)),1));
    }
    a[0]=(uint32_t)fx_s16(R16(v));a[1]=(uint32_t)fx_s16(R16(v+2u));a[2]=(uint32_t)fx_s16(R16(v+4u));
    r.eax=a[0];r.edx=a[0];GC(0x47de72,a,3);
    int64_t q=(int64_t)((int32_t)((uint32_t)fx_s16(length)<<16))/fx_s16(r.eax);
    uint32_t edx=0;
    for(unsigned i=0;i<3;++i) {
        int64_t product=fx_s16(R16(v+2u*i))*q;
        W16(v+2u*i,(uint32_t)((uint64_t)product>>16));edx=(uint32_t)((uint64_t)product>>32);
    }
    return pair(v,edx);
}

/* 46fdb8: point where the segment prev..cur crosses the cell's diagonal,
 * by eight bisections in cell coordinates (X mirrored when flags & 3 is 0).
 * Without a crossing out = prev. The cell must exist (the original reads
 * its flags through a null pointer otherwise). */
uint64_t es2_diagonal_registers(const es2_guest_memory *m,uint32_t map,uint32_t prev,uint32_t cur,
    uint32_t column,uint32_t row,uint32_t out) {
    uint32_t cell=terrain_cell(m,map,column,row),s=R32(map+0x108u)&31u,size=1u<<s;
    int mirror=(R8(cell+0xfu)&3u)==0;
    uint32_t ox=column<<s,oy=row<<s;
    uint32_t cx=R32(cur)-ox,cy=R32(cur+4u)-oy,px=R32(prev)-ox,py=R32(prev+4u)-oy;
    if(mirror) { cx=size-cx;px=size-px; }
    int side=fx_s32(px)>fx_s32(py);
    if((fx_s32(cx)>fx_s32(cy))==side) {
        W32(out+8u,R32(prev+8u));W32(out,R32(prev));uint32_t y=R32(prev+4u);W32(out+4u,y);
        return pair(y,out);
    }
    uint32_t mx=0,my=0;
    for(unsigned i=0;i<8;++i) {
        mx=fx_sar(cx+px,1);my=fx_sar(cy+py,1);
        if(my==mx)break;
        if((fx_s32(my)<fx_s32(mx))==side) { px=mx;py=my; } else { cx=mx;cy=my; }
    }
    if(mirror)mx=size-mx;
    mx+=ox;my+=oy;
    W32(out,mx);W32(out+4u,my);W32(out+8u,0);
    return pair(0,my);
}

/* 470468: does the point lie below the terrain edge it is crossing? The
 * direction code picks the two corner cells of that edge (0: left, 1: right,
 * 2: top, 3: bottom); a missing cell or another code gives 0. At or below
 * the lower corner AL = 1 (EDX = the frame address of that height, dead at
 * the caller), above the higher 0, else the linear height along the edge
 * decides. `frame` is scratch holding the two heights at EBP-38h/-3ch. */
uint64_t es2_edge_test_registers(const es2_guest_memory *m,uint32_t frame,uint32_t map,uint32_t column,uint32_t row,
    uint32_t point,uint32_t direction) {
    uint32_t a=0,b=0;
    switch(direction) {
    case 0:a=terrain_cell(m,map,column,row);if(!a)return pair(0,0);b=terrain_cell(m,map,column,row+1u);break;
    case 1:a=terrain_cell(m,map,column+1u,row);if(!a)return pair(0,0);b=terrain_cell(m,map,column+1u,row+1u);break;
    case 2:a=terrain_cell(m,map,column,row+1u);if(!a)return pair(0,0);b=terrain_cell(m,map,column+1u,row+1u);break;
    case 3:a=terrain_cell(m,map,column,row);if(!a)return pair(0,0);b=terrain_cell(m,map,column+1u,row);break;
    default:return pair(0,0);
    }
    if(!b)return pair(0,a);
    uint32_t ha=terrain_height(m,map,a),hb=terrain_height(m,map,b),ebp=frame+0x40u;
    W32(ebp-0x38u,ha);W32(ebp-0x3cu,hb);
    uint32_t low=fx_s32(ha)<fx_s32(hb)?ebp-0x38u:ebp-0x3cu,high=low==ebp-0x38u?ebp-0x3cu:ebp-0x38u;
    if(fx_s32(R32(point+8u))<=fx_s32(R32(low)))return pair((map&0xffffff00u)|1u,low);
    uint32_t z=R32(point+8u);
    if(fx_s32(z)>fx_s32(R32(high)))return pair(0,z);
    uint32_t s=R32(map+0x108u),t=direction<2?R32(point+4u)-(row<<(s&31u)):R32(point)-(column<<(s&31u));
    uint32_t h=fx_sar((hb-ha)*t,s)+ha;
    uint32_t above=fx_s32(h)>=fx_s32(R32(point+8u));
    return pair(above,above);
}

/* 46ff90: slope test at a point: the triangle of its cell that contains the
 * point (flags 1: first, flags 0/2: split by the diagonal), checked by
 * 46ff4c. Outside the grid AL = 1. Flags 3 has no case in the original,
 * which then passes its uninitialized local -18h; the entry shim captures
 * that dword into `residue`. */
uint64_t es2_face_test_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t map,uint32_t vector,
    uint32_t point,uint32_t residue,const uint32_t entry[3]) {
    uint32_t s=R32(map+0x108u)&31u,mask=(1u<<s)-1u;
    uint32_t cell=terrain_cell(m,map,fx_sar(R32(point),s),fx_sar(R32(point+4u),s));
    (void)entry;
    if(!cell)return pair((map&0xffffff00u)|1u,0);
    uint32_t fx=R32(point)&mask,fy=R32(point+4u)&mask,size=1u<<s,normal;
    switch(R8(cell+0xfu)&3u) {
    case 0:normal=fx_s32(size)<fx_s32(fy+fx)?cell+7u:cell+1u;break;
    case 1:normal=cell+1u;break;
    case 2:normal=fx_s32(fy)<=fx_s32(fx)?cell+7u:cell+1u;break;
    default:normal=residue;break;
    }
    es2_weapon_registers r={map,vector,normal};uint32_t a[3]={map,vector,normal};
    GC(0x46ff4c,a,3);
    return pair(r.eax,r.edx);
}

/* 470080: slope test across the cell edge being crossed: the triangle on
 * this side, then the one on the far side (direction 0: left, 1: right,
 * 2: up, 3: down; which triangle follows the flags for 2 and 3). A missing
 * cell gives 1, a steep near triangle 2, else the far test's AL. */
uint64_t es2_face_cross_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t map,uint32_t column,
    uint32_t row,uint32_t vector,uint32_t direction,const uint32_t entry[3]) {
    static const int8_t dx[4]={-1,1,0,0},dy[4]={0,0,1,-1};
    if(direction>3u)return pair(direction-3u,entry[1]);
    uint32_t cell=terrain_cell(m,map,column,row);
    if(!cell)return pair(1,0);
    uint32_t near,far;
    switch(direction) {
    case 0:near=cell+1u;break;
    case 1:near=cell+7u;break;
    case 2:near=R8(cell+0xfu)&3u?cell+1u:cell+7u;break;
    default:near=R8(cell+0xfu)&3u?cell+7u:cell+1u;break;
    }
    es2_weapon_registers r={vector,cell,near};uint32_t a[3]={map,vector,near};
    if(direction>=2) { r.eax=near;r.edx=near;r.ecx=vector; }
    GC(0x46ff4c,a,3);
    if((uint8_t)r.eax)return pair(2,r.edx);
    uint32_t other=terrain_cell(m,map,column+(uint32_t)(int32_t)dx[direction],row+(uint32_t)(int32_t)dy[direction]);
    if(!other)return pair(1,0);
    switch(direction) {
    case 0:far=other+7u;break;
    case 1:far=other+1u;break;
    case 2:far=R8(other+0xfu)&3u?other+7u:other+1u;break;
    default:far=R8(other+0xfu)&3u?other+1u:other+7u;break;
    }
    r.eax=vector;r.edx=other;r.ecx=far;
    if(direction>=2) { r.eax=far;r.edx=far;r.ecx=vector; }
    a[2]=far;GC(0x46ff4c,a,3);
    return pair(r.eax&0xffu,r.edx);
}

/* 470798: contact point of prev..cur with the cell's two triangle planes
 * (normal words at cell+1 and cell+7, constant -height*n.z) through 47e610
 * into 6b505c (cell coordinates, then shifted by the cell origin). The
 * radius slot is not read. Flags 2 keeps the first plane's constant for the
 * second triangle; flags 0 rebases the second test on the next column's
 * height. `frame` is 64 bytes of scratch for the plane and segment. */
uint64_t es2_contact_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t frame,uint32_t map,
    uint32_t prev,uint32_t cur,uint32_t column,uint32_t row,const uint32_t entry[3]) {
    uint32_t b=g->image,f=frame+0x40u,hit=b+0x2b505cu,a[3];
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    if(!R8(b+0xa0c4cu))W8(b+0xa0c4cu,1);
    uint32_t cell=terrain_cell(m,map,column,row),s=R32(map+0x108u)&31u,size=1u<<s;
    uint32_t ox=column<<s,oy=row<<s,origin_x=ox;
    if(!cell)return pair(0,0);
    W32(f-0x28u,(uint32_t)fx_s16(R16(cell+1u)));W32(f-0x24u,(uint32_t)fx_s16(R16(cell+3u)));
    uint32_t nz=(uint32_t)fx_s16(R16(cell+5u));W32(f-0x20u,nz);
    W32(f-0x1cu,(0u-terrain_height(m,map,cell))*nz);
    W32(f-0x38u,R32(prev+8u));W32(f-0x40u,R32(prev)-ox);W32(f-0x3cu,R32(prev+4u)-oy);
    W32(f-0x2cu,R32(cur+8u));W32(f-0x34u,R32(cur)-ox);W32(f-0x30u,R32(cur+4u)-oy);
    a[0]=f-0x28u;a[1]=f-0x40u;a[2]=hit;r.eax=oy;r.ecx=f-0x28u;r.edx=f-0x40u;GC(0x47e610,a,3);
    uint32_t flags=R8(cell+0xfu)&3u;
    if((uint8_t)r.eax) {
        uint32_t x=R32(hit),y=R32(hit+4u);int accept=0;uint32_t edx=ox;
        if(flags==1)accept=1;
        else if(flags==2) { edx=x;accept=fx_s32(x)<fx_s32(y); }
        else if(flags==0) { edx=x+y;accept=fx_s32(size)>=fx_s32(x+y); }
        if(accept) { W32(hit,R32(hit)+ox);W32(hit+4u,R32(hit+4u)+oy);return pair(hit,edx); }
    }
    if(flags==1)return pair(0,0);
    W32(f-0x28u,(uint32_t)fx_s16(R16(cell+7u)));W32(f-0x24u,(uint32_t)fx_s16(R16(cell+9u)));W32(f-0x20u,(uint32_t)fx_s16(R16(cell+0xbu)));
    r.ecx=R32(f-0x24u);
    if(!flags) {
        uint32_t next=terrain_cell(m,map,column+1u,row);
        if(!next)return pair(0,0);
        r.ecx=(0u-terrain_height(m,map,next))*R32(f-0x20u);W32(f-0x1cu,r.ecx);
        W32(f-0x40u,R32(f-0x40u)-size);W32(f-0x34u,R32(f-0x34u)-size);origin_x+=size;
    }
    a[0]=f-0x28u;a[1]=f-0x40u;a[2]=hit;r.edx=f-0x28u;r.eax=f-0x40u;GC(0x47e610,a,3);
    if(!(uint8_t)r.eax)return pair(0,r.edx);
    W32(hit,R32(hit)+origin_x);W32(hit+4u,R32(hit+4u)+oy);
    return pair(hit,r.edx);
}

/* 4280f4: terrain contact of start..end (mode-0 walk with radius word);
 * out = distance to the contact. AX = 1 over the distance's high word. */
uint64_t es2_terrain_line_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t out,uint32_t start,
    uint32_t end,uint32_t radius,const uint32_t entry[3]) {
    uint32_t b=g->image,f=g->scratch+0x10u,a[6];
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    a[0]=R32(b+0xa0be0u);a[1]=start;a[2]=end;a[3]=(uint32_t)fx_s16(radius);a[4]=f-0xcu;a[5]=0;
    r.eax=a[0];r.ecx=end;r.edx=a[3];GC(0x46e980,a,6);
    if(!(uint8_t)r.eax)return pair(0,r.edx);
    a[0]=start;a[1]=f-0xcu;r.edx=f-0xcu;GC(0x49288c,a,2);
    W32(out,r.eax);
    return pair((r.eax&0xffff0000u)|1u,r.edx);
}

/* Original: DBSIM.EXE 0x40b804 (weapons.md, "beam constructor").
 * BEAM constructor: base object_init(O, 0), the two vtables and fields,
 * then the section array from the runtime's array helper 49667c (called
 * through the graph like every callee): types 1 and 7 sample F..T in steps
 * of 400h with rng_next jitter, other types store F and T. Guest reads and
 * writes keep the original's order (O+54h, O+58h, the table pointer and
 * the runtime counter pointer are reread where the original rereads them).
 * Frame: point at scratch+0, delta at +0Ch, cur at +18h. */
uint64_t es2_beam_create_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t o,
    uint32_t type,uint32_t from,uint32_t to,uint32_t owner,const uint32_t entry[3]) {
    uint32_t b=g->image,s=g->scratch,delta=s+0x0cu,cur=s+0x18u,a[5],counter,edx;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    int32_t t=fx_s16(type);
    a[0]=o;a[1]=0;GC(0x402188,a,2);
    W32(o,b+0x987a0u);W16(o+4u,3);W8(o+0x41u,type);W32(o+0x4eu,0);
    counter=R32(b+0x2cbee4u);W32(counter,R32(counter)+1u);
    W32(o,b+0x987c4u);W16(o+0x52u,type);W32(o+0x4au,owner);
    if(t==1||t==7) {
        r.eax=to;r.edx=from;r.ecx=0;a[0]=from;a[1]=to;GC(0x49288c,a,2);
        uint32_t e=fx_sar(r.eax,10)+1u;uint8_t n=(uint8_t)e;W8(o+0x54u,n);
        uint16_t c=(uint16_t)(2u*(uint32_t)signed_byte(n)+2u);W16(o+0x56u,c);
        r.eax=(uint32_t)fx_s16(c);r.edx=e;
        a[0]=0;a[1]=12;a[2]=r.eax;a[3]=1;a[4]=b+0x953c4u;GC(0x49667c,a,5);
        W32(o+0x58u,r.eax);W8(o+0x55u,1);
        W32(cur,R32(from));W32(cur+4u,R32(from+4u));W32(cur+8u,R32(from+8u));
        W32(delta,R32(to));W32(delta+4u,R32(to+4u));W32(delta+8u,R32(to+8u));
        W32(delta+8u,R32(delta+8u)-R32(from+8u));
        r.ecx=R32(from);W32(delta,R32(delta)-r.ecx);
        r.eax=R32(from+4u);W32(delta+4u,R32(delta+4u)-r.eax);
        r.edx=delta;a[0]=delta;a[1]=0x400;GC(0x4927f0,a,2);
        for(uint16_t i=0;;++i) {
            int32_t last=signed_byte(R8(o+0x54u));
            if(!(fx_s16(i)<last+1))break;
            uint32_t p=fx_s16(i)==last?to:cur;
            uint32_t x=R32(p),y=R32(p+4u),z=R32(p+8u);
            uint32_t j=(uint16_t)(2u*i);
            if(i) {
                a[0]=b+0xd2599u;r.eax=z;r.edx=y;r.ecx=2u*((p&0xffff0000u)|i);GC(0x492ee0,a,1);
                r.eax&=0x7fu;r.ecx=b+0xd2599u;x+=r.eax;
                GC(0x492ee0,a,1);
                r.eax&=0xffff007fu;r.edx=r.eax&0x7fu;r.ecx=b+0xd2599u;y+=r.edx;
                GC(0x492ee0,a,1);
                r.eax&=0xffff007fu;r.ecx=r.eax&0x7fu;z+=r.ecx;
            }
            uint32_t next=R32(o+0x58u)+12u*j+12u;
            W32(next+8u,z);W32(next,x);W32(next+4u,y);
            uint32_t here=12u*j+R32(o+0x58u);
            W32(here+8u,R32(next+8u));W32(here,R32(next));W32(here+4u,R32(next+4u));
            uint32_t lift=(uint32_t)fx_s16(R16(R32(b+0xa9804u)+6u*(uint32_t)t));
            here=R32(o+0x58u)+12u*j;W32(here+8u,R32(here+8u)+lift);
            W32(cur+8u,R32(cur+8u)+R32(delta+8u));W32(cur,R32(cur)+R32(delta));W32(cur+4u,R32(cur+4u)+R32(delta+4u));
        }
    } else {
        W8(o+0x54u,1);W16(o+0x56u,2);
        r.eax=2;r.edx=owner;r.ecx=(uint32_t)(t-7);
        a[0]=0;a[1]=12;a[2]=2;a[3]=1;a[4]=b+0x953c4u;GC(0x49667c,a,5);
        uint32_t array=r.eax;W32(o+0x58u,array);
        W32(array+8u,R32(from+8u));W32(array,R32(from));W32(array+4u,R32(from+4u));
        uint32_t second=R32(o+0x58u)+12u;
        W32(second+8u,R32(to+8u));W32(second,R32(to));W32(second+4u,R32(to+4u));
        W8(o+0x55u,0);
    }
    W16(o+0x5du,0x38);
    W32(o+0x2eu,R32(from+8u));W32(o+0x26u,R32(from));edx=R32(from+4u);W32(o+0x2au,edx);
    counter=R32(b+0x2cbee4u);W32(counter,R32(counter)+1u);
    return pair(o,edx);
}

/* Bullet constructor (weapons.md, "bullet constructor"). Direct calls name the
 * original VA (observer key) and reach the bridge at this image's address; the
 * virtual +38h passes its resolved target and the slot. r holds what the
 * original's EAX/EDX/ECX hold at the next game call; word() is a 16-bit load or
 * 16-bit arithmetic that keeps the high word. */
#define BC_VA(va) (g->image+(uint32_t)(va)-0x400000u)
static void bc_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,BC_VA(va),0);
    g->call(g->context,BC_VA(va),a,n,r);
}
static void bc_virtual(const es2_weapon_graph *g,uint32_t target,unsigned slot_id,const uint32_t *a,unsigned n,
    es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot_id);
    g->call(g->context,target,a,n,r);
}
/* EBX/ESI/EDI of the original at its next call and the address after that
 * CALL instruction. */
static void bc_frame(es2_weapon_frame *f,const es2_weapon_graph *g,uint32_t ebx,uint32_t esi,uint32_t edi,uint32_t ret) {
    if(f) { f->ebx=ebx;f->esi=esi;f->edi=edi;f->ret=BC_VA(ret); }
}
static uint32_t bc_s16(uint32_t x) { return (uint32_t)fx_s16(x); }

/* Original: DBSIM.EXE 0x40ffc8. Walks the [4a98fch] records (stride 24h)
 * while s16(EDX) < s16 of the count word [4a9900h] (reread per test) for
 * the first one whose words +0/+2 equal the low words of category/type.
 * EAX = the record or 0, EDX = its index or the final count. */
uint64_t es2_def_lookup_registers(const es2_guest_memory *m,uint32_t image,uint32_t category,uint32_t type) {
    uint32_t eax=R32(image+0xa98fcu),edx=0;
    while(fx_s16(edx)<fx_s16(R16(image+0xa9900u))) {
        if((uint16_t)category==R16(eax)&&(uint16_t)type==R16(eax+2u))return pair(eax,edx);
        ++edx;eax+=0x24u;
    }
    return pair(0,edx);
}

/* Original: DBSIM.EXE 0x40af6c. object_init(O, shape of the bullet
 * definition), the two vtables, the definition record of category 2 at
 * O+4Eh and the word O+59h from bullet_def. The runtime counter pointer is
 * reread at each use. */
uint64_t es2_bullet_init_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t o,uint32_t type,
    const uint32_t entry[3]) {
    uint32_t a[2];
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    r.ecx=R32(BC_VA(0x4a96f8u));
    r.eax=bc_s16(type);r.edx=(r.eax<<3)-r.eax;
    r.eax=bc_s16(R16(r.ecx+2u*r.edx));
    r.edx=R32(BC_VA(0x4a9700u));
    r.eax=R32(r.edx+4u*r.eax);
    a[0]=o;a[1]=r.eax;bc_call(g,0x402188u,a,2,&r);
    W32(o,BC_VA(0x4987a0u));r.edx=type;W16(o+4u,3);r.ecx=0;W8(o+0x41u,r.edx);W32(o+0x4eu,r.ecx);
    r.eax=R32(BC_VA(0x6cbee4u));W32(r.eax,R32(r.eax)+1u);
    W32(o,BC_VA(0x498628u));
    a[0]=2;a[1]=type;bc_call(g,0x40ffc8u,a,2,&r);
    W32(o+0x4eu,r.eax);
    a[0]=o;bc_call(g,0x40adc0u,a,1,&r);
    r.eax=word(r.eax,R16(r.eax+6u));W16(o+0x59u,r.eax);
    r.edx=R32(BC_VA(0x6cbee4u));W32(r.edx,R32(r.edx)+1u);
    return pair(o,r.edx);
}

/* Original: DBSIM.EXE 0x40b43c. Frame f: the angles copy f-2Ch/-2Ah/-28h
 * (words), the runtime record f-24h (context word f-14h), the object f-4.
 * EBX is the caller's until the object is known, ESI until the definition
 * D (bullet_def) is known, EDI until the first spread draw. */
uint64_t es2_bullet_create_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_weapon_frame *frame,
    uint32_t ebx,uint32_t esi,uint32_t edi,uint32_t type,uint32_t position,uint32_t angles,uint32_t owner,
    const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_BULLET_CREATE_SCRATCH;
    uint32_t a[3];
    es2_weapon_registers r={BC_VA(0x498549u),entry[1],entry[2]};
    bc_frame(frame,g,ebx,esi,edi,0x40b44fu);bc_call(g,0x49630cu,NULL,0,&r);
    r.edx=R32(BC_VA(0x4a96c2u));a[0]=r.edx;
    bc_frame(frame,g,ebx,esi,edi,0x40b45bu);bc_call(g,0x471b30u,a,1,&r);
    r.ecx=a[0];
    ebx=r.eax;W32(f-4u,ebx);
    if(ebx) {
        W16(f-0x14u,0x14u);
        r.eax=word(r.eax,type);a[1]=r.eax;r.edx=R32(f-4u);a[0]=r.edx;
        bc_frame(frame,g,ebx,esi,edi,0x40b479u);bc_call(g,0x40af6cu,a,2,&r);
        r.ecx=R32(BC_VA(0x6cbee4u));W32(r.ecx,R32(r.ecx)-5u);
        W16(f-0x14u,8u);
    }
    /* A null object faults in bullet_def below (precondition). */
    r.eax=R32(f-4u);ebx=r.eax;
    const uint32_t o=ebx;
    a[0]=o;
    bc_frame(frame,g,ebx,esi,edi,0x40b49bu);bc_call(g,0x40adc0u,a,1,&r);
    esi=r.eax;r.eax=angles;r.ecx=a[0];
    r.edx=word(r.edx,R16(r.eax));W16(f-0x2cu,r.edx);
    r.ecx=word(r.ecx,R16(r.eax+2u));W16(f-0x2au,r.ecx);
    r.eax=word(r.eax,R16(r.eax+4u));W16(f-0x28u,r.eax);
    /* The spread: two stream A draws (rng_next 4d2599h), each masked with
     * 2w and centred by -w, w = word [D+0Ah] reread at every use. */
    edi=word(edi,R16(esi+0xau));a[0]=BC_VA(0x4d2599u);edi+=edi;
    bc_frame(frame,g,ebx,esi,edi,0x40b4c8u);bc_call(g,0x492ee0u,a,1,&r);
    edi=word(edi,edi&r.eax);r.ecx=a[0];edi=word(edi,edi-R16(esi+0xau));W16(f-0x2cu,R16(f-0x2cu)+edi);
    edi=word(edi,R16(esi+0xau));a[0]=BC_VA(0x4d2599u);edi+=edi;
    bc_frame(frame,g,ebx,esi,edi,0x40b4e4u);bc_call(g,0x492ee0u,a,1,&r);
    edi=word(edi,edi&r.eax);r.ecx=a[0];edi=word(edi,edi-R16(esi+0xau));W16(f-0x28u,R16(f-0x28u)+edi);
    /* Position, angles, owner; the memset of O+42h. */
    r.eax=o+0x26u;r.edx=position;
    r.ecx=R32(r.edx+8u);W32(r.eax+8u,r.ecx);
    r.ecx=R32(r.edx);W32(r.eax,r.ecx);
    r.edx=R32(r.edx+4u);W32(r.eax+4u,r.edx);
    r.eax=o+0xcu;r.edx=R32(f-0x2cu);W32(r.eax,r.edx);
    r.edx=word(r.edx,R16(f-0x28u));W16(r.eax+4u,r.edx);
    W16(r.eax+0x26u,0);
    r.eax=owner;W32(o+0x4au,r.eax);
    r.eax=o+0x42u;a[0]=r.eax;a[1]=0;a[2]=8;
    bc_frame(frame,g,ebx,esi,edi,0x40b52fu);bc_call(g,0x49681au,a,3,&r);
    r.edx=0;W16(o+0x56u,r.edx);W16(o+0x54u,r.edx);
    /* The owner's virtual +38h; its AX plus the definition word +0Ah. */
    r.eax=owner;a[0]=r.eax;r.ecx=R32(r.eax);
    bc_frame(frame,g,ebx,esi,edi,0x40b545u);bc_virtual(g,R32(r.ecx+0x38u),0x38,a,1,&r);
    r.ecx=a[0];
    r.edx=R32(o+0x4eu);r.eax=word(r.eax,r.eax+R16(r.edx+0xau));W16(o+0x52u,r.eax);
    r.eax=0;W32(o+0x5bu,r.eax);W16(o+0x5fu,0x400u);
    if(R16(esi+0xcu)) {
        r.ecx=bc_s16(R16(esi+2u));
        const int32_t d=(int32_t)r.ecx;
        r.eax=(uint32_t)(0x400/d);r.edx=(uint32_t)(0x400%d);
        W16(o+0x61u,r.eax);
    }
    r.ecx=position;a[1]=r.ecx;r.eax=0xau;r.edx=bc_s16(R16(esi+8u));r.eax+=r.edx;a[0]=r.eax;
    bc_frame(frame,g,ebx,esi,edi,0x40b588u);bc_call(g,0x4628e0u,a,2,&r);
    *ecx=r.ecx;
    r.edx=R32(f-0x24u);
    return pair(o,r.edx);
}
#undef BC_VA

/* Weapon and projectile helpers (weapons.md, "weapon and projectile helpers").
 * Direct calls name the original VA (observer key) and reach the bridge at this
 * image's address. */
#define WL_VA(va) (g->image+(uint32_t)(va)-0x400000u)
static void wl_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,WL_VA(va),0);
    g->call(g->context,WL_VA(va),a,n,r);
}

/* Original: DBSIM.EXE 0x40ffb0. EAX = [4a98fch] + 24h * s16(i); EDX and
 * ECX untouched (EDX returned from the entry registers). */
uint64_t es2_proj_record_registers(const es2_guest_memory *m,uint32_t image,uint32_t index,const uint32_t entry[3]) {
    uint32_t eax=(uint32_t)fx_s16(index)<<2;
    eax+=8u*eax;eax+=R32(image+0xa98fcu);
    return pair(eax,entry[1]);
}

/* Original: DBSIM.EXE 0x40501c. Word [4a95f2h] = lo(x) through AX, then word
 * [4a95f4h] = lo(y) through DX; the high words of EAX/EDX stay the entry
 * registers', ECX untouched. */
uint64_t es2_damage_words_registers(const es2_guest_memory *m,uint32_t image,uint32_t x,uint32_t y,const uint32_t entry[3]) {
    uint32_t eax=word(entry[0],x);W16(image+0xa95f2u,eax);
    uint32_t edx=word(entry[1],y);W16(image+0xa95f4u,edx);
    return pair(eax,edx);
}

/* Original: DBSIM.EXE 0x49295c. dist2d_delta(dx, dy, dz) with EAX = dz,
 * EDX = dy, ECX = dx (the dz slot stays pushed), then vector_angle(its EAX,
 * dz) with dist2d_delta's registers; vector_angle's EAX/EDX/ECX return. */
uint64_t es2_pitch_point_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t p,uint32_t q,
    const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    r.eax=R32(p+8u)-R32(q+8u);r.edx=R32(p+4u)-R32(q+4u);r.ecx=R32(p)-R32(q);
    const uint32_t delta[3]={r.ecx,r.edx,r.eax};
    wl_call(g,0x492a9au,delta,3,&r);
    const uint32_t angle[2]={r.eax,delta[2]};
    wl_call(g,0x49290cu,angle,2,&r);
    if(ecx)*ecx=r.ecx;
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x40b5a0. bullet_create(t, P, A, W) with EAX = A,
 * EDX = P, ECX = the entry ECX; CX = lo(c), word [O+56h] = c; for type
 * word 9 also EDX = [W+1A4h] and dword [O+5Bh] = EDX. A null O faults in
 * the original (precondition). */
uint64_t es2_bullet_charge_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t type,uint32_t position,
    uint32_t angles,uint32_t owner,uint32_t charge,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={angles,position,entry[2]};
    const uint32_t a[4]={type,position,angles,owner};
    wl_call(g,0x40b43cu,a,4,&r);
    r.ecx=word(r.ecx,charge);
    const uint32_t o=r.eax;W16(o+0x56u,r.ecx);
    if((uint16_t)type==9u) { r.edx=R32(owner+0x1a4u);W32(o+0x5bu,r.edx); }
    if(ecx)*ecx=r.ecx;
    return pair(o,r.edx);
}

/* Original: DBSIM.EXE 0x40a948. object_init(O, shape of projectile
 * definition s16(n)) with the original's EAX/EDX/ECX, the two vtables, the
 * category-0 lookup record at O+4Eh; the counter pointer is reread at each
 * use. */
uint64_t es2_rocket_init_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t o,uint32_t type,
    const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    r.ecx=R32(WL_VA(0x4a96d0u));
    r.eax=(uint32_t)fx_s16(type);r.edx=(r.eax<<3)-r.eax;
    r.eax=(uint32_t)fx_s16(R16(r.ecx+2u*r.edx));
    r.edx=R32(WL_VA(0x4a96d8u));
    r.eax=R32(r.edx+4u*r.eax);
    const uint32_t init[2]={o,r.eax};wl_call(g,0x402188u,init,2,&r);
    W32(o,WL_VA(0x4987a0u));r.edx=type;W16(o+4u,3);W8(o+0x41u,r.edx);r.ecx=0;W32(o+0x4eu,r.ecx);
    r.eax=R32(WL_VA(0x6cbee4u));W32(r.eax,R32(r.eax)+1u);
    r.edx=0;W32(o,WL_VA(0x498448u));W32(o+0x56u,r.edx);
    const uint32_t lookup[2]={0,type};wl_call(g,0x40ffc8u,lookup,2,&r);
    W32(o+0x4eu,r.eax);
    r.ecx=R32(WL_VA(0x6cbee4u));r.eax=o;W32(r.ecx,R32(r.ecx)+1u);
    if(ecx)*ecx=r.ecx;
    return pair(o,r.edx);
}
#undef WL_VA

/* (docs/reverse-engineering/weapons.md, section "missile
 * steering 40a488 and ally selection 423974"). */

/* Original: DBSIM.EXE 0x40a488 (O, R): the player-steered missile (type 3)
 * of projectile_flight with the control record R = 4d22c6. With the camera
 * byte R+0Dh set, the words O+0Ch/+0Eh/+10h are copied, the axis words
 * [R+22h] and [R+26h] (scaled by 500h in Q8 and by the time scale) bend
 * O+10h down and O+0Ch up, the pose is stored with its cache word O+32h
 * cleared, and R+0Dh and both axis words are cleared (EAX = 0, EDX =
 * [R+22h]). Without it the guidance stops (O+56h = 0, byte O+41h = 0) and
 * EDX:EAX are the entry pair. ECX at the return is an uninitialized frame
 * dword (low word O+10h on the first path), restored by the framework;
 * dead at the only caller (40a63c: movement_step's slot is popped into it
 * before any read). */
uint64_t es2_missile_steer_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t o,uint32_t R,
    const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[2];
    if(!R8(R+0xdu)) { r.ecx=0;W32(o+0x56u,0);W8(o+0x41u,0);return pair(r.eax,r.edx); }
    const uint32_t p=o+0xcu;
    uint16_t x=R16(p);r.edx=word(r.edx,x);
    const uint16_t y=R16(p+2u);r.ecx=word(r.ecx,y);
    uint16_t z=R16(p+4u);r.eax=word(p,z);
    r.edx=R32(R+0x22u);a[0]=0x500;a[1]=(uint32_t)fx_s16(R16(r.edx));GC(0x47e0a0,a,2);
    a[0]=r.eax;GC(0x467924,a,1);r.ecx=a[0];
    z=(uint16_t)(z-r.eax);
    r.eax=R32(R+0x26u);a[0]=0x500;a[1]=(uint32_t)fx_s16(R16(r.eax));GC(0x47e0a0,a,2);
    a[0]=r.eax;GC(0x467924,a,1);r.ecx=a[0];
    x=(uint16_t)(x+r.eax);
    r.edx=(uint32_t)x|(uint32_t)y<<16;r.ecx=0;W32(p,r.edx);
    r.edx=word(r.edx,z);W16(p+4u,z);W16(p+0x26u,0);W8(R+0xdu,0);
    r.eax=0;r.edx=R32(R+0x26u);W16(r.edx,0);r.edx=R32(R+0x22u);W16(r.edx,0);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x423974 (G, X): the member of group G other than X
 * nearest to X (dist_gate below 100,000, the first of equal distances),
 * skipping members whose bytes +0A4h or +99h are set; 0 if none. The
 * index is the 16-bit SI against the signed count word G+10h, re-read
 * every pass. EDX is the last pass's (the list pointer, the member's +92h,
 * or dist_gate's), the entry EDX without a pass. ECX (the last pass's
 * 0/1, A+26h or dist_gate's) is restored by the framework; dead at the
 * only caller (sweep 4267bd: it reaches only the high word of a word
 * slot of the target's virtual +50h). */
uint64_t es2_ally_nearest_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t G,uint32_t X,
    const uint32_t entry[3]) {
    es2_weapon_registers r={0,entry[1],entry[2]};uint32_t a[2],best=0,nearest=100000;
    for(uint32_t i=0;fx_s16(i)<fx_s16(R16(G+0x10u));i=(uint16_t)(i+1u)) {
        r.eax=(uint32_t)fx_s16(i);r.edx=R32(G+0xcu);const uint32_t A=R32(r.edx+4u*r.eax);
        if(A==X)continue;
        r.eax=A+0x92u;
        if(R8(A+0xa4u)) { r.ecx=1;continue; }
        r.edx=A+0x92u;
        if(R8(A+0x99u)) { r.ecx=1;continue; }
        r.ecx=0;
        r.eax=X;r.edx=X+0x26u;r.ecx=A+0x26u;a[0]=A+0x26u;a[1]=X+0x26u;GC(0x49288c,a,2);
        if(fx_s32(r.eax)<fx_s32(nearest)) { nearest=r.eax;best=A; }
    }
    return pair(best,r.edx);
}
