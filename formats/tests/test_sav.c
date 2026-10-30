#include "es2/formats/sav.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t checks,failures;
#define CHECK(x) do {checks++;if(!(x)){failures++;fprintf(stderr,"line %d: %s\n",__LINE__,#x);}}while(0)
static void roundtrip(es2_span bytes) {
    es2_sav s;es2_error e;size_t required=0;uint8_t *out;
    es2_status status=es2_sav_parse(bytes,&s,&e);
    CHECK(status==ES2_OK);if(status!=ES2_OK)return;
    CHECK(es2_sav_write(&s,NULL,NULL,0,&required,&e)==ES2_OK && required==bytes.size);
    out=(uint8_t *)malloc(required);CHECK(out!=NULL);if(!out)return;
    CHECK(es2_sav_write(&s,NULL,out,required,&required,&e)==ES2_OK);
    CHECK(!memcmp(out,bytes.data,bytes.size));free(out);
}
/* Rebuild a SAV through every replacement path while keeping its values:
 * a zero edit, an edit that re-supplies each parsed record, and a complete
 * image. All three must reproduce the input byte for byte. */
static void identity_rewrites(es2_span bytes) {
    es2_sav s;es2_error e;es2_sav_pilot roster[36];es2_sav_loadout bays[8];
    es2_sav_edit zero,full;es2_sav_image image;uint8_t flags[33];uint16_t selection[6];
    int16_t team[2],nine[9],variables[1000];size_t i,n=0;uint8_t *out;
    if(es2_sav_parse(bytes,&s,&e)!=ES2_OK || s.loadout_count>8){CHECK(0);return;}
    for(i=0;i<36;i++)CHECK(es2_sav_pilot_at(&s,i,&roster[i],&e)==ES2_OK);
    for(i=0;i<s.loadout_count;i++)CHECK(es2_sav_loadout_at(&s,i,&bays[i],&e)==ES2_OK);
    for(i=0;i<33;i++)flags[i]=s.inventory[i].flag;
    for(i=0;i<6;i++)selection[i]=(uint16_t)(s.roster_selection.data[2*i]|s.roster_selection.data[2*i+1]<<8);
    for(i=0;i<2;i++)team[i]=(int16_t)(s.team.data[2*i]|s.team.data[2*i+1]<<8);
    for(i=0;i<9;i++)nine[i]=(int16_t)(s.nine_word_bank.data[2*i]|s.nine_word_bank.data[2*i+1]<<8);
    for(i=0;i<1000;i++)variables[i]=(int16_t)(s.variables.data[2*i]|s.variables.data[2*i+1]<<8);
    memset(&zero,0,sizeof zero);memset(&full,0,sizeof full);
    full.variables=variables;full.set_position=1;
    full.group=(int16_t)(s.selection.data[0]|s.selection.data[1]<<8);
    full.position=(int16_t)(s.selection.data[2]|s.selection.data[3]<<8);
    full.inventory_flags=flags;full.roster=roster;full.roster_selection=selection;full.team=team;
    full.player=&s.player;full.set_loadouts=1;full.loadout_count=s.loadout_count;full.loadouts=bays;
    full.nine_word_bank=nine;
    memset(&image,0,sizeof image);
    for(i=0;i<33;i++)image.inventory[i]=s.inventory[i];
    image.five_entry_bank=s.five_entry_bank;image.selection=s.selection;image.roster=roster;
    image.roster_selection=s.roster_selection;image.team=s.team;image.player=&s.player;
    image.loadout_count=s.loadout_count;image.loadouts=bays;image.nine_word_bank=s.nine_word_bank;
    image.tally=s.tally;image.variables=s.variables;image.final_fields=s.final_fields;image.trailing=s.trailing;
    out=(uint8_t *)malloc(bytes.size+1);CHECK(out!=NULL);if(!out)return;
    CHECK(es2_sav_write(&s,&zero,out,bytes.size,&n,&e)==ES2_OK && n==bytes.size && !memcmp(out,bytes.data,n));
    memset(out,0,bytes.size);
    CHECK(es2_sav_write(&s,&full,out,bytes.size,&n,&e)==ES2_OK && n==bytes.size && !memcmp(out,bytes.data,n));
    memset(out,0,bytes.size);
    CHECK(es2_sav_compose(&image,out,bytes.size,&n,&e)==ES2_OK && n==bytes.size && !memcmp(out,bytes.data,n));
    free(out);
}
/* The original rewrites a SAV without truncating it: saving the parsed state
 * over its own file reproduces the file (image plus stale tail), saving it
 * where no file exists gives the bare writer image input[0..reader_end). */
