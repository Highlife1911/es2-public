#include "es2/core/collision_data.h"
#include "fixed.h"
#include <stddef.h>
/* The collision data of collide.cpp (docs/reverse-engineering/ mission_load.md,
 * section "collision data"). Every guest read and write is done in the
 * original's order (objects may overlap), and the register struct r follows
 * EAX/EDX/ECX so that each game call sees the original's entry registers; lo()
 * is a 16-bit load that keeps the register's high word. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define GVA(x) (g->image+(uint32_t)(x)-0x400000u)
static uint64_t cd_pair(uint32_t a,uint32_t d) { return (uint64_t)d<<32|a; }
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
static int32_t s16(uint32_t x) { return fx_s16(x); }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static uint16_t sar1(uint32_t word) { return (uint16_t)fx_sar((uint32_t)fx_s16(word),1); }
static uint64_t cd_done(const es2_weapon_registers *r,uint32_t *ecx) { if(ecx)*ecx=r->ecx;return cd_pair(r->eax,r->edx); }
/* One direct game call: the observer first (reference VA), then the bridge. */
static void cd_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,GVA(va),a,n,r);
}
/* The stream's read, slot +18h of the vtable the caller loaded. */
static void cd_read(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t vtable,const uint32_t *a,
    es2_weapon_registers *r) {
    const uint32_t target=R32(vtable+0x18u);
    if(g->observe)g->observe(g->observer_context,target,0x18u);
    g->call(g->context,target,a,3,r);
}

/* Original: DBSIM.EXE 0x40cd88. Frame: the record f-50h (context word
 * f-40h), the stream object f-30h; EBX = the name, then the slot. */
uint64_t es2_collision_load_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t name,
    const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_COLLISION_LOAD_FRAME,S=f-0x30u;
    es2_weapon_registers r={GVA(0x4987f0u),entry[1],entry[2]};uint32_t a[6];
    cd_call(g,0x49630cu,NULL,0,&r);
    W16(f-0x40u,8);
    r.edx=S;a[0]=S;a[1]=name;cd_call(g,0x472f14u,a,2,&r);
    r.eax=R32(S+4u);
    if(r.eax) {
        /* 40cdc1: TEST EAX,EAX; SETE DL; AND EDX,1 gives 0. */
        r.edx=0;
        a[0]=GVA(0x6c56c8u);a[1]=5;a[2]=r.edx;a[3]=GVA(0x498824u);a[4]=0x101u;a[5]=name;
        cd_call(g,0x479c78u,a,6,&r);
    }
    /* 40cdd9: MOV CX keeps ECX's high word; INC WORD rereads the counter. */
    r.ecx=lo(r.ecx,R16(GVA(0x4987deu)));
    r.eax=sx(r.ecx);
    W16(GVA(0x4987deu),R16(GVA(0x4987deu))+1u);
    uint32_t slot=6u*r.eax;
    r.eax=S;
    slot+=R32(GVA(0x4a9824u));
    a[0]=slot;a[1]=S;cd_call(g,0x40ccf8u,a,2,&r);
    /* 40ce04: the slot is pushed above the two arguments and popped into
     * EAX afterwards; 472f64 reads two slots. */
    r.eax=slot;r.edx=S;a[0]=S;a[1]=2;cd_call(g,0x472f64u,a,2,&r);
    r.eax=slot;
    r.edx=R32(f-0x50u);                                /* the saved FS:0 head */
    return cd_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x40ccf8. ESI = D, EBX = the group cursor, EDI = i. */
