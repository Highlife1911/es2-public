/* DBSIM's DirectDraw branch in a window instead of exclusive full
 * screen ([presentation] directdraw=windowed). Contract:
 * docs/reverse-engineering/presentation.md, sections "DirectDraw branch" and
 * "capture, own GDI presenter, windowed DirectDraw".
 *
 * DBSIM takes the DirectDraw branch only when prefs.cfg byte 6 or -Z1 sets
 * [0x4d255e], or on Alt+Enter / the cockpit toggle 0x4667c8 (never in the
 * oracle, which runs the GDI branch). At the seam of the DirectDraw observer
 * (the DirectDrawCreate export of this proxy) DirectDrawCreate then returns
 * own v1 objects instead of the system's:
 *
 *   IDirectDraw         SetCooperativeLevel records the window (no exclusive
 *                       mode), SetDisplayMode records W x H x 8 (no mode
 *                       change), CreateSurface/CreatePalette below; every
 *                       other method answers DDERR_UNSUPPORTED (DBSIM calls
 *                       none of them).
 *   IDirectDrawSurface  primary of W x H, 8 bit, in own memory (pitch = W
 *                       rounded up to 4); with DDSCAPS_FLIP and a back-buffer
 *                       count (W < 640) one attached back buffer. Lock hands
 *                       out the memory, IsLost/Restore succeed. Present: the
 *                       Unlock of a non-flipping primary, or Flip (the back
 *                       buffer); drawn with the presenter's conversion and
 *                       scaler (presenter_frame.h) into the game window's
 *                       client area through GetDC/StretchDIBits/ReleaseDC.
 *   IDirectDrawPalette  256 entries from CreatePalette and SetEntries; the
 *                       palette set on the primary is the colour table.
 *
 * DBSIM reads back only return codes and lpSurface/lPitch (presentation.md):
 * every method it calls succeeds. DBSIM's own USER32 calls around the toggle
 * (SetWindowPos HWND_TOPMOST to the adjusted W x H rectangle, ClipCursor,
 * SetCursorPos, ShowCursor) and the cursor sprite drawn from GetCursorPos
 * stay its own; in a window the sprite and the clip use screen coordinates
 * (the limitation presentation.md "capture, own GDI presenter, windowed
 * DirectDraw" names). The capture of presenter.h
 * records the DirectDraw presents too (source "surface"; with capture_qpc=1
 * the stamp is read on entry to ddw_present). Flip does not swap
 * the two buffers: DBSIM rewrites every row of the back buffer before each
 * Flip and never reads a surface. No system DirectDraw object is created. */
#ifndef ES2_DDRAW_WINDOWED_H
#define ES2_DDRAW_WINDOWED_H
#include <ddraw.h>

#ifndef ES2_PRESENTER_H
#error "ddraw_windowed.h needs presenter.h (capture, conversion and the verified image)"
#endif

#define DDW_UNLOCK_PRESENT_RVA 0x00065876u   /* after `call [ecx+0x80]` at 0x465870 (0x465628) */
#define DDW_FLIP_PRESENT_RVA   0x00065895u   /* after `call [edx+0x2c]` at 0x465892 (0x465628) */
#define DDW_UNLOCK_CLEAR_RVA   0x0006552au   /* after `call [edx+0x80]` at 0x465524 (0x46548c) */
#define DDW_FLIP_CLEAR_RVA     0x0006554au   /* after `call [eax+0x2c]` at 0x465547 (0x46548c) */
#define DDW_MAX_SIDE 4096

enum { DDW_DRAW, DDW_SURFACE, DDW_PALETTE };
typedef struct ddw_object ddw_object;
struct ddw_object {
    void *const *vtable;          /* first: the COM object layout */
    LONG refs;
    unsigned kind;
    HWND window; DWORD cooperative; int mode_w, mode_h;          /* IDirectDraw */
    BOOL primary, flipping, locked; DWORD caps;                  /* IDirectDrawSurface */
    int width, height, pitch; BYTE *bits;
    ddw_object *back, *front, *palette;
    PALETTEENTRY entries[256];                                   /* IDirectDrawPalette */
};
static BOOL ddw_enabled;
static DWORD ddw_objects, ddw_released, ddw_unsupported, ddw_presents, ddw_present_clears, ddw_no_palette;
static DWORD ddw_no_window, ddw_draw_failures, ddw_uploads, ddw_mode_sets;
/* USER32 entry points the presenter uses; NULL = the system's (the fixture substitutes recorders). */
static HDC (WINAPI *ddw_get_dc)(HWND);
static int (WINAPI *ddw_release_dc)(HWND, HDC);
static BOOL (WINAPI *ddw_client_rect)(HWND, LPRECT);

