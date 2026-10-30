// Own-code fixture for proxy/vshell_capture.h and vshell_journal.h. A
// synthetic image at a test base provides the thunks, import slots, code
// signatures and globals; fake "real" APIs stand in for Windows. No original
// game code or data is used; no game is started.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#define VC_BASE ((uintptr_t)0x30000000)
#include "../vshell_capture.h"

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); return 1; } } while (0)

static BYTE *image;
static BYTE *frame;
static WCHAR directory[MAX_PATH];

/* ---- fakes ---------------------------------------------------------------------------------------- */
static unsigned bitblt_calls, stretch_calls, dib_calls, key_calls;
/* A synthetic QueryPerformanceCounter. Every read advances it by 10; a forwarded BitBlt takes 1000
 * ticks, so a stamp taken after the blit would differ from one taken before it. */
static uint64_t fake_qpc_value;
static uint64_t fake_qpc(void) { fake_qpc_value += 10; return fake_qpc_value; }
static BOOL WINAPI fake_bitblt(HDC a, int b, int c, int d, int e, HDC f, int g, int h, DWORD i) {
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h; (void)i; ++bitblt_calls; fake_qpc_value += 1000; return TRUE;
}
static BOOL WINAPI fake_stretch(HDC a, int b, int c, int d, int e, HDC f, int g, int h, int i, int j, DWORD k) {
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h; (void)i; (void)j; (void)k; ++stretch_calls; return TRUE;
}
static UINT WINAPI fake_dib(HDC a, UINT b, UINT c, const RGBQUAD *d) { (void)a; (void)b; (void)d; ++dib_calls; return c; }
static SHORT WINAPI fake_key(int vk) { ++key_calls; return vk == VK_SHIFT ? (SHORT)0x8000 : 0; }
/* The real pointer is at screen (7, 9); the client area starts at screen (100, 50). */
static unsigned get_cursor_calls, set_cursor_calls;
static BOOL WINAPI fake_get_cursor(LPPOINT p) { ++get_cursor_calls; p->x = 7; p->y = 9; return TRUE; }
static BOOL WINAPI fake_set_cursor(int x, int y) { (void)x; (void)y; ++set_cursor_calls; return TRUE; }
static BOOL WINAPI fake_to_screen(HWND w, LPPOINT p) { if (w != (HWND)(uintptr_t)0x4321) return FALSE; p->x += 100; p->y += 50; return TRUE; }
static BOOL WINAPI fake_to_client(HWND w, LPPOINT p) { if (w != (HWND)(uintptr_t)0x4321) return FALSE; p->x -= 100; p->y -= 50; return TRUE; }
static MSG queue[32];
static unsigned queue_head, queue_tail;
static void push(UINT message, WPARAM wp, LPARAM lp) {
    MSG m = {0};
    m.hwnd = (HWND)(uintptr_t)0x4321; m.message = message; m.wParam = wp; m.lParam = lp;
    queue[queue_tail++ % 32] = m;
}
static BOOL WINAPI fake_peek(LPMSG msg, HWND w, UINT a, UINT b, UINT flags) {
    (void)w; (void)a; (void)b;
    if (queue_head == queue_tail) return FALSE;
    *msg = queue[queue_head % 32];
    if (flags & PM_REMOVE) ++queue_head;
    return TRUE;
}
static DWORD fake_now;
static DWORD WINAPI fake_clock(void) { return fake_now; }
static BOOL module_ok = TRUE;
static BOOL fake_module(void) { return module_ok; }

static void put32(DWORD rva, DWORD v) { memcpy(image + rva, &v, 4); }

static BOOL make_image(void) {
    image = VirtualAlloc((void *)VC_BASE, VC_IMAGE_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (image != (BYTE *)VC_BASE) return FALSE;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)image;
    dos->e_magic = IMAGE_DOS_SIGNATURE; dos->e_lfanew = 0x80;
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(image + 0x80);
    nt->Signature = IMAGE_NT_SIGNATURE; nt->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    nt->OptionalHeader.ImageBase = (DWORD)VC_BASE; nt->OptionalHeader.SizeOfImage = VC_IMAGE_SIZE;
    void *fakes[7] = {(void *)fake_bitblt, (void *)fake_stretch, (void *)fake_dib, (void *)fake_peek, (void *)fake_key,
                      (void *)fake_get_cursor, (void *)fake_set_cursor};
    for (unsigned i = 0; i < 7; ++i) {
        BYTE *thunk = image + vc_slots[i].thunk_rva;
        DWORD iat = (DWORD)(VC_BASE + vc_slots[i].iat_rva);
        thunk[0] = 0xff; thunk[1] = 0x25; memcpy(thunk + 2, &iat, 4);
        put32(vc_slots[i].iat_rva, (DWORD)(uintptr_t)fakes[i]);
    }
    for (unsigned i = 0; i < sizeof(vc_signatures) / sizeof(vc_signatures[0]); ++i)
        memcpy(image + vc_signatures[i].rva, vc_signatures[i].bytes, vc_signatures[i].size);
    frame = VirtualAlloc(NULL, VC_FRAME_BYTES, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!frame) return FALSE;
    put32(VC_BITS_RVA, (DWORD)(uintptr_t)frame);
    put32(VC_MEMDC_RVA, 0x5150);
    put32(VC_WINDOW_RVA, 0x4321);
    put32(VC_FOCUS_RVA, 1);
    return TRUE;
}

