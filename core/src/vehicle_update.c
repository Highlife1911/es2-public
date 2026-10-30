#include "es2/core/vehicle_update.h"
#include "fixed.h"

/* Per-vehicle update 41aa5c and its leaves 41edd8, 41b4ec, 41aa10, 41aa44,
 * 41b0bc and 40fbdc. Contract: docs/reverse-engineering/vehicle.md. The
 * register struct r follows EAX/EDX/ECX wherever a game call or the result can
 * see them; lo() is a 16-bit load or operation that keeps the register's high
 * word. Memory is read again wherever the original reads it again (callees may
 * change it). */
static uint64_t pair(uint32_t a,uint32_t d) { return (uint64_t)d<<32|a; }
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
static int32_t s16(uint32_t x) { return fx_s16(x); }
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
static uint32_t sx8(uint32_t x) { return (uint32_t)(int32_t)(int8_t)(uint8_t)x; }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static void vu_call(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,
                    es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}
/* A direct call at its original address, or the virtual at `slot` of the
 * object whose table the register already holds (as in the original). A
 * slot-0 call is observed as a call of its target. */
#define CALL(va,n) vu_call(g,g->image+(va)-0x400000u,0,a,n,&r)
#define VCALL(table,slot,n) vu_call(g,R32((table)+(slot)),(slot),a,n,&r)
#define FRAME(g) ((g)->scratch+0x80u)

