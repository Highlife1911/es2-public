#include "es2/core/vehicle_control.h"
#include "fixed.h"

/* Vehicle control 416a04 with part_ratio 40dbc0, slope_speed 41693c and
 * random_damage 417a04. Contract: docs/reverse-engineering/ai.md. The register
 * struct r follows EAX/EDX/ECX through every path so that each game call sees
 * the original's entry registers; lo() is a 16-bit load that keeps the
 * register's high word. */
static uint64_t pair(uint32_t a,uint32_t d) { return (uint64_t)d<<32|a; }
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
static int32_t i32(uint32_t x) { return (int32_t)fx_s32(x); }
static int32_t s16(uint32_t x) { return fx_s16(x); }
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
/* |x| of a word as 416a04 computes it: 8000h gives 7FFFh. */
static uint16_t mag(uint16_t x) { return x==0x8000u?0x7fffu:s16(x)<0?(uint16_t)(0u-x):x; }
static void vc_call(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,
                    es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}
/* One direct game call; r holds the entry registers the original sets up. */
#define CALL(va,n) vc_call(g,g->image+(va)-0x400000u,0,a,n,&r)
#define ARGS2(x,y) (a[0]=(x),a[1]=(y))
#define ARGS4(x,y,z,w) (a[0]=(x),a[1]=(y),a[2]=(z),a[3]=(w))

/* Original: DBSIM.EXE 0x40dbc0. */
uint64_t es2_part_ratio_registers(const es2_guest_memory *m,uint32_t obj,uint32_t group) {
    const uint32_t index=sx(group),table=R32(obj+0xcu),entry=R32(table)+18u*index,maxima=R32(table+4u);
    uint32_t max=sx(R16(entry)),cur=sx(R16(R32(obj+4u)+2u*index));
    if(i32(cur)<0)cur=max;
    uint32_t item=R32(entry+8u);
    for(uint32_t i=0;s16(i)<s16(R16(entry+6u));++i,item+=4u) {
        const uint32_t part=sx(R16(item+2u));
        uint16_t x=R16(R32(obj)+2u*part);const uint16_t top=R16(maxima+2u*part);
        if(s16(x)<0)x=top;
        cur+=sx(x);max+=sx(top);
    }
    const int32_t dividend=i32(cur<<8),divisor=i32(max);
    return pair((uint32_t)(dividend/divisor),(uint32_t)(dividend%divisor));
}

/* Original: DBSIM.EXE 0x41693c. The local vector (0, speed) at EBP-10h and
 * its rotation at EBP-0Ch live in scratch; 47ff8c (2 x 2 Q14 rotation) and
 * 47dbca (word dot product) are pure helpers computed here with their
 * result registers. */
