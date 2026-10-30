#include "es2/core/control_record.h"
#include "fixed.h"
/* The per-frame control record (docs/reverse-engineering/input.md,
 * "control record 45a810"). The register file r follows the
 * original's EAX/EDX/ECX wherever a value reaches a callee's entry or the
 * RET; pointers kept in globals (the device block pointer 4d2390, the
 * axis and button pointer tables 4d2310..4d238c) are reread exactly where
 * the original rereads them, so callees and aliasing stores behave the
 * same. */
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define R16(a) m->read16(m->context,(a))
#define R32(a) m->read32(m->context,(a))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W32(a,v) m->write32(m->context,(a),(uint32_t)(v))
#define VA(va) (c->g->image+(uint32_t)(va)-0x400000u)
#define S(off) (c->g->scratch+(uint32_t)(off))
/* Globals (hypothetical names, input.md). */
#define REC VA(0x4d22c6u)        /* the 48-byte record */
#define DEVICE VA(0x4d2390u)     /* pointer to the device block (45baa8's result) */
#define PLAYBACK VA(0x4d24d6u)   /* tape playback word */
#define RECORDING VA(0x4d24d8u)  /* tape recording word */
#define TAPE VA(0x4d24e2u)       /* the playback FILE* */
#define VIEW VA(0x4d24f0u)       /* 0 cockpit, else outside */
#define STEER VA(0x4d2526u)      /* set by projectile flight 40a644 */
#define KEYMASK VA(0x49eab8u)
#define QUEUE VA(0x4d20c4u)      /* queued key words */
#define QUEUED VA(0x49eaa0u)     /* their count */
#define MAP VA(0x4d1f38u)        /* control mapping bytes */
#define SET VA(0x4d2577u)        /* mapping set index */
#define LATCH VA(0x4d22f6u)      /* eight latch flags, eight latch values at +8 */
#define MOUSE VA(0x4d23c1u)      /* the keyboard-axis block of 45a4cc */
enum { K_PUMP=0x464c78u, K_MEMSET=0x49681au, K_POLL=0x45baa8u, K_KEY=0x45bbc4u, K_JOYSTICK=0x477904u,
    K_FREAD=0x49684au, K_ADDRESS=0x486eceu, K_PLAYKEY=0x45fe6cu, K_FCLOSE=0x496838u, K_EVENT=0x453078u,
    K_DEVICE=0x45c524u, K_AXES=0x45a4ccu, K_COMMAND=0x45feb8u, K_EVENTS=0x452d40u, K_NAME=0x492b90u,
    K_FOPEN=0x496802u, K_ERROR=0x479c78u, K_FWRITE=0x496832u };
typedef struct cr_state {
    const es2_guest_memory *m;
    const es2_weapon_graph *g;
    es2_weapon_registers r;
    uint32_t buttons;  /* frame +0: the second button set (tape bytes or device +1Dh) */
    uint32_t axis[8];  /* frame +14h..+30h: axis sources */
    uint8_t mode[6];   /* frame +34h..+39h: mapping bytes of the four joystick controls, two flags */
    uint32_t list;     /* frame +44h: 452d40's result */
} cr_state;
enum { A14, A18, A1C, A20, A24, A28, A2C, A30 };
enum { M_AXES, M_THROTTLE, M_RUDDER, M_HAT, M_RUDDER_ON, M_THROTTLE_ON };

static void cr_call(cr_state *c,uint32_t va,const uint32_t *a,unsigned n) {
    if(c->g->observe)c->g->observe(c->g->observer_context,va,0);
    c->g->call(c->g->context,VA(va),a,n,&c->r);
}
static uint32_t low16(uint32_t reg,uint32_t v) { return (reg&0xffff0000u)|(uint16_t)v; }
static uint32_t low8(uint32_t reg,uint32_t v) { return (reg&0xffffff00u)|(uint8_t)v; }

