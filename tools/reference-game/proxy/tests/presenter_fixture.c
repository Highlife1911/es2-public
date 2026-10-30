// Own-code fixture for proxy/presenter.h and presenter_frame.h. A
// synthetic image carries the thunks and IAT slots of the input journal, the
// window shim and the two GDI imports, the code bytes the capture checks, and
// small generated routines that reproduce DBSIM's call shapes: the main-loop
// call of 0x464a14, 0x464a14 itself, a 0x465628 frame (client RECT at
// [ebp-0x100]) whose StretchBlt call sits at 0x4659ad, and the clear's call at
// 0x4655e3. The frames are synthetic; no original game code or data is used.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
static WCHAR config_path[MAX_PATH];
static HMODULE self_module;
static char last_event[128];
static unsigned events;
static void log_event(const char *event) {
    printf("event %s\n", event);
    snprintf(last_event, sizeof(last_event), "%s", event);
    ++events;
}
#include "../input_journal.h"
#include "../window_shim.h"
#include "../presenter.h"
#include "../ddraw_windowed.h"

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)

static BYTE *image;
static WCHAR tmp[MAX_PATH];

// ---- fakes standing for USER32/GDI32 --------------------------------------
static BOOL WINAPI fake_peek(LPMSG msg, HWND w, UINT a, UINT b, UINT flags) { (void)msg; (void)w; (void)a; (void)b; (void)flags; return FALSE; }
static SHORT WINAPI fake_key(int vk) { (void)vk; return 0; }
static MMRESULT WINAPI fake_joypos(UINT id, LPJOYINFOEX info) { (void)id; (void)info; return JOYERR_UNPLUGGED; }
static MMRESULT WINAPI fake_joycaps(UINT_PTR id, LPJOYCAPSA caps, UINT size) { (void)id; (void)caps; (void)size; return JOYERR_PARMS; }
static BOOL WINAPI fake_cursor(LPPOINT p) { p->x = 1; p->y = 2; return TRUE; }
static unsigned created;
static HWND WINAPI fake_create(DWORD a, LPCSTR b, LPCSTR c, DWORD style, int e, int f, int g, int h, HWND parent, HMENU i, HINSTANCE j, LPVOID k) {
    (void)a; (void)b; (void)c; (void)style; (void)e; (void)f; (void)g; (void)h; (void)i; (void)j; (void)k;
    ++created;
    return parent ? (HWND)(uintptr_t)0x9999 : (HWND)(uintptr_t)0x1234;
}
static BOOL WINAPI fake_window(void) { return 77; }   // the seven window-shim imports (never called here)

typedef struct { HDC hdc, source; int a[8]; DWORD rop; } blit_call;
static blit_call blits[32];
static unsigned blit_count;
static BOOL WINAPI fake_stretch(HDC hdc, int dx, int dy, int dw, int dh, HDC source, int sx, int sy, int sw, int sh, DWORD rop) {
    if (blit_count < 32) blits[blit_count] = (blit_call){hdc, source, {dx, dy, dw, dh, sx, sy, sw, sh}, rop};
    ++blit_count;
    SetLastError(4242);   // must reach DBSIM unchanged when the original runs
    return 77;
}
static unsigned table_calls;
static UINT WINAPI fake_table(HDC hdc, UINT start, UINT count, const RGBQUAD *quads) { (void)hdc; (void)start; (void)quads; ++table_calls; return count; }

static unsigned draw_calls;
static int draw_result = -1;   // -1: number of lines
static int draw_args[8];
static HDC draw_hdc;
static DWORD *draw_pixels;
static BITMAPINFOHEADER draw_header;
static DWORD draw_sleep_ms;            // The fake draw waits (and notes the counter) so a stamp before it differs from one after
static uint64_t draw_qpc_log[16];      // The counter on entry to each fake draw
static int WINAPI fake_draw(HDC hdc, int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh, const void *bits,
                            const BITMAPINFO *info, UINT usage, DWORD rop) {
    if (draw_sleep_ms) {
        LARGE_INTEGER t; QueryPerformanceCounter(&t);
        if (draw_calls < 16) draw_qpc_log[draw_calls] = (uint64_t)t.QuadPart;
        Sleep(draw_sleep_ms);
    }
    ++draw_calls;
    draw_hdc = hdc;
    int a[8] = {dx, dy, dw, dh, sx, sy, sw, sh};
    memcpy(draw_args, a, sizeof(a));
    draw_header = info->bmiHeader;
    if (usage != DIB_RGB_COLORS || rop != SRCCOPY) return 0;
    size_t n = (size_t)info->bmiHeader.biWidth * (size_t)-info->bmiHeader.biHeight;
    free(draw_pixels); draw_pixels = malloc(n * 4u);
    memcpy(draw_pixels, bits, n * 4u);
    return draw_result < 0 ? dh : draw_result;
}

// ---- synthetic image --------------------------------------------------------
static void put32(DWORD rva, DWORD v) { memcpy(image + rva, &v, 4); }
static DWORD get32(DWORD rva) { DWORD v; memcpy(&v, image + rva, 4); return v; }
static void put_bytes(DWORD rva, const BYTE *bytes, size_t n) { memcpy(image + rva, bytes, n); }
static void put_thunk(DWORD iat_rva, DWORD thunk_rva, void *target) {
    put32(iat_rva, (DWORD)(uintptr_t)target);
    image[thunk_rva] = 0xff; image[thunk_rva + 1] = 0x25;
    put32(thunk_rva + 2, (DWORD)(uintptr_t)(image + iat_rva));
}
static void put_rel_call(DWORD rva, BYTE opcode, DWORD target_rva) {   // e8/e9 rel32
    image[rva] = opcode; put32(rva + 1, target_rva - (rva + 5));
}
static void *slot(DWORD rva) { return (void *)(uintptr_t)get32(rva); }

#define SITE_MAIN_CALL 0x0005fbdfu    // call 0x464a14 in the presentation frame 0x45fba4 -> site 0x45fbe4
#define SITE_SLIDE_CALL 0x0002b135u   // the first slide call inside 0x42a9c0 -> site 0x42b13a
#define SITE_MODAL_CALL 0x00054906u   // UI screen -> site 0x45490b
#define CLEAR_STUB 0x00001000u
#define PUSH_AREA (0x000659adu - 55u)  // eleven `push imm32` end at the call 0x4659ad
#define DIB_DC 0x5151u

static void set_present(LONG cw, LONG ch, HDC hdc, int dx, int dy, int dw, int dh, HDC source, int sx, int sy, int sw, int sh, DWORD rop);
static void make_image(void) {
    image = VirtualAlloc(NULL, IJ_IMAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    CHECK(image != NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)image;
    dos->e_magic = IMAGE_DOS_SIGNATURE; dos->e_lfanew = 0x80;
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(image + 0x80);
    nt->Signature = IMAGE_NT_SIGNATURE; nt->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    nt->OptionalHeader.ImageBase = (DWORD)(uintptr_t)image; nt->OptionalHeader.SizeOfImage = IJ_IMAGE_SIZE;
    void *journal[] = {(void *)fake_peek, (void *)fake_key, (void *)fake_joypos, (void *)fake_joycaps, (void *)fake_cursor, (void *)fake_create};
    for (unsigned i = 0; i < 6; ++i) put_thunk(ij_slots[i].iat_rva, ij_slots[i].thunk_rva, journal[i]);
    for (unsigned i = 0; i < WS_API_COUNT; ++i) put_thunk(ws_slots[i].iat_rva, ws_slots[i].thunk_rva, (void *)fake_window);
    put_thunk(PR_STRETCH_IAT_RVA, PR_STRETCH_THUNK_RVA, (void *)fake_stretch);
    put_thunk(PR_TABLE_IAT_RVA, PR_TABLE_THUNK_RVA, (void *)fake_table);
    // Checked code bytes (presenter.h pr_signatures), executed where marked.
    BYTE push = 0x68;
    put_bytes(0x00064a14u, &push, 1); put32(0x00064a15u, (DWORD)(uintptr_t)(image + PR_RECT_RVA));      // executed
    put_bytes(0x00064a19u, &push, 1); put32(0x00064a1au, (DWORD)(uintptr_t)(image + PR_PAGE_RVA));      // executed
    static const BYTE call_tail[] = {0xe8, 0x05, 0x0c, 0x00, 0x00, 0x83, 0xc4, 0x08, 0xc3};             // executed
    put_bytes(0x00064a1eu, call_tail, sizeof(call_tail));
    static const BYTE lea[] = {0x8d, 0x8d, 0x00, 0xff, 0xff, 0xff};                                     // data only
    put_bytes(0x000658a7u, lea, sizeof(lea));
    put_rel_call(0x000659adu, 0xe8, PR_STRETCH_THUNK_RVA);                                              // executed
    static const BYTE epilogue[] = {0x5f, 0x5e, 0x5b, 0x8b, 0xe5, 0x5d, 0xc3};   // pop edi/esi/ebx; mov esp, ebp; pop ebp; ret
    put_bytes(PR_PRESENT_RETURN_RVA, epilogue, sizeof(epilogue));
    put_rel_call(0x000655e3u, 0xe8, PR_STRETCH_THUNK_RVA);                                              // executed
    image[PR_CLEAR_RETURN_RVA] = 0xc3;
    put_rel_call(0x00065de5u, 0xe8, PR_TABLE_THUNK_RVA);                                                // data only
    put_rel_call(0x00065f00u, 0xe8, PR_TABLE_THUNK_RVA);
    // Callers of 0x464a14: `call 0x464a14; ret`.
    DWORD sites[] = {SITE_MAIN_CALL, SITE_SLIDE_CALL, SITE_MODAL_CALL};
    for (unsigned i = 0; i < 3; ++i) { put_rel_call(sites[i], 0xe8, 0x00064a14u); image[sites[i] + 5] = 0xc3; }
    set_present(1, 1, NULL, 0, 0, 1, 1, NULL, 0, 0, 1, 1, SRCCOPY);   // the checked prologue at 0x465628
    // DirectDraw branch: Unlock/Flip of the present and of the clear (checked; executed by the ddraw mode).
    static const BYTE unlock_ecx[] = {0xff, 0x91, 0x80, 0x00, 0x00, 0x00}, flip_edx[] = {0xff, 0x52, 0x2c};
    static const BYTE unlock_edx[] = {0xff, 0x92, 0x80, 0x00, 0x00, 0x00}, flip_eax[] = {0xff, 0x50, 0x2c};
    put_bytes(0x00065870u, unlock_ecx, 6); put_bytes(DDW_UNLOCK_PRESENT_RVA, epilogue, sizeof(epilogue));
    put_bytes(0x00065892u, flip_edx, 3); put_bytes(DDW_FLIP_PRESENT_RVA, epilogue, sizeof(epilogue));
    put_bytes(0x00065524u, unlock_edx, 6); image[DDW_UNLOCK_CLEAR_RVA] = 0xc3;
    put_bytes(0x00065547u, flip_eax, 3); image[DDW_FLIP_CLEAR_RVA] = 0xc3;
}

// 0x465628's frame: the prologue of the original (checked), saved registers,
// the client RECT at [ebp-0x100], then the eleven StretchBlt arguments.
static void set_present(LONG cw, LONG ch, HDC hdc, int dx, int dy, int dw, int dh, HDC source, int sx, int sy, int sw, int sh, DWORD rop) {
    BYTE code[64]; unsigned n = 0;
    static const BYTE prologue[] = {0x55, 0x8b, 0xec, 0x81, 0xc4, 0x00, 0xff, 0xff, 0xff, 0x53, 0x56, 0x57};
    memcpy(code, prologue, sizeof(prologue)); n = sizeof(prologue);
    LONG rect[4] = {0, 0, cw, ch};
    for (unsigned i = 0; i < 4; ++i) {   // mov dword [ebp - 0x100 + 4i], imm32
        code[n++] = 0xc7; code[n++] = 0x85; DWORD disp = (DWORD)(-0x100 + 4 * (int)i); memcpy(code + n, &disp, 4); n += 4;
        memcpy(code + n, &rect[i], 4); n += 4;
    }
    put_bytes(0x00065628u, code, n);
    put_rel_call(0x00065628u + n, 0xe9, PUSH_AREA);
    DWORD args[11] = {(DWORD)(uintptr_t)hdc, (DWORD)dx, (DWORD)dy, (DWORD)dw, (DWORD)dh, (DWORD)(uintptr_t)source,
                      (DWORD)sx, (DWORD)sy, (DWORD)sw, (DWORD)sh, rop};
    for (unsigned i = 0; i < 11; ++i) { image[PUSH_AREA + 5 * i] = 0x68; put32(PUSH_AREA + 5 * i + 1, args[10 - i]); }
}
// Every present must leave the synthetic game globals (0x4d0000..0x4d6000) unchanged.
static void present_from(DWORD call_rva) {
    static BYTE before[0x6000];
    memcpy(before, image + 0xd0000u, sizeof(before));
    ((void (*)(void))(void *)(image + call_rva))();
    CHECK(!memcmp(before, image + 0xd0000u, sizeof(before)));
}
static DWORD WINAPI present_thread(LPVOID unused) { (void)unused; ((void (*)(void))(void *)(image + SITE_MAIN_CALL))(); return 0; }
// The clear: eleven pushes, then `jmp 0x4655e3` (whose call returns to 0x4655e8: ret).
static void clear_blit(HDC hdc, HDC source) {
    DWORD args[11] = {(DWORD)(uintptr_t)hdc, 1, 2, 30, 40, (DWORD)(uintptr_t)source, 0, 0, 10, 10, SRCCOPY};
    for (unsigned i = 0; i < 11; ++i) { image[CLEAR_STUB + 5 * i] = 0x68; put32(CLEAR_STUB + 5 * i + 1, args[10 - i]); }
    put_rel_call(CLEAR_STUB + 55, 0xe9, 0x000655e3u);
    ((void (*)(void))(void *)(image + CLEAR_STUB))();
}

// ---- synthetic game state ------------------------------------------------------
enum { SCREEN_W = 16, SCREEN_H = 12, DIB_W = 32, DIB_H = 24 };
static BYTE dib[DIB_W * DIB_H];
static RGBQUAD quads[256];
static void set_game(DWORD frame, LONG page_x, LONG page_y) {
    put32(PR_FRAME_RVA, frame); put32(PR_MODE_RVA, 0);
    put32(PR_SCREEN_W_RVA, SCREEN_W); put32(PR_SCREEN_H_RVA, SCREEN_H);
    put32(PR_DIB_W_RVA, DIB_W); put32(PR_DIB_H_RVA, DIB_H);
    LONG r[4] = {0, 0, SCREEN_W - 1, SCREEN_H - 1};
    memcpy(image + PR_RECT_RVA, r, sizeof(r));
    put32(PR_PAGE_RVA, (DWORD)page_x); put32(PR_PAGE_RVA + 4, (DWORD)page_y);
    put32(PR_DIB_BITS_RVA, (DWORD)(uintptr_t)dib); put32(PR_DIB_DC_RVA, DIB_DC);
}
static void fill_dib(unsigned seed) {
    for (unsigned i = 0; i < sizeof(dib); ++i) dib[i] = (BYTE)(i * 7u + seed * 13u + (i >> 5));
}
static void fill_quads(unsigned seed) {
    for (unsigned i = 0; i < 256; ++i)
        quads[i] = (RGBQUAD){(BYTE)(i ^ seed), (BYTE)(i * 3u + seed), (BYTE)(255u - i), (BYTE)(0xa5 + i)};   // reserved = residue
}
static UINT upload(HDC dc) { return ((PrSetTable)slot(PR_TABLE_IAT_RVA))(dc, 0, 256, quads); }

// ---- independent SHA-256 (CryptoAPI) ---------------------------------------------
static void crypto_sha256(const BYTE *data, DWORD n, char out[65]) {
    HCRYPTPROV provider; HCRYPTHASH hash; BYTE digest[32]; DWORD size = 32;
    CHECK(CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT));
    CHECK(CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash));
    CHECK(CryptHashData(hash, data, n, 0));
    CHECK(CryptGetHashParam(hash, HP_HASHVAL, digest, &size, 0) && size == 32);
    CryptDestroyHash(hash); CryptReleaseContext(provider, 0);
    for (unsigned i = 0; i < 32; ++i) snprintf(out + 2 * i, 3, "%02x", digest[i]);
}
static void frame_sha(LONG sx, LONG sy, int w, int h, char out[65]) {
    BYTE *rows = malloc((size_t)w * (size_t)h);
    for (int y = 0; y < h; ++y) memcpy(rows + (size_t)y * (size_t)w, dib + (size_t)(sy + y) * DIB_W + (size_t)sx, (size_t)w);
    crypto_sha256(rows, (DWORD)(w * h), out);
    free(rows);
}
static void palette_sha(char out[65]) {
    BYTE rgb[768];
    for (unsigned i = 0; i < 256; ++i) { rgb[3 * i] = quads[i].rgbRed; rgb[3 * i + 1] = quads[i].rgbGreen; rgb[3 * i + 2] = quads[i].rgbBlue; }
    crypto_sha256(rgb, sizeof(rgb), out);
}

