#include "es2/core/mfd_orders.h"
#include "fixed.h"
#include <stddef.h>
/* The MFD order path and the set-up of the order group
 * (docs/reverse-engineering/cockpit.md, section "MFD order path"). Every guest
 * read and write is done in the original's order and repeated where the
 * original repeats it; the registers EAX/EDX/ECX are carried through each
 * function as the original leaves them, so every game call sees the original's
 * entry registers and the functions return the original's EDX:EAX. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define VA(x) (g->image+(uint32_t)(x)-0x400000u)
static uint64_t mo_pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
/* A 16-bit register write keeps the high half. */
static uint32_t lo16(uint32_t r,uint32_t v) { return (r&0xffff0000u)|(v&0xffffu); }
static uint32_t lo8(uint32_t r,uint32_t v) { return (r&0xffffff00u)|(v&0xffu); }
static uint32_t sx8(uint32_t v) { return (uint32_t)(int32_t)(int8_t)(uint8_t)v; }
static uint32_t sx16(uint32_t v) { return (uint32_t)fx_s16(v); }

/* One function's state: the original's EBX/ESI/EDI and EAX/EDX/ECX. */
typedef struct mo_state {
    const es2_guest_memory *m;
    const es2_weapon_graph *g;
    es2_mfd_frame *frame;
    uint32_t ebx, esi, edi;
    es2_weapon_registers r;
} mo_state;
/* One game call returning to `ret` (original VA). A direct call names the
 * original VA (observer key) and the bridge gets its address in this
 * image; a virtual passes the resolved target under its slot key. */
static void mo_call(mo_state *s,uint32_t target,unsigned slot,uint32_t ret,const uint32_t *a,unsigned n) {
    const es2_weapon_graph *g=s->g;
    if(s->frame) {
        s->frame->call.ebx=s->ebx;s->frame->call.esi=s->esi;s->frame->call.edi=s->edi;
        s->frame->call.ret=VA(ret);
    }
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,slot?target:VA(target),a,n,&s->r);
}
/* POP ECX after a one-slot call: the slot as the callee left it. */
static void mo_pop(mo_state *s,uint32_t value) {
    const es2_guest_memory *m=s->m;
    s->r.ecx=s->frame&&s->frame->top?R32(s->frame->top-4u):value;
}

/* Original: DBSIM.EXE 0x43f9c0 */
uint64_t es2_menu_code_registers(const es2_guest_memory *m,uint32_t menu,uint32_t *ecx) {
    const uint32_t e=R32(menu+0x32u);                       /* 43f9c6 */
    uint32_t code;
    if(R8(menu+e+0x2cu)&2u)code=R32(menu+0x32u)+3u;         /* 43f9c9 test, 43f9d0 reread, add */
    else code=R32(menu+0x32u);                              /* 43f9d8 reread */
    *ecx=code;
    return mo_pair(code,e);
}

/* Original: DBSIM.EXE 0x43f9f8 */
uint64_t es2_menu_toggle_registers(const es2_guest_memory *m,uint32_t menu,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t e=R32(menu+0x32u);                       /* 43f9fe */
    *ecx=entry[2];
    if(e-4u<2u) {                                           /* 43fa01 add -4, 43fa04 sub 2; jb */
        const uint32_t k=R32(menu+0x32u);                   /* 43fa0b reread */
        *ecx=k;
        W8(menu+k+0x2cu,R8(menu+k+0x2cu)^2u);               /* 43fa0e xor */
        W8(menu+0x36u,1u);                                  /* 43fa13 */
    }
    return mo_pair(menu,e-6u);
}

/* Original: DBSIM.EXE 0x4470a4 */
uint64_t es2_mfd_entry_registers(const es2_guest_memory *m,uint32_t gauge,uint32_t index,const uint32_t entry[3],
    uint32_t *ecx) {
    *ecx=entry[2];
    if(index!=R32(gauge+0xb1u))W32(gauge+0xb1u,index);     /* 4470ad cmp, 4470b5 */
    return mo_pair(gauge,index);
}

