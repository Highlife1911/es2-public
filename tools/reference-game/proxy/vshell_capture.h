/* VSHELL frame capture and input journal replay, in memory only.
 * Contracts: docs/reverse-engineering/vshell_presentation.md ("Present
 * sites"; section "capture tour" for the run and the record schema),
 * vshell_widgets.md section 7.6 (journal forms), vshell_process.md
 * sections 7 and 8 (window procedure, focus flag, pump).
 *
 * Off unless the environment variable ES2_VSHELL_CAPTURE_CONFIG names an INI
 * and the process is VSHELL.EXE: then DllMain (kernel32 only, no file I/O)
 * checks the image (base, size, the seven import thunks and the code bytes
 * below) and replaces seven import slots of the executable:
 *
 *   BitBlt            IAT 0x5de3f0, thunk 0x46bb38; present  0x406e52 (return 0x406e57)
 *   StretchBlt        IAT 0x5de3f4, thunk 0x46bb3e; clear    0x40791f (return 0x407924)
 *   SetDIBColorTable  IAT 0x5de3f8, thunk 0x46bb44; palette  0x4072f3 (return 0x4072f8)
 *   PeekMessageA      IAT 0x5de414, thunk 0x46bb68; pump     0x40637a (return 0x40637f)
 *   GetKeyState       IAT 0x5de440, thunk 0x46bbaa; keys     0x405232, 0x405243, 0x405254
 *   GetCursorPos      IAT 0x5de448, thunk 0x46bbb6; pointer  0x420160 (return 0x420165)
 *   SetCursorPos      IAT 0x5de49c, thunk 0x46bc34; warps    0x405bf0, 0x407758, 0x420149
 *
 * Pointer: VSHELL takes the position of every posted event from
 * GetCursorPos (display slot 0x420152, called from 0x4689c1), not from the
 * message's lParam, so with a journal the hooks keep a virtual pointer in
 * client coordinates: an injected mouse message moves it to its lParam, the
 * three SetCursorPos sites move it (the real pointer is not moved), and the
 * GetCursorPos site reads it back in screen coordinates. Until the first
 * injected mouse message or warp the real pointer is read.
 *
 * At the first hooked call the INI is read and the SHA-256 of the executable
 * file is compared with the reference (1534aa8f...a16e); on any refusal every
 * hook only forwards. Each hook calls the previous slot value, so it chains
 * with other slot users. The executable file is never changed.
 *
 * [vshell_capture] log=<path>   JSONL record file, created new (an existing file refuses)
 *                  capture=1    frame records at the present and clear sites
 *                  every=0      1: a record for every present, else only when the frame or
 *                               colour table hash changed since the last record
 *                  dump_dir=    optional: raw frames <index_sha256>.idx (307,200 bytes) and
 *                               colour tables <palette_sha256>.rgb (768 bytes) once each
 *                  journal=     optional: an input journal (vshell_journal.h) to replay
 *                  hold_focus=1 with a journal: inject WM_SETFOCUS while the focus flag is 0
 *                  block_real_input=1  with a journal: drop real keyboard and mouse messages
 *                  qpc=1        off by default: record schema 2. The start record says "schema":2 and
 *                               "qpc_frequency" (QueryPerformanceFrequency), and every record carries "qpc",
 *                               the QueryPerformanceCounter value of the moment it was made; a present or clear
 *                               record carries the value read on entry to the hooked BitBlt/StretchBlt, before
 *                               the call is forwarded. Without the key the bytes of every record are schema 1's
 *                               (no "qpc" field, "schema":1). Readers take both versions: a record without
 *                               "qpc" has no time (docs/reverse-engineering/vshell_ui.md section 42.5).
 *
 * The capture reads game memory only (frame [0x481950], memory DC [0x48195c],
 * DirectDraw flag [0x481f4c], focus [0x46c288], movie [0x470eb0], window
 * [0x481f38]); it writes none and calls no VSHELL code. The journal injects
 * messages only through PeekMessageA's result in the pump and answers the
 * pointer through GetCursorPos (above). */
#ifndef ES2_VSHELL_CAPTURE_H
#define ES2_VSHELL_CAPTURE_H
#include <stdint.h>
#include <stdio.h>
#include "sha256.h"
#include "vshell_journal.h"

#ifndef VC_BASE
#define VC_BASE ((uintptr_t)0x00400000)
#endif
#define VC_IMAGE_SIZE 0x001ea000u
#define VC_FRAME_W 640u
#define VC_FRAME_H 480u
#define VC_FRAME_BYTES (VC_FRAME_W * VC_FRAME_H)
/* Globals (RVAs of dword variables; vshell_process.md, vshell_presentation.md, confirmed dword accesses in the listing). */
#define VC_BITS_RVA 0x00081950u
#define VC_MEMDC_RVA 0x0008195cu
#define VC_WINDOW_RVA 0x00081f38u
#define VC_DD_RVA 0x00081f4cu
#define VC_FOCUS_RVA 0x0006c288u
#define VC_MOVIE_RVA 0x00070eb0u
/* Return addresses (RVAs) of the one call of each import. */
#define VC_PRESENT_RET 0x00006e57u
#define VC_CLEAR_RET 0x00007924u
#define VC_PALETTE_RET 0x000072f8u
#define VC_PUMP_RET 0x0000637fu
#define VC_CURSOR_GET_RET 0x00020165u
static const DWORD vc_cursor_set_rets[] = {0x00005bf5u, 0x0000775du, 0x0002014eu};

