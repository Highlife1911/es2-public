/* Private text transport for es2prep.py. No output paths or binary stdout:
 * hex lines avoid the Windows CRT text-mode newline/EOF transformations. */
#include "es2/shell/prepare.h"
#include "es2/shell/save.h"
#include "es2/shell/campaign.h"
#include "es2/shell/slots.h"
#include "es2/shell/hangar.h"
#include "es2/formats/msn.h"
#include "es2/formats/sav.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned get16(const uint8_t *p) {return p[0]+256u*p[1];}
static int16_t signed16(const uint8_t *p) {unsigned x=get16(p);return (int16_t)(x<32768? (int)x:(int)x-65536);}
static void put16(uint8_t *p,unsigned x) {p[0]=(uint8_t)x;p[1]=(uint8_t)(x>>8);}
static int file(const char *path,es2_span *out) {
    FILE *f=fopen(path,"rb");long z;uint8_t *p;
    if(!f)return 0;
    if(fseek(f,0,SEEK_END) || (z=ftell(f))<0 || fseek(f,0,SEEK_SET)) {fclose(f);return 0;}
    p=(uint8_t *)malloc((size_t)z+1);if(!p){fclose(f);return 0;}
    if(fread(p,1,(size_t)z,f)!=(size_t)z){free(p);fclose(f);return 0;}
    if(fclose(f)){free(p);return 0;}*out=(es2_span){p,(size_t)z};return 1;
}
static int nibble(int x) {if(x>='0'&&x<='9')return x-'0';if(x>='a'&&x<='f')return x-'a'+10;return -1;}
static int hex_read(es2_span *out) {
    size_t n=0,capacity=256;uint8_t *p=(uint8_t *)malloc(capacity);int a,b;
    if(!p)return 0;
    while((a=getchar())!='\n') {
        if(a=='\r')continue;
        b=getchar();if(nibble(a)<0 || nibble(b)<0 || n>=16777216){free(p);return 0;}
        if(n==capacity) {uint8_t *q;capacity*=2;q=(uint8_t *)realloc(p,capacity);if(!q){free(p);return 0;}p=q;}
        p[n++]=(uint8_t)(16*nibble(a)+nibble(b));
    }
    *out=(es2_span){p,n};return 1;
}
static void hex_write(const uint8_t *p,size_t n) {
    size_t i;for(i=0;i<n;i++)printf("%02x",p[i]);putchar('\n');
}

static void hex_field(const uint8_t *p,size_t n) {
    size_t i;for(i=0;i<n;i++)printf("%02x",p[i]);
}
/* Localized BIN catalogue entries as hex, for name resolution in the front
 * end. Original strings are printed on request, never stored by this tool. */
static int print_bin(const es2_vfs *v,const char *language,const char *name,size_t first,size_t last) {
    char path[64];es2_vfs_copy c;es2_error e;size_t n,base,i;
    snprintf(path,sizeof path,"%s/%s.BIN",language,name);
    if(es2_vfs_find(v,path,&c,&e)!=ES2_OK || c.payload.size<8)return 0;
    n=get16(c.payload.data)|(size_t)get16(c.payload.data+2)<<16;
    if(n>(c.payload.size-8)/2)return 0;
    base=8+2*n;
    for(i=first;i<=last && i<n;i++) {
        size_t off=get16(c.payload.data+8+2*i);const uint8_t *end;
        if(off>=c.payload.size-base || !(end=memchr(c.payload.data+base+off,0,c.payload.size-base-off)))return 0;
        printf("NAME %s %s %zu ",language,name,i);
        hex_field(c.payload.data+base+off,(size_t)(end-(c.payload.data+base+off)));putchar('\n');
    }
    return 1;
}
/* CD catalogue for the loadout front end: vehicles, weapons, inventory,
 * hangar defaults, hardpoint lists (ARM files named on stdin) and names. */