static void sha_hex(const BYTE *bytes, size_t n, char hex[65]) { sha256_ctx s; sha256_init(&s); sha256_update(&s, bytes, n); sha256_hex(&s, hex); }

static char *read_text(const WCHAR *path, DWORD *size) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
    DWORD n = GetFileSize(f, NULL), got = 0;
    char *text = malloc((size_t)n + 1);
    if (text && ReadFile(f, text, n, &got, NULL) && got == n) text[n] = 0; else { free(text); text = NULL; }
    CloseHandle(f);
    if (size) *size = n;
    return text;
}
static unsigned count_of(const char *text, const char *needle) {
    unsigned n = 0;
    for (const char *p = text; (p = strstr(p, needle)); p += strlen(needle)) ++n;
    return n;
}
static BOOL write_file(const WCHAR *path, const char *text) {
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return FALSE;
    DWORD written;
    BOOL ok = WriteFile(f, text, (DWORD)strlen(text), &written, NULL);
    CloseHandle(f);
    return ok;
}
static void path_in(WCHAR out[MAX_PATH], const WCHAR *name) { _snwprintf(out, MAX_PATH, L"%ls\\%ls", directory, name); out[MAX_PATH - 1] = 0; }

/* Writes the INI and points the header at it. */
static BOOL configure(const char *extra_lines) {
    WCHAR ini[MAX_PATH], log[MAX_PATH];
    path_in(ini, L"capture.ini"); path_in(log, L"capture.jsonl");
    DeleteFileW(log);
    char text[2048];
    snprintf(text, sizeof(text), "[vshell_capture]\r\nlog=%ls\r\n%s", log, extra_lines);
    if (!write_file(ini, text)) return FALSE;
    wcscpy(vc_config, ini);
    vc_module_check = fake_module;
    vc_clock = fake_clock;
    vc_client_to_screen = fake_to_screen;
    vc_screen_to_client = fake_to_client;
    return TRUE;
}

/* ---- parse ---------------------------------------------------------------------------------------- */
static VjEntry entries[VJ_ENTRY_MAX];
static long parse(const char *text) { return vj_parse(text, strlen(text), entries); }

struct Seen { unsigned steps, marks, snaps, done; char last_screen[VJ_LABEL_MAX]; };
static void seen_event(void *context, int kind, uint32_t step, const VjEntry *e) {
    struct Seen *s = context; (void)step; (void)e;
    if (kind == VJ_EVENT_STEP) ++s->steps;
    if (kind == VJ_EVENT_MARK) { ++s->marks; strcpy(s->last_screen, e->screen); }
    if (kind == VJ_EVENT_SNAP) ++s->snaps;
    if (kind == VJ_EVENT_DONE) ++s->done;
}