/* Original: DBSIM.EXE 0x41b4ec. */
uint64_t es2_condition_record_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t vehicle,uint32_t out,
    const uint32_t entry[3]) {
    const uint32_t A=vehicle;es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[2];
    const uint32_t config=R32(A+0x1f2u);
    r.ecx=lo(config,R16(config+0x6cu));W16(out,r.ecx);
    r.eax=A+0x206u;r.edx=out;a[0]=A+0x206u;a[1]=out;CALL(0x4151a4,2);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x41aa10. */
uint64_t es2_device_state_registers(const es2_guest_memory *m,uint32_t vehicle) {
    const uint32_t A=vehicle,D=R32(A+0x307u);
    if(!D)return 0;
    const uint32_t edx=R8(A+0xa3u)?lo(A+0x92u,R16(D+0x7du)):sx8(R8(A+0x96u));
    return pair(edx,edx);
}

/* Original: DBSIM.EXE 0x41aa44. */
uint64_t es2_device_word_registers(const es2_guest_memory *m,uint32_t vehicle) {
    const uint32_t D=R32(vehicle+0x307u);
    return pair(D?R16(D+0x7fu):0u,D);
}

/* Original: DBSIM.EXE 0x41b0bc. Bytes 49a1d0 (t0) and 49a1d1 (t1) are
 * the tone flags; 467828 (clock) and 462830 (sound) stay original. */
uint64_t es2_ready_tone_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t vehicle,const uint32_t entry[3]) {
    const uint32_t A=vehicle,t0=g->image+0x9a1d0u,t1=g->image+0x9a1d1u;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[1];
    r.eax=A;r.ecx=A+0x92u;r.edx=r.ecx;
    if(R8(A+0x9bu)) {
        W8(t1,1);CALL(0x467828,0);
        if(!(r.eax&0x40u)) { W8(t0,0);return pair(r.eax,r.edx); }
        if(R8(t0))return pair(r.eax,r.edx);
        a[0]=0x15;CALL(0x462830,1);W8(t0,1);
        return pair(r.eax,r.edx);
    }
    if(R8(t1)) { a[0]=0x16;CALL(0x462830,1);W8(t1,0);return pair(r.eax,r.edx); }
    if(R8(A+0x9du)) { a[0]=0x14;CALL(0x462830,1); }
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x40fbdc. The list pointer and count are frame
 * copies; V60 receives the frame word at EBP-0Ah. */
uint64_t es2_weapon_tally_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t group,uint32_t out,
    const uint32_t entry[3]) {
    const uint32_t G=group,local=FRAME(g)-0xau;es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[3];
    r.eax=G+4u;uint32_t list=R32(G+4u);r.edx=list;uint16_t count=R16(G+8u);r.ecx=lo(r.ecx,count);
    a[0]=out;a[1]=0;a[2]=10;CALL(0x49681a,3);
    for(;;) {
        const uint16_t old=count;count=(uint16_t)(count-1u);r.edx=lo(r.edx,old);
        if(!old) { r.eax=0;break; }
        r.eax=list;list+=4u;
        if(!r.eax)break;
        r.edx=R32(r.eax);
        if(!r.edx)continue;
        const uint32_t W=r.edx;r.eax=local;a[0]=W;a[1]=local;r.edx=R32(W);VCALL(r.edx,0x60u,2);
        if((uint16_t)r.eax==5u)continue;
        r.ecx=sx(r.eax);r.eax=lo(r.eax,R16(local));
        const uint32_t slot=out+2u*r.ecx;W16(slot,R16(slot)+r.eax);
    }
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x41edd8. out = the 84-byte record at EBP-74h. */
uint64_t es2_combat_rating_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t vehicle,const uint32_t entry[3]) {
    const uint32_t A=vehicle,out=FRAME(g)-0x74u;es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[2];
    const uint32_t P=R32(A+0x1f2u)+2u;
    r.eax=P;r.edx=out;a[0]=A;a[1]=out;CALL(0x41b4ec,2);
    r.ecx=P;uint32_t sum=sx(R16(P+0x42u)),c=out+0x40u;
    for(uint32_t i=0;;++i,c+=2u) {
        r.edx=R32(A+0x202u);r.ecx=sx(R16(r.edx+8u));
        if(!((int32_t)i<(int32_t)r.ecx))break;
        r.eax=sx(i);r.edx=R32(A+0x202u);r.ecx=R32(r.edx+4u);r.eax=R32(r.ecx+4u*r.eax);
        const uint32_t W=r.eax;
        if(!W||s16(R16(c))<0)continue;
        r.edx=W;r.ecx=R32(W);a[0]=W;VCALL(r.ecx,0x54u,1);r.ecx=W;
        if(!(uint16_t)r.eax)continue;
        r.eax=W;r.edx=R32(W+0x1cu);const uint32_t value=sx(R16(r.edx+0x4eu));
        r.eax=sx(R16(c));r.edx=0x100u-r.eax;r.ecx=r.edx;r.eax=value;a[0]=value;a[1]=r.edx;CALL(0x47e0a0,2);
        sum+=r.eax;
    }
    c=out+2u;
    for(uint32_t i=0;i<19u;++i,c+=2u) {
        if(s16(R16(c))<0)continue;
        r.ecx=R32(A+0x212u);r.edx=9u*i;r.eax=R32(r.ecx);r.edx=lo(r.edx,R16(r.eax+2u*r.edx));
        const uint32_t base=sx(r.edx);
        r.eax=sx(R16(c));r.edx=0x100u-r.eax;r.ecx=r.edx;r.eax=base;a[0]=base;a[1]=r.edx;CALL(0x47e0a0,2);
        sum+=r.eax;
    }
    c=out+0x28u;r.eax=P+0x7cu;
    for(uint32_t i=0;i<10u;++i,c+=2u,r.eax+=2u)
        if(s16(R16(c))>0xb4) { r.edx=sx(R16(r.eax));sum-=r.edx; }
    W16(A+0x29eu,fx_sar(sum,4));W8(A+0x94u,1);
    return pair(lo(A+0x92u,R16(A+0x29eu)),r.edx);
}

/* Original: DBSIM.EXE 0x41aa5c. buf = the five tally words at EBP-20h;
 * Wp = the group's request words, computed once. */
