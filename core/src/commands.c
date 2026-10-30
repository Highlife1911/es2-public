#include "es2/core/commands.h"
#include "fixed.h"
#include <stddef.h>

/* The command handler 45feb8 and the playback key check 45fe6c
 * (docs/reverse-engineering/input.md, section "command handler as own code"). r
 * holds what the original's EAX/EDX/ECX hold at the next game call, ebx/esi/edi
 * the original's callee-saved registers (stored in *saved before every call for
 * the adapter's bridge). The frame lives in the scratch at the original EBP
 * offsets (f = the synthetic EBP; 49630c fills the record at f-0x38 through the
 * bridge). Memory is read again wherever the original reads it. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define VA(g,x) ((g)->image+(uint32_t)(x)-0x400000u)
static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static uint32_t sx8(uint32_t x) { return (uint32_t)(int32_t)(int8_t)(uint8_t)x; }
static uint32_t sx16(uint32_t x) { return (uint32_t)fx_s16(x); }
static uint32_t lo16(uint32_t x,uint32_t w) { return (x&0xffff0000u)|(w&0xffffu); }

typedef struct ck_state {
    const es2_weapon_graph *g;
    es2_commands_saved *saved;
    es2_weapon_frame *frame;
    es2_weapon_registers r;
    uint32_t ebx,esi,edi;
} ck_state;
/* Before a call: the original's EBX/ESI/EDI for the plain bridge (*saved)
 * or, with the return address of the CALL (ret, an original VA), for the
 * stack bridge (*frame). */
static void ck_at(ck_state *s,uint32_t ret) {
    if(s->saved) { s->saved->ebx=s->ebx;s->saved->esi=s->esi;s->saved->edi=s->edi; }
    if(s->frame) { s->frame->ebx=s->ebx;s->frame->esi=s->esi;s->frame->edi=s->edi;s->frame->ret=VA(s->g,ret); }
}
/* A direct call names the original VA (observer key); the bridge gets the
 * address in this image. A virtual call passes an already-resolved target. */
static void ck_call(ck_state *s,uint32_t va,const uint32_t *a,unsigned n,uint32_t ret) {
    ck_at(s,ret);
    if(s->g->observe)s->g->observe(s->g->observer_context,va,0);
    s->g->call(s->g->context,VA(s->g,va),a,n,&s->r);
}
static void ck_vcall(ck_state *s,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,uint32_t ret) {
    ck_at(s,ret);
    if(s->g->observe)s->g->observe(s->g->observer_context,target,slot);
    s->g->call(s->g->context,target,a,n,&s->r);
}

/* The view-mode request of keys V (4603d9) and Ctrl+F (4602e1, modes 1 and
 * 2): 42a3f4(D, request), then word 4d251a <- state. EAX/EDX/ECX are the
 * caller's (set at the call site). */
static void ck_request(const es2_guest_memory *m,ck_state *s,uint32_t D,uint32_t request,uint32_t state,uint32_t ret) {
    uint32_t a[2]={D,request};
    ck_call(s,0x42a3f4u,a,2,ret);
    W16(VA(s->g,0x4d251au),state);
}

/* Original: DBSIM.EXE 0x45fe6c */
uint64_t es2_playback_key_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t code,uint32_t ebx,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t result=(ebx&0xffff0000u)|1u;                   /* 45fe70 mov bx,1 */
    uint32_t ax=(code-0x21cu)&0xffffu;                      /* 45fe74 mov ax,[ebp+8]; sub ax,21Ch */
    if(!ax) {                                               /* 45fe7c Alt+Enter */
        if(!R16(VA(g,0x4d24d6u))||!R16(VA(g,0x4d252cu))) {  /* 45fe95, 45fe9f */
            r.eax=lo16(r.eax,0);
            if(g->observe)g->observe(g->observer_context,0x4667c8u,0);
            g->call(g->context,VA(g,0x4667c8u),NULL,0,&r);  /* 45fea9 display switch */
        }
    } else if((ax-0x214u)&0xffffu) result=0;                /* 45fe7e sub ax,214h; jne -> xor ebx,ebx */
    else if(R16(VA(g,0x4d252cu))) {                         /* 45fe84 Ctrl+B with the option word */
        r.eax=lo16(r.eax,0);
        if(g->observe)g->observe(g->observer_context,0x467ad8u,0);
        g->call(g->context,VA(g,0x467ad8u),NULL,0,&r);      /* 45fe8e int3 */
    }
    return pair(result,r.edx);                              /* 45feb2 mov eax,ebx */
}

