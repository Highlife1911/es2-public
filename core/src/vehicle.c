#include "es2/core/vehicle.h"
#include "fixed.h"

/* Vehicle leaves 4151a4, 4152ec, 412608, 412c8c, 41e758 and 41b60c.
 * (docs/reverse-engineering/vehicle.md). The register struct r follows
 * EAX/EDX/ECX wherever a game call or the result can see them; lo() is a 16-bit
 * load or operation that keeps the register's high word. */
static uint64_t pair(uint32_t a,uint32_t d) { return (uint64_t)d<<32|a; }
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
static int32_t s16(uint32_t x) { return fx_s16(x); }
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
/* IDIV of a 32-bit dividend: EAX quotient, EDX remainder (a zero divisor
 * faults as in the original; no caller reaches an overflow). */
static void vh_divide(uint32_t *eax,uint32_t *edx,int32_t dividend,int32_t divisor) {
    *eax=(uint32_t)(dividend/divisor);*edx=(uint32_t)(dividend%divisor);
}
/* |x| as the original computes it in 41e7cb..41e807: 8000h gives 7FFFh,
 * a negative word is negated in the low word of a copy of the source. */
static uint32_t vh_magnitude(uint32_t reg,uint32_t source) {
    const uint16_t x=(uint16_t)source;
    return x==0x8000u?lo(reg,0x7fffu):s16(x)<0?lo(source,0u-x):source;
}
static void vh_call(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,
                    es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}
/* A direct call at its original address, or the virtual at `slot` of the
 * object whose table r->{eax,ecx,edx} already holds (as in the original). */
#define CALL(va,n) vh_call(g,g->image+(va)-0x400000u,0,a,n,&r)
#define VCALL(table,slot,n) vh_call(g,R32((table)+(slot)),(slot),a,n,&r)

/* Original: DBSIM.EXE 0x4151a4. */
uint64_t es2_condition_table_registers(const es2_guest_memory *m,uint32_t parts,uint32_t out) {
    uint32_t eax=0,edx=0,groups=R32(parts+4u);
    for(uint32_t i=0;i<19u;++i,groups+=2u) {
        const uint32_t o=out+2u+2u*i;
        if(s16(R16(groups))<0) { W16(o,0x100u);continue; }
        const int32_t base=s16(R16(R32(R32(parts+0xcu))+18u*i));
        vh_divide(&eax,&edx,s16(R16(groups))*256,base);W16(o,eax);
    }
    /* Groups 19..28 combined with parts 12..21; the group pointer continues. */
    const uint32_t tables=R32(parts+0xcu);
    uint32_t maxima=R32(tables+4u)+0x18u,values=R32(parts)+0x18u;
    for(uint32_t i=0;i<10u;++i,groups+=2u,values+=2u,maxima+=2u) {
        const int32_t base=s16(R16(R32(R32(parts+0xcu))+18u*(19u+i)));
        int32_t sum=s16(R16(groups))<0?base:s16(R16(groups));
        sum+=s16(s16(R16(values))<0?R16(maxima):R16(values));
        vh_divide(&eax,&edx,sum*256,s16(R16(maxima))+base);W16(out+0x40u+2u*i,eax);
    }
    maxima=R32(R32(parts+0xcu)+4u);values=R32(parts);eax=parts;
    for(uint32_t i=0;i<12u;++i,values+=2u,maxima+=2u) {
        const uint32_t o=out+0x28u+2u*i;
        if(!R16(maxima))W16(o,0);
        else if(s16(R16(values))<0)W16(o,0x100u);
        else { vh_divide(&eax,&edx,s16(R16(values))*256,s16(R16(maxima)));W16(o,eax); }
    }
    return pair(eax,edx);
}

/* Original: DBSIM.EXE 0x4152ec. entry[0] is the caller's EBX (entry shim),
 * entry[2] its ECX; both only matter for kinds outside 0..2. */
