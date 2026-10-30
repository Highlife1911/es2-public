#include "es2/core/type_loads.h"
#include "es2/core/divide.h"
#include "fixed.h"
#include <stddef.h>
/* The MECH and FLYER type loads of the world load's pass 1
 * (docs/reverse-engineering/mission_load.md, section "MECH and FLYER type
 * loads"). Every guest read and write is done in the original's order (objects
 * may overlap), locals that a callee can reach live in the scratch frame at the
 * original's offsets and are reread where the original rereads them, and the
 * register struct r follows EAX/EDX/ECX so that each game call sees the
 * original's entry registers; lo() replaces a register's low word, lo8() its
 * low byte. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define GVA(x) (g->image+(uint32_t)(x)-0x400000u)
static uint64_t tl_pair(uint32_t a,uint32_t d) { return (uint64_t)d<<32|a; }
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
static int32_t s16(uint32_t x) { return fx_s16(x); }
static uint32_t sx8(uint32_t b) { b&=0xffu; return b<0x80u?b:b|0xffffff00u; }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static uint32_t lo8(uint32_t reg,uint32_t byte) { return (reg&0xffffff00u)|(uint8_t)byte; }
static uint64_t tl_done(const es2_weapon_registers *r,uint32_t *ecx) { if(ecx)*ecx=r->ecx;return tl_pair(r->eax,r->edx); }
/* One direct game call: the observer first (reference VA), then the bridge. */
static void tl_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,GVA(va),a,n,r);
}
/* A virtual call through the vtable the caller loaded; observed by its slot
 * (slot +0 keyed 100h, since key 0 means a direct call). */
static void tl_virtual(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t vtable,uint32_t slot,const uint32_t *a,
    unsigned n,es2_weapon_registers *r) {
    const uint32_t target=R32(vtable+slot);
    if(g->observe)g->observe(g->observer_context,target,slot?slot:0x100u);
    g->call(g->context,target,a,n,r);
}
/* 0x47e068: EDX:EAX = (a << 16) sign-extended, IDIV b: EAX the quotient,
 * EDX the remainder; ECX unchanged. A zero divisor or a quotient outside 32
 * bits faults in the original (0xc0000094, 0xc0000095); both are raised here
 * on the host as well (flight models, own_process.md "Inputs that reach a site"). */
static void tl_q16_div(es2_weapon_registers *r,uint32_t a,uint32_t b) {
    r->eax=es2_divide_or_raise((uint64_t)(fx_s32(a)*65536),b,1,&r->edx);
}
/* 0x47e08d: EDX:EAX = a * b (IMUL), SHRD EAX,EDX,16: EAX = bits 16..47, EDX
 * the high dword; ECX unchanged. */
static void tl_q16_mul(es2_weapon_registers *r,uint32_t a,uint32_t b) {
    const uint64_t p=(uint64_t)(fx_s32(a)*fx_s32(b));
    r->eax=(uint32_t)(p>>16);r->edx=(uint32_t)(p>>32);
}
/* 0x41ff00 / 0x422e80 (t): MOVSX EAX,WORD [EBP+8]; MOVZX EAX,BYTE
 * [EAX+table]: the type's index byte, EDX and ECX unchanged. */
static uint32_t tl_index(const es2_guest_memory *m,uint32_t table,uint32_t type) { return R8(table+sx(type)); }

/* Original: DBSIM.EXE 0x40feb4, 0x40cbd4, 0x40d200: MOV AX,[word]; MOV
 * DX,[EBP+8]; ADD [word],DX. */
static uint64_t tl_slot_base(const es2_guest_memory *m,uint32_t word,uint32_t n,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t eax=lo(entry[0],R16(word)),edx=lo(entry[1],n);
    W16(word,R16(word)+(uint16_t)edx);
    if(ecx)*ecx=entry[2];
    return tl_pair(eax,edx);
}
uint64_t es2_gun_slot_base_registers(const es2_guest_memory *m,uint32_t image,uint32_t n,const uint32_t entry[3],uint32_t *ecx) {
    return tl_slot_base(m,image+0x4a98e8u-0x400000u,n,entry,ecx);
}
uint64_t es2_col_slot_base_registers(const es2_guest_memory *m,uint32_t image,uint32_t n,const uint32_t entry[3],uint32_t *ecx) {
    return tl_slot_base(m,image+0x4987dcu-0x400000u,n,entry,ecx);
}
uint64_t es2_dmg_slot_base_registers(const es2_guest_memory *m,uint32_t image,uint32_t n,const uint32_t entry[3],uint32_t *ecx) {
    return tl_slot_base(m,image+0x498856u-0x400000u,n,entry,ecx);
}