static ddw_object *ddw_new(unsigned kind, void *const *vtable) {
    ddw_object *o = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*o));
    if (!o) return NULL;
    o->vtable = vtable; o->refs = 1; o->kind = kind; ++ddw_objects;
    return o;
}
static ddw_object *ddw_cast(void *p, unsigned kind);

/* ---- methods every interface shares --------------------------------------- */
static HRESULT WINAPI ddw_query(void *self, REFIID id, void **out) { (void)self; (void)id; if (out) *out = NULL; return E_NOINTERFACE; }
static ULONG WINAPI ddw_add_ref(void *self) { return (ULONG)InterlockedIncrement(&((ddw_object *)self)->refs); }
static ULONG WINAPI ddw_release(void *self) {
    ddw_object *o = self;
    LONG left = InterlockedDecrement(&o->refs);
    if (left) return (ULONG)left;
    if (o->kind == DDW_SURFACE) {
        if (o->back) ddw_release(o->back);
        if (o->palette) ddw_release(o->palette);
        if (o->bits) HeapFree(GetProcessHeap(), 0, o->bits);
    }
    ++ddw_released;
    HeapFree(GetProcessHeap(), 0, o);
    return 0;
}
/* Unsupported methods, one per argument count (stdcall: the callee pops). */
static HRESULT WINAPI ddw_no1(void *s) { (void)s; ++ddw_unsupported; return DDERR_UNSUPPORTED; }
static HRESULT WINAPI ddw_no2(void *s, uintptr_t a) { (void)s; (void)a; ++ddw_unsupported; return DDERR_UNSUPPORTED; }
static HRESULT WINAPI ddw_no3(void *s, uintptr_t a, uintptr_t b) { (void)s; (void)a; (void)b; ++ddw_unsupported; return DDERR_UNSUPPORTED; }
static HRESULT WINAPI ddw_no4(void *s, uintptr_t a, uintptr_t b, uintptr_t c) { (void)s; (void)a; (void)b; (void)c; ++ddw_unsupported; return DDERR_UNSUPPORTED; }
static HRESULT WINAPI ddw_no5(void *s, uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) {
    (void)s; (void)a; (void)b; (void)c; (void)d; ++ddw_unsupported; return DDERR_UNSUPPORTED;
}
static HRESULT WINAPI ddw_no6(void *s, uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)s; (void)a; (void)b; (void)c; (void)d; (void)e; ++ddw_unsupported; return DDERR_UNSUPPORTED;
}
static HRESULT WINAPI ddw_ok1(void *s) { (void)s; return DD_OK; }
static HRESULT WINAPI ddw_ok2(void *s, uintptr_t a) { (void)s; (void)a; return DD_OK; }

/* ---- the present ---------------------------------------------------------- */
static void ddw_rgb(const ddw_object *palette, BYTE rgb[768], DWORD table[256]) {
    for (unsigned i = 0; i < 256; ++i) {
        const PALETTEENTRY *e = &palette->entries[i];
        rgb[3 * i] = e->peRed; rgb[3 * i + 1] = e->peGreen; rgb[3 * i + 2] = e->peBlue;
        table[i] = (DWORD)e->peRed << 16 | (DWORD)e->peGreen << 8 | e->peBlue;
    }
}
/* surface: the buffer shown (the primary, or the back buffer at Flip);
 * front: the primary (palette, window); back/caller: DBSIM's return address
 * and frame pointer, for the capture. */
