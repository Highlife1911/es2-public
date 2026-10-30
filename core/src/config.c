#include "es2/core/config.h"
#include "es2/formats/cfg.h"
#include "fixed.h"
/* Configuration readers and tape header records (docs/reverse-
 * engineering/file_io.md, section "configuration and tape files as own code").
 * Guest memory is read and written in the original's order; r holds what the
 * original's EAX/EDX/ECX hold at the next call; lo8/lo16 are the partial
 * register loads of the original. */
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define R16(a) m->read16(m->context,(a))
#define R32(a) m->read32(m->context,(a))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W32(a,v) m->write32(m->context,(a),(uint32_t)(v))
#define VA(x) (g->image+(uint32_t)(x)-0x400000u)
enum { K_FRAME=0x49630cu, K_PATH=0x45ee00u, K_RCTOR=0x47ad40u, K_READ=0x47af98u, K_RDTOR=0x47ad78u,
    K_WCTOR=0x47b064u, K_WRITE=0x47b2b8u, K_WDTOR=0x47b09cu, K_MEMSET=0x49681au, K_FOPEN=0x496802u,
    K_FREAD=0x49684au, K_FWRITE=0x496832u, K_FCLOSE=0x496838u, K_FSEEK=0x49682cu, K_FTELL=0x4967deu,
    K_REWIND=0x496772u, K_PROFILE=0x496910u, K_STRICMP=0x496868u, K_ATOL=0x496880u };
/* The prefs path (a 255-byte buffer filled by 45972c or the -p option) and
 * the three 54-byte prefs blocks with the callback table. */
#define PREFS_PATH 0x49e81cu
#define BLOCK 0x4d1f38u
#define COMMITTED 0x4d1f6eu
#define PREVIOUS 0x4d1fa4u
#define CALLBACKS 0x4d1fdcu
#define PREFS_SIZE ES2_PREFS_SIZE  /* 36h */
#define CHUNK ES2_TAPE_PIECE       /* 1F4h */
static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static uint32_t lo8(uint32_t reg,uint32_t v) { return (reg&0xffffff00u)|(uint8_t)v; }
static uint32_t lo16(uint32_t reg,uint32_t v) { return (reg&0xffff0000u)|(uint16_t)v; }
static uint32_t sx16(uint32_t v) { return (uint32_t)fx_s16(v); }
static void put_ecx(uint32_t *ecx,uint32_t v) { if(ecx)*ecx=v; }
static void note(const es2_weapon_graph *g,uint32_t key) { if(g->observe)g->observe(g->observer_context,key,0); }
/* A direct call at its original VA (observer key); the bridge gets the
 * address in this image. */
static void cf_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    note(g,va);
    g->call(g->context,VA(va),a,n,r);
}

/* Original: DBSIM.EXE 0x45e510. The 7-byte buffer is f-8..f-2; f-1 is
 * never written (the exit ECX is the dword f-4, the original's second
 * pop ecx). */
