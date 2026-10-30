/* The online manual's Windows operations (help.h). */
#include "help.h"

#include <windows.h>
#include <shellapi.h>
#include <string.h>

int es2_help_os_exists(void *ctx, const char *path) {
    DWORD attributes = GetFileAttributesA(path);
    (void)ctx;
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

int es2_help_os_open(void *ctx, const char *path) {
    SHELLEXECUTEINFOA info;
    (void)ctx;
    memset(&info, 0, sizeof info);
    info.cbSize = sizeof info;
    /* no error dialogs (a missing association is the caller's text) and no
     * asynchronous start that would outlive the thread */
    info.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    info.lpVerb = "open";
    info.lpFile = path;
    info.nShow = SW_SHOWNORMAL;
    /* window-lint: allow visible on purpose: the viewer for the manual the player asked for */
    return ShellExecuteExA(&info) ? 1 : 0;
}
