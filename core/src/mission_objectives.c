#include "es2/core/mission_objectives.h"
#include "fixed.h"
/* Objectives, route completion and the periodic outcome policy
 * (docs/reverse-engineering/mission_runtime.md). Registers are tracked as
 * the original leaves them, because they become the entry registers of the
 * next game call. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
/* A DBSIM VA (globals, tables, direct callees) inside the image at g->image. */
#define OBJ_VA(va) (g->image+(uint32_t)(va)-0x400000u)
static uint64_t obj_pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
/* A 16-bit register write: the high word stays. */
static uint32_t obj_word(uint32_t high,uint32_t low) { return (high&0xffff0000u)|(uint16_t)low; }
static uint32_t obj_sx16(uint32_t x) { return (uint32_t)fx_s16(x); }
static uint32_t obj_sx8(uint32_t x) { return (uint32_t)(int32_t)(int8_t)(uint8_t)x; }
static int obj_less(uint32_t a,uint32_t b) { return fx_s32(a)<fx_s32(b); }
/* A direct call names the original VA (observed for the callee counters). */
static void obj_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,OBJ_VA(va),a,n,r);
}
/* The group's current order: [group + 4*[group+6C] + 44h]. */
static uint32_t obj_order(const es2_guest_memory *m,uint32_t group) {
    return R32(group+4u*R32(group+0x6cu)+0x44u);
}

/* Original: DBSIM.EXE 0x423b0c */
uint64_t es2_route_point_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t holder,uint32_t index,const uint32_t entry[3]) {
    (void)g;(void)entry;
    uint32_t route=R32(holder+2u);
    if(!route||fx_s16(index)>=fx_s16(R16(route)))return obj_pair(0,0);
    uint32_t point=R32(R32(route+2u)+4u*obj_sx16(index));
    return obj_pair(point,point);
}

/* Original: DBSIM.EXE 0x423918 */
uint64_t es2_order_target_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t group,uint32_t actor,const uint32_t entry[3]) {
    (void)g;(void)entry;
    uint32_t index=R32(group+0x6cu),order=R32(group+4u*index+0x44u);
    if(!order)return obj_pair(0,index);
    uint32_t scope=obj_sx16(R16(order+0xcu));
    if(scope==0)return obj_pair(R32(actor+0x45u)==R32(order+0xeu),actor);
    if(scope-1u<3u)return obj_pair(actor==R32(order+0xeu),actor);
    return obj_pair(0,index);
}

/* Original: DBSIM.EXE 0x41373c */
uint64_t es2_bounds_outside_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t position,uint32_t margin,const uint32_t entry[3]) {
    (void)entry;
    uint32_t rect=OBJ_VA(0x4aa640u),x=R32(position),y=R32(position+4u);
    /* MOV AX,1 over the rectangle address loaded into EAX. */
    uint64_t outside=obj_pair(obj_word(rect,1),position);
    if(!obj_less(R32(rect)-margin,x))return outside;
    if(!obj_less(x,R32(rect+8u)+margin))return outside;
    if(!obj_less(R32(rect+4u)-margin,y))return outside;
    if(!obj_less(y,margin+R32(rect+12u)))return outside;
    return obj_pair(0,position);
}

/* Original: DBSIM.EXE 0x41379c */
uint64_t es2_bounds_check_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t actor,const uint32_t entry[3]) {
    (void)m;
    es2_weapon_registers r={actor+0x26u,entry[1],entry[2]};
    uint32_t a[2]={actor+0x26u,0};obj_call(g,0x41373cu,a,2,&r);
    return obj_pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x411c58 */
uint64_t es2_target_known_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t observer,uint32_t target,const uint32_t entry[3]) {
    es2_weapon_registers r={target+0x92u,entry[1],entry[2]};
    if(R8(target+0x95u)) {
        uint32_t a[2]={observer+0x26u,target+0x26u};
        r.eax=a[0];obj_call(g,0x49288cu,a,2,&r);
        if(obj_less(r.eax,1000000u))return obj_pair(obj_word(r.eax,1),r.edx);
    }
    uint32_t k=obj_sx16(R16(target+0x4bu));
    if(R8(observer+0xc2u+k))return obj_pair(obj_word(k,1),observer+0xc2u);
    return obj_pair(0,observer+0xc2u);
}