/* 45a8a0..45ab1b: one tape entry (playback). */
static void cr_playback(cr_state *c) {
    const es2_guest_memory *m=c->m;uint32_t a[4];
    if(R8(VA(0x49ebbcu))) {
        /* The joystick state block once, then the address 4d2306. */
        cr_call(c,K_JOYSTICK,a,0);
        uint32_t block=c->r.eax;
        c->r.eax=R32(TAPE);c->r.edx=block;
        a[0]=block;a[1]=8;a[2]=1;a[3]=c->r.eax;cr_call(c,K_FREAD,a,4);
        a[0]=VA(0x4d2306u);cr_call(c,K_ADDRESS,a,1);c->r.ecx=a[0];
        W8(VA(0x49ebbcu),0);
    }
    c->r.ecx=R32(TAPE);c->r.eax=S(0x58);
    a[0]=S(0x58);a[1]=0x10;a[2]=1;a[3]=c->r.ecx;cr_call(c,K_FREAD,a,4);
    uint32_t ok=c->r.eax;
    c->r.eax=R32(TAPE);c->r.edx=S(0xc);
    a[0]=S(0xc);a[1]=4;a[2]=1;a[3]=c->r.eax;cr_call(c,K_FREAD,a,4);
    ok&=c->r.eax;
    c->r.ecx=R32(TAPE);
    a[0]=QUEUED;a[1]=4;a[2]=1;a[3]=c->r.ecx;cr_call(c,K_FREAD,a,4);
    ok&=c->r.eax;
    if(R16(VA(0x4d2530u))) { c->r.eax=R32(DEVICE);if(R16(c->r.eax))W16(VA(0x4d2532u),1); }
    c->r.edx=R32(DEVICE);c->r.ecx=low16(c->r.ecx,R16(c->r.edx));
    a[0]=c->r.ecx;cr_call(c,K_PLAYKEY,a,1);c->r.ecx=a[0];
    if(ok) {
        c->r.eax=R32(DEVICE);
        if(R16(c->r.eax)!=0x412u) {
            for(uint32_t i=0;(int32_t)i<(int32_t)R32(S(0xc));++i) {
                c->r.eax=R32(TAPE);c->r.edx=S(0x68);
                a[0]=S(0x68);a[1]=1;a[2]=0xe;a[3]=c->r.eax;cr_call(c,K_FREAD,a,4);
                c->r.ecx=R32(S(0x72));c->r.eax=S(0x68);
                a[0]=S(0x68);a[1]=c->r.ecx;cr_call(c,K_EVENT,a,2);
                c->r.edx=R32(S(0x68));c->r.ecx=R32(S(0x6c));
                W32(VA(0x4d2306u),c->r.edx);W32(VA(0x4d230au),c->r.ecx);
            }
            c->r.eax=R32(TAPE);c->r.edx=R32(QUEUED);
            a[0]=QUEUE;a[1]=c->r.edx;a[2]=2;a[3]=c->r.eax;cr_call(c,K_FREAD,a,4);
            /* The 16-byte entry: key, four axes, eight + eight button bits,
             * the time scale word and the mouse-button bit. */
            uint32_t rec=REC;
            W16(rec,R16(S(0x58)));W16(rec+0xeu,R16(S(0x5a)));W16(rec+0x10u,R16(S(0x5c)));
            W16(rec+0x12u,R16(S(0x5e)));W16(rec+0x14u,R16(S(0x60)));
            static const uint8_t first[8]={0x16,0x17,0x18,0x19,0x1e,0x1f,0x20,0x21};
            for(unsigned b=0;b<8;++b)W8(rec+first[b],R8(S(0x64))>>b&1u);
            W16(rec+0x2eu,R16(S(0x62)));
            for(unsigned b=0;b<8;++b)W8(S(4+b),R8(S(0x65))>>b&1u);
            W8(rec+0xdu,R8(S(0x66))&1u);
            c->buttons=S(4);
            return;
        }
    }
    /* End of the tape (a short read or the key 412h). */
    if(R16(VA(0x4d2530u)))W16(VA(0x4d2532u),1);
    c->r.edx=R32(TAPE);a[0]=c->r.edx;cr_call(c,K_FCLOSE,a,1);c->r.ecx=a[0];
    W16(PLAYBACK,0);W8(VA(0x4d1dd6u),1);
    c->r.ecx=R32(DEVICE);W16(c->r.ecx,0);
}