static void ddw_present(ddw_object *surface, ddw_object *front, uintptr_t back, const DWORD *caller) {
    uintptr_t base = (uintptr_t)pr_image;
    BOOL present = back == base + DDW_UNLOCK_PRESENT_RVA || back == base + DDW_FLIP_PRESENT_RVA;
    BOOL record = pr_capture && present && GetCurrentThreadId() == pr_thread;
    uint64_t entry = record && pr_qpc ? pr_now() : 0;   /* before the palette, the draw and the hash */
    if (back == base + DDW_UNLOCK_CLEAR_RVA || back == base + DDW_FLIP_CLEAR_RVA) ++ddw_present_clears;
    ++ddw_presents;
    BYTE rgb[768]; DWORD table[256];
    if (front->palette) ddw_rgb(front->palette, rgb, table);
    HWND window = NULL;
    for (ddw_object *o = front; o && !window; o = o->front) window = o->window;
    RECT client = {0, 0, 0, 0};
    const char *outcome = "forwarded";
    if (!window) ++ddw_no_window;
    else if (!(ddw_client_rect ? ddw_client_rect : GetClientRect)(window, &client)) ++ddw_no_window;
    else if (!front->palette) ++ddw_no_palette;
    else {
        HDC dc = (ddw_get_dc ? ddw_get_dc : GetDC)(window);
        if (dc && pr_draw(dc, surface->bits, surface->pitch, surface->width, surface->height, table,
                          (int)(client.right - client.left), (int)(client.bottom - client.top))) outcome = "windowed";
        else ++ddw_draw_failures;
        if (dc) (ddw_release_dc ? ddw_release_dc : ReleaseDC)(window, dc);
    }
    if (record) {
        char index_hex[65], palette_hex[65] = "";
        pr_hash_window(surface->bits, surface->pitch, surface->width, surface->height, index_hex);
        if (front->palette) pr_rgb_hash(rgb, palette_hex);
        pr_record_fields r = {pr_read(PR_FRAME_RVA), pr_present_site(caller[4]),
            {client.left, client.top, client.right, client.bottom}, {0, 0, surface->width, surface->height},
            {0, 0, (int)(client.right - client.left), (int)(client.bottom - client.top)}, "surface", index_hex,
            palette_hex, outcome, ddw_uploads, surface->bits, surface->pitch, rgb};
        pr_entry_qpc = entry;
        pr_write_record(&r);
        pr_entry_qpc = 0;
    }
}