/* Original: DBSIM.EXE 0x45feb8. Frame locals from the synthetic EBP f: the
 * SEH record f-0x38 (saved FS:0 head), its try-state word f-0x28, the
 * dialog objects f-4/f-8 (Q), f-0xc/f-0x10 (Ctrl+Q), f-0x14/f-0x18 (P) and
 * f-0x5c (Q), the Alt+Ctrl+N loop f-0x3c (index word), f-0x40 (list
 * cursor), f-0x44 (actor), the Ctrl+N/P cursor f-0x48 with its copies
 * f-0x4c/f-0x50, and the N loop's group f-0x54 and cursor copy f-0x58. */
static uint64_t ck_key(const es2_guest_memory *m,const es2_weapon_graph *g,es2_commands_saved *saved,
    es2_weapon_frame *frame,uint32_t caller_ebx,uint32_t caller_edi,uint32_t code,const uint32_t entry[3],uint32_t *fs_head) {
    ck_state st={g,saved,frame,{entry[0],entry[1],entry[2]},caller_ebx,code,caller_edi},*s=&st;
    es2_weapon_registers *r=&st.r;uint32_t a[4];
    const uint32_t f=g->scratch+ES2_COMMANDS_FRAME;
    /* The caller's EBX and EDI reach only 49630c and 432bc8, which save them
     * before writing them (0 without the entry shim). */
    r->eax=VA(g,0x49f648u);                                 /* 45febe exception descriptor */
    ck_call(s,0x49630cu,NULL,0,0x45feceu);                  /* 45fec9 sets up the SEH record */
    if(!R16(VA(g,0x4d2564u))) {                             /* 45fece commands disabled */
        *fs_head=R32(f-0x38u);
        return pair(0,*fs_head);                            /* 45fed8 xor eax,eax */
    }
    s->edi=0;                                               /* 45fee9 */
    {   const uint32_t H=R32(VA(g,0x49b088u));
        r->eax=H;a[0]=H;a[1]=code;
        ck_call(s,0x432bc8u,a,2,0x45fef7u);                           /* 45fef2 hud_key(H, code) */
    }
    s->ebx=(uint8_t)r->eax;                                 /* 45fefa xor ebx,ebx; mov bl,al */
    if(s->ebx)goto out;                                     /* 45ff01 handled by the HUD */
    a[0]=code;
    ck_call(s,0x45fe6cu,a,1,0x45ff0du);                               /* 45ff08 playback key check */
    r->ecx=a[0];                                            /* 45ff0d pop ecx */
    s->ebx=r->eax;
    if((uint16_t)r->eax)goto out;                           /* 45ff13 Alt+Enter / Ctrl+B */
    s->ebx=lo16(s->ebx,1u);                                 /* 45ff19 mov bx,1 */
    const uint32_t kc=code&0xffffu;
    r->eax=kc;                                              /* 45ff1d movzx eax,si */
    /* The compare chain (signed compares of a zero-extended word); EAX keeps
     * the remainder of its subtractions. */
    if(kc>0x21fu) {                                         /* 45ff25 */
        if(kc>0x431u) {                                     /* 46000d */
            if(kc>0x634u) {                                 /* 460064 */
                r->eax=kc-0x82fu;if(!r->eax)goto shift_v;   /* 46008a */
                r->eax-=6u;if(!r->eax)goto help;            /* 460091 Shift+/ */
                goto deflt;
            }
            if(kc==0x634u)goto slot_up;                     /* 460066 */
            r->eax=kc-0x620u;if(!r->eax)goto damage_view;   /* 46006c */
            r->eax-=0x11u;if(!r->eax)goto damage_near;      /* 460077 */
            r->eax-=2u;if(!r->eax)goto slot_down;           /* 46007c */
            goto deflt;
        }
        if(kc==0x431u)goto view_cycle;                      /* 46000f Ctrl+N */
        if(kc>0x414u) {                                     /* 46001a */
            r->eax=kc-0x419u;if(!r->eax)goto view_cycle;    /* 460046 Ctrl+P */
            r->eax-=8u;if(!r->eax)goto view_free;           /* 460051 Ctrl+F */
            goto deflt;
        }
        if(kc==0x414u)goto view_toggle;                     /* 46001c Ctrl+T */
        r->eax=kc-0x24eu;if(!r->eax)goto step;              /* 460022 Alt+Num+ */
        r->eax-=0x1b4u;if(!r->eax)goto lever;               /* 46002d Ctrl+1 */
        r->eax-=0xeu;if(!r->eax)goto quit;                  /* 460038 Ctrl+Q */
        goto deflt;
    }
    if(kc==0x21fu)goto freeze;                              /* 45ff2b Alt+S */
    if(kc>0x28u) {                                          /* 45ff34 */
        if(kc>0x35u) {                                      /* 45ffc7 */
            r->eax=kc-0x58u;if(!r->eax)goto setup;          /* 45ffef F12 */
            r->eax-=0x1c1u;if(!r->eax)goto setup;           /* 45fff8 Alt+P */
            goto deflt;
        }
        if(kc==0x35u)goto help;                             /* 45ffc9 / */
        r->eax=kc-0x2bu;if(!r->eax)goto align;              /* 45ffcf \ */
        r->eax-=4u;if(!r->eax)goto view;                    /* 45ffd8 V */
        r->eax-=2u;if(!r->eax)goto next_member;             /* 45ffe1 N */
        goto deflt;
    }
    if(kc==0x28u)goto nearest;                              /* 45ff3a ' */
    switch(kc) {                                            /* 45ff43 ja / jump table 45ff50 */
    case 0x01:goto esc;
    case 0x0e:goto backspace;
    case 0x0f:case 0x1c:goto tab;
    case 0x10:goto alert;
    case 0x14:goto lamp;
    case 0x19:goto pause;
    default:goto deflt;
    }

shift_v: {                                                  /* 46009f Shift+V (masked off by 49eab8) */
        const uint32_t C=R32(VA(g,0x4d24eau));r->eax=C;
        if(R16(C+0x36u)!=1u)goto out;
        a[0]=C;a[1]=3u;
        ck_call(s,0x401148u,a,2,0x4600b7u);                           /* 4600b2 camera(C, 3) */
        goto out;
    }
damage_near:                                                /* 4600bf Alt+Ctrl+N */
    if(!R32(VA(g,0x49ef38u)))goto out;
    W16(f-0x3cu,0);
    r->edx=R32(VA(g,0x4a9af8u));W32(f-0x40u,r->edx);
    for(;;) {
        const uint32_t i=R16(f-0x3cu);                      /* 460143 mov ax,[ebp-3Ch] */
        r->eax=lo16(r->eax,i);
        W16(f-0x3cu,i+1u);                                  /* inc word [ebp-3Ch] */
        if(fx_s16(i)>=fx_s16(R16(VA(g,0x4a9afeu))))s->esi=0;    /* 46014b cmp ax,[4a9afeh]; jge */
        else { r->edx=R32(f-0x40u);W32(f-0x40u,r->edx+4u);s->esi=R32(r->edx); }
        if(!s->esi)goto out;                                /* 460161 */
        r->edx=R32(s->esi+0x45u);                           /* 4600dd group */
        r->eax=R32(r->edx+0x14u)?1u:0u;                     /* setne al; and eax,1 */
        if(r->eax)continue;                                 /* 4600ed activation pending */
        r->eax=s->esi+0x92u;                                /* 4600ef */
        if(R8(r->eax+7u))continue;                          /* 4600f5 byte +99h */
        r->edx=sx8(R8(r->edx+0x12u));                       /* 4600fb side */
        if((uint16_t)r->edx!=1u)continue;                   /* 460103 */
        W32(f-0x44u,s->esi);
        r->eax=R32(VA(g,0x4d24e6u));
        r->ecx=R32(f-0x44u)+0x26u;r->eax+=0x26u;
        a[0]=r->eax;a[1]=r->ecx;
        ck_call(s,0x49288cu,a,2,0x46011du);                           /* 460118 dist_gate(P+26h, A+26h) */
        if(fx_s32(r->eax)>=100000)continue;                 /* 460120 */
        r->edx=R32(VA(g,0x4d24e6u));
        a[0]=s->esi;a[1]=0;a[2]=0x7d00u;a[3]=r->edx;
        r->ecx=R32(s->esi);
        ck_vcall(s,R32(r->ecx+0x74u),0x74u,a,4,0x46013bu);            /* 460138 A.+74h(A, 0, 7D00h, P) */
        goto out;
    }
damage_view:                                                /* 46016e Alt+Ctrl+D */
    if(!R32(VA(g,0x49ef38u)))goto out;
    r->edx=lo16(r->edx,R16(VA(g,0x4d2500u)));               /* 46017b mov dx,[4d2500h] */
    r->eax=R32(VA(g,0x4d24e6u));
    r->ecx=R32(VA(g,0x4d2684u));
    a[0]=r->ecx;a[1]=r->edx;a[2]=0x12cu;a[3]=r->eax;
    r->eax=R32(r->ecx);
    ck_vcall(s,R32(r->eax+0x74u),0x74u,a,4,0x46019au);                /* 460197 S.+74h(S, slot, 12Ch, P) */
    goto out;
slot_down:                                                  /* 4601a2 Alt+Ctrl+, */
    if(!R32(VA(g,0x49ef38u)))goto out;
    if(fx_s16(R16(VA(g,0x4d2500u)))<=0)goto out;
    W16(VA(g,0x4d2500u),R16(VA(g,0x4d2500u))-1u);
    goto out;
slot_up:                                                    /* 4601c9 Alt+Ctrl+. */
    if(!R32(VA(g,0x49ef38u)))goto out;
    if(fx_s16(R16(VA(g,0x4d2500u)))>=0x1d)goto out;
    W16(VA(g,0x4d2500u),R16(VA(g,0x4d2500u))+1u);
    goto out;
step:                                                       /* 4601f0 Alt+Num+ single step */
    if(!R32(VA(g,0x49ef38u)))goto out;
    W16(VA(g,0x4d24fcu),1u);
    W16(VA(g,0x4d24f2u),0);
    goto out;
view_cycle: {                                               /* 460214 Ctrl+N / Ctrl+P */
        if(!R32(VA(g,0x49ef38u)))goto out;
        r->edx=R32(VA(g,0x4d24eau));
        if(R32(r->edx+0x4au))goto out;                      /* 46022b camera +4Ah */
        r->ecx=R32(VA(g,0x4d2684u));W32(f-0x48u,r->ecx);
        do {
            if((uint16_t)s->esi==0x431u) {                  /* 46023a cmp si,431h */
                r->eax=R32(f-0x48u);W32(f-0x4cu,r->eax);r->edx=R32(f-0x4cu);
                a[0]=VA(g,0x4a9af8u);a[1]=r->edx;
                ck_call(s,0x411e40u,a,2,0x460255u);                   /* 460250 next entry */
            } else {
                r->ecx=R32(f-0x48u);W32(f-0x50u,r->ecx);r->eax=R32(f-0x50u);
                a[0]=VA(g,0x4a9af8u);a[1]=r->eax;
                ck_call(s,0x411e60u,a,2,0x460271u);                   /* 46026c previous entry */
            }
            W32(f-0x48u,r->eax);
            r->edx=R32(f-0x48u);                            /* 460277 */
        } while(fx_s32(R32(r->edx+0x2eu))<-99000);           /* cmp [edx+2Eh],FFFE7D48h; jl */
        r->ecx=R32(f-0x48u);
        if(r->ecx==R32(VA(g,0x4d2684u)))goto out;           /* 460286 */
        r->eax=R32(f-0x48u);W32(VA(g,0x4d251cu),r->eax);
        if(R16(VA(g,0x4d24eeu))==2u) {                      /* 46029a */
            r->edx=R32(VA(g,0x4d24c0u));
            ck_request(m,s,r->edx,2u,3u,0x4602b2u);                   /* 4602ad 42a3f4(D, 2) */
        } else {
            r->ecx=R32(f-0x48u);a[0]=r->ecx;
            ck_call(s,0x45df34u,a,1,0x4602c9u);                       /* 4602c4 view subject setter */
            r->ecx=a[0];
            W16(VA(g,0x4d2520u),2u);
        }
        W16(VA(g,0x4d24f0u),1u);
        goto out;
    }
view_free:                                                  /* 4602e1 Ctrl+F */
    if(!R32(VA(g,0x49ef38u)))goto out;
    r->eax=R32(VA(g,0x4d24eau));
    if(R32(r->eax+0x4au))goto out;
    W16(VA(g,0x4d2534u),1u);
    r->edx=sx16(R16(VA(g,0x4d24eeu)))-1u;                   /* 460306 movsx; dec */
    if(!r->edx) { r->ecx=R32(VA(g,0x4d24c0u));ck_request(m,s,r->ecx,3u,2u,0x460329u);goto out; }   /* 46031b */
    r->edx-=1u;
    if(!r->edx) { r->eax=R32(VA(g,0x4d24c0u));ck_request(m,s,r->eax,2u,3u,0x460347u);goto out; }   /* 46033a */
    r->edx-=1u;
    if(!r->edx) {                                           /* 460358 mode 3 */
        r->eax=R32(VA(g,0x4d24eau));
        if(!R32(r->eax+0x4au))W16(r->eax+0x36u,0);
        W16(VA(g,0x4d24eeu),1u);
    }
    goto out;
view_toggle:                                                /* 460377 Ctrl+T */
    if(!R32(VA(g,0x49ef38u)))goto out;
    r->edx=R16(VA(g,0x4d24f0u))?0u:1u;                      /* sete dl; and edx,1 */
    W16(VA(g,0x4d24f0u),r->edx);
    goto out;
freeze:                                                     /* 46039e Alt+S */
    if(!R32(VA(g,0x49ef38u))&&!R16(VA(g,0x4d24d6u))&&!R16(VA(g,0x4d24d8u)))goto out;
    r->ecx=R16(VA(g,0x4d24f2u))?0u:1u;                      /* sete cl; and ecx,1 */
    W16(VA(g,0x4d24f2u),r->ecx);
    goto out;
view:                                                       /* 4603d9 V */
    r->eax=R32(VA(g,0x4d24eau));
    if(R32(r->eax+0x4au))goto out;
    W16(VA(g,0x4d2534u),0);
    r->edx=sx16(R16(VA(g,0x4d24eeu)))-1u;
    if(!r->edx)goto view3;                                  /* 4603f9 mode 1 */
    r->edx-=1u;
    if(!r->edx) { r->eax=R32(VA(g,0x4d24c0u));ck_request(m,s,r->eax,2u,3u,0x460435u);goto out; }   /* 460428 mode 2 */
    r->edx-=1u;
    {   const uint32_t d=r->edx;r->edx-=2u;                 /* 4603ff sub edx,2; jb (modes 3, 4) */
        if(d<2u)goto view3;
    }
    goto out;
view3:
    r->ecx=R32(VA(g,0x4d24c0u));ck_request(m,s,r->ecx,3u,2u,0x460417u);   /* 460409 */
    goto out;
next_member: {                                              /* 460446 N */
        r->edx=R32(VA(g,0x4d24eau));
        if(R32(r->edx+0x4au))goto out;
        r->ecx=R32(VA(g,0x4d24c0u));
        if(R32(r->ecx+0x14u)!=4u)goto out;
        if(R16(VA(g,0x4d24eeu))!=3u)goto out;
        r->eax=R32(VA(g,0x4d24e6u));
        s->esi=R32(VA(g,0x4d2684u));
        r->edx=R32(r->eax+0x45u);W32(f-0x54u,r->edx);       /* the player's group */
        for(;;) {
            W32(f-0x58u,s->esi);r->eax=R32(f-0x58u);        /* 460485 */
            a[0]=VA(g,0x4a9af8u);a[1]=r->eax;
            ck_call(s,0x411e40u,a,2,0x460496u);                       /* 460491 next entry */
            s->esi=r->eax;
            if(fx_s32(R32(s->esi+0x2eu))<0)continue;        /* 46049b */
            r->edx=R32(s->esi+0x45u);
            if(r->edx!=R32(f-0x54u))continue;               /* 4604a4 */
            break;
        }
        if(s->esi==R32(VA(g,0x4d2684u)))goto out;           /* 4604a9 */
        a[0]=s->esi;
        ck_call(s,0x45df34u,a,1,0x4604bbu);                           /* 4604b6 view subject setter */
        r->ecx=a[0];
        r->eax=R32(VA(g,0x4d24eau));
        if(!R32(r->eax+0x4au))W16(r->eax+0x36u,1u);
        W32(VA(g,0x4d251cu),s->esi);
        W16(VA(g,0x4d2520u),2u);
        goto out;
    }
tab:                                                        /* 4604e1 Tab, Enter */
    r->edx=R32(VA(g,0x4d24c0u));
    if(R32(r->edx+0x14u)!=4u)goto out;
    ck_call(s,0x45fe38u,NULL,0,0x4604f6u);                            /* 4604f1 view preference toggle */
    goto out;
align: {                                                    /* 4604fb \ */
        s->esi=R32(VA(g,0x4d24e6u));
        W32(VA(g,0x4d2a70u),1u);
        W16(VA(g,0x4d2504u),0);
        r->eax=R32(R32(VA(g,0x4d24e6u))+0x202u)+0x14u;W8(r->eax,0);
        a[0]=s->esi;r->edx=R32(s->esi);
        ck_vcall(s,R32(r->edx+0x3cu),0x3cu,a,1,0x46052bu);            /* 460528 P.+3Ch twist */
        r->ecx=lo16(a[0],R16(s->esi+0x10u)-r->eax);         /* pop ecx; mov cx,[esi+10h]; sub cx,ax */
        W16(VA(g,0x4d2a74u),r->ecx);
        goto out;
    }
backspace:                                                  /* 46053f Backspace */
    W16(VA(g,0x4d2504u),1u);
    r->eax=0;W32(VA(g,0x4d2a70u),0);
    r->edx=R32(VA(g,0x4d24e6u));
    r->eax=R32(r->edx+0x202u)+0x14u;W8(r->eax,0);
    r->edx=R32(VA(g,0x4d24c0u));a[0]=r->edx;
    ck_call(s,0x429828u,a,1,0x46056du);                               /* 460568 */
    r->ecx=VA(g,0x4610adu);                                 /* pop ecx; mov ecx,4610ADh */
    ck_call(s,0x4610adu,NULL,0,0x460575u);                            /* 460573 call ecx: [[49b088h]+1E1h] */
    if(!R8(r->eax+0xb3u))goto out;                          /* 460575 */
    a[0]=r->eax;
    ck_call(s,0x441fa4u,a,1,0x460588u);                               /* 460583 lamp toggle */
    r->ecx=a[0];
    goto out;
lamp: {                                                     /* 46058e T */
        r->eax=R32(VA(g,0x4d24c0u));a[0]=r->eax;
        ck_call(s,0x429828u,a,1,0x460599u);                           /* 460594 */
        r->ecx=a[0];r->edx=VA(g,0x4610adu);
        ck_call(s,0x4610adu,NULL,0,0x4605a1u);                        /* 46059f call edx */
        s->esi=r->eax;a[0]=s->esi;
        ck_call(s,0x441fa4u,a,1,0x4605a9u);                           /* 4605a4 lamp toggle */
        r->ecx=a[0];
        if(!(uint8_t)r->eax)goto out;                       /* 4605aa */
        a[0]=s->esi;
        ck_call(s,0x442120u,a,1,0x4605b8u);                           /* 4605b3 gauge state */
        const uint32_t on=R8(r->eax+2u);                    /* 4605b8 */
        r->ecx=a[0];
        if(on)goto out;
        W16(VA(g,0x4d2504u),1u);
        r->eax=0;W32(VA(g,0x4d2a70u),0);
        r->edx=R32(VA(g,0x4d24e6u));
        r->eax=R32(r->edx+0x202u)+0x14u;W8(r->eax,0);
        goto out;
    }
alert: {                                                    /* 4605ea Q outcome dialog */
        r->edx=R32(VA(g,0x4d24e6u));a[0]=r->edx;a[1]=1u;
        ck_call(s,0x413180u,a,2,0x4605f8u);                           /* 4605f3 status_eval(P, 1) */
        s->esi=r->eax;s->edi=sx16(s->esi);
        a[0]=0x57eu;
        ck_call(s,0x47749cu,a,1,0x46060au);                           /* 460605 allocate */
        r->ecx=0x57eu;
        W32(f-4u,r->eax);
        if(r->eax) {                                        /* 460610 */
            W16(f-0x28u,0x14u);
            r->edx=R32(f-4u);a[0]=r->edx;a[1]=s->edi;a[2]=1u;a[3]=0;
            ck_call(s,0x455950u,a,4,0x460626u);                       /* 460621 alert(obj, status, 1, 0) */
            r->ecx=R32(VA(g,0x6cbee4u));W32(r->ecx,R32(r->ecx)-0xau);
            W16(f-0x28u,8u);
        }
        r->eax=R32(f-4u);
        W32(f-0x5cu,r->eax);
        r->edx=R32(f-0x5cu);a[0]=r->edx;r->ecx=R32(r->edx);
        ck_vcall(s,R32(r->ecx+8u),8u,a,1,0x46064cu);                  /* 460649 modal */
        r->edx=sx16(s->esi);
        r->ecx=sx16(R16(VA(g,0x49f5c0u)+2u*r->edx));        /* 460650 status table */
        r->eax=r->eax==r->ecx?1u:0u;                        /* sete al; and eax,1 */
        s->edi=r->eax;
        r->eax=R32(f-0x5cu);W32(f-8u,r->eax);
        if(R32(f-8u)) {                                     /* 46066c */
            r->edx=R32(VA(g,0x6cbee4u));W32(r->edx,R32(r->edx)+9u);
            W16(f-0x28u,0x2cu);
            r->ecx=R32(f-8u);a[0]=r->ecx;a[1]=3u;
            ck_call(s,0x4543b0u,a,2,0x460688u);                       /* 460683 destroy(obj, 3) */
            W16(f-0x28u,0x20u);
        }
        W16(VA(g,0x4a9e68u),0);
        goto out;
    }
esc:                                                        /* 46069f Esc */
    r->eax=R32(VA(g,0x4d24eau));
    if(R32(r->eax+0x4au))goto out;
    r->edx=R32(VA(g,0x4d24c0u));ck_request(m,s,r->edx,3u,2u,0x4606bcu);   /* 4606b7 */
    goto out;
quit:                                                       /* 4606cd Ctrl+Q */
    a[0]=0x57eu;
    ck_call(s,0x47749cu,a,1,0x4606d7u);                               /* 4606d2 allocate */
    r->ecx=0x57eu;
    W32(f-0xcu,r->eax);
    if(r->eax) {                                            /* 4606dd */
        W16(f-0x28u,0x44u);
        r->edx=R32(f-0xcu);a[0]=r->edx;a[1]=1u;a[2]=1u;a[3]=0;
        ck_call(s,0x4561dcu,a,4,0x4606f4u);                           /* 4606ef dialog(obj, 1, 1, 0) */
        r->ecx=R32(VA(g,0x6cbee4u));W32(r->ecx,R32(r->ecx)-0xbu);
        W16(f-0x28u,0x38u);
    }
    s->esi=R32(f-0xcu);
    a[0]=s->esi;r->eax=R32(s->esi);
    ck_vcall(s,R32(r->eax+8u),8u,a,1,0x460714u);                      /* 460711 modal */
    r->ecx=a[0];
    W32(f-0x10u,s->esi);
    s->edi=r->eax;
    if(R32(f-0x10u)) {                                      /* 46071e */
        r->eax=R32(VA(g,0x6cbee4u));W32(r->eax,R32(r->eax)+9u);
        W16(f-0x28u,0x5cu);
        r->edx=R32(f-0x10u);a[0]=r->edx;a[1]=3u;
        ck_call(s,0x4543b0u,a,2,0x460739u);                           /* 460734 destroy(obj, 3) */
        W16(f-0x28u,0x50u);
    }
    if((uint16_t)s->edi)W16(VA(g,0x4d24feu),1u);            /* 460742 end the main loop */
    goto out;
pause:                                                      /* 460759 P */
    a[0]=0x57eu;
    ck_call(s,0x47749cu,a,1,0x460763u);                               /* 46075e allocate */
    r->ecx=0x57eu;
    W32(f-0x14u,r->eax);
    if(r->eax) {                                            /* 460769 */
        W16(f-0x28u,0x74u);
        r->edx=R32(f-0x14u);a[0]=r->edx;a[1]=0;a[2]=1u;a[3]=0;
        ck_call(s,0x4561dcu,a,4,0x460780u);                           /* 46077b dialog(obj, 0, 1, 0) */
        r->ecx=R32(VA(g,0x6cbee4u));W32(r->ecx,R32(r->ecx)-0xbu);
        W16(f-0x28u,0x68u);
    }
    s->esi=R32(f-0x14u);
    a[0]=s->esi;r->eax=R32(s->esi);
    ck_vcall(s,R32(r->eax+8u),8u,a,1,0x4607a0u);                      /* 46079d modal */
    r->ecx=a[0];
    W32(f-0x18u,s->esi);
    s->edi=r->eax;
    if(!R32(f-0x18u))goto out;                              /* 4607aa */
    r->eax=R32(VA(g,0x6cbee4u));W32(r->eax,R32(r->eax)+9u);
    W16(f-0x28u,0x8cu);
    r->edx=R32(f-0x18u);a[0]=r->edx;a[1]=3u;
    ck_call(s,0x4543b0u,a,2,0x4607c9u);                               /* 4607c4 destroy(obj, 3) */
    W16(f-0x28u,0x80u);
    goto out;
setup:                                                      /* 4607d7 F12, Alt+P */
    ck_call(s,0x45cff0u,NULL,0,0x4607dcu);
    goto out;
lever:                                                      /* 4607de Ctrl+1 throttle lever */
    ck_call(s,0x477904u,NULL,0,0x4607e3u);                            /* joystick summary */
    if(!R8(r->eax+4u))goto out;
    if(R8(VA(g,0x4d1f46u))!=1u)goto out;
    W16(VA(g,0x49a06eu),fx_s16(R16(VA(g,0x49a06eu)))>0?0xffffu:1u);
    goto out;
nearest:                                                    /* 460812 ' */
    r->ecx=R32(VA(g,0x49b088u));a[0]=r->ecx;
    ck_call(s,0x4333c8u,a,1,0x46081eu);                               /* 460819 cockpit_nearest(H) */
    r->ecx=a[0];
    goto out;
help:                                                       /* 460821 /, Shift+/ */
    ck_call(s,0x45f070u,NULL,0,0x460826u);                            /* help file name */
    a[0]=r->eax;
    ck_call(s,0x4669c4u,a,1,0x46082cu);                               /* 460827 WinHelp */
    r->ecx=a[0];
    goto out;
deflt: {                                                    /* 46082f the key method +2Ch */
        const int player=R32(VA(g,0x49ef34u))!=0;
        const uint32_t v=R32(VA(g,player?0x4d24e6u:0x4d2684u));
        r->eax=v;a[0]=v;a[1]=s->esi;r->edx=R32(v);
        ck_vcall(s,R32(r->edx+0x2cu),0x2cu,a,2,player?0x460844u:0x460857u);   /* 460841 / 460854 */
        s->ebx=r->eax;
    }
out:
    *fs_head=R32(f-0x38u);
    return pair(s->edi+s->ebx,*fs_head);                    /* 46085c lea eax,[edi+ebx]; edx=[ebp-38h] */
}
uint64_t es2_command_key_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    es2_commands_saved *saved,uint32_t code,const uint32_t entry[3],uint32_t *fs_head) {
    return ck_key(m,g,saved,NULL,0,0,code,entry,fs_head);
}
uint64_t es2_command_key_frame_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_weapon_frame *frame,
    uint32_t caller_ebx,uint32_t caller_edi,uint32_t code,const uint32_t entry[3],uint32_t *fs_head) {
    return ck_key(m,g,NULL,frame,caller_ebx,caller_edi,code,entry,fs_head);
}
