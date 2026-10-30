#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/asset_load.h"
/* Synthetic guest for the loader functions: one archive file built
 * here (directory dba\ with a GLBitmapArray of two GLBitmaps, a single
 * GLBitmap, an object of an unregistered tag and an object of a class the
 * test does not model), the original's archive object A with copies of its
 * tables, its buffered stream B, the GL registry, vtables and globals in a
 * flat image, and stream objects. Guest calls go to a small model: new
 * (a bump heap), the nested loader and the class functions as the core
 * functions themselves, the stream close as the core, a logging stub
 * for the rest. Expected values follow the contract in asset_loading.md. */
#define B0 UINT32_C(0x00400000)
#define OBJ UINT32_C(0x90000000)
static uint8_t arena[0x8000];
static uint8_t *image;
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"asset_load line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-B0<0x2d8000u)return image+(a-B0);
    fprintf(stderr,"asset_load: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
static void put16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}

/* Arena layout. */
#define A (OBJ+0x100)          /* archive object */
#define BS (OBJ+0x140)         /* its buffered stream */
#define DIRS (OBJ+0x180)
#define BLOB (OBJ+0x1a0)
#define RECS (OBJ+0x200)
#define S (OBJ+0x400)          /* stream */
#define S2 (OBJ+0x440)         /* a stream of another class */
#define OB (OBJ+0x500)         /* a bitmap object for the direct read */
#define SCR (OBJ+0x600)        /* scratch: 0x140 per nesting level, compare at +500h */
#define AR (OBJ+0xd00)         /* an arena record and its memory */
#define AMEM (OBJ+0xe00)
#define HEAP (OBJ+0x2000)      /* the bump heap of new */
#define COUNTER (OBJ+0x1f00)
/* Image globals and tables. */
#define G(va) (B0+(uint32_t)(va)-0x400000u)