static const BYTE vc_sig_present_prologue[] = {0x55,0x8b,0xec,0x81,0xc4,0x2c,0xff,0xff,0xff,0x53,0x56,0x57};
static const BYTE vc_sig_present_call[] = {0xe8,0xe1,0x4c,0x06,0x00};
static const BYTE vc_sig_palette_call[] = {0xe8,0x4c,0x48,0x06,0x00};
static const BYTE vc_sig_clear_call[] = {0xe8,0x1a,0x42,0x06,0x00};
static const BYTE vc_sig_flush_rect[] = {0x55,0x8b,0xec,0x53,0x8b,0x45,0x0c,0x8b,0x5d,0x08,0x50,0x50,0xe8,0xbe,0x6d,0xfe,0xff};
static const BYTE vc_sig_dd_unlock_present[] = {0xff,0x91,0x80,0x00,0x00,0x00};
static const BYTE vc_sig_dd_unlock_clear[] = {0xff,0x92,0x80,0x00,0x00,0x00};
static const BYTE vc_sig_pump[] = {0x6a,0x01,0x6a,0x00,0x6a,0x00,0x6a,0x00,0x8d,0x45,0xe4,0x50,0xe8,0xe9,0x57,0x06,0x00,0x85,0xc0,0x75,0xad};
static const BYTE vc_sig_keys[] = {
    0x6a,0x10,0xe8,0x73,0x69,0x06,0x00,0x66,0x85,0xc0,0x7d,0x05,0x66,0x81,0xce,0x00,0x08,
    0x6a,0x12,0xe8,0x62,0x69,0x06,0x00,0x66,0x85,0xc0,0x7d,0x05,0x66,0x81,0xce,0x00,0x02,
    0x6a,0x11,0xe8,0x51,0x69,0x06,0x00,0x66,0x85,0xc0,0x7d,0x05,0x66,0x81,0xce,0x00,0x04
};
static const BYTE vc_sig_cursor_get[] = {0x8d,0x45,0xf8,0x50,0xe8,0x51,0xba,0x04,0x00};
static const BYTE vc_sig_cursor_set_logical[] = {0x56,0xe8,0x3f,0x60,0x06,0x00};
static const BYTE vc_sig_cursor_set_centre[] = {0x53,0xe8,0xd7,0x44,0x06,0x00};
static const BYTE vc_sig_cursor_set_display[] = {0xff,0x75,0xfc,0xff,0x75,0xf8,0xe8,0xe6,0xba,0x04,0x00};
struct VcSignature { DWORD rva; const BYTE *bytes; SIZE_T size; };
static const struct VcSignature vc_signatures[] = {
    {0x00006bfbu, vc_sig_present_prologue, sizeof(vc_sig_present_prologue)},
    {0x00006e52u, vc_sig_present_call, sizeof(vc_sig_present_call)},
    {0x000072f3u, vc_sig_palette_call, sizeof(vc_sig_palette_call)},
    {0x0000791fu, vc_sig_clear_call, sizeof(vc_sig_clear_call)},
    {0x0001fe2cu, vc_sig_flush_rect, sizeof(vc_sig_flush_rect)},
    {0x00006d67u, vc_sig_dd_unlock_present, sizeof(vc_sig_dd_unlock_present)},
    {0x00007888u, vc_sig_dd_unlock_clear, sizeof(vc_sig_dd_unlock_clear)},
    {0x0000636eu, vc_sig_pump, sizeof(vc_sig_pump)},
    {0x00005230u, vc_sig_keys, sizeof(vc_sig_keys)},
    {0x0002015cu, vc_sig_cursor_get, sizeof(vc_sig_cursor_get)},
    {0x00005befu, vc_sig_cursor_set_logical, sizeof(vc_sig_cursor_set_logical)},
    {0x00007757u, vc_sig_cursor_set_centre, sizeof(vc_sig_cursor_set_centre)},
    {0x00020143u, vc_sig_cursor_set_display, sizeof(vc_sig_cursor_set_display)},
};

typedef BOOL (WINAPI *VcBitBlt)(HDC, int, int, int, int, HDC, int, int, DWORD);
typedef BOOL (WINAPI *VcStretchBlt)(HDC, int, int, int, int, HDC, int, int, int, int, DWORD);
typedef UINT (WINAPI *VcSetDibColors)(HDC, UINT, UINT, const RGBQUAD *);
typedef BOOL (WINAPI *VcPeek)(LPMSG, HWND, UINT, UINT, UINT);
typedef SHORT (WINAPI *VcKeyState)(int);
typedef BOOL (WINAPI *VcGetCursor)(LPPOINT);
typedef BOOL (WINAPI *VcSetCursor)(int, int);
typedef BOOL (WINAPI *VcMapPoint)(HWND, LPPOINT);

