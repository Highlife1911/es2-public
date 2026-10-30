#include "es2/shell/campaign.h"
#include "es2/shell/bytes.h"
#include "es2/shell/hangar.h"
#include "es2/formats/dat.h"
#include "es2/formats/sav.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Campaign advancement written in our own words from the byte contract in
 * vshell_preparation.md ("Results path", "Debrief selection").
 * The state is the projection VSHELL's SAV readers restore and its writers
 * emit; every serialized byte is read and written explicitly little endian.
 * Arithmetic follows the original widths: 16-bit words wrap, the total is a
 * 32-bit dword, divisions truncate toward zero. Inputs on which the original
 * faults or reads outside an object are rejected (safe API policy). */
#define TRY(call) do { es2_status status_=(call);if(status_!=ES2_OK)return status_; } while(0)
static int16_t wrap(long x) { unsigned long u=(unsigned long)x&65535ul; return (int16_t)(u<32768ul?(long)u:(long)u-65536l); }
static int32_t wrap32(int64_t x) { uint32_t u=(uint32_t)((uint64_t)x&0xffffffffu); return u<0x80000000u?(int32_t)u:(int32_t)((int64_t)u-0x100000000ll); }

enum { MAX_ITEMS = 32767 };
typedef struct item { int16_t w[5]; } item;           /* ID, index, +4, +6, +8 */
/* Items in file order: the list head (written first) is the last element. */
typedef struct category { uint8_t flag; uint16_t cost; int16_t priority; size_t n, cap; item *v; } category;
/* Pilot suffix bytes are the runtime fields +0x22..+0x3a. */
typedef struct pilot { uint8_t prefix[4], name[30], suffix[25]; size_t name_size; } pilot;
typedef struct bay { int present, used[10]; uint8_t fixed[76]; int16_t occupied; item slot[10]; } bay;
typedef struct world {
    category cat[33];
    int16_t five_count, five[5];
    uint8_t selection[152], final_fields[22];
    pilot roster[36], player;
    uint16_t selected[3], next[3];
    int16_t capacity, count, hangar_count, hercs_lost, nine[9], bank[1000];
    bay bays[8];
    uint32_t total;
    /* tables */
    int16_t groups[24], threshold[6], level[6], unlock_value[33], unlock_var[33], var_items[29], indices[30];
    int16_t herc[9][8], value[9][33], item_value[33];
    es2_error *e;
} world;

static int16_t pw(const pilot *p,unsigned off) { return es2_le16s(p->suffix+off-0x22); }
static void ps(pilot *p,unsigned off,long v) { es2_put16(p->suffix+off-0x22,(uint16_t)wrap(v)); }
static int active(const pilot *p) { return p->suffix[2]!=0; }
static int16_t cw(const bay *b,unsigned i) { return es2_le16s(b->fixed+4+2*i); }
static void cs(bay *b,unsigned i,long v) { es2_put16(b->fixed+4+2*i,(uint16_t)wrap(v)); }
static int16_t bw(const bay *b,unsigned off) { return es2_le16s(b->fixed+off); }

static es2_status prepend(world *w,int16_t id,const item *it) {
    category *c;
    if(id<0 || id>32)return es2_fail(w->e,ES2_ERR_OFFSET,(size_t)(uint16_t)id);
    c=&w->cat[id];
    if(c->n>=MAX_ITEMS)return es2_fail(w->e,ES2_ERR_COUNT,(size_t)id);
    if(c->n==c->cap) {
        size_t cap=c->cap?c->cap*2:16;item *v=(item *)realloc(c->v,cap*sizeof *v);
        if(!v)return es2_fail(w->e,ES2_ERR_CAPACITY,0);
        c->v=v;c->cap=cap;
    }
    c->v[c->n++]=*it;return ES2_OK;
}
static int16_t item_index(const world *w,int16_t id) {
    int i;for(i=0;i<30;i++)if(w->indices[i]==id)return (int16_t)i;
    return -1;
}
static es2_status new_item(world *w,int16_t id,int16_t a,int16_t b,int16_t c) {
    item it;it.w[0]=id;it.w[1]=item_index(w,id);it.w[2]=a;it.w[3]=b;it.w[4]=c;
    return prepend(w,id,&it);
}

/* ---- SAV projection ------------------------------------------------------ */
static void pilot_in(pilot *p,const es2_sav_pilot *s) {
    memcpy(p->prefix,s->prefix.data,4);memcpy(p->name,s->name.data,s->name.size);
    p->name_size=s->name.size;memcpy(p->suffix,s->suffix.data,25);
}
/* Bounds of the loader-safe subset the original restores without reading
 * outside its objects (pilot bay/order, selection indices, vehicle words). */
static es2_status check_pilot(const pilot *p,size_t which,es2_error *e) {
    int16_t bay=pw(p,0x22),order=pw(p,0x27);
    if((bay!=-1 && (bay<0 || bay>7)) || (order!=-1 && (order<0 || order>3)))return es2_fail(e,ES2_ERR_OFFSET,which);
    return ES2_OK;
}
static es2_status load_save(world *w,es2_span bytes) {
    es2_sav s;size_t i,j;es2_reader r;
    TRY(es2_sav_parse(bytes,&s,w->e));
    for(i=0;i<33;i++) {
        category *c=&w->cat[i];c->flag=s.inventory[i].flag;
        for(j=0;j<s.inventory[i].count;j++) {
            const uint8_t *p=s.inventory[i].items.data+10*j;item it;size_t k;
            for(k=0;k<5;k++)it.w[k]=es2_le16s(p+2*k);
            /* The composed SAV requires each item in the list of its ID. */
            if(it.w[0]!=(int16_t)i)return es2_fail(w->e,ES2_ERR_OFFSET,i);
            TRY(prepend(w,(int16_t)i,&it));
        }
    }
    w->five_count=es2_le16s(s.five_entry_bank.data);
    for(i=0;i<5;i++) {
        int16_t index=es2_le16s(s.five_entry_bank.data+2+4*i);
        if(index<0 || index>4)return es2_fail(w->e,ES2_ERR_OFFSET,i);
        w->five[index]=es2_le16s(s.five_entry_bank.data+4+4*i);
    }
    memcpy(w->selection,s.selection.data,152);
    for(i=0;i<36;i++){es2_sav_pilot p;TRY(es2_sav_pilot_at(&s,i,&p,w->e));pilot_in(&w->roster[i],&p);TRY(check_pilot(&w->roster[i],i,w->e));}
    for(i=0;i<3;i++) {
        w->selected[i]=es2_le16(s.roster_selection.data+2*i);w->next[i]=es2_le16(s.roster_selection.data+6+2*i);
        if(w->selected[i]>11 || w->next[i]>11)return es2_fail(w->e,ES2_ERR_OFFSET,i);
    }
    w->capacity=es2_le16s(s.team.data);w->count=es2_le16s(s.team.data+2);
    pilot_in(&w->player,&s.player);TRY(check_pilot(&w->player,36,w->e));
    w->hangar_count=(int16_t)s.loadout_count;
    TRY(es2_reader_init(&r,s.loadouts,w->e));
    for(i=0;i<s.loadout_count;i++) {
        es2_sav_loadout l;bay *b;
        TRY(es2_sav_loadout_at(&s,i,&l,w->e));
        if(l.index<0 || l.index>7 || w->bays[l.index].present)return es2_fail(w->e,ES2_ERR_OFFSET,i);
        b=&w->bays[l.index];b->present=1;memcpy(b->fixed,l.fixed.data,76);
        if(bw(b,0)<0 || bw(b,0)>8 || bw(b,2)<0 || bw(b,2)>8 || bw(b,74)<0 || bw(b,74)>10 || (size_t)bw(b,74)<l.count)
            return es2_fail(w->e,ES2_ERR_OFFSET,i);
        b->occupied=(int16_t)l.count;
        for(j=0;j<l.count;j++) {
            const uint8_t *p=l.items.data+12*j;int16_t slot=es2_le16s(p);size_t k;
            if(slot<0 || slot>=bw(b,74) || b->used[slot] || es2_le16s(p+2)<0 || es2_le16s(p+2)>32)return es2_fail(w->e,ES2_ERR_OFFSET,i);
            b->used[slot]=1;for(k=0;k<5;k++)b->slot[slot].w[k]=es2_le16s(p+2+2*k);
        }
    }
    for(i=0;i<9;i++)w->nine[i]=es2_le16s(s.nine_word_bank.data+2*i);
    w->total=es2_le32(s.tally.data);
    memcpy(w->final_fields,s.final_fields.data,22);
    return ES2_OK;
}
static es2_status save_out(world *w,es2_shell_buffer *out) {
    es2_sav_image m;es2_sav_pilot roster[36],player;es2_sav_loadout loadouts[8];
    uint8_t five[22],choice[12],team[4],nine[18],tally[4],variables[2000],bays[8][196];
    uint8_t *inventory=NULL;size_t total=0,at=0,i,j,n=0,required=0;es2_status status;
    memset(&m,0,sizeof m);
    for(i=0;i<33;i++)total+=w->cat[i].n;
    inventory=(uint8_t *)malloc(10*total+1);
    if(!inventory)return es2_fail(w->e,ES2_ERR_CAPACITY,0);
    for(i=0;i<33;i++) {
        const category *c=&w->cat[i];size_t start=at;
        for(j=c->n;j-->0;){size_t k;for(k=0;k<5;k++)es2_put16(inventory+at+2*k,(uint16_t)c->v[j].w[k]);at+=10;}
        m.inventory[i].flag=c->flag;m.inventory[i].count=c->n;m.inventory[i].items=(es2_span){inventory+start,at-start};
    }
    es2_put16(five,(uint16_t)w->five_count);
    for(i=0;i<5;i++){es2_put16(five+2+4*i,(uint16_t)i);es2_put16(five+4+4*i,(uint16_t)w->five[i]);}
    for(i=0;i<36;i++) {
        roster[i].prefix=(es2_span){w->roster[i].prefix,4};roster[i].name=(es2_span){w->roster[i].name,w->roster[i].name_size};
        roster[i].suffix=(es2_span){w->roster[i].suffix,25};roster[i].offset=0;
    }
    player.prefix=(es2_span){w->player.prefix,4};player.name=(es2_span){w->player.name,w->player.name_size};
    player.suffix=(es2_span){w->player.suffix,25};player.offset=0;
    for(i=0;i<3;i++){es2_put16(choice+2*i,w->selected[i]);es2_put16(choice+6+2*i,w->next[i]);}
    es2_put16(team,(uint16_t)w->capacity);es2_put16(team+2,(uint16_t)w->count);
    for(i=0;i<8;i++) {
        const bay *b=&w->bays[i];size_t k=0;
        if(!b->present)continue;
        memcpy(bays[i],b->fixed,76);
        for(j=0;j<10;j++)if(b->used[j]){size_t q;es2_put16(bays[i]+76+12*k,(uint16_t)j);for(q=0;q<5;q++)es2_put16(bays[i]+78+12*k+2*q,(uint16_t)b->slot[j].w[q]);k++;}
        /* The writer emits the occupied count word, then the occupied slots. */
        if((size_t)b->occupied!=k){free(inventory);return es2_fail(w->e,ES2_ERR_COUNT,i);}
        loadouts[n].index=(int16_t)i;loadouts[n].fixed=(es2_span){bays[i],76};
        loadouts[n].items=(es2_span){bays[i]+76,12*k};loadouts[n].count=k;loadouts[n].offset=0;n++;
    }
    if((size_t)w->hangar_count!=n){free(inventory);return es2_fail(w->e,ES2_ERR_COUNT,n);}
    for(i=0;i<9;i++)es2_put16(nine+2*i,(uint16_t)w->nine[i]);
    es2_put32(tally,w->total);
    for(i=0;i<1000;i++)es2_put16(variables+2*i,(uint16_t)w->bank[i]);
    m.five_entry_bank=(es2_span){five,22};m.selection=(es2_span){w->selection,152};m.roster=roster;
    m.roster_selection=(es2_span){choice,12};m.team=(es2_span){team,4};m.player=&player;
    m.loadout_count=n;m.loadouts=loadouts;m.nine_word_bank=(es2_span){nine,18};
    m.tally=(es2_span){tally,4};m.variables=(es2_span){variables,2000};m.final_fields=(es2_span){w->final_fields,22};
    status=es2_sav_compose(&m,NULL,0,&required,w->e);
    if(status==ES2_OK) {
        out->data=(uint8_t *)malloc(required);
        if(!out->data)status=es2_fail(w->e,ES2_ERR_CAPACITY,0);
        else {status=es2_sav_compose(&m,out->data,required,&required,w->e);out->size=required;}
        if(status!=ES2_OK){free(out->data);out->data=NULL;out->size=0;}
    }
    free(inventory);return status;
}

