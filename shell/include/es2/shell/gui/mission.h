#ifndef ES2_SHELL_GUI_MISSION_H
#define ES2_SHELL_GUI_MISSION_H

/* The mission screen 0x442bc4 (src\wmissini.cpp), pages 0, 1 and 4,
 * on the widget toolkit, a presenter of the shell flow. Built once from
 * the layout table (screen "mission", 55 controls) plus the five text
 * panels (ESREPORT objects 0x40d1bc, not in the table).
 *
 *   open 0x444873(page)       terradef into the Telecomm panel; page 0: the
 *                             theatre image (th_earth / th_moon), the
 *                             campaign text, the group and theatre movies,
 *                             the "%s %s" title; page 1: the three page-1
 *                             texts, Objectives / Intelligence / Rock & Roll
 *                             enabled, title 0xb3, the map animation pending,
 *                             the briefing movie; page 4: the debrief text,
 *                             Rock & Roll disabled, title 0xb9, the debrief
 *                             movie; then the panels shown, the page buttons
 *                             (not on page 0), the button row and the map
 *                             arrows (page 1 only), the text panel 0x445343
 *   leave 0x445095            images freed, statistics hidden, the four
 *                             page-1/4 texts freed, the panels hidden
 *   text panel 0x445343(p)    the previous panel 0x478140 hidden, panel p
 *                             shown on its current page, 0x478140 := p
 *   buttons 0x445a51/0x445ac7/0x445b30   Briefing (the page's panel),
 *                             Objectives (2), Intelligence (3), with the
 *                             frames 0x4452d9
 *   page buttons 0x445c79/0x445d2a       previous / next page of panels 1..4
 *   Rock & Roll 0x445b99      the flow's launch (checks, warning box n)
 *   Telecomm 0x4454b8         nothing (vshell_ui.md 14.4: the body is
 * unreachable)
 *   map arrows 0x445577..     the mission map (host hook)
 *
 * Contract: docs/reverse-engineering/vshell_ui.md sections 4.8, 14, 16 and
 * 33. The service bay shows and leaves the screen through its
 * host's screen(7, 1) / screen(7, 0): connect them to es2_mission_screen_show
 * (with flow->page) and es2_mission_screen_leave. No file I/O: the host
 * supplies fonts, images, ESTEXT, the composed texts (texts.h) and the
 * CAREER words; movies go into the movie queue (es2/shell/movies.h), which the
 * host's main loop plays. */
#include <stdint.h>

#include "es2/shell/flow.h"
#include "es2/shell/gui/layout.h"
#include "es2/shell/gui/service_bay.h"
#include "es2/shell/movies.h"
#include "es2/shell/texts.h"

#define ES2_SCREEN_MISSION_CONTROLS 55 /* mission.00 .. .54 */
#define ES2_MISSION_PANELS 5           /* the ESREPORT text panels */
#define ES2_MISSION_STATISTICS 20      /* 0x48d918 .. 0x48d964 */

/* mission.NN */
enum {
    ES2_MISSION_MAP_FRAME = 0,     /* [0x48d8b4] ESTitle 0xb0 (7, 43)-(275, 299) */
    ES2_MISSION_THEATRE = 2,       /* [0x48d8c0] ESBitmap, the whole display: the theatre picture */
    ES2_MISSION_TELECOMM = 3,      /* [0x48d8c4] ESBitmap of dba\terradef.dba, 0x4454b8 */
    ES2_MISSION_TITLE = 4,         /* [0x48d8b8] ESTitle, the page title (279, 43)-(632, 299) */
    ES2_MISSION_TEXT_FRAME = 6,    /* [0x48d8bc] ESTitle 0xaf, parent of the text panels */
    ES2_MISSION_ARROW = 8,         /* [0x48d8c8 + 4k] = mission.(8 + 2k), k = 0..5: the map arrows */
    ES2_MISSION_BUTTON_ROW = 20,   /* [0x48d8e0] ESRect (7, 433)-(632, 473) */
    ES2_MISSION_BRIEFING = 21,     /* [0x48d8e4] ESTEXT 0xb4, 0x445a51 */
    ES2_MISSION_OBJECTIVES = 23,   /* [0x48d8e8] 0xb6, 0x445ac7 */
    ES2_MISSION_INTELLIGENCE = 25, /* [0x48d8ec] 0xb7, 0x445b30 */
    ES2_MISSION_ROCK_AND_ROLL = 27,/* [0x48d8f0] 0xb8, 0x445b99 */
    ES2_MISSION_THEATRE_IMAGE = 29,/* [0x48d8f4] ESGrid: th_earth / th_moon on page 0 */
    ES2_MISSION_PALETTE = 30,      /* [0x48d8f8] ESPal (no use after construction) */
    ES2_MISSION_STATISTIC = 31,    /* mission.31 .. .50: the twenty statistics labels (vshell_ui.md 14.1, es2_mission_statistic) */
    ES2_MISSION_PAGE_PREVIOUS = 51,/* [0x48d910] 0x445c79 */
    ES2_MISSION_PAGE_NEXT = 53     /* [0x48d914] 0x445d2a */
};

