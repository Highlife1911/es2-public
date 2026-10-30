#include "es2/core/building_types.h"
#include "fixed.h"
#include <stddef.h>
/* The building module's stage-2 callback and building pools
 * (docs/reverse-engineering/mission_load.md, section "building types and
 * pools"). Every guest read and write is done in the original's order (stubs
 * and aliasing objects observe it), and the register struct r follows
 * EAX/EDX/ECX so that each game call sees the original's entry registers. f is
 * the synthetic EBP: the locals and the runtime record lie at the original's
 * offsets below it. lo() is a 16-bit load that keeps the register's high word.
 * */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define GVA(x) (g->image+(uint32_t)(x)-0x400000u)
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
static int32_t s16(uint32_t x) { return fx_s16(x); }
static uint32_t sx8(uint32_t x) { return (uint32_t)(int32_t)(int8_t)(uint8_t)x; }
static int32_t s32(uint32_t x) { return (int32_t)fx_s32(x); }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static uint64_t bt_done(const es2_weapon_registers *r,uint32_t *ecx) {
    if(ecx)*ecx=r->ecx;
    return (uint64_t)r->edx<<32|r->eax;
}
/* One direct game call: the observer first (reference VA), then the bridge. */
static void bt_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,GVA(va),a,n,r);
}
/* The stream's read method: slot +18h of the table the original loaded
 * into a register from the stream object; arguments (S, size, buffer). */
static void bt_read(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t table,const uint32_t a[3],
    es2_weapon_registers *r) {
    const uint32_t target=R32(table+0x18u);
    if(g->observe)g->observe(g->observer_context,target,0x18u);
    g->call(g->context,target,a,3,r);
}
/* The fatal report after a failed open: 479c78(6C56C8h, 5, 0, file, line,
 * path); the caller has set the SETE register to 0 (the status is nonzero). */
static void bt_report(const es2_weapon_graph *g,es2_weapon_registers *r,uint32_t file,uint32_t line,uint32_t path) {
    const uint32_t a[6]={GVA(0x6c56c8u),5,0,GVA(file),line,path};
    bt_call(g,0x479c78u,a,6,r);
}
/* SETE reg8 of the status compare, AND reg,1. */
static uint32_t bt_sete(uint32_t status) { return status==0?1u:0u; }

/* bforms: the forms at 4a95ach (dword count 4a95a8h), stream S at f-60h,
 * loop counter f-E8h, the length byte f-E9h. */
