#include "es2/shell/save.h"
#include "es2/formats/sav.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Synthetic CD members only; no original bytes or tables. */
static unsigned checks,failures;
#define CHECK(x) do {checks++;if(!(x)){failures++;fprintf(stderr,"line %d: %s\n",__LINE__,#x);}}while(0)
static void w(uint8_t *p,unsigned x){p[0]=(uint8_t)x;p[1]=(uint8_t)(x>>8);}
static unsigned u(const uint8_t *p){return p[0]+256u*p[1];}
typedef struct fixture {
    uint8_t tables[164],items[60],names[8+72+36*3],weapons_names[8+66+33*3];
    uint8_t weapons[2+11+2+3*6],hercs[2+2+8+2*8],herc_inf[2+9*16];
    es2_vfs_file files[5];es2_vfs_source source;es2_vfs vfs;es2_shell_save_tables t;es2_error e;
} fixture;
static void bin(uint8_t *b,size_t count,const char *prefix) {
    size_t i,base=8+2*count,at=0;
    w(b,(unsigned)count);w(b+2,0);w(b+4,(unsigned)(count*3));w(b+6,0);
    for(i=0;i<count;i++){w(b+8+2*i,(unsigned)at);b[base+at]=(uint8_t)prefix[0];b[base+at+1]=(uint8_t)('A'+i%26);b[base+at+2]=0;at+=3;}
}
static void init(fixture *f) {
    size_t i;memset(f,0,sizeof *f);
    for(i=0;i<4;i++){w(f->tables+112+2*i,100);w(f->tables+120+2*i,(unsigned)(700+i));}
    for(i=0;i<9;i++){w(f->tables+128+2*i,(unsigned)(42+i));w(f->tables+146+2*i,i==1?4u:9u);}
    for(i=0;i<30;i++)w(f->items+2*i,(unsigned)(30-i));
    bin(f->names,36,"N");bin(f->weapons_names,33,"W");
    /* One weapon (ID 7, flag 201) and three inventory items: two of ID 7
     * with distinct condition words, one of ID 3. */
    w(f->weapons,1);w(f->weapons+2,7);w(f->weapons+4,2);f->weapons[6]='x';w(f->weapons+8,1);
    f->weapons[10]=201;w(f->weapons+11,2);w(f->weapons+13,3);
    w(f->weapons+15,7);w(f->weapons+17,90);w(f->weapons+19,5);
    w(f->weapons+21,7);w(f->weapons+23,80);w(f->weapons+25,6);
    w(f->weapons+27,3);w(f->weapons+29,70);w(f->weapons+31,0);
    /* HERCS: one record, bay 3, vehicle 43 (capacity 4), progress 0,
     * countdown 3, slots written out of order. */
    w(f->hercs,1);w(f->hercs+2,3);w(f->hercs+4,43);w(f->hercs+6,0);w(f->hercs+8,3);w(f->hercs+10,2);
    w(f->hercs+12,2);w(f->hercs+14,7);w(f->hercs+16,77);w(f->hercs+18,1);
    w(f->hercs+20,0);w(f->hercs+22,3);w(f->hercs+24,66);w(f->hercs+26,5);
    w(f->herc_inf,9);for(i=0;i<9;i++){w(f->herc_inf+2+16*i,(unsigned)i);w(f->herc_inf+2+16*i+14,(unsigned)(i%3==0));}
    f->files[0]=(es2_vfs_file){"GER/ESNAMES.BIN",{f->names,sizeof f->names}};
    f->files[1]=(es2_vfs_file){"GER/WEAPONS.BIN",{f->weapons_names,sizeof f->weapons_names}};
    f->files[2]=(es2_vfs_file){"GAM/WEAPONS.DAT",{f->weapons,sizeof f->weapons}};
    f->files[3]=(es2_vfs_file){"GAM/HERCS.DAT",{f->hercs,sizeof f->hercs}};
    f->files[4]=(es2_vfs_file){"GAM/HERC_INF.DAT",{f->herc_inf,sizeof f->herc_inf}};
    f->source.kind=ES2_VFS_LOOSE;f->source.data.loose.files=f->files;f->source.data.loose.count=5;
    CHECK(es2_vfs_open(&f->source,1,&f->vfs,&f->e)==ES2_OK);
    f->t.defaults=(es2_span){f->tables,164};f->t.item_indices=(es2_span){f->items,60};
}
static size_t make_default(fixture *f,uint8_t *out,size_t capacity) {
    size_t n=0;
    es2_status status=es2_shell_default_save(&f->vfs,"GER",&f->t,"Synthetic",4,2,5,out,capacity,&n,&f->e);
    CHECK(status==ES2_OK);
    if(status!=ES2_OK)fprintf(stderr,"default save: %s at %zu\n",es2_status_string(status),f->e.offset);
    return n;
}
static void defaults(void) {
    fixture f;uint8_t out[8192],guard[8192];size_t n,i,keep;es2_sav s;es2_sav_pilot p;es2_sav_loadout l;
    init(&f);n=make_default(&f,out,sizeof out);
    CHECK(es2_sav_parse((es2_span){out,n},&s,&f.e)==ES2_OK && s.trailing.size==0);
    if(failures)return;
    /* Items are prepended: category 7 lists the later DAT entry first. */
    CHECK(s.inventory[7].flag==201 && s.inventory[7].count==2 && s.inventory[3].count==1 && s.inventory[3].flag==0);
    CHECK(u(s.inventory[7].items.data)==7 && u(s.inventory[7].items.data+2)==23 && u(s.inventory[7].items.data+4)==100);
    CHECK(u(s.inventory[7].items.data+6)==80 && u(s.inventory[7].items.data+8)==6 && u(s.inventory[7].items.data+16)==90);
    CHECK(u(s.inventory[3].items.data+2)==27 && u(s.inventory[3].items.data+6)==70);
    CHECK(u(s.five_entry_bank.data)==5 && u(s.five_entry_bank.data+18)==4 && u(s.five_entry_bank.data+20)==0);
    CHECK(u(s.selection.data)==2 && u(s.selection.data+2)==5 && s.selection.data[151]==0);
    for(i=0;i<36;i++) {
        CHECK(es2_sav_pilot_at(&s,i,&p,&f.e)==ES2_OK);
        /* All-zero synthetic RNG: identities/variants follow roster order;
         * names are ESNAMES[variant]. */
        CHECK(p.name.size==3 && p.name.data[0]=='N' && p.name.data[1]=='A'+u(p.prefix.data+2)%26);
        CHECK(u(p.suffix.data)==65535 && p.suffix.data[2]==0 && u(p.suffix.data+5)==65535);
        CHECK(u(p.suffix.data+3)==0 && u(p.suffix.data+7)==700 && u(p.suffix.data+9)==100 && u(p.suffix.data+11)==0);
    }
    CHECK(u(s.roster_selection.data)==0 && u(s.roster_selection.data+6)==1 && u(s.team.data)==0 && u(s.team.data+2)==1);
    CHECK(s.player.name.size==10 && !memcmp(s.player.name.data,"Synthetic",10));
    CHECK(u(s.player.suffix.data)==0 && s.player.suffix.data[2]==1 && u(s.player.suffix.data+3)==4 && u(s.player.suffix.data+5)==0);
    CHECK(s.loadout_count==1 && es2_sav_loadout_at(&s,0,&l,&f.e)==ES2_OK && l.index==3 && l.count==2);
    CHECK(u(l.fixed.data)==43 && u(l.fixed.data+2)==1 && u(l.fixed.data+70)==0 && u(l.fixed.data+72)==3 && u(l.fixed.data+74)==4);
    CHECK(u(l.fixed.data+4)==100 && u(l.fixed.data+68)==100);
    CHECK(u(l.items.data)==0 && u(l.items.data+2)==3 && u(l.items.data+8)==66 && u(l.items.data+12)==2 && u(l.items.data+14)==7);
    CHECK(u(l.items.data+16)==23 && u(l.items.data+18)==100 && u(l.items.data+20)==77 && u(l.items.data+22)==1);
    for(i=0;i<9;i++)CHECK(u(s.nine_word_bank.data+2*i)==(i%3==0));
    for(i=0;i<2000;i++)CHECK(!s.variables.data[i]);
    /* Errors leave output and size untouched. */
    memset(guard,0x6b,sizeof guard);memcpy(out,guard,sizeof out);keep=31337;
    CHECK(es2_shell_default_save(&f.vfs,"GER",&f.t,"",0,1,0,out,sizeof out,&keep,&f.e)==ES2_ERR_LENGTH);
    CHECK(es2_shell_default_save(&f.vfs,"GER",&f.t,"0123456789012345678901234567890",0,1,0,out,sizeof out,&keep,&f.e)==ES2_ERR_LENGTH);
    f.t.item_indices.size=58;
    CHECK(es2_shell_default_save(&f.vfs,"GER",&f.t,"x",0,1,0,out,sizeof out,&keep,&f.e)==ES2_ERR_ARGUMENT);
    f.t.item_indices.size=60;w(f.herc_inf,8);f.files[4].data.size-=16;
    CHECK(es2_shell_default_save(&f.vfs,"GER",&f.t,"x",0,1,0,out,sizeof out,&keep,&f.e)==ES2_ERR_COUNT);
    w(f.herc_inf,9);f.files[4].data.size+=16;w(f.weapons+27,33);
    CHECK(es2_shell_default_save(&f.vfs,"GER",&f.t,"x",0,1,0,out,sizeof out,&keep,&f.e)==ES2_ERR_OFFSET);
    w(f.weapons+27,3);w(f.hercs+12,4);
    CHECK(es2_shell_default_save(&f.vfs,"GER",&f.t,"x",0,1,0,out,sizeof out,&keep,&f.e)==ES2_ERR_OFFSET);
    w(f.hercs+12,2);
    CHECK(es2_shell_default_save(&f.vfs,"FRE",&f.t,"x",0,1,0,out,sizeof out,&keep,&f.e)==ES2_ERR_NOT_FOUND);
    CHECK(keep==31337 && !memcmp(out,guard,sizeof out));
    CHECK(es2_shell_default_save(&f.vfs,"GER",&f.t,"x",0,1,0,out,10,&keep,&f.e)==ES2_ERR_CAPACITY && keep>10 && !memcmp(out,guard,sizeof out));
}
static void plans(void) {
    fixture f;uint8_t base[8192],out[8192],guard[8192],flags[33];size_t n,m,i,keep;es2_sav s;es2_sav_pilot p;es2_sav_loadout l;
    es2_shell_mount player_mounts[3]={{8,15,0,0},{1,7,1,1234},{4,25,0,0}},team_mounts[2]={{3,13,0,0},{0,5,0,0}};
    es2_shell_member team[3]={{0,11,{43,2,team_mounts}},{2,0,{50,0,NULL}},{1,4,{42,1,team_mounts}}};
    es2_shell_plan plan;es2_shell_state state;
    init(&f);n=make_default(&f,base,sizeof base);
    memset(&plan,0,sizeof plan);plan.group=3;plan.position=8;
    plan.player=(es2_shell_herc){48,3,player_mounts};plan.team_count=3;plan.team=team;plan.zero_variables=1;
    CHECK(es2_shell_plan_save((es2_span){base,n},&f.t,&plan,NULL,0,&m,&f.e)==ES2_OK);
    CHECK(es2_shell_plan_save((es2_span){base,n},&f.t,&plan,out,sizeof out,&m,&f.e)==ES2_OK);
    CHECK(es2_sav_parse((es2_span){out,m},&s,&f.e)==ES2_OK);
    if(failures)return;
    CHECK(u(s.selection.data)==3 && u(s.selection.data+2)==8);
    CHECK(s.loadout_count==4 && u(s.team.data)==4 && u(s.team.data+2)==4);
    CHECK(es2_sav_loadout_at(&s,0,&l,&f.e)==ES2_OK && l.index==0 && l.count==3 && u(l.fixed.data)==48 && u(l.fixed.data+2)==6);
    CHECK(u(l.fixed.data+70)==100 && u(l.fixed.data+72)==0 && u(l.fixed.data+74)==9);
    /* Slots ascending; mount rule secondary 1 for 13..15, 5 otherwise. */
    CHECK(u(l.items.data)==1 && u(l.items.data+2)==7 && u(l.items.data+4)==23 && u(l.items.data+10)==1234);
    CHECK(u(l.items.data+12)==4 && u(l.items.data+14)==25 && u(l.items.data+16)==5 && u(l.items.data+22)==5);
    CHECK(u(l.items.data+24)==8 && u(l.items.data+26)==15 && u(l.items.data+34)==1 && u(l.items.data+30)==100);
    CHECK(es2_sav_loadout_at(&s,1,&l,&f.e)==ES2_OK && l.index==1 && u(l.fixed.data+74)==4 && u(l.items.data)==0 &&
          u(l.items.data+12)==3 && u(l.items.data+10)==5 && u(l.items.data+22)==1);
    CHECK(es2_sav_loadout_at(&s,2,&l,&f.e)==ES2_OK && l.index==2 && l.count==0);
    for(i=0;i<36;i++) {
        int bay=i==11?1:i==24?2:i==16?3:-1;
        CHECK(es2_sav_pilot_at(&s,i,&p,&f.e)==ES2_OK);
        CHECK((int16_t)u(p.suffix.data)==bay && p.suffix.data[2]==(bay>0) && (int16_t)u(p.suffix.data+5)==bay);
    }
    CHECK(u(s.roster_selection.data)==11 && u(s.roster_selection.data+2)==4 && u(s.roster_selection.data+4)==0);
    CHECK(u(s.roster_selection.data+6)==1);
    CHECK(u(s.player.suffix.data)==0 && s.player.suffix.data[2]==1 && u(s.player.suffix.data+3)==4);
    CHECK(s.inventory[7].flag==201 && s.inventory[25].flag==1 && s.inventory[13].flag==1 && s.inventory[5].flag==1 && s.inventory[3].flag==0);
    CHECK(s.inventory[7].count==2);
    /* The projection restored from it is what the preparation consumes. */
    memset(&state,0,sizeof state);
    CHECK(es2_shell_restore_save((es2_span){out,m},&state,&f.e)==ES2_OK);
    CHECK(state.equipment.count==4 && state.equipment.player.bay==0 && state.equipment.bays[0].vehicle==48);
    CHECK(state.equipment.bays[0].items[4]==25 && state.equipment.bays[0].secondary[8]==1 && state.equipment.bays[0].items[0]==0);
    CHECK(state.equipment.pilots[16].order==3 && state.equipment.selected[1]==4);
    /* Explicit flags, kept variables, and a base with an opaque tail. */
    for(i=0;i<33;i++)flags[i]=(uint8_t)(i==1);
    plan.flags=flags;plan.zero_variables=0;plan.team_count=0;
    memcpy(out,base,n);out[n]=1;out[n+1]=2;
    CHECK(es2_shell_plan_save((es2_span){out,n+2},&f.t,&plan,guard,sizeof guard,&m,&f.e)==ES2_OK);
    CHECK(es2_sav_parse((es2_span){guard,m},&s,&f.e)==ES2_OK && s.trailing.size==0 && s.inventory[25].flag==0 && s.inventory[1].flag==1);
    CHECK(u(s.team.data)==1 && s.loadout_count==1);
    /* Invalid plans fail before output. */
    {
        es2_shell_mount bad_mounts[11];es2_shell_member bad_team[4];
        for(i=0;i<11;i++)bad_mounts[i]=(es2_shell_mount){(int16_t)i,1,0,0};
        for(i=0;i<12;i++) {
            es2_shell_plan q=plan;es2_status expected=ES2_ERR_OFFSET;
            memcpy(bad_team,team,sizeof team);q.team=bad_team;q.team_count=3;q.flags=NULL;
            switch(i) {
            case 0: q.player.vehicle=99;expected=ES2_ERR_NOT_FOUND;break;
            case 1: q.player=(es2_shell_herc){48,10,bad_mounts};expected=ES2_ERR_COUNT;break;
            case 2: bad_mounts[0].slot=9;q.player=(es2_shell_herc){43,1,bad_mounts};break;
            case 3: bad_mounts[0].slot=1;q.player=(es2_shell_herc){48,2,bad_mounts};break;
            case 4: bad_mounts[0].slot=0;bad_mounts[0].item=0;q.player=(es2_shell_herc){48,1,bad_mounts};break;
            case 5: bad_mounts[0].item=33;q.player=(es2_shell_herc){48,1,bad_mounts};break;
            case 6: bad_team[1].group=0;break;
            case 7: bad_team[1].group=3;break;
            case 8: bad_team[1].index=12;break;
            case 9: q.team_count=4;expected=ES2_ERR_COUNT;break;
            case 10: q.team=NULL;expected=ES2_ERR_COUNT;break;
            case 11: bad_team[2].herc.vehicle=41;expected=ES2_ERR_NOT_FOUND;break;
            }
            for(m=0;m<11;m++)if(i!=2 && i!=3 && i!=4 && i!=5)bad_mounts[m]=(es2_shell_mount){(int16_t)m,1,0,0};
            memset(out,0x2c,sizeof out);memcpy(guard,out,sizeof out);keep=4242;
            {es2_status got=es2_shell_plan_save((es2_span){base,n},&f.t,&q,out,sizeof out,&keep,&f.e);
             CHECK(got==expected);if(got!=expected)fprintf(stderr,"plan case %zu: %d\n",i,(int)got);}
            CHECK(keep==4242 && !memcmp(out,guard,sizeof out));
            bad_mounts[0]=(es2_shell_mount){0,1,0,0};
        }
        CHECK(es2_shell_plan_save((es2_span){base,n},&f.t,NULL,out,sizeof out,&keep,&f.e)==ES2_ERR_ARGUMENT);
        CHECK(es2_shell_plan_save((es2_span){base,n-1},&f.t,&plan,out,sizeof out,&keep,&f.e)!=ES2_OK);
        CHECK(es2_shell_mount_secondary(12)==5 && es2_shell_mount_secondary(13)==1 && es2_shell_mount_secondary(15)==1 &&
              es2_shell_mount_secondary(16)==5);
    }
}
/* New career: an independent model of the additive generator counts
 * the draws (roster 3 x (4 + 3 + 12), player 1, salvage 1). */