/* Original: DBSIM.EXE 0x41ff68. EAX = P throughout. */
uint64_t es2_remap_word_registers(const es2_guest_memory *m,uint32_t word,uint32_t table,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t w=R16(word);
    uint32_t edx=lo(entry[1],w),c=entry[2];
    if(s16(w)>-1) {                                    /* CMP DX,-1; JLE */
        c=sx(w);edx=table;
        c=sx8(R8(table+c));                            /* MOVSX ECX,BYTE [EDX+ECX] */
        W16(word,c);
    }
    if(ecx)*ecx=c;
    return tl_pair(word,edx);
}

/* Original: DBSIM.EXE 0x40fc50. EBX = L, EAX the entry cursor, EDX = i
 * (compared as DX), ECX the output cursor, ESI the value. */
uint64_t es2_gun_classes_registers(const es2_guest_memory *m,uint32_t list,uint32_t out,const uint32_t entry[3],uint32_t *ecx) {
    (void)entry;
    uint32_t cursor=R32(list+2u),i=0,c=out;
    while(s16(i)<s16(R16(list))) {                     /* CMP DX,[EBX]; JL */
        uint32_t value=sx8(R8(cursor+6u));
        if((int32_t)value<4)value=lo(value,R16(cursor)); /* MOV SI,[EAX] */
        else value=0xffffffffu;                        /* OR ESI,-1 */
        W16(c,value);
        c+=2u;++i;cursor+=0x1au;
    }
    if(ecx)*ecx=c;
    return tl_pair(lo(cursor,R16(list)),i);
}

/* Original: DBSIM.EXE 0x40304c. ESI = L, then the slot; EDI = S; EBX the
 * class cursor; [EBP-2] = n, [EBP-4] the counter word, [EBP-8] the slot
 * cursor. */
uint64_t es2_node_list_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t list,uint32_t shape,
    uint32_t n,uint32_t classes,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={lo(entry[0],n),entry[1],entry[2]};uint32_t a[2];
    const uint16_t count=(uint16_t)n;
    r.edx=lo(r.edx,count);
    W16(list+4u,count);
    r.ecx=sx(count)<<2;
    a[0]=r.ecx;tl_call(g,0x4774f0u,a,1,&r);
    r.ecx=a[0];                                        /* POP ECX */
    W32(list,r.eax);
    r.eax=list;r.edx=R32(list);
    uint32_t cursor=r.edx;
    r.ecx=lo(r.ecx,R16(list+4u));
    uint16_t counter=(uint16_t)r.ecx;
    for(;;) {
        r.eax=lo(r.eax,counter);                       /* MOV AX,[EBP-4] */
        const uint16_t before=counter;counter=(uint16_t)(counter-1u);
        if(!before)break;                              /* TEST AX,AX: ESI = 0 */
        const uint32_t slot=cursor;cursor+=4u;
        if(!slot)break;                                /* TEST ESI,ESI */
        if(s16(R16(classes))<0)r.eax=0;                /* CMP WORD [EBX],0; JGE */
        else {
            r.edx=lo(r.edx,R16(classes));
            r.ecx=R32(shape);
            a[0]=shape;a[1]=r.edx;tl_virtual(m,g,r.ecx,0x24u,a,2,&r);
        }
        W32(slot,r.eax);
        classes+=2u;
    }
    if(ecx)*ecx=(uint32_t)count<<16|counter;           /* POP ECX twice: [EBP-4] */
    return tl_pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x41ff88. EDI = N, ESI = table, EBX the word or child
 * cursor, [EBP-6] the word index, EDI the child index. */