/* ---- Runtime tables -------------------------------------------------------- */
static int32_t fixed10(int32_t a,int32_t b) { return wrap32(((int64_t)a*b)>>10); }
static es2_status load_tables(world *w,const es2_vfs *v,es2_span t) {
    es2_vfs_copy c;es2_dat d;es2_dat_damage dmg;size_t i,j;
    if(!t.data || t.size!=ES2_SHELL_RESULTS_TABLES_SIZE)return es2_fail(w->e,ES2_ERR_ARGUMENT,0);
    for(i=0;i<24;i++)w->groups[i]=es2_le16s(t.data+2*i);
    for(i=0;i<6;i++){w->threshold[i]=es2_le16s(t.data+48+2*i);w->level[i]=es2_le16s(t.data+60+2*i);}
    for(i=0;i<33;i++){w->unlock_value[i]=es2_le16s(t.data+72+2*i);w->unlock_var[i]=es2_le16s(t.data+138+2*i);}
    for(i=0;i<29;i++)w->var_items[i]=es2_le16s(t.data+204+2*i);
    for(i=0;i<30;i++)w->indices[i]=es2_le16s(t.data+262+2*i);
    TRY(es2_vfs_find(v,"GAM/HERC_INF.DAT",&c,w->e));TRY(es2_dat_parse(ES2_DAT_HERC_INF,c.payload,0,&d,w->e));
    if(d.count!=9)return es2_fail(w->e,ES2_ERR_COUNT,d.count);
    for(i=0;i<9;i++) {
        es2_dat_herc_info h;TRY(es2_dat_herc_info_at(&d,i,&h,w->e));
        w->herc[i][0]=h.herc_id;w->herc[i][1]=h.word_0x02;w->herc[i][2]=h.word_0x04;w->herc[i][3]=h.word_0x06;
        w->herc[i][4]=h.word_0x08;w->herc[i][5]=(int16_t)h.word_0x0a;w->herc[i][6]=h.build_progress_divisor;w->herc[i][7]=0;
    }
    /* 0x413c4c: per HERC_INF record, words 0..5 and 13..21 of a 33-word row;
     * item values from the second coefficient. */
    TRY(es2_vfs_find(v,"GAM/DAMAGE.DAT",&c,w->e));TRY(es2_dat_parse(ES2_DAT_DAMAGE,c.payload,0,&d,w->e));
    TRY(es2_dat_damage_get(&d,&dmg,w->e));
    memset(w->value,0,sizeof w->value);
    for(i=0;i<9;i++) {
        int32_t base=(int32_t)w->herc[i][4]*1000;
        for(j=0;j<6;j++)w->value[i][j]=wrap(fixed10(dmg.common_coefficient,fixed10(dmg.coefficients_0x02[j],base)));
        for(j=0;j<9;j++)w->value[i][13+j]=wrap(fixed10(dmg.common_coefficient,fixed10(dmg.coefficients_0x0e[j],base)));
    }
    for(i=0;i<33;i++)w->item_value[i]=wrap(fixed10(dmg.second_common_coefficient,dmg.item_coefficients[i]));
    /* Categories: cost (u16)(raw*1000) and priority word by category ID. */
    TRY(es2_vfs_find(v,"GAM/WEAPONS.DAT",&c,w->e));TRY(es2_dat_parse(ES2_DAT_SHELL_WEAPONS,c.payload,0,&d,w->e));
    for(i=0;i<d.count;i++) {
        es2_dat_shell_weapon s;TRY(es2_dat_shell_weapon_at(&d,i,&s,w->e));
        if(s.weapon_id<0 || s.weapon_id>32)return es2_fail(w->e,ES2_ERR_OFFSET,i);
        w->cat[s.weapon_id].cost=(uint16_t)((uint32_t)s.word_after_name*1000u);
        w->cat[s.weapon_id].priority=(int16_t)s.final_word;
    }
    return ES2_OK;
}

/* ---- Condition helpers (0x41232a family 0/1/2, 0x4121f8) ------------------ */
/* 0x41224d: the part group's listed words, summed with 16-bit wrap, the sum
 * sign-extended and divided by the list length. */
static es2_status group_average(const int16_t groups[24],const int16_t words[33],unsigned group,int16_t *out,
                                es2_error *e) {
    unsigned at,n=0;long sum=0;
    if(group>5)return es2_fail(e,ES2_ERR_UNSUPPORTED,group);
    for(at=4*group;at<24 && groups[at]!=-1;at++,n++) {
        int16_t index=groups[at];
        if(index<0 || index>32)return es2_fail(e,ES2_ERR_OFFSET,at);
        sum=wrap(sum+words[index]);
    }
    if(at==24 || !n)return es2_fail(e,ES2_ERR_UNSUPPORTED,group);
    *out=wrap((long)(int16_t)sum/(long)n);return ES2_OK;
}
es2_status es2_shell_condition(const int16_t groups[24],const int16_t words[33],int16_t family,int16_t index,
                               int16_t *out,es2_error *e) {
    long at;
    if(!words || !out || (family==0 && !groups))return es2_fail(e,ES2_ERR_ARGUMENT,0);
    if(family==0)return group_average(groups,words,(unsigned)(uint16_t)index,out,e);
    if(family!=1 && family!=2)return es2_fail(e,ES2_ERR_ARGUMENT,(size_t)(uint16_t)family);
    at=(family==1?13L:23L)+index;
    if(at<0 || at>32)return es2_fail(e,ES2_ERR_OFFSET,(size_t)(uint16_t)index);
    *out=words[at];return ES2_OK;
}
/* Original: VSHELL.EXE 0x4121f8 (0x412203..0x41224c): cx accumulates the
 * words (add cx, 16-bit), the loops are signed against the capacity word,
 * then movsx/cdq/idiv by capacity + 0x16. */
es2_status es2_shell_overall_condition(const int16_t words[33],int16_t capacity,int16_t *out,es2_error *e) {
    long sum=0;int i;
    if(!words || !out)return es2_fail(e,ES2_ERR_ARGUMENT,0);
    if(capacity<0 || capacity>10)return es2_fail(e,ES2_ERR_ARGUMENT,(size_t)(uint16_t)capacity);
    for(i=0;i<13;i++)sum=wrap(sum+words[i]);
    for(i=0;i<9;i++)sum=wrap(sum+words[13+i]);
    for(i=0;i<capacity;i++)sum=wrap(sum+words[23+i]);
    *out=wrap((long)(int16_t)sum/(long)(capacity+22));return ES2_OK;
}
es2_status es2_shell_record_condition(es2_span fixed,int16_t *out,es2_error *e) {
    int16_t words[33];int i;
    if(!fixed.data || fixed.size<76)return es2_fail(e,ES2_ERR_TRUNCATED,fixed.size);
    for(i=0;i<33;i++)words[i]=es2_le16s(fixed.data+4+2*i);
    return es2_shell_overall_condition(words,es2_le16s(fixed.data+74),out,e);
}
static void bay_words(const bay *b,int16_t words[33]) { unsigned i;for(i=0;i<33;i++)words[i]=cw(b,i); }
static es2_status part(world *w,const bay *b,unsigned group,int16_t *out) {
    int16_t words[33];
    bay_words(b,words);
    return group_average(w->groups,words,group,out,w->e);
}
static int16_t overall(const bay *b) {
    int16_t words[33],out=0;
    bay_words(b,words);
    (void)es2_shell_overall_condition(words,bw(b,74),&out,NULL);   /* the SAV reader bounds the capacity to 0..10 */
    return out;
}
static int threshold_index(const world *w,int16_t x) {
    int i;for(i=0;i<6;i++)if(x>=w->threshold[i])return i;
    return 0;
}
static int16_t slot_item(const bay *b,unsigned s) { return b->used[s]?b->slot[s].w[0]:0; }

/* 0x411db9: slots below capacity; condition >= 80 returns the item to its
 * category, lower conditions free it; the slot condition becomes 100. */
static es2_status return_items(world *w,bay *b) {
    unsigned s;
    for(s=0;s<(unsigned)bw(b,74);s++) {
        if(!b->used[s])continue;
        if(cw(b,23+s)>=80)TRY(prepend(w,b->slot[s].w[0],&b->slot[s]));
        b->used[s]=0;cs(b,23+s,100);
    }
    return ES2_OK;
}
/* 0x414174: value of a lost Herc; rows indexed by the vehicle word +0. */
static es2_status lost_value(world *w,const bay *b,int32_t *out) {
    int id=bw(b,0);int32_t sum=0;unsigned i;
    if(bw(b,70)<100){*out=wrap32((int64_t)wrap32((int64_t)((100-bw(b,70))/100)*w->herc[id][4])*1000);return ES2_OK;}
    for(i=0;i<6;i++){int16_t c;TRY(part(w,b,i,&c));sum=wrap32((int64_t)sum+wrap32((int64_t)c*w->value[id][i])/100);}
    for(i=0;i<9;i++)sum=wrap32((int64_t)sum+wrap32((int64_t)cw(b,13+i)*w->value[id][13+i])/100);
    for(i=0;i<(unsigned)bw(b,74);i++) {
        int16_t c=cw(b,23+i);
        if(c<80)sum=wrap32((int64_t)sum+wrap32((int64_t)c*w->item_value[slot_item(b,i)])/100);
    }
    *out=sum;return ES2_OK;
}

typedef struct cursor { es2_span data; size_t at; } cursor;
static es2_status take(world *w,cursor *r,uint8_t *out,size_t n) {
    if(r->data.size-r->at<n)return es2_fail(w->e,ES2_ERR_TRUNCATED,r->at);
    memcpy(out,r->data.data+r->at,n);r->at+=n;return ES2_OK;
}
static pilot *selected(world *w,int g) { return &w->roster[g*12+w->selected[g]]; }
static int find_order(world *w,int order) {
    int g;for(g=0;g<3;g++)if(pw(selected(w,g),0x27)==order)return g;
    return -1;
}

