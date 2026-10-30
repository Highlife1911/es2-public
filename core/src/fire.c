#include "es2/core/fire.h"
#include "fixed.h"
/* FIRE pool helpers (docs/reverse-engineering/main_loop.md, section "fire
 * allocation 46b430, fire constructor 46b48c, fire release 46b62c"). r holds
 * what the original's EAX/EDX/ECX hold at the next game call. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define VA(x) (g->image+(uint32_t)(x)-0x400000u)
static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
/* Replace the low word of a register (MOV r16). */
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(word&0xffffu); }
/* Calls name the original VA (observer key); the bridge gets the address
 * in this image. */
static void fr_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,VA(va),a,n,r);
}

/* Original: DBSIM.EXE 0x46b430 */
uint64_t es2_fire_alloc_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    const uint32_t entry[3],uint32_t *ecx) {
    /* 46b439: pool_alloc([6b4f30h]); POP ECX leaves the pool. */
    uint32_t a[2]={R32(VA(0x6b4f30u))};
    es2_weapon_registers r={a[0],entry[1],entry[2]};
    fr_call(g,0x471b30u,a,1,&r);
    r.ecx=a[0];
    uint32_t best=r.eax;
    if(best) { /* 46b449: the uninitialized cursor goes to ECX */
        *ecx=entry[2];
        return pair(best,r.edx);
    }
    /* 46b44b: walk the pool oldest first for the smallest lifetime. */
    int32_t least=0x2710;
    uint32_t cursor=0;
    for(;;) {
        a[0]=R32(VA(0x6b4f30u));a[1]=cursor;
        r.eax=a[0];
        fr_call(g,0x471c70u,a,2,&r);
        cursor=r.eax;
        if(!cursor)break;
        const int32_t life=fx_s16(R16(cursor+0x59u));
        if(life<least) { least=life;best=cursor; }
    }
    *ecx=0;
    return pair(best,r.edx);
}

/* Original: DBSIM.EXE 0x46b48c */
uint64_t es2_fire_create_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t f,uint32_t owner,uint32_t node,uint32_t offset,uint32_t kind,const uint32_t entry[3],uint32_t *ecx) {
    (void)entry;
    /* 46b492: object_init(F, table[sx16(k)]). */
    const uint32_t table=R32(VA(0x6b4f28u));
    const uint32_t index=(uint32_t)fx_s16(kind);
    uint32_t a[3]={f,R32(table+4u*index)};
    es2_weapon_registers r={index,table,a[1]};
    fr_call(g,0x402188u,a,2,&r);
    W32(f,VA(0x4a0ba0u));
    const uint32_t flag=R16(VA(0x4a9e4eu))==4u;
    const uint32_t k=kind&0xffffu;
    if(!flag||k==3u||k==1u) {
        /* 46b4d5: memset(F+41h, 0, 8) with object_init's EDX/ECX. */
        a[0]=f+0x41u;a[1]=0;a[2]=8;
        r.eax=a[0];
        fr_call(g,0x49681au,a,3,&r);
        r.edx=owner;W32(f+0x4au,r.edx);
        r.ecx=lo(r.ecx,node);W16(f+0x4eu,r.ecx);
        if(offset) { /* 46b4fb */
            r.eax=offset;
            r.edx=R32(offset);W32(f+0x50u,r.edx);
            r.edx=lo(r.edx,R16(offset+4u));W16(f+0x54u,r.edx);
        } else { /* 46b50d */
            r.eax=f+0x41u;
            *(uint8_t *)m->host(m->context,f+0x41u)=1u;
        }
        W16(f+4u,4u);
        W16(f+0x57u,0x40u);
        r.edx=lo(r.edx,5u);
        if(!flag)r.edx+=0x19u;
        W16(f+0x59u,r.edx);
        if(!R32(VA(0x6b4f38u))) { /* 46b536: sound 33h */
            a[0]=0x33u;
            fr_call(g,0x462830u,a,1,&r);
            r.ecx=0x33u;
        }
        W32(VA(0x6b4f38u),R32(VA(0x6b4f38u))+1u);
        a[0]=f;
        fr_call(g,0x46b678u,a,1,&r);
    } else { /* 46b54d: discard */
        W32(VA(0x6b4f38u),R32(VA(0x6b4f38u))+1u);
        a[0]=R32(VA(0x6b4f34u));a[1]=f;
        r.eax=f;r.edx=a[0];
        fr_call(g,0x478628u,a,2,&r);
    }
    /* 46b565: the construction count. */
    const uint32_t counter=R32(VA(0x6cbee4u));
    W32(counter,R32(counter)+1u);
    *ecx=counter;
    return pair(f,r.edx);
}

/* Original: DBSIM.EXE 0x46b62c */
uint64_t es2_fire_release_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t owner,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={0,entry[1],entry[2]};
    uint32_t a[2],cursor=0;
    for(;;) {
        a[0]=R32(VA(0x6b4f30u));a[1]=cursor;
        r.ecx=a[0];
        fr_call(g,0x471c70u,a,2,&r);
        cursor=r.eax;
        if(!cursor)break;
        if(owner==R32(cursor+0x4au)) { /* 46b642 */
            a[0]=R32(VA(0x6b4f34u));a[1]=cursor;
            r.eax=cursor;r.edx=a[0];
            fr_call(g,0x478628u,a,2,&r);
        }
    }
    *ecx=0;
    return pair(0,r.edx);
}

/* The object base methods of slots +04 and +10. */
/* Original: DBSIM.EXE 0x46b905 */
uint64_t es2_object_position_registers(uint32_t object,const uint32_t entry[3],uint32_t *ecx) {
    *ecx=entry[2];
    return pair(object+0x26u,entry[1]);
}

/* Original: DBSIM.EXE 0x46b910 */
uint64_t es2_shape_word_registers(const es2_guest_memory *m,uint32_t object,const uint32_t entry[3],uint32_t *ecx) {
    uint32_t edx=entry[1],c=0;
    const uint32_t s=R32(object+0x34u);
    if(s) { edx=s;c=R32(s+4u); }   /* 46b91c */
    *ecx=c;
    if(!c)return pair(0,edx);       /* 46b928 */
    /* 46b92a: [O+34h] and its +4 read again (46b92e cannot jump). */
    const uint32_t again=R32(object+0x34u);
    const uint32_t eax=again?again:object;
    edx=again?R32(again+4u):0u;
    return pair(lo(eax,R16(edx+8u)),edx);
}