uint64_t es2_prefs_video_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t mode,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_CONFIG_FRAME,buf=f-8u;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t a[4];
    uint8_t bl=(uint8_t)mode;
    if(!R8(VA(0x49f254u))) {
        a[0]=VA(0x49f287u);cf_call(g,K_PATH,a,1,&r);        /* "data\prefs.cfg" */
        r.ecx=VA(0x49f287u);
        a[0]=r.eax;a[1]=VA(0x49f296u);cf_call(g,K_FOPEN,a,2,&r); /* "rb" */
        const uint32_t file=r.eax;
        if(file) {
            r.eax=buf;a[0]=buf;a[1]=1;a[2]=7;a[3]=file;cf_call(g,K_FREAD,a,4,&r);
            if(r.eax==7u) { bl=R8(f-4u);r.eax=R8(f-2u);W32(VA(0x4d255eu),r.eax); }
            a[0]=file;cf_call(g,K_FCLOSE,a,1,&r);
            r.ecx=file;
        }
        bl=bl==1u?0u:3u;
        W8(VA(0x49f254u),1);
    }
    /* 45e57f: mode 0 (320x240), 1, anything else (640x480). */
    if(bl==0) {
        W8(VA(0x4d2537u),0);W8(VA(0x4d256cu),0);W16(VA(0x4d2566u),1);
        W32(VA(0x4d253eu),0x140u);W32(VA(0x4d2542u),0xf0u);W32(VA(0x4d2546u),0x280u);
        W32(VA(0x4d254au),0x1e0u);W32(VA(0x4d254eu),0x140u);W32(VA(0x4d2552u),0x1e0u);
        r.eax=0;W16(VA(0x4d253cu),0);W16(VA(0x4d253au),0);
    } else {
        W8(VA(0x4d2537u),3);W8(VA(0x4d256cu),bl==1?0:1);W16(VA(0x4d2566u),1);
        W32(VA(0x4d253eu),0x280u);W32(VA(0x4d2542u),0x1e0u);W32(VA(0x4d2546u),0x500u);
        if(bl==1)r.edx=lo16(r.edx,1);else r.eax=lo16(r.eax,1);
        W32(VA(0x4d254au),0x3c0u);W32(VA(0x4d254eu),0x280u);W32(VA(0x4d2552u),0x3c0u);
        W16(VA(0x4d253cu),1);W16(VA(0x4d253au),1);
    }
    put_ecx(ecx,R32(f-4u));
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x459770. Frame: record f-2Ch (context word f-1Ch),
 * read stream S f-0Ch (status f-8). */
uint64_t es2_prefs_load_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t flag,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_CONFIG_FRAME,S=f-0xcu;
    es2_weapon_registers r={VA(0x49e92cu),entry[1],entry[2]};
    uint32_t a[3];
    cf_call(g,K_FRAME,NULL,0,&r);
    W16(f-0x1cu,8);
    r.edx=S;a[0]=S;a[1]=VA(PREFS_PATH);cf_call(g,K_RCTOR,a,2,&r);
    a[0]=VA(BLOCK);a[1]=0;a[2]=PREFS_SIZE;cf_call(g,K_MEMSET,a,3,&r);
    if(!R32(f-8u)) { r.ecx=S;a[0]=S;a[1]=PREFS_SIZE;a[2]=VA(BLOCK);cf_call(g,K_READ,a,3,&r); }
    /* 4597c4: per byte the callback (table entry read for the test and
     * again for the call), the committed copy (flag) and the previous copy. */
    uint32_t p=VA(BLOCK),q=VA(BLOCK),committed=VA(COMMITTED),previous=VA(PREVIOUS),table=VA(CALLBACKS);
    r.eax=0;
    for(unsigned i=0;i<PREFS_SIZE;++i,table+=4u,++p,++q,++committed,++previous) {
        if(R32(table)) {
            r.eax=p;r.edx=lo8(r.edx,R8(p));
            const uint32_t target=R32(table);
            a[0]=r.edx;note(g,ES2_CONFIG_CALLBACK);g->call(g->context,target,a,1,&r);
            r.ecx=a[0];
        }
        if((uint8_t)flag) { r.ecx=q;r.eax=lo8(r.eax,R8(q));W8(committed,r.eax); }
        r.edx=lo8(r.edx,R8(committed));W8(previous,r.edx);
    }
    r.ecx=S;a[0]=S;a[1]=2;cf_call(g,K_RDTOR,a,2,&r);
    r.eax=R32(f-0x2cu);
    put_ecx(ecx,r.ecx);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x459b94. Frame: buffer f-70h..f-3Bh, record f-38h
 * (context word f-28h), write stream W f-18h (status f-14h), read stream
 * S f-0Ch (status f-8). An index is stored at f-70h+index; 0..6Fh lie in
 * the frame (the buffer, the record, the streams) and are stored there,
 * any other index is not stored (ES2_CONFIG_UNMODELED). */