static uint8_t file[1024];
static size_t file_size;
static es2_vol vol;
static es2_vfs_archive archive_entry;
static uint32_t block[4], length[4];
/* Member payloads. */
static size_t member_array(uint8_t *p){
    /* GLBitmapArray, 2 children: 2x3 (flags 35h, compression 1, P 6, C 1,
     * odd body 23 + pad), 1x1 (P 1, C 0, body 14). */
    put32(p,0x00280001u);put32(p+4,58);put32(p+8,2);
    uint8_t *c=p+12;
    put32(c,0x0028000eu);put32(c+4,23);put16(c+8,2);put16(c+10,3);c[12]=8;c[13]=0x35;c[14]=1;put32(c+15,6);put16(c+19,1);
    for(unsigned i=0;i<6;++i)c[21+i]=(uint8_t)(0xa0+i);
    put32(c+27,0x11223344u);c[31]=0;
    c+=32;
    put32(c,0x0028000eu);put32(c+4,14);put16(c+8,1);put16(c+10,1);c[12]=8;c[13]=0;c[14]=0;put32(c+15,1);put16(c+19,0);c[21]=0x77;
    return 66;
}
static size_t member_bitmap(uint8_t *p){
    put32(p,0x0028000eu);put32(p+4,15);put16(p+8,1);put16(p+10,2);p[12]=8;p[13]=0x1f;p[14]=0;put32(p+15,2);put16(p+19,0);
    p[21]=0x55;p[22]=0x66;p[23]=0;p[24]=0xee;                  /* odd body, pad, then one more byte */
    return 25;
}
static size_t member_unknown(uint8_t *p){put32(p,0x12345678u);put32(p+4,4);put32(p+8,0xdeadbeefu);return 12;}
static size_t member_other(uint8_t *p){put32(p,0x0028000fu);put32(p+4,2);p[8]=1;p[9]=2;return 10;}
static void build_file(void){
    memset(file,0,sizeof(file));
    memcpy(file,"VOLN",4);put32(file+4,1);file[8]=5;file[9]=1;file[10]=5;
    memcpy(file+12,"dba\\",5);
    size_t t=17;file[t]=4;put32(file+t+2,72);t+=6;
    const char *names[4]={"A.DBA","B.DBM","C.XXX","D.DPL"};
    size_t (*const make[4])(uint8_t *)={member_array,member_bitmap,member_unknown,member_other};
    size_t at=t+72;
    for(unsigned i=0;i<4;++i){
        uint8_t *slot=file+t+18*i;memcpy(slot,names[i],strlen(names[i]));slot[13]=0;put32(slot+14,(uint32_t)at);
        block[i]=(uint32_t)at;
        const size_t n=make[i](file+at+9);
        file[at]=2;put32(file+at+1,(uint32_t)n);put32(file+at+5,0xabcd0000u+i);
        file[at+9+n]=file[at+8+n];
        length[i]=(uint32_t)n;
        at+=10+n;
    }
    file_size=at;
    CHECK(es2_vol_open((es2_span){file,file_size},&vol,NULL)==ES2_OK);
}
static uint32_t heap_next;
static void build_guest(void){
    memset(arena,0,sizeof(arena));memset(image,0,0x2d8000);
    w32(0,A+4,BS);arena[A-OBJ+0x1c]=5;arena[A-OBJ+0x1d]=1;w16(0,A+0x1e,4);
    w32(0,A+0x24,DIRS);w32(0,A+0x2c,RECS);
    memcpy(arena+(BLOB-OBJ),file+12,5);w32(0,DIRS,BLOB);
    memcpy(arena+(RECS-OBJ),file+23,72);
    w32(0,G(0x6cbee4),COUNTER);w32(0,COUNTER,100);
    /* GL registry: GLBitmap, GLBitmapArray, GLPalette (not modeled). */
    const uint32_t reg=G(0x4a2eb8);
    w32(0,reg,0x0028000eu);w32(0,reg+4,G(0x481cd4));
    w32(0,reg+12,0x00280001u);w32(0,reg+16,G(0x482d88));
    w32(0,reg+24,0x0028000fu);w32(0,reg+28,G(0x4820d4));
    w32(0,G(0x6c5834),reg);w32(0,G(0x6c5838),3);w32(0,G(0x4a1690),1);
    w32(0,G(0x4a306c)+0xc,G(0x481d8c));w32(0,G(0x4a32ec)+0xc,G(0x482e7c));
    w32(0,G(0x4a16e8)+0x10,G(0x47a904));
    w32(0,G(0x4a0e10)+0x18,G(0x4735b8));w32(0,G(0x4a0e10)+4,G(0x473368));
    w32(0,G(0x6c5964),0x13572468u);
    w32(0,BS+ES2_VFS_B_BASE,0x37);w32(0,BS+ES2_VFS_B_CURSOR,3);
    heap_next=HEAP;
    archive_entry=(es2_vfs_archive){A,BS,&vol};
}
/* A member stream as the open 4730e8 leaves it (kind 2, B moved by the lookup). */
static void open_member(uint32_t s,unsigned i){
    memset(host(0,s),0,0x30);
    w32(0,s,G(0x4a0e10));w16(0,s+8,0xffff);w32(0,s+ES2_VFS_S_ARCHIVE,BS);w32(0,s+ES2_VFS_S_SOURCE,BS);
    w32(0,s+ES2_VFS_S_START,block[i]+9);w32(0,s+ES2_VFS_S_APOS,block[i]+9);w32(0,s+ES2_VFS_S_KIND,2);
    w32(0,s+ES2_VFS_S_LENGTH,length[i]);arena[s-OBJ+0x26]=1;
    w32(0,BS+ES2_VFS_B_BASE,block[i]+9);w32(0,BS+ES2_VFS_B_CURSOR,0);w32(0,BS+ES2_VFS_B_VALID,0);
}
static const es2_vfs_archive *find(void *c,uint32_t a,uint32_t b){
    (void)c;
    if(a)return a==A&&es2_vfs_stream_archive_matches(&memory,A,&vol)?&archive_entry:NULL;
    return b==BS?&archive_entry:NULL;
}
typedef struct logged{uint32_t f,a[3],r[3];unsigned n;}logged;
static logged calls[64];static unsigned call_count;
static es2_asset_host make(int commit);
static unsigned nesting;
static void guest(void *c,uint32_t f,const uint32_t *a,unsigned n,uint32_t r[3]){
    (void)c;CHECK(call_count<64);logged *l=calls+call_count++;l->f=f;l->n=n;memcpy(l->a,a,4u*(n<3?n:3));memcpy(l->r,r,12);
    ++nesting;
    es2_asset_host h=make(1);es2_asset_result o={0,0};es2_vfs_result v={0,0};
    switch(f-B0+0x400000u){
    case 0x4774f0:r[0]=heap_next;heap_next+=(a[0]+3u)&~3u;break;
    case 0x47a144:CHECK(es2_asset_dts_load(&h,a[0],a[1],&o)==ES2_VFS_DONE);r[0]=o.eax;break;
    case 0x481cd4:CHECK(es2_asset_bitmap_new(&h,a[0],a[1],0,&o)==ES2_VFS_DONE);r[0]=o.eax;break;
    case 0x482d88:CHECK(es2_asset_bitmap_array_new(&h,a[0],a[1],0,&o)==ES2_VFS_DONE);r[0]=o.eax;break;
    case 0x481d8c:CHECK(es2_asset_bitmap_read(&h,a[0],a[1],r[2],&o)==ES2_VFS_DONE);r[0]=o.eax;break;
    case 0x482e7c:CHECK(es2_asset_bitmap_array_read(&h,a[0],a[1],&o)==ES2_VFS_DONE);r[0]=o.eax;break;
    case 0x47307c:CHECK(es2_vfs_stream_close(&h.vfs,a[0],&v)==ES2_VFS_DONE);r[0]=v.eax;break;
    case 0x4820d4:r[0]=0xcafe0000u;break;                         /* a factory not modeled */
    case 0x496406:r[0]=7;break;
    case 0x477548:r[0]=0x5150u;r[1]=0x6160u;break;
    case 0x4b0018:                                                /* the other stream class: read */
        memset(host(0,a[2]),a[1]==4?0:a[1]==2?1:a[1]==1?0x3c:0x5a,a[1]);r[0]=0x99;break;
    case 0x4b0004:r[0]=0x1000u;break;                             /* its tell */
    default:r[0]=0xffffffffu;break;
    }
    --nesting;
}
static es2_asset_expect expect;
static es2_asset_host make(int commit){
    /* Each nested guest call gets its own scratch, as each hook call has
     * its own stack area in the adapter. */
    es2_asset_host h={{&memory,B0,commit,0,find,guest,NULL,NULL,NULL},SCR+(commit?0x140u*nesting:0x500u),commit?NULL:&expect};
    return h;
}
static void reset_calls(void){call_count=0;memset(calls,0,sizeof(calls));}
static unsigned count_calls(uint32_t va){unsigned k=0;for(unsigned i=0;i<call_count;++i)k+=calls[i].f==G(va);return k;}

