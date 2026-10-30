// DBSIM 1.11 input journal: records and replays the simulator's input APIs,
// keyed by the active-loop frame counter [0x004d257b].
//
// DBSIM reads input only through these imports (docs/reference-game/input_journal.md):
//   PeekMessageA      message pump 0x00464c78 / 0x004659e8 -> WndProc 0x00466034
//                     -> handler chain 0x004d3b30 (keyboard handler 0x00477bec)
//   GetKeyState       modifier state inside the keyboard handler
//   joyGetDevCapsA, joyGetPosEx   joystick poll 0x00477690
//   GetCursorPos      mouse position
// CreateWindowExA is wrapped only to learn the main window handle for injected
// messages. Each IAT slot is replaced in memory from DllMain after checking the
// image and the `jmp dword [iat]` thunk; the executable file is unchanged.
//
// record: real results are passed through and written to the journal.
// replay: journal messages are injected at their frame, real keyboard/mouse
//         messages are discarded, other messages pass; GetKeyState, joystick
//         and cursor results come from the journal (fallbacks are counted).
//
// Pump key: the frame counter stops while a modal game screen runs its own
// loop, but that loop keeps pumping messages. A message entry may therefore
// carry "p": it is injected at its frame only after that many earlier
// PM_REMOVE PeekMessageA calls in the same frame. The count depends on the
// real message traffic of the session, so "p" is for scripts answering modal
// screens only (simulation state cannot change while the frame counter stands
// still). Record mode does not write it: in ordinary frames a recorded count
// could push a key into a later frame and break replay determinism.
//
// Focus: DBSIM pauses on WM_KILLFOCUS (WndProc 0x00466369 -> 0x0045f0d4) and
// resumes on WM_SETFOCUS (0x00466373 -> 0x0045f108); WM_POWERBROADCAST
// suspend/resume (0x004664d1) does the same. These are sent, not posted, so
// the PeekMessageA hook never sees them. In replay mode the main window is
// subclassed and these messages are kept from the game: a replayed session
// always behaves as if it kept the focus, independent of the desktop.
#ifndef ES2_INPUT_JOURNAL_H
#define ES2_INPUT_JOURNAL_H
#include <stdint.h>
#include <stdlib.h>
#include <mmsystem.h>
// The parser, the entry table, the per-frame pump count, the virtual
// key state and cursor and the counters are the shared library
// //sim/oracle:journal, which the own process links too. This header keeps
// the IAT wrappers, the record writer, the configuration and the focus
// subclass; the ij_* names below are the library state's fields.
#include "sim/oracle/journal.h"

#define IJ_IMAGE_SIZE 0x002d8000u
#define IJ_FRAME_RVA 0x000d257bu
#define IJ_LINE_MAX 2048u
// DBSIM passes 808 as the JOYCAPSA size (0x4776ba; oracle_suites.md, "scripted joystick"), so a caps blob must fit 808 bytes;
// a line keeps 2 * IJ_BLOB_MAX hex digits plus the fields below IJ_LINE_MAX.
#define IJ_BLOB_MAX ES2_JOURNAL_BLOB_MAX

enum { IJ_OFF, IJ_RECORD, IJ_REPLAY };
enum { IJ_API_MSG = ES2_JOURNAL_MSG, IJ_API_KEY = ES2_JOURNAL_KEY, IJ_API_JOYCAPS = ES2_JOURNAL_JOYCAPS,
       IJ_API_JOYPOS = ES2_JOURNAL_JOYPOS, IJ_API_CURSOR = ES2_JOURNAL_CURSOR, IJ_API_COUNT = ES2_JOURNAL_API_COUNT };
#define ij_api_names es2_journal_api_names

typedef BOOL (WINAPI *IjPeek)(LPMSG, HWND, UINT, UINT, UINT);
typedef SHORT (WINAPI *IjKeyState)(int);
typedef MMRESULT (WINAPI *IjJoyPos)(UINT, LPJOYINFOEX);
typedef MMRESULT (WINAPI *IjJoyCaps)(UINT_PTR, LPJOYCAPSA, UINT);
typedef BOOL (WINAPI *IjCursor)(LPPOINT);
typedef HWND (WINAPI *IjCreate)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);

