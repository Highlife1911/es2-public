#ifndef ES2_SHELL_TESTS_SCREEN_HARNESS_H
#define ES2_SHELL_TESTS_SCREEN_HARNESS_H

/* The shell screen test harness: the own shell (es2/shell/gui/loop.h)
 * on a scripted platform: window messages from a script, clicks by control
 * name from the layout table (shell/data/vshell_layout.json), keys and
 * text through VSHELL's input path as the own shell models it, one hash record
 * per present and per snap; comparison with the capture tour's reference hashes
 * (frames.json of es2-data/logs/vshell-screens-<stamp>); a difference is
 * reported as the first differing rectangle and a class, never as pixels.
 * See shell/README.md, section "Verification". */
#include "es2/shell/gui/loop.h"

#include <stddef.h>
#include <stdint.h>

/* ---- layout table (schema 1) ---------------------------------------- */

typedef struct es2_screen_control {
    char id[48];           /* "<screen>.<NN>" */
    char cls[24];          /* RTTI class, e.g. "ESButtonFont" */
    char owner[48];        /* "" when the control has no owner */
    int has_rect;          /* screen_rect is not null */
    es2_gui_rect rect;     /* screen_rect (left, top, right, bottom) */
    int text;              /* the control carries a font (args.font): its pixels are glyphs */
    int clickable;         /* no owner and a non-zero handler (vshell_widgets.md 4.5) */
} es2_screen_control;

typedef struct es2_screen_layout {
    es2_screen_control *controls;
    size_t count;
} es2_screen_layout;

/* Parses the table text / reads and parses the file. 0 = ok; else -1 and a
 * message in err. */
int es2_screen_layout_parse(const char *text, size_t length, es2_screen_layout *out, char *err, size_t err_size);
int es2_screen_layout_load(const char *path, es2_screen_layout *out, char *err, size_t err_size);
void es2_screen_layout_free(es2_screen_layout *layout);
const es2_screen_control *es2_screen_layout_find(const es2_screen_layout *layout, const char *id);
/* the capture tour's click point: floor((left + right) / 2), floor((top + bottom) / 2)
 * of screen_rect (capture-vshell-screens.ps1, Get-ControlPoints). 0 when the
 * control has no screen_rect. */
int es2_screen_control_centre(const es2_screen_control *control, int32_t *x, int32_t *y);

/* ---- references (capture-tour frames.json, schema 1)
 * ------------------------------ */

typedef struct es2_screen_reference {
    char key[160];             /* "<run>:<screen>:<state>" */
    char run[48];
    int step;
    char index_sha256[65];     /* SHA-256 of the 640 x 480 indices */
    char palette_sha256[65];   /* SHA-256 of 256 RGB triples; "" when null (no upload yet) */
} es2_screen_reference;

typedef struct es2_screen_references {
    es2_screen_reference *items;
    size_t count;
    char session[512];         /* directory of frames.json ("" for parsed text) */
} es2_screen_references;

int es2_screen_references_parse(const char *text, size_t length, es2_screen_references *out, char *err,
                                size_t err_size);
/* path = frames.json; session := its directory. */
int es2_screen_references_load(const char *path, es2_screen_references *out, char *err, size_t err_size);
void es2_screen_references_free(es2_screen_references *refs);
/* First reference with this key (a repeated key keeps its first snap). */
const es2_screen_reference *es2_screen_references_find(const es2_screen_references *refs, const char *key);
/* Where the references are: ES2_VSHELL_SCREENS (a session directory or a
 * frames.json), else the newest <es2-data>/logs/vshell-screens-* holding a
 * frames.json, with <es2-data> = ES2_DATA_REPO, else ES2_DATA_ROOT's
 * grandparent (an ES2_DATA_ROOT naming es2-data's extracted/cd, the older
 * layout). Returns 1 and the frames.json path, 0 when none is
 * configured or found. */
int es2_screen_references_locate(char *path, size_t size);

/* ---- difference ------------------------------------------------------------ */

typedef enum es2_screen_class {
    ES2_SCREEN_SAME,
    ES2_SCREEN_PALETTE,   /* indices equal, colour table differs */
    ES2_SCREEN_GEOMETRY,  /* the differing block equals the expected one moved by up to 8 pixels
                             (blocks of at least 16 pixels) */
    ES2_SCREEN_FONT,      /* the difference starts inside a control that carries a font */
    ES2_SCREEN_STATE,     /* any other difference of the indices */
    ES2_SCREEN_UNKNOWN,   /* hashes differ, no reference pixels to compare with */
} es2_screen_class;

