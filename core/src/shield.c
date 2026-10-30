#include "es2/core/shield.h"
#include "fixed.h"
/* Shield balance (docs/reverse-engineering/cockpit.md, "shield balance as own
 * code"); every guest read and write happens in the original's order, so a
 * vehicle block that overlaps the gauge record behaves the same. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define VA(va) (g->image+(uint32_t)(va)-0x400000u)
/* The cockpit object [49b088], its shields gauge +1E9h and the gauge record
 * +0B5h: front, rear, balance (dwords), a passed-through byte, the up and
 * down request bytes. */
enum { COCKPIT_GAUGE=0x1e9u, GAUGE_RECORD=0xb5u, REC_UP=0xdu, REC_DOWN=0xeu };
/* The vehicle shield block S: front +0, rear +2, balance +4, divisor +8. */
enum { S_FRONT=0u, S_REAR=2u, S_BALANCE=4u, S_MAXIMUM=8u };

/* Original: DBSIM.EXE 0x413af8 (step = 66h up, FF9Ah = -66h down, formed
 * in DX; the sum is stored, reread and replaced by its clamp to 0..400h). */
static void shield_step(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t s,int up) {
    if(g->observe)g->observe(g->observer_context,VA(0x413af8u),0);
    W16(s+S_BALANCE,R16(s+S_BALANCE)+(up?0x66u:0xff9au));
    const int32_t sum=fx_s16(R16(s+S_BALANCE));
    W16(s+S_BALANCE,sum>=0x400?0x400u:sum>0?(uint32_t)sum:0u);
}

/* Original: DBSIM.EXE 0x413bc8 */
uint64_t es2_shield_balance_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t s) {
    const uint32_t l=g->scratch;
    /* 443908 returns gauge+0B5h; the record is copied as three dwords, a
     * word and a byte. */
    uint32_t record=R32(R32(VA(0x49b088u))+COCKPIT_GAUGE)+GAUGE_RECORD;
    for(uint32_t i=0;i<12u;i+=4u)W32(l+i,R32(record+i));
    W16(l+0xcu,R16(record+0xcu));
    W8(l+0xeu,R8(record+0xeu));
    /* One request per frame; the down request waits while up is set. */
    if(R8(l+REC_UP)) { shield_step(m,g,s,1);W8(l+REC_UP,0); }
    else if(R8(l+REC_DOWN)) { shield_step(m,g,s,0);W8(l+REC_DOWN,0); }
    /* Front and rear relative to the maximum, Q10, signed IDIV. */
    const int32_t front=fx_s16(R16(s+S_FRONT)),maximum=fx_s16(R16(s+S_MAXIMUM));
    W32(l,(uint32_t)((front*1024)/maximum));
    const int32_t rear=fx_s16(R16(s+S_REAR));
    W32(l+4u,(uint32_t)((rear*1024)/maximum));
    es2_weapon_registers r;
    r.edx=R32(VA(0x49b088u));
    W32(l+8u,(uint32_t)fx_s16(R16(s+S_BALANCE)));
    r.eax=l;r.ecx=R32(r.edx+COCKPIT_GAUGE);
    const uint32_t a[2]={r.ecx,l};
    if(g->observe)g->observe(g->observer_context,VA(0x443880u),0);
    g->call(g->context,VA(0x443880u),a,2,&r);
    return (uint64_t)r.edx<<32|r.eax;
}