uint64_t es2_condition_ratio_registers(const es2_guest_memory *m,uint32_t parts,uint32_t kind,uint32_t index,
    const uint32_t entry[3]) {
    const int32_t k=s16(kind);const uint32_t s=sx(index);
    uint32_t eax,edx=index,value,divisor;
    if(k==0) { divisor=R32(R32(parts+0xcu)+4u)+2u*s;eax=R32(parts)+2u*s;value=eax; }
    else if(k==1||k==2) { divisor=R32(R32(parts+0xcu))+18u*s;value=R32(parts+4u)+2u*s+(k==2?0x26u:0u);eax=2u*s; }
    else { value=entry[2];divisor=entry[0];eax=parts; }
    if(s16(R16(value))<0||!R16(divisor))return pair(lo(eax,0x100u),edx);
    vh_divide(&eax,&edx,s16(R16(value))*256,s16(R16(divisor)));
    return pair(eax,edx);
}

/* Original: DBSIM.EXE 0x412608. The frame (EBP = scratch+30h) holds the
 * two position vectors at EBP-18h (viewer) and EBP-24h (target) and the
 * terrain_line output at EBP-8. */
uint64_t es2_sight_check_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t viewer,uint32_t target,
    const uint32_t entry[3]) {
    const uint32_t A=viewer,B=target,f=g->scratch+0x30u,va=f-0x18u,vb=f-0x24u,hit=f-8u,table=A+0x132u;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[4];
    const uint32_t pending=R16(B+0x1e3u),slot=sx(R16(B+0x4bu));
    if(!pending) {
        a[0]=g->image+0xd2599u;a[1]=1000;CALL(0x492f24,2);
        r.eax=lo(r.eax,r.eax+5000u);W16(A+0x1e3u,r.eax);
        W32(va,R32(A+0x26u));W32(va+4u,R32(A+0x2au));W32(va+8u,R32(A+0x2eu));
        W32(vb,R32(B+0x26u));r.ecx=R32(B+0x2au);W32(vb+4u,r.ecx);r.eax=R32(B+0x2eu);W32(vb+8u,r.eax);
        r.edx=R32(A);a[0]=A;VCALL(r.edx,0x24u,1);
        const uint32_t viewer_record=r.eax;r.ecx=A;
        r.ecx=R32(B);a[0]=B;VCALL(r.ecx,0x24u,1);
        const uint32_t target_record=r.eax;r.ecx=B;
        if(viewer_record) { r.edx=viewer_record;r.ecx=R32(viewer_record+0x1cu); } else r.ecx=500;
        W32(va+8u,R32(va+8u)+r.ecx);
        r.eax=target_record?R32(target_record+0x1cu):500u;
        W32(vb+8u,R32(vb+8u)+r.eax);
        r.eax=hit;r.ecx=va;r.edx=vb;a[0]=hit;a[1]=va;a[2]=vb;a[3]=0;CALL(0x4280f4,4);
        W8(table+slot,(uint16_t)r.eax==0);
    }
    return pair((uint32_t)(int32_t)(int8_t)R8(table+slot),table);
}

/* Original: DBSIM.EXE 0x412c8c. Count and member array are re-read every
 * iteration (the member virtual +40h may change them). */
uint64_t es2_group_state_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t group,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[1];
    uint32_t down=0,sum=0;
    for(uint32_t i=0;s16(i)<s16(R16(group+0x10u));++i) {
        r.eax=sx(i);r.edx=R32(group+0xcu);const uint32_t M=R32(r.edx+4u*r.eax);
        r.eax=M+0x92u;
        if(R8(M+0xa4u))r.ecx=1;
        else { r.edx=M+0x92u;r.ecx=R8(M+0x99u)?1u:0u; }
        if((uint16_t)r.ecx)down=(uint16_t)(down+1u);
        r.eax=R32(M);a[0]=M;VCALL(r.eax,0x40u,1);
        r.ecx=M;sum=(uint16_t)(sum+r.eax);
    }
    const uint32_t count=R16(group+0x10u);
    uint32_t average,remainder,eax,edx;
    vh_divide(&average,&remainder,s16(sum),s16(count));
    vh_divide(&eax,&edx,s16(down)*1024,s16(count));
    if((uint16_t)count==(uint16_t)down)eax=lo(eax,4);
    else if(s16(eax)>=650||s16(average)>0xc0)eax=lo(eax,3);
    else if(s16(eax)>=250||s16(average)>0x80)eax=lo(eax,2);
    else if(s16(average)>0x32)eax=lo(eax,1);
    else eax=0;
    return pair(eax,edx);
}

