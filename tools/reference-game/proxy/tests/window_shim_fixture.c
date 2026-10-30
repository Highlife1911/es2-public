// Own-code fixture for proxy/window_shim.h. A synthetic image provides
// the thunks and IAT slots of the input journal and of the seven window
// imports; fake "real" functions count every call that would have reached
// USER32. No original game code runs here.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
static WCHAR config_path[MAX_PATH];
static HMODULE self_module;
static unsigned events_hidden, events_first;
static void log_event(const char *event) {
    printf("event %s\n", event);
    if (!strcmp(event, "window_hidden")) ++events_hidden;
    else if (!strncmp(event, "window_hidden_", 14)) ++events_first;
}
#include "../input_journal.h"
#include "../window_shim.h"

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); return 1; } } while (0)

static BYTE *image;
static unsigned real_calls[WS_API_COUNT];
static DWORD created_style[4];
static unsigned created;

static BOOL WINAPI fake_peek(LPMSG msg, HWND w, UINT a, UINT b, UINT flags) { (void)msg; (void)w; (void)a; (void)b; (void)flags; return FALSE; }
static SHORT WINAPI fake_key(int vk) { (void)vk; return 0; }
static MMRESULT WINAPI fake_joypos(UINT id, LPJOYINFOEX info) { (void)id; (void)info; return JOYERR_UNPLUGGED; }
static MMRESULT WINAPI fake_joycaps(UINT_PTR id, LPJOYCAPSA caps, UINT size) { (void)id; (void)caps; (void)size; return JOYERR_PARMS; }
static BOOL WINAPI fake_cursor(LPPOINT p) { p->x = 1; p->y = 2; return TRUE; }
static HWND WINAPI fake_create(DWORD a, LPCSTR b, LPCSTR c, DWORD style, int e, int f, int g, int h, HWND parent, HMENU i, HINSTANCE j, LPVOID k) {
    (void)a; (void)b; (void)c; (void)e; (void)f; (void)g; (void)h; (void)i; (void)j; (void)k;
    if (created < 4) created_style[created] = style;
    ++created;
    return parent ? (HWND)(uintptr_t)0x9999 : (HWND)(uintptr_t)0x1234;
}
// The real functions return values a shim must not return (so a leak is visible).
static BOOL WINAPI fake_show_window(HWND w, int c) { (void)w; (void)c; ++real_calls[WS_SHOW_WINDOW]; return 77; }
static BOOL WINAPI fake_update_window(HWND w) { (void)w; ++real_calls[WS_UPDATE_WINDOW]; return 77; }
static BOOL WINAPI fake_set_window_pos(HWND w, HWND a, int x, int y, int cx, int cy, UINT f) {
    (void)w; (void)a; (void)x; (void)y; (void)cx; (void)cy; (void)f; ++real_calls[WS_SET_WINDOW_POS]; return 77;
}
static BOOL WINAPI fake_move_window(HWND w, int x, int y, int cx, int cy, BOOL r) {
    (void)w; (void)x; (void)y; (void)cx; (void)cy; (void)r; ++real_calls[WS_MOVE_WINDOW]; return 77;
}
static BOOL WINAPI fake_set_cursor_pos(int x, int y) { (void)x; (void)y; ++real_calls[WS_SET_CURSOR_POS]; return 77; }
static BOOL WINAPI fake_clip_cursor(const RECT *r) { (void)r; ++real_calls[WS_CLIP_CURSOR]; return 77; }
static int WINAPI fake_show_cursor(BOOL s) { (void)s; ++real_calls[WS_SHOW_CURSOR]; return 77; }

static void *slot_value(DWORD iat_rva) { DWORD v; memcpy(&v, image + iat_rva, 4); return (void *)(uintptr_t)v; }
static void put_thunk(DWORD iat_rva, DWORD thunk_rva, void *target) {
    DWORD iat = (DWORD)(uintptr_t)(image + iat_rva), value = (DWORD)(uintptr_t)target;
    memcpy(image + iat_rva, &value, 4);
    image[thunk_rva] = 0xff; image[thunk_rva + 1] = 0x25;
    memcpy(image + thunk_rva + 2, &iat, 4);
}

