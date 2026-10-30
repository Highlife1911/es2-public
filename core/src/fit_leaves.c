#include "es2/core/fit_leaves.h"
#include "fixed.h"
/* The remaining callees of the weapon fit (docs/reverse-engineering/
 * mission_load.md, section "the remaining weapon_fit callees"). The register
 * struct r follows EAX/EDX/ECX instruction by instruction so that each game
 * call sees the original's entry registers; lo() is a 16-bit load that keeps
 * the register's high word. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define GVA(x) (g->image+(uint32_t)(x)-0x400000u)
static uint64_t pair(uint32_t a,uint32_t d) { return (uint64_t)d<<32|a; }
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
static uint32_t sx8(uint8_t x) { return (uint32_t)(int32_t)(int8_t)x; }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static void ecx_out(uint32_t *ecx,uint32_t value) { if(ecx)*ecx=value; }
/* One game call: the observer first (reference VA, slot 0), then the bridge
 * with the entry registers in r. */
static void fl_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,GVA(va),a,n,r);
}

/* Original: DBSIM.EXE 0x40e61c. push ebp; mov ebp,esp; mov eax,[ebp+8];
 * mov eax,[eax+18h]; mov ax,[eax]. */
uint64_t es2_weapon_shape_word_registers(const es2_guest_memory *m,uint32_t weapon,const uint32_t entry[3],
    uint32_t *ecx) {
    const uint32_t definition=R32(weapon+0x18u);
    ecx_out(ecx,entry[2]);
    return pair(lo(definition,R16(definition)),entry[1]);
}

/* Original: DBSIM.EXE 0x40fea0. EAX = 6 x sx(type word) + [4a98e4]. */
uint64_t es2_mount_entry_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t type,
    const uint32_t entry[3],uint32_t *ecx) {
    ecx_out(ecx,entry[2]);
    return pair(6u*sx(type)+R32(GVA(0x4a98e4u)),entry[1]);
}

/* Original: DBSIM.EXE 0x420634. EAX = A, EDX = [A+1F2h] (the actor's
 * mount-type record), CX = word [EDX] + word [4a9d70] in the entry ECX's
 * high word, pushed to 40fea0; POP ECX restores it. */
uint64_t es2_mount_table_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t actor,
    const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={actor,0,entry[2]};uint32_t a[1];
    r.edx=R32(actor+0x1f2u);
    r.ecx=lo(r.ecx,R16(r.edx));
    r.ecx=lo(r.ecx,r.ecx+R16(GVA(0x4a9d70u)));
    const uint32_t pushed=r.ecx;
    a[0]=pushed;fl_call(g,0x40fea0u,a,1,&r);
    ecx_out(ecx,pushed);                       /* POP ECX */
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x413ac8. EBX = S, ESI = sx word +4, EDI = sx word +6
 * (callee-saved, restored); multiply_q10 47e0b0(ESI, EDI) is called with the
 * entry registers, its AX is stored at +0, then +2 = word +6 minus word +0
 * (both reread), EAX keeping multiply_q10's high word. */
uint64_t es2_shield_split_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t record,
    const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[2];
    a[0]=sx(R16(record+4u));a[1]=sx(R16(record+6u));
    fl_call(g,0x47e0b0u,a,2,&r);
    W16(record,r.eax);
    const uint32_t ax=(uint16_t)(R16(record+6u)-R16(record));
    W16(record+2u,ax);
    r.eax=lo(r.eax,ax);
    ecx_out(ecx,r.ecx);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x417d08. EBX = A, ESI = 20, EDI the sum. */
uint64_t es2_energy_rate_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t actor,
    const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={actor+0x92u,actor+0x92u,entry[2]};uint32_t a[2],edi=0x14u;
    if(R8(r.eax+0x19u)) {
        a[0]=0xc8u;a[1]=edi;fl_call(g,0x47e0b0u,a,2,&r);edi=r.eax;
    } else if(R8(r.eax+0x18u)) {
        a[0]=0x258u;a[1]=edi;fl_call(g,0x47e0b0u,a,2,&r);edi=r.eax;
    }
    r.eax=R32(actor+0x313u);
    if(r.eax) {
        /* 417d61: the weapon's mount definition D = [W+18h], index byte
         * D+17h (signed); the pushed ECX carries D's high word. */
        r.ecx=R32(r.eax+0x18u);
        r.eax=sx8(R8(r.ecx+0x17u));
        r.edx=sx(r.eax);
        r.ecx=lo(r.ecx,r.edx+0x13u);
        r.eax=actor+0x206u;
        a[0]=r.eax;a[1]=r.ecx;fl_call(g,0x40dbc0u,a,2,&r);
        r.edx=sx(r.eax);
        const int32_t condition=(int32_t)r.edx;
        if(condition<0xe1) {
            /* 417d95: IDIV by 51, then 204 x quotient (417da0..417dab). */
            const int32_t quotient=condition/0x33;
            const uint32_t value=0x400u-(uint32_t)quotient*204u;
            r.eax=value;r.edx=0x14u;r.ecx=value;
            a[0]=value;a[1]=0x14u;fl_call(g,0x47e0b0u,a,2,&r);
            edi+=r.eax;
        }
    }
    W16(actor+0x256u,edi);
    ecx_out(ecx,r.ecx);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x40fb2c. The weapon ID sx[[W+1Ch]+56h] minus 12h
 * (unsigned, at most 0Eh) indexes the byte table 40fb7a (5, ten zeros, 4,
 * 3, 2, 1), which selects the jump table 40fb89: 1 -> record+0Ch, 2 ->
 * +10h, 3 -> +8, 4 -> +4, 5 -> +0, 0 -> none. */