/* The five text panels by 0x445343's argument. */
enum {
    ES2_MISSION_PANEL_CAMPAIGN = 0,     /* [0x48d8fc] (10, 21)-(613, 168) */
    ES2_MISSION_PANEL_BRIEFING = 1,     /* [0x48d900] (10, 21)-(575, 115), 0x46fc5c */
    ES2_MISSION_PANEL_OBJECTIVES = 2,   /* [0x48d908], 0x46fc60 */
    ES2_MISSION_PANEL_INTELLIGENCE = 3, /* [0x48d90c], 0x46fc64 */
    ES2_MISSION_PANEL_DEBRIEF = 4       /* [0x48d904], 0x46fc68 */
};

/* VSHELL handler and routine addresses. */
enum {
    ES2_MISSION_TELECOMM_HANDLER = 0x4454b8u,
    ES2_MISSION_BRIEFING_HANDLER = 0x445a51u,
    ES2_MISSION_OBJECTIVES_HANDLER = 0x445ac7u,
    ES2_MISSION_INTELLIGENCE_HANDLER = 0x445b30u,
    ES2_MISSION_PREVIOUS_HANDLER = 0x445c79u,
    ES2_MISSION_NEXT_HANDLER = 0x445d2au,
    ES2_MISSION_SHOW = 0x444873u,        /* `changed` after es2_mission_screen_show */
    ES2_MISSION_LEAVE = 0x445095u,       /* `changed` after es2_mission_screen_leave */
    ES2_MISSION_MUSIC_FADE_OUT = 0x42f7f0u, /* audio calls of the dropship step 0x44472f */
    ES2_MISSION_FOCUS_LOST = 0x42f6a8u
};

/* The window of the statistics label that VSHELL keeps at `global`
 * (0x48d918 .. 0x48d964, step 4); -1 for another address. */
int es2_mission_statistic(uint32_t global);

typedef struct es2_mission_screen_host {
    void *ctx;
    /* Fonts by VSHELL global (0x46de00 panels and labels, 0x46ddfc buttons). */
    const es2_dfn *(*font)(void *ctx, uint32_t global);
    /* Frame `frame` of a DBA: dba\miss_arw.dba (the arrows, the page
     * buttons), dba\terradef.dba 0, dba\th_earth.dba 0, dba\th_moon.dba 0
     * and the theatre pictures dba\alph2.dba, dba\delt1.dba, dba\omic1.dba,
     * dba\brav1.dba 0 (table 0x478068). */
    const es2_bitmap *(*bitmap)(void *ctx, const char *file, int frame);
    /* An ESTEXT entry up to its NUL (NULL: the empty text). */
    const char *(*text)(void *ctx, uint32_t id);
    /* The CAREER group word 0x46fc58. */
    int16_t (*group)(void *ctx);
    /* The page-0 text 0x40fd2d(group - 1) (es2_shell_campaign_text);
     * NULL where VSHELL's pointer is null (it faults in strlen: refused). */
    const char *(*campaign)(void *ctx, int16_t group);
    /* The text of panel 1..4 (the briefing 0x46fc5c, objectives
     * 0x46fc60, intelligence 0x46fc64, debrief 0x46fc68); NULL: refused. */
    const char *(*report)(void *ctx, int panel);
    /* The movie words: briefing 0x48419c and debrief 0x48419e. */
    void (*movie_words)(void *ctx, int16_t *briefing, int16_t *debrief);
    /* The mission map: a map arrow handler (0x445577, 0x445646, 0x445715,
     * 0x4457e4, 0x4458b3, 0x445982) on the map object [0x46f3a4]. */
    void (*map)(void *ctx, uint32_t handler);
    /* The audio calls of the dropship step (ES2_MISSION_MUSIC_FADE_OUT,
     * ES2_MISSION_FOCUS_LOST). */
    void (*audio)(void *ctx, uint32_t address);
    /* After every flow event and after the show and the leave. */
    void (*changed)(void *ctx, uint32_t handler, es2_shell_flow_result result);
} es2_mission_screen_host;

/* One ESREPORT object: the layout, the visible word +0x18, the current
 * page +0x1e, the window drawing it (a child of [0x48d8bc]). */
typedef struct es2_mission_panel {
    es2_ui_id window;
    es2_shell_report report;
    int has_text;
    int16_t visible;
    int16_t page;
} es2_mission_panel;