/* Original: DBSIM.EXE 0x446fc4 */
uint64_t es2_mfd_view_next_registers(const es2_guest_memory *m,uint32_t gauge,const uint32_t entry[3],uint32_t *ecx) {
    *ecx=entry[2];
    W32(gauge+0x318u,R32(gauge+0x318u)+1u);                 /* 446fca inc dword */
    const uint32_t v=R32(gauge+0x318u);                     /* 446fd0 */
    if(v==R32(gauge+0x31cu)) {                              /* 446fd6 cmp; jne */
        *ecx=0;
        W32(gauge+0x318u,0u);                               /* 446fde xor, 446fe0 */
    }
    return mo_pair(gauge,v);
}

/* Original: DBSIM.EXE 0x446fb0 */
uint64_t es2_mfd_target_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_mfd_frame *frame,
    const uint32_t caller[3],uint32_t gauge,const uint32_t entry[3]) {
    (void)gauge;
    mo_state s={m,g,frame,caller[0],caller[1],caller[2],{entry[0],entry[1],entry[2]}};
    const uint32_t H=R32(VA(0x49b088u));                    /* 446fb3 */
    s.r.eax=H;
    const uint32_t a[1]={H};
    mo_call(&s,0x43349cu,0,0x446fbeu,a,1);                  /* 446fb9 cockpit_next(H) */
    mo_pop(&s,H);                                           /* 446fbe pop ecx */
    return mo_pair(s.r.eax,s.r.edx);
}

/* Original: DBSIM.EXE 0x446ff0 */
uint64_t es2_mfd_range_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_mfd_frame *frame,
    const uint32_t caller[2],uint32_t gauge,const uint32_t entry[3]) {
    mo_state s={m,g,frame,0,caller[0],caller[1],{gauge,entry[1],entry[2]}};   /* 446ff4 EAX = G */
    uint32_t i=R32(gauge+0xc5u)+1u;                         /* 446ff7, 446ffd inc */
    if(i==3u)i=0;                                           /* 446ffe cmp 3, 447003 xor */
    s.ebx=i;
    const uint32_t a[2]={gauge,i};
    mo_call(&s,0x447014u,0,0x44700cu,a,2);                  /* 447007 range set 447014(G, i) */
    return mo_pair(i,s.r.edx);                              /* 44700f mov eax,ebx */
}

/* Original: DBSIM.EXE 0x43166c */
uint64_t es2_order_call_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_mfd_frame *frame,
    const uint32_t caller[3],uint32_t cockpit,uint32_t record,const uint32_t entry[3]) {
    mo_state s={m,g,frame,caller[0],caller[1],caller[2],{entry[0],entry[1],entry[2]}};
    const uint32_t player=R32(cockpit+0x203u);              /* 431672, 431675 */
    W32(record+2u,player);                                  /* 43167b */
    const uint32_t group=R32(VA(0x49b0f8u));                /* 43167f */
    s.r.eax=player;s.r.edx=group;
    const uint32_t a[2]={group,record};
    mo_call(&s,0x4231a4u,0,0x43168bu,a,2);                  /* 431686 group order */
    return mo_pair(s.r.eax,s.r.edx);
}

/* Original: DBSIM.EXE 0x4231a4. The locals at f: -4 best member, -6 result
 * word s, -10h..-7 the done flags, -12h best index, -18h best distance,
 * -1Ah best priority, -1Ch priority w, -20h flag pointer; Gp at f+8. */