static VcBitBlt vc_real_bitblt;
static VcStretchBlt vc_real_stretchblt;
static VcSetDibColors vc_real_set_dib;
static VcPeek vc_real_peek;
static VcKeyState vc_real_key;
static VcGetCursor vc_real_get_cursor;
static VcSetCursor vc_real_set_cursor;

static BYTE *vc_image;
static volatile LONG vc_install_state;     /* 0 not tried, 1 installed, -1 refused */
static INIT_ONCE vc_once = INIT_ONCE_STATIC_INIT;
static WCHAR vc_config[MAX_PATH];
static const char vc_reference_sha256[] = "1534aa8f732bdecd826484d893ad58b8a5e6ebd4d25ab3158a2469b8f69ba16e";
/* State after configuration. */
static int vc_active, vc_capture, vc_every, vc_hold_focus, vc_block_input;
/* Schema 2 (qpc=1). vc_stamp is the counter value taken on entry to a present or clear hook (0: none,
 * a record reads the counter itself). */
static int vc_qpc_on;
static uint64_t vc_qpc_frequency, vc_stamp;
static HANDLE vc_log = INVALID_HANDLE_VALUE;
static WCHAR vc_dump_dir[MAX_PATH];
static BYTE vc_rgb[768];
static char vc_palette_hex[65], vc_last_index_hex[65], vc_last_palette_hex[65];
static DWORD vc_seq, vc_presents, vc_presents_since, vc_uploads, vc_records, vc_dumps, vc_dump_failures;
/* Journal. */
static VjEntry *vc_entries;
static VjRunner vc_runner;
static int vc_journal;
static DWORD vc_pass, vc_injected, vc_dropped, vc_focus_injected, vc_last_focus_pass;
static int vc_in_pass;
static VjMsg vc_pending[4];
static unsigned vc_pending_count, vc_pending_next;
static DWORD vc_mods;                         /* modifiers of the injected message being handled */
static POINT vc_pointer;                      /* virtual pointer, client coordinates */
static int vc_pointer_known;
static DWORD vc_pointer_reads, vc_pointer_real_reads, vc_pointer_warps;

/* Tests replace these (synthetic module, synthetic clock, point mapping). */
static BOOL vc_module_matches(void);
static BOOL (*vc_module_check)(void) = vc_module_matches;
static DWORD (WINAPI *vc_clock)(void) = GetTickCount;
static uint64_t vc_qpc_default(void) { LARGE_INTEGER n; QueryPerformanceCounter(&n); return (uint64_t)n.QuadPart; }
static uint64_t (*vc_qpc_source)(void) = vc_qpc_default;
static VcMapPoint vc_client_to_screen = ClientToScreen, vc_screen_to_client = ScreenToClient;

static DWORD vc_read32(DWORD rva) { DWORD v; memcpy(&v, vc_image + rva, 4); return v; }

static BOOL vc_readable(const void *pointer, SIZE_T size) {
    uintptr_t cursor = (uintptr_t)pointer;
    if (!cursor || size > UINTPTR_MAX - cursor) return FALSE;
    uintptr_t end = cursor + size;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION region;
        if (!VirtualQuery((void *)cursor, &region, sizeof(region)) || region.State != MEM_COMMIT ||
            (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return FALSE;
        uintptr_t next = (uintptr_t)region.BaseAddress + region.RegionSize;
        if (next <= cursor) return FALSE;
        cursor = next;
    }
    return TRUE;
}

/* ---- records ---------------------------------------------------------------------------------- */
static void vc_write(const char *line, int n) {
    DWORD written;
    if (vc_log == INVALID_HANDLE_VALUE || n <= 0) return;
    WriteFile(vc_log, line, (DWORD)n, &written, NULL);
    ++vc_records;
}

static void vc_quote_hex(char *out, size_t size, const char *hex) {
    if (hex && hex[0]) snprintf(out, size, "\"%s\"", hex); else snprintf(out, size, "null");
}

/* SHA-256 of the 640 x 480 frame; empty string when the frame is not readable. */
static const BYTE *vc_frame(void) {
    const BYTE *bits = (const BYTE *)(uintptr_t)vc_read32(VC_BITS_RVA);
    return bits && vc_readable(bits, VC_FRAME_BYTES) ? bits : NULL;
}
static void vc_hash_frame(const BYTE *bits, char hex[65]) {
    hex[0] = 0;
    if (!bits) return;
    sha256_ctx s; sha256_init(&s); sha256_update(&s, bits, VC_FRAME_BYTES); sha256_hex(&s, hex);
}

static void vc_dump(const char *hex, const char *suffix, const BYTE *bytes, DWORD size) {
    if (!vc_dump_dir[0] || !hex[0] || !bytes) return;
    WCHAR path[MAX_PATH];
    if (_snwprintf(path, MAX_PATH, L"%ls\\%hs%hs", vc_dump_dir, hex, suffix) >= MAX_PATH) { ++vc_dump_failures; return; }
    path[MAX_PATH - 1] = 0;
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) { if (GetLastError() != ERROR_FILE_EXISTS) ++vc_dump_failures; return; }
    DWORD written = 0;
    BOOL ok = WriteFile(f, bytes, size, &written, NULL) && written == size;
    CloseHandle(f);
    if (ok) ++vc_dumps; else ++vc_dump_failures;
}