static void *const fakes[WS_API_COUNT] = {
    (void *)fake_show_window, (void *)fake_update_window, (void *)fake_set_window_pos, (void *)fake_move_window,
    (void *)fake_set_cursor_pos, (void *)fake_clip_cursor, (void *)fake_show_cursor};

static BOOL make_image(void) {
    image = VirtualAlloc(NULL, IJ_IMAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!image) return FALSE;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)image;
    dos->e_magic = IMAGE_DOS_SIGNATURE; dos->e_lfanew = 0x80;
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(image + 0x80);
    nt->Signature = IMAGE_NT_SIGNATURE; nt->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    nt->OptionalHeader.ImageBase = (DWORD)(uintptr_t)image; nt->OptionalHeader.SizeOfImage = IJ_IMAGE_SIZE;
    void *journal[] = {(void *)fake_peek, (void *)fake_key, (void *)fake_joypos, (void *)fake_joycaps, (void *)fake_cursor, (void *)fake_create};
    for (unsigned i = 0; i < 6; ++i) put_thunk(ij_slots[i].iat_rva, ij_slots[i].thunk_rva, journal[i]);
    for (unsigned i = 0; i < WS_API_COUNT; ++i) put_thunk(ws_slots[i].iat_rva, ws_slots[i].thunk_rva, fakes[i]);
    return TRUE;
}