uint64_t es2_group_order_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_mfd_frame *frame,
    uint32_t f,const uint32_t caller[2],uint32_t group,uint32_t record,const uint32_t entry[3]) {
    (void)group;
    mo_state s={m,g,frame,caller[0],caller[1],record,{entry[0],entry[1],entry[2]}};   /* 4231ad EDI = R */
    const uint32_t R=record;
    W16(f-6u,0u);                                           /* 4231b0 */
    s.r.eax=f-0x10u;                                        /* 4231ba lea */
    {
        const uint32_t a[3]={f-0x10u,0u,10u};
        mo_call(&s,0x49681au,0,0x4231c3u,a,3);              /* 4231be memset(f-10h, 0, 10) */
    }
    for(;;) {
        /* 4231c6: a pass. */
        W32(f-0x18u,0x7fffffffu);
        W16(f-0x1au,0xffffu);
        s.r.edx=0;                                          /* 4231d3 */
        s.r.eax=f-0x10u;                                    /* 4231d5 */
        W32(f-4u,0u);                                       /* 4231d8 */
        W32(f-0x20u,s.r.eax);                               /* 4231db */
        s.esi=0;                                            /* 4231de */
        for(;;) {
            s.r.eax=R32(f+8u);                              /* 4232da */
            if(fx_s16(s.esi)>=fx_s16(R16(s.r.eax+0x10u)))break;   /* 4232dd cmp si,[eax+10h]; jl */
            s.r.edx=R32(f-0x20u);                           /* 4231e5 */
            if(R8(s.r.edx)==0) {                            /* 4231e8 */
                s.r.ecx=sx16(s.esi);                        /* 4231f1 movsx */
                s.r.eax=R32(f+8u);                          /* 4231f4 */
                s.r.edx=R32(s.r.eax+0xcu);                  /* 4231f7 */
                const uint32_t B=R32(s.r.edx+4u*s.r.ecx);   /* 4231fa */
                s.ebx=B;
                s.r.eax=B+0x92u;                            /* 4231fd */
                if(R8(B+0x99u)==0&&B!=R32(R+2u)) {          /* 423203, 42320d */
                    s.r.eax=R32(B+0x4du)+8u;                /* 423216, 423219 */
                    s.r.edx=lo8(s.r.edx,R8(s.r.eax+1u));    /* 42321c mov dl */
                    s.r.ecx=sx8(s.r.edx);                   /* 42321f movsx ecx,dl */
                    s.r.eax=s.r.ecx;                        /* 423222 */
                    const uint32_t code=R16(R);             /* 423224 movzx */
                    s.r.edx=code;
                    if(code<=0xfu) {                        /* 423227 cmp 0Fh; ja */
                        static const uint8_t table[16]={3,1,3,0,4,3,0,2,1,0,0,0,0,0,0,1};   /* 423239 */
                        s.r.edx=lo8(s.r.edx,table[code]);   /* 42322c mov dl */
                        switch(table[code]) {
                        case 3:                             /* 42325d test ax; sete al; and eax,1 */
                            s.r.eax=(s.r.eax&0xffffu)==0?1u:0u;
                            W16(f-0x1cu,s.r.eax);
                            break;
                        case 1:                             /* 42326c */
                            W16(f-0x1cu,s.r.eax);
                            break;
                        case 4:                             /* 423272 */
                            s.r.eax=B+0x92u;
                            s.r.edx=lo8(s.r.edx,R8(B+0x96u)==0?1u:0u)&1u;
                            W16(f-0x1cu,s.r.edx);
                            break;
                        case 2:                             /* 423288 */
                            s.r.eax=B+0x92u;
                            s.r.edx=lo8(s.r.edx,R8(B+0x96u));
                            s.r.ecx=sx8(s.r.edx);
                            W16(f-0x1cu,s.r.ecx);
                            break;
                        default:                            /* 0: 423298 directly, w is the residue */
                            break;
                        }
                    }
                    /* 423298: the priority test. */
                    const uint32_t w=R16(f-0x1cu);
                    s.r.eax=lo16(s.r.eax,w);
                    if(fx_s16(w)>=fx_s16(R16(f-0x1au))) {   /* 42329c cmp ax,[ebp-1Ah]; jl */
                        const uint32_t issuer=R32(R+2u)+0x26u;   /* 4232a2, 4232a5 */
                        s.r.edx=issuer;
                        s.r.ecx=B+0x26u;                    /* 4232a9 */
                        const uint32_t a[2]={B+0x26u,issuer};
                        mo_call(&s,0x49288cu,0,0x4232b2u,a,2);   /* 4232ad dist_gate */
                        const uint32_t d=s.r.eax;
                        int take=(int32_t)d<(int32_t)R32(f-0x18u);   /* 4232b5 cmp eax,[ebp-18h]; jl */
                        if(!take) {
                            const uint32_t w2=R16(f-0x1cu);  /* 4232ba mov dx */
                            s.r.edx=lo16(s.r.edx,w2);
                            take=fx_s16(w2)>fx_s16(R16(f-0x1au));   /* 4232be cmp dx; jle */
                        }
                        if(take) {
                            const uint32_t w3=R16(f-0x1cu); /* 4232c4 mov cx */
                            s.r.ecx=lo16(s.r.ecx,w3);
                            W16(f-0x1au,w3);                /* 4232c8 */
                            W32(f-0x18u,d);                 /* 4232cc */
                            W32(f-4u,B);                    /* 4232cf */
                            W16(f-0x12u,s.esi);             /* 4232d2 */
                        }
                    }
                }
            }
            s.esi+=1u;                                      /* 4232d6 inc esi */
            W32(f-0x20u,R32(f-0x20u)+1u);                   /* 4232d7 inc dword [ebp-20h] */
        }
        /* 4232e7: the pass's best member. */
        if(R32(f-4u)==0)break;
        s.r.eax=lo16(s.r.eax,R16(f-6u));                    /* 4232ed mov ax,[ebp-6] over EAX = Gp */
        const uint32_t best=R32(f-4u);                      /* 4232f3 */
        s.r.edx=best;
        const uint32_t table=R32(best);                     /* 4232f7 */
        s.r.ecx=table;
        const uint32_t target=R32(table+0x28u);
        {
            const uint32_t a[3]={best,R,s.r.eax};
            mo_call(&s,target,0x28u,0x4232fcu,a,3);         /* 4232f9 call [ecx+28h](B, R, mode) */
        }
        if(s.r.eax&0xffffu)W16(f-6u,2u);                    /* 4232ff test ax; 423304 */
        else if(R16(f-6u)==0)W16(f-6u,1u);                  /* 42330c, 423313 */
        s.r.eax=sx16(R16(f-0x12u));                         /* 423319 movsx */
        W8(f+s.r.eax-0x10u,1u);                             /* 42331d */
        const uint32_t code=R16(R);                         /* 423322 mov ax,[edi] */
        s.r.eax=lo16(s.r.eax,code);
        if((code==0||code==2u)&&R16(f-6u)==2u) {            /* 423325 test, 42332a cmp 2, 423330 cmp [ebp-6],2 */
            s.r.edx=0;                                      /* 423337 xor */
            W32(f-4u,0u);                                   /* 423339 */
        }
        if(R32(f-4u)==0)break;                              /* 42333c cmp [ebp-4],0; jne 4231c6 */
    }
    s.r.eax=R16(f-6u)==2u?1u:0u;                            /* 423346 cmp; sete al; and eax,1 */
    return mo_pair(s.r.eax,s.r.edx);
}

