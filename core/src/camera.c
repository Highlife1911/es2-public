#include "es2/core/camera.h"
#include "fixed.h"

#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
/* EBP-k of the original frame. */
#define F(k) (frame-(uint32_t)(k))

static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
/* A 16-bit load into a 32-bit register keeps its high word. */
static uint32_t low(uint32_t reg,uint32_t w) { return (reg&0xffff0000u)|(w&0xffffu); }
static uint32_t sx16(uint32_t w) { return (uint32_t)fx_s16(w); }
/* 47e068 (a << 16) / b and 47e08d (a * b) >> 16, signed 64-bit, low dword. */
static uint32_t q16_div(uint32_t a,uint32_t b) { return (uint32_t)(int32_t)(fx_s32(a)*65536/fx_s32(b)); }
static uint32_t q16_mul(uint32_t a,uint32_t b) { return (uint32_t)((uint64_t)(fx_s32(a)*fx_s32(b))>>16); }

static void call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    uint32_t f=g->image+va-0x400000u;
    if(g->observe)g->observe(g->observer_context,f,0);
    g->call(g->context,f,a,n,r);
}
static void call2(const es2_weapon_graph *g,uint32_t va,uint32_t a0,uint32_t a1,es2_weapon_registers *r) {
    uint32_t a[2]={a0,a1};call(g,va,a,2,r);
}
static void call3(const es2_weapon_graph *g,uint32_t va,uint32_t a0,uint32_t a1,uint32_t a2,es2_weapon_registers *r) {
    uint32_t a[3]={a0,a1,a2};call(g,va,a,3,r);
}
/* Lazily built target matrix: angles at T+0c, matrix at T+12, flag word T+32. */
static int target_dirty(const es2_guest_memory *m,uint32_t e) { return !R16(e+0x26u); }
static uint32_t clamp_pitch(uint32_t t) {
    return fx_s32(t)>=0x3000?0x3000u:fx_s32(t)<=-0x3000?0xffffd000u:t;
}
static int pitch_inside(uint32_t t) { return fx_s32(t)<=0x3000&&fx_s32(t)>=-0x3000; }

