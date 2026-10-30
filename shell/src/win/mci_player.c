/* The own MCI movie player; contract in
 * docs/reverse-engineering/vshell_ui.md section 19, interface in
 * mci_player.h. */
#include "mci_player.h"

#include "es2/core/rtl.h"
#include "es2/media/iv32_bind.h"

#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int es2_mci_player_path(const char *cd_root, int index, int language, char *out, size_t capacity) {
    char name[64];
    const char *tail = name;
    size_t root, n;
    if (!cd_root || !out || es2_shell_movie_path(index, language, name, sizeof name) != 0) return -1;
    /* Original: 0x40d9e1, strcpy(buffer, CD root), strcat(buffer, path + 2
     * when path[1] is ':') */
    if (name[0] && name[1] == ':') tail = name + 2;
    root = strlen(cd_root);
    n = strlen(tail);
    if (root + n >= capacity) return -1;
    memcpy(out, cd_root, root);
    memcpy(out + root, tail, n + 1);
    return 0;
}

static int floor_scale(int64_t value, int64_t size, int64_t full) {
    int64_t n = value * size, q = n / full;
    if (n % full != 0 && n < 0) q--;
    return (int)q;
}

es2_mci_rect es2_mci_player_place(const es2_mci_rect *frame, const es2_shell_movie_rect *rect) {
    es2_mci_rect f = {0, 0, 640, 480}, out = {0, 0, 0, 0};
    if (!rect) return out;
    if (frame && frame->width > 0 && frame->height > 0) f = *frame;
    int x0 = f.x + floor_scale(rect->x, f.width, 640);
    int y0 = f.y + floor_scale(rect->y, f.height, 480);
    int x1 = f.x + floor_scale((int64_t)rect->x + rect->width, f.width, 640);
    int y1 = f.y + floor_scale((int64_t)rect->y + rect->height, f.height, 480);
    out.x = x0;
    out.y = y0;
    out.width = x1 - x0;
    out.height = y1 - y0;
    return out;
}

/* The parent window or a window inside it (the MCI child window). */
static int in_parent(const es2_mci_player *p, HWND hwnd) {
    return hwnd && (hwnd == p->parent || IsChild(p->parent, hwnd));
}

/* mciGetErrorStringA into player->error, then the box the original shows */
static void error_box(es2_mci_player *p, MCIERROR code, HWND owner, const char *title, UINT length) {
    if (length > sizeof p->error) length = sizeof p->error;
    p->error[0] = 0;
    mciGetErrorStringA(code, p->error, length);
    MessageBoxA(owner, p->error, title, MB_ICONWARNING);   /* 0x30 */
}

/* 0x41e48c: a 236-entry palette from screen entries 10..245 */
static void build_palette(es2_mci_player *p) {
    struct { WORD version, count; PALETTEENTRY entry[236]; } logical;
    if (!p->palette) return;
    logical.version = 0x300;
    logical.count = 236;
    for (int i = 0; i < 236; i++) {
        logical.entry[i].peRed = p->palette[i][0];
        logical.entry[i].peGreen = p->palette[i][1];
        logical.entry[i].peBlue = p->palette[i][2];
        logical.entry[i].peFlags = p->palette[i][3];
    }
    HPALETTE created = CreatePalette((LOGPALETTE *)&logical);
    if (!created) return;
    if (p->palette_handle) DeleteObject(p->palette_handle);   /* VSHELL keeps the old one (no effect) */
    p->palette_handle = created;
}

/* 0x41e51b: the IV32 binding first, then "open avivideo" */
static int open_device(es2_mci_player *p, int with_palette) {
    if (!p->iv32_status) {
        es2_iv32_drivers drivers;
        p->iv32_status = es2_iv32_system_drivers(&drivers) ? (int)es2_iv32_bind(NULL, &drivers)
                                                            : (int)ES2_IV32_BAD_ARGUMENT;
    }
    if (with_palette) build_palette(p);
    return mciSendStringA(es2_shell_mci_open_device, NULL, 0, NULL) == 0;
}

/* 0x41e5bb */
static void close_device(es2_mci_player *p) {
    MCIERROR r = mciSendStringA(es2_shell_mci_close_device, NULL, 0, NULL);
    if (r) error_box(p, r, NULL, "term avi ", 0x80);
}

/* One pass of the message pump 0x406328; WM_QUIT is kept as the quit flag. */
static void pump(es2_mci_player *p) {
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) { p->quit = 1; return; }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

/* 0x41e640: open, place, palette, play with notify, wait for the end, the
 * Esc/Space keys or a mouse button on the main window, close. */