/* Original: DBSIM.EXE 0x4137b4 */
uint64_t es2_actor_safe_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t actor,const uint32_t entry[3]) {
    uint32_t player=R32(OBJ_VA(0x4d24e6u)),cursor=R32(OBJ_VA(0x4a9af8u));
    es2_weapon_registers r={entry[0],player,R32(player+0x45u)};
    uint32_t same=r.ecx==R32(actor+0x45u);
    r.eax=same;
    for(uint32_t i=0;;) {
        r.edx=obj_word(r.edx,i);
        uint32_t index=i,other=0;i=(uint16_t)(i+1u);
        if(fx_s16(index)<fx_s16(R16(OBJ_VA(0x4a9afeu)))) { r.ecx=cursor;cursor+=4u;other=R32(r.ecx); }
        if(!other)break;
        /* Pending group, same category, or not alive: next actor. */
        uint32_t group=R32(other+0x45u);
        r.eax=group;r.edx=R32(group+0x14u)!=0;
        if(r.edx)continue;
        r.ecx=obj_sx8(R8(group+0x12u));
        r.eax=R32(actor+0x45u);r.edx=obj_sx8(R8(r.eax+0x12u));
        if((uint16_t)r.ecx==(uint16_t)r.edx)continue;
        r.eax=other+0x92u;
        if(R8(other+0xa5u)) { r.edx=1;continue; }
        r.edx=other+0x92u;
        if(R8(other+0xa4u)||R8(other+0x99u)) { r.eax=1;r.edx=1;continue; }
        r.eax=0;r.edx=0;
        uint32_t a[2]={other,actor};obj_call(g,0x411c58u,a,2,&r);
        uint32_t known=r.eax;
        if(!(uint16_t)same) {
            a[0]=R32(other+0x45u);a[1]=actor;
            r.eax=actor;r.edx=a[0];obj_call(g,0x423918u,a,2,&r);
            if((uint16_t)r.eax) {
                /* Route end: MOV DX,[holder] over 423918's EDX, INC EDX. */
                uint32_t holder=R32(other+0x45u)+4u;
                r.eax=holder;r.ecx=holder;r.edx=obj_word(r.edx,R16(holder))+1u;
                a[0]=holder;a[1]=r.edx;obj_call(g,0x423b0cu,a,2,&r);
                r.eax=r.eax==0;
                if(!(uint16_t)r.eax||(uint16_t)known)return obj_pair(0,r.edx);
                continue;
            }
        }
        if(!(uint16_t)known)continue;
        uint32_t threshold=actor==R32(other+0x1a4u)?100000u:80000u;
        a[0]=actor+0x26u;a[1]=other+0x26u;
        r.eax=a[1];r.edx=a[0];obj_call(g,0x49288cu,a,2,&r);
        if(obj_less(r.eax,threshold))return obj_pair(0,r.edx);
    }
    return obj_pair(obj_word(r.eax,1),r.edx);
}

/* Original: DBSIM.EXE 0x412e58 */
uint64_t es2_subject_done_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t group,const uint32_t entry[3]) {
    (void)m;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t a[1]={group};obj_call(g,0x412dc4u,a,1,&r);
    return obj_pair((uint16_t)r.eax==4,r.edx);
}

