/* Known differences from VSHELL's frames: one row per real case whose snap
 * has a frames.json reference and differs from it; the harness passes such
 * a snap as "known" while its first difference keeps the row's class and
 * fails a row whose difference is gone.
 *
 * Traps in the setup of the real cases (vshell_ui.md, "The tour's screen
 * differences closed"):
 * - frame history: VSHELL does not clear the frame when the main menu hands
 *   over, so the 371 index-0 pixels of the bay background (and the gaps of
 *   the other screens) keep the menu's pixels; the cases draw the menu
 *   first (es2_screen_main_menu_history);
 * - the clone's settings and saves: preferences.real_shown and
 *   saved_games.real_shown set the tour's PREFS.CFG bytes and GAMEFILE.STR
 *   rows in their setups;
 * - registration.real: the caret's 500 ms blink on a 100 ms clock per pass;
 * - weapons.real_shown, mission.real_page0: VSHELL's frames are compositions
 *   (the bay's tab bar, the career's squad panel and herc picture under the
 *   screen); //shell:gui_tour_real_test compares es2shell's whole frames
 *   with them on a career of its own, and the two cases keep their own
 *   hashes without a reference.
 *
 * The table is empty; the sentinel row is never read (the count is 0). */
#include "screen_harness.h"

#include <stddef.h>

const es2_screen_known es2_screen_known_differences[] = {
    {NULL, NULL, NULL, NULL},
};
const size_t es2_screen_known_difference_count = 0;
