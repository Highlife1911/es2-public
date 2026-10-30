#include "es2/core/lc_base.h"
#include "fixed.h"
/* LC_BASE update (docs/reverse-engineering/vtables.md, section "lc_base_update:
 * contract"). r holds what the original's EAX/EDX/ECX hold at the next game
 * call; lo() is a 16-bit load or 16-bit arithmetic that keeps the register's
 * high word. Memory, including the frame locals at f (the original's EBP), is
 * read again wherever the original reads it again. B is the building, C its
 * configuration [B+1F2h], P the runtime part records [B+205h] (stride 0Bh), Q
 * the configuration part records [C+14h] (stride 1Eh), K a bank (B+209h + 1Fh *
 * i), S a weapon record of the bank (K + 8j), W a mount record ([4a95bch] + 16h
 * * j). */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define VA(va) (g->image+(uint32_t)(va)-0x400000u)
static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
/* Direct calls name the original VA (observer key); the bridge gets the
 * address in this image. A virtual call passes its resolved target and the
 * vtable offset. Registers are updated in place. */
static void lb_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,VA(va),a,n,r);
}
static void lb_virtual(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,
    es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}
/* EBX/ESI/EDI of the original at its next call and the address after that
 * CALL instruction. */
static void lb_frame(es2_ai_call_frame *f,const es2_weapon_graph *g,uint32_t ebx,uint32_t esi,uint32_t edi,
    uint32_t ret) {
    if(f) { f->ebx=ebx;f->esi=esi;f->edi=edi;f->ret=VA(ret); }
}

/* Original: DBSIM.EXE 0x40fe84 */
uint64_t es2_weapon_record_registers(uint32_t table,uint32_t index) {
    uint32_t i=sx(index);
    return pair(table+i*0x58u,i);
}

/* Original: DBSIM.EXE 0x40a9c4. Frame f: the angles pointer f-28h, the
 * runtime record f-24h (context word f-14h), the object f-4. ESI is the
 * owner throughout; EBX the caller's until the object is known. */