static void test_fetch(void){
    es2_vfs_host h={&memory,B0,1,0,find,guest,NULL,NULL,NULL};es2_vfs_result o={0,0};
    build_guest();open_member(S,1);
    const uint8_t *from;uint32_t count;
    CHECK(es2_vfs_stream_fetch(&h,S,4,0,0,0,&from,&count,&o)==ES2_VFS_DONE&&count==4&&from==file+block[1]+9);
    CHECK(r32(0,S+ES2_VFS_S_POS)==4&&r32(0,BS+ES2_VFS_B_BASE)==block[1]+13&&o.eax==(((block[1]+13)&~0xffu)|1u));
    /* Compare without move: B stays; the next fetch resyncs to the same bytes. */
    es2_vfs_host c=h;c.commit=0;
    CHECK(es2_vfs_stream_fetch(&c,S,2,SCR,0,0,&from,&count,&o)==ES2_VFS_DONE&&count==2&&arena[SCR-OBJ]==0x0f&&arena[SCR-OBJ+1]==0);
    CHECK(r32(0,BS+ES2_VFS_B_BASE)==block[1]+13&&r32(0,S+ES2_VFS_S_POS)==6);
    CHECK(es2_vfs_stream_fetch(&c,S,1,0,0,1,&from,&count,&o)==ES2_VFS_DONE&&from==file+block[1]+15&&r32(0,BS+ES2_VFS_B_BASE)==block[1]+16);
    /* Past the end: length-pos+1 bytes, status 2, EAX 0; then nothing. */
    w32(0,S+ES2_VFS_S_POS,length[1]-1);w32(0,S+ES2_VFS_S_APOS,block[1]+8+length[1]);w32(0,BS+ES2_VFS_B_BASE,block[1]+8+length[1]);
    CHECK(es2_vfs_stream_fetch(&h,S,4,0,0,0,&from,&count,&o)==ES2_VFS_DONE&&count==2&&o.eax==0&&r32(0,S+4)==2);
    CHECK(es2_vfs_stream_fetch(&h,S,4,0,0,0,&from,&count,&o)==ES2_VFS_DONE&&count==0&&from==NULL);
    /* Size 0: the entry EAX with AL 1; a handle: not modeled. */
    w32(0,S+4,0);CHECK(es2_vfs_stream_fetch(&h,S,0,0,0x12345678u,0,&from,&count,&o)==ES2_VFS_DONE&&o.eax==0x12345601u);
    w16(0,S+8,5);CHECK(es2_vfs_stream_fetch(&h,S,1,0,0,0,&from,&count,&o)==ES2_VFS_PASS);
    CHECK(es2_vfs_stream_fetch(&c,S,1,0,0,0,&from,&count,&o)==ES2_VFS_SKIP);
}

