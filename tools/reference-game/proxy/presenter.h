/* Presentation capture and the own GDI presenter of DBSIM 1.11.
 * Contract and evidence: docs/reverse-engineering/presentation.md, sections
 * "GDI branch", "Acceptance" and "capture, own GDI presenter, windowed
 * DirectDraw".
 *
 * DBSIM draws every frame in software into one 8-bit top-down DIB section
 * (bits [0x4d3028], memory DC [0x4d3034], stride = DIB width [0x4d2546]).
 * In the GDI branch (the oracle path, [0x4d255e] = 0) the present 0x465628
 * copies the screen-sized window at the page origin S ([0x4d3018]) to the
 * window with StretchBlt (call 0x4659ad); the palette reaches the DIB only
 * through SetDIBColorTable (0x465de5 in the upload 0x465cc4, 0x465f00 in the
 * install 0x465e28). This header replaces, in memory only, the two GDI32
 * import slots of those calls:
 *
 *   StretchBlt        IAT 0x6cc4c4, thunk 0x496a2a
 *   SetDIBColorTable  IAT 0x6cc4c8, thunk 0x496a30
 *
 * [presentation] capture=1: every StretchBlt from the present's call site
 *   (return address 0x4659b2) writes one JSONL record: the frame counter
 *   [0x4d257b], the site (the return address of 0x464a14, [ebp+0x10] in
 *   0x465628's frame; an own-code call of 0x464a14 gets the
 *   original's site through pr_present_site), an ordinal per (frame, site), mode [0x4d255e], S, R
 *   ([0x4d2ff8]), the client rectangle (0x465628's GetClientRect result at
 *   [ebp-0x100]), source and destination rectangles, SHA-256 of the w x h
 *   indexed bytes and of the last uploaded colour table (256 RGB triples).
 *   The file is separate from the tick log; records of a frame are written
 *   when the next frame's first present arrives (a killed process loses at
 *   most its current frame). Optional dumps of chosen frames for the
 *   offline pass (capture_dump_frames, capture_dump_dir; capture_dump_range=<from>-<to>[/<step>]
 *   names a regular series of frames, at most 4096 frames and 8192 dumps).
 *   capture_regions=1 adds to every hashed record the display mode and
 *   the region hashes of frame_regions.h ("display", "regions") and to the
 *   start line "regions":"<table id>"; without the key the bytes are
 *   unchanged.
 *   capture_qpc=1 (needs capture=1) makes the start line "schema":2
 *   and ends it with "qpc_frequency":<counts per second>, and ends every
 *   record with "qpc":<QueryPerformanceCounter>, read on entry to the hooked
 *   present (the counter the capture reads anyway for its cost figures; no
 *   further clock read). In the DirectDraw branch (ddraw_windowed.h) it is
 *   read on entry to the present of Unlock or Flip, before the palette
 *   conversion, the draw and the hash (one read, only with the key). Without
 *   the key every byte is schema 1's (own_process.md "a time per DBSIM
 *   capture record").
 * [presentation] presenter=gdi: the same StretchBlt is answered by an own
 *   presenter when it is the full-screen case: index -> RGB through the
 *   recorded colour table, integer scaling with letterboxing (nearest,
 *   aspect-correct fit when the client is smaller than the screen), one
 *   StretchDIBits of a 32-bit image of the whole client into the BeginPaint
 *   DC DBSIM passes. Every other call is forwarded unchanged. The colour
 *   table upload is always forwarded (palette upload kept).
 *
 * capture_report may contain "{pid}" (process id): the processes of an interactive session
 * write one report each. A refused capture (existing or unusable report) switches only the
 * capture off; presenter=gdi stays installed (presentation.md "capture in
 * an interactive session").
 *
 * Neither reads a clock through DBSIM, writes game memory, sends or posts a
 * message, or changes the window: InvalidateRect, BeginPaint, EndPaint and
 * the palette selection of 0x465628 stay DBSIM's own calls (keep-list item 7
 * of presentation.md). Without both keys no import slot changes.
 *
 * Configuration (read at the first top-level CreateWindowExA, before the
 * window and the DIB exist): the INI named by ES2_PRESENTATION_CONFIG, else
 * the measurement INI ES2_ORACLE_SPEED_CONFIG, else es2-oracle.ini.
 * Installation: DllMain chains the CreateWindowExA slot after the window
 * shim (window_shim.h), after checking the thunks and the code bytes the
 * frame layout depends on. The executable file is unchanged. */
#ifndef ES2_PRESENTER_H
#define ES2_PRESENTER_H
#include <stdint.h>
#include "presenter_frame.h"
#include "frame_regions.h"
#include "present_site.h"

#ifndef ES2_WINDOW_SHIM_H
#error "presenter.h needs window_shim.h (CreateWindowExA chain) and input_journal.h"
#endif

#define PR_STRETCH_IAT_RVA     0x002cc4c4u
#define PR_STRETCH_THUNK_RVA   0x00096a2au
#define PR_TABLE_IAT_RVA       0x002cc4c8u
#define PR_TABLE_THUNK_RVA     0x00096a30u
#define PR_PRESENT_RETURN_RVA  0x000659b2u   /* after `call 0x496a2a` at 0x4659ad (0x465628) */
#define PR_CLEAR_RETURN_RVA    0x000655e8u   /* after `call 0x496a2a` at 0x4655e3 (frame clear 0x46548c) */
#define PR_FRAME_RVA           0x000d257bu   /* frame counter (dword, unaligned) */
#define PR_MODE_RVA            0x000d255eu   /* 0 = GDI, else DirectDraw */
#define PR_SCREEN_W_RVA        0x000d253eu
#define PR_SCREEN_H_RVA        0x000d2542u
#define PR_DIB_W_RVA           0x000d2546u   /* also the row stride of the 8-bit DIB */
#define PR_DIB_H_RVA           0x000d254au
#define PR_RECT_RVA            0x000d2ff8u   /* R: left, top, right, bottom (inclusive) */
#define PR_PAGE_RVA            0x000d3018u   /* S: x, y */
#define PR_DIB_BITS_RVA        0x000d3028u
#define PR_DIB_DC_RVA          0x000d3034u
#define PR_REFERENCE_BASE      0x00400000u
#define PR_DUMP_FRAMES_MAX     64u
#define PR_DUMP_RANGE_FRAMES_MAX 4096u   /* frames a capture_dump_range may name */
#define PR_DUMP_RANGE_RECORDS_MAX 8192u  /* dumps written for the range (several records may share a frame) */
#define PR_SITES_MAX           64u
#define PR_STATS_EVERY         500u

