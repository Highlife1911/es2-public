#include "es2/shell/save.h"
#include "es2/shell/bytes.h"
#include "es2/formats/sav.h"
#include "equipment.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* SAV field values for the preparation boundary, written in our own words
 * from the byte contracts in vshell_preparation.md ("Loadout SAV contract").
 * All serialized bytes are produced explicitly little endian and framed by
 * formats:sav; no original table or value list is embedded here. */
#define TRY(call) do { es2_status status_=(call);if(status_!=ES2_OK)return status_; } while(0)

static es2_status check_tables(const es2_shell_save_tables *t,es2_error *e) {
    if(!t || !t->defaults.data || t->defaults.size!=164 || !t->item_indices.data || t->item_indices.size!=60)
        return es2_fail(e,ES2_ERR_ARGUMENT,0);
    return ES2_OK;
}
/* Item word +2: position of the ID in the executable's thirty-entry list. */
static uint16_t item_index(const es2_shell_save_tables *t,int16_t id) {
    unsigned i;
    for(i=0;i<30;i++)if(es2_le16s(t->item_indices.data+2*i)==id)return (uint16_t)i;
    return 0xffff;
}
/* Hangar word +2 is the vehicle table position; capacity comes from the
 * parallel table, as VSHELL's loaders compute it. */
static es2_status vehicle_entry(const es2_shell_save_tables *t,int16_t vehicle,
    uint16_t *index,uint16_t *capacity,es2_error *e) {
    unsigned i;
    for(i=0;i<9;i++)if(es2_le16s(t->defaults.data+128+2*i)==vehicle) {
        uint16_t n=es2_le16(t->defaults.data+146+2*i);
        if(n>10)return es2_fail(e,ES2_ERR_COUNT,i);
        *index=(uint16_t)i;*capacity=n;return ES2_OK;
    }
    return es2_fail(e,ES2_ERR_NOT_FOUND,(uint16_t)vehicle);
}
uint16_t es2_shell_mount_secondary(int16_t item) { return item>=13 && item<=15?1:5; }

/* Ten-byte item record: ID, list position, word +4, condition, secondary. */
static void item_record(uint8_t *p,const es2_shell_save_tables *t,int16_t id,
    uint16_t condition,uint16_t secondary) {
    es2_put16(p,(uint16_t)id);es2_put16(p+2,item_index(t,id));es2_put16(p+4,100);es2_put16(p+6,condition);es2_put16(p+8,secondary);
}
static void bay_fixed(uint8_t *p,int16_t vehicle,uint16_t index,uint16_t progress,
    uint16_t countdown,uint16_t capacity) {
    unsigned i;
    es2_put16(p,(uint16_t)vehicle);es2_put16(p+2,index);
    for(i=0;i<33;i++)es2_put16(p+4+2*i,100);
    es2_put16(p+70,progress);es2_put16(p+72,countdown);es2_put16(p+74,capacity);
}
typedef struct bay_bytes { uint8_t fixed[76], items[120]; } bay_bytes;

/* A ready Herc with freshly mounted items; slots are emitted in increasing
 * order, as the original hangar writer does. */
static es2_status plan_bay(const es2_shell_save_tables *t,const es2_shell_herc *h,int16_t index,
    bay_bytes *b,es2_sav_loadout *out,uint8_t used[33],es2_error *e) {
    uint16_t vindex,capacity;size_t i,n=0;unsigned seen=0,slot;
    TRY(vehicle_entry(t,h->vehicle,&vindex,&capacity,e));
    if(h->count>capacity || (h->count && !h->mounts))return es2_fail(e,ES2_ERR_COUNT,(size_t)index);
    for(i=0;i<h->count;i++) {
        const es2_shell_mount *m=&h->mounts[i];
        if(m->slot<0 || m->slot>=(int)capacity || (seen&(1u<<m->slot)) || m->item<1 || m->item>32)
            return es2_fail(e,ES2_ERR_OFFSET,(size_t)index*16+i);
        seen|=1u<<m->slot;
    }
    bay_fixed(b->fixed,h->vehicle,vindex,100,0,capacity);
    for(slot=0;slot<capacity;slot++)for(i=0;i<h->count;i++)if(h->mounts[i].slot==(int)slot) {
        const es2_shell_mount *m=&h->mounts[i];
        es2_put16(b->items+12*n,slot);
        item_record(b->items+12*n+2,t,m->item,100,m->has_secondary?m->secondary:es2_shell_mount_secondary(m->item));
        used[m->item]=1;n++;
    }
    out->index=index;out->fixed=(es2_span){b->fixed,76};out->items=(es2_span){b->items,12*n};
    out->count=n;out->offset=0;
    return ES2_OK;
}
/* Pilot suffix: bay word, active byte, rank, order, companion, seven words. */
static void assign(uint8_t *suffix,int bay,int active,int order) {
    es2_put16(suffix,(uint16_t)bay);suffix[2]=(uint8_t)active;es2_put16(suffix+5,(uint16_t)order);
}