uint64_t es2_vehicle_update_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t vehicle,const uint32_t entry[3]) {
    const uint32_t A=vehicle,buf=FRAME(g)-0x20u;es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[3];
    /* Movement step and energy. */
    r.eax=lo(r.eax,R16(A+0x256u));a[0]=r.eax;CALL(0x467924,1);r.ecx=a[0];
    W16(A+0x292u,R16(A+0x292u)+r.eax);
    r.edx=lo(r.edx,R16(A+0x292u));r.edx=lo(r.edx,r.edx+0xfe0cu);
    r.ecx=R32(A+0x202u);r.eax=R32(r.ecx);a[0]=r.ecx;a[1]=r.edx;a[2]=A;VCALL(r.eax,0u,3);
    const uint32_t request=r.eax;r.eax=A+0x222u;a[0]=A+0x222u;a[1]=request;CALL(0x413b38,2);
    r.eax=lo(r.eax,r.eax+0x1f4u);W16(A+0x292u,r.eax);r.eax=lo(r.eax,R16(A+0x292u));
    if(s16(r.eax)>=10000)r.edx=lo(r.edx,10000u);
    else if(s16(r.eax)>0)r.edx=r.eax;
    else r.edx=0;
    W16(A+0x292u,r.edx);
    r.eax=A+0x92u;
    if(R8(A+0xa3u)) {
        r.edx=lo(r.edx,R16(A+0x222u));r.ecx=sx(r.edx);
        r.edx=A+0x222u;r.eax=lo(A+0x222u,R16(A+0x224u));r.edx=sx(r.eax);r.ecx+=r.edx;
        if((int32_t)r.ecx>0x5dc) { r.ecx=A+0x92u;r.eax=A+0x92u;W8(A+0xb0u,0); }
    }
    /* Countdowns. */
    r.edx=A+0x272u;a[0]=r.edx;CALL(0x467aa8,1);r.ecx=A+0x272u;
    r.ecx=A+0x277u;a[0]=r.ecx;CALL(0x467aa8,1);r.ecx=A+0x277u;
    r.eax=A+0x26au;a[0]=r.eax;CALL(0x467a48,1);r.ecx=A+0x26au;
    r.edx=A+0x27cu;a[0]=r.edx;CALL(0x467aa8,1);r.ecx=r.eax==0;
    if(r.ecx) { r.eax=0;W32(A+0x281u,0); }
    r.edx=A+0x285u;a[0]=r.edx;CALL(0x467a48,1);r.ecx=(uint16_t)r.eax==0;
    if(r.ecx) { W16(A+0x286u,0xfa0u);r.eax=0;W32(A+0x288u,0); }
    /* Rating, device flags, weapon tally. */
    r.eax=A+0x92u;
    if(!R8(A+0x94u)) { a[0]=A;CALL(0x41edd8,1);r.ecx=A; }
    if(R32(A+0x1a4u)) {
        a[0]=A;CALL(0x41aa44,1);r.ecx=A;
        if((uint16_t)r.eax) { r.edx=R32(A+0x1a4u)+0x92u;r.ecx=r.edx;W8(r.edx+0x10u,1); }
    }
    a[0]=A;CALL(0x41aa10,1);r.ecx=A;
    if((uint16_t)r.eax) { r.eax=A+0x92u;r.edx=A+0x92u;W8(A+0xa1u,1); }
    else { r.eax=A+0x92u;W8(A+0xa1u,0); }
    int done=0;
    const uint32_t Wp=R32(A+0x202u)+0xau;
    r.edx=Wp;r.ecx=buf;r.eax=R32(A+0x202u);a[0]=r.eax;a[1]=buf;CALL(0x40fbdc,2);
    for(r.edx=0,r.eax=buf,r.ecx=Wp;r.edx<5u;++r.edx,r.eax+=2u,r.ecx+=2u)W16(r.ecx,0);
    r.eax=A+0x92u;W8(A+0x9bu,0);r.eax=R32(A+0x1a4u);
    if(r.eax) {
        /* Lock roll. */
        const uint32_t T=r.eax;uint32_t si=0,di=0;
        if(R16(T+0x1a8u)) { r.eax=A+0x92u;W8(A+0x9cu,0); }
        else if(r.edx=T+0x92u,!R8(T+0xa1u)) { r.eax=A+0x92u;W8(A+0x9cu,0); }
        else {
            r.eax=A+0x267u;a[0]=r.eax;CALL(0x467a48,1);r.ecx=(uint16_t)r.eax==0;
            if(r.ecx) {
                di=0x14u;
                const uint32_t device=R32(A+0x30bu);
                if(device) { r.eax=device;r.edx=sx(R16(device+0x7fu));if((int32_t)r.edx<0x33)di=5u; }
                a[0]=g->image+0xd2599u;CALL(0x492ee0,1);
                r.edx=sx(di);r.eax=sx(r.eax)&0xfffu;r.ecx=41u*r.edx;r.eax=(int32_t)r.eax<(int32_t)r.ecx;
                if(r.eax) { r.edx=A+0x92u;r.ecx=A+0x92u;W8(A+0x9cu,1);W16(A+0x268u,0x1388u);si=1; }
                else { r.eax=A+0x92u;W8(A+0x9cu,0);W16(A+0x268u,0x5dcu); }
            }
        }
        /* Bearing and range. */
        r.edx=R32(A+0x1f2u);uint16_t angle;
        if(R16(r.edx+0x50u)) {
            r.eax=R32(A+0x1a4u);di=0;r.ecx=A+0x26u;r.edx=r.eax+0x26u;r.eax=r.edx;a[0]=r.edx;a[1]=A+0x26u;CALL(0x492934,2);
            r.eax=lo(r.eax,r.eax-R16(A+0x10u));angle=(uint16_t)r.eax;
        } else {
            r.edx=R32(A+0x1a4u);r.ecx=r.edx+0x26u;r.eax=A+0x26u;a[0]=A+0x26u;a[1]=r.ecx;CALL(0x49288c,2);
            di=fx_sar(r.eax,2);r.eax=(int32_t)di>0x7fff?0x7fffu:di;
            r.edx=R32(A+0x1a4u);di=r.eax;r.eax=A+0x26u;r.ecx=r.edx+0x26u;r.edx=r.ecx;a[0]=r.edx;a[1]=A+0x26u;CALL(0x492934,2);
            r.eax=lo(r.eax,r.eax-R16(A+0x10u));r.eax=lo(r.eax,r.eax+R16(A+0x298u));angle=(uint16_t)r.eax;
        }
        /* Line of sight; the group byte +12h orders the pair. */
        r.ecx=R32(A+0x45u);
        if(!R8(r.ecx+0x12u)) { r.eax=R32(A+0x1a4u);a[0]=A;a[1]=r.eax;CALL(0x412608,2); }
        else { r.edx=R32(A+0x1a4u);a[0]=r.edx;a[1]=A;CALL(0x412608,2); }
        const uint32_t seen=sx(r.eax);r.ecx=seen;r.eax=A+0x92u;r.edx=A+0x92u;
        int all=1;
        if(!R8(A+0x9du)) {
            r.ecx=lo(r.ecx,angle+0x3000u);r.eax=(uint16_t)r.ecx<0x6000u;
            if(r.eax&&seen)all=0;
        }
        if(all) {
            r.edx=lo(r.edx,di);W16(A+0x259u,r.edx);r.ecx=lo(r.ecx,di);W16(A+0x25cu,r.ecx);
            r.eax=lo(r.eax,di);W16(A+0x25fu,r.eax);done=1;
        } else {
            if(R16(buf)) {
                r.edx=A+0x92u;r.ecx=A+0x92u;
                if(!R8(A+0x96u)||(uint16_t)si) { r.eax=lo(r.eax,di);W16(A+0x259u,r.eax); }
                else {
                    r.edx=A+0x258u;a[0]=r.edx;CALL(0x467a48,1);r.ecx=(uint16_t)r.eax==0;
                    if(r.ecx) { r.eax=A+0x92u;r.edx=A+0x92u;if(!R8(A+0x9cu)) { r.ecx=Wp;W16(Wp,1); } }
                }
            }
            if(R16(buf+8u)) {
                r.eax=A+0x92u;r.edx=A+0x92u;
                if(!R8(A+0x96u)||(uint16_t)si) { r.ecx=lo(r.ecx,di);W16(A+0x265u,r.ecx); }
                else {
                    r.eax=A+0x264u;a[0]=r.eax;CALL(0x467a48,1);r.ecx=A+0x264u;r.edx=(uint16_t)r.eax==0;
                    if(r.edx) { r.ecx=A+0x92u;r.eax=A+0x92u;if(!R8(A+0x9cu)) { r.edx=Wp;W16(Wp+8u,1); } }
                }
            }
            if(R16(buf+2u)) {
                if((uint16_t)si) { r.ecx=lo(r.ecx,di);W16(A+0x25cu,r.ecx); }
                else {
                    r.eax=A+0x25bu;a[0]=r.eax;CALL(0x467a48,1);r.ecx=A+0x25bu;r.edx=(uint16_t)r.eax==0;
                    if(r.edx) { r.ecx=A+0x92u;r.eax=A+0x92u;if(!R8(A+0x9cu)) { r.edx=Wp;W16(Wp+2u,1); } }
                }
            }
            if(R16(buf+4u)) {
                r.ecx=R32(A+0x1a4u)+0x92u;r.eax=r.ecx;int own=0;
                if(!R8(r.eax+4u)) { r.edx=R32(A+0x1a4u)+0x92u;r.ecx=r.edx;own=!R8(r.ecx+0xfu); }
                if(own) { r.eax=lo(r.eax,di);W16(A+0x25fu,r.eax); }
                else {
                    r.edx=A+0x25eu;a[0]=r.edx;CALL(0x467a48,1);r.ecx=(uint16_t)r.eax==0;
                    if(r.ecx) { r.eax=Wp;W16(Wp+4u,1); }
                }
            }
        }
        /* Fire request flags and the tones (byte A+A3h). */
        r.edx=A+0x92u;r.ecx=A+0x92u;
        if(R8(A+0xa3u)) {
            const uint32_t G=R32(A+0x202u);r.eax=sx8(R8(G+0x1du))+1u;
            if((int32_t)r.eax<=0)r.eax=lo(r.eax,5u);
            else {
                r.edx=sx8(R8(G+0x1du));r.ecx=sx(r.edx);r.eax=R32(G+4u);r.edx=R32(r.eax+4u*r.ecx);
                a[0]=r.edx;a[1]=0;r.ecx=R32(r.edx);VCALL(r.ecx,0x60u,2);
            }
            if((uint16_t)r.eax==5u) {
                r.ecx=Wp;
                for(r.edx=0,r.eax=Wp;;) {
                    if(R16(r.eax)) { r.eax=A+0x92u;W8(A+0x9bu,1);break; }
                    ++r.edx;r.eax+=2u;
                    if(!((int32_t)r.edx<5))break;
                }
            } else {
                r.edx=sx(r.eax);r.ecx=Wp;
                if(R16(r.ecx+2u*r.edx)) { r.eax=A+0x92u;r.edx=A+0x92u;W8(A+0x9bu,1); }
            }
            a[0]=A;CALL(0x41b0bc,1);r.ecx=A;
        }
    }
    if(done) { r.eax=A+0x92u;W8(A+0x9du,0); }
    return pair(r.eax,r.edx);
}

/* The slot-0 bodies of the weapon group tables 499338 (411a1d) and
 * 499238 (41194a), reached from 41aa97 in 41aa5c. Both pass the group,
 * the value word and the owner on to group_fire 4107e4 (through the
 * bridge, so the hook keeps its mode) and return its EDX:EAX. The value
 * dword pushed is a register whose low word was loaded from the argument:
 * ECX's high word comes from the entry ECX (411a1d) or EDX's from the owner
 * (41194a); group_fire reads only the low word. */
/* Original: DBSIM.EXE 0x411a1d. */
uint64_t es2_group_fire_any_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t group,uint32_t value,
    uint32_t owner,const uint32_t entry[3]) {
    (void)m;es2_weapon_registers r={group,owner,lo(entry[2],value)};
    uint32_t a[4]={group,r.ecx,0xffffffffu,owner};
    CALL(0x4107e4,4);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x41194a. */
uint64_t es2_group_fire_sel_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t group,uint32_t value,
    uint32_t owner) {
    const uint32_t index=sx8(R8(group+0x1du));
    es2_weapon_registers r={group,lo(owner,value),index};
    uint32_t a[4]={group,r.edx,index,owner};
    CALL(0x4107e4,4);
    return pair(r.eax,r.edx);
}
