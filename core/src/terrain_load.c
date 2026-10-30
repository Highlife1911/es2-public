#include "es2/core/terrain_load.h"
#include "fixed.h"
#include <stddef.h>
/* The terrain zone load of terrain.cpp (docs/reverse-engineering/ terrain.md,
 * section "the terrain zone load"). Every guest read and write is done in the
 * original's order (the stubs of the in-vitro harness change what the originals
 * reread), and the register struct r follows EAX/EDX/ECX so that each game call
 * sees the original's entry registers. Locals of the two framed functions live
 * at the original's EBP offsets in the zeroed scratch frame f. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define GVA(x) (g->image+(uint32_t)(x)-0x400000u)
static uint64_t tl_pair(uint32_t a,uint32_t d) { return (uint64_t)d<<32|a; }
static int32_t s32(uint32_t x) { return (int32_t)fx_s32(x); }
static uint32_t lo8(uint32_t reg,uint32_t byte) { return (reg&0xffffff00u)|(uint8_t)byte; }
static uint32_t shl(uint32_t x,uint32_t n) { return x<<(n&31u); }
static uint64_t tl_done(const es2_weapon_registers *r,uint32_t *ecx) { if(ecx)*ecx=r->ecx;return tl_pair(r->eax,r->edx); }
/* One direct game call: the observer first (reference VA), then the bridge. */
static void tl_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,GVA(va),a,n,r);
}
/* A virtual call through slot +18h of the vtable the caller loaded. */
static void tl_slot(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t vtable,const uint32_t *a,unsigned n,
    es2_weapon_registers *r) {
    const uint32_t target=R32(vtable+0x18u);
    if(g->observe)g->observe(g->observer_context,target,0x18u);
    g->call(g->context,target,a,n,r);
}
/* count +delta: the pointer [6CBEE4h] read, then [pointer] += delta. */
static uint32_t tl_count(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t delta) {
    const uint32_t p=R32(GVA(0x6cbee4u));W32(p,R32(p)+delta);return p;
}
/* The exponent loop of 46c4c4 (and nothing else): the smallest k with
 * 1 << k not below the signed word (k <= 15; 0 for words below 2). */
static uint32_t tl_exponent(uint32_t word) {
    uint32_t k=0,p=1;
    while(s32(p)<fx_s16(word)){++k;p+=p;}
    return k;
}
/* Original: DBSIM.EXE 0x42789c. Frame f: the record f-58h (context word
 * f-48h), the stream object f-30h, the grid f-34h, the name f-64h..f-5Ch,
 * the reads f-70h/f-6Ch/f-74h/f-68h, the angles f-7Ch..f-78h, the origin
 * copy f-88h..f-80h. EBX = the zone word, then the path; ESI = 0x49aac8. */