typedef BOOL (WINAPI *PrStretchBlt)(HDC, int, int, int, int, HDC, int, int, int, int, DWORD);
typedef UINT (WINAPI *PrSetTable)(HDC, UINT, UINT, const RGBQUAD *);
typedef int (WINAPI *PrStretchDIBits)(HDC, int, int, int, int, int, int, int, int, const void *, const BITMAPINFO *, UINT, DWORD);

/* Code bytes whose layout the capture relies on. `rel` entries are written
 * with the image base added to the 32-bit value at offset `at` (absolute
 * addresses of DBSIM globals); call displacements are base-independent. */
typedef struct pr_signature { DWORD rva; BYTE length; BYTE bytes[20]; signed char at; DWORD rel; } pr_signature;
static const pr_signature pr_signatures[] = {
    /* 0x464a14: push 0x4d2ff8 (R); push 0x4d3018 (S); call 0x465628; add esp, 8; ret: no frame of its own,
     * so [ebp+0x10] inside 0x465628 is 0x464a14's return address. */
    {0x00064a14u, 5, {0x68,0,0,0,0}, 1, 0x000d2ff8u},
    {0x00064a19u, 5, {0x68,0,0,0,0}, 1, 0x000d3018u},
    {0x00064a1eu, 9, {0xe8,0x05,0x0c,0x00,0x00, 0x83,0xc4,0x08, 0xc3}, -1, 0},
    /* 0x465628: push ebp; mov ebp, esp; add esp, -0x100 */
    {0x00065628u, 9, {0x55, 0x8b,0xec, 0x81,0xc4,0x00,0xff,0xff,0xff}, -1, 0},
    /* 0x4658a7: lea ecx, [ebp-0x100] (the RECT of GetClientRect) */
    {0x000658a7u, 6, {0x8d,0x8d,0x00,0xff,0xff,0xff}, -1, 0},
    /* 0x4659ad: call 0x496a2a (StretchBlt); 0x4655e3: the same in the clear */
    {0x000659adu, 5, {0xe8,0x78,0x10,0x03,0x00}, -1, 0},
    {0x000655e3u, 5, {0xe8,0x42,0x14,0x03,0x00}, -1, 0},
    /* 0x465de5 and 0x465f00: call 0x496a30 (SetDIBColorTable) */
    {0x00065de5u, 5, {0xe8,0x46,0x0c,0x03,0x00}, -1, 0},
    {0x00065f00u, 5, {0xe8,0x2b,0x0b,0x03,0x00}, -1, 0},
    /* DirectDraw branch (ddraw_windowed.h): Unlock and Flip of the present inside 0x465628's frame
     * (return 0x465876, 0x465895) and of the clear 0x46548c (return 0x46552a, 0x46554a). */
    {0x00065870u, 6, {0xff,0x91,0x80,0x00,0x00,0x00}, -1, 0},
    {0x00065892u, 3, {0xff,0x52,0x2c}, -1, 0},
    {0x00065524u, 6, {0xff,0x92,0x80,0x00,0x00,0x00}, -1, 0},
    {0x00065547u, 3, {0xff,0x50,0x2c}, -1, 0},
};
#define PR_SIGNATURE_COUNT (sizeof(pr_signatures) / sizeof(pr_signatures[0]))

/* ---- state --------------------------------------------------------------- */
static BYTE *pr_image;
static volatile LONG pr_state;          /* 0 not installed, 1 create chained, 2 slots patched, -1 refused */
static IjCreate pr_next_create;
static BOOL pr_decided, pr_capture, pr_present_gdi;
static BOOL pr_regions;   /* capture_regions=1 */
static BOOL pr_qpc;       /* capture_qpc=1 */
static uint64_t pr_entry_qpc;   /* the counter on entry to the present being recorded (both branches); 0 = stamp when written */
static DWORD pr_thread;
static PrStretchBlt pr_real_stretch;
static PrSetTable pr_real_table;
static PrStretchDIBits pr_stretch_dibits;   /* NULL: GDI32's StretchDIBits; the fixture substitutes a recorder */
static RGBQUAD pr_quads[256];
static DWORD pr_rgb[256];
static BOOL pr_have_table;
static char pr_palette_hex[65];
static DWORD pr_uploads;
/* Counters (also in the stats lines). */
static DWORD pr_presents, pr_presented_gdi, pr_forwarded, pr_clear_blits, pr_other_blits, pr_foreign_thread;
static DWORD pr_bad_geometry, pr_no_table, pr_not_full_screen, pr_draw_failures, pr_records, pr_write_failures;
static DWORD pr_dumps, pr_dump_failures;
static DWORD pr_announced_sites, pr_unattributed_sites;   /* pr_present_site */
static uint64_t pr_capture_qpc, pr_capture_qpc_max, pr_present_qpc, pr_present_qpc_max;
/* Capture file: records of the current frame are buffered. */
static HANDLE pr_file = INVALID_HANDLE_VALUE;
static char pr_buffer[65536];
static DWORD pr_used, pr_buffer_frame = 0xffffffffu, pr_stats_frame = 0xffffffffu;
static DWORD pr_ordinal_frame = 0xffffffffu, pr_ordinal_sites[PR_SITES_MAX], pr_ordinal_counts[PR_SITES_MAX];
static unsigned pr_ordinal_used;
static WCHAR pr_dump_directory[MAX_PATH];
static DWORD pr_dump_frames[PR_DUMP_FRAMES_MAX];
static unsigned pr_dump_frame_count;
/* capture_dump_range=<from>-<to>[/<step>]: every step-th frame from `from` to `to` (inclusive). */
static BOOL pr_range_set;
static DWORD pr_range_from, pr_range_to, pr_range_step, pr_range_dumps;
/* Own buffers (process heap, never DBSIM's allocator). */
static DWORD *pr_pixels;
static size_t pr_pixel_capacity;
static int *pr_columns;
static size_t pr_column_capacity;