static void test_load_array(void){
    build_guest();open_member(S,0);reset_calls();
    es2_asset_host h=make(1);es2_asset_result o={0,0};
    CHECK(es2_asset_dts_load(&h,S,0x77,&o)==ES2_VFS_DONE);
    const uint32_t a=o.eax;
    /* Allocations in order: the array (0Ch), the pointer array (8), then per
     * child the object (1Ah), the pixels and (first child) the colours. */
    CHECK(count_calls(0x4774f0)==7&&calls[0].f==G(0x482d88)&&calls[0].a[0]==S&&calls[0].a[1]==0x77);
    CHECK(a==HEAP&&r32(0,a)==G(0x4a32ec)&&r32(0,a+4)==2&&r32(0,a+8)==HEAP+0xc);
    const uint32_t b1=r32(0,a+8),c1=r32(0,b1),c2=r32(0,b1+4);
    CHECK(r32(0,c1)==G(0x4a306c)&&r16(0,c1+4)==2&&r16(0,c1+6)==3&&arena[c1-OBJ+8]==8&&arena[c1-OBJ+9]==0x15);
    CHECK(r16(0,c1+0x12)==1&&r32(0,c1+0xa)==6&&r16(0,c1+0x14)==1);
    CHECK(!memcmp(host(0,r32(0,c1+0xe)),file+block[0]+9+12+21,6)&&r32(0,r32(0,c1+0x16))==0x11223344u);
    CHECK(r32(0,c2)==G(0x4a306c)&&r16(0,c2+4)==1&&r32(0,c2+0xa)==1&&arena[r32(0,c2+0xe)-OBJ]==0x77&&r32(0,c2+0x16)==0);
    /* The stream is at the end of the array (the first child's pad byte was
     * drained), the counter and the context are as before. */
    CHECK(r32(0,S+ES2_VFS_S_POS)==66&&r32(0,S+4)==0&&r32(0,COUNTER)==100&&r32(0,G(0x6c5964))==0x13572468u);
    CHECK(r32(0,BS+ES2_VFS_B_BASE)==block[0]+9+66&&r32(0,BS+ES2_VFS_B_VALID)==0);
    /* The factory call: EAX = ECX = the registry entry, EDX = arg. */
    CHECK(calls[0].r[0]==G(0x4a2eb8)+12&&calls[0].r[2]==G(0x4a2eb8)+12&&calls[0].r[1]==0x77);
    uint8_t heap_copy[0x100];memcpy(heap_copy,host(0,HEAP),sizeof(heap_copy));
    uint8_t s_after[0x30];memcpy(s_after,host(0,S),0x30);
    /* Compare: the same load as a description, S's clone equal. */
    build_guest();open_member(S,0);reset_calls();
    es2_asset_host c=make(0);
    CHECK(es2_asset_dts_load(&c,S,0x77,&o)==ES2_VFS_DONE&&call_count==0&&o.eax==0);
    CHECK(!memcmp(host(0,S),s_after,0x30)&&r32(0,COUNTER)==100&&r32(0,G(0x6c5964))==0x13572468u);
    CHECK(expect.count==3&&expect.objects[0].kind==ES2_ASSET_ARRAY&&expect.objects[0].count==2&&expect.objects[0].end==3);
    const es2_asset_object *e1=&expect.objects[1],*e2=&expect.objects[2];
    CHECK(e1->kind==ES2_ASSET_BITMAP&&e1->height==2&&e1->width==3&&e1->depth==8&&e1->flags==0x15&&e1->compression==1);
    CHECK(e1->pixel_size==6&&e1->colours==1&&e1->pixels==file+block[0]+9+33&&e1->colour_bytes==file+block[0]+9+39);
    CHECK(!memcmp(e1->pixels,file+block[0]+9+33,6)&&!memcmp(heap_copy+0x30,e1->pixels,6));
    CHECK(e2->pixel_size==1&&*e2->pixels==0x77&&e2->colour_bytes==NULL&&e2->end==3);
}

