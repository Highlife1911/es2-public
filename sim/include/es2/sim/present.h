/* The presentation frame of the own dbsim.exe (presentation.md, "the
 * presentation frame in the own process"): the frame sequence 0x45fba4, the
 * palette cycler 0x4306ac, the fade set-up 0x4308fc and step 0x430b34, the
 * palette upload 0x465cc4 and the present 0x464a14 -> 0x465628 (GDI branch)
 * with the headless frame capture in the format of the proxy's presenter (one
 * ndjson record per present: frame, site, ordinal, mode, page origin S, rect R,
 * client, source and destination rectangles, the SHA-256 of the 8-bit window
 * and of the last uploaded colour table).
 *
 * Guest state is read and written only through the bound main loop's guest
 * view; calls of other DBSIM code go through the dispatch. Headless the
 * platform has no display: the GDI and USER calls are counted and answered
 * as a 32-bit desktop answers them (GetDeviceCaps(NUMCOLORS) -1,
 * GetSystemPaletteEntries writes nothing, the client area is the game
 * size). The colour table the original hands to SetDIBColorTable is kept in
 * the load layer's platform record (es2/sim/load.h), shared with the full
 * install 0x465e28 (load.c), so "the last uploaded table" is one table. */
#ifndef ES2_SIM_PRESENT_H
#define ES2_SIM_PRESENT_H
#include <stdint.h>
#include <stdio.h>
#include "es2/platform/os.h"
#include "es2/sim/load.h"
#include "es2/sim/loop.h"

#define ES2_SIM_PRESENT_SITES_MAX 64u
/* The frame dumps: at most 64 list items (a frame or a range from-to), 4096
 * frames named and 8192 files written (the proxy's limits, proxy/presenter.h). */
#define ES2_SIM_PRESENT_DUMP_ITEMS_MAX 64u
#define ES2_SIM_PRESENT_DUMP_FRAMES_MAX 4096u
#define ES2_SIM_PRESENT_DUMP_FILES_MAX 8192u

typedef struct es2_sim_present_platform {
    /* Answers. */
    int32_t num_colors;            /* GetDeviceCaps(dc, NUMCOLORS = 24); headless -1 */
    unsigned system_count;         /* entries GetSystemPaletteEntries writes (headless 0) */
    uint8_t system[1024];          /* those entries (PALETTEENTRY: red, green, blue, flags) */
    int32_t client_w, client_h;    /* GetClientRect's right/bottom; 0: the game size [0x4d253e]/[0x4d2542] */
    /* Counts of the calls the own code made instead of the platform. */
    unsigned get_dc, release_dc, device_caps, palette_entries, color_table, animate;
    unsigned client_rect, invalidate, begin_paint, select_palette, realize_palette, stretch_blt, end_paint;
    /* The last AnimatePalette table (PALETTEENTRY x 256) and the last
     * StretchBlt's rectangles (x, y, w, h) and client RECT. */
    uint8_t animate_table[1024];
    int32_t src[4], dst[4], client[4];
    /* The help call 0x4669c4 (WinHelpA, a system component the platform no
     * longer has: counted, its arguments kept, `win_help_answer` returned) and
     * the release 0x466730 (GetSystemMetrics(SM_CXSCREEN / SM_CYSCREEN)
     * answered by screen_cx/screen_cy, 0: 640 x 480; the ClipCursor rectangle
     * kept). */
    uint32_t win_help_answer, screen_cx, screen_cy;
    unsigned win_help, system_metrics, clip_cursor;
    uint32_t win_help_args[4];     /* window, file, command, data */
    int32_t clip_rect[4];
    /* DirectDraw full screen (presentation.md, "the DirectDraw full-screen
     * switch"): the switch 0x4667c8 and the DirectDraw branches of the
     * present, the upload, the frame clear and the palette apply. No
     * DirectDraw exists: every call is modelled as succeeding unless its bit
     * is set in `dd_fail` (ES2_SIM_DD_FAIL_*, the in-vitro suite's; nothing
     * else sets one). Answers: the objects DirectDrawCreate, CreateSurface,
     * GetAttachedSurface and CreatePalette write (dd_object[0..3], for
     * 0x4d3710, 0x4d3714, 0x4d3718, 0x4d371c; es2_sim_present_headless sets
     * the stand-ins ES2_SIM_DD_STANDIN + i) and the change AdjustWindowRectEx
     * makes to left, top, right, bottom (`adjust`, headless 0). Every
     * modelled call is counted in dd_calls[id] and handed to `dd_note`
     * (NULL: none). `display_mode` is the platform's request to show the
     * window full screen (1) or windowed (0), made where the original's
     * SetWindowPos makes the window topmost and screen-sized or gives it its
     * rectangle back (NULL headless: no window action). */
    uint32_t dd_object[4];
    uint32_t dd_fail;
    int32_t adjust[4];
    unsigned dd_calls[32];
    void (*dd_note)(void *user, uint32_t id, const uint32_t a[7]);
    void *dd_note_user;
    void (*display_mode)(void *user, int fullscreen);
    void *display_user;
    unsigned display_requests;
    int fullscreen;
} es2_sim_present_platform;

