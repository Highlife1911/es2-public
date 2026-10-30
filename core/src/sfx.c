#include "es2/core/sfx.h"
#include "fixed.h"
/* Sound API calls (docs/reverse-engineering/audio.md, section "sound/clock
 * isolation"). r holds what the original's EAX/EDX/ECX hold at the next game
 * call. The originals keep the index in EBX and have no frame locals except
 * place's view-space position (EBP-18h, here g->scratch). */
#define R32(a) m->read32(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define VA(g,x) ((g)->image+(x)-0x400000u)
#define CONTEXT(g) VA(g,0x49f8ecu)
static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
/* Direct calls name the original VA (observer key); the bridge gets the
 * address in this image. Registers are updated in place. */
static void sfx_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,VA(g,va),a,n,r);
}
/* [4*i+0x4d2b78] (the row) and [4*i+0x4d2c78] (the handle); the scaled
 * index wraps to 32 bits. */
static uint32_t sfx_row(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t i) { return R32(VA(g,0x4d2b78u)+4u*i); }
static uint32_t sfx_handle(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t i) { return R32(VA(g,0x4d2c78u)+4u*i); }
/* The variant draw of 0x462845..0x462867 (0x4628f1..0x462913): the
 * variant count is byte +6 of the row (CMP SI,1 / JLE on the zero-extended
 * byte); rng_bounded(0x4d260b, count) with EAX = the row pointer, then
 * MOVSX EAX,AX / ADD EBX,EAX. */
static uint32_t sfx_select(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t i,es2_weapon_registers *r) {
    uint32_t a[2];
    r->eax=sfx_row(m,g,i);
    uint32_t count=R8(r->eax+6u);
    if(count>1u) {
        a[0]=VA(g,0x4d260bu);a[1]=count;sfx_call(g,0x492f24u,a,2,r);
        r->eax=(uint32_t)fx_s16(r->eax);i+=r->eax;
    }
    return i;
}
/* Original: DBSIM.EXE 0x462784, the effect gate, inline. With a context:
 * EAX = EDX = (signed i < 10 ? [0x49f8f4] : [0x49f8f8]) != 0. Without one
 * (reread here) only AL = 1. Returns AL. */
static uint32_t sfx_gate(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t i,es2_weapon_registers *r) {
    if(!R32(CONTEXT(g))) { r->eax=(r->eax&0xffffff00u)|1u;return 1u; }
    uint32_t open=(int32_t)i<10 ? R32(VA(g,0x49f8f4u))!=0u : R32(VA(g,0x49f8f8u))!=0u;
    r->eax=r->edx=open;
    return open;
}
/* Original: DBSIM.EXE 0x47e08d(a, b), inline: EDX:EAX = a*b signed, EAX =
 * bits 16..47 (SHRD EAX,EDX,10h), EDX = the high dword. */
static void sfx_q16(uint32_t a,uint32_t b,es2_weapon_registers *r) {
    uint64_t p=(uint64_t)((int64_t)(int32_t)a*(int64_t)(int32_t)b);
    r->eax=(uint32_t)(p>>16);r->edx=(uint32_t)(p>>32);
}
/* IMUL r32 / CDQ / IDIV r32: the low dword of the product, sign-extended,
 * divided toward zero. The divisors here are 100 and a positive far
 * distance, so the quotient always fits. */
static void sfx_scale(uint32_t x,uint32_t y,uint32_t divisor,es2_weapon_registers *r) {
    int32_t lo=(int32_t)(x*y);
    r->eax=(uint32_t)(lo/(int32_t)divisor);r->edx=(uint32_t)(lo%(int32_t)divisor);
}

/* Original: DBSIM.EXE 0x462830 */
uint64_t es2_sfx_play_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t index,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t a[3];
    if(!R32(CONTEXT(g))) { *ecx=r.ecx;return pair(r.eax,r.edx); } /* 46283f: entry registers */
    uint32_t i=sfx_select(m,g,index,&r);
    if(sfx_gate(m,g,i,&r)&0xffu) { /* 462872 */
        r.edx=sfx_row(m,g,i);r.ecx=R8(r.edx+1u);
        sfx_q16(r.ecx,0xfde8u,&r);r.ecx=r.eax;
    } else r.ecx=0;
    /* 462894: MOVSX EAX,CX times byte +8, IDIV 100. */
    sfx_scale((uint32_t)fx_s16(r.ecx),R8(sfx_row(m,g,i)+8u),100u,&r);r.ecx=100u;
    a[2]=r.eax;a[1]=sfx_handle(m,g,i);a[0]=R32(CONTEXT(g));r.eax=a[0];
    sfx_call(g,0x464618u,a,3,&r);
    a[1]=sfx_handle(m,g,i);r.edx=a[1];a[0]=R32(CONTEXT(g));r.ecx=a[0];
    sfx_call(g,0x464038u,a,2,&r);
    *ecx=r.ecx;return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x4628e0 */
