/* The CODE read sites of es2/sim/image_access.h, in both builds. */
#include "es2/sim/image_access.h"

/* dbsim_gap.md, "What `es2sim` takes from `DBSIM.EXE`" (grep of sim/src and core/src, each site read). */
const uint32_t es2_sim_image_table_sites[ES2_SIM_IMAGE_TABLE_SITES] = {
    0x004100fdu,   /* core/src/mission_load.c: index bytes of a jump table */
    0x00405366u,   /* core/src/mission_load.c: case bytes */
    0x00426492u,   /* core/src/mission_load.c: type-word bytes */
    0x00436be2u,   /* sim/src/hud_widgets.c: index bytes for codes 0..0x38 */
    0x0044bb9fu,   /* sim/src/hud_draw.c: index bytes by kind */
    0x0044e1f4u,   /* sim/src/hud_draw.c: a 29-row byte table */
    0x004967aeu,   /* sim/src/input.c, services_files.c: a dword of an import thunk */
};