/* Original: DBSIM.EXE 0x41e758. */
uint64_t es2_target_bearing_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t actor,uint32_t out,
    uint32_t target,const uint32_t entry[3]) {
    const uint32_t A=actor,T=target?target:R32(A+0x1a4u);
    es2_weapon_registers r={target,entry[1],entry[2]};uint32_t a[2];
    r.eax=A+0x26u;r.edx=T+0x26u;a[0]=T+0x26u;a[1]=A+0x26u;CALL(0x492934,2);
    W16(out,r.eax);
    r.ecx=lo(r.ecx,R16(out));r.ecx=lo(r.ecx,r.ecx-R16(A+0x10u));W16(out+2u,r.ecx);
    r.eax=R32(T);a[0]=T;VCALL(r.eax,0x3cu,1);
    r.edx=lo(r.edx,R16(out));r.ecx=T;
    r.edx=lo(r.edx,r.edx+0x8000u);r.edx=lo(r.edx,r.edx-R16(T+0x10u));r.eax=lo(r.eax,r.eax+r.edx);
    W16(out+6u,r.eax);
    r.ecx=T+0x26u;a[0]=A+0x26u;a[1]=T+0x26u;CALL(0x49288c,2);
    W32(out+0xau,r.eax);
    r.eax=lo(r.eax,R16(out+2u));r.edx=vh_magnitude(r.edx,r.eax);W16(out+4u,r.edx);
    r.eax=lo(r.eax,R16(out+6u));r.ecx=vh_magnitude(r.ecx,r.eax);W16(out+8u,r.ecx);
    W16(out+0xeu,0);W32(out+0x10u,15000u);W32(out+0x14u,30000u);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x41b60c (vehicle virtual +58h). The transformed
 * offset vector lives at EBP-1Ch (EBP = scratch+20h). 0x6c56a8 is the
 * default node record in the image. */
uint64_t es2_part_position_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t vehicle,uint32_t part,
    uint32_t out,const uint32_t entry[3]) {
    const uint32_t A=vehicle,local=g->scratch+0x20u-0x1cu,s=sx(part);
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[3];
    const uint32_t rec=R32(R32(A+0x1fau))+18u*s;r.edx=18u*s;r.eax=0;
    if(!R32(rec+0xeu)) {
        const uint32_t z=R32(A+0x2eu);W32(out+8u,z);
        const uint32_t x=R32(A+0x26u);W32(out,x);
        const uint32_t y=R32(A+0x2au);W32(out+4u,y);
        return pair(y,z);
    }
    if(s16(R16(rec+0xcu))>0) {
        const uint32_t node=R16(rec+0xcu),S=R32(A+0x34u);
        r.edx=S;r.ecx=R32(S+4u);a[0]=r.ecx;a[1]=node;r.eax=R32(r.ecx);VCALL(r.eax,0x20u,2);
        const uint32_t found=r.eax;
        if(!found)r.eax=g->image+0x2c56a8u;
        else {
            r.edx=found;
            if(s16(R16(found+4u))<=-1)r.eax=g->image+0x2c56a8u;
            else { r.eax=R32(S+0x16u);r.ecx=sx(R16(found+4u))<<5;r.eax+=r.ecx; }
        }
    }
    if(r.eax) {
        const uint32_t record=r.eax;
        r.edx=R32(rec+0xeu);r.ecx=local;a[0]=r.edx;a[1]=record;a[2]=local;CALL(0x4801d4,3);
        if(!R16(A+0x32u)) { r.eax=A+0x12u;a[0]=A+0xcu;a[1]=A+0x12u;CALL(0x47ebb8,2);W16(A+0x32u,1); }
        r.eax=A+0x12u;r.edx=local;a[0]=local;a[1]=A+0x12u;a[2]=out;CALL(0x48043c,3);
        return pair(r.eax,r.edx);
    }
    if(!R16(A+0x32u)) { r.eax=A+0x12u;r.edx=A+0xcu;a[0]=A+0xcu;a[1]=A+0x12u;CALL(0x47ebb8,2);W16(A+0x32u,1); }
    r.eax=A+0x12u;r.edx=R32(rec+0xeu);a[0]=r.edx;a[1]=A+0x12u;a[2]=out;CALL(0x4801d4,3);
    return pair(r.eax,r.edx);
}

