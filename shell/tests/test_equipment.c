#include "shell/src/equipment.h"
#include <stdio.h>
#include <string.h>

static unsigned checks,failures;
#define CHECK(x) do {checks++;if(!(x)){failures++;fprintf(stderr,"line %d: %s\n",__LINE__,#x);}}while(0)
static void w(uint8_t *p,unsigned x){p[0]=(uint8_t)x;p[1]=(uint8_t)(x>>8);}
typedef struct fixture {
    uint8_t tables[164],names[82],weapons_names[76],weapons[15],hercs[28];
    es2_vfs_file files[5];es2_vfs_source source;es2_vfs vfs;
    es2_shell_options options;es2_error error;
} fixture;
static void init(fixture *f) {
    size_t i;memset(f,0,sizeof *f);
    /* All random draws are zero. Empty-ordinal permutations are identities. */
    for(i=0;i<4;i++){w(f->tables+112+2*i,100);w(f->tables+120+2*i,(unsigned)(700+i));}
    for(i=0;i<9;i++){w(f->tables+128+2*i,(unsigned)(42+i));w(f->tables+146+2*i,9);}
    f->options.default_tables=(es2_span){f->tables,sizeof f->tables};f->options.language="GER";
    w(f->names,36);w(f->names+4,2);f->names[80]='x';
    w(f->weapons_names,33);w(f->weapons_names+4,2);f->weapons_names[74]='x';
    w(f->weapons,1);w(f->weapons+2,7);w(f->weapons+4,2);f->weapons[6]='x';w(f->weapons+8,1);
    f->weapons[10]=201;w(f->weapons+11,2);w(f->weapons+13,0);
    w(f->hercs,1);w(f->hercs+2,0);w(f->hercs+4,42);w(f->hercs+6,25);w(f->hercs+8,9);w(f->hercs+10,2);
    w(f->hercs+12,1);w(f->hercs+14,2);w(f->hercs+16,41);w(f->hercs+18,60);
    w(f->hercs+20,7);w(f->hercs+22,3);w(f->hercs+24,42);w(f->hercs+26,61);
    f->files[0]=(es2_vfs_file){"GER/ESNAMES.BIN",{f->names,sizeof f->names}};
    f->files[1]=(es2_vfs_file){"GER/WEAPONS.BIN",{f->weapons_names,sizeof f->weapons_names}};
    f->files[2]=(es2_vfs_file){"GAM/WEAPONS.DAT",{f->weapons,sizeof f->weapons}};
    f->files[3]=(es2_vfs_file){"GAM/HERCS.DAT",{f->hercs,sizeof f->hercs}};
    f->files[4]=(es2_vfs_file){"GAM/SYNTH.DAT",{f->hercs+4,24}};
    f->source.kind=ES2_VFS_LOOSE;f->source.data.loose.files=f->files;f->source.data.loose.count=5;
    CHECK(es2_vfs_open(&f->source,1,&f->vfs,&f->error)==ES2_OK);
}
static void defaults(void) {
    fixture f;es2_shell_equipment q={0},before;size_t i,g;
    init(&f);CHECK(shell_equipment_default(&f.vfs,&f.options,&q,&f.error)==ES2_OK);
    CHECK(q.initialized && q.count==1 && q.capacity==0 && q.player.active && q.player.variant==0 && q.player.rank==0);
    for(g=0;g<3;g++) {
        CHECK(q.selected[g]==0 && q.next[g]==1);
        for(i=0;i<12;i++) {
            const es2_shell_pilot *p=&q.pilots[g*12+i];
            CHECK(p->identity==g*4+i%4 && p->variant==(g*4+i%4)*3+i/4);
            CHECK(p->bay==-1 && p->order==-1 && !p->active && p->rank==0 && p->companion==700);
        }
    }
    CHECK(q.flags[7]==201 && q.flags[6]==0 && q.flags[8]==0);
    CHECK(q.bays[0].present && !q.bays[1].present && q.bays[0].capacity==9);
    CHECK(q.bays[0].items[1]==2 && q.bays[0].items[7]==3 && q.bays[0].items[2]==0);
    CHECK(q.bays[0].secondary[1]==60 && q.bays[0].secondary[7]==61 && q.bays[0].secondary[2]==5);
    for(i=0;i<33;i++)CHECK(q.bays[0].condition[i]==100);
    /* Strict threshold comparison: draw==threshold goes to the next rank. */
    w(f.tables+112,0);CHECK(shell_equipment_default(&f.vfs,&f.options,&q,&f.error)==ES2_OK && q.pilots[0].rank==1 && q.pilots[0].companion==701);
    for(i=0;i<4;i++)w(f.tables+112+2*i,65535);
    CHECK(shell_equipment_default(&f.vfs,&f.options,&q,&f.error)==ES2_OK && q.pilots[0].rank==0);
    /* Synthetic all-one RNG: the first seven draws are two. Empty-ordinal
     * placement gives identities [2,3,0,1] and variants [1,2,0], not swaps. */
    for(i=0;i<56;i++)w(f.tables+2*i,1);
    CHECK(shell_equipment_default(&f.vfs,&f.options,&q,&f.error)==ES2_OK);
    for(i=0;i<12;i++) {
        unsigned id=(unsigned)((i%4+2)%4),variant=(unsigned)((i/4+1)%3);
        CHECK(q.pilots[i].identity==id && q.pilots[i].variant==3*id+variant);
    }
    before=q;f.options.default_tables.size--;
    CHECK(shell_equipment_default(&f.vfs,&f.options,&q,&f.error)==ES2_ERR_ARGUMENT && !memcmp(&q,&before,sizeof q));
    f.options.default_tables.size++;
    w(f.hercs+2,7);
    CHECK(shell_equipment_default(&f.vfs,&f.options,&q,&f.error)==ES2_OK && q.bays[7].present && !q.bays[0].present);
    w(f.hercs+2,8);
    CHECK(shell_equipment_default(&f.vfs,&f.options,&q,&f.error)==ES2_ERR_OFFSET);
    w(f.hercs+2,0);w(f.hercs+4,99);
    CHECK(shell_equipment_default(&f.vfs,&f.options,&q,&f.error)==ES2_ERR_NOT_FOUND);
    w(f.hercs+4,42);w(f.tables+146,11);
    CHECK(shell_equipment_default(&f.vfs,&f.options,&q,&f.error)==ES2_ERR_COUNT);
    w(f.tables+146,9);
    /* Sparse IDs are bounded by vehicle capacity, not occupied count. */
    w(f.hercs+20,9);CHECK(shell_equipment_default(&f.vfs,&f.options,&q,&f.error)==ES2_ERR_OFFSET);
    w(f.hercs+20,1);CHECK(shell_equipment_default(&f.vfs,&f.options,&q,&f.error)==ES2_ERR_OFFSET);
    w(f.hercs+20,7);f.files[3].data.size--;
    CHECK(shell_equipment_default(&f.vfs,&f.options,&q,&f.error)!=ES2_OK);
    f.files[3].data.size++;f.names[81]='x';
    CHECK(shell_equipment_default(&f.vfs,&f.options,&q,&f.error)==ES2_ERR_STRING);
}
static void mission(void) {
    fixture f;es2_shell_equipment q;uint8_t bytes[4][144]={{0}};const uint8_t *records[4];
    es2_shell_loadout load;es2_shell_actor actors[4];size_t i;
    init(&f);
    for(i=0;i<4;i++){records[i]=bytes[i];w(bytes[i]+48,(unsigned)(42+i));w(bytes[i]+50,65535);w(bytes[i]+116,1234);}
    CHECK(shell_equipment_default(&f.vfs,&f.options,&q,&f.error)==ES2_OK);
    CHECK(shell_equipment_mission(&f.vfs,&f.options,&q,4,records,&f.error)==ES2_OK);
    /* Each next assignment reevaluates the preceding pilot's new order. The
     * last newly assigned pilot remains inactive on the first preparation. */
    CHECK(q.count==3 && q.player.bay==0);
    for(i=0;i<3;i++)CHECK(q.pilots[i*12].bay==(int)i+1 && q.pilots[i*12].order==(int)i+1 && q.pilots[i*12].active==(i<2));
    CHECK(q.bays[0].items[0]==-1 && q.bays[0].secondary[0]==1234 && q.bays[0].secondary[1]==5);
    CHECK(shell_equipment_project(&q,&load,actors,&f.error)==ES2_OK && load.count==3 && load.flags[7]==201);
    /* Retained preparation reevaluates all established orders first. */
    CHECK(shell_equipment_mission(&f.vfs,&f.options,&q,4,records,&f.error)==ES2_OK && q.count==4);
    CHECK(shell_equipment_project(&q,&load,actors,&f.error)==ES2_OK && load.count==4);
    CHECK(actors[1].vehicle==43 && actors[3].vehicle==45);
    f.options.mode=1;
    CHECK(shell_equipment_mission(&f.vfs,&f.options,&q,2,records,&f.error)==ES2_OK && q.count==2);
    CHECK(q.bays[3].present && q.pilots[0].active && !q.pilots[12].active);
    q.pilots[0].bay=-1;
    CHECK(shell_equipment_mission(&f.vfs,&f.options,&q,2,records,&f.error)==ES2_OK && q.count==1);
    /* First duplicate order wins; missing order leaves unrelated pilot alone. */
    q.pilots[12].order=1;q.pilots[12].active=0;q.pilots[24].order=-1;
    CHECK(shell_equipment_mission(&f.vfs,&f.options,&q,4,records,&f.error)==ES2_OK && !q.pilots[12].active);
    CHECK(shell_equipment_mission(&f.vfs,&f.options,&q,5,records,&f.error)==ES2_ERR_COUNT);
    f.options.mode=0;f.options.training_preset_path="GAM/SYNTH.DAT";
    for(i=0;i<4;i++) {
        CHECK(shell_equipment_default(&f.vfs,&f.options,&q,&f.error)==ES2_OK);
        f.options.training_preset_index=i<2?3:4;f.options.training_preset_flag=(uint8_t)(i%2);
        CHECK(shell_equipment_mission(&f.vfs,&f.options,&q,1,records,&f.error)==ES2_OK);
        CHECK(q.bays[0].items[1]==(i==2?2:0));
    }
    q.bays[0].present=0;
    CHECK(shell_equipment_project(&q,&load,actors,&f.error)==ES2_ERR_NOT_FOUND);
}
static void restore(void) {
    uint8_t bytes[8192]={0};size_t at=0,i,load,entry,end;es2_shell_state state={0},before;es2_error e;
    /* Synthetic SAV: no inventory objects, 36 one-character roster names,
     * one selected player bay with a sparse slot, and an opaque trailer. */
    at=33*3+22+152;
    for(i=0;i<37;i++) {
        if(i==36){at+=12;w(bytes+at,1);w(bytes+at+2,1);at+=4;}
        w(bytes+at,(unsigned)i);w(bytes+at+2,(unsigned)(i+100));w(bytes+at+4,2);bytes[at+6]='x';
        w(bytes+at+8,i==36?0:65535);bytes[at+10]=(uint8_t)(i==36);w(bytes+at+11,23);w(bytes+at+13,i==36?0:65535);
        at+=33;
    }
    w(bytes+at,1);at+=2;load=at;w(bytes+at,0);at+=2;
    w(bytes+at,42);for(i=0;i<33;i++)w(bytes+at+4+2*i,(unsigned)(80+i));
    w(bytes+at+74,9);w(bytes+at+76,1);at+=78;entry=at;
    w(bytes+at,7);w(bytes+at+2,3);w(bytes+at+10,4321);at+=12;
    at+=22;w(bytes+at+1998,32768);at+=2022;bytes[at++]=0x5a;end=at;
    state.rng.a=24;state.rng.b=55;state.reset_selectors[1]=-1;before=state;
    CHECK(es2_shell_restore_save((es2_span){bytes,end},&state,&e)==ES2_OK);
    CHECK(state.variables[999]==-32768 && state.equipment.player.variant==136 && state.equipment.player.rank==23);
    CHECK(state.equipment.bays[0].capacity==9 && state.equipment.bays[0].secondary[7]==4321 && state.equipment.bays[0].condition[32]==112);
    CHECK(state.rng.a==24 && state.reset_selectors[1]==-1);
    for(i=0;i<end-1;i++) {
        state=before;
        CHECK(es2_shell_restore_save((es2_span){bytes,i},&state,&e)!=ES2_OK);
        CHECK(!memcmp(&state,&before,sizeof state));
    }
    state=before;w(bytes+entry,10);
    CHECK(es2_shell_restore_save((es2_span){bytes,end},&state,&e)==ES2_ERR_OFFSET && !memcmp(&state,&before,sizeof state));
    w(bytes+entry,7);w(bytes+load+2+74,11);
    CHECK(es2_shell_restore_save((es2_span){bytes,end},&state,&e)==ES2_ERR_COUNT);
}
int main(void) {
    defaults();mission();restore();printf("equipment synthetic: %u checks, %u failures\n",checks,failures);return failures!=0;
}
