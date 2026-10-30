#include "es2/shell/practice.h"
#include "es2/shell/bytes.h"
#include "es2/shell/vshell_store.h"
#include "es2/formats/sav.h"
#include <stdlib.h>
#include <string.h>

/* Practice missions, INSTANT ACTION, VIEW DEMO and the DEBUG dialog
 * (vshell_ui.md section 15). The Herc type table and the player name come
 * in through es2_shell_practice_tables (from the generated span store,
 * vshell_store.h). */
#define TRY(call) do { es2_status status_=(call);if(status_!=ES2_OK)return status_; } while(0)

/* ES2_VSHELL_STORE answers from the span store. The section walk over a
 * file image is test code (shell/tests/vshell_file.c); any other image goes to
 * a test's file reader, without one it is refused. Every successful read is
 * noted for the measurement switch (off by default). */
static es2_shell_file_reader file_reader;
void es2_shell_set_file_reader(es2_shell_file_reader reader) { file_reader=reader; }
es2_status es2_shell_exe_span(es2_span image,uint32_t va,size_t size,es2_span *out,es2_error *e) {
    es2_status status;
    int store;
    if(!out || !image.data)return es2_fail(e,ES2_ERR_ARGUMENT,0);
    store=image.data==es2_vshell_store_handle;
    if(!store && !file_reader)return es2_fail(e,ES2_ERR_ARGUMENT,va);
    status=store?es2_vshell_store_span(va,size,out,e):file_reader(image,va,size,out,e);
    if(status==ES2_OK)es2_shell_span_log(store?"store":"file",va,size);
    return status;
}

/* The Herc type table 0x479c56 (12 words), the names at 0x479f70 and 0x47396b. */
es2_status es2_shell_practice_tables(es2_span image,uint8_t out[ES2_SHELL_PRACTICE_TABLES_SIZE],es2_error *e) {
    static const uint32_t spans[3][2]={{0x479c56,24},{0x479f70,8},{0x47396b,8}};
    uint8_t buffer[ES2_SHELL_PRACTICE_TABLES_SIZE];size_t at=0,i;
    if(!out)return es2_fail(e,ES2_ERR_ARGUMENT,0);
    for(i=0;i<3;i++) {
        es2_span s;TRY(es2_shell_exe_span(image,spans[i][0],spans[i][1],&s,e));
        memcpy(buffer+at,s.data,s.size);at+=s.size;
    }
    if(!memchr(buffer+24,0,8) || !memchr(buffer+32,0,8) || !buffer[24] || !buffer[32])return es2_fail(e,ES2_ERR_STRING,24);
    memcpy(out,buffer,sizeof buffer);
    return ES2_OK;
}

void es2_shell_practice_open(es2_shell_practice *p) {
    /* The button as the constructor 0x44b310 leaves it (H: enabled); every
     * show selects entry 0 before the button can be pressed. */
    if(p){p->index=-1;p->preset_flag=0;p->herc_enabled=1;}
}
int es2_shell_practice_herc_enabled(const es2_shell_practice *p) {
    return p && p->herc_enabled;
}

static const struct { uint8_t byte, modulus; uint16_t label; } controls[5]={
    {ES2_SHELL_PREFS_AMMO,2,0x12a},{ES2_SHELL_PREFS_DAMAGE,2,0x128},{ES2_SHELL_PREFS_DIFFICULTY,4,0x35},
    {ES2_SHELL_PREFS_HERC,9,0x6e},{ES2_SHELL_PREFS_TIME,2,0x12c}};

unsigned es2_shell_practice_label(int control,const es2_shell_prefs_session *s) {
    if(control<0 || control>4 || !s)return 0;
    /* `add ax, base` on the zero-extended byte (0x44c5f9, 0x44c6b6, 0x44c773, 0x44c8ed, 0x44c830) */
    return (unsigned)controls[control].label+s->current[controls[control].byte];
}

/* 0x44c40c(i): nothing when i is the selection; else +0x28 := low byte of
 * word i of the table (0x44c533..0x44c53d, 0x40ddfe), selection := i. */
