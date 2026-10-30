#include "es2/core/mission_load.h"
#include "fixed.h"
/* The mission load's pieces L1 to L3 (docs/reverse-engineering/
 * mission_load.md, section "pieces L1 to L3 and the snapshot noise model").
 * Every guest read and write is done in the original's order (objects may
 * overlap), and the register struct r follows EAX/EDX/ECX so that each game
 * call sees the original's entry registers; lo() is a 16-bit load that keeps
 * the register's high word. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define GVA(x) (g->image+(uint32_t)(x)-0x400000u)
static uint64_t pair(uint32_t a,uint32_t d) { return (uint64_t)d<<32|a; }
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
static uint32_t sx8(uint8_t x) { return (uint32_t)(int32_t)(int8_t)x; }
static int32_t s16(uint32_t x) { return fx_s16(x); }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static void ecx_out(uint32_t *ecx,uint32_t value) { if(ecx)*ecx=value; }
/* One game call: the observer first (reference VA, or the virtual slot),
 * then the bridge with the entry registers in r. */
static void ml_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,GVA(va),a,n,r);
}
static void ml_virtual(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,
    es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}

/* Original: DBSIM.EXE 0x471ad8, with the pool initializer 0x471a50 (its
 * only caller is 471ad8). */
uint64_t es2_pool_create_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t header,
    uint32_t capacity,uint32_t size,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[3];
    const uint16_t c=(uint16_t)capacity,s=(uint16_t)size;
    /* 471ae5..471aef: IMUL of the sign-extended words, EDX:EAX. */
    const int64_t product=(int64_t)s16(c)*((int64_t)s16(s)+8);
    const uint32_t total=(uint32_t)(uint64_t)product;
    r.eax=total;r.edx=(uint32_t)((uint64_t)product>>32);
    uint32_t block=0;
    if(c) {
        /* 471af9: new[](total); POP ECX reloads the pushed total. */
        r.ecx=total;a[0]=total;ml_call(g,0x4774f0u,a,1,&r);
        block=r.eax;r.ecx=total;
    }
    r.eax=total;a[0]=block;a[1]=0;a[2]=total;ml_call(g,0x49681au,a,3,&r);
    /* 471b18: MOV DX,[EBP+10h] keeps memset's high word. */
    r.edx=lo(r.edx,s);
    /* 471a50 (header, block, capacity slot, size slot). */
    W16(header,c);
    W16(header+2u,0);
    W16(header+4u,s);
    W32(header+6u,block);
    W32(header+0xau,block);
    W32(header+0xeu,0);
    const uint32_t stride=(uint16_t)(R16(header+4u)+8u);   /* 471a7e: read back */
    if(c) {
        uint32_t node=block,previous=0;
        for(int32_t k=0;k<s16(c);++k) {
            r.edx=node;
            W32(node,node+stride);
            W32(node+4u,previous);
            previous=node;
            node+=stride;
        }
        /* 471acd: the last node's link; for a negative capacity the
         * original writes at the EDX it was called with (it faults first:
         * memset of a negative size, or this write at 0000FFF8h). */
        W32(r.edx,0);
    }
    ecx_out(ecx,total);
    return pair(header,r.edx);
}

/* Original: DBSIM.EXE 0x471cb0. The decremented argument word is the
 * original's own slot, released by every caller (ADD ESP,8). */
uint64_t es2_pool_nth_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t pool,
    uint32_t index,const uint32_t entry[3],uint32_t *ecx) {
    (void)m;
    es2_weapon_registers r={0,entry[1],entry[2]};uint32_t a[2];
    uint16_t k=(uint16_t)index;
    for(;;) {
        a[0]=pool;a[1]=r.eax;ml_call(g,0x471c70u,a,2,&r);
        if(!r.eax)break;
        const uint16_t old=k--;
        r.edx=lo(r.edx,old);
        if(!old)break;
    }
    ecx_out(ecx,r.ecx);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x425348. */
uint64_t es2_subject_resolve_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t index,
    uint32_t scope,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={scope-1u,entry[1],entry[2]};uint32_t a[2];
    switch(scope) {
    case 0: {
        /* 425362: 122 x the sign-extended word, plus the group array. */
        const uint32_t e=sx(index);
        r.edx=e;r.eax=122u*e+R32(GVA(0x4a9e00u));
        ecx_out(ecx,r.ecx);
        return pair(r.eax,r.edx);
    }
    case 1: {
        const uint32_t k=sx8(R8(GVA(0x4aa78eu)+index));
        r.edx=k;r.ecx=R32(GVA(0x4a9b7au));
        a[0]=r.ecx;a[1]=k;ml_call(g,0x471cb0u,a,2,&r);
        break;
    }
    case 2: {
        const uint32_t k=sx8(R8(GVA(0x4aa888u)+index));
        r.eax=k;r.edx=R32(GVA(0x4a9db9u));
        a[0]=r.edx;a[1]=k;ml_call(g,0x471cb0u,a,2,&r);
        break;
    }
    case 3: {
        const uint32_t k=sx8(R8(GVA(0x4aa946u)+index));
        r.ecx=k;r.eax=R32(GVA(0x4a95a0u));
        a[0]=r.eax;a[1]=k;ml_call(g,0x471cb0u,a,2,&r);
        break;
    }
    default:
        r.eax=0;
        break;
    }
    ecx_out(ecx,r.ecx);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x411b90. */
uint64_t es2_action_copy_registers(const es2_guest_memory *m,uint32_t actor,uint32_t ids,uint32_t actions,
    uint32_t *ecx) {
    uint32_t d=actor+0x1bau,first=ids,second=actions;
    for(unsigned i=0;i<10;++i) {
        W16(d,R16(first));
        W16(d+0x14u,R16(second));
        first+=2u;second+=2u;d+=2u;
    }
    ecx_out(ecx,second);
    return pair(d,first);
}

/* (100 - word) x value / 100 as 4150b3..4150bb compute it: IMUL, CDQ,
 * IDIV by 100; EAX the quotient, EDX the remainder. */
static void ml_percent(uint32_t word,uint32_t value,uint32_t *eax,uint32_t *edx) {
    /* CDQ keeps only the low dword of the product (it cannot overflow for
     * word operands, |product| < 2^31). */
    const int32_t product=(int32_t)fx_s32((uint32_t)((int64_t)(int32_t)(100u-sx(word))*(int32_t)value));
    *eax=(uint32_t)(product/100);*edx=(uint32_t)(product%100);
}

/* Original: DBSIM.EXE 0x415068. C = conditions: [C] the second table, [C+4]
 * the first, [C+0Ch] the type's definition T with the maxima table [T]
 * (rows of nine words) and the word list [T+4]; C+0Ch and [T] are reread
 * for every row, as the original does. */
uint64_t es2_condition_init_registers(const es2_guest_memory *m,uint32_t conditions,uint32_t first,
    uint32_t second,uint32_t third,uint32_t *ecx) {
    const uint32_t C=conditions;
    uint32_t list=R32(R32(C+0xcu)+4u);
    uint32_t out=R32(C+4u),out2=R32(C);
    uint32_t eax=0,edx=0;
    for(uint32_t i=0;i<19;++i) {
        if(i<13) {
            const uint32_t word=R16(first);
            const uint32_t maxima=R32(R32(C+0xcu));
            ml_percent(word,sx(R16(maxima+18u*i)),&eax,&edx);
            W16(out,eax);
        } else W16(out,0);
        first+=2u;out+=2u;
    }
    for(uint32_t i=0;i<12;++i) {
        if(i<9) {
            const uint32_t word=R16(second);
            ml_percent(word,sx(R16(list)),&eax,&edx);
            W16(out2,eax);
        } else W16(out2,0);
        list+=2u;second+=2u;out2+=2u;
    }
    for(uint32_t i=0;i<10;++i) {
        const uint32_t row=sx(i+0x13u);
        const uint16_t top=R16(R32(R32(C+0xcu))+18u*row);
        const uint16_t sum=(uint16_t)(R16(list)+top);
        ml_percent(R16(third),sx(sum),&eax,&edx);
        if(s16(eax)>s16(top)) {
            W16(out,0xffffu);
            eax=lo(eax,eax-top);
            edx=list;
            if(s16(eax)>s16(R16(list)))W16(out2,0xffffu);
            else W16(out2,eax);
        } else W16(out,eax);
        out+=2u;out2+=2u;third+=2u;list+=2u;
    }
    /* 41519e: the second POP ECX reloads the list pointer [EBP-4]. */
    ecx_out(ecx,list);
    return pair(eax,edx);
}

/* Original: DBSIM.EXE 0x423358. */
uint64_t es2_region_init_registers(const es2_guest_memory *m,uint32_t region,uint32_t record,uint32_t points,
    uint32_t *ecx) {
    uint32_t c=12u*sx(R16(record+2u))+points;
    W32(region+2u,c);
    const uint16_t kind=R16(record);
    W16(region,kind);
    c=lo(c,kind);
    if(!kind) {
        const uint32_t d=12u*sx(R16(record+4u));
        W32(region+6u,points+d);
        ecx_out(ecx,c);
        return pair(region,d);
    }
    const uint32_t v=sx(R16(record+4u));
    W32(region+6u,10u*v);
    ecx_out(ecx,v);
    return pair(region,10u*v);
}

/* Original: DBSIM.EXE 0x423104. */
uint64_t es2_timer_init_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t timer,
    uint32_t first,uint32_t words,uint32_t duration,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={first,entry[1],entry[2]};uint32_t a[2];
    W32(timer,first);
    /* 423110: MOV DX keeps the entry EDX's high word in the pushed slot. */
    r.edx=lo(r.edx,duration);r.ecx=timer+0x2cu;
    a[0]=r.ecx;a[1]=r.edx;ml_call(g,0x467ac4u,a,2,&r);
    uint32_t d=timer+4u,s=words;
    for(unsigned i=0;i<10;++i) {
        W32(d,R32(s));
        d+=4u;s+=4u;
    }
    ecx_out(ecx,10u);
    return pair(s,d);
}

/* Original: DBSIM.EXE 0x423d38. */
uint64_t es2_group_array_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t groups,
    uint32_t count,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={sx(count),entry[1],entry[2]};uint32_t a[5];
    W16(groups+4u,count);
    a[0]=0;a[1]=0x7au;a[2]=sx(count);a[3]=1;a[4]=GVA(0x4240e4u);
    ml_call(g,0x49667cu,a,5,&r);
    W32(groups,r.eax);
    ecx_out(ecx,r.ecx);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x4178e8. */
uint64_t es2_initial_damage_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t actor,
    uint32_t word,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t A=actor;
    es2_weapon_registers r={word,entry[1],entry[2]};uint32_t a[4];
    const int32_t w=s16(word);
    uint32_t severity=0,local=0;
    int local_set=0;
    if(w>-1&&w<0x50) {
        if(w>=0x3c)severity=0x32u;
        else if(w>=0x28)severity=0x50u;
        else if(w>=0x14) {
            /* 417931: the word +0Ah of the condition word list [T+4]
             * (T = [A+212h]), scaled by multiply_q8(100h, word), into the
             * word +0Ah of the table [A+206h]. */
            severity=0x78u;
            const uint32_t list=R32(R32(A+0x206u+0xcu)+4u);
            local=list;local_set=1;   /* [EBP-4] */
            const uint32_t v=sx(R16(list+0xau));
            r.eax=v;r.edx=v;r.ecx=list;
            a[0]=0x100u;a[1]=v;ml_call(g,0x47e0a0u,a,2,&r);
            const uint32_t table=R32(A+0x206u);
            r.ecx=table;
            W16(table+0xau,r.eax);
            r.eax=A+0x92u;
            W8(A+0xb3u,1);
        } else {
            const uint32_t stream=GVA(0x4d2599u);
            severity=0x96u;
            a[0]=stream;ml_call(g,0x492ee0u,a,1,&r);
            r.ecx=stream;   /* POP ECX reloads the pushed stream */
            r.eax=lo(r.eax,(r.eax&1u)?7u:8u);
            a[0]=A;a[1]=r.eax;a[2]=0x7d00u;a[3]=0;
            r.eax=R32(A);
            ml_virtual(g,R32(r.eax+0x74u),0x74u,a,4,&r);
            r.edx=R32(A+0x1f2u);
            if(s16(R16(r.edx+0x4au))>2) {
                a[0]=stream;ml_call(g,0x492ee0u,a,1,&r);
                r.ecx=stream;
                r.eax=lo(r.eax,(r.eax&1u)?0xdu:0xeu);
                a[0]=A;a[1]=r.eax;a[2]=0x7d00u;a[3]=0;
                r.edx=R32(A);
                ml_virtual(g,R32(r.edx+0x74u),0x74u,a,4,&r);
            }
            r.eax=A+0x92u;r.edx=r.eax;
            W8(A+0xa4u,1);
            W8(A+0xb3u,1);
            W8(A+0xb4u,1);
        }
    }
    r.eax=severity+0x19u;
    a[0]=A;a[1]=severity;a[2]=r.eax;ml_call(g,0x417a04u,a,3,&r);
    /* 4179fd: the second POP ECX reloads [EBP-4], written only on the
     * 20..39 path; otherwise the original returns that uninitialized
     * local (stack residue, dead at the only caller) and *ecx is left
     * unchanged. */
    if(local_set)ecx_out(ecx,local);
    return pair(r.eax,r.edx);
}

/* ---- L8: the pool-only stage-2 callbacks (mission_load.md, section
 * "L8 stage-2 pool callbacks"). f is the synthetic EBP; the locals and the
 * runtime record lie at the original's offsets below it. */
/* new(12h), and when it succeeds the context word `in`, pool_create(p,
 * capacity, size) and the context word `out`; the pointer (or 0) into the
 * global. ECX = the pointer reread from its slot. */
static void sp_pool(const es2_guest_memory *m,const es2_weapon_graph *g,es2_weapon_registers *r,uint32_t slot,
    uint32_t context,uint32_t in,uint32_t out,uint32_t capacity,uint32_t size,uint32_t global) {
    uint32_t a[3]={0x12u};
    ml_call(g,0x47749cu,a,1,r);
    W32(slot,r->eax);
    r->ecx=0x12u;                                   /* POP ECX */
    if(r->eax) {
        W16(context,in);
        r->edx=R32(slot);
        a[0]=r->edx;a[1]=capacity;a[2]=size;
        ml_call(g,0x471ad8u,a,3,r);
        W16(context,out);
    }
    r->ecx=R32(slot);
    W32(GVA(global),r->ecx);
}
/* new(8), and when it succeeds: word +6 = capacity (MOV AX keeps EAX's high
 * word), [list] = new[](4 x capacity), word +4 = 0, with the context words;
 * the pointer (or 0) into the global. EDX = the pointer reread. */
static void sp_list(const es2_guest_memory *m,const es2_weapon_graph *g,es2_weapon_registers *r,uint32_t slot,
    uint32_t context,uint32_t in,uint32_t out,uint32_t capacity,uint32_t global) {
    uint32_t a[1]={8u};
    ml_call(g,0x47749cu,a,1,r);
    W32(slot,r->eax);
    r->ecx=8u;                                      /* POP ECX */
    if(r->eax) {
        W16(context,in);
        r->edx=R32(slot);
        r->eax=lo(r->eax,capacity);
        W16(r->edx+6u,capacity);
        r->edx=sx(capacity)<<2;
        a[0]=r->edx;
        ml_call(g,0x4774f0u,a,1,r);
        r->ecx=R32(slot);                           /* after POP ECX */
        W32(r->ecx,r->eax);
        r->eax=R32(slot);
        W16(r->eax+4u,0);
        W16(context,out);
    }
    r->edx=R32(slot);
    W32(GVA(global),r->edx);
}

/* Original: DBSIM.EXE 0x4076e4. Frame: record f-2Ch (context word f-1Ch),
 * table f-4, pool f-8, list f-0Ch. */
