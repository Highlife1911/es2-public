#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/config.h"
/* Synthetic guest for the configuration hooks: objects at 90000000h,
 * the scratch frame at A0000000h, image base B with three pages of
 * globals. Every call is logged with its arguments and entry registers;
 * the callees are emulated over small in-memory files (the stream
 * classes, the CW3220 f-functions, GetPrivateProfileStringA over a
 * key table, _stricmp and _atol over the image strings). */
#define OBJ UINT32_C(0x90000000)
#define FRAME UINT32_C(0xa0000000)
#define B UINT32_C(0x10000000)
#define G(va) (B+(va)-0x400000u)
#define F (FRAME+ES2_CONFIG_FRAME)
static uint8_t arena[8192],scratch[ES2_CONFIG_SCRATCH],low[0x2000],mid[0x2000],high[0x1000];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"config line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-FRAME<sizeof(scratch))return scratch+(a-FRAME);
    if(a-G(0x49e000)<sizeof(low))return low+(a-G(0x49e000));
    if(a-G(0x4d1000)<sizeof(mid))return mid+(a-G(0x4d1000));
    if(a-G(0x6cb000)<sizeof(high))return high+(a-G(0x6cb000));
    fprintf(stderr,"config: unmapped %08x\n",a);exit(1);
}
static uint8_t r8(uint32_t a){return *(uint8_t *)host(0,a);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static void w8(uint32_t a,uint8_t v){*(uint8_t *)host(0,a)=v;}
static void wstr(uint32_t a,const char *s){memcpy(host(0,a),s,strlen(s)+1);}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
typedef struct logged { uint32_t f,a[6],r[3]; unsigned n; } logged;
static logged log_[128];static unsigned calls,observed[4];
/* Files: 0 the source (prefs/tape member), 1 the tape, 2 the output. */
typedef struct file { uint8_t data[2048]; uint32_t size,pos; int open; } file;
static file files[3];
static uint32_t short_read,read_status,write_status,open_fail,ftell_value;
static uint8_t written[2048];static uint32_t written_size;
static const char *profile[4];
#define HANDLE(i) (OBJ+0x1f00+0x10u*(i))
static uint32_t edx_after(unsigned n){return 0xed000000u|n;}
static uint32_t ecx_after(unsigned n){return 0xec000000u|n;}
/* The image strings the emulated string functions read. */
static const char *image_string(uint32_t a){
    static char text[64];size_t n=0;
    while(n<sizeof(text)-1&&r8(a+n)){text[n]=(char)r8(a+n);++n;}
    text[n]=0;return text;
}
static int fold_compare(const char *a,const char *b){
    for(;;++a,++b){int x=toupper((unsigned char)*a),y=toupper((unsigned char)*b);if(x!=y||!x)return x-y;}
}
static uint32_t emulate(uint32_t f,const uint32_t *a,uint32_t eax){
    if(f==G(0x49630c)){const uint32_t record=F+r32(0,eax+4);w32(0,record,0x0badf00du);w16(0,record+0x10,0);return 0x12340000u;}
    if(f==G(0x45ee00))return OBJ+0x100+(a[0]&0xff);
    if(f==G(0x47ad40)){w32(0,a[0],0x4a1738u);w32(0,a[0]+4,read_status);return a[0];}
    if(f==G(0x47b064)){w32(0,a[0],0x4a1760u);w32(0,a[0]+4,write_status);return a[0];}
    if(f==G(0x47af98)){uint32_t n=a[1];if(n>files[0].size)n=files[0].size;if(short_read&&n>short_read)n=short_read;
        memcpy(host(0,a[2]),files[0].data,n);if(n!=a[1])w32(0,a[0]+4,2);return n==a[1];}
    if(f==G(0x47b2b8)){memcpy(written,host(0,a[2]),a[1]);written_size=a[1];return 1;}
    if(f==G(0x49681a)){memset(host(0,a[0]),(int)a[1],a[2]);return a[0];}
    if(f==G(0x496802)){unsigned i=a[1]==G(0x49efab)?2u:0u;if(open_fail)return 0;files[i].open=1;files[i].pos=0;if(i==2)files[2].size=0;return HANDLE(i);}
    if(f==G(0x49682c)){files[0].pos=files[0].size;return 0;}
    if(f==G(0x4967de))return ftell_value?ftell_value:files[0].pos;
    if(f==G(0x496772)){files[0].pos=0;return 0;}
    if(f==G(0x49684a)){unsigned i=(a[3]-HANDLE(0))/0x10u;file *s=&files[i];uint32_t want=a[1]*a[2],n=s->size-s->pos;
        if(n>want)n=want;if(short_read&&n>short_read)n=short_read;
        memcpy(host(0,a[0]),s->data+s->pos,n);s->pos+=n;return a[1]==1?n:n/a[1];}
    if(f==G(0x496832)){unsigned i=(a[3]-HANDLE(0))/0x10u;file *s=&files[i];uint32_t n=a[1]*a[2];
        memcpy(s->data+s->size,host(0,a[0]),n);s->size+=n;return a[2];}
    if(f==G(0x496838)){if(a[0])files[(a[0]-HANDLE(0))/0x10u].open=0;return a[0]?0:0xffffffffu;}
    if(f==G(0x496910)){ /* GetPrivateProfileStringA(section, key, default, buffer, size, path) */
        const char *key=image_string(a[1]),*value="";
        static const char *const keys[4]={"Tilt","Backturn","Missile","Rudder"},*const sound[4]={"Driver","Buffers","Rate","Width"};
        for(unsigned k=0;k<4;++k)if(!strcmp(key,keys[k])||!strcmp(key,sound[k]))value=profile[k]?profile[k]:"";
        size_t n=strlen(value);if(n>a[4]-1)n=a[4]-1;memcpy(host(0,a[3]),value,n);w8(a[3]+(uint32_t)n,0);return (uint32_t)n;}
    if(f==G(0x496868)){char x[64];snprintf(x,sizeof(x),"%s",image_string(a[0]));return (uint32_t)fold_compare(x,image_string(a[1]));}
    if(f==G(0x496880))return (uint32_t)strtol(image_string(a[0]),NULL,10);
    if(f>=OBJ+0x1000&&f<OBJ+0x1100)return 0x5a5a0000u|(a[0]&0xff); /* a prefs callback */
    return eax;
}
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<128);logged *l=log_+calls;l->f=f;l->n=n;if(n)memcpy(l->a,a,4*n);
    l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    uint32_t eax=emulate(f,a,r->eax);
    if(f==G(0x49630c)){r->eax=eax;++calls;return;}   /* the frame setup keeps EDX and ECX */
    r->eax=eax;r->edx=edx_after(calls);r->ecx=ecx_after(calls);++calls;
}
static void observe(void *c,uint32_t f,unsigned slot){
    (void)c;(void)slot;
    if(f==ES2_CONFIG_RESIDUE)++observed[1];else if(f==ES2_CONFIG_UNMODELED)++observed[2];
    else if(f==ES2_CONFIG_CALLBACK)++observed[3];else ++observed[0];
}
static es2_weapon_graph graph(void){es2_weapon_graph g={B,FRAME,0,0,0,call,0,observe,0};return g;}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
static void reset(void){
    calls=0;memset(observed,0,sizeof(observed));memset(log_,0,sizeof(log_));memset(files,0,sizeof(files));
    short_read=read_status=write_status=open_fail=ftell_value=0;written_size=0;memset(profile,0,sizeof(profile));
    memset(arena,0,sizeof(arena));memset(scratch,0,sizeof(scratch));memset(low,0,sizeof(low));memset(mid,0,sizeof(mid));
    memset(high,0,sizeof(high));
    /* The image strings and descriptors the functions name. */
    wstr(G(0x49f287),"data\\prefs.cfg");wstr(G(0x49f296),"rb");wstr(G(0x49efa8),"rb");wstr(G(0x49efab),"wb");
    w32(0,G(0x49e92c)+4,(uint32_t)-0x2c);w32(0,G(0x49e980)+4,(uint32_t)-0x38);
    wstr(G(0x49ebe0),"tap");wstr(G(0x49ebf4),"data\\keyjoy.cfg");
    static const struct { uint32_t at; const char *text; } k[]={{0x49ec04,"Keyjoy"},{0x49ec0b,"Tilt"},{0x49ec11,"Reverse"},
        {0x49ec19,"Keyjoy"},{0x49ec20,"Backturn"},{0x49ec2a,"Reverse"},{0x49ec32,"Keyjoy"},{0x49ec39,"Missile"},
        {0x49ec42,"Reverse"},{0x49ec4a,"Keyjoy"},{0x49ec51,"Rudder"},{0x49ec59,"Reverse"},{0x49fa4b,"data\\sound.cfg"},
        {0x49fa5a,"Sound"},{0x49fa60,"Driver"},{0x49fa68,"DirectSound"},{0x49fa74,"Sound"},{0x49fa7a,"Buffers"},
        {0x49fa83,"Sound"},{0x49fa89,"Rate"},{0x49fa8f,"Sound"},{0x49fa95,"Width"},{0x49fa9c,"Mono"}};
    for(unsigned i=0;i<sizeof(k)/sizeof(k[0]);++i)wstr(G(k[i].at),k[i].text);
    w32(0,G(0x6cbee4),G(0x6cbf00));
}
static unsigned find(unsigned from,uint32_t f){for(unsigned i=from;i<calls;++i)if(log_[i].f==f)return i;return 999;}