uint64_t es2_zone_load_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t zone,
    const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_ZONE_LOAD_FRAME,S=f-0x30u,P=GVA(0x49aac8u);
    es2_weapon_registers r={GVA(0x49ab2au),entry[1],entry[2]};uint32_t a[6];
    tl_call(g,0x49630cu,NULL,0,&r);
    if(!R8(GVA(0x49aaf0u)))W8(GVA(0x49aaf0u),R8(GVA(0x49aaf0u))+1u);
    r.edx=R8(GVA(0x4d1f40u));
    W32(GVA(0x4aaaa8u),r.edx);
    W32(GVA(0x4aaaacu),1);
    W32(GVA(0x49aad0u),0x4e20u);
    if(fx_s16(zone)<0) {
        /* 4278ef: no terrain, the object table cleared */
        W16(GVA(0x4aaa7cu),0);
        r.ecx=0;W32(GVA(0x4aaa74u),r.ecx);
        r.eax=0;W32(GVA(0x4aaa78u),r.eax);
        r.ecx=R32(f-0x58u);                            /* the saved FS:0 head */
        return tl_done(&r,ecx);
    }
    /* 42790c: "zone" + _itoa(signed zone word, f-60h, 10) */
    r.edx=R32(GVA(0x49aaf1u));
    r.ecx=f-0x60u;
    r.eax=(uint32_t)fx_s16(zone);
    W32(f-0x64u,r.edx);
    r.edx=R32(GVA(0x49aaf5u));
    W32(f-0x60u,r.edx);
    r.edx=lo8(r.edx,R8(GVA(0x49aaf9u)));
    W8(f-0x5cu,r.edx);
    a[0]=r.eax;a[1]=r.ecx;a[2]=10;tl_call(g,0x496820u,a,3,&r);
    r.edx=R32(GVA(0x4a09f0u));
    r.ecx=f-0x64u;
    a[0]=r.ecx;a[1]=r.edx;tl_call(g,0x492becu,a,2,&r);
    const uint32_t path=r.eax;
    W16(f-0x48u,8);
    r.eax=S;a[0]=S;a[1]=path;tl_call(g,0x472f14u,a,2,&r);
    r.eax=R32(f-0x2cu);
    if(r.eax) {
        r.edx=0;                                       /* SETE DL; AND EDX,1 */
        a[0]=GVA(0x6c56c8u);a[1]=5;a[2]=r.edx;a[3]=GVA(0x49ab94u);a[4]=0x49u;a[5]=path;
        tl_call(g,0x479c78u,a,6,&r);
    }
    r.ecx=f-0x70u;r.eax=S;r.edx=R32(S);
    a[0]=S;a[1]=4;a[2]=f-0x70u;tl_slot(m,g,r.edx,a,3,&r);
    r.ecx=f-0x6cu;r.eax=S;r.edx=R32(S);
    a[0]=S;a[1]=4;a[2]=f-0x6cu;tl_slot(m,g,r.edx,a,3,&r);
    a[0]=0x129u;tl_call(g,0x47749cu,a,1,&r);
    r.ecx=0x129u;                                      /* POP ECX */
    W32(f-0x34u,r.eax);
    if(r.eax) {
        W16(f-0x48u,0x20);
        r.edx=f-0x70u;
        r.ecx=R32(f-0x34u);
        a[0]=r.ecx;a[1]=r.edx;tl_call(g,0x46befcu,a,2,&r);
        r.eax=tl_count(m,g,0xfffffffcu);               /* SUB [count],4 */
        W16(f-0x48u,0x14);
    }
    r.edx=R32(f-0x34u);
    W32(P,r.edx);
    r.eax=f-0x74u;r.ecx=S;
    r.eax=R32(S);
    a[0]=S;a[1]=4;a[2]=f-0x74u;tl_slot(m,g,r.eax,a,3,&r);
    r.edx=R32(P);
    r.eax=R32(f-0x74u);
    r.ecx=f-0x68u;
    W32(r.edx+0x108u,r.eax);
    r.eax=S;r.edx=R32(S);
    a[0]=S;a[1]=4;a[2]=f-0x68u;tl_slot(m,g,r.edx,a,3,&r);
    r.eax=R32(P);
    r.edx=R32(f-0x68u);
    W32(r.eax+0x118u,r.edx);
    r.edx=10;
    if(s32(R32(f-0x74u))>14) {
        r.ecx=R32(f-0x74u)-14u;
        r.edx=fx_sar(r.edx,r.ecx);
    }
    W32(r.eax+0x10cu,r.edx);
    /* 427a43: the origin through three zero locals */
    r.edx=0;W32(f-0x88u,r.edx);
    r.ecx=0;r.eax+=0xf4u;
    W32(f-0x84u,r.ecx);
    W32(f-0x80u,r.edx);
    W32(r.eax+8u,r.edx);
    r.ecx=R32(f-0x88u);W32(r.eax,r.ecx);
    r.edx=R32(f-0x84u);W32(r.eax+4u,r.edx);
    r.eax=f-0x64u;
    a[0]=r.eax;a[1]=GVA(0x49aba0u);tl_call(g,0x492becu,a,2,&r);
    const uint32_t dba=r.eax;
    r.eax=R32(P);
    a[0]=r.eax;a[1]=dba;a[2]=0;tl_call(g,0x46c754u,a,3,&r);
    /* 427b3a: the zone transform: angles 0, the origin as translation */
    W16(f-0x7cu,0);W16(f-0x7au,0);W16(f-0x78u,0);
    r.eax=R32(P)+0xf4u;
    r.edx=R32(r.eax+8u);
    W32(GVA(0x4aaaa0u),r.edx);
    r.edx=f-0x7cu;
    r.ecx=R32(r.eax);
    W32(GVA(0x4aaa98u),r.ecx);
    r.eax=R32(r.eax+4u);
    W32(GVA(0x4aaa9cu),r.eax);
    a[0]=f-0x7cu;a[1]=GVA(0x4aaa84u);tl_call(g,0x47ebb8u,a,2,&r);
    r.eax=R32(P);
    r.edx=S;
    W32(GVA(0x4a0be0u),r.eax);
    a[0]=S;a[1]=2;tl_call(g,0x472f64u,a,2,&r);
    r.ecx=R32(f-0x58u);                                /* the saved FS:0 head */
    return tl_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x46befc. EBX = G. */
uint64_t es2_grid_ctor_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t grid,
    const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t G=grid;es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[5];
    W32(G,GVA(0x4a69f0u));
    W32(G,GVA(0x4a6a14u));
    r.eax=R32(GVA(0x6cbee4u));
    r.edx=G+0x28u;
    W32(r.eax,R32(r.eax)+1u);
    W32(G,GVA(0x4a0c74u));
    a[0]=r.edx;a[1]=8;a[2]=0x14u;a[3]=1;a[4]=GVA(0x49545bu);tl_call(g,0x49667cu,a,5,&r);
    r.ecx=G+0xd0u;
    a[0]=r.ecx;a[1]=0x100u;tl_call(g,0x495268u,a,2,&r);
    r.eax=0;
    W32(G+0xfcu,r.eax);W32(G+0xf8u,r.eax);W32(G+0xf4u,r.eax);
    r.eax=G;
    W32(G+0x108u,10);
    W32(G+0x10cu,8);
    r.edx=R32(GVA(0x6cbee4u));
    W32(r.edx,R32(r.edx)+1u);
    return tl_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x495268. EBX = C. */