/* Vehicle leaves 413ab8, 417bec, 41537c, 41b534 and 4157c8 (vehicle.md,
 * section "Shield, damage, facing and keys"). */

/* Original: DBSIM.EXE 0x413ab8. */
uint64_t es2_shield_max_set_registers(const es2_guest_memory *m,uint32_t shield,uint32_t value,const uint32_t entry[3]) {
    W16(shield+6u,value);
    return pair(shield,lo(entry[1],value));
}

/* Original: DBSIM.EXE 0x417bec. */
uint64_t es2_shield_capacity_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t vehicle,
    const uint32_t entry[3]) {
    const uint32_t A=vehicle,P=A+0x206u;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[2];
    r.ecx=P;r.edx=R32(P);r.eax=R32(P+0xcu);
    const uint32_t part=sx(R16(r.edx+8u)),maxima=R32(r.eax+4u);
    if(s16(part)<0)r.eax=lo(r.eax,0x100u);
    else { r.ecx=sx(R16(maxima+8u));vh_divide(&r.eax,&r.edx,(int32_t)(part<<8),(int32_t)r.ecx); }
    const uint32_t q=sx(r.eax);
    const uint32_t config=R32(A+0x1f2u),base=sx(R16(config+0xc0u));
    uint32_t total=base;r.eax=config;
    if((int32_t)q>=0x80) {
        uint32_t t,remainder;vh_divide(&t,&remainder,(int32_t)(q-0x80u),25);
        r.ecx=0x400u-102u*t;r.eax=r.ecx;r.edx=base;a[0]=base;a[1]=r.ecx;CALL(0x47e0b0,2);
        total=r.eax;
    }
    const uint32_t X=R32(A+0x30fu);r.eax=X;
    if(X) {
        r.edx=R32(X+0x18u);
        const uint32_t e=(uint32_t)(int32_t)(int8_t)R8(r.edx+0x17u);
        r.eax=P;a[0]=P;a[1]=lo(e,e+0x13u);CALL(0x40dbc0,2);
        const uint32_t s=sx(r.eax);r.edx=s;
        if((int32_t)s<0xe1) {
            uint32_t t,remainder;vh_divide(&t,&remainder,(int32_t)s,51);
            r.ecx=0x400u-204u*t;r.edx=r.ecx;r.eax=base;a[0]=r.ecx;a[1]=base;CALL(0x47e0b0,2);
            total+=r.eax;
        }
    }
    r.ecx=lo(r.ecx,total);a[0]=A+0x222u;a[1]=r.ecx;CALL(0x413ab8,2);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x41537c. The weight words live in the image at
 * 499fe0 (groups) and 49a006 (parts). */
uint64_t es2_damage_score_registers(const es2_guest_memory *m,uint32_t image,uint32_t parts,const uint32_t entry[3]) {
    const uint32_t P=parts;uint32_t sum=0,edx=entry[1],q;
    uint32_t weight=image+0x99fe0u,flags=R32(P+8u),values=R32(P+4u);
    for(uint32_t i=0;i<19u;++i,flags+=2u,values+=2u,weight+=2u) {
        const int32_t v=s16(R16(values));
        if(!R16(flags)||v<0||v>=0x96)continue;
        const int32_t base=s16(R16(R32(R32(P+0xcu))+18u*i));
        vh_divide(&q,&edx,(base-v)*s16(R16(weight)),base);sum+=q;
    }
    uint32_t maxima=R32(R32(P+0xcu)+4u);values=R32(P);weight=image+0x9a006u;
    for(uint32_t i=0;i<12u;++i,maxima+=2u,values+=2u,weight+=2u) {
        const int32_t v=s16(R16(values)),top=s16(R16(maxima));
        if(!top||v<0||v>=0x96)continue;
        vh_divide(&q,&edx,(top-v)*s16(R16(weight)),top);sum+=q;
    }
    return pair(sum,edx);
}

/* Original: DBSIM.EXE 0x41b534 (vehicle virtual +54h). The part position
 * L18 and the angle words Lc live in the scratch frame (EBP-18h, -0Ch). */
uint64_t es2_facing_part_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t vehicle,uint32_t pose,
    const uint32_t entry[3]) {
    const uint32_t A=vehicle,T=pose,position=g->scratch,angles=g->scratch+0xcu;
    es2_weapon_registers r={A,entry[1],entry[2]};uint32_t a[3];
    uint32_t flags=R32(A+0x20eu),best=0x1000000u,index=0xffffu;
    for(uint32_t i=0;s16(i)<0x1d;++i,flags+=2u) {
        if(!R16(flags))continue;
        r.eax=position;r.edx=A;r.ecx=R32(A);a[0]=A;a[1]=i;a[2]=position;VCALL(r.ecx,0x58u,3);
        r.eax=T+0x1au;r.edx=position;r.ecx=angles;a[0]=angles;a[1]=position;a[2]=T+0x1au;CALL(0x492990,3);
        r.edx=lo(r.edx,R16(T+4u));W16(angles+4u,R16(angles+4u)-r.edx);
        r.ecx=lo(r.ecx,R16(T));W16(angles,R16(angles)-r.ecx);
        r.eax=lo(T,R16(T+2u));W16(angles+2u,R16(angles+2u)-r.eax);
        r.eax=lo(r.eax,R16(angles));r.ecx=vh_magnitude(r.ecx,r.eax);r.eax=sx(r.ecx);
        r.edx=lo(r.edx,R16(angles+4u));r.ecx=vh_magnitude(r.ecx,r.edx);r.edx=sx(r.ecx);
        r.eax+=r.edx;
        if((int32_t)r.eax<(int32_t)best) { best=r.eax;index=i; }
    }
    return pair(lo(r.eax,index),r.edx);
}

