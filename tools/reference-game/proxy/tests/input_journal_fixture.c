// Own-code fixture for proxy/input_journal.h. A synthetic image provides the
// thunks and IAT slots; fake "real" APIs stand in for Windows. No original
// game code runs here.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
static WCHAR config_path[MAX_PATH];
static HMODULE self_module;
static void log_event(const char *event) { printf("event %s\n", event); }
#include "../input_journal.h"

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); return 1; } } while (0)

static BYTE *image;
static MSG queue[16];
static unsigned queue_head, queue_tail;
static void push(UINT message, WPARAM wp, LPARAM lp) {
    MSG m = {0};
    m.hwnd = (HWND)(uintptr_t)0x1234; m.message = message; m.wParam = wp; m.lParam = lp;
    queue[queue_tail++ % 16] = m;
}
static BOOL WINAPI fake_peek(LPMSG msg, HWND w, UINT a, UINT b, UINT flags) {
    (void)w; (void)a; (void)b;
    if (queue_head == queue_tail) return FALSE;
    *msg = queue[queue_head % 16];
    if (flags & PM_REMOVE) ++queue_head;
    return TRUE;
}
static SHORT WINAPI fake_key(int vk) { return vk == VK_CONTROL ? (SHORT)0x8000 : 0; }
static MMRESULT WINAPI fake_joypos(UINT id, LPJOYINFOEX info) { if (id == 0) { info->dwXpos = 1111; return JOYERR_NOERROR; } return JOYERR_UNPLUGGED; }
static MMRESULT WINAPI fake_joycaps(UINT_PTR id, LPJOYCAPSA caps, UINT size) { (void)size; if (id == 0) { caps->wMid = 42; return JOYERR_NOERROR; } return JOYERR_PARMS; }
static BOOL WINAPI fake_cursor(LPPOINT p) { p->x = 100; p->y = 200; return TRUE; }
static HWND WINAPI fake_create(DWORD a, LPCSTR b, LPCSTR c, DWORD d, int e, int f, int g, int h, HWND parent, HMENU i, HINSTANCE j, LPVOID k) {
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h; (void)i; (void)j; (void)k;
    return parent ? (HWND)(uintptr_t)0x9999 : (HWND)(uintptr_t)0x1234;
}

static unsigned forwarded;
static LRESULT CALLBACK counting_proc(HWND w, UINT m, WPARAM wp, LPARAM lp) { (void)w; (void)m; (void)wp; (void)lp; ++forwarded; return 0; }

static void *slot(unsigned i) { DWORD v; memcpy(&v, image + ij_slots[i].iat_rva, 4); return (void *)(uintptr_t)v; }
static void set_frame(DWORD f) { memcpy(image + IJ_FRAME_RVA, &f, 4); }
#define PEEK ((IjPeek)slot(0))
#define KEY ((IjKeyState)slot(1))
#define JOYPOS ((IjJoyPos)slot(2))
#define JOYCAPS ((IjJoyCaps)slot(3))
#define CURSOR ((IjCursor)slot(4))
#define CREATE ((IjCreate)slot(5))

static BOOL make_image(void) {
    image = VirtualAlloc(NULL, IJ_IMAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!image) return FALSE;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)image;
    dos->e_magic = IMAGE_DOS_SIGNATURE; dos->e_lfanew = 0x80;
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(image + 0x80);
    nt->Signature = IMAGE_NT_SIGNATURE; nt->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    nt->OptionalHeader.ImageBase = (DWORD)(uintptr_t)image; nt->OptionalHeader.SizeOfImage = IJ_IMAGE_SIZE;
    void *fakes[] = {(void *)fake_peek, (void *)fake_key, (void *)fake_joypos, (void *)fake_joycaps, (void *)fake_cursor, (void *)fake_create};
    for (unsigned i = 0; i < 6; ++i) {
        DWORD iat = (DWORD)(uintptr_t)(image + ij_slots[i].iat_rva), fake = (DWORD)(uintptr_t)fakes[i];
        memcpy(image + ij_slots[i].iat_rva, &fake, 4);
        image[ij_slots[i].thunk_rva] = 0xff; image[ij_slots[i].thunk_rva + 1] = 0x25;
        memcpy(image + ij_slots[i].thunk_rva + 2, &iat, 4);
    }
    return TRUE;
}

