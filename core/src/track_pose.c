#include "es2/core/track_pose.h"
#include "fixed.h"
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static uint32_t low_word(uint32_t high,uint32_t low) { return (high&0xffff0000u)|(uint16_t)low; }
/* Direct call of the original VA through the bridge; registers in and out. */
static void tp_call(const es2_weapon_graph *g,uint32_t function,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,function,0);
    g->call(g->context,g->image+function-0x400000u,a,n,r);
}

/* Original: DBSIM.EXE 0x479344 */
uint64_t es2_track_seek_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t t,uint32_t clip,uint32_t fraction) {
    uint32_t slot=R32(R32(t)+6u)+4u*(uint32_t)fx_s16(clip);
    /* Total duration: 16-bit sum, clip pointer and count reread per frame. */
    uint32_t total=0;
    for(int32_t j=0;j<fx_s16(R16(R32(slot)+0x0au));++j)
        total=(uint16_t)(total+R16(R32(R32(slot)+0x0cu)+6u*(uint32_t)j));
    /* 47e0c0(s16 fraction, total-1): s16 x s16 fits 32 bits, bits 14..45. */
    uint32_t phase=fx_sar((uint32_t)(fx_s16(fraction)*fx_s16(total-1u)),14),frame=0;
    for(;;) {
        uint16_t duration=R16(R32(R32(slot)+0x0cu)+6u*frame);
        if(fx_s16(phase)<fx_s16(duration))break;
        phase=low_word(phase,phase-duration);++frame;
    }
    uint32_t selected=low_word(slot,clip),a[4]={t,selected,frame,phase};
    es2_weapon_registers r={selected,t,phase};
    tp_call(g,0x4792acu,a,4,&r);
    return pair(r.eax,r.edx);
}

/* s16 word >> 3 (arithmetic), clamped to -256..256. */
static uint32_t tp_target(uint16_t w) {
    int64_t v=fx_s32(fx_sar((uint32_t)fx_s16(w),3));
    return (uint32_t)(v>=256?256:v<=-256?-256:v);
}

/* Original: DBSIM.EXE 0x403d5c */
uint64_t es2_turret_aim_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t b,uint32_t p) {
    uint16_t target[2]={(uint16_t)tp_target(R16(p)),(uint16_t)(0u-tp_target(R16(p+4u)))};
    es2_weapon_registers r={0,0,0};
    for(uint32_t i=0;i<2;++i) {
        uint32_t table=g->image+0x973e4u+2u*i,rate=b+0x209u+2u*i,angle=b+0x20du+2u*i,a[4];
        uint32_t t=(uint32_t)fx_s16(target[i]),magnitude=(uint32_t)fx_s16(fx_s16(t)<0?0u-t:t);
        a[0]=magnitude;a[1]=t;r.eax=t;r.edx=magnitude;r.ecx=t;
        tp_call(g,0x47e0a0u,a,2,&r);
        uint32_t first=r.eax,step=R16(table+4u),k=(uint32_t)fx_s16(R16(table));
        a[0]=first;a[1]=k;r.eax=k;r.edx=first;r.ecx=k;
        tp_call(g,0x47e0a0u,a,2,&r);
        a[0]=rate;a[1]=r.eax;a[2]=low_word(first,step);r.ecx=rate;
        tp_call(g,0x467adcu,a,3,&r);
        uint16_t value=(uint16_t)(R16(angle)+R16(rate)),high=R16(table+0x0cu),low=R16(table+8u);
        uint16_t clamped=fx_s16(high)<=fx_s16(value)?high:fx_s16(low)<fx_s16(value)?value:low;
        W16(angle,clamped);
        uint32_t track=R32(b+0x1f9u+4u*i);
        a[0]=track;a[1]=low_word(table+8u,i);a[2]=(uint32_t)R16(angle)>>2;
        r.eax=b+0x1f9u+4u*i;r.edx=track;r.ecx=a[1];
        tp_call(g,0x479344u,a,3,&r);
    }
    uint32_t a[2]={b,100};r.ecx=b;
    tp_call(g,0x402604u,a,2,&r);
    return pair(r.eax,r.edx);
}
