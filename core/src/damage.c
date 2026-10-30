#include "es2/core/damage.h"
#include "es2/core/geometry.h"
#include "es2/core/trig.h"
#include "fixed.h"

static uint64_t pair(uint32_t eax, uint32_t edx) { return ((uint64_t)edx << 32) | eax; }
static uint32_t lo(uint64_t r) { return (uint32_t)r; }
static uint32_t hi(uint64_t r) { return (uint32_t)(r >> 32); }
uint64_t es2_distance3_registers(uint32_t x, uint32_t y, uint32_t z) {
    return pair(es2_distance3(x, y, z), (z & 0x80000000u) ? UINT32_MAX : 0);
}
uint64_t es2_distance3_forward_registers(uint32_t x, uint32_t y, uint32_t z) {
    return es2_distance3_registers(x, y, z);
}
uint64_t es2_heading_point_registers(const void *p, const void *q, const void *corrections) {
    const uint8_t *a = p, *b = q;
    uint32_t x = fx_r32(a) - fx_r32(b), y = fx_r32(a + 4) - fx_r32(b + 4);
    if (!x && !y) x = 1;
    uint64_t r = es2_angle_ratio_registers(x, y, corrections);
    return pair((uint16_t)(lo(r) + 0xc000u), hi(r));
}
uint64_t es2_shield_damage_registers(void *shields, uint32_t front, uint32_t damage) {
    uint8_t *selected = (uint8_t *)shields + ((uint16_t)front ? 0 : 2);
    int64_t product = INT64_C(1000) * fx_s16(damage);
    /* Unsigned shift keeps the low 32 quotient bits without signed shifts. */
    uint32_t scaled = (uint32_t)((uint64_t)product >> 10);
    uint16_t left = (uint16_t)(fx_r16(selected) - scaled);
    fx_w16(selected, left);
    if (fx_s16(left) >= 0) return pair(0, (uint32_t)((uint64_t)product >> 32));
    uint32_t excess = (uint32_t)(-fx_s16(left)) * 1024u;
    fx_w16(selected, 0);
    return pair((uint32_t)fx_s16(excess / 1000u), excess % 1000u);
}

#define R16(a) m->read16(m->context, (a))
#define R32(a) m->read32(m->context, (a))
static uint64_t invoke(const es2_damage_context *g, uint32_t f, uint32_t slot,
                       const uint32_t *args, unsigned count) {
    if (g->observe) g->observe(g->observer_context, slot ? ES2_DAMAGE_SLOT : ES2_DAMAGE_CALL, slot ? slot : f);
    return g->call(g->call_context, f, args, count);
}
static uint64_t apply_part(const es2_guest_memory *m, const es2_damage_context *g,
                           uint32_t actor, uint32_t part, uint32_t damage,
                           uint32_t point, uint32_t range, uint32_t source) {
    uint32_t lookup[3] = {actor, part, g->point};
    (void)invoke(g, R32(R32(actor) + 0x58u), 0x58u, lookup, 3);
    uint32_t points[2] = {g->point, point};
    uint64_t r = invoke(g, g->distance, 0, points, 2);
    uint32_t reach = (uint32_t)fx_s16(range);
    if (fx_s32(lo(r)) >= fx_s32(reach)) return pair(reach, hi(r));
    uint32_t product = (reach - lo(r)) * (uint32_t)fx_s16(damage);
    uint32_t quotient = (uint32_t)(fx_s32(product) / fx_s16(range));
    uint32_t hit[4] = {actor, part, quotient, source};
    return invoke(g, R32(R32(actor) + 0x74u), 0x74u, hit, 4);
}
uint64_t es2_vehicle_hit_registers(const es2_guest_memory *m, const es2_damage_context *g,
    uint32_t actor, uint32_t damage, uint32_t point, uint32_t range, uint32_t source) {
    uint32_t points[2] = {point, actor + 0x26u};
    uint64_t r = invoke(g, g->heading, 0, points, 2);
    uint16_t angle = (uint16_t)(lo(r) - R16(actor + 0x10u));
    int32_t magnitude = angle == 0x8000u ? 32767 : fx_s16(angle);
    if (magnitude < 0) magnitude = -magnitude;
    uint32_t shield[3] = {actor + 0x222u, magnitude < 0x4000,
                          (lo(r) & 0xffff0000u) | (uint16_t)damage};
    r = invoke(g, g->shield, 0, shield, 3);
    damage = (uint16_t)lo(r);
    if (fx_s16(damage) <= 0) return r;
    uint32_t flags = R32(actor + 0x20eu);
    for (uint32_t part = 0; part < 29; ++part, flags += 2u) {
        if (!R16(flags)) continue;
        r = invoke(g, g->random, 0, &g->generator, 1);
        uint32_t trial = lo(r) & 0xfffu;
        r = pair(trial, trial < 0x802u);
        if (trial < 0x802u) r = apply_part(m, g, actor, part, damage, point, range, source);
    }
    return r;
}
uint64_t es2_building_hit_registers(const es2_guest_memory *m, const es2_damage_context *g,
    uint32_t actor, uint32_t damage, uint32_t point, uint32_t range, uint32_t source, uint32_t entry_edx) {
    uint32_t config = R32(actor + 0x1f2u), eax = config;
    if (R16(config + 0x1eu)) return pair(eax, entry_edx);
    if (R16(config + 4u) != 0xffffu && fx_s16(R16(R32(actor + 0x205u) + 5u)) < 2) {
        eax = actor + 0x92u;
        if (*(const uint8_t *)m->host(m->context, actor + 0x99u)) return pair(eax, config);
    }
    if (!R32(config + 0x38u)) return pair(eax, config);
    uint32_t flags = R32(actor + 0x201u);
    for (uint32_t part = 0;; ++part, flags += 2u) {
        config = R32(actor + 0x1f2u);
        uint32_t count = (uint32_t)fx_s16(R16(config + 0x12u));
        if (fx_s32(part) >= fx_s32(count)) return pair(config, count);
        if (!R16(flags)) continue;
        /* Masked random is always < 0x1004. Still advance the game RNG. */
        (void)invoke(g, g->random, 0, &g->generator, 1);
        (void)apply_part(m, g, actor, part, damage, point, range, source);
    }
}

