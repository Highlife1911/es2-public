#include "es2/core/logic.h"
#include "fixed.h"
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(uint8_t *)m->host(m->context,(a)))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define VA(b,x) ((b)+(x)-0x400000u)
static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static uint32_t low_word(uint32_t high,uint32_t low) { return (high&0xffff0000u)|(uint16_t)low; }
/* Direct calls name the original VA; a virtual call passes its resolved
 * target with the vtable offset as slot. Registers are updated in place. */
static void logic_call(const es2_weapon_graph *g,uint32_t function,unsigned slot,
    const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,function,slot);
    g->call(g->context,slot?function:VA(g->image,function),a,n,r);
}
/* 0x407b20: explosion type entry, [4a9628h] + 40 * signed type byte O+41. */
static uint32_t explosion_type(const es2_guest_memory *m,uint32_t b,uint32_t o) {
    uint32_t type=R8(o+0x41u);
    return R32(VA(b,0x4a9628u))+40u*(type<0x80u?type:type-0x100u);
}

/* Original: DBSIM.EXE 0x47df9c. Each step tries the next lower bit of the
 * root; the candidate square is kept incrementally (EAX, ESI, EDI). */
uint32_t es2_logic_root(uint32_t x) {
    if(!x)return 0;
    uint32_t c=31;while(!(x>>c))--c;
    c>>=1;
    uint32_t root=1u<<c;
    if(!c)return root;
    uint32_t square=root<<c,bit=root>>1,cross;
    --c;
    for(;;) {
        cross=(root<<(c&31u))<<1;
        for(;;) {
            uint32_t candidate=(bit<<(c&31u))+square+cross;
            --c;
            if(candidate<=x) {
                root+=bit;bit>>=1;
                if(!bit)return root;
                square=candidate;
                break;
            }
            bit>>=1;
            if(!bit)return root;
            cross>>=1;
        }
    }
}

/* Original: DBSIM.EXE 0x4076a0 (with 0x407048 and 0x40735c inlined) */
uint64_t es2_light_level_registers(const es2_guest_memory *m,uint32_t b,uint32_t light,uint32_t value) {
    uint32_t table=R32(VA(b,0x4a9608u));
    uint32_t entry=table+0x6cu+35u*(uint32_t)fx_s16(R16(light+0xcu));
    uint32_t level=(uint8_t)value;
    W16(entry+0xdu,level);
    W32(entry+0x1bu,(uint32_t)fx_s16(level));
    /* IMUL keeps the low dword, CDQ/IDIV 10 truncates toward zero. */
    uint32_t product=(uint32_t)fx_s16(R16(entry+0xdu))*(R32(entry+0x13u)<<5);
    uint32_t quotient=(uint32_t)(int32_t)(fx_s32(product)/10);
    uint32_t base=R32(entry+0xfu)<<5,radius=quotient+base;
    W32(entry+0x17u,radius);
    return pair(radius,base);
}

/* Original: DBSIM.EXE 0x40813c */
uint64_t es2_explosion_update_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t o,const uint32_t entry[3]) {
    uint32_t b=g->image,a[2];
    es2_weapon_registers r={o+0x4au,entry[1],entry[2]};
    a[0]=o+0x4au;logic_call(g,0x467a48u,0,a,1,&r);
    if((uint16_t)r.eax)return pair(0,0);
    uint32_t type=explosion_type(m,b,o),table=R32(VA(b,0x4a963cu));
    uint32_t anim=R16(table+4u*(uint32_t)fx_s16(R16(type)));
    if(fx_s16(anim)<=-1)return pair((table&0xffff0000u)|1u,low_word(type,anim));
    uint32_t shape=R32(o+0x34u);
    uint32_t next=R16(R32(shape+8u)+2u*anim)+1u,count=R16(R32(R32(shape+4u)+0x20u)+2u*anim);
    W16(R32(shape+8u)+2u*anim,next%count);
    uint32_t frames=R32(shape+8u),frame=R16(frames+2u*anim);
    if(!frame)return pair(1,frames);
    uint32_t light=R32(o+0x53u);
    if(light) {
        type=explosion_type(m,b,o);
        uint32_t level=(type&0xffffff00u)|R8(type+8u+2u*(uint32_t)fx_s16(frame));
        a[0]=light;a[1]=level;r.eax=level;r.edx=(uint32_t)fx_s16(frame);r.ecx=o;
        logic_call(g,0x4076a0u,0,a,2,&r);
    }
    uint32_t linked=R32(o+0x4fu);
    if(linked) {
        uint32_t s=R32(linked+0x34u),f=R32(s+8u);
        uint32_t k=R16(f)+1u,c=R16(R32(R32(s+4u)+0x20u));
        W16(f,k%c);
    }
    type=explosion_type(m,b,o);
    W16(o+0x4bu,R16(type+2u));
    return pair(0,type);
}

