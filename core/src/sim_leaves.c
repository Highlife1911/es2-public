#include "es2/core/sim_leaves.h"
#include "fixed.h"
/* The last simulation leaves (docs/reverse-engineering/vtables.md, section
 * "simulation leaves"). r follows EAX/EDX/ECX instruction by instruction so
 * that each game call sees the original's registers; lo() is a 16-bit load that
 * keeps the register's high word. Memory is read again wherever the original
 * reads it again, in the original's order. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
/* A DBSIM VA (globals, pushed addresses). */
#define IVA(x) (image+(uint32_t)(x)-0x400000u)
#define GVA(x) (g->image+(uint32_t)(x)-0x400000u)
static uint64_t pair(uint32_t a,uint32_t d) { return (uint64_t)d<<32|a; }
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
static uint32_t sx8(uint8_t x) { return (uint32_t)(int32_t)(int8_t)x; }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static void ecx_out(uint32_t *ecx,uint32_t value) { if(ecx)*ecx=value; }
/* One game call: the observer first (reference VA, slot 0), then the bridge
 * with the entry registers in r. */
static void sl_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,GVA(va),a,n,r);
}
/* A virtual call: the resolved (absolute) target and the vtable offset go to
 * the observer, the arguments and registers to the bridge. */
static void sl_virtual(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,
    es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}

/* The event port's slot 0 with (port, record): the observer key is the
 * call site (a vtable slot 0 is not a counter key), the target the
 * absolute address read from the vtable. */
static void sl_port(const es2_weapon_graph *g,uint32_t site,uint32_t target,const uint32_t *a,
    es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,site,0);
    g->call(g->context,target,a,2,r);
}

/* Original: DBSIM.EXE 0x4030d0. Locals: PA/cntA from A, PB/cntB from B (the
 * counts are words, decremented with 16-bit wrap and tested before the
 * decrement). The outer loop takes the next slot of A (EAX), the inner
 * step the next entry of B (EDX); a zero entry ends nothing, it only skips.
 * EBX is saved and restored. */
uint64_t es2_part_shapes_bind_registers(const es2_guest_memory *m,uint32_t a,uint32_t b,uint32_t pool,
    const uint32_t entry[3],uint32_t *ecx) {
    uint32_t eax=a,edx=R32(a),ecx_=pool,pa=edx,ebx;
    uint16_t ca=R16(a+4u);
    edx=R32(b);
    uint32_t pb=edx;
    uint16_t cb=R16(b+4u);
    (void)entry;
    for(;;) {
        /* 403148 */
        edx=lo(edx,ca);ca=(uint16_t)(ca-1u);
        if((uint16_t)edx!=0) { eax=pa;pa+=4u; } else eax=0;
        if(eax==0)break;
        /* 4030fc */
        edx=lo(edx,cb);cb=(uint16_t)(cb-1u);
        if((uint16_t)edx!=0) { edx=pb;pb+=4u; } else edx=0;
        if(edx==0)continue;
        if(R32(eax)==0) { ecx_+=0x18u;continue; }
        if(R32(edx)!=0)edx=R32(edx); else edx=ecx_;
        ebx=R32(eax);ebx=R32(ebx);ebx=lo(ebx,R16(ebx+4u));W16(edx+4u,ebx);
        ebx=R32(eax);ebx=R32(ebx);ebx=lo(ebx,R16(ebx+6u));W16(edx+6u,ebx);
        eax=R32(eax);W32(eax,edx);
        ecx_+=0x18u;
    }
    ecx_out(ecx,ecx_);
    return pair(0,edx);
}

/* Original: DBSIM.EXE 0x403668. EBX walks the 0Bh-byte words from
 * [O+205h], EAX the 1Eh-byte part records from [[O+1F2h]+14h]; the count
 * word [[O+1F2h]+12h] is read again at every test. EBX and ESI are saved
 * and restored. */
uint64_t es2_parts_intact_registers(const es2_guest_memory *m,uint32_t object,const uint32_t entry[3],uint32_t *ecx) {
    uint32_t edx=0,eax,ebx,esi;
    (void)entry;
    eax=R32(object+0x1f2u);ebx=R32(object+0x205u);eax=R32(eax+0x14u);
    for(;;) {
        esi=R32(object+0x1f2u);esi=sx(R16(esi+0x12u));
        if(!((int32_t)edx<(int32_t)esi))break;
        if((int16_t)R16(eax+6u)>-1) {
            esi=lo(esi,R16(ebx));
            if((int16_t)esi<(int16_t)R16(eax))break;
        }
        edx++;ebx+=0x0bu;eax+=0x1eu;
    }
    eax=R32(object+0x1f2u);
    const uint32_t count=sx(R16(eax+0x12u));
    ecx_out(ecx,count);
    return pair(edx==count?1u:0u,edx);
}