uint64_t es2_light_pools_registers(const es2_guest_memory *m,const es2_weapon_graph *g,const uint32_t entry[3],
    uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_STAGE_POOLS_FRAME,context=f-0x1cu;
    es2_weapon_registers r={GVA(0x497b16u),entry[1],entry[2]};uint32_t a[1];
    ml_call(g,0x49630cu,NULL,0,&r);
    a[0]=0x404u;ml_call(g,0x47749cu,a,1,&r);
    W32(f-4u,r.eax);
    r.ecx=0x404u;
    if(r.eax) {
        W16(context,0x14u);
        r.edx=R32(f-4u);
        a[0]=r.edx;ml_call(g,0x406e44u,a,1,&r);
        r.ecx=R32(GVA(0x6cbee4u));                 /* the POP ECX value is overwritten */
        W32(r.ecx,R32(r.ecx)-5u);
        W16(context,8u);
    }
    const uint32_t table=R32(f-4u);
    W32(GVA(0x4a9608u),table);
    /* 406ee4(table, 10, 2000): +10h = 10 >> 5, +14h = 2000 >> 5 (SAR). A
     * null table faults here in the original (PROVED edge of 407705). */
    r.eax=table;
    r.edx=(uint32_t)(10>>5);
    W32(table+0x10u,r.edx);
    r.ecx=(uint32_t)(2000>>5);
    W32(table+0x14u,r.ecx);
    r.eax=R32(GVA(0x4a9608u));
    W32(r.eax+0xcu,0x2710u);
    sp_pool(m,g,&r,f-8u,context,0x2cu,0x20u,3u,0xeu,0x4a95feu);
    sp_list(m,g,&r,f-0xcu,context,0x38u,0x20u,0x19u,0x4a9602u);
    r.eax=R32(f-0x2cu);
    ecx_out(ecx,r.ecx);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x4090cc. Frame: record f-28h (context word f-18h),
 * pool f-4, list f-8. */
uint64_t es2_smoke_pools_registers(const es2_guest_memory *m,const es2_weapon_graph *g,const uint32_t entry[3],
    uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_STAGE_POOLS_FRAME,context=f-0x18u;
    es2_weapon_registers r={GVA(0x497f5eu),entry[1],entry[2]};
    ml_call(g,0x49630cu,NULL,0,&r);
    sp_pool(m,g,&r,f-4u,context,0x14u,8u,0x14u,0x2au,0x4a9684u);
    sp_list(m,g,&r,f-8u,context,0x20u,8u,0x19u,0x4a9688u);
    ml_call(g,0x408fb8u,NULL,0,&r);                 /* the smoke-ball set-up (resources) */
    r.eax=R32(f-0x28u);
    ecx_out(ecx,r.ecx);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x40a060. Frame: record f-28h (context word f-18h),
 * pool f-4, list f-8. */
uint64_t es2_projectile_pools_registers(const es2_guest_memory *m,const es2_weapon_graph *g,const uint32_t entry[3],
    uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_STAGE_POOLS_FRAME,context=f-0x18u;
    es2_weapon_registers r={GVA(0x4982ecu),entry[1],entry[2]};
    ml_call(g,0x49630cu,NULL,0,&r);
    sp_pool(m,g,&r,f-4u,context,0x14u,8u,0xa0u,0x66u,0x4a96c2u);
    sp_list(m,g,&r,f-8u,context,0x20u,8u,0x50u,0x4a96c6u);
    r.eax=R32(f-0x28u);
    ecx_out(ecx,r.ecx);
    return pair(r.eax,r.edx);
}

/* ---- L4: the weapon fit (mission_load.md, section "L4 weapon fit"). f
 * is the synthetic EBP; the locals and the runtime record lie at the original's
 * offsets below it. The construction counter is the dword at [6CBEE4h]. */
static uint32_t ml_counter(const es2_guest_memory *m,const es2_weapon_graph *g) { return R32(GVA(0x6cbee4u)); }

/* One case block of the weapon factory's dispatch (410146 .. 41049f): the
 * allocator 47a2c8(size, heap) into the frame slot, and when it succeeds the
 * context word `in`, the constructor and the context word `out`; the object
 * (or 0) into the weapon slot [f-50h]. Styles: 'A' energy/ballistic
 * (constructor with the projectile record, construction count -6), 'B' the
 * ELF/LAEW variants (the arguments staged in four locals at f-L, the
 * vtable stored after the constructor, count +1 then -7), 'E' equipment
 * (no projectile record, count -7). */
typedef struct wf_case_block { uint8_t style; uint8_t size,slot,in,out,locals; uint32_t ctor,vtable; } wf_case_block;
static const wf_case_block wf_blocks[10]={
    {0,0,0,0,0,0,0,0},
    {'E',0x7du,0x24u,0xd4u,0xc8u,0,0x40e380u,0},          /* 410453 ENERGY */
    {'E',0x85u,0x20u,0xbcu,0xb0u,0,0x40e2bcu,0},          /* 410400 TURBO */
    {'E',0x81u,0x1cu,0xa4u,0x98u,0,0x40e344u,0},          /* 4103aa SHIELD */
    {'E',0x8au,0x18u,0x8cu,0x80u,0,0x40e308u,0},          /* 410354 TARG */
    {'A',0x83u,0x04u,0x14u,0x08u,0,0x40e074u,0},          /* 410146 energy weapons */
    {'B',0x85u,0x10u,0x5cu,0x50u,0x70u,0x40e140u,0x499248u},/* 41027c LAEW */
    {'B',0x86u,0x08u,0x2cu,0x20u,0x60u,0x40e074u,0x4992c0u},/* 4101a0 ELF */
    {'A',0x81u,0x0cu,0x44u,0x38u,0,0x40e140u,0},          /* 410222 ballistic */
    {'E',0x81u,0x14u,0x74u,0x68u,0,0x40e274u,0},          /* 4102fe ECM */
};
static void wf_case(const es2_guest_memory *m,const es2_weapon_graph *g,es2_weapon_registers *r,uint32_t f,
    const wf_case_block *b,uint32_t actor,uint32_t mount,uint32_t record,uint32_t word,uint32_t *ebx) {
    const uint32_t context=f-0x34u,slot=f-b->slot;uint32_t a[6];
    const uint32_t heap=R32(f-0x5cu);
    if(b->style=='B')r->ecx=heap;else r->edx=heap;
    a[0]=b->size;a[1]=heap;ml_call(g,0x47a2c8u,a,2,r);
    W32(slot,r->eax);
    if(b->style=='A') {
        if(r->eax) {
            W16(context,b->in);
            r->edx=lo(r->edx,word);a[5]=r->edx;
            r->ecx=R32(f-0x58u);a[4]=r->ecx;a[3]=record;a[2]=mount;
            r->eax=actor;a[1]=actor;
            r->edx=R32(slot);a[0]=r->edx;
            ml_call(g,b->ctor,a,6,r);
            r->ecx=ml_counter(m,g);W32(r->ecx,R32(r->ecx)-6u);
            W16(context,b->out);
        }
        r->eax=R32(slot);
        r->edx=R32(f-0x50u);W32(r->edx,r->eax);
        return;
    }
    if(r->eax) {
        W16(context,b->in);
        if(b->style=='B') {
            const uint32_t L=f-b->locals;
            *ebx=lo(*ebx,word);
            r->edx=R32(f-0x58u);W32(L,r->edx);
            W32(L-4u,record);W32(L-8u,mount);
            r->ecx=actor;W32(L-0xcu,r->ecx);
            a[5]=*ebx;
            r->eax=R32(L);a[4]=r->eax;
            r->edx=R32(L-4u);a[3]=r->edx;
            r->ecx=R32(L-8u);a[2]=r->ecx;
            r->eax=R32(L-0xcu);a[1]=r->eax;
            r->edx=R32(slot);a[0]=r->edx;
            ml_call(g,b->ctor,a,6,r);
            r->ecx=R32(slot);W32(r->ecx,GVA(b->vtable));
            r->eax=ml_counter(m,g);W32(r->eax,R32(r->eax)+1u);
            r->edx=ml_counter(m,g);W32(r->edx,R32(r->edx)-7u);
        } else {
            r->edx=lo(r->edx,word);a[4]=r->edx;a[3]=record;a[2]=mount;
            r->ecx=actor;a[1]=actor;
            r->eax=R32(slot);a[0]=r->eax;
            ml_call(g,b->ctor,a,5,r);
            r->edx=ml_counter(m,g);W32(r->edx,R32(r->edx)-7u);
        }
        W16(context,b->out);
    }
    r->ecx=R32(slot);
    r->eax=R32(f-0x50u);W32(r->eax,r->ecx);
}

/* Original: DBSIM.EXE 0x40fff8. Frame: record f-44h (context word f-34h),
 * the slot cursor f-4Ch and the remaining count f-48h, the weapon slot
 * f-50h, the secondary word f-52h, the projectile record f-58h, the heap
 * f-5Ch, the case slots f-4 .. f-24h and the staging locals f-60h .. f-7Ch.
 * ESI walks the mounts, EDI holds the weapon record, EBX the item word. */
uint64_t es2_weapon_factory_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t system,
    uint32_t actor,uint32_t count,uint32_t mounts,uint32_t items,uint32_t secondary,uint32_t word,uint32_t ebx,
    const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_WEAPON_FACTORY_FRAME,W=system;
    es2_weapon_registers r={GVA(0x499055u),entry[1],entry[2]};uint32_t a[2];
    uint32_t mount=mounts;
    ml_call(g,0x49630cu,NULL,0,&r);
    /* 41000e: vtable, the slot array object at W+4 (pointer, count word),
     * construction count +1. */
    r.edx=W;W32(W,GVA(0x499338u));
    r.eax=W+4u;r.edx=0;W32(r.eax,0);
    r.ecx=ml_counter(m,g);W32(r.ecx,R32(r.ecx)+1u);
    ebx=lo(ebx,count);
    W16(W+8u,ebx);
    r.eax=sx(ebx)<<2;a[0]=r.eax;ml_call(g,0x4774f0u,a,1,&r);
    r.ecx=a[0];                                     /* POP ECX */
    W32(W+4u,r.eax);
    r.eax=W+4u;r.edx=R32(r.eax);W32(f-0x4cu,r.edx);
    r.ecx=lo(r.ecx,R16(r.eax+4u));W16(f-0x48u,r.ecx);
    a[0]=0x30d40u;ml_call(g,0x474bbcu,a,1,&r);
    r.ecx=0x30d40u;
    for(;;) {
        /* 4104a4: the old count word decides; the cursor advances by 4. */
        r.edx=lo(r.edx,R16(f-0x48u));
        W16(f-0x48u,R16(f-0x48u)-1u);
        if((uint16_t)r.edx) {
            r.eax=R32(f-0x4cu);
            W32(f-0x4cu,R32(f-0x4cu)+4u);
        } else r.eax=0;
        W32(f-0x50u,r.eax);
        if(!r.eax)break;
        /* 410069: the item word of the mount's index byte; the flags of
         * TEST BX,BX are taken before the secondary word is read. */
        r.eax=sx8(R8(mount+0x17u));
        r.edx=items;ebx=lo(ebx,R16(r.edx+2u*r.eax));
        r.edx=secondary;
        const int negative=s16(ebx)<0;
        r.eax=lo(r.eax,R16(r.edx+2u*r.eax));
        W16(f-0x52u,r.eax);
        if(negative)ebx=0;
        if(!(uint16_t)ebx) {
            r.eax=R32(f-0x50u);r.edx=0;W32(r.eax,0);
            mount+=0x1au;
            continue;
        }
        a[0]=ebx;ml_call(g,0x40fe84u,a,1,&r);
        r.ecx=ebx;                                  /* POP ECX */
        const uint32_t record=r.eax;
        r.eax=lo(r.eax,R16(record+0x3eu));
        if((uint16_t)r.eax==0x22u) {
            if(R16(f-0x52u)==5u)W16(f-0x52u,0);
            r.edx=lo(r.edx,R16(f-0x52u));
            a[0]=0;a[1]=r.edx;ml_call(g,0x40ffc8u,a,2,&r);
            W32(f-0x58u,r.eax);
        } else if((uint16_t)r.eax!=0x21u) {
            a[0]=r.eax;ml_call(g,0x40ffb0u,a,1,&r);
            r.ecx=a[0];                             /* POP ECX */
            W32(f-0x58u,r.eax);
        }
        /* 4100db: IDs above 20h build nothing and leave the slot as it is. */
        r.eax=sx(ebx);
        r.ecx=R32(GVA(0x6c5830u));
        W32(f-0x5cu,r.ecx);
        if(r.eax<=0x20u) {
            const uint8_t index=R8(GVA(0x4100fdu)+r.eax);
            r.eax=(r.eax&0xffffff00u)|index;
            if(index&&index<10u)wf_case(m,g,&r,f,wf_blocks+index,actor,mount,record,word,&ebx);
        }
        mount+=0x1au;
    }
    ml_call(g,0x474be4u,NULL,0,&r);
    r.edx=ml_counter(m,g);W32(r.edx,R32(r.edx)+1u);
    r.ecx=R32(f-0x44u);
    ecx_out(ecx,r.ecx);
    return pair(W,r.edx);
}

/* Original: DBSIM.EXE 0x4104ec. [EBP-4] is the entry ECX (PUSH ECX), its
 * words replaced in the membership loop and returned by POP ECX. */
uint64_t es2_player_weapons_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t system,
    uint32_t actor,uint32_t count,uint32_t mounts,uint32_t items,uint32_t secondary,const uint32_t entry[3],
    uint32_t *ecx) {
    const uint32_t P=system;
    es2_weapon_registers r={lo(secondary,count),actor,P};uint32_t a[7];
    uint32_t local=entry[2];
    a[0]=P;a[1]=actor;a[2]=r.eax;a[3]=mounts;a[4]=items;a[5]=secondary;a[6]=1;
    ml_call(g,0x40fff8u,a,7,&r);
    r.eax=P;W32(P,GVA(0x499238u));
    r.edx=P+0x1fu;
    a[0]=r.edx;a[1]=6;a[2]=3;a[3]=1;a[4]=GVA(0x411892u);
    ml_call(g,0x49667cu,a,5,&r);
    /* 41053b: three groups of six bytes at P+1Fh: the array pointer and
     * the count word. */
    for(uint32_t k=0;k<3;++k) {
        const uint32_t group=P+0x1fu+6u*k;
        r.eax=2u*sx(count);
        W16(group+4u,count);
        a[0]=r.eax;ml_call(g,0x4774f0u,a,1,&r);
        r.ecx=a[0];                                 /* POP ECX */
        W32(group,r.eax);
    }
    r.edx=P;W8(P+0x1du,0xffu);
    for(uint32_t b=0;;++b) {
        r.eax=sx(count);
        if((int32_t)b>=(int32_t)r.eax)break;
        r.eax=b;r.edx=P;r.ecx=R32(P+4u);
        r.edx=R32(r.ecx+4u*r.eax);r.eax=r.edx;
        if(!r.edx)continue;
        /* 410588: the record index word of the weapon's record. */
        r.edx=R32(r.eax+0x1cu);
        r.ecx=b;
        r.edx=lo(r.edx,R16(r.edx+0x56u));
        (void)R32(P+4u);                            /* MOV ESI,[ESI+4] (unused) */
        const uint16_t id=(uint16_t)r.edx;
        r.eax=(id==0x12u||id==0x1du||id==0x1eu||id==0x1fu||id==0x20u)?1u:0u;
        if(r.eax)local=(local&0xffffu)|0xffff0000u;
        else {
            local&=0xffffu;
            r.ecx=P;
            if((int8_t)R8(P+0x1du)<0) {
                r.eax=P;W8(P+0x1du,b);
            }
        }
        /* 4105e1: membership word 1 in group k when k equals [EBP-2]. */
        r.eax=0;r.edx=P+0x1fu;
        do {
            r.ecx=lo(r.ecx,1);
            if((uint16_t)r.eax!=(uint16_t)(local>>16))r.ecx-=1u;
            local=(local&0xffff0000u)|(uint16_t)b;
            W16(R32(r.edx)+2u*sx(local),r.ecx);
            r.eax+=1u;r.edx+=6u;
        } while(s16(r.eax)<3);
    }
    a[0]=P;a[1]=0;r.edx=P;
    ml_call(g,0x410ae4u,a,2,&r);
    r.eax=P+0x14u;W8(r.eax+2u,1);
    r.edx=ml_counter(m,g);W32(r.edx,R32(r.edx)+1u);
    ecx_out(ecx,local);
    return pair(P,r.edx);
}

/* Original: DBSIM.EXE 0x4175dc. Frame: record f-28h (context word f-18h),
 * the new objects f-4/f-8, per weapon C f-2Ch, C+12h f-30h, C f-34h, the
 * mount offset f-38h, the words f-3Ah/f-3Ch/f-3Eh/f-40h, and the three
 * part-list buffers f-12Ch, f-0F0h, f-0B4h. EBX = actor, ESI the weapon
 * index, EDI the weapon. */
uint64_t es2_weapon_fit_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t actor,
    uint32_t items,uint32_t secondary,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_WEAPON_FIT_FRAME,context=f-0x18u,A=actor;
    es2_weapon_registers r={GVA(0x49a0c0u),entry[1],entry[2]};uint32_t a[7];
    ml_call(g,0x49630cu,NULL,0,&r);
    a[0]=A;ml_call(g,0x420634u,a,1,&r);
    const uint32_t M=r.eax;                         /* ESI: the mount table */
    r.eax=A+0x92u;r.ecx=A;                          /* POP ECX */
    if(R8(r.eax+0x11u)) {
        a[0]=0x35u;ml_call(g,0x47749cu,a,1,&r);
        r.ecx=0x35u;
        W32(f-4u,r.eax);
        if(r.eax) {
            W16(context,0x14u);
            r.edx=secondary;
            r.ecx=R32(M+2u);
            r.eax=lo(r.eax,R16(M));
            r.edx=R32(f-4u);
            a[0]=r.edx;a[1]=A;a[2]=r.eax;a[3]=r.ecx;a[4]=items;a[5]=secondary;
            ml_call(g,0x4104ecu,a,6,&r);
            r.ecx=ml_counter(m,g);W32(r.ecx,R32(r.ecx)-6u);
            W16(context,8u);
        }
        r.eax=R32(f-4u);
    } else {
        a[0]=0x14u;ml_call(g,0x47749cu,a,1,&r);
        r.ecx=0x14u;
        W32(f-8u,r.eax);
        if(r.eax) {
            W16(context,0x2cu);
            r.edx=secondary;
            r.ecx=R32(M+2u);
            r.eax=lo(r.eax,R16(M));
            r.edx=R32(f-8u);
            a[0]=r.edx;a[1]=A;a[2]=r.eax;a[3]=r.ecx;a[4]=items;a[5]=secondary;a[6]=0;
            ml_call(g,0x40fff8u,a,7,&r);
            r.ecx=ml_counter(m,g);W32(r.ecx,R32(r.ecx)-2u);
            W16(context,0x20u);
        }
        r.eax=R32(f-8u);
    }
    W32(A+0x202u,r.eax);
    a[0]=A;a[1]=0;ml_call(g,0x402604u,a,2,&r);
    for(uint32_t i=0;;++i) {
        r.edx=R32(A+0x202u);
        r.eax=sx(R16(r.edx+8u));
        if((int32_t)i>=(int32_t)r.eax)break;
        r.eax=i;r.edx=R32(r.edx+4u);r.eax=sx(r.eax);
        const uint32_t W=R32(r.edx+4u*r.eax);
        if(!W)continue;
        /* 4176ce: the weapon record C. */
        r.eax=R32(W+0x1cu);W32(f-0x2cu,r.eax);
        r.edx=R32(f-0x2cu)+0x12u;W32(f-0x30u,r.edx);
        r.ecx=R32(f-0x2cu);W32(f-0x34u,r.ecx);
        a[0]=W;ml_call(g,0x40f540u,a,1,&r);
        r.ecx=W;                                    /* POP ECX */
        W32(f-0x38u,r.eax);
        /* 4176ed: the mount's index byte D+17h gives the words +13h and +0Ch. */
        r.eax=R32(W+0x18u);r.edx=sx8(R8(r.eax+0x17u));r.edx=lo(r.edx,r.edx+0x13u);W16(f-0x3au,r.edx);
        r.ecx=R32(W+0x18u);r.eax=sx8(R8(r.ecx+0x17u));r.eax=lo(r.eax,r.eax+0xcu);W16(f-0x3cu,r.eax);
        r.edx=R32(f-0x2cu);r.ecx=lo(r.ecx,R16(r.edx+0x2au));W16(f-0x3eu,r.ecx);
        if(R32(W+0x10u)) {
            a[0]=W;ml_call(g,0x40e61cu,a,1,&r);
            r.ecx=W;                                /* POP ECX */
            W16(f-0x40u,r.eax);
            r.eax=lo(r.eax,R16(f-0x40u));
            const uint32_t word=r.eax;
            r.edx=R32(A+0x34u);r.ecx=R32(r.edx+4u);r.eax=R32(r.ecx);
            a[0]=r.ecx;a[1]=word;ml_virtual(g,R32(r.eax+0x20u),0x20u,a,2,&r);
            r.eax=lo(r.eax,R16(r.eax+4u));
            const uint32_t part=r.eax;
            r.edx=R32(W+0x10u);
            a[0]=r.edx;a[1]=part;ml_call(g,0x417530u,a,2,&r);
        }
        /* 41774c: the record words C+18h and [C+8]+2, the byte C+4 from the
         * part table [[A+1FAh]] (rows of 18 bytes, byte +4 of row w). */
        r.eax=lo(r.eax,R16(f-0x3au));r.edx=R32(f-0x30u);W16(r.edx+6u,r.eax);
        r.eax=lo(r.eax,R16(f-0x3cu));r.edx=R32(f-0x34u);r.ecx=R32(r.edx+8u);W16(r.ecx+2u,r.eax);
        r.eax=sx(R16(f-0x3au));r.ecx=R32(A+0x1fau);r.edx=9u*r.eax;r.eax=R32(r.ecx);r.ecx=R32(f-0x34u);
        r.edx=(r.edx&0xffffff00u)|R8(r.eax+2u*r.edx+4u);
        r.eax=f-0x38u;W8(r.ecx+4u,r.edx);
        r.edx=f-0x30u;r.ecx=R32(A+0x1f6u);
        a[0]=r.ecx;a[1]=r.edx;a[2]=r.eax;a[3]=1;ml_call(g,0x40cec0u,a,4,&r);
        r.eax=f-0x34u;r.edx=f-0x3au;r.ecx=R32(A+0x1fau);
        a[0]=r.ecx;a[1]=r.edx;a[2]=r.eax;a[3]=1;ml_call(g,0x40d240u,a,4,&r);
        r.eax=f-0x3eu;r.edx=f-0x3cu;r.ecx=R32(A+0x1fau);
        a[0]=r.ecx;a[1]=r.edx;a[2]=r.eax;a[3]=1;ml_call(g,0x40d128u,a,4,&r);
    }
    /* 4177dc: the part lists into the three frame buffers, then 40d284 with
     * 40cb84's count. */
    r.ecx=R32(A+0x1f6u);r.eax=f-0xf0u;r.edx=f-0x12cu;
    a[0]=r.ecx;a[1]=f-0x12cu;a[2]=f-0xf0u;a[3]=f-0xb4u;ml_call(g,0x40cb84u,a,4,&r);
    a[1]=r.eax;
    r.edx=f-0x12cu;r.ecx=f-0xf0u;r.eax=R32(A+0x1fau);
    a[0]=r.eax;a[2]=f-0x12cu;a[3]=f-0xf0u;a[4]=f-0xb4u;ml_call(g,0x40d284u,a,5,&r);
    /* 417825: the 29 condition words of parts whose table word +6 is 0. */
    for(uint32_t k=0;k<0x1du;++k) {
        r.eax=k;r.ecx=9u*k;
        r.edx=R32(R32(A+0x1fau));
        if(!R16(r.edx+2u*r.ecx+6u)) {
            r.ecx=R32(A+0x20eu);W16(r.ecx+2u*k,0);
        }
    }
    r.eax=A+0x307u;r.edx=R32(A+0x202u);
    a[0]=r.edx;a[1]=r.eax;ml_call(g,0x40fb2cu,a,2,&r);
    a[0]=A;ml_call(g,0x417becu,a,1,&r);
    r.ecx=A+0x222u;a[0]=r.ecx;ml_call(g,0x413ac8u,a,1,&r);
    r.ecx=A+0x222u;                                 /* POP ECX */
    a[0]=A;ml_call(g,0x417d08u,a,1,&r);
    r.ecx=A;                                        /* POP ECX */
    r.eax=R32(f-0x28u);
    ecx_out(ecx,r.ecx);
    return pair(r.eax,r.edx);
}

/* ---- L5: the constructors of the actor load (mission_load.md, section
 * "L5 constructors"). f is the synthetic EBP of the framed ones; the locals and
 * the runtime record lie at the original's offsets below it. */
/* 41ff00 / 422e80 (type): the byte at table + sx(type word); EAX = that
 * byte, EDX and ECX unchanged (computed here). */
static uint32_t ml_type_index(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t table,uint32_t type) {
    return R8(GVA(table)+(uint32_t)s16(type));
}
/* 411dd4 (list, object): [[list] + 4 x sx(count)] = object with the count
 * word at list+6, which is reread (the store may hit it), incremented and
 * stored at object+4Bh. EAX = ECX = sx(first count) with its low word
 * replaced by the reread count, EDX = object (computed here). */