static void test_prefs_video(void){
    es2_weapon_graph g=graph();uint32_t ecx=0;
    /* No file: the argument 1 maps to mode 0 (320x240). */
    reset();open_fail=1;
    uint64_t r=es2_prefs_video_registers(&memory,&g,0x5501,entry,&ecx);
    CHECK(calls==2&&log_[0].f==G(0x45ee00)&&log_[0].a[0]==G(0x49f287)&&log_[0].r[0]==entry[0]);
    CHECK(log_[1].f==G(0x496802)&&log_[1].a[1]==G(0x49f296)&&log_[1].r[2]==G(0x49f287)&&log_[1].r[1]==edx_after(0));
    CHECK(r8(G(0x49f254))==1&&r8(G(0x4d2537))==0&&r32(0,G(0x4d253e))==0x140&&r32(0,G(0x4d2552))==0x1e0);
    CHECK((uint32_t)r==0&&r>>32==edx_after(1)&&r16(0,G(0x4d253a))==0&&r16(0,G(0x4d2566))==1);
    /* The file: byte 4 = 1 (low resolution), byte 6 = 1 (full screen). */
    reset();files[0].size=54;files[0].data[4]=1;files[0].data[6]=1;files[0].data[5]=0x77;
    r=es2_prefs_video_registers(&memory,&g,3,entry,&ecx);
    CHECK(calls==4&&log_[2].f==G(0x49684a)&&log_[2].a[0]==F-8&&log_[2].a[1]==1&&log_[2].a[2]==7&&log_[2].r[0]==F-8);
    CHECK(log_[3].f==G(0x496838)&&log_[3].r[0]==1&&r32(0,G(0x4d255e))==1&&r8(G(0x4d2537))==0);
    CHECK(ecx==0x00017701u);
    /* Byte 4 = 2: mode 3 with the full-screen byte 4d256c (path C). */
    reset();files[0].size=7;files[0].data[4]=2;
    r=es2_prefs_video_registers(&memory,&g,1,entry,&ecx);
    CHECK(r8(G(0x4d2537))==3&&r8(G(0x4d256c))==1&&r32(0,G(0x4d2546))==0x500&&r16(0,G(0x4d253c))==1);
    CHECK((uint32_t)r==1u&&r>>32==edx_after(3));
    /* A short file: the bytes are ignored, the argument decides. */
    reset();files[0].size=5;files[0].data[4]=1;
    r=es2_prefs_video_registers(&memory,&g,1,entry,&ecx);
    CHECK(calls==4&&r8(G(0x4d2537))==0&&r32(0,G(0x4d255e))==0);
    /* Done byte set: no call, the argument 1 selects path B (EDX low word 1). */
    reset();w8(G(0x49f254),1);
    r=es2_prefs_video_registers(&memory,&g,1,entry,&ecx);
    CHECK(calls==0&&r8(G(0x4d2537))==3&&r8(G(0x4d256c))==0&&(uint32_t)r==entry[0]&&r>>32==((entry[1]&0xffff0000u)|1u));
    r=es2_prefs_video_registers(&memory,&g,7,entry,&ecx);
    CHECK(calls==0&&r8(G(0x4d256c))==1&&(uint32_t)r==((entry[0]&0xffff0000u)|1u)&&r>>32==entry[1]);
}

