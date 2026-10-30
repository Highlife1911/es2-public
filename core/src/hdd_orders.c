#include "es2/core/hdd_orders.h"
#include "fixed.h"
/* The HDD map order controls (docs/reverse-engineering/cockpit.md, "HDD map
 * order controls"). Every guest read and write is done in the original's order
 * and repeated where the original repeats it, so callee writes and objects that
 * overlap the map are seen as by the original. Game calls go through g->call
 * with the original's registers. */
#define R32(a) m->read32(m->context,(a))
#define W32(a,v) m->write32(m->context,(a),(v))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define GVA(x) (g->image+(uint32_t)(x)-0x400000u)
#define NONE 0xffffffffu
static int32_t s32(uint32_t x) { return (int32_t)fx_s32(x); }
static void ho_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,GVA(va),a,n,r);
}
/* The map gadget's redraw request: G+10Fh = 1, G+B0h = 2. */
static void redraw(const es2_guest_memory *m,uint32_t gadget) {
    W8(gadget+0x10fu,1u);
    W8(gadget+0xb0u,2u);
}
static uint32_t map_gadget(const es2_guest_memory *m,const es2_weapon_graph *g) {
    return R32(R32(GVA(0x49d6b0u))+8u);
}

/* Original: DBSIM.EXE 0x44d9f4 */
uint64_t es2_map_mode_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t map,
    uint32_t mode,const uint32_t entry[3]) {
    const uint32_t gadget=map_gadget(m,g);
    es2_weapon_registers r={mode-0xfu,entry[1],entry[2]};
    const uint32_t self[1]={map};
    if(mode-0xbu<4u) {
        /* 44da14: pick modes 0Bh..0Eh: cursor 2, the pick label (text 3
         * for mode 0Bh, else 2), pick flags. */
        const uint32_t shape[1]={2u};
        ho_call(g,0x433e3cu,shape,1,&r);
        const uint32_t k=mode==0xbu?3u:2u;
        const uint32_t text=R32(GVA(0x4d1638u)+4u*k);
        const uint32_t label[3]={map,text,0u};
        r.eax=k;r.edx=text;r.ecx=2u;
        ho_call(g,0x44dc6cu,label,3,&r);
        W8(map+0x103u,1u);
        W8(map+0x380u,1u);
        W8(map+0x104u,0u);
    } else {
        /* 44da51: cursor 0, flags, then the label update 44dd14. */
        const uint32_t shape[1]={0u};
        ho_call(g,0x433e3cu,shape,1,&r);
        W8(map+0x103u,0u);
        W8(map+0x104u,1u);
        W8(map+0x380u,0u);
        r.ecx=0u;
        ho_call(g,0x44dd14u,self,1,&r);
    }
    /* 44da75: a change keeps the previous mode and requests the redraw. */
    const uint32_t old=R32(map+0x5du);
    if(mode!=old) {
        W32(map+0x81u,old);
        W32(map+0x5du,mode);
        redraw(m,gadget);
    }
    return (uint64_t)r.edx<<32|old;
}

/* Original: DBSIM.EXE 0x44da98 */
uint64_t es2_wingman_select_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t map,
    uint32_t index,const uint32_t entry[3]) {
    const uint32_t old=R32(map+0x105u);
    es2_weapon_registers r={old,entry[1],entry[2]};
    if(old!=NONE) {
        /* 44daaa: toggle the old wingman's button; with a map mode set,
         * leave it and clear the old record's pending byte and the flags. */
        const uint32_t toggle[2]={map,old};
        ho_call(g,0x44ede0u,toggle,2,&r);
        if(R32(map+0x5du)!=NONE) {
            const uint32_t leave[2]={map,NONE};
            ho_call(g,0x44d9f4u,leave,2,&r);
            const uint32_t e=R32(map+0x105u); /* 44dac2: reread */
            W8(map+42u*e+0xa6u,0u);
            W8(map+0x104u,0u);
            W8(map+0x381u,0u);
            W8(map+0x144u,1u);
            W32(map+0x15cu,0u);
            W8(map+0x103u,0u);
            r.eax=0u;r.edx=21u*e;r.ecx=e;
        }
    }
    /* 44db01: the new selection. */
    W32(map+0x105u,index);
    r.eax=index;
    const uint32_t self[1]={map};
    if(index!=NONE) {
        const uint32_t toggle[2]={map,index};
        ho_call(g,0x44ede0u,toggle,2,&r);
        ho_call(g,0x44ee00u,self,1,&r);
        W8(map+0x381u,1u);
    } else {
        ho_call(g,0x44ee24u,self,1,&r);
    }
    /* 44db30: the gadget is read only now. */
    const uint32_t holder=R32(GVA(0x49d6b0u));
    const uint32_t gadget=R32(holder+8u);
    redraw(m,gadget);
    return (uint64_t)holder<<32|gadget;
}

