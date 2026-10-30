#ifndef ES2_SHELL_GUI_CREW_H
#define ES2_SHELL_GUI_CREW_H

/* The CREW screen 0x441548 on the widget toolkit, an editor of the
 * service bay (es2/shell/gui/service_bay.h). Built once from the
 * layout table (screen "crew"); the state changes are the editors library
 * (es2/shell/editors.h: CREW_ORDER, CREW_PILOT, CREW_CLEAR and the bay
 * selector's BAY), which the host runs on its game state with the session
 * the service bay shares (es2_service_bay.session: screen word, bay
 * selection, order 0x477778, order pilots 0x47777c[3]).
 *
 *   show 0x442091          colours 0x441ee7, order pilots 0x442198, the three
 *                          group images (dba\c_pilots.dba), order rows
 *                          0x442215(0..3, 0), texts 0x4422df, every bay with
 *                          a record through 0x43dcdd, the frame shown
 *   leave 0x44218a         the frame hidden
 *   group images 0x4427e1, 0x44288c, 0x442937
 *                          colours, then the pilot list 0x442548(g)
 *   CLEAR 0x4429e2         0x442624
 *   order rows 0x442a40, 0x442aa1, 0x442b02, 0x442b63 (the row and its image)
 *                          0x442215(o)
 *   bay selector, CREW's part of 0x43dcdd with the selector flag
 *                          the assignment 0x4426e5 (the host's `selected`
 *                          hook of the service bay calls es2_crew_selected)
 *
 * Contract: docs/reverse-engineering/vshell_ui.md sections 17 and 32. No file
 * I/O: the host supplies fonts, bitmaps, texts, the team view and the
 * library call. */
#include <stdint.h>

#include "es2/shell/editors.h"
#include "es2/shell/gui/layout.h"
#include "es2/shell/gui/service_bay.h"

#define ES2_SCREEN_CREW_CONTROLS 40 /* crew.00 .. crew.39 */

/* crew.NN */
enum {
    ES2_CREW_FRAME = 0,            /* [0x48d80c] ESTitle, ESTEXT 0xa8 */
    ES2_CREW_GROUP = 2,            /* [0x48d810 + 4g] = crew.(2 + g), g = 0..2: ESBitmap */
    ES2_CREW_CLEAR = 5,            /* [0x48d81c] ESButtonFont, ESTEXT 0xa9 */
    ES2_CREW_CAPTION = 7,          /* [0x48d820] ESTEXT 0xab */
    ES2_CREW_ROW = 8,              /* [0x48d824 + 4o] = crew.(8 + o), o = 0..3: ESArm */
    ES2_CREW_IMAGE = 12,           /* [0x48d834 + 4o]: ESBitmap in the row (o = 0: dba\star.dba 0) */
    ES2_CREW_NAME_CAPTION = 16,    /* [0x48d844 + 4o]: ESTEXT 0xac */
    ES2_CREW_SKILL_CAPTION = 20,   /* [0x48d854 + 4o]: ESTEXT 0xad */
    ES2_CREW_VEHICLE_CAPTION = 24, /* [0x48d864 + 4o]: ESTEXT 0xae */
    ES2_CREW_NAME = 28,            /* [0x48d874 + 4o] */
    ES2_CREW_SKILL = 32,           /* [0x48d884 + 4o] */
    ES2_CREW_VEHICLE = 36          /* [0x48d894 + 4o] */
};

/* VSHELL addresses of the screen's handlers (what a press runs). */
enum {
    ES2_CREW_SHOW = 0x442091u,     /* `changed` after es2_crew_show */
    ES2_CREW_GROUP_0 = 0x4427e1u, ES2_CREW_GROUP_1 = 0x44288cu, ES2_CREW_GROUP_2 = 0x442937u,
    ES2_CREW_CLEAR_BUTTON = 0x4429e2u,
    ES2_CREW_ORDER_0 = 0x442a40u, ES2_CREW_ORDER_1 = 0x442aa1u, ES2_CREW_ORDER_2 = 0x442b02u,
    ES2_CREW_ORDER_3 = 0x442b63u
};

/* A pilot record as the screen reads it (memory +0 portrait word = the SAV
 * prefix's first word, +4 name, +0x22 bay, +0x25 skill word, +0x27 order). */
typedef struct es2_crew_pilot {
    int16_t portrait;   /* member of dba\c_pilots.dba */
    const char *name;
    int16_t bay;
    int16_t skill;      /* the word at +0x25 (ESTEXT 0x35 + skill) */
    int16_t order;
} es2_crew_pilot;

/* What the screen reads of the team 0x482b5c and the hangar. */
typedef struct es2_crew_team {
    int16_t capacity;          /* 0x482b5c */
    es2_crew_pilot player;     /* 0x482b60 */
    es2_crew_pilot group[3];   /* the selected pilot of each roster group ([0x482b9b + 4g]) */
    int16_t vehicle[8];        /* 0x411164: the record's vehicle word, -1 without a record */
} es2_crew_team;

/* The host's parts. Every member may be NULL. */
typedef struct es2_crew_host {
    void *ctx;
    /* Fonts by VSHELL global (0x46de00 the panel, 0x46ddfc CLEAR). */
    const es2_dfn *(*font)(void *ctx, uint32_t global);
    /* A member of "dba\\c_pilots.dba" or "dba\\star.dba". */
    const es2_bitmap *(*bitmap)(void *ctx, const char *file, int frame);
    /* An ESTEXT entry up to its NUL (NULL: the empty text). */
    const char *(*text)(void *ctx, uint32_t id);
    /* The current team view (0 = none: an empty team). */
    int (*team)(void *ctx, es2_crew_team *out);
    /* The editors library on the host's game state: es2_shell_editors with this
     * one operation and this session (in/out). ES2_OK when applied. */
    es2_status (*edit)(void *ctx, es2_shell_editor_session *session, es2_shell_editor_op *op);
    /* After every handler and after the show (with its VSHELL address). */
    void (*changed)(void *ctx, uint32_t handler);
} es2_crew_host;

typedef struct es2_crew {
    es2_service_bay *bay;      /* required: the flow, the shared session, the squad panel */
    es2_crew_host host;
    es2_ui *ui;
    es2_ui_id ids[ES2_SCREEN_CREW_CONTROLS];
    /* the last handler and library calls (tests, presenters) */
    uint32_t last_handler;
    unsigned edits, edit_failures;
    /* the library's session differed from the one the screen kept */
    unsigned mismatches;
} es2_crew;

/* The constructor 0x441548 (registration table): the layout table's controls,
 * the handlers bound to the screen. ctx = the es2_crew (bay and host set by the
 * caller). 0 = built, -1 = bad argument or the table could not be built. */
int es2_screen_crew_build(es2_ui *ui, void *ctx);

/* The show 0x442091 (the service bay's host calls it from screen(6, 1);
 * the screen word is already 6) and the leave 0x44218a (screen(6, 0)). */
void es2_crew_show(es2_crew *c);
void es2_crew_leave(es2_crew *c);
/* CREW's part of the squad panel's selection 0x43dcdd(k) with the selector
 * flag: the assignment 0x4426e5 (the service bay's host calls it from
 * selected(6, k, 1); the selection is already k). */
void es2_crew_selected(es2_crew *c, int16_t k);
/* One handler by its VSHELL address; 0 when it is not this screen's. */
int es2_crew_press(es2_crew *c, uint32_t handler);
/* 0x4422df: the texts and images of the four order rows. */
void es2_crew_texts(es2_crew *c);

#endif