uint64_t es2_sfx_play_at_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t index,uint32_t position,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t a[2];
    if(!R32(CONTEXT(g))) { *ecx=r.ecx;return pair(r.eax,r.edx); } /* 4628ef: entry registers */
    uint32_t i=sfx_select(m,g,index,&r);
    r.edx=position;a[0]=i;a[1]=position;sfx_call(g,0x46299cu,a,2,&r);
    if(r.eax&0xffu) { /* 462924: audible */
        a[1]=sfx_handle(m,g,i);r.ecx=a[1];a[0]=R32(CONTEXT(g));r.eax=a[0];
        sfx_call(g,0x464038u,a,2,&r);
    }
    *ecx=r.ecx;return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x46299c */
uint64_t es2_sfx_place_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t index,uint32_t position,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    const uint32_t v=g->scratch,i=index;
    uint32_t a[3],level;
    if(!R32(CONTEXT(g))) { *ecx=r.ecx;return pair(0,r.edx); } /* 4629af: XOR EAX,EAX */
    /* 4629b8: rebuild the default view (0x48c444(0)), then the source in
     * view space (0x48c57c(position, v)). */
    a[0]=0;sfx_call(g,0x48c444u,a,1,&r);
    r.edx=v;r.ecx=position;a[0]=position;a[1]=v;sfx_call(g,0x48c57cu,a,2,&r);
    if(!(sfx_gate(m,g,i,&r)&0xffu)) { r.ecx=i;level=0; } /* 4629d9 */
    else {
        r.ecx=i;r.eax=r.edx=R32(v);a[0]=r.eax;a[1]=R32(v+4u);a[2]=R32(v+8u);
        sfx_call(g,0x47de72u,a,3,&r);
        const int32_t d=(int32_t)r.eax;
        r.eax=sfx_row(m,g,i);
        const uint32_t inner=(uint32_t)R8(r.eax+4u)<<10,outer=(uint32_t)R8(r.eax+5u)<<10;
        r.edx=inner;
        if((int32_t)outer<d) level=0; /* 462a16: beyond the far distance (byte +5) */
        else {
            sfx_q16(R8(r.eax+1u),0xfde8u,&r);r.ecx=r.eax;
            if(d<(int32_t)inner) level=r.ecx; /* 462a33: inside the near distance (byte +4) */
            else { sfx_scale(outer-(uint32_t)d,r.ecx,outer,&r);level=r.eax; } /* (far-d)*v/far */
        }
    }
    /* 462a44: level times byte +8, IDIV 100; then the volume. */
    sfx_scale(R8(sfx_row(m,g,i)+8u),level,100u,&r);
    a[2]=r.eax;a[0]=R32(CONTEXT(g));a[1]=sfx_handle(m,g,i);r.edx=a[0];r.eax=a[1];r.ecx=100u;
    sfx_call(g,0x464618u,a,3,&r);
    if(level) { /* 462a74: the pan from the view-space direction */
        r.ecx=r.eax=R32(v);a[0]=r.eax;a[1]=R32(v+4u);sfx_call(g,0x47d32cu,a,2,&r);
        uint32_t e=r.eax;
        if((uint16_t)e<0x8000u) { e+=e;e=(e&0xffff0000u)|(uint16_t)(0u-(uint16_t)e); } /* 462a90: NEG AX */
        else e+=e;
        r.edx=(uint16_t)e;a[2]=r.edx;a[1]=sfx_handle(m,g,i);r.ecx=a[1];a[0]=R32(CONTEXT(g));r.eax=a[0];
        sfx_call(g,0x4646dcu,a,3,&r);
    }
    *ecx=r.ecx;return pair(level!=0u,r.edx);
}

/* Original: DBSIM.EXE 0x46297c */
uint64_t es2_sfx_refresh_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t index,uint32_t position,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t a[2];
    if(R32(CONTEXT(g))) { /* 462986 */
        r.eax=position;r.edx=index;a[0]=index;a[1]=position;sfx_call(g,0x46299cu,a,2,&r);
    }
    *ecx=r.ecx;return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x462ac4 */
uint64_t es2_sfx_stop_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t index,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t a[2];
    r.eax=R32(CONTEXT(g)); /* read once */
    if(r.eax) { /* 462ace */
        r.edx=index;r.ecx=sfx_handle(m,g,index);a[0]=r.eax;a[1]=r.ecx;sfx_call(g,0x464118u,a,2,&r);
    }
    *ecx=r.ecx;return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x463114 */
uint64_t es2_sfx_rate_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t index,uint32_t rate,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t a[3];
    r.eax=R32(CONTEXT(g)); /* read once */
    if(r.eax) { /* 46311e */
        r.ecx=index;r.edx=sfx_handle(m,g,index);a[0]=r.eax;a[1]=r.edx;a[2]=rate;sfx_call(g,0x46466cu,a,3,&r);
    }
    *ecx=r.ecx;return pair(r.eax,r.edx);
}