/* Original: DBSIM.EXE 0x447248 */
uint64_t es2_mfd_order_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_mfd_frame *frame,
    uint32_t gauge,const uint32_t entry[3]) {
    mo_state s={m,g,frame,gauge,0,0,{entry[0],entry[1],entry[2]}};   /* 44724e EBX = G */
    const uint32_t R=VA(0x4d03d4u);
    s.esi=R;                                                /* 447251 */
    const uint32_t menu=R32(gauge+0xd1u);                   /* 447256 */
    s.edi=menu;
    {
        const uint32_t a[1]={menu};
        mo_call(&s,0x43f9c0u,0,0x447262u,a,1);              /* 44725d order code 43f9c0(menu) */
        mo_pop(&s,menu);                                    /* 447262 pop ecx */
    }
    W16(R,s.r.eax);                                         /* 447263 mov [esi],ax */
    s.r.eax=R32(gauge+0x14u);                               /* 447267 */
    {
        const uint32_t a[2]={s.r.eax,R};
        mo_call(&s,0x43166cu,0,0x447270u,a,2);              /* 44726b order call */
    }
    if(s.r.eax) {                                           /* 447273 test eax; je */
        const uint32_t a[1]={menu};
        mo_call(&s,0x43f9f8u,0,0x44727du,a,1);              /* 447278 toggle 43f9f8(menu) */
        mo_pop(&s,menu);                                    /* 44727d pop ecx */
    }
    return mo_pair(s.r.eax,s.r.edx);
}

/* Original: DBSIM.EXE 0x446844. Jump table 446873 over the button index
 * 0..0Ch (446863 cmp ecx,0Ch; ja). */