static DWORD pr_read(DWORD rva) { DWORD v; memcpy(&v, pr_image + rva, sizeof(v)); return v; }
static LONG pr_read_signed(DWORD rva) { return (LONG)pr_read(rva); }
static uint64_t pr_now(void) { LARGE_INTEGER n; QueryPerformanceCounter(&n); return (uint64_t)n.QuadPart; }
/* A value inside the image as a reference VA, anything else as it is. */
static DWORD pr_reference_va(DWORD value) {
    uintptr_t base = (uintptr_t)pr_image;
    return value >= base && value - base < IJ_IMAGE_SIZE ? (DWORD)(value - base + PR_REFERENCE_BASE) : value;
}

/* ---- The site of a present --------------------------------------------------
 * `raw` is the return address of the call of 0x464a14 ([ebp+0x10] in
 * 0x465628's frame). The site, in this order (as es2sim's present.c
 * call_site(), presentation.md "present sites from own code"
 * and "present sites from own code in the proxy capture"):
 *   1. a return address inside CODE [0x401000, 0x497000): unchanged;
 *   2. own code's announced call (present_site.h) whose callee is 0x464a14:
 *      its return address, if that follows a `call 0x464a14` (e8 rel32) in
 *      the image;
 *   3. else the announced caller's row in pr_own_present_callers, whose site
 *      must follow such a call as well;
 *   4. otherwise the raw address, counted in pr_unattributed_sites (the
 *      frames table then refuses the record: dbsim_frames.py "bad site").
 * Every own caller of 0x464a14 in the hook adapters announces (only the
 * mission-load root 0x461608 calls it: core mission_root.c, the call at
 * 0x461e1c). A new one needs its announcement and a row here. */
#define PR_CODE_LO_RVA         0x00001000u   /* DBSIM's CODE section [0x401000, 0x497000) */
#define PR_CODE_HI_RVA         0x00097000u
#define PR_PRESENT_ENTRY_RVA   0x00064a14u
typedef struct pr_own_caller { DWORD entry, site; } pr_own_caller;
static const pr_own_caller pr_own_present_callers[] = {
    {0x00461608u, 0x00461e21u},   /* mission load (core mission_root): 0x461e1c call 0x464a14 */
};
#define PR_OWN_CALLER_COUNT (sizeof(pr_own_present_callers) / sizeof(pr_own_present_callers[0]))
static BOOL pr_code_address(uintptr_t value) {
    uintptr_t base = (uintptr_t)pr_image;
    return value >= base + PR_CODE_LO_RVA && value < base + PR_CODE_HI_RVA;
}
/* `value` (absolute) lies in CODE right after `call 0x464a14`. */
static BOOL pr_present_return(uintptr_t value) {
    uintptr_t base = (uintptr_t)pr_image;
    if (!pr_code_address(value) || value - 5u < base + PR_CODE_LO_RVA) return FALSE;
    const BYTE *call = (const BYTE *)value - 5;
    DWORD displacement; memcpy(&displacement, call + 1, sizeof(displacement));
    return call[0] == 0xe8 && (DWORD)(value - base) + displacement == PR_PRESENT_ENTRY_RVA;
}
static DWORD pr_present_site(DWORD raw) {
    uintptr_t base = (uintptr_t)pr_image;
    if (pr_code_address(raw)) return pr_reference_va(raw);
    const es2_present_site_call *a = &es2_present_site_current;
    if (a->target && a->target == base + PR_PRESENT_ENTRY_RVA) {
        if (a->ret && pr_present_return(a->ret)) { ++pr_announced_sites; return pr_reference_va(a->ret); }
        for (unsigned i = 0; i < PR_OWN_CALLER_COUNT; ++i)
            if (pr_own_present_callers[i].entry == a->caller &&
                pr_present_return(base + pr_own_present_callers[i].site - PR_REFERENCE_BASE)) {
                ++pr_announced_sites;
                return pr_own_present_callers[i].site;
            }
    }
    ++pr_unattributed_sites;
    return pr_reference_va(raw);
}

/* ---- capture file ---------------------------------------------------------- */
static void pr_flush(void) {
    if (!pr_used || pr_file == INVALID_HANDLE_VALUE) { pr_used = 0; return; }
    DWORD written = 0;
    if (!WriteFile(pr_file, pr_buffer, pr_used, &written, NULL) || written != pr_used) ++pr_write_failures;
    pr_used = 0;
}
static void pr_append(const char *line, int length) {
    if (length <= 0 || (DWORD)length >= sizeof(pr_buffer)) { ++pr_write_failures; return; }
    if (pr_used + (DWORD)length > sizeof(pr_buffer)) pr_flush();
    memcpy(pr_buffer + pr_used, line, (size_t)length);
    pr_used += (DWORD)length;
}
static void pr_write_stats(DWORD frame) {
    char line[768];
    LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency);
    int n = snprintf(line, sizeof(line),
        "{\"event\":\"stats\",\"frame\":%lu,\"records\":%lu,\"presents\":%lu,\"presented_gdi\":%lu,\"forwarded\":%lu,"
        "\"clear_blits\":%lu,\"other_blits\":%lu,\"foreign_thread\":%lu,\"bad_geometry\":%lu,\"no_table\":%lu,"
        "\"not_full_screen\":%lu,\"draw_failures\":%lu,\"uploads\":%lu,\"dumps\":%lu,\"dump_failures\":%lu,"
        "\"write_failures\":%lu,\"qpc_frequency\":%lld,\"capture_qpc\":%llu,\"capture_qpc_max\":%llu,"
        "\"present_qpc\":%llu,\"present_qpc_max\":%llu,\"announced_sites\":%lu,\"unattributed_sites\":%lu}\r\n",
        (unsigned long)frame, (unsigned long)pr_records, (unsigned long)pr_presents, (unsigned long)pr_presented_gdi,
        (unsigned long)pr_forwarded, (unsigned long)pr_clear_blits, (unsigned long)pr_other_blits,
        (unsigned long)pr_foreign_thread, (unsigned long)pr_bad_geometry, (unsigned long)pr_no_table,
        (unsigned long)pr_not_full_screen, (unsigned long)pr_draw_failures, (unsigned long)pr_uploads,
        (unsigned long)pr_dumps, (unsigned long)pr_dump_failures, (unsigned long)pr_write_failures,
        (long long)frequency.QuadPart, (unsigned long long)pr_capture_qpc, (unsigned long long)pr_capture_qpc_max,
        (unsigned long long)pr_present_qpc, (unsigned long long)pr_present_qpc_max,
        (unsigned long)pr_announced_sites, (unsigned long)pr_unattributed_sites);
    pr_append(line, n);
}
/* Called with the frame of a new record: a frame change writes the previous
 * frame's records (and every PR_STATS_EVERY frames a stats line). */