uint64_t es2_collision_read_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t descriptor,
    uint32_t stream,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[6];
    r.eax=R32(stream+4u);
    if(r.eax) {
        r.edx=0;                                       /* SETE DL; AND EDX,1 */
        a[0]=GVA(0x6c56c8u);a[1]=5;a[2]=r.edx;a[3]=GVA(0x498804u);a[4]=0xeeu;a[5]=GVA(0x498810u);
        cd_call(g,0x479c78u,a,6,&r);
    }
    r.eax=descriptor;r.edx=stream;r.ecx=R32(stream);
    a[0]=stream;a[1]=2;a[2]=descriptor;cd_read(m,g,r.ecx,a,&r);
    const uint32_t count=R16(descriptor);
    if((uint16_t)count) {
        r.eax=sx(count);r.edx=10u*r.eax;
        a[0]=r.edx;cd_call(g,0x4774f0u,a,1,&r);
        r.ecx=a[0];                                    /* POP ECX */
        uint32_t group=r.eax;
        W32(descriptor+2u,r.eax);
        for(uint32_t i=0;s16(i)<s16(R16(descriptor));++i,group+=10u) {   /* DI, signed words */
            r.eax=stream;a[0]=group;a[1]=stream;cd_call(g,0x40cc50u,a,2,&r);
            a[0]=group;cd_call(g,0x40ccc8u,a,1,&r);
            r.ecx=a[0];                                /* POP ECX */
        }
    }
    return cd_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x40cc50. EBX = G, ESI = the volume cursor, EDI = i. */
uint64_t es2_collision_entry_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t group,
    uint32_t stream,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[5];
    r.eax=group;r.edx=stream;r.ecx=R32(stream);
    a[0]=stream;a[1]=2;a[2]=group;cd_read(m,g,r.ecx,a,&r);
    r.eax=group+4u;r.edx=stream;r.ecx=R32(stream);
    a[0]=stream;a[1]=2;a[2]=group+4u;cd_read(m,g,r.ecx,a,&r);
    const uint32_t count=R16(group+4u);
    if((uint16_t)count) {
        r.eax=sx(count);
        a[0]=0;a[1]=0x10u;a[2]=r.eax;a[3]=1;a[4]=GVA(0x40cf04u);cd_call(g,0x49667cu,a,5,&r);
        W32(group+6u,r.eax);
        uint32_t volume=r.eax;
        for(uint32_t i=0;s16(i)<s16(R16(group+4u));++i,volume+=0x10u) {
            r.eax=stream;a[0]=volume;a[1]=stream;cd_call(g,0x40cc14u,a,2,&r);
        }
    }
    r.eax=lo(r.eax,1);                                 /* MOV AX,1 */
    return cd_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x40cc14. EBX = V, ESI = the read's low byte. */
uint64_t es2_collision_volume_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t volume,
    uint32_t stream,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={stream,0,entry[2]};uint32_t a[3];
    r.edx=R32(stream);
    a[0]=stream;a[1]=2;a[2]=volume+6u;cd_read(m,g,r.edx,a,&r);
    const uint32_t ok=r.eax&0xffu;
    if((uint16_t)ok) { r.eax=stream;a[0]=volume;a[1]=stream;cd_call(g,0x40c7c4u,a,2,&r); }
    r.eax=ok;
    return cd_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x40c7c4. EBX = V, EDI = V with its low word the
 * count, shifted (DI = 8 x count, 16 bits), ESI = the result byte. */
uint64_t es2_collision_points_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t volume,
    uint32_t stream,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={stream,0,entry[2]};uint32_t a[5];
    r.edx=R32(stream);
    a[0]=stream;a[1]=2;a[2]=volume;cd_read(m,g,r.edx,a,&r);
    uint32_t ok=r.eax&0xffu;
    if((uint16_t)ok) {
        const uint32_t size=lo(volume,R16(volume))<<3;
        if((uint16_t)size) {
            r.eax=sx(R16(volume));
            a[0]=0;a[1]=8;a[2]=r.eax;a[3]=1;a[4]=GVA(0x40c8beu);cd_call(g,0x49667cu,a,5,&r);
            const uint32_t points=r.eax;
            r.eax=sx(size);
            W32(volume+2u,points);
            r.edx=stream;r.ecx=R32(stream);
            a[0]=stream;a[1]=r.eax;a[2]=points;cd_read(m,g,r.ecx,a,&r);
            r.eax&=0xffu;ok=r.eax;
        }
    }
    r.eax=ok;
    return cd_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x40ccc8. EDI = G, EBX = the volume cursor, ESI = i. */