struct IjSlot { DWORD iat_rva, thunk_rva; void *hook; void **real; };

static IjPeek ij_real_peek;
static IjKeyState ij_real_key;
static IjJoyPos ij_real_joypos;
static IjJoyCaps ij_real_joycaps;
static IjCursor ij_real_cursor;
static IjCreate ij_real_create;
static BYTE *ij_image;
static volatile LONG ij_iat_state;
static INIT_ONCE ij_once = INIT_ONCE_STATIC_INIT;
static int ij_mode;
static HWND ij_window;
static HANDLE ij_file = INVALID_HANDLE_VALUE;
static char ij_buffer[65536];
static DWORD ij_used;
// The replay state (entries, cursors, virtual keys and cursor, the pump
// count) and the statistics (also written to the tick log): the library's.
static es2_journal ij_state = {NULL, 0, {0}, {0}, 320, 240, 0xffffffffu, 0, 0, 0, 0, 0, 0, 0};
#define ij_entry_count (ij_state.count)
#define ij_mouse_x (ij_state.mouse_x)
#define ij_mouse_y (ij_state.mouse_y)
#define ij_recorded (ij_state.recorded)
#define ij_injected (ij_state.injected)
#define ij_dropped (ij_state.dropped)
#define ij_fallbacks (ij_state.fallbacks)
#define ij_no_window (ij_state.no_window)
#define ij_focus_dropped (ij_state.focus_dropped)
static WNDPROC ij_game_window_proc;
static DWORD ij_last_frame = 0xffffffffu;

static DWORD ij_frame(void) {
    DWORD frame;
    memcpy(&frame, ij_image + IJ_FRAME_RVA, sizeof(frame));
    return frame;
}

// ---- journal writing (record) --------------------------------------------
static void ij_flush(void) {
    DWORD written;
    if (ij_used && ij_file != INVALID_HANDLE_VALUE) WriteFile(ij_file, ij_buffer, ij_used, &written, NULL);
    ij_used = 0;
}

static void ij_write(const char *line, int length) {
    if (length <= 0 || (DWORD)length > sizeof(ij_buffer)) return;
    if (ij_used + (DWORD)length > sizeof(ij_buffer)) ij_flush();
    memcpy(ij_buffer + ij_used, line, (size_t)length);
    ij_used += (DWORD)length;
    ++ij_recorded;
}

static int ij_hex(char *out, const BYTE *bytes, DWORD size) {
    static const char digits[] = "0123456789abcdef";
    for (DWORD i = 0; i < size; ++i) { out[2 * i] = digits[bytes[i] >> 4]; out[2 * i + 1] = digits[bytes[i] & 15]; }
    return (int)(2 * size);
}

static void ij_record(DWORD api, LONG a, LONG b, LONG c, LONG d, const void *blob, DWORD blob_size) {
    char line[IJ_LINE_MAX];
    // Write completed frames at once: a terminated process loses at most the current frame.
    if (ij_frame() != ij_last_frame) { ij_flush(); ij_last_frame = ij_frame(); }
    int n = snprintf(line, sizeof(line), "{\"f\":%lu,\"api\":\"%s\",\"a\":%ld,\"b\":%ld,\"c\":%ld,\"d\":%ld",
        (unsigned long)ij_frame(), ij_api_names[api], (long)a, (long)b, (long)c, (long)d);
    if (blob && blob_size <= IJ_BLOB_MAX) {
        n += snprintf(line + n, sizeof(line) - (size_t)n, ",\"hex\":\"");
        n += ij_hex(line + n, blob, blob_size);
        line[n++] = '"';
    }
    n += snprintf(line + n, sizeof(line) - (size_t)n, "}\r\n");
    ij_write(line, n);
}

// ---- journal reading (replay) ---------------------------------------------
static unsigned ij_vk_scan(unsigned vk) { return MapVirtualKeyA(vk, 0 /* MAPVK_VK_TO_VSC */); }