static void pr_frame_boundary(DWORD frame) {
    if (frame == pr_buffer_frame) return;
    if (pr_stats_frame == 0xffffffffu) pr_stats_frame = frame;
    else if (frame - pr_stats_frame >= PR_STATS_EVERY) { pr_write_stats(frame); pr_stats_frame = frame; }
    pr_flush();
    pr_buffer_frame = frame;
}
static unsigned pr_next_ordinal(DWORD frame, DWORD site) {
    if (frame != pr_ordinal_frame) { pr_ordinal_frame = frame; pr_ordinal_used = 0; }
    for (unsigned i = 0; i < pr_ordinal_used; ++i) if (pr_ordinal_sites[i] == site) return pr_ordinal_counts[i]++;
    if (pr_ordinal_used == PR_SITES_MAX) return 0xffffffffu;
    pr_ordinal_sites[pr_ordinal_used] = site; pr_ordinal_counts[pr_ordinal_used] = 1;
    ++pr_ordinal_used;
    return 0;
}
static BOOL pr_dump_wanted(DWORD frame) {
    for (unsigned i = 0; i < pr_dump_frame_count; ++i) if (pr_dump_frames[i] == frame) return TRUE;
    if (pr_range_set && frame >= pr_range_from && frame <= pr_range_to && (frame - pr_range_from) % pr_range_step == 0 &&
        pr_range_dumps < PR_DUMP_RANGE_RECORDS_MAX) {
        ++pr_range_dumps;
        return TRUE;
    }
    return FALSE;
}
static void pr_put32(BYTE *p, DWORD v) { p[0] = (BYTE)v; p[1] = (BYTE)(v >> 8); p[2] = (BYTE)(v >> 16); p[3] = (BYTE)(v >> 24); }
/* Dump format (presentation.md "capture, own GDI presenter, windowed
 * DirectDraw"): "ES2F", then little-endian u32
 * version 1, frame, site, ordinal, width, height, uploads; 256 RGB triples of
 * the colour table; width x height index bytes, rows top-down. */
static void pr_dump(DWORD frame, DWORD site, unsigned ordinal, const BYTE *bits, int stride, int w, int h,
                    const BYTE rgb[768], DWORD uploads) {
    WCHAR path[MAX_PATH];
    if (swprintf(path, MAX_PATH, L"%ls\\frame-%06lu-%06lx-%u.es2f", pr_dump_directory, (unsigned long)frame,
                 (unsigned long)site, ordinal) < 0) { ++pr_dump_failures; return; }
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) { ++pr_dump_failures; return; }
    BYTE header[32 + 768];
    memcpy(header, "ES2F", 4);
    DWORD fields[7] = {1, frame, site, ordinal, (DWORD)w, (DWORD)h, uploads};
    for (unsigned i = 0; i < 7; ++i) pr_put32(header + 4 + 4 * i, fields[i]);
    memcpy(header + 32, rgb, 768);
    DWORD written = 0;
    BOOL ok = WriteFile(f, header, sizeof(header), &written, NULL) && written == sizeof(header);
    for (int y = 0; ok && y < h; ++y)
        ok = WriteFile(f, bits + (size_t)y * (size_t)stride, (DWORD)w, &written, NULL) && written == (DWORD)w;
    CloseHandle(f);
    if (ok) ++pr_dumps; else ++pr_dump_failures;
}

/* SHA-256 of w x h bytes, rows `stride` apart. */
static void pr_hash_window(const BYTE *origin, int stride, int w, int h, char hex[65]) {
    sha256_ctx s; sha256_init(&s);
    for (int y = 0; y < h; ++y) sha256_update(&s, origin + (size_t)y * (size_t)stride, (size_t)w);
    sha256_hex(&s, hex);
}
static void pr_rgb_hash(const BYTE rgb[768], char hex[65]) {
    sha256_ctx s; sha256_init(&s); sha256_update(&s, rgb, 768); sha256_hex(&s, hex);
}