/* Original: DBSIM.EXE 0x4011a0 */
uint64_t es2_camera_update_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t c) {
    uint32_t b=g->image-0x400000u,frame=g->scratch+0x1c0u,map=b+0x4a0be0u,slot=b+0x4a8048u;
    es2_weapon_registers r;
    /* Ring entry: position (followed object +26, 12 bytes) and angles (+0c, 6 bytes). */
    uint32_t ring=R32(slot)*12u+b+0x4a7bfcu,from=R32(b+0x4d24e6u)+0x26u;
    W32(ring+8u,R32(from+8u));W32(ring,R32(from));W32(ring+4u,R32(from+4u));
    uint32_t index3=R32(slot)*3u,player=R32(b+0x4d24e6u),ecx=R32(player+0xcu);
    W32(index3*2u+b+0x4a7e54u,ecx);ecx=low(ecx,R16(player+0x10u));W16(index3*2u+b+0x4a7e58u,ecx);
    r.eax=index3;r.edx=player;r.ecx=ecx;
    uint32_t a1=0x400;call(g,0x467924,&a1,1,&r);
    uint32_t step=sx16(r.eax);
    W32(R32(slot)*4u+b+0x4a7f80u,step);
    uint32_t next=(uint32_t)(int32_t)(fx_s32(R32(slot)+1u)%50);
    W32(slot,next);
    uint32_t mode=sx16(R16(c+0x36u));
    if(mode==0) {
        /* Free flight: angles += deltas (pitch clamped), then move by
         * (0, speed C+24, 0) in the camera frame; stay 226h above the
         * terrain plus 500/1024 of the face slope. */
        uint32_t speed=sx16(R16(c+0x24u));
        W32(F(0xdc),0);W32(F(0xd8),speed);W32(F(0xd4),0);
        uint32_t ax=R16(c+0x10u)+R16(c+0x2cu),ay=R16(c+0x12u)+R16(c+0x2eu),az=R16(c+0x14u)+R16(c+0x30u);
        uint32_t t=sx16(ax);
        if(!pitch_inside(t)) { ax=clamp_pitch(t);W16(c+0x2cu,0); }
        W32(c+0x10u,(ax&0xffffu)|(ay&0xffffu)<<16);W16(c+0x14u,az);
        W32(F(0xec),R32(c+0xcu));
        uint32_t px=R32(c+4u);W32(F(0xf4),px);W32(F(0xf0),R32(c+8u));
        r.eax=c+0x10u;r.edx=px;r.ecx=F(0x108);call2(g,0x47ebb8,c+0x10u,F(0x108),&r);
        r.eax=F(0xdc);r.edx=F(0xe8);r.ecx=F(0x108);call3(g,0x48043c,F(0xdc),F(0x108),F(0xe8),&r);
        uint32_t terrain=R32(map);
        r.edx=c+4u;r.ecx=terrain;call2(g,0x46e498,terrain,c+4u,&r);
        uint32_t face=r.eax,slope=0;
        if(face) {
            uint32_t nx=sx16(R16(face)),ny=sx16(R16(face+2u));
            r.eax=nx;r.edx=nx;r.ecx=ny;call2(g,0x47de4c,nx,ny,&r);
            slope=sx16(r.eax);
        }
        terrain=R32(map);
        r.eax=c+4u;r.edx=slope;r.ecx=terrain;call2(g,0x46e180,terrain,c+4u,&r);
        uint32_t height=r.eax;
        r.eax=slope;r.edx=slope;call2(g,0x47e0b0,0x1f4,slope,&r);
        height+=r.eax+0x226u;
        if(fx_s32(height)>fx_s32(R32(F(0xe0))))W32(F(0xe0),height);
        W32(c+0xcu,R32(F(0xe0)));W32(c+4u,R32(F(0xe8)));
        uint32_t y=R32(F(0xe4));W32(c+8u,y);
        return pair(c+4u,y);
    }
    if(mode==1) {
        /* Orbit: distance C+22 += C+24 (2500..30000), orbit angles C+26 +=
         * deltas (pitch clamped), yaw relative to the target; position =
         * target transform of offset C+38 plus the rotated (0,-distance,0);
         * above the terrain, and a positive pitch delta that would sink
         * the camera is undone once. */
        uint32_t old_pitch=R16(c+0x10u);
        int32_t distance=fx_s16(R16(c+0x22u)+R16(c+0x24u));
        W16(c+0x22u,distance>=30000?30000:distance<=2500?2500:distance);
        W16(c+0x2au,R16(c+0x2au)+R16(c+0x30u));
        W16(c+0x26u,R16(c+0x26u)+R16(c+0x2cu));
        W16(c+0x28u,R16(c+0x28u)+R16(c+0x2eu));
        W32(F(0x30),R32(c+0x26u));W16(F(0x2c),R16(c+0x2au));
        uint32_t t=sx16(R16(F(0x30)));
        if(!pitch_inside(t)) { uint32_t v=clamp_pitch(t);W16(c+0x26u,v);W16(F(0x30),v);W16(c+0x2cu,0); }
        uint32_t yaw=R16(R32(c+0x32u)+0x10u);W16(F(0x2c),R16(F(0x2c))+yaw);
        r.eax=F(0x134);r.edx=F(0x30);r.ecx=yaw;call2(g,0x47ebb8,F(0x30),F(0x134),&r);
        W32(c+0x10u,R32(F(0x30)));W16(c+0x14u,R16(F(0x2c)));
        uint32_t back=0u-sx16(R16(c+0x22u));
        W32(F(0x14c),0);W32(F(0x148),back);W32(F(0x144),0);
        r.eax=F(0x14c);r.edx=F(0x114);r.ecx=F(0x134);call3(g,0x480304,F(0x14c),F(0x134),F(0x114),&r);
        uint32_t e=R32(c+0x32u)+0xcu;
        if(target_dirty(m,e)) { r.eax=e+6u;r.edx=e;call2(g,0x47ebb8,e,e+6u,&r);W16(e+0x26u,1); }
        e+=6u;
        r.eax=c+0x38u;r.edx=F(0x120);r.ecx=e;call3(g,0x4801d4,c+0x38u,e,F(0x120),&r);
        W32(c+0xcu,R32(F(0x118)));W32(c+4u,R32(F(0x120)));W32(c+8u,R32(F(0x11c)));
        W32(c+0xcu,R32(c+0xcu)+R32(F(0x10c)));
        uint32_t rx=R32(F(0x114)),ry=R32(F(0x110));
        W32(c+4u,R32(c+4u)+rx);W32(c+8u,R32(c+8u)+ry);
        uint32_t terrain=R32(map);
        r.eax=terrain;r.edx=rx;r.ecx=ry;call2(g,0x46e180,terrain,c+4u,&r);
        uint32_t floor=r.eax+0x226u;
        if(fx_s32(floor)<=fx_s32(R32(c+0xcu)))return pair(floor,r.edx);
        uint32_t x=R32(c+4u),y=R32(c+8u);
        W32(c+0xcu,floor);W32(c+4u,x);W32(c+8u,y);
        if(fx_s16(R16(c+0x2cu))<=0)return pair(c+4u,y);
        W16(c+0x26u,R16(c+0x26u)-R16(c+0x2cu));W16(c+0x2cu,0);
        ecx=low(x,old_pitch);W16(F(0x30),ecx);
        r.eax=F(0x134);r.edx=F(0x30);r.ecx=ecx;call2(g,0x47ebb8,F(0x30),F(0x134),&r);
        W32(c+0x10u,R32(F(0x30)));W16(c+0x14u,R16(F(0x2c)));
        back=0u-sx16(R16(c+0x22u));
        W32(F(0x164),0);W32(F(0x160),back);W32(F(0x15c),0);
        r.eax=F(0x164);r.edx=F(0x114);r.ecx=F(0x134);call3(g,0x480304,F(0x164),F(0x134),F(0x114),&r);
        e=R32(c+0x32u)+0xcu;r.edx=e;r.ecx=e;
        if(target_dirty(m,e)) { r.eax=e;r.edx=e+6u;r.ecx=e;call2(g,0x47ebb8,e,e+6u,&r);W16(e+0x26u,1); }
        r.eax=F(0x120);r.edx=e+6u;r.ecx=c+0x38u;call3(g,0x4801d4,c+0x38u,e+6u,F(0x120),&r);
        W32(c+0xcu,R32(F(0x118)));W32(c+4u,R32(F(0x120)));W32(c+8u,R32(F(0x11c)));
        W32(c+0xcu,R32(c+0xcu)+R32(F(0x10c)));W32(c+4u,R32(c+4u)+R32(F(0x114)));
        ry=R32(F(0x110));W32(c+8u,R32(c+8u)+ry);
        terrain=R32(map);
        r.eax=terrain;r.edx=c+4u;r.ecx=ry;call2(g,0x46e180,terrain,c+4u,&r);
        floor=r.eax+0x226u;
        if(fx_s32(floor)<=fx_s32(R32(c+0xcu)))return pair(floor,r.edx);
        x=R32(c+4u);y=R32(c+8u);
        W32(c+0xcu,floor);W32(c+4u,x);W32(c+8u,y);
        return pair(c+4u,y);
    }
    if(mode==2) {
        /* Attached: optional mount matrix from the target's virtual +24
         * composed with the target matrix; else the target matrix itself. */
        uint32_t target=R32(c+0x32u),vtable=R32(target),method=R32(vtable+0x24u);
        r.eax=vtable;r.edx=next;r.ecx=target;
        if(g->observe)g->observe(g->observer_context,method,0x24);
        g->call(g->context,method,&target,1,&r);
        uint32_t mount=r.eax;r.ecx=target;
        if(mount) {
            uint32_t e=R32(c+0x32u)+0xcu;r.edx=e;r.ecx=e;
            if(target_dirty(m,e)) { r.eax=e;r.edx=e+6u;r.ecx=e;call2(g,0x47ebb8,e,e+6u,&r);W16(e+0x26u,1); }
            r.eax=F(0x190);r.edx=e+6u;r.ecx=mount;call3(g,0x47fa20,mount,e+6u,F(0x190),&r);
            r.eax=c+0x3eu;r.edx=F(0x19c);r.ecx=F(0x190);call3(g,0x4801d4,c+0x3eu,F(0x190),F(0x19c),&r);
            r.edx=F(0x88);r.ecx=F(0x190);call2(g,0x47f9a0,F(0x190),F(0x88),&r);
            W16(F(0x84),R16(F(0x84))+R16(c+0x48u));
            W16(F(0x88),R16(F(0x88))+R16(c+0x44u));
            W16(F(0x86),R16(F(0x86))+R16(c+0x46u));
            W32(c+0xcu,R32(F(0x194)));W32(c+4u,R32(F(0x19c)));
            uint32_t y=R32(F(0x198));W32(c+8u,y);
            W32(c+0x10u,R32(F(0x88)));W16(c+0x14u,R16(F(0x84)));
            return pair(c+4u,y);
        }
        uint32_t e=R32(c+0x32u)+0xcu;
        if(target_dirty(m,e)) { r.eax=e+6u;r.edx=e;call2(g,0x47ebb8,e,e+6u,&r);W16(e+0x26u,1); }
        r.eax=c+0x3eu;r.edx=F(0x1a8);r.ecx=e+6u;call3(g,0x4801d4,c+0x3eu,e+6u,F(0x1a8),&r);
        W32(c+0xcu,R32(F(0x1a0)));W32(c+4u,R32(F(0x1a8)));
        uint32_t y=R32(F(0x1a4));W32(c+8u,y);
        e=R32(c+0x32u)+0xcu;
        uint32_t angles=R32(e);W32(c+0x10u,angles);
        angles=low(angles,R16(e+4u));W16(c+0x14u,angles);
        return pair(angles,y);
    }
    if(mode==3) {
        /* Replay: walk back from the newest step until 8000 units are
         * consumed, then interpolate between that ring entry and the next. */
        uint32_t i=R32(slot),left=0x1f40,s;
        for(;;) {
            if(fx_s32(--i)<0)i=0x31;
            s=R32(i*4u+b+0x4a7f80u);
            if(fx_s32(left)<=fx_s32(s))break;
            left-=s;
        }
        uint32_t f=q16_div(left,R32(i*4u+b+0x4a7f80u)),k=i+1u;
        if(fx_s32(k)>=0x32)k=0;
        uint32_t na=k*12u+b+0x4a7bfcu,oa=i*12u+b+0x4a7bfcu;
        uint32_t x=R32(na),y=R32(na+4u),z=R32(na+8u);
        uint32_t dx=R32(oa)-x,dy=R32(oa+4u)-y,dz=R32(oa+8u)-z;
        x+=q16_mul(dx,f);y+=q16_mul(dy,f);z+=q16_mul(dz,f);
        uint32_t ng=k*6u+b+0x4a7e54u,og=i*6u+b+0x4a7e54u;
        uint32_t gx=R16(ng),gy=R16(ng+2u),gz=R16(ng+4u);
        uint32_t hx=R16(og)-gx,hy=R16(og+2u)-gy,hz=R16(og+4u)-gz;
        gx+=q16_mul(sx16(hx),f);gy+=q16_mul(sx16(hy),f);gz+=q16_mul(sx16(hz),f);
        W32(c+0xcu,z);W32(c+4u,x);W32(c+8u,y);
        uint32_t angles=(gx&0xffffu)|(gy&0xffffu)<<16;W32(c+0x10u,angles);W16(c+0x14u,gz);
        return pair(low(angles,gz),y);
    }
    return pair(mode-3u,next);
}