uint64_t es2_node_remap_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t node,uint32_t table,
    uint32_t limit,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={R32(node),entry[1],entry[2]};uint32_t a[3];
    a[0]=node;tl_virtual(m,g,r.eax,0,a,1,&r);
    r.ecx=node;                                        /* POP ECX */
    const uint32_t c=r.eax;
    int words=0,remap=0,children=0;
    if((int32_t)c>0x140014) {                          /* JG */
        r.eax=c-0x140015u;
        if(!r.eax)words=1;                             /* TSBSPPart */
        else {
            r.eax-=0x9ffeeu;
            if(!r.eax)children=1;                      /* ANShape 1E0003h */
            else r.eax-=2u;                            /* 1E0005h and the others end */
        }
    } else if(c==0x140014u) {                          /* TSGroup */
        a[0]=node+4u;a[1]=table;tl_call(g,0x41ff68u,a,2,&r);
        return tl_done(&r,ecx);
    } else {
        const uint32_t d=c+0xffebfffbu;                /* class - 140005h */
        if(d<2u)r.eax=d-2u;                            /* 140005h, 140006h end */
        else if(d-2u<2u){r.eax=d-4u;children=1;}       /* 140007h, 140008h */
        else if(d==6u){r.eax=0;remap=children=1;}      /* TSCellAnimPart 14000Bh */
        else {r.eax=d-7u;children=r.eax==0;}           /* TSDetailPart 14000Ch; the others end */
    }
    if(words) {
        uint32_t cursor=R32(node+0x1cu);uint16_t i=0;
        for(;;) {
            r.edx=node;r.eax=sx(i);r.ecx=R16(node+0x16u);  /* MOVZX: the count unsigned */
            if(!((int32_t)r.eax<(int32_t)r.ecx))break;
            r.eax=lo(r.eax,R16(cursor));
            const uint32_t w=r.eax;
            if(s16(w)>0&&s16(w)<s16(limit)) {          /* TEST AX,AX; JLE / CMP AX,[EBP+10h]; JGE */
                r.edx=sx(w);
                r.ecx=sx8(R8(table+r.edx))+1u;         /* MOVSX ECX,BYTE; INC ECX; JLE */
                if((int32_t)r.ecx>0){a[0]=cursor;a[1]=table;tl_call(g,0x41ff68u,a,2,&r);}
            }
            i=(uint16_t)(i+1u);cursor+=2u;
        }
        children=1;
    }
    if(remap){r.eax=node+4u;a[0]=node+4u;a[1]=table;tl_call(g,0x41ff68u,a,2,&r);}
    if(children) {
        const uint32_t kids=R32(node+0x12u);
        for(uint32_t i=0;;++i) {
            r.eax=node;r.ecx=sx(i);r.edx=R16(node+0x10u);  /* MOVSX ECX,DI; MOVZX EDX */
            if(!((int32_t)r.ecx<(int32_t)r.edx))break;
            r.eax=lo(r.eax,limit);r.edx=R32(kids+4u*i);
            a[0]=r.edx;a[1]=table;a[2]=r.eax;tl_call(g,0x41ff88u,a,3,&r);
        }
    }
    return tl_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x420090. EDI = S; [EBP-4] the part cursor, [EBP-8]
 * i then k, [EBP-0Ch] the node, [EBP-0Eh] node0's word, rows at EBP-1A0h;
 * ESI the shape cursor and BX = k in the first loop, EBX the shape cursor in
 * the second. */
