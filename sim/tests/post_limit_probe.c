/* The per-queue limit of posted messages on this Windows, measured by a
 * synthetic program that posts to its own thread until the post fails
 * (main_loop.md, "the posted queue's depth"). No game file, no visible window,
 * no sound device, no child process: the thread's queue is created by a
 * PeekMessageA call, the posts go through PostThreadMessageA, PostMessageA with
 * a NULL window (the calling thread) and PostMessageA to a message-only window
 * (the original posts to its main window with PostMessageA); after each run the
 * queue is read back in order. The registry value USERPostMessageLimit is only
 * read. Nothing is changed on the system.
 *
 * Without arguments: prints the measurements (the program of the document)
 * and checks them: the posts fail with ERROR_NOT_ENOUGH_QUOTA after at
 * least the documented minimum of 4000 messages, the messages come back in the order
 * they were posted and the count equals the limit. Exit code 0: all checks
 * held. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "CHECK failed %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)
#define PROBE_MESSAGE (WM_USER + 7u)
#define LIMIT_CEILING 100000u   /* a stop for a system without a limit */

static int failures;

typedef struct run {
    const char *name;
    unsigned accepted;          /* posts that succeeded before the first failure */
    DWORD error;                /* GetLastError() of the first failure; 0 when none */
    unsigned retrieved;         /* messages read back with PeekMessageA(PM_REMOVE) */
    unsigned in_order;          /* of those, the ones whose payload is their post index */
    unsigned again_after_one;   /* after removing one message: does the next post succeed (1) or not (0) */
} run;

enum { VIA_THREAD, VIA_NULL_WINDOW, VIA_WINDOW };

static BOOL do_post(int how, HWND window, DWORD thread, unsigned index) {
    switch (how) {
    case VIA_THREAD: return PostThreadMessageA(thread, PROBE_MESSAGE, (WPARAM)index, (LPARAM)(index ^ 0x5a5a5a5au));
    case VIA_NULL_WINDOW: return PostMessageA(NULL, PROBE_MESSAGE, (WPARAM)index, (LPARAM)(index ^ 0x5a5a5a5au));
    default: return PostMessageA(window, PROBE_MESSAGE, (WPARAM)index, (LPARAM)(index ^ 0x5a5a5a5au));
    }
}

/* Removes every message of the thread's queue; counts the probe messages and
 * those whose payload is the next expected index (counting up from `expect`). */
static void drain(run *r, unsigned expect) {
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message != PROBE_MESSAGE) continue;
        ++r->retrieved;
        if (msg.wParam == (WPARAM)expect && msg.lParam == (LPARAM)(expect ^ 0x5a5a5a5au)) ++r->in_order;
        ++expect;
    }
}

static void measure(run *r, const char *name, int how, HWND window, DWORD thread) {
    MSG msg;
    memset(r, 0, sizeof(*r));
    r->name = name;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {}   /* the queue starts empty */
    for (unsigned i = 0; i < LIMIT_CEILING; ++i) {
        SetLastError(0);
        if (!do_post(how, window, thread, i)) { r->error = GetLastError(); break; }
        ++r->accepted;
    }
    /* Remove the oldest message: the quota is freed, so a post succeeds again. */
    if (PeekMessageA(&msg, NULL, PROBE_MESSAGE, PROBE_MESSAGE, PM_REMOVE)) {
        ++r->retrieved;
        if (msg.wParam == 0) ++r->in_order;
        r->again_after_one = do_post(how, window, thread, r->accepted) ? 1u : 0u;
        /* The reposted message is the last in the queue: indices 1..accepted. */
        drain(r, 1u);
    } else {
        drain(r, 0u);
    }
}