static void ml_list_append(const es2_guest_memory *m,es2_weapon_registers *r,uint32_t list,uint32_t object) {
    r->edx=object;r->eax=list;
    r->ecx=sx(R16(list+6u));
    W32(R32(list)+4u*r->ecx,object);
    r->ecx=lo(r->ecx,R16(list+6u));
    W16(list+6u,R16(list+6u)+1u);
    r->eax=r->ecx;
    W16(object+0x4bu,r->eax);
}
/* [[6CBEE4h]] += delta, the pointer read first; returns the pointer. */
static uint32_t ml_count_add(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t delta) {
    const uint32_t p=R32(GVA(0x6cbee4u));W32(p,R32(p)+delta);return p;
}

/* Original: DBSIM.EXE 0x415bb0, with the leaves 0x41ff00 (twice), 0x411dd4
 * and 0x413a90 computed here. Frame: record f-24h (context word f-14h), the
 * flat object f-4 (its copy f-44h), the shape word f-46h, the dead stores
 * f-28h .. f-40h and f-4Ch, and the vector f-58h .. f-4Dh that rotate_point
 * reads. EBX = A, ESI the type word, then the type record cursor and the
 * name; EDI = A+92h, then the flat-object index and the part table. */
uint64_t es2_mech_ctor_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t actor,uint32_t type,
    uint32_t esi,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_MECH_CTOR_FRAME,context=f-0x14u,A=actor;
    es2_weapon_registers r={GVA(0x49a080u),entry[1],entry[2]};uint32_t a[5];
    ml_call(g,0x49630cu,NULL,0,&r);
    /* 415bc6: MOV SI keeps the caller's high word in the pushed dword; POP
     * ECX returns it. */
    esi=lo(esi,type);
    r.eax=ml_type_index(m,g,0x4a9d08u,esi);
    r.edx=r.eax;r.ecx=esi;
    r.eax=0x21eu*r.eax+R32(GVA(0x4a9d64u))+0xdeu;
    a[0]=A;a[1]=r.eax;ml_call(g,0x4033a4u,a,2,&r);
    W32(A,GVA(0x49a54cu));
    r.ecx=A+0x4du;W32(f-0x28u,r.ecx);
    const uint32_t A92=A+0x92u;                    /* EDI */
    W16(A+0x1a8u,0xffffu);
    r.eax=A92;W8(r.eax+6u,1);W8(r.eax+0x15u,1);
    ml_list_append(m,&r,GVA(0x4a9af8u),A);
    r.edx=0;r.eax=0xffffffffu;
    W32(A+0x1b6u,r.edx);W32(A+0x1b2u,r.edx);
    W16(A+0x1beu,r.eax);W16(A+0x1bcu,r.eax);
    esi=A+0x206u;
    W16(A+0x1bau,r.eax);
    r.edx=ml_count_add(m,g,1);
    W32(A,GVA(0x49a282u));
    a[0]=esi;a[1]=0x1du;a[2]=0x16u;ml_call(g,0x40d2ccu,a,3,&r);
    r.eax=A+0x23eu;W32(f-0x2cu,r.eax);
    r.edx=A+0x258u;
    a[0]=r.edx;a[1]=3;a[2]=5;a[3]=1;a[4]=GVA(0x41c03eu);ml_call(g,0x49667cu,a,5,&r);
    /* 415c93: dead stores of member addresses. */
    r.eax=A+0x2b9u;r.edx=A+0x2c5u;r.ecx=A+0x2d1u;W32(f-0x30u,r.edx);
    r.edx=A+0x2ddu;W32(f-0x34u,r.ecx);
    r.eax=A+0x31eu;r.ecx=A+0x2f1u;W32(f-0x38u,r.ecx);W32(f-0x3cu,r.eax);
    W16(A+0x29cu,0);W16(A+4u,7);
    esi=lo(esi,type);
    r.eax=ml_type_index(m,g,0x4a9d08u,esi);      /* EDX, ECX unchanged; POP ECX, then XOR */
    r.edx=0x21eu*r.eax+R32(GVA(0x4a9d64u));
    r.ecx=0;
    W32(A+0x1f2u,r.edx);
    r.edx=A+0x222u;
    W16(A+0x29au,0);W16(A+0x298u,0);W16(A+0x296u,0);W16(A+0x294u,0);W16(A+0x290u,0);W16(A+0x28eu,0);
    W16(A+0x28cu,0);W16(A+0x292u,0x2710u);W16(A+0x1a8u,0);
    esi=R32(A+0x1f2u)+2u;
    r.eax=lo(r.eax,R16(esi+0xbeu));
    /* 415d50: 413a90(A+222h, word) (computed here): EAX = A+222h, DX = the
     * word, words +6 = +8 = word, +4 = 200h, +2 = +0 = sx(word +6) SAR 1. */
    {
        const uint32_t S=A+0x222u;
        r.edx=lo(r.edx,r.eax);
        W16(S+6u,r.edx);W16(S+8u,r.edx);W16(S+4u,0x200u);
        r.ecx=fx_sar((uint32_t)s16(R16(S+6u)),1);
        W16(S+2u,r.ecx);W16(S,r.ecx);
        r.eax=S;
    }
    W16(A+0x2aau,0);W16(A+0x28eu,0);W16(A+0x252u,0);
    if(!R16(esi+0x52u)) { W32(f-0x40u,A92);r.ecx=R32(f-0x40u);W8(r.ecx+0x21u,1); }
    /* 415d84: the animation tracks of the type words +12h, +1Ch and +24h;
     * the first also clears word +6 of the shape nodes of words +12h and
     * +14h. */
    if(s16(R16(esi+0x10u))<0) { r.eax=0;W32(A+0x22cu,r.eax); }
    else {
        r.edx=lo(r.edx,R16(esi+0x10u));
        a[0]=A;a[1]=r.edx;ml_call(g,0x402374u,a,2,&r);
        W32(A+0x22cu,r.eax);
        r.eax=R32(A+0x34u);r.ecx=sx(R16(esi+0x10u));r.edx=R32(r.eax+4u);r.eax=R32(r.edx+0x2au);r.edx=R32(r.eax+6u);
        r.ecx=R32(r.edx+4u*r.ecx);W16(r.ecx+6u,0);
        r.edx=R32(A+0x34u);r.eax=sx(R16(esi+0x12u));r.ecx=R32(r.edx+4u);r.edx=R32(r.ecx+0x2au);r.ecx=R32(r.edx+6u);
        r.eax=R32(r.ecx+4u*r.eax);W16(r.eax+6u,0);
    }
    if(s16(R16(esi+0x1au))<0) { r.eax=0;W32(A+0x230u,r.eax); }
    else {
        r.edx=lo(r.edx,R16(esi+0x1au));
        a[0]=A;a[1]=r.edx;ml_call(g,0x402374u,a,2,&r);
        W32(A+0x230u,r.eax);
    }
    if(s16(R16(esi+0x22u))<0) { r.ecx=0;W32(A+0x234u,r.ecx); }
    else {
        r.eax=lo(r.eax,R16(esi+0x22u));
        a[0]=A;a[1]=r.eax;ml_call(g,0x402374u,a,2,&r);
        W32(A+0x234u,r.eax);
    }
    /* 415e25: the flat objects: one per non-negative byte from type record
     * +72h; the count word A+23Ch is reread and incremented in place. */
    W16(A+0x23cu,0);
    esi+=0x70u;
    for(;;) {
        r.eax=sx(R16(A+0x23cu));
        r.edx=sx8(R8(esi+r.eax))+1u;
        if((int32_t)r.edx<=0)break;
        W16(A+0x23cu,R16(A+0x23cu)+1u);
    }
    if(R16(A+0x23cu)) {
        r.ecx=sx(R16(A+0x23cu))<<2;
        a[0]=r.ecx;ml_call(g,0x4774f0u,a,1,&r);
        r.ecx=a[0];                                 /* POP ECX */
        W32(A+0x238u,r.eax);
        for(uint32_t k=0;s16(k)<s16(R16(A+0x23cu));++k,++esi) {   /* DI, signed words */
            r.eax=R32(GVA(0x4a968du));
            a[0]=r.eax;ml_call(g,0x471b30u,a,1,&r);
            r.ecx=a[0];                             /* POP ECX */
            W32(f-0x44u,r.eax);r.edx=R32(f-0x44u);W32(f-4u,r.edx);
            if(r.edx) {
                W16(context,0x14u);
                r.eax=sx8(R8(esi));W16(f-0x46u,r.eax);
                r.ecx=sx(R16(f-0x46u));
                r.eax=R32(GVA(0x4a9698u));
                r.edx=R32(r.eax+4u*r.ecx);
                r.ecx=R32(f-4u);
                a[0]=r.ecx;a[1]=r.edx;ml_call(g,0x402188u,a,2,&r);
                r.eax=R32(f-4u);W32(r.eax,GVA(0x49a262u));
                r.edx=R32(f-4u);W16(r.edx+4u,9);
                r.ecx=ml_count_add(m,g,1);
                r.eax=ml_count_add(m,g,0xfffffffcu);
                W16(context,8);
            }
            /* A null object faults below (PROVED edge of 415e8b). */
            r.edx=R32(f-4u);
            r.eax=sx(k);r.ecx=R32(A+0x238u);W32(r.ecx+4u*r.eax,r.edx);
            r.eax=sx(k);r.edx=R32(A+0x238u);r.ecx=R32(r.edx+4u*r.eax);
            W32(r.ecx+0x2eu,0xfffe7960u);
        }
    }
    /* 415f11: the AI state: player fly, player, or deciding. */
    r.eax=A92;
    if(!R8(r.eax+0x11u)) {
        r.edx=A+0x4du;a[0]=r.edx;a[1]=GVA(0x4993a4u);ml_call(g,0x413e50u,a,2,&r);
    } else {
        r.edx=R32(A+0x1f2u);
        if(!R16(r.edx+0x50u)) { r.eax=A+0x4du;a[0]=r.eax;a[1]=GVA(0x4993e2u);ml_call(g,0x413e50u,a,2,&r); }
        else { r.ecx=A+0x4du;a[0]=r.ecx;a[1]=GVA(0x499420u);ml_call(g,0x413e50u,a,2,&r); }
    }
    W16(A+0x29eu,0);W16(A+0x23eu,0);
    /* 415f73: the part tables of the type's name: 40d160 and 40cd88 of the
     * name with the two suffixes (492bec), then 40d354. */
    r.eax=A;r.edx=R32(GVA(0x4a9d60u));r.ecx=R32(r.eax+0x1f2u);r.eax=sx(R16(r.ecx+0x6cu));
    esi=R32(r.edx+4u*r.eax);
    r.eax=R32(GVA(0x4a0a04u));
    a[0]=esi;a[1]=r.eax;ml_call(g,0x492becu,a,2,&r);
    a[0]=r.eax;ml_call(g,0x40d160u,a,1,&r);
    r.ecx=a[0];                                     /* POP ECX */
    W32(A+0x1fau,r.eax);
    r.edx=R32(GVA(0x4a0a08u));
    a[0]=esi;a[1]=r.edx;ml_call(g,0x492becu,a,2,&r);
    a[0]=r.eax;ml_call(g,0x40cd88u,a,1,&r);
    const uint32_t part_table=r.eax;                /* EDI */
    r.ecx=a[0];                                     /* POP ECX */
    W32(A+0x1f6u,part_table);
    r.eax=R32(A+0x1fau);r.edx=A+0x206u;
    a[0]=r.edx;a[1]=A;a[2]=r.eax;a[3]=part_table;ml_call(g,0x40d354u,a,4,&r);
    r.ecx=0;
    W8(A+0x2b7u,0);W8(A+0x2b6u,0);W8(A+0x2b5u,0);W8(A+0x2b4u,0);W8(A+0x2b8u,1);
    r.eax=A+0x2b9u;a[0]=r.eax;a[1]=0;a[2]=0x4eu;ml_call(g,0x49681au,a,3,&r);
    /* 41600f: the object matrix (euler_matrix when word A+32h is 0), its
     * copy at A+2DDh, transposed, and the negated translation rotated. */
    r.eax=A+0xcu;W32(A+0x2d9u,r.eax);esi=r.eax;
    if(!R16(esi+0x26u)) {
        W32(f-0x4cu,esi);r.eax=esi+6u;r.edx=R32(f-0x4cu);
        a[0]=r.edx;a[1]=r.eax;ml_call(g,0x47ebb8u,a,2,&r);
        W16(esi+0x26u,1);
    }
    r.edx=esi+6u;r.eax=A+0x2ddu;
    for(unsigned i=0;i<5;++i)W32(r.eax+4u*i,R32(r.edx+4u*i));   /* REP MOVSD */
    r.ecx=r.edx+0x14u;r.edx=r.eax+0x14u;
    W32(r.edx+8u,R32(r.ecx+8u));W32(r.edx,R32(r.ecx));
    r.ecx=R32(r.ecx+4u);W32(r.edx+4u,r.ecx);
    esi=A+0x2ddu;
    a[0]=esi;ml_call(g,0x47df19u,a,1,&r);
    r.ecx=a[0];                                     /* POP ECX */
    r.edx=esi+0x14u;r.eax=r.edx;
    W32(r.eax+8u,0u-R32(r.eax+8u));W32(r.eax,0u-R32(r.eax));W32(r.eax+4u,0u-R32(r.eax+4u));
    r.eax=r.edx;
    r.ecx=R32(r.eax);W32(f-0x58u,r.ecx);
    r.ecx=R32(r.eax+4u);W32(f-0x54u,r.ecx);
    r.eax=R32(r.eax+8u);W32(f-0x50u,r.eax);
    a[0]=f-0x58u;a[1]=esi;a[2]=r.edx;r.edx=f-0x58u;ml_call(g,0x480304u,a,3,&r);
    W32(A+0x2bdu,0x3e8u);
    r.eax=lo(r.eax,R16(A+0x290u));W16(A+0x2d7u,r.eax);
    W16(A+0x31cu,0);
    r.edx=ml_count_add(m,g,1);
    r.ecx=R32(f-0x24u);                             /* the saved FS:0 head */
    ecx_out(ecx,r.ecx);
    return pair(A,r.edx);
}

/* Original: DBSIM.EXE 0x4215f4, with the leaves 0x422e80 (twice) and
 * 0x411dd4 computed here. Frame: the vector f-0Ch .. f-1 that rotate_point
 * reads. EBX = F, ESI = EDI = the type dword. */
uint64_t es2_flyer_ctor_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t flyer,uint32_t type,
    const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_FLYER_CTOR_FRAME,F=flyer;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[4];
    r.eax=ml_type_index(m,g,0x4a9dccu,type);
    r.edx=r.eax;
    r.ecx=R32(GVA(0x4a9df4u));                      /* after POP ECX */
    r.edx=((r.edx<<3)-r.eax)*2u;                    /* 14 x index */
    r.eax=R32(r.ecx+8u*r.edx+0x36u);
    a[0]=F;a[1]=r.eax;ml_call(g,0x403368u,a,2,&r);
    W32(F,GVA(0x49a54cu));
    r.edx=F+0x92u;
    W16(F+0x1a8u,0xffffu);
    r.eax=r.edx;W8(r.eax+6u,1);W8(r.eax+0x15u,1);
    ml_list_append(m,&r,GVA(0x4a9af8u),F);
    r.edx=0;r.eax=0xffffffffu;
    W32(F+0x1b6u,r.edx);W32(F+0x1b2u,r.edx);
    W16(F+0x1beu,r.eax);W16(F+0x1bcu,r.eax);W16(F+0x1bau,r.eax);
    r.edx=ml_count_add(m,g,1);
    W32(F,GVA(0x49a5e0u));
    a[0]=F+0x200u;a[1]=1;a[2]=1;ml_call(g,0x40d2ccu,a,3,&r);
    W16(F+4u,6);
    r.eax=F+0x243u;r.edx=r.eax+0x24u;
    r.eax=ml_type_index(m,g,0x4a9dccu,type);       /* 4216b7; EDX unchanged */
    r.ecx=r.eax;                                    /* POP ECX, MOVSX ECX,AX */
    r.eax=((r.ecx<<3)-r.ecx)<<4;                    /* 70h x index */
    r.eax+=R32(GVA(0x4a9df4u));
    r.edx=0;
    W32(F+0x1fau,r.eax);
    W16(F+0x1a8u,2);W16(F+0x235u,r.edx);W16(F+0x231u,r.edx);
    r.eax=R32(F+0x1fau);r.ecx=lo(r.ecx,R16(r.eax+4u));W16(F+0x233u,r.ecx);
    r.eax=R32(F+0x1fau);r.edx=R32(r.eax+0x32u);a[3]=r.edx;
    r.edx=F+0x200u;r.ecx=R32(F+0x1fau);r.eax=R32(r.ecx+0x2eu);
    a[0]=r.edx;a[1]=F;a[2]=r.eax;ml_call(g,0x40d354u,a,4,&r);
    r.ecx=F+0x4du;a[0]=r.ecx;a[1]=GVA(0x499cf8u);ml_call(g,0x413e50u,a,2,&r);
    r.eax=F+0x243u;a[0]=r.eax;a[1]=0;a[2]=0x4eu;ml_call(g,0x49681au,a,3,&r);
    uint32_t esi=F+0xcu;
    r.edx=F+0xcu;W32(F+0x263u,r.edx);
    if(!R16(esi+0x26u)) {
        r.eax=esi+6u;a[0]=esi;a[1]=r.eax;ml_call(g,0x47ebb8u,a,2,&r);
        W16(esi+0x26u,1);
    }
    r.edx=esi+6u;r.eax=F+0x267u;
    for(unsigned i=0;i<5;++i)W32(r.eax+4u*i,R32(r.edx+4u*i));   /* REP MOVSD */
    r.ecx=r.edx+0x14u;r.edx=r.eax+0x14u;
    W32(r.edx+8u,R32(r.ecx+8u));W32(r.edx,R32(r.ecx));
    r.ecx=R32(r.ecx+4u);W32(r.edx+4u,r.ecx);
    esi=F+0x267u;
    a[0]=esi;ml_call(g,0x47df19u,a,1,&r);
    r.eax=esi+0x14u;
    r.ecx=a[0];                                     /* POP ECX */
    W32(r.eax+8u,0u-R32(r.eax+8u));W32(r.eax,0u-R32(r.eax));W32(r.eax+4u,0u-R32(r.eax+4u));
    r.eax=esi+0x14u;
    r.edx=R32(r.eax);W32(f-0xcu,r.edx);
    r.edx=esi+0x14u;
    r.ecx=R32(r.eax+4u);W32(f-8u,r.ecx);
    r.ecx=f-0xcu;
    r.eax=R32(r.eax+8u);W32(f-4u,r.eax);
    a[0]=r.ecx;a[1]=esi;a[2]=r.edx;ml_call(g,0x480304u,a,3,&r);
    W32(F+0x247u,0x3e8u);W16(F+0x261u,0x200u);
    ml_count_add(m,g,1);
    ecx_out(ecx,r.ecx);
    return pair(F,r.edx);
}

/* The ACTOR and BASE part of the five factory cases (4053f2 .. 4054a6 and
 * the copies): the dead locals d1 (object+4Dh), d2 and d3 (object+92h, read
 * back for the byte stores), the actor list (411dd4, computed here), the
 * construction count +1, BASE with its words, +1. Returns the counter
 * pointer of the last increment (EAX). The object pointer is reread from
 * its frame slot before every access in the original; no callee receives
 * the slot's address, so the value is the same. */
static uint32_t bf_actor_base(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t obj,uint32_t d1,uint32_t d2,
    uint32_t d3) {
    es2_weapon_registers r={0,0,0};
    W32(obj,GVA(0x49a54cu));
    W32(d1,obj+0x4du);
    W16(obj+0x1a8u,0xffffu);
    W32(d2,obj+0x92u);W8(R32(d2)+6u,1);
    W32(d3,obj+0x92u);W8(R32(d3)+0x15u,1);
    ml_list_append(m,&r,GVA(0x4a9af8u),obj);
    W32(obj+0x1b6u,0);W32(obj+0x1b2u,0);
    W16(obj+0x1beu,0xffffu);W16(obj+0x1bcu,0xffffu);W16(obj+0x1bau,0xffffu);
    ml_count_add(m,g,1);
    W32(obj,GVA(0x497940u));W16(obj+4u,5);W16(obj+0x1a8u,1);
    return ml_count_add(m,g,1);
}

/* Original: DBSIM.EXE 0x405314, with 0x411dd4 computed here. Frame: record
 * f-34h (context word f-24h), the table pointer f-38h, the case slots f-4,
 * f-8, f-0Ch, f-10h, f-14h with their copies and dead locals f-3Ch ..
 * f-94h, the part counter f-98h, the shape word f-9Ah, the track cursor
 * f-0A0h and the rate cursor f-0A4h. EBX = the type record T, ESI the
 * second shape table, then the track index and the part configuration;
 * EDI = the object (the caller's EDI in case 0). */