static void test_prefs_load(void){
    es2_weapon_graph g=graph();uint32_t ecx=0;
    reset();files[0].size=54;for(unsigned i=0;i<54;++i)files[0].data[i]=(uint8_t)(i+1);
    w32(0,G(0x4d1fdc),OBJ+0x1000);w32(0,G(0x4d1fdc)+8,OBJ+0x1010);
    for(unsigned i=0;i<54;++i){w8(G(0x4d1f6e)+i,0xee);w8(G(0x4d1fa4)+i,0xdd);}
    uint64_t r=es2_prefs_load_registers(&memory,&g,1,entry,&ecx);
    CHECK(calls==7&&observed[3]==2&&log_[0].f==G(0x49630c)&&log_[0].r[0]==G(0x49e92c));
    CHECK(log_[1].f==G(0x47ad40)&&log_[1].a[0]==F-0xc&&log_[1].a[1]==G(0x49e81c)&&log_[1].r[0]==0x12340000u&&log_[1].r[1]==F-0xc);
    CHECK(log_[2].f==G(0x49681a)&&log_[2].a[0]==G(0x4d1f38)&&log_[2].a[2]==54&&log_[3].f==G(0x47af98)&&log_[3].r[2]==F-0xc);
    /* Callback 0 gets EDX with byte 1 in its low byte, EAX the block pointer. */
    CHECK(log_[4].f==OBJ+0x1000&&log_[4].n==1&&log_[4].a[0]==((edx_after(3)&0xffffff00u)|1u)&&log_[4].r[0]==G(0x4d1f38));
    CHECK(log_[5].f==OBJ+0x1010&&log_[5].a[0]==((edx_after(4)&0xffffff00u)|3u)&&log_[5].r[0]==G(0x4d1f38)+2);
    CHECK(log_[6].f==G(0x47ad78)&&log_[6].a[1]==2&&log_[6].r[2]==F-0xc);
    CHECK(r8(G(0x4d1f6e))==1&&r8(G(0x4d1f6e)+53)==54&&r8(G(0x4d1fa4)+20)==21);
    CHECK((uint32_t)r==0x0badf00du&&r>>32==edx_after(6)&&r16(0,F-0x1c)==8);
    /* Flag 0 and no file: the block is zero, the committed copy is kept. */
    reset();read_status=1;for(unsigned i=0;i<54;++i)w8(G(0x4d1f6e)+i,(uint8_t)(0x40+i));w8(G(0x4d1f38)+5,9);
    r=es2_prefs_load_registers(&memory,&g,0x100,entry,&ecx);
    CHECK(calls==4&&r8(G(0x4d1f38)+5)==0&&r8(G(0x4d1f6e)+5)==0x45&&r8(G(0x4d1fa4)+5)==0x45&&ecx==ecx_after(3));
}