uint64_t es2_slope_speed_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t actor,uint32_t speed,
    const uint32_t entry[3]) {
    const uint32_t f=g->scratch+0x10u,vector=f-0x10u,out=f-0xcu,mat=actor+0x12u;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[2];
    r.eax=lo(r.eax,R16(speed));r.edx=actor+0x26u;r.ecx=R32(g->image+0xa0be0u);
    ARGS2(r.ecx,r.edx);CALL(0x46e498,2);
    const uint32_t face=r.eax;
    if(!R16(actor+0x32u)) {
        r.eax=mat;r.edx=actor+0xcu;ARGS2(actor+0xcu,mat);CALL(0x47ebb8,2);
        W16(actor+0x32u,1);
    }
    r.edx=lo(r.edx,R16(speed));r.ecx=out;
    W16(vector,0);W16(vector+2u,r.edx);
    /* 47ff8c(vector, matrix, out). */
    const uint32_t vx=sx(R16(vector)),vy=sx(R16(vector+2u));
    W16(out,fx_sar(vx*sx(R16(mat))+vy*sx(R16(mat+4u))+0x2000u,14));
    const uint32_t m3=sx(R16(mat+6u));
    W16(out+2u,fx_sar(vx*sx(R16(mat+2u))+vy*m3+0x2000u,14));
    r.eax=out;r.edx=vy*m3;r.ecx=m3;
    if(!face)return pair(r.eax,r.edx);
    /* 47dbca(face, out). */
    r.ecx=sx(R16(face))*sx(R16(out));r.edx=sx(R16(out+2u));r.eax=sx(R16(face+2u))*r.edx+r.ecx;
    const uint32_t dot=r.eax;
    r.ecx=lo(r.ecx,R16(speed));
    const uint16_t command=(uint16_t)r.ecx;
    if(!command)return pair(r.eax,r.edx);
    r.eax=(uint32_t)(i32(dot)/2400);r.edx=(uint32_t)(i32(dot)%2400);
    if(s16(command)<0)r.eax=lo(r.eax,0u-r.eax);
    r.eax=lo(r.eax,r.eax+command);
    const int32_t sum=s16(r.eax);
    if((sum>0&&s16(R16(speed))>0)||(sum<0&&s16(R16(speed))<0))W16(speed,r.eax);
    else W16(speed,0);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x417a04. 29 part words at [V+20Eh]; each nonzero
 * part is hit with probability chance/256 (RNG stream A at 4d2599h). */
uint64_t es2_random_damage_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t vehicle,uint32_t amount,
    uint32_t chance,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[4];
    if(!(uint16_t)amount)return pair(r.eax,r.edx);
    const uint32_t stream=0x4d2599u-0x400000u+g->image;
    uint32_t parts=R32(vehicle+0x20eu),i=0;
    do {
        if(R16(parts)) {
            a[0]=stream;CALL(0x492ee0,1);
            r.eax&=0xffff00ffu;r.ecx=stream;
            if(s16(r.eax)<s16(chance)) {
                r.eax=lo(r.eax,amount);ARGS2(stream,r.eax);CALL(0x492f24,2);
                r.edx=fx_sar(sx(amount),1);r.eax=lo(r.eax,r.eax+r.edx);
                const uint32_t value=sx(r.eax);r.ecx=value;
                r.eax=vehicle+0x206u;ARGS2(r.eax,i);CALL(0x40dc58,2);
                r.edx=sx(r.eax);r.ecx=r.edx;r.eax=value;ARGS2(value,r.ecx);CALL(0x47e0a0,2);
                const uint32_t table=R32(vehicle);r.edx=table;
                ARGS4(vehicle,i,r.eax,0);vc_call(g,R32(table+0x74u),0x74,a,4,&r);
            }
        }
        ++i;parts+=2u;
    } while(s16(i)<29);
    return pair(r.eax,r.edx);
}

/* Track targets cleared (416f79, 416fe8, 417087, 4170f8). */
static void vc_reset_track(const es2_guest_memory *m,uint32_t track) {
    W16(track+0xcu,0xffff);W16(track+0x18u,0xffff);W16(track+0x16u,0xffff);W16(track+0x50u,1);W16(track+0x1au,0);
}
/* 47e08d(x, y): Q16 product, EAX the low dword of product >> 16, EDX its
 * high dword. 47e068(x, y): (x << 16) / y, EDX the remainder. */
static void vc_q16_multiply(es2_weapon_registers *r,uint32_t x,uint32_t y) {
    const uint64_t p=(uint64_t)((int64_t)i32(x)*i32(y));r->eax=(uint32_t)(p>>16);r->edx=(uint32_t)(p>>32);
}
static void vc_q16_divide(es2_weapon_registers *r,uint32_t x,uint32_t y) {
    const int64_t n=(int64_t)i32(x)*65536;r->eax=(uint32_t)(n/i32(y));r->edx=(uint32_t)(n%i32(y));
}

/* Original: DBSIM.EXE 0x416a04. turn and speed are the function's own
 * argument words; they live in scratch at the original EBP offsets. */
