#include "es2/core/effects.h"
#include "fixed.h"
#include <stddef.h>

#define R16(a) m->read16(m->context, (a))
#define R32(a) m->read32(m->context, (a))
#define W16(a, v) m->write16(m->context, (a), (uint16_t)(v))
#define W32(a, v) m->write32(m->context, (a), (v))
static uint8_t byte(const es2_guest_memory *m, uint32_t a) {
    return *(const uint8_t *)m->host(m->context, a);
}
static void put_byte(const es2_guest_memory *m, uint32_t a, uint8_t v) {
    *(uint8_t *)m->host(m->context, a) = v;
}
static uint32_t signed_byte(uint8_t v) { return v < 128 ? v : 0xffffff00u | v; }
static uint32_t word(uint32_t v, uint16_t w) { return (v & 0xffff0000u) | w; }
static uint64_t pair(uint32_t eax, uint32_t edx) { return ((uint64_t)edx << 32) | eax; }
static uint64_t invoke(const es2_effects_context *g, uint32_t f, uint32_t slot,
                       const uint32_t *args, unsigned count) {
    if (g->observe) g->observe(g->observer_context, slot ? ES2_EFFECTS_SLOT : ES2_EFFECTS_CALL, slot ? slot : f);
    return g->call(g->call_context, f, args, count, 0);
}

uint64_t es2_ground_effects_registers(const es2_guest_memory *m, uint32_t a,
                                     const es2_effects_context *g, uint32_t eax, uint32_t edx) {
    if (!R32(a + 0x22cu)) return pair(eax, edx);
    uint32_t config = R32(a + 0x1f2u), selector = config + 0x77u, category = config + 0x72u;
    uint16_t animation = R16(R32(a + 0x22cu) + 0x16u), index = 0;
    uint32_t latch = a + 0x2b4u;
    eax = latch;
    edx = category;
    for (;;) {
        eax = word(eax, index);
        if (fx_s16(index) >= fx_s16(R16(a + 0x23cu))) return pair(eax, edx);
        edx = (uint32_t)fx_s16(index);
        if (R32(R32(a + 0x238u) + 4u * edx)) {
            uint32_t manager = R32(a + 0x34u), list = R32(manager + 4u);
            uint32_t lookup[2] = {list, word(selector, (uint16_t)signed_byte(byte(m, selector)))};
            uint32_t entry = (uint32_t)invoke(g, R32(R32(list) + 0x20u), 0x20u, lookup, 2);
            uint32_t record = g->default_part;
            if (entry && fx_s16(R16(entry + 4u)) > -1)
                record = R32(manager + 0x16u) + ((uint32_t)fx_s16(R16(entry + 4u)) << 5);
            uint32_t point = record + 0x14u;
            if (!R16(a + 0x32u)) {
                uint32_t args[2] = {a + 0x0cu, a + 0x12u};
                (void)invoke(g, g->euler, 0, args, 2);
                W16(a + 0x32u, 1);
            }
            uint32_t transform[3] = {point, a + 0x12u, g->point};
            (void)invoke(g, g->transform, 0, transform, 3);
            uint32_t attached = R32(R32(a + 0x238u) + 4u * (uint32_t)fx_s16(index));
            W32(attached + 0x2eu, R32(g->point + 8u));
            W32(attached + 0x26u, R32(g->point));
            W32(attached + 0x2au, R32(g->point + 4u));
            uint16_t heading = R16(a + 0x10u);
            attached = R32(R32(a + 0x238u) + 4u * (uint32_t)fx_s16(index));
            W16(attached + 0x10u, heading);
            W16(attached + 0x32u, 0);
            uint32_t position[3] = {R32(a + 0x26u), R32(a + 0x2au), R32(a + 0x2eu)};
            uint32_t reference = R32(g->reference_position);
            position[2] -= R32(reference + 0x0cu);
            position[0] -= R32(reference + 4u);
            position[1] -= R32(reference + 8u);
            uint32_t distance = (uint32_t)invoke(g, g->distance, 0, position, 3);
            edx = fx_s32(distance) < 15000;
            eax = a + 0x92u;
            if (byte(m, a + 0xa3u) || edx) {
                if (animation == 0xffffu) animation = R16(R32(a + 0x22cu) + 4u);
                eax = UINT32_MAX;
                if (animation == R16(config + 0x46u)) {
                    if (signed_byte(byte(m, category)) == 2) latch = a + 0x2b8u;
                } else if (!byte(m, category)) {
                    unsigned k;
                    int forward = 1;
                    if (animation == R16(config + 0x0eu) || animation == R16(config + 0x7cu) ||
                        animation == R16(config + 0x12u) || animation == R16(config + 0x14u)) {
                        forward = fx_s16(R16(a + 0x2a0u)) > 0;
                        k = forward ? 0u : 1u;
                    } else if (animation == R16(config + 0x10u)) k = 2;
                    else { ++latch; goto next; }
                    uint32_t coordinate = R32(point + 4u);
                    eax = (uint32_t)fx_s16(R16(config + 0xacu + 2u * k));
                    if (forward ? fx_s32(eax) < fx_s32(coordinate) : fx_s32(eax) > fx_s32(coordinate))
                        put_byte(m, latch, 1);
                    eax = word(eax, 0x1du);
                    if (byte(m, latch)) {
                        int32_t fire = fx_s16(R16(config + 0x98u + 2u * k));
                        if (forward ? fire > fx_s32(coordinate) : fire < fx_s32(coordinate)) {
                            if (edx) {
                                uint32_t args[2] = {0x1du, a + 0x26u};
                                (void)invoke(g, g->sound, 0, args, 2);
                            }
                            eax = edx = a + 0x92u;
                            if (byte(m, a + 0xa3u)) {
                                uint32_t registers[3] = {eax, edx, 0};
                                if (g->observe) g->observe(g->observer_context, ES2_EFFECTS_CALL, g->feedback);
                                uint64_t result = g->call(g->call_context, g->feedback, 0, 0, registers);
                                eax = (uint32_t)result;
                                edx = (uint32_t)(result >> 32);
                            }
                            put_byte(m, latch, 0);
                        }
                    }
                }
                ++latch;
            }
        }
next:
        index = (uint16_t)(index + 1u);
        ++selector;
        ++category;
    }
}