static void test_prefs_store(void){
    es2_weapon_graph g=graph();uint32_t ecx=0;
    const uint32_t list=OBJ+0x200;
    reset();files[0].size=54;for(unsigned i=0;i<54;++i){files[0].data[i]=(uint8_t)(0x80+i);w8(G(0x4d1f38)+i,(uint8_t)i);}
    w16(0,list,6);w16(0,list+2,12);w16(0,list+4,36);
    uint64_t r=es2_prefs_store_registers(&memory,&g,0x10003,list,entry,&ecx);
    CHECK(calls==7&&log_[0].r[0]==G(0x49e980)&&log_[2].f==G(0x47af98)&&log_[2].a[2]==F-0x70&&log_[2].r[0]==F-0xc&&log_[2].r[2]==F-0x70);
    CHECK(log_[3].f==G(0x47b064)&&log_[3].a[0]==F-0x18&&log_[3].r[0]==F-0x18&&log_[3].r[1]==3u);
    CHECK(log_[3].r[2]==((ecx_after(2)&0xffffff00u)|36u));
    CHECK(log_[4].f==G(0x47b2b8)&&log_[4].a[1]==54&&log_[4].r[2]==F-0x70&&written_size==54);
    CHECK(written[6]==6&&written[12]==12&&written[36]==36&&written[7]==0x87&&written[53]==0x80+53);
    CHECK(log_[5].f==G(0x47b09c)&&log_[5].r[1]==F-0x18&&log_[6].f==G(0x47ad78)&&log_[6].r[2]==F-0xc);
    CHECK((uint32_t)r==0x0badf00du&&r>>32==edx_after(6)&&observed[1]==0&&observed[2]==0);
    /* Count 0 (high word ignored): the file is rewritten unchanged. */
    reset();files[0].size=54;files[0].data[3]=0x33;
    r=es2_prefs_store_registers(&memory,&g,0xffff0000u,list,entry,&ecx);
    CHECK(written[3]==0x33&&log_[3].r[1]==0);
    /* No file: the zeroed buffer is written (the original: its residue). */
    reset();read_status=1;w16(0,list,6);
    r=es2_prefs_store_registers(&memory,&g,1,list,entry,&ecx);
    CHECK(calls==6&&find(0,G(0x47af98))==999&&written[6]==0&&observed[1]==1&&log_[2].r[1]==edx_after(1));
    /* A short file, a write stream that failed to open. */
    reset();files[0].size=20;short_read=0;write_status=1;w16(0,list,6);
    r=es2_prefs_store_registers(&memory,&g,1,list,entry,&ecx);
    CHECK(observed[1]==1&&find(0,G(0x47b2b8))==999&&log_[4].r[0]==F-0x18);
    /* Index 64h lands in the read stream object; 70h and -1 are not stored. */
    reset();files[0].size=54;w8(G(0x4d1f38)+0x64,0x5c);w16(0,list,0x64);w16(0,list+2,0x70);w16(0,list+4,0xffff);
    r=es2_prefs_store_registers(&memory,&g,3,list,entry,&ecx);
    CHECK(r8(F-0xc)==0x5c&&observed[2]==2);
}