uint64_t es2_building_factory_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t record,
    uint32_t edi,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_BUILDING_FACTORY_FRAME,context=f-0x24u,R=record;
    es2_weapon_registers r={GVA(0x497556u),entry[1],entry[2]};uint32_t a[5];
    ml_call(g,0x49630cu,NULL,0,&r);
    r.edx=R;
    const uint32_t table2=R32(GVA(0x4a957cu));     /* ESI */
    r.eax=sx(R16(r.edx));
    const uint32_t t=r.eax;
    r.edx=R32(GVA(0x4a9574u));W32(f-0x38u,r.edx);
    const uint32_t T=60u*t+R32(GVA(0x4a959cu));    /* EBX */
    uint32_t obj=edi,kase=0;
    if(t<=0x3du) { kase=R8(GVA(0x405366u)+t);r.eax=kase; }   /* EAX = the case byte */
    switch(kase) {
    case 5:                                         /* 4053bc: RADAR_BASE */
        r.eax=R32(GVA(0x4a95a0u));a[0]=r.eax;ml_call(g,0x471b30u,a,1,&r);
        r.ecx=a[0];
        obj=r.eax;W32(f-4u,obj);
        if(obj) {
            W16(context,0x14u);
            r.eax=sx(R16(T+2u));r.edx=R32(f-0x38u);
            const uint32_t shape=R32(r.edx+4u*r.eax);
            r.ecx=R32(f-4u);a[0]=r.ecx;a[1]=shape;ml_call(g,0x40332cu,a,2,&r);
            bf_actor_base(m,g,obj,f-0x3cu,f-0x40u,f-0x44u);
            W32(obj,GVA(0x4979d4u));
            ml_count_add(m,g,1);ml_count_add(m,g,0xfffffff8u);
            W16(context,8);
        }
        /* 4054cf: a null object faults here (PROVED edge of 4053cf). */
        obj=R32(f-4u);
        W8(obj+0x96u,1);W8(obj+0xa5u,1);
        break;
    case 3:                                         /* 4054ea: GUN_BASE */
        r.edx=R32(GVA(0x4a95a0u));a[0]=r.edx;ml_call(g,0x471b30u,a,1,&r);
        r.ecx=a[0];
        obj=r.eax;W32(f-8u,obj);
        if(obj) {
            W16(context,0x2cu);
            r.eax=sx(R16(T+2u));r.edx=R32(f-0x38u);r.ecx=R32(r.edx+4u*r.eax);W32(f-0x48u,r.ecx);
            r.eax=R32(f-0x48u);r.edx=R32(f-8u);
            a[0]=r.edx;a[1]=r.eax;ml_call(g,0x40332cu,a,2,&r);
            bf_actor_base(m,g,obj,f-0x4cu,f-0x50u,f-0x54u);
            W32(obj,GVA(0x4978acu));
            ml_count_add(m,g,1);ml_count_add(m,g,0xfffffff8u);
            W16(context,0x20u);
        }
        obj=R32(f-8u);                              /* a null object faults in the tail */
        break;
    case 2:                                         /* 40560d: BASE */
        r.eax=R32(GVA(0x4a95a0u));a[0]=r.eax;ml_call(g,0x471b30u,a,1,&r);
        r.ecx=a[0];
        W32(f-0x58u,r.eax);r.edx=R32(f-0x58u);W32(f-0xcu,r.edx);obj=r.edx;
        if(obj) {
            W16(context,0x44u);
            r.eax=sx(R16(T+2u));r.ecx=R32(table2+4u*r.eax);W32(f-0x5cu,r.ecx);
            r.eax=R32(f-0x5cu);r.edx=R32(f-0xcu);
            a[0]=r.edx;a[1]=r.eax;ml_call(g,0x403368u,a,2,&r);
            bf_actor_base(m,g,obj,f-0x60u,f-0x64u,f-0x68u);
            ml_count_add(m,g,0xfffffff9u);
            W16(context,0x38u);
        }
        obj=R32(f-0xcu);
        W32(f-0x6cu,obj+0x92u);W8(R32(f-0x6cu)+0x13u,1);   /* a null object faults here */
        break;
    case 1:                                         /* 40572d: VEHICLE */
        r.ecx=R32(GVA(0x4a95a0u));a[0]=r.ecx;ml_call(g,0x471b30u,a,1,&r);
        r.ecx=a[0];
        W32(f-0x70u,r.eax);r.eax=R32(f-0x70u);W32(f-0x10u,r.eax);obj=r.eax;
        if(obj) {
            W16(context,0x5cu);
            r.edx=sx(R16(T+2u));r.ecx=R32(table2+4u*r.edx);W32(f-0x74u,r.ecx);
            r.eax=R32(f-0x74u);r.edx=R32(f-0x10u);
            a[0]=r.edx;a[1]=r.eax;ml_call(g,0x403368u,a,2,&r);
            bf_actor_base(m,g,obj,f-0x78u,f-0x7cu,f-0x80u);
            W32(obj,GVA(0x4978acu));
            ml_count_add(m,g,1);
            W32(obj,GVA(0x497818u));W16(obj+4u,0xau);W16(obj+0x1a8u,3);
            ml_count_add(m,g,1);ml_count_add(m,g,0xfffffff7u);
            W16(context,0x50u);
        }
        obj=R32(f-0x10u);
        break;
    case 4:                                         /* 405877: LC_BASE */
        r.eax=R32(GVA(0x4a95a0u));a[0]=r.eax;ml_call(g,0x471b30u,a,1,&r);
        r.ecx=a[0];
        W32(f-0x84u,r.eax);r.edx=R32(f-0x84u);W32(f-0x14u,r.edx);obj=r.edx;
        if(obj) {
            W16(context,0x74u);
            r.eax=sx(R16(T+2u));r.ecx=R32(table2+4u*r.eax);W32(f-0x88u,r.ecx);
            r.eax=R32(f-0x88u);r.edx=R32(f-0x14u);
            a[0]=r.edx;a[1]=r.eax;ml_call(g,0x403368u,a,2,&r);
            r.eax=bf_actor_base(m,g,obj,f-0x8cu,f-0x90u,f-0x94u);
            r.edx=obj;W32(obj,GVA(0x497784u));
            r.ecx=obj+0x209u;
            a[0]=r.ecx;a[1]=0x1fu;a[2]=3;a[3]=1;a[4]=GVA(0x4069c5u);ml_call(g,0x49667cu,a,5,&r);
            ml_count_add(m,g,1);ml_count_add(m,g,0xfffffff8u);
            W16(context,0x68u);
        }
        obj=R32(f-0x14u);
        break;
    default:                                        /* case 0: EDI stays the caller's */
        break;
    }
    /* 4059ca: the two animation tracks of the type (count word +6, rates
     * from +20h), the second one sought to frame 6000. */
    uint32_t esi=0;
    r.eax=T+0x20u;W32(f-0xa4u,r.eax);
    r.edx=obj+0x1f9u;W32(f-0xa0u,r.edx);
    do {
        r.ecx=sx(R16(T+6u));
        if((int32_t)esi<(int32_t)r.ecx) {
            a[0]=obj;a[1]=esi;ml_call(g,0x402374u,a,2,&r);
            r.edx=R32(f-0xa0u);W32(r.edx,r.eax);
            r.eax=R32(f-0xa4u);r.ecx=lo(r.ecx,R16(r.eax));
            r.eax=R32(f-0xa0u);r.edx=R32(r.eax);
            a[0]=r.edx;a[1]=r.ecx;ml_call(g,0x4796d8u,a,2,&r);
            if(esi==1u) {
                r.ecx=R32(f-0xa0u);r.eax=R32(r.ecx);
                a[0]=r.eax;a[1]=esi;a[2]=0x1770u;ml_call(g,0x479344u,a,3,&r);
            }
        }
        ++esi;
        W32(f-0xa4u,R32(f-0xa4u)+2u);W32(f-0xa0u,R32(f-0xa0u)+4u);
    } while((int32_t)esi<2);
    if(R16(T+0x1eu)) { r.eax=obj+0x92u;W8(r.eax+0x25u,1); }
    /* 405a57: the part words (new[] of word +12h words, all 1) and the part
     * records (49667c of word +12h records of 0Bh bytes, constructor
     * 4069e5). */
    r.edx=sx(R16(T+0x12u))*2u;
    a[0]=r.edx;ml_call(g,0x4774f0u,a,1,&r);
    r.ecx=a[0];                                     /* POP ECX */
    W32(obj+0x201u,r.eax);
    r.ecx=sx(R16(T+0x12u));
    a[0]=0;a[1]=0xbu;a[2]=r.ecx;a[3]=1;a[4]=GVA(0x4069e5u);ml_call(g,0x49667cu,a,5,&r);
    W32(obj+0x205u,r.eax);
    for(r.eax=0;;++r.eax) {
        r.ecx=sx(R16(T+0x12u));
        if((int32_t)r.eax>=(int32_t)r.ecx)break;
        r.edx=R32(obj+0x201u);W16(r.edx+2u*r.eax,1);
    }
    /* 405aa1: per part record (stride 0Bh) and part configuration (stride
     * 1Eh from [T+14h]): word +5 = 0, and the word +0 from the record's
     * word +32h: negative 0; 0 the configuration word (and frame word 1 of
     * the shape instance for a non-negative configuration word +2); else
     * (100 - w) x configuration word / 100. */
    r.ecx=R32(obj+0x205u);
    esi=R32(T+0x14u);
    r.eax=0;W32(f-0x98u,r.eax);
    for(;;) {
        r.eax=sx(R16(T+0x12u));
        if(!((int32_t)r.eax>(int32_t)R32(f-0x98u)))break;
        W16(r.ecx+5u,0);
        r.edx=R;
        if(s16(R16(r.edx+0x32u))<0) W16(r.ecx,0);
        else {
            r.eax=R;
            if(!R16(r.eax+0x32u)) {
                r.edx=lo(r.edx,R16(esi));W16(r.ecx,r.edx);
                r.eax=lo(r.eax,R16(esi+2u));
                if(s16(r.eax)>-1) {
                    W16(f-0x9au,r.eax);r.edx=R16(f-0x9au);
                    r.eax=R32(R32(obj+0x34u)+8u);W16(r.eax+2u*r.edx,1);
                }
            } else {
                r.edx=R;
                r.edx=sx(R16(r.edx+0x32u));
                r.eax=100u-r.edx;
                r.edx=sx(R16(esi));
                /* IMUL EDX, then CDQ: only the product's low dword counts. */
                const int32_t product=(int32_t)fx_s32(r.eax*r.edx);
                r.eax=(uint32_t)(product/100);r.edx=(uint32_t)(product%100);
                W16(r.ecx,r.eax);
            }
        }
        W32(f-0x98u,R32(f-0x98u)+1u);
        r.ecx+=0xbu;esi+=0x1eu;
    }
    r.ecx=R;
    if(!R16(r.ecx+0x32u)) {
        r.edx=obj+0x92u;r.eax=r.edx;W8(obj+0x99u,1);W8(obj+0x96u,0);
        r.eax=lo(r.eax,R16(T+4u));
        if(s16(r.eax)>-1) {
            r.edx=sx(r.eax);r.ecx=R32(GVA(0x4a9584u));r.eax=R32(r.ecx+4u*r.edx);
            r.edx=R32(obj+0x34u);W32(r.edx+4u,r.eax);
        }
    }
    r.eax=lo(r.eax,R16(T+0x26u));W16(obj+0x1f7u,r.eax);
    W32(obj+0x1f2u,T);
    r.edx=R32(f-0x34u);                             /* the saved FS:0 head */
    ecx_out(ecx,r.ecx);
    return pair(obj,r.edx);
}

/* Original: DBSIM.EXE 0x423b34. The locals (the roster cursor [EBP-4], the
 * member counters [EBP-6]/[EBP-0Ah]/[EBP-0Eh] and the slot words) reach no
 * callee and are kept in variables here. */
uint64_t es2_group_ctor_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t group,uint32_t record,
    uint32_t trigger,uint32_t orders,uint32_t heading,uint32_t point,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t G=group,R=record;
    es2_weapon_registers r={R,entry[1],entry[2]};uint32_t a[4];
    r.edx=lo(r.edx,R16(R+0x28u));W16(G,r.edx);
    r.ecx=R;r.eax=(r.eax&0xffffff00u)|R8(R+0x6eu);W8(G+0x12u,r.eax);
    r.edx=lo(r.edx,heading);W16(G+0xau,r.edx);
    r.eax=lo(r.eax,R16(R+0x2au));W16(G+2u,r.eax);
    uint32_t roster=R+0x32u;
    r.edx=0;W16(G+0x10u,0);
    r.ecx=trigger;W32(G+0x14u,r.ecx);
    r.ecx=G+0x70u;
    r.eax=point;W32(G+0x18u,r.eax);
    W32(G+0x6cu,r.edx);W8(G+0x13u,0);
    a[0]=r.ecx;a[1]=0;a[2]=0xau;ml_call(g,0x49681au,a,3,&r);
    /* 423b9a: the words R+72h.. to G+1Ch.. and R+86h.. to G+30h..,
     * interleaved; then the ten order pointers to G+44h. */
    r.eax=0;r.ecx=G+0x1cu;r.edx=R+0x72u;
    do {
        const uint32_t w=R16(r.edx);++r.eax;W16(r.ecx,w);
        const uint32_t w2=R16(r.edx+0x14u);r.edx+=2u;W16(r.ecx+0x14u,w2);r.ecx+=2u;
    } while((int32_t)r.eax<10);
    r.eax=0;r.ecx=G+0x44u;r.edx=orders;
    do { W32(r.ecx,R32(r.edx));++r.eax;r.ecx+=4u;r.edx+=4u; } while((int32_t)r.eax<10);
    r.eax=R32(G+0x44u);
    if(r.eax) { r.edx=R32(r.eax+8u);r.eax=G+4u;W32(G+6u,r.edx);W16(G+4u,0); }
    /* 423bec: the member count word G+10h: non-negative roster slots. */
    r.eax=0;
    do {
        r.edx=roster;
        if(s16(R16(r.edx))>=0)W16(G+0x10u,R16(G+0x10u)+1u);
        ++r.eax;roster+=2u;
    } while((int32_t)r.eax<0x14);
    r.ecx=sx(R16(G+0x10u))<<2;
    a[0]=r.ecx;ml_call(g,0x4774f0u,a,1,&r);
    r.ecx=a[0];                                     /* POP ECX */
    W32(G+0xcu,r.eax);
    r.eax=sx(R16(G))-1u;
    /* 423c19: class 0 (JB), 1 (JE), 2 (DEC, JE); otherwise no members. */
    uint32_t pool=0,join=0;
    if(r.eax==0xffffffffu) { pool=0x4a9b7au;join=0x417aa8u; }
    else if(!r.eax) { pool=0x4a9db9u;join=0x421ee8u; }
    else if(!--r.eax) { pool=0x4a95a0u;join=0x405c3cu; }
    if(pool) {
        uint32_t slot=R+0x32u;uint16_t count=0;
        for(uint32_t k=0;s16(k)<0x14;++k,slot+=2u) {     /* EDI = k; CMP DI */
            if(s16(R16(slot))<0)continue;
            r.eax=lo(r.eax,R16(slot));
            r.edx=lo(r.edx,r.eax);                  /* the slot word through [EBP-8] */
            r.ecx=R32(GVA(pool));
            a[0]=r.ecx;a[1]=r.edx;ml_call(g,0x471cb0u,a,2,&r);
            r.edx=sx(count);r.ecx=R32(G+0xcu);
            W32(r.ecx+4u*r.edx,r.eax);
            ++count;
            if(join==0x405c3cu) {
                r.edx=R;r.ecx=sx(R16(r.edx));
                a[0]=r.eax;a[1]=G;a[2]=k;a[3]=r.ecx;ml_call(g,join,a,4,&r);
            } else { a[0]=r.eax;a[1]=G;a[2]=k;ml_call(g,join,a,3,&r); }
        }
    }
    ecx_out(ecx,r.ecx);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x405db0. */
uint64_t es2_base_marks_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t building,
    const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={0,building,entry[2]};uint32_t a[5];
    r.eax=R32(building+0x45u);
    r.ecx=sx(R16(r.eax+2u));
    r.eax=28u*r.ecx+R32(GVA(0x4a95acu));
    r.ecx=R32(r.eax+8u);
    if(r.ecx!=0xffffffffu) {
        const uint32_t t14=R32(r.eax+0x14u);
        r.edx=building+0x26u;
        const uint32_t t18=R32(r.eax+0x18u);
        r.eax=R32(GVA(0x4a0be0u));
        a[0]=r.eax;a[1]=r.edx;a[2]=r.ecx;a[3]=t18;a[4]=t14;ml_call(g,0x47136cu,a,5,&r);
    }
    ecx_out(ecx,r.ecx);
    return pair(r.eax,r.edx);
}

/* (L6): the actor load 4253d8, script pass 2 (docs/reverse-
 * engineering/mission_load.md, section "L6 actor load"). The frame lies in
 * g->scratch at the original's offsets below f (guest callees receive the
 * addresses of its buffers): the runtime record f-34h (context word f-24h), the
 * stream object f-0Ch, the count words f-36h (the current list), f-38h (the
 * authored mechs M), f-42h (timers) and f-44h (route points), the MEC ordinal
 * f-46h, the record buffers (skip f-8C4h, timer f-8DCh, mech f-964h, flyer
 * f-9CCh, building f-0A0Ch, order f-0A28h, group f-0AC4h, objective f-0B24h),
 * the group's order pointers f-0AECh and the staged points and angles. Every
 * local the original writes is written here too, in its order. EBX is the loop
 * counter (compared as BX), ESI the object or record cursor, EDI the
 * selection-flag pointer or the trigger scope. */
/* The stream's virtual read +18h (stream f-0Ch, size, buffer); the caller
 * sets the original's registers, `vtable` is the value just read from the
 * stream object. */
static void al_vread(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t f,uint32_t size,uint32_t buffer,
    uint32_t vtable,es2_weapon_registers *r) {
    const uint32_t a[3]={f-0xcu,size,buffer};
    ml_virtual(g,R32(vtable+0x18u),0x18u,a,3,r);
}
/* The direct read 47af98 (stream f-0Ch, size, buffer). */
static void al_read(const es2_weapon_graph *g,uint32_t f,uint32_t size,uint32_t buffer,es2_weapon_registers *r) {
    const uint32_t a[3]={f-0xcu,size,buffer};
    ml_call(g,0x47af98u,a,3,r);
}
/* 58 x sx(word) + [4A9E28h]: a trigger record (the original computes it
 * with SHL, SUB, LEA and ADD; the result is the same modulo 2^32). */
static uint32_t al_trigger(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t index) {
    return 58u*index+R32(GVA(0x4a9e28u));
}
#define AL_MEC() (s16(ebx)>=s16(R16(f-0x38u)))