uint64_t es2_height_cache_ctor_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t cache,
    uint32_t size,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t C=cache;es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[5];
    W32(C,GVA(0x4a69f0u));
    W32(C,GVA(0x4a6a14u));
    r.eax=R32(GVA(0x6cbee4u));
    W32(r.eax,R32(r.eax)+1u);
    W32(C,GVA(0x4a6a40u));
    r.edx=size;r.ecx=0;
    W32(C+0x0cu,r.edx);
    W32(C+8u,r.ecx);
    W32(C+4u,100);
    r.eax=R32(C+0x0cu);
    a[0]=r.eax;tl_call(g,0x4774f0u,a,1,&r);
    r.ecx=a[0];                                        /* POP ECX */
    W32(C+0x10u,r.eax);
    r.edx=R32(C+0x0cu);
    a[0]=0;a[1]=0x0cu;a[2]=r.edx;a[3]=1;a[4]=GVA(0x4953c4u);tl_call(g,0x49667cu,a,5,&r);
    W32(C+0x14u,r.eax);
    a[0]=0;a[1]=0x0cu;a[2]=8;a[3]=1;a[4]=GVA(0x4953c4u);tl_call(g,0x49667cu,a,5,&r);
    W32(C+0x18u,r.eax);
    r.ecx=R32(GVA(0x6cbee4u));
    r.eax=C;
    W32(r.ecx,R32(r.ecx)+1u);
    return tl_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x46c754. Frame f: the record f-84h (context word
 * f-74h), the material stream f-30h, the bitmap stream f-60h, the bitmap
 * f-64h, the name copy f-0C8h, the text grid's locals: the file f-94h, the
 * shift f-90h, the four fields f-9Ch/f-98h/f-8Ch/f-88h, the height f-0A0h.
 * EBX = G, ESI = the record index, then the bitmap or the cell index, EDI =
 * the material try. */
