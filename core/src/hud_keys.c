#include "es2/core/hud_keys.h"
#include "fixed.h"
#include <stddef.h>

/* The HUD key handler 432bc8, the button press 438d9c and the button-release
 * scheduler 4530bc (docs/reverse-engineering/hud.md, section "HUD key handler
 * as own code"). r holds what the original's EAX/EDX/ECX hold at the next game
 * call; the hud_key frame lives in the scratch at the original EBP offsets (f =
 * the synthetic EBP, filled by 49630c through the bridge). Memory is read again
 * wherever the original reads it. The getter 429820 is called (not read
 * inline), the entry EAX/EDX/ECX follow the listing at every call, and hk_at
 * names EBX/ESI/EDI, the return address and a pushed slot for the stack bridge
 * (input.md, "stack residue under the command handler"). */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define VA(g,x) ((g)->image+(uint32_t)(x)-0x400000u)
static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static uint32_t sx8(uint32_t x) { return (uint32_t)(int32_t)(int8_t)(uint8_t)x; }
/* A direct call names the original VA (observer key); the bridge gets the
 * address in this image. A virtual call passes an already-resolved target. */
static void hk_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,VA(g,va),a,n,r);
}
static void hk_vcall(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}

/* Original: DBSIM.EXE 0x4530bc */
uint64_t es2_button_release_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t P,uint32_t obj,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    const uint32_t old=R16(P+0x396u);                       /* 4530cb mov bx */
    W16(P+0x396u,old+1u);                                   /* 4530d2 inc */
    const uint32_t slot=P+0x356u+8u*(uint32_t)fx_s16(old);  /* movsx, shl 3, add */
    W32(slot,obj);                                          /* 4530ea */
    r.eax=obj;
    hk_call(g,0x467828u,NULL,0,&r);                         /* 4530ec clock (guest) */
    r.eax=r.eax+0xau;                                       /* 4530f1 */
    W32(slot+4u,r.eax);                                     /* 4530f4 */
    return pair(r.eax,r.edx);                               /* edx/ecx unchanged */
}

/* Original: DBSIM.EXE 0x438d9c */
uint64_t es2_button_press_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t G,uint32_t index,uint32_t flag,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[2];
    const uint32_t B=R32(G+0x18u+4u*index);                 /* 438da7 */
    r.edx=B;
    r.eax=(uint32_t)fx_s16(R16(B+0x1bu));                   /* 438dab movsx */
    if(r.eax==2u)return pair(r.eax,r.edx);                  /* 438db2 state 2 -> skip */
    r.ecx=flag;
    r.eax=R32(B+0x17u);                                     /* 438db9 */
    a[0]=B;a[1]=flag;
    hk_vcall(g,R32(r.eax+8u),8u,a,2,&r);                    /* 438dbc button virtual +8 */
    r.ecx=R32(VA(g,0x49b088u));                             /* 438dc2 H = [0x49b088] */
    const uint32_t B2=R32(G+0x18u+4u*index);                /* 438dc8 reread */
    r.edx=B2;
    r.eax=R32(r.ecx);                                       /* 438dcd [H+0] = panel */
    a[0]=r.eax;a[1]=B2;
    hk_call(g,0x4530bcu,a,2,&r);                            /* 438dd0 4530bc([H+0], B) */
    return pair(r.eax,r.edx);
}

/* What 432bc8 holds in EBX/ESI/EDI at the next call and that call's return
 * address (*frame only on the stack bridge's path). */
typedef struct hk_state { const es2_weapon_graph *g; es2_hud_key_frame *frame; uint32_t ebx,esi,edi; } hk_state;
static void hk_at(hk_state *s,uint32_t ret,uint32_t pushed,uint32_t temp) {
    if(!s->frame)return;
    s->frame->call.ebx=s->ebx;s->frame->call.esi=s->esi;s->frame->call.edi=s->edi;
    s->frame->call.ret=VA(s->g,ret);s->frame->pushed=pushed;s->frame->temp=temp;
}
/* 429820 (the display object [4CF99Ch]) with the display request already
 * pushed for the following 42a3f4 call, then 42a3f4(D, request). */