/* Original: DBSIM.EXE 0x4253d8. */
uint64_t es2_actor_load_registers(const es2_guest_memory *m,const es2_weapon_graph *g,const uint32_t entry[3],
    uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_ACTOR_LOAD_FRAME,context=f-0x24u,stream=f-0xcu;
    es2_weapon_registers r={GVA(0x49a8b5u),entry[1],entry[2]};uint32_t a[6],ebx,esi=0,edi;
    ml_call(g,0x49630cu,NULL,0,&r);
    /* 4253ee: the function-local statics: default position (0, 0, -32768)
     * and angle words (8000h, 8000h, 0), each with its guard byte. */
    if(!R8(GVA(0x49a87du))) {
        r.edx=0;W32(GVA(0x49a871u),r.edx);
        r.ecx=0;W32(GVA(0x49a875u),r.ecx);
        W32(GVA(0x49a879u),0xffff8000u);
        W8(GVA(0x49a87du),R8(GVA(0x49a87du))+1u);
    }
    if(!R8(GVA(0x49a884u))) {
        W16(GVA(0x49a87eu),0x8000u);W16(GVA(0x49a880u),0x8000u);W16(GVA(0x49a882u),0);
        W8(GVA(0x49a884u),R8(GVA(0x49a884u))+1u);
    }
    /* 425441: context 8; the name 492b90(45ee00("data\script"), [4A09F0h])
     * (the outer call's second slot is pushed first), the stream f-0Ch on it
     * (47ad40), the status word -1, the 20-byte header. */
    W16(context,8);
    r.eax=R32(GVA(0x4a09f0u));
    const uint32_t name=r.eax;
    a[0]=GVA(0x49aa3eu);ml_call(g,0x45ee00u,a,1,&r);
    r.ecx=a[0];                                     /* POP ECX */
    a[0]=r.eax;a[1]=name;ml_call(g,0x492b90u,a,2,&r);
    r.edx=stream;a[0]=r.edx;a[1]=r.eax;ml_call(g,0x47ad40u,a,2,&r);
    r.ecx=f-0x8c4u;
    W16(GVA(0x4a9e4cu),0xffffu);
    r.eax=stream;al_read(g,f,0x14u,r.ecx,&r);
    /* 42548c: pass 1's lists are skipped: coordinates (12 bytes each),
     * angles (2), routes (a count word, then 2 bytes per point), regions (6),
     * triggers (74 each, one read per record) and timers (24 each). */
    r.edx=f-0x36u;r.ecx=stream;r.eax=R32(stream);al_vread(m,g,f,2,r.edx,r.eax,&r);
    if(R16(f-0x36u)) {
        r.edx=f-0x8c4u;const uint32_t buffer=r.edx;
        r.ecx=sx(R16(f-0x36u));r.eax=12u*r.ecx;r.edx=stream;al_read(g,f,r.eax,buffer,&r);
    }
    r.ecx=f-0x36u;r.eax=stream;r.edx=R32(stream);al_vread(m,g,f,2,r.ecx,r.edx,&r);
    if(R16(f-0x36u)) {
        r.ecx=f-0x8c4u;r.eax=2u*sx(R16(f-0x36u));r.edx=stream;al_read(g,f,r.eax,r.ecx,&r);
    }
    r.ecx=f-0x36u;r.eax=stream;r.edx=R32(stream);al_vread(m,g,f,2,r.ecx,r.edx,&r);
    for(ebx=0;s16(ebx)<s16(R16(f-0x36u));++ebx) {
        r.eax=f-0x44u;r.edx=stream;r.ecx=R32(stream);al_vread(m,g,f,2,r.eax,r.ecx,&r);
        r.eax=f-0x8c4u;r.edx=2u*sx(R16(f-0x44u));r.ecx=stream;al_read(g,f,r.edx,r.eax,&r);
    }
    r.eax=f-0x36u;r.edx=stream;r.ecx=R32(stream);al_vread(m,g,f,2,r.eax,r.ecx,&r);
    if(R16(f-0x36u)) {
        r.eax=f-0x8c4u;const uint32_t buffer=r.eax;
        r.edx=sx(R16(f-0x36u));r.ecx=6u*r.edx;r.eax=stream;al_read(g,f,r.ecx,buffer,&r);
    }
    r.edx=f-0x36u;r.ecx=stream;r.eax=R32(stream);al_vread(m,g,f,2,r.edx,r.eax,&r);
    if(R16(f-0x36u))
        for(ebx=0;s16(ebx)<s16(R16(f-0x36u));++ebx) { r.eax=f-0x8c4u;r.edx=stream;al_read(g,f,0x4au,r.eax,&r); }
    r.ecx=f-0x42u;r.eax=stream;r.edx=R32(stream);al_vread(m,g,f,2,r.ecx,r.edx,&r);
    if(R16(f-0x42u))
        for(ebx=0;s16(ebx)<s16(R16(f-0x42u));++ebx) { r.eax=f-0x8dcu;r.edx=stream;al_read(g,f,0x18u,r.eax,&r); }
    /* 4255fe: the MEC ordinal; the weapon shape cache (40f998); M. */
    W16(f-0x46u,0);
    ml_call(g,0x40f998u,NULL,0,&r);
    r.ecx=f-0x38u;r.eax=stream;r.edx=R32(stream);al_vread(m,g,f,2,r.ecx,r.edx,&r);
    /* 42561c: mechs i = 0 .. M+N-1 (EBX), the selection flag at 4AA72Ah+i
     * (EDI); i < M reads the 134-byte record always, i >= M is MEC record
     * [4AAA60h] + 126 x the ordinal f-46h. */
    ebx=0;edi=GVA(0x4aa72au);
    for(;;) {
        r.ecx=sx(R16(f-0x38u));r.eax=sx(R16(GVA(0x4aaa5eu)));r.ecx+=r.eax;r.edx=sx(ebx);
        if(!((int32_t)r.ecx>(int32_t)r.edx))break;
        r.eax=sx(R16(f-0x46u));r.edx=126u*r.eax+R32(GVA(0x4aaa60u));W32(f-0x4cu,r.edx);
        if(!AL_MEC()) { r.ecx=f-0x964u;r.eax=stream;al_read(g,f,0x86u,r.ecx,&r); }
        if(R8(edi)) {
            if(AL_MEC()) { r.edx=R32(f-0x4cu);r.ecx=lo(r.ecx,R16(r.edx+4u));W16(f-0x93cu,r.ecx); }
            r.eax=R32(GVA(0x4a9b7au));
            a[0]=r.eax;ml_call(g,0x471b30u,a,1,&r);
            r.ecx=a[0];                             /* POP ECX */
            W32(f-0x50u,r.eax);r.edx=R32(f-0x50u);W32(f-0x10u,r.edx);
            if(r.edx) {
                /* 425692: mech_ctor(A, type) inside context 20h; the
                 * construction count -7 afterwards. */
                W16(context,0x20u);
                r.eax=lo(r.eax,R16(f-0x93cu));r.ecx=R32(f-0x10u);
                a[0]=r.ecx;a[1]=r.eax;ml_call(g,0x415bb0u,a,2,&r);
                r.eax=R32(GVA(0x6cbee4u));W32(r.eax,R32(r.eax)-7u);
                W16(context,0x14u);
            }
            /* A null object faults below (PROVED edge of 425690). */
            esi=R32(f-0x10u);
            if(AL_MEC()) {
                r.eax=lo(r.eax,R16(f-0x46u));
                if((uint16_t)r.eax==R16(GVA(0x4aaa64u))) {
                    /* 4256d5: the player: 4A9B84h, byte A+0A3h. */
                    W32(GVA(0x4a9b84u),esi);
                    r.edx=esi+0x92u;W32(f-0x54u,r.edx);r.ecx=R32(f-0x54u);W8(r.ecx+0x11u,1);
                }
            }
            if(AL_MEC()) {
                /* 4256f1: the defaults. */
                r.eax=R32(GVA(0x49a879u));r.edx=R32(GVA(0x49a871u));W32(f-0x968u,r.eax);W32(f-0x970u,r.edx);
                r.ecx=R32(GVA(0x49a875u));r.eax=R32(GVA(0x49a87eu));W32(f-0x96cu,r.ecx);W32(f-0x5cu,r.eax);
                r.eax=lo(r.eax,R16(GVA(0x49a882u)));W16(f-0x58u,r.eax);
            } else {
                /* 42572d: the coordinate of record word +3Eh with the
                 * ground height (-32768 becomes -32767) plus the type word
                 * +16h and 5000 for a type with word +50h, or the defaults;
                 * the angle words (0, 0, angle of word +40h) or the
                 * defaults. */
                if(s16(R16(f-0x926u))<0) {
                    r.ecx=R32(GVA(0x49a879u));W32(f-0x968u,r.ecx);
                    r.eax=R32(GVA(0x49a871u));W32(f-0x970u,r.eax);
                    r.edx=R32(GVA(0x49a875u));W32(f-0x96cu,r.edx);
                } else {
                    r.edx=sx(R16(f-0x926u));r.ecx=12u*r.edx+R32(GVA(0x4a9e0cu));W32(f-0x60u,r.ecx);
                    r.eax=R32(f-0x60u);r.edx=R32(r.eax+8u);W32(f-0x968u,r.edx);
                    r.ecx=R32(f-0x60u);r.eax=R32(r.ecx);W32(f-0x970u,r.eax);
                    r.eax=f-0x970u;
                    r.edx=R32(f-0x60u);r.ecx=R32(r.edx+4u);r.edx=R32(GVA(0x4a0be0u));W32(f-0x96cu,r.ecx);
                    a[0]=r.edx;a[1]=r.eax;ml_call(g,0x46e180u,a,2,&r);
                    W32(f-0x968u,r.eax);
                    if(R32(f-0x968u)==0xffff8000u)W32(f-0x968u,R32(f-0x968u)+1u);
                    r.ecx=R32(esi+0x1f2u);r.eax=sx(R16(r.ecx+0x16u));W32(f-0x968u,R32(f-0x968u)+r.eax);
                    r.edx=R32(esi+0x1f2u);
                    if(R16(r.edx+0x50u))W32(f-0x968u,R32(f-0x968u)+5000u);
                }
                if(s16(R16(f-0x924u))<0) {
                    r.edx=R32(GVA(0x49a87eu));W32(f-0x5cu,r.edx);
                    r.edx=lo(r.edx,R16(GVA(0x49a882u)));W16(f-0x58u,r.edx);
                } else {
                    r.ecx=0;r.edx=R32(GVA(0x4a9e10u));W16(f-0x5au,r.ecx);W16(f-0x5cu,r.ecx);
                    r.eax=sx(R16(f-0x924u));r.eax=lo(r.eax,R16(r.edx+2u*r.eax));W16(f-0x58u,r.eax);
                }
            }
            /* 42582f: the item and secondary words (MEC +8/+20h, record
             * +2Ah/+6Ah); position A+26h, angles A+0Ch, word A+32h = 0;
             * weapon_fit(A, items, secondary). */
            r.ecx=AL_MEC()?R32(f-0x4cu)+8u:f-0x93au;W32(f-0x64u,r.ecx);
            r.eax=AL_MEC()?R32(f-0x4cu)+0x20u:f-0x8fau;W32(f-0x68u,r.eax);
            r.eax=esi+0x26u;r.edx=R32(f-0x968u);W32(r.eax+8u,r.edx);
            r.ecx=R32(f-0x970u);W32(r.eax,r.ecx);r.edx=R32(f-0x96cu);W32(r.eax+4u,r.edx);
            r.eax=esi+0xcu;W32(f-0x6cu,r.eax);r.ecx=R32(f-0x6cu);r.eax=R32(f-0x5cu);W32(r.ecx,r.eax);
            r.eax=lo(r.eax,R16(f-0x58u));W16(r.ecx+4u,r.eax);
            r.edx=R32(f-0x6cu);W16(r.edx+0x26u,0);
            r.ecx=R32(f-0x68u);r.eax=R32(f-0x64u);
            a[0]=esi;a[1]=r.eax;a[2]=r.ecx;ml_call(g,0x4175dcu,a,3,&r);
            if(AL_MEC()) {
                /* 4258b0: condition_init(A+206h, MEC +3Ch, +56h, +6Ah), word
                 * A+29Ch = MEC word 0, the ordinal +1. */
                r.edx=R32(f-0x4cu)+0x6au;W32(f-0x70u,r.edx);
                r.ecx=R32(f-0x4cu)+0x56u;W32(f-0x74u,r.ecx);
                r.eax=R32(f-0x4cu)+0x3cu;W32(f-0x78u,r.eax);
                r.edx=R32(f-0x70u);a[3]=r.edx;r.ecx=R32(f-0x74u);a[2]=r.ecx;r.eax=R32(f-0x78u);a[1]=r.eax;
                r.edx=esi+0x206u;a[0]=r.edx;ml_call(g,0x415068u,a,4,&r);
                r.ecx=R32(f-0x4cu);r.eax=lo(r.eax,R16(r.ecx));W16(f-0x7au,r.eax);
                r.edx=lo(r.edx,R16(f-0x7au));W16(esi+0x29cu,r.edx);
                W16(f-0x46u,R16(f-0x46u)+1u);
            } else {
                /* 425904: byte A+97h when word 0 is nonzero, the trigger
                 * pointers of words +80h/+82h, action_copy(A, +42h, +56h),
                 * initial_damage(A, word +84h), word A+252h = word +2 when
                 * it is above -1. */
                if(R16(f-0x964u)) { r.ecx=esi+0x92u;W32(f-0x80u,r.ecx);r.eax=R32(f-0x80u);W8(r.eax+5u,1); }
                if(s16(R16(f-0x8e4u))>=0) { r.ecx=sx(R16(f-0x8e4u));r.edx=al_trigger(m,g,r.ecx); } else r.edx=0;
                W32(f-0x84u,r.edx);r.eax=R32(f-0x84u);W32(esi+0x1b2u,r.eax);
                if(s16(R16(f-0x8e2u))>=0) { r.eax=sx(R16(f-0x8e2u));r.ecx=al_trigger(m,g,r.eax); } else r.ecx=0;
                W32(f-0x88u,r.ecx);r.eax=f-0x90eu;r.edx=R32(f-0x88u);W32(esi+0x1b6u,r.edx);r.edx=f-0x922u;
                a[0]=esi;a[1]=r.edx;a[2]=r.eax;ml_call(g,0x411b90u,a,3,&r);
                r.ecx=lo(r.ecx,R16(f-0x8e0u));
                a[0]=esi;a[1]=r.ecx;ml_call(g,0x4178e8u,a,2,&r);
                if(s16(R16(f-0x962u))>-1) {
                    r.eax=lo(r.eax,R16(f-0x962u));W16(f-0x8au,r.eax);
                    r.edx=lo(r.edx,R16(f-0x8au));W16(esi+0x252u,r.edx);
                }
            }
        }
        ++ebx;++edi;
    }
    /* 4259f8: the weapon shape cache freed (40fa88). */
    ml_call(g,0x40fa88u,NULL,0,&r);
    /* 4259fd: flyers (92 bytes; selection 4AA856h): pool_alloc,
     * flyer_ctor(F, type +2Ch) inside context 38h, the count -7, the
     * coordinate +28h (no ground height) and angle +2Ah or the defaults, the
     * trigger pointers +56h/+58h, action_copy(F, +2Eh, +42h). */
    r.ecx=f-0x36u;r.eax=stream;r.edx=R32(stream);al_vread(m,g,f,2,r.ecx,r.edx,&r);
    edi=GVA(0x4aa856u);
    for(ebx=0;s16(ebx)<s16(R16(f-0x36u));++ebx,++edi) {
        r.eax=f-0x9ccu;r.edx=stream;al_read(g,f,0x5cu,r.eax,&r);
        if(!R8(edi))continue;
        r.ecx=R32(GVA(0x4a9db9u));
        a[0]=r.ecx;ml_call(g,0x471b30u,a,1,&r);
        r.ecx=a[0];                                 /* POP ECX */
        W32(f-0x90u,r.eax);r.eax=R32(f-0x90u);W32(f-0x14u,r.eax);
        if(r.eax) {
            W16(context,0x38u);
            r.edx=lo(r.edx,R16(f-0x9a0u));r.ecx=R32(f-0x14u);
            a[0]=r.ecx;a[1]=r.edx;ml_call(g,0x4215f4u,a,2,&r);
            r.eax=R32(GVA(0x6cbee4u));W32(r.eax,R32(r.eax)-7u);
            W16(context,0x2cu);
        }
        /* A null flyer faults below (PROVED edge of 425a5d). */
        r.eax=R32(f-0x14u);
        if(s16(R16(f-0x9a4u))<0) {
            r.edx=R32(GVA(0x49a879u));r.ecx=R32(GVA(0x49a871u));W32(f-0x9d0u,r.edx);
            r.edx=R32(GVA(0x49a875u));W32(f-0x9d8u,r.ecx);W32(f-0x9d4u,r.edx);
        } else {
            r.edx=sx(R16(f-0x9a4u));r.ecx=12u*r.edx+R32(GVA(0x4a9e0cu));W32(f-0x9cu,r.ecx);
            r.edx=R32(f-0x9cu);r.ecx=R32(r.edx+8u);W32(f-0x9d0u,r.ecx);
            r.edx=R32(f-0x9cu);r.ecx=R32(r.edx);W32(f-0x9d8u,r.ecx);
            r.edx=R32(f-0x9cu);r.ecx=R32(r.edx+4u);W32(f-0x9d4u,r.ecx);
        }
        if(s16(R16(f-0x9a2u))<0) {
            r.edx=R32(GVA(0x49a87eu));W32(f-0x98u,r.edx);
            r.edx=lo(r.edx,R16(GVA(0x49a882u)));W16(f-0x94u,r.edx);
        } else {
            r.ecx=0;W16(f-0x96u,r.ecx);W16(f-0x98u,r.ecx);
            r.edx=sx(R16(f-0x9a2u));r.ecx=R32(GVA(0x4a9e10u));r.edx=lo(r.edx,R16(r.ecx+2u*r.edx));W16(f-0x94u,r.edx);
        }
        r.edx=r.eax+0x26u;r.ecx=R32(f-0x9d0u);W32(r.edx+8u,r.ecx);
        r.ecx=R32(f-0x9d8u);W32(r.edx,r.ecx);r.ecx=R32(f-0x9d4u);W32(r.edx+4u,r.ecx);
        r.edx=r.eax+0xcu;W32(f-0xa0u,r.edx);r.edx=R32(f-0xa0u);r.ecx=R32(f-0x98u);W32(r.edx,r.ecx);
        r.ecx=lo(r.ecx,R16(f-0x94u));W16(r.edx+4u,r.ecx);r.edx=R32(f-0xa0u);W16(r.edx+0x26u,0);
        if(s16(R16(f-0x976u))>=0) { r.edx=sx(R16(f-0x976u));r.ecx=al_trigger(m,g,r.edx); } else r.ecx=0;
        W32(f-0xa4u,r.ecx);r.edx=R32(f-0xa4u);W32(r.eax+0x1b2u,r.edx);
        if(s16(R16(f-0x974u))>=0) { r.edx=sx(R16(f-0x974u));r.ecx=al_trigger(m,g,r.edx); } else r.ecx=0;
        W32(f-0xa8u,r.ecx);r.ecx=f-0x98au;r.edx=R32(f-0xa8u);W32(r.eax+0x1b6u,r.edx);r.edx=f-0x99eu;
        a[0]=r.eax;a[1]=r.edx;a[2]=r.ecx;ml_call(g,0x411b90u,a,3,&r);
    }
    /* 425c34: buildings (52 bytes; selection 4AA8BAh, the factory byte
     * from 4AA9D2h+i): building_factory(record, byte); with a coordinate
     * (word +2) the object's virtual +10h (shape_word), the terrain marks
     * 470ed4(terrain, point, sx(word)) and the ground height (-32768
     * becomes -32767), otherwise the defaults; angle +4 or the defaults; the
     * trigger pointers +2Eh/+30h, action_copy(B, +6, +1Ah). */
    r.ecx=stream;r.eax=R32(stream);al_vread(m,g,f,2,f-0x36u,r.eax,&r);
    ebx=0;W32(f-0xf4u,GVA(0x4aa9d2u));edi=GVA(0x4aa8bau);
    while(s16(ebx)<s16(R16(f-0x36u))) {
        r.eax=f-0xa0cu;r.edx=stream;al_read(g,f,0x34u,r.eax,&r);
        if(R8(edi)) {
            r.ecx=R32(f-0xf4u);r.eax=(r.eax&0xffffff00u)|R8(r.ecx);r.edx=f-0xa0cu;
            a[0]=r.edx;a[1]=r.eax;ml_call(g,0x405314u,a,2,&r);
            esi=r.eax;
            if(s16(R16(f-0xa0au))>=0) {
                r.eax=sx(R16(f-0xa0au));r.edx=12u*r.eax+R32(GVA(0x4a9e0cu));W32(f-0xb4u,r.edx);
                r.ecx=R32(f-0xb4u);r.eax=R32(r.ecx+8u);W32(f-0xa10u,r.eax);
                r.edx=R32(f-0xb4u);r.ecx=R32(r.edx);W32(f-0xa18u,r.ecx);
                r.eax=R32(f-0xb4u);r.edx=R32(r.eax+4u);W32(f-0xa14u,r.edx);
                r.ecx=R32(esi);
                a[0]=esi;ml_virtual(g,R32(r.ecx+0x10u),0x10u,a,1,&r);
                r.ecx=a[0];                         /* POP ECX */
                r.eax=sx(r.eax);
                r.edx=f-0xa18u;r.ecx=R32(GVA(0x4a0be0u));
                a[0]=r.ecx;a[1]=r.edx;a[2]=r.eax;ml_call(g,0x470ed4u,a,3,&r);
                r.eax=f-0xa18u;r.edx=R32(GVA(0x4a0be0u));
                a[0]=r.edx;a[1]=r.eax;ml_call(g,0x46e180u,a,2,&r);
                W32(f-0xa10u,r.eax);
                if(R32(f-0xa10u)==0xffff8000u)W32(f-0xa10u,R32(f-0xa10u)+1u);
            } else {
                r.ecx=R32(GVA(0x49a879u));W32(f-0xa10u,r.ecx);
                r.eax=R32(GVA(0x49a871u));W32(f-0xa18u,r.eax);
                r.edx=R32(GVA(0x49a875u));W32(f-0xa14u,r.edx);
            }
            if(s16(R16(f-0xa08u))>=0) {
                r.ecx=0;r.edx=R32(GVA(0x4a9e10u));W16(f-0xaeu,r.ecx);W16(f-0xb0u,r.ecx);
                r.eax=sx(R16(f-0xa08u));r.eax=lo(r.eax,R16(r.edx+2u*r.eax));W16(f-0xacu,r.eax);
            } else {
                r.edx=R32(GVA(0x49a87eu));W32(f-0xb0u,r.edx);
                r.edx=lo(r.edx,R16(GVA(0x49a882u)));W16(f-0xacu,r.edx);
            }
            r.eax=esi+0x26u;r.edx=R32(f-0xa10u);W32(r.eax+8u,r.edx);
            r.ecx=R32(f-0xa18u);W32(r.eax,r.ecx);r.edx=R32(f-0xa14u);W32(r.eax+4u,r.edx);
            r.eax=esi+0xcu;W32(f-0xb8u,r.eax);r.ecx=R32(f-0xb8u);r.eax=R32(f-0xb0u);W32(r.ecx,r.eax);
            r.eax=lo(r.eax,R16(f-0xacu));W16(r.ecx+4u,r.eax);r.edx=R32(f-0xb8u);W16(r.edx+0x26u,0);
            if(s16(R16(f-0x9deu))>=0) { r.eax=sx(R16(f-0x9deu));r.ecx=al_trigger(m,g,r.eax); } else r.ecx=0;
            W32(f-0xbcu,r.ecx);r.edx=R32(f-0xbcu);W32(esi+0x1b2u,r.edx);
            if(s16(R16(f-0x9dcu))>=0) { r.edx=sx(R16(f-0x9dcu));r.eax=al_trigger(m,g,r.edx); } else r.eax=0;
            W32(f-0xc0u,r.eax);r.ecx=f-0x9f2u;r.edx=R32(f-0xc0u);r.eax=f-0xa06u;W32(esi+0x1b6u,r.edx);
            a[0]=esi;a[1]=r.eax;a[2]=r.ecx;ml_call(g,0x411b90u,a,3,&r);
        }
        ++ebx;W32(f-0xf4u,R32(f-0xf4u)+1u);++edi;
    }
    /* 425e97: orders: the count straight into 4A9E2Ch, new[] of 22-byte
     * runtime orders at 4A9E30h; per 14-byte record: words +0, +2 and the
     * scope +8, the trigger (+0Ch), route (+6, 6-byte routes at 4A9E18h)
     * and coordinate (+4) pointers for indices other than -1, and the
     * subject subject_resolve(sx +0Ah, sx +8). */
    r.edx=stream;r.ecx=R32(stream);al_vread(m,g,f,2,GVA(0x4a9e2cu),r.ecx,&r);
    if(R16(GVA(0x4a9e2cu))) {
        r.eax=sx(R16(GVA(0x4a9e2cu)));r.edx=22u*r.eax;
        a[0]=r.edx;ml_call(g,0x4774f0u,a,1,&r);
        r.ecx=a[0];                                 /* POP ECX */
        W32(GVA(0x4a9e30u),r.eax);esi=R32(GVA(0x4a9e30u));
        for(ebx=0;s16(ebx)<s16(R16(GVA(0x4a9e2cu)));++ebx,esi+=0x16u) {
            r.eax=f-0xa28u;r.edx=stream;al_read(g,f,0xeu,r.eax,&r);
            r.ecx=lo(r.ecx,R16(f-0xa28u));W16(esi,r.ecx);
            r.eax=lo(r.eax,R16(f-0xa26u));W16(esi+2u,r.eax);
            r.edx=lo(r.edx,R16(f-0xa20u));W16(esi+0xcu,r.edx);
            if(R16(f-0xa1cu)!=0xffffu) { r.ecx=sx(R16(f-0xa1cu));r.eax=al_trigger(m,g,r.ecx); } else r.eax=0;
            W32(esi+0x12u,r.eax);
            if(R16(f-0xa22u)!=0xffffu) { r.edx=sx(R16(f-0xa22u));r.ecx=6u*r.edx+R32(GVA(0x4a9e18u)); } else r.ecx=0;
            W32(esi+8u,r.ecx);
            if(R16(f-0xa24u)!=0xffffu) { r.eax=sx(R16(f-0xa24u));r.edx=12u*r.eax+R32(GVA(0x4a9e0cu)); } else r.edx=0;
            W32(esi+4u,r.edx);
            r.eax=sx(R16(f-0xa20u));a[1]=r.eax;r.ecx=sx(R16(f-0xa1eu));a[0]=r.ecx;
            ml_call(g,0x425348u,a,2,&r);
            W32(esi+0xeu,r.eax);
        }
    }
    /* 425fb7: groups. The count read here is only tested for 0; the loop
     * runs over pass 1's group array ([4A9E00h], count word 4A9E04h, stride
     * 7Ah). Per 156-byte record: group 0's roster becomes the remap indices
     * of the MEC actors (4AA78Eh + M + k for k < N, else -1), the other
     * groups' non-negative slots go through the remap table of their class
     * (+28h: 0, 1, 2; others unchanged); the ten order pointers of words
     * +5Ah (22-byte orders, 0 for -1) to f-0AECh; the heading: angle +2Eh,
     * else heading_point of the first two points of the first order's route
     * (at least two points), else 0; the trigger (+70h) and coordinate
     * (+2Ch) pointers for indices other than -1; group_ctor(G, record,
     * trigger, orders, heading, point); for a base group (class 2, word 0 =
     * 1) base_marks of its first member, or of 0 without members. */
    r.eax=f-0x36u;r.edx=stream;r.ecx=R32(stream);al_vread(m,g,f,2,r.eax,r.ecx,&r);
    if(R16(f-0x36u)) {
        W16(f-0xc2u,0);r.eax=lo(r.eax,R16(GVA(0x4a9e04u)));W16(f-0xc4u,r.eax);
        r.edx=R32(GVA(0x4a9e00u));W32(f-0xc8u,r.edx);
        for(;;) {
            r.ecx=lo(r.ecx,R16(f-0xc4u));W16(f-0xc4u,R16(f-0xc4u)+0xffffu);
            if((uint16_t)r.ecx) { r.eax=R32(f-0xc8u);W32(f-0xc8u,R32(f-0xc8u)+0x7au); } else r.eax=0;
            W32(f-0xccu,r.eax);
            if(!r.eax)break;
            r.ecx=f-0xac4u;r.eax=stream;al_read(g,f,0x9cu,r.ecx,&r);
            r.eax=f-0xa92u;
            if(!R16(f-0xc2u)) {
                ebx=0;r.edx=GVA(0x4aa78eu);
                do {
                    if(s16(ebx)<s16(R16(GVA(0x4aaa5eu)))) {
                        r.ecx=sx(R16(f-0x38u));r.ecx=sx8(R8(r.edx+r.ecx));W16(r.eax,r.ecx);
                    } else W16(r.eax,0xffffu);
                    ++r.edx;++ebx;r.eax+=2u;
                } while(s16(ebx)<0x14);
            } else {
                ebx=0;
                do {
                    if(s16(R16(r.eax))>=0) {
                        r.edx=sx(R16(f-0xa9cu));
                        const uint32_t cls=r.edx;
                        uint32_t table=0;
                        r.edx-=1u;
                        if(!cls)table=0x4aa78eu;                       /* SUB 1; JB */
                        else if(!r.edx)table=0x4aa888u;                /* JE */
                        else if(!--r.edx)table=0x4aa946u;              /* DEC; JE */
                        if(table) { r.ecx=sx(R16(r.eax));r.edx=sx8(R8(r.ecx+GVA(table)));W16(r.eax,r.edx); }
                    }
                    ++ebx;r.eax+=2u;
                } while(s16(ebx)<0x14);
            }
            W16(f-0xceu,0);r.eax=f-0xa6au;ebx=0;r.edx=f-0xaecu;
            do {
                if(R16(r.eax)!=0xffffu) { r.ecx=sx(R16(r.eax));esi=r.ecx;r.ecx=22u*esi+R32(GVA(0x4a9e30u)); }
                else r.ecx=0;
                W32(r.edx,r.ecx);r.edx+=4u;++ebx;r.eax+=2u;
            } while(s16(ebx)<0xa);
            if(s16(R16(f-0xa96u))<0) {
                r.eax=R32(f-0xaecu);r.edx=R32(r.eax+8u);W32(f-0xd4u,r.edx);
                if(R32(f-0xd4u)) {
                    r.eax=R32(f-0xd4u);
                    if(s16(R16(r.eax))>=2) {
                        r.edx=R32(f-0xd4u);r.ecx=R32(r.edx+2u);r.eax=R32(r.ecx);a[1]=r.eax;
                        r.edx=R32(f-0xd4u);r.ecx=R32(r.edx+2u);r.eax=R32(r.ecx+4u);a[0]=r.eax;
                        ml_call(g,0x492934u,a,2,&r);
                        W16(f-0xceu,r.eax);
                    }
                }
            } else {
                r.edx=sx(R16(f-0xa96u));r.ecx=R32(GVA(0x4a9e10u));r.eax=lo(r.eax,R16(r.ecx+2u*r.edx));W16(f-0xceu,r.eax);
            }
            r.edx=lo(r.edx,R16(f-0xa54u));W16(f-0xd6u,r.edx);
            if(R16(f-0xd6u)!=0xffffu) { r.ecx=sx(R16(f-0xd6u));r.eax=al_trigger(m,g,r.ecx); } else r.eax=0;
            W32(f-0xdcu,r.eax);
            if(R16(f-0xa98u)==0xffffu)r.edx=0;
            else { r.ecx=sx(R16(f-0xa98u));r.edx=12u*r.ecx+R32(GVA(0x4a9e0cu)); }
            W32(f-0xe0u,r.edx);
            r.eax=R32(f-0xe0u);a[5]=r.eax;
            r.eax=f-0xaecu;r.ecx=lo(r.ecx,R16(f-0xceu));a[4]=r.ecx;a[3]=r.eax;
            r.edx=R32(f-0xdcu);r.ecx=f-0xac4u;a[2]=r.edx;a[1]=r.ecx;
            r.eax=R32(f-0xccu);a[0]=r.eax;
            ml_call(g,0x423b34u,a,6,&r);
            W16(f-0xc2u,R16(f-0xc2u)+1u);
            if(R16(f-0xa9cu)==2u&&R16(f-0xac4u)==1u) {
                r.edx=R32(f-0xccu);
                if(s16(R16(r.edx+0x10u))>0) { r.ecx=R32(f-0xccu);r.eax=R32(r.ecx+0xcu);r.edx=R32(r.eax); }
                else r.edx=0;
                W32(f-0xe4u,r.edx);r.eax=R32(f-0xe4u);
                a[0]=r.eax;ml_call(g,0x405db0u,a,1,&r);
                r.ecx=a[0];                         /* POP ECX */
            }
        }
    }
    /* 42626a: objectives: the count straight into 4A9E3Ch, new[] of 76-byte
     * runtime objectives at 4A9E40h; per 54-byte record: words +0, +2, the
     * scope +4, the subject subject_resolve(sx +6, sx +4), the coordinate
     * (+8) and route (+0Ah) pointers for indices above -1, two 20-byte
     * memcpy (+0Eh, +22h), three string pointers from 4AA660h + 4 x index
     * (+0Ch above -1, else 0) and 49A870h. */
    r.edx=stream;r.ecx=R32(stream);al_vread(m,g,f,2,GVA(0x4a9e3cu),r.ecx,&r);
    if(R16(GVA(0x4a9e3cu))) {
        r.eax=sx(R16(GVA(0x4a9e3cu)));r.edx=76u*r.eax;
        a[0]=r.edx;ml_call(g,0x4774f0u,a,1,&r);
        r.ecx=a[0];                                 /* POP ECX */
        W32(GVA(0x4a9e40u),r.eax);esi=R32(GVA(0x4a9e40u));
        for(ebx=0;s16(ebx)<s16(R16(GVA(0x4a9e3cu)));++ebx,esi+=0x4cu) {
            r.eax=f-0xb24u;r.edx=stream;al_read(g,f,0x36u,r.eax,&r);
            r.ecx=lo(r.ecx,R16(f-0xb24u));W16(esi,r.ecx);
            r.eax=lo(r.eax,R16(f-0xb22u));W16(esi+2u,r.eax);
            r.edx=lo(r.edx,R16(f-0xb20u));W16(esi+4u,r.edx);
            r.ecx=sx(R16(f-0xb20u));a[1]=r.ecx;r.eax=sx(R16(f-0xb1eu));a[0]=r.eax;
            ml_call(g,0x425348u,a,2,&r);
            W32(esi+6u,r.eax);
            if(s16(R16(f-0xb1cu))>-1) { r.edx=sx(R16(f-0xb1cu));r.ecx=12u*r.edx+R32(GVA(0x4a9e0cu)); } else r.ecx=0;
            W32(esi+0xau,r.ecx);
            if(s16(R16(f-0xb1au))>-1) { r.eax=sx(R16(f-0xb1au));r.edx=6u*r.eax+R32(GVA(0x4a9e18u)); } else r.edx=0;
            W32(esi+0xeu,r.edx);
            r.eax=f-0xb16u;r.ecx=esi+0x24u;a[0]=r.ecx;a[1]=r.eax;a[2]=0x14u;ml_call(g,0x49680eu,a,3,&r);
            r.eax=f-0xb02u;r.edx=esi+0x38u;a[0]=r.edx;a[1]=r.eax;a[2]=0x14u;ml_call(g,0x49680eu,a,3,&r);
            r.eax=esi+0x12u;r.edx=(sx(R16(f-0xb18u))<<2)+GVA(0x4aa660u);r.ecx=0;W32(f-0xe8u,r.ecx);
            do {
                if(s16(R16(f-0xb18u))>-1)r.ecx=R32(r.edx); else r.ecx=0;
                W32(r.eax,r.ecx);W32(f-0xe8u,R32(f-0xe8u)+1u);r.eax+=4u;r.edx+=4u;
            } while((int32_t)R32(f-0xe8u)<3);
            W32(r.eax,GVA(0x49a870u));
        }
    }
    /* 4263d3: the terrain cell marks (47129c), then every building of the
     * pool (pool_prev order, oldest first) at the ground height (no -32768
     * adjustment, no offsets). */
    r.eax=R32(GVA(0x4a0be0u));a[0]=r.eax;ml_call(g,0x47129cu,a,1,&r);
    r.ecx=a[0];                                     /* POP ECX */
    r.edx=0;W32(f-0xecu,r.edx);
    for(;;) {
        r.eax=R32(f-0xecu);W32(f-0xf0u,r.eax);r.ecx=R32(f-0xf0u);
        r.eax=R32(GVA(0x4a95a0u));a[0]=r.eax;a[1]=r.ecx;ml_call(g,0x471c70u,a,2,&r);
        W32(f-0xecu,r.eax);ebx=r.eax;
        if(!r.eax)break;
        r.eax=ebx+0x26u;r.edx=R32(r.eax);W32(f-0xb30u,r.edx);r.edx=f-0xb30u;
        r.ecx=R32(r.eax+4u);W32(f-0xb2cu,r.ecx);r.ecx=R32(GVA(0x4a0be0u));
        r.eax=R32(r.eax+8u);W32(f-0xb28u,r.eax);
        a[0]=r.ecx;a[1]=r.edx;ml_call(g,0x46e180u,a,2,&r);
        W32(f-0xb28u,r.eax);r.eax=ebx+0x26u;r.edx=R32(f-0xb28u);W32(r.eax+8u,r.edx);
        r.ecx=R32(f-0xb30u);W32(r.eax,r.ecx);r.edx=R32(f-0xb2cu);W32(r.eax+4u,r.edx);
    }
    /* 426470: the trigger subjects ([4A9E28h], count 4A9E24h reread, 58-byte
     * records): the type word through the byte table 426492 (read from the
     * image) and the jump table 42649d: types 0..6 (case 5) clear the
     * subject dword +36h, 7/8/9/10 (cases 4/3/2/1) resolve it with scope
     * 1/2/3/0 through subject_resolve, other types (above 10 unsigned) and
     * case 0 leave it. */
    esi=R32(GVA(0x4a9e28u));
    for(ebx=0;s16(ebx)<s16(R16(GVA(0x4a9e24u)));++ebx,esi+=0x3au) {
        edi=0xffffffffu;
        r.eax=sx(R16(esi));
        if(r.eax<=0xau) {
            r.eax=(r.eax&0xffffff00u)|R8(GVA(0x426492u)+r.eax);
            switch(r.eax) {
            case 5:r.edx=0;W32(esi+0x36u,r.edx);break;
            case 4:edi=1;break;
            case 3:edi=2;break;
            case 2:edi=3;break;
            case 1:edi=0;break;
            default:break;
            }
        }
        if(edi!=0xffffffffu) {
            a[1]=edi;r.eax=R32(esi+0x36u);a[0]=r.eax;ml_call(g,0x425348u,a,2,&r);
            W32(esi+0x36u,r.eax);
        }
    }
    /* 4264f5: the stream closed (47ad78(stream, 2): no delete); ECX = the
     * record's saved FS:0 head, which the caller writes back to FS:0. */
    r.edx=stream;a[0]=r.edx;a[1]=2;ml_call(g,0x47ad78u,a,2,&r);
    r.ecx=R32(f-0x34u);
    ecx_out(ecx,r.ecx);
    return pair(r.eax,r.edx);
}
#undef AL_MEC

