#ifndef ES2_SHELL_EDITORS_H
#define ES2_SHELL_EDITORS_H

#include "es2/shell/campaign.h"

/* Service-bay editors (vshell_ui.md section 12): the REPAIR screen
 * (0x4326c7), the BUILD screen (0x445de8) and the Herc scrap (warning box
 * 0x4479b8, ACCEPT 0x448326 -> 0x40ed0f), with the rebalance 0x412a37 on
 * leaving either screen.
 *
 * The SAV describes VSHELL's memory state as for es2_shell_service_bay
 * (hangar.h): every item list in list order, head first. The session holds
 * the shell globals these screens keep between button presses; it is in/out
 * so that a caller can apply operations one call at a time. Operations are
 * the buttons and list rows; a press on a control the original shows
 * disabled (or that changes nothing) is not an error: its `applied` is 0.
 * No file I/O; on failure the session, *out, *view and every `applied` stay
 * unchanged and error->offset names the failing operation's index. */

/* Executable tables of the editors, VSHELL.EXE's bytes read at run time from
 * the span store (vshell_store.h), concatenated in this order: the five band
 * limits of a single repair (5 words, VA 0x47665c), the vehicle IDs of the
 * HERC_INF records (9 words, 0x46f868), the hardpoint capacity per record (9
 * words, 0x46f87a), the weapon category of each ARMORY row (26 words,
 * 0x476a4c). */
#define ES2_SHELL_EDITOR_TABLES_SIZE 98u
#define ES2_SHELL_ARMORY_ROWS 26

/* Screen word 0x4758b8 (vshell_ui.md section 1): the service-bay screen shown
 * last. The bar buttons store it after the screen's show routine; leaving a
 * screen does not reset it. */
enum {
    ES2_SHELL_SCREEN_BAY = -1, ES2_SHELL_SCREEN_MAIN = 0, ES2_SHELL_SCREEN_SAVE = 1,
    ES2_SHELL_SCREEN_WEAPONS = 2, ES2_SHELL_SCREEN_REPAIR = 3, ES2_SHELL_SCREEN_BUILD = 4,
    ES2_SHELL_SCREEN_ARMORY = 5, ES2_SHELL_SCREEN_CREW = 6, ES2_SHELL_SCREEN_MISSION = 7
};

typedef struct es2_shell_editor_session {
    int16_t screen;          /* screen word 0x4758b8 */
    int16_t bay;             /* bay selection 0x482bc9 (hangar +0x22; -1 none) */
    int16_t tab, row;        /* REPAIR list selection 0x474094 (0 External, 1 Internal), 0x474098 */
    int16_t family, index;   /* its condition family and index 0x474096, 0x47409a */
    uint32_t salvage;        /* REPAIR snapshot of the salvage total 0x48d33c */
    int16_t conditions[33];  /* REPAIR snapshot of the bay's 33 condition words 0x48d340 */
    int16_t herc_type;       /* BUILD row selection 0x478780 (-1 in the image) */
    /* CREW and ARMORY (vshell_ui.md section 17). */
    int16_t order;           /* CREW order row 0x477778 (0 the player, 1..3 the wingmen; 0 in the image) */
    int16_t crew[3];         /* CREW pilot of orders 1..3 (0x47777c[3]) as the roster group 0..2 whose
                              * selected pilot it is, -1 for none (the image) */
    int16_t armory_row;      /* ARMORY row selection 0x479210 (-1 in the image and after leaving) */
    int16_t building;        /* the weapons-building option byte 0x4825c9 (prefs.cfg +0x2d) that the
                              * ARMORY controls and the weapons scrap read: 0 auto, else manual. The
                              * caller keeps it equal to the byte it passes as b to OPEN and CLOSE. */
} es2_shell_editor_session;

/* The image values (bay screen just shown: screen word -1) with the bay
 * selection `bay` (-1..7) the caller's bay screen holds. */
void es2_shell_editor_session_init(es2_shell_editor_session *session, int16_t bay);