static void test_bitmap_read(void){
    /* The read alone: +9 keeps bits 5-7, a read past the end keeps the
     * local byte (ECX's top byte) and the fields. */
    build_guest();open_member(S,1);
    es2_asset_host h=make(1);es2_asset_result o={0,0};
    w32(0,S+ES2_VFS_S_POS,8);w32(0,S+ES2_VFS_S_APOS,block[1]+17);w32(0,BS+ES2_VFS_B_BASE,block[1]+17);
    memset(host(0,OB),0xff,0x1a);w32(0,OB,G(0x4a306c));
    reset_calls();
    CHECK(es2_asset_bitmap_read(&h,OB,S,0x12345678u,&o)==ES2_VFS_DONE);
    CHECK(r16(0,OB+4)==1&&r16(0,OB+6)==2&&arena[OB-OBJ+9]==0xff&&r16(0,OB+0x12)==0&&r32(0,OB+0xa)==2&&r32(0,OB+0x16)==0);
    CHECK(arena[r32(0,OB+0xe)-OBJ]==0x55&&arena[r32(0,OB+0xe)-OBJ+1]==0x66&&count_calls(0x4774f0)==1&&calls[0].a[0]==2);
    CHECK(r32(0,S+ES2_VFS_S_POS)==23&&o.eax==(((block[1]+9+23)&~0xffu)|1u));
    /* Compare on a clone with the same pre-state: equal fields, pointers 0. */
    build_guest();open_member(S,1);
    w32(0,S+ES2_VFS_S_POS,8);w32(0,S+ES2_VFS_S_APOS,block[1]+17);w32(0,BS+ES2_VFS_B_BASE,block[1]+17);
    memset(host(0,OB),0xff,0x1a);w32(0,OB,G(0x4a306c));
    es2_asset_host c=make(0);es2_asset_result q={0,0};
    CHECK(es2_asset_bitmap_read(&c,OB,S,0x12345678u,&q)==ES2_VFS_DONE&&q.eax==o.eax&&r32(0,OB+0xe)==0);
    CHECK(expect.count==1&&expect.objects[0].pixels==file+block[1]+9+21&&expect.objects[0].flags==0x1f);
    /* Status set: nothing is read; +9 and +12h come from ECX's top byte. */
    build_guest();open_member(S,1);w32(0,S+4,2);
    memset(host(0,OB),0,0x1a);arena[OB-OBJ+9]=0xe0;reset_calls();
    CHECK(es2_asset_bitmap_read(&h,OB,S,0xab000000u,&o)==ES2_VFS_DONE&&o.eax==0);
    CHECK(arena[OB-OBJ+9]==(0xe0|0x0b)&&r16(0,OB+0x12)==0xab&&r32(0,OB+0xe)==0&&call_count==0);
    /* Another stream class: the reads are its virtual calls. */
    build_guest();memset(host(0,S2),0,0x30);w32(0,S2,G(0x4b0000));w32(0,G(0x4b0000)+0x18,G(0x4b0018));
    memset(host(0,OB),0,0x1a);reset_calls();
    CHECK(es2_asset_bitmap_read(&h,OB,S2,0,&o)==ES2_VFS_DONE&&o.eax==0x99);
    CHECK(r16(0,OB+4)==0x0101&&arena[OB-OBJ+8]==0x3c&&r32(0,OB+0xa)==0&&r16(0,OB+0x14)==0x0101&&r16(0,OB+0x12)==0x3c);
    CHECK(count_calls(0x4b0018)==8&&count_calls(0x4774f0)==1&&calls[8].a[1]==0x404&&arena[r32(0,OB+0x16)-OBJ+0x403]==0x5a);
    CHECK(calls[8].r[0]==0x404&&calls[8].r[1]==G(0x4b0000));     /* the colour read: EAX = size, EDX = vtable */
    CHECK(calls[0].r[0]==G(0x4b0000)&&calls[0].r[1]==OB+4);       /* EAX = vtable, EDX = buffer */
    c=make(0);CHECK(es2_asset_bitmap_read(&c,OB,S2,0,&o)==ES2_VFS_SKIP);
}

