#include "es2/core/ai.h"
#include "fixed.h"

/* Vehicle AI: object line query and feeler obstacle steering.
 * (docs/reverse-engineering/ai.md). Frame buffers the callees receive live in
 * scratch at their original EBP offsets. */
static uint64_t pair(uint32_t a,uint32_t d) { return (uint64_t)d<<32|a; }
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
static int32_t s32(uint32_t x) { return (int32_t)fx_s32(x); }
/* One game call with the EAX/EDX/ECX the original holds at that call. */
static void ai_call(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,
                    es2_weapon_registers *r,uint32_t eax,uint32_t edx,uint32_t ecx) {
    r->eax=eax;r->edx=edx;r->ecx=ecx;
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}
#define CALL(va,n,e,d,c) ai_call(g,g->image+(va)-0x400000u,0,a,n,&r,(e),(d),(c))

/* Original: DBSIM.EXE 0x404ca0. The record at EBP-34h is the swept segment
 * of the swept query: +0 forward pose (EBP-54h: 492990 angles through
 * 47ebb8, translation start), +4 length 49288c(end, start), +8 radius word,
 * +0Ah inverse pose (transposed matrix, translation -start rotated). Byte 19
 * of both matrices is stack residue in the original (47ebb8 never writes
 * it); no reader exists in the 404bc0 subtree, so scratch keeps its zero. */
uint64_t es2_object_line_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t out,uint32_t start,
    uint32_t end,uint32_t radius,uint32_t filter,const uint32_t entry[3]) {
    const uint32_t f=g->scratch+0x70u,angles=f-8u,pose=f-0x54u,line=f-0x34u,back=f-0x60u;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[3];
    a[0]=angles;a[1]=end;a[2]=start;CALL(0x492990,3,angles,r.edx,r.ecx);
    W32(pose+0x1cu,R32(start+8u));W32(pose+0x14u,R32(start));
    uint32_t y=R32(start+4u);W32(pose+0x18u,y);
    a[0]=angles;a[1]=pose;CALL(0x47ebb8,2,y,pose,angles);
    W32(line,pose);
    a[0]=end;a[1]=start;CALL(0x49288c,2,pose,r.edx,r.ecx);
    W32(line+4u,r.eax);W16(line+8u,radius);
    for(unsigned i=0;i<20u;i+=4u)W32(line+0xau+i,R32(pose+i));
    W32(line+0x26u,R32(pose+0x1cu));
    uint32_t x=R32(pose+0x14u);W32(line+0x1eu,x);
    y=R32(pose+0x18u);W32(line+0x22u,y);
    a[0]=line+0xau;CALL(0x47df19,1,line+0xau,x,y);
    W32(line+0x26u,0u-R32(line+0x26u));W32(line+0x1eu,0u-R32(line+0x1eu));
    y=0u-R32(line+0x22u);W32(line+0x22u,y);
    W32(back,R32(line+0x1eu));W32(back+4u,y);W32(back+8u,R32(line+0x26u));
    a[0]=back;a[1]=line+0xau;a[2]=line+0x1eu;CALL(0x480304,3,line+0x1eu,line+0xau,back);
    /* Only DI of the filter slot is read; its high word is 480304's EAX. */
    a[0]=out;a[1]=line;a[2]=(r.eax&0xffff0000u)|(uint16_t)filter;CALL(0x404bc0,3,a[2],out,r.ecx);
    return pair(r.eax,r.edx);
}

/* Inlined "matrix of pose": 47ebb8 only while the word at pose+26h is 0. */
static void steer_matrix(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t actor,
                         es2_weapon_registers *r,uint32_t eax,uint32_t edx,uint32_t ecx) {
    if(R16(actor+0x32u))return;
    uint32_t a[2]={actor+0xcu,actor+0x12u};
    ai_call(g,g->image+0x7ebb8u,0,a,2,r,eax,edx,ecx);
    W16(actor+0x32u,1);
}
/* Heading of a target relative to the actor (AX minus heading word +10h):
 * |angle| (8000h counts as 7fffh) below the cone limit selects a side. */
