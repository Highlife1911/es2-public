/* The SDL key event -> Windows virtual-key mapping of window.c and the
 * resulting key indices of VSHELL's table; the test's own icon loader
 * (file_icon) on a missing file, on a file without the icon and, under
 * ES2_DATA_ROOT, on the reference VER95/VSHELL.EXE (its "es2icon" at 32 and
 * 16 pixels); the stored icon (es2_gui_store_icon) without a file, and under
 * ES2_DATA_ROOT equal in size and pixels to the file's at 32 and 16 pixels;
 * the presenter's texture upload (es2_gui_screen_pixels into the locked
 * XRGB8888 texture) against SDL_UpdateTexture from the screen words, read
 * back through a software renderer. No SDL_Init, no window. */
#include "es2/shell/gui/events.h"
#include "es2/shell/gui/frame.h"
#include "es2/shell/gui/window.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static unsigned checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; fprintf(stderr, "line %d: %s\n", __LINE__, #x); } } while (0)

/* VSHELL's "es2icon" at size x size from the resources of exe_path, mapped as
 * a data file (none of its code runs, the file is not changed); NULL with
 * the reason in error. */
static void *file_icon(const char *exe_path, int size, char *error, size_t error_size) {
    HMODULE module = LoadLibraryExA(exe_path, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (!module) {
        snprintf(error, error_size, "cannot read %s", exe_path);
        return NULL;
    }
    HICON icon = (HICON)LoadImageA(module, "es2icon", IMAGE_ICON, size, size, LR_DEFAULTCOLOR);
    FreeLibrary(module);
    if (!icon) snprintf(error, error_size, "no icon es2icon in %s", exe_path);
    return icon;
}

static unsigned index_of(SDL_Keycode key, SDL_Scancode scan, int numlock) {
    return es2_gui_key_index(es2_gui_sdl_vk((unsigned)key, (unsigned)scan, numlock));
}

static int icon_width(void *icon) {
    ICONINFO info;
    BITMAP bitmap;
    int width = 0;
    if (!GetIconInfo(icon, &info)) return 0;
    if (GetObjectA(info.hbmColor ? info.hbmColor : info.hbmMask, sizeof bitmap, &bitmap)) width = bitmap.bmWidth;
    if (info.hbmColor) DeleteObject(info.hbmColor);
    if (info.hbmMask) DeleteObject(info.hbmMask);
    return width;
}

/* The colour and mask bits of two icons are equal (32-bit DIBs of both). */
static int icon_bits(void *icon, uint8_t *out, size_t capacity, size_t *used) {
    ICONINFO info;
    BITMAP bitmap;
    int ok = 0;
    *used = 0;
    if (!GetIconInfo(icon, &info)) return 0;
    HDC dc = GetDC(NULL);
    HBITMAP parts[2] = {info.hbmColor, info.hbmMask};
    for (int k = 0; k < 2; k++) {
        if (!parts[k] || !GetObjectA(parts[k], sizeof bitmap, &bitmap)) continue;
        BITMAPINFO bi = {0};
        size_t n = (size_t)bitmap.bmWidth * (size_t)abs(bitmap.bmHeight) * 4;
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = bitmap.bmWidth;
        bi.bmiHeader.biHeight = bitmap.bmHeight;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        if (*used + n > capacity || !GetDIBits(dc, parts[k], 0, (UINT)abs(bitmap.bmHeight), out + *used, &bi, DIB_RGB_COLORS))
            goto done;
        *used += n;
    }
    ok = *used > 0;
done:
    ReleaseDC(NULL, dc);
    if (info.hbmColor) DeleteObject(info.hbmColor);
    if (info.hbmMask) DeleteObject(info.hbmMask);
    return ok;
}
static int same_icon(void *a, void *b) {
    static uint8_t x[65536], y[65536];
    size_t nx, ny;
    if (!a || !b || !icon_bits(a, x, sizeof x, &nx) || !icon_bits(b, y, sizeof y, &ny)) return 0;
    return icon_width(a) == icon_width(b) && nx == ny && !memcmp(x, y, nx);
}

static void icons(const char *self) {
    char error[512] = "", path[1024];
    CHECK(!file_icon("no such directory\\VSHELL.EXE", 32, error, sizeof error));
    CHECK(!strncmp(error, "cannot read", 11));
    CHECK(!file_icon(self, 32, error, sizeof error)); /* the test itself has no es2icon */
    CHECK(!strncmp(error, "no icon es2icon", 15));
    /* The stored icon needs no file */
    void *stored = es2_gui_store_icon(32, error, sizeof error);
    CHECK(stored && icon_width(stored) == 32);
    if (stored) DestroyIcon(stored);
    const char *root = getenv("ES2_DATA_ROOT");
    if (!root || !*root) {
        printf("SKIP: the reference icon (ES2_DATA_ROOT not set)\n");
        return;
    }
    snprintf(path, sizeof path, "%s/VER95/VSHELL.EXE", root);
    void *big = file_icon(path, 32, error, sizeof error), *little = file_icon(path, 16, error, sizeof error);
    CHECK(big && little);
    CHECK(icon_width(big) == 32 && icon_width(little) == 16);
    /* The stored icon gives the same images (size and pixels) */
    void *stored_big = es2_gui_store_icon(32, error, sizeof error), *stored_little = es2_gui_store_icon(16, error, sizeof error);
    CHECK(stored_big && stored_little);
    CHECK(same_icon(big, stored_big) && same_icon(little, stored_little));
    if (stored_big) DestroyIcon(stored_big);
    if (stored_little) DestroyIcon(stored_little);
    if (big) DestroyIcon(big);
    if (little) DestroyIcon(little);
}

/* The picture a 640 x 480 XRGB8888 streaming texture gives on a software
 * renderer, filled by SDL_UpdateTexture (by_words) or by the presenter's
 * lock and es2_gui_screen_pixels; 1 when it was read back. */
static int texture_picture(const es2_gui_screen *screen, int by_words, uint8_t *out) {
    int ok = 0;
    SDL_Surface *target = SDL_CreateSurface(ES2_GUI_WIDTH, ES2_GUI_HEIGHT, SDL_PIXELFORMAT_XRGB8888);
    SDL_Renderer *r = target ? SDL_CreateSoftwareRenderer(target) : NULL;
    SDL_Texture *t = r ? SDL_CreateTexture(r, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, ES2_GUI_WIDTH,
                                           ES2_GUI_HEIGHT) : NULL;
    if (t) {
        void *pixels = NULL;
        int pitch = 0;
        if (by_words) ok = SDL_UpdateTexture(t, NULL, screen->rgb, ES2_GUI_WIDTH * 4);
        else if (SDL_LockTexture(t, NULL, &pixels, &pitch)) {
            ok = pitch > 0 && es2_gui_screen_pixels(screen, ES2_GUI_PIXELS_XRGB8888, pixels, (size_t)pitch);
            SDL_UnlockTexture(t);
        }
        ok = ok && SDL_RenderTexture(r, t, NULL, NULL);
        SDL_Surface *read = ok ? SDL_RenderReadPixels(r, NULL) : NULL;
        SDL_Surface *xrgb = read ? SDL_ConvertSurface(read, SDL_PIXELFORMAT_XRGB8888) : NULL;
        ok = xrgb != NULL && xrgb->w == ES2_GUI_WIDTH && xrgb->h == ES2_GUI_HEIGHT;
        for (int y = 0; ok && y < ES2_GUI_HEIGHT; y++)
            memcpy(out + (size_t)y * ES2_GUI_WIDTH * 4, (uint8_t *)xrgb->pixels + (size_t)y * xrgb->pitch,
                   ES2_GUI_WIDTH * 4);
        SDL_DestroySurface(xrgb);
        SDL_DestroySurface(read);
        SDL_DestroyTexture(t);
    }
    if (r) SDL_DestroyRenderer(r);
    SDL_DestroySurface(target);
    return ok;
}

static void texture_upload(void) {
    static es2_gui_screen screen;
    static uint8_t a[ES2_GUI_PIXELS * 4], b[ES2_GUI_PIXELS * 4];
    uint32_t seed = 12345;
    for (size_t i = 0; i < ES2_GUI_PIXELS; i++) {
        seed = seed * 1103515245u + 12345u;
        screen.rgb[i] = seed >> 8;   /* 0x00RRGGBB, as the frame's lookup gives */
    }
    CHECK(texture_picture(&screen, 1, a));
    CHECK(texture_picture(&screen, 0, b));
    CHECK(memcmp(a, b, sizeof a) == 0);
}

int main(int argc, char **argv) {
    (void)argc;
    icons(argv[0]);
    texture_upload();
    CHECK(index_of(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE, 0) == 1);
    CHECK(index_of(SDLK_1, SDL_SCANCODE_1, 0) == 2 && index_of(SDLK_0, SDL_SCANCODE_0, 0) == 11);
    CHECK(index_of(SDLK_AMPERSAND, SDL_SCANCODE_1, 0) == 2); /* French layout: the digit row is still VK_1 */
    CHECK(index_of(SDLK_Q, SDL_SCANCODE_Q, 0) == 16 && index_of(SDLK_Z, SDL_SCANCODE_Y, 0) == 44); /* layout letters */
    CHECK(index_of(SDLK_RETURN, SDL_SCANCODE_RETURN, 0) == 28 && index_of(SDLK_KP_ENTER, SDL_SCANCODE_KP_ENTER, 0) == 28);
    CHECK(index_of(SDLK_LCTRL, SDL_SCANCODE_LCTRL, 0) == 29 && index_of(SDLK_RALT, SDL_SCANCODE_RALT, 0) == 56);
    CHECK(index_of(SDLK_LSHIFT, SDL_SCANCODE_LSHIFT, 0) == 0); /* Shift is not in the table */
    CHECK(index_of(SDLK_F1, SDL_SCANCODE_F1, 0) == 59 && index_of(SDLK_F10, SDL_SCANCODE_F10, 0) == 68);
    CHECK(index_of(SDLK_F11, SDL_SCANCODE_F11, 0) == 0 && index_of(SDLK_MINUS, SDL_SCANCODE_MINUS, 0) == 0);
    CHECK(index_of(SDLK_UP, SDL_SCANCODE_UP, 0) == 72 && index_of(SDLK_DELETE, SDL_SCANCODE_DELETE, 0) == 83);
    CHECK(index_of(SDLK_KP_8, SDL_SCANCODE_KP_8, 0) == 72);  /* Num Lock off: navigation */
    CHECK(index_of(SDLK_KP_5, SDL_SCANCODE_KP_5, 0) == 76);  /* VK_CLEAR */
    CHECK(index_of(SDLK_KP_PERIOD, SDL_SCANCODE_KP_PERIOD, 0) == 83);
    CHECK(index_of(SDLK_KP_8, SDL_SCANCODE_KP_8, 1) == 0);   /* Num Lock on: VK_NUMPAD8, not in the table */
    CHECK(index_of(SDLK_KP_MINUS, SDL_SCANCODE_KP_MINUS, 1) == 74 && index_of(SDLK_KP_PLUS, SDL_SCANCODE_KP_PLUS, 1) == 78);
    CHECK(index_of(SDLK_SPACE, SDL_SCANCODE_SPACE, 0) == 57 && index_of(SDLK_CAPSLOCK, SDL_SCANCODE_CAPSLOCK, 0) == 58);
    CHECK(es2_gui_sdl_vk(SDLK_KP_0, SDL_SCANCODE_KP_0, 1) == 0x60 && es2_gui_sdl_vk(SDLK_KP_9, SDL_SCANCODE_KP_9, 1) == 0x69);
    printf("%u checks, %u failures\n", checks, failures);
    return failures != 0;
}
