#include "es2/core/hdd_map_keys.h"
#include "fixed.h"
/* The HDD map key path (docs/reverse-engineering/cockpit.md, section "HDD map
 * key path"). Every guest read and write is done in the original's order and
 * repeated where the original repeats it (the callees may change M, the list
 * and the globals); r holds what the original's EAX/EDX/ECX hold at the next
 * game call. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define GVA(x) (g->image+(uint32_t)(x)-0x400000u)
#define NONE 0xffffffffu

static uint64_t mk_pair(const es2_weapon_registers *r) { return (uint64_t)r->edx<<32|r->eax; }
static void mk_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,GVA(va),a,n,r);
}
/* A one-argument call followed by POP ECX. */
static void mk_one(const es2_weapon_graph *g,uint32_t va,uint32_t arg,es2_weapon_registers *r) {
    const uint32_t a[1]={arg};
    mk_call(g,va,a,1,r);
    r->ecx=arg;
}
/* The map gadget's redraw request (44ee6d..44ee7c): EAX = the gadget. */
static void mk_redraw(const es2_guest_memory *m,const es2_weapon_graph *g,es2_weapon_registers *r) {
    r->eax=R32(GVA(0x49d6b0u));
    r->eax=R32(r->eax+8u);
    W8(r->eax+0x10fu,1u);
    W8(r->eax+0xb0u,2u);
}

/* The key table 44cc8c (codes 0Eh..4Ch), the index into the jump table. */
static const uint8_t mk_keys[0x3f]={
    15,14,0,0,13,0,12,0,0,0,11,0,0,0,10,0,9,0,8,7,6,0,0,0,0,0,0,0,0,0,0,5,4,0,0,0,0,3,2,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
/* Jump index -> map_press mode and the register that holds [M+35h]
 * (0 EDX, 1 ECX, 2 EAX); 0 = not a map_press key. */
static const uint8_t mk_press_mode[16]={0,0,0,0,0x10,0,0xe,0xc,0xa,0xb,0,0xf,0xd,0x11,0,0};
static const uint8_t mk_press_reg[16]={0,0,0,0,0,0,1,2,0,1,0,2,0,1,0,0};

static void mk_press(const es2_weapon_graph *g,uint32_t M,uint32_t gadget,uint32_t mode,es2_weapon_registers *r) {
    const uint32_t a[3]={M,gadget,mode};
    mk_call(g,0x44d5c0u,a,3,r);
}

/* Original: DBSIM.EXE 0x44cc68 */
uint64_t es2_map_key_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t map,
    uint32_t code,const uint32_t entry[3]) {
    const uint32_t M=map;
    es2_weapon_registers r={(code&0xffffu)-0xeu,entry[1],entry[2]};   /* 44cc6c movzx; add eax,-0eh */
    if(r.eax>0x3eu) { r.eax=0u; return mk_pair(&r); }                /* 44cc79 ja -> 44cf06 */
    const unsigned j=mk_keys[r.eax];
    r.eax=j;                                                           /* mov al,[eax+44cc8ch] */
    if(mk_press_mode[j]) {
        /* 44cd0b..44cda6: the mode keys. */
        const uint32_t gadget=R32(M+0x35u);
        if(mk_press_reg[j]==0)r.edx=gadget; else if(mk_press_reg[j]==1)r.ecx=gadget; else r.eax=gadget;
        mk_press(g,M,gadget,mk_press_mode[j],&r);
    } else switch(j) {
    case 0:                                                            /* 44cf06 */
        r.eax=0u;
        return mk_pair(&r);
    case 1:                                                            /* 44cef6: keypad 5 */
        r.edx=0u;
        W32(M+0x118u,0u);
        W32(M+0x114u,0u);
        break;
    case 2:                                                            /* 44cdd5 */
        mk_one(g,0x44ee48u,M,&r);
        break;
    case 3:                                                            /* 44cde1 */
        mk_one(g,0x44ee88u,M,&r);
        break;
    case 5: case 15: {                                                 /* 44cdab X, 44cdc0 backspace */
        const uint32_t hdd=R32(M+8u);
        if(j==5)r.eax=hdd; else r.edx=hdd;
        const uint32_t a[3]={hdd,j==5?0xdu:0xeu,1u};
        mk_call(g,0x438d9cu,a,3,&r);
        break;
    }
    case 10: {                                                         /* 44cded: Enter */
        if(!R8(M+0x103u))break;
        const uint32_t mode=R32(M+0x5du);
        r.ecx=mode-0xdu;                                               /* add ecx,-0bh; sub ecx,2 */
        int zero=0;
        if(mode-0xbu<2u) {
            if(R32(M+0x15cu)) {                                        /* 44ce0f */
                r.eax=R32(M+0x39u);
                mk_press(g,M,r.eax,1u,&r);
                break;
            }
            if(R32(M+0x5du)==0xbu)break;                               /* 44ce2c */
            zero=1;
        } else {
            r.ecx=mode-0xfu;                                           /* sub ecx,2 */
            zero=mode-0xdu<2u;
        }
        if(zero) {                                                     /* 44ce36 */
            r.edx=R32(M+0x39u);
            mk_press(g,M,r.edx,0u,&r);
        }
        break;
    }
    case 14: {                                                         /* 44ce4a: Tab */
        if(!R8(M+0x103u))break;
        const uint32_t mode=R32(M+0x5du);
        r.ecx=mode-0xbu;
        if(r.ecx==0u) {                                                /* 44ce64 */
            mk_one(g,0x44ef08u,M,&r);
            W32(M+0x15cu,r.eax);
        } else if(--r.ecx==0u) {                                       /* 44ce73 */
            mk_one(g,0x44ef1cu,M,&r);
            W32(M+0x15cu,r.eax);
        }
        r.eax=R32(M+0x15cu);                                           /* 44ce80 */
        if(!r.eax) {
            mk_one(g,0x462830u,0x1bu,&r);                              /* 44ceec */
            break;
        }
        const uint32_t target=r.eax;
        r.edx=R32(M+0x105u);                                           /* 44ce8a */
        r.ecx=r.edx;
        r.edx=21u*r.edx;
        W32(M+2u*r.edx+0xa1u,target);
        r.eax=R32(M+0x105u);                                           /* 44cea1 */
        r.edx=r.eax;
        r.eax=21u*r.eax;
        W32(M+2u*r.eax+0xabu,1u);
        r.ecx=R32(M+0x105u);                                           /* 44cebc */
        r.eax=r.ecx;
        r.ecx=21u*r.ecx;
        W8(M+2u*r.ecx+0xa6u,1u);
        W8(M+0x104u,1u);
        mk_one(g,0x44dd14u,M,&r);
        mk_one(g,0x462830u,0x1au,&r);
        break;
    }
    default:
        break;
    }
    r.eax=(r.eax&0xffffff00u)|1u;                                      /* 44cf0b mov al,1 */
    return mk_pair(&r);
}