/* Original: DBSIM.EXE 0x44dc10 */
uint64_t es2_order_cancel_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t map,
    const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    const uint32_t leave[2]={map,NONE};
    ho_call(g,0x44d9f4u,leave,2,&r);
    const uint32_t e=R32(map+0x105u);
    W8(map+42u*e+0xa6u,0u);
    r.eax=21u*e;r.edx=e;
    ho_call(g,0x44da98u,leave,2,&r);
    W8(map+0x104u,0u);
    W8(map+0x381u,0u);
    W8(map+0x144u,1u);
    W32(map+0x15cu,0u);
    W8(map+0x103u,0u);
    return (uint64_t)r.edx<<32|r.eax;
}

/* Original: DBSIM.EXE 0x44d450 */
uint64_t es2_map_areas_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t map,
    const uint32_t entry[3]) {
    const uint32_t code=R32(map+0x14eu);
    const uint32_t point=map+0x152u;
    es2_weapon_registers r={code,entry[1],entry[2]};
    if(code==R32(map+0x35u)) {
        /* 44d477: the eight mode buttons, rectangle at button+4. */
        uint32_t i=0;
        for(;;) {
            const uint32_t slot=map+0x3du+4u*i;
            const uint32_t rect=R32(slot)+4u;
            r.eax=rect;r.edx=i;r.ecx=slot;
            if(s32(R32(point+4u))>s32(R32(rect+4u))&&s32(R32(point+4u))<s32(R32(rect+0xcu))&&
               s32(R32(point))>s32(R32(rect))&&s32(R32(point))<s32(R32(rect+8u))) break;
            if(++i>=8u) { r.edx=8u;r.ecx=map+0x5du;i=8u;break; }
        }
        if(i<8u) {
            const uint32_t slot=map+0x3du+4u*i;
            if(R32(map+0x105u)==NONE) {
                /* 44d557: no wingman selected: the label with text 0. */
                const uint32_t text=R32(GVA(0x4d1638u));
                const uint32_t label[3]={map,text,1u};
                r.ecx=text;
                ho_call(g,0x44dc6cu,label,3,&r);
            } else {
                const uint32_t mode=i+0xau;
                r.eax=mode;
                if(mode!=R32(map+0x5du)) {
                    /* 44d4e7: a new map mode. */
                    const uint32_t set[2]={map,mode};
                    r.edx=mode;r.ecx=slot;
                    ho_call(g,0x44d9f4u,set,2,&r);
                } else if(R8(map+0x15bu)&&R8(map+0x104u)) {
                    /* 44d4d5: the same mode again with a changed record
                     * presses the HDD's send button 0Dh. */
                    const uint32_t hdd=R32(map+8u);
                    const uint32_t press[3]={hdd,0xdu,1u};
                    r.edx=hdd;r.ecx=slot;
                    ho_call(g,0x438d9cu,press,3,&r);
                }
                if(R8(map+0x15au)) {
                    /* 44d4fd: warp the mouse to the map centre (an 8-byte
                     * local in the scratch). */
                    const uint32_t x0=R32(map+0x71u),x1=R32(map+0x79u);
                    const uint32_t base=R32(R32(GVA(0x49b088u))+4u);
                    uint32_t x=fx_sar(x1-x0,1)+x0;
                    const uint32_t y0=R32(map+0x75u),y1=R32(map+0x7du);
                    uint32_t y=fx_sar(y1-y0,1)+y0;
                    x+=R32(base+0x210u);
                    y+=R32(base+0x214u);
                    const uint32_t ox=R32(base+0x220u);
                    x-=ox;
                    const uint32_t oy=R32(base+0x224u);
                    y-=oy;
                    const uint32_t local=g->scratch;
                    W32(local,x);W32(local+4u,y);
                    const uint32_t warp[1]={local};
                    r.eax=oy;r.edx=local;r.ecx=ox;
                    ho_call(g,0x486e7cu,warp,1,&r);
                    W8(map+0x15au,0u);
                }
            }
        }
    }
    /* 44d578: an event on the map area becomes a pending click. */
    r.eax=code;
    if(code==R32(map+0x39u)) {
        W32(map+0x11du,R32(point));
        r.edx=R32(point+4u);
        W32(map+0x121u,r.edx);
        W8(map+0x11cu,1u);
        r.eax=map_gadget(m,g);
        redraw(m,r.eax);
    }
    W8(map+0x14du,0u);
    return (uint64_t)r.edx<<32|r.eax;
}