static void bt_forms(const es2_guest_memory *m,const es2_weapon_graph *g,es2_weapon_registers *r,uint32_t f) {
    const uint32_t S=f-0x60u;uint32_t a[6];
    r->edx=R32(GVA(0x4a09f0u));
    a[0]=GVA(0x497712u);a[1]=r->edx;bt_call(g,0x492becu,a,2,r);
    W32(f-0xe4u,r->eax);
    W16(f-0xd0u,8u);
    r->ecx=R32(f-0xe4u);r->eax=S;a[0]=S;a[1]=r->ecx;bt_call(g,0x472f14u,a,2,r);
    if(R32(S+4u)) {
        r->edx=R32(f-0xe4u);
        r->ecx=bt_sete(R32(S+4u));
        bt_report(g,r,0x497719u,0x51au,r->edx);
    }
    r->eax=S;r->edx=R32(S);a[0]=S;a[1]=4;a[2]=GVA(0x4a95a8u);bt_read(m,g,r->edx,a,r);
    r->ecx=R32(GVA(0x4a95a8u));
    a[0]=0;a[1]=0x1cu;a[2]=r->ecx;a[3]=1;a[4]=GVA(0x406a5cu);bt_call(g,0x49667cu,a,5,r);
    uint32_t form=r->eax;
    W32(GVA(0x4a95acu),form);
    r->eax=0;
    W32(f-0xe8u,0);
    for(;;) {
        r->ecx=R32(f-0xe8u);
        if(!(s32(r->ecx)<s32(R32(GVA(0x4a95a8u)))))break;
        /* the element count +0, the elements +4 (10 bytes each) */
        r->eax=form;r->edx=S;r->ecx=R32(S);a[0]=S;a[1]=4;a[2]=form;bt_read(m,g,r->ecx,a,r);
        r->eax=R32(form);
        a[0]=0;a[1]=0xau;a[2]=r->eax;a[3]=1;a[4]=GVA(0x406a64u);bt_call(g,0x49667cu,a,5,r);
        W32(form+4u,r->eax);
        r->eax=R32(form)*10u;r->edx=R32(form+4u);r->ecx=S;
        a[0]=S;a[1]=r->eax;a[2]=r->edx;bt_call(g,0x4735b8u,a,3,r);
        /* +8 (4 bytes), +0Ch (8 bytes), the list count +14h */
        r->eax=form+8u;r->edx=S;r->ecx=R32(S);a[0]=S;a[1]=4;a[2]=form+8u;bt_read(m,g,r->ecx,a,r);
        r->eax=form+0xcu;r->edx=S;a[0]=S;a[1]=8;a[2]=form+0xcu;bt_call(g,0x4735b8u,a,3,r);
        r->eax=form+0x14u;r->edx=S;r->ecx=R32(S);a[0]=S;a[1]=4;a[2]=form+0x14u;bt_read(m,g,r->ecx,a,r);
        if(R32(form+0x14u)) {
            r->eax=R32(form+0x14u)<<2;a[0]=r->eax;bt_call(g,0x4774f0u,a,1,r);
            r->ecx=a[0];                                    /* POP ECX */
            W32(form+0x18u,r->eax);
            for(uint32_t i=0;s32(i)<s32(R32(form+0x14u));++i) {
                r->eax=R32(form+0x14u);a[0]=r->eax;bt_call(g,0x4774f0u,a,1,r);
                r->ecx=a[0];                                /* POP ECX */
                r->edx=R32(form+0x18u);
                W32(r->edx+4u*i,r->eax);
                /* the length byte into f-E9h, then that many bytes */
                r->ecx=f-0xe9u;r->eax=S;r->edx=R32(S);a[0]=S;a[1]=1;a[2]=f-0xe9u;bt_read(m,g,r->edx,a,r);
                r->edx=sx8(R8(f-0xe9u));
                r->ecx=R32(form+0x18u);
                const uint32_t buffer=R32(r->ecx+4u*i);
                r->ecx=S;r->eax=R32(S);a[0]=S;a[1]=r->edx;a[2]=buffer;bt_read(m,g,r->eax,a,r);
            }
        } else {
            r->edx=0;
            W32(form+0x18u,0);
        }
        W32(f-0xe8u,R32(f-0xe8u)+1u);
        form+=0x1cu;
    }
    r->eax=S;a[0]=S;a[1]=2;bt_call(g,0x472f64u,a,2,r);
}

/* lc_wpns: the records at 4a95bch (count word 4a95b8h), stream at f-90h,
 * the size word f-F2h. */
static void bt_weapons(const es2_guest_memory *m,const es2_weapon_graph *g,es2_weapon_registers *r,uint32_t f) {
    const uint32_t S=f-0x90u;uint32_t a[5];
    r->edx=R32(GVA(0x4a09f0u));
    a[0]=GVA(0x497722u);a[1]=r->edx;bt_call(g,0x492becu,a,2,r);
    W32(f-0xf0u,r->eax);
    W16(f-0xd0u,0x14u);
    r->ecx=R32(f-0xf0u);r->eax=S;a[0]=S;a[1]=r->ecx;bt_call(g,0x472f14u,a,2,r);
    if(R32(S+4u)) {
        r->edx=R32(f-0xf0u);
        r->ecx=bt_sete(R32(S+4u));
        bt_report(g,r,0x49772au,0x545u,r->edx);
    }
    r->eax=S;r->edx=R32(S);a[0]=S;a[1]=2;a[2]=GVA(0x4a95b8u);bt_read(m,g,r->edx,a,r);
    r->ecx=sx(R16(GVA(0x4a95b8u)));
    a[0]=0;a[1]=0x16u;a[2]=r->ecx;a[3]=1;a[4]=GVA(0x406a6cu);bt_call(g,0x49667cu,a,5,r);
    W32(GVA(0x4a95bcu),r->eax);
    r->eax=sx(R16(GVA(0x4a95b8u)));
    r->ecx=R32(GVA(0x4a95bcu));
    r->edx=r->eax*22u;                                      /* LEA x5, LEA x11, ADD */
    W16(f-0xf2u,r->edx);
    r->eax=sx(R16(f-0xf2u));
    r->edx=S;a[0]=S;a[1]=r->eax;a[2]=r->ecx;bt_call(g,0x4735b8u,a,3,r);
    r->ecx=S;a[0]=S;a[1]=2;bt_call(g,0x472f64u,a,2,r);
}