/* ---- effect creation (weapons.md, section "effect creation")
 * ------------------ */
#include "es2/core/effect_create.h"
#define EC_VA(g,x) ((g)->image+(x)-0x400000u)
/* Direct calls name the original VA; the observer gets the VA, the bridge
 * the address in this image. Registers are updated in place. */
static void ec_call(const es2_weapon_graph *g,uint32_t function,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,function,0);
    g->call(g->context,EC_VA(g,function),a,n,r);
}
/* The runtime's construction count: the dword [0x6cbee4] points to. */
static uint32_t ec_count(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t delta) {
    uint32_t counter=R32(EC_VA(g,0x6cbee4u));
    W32(counter,R32(counter)+delta);
    return counter;
}
/* 0x407b20: explosion type entry, [4a9628h] + 40 * signed byte O+41. */
static uint32_t ec_type(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t o) {
    return R32(EC_VA(g,0x4a9628u))+40u*signed_byte(byte(m,o+0x41u));
}
/* Copies the 12-byte point P in the original's order (+8, +0, +4); ECX and
 * EDX keep the last two values read, as the original's registers do. */
static void ec_copy_point(const es2_guest_memory *m,uint32_t out,uint32_t p,es2_weapon_registers *r) {
    r->ecx=R32(p+8u);W32(out+8u,r->ecx);
    r->ecx=R32(p);W32(out,r->ecx);
    r->edx=R32(p+4u);W32(out+4u,r->edx);
}