/* 45ab1e..45acfa: the joystick controls' sources by mapping byte
 * (0 clears the device value, 1 and 2 pick one of two axis pairs). */
static void cr_joystick(cr_state *c) {
    const es2_guest_memory *m=c->m;uint32_t a[1];
    c->r.eax=c->r.edx=c->r.ecx=0;
    a[0]=3;cr_call(c,K_DEVICE,a,1);c->r.ecx=a[0];
    if(!c->r.eax)return;
    cr_call(c,K_JOYSTICK,a,0);
    uint32_t j=c->r.eax;
    if(R8(j+5u))c->mode[M_RUDDER_ON]=1;
    if(!R16(j))return;
    uint32_t k=R8(MAP+R8(SET));
    c->r.edx=k;c->mode[M_AXES]=(uint8_t)k;c->r.ecx=(uint8_t)(k-1u);
    if(k==0) {
        c->r.edx=R32(DEVICE);c->r.ecx=0;W32(c->r.edx+0xdu,0);
        c->r.edx=R32(DEVICE);W32(c->r.edx+0x11u,0);
    } else if(k==1) {
        c->r.edx=R32(DEVICE)+0xdu;c->axis[A14]=c->r.edx;
        c->r.ecx=R32(DEVICE)+0x11u;c->axis[A18]=c->r.ecx;
    } else {
        c->r.ecx=(uint8_t)(k-2u);
        if(k==2) {
            c->r.edx=R32(DEVICE)+0xdu;c->axis[A1C]=c->r.edx;
            c->r.ecx=R32(DEVICE)+0x11u;c->axis[A20]=c->r.ecx;
            if(R8(VA(0x4d2571u))) { c->r.edx=R32(DEVICE);W32(c->r.edx+0x11u,0u-R32(c->r.edx+0x11u)); }
        }
    }
    if(R8(j+4u)) {
        /* INC ECX on the loaded byte, then MOVSX of the low byte. */
        c->r.ecx=(uint8_t)(low8(c->r.ecx,R8(SET))+1u);
        c->mode[M_THROTTLE_ON]=1;
        c->r.edx=R8(MAP+c->r.ecx);
        uint32_t k1=c->r.edx;c->mode[M_THROTTLE]=(uint8_t)k1;
        if(R8(VA(0x4d2571u))) { c->r.ecx=R32(DEVICE);W32(c->r.ecx+0x15u,0u-R32(c->r.ecx+0x15u)); }
        c->r.edx=(uint8_t)(k1-1u);
        if(k1==0) { c->r.ecx=R32(DEVICE);c->r.edx=0;W32(c->r.ecx+0x15u,0); }
        else if(k1==1) {
            if(c->axis[A18]) { c->r.ecx=R32(DEVICE)+0x15u;c->axis[A28]=c->r.ecx; }
            else { c->r.edx=R32(DEVICE)+0x15u;c->axis[A18]=c->r.edx; }
        } else {
            c->r.edx=(uint8_t)(k1-2u);
            if(k1==2) {
                if(c->axis[A20]) { c->r.ecx=R32(DEVICE)+0x15u;c->axis[A30]=c->r.ecx; }
                else { c->r.edx=R32(DEVICE)+0x15u;c->axis[A20]=c->r.edx; }
            }
        }
    }
    if(R8(j+5u)) {
        c->r.eax=(uint8_t)(R8(SET)+2u);c->r.ecx=c->r.eax;
        c->r.edx=low8(c->r.edx,R8(MAP+c->r.eax));
        uint32_t k2=(uint8_t)c->r.edx;c->mode[M_RUDDER]=(uint8_t)k2;
        c->r.ecx=(uint8_t)(k2-1u);
        if(k2==0) { c->r.eax=R32(DEVICE);c->r.edx=0;W32(c->r.eax+0x19u,0); }
        else if(k2==1) {
            if(c->axis[A14]) { c->r.ecx=R32(DEVICE)+0x19u;c->axis[A24]=c->r.ecx; }
            else { c->r.eax=R32(DEVICE)+0x19u;c->axis[A14]=c->r.eax; }
        } else {
            c->r.ecx=(uint8_t)(k2-2u);
            if(k2==2) {
                if(c->axis[A1C]) { c->r.edx=R32(DEVICE)+0x19u;c->axis[A2C]=c->r.edx; }
                else { c->r.ecx=R32(DEVICE)+0x19u;c->axis[A1C]=c->r.ecx; }
            }
        }
    }
}