/* ---- L7: the world load 424308, script pass 1, and the four leaves
 * only it calls (mission_load.md, section "L7 world load"). The frame lies in
 * g->scratch at the original's offsets below f: the runtime record f-54h
 * (context word f-44h), the three stream objects (12 bytes each: player.mec
 * f-24h, mission.var f-18h, the script f-0Ch), the pool and list headers f-28h
 * .. f-34h, the count words of the actor lists f-56h (mechs M), f-58h (flyers),
 * f-5Ah (buildings), f-62h (groups), f-64h (orders), f-66h (objectives), the
 * compaction counters f-5Ch/-5Eh/-60h, the cursors f-6Ch .. f-14Ch, the local
 * type arrays (mechs f-9E4h, 100 words; flyers f-0A48h, 50; buildings f-0B60h)
 * and the record buffers (point f-0B6Ch, trigger words f-0B7Ch, timer f-0B94h
 * with its ten trigger pointers f-0BBCh, mech f-0C44h, flyer f-0CA0h, building
 * f-0CD4h, order f-0CE4h, group 0 f-0D80h, group f-0E1Ch, objective f-0E54h).
 * Every local the original writes is written here too, in its order. EBX is the
 * loop counter (compared as BX, signed words), ESI and EDI the record cursors.
 * */
/* The stream's read at slot +18h of its table (stream, size, buffer); the
 * caller sets the original's registers, `table` is the value it read from
 * the stream object. */
static void wl_vread(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t stream,uint32_t size,uint32_t buffer,
    uint32_t table,es2_weapon_registers *r) {
    const uint32_t a[3]={stream,size,buffer};
    ml_virtual(g,R32(table+0x18u),0x18u,a,3,r);
}
/* The direct read 47af98 (stream, size, buffer). */
static void wl_read(const es2_weapon_graph *g,uint32_t stream,uint32_t size,uint32_t buffer,es2_weapon_registers *r) {
    const uint32_t a[3]={stream,size,buffer};
    ml_call(g,0x47af98u,a,3,r);
}
/* memset 49681a (dest, 0, size). */
static void wl_clear(const es2_weapon_graph *g,uint32_t dest,uint32_t size,es2_weapon_registers *r) {
    const uint32_t a[3]={dest,0,size};
    ml_call(g,0x49681au,a,3,r);
}
/* The fatal report 479c78(6C56C8h, 5, edx, file, line, name) of a failed
 * open; the SETE register (EDX or ECX, the caller sets it) is 1 when the
 * reread status is 0, else 0. */
static void wl_report(const es2_weapon_graph *g,uint32_t flag,uint32_t file,uint32_t line,uint32_t name,
    es2_weapon_registers *r) {
    const uint32_t a[6]={GVA(0x6c56c8u),5,flag,GVA(file),line,name};
    ml_call(g,0x479c78u,a,6,r);
}
/* 58 x sx(word) + [4A9E28h]: a runtime trigger (SHL, SUB, LEA, ADD in the
 * original; the same modulo 2^32). */
static uint32_t wl_trigger(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t index) {
    return 58u*index+R32(GVA(0x4a9e28u));
}

