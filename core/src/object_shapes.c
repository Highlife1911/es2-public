#include "es2/core/object_shapes.h"
#include "fixed.h"
#include <stddef.h>
/* The object shape set-up (docs/reverse-engineering/mission_load.md, section
 * "object shape set-up"). Every guest read and write is done in the original's
 * order (objects may overlap), and the register struct r follows EAX/EDX/ECX so
 * that each game call sees the original's entry registers; lo() is a 16-bit
 * load that keeps the register's high word. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define GVA(x) (g->image+(uint32_t)(x)-0x400000u)
static uint64_t pair(uint32_t a,uint32_t d) { return (uint64_t)d<<32|a; }
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static void ecx_out(uint32_t *ecx,uint32_t value) { if(ecx)*ecx=value; }
/* One game call: the observer first (reference VA), then the bridge with
 * the entry registers in r. */
static void ss_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,GVA(va),a,n,r);
}
/* count +delta: the pointer [6CBEE4h] read, then [pointer] += delta;
 * returns the pointer. */
static uint32_t ss_count(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t delta) {
    const uint32_t p=R32(GVA(0x6cbee4u));W32(p,R32(p)+delta);return p;
}

/* Original: DBSIM.EXE 0x402278. Frame: the record f-24h (context word
 * f-14h), the allocation f-4, the zero locals f-38h .. f-28h (written,
 * then read back into the pose). EBX = O, ESI = O+38h. */
uint64_t es2_ts_obj_ctor_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t object,
    uint32_t shape,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_OBJECT_SHAPES_FRAME,O=object;
    es2_weapon_registers r={GVA(0x497145u),entry[1],entry[2]};uint32_t a[3];
    ss_call(g,0x49630cu,NULL,0,&r);
    W32(O,GVA(0x4a14a4u));
    r.ecx=0;r.edx=R32(GVA(0x6cbee4u));r.eax=0;W32(r.edx,R32(r.edx)+1u);
    W16(f-0x2cu,0);W16(f-0x2au,0);W16(f-0x28u,0);
    W32(f-0x38u,r.ecx);W32(f-0x34u,r.eax);
    r.eax=O+0x0cu;r.ecx=0;W32(f-0x30u,r.ecx);
    r.edx=r.eax+0x1au;                              /* O+26h (LEA EDX,[EAX+6] is dead) */
    W32(r.edx+8u,r.ecx);
    r.ecx=R32(f-0x38u);W32(r.edx,r.ecx);
    r.ecx=R32(f-0x34u);W32(r.edx+4u,r.ecx);
    r.edx=R32(f-0x2cu);W32(r.eax,r.edx);
    r.edx=lo(r.edx,R16(f-0x28u));W16(r.eax+4u,r.edx);
    W16(r.eax+0x26u,0);
    W32(O,GVA(0x497289u));
    r.eax=ss_count(m,g,1);
    W32(O,GVA(0x4a0b80u));
    if(!shape)r.edx=0;
    else {
        /* 4022ff: new(42h); POP ECX reloads the size. */
        a[0]=0x42u;ss_call(g,0x47749cu,a,1,&r);
        r.ecx=0x42u;W32(f-4u,r.eax);
        if(!r.eax)r.edx=R32(f-4u);
        else {
            W16(f-0x14u,0x14u);
            r.edx=shape;r.ecx=R32(f-4u);
            a[0]=r.ecx;a[1]=r.edx;ss_call(g,0x478838u,a,2,&r);
            r.eax=ss_count(m,g,0xfffffffdu);
            W16(f-0x14u,8);
            r.edx=R32(f-4u);
        }
    }
    W32(O+0x34u,r.edx);
    r.eax=O+0x38u;
    a[0]=r.eax;a[1]=0;a[2]=8;ss_call(g,0x49681au,a,3,&r);
    r.eax=O+0x38u;W8(r.eax+1u,1);
    r.edx=ss_count(m,g,1);
    r.ecx=R32(f-0x24u);                            /* the saved FS:0 head */
    ecx_out(ecx,r.ecx);
    return pair(O,r.edx);
}

/* 40332c / 403368: the base constructor, then ANIM_OBJ, count +1 (ECX
 * cleared first, EDX the pointer), DET_ANIM_OBJ, O+41h = 0, count +1 (EAX
 * the pointer). */