/* Steps 4/5 of the contract for one pilot (0x411d44, 0x410632, 0x4112a0). */
static es2_status consume(world *w,pilot *p,cursor *r) {
    int16_t k=pw(p,0x22);uint8_t words[66];bay *b;unsigned s;
    if(k<0 || k>7)return es2_fail(w->e,ES2_ERR_OFFSET,(size_t)(uint16_t)k);
    b=&w->bays[k];
    if(b->present) {
        TRY(take(w,r,words,66));memcpy(b->fixed+4,words,66);
        for(s=0;s<10;s++)if(b->used[s] && cw(b,23+s)==0){b->used[s]=0;cs(b,23+s,100);b->occupied=wrap(b->occupied-1);}
    }
    ps(p,0x2b,b->present?cw(b,22):100);
    TRY(take(w,r,words,6));
    ps(p,0x2d,es2_le16s(words));ps(p,0x31,es2_le16s(words+2));ps(p,0x2f,es2_le16s(words+4));
    ps(p,0x33,pw(p,0x33)+pw(p,0x2d));ps(p,0x37,pw(p,0x37)+pw(p,0x31));ps(p,0x35,pw(p,0x35)+pw(p,0x2f));
    ps(p,0x39,pw(p,0x39)+1);
    if(!b->present)return es2_fail(w->e,ES2_ERR_NOT_FOUND,(size_t)k); /* null loadout write */
    if(overall(b)>29)cs(b,22,100);
    else {
        int32_t value;TRY(lost_value(w,b,&value));TRY(return_items(w,b));
        b->present=0;w->hangar_count=wrap(w->hangar_count-1);w->hercs_lost=wrap(w->hercs_lost+1);
        w->total+=(uint32_t)value;
    }
    return ES2_OK;
}
/* 0x41068a: rank and +0x29 counters. */
static void promote(pilot *p,int player) {
    int m,n;
    if(!active(p))return;
    if(pw(p,0x27)){m=10;n=15;}else{m=20;n=10;}
    if(pw(p,0x25)<3 && !player && ((int)pw(p,0x33)+pw(p,0x35))%m==0)ps(p,0x25,pw(p,0x25)+1);
    if(pw(p,0x29)<3 && pw(p,0x39)%n==0)ps(p,0x29,pw(p,0x29)+1);
}
/* 0x410173 then 0x41033b: next roster pilot replaces a lost one. */
static void replace(world *w,int g) {
    pilot *p;
    w->selected[g]=w->next[g];w->next[g]=(uint16_t)((w->next[g]+1)%12);
    p=selected(w,g);
    ps(p,0x22,-1);p->suffix[2]=0;ps(p,0x27,-1);ps(p,0x2b,100);
    ps(p,0x2d,0);ps(p,0x2f,0);ps(p,0x31,0);ps(p,0x33,0);ps(p,0x35,0);ps(p,0x37,0);ps(p,0x39,0);
}

/* ---- Repair (0x41194c, 0x41401b, 0x413f4e, 0x4119d3) ---------------------- */
static int32_t weigh(const world *w,int16_t weight,int16_t c,int16_t x) {
    if(c>=x)return 0;
    return wrap32((int64_t)((int32_t)w->level[threshold_index(w,x)]-c)*weight)/100;
}
static es2_status repair_cost(world *w,const bay *b,int16_t x,int32_t *out) {
    int t=bw(b,2);int32_t sum=0;unsigned i;
    for(i=0;i<6;i++){int16_t c;TRY(part(w,b,i,&c));sum=wrap32((int64_t)sum+weigh(w,w->value[t][i],c,x));}
    for(i=0;i<9;i++)sum=wrap32((int64_t)sum+weigh(w,w->value[t][13+i],cw(b,13+i),x));
    for(i=0;i<(unsigned)bw(b,74);i++) {
        int16_t id=slot_item(b,i);
        if(id && cw(b,23+i))sum=wrap32((int64_t)sum+weigh(w,w->item_value[id],cw(b,23+i),x));
    }
    *out=sum;return ES2_OK;
}
static es2_status repair(world *w,bay *b,int32_t budget) {
    int j=0;int32_t cost;unsigned i;
    TRY(repair_cost(w,b,w->level[0],&cost));
    while((uint32_t)cost>(uint32_t)budget && j<threshold_index(w,overall(b))){j++;TRY(repair_cost(w,b,w->level[j],&cost));}
    if((uint32_t)cost>=(uint32_t)budget)return ES2_OK;
    for(i=0;i<13;i++)cs(b,i,w->level[j]);
    for(i=0;i<9;i++)cs(b,13+i,w->level[j]);
    for(i=0;i<(unsigned)bw(b,74);i++)cs(b,23+i,slot_item(b,i)?w->level[j]:100);
    w->total-=(uint32_t)cost;return ES2_OK;
}
static es2_status repairs(world *w,uint8_t option) {
    int32_t budget;int o;
    if(!w->count || ((int32_t)w->total==INT32_MIN && w->count==-1))return es2_fail(w->e,ES2_ERR_ARGUMENT,0);
    budget=(int32_t)w->total/(int32_t)w->count;
    if(!option) {
        int16_t k=pw(&w->player,0x22);
        if(k<0 || k>7)return es2_fail(w->e,ES2_ERR_OFFSET,(size_t)(uint16_t)k);
        if(w->bays[k].present)TRY(repair(w,&w->bays[k],budget));
    }
    if(option==2)return ES2_OK;
    for(o=1;o<w->capacity;o++) {
        int g=find_order(w,o);int16_t k;
        if(g<0 || !active(selected(w,g)))continue;
        k=pw(selected(w,g),0x22);
        if(k<0 || k>7)return es2_fail(w->e,ES2_ERR_OFFSET,(size_t)(uint16_t)k);
        if(w->bays[k].present)TRY(repair(w,&w->bays[k],budget));
    }
    return ES2_OK;
}

/* ---- Purchases and restock (0x412a4c, 0x412a37) --------------------------- */
static es2_status category_cost(world *w,int16_t id,uint16_t *cost,uint8_t *flag,int16_t *count) {
    if(id==-1) {
        /* 0x412930 found no category: the original reads the bytes before
         * category 0, which belong to HERC_INF record 8 (availability = high
         * byte of its word +8). Only the harmless zero case is supported. */
        if((uint16_t)w->herc[8][4]>>8)return es2_fail(w->e,ES2_ERR_UNSUPPORTED,0);
        *flag=0;*cost=0;*count=0;return ES2_OK;
    }
    if(id<0 || id>32)return es2_fail(w->e,ES2_ERR_OFFSET,(size_t)(uint16_t)id);
    *cost=w->cat[id].cost;*flag=w->cat[id].flag;*count=(int16_t)w->cat[id].n;return ES2_OK;
}
static es2_status purchases(world *w) {
    uint32_t spent=0;int i;
    for(i=0;i<5;i++) {
        int16_t id=w->five[i];
        if(!id)continue;
        if(id<0 || id>32)return es2_fail(w->e,ES2_ERR_OFFSET,(size_t)i);
        spent+=w->cat[id].cost;
        TRY(new_item(w,id,100,100,id>=13 && id<=16?1:5));
    }
    w->total-=spent;return ES2_OK;
}
static es2_status restock(world *w,uint8_t option) {
    int i;
    if(!option) {
        uint32_t spent=0;int j;
        w->five_count=5;for(i=0;i<5;i++)w->five[i]=0;
        for(j=0;j<33 && w->five_count;j++) {
            int16_t id=-1;uint16_t cost;uint8_t flag;int16_t count;
            for(i=0;i<33;i++)if(w->cat[i].priority!=99 && w->cat[i].priority==j){id=(int16_t)i;break;}
            TRY(category_cost(w,id,&cost,&flag,&count));
            if(flag && count<2 && (uint32_t)cost+spent<w->total) {
                spent+=cost;
                for(i=0;i<5;i++)if(!w->five[i])break;
                if(i<5){w->five[i]=id;w->five_count=wrap(w->five_count-1);}
            }
        }
    } else {
        uint32_t sum=0;
        for(i=0;i<5;i++)if(w->five[i]) {
            if(w->five[i]<0 || w->five[i]>32)return es2_fail(w->e,ES2_ERR_OFFSET,(size_t)i);
            sum+=w->cat[w->five[i]].cost;
        }
        if(sum>w->total)for(i=0;i<5;i++) {
            if(w->five[i]){sum-=w->cat[w->five[i]].cost;w->five[i]=0;w->five_count=wrap(w->five_count+1);}
            if(sum<=w->total)break;
        }
    }
    return ES2_OK;
}

/* ---- Mission-end bookkeeping (0x41104f, 0x411ee9, 0x412ce2) --------------- */
static es2_status construction(world *w) {
    int i;
    for(i=0;i<8;i++) {
        bay *b=&w->bays[i];int32_t div;
        if(!b->present || bw(b,70)==100)continue;
        es2_put16(b->fixed+72,(uint16_t)wrap(bw(b,72)-1));
        div=w->herc[bw(b,2)][6];
        if(!div)return es2_fail(w->e,ES2_ERR_UNSUPPORTED,(size_t)i);
        es2_put16(b->fixed+70,(uint16_t)wrap(wrap32((int64_t)(div-bw(b,72))*100)/div));
    }
    return ES2_OK;
}
static void unlock_hercs(world *w) {
    int i,expected=2;
    for(i=0;i<9;i++) {
        int var=-1;
        if(w->nine[i])continue;
        switch(w->herc[i][0]){case 1:var=60;break;case 7:var=61;break;case 6:var=62;break;case 8:var=63;expected=1;break;default:break;}
        if(var<0)continue;
        if(w->bank[var]==expected)w->nine[i]=1;
        w->bank[var]=0;
    }
}
static es2_status unlock_items(world *w,int16_t *added) {
    int i,k;*added=0;
    for(i=0;i<33;i++) {
        int16_t var=w->unlock_var[i];
        if(var==-1 || w->cat[i].flag)continue;
        if(var<0 || var>=1000)return es2_fail(w->e,ES2_ERR_OFFSET,(size_t)i);
        if(w->bank[var]==w->unlock_value[i])w->cat[i].flag=1;
        w->bank[var]=0;
    }
    for(i=21;i<50;i++)for(k=0;k<w->bank[i];k++) {
        TRY(new_item(w,w->var_items[i-21],100,100,5));
        *added=wrap(*added+1);
    }
    return ES2_OK;
}