/* Original: DBSIM.EXE 0x44d5c0 */
uint64_t es2_map_press_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t frame,
    uint32_t map,uint32_t gadget,uint32_t mode,const uint32_t entry[3]) {
    const uint32_t M=map,f=frame;
    es2_weapon_registers r={mode,entry[1],entry[2]};
    W8(M+0x15au,1u);
    if(gadget==R32(M+0x35u)) {
        /* 44d5de: the mode button's top left, one pixel in. */
        r.eax=R32(M+4u*(mode-0xau)+0x3du)+4u;
        r.edx=R32(r.eax);
        W32(f-8u,r.edx);
        r.ecx=R32(r.eax+4u);
        W32(f-4u,r.ecx);
        W32(f-8u,R32(f-8u)+1u);
        W32(f-4u,R32(f-4u)+1u);
        r.eax=f-8u;                                                    /* lea eax,[ebp-8] */
    } else if(gadget==R32(M+0x39u)) {
        r.eax=mode-1u;                                                 /* 44d615 sub eax,1 */
        if(mode==0u) {
            /* 44d621: the cursor, from screen to map-area coordinates. */
            r.edx=R32(GVA(0x49b088u));
            const uint32_t B=R32(r.edx+4u);
            r.eax=f-8u;
            mk_one(g,0x486eceu,f-8u,&r);
            r.eax=B+0x210u;
            r.edx=R32(r.eax);
            W32(f-8u,R32(f-8u)-r.edx);
            r.ecx=R32(r.eax+4u);
            r.eax=B+0x220u;
            W32(f-4u,R32(f-4u)-r.ecx);
            r.edx=R32(r.eax);
            W32(f-8u,R32(f-8u)+r.edx);
            r.ecx=R32(r.eax+4u);
            r.eax=f-8u;
            W32(f-4u,R32(f-4u)+r.ecx);
        } else if(mode==1u) {
            /* 44d666: the map view centred, the target projected onto it. */
            r.edx=M+0x18u;
            r.eax=R32(M+0x14u)+4u;
            r.ecx=R32(r.edx+8u);
            W32(r.eax+8u,r.ecx);
            r.ecx=R32(r.edx);
            W32(r.eax,r.ecx);
            r.edx=R32(r.edx+4u);
            W32(r.eax+4u,r.edx);
            r.eax=R32(M+0x14u);
            mk_one(g,0x48c2e4u,r.eax,&r);
            mk_one(g,0x48c444u,0u,&r);
            r.eax=f-0x24u;
            r.edx=R32(M+0x15cu)+0x26u;
            {   const uint32_t a[2]={r.edx,f-0x24u};
                mk_call(g,0x48c57cu,a,2,&r); }
            r.ecx=f-0x10u;
            r.eax=f-0x24u;
            {   const uint32_t a[2]={f-0x24u,f-0x10u};
                mk_call(g,0x48c5fcu,a,2,&r); }
            r.edx=f-0x18u;
            W32(f-0xcu,0u-R32(f-0xcu));                                /* neg [ebp-0ch] */
            r.ecx=f-0x10u;
            {   const uint32_t a[3]={M,f-0x10u,f-0x18u};
                mk_call(g,0x44d24cu,a,3,&r); }
            r.eax=f-0x18u;
            const uint32_t a[3]={M,gadget,f-0x18u};
            mk_call(g,0x44d3ccu,a,3,&r);
            return mk_pair(&r);
        } else {
            return mk_pair(&r);
        }
    } else {
        return mk_pair(&r);
    }
    const uint32_t a[3]={M,gadget,f-8u};
    mk_call(g,0x44d3ccu,a,3,&r);
    return mk_pair(&r);
}