uint64_t es2_collision_bounds_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t group,
    const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[2];
    uint32_t volume=R32(group+6u);
    for(uint32_t i=0;s16(i)<s16(R16(group+4u));++i,volume+=0x10u) {
        r.eax=volume+8u;a[0]=volume;a[1]=volume+8u;cd_call(g,0x40c5d0u,a,2,&r);
        W16(volume+0xeu,r.eax);
    }
    return cd_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x40c5d0. The extrema are the frame words EBP-10h ..
 * EBP-4 (maxima x, y, z; minima x, y, z), EBX = i, EAX the sphere cursor;
 * ECX/EDX in the loop never reach memory beyond their low words, and the
 * tail reloads both before the call. */
uint64_t es2_collision_radius_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t volume,
    uint32_t centre,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[3];
    uint16_t lowest[3]={10000,10000,10000},highest[3]={0xd8f0,0xd8f0,0xd8f0};
    uint32_t sphere=R32(volume+2u);
    for(uint32_t i=0;s16(i)<s16(R16(volume));++i,sphere+=8u) {
        for(unsigned k=0;k<3;++k) {
            const uint16_t v=(uint16_t)(R16(sphere+2u*k)+R16(sphere+6u));
            if(!(s16(v)<s16(highest[k])))highest[k]=v;
        }
        for(unsigned k=0;k<3;++k) {
            const uint16_t v=(uint16_t)(R16(sphere+2u*k)-R16(sphere+6u));
            if(!(s16(v)>s16(lowest[k])))lowest[k]=v;
        }
    }
    W32(centre,(uint32_t)lowest[0]|(uint32_t)lowest[1]<<16);
    W16(centre+4u,lowest[2]);
    W16(centre+4u,R16(centre+4u)+highest[2]);
    W16(centre,R16(centre)+highest[0]);
    W16(centre+2u,R16(centre+2u)+highest[1]);
    W16(centre+4u,sar1(R16(centre+4u)));
    W16(centre,sar1(R16(centre)));
    W16(centre+2u,sar1(R16(centre+2u)));
    r.edx=lo(centre,R16(centre+4u));highest[2]=(uint16_t)(highest[2]-r.edx);
    r.ecx=lo(centre,R16(centre));highest[0]=(uint16_t)(highest[0]-r.ecx);
    r.eax=lo(centre,R16(centre+2u));highest[1]=(uint16_t)(highest[1]-r.eax);
    a[0]=sx(highest[0]);a[1]=sx(highest[1]);a[2]=sx(highest[2]);
    cd_call(g,0x47de72u,a,3,&r);
    return cd_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x40cec0. EDI = T, EBX = the group cursor, ESI = i;
 * EAX holds s16([T]) at every loop test, so the pushed word's high half is
 * that count's (0 inside the loop). */
uint64_t es2_collision_scan_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t table,
    uint32_t a1,uint32_t b,uint32_t n,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[4];
    uint32_t group=R32(table+2u);
    for(uint32_t i=0;;++i,group+=10u) {
        r.eax=sx(R16(table));
        if(!((int32_t)i<(int32_t)r.eax))break;
        r.eax=lo(r.eax,n);r.edx=b;r.ecx=a1;
        a[0]=group;a[1]=a1;a[2]=b;a[3]=r.eax;cd_call(g,0x40ce78u,a,4,&r);
        if((uint16_t)r.eax) {
            a[0]=group;cd_call(g,0x40ccc8u,a,1,&r);
            r.ecx=a[0];                                /* POP ECX */
            break;
        }
    }
    return cd_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x40ce78. EDI = G, ESI = the volume cursor, EBX = i. */
uint64_t es2_collision_match_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t group,
    uint32_t a1,uint32_t b,uint32_t n,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[4];
    uint32_t volume=R32(group+6u),i=0;
    for(;;++i,volume+=0x10u) {
        r.eax=sx(R16(group+4u));
        if(!((int32_t)i<(int32_t)r.eax))break;
        r.eax=lo(r.eax,n);r.edx=b;r.ecx=a1;
        a[0]=volume;a[1]=a1;a[2]=b;a[3]=r.eax;cd_call(g,0x40ce28u,a,4,&r);
        if((uint16_t)r.eax)break;
    }
    r.edx=sx(R16(group+4u));
    r.eax=(int32_t)i<(int32_t)r.edx?1u:0u;            /* SETL AL; AND EAX,1 */
    return cd_done(&r,ecx);
}