/* ---- Advancement (0x4133eb) and hooks ---------------------------------- */
static void remove_vehicle(world *w,int16_t vehicle,es2_status *status) {
    int i;int16_t found=-1;pilot *p=NULL;
    for(i=0;i<8;i++) {
        found=w->bays[i].present?bw(&w->bays[i],0):-1;
        if(found==vehicle) {
            *status=return_items(w,&w->bays[i]);if(*status!=ES2_OK)return;
            w->bays[i].present=0;w->hangar_count=wrap(w->hangar_count-1);found=(int16_t)i;break;
        }
    }
    /* No match leaves bay 7's lookup result (its vehicle or -1) as the word. */
    if(found==-1)return;
    if(pw(&w->player,0x22)==found)p=&w->player;
    else for(i=0;i<3;i++)if(pw(selected(w,i),0x22)==found){p=selected(w,i);break;}
    if(p)ps(p,0x22,-1);
}
static es2_status compose_text(world *w,const es2_shell_debrief *d,es2_span str,es2_shell_buffer *out) {
    es2_reader r;int16_t count;const uint8_t **text=NULL;size_t *size=NULL,i,total=0;es2_status status;
    es2_reader_init(&r,str,w->e);es2_skip(&r,4);es2_read_i16(&r,&count);
    if(r.status!=ES2_OK || count<0)return es2_fail(w->e,ES2_ERR_COUNT,4);
    text=(const uint8_t **)calloc((size_t)count+1,sizeof *text);size=(size_t *)calloc((size_t)count+1,sizeof *size);
    status=(!text || !size)?es2_fail(w->e,ES2_ERR_CAPACITY,0):ES2_OK;
    for(i=0;i<(size_t)count && status==ES2_OK;i++) {
        int16_t n=0;es2_span s;const uint8_t *end;
        if(es2_read_i16(&r,&n)!=ES2_OK || n<1 || es2_read_span(&r,(size_t)n,&s)!=ES2_OK || es2_skip(&r,1)!=ES2_OK)status=r.status!=ES2_OK?r.status:es2_fail(w->e,ES2_ERR_STRING,i);
        else {end=(const uint8_t *)memchr(s.data,0,s.size);text[i]=s.data;size[i]=end?(size_t)(end-s.data):s.size;}
    }
    for(i=0;i<30 && status==ES2_OK;i++) {
        if(d->refs[i]==-1)continue;
        if(d->refs[i]<0 || d->refs[i]>=count)status=es2_fail(w->e,ES2_ERR_OFFSET,i);
        else total+=size[d->refs[i]];
    }
    if(status==ES2_OK) {
        out->data=(uint8_t *)malloc(total+1);out->size=0;
        if(!out->data)status=es2_fail(w->e,ES2_ERR_CAPACITY,0);
        for(i=0;i<30 && status==ES2_OK;i++)if(d->refs[i]!=-1){memcpy(out->data+out->size,text[d->refs[i]],size[d->refs[i]]);out->size+=size[d->refs[i]];}
    }
    free(text);free(size);return status;
}
static es2_status advance(world *w,const es2_vfs *v,const es2_shell_results_input *in,
    int16_t outcome,es2_shell_results_output *o) {
    es2_vfs_copy c;es2_dat career;es2_dat_career_group group;char name[40];int16_t id;
    int16_t g=es2_le16s(w->selection),p=es2_le16s(w->selection+2);es2_status status=ES2_OK;
    TRY(es2_vfs_find(v,"GAM/CAREER.DAT",&c,w->e));TRY(es2_dat_parse(ES2_DAT_CAREER,c.payload,0,&career,w->e));
    TRY(es2_shell_campaign_mission(v,g,p,in->language,name,sizeof name,&id,w->e));
    TRY(es2_shell_debrief_select(v,name,in->language,w->bank,&o->rng,&o->selected,w->e));
    o->strings=o->selected.strings;o->selected.strings.data=NULL;o->selected.strings.size=0;o->debrief=1;
    TRY(es2_dat_career_group_at(&career,(size_t)g,&group,w->e));
    p=wrap(p+1);
    if((long)group.count<=p){p=0;g=wrap(g+1);}
    es2_put16(w->selection,(uint16_t)g);es2_put16(w->selection+2,(uint16_t)p);
    if(outcome && (size_t)g>=career.count){o->advance=1;return ES2_OK;}
    if((!p || g>4) && !outcome){o->advance=0;return ES2_OK;}
    if(g==1 && p==3)ps(&w->player,0x22,4);
    if(g==1 && p==6){remove_vehicle(w,8,&status);TRY(status);}
    o->advance=2;
    return compose_text(w,&o->selected,(es2_span){o->strings.data,o->strings.size},&o->text);
}

/* ---- Results path 0x40f09f ------------------------------------------------- */
static es2_status run(world *w,const es2_vfs *v,const es2_shell_results_input *in,es2_shell_results_output *o) {
    cursor r={in->results,0};uint8_t head[8];int16_t outcome,pairs,lost=0;int i,order;
    if(!in->variables.data || in->variables.size!=2000)return es2_fail(w->e,ES2_ERR_LENGTH,in->variables.size);
    for(i=0;i<1000;i++)w->bank[i]=es2_le16s(in->variables.data+2*i);
    TRY(take(w,&r,head,6));
    outcome=es2_le16s(head);w->total+=es2_le32(head+2);w->bank[0]=outcome;
    TRY(take(w,&r,head,2));pairs=es2_le16s(head);
    for(i=0;i<pairs;i++) {
        TRY(take(w,&r,head,4));
        if(in->mode==1)TRY(new_item(w,es2_le16s(head),es2_le16s(head+2),100,5));
    }
    TRY(consume(w,&w->player,&r));
    for(order=1;order<w->count;order++) {
        int g=find_order(w,order);
        if(g<0 || !active(selected(w,g)))continue;
        TRY(consume(w,selected(w,g),&r));
    }
    /* 0x4109de: losses and counters. */
    if(!pw(&w->player,0x2b))lost++;else promote(&w->player,1);
    for(order=1;order<w->capacity;order++) {
        int g=find_order(w,order);pilot *p;
        if(g<0)continue;
        p=selected(w,g);
        if(!active(p))continue;
        if(!pw(p,0x2b)){lost++;replace(w,g);w->count=wrap(w->count-1);}
        else promote(p,0);
    }
    o->lost=lost;w->bank[9]=lost;
    if(!pw(&w->player,0x2b)){es2_put16(w->final_fields,3);o->ending=ES2_SHELL_RESULTS_PLAYER_LOST;return ES2_OK;}
    w->bank[8]=w->hercs_lost;
    if(in->mode!=1){o->ending=ES2_SHELL_RESULTS_OTHER_MODE;return ES2_OK;}
    TRY(purchases(w));
    TRY(repairs(w,in->repair_option));
    TRY(construction(w));
    unlock_hercs(w);
    TRY(unlock_items(w,&o->added));
    TRY(restock(w,in->restock_option));
    o->ending=ES2_SHELL_RESULTS_ADVANCED;
    TRY(advance(w,v,in,outcome,o));
    if(o->advance==1)es2_put16(w->final_fields,1);
    else if(o->advance==0)es2_put16(w->final_fields,0);
    return ES2_OK;
}

void es2_shell_results_free(es2_shell_results_output *out) {
    if(!out)return;
    free(out->save.data);free(out->strings.data);free(out->text.data);
    es2_shell_debrief_free(&out->selected);memset(out,0,sizeof *out);
}
es2_status es2_shell_results(const es2_vfs *v,const es2_shell_results_input *in,
    es2_shell_results_output *out,es2_error *e) {
    world *w;es2_shell_results_output o;es2_status status;size_t i;
    if(!v || !in || !out || out->save.data || out->strings.data || out->text.data || in->rng.a>=56 || in->rng.b>=56)
        return es2_fail(e,ES2_ERR_ARGUMENT,0);
    w=(world *)calloc(1,sizeof *w);
    if(!w)return es2_fail(e,ES2_ERR_CAPACITY,0);
    memset(&o,0,sizeof o);o.advance=-1;o.rng=in->rng;w->e=e;
    status=load_tables(w,v,in->tables);
    if(status==ES2_OK)status=load_save(w,in->save);
    if(status==ES2_OK && in->set_position){es2_put16(w->selection,(uint16_t)in->group);es2_put16(w->selection+2,(uint16_t)in->position);}
    if(status==ES2_OK)status=run(w,v,in,&o);
    if(status==ES2_OK)status=save_out(w,&o.save);
    if(status==ES2_OK) {
        o.group=es2_le16s(w->selection);o.position=es2_le16s(w->selection+2);o.final_word=es2_le16s(w->final_fields);o.total=w->total;
        *out=o;es2_error_clear(e);
    } else {
        int code=e?e->code:status;size_t offset=e?e->offset:0;
        es2_shell_results_free(&o);status=es2_fail(e,(es2_status)code,offset);
    }
    for(i=0;i<33;i++)free(w->cat[i].v);
    free(w);return status;
}

/* Shell options of the preferences file data\prefs.cfg, written in our
 * own words from vshell_preparation.md ("Shell options"). VSHELL keeps
 * the 54 bytes at VA 0x48259c; the mode, repair and weapons-building bytes
 * reach the results path above as es2_shell_results_input fields. */
void es2_shell_prefs_load(const es2_span *file,uint8_t prefs[ES2_SHELL_PREFS_SIZE]) {
    size_t n;
    /* Original: VSHELL.EXE 0x40dc8c..0x40dcb4: the block is zeroed, then up
     * to 54 bytes are read only when the file opens; a short read leaves the
     * remaining bytes zero and is not reported. */
    memset(prefs,0,ES2_SHELL_PREFS_SIZE);
    if(!file || !file->data)return;
    n=file->size<ES2_SHELL_PREFS_SIZE?file->size:ES2_SHELL_PREFS_SIZE;
    memcpy(prefs,file->data,n);
}
void es2_shell_prefs_options(const uint8_t prefs[ES2_SHELL_PREFS_SIZE],es2_shell_results_input *in) {
    if(!prefs || !in)return;
    /* Original: VSHELL.EXE 0x40e751..0x40e758 (start-up 0x401d44): the mode
     * word is the zero-extended byte +0x2a. The results path reads +0x2c
     * (0x40edc7) and +0x2d (0x40f4cd, 0x412a37) directly. */
    in->mode=(int16_t)prefs[ES2_SHELL_PREFS_MODE];
    in->repair_option=prefs[ES2_SHELL_PREFS_REPAIR];
    in->restock_option=prefs[ES2_SHELL_PREFS_BUILD];
}
es2_status es2_shell_prefs_choose(uint8_t prefs[ES2_SHELL_PREFS_SIZE],int mode,int repair,int build,es2_error *e) {
    /* Only the values the UI controls pass; -1 leaves a byte unchanged. */
    if(!prefs || mode<-1 || mode>ES2_SHELL_MODE_CAMPAIGN || repair<-1 || repair>ES2_SHELL_REPAIR_MANUAL_ALL ||
       build<-1 || build>ES2_SHELL_BUILD_MANUAL)
        return es2_fail(e,ES2_ERR_ARGUMENT,0);
    /* Original: 0x40ec56 (main menu): mode word := argument, byte +0x2a :=
     * its low byte, and only that byte is rewritten in the file (0x40df90). */
    if(mode>=0)prefs[ES2_SHELL_PREFS_MODE]=(uint8_t)mode;
    /* Original: 0x43712c / 0x4371e0 (preferences screen radio buttons) store
     * the value; Accept 0x4372e1 writes all 54 bytes (0x40dd0a). */
    if(repair>=0)prefs[ES2_SHELL_PREFS_REPAIR]=(uint8_t)repair;
    if(build>=0)prefs[ES2_SHELL_PREFS_BUILD]=(uint8_t)build;
    es2_error_clear(e);
    return ES2_OK;
}

/* ---- Service bay, WEAPONS screen ------------------------------------
 * Contract: vshell_ui.md section 10 and es2/shell/hangar.h;
 * original code: 0x411b10, 0x43fdac, 0x43e346 and 0x412a37. */

/* load_save models the reader 0x412781, which prepends: a file's lists come
 * out reversed. The service-bay SAV is a memory state, so turn them back. */