static BOOL ij_load(const WCHAR *path) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    DWORD size = GetFileSize(file, NULL), got = 0;
    char *text = size != INVALID_FILE_SIZE ? malloc((size_t)size + 1) : NULL;
    BOOL ok = text && ReadFile(file, text, size, &got, NULL) && got == size;
    CloseHandle(file);
    if (ok) ok = !es2_journal_load_text(&ij_state, text, size, ij_vk_scan);
    free(text);
    return ok;
}

// ---- message trace (off unless [input] message_trace=1) -------------------
// Diagnostics only: every message a PM_REMOVE PeekMessageA call removes that is
// not an injected journal entry, keyed by frame and pump ordinal, plus the
// injected entries and one summary per frame, written to
// "<[trace] tick_log>.messages.ndjson" (docs/reverse-engineering/main_loop.md,
// "the message trace of the replay pump"). DBSIM's two PeekMessageA calls are `call 0x49691c` (the thunk of IAT
// 0x6cc40c) at 0x464ca2 and 0x465a05, both (msg, 0, 0, 0, PM_REMOVE). With the
// key absent nothing below runs: mt_on stays 0 and no file is created.
static int mt_on;
static HANDLE mt_file = INVALID_HANDLE_VALUE;
static char mt_buffer[65536];
static DWORD mt_used, mt_thread, mt_other_thread;
static DWORD mt_frame = 0xffffffffu;
// Per frame: PM_REMOVE calls (the next one's ordinal, the journal's "p"), calls
// that returned FALSE, removed messages, injected and dropped ones, other calls.
static DWORD mt_pumps, mt_empty, mt_messages, mt_injected, mt_dropped, mt_noremove;

static void mt_flush(void) {
    DWORD written;
    if (mt_used && mt_file != INVALID_HANDLE_VALUE) WriteFile(mt_file, mt_buffer, mt_used, &written, NULL);
    mt_used = 0;
}

static void mt_write(const char *line, int length) {
    if (length <= 0 || (DWORD)length > sizeof(mt_buffer)) return;
    if (mt_used + (DWORD)length > sizeof(mt_buffer)) mt_flush();
    memcpy(mt_buffer + mt_used, line, (size_t)length);
    mt_used += (DWORD)length;
}

static void mt_frame_line(void) {
    char line[256];
    if (mt_frame == 0xffffffffu) return;
    int n = snprintf(line, sizeof(line),
        "{\"k\":\"frame\",\"f\":%lu,\"pumps\":%lu,\"empty\":%lu,\"msgs\":%lu,\"inject\":%lu,\"drop\":%lu,\"noremove\":%lu}\r\n",
        (unsigned long)mt_frame, (unsigned long)mt_pumps, (unsigned long)mt_empty, (unsigned long)mt_messages,
        (unsigned long)mt_injected, (unsigned long)mt_dropped, (unsigned long)mt_noremove);
    mt_write(line, n);
}

// The VA of the `call rel32` (E8) that returned to `ret`, or 0 when `ret` does
// not follow one inside the image (a caller outside DBSIM, e.g. own code).
static DWORD mt_call_site(uintptr_t ret) {
    uintptr_t base = (uintptr_t)ij_image;
    if (!ij_image || ret < base + 5 || ret >= base + IJ_IMAGE_SIZE) return 0;
    return ((const BYTE *)ret)[-5] == 0xe8 ? (DWORD)(ret - 5) : 0;
}

// Called first in every traced PeekMessageA call: closes the previous frame's
// summary when the frame counter moved and returns this call's pump ordinal
// (PM_REMOVE calls of this frame before it; 0xffffffff for other calls).
static DWORD mt_begin(UINT flags) {
    DWORD frame = ij_frame();
    if (frame != mt_frame) {
        mt_frame_line();
        mt_flush();   // completed frames are on disk: a terminated process loses only the current one
        mt_frame = frame;
        mt_pumps = mt_empty = mt_messages = mt_injected = mt_dropped = mt_noremove = 0;
    }
    if (!(flags & PM_REMOVE)) { ++mt_noremove; return 0xffffffffu; }
    return mt_pumps++;
}

