#include "es2/core/mfd_keys.h"
/* The MFD's key handlers (docs/reverse-engineering/cockpit.md, section "MFD
 * keys"). Every guest read and write is done in the original's order; the
 * registers EAX/EDX/ECX are carried through each function as the original
 * leaves them, so every game call sees the original's entry registers and the
 * functions return the original's EDX:EAX. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define VA(x) (g->image+(uint32_t)(x)-0x400000u)
static uint64_t mk_pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static uint32_t lo16(uint32_t r,uint32_t v) { return (r&0xffff0000u)|(v&0xffffu); }
static uint32_t lo8(uint32_t r,uint32_t v) { return (r&0xffffff00u)|(v&0xffu); }

/* One function's state: the original's EBX/ESI/EDI and EAX/EDX/ECX. */
typedef struct mk_state {
    const es2_guest_memory *m;
    const es2_weapon_graph *g;
    es2_mfd_frame *frame;
    uint32_t ebx, esi, edi;
    es2_weapon_registers r;
} mk_state;
/* One direct game call to `target` returning to `ret` (original VAs). */
static void mk_call(mk_state *s,uint32_t target,uint32_t ret,const uint32_t *a,unsigned n) {
    const es2_weapon_graph *g=s->g;
    if(s->frame) {
        s->frame->call.ebx=s->ebx;s->frame->call.esi=s->esi;s->frame->call.edi=s->edi;
        s->frame->call.ret=VA(ret);
    }
    if(g->observe)g->observe(g->observer_context,target,0);
    g->call(g->context,VA(target),a,n,&s->r);
}
/* POP ECX after a one-slot call: the slot as the callee left it. */
static void mk_pop(mk_state *s,uint32_t value) {
    const es2_guest_memory *m=s->m;
    s->r.ecx=s->frame&&s->frame->top?R32(s->frame->top-4u):value;
}

/* Original: DBSIM.EXE 0x43f9e0 */
uint64_t es2_menu_select_registers(const es2_guest_memory *m,uint32_t menu,uint32_t k,const uint32_t entry[3],
    uint32_t *ecx) {
    const uint32_t old=R16(menu+0x32u);                     /* 43f9e6 mov dx,[eax+32h] */
    W16(menu+0x37u,old);                                    /* 43f9ea */
    *ecx=k;
    W32(menu+0x32u,k);                                      /* 43f9f1 */
    return mk_pair(menu,lo16(entry[1],old));
}

/* Original: DBSIM.EXE 0x447158 */
uint64_t es2_mfd_key_select_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_mfd_frame *frame,
    uint32_t caller_esi,uint32_t caller_edi,uint32_t gauge,uint32_t o,uint32_t n,const uint32_t entry[3]) {
    mk_state s={m,g,frame,gauge,caller_esi,caller_edi,{o,entry[1],entry[2]}};   /* 44715f EAX = o */
    if(o==R32(gauge+0xfbu)&&R32(gauge+0xc1u)==1u) {        /* 447162 cmp; 44716a cmp 1 */
        s.r.edx=n;                                          /* 447173 */
        const uint32_t a[2]={gauge,n};
        mk_call(&s,0x4470a4u,0x44717du,a,2);                /* 447178 mfd_entry(G, n) */
        W8(gauge+0xe6u,1u);                                 /* 447180 */
    }
    return mk_pair(s.r.eax,s.r.edx);
}

/* Original: DBSIM.EXE 0x44703c and 0x447070 (next: step +1, prev: -1). */
static uint64_t mk_entry_step(const es2_guest_memory *m,const es2_weapon_graph *g,es2_mfd_frame *frame,
    uint32_t caller_esi,uint32_t caller_edi,uint32_t gauge,const uint32_t entry[3],int next) {
    const uint32_t menu=R32(gauge+0xd1u);                   /* 447043 / 447077 */
    uint32_t i=R32(gauge+0xb1u);                            /* 447049 / 44707d */
    do {
        if(next) { ++i;if(i==6u)i=0; }                      /* 44704f inc; cmp 6; xor */
        else { --i;if((int32_t)i<0)i=5u; }                  /* 447083 dec; test; jge; mov 5 */
    } while(R8(menu+i+0x2cu)&1u);                           /* 447057 / 44708d test byte,1; jne */
    mk_state s={m,g,frame,i,caller_esi,caller_edi,{gauge,menu,entry[2]}};
    const uint32_t a[2]={gauge,i};
    mk_call(&s,0x4470a4u,next?0x447065u:0x44709bu,a,2);    /* 447060 / 447096 mfd_entry(G, i) */
    return mk_pair(i,s.r.edx);                              /* 447068 / 44709e mov eax,ebx */
}
uint64_t es2_mfd_entry_next_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_mfd_frame *frame,
    uint32_t caller_esi,uint32_t caller_edi,uint32_t gauge,const uint32_t entry[3]) {
    return mk_entry_step(m,g,frame,caller_esi,caller_edi,gauge,entry,1);
}
uint64_t es2_mfd_entry_prev_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_mfd_frame *frame,
    uint32_t caller_esi,uint32_t caller_edi,uint32_t gauge,const uint32_t entry[3]) {
    return mk_entry_step(m,g,frame,caller_esi,caller_edi,gauge,entry,0);
}

