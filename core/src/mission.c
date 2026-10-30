#include "es2/core/mission.h"
#include "fixed.h"
#include <stddef.h>
/* Runtime mission logic (docs/reverse-engineering/mission_runtime.md).
 * The helpers in this first block are shared by every section. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
/* A DBSIM VA (globals, direct callees) inside the image at g->image. */
#define MISSION_VA(va) (g->image+(uint32_t)(va)-0x400000u)
static uint64_t mission_pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
/* A 16-bit register write: the high word stays. */
static uint32_t mission_word(uint32_t high,uint32_t low) { return (high&0xffff0000u)|(uint16_t)low; }
static uint32_t mission_sx16(uint32_t x) { return (uint32_t)fx_s16(x); }
static int mission_less(uint32_t a,uint32_t b) { return fx_s32(a)<fx_s32(b); }
/* A direct call names the original VA (observed for the callee counters);
 * registers go in and come back in place. */
static void mission_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,MISSION_VA(va),a,n,r);
}

/* ---- trigger dispatch and region predicates ---------------- */

/* Original: DBSIM.EXE 0x4233a4 */
uint64_t es2_region_test_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t region,uint32_t position,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    if(R16(region)) {
        /* Distance region: 4928d0(position, point1) < threshold (+6). */
        uint32_t a[2]={position,R32(region+2u)};
        r.ecx=a[1];mission_call(g,0x4928d0u,a,2,&r);
        if(mission_less(r.eax,R32(region+6u)))return mission_pair(mission_word(r.eax,1),r.edx);
        return mission_pair(0,r.edx);
    }
    /* Rectangle: lower X, upper X, lower Y, upper Y, each bound recomputed
     * from the two corner pointers; edges are outside. */
    uint32_t eax=0,edx=0;
    for(unsigned k=0;k<4;++k) {
        uint32_t axis=k<2?0u:4u,second=R32(region+6u),first=R32(region+2u);
        edx=R32(first+axis);eax=R32(second+axis);
        uint32_t p=R32(position+axis);
        if(!(k&1)) {
            if(!mission_less(edx,eax))edx=eax;           /* min */
            if(!mission_less(edx,p))return mission_pair(0,edx);
        } else {
            if(!mission_less(eax,edx))edx=eax;           /* max */
            if(!mission_less(p,edx))return mission_pair(0,edx);
        }
    }
    return mission_pair(mission_word(eax,1),edx);
}

/* Original: DBSIM.EXE 0x4234b8 */
uint64_t es2_trigger_regions_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t trigger,uint32_t position,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    if(R16(trigger+0xau))return mission_pair(mission_word(r.eax,1),r.edx);
    uint32_t list=R32(trigger+6u);
    for(uint32_t i=0;fx_s16(i)<fx_s16(R16(trigger+4u));++i,list+=4u) {
        uint32_t a[2]={R32(list),position};
        r.eax=position;r.edx=a[0];mission_call(g,0x4233a4u,a,2,&r);
        if((uint16_t)r.eax) {
            uint32_t b[1]={trigger};mission_call(g,0x423430u,b,1,&r);
            return mission_pair(mission_word(r.eax,1),r.edx);
        }
    }
    return mission_pair(0,r.edx);
}

/* Original: DBSIM.EXE 0x423508 */
uint64_t es2_trigger_group_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t group,uint32_t trigger,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    /* The local at EBP-2 is the high word of the pushed entry ECX. */
    uint32_t local=entry[2]>>16;
    for(uint32_t i=0;fx_s16(i)<fx_s16(R16(group+0x10u));++i) {
        uint32_t array=R32(group+0xcu),actor=R32(array+4u*mission_sx16(i));
        uint32_t a[2]={trigger,actor+0x26u};
        r.eax=trigger;r.edx=array;mission_call(g,0x4234b8u,a,2,&r);
        local=(uint16_t)r.eax;
        if(local)break;
    }
    return mission_pair(mission_word(r.eax,local),r.edx);
}