static int catalog(const es2_vfs *v) {
    char line[1024],*name;es2_vfs_copy c;es2_error e;es2_dat d;size_t i,j,k=0;
    const char *languages[]={"ENG","FRE","GER"};
    if(es2_vfs_find(v,"GAM/HERC_INF.DAT",&c,&e)!=ES2_OK || es2_dat_parse(ES2_DAT_HERC_INF,c.payload,0,&d,&e)!=ES2_OK)return 2;
    for(i=0;i<d.count;i++) {
        es2_dat_herc_info h;if(es2_dat_herc_info_at(&d,i,&h,&e)!=ES2_OK)return 2;
        printf("HERC_INF %zu %d %d %d %d %d %u %d %u\n",i,h.herc_id,h.word_0x02,h.word_0x04,h.word_0x06,
               h.word_0x08,h.word_0x0a,h.build_progress_divisor,h.word_0x0e);
    }
    if(es2_vfs_find(v,"GAM/HERCS.DAT",&c,&e)!=ES2_OK || es2_dat_parse(ES2_DAT_HERCS,c.payload,0,&d,&e)!=ES2_OK)return 2;
    for(i=0;i<d.count;i++) {
        es2_dat_loadout r;if(es2_dat_loadout_at(&d,i,&r,&e)!=ES2_OK)return 2;
        printf("HERCS %d %d %u %u %zu",r.bay_slot,r.herc_id,r.word_0x02,r.word_0x04,r.slot_count);
        for(j=0;j<r.slot_count;j++) {
            es2_dat_loadout_slot s;if(es2_dat_loadout_slot_at(&r,j,&s,&e)!=ES2_OK)return 2;
            printf(" %d:%d:%u:%u",s.slot_id,s.item_id,s.word_0x04,s.word_0x06);
        }
        putchar('\n');
    }
    if(es2_vfs_find(v,"GAM/WEAPONS.DAT",&c,&e)!=ES2_OK || es2_dat_parse(ES2_DAT_SHELL_WEAPONS,c.payload,0,&d,&e)!=ES2_OK)return 2;
    for(i=0;i<d.count;i++) {
        es2_dat_shell_weapon w;if(es2_dat_shell_weapon_at(&d,i,&w,&e)!=ES2_OK)return 2;
        printf("WEAPON %d %u %u %u ",w.weapon_id,w.word_after_name,w.byte_after_word,w.final_word);
        hex_field(w.name.data,w.name.size);putchar('\n');
    }
    for(i=0;i<d.secondary_count;i++) {
        es2_dat_item it;if(es2_dat_shell_item_at(&d,i,&it,&e)!=ES2_OK)return 2;
        printf("ITEM %d %u %u\n",it.item_id,it.word_0x02,it.word_0x04);
    }
    if(!fgets(line,sizeof line,stdin))return 2;
    for(name=strtok(line," \r\n");name;name=strtok(NULL," \r\n"),k++) {
        /* ARM_<vehicle>: two images, then groups of (item, K, K records whose
         * first word is the hardpoint slot plus two). */
        es2_reader r;int16_t groups,item,count,slot;
        if(es2_vfs_find(v,name,&c,&e)!=ES2_OK || es2_reader_init(&r,c.payload,&e)!=ES2_OK ||
           es2_skip(&r,44)!=ES2_OK || es2_read_i16(&r,&groups)!=ES2_OK || groups<0)return 2;
        for(i=0;i<(size_t)groups;i++) {
            if(es2_read_i16(&r,&item)!=ES2_OK || es2_read_i16(&r,&count)!=ES2_OK || count<0)return 2;
            printf("ARM %zu %d",k,item);
            for(j=0;j<(size_t)count;j++) {
                if(es2_read_i16(&r,&slot)!=ES2_OK || es2_skip(&r,20)!=ES2_OK)return 2;
                printf(" %d",slot);
            }
            putchar('\n');
        }
        if(es2_reader_end(&r)!=ES2_OK)return 2;
    }
    for(i=0;i<3;i++)
        if(!print_bin(v,languages[i],"WEAPONS",0,32) || !print_bin(v,languages[i],"ESNAMES",0,35) ||
           !print_bin(v,languages[i],"ESTEXT",110,118))return 2;
    return 0;
}
static void status_line(es2_status status,const es2_error *e) {
    printf("ERROR %s %zu\n",es2_status_string(status),e->offset);
}
/* Private SAV build request: control line, then hex lines (defaults tables,
 * item list, base SAV or empty for CD defaults, player name, flags or empty,
 * plan). Plan: player then members, each u8 group, u8 index, i16 vehicle,
 * u8 count and count x (i16 slot, i16 item, u8 has_secondary, u16 value).
 * Requests repeat until end of input; -1 reports that end. */