const char *es2_screen_class_name(es2_screen_class c);

typedef struct es2_screen_diff {
    es2_screen_class cls;
    es2_gui_rect rect;          /* the first differing rectangle (inclusive) */
    char control[48];           /* the smallest control of the screen containing its first pixel, or "" */
    int table_differs;          /* also with an index difference */
    int first_entry, last_entry; /* differing colour-table entries (palette class) */
    size_t pixels;              /* number of differing indices in the frame */
} es2_screen_diff;

/* Compares two frames. The first differing pixel in row order is seeded and
 * grown to the bounding box of its 8-connected region of differing pixels.
 * Classes in this order: palette, geometry, font, state. layout and screen
 * (the layout table's screen name, e.g. "main_menu") may be NULL. */
void es2_screen_compare(const uint8_t expected[ES2_GUI_PIXELS], const uint8_t expected_table[256][3],
                        const uint8_t actual[ES2_GUI_PIXELS], const uint8_t actual_table[256][3],
                        const es2_screen_layout *layout, const char *screen, es2_screen_diff *out);
/* One line, never pixel values or colours: "class=font rect=(l,t,r,b) control=... pixels=N". */
void es2_screen_diff_format(const es2_screen_diff *diff, char *line, size_t size);

/* ---- session: one shell driven by a script -------------------------------- */

typedef struct es2_screen_snap {
    char label[48];
    unsigned pass;
    char frame[65];   /* SHA-256 of the indices (= the capture tour's index_sha256) */
    char table[65];   /* SHA-256 of 256 RGB triples (= the capture tour's palette_sha256) */
    char screen[65];  /* SHA-256 of the presented screen (headless form) */
    uint8_t *pixels;  /* copy of the indices */
    uint8_t rgb[256][3];
} es2_screen_snap;

typedef struct es2_screen_present {
    unsigned pass, seq;
    es2_gui_present_kind kind;
    char frame[65], table[65];
} es2_screen_present;

typedef struct es2_screen_session {
    es2_gui_shell *shell;            /* the shell under test */
    const es2_screen_layout *layout;
    es2_gui_hooks screen;            /* the screen's hooks (context = the case state) */
    unsigned pass;
    es2_screen_snap *snaps;
    size_t snap_count, snap_capacity;
    es2_screen_present *presents;
    size_t present_count, present_capacity;
    es2_gui_event *events;           /* every drained event, in order */
    size_t event_count, event_capacity;
    char error[256];                 /* first script error */
    int errors;
    /* platform internals */
    struct es2_screen_message *pending;
    size_t pending_count, pending_capacity;
    int held_left, held_right;
} es2_screen_session;

/* A fresh focused shell (args may be NULL) whose events go to screen->handle
 * after the harness recorded them. 0 = ok, -1 out of memory. */
int es2_screen_session_open(es2_screen_session *s, const es2_screen_layout *layout, const es2_gui_args *args,
                            const es2_gui_hooks *screen);
void es2_screen_session_close(es2_screen_session *s);
/* Runs script lines (see README); returns the number of bad lines. Verbs:
 *   step [N] | move X Y | ldown | lup | rdown | rup
 *   key NAME [shift] [alt] [ctrl] | keydown ... | keyup ...   (headless.h's key names)
 *   focus 0|1 | paint | close | quit | interactive 0|1 | movie 0|1
 *   click CONTROL [right]   the capture tour's click form at the control's
 * centre:
 *                           moves to (639,479), (0,0), centre; 2 passes;
 *                           button down; 2 passes; button up; 1 pass
 *   press NAME [mods]       the capture tour's key form: down; 2 passes; up; 1
 * pass
 *   type TEXT               press for each character (the rest of the line);
 *                           the key and Shift that give it in VSHELL's tables
 *   snap LABEL              hash the frame now (no present) */
int es2_screen_run(es2_screen_session *s, const char *script);
const es2_screen_snap *es2_screen_find_snap(const es2_screen_session *s, const char *label);

/* ---- cases and suites ------------------------------------------------------ */