/* Original: DBSIM.EXE 0x412e74 */
uint64_t es2_rivals_done_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t group,const uint32_t entry[3]) {
    uint32_t count=R16(OBJ_VA(0x4a9e04u)),next=R32(OBJ_VA(0x4a9e00u));
    es2_weapon_registers r={obj_word(entry[0],count),next,R32(group+0x6cu)};
    uint32_t own=obj_order(m,group);
    for(;;) {
        uint32_t left=count,other=0;
        count=(uint16_t)(count-1u);
        if(left) { other=next;next+=0x7au; }
        r.eax=other;
        if(!other)break;
        if(other==group)continue;
        r.edx=(r.edx&0xffffff00u)|R8(other+0x12u);
        if(R8(other+0x12u)==R8(group+0x12u))continue;
        r.ecx=R32(other+0x6cu);r.edx=R32(other+4u*r.ecx+0x44u);
        if(!r.edx)continue;
        r.ecx=R32(r.edx+0xeu);
        if(r.ecx!=R32(own+0xeu))continue;
        uint32_t a[1]={other};obj_call(g,0x412be4u,a,1,&r);r.ecx=other;
        if(!(uint16_t)r.eax)return obj_pair(0,r.edx);
    }
    return obj_pair(1,r.edx);
}

/* Original: DBSIM.EXE 0x4239fc */
uint64_t es2_order_done_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t group,uint32_t order,const uint32_t entry[3]) {
    es2_weapon_registers r={0,order,entry[2]};
    uint32_t type=obj_sx16(R16(order)),a[2]={group,0};
    if(type==0) {
        r.ecx=0xffffffffu;obj_call(g,0x412e58u,a,1,&r);
        return obj_pair(r.eax,r.edx);
    }
    if(type==2) {
        r.ecx=0;
        if(!R32(order+0xeu))return obj_pair(0,order);
        obj_call(g,0x412e58u,a,1,&r);r.ecx=group;
        if(r.eax)return obj_pair(1,r.edx);
        obj_call(g,0x412e74u,a,1,&r);
        return obj_pair((uint16_t)r.eax!=0,r.edx);
    }
    if(type==3||type==5||type==6) {
        r.ecx=type-7u+(type==3?4u:0u);
        r.eax=(uint32_t)R16(group+4u)+1u;
        a[0]=group+4u;a[1]=r.eax;obj_call(g,0x423b0cu,a,2,&r);
        return obj_pair(r.eax==0,r.edx);
    }
    return obj_pair(0,order);
}

/* Original: DBSIM.EXE 0x413180 */
uint64_t es2_status_eval_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t player,uint32_t force,const uint32_t entry[3]) {
    es2_weapon_registers r={obj_word(entry[0],R16(OBJ_VA(0x4a9e54u))),player,entry[2]};
    uint32_t a[3]={player,r.eax,force};obj_call(g,0x4135e8u,a,3,&r);
    if((uint16_t)force)W16(OBJ_VA(0x4a9e4cu),r.eax);
    return obj_pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x4131ac */
uint64_t es2_outcome_poll_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t player,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t a[2]={OBJ_VA(0x4a9e65u),0};
    obj_call(g,0x467a48u,a,1,&r);r.ecx=a[0];
    a[0]=OBJ_VA(0x4a9e62u);obj_call(g,0x467a48u,a,1,&r);r.ecx=a[0];
    r.eax=(uint16_t)r.eax==0;
    if(!r.eax) {
        /* Evaluation countdown still running: only a bounds violation. */
        a[0]=player;obj_call(g,0x41379cu,a,1,&r);r.ecx=player;
        if(!(uint16_t)r.eax)return obj_pair(0xffffffffu,r.edx);
    }
    a[0]=player;a[1]=0;obj_call(g,0x413180u,a,2,&r);
    uint32_t status=obj_sx16(r.eax);r.edx=status;
    if(R16(OBJ_VA(0x49935cu)+2u*status)&&(uint16_t)r.eax!=R16(OBJ_VA(0x4a9e4cu))) {
        if(!R16(OBJ_VA(0x4a9e68u))) { W16(OBJ_VA(0x4a9e68u),1);W16(OBJ_VA(0x4a9e66u),10000); }
        /* Grace countdown running: only status 2 passes. */
        if(!R16(OBJ_VA(0x4a9e66u))||(uint16_t)r.eax==2) {
            W16(OBJ_VA(0x4a9e4cu),r.eax);
            return obj_pair(r.eax,r.edx);
        }
    }
    W16(OBJ_VA(0x4a9e63u),10000);
    return obj_pair(0xffffffffu,r.edx);
}