/* Original: DBSIM.EXE 0x471b30 */
uint64_t es2_pool_alloc_registers(const es2_guest_memory *m,uint32_t h,const uint32_t entry[3]) {
    if(!R16(h))return pair(0,entry[1]);
    uint32_t result=R32(h+0xau)+8u;
    W16(h,R16(h)-1u);
    W16(h+2u,R16(h+2u)+1u);
    uint32_t node=R32(h+0xau),next=R32(node);
    W32(h+0xau,next);
    if(next)W32(next+4u,0);
    uint32_t head=R32(h+0xeu);
    if(head) {
        W32(node,head);
        W32(R32(h+0xeu)+4u,node);
    } else W32(node,0);
    W32(h+0xeu,node);
    W32(node+4u,0);
    return pair(result,node);
}

/* Original: DBSIM.EXE 0x402188. The record lies at frame-24h, its context
 * word at frame-14h, the allocation slot at frame-4. */
uint64_t es2_object_init_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t o,uint32_t shape,const uint32_t entry[3]) {
    uint32_t f=g->scratch+208u,a[3];
    es2_weapon_registers r={EC_VA(g,0x497115u),entry[1],entry[2]};
    ec_call(g,0x49630cu,NULL,0,&r);
    W32(o,EC_VA(g,0x4a14a4u));
    (void)ec_count(m,g,1);
    W32(o+0x2eu,0);W32(o+0x26u,0);W32(o+0x2au,0);W32(o+0xcu,0);W16(o+0x10u,0);W16(o+0x32u,0);
    W32(o,EC_VA(g,0x497289u));
    r.eax=ec_count(m,g,1);r.edx=0;r.ecx=0;
    W32(o,EC_VA(g,0x4a0b80u));
    if(shape) {
        a[0]=12;ec_call(g,0x47749cu,a,1,&r);
        r.ecx=12;uint32_t allocated=r.eax;W32(f-4u,allocated);
        if(allocated) {
            W16(f-0x14u,0x14);
            a[0]=allocated;a[1]=shape;r.eax=allocated;r.edx=shape;r.ecx=allocated;
            ec_call(g,0x490b64u,a,2,&r);
            r.eax=ec_count(m,g,(uint32_t)-2);
            W16(f-0x14u,8);
        }
        r.edx=allocated;
    }
    W32(o+0x34u,r.edx);
    a[0]=o+0x38u;a[1]=0;a[2]=8;r.eax=o+0x38u;
    ec_call(g,0x49681au,a,3,&r);
    (void)ec_count(m,g,1);
    return pair(o,r.edx);
}

/* Original: DBSIM.EXE 0x407604 */
uint64_t es2_light_create_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t l,uint32_t position,uint32_t level) {
    uint32_t a[3];
    W32(l,EC_VA(g,0x4a14a4u));
    uint32_t counter=ec_count(m,g,1);
    W32(l,EC_VA(g,0x497b8au));
    uint32_t table=R32(EC_VA(g,0x4a9608u));
    es2_weapon_registers r={counter,position,table};
    a[0]=table;a[1]=position;ec_call(g,0x406f38u,a,2,&r);
    uint32_t index=r.eax;
    W16(l+0xcu,index);
    r.eax=word(index,(uint16_t)level);r.edx=R32(EC_VA(g,0x4a9608u));
    a[0]=r.edx;a[1]=index;a[2]=r.eax;ec_call(g,0x407048u,a,3,&r);
    (void)ec_count(m,g,1);
    return pair(l,r.edx);
}

/* Original: DBSIM.EXE 0x407f1c. The record lies at frame-28h, its context
 * word at frame-18h, the linked-object slot at frame-4, the light slot at
 * frame-8. */