static es2_status entry(es2_shell_practice *p,es2_shell_prefs_session *s,const uint8_t *tables,int i,int *changed,es2_error *e) {
    if(i<0 || i>10)return es2_fail(e,ES2_ERR_OFFSET,(size_t)(i<0?0:i));
    if(p->index==i){*changed=0;return ES2_OK;}
    /* 0x44c465..0x44c52d: entries below 8 set the Herc Type button +0x49 */
    if(i<8)p->herc_enabled=i>=4;
    s->current[ES2_SHELL_PREFS_HERC]=tables[2*i];
    p->index=(int16_t)i;*changed=1;
    return ES2_OK;
}
es2_status es2_shell_practice_entry(es2_shell_practice *p,es2_shell_prefs_session *s,
    const uint8_t tables[ES2_SHELL_PRACTICE_TABLES_SIZE],int index,int *changed,es2_error *e) {
    es2_shell_practice q;es2_shell_prefs_session t;int c=0;
    if(!p || !s || !tables)return es2_fail(e,ES2_ERR_ARGUMENT,0);
    q=*p;t=*s;
    TRY(entry(&q,&t,tables,index,&c,e));
    *p=q;*s=t;if(changed)*changed=c;
    es2_error_clear(e);return ES2_OK;
}

es2_status es2_shell_practice_show(es2_shell_practice *p,es2_shell_prefs_session *s,
    const uint8_t tables[ES2_SHELL_PRACTICE_TABLES_SIZE],const es2_span *file,
    uint8_t out[ES2_SHELL_PREFS_SIZE],es2_error *e) {
    es2_shell_practice q;es2_shell_prefs_session t;uint8_t o[ES2_SHELL_PREFS_SIZE];int c;
    if(!p || !s || !tables || !out)return es2_fail(e,ES2_ERR_ARGUMENT,0);
    q=*p;t=*s;
    TRY(entry(&q,&t,tables,0,&c,e));                                   /* 0x44c322 -> 0x44c40c(0) */
    TRY(es2_shell_prefs_store(&t,file,ES2_SHELL_PREFS_MODE,0,o,e));      /* 0x431f56: 0x40ec56(0) */
    *p=q;*s=t;memcpy(out,o,sizeof o);
    es2_error_clear(e);return ES2_OK;
}

es2_status es2_shell_practice_control(es2_shell_practice *p,es2_shell_prefs_session *s,
    int control,int click,int *applied,es2_error *e) {
    uint8_t v,n;
    if(!p || !s || control<0 || control>4 || (click!=ES2_SHELL_CLICK_LEFT && click!=ES2_SHELL_CLICK_RIGHT))
        return es2_fail(e,ES2_ERR_ARGUMENT,0);
    if(control==ES2_SHELL_PRACTICE_HERC && !es2_shell_practice_herc_enabled(p)) {
        /* disabled button: 0x409d8a ignores the press (+0x49 = 0) */
        if(applied)*applied=0;
        es2_error_clear(e);return ES2_OK;
    }
    v=s->current[controls[control].byte];n=controls[control].modulus;
    if(click==ES2_SHELL_CLICK_LEFT){v=(uint8_t)(v+1);if(v==n)v=0;}              /* 0x40de5b */
    else{v=(uint8_t)(v-1);if(v==0xff)v=(uint8_t)(n-1);}                          /* 0x40de88 */
    s->current[controls[control].byte]=v;
    if(applied)*applied=1;
    es2_error_clear(e);return ES2_OK;
}

static void launch_of(const es2_shell_practice *p,const es2_shell_prefs_session *s,const uint8_t *name,
    es2_shell_practice_launch *l) {
    memset(l,0,sizeof *l);
    memcpy(l->name,name,8);l->name[7]=0;
    l->rank=s->current[ES2_SHELL_PREFS_DIFFICULTY];
    l->position=p->index;
    memcpy(l->controls,s->current+ES2_SHELL_PREFS_AMMO,5);
    l->preset_flag=(uint8_t)(p->preset_flag!=0);
    l->preset_index=p->index;
}

es2_status es2_shell_practice_begin(const es2_shell_practice *p,es2_shell_prefs_session *s,
    const uint8_t tables[ES2_SHELL_PRACTICE_TABLES_SIZE],uint8_t out[ES2_SHELL_PREFS_SIZE],
    es2_shell_practice_launch *launch,es2_error *e) {
    if(!p || !s || !tables || !out || !launch)return es2_fail(e,ES2_ERR_ARGUMENT,0);
    if(p->index<0 || p->index>10)return es2_fail(e,ES2_ERR_OFFSET,0);
    es2_shell_prefs_accept(s,out);                                     /* 0x40dd6e(1), 0x40dd0a */
    launch_of(p,s,tables+24,launch);                                   /* 0x40e8a5(0x479f70, +0x27) */
    es2_error_clear(e);return ES2_OK;
}