static BOOL write_config(const WCHAR *mode, const WCHAR *hidden) {
    WCHAR directory[MAX_PATH], journal[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"TEST_TMPDIR", directory, MAX_PATH);
    if (!n || n >= MAX_PATH - 40) return FALSE;
    swprintf(config_path, MAX_PATH, L"%ls\\es2-oracle.ini", directory);
    swprintf(journal, MAX_PATH, L"%ls\\journal.jsonl", directory);
    HANDLE f = CreateFileW(journal, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return FALSE;
    CloseHandle(f);
    DeleteFileW(config_path);
    WritePrivateProfileStringW(L"input", L"mode", mode, config_path);
    // Record mode refuses an existing journal; it writes its own file.
    if (!wcscmp(mode, L"record")) swprintf(journal, MAX_PATH, L"%ls\\recorded-%lu.jsonl", directory, (unsigned long)GetTickCount());
    WritePrivateProfileStringW(L"input", L"journal", journal, config_path);
    if (hidden) WritePrivateProfileStringW(L"window", L"hidden", hidden, config_path);
    return TRUE;
}

typedef BOOL (WINAPI *ShowWindowFn)(HWND, int);
typedef BOOL (WINAPI *UpdateWindowFn)(HWND);
typedef BOOL (WINAPI *SetWindowPosFn)(HWND, HWND, int, int, int, int, UINT);
typedef BOOL (WINAPI *MoveWindowFn)(HWND, int, int, int, int, BOOL);
typedef BOOL (WINAPI *SetCursorPosFn)(int, int);
typedef BOOL (WINAPI *ClipCursorFn)(const RECT *);
typedef int (WINAPI *ShowCursorFn)(BOOL);
#define SLOT(api, type) ((type)slot_value(ws_slots[api].iat_rva))
#define CREATE ((IjCreate)slot_value(WS_CREATE_IAT_RVA))
#define GAME_STYLE 0x100e0000u   /* WS_VISIBLE | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX (0x465158) */

// The call sequence of DBSIM (creation, toggle, warp, teardown) through the slots.
static void game_calls(void) {
    HWND w = (HWND)(uintptr_t)0x1234;
    RECT r = {0, 0, 640, 480};
    SLOT(WS_SHOW_WINDOW, ShowWindowFn)(w, SW_SHOWNORMAL);
    SLOT(WS_UPDATE_WINDOW, UpdateWindowFn)(w);
    SLOT(WS_SET_WINDOW_POS, SetWindowPosFn)(w, HWND_TOPMOST, 0, 0, 646, 505, 0x108);
    SLOT(WS_MOVE_WINDOW, MoveWindowFn)(w, 0, 0, 320, 240, TRUE);
    SLOT(WS_SET_CURSOR_POS, SetCursorPosFn)(320, 240);
    SLOT(WS_CLIP_CURSOR, ClipCursorFn)(&r);
}

static BOOL untouched(void) {
    for (unsigned i = 0; i < WS_API_COUNT; ++i) if (slot_value(ws_slots[i].iat_rva) != fakes[i]) return FALSE;
    return TRUE;
}

int wmain(int argc, WCHAR **argv) {
    (void)install_window_shim; (void)install_input_journal;
    if (argc != 2 || !make_image()) return 2;
    const WCHAR *mode = argv[1];
    if (!wcscmp(mode, L"order")) {
        // Without the input journal there is no replay mode: refused, nothing patched.
        CHECK(!install_window_shim_image(image, (uintptr_t)image));
        CHECK(slot_value(WS_CREATE_IAT_RVA) == (void *)fake_create && untouched());
        printf("PASS order\n");
        return 0;
    }
    CHECK(install_input_journal_image(image, (uintptr_t)image));
    CHECK(slot_value(WS_CREATE_IAT_RVA) == (void *)ij_create_window);
    if (!wcscmp(mode, L"mismatch")) {
        image[ws_slots[WS_CLIP_CURSOR].thunk_rva + 1] = 0x15;   // `call [iat]` instead of the thunk's `jmp`
        CHECK(!install_window_shim_image(image, (uintptr_t)image));
        CHECK(slot_value(WS_CREATE_IAT_RVA) == (void *)ij_create_window && untouched());
        CHECK(!install_window_shim_image(image, (uintptr_t)image));   // refusal is final
        printf("PASS mismatch\n");
        return 0;
    }
    CHECK(install_window_shim_image(image, (uintptr_t)image));
    CHECK(slot_value(WS_CREATE_IAT_RVA) == (void *)ws_create_window && ws_next_create == ij_create_window);
    CHECK(untouched());   // nothing but CreateWindowExA before the configuration is read
    CHECK(install_window_shim_image(image, (uintptr_t)image));   // idempotent

    if (!wcscmp(mode, L"hidden")) {
        CHECK(write_config(L"replay", L"1"));
        // A child window first (DBSIM creates none; the AVI path is dead): style kept, nothing decided.
        CHECK(CREATE(0, "c", "t", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, (HWND)(uintptr_t)0x5555, NULL, NULL, NULL) == (HWND)(uintptr_t)0x9999);
        CHECK(created_style[0] == (WS_CHILD | WS_VISIBLE) && !ws_decided && untouched());
        CHECK(CREATE(0, "ES2", "t", GAME_STYLE, 10, 20, 646, 505, NULL, NULL, NULL, NULL) == (HWND)(uintptr_t)0x1234);
        CHECK(ij_mode == IJ_REPLAY && ws_hidden && ws_state == 2 && ws_visible_stripped == 1);
        CHECK(created_style[1] == (GAME_STYLE & ~(DWORD)WS_VISIBLE) && ij_window == (HWND)(uintptr_t)0x1234);
        for (unsigned i = 0; i < WS_API_COUNT; ++i) CHECK(slot_value(ws_slots[i].iat_rva) == ws_slots[i].hook);
        HWND w = (HWND)(uintptr_t)0x1234;
        RECT r = {0, 0, 640, 480};
        CHECK(SLOT(WS_SHOW_WINDOW, ShowWindowFn)(w, SW_SHOWNORMAL) == FALSE);   // never visible before
        CHECK(SLOT(WS_UPDATE_WINDOW, UpdateWindowFn)(w) == TRUE);
        CHECK(SLOT(WS_SET_WINDOW_POS, SetWindowPosFn)(w, HWND_TOPMOST, 0, 0, 646, 505, 0x108) == TRUE);
        CHECK(SLOT(WS_MOVE_WINDOW, MoveWindowFn)(w, 0, 0, 320, 240, TRUE) == TRUE);
        CHECK(SLOT(WS_SET_CURSOR_POS, SetCursorPosFn)(320, 240) == TRUE);
        CHECK(SLOT(WS_CLIP_CURSOR, ClipCursorFn)(&r) == TRUE && SLOT(WS_CLIP_CURSOR, ClipCursorFn)(NULL) == TRUE);
        CHECK(SLOT(WS_SHOW_CURSOR, ShowCursorFn)(FALSE) == -1);
        CHECK(SLOT(WS_SHOW_CURSOR, ShowCursorFn)(FALSE) == -2);
        CHECK(SLOT(WS_SHOW_CURSOR, ShowCursorFn)(TRUE) == -1);
        CHECK(SLOT(WS_SHOW_CURSOR, ShowCursorFn)(TRUE) == 0);
        for (unsigned i = 0; i < WS_API_COUNT; ++i) CHECK(real_calls[i] == 0);   // USER32 never reached
        CHECK(ws_calls[WS_SHOW_WINDOW] == 1 && ws_calls[WS_CLIP_CURSOR] == 2 && ws_calls[WS_SHOW_CURSOR] == 4);
        CHECK(events_hidden == 1 && events_first == WS_API_COUNT);   // one event per import, not per call
        // A replayed GetCursorPos is unaffected by the warp: the virtual cursor stays (320,240).
        POINT p = {0, 0};
        CHECK(((IjCursor)slot_value(ij_slots[4].iat_rva))(&p) && p.x == 320 && p.y == 240);
        // A second top-level window is created hidden as well; the slots are patched once.
        CHECK(CREATE(0, "x", "t", WS_VISIBLE | WS_POPUP, 0, 0, 1, 1, NULL, NULL, NULL, NULL) && created_style[2] == WS_POPUP);
        CHECK(ws_visible_stripped == 2 && events_hidden == 1);
    } else if (!wcscmp(mode, L"visible") || !wcscmp(mode, L"record") || !wcscmp(mode, L"off") || !wcscmp(mode, L"zero")) {
        // Replay without the key, hidden=0, record and off with hidden=1: no slot changes, USER32 reached.
        const WCHAR *input = !wcscmp(mode, L"record") ? L"record" : !wcscmp(mode, L"off") ? L"off" : L"replay";
        const WCHAR *hidden = !wcscmp(mode, L"visible") ? NULL : !wcscmp(mode, L"zero") ? L"0" : L"1";
        CHECK(write_config(input, hidden));
        CHECK(CREATE(0, "ES2", "t", GAME_STYLE, 10, 20, 646, 505, NULL, NULL, NULL, NULL) == (HWND)(uintptr_t)0x1234);
        CHECK(created_style[0] == GAME_STYLE && !ws_hidden && ws_state == 1 && ws_visible_stripped == 0);
        CHECK(untouched());
        game_calls();
        CHECK(SLOT(WS_SHOW_CURSOR, ShowCursorFn)(FALSE) == 77);
        for (unsigned i = 0; i < WS_API_COUNT; ++i) CHECK(real_calls[i] == 1 && ws_calls[i] == 0);
        CHECK(events_hidden == 0 && events_first == 0);
        if (!wcscmp(mode, L"record")) { CHECK(ij_mode == IJ_RECORD); input_journal_shutdown(); }
        if (!wcscmp(mode, L"off")) CHECK(ij_mode == IJ_OFF);
    } else {
        return 2;
    }
    printf("PASS %ls\n", mode);
    return 0;
}