/* bases: the types at 4a959ch (count word 4a9598h), the 6-byte slots at
 * 4a95b0h, stream at f-30h (EDI, kept open until the end), counter word
 * f-FAh, the dead word f-FCh, the address slots f-100h..f-114h. */
static void bt_types(const es2_guest_memory *m,const es2_weapon_graph *g,es2_weapon_registers *r,uint32_t f) {
    const uint32_t S=f-0x30u;uint32_t a[5];
    r->eax=R32(GVA(0x4a09f0u));
    a[0]=GVA(0x497733u);a[1]=r->eax;bt_call(g,0x492becu,a,2,r);
    W32(f-0xf8u,r->eax);
    W16(f-0xd0u,0x20u);
    r->edx=R32(f-0xf8u);a[0]=S;a[1]=r->edx;bt_call(g,0x472f14u,a,2,r);
    if(R32(S+4u)) {
        r->ecx=R32(f-0xf8u);
        r->eax=bt_sete(R32(S+4u));
        bt_report(g,r,0x497739u,0x550u,r->ecx);
    }
    a[0]=S;a[1]=2;a[2]=GVA(0x4a9598u);bt_call(g,0x4735b8u,a,3,r);
    r->edx=sx(R16(GVA(0x4a9598u)));
    a[0]=0;a[1]=0x3cu;a[2]=r->edx;a[3]=1;a[4]=GVA(0x406a77u);bt_call(g,0x49667cu,a,5,r);
    W32(GVA(0x4a959cu),r->eax);
    r->edx=sx(R16(GVA(0x4a9598u)));
    uint32_t type=r->eax;
    r->ecx=r->edx*6u;a[0]=r->ecx;bt_call(g,0x4774f0u,a,1,r);
    r->ecx=a[0];                                            /* POP ECX */
    W32(GVA(0x4a95b0u),r->eax);
    W16(f-0xfau,0);
    for(;;) {
        r->ecx=lo(r->ecx,R16(f-0xfau));
        if(!(s16(r->ecx)<s16(R16(GVA(0x4a9598u)))))break;
        W16(f-0xfcu,0x34u);
        /* five words +0..+8 */
        for(uint32_t k=0;k<10u;k+=2u) {
            r->eax=type+k;r->edx=R32(S);a[0]=S;a[1]=2;a[2]=type+k;bt_read(m,g,r->edx,a,r);
        }
        r->ecx=type+0xau;a[0]=S;a[1]=6;a[2]=type+0xau;bt_call(g,0x4735b8u,a,3,r);
        r->eax=type+0x10u;r->edx=R32(S);a[0]=S;a[1]=2;a[2]=type+0x10u;bt_read(m,g,r->edx,a,r);
        r->eax=type+0x12u;r->edx=R32(S);a[0]=S;a[1]=2;a[2]=type+0x12u;bt_read(m,g,r->edx,a,r);
        /* the children: word +12h of them, 1Eh bytes each, array +14h */
        r->ecx=sx(R16(type+0x12u));
        a[0]=0;a[1]=0x1eu;a[2]=r->ecx;a[3]=1;a[4]=GVA(0x406a85u);bt_call(g,0x49667cu,a,5,r);
        W32(type+0x14u,r->eax);
        for(uint32_t i=0;;++i) {
            r->edx=sx(R16(type+0x12u));
            if(!(s32(i)<s32(r->edx)))break;
            r->eax=i*30u+R32(type+0x14u);
            a[0]=S;a[1]=0x1eu;a[2]=r->eax;bt_call(g,0x4735b8u,a,3,r);
        }
        r->ecx=type+0x18u;a[0]=S;a[1]=6;a[2]=type+0x18u;bt_call(g,0x4735b8u,a,3,r);
        r->eax=type+0x1eu;r->edx=R32(S);a[0]=S;a[1]=2;a[2]=type+0x1eu;bt_read(m,g,r->edx,a,r);
        r->ecx=type+0x20u;a[0]=S;a[1]=4;a[2]=type+0x20u;bt_call(g,0x4735b8u,a,3,r);
        r->eax=type+0x24u;r->edx=R32(S);a[0]=S;a[1]=2;a[2]=type+0x24u;bt_read(m,g,r->edx,a,r);
        r->eax=type+0x26u;r->edx=R32(S);a[0]=S;a[1]=2;a[2]=type+0x26u;bt_read(m,g,r->edx,a,r);
        /* six words +28h..+32h through the address slots f-100h..f-114h */
        for(uint32_t k=0;k<6u;++k) {
            const uint32_t slot=f-0x100u-4u*k;
            r->ecx=type+0x28u+2u*k;
            W32(slot,r->ecx);
            r->eax=R32(slot);
            r->edx=R32(S);a[0]=S;a[1]=2;a[2]=r->eax;bt_read(m,g,r->edx,a,r);
        }
        if(R16(type+0x30u)) {
            r->ecx=sx(R16(f-0xfau));
            r->eax=r->ecx*6u+R32(GVA(0x4a95b0u));
            W32(type+0x38u,r->eax);
        } else {
            r->edx=0;
            W32(type+0x38u,0);
        }
        W16(f-0xfau,R16(f-0xfau)+1u);
        type+=0x3cu;
    }
}