uint64_t es2_projectile_create_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_ai_call_frame *frame,
    uint32_t ebx,uint32_t edi,uint32_t type,uint32_t position,uint32_t angles,uint32_t owner,uint32_t word,
    const uint32_t entry[3]) {
    const uint32_t f=g->scratch+ES2_PROJECTILE_CREATE_SCRATCH;
    uint32_t a[3];
    es2_weapon_registers r={VA(0x498370u),entry[1],entry[2]};
    lb_frame(frame,g,ebx,owner,edi,0x40a9dau);lb_call(g,0x49630cu,NULL,0,&r);
    r.edx=R32(VA(0x4a96c2u));a[0]=r.edx;
    lb_frame(frame,g,ebx,owner,edi,0x40a9e6u);lb_call(g,0x471b30u,a,1,&r);
    r.ecx=a[0];
    uint32_t o=r.eax;W32(f-4u,o);
    if(o) {
        W16(f-0x14u,0x14u);
        r.eax=lo(r.eax,type);a[0]=R32(f-4u);a[1]=r.eax;r.edx=a[0];
        lb_frame(frame,g,o,owner,edi,0x40aa04u);lb_call(g,0x40a948u,a,2,&r);
        r.ecx=R32(VA(0x6cbee4u));W32(r.ecx,R32(r.ecx)-5u);
        W16(f-0x14u,8u);
    }
    /* A null object faults in the original at 40aa29 (precondition). */
    r.eax=R32(f-4u);o=r.eax;
    r.edx=position;r.eax=o+0x26u;
    r.ecx=R32(r.edx+8u);W32(r.eax+8u,r.ecx);
    r.ecx=R32(r.edx);W32(r.eax,r.ecx);
    r.edx=R32(r.edx+4u);W32(r.eax+4u,r.edx);
    r.eax=angles;W32(f-0x28u,r.eax);
    r.eax=o+0xcu;r.edx=R32(f-0x28u);
    r.ecx=R32(r.edx);W32(r.eax,r.ecx);
    r.ecx=lo(r.ecx,R16(r.edx+4u));W16(r.eax+4u,r.ecx);
    W16(r.eax+0x26u,0);
    W32(o+0x4au,owner);
    r.eax=lo(r.eax,word);r.eax=lo(r.eax,r.eax+0x1f4u);W16(o+0x52u,r.eax);
    r.eax=o+0x42u;a[0]=r.eax;a[1]=0;a[2]=8;
    lb_frame(frame,g,o,owner,edi,0x40aa70u);lb_call(g,0x49681au,a,3,&r);
    W16(o+0x54u,0);
    r.eax=owner+0x92u;
    if(!R8(r.eax+0x11u)&&(uint16_t)type==3u) r.eax=1;
    else {
        r.edx=lo(r.edx,type);a[0]=owner;a[1]=r.edx;r.ecx=R32(owner);
        lb_frame(frame,g,o,owner,edi,0x40aa9eu);lb_virtual(g,R32(r.ecx+0x6cu),0x6c,a,2,&r);
        r.eax=sx(r.eax);
    }
    if(!r.eax) { r.edx=0;W32(o+0x56u,r.edx); }
    else {
        edi=R32(owner+0x1a4u);W32(o+0x56u,edi);
        if(edi) {
            /* 40aabe tests the object again; it is never null here. */
            r.eax=o?o+0xcu:o;a[0]=edi;a[1]=r.eax;r.edx=R32(edi);
            lb_frame(frame,g,o,owner,edi,0x40aaceu);lb_virtual(g,R32(r.edx+0x54u),0x54,a,2,&r);
            W16(o+0x5au,r.eax);
        }
    }
    r.eax=owner+0x92u;
    if(R8(r.eax+0x11u))W32(VA(0x49c394u),o);
    r.edx=position;a[0]=o;a[1]=r.edx;
    lb_frame(frame,g,o,owner,edi,0x40aaf1u);lb_call(g,0x40a234u,a,2,&r);
    r.ecx=a[0];
    r.ecx=sx(R16(r.eax+0xcu));r.eax=0xau;r.ecx+=r.eax;
    a[0]=r.ecx;a[1]=position;
    lb_frame(frame,g,o,owner,edi,0x40ab03u);lb_call(g,0x4628e0u,a,2,&r);
    r.edx=R32(f-0x24u);
    return pair(r.eax,r.edx);
}

/* One weapon j of the bank K (4047a4..404a71). Returns 0 when the original
 * jumps to 404a75 (next weapon) before the re-arm. */
