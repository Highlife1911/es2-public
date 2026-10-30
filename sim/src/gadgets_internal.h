/* The HUD gadgets' draws of src/gadgets.c, called by the service rows of
 * src/hud_draw.c that already serve their VAs. Private to the HUD files. Each
 * returns 0, or 1 when a call was refused. */
#ifndef ES2_SIM_GADGETS_INTERNAL_H
#define ES2_SIM_GADGETS_INTERNAL_H
#include "hud_internal.h"

int es2_sim_gadget_gunsight_fixed(hud_ctx *c, uint32_t d);   /* 0x43b808 */
int es2_sim_gadget_gunsight_target(hud_ctx *c, uint32_t d);  /* 0x43b978 */
int es2_sim_gadget_rotation_fixed(hud_ctx *c, uint32_t d);   /* 0x43b4cc */
int es2_sim_gadget_roving_gunsight(hud_ctx *c, uint32_t d);  /* 0x43c204 */
int es2_sim_gadget_nav_gauge(hud_ctx *c, uint32_t d);        /* 0x43c40c */

/* The radar page of src/panel.c, called by the same rows of hud_load.h (0x43f2d8,
 * 0x43e114, 0x43d5f0, 0x43eef4). */
int es2_sim_panel_hud_radar(hud_ctx *c, uint32_t r, uint32_t pt);  /* 0x43f2d8 */
int es2_sim_panel_radar_draw(hud_ctx *c, uint32_t r);              /* 0x43eef4 */

/* The bar draws (kind 0: 0x439118, 1: 0x439398, 2: 0x4395e8) and the shields
 * gauge's slot +4 0x443758 of src/panel.c, called by rows of hud_load.h; each leaves
 * the original's EAX in *eax. */
int es2_sim_panel_bar_draw(hud_ctx *c, uint32_t b, uint32_t value, unsigned kind, uint32_t *eax);
int es2_sim_panel_shields_draw(hud_ctx *c, uint32_t g, uint32_t *eax);  /* 0x443758 */

#endif