/* The stand-in object values: below 0x10000, where no guest object can be,
 * so a stand-in is never called through the dispatch. */
#define ES2_SIM_DD_STANDIN 0x0000dd01u
#define ES2_SIM_DD_IS_STANDIN(o) ((o) >= ES2_SIM_DD_STANDIN && (o) < ES2_SIM_DD_STANDIN + 4u)
/* The modelled calls (dd_calls index, dd_note id) and their arguments a[]. */
enum {
    ES2_SIM_DD_CREATE,              /* DirectDrawCreate(guid, out, outer) */
    ES2_SIM_DD_COOPERATIVE,         /* SetCooperativeLevel(dd, hwnd, flags) */
    ES2_SIM_DD_MODE,                /* SetDisplayMode(dd, w, h, bpp) */
    ES2_SIM_DD_RELEASE,             /* Release(stand-in) */
    ES2_SIM_DD_SURFACE,             /* CreateSurface(dd, desc size, flags, back buffers, caps, out, outer) */
    ES2_SIM_DD_ATTACHED,            /* GetAttachedSurface(primary, caps, out) */
    ES2_SIM_DD_PALETTE,             /* CreatePalette(dd, flags, entries, out, outer) */
    ES2_SIM_DD_SET_PALETTE,         /* SetPalette(primary, palette) */
    ES2_SIM_DD_SET_ENTRIES,         /* SetEntries(palette, flags, first, count) */
    ES2_SIM_DD_IS_LOST,             /* IsLost(primary) */
    ES2_SIM_DD_RESTORE,             /* Restore(primary) */
    ES2_SIM_DD_LOCK,                /* Lock(surface, rect, desc size, flags, event) */
    ES2_SIM_DD_UNLOCK,              /* Unlock(surface, pointer) */
    ES2_SIM_DD_FLIP,                /* Flip(primary, override, flags) */
    ES2_SIM_DD_METRICS,             /* GetSystemMetrics(index) */
    ES2_SIM_DD_SET_RECT,            /* SetRect(rect, left, top, right, bottom): the four values */
    ES2_SIM_DD_ADJUST,              /* AdjustWindowRectEx(left, top, right, bottom, style, menu, ex style) */
    ES2_SIM_DD_WINDOW_POS,          /* SetWindowPos(hwnd, after, x, y, cx, cy, flags) */
    ES2_SIM_DD_CLIP,                /* ClipCursor(left, top, right, bottom) */
    ES2_SIM_DD_SHOW_CURSOR,         /* ShowCursor(show) */
    ES2_SIM_DD_POST_QUIT,           /* PostQuitMessage(code) */
    ES2_SIM_DD_CALLS
};
/* dd_fail bits: the call answers an error (DDERR_SURFACELOST for IsLost). */
#define ES2_SIM_DD_FAIL_CREATE 0x001u
#define ES2_SIM_DD_FAIL_COOPERATIVE 0x002u
#define ES2_SIM_DD_FAIL_MODE 0x004u
#define ES2_SIM_DD_FAIL_SURFACE 0x008u        /* the first CreateSurface */
#define ES2_SIM_DD_FAIL_SURFACE_RETRY 0x010u  /* its retry without DDSCAPS_FLIP */
#define ES2_SIM_DD_FAIL_ATTACHED 0x020u
#define ES2_SIM_DD_FAIL_PALETTE 0x040u
#define ES2_SIM_DD_FAIL_LOST 0x080u
#define ES2_SIM_DD_FAIL_LOCK 0x100u
#define ES2_SIM_DD_ERROR 0x80004005u          /* the error the modelled calls answer */

/* One present as the capture writes it. */
typedef struct es2_sim_present_record {
    uint32_t frame, site, mode, uploads;
    int32_t ordinal;               /* -1: more than ES2_SIM_PRESENT_SITES_MAX sites in one frame */
    int32_t page[2], rect[4], client[4], src[4], dst[4];
    const char *source;            /* "dib" or "dib_outside" */
    char index_sha256[65];         /* "" = null (the window is outside the DIB) */
    char palette_sha256[65];       /* "" = null (no table uploaded yet) */
} es2_sim_present_record;