// kind: "msg" (the real PeekMessageA removed it and the game gets it), "drop"
// (real keyboard/mouse input removed and withheld in replay), "inject" (a journal entry).
static void mt_message(const char *kind, DWORD pump, const MSG *msg, uintptr_t ret) {
    char line[320];
    int n = snprintf(line, sizeof(line),
        "{\"k\":\"%s\",\"f\":%lu,\"p\":%lu,\"m\":%lu,\"w\":%lu,\"l\":%lu,\"h\":%lu,\"t\":%lu,\"c\":\"0x%lx\",\"r\":\"0x%lx\"}\r\n",
        kind, (unsigned long)mt_frame, (unsigned long)pump, (unsigned long)msg->message, (unsigned long)(DWORD)msg->wParam,
        (unsigned long)(DWORD)msg->lParam, (unsigned long)(DWORD)(uintptr_t)msg->hwnd, (unsigned long)msg->time,
        (unsigned long)mt_call_site(ret), (unsigned long)(DWORD)ret);
    mt_write(line, n);
}

// The end of a traced PM_REMOVE call that returned the real PeekMessageA result.
static BOOL mt_result(BOOL result, DWORD pump, const MSG *msg, uintptr_t ret) {
    if (pump == 0xffffffffu) return result;
    if (!result) { ++mt_empty; return result; }
    ++mt_messages;
    mt_message("msg", pump, msg, ret);
    return result;
}

// Only the thread of the first traced call (DBSIM's main thread) is traced;
// calls from other threads are counted in the end record.
static BOOL mt_traced(void) {
    if (!mt_on) return FALSE;
    DWORD thread = GetCurrentThreadId();
    if (!mt_thread) mt_thread = thread;
    if (thread == mt_thread) return TRUE;
    ++mt_other_thread;
    return FALSE;
}

static void mt_configure(void) {
    WCHAR path[MAX_PATH];
    if (GetPrivateProfileIntW(L"input", L"message_trace", 0, config_path) != 1) return;
    DWORD n = GetPrivateProfileStringW(L"trace", L"tick_log", L"", path, MAX_PATH, config_path);
    if (!n || n + wcslen(L".messages.ndjson") >= MAX_PATH) { log_event("message_trace_refused"); return; }
    wcscat(path, L".messages.ndjson");
    mt_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (mt_file == INVALID_HANDLE_VALUE) { log_event("message_trace_refused"); return; }
    static const char header[] = "{\"event\":\"start\",\"schema\":1,\"kind\":\"message_trace\",\"key\":\"frame_counter_0x4d257b\","
                                 "\"pump\":\"pm_remove_calls_in_frame\"}\r\n";
    mt_write(header, (int)sizeof(header) - 1);
    mt_flush();
    mt_on = 1;
    log_event("message_trace_on");
}

static void mt_shutdown(void) {
    if (mt_file == INVALID_HANDLE_VALUE) return;
    char line[128];
    mt_frame_line();
    int n = snprintf(line, sizeof(line), "{\"event\":\"end\",\"other_thread\":%lu}\r\n", (unsigned long)mt_other_thread);
    mt_write(line, n);
    mt_flush();
    CloseHandle(mt_file);
    mt_file = INVALID_HANDLE_VALUE;
    mt_on = 0;
}

