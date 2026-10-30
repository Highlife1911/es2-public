#include "es2/core/effect_pools.h"
#include "fixed.h"
/* The effect pools with resources (docs/reverse-engineering/ mission_load.md,
 * section "effect pools with resources"). Every guest read and write is done in
 * the original's order (stubs and aliasing objects observe it), and the
 * register struct r follows EAX/EDX/ECX so that each game call sees the
 * original's entry registers. f is the synthetic EBP: the locals and the
 * runtime record lie at the original's offsets below it. lo() is a 16-bit load
 * that keeps the register's high word, lo8() the same for an 8-bit load. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define GVA(x) (g->image+(uint32_t)(x)-0x400000u)
typedef es2_weapon_registers ep_regs;
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
static int32_t s16(uint32_t x) { return fx_s16(x); }
static uint32_t sx8(uint32_t x) { return (uint32_t)(int32_t)(int8_t)(uint8_t)x; }
static int32_t s32(uint32_t x) { return (int32_t)fx_s32(x); }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static uint32_t lo8(uint32_t reg,uint32_t byte) { return (reg&0xffffff00u)|(uint8_t)byte; }
static uint64_t ep_done(const ep_regs *r,uint32_t *ecx) {
    if(ecx)*ecx=r->ecx;
    return (uint64_t)r->edx<<32|r->eax;
}
/* One direct game call: the observer first (reference VA), then the bridge. */
static void ep_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,ep_regs *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,GVA(va),a,n,r);
}
/* The stream's read method: slot +18h of the table the original loaded
 * into a register from the stream object; arguments (S, size, buffer). */
static void ep_read(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t table,const uint32_t a[3],ep_regs *r) {
    const uint32_t target=R32(table+0x18u);
    if(g->observe)g->observe(g->observer_context,target,0x18u);
    g->call(g->context,target,a,3,r);
}
/* The record set-up 49630c(EAX = descriptor), EDX/ECX as at entry. */
static void ep_record(const es2_weapon_graph *g,uint32_t descriptor,const uint32_t entry[3],ep_regs *r) {
    r->eax=GVA(descriptor);r->edx=entry[1];r->ecx=entry[2];
    ep_call(g,0x49630cu,NULL,0,r);
}
/* new(12h) into slot f-4, then pool_create(p, capacity, size) between the
 * context words 14h and 8. `ecx_header`: the header is loaded into ECX for
 * pool_create (flat_pools) instead of EDX; `eax_store`: the header is
 * reloaded into EAX for the store (flat_pools) instead of ECX. */
static void ep_pool(const es2_guest_memory *m,const es2_weapon_graph *g,ep_regs *r,uint32_t f,uint32_t context,
    uint32_t capacity,uint32_t size,uint32_t global,int flat) {
    uint32_t a[3];
    a[0]=0x12u;ep_call(g,0x47749cu,a,1,r);
    r->ecx=0x12u;                                           /* POP ECX */
    W32(f-4u,r->eax);
    if(r->eax) {
        W16(context,0x14u);
        if(flat) { r->ecx=R32(f-4u);a[0]=r->ecx; } else { r->edx=R32(f-4u);a[0]=r->edx; }
        a[1]=capacity;a[2]=size;ep_call(g,0x471ad8u,a,3,r);
        W16(context,8u);
    }
    if(flat) { r->eax=R32(f-4u);W32(GVA(global),r->eax); }
    else { r->ecx=R32(f-4u);W32(GVA(global),r->ecx); }
}
/* new(8) into slot f-8; word +6 = capacity, [list] = new[](4 x capacity),
 * word +4 = 0, between the context words 20h and 8. The capacity goes
 * through AX (through CX in flat_pools), the slot reload for the block
 * store through ECX (through EDX in flat_pools). */