/* Original: DBSIM.EXE 0x423430 */
uint64_t es2_trigger_fire_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t trigger,uint32_t residue,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    if(R16(trigger+0xau))return mission_pair(r.eax,r.edx);
    W16(trigger+0xau,1);
    /* Event record {selector word, actor dword, 16 bytes never written}. */
    uint32_t record=g->scratch,bank=MISSION_VA(0x4a9e70u);
    for(unsigned j=0;j<16;j+=4)W32(record+6u+j,residue?R32(residue+j):0u);
    for(unsigned i=0;i<10;++i) {
        uint32_t slot=trigger+0xcu+2u*i;
        r.eax=mission_word(r.eax,R16(slot));
        if(fx_s16(r.eax)<=-1)continue;
        r.edx=mission_word(r.edx,R16(slot+0x14u));
        if((uint16_t)r.edx==6) {
            r.ecx=mission_sx16(r.eax);W16(bank+2u*r.ecx,R16(bank+2u*r.ecx)+1u);
        } else if((uint16_t)r.edx==5) {
            r.eax=mission_sx16(r.eax);W16(bank+2u*r.eax,0);
        }
        r.eax=mission_word(r.eax,R16(trigger+0x34u));
        if(fx_s16(r.eax)<=-1)continue;
        W16(record,r.eax);r.edx=0;W32(record+2u,0);
        mission_call(g,0x433158u,NULL,0,&r);
        /* Virtual slot 0 of the cockpit object: (object, &record). */
        uint32_t a[2]={r.eax,record};
        r.ecx=record;r.eax=R32(a[0]);
        g->call(g->context,R32(r.eax),a,2,&r);
    }
    return mission_pair(r.eax,r.edx);
}

/* 426b70 types 2..6: the group walk. Returns the predicate (ESI). */
static uint32_t trigger_scan_groups(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t trigger,uint32_t type,es2_weapon_registers *r) {
    uint32_t count=R16(MISSION_VA(0x4a9e04u)),next=R32(MISSION_VA(0x4a9e00u));
    r->eax=mission_word(r->eax,count);
    if(type==2)r->edx=next; else r->ecx=next;
    for(;;) {
        r->eax=mission_word(r->eax,count);
        uint32_t group=0;
        if((uint16_t)count) { group=next;next+=0x7au; }
        count=(uint16_t)(count-1u);
        r->edx=group;
        if(!group)return 0;
        if(type==2) {
            r->ecx=group;
            if(R8(group+0x12u))continue;
            r->eax=group;r->edx=R32(group+0x14u)!=0;
            if(r->edx)continue;
            r->ecx=group;
        } else {
            if(type==3) {
                r->eax=group;r->edx=(uint32_t)(int32_t)(int8_t)R8(group+0x12u)-1u;
                if(r->edx)continue;
            } else {
                r->eax=group;r->edx=R16(group)==type-4u;
                if(!r->edx)continue;
            }
            r->ecx=group;r->eax=R32(group+0x14u)!=0;
            if(r->eax)continue;
            r->edx=group;
        }
        uint32_t a[2]={group,trigger};mission_call(g,0x423508u,a,2,r);
        uint32_t result=mission_sx16(r->eax);
        if(result)return result;
    }
}