static int test_parse(void) {
    const char *good =
        "{\"op\":\"mark\",\"screen\":\"main_menu\",\"state\":\"idle\"}\r\n"
        "\r\n"
        "  {\"op\": \"wait\", \"passes\": 3, \"ms\": 100}\n"
        "{\"op\":\"snap\"}\n"
        "{\"op\":\"click\",\"control\":\"main_menu.19\",\"x\":392,\"y\":230}\n"
        "{\"op\":\"key\",\"vk\":13,\"mods\":5}\n"
        "{\"op\":\"close\",\"note\":\"ignored key\"}";
    CHECK(parse(good) == 6);
    CHECK(entries[0].op == VJ_OP_MARK && !strcmp(entries[0].screen, "main_menu") && !strcmp(entries[0].state, "idle") && entries[0].line == 1);
    CHECK(entries[1].op == VJ_OP_WAIT && entries[1].passes == 3 && entries[1].ms == 100 && entries[1].line == 3);
    CHECK(entries[3].op == VJ_OP_CLICK && entries[3].x == 392 && entries[3].y == 230);
    CHECK(entries[4].op == VJ_OP_KEY && entries[4].vk == 13 && entries[4].mods == 5);
    CHECK(entries[5].op == VJ_OP_CLOSE && entries[5].line == 7);
    static const char *bad[] = {
        "{\"op\":\"jump\"}", "{\"op\":\"click\",\"x\":640,\"y\":1}", "{\"op\":\"click\",\"x\":1}",
        "{\"op\":\"click\",\"control\":\"main_menu.19\"}", "{\"op\":\"mark\",\"screen\":\"a b\"}", "{\"op\":\"mark\"}",
        "{\"op\":\"wait\"}", "{\"op\":\"wait\",\"ms\":-1}", "{\"op\":\"key\",\"vk\":0}", "{\"op\":\"key\",\"vk\":13,\"mods\":8}",
        "[\"op\",\"snap\"]", "{\"x\":1}", "{\"op\":\"click\",\"x\":\"1\",\"y\":2}",
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        char text[256];
        snprintf(text, sizeof(text), "{\"op\":\"snap\"}\n%s\n", bad[i]);
        if (parse(text) != -2) { fprintf(stderr, "accepted: %s\n", bad[i]); return 1; }
    }
    char longline[VJ_LINE_MAX + 64];
    memset(longline, ' ', sizeof(longline)); memcpy(longline, "{\"op\":\"snap\"", 12);
    longline[sizeof(longline) - 2] = '}'; longline[sizeof(longline) - 1] = 0;
    CHECK(parse(longline) == -1);

    /* Scheduler: phases, gaps, readiness, labels, events. */
    CHECK(parse("{\"op\":\"mark\",\"screen\":\"bay\"}\n{\"op\":\"click\",\"x\":10,\"y\":20}\n{\"op\":\"wait\",\"passes\":2,\"ms\":50}\n"
                "{\"op\":\"snap\"}\n{\"op\":\"key\",\"vk\":65,\"mods\":4}\n{\"op\":\"close\"}\n") == 6);
    struct Seen seen = {0};
    VjRunner r; VjMsg out[4];
    vj_init(&r, entries, 6, seen_event, &seen);
    CHECK(vj_begin_pass(&r, 0, 0, 0, out) == 0 && seen.marks == 1 && !strcmp(r.screen, "bay") && r.index == 1);
    unsigned n = vj_begin_pass(&r, 1, 0, 1, out);
    CHECK(n == 3 && out[0].message == VJ_WM_MOUSEMOVE && out[0].lparam == ((479u << 16) | 639u) && out[1].lparam == 0 &&
          out[2].lparam == ((20u << 16) | 10u));
    CHECK(vj_begin_pass(&r, 2, 0, 1, out) == 0);               /* gap */
    CHECK(vj_begin_pass(&r, 3, 0, 0, out) == 0);               /* not ready */
    n = vj_begin_pass(&r, 3, 0, 1, out);
    CHECK(n == 1 && out[0].message == VJ_WM_LBUTTONDOWN && out[0].wparam == VJ_MK_LBUTTON && out[0].lparam == ((20u << 16) | 10u));
    CHECK(vj_begin_pass(&r, 4, 0, 1, out) == 0);
    n = vj_begin_pass(&r, 5, 0, 1, out);
    CHECK(n == 1 && out[0].message == VJ_WM_LBUTTONUP && r.index == 2);
    CHECK(vj_begin_pass(&r, 6, 10, 0, out) == 0);               /* wait starts (no readiness needed) */
    CHECK(vj_begin_pass(&r, 8, 40, 1, out) == 0);               /* passes reached, ms not */
    n = vj_begin_pass(&r, 9, 60, 1, out);                       /* wait done, snap, key down */
    CHECK(n == 1 && seen.snaps == 1 && out[0].message == VJ_WM_SYSKEYDOWN && out[0].wparam == 65 && out[0].mods == 4);
    n = vj_begin_pass(&r, 11, 60, 1, out);
    CHECK(n == 1 && out[0].message == VJ_WM_SYSKEYUP && (out[0].lparam & 0xc0000000u) == 0xc0000000u);
    n = vj_begin_pass(&r, 12, 60, 1, out);
    CHECK(n == 1 && out[0].message == VJ_WM_CLOSE && r.done && seen.done == 1 && seen.steps == 6);
    CHECK(vj_begin_pass(&r, 13, 60, 1, out) == 0);
    puts("PASS: journal parser (6 ops, 14 refusals) and scheduler (click/key phases, gaps, readiness, wait limits).");
    return 0;
}

