#include "es2/core/logic_frame.h"
#include "fixed.h"
/* The logic frame 0x45f570 (docs/reverse-engineering/main_loop.md, section "the
 * logic frame as own code"). r holds what the original's EAX/EDX/ECX hold at
 * the next game call, ebx/esi/edi its callee-saved registers; every global and
 * frame local is read again where the original reads it (the callees change the
 * pools, the freeze word, the player and the groups), and every local is
 * written at the original's EBP offset. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define VA(x) (s->g->image+(uint32_t)(x)-0x400000u)
static uint64_t lf_pair(uint32_t eax,uint32_t edx){return (uint64_t)edx<<32|eax;}
static uint32_t lf_lo16(uint32_t x,uint32_t w){return (x&0xffff0000u)|(w&0xffffu);}
static uint32_t lf_sx16(uint32_t x){return (uint32_t)fx_s16(x);}

typedef struct lf_state {
    const es2_guest_memory *m;
    const es2_weapon_graph *g;
    es2_logic_frame *frame;
    es2_weapon_registers r;
    uint32_t ebx,esi,edi;
} lf_state;

/* The callee-saved registers and return address of the CALL at `ret`
 * (original VA), for the bridge. */
static void lf_at(lf_state *s,uint32_t ret){
    s->frame->call.ebx=s->ebx;s->frame->call.esi=s->esi;s->frame->call.edi=s->edi;s->frame->call.ret=VA(ret);
}
static void lf_call(lf_state *s,uint32_t va,const uint32_t *a,unsigned n,uint32_t ret){
    lf_at(s,ret);
    if(s->g->observe)s->g->observe(s->g->observer_context,va,0);
    s->g->call(s->g->context,VA(va),a,n,&s->r);
}
/* A one-slot virtual call of the resolved target. */
static void lf_vcall(lf_state *s,uint32_t target,unsigned slot,uint32_t object,uint32_t ret){
    const uint32_t a[1]={object};
    lf_at(s,ret);
    if(s->g->observe)s->g->observe(s->g->observer_context,target,slot);
    s->g->call(s->g->context,target,a,1,&s->r);
}
/* POP ECX after a one-slot call: the slot as the callee left it at the
 * original coordinate, else the pushed argument. */
static void lf_pop(lf_state *s,uint32_t argument){
    const es2_guest_memory *m=s->m;
    s->r.ecx=s->frame->top?R32(s->frame->top-4u):argument;
}
static void lf_one(lf_state *s,uint32_t va,uint32_t argument,uint32_t ret){
    const uint32_t a[1]={argument};
    lf_call(s,va,a,1,ret);
    lf_pop(s,argument);
}

/* The seven pool loops of steps 5: update (0 = the projectiles' virtual
 * +14h), pool and removal-list globals, cursor offset below EBP, return
 * addresses of the update, the append and 0x471c70. */