static int save_build_one(const es2_vfs *v) {
    char line[256],language[4],player_name[32];int with_plan,group,position,zero_vars,rank,team,explicit_flags;
    es2_span tables={0},items={0},base={0},name={0},flags={0},plan_bytes={0};es2_error e={0};es2_status status=ES2_OK;
    es2_shell_save_tables t;es2_shell_plan plan;es2_shell_mount mounts[4][10];es2_shell_member members[3];
    uint8_t *made=NULL,*out=NULL;size_t size=0,i,j;int result=0;
    if(!fgets(line,sizeof line,stdin))return -1;
    if(sscanf(line,"%3s %d %d %d %d %d %d %d",language,&with_plan,&group,&position,
           &zero_vars,&rank,&team,&explicit_flags)!=8 ||
       !hex_read(&tables) || !hex_read(&items) || !hex_read(&base) || !hex_read(&name) || !hex_read(&flags) ||
       !hex_read(&plan_bytes) || tables.size!=164 || items.size!=60 || (explicit_flags && flags.size!=33) ||
       team<0 || team>3 || name.size>29 || group<-32768 || group>32767 || position<-32768 || position>32767 ||
       rank<0 || rank>65535) {
        fprintf(stderr,"invalid private save request\n");result=2;goto done;
    }
    t.defaults=tables;t.item_indices=items;
    if(!base.size) {
        if(name.size)memcpy(player_name,name.data,name.size);
        player_name[name.size]=0;
        status=es2_shell_default_save(v,language,&t,player_name,(uint16_t)rank,(int16_t)group,(int16_t)position,NULL,0,&size,&e);
        if(status==ES2_OK && !(made=(uint8_t *)malloc(size)))status=ES2_ERR_CAPACITY;
        if(status==ES2_OK)status=es2_shell_default_save(v,language,&t,player_name,(uint16_t)rank,(int16_t)group,(int16_t)position,made,size,&size,&e);
        free((void *)base.data);base=(es2_span){made,size};
    }
    if(status==ES2_OK && with_plan) {
        es2_reader r;memset(&plan,0,sizeof plan);
        es2_reader_init(&r,plan_bytes,&e);
        for(i=0;i<=(size_t)team && status==ES2_OK;i++) {
            uint8_t g=0,index=0,count=0;int16_t vehicle=0;es2_shell_herc *h=i?&members[i-1].herc:&plan.player;
            if(es2_read_u8(&r,&g)!=ES2_OK || es2_read_u8(&r,&index)!=ES2_OK || es2_read_i16(&r,&vehicle)!=ES2_OK ||
               es2_read_u8(&r,&count)!=ES2_OK || count>10){status=es2_fail(&e,ES2_ERR_TRUNCATED,i);break;}
            for(j=0;j<count;j++) {
                uint8_t has=0;uint16_t value=0;es2_shell_mount *m=&mounts[i][j];
                if(es2_read_i16(&r,&m->slot)!=ES2_OK || es2_read_i16(&r,&m->item)!=ES2_OK ||
                   es2_read_u8(&r,&has)!=ES2_OK || es2_read_u16(&r,&value)!=ES2_OK){status=es2_fail(&e,ES2_ERR_TRUNCATED,i);break;}
                m->has_secondary=has!=0;m->secondary=value;
            }
            h->vehicle=vehicle;h->count=count;h->mounts=mounts[i];
            if(i){members[i-1].group=g;members[i-1].index=index;}
        }
        if(status==ES2_OK && es2_reader_end(&r)!=ES2_OK)status=es2_fail(&e,ES2_ERR_LENGTH,0);
        plan.group=(int16_t)group;plan.position=(int16_t)position;plan.team_count=(size_t)team;plan.team=members;
        plan.flags=explicit_flags?flags.data:NULL;plan.zero_variables=zero_vars;
        if(status==ES2_OK)status=es2_shell_plan_save(base,&t,&plan,NULL,0,&size,&e);
        if(status==ES2_OK && !(out=(uint8_t *)malloc(size)))status=ES2_ERR_CAPACITY;
        if(status==ES2_OK)status=es2_shell_plan_save(base,&t,&plan,out,size,&size,&e);
    }
    if(status!=ES2_OK)status_line(status,&e);
    else {puts("OK");hex_write(out?out:base.data,out?size:base.size);}
done:
    free(out);free((void *)tables.data);free((void *)items.data);free((void *)name.data);
    free((void *)flags.data);free((void *)plan_bytes.data);free((void *)base.data);
    fflush(stdout);return result;
}
static int save_build(const es2_vfs *v) {
    int result;
    while(!(result=save_build_one(v)));
    return result<0?0:result;
}
/* Private new-career request: control line "<language> <rank> <group>
 * <position>", then hex lines: default tables (164), item list (60), player
 * name, RNG state (114). Response: OK, the SAV, the RNG state after the
 * new-career draws and "SALVAGE <total>"; or ERROR. One request. */