uint64_t es2_terrain_materials_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t grid,
    uint32_t name,uint32_t base,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_TERRAIN_MATERIALS_FRAME,G=grid,S=f-0x30u,T=f-0x60u,copy=f-0xc8u;
    const uint32_t end=g->scratch+ES2_TERRAIN_MATERIALS_SCRATCH;
    es2_weapon_registers r={GVA(0x4a0c14u),entry[1],entry[2]};uint32_t a[6];
    tl_call(g,0x49630cu,NULL,0,&r);
    /* 46c76d: the inline strcpy of the name (REPNE SCASB, REP MOVSD/MOVSB) */
    uint32_t length=0;
    while(R8(name+length))++length;
    ++length;
    for(uint32_t i=0;i<length&&copy+i<end;++i)W8(copy+i,R8(name+i));
    r.eax=copy;r.edx=length;r.ecx=0;
    W16(f-0x74u,8);
    r.eax=R32(GVA(0x4a09f0u));
    a[0]=GVA(0x4a0c4du);a[1]=r.eax;tl_call(g,0x492becu,a,2,&r);
    r.edx=S;
    a[0]=S;a[1]=r.eax;tl_call(g,0x472f14u,a,2,&r);
    r.eax=G+0x11du;r.edx=S;r.ecx=R32(S);
    a[0]=S;a[1]=4;a[2]=G+0x11du;tl_slot(m,g,r.ecx,a,3,&r);
    r.eax=R32(G+0x11du)<<3;
    a[0]=r.eax;tl_call(g,0x4774f0u,a,1,&r);
    r.ecx=a[0];                                        /* POP ECX */
    W32(G+0x121u,r.eax);
    for(uint32_t i=0;s32(i)<s32(R32(G+0x11du));++i) {
        r.eax=(i<<3)+R32(G+0x121u);r.edx=S;
        a[0]=S;a[1]=8;a[2]=r.eax;tl_call(g,0x4735b8u,a,3,&r);
    }
    r.ecx=copy;
    a[0]=copy;a[1]=0x2eu;tl_call(g,0x4967a2u,a,2,&r);
    int bitmap_path=0;
    if(r.eax) {
        a[0]=r.eax;a[1]=GVA(0x4a0c52u);tl_call(g,0x496868u,a,2,&r);
        bitmap_path=!r.eax;
    }
    if(bitmap_path) {
        /* 46c83d: the first bitmap of the DBA */
        W16(f-0x74u,0x14);
        r.eax=copy;r.edx=T;
        a[0]=T;a[1]=copy;tl_call(g,0x472f14u,a,2,&r);
        r.ecx=T;
        a[0]=T;a[1]=0;tl_call(g,0x47a144u,a,2,&r);
        r.eax=R32(r.eax+8u);
        const uint32_t B=R32(r.eax);
        r.eax=lo8(r.eax,base);
        a[0]=G;a[1]=B;a[2]=r.eax;tl_call(g,0x46c4c4u,a,3,&r);
        W32(f-0x64u,B);
        if(R32(f-0x64u)) {
            r.edx=R32(f-0x64u);
            a[0]=r.edx;a[1]=0;tl_call(g,0x496406u,a,2,&r);
            r.ecx=R32(GVA(0x6cbee4u));
            W32(r.ecx,R32(r.ecx)+r.eax);
            W16(f-0x74u,0x2c);
            r.eax=R32(f-0x64u);
            r.edx=R32(r.eax);
            a[0]=r.eax;a[1]=3;tl_slot(m,g,r.edx,a,2,&r);
            W16(f-0x74u,0x20);
        }
        r.ecx=T;
        a[0]=T;a[1]=2;tl_call(g,0x472f64u,a,2,&r);
    } else {
        /* 46c8c3: the text grid */
        r.eax=15u-R32(G+0x108u);
        r.edx=copy;
        W32(f-0x90u,r.eax);
        a[0]=copy;a[1]=GVA(0x4a0c57u);tl_call(g,0x496802u,a,2,&r);
        W32(f-0x94u,r.eax);
        r.ecx=f-0x9cu;r.edx=f-0x98u;r.eax=R32(f-0x94u);
        a[0]=r.eax;a[1]=GVA(0x4a0c59u);a[2]=f-0x9cu;a[3]=f-0x98u;a[4]=f-0x8cu;a[5]=f-0x88u;
        tl_call(g,0x4967b4u,a,6,&r);
        r.ecx=R32(f-0x90u);
        if(s32(r.ecx)<0){r.edx=shl(R32(f-0x8cu),0u-r.ecx);r.ecx=0u-r.ecx;}
        else r.edx=fx_sar(R32(f-0x8cu),r.ecx);
        W32(f-0x8cu,r.edx);
        r.ecx=R32(f-0x90u);
        if(s32(r.ecx)<0){r.eax=shl(R32(f-0x88u),0u-r.ecx);r.ecx=0u-r.ecx;}
        else r.eax=fx_sar(R32(f-0x88u),r.ecx);
        W32(f-0x88u,r.eax);
        r.edx=R32(f-0x9cu);W32(G+0x100u,r.edx);
        r.ecx=R32(f-0x98u);W32(G+0x104u,r.ecx);
        r.ecx=R32(G+0x100u);
        uint32_t n=shl(1,r.ecx);
        r.ecx=R32(G+0x104u);
        r.eax=shl(1,r.ecx);
        n*=r.eax;
        a[0]=0;a[1]=0x10u;a[2]=n;a[3]=1;a[4]=GVA(0x4715e1u);tl_call(g,0x49667cu,a,5,&r);
        W32(G+0xecu,r.eax);
        W32(f-0x88u,R32(f-0x88u)<<3);
        W32(f-0x8cu,R32(f-0x8cu)<<3);
        r.edx=R32(f-0x88u);
        W32(G+0x110u,r.edx);
        r.eax=fx_sar(R32(f-0x8cu)-R32(f-0x88u),8);
        W32(G+0x118u,r.eax);
        for(uint32_t i=0;;++i) {
            r.ecx=R32(G+0x100u);r.eax=shl(1,r.ecx);
            r.ecx=R32(G+0x104u);r.edx=shl(1,r.ecx);
            const int64_t product=(int64_t)s32(r.eax)*s32(r.edx);    /* IMUL EDX */
            r.eax=(uint32_t)product;r.edx=(uint32_t)((uint64_t)product>>32);
            if(!(s32(i)<s32(r.eax)))break;
            r.eax=f-0xa0u;r.edx=R32(f-0x94u);
            a[0]=r.edx;a[1]=GVA(0x4a0c65u);a[2]=f-0xa0u;tl_call(g,0x4967b4u,a,3,&r);
            r.ecx=R32(f-0x90u);
            if(s32(r.ecx)<0){r.eax=shl(R32(f-0xa0u),0u-r.ecx);r.ecx=0u-r.ecx;}
            else r.eax=fx_sar(R32(f-0xa0u),r.ecx);
            W32(f-0xa0u,r.eax);
            /* 46ca4a: the cell (index i), its height byte, its mark */
            if(s32(i)>=0){r.ecx=R32(G+0x100u)+R32(G+0x104u);r.edx=shl(1,r.ecx);}
            r.eax=s32(i)<0||!(s32(i)<s32(r.edx))?0u:(i<<4)+R32(G+0xecu);
            r.edx=lo8(r.edx,R8(f-0xa0u));
            W8(r.eax,r.edx);
            r.ecx=R32(G+0xf0u);
            W8(r.ecx+i,0);
            if(s32(i)>=0){r.ecx=R32(G+0x100u)+R32(G+0x104u);r.eax=shl(1,r.ecx);}
            r.eax=s32(i)<0||!(s32(i)<s32(r.eax))?0u:(i<<4)+R32(G+0xecu);
            W8(r.eax+0xfu,R8(r.eax+0xfu)&2u);
            for(uint32_t k=1;s32(k)<s32(R32(G+0x11du));++k) {
                a[0]=GVA(0x4d2599u);tl_call(g,0x492ee0u,a,1,&r);
                r.eax=(uint32_t)fx_s16(r.eax)&0xfffu;
                r.ecx=a[0];                            /* POP ECX */
                r.edx=s32(r.eax)<0x4ce?1u:0u;          /* SETL DL; AND EDX,1 */
                if(r.edx) {
                    if(s32(i)>=0){r.ecx=R32(G+0x100u)+R32(G+0x104u);r.eax=shl(1,r.ecx);}
                    r.eax=s32(i)<0||!(s32(i)<s32(r.eax))?0u:(i<<4)+R32(G+0xecu);
                    r.ecx=k<<2;
                    r.edx=lo8(r.edx,(R8(r.eax+0xfu)&2u)|(uint8_t)r.ecx);
                    W8(r.eax+0xfu,r.edx);
                    break;
                }
            }
        }
        r.eax=R32(f-0x94u);
        a[0]=r.eax;tl_call(g,0x496838u,a,1,&r);
        r.ecx=a[0];                                    /* POP ECX */
        r.eax=R32(G+0x118u);
        r.edx=R32(G+0x110u);
        a[0]=G+0xd0u;a[1]=r.edx;a[2]=r.eax;tl_call(g,0x4952e8u,a,3,&r);
        a[0]=G+0xd0u;tl_call(g,0x4952fcu,a,1,&r);
        r.ecx=G+0xd0u;                                 /* POP ECX */
    }
    /* 46cb79 */
    r.eax=S;
    a[0]=S;a[1]=2;tl_call(g,0x472f64u,a,2,&r);
    r.edx=R32(f-0x84u);                                /* the saved FS:0 head */
    return tl_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x46c4c4. ESI = G, EDI = the cell, EBX = the material
 * try; locals: y f-4, the source f-8, x f-0Ch, the block column f-10h. */