static void lb_weapon(const es2_guest_memory *m,const es2_weapon_graph *g,es2_ai_call_frame *frame,
    uint32_t b,uint32_t f,uint32_t esi,uint32_t edi,es2_weapon_registers *r) {
    uint32_t a[5];
    r->edx=R32(f-0x14u);r->edx+=r->edx;r->ecx=R32(f-0x44u)!=0;r->edx+=r->ecx;r->edx+=1u;W32(f-0x48u,r->edx);
    r->eax=R32(f-0x48u);r->ecx=R32(b+0x205u);r->edx=r->eax;r->eax=r->edx+4u*r->eax;r->eax=r->edx+2u*r->eax;
    r->edx=R32(f-0x48u);r->eax=lo(r->eax,R16(r->ecx+r->eax));
    r->ecx=r->edx;r->edx=(r->edx<<4)-r->ecx;
    r->ecx=R32(R32(b+0x1f2u)+0x14u);
    if((uint16_t)r->eax==R16(r->ecx+2u*r->edx))return;
    /* Cooldown timer S+3: a new phase and a new cooldown from the tables
     * 4973f4h/4973f8h and one RNG stream A draw. */
    r->eax=esi+3u;a[0]=r->eax;
    lb_frame(frame,g,b,esi,edi,0x4047efu);lb_call(g,0x467a48u,a,1,r);
    r->ecx=a[0];r->edx=!(uint16_t)r->eax;
    if(r->edx) {
        r->eax=R16(esi+6u)==0;W16(esi+6u,r->eax);
        r->edx=sx(r->eax);r->ecx=lo(r->ecx,R16(VA(0x4973f4u)+2u*r->edx));W16(f-0x4au,r->ecx);
        r->eax=sx(R16(esi+6u));r->edx=lo(r->edx,R16(VA(0x4973f8u)+2u*r->eax));
        a[0]=VA(0x4d2599u);a[1]=r->edx;
        lb_frame(frame,g,b,esi,edi,0x404833u);lb_call(g,0x492f24u,a,2,r);
        W16(f-0x4au,R16(f-0x4au)+r->eax);r->eax=lo(r->eax,R16(f-0x4au));W16(esi+4u,r->eax);
    }
    /* Fire timer S, the phase, the cone (16-bit wrapped) and the range. */
    a[0]=esi;
    lb_frame(frame,g,b,esi,edi,0x404848u);lb_call(g,0x467a48u,a,1,r);
    r->ecx=a[0];r->edx=!(uint16_t)r->eax;
    if(!r->edx||!R16(esi+6u))return;
    r->eax=lo(r->eax,R16(edi+2u));r->ecx=lo(r->ecx,R16(f-0x38u));r->ecx=lo(r->ecx,r->ecx+r->eax);r->eax+=r->eax;
    r->eax=(uint16_t)r->ecx<(uint16_t)r->eax;
    if(!r->eax)return;
    r->eax=lo(r->eax,R16(edi));r->edx=lo(r->edx,R16(f-0x3cu));r->edx=lo(r->edx,r->edx+r->eax);r->eax+=r->eax;
    r->ecx=(uint16_t)r->edx<(uint16_t)r->eax;
    if(!r->ecx)return;
    r->eax=R32(edi+4u);
    if(fx_s32(r->eax)<=fx_s32(R32(f-0x20u)))return;
    /* The muzzle: B's position plus the mount offset turned by B's
     * heading in the plane, the offset's third dword unturned. */
    r->eax=edi+8u;
    r->edx=R32(r->eax);W32(f-0x88u,r->edx);
    r->ecx=R32(r->eax+4u);W32(f-0x84u,r->ecx);
    r->eax=R32(r->eax+8u);W32(f-0x80u,r->eax);
    r->eax=b+0x26u;
    r->edx=R32(r->eax);W32(f-0x94u,r->edx);r->edx=f-0x54u;
    r->ecx=R32(r->eax+4u);W32(f-0x90u,r->ecx);
    r->eax=R32(r->eax+8u);W32(f-0x8cu,r->eax);
    r->eax=lo(r->eax,R16(b+0x10u));a[0]=r->eax;a[1]=r->edx;
    lb_frame(frame,g,b,esi,edi,0x4048f4u);lb_call(g,0x47eb30u,a,2,r);
    r->ecx=f-0xa0u;r->eax=f-0x54u;r->edx=f-0x88u;a[0]=r->edx;a[1]=r->eax;a[2]=r->ecx;
    lb_frame(frame,g,b,esi,edi,0x40490eu);lb_call(g,0x47ffe8u,a,3,r);
    r->eax=R32(f-0x80u);W32(f-0x98u,r->eax);W32(f-0x8cu,R32(f-0x8cu)+r->eax);
    r->edx=R32(f-0xa0u);W32(f-0x94u,R32(f-0x94u)+r->edx);
    r->ecx=R32(f-0x9cu);W32(f-0x90u,R32(f-0x90u)+r->ecx);
    if(!R32(f-0x44u)) {
        /* The cannon: the target is linked to B for the projectile's
         * homing (virtual +54h in projectile_create) and unlinked after. */
        r->eax=R32(R32(f-0x1cu)+0x1bu);
        if(r->eax!=R32(b+0x1a4u)) {
            if(R32(b+0x1a4u)) { r->edx=R32(b+0x1a4u);W16(r->edx+0x1a2u,R16(r->edx+0x1a2u)-1u); }
            W32(b+0x1a4u,r->eax);
            if(r->eax) { r->edx=R32(b+0x1a4u);W16(r->edx+0x1a2u,R16(r->edx+0x1a2u)+1u); }
            r->ecx=b+0x92u;W32(f-0x58u,r->ecx);r->eax=R32(f-0x58u);W8(r->eax+0xbu,1);
        }
        r->edx=b+0xcu;r->ecx=f-0x94u;a[0]=3;a[1]=r->ecx;a[2]=r->edx;a[3]=b;a[4]=0;
        lb_frame(frame,g,b,esi,edi,0x4049a2u);lb_call(g,0x40a9c4u,a,5,r);
        if(R32(b+0x1a4u)) {
            /* 4049b9 repeats the test; 4049d2 tests the XOR-zero EDX. */
            r->eax=R32(b+0x1a4u);W16(r->eax+0x1a2u,R16(r->eax+0x1a2u)-1u);
            r->edx=0;W32(b+0x1a4u,r->edx);
            r->ecx=b+0x92u;W32(f-0x5cu,r->ecx);r->eax=R32(f-0x5cu);W8(r->eax+0xbu,1);
        }
    } else {
        /* The lasers: immediate impact of type 3 with the range and scale
         * of weapon record 8 along the Euler matrix of the aim angles. */
        r->edx=f-0x94u;r->ecx=f-0x7cu;r->eax=f-0x64u;a[0]=r->eax;a[1]=r->ecx;a[2]=r->edx;
        lb_frame(frame,g,b,esi,edi,0x404a07u);lb_call(g,0x492990u,a,3,r);
        a[0]=8;
        lb_frame(frame,g,b,esi,edi,0x404a11u);lb_call(g,0x40fe84u,a,1,r);
        r->ecx=a[0];W32(f-0x68u,r->eax);
        r->edx=R32(f-0x8cu);W32(f-0xa4u,r->edx);r->edx=f-0xc0u;
        r->ecx=R32(f-0x94u);W32(f-0xacu,r->ecx);r->ecx=f-0x64u;
        r->eax=R32(f-0x90u);W32(f-0xa8u,r->eax);
        a[0]=r->ecx;a[1]=r->edx;
        lb_frame(frame,g,b,esi,edi,0x404a49u);lb_call(g,0x47ebb8u,a,2,r);
        r->eax=R32(f-0x68u);r->edx=lo(r->edx,R16(r->eax+0x38u));a[4]=r->edx;a[3]=b;
        r->ecx=R32(f-0x68u);r->edx=f-0xc0u;r->eax=R32(r->ecx+0x30u);a[2]=r->eax;a[1]=r->edx;a[0]=3;
        lb_frame(frame,g,b,esi,edi,0x404a6au);lb_call(g,0x40bf74u,a,5,r);
    }
    r->eax=lo(r->eax,R16(edi+0x14u));W16(esi+1u,r->eax);
}