/* Original: DBSIM.EXE 0x4157c8 (vehicle virtual +2Ch). entry[0] is the
 * caller's ESI (entry shim); *esi is the ESI the bridge loads for every
 * game call. The move vectors and the 4801d4 output live in the scratch
 * frame (EBP = scratch+3Ch). */
uint64_t es2_vehicle_keys_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t object,uint32_t code,
    const uint32_t entry[3],uint32_t *esi) {
    const uint32_t O=object,F=g->scratch+0x3cu,out=F-0x3cu,image=g->image;
    es2_weapon_registers r={0,0,entry[2]};uint32_t a[3];
    r.eax=R32(image+0xa9b84u);r.edx=R32(r.eax+0x202u);
    *esi=entry[0];a[0]=r.edx;a[1]=code;CALL(0x410f78,2);
    if((uint16_t)r.eax)return pair(lo(r.eax,1u),r.edx);
    const uint32_t result=lo(entry[0],1u);*esi=result;
    const uint32_t k=(uint16_t)code;
    if(k==0x1au||k==0x1bu) {
        r.eax=R32(image+0x9b088u);r.ecx=k!=0x1bu;r.edx=R32(r.eax+0x1e9u);
        a[0]=r.edx;a[1]=r.ecx;a[2]=1;CALL(0x438d9c,3);
        return pair(result,r.edx);
    }
    if(k==0x4cu) {
        const uint32_t V=R32(image+0xa9b84u);W16(V+0x290u,0);W8(V+0x93u,1);
        return pair(result,r.edx);
    }
    const int move=k==0x248u||k==0x24bu||k==0x24du||k==0x250u,turn=k==0x44bu||k==0x44du,steps=k-0x602u<9u;
    if(!move&&!turn&&!steps)return pair(0,r.edx);
    if(!R32(image+0x9ef38u))return pair(result,r.edx);
    if(steps) {
        const uint32_t step=R16(image+0x9941cu+2u*k);W16(image+0xa9cccu,step);
        W16(image+0xa9cceu,R16(image+0x9942eu+2u*k));
        return pair(result,lo(r.edx,step));
    }
    if(turn) {
        const uint32_t turn_step=R16(image+0xa9cceu);
        const uint32_t heading=k==0x44bu?R16(O+0x10u)+turn_step:R16(O+0x10u)-turn_step;
        W16(O+0x10u,heading);W16(O+0x32u,0);
        return pair(result,lo(r.edx,heading));
    }
    if(!R16(O+0x32u)) { r.eax=O+0x12u;r.edx=O+0xcu;a[0]=O+0xcu;a[1]=O+0x12u;CALL(0x47ebb8,2);W16(O+0x32u,1); }
    const uint32_t s=R16(image+0xa9cccu),negative=(uint16_t)(0u-s);
    const uint32_t vector=F-(k==0x24bu?0xcu:k==0x24du?0x18u:k==0x248u?0x24u:0x30u);
    if(k==0x24bu||k==0x24du) { r.edx=lo(r.edx,k==0x24bu?negative:s);W16(vector,r.edx);W16(vector+2u,0);W16(vector+4u,0); }
    else { r.edx=lo(r.edx,k==0x248u?s:negative);W16(vector,0);W16(vector+2u,r.edx);W16(vector+4u,0); }
    r.ecx=out;r.eax=vector;a[0]=vector;a[1]=O+0x12u;a[2]=out;CALL(0x4801d4,3);
    W32(O+0x2eu,R32(out+8u));W32(O+0x26u,R32(out));
    const uint32_t y=R32(out+4u);W32(O+0x2au,y);
    return pair(result,y);
}