enum {
    /* A bar button of an editor (REPAIR 0x43a962, BUILD 0x43aa62, WEAPONS
     * 0x43a862, ARMORY 0x43ab62, CREW 0x43ac5a), a = 2..6, b = the
     * weapons-building option byte (prefs.cfg +0x2d) for leaving the
     * previous screen. Nothing when that screen is already shown. Otherwise
     * the previous screen is left (0x43a537; WEAPONS, REPAIR, BUILD:
     * 0x412a37(b); ARMORY 0x449c46: row -1; the others change no SAV state),
     * then the show: REPAIR 0x43397c and WEAPONS 0x43fbd8 keep the bay
     * selection when that bay holds a finished Herc (0x411088), else run the
     * squad panel's selection 0x43dcdd(0x41124e) in the case of the previous
     * screen (after CREW: not a vehicle-8 bay while a wingman order is
     * selected); REPAIR then takes the snapshot (0x433f86) and selects list
     * row (0, 0); BUILD 0x446f9d selects Herc type 0 (0x4472cb(0), even when
     * that row is disabled); ARMORY 0x449b87 selects row 0 (0x449d2f(0)); CREW
     * stores the screen word first, then shows (0x442091: the order pilots
     * 0x442198, the order rows 0x442215(0..3, 0), every bay with a record
     * through 0x43dcdd). The WEAPONS screen's own controls are
     * es2_shell_service_bay's. */
    ES2_SHELL_EDITOR_OPEN = 1,
    /* Another bar button while an editor is shown (0x43a537): a = the next
     * screen word (-1, 0, 1, 2, 5, 6, 7), b = the option byte for 0x412a37.
     * The next screen's show is the caller's. */
    ES2_SHELL_EDITOR_CLOSE,
    /* A bay selector of the squad panel (0x43dcdd), a = bay 0..7, on WEAPONS,
     * REPAIR, BUILD or CREW. Nothing when it is already selected. WEAPONS:
     * only a bay with a finished Herc. REPAIR: the same; the snapshot is
     * taken again and row (0, 0) selected. BUILD: any bay. CREW: a
     * finished Herc, not of vehicle 8 while a wingman order (1..3) is
     * selected; then the assignment 0x4426e5 (see CREW_ORDER). */
    ES2_SHELL_EDITOR_BAY,
    /* REPAIR list row (handlers 0x434745.. -> 0x434549(tab, row)): a = tab
     * (0 External rows 0..15, 1 Internal rows 0..8), b = row. External rows
     * 0..5 are the part groups (family 0), rows 6..15 the hardpoints 0..9
     * (family 2, only below the capacity and holding an item); Internal rows
     * are condition words 13..21 (family 1). */
    ES2_SHELL_EDITOR_REPAIR_PICK,
    /* REPAIR 0x4351bd: the selected item's cost 0x411a78 is paid and its
     * condition raised to the next band (0x43e05f, 0x413e5c). Enabled (0x433ad5)
     * when salvage - bank cost (0x412baa) >= cost, unsigned. */
    ES2_SHELL_EDITOR_REPAIR,
    /* REPAIR ALL 0x4352e9: salvage -= 0x41401b(L, 100), 0x4119d3(L, 100).
     * Enabled when salvage - bank cost >= that cost, unsigned. */
    ES2_SHELL_EDITOR_REPAIR_ALL,
    /* CANCEL 0x435403: salvage and the selected bay's 33 condition words from
     * the snapshot. Always enabled. */
    ES2_SHELL_EDITOR_REPAIR_CANCEL,
    /* BUILD type row (0x44764f.. -> 0x4472cb(a)), a = 0..8; rows whose
     * nine-word bank entry is 0 are disabled (0x446ec5). */
    ES2_SHELL_EDITOR_BUILD_TYPE,
    /* Build 0x4475ce -> 0x40eed4(type): a new record in the selected bay
     * (0x410fa6, 0x41163d) and salvage -= its cost. Enabled (0x447064) when
     * the bay holds no record and salvage - bank cost > HERC_INF[type] +8 *
     * 1000, unsigned. */
    ES2_SHELL_EDITOR_BUILD,
    /* Scrap (REPAIR 0x4353a5, BUILD 0x447570) and the warning's ACCEPT
     * 0x448326 -> 0x40ed0f: salvage += 0x410f46 (value 0x414174, items with
     * condition >= 80 back to the inventory), the Herc removed, its pilot's
     * bay -1 and the pilot of that order deactivated (0x41094b). Enabled
     * when the bay holds a record, not exactly one Herc of the hangar is
     * functional (0x411101) and the nine-word bank entry of its vehicle is not
     * 0. In REPAIR the first bay with a finished Herc is selected after it
     * (0x43dcdd(0x41124e)). The warning's CANCEL changes nothing. */
    ES2_SHELL_EDITOR_SCRAP,
    /* ARMORY (screen 5). A row press, a = row 0..25 (weapon category
     * of the row from 0x476a4c), b = 1 for the left button (0x449d2f), other
     * values for the other button (0x44a06e). Rows whose category flag is 0
     * are disabled (0x4499b9). Left: with weapons building manual
     * (building != 0) on the selected row, the category is added to the
     * first free entry of the five-entry build bank (0x412c0b) when the
     * bank's free count is not 0 and bank cost (0x412baa) + the category's
     * cost < salvage, unsigned; otherwise the row is selected. Other: with
     * manual building on the selected row, every entry of the category is
     * removed (0x412c31) and one fewer added again through the left-button
     * rule; otherwise the row is selected. */
    ES2_SHELL_EDITOR_ARMORY_ROW,
    /* Clear 0x44a584 -> 0x449ccc: every bank entry of the selected row's
     * category becomes 0, the free count + 1 each (0x412c31). The button is
     * enabled only with manual building (0x449b87). */
    ES2_SHELL_EDITOR_ARMORY_CLEAR,
    /* Scrap 0x44a508 (with a row selected) and the weapons scrap warning's
     * ACCEPT 0x448447: salvage += 0x412b79(category) = 1000 * the warning's
     * number 0x412c8e (0 without items, else max(1, cost / 1000 * count /
     * 10) in 16 bits), every item of the category freed (0x4125c1), then
     * 0x412a37(building). The warning's CANCEL changes nothing. */
    ES2_SHELL_EDITOR_ARMORY_SCRAP,
    /* CREW (screen 6). An order row 0x442215(a), a = 0..3 (0 the
     * player, 1..3 the wingman orders): the squad panel's selection
     * 0x43dcdd(bay of that order's pilot, -1 without one; the player's bay
     * for 0) without the assignment, decided with the order selected before,
     * then the order is selected. */
    ES2_SHELL_EDITOR_CREW_ORDER,
    /* A pilot list 0x442548(a), a = roster group 0..2 (its selected pilot),
     * with a wingman order selected (nothing for order 0): the pilot that
     * had the order (0x4108fa) loses it (order -1, bay and activity kept),
     * pilot a gets it (0x4108e2) and becomes the order's pilot, then the
     * assignment 0x4426e5: the bay selected is taken from the player and
     * from every selected pilot that has it (0x40ec8f(g, -1): bay -1,
     * deactivated), then given to the player (order 0) or to the order's
     * pilot, whose activity follows 0x41098a(order): active when the order
     * is below the team capacity and the pilot has a bay. */
    ES2_SHELL_EDITOR_CREW_PILOT,
    /* CLEAR 0x4429e2 -> 0x442624, with a wingman order selected: the pilot
     * that has the order (0x4108fa) gets bay -1 and is deactivated, loses the
     * order, the order has no pilot any more, and the bay selection becomes
     * -1 (0x43dcdd(-1)). */
    ES2_SHELL_EDITOR_CREW_CLEAR
};
typedef struct es2_shell_editor_op {
    int kind;
    int16_t a, b;
    int applied;   /* output: 1 when the operation changed the session or the state */
} es2_shell_editor_op;

