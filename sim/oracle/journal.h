/* The input journal of the oracle (own_process.md section 8; contract in
 * docs/reference-game/input_journal.md): the parser and the per-frame state
 * that answer DBSIM's input imports in a replay, shared by the proxy
 * (tools/reference-game/proxy/input_journal.h, which keeps the IAT wrappers,
 * the record writer, the configuration and the focus subclass) and the own
 * process (sim/: the pump 0x464c78 and the input services). The proxy's
 * input_journal_* fixtures guard it. Pure C17: no Win32, explicit state.
 *
 * Entries are keyed by the main loop's frame counter [0x4d257b]. A message
 * entry may carry a pump key "p": it is delivered at its frame only after
 * that many earlier PM_REMOVE peeks in the same frame (the first peek of a
 * frame counts 0, every further peek 1 more, the empty one included). */
#ifndef ES2_SIM_ORACLE_JOURNAL_H
#define ES2_SIM_ORACLE_JOURNAL_H
#include <stddef.h>
#include <stdint.h>

/* DBSIM passes 808 as the JOYCAPSA size (0x4776ba), so a blob must hold 808 bytes. */
#define ES2_JOURNAL_BLOB_MAX 960u

enum { ES2_JOURNAL_MSG, ES2_JOURNAL_KEY, ES2_JOURNAL_JOYCAPS, ES2_JOURNAL_JOYPOS, ES2_JOURNAL_CURSOR, ES2_JOURNAL_API_COUNT };
extern const char *const es2_journal_api_names[ES2_JOURNAL_API_COUNT];

/* Window messages the journal produces or tests (winuser.h values). */
#define ES2_WM_KEYDOWN 0x0100u
#define ES2_WM_KEYUP 0x0101u
#define ES2_WM_SYSKEYDOWN 0x0104u
#define ES2_WM_SYSKEYUP 0x0105u
#define ES2_WM_MOUSEMOVE 0x0200u
#define ES2_WM_LBUTTONDOWN 0x0201u
#define ES2_WM_LBUTTONUP 0x0202u
#define ES2_WM_MOUSELAST 0x020eu
#define ES2_MK_LBUTTON 0x0001u

typedef struct es2_journal_entry {
    uint32_t frame, api, pump;
    int32_t a, b, c, d;        /* msg: message, wParam, lParam; key: vk, result; joy: id, result; cursor: x, y, result */
    uint8_t *blob;             /* malloc'd; freed by es2_journal_free */
    uint32_t blob_size;
} es2_journal_entry;

/* The scan code of a virtual key for the script form "keydown"/"keyup"
 * (the proxy passes MapVirtualKeyA(vk, MAPVK_VK_TO_VSC)). */
typedef unsigned (*es2_journal_vk_scan)(unsigned vk);

typedef struct es2_journal {
    es2_journal_entry *entries;
    uint32_t count, cursor[ES2_JOURNAL_API_COUNT];
    uint8_t keys[256];         /* virtual key state for scripts without key entries: bit 7 down, bit 0 toggle */
    int32_t mouse_x, mouse_y;  /* virtual cursor: the last injected mouse message's position */
    uint32_t pump_frame, pump_count;   /* PM_REMOVE peeks in the current frame */
    /* Statistics (tick log fields in_recorded .. in_focus_dropped). */
    uint32_t recorded, injected, dropped, fallbacks, no_window, focus_dropped;
} es2_journal;

/* Empty state: no entries, cursor (320, 240), no frame seen. */
void es2_journal_init(es2_journal *j);
/* Frees the entries and their blobs (the state is empty afterwards). */
void es2_journal_free(es2_journal *j);

/* One line of the journal (the recorded form and the script form with
 * "keydown"/"keyup" "vk" and "mousedown"/"mouseup"/"mousemove" "x"/"y"):
 * 1 and *entry, or 0 when the line is not an entry. */
int es2_journal_parse_line(const char *line, const char *end, es2_journal_entry *entry, es2_journal_vk_scan scan);
/* Parses the whole text (`size` bytes, need not be terminated) into `j`'s
 * entry table. 0 on success, 1 when out of memory. */
int es2_journal_load_text(es2_journal *j, const char *text, size_t size, es2_journal_vk_scan scan);

/* A PM_REMOVE peek at `frame` (before the entry for it is taken). */
void es2_journal_count_pump(es2_journal *j, uint32_t frame);
/* The next entry of `api` at `frame` (entries are consumed in file order
 * per API; non-message entries of earlier frames that were never requested
 * are skipped, message entries of earlier frames are delivered); a message
 * entry of this frame only when its pump key is at most the frame's peek
 * count. `match` (may be NULL) must accept the entry, else NULL and it
 * stays. */
typedef int (*es2_journal_match)(const es2_journal_entry *entry, int32_t key);
es2_journal_entry *es2_journal_take(es2_journal *j, uint32_t api, uint32_t frame, es2_journal_match match, int32_t key);
/* match: the entry's first value (vk, joystick id) equals `key`. */
int es2_journal_match_first(const es2_journal_entry *entry, int32_t key);

/* An injected message: the virtual key state and cursor follow it. */
void es2_journal_apply_message(es2_journal *j, uint32_t message, uint32_t wparam, uint32_t lparam);
/* GetKeyState's fallback from the virtual key state (counts a fallback). */
int16_t es2_journal_key_fallback(es2_journal *j, int32_t vk);
/* 1 for the keyboard and mouse messages a replay drops from the real queue. */
int es2_journal_is_input_message(uint32_t message);

#endif