/* ---- IDirectDrawSurface ---------------------------------------------------- */
static void ddw_describe(const ddw_object *s, DDSURFACEDESC *d) {
    memset(d, 0, sizeof(*d));
    d->dwSize = sizeof(*d);
    d->dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT | (s->flipping && s->primary ? DDSD_BACKBUFFERCOUNT : 0);
    d->dwHeight = (DWORD)s->height; d->dwWidth = (DWORD)s->width; d->lPitch = s->pitch;
    d->dwBackBufferCount = s->flipping && s->primary ? 1 : 0;
    d->ddpfPixelFormat.dwSize = sizeof(d->ddpfPixelFormat);
    d->ddpfPixelFormat.dwFlags = DDPF_RGB | DDPF_PALETTEINDEXED8;
    d->ddpfPixelFormat.dwRGBBitCount = 8;
    d->ddsCaps.dwCaps = s->caps;
}
static __attribute__((noinline)) HRESULT WINAPI ddw_flip(void *self, void *target, DWORD flags) {
    uintptr_t back = (uintptr_t)__builtin_return_address(0);
    const DWORD *caller = *(const DWORD *const *)__builtin_frame_address(0);
    ddw_object *s = self;
    (void)flags;
    if (!s->primary || !s->flipping || !s->back || (target && target != s->back)) return DDERR_NOTFLIPPABLE;
    if (s->locked || s->back->locked) return DDERR_SURFACEBUSY;
    ddw_present(s->back, s, back, caller);
    return DD_OK;
}
static HRESULT WINAPI ddw_get_attached(void *self, LPDDSCAPS caps, void **out) {
    ddw_object *s = self;
    if (!caps || !out) return DDERR_INVALIDPARAMS;
    if (!s->back || !(caps->dwCaps & DDSCAPS_BACKBUFFER)) { *out = NULL; return DDERR_NOTFOUND; }
    ddw_add_ref(s->back); *out = s->back;
    return DD_OK;
}
static HRESULT WINAPI ddw_surface_caps(void *self, LPDDSCAPS caps) {
    if (!caps) return DDERR_INVALIDPARAMS;
    caps->dwCaps = ((ddw_object *)self)->caps;
    return DD_OK;
}
static HRESULT WINAPI ddw_get_palette(void *self, void **out) {
    ddw_object *s = self;
    if (!out) return DDERR_INVALIDPARAMS;
    if (!s->palette) { *out = NULL; return DDERR_NOPALETTEATTACHED; }
    ddw_add_ref(s->palette); *out = s->palette;
    return DD_OK;
}
static HRESULT WINAPI ddw_surface_desc(void *self, LPDDSURFACEDESC d) {
    if (!d || d->dwSize != sizeof(*d)) return DDERR_INVALIDPARAMS;
    ddw_describe(self, d);
    return DD_OK;
}
static HRESULT WINAPI ddw_lock(void *self, LPRECT rect, LPDDSURFACEDESC d, DWORD flags, HANDLE event) {
    ddw_object *s = self;
    (void)flags; (void)event;
    if (!d || d->dwSize != sizeof(*d)) return DDERR_INVALIDPARAMS;
    if (s->locked) return DDERR_SURFACEBUSY;
    LONG x = 0, y = 0;
    if (rect) {
        if (rect->left < 0 || rect->top < 0 || rect->right > s->width || rect->bottom > s->height ||
            rect->left >= rect->right || rect->top >= rect->bottom) return DDERR_INVALIDRECT;
        x = rect->left; y = rect->top;
    }
    ddw_describe(s, d);
    d->dwFlags |= DDSD_LPSURFACE;
    d->lpSurface = s->bits + (size_t)y * (size_t)s->pitch + (size_t)x;
    s->locked = TRUE;
    return DD_OK;
}
static __attribute__((noinline)) HRESULT WINAPI ddw_unlock(void *self, void *pointer) {
    uintptr_t back = (uintptr_t)__builtin_return_address(0);
    const DWORD *caller = *(const DWORD *const *)__builtin_frame_address(0);
    ddw_object *s = self;
    (void)pointer;
    if (!s->locked) return DDERR_NOTLOCKED;
    s->locked = FALSE;
    if (s->primary && !s->flipping) ddw_present(s, s, back, caller);
    return DD_OK;
}
static HRESULT WINAPI ddw_set_palette(void *self, void *palette) {
    ddw_object *s = self, *p = palette ? ddw_cast(palette, DDW_PALETTE) : NULL;
    if (palette && !p) return DDERR_INVALIDOBJECT;
    if (p) ddw_add_ref(p);
    if (s->palette) ddw_release(s->palette);
    s->palette = p;
    return DD_OK;
}
/* IDirectDrawSurface v1, 36 slots (argument counts include `this`). */
static void *const ddw_surface_table[36] = {
    ddw_query, ddw_add_ref, ddw_release, ddw_no2 /* AddAttachedSurface */, ddw_no2 /* AddOverlayDirtyRect */,
    ddw_no6 /* Blt */, ddw_no4 /* BltBatch */, ddw_no6 /* BltFast */, ddw_no3 /* DeleteAttachedSurface */,
    ddw_no3 /* EnumAttachedSurfaces */, ddw_no4 /* EnumOverlayZOrders */, ddw_flip, ddw_get_attached,
    ddw_ok2 /* GetBltStatus */, ddw_surface_caps, ddw_no2 /* GetClipper */, ddw_no3 /* GetColorKey */,
    ddw_no2 /* GetDC */, ddw_ok2 /* GetFlipStatus */, ddw_no3 /* GetOverlayPosition */, ddw_get_palette,
    ddw_no2 /* GetPixelFormat */, ddw_surface_desc, ddw_no3 /* Initialize */, ddw_ok1 /* IsLost */, ddw_lock,
    ddw_no2 /* ReleaseDC */, ddw_ok1 /* Restore */, ddw_no2 /* SetClipper */, ddw_no3 /* SetColorKey */,
    ddw_no3 /* SetOverlayPosition */, ddw_set_palette, ddw_unlock, ddw_no6 /* UpdateOverlay */,
    ddw_no2 /* UpdateOverlayDisplay */, ddw_no3 /* UpdateOverlayZOrder */,
};