/* Common prefix: kind, sequence, pass, journal step and labels. */
static int vc_prefix(char *line, size_t size, const char *kind) {
    int n = snprintf(line, size, "{\"kind\":\"%s\",\"seq\":%lu,\"pass\":%lu,\"step\":%lu,\"screen\":\"%s\",\"state\":\"%s\"",
        kind, (unsigned long)vc_seq++, (unsigned long)vc_pass, (unsigned long)(vc_journal ? vc_runner.index : 0),
        vc_journal ? vc_runner.screen : "none", vc_journal ? vc_runner.state : "");
    if (vc_qpc_on)   /* schema 2 */
        n += snprintf(line + n, size - (size_t)n, ",\"qpc\":%llu", (unsigned long long)(vc_stamp ? vc_stamp : vc_qpc_source()));
    return n;
}

/* A frame record (present, clear, snap). force: written even when nothing changed. */
static void vc_frame_record(const char *kind, DWORD site, const int dst[4], const int src[2], BOOL force) {
    const BYTE *bits = vc_frame();
    char index_hex[65];
    vc_hash_frame(bits, index_hex);
    if (!force && !vc_every && !strcmp(index_hex, vc_last_index_hex) && !strcmp(vc_palette_hex, vc_last_palette_hex)) return;
    strcpy(vc_last_index_hex, index_hex);
    strcpy(vc_last_palette_hex, vc_palette_hex);
    char line[768], index_json[72], palette_json[72];
    vc_quote_hex(index_json, sizeof(index_json), index_hex);
    vc_quote_hex(palette_json, sizeof(palette_json), vc_palette_hex);
    int n = vc_prefix(line, sizeof(line), kind);
    if (site) n += snprintf(line + n, sizeof(line) - (size_t)n, ",\"site\":\"0x%06lx\"", (unsigned long)(site + VC_BASE));
    if (dst) n += snprintf(line + n, sizeof(line) - (size_t)n, ",\"dst\":[%d,%d,%d,%d]", dst[0], dst[1], dst[2], dst[3]);
    if (src) n += snprintf(line + n, sizeof(line) - (size_t)n, ",\"src\":[%d,%d]", src[0], src[1]);
    n += snprintf(line + n, sizeof(line) - (size_t)n,
        ",\"presents\":%lu,\"uploads\":%lu,\"dd\":%lu,\"focus\":%lu,\"index_sha256\":%s,\"palette_sha256\":%s}\r\n",
        (unsigned long)vc_presents_since, (unsigned long)vc_uploads, (unsigned long)vc_read32(VC_DD_RVA),
        (unsigned long)vc_read32(VC_FOCUS_RVA), index_json, palette_json);
    vc_presents_since = 0;
    vc_write(line, n);
    vc_dump(index_hex, ".idx", bits, VC_FRAME_BYTES);
    if (vc_palette_hex[0]) vc_dump(vc_palette_hex, ".rgb", vc_rgb, sizeof(vc_rgb));
}

static void vc_event_record(const char *kind, const char *extra) {
    char line[640];   /* the schema 2 prefix is 30 bytes longer */
    int n = vc_prefix(line, sizeof(line), kind);
    n += snprintf(line + n, sizeof(line) - (size_t)n, "%s}\r\n", extra ? extra : "");
    vc_write(line, n);
}

static void vc_journal_event(void *context, int kind, uint32_t step, const VjEntry *e) {
    (void)context; (void)step;
    char extra[160];
    if (kind == VJ_EVENT_STEP) {
        snprintf(extra, sizeof(extra), ",\"op\":\"%s\",\"line\":%lu,\"x\":%ld,\"y\":%ld,\"vk\":%lu,\"mods\":%lu",
            vj_op_names[e->op], (unsigned long)e->line, (long)e->x, (long)e->y, (unsigned long)e->vk, (unsigned long)e->mods);
        vc_event_record("step", extra);
    } else if (kind == VJ_EVENT_SNAP) {
        vc_frame_record("snap", 0, NULL, NULL, TRUE);
        if (vc_pointer_known) {
            snprintf(extra, sizeof(extra), ",\"x\":%ld,\"y\":%ld", (long)vc_pointer.x, (long)vc_pointer.y);
            vc_event_record("pointer", extra);
        }
    } else if (kind == VJ_EVENT_DONE) {
        vc_event_record("journal_done", NULL);
    }
}