es2_status es2_shell_plan_save(es2_span base,const es2_shell_save_tables *t,
    const es2_shell_plan *plan,uint8_t *output,size_t capacity,size_t *required,es2_error *e) {
    es2_sav s;es2_sav_pilot roster[36],player;es2_sav_loadout loadouts[4];es2_sav_edit edit;
    static const int16_t zero[1000];
    uint8_t suffixes[37][25],used[33]={0},flags[33];bay_bytes bays[4];
    uint16_t selection[6];int16_t team[2];size_t i;unsigned groups=0;
    if(!plan || !required)return es2_fail(e,ES2_ERR_ARGUMENT,0);
    TRY(check_tables(t,e));
    if(plan->team_count>3 || (plan->team_count && !plan->team))return es2_fail(e,ES2_ERR_COUNT,plan->team_count);
    /* Bytes after the reader end (GAME_R's untruncated tail) are not state;
     * the original writer never emits them, so the generated SAV omits them. */
    TRY(es2_sav_parse(base,&s,e));
    TRY(es2_sav_parse((es2_span){base.data,s.reader_end},&s,e));
    TRY(plan_bay(t,&plan->player,0,&bays[0],&loadouts[0],used,e));
    for(i=0;i<plan->team_count;i++) {
        const es2_shell_member *m=&plan->team[i];
        if(m->group>2 || m->index>11 || (groups&(1u<<m->group)))return es2_fail(e,ES2_ERR_OFFSET,i);
        groups|=1u<<m->group;
        TRY(plan_bay(t,&m->herc,(int16_t)(i+1),&bays[i+1],&loadouts[i+1],used,e));
    }
    /* Clear every earlier assignment, then bind member k to bay k / order k
     * and make it the selected pilot of its roster group. */
    for(i=0;i<36;i++) {
        TRY(es2_sav_pilot_at(&s,i,&roster[i],e));
        memcpy(suffixes[i],roster[i].suffix.data,25);assign(suffixes[i],-1,0,-1);
        roster[i].suffix=(es2_span){suffixes[i],25};
    }
    for(i=0;i<6;i++)selection[i]=es2_le16(s.roster_selection.data+2*i);
    for(i=0;i<plan->team_count;i++) {
        const es2_shell_member *m=&plan->team[i];
        assign(suffixes[m->group*12+m->index],(int)i+1,1,(int)i+1);selection[m->group]=m->index;
    }
    player=s.player;memcpy(suffixes[36],s.player.suffix.data,25);assign(suffixes[36],0,1,0);
    player.suffix=(es2_span){suffixes[36],25};
    team[0]=team[1]=(int16_t)(1+plan->team_count);
    for(i=0;i<33;i++)flags[i]=plan->flags?plan->flags[i]:
        (used[i] && !s.inventory[i].flag)?1:s.inventory[i].flag;
    memset(&edit,0,sizeof edit);
    edit.set_position=1;edit.group=plan->group;edit.position=plan->position;
    edit.inventory_flags=flags;edit.roster=roster;edit.roster_selection=selection;edit.team=team;
    edit.player=&player;edit.set_loadouts=1;edit.loadout_count=1+plan->team_count;edit.loadouts=loadouts;
    if(plan->zero_variables)edit.variables=zero;
    return es2_sav_write(&s,&edit,output,capacity,required,e);
}

