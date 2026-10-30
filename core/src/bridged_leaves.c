#include "es2/core/bridged_leaves.h"
#include "fixed.h"
/* The bridged leaves (docs/reverse-engineering/hud.md, section "the bridged
 * leaves"). Reads and writes follow the original's order (a fault on an
 * unmapped pointer happens at the same access). */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define VA(base,x) ((base)+(uint32_t)(x)-0x400000u)
static uint64_t bl_pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }

/* Original: DBSIM.EXE 0x40e484 */
uint64_t es2_weapon_target_word_registers(const es2_guest_memory *m,uint32_t weapon,uint32_t target,
    const uint32_t entry[3],uint32_t *ecx) {
    *ecx=entry[2];
    if(target&&!R16(target+0x1a8u))W16(weapon+0x7du,0u);   /* 40e48d test edx / 40e491 cmp */
    else W16(weapon+0x7du,0xffffu);                         /* 40e4a3 */
    return bl_pair(weapon,target);
}

/* Original: DBSIM.EXE 0x411e40 (k = index + 1) and 0x411e60 (k = index +
 * count - 1), then CDQ/IDIV by the count. */
static uint64_t bl_list_step(const es2_guest_memory *m,uint32_t list,uint32_t index,uint32_t count,uint32_t *ecx) {
    const int32_t rest=(int32_t)index%(int32_t)count;      /* IDIV EBX: a zero count faults */
    *ecx=list;
    const uint32_t k=(uint32_t)rest;
    return bl_pair(R32(R32(list)+4u*k),k);                  /* mov eax,[ecx]; mov eax,[eax+4*edx] */
}
uint64_t es2_list_next_registers(const es2_guest_memory *m,uint32_t list,uint32_t object,const uint32_t entry[3],uint32_t *ecx) {
    (void)entry;
    const uint32_t index=(uint32_t)fx_s16(R16(object+0x4bu))+1u;  /* 411e4a movsx, 411e54 inc */
    const uint32_t count=(uint32_t)fx_s16(R16(list+6u));           /* 411e4e */
    return bl_list_step(m,list,index,count,ecx);
}
uint64_t es2_list_prev_registers(const es2_guest_memory *m,uint32_t list,uint32_t object,const uint32_t entry[3],uint32_t *ecx) {
    (void)entry;
    const uint32_t index=(uint32_t)fx_s16(R16(object+0x4bu));      /* 411e6a */
    const uint32_t count=(uint32_t)fx_s16(R16(list+6u));           /* 411e6e */
    return bl_list_step(m,list,index+count-1u,count,ecx);          /* 411e72 add, 411e74 dec */
}

/* 0x41b468's state: EBX/ESI/EDI at its calls and EAX/EDX/ECX. */
typedef struct rk_state {
    const es2_guest_memory *m;
    const es2_weapon_graph *g;
    es2_r_key_frame *frame;
    uint32_t ebx, esi, edi;
    es2_weapon_registers r;
} rk_state;
/* One game call at `ret` (original VA of the return address). */
static void rk_call(rk_state *s,uint32_t target,unsigned slot,uint32_t ret,const uint32_t *a,unsigned n) {
    const es2_weapon_graph *g=s->g;
    if(s->frame) {
        s->frame->call.ebx=s->ebx;s->frame->call.esi=s->esi;s->frame->call.edi=s->edi;
        s->frame->call.ret=VA(g->image,ret);
    }
    /* A direct call names the original VA (observer key) and the bridge
     * gets its address in this image; the virtual passes the resolved
     * target under the slot key. */
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,slot?target:VA(g->image,target),a,n,&s->r);
}
/* Original: DBSIM.EXE 0x41b468 */
uint64_t es2_r_key_toggle_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_r_key_frame *frame,
    uint32_t f,uint32_t caller_edi,uint32_t player,const uint32_t entry[3],uint32_t *ecx) {
    rk_state s={m,g,frame,0,0,caller_edi,{entry[0],entry[1],entry[2]}};
    s.r.eax=player;                                         /* 41b470 */
    const uint32_t F=player+0x92u;
    s.r.edx=F;s.r.ecx=F;                                    /* 41b473 lea, 41b479 */
    if(R8(F+0x11u))W8(F+4u,R8(F+4u)^1u);                    /* 41b47b cmp, 41b483 xor */
    const uint32_t H=R32(VA(g->image,0x49b088u));            /* 41b487 */
    s.esi=F;
    const uint32_t E=R32(H+0x20bu);s.ebx=E;                 /* 41b48f */
    s.r.ecx=0;W32(f-0x16u,0u);                              /* 41b495 xor, 41b497 */
    uint32_t a[3];
    const unsigned on=R8(F+4u)!=0;                          /* 41b49a */
    a[0]=on?0x1au:0x1bu;
    rk_call(&s,0x462830u,0,on?0x41b4a7u:0x41b4b7u,a,1);     /* 41b4a2 / 41b4b2 sound row */
    s.r.ecx=s.frame&&s.frame->top?R32(s.frame->top-4u):a[0];   /* pop ecx */
    W16(f-0x18u,on?0x2cu:0x2du);                            /* 41b4a8 / 41b4b8 */
    a[0]=E;a[1]=0;a[2]=0x2cu;
    rk_call(&s,0x435ac8u,0,0x41b4c8u,a,3);                  /* 41b4c3 cancel 2Ch */
    a[0]=E;a[1]=0;a[2]=0x2du;
    rk_call(&s,0x435ac8u,0,0x41b4d5u,a,3);                  /* 41b4d0 cancel 2Dh */
    s.r.eax=f-0x18u;                                        /* 41b4d8 lea */
    s.r.edx=R32(E);                                         /* 41b4dd */
    const uint32_t target=R32(s.r.edx);
    a[0]=E;a[1]=f-0x18u;
    rk_call(&s,target,0x20bu,0x41b4e1u,a,2);                /* 41b4df call [edx]: slot 0 of [H+20Bh] */
    *ecx=s.r.ecx;
    return bl_pair(s.r.eax,s.r.edx);
}