static void test_keyjoy(void){
    es2_weapon_graph g=graph();uint32_t ecx=0;
    reset();profile[0]="reverse";profile[1]="Default";profile[3]="REVERSE";
    uint64_t r=es2_keyjoy_load_registers(&memory,&g,entry,&ecx);
    CHECK(calls==9&&log_[0].f==G(0x45ee00)&&log_[0].a[0]==G(0x49ebf4)&&log_[0].r[0]==G(0x49ebf4));
    CHECK(log_[1].f==G(0x496910)&&log_[1].n==6&&log_[1].a[0]==G(0x49ec04)&&log_[1].a[1]==G(0x49ec0b)&&log_[1].a[2]==G(0x49ec10));
    CHECK(log_[1].a[3]==F-0x28&&log_[1].a[4]==0x28&&log_[1].a[5]==OBJ+0x100+0xf4);
    CHECK(log_[1].r[0]==G(0x49ec04)&&log_[1].r[1]==G(0x49ec10)&&log_[1].r[2]==G(0x49ec0b));
    CHECK(log_[2].f==G(0x496868)&&log_[2].a[0]==F-0x28&&log_[2].a[1]==G(0x49ec11)&&log_[2].r[1]==G(0x49ec11)&&log_[2].r[2]==F-0x28);
    CHECK(log_[7].a[0]==G(0x49ec4a)&&log_[7].a[1]==G(0x49ec51)&&log_[7].a[2]==G(0x49ec58));
    CHECK(r32(0,G(0x49ea90))==1&&r32(0,G(0x49ea94))==0&&r32(0,G(0x49ea98))==0&&r32(0,G(0x49ea9c))==1);
    CHECK((uint32_t)r==0&&r>>32==edx_after(8)&&ecx==ecx_after(8));
}