// ---- configuration ----------------------------------------------------------------
static void path_in_tmp(WCHAR *out, const WCHAR *name) { swprintf(out, MAX_PATH, L"%ls\\%ls", tmp, name); }
static void write_ini(const WCHAR *name, const WCHAR *const *pairs) {   // section, key, value triples, NULL-terminated
    WCHAR path[MAX_PATH]; path_in_tmp(path, name);
    DeleteFileW(path);
    for (unsigned i = 0; pairs[i]; i += 3) CHECK(WritePrivateProfileStringW(pairs[i], pairs[i + 1], pairs[i + 2], path));
}
static void set_env_ini(const WCHAR *variable, const WCHAR *name) {
    WCHAR path[MAX_PATH]; path_in_tmp(path, name);
    SetEnvironmentVariableW(variable, name ? path : NULL);
}
static void install_all(BOOL with_shim) {
    CHECK(install_input_journal_image(image, (uintptr_t)image));
    if (with_shim) CHECK(install_window_shim_image(image, (uintptr_t)image));
    void *before = slot(WS_CREATE_IAT_RVA);
    CHECK(install_presenter_image(image, (uintptr_t)image));
    CHECK(pr_next_create == (IjCreate)before && slot(WS_CREATE_IAT_RVA) == (void *)pr_create_window);
    CHECK(install_presenter_image(image, (uintptr_t)image));   // idempotent
    CHECK(slot(PR_STRETCH_IAT_RVA) == (void *)fake_stretch && slot(PR_TABLE_IAT_RVA) == (void *)fake_table);
}
static void create_game_window(void) {
    CHECK(((IjCreate)slot(WS_CREATE_IAT_RVA))(0, "ES2", "t", 0x100e0000u, 0, 0, 646, 505, NULL, NULL, NULL, NULL) == (HWND)(uintptr_t)0x1234);
}
static BOOL patched(void) {
    return slot(PR_STRETCH_IAT_RVA) == (void *)pr_stretch_blt && slot(PR_TABLE_IAT_RVA) == (void *)pr_set_table;
}

static char *read_text(const WCHAR *name, size_t *size) {
    WCHAR path[MAX_PATH]; path_in_tmp(path, name);
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
    DWORD n = GetFileSize(f, NULL), got = 0;
    char *text = malloc(n + 1);
    CHECK(ReadFile(f, text, n, &got, NULL) && got == n);
    CloseHandle(f);
    text[n] = 0;
    if (size) *size = n;
    return text;
}
// The line'th line (0-based), without CRLF.
static BOOL nth_line(const char *text, unsigned line, char *out, size_t capacity) {
    for (unsigned i = 0; i < line; ++i) { text = strstr(text, "\r\n"); if (!text) return FALSE; text += 2; }
    const char *end = strstr(text, "\r\n");
    if (!end || (size_t)(end - text) >= capacity) return FALSE;
    memcpy(out, text, (size_t)(end - text)); out[end - text] = 0;
    return TRUE;
}
static unsigned line_count(const char *text) { unsigned n = 0; for (; (text = strstr(text, "\r\n")); text += 2) ++n; return n; }

static void expect_record(const char *text, unsigned line, DWORD frame, const char *site, int ordinal, LONG sx, LONG sy,
                          int cw, int ch, const char *source, const char *index, const char *palette, DWORD uploads, const char *presented) {
    char expected[1024], got[1024];
    snprintf(expected, sizeof(expected),
        "{\"frame\":%lu,\"site\":\"%s\",\"ordinal\":%d,\"mode\":0,\"S\":[%ld,%ld],\"R\":[0,0,%d,%d],\"client\":[0,0,%d,%d],"
        "\"src\":[%ld,%ld,%d,%d],\"dst\":[0,0,%d,%d],\"source\":\"%s\",\"index_sha256\":%s,\"palette_sha256\":%s,"
        "\"uploads\":%lu,\"presented\":\"%s\"}",
        (unsigned long)frame, site, ordinal, (long)sx, (long)sy, SCREEN_W - 1, SCREEN_H - 1, cw, ch,
        (long)sx, (long)sy, SCREEN_W, SCREEN_H, cw, ch, source, index, palette, (unsigned long)uploads, presented);
    CHECK(nth_line(text, line, got, sizeof(got)));
    if (strcmp(expected, got)) { fprintf(stderr, "line %u\n got %s\nwant %s\n", line, got, expected); exit(1); }
}
static void quoted(char *out, const char *hex) { snprintf(out, 70, "\"%s\"", hex); }

// ---- expected presenter output (by definition, not through pr_render) --------------
static DWORD rgb(BYTE index) { return (DWORD)quads[index].rgbRed << 16 | (DWORD)quads[index].rgbGreen << 8 | quads[index].rgbBlue; }
static void expect_image(const DWORD *pixels, int cw, int ch, LONG sx, LONG sy, int ox, int oy, int width, int height) {
    for (int y = 0; y < ch; ++y) for (int x = 0; x < cw; ++x) {
        DWORD want = 0;
        if (x >= ox && x < ox + width && y >= oy && y < oy + height) {
            int u = (x - ox) * SCREEN_W / width, v = (y - oy) * SCREEN_H / height;
            want = rgb(dib[(size_t)(sy + v) * DIB_W + (size_t)(sx + u)]);
        }
        if (pixels[(size_t)y * (size_t)cw + (size_t)x] != want) { fprintf(stderr, "pixel %d,%d: %08lx want %08lx\n", x, y,
            (unsigned long)pixels[(size_t)y * (size_t)cw + (size_t)x], (unsigned long)want); exit(1); }
    }
}

// ---- modes ----------------------------------------------------------------------------
static void mode_capture(BOOL with_presenter) {
    WCHAR report[MAX_PATH]; path_in_tmp(report, L"capture.jsonl");
    const WCHAR *pairs[] = {L"presentation", L"capture", L"1", L"presentation", L"capture_report", report,
                            L"presentation", L"capture_dump_frames", L"2, 7", L"presentation", L"presenter",
                            with_presenter ? L"gdi" : L"original", NULL};
    write_ini(L"presentation.ini", pairs);
    set_env_ini(L"ES2_PRESENTATION_CONFIG", L"presentation.ini");
    install_all(TRUE);
    fill_dib(1); fill_quads(3); set_game(1, 4, 2);
    create_game_window();
    CHECK(patched() && pr_state == 2 && pr_capture && pr_present_gdi == with_presenter && pr_real_stretch == fake_stretch);
    CHECK(!strcmp(last_event, with_presenter ? "presentation_capture_gdi" : "presentation_capture"));
    pr_stretch_dibits = fake_draw;
    HDC window_dc = (HDC)(uintptr_t)0x7777, source = (HDC)(uintptr_t)DIB_DC;

    // Frame 1: a present before any upload (no table: palette null; the presenter forwards).
    set_present(SCREEN_W, SCREEN_H, window_dc, 0, 0, SCREEN_W, SCREEN_H, source, 4, 2, SCREEN_W, SCREEN_H, SRCCOPY);
    SetLastError(99);
    present_from(SITE_MAIN_CALL);
    CHECK(GetLastError() == 4242 && blit_count == 1);   // the original ran last
    CHECK(blits[0].hdc == window_dc && blits[0].source == source && blits[0].a[2] == SCREEN_W && blits[0].a[4] == 4 &&
          blits[0].a[5] == 2 && blits[0].a[6] == SCREEN_W && blits[0].a[7] == SCREEN_H && blits[0].rop == SRCCOPY);
    char f1[65]; frame_sha(4, 2, SCREEN_W, SCREEN_H, f1);
    // Uploads: one to another DC (forwarded, not recorded), one to the DIB DC.
    CHECK(upload((HDC)(uintptr_t)0x4444) == 256 && pr_uploads == 0 && !pr_have_table);
    CHECK(upload(source) == 256 && pr_uploads == 1 && pr_have_table && table_calls == 2);
    char p1[65]; palette_sha(p1);
    CHECK(!strcmp(pr_palette_hex, p1));
    // Frame 2: two main-loop presents, a slide present, a modal present with S moved,
    // the clear, and a present-site blit from another source DC.
    put32(PR_FRAME_RVA, 2);
    present_from(SITE_MAIN_CALL);
    fill_dib(2);
    char f2[65]; frame_sha(4, 2, SCREEN_W, SCREEN_H, f2);
    present_from(SITE_MAIN_CALL);
    present_from(SITE_SLIDE_CALL);
    put32(PR_PAGE_RVA, 6); put32(PR_PAGE_RVA + 4, 1);
    set_present(SCREEN_W, SCREEN_H, window_dc, 0, 0, SCREEN_W, SCREEN_H, source, 6, 1, SCREEN_W, SCREEN_H, SRCCOPY);
    present_from(SITE_MODAL_CALL);
    char f3[65]; frame_sha(6, 1, SCREEN_W, SCREEN_H, f3);
    clear_blit(window_dc, source);
    set_present(SCREEN_W, SCREEN_H, window_dc, 0, 0, SCREEN_W, SCREEN_H, (HDC)(uintptr_t)0x6666, 6, 1, SCREEN_W, SCREEN_H, SRCCOPY);
    present_from(SITE_MAIN_CALL);
    // Out-of-bounds source rectangle: recorded without a hash, forwarded.
    set_present(SCREEN_W, SCREEN_H, window_dc, 0, 0, SCREEN_W, SCREEN_H, source, DIB_W - SCREEN_W + 1, 0, SCREEN_W, SCREEN_H, SRCCOPY);
    present_from(SITE_MAIN_CALL);
    CHECK(pr_clear_blits == 1 && pr_other_blits == 0 && pr_presents == 7);
    // A present from another thread: forwarded, not recorded.
    HANDLE thread = CreateThread(NULL, 0, present_thread, NULL, 0, NULL);
    CHECK(thread && WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0);
    CloseHandle(thread);
    CHECK(pr_foreign_thread == 1 && pr_presents == 7);
    // Frame 3 writes frame 2's records.
    put32(PR_FRAME_RVA, 3); put32(PR_PAGE_RVA, 4); put32(PR_PAGE_RVA + 4, 2);
    set_present(SCREEN_W, SCREEN_H, window_dc, 0, 0, SCREEN_W, SCREEN_H, source, 4, 2, SCREEN_W, SCREEN_H, SRCCOPY);
    present_from(SITE_MAIN_CALL);
    const char *ok = with_presenter ? "gdi" : "original", *fw = with_presenter ? "forwarded" : "original";
    char q1[70], q2[70], q3[70], qp[70];
    quoted(q1, f1); quoted(q2, f2); quoted(q3, f3); quoted(qp, p1);
    size_t size;
    char *text = read_text(L"capture.jsonl", &size);
    CHECK(text);
    char line[1024];
    CHECK(nth_line(text, 0, line, sizeof(line)));
    char header[256];
    snprintf(header, sizeof(header), "{\"event\":\"start\",\"schema\":1,\"key\":\"frame_counter_0x4d257b\",\"presenter\":\"%s\",\"dump_frames\":2,\"pid\":%lu}",
             with_presenter ? "gdi" : "original", (unsigned long)GetCurrentProcessId());
    CHECK(!strcmp(line, header));
    CHECK(line_count(text) == 8);   // header, frame 1 (one record), frame 2 (six); frame 3 still buffered
    expect_record(text, 1, 1, "0x45fbe4", 0, 4, 2, SCREEN_W, SCREEN_H, "dib", q1, "null", 0, fw);
    expect_record(text, 2, 2, "0x45fbe4", 0, 4, 2, SCREEN_W, SCREEN_H, "dib", q1, qp, 1, ok);
    expect_record(text, 3, 2, "0x45fbe4", 1, 4, 2, SCREEN_W, SCREEN_H, "dib", q2, qp, 1, ok);
    expect_record(text, 4, 2, "0x42b13a", 0, 4, 2, SCREEN_W, SCREEN_H, "dib", q2, qp, 1, ok);
    expect_record(text, 5, 2, "0x45490b", 0, 6, 1, SCREEN_W, SCREEN_H, "dib", q3, qp, 1, ok);
    expect_record(text, 6, 2, "0x45fbe4", 2, 6, 1, SCREEN_W, SCREEN_H, "other", "null", qp, 1, fw);
    char outside[1024];
    CHECK(nth_line(text, 7, outside, sizeof(outside)));
    CHECK(strstr(outside, "\"ordinal\":3,") && strstr(outside, "\"src\":[17,0,16,12]") && strstr(outside, "\"source\":\"dib_outside\"") &&
          strstr(outside, "\"index_sha256\":null"));
    free(text);
    // Shutdown: frame 3 and a stats line.
    presenter_shutdown();
    text = read_text(L"capture.jsonl", &size);
    CHECK(line_count(text) == 10);
    expect_record(text, 8, 3, "0x45fbe4", 0, 4, 2, SCREEN_W, SCREEN_H, "dib", q2, qp, 1, ok);
    CHECK(nth_line(text, 9, line, sizeof(line)) && !strncmp(line, "{\"event\":\"stats\",\"frame\":3,", 26) &&
          strstr(line, "\"records\":8,\"presents\":8,") && strstr(line, "\"foreign_thread\":1,") &&
          strstr(line, "\"clear_blits\":1,") && strstr(line, "\"uploads\":1,\"dumps\":4,"));
    free(text);
    // Dumps of frame 2 (four records with a hash), none of frames 1 and 3.
    CHECK(pr_dumps == 4 && pr_dump_failures == 0);
    char *dump = read_text(L"capture.jsonl.frames\\frame-000002-45fbe4-1.es2f", &size);
    CHECK(dump && size == 32 + 768 + SCREEN_W * SCREEN_H && !memcmp(dump, "ES2F", 4));
    DWORD fields[7]; memcpy(fields, dump + 4, sizeof(fields));
    CHECK(fields[0] == 1 && fields[1] == 2 && fields[2] == 0x45fbe4 && fields[3] == 1 && fields[4] == SCREEN_W && fields[5] == SCREEN_H && fields[6] == 1);
    for (unsigned i = 0; i < 256; ++i)
        CHECK((BYTE)dump[32 + 3 * i] == quads[i].rgbRed && (BYTE)dump[33 + 3 * i] == quads[i].rgbGreen && (BYTE)dump[34 + 3 * i] == quads[i].rgbBlue);
    for (int y = 0; y < SCREEN_H; ++y) CHECK(!memcmp(dump + 800 + y * SCREEN_W, dib + (size_t)(2 + y) * DIB_W + 4, SCREEN_W));
    free(dump);
    CHECK(read_text(L"capture.jsonl.frames\\frame-000002-45490b-0.es2f", NULL) != NULL);
    CHECK(read_text(L"capture.jsonl.frames\\frame-000001-45fbe4-0.es2f", NULL) == NULL);
    if (with_presenter) CHECK(pr_presented_gdi == 5 && draw_calls == 5);
    else CHECK(draw_calls == 0 && blit_count == 10);
}