static uint64_t part_call(const es2_building_part_context *g,uint32_t function,const uint32_t *a,unsigned n) {
    return g->call(g->call_context,function,a,n);
}
uint64_t es2_building_part_registers(const es2_guest_memory *m,const es2_building_part_context *g,
    uint32_t actor,uint32_t part,uint32_t damage,uint32_t source,uint32_t entry_eax) {
    uint32_t config=R32(actor+0x1f2u);
    if(R16(config+0x1eu))return pair(entry_eax,config);
    part=(uint32_t)fx_s16((uint16_t)part==0xffffu?0:part);
    if(!R16(R32(actor+0x201u)+2u*part))return pair(part,config);
    uint32_t state=R32(actor+0x205u)+11u*part;
    uint32_t definition=R32(config+0x14u)+30u*part;
    int32_t sum=fx_s16(R16(state))+fx_s16(damage);
    int32_t threshold=fx_s16(R16(definition));
    int destroy=threshold<=sum;
    uint64_t result=pair((uint32_t)threshold,(uint32_t)destroy);
    if(!destroy && fx_s32(fx_sar((uint32_t)threshold,1))<sum) {
        uint32_t divide[2]={10,(uint32_t)fx_s16(R16(definition))};
        uint32_t scale=lo(part_call(g,g->divide,divide,2));
        uint32_t old_args[2]={(uint32_t)fx_s16(R16(state)),scale};
        uint32_t previous=lo(part_call(g,g->multiply,old_args,2));
        uint32_t new_args[2]={(uint32_t)sum,scale};
        result=part_call(g,g->multiply,new_args,2);
        uint32_t next=lo(result);
        for(;;) {
            uint32_t bucket=previous++;
            result=pair(lo(result),bucket);
            if(fx_s32(bucket)>=fx_s32(next))break;
            result=part_call(g,g->random,&g->generator,1);
            destroy=(lo(result)&0xfffu)<0x19au;
            result=pair((uint32_t)destroy,hi(result));
            if(destroy)break;
        }
    }
    if(!destroy) {
        m->write16(m->context,state,(uint16_t)(R16(state)+(uint16_t)damage));
        return pair((lo(result)&0xffff0000u)|(uint16_t)damage,hi(result));
    }
    m->write32(m->context,state+7u,source);
    m->write16(m->context,state,R16(definition));
    m->write16(m->context,R32(actor+0x201u)+2u*part,0);
    result=part_call(g,R32(R32(actor)+0x40u),&actor,1);
    if((uint16_t)lo(result)==0x100u) {
        uint32_t player=R32(g->player_slot);
        result=pair(player,hi(result));
        if(player==source && R32(player+0x1a4u)==actor) {
            uint32_t message=0x2e;
            result=part_call(g,g->notify,&message,1);
        }
        if(source) {
            uint32_t event[3]={source,actor,0};
            result=part_call(g,R32(R32(source)+0x60u),event,3);
        }
        *(uint8_t *)m->host(m->context,actor+0x99u)=1;
        *(uint8_t *)m->host(m->context,actor+0x96u)=0;
        result=part_call(g,g->deactivate,&actor,1);
        uint32_t linked=R32(actor+0x1b6u);
        result=pair(linked,hi(result));
        if(linked) {
            uint32_t registers[3]={linked,hi(result),actor};
            result=g->call_registers(g->call_context,g->unlink,&linked,1,registers);
        }
    }
    uint16_t selector=R16(definition+4u);
    result=pair((lo(result)&0xffff0000u)|selector,hi(result));
    if(selector!=0xffffu) {
        uint32_t index=(uint32_t)fx_s16(selector);
        m->write16(m->context,state+5u,R16(g->state_table+2u*index));
        m->write16(m->context,state+3u,300);
        result=pair(lo(result),index);
    }
    return result;
}