static void write_config(const WCHAR *dir, const WCHAR *mode, const WCHAR *journal) {
    swprintf(config_path, MAX_PATH, L"%ls\\es2-oracle.ini", dir);
    WCHAR path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%ls\\%ls", dir, journal);
    WritePrivateProfileStringW(L"input", L"mode", mode, config_path);
    WritePrivateProfileStringW(L"input", L"journal", path, config_path);
}

static char *read_all(const WCHAR *dir, const WCHAR *name) {
    WCHAR path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%ls\\%ls", dir, name);
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
    DWORD size = GetFileSize(f, NULL), got;
    char *text = calloc(1, size + 1);
    ReadFile(f, text, size, &got, NULL);
    CloseHandle(f);
    return text;
}

// One "game frame" of the synthetic pump: drain the queue like DBSIM does and
// call GetKeyState for every key-down it dispatches (as the keyboard handler does).
static unsigned delivered[16], delivered_count;
static WPARAM delivered_wp[16];
static HWND delivered_hwnd[16];
static void pump(void) {
    MSG m;
    while (PEEK(&m, NULL, 0, 0, PM_REMOVE)) {
        delivered_hwnd[delivered_count] = m.hwnd;
        delivered_wp[delivered_count] = m.wParam;
        delivered[delivered_count++] = m.message;
        if (m.message == WM_KEYDOWN) { KEY(VK_SHIFT); KEY(VK_MENU); KEY(VK_CONTROL); }
    }
}
// One empty PM_REMOVE call from a second thread (the trace counts it, never traces it).
static DWORD WINAPI other_thread_peek(LPVOID unused) {
    (void)unused;
    MSG m;
    return (DWORD)PEEK(&m, NULL, 0, 0, PM_REMOVE);
}