/* ---- IDirectDrawPalette ---------------------------------------------------- */
static HRESULT WINAPI ddw_palette_caps(void *self, LPDWORD caps) {
    (void)self;
    if (!caps) return DDERR_INVALIDPARAMS;
    *caps = DDPCAPS_8BIT | DDPCAPS_ALLOW256;
    return DD_OK;
}
static HRESULT WINAPI ddw_get_entries(void *self, DWORD flags, DWORD start, DWORD count, LPPALETTEENTRY out) {
    ddw_object *p = self;
    (void)flags;
    if (!out || start >= 256 || count > 256 - start) return DDERR_INVALIDPARAMS;
    memcpy(out, p->entries + start, count * sizeof(PALETTEENTRY));
    return DD_OK;
}
static HRESULT WINAPI ddw_set_entries(void *self, DWORD flags, DWORD start, DWORD count, LPPALETTEENTRY in) {
    ddw_object *p = self;
    (void)flags;
    if (!in || start >= 256 || count > 256 - start) return DDERR_INVALIDPARAMS;
    memcpy(p->entries + start, in, count * sizeof(PALETTEENTRY));
    ++ddw_uploads;
    return DD_OK;
}
static void *const ddw_palette_table[7] = {
    ddw_query, ddw_add_ref, ddw_release, ddw_palette_caps, ddw_get_entries, ddw_no4 /* Initialize */, ddw_set_entries,
};

/* ---- IDirectDraw ------------------------------------------------------------- */
static HRESULT WINAPI ddw_create_palette(void *self, DWORD flags, LPPALETTEENTRY entries, void **out, IUnknown *outer) {
    (void)self;
    if (!out) return DDERR_INVALIDPARAMS;
    *out = NULL;
    if (outer) return CLASS_E_NOAGGREGATION;
    if (!(flags & DDPCAPS_8BIT) || !entries) return DDERR_INVALIDPARAMS;
    ddw_object *p = ddw_new(DDW_PALETTE, ddw_palette_table);
    if (!p) return DDERR_OUTOFMEMORY;
    memcpy(p->entries, entries, sizeof(p->entries));
    ++ddw_uploads;
    *out = p;
    return DD_OK;
}
static ddw_object *ddw_new_surface(int w, int h, DWORD caps) {
    ddw_object *s = ddw_new(DDW_SURFACE, ddw_surface_table);
    if (!s) return NULL;
    s->width = w; s->height = h; s->pitch = (w + 3) & ~3; s->caps = caps;
    s->bits = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (size_t)s->pitch * (size_t)h);
    if (!s->bits) { ddw_release(s); return NULL; }
    return s;
}
/* DBSIM's descriptor (0x4665c4) sets the caps but not DDSD_CAPS; the caps
 * word is read regardless. Only the primary (with or without one back
 * buffer) and plain off-screen surfaces with DDSD_WIDTH | DDSD_HEIGHT. */