typedef struct lf_pool { uint32_t update,pool,list,cursor,ret_update,ret_append,ret_prev; } lf_pool;
static const lf_pool lf_pools[7]={
    {0x40813cu,0x4a961eu,0x4a9622u,0x3cu,0x45f5c7u,0x45f5dcu,0x45f5efu},   /* explosions */
    {0x4091ccu,0x4a966eu,0x4a9672u,0x40u,0x45f608u,0x45f61du,0x45f630u},   /* smoke balls */
    {0x4092dcu,0x4a9684u,0x4a9688u,0x44u,0x45f649u,0x45f65eu,0x45f671u},   /* smoke emitters */
    {0x46b678u,0x6b4f30u,0x6b4f34u,0x48u,0x45f68au,0x45f69fu,0x45f6b2u},   /* fires */
    {0x408bd8u,0x4a9642u,0x4a9646u,0x4cu,0x45f6cbu,0x45f6e0u,0x45f6f3u},   /* debris */
    {0x409d2cu,0x4a96aau,0x4a96aeu,0x50u,0x45f70cu,0x45f721u,0x45f734u},   /* meteors */
    {0,        0x4a96c2u,0x4a96c6u,0x54u,0x45f756u,0x45f76bu,0x45f77eu},   /* projectiles, +14h */
};
static void lf_pool_loop(lf_state *s,const lf_pool *p,uint32_t f){
    const es2_guest_memory *m=s->m;es2_weapon_registers *r=&s->r;
    const uint32_t c=f-p->cursor;
    for(;;){
        s->edi=R32(c);                                      /* mov edi,[ebp-c] */
        r->ecx=R32(VA(p->pool));                            /* mov ecx,[pool] */
        {const uint32_t a[2]={r->ecx,s->edi};lf_call(s,0x471c70u,a,2,p->ret_prev);}
        W32(c,r->eax);s->ebx=r->eax;                        /* mov [ebp-c],eax; mov ebx,eax */
        if(!r->eax)return;                                  /* test eax,eax; jne */
        if(p->update)lf_one(s,p->update,s->ebx,p->ret_update);   /* EAX = EBX = the object */
        else {
            r->ecx=R32(s->ebx);                             /* mov ecx,[ebx] */
            lf_vcall(s,R32(r->ecx+0x14u),0x14u,s->ebx,p->ret_update);
            lf_pop(s,s->ebx);
        }
        if(!(uint16_t)r->eax)continue;                      /* test ax,ax; je */
        r->eax=s->ebx;                                      /* mov eax,ebx */
        r->edx=R32(VA(p->list));                            /* mov edx,[list] */
        {const uint32_t a[2]={r->edx,r->eax};lf_call(s,0x478628u,a,2,p->ret_append);}
    }
}