/* Vehicle part lifecycle. Ratios are signed words even when their division
 * produces a wider value. Guest calls may change graph pointers: reload at
 * each subsequent stage. Equipment slots present after damage must have been
 * present in the initial snapshot; callers provide at most 59 such slots. */
#define VB(a) (*(uint8_t *)m->host(m->context,(a)))
#define VW(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define VD(a,v) m->write32(m->context,(a),(v))
static void vehicle_call(const es2_weapon_graph *g,uint32_t address,unsigned slot,
    uint32_t *args,unsigned count,es2_weapon_registers *r) {
    uint32_t f=slot?address:g->image+address-0x400000u;
    if(g->observe)g->observe(g->observer_context,f,slot);
    g->call(g->context,f,args,count,r);
}
static int32_t vehicle_s8(uint8_t value) { return value<128 ? (int32_t)value : (int32_t)value-256; }
static int32_t vehicle_ratio(const es2_guest_memory *m,uint32_t actor,unsigned offset,uint32_t *residue) {
    uint32_t state=R32(actor+0x206u),definition=R32(R32(actor+0x212u)+4u);
    int32_t value=fx_s16(R16(state+offset));
    if(value<0){if(residue)*residue=state;return 256;}
    int32_t denominator=fx_s16(R16(definition+offset)),numerator=value*256;
    if(residue)*residue=(uint32_t)(numerator%denominator);
    return fx_s16((uint32_t)(numerator/denominator));
}
static void vehicle_release_target(const es2_guest_memory *m,uint32_t actor) {
    uint32_t target=R32(actor+0x1a4u);
    if(target){VW(target+0x1a2u,R16(target+0x1a2u)-1u);VD(actor+0x1a4u,0);VB(actor+0x9du)=1;}
}
uint64_t es2_vehicle_part_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t actor,uint32_t part,uint32_t damage,uint32_t source,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[4];
#define VC(va,n) vehicle_call(g,va,0,a,n,&r)
#define VS(object,slot,n) vehicle_call(g,R32(R32(object)+slot),slot,a,n,&r)
#define NOTICE(id) do{a[0]=id;VC(0x420a68,1);}while(0)
#define DEACTIVATE() do{a[0]=actor;VC(0x411bc8,1);if(R32(actor+0x1b6u)){a[0]=R32(actor+0x1b6u);r.eax=a[0];r.ecx=actor;VC(0x423430,1);}}while(0)
#define CALLBACK(va) do{a[0]=actor+0x4du;a[1]=g->image+(va)-0x400000u;VC(0x413e50,2);}while(0)
    r.eax=r.edx=actor+0x92u;
    if(VB(actor+0xa3u)){VC(0x4240f4,0);if((uint16_t)r.eax)return pair(r.eax,r.edx);}
    r.eax=R32(actor+0x20eu);r.edx=(uint32_t)fx_s16(part);
    if(!R16(r.eax+2u*r.edx))return pair(r.eax,r.edx);
    int32_t previous[59]={0},old_ratio=0;
    for(int32_t i=0;i<fx_s16(R16(R32(actor+0x202u)+8u));++i){
        uint32_t equipment=R32(R32(R32(actor+0x202u)+4u)+4u*(uint32_t)fx_s16((uint32_t)i));
        if(equipment){uint32_t config=R32(equipment+0x18u);
            a[0]=actor+0x206u;a[1]=(config&0xffff0000u)|(uint16_t)(vehicle_s8(VB(config+0x17u))+19);
            VC(0x40dbc0,2);previous[i]=fx_s16(r.eax);}
    }
    r.edx=actor+0x92u;
    if(VB(actor+0xa3u))old_ratio=vehicle_ratio(m,actor,8,&r.edx);
    uint32_t config=R32(actor+0x1f2u)+0x212u;
    VD(g->image+0xa964cu,config);
    a[0]=actor+0x206u;a[1]=(config&0xffff0000u)|(uint16_t)part;a[2]=(r.edx&0xffff0000u)|(uint16_t)damage;VC(0x40da38,3);
    if(VB(actor+0xa3u)){
        int32_t next=vehicle_ratio(m,actor,8,NULL);
        if(old_ratio<256 && next==256){NOTICE(12);}else if(old_ratio==0 && next!=0){NOTICE(3);}
    }
    for(int32_t i=0;i<fx_s16(R16(R32(actor+0x202u)+8u));++i){
        uint32_t equipment=R32(R32(R32(actor+0x202u)+4u)+4u*(uint32_t)fx_s16((uint32_t)i));
        if(equipment){uint32_t config=R32(equipment+0x18u);
            a[0]=actor+0x206u;a[1]=(config&0xffff0000u)|(uint16_t)(vehicle_s8(VB(config+0x17u))+19);VC(0x40dbc0,2);
            int32_t next=fx_s16(r.eax);a[0]=equipment;a[1]=actor;a[2]=(uint32_t)previous[i];a[3]=(uint32_t)next;VS(equipment,0x68u,4);
            if(VB(actor+0xa3u)&&previous[i]!=256&&next==256){NOTICE(16);}
        }
    }
    a[0]=actor;VC(0x417bec,1);
    if(!R16(R32(actor+0x1f2u)+0x50u)){
        int32_t count=fx_s16(R16(R32(actor+0x1f2u)+0x4au));
        int32_t ratios[4]={vehicle_ratio(m,actor,0,NULL),vehicle_ratio(m,actor,2,NULL),0,0};
        if(count>2){ratios[2]=vehicle_ratio(m,actor,20,NULL);ratios[3]=vehicle_ratio(m,actor,22,NULL);}
        for(int32_t i=0;i<count;++i)if(ratios[i]==256&&R32(R32(actor+0x238u)+4u*i)){
            a[0]=R32(g->image+0xa9691u);a[1]=R32(R32(actor+0x238u)+4u*i);VC(0x478628,2);VD(R32(actor+0x238u)+4u*i,0);
        }
        if(!VB(actor+0xa4u)&&count){
            int32_t left=ratios[0],right=ratios[1],destroyed=0;int disabled=0;
            if(count==4){left=fx_s32(fx_sar((uint32_t)(left+ratios[2]),1));right=fx_s32(fx_sar((uint32_t)(right+ratios[3]),1));}
            for(int32_t i=0;i<count;++i)if(ratios[i]==256&&++destroyed>=fx_s32(fx_sar((uint32_t)count,1))){disabled=1;break;}
            if(disabled){
                if(!VB(actor+0x99u)){
                    if(source){a[0]=source;a[1]=actor;a[2]=0;VS(source,0x60u,3);
                        if(source==R32(g->image+0xd24e6u)&&R32(source+0x1a4u)==actor){NOTICE(47);}}
                    DEACTIVATE();CALLBACK(0x4998ba);
                }
                VB(actor+0xa4u)=1;vehicle_release_target(m,actor);
            }else if(left>140||right>140){if(VB(actor+0xa3u)&&!VB(actor+0xa9u)){NOTICE(19);}VB(actor+0xa9u)=1;
            }else if(left>80||right>80){if(VB(actor+0xa3u)&&!VB(actor+0xa8u)){NOTICE(8);}VB(actor+0xa8u)=1;}
        }
    }
    if(!VB(actor+0x99u)){
        a[0]=actor+0x206u;a[1]=0;VC(0x40d9f8,2);int destroyed=(uint16_t)r.eax!=0;
        if(!destroyed){a[0]=actor+0x206u;a[1]=1;VC(0x40d9f8,2);destroyed=(uint16_t)r.eax!=0;}
        if(!destroyed)destroyed=vehicle_ratio(m,actor,18,NULL)==256;
        if(!destroyed)destroyed=vehicle_ratio(m,actor,16,NULL)==256;
        if(destroyed){
            VB(actor+0x99u)=1;uint16_t disabled=(uint16_t)vehicle_s8(VB(actor+0xa4u));
            a[0]=actor;a[1]=0;a[2]=30000;a[3]=0;VS(actor,0x74u,4);
            if(source){a[0]=source;a[1]=actor;a[2]=(r.eax&0xffff0000u)|disabled;VS(source,0x60u,3);}
            if(!disabled){DEACTIVATE();}
            vehicle_release_target(m,actor);VB(actor+0x96u)=0;
            if(source&&source==R32(g->image+0xd24e6u)&&R32(source+0x1a4u)==actor){NOTICE(46);}
            if(R16(R32(actor+0x1f2u)+0x4cu)){
                VB(actor+0x38u)=1;VD(actor+0x2eu,(uint32_t)-100000);CALLBACK(0x49983e);
                for(int32_t i=0;i<fx_s16(R16(actor+0x23cu));++i)if(R32(R32(actor+0x238u)+4u*i)){
                    a[0]=R32(g->image+0xa9691u);a[1]=R32(R32(actor+0x238u)+4u*i);VC(0x478628,2);VD(R32(actor+0x238u)+4u*i,0);
                }VW(actor+0x23cu,0);
            }else if(!R16(R32(actor+0x1f2u)+0x50u)){CALLBACK(0x49987c);}
        }
    }
    if(!VB(actor+0xaau)&&!VB(actor+0xabu)){
        int32_t ratio=vehicle_ratio(m,actor,10,NULL);
        if(ratio>192){if(VB(actor+0xa3u)&&!VB(actor+0xabu)){NOTICE(4);}VB(actor+0xabu)=1;
        }else if(ratio>128){if(VB(actor+0xa3u)&&!VB(actor+0xaau)){NOTICE(4);}VB(actor+0xaau)=1;}
    }
    VC(0x409904,0);return pair(r.eax,r.edx);