static es2_status bay_load(world *w,es2_span save) {
    es2_sav s;size_t i,j;
    TRY(load_save(w,save));
    for(i=0;i<33;i++)for(j=0;j<w->cat[i].n/2;j++) {
        item t=w->cat[i].v[j];w->cat[i].v[j]=w->cat[i].v[w->cat[i].n-1-j];w->cat[i].v[w->cat[i].n-1-j]=t;
    }
    TRY(es2_sav_parse(save,&s,w->e));
    for(i=0;i<1000;i++)w->bank[i]=es2_le16s(s.variables.data+2*i);
    return ES2_OK;
}
static es2_status bay_slot(world *w,const es2_shell_bay_op *op,size_t index,bay **out) {
    bay *b;
    if(op->bay<0 || op->bay>7 || op->slot<0 || op->slot>9)return es2_fail(w->e,ES2_ERR_ARGUMENT,index);
    b=&w->bays[op->bay];
    if(b->present && op->slot>=bw(b,74))return es2_fail(w->e,ES2_ERR_OFFSET,index);
    *out=b;return ES2_OK;
}
/* 0x411b10 */
static es2_status bay_mount(world *w,bay *b,unsigned s,int16_t id) {
    category *c=&w->cat[id];
    if(b->used[s]) {
        TRY(prepend(w,b->slot[s].w[0],&b->slot[s]));
        b->used[s]=0;b->occupied=wrap(b->occupied-1);
    }
    if(!id){cs(b,23+s,100);return ES2_OK;}
    if(!c->n)return ES2_OK;   /* 0x4124eb returns NULL: the slot stays empty */
    b->slot[s]=c->v[--c->n];b->used[s]=1;
    cs(b,23+s,b->slot[s].w[2]);b->occupied=wrap(b->occupied+1);
    b->slot[s].w[4]=(int16_t)(id>=13 && id<=15?1:5);
    return ES2_OK;
}
static es2_status bay_op(world *w,es2_shell_bay_op *op,size_t index,int *applied) {
    bay *b;unsigned s;int16_t id=op->value;
    *applied=0;
    switch(op->kind) {
    case ES2_SHELL_BAY_MOUNT:
        TRY(bay_slot(w,op,index,&b));
        if(!b->present)return es2_fail(w->e,ES2_ERR_NOT_FOUND,index);
        if(id<0 || id>32)return es2_fail(w->e,ES2_ERR_ARGUMENT,index);
        TRY(bay_mount(w,b,(unsigned)op->slot,id));*applied=1;return ES2_OK;
    case ES2_SHELL_BAY_CLICK:
        TRY(bay_slot(w,op,index,&b));
        if(id<0 || id>32)return es2_fail(w->e,ES2_ERR_ARGUMENT,index);
        if(!b->present)return ES2_OK;                         /* 0x43e30a */
        if(!w->cat[id].flag || !op->admitted)return ES2_OK;   /* disabled row (0x440256) */
        s=(unsigned)op->slot;
        if(id && (b->used[s]?b->slot[s].w[4]:5)!=id && !w->cat[id].n && slot_item(b,s)!=id)return ES2_OK;
        TRY(bay_mount(w,b,s,id));*applied=1;return ES2_OK;
    case ES2_SHELL_BAY_GUIDANCE:
        TRY(bay_slot(w,op,index,&b));
        s=(unsigned)op->slot;
        if(!b->present || !b->used[s] || b->slot[s].w[0]<13 || b->slot[s].w[0]>16 || id<0 || id>3)
            return es2_fail(w->e,ES2_ERR_ARGUMENT,index);
        b->slot[s].w[4]=id;*applied=1;return ES2_OK;
    case ES2_SHELL_BAY_REBALANCE:
        if(id<0 || id>255)return es2_fail(w->e,ES2_ERR_ARGUMENT,index);
        TRY(restock(w,(uint8_t)id));*applied=1;return ES2_OK;
    default:
        return es2_fail(w->e,ES2_ERR_ARGUMENT,index);
    }
}
es2_status es2_shell_service_bay(const es2_vfs *v,es2_span tables,es2_span save,
    es2_shell_bay_op *ops,size_t count,es2_shell_buffer *out,es2_error *e) {
    world *w;es2_shell_buffer o={0};es2_status status=ES2_OK;size_t i;int rebalance=0,*applied;
    if(!out || out->data || (count && !ops))return es2_fail(e,ES2_ERR_ARGUMENT,0);
    for(i=0;i<count;i++)rebalance|=ops[i].kind==ES2_SHELL_BAY_REBALANCE;
    if(rebalance && !v)return es2_fail(e,ES2_ERR_ARGUMENT,0);
    w=(world *)calloc(1,sizeof *w);
    applied=(int *)calloc(count+1,sizeof *applied);
    if(!w || !applied){free(w);free(applied);return es2_fail(e,ES2_ERR_CAPACITY,0);}
    w->e=e;
    if(rebalance)status=load_tables(w,v,tables);
    if(status==ES2_OK)status=bay_load(w,save);
    for(i=0;i<count && status==ES2_OK;i++)status=bay_op(w,&ops[i],i,&applied[i]);
    if(status==ES2_OK)status=save_out(w,&o);
    if(status==ES2_OK) {
        for(i=0;i<count;i++)ops[i].applied=applied[i];
        *out=o;es2_error_clear(e);
    }
    for(i=0;i<33;i++)free(w->cat[i].v);
    free(w);free(applied);return status;
}

/* ---- Preferences blocks ----------------------------------------------
 * Contract: vshell_ui.md section 11.3; original code:
 * the loader 0x40dc5b, the byte store 0x40ddfe, the toggle 0x40de35, commit
 * 0x40dd6e, revert 0x40ddb6, the writer 0x40dd0a, the read-modify-write
 * 0x40dfa3 and the Preferences controls 0x437351..0x437758. No VSHELL code
 * registers a per-byte callback (0x40e0dd has no caller), so the stores have
 * no side effects on other bytes. */
void es2_shell_prefs_session_open(const es2_span *file,es2_shell_prefs_session *s) {
    if(!s)return;
    es2_shell_prefs_load(file,s->current);
    memcpy(s->committed,s->current,ES2_SHELL_PREFS_SIZE);
}
es2_status es2_shell_prefs_store(es2_shell_prefs_session *s,const es2_span *file,size_t index,uint8_t value,
    uint8_t out[ES2_SHELL_PREFS_SIZE],es2_error *e) {
    uint8_t image[ES2_SHELL_PREFS_SIZE];
    if(!s || !out || index>=ES2_SHELL_PREFS_SIZE)return es2_fail(e,ES2_ERR_ARGUMENT,index);
    s->current[index]=value;
    /* 0x40dfe4: the image is a copy of the current block; 0x40dfec: only when
     * the read stream opened, the file's bytes go over its start and the
     * listed byte is taken from the current block again. */
    memcpy(image,s->current,sizeof image);
    if(file && file->data) {
        size_t n=file->size<ES2_SHELL_PREFS_SIZE?file->size:ES2_SHELL_PREFS_SIZE;
        memcpy(image,file->data,n);
        image[index]=s->current[index];
    }
    memcpy(out,image,sizeof image);
    es2_error_clear(e);return ES2_OK;
}
es2_status es2_shell_prefs_control(es2_shell_prefs_session *s,int control,es2_error *e) {
    if(!s)return es2_fail(e,ES2_ERR_ARGUMENT,0);
    switch(control) {
    case ES2_SHELL_PREFS_MUSIC_TOGGLE:s->current[0]^=1;break;     /* 0x436ed1(0) -> 0x40de35(0) */
    case ES2_SHELL_PREFS_SFX_TOGGLE:s->current[1]^=1;break;       /* 0x436ed1(1) -> 0x40de35(1) */
    case ES2_SHELL_PREFS_HIGH_RES:s->current[4]=0;break;          /* 0x43714c(0) */
    case ES2_SHELL_PREFS_LOW_RES:s->current[4]=1;break;           /* 0x43714c(1) */
    case ES2_SHELL_PREFS_WINDOW:s->current[6]=0;break;            /* 0x437200(0) */
    case ES2_SHELL_PREFS_FULL_SCREEN:s->current[6]=1;break;       /* 0x437200(1) */
    case ES2_SHELL_PREFS_REPAIR_0:case ES2_SHELL_PREFS_REPAIR_1:case ES2_SHELL_PREFS_REPAIR_2:
        s->current[ES2_SHELL_PREFS_REPAIR]=(uint8_t)(control-ES2_SHELL_PREFS_REPAIR_0);break;   /* 0x43712c */
    case ES2_SHELL_PREFS_BUILD_0:case ES2_SHELL_PREFS_BUILD_1:
        s->current[ES2_SHELL_PREFS_BUILD]=(uint8_t)(control-ES2_SHELL_PREFS_BUILD_0);break;     /* 0x4371e0 */
    default:return es2_fail(e,ES2_ERR_ARGUMENT,0);
    }
    es2_error_clear(e);return ES2_OK;
}
void es2_shell_prefs_accept(es2_shell_prefs_session *s,uint8_t out[ES2_SHELL_PREFS_SIZE]) {
    if(!s)return;
    memcpy(s->committed,s->current,ES2_SHELL_PREFS_SIZE);
    if(out)memcpy(out,s->current,ES2_SHELL_PREFS_SIZE);
}
void es2_shell_prefs_cancel(es2_shell_prefs_session *s) {
    if(s)memcpy(s->current,s->committed,ES2_SHELL_PREFS_SIZE);
}

/* ---- Launch checks of Rock & Roll -------------------------------------
 * Contract: vshell_ui.md sections 4.8 and 11.1;
 * original code: 0x445b99, 0x4110c1 (with 0x411088, 0x411ca5, 0x41232a),
 * 0x411135 (0x411d10), 0x40fc7e and 0x4108fa. Bay records are the SAV's
 * loadouts: memory +0x08 = fixed byte 4 (33 condition words, family 1 from word
 * 13), +0x4a = fixed byte 70 (build progress), +0x50 = the ten slot pointers.
 * */
static int launch_functional(const world *w,int16_t k) {
    const bay *b=&w->bays[k];
    /* 0x411088: progress word +0x4a = 100; 0x411ca5: family-1 words 0, 1, 5,
     * 8 (condition words 13, 14, 18, 21) each above 50, signed. */
    return b->present && bw(b,70)==100 && cw(b,13)>50 && cw(b,14)>50 && cw(b,18)>50 && cw(b,21)>50;
}
static int launch_armed(const world *w,int16_t k) {
    const bay *b=&w->bays[k];unsigned s;
    /* 0x411d10: any of the ten slot pointers (not only below the capacity)
     * holds an item whose ID word is below 29, signed. */
    if(!b->present)return 0;
    for(s=0;s<10;s++)if(b->used[s] && b->slot[s].w[0]<29)return 1;
    return 0;
}
/* 0x40fc7e: orders 1 .. capacity - 1; per order the first roster group whose
 * selected pilot has it (0x4108fa); an active one needs an armed Herc in its
 * bay. -1: an active pilot without a bay (the original reads the word before
 * the hangar array, the third selected-pilot pointer, as a record). */
static int launch_squad(world *w) {
    int16_t order;int g;
    for(order=1;order<w->capacity;order++) {
        const pilot *p=NULL;int16_t k;
        for(g=0;g<3 && !p;g++)if(pw(selected(w,g),0x27)==order)p=selected(w,g);
        if(!p || !active(p))continue;
        k=pw(p,0x22);
        if(k<0 || k>7)return -1;
        if(!launch_armed(w,k))return 0;
    }
    return 1;
}
es2_status es2_shell_launch_check(es2_span save,es2_shell_launch_check_result *out,es2_error *e) {
    world *w;es2_shell_launch_check_result r;es2_status status;size_t i;
    if(!out)return es2_fail(e,ES2_ERR_ARGUMENT,0);
    w=(world *)calloc(1,sizeof *w);
    if(!w)return es2_fail(e,ES2_ERR_CAPACITY,0);
    w->e=e;
    status=load_save(w,save);
    if(status==ES2_OK) {
        r.player_bay=pw(&w->player,0x22);
        r.functional=r.player_bay==-1?-1:launch_functional(w,r.player_bay);
        r.armed=r.player_bay==-1?-1:launch_armed(w,r.player_bay);
        r.squad_armed=launch_squad(w);
        /* 0x445baa..0x445c1a, in this order; the first failure picks the box */
        if(r.player_bay==-1)r.warning=ES2_SHELL_LAUNCH_NO_HERC;
        else if(!r.functional)r.warning=ES2_SHELL_LAUNCH_NOT_FUNCTIONAL;
        else if(!r.armed)r.warning=ES2_SHELL_LAUNCH_UNARMED;
        else if(r.squad_armed<0)status=es2_fail(e,ES2_ERR_OFFSET,0);
        else r.warning=r.squad_armed?ES2_SHELL_LAUNCH_OK:ES2_SHELL_LAUNCH_SQUAD_UNARMED;
        if(status==ES2_OK){*out=r;es2_error_clear(e);}
    }
    for(i=0;i<33;i++)free(w->cat[i].v);
    free(w);return status;
}