/* One capture record (both branches). index_hex/palette_hex empty = null. */
typedef struct pr_record_fields {
    DWORD frame, site; LONG client[4]; int src[4], dst[4];
    const char *source, *index_hex, *palette_hex, *outcome; DWORD uploads;
    const BYTE *origin; int stride; const BYTE *palette_rgb;   /* for a dump; origin NULL = no dump */
} pr_record_fields;
/* The rows of a hashed window for frame_regions.h. */
typedef struct pr_region_rows { const BYTE *origin; int stride; } pr_region_rows;
static const uint8_t *pr_region_row(void *context, int32_t y) {
    const pr_region_rows *r = context;
    return r->origin + (size_t)y * (size_t)r->stride;
}
/* The display mode [[0x4d24c0]+0x14] (-1 while the display object is not built). */
static int32_t pr_display_mode(void) {
    DWORD d = pr_read(FR_DISPLAY_VA - PR_REFERENCE_BASE);
    if (!d) return -1;
    int32_t mode;
    memcpy(&mode, (const BYTE *)(uintptr_t)d + FR_DISPLAY_MODE_OFFSET, sizeof(mode));
    return mode;
}
static void pr_write_record(const pr_record_fields *r) {
    pr_frame_boundary(r->frame);
    unsigned ordinal = pr_next_ordinal(r->frame, r->site);
    const LONG *page = (const LONG *)(pr_image + PR_PAGE_RVA), *rect = (const LONG *)(pr_image + PR_RECT_RVA);
    BOOL index = r->index_hex && r->index_hex[0], palette = r->palette_hex && r->palette_hex[0];
    char region_fields[200] = "";
    if (pr_regions && index && r->origin) {
        char hashes[FR_REGION_COUNT][FR_HASH_DIGITS + 1];
        pr_region_rows rows = {r->origin, r->stride};
        int32_t display = pr_display_mode();
        if (fr_region_hashes(display, r->src[2], r->src[3], pr_region_row, &rows, hashes) ||
            fr_format(region_fields, sizeof(region_fields), display, hashes) < 0)
            region_fields[0] = 0;
    }
    /* The time stamp ends the record, after the region fields. */
    char qpc_field[48] = "";
    if (pr_qpc) snprintf(qpc_field, sizeof(qpc_field), ",\"qpc\":%llu", (unsigned long long)(pr_entry_qpc ? pr_entry_qpc : pr_now()));
    char line[1024];
    int n = snprintf(line, sizeof(line),
        "{\"frame\":%lu,\"site\":\"0x%06lx\",\"ordinal\":%ld,\"mode\":%lu,\"S\":[%ld,%ld],\"R\":[%ld,%ld,%ld,%ld],"
        "\"client\":[%ld,%ld,%ld,%ld],\"src\":[%d,%d,%d,%d],\"dst\":[%d,%d,%d,%d],\"source\":\"%s\","
        "\"index_sha256\":%s%s%s,\"palette_sha256\":%s%s%s,\"uploads\":%lu,\"presented\":\"%s\"%s%s}\r\n",
        (unsigned long)r->frame, (unsigned long)r->site, ordinal == 0xffffffffu ? -1L : (long)ordinal,
        (unsigned long)pr_read(PR_MODE_RVA), (long)page[0], (long)page[1],
        (long)rect[0], (long)rect[1], (long)rect[2], (long)rect[3],
        (long)r->client[0], (long)r->client[1], (long)r->client[2], (long)r->client[3],
        r->src[0], r->src[1], r->src[2], r->src[3], r->dst[0], r->dst[1], r->dst[2], r->dst[3], r->source,
        index ? "\"" : "", index ? r->index_hex : "null", index ? "\"" : "",
        palette ? "\"" : "", palette ? r->palette_hex : "null", palette ? "\"" : "",
        (unsigned long)r->uploads, r->outcome, region_fields, qpc_field);
    pr_append(line, n);
    ++pr_records;
    if (r->origin && index && palette && (pr_dump_frame_count || pr_range_set) && pr_dump_wanted(r->frame))
        pr_dump(r->frame, r->site, ordinal, r->origin, r->stride, r->src[2], r->src[3], r->palette_rgb, r->uploads);
}

/* ---- the presenter ---------------------------------------------------------- */
static BOOL pr_ensure_buffers(size_t pixels, size_t columns) {
    HANDLE heap = GetProcessHeap();
    if (pixels > pr_pixel_capacity) {
        DWORD *grown = pr_pixels ? HeapReAlloc(heap, 0, pr_pixels, pixels * 4u) : HeapAlloc(heap, 0, pixels * 4u);
        if (!grown) return FALSE;
        pr_pixels = grown; pr_pixel_capacity = pixels;
    }
    if (columns > pr_column_capacity) {
        int *grown = pr_columns ? HeapReAlloc(heap, 0, pr_columns, columns * sizeof(int)) : HeapAlloc(heap, 0, columns * sizeof(int));
        if (!grown) return FALSE;
        pr_columns = grown; pr_column_capacity = columns;
    }
    return TRUE;
}
/* Converts and scales a w x h indexed image into the whole cw x ch client
 * and draws it with one StretchDIBits. FALSE: nothing was drawn. */
static BOOL pr_draw(HDC hdc, const BYTE *bits, int stride, int w, int h, const DWORD table[256], int cw, int ch) {
    pr_layout l;
    if (!pr_compute_layout(w, h, cw, ch, &l) || !pr_ensure_buffers((size_t)cw * (size_t)ch, (size_t)l.width)) {
        ++pr_bad_geometry; return FALSE;
    }
    pr_render(bits, stride, w, h, table, pr_pixels, cw, ch, &l, pr_columns);
    BITMAPINFO info;
    memset(&info, 0, sizeof(info));
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = cw; info.bmiHeader.biHeight = -ch;   /* top-down */
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    PrStretchDIBits draw = pr_stretch_dibits ? pr_stretch_dibits : StretchDIBits;
    int lines = draw(hdc, 0, 0, cw, ch, 0, 0, cw, ch, pr_pixels, &info, DIB_RGB_COLORS, SRCCOPY);
    if (lines == 0 || lines == (int)GDI_ERROR) { ++pr_draw_failures; return FALSE; }
    return TRUE;
}

/* ---- the two import replacements ------------------------------------------- */
static void pr_palette_hash(void) {
    BYTE rgb[768];
    for (unsigned i = 0; i < 256; ++i) { rgb[3 * i] = pr_quads[i].rgbRed; rgb[3 * i + 1] = pr_quads[i].rgbGreen; rgb[3 * i + 2] = pr_quads[i].rgbBlue; }
    pr_rgb_hash(rgb, pr_palette_hex);
}

/* SetDIBColorTable: always forwarded; for the DIB DC the table as DBSIM
 * passed it is recorded (the reserved byte, stack residue, is ignored). */
