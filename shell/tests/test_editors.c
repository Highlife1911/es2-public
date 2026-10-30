#include "es2/shell/editors.h"
#include "es2/formats/sav.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The service-bay editors (es2/shell/editors.h) on synthetic states.
 * Every table, catalogue and SAV below is invented for the test; expected
 * values follow from the contract in vshell_ui.md section 12. */
static unsigned checks,failures;
#define CHECK(x) do {checks++;if(!(x)){failures++;fprintf(stderr,"line %d: %s\n",__LINE__,#x);}}while(0)
static void w(uint8_t *p,unsigned x){p[0]=(uint8_t)x;p[1]=(uint8_t)(x>>8);}
static unsigned u(const uint8_t *p){return p[0]+256u*p[1];}
static int s(const uint8_t *p){unsigned x=u(p);return x<32768?(int)x:(int)x-65536;}

typedef struct fixture {
    uint8_t tables[322],editor[98],herc[2+144],damage[102],weapons[2+33*11+2];
    uint8_t inventory[33][10*8];size_t inventory_count[33];uint8_t flags[33];
    uint8_t five[22],selection[152],choice[12],team[4],nine[18],tally[4],final[22],variables[2000];
    uint8_t prefixes[37][4],suffixes[37][25],bays[8][196];size_t bay_items[8];int bay_present[8];
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
static void cond(fixture *f,int index,int word,int value) { w(f->bays[index]+4+2*word,(unsigned)value); }
static void mount(fixture *f,int index,int slot,int item,int condition_word) {
    uint8_t *r=f->bays[index]+76+12*f->bay_items[index]++;
    w(r,(unsigned)slot);w(r+2,(unsigned)item);w(r+4,(unsigned)item);w(r+6,100);w(r+8,100);w(r+10,5);
    cond(f,index,23+slot,condition_word);
}
static void set_tally(fixture *f,uint32_t t) { w(f->tally,t&65535u);w(f->tally+2,t>>16); }
/* Part groups {0,1} {2,4} {3,5} {6} {7,9,11} {8,10,12}; thresholds and levels
 * as a band table; HERC_INF record r: vehicle r, +8 = 100, divisor 2 + r;
 * DAMAGE: every family weight 97 (100,000 >> 10), every item value 100;
 * WEAPONS: categories cost 2,000. Bay 0: player, vehicle 1, capacity 3, item
 * 3 in slot 0; bay 1: roster[0] order 1, vehicle 2, capacity 2; bay 2:
 * roster[12] order 2, vehicle 0. Salvage 100,000. */
static void init(fixture *f) {
    uint8_t *p;size_t i;
    const int groups[24]={0,1,-1,0, 2,4,-1,0, 3,5,-1,0, 6,-1,0,0, 7,9,11,-1, 8,10,12,-1};
    const int t[]={90,80,60,30,1,0},l[]={100,89,79,59,29,0},band[]={89,79,59,29,0},cap[]={3,5,5,8,9,9,10,4,7};
    memset(f,0,sizeof *f);
    for(i=0;i<24;i++)w(f->tables+2*i,(unsigned)groups[i]);
    for(i=0;i<6;i++){w(f->tables+48+2*i,(unsigned)t[i]);w(f->tables+60+2*i,(unsigned)l[i]);}
    for(i=0;i<33;i++)w(f->tables+138+2*i,65535);
    for(i=0;i<30;i++)w(f->tables+262+2*i,(unsigned)i);
    for(i=0;i<5;i++)w(f->editor+2*i,(unsigned)band[i]);
    for(i=0;i<9;i++){w(f->editor+10+2*i,(unsigned)i);w(f->editor+28+2*i,(unsigned)cap[i]);}
    for(i=0;i<26;i++)w(f->editor+46+2*i,(unsigned)(i+1));   /* ARMORY row r -> category r + 1 */
    w(f->herc,9);for(i=0;i<9;i++){p=f->herc+2+16*i;w(p,(unsigned)i);w(p+8,100);w(p+12,(unsigned)(2+i));}
    w(f->damage,1024);for(i=0;i<15;i++)w(f->damage+2+2*i,1);w(f->damage+32,1024);w(f->damage+34,33);
    for(i=0;i<33;i++)w(f->damage+36+2*i,100);
    p=f->weapons;w(p,33);p+=2;
    for(i=0;i<33;i++){w(p,(unsigned)i);w(p+2,2);p[4]='W';p[5]=0;w(p+6,2);p[8]=1;w(p+9,i?(unsigned)i:99u);p+=11;}
    w(p,0);
    for(i=0;i<33;i++)f->flags[i]=1;
    w(f->five,5);for(i=0;i<5;i++)w(f->five+2+4*i,(unsigned)i);
    for(i=0;i<36;i++)pilot(f->suffixes[i],-1,0,-1);
    pilot(f->suffixes[0],1,1,1);pilot(f->suffixes[12],2,1,2);pilot(f->suffixes[36],0,1,0);
    for(i=0;i<3;i++){w(f->choice+2*i,0);w(f->choice+6+2*i,1);}
    w(f->team,4);w(f->team+2,3);
    for(i=0;i<9;i++)w(f->nine+2*i,1);
    bay(f,0,1,3,100);mount(f,0,0,3,100);bay(f,1,2,2,100);bay(f,2,0,3,100);
    set_tally(f,100000);
}
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

static es2_error error;
static es2_sav sav;
static es2_shell_buffer out;
static es2_shell_editor_view view;
/* Runs ops on the fixture's state; on success *sav describes the output. */
static es2_status run(fixture *f,es2_shell_editor_session *session,es2_shell_editor_op *ops,size_t count) {
    static uint8_t image[16384];es2_vfs_file files[3];es2_vfs_source source;es2_vfs vfs;size_t n=compose(f,image,sizeof image);
    es2_status status;
    files[0]=(es2_vfs_file){"GAM/HERC_INF.DAT",{f->herc,sizeof f->herc}};
    files[1]=(es2_vfs_file){"GAM/DAMAGE.DAT",{f->damage,sizeof f->damage}};
    files[2]=(es2_vfs_file){"GAM/WEAPONS.DAT",{f->weapons,sizeof f->weapons}};
    source.kind=ES2_VFS_LOOSE;source.data.loose.files=files;source.data.loose.count=3;
    CHECK(es2_vfs_open(&source,1,&vfs,&error)==ES2_OK);
    free(out.data);memset(&out,0,sizeof out);
    status=es2_shell_editors(&vfs,(es2_span){f->tables,322},(es2_span){f->editor,98},(es2_span){image,n},session,ops,count,
        &out,&view,&error);
    if(status==ES2_OK)CHECK(es2_sav_parse((es2_span){out.data,out.size},&sav,&error)==ES2_OK);
    else CHECK(!out.data);
    return status;
}
static int has_bay(int bay_index) {
    size_t i;es2_sav_loadout l;es2_error e;
    for(i=0;i<sav.loadout_count;i++){CHECK(es2_sav_loadout_at(&sav,i,&l,&e)==ES2_OK);if(l.index==bay_index)return 1;}
    return 0;
}
static int condition(int bay_index,unsigned word) {
    size_t i;es2_sav_loadout l;es2_error e;
    for(i=0;i<sav.loadout_count;i++){CHECK(es2_sav_loadout_at(&sav,i,&l,&e)==ES2_OK);if(l.index==bay_index)return s(l.fixed.data+4+2*word);}
    return -1000;
}
static int fixed_word(int bay_index,unsigned offset) {
    size_t i;es2_sav_loadout l;es2_error e;
    for(i=0;i<sav.loadout_count;i++){CHECK(es2_sav_loadout_at(&sav,i,&l,&e)==ES2_OK);if(l.index==bay_index)return s(l.fixed.data+offset);}
    return -1000;
}
static uint32_t tally(void) { return u(sav.tally.data)|(uint32_t)u(sav.tally.data+2)<<16; }
static const uint8_t *suffix(size_t index) {
    static es2_sav_pilot p;es2_error e;
    if(index==36)return sav.player.suffix.data;
    CHECK(es2_sav_pilot_at(&sav,index,&p,&e)==ES2_OK);return p.suffix.data;
}
#define OP(k,a,b) ((es2_shell_editor_op){(k),(a),(b),-1})
#define OPEN_REPAIR OP(ES2_SHELL_EDITOR_OPEN,ES2_SHELL_SCREEN_REPAIR,0)
#define OPEN_BUILD OP(ES2_SHELL_EDITOR_OPEN,ES2_SHELL_SCREEN_BUILD,0)

static void repair_one(void) {
    static fixture f;es2_shell_editor_session x;es2_shell_editor_op op[4];
    /* External row 1 = part group {2,4}: words 50 and 70, average 60 (band
     * from 60): cost (80 - 60) * 97 / 100 = 19, both words become 80. */
    init(&f);cond(&f,0,2,50);cond(&f,0,4,70);
    es2_shell_editor_session_init(&x,0);
    op[0]=OPEN_REPAIR;op[1]=OP(ES2_SHELL_EDITOR_REPAIR_PICK,0,1);op[2]=OP(ES2_SHELL_EDITOR_REPAIR,0,0);
    CHECK(run(&f,&x,op,3)==ES2_OK && op[0].applied==1 && op[1].applied==1 && op[2].applied==1);
    CHECK(condition(0,2)==80 && condition(0,4)==80 && condition(0,3)==100 && tally()==100000-19);
    CHECK(x.screen==3 && x.bay==0 && x.tab==0 && x.row==1 && x.family==0 && x.index==1);
    CHECK(x.salvage==100000 && x.conditions[2]==50 && x.conditions[4]==70);
    /* the view of the end state: average 80 now, cost (90 - 80) * 97 / 100 */
    CHECK(view.condition==80 && view.item_cost==9 && view.repair_enabled && view.bank_cost==0);
    /* Internal row 3 = word 16 at 0: band of 0 (limit 1), cost 0, value 1;
     * hardpoint row 6 = slot 0 (item 3) at 85: cost (90 - 85) * 100 / 100,
     * value 90; at 95: cost (100 - 95) * 100 / 100, value 100. */
    init(&f);cond(&f,0,16,0);cond(&f,0,23,85);
    es2_shell_editor_session_init(&x,0);
    op[0]=OPEN_REPAIR;op[1]=OP(ES2_SHELL_EDITOR_REPAIR_PICK,1,3);op[2]=OP(ES2_SHELL_EDITOR_REPAIR,0,0);
    CHECK(run(&f,&x,op,3)==ES2_OK && op[2].applied==1 && condition(0,16)==1 && tally()==100000);
    op[1]=OP(ES2_SHELL_EDITOR_REPAIR_PICK,0,6);op[3]=OP(ES2_SHELL_EDITOR_REPAIR,0,0);
    es2_shell_editor_session_init(&x,0);
    CHECK(run(&f,&x,op,4)==ES2_OK && op[2].applied==1 && op[3].applied==1);
    CHECK(condition(0,23)==100 && tally()==100000-5-10 && x.family==2 && x.index==0);
    /* a negative condition: band 0 by 0x413e40 (cost (100 + 5) * 97 / 100 =
     * 101), but band 5 by 0x43e05f: the value becomes 1 (sic) */
    init(&f);cond(&f,0,13,-5);
    es2_shell_editor_session_init(&x,0);
    op[1]=OP(ES2_SHELL_EDITOR_REPAIR_PICK,1,0);
    CHECK(run(&f,&x,op,3)==ES2_OK && condition(0,13)==1 && tally()==100000-101);
    /* disabled: salvage - bank cost (5 entries of 2,000) < cost (unsigned) */
    init(&f);cond(&f,0,2,0);cond(&f,0,4,0);set_tally(&f,10000+18);
    {size_t i;for(i=0;i<5;i++)w(f.five+4+4*i,7);}
    es2_shell_editor_session_init(&x,0);
    op[1]=OP(ES2_SHELL_EDITOR_REPAIR_PICK,0,1);
    /* average 0: cost (1 - 0) * 97 / 100 = 0, enabled */
    CHECK(run(&f,&x,op,3)==ES2_OK && op[2].applied==1 && condition(0,2)==1);
    cond(&f,0,2,40);cond(&f,0,4,40);  /* cost (60 - 40) * 97 / 100 = 19 > 18 */
    es2_shell_editor_session_init(&x,0);
    CHECK(run(&f,&x,op,3)==ES2_OK && op[2].applied==0 && condition(0,2)==40 && tally()==10018);
    CHECK(view.bank_cost==10000 && view.item_cost==19 && !view.repair_enabled);
    set_tally(&f,10019);es2_shell_editor_session_init(&x,0);
    CHECK(run(&f,&x,op,3)==ES2_OK && op[2].applied==1 && condition(0,2)==60 && tally()==10000);
}
static void repair_all_and_cancel(void) {
    static fixture f;es2_shell_editor_session x;es2_shell_editor_op op[6];
    /* REPAIR ALL: (100 - c) * weight / 100 per part group, internal word and
     * mounted hardpoint below the capacity with a nonzero condition: group 0
     * at 50 -> 48, word 13 at 0 -> 97, slot 0 at 20 -> 80; then every word
     * 0..21 and the mounted slot become 100, an empty slot below the capacity
     * 100; word 22 and slots beyond the capacity stay. */
    init(&f);cond(&f,0,0,50);cond(&f,0,1,50);cond(&f,0,13,0);cond(&f,0,23,20);cond(&f,0,22,7);cond(&f,0,30,9);cond(&f,0,24,3);
    es2_shell_editor_session_init(&x,0);
    op[0]=OPEN_REPAIR;op[1]=OP(ES2_SHELL_EDITOR_REPAIR_ALL,0,0);
    CHECK(run(&f,&x,op,2)==ES2_OK && op[1].applied==1);
    CHECK(tally()==100000-48-97-80 && condition(0,0)==100 && condition(0,13)==100 && condition(0,23)==100);
    CHECK(condition(0,24)==100 && condition(0,22)==7 && condition(0,30)==9);
    /* CANCEL: salvage and all 33 words from the snapshot of the show */
    op[2]=OP(ES2_SHELL_EDITOR_REPAIR_CANCEL,0,0);
    es2_shell_editor_session_init(&x,0);
    CHECK(run(&f,&x,op,3)==ES2_OK && op[2].applied==1);
    CHECK(tally()==100000 && condition(0,0)==50 && condition(0,13)==0 && condition(0,23)==20 && condition(0,24)==3);
    /* disabled when the salvage is short; CANCEL still restores */
    set_tally(&f,224);es2_shell_editor_session_init(&x,0);
    CHECK(run(&f,&x,op,2)==ES2_OK && op[1].applied==0 && condition(0,0)==50 && !view.repair_all_enabled && view.all_cost==225);
    set_tally(&f,225);es2_shell_editor_session_init(&x,0);
    CHECK(run(&f,&x,op,2)==ES2_OK && op[1].applied==1 && tally()==0);
    /* a bay change in REPAIR takes the snapshot again: CANCEL after it keeps
     * the repair of the first bay and its cost */
    set_tally(&f,100000);cond(&f,1,13,0);
    es2_shell_editor_session_init(&x,0);
    op[2]=OP(ES2_SHELL_EDITOR_BAY,1,0);op[3]=OP(ES2_SHELL_EDITOR_REPAIR_ALL,0,0);op[4]=OP(ES2_SHELL_EDITOR_REPAIR_CANCEL,0,0);
    CHECK(run(&f,&x,op,5)==ES2_OK && op[2].applied==1 && op[3].applied==1);
    CHECK(tally()==100000-225 && condition(0,0)==100 && condition(1,13)==0 && x.bay==1 && x.salvage==100000-225);
}
static void selection_rules(void) {
    static fixture f;es2_shell_editor_session x;es2_shell_editor_op op[8];
    init(&f);
    /* rows: the same row changes nothing; hardpoint rows only below the
     * capacity (3) and with an item (slot 0 only) */
    es2_shell_editor_session_init(&x,0);
    op[0]=OPEN_REPAIR;op[1]=OP(ES2_SHELL_EDITOR_REPAIR_PICK,0,0);op[2]=OP(ES2_SHELL_EDITOR_REPAIR_PICK,0,7);
    op[3]=OP(ES2_SHELL_EDITOR_REPAIR_PICK,0,15);op[4]=OP(ES2_SHELL_EDITOR_REPAIR_PICK,0,6);op[5]=OP(ES2_SHELL_EDITOR_REPAIR_PICK,1,8);
    CHECK(run(&f,&x,op,6)==ES2_OK && op[1].applied==0 && op[2].applied==0 && op[3].applied==0 && op[4].applied==1 && op[5].applied==1);
    CHECK(x.tab==1 && x.row==8 && x.family==1 && x.index==8);
    /* the show: the bay is kept when it holds a finished Herc, else the
     * first finished bay; the row goes back to (0, 0) */
    w(f.bays[0]+70,50);
    es2_shell_editor_session_init(&x,0);x.tab=1;x.row=2;x.family=1;x.index=2;
    op[1]=OP(ES2_SHELL_EDITOR_BAY,0,0);op[2]=OP(ES2_SHELL_EDITOR_BAY,2,0);op[3]=OP(ES2_SHELL_EDITOR_BAY,2,0);op[4]=OP(ES2_SHELL_EDITOR_BAY,5,0);
    CHECK(run(&f,&x,op,5)==ES2_OK && x.bay==2 && x.tab==0 && x.row==0);
    CHECK(op[1].applied==0 && op[2].applied==1 && op[3].applied==0 && op[4].applied==0);
    CHECK(view.first_finished==1);
    /* no finished Herc: bay -1, the controls that read the hangar refuse */
    w(f.bays[1]+70,99);w(f.bays[2]+70,0);
    es2_shell_editor_session_init(&x,3);
    CHECK(run(&f,&x,op,1)==ES2_OK && x.bay==-1 && view.first_finished==-1 && !view.has_record && !view.repair_enabled);
    op[1]=OP(ES2_SHELL_EDITOR_REPAIR,0,0);
    es2_shell_editor_session_init(&x,3);
    CHECK(run(&f,&x,op,2)==ES2_ERR_OFFSET && error.offset==1 && x.bay==3 && x.screen==-1);
    op[1]=OP(ES2_SHELL_EDITOR_REPAIR_PICK,0,6);
    CHECK(run(&f,&x,op,2)==ES2_ERR_OFFSET);
    op[1]=OP(ES2_SHELL_EDITOR_REPAIR_PICK,1,0);
    CHECK(run(&f,&x,op,2)==ES2_OK && op[1].applied==1);
}
static void screens_and_leaving(void) {
    static fixture f;es2_shell_editor_session x;es2_shell_editor_op op[6];
    /* leaving REPAIR or BUILD runs 0x412a37: option 0 refills the five-entry
     * bank by priority (categories 1.. with fewer than two items) */
    init(&f);
    es2_shell_editor_session_init(&x,0);
    op[0]=OPEN_REPAIR;op[1]=OP(ES2_SHELL_EDITOR_CLOSE,ES2_SHELL_SCREEN_MAIN,0);
    CHECK(run(&f,&x,op,2)==ES2_OK && x.screen==0 && s(sav.five_entry_bank.data+4)==1 && s(sav.five_entry_bank.data)==0);
    /* option 1 drops entries the salvage cannot pay */
    {size_t i;for(i=0;i<5;i++)w(f.five+4+4*i,4);}
    w(f.five,0);set_tally(&f,5000);
    es2_shell_editor_session_init(&x,0);
    op[1]=OP(ES2_SHELL_EDITOR_CLOSE,ES2_SHELL_SCREEN_BAY,1);
    CHECK(run(&f,&x,op,2)==ES2_OK && s(sav.five_entry_bank.data)==3 && s(sav.five_entry_bank.data+4)==0 &&
          s(sav.five_entry_bank.data+16)==4 && s(sav.five_entry_bank.data+20)==4);
    /* REPAIR -> BUILD leaves REPAIR (rebalance) and selects type 0; the bar
     * button of the shown screen changes nothing */
    init(&f);
    es2_shell_editor_session_init(&x,0);
    op[1]=OPEN_BUILD;op[2]=OPEN_BUILD;op[3]=OP(ES2_SHELL_EDITOR_BUILD_TYPE,4,0);op[4]=OPEN_REPAIR;
    CHECK(run(&f,&x,op,5)==ES2_OK && op[1].applied==1 && op[2].applied==0 && op[3].applied==1 && op[4].applied==1);
    CHECK(x.screen==3 && x.herc_type==4 && s(sav.five_entry_bank.data+4)==1);
    /* WEAPONS was shown: OPEN leaves it with 0x412a37 too; after CREW the
     * REPAIR show changes the bay through CREW's case of 0x43dcdd */
    init(&f);
    es2_shell_editor_session_init(&x,0);x.screen=ES2_SHELL_SCREEN_WEAPONS;
    CHECK(run(&f,&x,op,1)==ES2_OK && s(sav.five_entry_bank.data+4)==1);
    es2_shell_editor_session_init(&x,5);x.screen=ES2_SHELL_SCREEN_CREW;
    CHECK(run(&f,&x,op,1)==ES2_OK && x.bay==0);
    es2_shell_editor_session_init(&x,1);x.screen=ES2_SHELL_SCREEN_CREW;
    CHECK(run(&f,&x,op,1)==ES2_OK && x.bay==1);
    /* refusals: unknown kind, values, screens */
    es2_shell_editor_session_init(&x,0);
    op[0]=OP(99,0,0);CHECK(run(&f,&x,op,1)==ES2_ERR_ARGUMENT);
    op[0]=OP(ES2_SHELL_EDITOR_OPEN,7,0);CHECK(run(&f,&x,op,1)==ES2_ERR_ARGUMENT);
    op[0]=OP(ES2_SHELL_EDITOR_OPEN,3,256);CHECK(run(&f,&x,op,1)==ES2_ERR_ARGUMENT);
    op[0]=OP(ES2_SHELL_EDITOR_REPAIR,0,0);CHECK(run(&f,&x,op,1)==ES2_ERR_ARGUMENT);
    op[0]=OP(ES2_SHELL_EDITOR_BUILD,0,0);CHECK(run(&f,&x,op,1)==ES2_ERR_ARGUMENT);
    op[0]=OP(ES2_SHELL_EDITOR_SCRAP,0,0);CHECK(run(&f,&x,op,1)==ES2_ERR_ARGUMENT);
    op[0]=OP(ES2_SHELL_EDITOR_BAY,0,0);CHECK(run(&f,&x,op,1)==ES2_ERR_ARGUMENT);
    op[0]=OP(ES2_SHELL_EDITOR_CLOSE,0,0);CHECK(run(&f,&x,op,1)==ES2_ERR_ARGUMENT);
    op[0]=OPEN_REPAIR;op[1]=OP(ES2_SHELL_EDITOR_CLOSE,4,0);CHECK(run(&f,&x,op,2)==ES2_ERR_ARGUMENT && error.offset==1);
    op[1]=OP(ES2_SHELL_EDITOR_REPAIR_PICK,1,9);CHECK(run(&f,&x,op,2)==ES2_ERR_ARGUMENT);
    op[1]=OP(ES2_SHELL_EDITOR_REPAIR_PICK,0,16);CHECK(run(&f,&x,op,2)==ES2_ERR_ARGUMENT);
    op[1]=OP(ES2_SHELL_EDITOR_BAY,8,0);CHECK(run(&f,&x,op,2)==ES2_ERR_ARGUMENT);
    op[1]=OP(ES2_SHELL_EDITOR_BUILD_TYPE,1,0);CHECK(run(&f,&x,op,2)==ES2_ERR_ARGUMENT);
    x.row=3;CHECK(run(&f,&x,op,0)==ES2_ERR_ARGUMENT);   /* family/index do not match the row */
    es2_shell_editor_session_init(&x,9);CHECK(run(&f,&x,op,0)==ES2_ERR_ARGUMENT);
    /* the session and the outputs stay unchanged on failure */
    es2_shell_editor_session_init(&x,0);op[0]=OPEN_REPAIR;op[1]=OP(99,0,0);op[0].applied=7;
    CHECK(run(&f,&x,op,2)==ES2_ERR_ARGUMENT && x.screen==-1 && op[0].applied==7 && !out.data);
}
static void build_rules(void) {
    static fixture f;es2_shell_editor_session x;es2_shell_editor_op op[8];
    /* type rows: disabled when the nine-word bank entry is 0; the show
     * selects type 0 even then. Build into an empty bay: record of type 5,
     * capacity 9, all words 100, progress 0, countdown = divisor 7; salvage
     * -100,000 (+8 = 100 * 1000). */
    init(&f);w(f.nine+2*0,0);w(f.nine+2*6,0);set_tally(&f,100001);
    es2_shell_editor_session_init(&x,0);
    op[0]=OPEN_BUILD;op[1]=OP(ES2_SHELL_EDITOR_BUILD_TYPE,6,0);op[2]=OP(ES2_SHELL_EDITOR_BAY,4,0);op[3]=OP(ES2_SHELL_EDITOR_BUILD_TYPE,5,0);
    op[4]=OP(ES2_SHELL_EDITOR_BUILD,0,0);op[5]=OP(ES2_SHELL_EDITOR_BUILD,0,0);
    CHECK(run(&f,&x,op,6)==ES2_OK && x.herc_type==5 && op[1].applied==0 && op[2].applied==1 && op[4].applied==1 && op[5].applied==0);
    CHECK(has_bay(4) && fixed_word(4,0)==5 && fixed_word(4,2)==5 && fixed_word(4,70)==0 && fixed_word(4,72)==7 && fixed_word(4,74)==9);
    CHECK(condition(4,0)==100 && condition(4,22)==100 && condition(4,32)==100 && tally()==1 && sav.loadout_count==4);
    CHECK(view.has_record && !view.build_enabled && view.build_cost==100000);
    /* not enabled with salvage - bank cost = cost (strict) or on a record */
    set_tally(&f,100000);es2_shell_editor_session_init(&x,0);
    CHECK(run(&f,&x,op,5)==ES2_OK && op[4].applied==0 && !has_bay(4) && !view.build_enabled);
    es2_shell_editor_session_init(&x,0);op[2]=OP(ES2_SHELL_EDITOR_BAY,1,0);set_tally(&f,900000);
    CHECK(run(&f,&x,op,5)==ES2_OK && op[4].applied==0 && fixed_word(1,0)==2);
    /* type 0 from the show is built although its row is disabled */
    es2_shell_editor_session_init(&x,7);
    op[1]=OP(ES2_SHELL_EDITOR_BUILD,0,0);
    CHECK(run(&f,&x,op,2)==ES2_OK && op[1].applied==1 && fixed_word(7,0)==0 && fixed_word(7,74)==3);
    /* a type outside the vehicle table: the original reads HERC_INF[-1] */
    w(f.editor+10+2*5,77);
    es2_shell_editor_session_init(&x,7);op[1]=OP(ES2_SHELL_EDITOR_BUILD_TYPE,5,0);op[2]=OP(ES2_SHELL_EDITOR_BUILD,0,0);
    CHECK(run(&f,&x,op,3)==ES2_ERR_OFFSET && error.offset==2);
    es2_shell_editor_session_init(&x,-1);op[1]=OP(ES2_SHELL_EDITOR_BUILD,0,0);
    CHECK(run(&f,&x,op,2)==ES2_ERR_OFFSET);
}
static void scrap_rules(void) {
    static fixture f;es2_shell_editor_session x;es2_shell_editor_op op[4];uint8_t *r;
    /* BUILD screen, bay 1 (roster[0], order 1, active): value 0x414174 =
     * sum over groups/words/slots below 80 of c * weight / 100 = 6 * 97 +
     * 9 * 97 (all 100) + slot 0 at 50 (item 9: 50 * 100 / 100); item 9 (50)
     * is dropped, item 8 in slot 1 (90) goes back to its list; bay 1 gone,
     * roster[0] bay -1 and inactive, team count 3 -> 2. */
    init(&f);mount(&f,1,0,9,50);mount(&f,1,1,8,90);
    es2_shell_editor_session_init(&x,1);
    op[0]=OPEN_BUILD;op[1]=OP(ES2_SHELL_EDITOR_SCRAP,0,0);
    CHECK(run(&f,&x,op,2)==ES2_OK && op[1].applied==1 && !has_bay(1) && sav.loadout_count==2);
    CHECK(tally()==100000+15*97+50 && sav.inventory[8].count==1 && sav.inventory[9].count==0);
    CHECK(s(suffix(0))==-1 && suffix(0)[2]==0 && s(sav.team.data+2)==2 && x.bay==1);
    /* REPAIR screen: the player's bay 0; the player's order 0 finds no
     * selected pilot with order 0 (orders 1, 2 and -1), so nobody is
     * deactivated; then the first finished bay (1) is selected and the
     * snapshot taken again */
    init(&f);cond(&f,1,13,30);
    es2_shell_editor_session_init(&x,0);
    op[0]=OPEN_REPAIR;op[1]=OP(ES2_SHELL_EDITOR_REPAIR_PICK,1,0);op[2]=OP(ES2_SHELL_EDITOR_SCRAP,0,0);
    CHECK(run(&f,&x,op,3)==ES2_OK && op[2].applied==1 && !has_bay(0) && s(suffix(36))==-1 && suffix(36)[2]==1);
    CHECK(s(sav.team.data+2)==3 && x.bay==1 && x.tab==0 && x.row==0 && x.conditions[13]==30 && x.salvage==tally());
    CHECK(sav.inventory[3].count==1);   /* item 3 (condition 100) back to its list */
    /* a pilot whose order an earlier group also has: that group's pilot is
     * deactivated (0x4108fa), not the scrapped bay's */
    init(&f);pilot(f.suffixes[0],7,1,2);pilot(f.suffixes[12],2,1,2);
    es2_shell_editor_session_init(&x,2);
    op[0]=OPEN_BUILD;op[1]=OP(ES2_SHELL_EDITOR_SCRAP,0,0);
    CHECK(run(&f,&x,op,2)==ES2_OK && op[1].applied==1 && s(suffix(12))==-1 && suffix(12)[2]==1 && suffix(0)[2]==0);
    CHECK(s(sav.team.data+2)==2);
    /* that pilot inactive: nothing to deactivate, the team count stays */
    pilot(f.suffixes[0],7,0,2);
    es2_shell_editor_session_init(&x,2);
    CHECK(run(&f,&x,op,2)==ES2_OK && op[1].applied==1 && suffix(0)[2]==0 && s(sav.team.data+2)==3);
    /* disabled: exactly one functional Herc in the hangar (bays 1 and 2
     * damaged), or the nine-word bank entry of the vehicle 0, or no record */
    init(&f);cond(&f,1,13,10);cond(&f,2,14,50);
    es2_shell_editor_session_init(&x,1);
    CHECK(run(&f,&x,op,2)==ES2_OK && op[1].applied==0 && has_bay(1) && view.one_functional && !view.scrap_enabled);
    init(&f);w(f.nine+2*2,0);
    es2_shell_editor_session_init(&x,1);
    CHECK(run(&f,&x,op,2)==ES2_OK && op[1].applied==0 && view.scrap_bank==0);
    es2_shell_editor_session_init(&x,5);
    CHECK(run(&f,&x,op,2)==ES2_OK && op[1].applied==0 && !view.has_record);
    /* a Herc under construction: value ((100 - progress) / 100) * +8 * 1000 */
    init(&f);w(f.bays[1]+70,0);
    es2_shell_editor_session_init(&x,1);
    CHECK(run(&f,&x,op,2)==ES2_OK && op[1].applied==1 && tally()==200000);
    init(&f);w(f.bays[1]+70,1);
    es2_shell_editor_session_init(&x,1);
    CHECK(run(&f,&x,op,2)==ES2_OK && op[1].applied==1 && tally()==100000);
    r=NULL;(void)r;
}
/* ---- ARMORY, weapons scrap, CREW (vshell_ui.md section 17) ---- */
static int five_entry(int i) { return s(sav.five_entry_bank.data+4+4*i); }
static int five_free(void) { return s(sav.five_entry_bank.data); }
static void items(fixture *f,int category,size_t n) {
    size_t k;
    for(k=0;k<n;k++){uint8_t *p=f->inventory[category]+10*k;memset(p,0,10);w(p,(unsigned)category);w(p+4,100);}
    f->inventory_count[category]=n;
}
#define OPEN_ARMORY OP(ES2_SHELL_EDITOR_OPEN,ES2_SHELL_SCREEN_ARMORY,0)
#define OPEN_CREW OP(ES2_SHELL_EDITOR_OPEN,ES2_SHELL_SCREEN_CREW,0)
#define ROW(r,b) OP(ES2_SHELL_EDITOR_ARMORY_ROW,(r),(b))
static void armory_rules(void) {
    static fixture f;es2_shell_editor_session x;es2_shell_editor_op op[10];
    /* manual building: the show selects row 0 (category 1); a second left
     * press adds category 1 to the first free entry while bank cost + 2,000
     * < salvage; another row is only selected */
    init(&f);w(f.five+4+4*1,9);
    es2_shell_editor_session_init(&x,0);x.building=1;
    op[0]=OPEN_ARMORY;op[1]=ROW(0,1);op[2]=ROW(0,1);op[3]=ROW(3,1);op[4]=ROW(3,1);
    CHECK(run(&f,&x,op,5)==ES2_OK && op[0].applied==1 && op[1].applied==1 && op[2].applied==1 && op[3].applied==1 && op[4].applied==1);
    CHECK(five_entry(0)==1 && five_entry(1)==9 && five_entry(2)==1 && five_entry(3)==4 && five_free()==2 && x.armory_row==3);
    CHECK(view.armory_category==4 && view.armory_in_bank==1 && view.armory_free==2 && view.armory_clear_enabled && view.armory_rows==0x3ffffffu);
    /* the other button on the selected row: all entries of the category
     * removed, one fewer added again (into the first free entries) */
    op[5]=ROW(0,1);op[6]=ROW(0,2);
    es2_shell_editor_session_init(&x,0);x.building=1;
    CHECK(run(&f,&x,op,7)==ES2_OK && op[6].applied==1);
    CHECK(five_entry(0)==1 && five_entry(1)==9 && five_entry(2)==0 && five_entry(3)==4 && five_free()==3);
    /* the other button on another row only selects it; Clear removes every
     * entry of the selected row's category */
    op[5]=ROW(1,2);op[6]=ROW(0,2);op[7]=OP(ES2_SHELL_EDITOR_ARMORY_CLEAR,0,0);
    es2_shell_editor_session_init(&x,0);x.building=1;
    CHECK(run(&f,&x,op,8)==ES2_OK && op[5].applied==1 && op[6].applied==1 && op[7].applied==1);
    CHECK(five_entry(0)==0 && five_entry(2)==0 && five_entry(3)==4 && five_free()==4 && x.armory_row==0);
    /* auto building: rows are only selected, Clear is disabled */
    es2_shell_editor_session_init(&x,0);
    CHECK(run(&f,&x,op,8)==ES2_OK && op[2].applied==0 && op[7].applied==0 && five_entry(0)==0 && five_free()==5);
    CHECK(!view.armory_clear_enabled);
    /* affordability: 0 + 2,000 < 4,000 adds, 2,000 + 2,000 < 4,000 does not;
     * a free count of 0 adds nothing; a row whose category flag is 0 is
     * disabled */
    init(&f);set_tally(&f,4000);
    es2_shell_editor_session_init(&x,0);x.building=1;
    op[1]=ROW(0,1);op[2]=ROW(0,1);
    CHECK(run(&f,&x,op,3)==ES2_OK && op[1].applied==1 && op[2].applied==0 && five_entry(0)==1 && five_entry(1)==0);
    init(&f);w(f.five,0);
    es2_shell_editor_session_init(&x,0);x.building=1;
    CHECK(run(&f,&x,op,2)==ES2_OK && op[1].applied==0 && five_entry(0)==0);
    init(&f);f.flags[2]=0;
    es2_shell_editor_session_init(&x,0);x.building=1;
    op[1]=ROW(1,1);
    CHECK(run(&f,&x,op,2)==ES2_OK && op[1].applied==0 && x.armory_row==0 && view.armory_rows==(0x3ffffffu&~2u));
    /* weapons scrap: 0x412c8e = max(1, cost / 1000 * count / 10) for a
     * nonempty list (category 5 costs 30,000, 8 items: 24), 0 for an empty
     * one; the list is freed, then 0x412a37(building): manual drops bank
     * entries the salvage cannot pay */
    init(&f);w(f.weapons+2+11*5+6,30);items(&f,5,8);items(&f,1,3);
    es2_shell_editor_session_init(&x,0);
    op[1]=ROW(4,1);op[2]=OP(ES2_SHELL_EDITOR_ARMORY_SCRAP,0,0);
    CHECK(run(&f,&x,op,3)==ES2_OK && op[2].applied==1 && tally()==100000+24000 && sav.inventory[5].count==0);
    CHECK(sav.inventory[1].count==3);
    es2_shell_editor_session_init(&x,0);
    op[1]=OP(ES2_SHELL_EDITOR_ARMORY_SCRAP,0,0);
    CHECK(run(&f,&x,op,2)==ES2_OK && tally()==101000 && sav.inventory[1].count==0);
    CHECK(run(&f,&x,op,2)==ES2_OK && view.armory_scrap_value==0);
    es2_shell_editor_session_init(&x,0);x.building=1;
    {size_t i;for(i=0;i<5;i++)w(f.five+4+4*i,7);}w(f.five,0);set_tally(&f,5000);
    op[1]=ROW(3,1);op[2]=OP(ES2_SHELL_EDITOR_ARMORY_SCRAP,0,0);
    CHECK(run(&f,&x,op,3)==ES2_OK && tally()==5000 && five_free()==3 && five_entry(0)==0 && five_entry(4)==7);
    /* leaving ARMORY resets the row; refusals */
    init(&f);
    es2_shell_editor_session_init(&x,0);
    op[1]=OP(ES2_SHELL_EDITOR_CLOSE,ES2_SHELL_SCREEN_MAIN,0);
    CHECK(run(&f,&x,op,2)==ES2_OK && x.armory_row==-1 && x.screen==0);
    op[1]=ROW(26,1);CHECK(run(&f,&x,op,2)==ES2_ERR_ARGUMENT);
    es2_shell_editor_session_init(&x,0);op[0]=ROW(0,1);CHECK(run(&f,&x,op,1)==ES2_ERR_ARGUMENT);
    w(f.editor+46,40);op[0]=OPEN_ARMORY;
    CHECK(run(&f,&x,op,1)==ES2_ERR_OFFSET);
}
static void crew_rules(void) {
    static fixture f;es2_shell_editor_session x;es2_shell_editor_op op[8];
    /* the show: the order pilots (group 0 order 1, group 1 order 2, none
     * for 3), the order rows select their bays (order 3: -1), the loop over
     * the bays ends on the last finished one; order 0; no state change */
    init(&f);
    es2_shell_editor_session_init(&x,-1);
    op[0]=OPEN_CREW;op[1]=OP(ES2_SHELL_EDITOR_CREW_PILOT,2,0);
    CHECK(run(&f,&x,op,2)==ES2_OK && op[0].applied==1 && op[1].applied==0);
    CHECK(x.screen==6 && x.bay==2 && x.order==0 && x.crew[0]==0 && x.crew[1]==1 && x.crew[2]==-1);
    CHECK(s(sav.team.data+2)==3 && s(suffix(36))==0 && s(suffix(0))==1 && s(suffix(12))==2);
    /* order 3 (bay -1), pilot of group 2 gets order 3 (no bay: inactive),
     * then bay 2: taken from group 1 (bay -1, deactivated), given to group
     * 2, activated (order 3 < capacity 4) */
    op[1]=OP(ES2_SHELL_EDITOR_CREW_ORDER,3,0);op[2]=OP(ES2_SHELL_EDITOR_CREW_PILOT,2,0);op[3]=OP(ES2_SHELL_EDITOR_BAY,2,0);
    es2_shell_editor_session_init(&x,-1);
    CHECK(run(&f,&x,op,4)==ES2_OK && op[1].applied==1 && op[2].applied==1 && op[3].applied==1);
    CHECK(x.bay==2 && x.order==3 && x.crew[2]==2);
    CHECK(s(suffix(24))==2 && suffix(24)[2]==1 && s(suffix(24)+5)==3);
    CHECK(s(suffix(12))==-1 && suffix(12)[2]==0 && s(suffix(12)+5)==2 && s(sav.team.data+2)==3);
    /* the same with capacity 3: order 3 is not below it, group 2 stays
     * inactive */
    w(f.team,3);es2_shell_editor_session_init(&x,-1);
    CHECK(run(&f,&x,op,4)==ES2_OK && s(suffix(24))==2 && suffix(24)[2]==0 && s(sav.team.data+2)==2);
    /* a pilot takes another's order: the old holder (group 0) keeps bay
     * and activity but loses the order; the assignment then takes the
     * selected bay 1 from it (bay -1, deactivated) and gives it to group 1,
     * whose order-2 slot is emptied */
    init(&f);
    op[1]=OP(ES2_SHELL_EDITOR_CREW_ORDER,1,0);op[2]=OP(ES2_SHELL_EDITOR_CREW_PILOT,1,0);
    es2_shell_editor_session_init(&x,-1);
    CHECK(run(&f,&x,op,3)==ES2_OK && x.bay==1 && x.crew[0]==1 && x.crew[1]==-1);
    CHECK(s(suffix(0))==-1 && suffix(0)[2]==0 && s(suffix(0)+5)==-1);
    CHECK(s(suffix(12))==1 && suffix(12)[2]==1 && s(suffix(12)+5)==1 && s(sav.team.data+2)==2);
    /* order 0: a bay selector gives the bay to the player and takes it from
     * group 0 */
    op[1]=OP(ES2_SHELL_EDITOR_BAY,1,0);
    es2_shell_editor_session_init(&x,-1);
    CHECK(run(&f,&x,op,2)==ES2_OK && op[1].applied==1 && s(suffix(36))==1 && s(suffix(0))==-1 && suffix(0)[2]==0);
    /* the selector refuses a bay without a finished Herc, and a vehicle-8
     * bay while a wingman order is selected (not for order 0) */
    init(&f);w(f.bays[2],8);w(f.bays[1]+70,99);
    op[1]=OP(ES2_SHELL_EDITOR_CREW_ORDER,3,0);op[2]=OP(ES2_SHELL_EDITOR_BAY,2,0);op[3]=OP(ES2_SHELL_EDITOR_BAY,1,0);
    es2_shell_editor_session_init(&x,-1);
    CHECK(run(&f,&x,op,4)==ES2_OK && op[2].applied==0 && op[3].applied==0 && x.bay==-1 && x.order==3);
    op[1]=OP(ES2_SHELL_EDITOR_CREW_ORDER,0,0);
    es2_shell_editor_session_init(&x,-1);
    CHECK(run(&f,&x,op,3)==ES2_OK && op[2].applied==1 && s(suffix(36))==2 && s(suffix(12))==-1);
    /* CLEAR with order 1: group 0 bay -1, inactive, no order; selection -1 */
    init(&f);
    op[1]=OP(ES2_SHELL_EDITOR_CREW_ORDER,1,0);op[2]=OP(ES2_SHELL_EDITOR_CREW_CLEAR,0,0);
    es2_shell_editor_session_init(&x,-1);
    CHECK(run(&f,&x,op,3)==ES2_OK && op[2].applied==1 && x.bay==-1 && x.crew[0]==-1);
    CHECK(s(suffix(0))==-1 && suffix(0)[2]==0 && s(suffix(0)+5)==-1 && s(sav.team.data+2)==2);
    /* REPAIR after it: CREW's case of 0x43dcdd with order 1 refuses the
     * first finished bay when it holds vehicle 8 (bay -1 stays); with order 0
     * it is taken */
    w(f.bays[0],8);
    op[3]=OPEN_REPAIR;
    es2_shell_editor_session_init(&x,-1);
    CHECK(run(&f,&x,op,4)==ES2_OK && x.screen==3 && x.bay==-1);
    op[1]=OP(ES2_SHELL_EDITOR_CREW_ORDER,0,0);op[2]=OP(ES2_SHELL_EDITOR_CREW_CLEAR,0,0);
    es2_shell_editor_session_init(&x,-1);
    CHECK(run(&f,&x,op,4)==ES2_OK && op[2].applied==0 && x.bay==0);
    /* refusals */
    op[1]=OP(ES2_SHELL_EDITOR_CREW_ORDER,4,0);CHECK(run(&f,&x,op,2)==ES2_ERR_ARGUMENT);
    op[1]=OP(ES2_SHELL_EDITOR_CREW_PILOT,3,0);CHECK(run(&f,&x,op,2)==ES2_ERR_ARGUMENT);
    es2_shell_editor_session_init(&x,-1);op[0]=OP(ES2_SHELL_EDITOR_CREW_CLEAR,0,0);CHECK(run(&f,&x,op,1)==ES2_ERR_ARGUMENT);
    es2_shell_editor_session_init(&x,-1);x.crew[1]=3;CHECK(run(&f,&x,op,0)==ES2_ERR_ARGUMENT);
}
int main(void) {
    armory_rules();
    crew_rules();
    repair_one();
    repair_all_and_cancel();
    selection_rules();
    screens_and_leaving();
    build_rules();
    scrap_rules();
    free(out.data);
    printf("%u checks, %u failures\n",checks,failures);
    return failures!=0;
}