static void test_load_paths(void){
    es2_asset_host h=make(1),c=make(0);es2_asset_result o={0,0};
    /* A single bitmap: odd body, the drain reads the pad byte only. */
    build_guest();open_member(S,1);reset_calls();
    CHECK(es2_asset_dts_load(&h,S,0,&o)==ES2_VFS_DONE&&o.eax==HEAP&&r32(0,S+ES2_VFS_S_POS)==24);
    CHECK(r32(0,HEAP)==G(0x4a306c)&&arena[HEAP-OBJ+9]==0x1f&&r32(0,HEAP+0xa)==2);
    /* An unregistered tag: the stream stays readable -> PASS / SKIP, nothing written. */
    build_guest();open_member(S,2);uint8_t before[0x30];memcpy(before,host(0,S),0x30);
    CHECK(es2_asset_dts_load(&h,S,0,&o)==ES2_VFS_PASS&&!memcmp(before,host(0,S),0x30)&&r32(0,BS+ES2_VFS_B_BASE)==block[2]+9);
    CHECK(es2_asset_dts_load(&c,S,0,&o)==ES2_VFS_SKIP);
    /* ... at the member end (status 2 after the header): modeled, result 0. */
    w32(0,S+ES2_VFS_S_POS,length[2]-4);w32(0,S+ES2_VFS_S_APOS,block[2]+9+length[2]-4);w32(0,BS+ES2_VFS_B_BASE,block[2]+9+length[2]-4);
    CHECK(es2_asset_dts_load(&h,S,0,&o)==ES2_VFS_DONE&&o.eax==0&&r32(0,S+4)==2&&r32(0,S+ES2_VFS_S_POS)==length[2]+1);
    /* Status already set: tag stays -1, no reads. */
    CHECK(es2_asset_dts_load(&h,S,0,&o)==ES2_VFS_DONE&&o.eax==0&&r32(0,S+ES2_VFS_S_POS)==length[2]+1);
    CHECK(es2_asset_dts_load(&c,S,0,&o)==ES2_VFS_DONE&&o.eax==0&&expect.count==1&&expect.objects[0].kind==ES2_ASSET_NONE);
    /* The fallback registry in use: PASS. */
    w32(0,G(0x4a1694),1);CHECK(es2_asset_dts_load(&h,S,0,&o)==ES2_VFS_PASS);w32(0,G(0x4a1694),0);
    /* A class the core does not model: its factory is called (commit);
     * compare skips. */
    build_guest();open_member(S,3);reset_calls();
    CHECK(es2_asset_dts_load(&h,S,5,&o)==ES2_VFS_DONE&&o.eax==0xcafe0000u&&calls[0].f==G(0x4820d4)&&r32(0,S+ES2_VFS_S_POS)==10);
    build_guest();open_member(S,3);CHECK(es2_asset_dts_load(&c,S,5,&o)==ES2_VFS_SKIP);
    /* Another stream class: PASS. */
    build_guest();memset(host(0,S2),0,0x30);w32(0,S2,G(0x4b0000));
    CHECK(es2_asset_dts_load(&h,S2,0,&o)==ES2_VFS_PASS);
    /* The arena path: [6c5830] with room for the array and its pointers,
     * then new for the rest. */
    build_guest();open_member(S,0);reset_calls();
    w32(0,G(0x6c5830),AR);w32(0,AR,AMEM);w32(0,AR+4,0x14);w32(0,AR+8,0);
    CHECK(es2_asset_dts_load(&h,S,0,&o)==ES2_VFS_DONE&&o.eax==AMEM&&r32(0,AMEM+8)==AMEM+0xc&&r32(0,AR+8)==0x14);
    CHECK(count_calls(0x4774f0)==5&&r32(0,AMEM+0xc)==HEAP);
}

/* The memory stream class 4a1788 (+4 status, +8 start, +0Ch end,
 * +10h cursor) and dts_load's PASS reasons. */