static int32_t play(es2_mci_player *p, const es2_shell_movie_record *r) {
    char path[MAX_PATH], command[600], reply[500];
    HWND movie = NULL;
    MCIERROR code;
    int done = 0, skipped = 0;
    if (!r || es2_mci_player_path(p->cd_root, r->index, p->language, path, sizeof path) != 0 ||
        es2_shell_mci_open(command, sizeof command, path, (long)(LONG_PTR)p->parent) < 0)
        return ES2_SHELL_MOVIE_FAILED;
    if (mciSendStringA(command, NULL, 0, NULL) != 0)
        return ES2_SHELL_MOVIE_FAILED;   /* 0x41e815: assertion (level 3), return 0 */
    code = mciSendStringA(es2_shell_mci_window_handle, reply, sizeof reply, NULL);
    if (code == 0)
        movie = (HWND)(LONG_PTR)es2_rtl_atol(reply);   /* 0x41e6b4 _atol; the _scanf after it reads nothing */
    else
        error_box(p, code, p->parent, NULL, sizeof reply);   /* the handle stays undefined in VSHELL */
    /* 0x41e5fa: MoveWindow(x, y, width, height, repaint) in the main window's
     * client area; through the caller's frame rectangle (scaled and
     * centred like the presenter's picture) */
    if (movie) {
        es2_mci_rect frame = {0, 0, 0, 0}, at;
        if (!p->frame || !p->frame(p->context, &frame)) frame.width = frame.height = 0;
        at = es2_mci_player_place(&frame, &r->rect);
        MoveWindow(movie, at.x, at.y, at.width, at.height, TRUE);
    }
    if (es2_shell_mci_palette(command, sizeof command, (long)(LONG_PTR)p->palette_handle) >= 0) {
        code = mciSendStringA(command, NULL, 0, NULL);
        if (code) error_box(p, code, p->parent, "string palette setting error", 100);
    }
    SetCapture(p->parent);                 /* 0x41e781 */
    p->movie_open = 1;                     /* 0x41e786 [0x470eb0] */
    mciSendStringA(es2_shell_mci_play, NULL, 0, p->parent);   /* notify to the main window */
    while (!done) {
        MSG msg;
        if (!PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            WaitMessage();                 /* VSHELL polls; waiting is a host courtesy */
            continue;
        }
        if (msg.message == WM_QUIT) { p->quit = 1; break; }
        if (msg.message == MM_MCINOTIFY && msg.wParam == MCI_NOTIFY_SUCCESSFUL) done = 1;   /* 0x4057f2 */
        /* 0x405145 (left button down), 0x4050e0 (right button down): VSHELL
         * holds the capture, so every click reaches its main window.
         * SDL's mouse auto-capture releases the capture on a button up
         * (WIN_CaptureMouse -> ReleaseCapture), after which a click on the
         * movie lands in the MCI child window; a click on the parent or on
         * any window inside it skips. */
        if ((msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN) && in_parent(p, msg.hwnd))
            done = skipped = 1;
        /* 0x41e7b8..0x41e7e2: key ring codes 1 (Esc) and 0x39 (Space), key down */
        if ((msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN) &&
            (msg.wParam == VK_ESCAPE || msg.wParam == VK_SPACE))
            done = skipped = 1;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
        /* The capture back on the main window after a host window
         * procedure released it (VSHELL keeps it from 0x41e781 to 0x41e807) */
        if (!done && GetCapture() != p->parent) SetCapture(p->parent);
    }
    code = mciSendStringA(es2_shell_mci_close, NULL, 0, NULL);   /* 0x41e57c */
    if (code) error_box(p, code, NULL, "close avi ", 0x80);
    ReleaseCapture();                      /* 0x41e807 */
    p->movie_open = 0;                     /* 0x41e80e */
    return skipped ? ES2_SHELL_MOVIE_SKIPPED : ES2_SHELL_MOVIE_ENDED;
}

int32_t es2_mci_player_call(es2_mci_player *p, uint32_t action, int32_t arg, const es2_shell_movie_record *r) {
    if (!p) return 0;
    switch (action) {
    case ES2_SHELL_MOVIE_OPEN_DEVICE: return open_device(p, arg != 0);
    case ES2_SHELL_MOVIE_PLAY: return play(p, r);
    case ES2_SHELL_MOVIE_CLOSE_DEVICE: close_device(p); return 0;
    case ES2_SHELL_MOVIE_PUMP: pump(p); return 0;
    case ES2_SHELL_MOVIE_CD_PROMPT:
        if (p->cd_prompt) return p->cd_prompt(p->context, r);
        p->quit = 1;
        return 0;
    case ES2_SHELL_MOVIE_INTRO_ERROR:
        if (p->intro_error) p->intro_error(p->context, r);
        p->quit = 1;
        return 0;
    default: return 0;
    }
}