/* 413280's predicate for one objective; returns ESI (unchanged when the
 * scope or type assigns none). */
static uint32_t objective_predicate(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t player,uint32_t objective,uint32_t esi,es2_weapon_registers *r) {
    uint32_t scope=obj_sx16(R16(objective+4u)),subject=R32(objective+6u),a[2]={subject,0};
    if(scope-1u<3u) {
        /* Actor subject (scopes 1..3), ECX = scope-4. */
        r->ecx=scope-4u;r->eax=obj_sx16(R16(objective+2u));
        switch(r->eax) {
        case 0:
            a[0]=R32(subject+0x45u);a[1]=R32(objective+0xeu);r->edx=a[0];
            obj_call(g,0x41324cu,a,2,r);r->ecx=obj_sx16(r->eax);return r->ecx;
        case 1: {
            r->eax=subject+0x92u;
            uint32_t v=1;
            if(!R8(subject+0xa4u)) { r->edx=subject+0x92u;v=R8(subject+0x99u)!=0; }
            r->eax=r->edx=v;return v; }
        case 2: obj_call(g,0x4137b4u,a,1,r);r->ecx=subject;return obj_sx16(r->eax);
        case 3: case 4:
            r->eax=player+0x92u;r->edx=(r->edx&0xffffff00u)|R8(player+0xa0u);return obj_sx8(R8(player+0xa0u));
        case 6:
            r->eax=subject+0x92u;r->edx=(r->edx&0xffffff00u)|R8(subject+0x9eu);return obj_sx8(R8(subject+0x9eu));
        case 7:
            r->eax=subject+0x92u;r->edx=(r->edx&0xffffff00u)|R8(subject+0xa5u);return obj_sx8(R8(subject+0xa5u));
        case 8: r->eax=subject+0x92u;r->edx=R8(subject+0x9eu)==0;return r->edx;
        case 9: case 10: r->eax=player+0x92u;r->edx=R8(player+0xa0u)==0;return r->edx;
        default: return esi;
        }
    }
    if(scope) { r->ecx=scope-4u;return esi; }
    /* Group subject (scope 0), ECX = -1. */
    r->ecx=0xffffffffu;r->eax=obj_sx16(R16(objective+2u));
    switch(r->eax) {
    case 0:
        a[1]=R32(objective+0xeu);r->edx=a[1];
        obj_call(g,0x41324cu,a,2,r);return obj_sx16(r->eax);
    case 1: obj_call(g,0x413920u,a,1,r);r->ecx=subject;return r->eax;
    case 2: obj_call(g,0x413950u,a,1,r);r->ecx=subject;return obj_sx16(r->eax);
    case 3: case 4:
        r->eax=player+0x92u;r->edx=(r->edx&0xffffff00u)|R8(player+0xa0u);return obj_sx8(R8(player+0xa0u));
    case 6: obj_call(g,0x412d90u,a,1,r);r->ecx=subject;return obj_sx16(r->eax);
    case 7: obj_call(g,0x412c58u,a,1,r);r->ecx=subject;return obj_sx16(r->eax);
    case 8: obj_call(g,0x412d90u,a,1,r);r->ecx=subject;r->eax=(uint16_t)r->eax==0;return r->eax;
    case 9: case 10: r->eax=player+0x92u;r->edx=R8(player+0xa0u)==0;return r->edx;
    default: return esi;
    }
}