#define MS (OBJ+0x480)         /* a memory stream */
#define MEMB (OBJ+0x1900)      /* its bytes */
static unsigned reasons[8],observed_reads,observed_tells;
static void note(void *c,uint32_t f){
    (void)c;
    if(f<8)++reasons[f];
    else if(f==0x47b4b4u)++observed_reads;
    else if(f==0x47b400u)++observed_tells;
}
static void open_memory(uint32_t s,const uint8_t *bytes,uint32_t n,uint32_t cursor,uint32_t end){
    memset(host(0,s),0xcc,0x14);memcpy(host(0,MEMB),bytes,n);
    w32(0,s,G(0x4a1788));w32(0,s+4,0);w32(0,s+8,MEMB);w32(0,s+0xc,MEMB+end);w32(0,s+0x10,MEMB+cursor);
}
static void test_memory(void){
    es2_asset_host h=make(1),c=make(0);es2_asset_result o={0,0};
    h.vfs.observe=note;c.vfs.observe=note;
    /* The array member from memory: the same objects as from the archive. */
    uint8_t bytes[128];const uint32_t n=(uint32_t)member_array(bytes);
    build_guest();open_memory(MS,bytes,n,0,n);reset_calls();memset(reasons,0,sizeof(reasons));observed_reads=observed_tells=0;
    CHECK(es2_asset_dts_load(&h,MS,0x77,&o)==ES2_VFS_DONE&&o.eax==HEAP);
    CHECK(count_calls(0x4774f0)==7&&calls[0].f==G(0x482d88)&&calls[0].a[0]==MS);
    const uint32_t b1=r32(0,HEAP+8),c1=r32(0,b1),c2=r32(0,b1+4);
    CHECK(r32(0,HEAP+4)==2&&r16(0,c1+4)==2&&r16(0,c1+6)==3&&arena[c1-OBJ+9]==0x15&&r32(0,c1+0xa)==6&&r16(0,c1+0x14)==1);
    CHECK(!memcmp(host(0,r32(0,c1+0xe)),bytes+33,6)&&r32(0,r32(0,c1+0x16))==0x11223344u&&arena[r32(0,c2+0xe)-OBJ]==0x77);
    /* The cursor at the end (the first child's pad byte drained), status 0,
     * nothing PASSed; the tells and reads of the top load are own code. */
    CHECK(r32(0,MS+0x10)==MEMB+66&&r32(0,MS+4)==0&&r32(0,COUNTER)==100&&r32(0,G(0x6c5964))==0x13572468u);
    CHECK(!reasons[1]&&!reasons[2]&&!reasons[3]&&!reasons[4]&&!reasons[5]&&observed_tells==3&&observed_reads==1);
    /* The bitmap read alone from memory, cut short in its pixels: status 2,
     * EAX 0, the bytes up to the end copied, the colours not read. */
    uint8_t bm[32];member_bitmap(bm);
    build_guest();open_memory(MS,bm,25,8,22);memset(host(0,OB),0,0x1a);w32(0,OB,G(0x4a306c));reset_calls();
    CHECK(es2_asset_bitmap_read(&h,OB,MS,0,&o)==ES2_VFS_DONE&&o.eax==0&&r32(0,MS+4)==2&&r32(0,MS+0x10)==MEMB+22);
    CHECK(r16(0,OB+4)==1&&r16(0,OB+6)==2&&r32(0,OB+0xa)==2&&arena[r32(0,OB+0xe)-OBJ]==0x55&&r32(0,OB+0x16)==0);
    /* Whole: EAX = the last buffer with AL 1. */
    build_guest();open_memory(MS,bm,25,8,25);memset(host(0,OB),0,0x1a);reset_calls();
    CHECK(es2_asset_bitmap_read(&h,OB,MS,0,&o)==ES2_VFS_DONE&&o.eax==((r32(0,OB+0xe)&~0xffu)|1u)&&r32(0,MS+0x10)==MEMB+23);
    /* Status set: nothing read, EAX 0. */
    build_guest();open_memory(MS,bm,25,8,25);w32(0,MS+4,1);memset(host(0,OB),0,0x1a);reset_calls();
    CHECK(es2_asset_bitmap_read(&h,OB,MS,0x41000000u,&o)==ES2_VFS_DONE&&o.eax==0&&r32(0,MS+0x10)==MEMB+8&&r16(0,OB+0x12)==0x41);
    /* Overlapping ranges: the read is the stream's virtual call. */
    build_guest();open_memory(MS,bm,25,8,25);w32(0,G(0x4a1788)+0x18,G(0x47b4b4));reset_calls();
    CHECK(es2_asset_bitmap_read(&h,MEMB+5,MS,0,&o)==ES2_VFS_DONE&&calls[0].f==G(0x47b4b4)&&calls[0].a[1]==2&&calls[0].a[2]==MEMB+9);
    /* dts_load: a cursor past the end is not modeled; a tag the registry
     * lacks with bytes left: PASS (tag); fewer than 8 bytes left: status 2,
     * modeled; compare skips the memory stream. */
    uint8_t unknown[16];member_unknown(unknown);memset(reasons,0,sizeof(reasons));
    build_guest();open_memory(MS,bm,25,9,8);
    CHECK(es2_asset_dts_load(&h,MS,0,&o)==ES2_VFS_PASS&&reasons[ES2_ASSET_PASS_UNSERVED]==1&&r32(0,MS+0x10)==MEMB+9);
    open_memory(MS,unknown,12,0,12);
    CHECK(es2_asset_dts_load(&h,MS,0,&o)==ES2_VFS_PASS&&reasons[ES2_ASSET_PASS_TAG]==1&&r32(0,MS+0x10)==MEMB);
    open_memory(MS,unknown,12,0,6);
    CHECK(es2_asset_dts_load(&h,MS,0,&o)==ES2_VFS_DONE&&o.eax==0&&r32(0,MS+4)==2&&r32(0,MS+0x10)==MEMB+6);
    open_memory(MS,bytes,n,0,n);
    CHECK(es2_asset_dts_load(&c,MS,0,&o)==ES2_VFS_SKIP&&r32(0,MS+0x10)==MEMB);
    CHECK(reasons[1]+reasons[2]+reasons[3]+reasons[4]+reasons[5]==2);
}
static void test_reasons(void){
    es2_asset_host h=make(1),c=make(0);es2_asset_result o={0,0};
    h.vfs.observe=note;c.vfs.observe=note;memset(reasons,0,sizeof(reasons));
    /* One key per PASS in replaced mode, none in compare mode. */
    build_guest();memset(host(0,S2),0,0x30);w32(0,S2,G(0x4b0000));
    CHECK(es2_asset_dts_load(&h,S2,0,&o)==ES2_VFS_PASS&&reasons[ES2_ASSET_PASS_STREAM]==1);
    CHECK(es2_asset_dts_load(&c,S2,0,&o)==ES2_VFS_SKIP&&reasons[ES2_ASSET_PASS_STREAM]==1);
    build_guest();open_member(S,1);w16(0,S+8,7);
    CHECK(es2_asset_dts_load(&h,S,0,&o)==ES2_VFS_PASS&&reasons[ES2_ASSET_PASS_LOOSE]==1);
    build_guest();open_member(S,1);w32(0,BS+ES2_VFS_B_STATUS,1);
    CHECK(es2_asset_dts_load(&h,S,0,&o)==ES2_VFS_PASS&&reasons[ES2_ASSET_PASS_UNSERVED]==1);
    build_guest();open_member(S,2);
    CHECK(es2_asset_dts_load(&h,S,0,&o)==ES2_VFS_PASS&&reasons[ES2_ASSET_PASS_TAG]==1);
    w32(0,G(0x4a1694),1);
    CHECK(es2_asset_dts_load(&h,S,0,&o)==ES2_VFS_PASS&&reasons[ES2_ASSET_PASS_FALLBACK]==1&&reasons[ES2_ASSET_PASS_TAG]==1);
    /* The fallback registry decides before the status: at the member end too. */
    w32(0,S+ES2_VFS_S_POS,length[2]-4);w32(0,S+ES2_VFS_S_APOS,block[2]+9+length[2]-4);w32(0,BS+ES2_VFS_B_BASE,block[2]+9+length[2]-4);
    CHECK(es2_asset_dts_load(&h,S,0,&o)==ES2_VFS_PASS&&reasons[ES2_ASSET_PASS_FALLBACK]==2);
    CHECK(es2_asset_dts_load(&c,S,0,&o)==ES2_VFS_SKIP);
    w32(0,G(0x4a1694),0);
    CHECK(reasons[1]==1&&reasons[2]==1&&reasons[3]==1&&reasons[4]==1&&reasons[5]==2&&!reasons[0]&&!reasons[6]);
}

