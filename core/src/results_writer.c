#include "es2/core/results_writer.h"
#include "fixed.h"
#include <stddef.h>
/* Results writer and helpers (docs/reverse-engineering/ mission_runtime.md,
 * section "results writer 42412c and its helpers"). Guest memory is read and
 * written in the original's order; r holds what the original's EAX/EDX/ECX hold
 * at the next game call; lo() is a 16-bit load that keeps the register's high
 * word. */
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define R16(a) m->read16(m->context,(a))
#define R32(a) m->read32(m->context,(a))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define VA(g,x) ((g)->image+(x)-0x400000u)
static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
static uint32_t sx8(uint32_t x) { x&=0xffu; return x<0x80u?x:x|0xffffff00u; }
static void put_ecx(uint32_t *ecx,uint32_t value) { if(ecx)*ecx=value; }
/* A direct call at its original VA (observer key); the bridge gets the
 * address in this image. */
static void rw_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,VA(g,va),a,n,r);
}
/* The stream's write virtual: the target is [table+14h] of the table the
 * register already holds. */
static void rw_write(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t table,uint32_t stream,uint32_t size,
                     uint32_t pointer,es2_weapon_registers *r) {
    const uint32_t a[3]={stream,size,pointer};
    if(g->observe)g->observe(g->observer_context,0x14u,0x14u);
    g->call(g->context,R32(table+0x14u),a,3,r);
}

/* Original: DBSIM.EXE 0x426ac8 */
uint64_t es2_list_append_registers(const es2_guest_memory *m,uint32_t image,uint32_t item,uint32_t *ecx) {
    const uint32_t count=image+0x4a9e6au-0x400000u,list=image+0x4a9e6cu-0x400000u;
    const uint32_t eax=sx(R16(count)),edx=R32(list),value=R32(item);
    W32(edx+4u*eax,value);
    W16(count,R16(count)+1u); /* INC WORD: the count is read again */
    put_ecx(ecx,value);
    return pair(eax,edx);
}

/* Original: DBSIM.EXE 0x418e60. f-8 and f-4 are the two scratch dwords. */
uint64_t es2_vehicle_value_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t v,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_VEHICLE_VALUE_SCRATCH,parts=v+0x206u;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t a[2];
    if(R8(v+0xb3u)) { /* 418e76: excluded; ECX = the unwritten f-4 */
        put_ecx(ecx,R32(f-4u));
        return pair(0,r.edx);
    }
    for(uint32_t i=0;;++i) {
        /* 418ee5: the list and its count are reread at every test. */
        r.edx=R32(v+0x202u);
        r.eax=sx(R16(r.edx+8u));
        if(!(fx_s32(i)<fx_s32(r.eax)))break;
        r.eax=sx(i);
        r.edx=R32(r.edx+4u);
        const uint32_t item=R32(r.edx+4u*r.eax);
        if(!item)continue;
        r.eax=sx8(R8(R32(item+0x18u)+0x17u));
        r.eax=lo(r.eax,r.eax+0x13u);
        r.edx=parts;
        a[0]=parts;a[1]=r.eax;rw_call(g,0x40dbc0u,a,2,&r);
        if(fx_s16(r.eax)>=0x80)continue;
        /* 418eb3: the pair (item id, damage percentage) at f-4. */
        r.ecx=R32(item+0x1cu);
        r.eax=sx(r.eax);
        r.edx=lo(r.edx,R16(r.ecx+0x56u));
        r.ecx=0x100u-r.eax;
        W16(f-4u,r.edx);
        r.eax=fx_sar(r.ecx*100u,8);
        r.edx=f-4u;
        W16(f-2u,r.eax);
        a[0]=f-4u;rw_call(g,0x426ac8u,a,1,&r);
        r.ecx=f-4u;
    }
    /* 418ef3: the base word, halved when the whole vehicle is intact. */
    r.ecx=R32(v+0x1f2u);
    r.eax=parts;
    uint32_t si=R16(r.ecx+0x54u);
    a[0]=parts;a[1]=0;rw_call(g,0x40dbc0u,a,2,&r);
    if((uint16_t)r.eax==0x100u)si=fx_sar(sx(si),1)&0xffffu;
    a[0]=parts;rw_call(g,0x41537cu,a,1,&r);
    r.ecx=parts;
    const uint32_t score=r.eax;
    r.eax=sx(si);
    W32(f-8u,r.eax);
    r.edx=R32(f-8u);
    a[0]=score;a[1]=r.edx;rw_call(g,0x47e0b0u,a,2,&r);
    put_ecx(ecx,R32(f-4u));
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x423e88 */
uint64_t es2_score_sum_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t team,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={0,0,entry[2]};
    uint32_t sum=0,cursor=0,a[2];
    for(;;) {
        /* 423ee0: the MECH list dword is reread before every call. */
        r.edx=R32(VA(g,0x4a9b7au));
        a[0]=r.edx;a[1]=cursor;rw_call(g,0x471c70u,a,2,&r);
        cursor=r.eax;
        if(!cursor)break;
        r.ecx=R32(cursor+0x45u);
        r.eax=sx8(R8(r.ecx+0x12u));
        r.edx=r.eax;
        r.ecx=sx8(R8(team+0x12u));
        if(r.edx==r.ecx)continue;
        if(R8(cursor+0xa4u))r.eax=1;
        else { r.edx=cursor+0x92u;r.eax=R8(cursor+0x99u)?1u:0u; }
        if(!(uint16_t)r.eax)continue;
        a[0]=cursor;rw_call(g,0x418e60u,a,1,&r);
        r.ecx=cursor;
        sum+=r.eax;
    }
    put_ecx(ecx,0);
    return pair(sum,r.edx);
}