typedef struct es2_mission_screen {
    es2_shell_flow *flow;               /* required */
    es2_shell_movies *movies;           /* the movie queue and once-flags (NULL: no movies) */
    es2_service_bay *bay;               /* for es2_mission_screen_open and the dropship's palette 2 (may be NULL) */
    es2_mission_screen_host host;
    /* screen globals (vshell_state.md; image values set by the build) */
    int16_t panel;                      /* 0x478140, -1 */
    int16_t state;                      /* 0x477944: 1 page 0, 2 page 1, 0 left (no reader found) */
    uint8_t map_pending;                /* 0x46c269: main-loop step 6 runs the map animation */
    int16_t map_palette;                /* 0x46c26a, -1: the palette step 6 selects first */
    const es2_bitmap *terradef;         /* 0x47805c */
    const es2_bitmap *theatre_image;    /* 0x478060 (th_earth / th_moon) */
    const es2_bitmap *picture;          /* 0x478064 (the theatre picture) */
    /* filled by the build */
    es2_ui *ui;
    es2_ui_id ids[ES2_SCREEN_MISSION_CONTROLS];
    es2_mission_panel panels[ES2_MISSION_PANELS];
    /* the last flow event (tests, presenters) */
    uint32_t last_handler;
    es2_shell_flow_result last_result;
    unsigned events;
    unsigned refused;                   /* own refusals where VSHELL faults or asserts */
} es2_mission_screen;

/* The constructor 0x442bc4 (registration table): the layout table's controls
 * with the handlers bound, the five text panels hidden and empty; the globals
 * at their image values. ctx = the es2_mission_screen (flow and host set). 0 =
 * built, -1 = bad argument or the table could not be built. */
int es2_screen_mission_build(es2_ui *ui, void *ctx);
/* Releases the panels' layouts. */
void es2_mission_screen_release(es2_mission_screen *m);

/* 0x444873(page): pages 0, 1 and 4 as above; other pages only the common
 * part (the panels shown, page buttons and button row by the page). */
void es2_mission_screen_show(es2_mission_screen *m, int page);
/* 0x445095 (the leave 0x43a537 for word 7, the movie player's teardown). */
void es2_mission_screen_leave(es2_mission_screen *m);
/* 0x445343(p): the text panel. */
void es2_mission_screen_panel(es2_mission_screen *m, int panel);
/* 0x4452d9(p): the three button frames 0x22, the page's 0x20 (1 and 4
 * Briefing, 2 Objectives, 3 Intelligence). */
void es2_mission_screen_buttons(es2_mission_screen *m, int page);
/* 0x40d231(panel, text): the layout of a new text (visible 0, page 0, its
 * lines hidden). -1 refused (NULL text, no font, the breaker's assertion, a
 * line of 100 bytes or more), 0 done. */
int es2_mission_screen_set_text(es2_mission_screen *m, int panel, const char *text);
/* 0x40d7ed / 0x40d80e: previous / next page of a visible panel. */
void es2_mission_screen_page(es2_mission_screen *m, int panel, int next);
/* The ten setText calls of 0x40f904 (results path, step 9): texts from
 * es2_shell_debrief_texts_compose, align 0 for the first three and 1
 * for the rest, colour 0x29, redrawn. */
void es2_mission_screen_statistics(es2_mission_screen *m, const es2_shell_debrief_texts *texts);
/* 0x444fa4: the twenty statistics labels hidden. */
void es2_mission_screen_hide_statistics(es2_mission_screen *m);
/* 0x43aee7(page, click), the DEBUG dialog's path after a results path with
 * advance 2 (Use Default 0x44dbc4, ACCEPT 0x44dcca) once the bay is shown:
 * the page word := page, the leave of the current bay word (nothing for -1,
 * 0, 1), the bay palette 0x43b7f2(7), 0x444873(page), word 7 (the flow's
 * MISSION event) and with `click` a synthetic click posted to MISSION
 * (0x43af63..0x43afb2). Needs m->bay; refused (-1) without it or for a bay
 * word with an editor screen (no caller reaches that). */
int es2_mission_screen_open(es2_mission_screen *m, int page, int click);
/* 0x44472f, the movie player's theatre step: theatre 5 queues the dropship
 * (es2_shell_movies_theatre), the bay palette 2 (0x43a430(2)), music
 * fade-out and focus lost; returns 1. Otherwise the theatre picture of
 * 0x478068[group - 1] into [0x48d8c0], shown, palette 0xf + group - 1;
 * returns 0. -1 refused: group - 1 outside 0..4 (VSHELL reads past its
 * four-entry table). */
int es2_mission_screen_theatre(es2_mission_screen *m);
/* 0x44483e: the theatre picture hidden and freed. */
void es2_mission_screen_theatre_hide(es2_mission_screen *m);
/* One handler by its VSHELL address. Returns the flow's answer for Rock &
 * Roll, ES2_FLOW_DONE for the screen's own handlers, ES2_FLOW_IGNORED for
 * the Telecomm panel and unknown addresses. */
es2_shell_flow_result es2_mission_screen_press(es2_mission_screen *m, uint32_t handler);

#endif
