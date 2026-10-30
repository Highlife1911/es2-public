#include "es2/core/weapon_helpers.h"
#include "fixed.h"
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(uint8_t *)m->host(m->context,(a)))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static uint32_t low_word(uint32_t high,uint32_t low) { return (high&0xffff0000u)|(uint16_t)low; }
static uint32_t sword(uint32_t x) { return (uint32_t)fx_s16(x); }
/* A direct call names the original VA; a virtual call passes the target it
 * read from the receiver's vtable together with the slot offset. */
static void helper_call(const es2_weapon_graph *g,uint32_t function,unsigned slot,
    const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,function,slot);
    g->call(g->context,slot?function:g->image+function-0x400000u,a,n,r);
}

/* Original: DBSIM.EXE 0x4107e4 */
uint64_t es2_group_fire_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t group,uint32_t value,uint32_t index,uint32_t owner,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t visited=g->scratch,a[3];
    uint16_t any=0,saved=(uint16_t)value,current=(uint16_t)value,sel=0,k=0,free_shot;
    /* Step 1: the free-shot flag of the owner. */
    uint8_t mode=R8(owner+0xa3u);
    r.edx=(r.edx&0xffffff00u)|mode;r.eax=(uint32_t)(mode^0x80u)-0x80u;r.ecx=r.eax;
    if(!(uint16_t)r.eax)r.ecx=0;
    else {
        r.eax=R16(g->image+0xa9e52u)==0;
        r.edx=r.eax&&R16(g->image+0xa9e58u)==1;
        r.ecx=(uint16_t)r.edx!=0;
    }
    free_shot=(uint16_t)r.ecx;
    /* Step 2: the CW3220 memset of the ten visited words returns its target. */
    for(unsigned i=0;i<20;++i)W8(visited+i,0);
    r.eax=visited;
    r.edx=sword(index);r.ecx=visited+2u*r.edx;
    uint32_t preferred=r.ecx;
    for(;;) {
        r.edx=low_word(r.edx,k);
        if(fx_s16(k)>=fx_s16(R16(group+8u)))break;
        uint32_t w=0;int direct=0;
        if(fx_s16(index)>-1) { r.eax=preferred;direct=!R16(preferred); }
        if(direct) {
            r.edx=low_word(r.edx,index);sel=(uint16_t)index;
            r.ecx=sword(sel);r.eax=R32(group+4u);w=R32(r.eax+4u*r.ecx);
        } else {
            uint32_t j=0,cursor=visited;
            r.eax=visited;
            while(fx_s16(j)<fx_s16(R16(group+8u))) {
                r.edx=cursor;
                if(!R16(r.edx)) {
                    r.ecx=sword(j);r.eax=R32(group+4u);
                    if(R32(r.eax+4u*r.ecx)) {
                        int take=!w;
                        if(!take) {
                            r.edx=sword(j);r.ecx=R32(group+4u);r.eax=R32(r.ecx+4u*r.edx);
                            uint32_t candidate=r.eax;r.edx=R32(candidate);
                            a[0]=candidate;helper_call(g,R32(r.edx+0x48u),0x48,a,1,&r);
                            uint32_t first=r.eax;
                            r.ecx=R32(w);a[0]=w;a[1]=first;helper_call(g,R32(r.ecx+0x48u),0x48,a,2,&r);
                            r.ecx=w;r.edx=first;
                            take=fx_s16(first)>fx_s16(r.eax);
                        }
                        if(take) { r.eax=sword(j);r.ecx=R32(group+4u);w=R32(r.ecx+4u*r.eax);sel=(uint16_t)j; }
                    }
                }
                ++j;cursor+=2u;
            }
        }
        if(!w)break;
        r.eax=sword(sel);W16(visited+2u*r.eax,1);
        r.edx=low_word(r.edx,any);r.ecx=low_word(r.ecx,current);
        a[0]=w;a[1]=r.ecx;a[2]=r.edx;r.eax=R32(w);helper_call(g,R32(r.eax+0x34u),0x34,a,3,&r);
        current=(uint16_t)r.eax;
        a[0]=w;helper_call(g,0x40ede8u,0,a,1,&r);
        r.ecx=w;
        if((uint16_t)r.eax) {
            r.edx=low_word(r.edx,free_shot);r.ecx=owner;
            a[0]=w;a[1]=owner;a[2]=r.edx;r.eax=R32(w);helper_call(g,R32(r.eax+0x28u),0x28,a,3,&r);
        }
        r.eax=w+0x43u;
        if(R8(r.eax))any=1;
        ++k;
    }
    if(free_shot)current=saved;
    return pair(low_word(r.eax,current),r.edx);
}