uint64_t es2_mfd_button_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_mfd_frame *frame,
    uint32_t caller_esi,uint32_t caller_edi,uint32_t gauge,uint32_t button,const uint32_t entry[3]) {
    (void)entry;
    mo_state s={m,g,frame,gauge,caller_esi,caller_edi,{0,0,0}};
    const uint32_t G=gauge;
    s.r.ecx=button;                                         /* 446849 */
    s.r.eax=0;                                              /* 44684f */
    s.r.edx=G+0x18u;                                        /* 446851 */
    for(;;) {
        if((int32_t)s.r.eax>=(int32_t)R32(G+0x68u))         /* 4469db cmp eax,[ebx+68h]; jl */
            return mo_pair(s.r.eax,s.r.edx);
        if(s.r.ecx==R32(s.r.edx))break;                     /* 446859 cmp ecx,[edx]; jne */
        s.r.eax+=1u;s.r.edx+=4u;                            /* 4469d7, 4469d8 */
    }
    const uint32_t i=s.r.eax;
    s.r.ecx=i;                                              /* 446861 */
    if(i>0xcu)return mo_pair(s.r.eax,s.r.edx);              /* 446863 ja */
    const uint32_t a1[1]={G};
    switch(i) {
    case 0:case 1:case 2:case 3:case 4:case 5: {
        /* 4468a7 / 4468b6 / 4468c5: the page switch 446e60(G, i). */
        const uint32_t a[2]={G,i};
        mo_call(&s,0x446e60u,0,i==0?0x4468aeu:i==4?0x4468bdu:0x4468ccu,a,2);
        break;
    }
    case 8:                                                 /* 4468d4 */
        mo_call(&s,0x446ff0u,0,0x4468dau,a1,1);
        mo_pop(&s,G);
        s.r.eax=R32(G+0xc1u);                               /* 4468db */
        W8(G+s.r.eax+0xe5u,1u);                             /* 4468e1 */
        break;
    case 7:case 9:                                          /* 4468ee */
        if(R32(G+0xc1u)==0) {
            mo_call(&s,0x446fc4u,0,0x4468fdu,a1,1);         /* 4468f8 view cycle */
            mo_pop(&s,G);
        } else {
            mo_call(&s,0x446fb0u,0,0x446906u,a1,1);         /* 446901 next target */
            mo_pop(&s,G);
        }
        s.r.edx=R32(G+0xc1u);                               /* 446907 */
        W8(G+s.r.edx+0xe5u,1u);                             /* 44690d */
        s.r.ecx=R32(G+0xc1u);                               /* 446915 */
        s.r.eax=R32(G+4u*s.r.ecx+0xcdu);                    /* 44691b */
        W32(s.r.eax+0xcu,0x64u);                            /* 446922 */
        break;
    case 10:                                                /* 44692e */
        if(R8(VA(0x49dbe0u)))break;
        mo_call(&s,0x447248u,0,0x446941u,a1,1);             /* 44693c the order */
        mo_pop(&s,G);
        s.r.edx=R32(G+0xc1u);                               /* 446942 */
        W8(G+s.r.edx+0xe5u,1u);                             /* 446948 */
        break;
    case 11: {                                              /* 446955 */
        const uint32_t H=R32(VA(0x49b088u));
        s.r.ecx=H;
        s.r.eax=R32(H+0x203u)+0x92u;                        /* 44695b, 446961 */
        W8(s.r.eax+4u,0u);                                  /* 446966 */
        const uint32_t L1=R32(G+0x44u);                     /* 44696a */
        s.esi=L1;
        if(R8(L1+0x40u)==0) {                               /* 44696d cmp; jne */
            W8(L1+0x40u,1u);                                /* 446973 */
            s.r.eax=R32(L1+0x17u);                          /* 446978 */
            const uint32_t a[1]={L1};
            mo_call(&s,R32(s.r.eax+4u),0x44u,0x44697eu,a,1);   /* 44697b call [eax+4] */
            mo_pop(&s,L1);
        }
        const uint32_t L2=R32(G+0x48u);                     /* 44697f */
        s.r.eax=L2;
        if(R8(L2+0x40u))W8(L2+0x40u,0u);                    /* 446982 cmp; je; 446988 */
        s.r.edx=R32(L2+0x17u);                              /* 44698d */
        const uint32_t a[1]={L2};
        mo_call(&s,R32(s.r.edx+4u),0x48u,0x446993u,a,1);    /* 446990 call [edx+4] */
        mo_pop(&s,L2);
        break;
    }
    case 12: {                                              /* 446996 */
        const uint32_t H=R32(VA(0x49b088u));
        s.r.ecx=H;
        s.r.eax=R32(H+0x203u)+0x92u;                        /* 44699c, 4469a2 */
        W8(s.r.eax+4u,1u);                                  /* 4469a7 */
        const uint32_t L1=R32(G+0x44u);                     /* 4469ab */
        s.r.eax=L1;
        if(R8(L1+0x40u))W8(L1+0x40u,0u);                    /* 4469ae cmp; je; 4469b4 */
        s.r.edx=R32(L1+0x17u);                              /* 4469b9 */
        {
            const uint32_t a[1]={L1};
            mo_call(&s,R32(s.r.edx+4u),0x44u,0x4469bfu,a,1);   /* 4469bc call [edx+4] */
            mo_pop(&s,L1);
        }
        const uint32_t L2=R32(G+0x48u);                     /* 4469c0 mov ebx,[ebx+48h] */
        s.ebx=L2;
        if(R8(L2+0x40u))break;                              /* 4469c3 cmp; jne */
        W8(L2+0x40u,1u);                                    /* 4469c9 */
        s.r.ecx=R32(L2+0x17u);                              /* 4469ce */
        const uint32_t a[1]={L2};
        mo_call(&s,R32(s.r.ecx+4u),0x48u,0x4469d4u,a,1);    /* 4469d1 call [ecx+4] */
        mo_pop(&s,L2);
        break;
    }
    default:                                                /* 6: 4469e4 */
        break;
    }
    return mo_pair(s.r.eax,s.r.edx);
}

