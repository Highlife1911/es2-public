#include "es2/core/world.h"
#include "fixed.h"
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define W32(a,v) m->write32(m->context,(a),(v))
#define VA(b,x) ((b)+(x)-0x400000u)
static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static int32_t s32(uint32_t x) { return (int32_t)fx_s32(x); }
static int side(uint32_t difference) { return s32(difference)<0?-1:s32(difference)>0?1:0; }
/* CDQ/IDIV of the low product dword; the INT_MIN / -1 fault of the
 * original is outside the contract (the int64 quotient keeps C defined). */
static uint32_t quotient(uint32_t dividend,uint32_t divisor) {
    return (uint32_t)(int64_t)((int64_t)s32(dividend)/(int64_t)s32(divisor));
}

/* Original: DBSIM.EXE 0x472bcc */
uint64_t es2_poly_clip_registers(const es2_guest_memory *m,uint32_t keep,uint32_t axis,
    uint32_t line,uint32_t in,uint32_t out) {
    if(s32(R32(in))<=0) { W32(out,0); return pair(0,0); }
    uint32_t count=R32(in),points=R32(in+4u),last=(count<<3)+points-8u;
    uint32_t sx=R32(last),sy=R32(last+4u);
    int x_line=(uint8_t)axis!=0,orientation=(uint8_t)keep?1:-1;
    int before=orientation*side((x_line?sx:sy)-line);
    uint32_t cursor=R32(out+4u),k=0,py=0;
    for(uint32_t p=R32(in+4u),n=R32(in);s32(n)>0;--n,p+=8u) {
        uint32_t px=R32(p);py=R32(p+4u);
        int now=orientation*side((x_line?px:py)-line);
        int cross=now>=0?before<0&&now>0:before>0,point=now>=0;
        if(cross) {
            if(x_line) {
                W32(cursor,line);
                W32(cursor+4u,quotient((line-sx)*(py-sy),px-sx)+sy);
            } else {
                W32(cursor+4u,line);
                W32(cursor,quotient((line-sy)*(px-sx),py-sy)+sx);
            }
            ++k;cursor+=8u;
        }
        if(point) { W32(cursor,px);W32(cursor+4u,py);++k;cursor+=8u; }
        before=now;sx=px;sy=py;
    }
    W32(out,k);
    return pair((py&0xffffff00u)|(k&0xffu),out);
}

/* Original: DBSIM.EXE 0x472b14 */
uint64_t es2_poly_spans_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t polygon,uint32_t spans,uint32_t extent) {
    uint32_t points=R32(polygon+4u),minx=R32(points),miny=R32(points+4u),first=minx;
    uint32_t e=points+8u;
    for(uint32_t n=R32(polygon)-1u;s32(n)>0;--n,e+=8u) {
        uint32_t x=R32(e);if(s32(x)<s32(minx))minx=x;
        uint32_t y=R32(e+4u);if(s32(y)<s32(miny))miny=y;
    }
    uint32_t p=R32(polygon+4u),n=R32(polygon),ecx=s32(n)>0?miny:first;
    for(;s32(n)>0;--n,p+=8u) { W32(p,R32(p)-minx);W32(p+4u,R32(p+4u)-miny); }
    es2_weapon_registers r={R32(polygon+4u),R32(polygon),ecx};
    uint32_t a[3]={r.edx,r.eax,spans};
    if(g->observe)g->observe(g->observer_context,0x4930c1u,0);
    g->call(g->context,VA(g->image,0x4930c1u),a,3,&r);
    uint32_t y0=R32(spans)+miny;
    W32(extent,y0);
    W32(extent+4u,R32(spans+4u)+y0-1u);
    W32(extent+8u,R32(spans+4u));
    W32(extent+12u,spans+8u);
    uint32_t q=spans+8u,left=R32(spans+4u)*2u;
    for(;s32(left)>0;--left,q+=4u)W32(q,R32(q)+minx);
    return pair(q,left);
}

/* Original: DBSIM.EXE 0x46d324 */
uint64_t es2_view_rect_registers(const es2_guest_memory *m,uint32_t b,uint32_t map,uint32_t polygon) {
    uint32_t shift=R32(map+0x108u)&31u,r=R32(map+0x10cu)<<shift;
    uint32_t ex=R32(VA(b,0x6b4f44u)),ey=R32(VA(b,0x6b4f48u));
    uint32_t x0=ex-r,y0=ey-r,ox=R32(map+0xf4u),oy=R32(map+0xf8u);
    if(!(s32(x0)>s32(ox)))x0=ox;
    if(!(s32(y0)>s32(oy)))y0=oy;
    uint32_t xmax=(((1u<<(R32(map+0x100u)&31u))-2u)<<shift)+ox;
    uint32_t ymax=(((1u<<(R32(map+0x104u)&31u))-2u)<<shift)+oy;
    uint32_t x1=ex+r,y1=ey+r;
    if(!(s32(x1)<=s32(xmax)))x1=xmax;
    if(!(s32(y1)<=s32(ymax)))y1=ymax;
    if(s32(x1)<s32(x0)||s32(y1)<s32(y0)) { W32(polygon,0); return pair(polygon,0); }
    W32(polygon,4);
    uint32_t p=R32(polygon+4u);
    W32(p,x0);W32(p+4u,y0);W32(p+0x18u,x1);W32(p+0x1cu,y1);
    W32(p+0xcu,x0);W32(p+0x10u,y1);W32(p+0x24u,x1);W32(p+0x28u,y0);
    uint32_t z=R32(map+0xfcu);
    W32(p+0x2cu,z);W32(p+0x20u,z);W32(p+0x14u,z);W32(p+8u,z);
    return pair(polygon,z);
}

/* Original: DBSIM.EXE 0x46bdf8 */
uint64_t es2_cell_uv_registers(const es2_guest_memory *m,uint32_t b,uint32_t cell,
    uint32_t u,uint32_t v,uint32_t out) {
    uint32_t e=R32(cell)*20u+R32(VA(b,0x6b4f40u)),level=R32(cell+4u),u0,v0,u1,v1;
    if(level) {
        uint32_t k=(R32(R32(VA(b,0x4a0be0u))+0x108u)+level-13u)&31u,size=1u<<k;
        uint32_t w=(v<<k)&0xffu;
        u0=(u<<k)&0xffu;u1=size+u0;v0=0u-w;v1=0u-(size+w);
    } else {
        u0=R32(e);v0=R32(e+4u);u1=R32(e+8u);v1=R32(e+12u);
    }
    W32(out+8u,u0);W32(out,u0);W32(out+0x1cu,v0);W32(out+4u,v0);
    W32(out+0x18u,u1);W32(out+0x10u,u1);W32(out+0x14u,v1);W32(out+0xcu,v1);
    return pair((uint32_t)fx_s16(R16(e+0x10u)),e);
}