/* ---- capture ---------------------------------------------------------------------------------------- */
static int test_capture(void) {
    CHECK(make_image());
    CHECK(install_vshell_capture_image(image));
    CHECK(sizeof(vc_slots) / sizeof(vc_slots[0]) == 7);
    for (unsigned i = 0; i < 7; ++i) {
        DWORD v; memcpy(&v, image + vc_slots[i].iat_rva, 4);
        CHECK(v == (DWORD)(uintptr_t)vc_slots[i].hook);
    }
    CHECK(vc_real_bitblt == fake_bitblt && vc_real_peek == fake_peek && vc_real_get_cursor == fake_get_cursor &&
          vc_real_set_cursor == fake_set_cursor);
    WCHAR dumps[MAX_PATH]; path_in(dumps, L"dumps");
    CreateDirectoryW(dumps, NULL);
    char extra[600];
    snprintf(extra, sizeof(extra), "capture=1\r\ndump_dir=%ls\r\n", dumps);
    CHECK(configure(extra));
    /* Without a journal the pointer hooks only forward. */
    POINT pt = {0, 0};
    CHECK(vc_get_cursor_at(VC_CURSOR_GET_RET, &pt) && pt.x == 7 && pt.y == 9 && get_cursor_calls == 1);
    CHECK(vc_set_cursor_at(0x775du, 1, 2) && set_cursor_calls == 1 && vc_pointer_warps == 0);
    /* Palette: 256 quads; the fourth byte is residue and must not change the hash. */
    RGBQUAD quads[256];
    BYTE rgb[768];
    for (unsigned i = 0; i < 256; ++i) {
        quads[i].rgbRed = (BYTE)i; quads[i].rgbGreen = (BYTE)(255 - i); quads[i].rgbBlue = (BYTE)(i * 3); quads[i].rgbReserved = 0xaa;
        rgb[3 * i] = (BYTE)i; rgb[3 * i + 1] = (BYTE)(255 - i); rgb[3 * i + 2] = (BYTE)(i * 3);
    }
    char palette_hex[65], frame_a[65], frame_b[65];
    sha_hex(rgb, 768, palette_hex);
    CHECK(vc_set_dib_at(VC_PALETTE_RET, (HDC)(uintptr_t)0x5150, 0, 256, quads) == 256 && dib_calls == 1);
    CHECK(vc_set_dib_at(VC_PALETTE_RET, (HDC)(uintptr_t)0x9999, 0, 256, quads) == 256 && dib_calls == 2);   /* other DC: no upload */
    CHECK(vc_uploads == 1 && !strcmp(vc_palette_hex, palette_hex));
    for (unsigned i = 0; i < VC_FRAME_BYTES; ++i) frame[i] = (BYTE)(i * 7);
    sha_hex(frame, VC_FRAME_BYTES, frame_a);
    CHECK(vc_bitblt_at(VC_PRESENT_RET, NULL, 10, 20, 30, 40, NULL, 10, 20, SRCCOPY));
    CHECK(vc_bitblt_at(VC_PRESENT_RET, NULL, 10, 20, 30, 40, NULL, 10, 20, SRCCOPY));   /* unchanged: no record */
    CHECK(vc_bitblt_at(0x1234, NULL, 0, 0, 1, 1, NULL, 0, 0, SRCCOPY));                 /* other site: forwarded only */
    frame[1000] ^= 0x55;
    sha_hex(frame, VC_FRAME_BYTES, frame_b);
    CHECK(vc_bitblt_at(VC_PRESENT_RET, NULL, 0, 0, 640, 480, NULL, 0, 0, SRCCOPY));
    memset(frame, 0, VC_FRAME_BYTES);
    CHECK(vc_stretchblt_at(VC_CLEAR_RET, NULL, 0, 0, 640, 480, NULL, 0, 0, 10, 10, SRCCOPY));
    CHECK(bitblt_calls == 4 && stretch_calls == 1 && vc_presents == 3);
    vshell_capture_shutdown();
    WCHAR log[MAX_PATH]; path_in(log, L"capture.jsonl");
    char *text = read_text(log, NULL);
    CHECK(text);
    CHECK(count_of(text, "\"kind\":\"start\"") == 1 && strstr(text, "\"module_sha256_ok\":true"));
    CHECK(count_of(text, "\"kind\":\"palette\"") == 1 && count_of(text, palette_hex) >= 3);
    CHECK(count_of(text, "\"kind\":\"present\"") == 2 && count_of(text, frame_a) == 1 && count_of(text, frame_b) == 1);
    CHECK(strstr(text, "\"site\":\"0x30006e57\",\"dst\":[10,20,30,40],\"src\":[10,20],\"presents\":1"));
    CHECK(strstr(text, "\"dst\":[0,0,640,480],\"src\":[0,0],\"presents\":2"));
    CHECK(count_of(text, "\"kind\":\"clear\"") == 1 && strstr(text, "\"site\":\"0x30007924\""));
    CHECK(strstr(text, "\"kind\":\"end\"") && strstr(text, "\"presents\":3,\"uploads\":1"));
    CHECK(strstr(text, "\"screen\":\"none\""));
    /* Schema 1 is what the default writes: the start record says so and no record has a time. */
    CHECK(strstr(text, "{\"kind\":\"start\",\"seq\":0,\"pass\":0,\"step\":0,\"screen\":\"none\",\"state\":\"\",\"schema\":1,\"pid\":"));
    CHECK(!strstr(text, "qpc") && !vc_qpc_on && vc_stamp == 0);
    /* Dumps: each frame and the table once, by hash. */
    WCHAR dump[MAX_PATH];
    DWORD size = 0;
    _snwprintf(dump, MAX_PATH, L"%ls\\%hs.idx", dumps, frame_b);
    char *bytes = read_text(dump, &size);
    CHECK(bytes && size == VC_FRAME_BYTES);
    char check_hex[65]; sha_hex((BYTE *)bytes, size, check_hex);
    CHECK(!strcmp(check_hex, frame_b));
    free(bytes);
    _snwprintf(dump, MAX_PATH, L"%ls\\%hs.rgb", dumps, palette_hex);
    bytes = read_text(dump, &size);
    CHECK(bytes && size == 768 && !memcmp(bytes, rgb, 768));
    free(bytes);
    CHECK(vc_dumps == 4 && vc_dump_failures == 0);   /* frames a, b, zero frame; one table */
    free(text);
    puts("PASS: capture records (palette, deduplicated presents, clear, end), hashes and dumps.");
    return 0;
}

