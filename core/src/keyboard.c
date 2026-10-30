#include "es2/core/keyboard.h"
/* The keyboard message handler (docs/reverse-engineering/input.md,
 * "keyboard message handler 477bec"). Every global is reread where
 * the original reads it, because a hot-key callback may change the entry
 * table, the ring indices or the device pointer before the next read. */
#define R32(a) m->read32(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define VA(va) (g->image+(uint32_t)(va)-0x400000u)
enum {
    KEY_TABLE=0x4a10ecu,     /* dword per virtual key below DFh, 0 = unmapped */
    KEY_RING=0x6bb6b0u,      /* 256 code words */
    KEY_WRITE=0x6bb8b0u, KEY_READ=0x6bb8b4u,
    HOTKEYS=0x6bb8b8u,       /* ten entries of 12 bytes: active, code list, callback */
    DEVICE=0x6c5eb8u,        /* keyboard device; bit 1 of its first dword = keys queued */
    GET_KEY_STATE=0x49695eu, /* import thunk of GetKeyState */
    FOCUS=0x4669b4u          /* leaves exclusive mode on Alt+Tab, Ctrl+Esc, Alt+Esc */
};
enum { VK_SHIFT_KEY=0x10u, VK_CONTROL_KEY=0x11u, VK_MENU_KEY=0x12u };
enum { MOD_SHIFT=0x800u, MOD_ALT=0x200u, MOD_CTRL=0x400u, KEY_UP=0x80u, KEY_F10=0x44u };

static void kb_call(const es2_weapon_graph *g,uint32_t target,uint32_t original,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,original,0);
    g->call(g->context,target,a,n,r);
}
/* GetKeyState(vk); TEST AX,AX / JGE: down when bit 15 of the SHORT is set. */
static int kb_down(const es2_weapon_graph *g,uint32_t vk,es2_weapon_registers *r) {
    const uint32_t a[1]={vk};
    kb_call(g,VA(GET_KEY_STATE),GET_KEY_STATE,a,1,r);
    return (r->eax&0x8000u)!=0u;
}

/* Original: DBSIM.EXE 0x477bec */
uint64_t es2_key_message_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t window,uint32_t message,uint32_t key,uint32_t lparam,const uint32_t entry[3]) {
    (void)window;(void)lparam;(void)entry;
    /* 477bf9..477c0c: ECX = message-100h-2 (below zero for 100h/101h),
     * then two more subtractions of 2 (below zero for 104h/105h). */
    es2_weapon_registers r={message,key,message-0x102u};
    if(message-0x100u>=2u) {
        r.ecx-=4u;
        if(message-0x104u>=2u)return (uint64_t)key<<32;
    }
    const int up=message==0x101u||message==0x105u;
    uint32_t code=key<0xdfu?R32(VA(KEY_TABLE)+4u*key):0u;
    if(!code) {
        /* 477d29: an unmapped key is reported handled while Alt is down. */
        const int alt=kb_down(g,VK_MENU_KEY,&r);
        return (uint64_t)r.edx<<32|(alt?1u:0u);
    }
    if(kb_down(g,VK_SHIFT_KEY,&r))code|=MOD_SHIFT;
    if(kb_down(g,VK_MENU_KEY,&r))code|=MOD_ALT;
    if(kb_down(g,VK_CONTROL_KEY,&r))code|=MOD_CTRL;
    /* Alt+Tab, Ctrl+Esc, Alt+Esc (down and up) leave the code unqueued. */
    if(code==0x20fu||code==0x401u||code==0x201u) {
        kb_call(g,VA(FOCUS),FOCUS,NULL,0,&r);
        return (uint64_t)r.edx<<32;
    }
    if(up)code|=KEY_UP;
    /* 477ca7..477ce5: each active entry whose zero-terminated byte list
     * holds the code's low byte gets the whole code; a result of 1
     * consumes it, any other result ends that entry's list. */
    r.eax=0u;
    for(unsigned i=0;i<10u;++i) {
        const uint32_t e=VA(HOTKEYS)+12u*i;
        if(!R32(e))continue;
        for(r.eax=R32(e+4u);R8(r.eax);) {
            const uint8_t b=R8(r.eax);
            r.edx=(r.edx&0xffffff00u)|b;++r.eax;
            if(b!=(uint8_t)code)continue;
            const uint32_t a[1]={code},target=R32(e+8u);
            kb_call(g,target,target-g->image+0x400000u,a,1,&r);
            r.ecx=code; /* POP ECX takes the argument back */
            if(--r.eax==0u)return (uint64_t)r.edx<<32|1u;
            break;
        }
    }
    /* 477ce7..477d12: queue the code unless the ring is full (the next
     * write index would meet the read index). The stored write index is
     * used unmasked as the slot. */
    r.eax=(R32(VA(KEY_WRITE))+1u)&0xffu;
    if(r.eax!=R32(VA(KEY_READ))) {
        r.edx=R32(VA(KEY_WRITE));
        W16(VA(KEY_RING)+2u*r.edx,code);
        W32(VA(KEY_WRITE),r.eax);
        r.eax=R32(VA(DEVICE));
        W32(r.eax,R32(r.eax)|2u);
    }
    /* F10 down (low byte 44h) is reported handled. */
    return (uint64_t)r.edx<<32|((code&0xffu)==KEY_F10?1u:0u);
}