/* ---- configuration (first hooked call; never under the loader lock) --------------------------- */
static BOOL vc_module_matches(void) {
    WCHAR path[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (!n || n >= MAX_PATH) return FALSE;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return FALSE;
    sha256_ctx s; sha256_init(&s);
    static BYTE buffer[65536];
    DWORD got;
    BOOL ok = TRUE;
    while ((ok = ReadFile(f, buffer, sizeof(buffer), &got, NULL)) && got) sha256_update(&s, buffer, got);
    CloseHandle(f);
    char hex[65];
    sha256_hex(&s, hex);
    return ok && !strcmp(hex, vc_reference_sha256);
}

static BOOL vc_load_journal(const WCHAR *path, long *result) {
    *result = 0;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return FALSE;
    DWORD size = GetFileSize(f, NULL), got = 0;
    char *text = size != INVALID_FILE_SIZE && size < (VJ_ENTRY_MAX * VJ_LINE_MAX) ? malloc((size_t)size + 1) : NULL;
    BOOL ok = text && ReadFile(f, text, size, &got, NULL) && got == size;
    CloseHandle(f);
    vc_entries = ok ? calloc(VJ_ENTRY_MAX, sizeof(VjEntry)) : NULL;
    if (vc_entries) *result = vj_parse(text, size, vc_entries);
    free(text);
    return vc_entries && *result >= 0;
}

static BOOL CALLBACK vc_configure(PINIT_ONCE once, PVOID arg, PVOID *context) {
    (void)once; (void)arg; (void)context;
    WCHAR log[MAX_PATH], journal[MAX_PATH];
    if (!GetPrivateProfileStringW(L"vshell_capture", L"log", L"", log, MAX_PATH, vc_config)) return TRUE;
    vc_log = CreateFileW(log, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (vc_log == INVALID_HANDLE_VALUE) return TRUE;
    vc_capture = GetPrivateProfileIntW(L"vshell_capture", L"capture", 1, vc_config) != 0;
    vc_every = GetPrivateProfileIntW(L"vshell_capture", L"every", 0, vc_config) != 0;
    vc_hold_focus = GetPrivateProfileIntW(L"vshell_capture", L"hold_focus", 1, vc_config) != 0;
    vc_block_input = GetPrivateProfileIntW(L"vshell_capture", L"block_real_input", 1, vc_config) != 0;
    GetPrivateProfileStringW(L"vshell_capture", L"dump_dir", L"", vc_dump_dir, MAX_PATH, vc_config);
    GetPrivateProfileStringW(L"vshell_capture", L"journal", L"", journal, MAX_PATH, vc_config);
    vc_qpc_on = GetPrivateProfileIntW(L"vshell_capture", L"qpc", 0, vc_config) != 0;   /* schema 2 */
    if (vc_qpc_on) { LARGE_INTEGER f; f.QuadPart = 0; QueryPerformanceFrequency(&f); vc_qpc_frequency = (uint64_t)f.QuadPart; }
    BOOL module = vc_module_check();
    long parsed = 0;
    BOOL journal_ok = !journal[0] || vc_load_journal(journal, &parsed);
    char extra[384], qpc_field[64] = "";
    if (vc_qpc_on) snprintf(qpc_field, sizeof(qpc_field), ",\"qpc_frequency\":%llu", (unsigned long long)vc_qpc_frequency);
    snprintf(extra, sizeof(extra), ",\"schema\":%d,\"pid\":%lu,\"module_sha256_ok\":%s,\"capture\":%d,\"every\":%d,"
        "\"journal\":%s,\"journal_entries\":%ld,\"hold_focus\":%d,\"block_real_input\":%d,\"dump\":%s%s",
        vc_qpc_on ? 2 : 1, (unsigned long)GetCurrentProcessId(), module ? "true" : "false", vc_capture, vc_every,
        journal[0] ? "true" : "false", parsed, vc_hold_focus, vc_block_input, vc_dump_dir[0] ? "true" : "false", qpc_field);
    if (!module || !journal_ok) {
        vc_event_record("refused", extra);
        vc_capture = 0;
        return TRUE;
    }
    if (journal[0]) {
        vc_journal = 1;
        vj_init(&vc_runner, vc_entries, (uint32_t)parsed, vc_journal_event, NULL);
    }
    vc_active = 1;
    vc_event_record("start", extra);
    return TRUE;
}
static void vc_ready(void) { InitOnceExecuteOnce(&vc_once, vc_configure, NULL, NULL); }

/* ---- hooks (the inner forms take the return address; tests call them directly) ---------------- */
static BOOL vc_bitblt_at(DWORD ret_rva, HDC dc, int x, int y, int cx, int cy, HDC src, int x1, int y1, DWORD rop) {
    vc_ready();
    uint64_t stamp = vc_qpc_on && vc_active && vc_capture && ret_rva == VC_PRESENT_RET ? vc_qpc_source() : 0;   /* before the blit */
    BOOL result = vc_real_bitblt(dc, x, y, cx, cy, src, x1, y1, rop);
    if (vc_active && vc_capture && ret_rva == VC_PRESENT_RET) {
        ++vc_presents; ++vc_presents_since;
        int dst[4] = {x, y, cx, cy}, from[2] = {x1, y1};
        vc_stamp = stamp;
        vc_frame_record("present", ret_rva, dst, from, FALSE);
        vc_stamp = 0;
    }
    return result;
}
static BOOL vc_stretchblt_at(DWORD ret_rva, HDC dc, int x, int y, int cx, int cy, HDC src, int x1, int y1, int cx1, int cy1, DWORD rop) {
    vc_ready();
    uint64_t stamp = vc_qpc_on && vc_active && vc_capture && ret_rva == VC_CLEAR_RET ? vc_qpc_source() : 0;   /* before the blit */
    BOOL result = vc_real_stretchblt(dc, x, y, cx, cy, src, x1, y1, cx1, cy1, rop);
    if (vc_active && vc_capture && ret_rva == VC_CLEAR_RET) {
        ++vc_presents_since;
        int dst[4] = {x, y, cx, cy}, from[2] = {x1, y1};
        vc_stamp = stamp;
        vc_frame_record("clear", ret_rva, dst, from, TRUE);
        vc_stamp = 0;
    }
    return result;
}
static UINT vc_set_dib_at(DWORD ret_rva, HDC dc, UINT start, UINT count, const RGBQUAD *quads) {
    vc_ready();
    UINT result = vc_real_set_dib(dc, start, count, quads);
    if (vc_active && vc_capture && ret_rva == VC_PALETTE_RET && dc == (HDC)(uintptr_t)vc_read32(VC_MEMDC_RVA) &&
        quads && start < 256 && count <= 256 - start && vc_readable(quads, count * sizeof(RGBQUAD))) {
        /* Hash red, green, blue per entry (the quad's fourth byte is residue, vshell_presentation.md). */
        for (UINT i = 0; i < count; ++i) {
            vc_rgb[3 * (start + i)] = quads[i].rgbRed;
            vc_rgb[3 * (start + i) + 1] = quads[i].rgbGreen;
            vc_rgb[3 * (start + i) + 2] = quads[i].rgbBlue;
        }
        ++vc_uploads;
        sha256_ctx s; sha256_init(&s); sha256_update(&s, vc_rgb, sizeof(vc_rgb)); sha256_hex(&s, vc_palette_hex);
        char extra[160];
        snprintf(extra, sizeof(extra), ",\"start\":%u,\"count\":%u,\"uploads\":%lu,\"palette_sha256\":\"%s\"",
            start, count, (unsigned long)vc_uploads, vc_palette_hex);
        vc_event_record("palette", extra);
    }
    return result;
}

static BOOL vc_is_input(UINT message) {
    return (message >= 0x0100 && message <= 0x0109) || (message >= 0x0200 && message <= 0x020e);
}

static void vc_fill(LPMSG msg, const VjMsg *m) {
    memset(msg, 0, sizeof(*msg));
    msg->hwnd = (HWND)(uintptr_t)vc_read32(VC_WINDOW_RVA);
    msg->message = m->message;
    msg->wParam = m->wparam;
    msg->lParam = (LPARAM)m->lparam;
    if (m->message >= VJ_WM_KEYDOWN && m->message <= VJ_WM_SYSKEYUP)
        msg->lParam = (LPARAM)(m->lparam | (MapVirtualKeyA(m->wparam, 0 /* MAPVK_VK_TO_VSC */) & 0xffu) << 16);
    msg->time = vc_clock();
    vc_mods = m->mods;
    if (m->message >= VJ_WM_MOUSEMOVE && m->message <= 0x0209u) {
        vc_pointer.x = (LONG)(int16_t)(m->lparam & 0xffffu);
        vc_pointer.y = (LONG)(int16_t)(m->lparam >> 16);
        vc_pointer_known = 1;
    }
}

static BOOL vc_peek_at(DWORD ret_rva, LPMSG msg, HWND window, UINT first, UINT last, UINT flags) {
    vc_ready();
    vc_mods = 0;
    if (!vc_active || !vc_journal || ret_rva != VC_PUMP_RET || !(flags & PM_REMOVE) || window || first || last)
        return vc_real_peek(msg, window, first, last, flags);
    if (!vc_in_pass) {
        vc_in_pass = 1;
        vc_pending_count = vc_pending_next = 0;
        BOOL focused = vc_read32(VC_FOCUS_RVA) != 0, movie = vc_read32(VC_MOVIE_RVA) != 0;
        if (!focused && vc_hold_focus && !vc_runner.done && (!vc_focus_injected || vc_pass - vc_last_focus_pass >= 50)) {
            /* WM_SETFOCUS sets the focus flag (window procedure 0x405455); the journal goes on next pass. */
            ++vc_focus_injected; vc_last_focus_pass = vc_pass;
            vc_pending[0] = (VjMsg){0x0007u /* WM_SETFOCUS */, 0, 0, 0};
            vc_pending_count = 1;
            vc_event_record("focus_injected", NULL);
        } else {
            vc_pending_count = vj_begin_pass(&vc_runner, vc_pass, vc_clock(), focused && !movie, vc_pending);
        }
    }
    if (vc_pending_next < vc_pending_count) {
        vc_fill(msg, &vc_pending[vc_pending_next++]);
        ++vc_injected;
        return TRUE;
    }
    for (;;) {
        BOOL result = vc_real_peek(msg, window, first, last, flags);
        if (result && vc_block_input && vc_is_input(msg->message)) { ++vc_dropped; continue; }
        if (!result || msg->message == WM_QUIT) { vc_in_pass = 0; ++vc_pass; }
        return result;
    }
}

static SHORT vc_key_state(int vk) {
    vc_ready();
    if (!vc_active || !vc_journal) return vc_real_key(vk);
    DWORD bit = vk == VK_SHIFT ? 1u : vk == VK_CONTROL ? 2u : vk == VK_MENU ? 4u : 0u;
    if (!bit) return vc_real_key(vk);
    return (vc_mods & bit) ? (SHORT)0x8000 : 0;
}

static BOOL vc_get_cursor_at(DWORD ret_rva, LPPOINT point) {
    vc_ready();
    if (!vc_active || !vc_journal || ret_rva != VC_CURSOR_GET_RET || !point) return vc_real_get_cursor(point);
    ++vc_pointer_reads;
    if (!vc_pointer_known) { ++vc_pointer_real_reads; return vc_real_get_cursor(point); }
    POINT p = vc_pointer;
    if (!vc_client_to_screen((HWND)(uintptr_t)vc_read32(VC_WINDOW_RVA), &p)) return vc_real_get_cursor(point);
    *point = p;
    return TRUE;
}

static BOOL vc_set_cursor_at(DWORD ret_rva, int x, int y) {
    vc_ready();
    BOOL site = FALSE;
    for (unsigned i = 0; i < sizeof(vc_cursor_set_rets) / sizeof(vc_cursor_set_rets[0]); ++i)
        if (ret_rva == vc_cursor_set_rets[i]) site = TRUE;
    if (!vc_active || !vc_journal || !site) return vc_real_set_cursor(x, y);
    POINT p = {x, y};
    if (!vc_screen_to_client((HWND)(uintptr_t)vc_read32(VC_WINDOW_RVA), &p)) return vc_real_set_cursor(x, y);
    vc_pointer = p;
    vc_pointer_known = 1;
    ++vc_pointer_warps;
    char extra[96];
    snprintf(extra, sizeof(extra), ",\"site\":\"0x%06lx\",\"x\":%ld,\"y\":%ld", (unsigned long)(ret_rva - 5 + VC_BASE),
        (long)p.x, (long)p.y);
    vc_event_record("warp", extra);
    return TRUE;
}

/* Import-slot entry points: the immediate return address is the call site in VSHELL. */
#define VC_RET() ((DWORD)((uintptr_t)__builtin_return_address(0) - (uintptr_t)vc_image))
static BOOL WINAPI vc_hook_bitblt(HDC dc, int x, int y, int cx, int cy, HDC src, int x1, int y1, DWORD rop) {
    return vc_bitblt_at(VC_RET(), dc, x, y, cx, cy, src, x1, y1, rop);
}
static BOOL WINAPI vc_hook_stretchblt(HDC dc, int x, int y, int cx, int cy, HDC src, int x1, int y1, int cx1, int cy1, DWORD rop) {
    return vc_stretchblt_at(VC_RET(), dc, x, y, cx, cy, src, x1, y1, cx1, cy1, rop);
}
static UINT WINAPI vc_hook_set_dib(HDC dc, UINT start, UINT count, const RGBQUAD *quads) {
    return vc_set_dib_at(VC_RET(), dc, start, count, quads);
}
static BOOL WINAPI vc_hook_peek(LPMSG msg, HWND window, UINT first, UINT last, UINT flags) {
    return vc_peek_at(VC_RET(), msg, window, first, last, flags);
}
static SHORT WINAPI vc_hook_key_state(int vk) { return vc_key_state(vk); }
static BOOL WINAPI vc_hook_get_cursor(LPPOINT point) { return vc_get_cursor_at(VC_RET(), point); }
static BOOL WINAPI vc_hook_set_cursor(int x, int y) { return vc_set_cursor_at(VC_RET(), x, y); }

/* ---- installation (DllMain-safe: memory checks and slot writes only) -------------------------- */
struct VcSlot { DWORD iat_rva, thunk_rva; void *hook; void **real; };
static struct VcSlot vc_slots[] = {
    {0x001de3f0u, 0x0006bb38u, (void *)vc_hook_bitblt, (void **)&vc_real_bitblt},
    {0x001de3f4u, 0x0006bb3eu, (void *)vc_hook_stretchblt, (void **)&vc_real_stretchblt},
    {0x001de3f8u, 0x0006bb44u, (void *)vc_hook_set_dib, (void **)&vc_real_set_dib},
    {0x001de414u, 0x0006bb68u, (void *)vc_hook_peek, (void **)&vc_real_peek},
    {0x001de440u, 0x0006bbaau, (void *)vc_hook_key_state, (void **)&vc_real_key},
    {0x001de448u, 0x0006bbb6u, (void *)vc_hook_get_cursor, (void **)&vc_real_get_cursor},
    {0x001de49cu, 0x0006bc34u, (void *)vc_hook_set_cursor, (void **)&vc_real_set_cursor},
};

static BOOL vc_image_valid(BYTE *image) {
    if ((uintptr_t)image != VC_BASE || sizeof(void *) != 4 || !vc_readable(image, 4096)) return FALSE;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)image;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < (LONG)sizeof(*dos) ||
        dos->e_lfanew > 4096 - (LONG)sizeof(IMAGE_NT_HEADERS32)) return FALSE;
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(image + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt->OptionalHeader.ImageBase != VC_BASE || nt->OptionalHeader.SizeOfImage != VC_IMAGE_SIZE) return FALSE;
    for (unsigned i = 0; i < sizeof(vc_slots) / sizeof(vc_slots[0]); ++i) {
        BYTE thunk[6] = {0xff, 0x25};
        DWORD iat = (DWORD)(VC_BASE + vc_slots[i].iat_rva);
        memcpy(thunk + 2, &iat, 4);
        if (!vc_readable(image + vc_slots[i].thunk_rva, 6) || memcmp(image + vc_slots[i].thunk_rva, thunk, 6) ||
            !vc_readable(image + vc_slots[i].iat_rva, 4)) return FALSE;
    }
    for (unsigned i = 0; i < sizeof(vc_signatures) / sizeof(vc_signatures[0]); ++i)
        if (!vc_readable(image + vc_signatures[i].rva, vc_signatures[i].size) ||
            memcmp(image + vc_signatures[i].rva, vc_signatures[i].bytes, vc_signatures[i].size)) return FALSE;
    return vc_readable(image + VC_BITS_RVA, 4) && vc_readable(image + VC_DD_RVA, 4) &&
        vc_readable(image + VC_FOCUS_RVA, 4) && vc_readable(image + VC_MOVIE_RVA, 4);
}