/* Original: DBSIM.EXE 0x426b70 */
uint64_t es2_trigger_scan_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t player,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t trigger=R32(MISSION_VA(0x4a9e28u));
    for(uint32_t i=0;;++i,trigger+=0x3au) {
        r.edx=mission_sx16(R16(MISSION_VA(0x4a9e24u)));
        if(!mission_less(i,r.edx))break;
        uint32_t result=0,type=mission_sx16(R16(trigger)),a[2];
        r.eax=type;
        if(type==0||type==7||type==8||type==9) {
            uint32_t position=(type?R32(trigger+0x36u):player)+0x26u;
            if(!type)r.edx=position;
            a[0]=trigger;a[1]=position;mission_call(g,0x4234b8u,a,2,&r);
            result=mission_sx16(r.eax);
            if(type)r.eax=result;
        } else if(type==1||type==10) {
            uint32_t group;
            if(type==1) { r.eax=player;group=R32(player+0x45u);r.edx=group; } else group=R32(trigger+0x36u);
            a[0]=group;a[1]=trigger;mission_call(g,0x423508u,a,2,&r);
            result=mission_sx16(r.eax);
            if(type==10)r.eax=result;
        } else if(type<=6) result=trigger_scan_groups(m,g,trigger,type,&r);
        if(result) { a[0]=trigger;mission_call(g,0x423430u,a,1,&r);r.ecx=trigger; }
    }
    return mission_pair(r.eax,r.edx);
}

/* ---- player route callback, mission timers, status check --- */

#define ROUTE_W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))

/* Original: DBSIM.EXE 0x423170 */
uint64_t es2_route_closing_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t holder,const uint32_t entry[3]) {
    (void)g;(void)entry;
    uint32_t index=mission_sx16(R16(holder)),route=R32(holder+2u);
    if(index+1u!=mission_sx16(R16(route))-1u)return mission_pair(0,route);
    /* The next point pointer against the first one. */
    uint32_t points=R32(route+2u),next=R32(points+4u*index+4u);
    return mission_pair(next==R32(points),next);
}

/* Original: DBSIM.EXE 0x42313c */
uint64_t es2_route_advance_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t holder,const uint32_t entry[3]) {
    (void)g;
    W16(holder,R16(holder)+1u);
    uint32_t route=R32(holder+2u),count=R16(route);
    /* MOV DX,[route]: the entry EDX keeps its high word. */
    if(fx_s16(count)<=1)return mission_pair(holder,mission_word(entry[1],count));
    uint32_t index=mission_sx16(R16(holder)),last=mission_sx16(count)-1u;
    if(index!=last)return mission_pair(holder,last);
    uint32_t points=R32(route+2u);
    if(R32(points+4u*index)==R32(points))W16(holder,0);
    return mission_pair(holder,points);
}

/* 41c194 modes 3 and 7: the target sequence. Returns EDX. */
static uint32_t player_route_sequence(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t actor,es2_weapon_registers *r) {
    uint32_t step=MISSION_VA(0x4a9cecu),countdown=MISSION_VA(0x4a9cf1u),a[2];
    if(R32(step)==4u)return r->edx;
    a[0]=R32(actor+0x45u);r->edx=a[0];mission_call(g,0x4238a0u,a,1,r);r->ecx=a[0];
    uint32_t target=r->eax;
    r->eax=target+0x92u;
    if(!R8(target+0x99u)) {
        r->edx=R32(target+0x45u);r->ecx=R32(r->edx+0x14u)!=0;
        if(!r->ecx) {
            a[0]=actor+0x26u;a[1]=target+0x26u;r->eax=a[0];mission_call(g,0x49288cu,a,2,r);
            if(mission_less(r->eax,10000u)) {
                a[0]=target+0x26u;a[1]=actor+0x26u;r->eax=a[0];r->edx=a[0];r->ecx=a[1];
                mission_call(g,0x492934u,a,2,r);
                /* The bearing relative to the facing plus the turret word,
                 * shifted by a quarter: in front when below a half. */
                uint16_t front=(uint16_t)(r->eax-R16(actor+0x10u)+R16(actor+0x298u)+0x2000u);
                r->eax=front<0x4000u;
                if(r->eax) {
                    a[0]=MISSION_VA(0x4a9cf0u);r->eax=1;mission_call(g,0x467aa8u,a,1,r);r->ecx=a[0];
                    r->edx=r->eax==0;
                    if(!r->edx)return r->edx;
                    r->ecx=R32(step);r->eax=R32(MISSION_VA(0x49a318u)+4u*r->ecx);W32(countdown,r->eax);
                    r->edx=R32(step)+0x34u;a[0]=r->edx;mission_call(g,0x420a68u,a,1,r);r->ecx=a[0];
                    W32(step,R32(step)+1u);
                    r->ecx=R32(step);
                    if(r->ecx==4u)ROUTE_W8(actor+0xa0u,1);
                    return r->edx;
                }
            }
        }
    }
    /* A broken sequence starts over (event 38h when it had begun). */
    if(R32(step)) { a[0]=0x38u;mission_call(g,0x420a68u,a,1,r);r->ecx=0x38u;r->edx=0;W32(step,0); }
    r->ecx=0;W32(countdown,0);
    return r->edx;
}