static void mode_presenter(void) {
    const WCHAR *pairs[] = {L"presentation", L"presenter", L"GDI", NULL};
    write_ini(L"speed.ini", pairs);
    set_env_ini(L"ES2_ORACLE_SPEED_CONFIG", L"speed.ini");   // the measurement INI is the second source
    install_all(TRUE);
    fill_dib(5); fill_quads(9); set_game(10, 3, 5);
    create_game_window();
    CHECK(patched() && !pr_capture && pr_present_gdi && pr_file == INVALID_HANDLE_VALUE && !strcmp(last_event, "presentation_gdi"));
    pr_stretch_dibits = fake_draw;
    HDC window_dc = (HDC)(uintptr_t)0x7777, source = (HDC)(uintptr_t)DIB_DC;
    struct { int cw, ch, ox, oy, width, height; } cases[] = {
        {SCREEN_W, SCREEN_H, 0, 0, SCREEN_W, SCREEN_H},              // 1:1
        {40, 30, 4, 3, 32, 24},                                      // 2x, letterbox both ways
        {50, 20, 17, 4, 16, 12},                                     // 1x, pillarbox
        {64, 48, 0, 0, 64, 48},                                      // 4x exact
        {8, 12, 0, 3, 8, 6},                                         // smaller: aspect-correct fit
        {16, 5, 5, 0, 6, 5},
    };
    // Before the first upload: forwarded.
    set_present(SCREEN_W, SCREEN_H, window_dc, 0, 0, SCREEN_W, SCREEN_H, source, 3, 5, SCREEN_W, SCREEN_H, SRCCOPY);
    present_from(SITE_MAIN_CALL);
    CHECK(blit_count == 1 && draw_calls == 0 && pr_no_table == 1 && pr_forwarded == 1);
    CHECK(upload(source) == 256);
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        set_present(cases[i].cw, cases[i].ch, window_dc, 0, 0, cases[i].cw, cases[i].ch, source, 3, 5, SCREEN_W, SCREEN_H, SRCCOPY);
        SetLastError(31);
        present_from(SITE_MAIN_CALL);
        CHECK(GetLastError() == 31 && blit_count == 1 && draw_calls == i + 1 && draw_hdc == window_dc);
        CHECK(draw_args[0] == 0 && draw_args[1] == 0 && draw_args[2] == cases[i].cw && draw_args[3] == cases[i].ch &&
              draw_args[4] == 0 && draw_args[5] == 0 && draw_args[6] == cases[i].cw && draw_args[7] == cases[i].ch);
        CHECK(draw_header.biWidth == cases[i].cw && draw_header.biHeight == -cases[i].ch && draw_header.biBitCount == 32 &&
              draw_header.biCompression == BI_RGB && draw_header.biPlanes == 1);
        expect_image(draw_pixels, cases[i].cw, cases[i].ch, 3, 5, cases[i].ox, cases[i].oy, cases[i].width, cases[i].height);
    }
    // 1:1 is exactly the table lookup of the source window.
    set_present(SCREEN_W, SCREEN_H, window_dc, 0, 0, SCREEN_W, SCREEN_H, source, 3, 5, SCREEN_W, SCREEN_H, SRCCOPY);
    present_from(SITE_MAIN_CALL);
    for (int y = 0; y < SCREEN_H; ++y) for (int x = 0; x < SCREEN_W; ++x)
        CHECK(draw_pixels[y * SCREEN_W + x] == rgb(dib[(size_t)(5 + y) * DIB_W + (size_t)(3 + x)]));
    unsigned drawn = draw_calls;
    // Not the full-screen case, another source, the clear, a failed draw: all forwarded unchanged.
    set_present(40, 30, window_dc, 0, 0, 39, 30, source, 3, 5, SCREEN_W, SCREEN_H, SRCCOPY); present_from(SITE_MAIN_CALL);
    set_present(40, 30, window_dc, 1, 0, 40, 30, source, 3, 5, SCREEN_W, SCREEN_H, SRCCOPY); present_from(SITE_MAIN_CALL);
    set_present(40, 30, window_dc, 0, 0, 40, 30, source, 3, 5, SCREEN_W - 1, SCREEN_H, SRCCOPY); present_from(SITE_MAIN_CALL);
    CHECK(pr_not_full_screen == 3 && blit_count == 4);
    set_present(40, 30, window_dc, 0, 0, 40, 30, (HDC)(uintptr_t)0x6666, 3, 5, SCREEN_W, SCREEN_H, SRCCOPY); present_from(SITE_MAIN_CALL);
    set_present(40, 30, window_dc, 0, 0, 40, 30, source, 3, 5, SCREEN_W, SCREEN_H, SRCPAINT); present_from(SITE_MAIN_CALL);
    CHECK(pr_bad_geometry == 2 && blit_count == 6);
    clear_blit(window_dc, source);
    CHECK(blit_count == 7 && pr_clear_blits == 1 && blits[6].a[0] == 1 && blits[6].a[3] == 40 && blits[6].a[6] == 10);
    set_present(0, 0, window_dc, 0, 0, 0, 0, source, 3, 5, SCREEN_W, SCREEN_H, SRCCOPY); present_from(SITE_MAIN_CALL);   // minimized
    CHECK(pr_bad_geometry == 3 && blit_count == 8);
    draw_result = 0;
    set_present(40, 30, window_dc, 0, 0, 40, 30, source, 3, 5, SCREEN_W, SCREEN_H, SRCCOPY); present_from(SITE_MAIN_CALL);
    CHECK(pr_draw_failures == 1 && blit_count == 9 && draw_calls == drawn + 1);
    CHECK(blits[8].a[2] == 40 && blits[8].a[3] == 30 && blits[8].a[4] == 3 && blits[8].a[5] == 5);
    CHECK(drawn == 7 && pr_presented_gdi == drawn && pr_forwarded == 8 && pr_presents == 1 + drawn + 7);
}

// The real GDI32 StretchDIBits into a 32-bit DIB section of the client size.
static void mode_gdi_real(void) {
    const WCHAR *pairs[] = {L"presentation", L"presenter", L"gdi", NULL};
    write_ini(L"presentation.ini", pairs);
    set_env_ini(L"ES2_PRESENTATION_CONFIG", L"presentation.ini");
    install_all(TRUE);
    fill_dib(11); fill_quads(17); set_game(1, 7, 3);
    create_game_window();
    CHECK(patched() && pr_present_gdi);
    CHECK(upload((HDC)(uintptr_t)DIB_DC) == 256);
    int sizes[][6] = {{SCREEN_W, SCREEN_H, 0, 0, SCREEN_W, SCREEN_H}, {35, 26, 1, 1, 32, 24}, {48, 40, 0, 2, 48, 36}};
    for (unsigned i = 0; i < 3; ++i) {
        int cw = sizes[i][0], ch = sizes[i][1];
        HDC dc = CreateCompatibleDC(NULL);
        BITMAPINFO info; memset(&info, 0, sizeof(info));
        info.bmiHeader.biSize = sizeof(info.bmiHeader); info.bmiHeader.biWidth = cw; info.bmiHeader.biHeight = -ch;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        void *bits = NULL;
        HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, NULL, 0);
        CHECK(dc && bitmap && bits);
        HGDIOBJ old = SelectObject(dc, bitmap);
        memset(bits, 0x5a, (size_t)cw * (size_t)ch * 4u);   // the presenter must also clear the borders
        set_present(cw, ch, dc, 0, 0, cw, ch, (HDC)(uintptr_t)DIB_DC, 7, 3, SCREEN_W, SCREEN_H, SRCCOPY);
        unsigned before = blit_count;
        present_from(SITE_MAIN_CALL);
        GdiFlush();
        CHECK(blit_count == before && pr_draw_failures == 0);
        DWORD *pixels = bits;   // GDI's fourth byte is not part of the contract
        for (size_t p = 0; p < (size_t)cw * (size_t)ch; ++p) pixels[p] &= 0xffffffu;
        expect_image(pixels, cw, ch, 7, 3, sizes[i][2], sizes[i][3], sizes[i][4], sizes[i][5]);
        SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc);
    }
    CHECK(pr_presented_gdi == 3);
}

static void mode_beside(void) {
    WCHAR tick[MAX_PATH]; path_in_tmp(tick, L"tick.log");
    path_in_tmp(config_path, L"es2-oracle.ini");
    WCHAR dumps[MAX_PATH]; path_in_tmp(dumps, L"dumps");
    const WCHAR *oracle[] = {L"trace", L"tick_log", tick, L"presentation", L"capture", L"1",
                             L"presentation", L"capture_report", L"beside_tick_log",
                             L"presentation", L"capture_dump_frames", L"5", L"presentation", L"capture_dump_dir", dumps, NULL};
    write_ini(L"es2-oracle.ini", oracle);   // no environment: es2-oracle.ini is the third source
    install_all(TRUE);
    set_game(5, 0, 0); fill_dib(1); fill_quads(1);
    create_game_window();
    CHECK(patched() && pr_capture && !pr_present_gdi);
    char *text = read_text(L"tick.log.capture.ndjson", NULL);
    CHECK(text && line_count(text) == 1 && strstr(text, "\"presenter\":\"original\",\"dump_frames\":1"));
    free(text);
    // capture_dump_dir: the dumps go to <dir>\<capture file name>.frames, not beside the capture.
    CHECK(upload((HDC)(uintptr_t)DIB_DC) == 256);
    set_present(SCREEN_W, SCREEN_H, (HDC)(uintptr_t)0x7777, 0, 0, SCREEN_W, SCREEN_H, (HDC)(uintptr_t)DIB_DC, 0, 0, SCREEN_W, SCREEN_H, SRCCOPY);
    present_from(SITE_MAIN_CALL);
    CHECK(pr_dumps == 1 && read_text(L"dumps\\tick.log.capture.ndjson.frames\\frame-000005-45fbe4-0.es2f", NULL) != NULL);
    WCHAR beside[MAX_PATH]; path_in_tmp(beside, L"tick.log.capture.ndjson.frames");
    CHECK(GetFileAttributesW(beside) == INVALID_FILE_ATTRIBUTES);
}

// capture_dump_range names a regular series of frames; the list and the range add up.
static void mode_range(void) {
    // The parser alone: accepted and refused spellings.
    struct { const WCHAR *text; BOOL ok, set; DWORD from, to, step; } parse[] = {
        {L"", TRUE, FALSE, 0, 0, 0}, {L"  ", TRUE, FALSE, 0, 0, 0},
        {L"3-9", TRUE, TRUE, 3, 9, 1}, {L" 3 - 9 / 2 ", TRUE, TRUE, 3, 9, 2}, {L"5-5/7", TRUE, TRUE, 5, 5, 7},
        {L"0-4095", TRUE, TRUE, 0, 4095, 1}, {L"0-8190/2", TRUE, TRUE, 0, 8190, 2},
        {L"0-4096", FALSE, FALSE, 0, 0, 0}, {L"9-3", FALSE, FALSE, 0, 0, 0}, {L"1-2/0", FALSE, FALSE, 0, 0, 0},
        {L"x", FALSE, FALSE, 0, 0, 0}, {L"3", FALSE, FALSE, 0, 0, 0}, {L"3-", FALSE, FALSE, 0, 0, 0},
        {L"3-9/", FALSE, FALSE, 0, 0, 0}, {L"3-9/2x", FALSE, FALSE, 0, 0, 0}, {L"-3-9", FALSE, FALSE, 0, 0, 0},
        {L"4294967295-4294967295", FALSE, FALSE, 0, 0, 0},
    };
    for (unsigned i = 0; i < sizeof(parse) / sizeof(parse[0]); ++i) {
        CHECK(pr_parse_range(parse[i].text) == parse[i].ok);
        CHECK(pr_range_set == parse[i].set);
        if (parse[i].set) CHECK(pr_range_from == parse[i].from && pr_range_to == parse[i].to && pr_range_step == parse[i].step);
    }
    // The selection: frames 3, 5, 7, 9 of the range, 4 from the list.
    CHECK(pr_parse_range(L"3-9/2"));
    pr_dump_frames[0] = 4; pr_dump_frame_count = 1;
    BOOL want[12] = {0, 0, 0, 1, 1, 1, 0, 1, 0, 1, 0, 0};
    for (DWORD f = 0; f < 12; ++f) CHECK(pr_dump_wanted(f) == want[f]);
    pr_range_dumps = PR_DUMP_RANGE_RECORDS_MAX;   // the cap stops the range, never the list
    CHECK(!pr_dump_wanted(5) && pr_dump_wanted(4));
    pr_range_dumps = 0; pr_range_set = FALSE; pr_dump_frame_count = 0;

    // The whole path: the start line, the dump files, the stats.
    WCHAR report[MAX_PATH]; path_in_tmp(report, L"range.ndjson");
    const WCHAR *pairs[] = {L"presentation", L"capture", L"1", L"presentation", L"capture_report", report,
                            L"presentation", L"capture_dump_frames", L"4", L"presentation", L"capture_dump_range", L"3-9/2", NULL};
    write_ini(L"presentation.ini", pairs);
    set_env_ini(L"ES2_PRESENTATION_CONFIG", L"presentation.ini");
    install_all(TRUE);
    fill_dib(1); fill_quads(3); set_game(1, 0, 0);
    create_game_window();
    CHECK(patched() && pr_capture && pr_range_set && pr_dump_frame_count == 1 && !strcmp(last_event, "presentation_capture"));
    HDC window_dc = (HDC)(uintptr_t)0x7777, source = (HDC)(uintptr_t)DIB_DC;
    CHECK(upload(source) == 256);
    for (DWORD f = 1; f <= 10; ++f) {
        put32(PR_FRAME_RVA, f);
        fill_dib(f);
        set_present(SCREEN_W, SCREEN_H, window_dc, 0, 0, SCREEN_W, SCREEN_H, source, 0, 0, SCREEN_W, SCREEN_H, SRCCOPY);
        present_from(SITE_MAIN_CALL);
    }
    presenter_shutdown();
    CHECK(pr_dumps == 5 && pr_dump_failures == 0 && pr_range_dumps == 4);
    static const DWORD dumped[] = {3, 4, 5, 7, 9};
    for (DWORD f = 1; f <= 10; ++f) {
        WCHAR name[96]; swprintf(name, 96, L"range.ndjson.frames\\frame-%06lu-45fbe4-0.es2f", (unsigned long)f);
        BOOL expected = FALSE;
        for (unsigned i = 0; i < 5; ++i) expected |= dumped[i] == f;
        char *dump = read_text(name, NULL);
        CHECK((dump != NULL) == expected);
        free(dump);
    }
    char *text = read_text(L"range.ndjson", NULL);
    char line[512], want_line[256];
    CHECK(text && nth_line(text, 0, line, sizeof(line)));
    snprintf(want_line, sizeof(want_line), "{\"event\":\"start\",\"schema\":1,\"key\":\"frame_counter_0x4d257b\",\"presenter\":\"original\","
             "\"dump_frames\":1,\"pid\":%lu,\"dump_range\":[3,9,2]}", (unsigned long)GetCurrentProcessId());
    CHECK(!strcmp(line, want_line));
    CHECK(line_count(text) == 1 + 10 + 1);   // start, ten records, stats
    free(text);
}

