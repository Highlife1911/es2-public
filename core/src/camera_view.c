#include "es2/core/camera_view.h"
#include "fixed.h"
#include <stddef.h>
/* The outside camera and the view subject (docs/reverse-engineering/ camera.md,
 * section "The hooks (group camera_view)"). Every guest read and
 * write is done in the original's order (aliasing camera objects observe it),
 * and the register struct r follows EAX/EDX/ECX instruction by instruction so
 * that each game call sees the original's entry registers. lo() is a 16-bit
 * load that keeps the register's high word. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define GVA(x) (image+(uint32_t)(x)-0x400000u)
typedef es2_weapon_registers cmv_regs;
static uint64_t pair(uint32_t a,uint32_t d) { return (uint64_t)d<<32|a; }
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static uint64_t cmv_done(const cmv_regs *r,uint32_t *ecx) { if(ecx)*ecx=r->ecx;return pair(r->eax,r->edx); }
/* One direct game call: the observer first (reference VA, slot 0), then the
 * bridge with the entry registers in r. */
static void cmv_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,cmv_regs *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,g->image+va-0x400000u,a,n,r);
}

/* Original: DBSIM.EXE 0x401078. EAX = C; mode word C+36h compared with 2,
 * then C+32h with O; EDX = 0 or 1, EAX = EDX. */
uint64_t es2_camera_attached_registers(const es2_guest_memory *m,uint32_t camera,uint32_t object,
    const uint32_t entry[3],uint32_t *ecx) {
    uint32_t edx=0;
    if(R16(camera+0x36u)==2u && R32(camera+0x32u)==object)edx=1;
    if(ecx)*ecx=entry[2];
    return pair(edx,edx);
}

/* Original: DBSIM.EXE 0x40109c. EAX = C throughout; the zero words come from
 * two 6-byte locals (f-8 and f-10h, cleared before each copy), so EDX ends
 * as 0 and ECX as the address f-10h. */
uint64_t es2_camera_orbit_set_registers(const es2_guest_memory *m,uint32_t camera,uint32_t subject,
    uint32_t offset,const uint32_t entry[3],uint32_t *ecx) {
    if(R32(camera+0x4au)) { if(ecx)*ecx=entry[2];return pair(camera,entry[1]); }
    uint32_t c=R32(offset);
    W32(camera+0x38u,c);
    c=lo(c,R16(offset+4u));
    W16(camera+0x3cu,c);
    W32(camera+0x32u,subject);
    W32(camera+0x26u,0);W16(camera+0x2au,0);
    W16(camera+0x22u,0);
    W32(camera+0x2cu,0);W16(camera+0x30u,0);
    W16(camera+0x24u,0);
    W16(camera+0x36u,1);
    if(ecx)*ecx=0;                              /* lea ecx,[ebp-10h]: a frame address */
    return pair(camera,0);
}

/* Original: DBSIM.EXE 0x40111c. EAX = C, EDX = a then S, ECX the copied
 * dword with the word a+4 in its low half. */
uint64_t es2_camera_attach_set_registers(const es2_guest_memory *m,uint32_t camera,uint32_t subject,
    uint32_t offset,const uint32_t entry[3],uint32_t *ecx) {
    if(R32(camera+0x4au)) { if(ecx)*ecx=entry[2];return pair(camera,entry[1]); }
    uint32_t c=R32(offset);
    W32(camera+0x3eu,c);
    c=lo(c,R16(offset+4u));
    W16(camera+0x42u,c);
    W32(camera+0x32u,subject);
    W16(camera+0x36u,2);
    if(ecx)*ecx=c;
    return pair(camera,subject);
}

/* Original: DBSIM.EXE 0x401c20 / 0x401c4c. multiply_q8 47e0a0(scale, sx w)
 * with the entry registers; then approach_word 467adc(p, product, step) with
 * EAX = p and multiply_q8's EDX/ECX. */
static uint64_t cmv_step(const es2_weapon_graph *g,uint32_t target,uint32_t value,uint32_t scale,uint32_t step,
    const uint32_t entry[3],uint32_t *ecx) {
    cmv_regs r={entry[0],entry[1],entry[2]};uint32_t a[3];
    a[0]=scale;a[1]=sx(value);cmv_call(g,0x47e0a0u,a,2,&r);
    const uint32_t product=r.eax;
    r.eax=target;a[0]=target;a[1]=product;a[2]=step;cmv_call(g,0x467adcu,a,3,&r);
    return cmv_done(&r,ecx);
}
uint64_t es2_camera_turn_step_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t target,
    uint32_t value,const uint32_t entry[3],uint32_t *ecx) {
    (void)m;return cmv_step(g,target,value,0x800u,0xc0u,entry,ecx);
}
uint64_t es2_camera_speed_step_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t target,
    uint32_t value,const uint32_t entry[3],uint32_t *ecx) {
    (void)m;return cmv_step(g,target,value,0x400u,0x50u,entry,ecx);
}