static UINT WINAPI pr_set_table(HDC hdc, UINT start, UINT count, const RGBQUAD *quads) {
    UINT result = pr_real_table(hdc, start, count, quads);
    if (GetCurrentThreadId() == pr_thread && hdc && hdc == (HDC)(uintptr_t)pr_read(PR_DIB_DC_RVA) && quads && start < 256) {
        UINT n = count < 256 - start ? count : 256 - start;
        memcpy(pr_quads + start, quads, n * sizeof(RGBQUAD));
        pr_table_from_quads(pr_quads, pr_rgb);
        pr_palette_hash();
        pr_have_table = TRUE; ++pr_uploads;
    }
    return result;
}

/* StretchBlt. The return address tells the present (0x4659b2) from the
 * clear (0x4655e8); for the present, 0x465628's frame pointer (EBP on
 * entry: the thunk is a plain jmp) gives the site and the client RECT. */
static __attribute__((noinline)) BOOL WINAPI pr_stretch_blt(HDC hdc, int dx, int dy, int dw, int dh, HDC source,
                                                            int sx, int sy, int sw, int sh, DWORD rop) {
    uintptr_t back = (uintptr_t)__builtin_return_address(0);
    const DWORD *caller = *(const DWORD *const *)__builtin_frame_address(0);
    uintptr_t base = (uintptr_t)pr_image;
    if (back != base + PR_PRESENT_RETURN_RVA) {
        if (back == base + PR_CLEAR_RETURN_RVA) ++pr_clear_blits; else ++pr_other_blits;
        return pr_real_stretch(hdc, dx, dy, dw, dh, source, sx, sy, sw, sh, rop);
    }
    if (GetCurrentThreadId() != pr_thread) { ++pr_foreign_thread; return pr_real_stretch(hdc, dx, dy, dw, dh, source, sx, sy, sw, sh, rop); }
    DWORD error = GetLastError();
    uint64_t start = pr_now();
    ++pr_presents;
    const LONG *rc = (const LONG *)((const BYTE *)caller - 0x100);
    int stride = (int)pr_read_signed(PR_DIB_W_RVA), dib_h = (int)pr_read_signed(PR_DIB_H_RVA);
    const BYTE *bits = (const BYTE *)(uintptr_t)pr_read(PR_DIB_BITS_RVA);
    BOOL dib = source && source == (HDC)(uintptr_t)pr_read(PR_DIB_DC_RVA) && bits && rop == SRCCOPY;
    BOOL inside = dib && sx >= 0 && sy >= 0 && sw > 0 && sh > 0 && stride > 0 && dib_h > 0 &&
                  sx <= stride - sw && sy <= dib_h - sh;
    const BYTE *origin = inside ? bits + (size_t)sy * (size_t)stride + (size_t)sx : NULL;
    char index_hex[65] = "";
    if (pr_capture && inside) pr_hash_window(origin, stride, sw, sh, index_hex);
    uint64_t hashed = pr_now();
    pr_capture_qpc += hashed - start;
    if (hashed - start > pr_capture_qpc_max) pr_capture_qpc_max = hashed - start;
    const char *outcome = "original";
    BOOL drawn = FALSE;
    if (pr_present_gdi) {
        int screen_w = (int)pr_read_signed(PR_SCREEN_W_RVA), screen_h = (int)pr_read_signed(PR_SCREEN_H_RVA);
        if (!inside) ++pr_bad_geometry;
        else if (dx || dy || rc[0] || rc[1] || dw != rc[2] || dh != rc[3] || sw != screen_w || sh != screen_h)
            ++pr_not_full_screen;
        else if (!pr_have_table) ++pr_no_table;
        else drawn = pr_draw(hdc, origin, stride, sw, sh, pr_rgb, dw, dh);
        if (drawn) { ++pr_presented_gdi; outcome = "gdi"; } else { ++pr_forwarded; outcome = "forwarded"; }
        uint64_t done = pr_now();
        pr_present_qpc += done - hashed;
        if (done - hashed > pr_present_qpc_max) pr_present_qpc_max = done - hashed;
    }
    if (pr_capture) {
        BYTE rgb[768];
        for (unsigned i = 0; i < 256; ++i) { rgb[3 * i] = pr_quads[i].rgbRed; rgb[3 * i + 1] = pr_quads[i].rgbGreen; rgb[3 * i + 2] = pr_quads[i].rgbBlue; }
        pr_record_fields r = {pr_read(PR_FRAME_RVA), pr_present_site(caller[4]), {rc[0], rc[1], rc[2], rc[3]},
            {sx, sy, sw, sh}, {dx, dy, dw, dh}, !dib ? "other" : inside ? "dib" : "dib_outside", index_hex,
            pr_have_table ? pr_palette_hex : "", outcome, pr_uploads, origin, stride, rgb};
        pr_entry_qpc = start;
        pr_write_record(&r);
        pr_entry_qpc = 0;
    }
    SetLastError(error);
    if (drawn) return TRUE;
    return pr_real_stretch(hdc, dx, dy, dw, dh, source, sx, sy, sw, sh, rop);
}
/* ---- configuration and installation ---------------------------------------- */
static BOOL pr_signatures_valid(const BYTE *image, uintptr_t base) {
    if (!ws_thunk_valid(image, base, PR_STRETCH_IAT_RVA, PR_STRETCH_THUNK_RVA) ||
        !ws_thunk_valid(image, base, PR_TABLE_IAT_RVA, PR_TABLE_THUNK_RVA)) return FALSE;
    for (unsigned i = 0; i < PR_SIGNATURE_COUNT; ++i) {
        const pr_signature *s = &pr_signatures[i];
        BYTE expected[20];
        memcpy(expected, s->bytes, s->length);
        if (s->at >= 0) { DWORD v = (DWORD)(base + s->rel); memcpy(expected + s->at, &v, 4); }
        if (memcmp(image + s->rva, expected, s->length)) return FALSE;
    }
    return TRUE;
}