#undef VC
#undef VS
#undef NOTICE
#undef DEACTIVATE
#undef CALLBACK
}
#undef VB
#undef VW
#undef VD

/* The damage data (damage.cpp) and the part tables of the mission load
 * (docs/reverse-engineering/mission_load.md, section "damage data and part
 * tables"). Every guest read and write is done in the original's order (outputs
 * may alias the inputs), and the register struct r follows EAX/EDX/ECX so that
 * each game call sees the original's entry registers. */
#define DD_R32(a) m->read32(m->context,(a))
#define DD_R16(a) m->read16(m->context,(a))
#define DD_W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define DD_W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define DD_W32(a,v) m->write32(m->context,(a),(v))
#define DD_GVA(x) (g->image+(uint32_t)(x)-0x400000u)
static uint32_t dd_sx(uint32_t x) { return (uint32_t)fx_s16(x); }
static void dd_ecx(uint32_t *ecx,uint32_t value) { if(ecx)*ecx=value; }
/* One game call: the observer first (reference VA), then the bridge. */
static void dd_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,DD_GVA(va),a,n,r);
}
/* The stream's read method: slot +18h of the table `table` (read by the
 * caller into the register the original uses), arguments (S, size, buffer). */
static void dd_read(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t table,const uint32_t a[3],
    es2_weapon_registers *r) {
    const uint32_t target=DD_R32(table+0x18u);
    if(g->observe)g->observe(g->observer_context,target,0x18u);
    g->call(g->context,target,a,3,r);
}

