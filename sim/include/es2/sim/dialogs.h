/* The modal dialogs of DBSIM (main_loop.md, "the main loop, pump, window
 * procedure, pause, limiter and clock" and "the modal dialogs in the own
 * process"): the dialog open and close,
 * the modal loops of the mission-end alert and of the F11 options dialog with
 * their control-record pass, the key handler, focus and hit test, the
 * pressed-gadget release list, the port pause and resume around a dialog and
 * the prefs toggles, as service rows of sim/dispatch/services.h
 * (src/dialogs.c). Headless: no window; a pass's input is the pump 0x464c78
 * over the input journal (the pump-keyed entries) and the control record
 * 0x45a810 called through the dispatch; the clock 0x467828 is called at the
 * original's sites in the original order and traced in the bound es2_sim_hud;
 * the pixel work of the dialog frame and the cursor placement are not done
 * (counted in `headless`). */
#ifndef ES2_SIM_DIALOGS_H
#define ES2_SIM_DIALOGS_H
#include <stdint.h>
#include "es2/sim/hud.h"

typedef struct es2_sim_dialogs {
    /* Modal passes run (one control record each) by every modal loop. */
    unsigned passes;
    /* 0: none. Else a modal loop that runs this many passes without ending
     * is refused (a guard for headless runs without input; the original
     * waits for input forever). */
    unsigned pass_limit;
} es2_sim_dialogs;

/* Zeroes the record. */
void es2_sim_dialogs_init(es2_sim_dialogs *d);
/* The instance the modal loops count in. Unbound, the rows use an own
 * record without a limit. */
void es2_sim_dialogs_bind(es2_sim_dialogs *d);

#endif