/* The INI with the [presentation] keys (see the header comment). */
static BOOL pr_config_file(WCHAR *out) {
    DWORD n = GetEnvironmentVariableW(L"ES2_PRESENTATION_CONFIG", out, MAX_PATH);
    if (n && n < MAX_PATH) return TRUE;
    n = GetEnvironmentVariableW(L"ES2_ORACLE_SPEED_CONFIG", out, MAX_PATH);
    if (n && n < MAX_PATH) return TRUE;
    if (!config_path[0] || wcslen(config_path) >= MAX_PATH) return FALSE;
    wcscpy(out, config_path);
    return TRUE;
}

static void pr_parse_frames(const WCHAR *text) {
    pr_dump_frame_count = 0;
    while (*text && pr_dump_frame_count < PR_DUMP_FRAMES_MAX) {
        WCHAR *end;
        unsigned long v = wcstoul(text, &end, 10);
        if (end == text) { ++text; continue; }
        pr_dump_frames[pr_dump_frame_count++] = (DWORD)v;
        text = end;
    }
}

/* "<from>-<to>" or "<from>-<to>/<step>" (decimal, optional blanks). FALSE: malformed, from > to,
 * step 0, or more than PR_DUMP_RANGE_FRAMES_MAX frames; the range stays off. An empty text is not an error. */
static BOOL pr_parse_range(const WCHAR *text) {
    pr_range_set = FALSE;
    while (*text == L' ') ++text;
    if (!*text) return TRUE;
    WCHAR *end;
    if (*text < L'0' || *text > L'9') return FALSE;
    unsigned long from = wcstoul(text, &end, 10);
    while (*end == L' ') ++end;
    if (*end != L'-') return FALSE;
    text = end + 1;
    while (*text == L' ') ++text;
    if (*text < L'0' || *text > L'9') return FALSE;
    unsigned long to = wcstoul(text, &end, 10), step = 1;
    while (*end == L' ') ++end;
    if (*end == L'/') {
        text = end + 1;
        while (*text == L' ') ++text;
        if (*text < L'0' || *text > L'9') return FALSE;
        step = wcstoul(text, &end, 10);
        while (*end == L' ') ++end;
    }
    if (*end || from > to || to > 0xfffffffeul || !step || (to - from) / step >= PR_DUMP_RANGE_FRAMES_MAX) return FALSE;
    pr_range_from = (DWORD)from; pr_range_to = (DWORD)to; pr_range_step = (DWORD)step;
    pr_range_set = TRUE;
    return TRUE;
}

/* Replaces every "{pid}" in `path` (MAX_PATH WCHARs) by the decimal process id, so that the DBSIM
 * processes of one interactive session (VSHELL -> DBSIM -> VSHELL -> DBSIM ...) write one report
 * each. FALSE when the result does not fit. */
static BOOL pr_expand_pid(WCHAR *path) {
    static const WCHAR token[] = L"{pid}";
    WCHAR out[MAX_PATH], digits[16];
    int pid_length = swprintf(digits, 16, L"%lu", (unsigned long)GetCurrentProcessId());
    size_t used = 0;
    const WCHAR *in = path, *hit;
    if (pid_length <= 0) return FALSE;
    while ((hit = wcsstr(in, token)) != NULL) {
        size_t before = (size_t)(hit - in);
        if (used + before + (size_t)pid_length >= MAX_PATH) return FALSE;
        memcpy(out + used, in, before * sizeof(WCHAR)); used += before;
        memcpy(out + used, digits, (size_t)pid_length * sizeof(WCHAR)); used += (size_t)pid_length;
        in = hit + (sizeof(token) / sizeof(WCHAR) - 1);
    }
    size_t rest = wcslen(in);
    if (used + rest >= MAX_PATH) return FALSE;
    memcpy(out + used, in, (rest + 1) * sizeof(WCHAR));
    wcscpy(path, out);
    return TRUE;
}

/* capture_report=<path>, or beside_tick_log: "<[trace] tick_log>.capture.ndjson" (not *.jsonl: the
 * evidence rules treat a *.jsonl in a run directory as a raw tick log, tools/evidence_budget.py).
 * An explicit path may contain "{pid}" (process id): a per-process report for an interactive session.
 * The file is created new; an existing file refuses the capture (the caller keeps the presenter). */
static BOOL pr_open_capture(const WCHAR *ini) {
    WCHAR path[MAX_PATH], frames[512];
    DWORD n = GetPrivateProfileStringW(L"presentation", L"capture_report", L"", path, MAX_PATH, ini);
    if (!n || n >= MAX_PATH - 1) return FALSE;
    if (_wcsicmp(path, L"beside_tick_log") && !pr_expand_pid(path)) return FALSE;
    if (!_wcsicmp(path, L"beside_tick_log")) {
        n = config_path[0] ? GetPrivateProfileStringW(L"trace", L"tick_log", L"", path, MAX_PATH, config_path) : 0;
        if (!n || n + wcslen(L".capture.ndjson") >= MAX_PATH) return FALSE;
        wcscat(path, L".capture.ndjson");
    }
    pr_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (pr_file == INVALID_HANDLE_VALUE) return FALSE;
    pr_regions = GetPrivateProfileIntW(L"presentation", L"capture_regions", 0, ini) != 0;
    pr_qpc = GetPrivateProfileIntW(L"presentation", L"capture_qpc", 0, ini) != 0;
    GetPrivateProfileStringW(L"presentation", L"capture_dump_frames", L"", frames, 512, ini);
    pr_parse_frames(frames);
    {
        WCHAR range[64];
        GetPrivateProfileStringW(L"presentation", L"capture_dump_range", L"", range, 64, ini);
        if (!pr_parse_range(range)) log_event("presentation_dump_range_refused");
    }
    if (pr_dump_frame_count || pr_range_set) {
        /* capture_dump_dir=<dir>: <dir>/<capture file name>.frames (dumps are decoded game frames:
         * keep them out of evidence that is archived); else <capture file>.frames. */
        WCHAR dir[MAX_PATH];
        const WCHAR *name = wcsrchr(path, L'\\');
        int length;
        if (GetPrivateProfileStringW(L"presentation", L"capture_dump_dir", L"", dir, MAX_PATH, ini)) {
            CreateDirectoryW(dir, NULL);
            length = swprintf(pr_dump_directory, MAX_PATH, L"%ls\\%ls.frames", dir, name ? name + 1 : path);
        } else length = swprintf(pr_dump_directory, MAX_PATH, L"%ls.frames", path);
        if (length < 0 || length >= MAX_PATH - 40 || !CreateDirectoryW(pr_dump_directory, NULL)) { pr_dump_frame_count = 0; pr_range_set = FALSE; }
    }
    return TRUE;
}