/* Original: DBSIM.EXE 0x424308. */
uint64_t es2_world_load_registers(const es2_guest_memory *m,const es2_weapon_graph *g,const uint32_t entry[3],
    uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_WORLD_LOAD_FRAME,context=f-0x44u,player=f-0x24u,vars=f-0x18u,stream=f-0xcu;
    es2_weapon_registers r={GVA(0x49a7fcu),entry[1],entry[2]};uint32_t a[6],ebx,esi,edi=player;
    ml_call(g,0x49630cu,NULL,0,&r);
    /* 424321: the selection, remap and MEC tables and the two local type
     * arrays cleared. */
    wl_clear(g,GVA(0x4aa72au),100,&r);
    wl_clear(g,GVA(0x4aa78eu),100,&r);
    r.edx=f-0x9e4u;wl_clear(g,r.edx,0xc8u,&r);
    wl_clear(g,GVA(0x4aa7f2u),100,&r);
    wl_clear(g,GVA(0x4aa856u),50,&r);
    wl_clear(g,GVA(0x4aa888u),50,&r);
    r.ecx=f-0xa48u;wl_clear(g,r.ecx,100,&r);
    wl_clear(g,GVA(0x4aa8bau),140,&r);
    wl_clear(g,GVA(0x4aa946u),140,&r);
    wl_clear(g,GVA(0x4aa9d2u),140,&r);
    /* 4243db: data\mission.var (context 8): 2000 bytes into the variables,
     * then words 20, 10 and 21..42 cleared; the stream closed (no delete). */
    W16(context,8);
    a[0]=GVA(0x49a9cbu);ml_call(g,0x45ee00u,a,1,&r);
    r.ecx=a[0];                                     /* POP ECX */
    {const uint32_t name=r.eax;r.eax=vars;a[0]=r.eax;a[1]=name;ml_call(g,0x47ad40u,a,2,&r);}
    if(R32(vars+4u)) { r.edx=R32(vars+4u)==0;wl_report(g,r.edx,0x49a9dcu,0xa5u,GVA(0x49a9e6u),&r); }
    r.ecx=vars;wl_read(g,vars,0x7d0u,GVA(0x4a9e70u),&r);
    W16(GVA(0x4a9e98u),0);
    W16(GVA(0x4a9e84u),0);
    r.eax=0x15u;esi=GVA(0x4a9e9au);
    do { W16(esi,0);++r.eax;esi+=2u; } while((int32_t)r.eax<=0x2a);
    r.eax=vars;a[0]=r.eax;a[1]=2;ml_call(g,0x47ad78u,a,2,&r);
    /* 424476: data\player.mec (context 14h): the player ordinal 4AAA64h and
     * N 4AAA5Eh through the read at slot +18h, new[] of 126 x N bytes at
     * 4AAA60h, per record the words +0 .. +6, two lists of word +6 words at
     * +8 and +20h, word +3Ah and 26, 20 and 20 bytes at +3Ch, +56h, +6Ah. */
    W16(context,0x14u);
    a[0]=GVA(0x49a9f2u);ml_call(g,0x45ee00u,a,1,&r);
    r.ecx=a[0];                                     /* POP ECX */
    a[0]=edi;a[1]=r.eax;ml_call(g,0x47ad40u,a,2,&r);
    if(R32(edi+4u)) { r.edx=R32(edi+4u)==0;wl_report(g,r.edx,0x49aa02u,0xb2u,GVA(0x49aa0cu),&r); }
    r.ecx=R32(edi);wl_vread(m,g,edi,2,GVA(0x4aaa64u),r.ecx,&r);
    r.eax=R32(edi);wl_vread(m,g,edi,2,GVA(0x4aaa5eu),r.eax,&r);
    r.edx=sx(R16(GVA(0x4aaa5eu)));r.ecx=r.edx<<6;r.ecx-=r.edx;r.ecx+=r.ecx;
    a[0]=r.ecx;ml_call(g,0x4774f0u,a,1,&r);
    r.ecx=a[0];                                     /* POP ECX */
    W32(GVA(0x4aaa60u),r.eax);esi=R32(GVA(0x4aaa60u));
    for(ebx=0;s16(ebx)<s16(R16(GVA(0x4aaa5eu)));++ebx,esi+=0x7eu) {
        r.eax=esi;r.edx=R32(edi);wl_vread(m,g,edi,2,r.eax,r.edx,&r);
        r.eax=esi+2u;r.edx=R32(edi);wl_vread(m,g,edi,2,r.eax,r.edx,&r);
        r.eax=esi+4u;r.edx=R32(edi);wl_vread(m,g,edi,2,r.eax,r.edx,&r);
        r.eax=esi+6u;r.edx=R32(edi);wl_vread(m,g,edi,2,r.eax,r.edx,&r);
        r.ecx=esi+8u;r.eax=2u*sx(R16(esi+6u));wl_read(g,edi,r.eax,r.ecx,&r);
        r.edx=esi+0x20u;r.ecx=2u*sx(R16(esi+6u));wl_read(g,edi,r.ecx,r.edx,&r);
        r.eax=esi+0x3au;r.edx=R32(edi);wl_vread(m,g,edi,2,r.eax,r.edx,&r);
        r.ecx=esi+0x3cu;wl_read(g,edi,0x1au,r.ecx,&r);
        r.eax=esi+0x56u;wl_read(g,edi,0x14u,r.eax,&r);
        r.edx=esi+0x6au;wl_read(g,edi,0x14u,r.edx,&r);
    }
    a[0]=edi;a[1]=2;ml_call(g,0x47ad78u,a,2,&r);
    /* 4245c4: byte 4D2571h = (MEC record 0's word +4 is 8); data\mission.str
     * into 4AA650h (453878), up to 50 string pointers at 4AA660h (4539a0,
     * the count is its AX), each string's final LF (byte before the length
     * strlen 496766 gives) removed. */
    r.ecx=R32(GVA(0x4aaa60u));W32(f-0x6cu,r.ecx);r.eax=R32(f-0x6cu);
    r.edx=R16(r.eax+4u)==8u;
    W8(GVA(0x4d2571u),r.edx);
    a[0]=GVA(0x49aa17u);ml_call(g,0x45ee00u,a,1,&r);
    r.ecx=a[0];                                     /* POP ECX */
    a[0]=GVA(0x4aa650u);a[1]=r.eax;ml_call(g,0x453878u,a,2,&r);
    a[0]=GVA(0x4aa650u);a[1]=GVA(0x4aa660u);a[2]=0x32u;a[3]=0;ml_call(g,0x4539a0u,a,4,&r);
    r.ecx=sx(r.eax);W32(f-0x70u,r.ecx);
    ebx=GVA(0x4aa660u);
    for(esi=0;(int32_t)esi<(int32_t)R32(f-0x70u);++esi,ebx+=4u) {
        r.eax=R32(ebx);a[0]=r.eax;ml_call(g,0x496766u,a,1,&r);
        r.ecx=a[0];                                 /* POP ECX */
        r.edx=R32(ebx);r.ecx=sx8(R8(r.edx+r.eax-1u));
        if(r.ecx==0xau) { r.edx=R32(ebx);W8(r.edx+r.eax-1u,0); }
    }
    /* 424647: data\script<[4A09F0h]> (45ee00, 492b90; the outer call's
     * second slot pushed first), the stream f-0Ch (context 20h), the 20-byte
     * header 4A9E4Eh, word 4A9E52h cleared, the world 42e010(4A9E4Eh). */
    r.eax=R32(GVA(0x4a09f0u));
    {const uint32_t ext=r.eax;a[0]=GVA(0x49aa28u);ml_call(g,0x45ee00u,a,1,&r);
     r.ecx=a[0];                                    /* POP ECX */
     a[0]=r.eax;a[1]=ext;ml_call(g,0x492b90u,a,2,&r);}
    W32(f-0x74u,r.eax);
    W16(context,0x20u);
    r.ecx=R32(f-0x74u);r.eax=stream;a[0]=r.eax;a[1]=r.ecx;ml_call(g,0x47ad40u,a,2,&r);
    if(R32(stream+4u)) { r.edx=R32(f-0x74u);r.ecx=R32(stream+4u)==0;wl_report(g,r.ecx,0x49aa34u,0xdcu,r.edx,&r); }
    r.eax=stream;wl_read(g,stream,0x14u,GVA(0x4a9e4eu),&r);
    W16(GVA(0x4a9e52u),0);
    a[0]=GVA(0x4a9e4eu);ml_call(g,0x42e010u,a,1,&r);
    r.ecx=a[0];                                     /* POP ECX */
    /* 4246cf: coordinates: the count 4A9E06h; when nonzero the 12-byte
     * points (49667c(0, 0Ch, n, 1, 4953C4h)) at 4A9E0Ch and the map
     * rectangle (min x 4AA640h, min y 4AA644h, max x 4AA648h, max y 4AA64Ch,
     * signed) over every point read. */
    r.edx=stream;r.ecx=R32(stream);wl_vread(m,g,stream,2,GVA(0x4a9e06u),r.ecx,&r);
    if(R16(GVA(0x4a9e06u))) {
        r.eax=sx(R16(GVA(0x4a9e06u)));
        a[0]=0;a[1]=0xcu;a[2]=r.eax;a[3]=1;a[4]=GVA(0x4953c4u);ml_call(g,0x49667cu,a,5,&r);
        r.edx=0x7fffffffu;W32(GVA(0x4a9e0cu),r.eax);W32(GVA(0x4aa644u),r.edx);
        r.eax=0x80000000u;W32(GVA(0x4aa640u),r.edx);W32(GVA(0x4aa64cu),r.eax);W32(GVA(0x4aa648u),r.eax);
        esi=R32(GVA(0x4a9e0cu));
        for(ebx=0;s16(ebx)<s16(R16(GVA(0x4a9e06u)));++ebx,esi+=12u) {
            r.eax=f-0xb6cu;r.edx=stream;wl_read(g,stream,12,r.eax,&r);
            r.ecx=R32(f-0xb6cu);W32(esi,r.ecx);
            r.eax=R32(f-0xb68u);W32(esi+4u,r.eax);
            r.edx=R32(f-0xb64u);W32(esi+8u,r.edx);
            r.ecx=R32(f-0xb6cu);
            if((int32_t)r.ecx<(int32_t)R32(GVA(0x4aa640u))) { r.eax=R32(f-0xb6cu);W32(GVA(0x4aa640u),r.eax); }
            r.edx=R32(f-0xb68u);
            if((int32_t)r.edx<(int32_t)R32(GVA(0x4aa644u))) { r.ecx=R32(f-0xb68u);W32(GVA(0x4aa644u),r.ecx); }
            r.eax=R32(f-0xb6cu);
            if((int32_t)r.eax>(int32_t)R32(GVA(0x4aa648u))) { r.edx=R32(f-0xb6cu);W32(GVA(0x4aa648u),r.edx); }
            r.ecx=R32(f-0xb68u);
            if((int32_t)r.ecx>(int32_t)R32(GVA(0x4aa64cu))) { r.eax=R32(f-0xb68u);W32(GVA(0x4aa64cu),r.eax); }
        }
    }
    /* 4247e4: angles: new[](2n) at 4A9E10h, each word read x 182. */
    r.edx=stream;r.ecx=R32(stream);wl_vread(m,g,stream,2,GVA(0x4a9e08u),r.ecx,&r);
    if(R16(GVA(0x4a9e08u))) {
        r.eax=2u*sx(R16(GVA(0x4a9e08u)));a[0]=r.eax;ml_call(g,0x4774f0u,a,1,&r);
        W32(GVA(0x4a9e10u),r.eax);r.ecx=a[0];       /* POP ECX */
        r.edx=R32(GVA(0x4a9e10u));W32(f-0x78u,r.edx);
        for(ebx=0;s16(ebx)<s16(R16(GVA(0x4a9e08u)));++ebx) {
            r.eax=f-0xfeu;r.edx=stream;wl_read(g,stream,2,r.eax,&r);
            r.ecx=sx(R16(f-0xfeu));r.edx=R32(f-0x78u);
            r.eax=182u*r.ecx;                       /* LEA, SHL 3, SUB, SHL 2, SUB, ADD */
            W16(r.edx,r.eax);
            W32(f-0x78u,R32(f-0x78u)+2u);
        }
    }
    /* 424863: routes: new[](6n) at 4A9E18h; per 6-byte route the point
     * count word (read at slot +18h), new[](4 x count) of point pointers at
     * +2, each 12 x sx(index read) + [4A9E0Ch]. The inner loop compares CX
     * (the count word, reread) with the counter word f-106h. */
    r.ecx=stream;r.eax=R32(stream);wl_vread(m,g,stream,2,GVA(0x4a9e14u),r.eax,&r);
    if(R16(GVA(0x4a9e14u))) {
        r.edx=sx(R16(GVA(0x4a9e14u)));r.ecx=r.edx+r.edx;r.ecx*=3u;
        a[0]=r.ecx;ml_call(g,0x4774f0u,a,1,&r);
        r.ecx=a[0];                                 /* POP ECX */
        W32(GVA(0x4a9e18u),r.eax);esi=R32(GVA(0x4a9e18u));
        for(ebx=0;s16(ebx)<s16(R16(GVA(0x4a9e14u)));++ebx,esi+=6u) {
            W32(f-0x104u,esi);r.edx=stream;r.eax=R32(f-0x104u);r.ecx=R32(stream);wl_vread(m,g,stream,2,r.eax,r.ecx,&r);
            r.eax=sx(R16(esi))<<2;a[0]=r.eax;ml_call(g,0x4774f0u,a,1,&r);
            r.ecx=a[0];                             /* POP ECX */
            W32(esi+2u,r.eax);edi=R32(esi+2u);
            W16(f-0x106u,0);
            for(;;) {
                r.ecx=lo(r.ecx,R16(esi));
                if(!(s16(r.ecx)>s16(R16(f-0x106u))))break;
                r.eax=f-0x108u;r.edx=stream;r.ecx=R32(stream);wl_vread(m,g,stream,2,r.eax,r.ecx,&r);
                r.eax=sx(R16(f-0x108u));r.edx=r.eax<<2;r.edx*=3u;
                r.edx+=R32(GVA(0x4a9e0cu));W32(edi,r.edx);
                W16(f-0x106u,R16(f-0x106u)+1u);edi+=4u;
            }
        }
    }
    /* 42493a: regions: new[](10n) at 4A9E20h, per 6-byte record
     * region_init(region, record, [4A9E0Ch], the coordinate count in CX):
     * four slots, the fourth unread. */
    r.eax=stream;r.edx=R32(stream);wl_vread(m,g,stream,2,GVA(0x4a9e1cu),r.edx,&r);
    if(R16(GVA(0x4a9e1cu))) {
        r.ecx=sx(R16(GVA(0x4a9e1cu)));r.eax=r.ecx+r.ecx;r.eax*=5u;
        a[0]=r.eax;ml_call(g,0x4774f0u,a,1,&r);
        W32(GVA(0x4a9e20u),r.eax);r.ecx=a[0];       /* POP ECX */
        r.edx=R32(GVA(0x4a9e20u));W32(f-0x10cu,r.edx);
        for(ebx=0;s16(ebx)<s16(R16(GVA(0x4a9e1cu)));++ebx) {
            r.eax=f-0x114u;r.edx=stream;wl_read(g,stream,6,r.eax,&r);
            r.ecx=lo(r.ecx,R16(GVA(0x4a9e06u)));
            r.eax=R32(GVA(0x4a9e0cu));r.edx=f-0x114u;
            a[3]=r.ecx;a[2]=r.eax;a[1]=r.edx;r.ecx=R32(f-0x10cu);a[0]=r.ecx;
            ml_call(g,0x423358u,a,4,&r);
            W32(f-0x10cu,R32(f-0x10cu)+10u);
        }
    }
    /* 4249cc: triggers: new[](58n) at 4A9E28h; per runtime trigger T: words
     * +0, +2 (slot +18h), 16 bytes of region indices into f-0B7Ch, 20 bytes
     * at +0Ch and +20h, 10 bytes into f-120h (unused), word +34h (minus 1
     * afterwards), a word f-122h whose sign extension goes to +36h; word +0Ah
     * = 0; the leading non-negative region indices (at most 8) counted into
     * word +4 and, when there are any, new[](4 x count) at +6 with the
     * region pointers 10 x sx(index) + [4A9E20h]. */
    r.eax=stream;r.edx=R32(stream);wl_vread(m,g,stream,2,GVA(0x4a9e24u),r.edx,&r);
    if(R16(GVA(0x4a9e24u))) {
        r.ecx=sx(R16(GVA(0x4a9e24u)));r.eax=58u*r.ecx;
        a[0]=r.eax;ml_call(g,0x4774f0u,a,1,&r);
        r.ecx=a[0];                                 /* POP ECX */
        W32(GVA(0x4a9e28u),r.eax);edi=R32(GVA(0x4a9e28u));
        for(ebx=0;s16(ebx)<s16(R16(GVA(0x4a9e24u)));++ebx,edi+=0x3au) {
            W32(f-0x128u,edi);r.edx=stream;r.eax=R32(f-0x128u);r.ecx=R32(stream);wl_vread(m,g,stream,2,r.eax,r.ecx,&r);
            r.eax=edi+2u;W32(f-0x12cu,r.eax);r.ecx=stream;r.edx=R32(f-0x12cu);r.eax=R32(stream);
            wl_vread(m,g,stream,2,r.edx,r.eax,&r);
            r.edx=f-0xb7cu;r.ecx=stream;wl_read(g,stream,0x10u,r.edx,&r);
            r.eax=edi+0xcu;r.edx=stream;wl_read(g,stream,0x14u,r.eax,&r);
            r.ecx=edi+0x20u;r.eax=stream;wl_read(g,stream,0x14u,r.ecx,&r);
            r.edx=f-0x120u;r.ecx=stream;wl_read(g,stream,0xau,r.edx,&r);
            r.eax=edi+0x34u;W32(f-0x130u,r.eax);r.ecx=stream;r.edx=R32(f-0x130u);r.eax=R32(stream);
            wl_vread(m,g,stream,2,r.edx,r.eax,&r);
            r.edx=f-0x122u;r.ecx=stream;r.eax=R32(stream);wl_vread(m,g,stream,2,r.edx,r.eax,&r);
            r.edx=sx(R16(f-0x122u));W32(edi+0x36u,r.edx);
            r.eax=f-0xb7cu;
            W16(edi+0x34u,R16(edi+0x34u)-1u);
            W16(edi+0xau,0);
            esi=0;
            while(s16(R16(r.eax))>=0) { ++esi;r.eax+=2u;if((int32_t)esi>=8)break; }
            W16(edi+4u,esi);
            if((uint16_t)esi) {
                esi<<=2;a[0]=esi;ml_call(g,0x4774f0u,a,1,&r);
                r.ecx=a[0];                         /* POP ECX */
                W32(edi+6u,r.eax);
                esi=0;r.eax=f-0xb7cu;
                for(;;) {
                    r.edx=sx(R16(edi+4u));
                    if(!((int32_t)esi<(int32_t)r.edx))break;
                    r.ecx=sx(R16(r.eax));r.ecx+=r.ecx;r.edx=R32(edi+6u);r.ecx*=5u;r.ecx+=R32(GVA(0x4a9e20u));
                    W32(r.edx+4u*esi,r.ecx);
                    ++esi;r.eax+=2u;
                }
            }
        }
    }
    /* 424b54: timers: 49667c(0, 31h, n, 1, 427207h) at 4A9E38h; per 24-byte
     * record the first trigger (word +0, -1: 0) into f-134h, the ten trigger
     * pointers of words +4 .. +16h into f-0BBCh (negative: 0), then
     * timer_init(timer 49 x i + [4A9E38h], first, f-0BBCh, sx(word +2)). */
    r.eax=R32(stream);wl_vread(m,g,stream,2,GVA(0x4a9e34u),r.eax,&r);
    if(R16(GVA(0x4a9e34u))) {
        r.edx=sx(R16(GVA(0x4a9e34u)));
        a[0]=0;a[1]=0x31u;a[2]=r.edx;a[3]=1;a[4]=GVA(0x427207u);ml_call(g,0x49667cu,a,5,&r);
        W32(GVA(0x4a9e38u),r.eax);
        for(ebx=0;s16(ebx)<s16(R16(GVA(0x4a9e34u)));++ebx) {
            r.eax=f-0xb94u;r.edx=stream;wl_read(g,stream,0x18u,r.eax,&r);
            if(s16(R16(f-0xb94u))<0)r.ecx=0;
            else { r.eax=sx(R16(f-0xb94u));r.ecx=wl_trigger(m,g,r.eax); }
            W32(f-0x134u,r.ecx);
            r.eax=0;
            r.edx=f-0xbbcu;W32(f-0x14cu,r.edx);
            r.ecx=f-0xb90u;W32(f-0x148u,r.ecx);
            do {
                r.edx=R32(f-0x148u);
                if(s16(R16(r.edx))<0)r.ecx=0;
                else { r.edx=R32(f-0x148u);r.ecx=sx(R16(r.edx));r.edx=r.ecx;r.ecx=wl_trigger(m,g,r.edx); }
                r.edx=R32(f-0x14cu);W32(r.edx,r.ecx);
                ++r.eax;W32(f-0x14cu,R32(f-0x14cu)+4u);W32(f-0x148u,R32(f-0x148u)+2u);
            } while((int32_t)r.eax<10);
            r.edx=sx(ebx);
            r.eax=sx(R16(f-0xb92u));a[3]=r.eax;
            r.ecx=f-0xbbcu;a[2]=r.ecx;
            r.ecx=3u*r.edx;r.ecx<<=4;r.eax=R32(f-0x134u);r.ecx+=r.edx;a[1]=r.eax;r.ecx+=R32(GVA(0x4a9e38u));a[0]=r.ecx;
            ml_call(g,0x423104u,a,4,&r);
        }
    }
    /* 424c7e: the authored mechs (134 bytes; type +28h), flyers (92; +2Ch)
     * and buildings (52; +0) into the local type arrays, the orders (14)
     * skipped. */
    r.eax=f-0x56u;r.edx=stream;r.ecx=R32(stream);wl_vread(m,g,stream,2,r.eax,r.ecx,&r);
    if(R16(f-0x56u)) {
        esi=f-0x9e4u;
        for(ebx=0;s16(ebx)<s16(R16(f-0x56u));++ebx,esi+=2u) {
            r.eax=f-0xc44u;r.edx=stream;wl_read(g,stream,0x86u,r.eax,&r);
            r.ecx=lo(r.ecx,R16(f-0xc1cu));W16(esi,r.ecx);
        }
    }
    r.eax=f-0x58u;r.edx=stream;r.ecx=R32(stream);wl_vread(m,g,stream,2,r.eax,r.ecx,&r);
    if(R16(f-0x58u)) {
        esi=f-0xa48u;
        for(ebx=0;s16(ebx)<s16(R16(f-0x58u));++ebx,esi+=2u) {
            r.eax=f-0xca0u;r.edx=stream;wl_read(g,stream,0x5cu,r.eax,&r);
            r.ecx=lo(r.ecx,R16(f-0xc74u));W16(esi,r.ecx);
        }
    }
    r.eax=f-0x5au;r.edx=stream;r.ecx=R32(stream);wl_vread(m,g,stream,2,r.eax,r.ecx,&r);
    if(R16(f-0x5au)) {
        esi=f-0xb60u;
        for(ebx=0;s16(ebx)<s16(R16(f-0x5au));++ebx,esi+=2u) {
            r.eax=f-0xcd4u;r.edx=stream;wl_read(g,stream,0x34u,r.eax,&r);
            r.ecx=lo(r.ecx,R16(f-0xcd4u));W16(esi,r.ecx);
        }
    }
    r.eax=f-0x64u;r.edx=stream;r.ecx=R32(stream);wl_vread(m,g,stream,2,r.eax,r.ecx,&r);
    if(R16(f-0x64u))
        for(ebx=0;s16(ebx)<s16(R16(f-0x64u));++ebx) { r.eax=f-0xce4u;r.edx=stream;wl_read(g,stream,0xeu,r.eax,&r); }
    /* 424db2: the compaction counters; groups: the count f-62h; when
     * nonzero group_array(4A9E00h, count in CX) and the 156-byte records:
     * record 0 aside in f-0D80h, every other one sets its word +6Eh to 1 and
     * marks its 20 non-negative member indices +32h selected in the table
     * of its class +28h (0 mechs 4AA72Ah, 1 flyers 4AA856h, 2 buildings
     * 4AA8BAh with the byte of +6Eh at 4AA9D2h; the class word reread for
     * each test). */
    W16(f-0x5cu,0);W16(f-0x5eu,0);W16(f-0x60u,0);
    r.ecx=f-0x62u;r.eax=stream;r.edx=R32(stream);wl_vread(m,g,stream,2,r.ecx,r.edx,&r);
    if(R16(f-0x62u)) {
        r.ecx=lo(r.ecx,R16(f-0x62u));a[0]=GVA(0x4a9e00u);a[1]=r.ecx;ml_call(g,0x423d38u,a,2,&r);
        for(ebx=0;s16(ebx)<s16(R16(f-0x62u));++ebx) {
            if(!(uint16_t)ebx) { r.eax=f-0xd80u;r.edx=stream;wl_read(g,stream,0x9cu,r.eax,&r);continue; }
            r.ecx=f-0xe1cu;r.eax=stream;wl_read(g,stream,0x9cu,r.ecx,&r);
            W16(f-0xdaeu,1);
            r.eax=f-0xdeau;r.edx=0;
            do {
                if(s16(R16(r.eax))>=0) {
                    if(!R16(f-0xdf4u)) { r.ecx=sx(R16(r.eax));W8(r.ecx+GVA(0x4aa72au),1); }
                    else if(R16(f-0xdf4u)==1u) { r.ecx=sx(R16(r.eax));W8(r.ecx+GVA(0x4aa856u),1); }
                    else if(R16(f-0xdf4u)==2u) {
                        r.ecx=sx(R16(r.eax));W8(r.ecx+GVA(0x4aa8bau),1);
                        r.ecx=sx(R16(r.eax));esi=r.ecx+GVA(0x4aa9d2u);r.ecx=(r.ecx&0xffffff00u)|R8(f-0xdaeu);W8(esi,r.ecx);
                    }
                }
                ++r.edx;r.eax+=2u;
            } while(s16(r.edx)<0x14);
        }
    }
    /* 424eb7: the objectives (54) skipped; the string indices: new[](2n) at
     * 4A9E48h, read at slot +18h in one piece. */
    r.edx=stream;r.eax=R32(stream);wl_vread(m,g,stream,2,f-0x66u,r.eax,&r);
    if(R16(f-0x66u))
        for(ebx=0;s16(ebx)<s16(R16(f-0x66u));++ebx) { r.eax=f-0xe54u;r.edx=stream;wl_read(g,stream,0x36u,r.eax,&r); }
    r.ecx=stream;r.eax=R32(stream);wl_vread(m,g,stream,2,GVA(0x4a9e44u),r.eax,&r);
    if(R16(GVA(0x4a9e44u))) {
        r.edx=2u*sx(R16(GVA(0x4a9e44u)));a[0]=r.edx;ml_call(g,0x4774f0u,a,1,&r);
        W32(GVA(0x4a9e48u),r.eax);r.ecx=a[0];       /* POP ECX */
        r.eax=sx(R16(GVA(0x4a9e44u)));r.ecx=R32(GVA(0x4a9e48u));W32(f-0x138u,r.ecx);W32(f-0x13cu,r.eax);
        r.edx=R32(f-0x138u);{const uint32_t buffer=r.edx;r.eax=stream;r.ecx=R32(f-0x13cu);r.ecx+=r.ecx;
         r.edx=R32(stream);wl_vread(m,g,stream,r.ecx,buffer,r.edx,&r);}
    }
    /* 424f5e: compaction: consecutive remap indices (counter f-5Ch) for the
     * selected mechs, then group 0's non-negative slots get counter + slot
     * (the remap index of the k-th MEC actor); flyers (f-5Eh) and buildings
     * (f-60h) likewise. Without groups the slots are the never-read buffer
     * f-0D80h: stack residue in the original, zero here (outside the
     * contract; every launch set has groups). */
    W32(f-0x14cu,GVA(0x4aa78eu));esi=GVA(0x4aa72au);
    for(ebx=0;s16(ebx)<s16(R16(f-0x56u));++ebx) {
        if(R8(esi)) { r.eax=(r.eax&0xffffff00u)|R8(f-0x5cu);r.edx=R32(f-0x14cu);W8(r.edx,r.eax);W16(f-0x5cu,R16(f-0x5cu)+1u); }
        W32(f-0x14cu,R32(f-0x14cu)+1u);++esi;
    }
    r.eax=f-0xd4eu;r.edx=0;
    do {
        if(s16(R16(r.eax))>=0) {
            ebx=sx(R16(r.eax));r.ecx=(r.ecx&0xffffff00u)|(uint8_t)(R8(f-0x5cu)+(uint8_t)r.edx);W8(ebx+GVA(0x4aa78eu),r.ecx);
        }
        ++r.edx;r.eax+=2u;
    } while(s16(r.edx)<0x14);
    ebx=0;esi=GVA(0x4aa888u);W32(f-0x14cu,GVA(0x4aa856u));
    while(s16(ebx)<s16(R16(f-0x58u))) {
        r.eax=R32(f-0x14cu);
        if(R8(r.eax)) { r.edx=(r.edx&0xffffff00u)|R8(f-0x5eu);W8(esi,r.edx);W16(f-0x5eu,R16(f-0x5eu)+1u); }
        ++ebx;++esi;W32(f-0x14cu,R32(f-0x14cu)+1u);
    }
    W32(f-0x14cu,GVA(0x4aa946u));ebx=0;esi=GVA(0x4aa8bau);
    while(s16(ebx)<s16(R16(f-0x5au))) {
        if(R8(esi)) { r.eax=(r.eax&0xffffff00u)|R8(f-0x60u);r.edx=R32(f-0x14cu);W8(r.edx,r.eax);W16(f-0x60u,R16(f-0x60u)+1u); }
        ++ebx;W32(f-0x14cu,R32(f-0x14cu)+1u);++esi;
    }
    /* 42502f: the MEC actors appended: mech M + i selected (4AA7F2h,
     * 4AA72Ah), type MEC word +4, remap index from f-5Ch; M += N. */
    for(ebx=0;s16(ebx)<s16(R16(GVA(0x4aaa5eu)));++ebx) {
        r.eax=lo(r.eax,R16(f-0x56u));r.ecx=(r.ecx&0xffffff00u)|1u;r.eax=lo(r.eax,(uint16_t)(r.eax+ebx));
        r.edx=sx(r.eax);W8(r.edx+GVA(0x4aa7f2u),r.ecx);
        r.edx=sx(r.eax);W8(r.edx+GVA(0x4aa72au),r.ecx);
        r.edx=sx(ebx);r.ecx=(r.edx<<6)-r.edx;
        r.edx=R32(GVA(0x4aaa60u));r.ecx=lo(r.ecx,R16(r.edx+2u*r.ecx+4u));
        r.edx=sx(r.eax);r.eax=sx(r.eax);
        W16(f-0x9e4u+2u*r.edx,r.ecx);
        r.ecx=(r.ecx&0xffffff00u)|R8(f-0x5cu);W8(r.eax+GVA(0x4aa78eu),r.ecx);
        W16(f-0x5cu,R16(f-0x5cu)+1u);
    }
    r.eax=lo(r.eax,R16(GVA(0x4aaa5eu)));W16(f-0x56u,R16(f-0x56u)+(uint16_t)r.eax);
    /* 425092: the type selects: mech_type_select(type) per selected mech,
     * flyer_type_select(type) per selected flyer, 405ebc(type, the signed
     * byte of 4AA9D2h) per selected building. */
    ebx=0;esi=f-0x9e4u;W32(f-0x14cu,GVA(0x4aa72au));
    while(s16(ebx)<s16(R16(f-0x56u))) {
        r.eax=R32(f-0x14cu);
        if(R8(r.eax)) { r.edx=lo(r.edx,R16(esi));a[0]=r.edx;ml_call(g,0x41ff10u,a,1,&r);r.ecx=a[0]; }   /* POP ECX */
        ++ebx;esi+=2u;W32(f-0x14cu,R32(f-0x14cu)+1u);
    }
    ebx=0;r.eax=f-0xa48u;W32(f-0x14cu,r.eax);esi=GVA(0x4aa856u);
    while(s16(ebx)<s16(R16(f-0x58u))) {
        if(R8(esi)) {
            r.eax=R32(f-0x14cu);r.edx=lo(r.edx,R16(r.eax));a[0]=r.edx;ml_call(g,0x422e90u,a,1,&r);
            r.ecx=a[0];                             /* POP ECX */
        }
        W32(f-0x14cu,R32(f-0x14cu)+2u);++ebx;++esi;
    }
    W32(f-0x148u,GVA(0x4aa9d2u));W32(f-0x14cu,GVA(0x4aa8bau));ebx=0;esi=f-0xb60u;
    while(s16(ebx)<s16(R16(f-0x5au))) {
        r.eax=R32(f-0x14cu);
        if(R8(r.eax)) {
            r.edx=R32(f-0x148u);r.ecx=sx8(R8(r.edx));
            r.eax=lo(r.eax,R16(esi));a[0]=r.eax;a[1]=r.ecx;ml_call(g,0x405ebcu,a,2,&r);
        }
        ++ebx;W32(f-0x148u,R32(f-0x148u)+1u);esi+=2u;W32(f-0x14cu,R32(f-0x14cu)+1u);
    }
    /* 425165: the MECH pool (new(12h), pool_create(H, selected mechs, 36Ah)
     * inside context 38h) at 4A9B7Ah and its removal list (new(8): word +6 =
     * 25, new[](100), count 0; context 44h) at 4A9B7Eh; the FLYER pool
     * (291h; contexts 5Ch/50h) at 4A9DB9h and list (context 68h) at
     * 4A9DBDh; a null header or list skips its set-up and is stored. */
    r.edx=lo(r.edx,R16(f-0x5cu));W16(f-0x13eu,r.edx);
    a[0]=0x12u;ml_call(g,0x47749cu,a,1,&r);r.ecx=a[0];   /* POP ECX */
    W32(f-0x28u,r.eax);
    if(r.eax) {
        W16(context,0x38u);
        r.edx=lo(r.edx,R16(f-0x13eu));r.ecx=R32(f-0x28u);a[0]=r.ecx;a[1]=r.edx;a[2]=0x36au;ml_call(g,0x471ad8u,a,3,&r);
        W16(context,0x2cu);
    }
    r.eax=R32(f-0x28u);
    W32(GVA(0x4a9b7au),r.eax);
    a[0]=8;ml_call(g,0x47749cu,a,1,&r);r.ecx=a[0];       /* POP ECX */
    W32(f-0x2cu,r.eax);
    if(r.eax) {
        W16(context,0x44u);
        r.edx=R32(f-0x2cu);r.ecx=lo(r.ecx,0x19u);W16(r.edx+6u,r.ecx);
        r.eax=sx(r.ecx)<<2;a[0]=r.eax;ml_call(g,0x4774f0u,a,1,&r);
        r.ecx=a[0];                                 /* POP ECX */
        r.edx=R32(f-0x2cu);W32(r.edx,r.eax);
        r.eax=R32(f-0x2cu);W16(r.eax+4u,0);
        W16(context,0x2cu);
    }
    r.edx=R32(f-0x2cu);
    r.eax=lo(r.eax,R16(f-0x5eu));
    W32(GVA(0x4a9b7eu),r.edx);
    W16(f-0x140u,r.eax);
    a[0]=0x12u;ml_call(g,0x47749cu,a,1,&r);r.ecx=a[0];   /* POP ECX */
    W32(f-0x30u,r.eax);
    if(r.eax) {
        W16(context,0x5cu);
        r.edx=lo(r.edx,R16(f-0x140u));r.ecx=R32(f-0x30u);a[0]=r.ecx;a[1]=r.edx;a[2]=0x291u;ml_call(g,0x471ad8u,a,3,&r);
        W16(context,0x50u);
    }
    r.eax=R32(f-0x30u);
    W32(GVA(0x4a9db9u),r.eax);
    a[0]=8;ml_call(g,0x47749cu,a,1,&r);r.ecx=a[0];       /* POP ECX */
    W32(f-0x34u,r.eax);
    if(r.eax) {
        W16(context,0x68u);
        r.edx=R32(f-0x34u);r.ecx=lo(r.ecx,0x19u);W16(r.edx+6u,r.ecx);
        r.eax=sx(r.ecx)<<2;a[0]=r.eax;ml_call(g,0x4774f0u,a,1,&r);
        r.ecx=a[0];                                 /* POP ECX */
        r.edx=R32(f-0x34u);W32(r.edx,r.eax);
        r.eax=R32(f-0x34u);W16(r.eax+4u,0);
        W16(context,0x50u);
    }
    r.edx=R32(f-0x34u);
    /* 425295: base_pools(buildings); actor_list_init(4A9AF8h, mechs +
     * flyers + buildings); the slot tables; mech_types, flyer_types;
     * zone_load(header word 1, header word 0). */
    r.eax=lo(r.eax,R16(f-0x60u));
    W32(GVA(0x4a9dbdu),r.edx);
    a[0]=r.eax;ml_call(g,0x405df4u,a,1,&r);
    r.ecx=a[0];                                     /* POP ECX */
    r.ecx=lo(r.ecx,R16(f-0x5cu));r.ecx=lo(r.ecx,(uint16_t)(r.ecx+R16(f-0x5eu)));r.ecx=lo(r.ecx,(uint16_t)(r.ecx+R16(f-0x60u)));
    W16(f-0x142u,r.ecx);
    r.eax=lo(r.eax,R16(f-0x142u));
    a[0]=GVA(0x4a9af8u);a[1]=r.eax;ml_call(g,0x411df8u,a,2,&r);
    ml_call(g,0x40feccu,NULL,0,&r);
    ml_call(g,0x40d218u,NULL,0,&r);
    ml_call(g,0x40cbecu,NULL,0,&r);
    ml_call(g,0x42049cu,NULL,0,&r);
    ml_call(g,0x422fb4u,NULL,0,&r);
    r.edx=lo(r.edx,R16(GVA(0x4a9e4eu)));r.ecx=lo(r.ecx,R16(GVA(0x4a9e50u)));
    a[0]=r.ecx;a[1]=r.edx;ml_call(g,0x42789cu,a,2,&r);
    /* 4252ff: the result pairs (word 4A9E6Ah = 0, [4A9E6Ch] = new[](200)),
     * word 4AA728h = header word 4; the script stream closed (no delete);
     * ECX = the record's saved FS:0 head. */
    W16(GVA(0x4a9e6au),0);
    a[0]=0xc8u;ml_call(g,0x4774f0u,a,1,&r);
    W32(GVA(0x4a9e6cu),r.eax);r.ecx=a[0];           /* POP ECX */
    r.eax=lo(r.eax,R16(GVA(0x4a9e56u)));r.edx=stream;
    W16(GVA(0x4aa728u),r.eax);
    a[0]=r.edx;a[1]=2;ml_call(g,0x47ad78u,a,2,&r);
    r.ecx=R32(f-0x54u);
    ecx_out(ecx,r.ecx);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x411df8. */
uint64_t es2_actor_list_init_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t list,
    uint32_t count,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[1];
    r.eax=sx(count)<<2;a[0]=r.eax;ml_call(g,0x4774f0u,a,1,&r);
    r.ecx=a[0];                                     /* POP ECX */
    W32(list,r.eax);W16(list+4u,count);
    ecx_out(ecx,r.ecx);
    return pair(r.eax,r.edx);
}
/* The three slot tables: new[](unit x sx(word)) into the table pointer;
 * `always` for 40fecc, which has no zero test. */