/* Schema 2 (qpc=1): the start record names the version and the counter's frequency, every record has
 * "qpc", and a present or clear record has the value read on entry to the hook, before the blit. */
static unsigned pump(DWORD ret, MSG got[16]);
static int test_qpc(void) {
    CHECK(make_image());
    CHECK(install_vshell_capture_image(image));
    WCHAR journal[MAX_PATH]; path_in(journal, L"tour.jsonl");
    CHECK(write_file(journal, "{\"op\":\"mark\",\"screen\":\"main_menu\",\"state\":\"idle\"}\n{\"op\":\"snap\"}\n{\"op\":\"close\"}\n"));
    char extra[600];
    snprintf(extra, sizeof(extra), "capture=1\r\nqpc=1\r\njournal=%ls\r\n", journal);
    CHECK(configure(extra));
    vc_qpc_source = fake_qpc;
    for (unsigned i = 0; i < VC_FRAME_BYTES; ++i) frame[i] = (BYTE)(i * 5);
    vc_ready();                                                   /* the start record reads the counter once */
    CHECK(vc_qpc_on && vc_active && vc_qpc_frequency > 0 && fake_qpc_value == 10);
    char needle[96];
    uint64_t want = fake_qpc_value + 10;
    CHECK(vc_bitblt_at(VC_PRESENT_RET, NULL, 0, 0, 640, 480, NULL, 0, 0, SRCCOPY));
    uint64_t present_a = want;
    CHECK(fake_qpc_value == want + 1000 && vc_stamp == 0);        /* the stamp was read before the 1000-tick blit */
    CHECK(vc_bitblt_at(VC_PRESENT_RET, NULL, 0, 0, 640, 480, NULL, 0, 0, SRCCOPY));   /* unchanged: no record, a read */
    CHECK(vc_bitblt_at(0x1234, NULL, 0, 0, 1, 1, NULL, 0, 0, SRCCOPY));               /* other site: no read at all */
    CHECK(fake_qpc_value == want + 1000 + 10 + 1000 + 1000);
    frame[77] ^= 0x33;
    want = fake_qpc_value + 10;
    CHECK(vc_bitblt_at(VC_PRESENT_RET, NULL, 0, 0, 640, 480, NULL, 0, 0, SRCCOPY));
    uint64_t present_b = want;
    want = fake_qpc_value + 10;
    CHECK(vc_stretchblt_at(VC_CLEAR_RET, NULL, 0, 0, 640, 480, NULL, 0, 0, 10, 10, SRCCOPY));
    uint64_t clear = want;
    MSG got[16];
    CHECK(pump(VC_PUMP_RET, got) >= 1);                           /* mark, snap and the first message of the journal */
    CHECK(pump(VC_PUMP_RET, got) >= 0);
    uint64_t end = fake_qpc_value + 10;
    vshell_capture_shutdown();
    WCHAR log[MAX_PATH]; path_in(log, L"capture.jsonl");
    char *text = read_text(log, NULL);
    CHECK(text);
    CHECK(!strncmp(text, "{\"kind\":\"start\",\"seq\":0,", 24) && strstr(text, "\"qpc\":10,\"schema\":2,\"pid\":"));
    CHECK(strstr(text, ",\"dump\":false,\"qpc_frequency\":"));
    snprintf(needle, sizeof(needle), "\"qpc\":%llu,\"site\":\"0x30006e57\"", (unsigned long long)present_a);
    CHECK(count_of(text, needle) == 1);
    snprintf(needle, sizeof(needle), "\"qpc\":%llu,\"site\":\"0x30006e57\"", (unsigned long long)present_b);
    CHECK(count_of(text, needle) == 1);
    snprintf(needle, sizeof(needle), "\"qpc\":%llu,\"site\":\"0x30007924\"", (unsigned long long)clear);
    CHECK(count_of(text, needle) == 1);
    snprintf(needle, sizeof(needle), "\"qpc\":%llu,\"presents\":3", (unsigned long long)end);
    CHECK(count_of(text, needle) == 1);                           /* the end record */
    CHECK(count_of(text, "\"kind\":\"present\"") == 2 && count_of(text, "\"kind\":\"snap\"") == 1);
    CHECK(count_of(text, "\"qpc\":") == count_of(text, "\"kind\":"));   /* every record has a time */
    CHECK(present_a < present_b && present_b < clear && clear < end);
    free(text);
    puts("PASS: schema 2 (qpc=1): version and frequency in the start record, a time on every record, present/clear read before the blit.");
    return 0;
}