/* basecol: collision_read per type into the 6-byte slots, stream at
 * f-C0h. */
static void bt_collision(const es2_guest_memory *m,const es2_weapon_graph *g,es2_weapon_registers *r,uint32_t f) {
    const uint32_t S=f-0xc0u;uint32_t a[2];
    r->eax=R32(GVA(0x4a09f0u));
    a[0]=GVA(0x497742u);a[1]=r->eax;bt_call(g,0x492becu,a,2,r);
    W32(f-0x118u,r->eax);
    W16(f-0xd0u,0x2cu);
    r->edx=R32(f-0x118u);r->ecx=S;a[0]=S;a[1]=r->edx;bt_call(g,0x472f14u,a,2,r);
    uint32_t slot=R32(GVA(0x4a95b0u));
    if(R32(S+4u)) {
        r->eax=R32(f-0x118u);
        r->edx=bt_sete(R32(S+4u));
        bt_report(g,r,0x49774au,0x586u,r->eax);
    }
    for(uint32_t i=0;s16(i)<s16(R16(GVA(0x4a9598u)));++i) {
        r->eax=S;a[0]=slot;a[1]=S;bt_call(g,0x40ccf8u,a,2,r);
        slot+=6u;
    }
    r->edx=S;a[0]=S;a[1]=2;bt_call(g,0x472f64u,a,2,r);
}

/* The count words: 4a9572h = max over the types with word +6 nonzero of
 * (word +2) + 1, 4a9578h the same over the others, 4a9580h = max over all
 * of (word +4) + 1, each starting at 0 (INC of the 32-bit register,
 * compared and stored as words). EBX carries the stored words. */