static int steer_cone(uint32_t eax,uint32_t heading,int32_t limit,int *negative) {
    uint16_t angle=(uint16_t)(eax-heading);
    uint16_t size=angle==0x8000u?0x7fffu:fx_s16(angle)<0?(uint16_t)(0u-angle):angle;
    *negative=fx_s16(angle)<0;
    return fx_s16(size)<limit;
}
/* Original: DBSIM.EXE 0x416274. ESI ("left", first feeler at local X -1500)
 * and EDI ("right") start at 22000 and keep the nearest obstacle per side. */
uint64_t es2_obstacle_steer_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t actor,
    uint32_t turn,uint32_t speed,uint32_t flags,const uint32_t entry[3]) {
    const uint32_t b=g->image,f=g->scratch+0xf0u,pos=actor+0x26u,pose=actor+0xcu,mat=actor+0x12u;
    const uint32_t near=f-0xd0u,far=f-0xc4u,far2=f-0xe8u,first=f-0xacu,second=f-0xb8u,hit=f-0xdcu,found=f-0x10u;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[6],eax=entry[0];
    if(!R16(actor+0x28eu)) { eax=speed;if(!R16(speed))return pair(speed,entry[1]); }
    if(fx_s16(R16(speed))<0&&!R32(actor+0x26eu))return pair(eax,speed);
    /* The word at EBP-2 is the constant 0: no half-turn offset, and the
     * feeler length word is (B1E0h + 9C40h) & FFFFh = 20000. */
    int32_t left=0x55f0,right=0x55f0;
    if(!(flags&1u)) {
        uint32_t map=R32(b+0xa0be0u);a[0]=map;a[1]=pos;CALL(0x46e180,2,eax&0xffff0000u,pos,map);
        const uint32_t h=r.eax;
        W32(far,0xffffd8f0u);W32(far+4u,20000);W32(far+8u,0);
        W32(near,0xfffffa24u);W32(near+4u,0);W32(near+8u,0);
        steer_matrix(m,g,actor,&r,pose,pose,mat);
        a[0]=near;a[1]=mat;a[2]=first;CALL(0x48043c,3,first,mat,near);
        steer_matrix(m,g,actor,&r,mat,pose,pose);
        a[0]=far;a[1]=mat;a[2]=second;CALL(0x48043c,3,far,second,mat);
        W32(second+8u,h);W32(first+8u,h);
        a[0]=found;a[1]=first;a[2]=second;a[3]=0;a[4]=0xffffffffu;CALL(0x404ca0,5,found,h,first);
        if((uint16_t)r.eax&&left>s32(R32(found)))left=s32(R32(found));
        map=R32(b+0xa0be0u);a[0]=map;a[1]=first;a[2]=second;a[3]=0;a[4]=hit;a[5]=1;CALL(0x46e980,6,map,second,first);
        if((uint8_t)r.eax) {
            a[0]=first;a[1]=hit;CALL(0x4928d0,2,r.eax,hit,first);
            if(left>s32(r.eax))left=s32(r.eax);
        }
        W32(far2,10000);W32(far2+4u,20000);W32(far2+8u,0);W32(near,1500);
        steer_matrix(m,g,actor,&r,mat,pose,pose);
        a[0]=near;a[1]=mat;a[2]=first;CALL(0x48043c,3,near,first,mat);
        steer_matrix(m,g,actor,&r,pose,mat,pose);
        a[0]=far2;a[1]=mat;a[2]=second;CALL(0x48043c,3,mat,far2,second);
        W32(second+8u,h);W32(first+8u,h);
        a[0]=found;a[1]=first;a[2]=second;a[3]=0;a[4]=0xffffffffu;CALL(0x404ca0,5,found,first,h);
        if((uint16_t)r.eax&&right>s32(R32(found)))right=s32(R32(found));
        map=R32(b+0xa0be0u);a[0]=map;a[1]=first;a[2]=second;a[3]=0;a[4]=hit;a[5]=1;CALL(0x46e980,6,map,second,first);
        if((uint8_t)r.eax) {
            a[0]=first;a[1]=hit;CALL(0x4928d0,2,r.eax,hit,first);
            if(right>s32(r.eax))right=s32(r.eax);
        }
        /* Vehicles within 45 degrees, scaled distance 2000/1024. */
        uint32_t cursor=R32(b+0xa9af8u);uint16_t index=0;
        for(;;) {
            uint16_t old=index++;uint32_t object=0;
            if(fx_s16(old)<fx_s16(R16(b+0xa9afeu))) { object=R32(cursor);cursor+=4u; }
            if(!object)break;
            if(object==actor||R32(R32(object+0x45u)+0x14u))continue;
            uint32_t table=R32(object);a[0]=object;
            ai_call(g,R32(table+0x7cu),0x7c,a,1,&r,table,0,object);
            if(!(uint16_t)r.eax)continue;
            a[0]=pos;a[1]=object+0x26u;CALL(0x49288c,2,pos,object,object+0x26u);
            uint32_t distance=r.eax;a[0]=0x7d0;a[1]=distance;CALL(0x47e0b0,2,distance,distance,r.ecx);
            int32_t v=s32(r.eax);int negative;
            if(left<=v&&right<=v)continue;
            a[0]=object+0x26u;a[1]=pos;CALL(0x492934,2,pos,object+0x26u,object+0x26u);
            if(!steer_cone(r.eax,R16(actor+0x10u),0x2000,&negative))continue;
            if(negative) { if(right>v)right=v; }
            else if(left>v)left=v;
        }
    }
    /* Avoidance points 4a9b88 (8-byte X/Y entries, count 4a9cc8) when the
     * byte at [[[actor+45h]+0Ch]]+0A3h is set: 67.5 degrees, 1000/1024. */
    int point_right=0,point_left=0;
    uint32_t p=R32(R32(R32(actor+0x45u)+0xcu))+0x92u,result=p,residue=actor+0x92u;
    if(R8(p+0x11u)) {
        uint32_t point=b+0xa9b88u;
        for(uint32_t i=0;;++i,point+=8u) {
            result=i;if(s32(i)>=s32(R32(b+0xa9cc8u)))break;
            a[0]=pos;a[1]=point;CALL(0x4928d0,2,point,point,pos);
            uint32_t distance=r.eax;a[0]=0x3e8;a[1]=distance;CALL(0x47e0b0,2,distance,r.edx,r.ecx);
            int32_t v=s32(r.eax);int negative;
            if(left<=v&&right<=v)continue;
            a[0]=point;a[1]=pos;CALL(0x492934,2,point,point,pos);
            if(!steer_cone(r.eax,R16(actor+0x10u),0x3000,&negative))continue;
            if(negative) { if(right>v) { point_right=1;right=v; } }
            else if(left>v) { point_left=1;left=v; }
        }
    }
    /* Nothing found: byte actor+0AEh shortens one side by a quarter. */
    if(R8(actor+0xaeu)&&left==0x55f0&&right==0x55f0) {
        if(fx_s16(R16(actor+0x254u))>0) { result=0x157c;left-=0x157c; }
        else { residue=0x157c;right-=0x157c; }
    }
    if(left>=0x55f0&&right>=0x55f0)return pair(result,residue);
    if(R8(actor+0xaeu)&&left<0x55f0&&right<0x55f0)left=s32((uint32_t)left+(uint32_t)fx_s16(R16(actor+0x254u)));
    const unsigned nearer_right=right<left;
    const int32_t nearest=nearer_right?right:left;
    const int point_hit=nearer_right?point_right:point_left;
    const int32_t limit=point_hit?3000:0,gain=point_hit?1000:700;
    /* IMUL leaves the full product in EDX:EAX, but CDQ replaces EDX with the
     * sign of EAX before IDIV: only the low dword is divided. */
    const int64_t product=fx_s32(0x55f0u-(uint32_t)nearest)*gain;
    const uint32_t quotient=(uint32_t)(int32_t)(fx_s32((uint32_t)product)/22000);
    const uint32_t step=nearer_right?quotient:(quotient&0xffff0000u)|(uint16_t)(0u-quotient);
    W16(turn,R16(turn)+step);
    if(nearest>=limit)return pair(turn,step);
    /* EBP-2 is 0 and SETL repeats the test just passed, so 41691a (half the
     * slow speed) never runs: the speed word becomes [[actor+1F2h]+4]. */
    W16(speed,R16(R32(actor+0x1f2u)+4u));
    return pair(speed,1);
}