static void hk_request(hk_state *s,uint32_t request,uint32_t ret,es2_weapon_registers *r) {
    uint32_t a[2];
    hk_at(s,ret,1,request);hk_call(s->g,0x429820u,NULL,0,r);
    a[0]=r->eax;a[1]=request;
    hk_at(s,ret+6u,0,0);hk_call(s->g,0x42a3f4u,a,2,r);   /* push eax; call 42a3f4: 6 bytes on */
}

/* Offers code to the panel gadget [H+off] through its virtual +0xc; returns
 * whether it handled the key (AL). Most offers load the gadget into ECX and
 * its vtable into EAX; [H+1D8h] (432fbe) and the second [H+1F9h] offer
 * (4330cd) load the gadget into EAX and the vtable into EDX (in_eax). */
static int hk_offer(const es2_guest_memory *m,hk_state *s,uint32_t H,uint32_t off,
    uint32_t code,uint32_t ret,int in_eax,es2_weapon_registers *r) {
    const uint32_t obj=R32(H+off);
    uint32_t a[2]={obj,code};
    if(in_eax) { r->eax=obj;r->edx=R32(obj); } else { r->ecx=obj;r->eax=R32(obj); }
    hk_at(s,ret,0,0);hk_vcall(s->g,R32(R32(obj)+0xcu),0xcu,a,2,r);
    return (uint8_t)r->eax!=0;
}

/* Original: DBSIM.EXE 0x432bc8. Frame locals from the synthetic EBP f: the
 * SEH record f-0x28 (its saved FS:0 head at f-0x28, try-state word at f-0x18
 * = record+0x10), the F1-F6 request f-0x2c, the Esc request f-0x30, the F11
 * dialog object f-4 and its copy f-8. EBX = H and EDI = code throughout
 * (EBX = the F11 dialog object at its last two calls); ESI is the caller's
 * up to 429820, then the display mode M, which the Esc and F1-F6 dispatch,
 * Tab and the default chain step down (432ceb, 432ddf, 432eae, 432fb7). */