static void bt_counts(const es2_guest_memory *m,const es2_weapon_graph *g,es2_weapon_registers *r,uint32_t f,
    uint32_t *ebx) {
    W16(GVA(0x4a9580u),0);
    r->eax=0;
    W16(GVA(0x4a9578u),0);
    W16(GVA(0x4a9572u),0);
    r->edx=R32(GVA(0x4a959cu));
    for(;;) {
        r->ecx=sx(R16(GVA(0x4a9598u)));
        if(!(s32(r->eax)<s32(r->ecx)))break;
        if(R16(r->edx+6u)) {
            r->ecx=lo(r->ecx,R16(r->edx+2u))+1u;
            W16(f-0x11au,r->ecx);
            r->ecx=lo(r->ecx,R16(GVA(0x4a9572u)));
            if(s16(r->ecx)<=s16(R16(f-0x11au)))r->ecx=lo(r->ecx,R16(f-0x11au));
            else r->ecx=lo(r->ecx,R16(GVA(0x4a9572u)));
            W16(GVA(0x4a9572u),r->ecx);
        } else {
            r->ecx=lo(r->ecx,R16(r->edx+2u))+1u;
            if(s16(r->ecx)>=s16(R16(GVA(0x4a9578u))))*ebx=r->ecx;
            else *ebx=lo(*ebx,R16(GVA(0x4a9578u)));
            W16(GVA(0x4a9578u),*ebx);
        }
        r->ecx=lo(r->ecx,R16(r->edx+4u))+1u;
        if(s16(r->ecx)>=s16(R16(GVA(0x4a9580u))))*ebx=r->ecx;
        else *ebx=lo(*ebx,R16(GVA(0x4a9580u)));
        W16(GVA(0x4a9580u),*ebx);
        ++r->eax;
        r->edx+=0x3cu;
    }
}

/* One texture: the bitmap of `name` (45cdf4) into `slot`, its descriptor
 * (46a03c) into slot+4, the construction count, the bitmap released
 * (48349c(bitmap, 3)) and the slot cleared. The register that carries the
 * directory, the counter pointer and the release argument differs between
 * the two copies (`second`). */
static void bt_texture(const es2_guest_memory *m,const es2_weapon_graph *g,es2_weapon_registers *r,uint32_t name,
    uint32_t slot,int second) {
    uint32_t a[2];
    if(second) { r->edx=R32(GVA(0x4a0a10u));a[1]=r->edx; } else { r->eax=R32(GVA(0x4a0a10u));a[1]=r->eax; }
    a[0]=GVA(name);bt_call(g,0x492becu,a,2,r);
    a[0]=r->eax;bt_call(g,0x45cdf4u,a,1,r);
    r->ecx=a[0];                                            /* POP ECX */
    W32(GVA(slot),r->eax);
    if(second) { r->ecx=R32(GVA(slot));a[0]=r->ecx; } else { r->eax=R32(GVA(slot));a[0]=r->eax; }
    bt_call(g,0x46a03cu,a,1,r);
    r->ecx=a[0];                                            /* POP ECX */
    W32(GVA(slot+4u),r->eax);
    if(second) {
        r->eax=R32(GVA(0x6cbee4u));W32(r->eax,R32(r->eax)+1u);
        r->edx=R32(GVA(slot));a[0]=r->edx;
    } else {
        r->edx=R32(GVA(0x6cbee4u));W32(r->edx,R32(r->edx)+1u);
        r->ecx=R32(GVA(slot));a[0]=r->ecx;
    }
    a[1]=3;bt_call(g,0x48349cu,a,2,r);
    if(second) { r->ecx=0;W32(GVA(slot),0); } else { r->eax=0;W32(GVA(slot),0); }
}