typedef LONG(WINAPI *rtl_get_version_fn)(OSVERSIONINFOW *);

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    /* The Windows build. */
    OSVERSIONINFOW os;
    memset(&os, 0, sizeof(os));
    os.dwOSVersionInfoSize = sizeof(os);
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    rtl_get_version_fn rtl = ntdll ? (rtl_get_version_fn)(void *)GetProcAddress(ntdll, "RtlGetVersion") : NULL;
    if (rtl && rtl(&os) == 0)
        printf("windows: %lu.%lu build %lu\n", (unsigned long)os.dwMajorVersion, (unsigned long)os.dwMinorVersion,
               (unsigned long)os.dwBuildNumber);
    else
        printf("windows: version unavailable\n");
    /* Read-only registry facts: the update revision and the post limit. */
    HKEY key;
    char text[64];
    DWORD size = sizeof(text), type = 0;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ | KEY_WOW64_64KEY,
                      &key) == ERROR_SUCCESS) {
        DWORD ubr = 0, ubr_size = sizeof(ubr);
        if (RegQueryValueExA(key, "UBR", NULL, &type, (BYTE *)&ubr, &ubr_size) == ERROR_SUCCESS && type == REG_DWORD)
            printf("windows: update revision %lu\n", (unsigned long)ubr);
        size = sizeof(text);
        if (RegQueryValueExA(key, "ProductName", NULL, &type, (BYTE *)text, &size) == ERROR_SUCCESS && type == REG_SZ) {
            text[sizeof(text) - 1u] = 0;
            printf("windows: %s\n", text);
        }
        RegCloseKey(key);
    }
    int registry_value = -1;   /* -1: the value is not present (the default applies) */
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Windows", 0,
                      KEY_READ | KEY_WOW64_64KEY, &key) == ERROR_SUCCESS) {
        DWORD value = 0, value_size = sizeof(value);
        if (RegQueryValueExA(key, "USERPostMessageLimit", NULL, &type, (BYTE *)&value, &value_size) == ERROR_SUCCESS &&
            type == REG_DWORD)
            registry_value = (int)value;
        RegCloseKey(key);
    }
    if (registry_value < 0) printf("registry: USERPostMessageLimit not present (the documented default is 10000)\n");
    else printf("registry: USERPostMessageLimit = %d\n", registry_value);

    /* The thread's queue. */
    MSG msg;
    PeekMessageA(&msg, NULL, 0, 0, PM_NOREMOVE);
    DWORD thread = GetCurrentThreadId();
    HWND window = CreateWindowExA(0, "STATIC", "h5-20c", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, GetModuleHandleA(NULL), NULL);
    CHECK(window != NULL);

    run runs[3];
    measure(&runs[0], "PostThreadMessageA(own thread)", VIA_THREAD, NULL, thread);
    measure(&runs[1], "PostMessageA(NULL window)", VIA_NULL_WINDOW, NULL, thread);
    measure(&runs[2], "PostMessageA(message-only window)", VIA_WINDOW, window, thread);
    for (unsigned i = 0; i < 3u; ++i)
        printf("%s: %u accepted, then error %lu; %u read back, %u in order; a post after removing one: %s\n", runs[i].name,
               runs[i].accepted, (unsigned long)runs[i].error, runs[i].retrieved, runs[i].in_order,
               runs[i].again_after_one ? "accepted" : "refused");
    if (window) DestroyWindow(window);

    for (unsigned i = 0; i < 3u; ++i) {
        CHECK(runs[i].error == ERROR_NOT_ENOUGH_QUOTA);
        CHECK(runs[i].accepted >= 4000u && runs[i].accepted < LIMIT_CEILING);   /* the documented minimum of the setting is 4000 */
        CHECK(runs[i].accepted > 16u);
        CHECK(runs[i].again_after_one == 1u);
        CHECK(runs[i].retrieved == runs[i].accepted + 1u);                      /* all accepted ones, plus the repost */
        CHECK(runs[i].in_order == runs[i].retrieved);
        CHECK(runs[i].accepted == runs[0].accepted);
    }
    if (registry_value >= 0) CHECK((unsigned)registry_value == runs[0].accepted);
    else CHECK(runs[0].accepted == 10000u);
    if (failures) {
        fprintf(stderr, "post_limit_probe: %d check(s) failed\n", failures);
        return 1;
    }
    printf("post_limit_probe: ok\n");
    return 0;
}