static uint64_t wl_slot_table(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t word,uint32_t unit,
    uint32_t table,int always,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[1];
    const uint16_t n=R16(GVA(word));
    if(always||n) {
        r.eax=sx(n);r.edx=unit*r.eax;
        a[0]=r.edx;ml_call(g,0x4774f0u,a,1,&r);
        r.ecx=a[0];                                 /* POP ECX */
        W32(GVA(table),r.eax);
    }
    ecx_out(ecx,r.ecx);
    return pair(r.eax,r.edx);
}
/* Original: DBSIM.EXE 0x40fecc. */
uint64_t es2_gun_slot_table_registers(const es2_guest_memory *m,const es2_weapon_graph *g,const uint32_t entry[3],
    uint32_t *ecx) {
    return wl_slot_table(m,g,0x4a98e8u,6,0x4a98e4u,1,entry,ecx);
}
/* Original: DBSIM.EXE 0x40d218. */
uint64_t es2_dmg_slot_table_registers(const es2_guest_memory *m,const es2_weapon_graph *g,const uint32_t entry[3],
    uint32_t *ecx) {
    return wl_slot_table(m,g,0x498856u,12,0x4a9828u,0,entry,ecx);
}
/* Original: DBSIM.EXE 0x40cbec. */
uint64_t es2_col_slot_table_registers(const es2_guest_memory *m,const es2_weapon_graph *g,const uint32_t entry[3],
    uint32_t *ecx) {
    return wl_slot_table(m,g,0x4987dcu,6,0x4a9824u,0,entry,ecx);
}
