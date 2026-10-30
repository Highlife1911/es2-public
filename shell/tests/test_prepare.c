#include "es2/shell/prepare.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks,failures;
#define CHECK(x) do {checks++;if(!(x)){failures++;fprintf(stderr,"line %d: %s\n",__LINE__,#x);}}while(0)
static void w(uint8_t *p,unsigned x){p[0]=(uint8_t)x;p[1]=(uint8_t)(x>>8);}
static unsigned u(const uint8_t *p){return p[0]+256u*p[1];}
static const size_t strides[17]={14,82,8,144,64,22,10,0,12,82,30,144,102,62,22,164,58};
typedef struct fixture {
    uint8_t lists[17][4096],msn[70000],language[4096];size_t sizes[17],counts[17],strings;
    es2_shell_state state;es2_shell_options options;es2_shell_actor actor;es2_shell_loadout loadout;
} fixture;
static void init(fixture *f) {
    size_t i;memset(f,0,sizeof *f);f->options.mission="SYNTH";f->options.mode=2;
    f->options.loadout=&f->loadout;f->loadout.actors=&f->actor;f->loadout.count=1;
    for(i=0;i<56;i++)f->state.rng.words[i]=(uint16_t)i;
    f->state.rng.a=24;f->state.rng.b=55;f->strings=2;
}
static uint8_t *add(fixture *f,size_t family,int id,int dependency) {
    size_t n=strides[family-1];uint8_t *r=f->lists[family-1]+f->sizes[family-1];
    if(!n)n=10;
    memset(r,255,n);w(r,(unsigned)id);if(n>=4)w(r+2,(unsigned)dependency);
    f->counts[family-1]++;f->sizes[family-1]+=n;return r;
}
static void text(fixture *f,int id,int dep,const char *s) {
    size_t n=strlen(s)+1;uint8_t *p=f->language+f->strings;
    w(p,(unsigned)id);w(p+2,(unsigned)dep);w(p+4,555);w(p+6,(unsigned)n);memcpy(p+8,s,n);
    f->strings+=8+n;w(f->language,u(f->language)+1);
}
static es2_status run(fixture *f,es2_shell_output *out) {
    uint8_t career[]={1,0,7,0,1,0,0,0};es2_vfs_file files[3];es2_vfs_source source;es2_vfs vfs;es2_error e;
    size_t p=2,i;w(f->msn,5);
    for(i=0;i<17;i++){w(f->msn+p,(unsigned)f->counts[i]);p+=2;memcpy(f->msn+p,f->lists[i],f->sizes[i]);p+=f->sizes[i];}
    files[0]=(es2_vfs_file){"MSN/SYNTH.MSN",{f->msn,p}};
    files[1]=(es2_vfs_file){"MSN/SYNTH.GER",{f->language,f->strings}};
    files[2]=(es2_vfs_file){"GAM/CAREER.DAT",{career,sizeof career}};
    source.kind=ES2_VFS_LOOSE;source.data.loose.files=files;source.data.loose.count=3;
    CHECK(es2_vfs_open(&source,1,&vfs,&e)==ES2_OK);
    {es2_status status=es2_shell_prepare(&vfs,&f->options,&f->state,out,&e);
    if(status!=ES2_OK)fprintf(stderr,"expected/observed status %s offset %zu\n",es2_status_string(status),e.offset);
    return status;}
}
/* Independent output framing, intentionally uses the consumer's fixed strides. */
static const uint8_t *section(const es2_shell_output *o,size_t family,size_t *count) {
    static const size_t widths[]={12,2,0,6,74,24,134,92,52,14,156,54,2};
    size_t p=20,f,i;
    for(f=6;f<=18;f++) {
        size_t n=u(o->script.data+p);p+=2;
        if(f==family){*count=n;return o->script.data+p;}
        if(f==8)for(i=0;i<n;i++)p+=2+2*u(o->script.data+p);
        else p+=widths[f-6]*n;
        if(p>o->script.size){CHECK(0);return NULL;}
    }
    CHECK(0);return NULL;
}
static void conditions_environment(void) {
    fixture f;es2_shell_output o={0};uint8_t *p;size_t i;
    init(&f);f.state.variables[50]=-2;f.state.variables[77]=99;f.options.difficulty=9;
    /* Every comparison, signed operands, dependency order and duplicate IDs. */
    for(i=0;i<6;i++) {p=add(&f,1,(int)i,-1);w(p+4,0);w(p+6,50);w(p+8,281u+(unsigned)i);w(p+10,(unsigned)(i==0 || i==3 || i==5?-2:(i==1 || i==2?-1:-3)));text(&f,(int)i,(int)i,"a");}
    p=add(&f,1,10,99);w(p+4,1);w(p+6,0); /* filtered before zero-bound draw */
    p=add(&f,1,0,-1);w(p+4,3);w(p+6,12);w(p+10,1);
    p=add(&f,1,11,0);w(p+4,2);w(p+6,55);w(p+8,55);text(&f,11,11,"b");
    p=add(&f,1,12,-1);w(p+4,1);w(p+6,1);
    p=add(&f,1,13,12);w(p+4,2);w(p+6,0);w(p+8,0);text(&f,13,13,"c");
    p=add(&f,1,14,-1);w(p+4,0);w(p+6,50);w(p+8,999);text(&f,14,14,"no");
    p=add(&f,1,15,-1);w(p+4,99);text(&f,15,15,"no");
    text(&f,100,10,"no");text(&f,0,-1,"replacement");text(&f,-1,-1,"template");text(&f,-1,-1,"second");
    p=add(&f,2,-1,0);memset(p+2,0,20);w(p+2,4);w(p+4,123);w(p+22,77);w(p+24,50);
    p=add(&f,2,-1,0);memset(p+2,0,20);w(p+2,1);w(p+6,88);
    CHECK(run(&f,&o)==ES2_OK);
    CHECK(u(o.script.data)==4 && u(o.script.data+2)==123 && u(o.script.data+4)==1);
    CHECK(f.state.variables[50]==0 && f.state.variables[77]==0 && f.state.variables[3]==9);
    CHECK(f.state.variables[4]==7 && f.state.variables[5]==1 && f.state.variables[6]==3);
    CHECK(f.state.rng.a==28 && f.state.rng.b==3);
    CHECK(u(o.strings.data+4)==10); /* six comparisons + type2 twice + two -1 */
    CHECK(u(o.strings.data+6)==12 && !memcmp(o.strings.data+8,"replacement\0\0",13));
    CHECK(o.variables.size==2000 && o.player.size==115);
    es2_shell_output_free(&o);
    /* Retained clearing selectors carry across calls even with no environment. */
    f.counts[1]=f.sizes[1]=0;f.counts[0]=f.sizes[0]=0;f.strings=2;w(f.language,0);
    f.state.variables[50]=44;CHECK(run(&f,&o)==ES2_OK && f.state.variables[50]==0);
    es2_shell_output_free(&o);
}
static void modes_and_errors(void) {
    fixture f;es2_shell_output o={0};es2_shell_state before;uint8_t *p;size_t i;
    init(&f);f.options.mode=1;f.options.player_value=23;f.state.variables[99]=77;
    add(&f,4,-1,0);
    CHECK(run(&f,&o)==ES2_OK);CHECK(u(o.script.data)==7 && u(o.script.data+14)==23 && f.state.variables[99]==77);es2_shell_output_free(&o);
    f.options.mode=0;for(i=0;i<4;i++)f.options.mode0_values[i]=(uint8_t)(201+i);
    CHECK(run(&f,&o)==ES2_OK);CHECK(f.state.variables[99]==0 && u(o.script.data)==1);
    CHECK(u(o.script.data+10)==201 && u(o.script.data+12)==202 && u(o.script.data+14)==203 && u(o.script.data+16)==0 && u(o.script.data+18)==204);es2_shell_output_free(&o);
    init(&f);p=add(&f,1,1,-1);w(p+4,1);w(p+6,0);before=f.state;
    CHECK(run(&f,&o)==ES2_ERR_ARGUMENT);CHECK(!memcmp(&before,&f.state,sizeof before) && !o.script.data);
    init(&f);p=add(&f,1,1,-1);w(p+4,0);w(p+6,1000);CHECK(run(&f,&o)==ES2_ERR_OFFSET);
    init(&f);p=add(&f,2,-1,0);memset(p+2,0,20);w(p+22,1000);CHECK(run(&f,&o)==ES2_ERR_OFFSET);
    init(&f);p=add(&f,6,1,-1);w(p+8,1);CHECK(run(&f,&o)==ES2_ERR_NOT_FOUND);
    init(&f);p=add(&f,6,1,-1);w(p+4,999);CHECK(run(&f,&o)==ES2_ERR_NOT_FOUND);
    init(&f);add(&f,4,999,0);CHECK(run(&f,&o)==ES2_ERR_UNSUPPORTED);
    init(&f);text(&f,0,-1,"a");f.language[f.strings-1]=99;CHECK(run(&f,&o)==ES2_ERR_STRING);
}
static void geometry_presentation(void) {
    fixture f;es2_shell_output o={0};uint8_t *p;const uint8_t *q;size_t n,i;
    init(&f);
    p=add(&f,1,1,-1);w(p+4,3);w(p+6,90);w(p+10,1);
    p=add(&f,1,2,1);w(p+4,2);w(p+6,0);w(p+8,0);
    text(&f,-1,-1,"hidden-slot");text(&f,50,-1,"first");text(&f,70,-1,"second");
    p=add(&f,3,91,-1);w(p+6,8);p=add(&f,3,91,-1);w(p+6,9);
    p=add(&f,4,-1,0);for(i=2;i<=140;i+=2)w(p+i,65535);w(p+2,50);w(p+6,70);w(p+142,91);
    p=add(&f,4,-1,0);w(p+2,70);w(p+142,91); /* append; writer uses first */
    add(&f,5,888,0); /* startup deliberately ignores this family */
    p=add(&f,6,-1,2);w(p+8,0);memset(p+10,255,12);
    p=add(&f,6,100,-1);w(p+8,0);memset(p+10,0,12);w(p+10,10);
    p=add(&f,6,200,-1);w(p+8,0);memset(p+10,0,12);w(p+10,20);
    p=add(&f,6,100,-1);w(p+4,90);w(p+8,0); /* full replace at original slot */
    p=add(&f,6,300,-1);w(p+8,1);w(p+10,100);w(p+14,200);
    p=add(&f,7,-1,2);w(p+8,456);
    p=add(&f,7,88,-1);w(p+8,123);
    p=add(&f,7,88,-1);w(p+4,90);
    p=add(&f,8,-1,2);w(p+8,2);w(p+10,100);w(p+12,300);f.sizes[7]+=4;
    p=add(&f,8,77,-1);w(p+8,1);w(p+10,200);f.sizes[7]+=2;
    p=add(&f,8,77,-1);w(p+8,0);w(p+4,90);
    p=add(&f,9,99,-1);w(p+6,0);w(p+8,100);w(p+10,300);
    p=add(&f,9,99,-1);w(p+6,1);w(p+8,200);w(p+10,1234);
    CHECK(run(&f,&o)==ES2_OK);
    q=section(&o,6,&n);CHECK(n==3 && u(q)==65535 && u(q+12)==20 && u(q+24)==19 && u(q+26)==0);
    CHECK(u(q+28)==65535 && u(q+30)==65535); /* unsigned wrapping of other components */
    q=section(&o,7,&n);CHECK(n==1 && u(q)==456);
    q=section(&o,8,&n);CHECK(n==1 && u(q)==2 && u(q+2)==0 && u(q+4)==2);
    q=section(&o,9,&n);CHECK(n==1 && u(q)==1 && u(q+2)==1 && u(q+4)==1234);
    q=section(&o,18,&n);CHECK(n==2 && u(q)==0 && u(q+2)==65535); /* count, then leading prefix */
    CHECK(f.state.rng.a==30); /* three setup draws and three copy draws */
    es2_shell_output_free(&o);
}
static void sparse_merges(void) {
    /* Each field gets its preservation sentinel; the output must remain
     * identical to the single-record preparation. Then non-sentinel words
     * exercise the emitted name/action/status ranges independently. */
    size_t family,i;fixture f;es2_shell_output base={0},same={0},changed={0};
    for(family=10;family<=16;family++) {
        uint8_t *p,*r;size_t n;const uint8_t *a,*b;
        init(&f);p=add(&f,family,40,-1);
        for(i=6;i<strides[family-1];i+=2)w(p+i,77);
        if(family>=12 && family<=15)w(p+4,65535);
        CHECK(run(&f,&base)==ES2_OK);
        r=add(&f,family,40,-1);
        if(family==10)w(r+78,0);
        if(family==11)w(r+8,0);
        if(family==12){memset(r+8,0,40);for(i=116;i<=134;i+=2)w(r+i,5);w(r+136,2);w(r+142,100);}
        if(family==13){memset(r+8,0,40);w(r+100,100);}
        if(family==14)w(r+60,100);
        if(family==16)memset(r+6,0,40);
        CHECK(run(&f,&same)==ES2_OK);
        CHECK(base.script.size==same.script.size && !memcmp(base.script.data,same.script.data,base.script.size));
        for(i=(family==16?6:(family>=12?8:6));i<strides[family-1];i+=2)w(r+i,88);
        CHECK(run(&f,&changed)==ES2_OK);
        a=section(&same,family,&n);CHECK(n==1);b=section(&changed,family,&n);CHECK(n==1 && u(b)==(family==11?65535u:88u));
        if(family==10){CHECK(u(b+20)==88 && u(b+40)==88 && u(b+72)==88);}
        if(family==11){CHECK(u(b+2)==88 && u(b)==65535); /* trigger ID 88 unresolved */}
        if(family==12){CHECK(u(b+66)==88 && u(b+86)==88 && u(b+106)==88 && u(b+132)==88);}
        if(family==13){CHECK(u(b+46)==88 && u(b+66)==88 && u(b+90)==88);}
        if(family==14){CHECK(u(b+6)==88 && u(b+26)==88 && u(b+50)==88);}
        if(family==15){CHECK(u(b+4)==65535 && u(b+6)==65535 && u(b+12)==65535);}
        if(family==16){CHECK(u(b+114)==88 && u(b+134)==88 && u(b+154)==88);}
        CHECK(a!=NULL);
        es2_shell_output_free(&base);es2_shell_output_free(&same);es2_shell_output_free(&changed);
    }
}
static void references_and_copies(void) {
    fixture f;es2_shell_output o={0};uint8_t *p;const uint8_t *q;size_t i,n;
    init(&f);text(&f,90,-1,"s");
    p=add(&f,1,1,-1);w(p+4,3);w(p+6,99);w(p+10,1);
    p=add(&f,1,2,1);w(p+4,2);w(p+6,0);w(p+8,0);
    p=add(&f,6,90,-1);w(p+8,0);
    p=add(&f,7,90,-1);w(p+8,123);
    p=add(&f,8,90,-1);w(p+8,0);
    p=add(&f,9,90,-1);w(p+6,0);w(p+8,90);w(p+10,90);
    for(i=7;i<=10;i++){p=add(&f,10,83+(int)i,-1);w(p+6,(unsigned)i);w(p+80,90);w(p+10,90);w(p+68,90);}
    p=add(&f,11,90,-1);w(p+6,90);w(p+10,93);
    for(i=12;i<=15;i++) {
        p=add(&f,i,-1,2);
        if(i==12){w(p+70,90);w(p+72,90);w(p+138,90);w(p+140,93);}
        if(i==13){w(p+48,90);w(p+50,90);w(p+96,90);w(p+98,93);}
        if(i==14){w(p+10,90);w(p+12,90);w(p+56,90);w(p+58,93);}
        if(i==15){w(p+12,90);w(p+14,90);w(p+16,0);w(p+18,90);w(p+20,90);}
        p=add(&f,i,90,-1);w(p+4,99);
    }
    for(i=0;i<3;i++) {p=add(&f,16,90+(int)i,-1);w(p+46,(unsigned)i);w(p+50,90);w(p+52,90);w(p+54,90);w(p+56,90);w(p+96,90);w(p+118,90);}
    for(i=0;i<4;i++) {p=add(&f,17,-1,0);w(p+6,(unsigned)i);w(p+8,90);w(p+10,90);w(p+12,90);w(p+14,90);}
    CHECK(run(&f,&o)==ES2_OK);
    q=section(&o,10,&n);CHECK(n==4);for(i=0;i<4;i++)CHECK(u(q+74*i+72)==0 && u(q+74*i+4)==0 && u(q+74*i+60)==0);
    q=section(&o,11,&n);CHECK(n==1 && u(q)==0 && u(q+4)==3);
    q=section(&o,12,&n);CHECK(n==1 && u(q+62)==0 && u(q+64)==0 && u(q+128)==0 && u(q+130)==3);
    q=section(&o,13,&n);CHECK(n==1 && u(q+40)==0 && u(q+42)==0 && u(q+86)==0 && u(q+88)==3);
    q=section(&o,14,&n);CHECK(n==1 && u(q+2)==0 && u(q+4)==0 && u(q+46)==0 && u(q+48)==3);
    q=section(&o,15,&n);CHECK(n==1 && u(q+4)==0 && u(q+6)==0 && u(q+10)==0 && u(q+12)==0);
    q=section(&o,16,&n);CHECK(n==3);for(i=0;i<3;i++)CHECK(u(q+156*i+44)==0 && u(q+156*i+50)==0 && u(q+156*i+90)==0 && u(q+156*i+112)==0);
    q=section(&o,17,&n);CHECK(n==4);for(i=0;i<4;i++)CHECK(u(q+54*i+6)==0 && u(q+54*i+8)==0 && u(q+54*i+10)==0 && u(q+54*i+12)==0);
    es2_shell_output_free(&o);
}
static void rng_and_player(void) {
    uint8_t table[112];es2_shell_rng r,before;uint16_t value;es2_error e;size_t i;
    fixture f;es2_shell_output o={0};int16_t items[]={2,3};uint16_t secondary[]={4,5};es2_mec mec;es2_mec_record rec;
    for(i=0;i<56;i++)w(table+2*i,(unsigned)i);
    CHECK(es2_shell_rng_init((es2_span){table,112},0,&r,&e)==ES2_OK && r.a==24 && r.b==55);
    before=r;CHECK(es2_shell_rng_draw(&r,0,&value,&e)==ES2_ERR_ARGUMENT && !memcmp(&r,&before,sizeof r));
    CHECK(es2_shell_rng_draw(&r,-12,&value,&e)==ES2_OK && value==7);
    CHECK(es2_shell_rng_init((es2_span){table,112},1,&r,&e)==ES2_OK && r.words[55]==79 && r.a==25);
    CHECK(es2_shell_rng_init((es2_span){table,112},32769,&r,&e)==ES2_OK && r.a==24);
    CHECK(es2_shell_rng_init((es2_span){table,112},32768,&r,&e)==ES2_OK && r.a==(24+32768)%56);
    r.a=56;CHECK(es2_shell_rng_draw(&r,12,&value,&e)==ES2_ERR_ARGUMENT);
    init(&f);f.actor.capacity=2;f.actor.items=items;f.actor.secondary=secondary;f.actor.vehicle=8;f.actor.pilot[0]=123;f.actor.pilot[1]=456;
    for(i=0;i<33;i++){f.actor.condition[i]=(uint16_t)(i*13);f.loadout.flags[i]=(uint8_t)i;}
    CHECK(run(&f,&o)==ES2_OK && o.player.size==123);
    CHECK(es2_mec_parse((es2_span){o.player.data,o.player.size},&mec,&e)==ES2_OK);
    CHECK(es2_mec_record_at(&mec,0,&rec,&e)==ES2_OK && rec.vehicle_id==8 && rec.count==2);
    CHECK(u(rec.pilot.data)==123 && u(rec.pilot.data+2)==456 && u(rec.items.data+2)==3 && u(rec.secondary.data)==4);
    CHECK(u(rec.tail.data)==0 && u(rec.tail.data+66)==416 && mec.flags.data[32]==32);
    {es2_shell_output copy={0};f.options.loadout=NULL;f.options.player_template=(es2_span){o.player.data,o.player.size};
    CHECK(run(&f,&copy)==ES2_OK && copy.player.size==o.player.size && !memcmp(copy.player.data,o.player.data,o.player.size));es2_shell_output_free(&copy);}
    es2_shell_output_free(&o);
}
static void comparison_boundaries(void) {
    size_t op;int left,right;
    /* Exercise true and false branches including signed extremes; the text
     * count witnesses retained-condition membership without exposing internals. */
    const int values[]={-32768,-1,0,1,32767};
    for(op=281;op<=286;op++)for(left=0;left<5;left++)for(right=0;right<5;right++) {
        fixture f;es2_shell_output out={0};uint8_t *p;int expected=0;
        init(&f);f.state.variables[99]=(int16_t)values[left];
        p=add(&f,1,55,-1);w(p+4,0);w(p+6,99);w(p+8,(unsigned)op);w(p+10,(unsigned)values[right]);text(&f,1,55,"pass");
        switch(op){case 281:expected=values[left]==values[right];break;case 282:expected=values[left]!=values[right];break;
        case 283:expected=values[left]<values[right];break;case 284:expected=values[left]<=values[right];break;
        case 285:expected=values[left]>values[right];break;case 286:expected=values[left]>=values[right];break;default:break;}
        CHECK(run(&f,&out)==ES2_OK && u(out.strings.data+4)==(unsigned)expected);
        CHECK(out.strings.size==(expected?14u:6u));es2_shell_output_free(&out);
    }
}
static void copy_then_relative(void) {
    fixture f;es2_shell_output out={0};uint8_t *p;const uint8_t *q;size_t n;
    init(&f);
    p=add(&f,1,1,-1);w(p+4,3);w(p+6,333);w(p+10,1);
    p=add(&f,1,2,1);w(p+4,2);w(p+6,0);w(p+8,0);
    p=add(&f,6,10,-1);w(p+8,0);memset(p+10,0,12);w(p+10,6);w(p+14,7);w(p+18,8);
    p=add(&f,6,20,-1);w(p+8,0);memset(p+10,0,12);w(p+10,9);w(p+14,10);w(p+18,11);
    p=add(&f,6,-1,2);w(p+8,0);memset(p+10,0,12);w(p+10,10);w(p+14,20);
    p=add(&f,6,30,-1);w(p+4,333);w(p+8,1); /* authored operands -1 must be overwritten first */
    CHECK(run(&f,&out)==ES2_OK);
    q=section(&out,6,&n);CHECK(n==3 && u(q+24)==15 && u(q+28)==17 && u(q+32)==19);
    es2_shell_output_free(&out);
}
int main(void) {
    conditions_environment();modes_and_errors();geometry_presentation();sparse_merges();references_and_copies();rng_and_player();
    comparison_boundaries();copy_then_relative();
    printf("shell synthetic: %u checks, %u failures\n",checks,failures);return failures!=0;
}