/* Original: DBSIM.EXE 0x40d160. Frame: record f-50h (context word f-40h),
 * stream f-30h. */
uint64_t es2_dmg_load_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t name,
    const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_DMG_LOAD_FRAME,stream=f-0x30u;
    es2_weapon_registers r={DD_GVA(0x498840u),entry[1],entry[2]};uint32_t a[6];
    dd_call(g,0x49630cu,NULL,0,&r);
    DD_W16(f-0x40u,8u);
    r.edx=stream;a[0]=stream;a[1]=name;dd_call(g,0x472f14u,a,2,&r);
    r.eax=DD_R32(f-0x2cu);
    if(r.eax) {
        /* 40d199: SETE DL of a nonzero EAX, AND EDX,1. */
        r.edx=0;
        a[0]=DD_GVA(0x6c56c8u);a[1]=5;a[2]=r.edx;a[3]=DD_GVA(0x498858u);a[4]=0x85u;a[5]=name;
        dd_call(g,0x479c78u,a,6,&r);
    }
    /* 40d1b1: MOV CX,[498854h] keeps ECX's high word; INC rereads memory;
     * the slot array is read after the increment. */
    const uint32_t counter=DD_R16(DD_GVA(0x498854u));
    r.ecx=(r.ecx&0xffff0000u)|counter;
    r.eax=dd_sx(counter);
    DD_W16(DD_GVA(0x498854u),DD_R16(DD_GVA(0x498854u))+1u);
    uint32_t slot=r.eax<<2;
    r.eax=stream;
    slot=slot*3u+DD_R32(DD_GVA(0x4a9828u));
    a[0]=slot;a[1]=stream;dd_call(g,0x40d09cu,a,2,&r);
    r.eax=slot;r.edx=stream;a[0]=stream;a[1]=2;dd_call(g,0x472f64u,a,2,&r);
    r.eax=slot;                                     /* POP EAX */
    r.edx=DD_R32(f-0x50u);
    dd_ecx(ecx,r.ecx);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x40d09c. */