static uint16_t model_word(es2_shell_rng *r) {
    uint16_t x=(uint16_t)(r->words[r->a]+r->words[r->b]);
    r->words[r->b]=x;r->a=(uint8_t)((r->a+1)%56);r->b=(uint8_t)((r->b+1)%56);return x;
}
static void new_game(void) {
    fixture f;uint8_t base[8192],out[8192];size_t n,m=0,i,keep;es2_shell_rng start,r,model;es2_sav s;uint32_t salvage=0;
    uint16_t x;es2_status status;
    init(&f);n=make_default(&f,base,sizeof base);
    /* Seed zero of the (all-zero) synthetic table: everything but the tally
     * and the final word equals the default SAV. */
    CHECK(es2_shell_rng_init((es2_span){f.tables,112},0,&start,&f.e)==ES2_OK);
    r=start;
    status=es2_shell_new_game(&f.vfs,"GER",&f.t,"Synthetic",4,2,5,&r,NULL,0,&m,&salvage,&f.e);
    CHECK(status==ES2_OK && m==n && !memcmp(&r,&start,sizeof r) && salvage==0);
    CHECK(es2_shell_new_game(&f.vfs,"GER",&f.t,"Synthetic",4,2,5,&r,out,sizeof out,&m,&salvage,&f.e)==ES2_OK && m==n);
    CHECK(es2_sav_parse((es2_span){out,m},&s,&f.e)==ES2_OK);
    if(failures)return;
    CHECK(salvage==100000 && u(s.tally.data)==(100000&0xffff) && u(s.tally.data+2)==(100000>>16) && u(s.final_fields.data)==2);
    {
        size_t t0=(size_t)(s.tally.data-out),f0=(size_t)(s.final_fields.data-out),other=0;
        for(i=0;i<n;i++)if((i<t0 || i>=t0+4) && (i<f0 || i>=f0+2) && out[i]!=base[i])other++;
        CHECK(other==0);
    }
    /* A nonzero stream: 59 draws, the last one sets the salvage. */
    for(i=0;i<56;i++)start.words[i]=(uint16_t)(i*7919u+13u);
    start.a=3;start.b=34;r=model=start;
    CHECK(es2_shell_new_game(&f.vfs,"GER",&f.t,"Pilot",1,1,0,&r,out,sizeof out,&m,&salvage,&f.e)==ES2_OK);
    for(i=0;i<58;i++)(void)model_word(&model);
    x=(uint16_t)((model_word(&model)&0x7fffu)%11u);
    CHECK(!memcmp(&r,&model,sizeof r) && salvage==100000u+1000u*x);
    CHECK(es2_sav_parse((es2_span){out,m},&s,&f.e)==ES2_OK && u(s.final_fields.data)==2 &&
          (u(s.tally.data)|(uint32_t)u(s.tally.data+2)<<16)==salvage && u(s.selection.data)==1 && u(s.player.suffix.data+3)==1);
    /* The roster depends on the stream: the seed-zero roster differs here. */
    CHECK(memcmp(out,base,n<m?n:m)!=0);
    /* Errors: stream, output and size unchanged. */
    r=start;keep=4242;memset(out,0x5a,sizeof out);
    CHECK(es2_shell_new_game(&f.vfs,"GER",&f.t,"",1,1,0,&r,out,sizeof out,&keep,&salvage,&f.e)==ES2_ERR_LENGTH);
    CHECK(es2_shell_new_game(&f.vfs,"GER",&f.t,"x",1,1,0,&r,out,10,&keep,&salvage,&f.e)==ES2_ERR_CAPACITY && keep>10);
    CHECK(!memcmp(&r,&start,sizeof r) && out[0]==0x5a && out[sizeof out-1]==0x5a);
    r.a=56;CHECK(es2_shell_new_game(&f.vfs,"GER",&f.t,"x",1,1,0,&r,out,sizeof out,&keep,&salvage,&f.e)==ES2_ERR_ARGUMENT);
    CHECK(es2_shell_new_game(&f.vfs,"GER",&f.t,"x",1,1,0,NULL,out,sizeof out,&keep,&salvage,&f.e)==ES2_ERR_ARGUMENT);
}
/* es2_shell_prepared_save: bank, briefing selection, position, team
 * words and the selected pilots' active bytes from the preparation; every
 * other byte of the base writer image kept, the stale tail dropped. */