/* Original: DBSIM.EXE 0x44d82c */
uint64_t es2_wingman_pick_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t map,
    const uint32_t entry[3]) {
    const uint32_t M=map;
    es2_weapon_registers r={M+0x11du,entry[1],entry[2]};
    {   const uint32_t a[2]={M,M+0x11du};
        mk_call(g,0x44d888u,a,2,&r); }
    const uint32_t actor=r.eax;
    if(actor) {
        r.eax=actor+0x92u;
        if(!R8(r.eax+7u)) {
            mk_one(g,0x433134u,actor,&r);
            if(r.eax!=NONE) {
                const uint32_t index=r.eax;
                r.edx=R32(GVA(0x49d6b0u));
                r.ecx=R32(r.edx+8u);
                const uint32_t a[2]={r.ecx,index};
                mk_call(g,0x44a748u,a,2,&r);
            }
        }
    }
    W8(M+0x11cu,0u);
    W8(GVA(0x49d6aeu),1u);
    return mk_pair(&r);
}

/* Original: DBSIM.EXE 0x433134 */
uint64_t es2_wingman_index_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t actor,
    const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],actor};
    if(actor) {
        r.eax=0u;
        r.edx=GVA(0x4d03c8u);
        for(;;) {
            if(actor==R32(r.edx))return mk_pair(&r);
            ++r.eax;
            r.edx+=4u;
            if(r.eax>=3u)break;
        }
    }
    r.eax=NONE;
    return mk_pair(&r);
}

/* Original: DBSIM.EXE 0x44ee48 and 0x44ee88 (next = 1: 44ee48). */
static uint64_t mk_mode_step(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t map,int next,
    const uint32_t entry[3]) {
    es2_weapon_registers r={map,R32(map+0x5du),entry[2]};
    if(r.edx==NONE)return mk_pair(&r);
    if(next) {
        r.edx-=9u;
        if(!((int32_t)fx_s32(r.edx)<8))r.edx=0u;                   /* cmp edx,8; jl */
    } else {
        r.edx-=0xbu;
        if((int32_t)fx_s32(r.edx)<0)r.edx=7u;                      /* test edx,edx; jge */
    }
    r.edx+=0xau;
    const uint32_t a[2]={map,r.edx};
    mk_call(g,0x44d9f4u,a,2,&r);
    mk_redraw(m,g,&r);
    return mk_pair(&r);
}
/* Original: DBSIM.EXE 0x44ee48 */
uint64_t es2_mode_next_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t map,
    const uint32_t entry[3]) { return mk_mode_step(m,g,map,1,entry); }