/* mfd_key_select(G, [G+FBh], n) with the slot value loaded into `reg`
 * (0 EAX, 1 EDX, 2 ECX, as the case's MOV names it), returning to `ret`. */
static void mk_select(mk_state *s,uint32_t gauge,uint32_t n,unsigned reg,uint32_t ret) {
    const es2_guest_memory *m=s->m;
    const uint32_t o=R32(gauge+0xfbu);
    if(reg==0)s->r.eax=o;else if(reg==1)s->r.edx=o;else s->r.ecx=o;
    const uint32_t a[3]={gauge,o,n};
    mk_call(s,0x447158u,ret,a,3);
}

/* Original: DBSIM.EXE 0x4469e8. The byte table 446b78 for key - 12h: the
 * jump-table index; the index's entry n, register and return address. */
static const uint8_t mk_key_index[29]={1,0,0,0,0,0,6,0,0,0,0,0,5,0,0,4,3,2,0,0,0,0,0,0,0,0,0,0,1};
static const struct { uint8_t n,reg; uint32_t ret; } mk_key_case[7]={
    {0,0,0},
    {4,2,0x446c10u},                                        /* 446c01 */
    {2,0,0x446be8u},                                        /* 446bd9 */
    {1,2,0x446bd4u},                                        /* 446bc5 */
    {5,0,0x446c24u},                                        /* 446c15 */
    {0,1,0x446bc0u},                                        /* 446bb1 */
    {3,1,0x446bfcu},                                        /* 446bed */
};
uint64_t es2_mfd_key_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_mfd_frame *frame,
    uint32_t caller_edi,uint32_t gauge,uint32_t key,const uint32_t entry[3]) {
    mk_state s={m,g,frame,gauge,key,caller_edi,{gauge+0xebu,entry[1],entry[2]}};   /* 4469f3 lea eax */
    const uint32_t a1[1]={gauge+0xebu};
    mk_call(&s,0x431410u,0x4469ffu,a1,1);                   /* 4469fa panel_visible(G+EBh) */
    mk_pop(&s,gauge+0xebu);                                 /* 4469ff pop ecx */
    if(!(s.r.eax&0xffu))return mk_pair(0,s.r.edx);          /* 446a00 test al; 446a04 xor */
    const uint32_t k=key&0xffffu;                           /* 446a0b movzx edx,si */
    s.r.edx=k;
    uint32_t button=0,flag=0,p;
    if(k==0x34u) {                                          /* 446a0e cmp 34h; je 446b28 */
        if(R32(gauge+0xc1u)!=1u)return mk_pair(lo8(s.r.eax,1u),s.r.edx);
        const uint32_t a[1]={gauge};
        mk_call(&s,0x44703cu,0x446b3bu,a,1);                /* 446b36 */
        mk_pop(&s,gauge);                                   /* 446b3b */
        s.r.ecx=R32(gauge+0xc1u);                           /* 446b3c */
        W8(gauge+s.r.ecx+0xe5u,1u);                         /* 446b42 */
        return mk_pair(lo8(s.r.eax,1u),s.r.edx);
    }
    if(k==0x20u||k==0x2du) {                                /* 446a19 sub 20h; 446a1e sub 0Dh */
        s.r.edx=0;p=R32(gauge+0xc1u);                       /* 446a45 / 446ad4 */
        s.r.eax=p;s.r.ecx=p+(p+2u*p)*4u;
        button=k==0x20u?7u:10u;
        flag=R8(VA(k==0x20u?0x49cbdfu:0x49cbe2u)+s.r.ecx);
    } else if(k==0x33u) {                                   /* 446a27 sub 6; je 446b01 */
        s.r.edx=0;
        if(R32(gauge+0xc1u)!=1u)return mk_pair(lo8(s.r.eax,1u),s.r.edx);
        const uint32_t a[1]={gauge};
        mk_call(&s,0x447070u,0x446b14u,a,1);                /* 446b0f */
        mk_pop(&s,gauge);                                   /* 446b14 */
        s.r.edx=R32(gauge+0xc1u);                           /* 446b15 */
        W8(gauge+s.r.edx+0xe5u,1u);                         /* 446b1b */
        return mk_pair(lo8(s.r.eax,1u),s.r.edx);
    } else if(k==0x213u) {                                  /* 446a35 sub 213h; je 446a72 */
        p=R32(gauge+0xc1u);
        s.r.ecx=p;s.r.edx=p+(p+2u*p)*4u;
        button=8;flag=R8(VA(0x49cbe0u)+s.r.edx);
        if(!flag) {
            const uint32_t a[1]={gauge};
            mk_call(&s,0x446ff0u,0x446aa1u,a,1);            /* 446a9c mfd_range(G) */
            mk_pop(&s,gauge);                               /* 446aa1 */
            return mk_pair(lo8(s.r.eax,1u),s.r.edx);
        }
    } else if(k==0x214u) {                                  /* 446a3d dec; je 446aa7 */
        p=R32(gauge+0xc1u);
        s.r.edx=p;s.r.eax=p+(p+2u*p)*4u;
        button=9;flag=R8(VA(0x49cbe1u)+s.r.eax);
    } else {                                                /* 446b4f */
        s.r.edx=k>0x34u?k-0x214u:k-0x33u;
        if(R32(gauge+0xc1u)!=1u)return mk_pair(0,s.r.edx); /* 446b56 jne 446c2d */
        const uint32_t idx=k-0x12u;                         /* 446b5c movzx; add -12h */
        if(idx>0x1cu)return mk_pair(0,s.r.edx);             /* 446b62 cmp 1Ch; ja 446c29 */
        const unsigned c=mk_key_index[idx];                 /* 446b6b mov al,[eax+446b78h] */
        s.r.eax=c;
        if(!c)return mk_pair(0,s.r.edx);                    /* 446c29 */
        mk_select(&s,gauge,mk_key_case[c].n,mk_key_case[c].reg,mk_key_case[c].ret);
        return mk_pair(lo8(s.r.eax,1u),s.r.edx);
    }
    if(flag) {
        const uint32_t a[3]={gauge,button,1u};
        mk_call(&s,0x438d9cu,button==7u?0x446a6au:button==8u?0x446a93u:button==9u?0x446accu:0x446af9u,a,3);
    }
    return mk_pair(lo8(s.r.eax,1u),s.r.edx);                /* 446c31 mov al,1 */
}

