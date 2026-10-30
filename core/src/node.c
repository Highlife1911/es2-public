#include "es2/core/node.h"
#include "fixed.h"
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(uint8_t *)m->host(m->context,(a)))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static uint32_t low_word(uint32_t high,uint32_t low) { return (high&0xffff0000u)|(uint16_t)low; }
/* Direct calls name the original VA; a virtual call passes its resolved
 * target with the vtable offset as slot. Registers are updated in place. */
static void node_call(const es2_weapon_graph *g,uint32_t function,unsigned slot,
    const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,function,slot);
    g->call(g->context,slot?function:g->image+function-0x400000u,a,n,r);
}
/* 47ffe8: two dwords times a 2 x 2 word matrix (column words 0,2 and 1,3),
 * 64-bit products, plus 2000h, bits 14..45 kept. Its result registers are
 * dead at every 4029d8 call site. */
static void node_rotate2(const es2_guest_memory *m,uint32_t vector,uint32_t matrix,uint32_t out) {
    for(unsigned i=0;i<2;++i) {
        uint64_t sum=(uint64_t)(fx_s32(R32(vector))*fx_s16(R16(matrix+2u*i)))+
            (uint64_t)(fx_s32(R32(vector+4u))*fx_s16(R16(matrix+4u+2u*i)))+0x2000u;
        W32(out+4u*i,(uint32_t)(sum>>14));
    }
}
/* 47e0c0: EDX:EAX = s32(x) * s16(word); EAX keeps bits 14..45, EDX the high
 * dword; ECX = s16(word). */
static void node_q14(uint32_t x,uint32_t w,es2_weapon_registers *r) {
    uint64_t product=(uint64_t)(fx_s32(x)*fx_s16(w));
    r->eax=(uint32_t)(product>>14);r->edx=(uint32_t)(product>>32);r->ecx=(uint32_t)fx_s16(w);
}

/* One terrain sample of 4029d8: rotate (x,y) by the copied matrix words,
 * add the object position, then 46e180(map, point). The three call sites
 * load the same values into different scratch registers (order 0..2). */
static uint32_t pose_sample(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t o,uint32_t x,uint32_t y,
    unsigned order,es2_weapon_registers *r) {
    uint32_t s=g->scratch,vector=s,matrix=s+8u,point=s+16u,a[2];
    W32(vector,x);W32(vector+4u,y);node_rotate2(m,vector,matrix,point);
    W32(point,R32(point)+R32(o+0x26u));
    uint32_t py=R32(o+0x2au),map=R32(g->image+0xa0be0u);
    W32(point+4u,R32(point+4u)+py);
    if(order==0) { r->eax=py;r->ecx=map;r->edx=point; }
    else if(order==1) { r->eax=point;r->ecx=py;r->edx=map; }
    else { r->eax=map;r->ecx=point;r->edx=py; }
    a[0]=map;a[1]=point;node_call(g,0x46e180u,0,a,2,r);
    return r->eax;
}
/* Original: DBSIM.EXE 0x4029d8 */
uint64_t es2_terrain_pose_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t o,const uint32_t entry[3]) {
    uint32_t matrix=g->scratch+8u,a[2];
    es2_weapon_registers r={R32(o),entry[1],entry[2]};
    a[0]=o;node_call(g,R32(r.eax+0x10u),0x10,a,1,&r);
    uint32_t size=(uint32_t)fx_s16(r.eax);
    r.ecx=o;
    if(!R16(o+0x32u)) {
        a[0]=o+0xcu;a[1]=o+0x12u;r.eax=o+0x12u;r.edx=o+0xcu;node_call(g,0x47ebb8u,0,a,2,&r);
        W16(o+0x32u,1);
    }
    for(unsigned i=0;i<4;++i)W16(matrix+2u*i,R16(o+0x12u+2u*i));
    uint32_t front=pose_sample(m,g,o,0,size,0,&r);
    uint32_t back=pose_sample(m,g,o,0,0u-size,0,&r);
    uint32_t sum=front+back,twice=size+size;
    a[0]=twice;a[1]=front-back;r.eax=front-back;r.edx=twice;r.ecx=twice;node_call(g,0x47d32cu,0,a,2,&r);
    uint32_t pitch=(uint16_t)r.eax;
    uint32_t left=pose_sample(m,g,o,0u-size,0,1,&r);
    sum+=left;
    uint32_t right=pose_sample(m,g,o,size,0,2,&r);
    sum+=right;
    /* ECX still holds what the fourth 46e180 left. */
    a[0]=twice;a[1]=left-right;r.eax=twice;r.edx=right;node_call(g,0x47d32cu,0,a,2,&r);
    uint32_t roll=(uint16_t)r.eax,yaw=R16(o+0x10u);
    W32(o+0xcu,pitch|roll<<16);W16(o+0x10u,yaw);W16(o+0x32u,0);
    uint32_t average=fx_sar(sum,2);W32(o+0x2eu,average);
    return pair(average,roll<<16|yaw);
}