uint64_t es2_dmg_read_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t table,
    uint32_t stream,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[3];
    r.eax=table+0xau;r.edx=stream;r.ecx=DD_R32(stream);
    a[0]=stream;a[1]=2;a[2]=table+0xau;dd_read(m,g,r.ecx,a,&r);
    r.eax=dd_sx(DD_R16(table+0xau))*2u;a[0]=r.eax;dd_call(g,0x4774f0u,a,1,&r);
    r.ecx=a[0];                                     /* POP ECX */
    const uint32_t capacities=r.eax;
    DD_W32(table+4u,capacities);
    r.eax=dd_sx(DD_R16(table+0xau))*2u;r.edx=stream;r.ecx=DD_R32(stream);
    a[0]=stream;a[1]=r.eax;a[2]=capacities;dd_read(m,g,r.ecx,a,&r);
    r.eax=table+8u;r.edx=stream;r.ecx=DD_R32(stream);
    a[0]=stream;a[1]=2;a[2]=table+8u;dd_read(m,g,r.ecx,a,&r);
    r.eax=dd_sx(DD_R16(table+8u));r.edx=r.eax*18u;
    a[0]=r.edx;dd_call(g,0x4774f0u,a,1,&r);
    r.ecx=a[0];                                     /* POP ECX */
    uint32_t region=r.eax;
    DD_W32(table,region);
    for(uint32_t k=0;fx_s16(k)<fx_s16(DD_R16(table+8u));++k,region+=0x12u) {
        r.eax=stream;a[0]=region;a[1]=stream;dd_call(g,0x40cff8u,a,2,&r);
    }
    dd_ecx(ecx,r.ecx);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x40cff8. */
uint64_t es2_dmg_region_read_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t region,
    uint32_t stream,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[3];
    a[0]=stream;a[1]=8;a[2]=region;r.eax=DD_R32(stream);dd_read(m,g,r.eax,a,&r);
    const uint32_t links=DD_R16(region+6u);          /* DI, read once */
    if(links) {
        r.eax=dd_sx(links)<<2;a[0]=r.eax;dd_call(g,0x4774f0u,a,1,&r);
        r.ecx=a[0];                                 /* POP ECX */
        DD_W32(region+8u,r.eax);
        r.edx=DD_R32(region+8u);
        r.ecx=dd_sx(DD_R16(region+6u))<<2;
        a[0]=stream;a[1]=r.ecx;a[2]=r.edx;r.eax=DD_R32(stream);dd_read(m,g,r.eax,a,&r);
    }
    DD_W16(region+0xcu,0xffffu);
    r.edx=0;
    DD_W32(region+0xeu,r.edx);
    dd_ecx(ecx,r.ecx);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x40d050. */