/* Original: DBSIM.EXE 0x4045c8. Frame f: see vtables.md (the heading
 * words f-6/-8/-6Ah/-6Ch, the bank index f-14h and pointer f-1Ch, the
 * distance f-20h, the target f-24h, the virtual +30h records f-2Ch/-34h,
 * the angles f-3Ch, the weapon index f-44h and part f-48h, the cooldown
 * f-4Ah, the sine/cosine pair f-54h, the laser angles f-64h and record
 * f-68h, the aim point f-7Ch, the offset f-88h, the muzzle f-94h, the
 * turned offset f-0A0h, the laser matrix f-0C0h). */
uint64_t es2_lc_base_update_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_ai_call_frame *frame,
    uint32_t esi,uint32_t edi,uint32_t b,const uint32_t entry[3]) {
    const uint32_t f=g->scratch+ES2_LC_BASE_UPDATE_SCRATCH;
    uint32_t a[3];
    es2_weapon_registers r={b+0x92u,0,entry[2]};
    W32(f-4u,r.eax);r.edx=R32(f-4u);
    a[0]=b;
    if(R8(r.edx+7u)) {
        lb_frame(frame,g,b,esi,edi,0x4045efu);lb_call(g,0x403ca8u,a,1,&r);
        return pair(r.eax,r.edx);
    }
    lb_frame(frame,g,b,esi,edi,0x4045fbu);lb_call(g,0x403914u,a,1,&r);
    r.ecx=a[0];r.edx=b+0xcu;
    r.ecx=lo(r.ecx,R16(b+0x10u));W16(f-6u,r.ecx);
    r.eax=lo(r.eax,R16(f-6u));r.eax=lo(r.eax,r.eax+0x1555u);W16(f-8u,r.eax);
    W32(f-0xcu,r.edx);
    r.eax=lo(r.eax,R16(f-8u));r.ecx=R32(f-0xcu);W16(r.ecx+4u,r.eax);
    r.ecx=b+0x92u;r.edx=R32(f-0xcu);W16(r.edx+0x26u,0);
    W32(f-0x10u,r.ecx);r.eax=R32(f-0x10u);
    if(!R8(r.eax+0x13u)) {
        /* Every weapon part 1..count-1 destroyed (runtime word >= its
         * maximum): the disabled byte B+0A5h; the banks still run now. */
        r.ecx=R32(b+0x1f2u);r.edx=R32(b+0x205u)+0xbu;r.eax=R32(r.ecx+0x14u);W32(f-0x14u,1);r.eax+=0x1eu;
        for(;;) {
            r.ecx=sx(R16(R32(b+0x1f2u)+0x12u));
            if(!(fx_s32(r.ecx)>fx_s32(R32(f-0x14u))))break;
            r.ecx=lo(r.ecx,R16(r.edx));
            if(fx_s16(r.ecx)<fx_s16(R16(r.eax)))break;
            W32(f-0x14u,R32(f-0x14u)+1u);r.edx+=0xbu;r.eax+=0x1eu;
        }
        r.eax=R32(b+0x1f2u);r.edx=sx(R16(r.eax+0x12u));
        if(r.edx==R32(f-0x14u)) { r.eax=b+0x92u;W32(f-0x18u,r.eax);r.edx=R32(f-0x18u);W8(r.edx+0x13u,1); }
        r.ecx=b+0x209u;r.eax=0;W32(f-0x1cu,r.ecx);W32(f-0x14u,r.eax);
        do {
            /* The bank's target timer K+18h: a new target (radius 10h,
             * range 3000h) when it runs out. */
            r.edx=R32(f-0x1cu)+0x18u;a[0]=r.edx;
            lb_frame(frame,g,b,esi,edi,0x4046b7u);lb_call(g,0x467a48u,a,1,&r);
            r.ecx=!(uint16_t)r.eax;
            if(r.ecx) {
                a[0]=b;a[1]=0x10u;a[2]=0x3000u;
                lb_frame(frame,g,b,esi,edi,0x4046d3u);lb_call(g,0x411fa0u,a,3,&r);
                r.edx=R32(f-0x1cu);W32(r.edx+0x1bu,r.eax);r.eax=R32(f-0x1cu);W16(r.eax+0x19u,0x2710u);
            }
            r.ecx=R32(f-0x1cu);
            if(R32(r.ecx+0x1bu)) {
                r.eax=R32(f-0x1cu);r.edx=R32(r.eax+0x1bu);W32(f-0x24u,r.edx);
                r.ecx=R32(f-0x24u)+0x26u;r.eax=b+0x26u;a[0]=r.eax;a[1]=r.ecx;
                lb_frame(frame,g,b,esi,edi,0x40470bu);lb_call(g,0x49288cu,a,2,&r);
                W32(f-0x20u,r.eax);
                r.edx=R32(f-0x1cu);r.eax=R32(r.edx+0x1bu)+0x26u;
                r.edx=R32(r.eax);W32(f-0x7cu,r.edx);r.edx=f-0x34u;
                r.ecx=R32(r.eax+4u);W32(f-0x78u,r.ecx);r.ecx=f-0x2cu;
                r.eax=R32(r.eax+8u);W32(f-0x74u,r.eax);
                a[2]=r.edx;a[1]=r.ecx;
                r.eax=R32(f-0x1cu);r.edx=R32(r.eax+0x1bu);a[0]=r.edx;r.ecx=R32(r.edx);
                lb_frame(frame,g,b,esi,edi,0x40473fu);lb_virtual(g,R32(r.ecx+0x30u),0x30,a,3,&r);
                r.eax=sx(R16(f-0x30u));W32(f-0x74u,R32(f-0x74u)+r.eax);
                r.edx=sx(R16(f-0x34u));W32(f-0x7cu,R32(f-0x7cu)+r.edx);
                r.ecx=sx(R16(f-0x32u));W32(f-0x78u,R32(f-0x78u)+r.ecx);
                r.eax=b+0x26u;r.edx=f-0x7cu;r.ecx=f-0x3cu;a[0]=r.ecx;a[1]=r.edx;a[2]=r.eax;
                lb_frame(frame,g,b,esi,edi,0x404768u);lb_call(g,0x492990u,a,3,&r);
                /* The aim angles relative to B's orientation. */
                r.eax=b+0xcu;W32(f-0x40u,r.eax);
                r.edx=R32(f-0x40u);r.ecx=lo(r.ecx,R16(r.edx+4u));W16(f-0x38u,R16(f-0x38u)-(uint16_t)r.ecx);
                r.eax=R32(f-0x40u);r.edx=lo(r.edx,R16(r.eax));W16(f-0x3cu,R16(f-0x3cu)-(uint16_t)r.edx);
                r.ecx=R32(f-0x40u);r.eax=lo(r.eax,R16(r.ecx+2u));W16(f-0x3au,R16(f-0x3au)-(uint16_t)r.eax);
                edi=R32(VA(0x4a95bcu));esi=R32(f-0x1cu);r.eax=0;W32(f-0x44u,r.eax);
                do {
                    lb_weapon(m,g,frame,b,f,esi,edi,&r);
                    W32(f-0x44u,R32(f-0x44u)+1u);edi+=0x16u;esi+=8u;
                } while(fx_s32(R32(f-0x44u))<3);
            }
            /* The next bank faces 5554h further. */
            r.edx=lo(r.edx,R16(b+0x10u));r.edx=lo(r.edx,r.edx+0x5554u);W16(f-0x6au,r.edx);
            r.eax=b+0xcu;r.edx=lo(r.edx,R16(f-0x6au));W16(r.eax+4u,r.edx);W16(r.eax+0x26u,0);
            W32(f-0x14u,R32(f-0x14u)+1u);W32(f-0x1cu,R32(f-0x1cu)+0x1fu);
        } while(fx_s32(R32(f-0x14u))<3);
    }
    /* The heading of before and the matrix cache cleared. */
    r.ecx=lo(r.ecx,R16(f-6u));W16(f-0x6cu,r.ecx);W32(f-0x70u,b+0xcu);
    r.edx=lo(r.edx,R16(f-0x6cu));r.eax=R32(f-0x70u);W16(r.eax+4u,r.edx);
    r.eax=0;r.ecx=R32(f-0x70u);W16(r.ecx+0x26u,0);
    return pair(r.eax,r.edx);
}