uint64_t es2_lod_remap_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t shapes,uint32_t parts,
    const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[3];
    const uint32_t f=g->scratch+ES2_LOD_REMAP_FRAME;
    if(!(s16(R16(shapes+0xcu))>1))return tl_done(&r,ecx); /* CMP WORD [EDI+0Ch],1; JLE */
    r.eax=parts;W32(f-4u,r.eax);
    r.edx=f-0x1a0u;a[0]=r.edx;a[1]=0xffffffffu;a[2]=0x190u;tl_call(g,0x49681au,a,3,&r);
    r.ecx=0;W32(f-8u,r.ecx);
    for(;;) {
        r.eax=R32(f-4u);r.eax=lo8(r.eax,R8(r.eax));     /* MOV AL,[EAX] */
        if((r.eax&0x80u))break;                        /* TEST AL,AL; JL */
        r.edx=sx8(r.eax);a[1]=r.edx;
        r.ecx=R32(shapes);r.eax=R32(r.ecx);a[0]=r.eax;
        r.edx=R32(r.eax);tl_virtual(m,g,r.edx,0x20u,a,2,&r);
        W32(f-0xcu,r.eax);
        r.ecx=R32(f-0xcu);r.eax=lo(r.eax,R16(r.ecx+4u));W16(f-0xeu,r.eax);
        uint32_t cursor=R32(shapes)+4u;
        for(uint32_t k=1;s16(k)<s16(R16(shapes+0xcu));++k,cursor+=4u) {   /* CMP BX,[EDI+0Ch] */
            r.eax=R32(f-4u);r.edx=sx8(R8(r.eax));a[1]=r.edx;
            r.ecx=R32(cursor);a[0]=r.ecx;r.eax=R32(r.ecx);
            tl_virtual(m,g,r.eax,0x20u,a,2,&r);
            W32(f-0xcu,r.eax);
            if(!r.eax)continue;
            r.edx=sx(k);r.eax=r.edx;r.edx=R32(f-0xcu);r.eax+=r.eax;
            r.ecx=sx(R16(r.edx+4u));r.eax*=5u;r.edx=f-0x1d2u;r.eax*=5u;r.eax+=r.ecx;
            r.ecx=lo8(r.ecx,R8(f-0xeu));r.eax+=r.edx;
            if(r.eax-(f-0x1a0u)<0x1a0u)W8(r.eax,r.ecx);  /* MOV [EAX],CL (inside the frame only) */
        }
        W32(f-8u,R32(f-8u)+1u);W32(f-4u,R32(f-4u)+1u);
        if(!((int32_t)R32(f-8u)<0x32))break;
    }
    uint32_t cursor=R32(shapes)+4u;
    W32(f-8u,1);
    for(;;) {
        r.edx=sx(R16(shapes+0xcu));
        if(!((int32_t)r.edx>(int32_t)R32(f-8u)))break; /* JG */
        r.eax=R32(shapes);r.edx=R32(r.eax);r.ecx=R32(r.edx+0x2au);r.eax=lo(r.eax,R16(r.ecx+0x10u));a[2]=r.eax;
        r.edx=R32(f-8u)*50u;r.ecx=f-0x1d2u;r.edx+=r.ecx;a[1]=r.edx;
        r.eax=R32(cursor);a[0]=r.eax;
        tl_call(g,0x41ff88u,a,3,&r);
        W32(f-8u,R32(f-8u)+1u);cursor+=4u;
    }
    return tl_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x41ff10. EBX = t; 0x41ff00 computed. */
uint64_t es2_mech_type_select_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t type,
    const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[1];
    const uint32_t table=GVA(0x4a9d08u),count=GVA(0x4a9d06u);
    r.eax=tl_index(m,table,type);r.ecx=type;           /* POP ECX */
    if((uint16_t)r.eax==0xffu) {
        r.eax=sx(type);
        r.edx=lo8(r.edx,R8(count));                    /* MOV DL,[4A9D06h] */
        W8(table+r.eax,r.edx);
        W16(count,R16(count)+1u);
        a[0]=1;tl_call(g,0x40feb4u,a,1,&r);r.ecx=a[0];
        if(R16(count)==1u)W16(GVA(0x4a9d70u),r.eax);
    }
    a[0]=1;tl_call(g,0x40cbd4u,a,1,&r);r.ecx=a[0];
    a[0]=1;tl_call(g,0x40d200u,a,1,&r);r.ecx=a[0];
    return tl_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x422e90. EBX = t; 0x422e80 computed. */