// ---- configuration ------------------------------------------------------
static BOOL CALLBACK ij_configure(PINIT_ONCE once, PVOID arg, PVOID *context) {
    (void)once; (void)arg; (void)context;
    if (!config_path[0]) {
        DWORD n = GetModuleFileNameW(self_module, config_path, MAX_PATH);
        WCHAR *slash = n && n < MAX_PATH ? wcsrchr(config_path, L'\\') : NULL;
        if (!slash || slash - config_path + 1 + wcslen(L"es2-oracle.ini") >= MAX_PATH) { config_path[0] = 0; return TRUE; }
        wcscpy(slash + 1, L"es2-oracle.ini");
    }
    mt_configure();
    WCHAR mode[16], path[MAX_PATH];
    GetPrivateProfileStringW(L"input", L"mode", L"off", mode, 16, config_path);
    GetPrivateProfileStringW(L"input", L"journal", L"", path, MAX_PATH, config_path);
    if (!_wcsicmp(mode, L"record") && path[0]) {
        ij_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (ij_file == INVALID_HANDLE_VALUE) { log_event("input_journal_output_refused"); return TRUE; }
        static const char header[] = "{\"event\":\"start\",\"schema\":1,\"mode\":\"record\",\"key\":\"frame_counter_0x4d257b\"}\r\n";
        DWORD written;
        WriteFile(ij_file, header, sizeof(header) - 1, &written, NULL);
        ij_mode = IJ_RECORD;
        log_event("input_journal_record");
    } else if (!_wcsicmp(mode, L"replay") && path[0]) {
        if (!ij_load(path)) { log_event("input_journal_replay_unreadable"); return TRUE; }
        ij_mode = IJ_REPLAY;
        log_event("input_journal_replay");
    }
    return TRUE;
}

static void ij_ready(void) { InitOnceExecuteOnce(&ij_once, ij_configure, NULL, NULL); }

// ---- hooks --------------------------------------------------------------
static BOOL ij_is_focus_message(UINT message, WPARAM wp) {
    return message == WM_KILLFOCUS || message == WM_SETFOCUS ||
        (message == WM_POWERBROADCAST && (wp == 4 /* PBT_APMSUSPEND */ || wp == 7 /* PBT_APMRESUMESUSPEND */));
}

static LRESULT CALLBACK ij_window_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (ij_is_focus_message(message, wp)) {
        ++ij_focus_dropped;
        return DefWindowProcA(window, message, wp, lp);
    }
    return CallWindowProcA(ij_game_window_proc, window, message, wp, lp);
}

static HWND WINAPI ij_create_window(DWORD ex_style, LPCSTR class_name, LPCSTR title, DWORD style, int x, int y,
                                     int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param) {
    HWND window = ij_real_create(ex_style, class_name, title, style, x, y, width, height, parent, menu, instance, param);
    if (window && !parent && !ij_window) {
        ij_window = window;
        ij_ready();
        if (ij_mode == IJ_REPLAY && IsWindow(window))
            ij_game_window_proc = (WNDPROC)(uintptr_t)SetWindowLongA(window, GWL_WNDPROC, (LONG)(uintptr_t)ij_window_proc);
    }
    return window;
}

static void ij_count_pump(void) { es2_journal_count_pump(&ij_state, ij_frame()); }

static BOOL WINAPI ij_peek_message(LPMSG msg, HWND window, UINT first, UINT last, UINT flags) {
    ij_ready();
    // Only with [input] message_trace=1; otherwise traced is FALSE and every path below is unchanged.
    BOOL traced = mt_traced();
    uintptr_t ret = traced ? (uintptr_t)__builtin_return_address(0) : 0;
    DWORD pump = traced ? mt_begin(flags) : 0xffffffffu;
    if (ij_mode == IJ_REPLAY && (flags & PM_REMOVE)) ij_count_pump();
    if (ij_mode == IJ_RECORD) {
        BOOL result = ij_real_peek(msg, window, first, last, flags);
        if (result && (flags & PM_REMOVE) && es2_journal_is_input_message((uint32_t)msg->message))
            ij_record(IJ_API_MSG, (LONG)msg->message, (LONG)msg->wParam, (LONG)msg->lParam, 0, NULL, 0);
        return traced ? mt_result(result, pump, msg, ret) : result;
    }
    if (ij_mode != IJ_REPLAY || !(flags & PM_REMOVE)) {
        BOOL result = ij_real_peek(msg, window, first, last, flags);
        return traced ? mt_result(result, pump, msg, ret) : result;
    }
    BOOL filter_ok = !window && !first && !last;
    if (filter_ok) {
        es2_journal_entry *entry = es2_journal_take(&ij_state, IJ_API_MSG, ij_frame(), NULL, 0);
        if (entry) {
            if (!ij_window) ++ij_no_window;
            memset(msg, 0, sizeof(*msg));
            msg->hwnd = ij_window;
            msg->message = (UINT)entry->a;
            msg->wParam = (WPARAM)(DWORD)entry->b;
            msg->lParam = (LPARAM)entry->c;
            es2_journal_apply_message(&ij_state, (uint32_t)msg->message, (uint32_t)msg->wParam, (uint32_t)msg->lParam);
            ++ij_injected;
            if (traced) { ++mt_injected; mt_message("inject", pump, msg, ret); }
            return TRUE;
        }
    }
    for (;;) {
        BOOL result = ij_real_peek(msg, window, first, last, flags);
        if (!result || !es2_journal_is_input_message((uint32_t)msg->message)) return traced ? mt_result(result, pump, msg, ret) : result;
        ++ij_dropped;   // real keyboard/mouse input is not part of a replay
        if (traced) { ++mt_dropped; mt_message("drop", pump, msg, ret); }
    }
}