/* Original: DBSIM.EXE 0x41c194 */
uint64_t es2_player_route_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t actor,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t group=R32(actor+0x45u),a[2];
    r.eax=group;r.edx=R32(group+0xcu);
    if(R32(r.edx)!=actor)return mission_pair(0,r.edx);
    /* The mode is read once, before any call. */
    uint32_t mode=R16(MISSION_VA(0x4a9e54u));
    /* Route: the next point, reached within 10000 unless it closes a loop. */
    uint32_t holder=R32(actor+0x45u)+4u;
    r.eax=mission_word(r.eax,R16(holder))+1u;
    a[0]=holder;a[1]=r.eax;mission_call(g,0x423b0cu,a,2,&r);
    if(r.eax) {
        a[0]=actor+0x26u;a[1]=r.eax;r.edx=a[0];mission_call(g,0x4928d0u,a,2,&r);
        if(mission_less(r.eax,10000u)) {
            holder=R32(actor+0x45u)+4u;r.ecx=holder;a[0]=holder;mission_call(g,0x423170u,a,1,&r);r.ecx=holder;
            if(!(uint16_t)r.eax) {
                a[0]=0x1du;mission_call(g,0x420a68u,a,1,&r);r.ecx=0x1du;
                holder=R32(actor+0x45u)+4u;r.eax=holder;a[0]=holder;mission_call(g,0x42313cu,a,1,&r);r.ecx=holder;
            }
        }
    }
    switch(mode) {
    case 0: {
        if(R32(MISSION_VA(0x4a9cf8u)))break;
        uint32_t target=R32(actor+0x1a4u);r.eax=target;
        if(!target)break;
        a[0]=R32(actor+0x45u);a[1]=target;r.edx=a[0];mission_call(g,0x423918u,a,2,&r);
        if(!(uint16_t)r.eax)break;
        W32(MISSION_VA(0x4a9cf8u),1);
        a[0]=0x19u;mission_call(g,0x420a68u,a,1,&r);r.ecx=0x19u;
        ROUTE_W8(actor+0x9fu,1);
        break; }
    case 5: {
        a[0]=actor;mission_call(g,0x41dbccu,a,1,&r);r.ecx=actor;
        a[0]=actor+0x26u;a[1]=r.eax;r.edx=a[0];mission_call(g,0x4928d0u,a,2,&r);
        if(mission_less(r.eax,40000u))ROUTE_W8(actor+0x9fu,1);
        break; }
    case 3: case 7: r.edx=player_route_sequence(m,g,actor,&r);break;
    default: break;
    }
    return mission_pair(0,r.edx);
}

/* Original: DBSIM.EXE 0x4230a4 */
uint64_t es2_mission_timer_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t timer,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t gate=R32(timer),a[2];
    r.eax=gate;
    /* A gated timer runs only after its gate trigger has fired. */
    if(gate&&!R16(gate+0xau))return mission_pair(r.eax,r.edx);
    a[0]=timer+0x2cu;r.edx=a[0];mission_call(g,0x467aa8u,a,1,&r);
    r.ecx=r.eax==0;
    if(!r.ecx)return mission_pair(r.eax,r.edx);
    for(unsigned k=0;k<10;++k) {
        r.eax=R32(timer+4u+4u*k);
        if(!r.eax)continue;
        a[0]=r.eax;mission_call(g,0x423430u,a,1,&r);r.ecx=a[0];
    }
    a[0]=timer+0x2cu;a[1]=30000u;mission_call(g,0x467ac4u,a,2,&r);
    return mission_pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x426b48 */