/* Original: DBSIM.EXE 0x4470c0 */
uint64_t es2_mfd_click_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_mfd_frame *frame,
    uint32_t gauge,uint32_t gadget,uint32_t point,const uint32_t entry[3]) {
    (void)gadget;
    const uint32_t G=gauge,P=point;
    mo_state s={m,g,frame,G,0,0,{entry[0],P,entry[2]}};     /* 4470c6 EDX = P, 4470c9 EBX = G */
    if(R32(G+0xc1u)!=1u)return mo_pair(s.r.eax,s.r.edx);    /* 4470cc cmp; jne */
    s.r.eax=R32(G+0xd1u);                                   /* 4470d5 */
    s.esi=0;                                                /* 4470db */
    s.r.ecx=s.r.eax+0x14u;                                  /* 4470dd lea */
    for(;;) {
        s.r.eax=R32(s.r.ecx)+4u;                            /* 4470e0, 4470e2: the rectangle */
        const uint32_t A=s.r.eax;
        int hit=0;
        s.edi=R32(P+4u);                                    /* 4470e5 */
        if((int32_t)s.edi>=(int32_t)R32(A+4u)) {            /* 4470e8 jl */
            s.edi=R32(P+4u);                                /* 4470ed */
            if((int32_t)s.edi<=(int32_t)R32(A+0xcu)) {      /* 4470f0 jg */
                s.edi=R32(P);                               /* 4470f5 */
                if((int32_t)s.edi>=(int32_t)R32(A)) {       /* 4470f7 jl */
                    s.edi=R32(P);                           /* 4470fb */
                    hit=(int32_t)s.edi<=(int32_t)R32(A+8u); /* 4470fd jg */
                }
            }
        }
        if(hit)break;
        s.esi+=1u;s.r.ecx+=4u;                              /* 44714a, 44714b */
        if((int32_t)s.esi>=6)return mo_pair(s.r.eax,s.r.edx);   /* 44714e cmp 6; jl */
    }
    const uint32_t i=s.esi;
    if(R8(VA(0x49dbe0u))&&i==R32(G+0xb1u)) {                /* 447102, 44710b */
        {
            const uint32_t a[3]={G,0xau,1u};
            mo_call(&s,0x438d9cu,0,0x44711du,a,3);          /* 447118 button_press(G, 0Ah, 1) */
        }
        {
            const uint32_t a[1]={G};
            mo_call(&s,0x447248u,0,0x447126u,a,1);          /* 447121 the order */
            mo_pop(&s,G);
        }
        s.r.eax=R32(G+0xc1u);                               /* 447127 */
        W8(G+s.r.eax+0xe5u,1u);                             /* 44712d */
    } else {
        const uint32_t a[2]={G,i};
        mo_call(&s,0x4470a4u,0,0x44713eu,a,2);              /* 447139 entry select */
        W8(G+0xe6u,1u);                                     /* 44713e */
    }
    return mo_pair(s.r.eax,s.r.edx);
}

/* Original: DBSIM.EXE 0x431530. Locals [ebp-4] prev, [ebp-8] its copy,
 * [ebp-0Ch] the table write pointer, [ebp-10h] the read pointer. */