int wmain(int argc, WCHAR **argv) {
    (void)install_input_journal;
    if (argc != 3 || !make_image()) return 2;
    const WCHAR *mode = argv[1], *dir = argv[2];
    if (!wcscmp(mode, L"mismatch")) {
        image[ij_slots[3].thunk_rva + 1] = 0x15;
        void *before = slot(0);
        CHECK(!install_input_journal_image(image, (uintptr_t)image));
        CHECK(slot(0) == before);
        printf("PASS mismatch\n");
        return 0;
    }
    CHECK(install_input_journal_image(image, (uintptr_t)image));
    CHECK(slot(0) == (void *)ij_peek_message && slot(5) == (void *)ij_create_window);
    JOYINFOEX info = {0}; info.dwSize = sizeof(info); info.dwFlags = JOY_RETURNALL;
    JOYCAPSA caps = {0};
    POINT point;

    if (!wcscmp(mode, L"record")) {
        write_config(dir, L"record", L"journal.jsonl");
        set_frame(0);
        CHECK(CREATE(0, "c", "t", 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL) == (HWND)(uintptr_t)0x1234);
        CHECK(JOYCAPS(0, &caps, sizeof(caps)) == JOYERR_NOERROR && caps.wMid == 42);
        set_frame(5);
        push(WM_PAINT, 0, 0); push(WM_KEYDOWN, 0x57, 0x00110001); pump();
        CHECK(JOYPOS(0, &info) == JOYERR_NOERROR && info.dwXpos == 1111);
        CHECK(JOYPOS(1, &info) == JOYERR_UNPLUGGED);
        set_frame(6);
        push(WM_KEYUP, 0x57, (LPARAM)0xc0110001); pump();
        CHECK(CURSOR(&point) && point.x == 100 && point.y == 200);
        input_journal_shutdown();
        char *text = read_all(dir, L"journal.jsonl");
        CHECK(text && strstr(text, "\"mode\":\"record\""));
        CHECK(strstr(text, "{\"f\":5,\"api\":\"msg\",\"a\":256,\"b\":87,\"c\":1114113,"));
        CHECK(strstr(text, "{\"f\":6,\"api\":\"msg\",\"a\":257,\"b\":87,"));
        CHECK(!strstr(text, "\"p\":"));     // pump counts are a script-only feature
        CHECK(!strstr(text, "\"a\":15,"));   // WM_PAINT is not input
        CHECK(strstr(text, "{\"f\":5,\"api\":\"key\",\"a\":17,\"b\":-32768,"));
        CHECK(strstr(text, "{\"f\":5,\"api\":\"joypos\",\"a\":0,\"b\":0,") && strstr(text, "{\"f\":5,\"api\":\"joypos\",\"a\":1,\"b\":167,"));
        CHECK(strstr(text, "{\"f\":0,\"api\":\"joycaps\",\"a\":0,\"b\":0,"));
        CHECK(strstr(text, "{\"f\":6,\"api\":\"cursor\",\"a\":100,\"b\":200,\"c\":1,"));
        CHECK(ij_recorded == 9);   // joycaps, msg, 3 key, 2 joypos, msg, cursor
        free(text);
    } else if (!wcscmp(mode, L"replay")) {
        write_config(dir, L"replay", L"journal.jsonl");
        set_frame(0);
        CREATE(0, "c", "t", 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL);
        CHECK(JOYCAPS(0, &caps, sizeof(caps)) == JOYERR_NOERROR && caps.wMid == 42);
        set_frame(4);
        push(WM_KEYDOWN, 0x41, 0x001e0001);   // real input: must be dropped
        pump();
        CHECK(delivered_count == 0 && ij_dropped == 1);
        set_frame(5);
        push(WM_PAINT, 0, 0);
        pump();
        CHECK(delivered_count == 2 && delivered[0] == WM_KEYDOWN && delivered_wp[0] == 0x57 &&
              delivered_hwnd[0] == (HWND)(uintptr_t)0x1234 && delivered[1] == WM_PAINT);
        CHECK(ij_fallbacks == 0);             // all three GetKeyState results came from the journal
        info.dwXpos = 0;
        CHECK(JOYPOS(0, &info) == JOYERR_NOERROR && info.dwXpos == 1111);
        CHECK(JOYPOS(1, &info) == JOYERR_UNPLUGGED);
        set_frame(6);
        pump();
        CHECK(delivered_count == 3 && delivered[2] == WM_KEYUP);
        point.x = point.y = 0;
        CHECK(CURSOR(&point) && point.x == 100 && point.y == 200);
        CHECK(ij_injected == 2 && ij_fallbacks == 0);
    } else if (!wcscmp(mode, L"script")) {
        WCHAR path[MAX_PATH];
        swprintf(path, MAX_PATH, L"%ls\\script.jsonl", dir);
        HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        // The second line uses the spacing written by Python's json.dumps.
        static const char script[] = "{\"f\":3,\"api\":\"keydown\",\"vk\":87}\n{\"f\": 9, \"api\": \"keyup\", \"vk\": 87}\n";
        DWORD written;
        WriteFile(f, script, sizeof(script) - 1, &written, NULL);
        CloseHandle(f);
        write_config(dir, L"replay", L"script.jsonl");
        set_frame(0);
        CREATE(0, "c", "t", 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL);
        pump();
        CHECK(delivered_count == 0);
        set_frame(3);
        pump();
        CHECK(delivered_count == 1 && delivered[0] == WM_KEYDOWN && delivered_wp[0] == 87);
        CHECK((USHORT)KEY(87) == 0x8001);     // virtual state: down, toggled
        set_frame(9);
        pump();
        CHECK(delivered_count == 2 && delivered[1] == WM_KEYUP && KEY(87) == 1);
        CHECK(JOYPOS(0, &info) == JOYERR_UNPLUGGED);   // nothing journaled: fallback
    } else if (!wcscmp(mode, L"joystick")) {
        // A scripted joystick: DBSIM asks for 808 caps bytes (0x4776ba) and polls
        // devices 0 and 1 every frame (0x477754); device 1 has no entry and falls back.
        WCHAR path[MAX_PATH];
        swprintf(path, MAX_PATH, L"%ls\\joystick.jsonl", dir);
        static char script[4096];
        BYTE blob[808] = {0};
        blob[0] = 7;                    // wMid
        blob[60] = 8;                   // wNumButtons
        blob[96] = 0x13;                // wCaps: Z, R, POV
        int n = snprintf(script, sizeof(script), "{\"f\":0,\"api\":\"joycaps\",\"a\":0,\"b\":0,\"c\":808,\"d\":0,\"hex\":\"");
        for (unsigned i = 0; i < sizeof(blob); ++i) n += snprintf(script + n, sizeof(script) - (size_t)n, "%02x", blob[i]);
        n += snprintf(script + n, sizeof(script) - (size_t)n,
            "\"}\n{\"f\":0,\"api\":\"joycaps\",\"a\":1,\"b\":165,\"c\":808,\"d\":0}\n"
            "{\"f\":2,\"api\":\"joypos\",\"a\":0,\"b\":0,\"hex\":\"34000000000000006400000000000000000000000000000000000000000000000500000000000000282300000000000000000000\"}\n");
        HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        DWORD written;
        WriteFile(f, script, (DWORD)n, &written, NULL);
        CloseHandle(f);
        write_config(dir, L"replay", L"joystick.jsonl");
        set_frame(0);
        CREATE(0, "c", "t", 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL);
        static BYTE big[808];
        CHECK(JOYCAPS(0, (LPJOYCAPSA)big, sizeof(big)) == JOYERR_NOERROR && big[0] == 7 && big[60] == 8 && big[96] == 0x13);
        CHECK(JOYCAPS(1, (LPJOYCAPSA)big, sizeof(big)) == JOYERR_PARMS);   // 165, as recorded without a device
        set_frame(2);
        info.dwXpos = 0;
        CHECK(JOYPOS(0, &info) == JOYERR_NOERROR && info.dwXpos == 100 && info.dwButtons == 5 && info.dwPOV == 9000);
        CHECK(JOYPOS(1, &info) == JOYERR_UNPLUGGED && ij_fallbacks == 1);
        set_frame(3);
        CHECK(JOYPOS(0, &info) == JOYERR_UNPLUGGED && ij_fallbacks == 2);   // no entry for frame 3
    } else if (!wcscmp(mode, L"focus")) {
        // Replay keeps sent focus/power messages from the game window procedure.
        write_config(dir, L"replay", L"script.jsonl");
        CREATE(0, "c", "t", 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL);   // fake handle: not subclassed
        CHECK(ij_mode == IJ_REPLAY && ij_game_window_proc == NULL);
        ij_game_window_proc = counting_proc;
        HWND w = (HWND)(uintptr_t)0x1234;
        ij_window_proc(w, WM_KILLFOCUS, 0, 0);
        ij_window_proc(w, WM_SETFOCUS, 0, 0);
        ij_window_proc(w, WM_POWERBROADCAST, 4, 0);
        CHECK(forwarded == 0 && ij_focus_dropped == 3);
        ij_window_proc(w, WM_PAINT, 0, 0);
        ij_window_proc(w, WM_POWERBROADCAST, 10, 0);
        ij_window_proc(w, WM_KEYDOWN, 0x57, 0);
        CHECK(forwarded == 3 && ij_focus_dropped == 3);
    } else if (!wcscmp(mode, L"pump")) {
        // A modal screen: the frame stays 7 while the pump keeps running.
        WCHAR path[MAX_PATH];
        swprintf(path, MAX_PATH, L"%ls\\pump.jsonl", dir);
        HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        static const char script[] = "{\"f\":7,\"p\":3,\"api\":\"keydown\",\"vk\":13}\n"
                                     "{\"f\":7,\"p\":5,\"api\":\"mousedown\",\"x\":100,\"y\":50}\n"
                                     "{\"f\":7,\"p\":5,\"api\":\"mouseup\",\"x\":100,\"y\":50}\n"
                                     "{\"f\":8,\"api\":\"mousedown\",\"x\":40000,\"y\":1}\n";   // rejected: out of range
        DWORD written;
        WriteFile(f, script, sizeof(script) - 1, &written, NULL);
        CloseHandle(f);
        write_config(dir, L"replay", L"pump.jsonl");
        set_frame(7);
        CREATE(0, "c", "t", 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL);
        CHECK(CURSOR(&point) && point.x == 320 && point.y == 240);   // initial virtual cursor
        pump(); pump(); pump();               // peeks 0, 1, 2: nothing yet
        CHECK(delivered_count == 0);
        pump();                               // peek 3 injects, peek 4 waits
        CHECK(delivered_count == 1 && delivered[0] == WM_KEYDOWN && delivered_wp[0] == 13);
        pump();                               // peeks 5, 6: both mouse messages
        CHECK(delivered_count == 3 && delivered[1] == WM_LBUTTONDOWN && delivered_wp[1] == MK_LBUTTON &&
              delivered[2] == WM_LBUTTONUP);
        CHECK(CURSOR(&point) && point.x == 100 && point.y == 50);
        CHECK(ij_entry_count == 3);
        set_frame(8);
        pump();
        CHECK(delivered_count == 3);
    } else if (!wcscmp(mode, L"trace") || !wcscmp(mode, L"trace_off") || !wcscmp(mode, L"trace_exists")) {
        // The message trace beside the tick log, only with [input] message_trace=1.
        WCHAR path[MAX_PATH], tick[MAX_PATH], trace[MAX_PATH];
        swprintf(path, MAX_PATH, L"%ls\\trace.jsonl", dir);
        HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        static const char script[] = "{\"f\":5,\"p\":1,\"api\":\"keydown\",\"vk\":87}\n";
        DWORD written;
        WriteFile(f, script, sizeof(script) - 1, &written, NULL);
        CloseHandle(f);
        write_config(dir, L"replay", L"trace.jsonl");
        swprintf(tick, MAX_PATH, L"%ls\\tick.jsonl", dir);
        swprintf(trace, MAX_PATH, L"%ls\\tick.jsonl.messages.ndjson", dir);
        WritePrivateProfileStringW(L"trace", L"tick_log", tick, config_path);
        if (wcscmp(mode, L"trace_off")) WritePrivateProfileStringW(L"input", L"message_trace", L"1", config_path);
        static const char existing[] = "keep";
        if (!wcscmp(mode, L"trace_exists")) {
            f = CreateFileW(trace, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
            WriteFile(f, existing, sizeof(existing) - 1, &written, NULL);
            CloseHandle(f);
        }
        // The call-site rule: a return address after an E8 inside the image gives the call's VA.
        image[0x64ca2] = 0xe8;
        CHECK(mt_call_site((uintptr_t)image + 0x64ca7) == (DWORD)(uintptr_t)(image + 0x64ca2));
        CHECK(mt_call_site((uintptr_t)image + 0x64ca8) == 0 && mt_call_site((uintptr_t)image + 2) == 0 &&
              mt_call_site((uintptr_t)image + IJ_IMAGE_SIZE) == 0);
        set_frame(4);
        CREATE(0, "c", "t", 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL);
        push(WM_KEYDOWN, 0x41, 0x001e0001);   // real input: dropped and traced as "drop"
        push(0xa000, 1, 2); queue[(queue_tail - 1) % 16].time = 1234;
        pump();                               // peek 0: drop, 0xa000; peek 1: empty
        CHECK(delivered_count == 1 && delivered[0] == 0xa000 && ij_dropped == 1);
        set_frame(5);
        push(MM_MCINOTIFY, 1, 7);
        pump();                               // peek 0: MM_MCINOTIFY; peek 1: the journal key; peek 2: empty
        CHECK(delivered_count == 3 && delivered[1] == MM_MCINOTIFY && delivered[2] == WM_KEYDOWN && delivered_wp[2] == 87);
        MSG m;
        CHECK(!PEEK(&m, NULL, 0, 0, PM_NOREMOVE));
        set_frame(6);
        pump();
        HANDLE thread = CreateThread(NULL, 0, other_thread_peek, NULL, 0, NULL);
        CHECK(thread && WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0);
        CloseHandle(thread);
        input_journal_shutdown();
        char *text = read_all(dir, L"tick.jsonl.messages.ndjson");
        if (!wcscmp(mode, L"trace_off")) {
            CHECK(!text && !mt_on && mt_file == INVALID_HANDLE_VALUE && mt_pumps == 0 && mt_other_thread == 0);
        } else if (!wcscmp(mode, L"trace_exists")) {
            CHECK(text && !strcmp(text, existing) && !mt_on && mt_pumps == 0);   // never overwritten
            free(text);
        } else {
            CHECK(text && !strncmp(text, "{\"event\":\"start\",\"schema\":1,\"kind\":\"message_trace\",", 51));
            CHECK(strstr(text, "{\"k\":\"drop\",\"f\":4,\"p\":0,\"m\":256,\"w\":65,\"l\":1966081,\"h\":4660,\"t\":0,\"c\":\"0x0\",\"r\":\"0x"));
            CHECK(strstr(text, "{\"k\":\"msg\",\"f\":4,\"p\":0,\"m\":40960,\"w\":1,\"l\":2,\"h\":4660,\"t\":1234,\"c\":\"0x0\","));
            CHECK(strstr(text, "{\"k\":\"frame\",\"f\":4,\"pumps\":2,\"empty\":1,\"msgs\":1,\"inject\":0,\"drop\":1,\"noremove\":0}\r\n"));
            CHECK(strstr(text, "{\"k\":\"msg\",\"f\":5,\"p\":0,\"m\":953,\"w\":1,\"l\":7,"));
            CHECK(strstr(text, "{\"k\":\"inject\",\"f\":5,\"p\":1,\"m\":256,\"w\":87,"));
            CHECK(strstr(text, "{\"k\":\"frame\",\"f\":5,\"pumps\":3,\"empty\":1,\"msgs\":1,\"inject\":1,\"drop\":0,\"noremove\":1}\r\n"));
            CHECK(strstr(text, "{\"k\":\"frame\",\"f\":6,\"pumps\":1,\"empty\":1,\"msgs\":0,\"inject\":0,\"drop\":0,\"noremove\":0}\r\n"));
            CHECK(strstr(text, "\r\n{\"event\":\"end\",\"other_thread\":1}\r\n"));
            CHECK(!strstr(text, "\"r\":\"0x0\""));   // the return address is always recorded
            // Order: the frame-4 messages before frame 4's summary; no record for another frame.
            CHECK(strstr(text, "\"k\":\"msg\",\"f\":4") < strstr(text, "\"k\":\"frame\",\"f\":4"));
            CHECK(!strstr(text, "\"f\":3,") && !strstr(text, "\"f\":7,"));
            unsigned lines = 0;
            for (const char *c = text; *c; ++c) lines += *c == '\n';
            CHECK(lines == 9);   // start, drop, msg, frame 4, msg, inject, frame 5, frame 6, end
            free(text);
        }
    } else {
        return 2;
    }
    printf("PASS %ls\n", mode);
    return 0;
}