uint64_t es2_flyer_type_select_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t type,
    const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[1];
    const uint32_t table=GVA(0x4a9dccu),count=GVA(0x4a9dcau);
    r.eax=tl_index(m,table,type);r.ecx=type;
    if((uint16_t)r.eax==0xffu) {
        r.eax=sx(type);
        r.edx=lo8(r.edx,R8(count));
        W8(table+r.eax,r.edx);
        W16(count,R16(count)+1u);
        a[0]=1;tl_call(g,0x40d200u,a,1,&r);r.ecx=a[0];
        a[0]=1;tl_call(g,0x40cbd4u,a,1,&r);r.ecx=a[0];
    }
    return tl_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x40fee8. Frame: the record f-50h (context word
 * f-40h), the stream object f-30h; ESI = name, EBX = the type word over the
 * caller's EBX, then the mount entry E. */
uint64_t es2_gun_list_load_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t name,uint32_t type,
    uint32_t ebx,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_GUN_LIST_LOAD_FRAME,S=f-0x30u;
    es2_weapon_registers r={GVA(0x498fb1u),entry[1],entry[2]};uint32_t a[6];
    tl_call(g,0x49630cu,NULL,0,&r);
    W16(f-0x40u,8);
    r.edx=S;a[0]=S;a[1]=name;tl_call(g,0x472f14u,a,2,&r);
    ebx=lo(ebx,type);                                  /* MOV BX,[EBP+0Ch] */
    a[0]=ebx;tl_call(g,0x40fea0u,a,1,&r);
    r.ecx=a[0];                                        /* POP ECX */
    const uint32_t E=r.eax;
    r.eax=R32(f-0x2cu);                                /* the stream status */
    if(r.eax) {
        r.edx=0;                                       /* SETE DL; AND EDX,1 */
        a[0]=GVA(0x6c56c8u);a[1]=5;a[2]=r.edx;a[3]=GVA(0x49921eu);a[4]=0x146u;a[5]=name;
        tl_call(g,0x479c78u,a,6,&r);
    }
    r.ecx=S;a[0]=S;a[1]=2;a[2]=E;tl_call(g,0x4735b8u,a,3,&r);
    r.eax=sx(R16(E));
    a[0]=0;a[1]=0x1au;a[2]=r.eax;a[3]=1;a[4]=GVA(0x4117a5u);tl_call(g,0x49667cu,a,5,&r);
    const uint32_t block=r.eax;
    W32(E+2u,block);
    r.eax=sx(R16(E));r.edx=r.eax*26u;
    r.ecx=S;a[0]=S;a[1]=r.edx;a[2]=block;tl_call(g,0x4735b8u,a,3,&r);
    /* 40ff8b: E is pushed above the two arguments and popped into EAX. */
    r.eax=E;r.edx=S;a[0]=S;a[1]=2;tl_call(g,0x472f64u,a,2,&r);
    r.eax=E;
    r.edx=R32(f-0x50u);                                /* the saved FS:0 head */
    return tl_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x4201a8. Frame: the record f-24h (context word
 * f-14h), the block f-4, the name f-28h, the index word f-2Ah, the word
 * f-2Ch, q f-30h, sx(R+4) f-34h, n f-36h, the class words f-60h. EBX = t,
 * then the shape list S; EDI = rec; ESI = the path, R or k. */