/* Original: DBSIM.EXE 0x42412c. Frame (f = the synthetic EBP): record
 * f-38h, context word f-28h, streams R = f-0Ch and V = f-18h, team f-3Ch,
 * flag f-3Eh, tally f-44h, count f-46h, pair words f-48h/f-4Ah. */
uint64_t es2_results_writer_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t p,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_RESULTS_WRITER_FRAME,R=f-0x0cu,V=f-0x18u;
    const uint32_t count=VA(g,0x4a9e6au);
    es2_weapon_registers r={VA(g,0x49a76cu),entry[1],entry[2]};
    uint32_t a[6];
    rw_call(g,0x49630cu,NULL,0,&r);
    r.edx=R32(p+0x45u);
    W32(f-0x3cu,r.edx);
    a[0]=p;rw_call(g,0x413280u,a,1,&r);
    const uint32_t flag=(uint16_t)r.eax==9u?1u:0u;
    r.ecx=flag;
    W16(f-0x28u,8u);
    a[0]=VA(g,0x49a97du);rw_call(g,0x45ee00u,a,1,&r);   /* "data\results.dat" */
    r.ecx=VA(g,0x49a97du);
    a[0]=R;a[1]=r.eax;rw_call(g,0x47b064u,a,2,&r);
    r.eax=R32(R+4u);
    if(r.eax) { /* 424180: the stream did not open */
        r.edx=0;
        a[0]=VA(g,0x6c56c8u);a[1]=5;a[2]=0;a[3]=VA(g,0x49a98eu);a[4]=0x6fu;a[5]=VA(g,0x49a998u);
        rw_call(g,0x479c78u,a,6,&r);
    }
    W16(f-0x3eu,flag);
    r.ecx=f-0x3eu;
    r.eax=R32(R);rw_write(m,g,r.eax,R,2,f-0x3eu,&r);
    r.edx=lo(r.edx,R16(VA(g,0x4a9e54u)));
    r.ecx=R32(f-0x3cu);
    a[0]=r.ecx;a[1]=r.edx;rw_call(g,0x423e88u,a,2,&r);
    a[0]=0x9c4u;a[1]=r.eax;rw_call(g,0x47e0b0u,a,2,&r);
    const uint32_t product=r.eax;
    r.eax=sx(R16(VA(g,0x4a9e98u)));
    r.edx=r.eax*25000u;
    r.ecx=f-0x44u;
    W32(f-0x44u,product+r.edx);
    r.eax=R32(R);rw_write(m,g,r.eax,R,4,f-0x44u,&r);
    r.edx=lo(r.edx,R16(count));
    W16(f-0x46u,r.edx);
    r.ecx=f-0x46u;
    r.eax=R32(R);rw_write(m,g,r.eax,R,2,f-0x46u,&r);
    /* 424228: the pair list pointer is read once, the count at every test. */
    uint32_t list=R32(VA(g,0x4a9e6cu));
    for(uint32_t i=0;fx_s16(i)<fx_s16(R16(count));++i,list+=4u) {
        r.eax=lo(r.eax,R16(list));
        r.edx=f-0x48u;
        W16(f-0x48u,r.eax);
        r.ecx=R32(R);rw_write(m,g,r.ecx,R,2,f-0x48u,&r);
        r.eax=lo(r.eax,R16(list+2u));
        W16(f-0x4au,r.eax);
        r.edx=f-0x4au;
        r.ecx=R32(R);rw_write(m,g,r.ecx,R,2,f-0x4au,&r);
    }
    r.eax=R32(f-0x3cu);
    a[0]=r.eax;a[1]=R;rw_call(g,0x423d68u,a,2,&r);
    a[0]=R;a[1]=2;rw_call(g,0x47b09cu,a,2,&r);
    W16(f-0x28u,0x14u);
    a[0]=VA(g,0x49a9a4u);rw_call(g,0x45ee00u,a,1,&r);   /* "data\mission.var" */
    r.ecx=VA(g,0x49a9a4u);
    r.edx=V;
    a[0]=V;a[1]=r.eax;rw_call(g,0x47b064u,a,2,&r);
    r.eax=R32(V+4u);
    if(r.eax) { /* 4242a9 */
        r.edx=0;
        a[0]=VA(g,0x6c56c8u);a[1]=5;a[2]=0;a[3]=VA(g,0x49a9b5u);a[4]=0x84u;a[5]=VA(g,0x49a9bfu);
        rw_call(g,0x479c78u,a,6,&r);
    }
    r.ecx=V;
    a[0]=V;a[1]=0x7d0u;a[2]=VA(g,0x4a9e70u);rw_call(g,0x47b2b8u,a,3,&r);
    r.eax=V;
    a[0]=V;a[1]=2;rw_call(g,0x47b09cu,a,2,&r);
    put_ecx(ecx,r.ecx);
    return pair(r.eax,R32(f-0x38u));
}