uint64_t es2_prefs_store_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t count,uint32_t list,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_CONFIG_FRAME,S=f-0xcu,W=f-0x18u,buf=f-0x70u;
    es2_weapon_registers r={VA(0x49e980u),entry[1],entry[2]};
    uint32_t a[3];
    cf_call(g,K_FRAME,NULL,0,&r);
    W16(f-0x28u,8);
    r.edx=S;a[0]=S;a[1]=VA(PREFS_PATH);cf_call(g,K_RCTOR,a,2,&r);
    if(!R32(f-8u)) {
        r.ecx=buf;r.eax=S;a[0]=S;a[1]=PREFS_SIZE;a[2]=buf;cf_call(g,K_READ,a,3,&r);
        if(R32(f-8u))note(g,ES2_CONFIG_RESIDUE);   /* short read: the rest of the buffer is residue */
        r.eax=0;
        for(uint32_t item=list;;item+=2u) {
            r.edx=sx16(count);
            if(!(fx_s32(r.eax)<fx_s32(r.edx)))break;
            r.edx=sx16(R16(item));
            r.ecx=lo8(r.ecx,R8(VA(BLOCK)+r.edx));
            if(r.edx<0x70u)W8(buf+r.edx,r.ecx);else note(g,ES2_CONFIG_UNMODELED);
            ++r.eax;
        }
    } else note(g,ES2_CONFIG_RESIDUE);                 /* no read: the whole buffer is residue */
    W16(f-0x28u,8);
    r.eax=W;a[0]=W;a[1]=VA(PREFS_PATH);cf_call(g,K_WCTOR,a,2,&r);
    if(!R32(f-0x14u)) { r.ecx=buf;r.eax=W;a[0]=W;a[1]=PREFS_SIZE;a[2]=buf;cf_call(g,K_WRITE,a,3,&r); }
    r.edx=W;a[0]=W;a[1]=2;cf_call(g,K_WDTOR,a,2,&r);
    r.ecx=S;a[0]=S;a[1]=2;cf_call(g,K_RDTOR,a,2,&r);
    r.eax=R32(f-0x38u);
    put_ecx(ecx,r.ecx);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x45b7a8. The strings are read from the image
 * through the table 49ebe0 (path +14h; per key: section, key, default,
 * "Reverse"); the 40-byte value buffer is f-28h. */