/* Helpers (docs/reverse-engineering/vehicle.md, "Vehicle part and
 * weapon-group helpers"). Part tables P as above: [P] part values, [P+4]
 * group values, [P+0Ch] -> {group records (18 bytes: word base maximum,
 * word +6 part count, dword +8 -> 4-byte entries whose word +2 is a part
 * index), part maxima}. */
/* Original: DBSIM.EXE 0x40cf10. */
uint64_t es2_part_list_dead_registers(const es2_guest_memory *m,uint32_t record,uint32_t values) {
    uint32_t list=R32(record+8u),k=0;
    for(;s16(k)<s16(R16(record+6u));++k,list+=4u)
        if(s16(R16(values+2u*sx(R16(list+2u))))>=0)return pair(0,k);
    return pair(lo(list,1u),k);
}

/* Original: DBSIM.EXE 0x40d9f8. */
uint64_t es2_part_group_dead_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t parts,uint32_t index) {
    const uint32_t i=sx(index);
    if(s16(R16(R32(parts+4u)+2u*i))>=0)return pair(0,index);
    uint32_t a[2];es2_weapon_registers r;
    a[1]=R32(parts);r.ecx=i;r.eax=18u*i;r.edx=R32(R32(parts+0xcu))+r.eax;a[0]=r.edx;
    CALL(0x40cf10,2);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x40dc58. */
uint64_t es2_part_group_max_registers(const es2_guest_memory *m,uint32_t parts,uint32_t index) {
    const uint32_t tables=R32(parts+0xcu),record=R32(tables)+18u*sx(index),maxima=R32(tables+4u);
    uint32_t list=R32(record+8u),sum=sx(R16(record)),k=0;
    for(;s16(k)<s16(R16(record+6u));++k,list+=4u)sum+=sx(R16(maxima+2u*sx(R16(list+2u))));
    return pair(sum,k);
}

/* Original: DBSIM.EXE 0x4240f4. Words 4a9e52, 4a9e5a, 4a9e5c; EDX and ECX
 * are not touched. */
uint64_t es2_mission_guard_registers(const es2_guest_memory *m,uint32_t image,const uint32_t entry[3]) {
    const uint32_t result=R16(image+0xa9e52u)==0?R16(image+0xa9e5au)==1u:R16(image+0xa9e5cu)==0;
    return pair(result,entry[1]);
}

/* Original: DBSIM.EXE 0x411a74 (ACTOR/FLYER slot +30h; the first argument
 * is not read). Two memset calls of six zero bytes through the bridge. */
uint64_t es2_actor_offsets_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t first,uint32_t second,
    const uint32_t entry[3]) {
    (void)m;es2_weapon_registers r={first,entry[1],entry[2]};uint32_t a[3]={first,0,6};
    CALL(0x49681a,3);
    r.edx=second;a[0]=second;a[1]=0;a[2]=6;
    CALL(0x49681a,3);
    return pair(r.eax,r.edx);
}