/* Original: DBSIM.EXE 0x44ee88 */
uint64_t es2_mode_prev_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t map,
    const uint32_t entry[3]) { return mk_mode_step(m,g,map,0,entry); }

static uint64_t mk_cycle_side(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t map,uint32_t side,
    const uint32_t entry[3]) {
    (void)m;
    es2_weapon_registers r={map,entry[1],entry[2]};
    const uint32_t a[2]={map,side};
    mk_call(g,0x44ef30u,a,2,&r);
    return mk_pair(&r);
}
/* Original: DBSIM.EXE 0x44ef08 */
uint64_t es2_target_cycle1_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t map,
    const uint32_t entry[3]) { return mk_cycle_side(m,g,map,1u,entry); }
/* Original: DBSIM.EXE 0x44ef1c */
uint64_t es2_target_cycle0_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t map,
    const uint32_t entry[3]) { return mk_cycle_side(m,g,map,0u,entry); }

/* The list iterator of 0x44ef30: the word [EBP-4] (counter) and the dword
 * [EBP-8] (next slot). One step: counter in `reg` (AX or DX, the rest of
 * the register kept), counter + 1; past the signed count word -> 0, else
 * the slot (address in `addr`, EDX or ECX) and the slot pointer + 4. */
typedef struct mk_iter { uint16_t counter; uint32_t slot; } mk_iter;
static uint32_t mk_step(const es2_guest_memory *m,const es2_weapon_graph *g,mk_iter *it,uint32_t *reg,uint32_t *addr) {
    *reg=(*reg&0xffff0000u)|it->counter;
    const uint16_t old=it->counter++;
    if(fx_s16(old)>=fx_s16(R16(GVA(0x4a9afeu))))return 0u;
    *addr=it->slot;
    it->slot+=4u;
    return R32(*addr);
}

/* Original: DBSIM.EXE 0x44ef30 */
uint64_t es2_target_cycle_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t map,
    uint32_t side,const uint32_t entry[3]) {
    const uint32_t M=map;
    es2_weapon_registers r={R32(GVA(0x4a9af8u)),entry[1],entry[2]};
    mk_iter it={0u,r.eax};
    uint32_t ebx=0u;
    r.eax=R32(M+0x15cu);
    int found=0;
    if(r.eax) {
        r.edx=R32(r.eax+0x45u);
        r.ecx=(uint32_t)(int32_t)(int8_t)R8(r.edx+0x12u);             /* movsx ecx,byte [edx+12h] */
        if((uint16_t)side==(uint16_t)r.ecx) {
            /* 44ef63: position the iterator after the current target. */
            do ebx=mk_step(m,g,&it,&r.eax,&r.edx);
            while(ebx!=R32(M+0x15cu));
            found=1;
        }
    }
    if(!found) {
        /* 44ef8b: the first entry. */
        ebx=mk_step(m,g,&it,&r.eax,&r.edx);
        const uint32_t a[3]={M,ebx,side};
        mk_call(g,0x44f03cu,a,3,&r);
        if(r.eax&0xffu) { r.eax=ebx; return mk_pair(&r); }
    }
    const uint32_t start=ebx;                                          /* 44efbc [ebp-0ch] */
    for(;;) {
        ebx=mk_step(m,g,&it,&r.edx,&r.ecx);
        if(!ebx) {
            /* 44efe1: wrap to the list start. */
            it.counter=0u;
            r.eax=R32(GVA(0x4a9af8u));
            it.slot=r.eax;
            ebx=mk_step(m,g,&it,&r.edx,&r.ecx);
            if(!ebx) { r.eax=0u; return mk_pair(&r); }
        }
        const uint32_t a[3]={M,ebx,side};
        mk_call(g,0x44f03cu,a,3,&r);
        if(r.eax&0xffu) { r.eax=ebx; return mk_pair(&r); }
        if(ebx==start) { r.eax=0u; return mk_pair(&r); }
    }
}