uint64_t es2_terrain_cells_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t grid,
    uint32_t bitmap,uint32_t base,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t G=grid;es2_weapon_registers r={1,0,entry[2]};uint32_t a[5];
    /* 46c4ca: the two exponent loops; EAX/EDX/ECX as the second leaves them */
    r.edx=tl_exponent(R16(bitmap+6u));
    W32(G+0x100u,r.edx);
    r.edx=tl_exponent(R16(bitmap+4u));
    r.ecx=(uint32_t)fx_s16(R16(bitmap+4u));
    r.eax=r.edx;
    W32(G+0x104u,r.eax);
    r.ecx=R32(G+0x100u);
    uint32_t n=shl(1,r.ecx);
    r.ecx=r.eax;
    r.eax=shl(1,r.ecx);
    n*=r.eax;
    a[0]=0;a[1]=0x10u;a[2]=n;a[3]=1;a[4]=GVA(0x4715e1u);tl_call(g,0x49667cu,a,5,&r);
    W32(G+0xecu,r.eax);
    a[0]=n;tl_call(g,0x4774f0u,a,1,&r);
    r.ecx=a[0];                                        /* POP ECX */
    W32(G+0xf0u,r.eax);
    W32(G+0x110u,0xffu);
    r.eax=0;W32(G+0x114u,r.eax);
    r.edx=0;
    for(uint32_t y=0;;++y) {
        r.ecx=R32(G+0x104u);r.edx=shl(1,r.ecx);
        if(!(s32(r.edx)>s32(y)))break;
        /* 46c56a: the grid row 2^rows - 1 - y */
        r.eax=shl(r.edx-y-1u,R32(G+0x100u));
        uint32_t cell;
        if(s32(r.eax)<0)cell=0;
        else {
            r.ecx=R32(G+0x100u)+R32(G+0x104u);
            r.edx=shl(1,r.ecx);
            cell=s32(r.eax)<s32(r.edx)?(r.eax<<4)+R32(G+0xecu):0;
        }
        r.ecx=R32(G+0x100u);
        r.eax=shl(y,r.ecx);
        r.edx=bitmap;
        uint32_t source=r.eax+R32(bitmap+0x0eu);
        r.eax=0;
        for(uint32_t x=0;;++x,cell+=0x10u,++source) {
            r.ecx=R32(G+0x100u);r.eax=shl(1,r.ecx);
            if(!(s32(r.eax)>s32(x)))break;
            /* 46c5c0: the height byte (pixel - b), the maximum, the mark */
            r.eax=lo8(r.eax,R8(source)-base);
            r.edx=r.eax&0xffu;
            if(s32(r.edx)>s32(R32(G+0x114u)))W32(G+0x114u,r.edx);
            W8(cell,r.eax);
            a[0]=G;a[1]=cell;a[2]=0;tl_call(g,0x470d74u,a,3,&r);
            r.ecx=R32(G+0x121u);
            r.eax=8u-R32(r.ecx+4u)+0x0du-R32(G+0x108u);
            r.ecx=r.eax;
            r.edx=shl(1,r.ecx)-1u;
            r.eax=r.edx;
            r.ecx=~r.eax;
            if((y&r.eax)||(x&r.eax)) {
                /* 46c61e: copy bits 2..7 from the block's first cell */
                r.edx=y&r.ecx;
                const uint32_t column=r.ecx&x;
                r.ecx=R32(G+0x104u);
                r.eax=shl(1,r.ecx)-r.edx-1u;
                r.ecx=R32(G+0x100u);
                r.eax=shl(r.eax,r.ecx);
                if(s32(r.eax)<0)r.edx=0;
                else {
                    r.ecx=R32(G+0x100u)+R32(G+0x104u);
                    r.edx=shl(1,r.ecx);
                    r.edx=s32(r.eax)<s32(r.edx)?(r.eax<<4)+R32(G+0xecu):0;
                }
                r.eax=column<<4;
                r.edx+=r.eax;
                r.eax=r.edx;
                r.edx=lo8(r.edx,R8(cell+0x0fu)&2u);
                r.ecx=R8(r.eax+0x0fu);
                r.ecx=fx_sar(r.ecx,2);
                r.eax=r.ecx<<2;
                r.edx=lo8(r.edx,(r.edx&0xffu)|(r.eax&0xffu));
                W8(cell+0x0fu,r.edx);
            } else {
                /* 46c68f: one draw on stream A */
                W8(cell+0x0fu,R8(cell+0x0fu)&2u);
                uint32_t k=1;
                for(;;) {
                    a[0]=GVA(0x4d2599u);tl_call(g,0x492ee0u,a,1,&r);
                    r.eax=(uint32_t)fx_s16(r.eax)&0xfffu;
                    r.ecx=a[0];                        /* POP ECX */
                    r.edx=s32(r.eax)<0x4ce?1u:0u;      /* SETL DL; AND EDX,1 */
                    if(r.edx) {
                        r.eax=k;
                        r.edx=lo8(r.edx,R8(cell+0x0fu)&2u);
                        r.eax<<=2;
                        r.edx=lo8(r.edx,(r.edx&0xffu)|(r.eax&0xffu));
                        W8(cell+0x0fu,r.edx);
                        break;
                    }
                    ++k;
                    if(!(s32(k)<2))break;
                }
            }
        }
    }
    /* 46c70b */
    r.eax=0;W32(G+0x110u,r.eax);
    r.edx=R32(G+0x118u)*R32(G+0x114u);
    W32(G+0x114u,r.edx);
    r.ecx=R32(G+0x118u);
    r.eax=R32(G+0x110u);
    a[0]=G+0xd0u;a[1]=r.eax;a[2]=r.ecx;tl_call(g,0x4952e8u,a,3,&r);
    a[0]=G+0xd0u;tl_call(g,0x4952fcu,a,1,&r);
    r.ecx=G+0xd0u;                                     /* POP ECX */
    return tl_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x470d74. */