/* Localized BIN catalogue entry including its NUL, bounded by the member. */
static es2_status bin_string(const es2_vfs *v,const char *language,const char *name,
    size_t index,es2_span *out,es2_error *e) {
    char path[64];es2_vfs_copy c;size_t n,base,off;const uint8_t *end;
    snprintf(path,sizeof path,"%s/%s.BIN",language?language:"GER",name);
    TRY(es2_vfs_find(v,path,&c,e));
    if(c.payload.size<8)return es2_fail(e,ES2_ERR_TRUNCATED,0);
    n=es2_le32(c.payload.data);
    if(n>(c.payload.size-8)/2 || index>=n)return es2_fail(e,ES2_ERR_COUNT,index);
    base=8+2*n;off=es2_le16(c.payload.data+8+2*index);
    if(off>=c.payload.size-base)return es2_fail(e,ES2_ERR_OFFSET,index);
    end=memchr(c.payload.data+base+off,0,c.payload.size-base-off);
    if(!end)return es2_fail(e,ES2_ERR_STRING,index);
    *out=(es2_span){c.payload.data+base+off,(size_t)(end-(c.payload.data+base+off))+1};
    return ES2_OK;
}
static es2_status table(const es2_vfs *v,const char *name,es2_dat_kind kind,es2_dat *out,es2_error *e) {
    es2_vfs_copy c;TRY(es2_vfs_find(v,name,&c,e));
    return es2_dat_parse(kind,c.payload,0,out,e);
}
/* Pilot defaults written by VSHELL's roster reset: unassigned, inactive,
 * word +0x2b = 100, remaining counters zero. */
static void pilot_suffix(uint8_t *s,int bay,int active,uint16_t rank,int order,uint16_t companion) {
    memset(s,0,25);assign(s,bay,active,order);es2_put16(s+3,rank);es2_put16(s+7,companion);es2_put16(s+9,100);
}

enum { MAX_INVENTORY = 1024 };
/* The state after VSHELL's new-career loaders. stream: roster and player
 * draws (NULL: seed-zero stream); tally and final word as given. */