/* Original: DBSIM.EXE 0x46b678. P (local point) and Q (world point) live in
 * the scratch bytes 0..11 and 16..27. */
uint64_t es2_fire_update_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t o,const uint32_t entry[3]) {
    uint32_t b=g->image,p=g->scratch,q=g->scratch+16u,a[3];
    es2_weapon_registers r={o+0x56u,entry[1],entry[2]};
    a[0]=o+0x56u;logic_call(g,0x467a48u,0,a,1,&r);
    r.ecx=o+0x56u;
    if((uint16_t)r.eax)r.edx=0;
    else {
        W16(o+0x57u,0x40u);
        uint32_t shape=R32(o+0x34u),frames=R32(shape+8u);
        uint32_t next=R16(frames)+1u,count=R16(R32(R32(shape+4u)+0x20u));
        uint32_t frame=next%count;
        W16(frames,frame);
        r.eax=next/count;r.edx=frame;r.ecx=shape;
        if(!frame)W16(o+0x59u,R16(o+0x59u)-1u);
    }
    uint32_t node=R16(o+0x4eu);
    if(fx_s16(node)<=-1&&R8(o+0x41u)) {
        uint32_t s=R32(o+0x4au)+0x26u;
        uint32_t z=R32(s+8u),x=R32(s),y=R32(s+4u);
        W32(q+8u,z);W32(q,x);W32(q+4u,y);
    } else {
        if(fx_s16(node)<0) {
            r.edx=(uint32_t)fx_s16(R16(o+0x54u));W32(p+8u,r.edx);
            r.ecx=(uint32_t)fx_s16(R16(o+0x50u));W32(p,r.ecx);
            r.eax=(uint32_t)fx_s16(R16(o+0x52u));W32(p+4u,r.eax);
        } else {
            uint32_t owner=R32(o+0x4au),shape=R32(owner+0x34u),object=R32(shape+4u),table=R32(object);
            a[0]=object;a[1]=low_word(r.edx,node);r.eax=table;r.edx=a[1];r.ecx=object;
            logic_call(g,R32(table+0x20u),0x20,a,2,&r);
            uint32_t record=r.eax,matrix=VA(b,0x6c56a8u);
            if(record&&fx_s16(R16(record+4u))>-1) {
                r.edx=(uint32_t)fx_s16(R16(record+4u))<<5;
                matrix=R32(shape+0x16u)+r.edx;
            }
            if(!R8(o+0x41u)) {
                a[0]=o+0x50u;a[1]=matrix;a[2]=p;r.eax=matrix;r.edx=o+0x50u;r.ecx=p;
                logic_call(g,0x4801d4u,0,a,3,&r);
            } else {
                uint32_t t=matrix+0x14u;
                r.edx=R32(t+8u);W32(p+8u,r.edx);
                r.ecx=R32(t);W32(p,r.ecx);
                r.eax=R32(t+4u);W32(p+4u,r.eax);
            }
        }
        uint32_t angles=R32(o+0x4au)+0xcu;
        if(!R16(angles+0x26u)) {
            a[0]=angles;a[1]=angles+6u;r.eax=angles+6u;
            logic_call(g,0x47ebb8u,0,a,2,&r);
            W16(angles+0x26u,1);
        }
        a[0]=p;a[1]=angles+6u;a[2]=q;r.eax=angles+6u;r.edx=q;r.ecx=p;
        logic_call(g,0x48043cu,0,a,3,&r);
    }
    uint32_t z=R32(q+8u),x=R32(q),y=R32(q+4u);
    W32(o+0x2eu,z);W32(o+0x26u,x);W32(o+0x2au,y);
    uint32_t camera=R32(VA(b,0x4d24eau))+4u;
    uint32_t dx=R32(camera)-x,dy=R32(camera+4u)-y,dz=R32(camera+8u)-z;
    uint32_t sum=dx*dx+dy*dy+dz*dz,root=es2_logic_root(sum);
    r.eax=root;r.edx=sum;
    if(fx_s32(root)<fx_s32(R32(VA(b,0x6b4f3cu)))) {
        a[0]=0x33u;a[1]=q;r.ecx=q;
        logic_call(g,0x46297cu,0,a,2,&r);
        W32(VA(b,0x6b4f3cu),root);
    }
    return pair(R16(o+0x59u)==0,r.edx);
}
