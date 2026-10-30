#include "equipment.h"
#include "es2/shell/bytes.h"
#include "es2/formats/sav.h"
#include <stdio.h>
#include <string.h>

/* Own state projection derived from the documented VSHELL byte contracts.
 * Tables and all authored values stay runtime inputs. */
static es2_status table(const es2_vfs *v,const char *name,es2_dat_kind kind,es2_dat *out,es2_error *e) {
    es2_vfs_copy c;es2_status s=es2_vfs_find(v,name,&c,e);
    return s==ES2_OK?es2_dat_parse(kind,c.payload,0,out,e):s;
}
static es2_status catalogue(const es2_vfs *v,const char *lang,const char *name,size_t minimum,es2_error *e) {
    char path[64];es2_vfs_copy c;size_t i,base,n;es2_status s;
    snprintf(path,sizeof path,"%s/%s.BIN",lang?lang:"GER",name);
    s=es2_vfs_find(v,path,&c,e);if(s!=ES2_OK)return s;
    if(c.payload.size<8)return es2_fail(e,ES2_ERR_TRUNCATED,0);
    n=es2_le32(c.payload.data);
    if(n<minimum || n>(c.payload.size-8)/2)return es2_fail(e,ES2_ERR_COUNT,0);
    base=8+2*n;
    if(es2_le32(c.payload.data+4)!=c.payload.size-base)return es2_fail(e,ES2_ERR_COUNT,4);
    for(i=0;i<n;i++) {
        size_t off=es2_le16(c.payload.data+8+2*i);
        if(off>=c.payload.size-base || !memchr(c.payload.data+base+off,0,c.payload.size-base-off))
            return es2_fail(e,ES2_ERR_STRING,8+2*i);
    }
    return ES2_OK;
}
static es2_status capacity(const es2_shell_options *o,int16_t vehicle,uint16_t *out,es2_error *e) {
    size_t i;
    if(!o->default_tables.data || o->default_tables.size!=164)return es2_fail(e,ES2_ERR_ARGUMENT,0);
    for(i=0;i<9;i++)if(es2_le16s(o->default_tables.data+128+2*i)==vehicle) {
        uint16_t n=es2_le16(o->default_tables.data+146+2*i);
        if(n>10)return es2_fail(e,ES2_ERR_COUNT,i);
        *out=n;return ES2_OK;
    }
    return es2_fail(e,ES2_ERR_NOT_FOUND,(uint16_t)vehicle);
}
static es2_status bay_init(const es2_shell_options *o,es2_shell_bay *b,int16_t vehicle,es2_error *e) {
    size_t i;uint16_t n;es2_status s=capacity(o,vehicle,&n,e);if(s!=ES2_OK)return s;
    memset(b,0,sizeof *b);b->present=1;b->vehicle=vehicle;b->capacity=n;
    for(i=0;i<33;i++)b->condition[i]=100;
    for(i=0;i<10;i++)b->secondary[i]=5;
    return ES2_OK;
}
static es2_status dat_bay(const es2_shell_options *o,const es2_dat_loadout *r,es2_shell_bay *b,es2_error *e) {
    unsigned seen=0;size_t i;es2_status s=bay_init(o,b,r->herc_id,e);if(s!=ES2_OK)return s;
    for(i=0;i<r->slot_count;i++) {
        es2_dat_loadout_slot slot;s=es2_dat_loadout_slot_at(r,i,&slot,e);if(s!=ES2_OK)return s;
        if(slot.slot_id<0 || slot.slot_id>=b->capacity || (seen&(1u<<slot.slot_id)) || slot.item_id<0 || slot.item_id>32)
            return es2_fail(e,ES2_ERR_OFFSET,i);
        seen|=1u<<slot.slot_id;b->items[slot.slot_id]=slot.item_id;b->secondary[slot.slot_id]=slot.word_0x06;
    }
    return ES2_OK;
}
static uint16_t draw(es2_shell_rng *rng,int16_t bound) {
    uint16_t n=0;(void)es2_shell_rng_draw(rng,bound,&n,NULL);return n;
}
static void permutation(es2_shell_rng *rng,int *out,int n) {
    int i,j,k;for(i=0;i<n;i++)out[i]=-1;
    for(i=0;i<n;i++) {
        k=draw(rng,(int16_t)(n-i));
        for(j=0;j<n;j++)if(out[j]==-1 && k--==0){out[j]=i;break;}
    }
}
es2_status shell_equipment_default(const es2_vfs *v,const es2_shell_options *o,es2_shell_equipment *out,es2_error *e) {
    return shell_equipment_new(v,o,NULL,out,e);
}
/* Roster and player draws come from *stream (advanced only on success), or
 * from a fresh seed-zero stream when stream is NULL. */