/* ---- Service-bay editors: REPAIR, BUILD, Herc scrap -------------------
 * Contract: vshell_ui.md section 12 and es2/shell/editors.h;
 * original code: the REPAIR screen (show 0x43397c, snapshot 0x433f86 /
 * 0x435547, row selection 0x434549 with 0x433aa0 / 0x433ac1, refresh
 * 0x433ad5, REPAIR 0x4351bd, REPAIR ALL 0x4352e9, CANCEL 0x435403), the cost
 * and condition helpers 0x411a78, 0x413f8c, 0x413e95, 0x43e05f, 0x413e5c,
 * 0x41229c, the BUILD screen (refresh 0x447064, type 0x4472cb, Build
 * 0x4475ce -> 0x40eed4 -> 0x410fa6 / 0x41139e / 0x41163d), the scrap
 * (0x448326 -> 0x40ed0f with 0x410f46, 0x410844, 0x4108fa, 0x41094b) and the
 * squad panel's bay selection 0x43dcdd. */
#include "es2/shell/editors.h"

typedef struct editor_tables { int16_t band[5], vehicle[9], capacity[9], rows[ES2_SHELL_ARMORY_ROWS]; } editor_tables;

void es2_shell_editor_session_init(es2_shell_editor_session *s,int16_t bay) {
    if(!s)return;
    memset(s,0,sizeof *s);
    s->screen=ES2_SHELL_SCREEN_BAY;s->bay=bay;s->herc_type=-1;
    s->crew[0]=s->crew[1]=s->crew[2]=-1;s->armory_row=-1;
}
/* 0x433aa0 / 0x433ac1: External rows 0..5 are family 0, 6..15 family 2
 * (row - 6); Internal rows are family 1. */
static int16_t row_family(int16_t tab,int16_t row) { return (int16_t)(tab?1:row>5?2:0); }
static int16_t row_index(int16_t family,int16_t row) { return (int16_t)(family==2?row-6:row); }
static int row_valid(int16_t tab,int16_t row) { return (tab==0 && row>=0 && row<16) || (tab==1 && row>=0 && row<9); }

static bay *editor_bay(world *w,const es2_shell_editor_session *s) {
    return s->bay>=0 && s->bay<8 && w->bays[s->bay].present?&w->bays[s->bay]:NULL;
}
/* 0x41232a / 0x4122e1 with families 0 (part group average, 0x41224d; the
 * write 0x41229c stores the value in every listed word), 1 and 2. */
static es2_status cond_get(world *w,const bay *b,int16_t family,int16_t index,int16_t *out) {
    if(family==0)return part(w,b,(unsigned)index,out);
    *out=cw(b,(unsigned)(family==1?13+index:23+index));return ES2_OK;
}
static es2_status cond_set(world *w,bay *b,int16_t family,int16_t index,int16_t v) {
    if(family==0) {
        int16_t check;unsigned at;
        TRY(part(w,b,(unsigned)index,&check));   /* the same list, bounded */
        for(at=4u*(unsigned)index;w->groups[at]!=-1;at++)cs(b,(unsigned)w->groups[at],v);
        return ES2_OK;
    }
    cs(b,(unsigned)(family==1?13+index:23+index),v);return ES2_OK;
}
/* 0x411a78 -> 0x413f8c(vehicle word +0, family, index or slot item, c, 1) ->
 * 0x413e95(weight, c, 1): the cost to raise c to the next band limit. */
static es2_status item_cost(world *w,const bay *b,int16_t family,int16_t index,int32_t *out) {
    int16_t c,weight;int t,ci;int32_t limit;
    TRY(cond_get(w,b,family,index,&c));
    t=bw(b,0);
    weight=family==0?w->value[t][index]:family==1?w->value[t][13+index]:w->item_value[slot_item(b,(unsigned)index)];
    ci=threshold_index(w,c);
    limit=ci?w->threshold[ci-1]:w->level[0];
    *out=wrap32((int64_t)(limit-c)*weight)/100;
    return ES2_OK;
}
/* 0x43e05f (first band limit of 0x47665c below c, else 5) and 0x413e5c
 * (band 0: level 0; band i: level i + 1). */
static int16_t repaired(const world *w,const editor_tables *x,int16_t c) {
    int i;for(i=0;i<5;i++)if(c>x->band[i])break;
    return i?wrap(w->level[i]+1):w->level[0];
}
/* 0x4119d3(L, x) */
static void set_all(bay *b,int16_t x) {
    unsigned i;
    for(i=0;i<22;i++)cs(b,i,x);
    for(i=0;i<(unsigned)bw(b,74);i++)cs(b,23+i,slot_item(b,i)?x:100);
}
/* 0x412baa: the five-entry build bank's cost. */
static es2_status bank_cost(world *w,uint32_t *out) {
    int i;uint32_t sum=0;
    for(i=0;i<5;i++)if(w->five[i]) {
        if(w->five[i]<0 || w->five[i]>32)return es2_fail(w->e,ES2_ERR_OFFSET,(size_t)i);
        sum+=w->cat[w->five[i]].cost;
    }
    *out=sum;return ES2_OK;
}
static int finished(const world *w,int k) { return w->bays[k].present && bw(&w->bays[k],70)==100; }
static int16_t first_finished(const world *w) {   /* 0x41124e */
    int k;for(k=0;k<8;k++)if(finished(w,k))return (int16_t)k;
    return -1;
}
static int one_functional(const world *w) {        /* 0x411101 */
    int k,n=0;for(k=0;k<8;k++)n+=launch_functional(w,(int16_t)k);
    return n==1;
}
/* 0x433f86 with 0x435547: the salvage total and, with a bay selected, its
 * 33 condition words. */
static es2_status snapshot(world *w,es2_shell_editor_session *s) {
    const bay *b=editor_bay(w,s);unsigned i;
    s->salvage=w->total;
    if(s->bay==-1)return ES2_OK;
    if(!b)return es2_fail(w->e,ES2_ERR_OFFSET,0);  /* 0x435547 leaves its buffer unwritten */
    for(i=0;i<33;i++)s->conditions[i]=cw(b,i);
    return ES2_OK;
}
/* 0x434549(tab, row) */
static es2_status pick(world *w,es2_shell_editor_session *s,int16_t tab,int16_t row,size_t at,int *applied) {
    int16_t f=row_family(tab,row),i=row_index(f,row);
    if(tab==s->tab && row==s->row)return ES2_OK;
    if(f==2) {
        const bay *b=editor_bay(w,s);
        if(s->bay==-1)return es2_fail(w->e,ES2_ERR_OFFSET,at);  /* reads a pilot pointer as a record */
        if(!b || i>=bw(b,74) || !slot_item(b,(unsigned)i))return ES2_OK;
    }
    s->tab=tab;s->row=row;s->family=f;s->index=i;*applied=1;
    return ES2_OK;
}
/* 0x43dcdd for screen word 3 (from the scrap or a selector): the new bay,
 * row (0, 0), the snapshot. */
static es2_status repair_select(world *w,es2_shell_editor_session *s,int16_t k,size_t at) {
    int dummy=0;
    s->bay=k;
    TRY(pick(w,s,0,0,at,&dummy));
    return snapshot(w,s);
}
/* 0x40ed0f for the selected bay k. */
static es2_status scrap(world *w,int16_t k) {
    bay *b=&w->bays[k];pilot *p=NULL;int g;
    if(b->present) {   /* 0x410f46 -> 0x411a56: 0x414174, then 0x411db9 */
        int32_t value;
        TRY(lost_value(w,b,&value));TRY(return_items(w,b));
        b->present=0;w->hangar_count=wrap(w->hangar_count-1);w->total+=(uint32_t)value;
    }
    /* 0x410844: the player when his bay is k, else the first selected pilot */
    if(pw(&w->player,0x22)==k)p=&w->player;
    else for(g=0;g<3 && !p;g++)if(pw(selected(w,g),0x22)==k)p=selected(w,g);
    if(!p)return ES2_OK;
    ps(p,0x22,-1);
    g=find_order(w,pw(p,0x27));   /* 0x4108fa: the first group with that order */
    if(g>=0) {                    /* 0x41094b(team, g, 0) */
        pilot *q=selected(w,g);
        if(active(q))w->count=wrap(w->count-1);
        q->suffix[2]=0;
    }
    return ES2_OK;
}
/* 0x410fa6 / 0x41139e / 0x41163d: a new record of `type` in bay k; returns
 * the cost HERC_INF[0x411354(type)] +8 * 1000. */
static es2_status build(world *w,const editor_tables *x,int16_t k,int16_t type,int32_t *cost) {
    bay *b=&w->bays[k];int r;unsigned i;
    for(r=0;r<9;r++)if(x->vehicle[r]==type)break;
    if(r==9 || x->capacity[r]<0 || x->capacity[r]>10)return es2_fail(w->e,ES2_ERR_OFFSET,(size_t)(uint16_t)type);
    memset(b,0,sizeof *b);b->present=1;
    es2_put16(b->fixed,(uint16_t)type);es2_put16(b->fixed+2,(uint16_t)r);
    for(i=0;i<33;i++)cs(b,i,100);
    es2_put16(b->fixed+70,0);es2_put16(b->fixed+72,(uint16_t)w->herc[r][6]);es2_put16(b->fixed+74,(uint16_t)x->capacity[r]);
    w->hangar_count=wrap(w->hangar_count+1);
    *cost=wrap32((int64_t)w->herc[r][4]*1000);
    return ES2_OK;
}

/* ---- Service-bay editors 2: CREW, ARMORY, weapons scrap --------------
 * Contract: vshell_ui.md section 17; original code:
 * the squad panel's selection 0x43dcdd, the CREW screen (show 0x442091 with
 * 0x442198, order rows 0x442215, pilot lists 0x442548, assignment 0x4426e5,
 * CLEAR 0x442624) and the team helpers 0x40ec80, 0x40ec8f, 0x4108e2,
 * 0x4108fa, 0x41094b, 0x41098a, 0x411164; the ARMORY screen (show 0x449b87,
 * leave 0x449c46, rows 0x449d2f / 0x44a06e, Clear 0x449ccc, Scrap 0x44a508,
 * the warning's ACCEPT 0x448447) and the bank helpers 0x412baa, 0x412bdf,
 * 0x412c0b, 0x412c31, 0x412c66, 0x412b79, 0x412c8e, 0x4125c1. */
static int16_t vehicle_of(const world *w,int16_t k) {   /* 0x411164 */
    return k>=0 && k<8 && w->bays[k].present?bw(&w->bays[k],0):-1;
}
/* 0x41094b(team, g, flag): the team count follows the activity change. */
static void crew_activity(world *w,int g,int flag) {
    pilot *q=selected(w,g);int was=active(q);
    if(flag && !was)w->count=wrap(w->count+1);
    if(!flag && was)w->count=wrap(w->count-1);
    q->suffix[2]=(uint8_t)(flag?1:0);
}
/* 0x41098a(team, order): the first group whose selected pilot has the order
 * is active when the order is below the capacity and the pilot has a bay. */