// An unusable range switches only the range off; the capture and the list stay.
static void mode_range_refused(void) {
    WCHAR report[MAX_PATH]; path_in_tmp(report, L"range-refused.ndjson");
    const WCHAR *pairs[] = {L"presentation", L"capture", L"1", L"presentation", L"capture_report", report,
                            L"presentation", L"capture_dump_frames", L"2", L"presentation", L"capture_dump_range", L"9-3", NULL};
    write_ini(L"presentation.ini", pairs);
    set_env_ini(L"ES2_PRESENTATION_CONFIG", L"presentation.ini");
    install_all(TRUE);
    fill_dib(1); fill_quads(3); set_game(1, 0, 0);
    create_game_window();
    CHECK(patched() && pr_capture && !pr_range_set && pr_dump_frame_count == 1);
    CHECK(!strcmp(last_event, "presentation_capture"));
    char *text = read_text(L"range-refused.ndjson", NULL);
    CHECK(text && strstr(text, "\"dump_frames\":1,\"pid\":") && !strstr(text, "dump_range"));
    free(text);
}

static void mode_refused(const WCHAR *which) {
    WCHAR report[MAX_PATH]; path_in_tmp(report, L"exists.jsonl");
    if (!wcscmp(which, L"exists")) {   // capture only: nothing else to keep, the whole switch stays off
        HANDLE f = CreateFileW(report, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        CHECK(f != INVALID_HANDLE_VALUE); CloseHandle(f);
        const WCHAR *pairs[] = {L"presentation", L"capture", L"1", L"presentation", L"capture_report", report, NULL};
        write_ini(L"presentation.ini", pairs);
    } else if (!wcscmp(which, L"unknown")) {
        const WCHAR *pairs[] = {L"presentation", L"presenter", L"sdl3", NULL};
        write_ini(L"presentation.ini", pairs);
    } else if (!wcscmp(which, L"noreport")) {
        const WCHAR *pairs[] = {L"presentation", L"capture", L"1", NULL};
        write_ini(L"presentation.ini", pairs);
    } else {   // off: the speed INI asks for capture, but ES2_PRESENTATION_CONFIG takes precedence and asks for nothing
        const WCHAR *speed[] = {L"presentation", L"capture", L"1", L"presentation", L"capture_report", report, NULL};
        write_ini(L"speed.ini", speed);
        const WCHAR *pairs[] = {L"presentation", L"every_n", L"0", NULL};
        write_ini(L"presentation.ini", pairs);
        set_env_ini(L"ES2_ORACLE_SPEED_CONFIG", L"speed.ini");
    }
    set_env_ini(L"ES2_PRESENTATION_CONFIG", L"presentation.ini");
    install_all(TRUE);
    events = 0;
    create_game_window();
    CHECK(pr_decided && pr_state == 1 && !pr_capture && !pr_present_gdi);
    CHECK(slot(PR_STRETCH_IAT_RVA) == (void *)fake_stretch && slot(PR_TABLE_IAT_RVA) == (void *)fake_table);
    if (!wcscmp(which, L"off")) CHECK(events == 0 && GetFileAttributesW(report) == INVALID_FILE_ATTRIBUTES);
    else if (!wcscmp(which, L"unknown")) CHECK(!strcmp(last_event, "presentation_presenter_unknown"));
    else CHECK(!strcmp(last_event, "presentation_capture_refused"));
    create_game_window();   // decided once
    CHECK(created == 2 && slot(PR_STRETCH_IAT_RVA) == (void *)fake_stretch);
}

// The report of an earlier process of the session exists (or has an unusable name). The capture is
// refused, the presenter is the feature and stays: installed, presenting, no capture file, no capture records.
static void mode_exists_gdi(BOOL unusable_name) {
    WCHAR report[MAX_PATH]; path_in_tmp(report, L"taken.ndjson");
    if (!unusable_name) {
        HANDLE f = CreateFileW(report, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        CHECK(f != INVALID_HANDLE_VALUE); CloseHandle(f);
    } else path_in_tmp(report, L"no-such-directory\\report.ndjson");
    const WCHAR *pairs[] = {L"presentation", L"capture", L"1", L"presentation", L"capture_report", report,
                            L"presentation", L"capture_dump_frames", L"1", L"presentation", L"presenter", L"gdi", NULL};
    write_ini(L"presentation.ini", pairs);
    set_env_ini(L"ES2_PRESENTATION_CONFIG", L"presentation.ini");
    install_all(TRUE);
    fill_dib(5); fill_quads(9); set_game(10, 3, 5);
    events = 0;
    create_game_window();
    CHECK(events == 2 && !strcmp(last_event, "presentation_gdi"));   // presentation_capture_refused, then presentation_gdi
    CHECK(patched() && pr_state == 2 && pr_decided && !pr_capture && pr_present_gdi && pr_file == INVALID_HANDLE_VALUE);
    CHECK(pr_dump_frame_count == 0);
    pr_stretch_dibits = fake_draw;
    HDC window_dc = (HDC)(uintptr_t)0x7777, source = (HDC)(uintptr_t)DIB_DC;
    CHECK(upload(source) == 256);
    set_present(40, 30, window_dc, 0, 0, 40, 30, source, 3, 5, SCREEN_W, SCREEN_H, SRCCOPY);
    present_from(SITE_MAIN_CALL);
    CHECK(draw_calls == 1 && blit_count == 0 && pr_presented_gdi == 1 && pr_records == 0);
    expect_image(draw_pixels, 40, 30, 3, 5, 4, 3, 32, 24);
    if (!unusable_name) {
        char *text = read_text(L"taken.ndjson", NULL);   // the earlier process's report is untouched
        CHECK(text && !text[0]);
        free(text);
    }
    create_game_window();   // decided once
    CHECK(created == 2 && patched());
}

// Two real processes of one session (VSHELL -> DBSIM -> VSHELL -> DBSIM): the parent and a child process
// of this binary both run the scenario with the same INI; `{pid}` in capture_report gives each its own report.
static void session_scenario(void) {
    set_env_ini(L"ES2_PRESENTATION_CONFIG", L"presentation.ini");
    install_all(TRUE);
    fill_dib(1); fill_quads(3); set_game(1, 4, 2);
    create_game_window();
    CHECK(patched() && pr_state == 2 && pr_capture && pr_present_gdi && !strcmp(last_event, "presentation_capture_gdi"));
    pr_stretch_dibits = fake_draw;
    HDC window_dc = (HDC)(uintptr_t)0x7777, source = (HDC)(uintptr_t)DIB_DC;
    CHECK(upload(source) == 256);
    set_present(SCREEN_W, SCREEN_H, window_dc, 0, 0, SCREEN_W, SCREEN_H, source, 4, 2, SCREEN_W, SCREEN_H, SRCCOPY);
    present_from(SITE_MAIN_CALL);
    CHECK(draw_calls == 1 && pr_presented_gdi == 1 && pr_records == 1);
    WCHAR name[80]; swprintf(name, 80, L"session.%lu.ndjson", (unsigned long)GetCurrentProcessId());
    char *text = read_text(name, NULL);   // the start line is flushed at once
    char line[512], want[256];
    CHECK(text && nth_line(text, 0, line, sizeof(line)));
    snprintf(want, sizeof(want), "{\"event\":\"start\",\"schema\":1,\"key\":\"frame_counter_0x4d257b\",\"presenter\":\"gdi\","
             "\"dump_frames\":0,\"pid\":%lu}", (unsigned long)GetCurrentProcessId());
    CHECK(!strcmp(line, want));
    free(text);
}
static void mode_session(void) {
    WCHAR report[MAX_PATH]; path_in_tmp(report, L"session.{pid}.ndjson");
    const WCHAR *pairs[] = {L"presentation", L"capture", L"1", L"presentation", L"capture_report", report,
                            L"presentation", L"presenter", L"gdi", NULL};
    write_ini(L"presentation.ini", pairs);
    // The child inherits TEST_TMPDIR and this INI (written before it starts).
    WCHAR exe[MAX_PATH], command[MAX_PATH + 40];
    CHECK(GetModuleFileNameW(NULL, exe, MAX_PATH) && swprintf(command, MAX_PATH + 40, L"\"%ls\" session_child", exe) > 0);
    STARTUPINFOW startup; memset(&startup, 0, sizeof(startup)); startup.cb = sizeof(startup);
    PROCESS_INFORMATION child;
    /* window-lint: allow inherits console: the child is this fixture, run by a Bazel test and sharing its windowless console */
    CHECK(CreateProcessW(exe, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &child));
    session_scenario();
    CHECK(WaitForSingleObject(child.hProcess, 60000) == WAIT_OBJECT_0);
    DWORD code = 99;
    CHECK(GetExitCodeProcess(child.hProcess, &code) && code == 0);
    CloseHandle(child.hProcess); CloseHandle(child.hThread);
    CHECK(child.dwProcessId != GetCurrentProcessId());
    // Both reports exist under their own names and the parent's survived the child's decision.
    WCHAR name[80], mine[MAX_PATH], theirs[MAX_PATH];
    swprintf(name, 80, L"session.%lu.ndjson", (unsigned long)GetCurrentProcessId()); path_in_tmp(mine, name);
    swprintf(name, 80, L"session.%lu.ndjson", (unsigned long)child.dwProcessId); path_in_tmp(theirs, name);
    CHECK(GetFileAttributesW(mine) != INVALID_FILE_ATTRIBUTES && GetFileAttributesW(theirs) != INVALID_FILE_ATTRIBUTES);
    WCHAR pattern[MAX_PATH]; path_in_tmp(pattern, L"session.*.ndjson");
    WIN32_FIND_DATAW found; unsigned files = 0;
    HANDLE search = FindFirstFileW(pattern, &found);
    CHECK(search != INVALID_HANDLE_VALUE);
    do ++files; while (FindNextFileW(search, &found));
    FindClose(search);
    CHECK(files == 2);   // and the token itself is never a file name
    WCHAR literal[MAX_PATH]; path_in_tmp(literal, L"session.{pid}.ndjson");
    CHECK(GetFileAttributesW(literal) == INVALID_FILE_ATTRIBUTES);
}
static void mode_session_child(void) { session_scenario(); }

static void mode_mismatch(void) {
    CHECK(install_input_journal_image(image, (uintptr_t)image));
    CHECK(install_window_shim_image(image, (uintptr_t)image));
    // Each checked byte matters: change one per round on a fresh state.
    for (unsigned i = 0; i < PR_SIGNATURE_COUNT + 2; ++i) {
        DWORD rva = i < PR_SIGNATURE_COUNT ? pr_signatures[i].rva + pr_signatures[i].length - 1 :
                    i == PR_SIGNATURE_COUNT ? PR_STRETCH_THUNK_RVA + 1 : PR_TABLE_THUNK_RVA + 5;
        image[rva] ^= 0x01;
        pr_state = 0;
        CHECK(!install_presenter_image(image, (uintptr_t)image));
        CHECK(slot(WS_CREATE_IAT_RVA) == (void *)ws_create_window && pr_state == -1);
        image[rva] ^= 0x01;
    }
    pr_state = 0;
    CHECK(install_presenter_image(image, (uintptr_t)image));   // all bytes restored: accepted
    // Order: without the input journal nothing is chained.
    pr_state = 0; ij_iat_state = 0;
    CHECK(!install_presenter_image(image, (uintptr_t)image));
}

static void mode_journal_only(void) {
    install_all(FALSE);   // the window shim not installed: chained in front of the journal
    CHECK(pr_next_create == ij_create_window);
    create_game_window();
    CHECK(created == 1 && ij_window == (HWND)(uintptr_t)0x1234 && pr_decided);
}

// ---- pure parts --------------------------------------------------------------------------
static unsigned rng_state = 12345;
static unsigned next_random(void) { rng_state = rng_state * 1103515245u + 12345u; return rng_state >> 8; }

static void mode_sha(void) {
    static const char *vectors[][2] = {
        {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
    };
    for (unsigned i = 0; i < 3; ++i) {
        sha256_ctx s; char hex[65]; sha256_init(&s);
        sha256_update(&s, (const BYTE *)vectors[i][0], strlen(vectors[i][0])); sha256_hex(&s, hex);
        CHECK(!strcmp(hex, vectors[i][1]));
    }
    static BYTE data[5000];
    for (unsigned i = 0; i < sizeof(data); ++i) data[i] = (BYTE)next_random();
    for (unsigned n = 0; n < 300; ++n) {
        unsigned length = n < 200 ? n : 200 + next_random() % 4800;
        sha256_ctx s; char hex[65], want[65]; sha256_init(&s);
        for (unsigned at = 0; at < length;) {   // random chunking
            unsigned take = 1 + next_random() % 97; if (take > length - at) take = length - at;
            sha256_update(&s, data + at, take); at += take;
        }
        sha256_hex(&s, hex); crypto_sha256(data, length, want);
        CHECK(!strcmp(hex, want));
    }
    // The capture's own entry points (pr_hash_window with a stride, pr_rgb_hash) against CryptoAPI over the packed bytes.
    for (unsigned t = 0; t < 40; ++t) {
        int w = 1 + (int)(next_random() % 120), h = 1 + (int)(next_random() % 40), stride = w + (int)(next_random() % 50);
        static BYTE rows[40 * 170], packed[40 * 120];
        for (unsigned i = 0; i < sizeof(rows); ++i) rows[i] = (BYTE)next_random();
        for (int y = 0; y < h; ++y) memcpy(packed + (size_t)y * (size_t)w, rows + (size_t)y * (size_t)stride, (size_t)w);
        char got[65], want[65];
        pr_hash_window(rows, stride, w, h, got); crypto_sha256(packed, (DWORD)(w * h), want);
        CHECK(!strcmp(got, want));
    }
    {
        BYTE rgb[768]; char got[65], want[65];
        for (unsigned i = 0; i < sizeof(rgb); ++i) rgb[i] = (BYTE)next_random();
        pr_rgb_hash(rgb, got); crypto_sha256(rgb, sizeof(rgb), want);
        CHECK(!strcmp(got, want));
    }
}

static void mode_scaler(void) {
    pr_layout l;
    CHECK(!pr_compute_layout(0, 1, 1, 1, &l) && !pr_compute_layout(1, 1, 0, 1, &l) && !pr_compute_layout(1, 1, 1, -1, &l));
    CHECK(!pr_compute_layout(640, 480, 4097, 4096, &l) && pr_compute_layout(640, 480, 4096, 4096, &l) && l.scale == 6);
    CHECK(pr_compute_layout(640, 480, 640, 480, &l) && l.scale == 1 && l.x == 0 && l.y == 0 && l.width == 640 && l.height == 480);
    CHECK(pr_compute_layout(640, 480, 1920, 1080, &l) && l.scale == 2 && l.x == 320 && l.y == 60 && l.width == 1280 && l.height == 960);
    CHECK(pr_compute_layout(640, 480, 2560, 1440, &l) && l.scale == 3 && l.x == 320 && l.y == 0);
    CHECK(pr_compute_layout(320, 240, 1366, 768, &l) && l.scale == 3 && l.x == 203 && l.y == 24 && l.width == 960);
    CHECK(pr_compute_layout(640, 480, 1024, 600, &l) && l.scale == 1 && l.x == 192 && l.y == 60);
    CHECK(pr_compute_layout(640, 480, 1024, 400, &l) && l.scale == 0 && l.width == 533 && l.height == 400 && l.x == 245 && l.y == 0);
    CHECK(pr_compute_layout(640, 480, 600, 1000, &l) && l.scale == 0 && l.width == 600 && l.height == 450 && l.y == 275);
    CHECK(pr_compute_layout(640, 480, 1, 1, &l) && l.width == 1 && l.height == 1);
    // Properties over many sizes: fits, centred, integer scale maximal, fractional keeps the aspect within a pixel.
    for (unsigned i = 0; i < 20000; ++i) {
        int w = 1 + (int)(next_random() % 700), h = 1 + (int)(next_random() % 500);
        int cw = 1 + (int)(next_random() % 2600), ch = 1 + (int)(next_random() % 1600);
        CHECK(pr_compute_layout(w, h, cw, ch, &l));
        CHECK(l.width >= 1 && l.height >= 1 && l.width <= cw && l.height <= ch);
        CHECK(l.x == (cw - l.width) / 2 && l.y == (ch - l.height) / 2);
        if (l.scale) {
            CHECK(l.width == w * l.scale && l.height == h * l.scale);
            CHECK(w * (l.scale + 1) > cw || h * (l.scale + 1) > ch);
        } else {
            CHECK(w > cw || h > ch);
            CHECK(l.width == cw || l.height == ch);
            int64_t cross = (int64_t)l.width * h - (int64_t)l.height * w;   // |width/height - w/h| < 1 pixel
            CHECK((cross < 0 ? -cross : cross) < (int64_t)(w > h ? w : h));
        }
    }
    // Rendering of random frames against the definition.
    static BYTE frame[64 * 48];
    static DWORD table[256], out[200 * 150];
    static int columns[200];
    for (unsigned i = 0; i < 256; ++i) table[i] = next_random() & 0xffffffu;
    for (unsigned round = 0; round < 400; ++round) {
        int stride = 64, w = 1 + (int)(next_random() % 64), h = 1 + (int)(next_random() % 48);
        int cw = 1 + (int)(next_random() % 200), ch = 1 + (int)(next_random() % 150);
        for (unsigned i = 0; i < sizeof(frame); ++i) frame[i] = (BYTE)next_random();
        CHECK(pr_compute_layout(w, h, cw, ch, &l));
        for (unsigned i = 0; i < sizeof(out) / 4; ++i) out[i] = 0xdeadbeefu;
        pr_render(frame, stride, w, h, table, out, cw, ch, &l, columns);
        for (int y = 0; y < ch; ++y) for (int x = 0; x < cw; ++x) {
            DWORD want = 0;
            if (x >= l.x && x < l.x + l.width && y >= l.y && y < l.y + l.height) {
                int u = l.scale ? (x - l.x) / l.scale : (int)((int64_t)(x - l.x) * w / l.width);
                int v = l.scale ? (y - l.y) / l.scale : (int)((int64_t)(y - l.y) * h / l.height);
                want = table[frame[v * stride + u]];
            }
            CHECK(out[y * cw + x] == want);
        }
    }
    RGBQUAD q[256]; DWORD t[256];
    for (unsigned i = 0; i < 256; ++i) q[i] = (RGBQUAD){(BYTE)i, (BYTE)(i + 1), (BYTE)(i + 2), 0xff};
    pr_table_from_quads(q, t);
    CHECK(t[0] == 0x020100u && t[255] == 0x0100ffu);   // red (i + 2) in bits 16..23, blue (i) in bits 0..7, reserved dropped
}

// The cost of the capture and of the presenter at the oracle's size: a 640 x 480 window of a
// 1280 x 960 DIB, presented into 1920 x 1080 (2x). Bounds are generous (wall time, shared host).
static void mode_cost(void) {
    WCHAR report[MAX_PATH]; path_in_tmp(report, L"cost.jsonl");
    const WCHAR *pairs[] = {L"presentation", L"capture", L"1", L"presentation", L"capture_report", report,
                            L"presentation", L"presenter", L"gdi", NULL};
    write_ini(L"presentation.ini", pairs);
    set_env_ini(L"ES2_PRESENTATION_CONFIG", L"presentation.ini");
    install_all(TRUE);
    BYTE *big = malloc(1280u * 960u);
    for (unsigned i = 0; i < 1280u * 960u; ++i) big[i] = (BYTE)(i * 31u >> 3);
    put32(PR_FRAME_RVA, 1); put32(PR_SCREEN_W_RVA, 640); put32(PR_SCREEN_H_RVA, 480);
    put32(PR_DIB_W_RVA, 1280); put32(PR_DIB_H_RVA, 960); put32(PR_DIB_BITS_RVA, (DWORD)(uintptr_t)big); put32(PR_DIB_DC_RVA, DIB_DC);
    create_game_window();
    CHECK(patched());
    pr_stretch_dibits = fake_draw;
    fill_quads(2); CHECK(upload((HDC)(uintptr_t)DIB_DC) == 256);
    set_present(1920, 1080, (HDC)(uintptr_t)0x7777, 0, 0, 1920, 1080, (HDC)(uintptr_t)DIB_DC, 0, 0, 640, 480, SRCCOPY);
    enum { ROUNDS = 30 };
    for (unsigned i = 0; i < ROUNDS; ++i) { put32(PR_FRAME_RVA, 1 + i); present_from(SITE_MAIN_CALL); }
    CHECK(pr_presented_gdi == ROUNDS && pr_records == ROUNDS);
    LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency);
    double capture_ms = (double)pr_capture_qpc * 1000.0 / (double)frequency.QuadPart / ROUNDS;
    double present_ms = (double)pr_present_qpc * 1000.0 / (double)frequency.QuadPart / ROUNDS;
    printf("cost: capture %.3f ms, presenter %.3f ms per present (mean of %d), max %.3f / %.3f ms\n", capture_ms, present_ms, ROUNDS,
           (double)pr_capture_qpc_max * 1000.0 / (double)frequency.QuadPart, (double)pr_present_qpc_max * 1000.0 / (double)frequency.QuadPart);
    CHECK(capture_ms < 15.0 && present_ms < 40.0);
    free(big);
}


// ---- DirectDraw branch (ddraw_windowed.h) ------------------------------------------------
typedef HRESULT (WINAPI *M1)(void *);
typedef HRESULT (WINAPI *M2)(void *, uintptr_t);
typedef HRESULT (WINAPI *M3)(void *, uintptr_t, uintptr_t);
typedef HRESULT (WINAPI *M4)(void *, uintptr_t, uintptr_t, uintptr_t);
typedef HRESULT (WINAPI *M5)(void *, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
typedef HRESULT (WINAPI *M6)(void *, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
#define METHOD(o, slot) ((*(void ***)(o))[slot])
#define CALL1(o, slot) ((M1)METHOD(o, slot))(o)
#define CALL2(o, slot, a) ((M2)METHOD(o, slot))(o, (uintptr_t)(a))
#define CALL3(o, slot, a, b) ((M3)METHOD(o, slot))(o, (uintptr_t)(a), (uintptr_t)(b))
#define CALL4(o, slot, a, b, c) ((M4)METHOD(o, slot))(o, (uintptr_t)(a), (uintptr_t)(b), (uintptr_t)(c))
#define CALL5(o, slot, a, b, c, d) ((M5)METHOD(o, slot))(o, (uintptr_t)(a), (uintptr_t)(b), (uintptr_t)(c), (uintptr_t)(d))
#define CALL6(o, slot, a, b, c, d, e) ((M6)METHOD(o, slot))(o, (uintptr_t)(a), (uintptr_t)(b), (uintptr_t)(c), (uintptr_t)(d), (uintptr_t)(e))
static RECT dd_client = {0, 0, SCREEN_W, SCREEN_H};
static unsigned dd_dc_gets, dd_dc_releases;
static HDC WINAPI fake_get_dc(HWND w) { CHECK(w == (HWND)(uintptr_t)0x1234); ++dd_dc_gets; return (HDC)(uintptr_t)0x8888; }
static int WINAPI fake_release_dc(HWND w, HDC dc) { CHECK(w == (HWND)(uintptr_t)0x1234 && dc == (HDC)(uintptr_t)0x8888); ++dd_dc_releases; return 1; }
static BOOL WINAPI fake_client(HWND w, LPRECT r) { (void)w; *r = dd_client; return TRUE; }
// 0x465628's frame, then `push 0; push surface; mov ecx, [surface]; call [ecx+0x80]` at 0x465870 (Unlock),
// or `push 1; push 0; push primary; mov edx, [primary]; call [edx+0x2c]` at 0x465892 (Flip).
static void set_dd_present(void *surface, BOOL flip) {
    DWORD call = flip ? 0x00065892u : 0x00065870u, at = call - (flip ? 20u : 15u);
    BYTE code[24]; unsigned n = 0;
    if (flip) { code[n++] = 0x68; DWORD one = 1; memcpy(code + n, &one, 4); n += 4; }
    code[n++] = 0x68; memset(code + n, 0, 4); n += 4;
    code[n++] = 0x68; DWORD self = (DWORD)(uintptr_t)surface; memcpy(code + n, &self, 4); n += 4;
    code[n++] = flip ? 0xba : 0xb9; DWORD table = (DWORD)(uintptr_t)*(void **)surface; memcpy(code + n, &table, 4); n += 4;
    CHECK(at + n == call);
    put_bytes(at, code, n);
    put_rel_call(0x00065628u + 52u, 0xe9, at);   // the end of set_present's prologue jumps here instead
}
// The clear 0x46548c: the same calls through edx / eax, entered by a jmp; they return to a `ret`.
static void dd_clear(void *surface, BOOL flip) {
    DWORD call = flip ? 0x00065547u : 0x00065524u;
    BYTE code[32]; unsigned n = 0;
    if (flip) { code[n++] = 0x68; DWORD one = 1; memcpy(code + n, &one, 4); n += 4; }
    code[n++] = 0x68; memset(code + n, 0, 4); n += 4;
    code[n++] = 0x68; DWORD self = (DWORD)(uintptr_t)surface; memcpy(code + n, &self, 4); n += 4;
    code[n++] = flip ? 0xb8 : 0xba; DWORD table = (DWORD)(uintptr_t)*(void **)surface; memcpy(code + n, &table, 4); n += 4;
    put_bytes(CLEAR_STUB + 0x100, code, n);
    put_rel_call(CLEAR_STUB + 0x100 + n, 0xe9, call);
    ((void (*)(void))(void *)(image + CLEAR_STUB + 0x100))();
}
static PALETTEENTRY dd_entries[256];
static void fill_entries(unsigned seed) {
    for (unsigned i = 0; i < 256; ++i) dd_entries[i] = (PALETTEENTRY){(BYTE)(i * 5u + seed), (BYTE)(i ^ 0x3c), (BYTE)(seed - i), PC_NOCOLLAPSE};
}
static DWORD entry_rgb(BYTE index) { return (DWORD)dd_entries[index].peRed << 16 | (DWORD)dd_entries[index].peGreen << 8 | dd_entries[index].peBlue; }
static void expect_dd_image(const BYTE *bits, int pitch, int cw, int ch, int ox, int oy, int width, int height) {
    CHECK(draw_header.biWidth == cw && draw_header.biHeight == -ch && draw_hdc == (HDC)(uintptr_t)0x8888);
    for (int y = 0; y < ch; ++y) for (int x = 0; x < cw; ++x) {
        DWORD want = 0;
        if (x >= ox && x < ox + width && y >= oy && y < oy + height)
            want = entry_rgb(bits[(size_t)((y - oy) * SCREEN_H / height) * (size_t)pitch + (size_t)((x - ox) * SCREEN_W / width)]);
        CHECK(draw_pixels[(size_t)y * (size_t)cw + (size_t)x] == want);
    }
}
static BYTE *dd_lock(void *surface, LONG *pitch) {
    DDSURFACEDESC d; memset(&d, 0, sizeof(d)); d.dwSize = sizeof(d);
    CHECK(sizeof(d) == 0x6c && CALL5(surface, 25, NULL, &d, DDLOCK_WAIT, NULL) == DD_OK);
    CHECK(d.dwWidth == SCREEN_W && d.dwHeight == SCREEN_H && d.ddpfPixelFormat.dwRGBBitCount == 8 && (d.dwFlags & DDSD_LPSURFACE));
    *pitch = d.lPitch;
    return d.lpSurface;
}
static void dd_paint(BYTE *bits, LONG pitch, unsigned seed) {
    for (int y = 0; y < SCREEN_H; ++y) for (int x = 0; x < SCREEN_W; ++x) bits[y * pitch + x] = (BYTE)(x * 17 + y * 5 + (int)seed);
}
static void mode_ddraw(void) {
    WCHAR report[MAX_PATH]; path_in_tmp(report, L"dd.jsonl");
    const WCHAR *pairs[] = {L"presentation", L"capture", L"1", L"presentation", L"capture_report", report,
                            L"presentation", L"directdraw", L"Windowed", NULL};
    write_ini(L"presentation.ini", pairs);
    set_env_ini(L"ES2_PRESENTATION_CONFIG", L"presentation.ini");
    CHECK(!ddw_requested());   // not before the image is verified
    install_all(TRUE);
    set_game(40, 0, 0); put32(PR_MODE_RVA, 1);
    create_game_window();
    CHECK(patched() && pr_capture && ddw_requested());
    pr_stretch_dibits = fake_draw; ddw_get_dc = fake_get_dc; ddw_release_dc = fake_release_dc; ddw_client_rect = fake_client;
    // Creation as 0x46658c / 0x4665c4 / 0x4667c8 do it.
    void *dd = NULL;
    CHECK(ddw_create((GUID *)(uintptr_t)0x1000, (LPDIRECTDRAW *)&dd, NULL) == DDERR_INVALIDDIRECTDRAWGUID && !dd);
    CHECK(ddw_create(NULL, (LPDIRECTDRAW *)&dd, NULL) == DD_OK && dd);
    DDSURFACEDESC d; memset(&d, 0, sizeof(d)); d.dwSize = 0x6c; d.ddsCaps.dwCaps = 0x4200;   // high resolution: no DDSD_CAPS, no back buffer
    void *primary = NULL;
    CHECK(CALL4(dd, 6, &d, &primary, NULL) == DDERR_INVALIDPARAMS && !primary);   // no display mode yet
    CHECK(CALL3(dd, 20, (HWND)(uintptr_t)0x1234, 0x57) == DD_OK);
    CHECK(CALL4(dd, 21, SCREEN_W, SCREEN_H, 16) == DDERR_INVALIDMODE && CALL4(dd, 21, SCREEN_W, SCREEN_H, 8) == DD_OK);
    CHECK(CALL4(dd, 6, &d, &primary, NULL) == DD_OK && primary);
    DDSCAPS caps = {0};
    CHECK(CALL2(primary, 14, &caps) == DD_OK && (caps.dwCaps & DDSCAPS_PRIMARYSURFACE) && !(caps.dwCaps & DDSCAPS_FLIP));
    void *none = (void *)1; DDSCAPS back_caps = {DDSCAPS_BACKBUFFER};
    CHECK(CALL3(primary, 12, &back_caps, &none) == DDERR_NOTFOUND && !none);
    fill_entries(7);
    void *palette = NULL;
    CHECK(CALL5(dd, 5, DDPCAPS_8BIT, dd_entries, &palette, NULL) == DD_OK && palette && ddw_uploads == 1);
    CHECK(CALL2(primary, 31, palette) == DD_OK && CALL2(primary, 31, (void *)primary) == DDERR_INVALIDOBJECT);
    CHECK(CALL1(primary, 24) == DD_OK && CALL1(primary, 27) == DD_OK);   // IsLost, Restore
    // Present (W >= 640 shape): Lock, copy, Unlock at 0x465870 inside 0x465628's frame.
    LONG pitch; BYTE *bits = dd_lock(primary, &pitch);
    DDSURFACEDESC again; memset(&again, 0, sizeof(again)); again.dwSize = sizeof(again);
    CHECK(pitch == 16 && CALL5(primary, 25, NULL, &again, DDLOCK_WAIT, NULL) == DDERR_SURFACEBUSY);
    dd_paint(bits, pitch, 1);
    set_dd_present(primary, FALSE);
    present_from(SITE_MAIN_CALL);
    CHECK(ddw_presents == 1 && draw_calls == 1 && dd_dc_gets == 1 && dd_dc_releases == 1 && blit_count == 0);
    expect_dd_image(bits, pitch, SCREEN_W, SCREEN_H, 0, 0, SCREEN_W, SCREEN_H);
    CHECK(CALL2(primary, 32, NULL) == DDERR_NOTLOCKED);
    // A larger client: scaled 2x with borders; a palette change through SetEntries.
    dd_client = (RECT){0, 0, 36, 30};
    fill_entries(9);
    CHECK(CALL5(palette, 6, 0, 0, 256, dd_entries) == DD_OK && ddw_uploads == 2);
    CHECK(CALL5(palette, 6, 0, 200, 57, dd_entries) == DDERR_INVALIDPARAMS);
    put32(PR_FRAME_RVA, 41);
    bits = dd_lock(primary, &pitch); dd_paint(bits, pitch, 2);
    present_from(SITE_MAIN_CALL);
    expect_dd_image(bits, pitch, 36, 30, 2, 3, 32, 24);
    // The clear's Unlock presents (black) but is not recorded.
    bits = dd_lock(primary, &pitch); memset(bits, 0, (size_t)pitch * SCREEN_H);
    dd_clear(primary, FALSE);
    CHECK(ddw_presents == 3 && ddw_present_clears == 1 && draw_calls == 3);
    // An unsupported method pops its arguments (Blt: six words) and answers DDERR_UNSUPPORTED.
    CHECK(CALL6(primary, 5, 0, 0, 0, 0, 0) == DDERR_UNSUPPORTED && ddw_unsupported == 1);
    CHECK(CALL1(primary, 2) == 0);   // Release the primary (and its palette reference)
    // Low resolution: a flipping primary with one back buffer (0x4665c4 for W < 640).
    memset(&d, 0, sizeof(d)); d.dwSize = 0x6c; d.dwFlags = DDSD_BACKBUFFERCOUNT; d.dwBackBufferCount = 1; d.ddsCaps.dwCaps = 0x4200 | 0x200018;
    CHECK(CALL4(dd, 6, &d, &primary, NULL) == DD_OK);
    void *back = NULL;
    CHECK(CALL3(primary, 12, &back_caps, &back) == DD_OK && back);
    CHECK(CALL2(back, 14, &caps) == DD_OK && (caps.dwCaps & DDSCAPS_BACKBUFFER) && CALL3(back, 11, NULL, DDFLIP_WAIT) == DDERR_NOTFLIPPABLE);
    CHECK(CALL2(primary, 31, palette) == DD_OK);
    dd_client = (RECT){0, 0, SCREEN_W, SCREEN_H};
    put32(PR_FRAME_RVA, 42);
    bits = dd_lock(back, &pitch); dd_paint(bits, pitch, 3);
    CHECK(CALL2(back, 32, NULL) == DD_OK && draw_calls == 3);   // a back-buffer Unlock does not present
    LONG front_pitch; (void)dd_lock(primary, &front_pitch);
    CHECK(CALL2(primary, 32, NULL) == DD_OK && draw_calls == 3);   // nor does the Unlock of a flipping primary
    set_dd_present(primary, TRUE);
    present_from(SITE_MAIN_CALL);
    CHECK(ddw_presents == 4 && draw_calls == 4);
    expect_dd_image(bits, pitch, SCREEN_W, SCREEN_H, 0, 0, SCREEN_W, SCREEN_H);
    dd_clear(primary, TRUE);
    CHECK(ddw_presents == 5 && ddw_present_clears == 2);
    // Teardown as 0x466730: back buffer, primary, palette, DirectDraw.
    CHECK(CALL1(back, 2) == 1 && CALL1(primary, 2) == 0 && CALL1(palette, 2) == 0 && CALL1(dd, 2) == 0);
    CHECK(ddw_objects == 5 && ddw_released == 5);
    CHECK(ddw_no_window == 0 && ddw_no_palette == 0 && ddw_draw_failures == 0);
    // The capture: three DirectDraw presents (frames 40, 41, 42), clears not recorded.
    presenter_shutdown();
    char *text = read_text(L"dd.jsonl", NULL);
    CHECK(text && line_count(text) == 5);
    char line[1024], want[1024], index[65], pal[65];
    BYTE rgb[768];
    fill_entries(9);
    for (unsigned i = 0; i < 256; ++i) { rgb[3 * i] = dd_entries[i].peRed; rgb[3 * i + 1] = dd_entries[i].peGreen; rgb[3 * i + 2] = dd_entries[i].peBlue; }
    crypto_sha256(rgb, sizeof(rgb), pal);
    BYTE frame[SCREEN_W * SCREEN_H];
    dd_paint(frame, SCREEN_W, 3);
    crypto_sha256(frame, sizeof(frame), index);
    snprintf(want, sizeof(want), "{\"frame\":42,\"site\":\"0x45fbe4\",\"ordinal\":0,\"mode\":1,\"S\":[0,0],\"R\":[0,0,15,11],"
        "\"client\":[0,0,16,12],\"src\":[0,0,16,12],\"dst\":[0,0,16,12],\"source\":\"surface\",\"index_sha256\":\"%s\","
        "\"palette_sha256\":\"%s\",\"uploads\":2,\"presented\":\"windowed\"}", index, pal);
    CHECK(nth_line(text, 3, line, sizeof(line)));
    if (strcmp(line, want)) { fprintf(stderr, " got %s\nwant %s\n", line, want); exit(1); }
    CHECK(nth_line(text, 2, line, sizeof(line)) && strstr(line, "\"frame\":41,") && strstr(line, "\"client\":[0,0,36,30]"));
    CHECK(nth_line(text, 1, line, sizeof(line)) && strstr(line, "\"frame\":40,") && strstr(line, "\"uploads\":1,"));
    CHECK(!strstr(text, ",\"qpc\":"));   // without capture_qpc: schema 1, no stamp
    free(text);
}
// Without the key nothing is replaced; an unknown value is reported.
static void mode_ddraw_off(void) {
    const WCHAR *pairs[] = {L"presentation", L"presenter", L"gdi", L"presentation", L"directdraw", L"exclusive", NULL};
    write_ini(L"presentation.ini", pairs);
    set_env_ini(L"ES2_PRESENTATION_CONFIG", L"presentation.ini");
    install_all(TRUE);
    create_game_window();
    CHECK(!ddw_requested() && !strcmp(last_event, "presentation_directdraw_unknown"));
    const WCHAR *off[] = {L"presentation", L"presenter", L"gdi", NULL};
    write_ini(L"presentation.ini", off);
    events = 0;
    CHECK(!ddw_requested() && events == 0);
}

// ES2_DATA_ROOT's DBSIM.EXE (skipped when unset): every checked byte, the import names of the
// two slots, and that the four GDI call sites are the only references to the two thunks and slots.
static DWORD u32(const BYTE *p) { DWORD v; memcpy(&v, p, 4); return v; }
static void mode_reference(void) {
    const char *root = getenv("ES2_DATA_ROOT");
    if (!root || !*root) { puts("SKIP: ES2_DATA_ROOT unset"); return; }
    char path[1024];
    snprintf(path, sizeof(path), "%s/VER95/DBSIM.EXE", root);
    FILE *f = fopen(path, "rb");
    CHECK(f);
    fseek(f, 0, SEEK_END); long size = ftell(f); rewind(f);
    CHECK(size == 724512);
    BYTE *bytes = malloc((size_t)size);
    CHECK(bytes && fread(bytes, 1, (size_t)size, f) == (size_t)size);
    fclose(f);
    DWORD pe = u32(bytes + 0x3c);
    CHECK(u32(bytes + pe + 24 + 56) == IJ_IMAGE_SIZE && u32(bytes + pe + 24 + 28) == PR_REFERENCE_BASE);
    unsigned sections = bytes[pe + 6] | bytes[pe + 7] << 8, optional = bytes[pe + 20] | bytes[pe + 21] << 8;
    BYTE *map = calloc(1, IJ_IMAGE_SIZE);
    CHECK(map);
    memcpy(map, bytes, u32(bytes + pe + 24 + 60));
    DWORD code_rva = 0, code_size = 0;
    for (unsigned i = 0; i < sections; ++i) {
        const BYTE *h = bytes + pe + 24 + optional + 40 * i;
        DWORD rva = u32(h + 12), raw = u32(h + 16), offset = u32(h + 20);
        if (raw && rva < IJ_IMAGE_SIZE) memcpy(map + rva, bytes + offset, raw < IJ_IMAGE_SIZE - rva ? raw : IJ_IMAGE_SIZE - rva);
        if (!code_size && (u32(h + 36) & 0x20u)) { code_rva = rva; code_size = raw; }
    }
    CHECK(code_size && pr_signatures_valid(map, PR_REFERENCE_BASE));
    CHECK(ws_thunk_valid(map, PR_REFERENCE_BASE, WS_CREATE_IAT_RVA, WS_CREATE_THUNK_RVA));
    // Import names of the slots (GDI32.dll FirstThunk entries; OriginalFirstThunk names).
    unsigned named = 0;
    for (DWORD d = u32(map + pe + 24 + 104); u32(map + d + 12); d += 20) {
        if (_stricmp((const char *)map + u32(map + d + 12), "GDI32.dll")) continue;
        DWORD names = u32(map + d), slots = u32(map + d + 16);
        for (DWORD j = 0; u32(map + names + 4 * j); ++j) {
            const char *name = (const char *)map + u32(map + names + 4 * j) + 2;
            if (slots + 4 * j == PR_STRETCH_IAT_RVA) { CHECK(!strcmp(name, "StretchBlt")); ++named; }
            if (slots + 4 * j == PR_TABLE_IAT_RVA) { CHECK(!strcmp(name, "SetDIBColorTable")); ++named; }
        }
    }
    CHECK(named == 2);
    unsigned stretch = 0, table = 0, present = 0, slot_refs = 0;
    for (DWORD r = code_rva; r + 5 <= code_rva + code_size; ++r) {
        DWORD target = r + 5 + u32(map + r + 1);
        if (map[r] == 0xe8 || map[r] == 0xe9) {
            if (target == PR_STRETCH_THUNK_RVA) { CHECK(map[r] == 0xe8 && (r == 0x000659adu || r == 0x000655e3u)); ++stretch; }
            if (target == PR_TABLE_THUNK_RVA) { CHECK(map[r] == 0xe8 && (r == 0x00065de5u || r == 0x00065f00u)); ++table; }
            if (target == 0x00064a14u && map[r] == 0xe8) ++present;
        }
        DWORD v = u32(map + r);
        if (v == PR_REFERENCE_BASE + PR_STRETCH_IAT_RVA || v == PR_REFERENCE_BASE + PR_TABLE_IAT_RVA) {
            CHECK(r == PR_STRETCH_THUNK_RVA + 2 || r == PR_TABLE_THUNK_RVA + 2); ++slot_refs;
        }
    }
    CHECK(stretch == 2 && table == 2 && slot_refs == 2 && present == 29);
    // Every row of the own callers' table follows a `call 0x464a14` (e8 rel32) inside CODE
    // and its entry is the replaced function's prologue (push ebp; mov ebp, esp).
    pr_image = map;
    for (unsigned i = 0; i < PR_OWN_CALLER_COUNT; ++i) {
        DWORD entry = pr_own_present_callers[i].entry - PR_REFERENCE_BASE, site = pr_own_present_callers[i].site - PR_REFERENCE_BASE;
        CHECK(pr_present_return((uintptr_t)map + site) && entry < site && map[entry] == 0x55 && map[entry + 1] == 0x8b && map[entry + 2] == 0xec);
    }
    CHECK(PR_CODE_LO_RVA == code_rva && PR_CODE_HI_RVA == code_rva + ((code_size + 0xfffu) & ~0xfffu));
    pr_image = NULL;
    printf("reference: signatures, slot names and the call sites hold (29 calls of 0x464a14)\n");
    free(map); free(bytes);
}

// capture_regions=1 adds the display mode and the region hashes
// (frame_regions.h) to every hashed record and the table id to the start line.
// The expected hashes come from a per-pixel classification (the first
// rectangle of the mode's list that holds the pixel) hashed with CryptoAPI.
static void expected_regions(int32_t mode, LONG sx, LONG sy, char *out, size_t size) {
    size_t count = 0;
    const fr_rect *table = fr_table(mode, &count);
    char hashes[FR_REGION_COUNT][FR_HASH_DIGITS + 1];
    for (int region = 0; region < FR_REGION_COUNT; ++region) {
        BYTE pixels[SCREEN_W * SCREEN_H]; DWORD n = 0;
        for (int y = 0; y < SCREEN_H; ++y) for (int x = 0; x < SCREEN_W; ++x) {
            int owner = FR_REST;
            for (size_t k = 0; k < count; ++k) {
                const fr_rect *r = &table[k];
                if (x >= r->left * SCREEN_W / 640 && x < r->right * SCREEN_W / 640 &&
                    y >= r->top * SCREEN_H / 480 && y < r->bottom * SCREEN_H / 480) { owner = r->region; break; }
            }
            if (owner == region) pixels[n++] = dib[(size_t)(sy + y) * DIB_W + (size_t)(sx + x)];
        }
        hashes[region][0] = 0;
        if (n) { char hex[65]; crypto_sha256(pixels, n, hex); memcpy(hashes[region], hex, 16); hashes[region][16] = 0; }
    }
    int k = snprintf(out, size, ",\"display\":%ld,\"regions\":{", (long)mode);
    for (int region = 0; region < FR_REGION_COUNT; ++region)
        k += hashes[region][0] ? snprintf(out + k, size - (size_t)k, "%s\"%s\":\"%s\"", region ? "," : "", fr_region_names[region], hashes[region])
                               : snprintf(out + k, size - (size_t)k, "%s\"%s\":null", region ? "," : "", fr_region_names[region]);
    snprintf(out + k, size - (size_t)k, "}");
}
static void mode_regions(void) {
    WCHAR report[MAX_PATH]; path_in_tmp(report, L"regions.ndjson");
    const WCHAR *pairs[] = {L"presentation", L"capture", L"1", L"presentation", L"capture_report", report,
                            L"presentation", L"capture_regions", L"1", NULL};
    write_ini(L"presentation.ini", pairs);
    set_env_ini(L"ES2_PRESENTATION_CONFIG", L"presentation.ini");
    install_all(TRUE);
    fill_dib(1); fill_quads(3); set_game(1, 4, 2);
    create_game_window();
    CHECK(patched() && pr_capture && pr_regions && !strcmp(last_event, "presentation_capture"));
    HDC window_dc = (HDC)(uintptr_t)0x7777, source = (HDC)(uintptr_t)DIB_DC;
    CHECK(upload(source) == 256);
    static DWORD display[8];   // the display object D; D+0x14 the mode
    static const int32_t modes[] = {-1, 0, 1, 2, 3, 4, 7};
    for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        put32(PR_FRAME_RVA, 1 + i);
        fill_dib(1 + i);
        put32(FR_DISPLAY_VA - PR_REFERENCE_BASE, modes[i] < 0 ? 0 : (DWORD)(uintptr_t)display);
        display[FR_DISPLAY_MODE_OFFSET / 4] = (DWORD)(modes[i] < 0 ? 0 : modes[i]);
        set_present(SCREEN_W, SCREEN_H, window_dc, 0, 0, SCREEN_W, SCREEN_H, source, 4, 2, SCREEN_W, SCREEN_H, SRCCOPY);
        present_from(SITE_MAIN_CALL);
    }
    // A present without a hash carries no region fields.
    set_present(SCREEN_W, SCREEN_H, window_dc, 0, 0, SCREEN_W, SCREEN_H, source, DIB_W - SCREEN_W + 1, 0, SCREEN_W, SCREEN_H, SRCCOPY);
    present_from(SITE_MAIN_CALL);
    presenter_shutdown();
    char *text = read_text(L"regions.ndjson", NULL);
    char line[1024], want[512];
    CHECK(text && nth_line(text, 0, line, sizeof(line)));
    snprintf(want, sizeof(want), "{\"event\":\"start\",\"schema\":1,\"key\":\"frame_counter_0x4d257b\",\"presenter\":\"original\","
             "\"dump_frames\":0,\"pid\":%lu,\"regions\":\"t66-1\"}", (unsigned long)GetCurrentProcessId());
    if (strcmp(line, want)) { fprintf(stderr, " got %s\nwant %s\n", line, want); exit(1); }
    CHECK(line_count(text) == 1 + 8 + 1);
    for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        fill_dib(1 + i);
        char fields[300];
        expected_regions(modes[i] < 0 ? -1 : modes[i], 4, 2, fields, sizeof(fields));
        CHECK(nth_line(text, 1 + i, line, sizeof(line)));
        size_t n = strlen(line), m = strlen(fields);
        if (n < m + 1 || strncmp(line + n - m - 1, fields, m) || line[n - 1] != '}' || !strstr(line, "\"index_sha256\":\"")) {
            fprintf(stderr, "record %u\n got %s\nwant ...%s}\n", i, line, fields); exit(1);
        }
    }
    CHECK(nth_line(text, 8, line, sizeof(line)) && strstr(line, "\"index_sha256\":null") && !strstr(line, "\"regions\""));
    // Mode 0 at 16 x 12: the scaled table leaves pixels in every region but rest.
    CHECK(nth_line(text, 2, line, sizeof(line)) && strstr(line, "\"display\":0,") && strstr(line, "\"rest\":null}"));
    // Without D every pixel is rest.
    CHECK(nth_line(text, 1, line, sizeof(line)) && strstr(line, "\"display\":-1,\"regions\":{\"view\":null,\"hud\":null,\"panel\":null,\"mfd\":null,\"rest\":\""));
    free(text);
}

// Presents that own code calls (presenter.h pr_present_site, present_site.h). A stub
// outside the image, at a high address as the proxy DLL's bridge, calls 0x464a14 (`call rel32;
// ret`): the slot [ebp+0x10] of 0x465628's frame then holds the stub's return address. The
// image carries the mission load's `call 0x464a14` at 0x461e1c (site 0x461e21) and, at 0x461e30,
// a call of something else.
#define OWN_ROOT_CALL 0x00061e1cu      // 0x461e1c call 0x464a14 -> site 0x461e21
#define OWN_OTHER_CALL 0x00061e30u     // a call of 0x45fca8, not of the present
static BYTE *own_stub;
static void present_from_stub(void) {
    static BYTE before[0x6000];
    memcpy(before, image + 0xd0000u, sizeof(before));
    ((void (*)(void))(void *)own_stub)();
    CHECK(!memcmp(before, image + 0xd0000u, sizeof(before)));
}
static void mode_own_site(void) {
    WCHAR report[MAX_PATH]; path_in_tmp(report, L"own-site.ndjson");
    const WCHAR *pairs[] = {L"presentation", L"capture", L"1", L"presentation", L"capture_report", report, NULL};
    write_ini(L"presentation.ini", pairs);
    set_env_ini(L"ES2_PRESENTATION_CONFIG", L"presentation.ini");
    install_all(TRUE);
    fill_dib(1); fill_quads(3); set_game(1, 4, 2);
    create_game_window();
    CHECK(patched() && pr_capture);
    put_rel_call(OWN_ROOT_CALL, 0xe8, 0x00064a14u); image[OWN_ROOT_CALL + 5] = 0xc3;
    put_rel_call(OWN_OTHER_CALL, 0xe8, 0x0005fca8u); image[OWN_OTHER_CALL + 5] = 0xc3;
    own_stub = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE | MEM_TOP_DOWN, PAGE_EXECUTE_READWRITE);
    CHECK(own_stub && (own_stub < image || own_stub >= image + IJ_IMAGE_SIZE));
    own_stub[0] = 0xe8; DWORD rel = (DWORD)(uintptr_t)(image + 0x00064a14u) - (DWORD)(uintptr_t)(own_stub + 5);
    memcpy(own_stub + 1, &rel, 4); own_stub[5] = 0xc3;
    const DWORD raw = (DWORD)(uintptr_t)(own_stub + 5);
    char raw_site[16]; snprintf(raw_site, sizeof(raw_site), "0x%06lx", (unsigned long)raw);
    CHECK(strlen(raw_site) == 10);   // eight digits, not a six-digit image VA: dbsim_frames.py refuses it ("bad site")
    const uint32_t present = (uint32_t)(uintptr_t)(image + 0x00064a14u), b = (uint32_t)(uintptr_t)image;
    HDC window_dc = (HDC)(uintptr_t)0x7777, source = (HDC)(uintptr_t)DIB_DC;
    CHECK(upload(source) == 256);
    set_present(SCREEN_W, SCREEN_H, window_dc, 0, 0, SCREEN_W, SCREEN_H, source, 4, 2, SCREEN_W, SCREEN_H, SRCCOPY);
    CHECK(es2_present_site_current.target == 0);

    // 1. Announced by the replaced root with the core's return address: the original's site.
    es2_present_site_call outer = es2_present_site_begin(present, 0x00461608u, b + 0x00061e21u);
    present_from_stub();
    es2_present_site_end(outer);
    CHECK(es2_present_site_current.target == 0 && pr_announced_sites == 1 && pr_unattributed_sites == 0);
    // 2. Without a return address: the root's row of the table.
    outer = es2_present_site_begin(present, 0x00461608u, 0);
    present_from_stub();
    es2_present_site_end(outer);
    // 3. A CODE return address stays unchanged, also while own code's present call is announced.
    outer = es2_present_site_begin(present, 0x00461608u, b + 0x00061e21u);
    present_from(SITE_MODAL_CALL);
    es2_present_site_end(outer);
    present_from(SITE_MAIN_CALL);
    CHECK(pr_announced_sites == 2 && pr_unattributed_sites == 0);
    // 4. Unattributed, the raw address kept and counted: no announcement; an announced call of
    // another callee; a return address that does not follow `call 0x464a14` and an unknown caller;
    // a caller of the table whose site does not hold the call (the bytes are checked).
    present_from_stub();
    outer = es2_present_site_begin(b + 0x0005fca8u, 0x00461608u, b + 0x00061e21u);
    present_from_stub();
    es2_present_site_end(outer);
    outer = es2_present_site_begin(present, 0x00401000u, b + OWN_OTHER_CALL + 5u);
    present_from_stub();
    es2_present_site_end(outer);
    image[OWN_ROOT_CALL] = 0x90;
    outer = es2_present_site_begin(present, 0x00461608u, 0);
    present_from_stub();
    es2_present_site_end(outer);
    image[OWN_ROOT_CALL] = 0xe8;
    CHECK(pr_announced_sites == 2 && pr_unattributed_sites == 4);
    // Nesting: end restores the announcement that was current before begin.
    outer = es2_present_site_begin(b + 0x0005fca8u, 0x00461608u, b + 0x00061e35u);
    es2_present_site_call inner = es2_present_site_begin(present, 0x00461608u, b + 0x00061e21u);
    present_from_stub();
    es2_present_site_end(inner);
    CHECK(es2_present_site_current.target == b + 0x0005fca8u && es2_present_site_current.ret == b + 0x00061e35u);
    present_from_stub();   // the outer announcement names another callee: unattributed
    es2_present_site_end(outer);
    CHECK(es2_present_site_current.target == 0 && pr_announced_sites == 3 && pr_unattributed_sites == 5);
    presenter_shutdown();

    char *text = read_text(L"own-site.ndjson", NULL);
    CHECK(text && line_count(text) == 1 + 10 + 1);   // start, ten records, stats
    const char *sites[] = {"0x461e21", "0x461e21", "0x45490b", "0x45fbe4", raw_site, raw_site, raw_site, raw_site,
                           "0x461e21", raw_site, NULL};
    char line[1024], want[64];
    for (unsigned i = 0; sites[i]; ++i) {
        snprintf(want, sizeof(want), "\"site\":\"%s\",", sites[i]);
        CHECK(nth_line(text, 1 + i, line, sizeof(line)));
        if (!strstr(line, want)) { fprintf(stderr, "record %u\n got %s\nwant %s\n", i, line, want); exit(1); }
    }
    CHECK(nth_line(text, 9, line, sizeof(line)) && strstr(line, "\"site\":\"0x461e21\",\"ordinal\":2,"));
    CHECK(nth_line(text, 11, line, sizeof(line)) && strstr(line, "\"records\":10,") &&
          strstr(line, "\"announced_sites\":3,\"unattributed_sites\":5}"));
    free(text);
}

// ---- capture_qpc=1 ----------------------------------------------------------------
static uint64_t qpc_now(void) { LARGE_INTEGER n; QueryPerformanceCounter(&n); return (uint64_t)n.QuadPart; }
static LONGLONG qpc_frequency(void) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f.QuadPart; }
// Removes `,"qpc":N` from every line of `text` in place (what is left is the schema-1 form of the same
// lines) and stores N per line index (0 for a line without one). Returns the number of lines that had one.
static unsigned strip_qpc(char *text, uint64_t *values, unsigned capacity) {
    unsigned found = 0, index = 0;
    char *line = text;
    while (*line) {
        char *end = strstr(line, "\r\n");
        CHECK(end != NULL);
        *end = 0;
        char *field = strstr(line, ",\"qpc\":");
        *end = '\r';
        if (field) {
            char *digits = field + 7, *stop;
            unsigned long long v = _strtoui64(digits, &stop, 10);
            CHECK(stop > digits && stop <= end);
            if (index < capacity) values[index] = v;
            memmove(field, stop, strlen(stop) + 1);
            end -= (stop - field);
            ++found;
        } else if (index < capacity) values[index] = 0;
        line = end + 2;
        ++index;
    }
    return found;
}