uint64_t es2_cell_mark_set_registers(const es2_guest_memory *m,uint32_t grid,uint32_t cell,uint32_t value,
    const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t edx=(cell-R32(grid+0xecu))>>4;
    const uint32_t eax=R32(grid+0xf0u);
    W8(eax+edx,value);
    if(ecx)*ecx=lo8(entry[2],value);
    return tl_pair(eax,edx);
}

/* ---- base flattening and the base-mark raster (terrain.md, section
 * "base flattening and the base-mark raster") like the functions above.
 * Appended here because they work on the same grid object with the same bridge.
 * ---- */

/* The cell pointer of index i (EAX) as 470fe8/471128/47129c/47136c compute
 * it inline: ECX = [G+100h] for a negative index, else [G+100h] +
 * [G+104h] (both reread); EDX = the cell or 0. */
static void tl_cell(const es2_guest_memory *m,uint32_t G,es2_weapon_registers *r) {
    if(s32(r->eax)<0) { r->edx=0; return; }
    r->ecx=R32(G+0x100u)+R32(G+0x104u);
    r->edx=shl(1u,r->ecx);
    r->edx=s32(r->eax)<s32(r->edx)?(r->eax<<4)+R32(G+0xecu):0u;
}

/* Original: DBSIM.EXE 0x470e00. */
uint64_t es2_cell_flag_get_registers(const es2_guest_memory *m,uint32_t grid,uint32_t x,uint32_t y,uint32_t *ecx) {
    const uint32_t n=R32(grid+0x100u);
    const uint32_t index=shl(y,n)+x;
    const uint32_t value=R8(R32(grid+0xf0u)+index);
    if(ecx)*ecx=n;
    return tl_pair(value,index);
}

/* The neighbours of a visited cell in call order: (dx, dy) and, for the
 * fill, the write flag pushed with them. */
static const int8_t tl_dx[8]={-1,0,1,-1,1,-1,0,1},tl_dy[8]={1,1,1,0,0,-1,-1,-1};
static const uint8_t tl_write[8]={0,1,1,0,1,0,0,0};
typedef struct tl_flood { const es2_guest_memory *m; const es2_weapon_graph *g; uint32_t grid,height; int fill; } tl_flood;