static void prepared(void) {
    fixture f;uint8_t base[8192],out[8192],bank[2000];size_t n,m=0,keep,i,changed=0;es2_shell_state after;
    es2_shell_output prepared;es2_sav s,b;es2_shell_pilot *p;
    init(&f);n=make_default(&f,base,sizeof base);
    memset(base+n,0xee,7);                      /* stale tail */
    memset(&after,0,sizeof after);
    CHECK(es2_shell_restore_save((es2_span){base,n+7},&after,&f.e)==ES2_OK);
    after.equipment.capacity=3;after.equipment.count=2;
    p=&after.equipment.pilots[12+after.equipment.selected[1]];p->active=(uint8_t)!p->active;
    memset(&prepared,0,sizeof prepared);
    for(i=0;i<2000;i++)bank[i]=(uint8_t)(i*13+1);
    prepared.variables.data=bank;prepared.variables.size=2000;
    for(i=0;i<148;i++)prepared.briefing[i]=(uint8_t)(200-i);
    CHECK(es2_shell_prepared_save((es2_span){base,n+7},1,4,&after,&prepared,out,sizeof out,&m,&f.e)==ES2_ERR_ARGUMENT);
    prepared.has_briefing=1;
    CHECK(es2_shell_prepared_save((es2_span){base,n+7},1,4,&after,&prepared,NULL,0,&m,&f.e)==ES2_OK && m==n);
    keep=77;
    CHECK(es2_shell_prepared_save((es2_span){base,n+7},1,4,&after,&prepared,out,n-1,&keep,&f.e)==ES2_ERR_CAPACITY && keep==n);
    CHECK(es2_shell_prepared_save((es2_span){base,n+7},1,4,&after,&prepared,out,sizeof out,&m,&f.e)==ES2_OK && m==n);
    CHECK(es2_sav_parse((es2_span){out,m},&s,&f.e)==ES2_OK && es2_sav_parse((es2_span){base,n},&b,&f.e)==ES2_OK);
    if(failures)return;
    CHECK(!memcmp(s.variables.data,bank,2000) && !memcmp(s.selection.data+4,prepared.briefing,148));
    CHECK(u(s.selection.data)==1 && u(s.selection.data+2)==4 && u(s.team.data)==3 && u(s.team.data+2)==2);
    {
        es2_sav_pilot q;size_t at;
        CHECK(es2_sav_pilot_at(&s,12+after.equipment.selected[1],&q,&f.e)==ES2_OK && q.suffix.data[2]==p->active);
        at=(size_t)(q.suffix.data-out)+2;
        for(i=0;i<n;i++) {
            size_t v=(size_t)(s.variables.data-out),sel=(size_t)(s.selection.data-out),t=(size_t)(s.team.data-out);
            if((i>=v && i<v+2000) || (i>=sel && i<sel+152) || (i>=t && i<t+4) || i==at)continue;
            if(out[i]!=base[i])changed++;
        }
        CHECK(changed==0);
    }
    prepared.variables.size=1999;
    CHECK(es2_shell_prepared_save((es2_span){base,n},1,4,&after,&prepared,out,sizeof out,&m,&f.e)==ES2_ERR_ARGUMENT);
}
int main(void) {
    defaults();plans();new_game();prepared();
    printf("save synthetic: %u checks, %u failures\n",checks,failures);return failures!=0;
}