static int new_game(const es2_vfs *v) {
    char line[128],language[4],player_name[32];int rank,group,position;
    es2_span tables={0},items={0},name={0},rng={0};es2_error e={0};es2_status status;
    es2_shell_save_tables t;es2_shell_rng state;uint8_t *out=NULL,bytes[114];size_t size=0,i;uint32_t salvage=0;int result=0;
    if(!fgets(line,sizeof line,stdin) || sscanf(line,"%3s %d %d %d",language,&rank,&group,&position)!=4 ||
       !hex_read(&tables) || !hex_read(&items) || !hex_read(&name) || !hex_read(&rng) ||
       tables.size!=164 || items.size!=60 || name.size>29 || rng.size!=114 || rank<0 || rank>65535 ||
       group<-32768 || group>32767 || position<-32768 || position>32767) {
        fprintf(stderr,"invalid private new-game request\n");result=2;goto done;
    }
    t.defaults=tables;t.item_indices=items;
    memcpy(player_name,name.data,name.size);player_name[name.size]=0;
    for(i=0;i<56;i++)state.words[i]=(uint16_t)get16(rng.data+2*i);
    state.a=rng.data[112];state.b=rng.data[113];
    status=es2_shell_new_game(v,language,&t,player_name,(uint16_t)rank,(int16_t)group,(int16_t)position,&state,NULL,0,&size,NULL,&e);
    if(status==ES2_OK && !(out=(uint8_t *)malloc(size)))status=es2_fail(&e,ES2_ERR_CAPACITY,0);
    if(status==ES2_OK)status=es2_shell_new_game(v,language,&t,player_name,(uint16_t)rank,(int16_t)group,(int16_t)position,&state,out,size,&size,&salvage,&e);
    if(status!=ES2_OK)status_line(status,&e);
    else {
        puts("OK");hex_write(out,size);
        for(i=0;i<56;i++)put16(bytes+2*i,state.words[i]);bytes[112]=state.a;bytes[113]=state.b;hex_write(bytes,114);
        printf("SALVAGE %lu\n",(unsigned long)salvage);
    }
done:
    free(out);free((void *)tables.data);free((void *)items.data);free((void *)name.data);free((void *)rng.data);
    fflush(stdout);return result;
}
/* SAV summary for reports and pilot lookup; values only, names as hex. */
static int describe_save(void) {
    es2_span bytes={0};es2_sav s;es2_error e;size_t i,j;
    if(!hex_read(&bytes)){fprintf(stderr,"invalid private SAV\n");return 2;}
    if(es2_sav_parse(bytes,&s,&e)!=ES2_OK){status_line(e.code,&e);free((void *)bytes.data);return 0;}
    puts("OK");
    printf("SIZE %zu %zu %zu\n",s.input.size,s.reader_end,s.trailing.size);
    printf("POSITION %d %d\n",signed16(s.selection.data),signed16(s.selection.data+2));
    printf("SELECTION");for(i=0;i<6;i++)printf(" %u",get16(s.roster_selection.data+2*i));putchar('\n');
    printf("TEAM %d %d\n",signed16(s.team.data),signed16(s.team.data+2));
    for(i=0;i<37;i++) {
        es2_sav_pilot p;const uint8_t *x;
        if(i<36){if(es2_sav_pilot_at(&s,i,&p,&e)!=ES2_OK)break;}else p=s.player;
        x=p.suffix.data;
        printf("%s %zu %u %u %d %u %u %d %u ",i<36?"PILOT":"PLAYER",i,get16(p.prefix.data),get16(p.prefix.data+2),
               signed16(x),x[2],get16(x+3),signed16(x+5),get16(x+7));
        hex_field(p.name.data,p.name.size-1);putchar('\n');
    }
    for(i=0;i<s.loadout_count;i++) {
        es2_sav_loadout l;const uint8_t *f;if(es2_sav_loadout_at(&s,i,&l,&e)!=ES2_OK)break;
        f=l.fixed.data;
        printf("BAY %d %d %d %u %u %u %zu",l.index,signed16(f),signed16(f+2),get16(f+74),get16(f+70),get16(f+72),l.count);
        for(j=0;j<l.count;j++) {
            const uint8_t *r=l.items.data+12*j;
            printf(" %u:%d:%d:%u:%u:%u",get16(r),signed16(r+2),signed16(r+4),get16(r+6),get16(r+8),get16(r+10));
        }
        putchar('\n');
    }
    printf("FLAGS ");for(i=0;i<33;i++)printf("%02x",s.inventory[i].flag);putchar('\n');
    /* List lengths, salvage total and the five-entry build bank. */
    printf("COUNTS");for(i=0;i<33;i++)printf(" %zu",s.inventory[i].count);putchar('\n');
    printf("TALLY %lu\n",(unsigned long)get16(s.tally.data)|(unsigned long)get16(s.tally.data+2)<<16);
    printf("BANK5 %d",signed16(s.five_entry_bank.data));
    for(i=0;i<5;i++)printf(" %d:%d",signed16(s.five_entry_bank.data+2+4*i),signed16(s.five_entry_bank.data+4+4*i));
    putchar('\n');
    printf("NINE");for(i=0;i<9;i++)printf(" %d",signed16(s.nine_word_bank.data+2*i));putchar('\n');
    free((void *)bytes.data);fflush(stdout);return 0;
}
/* Private results request: control line (language, mode, repair option,
 * restock option, set_position, group, position), then hex lines (executable
 * tables, SAV, mission.var, results.dat, 114-byte RNG state). Repeats until
 * end of input. Response: OK, a META line, the 30 debrief references, then
 * hex lines for SAV, MISSION.STR, debrief text and RNG; or ERROR. */
static int results_one(const es2_vfs *v) {
    char line[256],language[4];int mode,repair,restock,set,group,position;
    es2_span tables={0},save={0},bank={0},results={0},rng={0};es2_error e={0};es2_status status;
    es2_shell_results_input in;es2_shell_results_output out;uint8_t state[114];size_t i;int result=0;
    if(!fgets(line,sizeof line,stdin))return -1;
    memset(&in,0,sizeof in);memset(&out,0,sizeof out);
    if(sscanf(line,"%3s %d %d %d %d %d %d",language,&mode,&repair,&restock,&set,&group,&position)!=7 ||
       !hex_read(&tables) || !hex_read(&save) || !hex_read(&bank) || !hex_read(&results) || !hex_read(&rng) ||
       rng.size!=114 || mode<-32768 || mode>32767 || repair<0 || repair>255 || restock<0 || restock>255 ||
       group<-32768 || group>32767 || position<-32768 || position>32767) {
        fprintf(stderr,"invalid private results request\n");result=2;goto done;
    }
    in.save=save;in.variables=bank;in.results=results;in.tables=tables;in.language=language;
    in.mode=(int16_t)mode;in.repair_option=(uint8_t)repair;in.restock_option=(uint8_t)restock;
    in.set_position=set;in.group=(int16_t)group;in.position=(int16_t)position;
    for(i=0;i<56;i++)in.rng.words[i]=(uint16_t)get16(rng.data+2*i);
    in.rng.a=rng.data[112];in.rng.b=rng.data[113];
    status=es2_shell_results(v,&in,&out,&e);
    if(status!=ES2_OK)status_line(status,&e);
    else {
        puts("OK");
        printf("META %d %d %d %d %d %d %lu %d %zu %d %d %d\n",out.ending,out.advance,out.group,out.position,
               out.final_word,out.lost,(unsigned long)out.total,out.debrief,out.selected.passed,
               out.selected.count,out.selected.aux,out.added);
        printf("REFS");for(i=0;i<30;i++)printf(" %d",out.selected.refs[i]);putchar('\n');
        hex_write(out.save.data,out.save.size);hex_write(out.strings.data,out.strings.size);
        hex_write(out.text.data,out.text.size);
        for(i=0;i<56;i++)put16(state+2*i,out.rng.words[i]);state[112]=out.rng.a;state[113]=out.rng.b;
        hex_write(state,114);
    }
    es2_shell_results_free(&out);
done:
    free((void *)tables.data);free((void *)save.data);free((void *)bank.data);free((void *)results.data);free((void *)rng.data);
    fflush(stdout);return result;
}
static int results_batch(const es2_vfs *v) {
    int result;
    while(!(result=results_one(v)));
    return result<0?0:result;
}
/* Private prefs request: control line "<present> <mode> <repair>
 * <build>" (-1 keeps a byte), then one hex line with the file bytes.
 * Response: "OK <mode> <repair> <build>" (as the results path receives
 * them) and the 54-byte block in hex; or ERROR. Needs no CD archives. */