/* One entry of 470fe8 (fill = 0) or 471128 (fill = 1) at (x, y) with the
 * entry registers in r, up to its first recursive call. Returns 0 when the
 * original returns there (r = its result registers) and 1 for a visited
 * cell (r = the registers after the visit's own work: the sum EAX = the
 * cell, ECX = its height byte; the fill ECX = the cell, AL = h). */
static int tl_enter(const tl_flood *f,uint32_t x,uint32_t y,uint32_t write,es2_weapon_registers *r) {
    const es2_guest_memory *m=f->m;const es2_weapon_graph *g=f->g;const uint32_t G=f->grid;uint32_t a[4];
    r->ecx=R32(G+0x100u);
    r->eax=shl(1u,r->ecx);                             /* MOV EAX,1 / SHL EAX,CL */
    if(s32(x)<=-1||s32(r->eax)<=s32(x)||s32(y)<=-1||s32(r->eax)<=s32(y))return 0;
    r->ecx=R32(G+0x100u);
    r->eax=shl(y,r->ecx)+x;
    tl_cell(m,G,r);
    const uint32_t cell=r->edx;
    a[0]=G;a[1]=x;a[2]=y;tl_call(f->g,0x470e00u,a,3,r);
    if(f->fill&&write) {                               /* 4711bb: the spill write */
        r->ecx=lo8(r->ecx,f->height);r->edx=cell;W8(cell,f->height);
    }
    const uint32_t bit=f->fill?4u:2u;
    if(!(r->eax&1u)||(r->eax&bit))return 0;
    r->eax|=bit;
    a[3]=r->eax;tl_call(f->g,0x470ddcu,a,4,r);
    if(f->fill) {
        r->ecx=cell;r->eax=lo8(r->eax,f->height);W8(cell,f->height);
    } else {
        r->eax=cell;
        const uint32_t count=GVA(0x6b5474u),sum=GVA(0x6b5470u);
        W32(count,R32(count)+1u);
        r->ecx=R8(cell);
        W32(sum,R32(sum)+r->ecx);
    }
    return 1;
}
/* The registers the parent at (x, y) sets before its neighbour call k
 * (the rest are the previous call's). */
static void tl_neighbour(const tl_flood *f,unsigned k,uint32_t x,uint32_t y,es2_weapon_registers *r) {
    const uint32_t h=f->height;
    if(!f->fill)switch(k) {
    case 0: r->eax=y+1u;r->edx=x-1u;break;
    case 1: r->ecx=y+1u;break;
    case 2: r->eax=y+1u;r->edx=x+1u;break;
    case 3: r->ecx=x-1u;break;
    case 4: r->eax=x+1u;break;
    case 5: r->edx=y-1u;r->ecx=x-1u;break;
    case 6: r->eax=y-1u;break;
    default: break;
    } else switch(k) {
    case 0: r->edx=y+1u;r->eax=h;r->ecx=x-1u;break;
    case 1: r->eax=h;r->edx=y+1u;break;
    case 2: r->eax=y+1u;r->edx=x+1u;r->ecx=h;break;
    case 3: r->eax=x-1u;r->ecx=h;break;
    case 4: r->ecx=x+1u;r->edx=h;break;
    case 5: r->edx=y-1u;r->ecx=x-1u;r->eax=h;break;
    case 6: r->eax=h;r->edx=y-1u;break;
    default: r->ecx=h;break;
    }
}
/* The recursion as a loop: the directions on the path are the explicit
 * stack; a frame's result is its last neighbour's, which is the last call
 * completed when the frame ends. The calls and memory accesses come in the
 * original's order. */
static uint64_t tl_flood_run(const tl_flood *f,uint32_t x,uint32_t y,uint32_t write,const uint32_t entry[3],
    es2_flood_stack *s,uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    s->overflow=0;
    if(tl_enter(f,x,y,write,&r)) {
        uint32_t depth=0;unsigned k=0;
        for(;;) {
            if(k==8) {
                if(!depth)break;
                k=s->directions[--depth];
                x-=(uint32_t)(int32_t)tl_dx[k];y-=(uint32_t)(int32_t)tl_dy[k];++k;
                continue;
            }
            tl_neighbour(f,k,x,y,&r);
            const uint32_t cx=x+(uint32_t)(int32_t)tl_dx[k],cy=y+(uint32_t)(int32_t)tl_dy[k];
            if(!tl_enter(f,cx,cy,f->fill?tl_write[k]:0u,&r)) { ++k; continue; }
            if(depth==s->capacity) { s->overflow=1;break; }
            s->directions[depth++]=(uint8_t)k;
            if(depth>s->depth_max)s->depth_max=depth;
            x=cx;y=cy;k=0;
        }
    }
    return tl_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x470fe8. */
uint64_t es2_flood_sum_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t grid,uint32_t x,
    uint32_t y,const uint32_t entry[3],es2_flood_stack *stack,uint32_t *ecx) {
    const tl_flood f={m,g,grid,0,0};
    return tl_flood_run(&f,x,y,0,entry,stack,ecx);
}