/* Original: DBSIM.EXE 0x401c74. EBX = C; EAX = sx(mode) - 1 selects the case
 * (SUB/JB, JE, DEC/SUB/JB); EDI and ESI are cleared, then their low words
 * take the step argument; in mode 0 the first argument word is negated in AX
 * over EAX = FFFFFFFFh, in mode 1 loaded over EAX = 0. */
uint64_t es2_outside_camera_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t camera,
    uint32_t a,uint32_t b,uint32_t c,const uint32_t entry[3],uint32_t *ecx) {
    cmv_regs r={entry[0],entry[1],entry[2]};uint32_t v[2];
    if(R32(camera+0x4au))return cmv_done(&r,ecx);
    r.eax=sx(R16(camera+0x36u))-1u;
    if(r.eax==0xffffffffu || r.eax==0u) {
        const int free_flight=r.eax!=0u;
        uint32_t edi=0,esi=0;
        r.eax=lo(r.eax,free_flight?0u-a:a);
        r.edx=camera+0x30u;v[0]=r.edx;v[1]=r.eax;cmv_call(g,0x401c20u,v,2,&r);
        if((uint16_t)c)edi=lo(edi,free_flight?0u-b:b);
        else esi=lo(esi,b);
        r.eax=camera+0x24u;v[0]=r.eax;v[1]=edi;cmv_call(g,0x401c4cu,v,2,&r);
        v[0]=camera+0x2cu;v[1]=esi;cmv_call(g,0x401c20u,v,2,&r);
        return cmv_done(&r,ecx);
    }
    r.eax-=1u;r.eax-=2u;                        /* modes 2, 3 and every other value: nothing */
    return cmv_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x45de30. EBX = B (saved and restored). */
uint64_t es2_view_apply_registers(const es2_guest_memory *m,const es2_weapon_graph *g,const uint32_t entry[3],
    uint32_t *ecx) {
    const uint32_t image=g->image,B=GVA(0x4d24bcu);
    cmv_regs r={entry[0],entry[1],entry[2]};uint32_t a[1];
    r.eax=R32(GVA(0x4cf99cu));
    if(R8(r.eax+0x1cu)) {
        r.eax=lo(r.eax,R16(B+0x5eu));
        if((uint16_t)r.eax!=R16(B+0x32u)) {
            r.edx=r.eax;
            W16(B+0x32u,r.edx);
            r.ecx=sx(r.edx)-1u;
            if(r.ecx==0u) {
                /* 1: free flight, the outside word from its default. */
                r.eax=R32(B+0x2eu);
                if(!R32(r.eax+0x4au))W16(r.eax+0x36u,0);
                r.edx=lo(r.edx,R16(B+0x5cu));
                W16(B+0x34u,r.edx);
            } else if(--r.ecx==0u) {
                /* 2: attached to the subject [B+60h] (flag B+78h) or the player. */
                r.ecx=R16(B+0x78u)?R32(B+0x60u):R32(B+0x2au);
                a[0]=r.ecx;cmv_call(g,0x45df34u,a,1,&r);
                r.ecx=a[0];
                r.eax=R32(B+0x2eu);
                if(!R32(r.eax+0x4au))W16(r.eax+0x36u,2);
                W16(B+0x34u,0);
            } else if(--r.ecx==0u) {
                /* 3: orbit around the subject [B+60h]. */
                r.edx=R32(B+0x60u);
                a[0]=r.edx;cmv_call(g,0x45df34u,a,1,&r);
                r.ecx=a[0];
                r.eax=R32(B+0x2eu);
                if(!R32(r.eax+0x4au))W16(r.eax+0x36u,1);
                W16(B+0x64u,2);
            } else if(--r.ecx==0u) {
                /* 4: replay behind the player. */
                r.edx=R32(B+0x2au);
                a[0]=r.edx;cmv_call(g,0x45df34u,a,1,&r);
                r.ecx=a[0];
                r.eax=R32(B+0x2eu);
                if(!R32(r.eax+0x4au))W16(r.eax+0x36u,3);
                W16(B+0x64u,2);
                W16(B+0x34u,0);
            }
        }
    } else if(R16(B+0x32u)==3u) {
        r.edx=R32(GVA(0x4d2684u));
        if(r.edx==R32(B+0x2au)) {
            r.ecx=lo(r.ecx,R16(B+0x34u));
            if((uint16_t)r.ecx!=R16(B+0x5cu)) {
                r.eax=lo(r.eax,R16(B+0x5cu));
                W16(B+0x34u,r.eax);
            }
        }
    }
    if(R16(B+0x64u)) {
        cmv_call(g,0x45e208u,NULL,0,&r);
        W16(B+0x64u,R16(B+0x64u)-1u);
    }
    return cmv_done(&r,ecx);
}

/* The field copy of 45dfff..45e0a0: the current camera src into the group
 * camera dst, CX/ECX as the carrier, then the position dwords through ESI;
 * leaves EDX = dst+4 and ECX = [src+8]. */
static void cmv_copy_group(const es2_guest_memory *m,uint32_t dst,uint32_t src,cmv_regs *r) {
    static const uint8_t field[20]={0x22,0x24,0x26,0x2a,0x2c,0x30,0x32,0x36,0x38,0x3c,0x3e,0x42,0x44,0x48,0x4a,0x16,0x1a,0x1e,0x10,0x14};
    static const uint8_t dword[20]={0,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,1,1,0};
    for(unsigned i=0;i<20;++i) {
        if(dword[i]) { r->ecx=R32(src+field[i]);W32(dst+field[i],r->ecx); }
        else { r->ecx=lo(r->ecx,R16(src+field[i]));W16(dst+field[i],r->ecx); }
    }
    const uint32_t from=src+4u,to=dst+4u;
    uint32_t esi=R32(from+8u);W32(to+8u,esi);
    esi=R32(from);W32(to,esi);
    r->ecx=R32(from+4u);W32(to+4u,r->ecx);
    r->edx=to;r->eax=dst;
}
/* The field copy of 45e0d6..45e1b8: the current camera into the static
 * camera 4D2754h at the same offsets, alternating EDX and ECX as in the
 * original; leaves EAX = [C+8], EDX = [C+0Ch], ECX = [C+4]. */
static void cmv_copy_static(const es2_guest_memory *m,uint32_t image,cmv_regs *r) {
    /* register: 0 EDX, 1 ECX; width: 0 word, 1 dword */
    static const uint8_t field[19]={0x22,0x24,0x26,0x2a,0x2c,0x30,0x32,0x36,0x38,0x3c,0x3e,0x42,0x44,0x48,0x4a,0x16,0x1a,0x1e,0x10};
    static const uint8_t reg[19]={0,1,0,0,1,1,0,1,0,0,1,1,0,0,1,0,1,0,1};
    static const uint8_t dword[19]={0,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,1,1};
    const uint32_t S=GVA(0x4d2754u);
    r->eax=R32(GVA(0x4d24eau));
    for(unsigned i=0;i<19;++i) {
        uint32_t *x=reg[i]?&r->ecx:&r->edx;
        if(dword[i]) { *x=R32(r->eax+field[i]);W32(S+field[i],*x); }
        else { *x=lo(*x,R16(r->eax+field[i]));W16(S+field[i],*x); }
    }
    r->ecx=lo(r->ecx,R16(r->eax+0x14u));
    r->eax+=4u;
    W16(S+0x14u,r->ecx);
    r->edx=R32(r->eax+8u);W32(S+0xcu,r->edx);
    r->ecx=R32(r->eax);W32(S+4u,r->ecx);
    r->eax=R32(r->eax+4u);W32(S+8u,r->eax);
}

/* Original: DBSIM.EXE 0x45df34. f = the synthetic EBP; the runtime record
 * (descriptor 49F1FDh, offset -20h) lies at f-20h. */
uint64_t es2_view_subject_set_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t subject,
    const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t image=g->image,f=g->scratch+ES2_VIEW_SUBJECT_FRAME;
    cmv_regs r={GVA(0x49f1fdu),entry[1],entry[2]};uint32_t a[1];
    cmv_call(g,0x49630cu,NULL,0,&r);
    if(!R8(GVA(0x49f1ecu))) {
        /* 45df4f: the static camera, constructed once. */
        W16(f-0x10u,8);
        a[0]=GVA(0x4d2754u);cmv_call(g,0x48bd84u,a,1,&r);
        r.ecx=a[0];
        W32(GVA(0x4d2754u),GVA(0x49f8e8u));
        r.edx=0;W32(GVA(0x4d2786u),r.edx);
        r.ecx=0;W32(GVA(0x4d279eu),r.ecx);
        W16(GVA(0x4d278au),0);
        r.eax=0;
        W16(GVA(0x4d279cu),r.eax);W16(GVA(0x4d279au),r.eax);W16(GVA(0x4d2798u),r.eax);
        r.edx=R32(GVA(0x6cbee4u));W32(r.edx,R32(r.edx)+1u);
        r.ecx=R32(GVA(0x6cbee4u));W32(r.ecx,R32(r.ecx)-3u);
        W8(GVA(0x49f1ecu),R8(GVA(0x49f1ecu))+1u);
    }
    /* 45dfae: the group cameras 4D2688h[0 .. word [[P+45h]+10h]). */
    r.eax=R32(GVA(0x4d24e6u));
    uint32_t ebx=GVA(0x4d2688u);
    r.edx=R32(r.eax+0x45u);
    r.eax=0;
    r.ecx=lo(r.ecx,R16(r.edx+0x10u));
    W16(f-0x22u,r.ecx);
    W32(f-0x28u,r.eax);
    for(;;) {
        r.eax=sx(R16(f-0x22u));
        if(!((int32_t)r.eax>(int32_t)R32(f-0x28u)))break;
        r.edx=R32(ebx);
        r.eax=R32(r.edx+0x32u);
        if(r.eax==subject) {
            /* 45dfd7: the subject's own group camera, in orbit. */
            r.eax=r.edx;
            W32(GVA(0x4d24eau),r.eax);
            if(!R32(r.eax+0x4au))W16(r.eax+0x36u,1);
            break;
        }
        if(!r.eax) {
            /* 45dffb: a free group camera takes the current one's fields. */
            const uint32_t src=R32(GVA(0x4d24eau)),dst=R32(ebx);
            cmv_copy_group(m,dst,src,&r);
            r.eax=R32(ebx);
            W32(GVA(0x4d24eau),r.eax);
            r.edx=subject;a[0]=r.edx;cmv_call(g,0x45edb0u,a,1,&r);
            r.ecx=a[0];
            break;
        }
        W32(f-0x28u,R32(f-0x28u)+1u);
        ebx+=4u;
    }
    r.edx=sx(R16(f-0x22u));
    if(r.edx==R32(f-0x28u)) {
        /* 45e0d6: no group camera: the static camera takes the fields. */
        cmv_copy_static(m,image,&r);
        r.eax=subject;a[0]=r.eax;
        W32(GVA(0x4d24eau),GVA(0x4d2754u));
        cmv_call(g,0x45edb0u,a,1,&r);
        r.ecx=a[0];
    }
    /* 45e1d1: the view subject and the view word. */
    r.edx=subject;W32(GVA(0x4d2684u),r.edx);
    r.ecx=subject;
    r.eax=lo(r.eax,r.ecx==R32(GVA(0x4d24e6u))?R16(GVA(0x4d2518u)):1u);
    W16(GVA(0x4d24f0u),r.eax);
    r.edx=R32(f-0x20u);                         /* the saved FS:0 head */
    return cmv_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x45edb0. EBX = S; f = the synthetic EBP (locals f-8
 * and f-10h, three words each, written by the virtual). */
uint64_t es2_view_offsets_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t subject,
    const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t image=g->image,f=g->scratch+ES2_VIEW_OFFSETS_FRAME;
    cmv_regs r={f-0x10u,f-8u,R32(subject)};uint32_t a[3];
    (void)entry;
    const uint32_t target=R32(r.ecx+0x30u);
    a[0]=subject;a[1]=f-8u;a[2]=f-0x10u;
    if(g->observe)g->observe(g->observer_context,target,0x30u);
    g->call(g->context,target,a,3,&r);
    r.eax=f-8u;r.edx=R32(GVA(0x4d24eau));
    a[0]=r.edx;a[1]=subject;a[2]=f-8u;cmv_call(g,0x40111cu,a,3,&r);
    r.ecx=f-0x10u;r.eax=R32(GVA(0x4d24eau));
    a[0]=r.eax;a[1]=subject;a[2]=f-0x10u;cmv_call(g,0x40109cu,a,3,&r);
    return cmv_done(&r,ecx);
}

/* Original: DBSIM.EXE 0x45edf8. mov eax,[4D24EAh]; ret. */
uint64_t es2_camera_current_registers(const es2_guest_memory *m,uint32_t image,const uint32_t entry[3],
    uint32_t *ecx) {
    if(ecx)*ecx=entry[2];
    return pair(R32(GVA(0x4d24eau)),entry[1]);
}