static HRESULT WINAPI ddw_create_surface(void *self, LPDDSURFACEDESC d, void **out, IUnknown *outer) {
    ddw_object *draw = self;
    if (!out) return DDERR_INVALIDPARAMS;
    *out = NULL;
    if (outer) return CLASS_E_NOAGGREGATION;
    if (!d || d->dwSize != sizeof(*d)) return DDERR_INVALIDPARAMS;
    DWORD caps = d->ddsCaps.dwCaps;
    ddw_object *s;
    if (caps & DDSCAPS_PRIMARYSURFACE) {
        if (draw->mode_w <= 0 || draw->mode_h <= 0) return DDERR_INVALIDPARAMS;
        BOOL flipping = (caps & DDSCAPS_FLIP) && (d->dwFlags & DDSD_BACKBUFFERCOUNT) && d->dwBackBufferCount;
        DWORD shown = (caps & ~(DWORD)(DDSCAPS_VIDEOMEMORY | DDSCAPS_MODEX)) | DDSCAPS_SYSTEMMEMORY | (flipping ? DDSCAPS_FRONTBUFFER : 0);
        s = ddw_new_surface(draw->mode_w, draw->mode_h, shown);
        if (!s) return DDERR_OUTOFMEMORY;
        s->primary = TRUE; s->flipping = flipping; s->front = draw;
        if (flipping) {
            DWORD back_caps = (shown & ~(DWORD)(DDSCAPS_PRIMARYSURFACE | DDSCAPS_FRONTBUFFER)) | DDSCAPS_BACKBUFFER;
            s->back = ddw_new_surface(draw->mode_w, draw->mode_h, back_caps);
            if (!s->back) { ddw_release(s); return DDERR_OUTOFMEMORY; }
            s->back->front = s;
        }
    } else {
        if ((d->dwFlags & (DDSD_WIDTH | DDSD_HEIGHT)) != (DDSD_WIDTH | DDSD_HEIGHT) || !d->dwWidth || !d->dwHeight ||
            d->dwWidth > DDW_MAX_SIDE || d->dwHeight > DDW_MAX_SIDE) return DDERR_INVALIDPARAMS;
        s = ddw_new_surface((int)d->dwWidth, (int)d->dwHeight, (caps & ~(DWORD)DDSCAPS_VIDEOMEMORY) | DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY);
        if (!s) return DDERR_OUTOFMEMORY;
    }
    *out = s;
    return DD_OK;
}
static HRESULT WINAPI ddw_set_cooperative(void *self, HWND window, DWORD flags) {
    ddw_object *draw = self;
    draw->window = window; draw->cooperative = flags;   /* recorded only: no exclusive mode */
    return DD_OK;
}
static HRESULT WINAPI ddw_set_mode(void *self, DWORD w, DWORD h, DWORD bpp) {
    ddw_object *draw = self;
    if (bpp != 8 || !w || !h || w > DDW_MAX_SIDE || h > DDW_MAX_SIDE) return DDERR_INVALIDMODE;
    draw->mode_w = (int)w; draw->mode_h = (int)h; ++ddw_mode_sets;   /* the desktop mode stays */
    return DD_OK;
}
/* IDirectDraw v1, 23 slots. */
static void *const ddw_draw_table[23] = {
    ddw_query, ddw_add_ref, ddw_release, ddw_no1 /* Compact */, ddw_no4 /* CreateClipper */, ddw_create_palette,
    ddw_create_surface, ddw_no3 /* DuplicateSurface */, ddw_no5 /* EnumDisplayModes */, ddw_no5 /* EnumSurfaces */,
    ddw_ok1 /* FlipToGDISurface */, ddw_no3 /* GetCaps */, ddw_no2 /* GetDisplayMode */, ddw_no3 /* GetFourCCCodes */,
    ddw_no2 /* GetGDISurface */, ddw_no2 /* GetMonitorFrequency */, ddw_no2 /* GetScanLine */,
    ddw_no2 /* GetVerticalBlankStatus */, ddw_no2 /* Initialize */, ddw_ok1 /* RestoreDisplayMode */,
    ddw_set_cooperative, ddw_set_mode, ddw_no3 /* WaitForVerticalBlank */,
};

static ddw_object *ddw_cast(void *p, unsigned kind) {
    ddw_object *o = p;
    void *const *expected = kind == DDW_PALETTE ? ddw_palette_table : kind == DDW_SURFACE ? ddw_surface_table : ddw_draw_table;
    return o && o->vtable == expected && o->kind == kind ? o : NULL;
}

/* The replacement of the DirectDrawCreate export. */
static HRESULT WINAPI ddw_create(GUID *id, LPDIRECTDRAW *out, IUnknown *outer) {
    if (!out) return DDERR_INVALIDPARAMS;
    *out = NULL;
    if (outer) return CLASS_E_NOAGGREGATION;
    if (id && (uintptr_t)id > 2) return DDERR_INVALIDDIRECTDRAWGUID;   /* only the primary display (NULL) and the two DDCREATE_ values */
    ddw_object *draw = ddw_new(DDW_DRAW, ddw_draw_table);
    if (!draw) return DDERR_OUTOFMEMORY;
    *out = (LPDIRECTDRAW)draw;
    return DD_OK;
}

/* [presentation] directdraw=windowed in the presenter's INI (presenter.h
 * pr_config_file), on the verified reference image only. */
static BOOL ddw_requested(void) {
    if (pr_state <= 0) return FALSE;
    WCHAR ini[MAX_PATH], value[16];
    if (!pr_config_file(ini)) return FALSE;
    GetPrivateProfileStringW(L"presentation", L"directdraw", L"original", value, 16, ini);
    if (!_wcsicmp(value, L"windowed")) return TRUE;
    if (_wcsicmp(value, L"original")) log_event("presentation_directdraw_unknown");
    return FALSE;
}
#endif