static int prefs_request(void) {
    char line[128];int present,mode,repair,build;es2_span file={0};es2_error e={0};es2_status status;
    uint8_t prefs[ES2_SHELL_PREFS_SIZE];es2_shell_results_input in;
    if(!fgets(line,sizeof line,stdin) || sscanf(line,"%d %d %d %d",&present,&mode,&repair,&build)!=4 ||
       !hex_read(&file) || (!present && file.size)) {
        free((void *)file.data);fprintf(stderr,"invalid private prefs request\n");return 2;
    }
    es2_shell_prefs_load(present?&file:NULL,prefs);
    status=es2_shell_prefs_choose(prefs,mode,repair,build,&e);
    if(status!=ES2_OK)status_line(status,&e);
    else {
        memset(&in,0,sizeof in);es2_shell_prefs_options(prefs,&in);
        printf("OK %d %u %u\n",in.mode,(unsigned)in.repair_option,(unsigned)in.restock_option);
        hex_write(prefs,sizeof prefs);
    }
    free((void *)file.data);fflush(stdout);return 0;
}
/* Private request, repeated until end of input: one hex SAV line;
 * response "OK" and the hex SAV VSHELL writes after loading it
 * (es2_shell_loaded_save), or ERROR. */
static int loaded_save_request(void) {
    es2_span sav={0};int result=0;
    for(;;) {
        int c=getchar();uint8_t *out=NULL;size_t size=0;es2_error e={0};es2_status status;
        if(c==EOF)break;
        ungetc(c,stdin);
        if(!hex_read(&sav)){fprintf(stderr,"invalid private loaded-save request\n");result=2;break;}
        status=es2_shell_loaded_save(sav,NULL,0,&size,&e);
        if(status==ES2_OK && !(out=(uint8_t *)malloc(size)))status=es2_fail(&e,ES2_ERR_CAPACITY,0);
        if(status==ES2_OK)status=es2_shell_loaded_save(sav,out,size,&size,&e);
        if(status!=ES2_OK)status_line(status,&e);
        else{puts("OK");hex_write(out,size);}
        free(out);free((void *)sav.data);sav=(es2_span){0};fflush(stdout);
    }
    return result;
}
static int same_name(const char *a,const char *b) {
    for(;*a && *b;a++,b++) {
        int x=(unsigned char)*a,y=(unsigned char)*b;
        if(x>='a' && x<='z')x-=32;
        if(y>='a' && y<='z')y-=32;
        if(x!=y)return 0;
    }
    return !*a && !*b;
}
/* Private slot-save request, repeated until end of input. Control
 * line "slot mode has_text count", then hex lines: GAMEFILE.STR, the empty
 * text (ESTEXT 0x21), the slot text, the SAV of the loaded game, and count
 * pairs (file name, contents) of the save directory. Response: "OK
 * <effective slot>", hex lines of the slot's file name, the new GAMEFILE.STR
 * and the new slot file (the SAV writer image over the old file); or ERROR.
 * The companion copies are plain copies (es2_shell_slot_companion). */