static SHORT WINAPI ij_get_key_state(int vk) {
    ij_ready();
    if (ij_mode == IJ_RECORD) {
        SHORT result = ij_real_key(vk);
        ij_record(IJ_API_KEY, vk, result, 0, 0, NULL, 0);
        return result;
    }
    if (ij_mode != IJ_REPLAY) return ij_real_key(vk);
    es2_journal_entry *entry = es2_journal_take(&ij_state, IJ_API_KEY, ij_frame(), es2_journal_match_first, vk);
    if (entry) return (SHORT)entry->b;
    return (SHORT)es2_journal_key_fallback(&ij_state, vk);
}

static MMRESULT WINAPI ij_joy_get_pos(UINT id, LPJOYINFOEX info) {
    ij_ready();
    if (ij_mode == IJ_RECORD) {
        MMRESULT result = ij_real_joypos(id, info);
        DWORD size = info && info->dwSize <= IJ_BLOB_MAX ? info->dwSize : 0;
        ij_record(IJ_API_JOYPOS, (LONG)id, (LONG)result, 0, 0, size ? info : NULL, size);
        return result;
    }
    if (ij_mode != IJ_REPLAY) return ij_real_joypos(id, info);
    es2_journal_entry *entry = es2_journal_take(&ij_state, IJ_API_JOYPOS, ij_frame(), es2_journal_match_first, (int32_t)id);
    if (!entry) { ++ij_fallbacks; return JOYERR_UNPLUGGED; }
    if (info && entry->blob && entry->blob_size == info->dwSize) memcpy(info, entry->blob, entry->blob_size);
    return (MMRESULT)entry->b;
}

static MMRESULT WINAPI ij_joy_get_caps(UINT_PTR id, LPJOYCAPSA caps, UINT size) {
    ij_ready();
    if (ij_mode == IJ_RECORD) {
        MMRESULT result = ij_real_joycaps(id, caps, size);
        ij_record(IJ_API_JOYCAPS, (LONG)id, (LONG)result, (LONG)size, 0, caps && size <= IJ_BLOB_MAX ? caps : NULL, size);
        return result;
    }
    if (ij_mode != IJ_REPLAY) return ij_real_joycaps(id, caps, size);
    es2_journal_entry *entry = es2_journal_take(&ij_state, IJ_API_JOYCAPS, ij_frame(), es2_journal_match_first, (int32_t)id);
    if (!entry) { ++ij_fallbacks; return JOYERR_PARMS; }
    if (caps && entry->blob && entry->blob_size == size) memcpy(caps, entry->blob, size);
    return (MMRESULT)entry->b;
}