/* What the two screens show and decide on for the state and session after
 * the operations (0x433ad5, 0x447064, 0x447501). Without a bay selection the
 * original reads a selected-pilot pointer as a hangar record; here every
 * value of the selected bay is then 0 and every button disabled. */
typedef struct es2_shell_editor_view {
    uint32_t salvage, bank_cost;   /* 0x482bd8, 0x412baa */
    int16_t first_finished;        /* 0x41124e: first bay with a finished Herc, -1 */
    int one_functional;            /* 0x411101 */
    int has_record;                /* the selected bay holds a record */
    int16_t condition;             /* 0x41232a of the REPAIR row (100 without a record) */
    int32_t item_cost, all_cost;   /* 0x411a78 of the REPAIR row, 0x41401b(L, 100) */
    int32_t scrap_value;           /* 0x414174 (the warning shows it / 1000) */
    int16_t scrap_bank;            /* nine-word bank entry of the bay's vehicle (-1 without a record) */
    int32_t build_cost;            /* HERC_INF[herc type] +8 * 1000 (0 for type -1) */
    int repair_enabled, repair_all_enabled, scrap_enabled, build_enabled;
    /* ARMORY (the selected row; category -1 and values 0 without one) */
    uint32_t armory_rows;          /* bit r: row r enabled (category flag, 0x4499b9) */
    int armory_clear_enabled;      /* manual building (0x449b87) */
    int16_t armory_category;       /* 0x476a4c[row] */
    int16_t armory_items;          /* the category's item count (+0x17) */
    int16_t armory_in_bank;        /* bank entries of the category (0x412c66) */
    int16_t armory_free;           /* free count of the five-entry bank (0x46fa14) */
    int32_t armory_scrap_value;    /* 0x412b79: salvage a weapons scrap would add */
} es2_shell_editor_view;

/* Applies count operations in order. tables: ES2_SHELL_RESULTS_TABLES_SIZE
 * bytes as for es2_shell_results; editor_tables: ES2_SHELL_EDITOR_TABLES_SIZE
 * bytes; vfs: GAM/HERC_INF.DAT, GAM/DAMAGE.DAT, GAM/WEAPONS.DAT. *out (must
 * be empty; free its data) receives the SAV, *view (optional) the view of the
 * end state. Refused (ES2_ERR_ARGUMENT): unknown kinds, values out of range,
 * an operation of a screen that is not shown, CLOSE to 3/4. Refused because
 * the original would read outside its objects (ES2_ERR_OFFSET): REPAIR,
 * REPAIR ALL, CANCEL, Build and Scrap without a bay selection, a hardpoint
 * row without one, Build of a type outside the vehicle table, an ARMORY row
 * of a category outside 0..32. ES2_ERR_UNSUPPORTED: a part group without its
 * end mark (as es2_shell_results). */
es2_status es2_shell_editors(const es2_vfs *vfs, es2_span tables, es2_span editor_tables, es2_span save,
    es2_shell_editor_session *session, es2_shell_editor_op *ops, size_t count, es2_shell_buffer *out,
    es2_shell_editor_view *view, es2_error *error);

#endif