/* Runs one pump pass the way 0x406328 does; returns the injected messages in `got`. */
static unsigned pump(DWORD ret, MSG got[16]) {
    unsigned n = 0;
    MSG m;
    while (vc_peek_at(ret, &m, NULL, 0, 0, PM_REMOVE)) {
        if (n < 16) got[n++] = m;
        if (m.message == WM_QUIT) break;
    }
    return n;
}

static int test_journal(void) {
    CHECK(make_image());
    CHECK(install_vshell_capture_image(image));
    WCHAR journal[MAX_PATH]; path_in(journal, L"tour.jsonl");
    CHECK(write_file(journal,
        "{\"op\":\"mark\",\"screen\":\"main_menu\",\"state\":\"idle\"}\n"
        "{\"op\":\"snap\"}\n"
        "{\"op\":\"click\",\"x\":392,\"y\":230}\n"
        "{\"op\":\"key\",\"vk\":65,\"mods\":1}\n"
        "{\"op\":\"close\"}\n"));
    char extra[600];
    snprintf(extra, sizeof(extra), "journal=%ls\r\n", journal);
    CHECK(configure(extra));
    for (unsigned i = 0; i < VC_FRAME_BYTES; ++i) frame[i] = (BYTE)i;
    char frame_hex[65]; sha_hex(frame, VC_FRAME_BYTES, frame_hex);
    MSG got[16];
    /* Another caller of PeekMessageA (not the pump) is never served from the journal. */
    MSG m;
    CHECK(!vc_peek_at(0x1111, &m, NULL, 0, 0, PM_REMOVE) && vc_pass == 0);
    /* Before any injected mouse message or warp the real pointer is read. */
    POINT pt = {0, 0};
    unsigned real_reads = get_cursor_calls;
    CHECK(vc_get_cursor_at(VC_CURSOR_GET_RET, &pt) && pt.x == 7 && pt.y == 9 && get_cursor_calls == real_reads + 1);
    /* Pass 0: mark + snap + click phase A (three moves); real input is dropped, other messages pass. */
    push(WM_MOUSEMOVE, 0, MAKELPARAM(5, 5)); push(WM_KEYDOWN, 'B', 1); push(WM_TIMER, 1, 0);
    unsigned n = pump(VC_PUMP_RET, got);
    CHECK(n == 4 && got[0].message == WM_MOUSEMOVE && got[0].lParam == MAKELPARAM(639, 479) && got[0].hwnd == (HWND)(uintptr_t)0x4321);
    CHECK(got[1].lParam == 0 && got[2].lParam == MAKELPARAM(392, 230) && got[3].message == WM_TIMER && vc_dropped == 2 && vc_pass == 1);
    /* The pointer follows the last injected move: VSHELL's read at 0x420160 gets it in screen coordinates;
       another caller and the real pointer are untouched. */
    CHECK(vc_get_cursor_at(VC_CURSOR_GET_RET, &pt) && pt.x == 492 && pt.y == 280 && get_cursor_calls == real_reads + 1);
    CHECK(vc_get_cursor_at(0x2222, &pt) && pt.x == 7 && pt.y == 9 && get_cursor_calls == real_reads + 2);
    /* A warp at a known site moves the virtual pointer only; another site reaches the real SetCursorPos. */
    unsigned real_sets = set_cursor_calls;
    CHECK(vc_set_cursor_at(0x775du, 420, 290) && set_cursor_calls == real_sets && vc_pointer.x == 320 && vc_pointer.y == 240);
    CHECK(vc_set_cursor_at(0x3333, 1, 1) && set_cursor_calls == real_sets + 1 && vc_pointer.x == 320);
    CHECK(vc_get_cursor_at(VC_CURSOR_GET_RET, &pt) && pt.x == 420 && pt.y == 290);
    /* Focus lost: WM_SETFOCUS is injected, the click waits. */
    put32(VC_FOCUS_RVA, 0);
    n = pump(VC_PUMP_RET, got);
    CHECK(n == 1 && got[0].message == WM_SETFOCUS && vc_focus_injected == 1);
    n = pump(VC_PUMP_RET, got);                                   /* within 50 passes: nothing more */
    CHECK(n == 0 && vc_focus_injected == 1);
    put32(VC_FOCUS_RVA, 1);
    put32(VC_MOVIE_RVA, 1);                                       /* a movie blocks the journal */
    CHECK(pump(VC_PUMP_RET, got) == 0);
    put32(VC_MOVIE_RVA, 0);
    n = pump(VC_PUMP_RET, got);
    CHECK(n == 1 && got[0].message == WM_LBUTTONDOWN && got[0].wParam == MK_LBUTTON && got[0].lParam == MAKELPARAM(392, 230));
    CHECK(vc_pointer.x == 392 && vc_pointer.y == 230);           /* the button message sets it back */
    CHECK(pump(VC_PUMP_RET, got) == 0);
    n = pump(VC_PUMP_RET, got);
    CHECK(n == 1 && got[0].message == WM_LBUTTONUP);
    /* Key with Shift: GetKeyState answers from the journal only while the key message is handled. */
    CHECK(vc_peek_at(VC_PUMP_RET, &m, NULL, 0, 0, PM_REMOVE) && m.message == WM_KEYDOWN && m.wParam == 65);
    CHECK(vc_key_state(VK_SHIFT) == (SHORT)0x8000 && vc_key_state(VK_CONTROL) == 0 && vc_key_state(VK_MENU) == 0);
    CHECK(vc_key_state('A') == 0 && key_calls == 1);              /* other keys: forwarded */
    CHECK(!vc_peek_at(VC_PUMP_RET, &m, NULL, 0, 0, PM_REMOVE) && vc_key_state(VK_SHIFT) == 0);
    CHECK(pump(VC_PUMP_RET, got) == 0);
    n = pump(VC_PUMP_RET, got);
    CHECK(n == 1 && got[0].message == WM_KEYUP && ((DWORD)got[0].lParam & 0xc0000000u) == 0xc0000000u);
    n = pump(VC_PUMP_RET, got);
    CHECK(n == 1 && got[0].message == WM_CLOSE && vc_runner.done);
    put32(VC_FOCUS_RVA, 0);                                       /* journal done: no more focus injection */
    CHECK(pump(VC_PUMP_RET, got) == 0 && vc_focus_injected == 1);
    vshell_capture_shutdown();
    WCHAR log[MAX_PATH]; path_in(log, L"capture.jsonl");
    char *text = read_text(log, NULL);
    CHECK(text);
    CHECK(count_of(text, "\"kind\":\"step\"") == 5 && strstr(text, "\"op\":\"click\",\"line\":3,\"x\":392,\"y\":230"));
    CHECK(count_of(text, "\"kind\":\"snap\"") == 1 && strstr(text, frame_hex) && strstr(text, "\"screen\":\"main_menu\",\"state\":\"idle\""));
    CHECK(count_of(text, "\"kind\":\"focus_injected\"") == 1 && count_of(text, "\"kind\":\"journal_done\"") == 1);
    CHECK(strstr(text, "\"injected\":9,\"dropped\":2,\"focus_injected\":1,\"journal_done\":true"));
    CHECK(count_of(text, "\"kind\":\"warp\"") == 1 && strstr(text, "\"site\":\"0x30007758\",\"x\":320,\"y\":240"));
    CHECK(strstr(text, "\"pointer_reads\":3,\"pointer_real_reads\":1,\"pointer_warps\":1"));
    CHECK(!strstr(text, "\"kind\":\"pointer\""));             /* the snap came before any pointer */
    free(text);
    puts("PASS: journal replay (pump key, injected forms, virtual pointer and warps, dropped real input, focus, movie gate, modifiers, close).");
    return 0;
}