uint64_t es2_dmg_region_copy_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t dest,
    uint32_t source,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[3];
    (void)DD_R16(dest+6u);                          /* 40d05c: CMP, flags never tested */
    a[0]=dest;a[1]=source;a[2]=0x12u;dd_call(g,0x49680eu,a,3,&r);
    r.eax=dd_sx(DD_R16(dest+6u))<<2;a[0]=r.eax;dd_call(g,0x4774f0u,a,1,&r);
    r.ecx=a[0];                                     /* POP ECX */
    const uint32_t links=r.eax;
    DD_W32(dest+8u,links);
    r.eax=dd_sx(DD_R16(dest+6u))<<2;r.edx=DD_R32(source+8u);
    a[0]=links;a[1]=r.edx;a[2]=r.eax;dd_call(g,0x49680eu,a,3,&r);
    dd_ecx(ecx,r.ecx);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x40d128. */
uint64_t es2_dmg_capacity_set_registers(const es2_guest_memory *m,uint32_t table,uint32_t indices,
    uint32_t values,uint32_t count,const uint32_t entry[3],uint32_t *ecx) {
    (void)entry;
    uint32_t k=0;
    for(;fx_s16(k)<fx_s16(count);++k,indices+=2u) {
        const uint32_t index=dd_sx(DD_R16(indices));
        const uint32_t target=DD_R32(table+4u)+2u*index;
        const uint32_t value=DD_R16(values);
        values+=2u;
        DD_W16(target,value);
    }
    dd_ecx(ecx,indices);
    return pair(k,values);
}

/* Original: DBSIM.EXE 0x40d240. */
uint64_t es2_dmg_region_set_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t table,
    uint32_t indices,uint32_t sources,uint32_t count,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[2];
    for(uint32_t k=0;fx_s16(k)<fx_s16(count);++k,indices+=2u,sources+=4u) {
        const uint32_t source=DD_R32(sources);
        r.eax=dd_sx(DD_R16(indices));
        r.ecx=DD_R32(table);
        r.edx=r.eax*18u;
        r.ecx+=r.edx;
        a[0]=r.ecx;a[1]=source;dd_call(g,0x40d050u,a,2,&r);
    }
    dd_ecx(ecx,r.ecx);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x40d284. */
uint64_t es2_dmg_part_store_registers(const es2_guest_memory *m,uint32_t table,uint32_t count,
    uint32_t indices,uint32_t words,uint32_t dwords,const uint32_t entry[3],uint32_t *ecx) {
    uint32_t eax=entry[0],c=entry[2],k=0;
    for(;fx_s16(k)<fx_s16(count);++k,indices+=2u,words+=2u,dwords+=4u) {
        const uint32_t index=dd_sx(DD_R16(indices));
        c=DD_R32(dwords);
        const uint32_t word=DD_R16(words);
        eax=DD_R32(table)+index*18u;
        DD_W16(eax+0xcu,word);
        DD_W32(eax+0xeu,c);
    }
    dd_ecx(ecx,c);
    return pair(eax,k);
}

/* Original: DBSIM.EXE 0x40cafc. */
uint64_t es2_col_part_gather_registers(const es2_guest_memory *m,uint32_t record,uint32_t ids,
    uint32_t owners,uint32_t points,const uint32_t entry[3],uint32_t *ecx) {
    (void)entry;
    uint32_t item=DD_R32(record+6u),k=0;
    for(;fx_s16(k)<fx_s16(DD_R16(record+4u));++k,item+=0x10u,ids+=2u,points+=4u) {
        DD_W16(ids,DD_R16(item+6u));
        DD_W32(points,item+8u);
        DD_W16(owners,DD_R16(record));
        owners+=2u;
    }
    dd_ecx(ecx,k);
    return pair((item&0xffff0000u)|DD_R16(record+4u),record);
}