static void ep_list(const es2_guest_memory *m,const es2_weapon_graph *g,ep_regs *r,uint32_t f,uint32_t context,
    uint32_t capacity,uint32_t global,int flat) {
    uint32_t a[1];
    a[0]=8u;ep_call(g,0x47749cu,a,1,r);
    r->ecx=8u;                                              /* POP ECX */
    W32(f-8u,r->eax);
    if(r->eax) {
        W16(context,0x20u);
        r->edx=R32(f-8u);
        if(flat) {
            r->ecx=lo(r->ecx,capacity);W16(r->edx+6u,capacity);
            r->eax=sx(capacity)<<2;a[0]=r->eax;
        } else {
            r->eax=lo(r->eax,capacity);W16(r->edx+6u,capacity);
            r->edx=sx(capacity)<<2;a[0]=r->edx;
        }
        ep_call(g,0x4774f0u,a,1,r);
        r->ecx=a[0];                                        /* POP ECX */
        if(flat) { r->edx=R32(f-8u);W32(r->edx,r->eax); }
        else { r->ecx=R32(f-8u);W32(r->ecx,r->eax); }
        r->eax=R32(f-8u);
        W16(r->eax+4u,0);
        W16(context,8u);
    }
    r->edx=R32(f-8u);
    W32(GVA(global),r->edx);
}
/* 45cdf4(path) after 492bec(name, directory); POP ECX. */
static void ep_bitmap(const es2_weapon_graph *g,ep_regs *r,uint32_t name,uint32_t directory) {
    uint32_t a[2]={name,directory};
    ep_call(g,0x492becu,a,2,r);
    a[0]=r->eax;ep_call(g,0x45cdf4u,a,1,r);
    r->ecx=a[0];
}
/* 474cd8(list, path, 140008h, 0). */
static void ep_shapes(const es2_weapon_graph *g,ep_regs *r,uint32_t list,uint32_t path) {
    const uint32_t a[4]={GVA(list),path,0x140008u,0};
    ep_call(g,0x474cd8u,a,4,r);
}
/* The fatal report after a failed open: 479c78(6C56C8h, 5, sete, file,
 * line, path); the SETE register is 0 there (the status is nonzero). */
static void ep_report(const es2_weapon_graph *g,ep_regs *r,uint32_t file,uint32_t line,uint32_t path) {
    const uint32_t a[6]={GVA(0x6c56c8u),5,0,GVA(file),line,path};
    ep_call(g,0x479c78u,a,6,r);
}
/* The inline string copy (REPNE SCASB over the source with ECX = -1, then
 * REP MOVSD and REP MOVSB of the length with its terminator): returns the
 * registers as the original leaves them (EAX = destination, EDX = length
 * + 1, ECX = 0). */
static void ep_strcpy(const es2_guest_memory *m,ep_regs *r,uint32_t dst,uint32_t src) {
    uint32_t n=0,p=src;
    r->eax&=0xffffff00u;   /* XOR EAX,EAX before: AL = 0 is all SCASB compares */
    for(;;) { ++n;if(R8(p++)==0)break; }
    r->ecx=n;r->edx=n;r->eax=dst;
    uint32_t s=src,d=dst;
    for(uint32_t k=n>>2;k;--k) { W32(d,R32(s));s+=4u;d+=4u; }
    for(uint32_t k=n&3u;k;--k) { W8(d,R8(s));++s;++d; }
    r->ecx=0;
}