static BOOL WINAPI ij_get_cursor_pos(LPPOINT point) {
    ij_ready();
    if (ij_mode == IJ_RECORD) {
        BOOL result = ij_real_cursor(point);
        ij_record(IJ_API_CURSOR, point ? point->x : 0, point ? point->y : 0, result, 0, NULL, 0);
        return result;
    }
    if (ij_mode != IJ_REPLAY) return ij_real_cursor(point);
    es2_journal_entry *entry = es2_journal_take(&ij_state, IJ_API_CURSOR, ij_frame(), NULL, 0);
    if (!entry) ++ij_fallbacks;
    if (point) { point->x = entry ? entry->a : ij_mouse_x; point->y = entry ? entry->b : ij_mouse_y; }
    return entry ? entry->c : TRUE;
}

// ---- installation -------------------------------------------------------
static struct IjSlot ij_slots[] = {
    {0x002cc40cu, 0x0009691cu, (void *)ij_peek_message, (void **)&ij_real_peek},
    {0x002cc438u, 0x0009695eu, (void *)ij_get_key_state, (void **)&ij_real_key},
    {0x002cc4f8u, 0x00096a6cu, (void *)ij_joy_get_pos, (void **)&ij_real_joypos},
    {0x002cc4f4u, 0x00096a66u, (void *)ij_joy_get_caps, (void **)&ij_real_joycaps},
    {0x002cc440u, 0x0009696au, (void *)ij_get_cursor_pos, (void **)&ij_real_cursor},
    {0x002cc458u, 0x0009698eu, (void *)ij_create_window, (void **)&ij_real_create},
};

static BOOL ij_image_valid(BYTE *image, uintptr_t expected_base) {
    if ((uintptr_t)image != expected_base || sizeof(void *) != 4) return FALSE;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)image;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < (LONG)sizeof(*dos) ||
        dos->e_lfanew > 4096 - (LONG)sizeof(IMAGE_NT_HEADERS32)) return FALSE;
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(image + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt->OptionalHeader.ImageBase != expected_base || nt->OptionalHeader.SizeOfImage != IJ_IMAGE_SIZE) return FALSE;
    for (unsigned i = 0; i < sizeof(ij_slots) / sizeof(ij_slots[0]); ++i) {
        BYTE thunk[6] = {0xff, 0x25};
        DWORD iat = (DWORD)(expected_base + ij_slots[i].iat_rva);
        memcpy(thunk + 2, &iat, 4);
        if (memcmp(image + ij_slots[i].thunk_rva, thunk, sizeof(thunk))) return FALSE;
    }
    return TRUE;
}

// DllMain-safe: no file I/O; configuration is read on the first hooked call.
static BOOL install_input_journal_image(BYTE *image, uintptr_t expected_base) {
    if (InterlockedCompareExchange(&ij_iat_state, -1, 0) != 0) return ij_iat_state == 1;
    if (!ij_image_valid(image, expected_base)) return FALSE;
    ij_image = image;
    for (unsigned i = 0; i < sizeof(ij_slots) / sizeof(ij_slots[0]); ++i) {
        DWORD *slot = (DWORD *)(image + ij_slots[i].iat_rva);
        DWORD protection, ignored;
        if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection)) return FALSE;
        *ij_slots[i].real = (void *)(uintptr_t)*slot;
        InterlockedExchange((LONG *)slot, (LONG)(uintptr_t)ij_slots[i].hook);
        VirtualProtect(slot, sizeof(*slot), protection, &ignored);
    }
    InterlockedExchange(&ij_iat_state, 1);
    return TRUE;
}

static void install_input_journal(void) {
    WCHAR exe[MAX_PATH];
    DWORD length = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (!length || length >= MAX_PATH) return;
    WCHAR *name = wcsrchr(exe, L'\\');
    if (!name || _wcsicmp(name + 1, L"DBSIM.EXE")) return;
    install_input_journal_image((BYTE *)GetModuleHandleW(NULL), (uintptr_t)0x00400000);
}

static void input_journal_shutdown(void) {
    mt_shutdown();
    if (ij_file == INVALID_HANDLE_VALUE) return;
    ij_flush();
    CloseHandle(ij_file);
    ij_file = INVALID_HANDLE_VALUE;
}
#endif