/* Original: DBSIM.EXE 0x40cb84. EBX starts at 0 and only BX changes. */
uint64_t es2_col_part_collect_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t list,
    uint32_t ids,uint32_t owners,uint32_t points,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[4];
    uint32_t total=0,record=DD_R32(list+2u);
    for(uint32_t k=0;fx_s16(k)<fx_s16(DD_R16(list));++k,record+=10u) {
        const uint32_t s=dd_sx(total);
        r.eax=s*4u+points;r.edx=s*2u+owners;r.ecx=s*2u+ids;
        a[0]=record;a[1]=r.ecx;a[2]=r.edx;a[3]=r.eax;dd_call(g,0x40cafcu,a,4,&r);
        total=(uint16_t)(total+r.eax);
    }
    dd_ecx(ecx,r.ecx);
    return pair(total,r.edx);
}

/* Original: DBSIM.EXE 0x40d2cc. ESI = regions, EDI = capacities (only the
 * low words are used). */
uint64_t es2_damage_state_ctor_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t state,
    uint32_t regions,uint32_t capacities,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[3];
    const uint32_t c2=dd_sx(capacities)*2u,r2=dd_sx(regions)*2u;
    r.eax=c2;a[0]=c2;dd_call(g,0x4774f0u,a,1,&r);
    r.ecx=c2;DD_W32(state,r.eax);
    r.edx=r2;a[0]=r2;dd_call(g,0x4774f0u,a,1,&r);
    r.ecx=r2;DD_W32(state+4u,r.eax);
    r.ecx=r2;a[0]=r2;dd_call(g,0x4774f0u,a,1,&r);
    r.ecx=r2;DD_W32(state+8u,r.eax);
    r.eax=c2;r.edx=DD_R32(state);a[0]=r.edx;a[1]=0;a[2]=c2;dd_call(g,0x49681au,a,3,&r);
    r.ecx=r2;r.eax=DD_R32(state+4u);a[0]=r.eax;a[1]=0;a[2]=r2;dd_call(g,0x49681au,a,3,&r);
    r.edx=r2;r.ecx=DD_R32(state+8u);a[0]=r.ecx;a[1]=1;a[2]=r2;dd_call(g,0x49681au,a,3,&r);
    DD_W16(state+0x18u,regions);
    DD_W16(state+0x1au,capacities);
    dd_ecx(ecx,r.ecx);
    return pair(state,r.edx);
}

/* Original: DBSIM.EXE 0x40d354. Frame: record f-24h (context word f-14h),
 * the new object f-4. */
uint64_t es2_damage_state_bind_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t state,
    uint32_t owner,uint32_t dmg,uint32_t col,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_DAMAGE_BIND_FRAME,shared=DD_GVA(0x498864u);
    es2_weapon_registers r={DD_GVA(0x498878u),entry[1],entry[2]};uint32_t a[1];
    dd_call(g,0x49630cu,NULL,0,&r);
    r.edx=dmg;DD_W32(state+0xcu,r.edx);
    r.ecx=col;DD_W32(state+0x10u,r.ecx);
    r.eax=owner;DD_W32(state+0x14u,r.eax);
    if(!DD_R32(shared)) {
        a[0]=6;dd_call(g,0x47749cu,a,1,&r);
        r.ecx=6;                                    /* POP ECX */
        DD_W32(f-4u,r.eax);
        if(r.eax) {
            DD_W16(f-0x14u,0x14u);
            r.edx=DD_R32(f-4u);
            r.ecx=(r.ecx&0xffffff00u)|0x14u;        /* MOV CL,14h */
            DD_W8(r.edx+4u,0x14u);
            r.eax=0x28u;a[0]=r.eax;dd_call(g,0x4774f0u,a,1,&r);
            r.ecx=a[0];                             /* POP ECX */
            r.edx=DD_R32(f-4u);DD_W32(r.edx,r.eax);
            r.eax=DD_R32(f-4u);DD_W8(r.eax+5u,0);
            r.edx=DD_R32(DD_GVA(0x6cbee4u));DD_W32(r.edx,DD_R32(r.edx)+1u);
            r.ecx=DD_R32(DD_GVA(0x6cbee4u));DD_W32(r.ecx,DD_R32(r.ecx)-1u);
            DD_W16(f-0x14u,8u);
        }
        r.eax=DD_R32(f-4u);
        DD_W32(shared,r.eax);
    }
    r.edx=DD_R32(f-0x24u);
    dd_ecx(ecx,r.ecx);
    return pair(r.eax,r.edx);
}
#undef DD_R32
#undef DD_R16
#undef DD_W8
#undef DD_W16
#undef DD_W32
#undef DD_GVA