uint64_t es2_keyjoy_load_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    const uint32_t entry[3],uint32_t *ecx) {
    static const struct { uint8_t section,key,fallback,reverse; uint32_t flag; } keys[4]={
        {0x24,0x2b,0x30,0x31,0x49ea90u},{0x39,0x40,0x49,0x4a,0x49ea94u},
        {0x52,0x59,0x61,0x62,0x49ea98u},{0x6a,0x71,0x78,0x79,0x49ea9cu}};
    const uint32_t base=VA(0x49ebe0u),buf=g->scratch+ES2_CONFIG_FRAME-0x28u;
    es2_weapon_registers r={base+0x14u,entry[1],entry[2]};
    uint32_t a[6];
    a[0]=base+0x14u;cf_call(g,K_PATH,a,1,&r);   /* "data\keyjoy.cfg" */
    r.ecx=base+0x14u;
    const uint32_t path=r.eax;
    for(unsigned k=0;k<4;++k) {
        r.eax=base+keys[k].section;r.edx=base+keys[k].fallback;r.ecx=base+keys[k].key;
        a[0]=r.eax;a[1]=r.ecx;a[2]=r.edx;a[3]=buf;a[4]=0x28;a[5]=path;cf_call(g,K_PROFILE,a,6,&r);
        r.edx=base+keys[k].reverse;r.ecx=buf;
        a[0]=buf;a[1]=r.edx;cf_call(g,K_STRICMP,a,2,&r);
        if(!r.eax)W32(VA(keys[k].flag),1);
    }
    put_ecx(ecx,r.ecx);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x46379c. The strings: the table 49fa4b (path +0;
 * Driver +0Fh/+15h/+1Ch, "DirectSound" +1Dh; Buffers +29h/+2Fh/+37h; Rate
 * +38h/+3Eh/+43h; Width +44h/+4Ah/+50h, "Mono" +51h); the 40-byte value
 * buffer is f-28h. */
uint64_t es2_sound_config_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t o,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t d=VA(0x49fa4bu),buf=g->scratch+ES2_CONFIG_FRAME-0x28u;
    es2_weapon_registers r={0,0,0};
    uint32_t a[6];
    (void)entry;
    W32(o+0x34u,0);W32(o+0x38u,0);W32(o+0x40u,0);W32(o+0x3cu,0);
    W32(o+0x20u,1);W32(o+0x1cu,0);W32(o+0x44u,0);
    W16(o+0x26u,1);W16(o+0x2eu,0);W16(o+0x2cu,0);W16(o+0x32u,0x200u);
    a[0]=d;cf_call(g,K_PATH,a,1,&r);              /* "data\sound.cfg" */
    r.ecx=d;
    const uint32_t path=r.eax;
    /* Driver: "DirectSound" gives 2, anything else 1. */
    r.eax=d+0x0fu;r.edx=d+0x1cu;r.ecx=d+0x15u;
    a[0]=d+0x0fu;a[1]=d+0x15u;a[2]=d+0x1cu;a[3]=buf;a[4]=0x28;a[5]=path;cf_call(g,K_PROFILE,a,6,&r);
    r.edx=d+0x1du;r.ecx=buf;a[0]=buf;a[1]=d+0x1du;cf_call(g,K_STRICMP,a,2,&r);
    W16(o+0x2au,r.eax?1u:2u);
    /* Buffers: 1..64, else 5. */
    r.eax=d+0x29u;r.edx=d+0x37u;r.ecx=d+0x2fu;
    a[0]=d+0x29u;a[1]=d+0x2fu;a[2]=d+0x37u;a[3]=buf;a[4]=0x28;a[5]=path;cf_call(g,K_PROFILE,a,6,&r);
    r.edx=buf;a[0]=buf;cf_call(g,K_ATOL,a,1,&r);
    r.ecx=buf;
    if(fx_s32(r.eax)<1||fx_s32(r.eax)>0x40)W16(o+0x30u,5);else W16(o+0x30u,r.eax);
    /* Rate: 11 gives 10h, anything else 20h. */
    r.ecx=d+0x38u;r.eax=d+0x43u;r.edx=d+0x3eu;
    a[0]=d+0x38u;a[1]=d+0x3eu;a[2]=d+0x43u;a[3]=buf;a[4]=0x28;a[5]=path;cf_call(g,K_PROFILE,a,6,&r);
    r.eax=buf;a[0]=buf;cf_call(g,K_ATOL,a,1,&r);
    r.ecx=buf;
    W16(o+0x24u,r.eax==0xbu?0x10u:0x20u);
    /* Width: _atol (result unused), then "Mono" gives 4, anything else 8. */
    r.edx=d+0x44u;r.eax=d+0x4au;r.ecx=d+0x50u;
    a[0]=d+0x44u;a[1]=d+0x4au;a[2]=d+0x50u;a[3]=buf;a[4]=0x28;a[5]=path;cf_call(g,K_PROFILE,a,6,&r);
    r.ecx=buf;a[0]=buf;cf_call(g,K_ATOL,a,1,&r);
    r.ecx=buf;
    r.eax=d+0x51u;r.edx=buf;a[0]=buf;a[1]=d+0x51u;cf_call(g,K_STRICMP,a,2,&r);
    W16(o+0x28u,r.eax?8u:4u);
    /* 4638de: the construction counter. */
    r.ecx=R32(VA(0x6cbee4u));
    W32(r.ecx,R32(r.ecx)+1u);
    put_ecx(ecx,r.ecx);
    return pair(o,r.edx);
}