uint64_t es2_order_setup_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t cockpit,
    const uint32_t entry[3]) {
    mo_state s={m,g,NULL,0,0,cockpit,{entry[0],entry[1],entry[2]}};   /* 43153d EDI = H */
    const uint32_t H=cockpit,table=VA(0x4d03c8u),groupvar=VA(0x49b0f8u);
    s.r.eax=0;                                              /* 431536 */
    uint32_t n=0,prev=0,write=table;                        /* 43153b, 431540, 431543 */
    s.esi=n;
    for(;;) {
        /* 4315a2: the next actor backwards. */
        s.r.ecx=prev;
        s.r.eax=prev;                                       /* 4315a2..4315a8 [ebp-8] = [ebp-4] */
        s.r.edx=R32(VA(0x4a9b7au));                         /* 4315ac */
        {
            const uint32_t a[2]={s.r.edx,prev};
            mo_call(&s,0x471c70u,0,0x4315b8u,a,2);          /* 4315b3 pool_prev(list, prev) */
        }
        prev=s.r.eax;                                       /* 4315bb */
        s.ebx=s.r.eax;                                      /* 4315be */
        if(!s.r.eax)break;                                  /* 4315c0 test; jne */
        const uint32_t A=s.ebx;
        s.r.eax=A+0x92u;                                    /* 43154c */
        if(R8(A+0xa3u)==0)continue;                         /* 431552 cmp [eax+11h]; je */
        s.r.edx=R32(A+0x45u);                               /* 431558 */
        W32(groupvar,s.r.edx);                              /* 43155b */
        s.ebx=0;                                            /* 431561 */
        int done=0;
        for(;;) {
            s.r.edx=R32(groupvar);                          /* 431563 */
            s.r.eax=s.ebx;                                  /* 431569 */
            s.ebx+=1u;                                      /* 43156b */
            if(fx_s16(s.r.eax)>=fx_s16(R16(s.r.edx+0x10u))) /* 43156c cmp ax,[edx+10h]; jge */
                s.r.eax=0;                                  /* 431583 */
            else {
                s.r.eax=sx16(s.r.eax);                      /* 431572 movsx */
                s.r.ecx=R32(groupvar);                      /* 431575 */
                s.r.edx=R32(s.r.ecx+0xcu);                  /* 43157b */
                s.r.eax=R32(s.r.edx+4u*s.r.eax);            /* 43157e */
            }
            if(!s.r.eax){done=1;break;}                     /* 431585 test; je */
            if(s.r.eax!=R32(H+0x203u)) {                    /* 431589 cmp; je */
                s.r.edx=write;                              /* 431591 */
                W32(s.r.edx,s.r.eax);                       /* 431594 */
                n+=1u;s.esi=n;                              /* 431596 */
                write+=4u;                                  /* 431597 add [ebp-0Ch],4 */
            }
            if((int32_t)n>=3){done=1;break;}                /* 43159b cmp esi,3; jl */
        }
        if(done)break;
    }
    /* 4315c4: the HDD panels per wingman. */
    s.ebx=0;
    uint32_t read=table;                                    /* 4315c6 */
    while((int32_t)n>(int32_t)s.ebx) {                      /* 4315cd / 4315f3 cmp esi,ebx */
        s.r.eax=read;                                       /* 4315d1 */
        s.r.edx=R32(s.r.eax);                               /* 4315d4 */
        s.r.ecx=lo16(s.r.ecx,R16(s.r.edx+0x29cu));          /* 4315d6 mov cx */
        s.r.eax=R32(H+0x1f9u);                              /* 4315df */
        const uint32_t a[3]={s.r.eax,s.ebx,s.r.ecx};
        mo_call(&s,0x44a7e8u,0,0x4315ebu,a,3);              /* 4315e6 HDD panels 44a7e8(HDD, i, word) */
        s.ebx+=1u;                                          /* 4315ee */
        read+=4u;                                           /* 4315ef */
    }
    s.r.edx=R32(H+0x1edu);                                  /* 4315f8 */
    const uint32_t a[2]={s.r.edx,n};
    mo_call(&s,0x4472bcu,0,0x431604u,a,2);                  /* 4315ff MFD list 4472bc(MFD, n) */
    return mo_pair(s.r.eax,s.r.edx);
}