static es2_status compose_default(const es2_vfs *v,const char *language,
    const es2_shell_save_tables *t,const char *player_name,uint16_t player_rank,
    int16_t group,int16_t position,es2_shell_rng *stream,uint32_t salvage,int16_t final_word,
    uint8_t *output,size_t capacity,size_t *required,es2_error *e) {
    es2_shell_options o;es2_shell_equipment q;es2_dat d;es2_sav_image m;
    es2_sav_pilot roster[36],player;es2_sav_loadout loadouts[8];bay_bytes bays[8];
    uint8_t inventory[10*MAX_INVENTORY];
    uint8_t prefixes[37][4],suffixes[37][25],five[22],selection[152],choice[12],team[4],nine[18],tally[4]={0},final[22]={0};
    static const uint8_t variables[2000];
    size_t i,j,n,at=0,name_size;unsigned bays_seen=0;
    if(!v || !player_name || !required)return es2_fail(e,ES2_ERR_ARGUMENT,0);
    TRY(check_tables(t,e));
    name_size=strlen(player_name)+1;
    if(name_size<2 || name_size>30)return es2_fail(e,ES2_ERR_LENGTH,name_size);
    memset(&o,0,sizeof o);o.default_tables=t->defaults;o.language=language;
    TRY(shell_equipment_new(v,&o,stream,&q,e));
    es2_put16(tally,(unsigned)(salvage&0xffffu));es2_put16(tally+2,(unsigned)(salvage>>16));es2_put16(final,(uint16_t)final_word);
    memset(&m,0,sizeof m);
    /* Inventory: availability flags plus one list per ID; each new item is
     * prepended, so the list holds the DAT entries of that ID in reverse. */
    TRY(table(v,"GAM/WEAPONS.DAT",ES2_DAT_SHELL_WEAPONS,&d,e));
    if(d.secondary_count>MAX_INVENTORY)return es2_fail(e,ES2_ERR_COUNT,d.secondary_count);
    for(i=0;i<d.secondary_count;i++) {
        es2_dat_item it;TRY(es2_dat_shell_item_at(&d,i,&it,e));
        if(it.item_id<0 || it.item_id>32)return es2_fail(e,ES2_ERR_OFFSET,i);
    }
    for(i=0;i<33;i++) {
        size_t start=at;
        for(j=d.secondary_count;j-->0;) {
            es2_dat_item it;TRY(es2_dat_shell_item_at(&d,j,&it,e));
            if(it.item_id==(int16_t)i){item_record(inventory+at,t,it.item_id,it.word_0x02,it.word_0x04);at+=10;}
        }
        m.inventory[i].flag=q.flags[i];m.inventory[i].count=(at-start)/10;
        m.inventory[i].items=(es2_span){inventory+start,at-start};
    }
    es2_put16(five,5);for(i=0;i<5;i++){es2_put16(five+2+4*i,(unsigned)i);es2_put16(five+4+4*i,0);}
    memset(selection,0,sizeof selection);es2_put16(selection,(uint16_t)group);es2_put16(selection+2,(uint16_t)position);
    for(i=0;i<36;i++) {
        const es2_shell_pilot *p=&q.pilots[i];
        es2_put16(prefixes[i],p->identity);es2_put16(prefixes[i]+2,p->variant);
        pilot_suffix(suffixes[i],-1,0,p->rank,-1,p->companion);
        roster[i].prefix=(es2_span){prefixes[i],4};roster[i].suffix=(es2_span){suffixes[i],25};
        TRY(bin_string(v,language,"ESNAMES",p->variant,&roster[i].name,e));
        roster[i].offset=0;
    }
    for(i=0;i<3;i++){es2_put16(choice+2*i,q.selected[i]);es2_put16(choice+6+2*i,q.next[i]);}
    es2_put16(team,(uint16_t)q.capacity);es2_put16(team+2,(uint16_t)q.count);
    es2_put16(prefixes[36],q.player.identity);es2_put16(prefixes[36]+2,q.player.variant);
    pilot_suffix(suffixes[36],0,1,player_rank,0,0);
    player.prefix=(es2_span){prefixes[36],4};player.suffix=(es2_span){suffixes[36],25};
    player.name=(es2_span){(const uint8_t *)player_name,name_size};player.offset=0;
    /* Hangar: HERCS.DAT records in file order, progress/countdown words and
     * per-slot condition/secondary as read by the DAT loadout reader. */
    TRY(table(v,"GAM/HERCS.DAT",ES2_DAT_HERCS,&d,e));
    if(d.count>8)return es2_fail(e,ES2_ERR_COUNT,d.count);
    for(i=0;i<d.count;i++) {
        es2_dat_loadout r;uint16_t vindex,cap;unsigned seen=0,slot;
        TRY(es2_dat_loadout_at(&d,i,&r,e));
        if(r.bay_slot<0 || r.bay_slot>7 || (bays_seen&(1u<<r.bay_slot)))return es2_fail(e,ES2_ERR_OFFSET,i);
        bays_seen|=1u<<r.bay_slot;
        TRY(vehicle_entry(t,r.herc_id,&vindex,&cap,e));
        if(r.slot_count>cap)return es2_fail(e,ES2_ERR_COUNT,i);
        for(j=0;j<r.slot_count;j++) {
            es2_dat_loadout_slot s;TRY(es2_dat_loadout_slot_at(&r,j,&s,e));
            if(s.slot_id<0 || s.slot_id>=(int)cap || (seen&(1u<<s.slot_id)) || s.item_id<0 || s.item_id>32)
                return es2_fail(e,ES2_ERR_OFFSET,i);
            seen|=1u<<s.slot_id;
        }
        bay_fixed(bays[i].fixed,r.herc_id,vindex,r.word_0x02,r.word_0x04,cap);
        n=0;
        for(slot=0;slot<cap;slot++)for(j=0;j<r.slot_count;j++) {
            es2_dat_loadout_slot s;TRY(es2_dat_loadout_slot_at(&r,j,&s,e));
            if(s.slot_id!=(int)slot)continue;
            es2_put16(bays[i].items+12*n,slot);item_record(bays[i].items+12*n+2,t,s.item_id,s.word_0x04,s.word_0x06);n++;
        }
        loadouts[i].index=r.bay_slot;loadouts[i].fixed=(es2_span){bays[i].fixed,76};
        loadouts[i].items=(es2_span){bays[i].items,12*n};loadouts[i].count=n;loadouts[i].offset=0;
    }
    m.loadout_count=d.count;m.loadouts=loadouts;
    /* Nine-word bank: HERC_INF word +14 of each record, in file order. */
    TRY(table(v,"GAM/HERC_INF.DAT",ES2_DAT_HERC_INF,&d,e));
    if(d.count!=9)return es2_fail(e,ES2_ERR_COUNT,d.count);
    for(i=0;i<9;i++){es2_dat_herc_info h;TRY(es2_dat_herc_info_at(&d,i,&h,e));es2_put16(nine+2*i,h.word_0x0e);}
    m.five_entry_bank=(es2_span){five,22};m.selection=(es2_span){selection,152};m.roster=roster;
    m.roster_selection=(es2_span){choice,12};m.team=(es2_span){team,4};m.player=&player;
    m.nine_word_bank=(es2_span){nine,18};m.tally=(es2_span){tally,4};m.variables=(es2_span){variables,2000};
    m.final_fields=(es2_span){final,22};
    return es2_sav_compose(&m,output,capacity,required,e);
}
es2_status es2_shell_default_save(const es2_vfs *v,const char *language,
    const es2_shell_save_tables *t,const char *player_name,uint16_t player_rank,
    int16_t group,int16_t position,uint8_t *output,size_t capacity,size_t *required,es2_error *e) {
    return compose_default(v,language,t,player_name,player_rank,group,position,NULL,0,0,
                           output,capacity,required,e);
}