static void test_sound(void){
    es2_weapon_graph g=graph();uint32_t ecx=0;
    const uint32_t o=OBJ+0x400;
    reset();for(unsigned i=0;i<0x48;++i)w8(o+i,0xcc);profile[0]="directsound";profile[1]="64";profile[2]="11";profile[3]="mono";
    w32(0,G(0x6cbf00),41);
    uint64_t r=es2_sound_config_registers(&memory,&g,o,entry,&ecx);
    CHECK(calls==10&&log_[0].r[0]==0&&log_[0].r[1]==0&&log_[0].r[2]==0&&log_[0].a[0]==G(0x49fa4b));
    CHECK(log_[1].a[0]==G(0x49fa5a)&&log_[1].a[1]==G(0x49fa60)&&log_[1].a[2]==G(0x49fa67)&&log_[1].r[2]==G(0x49fa60));
    CHECK(log_[3].a[0]==G(0x49fa74)&&log_[3].a[1]==G(0x49fa7a)&&log_[4].f==G(0x496880)&&log_[4].r[1]==F-0x28);
    CHECK(log_[5].a[1]==G(0x49fa89)&&log_[5].r[0]==G(0x49fa8e)&&log_[5].r[2]==G(0x49fa83)&&log_[6].r[0]==F-0x28);
    CHECK(log_[7].a[1]==G(0x49fa95)&&log_[7].r[0]==G(0x49fa95)&&log_[7].r[1]==G(0x49fa8f)&&log_[8].f==G(0x496880)&&log_[8].r[2]==F-0x28);
    CHECK(log_[9].f==G(0x496868)&&log_[9].r[0]==G(0x49fa9c)&&log_[9].r[1]==F-0x28&&log_[9].r[2]==F-0x28);
    CHECK(r16(0,o+0x2a)==2&&r16(0,o+0x30)==64&&r16(0,o+0x24)==0x10&&r16(0,o+0x28)==4&&r32(0,o+0x20)==1);
    CHECK(r32(0,o+0x34)==0&&r32(0,o+0x1c)==0&&r16(0,o+0x26)==1&&r16(0,o+0x32)==0x200&&r8(o+0x1b)==0xcc);
    CHECK((uint32_t)r==o&&r>>32==edx_after(9)&&ecx==G(0x6cbf00)&&r32(0,G(0x6cbf00))==42);
    /* No file: MME, 5 buffers, 20h, 8. */
    reset();
    r=es2_sound_config_registers(&memory,&g,o,entry,&ecx);
    CHECK(r16(0,o+0x2a)==1&&r16(0,o+0x30)==5&&r16(0,o+0x24)==0x20&&r16(0,o+0x28)==8);
    reset();profile[1]="65";es2_sound_config_registers(&memory,&g,o,entry,&ecx);CHECK(r16(0,o+0x30)==5);
    reset();profile[1]="1";es2_sound_config_registers(&memory,&g,o,entry,&ecx);CHECK(r16(0,o+0x30)==1);
}