/* Original: DBSIM.EXE 0x446c38. The byte table 446c65 for code - 212h:
 * the jump-table index; per index the entry n, the register the slot
 * [G+FBh] is loaded into and the return addresses of menu_select,
 * menu_code (the gated cases), mfd_order and mfd_key_select; `want` is the
 * code menu_code must give (0: not gated). */
static const uint8_t mk_alt_index[29]={7,0,0,0,0,0,6,0,0,0,0,0,5,0,0,4,3,2,0,0,0,0,0,0,0,0,0,0,1};
static const struct { uint8_t n,reg,want; uint32_t select,code,order,key; } mk_alt_case[8]={
    {0,0,0,0,0,0,0},
    {4,2,4,0x446d4eu,0x446d57u,0x446d63u,0x446d73u},        /* 446d46 */
    {2,0,0,0x446cfcu,0,0x446d05u,0x446d15u},                /* 446cf4 */
    {1,2,0,0x446cd3u,0,0x446cdcu,0x446cecu},                /* 446ccb */
    {5,1,0,0x446db2u,0,0x446dbbu,0x446dcbu},                /* 446daa */
    {0,1,0,0x446caau,0,0x446cb3u,0x446cc3u},                /* 446ca2 */
    {3,1,0,0x446d25u,0,0x446d2eu,0x446d3eu},                /* 446d1d */
    {4,0,7,0x446d80u,0x446d89u,0x446d95u,0x446da5u},        /* 446d78 */
};
uint64_t es2_mfd_alt_key_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_mfd_frame *frame,
    uint32_t caller_edi,uint32_t gauge,uint32_t code,const uint32_t entry[3]) {
    const uint32_t idx=(code&0xffffu)-0x212u;               /* 446c3d movzx; 446c44 add */
    const uint32_t menu=R32(gauge+0xd1u);                   /* 446c4c mov esi,[ebx+0D1h] */
    if(idx>0x1cu)return mk_pair(0,entry[1]);                /* 446c49 cmp; 446c52 ja 446dd0 */
    const unsigned c=mk_alt_index[idx];                     /* 446c58 mov al,[eax+446c65h] */
    if(!c)return mk_pair(0,entry[1]);                       /* 446dd0 */
    mk_state s={m,g,frame,gauge,menu,caller_edi,{c,entry[1],entry[2]}};
    const uint32_t n=mk_alt_case[c].n;
    const uint32_t as[2]={menu,n};
    mk_call(&s,0x43f9e0u,mk_alt_case[c].select,as,2);       /* menu_select(menu, n); add esp,8 */
    if(mk_alt_case[c].want) {
        const uint32_t am[1]={menu};
        mk_call(&s,0x43f9c0u,mk_alt_case[c].code,am,1);     /* menu_code(menu) */
        mk_pop(&s,menu);
        if(s.r.eax!=mk_alt_case[c].want)return mk_pair(lo8(s.r.eax,1u),s.r.edx);   /* cmp eax; jne 446dd4 */
    }
    const uint32_t ao[1]={gauge};
    mk_call(&s,0x447248u,mk_alt_case[c].order,ao,1);        /* mfd_order(G) */
    mk_pop(&s,gauge);
    mk_select(&s,gauge,n,mk_alt_case[c].reg,mk_alt_case[c].key);
    return mk_pair(lo8(s.r.eax,1u),s.r.edx);                /* 446dd4 mov al,1 */
}