/* Original: DBSIM.EXE 0x407b54. */
uint64_t es2_explosion_pools_registers(const es2_guest_memory *m,const es2_weapon_graph *g,const uint32_t entry[3],
    uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_EXPLOSION_POOLS_FRAME,context=f-0x48u,name=f-0x6cu,S=f-0x38u;
    ep_regs r;uint32_t a[6];
    ep_record(g,0x497be0u,entry,&r);
    /* the name template (16 bytes, REP MOVSD) */
    for(uint32_t k=0;k<16u;k+=4u)W32(name+k,R32(GVA(0x497ba0u)+k));
    r.ecx=0;
    ep_pool(m,g,&r,f,context,0x28u,0x5bu,0x4a961eu,0);
    ep_list(m,g,&r,f,context,0x1eu,0x4a9622u,0);
    a[0]=0;a[1]=8;a[2]=0xfu;a[3]=1;a[4]=GVA(0x46bbc1u);ep_call(g,0x49667cu,a,5,&r);
    W32(GVA(0x4a9638u),r.eax);
    /* the explosion bitmaps: 13 with the low-memory word 4d2680h (-l) set (after the suffix
     * 497d24h), else 15; the number at name+5 */
    const int low_memory=R16(GVA(0x4d2680u))!=0;
    if(low_memory) { a[0]=name;a[1]=GVA(0x497d24u);ep_call(g,0x4967eau,a,2,&r); }
    for(uint32_t ebx=0;;) {
        r.eax=name+5u;a[0]=ebx;a[1]=r.eax;a[2]=10;ep_call(g,0x496820u,a,3,&r);
        r.edx=R32(GVA(0x4a0a10u));
        a[0]=name;a[1]=r.edx;ep_call(g,0x492becu,a,2,&r);
        a[0]=r.eax;ep_call(g,0x45cdf4u,a,1,&r);
        r.ecx=a[0];                                         /* POP ECX */
        r.ecx=R32(GVA(0x4a9638u));
        W32(r.ecx+8u*ebx,r.eax);
        ++ebx;
        if(!(ebx<(low_memory?13u:15u)))break;
    }
    const uint32_t edi=R16(GVA(0x4d2680u))?GVA(0x497d26u):GVA(0x497d2eu);
    /* the shapes and their map */
    r.eax=R32(GVA(0x4a09f8u));
    a[0]=edi;a[1]=r.eax;ep_call(g,0x492becu,a,2,&r);
    ep_shapes(g,&r,0x4a9630u,r.eax);
    const uint32_t esi=r.eax;
    r.eax=sx(esi)<<2;a[0]=r.eax;ep_call(g,0x4774f0u,a,1,&r);
    r.ecx=a[0];                                             /* POP ECX */
    W32(GVA(0x4a963cu),r.eax);
    /* the explosion data */
    r.edx=R32(GVA(0x4a09f0u));
    a[0]=edi;a[1]=r.edx;ep_call(g,0x492becu,a,2,&r);
    const uint32_t path=r.eax;
    W16(context,0x2cu);
    r.eax=S;a[0]=S;a[1]=path;ep_call(g,0x472f14u,a,2,&r);
    r.eax=R32(S+4u);
    if(r.eax) { r.edx=0;ep_report(g,&r,0x497d35u,0x56u,path); }
    r.ecx=f-0x5au;r.eax=S;r.edx=R32(S);a[0]=S;a[1]=2;a[2]=f-0x5au;ep_read(m,g,r.edx,a,&r);
    r.ecx=R32(GVA(0x4a963cu));
    r.eax=sx(R16(f-0x5au))<<2;
    r.edx=S;a[0]=S;a[1]=r.eax;a[2]=r.ecx;ep_call(g,0x4735b8u,a,3,&r);
    r.ecx=S;a[0]=S;a[1]=2;a[2]=GVA(0x4a962cu);ep_call(g,0x4735b8u,a,3,&r);
    r.eax=sx(R16(GVA(0x4a962cu)));
    r.edx=r.eax*40u;a[0]=r.edx;ep_call(g,0x4774f0u,a,1,&r);
    r.ecx=a[0];                                             /* POP ECX */
    const uint32_t block=r.eax;
    W32(GVA(0x4a9628u),block);
    r.eax=sx(R16(GVA(0x4a962cu)));
    r.edx=r.eax*40u;r.ecx=S;a[0]=S;a[1]=r.edx;a[2]=block;ep_call(g,0x4735b8u,a,3,&r);
    r.eax=S;a[0]=S;a[1]=2;ep_call(g,0x472f64u,a,2,&r);
    /* 4a9634h = the largest word +0 of the records, plus one (at least 0) */
    W16(GVA(0x4a9634u),0);
    r.eax=R32(GVA(0x4a9628u));
    r.edx=r.eax;
    for(uint32_t ebx=0;;++ebx) {
        r.eax=sx(R16(GVA(0x4a962cu)));
        if(!(s32(ebx)<s32(r.eax)))break;
        r.eax=lo(r.eax,R16(r.edx))+1u;
        if(s16(r.eax)>=s16(R16(GVA(0x4a9634u))))r.ecx=r.eax;
        else r.ecx=lo(r.ecx,R16(GVA(0x4a9634u)));
        W16(GVA(0x4a9634u),r.ecx);
        r.edx+=0x28u;
    }
    /* +26h of each shape = the bitmap pair its map word names */
    r.eax=R32(GVA(0x4a963cu));
    for(uint32_t ebx=0;;) {
        r.edx=sx(esi);
        if(!(s32(ebx)<s32(r.edx)))break;
        r.edx=sx(R16(r.eax+2u));
        r.ecx=R32(GVA(0x4a9630u));
        r.edx=(r.edx<<3)+R32(GVA(0x4a9638u));
        r.ecx=R32(r.ecx+4u*ebx);
        ++ebx;r.eax+=4u;
        W32(r.ecx+0x26u,r.edx);
    }
    r.eax=R32(f-0x58u);
    return ep_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x408860. */
uint64_t es2_debris_pools_registers(const es2_guest_memory *m,const es2_weapon_graph *g,const uint32_t entry[3],
    uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_DEBRIS_POOLS_FRAME,context=f-0x18u;
    ep_regs r;uint32_t a[3];
    ep_record(g,0x497dd4u,entry,&r);
    ep_pool(m,g,&r,f,context,0x96u,0x61u,0x4a9642u,0);
    ep_list(m,g,&r,f,context,0x28u,0x4a9646u,0);
    a[0]=GVA(0x4a9650u);a[1]=GVA(0x497ec3u);a[2]=0;ep_call(g,0x40874cu,a,3,&r);
    r.eax=0;
    W32(GVA(0x4a964cu),0);
    W16(GVA(0x4a9664u),0x1770u);
    W16(GVA(0x4a9666u),0x3e80u);
    W16(GVA(0x4a9668u),0x1a4u);
    r.ecx=R32(f-0x28u);
    return ep_done(&r,ecx);
}


/* Original: DBSIM.EXE 0x40874c. */
uint64_t es2_debris_set_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t set,uint32_t name,
    uint32_t tag,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_DEBRIS_SET_FRAME,context=f-0x40u,S=f-0x30u;
    ep_regs r;uint32_t a[6];
    ep_record(g,0x497da0u,entry,&r);
    /* the shapes: 474cd8(set+6, path, 140008h, 0), the count word +0Ah */
    a[0]=name;a[1]=GVA(0x497eb4u);ep_call(g,0x492becu,a,2,&r);
    uint32_t path=r.eax;
    r.eax=set+6u;a[0]=set+6u;a[1]=path;a[2]=0x140008u;a[3]=0;ep_call(g,0x474cd8u,a,4,&r);
    W16(set+0xau,r.eax);
    /* the records */
    r.edx=R32(GVA(0x4a09f0u));
    a[0]=name;a[1]=r.edx;ep_call(g,0x492becu,a,2,&r);
    path=r.eax;
    W16(context,8u);
    r.eax=S;a[0]=S;a[1]=path;ep_call(g,0x472f14u,a,2,&r);
    r.eax=R32(S+4u);
    if(r.eax) { r.edx=0;ep_report(g,&r,0x497eb8u,0xa1u,path); }
    r.eax=set+4u;r.edx=S;r.ecx=R32(S);a[0]=S;a[1]=2;a[2]=set+4u;ep_read(m,g,r.ecx,a,&r);
    r.eax=sx(R16(set+4u));
    r.edx=r.eax*10u;a[0]=r.edx;ep_call(g,0x4774f0u,a,1,&r);
    r.ecx=a[0];                                             /* POP ECX */
    W32(set,r.eax);
    uint32_t edi=r.eax;
    for(uint32_t ebx=0;;++ebx,edi+=10u) {
        r.edx=sx(R16(set+4u));
        if(!(s32(ebx)<s32(r.edx)))break;
        r.eax=S;a[0]=edi;a[1]=S;ep_call(g,0x4083f8u,a,2,&r);
    }
    /* +26h of every shape = tag */
    if(tag) {
        for(uint32_t ebx=0;;) {
            r.eax=sx(R16(set+0xau));
            if(!(s32(ebx)<s32(r.eax)))break;
            r.edx=R32(set+6u);
            r.eax=tag;
            r.ecx=R32(r.edx+4u*ebx);
            ++ebx;
            W32(r.ecx+0x26u,r.eax);
        }
    }
    r.edx=S;a[0]=S;a[1]=2;ep_call(g,0x472f64u,a,2,&r);
    r.ecx=R32(f-0x50u);
    return ep_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x4083f8. */
uint64_t es2_debris_record_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t record,uint32_t stream,
    const uint32_t entry[3],uint32_t *ecx) {
    ep_regs r={entry[0],entry[1],entry[2]};uint32_t a[3];
    /* the two words of the pushed ECX slot: f-2 the maximum, f-4 the counter */
    uint16_t maximum,counter;
    r.eax=record;r.edx=R32(stream);a[0]=stream;a[1]=2;a[2]=record;ep_read(m,g,r.edx,a,&r);
    r.eax=record+2u;r.edx=R32(stream);a[0]=stream;a[1]=2;a[2]=record+2u;ep_read(m,g,r.edx,a,&r);
    r.ecx=sx(R16(record+2u));
    r.eax=r.ecx*14u;a[0]=r.eax;ep_call(g,0x4774f0u,a,1,&r);
    r.ecx=a[0];                                             /* POP ECX */
    W32(record+4u,r.eax);
    W16(record+8u,0);
    maximum=0;
    uint32_t piece=r.eax;
    counter=0;
    for(;;) {
        r.edx=lo(r.edx,R16(record+2u));
        if(!(s16(r.edx)>s16(counter)))break;
        for(uint32_t k=0;k<14u;k+=2u) {
            r.eax=piece+k;r.edx=R32(stream);a[0]=stream;a[1]=2;a[2]=piece+k;ep_read(m,g,r.edx,a,&r);
        }
        /* words +8 and +0Ah x 182 unless FFFFh */
        for(uint32_t k=8u;k<=0xau;k+=2u) {
            r.eax=lo(r.eax,R16(piece+k));
            if((uint16_t)r.eax!=0xffffu) {
                r.edx=sx(r.eax);
                r.ecx=r.edx*182u;
                W16(piece+k,r.ecx);
            }
        }
        r.eax=lo(r.eax,R16(piece+2u));
        W16(record+8u,R16(record+8u)+(uint16_t)r.eax);
        r.eax=lo(r.eax,R16(piece));
        if(s16(r.eax)>s16(maximum))maximum=(uint16_t)r.eax;
        ++counter;
        piece+=0xeu;
    }
    r.eax=lo(r.eax,maximum);
    r.ecx=(uint32_t)maximum<<16|counter;                    /* POP ECX */
    return ep_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x4097a8. */
uint64_t es2_flat_pools_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t flag,
    const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_EFFECT_POOL_FRAME,context=f-0x18u,pair=GVA(0x4a969eu);
    ep_regs r;uint32_t a[2];
    ep_record(g,0x4980b9u,entry,&r);
    if(R8(GVA(0x498098u))==0)W8(GVA(0x498098u),R8(GVA(0x498098u))+1u);
    const uint32_t ebx=(uint16_t)flag?GVA(0x498109u):GVA(0x49810fu);
    r.eax=R32(GVA(0x4a09f8u));
    a[0]=ebx;a[1]=r.eax;ep_call(g,0x492becu,a,2,&r);
    ep_shapes(g,&r,0x4a9698u,r.eax);
    W16(GVA(0x4a969cu),r.eax);
    /* the bitmap pair: 45cdf4, its descriptor 46a03c, the construction
     * count, the bitmap released (48349c(bitmap, 3)) and cleared */
    r.eax=R32(GVA(0x4a0a10u));
    ep_bitmap(g,&r,ebx,r.eax);
    const uint32_t bitmap=r.eax;
    W32(pair,bitmap);
    a[0]=bitmap;ep_call(g,0x46a03cu,a,1,&r);
    r.ecx=a[0];                                             /* POP ECX */
    W32(pair+4u,r.eax);
    r.eax=R32(GVA(0x6cbee4u));W32(r.eax,R32(r.eax)+1u);
    r.edx=R32(pair);a[0]=r.edx;a[1]=3;ep_call(g,0x48349cu,a,2,&r);
    r.ecx=0;W32(pair,0);
    /* +26h of every shape = the pair */
    for(r.eax=0;s16(r.eax)<s16(R16(GVA(0x4a969cu)));++r.eax) {
        r.edx=sx(r.eax);
        r.ecx=R32(GVA(0x4a9698u));
        r.edx=R32(r.ecx+4u*r.edx);
        W32(r.edx+0x26u,pair);
    }
    ep_pool(m,g,&r,f,context,0x96u,0x41u,0x4a968du,1);
    ep_list(m,g,&r,f,context,0x19u,0x4a9691u,1);
    r.eax=R32(f-0x28u);
    return ep_done(&r,ecx);
}