/* 45ad7d..45aeeb: the keyboard-axis block 4d23c1 (+0 turn, +4 throttle,
 * +8 twist, +0Ch pitch: the defaults of the four axis targets) by view and latch state. */
static void cr_mouse(cr_state *c) {
    const es2_guest_memory *m=c->m;
    if(R16(STEER))W8(VA(0x49ebbdu),1);
    if(R8(LATCH)&&R8(LATCH+8u)&&R8(VA(0x49ebbdu))==1)W8(VA(0x49ebbdu),2);
    else if(!R8(LATCH))W8(VA(0x49ebbdu),0);
    if(R8(VA(0x49ebbdu))) { c->r.ecx=0;W32(MOUSE+4u,0);W32(MOUSE,0); }
    if(R16(VIEW)||R16(STEER)) {
        if(R16(STEER)&&!R32(VA(0x49ea98u)))W32(MOUSE+4u,0u-R32(MOUSE+4u));
        c->r.eax=R32(VA(0x4d2350u));
        if(R32(c->r.eax)) {
            c->r.edx=R32(VA(0x4d2350u));c->r.eax=R32(c->r.edx);W32(MOUSE,c->r.eax);
            c->r.edx=R32(VA(0x4d2350u));c->r.ecx=0;W32(c->r.edx,0);
        }
        c->r.eax=R32(VA(0x4d2354u));
        if(R32(c->r.eax)) {
            c->r.edx=R32(VA(0x4d2354u));c->r.ecx=R32(c->r.edx);W32(MOUSE+4u,c->r.ecx);
            c->r.eax=R32(VA(0x4d2354u));c->r.edx=0;W32(c->r.eax,0);
        }
        c->r.ecx=0;W32(MOUSE+0xcu,0);W32(MOUSE+8u,0);
        if(!c->mode[M_THROTTLE_ON]) {
            c->r.eax=R32(VA(0x4d235cu));c->r.edx=0;W32(c->r.eax,0);
            c->r.eax=R32(VA(0x4d2358u));W32(c->r.eax,0);
        }
    } else if(c->mode[M_AXES]==1) {
        if(R32(MOUSE)) { c->r.ecx=R32(MOUSE);c->r.eax=0;W32(MOUSE+8u,c->r.ecx);W32(MOUSE,0); }
        if(R32(MOUSE+4u)) { c->r.edx=0u-R32(MOUSE+4u);W32(MOUSE+0xcu,c->r.edx);c->r.ecx=0;W32(MOUSE+4u,0); }
    }
    if(c->mode[M_THROTTLE_ON]&&c->mode[M_THROTTLE]) { c->r.eax=0;W32(MOUSE+0xcu,0); }
    if(R32(VA(0x49ea90u)))W32(MOUSE+0xcu,0u-R32(MOUSE+0xcu));
    if(c->mode[M_RUDDER_ON]&&R32(VA(0x49ea9cu))) { c->r.edx=R32(DEVICE);W32(c->r.edx+0x19u,0u-R32(c->r.edx+0x19u)); }
}

/* One axis word: the second source if set and nonzero (dword test, word
 * copy), else the first under the same test, else the default pointer. */