uint64_t es2_vehicle_control_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t actor,uint32_t turn_slot,
    uint32_t speed_slot,uint32_t flags,const uint32_t entry[3]) {
    const uint32_t A=actor,E=A+0x92u,f=g->scratch,turn=f+0xcu,speed=f+0x10u,S=R32(A+0x1f2u)+2u;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[4];
    W32(turn,turn_slot);W32(speed,speed_slot);W32(f+0x14u,flags);
    r.eax=E;
    if(R8(E+0x12u)) { r.eax=0;W16(turn,0);W16(speed,0); }
    else {
        r.edx=A+0x26du;a[0]=r.edx;CALL(0x467aa8,1);
        const uint32_t timer=r.eax;r.ecx=!timer;
        if(timer) {
            r.eax=E;
            if(R8(E+0x1cu)) { r.edx=lo(r.edx,R16(S+2u));W16(speed,r.edx); }
            else { r.ecx=lo(r.ecx,R16(S+4u));W16(speed,r.ecx); }
        } else {
            r.edx=speed;r.eax=E;W8(E+0x1cu,0);ARGS2(A,speed);CALL(0x41693c,2);
            r.eax=lo(r.eax,R16(S+4u));r.edx=lo(r.edx,R16(S+2u));
            if(s16(r.eax)>s16(R16(speed))) {
                if(s16(r.edx)<s16(R16(speed)))r.eax=lo(r.eax,R16(speed));
                else r.eax=r.edx;
            }
            W16(speed,r.eax);
        }
    }
    if(A!=R32(g->image+0xa9b84u)) {
        r.edx=E;r.ecx=E;
        if(!R8(E+0x12u)) {
            r.eax=lo(r.eax,R16(f+0x14u));r.edx=speed;r.ecx=turn;ARGS4(A,turn,speed,r.eax);CALL(0x416274,4);
        }
    }
    const uint16_t old=R16(A+0x28eu);
    r.eax=lo(r.eax,old);r.edx=E;r.ecx=E;
    int slow=R8(E+0x17u)!=0;
    if(!slow) { r.eax=E;r.edx=E;slow=R8(E+0x19u)!=0; }
    if(slow) { r.ecx=sx(R16(speed));r.eax=r.ecx;ARGS2(400,r.eax);CALL(0x47e0b0,2);W16(speed,r.eax); }
    else {
        r.edx=E;r.ecx=E;slow=R8(E+0x16u)!=0;
        if(!slow) { r.eax=E;r.edx=E;slow=R8(E+0x18u)!=0; }
        if(slow) { r.ecx=sx(R16(speed));r.eax=r.ecx;ARGS2(750,r.eax);CALL(0x47e0b0,2);W16(speed,r.eax); }
    }
    /* Leg condition: the part group of [[A+317h]+18h]+17h, from 225/256 down. */
    r.eax=R32(A+0x317u);
    if(r.eax&&R32(r.eax+0x81u)) {
        r.edx=R32(r.eax+0x18u);r.ecx=(uint32_t)(int32_t)(int8_t)R8(r.edx+0x17u);r.eax=r.ecx;
        r.edx=lo(r.edx,r.eax+0x13u);r.ecx=A+0x206u;ARGS2(r.ecx,r.edx);CALL(0x40dbc0,2);
        const int32_t ratio=s16(r.eax);r.eax=(uint32_t)ratio;r.edx=0;
        uint32_t acc=0;
        if(ratio<0xe1) {
            const int32_t q=ratio/51;r.edx=(uint32_t)q;r.eax=204u*(uint32_t)q;r.ecx=0x400u-r.eax;
            const uint32_t k=r.ecx;
            if(R16(A+0x28eu)) { r.eax=k;r.edx=k;ARGS2(k,1000);CALL(0x47e0b0,2);acc+=r.eax; }
        }
        if(s16(R16(A+0x28eu))>0) { r.ecx=acc;r.eax=sx(R16(S+4u));r.edx=r.eax;ARGS2(acc,r.edx); }
        else { r.eax=acc;r.edx=sx(R16(S+2u));r.ecx=r.edx;ARGS2(acc,r.ecx); }
        CALL(0x47e0b0,2);W16(speed,R16(speed)+r.eax);
    }
    {
        const uint16_t t=R16(turn);
        r.edx=lo(r.edx,s16(t)>=0x100?0x100u:s16(t)<=-0x100?0xff00u:t);W16(turn,r.edx);
    }
    r.eax=lo(r.eax,R16(S+6u));const uint32_t step=r.eax;r.ecx=lo(r.ecx,R16(speed));
    r.eax=A+0x28eu;a[0]=r.eax;a[1]=r.ecx;a[2]=step;CALL(0x467adc,3);
    /* Clip selection. */
    const uint32_t T=R32(A+0x22cu);r.eax=T;
    r.edx=lo(r.edx,R16(T+0x16u));const uint16_t target=(uint16_t)r.edx;
    r.ecx=lo(r.ecx,R16(T+8u));const uint16_t next=(uint16_t)r.ecx;
    r.edx=lo(r.edx,R16(T+4u));const uint16_t cur=(uint16_t)r.edx;
    r.eax=lo(r.eax,R16(A+0x28eu));
    {
        const uint16_t v=(uint16_t)r.eax;
        if(v==0x8000u)r.ecx=lo(r.ecx,0x7fff);else if(s16(v)<0)r.ecx=lo(r.eax,0u-v);else r.ecx=r.eax;
    }
    r.eax=r.ecx;
    const uint16_t previous=mag(old);r.ecx=lo(r.ecx,previous);
    int flag50=0;
    const int stand=cur==R16(S+0x7au);
    const int walk=cur==R16(S+0x10u)||cur==R16(S+0x12u);
    r.ecx=0;
    if((uint16_t)r.eax&&stand&&!walk) { r.eax=0;W16(turn,0);W16(A+0x28eu,0); }
    if(cur!=next) { r.edx=lo(r.edx,old);W16(A+0x28eu,old);goto tail; }
    if(!stand) {
        r.ecx=E;
        if(R8(E+0x12u)) {
            if(cur==R16(S+0x44u)) {
                W16(A+0x2a0u,0x78);r.eax=E;r.edx=E;
                if(!R8(E+0x22u)) {
                    r.ecx=R32(A+0x22cu);r.edx=R32(A+0x22cu);r.eax=lo(r.eax,R16(r.ecx+6u));
                    if((uint16_t)r.eax==R16(r.edx+0xau)) {
                        r.ecx=E;r.eax=E;W8(E+0x22u,1);
                        a[0]=A;a[1]=0x96;a[2]=0x78;CALL(0x417a04,3);
                        r.edx=A+0x26u;ARGS2(0x29,r.edx);CALL(0x4628e0,2);
                    }
                }
                goto tail;
            }
            r.eax=lo(r.eax,R16(S+0x44u));
            if((uint16_t)r.eax!=target) {
                if(cur==R16(S+0x12u)) {
                    r.edx=lo(r.edx,R16(S+0x10u));r.eax=R32(A+0x22cu);ARGS4(r.eax,r.edx,0,0);CALL(0x4792ac,4);
                }
                r.edx=lo(r.edx,R16(S+0x44u));r.ecx=R32(A+0x22cu);ARGS4(r.ecx,r.edx,0xffffffffu,0);CALL(0x47967c,4);
                r.eax=E;r.edx=E;
                if(!R8(E+0x22u)) { r.ecx=A+0x26u;ARGS2(0x1e,r.ecx);CALL(0x4628e0,2); }
            }
            W16(A+0x2a0u,0x64);
            goto tail;
        }
    }
    {
        const int32_t now=s16(r.eax),before=s16(previous);
        if(now>before) {
            if(walk) {
                if(s16(R16(A+0x28eu))>0) {
                    r.ecx=lo(r.ecx,R16(S+0x2cu));
                    if(s16(r.ecx)<s16(R16(A+0x28eu))) { r.eax=lo(r.eax,R16(S+0x2cu));W16(A+0x28eu,r.eax); }
                    r.edx=lo(r.edx,R16(A+0x28eu));
                    r.ecx=lo(r.ecx,s16(r.edx)<0x3c?0x3cu:r.edx);W16(A+0x2a0u,r.ecx);
                } else {
                    r.eax=sx(R16(A+0x28eu));r.edx=0u-sx(R16(S+0x2cu));
                    if(i32(r.eax)<i32(r.edx)) { r.eax=lo(r.eax,0u-R16(S+0x2cu));W16(A+0x28eu,r.eax); }
                    r.eax=lo(r.eax,R16(A+0x28eu));
                    if(s16(r.eax)<=-0x3c)r.edx=r.eax;else r.edx=lo(r.edx,0xffc4);
                    W16(A+0x2a0u,r.edx);
                }
                flag50=1;r.eax=lo(r.eax,target);
                if(target!=R16(S+0xcu)) {
                    r.ecx=lo(r.ecx,s16(R16(A+0x28eu))<0?R16(S+0x12u):R16(S+0x10u));
                    r.eax=lo(r.eax,r.ecx);r.edx=R32(A+0x22cu);ARGS4(r.edx,r.eax,0,0);CALL(0x4792ac,4);
                    r.eax=lo(r.eax,R16(S+0xcu));r.edx=R32(A+0x22cu);ARGS4(r.edx,r.eax,0xffffffffu,0);CALL(0x47967c,4);
                }
            } else if(cur==R16(S+0xcu)) {
                r.ecx=lo(r.ecx,R16(A+0x28eu));W16(A+0x2a0u,r.ecx);
                r.eax=lo(r.eax,target);
                int reset=target==R16(S+0x10u);
                if(!reset) { r.edx=lo(r.edx,target);reset=target==R16(S+0x12u); }
                if(reset) { const uint32_t t=R32(A+0x22cu);r.ecx=t;r.eax=t;r.edx=0xffffffffu;vc_reset_track(m,t); }
                r.edx=lo(r.edx,R16(A+0x28eu));
                if(s16(r.edx)>s16(R16(S+0x2cu))) {
                    r.ecx=lo(r.ecx,R16(S+0xeu));
                    if((uint16_t)r.ecx!=target) {
                        r.eax=lo(r.eax,R16(S+0xeu));r.edx=R32(A+0x22cu);ARGS4(r.edx,r.eax,0xffffffffu,0);CALL(0x47967c,4);
                    }
                }
            } else {
                const uint32_t t=R32(A+0x22cu);r.eax=t;r.edx=0xffffffffu;vc_reset_track(m,t);
                r.eax=lo(r.eax,R16(A+0x28eu));W16(A+0x2a0u,r.eax);
            }
        } else if(now<before) {
            if(cur==R16(S+0xeu)) {
                if(now<s16(R16(S+0x2cu))) {
                    r.ecx=lo(r.ecx,s16(old)>0?R16(S+0x2cu):R16(S+0x6cu));W16(A+0x28eu,r.ecx);
                    r.eax=lo(r.eax,R16(A+0x28eu));W16(A+0x2a0u,r.eax);
                    r.edx=lo(r.edx,R16(S+0xcu));
                    if((uint16_t)r.edx!=target) {
                        r.eax=lo(r.eax,R16(S+0xcu));r.edx=R32(A+0x22cu);ARGS4(r.edx,r.eax,0xffffffffu,0);CALL(0x47967c,4);
                    }
                } else {
                    const uint32_t t=R32(A+0x22cu);r.ecx=t;r.eax=t;r.edx=0xffffffffu;vc_reset_track(m,t);
                    r.edx=lo(r.edx,R16(A+0x28eu));W16(A+0x2a0u,r.edx);
                }
            } else if(cur==R16(S+0xcu)) {
                if(now) {
                    r.ecx=lo(r.ecx,R16(A+0x28eu));W16(A+0x2a0u,r.ecx);
                    r.eax=lo(r.eax,target);
                    if(target==R16(S+0xeu)) {
                        const uint32_t t=R32(A+0x22cu);r.edx=t;r.ecx=t;r.eax=0xffffffffu;vc_reset_track(m,t);r.eax=t;
                    }
                } else {
                    r.edx=s16(old)>0;const int forward=(int)r.edx;
                    W16(A+0x2a0u,0);
                    if(!walk) {
                        r.ecx=lo(r.ecx,forward?R16(S+0x10u):R16(S+0x12u));const uint16_t clip=(uint16_t)r.ecx;
                        r.eax=lo(r.eax,target);
                        if(target!=clip) {
                            r.edx=lo(r.edx,clip);r.eax=R32(A+0x22cu);ARGS4(r.eax,r.edx,0xffffffffu,0);CALL(0x47967c,4);
                        }
                        r.edx=lo(r.edx,0x64);if(!forward)r.edx+=0xffffff38u;
                        W16(A+0x2a0u,r.edx);flag50=1;
                    }
                }
            }
        } else if(now) {
            r.eax=lo(r.eax,R16(A+0x28eu));W16(A+0x2a0u,r.eax);
        } else {
            r.ecx=lo(r.ecx,mag(R16(turn)));
            if(s16(r.ecx)>0x32&&(walk||stand)) {
                if(!stand) {
                    r.eax=lo(r.eax,target);
                    if(target!=R16(S+0x7au)) {
                        r.edx=lo(r.edx,s16(R16(turn))<0?R16(S+0x12u):R16(S+0x10u));
                        r.eax=lo(r.eax,r.edx);r.ecx=R32(A+0x22cu);ARGS4(r.ecx,r.eax,0,0);CALL(0x4792ac,4);
                        r.eax=lo(r.eax,R16(S+0x7au));r.edx=R32(A+0x22cu);ARGS4(r.edx,r.eax,0xffffffffu,0);CALL(0x47967c,4);
                    }
                    r.ecx=lo(r.ecx,0xff9c);if(s16(R16(turn))>=0)r.ecx+=0xc8u;
                    W16(A+0x2a0u,r.ecx);
                } else {
                    r.eax=sx(R16(turn));r.edx=r.eax;ARGS2(0x15e,r.edx);CALL(0x47e0b0,2);
                    W16(A+0x2a0u,r.eax);
                }
            } else if(stand&&!walk) {
                /* 41729b repeats the DI test 417296 just passed: 4172df never runs. */
                r.ecx=lo(r.ecx,R16(S+0x10u));
                if((uint16_t)r.ecx!=target) {
                    r.eax=lo(r.eax,R16(S+0x10u));r.edx=R32(A+0x22cu);ARGS4(r.edx,r.eax,0xffffffffu,0);CALL(0x47967c,4);
                }
                r.ecx=lo(r.ecx,0x64);if(s16(R16(A+0x2a0u))<=0)r.ecx+=0xffffff38u;
                W16(A+0x2a0u,r.ecx);
            }
        }
    }
tail:
    /* Minimum walking speed 45 with a minimum rate of 60. */
    r.eax=lo(r.eax,R16(A+0x28eu));
    {
        const uint16_t v=(uint16_t)r.eax;
        if(v==0x8000u)r.edx=lo(r.edx,0x7fff);else if(s16(v)<0)r.edx=lo(r.eax,0u-v);else r.edx=r.eax;
    }
    r.eax=r.edx;
    if((uint16_t)r.eax&&s16(r.eax)<0x2d) {
        r.eax=lo(r.eax,0x2d);
        if(R16(A+0x2a0u)) {
            r.edx=lo(r.edx,R16(A+0x2a0u));
            const uint16_t rate=(uint16_t)r.edx;
            if(rate==0x8000u)r.ecx=lo(r.ecx,0x7fff);else if(s16(rate)<0)r.ecx=lo(r.edx,0u-rate);else r.ecx=r.edx;
            if(s16(r.ecx)<0x3c) {
                r.edx=lo(r.edx,0x3c);if(s16(R16(A+0x2a0u))<=0)r.edx+=0xffffff88u;
                W16(A+0x2a0u,r.edx);
            }
        }
    }
    /* Turn scale: full S+0 at 45 falling linearly to S+0 / 2 at S+4. */
    uint32_t scale;
    if(flag50||s16(r.eax)<0x2d)scale=0;
    else {
        r.edx=lo(r.edx,R16(S+4u));
        if(s16(r.eax)>s16(r.edx))r.eax=r.edx;
        r.ecx=sx(r.edx)-0x2du;r.eax=sx(r.eax)-0x2du;
        const uint32_t range=r.ecx,above=r.eax,half=fx_sar(range,1);
        if(i32(half)<i32(above)) {
            r.eax=sx(R16(S));r.edx=above-half;r.ecx=range-half;
            r.eax=(uint32_t)((int64_t)i32(r.eax)*i32(r.edx));r.edx=(uint32_t)fx_sar(r.eax,31);r.ecx+=r.ecx;
            r.edx=(uint32_t)(i32(r.eax)%i32(r.ecx));r.eax=(uint32_t)(i32(r.eax)/i32(r.ecx));
            r.ecx=lo(r.ecx,R16(S)-r.eax);scale=r.ecx;
        } else {
            vc_q16_divide(&r,0x32,0x32);
            vc_q16_multiply(&r,half,r.eax);
            r.ecx=r.eax;r.edx=above+r.ecx;r.eax=sx(R16(S));r.ecx+=half;
            r.eax=(uint32_t)((int64_t)i32(r.eax)*i32(r.edx));r.edx=(uint32_t)fx_sar(r.eax,31);
            r.edx=(uint32_t)(i32(r.eax)%i32(r.ecx));r.eax=(uint32_t)(i32(r.eax)/i32(r.ecx));
            scale=r.eax;
        }
    }
    r.eax=sx(scale);ARGS2(0x640,r.eax);CALL(0x47e0b0,2);
    r.ecx=sx(r.eax);r.edx=lo(r.edx,R16(S+8u));const uint32_t limit=r.edx;
    r.eax=sx(R16(turn));r.edx=r.eax;ARGS2(r.ecx,r.edx);CALL(0x47e0a0,2);
    a[1]=r.eax;a[2]=limit;r.eax=A+0x28cu;a[0]=r.eax;CALL(0x467adc,3);
    /* Heading word A+10h += turn rate A+28Ch; the matrix becomes invalid. */
    const uint16_t h0=R16(A+0xcu),h1=R16(A+0xeu),h2=R16(A+0x10u);
    r.ecx=lo(r.ecx,h1);
    const uint16_t heading=(uint16_t)(h2+R16(A+0x28cu));
    W32(A+0xcu,(uint32_t)h1<<16|h0);W16(A+0x10u,heading);W16(A+0x32u,0);
    return pair(A+0xcu,(uint32_t)h1<<16|heading);
}