/* The voice/lip timeline (0x463400, 0x4634b0, 0x4634a0). A voice channel
 * record: +0 sample handle, +4 next event time, word +8 lip value (-1 =
 * end), +0Ah script pointer, +2Fh script buffer. The event queue: count
 * word 0x49f964, record pointers 0x4d2ec8[k], records 0x4d2e76 + 8*k
 * (time, channel). The clock reads go through g->call to the wrapper
 * 0x467828 (GetTickCount through the import thunk, so the virtual clock
 * sees the same calls in the same order). */
static uint32_t sfx_s8(uint32_t b) { b&=0xffu;return b<0x80u ? b : b-0x100u; }
#define R16(a) m->read16(m->context,(a))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define COUNT(g) VA(g,0x49f964u)
#define QUEUE(g) VA(g,0x4d2ec8u)

/* Original: DBSIM.EXE 0x4634b0 */
uint64_t es2_sfx_lip_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t ch,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t a[1]={0};
    sfx_call(g,0x467828u,a,0,&r);
    const uint32_t clock=r.ecx=r.eax;
    for(;;) { /* 4634c1: lip byte, then the time step, while the clock is ahead */
        r.eax=ch;uint32_t s=R32(ch+0xau);W32(ch+0xau,s+1u);r.edx=sfx_s8(R8(s));
        if(r.edx==0xffffffffu) { W16(ch+8u,0xffffu);*ecx=r.ecx;return pair(r.eax,r.edx); } /* 4634cd: end */
        s=R32(ch+0xau);W32(ch+0xau,s+1u);r.eax=sfx_s8(R8(s));
        W32(ch+4u,R32(ch+4u)+r.eax);
        if(!(clock>R32(ch+4u)))break; /* 4634f8: JA */
    }
    W16(ch+8u,r.edx);
    r.ecx=0;r.edx=QUEUE(g);
    if(0<fx_s16(R16(COUNT(g)))) { /* 463539 */
        r.edx=R32(QUEUE(g));
        if(r.eax<R32(r.edx)) { /* 46350e: JAE compares the last step, not the time */
            W16(COUNT(g),R16(COUNT(g))-1u);
            uint32_t k=0,p=VA(g,0x4d2eccu);
            while((int32_t)k<fx_s16(R16(COUNT(g)))) { W32(p-4u,R32(p));++k;p+=4u; }
            r.edx=k;r.eax=p;
        }
        /* The loop's increment (463530) is unreachable: the insert is always at 0. */
    }
    /* 46353b: the record of slot count, its pointer at position 0. */
    const uint32_t n=(uint32_t)fx_s16(R16(COUNT(g)));
    r.eax=n;r.edx=R32(ch+4u);W32(VA(g,0x4d2e76u)+8u*n,r.edx);r.edx=ch;W32(VA(g,0x4d2e7au)+8u*n,ch);
    r.eax=8u*n+VA(g,0x4d2e76u);W32(QUEUE(g)+4u*r.ecx,r.eax);
    W16(COUNT(g),R16(COUNT(g))+1u);
    *ecx=r.ecx;return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x4634a0 */
uint64_t es2_sfx_lip_due_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t ch,const uint32_t entry[3],uint32_t *ecx) {
    (void)m;
    es2_weapon_registers r={ch,entry[1],entry[2]};
    uint32_t a[1]={ch};
    sfx_call(g,0x4634b0u,a,1,&r);
    *ecx=ch;return pair(r.eax,r.edx); /* POP ECX */
}

/* Original: DBSIM.EXE 0x463400 */
uint64_t es2_sfx_voice_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t ch,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t a[2]={0,0};
    r.eax=R32(ch+0x2fu);W32(ch+0xau,r.eax);r.edx=sfx_s8(R8(r.eax));
    /* 463411: CMP EDX,0FFh never matches a sign-extended byte, so the
     * clock path always runs (branch site PROVED). */
    sfx_call(g,0x467828u,a,0,&r);
    W32(ch+4u,r.eax);
    a[0]=ch;sfx_call(g,0x4634b0u,a,1,&r);r.ecx=ch;
    const uint32_t c=R32(CONTEXT(g));
    if(c&&R8(VA(g,0x49f966u))&&R16(ch+8u)&&R32(ch)!=0xffffffffu) { /* 463438..46344d */
        a[0]=c;a[1]=R32(ch);r.eax=a[1];sfx_call(g,0x464038u,a,2,&r);
    } else r.eax=(r.eax&0xffffff00u)|1u; /* 46345d: MOV AL,1 */
    *ecx=r.ecx;return pair(r.eax,r.edx);
}