static void overwrite_identity(es2_span bytes) {
    es2_sav s;es2_error e;size_t n=0;uint8_t *out;
    if(es2_sav_parse(bytes,&s,&e)!=ES2_OK){CHECK(0);return;}
    out=(uint8_t *)malloc(bytes.size+1);CHECK(out!=NULL);if(!out)return;
    CHECK(es2_sav_write_over(&s,NULL,bytes,NULL,0,&n,&e)==ES2_OK && n==bytes.size);
    CHECK(es2_sav_write_over(&s,NULL,bytes,out,bytes.size,&n,&e)==ES2_OK && n==bytes.size &&
        !memcmp(out,bytes.data,n));
    CHECK(es2_sav_write_over(&s,NULL,(es2_span){NULL,0},out,bytes.size,&n,&e)==ES2_OK &&
        n==s.reader_end && !memcmp(out,bytes.data,n));
    CHECK(es2_sav_overlay((es2_span){bytes.data,s.reader_end},bytes,out,bytes.size,&n,&e)==ES2_OK && n==bytes.size && !memcmp(out,bytes.data,n));
    free(out);
}
#ifdef ES2_SAV_REAL_TEST
static uint8_t *load(const char *dir,const char *name,size_t *size) {
    char path[4096];FILE *file;long length;uint8_t *data;
    if(snprintf(path,sizeof path,"%s/%s",dir,name)>=(int)sizeof path)return NULL;
    file=fopen(path,"rb");if(!file){fprintf(stderr,"cannot open %s\n",path);return NULL;}
    if(fseek(file,0,SEEK_END) || (length=ftell(file))<0 || fseek(file,0,SEEK_SET)){fclose(file);return NULL;}
    data=(uint8_t *)malloc((size_t)length+1);
    if(data && fread(data,1,(size_t)length,file)!=(size_t)length){free(data);data=NULL;}
    if(fclose(file)){free(data);data=NULL;}
    *size=(size_t)length;return data;
}
/* Real saves are read from a snapshot copy under es2-data/logs/snapshots
 * (never from an installation): es2-data is ES2_DATA_REPO, else two levels
 * above ES2_DATA_ROOT (the older layout); ES2_SAV_DIR overrides
 * the directory. */
int main(void) {
    const char *root=getenv("ES2_DATA_ROOT"),*repo=getenv("ES2_DATA_REPO"),*dir=getenv("ES2_SAV_DIR");
    const char *names[]={"GAME_0.SAV","GAME_1.SAV","GAME_R.SAV","GAME_T.SAV"};
    const size_t bank_offsets[]={2731,2651,2731,2300},ends[]={4753,4673,4753,4322};
    char base[4096];size_t i,sizes[4]={0};uint8_t *files[4]={NULL};
    if(!root || !*root){puts("SKIP real SAV: set ES2_DATA_ROOT");return 0;}
    if(!dir || !*dir) {
        if(repo && *repo) snprintf(base,sizeof base,"%s/logs/snapshots/save-load-crash-20261005T054444359Z/SAV",repo);
        else snprintf(base,sizeof base,"%s/../../logs/snapshots/save-load-crash-20261005T054444359Z/SAV",root);
        dir=base;
    }
    for(i=0;i<4;i++) {
        es2_sav save;es2_error e;
        files[i]=load(dir,names[i],&sizes[i]);CHECK(files[i]!=NULL);if(!files[i])continue;
        if(es2_sav_parse((es2_span){files[i],sizes[i]},&save,&e)==ES2_OK) {
            CHECK(save.variable_offset==bank_offsets[i]);CHECK(save.reader_end==ends[i]);
            CHECK(save.trailing.size==(i==2?10u:0u));
            roundtrip((es2_span){files[i],sizes[i]});
            /* Real saves lie in the documented loader-safe domain. */
            identity_rewrites((es2_span){files[i],sizes[i]});
            overwrite_identity((es2_span){files[i],sizes[i]});
            printf("SAV %s: size=%zu reader_end=%zu trailing=%zu\n",names[i],sizes[i],save.reader_end,save.trailing.size);
        } else CHECK(0);
    }
    /* How a stale tail arises with real states: the shorter GAME_T state saved
     * over the GAME_0 file leaves GAME_0's last 431 bytes behind, the file
     * still loads (the readers stop at the reader end) and saving the same
     * state over the result reproduces it. */
    if(files[0] && files[3]) {
        es2_sav t,u;es2_error e;size_t n=0;uint8_t *out=(uint8_t *)malloc(sizes[0]);
        CHECK(out!=NULL && es2_sav_parse((es2_span){files[3],sizes[3]},&t,&e)==ES2_OK);
        if(out && es2_sav_write_over(&t,NULL,(es2_span){files[0],sizes[0]},out,sizes[0],&n,&e)==ES2_OK) {
            CHECK(n==sizes[0] && !memcmp(out,files[3],sizes[3]) && !memcmp(out+sizes[3],files[0]+sizes[3],sizes[0]-sizes[3]));
            CHECK(es2_sav_parse((es2_span){out,n},&u,&e)==ES2_OK && u.reader_end==sizes[3] && u.trailing.size==sizes[0]-sizes[3]);
            overwrite_identity((es2_span){out,n});
        } else CHECK(0);
        free(out);
    }
    for(i=0;i<4;i++)free(files[i]);
    printf("SAV real: %zu checks, %zu failures\n",checks,failures);return failures!=0;
}
#else
static void w(uint8_t *p,unsigned x){p[0]=(uint8_t)x;p[1]=(uint8_t)(x>>8);}
static unsigned u(const uint8_t *p){return p[0]+256u*p[1];}
static void fake_pilot(uint8_t *b,size_t *p,unsigned value) {
    w(b+*p,value);w(b+*p+2,value+1);w(b+*p+4,2);b[*p+6]='X';b[*p+7]=0;
    memset(b+*p+8,(int)(value&255),25);*p+=33;
}
/* Synthetic SAV inside the loader-safe domain: inventory IDs match their
 * category, two bays with capacity words and unique in-range slots. */
