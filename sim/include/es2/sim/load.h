/* The guest callees of the mission-load root 0x461608 as own code
 * (mission_load.md, "the mission load in the own process"). The root itself and
 * the load pieces L1 ... L9 are registry hooks (core/src/mission_root.c and the
 * load groups); the root's guest calls that are not hooks (graphics context,
 * frame clear, launch peek, loading screen, palette, ...) are service rows of
 * sim/dispatch/services.h whose functions live in src/load.c. Guest state is
 * read and written only through the bound main loop's guest view; every call of
 * other DBSIM code goes through the dispatch.
 *
 * Headless the platform has no display: the GDI and USER calls the load makes
 * are counted here and answered as a display without a palette answers them
 * (the values a 32-bit desktop gives: GetSystemPaletteUse 0,
 * GetDeviceCaps(NUMCOLORS) -1, GetSystemPaletteEntries 0); the colour table the
 * original uploads with SetDIBColorTable is kept for the presentation package. */
#ifndef ES2_SIM_LOAD_H
#define ES2_SIM_LOAD_H
#include <stdint.h>
#include "es2/sim/loop.h"

typedef struct es2_sim_load_platform {
    /* Answers. */
    uint32_t palette_use;          /* GetSystemPaletteUse */
    uint32_t num_colors;           /* GetDeviceCaps(NUMCOLORS = 24); -1 on a true-colour desktop */
    uint32_t palette_handle;       /* CreatePalette's stand-in handle */
    uint32_t paint_dc;             /* BeginPaint's / GetDC's stand-in */
    /* Counts of the calls the own code made instead of the platform. */
    unsigned get_dc, release_dc, begin_paint, end_paint, invalidate, stretch_blt, select_palette, realize_palette;
    unsigned create_palette, delete_object, color_table_uploads, palette_use_calls, device_caps, palette_entries;
    unsigned presents;
    /* The last colour table SetDIBColorTable received (RGBQUAD, 256). */
    uint8_t color_table[1024];
} es2_sim_load_platform;

typedef struct es2_sim_load {
    es2_sim_loop *loop;
    es2_sim_load_platform platform;
    uint32_t refused_va;
} es2_sim_load;

/* Zeroes the record and sets the headless answers. */
void es2_sim_load_headless(es2_sim_load *load, es2_sim_loop *loop);
/* The instance the service rows use (one per process). */
void es2_sim_load_bind(es2_sim_load *load);
es2_sim_load *es2_sim_load_bound(void);

#endif