static void cr_axis(cr_state *c,uint32_t target,uint32_t second,uint32_t first,uint32_t fallback) {
    const es2_guest_memory *m=c->m;
    if(second&&R32(second)) { W16(R32(target),R16(second));return; }
    uint32_t v=first&&R32(first)?R16(first):R16(R32(fallback));
    W16(R32(target),v);
}

/* 45aefc..45b2c7 (not in playback): axes, hat, buttons, latches, the
 * mouse-button byte and the cursor/button fields of the device block. */
static void cr_devices(cr_state *c) {
    const es2_guest_memory *m=c->m;uint32_t rec=REC;
    cr_axis(c,VA(0x4d2310u),c->axis[A24],c->axis[A14],VA(0x4d2340u));
    cr_axis(c,VA(0x4d2314u),c->axis[A28],c->axis[A18],VA(0x4d2344u));
    cr_axis(c,VA(0x4d2318u),c->axis[A2C],c->axis[A1C],VA(0x4d2348u));
    cr_axis(c,VA(0x4d231cu),c->axis[A30],c->axis[A20],VA(0x4d234cu));
    /* The hat: mapping byte 2 turns it into pitch/twist steps of C0h. */
    uint32_t k3=R8(MAP+(uint8_t)(R8(SET)+3u));c->mode[M_HAT]=(uint8_t)k3;
    if(k3==0||k3==2) {
        if(k3==2) {
            uint32_t d=R32(DEVICE);
            if(R8(d+0x25u))W16(R32(VA(0x4d231cu)),0xc0u);
            else if(d=R32(DEVICE),R8(d+0x26u))W16(R32(VA(0x4d231cu)),0xff40u);
            else if(d=R32(DEVICE),R8(d+0x28u))W16(R32(VA(0x4d2318u)),0xc0u);
            else if(d=R32(DEVICE),R8(d+0x27u))W16(R32(VA(0x4d2318u)),0xff40u);
        }
        W8(R32(DEVICE)+0x25u,0);W8(R32(DEVICE)+0x26u,0);W8(R32(DEVICE)+0x28u,0);W8(R32(DEVICE)+0x27u,0);
    }
    /* Eight button bytes: destinations 4d2320.., sources 4d2370.. when
     * their byte is set, else 4d2360.., the last four 4d2380.. directly.
     * All sixteen pointers are read before the first copy. */
    uint32_t dst[8],src[8];
    for(unsigned i=0;i<8;++i)dst[i]=R32(VA(0x4d2320u)+4u*i);
    for(unsigned i=0;i<4;++i) {
        uint32_t p=R32(VA(0x4d2370u)+4u*i);
        src[i]=R8(p)?R32(VA(0x4d2370u)+4u*i):R32(VA(0x4d2360u)+4u*i);
    }
    for(unsigned i=4;i<8;++i)src[i]=R32(VA(0x4d2380u)+4u*(i-4u));
    for(unsigned i=0;i<8;++i)W8(dst[i],R8(src[i]));
    /* Latches: a set flag toggles the button while it still holds the
     * latched value, else the flag clears. */
    for(unsigned i=0;i<8;++i) {
        if(!R8(LATCH+i))continue;
        if(R8(LATCH+8u+i)==R8(dst[i]))W8(dst[i],R8(LATCH+8u+i)^1u);
        else W8(LATCH+i,0);
    }
    /* The record's +0Dh: the first button whose mapping byte (4d1f49..) is
     * 1, consumed; else the byte at [4d2360]. */
    unsigned found=0;
    for(unsigned j=0;j<8;++j)if(R8(MAP+0x11u+j)==1) {
        W8(rec+0xdu,R8(dst[j]));W8(dst[j],0);found=1;break;
    }
    if(!found)W8(rec+0xdu,R8(R32(VA(0x4d2360u))));
    /* Cursor, mouse buttons and hat bytes of the device block, read and
     * written alternately through CL/DL as the original does (the block
     * may overlap the record in vitro). */
    uint32_t d=R32(DEVICE),cl=d+2u,dl=R32(d+2u);
    W32(rec+2u,dl);dl=R32(d+6u);W32(rec+6u,dl);
    static const uint8_t from[7]={0x0a,0x0b,0x0c,0x25,0x26,0x27,0x28},to[7]={0x0a,0x0b,0x0c,0x1e,0x1f,0x20,0x21};
    for(unsigned i=0;i<7;++i) {
        uint32_t v=R8(d+from[i]);W8(rec+to[i],v);
        if(i&1)dl=low8(dl,v);else cl=low8(cl,v);
    }
    uint32_t key=R16(d);W16(rec,key);
    c->r.eax=low16(d,key);c->r.edx=dl;c->r.ecx=cl;
}