/* Original: DBSIM.EXE 0x413cc4 */
uint64_t es2_shield_geometry_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t shields,
    uint32_t vehicle,uint32_t matrix,uint32_t range,uint32_t damage,const uint32_t entry[3]) {
    uint32_t s=g->scratch,vector=s,out=s+8u,local=s+32u,point=s+64u,a[3];
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t def=R32(vehicle+0x1f2u)+2u;
    W16(vector+4u,R16(def+0x16u));W16(vector,0);W16(vector+2u,0);
    a[0]=vector;a[1]=matrix;a[2]=out;r.eax=matrix;r.edx=out;r.ecx=vector;node_call(g,0x4801d4u,0,a,3,&r);
    uint32_t y=R32(out+4u);
    if(y>=range)return pair(0,r.edx);
    a[0]=R32(out);a[1]=R32(out+8u);r.eax=y;r.edx=a[0];r.ecx=a[0];node_call(g,0x47de4cu,0,a,2,&r);
    uint32_t distance=r.eax,radius=R16(def+0x18u);
    uint32_t edx=low_word(distance,distance+radius);
    if((uint16_t)edx>=(uint16_t)(radius*2u))return pair(0,edx);
    for(unsigned i=0;i<9;++i)W16(local+2u*i,R16(matrix+2u*i));
    W8(local+18u,R8(matrix+18u));W8(local+19u,R8(matrix+19u));
    uint32_t tx=R32(matrix+20u),ty=R32(matrix+24u),tz=R32(matrix+28u);
    W32(local+20u,tx);W32(local+24u,ty);W32(local+28u,tz);
    a[0]=local;r.eax=tz;r.edx=ty;r.ecx=local;node_call(g,0x47df19u,0,a,1,&r);
    W32(local+28u,0u-R32(local+28u));W32(local+20u,0u-R32(local+20u));
    uint32_t ny=0u-R32(local+24u);W32(local+24u,ny);
    W32(point,R32(local+20u));W32(point+4u,ny);W32(point+8u,R32(local+28u));
    a[0]=point;a[1]=local;a[2]=local+20u;r.eax=local+20u;r.edx=local;r.ecx=point;node_call(g,0x480304u,0,a,3,&r);
    uint32_t selected=fx_s32(R32(local+24u))>0?shields:shields+2u;
    /* CX takes the shield word over the ECX that 480304 left; DX the damage. */
    uint32_t shield=low_word(r.ecx,R16(selected));
    edx=low_word(damage,R16(damage));
    if(fx_s16(shield)<=fx_s16(edx))edx=shield;
    uint32_t absorbed=(uint16_t)edx;
    if(absorbed) {
        W16(damage,R16(damage)-absorbed);
        W16(selected,R16(selected)-absorbed);
    }
    int32_t margin=fx_s16(R16(def+0x18u)-distance);
    uint32_t height=R32(out+4u);
    if((int64_t)margin>=fx_s32(height))return pair(1,edx);
    return pair(height-(uint32_t)margin+1u,(uint32_t)margin);
}

/* Original: DBSIM.EXE 0x4089e4 */
uint64_t es2_debris_heading_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t o,
    uint32_t divisor,uint32_t pitch,uint32_t yaw,const uint32_t entry[3]) {
    uint32_t b=g->image,a[2];
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    int64_t numerator=(int64_t)fx_s16(R16(b+0xa9668u))*1024;
    /* Original IDIV: nonzero divisor; |numerator| < 2^25 cannot overflow. */
    uint32_t speed=(uint32_t)(numerator/fx_s32(divisor)),remainder=(uint32_t)(numerator%fx_s32(divisor));
    uint32_t w_pitch=(uint16_t)pitch,w_yaw=(uint16_t)yaw;
    a[0]=low_word(remainder,w_pitch);r.eax=low_word(speed,w_pitch);r.edx=a[0];node_call(g,0x47dff0u,0,a,1,&r);
    node_q14((uint32_t)fx_s16(speed),low_word(a[0],r.eax),&r);
    uint32_t horizontal=(uint32_t)fx_s16(r.eax);
    W32(o+0xcu,0);W16(o+0x10u,w_yaw);W16(o+0x32u,0);
    a[0]=low_word(o+0xcu,w_yaw+0x4000u);r.eax=low_word(yaw,w_yaw+0x4000u);r.edx=horizontal;r.ecx=a[0];
    node_call(g,0x47dff0u,0,a,1,&r);
    node_q14(horizontal,r.eax,&r);
    W16(o+0x4au,r.eax);
    uint32_t component=(uint32_t)fx_s16(r.eax);
    a[0]=low_word(r.edx,w_yaw);r.eax=horizontal;r.edx=a[0];r.ecx=component;node_call(g,0x47dff0u,0,a,1,&r);
    node_q14(horizontal,low_word(a[0],r.eax),&r);
    W16(o+0x4cu,r.eax);
    /* EAX and ECX stay as 47e0c0 left them; EDX is the sign-extended word. */
    a[0]=low_word(yaw,w_pitch)+0xc000u;r.edx=(uint32_t)fx_s16(r.eax);node_call(g,0x47dff0u,0,a,1,&r);
    node_q14((uint32_t)fx_s16(speed),r.eax,&r);
    W16(o+0x4eu,r.eax);
    r.ecx=(uint32_t)fx_s16(r.eax);
    if(R32(b+0xa9660u)) {
        uint32_t first=R32(b+0xa9660u),w=R16(first);
        r.edx=low_word(r.edx,w);W16(o+0x4au,R16(o+0x4au)+w);
        uint32_t second=R32(b+0xa9660u);w=R16(second+4u);
        r.eax=low_word(first,w);W16(o+0x4cu,R16(o+0x4cu)+w);
        uint32_t third=R32(b+0xa9660u);w=R16(third+8u);
        r.edx=third;r.ecx=low_word(second,w);W16(o+0x4eu,R16(o+0x4eu)+w);
    }
    a[0]=b+0xd2599u;node_call(g,0x492ee0u,0,a,1,&r);
    r.ecx=b+0xd2599u;a[0]=0xfffff95cu;a[1]=r.eax&0x3ffu;node_call(g,0x47e0b0u,0,a,2,&r);
    uint32_t eax=low_word(r.eax,r.eax+0xfce0u);
    W16(o+0x5bu,eax);
    return pair(eax,r.edx);
}