/* New career 0x40e8a5 (vshell_ui.md, section 4.2): final word 2, roster and
 * player drawn from the preparation RNG, then the salvage total 100,000 +
 * 1,000 x draw(11) from the same stream (0x40e906..0x40e924). The bank is
 * zero. The stream advances only when the SAV is written. */
es2_status es2_shell_new_game(const es2_vfs *v,const char *language,
    const es2_shell_save_tables *t,const char *player_name,uint16_t player_rank,
    int16_t group,int16_t position,es2_shell_rng *rng,uint8_t *output,size_t capacity,
    size_t *required,uint32_t *salvage,es2_error *e) {
    es2_shell_rng r;es2_status status;uint16_t x=0;uint32_t total;
    if(!rng || !required)return es2_fail(e,ES2_ERR_ARGUMENT,0);
    r=*rng;
    /* The roster draws come first; run them on a copy to know the salvage
     * draw that follows before composing. */
    {
        es2_shell_options o;es2_shell_equipment q;
        TRY(check_tables(t,e));
        memset(&o,0,sizeof o);o.default_tables=t->defaults;o.language=language;
        TRY(shell_equipment_new(v,&o,&r,&q,e));
    }
    TRY(es2_shell_rng_draw(&r,11,&x,e));
    total=100000u+1000u*x;
    {
        es2_shell_rng roster=*rng;
        status=compose_default(v,language,t,player_name,player_rank,group,position,&roster,total,2,
                               output,capacity,required,e);
    }
    if(status!=ES2_OK)return status;
    if(output && capacity){*rng=r;if(salvage)*salvage=total;}
    return ES2_OK;
}

