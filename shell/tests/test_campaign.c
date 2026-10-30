#include "es2/shell/campaign.h"
#include "es2/shell/hangar.h"
#include "es2/formats/player_mec.h"
#include "es2/formats/sav.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Synthetic campaign fixtures only: every table, catalogue, mission and SAV
 * below is invented for the test; expected values follow from the contract
 * in vshell_preparation.md ("Results path", "Debrief selection"). */
static unsigned checks,failures;
#define CHECK(x) do {checks++;if(!(x)){failures++;fprintf(stderr,"line %d: %s\n",__LINE__,#x);}}while(0)
static void w(uint8_t *p,unsigned x){p[0]=(uint8_t)x;p[1]=(uint8_t)(x>>8);}
static unsigned u(const uint8_t *p){return p[0]+256u*p[1];}
static int s(const uint8_t *p){unsigned x=u(p);return x<32768?(int)x:(int)x-65536;}

typedef struct fixture {
    uint8_t tables[322],herc[2+144],damage[102],weapons[2+33*11+2],career[64],missions[64];
    uint8_t msn[2048],language[256],empty_msn[64];size_t msn_size,language_size,career_size;
    /* SAV parts */
    uint8_t inventory[33][10*8];size_t inventory_count[33];uint8_t flags[33];
    uint8_t five[22],selection[152],choice[12],team[4],nine[18],tally[4],final[22],variables[2000];
    uint8_t prefixes[37][4],suffixes[37][25],bays[8][196];size_t bay_items[8];int bay_present[8];
    /* inputs */
    uint8_t bank[2000],results[1024];size_t results_size;
    es2_shell_results_input in;
} fixture;

static void pilot(uint8_t *x,int bay,int active,int order) {
    memset(x,0,25);w(x,(unsigned)bay);x[2]=(uint8_t)active;w(x+5,(unsigned)order);w(x+9,100);
}
static void bay(fixture *f,int index,int vehicle,int capacity,int condition) {
    uint8_t *b=f->bays[index];int i;
    memset(b,0,196);w(b,(unsigned)vehicle);w(b+2,(unsigned)vehicle);
    for(i=0;i<33;i++)w(b+4+2*i,(unsigned)condition);
    w(b+70,100);w(b+72,0);w(b+74,(unsigned)capacity);
    f->bay_present[index]=1;f->bay_items[index]=0;
}
static void mount(fixture *f,int index,int slot,int item,int condition_word) {
    uint8_t *r=f->bays[index]+76+12*f->bay_items[index]++;
    w(r,(unsigned)slot);w(r+2,(unsigned)item);w(r+4,(unsigned)item);w(r+6,100);w(r+8,100);w(r+10,5);
    w(f->bays[index]+4+2*(23+slot),(unsigned)condition_word);
}
static uint8_t *msn_list(uint8_t *p,size_t count,const uint8_t *records,size_t stride) {
    w(p,(unsigned)count);memcpy(p+2,records,count*stride);return p+2+count*stride;
}
/* Mission SYNTH: condition 1 = (bank[0] == 1), condition 2 always (type 3);
 * auxiliary records -1 (+6 = 77) and 40 (+6 = 55); debrief records A
 * (condition 1: texts 10, 11; auxiliary 40) and B (condition b_condition:
 * text 12). Texts 10 "Win", 11 "!", 12 "Lose". */
static void mission(fixture *f,int b_condition,int empty_five) {
    uint8_t c[28],a[16],d[128],*p=f->msn;size_t i;
    memset(c,0,sizeof c);w(c,1);w(c+2,65535);w(c+4,0);w(c+6,0);w(c+8,281);w(c+10,1);
    w(c+14,2);w(c+16,65535);w(c+18,3);w(c+20,7);w(c+24,1);
    w(a,65535);w(a+2,65535);w(a+6,77);w(a+8,40);w(a+10,65535);w(a+14,55);
    memset(d,255,sizeof d);w(d,1);w(d+2,10);w(d+4,11);w(d+62,40);w(d+64,(unsigned)b_condition);w(d+66,12);
    w(p,5);p+=2;
    for(i=1;i<=17;i++) {
        if(i==1)p=msn_list(p,2,c,14);
        else if(i==3)p=msn_list(p,2,a,8);
        else if(i==5)p=msn_list(p,empty_five?0:2,d,64);
        else {w(p,0);p+=2;}
    }
    f->msn_size=(size_t)(p-f->msn);
    p=f->language;w(p,3);p+=2;
    {const char *t[]={"Win","!","Lose"};for(i=0;i<3;i++){size_t n=strlen(t[i])+1;w(p,(unsigned)(10+i));w(p+2,65535);w(p+4,0);w(p+6,(unsigned)n);memcpy(p+8,t[i],n);p+=8+n;}}
    f->language_size=(size_t)(p-f->language);
}
static void init(fixture *f) {
    uint8_t *p;size_t i;
    memset(f,0,sizeof *f);
    /* Tables: part group i = {i}, thresholds/levels, unlock of category 3 by
     * variable 52 = 2, variable 21 adds item 7, item index list 0..29. */
    for(i=0;i<6;i++){w(f->tables+8*i,(unsigned)i);w(f->tables+8*i+2,65535);}
    {const int t[]={90,80,60,30,1,0},l[]={100,89,79,59,29,0};for(i=0;i<6;i++){w(f->tables+48+2*i,(unsigned)t[i]);w(f->tables+60+2*i,(unsigned)l[i]);}}
    for(i=0;i<33;i++)w(f->tables+138+2*i,65535);
    w(f->tables+72+2*3,2);w(f->tables+138+2*3,52);
    w(f->tables+204,7);
    for(i=0;i<30;i++)w(f->tables+262+2*i,(unsigned)i);
    /* HERC_INF: record r is vehicle r, word +8 = 100, build divisor 2. */
    w(f->herc,9);for(i=0;i<9;i++){p=f->herc+2+16*i;w(p,(unsigned)i);w(p+8,100);w(p+12,2);}
    /* DAMAGE: unit coefficients (1024 = 1.0 in the 10-bit products). */
    w(f->damage,1024);for(i=0;i<15;i++)w(f->damage+2+2*i,1);w(f->damage+32,1024);w(f->damage+34,33);
    for(i=0;i<33;i++)w(f->damage+36+2*i,100);
    /* WEAPONS: categories 0..32, raw cost 1, priority = ID (0: 99). */
    p=f->weapons;w(p,33);p+=2;
    for(i=0;i<33;i++){w(p,(unsigned)i);w(p+2,2);p[4]='W';p[5]=0;w(p+6,1);p[8]=1;w(p+9,i?(unsigned)i:99u);p+=11;}
    w(p,0);p+=2;f->career_size=0;
    /* CAREER: three groups with 1, 7 and 1 entries, all mission 0. */
    p=f->career;w(p,3);p+=2;
    {const unsigned n[]={1,7,1};size_t g,k;for(g=0;g<3;g++){w(p,(unsigned)g);w(p+2,n[g]);p+=4;for(k=0;k<n[g];k++){w(p,0);p+=2;}}}
    f->career_size=(size_t)(p-f->career);
    w(f->missions,1);w(f->missions+4,14);w(f->missions+8,0);memcpy(f->missions+10,"MSN\\SYNTH.MSN",14);
    mission(f,2,0);
    /* SAV: player in bay 0 (vehicle 1, item 3 in slot 0), member roster[0]
     * in bay 1 (vehicle 2) with order 1; team capacity/count 2/2. */
    for(i=0;i<33;i++)f->flags[i]=i!=3;
    w(f->five,5);for(i=0;i<5;i++)w(f->five+2+4*i,(unsigned)i);
    w(f->selection,1);w(f->selection+2,0);
    for(i=0;i<36;i++)pilot(f->suffixes[i],-1,0,-1);
    pilot(f->suffixes[0],1,1,1);pilot(f->suffixes[36],0,1,0);
    for(i=0;i<3;i++){w(f->choice+2*i,0);w(f->choice+6+2*i,1);}
    w(f->team,2);w(f->team+2,2);
    bay(f,0,1,2,100);mount(f,0,0,3,100);bay(f,1,2,2,100);
    w(f->tally,0xa0);w(f->tally+1,0x86);w(f->tally+2,1); /* 100000 */
    /* mission.var: unlock variable 52, two items of variable 21, vehicle 1 (60). */
    w(f->bank+2*52,2);w(f->bank+2*21,2);w(f->bank+2*60,2);
    f->in.tables=(es2_span){f->tables,322};f->in.language="GER";f->in.mode=1;
    for(i=0;i<56;i++)f->in.rng.words[i]=(uint16_t)(i*7);
    f->in.rng.a=24;f->in.rng.b=55;
}
/* results.dat: outcome, tally, pairs, then per consumed pilot 33 condition
 * words (when its bay exists) and three result words. */