static uint64_t hk_key(const es2_guest_memory *m,const es2_weapon_graph *g,es2_hud_key_frame *frame,
    uint32_t caller_esi,uint32_t H,uint32_t code,const uint32_t entry[3],uint32_t *fs_head) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[3];
    hk_state st={g,frame,H,caller_esi,code},*s=&st;
    const uint32_t f=g->scratch+ES2_HUD_KEYS_FRAME;
    r.eax=VA(g,0x49b571u);                                  /* 432bd7 exception descriptor */
    hk_at(s,0x432be1u,0,0);hk_call(g,0x49630cu,NULL,0,&r);  /* 432bdc sets up the SEH record */
    if(R8(H+0x20fu)) { *fs_head=R32(f-0x28u);return pair(0,R32(f-0x28u)); }   /* 432be1 panel off */
    hk_at(s,0x432c00u,0,0);hk_call(g,0x429820u,NULL,0,&r);  /* 432bfb display object */
    const uint32_t D=r.eax;
    const uint32_t M=R32(D+0x14u);                          /* 432c00 display mode */
    s->esi=M;
    /* The Esc and F1-F6 dispatch (sub esi,1; jb; je; dec esi; sub esi,2)
     * leave ESI = M-1 for M <= 1, else M-4. */
    const uint32_t stepped=M<=1u?M-1u:M-4u;
    const uint32_t kc=code&0xffffu;                         /* movzx eax,di */
    if(kc==0u) { r.eax=0;goto done; }                       /* 432c89 */
    else if(kc==1u) {                                       /* 432ddf Esc -> display request */
        r.eax=0;                                            /* eax = kc-1 = 0 from the compare chain */
        s->esi=stepped;
        if(M==0u)W32(f-0x30u,0u);                           /* 432df7 */
        else if(M==1u)W32(f-0x30u,1u);                      /* 432dfe */
        else if(M==2u||M==3u)W32(f-0x30u,6u);               /* 432e07 */
        else if(M==4u)W32(f-0x30u,0xffffffffu);             /* 432dee (M>=5: the frame word as found) */
        if(R32(f-0x30u)==0xffffffffu)goto al1;              /* 432e0e */
        r.edx=R32(f-0x30u);r.eax=D;                         /* 432e18 42a3f4(D, req) */
        hk_request(s,r.edx,0x432e21u,&r);
        goto al1;
    } else if(kc>=2u&&kc<=0xbu) {                           /* 432c9a weapon keys 1..0 */
        if(M==1u)goto deflt;                                /* 432c9d */
        r.eax=kc-2u;                                        /* 432ca6 add eax,-2 */
        const uint32_t elem=R32(H+0x70u+4u*r.eax);          /* 432ca9 panel element */
        r.edx=elem;
        if(!elem)goto al1;                                  /* 432caf null */
        s->esi=elem;                                        /* 432cb5 */
        a[0]=H;a[1]=elem;                                   /* 432cb9 panel_weapon(H, elem) */
        hk_at(s,0x432cbeu,0,0);hk_call(g,0x432a50u,a,2,&r);
        a[0]=elem;a[1]=0;a[2]=1;                            /* 432cc6 438d9c(elem, 0, 1) */
        hk_at(s,0x432ccbu,0,0);hk_call(g,0x438d9cu,a,3,&r);
        goto al1;
    } else if(kc>=0x3bu&&kc<=0x40u) {                       /* 432cd3 F1-F6 */
        r.eax=kc-0x3bu;a[0]=R32(H+0x1edu);a[1]=r.eax;a[2]=1;r.edx=a[0];/* MFD button code-0x3b */
        hk_at(s,0x432ce8u,0,0);hk_call(g,0x438d9cu,a,3,&r);
        s->esi=stepped;
        if(M==0u)W32(f-0x2cu,0xffffffffu);                  /* 432cfc */
        else if(M==1u)W32(f-0x2cu,1u);                      /* 432d05 */
        else if(M==2u||M==3u)W32(f-0x2cu,6u);               /* 432d0e */
        else if(M==4u)W32(f-0x2cu,0xffffffffu);             /* 432cfc (M>=5: the frame word as found) */
        if(R32(f-0x2cu)==0xffffffffu)goto al1;              /* 432d15 */
        r.ecx=R32(f-0x2cu);                                 /* 432d1f mov ecx,[ebp-2Ch]; 42a3f4(D, req) */
        hk_request(s,r.ecx,0x432d28u,&r);
        goto al1;
    } else if(kc==0x41u||kc==0x42u) {                       /* 432d36 F7, F8 */
        r.eax=kc-0x41u;a[0]=R32(H+0x1f9u);a[1]=r.eax;a[2]=1;r.edx=a[0];/* panel button code-0x41 */
        hk_at(s,0x432d4bu,0,0);hk_call(g,0x438d9cu,a,3,&r);
        if(sx8(R8(VA(g,0x4d2538u)))==1u)goto al1;           /* 432d4e dec ecx -> je */
        r.ecx=sx8(R8(VA(g,0x4d2538u)))-1u;                  /* 432d5c 42a3f4(D, 0) */
        hk_request(s,0,0x432d63u,&r);
        goto al1;
    } else if(kc==0x43u) {                                  /* 432d71 F9 */
        r.eax=0;
        if(M==0u)hk_request(s,5u,0x432d7cu,&r);             /* req 5 */
        else if(M==2u)hk_request(s,6u,0x432d9au,&r);        /* req 6 */
        goto al1;
    } else if(kc==0x44u) {                                  /* 432da8 F10 */
        r.eax=0x44u;
        if(M==0u)hk_request(s,4u,0x432db3u,&r);             /* req 4 */
        else if(M==3u)hk_request(s,6u,0x432dd1u,&r);        /* req 6 */
        goto al1;
    } else if(kc==0xfu) {                                   /* 432eae Tab */
        s->esi=M-1u;                                        /* 432eae dec esi */
        if(M==1u) {
            const uint32_t gad=R32(H+0x1f9u);               /* [H+0x1f9] virtual +0xc(0xf) */
            a[0]=gad;a[1]=0xfu;r.eax=gad;r.edx=R32(gad);
            hk_at(s,0x432ebfu,0,0);hk_vcall(g,R32(r.edx+0xcu),0xcu,a,2,&r);
            goto al1;
        }
        const uint32_t Hc=R32(VA(g,0x49b088u)),P=R32(Hc+0x203u),W=R32(P+0x30bu);   /* 432ec7 */
        r.eax=P;r.edx=W;
        if(!W)goto al1;                                     /* 432edb no weapon */
        a[0]=W;a[1]=R32(P+0x1a4u);r.ecx=R32(P+0x1a4u);      /* 432ee9 40e4ac(W, target) */
        hk_at(s,0x432eeeu,0,0);hk_call(g,0x40e4acu,a,2,&r);
        goto al1;
    } else if(kc==0x13u) {                                  /* 432f71 R */
        const uint32_t Hc=R32(VA(g,0x49b088u)),P=R32(Hc+0x203u);
        a[0]=P;r.eax=Hc;r.edx=P;                            /* 432f7d 41b468(P) */
        hk_at(s,0x432f82u,0,0);hk_call(g,0x41b468u,a,1,&r);
        goto al1;
    } else if(kc==0x1cu) {                                  /* 432e2f Enter */
        if(M==4u) { r.eax=0;goto done; }                    /* 432e34 */
        if(M==1u)goto deflt;                                /* 432e48 */
        const uint32_t mfd=R32(H+0x1edu),mode=R32(mfd+0xc1u);   /* 432e4e MFD mode */
        r.ecx=mfd;
        if(mode==3u) { r.eax=0;a[0]=mfd;a[1]=9u;a[2]=1;hk_at(s,0x432e8au,0,0);hk_call(g,0x438d9cu,a,3,&r);goto al1; }  /* 432e7a */
        if(mode==4u) { r.eax=0;r.edx=mfd;a[0]=mfd;a[1]=7u;a[2]=1;hk_at(s,0x432e72u,0,0);hk_call(g,0x438d9cu,a,3,&r);goto al1; }  /* 432e62 */
        r.eax=mode-4u;a[0]=H;                               /* 432e92 43349c(H) next target */
        hk_at(s,0x432e98u,0,0);hk_call(g,0x43349cu,a,1,&r);
        goto al1;
    } else if(kc==0x27u) {                                  /* 432e9e ';' clear target */
        r.eax=0x27u;a[0]=H;a[1]=0;
        hk_at(s,0x432ea6u,0,0);hk_call(g,0x4332dcu,a,2,&r);
        goto al1;
    } else if(kc==0x57u) {                                  /* 432ef6 F11 modal dialog */
        a[0]=0x3b8u;r.eax=0;                                /* 432efb 47749c(0x3b8) */
        hk_at(s,0x432f00u,0,0);hk_call(g,0x47749cu,a,1,&r);
        r.ecx=0x3b8u;                                       /* 432f00 pop ecx */
        W32(f-4u,r.eax);                                    /* 432f01 */
        if(r.eax) {                                         /* 432f04 */
            W16(f-0x18u,0x14u);                             /* 432f08 try-state */
            a[0]=R32(f-4u);a[1]=1;a[2]=0;r.edx=R32(f-4u);   /* 432f16 457538(obj, 1, 0) */
            hk_at(s,0x432f1bu,0,0);hk_call(g,0x457538u,a,3,&r);
            const uint32_t c=R32(VA(g,0x6cbee4u));W32(c,R32(c)-0xbu);   /* 432f1e */
            r.ecx=c;
            W16(f-0x18u,8u);
        }
        const uint32_t obj=R32(f-4u);                       /* 432f2d/432f32 ebx = [f-4] */
        s->ebx=obj;
        a[0]=obj;r.eax=R32(obj);                            /* 432f36 dialog virtual +8 */
        hk_at(s,0x432f3bu,0,0);hk_vcall(g,R32(r.eax+8u),8u,a,1,&r);
        r.ecx=obj;                                          /* 432f3b pop ecx */
        W32(f-8u,obj);                                      /* 432f3c */
        if(R32(f-8u)) {                                     /* 432f3f (0 -> al1) */
            const uint32_t c=R32(VA(g,0x6cbee4u));W32(c,R32(c)+9u);   /* 432f49 */
            r.edx=c;
            W16(f-0x18u,0x2cu);
            a[0]=R32(f-8u);a[1]=3u;r.ecx=R32(f-8u);         /* 432f5e 4543b0(obj, 3) */
            hk_at(s,0x432f63u,0,0);hk_call(g,0x4543b0u,a,2,&r);
            W16(f-0x18u,0x20u);
        }
        goto al1;
    } else if(kc==0x220u) {                                 /* 432f88 Alt+D nav marker */
        r.eax=0;
        hk_at(s,0x432f8du,0,0);hk_call(g,0x434974u,NULL,0,&r);
        goto al1;
    }