/* The HDD gauge's key method (docs/reverse-engineering/cockpit.md,
 * "HDD key method 449ff4"). The HDD buttons the keys press: the
 * jump table 44a05e for the codes 4Ah..50h (0 = the active page's key
 * slot). */
static const uint8_t hdd_key_table[7]={6u,4u,0u,5u,7u,0u,3u};

/* Original: DBSIM.EXE 0x449ff4 */
uint64_t es2_hdd_key_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t hdd,
    uint32_t code,const uint32_t entry[3]) {
    const uint32_t k=code&0xffffu;            /* 449ffe movzx edx,ax */
    es2_weapon_registers r={code,k,entry[2]};
    uint32_t button=0,view=0;
    if(k>0x48u) {
        r.edx=k-0x4au;                         /* 44a04b */
        if(r.edx>6u||!hdd_key_table[r.edx])goto page;
        button=hdd_key_table[r.edx];
    } else if(k==0x48u) button=2u;            /* 44a07a */
    else if(k>0x11u) {
        r.edx=k-0x17u;                         /* 44a034 */
        if(!r.edx) { view=1u;goto status; }
        r.edx=k-0x1fu;
        if(!r.edx) { view=0u;goto status; }
        goto page;
    } else if(k==0x11u) { view=2u;goto status; }
    else if(k-2u<3u) {
        r.edx=k-5u;                            /* 44a0c2: keys 1..3 */
        r.eax=k+8u;
        button=k+8u;
    } else {
        r.edx=k-12u;
        if(!r.edx) button=6u;                  /* 44a0eb */
        else {
            r.edx=k-13u;
            if(r.edx)goto page;
            button=7u;                         /* 44a0d9 */
        }
    }
    {   const uint32_t press[3]={hdd,button,1u};
        ho_call(g,0x438d9cu,press,3,&r);
    }
    goto handled;
status:
    {   /* 44a0fa/44a118/44a136: page 1, then its view. */
        const uint32_t show[2]={hdd,1u};
        ho_call(g,0x44a60cu,show,2,&r);
        const uint32_t p=R32(hdd+0x105u);
        if(view==0u)r.ecx=p; else if(view==1u)r.eax=p; else r.edx=p;
        const uint32_t set[2]={p,view};
        ho_call(g,0x450b88u,set,2,&r);
    }
handled:
    r.eax=(r.eax&0xffffff00u)|1u;              /* 44a16e mov al,1 */
    return (uint64_t)r.edx<<32|r.eax;
page:
    {   /* 44a154: the active page's key slot +10h(page, code). */
        r.ecx=R32(hdd+0x109u);
        r.eax=R32(hdd+4u*r.ecx+0x101u);
        r.edx=R32(r.eax);
        const uint32_t target=R32(r.edx+0x10u);
        const uint32_t key[2]={r.eax,code};
        if(g->observe)g->observe(g->observer_context,target,0x10u);
        g->call(g->context,target,key,2,&r);
    }
    return (uint64_t)r.edx<<32|r.eax;
}