es2_status shell_equipment_new(const es2_vfs *v,const es2_shell_options *o,es2_shell_rng *stream,
    es2_shell_equipment *out,es2_error *e) {
    es2_shell_equipment q={0};es2_shell_rng rng;es2_dat d;es2_status s;size_t i,g,j,k;
    if(!o->default_tables.data || o->default_tables.size!=164)return es2_fail(e,ES2_ERR_ARGUMENT,0);
    s=catalogue(v,o->language,"ESNAMES",36,e);if(s!=ES2_OK)return s;
    s=catalogue(v,o->language,"WEAPONS",33,e);if(s!=ES2_OK)return s;
    s=table(v,"GAM/WEAPONS.DAT",ES2_DAT_SHELL_WEAPONS,&d,e);if(s!=ES2_OK)return s;
    for(i=0;i<d.count;i++) {
        es2_dat_shell_weapon w;s=es2_dat_shell_weapon_at(&d,i,&w,e);if(s!=ES2_OK)return s;
        if(w.weapon_id<0 || w.weapon_id>=33)return es2_fail(e,ES2_ERR_OFFSET,i);
        q.flags[w.weapon_id]=w.byte_after_word;
    }
    if(stream){if(stream->a>=56 || stream->b>=56)return es2_fail(e,ES2_ERR_ARGUMENT,0);rng=*stream;}
    else{s=es2_shell_rng_init((es2_span){o->default_tables.data,112},0,&rng,e);if(s!=ES2_OK)return s;}
    for(g=0;g<3;g++) {
        int ids[4],variants[3];permutation(&rng,ids,4);permutation(&rng,variants,3);
        for(j=0;j<3;j++)for(k=0;k<4;k++) {
            es2_shell_pilot *p=&q.pilots[g*12+j*4+k];uint16_t r=draw(&rng,100);size_t rank;
            p->identity=(uint16_t)(g*4+(size_t)ids[k]);p->variant=(uint16_t)(p->identity*3+variants[j]);
            for(rank=0;rank<4;rank++)if((int)r<es2_le16s(o->default_tables.data+112+2*rank))break;
            if(rank==4)rank=0;
            p->rank=(uint16_t)rank;p->companion=es2_le16(o->default_tables.data+120+2*rank);p->bay=p->order=-1;
        }
        q.next[g]=1;
    }
    q.player.variant=draw(&rng,11);q.player.active=1;q.count=1;
    s=table(v,"GAM/HERCS.DAT",ES2_DAT_HERCS,&d,e);if(s!=ES2_OK)return s;
    for(i=0;i<d.count;i++) {
        es2_dat_loadout r;s=es2_dat_loadout_at(&d,i,&r,e);if(s!=ES2_OK)return s;
        if(r.bay_slot<0 || r.bay_slot>=8 || q.bays[r.bay_slot].present)return es2_fail(e,ES2_ERR_OFFSET,i);
        s=dat_bay(o,&r,&q.bays[r.bay_slot],e);if(s!=ES2_OK)return s;
    }
    q.initialized=1;*out=q;if(stream)*stream=rng;return ES2_OK;
}
static es2_shell_pilot *ordered(es2_shell_equipment *q,int order) {
    size_t i;for(i=0;i<3;i++) {
        es2_shell_pilot *p=&q->pilots[i*12+q->selected[i]];
        if(p->order==order)return p;
    }
    return NULL;
}
static void adjust(es2_shell_equipment *q,int order) {
    es2_shell_pilot *p=ordered(q,order);int active;if(!p)return;
    active=order<q->capacity && p->bay!=-1;
    if(active && !p->active)q->count++;
    if(!active && p->active)q->count--;
    p->active=(uint8_t)active;
}
es2_status shell_equipment_mission(const es2_vfs *v,const es2_shell_options *o,es2_shell_equipment *q,size_t n,const uint8_t *const actors[4],es2_error *e) {
    size_t i,j;es2_status s;
    if(n<1 || n>4)return es2_fail(e,ES2_ERR_COUNT,n);
    for(i=0;i<3;i++)if(q->selected[i]>=12)return es2_fail(e,ES2_ERR_OFFSET,i);
    q->capacity=(int16_t)n;
    for(i=1;i<4;i++)adjust(q,(int)i);
    if(o->mode!=0)return ES2_OK;
    memset(q->bays,0,sizeof q->bays);
    for(i=0;i<n;i++) {
        if(i==0 && o->training_preset_index>3 && !o->training_preset_flag) {
            es2_dat d;es2_dat_loadout r;
            if(!o->training_preset_path || o->training_vehicle>8)return es2_fail(e,ES2_ERR_ARGUMENT,0);
            s=table(v,o->training_preset_path,ES2_DAT_LOADOUT,&d,e);if(s!=ES2_OK)return s;
            s=es2_dat_loadout_at(&d,0,&r,e);if(s!=ES2_OK)return s;
            s=dat_bay(o,&r,&q->bays[i],e);if(s!=ES2_OK)return s;
        } else {
            if(!actors[i])return es2_fail(e,ES2_ERR_NOT_FOUND,i);
            s=bay_init(o,&q->bays[i],es2_le16s(actors[i]+48),e);if(s!=ES2_OK)return s;
            for(j=0;j<q->bays[i].capacity;j++) {
                int16_t item=es2_le16s(actors[i]+50+2*j);
                if(item) {q->bays[i].items[j]=item;q->bays[i].secondary[j]=es2_le16(actors[i]+116+2*j);}
            }
        }
        if(i==0)q->player.bay=0;
        else {
            es2_shell_pilot *p=&q->pilots[(i-1)*12+q->selected[i-1]];
            p->bay=(int16_t)i;adjust(q,(int)i-1);p->order=(int16_t)i;
        }
    }
    return ES2_OK;
}
es2_status shell_equipment_project(const es2_shell_equipment *q,es2_shell_loadout *load,es2_shell_actor actors[4],es2_error *e) {
    size_t n=0,i,j;const es2_shell_pilot *pilots[4]={&q->player,NULL,NULL,NULL};
    if(q->capacity<1 || q->capacity>4)return es2_fail(e,ES2_ERR_COUNT,0);
    for(i=1;i<(size_t)q->capacity;i++)for(j=0;j<3;j++) {
        const es2_shell_pilot *p=&q->pilots[j*12+q->selected[j]];
        if(p->order==(int)i){if(p->active)pilots[i]=p;break;}
    }
    for(i=0;i<(size_t)q->capacity;i++)if(pilots[i]) {
        const es2_shell_pilot *p=pilots[i];const es2_shell_bay *b;
        if(p->bay<0 || p->bay>=8 || !q->bays[p->bay].present)return es2_fail(e,ES2_ERR_NOT_FOUND,i);
        b=&q->bays[p->bay];memset(&actors[n],0,sizeof actors[n]);
        actors[n].pilot[0]=p->variant;actors[n].pilot[1]=p->rank;actors[n].vehicle=b->vehicle;
        actors[n].capacity=b->capacity;actors[n].items=b->items;actors[n].secondary=b->secondary;
        memcpy(actors[n].condition,b->condition,sizeof b->condition);n++;
    }
    if(n!=(size_t)q->count)return es2_fail(e,ES2_ERR_COUNT,n);
    load->actors=actors;load->count=n;memcpy(load->flags,q->flags,33);return ES2_OK;
}
static es2_shell_pilot saved_pilot(const es2_sav_pilot *p) {
    es2_shell_pilot q;
    q.identity=es2_le16(p->prefix.data);q.variant=es2_le16(p->prefix.data+2);q.bay=es2_le16s(p->suffix.data);
    q.active=p->suffix.data[2];q.rank=es2_le16(p->suffix.data+3);q.order=es2_le16s(p->suffix.data+5);q.companion=es2_le16(p->suffix.data+7);
    return q;
}
es2_status es2_shell_restore_save(es2_span bytes,es2_shell_state *out,es2_error *e) {
    es2_sav save;es2_shell_state state;es2_shell_equipment *q;size_t i,j;es2_status s;
    if(!out)return es2_fail(e,ES2_ERR_ARGUMENT,0);
    s=es2_sav_parse(bytes,&save,e);if(s!=ES2_OK)return s;
    state=*out;q=&state.equipment;memset(q,0,sizeof *q);
    for(i=0;i<33;i++)q->flags[i]=save.inventory[i].flag;
    for(i=0;i<36;i++) {es2_sav_pilot p;s=es2_sav_pilot_at(&save,i,&p,e);if(s!=ES2_OK)return s;q->pilots[i]=saved_pilot(&p);}
    for(i=0;i<3;i++) {
        q->selected[i]=es2_le16(save.roster_selection.data+2*i);q->next[i]=es2_le16(save.roster_selection.data+6+2*i);
        if(q->selected[i]>=12 || q->next[i]>=12)return es2_fail(e,ES2_ERR_OFFSET,i);
    }
    q->capacity=es2_le16s(save.team.data);q->count=es2_le16s(save.team.data+2);q->player=saved_pilot(&save.player);
    for(i=0;i<save.loadout_count;i++) {
        es2_sav_loadout r;es2_shell_bay *b;s=es2_sav_loadout_at(&save,i,&r,e);if(s!=ES2_OK)return s;
        if(r.index<0 || r.index>=8)return es2_fail(e,ES2_ERR_OFFSET,i);
        b=&q->bays[r.index];if(b->present)return es2_fail(e,ES2_ERR_OFFSET,i);
        b->present=1;b->vehicle=es2_le16s(r.fixed.data);b->capacity=es2_le16(r.fixed.data+74);
        if(b->capacity>10 || r.count>b->capacity)return es2_fail(e,ES2_ERR_COUNT,i);
        for(j=0;j<33;j++)b->condition[j]=es2_le16(r.fixed.data+4+2*j);
        for(j=0;j<10;j++)b->secondary[j]=5;
        {unsigned seen=0;for(j=0;j<r.count;j++) {
            const uint8_t *p=r.items.data+12*j;size_t slot=es2_le16(p);
            if(slot>=b->capacity || (seen&(1u<<slot)))return es2_fail(e,ES2_ERR_OFFSET,r.offset+76+12*j);
            seen|=1u<<slot;b->items[slot]=es2_le16s(p+2);b->secondary[slot]=es2_le16(p+10);
        }}
    }
    for(i=0;i<1000;i++)state.variables[i]=es2_le16s(save.variables.data+2*i);
    q->initialized=1;*out=state;return ES2_OK;
}