/* Original: DBSIM.EXE 0x405b9c (_, row, col). The three-dword cell 4975d6
 * is cleared on the first call (the byte 4975e2 counts that). The table
 * entry is [[4a95ac]+1Ch x sx(row)+4] + 0Ah x sx(col) - 0Ah. */
uint64_t es2_formation_slot_registers(const es2_guest_memory *m,uint32_t image,uint32_t row,uint32_t col,
    const uint32_t entry[3],uint32_t *ecx) {
    uint32_t eax,edx,ecx_;
    (void)entry;
    if(R8(IVA(0x4975e2u))==0) {
        W32(IVA(0x4975d6u),0);W32(IVA(0x4975dau),0);W32(IVA(0x4975deu),0);
        W8(IVA(0x4975e2u),R8(IVA(0x4975e2u))+1u);
    }
    eax=sx(row);edx=eax*7u;
    ecx_=R32(IVA(0x4a95acu));eax=R32(ecx_+4u*edx+4u);
    edx=sx(col);ecx_=edx*10u;
    eax=eax+ecx_-0x0au;
    edx=R32(eax);W32(IVA(0x4975d6u),edx);
    eax=R32(eax+4u);W32(IVA(0x4975dau),eax);
    ecx_out(ecx,ecx_);
    return pair(IVA(0x4975d6u),edx);
}

/* Original: DBSIM.EXE 0x407b20. */
uint64_t es2_explosion_entry_registers(const es2_guest_memory *m,uint32_t image,uint32_t object,
    const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t edx=sx8(R8(object+0x41u))*40u+R32(IVA(0x4a9628u));
    ecx_out(ecx,entry[2]);
    return pair(edx,edx);
}

/* Original: DBSIM.EXE 0x40991c. EBX = O. terrain_pose 4029d8(O) and
 * shape_actor_matrix 401fe4(O) run with the previous call's EAX/EDX, the
 * first with the entry registers; POP ECX leaves ECX = O. The child
 * [O+34h] gets slot +1Ch with EAX = child, EDX = its vtable, ECX = O; the
 * last POP ECX leaves ECX = child. */
uint64_t es2_flat_draw_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t object,
    const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[1];
    a[0]=object;sl_call(g,0x4029d8u,a,1,&r);r.ecx=object;
    a[0]=object;sl_call(g,0x401fe4u,a,1,&r);r.ecx=object;
    const uint32_t child=R32(object+0x34u);
    r.eax=child;r.edx=R32(child);
    a[0]=child;sl_virtual(g,R32(r.edx+0x1cu),0x1cu,a,1,&r);
    r.ecx=child;
    ecx_out(ecx,r.ecx);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x40a120. EAX = 0 is stored to 4a5b04 and passed to
 * shape_actor_matrix 401fe4(O) (EDX and ECX are the entry values); the
 * child's slot +1Ch gets EAX = 401fe4's EAX, EDX = child, ECX = the vtable.
 * Afterwards 4a5b04 = EAX = [4a5b08]. */
uint64_t es2_projectile_draw_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t object,
    const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={0,entry[1],entry[2]};uint32_t a[1];
    W32(GVA(0x4a5b04u),r.eax);
    a[0]=object;sl_call(g,0x401fe4u,a,1,&r);r.ecx=object;
    const uint32_t child=R32(object+0x34u);
    r.edx=child;r.ecx=R32(child);
    a[0]=child;sl_virtual(g,R32(r.ecx+0x1cu),0x1cu,a,1,&r);
    r.ecx=child;
    r.eax=R32(GVA(0x4a5b08u));W32(GVA(0x4a5b04u),r.eax);
    ecx_out(ecx,r.ecx);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x40c2a0. EBX = O+5Ch is pushed to timer_word_step
 * 467a48 with the entry registers; SETE AL then AND EAX,1 leave exactly
 * 0 or 1; POP ECX leaves ECX = O+5Ch. */