// The time stamp is the counter on entry to the present: with the own presenter on and a draw that takes
// 12 ms, a stamp taken when the record is written would lie after the draw; the entry stamp lies before it.
static void mode_qpc(void) {
    WCHAR report[MAX_PATH]; path_in_tmp(report, L"qpc.ndjson");
    const WCHAR *pairs[] = {L"presentation", L"capture", L"1", L"presentation", L"capture_report", report,
                            L"presentation", L"capture_qpc", L"1", L"presentation", L"presenter", L"gdi", NULL};
    write_ini(L"presentation.ini", pairs);
    set_env_ini(L"ES2_PRESENTATION_CONFIG", L"presentation.ini");
    install_all(TRUE);
    fill_dib(1); fill_quads(3); set_game(1, 4, 2);
    create_game_window();
    CHECK(patched() && pr_capture && pr_qpc && pr_present_gdi && !strcmp(last_event, "presentation_capture_gdi"));
    pr_stretch_dibits = fake_draw;
    draw_sleep_ms = 12;
    HDC window_dc = (HDC)(uintptr_t)0x7777, source = (HDC)(uintptr_t)DIB_DC;
    CHECK(upload(source) == 256);
    char palette[65]; palette_sha(palette);
    char hashes[3][70];
    uint64_t before[3], after[3];
    for (unsigned i = 0; i < 3; ++i) {
        put32(PR_FRAME_RVA, 1 + i);
        fill_dib(1 + i);
        char hex[65]; frame_sha(4, 2, SCREEN_W, SCREEN_H, hex); quoted(hashes[i], hex);
        set_present(SCREEN_W, SCREEN_H, window_dc, 0, 0, SCREEN_W, SCREEN_H, source, 4, 2, SCREEN_W, SCREEN_H, SRCCOPY);
        before[i] = qpc_now();
        present_from(SITE_MAIN_CALL);
        after[i] = qpc_now();
    }
    CHECK(draw_calls == 3);
    presenter_shutdown();
    char *text = read_text(L"qpc.ndjson", NULL);
    CHECK(text);
    char start[320], palette_quoted[70];
    quoted(palette_quoted, palette);
    uint64_t values[8] = {0};
    CHECK(strip_qpc(text, values, 8) == 3);   // the three records, no other line
    CHECK(line_count(text) == 5);              // start, three records, stats
    char line[1024];
    CHECK(nth_line(text, 0, line, sizeof(line)));
    // The start line is schema 2 and ends with the counter's rate (it is the only place the rate is in the file besides stats).
    snprintf(start, sizeof(start), "{\"event\":\"start\",\"schema\":2,\"key\":\"frame_counter_0x4d257b\",\"presenter\":\"gdi\","
             "\"dump_frames\":0,\"pid\":%lu,\"qpc_frequency\":%lld}", (unsigned long)GetCurrentProcessId(), (long long)qpc_frequency());
    if (strcmp(line, start)) { fprintf(stderr, " got %s\nwant %s\n", line, start); exit(1); }
    // Apart from the stamp the records are the ones without capture_qpc.
    for (unsigned i = 0; i < 3; ++i)
        expect_record(text, 1 + i, 1 + i, "0x45fbe4", 0, 4, 2, SCREEN_W, SCREEN_H, "dib", hashes[i], palette_quoted, 1, "gdi");
    // The stamp lies between the caller's reads, before the draw's entry (not after it) and a sleep away from the end.
    LONGLONG ten_ms = qpc_frequency() / 100;
    for (unsigned i = 0; i < 3; ++i) {
        uint64_t stamp = values[1 + i];
        if (!(stamp >= before[i] && stamp <= draw_qpc_log[i] && draw_qpc_log[i] <= after[i] && after[i] - stamp >= (uint64_t)ten_ms)) {
            fprintf(stderr, "record %u: before %llu stamp %llu draw %llu after %llu\n", i, (unsigned long long)before[i],
                    (unsigned long long)stamp, (unsigned long long)draw_qpc_log[i], (unsigned long long)after[i]);
            exit(1);
        }
    }
    CHECK(values[1] < values[2] && values[2] < values[3] && values[2] - values[1] >= (uint64_t)ten_ms);
    free(text);
}