static size_t valid_save(uint8_t *b) {
    size_t p=0,i,j;
    for(i=0;i<33;i++) {
        size_t n=i%3==1?2:0;b[p++]=(uint8_t)(i&1);w(b+p,(unsigned)n);p+=2;
        for(j=0;j<n;j++){w(b+p,(unsigned)i);w(b+p+2,(unsigned)i);w(b+p+4,100);w(b+p+6,100);w(b+p+8,5);p+=10;}
    }
    for(i=0;i<22;i++)b[p++]=(uint8_t)i;
    for(i=0;i<152;i++)b[p++]=(uint8_t)(i*7);
    for(i=0;i<36;i++) {
        w(b+p,(unsigned)i);w(b+p+2,(unsigned)(i*3));w(b+p+4,(unsigned)(2+i%5));p+=6;
        for(j=0;j<1+i%5;j++)b[p++]=(uint8_t)('A'+j);
        b[p++]=0;for(j=0;j<25;j++)b[p++]=(uint8_t)(i+j);
    }
    for(i=0;i<6;i++){w(b+p,(unsigned)i);p+=2;}
    w(b+p,2);w(b+p+2,1);p+=4;
    w(b+p,0);w(b+p+2,7);w(b+p+4,4);memcpy(b+p+6,"Ego",4);p+=10;memset(b+p,9,25);p+=25;
    w(b+p,2);p+=2;
    for(i=0;i<2;i++) {
        w(b+p,(unsigned)(i*5));p+=2;
        for(j=0;j<38;j++)w(b+p+2*j,100);
        w(b+p,(unsigned)i);w(b+p+74,i?10u:3u);p+=76;
        w(b+p,i?3u:0u);p+=2;
        if(i)for(j=0;j<3;j++){w(b+p,(unsigned)(9-j));w(b+p+2,(unsigned)(25+j));w(b+p+4,0);w(b+p+6,100);w(b+p+8,100);w(b+p+10,5);p+=12;}
    }
    for(i=0;i<9;i++){w(b+p,(unsigned)(i&1));p+=2;}
    w(b+p,1000);w(b+p+2,0);p+=4;
    for(i=0;i<1000;i++){w(b+p,(unsigned)(i*3));p+=2;}
    for(i=0;i<22;i++)b[p++]=(uint8_t)(200+i);
    return p;
}
static void edits(void) {
    uint8_t bytes[6000],out[6000],guard[6000],fixed[2][76],items[12*10],suffix[25],name[31];
    size_t total=valid_save(bytes),n=12345,i,changed;es2_sav s,t;es2_error e;es2_sav_edit edit;
    es2_sav_pilot roster[36],player,bad;es2_sav_loadout bays[9];es2_sav_image image;
    uint8_t flags[33];uint16_t selection[6]={11,10,9,1,2,3};int16_t team[2]={4,3},nine[9]={1,1,1,1,1,1,1,1,1};
    CHECK(es2_sav_parse((es2_span){bytes,total},&s,&e)==ES2_OK && s.trailing.size==0);
    roundtrip((es2_span){bytes,total});identity_rewrites((es2_span){bytes,total});
    for(i=0;i<total;i++) {t=s;CHECK(es2_sav_parse((es2_span){bytes,i},&t,&e)!=ES2_OK);}
    for(i=0;i<36;i++)CHECK(es2_sav_pilot_at(&s,i,&roster[i],&e)==ES2_OK);
    /* Replace every editable section at once and verify each field. */
    for(i=0;i<33;i++)flags[i]=(uint8_t)(i%2==0);
    memset(suffix,0xee,sizeof suffix);memcpy(name,"Replacement Pilot",18);
    roster[4].suffix=(es2_span){suffix,25};roster[4].name=(es2_span){name,18};
    player=s.player;player.suffix=(es2_span){suffix,25};
    for(i=0;i<2;i++){memset(fixed[i],0,76);w(fixed[i],(unsigned)(6+i));w(fixed[i]+74,i?7u:10u);}
    for(i=0;i<10;i++){w(items+12*i,(unsigned)i);w(items+12*i+2,(unsigned)(i+20));w(items+12*i+4,(unsigned)(i+20));
        w(items+12*i+6,100);w(items+12*i+8,100);w(items+12*i+10,i==3?1u:5u);}
    bays[0]=(es2_sav_loadout){7,{fixed[0],76},{items,120},10,0};
    bays[1]=(es2_sav_loadout){2,{fixed[1],76},{items,0},0,0};
    memset(&edit,0,sizeof edit);
    edit.inventory_flags=flags;edit.roster=roster;edit.roster_selection=selection;edit.team=team;
    edit.player=&player;edit.set_loadouts=1;edit.loadout_count=2;edit.loadouts=bays;edit.nine_word_bank=nine;
    edit.set_position=1;edit.group=3;edit.position=9;
    CHECK(es2_sav_write(&s,&edit,NULL,0,&n,&e)==ES2_OK);
    changed=n;CHECK(changed==total+7*12+12);
    memset(out,0xa5,sizeof out);
    CHECK(es2_sav_write(&s,&edit,out,sizeof out,&n,&e)==ES2_OK && n==changed && out[n]==0xa5);
    CHECK(es2_sav_parse((es2_span){out,n},&t,&e)==ES2_OK);
    if(!failures) {
        es2_sav_pilot q;es2_sav_loadout l;
        for(i=0;i<33;i++)CHECK(t.inventory[i].flag==flags[i] && t.inventory[i].count==s.inventory[i].count &&
            !memcmp(t.inventory[i].items.data,s.inventory[i].items.data,s.inventory[i].items.size));
        CHECK(!memcmp(t.five_entry_bank.data,s.five_entry_bank.data,22));
        CHECK(u(t.selection.data)==3 && u(t.selection.data+2)==9 && !memcmp(t.selection.data+4,s.selection.data+4,148));
        CHECK(es2_sav_pilot_at(&t,4,&q,&e)==ES2_OK && q.name.size==18 && !memcmp(q.name.data,name,18) && !memcmp(q.suffix.data,suffix,25));
        CHECK(es2_sav_pilot_at(&t,5,&q,&e)==ES2_OK && !memcmp(q.suffix.data,roster[5].suffix.data,25));
        for(i=0;i<6;i++)CHECK(u(t.roster_selection.data+2*i)==selection[i]);
        CHECK(u(t.team.data)==4 && u(t.team.data+2)==3);
        CHECK(!memcmp(t.player.suffix.data,suffix,25) && !memcmp(t.player.name.data,"Ego",4));
        CHECK(t.loadout_count==2 && es2_sav_loadout_at(&t,0,&l,&e)==ES2_OK && l.index==7 && l.count==10 &&
              !memcmp(l.fixed.data,fixed[0],76) && !memcmp(l.items.data,items,120));
        CHECK(es2_sav_loadout_at(&t,1,&l,&e)==ES2_OK && l.index==2 && l.count==0 && u(l.fixed.data+74)==7);
        for(i=0;i<9;i++)CHECK(u(t.nine_word_bank.data+2*i)==1);
        CHECK(!memcmp(t.tally.data,s.tally.data,4) && !memcmp(t.variables.data,s.variables.data,2000) &&
              !memcmp(t.final_fields.data,s.final_fields.data,22));
        identity_rewrites((es2_span){out,n});
    }
    /* Each invalid replacement fails before any output byte and before required. */
    {
        const struct { int kind; size_t value; es2_status expected; } cases[]={
            {0,31,ES2_ERR_LENGTH},{0,0,ES2_ERR_LENGTH},{1,0,ES2_ERR_STRING},{2,0,ES2_ERR_STRING},
            {3,3,ES2_ERR_LENGTH},{4,24,ES2_ERR_LENGTH},{5,9,ES2_ERR_COUNT},{6,0,ES2_ERR_OFFSET},
            {7,8,ES2_ERR_OFFSET},{8,11,ES2_ERR_COUNT},{9,0,ES2_ERR_COUNT},{10,0,ES2_ERR_OFFSET},
            {11,0,ES2_ERR_OFFSET},{12,33,ES2_ERR_OFFSET},{13,0,ES2_ERR_LENGTH},{14,75,ES2_ERR_LENGTH},
            {15,0,ES2_ERR_COUNT}};
        for(i=0;i<sizeof cases/sizeof cases[0];i++) {
            es2_sav_edit d;es2_sav_loadout list[9];size_t k;uint8_t lname[32],lfixed[76],litems[12*3];size_t keep=777;
            memset(&d,0,sizeof d);memcpy(list,bays,2*sizeof bays[0]);
            memset(lname,'n',sizeof lname);bad=s.player;
            memcpy(lfixed,fixed[1],76);memcpy(litems,items,36);
            switch(cases[i].kind) {
            case 0: lname[cases[i].value?cases[i].value-1:0]=0;bad.name=(es2_span){lname,cases[i].value};d.player=&bad;break;
            case 1: bad.name=(es2_span){lname,5};d.player=&bad;break;
            case 2: lname[1]=0;lname[4]=0;bad.name=(es2_span){lname,5};d.player=&bad;break;
            case 3: bad.prefix=(es2_span){lname,cases[i].value};d.player=&bad;break;
            case 4: d.roster=roster;roster[35].suffix=(es2_span){suffix,cases[i].value};break;
            case 5: for(k=0;k<9;k++){list[k]=bays[1];list[k].index=(int16_t)(k%8);}d.set_loadouts=1;d.loadout_count=9;d.loadouts=list;break;
            case 6: list[1].index=7;d.set_loadouts=1;d.loadout_count=2;d.loadouts=list;break;
            case 7: list[1].index=8;d.set_loadouts=1;d.loadout_count=2;d.loadouts=list;break;
            case 8: w(lfixed+74,11);list[1].fixed=(es2_span){lfixed,76};d.set_loadouts=1;d.loadout_count=2;d.loadouts=list;break;
            case 9: w(lfixed+74,2);list[1].fixed=(es2_span){lfixed,76};list[1].count=3;list[1].items=(es2_span){litems,36};
                    d.set_loadouts=1;d.loadout_count=2;d.loadouts=list;break;
            case 10: w(litems+12,0);list[1].count=3;list[1].items=(es2_span){litems,36};d.set_loadouts=1;d.loadout_count=2;d.loadouts=list;break;
            case 11: w(litems+24,7);list[1].count=3;list[1].items=(es2_span){litems,36};d.set_loadouts=1;d.loadout_count=2;d.loadouts=list;break;
            case 12: w(litems+2,(unsigned)cases[i].value);list[1].count=3;list[1].items=(es2_span){litems,36};d.set_loadouts=1;d.loadout_count=2;d.loadouts=list;break;
            case 13: list[1].count=2;list[1].items=(es2_span){litems,36};d.set_loadouts=1;d.loadout_count=2;d.loadouts=list;break;
            case 14: list[1].fixed=(es2_span){lfixed,cases[i].value};d.set_loadouts=1;d.loadout_count=2;d.loadouts=list;break;
            case 15: d.set_loadouts=1;d.loadout_count=1;d.loadouts=NULL;break;
            }
            memset(out,0x5a,sizeof out);memcpy(guard,out,sizeof out);
            {es2_status got=es2_sav_write(&s,&d,out,sizeof out,&keep,&e);
             CHECK(got==cases[i].expected);
             if(got!=cases[i].expected)fprintf(stderr,"case %zu got %d\n",i,(int)got);}
            CHECK(keep==777 && !memcmp(out,guard,sizeof out));
            if(cases[i].kind==4)CHECK(es2_sav_pilot_at(&s,35,&roster[35],&e)==ES2_OK);
        }
    }
    /* Capacity: required is set, output untouched; overlapping edits refused. */
    memset(out,0x33,sizeof out);memcpy(guard,out,sizeof out);
    CHECK(es2_sav_write(&s,&edit,out,changed-1,&n,&e)==ES2_ERR_CAPACITY && n==changed && !memcmp(out,guard,sizeof out));
    edit.inventory_flags=out;
    CHECK(es2_sav_write(&s,&edit,out,sizeof out,&n,&e)==ES2_ERR_OVERLAP);
    edit.inventory_flags=flags;edit.team=(const int16_t *)(const void *)out;
    CHECK(es2_sav_write(&s,&edit,out+2,sizeof out-2,&n,&e)==ES2_ERR_OVERLAP);
    edit.team=team;roster[0].name=(es2_span){out+10,2};out[11]=0;
    CHECK(es2_sav_write(&s,&edit,out,sizeof out,&n,&e)==ES2_ERR_OVERLAP);
    CHECK(es2_sav_pilot_at(&s,0,&roster[0],&e)==ES2_OK);
    CHECK(es2_sav_write(&s,&edit,(uint8_t *)&n,sizeof n,&n,&e)==ES2_ERR_OVERLAP);
    /* Compose: complete image equals the edit result; malformed images fail. */
    memset(&image,0,sizeof image);
    CHECK(es2_sav_parse((es2_span){bytes,total},&s,&e)==ES2_OK);
    for(i=0;i<33;i++){image.inventory[i]=s.inventory[i];image.inventory[i].flag=flags[i];}
    image.five_entry_bank=s.five_entry_bank;image.selection=s.selection;image.roster=roster;
    image.roster_selection=s.roster_selection;image.team=s.team;image.player=&player;
    image.loadout_count=2;image.loadouts=bays;image.nine_word_bank=s.nine_word_bank;image.tally=s.tally;
    image.variables=s.variables;image.final_fields=s.final_fields;
    CHECK(es2_sav_compose(&image,NULL,0,&n,&e)==ES2_OK);
    memset(out,0,sizeof out);
    CHECK(es2_sav_compose(&image,out,sizeof out,&n,&e)==ES2_OK && es2_sav_parse((es2_span){out,n},&t,&e)==ES2_OK);
    CHECK(t.trailing.size==0 && t.loadout_count==2 && t.inventory[2].flag==1 && t.inventory[3].flag==0);
    {
        uint8_t bogus[20];size_t keep=99;es2_sav_image m;
        memset(bogus,0,sizeof bogus);w(bogus,4);
        m=image;m.inventory[3].count=2;m.inventory[3].items=(es2_span){bogus,20};
        memset(out,0x77,sizeof out);memcpy(guard,out,sizeof out);
        CHECK(es2_sav_compose(&m,out,sizeof out,&keep,&e)==ES2_ERR_OFFSET && keep==99 && !memcmp(out,guard,sizeof out));
        m=image;m.inventory[3].count=40000;
        CHECK(es2_sav_compose(&m,out,sizeof out,&keep,&e)==ES2_ERR_COUNT && keep==99);
        m=image;m.inventory[3].count=1;
        CHECK(es2_sav_compose(&m,out,sizeof out,&keep,&e)==ES2_ERR_LENGTH && keep==99);
        m=image;m.selection.size=151;
        CHECK(es2_sav_compose(&m,out,sizeof out,&keep,&e)==ES2_ERR_LENGTH && keep==99);
        m=image;m.variables.data=NULL;
        CHECK(es2_sav_compose(&m,out,sizeof out,&keep,&e)==ES2_ERR_LENGTH && keep==99);
        m=image;m.roster=NULL;
        CHECK(es2_sav_compose(&m,out,sizeof out,&keep,&e)==ES2_ERR_ARGUMENT && keep==99);
        m=image;m.player=NULL;
        CHECK(es2_sav_compose(&m,out,sizeof out,&keep,&e)==ES2_ERR_ARGUMENT && keep==99);
        m=image;m.loadout_count=9;
        CHECK(es2_sav_compose(&m,out,sizeof out,&keep,&e)==ES2_ERR_COUNT && keep==99);
        CHECK(!memcmp(out,guard,sizeof out));
        CHECK(es2_sav_compose(&image,out,n-1,&keep,&e)==ES2_ERR_CAPACITY && keep==n && !memcmp(out,guard,sizeof out));
        CHECK(es2_sav_compose(&image,(uint8_t *)(uintptr_t)s.tally.data,4,&keep,&e)==ES2_ERR_OVERLAP);
        CHECK(es2_sav_compose(NULL,out,sizeof out,&keep,&e)==ES2_ERR_ARGUMENT);
        CHECK(es2_sav_compose(&image,NULL,5,&keep,&e)==ES2_ERR_ARGUMENT);
        m=image;m.trailing=(es2_span){bogus,3};
        CHECK(es2_sav_compose(&m,out,sizeof out,&keep,&e)==ES2_OK && keep==n+3);
        CHECK(es2_sav_parse((es2_span){out,keep},&t,&e)==ES2_OK && t.trailing.size==3);
    }
}
/* Non-truncating rewrite model (es2_sav_write_over / es2_sav_overlay). */
static void overwrites(void) {
    uint8_t bytes[6000],old[6000],out[6000],guard[6000],small[6000];
    size_t total=valid_save(bytes),n=0,m=0,i,keep;es2_sav s,t;es2_error e;es2_sav_edit shrink;
    es2_span none={NULL,0};
    CHECK(es2_sav_parse((es2_span){bytes,total},&s,&e)==ES2_OK);
    overwrite_identity((es2_span){bytes,total});
    /* An empty hangar list shortens the image by both bay records (2+78 and
     * 2+78+3*12 bytes). Over nothing: the bare image, no tail. */
    memset(&shrink,0,sizeof shrink);shrink.set_loadouts=1;
    CHECK(es2_sav_write_over(&s,&shrink,none,small,sizeof small,&m,&e)==ES2_OK && m==total-196);
    CHECK(es2_sav_parse((es2_span){small,m},&t,&e)==ES2_OK && t.loadout_count==0 && t.trailing.size==0);
    /* Over the old file: image, then the old bytes from the image length on. */
    CHECK(es2_sav_write_over(&s,&shrink,s.input,out,sizeof out,&n,&e)==ES2_OK && n==total);
    CHECK(!memcmp(out,small,m) && !memcmp(out+m,bytes+m,total-m));
    CHECK(es2_sav_parse((es2_span){out,n},&t,&e)==ES2_OK && t.reader_end==m && t.trailing.size==total-m);
    overwrite_identity((es2_span){out,n});
    /* es2_sav_write keeps the parsed trailing (here none) instead. */
    CHECK(es2_sav_write(&s,&shrink,NULL,0,&keep,&e)==ES2_OK && keep==m);
    /* A longer image over a shorter file leaves no tail; over a longer one the
     * excess survives even when the old bytes are not a SAV. */
    CHECK(es2_sav_write_over(&s,NULL,(es2_span){small,m},out,sizeof out,&n,&e)==ES2_OK && n==total && !memcmp(out,bytes,total));
    memset(old,0xc3,sizeof old);
    CHECK(es2_sav_write_over(&s,NULL,(es2_span){old,total+10},out,sizeof out,&n,&e)==ES2_OK && n==total+10);
    CHECK(!memcmp(out,bytes,total) && out[total]==0xc3 && out[total+9]==0xc3);
    CHECK(es2_sav_parse((es2_span){out,n},&t,&e)==ES2_OK && t.reader_end==total && t.trailing.size==10);
    /* A parsed tail: write keeps it after a resized image, write_over models
     * the file instead (old bytes from the new image length on). */
    memcpy(old,bytes,total);for(i=0;i<10;i++)old[total+i]=(uint8_t)(0xe0+i);
    CHECK(es2_sav_parse((es2_span){old,total+10},&s,&e)==ES2_OK && s.trailing.size==10);
    CHECK(es2_sav_write(&s,&shrink,out,sizeof out,&n,&e)==ES2_OK && n==m+10 && !memcmp(out,small,m) && !memcmp(out+m,old+total,10));
    CHECK(es2_sav_write_over(&s,&shrink,s.input,out,sizeof out,&n,&e)==ES2_OK && n==total+10 &&
        !memcmp(out,small,m) && !memcmp(out+m,old+m,total+10-m));
    CHECK(es2_sav_write_over(&s,&shrink,none,out,sizeof out,&n,&e)==ES2_OK && n==m && !memcmp(out,small,m));
    /* Contract: sizing, capacity, arguments, overlap with previous. */
    memset(out,0x5a,sizeof out);memcpy(guard,out,sizeof out);keep=1;
    CHECK(es2_sav_write_over(&s,&shrink,s.input,out,total+9,&keep,&e)==ES2_ERR_CAPACITY && keep==total+10 && !memcmp(out,guard,sizeof out));
    keep=1;
    CHECK(es2_sav_write_over(&s,NULL,(es2_span){NULL,3},out,sizeof out,&keep,&e)==ES2_ERR_ARGUMENT && keep==1);
    CHECK(es2_sav_write_over(&s,NULL,(es2_span){out,20},out+10,sizeof out-10,&keep,&e)==ES2_ERR_OVERLAP && keep==1);
    CHECK(es2_sav_write_over(&s,NULL,(es2_span){(const uint8_t *)&keep,sizeof keep},out,sizeof out,&keep,&e)==ES2_ERR_OVERLAP);
    CHECK(es2_sav_write_over(NULL,NULL,none,out,sizeof out,&keep,&e)==ES2_ERR_ARGUMENT);
    /* Overlay of a prebuilt image. */
    CHECK(es2_sav_overlay((es2_span){small,m},(es2_span){old,total+10},NULL,0,&n,&e)==ES2_OK && n==total+10);
    CHECK(es2_sav_overlay((es2_span){small,m},(es2_span){old,total+10},out,sizeof out,&n,&e)==ES2_OK &&
        !memcmp(out,small,m) && !memcmp(out+m,old+m,total+10-m));
    CHECK(es2_sav_overlay((es2_span){old,total+10},(es2_span){small,m},out,sizeof out,&n,&e)==ES2_OK && n==total+10 && !memcmp(out,old,n));
    CHECK(es2_sav_overlay(none,none,out,sizeof out,&n,&e)==ES2_OK && n==0);
    memset(out,0x5a,sizeof out);keep=1;
    CHECK(es2_sav_overlay((es2_span){small,m},(es2_span){old,total+10},out,m,&keep,&e)==ES2_ERR_CAPACITY && keep==total+10 && !memcmp(out,guard,sizeof out));
    keep=1;
    CHECK(es2_sav_overlay((es2_span){NULL,1},none,out,sizeof out,&keep,&e)==ES2_ERR_ARGUMENT && keep==1);
    CHECK(es2_sav_overlay(none,(es2_span){NULL,1},out,sizeof out,&keep,&e)==ES2_ERR_ARGUMENT && keep==1);
    CHECK(es2_sav_overlay((es2_span){small,m},none,NULL,4,&keep,&e)==ES2_ERR_ARGUMENT && keep==1);
    CHECK(es2_sav_overlay((es2_span){out,4},none,out+2,8,&keep,&e)==ES2_ERR_OVERLAP && keep==1);
    CHECK(es2_sav_overlay(none,(es2_span){out,4},out+3,8,&keep,&e)==ES2_ERR_OVERLAP && keep==1);
    CHECK(es2_sav_overlay((es2_span){(const uint8_t *)&keep,sizeof keep},none,out,sizeof out,&keep,&e)==ES2_ERR_OVERLAP);
}
int main(void) {
    uint8_t bytes[5000],out[5000],guard[5000],mutated[5000];
    size_t p=0,i,roster_at,loadouts_at,variables_at,end,total,n;int16_t variables[1000];
    es2_sav s,before;es2_sav_pilot pilot;es2_sav_loadout loadout;es2_error e;es2_sav_edit edit={0};
    memset(bytes,0,sizeof bytes);
    for(i=0;i<33;i++) {bytes[p++]=(uint8_t)i;w(bytes+p,i==5?1:0);p+=2;if(i==5){memset(bytes+p,19,10);p+=10;}}
    memset(bytes+p,31,22+152);p+=22+152;roster_at=p;
    for(i=0;i<36;i++)fake_pilot(bytes,&p,(unsigned)i);
    memset(bytes+p,41,12+4);p+=16;fake_pilot(bytes,&p,200);
    loadouts_at=p;w(bytes+p,2);p+=2;
    for(i=0;i<2;i++) {
        w(bytes+p,(unsigned)(7-i));p+=2;memset(bytes+p,51,76);p+=76;w(bytes+p,(unsigned)i);p+=2;
        if(i){memset(bytes+p,61,12);p+=12;}
    }
    memset(bytes+p,71,22);p+=22;variables_at=p;
    for(i=0;i<1000;i++){variables[i]=(int16_t)((int)i-500);w(bytes+p,(unsigned)i);p+=2;}
    memset(bytes+p,81,22);p+=22;end=p;memset(bytes+p,91,3);p+=3;total=p;
    CHECK(es2_sav_parse((es2_span){bytes,total},&s,&e)==ES2_OK);
    CHECK(s.reader_end==end && s.trailing.size==3 && s.variable_offset==variables_at && s.loadout_count==2);
    CHECK(s.inventory[5].count==1 && s.inventory[5].items.size==10 && s.inventory[32].flag==32);
    CHECK(es2_sav_pilot_at(&s,35,&pilot,&e)==ES2_OK && pilot.offset==roster_at+35*33 && u(pilot.prefix.data)==35);
    CHECK(es2_sav_pilot_at(&s,36,&pilot,&e)==ES2_ERR_OFFSET);
    CHECK(es2_sav_loadout_at(&s,1,&loadout,&e)==ES2_OK && loadout.index==6 && loadout.count==1 && loadout.items.size==12);
    CHECK(es2_sav_loadout_at(&s,2,&loadout,&e)==ES2_ERR_OFFSET);
    roundtrip((es2_span){bytes,total});
    for(i=0;i<end;i++) {
        before=s;CHECK(es2_sav_parse((es2_span){bytes,i},&before,&e)!=ES2_OK);
        CHECK(before.input.data==s.input.data && before.reader_end==s.reader_end);
    }
    for(i=end;i<=total;i++)roundtrip((es2_span){bytes,i});
    memset(out,0xa5,sizeof out);memcpy(guard,out,sizeof out);
    CHECK(es2_sav_write(&s,NULL,out,total-1,&n,&e)==ES2_ERR_CAPACITY && n==total);
    CHECK(!memcmp(out,guard,sizeof out));
    CHECK(es2_sav_write(&s,NULL,bytes,total,&n,&e)==ES2_ERR_OVERLAP);
    CHECK(es2_sav_write(&s,NULL,NULL,1,&n,&e)==ES2_ERR_ARGUMENT);
    edit.variables=variables;edit.set_position=1;edit.group=2;edit.position=9;
    CHECK(es2_sav_write(&s,&edit,out,sizeof out,&n,&e)==ES2_OK && n==total);
    CHECK(es2_sav_parse((es2_span){out,n},&before,&e)==ES2_OK);
    CHECK(u(before.selection.data)==2 && u(before.selection.data+2)==9);
    CHECK(u(before.variables.data)==65036 && u(before.variables.data+1998)==499);
    memcpy(mutated,bytes,total);w(mutated+(s.selection.data-bytes),2);w(mutated+(s.selection.data-bytes)+2,9);
    for(i=0;i<1000;i++)w(mutated+variables_at+2*i,(unsigned)variables[i]);
    CHECK(!memcmp(mutated,out,total));
    /* Out-of-domain input still round-trips unedited; replacing its hangar
     * requires valid records only for the replacement itself. */
    {
        es2_sav_edit empty={0};CHECK(es2_sav_write(&s,&empty,out,sizeof out,&n,&e)==ES2_OK && n==total && !memcmp(out,bytes,total));
        empty.set_loadouts=1;CHECK(es2_sav_write(&s,&empty,out,sizeof out,&n,&e)==ES2_OK && n==total-2*80-12);
        CHECK(es2_sav_parse((es2_span){out,n},&before,&e)==ES2_OK && before.loadout_count==0 && before.trailing.size==3);
    }
    for(i=0;i<4;i++) {
        const size_t corrupt[]={1,roster_at+4,loadouts_at,loadouts_at+2+2+76};
        memcpy(mutated,bytes,total);w(mutated+corrupt[i],65535);
        CHECK(es2_sav_parse((es2_span){mutated,total},&before,&e)==ES2_ERR_COUNT);
    }
    memcpy(mutated,bytes,total);mutated[roster_at+7]=1;
    CHECK(es2_sav_parse((es2_span){mutated,total},&before,&e)==ES2_ERR_STRING);
    edits();overwrites();
    printf("SAV synthetic: %zu checks, %zu failures\n",checks,failures);return failures!=0;
}
#endif