es2_status es2_shell_prepared_save(es2_span base,int16_t group,int16_t position,const es2_shell_state *after,
    const es2_shell_output *prepared,uint8_t *output,size_t capacity,size_t *required,es2_error *e) {
    es2_sav save,written;es2_sav_edit edit;es2_sav_pilot roster[36];uint8_t suffix[36][25];
    int16_t team[2],variables[1000];size_t i,need=0,at;es2_status status;
    if(!after || !prepared || !required || !prepared->has_briefing || !prepared->variables.data ||
       prepared->variables.size!=2000 || !after->equipment.initialized)return es2_fail(e,ES2_ERR_ARGUMENT,0);
    status=es2_sav_parse(base,&save,e);if(status!=ES2_OK)return status;
    status=es2_sav_parse((es2_span){base.data,save.reader_end},&save,e);if(status!=ES2_OK)return status;
    for(i=0;i<36;i++) {
        status=es2_sav_pilot_at(&save,i,&roster[i],e);if(status!=ES2_OK)return status;
        memcpy(suffix[i],roster[i].suffix.data,25);suffix[i][2]=after->equipment.pilots[i].active;
        roster[i].suffix=(es2_span){suffix[i],25};
    }
    for(i=0;i<1000;i++)variables[i]=es2_le16s(prepared->variables.data+2*i);
    team[0]=after->equipment.capacity;team[1]=after->equipment.count;
    memset(&edit,0,sizeof edit);edit.variables=variables;edit.roster=roster;edit.team=team;
    edit.set_position=1;edit.group=group;edit.position=position;
    status=es2_sav_write(&save,&edit,NULL,0,&need,e);if(status!=ES2_OK)return status;
    if(!output || !capacity){*required=need;es2_error_clear(e);return ES2_OK;}
    if(capacity<need){*required=need;return es2_fail(e,ES2_ERR_CAPACITY,need);}
    status=es2_sav_write(&save,&edit,output,capacity,&need,e);if(status!=ES2_OK)return status;
    /* The selection span (152 bytes) keeps its offset: only fixed-size
     * fields before it. Its last 148 bytes are the briefing selection. */
    status=es2_sav_parse((es2_span){output,need},&written,e);if(status!=ES2_OK)return status;
    at=(size_t)(written.selection.data-output)+4;
    memcpy(output+at,prepared->briefing,148);
    *required=need;es2_error_clear(e);return ES2_OK;
}

es2_status es2_shell_loaded_save(es2_span file,uint8_t *output,size_t capacity,size_t *required,es2_error *e) {
    es2_sav s;es2_sav_image m;es2_sav_pilot roster[36];es2_sav_loadout loadouts[8];
    uint8_t *items=NULL;size_t total=0,at=0,i,j;es2_status status;
    if(!required)return es2_fail(e,ES2_ERR_ARGUMENT,0);
    status=es2_sav_parse(file,&s,e);if(status!=ES2_OK)return status;
    if(s.loadout_count>8)return es2_fail(e,ES2_ERR_COUNT,s.loadout_count);
    memset(&m,0,sizeof m);
    for(i=0;i<33;i++)total+=s.inventory[i].items.size;
    items=(uint8_t *)malloc(total+1);
    if(!items)return es2_fail(e,ES2_ERR_CAPACITY,total);
    /* 0x412781 prepends every record it reads: the list, and so the next
     * write, holds the file's records of a category in reverse order. */
    for(i=0;i<33;i++) {
        const es2_sav_inventory *c=&s.inventory[i];size_t start=at;
        for(j=c->count;j-->0;){memcpy(items+at,c->items.data+10*j,10);at+=10;}
        m.inventory[i].flag=c->flag;m.inventory[i].count=c->count;m.inventory[i].items=(es2_span){items+start,at-start};
    }
    for(i=0;i<36 && status==ES2_OK;i++)status=es2_sav_pilot_at(&s,i,&roster[i],e);
    for(i=0;i<s.loadout_count && status==ES2_OK;i++)status=es2_sav_loadout_at(&s,i,&loadouts[i],e);
    if(status==ES2_OK) {
        m.five_entry_bank=s.five_entry_bank;m.selection=s.selection;m.roster=roster;
        m.roster_selection=s.roster_selection;m.team=s.team;m.player=&s.player;
        m.loadout_count=s.loadout_count;m.loadouts=loadouts;m.nine_word_bank=s.nine_word_bank;
        m.tally=s.tally;m.variables=s.variables;m.final_fields=s.final_fields;
        status=es2_sav_compose(&m,output,capacity,required,e);
    }
    free(items);return status;
}