uint64_t es2_explosion_create_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t o,uint32_t type,uint32_t p,uint32_t value,uint32_t sound,const uint32_t entry[3]) {
    uint32_t f=g->scratch+208u,a[5];
    es2_weapon_registers r={EC_VA(g,0x497c68u),entry[1],entry[2]};
    ec_call(g,0x49630cu,NULL,0,&r);
    /* Shape of the 16-bit type's entry: [[4a9630h] + 4 * word T(t)+0]. */
    r.edx=(uint32_t)fx_s16((uint16_t)type);r.ecx=5u*r.edx;
    r.edx=(uint32_t)fx_s16(R16(R32(EC_VA(g,0x4a9628u))+8u*r.ecx));
    r.ecx=R32(EC_VA(g,0x4a9630u));r.eax=R32(r.ecx+4u*r.edx);
    a[0]=o;a[1]=r.eax;ec_call(g,0x402188u,a,2,&r);
    W32(o,EC_VA(g,0x497d4cu));
    W32(o+0x57u,value);
    put_byte(m,o+0x41u,(uint8_t)type);
    uint32_t t=ec_type(m,g,o);
    W16(o+4u,R16(t+0x26u)?8u:2u);
    W16(o+0x4bu,R16(t+2u));
    uint32_t animations=R32(EC_VA(g,0x4a963cu));
    uint32_t w=(uint32_t)fx_s16(R16(animations+4u*(uint32_t)fx_s16(R16(t))));
    if(fx_s32(w)>-1)W16(R32(R32(o+0x34u)+8u)+2u*(uint16_t)w,0);
    ec_copy_point(m,o+0x26u,p,&r);r.eax=o+0x26u;
    if(R16(t+4u)) {
        a[0]=R32(EC_VA(g,0x4a968du));r.eax=a[0];
        ec_call(g,0x471b30u,a,1,&r);r.ecx=a[0];
        uint32_t k=r.eax;W32(f-4u,k);
        if(k) {
            W16(f-0x18u,0x14);
            uint32_t list=R32(EC_VA(g,0x4a9698u));
            a[0]=k;a[1]=R32(list+4u);r.eax=list;r.edx=a[1];r.ecx=k;
            ec_call(g,0x402188u,a,2,&r);
            W32(k,EC_VA(g,0x49a262u));
            W16(k+4u,9);
            (void)ec_count(m,g,1);
            (void)ec_count(m,g,(uint32_t)-4);
            W16(f-0x18u,8);
        }
        W32(o+0x4fu,k);
        /* A null k faults here in the original (outside the contract). */
        r.eax=R32(o+0x4fu)+0x26u;ec_copy_point(m,r.eax,p,&r);
    } else { r.eax=0;W32(o+0x4fu,0); }
    if(R16(t+6u)) {
        a[0]=R32(EC_VA(g,0x4a95feu));r.eax=a[0];
        ec_call(g,0x471b30u,a,1,&r);r.ecx=a[0];
        uint32_t l=r.eax;W32(f-8u,l);
        if(l) {
            W16(f-0x18u,0x2c);
            a[0]=l;a[1]=p;a[2]=word(l,R16(t+8u));r.eax=a[2];r.edx=p;r.ecx=l;
            ec_call(g,0x407604u,a,3,&r);
            (void)ec_count(m,g,(uint32_t)-2);
            W16(f-0x18u,0x20);
        }
        W32(o+0x53u,l);
    } else W32(o+0x53u,0);
    if((uint8_t)sound) {
        uint32_t check=ec_type(m,g,o);
        if(fx_s16(R16(check+0x24u))>-1) {
            uint32_t entry_type=ec_type(m,g,o);
            uint32_t id=(uint32_t)fx_s16(R16(entry_type+0x24u))+10u;
            a[0]=EC_VA(g,0x4d260bu);a[1]=0x32;r.eax=r.edx=entry_type;r.ecx=o;
            ec_call(g,0x492f24u,a,2,&r);
            a[0]=id;a[1]=p;r.eax=p;
            ec_call(g,0x4628e0u,a,2,&r);
        }
    }
    uint32_t counter=ec_count(m,g,1);
    return pair(o,counter);
}