typedef struct es2_sim_present {
    es2_sim_loop *loop;
    es2_sim_present_platform platform;
    /* The colour table when no load layer is bound (tests, the harness). */
    es2_sim_load_platform own_table;
    /* The capture (NULL: none). */
    es2_os_stream *capture;
    unsigned long records, presents, write_failures;
    uint32_t ordinal_frame;
    unsigned ordinal_used;
    uint32_t ordinal_sites[ES2_SIM_PRESENT_SITES_MAX], ordinal_counts[ES2_SIM_PRESENT_SITES_MAX];
    /* Called with every present (after the capture line); may be NULL. */
    void (*on_present)(void *user, const es2_sim_present_record *record);
    void *user;
    uint32_t refused_va;
    /* Presents through the row 0x464a14 whose site no rule found
     * (es2_sim_service_present_capture): recorded with the raw return address. */
    unsigned long unattributed;
    /* The frame dumps (es2_sim_present_dump_open): the directory, the frame
     * ranges named (inclusive), the files written and failed. */
    char dump_dir[260];
    unsigned dump_items;
    uint32_t dump_from[ES2_SIM_PRESENT_DUMP_ITEMS_MAX], dump_to[ES2_SIM_PRESENT_DUMP_ITEMS_MAX];
    unsigned long dumps, dump_failures;
} es2_sim_present;

/* Zeroes the record and sets the headless answers. */
void es2_sim_present_headless(es2_sim_present *p, es2_sim_loop *loop);
/* The instance the service rows use (one per process). Without a bound
 * instance the rows use an internal headless one over the bound loop. */
void es2_sim_present_bind(es2_sim_present *p);
es2_sim_present *es2_sim_present_bound(void);

/* The capture report: opens `path` (truncating) and writes the start line;
 * 0, or 1 when the file cannot be opened. Close writes the closing stats
 * line (records = the number of records written) and closes the file. */
int es2_sim_present_capture_open(es2_sim_present *p, const char *path);
void es2_sim_present_capture_close(es2_sim_present *p);

/* Dumps of the presented 8-bit window of the frames in `list` (numbers or
 * ranges from-to, separated by commas or spaces) into the existing directory
 * `dir`, in the proxy's ES2F format and file names (presentation.md, "capture,
 * own GDI presenter, windowed DirectDraw":
 * "ES2F", little-endian u32 version 1, frame, site, ordinal, width, height,
 * uploads; 256 RGB triples of the last uploaded colour table; the index bytes,
 * rows top-down; file frame-<frame %06u>-<site %06x>-<ordinal>.es2f). A present
 * is dumped when its window lies inside the DIB and a colour table was
 * uploaded, as the proxy does. Works with or without the capture. 0, or 1 for
 * an unusable list (empty, malformed, more items or frames than the limits) or
 * a directory name that does not fit. */
int es2_sim_present_dump_open(es2_sim_present *p, const char *dir, const char *list);

/* The present 0x464a14 -> 0x465628 from own code, with the return address
 * `site` the original's call would have (0x45fbe4 for the frame sequence).
 * 0, or 1 when refused (a zero game size, a null DirectDraw surface). */
int es2_sim_present_frame(es2_sim_present *p, uint32_t site);

/* The colour-table record the uploads write (the bound load layer's
 * platform when it serves the same loop, else p's own_table; p NULL: the
 * rows' instance). NULL without a loop. */
const es2_sim_load_platform *es2_sim_present_table(es2_sim_present *p);

/* The function of the row 0x464a14 (present.c; the row is in mission_load.h, see
 * mission_load.md, "the root's last callees, the terrain shading pass"):
 * es2_sim_present_frame with the call's return address as the site. */
void es2_sim_service_present_capture(es2_sim_service_call *sc, void *user);

/* The call site (the original's return address of its call of 0x464a14) for the
 * next present through the row 0x464a14 from own C code, which calls the row
 * with return address 0. Set right before the call; the row consumes it. The
 * row's site, in order: a return address inside DBSIM's CODE as it is; else the
 * pending site; else, for a call (trapped or a machine call) out of an own
 * entry with one present site (0x461608: 0x461e21), that site; else the raw
 * return address, counted in `unattributed`. */
void es2_sim_present_site(uint32_t site);

/* The rows of sim/dispatch/services/presentation_roots.h: the help call 0x4669c4 and the
 * DirectDraw release 0x466730 (presentation.md, "the help call
 * and the DirectDraw release"). */
void es2_sim_service_help_4669c4(es2_sim_service_call *sc, void *user);
void es2_sim_service_dd_release_466730(es2_sim_service_call *sc, void *user);

/* The row of sim/dispatch/services/fullscreen.h: the DirectDraw full-screen
 * switch 0x4667c8 with its callees 0x46658c, 0x4665c4 and 0x45f13c. */
void es2_sim_service_fullscreen_switch(es2_sim_service_call *sc, void *user);
/* The DirectDraw tails of the load layer's rows (src/load.c) over the rows'
 * present instance: the frame clear 0x46548c (0x4654a2..0x46554a; EAX the
 * last call's) and the palette apply 0x465e28 (0x465f16..0x465f72; `local`
 * the colour table built on its GDI path). 0, or 1 when refused. */
int es2_sim_present_dd_clear(uint32_t *eax);
int es2_sim_present_dd_palette(const uint8_t local[1024]);

#endif