// capture_qpc=1 with the other keys: the stamp ends every record after the region fields, the rate ends the
// start line after the range and the table id; a record written without an entry stamp gets the counter at
// the write.
static void mode_qpc_all(void) {
    WCHAR report[MAX_PATH]; path_in_tmp(report, L"qpc-all.ndjson");
    const WCHAR *pairs[] = {L"presentation", L"capture", L"1", L"presentation", L"capture_report", report,
                            L"presentation", L"capture_regions", L"1", L"presentation", L"capture_dump_range", L"2-3",
                            L"presentation", L"capture_qpc", L"2", NULL};   // any nonzero value
    write_ini(L"presentation.ini", pairs);
    set_env_ini(L"ES2_PRESENTATION_CONFIG", L"presentation.ini");
    install_all(TRUE);
    fill_dib(1); fill_quads(3); set_game(1, 4, 2);
    create_game_window();
    CHECK(patched() && pr_capture && pr_qpc && pr_regions && pr_range_set);
    HDC window_dc = (HDC)(uintptr_t)0x7777, source = (HDC)(uintptr_t)DIB_DC;
    CHECK(upload(source) == 256);
    static DWORD display[8];
    put32(FR_DISPLAY_VA - PR_REFERENCE_BASE, (DWORD)(uintptr_t)display);
    display[FR_DISPLAY_MODE_OFFSET / 4] = 1;
    for (DWORD f = 1; f <= 3; ++f) {
        put32(PR_FRAME_RVA, f);
        fill_dib(f);
        set_present(SCREEN_W, SCREEN_H, window_dc, 0, 0, SCREEN_W, SCREEN_H, source, 4, 2, SCREEN_W, SCREEN_H, SRCCOPY);
        present_from(SITE_MAIN_CALL);
    }
    pr_record_fields r;
    memset(&r, 0, sizeof(r));
    r.frame = 4; r.site = 0x45fbe4u; r.source = "surface"; r.index_hex = ""; r.palette_hex = ""; r.outcome = "windowed";
    uint64_t low = qpc_now();
    pr_write_record(&r);
    uint64_t high = qpc_now();
    presenter_shutdown();
    char *text = read_text(L"qpc-all.ndjson", NULL);
    CHECK(text);
    uint64_t values[8] = {0};
    CHECK(strip_qpc(text, values, 8) == 4);
    CHECK(line_count(text) == 1 + 4 + 1);
    char line[1024], want[512];
    CHECK(nth_line(text, 0, line, sizeof(line)));
    snprintf(want, sizeof(want), "{\"event\":\"start\",\"schema\":2,\"key\":\"frame_counter_0x4d257b\",\"presenter\":\"original\","
             "\"dump_frames\":0,\"pid\":%lu,\"dump_range\":[2,3,1],\"regions\":\"t66-1\",\"qpc_frequency\":%lld}",
             (unsigned long)GetCurrentProcessId(), (long long)qpc_frequency());
    if (strcmp(line, want)) { fprintf(stderr, " got %s\nwant %s\n", line, want); exit(1); }
    for (DWORD f = 1; f <= 3; ++f) {   // what is left is the capture_regions record: the region fields close it
        fill_dib(f);
        char fields[300];
        expected_regions(1, 4, 2, fields, sizeof(fields));
        CHECK(nth_line(text, f, line, sizeof(line)));
        size_t n = strlen(line), m = strlen(fields);
        if (n < m + 1 || strncmp(line + n - m - 1, fields, m) || line[n - 1] != '}') {
            fprintf(stderr, "record %lu\n got %s\nwant ...%s}\n", (unsigned long)f, line, fields); exit(1);
        }
        CHECK(values[f] != 0 && (f == 1 || values[f] > values[f - 1]));
    }
    CHECK(values[4] >= low && values[4] <= high);   // no stamp of its own: the counter at the write
    CHECK(nth_line(text, 4, line, sizeof(line)) && strstr(line, "\"frame\":4,") && strstr(line, "\"source\":\"surface\""));
    free(text);
}