uint64_t es2_mech_type_load_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t type,
    const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_MECH_TYPE_LOAD_FRAME;
    es2_weapon_registers r={GVA(0x49a3b0u),entry[1],entry[2]};uint32_t a[5];
    tl_call(g,0x49630cu,NULL,0,&r);
    r.edx=sx(type);r.ecx=R32(GVA(0x4a9d60u));r.eax=R32(r.ecx+4u*r.edx);
    W32(f-0x28u,r.eax);
    r.eax=tl_index(m,GVA(0x4a9d08u),type);r.ecx=type; /* 0x41ff00; POP ECX */
    W16(f-0x2au,r.eax);
    r.edx=sx(R16(f-0x2au));
    uint32_t rec=r.edx*0x21eu;
    r.eax=R32(GVA(0x4a09f0u));a[1]=r.eax;
    r.edx=R32(f-0x28u);a[0]=r.edx;
    rec+=R32(GVA(0x4a9d64u));
    tl_call(g,0x492becu,a,2,&r);
    uint32_t path=r.eax;
    r.eax=rec+2u;a[0]=r.eax;a[1]=0xd8u;a[2]=1;a[3]=path;tl_call(g,0x492f54u,a,4,&r);
    r.edx=lo(r.edx,R16(f-0x2au));
    W16(rec,r.edx);
    W16(rec+0x6cu,type);                               /* MOV [EDI+6Ch],BX */
    const uint32_t S=rec+0xdeu;
    r.eax=R32(GVA(0x4a09f8u));a[1]=r.eax;
    r.edx=R32(f-0x28u);a[0]=r.edx;
    tl_call(g,0x492becu,a,2,&r);
    path=r.eax;
    W16(f-0x2cu,0);
    r.eax=lo(r.eax,R16(f-0x2cu));
    a[0]=S;a[1]=path;a[2]=0x1e0003u;a[3]=r.eax;tl_call(g,0x474cd8u,a,4,&r);
    W16(S+0xcu,r.eax);
    W32(S+4u,GVA(0x49a344u));
    r.edx=sx(R16(S+0xcu));r.ecx=r.edx*6u+4u;
    a[0]=r.ecx;tl_call(g,0x4774f0u,a,1,&r);r.ecx=a[0];
    W32(f-4u,r.eax);
    if(R32(f-4u)) {
        W16(f-0x14u,0x14u);
        r.eax=sx(R16(S+0xcu));r.edx=R32(f-4u);
        a[0]=r.edx;a[1]=6;a[2]=r.eax;a[3]=0x211u;a[4]=GVA(0x4207dbu);tl_call(g,0x49667cu,a,5,&r);
        W16(f-0x14u,8);
    } else r.eax=R32(f-4u);
    W32(S+8u,r.eax);
    r.edx=rec+0x30u;a[0]=S;a[1]=r.edx;tl_call(g,0x420090u,a,2,&r);
    const uint32_t R=rec+2u;
    r.eax=lo(r.eax,R16(R+4u));
    W16(R+0xc6u,r.eax);
    if(!R16(R+0x4eu)) {
        r.edx=sx(R16(rec+0xc6u));r.eax=r.edx;
        r.edx=sx(R16(R+0xc2u));a[1]=r.edx;
        r.eax*=400u;a[0]=r.eax;tl_q16_div(&r,a[0],a[1]);
        W32(f-0x30u,r.eax);
        r.ecx=sx(R16(R+4u));W32(f-0x34u,r.ecx);
        r.eax=R32(f-0x34u);a[0]=0x13bu;a[1]=r.eax;tl_call(g,0x47e0b0u,a,2,&r);
        W16(R+0xc0u,r.eax);
        r.edx=R32(f-0x30u);r.ecx=sx(R16(R+2u));tl_q16_mul(&r,r.ecx,r.edx);W16(R+2u,r.eax);
        r.eax=R32(f-0x30u);r.edx=sx(R16(R+4u));tl_q16_mul(&r,r.edx,r.eax);W16(R+4u,r.eax);
        r.ecx=R32(f-0x30u);r.eax=sx(R16(R+0x2cu));tl_q16_mul(&r,r.eax,r.ecx);W16(R+0x2cu,r.eax);
        r.edx=R32(f-0x30u);r.ecx=sx(R16(R+0x6cu));tl_q16_mul(&r,r.ecx,r.edx);W16(R+0x6cu,r.eax);
    }
    r.eax=R32(GVA(0x4a0a00u));a[1]=r.eax;
    r.edx=R32(f-0x28u);a[0]=r.edx;
    tl_call(g,0x492becu,a,2,&r);
    path=r.eax;
    r.eax=lo(r.eax,R16(f-0x2au));
    r.eax=lo(r.eax,(uint16_t)r.eax+R16(GVA(0x4a9d70u)));   /* ADD AX,[4A9D70h] */
    a[0]=path;a[1]=r.eax;tl_call(g,0x40fee8u,a,2,&r);
    r.edx=f-0x60u;a[0]=r.eax;a[1]=r.edx;tl_call(g,0x40fc50u,a,2,&r);
    W16(f-0x36u,r.eax);
    for(uint32_t k=0;s16(k)<s16(R16(S+0xcu));++k) {   /* CMP SI,[EBX+0Ch] */
        r.eax=f-0x60u;r.ecx=sx(k);a[3]=r.eax;
        r.edx=lo(r.edx,R16(f-0x36u));a[2]=r.edx;
        r.eax=R32(S);r.edx=R32(r.eax+4u*r.ecx);
        r.ecx=sx(k);r.eax=r.ecx;a[1]=r.edx;
        r.eax=r.eax*6u+R32(S+8u);a[0]=r.eax;
        tl_call(g,0x40304cu,a,4,&r);
        r.edx=sx(k);r.eax=sx(R16(rec+0x96u));r.ecx=R32(S);
        r.eax=(r.eax<<3)+GVA(0x4a9d72u);
        r.edx=R32(r.ecx+4u*r.edx);
        W32(r.edx+0x26u,r.eax);
    }
    r.eax=lo(r.eax,R16(f-0x2au));a[1]=r.eax;
    r.ecx=R32(f-0x28u);a[0]=r.ecx;
    tl_call(g,0x4379ccu,a,2,&r);
    W32(rec+0xdau,r.eax);
    if(R16(rec+0x50u)) {
        r.eax=R32(f-0x28u);a[0]=r.eax;a[1]=GVA(0x49a47eu);tl_call(g,0x492becu,a,2,&r);
        path=r.eax;
        const uint32_t fm=rec+0x1dcu;
        a[0]=fm;a[1]=0x36u;a[2]=1;a[3]=path;tl_call(g,0x492f54u,a,4,&r);
        r.eax=R32(fm+0x2au);r.eax-=R32(fm+0x2eu);a[1]=r.eax;
        r.edx=R32(fm+0x22u);r.edx-=R32(fm+0x26u);a[0]=r.edx;
        tl_q16_div(&r,a[0],a[1]);
        W32(fm+0xeu,r.eax);
    }
    r.ecx=rec+0xceu;W32(f-0x28u,r.ecx);r.eax=R32(f-0x28u);
    if(R8(r.eax)) {
        r.edx=(sx(R16(rec+0x96u))<<3)+GVA(0x4a9d72u);a[2]=r.edx;
        r.ecx=R32(f-0x28u);a[1]=r.ecx;
        a[0]=rec+0x212u;tl_call(g,0x40874cu,a,3,&r);
    }
    r.eax=R32(f-0x24u);                                /* the saved FS:0 head */
    return tl_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x42049c. Frame: the record f-24h (context word
 * f-14h), the block f-4; EBX = t in the loop; 0x41ff00 computed. */