/* Original: DBSIM.EXE 0x413280 */
uint64_t es2_objective_eval_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t player,uint32_t esi,const uint32_t entry[3]) {
    es2_weapon_registers r={0,0,entry[2]};
    uint32_t fail=0,ok=1,objective=R32(OBJ_VA(0x4a9e40u)),bank=OBJ_VA(0x4a9e70u);
    for(uint32_t i=0;;++i,objective+=0x4cu) {
        r.eax=obj_sx16(R16(OBJ_VA(0x4a9e3cu)));
        if(!obj_less(i,r.eax))break;
        esi=objective_predicate(m,g,player,objective,esi,&r);
        if(R16(objective)==1) {
            /* First unmet required objective: its string array for the alert. */
            if(!esi&&ok) { ok=0;r.eax=objective+0x12u;W32(OBJ_VA(0x4d1e98u),r.eax); }
        } else if(esi) fail=1;
        if(!esi||R16(objective+0x22u))continue;
        W16(objective+0x22u,1);
        r.edx=objective+0x24u;r.eax=objective+0x38u;
        for(r.ecx=0;r.ecx<10;++r.ecx,r.edx+=2u,r.eax+=2u) {
            if(fx_s16(R16(r.edx))<=-1||fx_s16(R16(r.eax))<=-1)continue;
            uint32_t cell=bank+2u*obj_sx16(R16(r.edx));
            switch(R16(r.eax)) {
            case 4: W16(cell,1);break;
            case 5: W16(cell,0);break;
            case 6: W16(cell,R16(cell)+1u);break;
            case 7: W16(cell,R16(cell)-1u);break;
            default: break;
            }
        }
    }
    /* The player's own safety decides between the paired statuses. */
    uint32_t a[1]={player};r.edx=player;obj_call(g,0x4137b4u,a,1,&r);
    uint32_t safe=(uint16_t)r.eax!=0;
    if(fail) { uint32_t v=safe?6u:10u;return obj_pair(v,v); }
    if(ok)return obj_pair(safe?9u:10u,r.edx);
    uint32_t v=safe?5u:4u;return obj_pair(v,v);
}

/* Objective predicates (mission_runtime.md, section "objective predicates").
 * ECX is outside every result contract (each caller pops or overwrites it) and
 * is tracked only where it enters a game call. */

/* Original: DBSIM.EXE 0x412bd0 */
uint64_t es2_subject_group_registers(const es2_guest_memory *m,uint32_t group) {
    uint32_t index=R32(group+0x6cu);
    return obj_pair(R32(R32(group+4u*index+0x44u)+0xeu),index);
}

/* Original: DBSIM.EXE 0x412bbc (byte-identical to 412bd0) */
uint64_t es2_subject_actor_registers(const es2_guest_memory *m,uint32_t group) {
    const uint32_t current=R32(group+0x6cu),order=R32(group+0x44u+4u*current);
    return obj_pair(R32(order+0xeu),current);
}

/* Original: DBSIM.EXE 0x412be4 */
uint64_t es2_group_down_registers(const es2_guest_memory *m,uint32_t group) {
    uint32_t slot=R32(group+0xcu);
    for(uint32_t i=0;fx_s16(i)<fx_s16(R16(group+0x10u));++i,slot+=4u) {
        uint32_t actor=R32(slot);
        /* Alive (+0A5h, +0A4h and +99h clear, read in this order): not down. */
        if(!R8(actor+0xa5u)&&!R8(actor+0xa4u)&&!R8(actor+0x99u))return obj_pair(0,slot);
    }
    /* MOV AX,1 over the group pointer reloaded for the loop test. */
    return obj_pair(obj_word(group,1),slot);
}

/* Original: DBSIM.EXE 0x412c58 */
uint64_t es2_group_flagged_registers(const es2_guest_memory *m,uint32_t group) {
    uint32_t slot=R32(group+0xcu),i=0;
    for(;fx_s16(i)<fx_s16(R16(group+0x10u));++i,slot+=4u)
        if(!R8(R32(slot)+0xa5u))return obj_pair(0,i);
    return obj_pair(obj_word(slot,1),i);
}