static int test_refused(void) {
    CHECK(make_image());
    CHECK(install_vshell_capture_image(image));
    WCHAR journal[MAX_PATH]; path_in(journal, L"tour.jsonl");
    CHECK(write_file(journal, "{\"op\":\"close\"}\n"));
    char extra[600];
    snprintf(extra, sizeof(extra), "journal=%ls\r\n", journal);
    CHECK(configure(extra));
    module_ok = FALSE;
    MSG got[16];
    CHECK(vc_bitblt_at(VC_PRESENT_RET, NULL, 0, 0, 1, 1, NULL, 0, 0, SRCCOPY) && bitblt_calls == 1);
    CHECK(pump(VC_PUMP_RET, got) == 0 && vc_injected == 0);
    CHECK(vc_key_state(VK_SHIFT) == (SHORT)0x8000);               /* forwarded */
    vshell_capture_shutdown();
    WCHAR log[MAX_PATH]; path_in(log, L"capture.jsonl");
    char *text = read_text(log, NULL);
    CHECK(text && strstr(text, "\"kind\":\"refused\"") && strstr(text, "\"module_sha256_ok\":false") &&
          !strstr(text, "\"kind\":\"present\"") && !strstr(text, "\"kind\":\"start\""));
    free(text);
    /* A bad journal refuses too. */
    puts("PASS: a module hash mismatch leaves every hook forwarding.");
    return 0;
}