static void test_tape_save(void){
    es2_weapon_graph g=graph();uint32_t ecx=0;
    const uint32_t tape=HANDLE(1),path=OBJ+0x300;
    reset();files[0].size=1200;for(unsigned i=0;i<1200;++i)files[0].data[i]=(uint8_t)(i*7);
    uint64_t r=es2_tape_save_registers(&memory,&g,tape,path,entry,&ecx);
    CHECK(calls==12&&log_[0].f==G(0x496802)&&log_[0].a[0]==path&&log_[0].a[1]==G(0x49efa8)&&log_[0].r[0]==path);
    CHECK(log_[1].f==G(0x49682c)&&log_[1].a[0]==HANDLE(0)&&log_[1].a[2]==2&&log_[2].f==G(0x4967de));
    CHECK(log_[3].f==G(0x496772)&&log_[3].r[0]==1200&&log_[3].r[2]==HANDLE(0));
    CHECK(log_[4].f==G(0x496832)&&log_[4].a[0]==F-4&&log_[4].a[1]==4&&log_[4].a[2]==1&&log_[4].r[1]==tape&&log_[4].r[2]==F-4);
    CHECK(log_[5].f==G(0x49684a)&&log_[5].a[2]==500&&log_[5].r[0]==F-0x1f8&&log_[9].a[2]==200);
    CHECK(log_[6].r[1]==tape&&log_[6].r[2]==F-0x1f8&&log_[11].f==G(0x496838)&&log_[11].a[0]==HANDLE(0));
    CHECK(files[1].size==1204&&files[1].data[0]==0xb0&&files[1].data[1]==4&&!memcmp(files[1].data+4,files[0].data,1200));
    CHECK((uint32_t)r==0&&r>>32==edx_after(11)&&ecx==HANDLE(0)&&observed[1]==0);
    /* No file: length 0, fclose(NULL). */
    reset();open_fail=1;
    r=es2_tape_save_registers(&memory,&g,tape,path,entry,&ecx);
    CHECK(calls==3&&files[1].size==4&&log_[1].r[0]==0&&log_[2].a[0]==0&&ecx==0);
    /* A file that shrinks: the short read counts residue; a negative length is not modeled. */
    reset();files[0].size=100;ftell_value=300;
    es2_tape_save_registers(&memory,&g,tape,path,entry,&ecx);
    CHECK(observed[1]==1&&files[1].size==304);
    reset();files[0].size=10;ftell_value=0xffffffffu;
    es2_tape_save_registers(&memory,&g,tape,path,entry,&ecx);
    CHECK(observed[2]==1&&files[1].size==4&&find(0,G(0x49684a))==999);
}

static void test_tape_restore(void){
    es2_weapon_graph g=graph();uint32_t ecx=0;
    const uint32_t tape=HANDLE(1),path=OBJ+0x300;
    reset();w32(0,OBJ+0x10,1100);memcpy(files[1].data,host(0,OBJ+0x10),4);files[1].size=4+1100+8;
    for(unsigned i=0;i<1108;++i)files[1].data[4+i]=(uint8_t)(i*3);
    uint64_t r=es2_tape_restore_registers(&memory,&g,tape,path,entry,&ecx);
    CHECK(calls==9&&log_[0].f==G(0x496802)&&log_[0].a[1]==G(0x49efab)&&log_[0].r[0]==path&&log_[0].r[1]==entry[1]);
    CHECK(log_[1].f==G(0x49684a)&&log_[1].a[0]==F-4&&log_[1].a[1]==1&&log_[1].a[2]==4&&log_[1].a[3]==tape&&log_[1].r[0]==F-4);
    CHECK(log_[2].a[2]==500&&log_[3].f==G(0x496832)&&log_[3].a[3]==HANDLE(2)&&log_[3].r[1]==F-0x1f8&&log_[7].a[2]==100);
    CHECK(files[2].size==1100&&!memcmp(files[2].data,files[1].data+4,1100)&&files[1].pos==1104&&ecx==HANDLE(2));
    CHECK((uint32_t)r==0&&r>>32==edx_after(8));
    /* No path: the bytes are skipped, fclose(NULL). */
    reset();w32(0,OBJ+0x10,600);memcpy(files[1].data,host(0,OBJ+0x10),4);files[1].size=604;
    r=es2_tape_restore_registers(&memory,&g,tape,0,entry,&ecx);
    CHECK(calls==4&&log_[0].f==G(0x49684a)&&log_[0].r[0]==F-4&&log_[0].r[1]==entry[1]&&log_[3].a[0]==0&&files[1].pos==604);
    /* A truncated tape: residue counted. */
    reset();files[1].size=2;
    es2_tape_restore_registers(&memory,&g,tape,path,entry,&ecx);
    CHECK(observed[1]==1);
}

int main(void){
    test_prefs_video();test_prefs_load();test_prefs_store();test_keyjoy();test_sound();test_tape_save();test_tape_restore();
    printf("config: %u checks passed\n",checks);
    return 0;
}
