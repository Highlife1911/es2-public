#include "es2/core/hud_helpers.h"
#include "fixed.h"
/* Pure HUD-region helpers (docs/reverse-engineering/hud.md, "HUD-region
 * helpers"). Reads only; they follow the original's order. */
#define R32(a) m->read32(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define VA(x) (image+(uint32_t)(x)-0x400000u)
static int32_t s32(uint32_t x) { return (int32_t)fx_s32(x); }
static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }

/* Original: DBSIM.EXE 0x431410 */
uint64_t es2_panel_visible_registers(const es2_guest_memory *m,uint32_t image,uint32_t rect,uint32_t entry_edx) {
    uint32_t h=R32(VA(0x49b088u));
    /* Panel off (byte H+20Fh): EAX = 0, EDX untouched. */
    if(R8(h+0x20fu))return pair(0,entry_edx);
    /* The view: clip origin C+210h plus canvas origin C+220h, size from
     * 4d253e/4d2542. The clip rect's far corner C+218h/C+21Ch is copied
     * into the local and overwritten at once. */
    uint32_t c=R32(h+4u);
    uint32_t vx1=R32(c+0x210u),vy1=R32(c+0x214u);
    (void)R32(c+0x218u);(void)R32(c+0x21cu);
    vx1+=R32(c+0x220u);vy1+=R32(c+0x224u);
    uint32_t vx2=vx1+R32(VA(0x4d253eu)),vy2=vy1+R32(VA(0x4d2542u));
    /* The argument rect moved by the scroll offset 4d2556/4d255a; the
     * second read of 4d2556 (4314aa) is what EDX keeps. */
    uint32_t rx1=R32(rect),ry1=R32(rect+4u),rx2=R32(rect+8u),ry2=R32(rect+12u);
    uint32_t dx=R32(VA(0x4d2556u)),dy=R32(VA(0x4d255au));
    rx1-=dx;ry1-=dy;
    uint32_t edx=R32(VA(0x4d2556u));
    rx2-=edx;ry2-=dy;
    /* Top-left corner: y low, y high, x low, x high (4314bc..4314ce). */
    if(s32(ry1)>=s32(vy1)&&s32(ry1)<=s32(vy2)&&s32(rx1)>=s32(vx1)&&s32(rx1)<=s32(vx2))return pair(1,edx);
    /* Bottom-right corner (4314d0..4314ea); EDX follows the loads. */
    edx=ry2;
    if(s32(ry2)<s32(vy1)||s32(ry2)>s32(vy2))return pair(0,edx);
    if(s32(rx2)<s32(vx1))return pair(0,edx);
    edx=rx2;
    return pair(s32(rx2)<=s32(vx2)?1u:0u,edx);
}