/* Original: DBSIM.EXE 0x471128. */
uint64_t es2_flood_fill_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t grid,uint32_t x,
    uint32_t y,uint32_t height,uint32_t write,const uint32_t entry[3],es2_flood_stack *stack,uint32_t *ecx) {
    const tl_flood f={m,g,grid,height,1};
    return tl_flood_run(&f,x,y,write,entry,stack,ecx);
}

/* Original: DBSIM.EXE 0x47129c. The row pointer [EBP-8] reaches no callee
 * and is not kept. */
uint64_t es2_base_flatten_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t grid,
    const uint32_t entry[3],uint32_t divide[2],int *fault,uint32_t *ecx) {
    const uint32_t G=grid,sum=GVA(0x6b5470u),count=GVA(0x6b5474u);
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[5];
    *fault=0;
    r.ecx=R32(G+0x100u);
    r.eax=shl(1u,r.ecx);
    const uint32_t size=r.eax;
    for(uint32_t y=0;s32(y)<s32(size);++y) {
        r.edx=y;r.ecx=R32(G+0x100u);r.eax=shl(r.edx,r.ecx);
        tl_cell(m,G,&r);
        for(uint32_t x=0;s32(x)<s32(size);++x) {
            a[0]=G;a[1]=x;a[2]=y;tl_call(g,0x470e00u,a,3,&r);
            if(!(r.eax&1u)||(r.eax&2u))continue;
            r.eax=0;W32(sum,0);W32(count,0);
            tl_call(g,0x470fe8u,a,3,&r);
            const int32_t dividend=s32(R32(sum)),divisor=s32(R32(count));
            if(divisor==0||(dividend==INT32_MIN&&divisor==-1)) {
                divide[0]=(uint32_t)dividend;divide[1]=(uint32_t)divisor;*fault=1;
                return tl_done(&r,ecx);
            }
            r.eax=(uint32_t)(dividend/divisor);r.edx=(uint32_t)(dividend%divisor);
            a[3]=r.eax;a[4]=0;tl_call(g,0x471128u,a,5,&r);
        }
    }
    a[0]=G;tl_call(g,0x46c2e0u,a,1,&r);
    r.ecx=size;                                        /* POP ECX x 3: [EBP-4] last */
    return tl_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x47136c. The box corner, the shift and the loop
 * counters live in [EBP-4] .. [EBP-20h], which reach no callee. */
uint64_t es2_mark_raster_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t grid,uint32_t point,
    uint32_t type,uint32_t rows,uint32_t count,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t G=grid,P=point;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[3];
    r.ecx=8u;
    r.eax=(type<<3)+R32(G+0x121u);
    r.edx=R32(G+0x108u)-13u;
    const uint32_t s=r.edx;
    r.eax=R32(r.eax+4u);
    uint32_t edi=R32(P);
    r.ecx-=r.eax;r.eax=r.ecx;                          /* 8 - t */
    r.edx=r.eax+0xdu;                                  /* 21 - t */
    r.eax-=s;                                          /* 8 - t - s */
    r.ecx=r.edx;edi=fx_sar(edi,r.ecx);
    r.ecx=r.eax;edi=shl(edi,r.ecx);
    r.ecx=r.edx;
    const uint32_t bx=edi;
    r.edx=fx_sar(R32(P+4u),r.ecx);
    r.ecx=r.eax;
    r.edx=shl(r.edx+1u,r.ecx)-1u;
    const uint32_t by=r.edx;
    r.eax=rows;r.edx=0;
    uint32_t cursor=rows;
    for(uint32_t j=0;s32(j)<s32(count);) {
        r.ecx=cursor;
        uint32_t row=R32(r.ecx);
        for(uint32_t i=0;s32(i)<s32(count);++i,++row) {
            r.ecx=s;r.edx=fx_sar(j,r.ecx);
            r.ecx=by-r.edx;
            const uint32_t cy=r.ecx;
            r.ecx=s;r.edx=fx_sar(i,r.ecx)+bx;
            const uint32_t cx=r.edx;
            r.ecx=R32(G+0x100u);
            r.eax=shl(cy,r.ecx)+cx;
            tl_cell(m,G,&r);
            r.eax=r.edx;
            if(!R16(GVA(0x4d2680u))) {
                r.ecx=type;
                r.edx=lo8(r.edx,R8(r.eax+0xfu));
                r.ecx<<=2;
                r.edx=lo8(r.edx,(r.edx&2u)|(r.ecx&0xffu));
                W8(r.eax+0xfu,r.edx);
            }
            if(R8(row)) { a[0]=G;a[1]=r.eax;a[2]=1;tl_call(g,0x470d74u,a,3,&r); }
        }
        ++j;cursor+=4u;r.eax=j;
    }
    return tl_done(&r,ecx);
}
