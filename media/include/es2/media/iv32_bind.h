/* Process-local Indeo 3.2 (IV32) decoder binding.
 *
 * Exposes the Windows Indeo wrapper that ships with the system
 * (%SystemRoot%\System32 or SysWOW64\ir32_32.dll) to Video for Windows in the
 * calling process only: the wrapper is opened through OpenDriver (so its own
 * checks run and it gets the driver handle it requires) and a callback is
 * added to this process's VfW function table with ICInstall(ICINSTALL_FUNCTION).
 * The registry-writing ICINSTALL_DRIVER branch is never used; no codec is
 * installed system-wide. See docs/reference-game/media_compatibility.md.
 *
 * User: the DirectDraw proxy (tools/reference-game/proxy/media_proxy.c, which
 * keeps its MCI import redirection to itself).
 *
 * msvfw32.dll and winmm.dll are resolved at run time (LoadLibraryExW +
 * GetProcAddress): linking this library adds no import to the module that
 * uses it. Not thread-safe; callers serialise (the proxy runs it under an
 * INIT_ONCE). */
#ifndef ES2_MEDIA_IV32_BIND_H
#define ES2_MEDIA_IV32_BIND_H

#include <windows.h>
#include <mmsystem.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The three installable-driver entry points of winmm.dll. The proxy passes the
 * ones it resolved from the system winmm.dll; es2_iv32_system_drivers fills
 * them for other callers. */
typedef struct es2_iv32_drivers {
    HDRVR (WINAPI *open_driver)(LPCWSTR driver, LPCWSTR section, LPARAM parameter);
    LRESULT (WINAPI *close_driver)(HDRVR driver, LPARAM a, LPARAM b);
    LRESULT (WINAPI *driver_message)(HDRVR driver, UINT message, LPARAM a, LPARAM b);
} es2_iv32_drivers;

typedef enum es2_iv32_status {
    ES2_IV32_NOT_ATTEMPTED = 0,
    ES2_IV32_BAD_ARGUMENT,          /* drivers NULL or incomplete; nothing called */
    ES2_IV32_WRAPPER_MISSING,       /* ir32_32.dll path too long or file absent */
    ES2_IV32_VFW_PATH,              /* msvfw32.dll path too long (no log event) */
    ES2_IV32_VFW_MISSING,           /* LoadLibraryExW(msvfw32.dll) failed */
    ES2_IV32_VFW_EXPORTS_MISSING,   /* ICOpen, ICClose or ICInstall missing */
    ES2_IV32_EXISTING,              /* an IV32 decompressor already opens: nothing installed */
    ES2_IV32_BOUND,                 /* ICInstall(ICINSTALL_FUNCTION) succeeded */
    ES2_IV32_INSTALL_FAILED         /* ICInstall returned FALSE */
} es2_iv32_status;

/* Binds IV32 in this process. system_directory NULL means GetSystemDirectoryW
 * (for a 32-bit process on 64-bit Windows: SysWOW64); tests pass their own
 * directory to reach the refusals. The calls, in order: the wrapper path and
 * GetFileAttributesW, the msvfw32 path and LoadLibraryExW, GetProcAddress of
 * ICOpen/ICClose/ICInstall, ICOpen(vidc, IV32, decompress) (+ ICClose when it
 * opens), ICInstall(vidc, IV32, callback, NULL, ICINSTALL_FUNCTION). Once a
 * call reached a result other than BAD_ARGUMENT, later calls return that same
 * result without calling anything, until es2_iv32_release. msvfw32.dll stays
 * loaded for the life of the process. */
es2_iv32_status es2_iv32_bind(const wchar_t *system_directory, const es2_iv32_drivers *drivers);

/* The proxy's log event name for a status ("system_indeo_missing", ...), NULL
 * for statuses the proxy never logged (NOT_ATTEMPTED, BAD_ARGUMENT, VFW_PATH). */
const char *es2_iv32_event(es2_iv32_status status);

/* Loads the system winmm.dll (LoadLibraryExW, System32 search) and resolves
 * OpenDriver, CloseDriver, SendDriverMessage. Returns 1 on success. */
int es2_iv32_system_drivers(es2_iv32_drivers *out);

/* 1 while this process's function-table entry is installed. */
int es2_iv32_bound(void);

/* Removes this process's function-table entry (ICRemove(vidc, IV32, 0)) when
 * es2_iv32_bind installed it; otherwise does nothing (so ICRemove can never
 * reach a registry mapping it did not create). Decompressors that are still
 * open keep their driver instance until ICClose. Returns 1 when an entry was
 * removed. The binding can be made again afterwards. */
int es2_iv32_release(void);

#ifdef __cplusplus
}
#endif

#endif