uint64_t es2_beam_done_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t object,
    const uint32_t entry[3],uint32_t *ecx) {
    (void)m;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[1];
    a[0]=object+0x5cu;sl_call(g,0x467a48u,a,1,&r);
    const uint32_t done=(uint16_t)r.eax==0?1u:0u;
    ecx_out(ecx,object+0x5cu);
    return pair(done,r.edx);
}

/* Original: DBSIM.EXE 0x410670. ESI = result, EDX = index (a dword; the
 * loop test is the signed word compare), EAX walks the pointer array; ECX
 * is the last pointer loaded. EBX, ESI and EDI are saved and restored. */
uint64_t es2_weapon_by_mount_registers(const es2_guest_memory *m,uint32_t list,uint32_t w,
    const uint32_t entry[3],uint32_t *ecx) {
    uint32_t esi=0,edx=0,eax=R32(list+4u),ecx_=entry[2],edi;
    while((int16_t)edx<(int16_t)R16(list+8u)) {
        ecx_=R32(eax);
        if(ecx_!=0) {
            edi=R32(ecx_+0x18u);edi=sx8(R8(edi+0x17u));
            if((uint16_t)edi==(uint16_t)w) { esi=ecx_;break; }
        }
        edx++;eax+=4u;
    }
    ecx_out(ecx,ecx_);
    return pair(esi,edx);
}

/* Original: DBSIM.EXE 0x411dd4. ECX = sx(word [L+6]); the count is read
 * again for CX and then incremented in memory; EAX = ECX; word [p+4Bh] =
 * AX. */
uint64_t es2_ptr_list_add_registers(const es2_guest_memory *m,uint32_t list,uint32_t p,const uint32_t entry[3],
    uint32_t *ecx) {
    uint32_t c,b;
    (void)entry;
    c=sx(R16(list+6u));b=R32(list);
    W32(b+4u*c,p);
    c=lo(c,R16(list+6u));
    W16(list+6u,R16(list+6u)+1u);
    W16(p+0x4bu,c);
    ecx_out(ecx,c);
    return pair(c,p);
}

/* Original: DBSIM.EXE 0x413a90. DX = w (the entry EDX's high word stays);
 * ECX = sx(word [R+6] read again) >> 1 (arithmetic). */
uint64_t es2_box_square_init_registers(const es2_guest_memory *m,uint32_t rect,uint32_t w,
    const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t edx=lo(entry[1],w);
    W16(rect+6u,edx);W16(rect+8u,edx);W16(rect+4u,0x200u);
    const uint32_t c=(uint32_t)((int32_t)sx(R16(rect+6u))>>1);
    W16(rect+2u,c);W16(rect,c);
    ecx_out(ecx,c);
    return pair(rect,edx);
}

/* Original: DBSIM.EXE 0x41a6d0 (twist) and 0x41a994 (pitch), the same code
 * on other cells. EAX = w (the whole dword, AX negated when negative), EBX =
 * V. Flag byte clear, AX above 0C0h (signed) and the latch word different
 * from the servo word: sfx_playing 462af0(21h) with EAX = AX', EDX = the
 * latch word in DX, entry ECX; a zero return sets the flag; POP ECX leaves
 * ECX = 21h. Flag set and (AX = 0 or latch = servo word): flag cleared.
 * CX = servo word, stored to the latch. */
static uint64_t sl_latch(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t vehicle,uint32_t w,
    const uint32_t entry[3],uint32_t *ecx,uint32_t flag_va,uint32_t word_va,uint32_t offset) {
    es2_weapon_registers r={w,entry[1],entry[2]};uint32_t a[1];
    const uint32_t flag=GVA(flag_va),word=GVA(word_va);
    if((int16_t)r.eax<0)r.eax=lo(r.eax,0u-(uint16_t)r.eax);
    int again=0;
    if(R8(flag)==0 && (int16_t)r.eax>0xc0) {
        r.edx=lo(r.edx,R16(word));
        if((uint16_t)r.edx!=R16(vehicle+offset)) {
            a[0]=0x21u;sl_call(g,0x462af0u,a,1,&r);r.ecx=0x21u;
            if(r.eax==0)W8(flag,1);
            again=1;
        }
    }
    if(!again && R8(flag)!=0) {
        int clear=1;
        if((uint16_t)r.eax!=0) {
            r.eax=lo(r.eax,R16(word));
            if((uint16_t)r.eax!=R16(vehicle+offset))clear=0;
        }
        if(clear)W8(flag,0);
    }
    r.ecx=lo(r.ecx,R16(vehicle+offset));W16(word,r.ecx);
    ecx_out(ecx,r.ecx);
    return pair(r.eax,r.edx);
}
uint64_t es2_twist_sound_latch_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t vehicle,
    uint32_t w,const uint32_t entry[3],uint32_t *ecx) {
    return sl_latch(m,g,vehicle,w,entry,ecx,0x49a1c9u,0x49a1cau,0x298u);
}
uint64_t es2_pitch_sound_latch_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t vehicle,
    uint32_t w,const uint32_t entry[3],uint32_t *ecx) {
    return sl_latch(m,g,vehicle,w,entry,ecx,0x49a1ccu,0x49a1ceu,0x29au);
}