static int slot_save_request(void) {
    char line[128];int slot,mode,has_text,count,result=0;
    while(fgets(line,sizeof line,stdin)) {
        es2_span gamefile={0},empty={0},text={0},image={0},names[32]={{0}},contents[32]={{0}},old={0};
        char text_z[128],file_name[ES2_SHELL_SLOT_FILE];int effective=0,i,ok=1;
        uint8_t *gf=NULL,*sav=NULL;size_t gf_size=0,sav_size=0;es2_sav parsed;es2_error e={0};es2_status status;
        if(sscanf(line,"%d %d %d %d",&slot,&mode,&has_text,&count)!=4 || count<0 || count>32 ||
           !hex_read(&gamefile) || !hex_read(&empty) || !hex_read(&text) || !hex_read(&image) || text.size>=sizeof text_z) ok=0;
        for(i=0;ok && i<count;i++)if(!hex_read(&names[i]) || !hex_read(&contents[i]) || names[i].size>=64)ok=0;
        if(!ok){fprintf(stderr,"invalid private slot-save request\n");result=2;}
        else {
            memcpy(text_z,text.data,text.size);text_z[text.size]=0;
            status=es2_shell_slot_gamefile(gamefile,empty,slot,has_text?text_z:NULL,(int16_t)mode,&effective,file_name,NULL,0,&gf_size,&e);
            if(status==ES2_OK && !(gf=(uint8_t *)malloc(gf_size)))status=es2_fail(&e,ES2_ERR_CAPACITY,0);
            if(status==ES2_OK)status=es2_shell_slot_gamefile(gamefile,empty,slot,has_text?text_z:NULL,(int16_t)mode,&effective,file_name,gf,gf_size,&gf_size,&e);
            for(i=0;status==ES2_OK && i<count;i++) {
                char n[64];memcpy(n,names[i].data,names[i].size);n[names[i].size]=0;
                if(same_name(n,file_name))old=contents[i];
            }
            if(status==ES2_OK)status=es2_sav_parse(image,&parsed,&e);
            if(status==ES2_OK)status=es2_sav_overlay((es2_span){image.data,parsed.reader_end},old,NULL,0,&sav_size,&e);
            if(status==ES2_OK && !(sav=(uint8_t *)malloc(sav_size)))status=es2_fail(&e,ES2_ERR_CAPACITY,0);
            if(status==ES2_OK)status=es2_sav_overlay((es2_span){image.data,parsed.reader_end},old,sav,sav_size,&sav_size,&e);
            if(status!=ES2_OK)status_line(status,&e);
            else {
                printf("OK %d\n",effective);hex_write((const uint8_t *)file_name,strlen(file_name));
                hex_write(gf,gf_size);hex_write(sav,sav_size);
            }
        }
        free(gf);free(sav);free((void *)gamefile.data);free((void *)empty.data);free((void *)text.data);free((void *)image.data);
        for(i=0;i<32;i++){free((void *)names[i].data);free((void *)contents[i].data);}
        fflush(stdout);
        if(result)break;
    }
    return result;
}
/* Private service-bay request, repeated until end of input. Control
 * line "<count>", then hex lines: the executable tables of the results path
 * (322 bytes, or empty when no REBALANCE follows) and the SAV; then count
 * lines "kind bay slot value admitted" (es2/shell/hangar.h). Response:
 * "OK" with "APPLIED" and one 0/1 per operation, then the hex SAV; or
 * ERROR with the failing operation's index. */
static int service_bay_request(const es2_vfs *v) {
    char line[256];int count,result=0;
    while(fgets(line,sizeof line,stdin)) {
        es2_span tables={0},sav={0};es2_shell_bay_op ops[64];es2_shell_buffer out={0};es2_error e={0};
        es2_status status;int i,ok=1;
        if(sscanf(line,"%d",&count)!=1 || count<0 || count>64 || !hex_read(&tables) || !hex_read(&sav))ok=0;
        for(i=0;ok && i<count;i++) {
            int kind,bay,slot,value,admitted;
            if(!fgets(line,sizeof line,stdin) || sscanf(line,"%d %d %d %d %d",&kind,&bay,&slot,&value,&admitted)!=5 ||
               bay<-32768 || bay>32767 || slot<-32768 || slot>32767 || value<-32768 || value>32767)ok=0;
            else{memset(&ops[i],0,sizeof ops[i]);ops[i].kind=kind;ops[i].bay=(int16_t)bay;ops[i].slot=(int16_t)slot;
                 ops[i].value=(int16_t)value;ops[i].admitted=admitted;}
        }
        if(!ok){fprintf(stderr,"invalid private service-bay request\n");result=2;}
        else {
            status=es2_shell_service_bay(v,tables,sav,ops,(size_t)count,&out,&e);
            if(status!=ES2_OK)status_line(status,&e);
            else{puts("OK");printf("APPLIED");for(i=0;i<count;i++)printf(" %d",ops[i].applied);putchar('\n');hex_write(out.data,out.size);}
        }
        free(out.data);free((void *)tables.data);free((void *)sav.data);fflush(stdout);
        if(result)break;
    }
    return result;
}
/* Private launch-files request, repeated until end of input: one hex
 * SAV line; response "OK" and hex lines player.mec and mission.var as the
 * launch-file writer 0x40f68c writes them for that state
 * (es2_shell_launch_files), or ERROR. Needs no CD archives. */