// capture_qpc=0 is the absent key: schema 1, no stamp.
static void mode_qpc_off(void) {
    WCHAR report[MAX_PATH]; path_in_tmp(report, L"qpc-off.ndjson");
    const WCHAR *pairs[] = {L"presentation", L"capture", L"1", L"presentation", L"capture_report", report,
                            L"presentation", L"capture_qpc", L"0", NULL};
    write_ini(L"presentation.ini", pairs);
    set_env_ini(L"ES2_PRESENTATION_CONFIG", L"presentation.ini");
    install_all(TRUE);
    fill_dib(1); fill_quads(3); set_game(1, 4, 2);
    create_game_window();
    CHECK(patched() && pr_capture && !pr_qpc);
    HDC window_dc = (HDC)(uintptr_t)0x7777, source = (HDC)(uintptr_t)DIB_DC;
    set_present(SCREEN_W, SCREEN_H, window_dc, 0, 0, SCREEN_W, SCREEN_H, source, 4, 2, SCREEN_W, SCREEN_H, SRCCOPY);
    present_from(SITE_MAIN_CALL);
    presenter_shutdown();
    char *text = read_text(L"qpc-off.ndjson", NULL);
    CHECK(text && line_count(text) == 3);
    char line[1024], want[320], q1[70];
    CHECK(nth_line(text, 0, line, sizeof(line)));
    snprintf(want, sizeof(want), "{\"event\":\"start\",\"schema\":1,\"key\":\"frame_counter_0x4d257b\",\"presenter\":\"original\","
             "\"dump_frames\":0,\"pid\":%lu}", (unsigned long)GetCurrentProcessId());
    CHECK(!strcmp(line, want));
    char f1[65]; frame_sha(4, 2, SCREEN_W, SCREEN_H, f1); quoted(q1, f1);
    expect_record(text, 1, 1, "0x45fbe4", 0, 4, 2, SCREEN_W, SCREEN_H, "dib", q1, "null", 0, "original");
    CHECK(!strstr(text, ",\"qpc\":"));   // no stamp (the stats line still names its rate, as before)
    free(text);
}