static void results(fixture *f,int outcome,unsigned long tally,int pair_item,const int *player,const int *member) {
    uint8_t *p=f->results;int i;
    w(p,(unsigned)outcome);w(p+2,(unsigned)(tally&65535u));w(p+4,(unsigned)(tally>>16));p+=6;
    if(pair_item>=0){w(p,1);w(p+2,(unsigned)pair_item);w(p+4,50);p+=6;}else{w(p,0);p+=2;}
    for(i=0;i<33;i++){w(p,(unsigned)(player?player[i]:100));p+=2;}
    w(p,1);w(p+2,2);w(p+4,3);p+=6;
    if(member) {for(i=0;i<33;i++){w(p,(unsigned)member[i]);p+=2;}w(p,4);w(p+2,5);w(p+4,6);p+=6;}
    f->results_size=(size_t)(p-f->results);
}
static es2_status run(fixture *f,es2_shell_results_output *out,es2_sav *sav,es2_error *e) {
    es2_vfs_file files[7];es2_vfs_source source;es2_vfs vfs;es2_sav_image m;es2_sav_pilot roster[37];
    es2_sav_loadout loadouts[8];static uint8_t image[16384];static uint8_t name[2]={'P',0};size_t i,n=0,required=0;es2_status status;
    memset(&m,0,sizeof m);
    for(i=0;i<33;i++){m.inventory[i].flag=f->flags[i];m.inventory[i].count=f->inventory_count[i];m.inventory[i].items=(es2_span){f->inventory[i],10*f->inventory_count[i]};}
    for(i=0;i<37;i++){roster[i].prefix=(es2_span){f->prefixes[i],4};roster[i].name=(es2_span){name,2};roster[i].suffix=(es2_span){f->suffixes[i],25};roster[i].offset=0;}
    for(i=0;i<8;i++)if(f->bay_present[i]){loadouts[n].index=(int16_t)i;loadouts[n].fixed=(es2_span){f->bays[i],76};
        loadouts[n].items=(es2_span){f->bays[i]+76,12*f->bay_items[i]};loadouts[n].count=f->bay_items[i];loadouts[n].offset=0;n++;}
    m.five_entry_bank=(es2_span){f->five,22};m.selection=(es2_span){f->selection,152};m.roster=roster;
    m.roster_selection=(es2_span){f->choice,12};m.team=(es2_span){f->team,4};m.player=&roster[36];
    m.loadout_count=n;m.loadouts=loadouts;m.nine_word_bank=(es2_span){f->nine,18};m.tally=(es2_span){f->tally,4};
    m.variables=(es2_span){f->variables,2000};m.final_fields=(es2_span){f->final,22};
    CHECK(es2_sav_compose(&m,image,sizeof image,&required,e)==ES2_OK);
    files[0]=(es2_vfs_file){"GAM/HERC_INF.DAT",{f->herc,sizeof f->herc}};
    files[1]=(es2_vfs_file){"GAM/DAMAGE.DAT",{f->damage,sizeof f->damage}};
    files[2]=(es2_vfs_file){"GAM/WEAPONS.DAT",{f->weapons,sizeof f->weapons}};
    files[3]=(es2_vfs_file){"GAM/CAREER.DAT",{f->career,f->career_size}};
    files[4]=(es2_vfs_file){"GER/MISSIONS.BIN",{f->missions,24}};
    files[5]=(es2_vfs_file){"MSN/SYNTH.MSN",{f->msn,f->msn_size}};
    files[6]=(es2_vfs_file){"MSN/SYNTH.GER",{f->language,f->language_size}};
    source.kind=ES2_VFS_LOOSE;source.data.loose.files=files;source.data.loose.count=7;
    CHECK(es2_vfs_open(&source,1,&vfs,e)==ES2_OK);
    f->in.save=(es2_span){image,required};f->in.variables=(es2_span){f->bank,2000};f->in.results=(es2_span){f->results,f->results_size};
    status=es2_shell_results(&vfs,&f->in,out,e);
    if(status!=ES2_OK)fprintf(stderr,"status %s offset %zu\n",es2_status_string(status),e->offset);
    if(status==ES2_OK && sav)CHECK(es2_sav_parse((es2_span){out->save.data,out->save.size},sav,e)==ES2_OK);
    return status;
}
static const uint8_t *suffix(const es2_sav *s,size_t index) {
    static es2_sav_pilot p;es2_error e;
    if(index==36)return s->player.suffix.data;
    CHECK(es2_sav_pilot_at(s,index,&p,&e)==ES2_OK);return p.suffix.data;
}
static int field(const uint8_t *x,unsigned offset) { return offset==0x24?x[2]:s(x+offset-0x22); }
static int bank_word(const es2_sav *sav,size_t i) { return s(sav->variables.data+2*i); }

