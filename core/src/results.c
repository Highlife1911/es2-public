#include "es2/core/results.h"
#include "fixed.h"
/* The team actor records of results.dat (docs/reverse-engineering/
 * mission_runtime.md, section "team result records 423d68"). The register
 * struct r follows EAX/EDX/ECX wherever a game call or the result can see them.
 * The team's count, member array and the stream's table are read again where
 * the original reads them again (the callees may change them). */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
/* A direct call at its original address (observed by that address), or the
 * virtual at `slot` of the table the register already holds. */
static void rs_call(const es2_weapon_graph *g,uint32_t target,uint32_t key,unsigned slot,const uint32_t *a,unsigned n,
                    es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,key,slot);
    g->call(g->context,target,a,n,r);
}
/* The three spans of the condition record that are written, with the frame
 * word (EBP-x) each span's value goes through: record +2 (13 group words),
 * +28h (parts 0..9), +40h (the ten combined group/part words). */
static const struct { uint32_t first,count,local; } rs_spans[3]={{0x02u,13u,0x0au},{0x28u,10u,0x0cu},{0x40u,10u,0x0eu}};

/* Original: DBSIM.EXE 0x423d68 */
uint64_t es2_result_conditions_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t team,uint32_t stream,const uint32_t entry[3]) {
    const uint32_t frame=g->scratch+0x80u,out=frame-0x64u;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[3];
    for(uint16_t i=0;;++i) {
        /* 423e6e: the count word is reread at every test (signed). */
        r.ecx=team;r.eax=lo(r.eax,R16(team+0x10u));
        if(!(fx_s16(r.eax)>fx_s16(i)))break;
        const uint32_t actor=R32(R32(team+0x0cu)+4u*sx(i));
        r.eax=actor;r.edx=out;r.ecx=actor;a[0]=actor;a[1]=out;
        rs_call(g,g->image+0x41b4ecu-0x400000u,0x41b4ecu,0,a,2,&r);
        for(unsigned s=0;s<3u;++s) {
            const uint32_t local=frame-rs_spans[s].local;
            for(uint32_t j=0;j<rs_spans[s].count;++j) {
                const uint32_t word=out+rs_spans[s].first+2u*j;
                r.ecx=fx_sar((0x100u-sx(R16(word)))*100u,8);
                W16(word,r.ecx);W16(local,r.ecx);
                r.eax=local;r.edx=R32(stream);a[0]=stream;a[1]=2;a[2]=local;
                rs_call(g,R32(r.edx+0x14u),0x14u,0x14u,a,3,&r);
            }
        }
        /* 423e56: ECX is what the last write returned. */
        r.eax=actor+0x2a4u;r.edx=R32(stream);a[0]=stream;a[1]=6;a[2]=r.eax;
        rs_call(g,R32(r.edx+0x14u),0x14u,0x14u,a,3,&r);
    }
    return (uint64_t)r.edx<<32|r.eax;
}