/* Original: DBSIM.EXE 0x434974 */
uint64_t es2_nav_marker_set_registers(const es2_guest_memory *m,uint32_t image,const uint32_t entry[3],uint32_t *ecx) {
    (void)entry;
    const uint32_t H=R32(VA(image,0x49b088u));              /* 434975 */
    const uint32_t S=R32(H+0x203u)+0x26u;                   /* 43497b, 434981 */
    const uint32_t D=H+0x25eu;                              /* 434984 */
    W32(D+8u,R32(S+8u));                                    /* 43498a, 43498d */
    W32(D,R32(S));                                          /* 434990, 434992 */
    *ecx=R32(S+4u);W32(D+4u,*ecx);                          /* 434994, 434997 */
    W8(H+0x26au,1u);                                        /* 43499a active */
    W8(H+0x26bu,0u);                                        /* 4349a1 not armed */
    return bl_pair(D,H);
}

/* Original: DBSIM.EXE 0x447e08 */
uint64_t es2_throttle_handler_registers(const es2_guest_memory *m,uint32_t G,uint32_t gadget,uint32_t value,
    const uint32_t entry[3],uint32_t *ecx) {
    (void)entry;
    *ecx=G;
    uint32_t k=0,p=G+0x18u;                                 /* 447e16, 447e18 */
    while((int32_t)k<(int32_t)R32(G+0x68u)) {               /* 447e43 cmp eax,[ecx+68h]; jl */
        if(gadget==R32(p)) {                                /* 447e1d */
            if(k)return bl_pair(k-1u,p);                    /* 447e21 sub eax,1; jae */
            const uint32_t v=R8(G+0xc1u)?value:0u-value;    /* 447e26 */
            W32(G+0xb5u,v);                                 /* 447e37 */
            return bl_pair(v,p);
        }
        ++k;p+=4u;                                          /* 447e3f, 447e40 */
    }
    return bl_pair(k,p);
}

/* Original: DBSIM.EXE 0x45fe38 */
uint64_t es2_view_pref_toggle_registers(const es2_guest_memory *m,uint32_t image,const uint32_t entry[3],uint32_t *ecx) {
    (void)entry;
    const uint32_t eax=R16(VA(image,0x4d2518u))==0;          /* 45fe38 cmp, sete al, and eax,1 */
    W16(VA(image,0x4d2518u),eax);                           /* 45fe46 */
    const uint32_t edx=R32(VA(image,0x4d24eau));            /* 45fe4c */
    *ecx=R32(edx+0x32u);                                    /* 45fe52 */
    if(*ecx==R32(VA(image,0x4d24e6u)))W16(VA(image,0x4d2520u),2u);   /* 45fe55, 45fe5d */
    return bl_pair(eax,edx);
}

/* Original: DBSIM.EXE 0x49514f */
uint64_t es2_rotation_base_ctor_registers(uint32_t object,const uint32_t entry[3],uint32_t *ecx) {
    *ecx=entry[2];
    return bl_pair(object,entry[1]);
}
static void bl_ctor_call(const es2_weapon_graph *g,uint32_t va,uint32_t arg,es2_weapon_registers *r) {
    const uint32_t a[1]={arg};
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,VA(g->image,va),a,1,r);
}
/* Original: DBSIM.EXE 0x495194 */
uint64_t es2_rotation_ctor_registers(const es2_weapon_graph *g,uint32_t object,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    bl_ctor_call(g,0x49514fu,object,&r);                    /* 49519c */
    *ecx=object;                                            /* 4951a1 pop ecx */
    return bl_pair(object,r.edx);                           /* 4951a2 mov eax,ebx */
}
/* Original: DBSIM.EXE 0x495132 */
uint64_t es2_matrix_ctor_registers(const es2_weapon_graph *g,uint32_t object,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    bl_ctor_call(g,0x495194u,object,&r);                    /* 49513a */
    r.ecx=object;r.eax=object+0x14u;                        /* 49513f pop ecx, 495140 lea */
    bl_ctor_call(g,0x4953c4u,object+0x14u,&r);              /* 495144 */
    *ecx=object+0x14u;                                      /* 495149 pop ecx */
    return bl_pair(object,r.edx);                           /* 49514a mov eax,ebx */
}