static void survive_and_advance(void) {
    static fixture f;es2_shell_results_output o;es2_sav sav;es2_error e;const int all[33]={100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100};
    const uint8_t *x;es2_sav_loadout l;
    init(&f);results(&f,1,5000,5,NULL,all);memset(&o,0,sizeof o);
    CHECK(run(&f,&o,&sav,&e)==ES2_OK);
    CHECK(o.ending==ES2_SHELL_RESULTS_ADVANCED && o.advance==2 && o.group==1 && o.position==1 && o.final_word==0);
    CHECK(o.total==105000u && o.lost==0 && o.added==2);
    CHECK(bank_word(&sav,0)==1 && bank_word(&sav,8)==0 && bank_word(&sav,9)==0);
    CHECK(bank_word(&sav,52)==0 && bank_word(&sav,60)==0 && bank_word(&sav,21)==2);
    CHECK(sav.inventory[3].flag==1 && s(sav.nine_word_bank.data+2)==1 && s(sav.nine_word_bank.data)==0);
    /* pair item (5, index 5, 50, 100, 5); two items of variable 21 */
    CHECK(sav.inventory[5].count==1 && s(sav.inventory[5].items.data+4)==50 && s(sav.inventory[5].items.data+6)==100);
    CHECK(sav.inventory[7].count==2 && s(sav.inventory[7].items.data+8)==5);
    /* restock: priorities 1..5 fill the five free entries */
    {size_t i;CHECK(s(sav.five_entry_bank.data)==0);for(i=0;i<5;i++)CHECK(s(sav.five_entry_bank.data+4+4*i)==(int)i+1);}
    x=suffix(&sav,36);CHECK(field(x,0x2d)==1 && field(x,0x31)==2 && field(x,0x2f)==3 && field(x,0x33)==1 && field(x,0x37)==2 && field(x,0x35)==3 && field(x,0x39)==1);
    CHECK(field(x,0x25)==0 && field(x,0x29)==0);
    x=suffix(&sav,0);CHECK(field(x,0x25)==1 && field(x,0x29)==0 && field(x,0x39)==1); /* (4+6) mod 10 = 0 */
    CHECK(sav.loadout_count==2 && es2_sav_loadout_at(&sav,0,&l,&e)==ES2_OK && l.count==1);
    /* debrief A (condition 1); auxiliary 40 looked up compactly, used physically */
    CHECK(o.debrief && o.selected.passed==2 && o.selected.count==2 && o.selected.refs[0]==0 && o.selected.refs[1]==1 && o.selected.refs[2]==-1);
    CHECK(o.selected.aux==77);
    CHECK(o.text.size==4 && !memcmp(o.text.data,"Win!",4));
    CHECK(o.strings.size==4+2+3*3+4+2+5 && u(o.strings.data+4)==3);
    es2_shell_results_free(&o);
    /* failure: record B. Purchases of the five entries (1..5, 1000 each) come
     * first; restock option 1 then clears entries 0..3 until 1000 <= total. */
    init(&f);results(&f,0,0,-1,NULL,all);memset(&o,0,sizeof o);
    {size_t i;for(i=0;i<5;i++)w(f.five+4+4*i,(unsigned)(i+1));w(f.five,0);}
    w(f.tally,6000);w(f.tally+2,0);f.in.restock_option=1;f.in.repair_option=2;
    CHECK(run(&f,&o,&sav,&e)==ES2_OK);
    CHECK(o.advance==2 && o.selected.refs[0]==2 && o.selected.count==1 && o.selected.aux==-1);
    CHECK(o.text.size==4 && !memcmp(o.text.data,"Lose",4));
    CHECK(o.total==1000u && s(sav.five_entry_bank.data)==4 && s(sav.five_entry_bank.data+4+4*3)==0 && s(sav.five_entry_bank.data+4+4*4)==5);
    CHECK(sav.inventory[1].count==1 && sav.inventory[2].count==1 && sav.inventory[5].count==1);
    CHECK(s(sav.inventory[1].items.data+8)==5 && sav.inventory[13].count==0);
    es2_shell_results_free(&o);
}
static void losses(void) {
    static fixture f;es2_shell_results_output o;es2_sav sav;es2_error e;int zero[33],member[33];size_t i;
    for(i=0;i<33;i++){zero[i]=0;member[i]=100;}
    member[22]=0;
    /* player's Herc lost and player killed: final word 3, no advance */
    init(&f);results(&f,1,0,-1,zero,member);memset(&o,0,sizeof o);
    CHECK(run(&f,&o,&sav,&e)==ES2_OK);
    CHECK(o.ending==ES2_SHELL_RESULTS_PLAYER_LOST && o.advance==-1 && o.final_word==3 && !o.debrief);
    CHECK(sav.loadout_count==1 && o.lost==2 && bank_word(&sav,9)==2 && bank_word(&sav,8)==0);
    CHECK(o.group==1 && o.position==0);
    es2_shell_results_free(&o);
    /* member killed, Herc kept: replaced by roster[1], team count 1 */
    init(&f);results(&f,1,0,-1,NULL,member);memset(&o,0,sizeof o);
    CHECK(run(&f,&o,&sav,&e)==ES2_OK);
    CHECK(o.ending==ES2_SHELL_RESULTS_ADVANCED && o.lost==1 && s(sav.team.data+2)==1);
    CHECK(u(sav.roster_selection.data)==1 && u(sav.roster_selection.data+6)==2);
    CHECK(field(suffix(&sav,1),0x22)==-1 && field(suffix(&sav,0),0x22)==1); /* the old record keeps its bay */
    es2_shell_results_free(&o);
}
static void positions_and_hooks(void) {
    static fixture f;es2_shell_results_output o;es2_sav sav;es2_error e;const uint8_t *x;
    const int group[][3]={{1,6,1},{1,6,0},{2,0,1},{2,0,0}};const int expect[][4]={{2,0,2,0},{2,0,0,0},{3,0,1,1},{3,0,0,0}};size_t i;
    for(i=0;i<4;i++) {
        init(&f);w(f.selection,(unsigned)group[i][0]);w(f.selection+2,(unsigned)group[i][1]);results(&f,group[i][2],0,-1,NULL,(const int[33]){100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100});
        memset(&o,0,sizeof o);CHECK(run(&f,&o,&sav,&e)==ES2_OK);
        CHECK(o.group==expect[i][0] && o.position==expect[i][1] && o.advance==expect[i][2] && o.final_word==expect[i][3]);
        CHECK(o.text.size==(o.advance==2?(group[i][2]?4u:4u):0u));
        es2_shell_results_free(&o);
    }
    /* (1,2) -> (1,3): player bay 4 */
    init(&f);w(f.selection+2,2);results(&f,1,0,-1,NULL,(const int[33]){100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100});
    memset(&o,0,sizeof o);CHECK(run(&f,&o,&sav,&e)==ES2_OK && field(suffix(&sav,36),0x22)==4);es2_shell_results_free(&o);
    /* (1,5) -> (1,6): vehicle 8 in bay 3 removed, slot conditions 80 kept, 79 freed */
    init(&f);w(f.selection+2,5);bay(&f,3,8,3,100);mount(&f,3,0,9,80);mount(&f,3,1,10,79);mount(&f,3,2,11,32767);
    results(&f,1,0,-1,NULL,(const int[33]){100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100});
    memset(&o,0,sizeof o);CHECK(run(&f,&o,&sav,&e)==ES2_OK);
    CHECK(sav.loadout_count==2 && sav.inventory[9].count==1 && sav.inventory[10].count==0 && sav.inventory[11].count==1);
    CHECK(field(suffix(&sav,36),0x22)==0 && field(suffix(&sav,0),0x22)==1);es2_shell_results_free(&o);
    /* no vehicle 8: bay 7 holds vehicle 1, so the pilot in bay 1 loses its bay */
    init(&f);w(f.selection+2,5);bay(&f,7,1,2,100);
    results(&f,1,0,-1,NULL,(const int[33]){100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100});
    memset(&o,0,sizeof o);CHECK(run(&f,&o,&sav,&e)==ES2_OK);
    x=suffix(&sav,0);CHECK(field(x,0x22)==-1 && field(x,0x24)==1 && sav.loadout_count==3);es2_shell_results_free(&o);
}
static void unlocks_and_repairs(void) {
    static fixture f;es2_shell_results_output o;es2_sav sav;es2_error e;int damaged[33];size_t i;es2_sav_loadout l;
    /* HERC_INF record 0 is vehicle 8: the expected value 1 stays for record 1 */
    init(&f);w(f.herc+2,8);w(f.herc+2+16*8,0);w(f.bank+2*60,1);
    for(i=0;i<33;i++)damaged[i]=70;
    results(&f,1,0,-1,damaged,damaged);memset(&o,0,sizeof o);
    CHECK(run(&f,&o,&sav,&e)==ES2_OK && s(sav.nine_word_bank.data+2)==1 && bank_word(&sav,60)==0);
    /* the budget is large, so the first level (100) is applied to the
     * player's loadout: parts, family 1 and the occupied slot */
    CHECK(es2_sav_loadout_at(&sav,0,&l,&e)==ES2_OK && s(l.fixed.data+4)==100 && s(l.fixed.data+4+2*23)==100);
    es2_shell_results_free(&o);
    /* small budget: total 4000, count 2 -> budget 2000; cost at level 100 is
     * 6*(30*97/100)+9*(30*97/100)+30*100/100 = 6*29+9*29+30 = 465 < 2000 */
    init(&f);w(f.tally,4000);w(f.tally+2,0);results(&f,1,0,-1,damaged,damaged);memset(&o,0,sizeof o);
    CHECK(run(&f,&o,&sav,&e)==ES2_OK && o.total==4000u-465u-435u);
    es2_shell_results_free(&o);
    /* budget equal to the player's cost (930 / 2 = 465): strict comparison,
     * no player repair; the member's 435 is still below and is repaired */
    init(&f);w(f.tally,930);w(f.tally+2,0);results(&f,1,0,-1,damaged,damaged);memset(&o,0,sizeof o);
    CHECK(run(&f,&o,&sav,&e)==ES2_OK && o.total==930u-435u);
    CHECK(es2_sav_loadout_at(&sav,0,&l,&e)==ES2_OK && s(l.fixed.data+4)==70);
    es2_shell_results_free(&o);
}
static void debrief_quirks_and_errors(void) {
    static fixture f;es2_shell_results_output o,before;es2_error e;es2_sav sav;
    const int all[33]={100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100};
    /* no passing record: the last record read stays, unremapped */
    init(&f);mission(&f,99,0);w(f.selection+2,6);results(&f,0,0,-1,NULL,all);memset(&o,0,sizeof o);
    CHECK(run(&f,&o,&sav,&e)==ES2_OK && o.advance==0 && o.selected.passed==0 && o.selected.refs[0]==12 && o.text.size==0);
    es2_shell_results_free(&o);
    /* the same record would need text 12 for return 2: outside the table */
    init(&f);mission(&f,99,0);results(&f,0,0,-1,NULL,all);memset(&o,0,sizeof o);
    CHECK(run(&f,&o,NULL,&e)==ES2_ERR_OFFSET && !o.save.data && !o.text.data);
    init(&f);mission(&f,2,1);results(&f,1,0,-1,NULL,all);memset(&o,0,sizeof o);
    CHECK(run(&f,&o,NULL,&e)==ES2_ERR_NOT_FOUND); /* empty list 5: original assertion */
    init(&f);pilot(f.suffixes[36],5,1,0);results(&f,1,0,-1,NULL,all);memset(&o,0,sizeof o);
    CHECK(run(&f,&o,NULL,&e)==ES2_ERR_NOT_FOUND); /* consumed pilot without loadout */
    init(&f);results(&f,1,0,-1,NULL,all);f.results_size-=1;memset(&o,0,sizeof o);
    CHECK(run(&f,&o,NULL,&e)==ES2_ERR_TRUNCATED);
    init(&f);results(&f,1,0,-1,NULL,all);w(f.team+2,0);memset(&o,0,sizeof o);
    CHECK(run(&f,&o,NULL,&e)==ES2_ERR_ARGUMENT); /* budget division by count 0 */
    init(&f);results(&f,1,0,-1,NULL,all);f.in.tables.size=321;memset(&o,0,sizeof o);memset(&before,0,sizeof before);
    CHECK(run(&f,&o,NULL,&e)==ES2_ERR_ARGUMENT && !memcmp(&o,&before,sizeof o));
    /* mode 0: path ends after bank word 8; pairs are read but not applied */
    init(&f);f.in.mode=0;results(&f,1,0,5,NULL,all);memset(&o,0,sizeof o);
    CHECK(run(&f,&o,&sav,&e)==ES2_OK && o.ending==ES2_SHELL_RESULTS_OTHER_MODE && sav.inventory[5].count==0 && !o.debrief);
    es2_shell_results_free(&o);
}
/* The shell options in an invented prefs.cfg image (no original bytes). */
static void shell_options(void) {
    static fixture f;es2_shell_results_output o;es2_error e;uint8_t file[60],prefs[ES2_SHELL_PREFS_SIZE],before[ES2_SHELL_PREFS_SIZE];
    es2_span span;size_t i;es2_shell_results_input in;int damaged[33];
    const int bad[][3]={{2,-1,-1},{-2,-1,-1},{-1,3,-1},{-1,-2,-1},{-1,-1,2},{-1,-1,-2},{1,0,5}};
    for(i=0;i<sizeof file;i++)file[i]=(uint8_t)(0x80+i);
    /* a file that does not open leaves the zeroed block */
    memset(prefs,0xaa,sizeof prefs);es2_shell_prefs_load(NULL,prefs);
    for(i=0;i<sizeof prefs;i++)CHECK(prefs[i]==0);
    /* a short file fills its bytes, the rest stays zero */
    span=(es2_span){file,3};memset(prefs,0xaa,sizeof prefs);es2_shell_prefs_load(&span,prefs);
    CHECK(prefs[0]==0x80 && prefs[2]==0x82 && prefs[3]==0 && prefs[53]==0);
    /* a longer file: only the first 54 bytes */
    span=(es2_span){file,sizeof file};es2_shell_prefs_load(&span,prefs);
    CHECK(!memcmp(prefs,file,ES2_SHELL_PREFS_SIZE));
    /* mode word is the zero-extended byte +0x2a, options bytes +0x2c/+0x2d;
     * other input fields are untouched */
    memset(&in,0,sizeof in);in.group=7;prefs[0x2a]=0xff;prefs[0x2c]=2;prefs[0x2d]=1;
    es2_shell_prefs_options(prefs,&in);
    CHECK(in.mode==255 && in.repair_option==2 && in.restock_option==1 && in.group==7);
    /* UI choices store exactly these bytes; -1 keeps a byte */
    memcpy(before,prefs,sizeof prefs);
    CHECK(es2_shell_prefs_choose(prefs,1,0,-1,&e)==ES2_OK);
    CHECK(prefs[0x2a]==1 && prefs[0x2c]==0 && prefs[0x2d]==1);
    for(i=0;i<sizeof prefs;i++)if(i!=0x2a && i!=0x2c)CHECK(prefs[i]==before[i]);
    CHECK(es2_shell_prefs_choose(prefs,0,2,0,&e)==ES2_OK && prefs[0x2a]==0 && prefs[0x2c]==2 && prefs[0x2d]==0);
    CHECK(es2_shell_prefs_choose(prefs,-1,-1,-1,&e)==ES2_OK && prefs[0x2a]==0 && prefs[0x2c]==2 && prefs[0x2d]==0);
    /* values no UI control stores are rejected before anything changes */
    for(i=0;i<sizeof bad/sizeof bad[0];i++) {
        memcpy(before,prefs,sizeof prefs);
        CHECK(es2_shell_prefs_choose(prefs,bad[i][0],bad[i][1],bad[i][2],&e)==ES2_ERR_ARGUMENT && !memcmp(before,prefs,sizeof prefs));
    }
    CHECK(es2_shell_prefs_choose(NULL,1,0,0,&e)==ES2_ERR_ARGUMENT);
    /* the options reach the results path: the small-budget repair case of
     * unlocks_and_repairs (player 465, member 435) for repair 0, 1 and 2 */
    for(i=0;i<33;i++)damaged[i]=70;
    {
        const unsigned long expect[3]={4000ul-465ul-435ul,4000ul-435ul,4000ul};int r;
        for(r=0;r<3;r++) {
            memset(prefs,0,sizeof prefs);
            CHECK(es2_shell_prefs_choose(prefs,ES2_SHELL_MODE_CAMPAIGN,r,ES2_SHELL_BUILD_MANUAL,&e)==ES2_OK);
            init(&f);w(f.tally,4000);w(f.tally+2,0);results(&f,1,0,-1,damaged,damaged);
            es2_shell_prefs_options(prefs,&f.in);memset(&o,0,sizeof o);
            CHECK(run(&f,&o,NULL,&e)==ES2_OK && o.ending==ES2_SHELL_RESULTS_ADVANCED && o.total==expect[r]);
            es2_shell_results_free(&o);
        }
    }
    /* a single-mission entry (mode 0) ends the path before advancing */
    memset(prefs,0,sizeof prefs);CHECK(es2_shell_prefs_choose(prefs,ES2_SHELL_MODE_SINGLE,-1,-1,&e)==ES2_OK);
    init(&f);results(&f,1,0,-1,NULL,damaged);es2_shell_prefs_options(prefs,&f.in);memset(&o,0,sizeof o);
    CHECK(run(&f,&o,NULL,&e)==ES2_OK && o.ending==ES2_SHELL_RESULTS_OTHER_MODE);
    es2_shell_results_free(&o);
}
/* ---- Service bay (es2/shell/hangar.h) and launch files -------------- */
static size_t compose(fixture *f,uint8_t *image,size_t capacity) {
    es2_sav_image m;es2_sav_pilot roster[37];es2_sav_loadout loadouts[8];static uint8_t name[2]={'P',0};
    size_t i,n=0,required=0;es2_error e;
    memset(&m,0,sizeof m);
    for(i=0;i<33;i++){m.inventory[i].flag=f->flags[i];m.inventory[i].count=f->inventory_count[i];m.inventory[i].items=(es2_span){f->inventory[i],10*f->inventory_count[i]};}
    for(i=0;i<37;i++){roster[i].prefix=(es2_span){f->prefixes[i],4};roster[i].name=(es2_span){name,2};roster[i].suffix=(es2_span){f->suffixes[i],25};roster[i].offset=0;}
    for(i=0;i<8;i++)if(f->bay_present[i]){loadouts[n].index=(int16_t)i;loadouts[n].fixed=(es2_span){f->bays[i],76};
        loadouts[n].items=(es2_span){f->bays[i]+76,12*f->bay_items[i]};loadouts[n].count=f->bay_items[i];loadouts[n].offset=0;n++;}
    m.five_entry_bank=(es2_span){f->five,22};m.selection=(es2_span){f->selection,152};m.roster=roster;
    m.roster_selection=(es2_span){f->choice,12};m.team=(es2_span){f->team,4};m.player=&roster[36];
    m.loadout_count=n;m.loadouts=loadouts;m.nine_word_bank=(es2_span){f->nine,18};m.tally=(es2_span){f->tally,4};
    m.variables=(es2_span){f->variables,2000};m.final_fields=(es2_span){f->final,22};
    CHECK(es2_sav_compose(&m,image,capacity,&required,&e)==ES2_OK);
    return required;
}
/* Item record in an inventory list (memory order: the first record is the head). */
static void stock(fixture *f,int id,int word4,int secondary) {
    uint8_t *r=f->inventory[id]+10*f->inventory_count[id]++;
    w(r,(unsigned)id);w(r+2,(unsigned)id);w(r+4,(unsigned)word4);w(r+6,100);w(r+8,(unsigned)secondary);
}
static es2_error bay_error;
static es2_status bay_ops(fixture *f,es2_shell_bay_op *ops,size_t count,es2_sav *out,es2_shell_buffer *buffer,int with_vfs) {
    static uint8_t image[16384];es2_vfs_file files[3];es2_vfs_source source;es2_vfs vfs;size_t n=compose(f,image,sizeof image);
    es2_status status;
    files[0]=(es2_vfs_file){"GAM/HERC_INF.DAT",{f->herc,sizeof f->herc}};
    files[1]=(es2_vfs_file){"GAM/DAMAGE.DAT",{f->damage,sizeof f->damage}};
    files[2]=(es2_vfs_file){"GAM/WEAPONS.DAT",{f->weapons,sizeof f->weapons}};
    source.kind=ES2_VFS_LOOSE;source.data.loose.files=files;source.data.loose.count=3;
    CHECK(es2_vfs_open(&source,1,&vfs,&bay_error)==ES2_OK);
    free(buffer->data);memset(buffer,0,sizeof *buffer);
    status=es2_shell_service_bay(with_vfs?&vfs:NULL,(es2_span){f->tables,322},(es2_span){image,n},ops,count,buffer,&bay_error);
    if(status==ES2_OK)CHECK(es2_sav_parse((es2_span){buffer->data,buffer->size},out,&bay_error)==ES2_OK);
    else CHECK(!buffer->data);
    return status;
}
static const uint8_t *slot_record(const es2_sav *sav,int bay,int slot) {
    size_t i,j;es2_sav_loadout l;es2_error e;
    for(i=0;i<sav->loadout_count;i++) {
        CHECK(es2_sav_loadout_at(sav,i,&l,&e)==ES2_OK);
        if(l.index!=bay)continue;
        for(j=0;j<l.count;j++)if(s(l.items.data+12*j)==slot)return l.items.data+12*j+2;
    }
    return NULL;
}
static int condition(const es2_sav *sav,int bay,unsigned word) {
    size_t i;es2_sav_loadout l;es2_error e;
    for(i=0;i<sav->loadout_count;i++){CHECK(es2_sav_loadout_at(sav,i,&l,&e)==ES2_OK);if(l.index==bay)return s(l.fixed.data+4+2*word);}
    return -1;
}
#define OP(k,b,sl,v,a) ((es2_shell_bay_op){(k),(b),(sl),(v),(a),-1})
static void service_bay(void) {
    static fixture f;es2_sav sav;es2_shell_buffer out={0};es2_shell_bay_op op[4];const uint8_t *r;
    /* mount (0x411b10): the head of list 5 goes to slot 1 of bay 0, with
     * condition word 24 = its word +4 and word +8 = 5; the rest stays. */
    init(&f);stock(&f,5,60,9);stock(&f,5,70,9);
    op[0]=OP(ES2_SHELL_BAY_MOUNT,0,1,5,0);
    CHECK(bay_ops(&f,op,1,&sav,&out,0)==ES2_OK && op[0].applied==1);
    r=slot_record(&sav,0,1);
    CHECK(r && s(r)==5 && s(r+4)==60 && s(r+8)==5 && condition(&sav,0,24)==60);
    CHECK(sav.inventory[5].count==1 && s(sav.inventory[5].items.data+4)==70);
    /* the old item of slot 0 goes back to the head of its list */
    init(&f);stock(&f,5,60,9);stock(&f,3,40,9);
    op[0]=OP(ES2_SHELL_BAY_MOUNT,0,0,5,0);
    CHECK(bay_ops(&f,op,1,&sav,&out,0)==ES2_OK && s(slot_record(&sav,0,0))==5 && condition(&sav,0,23)==60);
    CHECK(sav.inventory[3].count==2 && s(sav.inventory[3].items.data+4)==100 && s(sav.inventory[3].items.data+14)==40);
    /* missile racks 13..15 get word +8 = 1, rack 16 5 */
    init(&f);stock(&f,14,50,0);stock(&f,16,50,0);
    op[0]=OP(ES2_SHELL_BAY_MOUNT,0,1,14,0);op[1]=OP(ES2_SHELL_BAY_MOUNT,1,0,16,0);
    CHECK(bay_ops(&f,op,2,&sav,&out,0)==ES2_OK && s(slot_record(&sav,0,1)+8)==1 && s(slot_record(&sav,1,0)+8)==5);
    /* an empty list: the old item goes back, the slot stays empty, the
     * condition word keeps its value; item 0 empties and sets 100 */
    init(&f);w(f.bays[0]+4+2*23,55);
    op[0]=OP(ES2_SHELL_BAY_MOUNT,0,0,7,0);
    CHECK(bay_ops(&f,op,1,&sav,&out,0)==ES2_OK && !slot_record(&sav,0,0) && condition(&sav,0,23)==55 && sav.inventory[3].count==1);
    op[0]=OP(ES2_SHELL_BAY_MOUNT,0,0,0,0);
    CHECK(bay_ops(&f,op,1,&sav,&out,0)==ES2_OK && !slot_record(&sav,0,0) && condition(&sav,0,23)==100 && sav.inventory[3].count==1);
    /* refusals leave everything; the error names the operation */
    op[0]=OP(ES2_SHELL_BAY_MOUNT,5,0,1,0);CHECK(bay_ops(&f,op,1,&sav,&out,0)==ES2_ERR_NOT_FOUND && op[0].applied==-1);
    op[0]=OP(ES2_SHELL_BAY_MOUNT,0,2,1,0);CHECK(bay_ops(&f,op,1,&sav,&out,0)==ES2_ERR_OFFSET);
    op[0]=OP(ES2_SHELL_BAY_MOUNT,0,0,33,0);CHECK(bay_ops(&f,op,1,&sav,&out,0)==ES2_ERR_ARGUMENT);
    op[0]=OP(ES2_SHELL_BAY_MOUNT,0,10,1,0);CHECK(bay_ops(&f,op,1,&sav,&out,0)==ES2_ERR_ARGUMENT);
    op[0]=OP(99,0,0,1,0);CHECK(bay_ops(&f,op,1,&sav,&out,0)==ES2_ERR_ARGUMENT);
    op[0]=OP(ES2_SHELL_BAY_MOUNT,0,1,0,0);op[1]=OP(ES2_SHELL_BAY_MOUNT,0,9,0,0);
    CHECK(bay_ops(&f,op,2,&sav,&out,0)==ES2_ERR_OFFSET && op[0].applied==-1 && bay_error.offset==1);
    /* the click (0x43fdac) */
    {
        static uint8_t image[16384];size_t n;
        init(&f);stock(&f,5,60,9);n=compose(&f,image,sizeof image);
        /* disabled rows (availability 0: item 3; no ARM record) and a bay
         * without a Herc: nothing */
        op[0]=OP(ES2_SHELL_BAY_CLICK,0,1,3,1);op[1]=OP(ES2_SHELL_BAY_CLICK,0,1,5,0);op[2]=OP(ES2_SHELL_BAY_CLICK,6,0,5,1);
        CHECK(bay_ops(&f,op,3,&sav,&out,0)==ES2_OK && !op[0].applied && !op[1].applied && !op[2].applied);
        CHECK(out.size==n && !memcmp(out.data,image,n));
        /* a list that is not empty mounts; item 0 always */
        op[0]=OP(ES2_SHELL_BAY_CLICK,0,1,5,1);op[1]=OP(ES2_SHELL_BAY_CLICK,0,0,0,1);
        CHECK(bay_ops(&f,op,2,&sav,&out,0)==ES2_OK && op[0].applied && op[1].applied && s(slot_record(&sav,0,1))==5 && !slot_record(&sav,0,0));
        /* empty list 6, slot 0 holds item 3 with word +8 = 5: nothing */
        op[0]=OP(ES2_SHELL_BAY_CLICK,0,0,6,1);
        CHECK(bay_ops(&f,op,1,&sav,&out,0)==ES2_OK && !op[0].applied && out.size==n && !memcmp(out.data,image,n));
        /* empty list 5 on an empty slot: 5 equals the empty slot's 5 (sic) */
        init(&f);op[0]=OP(ES2_SHELL_BAY_CLICK,0,1,5,1);
        CHECK(bay_ops(&f,op,1,&sav,&out,0)==ES2_OK && op[0].applied && !slot_record(&sav,0,1));
        /* empty list 4, slot word +8 = 4: the click empties the slot (sic) */
        init(&f);w(f.bays[0]+76+10,4);op[0]=OP(ES2_SHELL_BAY_CLICK,0,0,4,1);
        CHECK(bay_ops(&f,op,1,&sav,&out,0)==ES2_OK && op[0].applied && !slot_record(&sav,0,0) && sav.inventory[3].count==1);
        /* the slot already holds the ID (list empty): returned and taken
         * back, word +8 reset by the mount */
        init(&f);f.flags[3]=1;w(f.bays[0]+76+10,2);op[0]=OP(ES2_SHELL_BAY_CLICK,0,0,3,1);
        CHECK(bay_ops(&f,op,1,&sav,&out,0)==ES2_OK && op[0].applied && s(slot_record(&sav,0,0)+8)==5 && sav.inventory[3].count==0);
    }
    /* guidance (0x43e346) on missile racks only, modes 0..3 */
    init(&f);stock(&f,13,50,0);op[0]=OP(ES2_SHELL_BAY_MOUNT,0,1,13,0);op[1]=OP(ES2_SHELL_BAY_GUIDANCE,0,1,2,0);
    CHECK(bay_ops(&f,op,2,&sav,&out,0)==ES2_OK && op[1].applied && s(slot_record(&sav,0,1)+8)==2);
    op[1]=OP(ES2_SHELL_BAY_GUIDANCE,0,1,4,0);CHECK(bay_ops(&f,op,2,&sav,&out,0)==ES2_ERR_ARGUMENT);
    op[0]=OP(ES2_SHELL_BAY_GUIDANCE,0,0,1,0);CHECK(bay_ops(&f,op,1,&sav,&out,0)==ES2_ERR_ARGUMENT);
    op[0]=OP(ES2_SHELL_BAY_GUIDANCE,0,1,1,0);CHECK(bay_ops(&f,op,1,&sav,&out,0)==ES2_ERR_ARGUMENT);
    /* rebalance (0x412a37): needs the tables; option 0 refills by priority
     * (category 3 unavailable, category 2 already twice in stock), option 1
     * drops entries from the front until the salvage covers the bank */
    init(&f);op[0]=OP(ES2_SHELL_BAY_REBALANCE,0,0,0,0);
    CHECK(bay_ops(&f,op,1,&sav,&out,0)==ES2_ERR_ARGUMENT);
    stock(&f,2,100,5);stock(&f,2,100,5);
    CHECK(bay_ops(&f,op,1,&sav,&out,1)==ES2_OK && s(sav.five_entry_bank.data)==0);
    {const int want[5]={1,4,5,6,7};int i;for(i=0;i<5;i++)CHECK(s(sav.five_entry_bank.data+4+4*i)==want[i]);}
    init(&f);{int i;for(i=0;i<5;i++)w(f.five+4+4*i,(unsigned)(i+1));}w(f.five,0);w(f.tally,3500);w(f.tally+2,0);
    op[0]=OP(ES2_SHELL_BAY_REBALANCE,0,0,1,0);
    CHECK(bay_ops(&f,op,1,&sav,&out,1)==ES2_OK && s(sav.five_entry_bank.data)==2 && s(sav.five_entry_bank.data+4)==0 &&
          s(sav.five_entry_bank.data+8)==0 && s(sav.five_entry_bank.data+12)==3 && u(sav.tally.data)==3500);
    free(out.data);
}
static void launch_files(void) {
    static fixture f;static uint8_t image[16384];es2_shell_buffer mec={0},bank={0};es2_error e;es2_mec m;size_t n;
    init(&f);w(f.prefixes[36]+2,4);w(f.suffixes[36]+3,2);w(f.prefixes[0]+2,7);w(f.variables+20,0x1234);
    n=compose(&f,image,sizeof image);
    CHECK(es2_shell_launch_files((es2_span){image,n},&mec,&bank,&e)==ES2_OK);
    CHECK(bank.size==2000 && u(bank.data+20)==0x1234);
    CHECK(es2_mec_parse((es2_span){mec.data,mec.size},&m,&e)==ES2_OK);
    /* word 0, count 2, then the player's variant 4, rank 2, vehicle 1,
     * capacity 2, items 3 and 0, secondary 5 and 5; the member's variant 7;
     * at the end 33 and the availability bytes (category 3: 0) */
    CHECK(u(mec.data)==0 && u(mec.data+2)==2 && u(mec.data+4)==4 && u(mec.data+6)==2 && u(mec.data+8)==1 && u(mec.data+10)==2);
    CHECK(u(mec.data+12)==3 && u(mec.data+14)==0 && u(mec.data+16)==5 && u(mec.data+18)==5);
    CHECK(mec.size>=4+2*(76+8)+35 && u(mec.data+4+84)==7);
    CHECK(u(mec.data+mec.size-35)==33 && mec.data[mec.size-33+3]==0 && mec.data[mec.size-33+4]==1);
    free(mec.data);free(bank.data);mec.data=bank.data=NULL;
    /* capacity 0 (never prepared) and a count that is not the records' */
    w(f.team,0);n=compose(&f,image,sizeof image);
    CHECK(es2_shell_launch_files((es2_span){image,n},&mec,&bank,&e)!=ES2_OK && !mec.data && !bank.data);
    w(f.team,2);w(f.team+2,1);n=compose(&f,image,sizeof image);
    CHECK(es2_shell_launch_files((es2_span){image,n},&mec,&bank,&e)!=ES2_OK && !mec.data && !bank.data);
}
/* Rock & Roll's launch checks (0x445b99: player bay, 0x4110c1,
 * 0x411135, 0x40fc7e). The fixture's player is in bay 0 (item 3 in slot 0,
 * conditions and progress 100), member roster[0] (group 0) in bay 1 without
 * items, active, order 1, team capacity 2. */