es2_status es2_shell_instant_action(es2_shell_practice *p,es2_shell_prefs_session *s,
    const uint8_t tables[ES2_SHELL_PRACTICE_TABLES_SIZE],const es2_span *file,
    uint8_t mode_out[ES2_SHELL_PREFS_SIZE],uint8_t prefs_out[ES2_SHELL_PREFS_SIZE],
    es2_shell_practice_launch *launch,es2_error *e) {
    es2_shell_practice q;es2_shell_prefs_session t;uint8_t m[ES2_SHELL_PREFS_SIZE];int c;uint8_t v;
    if(!p || !s || !tables || !mode_out || !prefs_out || !launch)return es2_fail(e,ES2_ERR_ARGUMENT,0);
    if(s->current[ES2_SHELL_PREFS_INSTANT]>2)return es2_fail(e,ES2_ERR_OFFSET,ES2_SHELL_PREFS_INSTANT);
    q=*p;t=*s;
    q.preset_flag=1;                                                   /* 0x431945 */
    TRY(es2_shell_prefs_store(&t,file,ES2_SHELL_PREFS_MODE,0,m,e));      /* 0x431951: 0x40ec56(0) */
    TRY(entry(&q,&t,tables,8+t.current[ES2_SHELL_PREFS_INSTANT],&c,e));/* 0x44c58b: 0x44c40c(+0x2e + 8) */
    v=(uint8_t)(t.current[ES2_SHELL_PREFS_INSTANT]+1);if(v==3)v=0;     /* 0x40de5b(0x2e, 3) */
    t.current[ES2_SHELL_PREFS_INSTANT]=v;
    *p=q;*s=t;
    memcpy(mode_out,m,sizeof m);
    es2_shell_prefs_accept(s,prefs_out);                               /* 0x40dd6e(1), 0x40dd0a */
    launch_of(p,s,tables+32,launch);                                   /* 0x40e8a5(0x47396b, +0x27) */
    es2_error_clear(e);return ES2_OK;
}

