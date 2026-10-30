// DBSIM 1.11 headless replay window (oracle_regression.md, "Headless window"). In replay mode with `[window]
// hidden=1` in es2-oracle.ini the game window is created without WS_VISIBLE
// and the seven USER32 imports that show, activate, move or size the window
// or move/clip the mouse cursor answer without calling USER32. Record mode,
// mode off and interactive runs (which never write the key) are untouched:
// then not a single IAT slot below changes.
//
// Every call site in the reference DBSIM.EXE (llvm-objdump listing plus a raw
// scan of the code section for `call rel32` to the thunks and for direct IAT
// references; docs/reference-game/oracle_regression.md "Headless window"):
//   ShowWindow    0x4652cf  window creation 0x465158, nCmdShow of WinMain; result discarded
//   UpdateWindow  0x4652db  same; result discarded (EAX <- 1 at 0x4652e0)
//   SetWindowPos  0x4652ac (DirectDraw at start), 0x464bff (centre helper 0x464b2c),
//                 0x466400/0x46641d (private message 0x812 after WM_DISPLAYCHANGE),
//                 0x4668c2/0x466989 (window/full-screen toggle 0x4667c8); results discarded
//   MoveWindow    0x4718a7  AVI player 0x4718f8, which has no static reference (dead)
//   SetCursorPos  0x48093e  warp helper 0x4808dc (callers ignore EAX), 0x466923 (toggle)
//   ClipCursor    0x4667bf  teardown 0x466730 at every exit, 0x46690e (toggle)
//   ShowCursor    0x466933, 0x466999 (toggle, only with a cursor sprite); results discarded
// No caller reads a result, so the values below only follow the documented
// API contract for a window that is never shown.
//
// Installation: DllMain chains the CreateWindowExA slot after the input
// journal's wrapper (input_journal.h must already be installed). The first
// top-level CreateWindowExA reads the configuration (after the journal's) and,
// only for replay + hidden, patches the seven slots before the window exists.
// The executable file is unchanged; slots are replaced in memory only after
// the image and every `jmp dword [iat]` thunk were checked.
#ifndef ES2_WINDOW_SHIM_H
#define ES2_WINDOW_SHIM_H
#include <stdint.h>

#ifndef ES2_INPUT_JOURNAL_H
#error "window_shim.h needs input_journal.h (replay mode, configuration and the CreateWindowExA wrapper)"
#endif

enum { WS_SHOW_WINDOW, WS_UPDATE_WINDOW, WS_SET_WINDOW_POS, WS_MOVE_WINDOW,
       WS_SET_CURSOR_POS, WS_CLIP_CURSOR, WS_SHOW_CURSOR, WS_API_COUNT };
static const char *const ws_api_names[WS_API_COUNT] = {
    "ShowWindow", "UpdateWindow", "SetWindowPos", "MoveWindow", "SetCursorPos", "ClipCursor", "ShowCursor"};
#define WS_CREATE_IAT_RVA 0x002cc458u
#define WS_CREATE_THUNK_RVA 0x0009698eu

static BOOL WINAPI ws_show_window(HWND window, int command);
static BOOL WINAPI ws_update_window(HWND window);
static BOOL WINAPI ws_set_window_pos(HWND window, HWND after, int x, int y, int width, int height, UINT flags);
static BOOL WINAPI ws_move_window(HWND window, int x, int y, int width, int height, BOOL repaint);
static BOOL WINAPI ws_set_cursor_pos(int x, int y);
static BOOL WINAPI ws_clip_cursor(const RECT *rect);
static int WINAPI ws_show_cursor(BOOL show);

struct WsSlot { DWORD iat_rva, thunk_rva; void *hook; };
// Slot RVAs from the import table of the reference DBSIM.EXE (USER32.dll
// FirstThunk entries), thunks from the `jmp dword [iat]` table at 0x4968xx.
static const struct WsSlot ws_slots[WS_API_COUNT] = {
    {0x002cc478u, 0x000969beu, (void *)ws_show_window},
    {0x002cc470u, 0x000969b2u, (void *)ws_update_window},
    {0x002cc484u, 0x000969d0u, (void *)ws_set_window_pos},
    {0x002cc410u, 0x00096922u, (void *)ws_move_window},
    {0x002cc490u, 0x000969e2u, (void *)ws_set_cursor_pos},
    {0x002cc45cu, 0x00096994u, (void *)ws_clip_cursor},
    {0x002cc47cu, 0x000969c4u, (void *)ws_show_cursor},
};

static BYTE *ws_image;
static volatile LONG ws_state;      // 0 not installed, 1 create chained, 2 slots patched, -1 refused
static IjCreate ws_next_create;     // the input journal's wrapper
static BOOL ws_hidden;              // replay + [window] hidden=1, decided at the first top-level window
static BOOL ws_decided;
static DWORD ws_calls[WS_API_COUNT];
static DWORD ws_visible_stripped;   // top-level windows created without WS_VISIBLE
static LONG ws_cursor_count;        // ShowCursor display counter (0 = mouse installed)

static void ws_count(unsigned api) {
    if (!ws_calls[api]++) {
        char event[64];
        snprintf(event, sizeof(event), "window_hidden_%s", ws_api_names[api]);
        log_event(event);
    }
}