/* 45b2d8..45b5b7: append this frame's entry to the tape file. */
static void cr_record(cr_state *c) {
    const es2_guest_memory *m=c->m;uint32_t a[6],rec=REC;
    c->r.eax=R32(VA(0x4d24dau));
    a[0]=c->r.eax;a[1]=VA(0x49ebe0u);cr_call(c,K_NAME,a,2);
    uint32_t name=c->r.eax;c->r.edx=name;
    a[0]=name;a[1]=VA(0x49ebe4u);cr_call(c,K_FOPEN,a,2);
    uint32_t file=c->r.eax;
    if(!file) {
        c->r.eax=name;
        a[0]=VA(0x6c56c8u);a[1]=5;a[2]=file;a[3]=VA(0x49ebe7u);a[4]=0x342;a[5]=name;cr_call(c,K_ERROR,a,6);
    }
    if(R8(VA(0x49ebbcu))) {
        cr_call(c,K_JOYSTICK,a,0);
        uint32_t block=c->r.eax;c->r.edx=block;
        a[0]=block;a[1]=8;a[2]=1;a[3]=file;cr_call(c,K_FWRITE,a,4);
        W8(VA(0x49ebbcu),0);
    }
    W16(S(0xb8),R16(rec));W16(S(0xba),R16(rec+0xeu));W16(S(0xbc),R16(rec+0x10u));
    W16(S(0xbe),R16(rec+0x12u));W16(S(0xc0),R16(rec+0x14u));
    static const uint8_t first[8]={0x16,0x17,0x18,0x19,0x1e,0x1f,0x20,0x21};
    uint32_t bits=0;
    for(unsigned b=0;b<8;++b)if(R8(rec+first[b]))bits|=1u<<b;
    for(unsigned b=0;b<8;++b)if(R8(c->buttons+b))bits|=0x100u<<b;
    c->r.ecx=R8(rec+0xdu)?0x10000u:0;bits|=c->r.ecx;
    c->r.eax=R16(VA(0x4d3b64u));
    W16(S(0xc2),c->r.eax);W32(S(0xc4),bits);
    c->r.edx=S(0xb8);
    a[0]=S(0xb8);a[1]=1;a[2]=0x10;a[3]=file;cr_call(c,K_FWRITE,a,4);
    c->r.ecx=c->list;
    a[0]=c->list;a[1]=1;a[2]=4;a[3]=file;cr_call(c,K_FWRITE,a,4);
    a[0]=QUEUED;a[1]=1;a[2]=4;a[3]=file;cr_call(c,K_FWRITE,a,4);
    c->r.eax=c->list;
    if(R32(c->list)) {
        c->r.edx=c->list;c->r.ecx=R32(c->list);c->r.eax=c->list;c->r.edx=R32(c->list+4u);
        a[0]=c->r.edx;a[1]=c->r.ecx;a[2]=0xe;a[3]=file;cr_call(c,K_FWRITE,a,4);
    }
    if(R32(QUEUED)) {
        c->r.ecx=R32(QUEUED);
        a[0]=QUEUE;a[1]=c->r.ecx;a[2]=2;a[3]=file;cr_call(c,K_FWRITE,a,4);
    }
    a[0]=file;cr_call(c,K_FCLOSE,a,1);c->r.ecx=a[0];
}