static BOOL install_vshell_capture_image(BYTE *image) {
    LONG previous = InterlockedCompareExchange(&vc_install_state, -1, 0);
    if (previous) return previous == 1;
    if (!vc_image_valid(image)) return FALSE;
    vc_image = image;
    for (unsigned i = 0; i < sizeof(vc_slots) / sizeof(vc_slots[0]); ++i) {
        DWORD *slot = (DWORD *)(image + vc_slots[i].iat_rva);
        DWORD protection, ignored;
        if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection)) return FALSE;
        *vc_slots[i].real = (void *)(uintptr_t)*slot;
        InterlockedExchange((LONG *)slot, (LONG)(uintptr_t)vc_slots[i].hook);
        VirtualProtect(slot, sizeof(*slot), protection, &ignored);
    }
    InterlockedExchange(&vc_install_state, 1);
    return TRUE;
}

/* DllMain: only with ES2_VSHELL_CAPTURE_CONFIG and only in VSHELL.EXE; kernel32 calls only. */
static void install_vshell_capture(void) {
    DWORD n = GetEnvironmentVariableW(L"ES2_VSHELL_CAPTURE_CONFIG", vc_config, MAX_PATH);
    if (!n || n >= MAX_PATH) { vc_config[0] = 0; return; }
    WCHAR exe[MAX_PATH];
    DWORD length = GetModuleFileNameW(NULL, exe, MAX_PATH);
    WCHAR *name = length && length < MAX_PATH ? wcsrchr(exe, L'\\') : NULL;
    if (!name || _wcsicmp(name + 1, L"VSHELL.EXE")) return;
    install_vshell_capture_image((BYTE *)GetModuleHandleW(NULL));
}

static void vshell_capture_shutdown(void) {
    if (vc_log == INVALID_HANDLE_VALUE) return;
    char extra[384];
    snprintf(extra, sizeof(extra), ",\"presents\":%lu,\"uploads\":%lu,\"records\":%lu,\"injected\":%lu,\"dropped\":%lu,"
        "\"focus_injected\":%lu,\"journal_done\":%s,\"dumps\":%lu,\"dump_failures\":%lu,"
        "\"pointer_reads\":%lu,\"pointer_real_reads\":%lu,\"pointer_warps\":%lu",
        (unsigned long)vc_presents, (unsigned long)vc_uploads, (unsigned long)vc_records, (unsigned long)vc_injected,
        (unsigned long)vc_dropped, (unsigned long)vc_focus_injected, vc_journal && vc_runner.done ? "true" : "false",
        (unsigned long)vc_dumps, (unsigned long)vc_dump_failures, (unsigned long)vc_pointer_reads,
        (unsigned long)vc_pointer_real_reads, (unsigned long)vc_pointer_warps);
    vc_event_record("end", extra);
    CloseHandle(vc_log);
    vc_log = INVALID_HANDLE_VALUE;
}
#endif