uint64_t es2_timer_scan_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t timer=R32(MISSION_VA(0x4a9e38u));
    for(uint32_t i=0;;++i,timer+=0x31u) {
        r.eax=mission_sx16(R16(MISSION_VA(0x4a9e34u)));
        if(!mission_less(i,r.eax))break;
        uint32_t a[1]={timer};mission_call(g,0x4230a4u,a,1,&r);r.ecx=timer;
    }
    return mission_pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x4135e8 */
uint64_t es2_status_check_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t player,uint32_t force,uint32_t residue,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t timer=MISSION_VA(0x4a9b68u),last=MISSION_VA(0x49939cu),reported=MISSION_VA(0x4993a0u),a[2];
    if(!R8(MISSION_VA(0x499398u)))ROUTE_W8(MISSION_VA(0x499398u),1);
    int periodic=!(uint16_t)force;
    if(periodic) {
        a[0]=timer;mission_call(g,0x467a48u,a,1,&r);r.ecx=timer;
        r.eax=(uint16_t)r.eax==0;
        if(!r.eax) {
            /* Still counting: step once more, report the last status. */
            mission_call(g,0x467a48u,a,1,&r);r.ecx=timer;
            return mission_pair(mission_word(r.eax,R16(reported)),r.edx);
        }
    }
    r.edx=player+0x92u;r.eax=r.edx;
    if(R8(player+0x99u))r.eax=mission_word(r.eax,2);
    else if(R8(player+0xa4u))r.eax=mission_word(r.eax,3);
    else {
        int found=0;
        a[0]=player;
        if(periodic) {
            mission_call(g,0x413780u,a,1,&r);r.ecx=player;
            if((uint16_t)r.eax) { r.eax=mission_word(r.eax,8);found=1; }
            else {
                mission_call(g,0x41379cu,a,1,&r);r.ecx=player;
                if((uint16_t)r.eax) { r.eax=mission_word(r.eax,7);found=1; }
            }
        }
        /* The objective pass reads the caller's ESI (force): g->call. */
        if(!found) { mission_call(g,0x413280u,a,1,&r);r.ecx=player; }
    }
    if(!periodic)return mission_pair(r.eax,r.edx);
    uint32_t status=mission_sx16(r.eax);
    if(R32(reported)!=0xffffffffu) { W32(reported,0xffffffffu);W32(last,status); }
    uint32_t code=0xffffffffu;
    if(status!=R32(last))switch(status) {
        case 6:code=0x16u;break; case 7:code=0x1eu;break; case 8:code=0x20u;break; case 9:code=0x17u;break; default:break;
    }
    if(code==0xffffffffu) { W32(last,status);return mission_pair(r.eax,code); }
    /* Event record {code, dword 0, 16 bytes never written} to the
     * EventPort's slot 0 with (port, &record). */
    uint32_t record=g->scratch,port=R32(R32(MISSION_VA(0x49b088u))+0x20bu);
    W16(record,code);W32(record+2u,0);
    for(unsigned j=0;j<16;j+=4)W32(record+6u+j,residue?R32(residue+j):0u);
    a[0]=port;a[1]=record;r.eax=R32(port);r.edx=record;r.ecx=0;
    if(g->observe)g->observe(g->observer_context,0x4136fbu,0);
    g->call(g->context,R32(r.eax),a,2,&r);
    W16(MISSION_VA(0x4a9b69u),500u);
    r.edx=R32(last);W32(reported,r.edx);
    return mission_pair(r.edx,r.edx);
}
#undef ROUTE_W8
