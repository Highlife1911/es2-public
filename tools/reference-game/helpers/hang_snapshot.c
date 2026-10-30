// Read-only snapshot of a (possibly hung) 32-bit game process: all top-level
// windows of the process, every thread's registers and stack return-address
// candidates, and a full-memory minidump. Threads are suspended only while
// their context is read and are resumed immediately; nothing is written to
// the target. The process is identified by PID and must run the expected
// executable path, otherwise nothing is done.
//
// Usage: hang_snapshot.exe --pid PID --exe PATH --output FRESH_DIR
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

typedef BOOL (WINAPI *DumpFunction)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                     PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
static FILE *report;
static DWORD target_pid;
static unsigned window_count;

static void json_string(const WCHAR *text) {
    fputc('"', report);
    for (; *text; ++text) {
        if (*text == L'"' || *text == L'\\') fprintf(report, "\\%c", (char)*text);
        else if (*text < 0x20 || *text > 0x7e) fprintf(report, "\\u%04x", (unsigned)*text);
        else fputc((char)*text, report);
    }
    fputc('"', report);
}

static BOOL CALLBACK on_window(HWND window, LPARAM unused) {
    (void)unused;
    DWORD pid = 0, tid = GetWindowThreadProcessId(window, &pid);
    if (pid != target_pid) return TRUE;
    WCHAR title[256] = L"", class_name[256] = L"";
    GetWindowTextW(window, title, 256);
    GetClassNameW(window, class_name, 256);
    RECT rect = {0};
    GetWindowRect(window, &rect);
    fprintf(report, "%s{\"hwnd\":\"0x%08lx\",\"tid\":%lu,\"class\":", window_count++ ? "," : "",
            (unsigned long)(uintptr_t)window, (unsigned long)tid);
    json_string(class_name);
    fputs(",\"title\":", report);
    json_string(title);
    fprintf(report, ",\"visible\":%s,\"enabled\":%s,\"style\":\"0x%08lx\",\"owner\":\"0x%08lx\",\"rect\":[%ld,%ld,%ld,%ld]}\n",
            IsWindowVisible(window) ? "true" : "false", IsWindowEnabled(window) ? "true" : "false",
            (unsigned long)GetWindowLongW(window, GWL_STYLE), (unsigned long)(uintptr_t)GetWindow(window, GW_OWNER),
            rect.left, rect.top, rect.right, rect.bottom);
    return TRUE;
}

static void thread_report(HANDLE process, DWORD tid, BOOL first) {
    fprintf(report, "%s{\"tid\":%lu", first ? "" : ",", (unsigned long)tid);
    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid);
    if (!thread) { fprintf(report, ",\"error\":\"OpenThread %lu\"}\n", GetLastError()); return; }
    CONTEXT context = {0};
    context.ContextFlags = CONTEXT_FULL;
    DWORD suspended = SuspendThread(thread);
    BOOL ok = suspended != (DWORD)-1 && GetThreadContext(thread, &context);
    DWORD stack[256];
    SIZE_T got = 0;
    if (ok) ReadProcessMemory(process, (LPCVOID)(uintptr_t)context.Esp, stack, sizeof(stack), &got);
    if (suspended != (DWORD)-1) ResumeThread(thread);
    CloseHandle(thread);
    if (!ok) { fprintf(report, ",\"error\":\"context\"}\n"); return; }
    fprintf(report, ",\"eip\":\"0x%08lx\",\"esp\":\"0x%08lx\",\"ebp\":\"0x%08lx\",\"eax\":\"0x%08lx\",\"ebx\":\"0x%08lx\","
                    "\"ecx\":\"0x%08lx\",\"edx\":\"0x%08lx\",\"esi\":\"0x%08lx\",\"edi\":\"0x%08lx\",\"previous_suspend\":%lu",
            context.Eip, context.Esp, context.Ebp, context.Eax, context.Ebx, context.Ecx, context.Edx, context.Esi,
            context.Edi, (unsigned long)suspended);
    // Candidate return addresses: stack words inside the main image's code range.
    fputs(",\"stack_code_candidates\":[", report);
    unsigned printed = 0;
    for (SIZE_T i = 0; i < got / 4 && printed < 48; ++i) {
        if (stack[i] >= 0x00401000u && stack[i] < 0x00497000u)
            fprintf(report, "%s[\"0x%08lx\",\"0x%08lx\"]", printed++ ? "," : "",
                    (unsigned long)(context.Esp + 4 * i), (unsigned long)stack[i]);
    }
    fputs("]}\n", report);
}

int wmain(int argc, WCHAR **argv) {
    const WCHAR *exe = NULL, *output = NULL;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!wcscmp(argv[i], L"--pid")) target_pid = wcstoul(argv[i + 1], NULL, 10);
        else if (!wcscmp(argv[i], L"--exe")) exe = argv[i + 1];
        else if (!wcscmp(argv[i], L"--output")) output = argv[i + 1];
    }
    if (!target_pid || !exe || !output) {
        fputs("Usage: hang_snapshot.exe --pid PID --exe PATH --output FRESH_DIR\n", stderr);
        return 2;
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, target_pid);
    if (!process) { fprintf(stderr, "OpenProcess failed: %lu\n", GetLastError()); return 3; }
    WCHAR image[MAX_PATH];
    DWORD size = MAX_PATH;
    if (!QueryFullProcessImageNameW(process, 0, image, &size) || _wcsicmp(image, exe)) {
        fwprintf(stderr, L"Process %lu does not run %ls\n", (unsigned long)target_pid, exe);
        return 4;
    }
    if (!CreateDirectoryW(output, NULL)) { fprintf(stderr, "Output directory must be new\n"); return 5; }
    WCHAR path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%ls\\snapshot.json", output);
    report = _wfopen(path, L"w");
    if (!report) return 6;
    fprintf(report, "{\"pid\":%lu,\"exe\":", (unsigned long)target_pid);
    json_string(image);
    fputs(",\"windows\":[\n", report);
    EnumWindows(on_window, 0);
    fputs("],\"threads\":[\n", report);
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 entry;
    memset(&entry, 0, sizeof(entry));
    entry.dwSize = sizeof(entry);
    BOOL first = TRUE;
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != target_pid) continue;
        thread_report(process, entry.th32ThreadID, first);
        first = FALSE;
    }
    CloseHandle(snapshot);
    WCHAR library[MAX_PATH];
    GetSystemDirectoryW(library, MAX_PATH);
    wcscat(library, L"\\dbghelp.dll");
    HMODULE dbghelp = LoadLibraryExW(library, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    DumpFunction dump = dbghelp ? (DumpFunction)(void *)GetProcAddress(dbghelp, "MiniDumpWriteDump") : NULL;
    swprintf(path, MAX_PATH, L"%ls\\snapshot.dmp", output);
    HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    BOOL dumped = dump && file != INVALID_HANDLE_VALUE &&
        dump(process, target_pid, file, (MINIDUMP_TYPE)(MiniDumpWithFullMemory | MiniDumpWithFullMemoryInfo | MiniDumpWithThreadInfo),
             NULL, NULL, NULL);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    fprintf(report, "],\"minidump\":%s}\n", dumped ? "true" : "false");
    fclose(report);
    CloseHandle(process);
    printf("snapshot written: %u windows, minidump %s\n", window_count, dumped ? "ok" : "failed");
    return dumped ? 0 : 7;
}