/* Original: DBSIM.EXE 0x412d90 */
uint64_t es2_group_marked_registers(const es2_guest_memory *m,uint32_t group) {
    uint32_t cursor=R32(group+0xcu),k=0;
    for(;fx_s16(k)<fx_s16(R16(group+0x10u));++k,cursor+=4u)
        if(R8(R32(cursor)+0x9eu))return obj_pair(obj_word(cursor,1),k);
    return obj_pair(0,k);
}

/* Original: DBSIM.EXE 0x41324c */
uint64_t es2_route_flag_registers(const es2_guest_memory *m,uint32_t group,uint32_t route) {
    uint32_t flag=group+0x70u,slot=group+0x44u;
    for(uint32_t i=0;i<10u;++i,++flag,slot+=4u) {
        uint32_t order=R32(slot);
        if(order&&route==R32(order+8u))return obj_pair(obj_sx8(R8(flag)),i);
    }
    return obj_pair(0,10);
}

/* Original: DBSIM.EXE 0x412dc4 */
uint64_t es2_subject_status_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t group,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t a[1]={group};
    r.eax=R32(group+0x6cu);r.edx=R32(group+4u*r.eax+0x44u);
    if(!R16(r.edx+0xcu)) {
        /* Group subject: its state (group_state) is the status. */
        obj_call(g,0x412bd0u,a,1,&r);r.ecx=group;
        a[0]=r.eax;obj_call(g,0x412c8cu,a,1,&r);
        return obj_pair(r.eax,r.edx);
    }
    /* Actor subject, fetched again before each test. */
    obj_call(g,0x412bbcu,a,1,&r);r.ecx=group;
    if(R8(r.eax+0x99u))return obj_pair(obj_word(r.eax,4),r.edx);
    obj_call(g,0x412bbcu,a,1,&r);r.ecx=group;
    if(R8(r.eax+0xb4u))return obj_pair(obj_word(r.eax,4),r.edx);
    obj_call(g,0x412bbcu,a,1,&r);r.ecx=group;
    uint32_t actor=r.eax;r.edx=R32(actor);
    uint32_t target=R32(r.edx+0x40u);a[0]=actor;
    if(g->observe)g->observe(g->observer_context,target,0x40u);
    g->call(g->context,target,a,1,&r);
    const int32_t level=fx_s16(r.eax);
    if(level<0x32)return obj_pair(0,r.edx);
    return obj_pair(obj_word(r.eax,level<0x80?1u:level<0xc0?2u:3u),r.edx);
}

/* Original: DBSIM.EXE 0x413920 */
uint64_t es2_state_reached_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t group,const uint32_t entry[3]) {
    const int32_t threshold=R8(group+0x12u)?4:3;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t a[1]={group};obj_call(g,0x412c8cu,a,1,&r);
    return obj_pair(threshold<=fx_s16(r.eax),r.edx);
}

/* Original: DBSIM.EXE 0x413950 */
uint64_t es2_group_safe_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t group,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t a[1]={group};obj_call(g,0x413920u,a,1,&r);r.ecx=group;
    if(r.eax)return obj_pair(0,r.edx);
    for(uint32_t i=0;fx_s16(i)<fx_s16(R16(group+0x10u));++i) {
        r.eax=R32(R32(group+0xcu)+4u*obj_sx16(i));r.edx=r.eax+0x92u;
        if(R8(r.eax+0x99u))continue;
        a[0]=r.eax;obj_call(g,0x4137b4u,a,1,&r);r.ecx=a[0];
        if(!(uint16_t)r.eax)return obj_pair(0,r.edx);
    }
    /* MOV AX,1 over 413920's zero, the last skipped actor or 4137b4's EAX. */
    return obj_pair(obj_word(r.eax,1),r.edx);
}

/* Original: DBSIM.EXE 0x413780 */
uint64_t es2_bounds_margin_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t actor,const uint32_t entry[3]) {
    (void)m;
    es2_weapon_registers r={actor+0x26u,entry[1],entry[2]};
    uint32_t a[2]={actor+0x26u,110000u};obj_call(g,0x41373cu,a,2,&r);
    return obj_pair(r.eax,r.edx);
}