// The DirectDraw branch: the stamp of an Unlock and of a Flip present is the counter on entry to the present,
// before the windowed draw (12 ms here) and the hash; the clears are drawn but neither recorded nor stamped.
static void mode_qpc_ddraw(void) {
    WCHAR report[MAX_PATH]; path_in_tmp(report, L"qpc-dd.ndjson");
    const WCHAR *pairs[] = {L"presentation", L"capture", L"1", L"presentation", L"capture_report", report,
                            L"presentation", L"capture_qpc", L"1", L"presentation", L"directdraw", L"windowed", NULL};
    write_ini(L"presentation.ini", pairs);
    set_env_ini(L"ES2_PRESENTATION_CONFIG", L"presentation.ini");
    install_all(TRUE);
    set_game(40, 0, 0); put32(PR_MODE_RVA, 1);
    create_game_window();
    CHECK(patched() && pr_capture && pr_qpc && ddw_requested());
    pr_stretch_dibits = fake_draw; ddw_get_dc = fake_get_dc; ddw_release_dc = fake_release_dc; ddw_client_rect = fake_client;
    draw_sleep_ms = 12;
    void *dd = NULL, *primary = NULL, *palette = NULL, *back = NULL;
    CHECK(ddw_create(NULL, (LPDIRECTDRAW *)&dd, NULL) == DD_OK && dd);
    CHECK(CALL3(dd, 20, (HWND)(uintptr_t)0x1234, 0x57) == DD_OK && CALL4(dd, 21, SCREEN_W, SCREEN_H, 8) == DD_OK);
    DDSURFACEDESC d; memset(&d, 0, sizeof(d)); d.dwSize = 0x6c; d.ddsCaps.dwCaps = 0x4200;
    CHECK(CALL4(dd, 6, &d, &primary, NULL) == DD_OK && primary);
    fill_entries(7);
    CHECK(CALL5(dd, 5, DDPCAPS_8BIT, dd_entries, &palette, NULL) == DD_OK && CALL2(primary, 31, palette) == DD_OK);
    uint64_t before[2], after[2];
    unsigned draw_index[2];
    // Unlock present (frame 40), then the clear's Unlock.
    LONG pitch; BYTE *bits = dd_lock(primary, &pitch); dd_paint(bits, pitch, 1);
    set_dd_present(primary, FALSE);
    draw_index[0] = draw_calls;
    before[0] = qpc_now(); present_from(SITE_MAIN_CALL); after[0] = qpc_now();
    bits = dd_lock(primary, &pitch); memset(bits, 0, (size_t)pitch * SCREEN_H);
    dd_clear(primary, FALSE);
    CHECK(CALL1(primary, 2) == 0);
    // Flip present (frame 41) of a flipping primary, then the clear's Flip.
    memset(&d, 0, sizeof(d)); d.dwSize = 0x6c; d.dwFlags = DDSD_BACKBUFFERCOUNT; d.dwBackBufferCount = 1; d.ddsCaps.dwCaps = 0x4200 | 0x200018;
    CHECK(CALL4(dd, 6, &d, &primary, NULL) == DD_OK);
    DDSCAPS back_caps = {DDSCAPS_BACKBUFFER};
    CHECK(CALL3(primary, 12, &back_caps, &back) == DD_OK && back && CALL2(primary, 31, palette) == DD_OK);
    put32(PR_FRAME_RVA, 41);
    bits = dd_lock(back, &pitch); dd_paint(bits, pitch, 3);
    CHECK(CALL2(back, 32, NULL) == DD_OK);
    set_dd_present(primary, TRUE);
    draw_index[1] = draw_calls;
    before[1] = qpc_now(); present_from(SITE_MAIN_CALL); after[1] = qpc_now();
    dd_clear(primary, TRUE);
    CHECK(ddw_presents == 4 && ddw_present_clears == 2 && draw_calls == 4 && ddw_draw_failures == 0);
    CHECK(CALL1(back, 2) == 1 && CALL1(primary, 2) == 0 && CALL1(palette, 2) == 0 && CALL1(dd, 2) == 0);
    presenter_shutdown();
    char *text = read_text(L"qpc-dd.ndjson", NULL);
    CHECK(text);
    uint64_t values[8] = {0};
    CHECK(strip_qpc(text, values, 8) == 2);   // the two presents; the clears are not recorded
    CHECK(line_count(text) == 4);              // start, two records, stats
    char line[1024], want[1024], index[65], pal[65];
    CHECK(nth_line(text, 0, line, sizeof(line)));
    snprintf(want, sizeof(want), "{\"event\":\"start\",\"schema\":2,\"key\":\"frame_counter_0x4d257b\",\"presenter\":\"original\","
             "\"dump_frames\":0,\"pid\":%lu,\"qpc_frequency\":%lld}", (unsigned long)GetCurrentProcessId(), (long long)qpc_frequency());
    if (strcmp(line, want)) { fprintf(stderr, " got %s\nwant %s\n", line, want); exit(1); }
    // Apart from the stamp the Flip record is the one of the DirectDraw capture without the key.
    BYTE rgb[768];
    for (unsigned i = 0; i < 256; ++i) { rgb[3 * i] = dd_entries[i].peRed; rgb[3 * i + 1] = dd_entries[i].peGreen; rgb[3 * i + 2] = dd_entries[i].peBlue; }
    crypto_sha256(rgb, sizeof(rgb), pal);
    BYTE frame[SCREEN_W * SCREEN_H];
    dd_paint(frame, SCREEN_W, 3);
    crypto_sha256(frame, sizeof(frame), index);
    snprintf(want, sizeof(want), "{\"frame\":41,\"site\":\"0x45fbe4\",\"ordinal\":0,\"mode\":1,\"S\":[0,0],\"R\":[0,0,15,11],"
        "\"client\":[0,0,16,12],\"src\":[0,0,16,12],\"dst\":[0,0,16,12],\"source\":\"surface\",\"index_sha256\":\"%s\","
        "\"palette_sha256\":\"%s\",\"uploads\":1,\"presented\":\"windowed\"}", index, pal);
    CHECK(nth_line(text, 2, line, sizeof(line)));
    if (strcmp(line, want)) { fprintf(stderr, " got %s\nwant %s\n", line, want); exit(1); }
    CHECK(nth_line(text, 1, line, sizeof(line)) && strstr(line, "\"frame\":40,") && strstr(line, "\"presented\":\"windowed\"}"));
    // The stamp lies between the caller's reads, before the draw's entry and a sleep away from the end.
    LONGLONG ten_ms = qpc_frequency() / 100;
    for (unsigned i = 0; i < 2; ++i) {
        uint64_t stamp = values[1 + i], drawn = draw_qpc_log[draw_index[i]];
        if (!(stamp >= before[i] && stamp <= drawn && drawn <= after[i] && after[i] - stamp >= (uint64_t)ten_ms)) {
            fprintf(stderr, "record %u: before %llu stamp %llu draw %llu after %llu\n", i, (unsigned long long)before[i],
                    (unsigned long long)stamp, (unsigned long long)drawn, (unsigned long long)after[i]);
            exit(1);
        }
    }
    free(text);
}

int wmain(int argc, WCHAR **argv) {
    (void)ddw_enabled;
    (void)install_presenter; (void)install_window_shim; (void)install_input_journal; (void)input_journal_shutdown;
    if (argc != 2) return 2;
    DWORD n = GetEnvironmentVariableW(L"TEST_TMPDIR", tmp, MAX_PATH);
    if (!n || n >= MAX_PATH - 80) return 2;
    SetEnvironmentVariableW(L"ES2_PRESENTATION_CONFIG", NULL);
    SetEnvironmentVariableW(L"ES2_ORACLE_SPEED_CONFIG", NULL);
    path_in_tmp(config_path, L"es2-oracle.ini");   // [input] absent: journal off
    make_image();
    const WCHAR *mode = argv[1];
    if (!wcscmp(mode, L"capture")) mode_capture(FALSE);
    else if (!wcscmp(mode, L"capture_gdi")) mode_capture(TRUE);
    else if (!wcscmp(mode, L"presenter")) mode_presenter();
    else if (!wcscmp(mode, L"gdi_real")) mode_gdi_real();
    else if (!wcscmp(mode, L"beside")) mode_beside();
    else if (!wcscmp(mode, L"range")) mode_range();
    else if (!wcscmp(mode, L"range_refused")) mode_range_refused();
    else if (!wcscmp(mode, L"exists") || !wcscmp(mode, L"unknown") || !wcscmp(mode, L"noreport") || !wcscmp(mode, L"off")) mode_refused(mode);
    else if (!wcscmp(mode, L"exists_gdi")) mode_exists_gdi(FALSE);
    else if (!wcscmp(mode, L"unusable_gdi")) mode_exists_gdi(TRUE);
    else if (!wcscmp(mode, L"session")) mode_session();
    else if (!wcscmp(mode, L"session_child")) mode_session_child();
    else if (!wcscmp(mode, L"mismatch")) mode_mismatch();
    else if (!wcscmp(mode, L"journal_only")) mode_journal_only();
    else if (!wcscmp(mode, L"sha")) mode_sha();
    else if (!wcscmp(mode, L"scaler")) mode_scaler();
    else if (!wcscmp(mode, L"cost")) mode_cost();
    else if (!wcscmp(mode, L"reference")) mode_reference();
    else if (!wcscmp(mode, L"ddraw")) mode_ddraw();
    else if (!wcscmp(mode, L"ddraw_off")) mode_ddraw_off();
    else if (!wcscmp(mode, L"regions")) mode_regions();
    else if (!wcscmp(mode, L"own_site")) mode_own_site();
    else if (!wcscmp(mode, L"qpc")) mode_qpc();
    else if (!wcscmp(mode, L"qpc_all")) mode_qpc_all();
    else if (!wcscmp(mode, L"qpc_off")) mode_qpc_off();
    else if (!wcscmp(mode, L"qpc_ddraw")) mode_qpc_ddraw();
    else return 2;
    printf("PASS %ls\n", mode);
    return 0;
}