/* Original: DBSIM.EXE 0x40ede8 */
uint64_t es2_weapon_ready_registers(const es2_guest_memory *m,uint32_t w,const uint32_t entry[3]) {
    if(R16(w+0x31u))return pair(0,entry[1]);
    return pair(R8(w+0x4du)!=0,w+0x43u);
}

/* Original: DBSIM.EXE 0x40db2c */
uint64_t es2_part_condition_registers(const es2_guest_memory *m,uint32_t x) {
    uint32_t sum=0,maximum=0,current=R32(x+4u);
    for(uint32_t k=0;fx_s16(k)<fx_s16(R16(x+0x18u));++k,current+=2u) {
        uint32_t limit=sword(R16(R32(R32(x+0xcu))+18u*sword(k)));
        sum+=fx_s16(R16(current))<0?limit:sword(R16(current));
        maximum+=limit;
    }
    current=R32(x);
    uint32_t limits=R32(R32(x+0xcu)+4u);
    for(uint32_t k=0;fx_s16(k)<fx_s16(R16(x+0x1au));++k,current+=2u,limits+=2u) {
        uint32_t limit=sword(R16(limits));
        sum+=fx_s16(R16(current))<0?limit:sword(R16(current));
        maximum+=limit;
    }
    /* Original IDIV: nonzero divisor, quotient within 32 bits. */
    int64_t dividend=fx_s32(sum<<8),divisor=fx_s32(maximum);
    return pair((uint32_t)(dividend/divisor),(uint32_t)(dividend%divisor));
}

/* Original: DBSIM.EXE 0x40c524 */
uint64_t es2_sphere_hits_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t region,uint32_t offset,const uint32_t entry[3]) {
    es2_weapon_registers r={region,entry[1],entry[2]};
    uint32_t out=g->scratch,point=out+12u,sphere=R32(region+2u),a[3];
    uint16_t hit=0;
    for(uint32_t i=0;;++i,sphere+=8u) {
        r.ecx=region;
        if(fx_s16(i)>=fx_s16(R16(region)))break;
        r.eax=sphere;
        uint16_t x=R16(sphere),y=R16(sphere+2u),z=R16(sphere+4u);
        r.edx=low_word(r.edx,x);r.ecx=low_word(r.ecx,y);r.eax=low_word(r.eax,z);
        if(offset) {
            uint16_t dz=R16(offset+4u),dx=R16(offset),dy=R16(offset+2u);
            r.edx=low_word(r.edx,dz);r.ecx=low_word(r.ecx,dx);r.eax=low_word(offset,dy);
            z=(uint16_t)(z+dz);x=(uint16_t)(x+dx);y=(uint16_t)(y+dy);
        }
        W16(point,x);W16(point+2u,y);W16(point+4u,z);
        r.eax=R32(g->image+0xa980cu);r.edx=out;r.ecx=point;
        a[0]=point;a[1]=r.eax;a[2]=out;helper_call(g,0x4801d4u,0,a,3,&r);
        r.eax=low_word(r.eax,R16(sphere+6u));r.edx=out;
        a[0]=out;a[1]=r.eax;a[2]=1;helper_call(g,0x40c428u,0,a,3,&r);
        if((uint16_t)r.eax)hit=1;
    }
    return pair(hit?R32(g->image+0xa9810u)+1u:0,r.edx);
}

/* Original: DBSIM.EXE 0x40c428 */
uint64_t es2_sphere_test_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t centre,uint32_t radius,uint32_t mode,const uint32_t entry[3]) {
    es2_weapon_registers r={R32(centre+4u),entry[1],entry[2]};
    if(r.eax>=R32(g->image+0xa9810u))return pair(0,r.edx);
    uint32_t a[2]={R32(centre),R32(centre+8u)};
    helper_call(g,0x47de4cu,0,a,2,&r);
    uint32_t distance=r.eax;
    r.edx=low_word(r.edx,R16(g->image+0xa9814u)+radius);
    if(fx_s32(distance)>=0x7fff)return pair(0,r.edx);
    if((uint16_t)(distance+r.edx)>=(uint16_t)(r.edx+r.edx))return pair(0,r.edx);
    if((uint16_t)mode) {
        distance=sword(r.edx)-distance;
        uint32_t y=R32(centre+4u);
        r.edx=fx_s32(distance)<fx_s32(y)?y-distance:0;
        W32(g->image+0xa9810u,r.edx);
    }
    return pair(low_word(distance,1),r.edx);
}