static void crew_order_activity(world *w,int16_t order) {
    int g=find_order(w,order);
    if(g<0)return;
    crew_activity(w,g,order<w->capacity && pw(selected(w,g),0x22)!=-1);
}
/* 0x40ec8f(g, bay): note the second branch passes the group as the order
 * (push esi at 0x40ecfb). */
static void crew_pilot_bay(world *w,int g,int16_t k) {
    ps(selected(w,g),0x22,k);
    if(k==-1)crew_activity(w,g,0);
    else crew_order_activity(w,(int16_t)g);
}
/* 0x4426e5 for the bay selection */
static void crew_assign(world *w,es2_shell_editor_session *s) {
    int16_t o=s->order,k=s->bay;int g;
    if(o && s->crew[o-1]<0)return;
    if(pw(&w->player,0x22)==k)ps(&w->player,0x22,-1);           /* 0x40ec80(-1) */
    for(g=0;g<3;g++)if(pw(selected(w,g),0x22)==k)crew_pilot_bay(w,g,-1);
    if(!o)ps(&w->player,0x22,k);                                 /* 0x40ec80(k) */
    else if(s->crew[o-1]>=0){ps(selected(w,s->crew[o-1]),0x22,k);crew_order_activity(w,o);}
}
/* 0x43dcdd(k) by the screen word; assign = the selector flag 0x47665a (set
 * by the bay selector handlers 0x43e477.. and CLEAR). */
static es2_status squad_select(world *w,es2_shell_editor_session *s,int16_t k,int assign,size_t at,int *applied) {
    if(k==s->bay)return ES2_OK;
    switch(s->screen) {
    case ES2_SHELL_SCREEN_WEAPONS:                       /* 0x43df86..0x43e048 */
        if(k!=-1 && !finished(w,k))return ES2_OK;
        s->bay=k;break;
    case ES2_SHELL_SCREEN_REPAIR:                        /* 0x43dd1c..0x43ddf2 */
        if(k!=-1 && !finished(w,k))return ES2_OK;
        TRY(repair_select(w,s,k,at));break;
    case ES2_SHELL_SCREEN_CREW:                          /* 0x43ddf7..0x43df06 */
        if(k!=-1 && !finished(w,k))return ES2_OK;
        if(k!=-1 && s->order && vehicle_of(w,k)==8)return ES2_OK;
        s->bay=k;
        if(assign)crew_assign(w,s);
        break;
    default:                                             /* BUILD 0x43df0b and the others 0x43dfe8 */
        s->bay=k;break;
    }
    *applied=1;return ES2_OK;
}
/* 0x442215(o) */
static es2_status crew_order_row(world *w,es2_shell_editor_session *s,int16_t o,size_t at,int *applied) {
    int16_t k=o?(s->crew[o-1]>=0?pw(selected(w,s->crew[o-1]),0x22):-1):pw(&w->player,0x22);
    TRY(squad_select(w,s,k,0,at,applied));
    if(s->order!=o){s->order=o;*applied=1;}
    return ES2_OK;
}
/* 0x442091 after the screen word 6 is stored */
static es2_status crew_show(world *w,es2_shell_editor_session *s,size_t at) {
    static const int16_t rows[5]={0,1,2,3,0};int o,i,dummy=0;int16_t k;
    for(o=1;o<=3;o++)s->crew[o-1]=(int16_t)find_order(w,o);   /* 0x442198 */
    for(i=0;i<5;i++)TRY(crew_order_row(w,s,rows[i],at,&dummy));
    for(k=0;k<8;k++)if(vehicle_of(w,k)!=-1)TRY(squad_select(w,s,k,0,at,&dummy));
    return ES2_OK;
}
/* 0x442548(g) */
static void crew_pilot(world *w,es2_shell_editor_session *s,int g,int *applied) {
    int16_t o=s->order;int r,i;
    if(!o)return;
    r=find_order(w,o);
    if(r>=0)ps(selected(w,r),0x27,-1);                   /* 0x4108e2(r, -1) */
    ps(selected(w,g),0x27,o);                            /* 0x4108e2(g, o) */
    for(i=0;i<3;i++)if(s->crew[i]==g)s->crew[i]=-1;
    s->crew[o-1]=(int16_t)g;
    crew_assign(w,s);*applied=1;
}
/* 0x442624 */
static es2_status crew_clear(world *w,es2_shell_editor_session *s,size_t at,int *applied) {
    int16_t o=s->order;int r;
    if(!o)return ES2_OK;
    r=find_order(w,o);
    if(r<0)return ES2_OK;
    crew_pilot_bay(w,r,-1);ps(selected(w,r),0x27,-1);
    s->crew[o-1]=-1;*applied=1;
    return squad_select(w,s,pw(selected(w,r),0x22),1,at,applied);
}
/* The category of an ARMORY row (0x476a4c); the original indexes its
 * catalogue with it unchecked. */
static es2_status armory_category(world *w,const editor_tables *x,int16_t row,size_t at,int16_t *out) {
    int16_t c=x->rows[row];
    if(c<0 || c>32)return es2_fail(w->e,ES2_ERR_OFFSET,at);
    *out=c;return ES2_OK;
}
static int16_t bank_entries(const world *w,int16_t c) {   /* 0x412c66 */
    int i;int16_t n=0;for(i=0;i<5;i++)n=wrap(n+(w->five[i]==c));
    return n;
}
/* 0x412c8e: the warning's number, 16 bits */
static int16_t scrap_number(const world *w,int16_t c) {
    int32_t n=(int32_t)w->cat[c].n;int16_t v;
    if(!n)return 0;
    v=wrap(((int32_t)(w->cat[c].cost/1000u)*n)/10);
    return v<1?1:v;
}
/* 0x449d2f(row) */
static es2_status armory_left(world *w,const editor_tables *x,es2_shell_editor_session *s,int16_t row,size_t at,int *applied) {
    int16_t c;
    TRY(armory_category(w,x,row,at,&c));
    if(s->building && row==s->armory_row) {
        uint32_t cost;int i;
        if(!w->five_count)return ES2_OK;
        TRY(bank_cost(w,&cost));
        if(cost+w->cat[c].cost>=w->total)return ES2_OK;    /* jae 0x44a068 */
        for(i=0;i<5;i++)if(!w->five[i]) {                  /* 0x412c0b */
            w->five[i]=c;w->five_count=wrap(w->five_count-1);*applied=1;break;
        }
        return ES2_OK;
    }
    if(s->armory_row!=row){s->armory_row=row;*applied=1;}
    return ES2_OK;
}
/* 0x449ccc: Clear by the selected row's category (0x412c31) */
static es2_status armory_clear(world *w,const editor_tables *x,es2_shell_editor_session *s,size_t at,int *applied) {
    int16_t c;int i;
    if(s->armory_row==-1)return ES2_OK;
    TRY(armory_category(w,x,s->armory_row,at,&c));
    for(i=0;i<5;i++)if(w->five[i]==c){w->five[i]=0;w->five_count=wrap(w->five_count+1);*applied=1;}
    return ES2_OK;
}
/* 0x44a06e(row) */
static es2_status armory_other(world *w,const editor_tables *x,es2_shell_editor_session *s,int16_t row,size_t at,int *applied) {
    if(s->building && row==s->armory_row) {
        int16_t c;int n,i;
        TRY(armory_category(w,x,row,at,&c));
        n=bank_entries(w,c)-1;
        TRY(armory_clear(w,x,s,at,applied));
        for(i=0;i<n;i++)TRY(armory_left(w,x,s,row,at,applied));
        return ES2_OK;
    }
    if(s->armory_row!=row){s->armory_row=row;*applied=1;}
    return ES2_OK;
}
/* Scrap 0x44a508 -> 0x448227, ACCEPT 0x448447: 0x40ed6a, 0x412a37 */
static es2_status armory_scrap(world *w,const editor_tables *x,es2_shell_editor_session *s,size_t at,int *applied) {
    int16_t c;
    if(s->armory_row==-1)return ES2_OK;
    TRY(armory_category(w,x,s->armory_row,at,&c));
    w->total+=(uint32_t)((int32_t)scrap_number(w,c)*1000);
    w->cat[c].n=0;                                       /* 0x4125c1 */
    TRY(restock(w,(uint8_t)s->building));
    *applied=1;return ES2_OK;
}
static es2_status armory_view(world *w,const editor_tables *x,const es2_shell_editor_session *s,es2_shell_editor_view *v) {
    int r;
    for(r=0;r<ES2_SHELL_ARMORY_ROWS;r++)if(x->rows[r]>=0 && x->rows[r]<=32 && w->cat[x->rows[r]].flag)v->armory_rows|=1u<<r;
    v->armory_clear_enabled=s->building!=0;
    v->armory_free=w->five_count;v->armory_category=-1;
    if(s->armory_row>=0) {
        int16_t c;
        TRY(armory_category(w,x,s->armory_row,0,&c));
        v->armory_category=c;v->armory_items=(int16_t)w->cat[c].n;v->armory_in_bank=bank_entries(w,c);
        v->armory_scrap_value=(int32_t)scrap_number(w,c)*1000;
    }
    return ES2_OK;
}