/* Original: DBSIM.EXE 0x45a810 */
uint64_t es2_control_record_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t *ecx,
    const uint32_t entry[3]) {
    cr_state state={m,g,{entry[0],entry[1],entry[2]},0,{0},{0},0};cr_state *c=&state;
    uint32_t a[3],rec=REC;
    cr_call(c,K_PUMP,a,0);
    if(c->r.eax)W16(VA(0x4d2532u),1);
    /* (45a836 stores 1 at frame +8, a byte no path reads before it is
     * rewritten.) */
    a[0]=rec;a[1]=0;a[2]=0x30;cr_call(c,K_MEMSET,a,3);
    cr_call(c,K_POLL,a,0);
    W32(DEVICE,c->r.eax);
    c->r.eax=R32(DEVICE);
    uint32_t key=R16(c->r.eax);c->r.edx=low16(c->r.edx,key);W16(S(0xa),key);
    /* Key words with an empty low byte are skipped while the reader
     * delivers more. */
    while(R16(S(0xa))&&!R8(S(0xa))) {
        c->r.ecx=S(0xa);a[0]=S(0xa);a[1]=1;cr_call(c,K_KEY,a,2);
        if(!(uint8_t)c->r.eax)break;
    }
    c->r.eax=low16(c->r.eax,R16(KEYMASK));
    W16(S(0xa),R16(S(0xa))&c->r.eax);
    c->r.edx=R32(DEVICE);c->r.ecx=low16(c->r.ecx,R16(S(0xa)));W16(c->r.edx,c->r.ecx);
    if(R16(PLAYBACK))cr_playback(c);
    cr_joystick(c);
    if(!R16(PLAYBACK)) {
        uint32_t flag=(c->mode[M_AXES]==1&&!R16(VIEW))||R16(STEER)?1u:0u;
        c->r.eax=c->r.edx=flag;
        a[0]=MOUSE;a[1]=flag;cr_call(c,K_AXES,a,2);
        c->r.ecx=R32(DEVICE)+0x1du;c->buttons=c->r.ecx;
    }
    /* Queued key words go to the command handler; the count is reread
     * after every call. */
    uint32_t q=QUEUE;
    for(uint32_t n=0;(int32_t)n<(int32_t)R32(QUEUED);++n,q+=2u) {
        c->r.eax=low16(c->r.eax,R16(KEYMASK));
        W16(q,R16(q)&c->r.eax);
        c->r.edx=low16(c->r.edx,R16(q));
        a[0]=c->r.edx;cr_call(c,K_COMMAND,a,1);c->r.ecx=a[0];
    }
    cr_call(c,K_EVENTS,a,0);c->list=c->r.eax;
    cr_mouse(c);
    if(!R16(PLAYBACK))cr_devices(c);
    if(R16(RECORDING))cr_record(c);
    /* 45b5b8: the camera arguments +22h/+26h point at the turn/throttle
     * pair, or at twist/pitch when the axes mapping byte is 2 (cockpit,
     * no projectile view); both latches 0 clear the pair. */
    if(R16(VIEW)||R16(STEER)) {
        c->r.eax=rec+0xeu;W32(rec+0x22u,c->r.eax);c->r.edx=rec+0x10u;W32(rec+0x26u,c->r.edx);
    } else if(c->mode[M_AXES]!=2) {
        if(R8(LATCH)&&R8(LATCH+8u)) { c->r.ecx=0;W16(rec+0x10u,0);W16(rec+0xeu,0); }
        c->r.eax=rec+0xeu;W32(rec+0x22u,c->r.eax);c->r.edx=rec+0x10u;W32(rec+0x26u,c->r.edx);
    } else {
        if(R8(LATCH)&&R8(LATCH+8u)) { c->r.eax=0;W16(rec+0x14u,0);W16(rec+0x12u,0); }
        c->r.edx=rec+0x12u;c->r.ecx=rec+0x14u;W32(rec+0x22u,c->r.edx);W32(rec+0x26u,c->r.ecx);
    }
    if(R32(VA(0x49ea94u))&&fx_s16(R16(rec+0x10u))>0)W16(rec+0xeu,0u-R16(rec+0xeu));
    W32(QUEUED,0);
    *ecx=c->r.ecx;
    return (uint64_t)c->r.edx<<32|rec;
}