/* meteor_pools and smoke_balls: the pool, the list, the shapes and one
 * bitmap, +26h of every shape = the bitmap slot. */
static uint64_t ep_shape_pools(const es2_guest_memory *m,const es2_weapon_graph *g,const uint32_t entry[3],
    uint32_t *ecx,uint32_t descriptor,uint32_t capacity,uint32_t size,uint32_t pool,uint32_t shape_name,
    uint32_t shapes,uint32_t bitmap_name,uint32_t bitmap) {
    const uint32_t f=g->scratch+ES2_EFFECT_POOL_FRAME,context=f-0x18u;
    ep_regs r;uint32_t a[2];
    ep_record(g,descriptor,entry,&r);
    ep_pool(m,g,&r,f,context,capacity,size,pool,0);
    ep_list(m,g,&r,f,context,0x19u,pool+4u,0);
    r.eax=R32(GVA(0x4a09f8u));
    a[0]=GVA(shape_name);a[1]=r.eax;ep_call(g,0x492becu,a,2,&r);
    ep_shapes(g,&r,shapes,r.eax);
    const uint32_t ebx=r.eax;
    r.eax=R32(GVA(0x4a0a10u));
    ep_bitmap(g,&r,GVA(bitmap_name),r.eax);
    W32(GVA(bitmap),r.eax);
    r.eax=0;
    if(s16(ebx)>s16(r.eax)) {
        do {
            r.edx=sx(r.eax);
            r.ecx=R32(GVA(shapes));
            r.edx=R32(r.ecx+4u*r.edx);
            W32(r.edx+0x26u,GVA(bitmap));
            ++r.eax;
        } while(s16(ebx)>s16(r.eax));
    }
    r.eax=R32(f-0x28u);
    return ep_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x409a34. */
uint64_t es2_meteor_pools_registers(const es2_guest_memory *m,const es2_weapon_graph *g,const uint32_t entry[3],
    uint32_t *ecx) {
    return ep_shape_pools(m,g,entry,ecx,0x498134u,0xau,0x59u,0x4a96aau,0x49829cu,0x4a96b4u,0x4982a3u,0x4a96b8u);
}

/* Original: DBSIM.EXE 0x408fb8. */
uint64_t es2_smoke_balls_registers(const es2_guest_memory *m,const es2_weapon_graph *g,const uint32_t entry[3],
    uint32_t *ecx) {
    return ep_shape_pools(m,g,entry,ecx,0x497f12u,0xc8u,0x4cu,0x4a966eu,0x49804au,0x4a9678u,0x498050u,0x4a967cu);
}

/* Original: DBSIM.EXE 0x46b1a8. */
uint64_t es2_fire_pools_registers(const es2_guest_memory *m,const es2_weapon_graph *g,const uint32_t entry[3],
    uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_FIRE_POOLS_FRAME,context=f-0x48u,S=f-0x38u,base=f-0x60u,name=f-0x74u;
    ep_regs r;uint32_t a[6];
    ep_record(g,0x4a0aa9u,entry,&r);
    ep_pool(m,g,&r,f,context,0xau,0x5bu,0x6b4f30u,0);
    ep_list(m,g,&r,f,context,0xau,0x6b4f34u,0);
    /* the base name (five bytes from 4a0a74h) and its copy */
    r.eax=R32(GVA(0x4a0a74u));
    W32(base,r.eax);
    r.eax=lo8(r.eax,R8(GVA(0x4a0a78u)));
    W8(base+4u,r.eax);
    r.eax=0;
    ep_strcpy(m,&r,name,base);
    if(R16(GVA(0x4d2680u))) { r.eax=name;a[0]=name;a[1]=GVA(0x4a0b65u);ep_call(g,0x4967eau,a,2,&r); }
    /* the shapes */
    r.edx=R32(GVA(0x4a09f8u));
    r.ecx=name;a[0]=name;a[1]=r.edx;ep_call(g,0x492becu,a,2,&r);
    ep_shapes(g,&r,0x6b4f28u,r.eax);
    const uint32_t esi=r.eax;
    W16(context,0x2cu);
    /* the per-shape bytes */
    r.eax=R32(GVA(0x4a09f0u));
    r.edx=base;a[0]=base;a[1]=r.eax;ep_call(g,0x492becu,a,2,&r);
    r.ecx=S;a[0]=S;a[1]=r.eax;ep_call(g,0x472f14u,a,2,&r);
    r.eax=R32(S+4u);
    if(r.eax) {
        r.edx=base;
        r.ecx=lo8(r.ecx,0);
        r.ecx&=1u;
        a[0]=GVA(0x6c56c8u);a[1]=5;a[2]=r.ecx;a[3]=GVA(0x4a0b67u);a[4]=0x3bu;a[5]=base;ep_call(g,0x479c78u,a,6,&r);
    }
    r.eax=f-0x64u;r.edx=S;r.ecx=R32(S);a[0]=S;a[1]=4;a[2]=f-0x64u;ep_read(m,g,r.ecx,a,&r);
    uint32_t ebx=sx(esi);
    a[0]=ebx;ep_call(g,0x4774f0u,a,1,&r);
    r.ecx=a[0];                                             /* POP ECX */
    const uint32_t bytes=r.eax;
    r.edx=ebx;r.ecx=S;r.eax=R32(S);a[0]=S;a[1]=ebx;a[2]=bytes;ep_read(m,g,r.eax,a,&r);
    r.edx=S;a[0]=S;a[1]=2;ep_call(g,0x472f64u,a,2,&r);
    /* the two bitmap pairs: base name + number (+ suffix 4a0b70h) */
    a[0]=0;a[1]=8;a[2]=2;a[3]=1;a[4]=GVA(0x46bbc1u);ep_call(g,0x49667cu,a,5,&r);
    W32(GVA(0x6b4f2cu),r.eax);
    for(ebx=0;;) {
        r.eax=0;
        ep_strcpy(m,&r,name,base);
        r.eax=f-0x66u;
        a[0]=ebx;a[1]=r.eax;a[2]=10;ep_call(g,0x496820u,a,3,&r);
        r.edx=name;a[0]=name;a[1]=r.eax;ep_call(g,0x4967eau,a,2,&r);
        if(R16(GVA(0x4d2680u))) { r.ecx=name;a[0]=name;a[1]=GVA(0x4a0b70u);ep_call(g,0x4967eau,a,2,&r); }
        r.eax=R32(GVA(0x4a0a10u));
        r.edx=name;ep_bitmap(g,&r,name,r.eax);
        r.ecx=R32(GVA(0x6b4f2cu));
        W32(r.ecx+8u*ebx,r.eax);
        ++ebx;
        if(!(s32(ebx)<2))break;
    }
    /* +26h of each shape = the pair its byte names */
    r.eax=bytes;
    for(ebx=0;;++ebx,++r.eax) {
        r.edx=sx(esi);
        if(!(s32(ebx)<s32(r.edx)))break;
        r.edx=sx8(R8(r.eax));
        r.ecx=R32(GVA(0x6b4f28u));
        r.edx=(r.edx<<3)+R32(GVA(0x6b4f2cu));
        r.ecx=R32(r.ecx+4u*ebx);
        W32(r.ecx+0x26u,r.edx);
    }
    r.eax=0;
    W32(GVA(0x6b4f38u),0);
    r.eax=R32(f-0x58u);
    return ep_done(&r,ecx);
}