static BOOL pr_replace_slot(DWORD iat_rva, void *hook, void **previous) {
    DWORD *slot = (DWORD *)(pr_image + iat_rva);
    DWORD protection, ignored;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection)) return FALSE;
    *previous = (void *)(uintptr_t)*slot;
    InterlockedExchange((LONG *)slot, (LONG)(uintptr_t)hook);
    VirtualProtect(slot, sizeof(*slot), protection, &ignored);
    return TRUE;
}

/* Main thread, first top-level CreateWindowExA (before the window exists). */
static void pr_decide(void) {
    if (pr_decided) return;
    pr_decided = TRUE;
    ij_ready();   /* config_path (input_journal.h) */
    WCHAR ini[MAX_PATH], presenter[16];
    if (!pr_config_file(ini)) return;
    BOOL capture = GetPrivateProfileIntW(L"presentation", L"capture", 0, ini) != 0;
    GetPrivateProfileStringW(L"presentation", L"presenter", L"original", presenter, 16, ini);
    BOOL gdi = !_wcsicmp(presenter, L"gdi");
    if (!gdi && _wcsicmp(presenter, L"original")) { log_event("presentation_presenter_unknown"); return; }
    if (!capture && !gdi) return;
    /* The capture is diagnosis, the presenter is the feature: a refused capture (existing report,
     * unusable path) switches only the capture off. */
    if (capture && !pr_open_capture(ini)) { log_event("presentation_capture_refused"); capture = FALSE; }
    if (!capture && !gdi) return;
    pr_thread = GetCurrentThreadId();
    if (!pr_replace_slot(PR_TABLE_IAT_RVA, (void *)pr_set_table, (void **)&pr_real_table) ||
        !pr_replace_slot(PR_STRETCH_IAT_RVA, (void *)pr_stretch_blt, (void **)&pr_real_stretch)) {
        log_event("presentation_refused"); return;
    }
    pr_capture = capture && pr_file != INVALID_HANDLE_VALUE;
    pr_present_gdi = gdi;
    InterlockedExchange(&pr_state, 2);
    if (pr_capture) {
        char line[384];
        int n = snprintf(line, sizeof(line),
            "{\"event\":\"start\",\"schema\":%d,\"key\":\"frame_counter_0x4d257b\",\"presenter\":\"%s\",\"dump_frames\":%u,\"pid\":%lu}\r\n",
            pr_qpc ? 2 : 1, gdi ? "gdi" : "original", pr_dump_frame_count, (unsigned long)GetCurrentProcessId());
        /* The range only when configured. */
        if (pr_range_set && n > 1 && line[n - 3] == '}') {
            n -= 3;
            n += snprintf(line + n, sizeof(line) - (size_t)n, ",\"dump_range\":[%lu,%lu,%lu]}\r\n",
                          (unsigned long)pr_range_from, (unsigned long)pr_range_to, (unsigned long)pr_range_step);
        }
        /* The region table only when configured. */
        if (pr_regions && n > 1 && line[n - 3] == '}') {
            n -= 3;
            n += snprintf(line + n, sizeof(line) - (size_t)n, ",\"regions\":\"%s\"}\r\n", FR_TABLE_ID);
        }
        /* Schema 2 names the counter's rate, last. */
        if (pr_qpc && n > 1 && line[n - 3] == '}') {
            LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency);
            n -= 3;
            n += snprintf(line + n, sizeof(line) - (size_t)n, ",\"qpc_frequency\":%lld}\r\n", (long long)frequency.QuadPart);
        }
        pr_append(line, n);
        pr_flush();
    }
    log_event(gdi ? (pr_capture ? "presentation_capture_gdi" : "presentation_gdi") : "presentation_capture");
}

static HWND WINAPI pr_create_window(DWORD ex_style, LPCSTR class_name, LPCSTR title, DWORD style, int x, int y,
                                     int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param) {
    if (!parent) pr_decide();
    return pr_next_create(ex_style, class_name, title, style, x, y, width, height, parent, menu, instance, param);
}

/* DllMain-safe: memory checks and one slot exchange, no file I/O. Chains
 * the CreateWindowExA slot in front of the window shim (or of the input
 * journal when the shim refused). */
static BOOL install_presenter_image(BYTE *image, uintptr_t expected_base) {
    if (InterlockedCompareExchange(&pr_state, -1, 0) != 0) return pr_state > 0;
    if (ij_iat_state != 1 || ij_image != image || !pr_signatures_valid(image, expected_base)) return FALSE;
    DWORD current;
    memcpy(&current, image + WS_CREATE_IAT_RVA, sizeof(current));
    if (current != (DWORD)(uintptr_t)ws_create_window && current != (DWORD)(uintptr_t)ij_create_window) return FALSE;
    pr_image = image;
    if (!pr_replace_slot(WS_CREATE_IAT_RVA, (void *)pr_create_window, (void **)&pr_next_create)) return FALSE;
    InterlockedExchange(&pr_state, 1);
    return TRUE;
}

static void install_presenter(void) {
    if (ij_iat_state != 1) return;   /* install_input_journal() checked the executable name and image */
    install_presenter_image(ij_image, (uintptr_t)PR_REFERENCE_BASE);
}

/* Process exit (DLL_PROCESS_DETACH): the buffered frame and a last stats line. */
static void presenter_shutdown(void) {
    if (pr_file == INVALID_HANDLE_VALUE) return;
    pr_write_stats(pr_buffer_frame);
    pr_flush();
    CloseHandle(pr_file);
    pr_file = INVALID_HANDLE_VALUE;
}
#endif