static es2_status editor_view(world *w,const es2_shell_editor_session *s,es2_shell_editor_view *v) {
    const bay *b=editor_bay(w,s);uint32_t available;
    memset(v,0,sizeof *v);
    v->salvage=w->total;TRY(bank_cost(w,&v->bank_cost));available=w->total-v->bank_cost;
    v->first_finished=first_finished(w);v->one_functional=one_functional(w);
    v->condition=100;v->scrap_bank=-1;
    if(s->herc_type>=0)v->build_cost=wrap32((int64_t)w->herc[s->herc_type][4]*1000);
    if(s->bay==-1)return ES2_OK;
    if(b) {
        v->has_record=1;
        TRY(cond_get(w,b,s->family,s->index,&v->condition));
        TRY(item_cost(w,b,s->family,s->index,&v->item_cost));
        TRY(repair_cost(w,b,100,&v->all_cost));
        TRY(lost_value(w,b,&v->scrap_value));
        v->scrap_bank=w->nine[bw(b,0)];
        v->scrap_enabled=!v->one_functional && v->scrap_bank!=0;   /* 0x433e50 / 0x44717f */
    } else v->build_enabled=available>(uint32_t)v->build_cost;      /* 0x4470c6..0x4470ec */
    v->repair_enabled=available>=(uint32_t)v->item_cost;            /* 0x433cae..0x433cf9 */
    v->repair_all_enabled=available>=(uint32_t)v->all_cost;         /* 0x433d86..0x433dc3 */
    return ES2_OK;
}
static es2_status editor_leave(world *w,es2_shell_editor_session *s,int16_t option,size_t at) {
    /* 0x43a537: WEAPONS, REPAIR and BUILD run 0x412a37; ARMORY's 0x449c46
     * resets its row selection; the other leave routines (0x44218a,
     * 0x445095) only hide and free UI. */
    if(option<0 || option>255)return es2_fail(w->e,ES2_ERR_ARGUMENT,at);
    if(s->screen>=2 && s->screen<=4)TRY(restock(w,(uint8_t)option));
    if(s->screen==ES2_SHELL_SCREEN_ARMORY)s->armory_row=-1;
    return ES2_OK;
}
static es2_status editor_op(world *w,const editor_tables *x,es2_shell_editor_session *s,es2_shell_editor_op *op,size_t at,int *applied) {
    es2_shell_editor_view v;bay *b;int16_t a=op->a;
    *applied=0;
    switch(op->kind) {
    case ES2_SHELL_EDITOR_OPEN:
        if(a<ES2_SHELL_SCREEN_WEAPONS || a>ES2_SHELL_SCREEN_CREW)return es2_fail(w->e,ES2_ERR_ARGUMENT,at);
        if(s->screen==a)return ES2_OK;                     /* 0x43a8ce, 0x43a9ce, 0x43aace, 0x43abce, 0x43acc6 */
        TRY(editor_leave(w,s,op->b,at));
        if(a==ES2_SHELL_SCREEN_REPAIR || a==ES2_SHELL_SCREEN_WEAPONS) {   /* 0x43397c, 0x43fbd8 */
            int dummy=0;
            /* 0x43dcdd runs the previous screen's case: the word is stored
             * after the show (0x43aa0d, 0x43a90d) */
            if(s->bay==-1 || !finished(w,s->bay))TRY(squad_select(w,s,first_finished(w),0,at,&dummy));
            if(a==ES2_SHELL_SCREEN_REPAIR){TRY(snapshot(w,s));TRY(pick(w,s,0,0,at,&dummy));}
        } else if(a==ES2_SHELL_SCREEN_BUILD) {
            if(s->herc_type!=0)s->herc_type=0;              /* 0x446f9d -> 0x4472cb(0) */
        } else if(a==ES2_SHELL_SCREEN_ARMORY) {
            int dummy=0;                                    /* 0x449b87 -> 0x449d2f(0) */
            TRY(armory_left(w,x,s,0,at,&dummy));
        } else {
            s->screen=a;                                    /* 0x43ad00, before the show */
            TRY(crew_show(w,s,at));
        }
        s->screen=a;*applied=1;return ES2_OK;
    case ES2_SHELL_EDITOR_CLOSE:
        if(s->screen<ES2_SHELL_SCREEN_WEAPONS || s->screen>ES2_SHELL_SCREEN_CREW || a<-1 || a>7 ||
           a==ES2_SHELL_SCREEN_REPAIR || a==ES2_SHELL_SCREEN_BUILD)
            return es2_fail(w->e,ES2_ERR_ARGUMENT,at);
        TRY(editor_leave(w,s,op->b,at));
        s->screen=a;*applied=1;return ES2_OK;
    case ES2_SHELL_EDITOR_BAY:
        if(a<0 || a>7 || s->screen<ES2_SHELL_SCREEN_WEAPONS || s->screen>ES2_SHELL_SCREEN_CREW ||
           s->screen==ES2_SHELL_SCREEN_ARMORY)
            return es2_fail(w->e,ES2_ERR_ARGUMENT,at);
        return squad_select(w,s,a,1,at,applied);       /* the selector handlers set 0x47665a */
    case ES2_SHELL_EDITOR_ARMORY_ROW:
    case ES2_SHELL_EDITOR_ARMORY_CLEAR:
    case ES2_SHELL_EDITOR_ARMORY_SCRAP:
        if(s->screen!=ES2_SHELL_SCREEN_ARMORY)return es2_fail(w->e,ES2_ERR_ARGUMENT,at);
        if(op->kind==ES2_SHELL_EDITOR_ARMORY_CLEAR)
            return s->building?armory_clear(w,x,s,at,applied):ES2_OK;   /* Clear disabled (0x449bc2) */
        if(op->kind==ES2_SHELL_EDITOR_ARMORY_SCRAP)return armory_scrap(w,x,s,at,applied);
        {
            int16_t c;
            if(a<0 || a>=ES2_SHELL_ARMORY_ROWS)return es2_fail(w->e,ES2_ERR_ARGUMENT,at);
            TRY(armory_category(w,x,a,at,&c));
            if(!w->cat[c].flag)return ES2_OK;               /* disabled row (0x4499b9) */
            return op->b==1?armory_left(w,x,s,a,at,applied):armory_other(w,x,s,a,at,applied);
        }
    case ES2_SHELL_EDITOR_CREW_ORDER:
    case ES2_SHELL_EDITOR_CREW_PILOT:
    case ES2_SHELL_EDITOR_CREW_CLEAR:
        if(s->screen!=ES2_SHELL_SCREEN_CREW)return es2_fail(w->e,ES2_ERR_ARGUMENT,at);
        if(op->kind==ES2_SHELL_EDITOR_CREW_CLEAR)return crew_clear(w,s,at,applied);
        if(op->kind==ES2_SHELL_EDITOR_CREW_ORDER) {
            if(a<0 || a>3)return es2_fail(w->e,ES2_ERR_ARGUMENT,at);
            return crew_order_row(w,s,a,at,applied);
        }
        if(a<0 || a>2)return es2_fail(w->e,ES2_ERR_ARGUMENT,at);
        crew_pilot(w,s,a,applied);return ES2_OK;
    case ES2_SHELL_EDITOR_BUILD_TYPE:
        if(s->screen!=ES2_SHELL_SCREEN_BUILD || a<0 || a>8)return es2_fail(w->e,ES2_ERR_ARGUMENT,at);
        if(!w->nine[a] || a==s->herc_type)return ES2_OK;  /* 0x446ec5 disables the row */
        s->herc_type=a;*applied=1;return ES2_OK;
    case ES2_SHELL_EDITOR_SCRAP:
        if(s->screen!=ES2_SHELL_SCREEN_REPAIR && s->screen!=ES2_SHELL_SCREEN_BUILD)return es2_fail(w->e,ES2_ERR_ARGUMENT,at);
        if(s->bay==-1)return es2_fail(w->e,ES2_ERR_OFFSET,at);
        TRY(editor_view(w,s,&v));
        if(!v.scrap_enabled)return ES2_OK;
        TRY(scrap(w,s->bay));
        if(s->screen==ES2_SHELL_SCREEN_REPAIR)TRY(repair_select(w,s,first_finished(w),at));   /* 0x448364 */
        *applied=1;return ES2_OK;
    case ES2_SHELL_EDITOR_BUILD:
        if(s->screen!=ES2_SHELL_SCREEN_BUILD)return es2_fail(w->e,ES2_ERR_ARGUMENT,at);
        if(s->bay==-1 || s->herc_type<0)return es2_fail(w->e,ES2_ERR_OFFSET,at);   /* hangar[-1], HERC_INF[-1] */
        TRY(editor_view(w,s,&v));
        if(!v.build_enabled)return ES2_OK;
        {
            int32_t cost;
            TRY(build(w,x,s->bay,s->herc_type,&cost));
            w->total-=(uint32_t)cost;                         /* 0x40eee9 */
        }
        *applied=1;return ES2_OK;
    default:
        break;
    }
    /* REPAIR screen controls */
    if(s->screen!=ES2_SHELL_SCREEN_REPAIR)return es2_fail(w->e,ES2_ERR_ARGUMENT,at);
    switch(op->kind) {
    case ES2_SHELL_EDITOR_REPAIR_PICK:
        if(!row_valid(a,op->b))return es2_fail(w->e,ES2_ERR_ARGUMENT,at);
        return pick(w,s,a,op->b,at,applied);
    case ES2_SHELL_EDITOR_REPAIR:
    case ES2_SHELL_EDITOR_REPAIR_ALL:
    case ES2_SHELL_EDITOR_REPAIR_CANCEL:
        if(s->bay==-1)return es2_fail(w->e,ES2_ERR_OFFSET,at);   /* hangar[-1] is a pilot pointer */
        b=editor_bay(w,s);
        if(op->kind==ES2_SHELL_EDITOR_REPAIR_CANCEL) {           /* 0x435418..0x4354e8 */
            unsigned i;
            w->total=s->salvage;
            if(b)for(i=0;i<33;i++)cs(b,i,s->conditions[i]);
            *applied=1;return ES2_OK;
        }
        TRY(editor_view(w,s,&v));
        if(op->kind==ES2_SHELL_EDITOR_REPAIR) {                  /* 0x4351cf..0x43527f */
            if(!v.repair_enabled)return ES2_OK;
            w->total-=(uint32_t)v.item_cost;
            if(b)TRY(cond_set(w,b,s->family,s->index,repaired(w,x,v.condition)));
        } else {                                                 /* 0x4352f9..0x435354 */
            if(!v.repair_all_enabled)return ES2_OK;
            w->total-=(uint32_t)v.all_cost;
            if(b)set_all(b,100);
        }
        *applied=1;return ES2_OK;
    default:
        return es2_fail(w->e,ES2_ERR_ARGUMENT,at);
    }
}
static es2_status editor_session_check(world *w,const es2_shell_editor_session *s) {
    if(s->screen<-1 || s->screen>7 || s->bay<-1 || s->bay>7 || !row_valid(s->tab,s->row) ||
       s->family!=row_family(s->tab,s->row) || s->index!=row_index(s->family,s->row) || s->herc_type<-1 || s->herc_type>8)
        return es2_fail(w->e,ES2_ERR_ARGUMENT,0);
    if(s->order<0 || s->order>3 || s->crew[0]<-1 || s->crew[0]>2 || s->crew[1]<-1 || s->crew[1]>2 ||
       s->crew[2]<-1 || s->crew[2]>2 || s->armory_row<-1 || s->armory_row>=ES2_SHELL_ARMORY_ROWS ||
       s->building<0 || s->building>255)
        return es2_fail(w->e,ES2_ERR_ARGUMENT,0);
    return ES2_OK;
}
es2_status es2_shell_editors(const es2_vfs *v,es2_span tables,es2_span editor_tables_span,es2_span save,
    es2_shell_editor_session *session,es2_shell_editor_op *ops,size_t count,es2_shell_buffer *out,
    es2_shell_editor_view *view,es2_error *e) {
    world *w;editor_tables x;es2_shell_buffer o={0};es2_shell_editor_session s;es2_shell_editor_view vw;
    es2_status status=ES2_OK;size_t i;int *applied;
    if(!v || !session || !out || out->data || (count && !ops))return es2_fail(e,ES2_ERR_ARGUMENT,0);
    if(!editor_tables_span.data || editor_tables_span.size!=ES2_SHELL_EDITOR_TABLES_SIZE)return es2_fail(e,ES2_ERR_ARGUMENT,0);
    for(i=0;i<5;i++)x.band[i]=es2_le16s(editor_tables_span.data+2*i);
    for(i=0;i<9;i++){x.vehicle[i]=es2_le16s(editor_tables_span.data+10+2*i);x.capacity[i]=es2_le16s(editor_tables_span.data+28+2*i);}
    for(i=0;i<ES2_SHELL_ARMORY_ROWS;i++)x.rows[i]=es2_le16s(editor_tables_span.data+46+2*i);
    w=(world *)calloc(1,sizeof *w);
    applied=(int *)calloc(count+1,sizeof *applied);
    if(!w || !applied){free(w);free(applied);return es2_fail(e,ES2_ERR_CAPACITY,0);}
    w->e=e;s=*session;
    status=load_tables(w,v,tables);
    if(status==ES2_OK)status=bay_load(w,save);
    if(status==ES2_OK)status=editor_session_check(w,&s);
    for(i=0;i<count && status==ES2_OK;i++) {
        status=editor_op(w,&x,&s,&ops[i],i,&applied[i]);
        if(status!=ES2_OK && status!=ES2_ERR_CAPACITY)status=es2_fail(e,status,i);   /* the failing operation */
    }
    if(status==ES2_OK && view)status=editor_view(w,&s,&vw);
    if(status==ES2_OK && view)status=armory_view(w,&x,&s,&vw);
    if(status==ES2_OK)status=save_out(w,&o);
    if(status==ES2_OK) {
        for(i=0;i<count;i++)ops[i].applied=applied[i];
        *session=s;*out=o;if(view)*view=vw;es2_error_clear(e);
    }
    for(i=0;i<33;i++)free(w->cat[i].v);
    free(w);free(applied);return status;
}