/* Original: DBSIM.EXE 0x41ff00 and 0x422e80. EAX = the byte at
 * 4a9d08 / 4a9dcc + sx(w); EDX and ECX unchanged. */
uint64_t es2_mech_type_index_registers(const es2_guest_memory *m,uint32_t image,uint32_t w,const uint32_t entry[3],
    uint32_t *ecx) {
    ecx_out(ecx,entry[2]);
    return pair(R8(sx(w)+IVA(0x4a9d08u)),entry[1]);
}
uint64_t es2_flyer_type_index_registers(const es2_guest_memory *m,uint32_t image,uint32_t w,const uint32_t entry[3],
    uint32_t *ecx) {
    ecx_out(ecx,entry[2]);
    return pair(R8(sx(w)+IVA(0x4a9dccu)),entry[1]);
}

/* The 24-byte event frame {word code, dword at +2, 18 bytes the original
 * never writes (stack residue, zero here: presentation state)}. */
static void sl_record(const es2_guest_memory *m,uint32_t record,uint32_t code,uint32_t second) {
    W32(record+2u,second);W16(record,code);
    W32(record+6u,0);W32(record+10u,0);W32(record+14u,0);W32(record+18u,0);W16(record+22u,0);
}

/* Original: DBSIM.EXE 0x420a68. EAX = [49b088] (the main object), EDX =
 * its port [EAX+20Bh], ECX = the port's vtable; slot 0 gets (port, record). */
uint64_t es2_cockpit_notice_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t id,
    const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={0,entry[1],entry[2]};uint32_t a[2];
    const uint32_t record=g->scratch;
    sl_record(m,record,id,0);
    r.eax=R32(GVA(0x49b088u));r.edx=lo(r.edx,id);
    r.edx=R32(r.eax+0x20bu);r.ecx=R32(r.edx);
    a[0]=r.edx;a[1]=record;
    sl_port(g,0x420a8du,R32(r.ecx),a,&r);
    ecx_out(ecx,r.ecx);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x420a98. EAX = A, EDX = A+92h; the call returns
 * (A, A+92h) at once when byte [A+99h] is set and the flag word is zero.
 * Else CX = code, the record {code, A} is built and port_a 433158 gives
 * the port in EAX; slot 0 gets EAX = vtable, EDX = record. */
uint64_t es2_radio_notice_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t actor,
    uint32_t code,uint32_t flag,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={actor,actor+0x92u,entry[2]};uint32_t a[2];
    if(R8(r.edx+7u)!=0 && (uint16_t)flag==0) {
        ecx_out(ecx,r.ecx);
        return pair(r.eax,r.edx);
    }
    const uint32_t record=g->scratch;
    r.ecx=lo(r.ecx,code);
    sl_record(m,record,r.ecx,actor);
    sl_call(g,0x433158u,a,0,&r);
    const uint32_t port=r.eax;
    r.edx=record;r.eax=R32(port);
    a[0]=port;a[1]=record;
    sl_port(g,0x420acbu,R32(r.eax),a,&r);
    ecx_out(ecx,r.ecx);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x426aec. */
uint64_t es2_notice_time_registers(const es2_guest_memory *m,uint32_t image,const uint32_t entry[3],uint32_t *ecx) {
    uint32_t eax=0x7530u;
    if(R16(IVA(0x4a9e5eu))!=1u)eax+=0x7530u;
    ecx_out(ecx,entry[2]);
    return pair(eax,entry[1]);
}