es2_status es2_shell_debug_mission(const char *text,char *stem,size_t capacity,es2_error *e) {
    char buffer[32];size_t n,i;char *hat;
    if(!text || !stem || capacity<32)return es2_fail(e,ES2_ERR_ARGUMENT,0);
    n=strlen(text);
    if(n<1 || n>31)return es2_fail(e,ES2_ERR_LENGTH,n);
    memcpy(buffer,text,n+1);
    if((hat=strchr(buffer,'^'))!=NULL)*hat='_';                        /* the first '^' only */
    for(i=0;i<n;i++) {
        char c=buffer[i];
        if(!((c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9') || c=='_'))return es2_fail(e,ES2_ERR_STRING,i);
    }
    memcpy(stem,buffer,n+1);
    es2_error_clear(e);return ES2_OK;
}

/* ---- the SAV after a mode-0 preparation ------------------------------------ */
static es2_status vehicle_entry(const es2_shell_save_tables *t,int16_t vehicle,uint16_t *index,uint16_t *capacity,es2_error *e) {
    unsigned i;
    for(i=0;i<9;i++)if(es2_le16s(t->defaults.data+128+2*i)==vehicle) {
        uint16_t n=es2_le16(t->defaults.data+146+2*i);
        if(n>10)return es2_fail(e,ES2_ERR_COUNT,i);
        *index=(uint16_t)i;*capacity=n;return ES2_OK;
    }
    return es2_fail(e,ES2_ERR_NOT_FOUND,(uint16_t)vehicle);
}
static uint16_t item_index(const es2_shell_save_tables *t,int16_t id) {
    unsigned i;
    for(i=0;i<30;i++)if(es2_le16s(t->item_indices.data+2*i)==id)return (uint16_t)i;
    return 0xffff;
}
/* Suffix of a pilot record: bay word, active byte, rank, order word, ... */
static void assign(uint8_t *suffix,int16_t bay,uint8_t active,int16_t order) {
    es2_put16(suffix,(uint16_t)bay);suffix[2]=active;es2_put16(suffix+5,(uint16_t)order);
}

es2_status es2_shell_mode0_prepared_save(es2_span base,const es2_shell_save_tables *t,
    const es2_shell_state *after,const es2_shell_output *prepared,uint8_t *output,
    size_t capacity,size_t *required,es2_error *e) {
    es2_sav s;es2_sav_edit edit;es2_sav_pilot roster[36],player;es2_sav_loadout loadouts[8];
    uint8_t suffix[37][25],fixed[8][76],items[8][120],flags[33];
    int16_t variables[1000],team[2],odd[8][10];uint16_t selection[6];size_t i,j,n=0;int patch=0;
    const es2_shell_equipment *q;es2_sav written;es2_status status;
    if(!t || !after || !prepared || !required || !after->equipment.initialized || !prepared->variables.data ||
       prepared->variables.size!=2000)return es2_fail(e,ES2_ERR_ARGUMENT,0);
    if(!t->defaults.data || t->defaults.size!=164 || !t->item_indices.data || t->item_indices.size!=60)
        return es2_fail(e,ES2_ERR_ARGUMENT,0);
    q=&after->equipment;
    TRY(es2_sav_parse(base,&s,e));
    TRY(es2_sav_parse((es2_span){base.data,s.reader_end},&s,e));
    for(i=0;i<36;i++) {
        TRY(es2_sav_pilot_at(&s,i,&roster[i],e));
        memcpy(suffix[i],roster[i].suffix.data,25);
        assign(suffix[i],q->pilots[i].bay,q->pilots[i].active,q->pilots[i].order);
        roster[i].suffix=(es2_span){suffix[i],25};
    }
    player=s.player;memcpy(suffix[36],s.player.suffix.data,25);
    assign(suffix[36],q->player.bay,q->player.active,q->player.order);
    player.suffix=(es2_span){suffix[36],25};
    for(i=0;i<3;i++){selection[i]=q->selected[i];selection[3+i]=q->next[i];}
    team[0]=q->capacity;team[1]=q->count;
    for(i=0;i<33;i++)flags[i]=q->flags[i];
    /* The hangar writer 0x410c7c walks bays 0..7; a record is the vehicle, its
     * table index, the 33 condition words, progress 100, countdown 0 and the
     * capacity, then one entry per occupied slot in slot order. */
    for(i=0;i<8;i++) {
        const es2_shell_bay *b=&q->bays[i];uint16_t vindex,cap;size_t k=0;
        if(!b->present)continue;
        TRY(vehicle_entry(t,b->vehicle,&vindex,&cap,e));
        es2_put16(fixed[n],(uint16_t)b->vehicle);es2_put16(fixed[n]+2,vindex);
        for(j=0;j<33;j++)es2_put16(fixed[n]+4+2*j,b->condition[j]);
        es2_put16(fixed[n]+70,100);es2_put16(fixed[n]+72,0);es2_put16(fixed[n]+74,cap);
        for(j=0;j<cap && j<10;j++) {
            uint8_t *r;
            if(!b->items[j])continue;
            r=items[n]+12*k;
            /* An authored item outside 1..32 (TRAIN3 holds -1) is kept by the
             * original; formats:sav only writes IDs 0..32, so such an entry
             * is framed with ID 0 and its two words are put in afterwards. */
            odd[n][k]=(b->items[j]<1 || b->items[j]>32)?b->items[j]:0;
            if(odd[n][k])patch=1;
            es2_put16(r,(uint16_t)j);es2_put16(r+2,odd[n][k]?0u:(uint16_t)b->items[j]);es2_put16(r+4,item_index(t,b->items[j]));
            es2_put16(r+6,100);es2_put16(r+8,b->condition[23+j]);es2_put16(r+10,b->secondary[j]);k++;
        }
        loadouts[n].index=(int16_t)i;loadouts[n].fixed=(es2_span){fixed[n],76};
        loadouts[n].items=(es2_span){items[n],12*k};loadouts[n].count=k;loadouts[n].offset=0;n++;
    }
    for(i=0;i<1000;i++)variables[i]=es2_le16s(prepared->variables.data+2*i);
    memset(&edit,0,sizeof edit);
    edit.variables=variables;edit.roster=roster;edit.roster_selection=selection;edit.team=team;
    edit.player=&player;edit.inventory_flags=flags;edit.set_loadouts=1;edit.loadout_count=n;edit.loadouts=loadouts;
    status=es2_sav_write(&s,&edit,output,capacity,required,e);
    if(status!=ES2_OK || !patch || !output || !capacity)return status;
    TRY(es2_sav_parse((es2_span){output,*required},&written,e));
    for(i=0;i<n;i++) {
        es2_sav_loadout r;TRY(es2_sav_loadout_at(&written,i,&r,e));
        for(j=0;j<r.count;j++)if(odd[i][j])es2_put16(output+(r.items.data-output)+12*j+2,(uint16_t)odd[i][j]);
    }
    return ES2_OK;
}

es2_status es2_shell_practice_new_game_files(es2_span new_game,es2_shell_buffer *player,
    es2_shell_buffer *variables,es2_error *e) {
    es2_sav s;uint8_t *mec,*var;size_t i;
    if(!player || !variables || player->data || variables->data)return es2_fail(e,ES2_ERR_ARGUMENT,0);
    TRY(es2_sav_parse(new_game,&s,e));
    if(s.loadout_count || es2_le16s(s.team.data)>1)return es2_fail(e,ES2_ERR_UNSUPPORTED,0);
    mec=(uint8_t *)malloc(8+2+33);var=(uint8_t *)malloc(2000);
    if(!mec || !var){free(mec);free(var);return es2_fail(e,ES2_ERR_CAPACITY,0);}
    es2_put16(mec,0);es2_put16(mec+2,es2_le16(s.team.data+2));                         /* 0x40f708, [0x482b5e] */
    es2_put16(mec+4,es2_le16(s.player.prefix.data+2));es2_put16(mec+6,es2_le16(s.player.suffix.data+3)); /* [0x482b62], [0x482b85] */
    es2_put16(mec+8,33);for(i=0;i<33;i++)mec[10+i]=s.inventory[i].flag;      /* 0x412877 */
    memcpy(var,s.variables.data,2000);                                 /* 0x40ef83 */
    player->data=mec;player->size=43;variables->data=var;variables->size=2000;
    es2_error_clear(e);return ES2_OK;
}

/* ---- the launch ------------------------------------------------------------- */
void es2_shell_practice_result_free(es2_shell_practice_result *out) {
    if(!out)return;
    es2_shell_output_free(&out->files);
    free(out->new_game.data);free(out->save.data);
    memset(out,0,sizeof *out);
}

es2_status es2_shell_practice_new_game(const es2_vfs *v,const char *language,const es2_shell_save_tables *t,
    const es2_shell_practice_launch *l,const es2_span *carry,es2_shell_rng *rng,es2_shell_buffer *out,es2_error *e) {
    size_t need=0,size=0;uint8_t *full,*bytes;es2_sav s,c,w;es2_sav_edit edit;es2_status status;
    es2_shell_rng r;int16_t nine[9];size_t i;
    if(!v || !t || !l || !rng || !out || out->data || !memchr(l->name,0,sizeof l->name) || l->position<0)
        return es2_fail(e,ES2_ERR_ARGUMENT,0);
    if(carry && carry->data)TRY(es2_sav_parse(*carry,&c,e));
    r=*rng;
    TRY(es2_shell_new_game(v,language,t,l->name,l->rank,0,l->position,&r,NULL,0,&need,NULL,e));
    full=(uint8_t *)malloc(need?need:1);
    if(!full)return es2_fail(e,ES2_ERR_CAPACITY,need);
    status=es2_shell_new_game(v,language,t,l->name,l->rank,0,l->position,&r,full,need,&need,NULL,e);
    /* 0x410b11(hangar, 0) clears the hangar (0x410aaf) and loads HERCS.DAT
     * only in mode 1: every other byte is the mode-1 new game's. */
    if(status==ES2_OK)status=es2_sav_parse((es2_span){full,need},&s,e);
    memset(&edit,0,sizeof edit);edit.set_loadouts=1;edit.loadout_count=0;
    if(carry && carry->data) {
        for(i=0;i<9;i++)nine[i]=es2_le16s(c.nine_word_bank.data+2*i);
        edit.nine_word_bank=nine;
    }
    if(status==ES2_OK)status=es2_sav_write(&s,&edit,NULL,0,&size,e);
    bytes=status==ES2_OK?(uint8_t *)malloc(size?size:1):NULL;
    if(status==ES2_OK && !bytes)status=es2_fail(e,ES2_ERR_CAPACITY,size);
    if(status==ES2_OK)status=es2_sav_write(&s,&edit,bytes,size,&size,e);
    /* 0x40e8a5 rewrites neither the briefing selection (selection span +4..)
     * nor 0x4833ac (final fields +2..): the carried game's bytes, in place */
    if(status==ES2_OK && carry && carry->data && (status=es2_sav_parse((es2_span){bytes,size},&w,e))==ES2_OK) {
        memcpy(bytes+(w.selection.data-bytes)+4,c.selection.data+4,148);
        memcpy(bytes+(w.final_fields.data-bytes)+2,c.final_fields.data+2,20);
    }
    free(full);
    if(status!=ES2_OK){free(bytes);return status;}
    out->data=bytes;out->size=size;*rng=r;
    es2_error_clear(e);return ES2_OK;
}

es2_status es2_shell_practice_prepare(const es2_vfs *v,const char *language,
    const es2_shell_save_tables *t,es2_span preset_paths,
    const es2_shell_practice_launch *l,const char *mission,es2_span new_game,
    es2_shell_rng *rng,int16_t selectors[30],es2_shell_output *files,es2_shell_buffer *save,es2_error *e) {
    es2_shell_state state;es2_shell_options o;es2_shell_output f={0};es2_shell_buffer s={0};
    size_t need=0;es2_status status;uint8_t c4;
    if(!v || !t || !l || !rng || !selectors || !files || !save || files->script.data || files->strings.data ||
       files->variables.data || files->player.data || save->data || l->position<0)
        return es2_fail(e,ES2_ERR_ARGUMENT,0);
    memset(&o,0,sizeof o);
    o.mission=mission;o.language=language;o.group=0;o.position=l->position;o.mode=0;o.difficulty=0;
    o.mode0_values[0]=l->controls[0];o.mode0_values[1]=l->controls[1];
    o.mode0_values[2]=l->controls[2];o.mode0_values[3]=l->controls[4];
    o.default_tables=t->defaults;
    c4=l->controls[3];o.training_vehicle=c4;o.training_preset_flag=l->preset_flag;
    o.training_preset_index=l->preset_index;
    /* 0x41cbb1: index > 3 and flag 0 take path c4 of the nine at 0x47083c */
    if(l->preset_index>3 && !l->preset_flag) {
        if(c4>8 || preset_paths.size<9u*64 || !memchr(preset_paths.data+64*c4,0,64))return es2_fail(e,ES2_ERR_OFFSET,c4);
        o.training_preset_path=(const char *)preset_paths.data+64*c4;
    }
    memset(&state,0,sizeof state);
    TRY(es2_shell_restore_save(new_game,&state,e));
    state.rng=*rng;memcpy(state.reset_selectors,selectors,sizeof state.reset_selectors);
    TRY(es2_shell_prepare(v,&o,&state,&f,e));
    status=es2_shell_mode0_prepared_save(new_game,t,&state,&f,NULL,0,&need,e);
    if(status==ES2_OK && !(s.data=(uint8_t *)malloc(need?need:1)))status=es2_fail(e,ES2_ERR_CAPACITY,need);
    if(status==ES2_OK)status=es2_shell_mode0_prepared_save(new_game,t,&state,&f,s.data,need,&need,e);
    if(status!=ES2_OK){es2_shell_output_free(&f);free(s.data);return status;}
    s.size=need;
    *rng=state.rng;memcpy(selectors,state.reset_selectors,sizeof state.reset_selectors);
    *files=f;*save=s;
    es2_error_clear(e);return ES2_OK;
}

es2_status es2_shell_practice_run(const es2_vfs *v,const char *language,
    const es2_shell_save_tables *t,es2_span preset_paths,
    const es2_shell_practice_launch *l,const es2_span *carry,const char *mission,es2_shell_rng *rng,
    int16_t selectors[30],es2_shell_practice_result *out,es2_error *e) {
    es2_shell_practice_result r;es2_shell_rng stream;int16_t chosen[30];
    if(!rng || !selectors || !out || out->files.script.data || out->files.strings.data ||
       out->files.variables.data || out->files.player.data || out->new_game.data || out->save.data)
        return es2_fail(e,ES2_ERR_ARGUMENT,0);
    memset(&r,0,sizeof r);stream=*rng;memcpy(chosen,selectors,sizeof chosen);
    TRY(es2_shell_practice_new_game(v,language,t,l,carry,&stream,&r.new_game,e));
    {
        es2_status status=es2_shell_practice_prepare(v,language,t,preset_paths,l,mission,
            (es2_span){r.new_game.data,r.new_game.size},&stream,chosen,&r.files,&r.save,e);
        if(status!=ES2_OK){es2_shell_practice_result_free(&r);return status;}
    }
    *rng=stream;memcpy(selectors,chosen,sizeof chosen);*out=r;
    es2_error_clear(e);return ES2_OK;
}