static const uint8_t fl_slot_kind[15]={5,0,0,0,0,0,0,0,0,0,0,4,3,2,1};
static const uint8_t fl_slot_offset[6]={0xff,0x0c,0x10,0x08,0x04,0x00};
uint64_t es2_equip_slots_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t set,
    uint32_t record,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={set+4u,0,entry[2]};uint32_t a[3];
    uint32_t cursor=R32(set+4u);
    uint16_t counter=R16(set+8u);
    r.edx=cursor;
    r.ecx=lo(r.ecx,counter);
    a[0]=record;a[1]=0;a[2]=0x14u;fl_call(g,0x49681au,a,3,&r);
    for(;;) {
        /* 40fbb7: the word counter is decremented, the cursor advanced
         * only while the old counter is nonzero; a null cursor ends. */
        const uint16_t old=counter;
        counter=(uint16_t)(counter-1u);
        r.eax=lo(r.eax,old);
        if(old) { r.edx=cursor;cursor+=4u; } else r.edx=0;
        if(!r.edx)break;
        r.eax=R32(r.edx);
        if(!r.eax)continue;
        r.edx=R32(r.eax+0x1cu);
        r.ecx=sx(R16(r.edx+0x56u))-0x12u;
        if(r.ecx>0xeu)continue;
        r.ecx=fl_slot_kind[r.ecx];
        if(r.ecx)W32(record+fl_slot_offset[r.ecx],r.eax);
    }
    ecx_out(ecx,counter);                      /* high word: stack residue */
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x417530. EBX = value (dword), ESI = node; the exit
 * registers are tracked on every path: the class compare chain leaves its
 * subtractions in EAX, the child loop ends with EAX = word [node+10h],
 * EDX = sx(counter), and POP ECX/POP ECX leave [EBP-4], which only the
 * array fill writes (node). Returns whether the array fill ran. */
static int fl_node_fill(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t node,uint32_t value,
    es2_weapon_registers *r) {
    uint32_t a[2];int fill=0,children=0;
    r->eax=R32(node);                          /* the vtable */
    a[0]=node;
    g->call(g->context,R32(r->eax),a,1,r);     /* call [eax]: the class id (virtual slot 0) */
    r->ecx=node;                               /* POP ECX */
    const uint32_t c=r->eax;
    if((int32_t)c>0x140014) {
        r->eax=c-0x140015u;
        if(!r->eax)fill=1;
        else {
            r->eax-=0x9ffeeu;
            if(r->eax)r->eax-=2u;
        }
    } else if(c==0x140014u) {
        W16(node+4u,value);
    } else {
        uint32_t t=c-0x140005u;
        r->eax=t-2u;
        if(t>=2u) {
            t=r->eax;r->eax-=2u;
            if(t<2u)children=1;
            else {
                r->eax-=2u;
                if(!r->eax) { W16(node+4u,value);children=1; }
                else { r->eax-=1u;if(!r->eax)children=1; }
            }
        }
    }
    if(fill) {
        /* 417582: the words [node+1Ch][i] for sx(i) < word [node+16h]. */
        uint32_t p=R32(node+0x1cu),i=0;
        while((int32_t)sx(i)<(int32_t)R16(node+0x16u)) { W16(p,value);++i;p+=2u; }
        children=1;
    }
    if(children) {
        /* 4175aa: the children [node+12h], the count word re-read for every
         * test; each call has EAX = child, EDX = sx(counter), ECX = node. */
        uint32_t pointer=R32(node+0x12u),i=0;
        for(;;) {
            r->ecx=node;r->edx=sx(i);r->eax=R16(node+0x10u);
            if(!((int32_t)r->edx<(int32_t)r->eax))break;
            r->eax=R32(pointer);
            fl_node_fill(m,g,r->eax,value,r);
            ++i;pointer+=4u;
        }
    }
    r->ecx=fill?node:0;                        /* POP ECX twice: [EBP-4] */
    return fill;
}
uint64_t es2_node_state_fill_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t node,
    uint32_t value,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    fl_node_fill(m,g,node,value,&r);
    ecx_out(ecx,r.ecx);
    return pair(r.eax,r.edx);
}