static uint64_t ss_det_anim(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t base,uint32_t O,
    uint32_t shape,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={shape,entry[1],entry[2]};uint32_t a[2]={O,shape};
    ss_call(g,base,a,2,&r);
    W32(O,GVA(0x4973acu));
    r.ecx=0;r.edx=ss_count(m,g,1);
    W32(O,GVA(0x4973ccu));
    W32(O+0x41u,r.ecx);
    (void)ss_count(m,g,1);
    ecx_out(ecx,r.ecx);
    return pair(O,r.edx);
}
/* Original: DBSIM.EXE 0x40332c */
uint64_t es2_det_anim_shape_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t object,
    uint32_t shape,const uint32_t entry[3],uint32_t *ecx) {
    return ss_det_anim(m,g,0x402278u,object,shape,entry,ecx);
}
/* Original: DBSIM.EXE 0x403368 */
uint64_t es2_det_anim_static_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t object,
    uint32_t shape,const uint32_t entry[3],uint32_t *ecx) {
    return ss_det_anim(m,g,0x402188u,object,shape,entry,ecx);
}
/* Original: DBSIM.EXE 0x4033a4. ESI = the record, EBX = O. */
uint64_t es2_det_anim_record_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t object,
    uint32_t record,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t O=object;
    es2_weapon_registers r={R32(R32(record)),entry[1],entry[2]};uint32_t a[2]={O,r.eax};
    ss_call(g,0x402278u,a,2,&r);
    W32(O,GVA(0x4973acu));
    r.edx=ss_count(m,g,1);
    W32(O,GVA(0x4973ccu));
    W32(O+0x41u,record);
    r.ecx=ss_count(m,g,1);
    ecx_out(ecx,r.ecx);
    return pair(O,r.edx);
}

/* Original: DBSIM.EXE 0x478838. EBX = I; ESI the node count word, then the
 * pose array and the scaled index, then the matrix array; EDI the copied
 * dwords. */
uint64_t es2_anim_instance_ctor_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t instance,
    uint32_t shape,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t I=instance;
    es2_weapon_registers r={shape,entry[1],entry[2]};uint32_t a[5];
    a[0]=I;a[1]=shape;ss_call(g,0x490b64u,a,2,&r);
    W32(I,GVA(0x4a14c4u));
    a[0]=GVA(0x6c56a8u);a[1]=0;a[2]=0x20u;ss_call(g,0x49681au,a,3,&r);
    /* 478862: MOV DX,4000h keeps memset's high word. */
    r.edx=lo(r.edx,0x4000u);
    W16(GVA(0x6c56b8u),r.edx);W16(GVA(0x6c56aeu),r.edx);W16(GVA(0x6c56a8u),r.edx);
    r.edx=0;r.eax=I+0x1au;
    do { r.ecx=0;W32(r.eax,r.ecx);++r.edx;r.eax+=4u; } while((int32_t)r.edx<10);
    r.eax=R32(I+4u);r.edx=R32(r.eax+0x2au);
    const uint32_t nodes=R16(r.edx+0x16u);           /* SI */
    W16(I+0x0cu,nodes);
    r.eax=sx(nodes);
    a[0]=r.eax;ss_call(g,0x4774f0u,a,1,&r);
    r.ecx=sx(nodes);                                  /* POP ECX */
    W32(I+0x0eu,r.eax);
    r.edx=sx(R16(I+0x0cu));
    a[0]=0;a[1]=0x0cu;a[2]=r.edx;a[3]=1;a[4]=GVA(0x4922e6u);ss_call(g,0x49667cu,a,5,&r);
    W32(I+0x12u,r.eax);
    r.ecx=sx(R16(I+0x0cu));
    a[0]=0;a[1]=0x20u;a[2]=r.ecx;a[3]=1;a[4]=GVA(0x495132u);ss_call(g,0x49667cu,a,5,&r);
    W32(I+0x16u,r.eax);
    /* 4788e1: per node k the 12-byte pose of entry sx(word [[S+18h]+2k]) of
     * the shape's pose table [S+12h] (S = [[I+4]+2Ah], reread per node). */
    r.eax=0;
    for(;;) {
        r.ecx=sx(R16(I+0x0cu));
        if((int32_t)r.eax>=(int32_t)r.ecx)break;
        r.edx=R32(I+4u);
        uint32_t esi=R32(I+0x12u);
        r.ecx=3u*r.eax;
        r.edx=R32(r.edx+0x2au);
        r.ecx=esi+4u*r.ecx;
        esi=R32(r.edx+0x18u);
        r.edx=R32(r.edx+0x12u);
        esi=sx(R16(esi+2u*r.eax));
        esi=3u*esi;
        W32(r.ecx,R32(r.edx+4u*esi));
        W32(r.ecx+4u,R32(r.edx+4u*esi+4u));
        W32(r.ecx+8u,R32(r.edx+4u*esi+8u));
        r.edx=R32(I+0x0eu);
        W8(r.edx+r.eax,1);
        ++r.eax;
    }
    a[0]=I;ss_call(g,0x478c64u,a,1,&r);
    r.ecx=I;                                          /* POP ECX */
    const uint32_t matrix=R32(I+0x16u);               /* ESI */
    a[0]=matrix;a[1]=0;a[2]=0x20u;ss_call(g,0x49681au,a,3,&r);
    W16(matrix+0x10u,0x4000u);W16(matrix+6u,0x4000u);W16(matrix,0x4000u);
    r.edx=ss_count(m,g,1);
    ecx_out(ecx,r.ecx);
    return pair(I,r.edx);
}