/* The piece size of a tape record (cmp with 1F4h, signed). A negative
 * remaining length is not modeled (the original would pass it to fread
 * as a size above 2 GiB, into its 500-byte stack buffer). */
static int tape_piece(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t length,uint32_t *n) {
    const uint32_t left=R32(length);
    if(fx_s32(left)<0) { note(g,ES2_CONFIG_UNMODELED);return 0; }
    *n=fx_s32(left)>=CHUNK?CHUNK:left;
    return 1;
}

/* Original: DBSIM.EXE 0x45cca4. Frame: length f-4, buffer f-1F8h. */
uint64_t es2_tape_save_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t tape,uint32_t path,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_CONFIG_FRAME,length=f-4u,buf=f-0x1f8u;
    es2_weapon_registers r={path,entry[1],entry[2]};
    uint32_t a[4],n;
    a[0]=path;a[1]=VA(0x49efa8u);cf_call(g,K_FOPEN,a,2,&r);   /* "rb" */
    const uint32_t file=r.eax;
    if(file) {
        a[0]=file;a[1]=0;a[2]=2;cf_call(g,K_FSEEK,a,3,&r);
        a[0]=file;cf_call(g,K_FTELL,a,1,&r);
        r.ecx=file;W32(length,r.eax);
        a[0]=file;cf_call(g,K_REWIND,a,1,&r);
        r.ecx=file;
    } else { r.eax=0;W32(length,0); }
    r.edx=tape;r.ecx=length;a[0]=length;a[1]=4;a[2]=1;a[3]=tape;cf_call(g,K_FWRITE,a,4,&r);
    while(R32(length)&&tape_piece(m,g,length,&n)) {
        r.eax=buf;a[0]=buf;a[1]=1;a[2]=n;a[3]=file;cf_call(g,K_FREAD,a,4,&r);
        if(r.eax!=n)note(g,ES2_CONFIG_RESIDUE);
        r.edx=tape;r.ecx=buf;a[0]=buf;a[1]=1;a[2]=n;a[3]=tape;cf_call(g,K_FWRITE,a,4,&r);
        W32(length,R32(length)-n);
    }
    a[0]=file;cf_call(g,K_FCLOSE,a,1,&r);
    put_ecx(ecx,file);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x45cd5c. Frame: length f-4, buffer f-1F8h. */
uint64_t es2_tape_restore_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t tape,uint32_t path,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_CONFIG_FRAME,length=f-4u,buf=f-0x1f8u;
    es2_weapon_registers r={path,entry[1],entry[2]};
    uint32_t a[4],n,file=0;
    if(path) { a[0]=path;a[1]=VA(0x49efabu);cf_call(g,K_FOPEN,a,2,&r);file=r.eax; }   /* "wb" */
    r.eax=length;a[0]=length;a[1]=1;a[2]=4;a[3]=tape;cf_call(g,K_FREAD,a,4,&r);
    if(r.eax!=4u)note(g,ES2_CONFIG_RESIDUE);   /* the length's unread bytes are residue */
    while(R32(length)&&tape_piece(m,g,length,&n)) {
        r.eax=buf;a[0]=buf;a[1]=1;a[2]=n;a[3]=tape;cf_call(g,K_FREAD,a,4,&r);
        if(r.eax!=n)note(g,ES2_CONFIG_RESIDUE);
        if(file) { r.edx=buf;a[0]=buf;a[1]=1;a[2]=n;a[3]=file;cf_call(g,K_FWRITE,a,4,&r); }
        W32(length,R32(length)-n);
    }
    a[0]=file;cf_call(g,K_FCLOSE,a,1,&r);
    put_ecx(ecx,file);
    return pair(r.eax,r.edx);
}
