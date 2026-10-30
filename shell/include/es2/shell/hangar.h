#ifndef ES2_SHELL_HANGAR_H
#define ES2_SHELL_HANGAR_H

#include "es2/shell/campaign.h"

/* Service bay, WEAPONS screen (vshell_ui.md sections 4.6, 4.7 and 10).
 * The SAV describes VSHELL's memory state, as es2_shell_prepared_save and
 * es2_shell_loaded_save produce it: every item list in list order (head
 * first), which is the order the original writers emit. The operations
 * change that state exactly as the named VSHELL functions do; the result is
 * the SAV the original writers produce afterwards (stale tail dropped). No
 * file I/O; outputs are unchanged on failure. */
enum {
    /* 0x411b10(bay record, slot, item): the slot's item, if any, goes back to
     * the head of its ID's list (occupied count - 1); item 0 empties the
     * slot and sets its condition word (23 + slot) to 100; any other item
     * takes the head of its list, the condition word becomes the item's word
     * +4, the occupied count + 1 and the item's word +8 becomes 1 for IDs
     * 13..15 and 5 for every other ID; an empty list leaves the slot empty.
     * The bay must hold a Herc and the slot be below its capacity. */
    ES2_SHELL_BAY_MOUNT = 1,
    /* A click on the weapon-list row of item `value` with slot `slot` of bay
     * `bay` selected (0x440990.. -> 0x43fdac -> 0x43e2d4): nothing for a bay
     * without a Herc; nothing for a disabled row (availability byte of the
     * item zero, or `admitted` zero: the ARM table of the bay's vehicle has
     * no record for this item and slot, 0x440256/0x41501f); otherwise the
     * mount above when the item is 0, or the slot item's word +8 (5 for an
     * empty slot) equals the item ID (sic, 0x43fe39), or the item's list is
     * not empty, or the slot already holds that ID. `applied` tells whether
     * the mount ran. */
    ES2_SHELL_BAY_CLICK,
    /* A guidance button (SARH 0, ARH 1, ARM 2, EO 3: 0x4406f0, 0x44068b,
     * 0x440626, 0x440755 -> 0x4403f9 -> 0x43e346): the slot item's word +8
     * becomes `value`. The panel exists for the list rows 13..16
     * (0x440169..0x44018d); the operation is refused unless the slot holds
     * one of these items and value is 0..3. */
    ES2_SHELL_BAY_GUIDANCE,
    /* 0x412a37 with the weapons-building option `value` (prefs.cfg +0x2d):
     * run when WEAPONS, REPAIR or BUILD is left (0x43a537) and after a
     * weapons scrap. Option 0 refills the five-entry build bank by priority
     * for categories with fewer than two items the salvage covers; any other
     * value drops bank entries the salvage cannot pay (the results path's
     * step, vshell_preparation.md "Results path"). Needs vfs and
     * tables. */
    ES2_SHELL_BAY_REBALANCE
};
typedef struct es2_shell_bay_op {
    int kind;
    int16_t bay, slot;   /* bay 0..7, slot 0..9 (MOUNT, CLICK, GUIDANCE) */
    int16_t value;       /* item 0..32, mode 0..3 or option byte 0..255 */
    int admitted;        /* CLICK: ARM answer for the bay's vehicle, item and slot */
    int applied;         /* output: 1 when the state changed hands (CLICK may give 0) */
} es2_shell_bay_op;

/* Applies count operations in order to `save` and returns the SAV image in
 * *out (must be empty; free its data). vfs and tables
 * (ES2_SHELL_RESULTS_TABLES_SIZE bytes, as es2_shell_results) are needed
 * only for REBALANCE. On failure *out and every `applied` stay unchanged and
 * the error names the failing operation's index in error->offset for
 * argument errors. */
es2_status es2_shell_service_bay(const es2_vfs *vfs, es2_span tables, es2_span save,
    es2_shell_bay_op *ops, size_t count, es2_shell_buffer *out, es2_error *error);

#endif