/* Original: DBSIM.EXE 0x402374. Frame: the record f-24h (context word
 * f-14h), the thread f-4. ESI = O, EBX = the thread. */
uint64_t es2_object_track_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t object,
    uint32_t word,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_OBJECT_SHAPES_FRAME,O=object;
    es2_weapon_registers r={GVA(0x497175u),entry[1],entry[2]};uint32_t a[4];
    ss_call(g,0x49630cu,NULL,0,&r);
    /* 402389: new(52h) with 49630c's EAX; POP ECX reloads the size. */
    a[0]=0x52u;ss_call(g,0x47749cu,a,1,&r);
    r.ecx=0x52u;W32(f-4u,r.eax);
    uint32_t T;
    if(r.eax) {
        W16(f-0x14u,0x14u);
        r.edx=R32(O+0x34u);r.ecx=R32(r.edx+4u);r.eax=R32(r.ecx+0x2au);
        r.edx=R32(f-4u);
        a[0]=r.edx;a[1]=r.eax;ss_call(g,0x478e48u,a,2,&r);
        W16(f-0x14u,8);
        T=R32(f-4u);
    } else T=R32(f-4u);   /* a null thread faults in track_clip */
    /* 4023c6: MOV AX,[EBP+0Ch] keeps EAX's high word in the pushed dword. */
    r.eax=lo(r.eax,word);
    a[0]=T;a[1]=r.eax;a[2]=0;a[3]=0;ss_call(g,0x4792acu,a,4,&r);
    a[0]=T;a[1]=0;ss_call(g,0x4796d8u,a,2,&r);
    r.edx=R32(O+0x34u);
    a[0]=r.edx;a[1]=T;ss_call(g,0x478a3cu,a,2,&r);
    r.edx=R32(f-0x24u);                               /* the saved FS:0 head */
    ecx_out(ecx,r.ecx);
    return pair(T,r.edx);
}

/* Original: DBSIM.EXE 0x478a3c. EAX the slot, EDX its index, ECX = I. */
uint64_t es2_thread_add_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t instance,
    uint32_t thread,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={instance+0x1au,0,instance};uint32_t a[1];
    (void)entry;
    do {
        if(!R32(r.eax)) {
            W32(r.eax,thread);
            a[0]=instance;ss_call(g,0x4789d0u,a,1,&r);
            ecx_out(ecx,instance);                    /* POP ECX */
            return pair(1,r.edx);
        }
        ++r.edx;r.eax+=4u;
    } while((int32_t)r.edx<10);
    ecx_out(ecx,instance);
    return pair(0,r.edx);
}

/* Original: DBSIM.EXE 0x478e48. EBX = T, ESI = T+2Eh. */
uint64_t es2_thread_ctor_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t thread,
    uint32_t manager,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t T=thread,state=T+0x2eu;
    es2_weapon_registers r={entry[0],manager,entry[2]};uint32_t a[4];
    W32(T,manager);
    r.eax=state;
    a[0]=T;a[1]=0;a[2]=0;a[3]=0;ss_call(g,0x4792acu,a,4,&r);
    a[0]=state;a[1]=0;a[2]=0x20u;ss_call(g,0x49681au,a,3,&r);
    /* 478e73: MOV CX,4000h keeps memset's high word. */
    r.ecx=lo(r.ecx,0x4000u);
    W16(state+0x10u,r.ecx);W16(state+6u,r.ecx);W16(state,r.ecx);
    ecx_out(ecx,r.ecx);
    return pair(T,r.edx);
}

/* Original: DBSIM.EXE 0x402fc0. EBX = R, SI the word count, EDI the
 * block. */
uint64_t es2_shape_word_block_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t block,
    uint32_t shape,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t R=block;
    es2_weapon_registers r={shape,entry[1],entry[2]};uint32_t a[3];
    W32(R,shape);
    if(shape) {
        const uint32_t words=R16(shape+0x24u);
        if(words) {
            /* 402fdb: 47a288(2 x word, heap); EAX = the heap, EDX = the size. */
            r.edx=2u*words;r.eax=R32(GVA(0x6c5830u));
            a[0]=r.edx;a[1]=r.eax;ss_call(g,0x47a288u,a,2,&r);
            const uint32_t copy=r.eax;               /* EDI */
            W32(R+4u,copy);
            r.eax=R32(R);
            r.edx=2u*R16(r.eax+0x24u);
            r.ecx=R32(r.eax+0x1cu);
            a[0]=copy;a[1]=r.ecx;a[2]=r.edx;ss_call(g,0x49680eu,a,3,&r);
        } else { r.eax=0;W32(R+4u,r.eax); }
    }
    r.edx=ss_count(m,g,1);
    ecx_out(ecx,r.ecx);
    return pair(R,r.edx);
}