static int test_bad_journal(void) {
    CHECK(make_image());
    CHECK(install_vshell_capture_image(image));
    WCHAR journal[MAX_PATH]; path_in(journal, L"tour.jsonl");
    CHECK(write_file(journal, "{\"op\":\"snap\"}\n{\"op\":\"click\",\"control\":\"main_menu.05\"}\n"));
    char extra[600];
    snprintf(extra, sizeof(extra), "journal=%ls\r\n", journal);
    CHECK(configure(extra));
    MSG got[16];
    CHECK(pump(VC_PUMP_RET, got) == 0 && !vc_active);
    vshell_capture_shutdown();
    WCHAR log[MAX_PATH]; path_in(log, L"capture.jsonl");
    char *text = read_text(log, NULL);
    CHECK(text && strstr(text, "\"kind\":\"refused\"") && strstr(text, "\"journal_entries\":-2"));
    free(text);
    puts("PASS: an unresolved control refuses the journal (line 2) and the capture.");
    return 0;
}

static int test_mismatch(void) {
    CHECK(make_image());
    image[0x5230 + 3] ^= 1;                                       /* one byte of the GetKeyState signature */
    DWORD before; memcpy(&before, image + vc_slots[0].iat_rva, 4);
    CHECK(!install_vshell_capture_image(image));
    DWORD after; memcpy(&after, image + vc_slots[0].iat_rva, 4);
    CHECK(before == after && after == (DWORD)(uintptr_t)fake_bitblt && !install_vshell_capture_image(image));
    puts("PASS: a changed signature byte refuses the installation; no slot changes.");
    return 0;
}

static int test_off(void) {
    /* Without ES2_VSHELL_CAPTURE_CONFIG nothing is installed; with it, only a process named VSHELL.EXE is. */
    SetEnvironmentVariableW(L"ES2_VSHELL_CAPTURE_CONFIG", NULL);
    install_vshell_capture();
    CHECK(vc_install_state == 0 && !vc_config[0]);
    SetEnvironmentVariableW(L"ES2_VSHELL_CAPTURE_CONFIG", L"C:\\nowhere\\capture.ini");
    install_vshell_capture();
    CHECK(vc_install_state == 0 && !vc_image);
    vshell_capture_shutdown();                                    /* nothing open: no effect */
    puts("PASS: off without the variable and outside VSHELL.EXE.");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    DWORD n = GetEnvironmentVariableW(L"TEST_TMPDIR", directory, MAX_PATH);
    if (!n || n >= MAX_PATH - 64) GetTempPathW(MAX_PATH, directory);
    size_t length = wcslen(directory);
    if (length && directory[length - 1] == L'\\') directory[length - 1] = 0;
    if (!strcmp(argv[1], "parse")) return test_parse();
    if (!strcmp(argv[1], "capture")) return test_capture();
    if (!strcmp(argv[1], "qpc")) return test_qpc();
    if (!strcmp(argv[1], "journal")) return test_journal();
    if (!strcmp(argv[1], "refused")) return test_refused();
    if (!strcmp(argv[1], "bad_journal")) return test_bad_journal();
    if (!strcmp(argv[1], "mismatch")) return test_mismatch();
    if (!strcmp(argv[1], "off")) return test_off();
    return 2;
}