uint64_t es2_mech_types_registers(const es2_guest_memory *m,const es2_weapon_graph *g,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_MECH_TYPES_FRAME,count=GVA(0x4a9d06u);
    es2_weapon_registers r={GVA(0x49a3e0u),entry[1],entry[2]};uint32_t a[5];
    tl_call(g,0x49630cu,NULL,0,&r);
    a[0]=GVA(0x49a481u);a[1]=0x55u;a[2]=8;tl_call(g,0x492d00u,a,3,&r);
    W32(GVA(0x4a9d60u),r.eax);
    r.edx=sx(R16(count));r.ecx=r.edx*0x21eu+4u;
    a[0]=r.ecx;tl_call(g,0x4774f0u,a,1,&r);r.ecx=a[0];
    W32(f-4u,r.eax);
    if(R32(f-4u)) {
        W16(f-0x14u,0x14u);
        r.eax=sx(R16(count));r.edx=R32(f-4u);
        a[0]=r.edx;a[1]=0x21eu;a[2]=r.eax;a[3]=0x211u;a[4]=GVA(0x420817u);tl_call(g,0x49667cu,a,5,&r);
        W16(f-0x14u,8);
    } else r.eax=R32(f-4u);
    W32(GVA(0x4a9d64u),r.eax);
    r.edx=lo(r.edx,R16(count));
    a[0]=r.edx;tl_call(g,0x4378d8u,a,1,&r);r.ecx=a[0];
    for(uint32_t t=0;s16(t)<0x55;++t) {                /* CMP BX,55h; JL */
        r.eax=tl_index(m,GVA(0x4a9d08u),t);r.ecx=t;
        if((uint16_t)r.eax!=0xffu){a[0]=t;tl_call(g,0x4201a8u,a,1,&r);r.ecx=a[0];}
    }
    r.eax=R32(f-0x24u);                                /* the saved FS:0 head */
    return tl_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x422ed0. ESI = the name, EBX = rec, then the "fm"
 * record; EDI = the shape; 0x422e80 computed. */
uint64_t es2_flyer_type_load_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t type,
    const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[4];
    r.ecx=R32(GVA(0x4a9df0u));r.eax=type;r.edx=sx(type);
    const uint32_t name=R32(r.ecx+4u*r.edx);
    r.eax=sx(tl_index(m,GVA(0x4a9dccu),type));         /* 0x422e80; MOVSX EAX,AX */
    uint32_t rec=r.eax;
    r.ecx=type;                                        /* POP ECX */
    rec=((rec<<3)-r.eax)<<4;
    r.eax=R32(GVA(0x4a09f0u));a[1]=r.eax;
    rec+=R32(GVA(0x4a9df4u));
    a[0]=name;tl_call(g,0x492becu,a,2,&r);
    a[0]=rec;a[1]=0x2eu;a[2]=1;a[3]=r.eax;tl_call(g,0x492f54u,a,4,&r);
    r.edx=R32(GVA(0x4a09f8u));a[0]=name;a[1]=r.edx;tl_call(g,0x492becu,a,2,&r);
    a[0]=r.eax;tl_call(g,0x474c38u,a,1,&r);r.ecx=a[0];
    const uint32_t shape=r.eax;
    W32(rec+0x36u,shape);
    W32(shape+0x26u,GVA(0x4a9d8au));
    r.eax=R32(GVA(0x4a0a04u));a[0]=name;a[1]=r.eax;tl_call(g,0x492becu,a,2,&r);
    a[0]=r.eax;tl_call(g,0x40d160u,a,1,&r);r.ecx=a[0];
    W32(rec+0x2eu,r.eax);
    r.edx=R32(GVA(0x4a0a08u));a[0]=name;a[1]=r.edx;tl_call(g,0x492becu,a,2,&r);
    a[0]=r.eax;tl_call(g,0x40cd88u,a,1,&r);r.ecx=a[0];
    W32(rec+0x32u,r.eax);
    a[0]=name;a[1]=GVA(0x49a6a0u);tl_call(g,0x492becu,a,2,&r);
    const uint32_t fm=rec+0x3au;
    a[0]=fm;a[1]=0x36u;a[2]=1;a[3]=r.eax;tl_call(g,0x492f54u,a,4,&r);
    r.eax=R32(fm+0x2au);r.eax-=R32(fm+0x2eu);a[1]=r.eax;
    r.edx=R32(fm+0x22u);r.edx-=R32(fm+0x26u);a[0]=r.edx;
    tl_q16_div(&r,a[0],a[1]);
    W32(fm+0xeu,r.eax);
    return tl_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x422fb4. EBX = t; ESI = 0x422e80's EAX (unused);
 * 0x422e80 computed. */
uint64_t es2_flyer_types_registers(const es2_guest_memory *m,const es2_weapon_graph *g,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[5];
    const uint32_t count=GVA(0x4a9dcau);
    if(!R16(count))return tl_done(&r,ecx);           /* CMP WORD [4A9DCAh],0; JE */
    a[0]=GVA(0x49a6a3u);a[1]=0x22u;a[2]=4;tl_call(g,0x492d00u,a,3,&r);
    W32(GVA(0x4a9df0u),r.eax);
    r.eax=sx(R16(count));
    a[0]=0;a[1]=0x70u;a[2]=r.eax;a[3]=1;a[4]=GVA(0x423098u);tl_call(g,0x49667cu,a,5,&r);
    W32(GVA(0x4a9df4u),r.eax);
    for(uint32_t t=0;s16(t)<0x22;++t) {                /* CMP BX,22h; JL */
        r.eax=tl_index(m,GVA(0x4a9dccu),t);r.ecx=t;
        if((uint16_t)r.eax!=0xffu){a[0]=t;tl_call(g,0x422ed0u,a,1,&r);r.ecx=a[0];}
    }
    return tl_done(&r,ecx);
}