deflt:
    /* EAX = the remainder the compare chain leaves on the way here (also
     * from 432c9d with kc 2..0Bh and 432e48 with Enter). */
    r.eax=kc<=0xeu?kc-0xcu:kc<=0x27u?kc-0x1cu:kc<=0x44u?kc-0x43u:kc-0x220u;
    a[0]=R32(H+0x1edu);a[1]=code;r.ecx=a[0];                /* 432f92 446c38(MFD, edi=full code) */
    hk_at(s,0x432f9fu,0,0);hk_call(g,0x446c38u,a,2,&r);
    if((uint8_t)r.eax)goto al1;                            /* 432fa2 handled */
    s->esi=M-1u;                                            /* 432fb7 dec esi */
    if(M==1u) { if(hk_offer(m,s,H,0x1f9u,code,0x4330fbu,0,&r))goto al1;goto al0; }   /* 4330ee */
    {   static const uint32_t offs[]={0x1d8u,0x1e1u,0x1e5u,0x1e9u,0x1f1u,0x1f5u,0x1edu};
        static const uint32_t rets[]={0x432fcfu,0x432ff4u,0x433019u,0x43303eu,0x433063u,0x433088u,0x4330adu};
        for(unsigned i=0;i<sizeof(offs)/sizeof(offs[0]);++i) {
            if(i==0u&&!R32(H+0x1d8u))continue;             /* 432fc4 [H+0x1d8] null-checked */
            if(hk_offer(m,s,H,offs[i],code,rets[i],i==0u,&r))goto al1;
        }
    }
    if(sx8(R8(VA(g,0x4d2538u)))==1u) {                      /* 4330c2 movsx ecx; dec ecx */
        r.ecx=0;if(hk_offer(m,s,H,0x1f9u,code,0x4330d9u,1,&r))goto al1;
    } else r.ecx=sx8(R8(VA(g,0x4d2538u)))-1u;
    goto al0;
al1:
    r.eax=(r.eax&0xffffff00u)|1u;                           /* 43311e mov al,1 */
    goto done;
al0:
    r.eax=0;                                                /* 433110 xor eax,eax */
done:
    *fs_head=R32(f-0x28u);
    return pair(r.eax,R32(f-0x28u));                        /* EDX = saved head */
}
uint64_t es2_hud_key_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t H,uint32_t code,const uint32_t entry[3],uint32_t *fs_head) {
    return hk_key(m,g,NULL,0,H,code,entry,fs_head);
}
uint64_t es2_hud_key_frame_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_hud_key_frame *frame,
    uint32_t caller_esi,uint32_t H,uint32_t code,const uint32_t entry[3],uint32_t *fs_head) {
    return hk_key(m,g,frame,caller_esi,H,code,entry,fs_head);
}