/* Original: DBSIM.EXE 0x45f570 */
uint64_t es2_logic_frame_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_logic_frame *frame,
    uint32_t caller_ebx,uint32_t caller_esi,uint32_t caller_edi,const uint32_t entry[3],uint32_t *ecx){
    lf_state st={m,g,frame,{entry[0],entry[1],entry[2]},caller_ebx,caller_esi,caller_edi},*s=&st;
    es2_weapon_registers *r=&st.r;
    const uint32_t f=g->scratch+ES2_LOGIC_FRAME_SIZE;      /* the original's EBP */
    /* 45f576: the runtime record at EBP-30h, then the control record. */
    r->eax=VA(0x49f558u);
    lf_call(s,0x49630cu,NULL,0,0x45f583u);
    lf_call(s,0x45a810u,NULL,0,0x45f588u);
    W32(f-0x34u,r->eax);
    /* 45f58b: tape playback copies the record's elapsed word. */
    if(R16(VA(0x4d24d6u))){
        r->edx=R32(f-0x34u);
        r->ecx=lf_lo16(r->ecx,R16(r->edx+0x2eu));
        W16(f-0x36u,r->ecx);
        r->eax=lf_lo16(r->eax,R16(f-0x36u));
        W16(VA(0x4d3b64u),r->eax);
    }
    s->esi=0;                                               /* 45f5aa xor esi,esi */
    if(!R16(VA(0x4d24f2u))){                                /* 45f5ac freeze */
        r->eax=0;W32(f-0x3cu,0);                            /* 45f5ba */
        lf_pool_loop(s,&lf_pools[0],f);
        for(unsigned i=1;i<7;++i){
            if(i==6)W16(VA(0x4d2526u),0);                   /* 45f740 */
            r->edx=0;W32(f-lf_pools[i].cursor,0);           /* xor edx,edx; mov [ebp-c],edx */
            lf_pool_loop(s,&lf_pools[i],f);
        }
        /* 45f78a: the actor pool's virtual +18h, groups not pending. */
        r->edx=0;W32(f-0x58u,0);
        for(;;){
            s->edi=R32(f-0x58u);
            r->ecx=R32(VA(0x4a95a0u));
            {const uint32_t a[2]={r->ecx,s->edi};lf_call(s,0x471c70u,a,2,0x45f7bau);}
            W32(f-0x58u,r->eax);s->ebx=r->eax;
            if(!r->eax)break;
            r->ecx=R32(s->ebx+0x45u);                       /* 45f791 the group */
            r->eax=R32(r->ecx+0x14u)?1u:0u;                 /* setne al; and eax,1 */
            if((uint16_t)r->eax)continue;
            r->edx=R32(s->ebx);                             /* mov edx,[ebx] */
            lf_vcall(s,R32(r->edx+0x18u),0x18u,s->ebx,0x45f7a9u);
            lf_pop(s,s->ebx);
        }
        /* 45f7c6: the groups by index. */
        r->edx=lf_lo16(r->edx,R16(VA(0x4a9e04u)));
        W16(f-0x5cu,r->edx);
        r->ecx=R32(VA(0x4a9e00u));
        W32(f-0x60u,r->ecx);
        for(;;){
            r->eax=lf_lo16(r->eax,R16(f-0x5cu));            /* 45f7fb mov ax,[ebp-5Ch] */
            W16(f-0x5cu,R16(f-0x5cu)+0xffffu);              /* add word [ebp-5Ch],-1 */
            if((uint16_t)r->eax){r->eax=R32(f-0x60u);W32(f-0x60u,R32(f-0x60u)+0x7au);}
            else r->eax=0;
            if(!r->eax)break;                               /* 45f814 test eax,eax */
            r->edx=R32(r->eax+0x14u)?1u:0u;                 /* setne dl; and edx,1 */
            if((uint16_t)r->edx)lf_one(s,0x4236c4u,r->eax,0x45f7f1u);
            else lf_one(s,0x423a74u,r->eax,0x45f7fau);
        }
    }
    /* 45f818: a single-step request freezes again. */
    if(R16(VA(0x4d24fcu))){W16(VA(0x4d24fcu),0);W16(VA(0x4d24f2u),1u);}
    r->edx=R32(f-0x34u);
    lf_one(s,0x460870u,r->edx,0x45f83du);                   /* input and commands */
    if((uint16_t)r->eax){                                   /* 45f841 */
        r->eax=lf_lo16(r->eax,1u);
        r->edx=R32(f-0x30u);
        *ecx=r->ecx;
        return lf_pair(r->eax,r->edx);
    }
    if(!R16(VA(0x4d24f2u))){                                /* 45f856 */
        lf_call(s,0x426b48u,NULL,0,0x45f865u);
        r->ecx=R32(VA(0x4d24e6u));
        lf_one(s,0x426b70u,r->ecx,0x45f871u);
    }
    lf_call(s,0x45de30u,NULL,0,0x45f877u);                  /* view request */
    if(!R16(VA(0x4d24f2u))){                                /* 45f877 */
        lf_call(s,0x4123acu,NULL,0,0x45f886u);
        r->eax=0;W32(f-0x64u,0);
        for(;;){                                            /* 45f8b2 the MECH pool */
            s->edi=R32(f-0x64u);
            r->edx=R32(VA(0x4a9b7au));
            {const uint32_t a[2]={r->edx,s->edi};lf_call(s,0x471c70u,a,2,0x45f8c2u);}
            W32(f-0x64u,r->eax);s->ebx=r->eax;
            if(!r->eax)break;
            r->edx=R32(s->ebx+0x45u);
            r->ecx=R32(r->edx+0x14u)?1u:0u;                 /* setne cl; and ecx,1 */
            if((uint16_t)r->ecx)continue;
            r->eax=s->ebx+0x92u;                            /* lea eax,[ebx+92h] */
            if(R8(r->eax+7u))continue;
            lf_one(s,0x41aa5cu,s->ebx,0x45f8b1u);
        }
    }
    /* 45f8ce: the player's +99h selects death sequence or outcome. */
    r->edx=R32(VA(0x4d24e6u))+0x92u;
    W32(f-0x68u,r->edx);
    r->ecx=R32(f-0x68u);
    if(R8(r->ecx+7u)){
        if(R16(VA(0x4d24f2u)))goto out;                     /* 45f8f2 */
        lf_call(s,0x45fa84u,NULL,0,0x45f8fdu);
        if(!r->eax)goto out;                                /* 45f8ff test eax,eax */
        lf_one(s,0x47749cu,0x57eu,0x45f90fu);               /* the alert, status 2 */
        W32(f-4u,r->eax);
        if(r->eax){
            W16(f-0x20u,0x14u);
            r->edx=R32(f-4u);
            {const uint32_t a[4]={r->edx,2u,1u,0u};lf_call(s,0x455950u,a,4,0x45f92cu);}
            r->ecx=R32(VA(0x6cbee4u));W32(r->ecx,R32(r->ecx)-0xau);
            W16(f-0x20u,8u);
        }
        r->eax=R32(f-4u);
        W32(f-0x6cu,r->eax);
        r->edx=R32(f-0x6cu);
        r->ecx=R32(r->edx);
        {const uint32_t o=r->edx;lf_vcall(s,R32(r->ecx+8u),8u,o,0x45f952u);lf_pop(s,o);}
        r->eax=R32(f-0x6cu);
        W32(f-8u,r->eax);
        if(R32(f-8u)){
            r->edx=R32(VA(0x6cbee4u));W32(r->edx,R32(r->edx)+9u);
            W16(f-0x20u,0x2cu);
            r->ecx=R32(f-8u);
            {const uint32_t a[2]={r->ecx,3u};lf_call(s,0x4543b0u,a,2,0x45f979u);}
            W16(f-0x20u,0x20u);
        }
        s->esi=1u;
        goto out;
    }
    /* 45f98c: the periodic outcome. */
    r->eax=R32(VA(0x4d24e6u));
    lf_one(s,0x4131acu,r->eax,0x45f997u);
    s->edi=r->eax;s->ebx=lf_sx16(s->edi);                   /* mov edi,eax; movsx ebx,di */
    if(fx_s32(s->ebx)<=-1||s->ebx==2u)goto out;
    if(s->ebx==3u){
        r->eax=R32(VA(0x4d24e6u));
        r->edx=R32(r->eax+0x45u);
        lf_one(s,0x423f08u,r->edx,0x45f9c2u);
        r->ecx=R32(VA(0x4d24e6u))+0x92u;
        W32(f-0x70u,r->ecx);
        r->eax=R32(f-0x70u);
        if(R8(r->eax+7u))s->ebx=0x12u;
    }
    lf_one(s,0x47749cu,0x57eu,0x45f9eau);                   /* the alert with the status */
    W32(f-0xcu,r->eax);
    if(r->eax){
        W16(f-0x20u,0x44u);
        r->edx=R32(f-0xcu);
        {const uint32_t a[4]={r->edx,s->ebx,1u,0u};lf_call(s,0x455950u,a,4,0x45fa06u);}
        r->ecx=R32(VA(0x6cbee4u));W32(r->ecx,R32(r->ecx)-0xau);
        W16(f-0x20u,0x38u);
    }
    r->eax=R32(f-0xcu);
    W32(f-0x74u,r->eax);
    r->edx=R32(f-0x74u);
    r->ecx=R32(r->edx);
    {const uint32_t o=r->edx;lf_vcall(s,R32(r->ecx+8u),8u,o,0x45fa2cu);
        r->edx=lf_sx16(s->edi);                             /* movsx edx,di */
        lf_pop(s,o);}
    r->ecx=lf_sx16(R16(VA(0x49f5c0u)+2u*r->edx));            /* the expected button */
    r->eax=r->eax==r->ecx?1u:0u;                            /* sete al; and eax,1 */
    s->esi=r->eax;
    r->eax=R32(f-0x74u);
    W32(f-0x10u,r->eax);
    if(R32(f-0x10u)){
        r->edx=R32(VA(0x6cbee4u));W32(r->edx,R32(r->edx)+9u);
        W16(f-0x20u,0x5cu);
        r->ecx=R32(f-0x10u);
        {const uint32_t a[2]={r->ecx,3u};lf_call(s,0x4543b0u,a,2,0x45fa68u);}
        W16(f-0x20u,0x50u);
    }
out:                                                        /* 45fa71 */
    r->eax=s->esi;
    r->edx=R32(f-0x30u);
    *ecx=r->ecx;
    return lf_pair(r->eax,r->edx);
}