typedef struct es2_screen_expect {
    const char *snap;       /* snap label */
    const char *frame;      /* expected index hash, NULL = not checked */
    const char *table;      /* expected RGB table hash, NULL = not checked */
    const char *reference;  /* "<run>:<screen>:<state>" in frames.json, NULL = none (real cases) */
} es2_screen_expect;

/* A classified difference from the reference that is still open
 * (screen_known.c, currently empty). It passes as "known" when the first difference
 * has this class (or cannot be classified without the dumped frame); another
 * class, or no difference at all, fails. */
typedef struct es2_screen_known {
    const char *name;       /* case name */
    const char *snap;       /* snap label */
    const char *reference;  /* frames.json key */
    const char *cls;        /* es2_screen_class_name of the first difference */
} es2_screen_known;

extern const es2_screen_known es2_screen_known_differences[];
extern const size_t es2_screen_known_difference_count;
/* Tests swap the table; (NULL, 0) restores screen_known.c's. */
void es2_screen_known_use(const es2_screen_known *table, size_t count);

typedef struct es2_screen_case {
    const char *name;
    const char *screen;     /* screen name for control lookups in diffs */
    int needs_data;         /* runs only with ES2_DATA_ROOT (data_root below) */
    size_t state_size;      /* zeroed state handed to setup and the hooks */
    /* Draws the screen into s->shell and fills s->screen (hooks with
     * context = state); 0 = ok, else the case fails with that code. */
    int (*setup)(es2_screen_session *s, void *state, const char *data_root);
    const char *script;
    const es2_screen_expect *expect;
    size_t expect_count;
    /* Optional: draws what a snap should show (fixture cases); used to
     * classify a hash mismatch. Return 0 when there is no picture. */
    int (*expected_picture)(const char *snap, uint8_t pixels[ES2_GUI_PIXELS], uint8_t table[256][3]);
} es2_screen_case;

typedef struct es2_screen_suite {
    const char *name;
    const es2_screen_case *cases;
    size_t count;
    struct es2_screen_suite *next;
} es2_screen_suite;

void es2_screen_register(es2_screen_suite *suite);
/* The registered suites, sorted by name (registration order is link order). */
size_t es2_screen_suites(const es2_screen_suite **out, size_t max);

/* The one line a screen file shell/tests/screen_<name>.c ends with. */
#define ES2_SCREEN_SUITE(name, cases)                                                                           \
    static es2_screen_suite es2_screen_suite_##name = {#name, cases, sizeof(cases) / sizeof((cases)[0]), 0};     \
    __attribute__((constructor)) static void es2_screen_register_##name(void) {                                 \
        es2_screen_register(&es2_screen_suite_##name);                                                          \
    }

typedef struct es2_screen_totals { unsigned passed, failed, skipped; } es2_screen_totals;

/* Runs one case: setup, script, expectations, references (refs may be NULL:
 * reference checks print SKIP). Prints one line per snap and per result. */
void es2_screen_run_case(const es2_screen_case *c, const es2_screen_layout *layout, const es2_screen_references *refs,
                         const char *data_root, es2_screen_totals *totals);

/* ---- frame history ---------------------------------------------------- */

/* VSHELL does not clear the frame when the main menu hands over to the next
 * screen (CONTINUE GAME, SAVE/RESTORE, PREFERENCES, START NEW GAME): that
 * screen draws over the menu's frame, and its index-0 pixels keep it. A real
 * case that compares with a tour reference draws the menu first: the real
 * continue-enabled main menu after a click on `control` ("09", "11", "19",
 * "07"; main_menu.<NN> of the layout table) is copied into s's frame.
 * Needs the real data (root); 0 = ok. Defined in screen_main_menu.c. */
int es2_screen_main_menu_history(es2_screen_session *s, const char *root, const char *control);

/* ---- synthetic fixtures (no game data) ------------------------------------ */

/* A seeded indexed image (w * h bytes into pixels); index 0 appears as the
 * transparent key at a few positions. */
void es2_screen_fixture_image(uint8_t *pixels, int w, int h, unsigned seed);
/* A seeded palette in DPL form (6-bit values, flag 1). */
void es2_screen_fixture_palette(uint8_t records[256][4], unsigned seed);
/* Synthetic text: 6 x 8 cells, glyph bits derived from the character code
 * (no game font); draws indices `colour`, clipped to the frame. Returns the
 * width drawn. */
int es2_screen_fixture_text(uint8_t pixels[ES2_GUI_PIXELS], int x, int y, const char *text, uint8_t colour);

#endif