/* The flight-key callback 45a324 (docs/reverse-engineering/
 * input.md, "flight-key callback 45a324"). No game call; every
 * access goes through the guest memory in the original's order. */
#undef VA
#define VA(va) (image+(uint32_t)(va)-0x400000u)
enum { FK_LIST=30u, FK_KEYS=15u, FK_PASS=2u, FK_TAKEN=1u };
static uint64_t fk_result(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }

/* Original: DBSIM.EXE 0x45a324 */
uint64_t es2_flight_key_registers(const es2_guest_memory *m,uint32_t image,uint32_t code) {
    /* 45a330: a demo stops at the first flight key. */
    if(R16(VA(0x4d2530u))) { W16(VA(0x4d2532u),1);return fk_result(FK_PASS,code); }
    if(!R8(VA(0x49eaa5u)))return fk_result(FK_PASS,code);
    /* 45a35a: SETE CL of TEST DL,80h; AND DX,7Fh keeps the high word. */
    const int down=!(code&0x80u);
    const uint32_t edx=(code&0xffff0000u)|(code&0x7fu),key=code&0x7fu;
    if(!R8(VA(0x4d2571u))&&(key==0x4eu||key==0x4au))return fk_result(FK_PASS,edx);
    uint32_t i=0;
    while(i<FK_LIST&&R8(VA(0x49eb0bu)+i)!=key)++i;
    /* 45a3a6: JBE, so index 15 stays 15 and only 16..30 fold down. */
    if(i>FK_KEYS)i-=FK_KEYS;
    if(i>=FK_KEYS)return fk_result(FK_PASS,edx);
    const uint32_t held=VA(0x4d2394u)+i,released=VA(0x4d23a3u)+i,seen=VA(0x4d23b2u)+i;
    if(down) { W8(held,1);W8(released,0); }
    else if(R8(seen)) { W8(released,0);W8(held,0);W8(seen,0); }
    else W8(released,1);
    W16(VA(0x4d23d6u),code&0x200u);
    if(key==0x1cu||key==0x39u)return fk_result(FK_PASS,edx);
    /* 45a411: TEST SI,600h (Ctrl or Alt) under the -SPRUNKNOWN flag. */
    if(R32(VA(0x49ef38u))&&(code&0x600u))return fk_result(FK_PASS,edx);
    return fk_result(FK_TAKEN,edx);
}

/* The command-key queue callback 45a498 (docs/reverse-engineering/
 * input.md, "command-key queue callback 45a498"). No game call. */
/* Original: DBSIM.EXE 0x45a498 */
uint64_t es2_command_queue_registers(const es2_guest_memory *m,uint32_t image,uint32_t code,uint32_t edx) {
    /* 45a49b: CMP BYTE [49eaa4],0 / JE: without the capture the key passes. */
    if(!R8(VA(0x49eaa4u)))return fk_result(FK_PASS,edx);
    /* 45a4a4: MOV DX,[ebp+8] keeps the high word of the entry EDX. */
    edx=(edx&0xffff0000u)|(code&0xffffu);
    /* 45a4a8..45a4ba: the index is read once and used unchecked as a
     * 32-bit scale-2 index (the address wraps); the count is then
     * incremented by rereading it, because the store may have hit it. */
    const uint32_t index=R32(VA(0x49eaa0u));
    W16(VA(0x4d20c4u)+2u*index,edx);
    W32(VA(0x49eaa0u),R32(VA(0x49eaa0u))+1u);
    return fk_result(FK_TAKEN,edx);
}
