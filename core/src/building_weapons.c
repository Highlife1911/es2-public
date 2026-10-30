#include "es2/core/building_weapons.h"
#include "fixed.h"
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(uint8_t *)m->host(m->context,(a)))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
static uint32_t low_word(uint32_t high,uint32_t low) {return (high&0xffff0000u)|(uint16_t)low;}
static uint64_t result(uint32_t eax,uint32_t edx) {return (uint64_t)edx<<32|eax;}
static void dispatch(const es2_weapon_graph *g,uint32_t function,unsigned slot,
    const uint32_t *args,unsigned count,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,function,slot);
    g->call(g->context,slot?function:g->image+function-0x400000u,args,count,r);
}
uint64_t es2_turret_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t b,const uint32_t entry[3]) {
    es2_weapon_registers r={b+0x92u,entry[1],entry[2]};uint32_t a[5];
    uint32_t s=g->scratch,point=s,offset=s+16,muzzle=s+24,matrix=s+32,angles=s+64,position=s+72;
#define CALL(f,n) dispatch(g,f,0,a,n,&r)
#define SLOT(f,k,n) dispatch(g,f,k,a,n,&r)
    a[0]=b;
    if(R8(b+0x99u)) {CALL(0x403ca8,1);return result(r.eax,r.edx);}
    CALL(0x403914,1);r.ecx=b;
    r.edx=b+0x21du;a[0]=r.edx;CALL(0x467a48,1);r.ecx=!(uint16_t)r.eax;
    if(r.ecx) {
        a[0]=b;a[1]=0x30;a[2]=0;CALL(0x411fa0,3);
        uint32_t old=R32(b+0x1a4u),next=r.eax;
        if(old!=next) {
            if(old) {r.ecx=R32(b+0x1a4u);W16(r.ecx+0x1a2u,R16(r.ecx+0x1a2u)-1u);}
            W32(b+0x1a4u,next);
            if(next) {r.eax=R32(b+0x1a4u);W16(r.eax+0x1a2u,R16(r.eax+0x1a2u)+1u);}
            r.edx=b+0x92u;W8(b+0x9du,1);
        }
        W16(b+0x21eu,10000);
    }
    uint32_t target=R32(b+0x1a4u);
    if(target) {r.eax=b+0x26u;a[0]=r.eax;a[1]=target+0x26u;CALL(0x49288c,2);}
    else r.eax=0;
    uint32_t distance=r.eax;
    if(fx_s32(distance)>60000) {
        r.edx=R32(b+0x1a4u);
        if(r.edx) {
            r.ecx=R32(b+0x1a4u);W16(r.ecx+0x1a2u,R16(r.ecx+0x1a2u)-1u);
            r.edx=0;W32(b+0x1a4u,0);r.eax=b+0x92u;W8(b+0x9du,1);
        }
    }
    r.eax=!R16(b+0x1f7u);
    if(!r.eax) {
        r.edx=b+0x1f6u;a[0]=r.edx;CALL(0x467a48,1);r.ecx=!(uint16_t)r.eax;
        if(r.ecx) {
            uint32_t def=R32(b+0x1f2u),manager=R32(b+0x34u);r.ecx=low_word(r.ecx,R16(def+0x24u));
            uint32_t index=(uint16_t)r.ecx;
            uint32_t numerator=R16(R32(manager+8u)+2u*index)+1u;
            uint32_t denominator=R16(R32(R32(manager+4u)+0x20u)+2u*index);
            W16(R32(manager+8u)+2u*index,numerator%denominator);
            r.eax=index;r.edx=manager;r.ecx=R32(manager+8u);
            if(R16(r.ecx+2u*index)) {
                r.eax=R32(b+0x1f2u);r.eax=low_word(r.eax,R16(r.eax+0x26u));W16(b+0x1f7u,r.eax);
            }
        }
    }
    r.edx=R32(b+0x1a4u);target=r.edx;
    if(!target)return result(0,r.edx);
    W32(point,R32(target+0x26u));W32(point+4u,R32(target+0x2au));W32(point+8u,R32(target+0x2eu));
    r.eax=R32(b+0x1f2u);unsigned projectile=R16(r.eax+0x2eu)==2;
    r.eax=offset;r.ecx=s+88u;a[0]=target;a[1]=r.ecx;a[2]=offset;r.edx=R32(target);
    SLOT(R32(r.edx+0x30u),0x30,3);
    r.eax=(uint32_t)fx_s16(R16(offset+4u));W32(point+8u,R32(point+8u)+r.eax);
    r.edx=(uint32_t)fx_s16(R16(offset));W32(point,R32(point)+r.edx);
    r.ecx=(uint32_t)fx_s16(R16(offset+2u));W32(point+4u,R32(point+4u)+r.ecx);
    if(fx_s32(distance)<40000) {
        uint32_t speed=0;r.eax=0;r.edx=R32(b+0x1a4u);a[0]=r.edx;r.ecx=R32(r.edx);
        SLOT(R32(r.ecx+0x38u),0x38,1);r.eax=(uint32_t)fx_s16(r.eax);r.ecx=a[0];
        int32_t target_speed=fx_s16(r.eax);
        if(!projectile) {a[0]=2;CALL(0x40ffb0,1);r.edx=(uint32_t)fx_s16(R16(r.eax+10u));r.ecx=2;speed=r.edx;}
        if(fx_s32(speed)>0 && target_speed) {
            uint32_t product=(uint32_t)((int64_t)fx_s16(fx_sar(distance,3))*(target_speed*8));
            r.eax=(uint32_t)fx_s16((uint32_t)(fx_s32(product)/fx_s32(speed)));
            a[2]=r.eax;r.edx=point;r.ecx=R32(b+0x1a4u);r.eax=low_word(r.eax,R16(r.ecx+0x10u));
            a[0]=point;a[1]=r.eax;CALL(0x4929fc,3);
        }
    }
    r.ecx=point;a[0]=b;a[1]=point;CALL(0x403eec,2);uint32_t aim=r.eax;
    r.eax=b+0x218u;a[0]=r.eax;CALL(0x467a48,1);r.ecx=a[0];r.edx=!(uint16_t)r.eax;
    if(r.edx) {
        r.ecx=!R16(b+0x21bu);r.eax=r.ecx;W16(b+0x21bu,r.ecx);
        r.eax=low_word(r.eax,R16(g->image+0x973e0u+2u*r.eax));W16(b+0x219u,r.eax);
    }
    r.edx=b+0x211u;a[0]=r.edx;CALL(0x467a48,1);r.ecx=!(uint16_t)r.eax;
    if(!r.ecx || !R16(b+0x21bu))return result(0,r.edx);
    for(unsigned j=0;j<2;++j) {
        r.eax=aim;r.edx=low_word(r.edx,R16(aim+4u*j)+1000u);r.ecx=(uint16_t)r.edx<2000u;
        if(!r.ecx)return result(0,r.edx);
    }
    if(fx_s32(distance)>=40000)return result(0,r.edx);
    uint32_t manager=R32(b+0x34u);r.eax=manager;r.edx=manager;r.ecx=R32(manager+4u);a[0]=r.ecx;a[1]=2;r.eax=R32(r.ecx);
    SLOT(R32(r.eax+0x20u),0x20,2);
    uint32_t record;
    if(r.eax && fx_s16(R16(r.eax+4u))>-1) {
        r.eax=(uint32_t)fx_s16(R16(r.eax+4u));r.edx=manager;r.eax*=32u;r.ecx=R32(manager+0x16u)+r.eax;record=r.ecx;
    } else {r.ecx=g->image+0x2c56a8u;record=r.ecx;}
    W16(muzzle,300);W16(muzzle+2u,400);W16(muzzle+4u,0);
    unsigned first=1,second=1;
    if(projectile) {
        a[0]=g->image+0xd2599u;CALL(0x492ee0,1);r.ecx=a[0];r.edx=!(r.eax&31u);first=r.edx;
        if(first){r.eax=0;second=0;}
        else {CALL(0x492ee0,1);r.ecx=a[0];r.edx=!(r.eax&31u);second=r.edx;}
    } else r.ecx=1;
    if(!first&&!second)return result(0,r.edx);
    if(!R16(b+0x32u)) {
        r.eax=b+0x12u;r.edx=b+0xcu;a[0]=r.edx;a[1]=r.eax;CALL(0x47ebb8,2);W16(b+0x32u,1);
    }
    r.eax=b+0x12u;r.edx=record;r.ecx=matrix;W8(matrix+19u,g->padding);
    a[0]=record;a[1]=r.eax;a[2]=matrix;CALL(0x47fa20,3);
    r.eax=angles;r.edx=matrix;a[0]=matrix;a[1]=angles;CALL(0x47f9a0,2);
    for(unsigned barrel=0;barrel<2;++barrel) {
        if(!(barrel?second:first))continue;
        if(barrel)W16(muzzle,0u-R16(muzzle));
        r.eax=barrel?position:matrix;r.edx=barrel?matrix:muzzle;r.ecx=barrel?muzzle:position;
        a[0]=muzzle;a[1]=matrix;a[2]=position;CALL(0x4801d4,3);
        a[0]=projectile?0:2;a[1]=position;a[2]=angles;a[3]=b;a[4]=0;
        if(projectile) {r.eax=barrel?angles:position;r.edx=barrel?position:r.edx;if(!barrel)r.ecx=angles;CALL(0x40a9c4,5);}
        else {if(barrel){r.ecx=angles;r.eax=position;}else {r.edx=angles;r.ecx=position;}CALL(0x40b43c,4);}
    }
    W16(b+0x212u,1500);W16(b+0x1f7u,1);return result(0,r.edx);
#undef CALL
#undef SLOT
}
