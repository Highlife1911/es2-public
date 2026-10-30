#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/vfs_stream.h"
/* Synthetic guest for the stream functions: one parsed archive file
 * (built here), the original's archive object A with copies of its tables,
 * its buffered stream B, the manager M and stream objects. Guest calls go
 * to a small model: the runtime's _open/_read/_lseek/_filelength/_close
 * and, for the nested hooks, the core functions themselves. Expected values
 * follow the contract in file_io.md, not the implementation. */
#define B0 UINT32_C(0x00400000)
#define OBJ UINT32_C(0x90000000)
static uint8_t arena[0x4000], image_data[0x400];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"vfs_stream line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
/* Image globals: the manager pointer 4a0d18, the counter pointer 6cbee4. */
#define G_MANAGER (B0+0xa0d18u)
#define G_COUNTER (B0+0x2cbee4u)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a==G_MANAGER)return image_data;
    if(a==G_COUNTER)return image_data+4;
    if(a==B0+0xa0e10u)return image_data+8;
    fprintf(stderr,"vfs_stream: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};

/* Layout in the arena. */
#define A (OBJ+0x100)          /* archive object */
#define BS (OBJ+0x140)         /* its buffered stream */
#define DIRS (OBJ+0x180)       /* directory pointer array */
#define BLOB (OBJ+0x1a0)       /* directory strings */
#define RECS (OBJ+0x200)       /* member records */
#define M (OBJ+0x300)          /* manager */
#define S (OBJ+0x400)          /* stream */
#define PATH (OBJ+0x500)
#define BUF (OBJ+0x600)
#define D7 (OBJ+0x800)
static uint8_t file[512];
static size_t file_size;
static es2_vol vol;
static es2_vfs_archive archive_entry;
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
/* "VOLN", mask 1, priority 5, dirs "dts\" and "DAT\", members ONE.DTS (4
 * bytes 10..13), TWO.BIN (2 bytes) in dat, THREE (1 byte) in dts. */
static void build_file(void){
    memset(file,0,sizeof(file));
    memcpy(file,"VOLN",4);put32(file+4,1);file[8]=5;file[9]=2;file[10]=10;
    memcpy(file+12,"dts\\\0DAT\\\0",10);
    size_t t=22;file[t]=3;put32(file+t+2,54);t+=6;
    const char *names[3]={"ONE.DTS","TWO.BIN","THREE"};const uint8_t dirs[3]={0,1,0};const uint32_t lens[3]={4,2,1};
    size_t block=t+54;
    for(unsigned i=0;i<3;++i){
        uint8_t *slot=file+t+18*i;memcpy(slot,names[i],strlen(names[i]));slot[13]=dirs[i];put32(slot+14,(uint32_t)block);
        file[block]=2;put32(file+block+1,lens[i]);put32(file+block+5,0xabcd0000u+i);
        for(uint32_t k=0;k<lens[i];++k)file[block+9+k]=(uint8_t)(0x10*(i+1)+k);
        file[block+9+lens[i]]=file[block+8+lens[i]];
        block+=10+lens[i];
    }
    file_size=block;
    CHECK(es2_vol_open((es2_span){file,file_size},&vol,NULL)==ES2_OK);
}
static uint32_t block_of(unsigned i){return r32(0,RECS+18u*i+14u);}
static void build_guest(void){
    memset(arena,0,sizeof(arena));memset(image_data,0,sizeof(image_data));
    w32(0,A+4,BS);arena[A-OBJ+0x1c]=5;arena[A-OBJ+0x1d]=2;w16(0,A+0x1e,3);
    w32(0,A+0x24,DIRS);w32(0,A+0x2c,RECS);
    memcpy(arena+(BLOB-OBJ),file+12,10);w32(0,DIRS,BLOB);w32(0,DIRS+4,BLOB+5);
    memcpy(arena+(RECS-OBJ),file+28,54);
    w32(0,M,A);arena[M-OBJ+0x78]=1;w32(0,G_MANAGER,M);w32(0,G_COUNTER,OBJ+0xf00);w32(0,OBJ+0xf00,100);
    w32(0,BS+ES2_VFS_B_BASE,0x37);w32(0,BS+ES2_VFS_B_CURSOR,3);   /* tell 3Ah */
    w32(0,S,B0+0xa0e10u);w16(0,S+8,0xffff);w32(0,S+4,5);
    archive_entry=(es2_vfs_archive){A,BS,&vol};
}
static const es2_vfs_archive *find(void *c,uint32_t a,uint32_t b){
    (void)c;
    if(a)return a==A&&es2_vfs_stream_archive_matches(&memory,A,&vol)?&archive_entry:NULL;
    return b==BS?&archive_entry:NULL;
}
typedef struct logged{uint32_t f,a[3];unsigned n;}logged;
static logged calls[32];static unsigned call_count;
static uint32_t rtl_open_result,rtl_read_result,rtl_lseek_result,rtl_close_result,decoder_result;
static es2_vfs_host make(int commit);
static void guest(void *c,uint32_t f,const uint32_t *a,unsigned n,uint32_t r[3]){
    (void)c;CHECK(call_count<32);logged *l=calls+call_count++;l->f=f;l->n=n;memcpy(l->a,a,4u*(n<3?n:3));
    es2_vfs_host h=make(1);es2_vfs_result o={0,0};
    switch(f-B0+0x400000u){
    case 0x4967c6:r[0]=rtl_open_result;break;
    case 0x496808:r[0]=rtl_read_result;if(rtl_read_result!=UINT32_MAX)memset(host(0,a[1]),0x5a,rtl_read_result);break;
    case 0x496826:r[0]=rtl_lseek_result;break;
    case 0x496856:r[0]=0x1234;break;
    case 0x496874:r[0]=rtl_close_result;break;
    case 0x47307c:CHECK(es2_vfs_stream_close(&h,a[0],&o)==ES2_VFS_DONE);r[0]=o.eax;break;
    case 0x468064:CHECK(es2_vfs_stream_lookup(&h,a[0],a[1],a[2],&o)==ES2_VFS_DONE);r[0]=o.eax;break;
    case 0x47cdf8:CHECK(es2_vfs_stream_member_lookup(&h,a[0],a[1],a[2],&o)==ES2_VFS_DONE);r[0]=o.eax;break;
    case 0x47749c:r[0]=D7;break;
    default:r[0]=decoder_result;break;
    }
}
static int loose_answer;
static int loose(void *c,uint32_t p){(void)c;(void)p;return loose_answer;}
static es2_vfs_host make(int commit){es2_vfs_host h={&memory,B0,commit,0,find,guest,loose,0,NULL};return h;}
static void reset_calls(void){call_count=0;memset(calls,0,sizeof(calls));}
static void set_path(const char *p){memset(arena+(PATH-OBJ),0,0x100);strcpy((char *)arena+(PATH-OBJ),p);}
static uint32_t tell_b(void){return r32(0,BS+ES2_VFS_B_BASE)+r32(0,BS+ES2_VFS_B_CURSOR);}

static void test_matches(void){
    build_guest();
    CHECK(es2_vfs_stream_archive_matches(&memory,A,&vol));
    arena[RECS-OBJ+20]^=1;CHECK(!es2_vfs_stream_archive_matches(&memory,A,&vol));arena[RECS-OBJ+20]^=1;
    arena[BLOB-OBJ+6]='a';CHECK(!es2_vfs_stream_archive_matches(&memory,A,&vol));arena[BLOB-OBJ+6]='A';
    arena[A-OBJ+0x1c]=6;CHECK(!es2_vfs_stream_archive_matches(&memory,A,&vol));arena[A-OBJ+0x1c]=5;
    w32(0,A+4,0);CHECK(!es2_vfs_stream_archive_matches(&memory,A,&vol));w32(0,A+4,BS);
    CHECK(es2_vfs_stream_archive_matches(&memory,A,&vol));
}

static size_t ensured[4][2];static unsigned ensure_count;
static void ensure(void *c,const es2_vfs_archive *arc,size_t offset,size_t length){
    (void)c;CHECK(arc==&archive_entry&&ensure_count<4);ensured[ensure_count][0]=offset;ensured[ensure_count++][1]=length;
}
static void test_member_lookup(void){
    es2_vfs_host h=make(1);es2_vfs_result o={0,0};
    build_guest();set_path("DAT\\two.bin");
    const uint32_t R=S+ES2_VFS_S_ARCHIVE;
    CHECK(es2_vfs_stream_member_lookup(&h,A,PATH,R,&o)==ES2_VFS_DONE);
    const uint32_t blk=block_of(1);
    CHECK(o.eax==1&&r32(0,R)==BS&&r32(0,R+8)==blk+9&&r32(0,R+0x10)==blk+9&&r32(0,R+0x14)==2&&r32(0,R+0x18)==2);
    CHECK(r32(0,R+4)==0&&r32(0,R+0xc)==0);                      /* not written */
    CHECK(tell_b()==blk+9&&r32(0,BS+ES2_VFS_B_VALID)==0&&r32(0,BS+ES2_VFS_B_STATUS)==0);
    /* Not found: neither the record nor B changes. */
    build_guest();set_path("dts\\two.bin");
    CHECK(es2_vfs_stream_member_lookup(&h,A,PATH,R,&o)==ES2_VFS_DONE&&o.eax==0&&r32(0,R+8)==0&&tell_b()==0x3a);
    set_path("nodir\\one.dts");
    CHECK(es2_vfs_stream_member_lookup(&h,A,PATH,R,&o)==ES2_VFS_DONE&&o.eax==0);
    CHECK(es2_vfs_stream_member_lookup(&h,A,0,R,&o)==ES2_VFS_DONE&&o.eax==0);
    /* B in an error state (status 3): not modeled. */
    set_path("dts\\one.dts");w32(0,BS+4,3);
    CHECK(es2_vfs_stream_member_lookup(&h,A,PATH,R,&o)==ES2_VFS_PASS&&r32(0,R+8)==0);
    w32(0,BS+4,2);CHECK(es2_vfs_stream_member_lookup(&h,A,PATH,R,&o)==ES2_VFS_DONE&&o.eax==1&&r32(0,BS+4)==0);
    /* Unknown archive: pass (commit) or skip (compare). */
    CHECK(es2_vfs_stream_member_lookup(&h,A+8,PATH,R,&o)==ES2_VFS_PASS);
    es2_vfs_host c=make(0);CHECK(es2_vfs_stream_member_lookup(&c,A+8,PATH,R,&o)==ES2_VFS_SKIP);
    /* Compare: B is not written. */
    build_guest();set_path("dts\\THREE");
    CHECK(es2_vfs_stream_member_lookup(&c,A,PATH,R,&o)==ES2_VFS_DONE&&o.eax==1&&r32(0,R+0x18)==1&&tell_b()==0x3a);
    /* The host's ensure: the prefix, then the payload and its suffix byte; nothing when not found. */
    build_guest();set_path("DAT\\two.bin");h.ensure=ensure;ensure_count=0;
    CHECK(es2_vfs_stream_member_lookup(&h,A,PATH,R,&o)==ES2_VFS_DONE&&o.eax==1);
    CHECK(ensure_count==2&&ensured[0][0]==blk&&ensured[0][1]==9&&ensured[1][0]==blk+9&&ensured[1][1]==3);
    set_path("dts\\two.bin");ensure_count=0;
    CHECK(es2_vfs_stream_member_lookup(&h,A,PATH,R,&o)==ES2_VFS_DONE&&o.eax==0&&ensure_count==0);
}

static void test_open(void){
    es2_vfs_host h=make(1);es2_vfs_result o={0,0};
    /* Member open: close, _open fails, lookup -> member_lookup, kind 2. */
    build_guest();set_path("dts\\one.dts");reset_calls();rtl_open_result=UINT32_MAX;
    w32(0,S+ES2_VFS_S_POS,77);
    CHECK(es2_vfs_stream_open(&h,S,PATH,&o)==ES2_VFS_DONE&&o.eax==1);
    CHECK(call_count==4&&calls[0].f==0x47307c&&calls[1].f==0x4967c6&&calls[1].a[0]==PATH&&calls[1].a[1]==0x8000);
    CHECK(calls[2].f==0x468064&&calls[2].a[0]==M&&calls[2].a[2]==S+0xa&&calls[3].f==0x47cdf8&&calls[3].a[0]==A);
    CHECK(r32(0,S+4)==0&&arena[S-OBJ+0x26]==1&&r16(0,S+8)==0xffff&&r32(0,S+0xa)==BS&&r32(0,S+0xe)==BS);
    CHECK(r32(0,S+0x16)==0&&r32(0,S+0x12)==block_of(0)+9&&r32(0,S+0x22)==4&&r32(0,S+0x1e)==2);
    /* Not found: status 1, fields zeroed. */
    set_path("dts\\none");reset_calls();
    CHECK(es2_vfs_stream_open(&h,S,PATH,&o)==ES2_VFS_DONE&&o.eax==0&&r32(0,S+4)==1&&r32(0,S+0xa)==0&&r32(0,S+0xe)==0);
    /* No manager. */
    w32(0,G_MANAGER,0);reset_calls();
    CHECK(es2_vfs_stream_open(&h,S,PATH,&o)==ES2_VFS_DONE&&o.eax==0&&call_count==2);
    w32(0,G_MANAGER,M);
    /* Loose: the runtime handle, EAX = handle with AL 1. */
    rtl_open_result=0x00000107u;reset_calls();
    CHECK(es2_vfs_stream_open(&h,S,PATH,&o)==ES2_VFS_DONE&&o.eax==0x101&&r16(0,S+8)==0x107&&r32(0,S+4)==0&&call_count==2);
    /* Kind 7 (decoder 7): new(92h), ctor, counter -2, init(d, archive). */
    build_guest();set_path("dts\\one.dts");rtl_open_result=UINT32_MAX;
    file[block_of(0)]=7;decoder_result=0x5555aa00u;reset_calls();
    CHECK(es2_vfs_stream_open(&h,S,PATH,&o)==ES2_VFS_DONE&&o.eax==0x5555aa01u);
    CHECK(call_count==7&&calls[4].f==0x47749c&&calls[4].a[0]==0x92&&calls[5].f==0x47b50c&&calls[5].a[0]==D7);
    CHECK(calls[6].f==0x47b594&&calls[6].a[0]==D7&&calls[6].a[1]==BS&&r32(0,S+0x27)==D7&&r32(0,S+0xe)==D7);
    CHECK(r32(0,OBJ+0xf00)==98);
    /* Another kind: EAX = kind-9 with AL 1. */
    file[block_of(0)]=4;reset_calls();
    CHECK(es2_vfs_stream_open(&h,S,PATH,&o)==ES2_VFS_DONE&&o.eax==0xffffff01u&&r32(0,S+0xe)==0);
    file[block_of(0)]=2;
    /* Compare: a loose file skips; otherwise the inline path. */
    es2_vfs_host c=make(0);build_guest();set_path("dts\\one.dts");loose_answer=1;
    CHECK(es2_vfs_stream_open(&c,S,PATH,&o)==ES2_VFS_SKIP);
    loose_answer=0;reset_calls();
    CHECK(es2_vfs_stream_open(&c,S,PATH,&o)==ES2_VFS_DONE&&o.eax==1&&call_count==0&&r32(0,S+0x22)==4&&tell_b()==0x3a);
}

static void test_read(void){
    es2_vfs_host h=make(1);es2_vfs_result o={0,0};
    build_guest();set_path("dts\\one.dts");rtl_open_result=UINT32_MAX;
    CHECK(es2_vfs_stream_open(&h,S,PATH,&o)==ES2_VFS_DONE);
    const uint32_t start=block_of(0)+9;
    /* In order (B at the member start): 3 bytes. */
    memset(arena+(BUF-OBJ),0xee,16);reset_calls();
    CHECK(es2_vfs_stream_read(&h,S,3,BUF,0,&o)==ES2_VFS_DONE&&o.eax==((start+3)&~0xffu)+1u&&call_count==0);
    CHECK(arena[BUF-OBJ]==0x10&&arena[BUF-OBJ+2]==0x12&&arena[BUF-OBJ+3]==0xee&&r32(0,S+0x16)==3&&r32(0,S+0x1a)==start+3);
    /* Moved B (another stream): resync to the position, then 1 byte. */
    w32(0,BS+ES2_VFS_B_BASE,5);w32(0,BS+ES2_VFS_B_CURSOR,0);
    CHECK(es2_vfs_stream_read(&h,S,1,BUF,0,&o)==ES2_VFS_DONE&&arena[BUF-OBJ]==0x13&&r32(0,S+0x16)==4&&tell_b()==start+4);
    /* At the end (pos == length, B in place): the suffix byte, status 2, EAX 0. */
    CHECK(es2_vfs_stream_read(&h,S,2,BUF,0,&o)==ES2_VFS_DONE&&o.eax==0&&arena[BUF-OBJ]==0x13&&r32(0,S+4)==2);
    CHECK(r32(0,S+0x16)==5&&r32(0,S+0x1a)==start+5);
    /* Status set: EAX 0, nothing read. */
    CHECK(es2_vfs_stream_read(&h,S,1,BUF,0,&o)==ES2_VFS_DONE&&o.eax==0);
    /* pos == length after a read, B moved: the resync seek's bug reads the
     * file byte at offset `length` (4) and leaves status 2. */
    w32(0,S+4,0);w32(0,S+0x16,4);w32(0,S+0x1a,start+4);w32(0,BS+ES2_VFS_B_BASE,9);
    CHECK(es2_vfs_stream_read(&h,S,1,BUF,0,&o)==ES2_VFS_DONE&&o.eax==0&&arena[BUF-OBJ]==file[4]&&r32(0,S+0x16)==5);
    CHECK(r32(0,S+0x1a)==5&&r32(0,S+4)==2&&tell_b()==5);
    /* Size 0: the entry EAX with AL 1. */
    w32(0,S+4,0);
    CHECK(es2_vfs_stream_read(&h,S,0,BUF,0x12345678u,&o)==ES2_VFS_DONE&&o.eax==0x12345601u);
    /* Compare: differences counted, buffer and B untouched. */
    es2_vfs_host c=make(0);w32(0,S+0x16,0);w32(0,S+0x1a,start);w32(0,BS+ES2_VFS_B_BASE,start);w32(0,BS+ES2_VFS_B_CURSOR,0);
    arena[BUF-OBJ]=0x10;arena[BUF-OBJ+1]=0x99;
    CHECK(es2_vfs_stream_read(&c,S,2,BUF,0,&o)==ES2_VFS_DONE&&o.differences==1&&arena[BUF-OBJ+1]==0x99&&tell_b()==start);
    /* Loose: _read(handle, buf, size). */
    w32(0,S+4,0);w16(0,S+8,7);rtl_read_result=2;reset_calls();
    CHECK(es2_vfs_stream_read(&h,S,2,BUF,0,&o)==ES2_VFS_DONE&&o.eax==1&&calls[0].f==0x496808&&calls[0].a[0]==7&&calls[0].a[2]==2);
    rtl_read_result=1;CHECK(es2_vfs_stream_read(&h,S,2,BUF,0,&o)==ES2_VFS_DONE&&o.eax==0&&r32(0,S+4)==2);
    w32(0,S+4,0);rtl_read_result=UINT32_MAX;
    CHECK(es2_vfs_stream_read(&h,S,2,BUF,0,&o)==ES2_VFS_DONE&&o.eax==0&&r32(0,S+4)==1);
    w32(0,S+4,0);CHECK(es2_vfs_stream_read(&c,S,2,BUF,0,&o)==ES2_VFS_SKIP);
}

static void test_seek_tell_size_close(void){
    es2_vfs_host h=make(1);es2_vfs_result o={0,0};
    build_guest();set_path("dts\\one.dts");rtl_open_result=UINT32_MAX;
    CHECK(es2_vfs_stream_open(&h,S,PATH,&o)==ES2_VFS_DONE);
    const uint32_t start=block_of(0)+9;
    CHECK(es2_vfs_stream_seek(&h,S,2,0,&o)==ES2_VFS_DONE&&(uint8_t)o.eax==1&&r32(0,S+0x16)==2&&r32(0,S+0x1a)==start+2&&tell_b()==start+2);
    CHECK(es2_vfs_stream_tell(&h,S,&o)==ES2_VFS_DONE&&o.eax==2);
    CHECK(es2_vfs_stream_size(&h,S,&o)==ES2_VFS_DONE&&o.eax==4);
    /* Past the end: S+1Ah = length, status 2; the next seek resets it. */
    CHECK(es2_vfs_stream_seek(&h,S,4,0,&o)==ES2_VFS_DONE&&(uint8_t)o.eax==1&&r32(0,S+0x1a)==4&&r32(0,S+4)==2);
    CHECK(es2_vfs_stream_seek(&h,S,1,0xabcd0000u,&o)==ES2_VFS_DONE&&r32(0,S+4)==0&&r32(0,S+0x1a)==start+1);
    /* Error status: EAX 0. No source: AL 1 over AX = FFFFh. */
    w32(0,S+4,3);CHECK(es2_vfs_stream_seek(&h,S,1,0,&o)==ES2_VFS_DONE&&o.eax==0);
    w32(0,S+4,0);w32(0,S+0xe,0);
    CHECK(es2_vfs_stream_seek(&h,S,1,0xabcd1234u,&o)==ES2_VFS_DONE&&o.eax==0xabcdff01u);
    w32(0,S+4,2);CHECK(es2_vfs_stream_seek(&h,S,1,0xabcd1234u,&o)==ES2_VFS_DONE&&o.eax==0x0000ff01u&&r32(0,S+4)==0);
    CHECK(es2_vfs_stream_tell(&h,S,&o)==ES2_VFS_DONE&&o.eax==0);
    CHECK(es2_vfs_stream_size(&h,S,&o)==ES2_VFS_DONE&&o.eax==0);
    /* Other kinds: position stored, AL 1; kinds 7/9 not modeled. */
    w32(0,S+0xe,BS);w32(0,S+0x1e,5);
    CHECK(es2_vfs_stream_seek(&h,S,3,0x77770000u,&o)==ES2_VFS_DONE&&o.eax==0x7777ff01u&&r32(0,S+0x16)==3);
    w32(0,S+0x1e,7);CHECK(es2_vfs_stream_seek(&h,S,3,0,&o)==ES2_VFS_PASS);
    /* Loose: _lseek(h,pos,0), _lseek(h,0,1), _filelength(h). */
    w32(0,S+0x1e,2);w16(0,S+8,0xfff9);rtl_lseek_result=0x2ff;reset_calls();
    CHECK(es2_vfs_stream_seek(&h,S,9,0,&o)==ES2_VFS_DONE&&o.eax==0x301&&calls[0].a[0]==0xfffffff9u&&calls[0].a[1]==9&&calls[0].a[2]==0);
    rtl_lseek_result=UINT32_MAX;CHECK(es2_vfs_stream_seek(&h,S,9,0,&o)==ES2_VFS_DONE&&o.eax==0&&r32(0,S+4)==1);
    w32(0,S+4,0);rtl_lseek_result=0x55;CHECK(es2_vfs_stream_tell(&h,S,&o)==ES2_VFS_DONE&&o.eax==0x55);
    CHECK(es2_vfs_stream_size(&h,S,&o)==ES2_VFS_DONE&&o.eax==0x1234);
    es2_vfs_host c=make(0);
    CHECK(es2_vfs_stream_tell(&c,S,&o)==ES2_VFS_SKIP&&es2_vfs_stream_size(&c,S,&o)==ES2_VFS_SKIP&&es2_vfs_stream_seek(&c,S,1,0,&o)==ES2_VFS_SKIP);
    /* Close: an owned handle that fails to close leaves status 3. */
    arena[S-OBJ+0x26]=1;rtl_close_result=UINT32_MAX;reset_calls();
    CHECK(es2_vfs_stream_close(&h,S,&o)==ES2_VFS_DONE&&o.eax==0&&r32(0,S+4)==3&&r16(0,S+8)==0xfff9);
    CHECK(es2_vfs_stream_close(&c,S,&o)==ES2_VFS_SKIP);
    rtl_close_result=0;w32(0,S+0x27,D7);w32(0,S+0x2b,D7+0x40);reset_calls();
    CHECK(es2_vfs_stream_close(&h,S,&o)==ES2_VFS_DONE&&o.eax==(((S+0xa)&~0xffu)|1u)&&r32(0,S+4)==5&&r16(0,S+8)==0xffff);
    CHECK(call_count==3&&calls[1].f==0x47b5cc&&calls[1].a[0]==D7&&calls[2].f==0x47b9cc&&calls[2].a[0]==D7+0x40);
    CHECK(r32(0,S+0xa)==0&&r32(0,S+0xe)==0&&r32(0,S+0x16)==0&&r32(0,S+0x27)==D7);
}

int main(void){
    build_file();
    test_matches();test_member_lookup();test_open();test_read();test_seek_tell_size_close();
    printf("vfs_stream: %u checks passed\n",checks);
    return 0;
}
