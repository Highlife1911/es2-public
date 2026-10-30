#include "es2/core/stage2_types.h"
#include "fixed.h"
#include <stddef.h>
/* The stage-2 callbacks with type data and the weapon-list entry
 * (docs/reverse-engineering/mission_load.md, section "stage-2 callbacks with
 * type data"). Every guest read and write is done in the original's order
 * (stubs and aliasing objects observe it), and the register struct r follows
 * EAX/EDX/ECX so that each game call sees the original's entry registers. f is
 * the synthetic EBP: the runtime record lies at f-50h (context word f-40h), the
 * stream object at f-30h, beam_types' count word at f-52h. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define GVA(x) (g->image+(uint32_t)(x)-0x400000u)
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
static int32_t s16(uint32_t x) { return fx_s16(x); }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static uint64_t st_done(const es2_weapon_registers *r,uint32_t *ecx) {
    if(ecx)*ecx=r->ecx;
    return (uint64_t)r->edx<<32|r->eax;
}
/* One direct game call: the observer first (reference VA), then the bridge. */
static void st_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,GVA(va),a,n,r);
}
/* The stream's read method: slot +18h of the table the original loaded into
 * a register; arguments (S, size, buffer). */
static void st_read(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t table,const uint32_t a[3],
    es2_weapon_registers *r) {
    const uint32_t target=R32(table+0x18u);
    if(g->observe)g->observe(g->observer_context,target,0x18u);
    g->call(g->context,target,a,3,r);
}
/* The file open shared by the six callbacks, from the context word on:
 * f-40h = 8, 472f14(S, name) with EAX = S, then EAX = the status [S+4]; a
 * nonzero status reports through 479c78(6c56c8h, 5, 0, file, line, name)
 * with EDX = 0 (SETE DL of a nonzero EAX, AND EDX,1). */
static void st_open(const es2_guest_memory *m,const es2_weapon_graph *g,es2_weapon_registers *r,uint32_t f,
    uint32_t name,uint32_t file,uint32_t line) {
    const uint32_t S=f-0x30u;uint32_t a[6];
    W16(f-0x40u,8u);
    r->eax=S;a[0]=S;a[1]=name;st_call(g,0x472f14u,a,2,r);
    r->eax=R32(S+4u);
    if(r->eax) {
        r->edx=0;
        a[0]=GVA(0x6c56c8u);a[1]=5;a[2]=0;a[3]=GVA(file);a[4]=line;a[5]=name;
        st_call(g,0x479c78u,a,6,r);
    }
}
/* The path builder 492bec(template, directory). */
static void st_path(const es2_weapon_graph *g,es2_weapon_registers *r,uint32_t text,uint32_t directory) {
    const uint32_t a[2]={GVA(text),directory};
    st_call(g,0x492becu,a,2,r);
}
/* The construction count [[6CBEE4h]] incremented (INC DWORD [EAX]); EAX =
 * the counter pointer. */
static void st_count(const es2_guest_memory *m,const es2_weapon_graph *g,es2_weapon_registers *r) {
    r->eax=R32(GVA(0x6cbee4u));
    W32(r->eax,R32(r->eax)+1u);
}
/* The running maximum of the type loops (40a8ef, 40aec5): v = the record
 * word + 1 (INC of the 32-bit register, compared as a word); a signed v at
 * least the word at `max` replaces it, otherwise the word is rewritten. */
static void st_max(const es2_guest_memory *m,uint32_t max,uint32_t v) {
    const uint32_t word=s16(v)>=s16(R16(max))?v:R16(max);
    W16(max,word);
}

