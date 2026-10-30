#include "es2/core/death.h"
#include "fixed.h"
/* The player's death sequence (docs/reverse-engineering/main_loop.md,
 * "death sequence 45fa84"). Pointers in the view block are reread
 * after every store and call, as the original does, so stores that alias
 * them behave the same. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define VA(va) (g->image+(uint32_t)(va)-0x400000u)
/* The view block and its fields (offsets from 4d24bc). */
#define VIEW VA(0x4d24bcu)
enum { V_DISPLAY=0x04u, V_PLAYER=0x2au, V_CAMERA=0x2eu, V_MODE=0x32u, V_OUTSIDE=0x34u,
    V_REQUEST=0x5eu, V_SUBJECT=0x60u, V_COUNTDOWN=0x64u };
/* Camera fields: the attachment block +22h/+26h/+2Ah and the lock +4Ah. */
enum { C_HEIGHT=0x22u, C_ANGLES=0x26u, C_ROLL=0x2au, C_LOCK=0x4au };

static void death_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,VA(va),a,n,r);
}
/* A 16-bit register load: the high word stays. */
static uint32_t death_word(uint32_t high,uint32_t low) { return (high&0xffff0000u)|(uint16_t)low; }

/* Original: DBSIM.EXE 0x45fa84 */
uint64_t es2_death_sequence_registers(const es2_guest_memory *m,const es2_weapon_graph *g,const uint32_t entry[3]) {
    uint32_t player=R32(VIEW+V_PLAYER);
    /* The player's class word selects one entry of each table; it is not
     * range-checked (classes 0 and 1 exist, other values read neighbours). */
    uint32_t k=(uint32_t)fx_s16(R16(R32(player+0x1f2u)+0x50u));
    es2_weapon_registers r={0,0,entry[2]};
    uint32_t a[4];
    if(!R32(VA(0x4d2a68u))) {
        W32(VA(0x4d2a68u),1u);
        r.eax=R32(VA(0x49f4f4u)+4u*k);
        W32(VA(0x4d2a59u),r.eax);
        W32(VIEW+V_SUBJECT,player);
        if(R16(VIEW+V_MODE)==2u) {
            /* Outside view: request display state 2, then view mode 3. */
            r.edx=R32(VIEW+V_DISPLAY);a[0]=r.edx;a[1]=2;
            death_call(g,0x42a3f4u,a,2,&r);
            W16(VIEW+V_REQUEST,3);
        } else {
            /* Otherwise the player becomes the view subject again. */
            r.edx=player;r.ecx=R32(VIEW+V_PLAYER);a[0]=r.ecx;
            death_call(g,0x45df34u,a,1,&r);
            r.ecx=a[0];
            W16(VIEW+V_COUNTDOWN,2);
        }
        W16(VIEW+V_OUTSIDE,1);
        r.eax=R32(VIEW+V_DISPLAY);a[0]=r.eax;
        death_call(g,0x42a4c4u,a,1,&r);
        r.ecx=a[0];
        death_call(g,0x45de30u,a,0,&r);
        /* The death camera's attachment, unless the camera is locked. */
        uint32_t angles=R16(VA(0x49f508u)+2u*k),roll=R16(VA(0x49f50cu)+2u*k);
        uint32_t camera=R32(VIEW+V_CAMERA);
        if(!R32(camera+C_LOCK)) { W32(camera+C_ANGLES,angles);W16(camera+C_ROLL,roll); }
        camera=R32(VIEW+V_CAMERA);
        if(!R32(camera+C_LOCK))W16(camera+C_HEIGHT,R32(VA(0x49f510u)+4u*k));
    }
    /* Every frame: the outside camera control runs with the lock released. */
    uint32_t camera=R32(VIEW+V_CAMERA);
    W32(camera+C_LOCK,0);
    r.ecx=death_word(r.ecx,R16(VA(0x49f4fcu)+2u*k));
    r.edx=R16(VA(0x49f500u)+2u*k);r.eax=camera;
    a[0]=camera;a[1]=r.ecx;a[2]=r.edx;a[3]=death_word(r.ecx,R16(VA(0x49f504u)+2u*k));
    death_call(g,0x401c74u,a,4,&r);
    r.eax=R32(VIEW+V_CAMERA);W32(r.eax+C_LOCK,1u);
    a[0]=VA(0x4d2a58u);
    death_call(g,0x467aa8u,a,1,&r);
    return (uint64_t)r.edx<<32|(r.eax==0u);
}