/* Original: DBSIM.EXE 0x405fac. */
uint64_t es2_base_types_registers(const es2_guest_memory *m,const es2_weapon_graph *g,const uint32_t entry[3],
    uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_BASE_TYPES_FRAME;
    es2_weapon_registers r={GVA(0x49766fu),entry[1],entry[2]};uint32_t a[4],ebx=0;
    bt_call(g,0x49630cu,NULL,0,&r);
    bt_forms(m,g,&r,f);
    bt_weapons(m,g,&r,f);
    bt_types(m,g,&r,f);
    bt_collision(m,g,&r,f);
    bt_counts(m,g,&r,f,&ebx);
    /* the hulk shapes */
    a[0]=GVA(0x497753u);a[1]=GVA(0x49775au);bt_call(g,0x492becu,a,2,&r);
    W32(f-0xf8u,r.eax);
    r.eax=R32(f-0xf8u);
    a[0]=GVA(0x4a9584u);a[1]=r.eax;a[2]=0x2bc0001u;a[3]=0;bt_call(g,0x474cd8u,a,4,&r);
    r.edx=sx(R16(GVA(0x4a9578u)));
    W16(f-0x11cu,r.eax);
    r.edx<<=2;a[0]=r.edx;bt_call(g,0x4774f0u,a,1,&r);
    r.ecx=a[0];                                             /* POP ECX */
    W32(GVA(0x4a957cu),r.eax);
    r.eax=sx(R16(GVA(0x4a9572u)))<<2;a[0]=r.eax;bt_call(g,0x4774f0u,a,1,&r);
    r.ecx=a[0];                                             /* POP ECX */
    W32(GVA(0x4a9574u),r.eax);
    for(r.eax=0;;++r.eax) {
        r.edx=sx(R16(GVA(0x4a9578u)));
        if(!(s32(r.eax)<s32(r.edx)))break;
        r.edx=R32(GVA(0x4a957cu));r.ecx=0;
        W32(r.edx+4u*r.eax,0);
    }
    for(r.eax=0;;++r.eax) {
        r.ecx=sx(R16(GVA(0x4a9572u)));
        if(!(s32(r.eax)<s32(r.ecx)))break;
        r.ecx=R32(GVA(0x4a9574u));r.edx=0;
        W32(r.ecx+4u*r.eax,0);
    }
    /* the textures */
    bt_texture(m,g,&r,0x49775eu,0x4a9588u,0);
    bt_texture(m,g,&r,0x497766u,0x4a9590u,1);
    /* +26h of each hulk shape = 4a9588h */
    for(r.eax=0;;) {
        r.edx=sx(R16(GVA(0x4a9580u)));
        if(!(s32(r.eax)<s32(r.edx)))break;
        r.edx=R32(GVA(0x4a9584u));
        r.ecx=R32(r.edx+4u*r.eax);
        ++r.eax;
        W32(r.ecx+0x26u,GVA(0x4a9588u));
    }
    /* the debris set, then the bases stream closed */
    a[0]=GVA(0x4a95c0u);a[1]=GVA(0x49776du);a[2]=0;bt_call(g,0x40874cu,a,3,&r);
    a[0]=f-0x30u;a[1]=2;bt_call(g,0x472f64u,a,2,&r);
    r.eax=R32(f-0xe0u);
    return bt_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x405df4. */
uint64_t es2_base_pools_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t count,uint32_t ebx,
    const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_BASE_POOLS_FRAME,context=f-0x18u;
    es2_weapon_registers r={GVA(0x497603u),entry[1],entry[2]};uint32_t a[3];
    bt_call(g,0x49630cu,NULL,0,&r);
    ebx=lo(ebx,count);                                      /* MOV BX,[EBP+8] */
    /* the pool: new(12h), pool_create(p, count, 26Dh) */
    a[0]=0x12u;bt_call(g,0x47749cu,a,1,&r);
    r.ecx=0x12u;                                            /* POP ECX */
    W32(f-4u,r.eax);
    if(r.eax) {
        W16(context,0x14u);
        r.edx=R32(f-4u);
        a[0]=r.edx;a[1]=ebx;a[2]=0x26du;bt_call(g,0x471ad8u,a,3,&r);
        W16(context,8u);
    }
    r.ecx=R32(f-4u);
    W32(GVA(0x4a95a0u),r.ecx);
    /* the removal list: new(8), word +6 = 25, [list] = new[](100), +4 = 0 */
    a[0]=8u;bt_call(g,0x47749cu,a,1,&r);
    r.ecx=8u;                                               /* POP ECX */
    W32(f-8u,r.eax);
    if(r.eax) {
        W16(context,0x20u);
        r.edx=R32(f-8u);
        r.eax=lo(r.eax,0x19u);                              /* MOV AX,19h */
        W16(r.edx+6u,0x19u);
        r.edx=sx(0x19u)<<2;
        a[0]=r.edx;bt_call(g,0x4774f0u,a,1,&r);
        r.ecx=R32(f-8u);                                    /* after POP ECX */
        W32(r.ecx,r.eax);
        r.eax=R32(f-8u);
        W16(r.eax+4u,0);
        W16(context,8u);
    }
    r.edx=R32(f-8u);
    W32(GVA(0x4a95a4u),r.edx);
    r.eax=R32(f-0x28u);
    return bt_done(&r,ecx);
}