static es2_status check_state(fixture *f,es2_shell_launch_check_result *r) {
    static uint8_t image[16384];es2_error e;size_t n=compose(f,image,sizeof image);
    memset(r,0x55,sizeof *r);
    return es2_shell_launch_check((es2_span){image,n},r,&e);
}
static int verdict(fixture *f) {
    es2_shell_launch_check_result r;
    return check_state(f,&r)==ES2_OK?r.warning:-100;
}
static void launch_checks(void) {
    static fixture f;es2_shell_launch_check_result r;
    init(&f);
    CHECK(check_state(&f,&r)==ES2_OK && r.player_bay==0 && r.functional==1 && r.armed==1 && r.squad_armed==0 &&
          r.warning==ES2_SHELL_LAUNCH_SQUAD_UNARMED);
    mount(&f,1,1,5,100);CHECK(verdict(&f)==ES2_SHELL_LAUNCH_OK);
    /* the member inactive, or its order not below the capacity: not checked */
    init(&f);f.suffixes[0][2]=0;CHECK(verdict(&f)==ES2_SHELL_LAUNCH_OK);
    init(&f);w(f.suffixes[0]+5,2);CHECK(verdict(&f)==ES2_SHELL_LAUNCH_OK);
    w(f.team,3);CHECK(verdict(&f)==ES2_SHELL_LAUNCH_SQUAD_UNARMED);
    w(f.team,0);CHECK(verdict(&f)==ES2_SHELL_LAUNCH_OK);
    /* the first group whose selected pilot has the order decides (0x4108fa):
     * group 0's pilot inactive with order 1 hides group 1's unarmed one */
    init(&f);f.suffixes[0][2]=0;pilot(f.suffixes[12],1,1,1);CHECK(verdict(&f)==ES2_SHELL_LAUNCH_OK);
    init(&f);w(f.suffixes[0]+5,-1);pilot(f.suffixes[12],1,1,1);CHECK(verdict(&f)==ES2_SHELL_LAUNCH_SQUAD_UNARMED);
    /* the selected pilot, not the roster index (selection word of group 0) */
    init(&f);w(f.choice,1);pilot(f.suffixes[1],1,0,1);CHECK(verdict(&f)==ES2_SHELL_LAUNCH_OK);
    /* a member Herc's bay without a record: unarmed */
    init(&f);w(f.suffixes[0],3);CHECK(verdict(&f)==ES2_SHELL_LAUNCH_SQUAD_UNARMED);
    /* an active member without a bay: the original reads outside the hangar */
    init(&f);w(f.suffixes[0],-1);
    CHECK(check_state(&f,&r)==ES2_ERR_OFFSET);
    w(f.suffixes[36],-1);   /* ... unless an earlier rule fails first */
    CHECK(check_state(&f,&r)==ES2_OK && r.warning==ES2_SHELL_LAUNCH_NO_HERC && r.functional==-1 && r.armed==-1 &&
          r.squad_armed==-1);
    /* player: no Herc, an empty bay, build progress, the four condition words */
    init(&f);w(f.suffixes[36],-1);CHECK(verdict(&f)==ES2_SHELL_LAUNCH_NO_HERC);
    init(&f);w(f.suffixes[36],2);
    CHECK(check_state(&f,&r)==ES2_OK && r.functional==0 && r.armed==0 && r.warning==ES2_SHELL_LAUNCH_NOT_FUNCTIONAL);
    init(&f);w(f.bays[0]+70,99);CHECK(verdict(&f)==ES2_SHELL_LAUNCH_NOT_FUNCTIONAL);
    {
        const int words[4]={13,14,18,21};int i,k;
        for(i=0;i<4;i++) {
            init(&f);mount(&f,1,0,5,100);w(f.bays[0]+4+2*words[i],50);CHECK(verdict(&f)==ES2_SHELL_LAUNCH_NOT_FUNCTIONAL);
            w(f.bays[0]+4+2*words[i],51);CHECK(verdict(&f)==ES2_SHELL_LAUNCH_OK);
            w(f.bays[0]+4+2*words[i],65535);CHECK(verdict(&f)==ES2_SHELL_LAUNCH_NOT_FUNCTIONAL);
        }
        /* every other word, the slot words 23.. included, does not matter */
        init(&f);mount(&f,1,0,5,100);
        for(k=0;k<33;k++)if(k!=13 && k!=14 && k!=18 && k!=21)w(f.bays[0]+4+2*k,0);
        CHECK(verdict(&f)==ES2_SHELL_LAUNCH_OK);
    }
    /* player's items: ID below 29 (signed) arms, 29 and up do not; item 0 in
     * a slot record arms (an ID, not an empty slot) */
    init(&f);mount(&f,1,0,5,100);w(f.bays[0]+76+2,29);CHECK(verdict(&f)==ES2_SHELL_LAUNCH_UNARMED);
    w(f.bays[0]+76+2,28);CHECK(verdict(&f)==ES2_SHELL_LAUNCH_OK);
    w(f.bays[0]+76+2,0);CHECK(verdict(&f)==ES2_SHELL_LAUNCH_OK);
    w(f.bays[0]+76+2,32);mount(&f,0,1,1,100);CHECK(verdict(&f)==ES2_SHELL_LAUNCH_OK);
    init(&f);mount(&f,1,0,5,100);f.bay_items[0]=0;
    CHECK(check_state(&f,&r)==ES2_OK && r.functional==1 && r.armed==0 && r.warning==ES2_SHELL_LAUNCH_UNARMED);
    /* the order of the rules: not functional before unarmed */
    w(f.bays[0]+70,0);CHECK(verdict(&f)==ES2_SHELL_LAUNCH_NOT_FUNCTIONAL);
    CHECK(es2_shell_launch_check((es2_span){NULL,0},&r,NULL)!=ES2_OK);
}
/* The preferences blocks (0x48259c current, 0x4825d2 committed). */
static void prefs_session(void) {
    es2_shell_prefs_session ps;uint8_t file[60],out[ES2_SHELL_PREFS_SIZE];es2_span span;es2_error e;size_t i;
    for(i=0;i<sizeof file;i++)file[i]=(uint8_t)(i+1);
    span=(es2_span){file,sizeof file};es2_shell_prefs_session_open(&span,&ps);
    CHECK(!memcmp(ps.current,file,54) && !memcmp(ps.committed,file,54));
    /* the controls: toggles 0 and 1, resolution 4, display 6, options */
    CHECK(es2_shell_prefs_control(&ps,ES2_SHELL_PREFS_MUSIC_TOGGLE,&e)==ES2_OK && ps.current[0]==0);
    CHECK(es2_shell_prefs_control(&ps,ES2_SHELL_PREFS_SFX_TOGGLE,&e)==ES2_OK && ps.current[1]==3);
    CHECK(es2_shell_prefs_control(&ps,ES2_SHELL_PREFS_LOW_RES,&e)==ES2_OK && ps.current[4]==1);
    CHECK(es2_shell_prefs_control(&ps,ES2_SHELL_PREFS_FULL_SCREEN,&e)==ES2_OK && ps.current[6]==1);
    CHECK(es2_shell_prefs_control(&ps,ES2_SHELL_PREFS_REPAIR_2,&e)==ES2_OK && ps.current[0x2c]==2);
    CHECK(es2_shell_prefs_control(&ps,ES2_SHELL_PREFS_BUILD_1,&e)==ES2_OK && ps.current[0x2d]==1);
    CHECK(ps.current[2]==3 && ps.committed[0]==1 && es2_shell_prefs_control(&ps,99,&e)==ES2_ERR_ARGUMENT);
    es2_shell_prefs_accept(&ps,out);
    CHECK(!memcmp(out,ps.current,54) && !memcmp(ps.committed,ps.current,54) && out[0]==0 && out[4]==1);
    CHECK(es2_shell_prefs_control(&ps,ES2_SHELL_PREFS_HIGH_RES,&e)==ES2_OK && ps.current[4]==0);
    CHECK(es2_shell_prefs_control(&ps,ES2_SHELL_PREFS_WINDOW,&e)==ES2_OK && ps.current[6]==0);
    es2_shell_prefs_cancel(&ps);CHECK(ps.current[4]==1 && ps.current[6]==1);
    /* a store: the file image with the one byte, the current block changed,
     * the committed one not; a later Cancel reverts the stored byte too */
    CHECK(es2_shell_prefs_store(&ps,&span,0x2a,1,out,&e)==ES2_OK && out[0x2a]==1 && out[0]==1 && out[0x2c]==0x2d);
    CHECK(ps.current[0x2a]==1 && ps.committed[0x2a]==0x2b);
    es2_shell_prefs_cancel(&ps);CHECK(ps.current[0x2a]==0x2b);
    /* a short file: the bytes after its end come from the current block */
    span.size=3;CHECK(es2_shell_prefs_store(&ps,&span,0x2a,0,out,&e)==ES2_OK);
    CHECK(out[0]==1 && out[2]==3 && out[3]==ps.current[3] && out[4]==1 && out[0x2c]==2 && out[0x2a]==0);
    /* no file: the whole current block */
    CHECK(es2_shell_prefs_store(&ps,NULL,6,0,out,&e)==ES2_OK && !memcmp(out,ps.current,54) && out[6]==0);
    CHECK(es2_shell_prefs_store(&ps,NULL,54,0,out,&e)==ES2_ERR_ARGUMENT);
}
int main(void) {
    survive_and_advance();losses();positions_and_hooks();unlocks_and_repairs();debrief_quirks_and_errors();
    shell_options();service_bay();launch_files();launch_checks();prefs_session();
    printf("campaign synthetic: %u checks, %u failures\n",checks,failures);return failures!=0;
}