static int launch_files_request(void) {
    int result=0;
    for(;;) {
        es2_span sav={0};es2_shell_buffer mec={0},bank={0};es2_error e={0};es2_status status;int c=getchar();
        if(c==EOF)break;
        ungetc(c,stdin);
        if(!hex_read(&sav)){fprintf(stderr,"invalid private launch-files request\n");result=2;break;}
        status=es2_shell_launch_files(sav,&mec,&bank,&e);
        if(status!=ES2_OK)status_line(status,&e);
        else{puts("OK");hex_write(mec.data,mec.size);hex_write(bank.data,bank.size);}
        free(mec.data);free(bank.data);free((void *)sav.data);fflush(stdout);
    }
    return result;
}
int main(int argc,char **argv) {
    const char *names[]={"ZONES.VOL","SHELL0.VOL","LANG0.VOL"};
    es2_span archives[3]={{0}},overlay[4]={{0}};es2_vol vols[3];es2_vfs_source sources[4];es2_vfs vfs;
    es2_vfs_file files[4];size_t overlays=0;
    es2_error e;size_t i;char path[4096],line[256];int result=0;
    es2_shell_equipment retained={0};
    /* --results [NAME FILE]...: synthetic test files shadowing VFS paths
     * (the same overlay the original worker takes with --overlay). */
    if(argc>3 && !strcmp(argv[2],"--results") && argc%2==1 && argc<=11) {
        for(i=3;i+1<(size_t)argc;i+=2,overlays++) {
            if(!file(argv[i+1],&overlay[overlays])){fprintf(stderr,"overlay read failed\n");return 2;}
            files[overlays].path=argv[i];files[overlays].data=overlay[overlays];
        }
        argc=3;
    }
    if(argc==3 && !strcmp(argv[2],"--prefs"))return prefs_request();
    if(argc==3 && !strcmp(argv[2],"--slot-save"))return slot_save_request();
    if(argc==3 && !strcmp(argv[2],"--loaded-save"))return loaded_save_request();
    if(argc==3 && !strcmp(argv[2],"--launch-files"))return launch_files_request();
    if(argc<2 || argc>3){fprintf(stderr,"Use shell/tools/es2prep.py --core <this executable>.\n");return 2;}
    for(i=0;i<3;i++) {
        if(snprintf(path,sizeof path,"%s/VOL/%s",argv[1],names[i])>=(int)sizeof path || !file(path,&archives[i]) || es2_vol_open(archives[i],&vols[i],&e)!=ES2_OK) {
            fprintf(stderr,"archive read failed: %s\n",names[i]);result=2;goto end;
        }
        sources[i].kind=ES2_VFS_VOL;sources[i].data.vol=&vols[i];
    }
    if(overlays) {
        memmove(sources+1,sources,3*sizeof *sources);
        sources[0].kind=ES2_VFS_LOOSE;sources[0].data.loose.files=files;sources[0].data.loose.count=overlays;
    }
    if(es2_vfs_open(sources,3+(overlays?1:0),&vfs,&e)!=ES2_OK){result=2;goto end;}
    if(argc==3 && !strcmp(argv[2],"--catalog")){result=catalog(&vfs);goto end;}
    if(argc==3 && !strcmp(argv[2],"--save-build")){result=save_build(&vfs);goto end;}
    if(argc==3 && !strcmp(argv[2],"--describe-save")){result=describe_save();goto end;}
    if(argc==3 && !strcmp(argv[2],"--new-game")){result=new_game(&vfs);goto end;}
    if(argc==3 && !strcmp(argv[2],"--results")){result=results_batch(&vfs);goto end;}
    if(argc==3 && !strcmp(argv[2],"--service-bay")){result=service_bay_request(&vfs);goto end;}
    if(argc==3 && (!strcmp(argv[2],"--list") || !strcmp(argv[2],"--comparisons"))) {
        es2_vfs_copy copy;es2_dat career;size_t g,p;
        if(es2_vfs_find(&vfs,"GAM/CAREER.DAT",&copy,&e)!=ES2_OK || es2_dat_parse(ES2_DAT_CAREER,copy.payload,0,&career,&e)!=ES2_OK){result=2;goto end;}
        for(g=0;g<career.count;g++) {
            es2_dat_career_group group;
            if(es2_dat_career_group_at(&career,g,&group,&e)!=ES2_OK){result=2;goto end;}
            for(p=0;p<group.count;p++) {
                char name[40];int16_t id;
                if(es2_shell_campaign_mission(&vfs,(int16_t)g,(int16_t)p,"GER",name,sizeof name,&id,&e)!=ES2_OK){result=2;goto end;}
                if(!strcmp(argv[2],"--list"))printf("%zu %zu %s\n",g,p,name);
                else {
                    es2_msn msn;snprintf(path,sizeof path,"MSN/%s.MSN",name);
                    if(es2_vfs_find(&vfs,path,&copy,&e)!=ES2_OK || es2_msn_parse(copy.payload,&msn,&e)!=ES2_OK){result=2;goto end;}
                    for(size_t j=0;j<msn.lists[0].count;j++) {
                        const uint8_t *r=msn.lists[0].records.data+14*j;
                        if(signed16(r+4)==0)printf("%zu %zu %s %zu %d %d %d %d\n",g,p,name,j,signed16(r+6),signed16(r+10),signed16(r+8),signed16(r+2));
                    }
                }
            }
        }
        goto end;
    }
    while(fgets(line,sizeof line,stdin)) {
        char mission[40],language[4];int g,p,mode,difficulty,pv,a,b,c,d,build,tv=0,pf=0,pi=0,retain=0,bank_override=1,fields,prepared_save=0;
        es2_span raw={0},mec_bytes={0},tables={0};es2_shell_options o={0};es2_shell_state state={0};es2_shell_output out={0};
        es2_shell_loadout load={0};es2_shell_actor *actors=NULL;int16_t *items=NULL;uint16_t *secondary=NULL;
        uint8_t rng[114],selectors[60];es2_status status;
        /* An optional 18th field 1 adds one line after the six: the
         * SAV after the preparation (es2_shell_prepared_save), or "-". */
        fields=sscanf(line,"%39s %3s %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",mission,language,&g,&p,&mode,&difficulty,&pv,&a,&b,&c,&d,&build,&tv,&pf,&pi,&retain,&bank_override,&prepared_save);
        if((fields!=17 && fields!=18) || prepared_save<0 || prepared_save>1 ||
            !hex_read(&raw) || !hex_read(&mec_bytes) || !hex_read(&tables) || raw.size!=2174 || (tables.size && tables.size!=740)) {
            fprintf(stderr,"invalid private request\n");free((void *)raw.data);free((void *)mec_bytes.data);result=2;goto end;
        }
        status=ES2_OK;
        if(retain)state.equipment=retained;
        else if(build==2)status=es2_shell_restore_save(mec_bytes,&state,&e);
        if(bank_override || build!=2)for(i=0;i<1000;i++)state.variables[i]=signed16(raw.data+2*i);
        for(i=0;i<56;i++)state.rng.words[i]=(uint16_t)get16(raw.data+2000+2*i);
        state.rng.a=raw.data[2112];state.rng.b=raw.data[2113];
        for(i=0;i<30;i++)state.reset_selectors[i]=signed16(raw.data+2114+2*i);
        o.mission=strcmp(mission,"-")?mission:NULL;o.language=language;o.group=(int16_t)g;o.position=(int16_t)p;
        o.mode=(int16_t)mode;o.difficulty=(int16_t)difficulty;o.player_value=(int16_t)pv;
        o.mode0_values[0]=(uint8_t)a;o.mode0_values[1]=(uint8_t)b;o.mode0_values[2]=(uint8_t)c;o.mode0_values[3]=(uint8_t)d;
        o.default_tables=(es2_span){tables.data,tables.size?164:0};
        o.training_vehicle=(uint8_t)tv;o.training_preset_flag=(uint8_t)pf;o.training_preset_index=(int16_t)pi;
        if(tables.size && tv>=0 && tv<9 && memchr(tables.data+164+64*tv,0,64))o.training_preset_path=(const char *)tables.data+164+64*tv;
        if(build!=2 && mec_bytes.size)o.player_template=mec_bytes;
        if(build==1 && mec_bytes.size) {
            es2_mec mec;status=es2_mec_parse(mec_bytes,&mec,&e);
            if(status==ES2_OK) {
                actors=(es2_shell_actor *)calloc(mec.count+1,sizeof *actors);
                items=(int16_t *)calloc(mec.count*10+1,sizeof *items);secondary=(uint16_t *)calloc(mec.count*10+1,sizeof *secondary);
                if(!actors || !items || !secondary)status=es2_fail(&e,ES2_ERR_CAPACITY,0);
                for(i=0;i<mec.count && status==ES2_OK;i++) {
                    es2_mec_record rec;size_t j;es2_shell_actor *actor=&actors[i];
                    status=es2_mec_record_at(&mec,i,&rec,&e);if(status!=ES2_OK)break;
                    if(rec.count>10){status=es2_fail(&e,ES2_ERR_COUNT,i);break;}
                    actor->pilot[0]=(uint16_t)get16(rec.pilot.data);actor->pilot[1]=(uint16_t)get16(rec.pilot.data+2);
                    actor->vehicle=rec.vehicle_id;actor->capacity=rec.count;actor->items=items+10*i;actor->secondary=secondary+10*i;
                    for(j=0;j<rec.count;j++) {items[10*i+j]=signed16(rec.items.data+2*j);secondary[10*i+j]=(uint16_t)get16(rec.secondary.data+2*j);}
                    for(j=0;j<33;j++)actor->condition[j]=(uint16_t)get16(rec.tail.data+2+2*j);
                }
                load.actors=actors;load.count=mec.count;memcpy(load.flags,mec.flags.data,33);
                o.player_template=(es2_span){0};o.loadout=&load;
            }
        }
        if(status==ES2_OK)status=es2_shell_prepare(&vfs,&o,&state,&out,&e);
        if(status!=ES2_OK)printf("ERROR %s %zu\n",es2_status_string(status),e.offset);
        else {
            retained=state.equipment;
            puts("OK");hex_write(out.script.data,out.script.size);hex_write(out.strings.data,out.strings.size);
            hex_write(out.variables.data,out.variables.size);hex_write(out.player.data,out.player.size);
            for(i=0;i<56;i++)put16(rng+2*i,state.rng.words[i]);rng[112]=state.rng.a;rng[113]=state.rng.b;hex_write(rng,114);
            for(i=0;i<30;i++)put16(selectors+2*i,(uint16_t)state.reset_selectors[i]);hex_write(selectors,60);
            if(prepared_save) {
                size_t size=0;uint8_t *sav=NULL;
                if(build==2 && out.has_briefing && es2_shell_prepared_save(mec_bytes,(int16_t)g,(int16_t)p,&state,&out,NULL,0,&size,&e)==ES2_OK &&
                   (sav=(uint8_t *)malloc(size)) && es2_shell_prepared_save(mec_bytes,(int16_t)g,(int16_t)p,&state,&out,sav,size,&size,&e)==ES2_OK)
                    hex_write(sav,size);
                else puts("-");
                free(sav);
            }
        }
        fflush(stdout);
        es2_shell_output_free(&out);free(actors);free(items);free(secondary);free((void *)raw.data);free((void *)mec_bytes.data);free((void *)tables.data);
    }
end:
    for(i=0;i<3;i++)free((void *)archives[i].data);
    for(i=0;i<overlays;i++)free((void *)overlay[i].data);
    return result;
}