/* Original: DBSIM.EXE 0x40fc8c. ESI = 4a9ae8h until the weapon list. */
uint64_t es2_weapon_types_registers(const es2_guest_memory *m,const es2_weapon_graph *g,const uint32_t entry[3],
    uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_STAGE2_TYPES_FRAME,S=f-0x30u,pair=GVA(0x4a9ae8u);
    es2_weapon_registers r={GVA(0x498f8du),entry[1],entry[2]};uint32_t a[6];
    st_call(g,0x49630cu,NULL,0,&r);
    /* the weapon texture */
    r.edx=R32(GVA(0x4a0a10u));st_path(g,&r,0x4991f5u,r.edx);
    const uint32_t texture=r.eax;
    a[0]=texture;st_call(g,0x45cdf4u,a,1,&r);
    r.ecx=texture;                                  /* POP ECX */
    const uint32_t bitmap=r.eax;
    W32(pair,bitmap);
    a[0]=bitmap;st_call(g,0x46a03cu,a,1,&r);
    r.ecx=bitmap;                                   /* POP ECX */
    W32(pair+4u,r.eax);
    st_count(m,g,&r);
    r.edx=R32(pair);a[0]=r.edx;a[1]=3;st_call(g,0x48349cu,a,2,&r);
    r.ecx=0;
    W32(pair,0);
    /* the weapon shapes */
    r.eax=GVA(0x4991fcu);r.edx=R32(GVA(0x4a09f8u));st_path(g,&r,0x4991fcu,r.edx);
    a[0]=GVA(0x4a98f4u);a[1]=r.eax;a[2]=0x140008u;a[3]=0;st_call(g,0x474cd8u,a,4,&r);
    W16(GVA(0x4a98f8u),r.eax);
    for(uint32_t k=0;;) {
        r.ecx=sx(R16(GVA(0x4a98f8u)));
        if(!((int32_t)k<(int32_t)r.ecx))break;
        r.eax=R32(GVA(0x4a98f4u));
        r.edx=R32(r.eax+4u*k);
        ++k;
        W32(r.edx+0x26u,pair);
    }
    /* the weapon list */
    r.eax=R32(GVA(0x4a09f0u));st_path(g,&r,0x499205u,r.eax);
    st_open(m,g,&r,f,r.eax,0x49920du,0x10bu);
    r.ecx=S;a[0]=S;a[1]=2;a[2]=GVA(0x4a98f0u);st_call(g,0x4735b8u,a,3,&r);
    r.eax=sx(R16(GVA(0x4a98f0u)));
    a[0]=0;a[1]=0x58u;a[2]=r.eax;a[3]=1;a[4]=GVA(0x41178du);st_call(g,0x49667cu,a,5,&r);
    uint32_t item=r.eax,names=GVA(0x498eb0u);
    W32(GVA(0x4a98ecu),item);
    for(uint32_t k=0;;) {
        r.ecx=sx(R16(GVA(0x4a98f0u)));
        if(!((int32_t)k<(int32_t)r.ecx))break;
        r.eax=S;a[0]=item;a[1]=S;st_call(g,0x40f8bcu,a,2,&r);
        r.edx=R32(names);
        names+=4u;
        W32(item+0x52u,r.edx);
        W16(item+0x56u,k);
        ++k;
        item+=0x58u;
    }
    r.eax=S;a[0]=S;st_call(g,0x47307cu,a,1,&r);
    r.ecx=S;                                        /* POP ECX */
    /* the projectile table on the same stream */
    r.edx=R32(GVA(0x4a09f0u));st_path(g,&r,0x499219u,r.edx);
    a[0]=S;a[1]=r.eax;r.eax=S;st_call(g,0x4730e8u,a,2,&r);
    /* 40fe23: the read method through the table reread from S */
    r.edx=S;r.ecx=R32(S);a[0]=S;a[1]=2;a[2]=GVA(0x4a9900u);st_read(m,g,r.ecx,a,&r);
    const uint32_t size=sx(R16(GVA(0x4a9900u)))*36u;
    r.eax=size;a[0]=size;st_call(g,0x4774f0u,a,1,&r);
    r.ecx=size;                                     /* POP ECX */
    const uint32_t table=r.eax;
    W32(GVA(0x4a98fcu),table);
    r.eax=sx(size);r.edx=S;a[0]=S;a[1]=r.eax;a[2]=table;st_call(g,0x4735b8u,a,3,&r);
    r.ecx=S;a[0]=S;a[1]=2;st_call(g,0x472f64u,a,2,&r);
    r.eax=R32(f-0x50u);
    return st_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x40f8bc (E, S). */
uint64_t es2_weapon_entry_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t entry_record,
    uint32_t stream,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t E=entry_record,S=stream;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[3];
    a[0]=E;a[1]=S;st_call(g,0x40cff8u,a,2,&r);
    r.eax=E+0x12u;a[0]=r.eax;a[1]=S;st_call(g,0x40cc14u,a,2,&r);
    /* 40f8df: MOV DI,30h; MOVSX EDX,DI; ECX = [S] */
    r.eax=E+0x22u;r.edx=0x30u;r.ecx=R32(S);a[0]=S;a[1]=0x30u;a[2]=E+0x22u;st_read(m,g,r.ecx,a,&r);
    r.eax&=0xffu;
    W16(E+0x48u,0);
    return st_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x40a818. ESI = 4a96dch. */
uint64_t es2_rocket_types_registers(const es2_guest_memory *m,const es2_weapon_graph *g,const uint32_t entry[3],
    uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_STAGE2_TYPES_FRAME,S=f-0x30u,count=GVA(0x4a96d4u),max=GVA(0x4a96dcu);
    es2_weapon_registers r={GVA(0x49834cu),entry[1],entry[2]};uint32_t a[4];
    st_call(g,0x49630cu,NULL,0,&r);
    r.edx=R32(GVA(0x4a09f0u));st_path(g,&r,0x498420u,r.edx);
    st_open(m,g,&r,f,r.eax,0x498428u,0xe6u);
    r.ecx=S;a[0]=S;a[1]=2;a[2]=count;st_call(g,0x4735b8u,a,3,&r);
    r.eax=sx(R16(count));r.edx=r.eax*14u;a[0]=r.edx;st_call(g,0x4774f0u,a,1,&r);
    r.ecx=a[0];                                     /* POP ECX */
    const uint32_t records=r.eax;
    W32(GVA(0x4a96d0u),records);
    r.eax=sx(R16(count));r.edx=r.eax*14u;r.ecx=S;a[0]=S;a[1]=r.edx;a[2]=records;st_call(g,0x4735b8u,a,3,&r);
    r.eax=S;a[0]=S;a[1]=2;st_call(g,0x472f64u,a,2,&r);
    W16(max,0);
    r.ecx=0;r.edx=R32(GVA(0x4a96d0u));
    while(s16(r.ecx)<s16(R16(count))) {             /* CMP CX,[4a96d4h]; JL */
        r.eax=lo(r.eax,R16(r.edx))+1u;
        st_max(m,max,r.eax);
        ++r.ecx;
        r.edx+=14u;
    }
    r.eax=R32(GVA(0x4a09f8u));st_path(g,&r,0x498433u,r.eax);
    a[0]=GVA(0x4a96d8u);a[1]=r.eax;a[2]=0x140008u;a[3]=0;st_call(g,0x474cd8u,a,4,&r);
    r.eax=R32(f-0x50u);
    return st_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x40ade0. ESI = 4a9704h, then the bitmap. */
uint64_t es2_bullet_types_registers(const es2_guest_memory *m,const es2_weapon_graph *g,const uint32_t entry[3],
    uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_STAGE2_TYPES_FRAME,S=f-0x30u,count=GVA(0x4a96fcu),max=GVA(0x4a9704u);
    es2_weapon_registers r={GVA(0x498525u),entry[1],entry[2]};uint32_t a[4];
    st_call(g,0x49630cu,NULL,0,&r);
    if(!R8(GVA(0x498514u)))W8(GVA(0x498514u),R8(GVA(0x498514u))+1u);
    r.edx=R32(GVA(0x4a09f0u));st_path(g,&r,0x4985f9u,r.edx);
    st_open(m,g,&r,f,r.eax,0x498601u,0x32u);
    r.ecx=S;a[0]=S;a[1]=2;a[2]=count;st_call(g,0x4735b8u,a,3,&r);
    r.eax=sx(R16(count));r.edx=r.eax*14u;a[0]=r.edx;st_call(g,0x4774f0u,a,1,&r);
    r.ecx=a[0];                                     /* POP ECX */
    const uint32_t records=r.eax;
    W32(GVA(0x4a96f8u),records);
    r.eax=sx(R16(count));r.edx=r.eax*14u;r.ecx=S;a[0]=S;a[1]=r.edx;a[2]=records;st_call(g,0x4735b8u,a,3,&r);
    r.eax=S;a[0]=S;a[1]=2;st_call(g,0x472f64u,a,2,&r);
    W16(max,0);
    r.eax=0;r.ecx=R32(GVA(0x4a96f8u));
    for(;;) {
        r.edx=sx(R16(count));
        if(!((int32_t)r.eax<(int32_t)r.edx))break;
        st_max(m,max,lo(r.edx,R16(r.ecx))+1u);     /* MOV DX,[ECX]; INC EDX */
        ++r.eax;
        r.ecx+=14u;
    }
    r.eax=R32(GVA(0x4a09f8u));st_path(g,&r,0x49860cu,r.eax);
    a[0]=GVA(0x4a9700u);a[1]=r.eax;a[2]=0x140008u;a[3]=0;st_call(g,0x474cd8u,a,4,&r);
    const uint32_t shapes=r.eax;                    /* EBX; the loop tests BX */
    r.eax=R32(GVA(0x4a0a10u));st_path(g,&r,0x498614u,r.eax);
    const uint32_t name=r.eax;
    a[0]=name;st_call(g,0x45cdf4u,a,1,&r);
    r.ecx=name;                                     /* POP ECX */
    const uint32_t bitmap=r.eax;
    W32(GVA(0x4a9706u),bitmap);
    a[0]=bitmap;st_call(g,0x46a03cu,a,1,&r);
    r.ecx=bitmap;                                   /* POP ECX */
    W32(GVA(0x4a970au),r.eax);
    r.eax=0;
    for(;;) {
        r.edx=sx(shapes);
        if(!((int32_t)r.eax<(int32_t)r.edx))break;
        r.edx=R32(GVA(0x4a9700u));
        r.ecx=R32(r.edx+4u*r.eax);
        ++r.eax;
        W32(r.ecx+0x26u,GVA(0x4a9706u));
    }
    r.eax=R32(f-0x50u);
    return st_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x40b6e0. Frame 54h: the count word at f-52h. */
uint64_t es2_beam_types_registers(const es2_guest_memory *m,const es2_weapon_graph *g,const uint32_t entry[3],
    uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_STAGE2_TYPES_FRAME,S=f-0x30u,local=f-0x52u;
    es2_weapon_registers r={GVA(0x498658u),entry[1],entry[2]};uint32_t a[3];
    st_call(g,0x49630cu,NULL,0,&r);
    /* 40b6f1: 120 words (i SAR 1) + [498640h + 2 (i AND 3)] */
    r.eax=0;r.edx=GVA(0x4a9712u);
    do {
        r.ecx=sx(r.eax);
        uint32_t ebx=fx_sar(r.ecx,1);
        r.ecx&=3u;
        ebx=lo(ebx,ebx+R16(GVA(0x498640u)+2u*r.ecx));
        W16(r.edx,ebx);
        ++r.eax;
        r.edx+=2u;
    } while(s16(r.eax)<0x78);
    r.eax=R32(GVA(0x4a09f0u));st_path(g,&r,0x49877cu,r.eax);
    st_open(m,g,&r,f,r.eax,0x498781u,0x30u);
    /* 40b766: the read method through the table in EDX */
    r.ecx=local;r.eax=S;r.edx=R32(S);a[0]=S;a[1]=2;a[2]=local;st_read(m,g,r.edx,a,&r);
    r.ecx=sx(R16(local));r.eax=r.ecx*6u;a[0]=r.eax;st_call(g,0x4774f0u,a,1,&r);
    r.ecx=a[0];                                     /* POP ECX */
    const uint32_t records=r.eax;
    W32(GVA(0x4a9804u),records);
    r.eax=sx(R16(local));r.edx=r.eax*6u;r.ecx=S;a[0]=S;a[1]=r.edx;a[2]=records;st_call(g,0x4735b8u,a,3,&r);
    r.eax=S;a[0]=S;a[1]=2;st_call(g,0x472f64u,a,2,&r);
    r.edx=R32(GVA(0x4a0a10u));st_path(g,&r,0x49878au,r.edx);
    const uint32_t name=r.eax;
    a[0]=name;st_call(g,0x45cdf4u,a,1,&r);
    r.ecx=name;                                     /* POP ECX */
    const uint32_t bitmap=r.eax;                    /* EBX */
    a[0]=bitmap;st_call(g,0x46a03cu,a,1,&r);
    r.ecx=bitmap;                                   /* POP ECX */
    W32(GVA(0x4a9808u),r.eax);
    st_count(m,g,&r);
    a[0]=bitmap;a[1]=3;st_call(g,0x48349cu,a,2,&r);
    r.edx=R32(f-0x50u);
    return st_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x41fdb0. EBX walks the bitmap pairs from 4a9d72h,
 * ESI the name table from 49a360h, DI counts to 7. */
uint64_t es2_mech_forms_registers(const es2_guest_memory *m,const es2_weapon_graph *g,const uint32_t entry[3],
    uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_STAGE2_TYPES_FRAME,S=f-0x30u,count=GVA(0x4a9d68u);
    es2_weapon_registers r={GVA(0x49a38cu),entry[1],entry[2]};uint32_t a[5];
    st_call(g,0x49630cu,NULL,0,&r);
    a[0]=GVA(0x4a9d08u);a[1]=0xffu;a[2]=0x55u;st_call(g,0x49681au,a,3,&r);
    W16(GVA(0x4a9d06u),0);
    r.edx=R32(GVA(0x4a09f0u));st_path(g,&r,0x49a46bu,r.edx);
    st_open(m,g,&r,f,r.eax,0x49a472u,0x34u);
    r.ecx=S;a[0]=S;a[1]=2;a[2]=count;st_call(g,0x4735b8u,a,3,&r);
    r.eax=sx(R16(count));
    a[0]=0;a[1]=0x1cu;a[2]=r.eax;a[3]=1;a[4]=GVA(0x4207bbu);st_call(g,0x49667cu,a,5,&r);
    const uint32_t forms=r.eax;
    W32(GVA(0x4a9d6cu),forms);
    r.edx=sx(R16(count));
    r.eax=((r.edx<<3)-r.edx)<<2;
    r.ecx=sx(r.eax);r.eax=S;a[0]=S;a[1]=r.ecx;a[2]=forms;st_call(g,0x4735b8u,a,3,&r);
    uint32_t pairs=GVA(0x4a9d72u),texts=GVA(0x49a360u);
    for(uint32_t k=0;;) {
        r.eax=R32(GVA(0x4a0a10u));r.edx=R32(texts);
        a[0]=r.edx;a[1]=r.eax;st_call(g,0x492becu,a,2,&r);
        const uint32_t name=r.eax;
        a[0]=name;st_call(g,0x45cdf4u,a,1,&r);
        r.ecx=name;                                 /* POP ECX */
        W32(pairs,r.eax);
        r.ecx=R32(pairs);
        const uint32_t bitmap=r.ecx;
        a[0]=bitmap;st_call(g,0x46a03cu,a,1,&r);
        r.ecx=bitmap;                               /* POP ECX */
        W32(pairs+4u,r.eax);
        st_count(m,g,&r);
        r.edx=R32(pairs);a[0]=r.edx;a[1]=3;st_call(g,0x48349cu,a,2,&r);
        r.ecx=0;
        W32(pairs,0);
        ++k;
        pairs+=8u;
        texts+=4u;
        if(!(s16(k)<7))break;                       /* CMP DI,7; JL */
    }
    r.eax=S;a[0]=S;a[1]=2;st_call(g,0x472f64u,a,2,&r);
    r.edx=R32(f-0x50u);
    return st_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x422d90. */
uint64_t es2_flyer_forms_registers(const es2_guest_memory *m,const es2_weapon_graph *g,const uint32_t entry[3],
    uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_STAGE2_TYPES_FRAME,S=f-0x30u,count=GVA(0x4a9df8u);
    es2_weapon_registers r={GVA(0x49a678u),entry[1],entry[2]};uint32_t a[5];
    st_call(g,0x49630cu,NULL,0,&r);
    a[0]=GVA(0x4a9dccu);a[1]=0xffu;a[2]=0x22u;st_call(g,0x49681au,a,3,&r);
    W16(GVA(0x4a9dcau),0);
    r.edx=R32(GVA(0x4a09f0u));st_path(g,&r,0x49a68cu,r.edx);
    st_open(m,g,&r,f,r.eax,0x49a693u,0x26u);
    r.ecx=S;a[0]=S;a[1]=2;a[2]=count;st_call(g,0x4735b8u,a,3,&r);
    r.eax=sx(R16(count));
    a[0]=0;a[1]=0x12u;a[2]=r.eax;a[3]=1;a[4]=GVA(0x423078u);st_call(g,0x49667cu,a,5,&r);
    const uint32_t forms=r.eax;
    W32(GVA(0x4a9dfcu),forms);
    r.eax=sx(R16(count))*18u;
    r.edx=sx(r.eax);r.ecx=S;a[0]=S;a[1]=r.edx;a[2]=forms;st_call(g,0x4735b8u,a,3,&r);
    r.eax=S;a[0]=S;a[1]=2;st_call(g,0x472f64u,a,2,&r);
    r.edx=R32(f-0x50u);
    return st_done(&r,ecx);
}