static void test_destroy(void){
    es2_asset_host h=make(1),c=make(0);es2_asset_result o={0,0};
    build_guest();open_member(S,0);reset_calls();
    CHECK(es2_asset_stream_destroy(&h,S,2,0x1111,&o)==ES2_VFS_DONE);
    CHECK(o.eax==((COUNTER&~0xffu)|1u)&&o.edx==G(0x4a16e8)&&r32(0,COUNTER)==98&&r32(0,S)==G(0x4a16e8));
    CHECK(call_count==1&&calls[0].f==G(0x47307c)&&r32(0,S+4)==5&&r32(0,S+ES2_VFS_S_ARCHIVE)==0);
    build_guest();open_member(S,0);reset_calls();
    CHECK(es2_asset_stream_destroy(&c,S,2,0x1111,&o)==ES2_VFS_DONE&&call_count==0&&r32(0,COUNTER)==98&&r32(0,S+4)==5);
    /* The delete flag, the decoders. */
    build_guest();open_member(S,0);w32(0,S+0x27,OBJ+0x1800);w32(0,OBJ+0x1800,OBJ+0x1840);w32(0,OBJ+0x1840,G(0x4b1000));reset_calls();
    CHECK(es2_asset_stream_destroy(&h,S,3,0x1111,&o)==ES2_VFS_DONE&&o.eax==0x5150u&&o.edx==0x6160u);
    CHECK(call_count==5&&calls[0].f==G(0x496406)&&calls[1].f==G(0x4b1000)&&calls[1].a[1]==3&&calls[2].f==G(0x47307c)&&calls[3].f==G(0x47b5cc)&&calls[4].f==G(0x477548));
    CHECK(r32(0,COUNTER)==105);
    CHECK(es2_asset_stream_destroy(&c,S,2,0,&o)==ES2_VFS_SKIP);
    /* No stream: the counter only; EAX = FS:0, EDX = the counter pointer. */
    CHECK(es2_asset_stream_destroy(&h,0,2,0x2222,&o)==ES2_VFS_DONE&&o.eax==0x2222&&o.edx==COUNTER&&r32(0,COUNTER)==104);
}

int main(void){
    image=calloc(1,0x2d8000);
    if(!image)return 1;
    build_file();
    test_fetch();test_load_array();test_bitmap_read();test_load_paths();test_memory();test_reasons();test_destroy();
    printf("asset_load: %u checks passed\n",checks);
    free(image);
    return 0;
}