// The window was never shown, so "previously visible" is FALSE.
static BOOL WINAPI ws_show_window(HWND window, int command) { (void)window; (void)command; ws_count(WS_SHOW_WINDOW); return FALSE; }
static BOOL WINAPI ws_update_window(HWND window) { (void)window; ws_count(WS_UPDATE_WINDOW); return TRUE; }
static BOOL WINAPI ws_set_window_pos(HWND window, HWND after, int x, int y, int width, int height, UINT flags) {
    (void)window; (void)after; (void)x; (void)y; (void)width; (void)height; (void)flags;
    ws_count(WS_SET_WINDOW_POS);
    return TRUE;
}
static BOOL WINAPI ws_move_window(HWND window, int x, int y, int width, int height, BOOL repaint) {
    (void)window; (void)x; (void)y; (void)width; (void)height; (void)repaint;
    ws_count(WS_MOVE_WINDOW);
    return TRUE;
}
// The replay's GetCursorPos answers from the journal or the virtual cursor of
// input_journal.h, never from the real cursor, so a warp changes nothing a
// replay reads.
static BOOL WINAPI ws_set_cursor_pos(int x, int y) { (void)x; (void)y; ws_count(WS_SET_CURSOR_POS); return TRUE; }
static BOOL WINAPI ws_clip_cursor(const RECT *rect) { (void)rect; ws_count(WS_CLIP_CURSOR); return TRUE; }
// The display counter the real function returns: +1 per show, -1 per hide.
static int WINAPI ws_show_cursor(BOOL show) {
    ws_count(WS_SHOW_CURSOR);
    return (int)(show ? ++ws_cursor_count : --ws_cursor_count);
}

static BOOL ws_thunk_valid(const BYTE *image, uintptr_t base, DWORD iat_rva, DWORD thunk_rva) {
    BYTE thunk[6] = {0xff, 0x25};
    DWORD iat = (DWORD)(base + iat_rva);
    memcpy(thunk + 2, &iat, 4);
    return !memcmp(image + thunk_rva, thunk, sizeof(thunk));
}

static BOOL ws_image_valid(BYTE *image, uintptr_t expected_base) {
    if ((uintptr_t)image != expected_base || sizeof(void *) != 4) return FALSE;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)image;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < (LONG)sizeof(*dos) ||
        dos->e_lfanew > 4096 - (LONG)sizeof(IMAGE_NT_HEADERS32)) return FALSE;
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(image + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt->OptionalHeader.ImageBase != expected_base || nt->OptionalHeader.SizeOfImage != IJ_IMAGE_SIZE) return FALSE;
    if (!ws_thunk_valid(image, expected_base, WS_CREATE_IAT_RVA, WS_CREATE_THUNK_RVA)) return FALSE;
    for (unsigned i = 0; i < WS_API_COUNT; ++i)
        if (!ws_thunk_valid(image, expected_base, ws_slots[i].iat_rva, ws_slots[i].thunk_rva)) return FALSE;
    return TRUE;
}

static BOOL ws_replace_slot(DWORD iat_rva, void *hook, void **previous) {
    DWORD *slot = (DWORD *)(ws_image + iat_rva);
    DWORD protection, ignored;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection)) return FALSE;
    if (previous) *previous = (void *)(uintptr_t)*slot;
    InterlockedExchange((LONG *)slot, (LONG)(uintptr_t)hook);
    VirtualProtect(slot, sizeof(*slot), protection, &ignored);
    return TRUE;
}

// Main thread, at the first top-level CreateWindowExA (before the window
// exists, so no message of its creation can reach a shimmed import early).
static void ws_patch_slots(void) {
    for (unsigned i = 0; i < WS_API_COUNT; ++i)
        if (!ws_replace_slot(ws_slots[i].iat_rva, ws_slots[i].hook, NULL)) { log_event("window_hidden_refused"); return; }
    InterlockedExchange(&ws_state, 2);
    log_event("window_hidden");
}

static void ws_decide(void) {
    if (ws_decided) return;
    ws_decided = TRUE;
    ij_ready();   // input mode and config_path (input_journal.h)
    ws_hidden = ij_mode == IJ_REPLAY && config_path[0] &&
        GetPrivateProfileIntW(L"window", L"hidden", 0, config_path) != 0;
    if (ws_hidden) ws_patch_slots();
    if (ws_state != 2) ws_hidden = FALSE;
}

static HWND WINAPI ws_create_window(DWORD ex_style, LPCSTR class_name, LPCSTR title, DWORD style, int x, int y,
                                     int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param) {
    if (!parent) {
        ws_decide();
        if (ws_hidden && (style & WS_VISIBLE)) { style &= ~(DWORD)WS_VISIBLE; ++ws_visible_stripped; }
    }
    return ws_next_create(ex_style, class_name, title, style, x, y, width, height, parent, menu, instance, param);
}

// DllMain-safe: no file I/O. Chains CreateWindowExA after the input journal.
static BOOL install_window_shim_image(BYTE *image, uintptr_t expected_base) {
    if (InterlockedCompareExchange(&ws_state, -1, 0) != 0) return ws_state > 0;
    DWORD current;
    if (ij_iat_state != 1 || ij_image != image || !ws_image_valid(image, expected_base)) return FALSE;
    memcpy(&current, image + WS_CREATE_IAT_RVA, sizeof(current));
    if (current != (DWORD)(uintptr_t)ij_create_window) return FALSE;
    ws_image = image;
    if (!ws_replace_slot(WS_CREATE_IAT_RVA, (void *)ws_create_window, (void **)&ws_next_create)) return FALSE;
    InterlockedExchange(&ws_state, 1);
    return TRUE;
}

static void install_window_shim(void) {
    if (ij_iat_state != 1) return;   // install_input_journal() checked the executable name and image
    install_window_shim_image(ij_image, (uintptr_t)0x00400000);
}
#endif
