// Local x86 child-tree crash capture. No target-memory writes, patches or hooks.
// Debug event process/thread handles are closed by Windows at their exit events;
// tracked process handles below are our own duplicates.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <fcntl.h>
#include <io.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#if !defined(_M_IX86) && !defined(__i386__)
#error Build this helper for x86, matching the original game.
#endif

#define PATH_CAP 4096
#define PROCESS_CAP 128
#define RAPID_AV_LIMIT 32
#define CAPTURE_LIMIT 8
#define EXCEPTION_EVENT_LIMIT 4096

typedef BOOL (WINAPI *DumpWriter)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
    PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION,
    PMINIDUMP_CALLBACK_INFORMATION);

typedef struct {
    DWORD pid;
    HANDLE handle;
    BOOL initial_breakpoint_pending;
    BOOL first_av_captured;
    DWORD captured_av_tid;
    uintptr_t captured_av_address;
    DWORD last_av_tid;
    uintptr_t last_av_address;
    ULONGLONG last_av_time;
    unsigned rapid_av_count;
    WCHAR image[PATH_CAP];
} Process;

static Process processes[PROCESS_CAP];
static FILE *journal;
static volatile LONG stop_requested;
static unsigned crash_sequence;
static BOOL capture_failed;
static char journal_buffer[16384];

static BOOL WINAPI console_control(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT ||
        type == CTRL_LOGOFF_EVENT || type == CTRL_SHUTDOWN_EVENT) {
        InterlockedExchange(&stop_requested, 1);
        return TRUE;
    }
    return FALSE;
}

static void json_string(FILE *file, const WCHAR *value) {
    int bytes = WideCharToMultiByte(CP_UTF8, 0, value, -1, NULL, 0, NULL, NULL);
    char *utf8 = bytes > 0 ? malloc((size_t)bytes) : NULL;
    if (!utf8 || !WideCharToMultiByte(CP_UTF8, 0, value, -1, utf8, bytes, NULL, NULL)) {
        free(utf8);
        fputs("null", file);
        return;
    }
    fputc('"', file);
    for (const unsigned char *p = (const unsigned char *)utf8; *p; ++p) {
        if (*p == '"' || *p == '\\') fprintf(file, "\\%c", *p);
        else if (*p < 32) fprintf(file, "\\u%04x", *p);
        else fputc(*p, file);
    }
    fputc('"', file);
    free(utf8);
}

static void header(FILE *file, const char *kind) {
    SYSTEMTIME now;
    GetSystemTime(&now);
    fprintf(file, "{\"utc\":\"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ\",\"kind\":\"%s\"",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
        now.wMilliseconds, kind);
}

static void error_record(const char *operation, DWORD error) {
    fprintf(stderr, "%s failed (Windows error 0x%08lx).\n", operation, (unsigned long)error);
    if (journal) {
        header(journal, "helper_error");
        fprintf(journal, ",\"operation\":\"%s\",\"error\":%lu}\n", operation, (unsigned long)error);
        fflush(journal);
    }
}

static BOOL join_path(WCHAR *result, const WCHAR *directory, const WCHAR *name) {
    int count = swprintf(result, PATH_CAP, L"%ls\\%ls", directory, name);
    if (count < 0 || count >= PATH_CAP) {
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return FALSE;
    }
    return TRUE;
}

static BOOL full_path(const WCHAR *source, WCHAR *destination) {
    DWORD size = GetFullPathNameW(source, PATH_CAP, destination, NULL);
    if (!size || size >= PATH_CAP) {
        if (size >= PATH_CAP) SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return FALSE;
    }
    return TRUE;
}

static FILE *new_file(const WCHAR *path) {
    HANDLE handle = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE) return NULL;
    int descriptor = _open_osfhandle((intptr_t)handle, _O_BINARY | _O_WRONLY);
    if (descriptor < 0) {
        CloseHandle(handle);
        SetLastError(ERROR_TOO_MANY_OPEN_FILES);
        return NULL;
    }
    FILE *file = _fdopen(descriptor, "wb");
    if (!file) {
        _close(descriptor);
        SetLastError(ERROR_TOO_MANY_OPEN_FILES);
    }
    return file;
}

static Process *find_process(DWORD pid) {
    for (unsigned i = 0; i < PROCESS_CAP; ++i)
        if (processes[i].pid == pid) return &processes[i];
    return NULL;
}

static Process *add_process(const DEBUG_EVENT *event) {
    for (unsigned i = 0; i < PROCESS_CAP; ++i) {
        Process *process = &processes[i];
        if (process->pid) continue;
        if (!DuplicateHandle(GetCurrentProcess(), event->u.CreateProcessInfo.hProcess,
            GetCurrentProcess(), &process->handle, 0, FALSE, DUPLICATE_SAME_ACCESS)) return NULL;
        process->pid = event->dwProcessId;
        process->initial_breakpoint_pending = TRUE;
        DWORD size = PATH_CAP;
        if (!QueryFullProcessImageNameW(process->handle, 0, process->image, &size))
            process->image[0] = L'\0';
        return process;
    }
    SetLastError(ERROR_TOO_MANY_OPEN_FILES);
    return NULL;
}

static void read_bytes(FILE *file, HANDLE process, DWORD address, SIZE_T requested) {
    unsigned char bytes[512] = {0};
    SIZE_T read = 0;
    BOOL success = ReadProcessMemory(process, (LPCVOID)(uintptr_t)address, bytes, requested, &read);
    DWORD error = success ? 0 : GetLastError();
    fprintf(file, "{\"address\":\"0x%08lx\",\"requested\":%lu,\"read\":%lu,\"error\":%lu,\"hex\":\"",
        (unsigned long)address, (unsigned long)requested, (unsigned long)read, (unsigned long)error);
    for (SIZE_T i = 0; i < read; ++i) fprintf(file, "%02x", bytes[i]);
    fputs("\"}", file);
}

static void capture_crash(Process *process, const DEBUG_EVENT *event,
    const WCHAR *output, DumpWriter write_dump) {
    WCHAR name[128], summary_path[PATH_CAP], dump_path[PATH_CAP];
    unsigned sequence = ++crash_sequence;
    swprintf(name, 128, L"crash-%lu-%lu-%u.json", (unsigned long)event->dwProcessId,
        (unsigned long)event->dwThreadId, sequence);
    if (!join_path(summary_path, output, name)) {
        error_record("summary_path", GetLastError());
        capture_failed = TRUE;
        return;
    }
    swprintf(name, 128, L"crash-%lu-%lu-%u.dmp", (unsigned long)event->dwProcessId,
        (unsigned long)event->dwThreadId, sequence);
    if (!join_path(dump_path, output, name)) {
        error_record("dump_path", GetLastError());
        capture_failed = TRUE;
        return;
    }

    CONTEXT context = {0};
    context.ContextFlags = CONTEXT_FULL;
    HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, event->dwThreadId);
    BOOL context_ok = thread && GetThreadContext(thread, &context);
    DWORD context_error = context_ok ? 0 : GetLastError();
    if (thread) CloseHandle(thread);

    FILE *summary = new_file(summary_path);
    if (!summary) {
        error_record("create_crash_summary", GetLastError());
        capture_failed = TRUE;
    }
    EXCEPTION_RECORD record = event->u.Exception.ExceptionRecord;
    // The copied record is local. Its nested ExceptionRecord pointer is remote;
    // omit that pointer rather than let DbgHelp dereference it in this process.
    record.ExceptionRecord = NULL;
    EXCEPTION_POINTERS pointers = {&record, &context};
    MINIDUMP_EXCEPTION_INFORMATION exception = {event->dwThreadId, &pointers, FALSE};
    const MINIDUMP_TYPE dump_type = (MINIDUMP_TYPE)(MiniDumpWithFullMemory |
        MiniDumpWithFullMemoryInfo | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
    HANDLE dump = CreateFileW(dump_path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    BOOL dump_ok = FALSE;
    DWORD dump_error;
    if (dump == INVALID_HANDLE_VALUE) dump_error = GetLastError();
    else {
        dump_ok = write_dump(process->handle, event->dwProcessId, dump, dump_type,
            context_ok ? &exception : NULL, NULL, NULL);
        dump_error = dump_ok ? 0 : GetLastError();
        if (dump_ok && !FlushFileBuffers(dump)) {
            dump_ok = FALSE;
            dump_error = GetLastError();
        }
        CloseHandle(dump);
    }
    if (!dump_ok) error_record("full_memory_dump", dump_error);
    if (!context_ok) error_record("get_crashing_thread_context", context_error);
    if (!dump_ok || !context_ok) capture_failed = TRUE;

    if (summary) {
        BOOL first = event->u.Exception.dwFirstChance != 0;
        header(summary, first ? "first_chance_exception" : "unhandled_exception");
        fprintf(summary, ",\"pid\":%lu,\"tid\":%lu,\"image\":", (unsigned long)event->dwProcessId,
            (unsigned long)event->dwThreadId);
        json_string(summary, process->image);
        fprintf(summary, ",\"exception_code\":\"0x%08lx\",\"exception_address\":\"0x%08lx\",\"first_chance\":%s,\"exception_parameters\":[",
            (unsigned long)record.ExceptionCode, (unsigned long)(uintptr_t)record.ExceptionAddress,
            first ? "true" : "false");
        DWORD count = record.NumberParameters > EXCEPTION_MAXIMUM_PARAMETERS ?
            EXCEPTION_MAXIMUM_PARAMETERS : record.NumberParameters;
        for (DWORD i = 0; i < count; ++i)
            fprintf(summary, "%s\"0x%08lx\"", i ? "," : "", (unsigned long)record.ExceptionInformation[i]);
        fprintf(summary, "],\"context_ok\":%s,\"context_error\":%lu,\"registers\":",
            context_ok ? "true" : "false", (unsigned long)context_error);
        if (context_ok) {
            fprintf(summary, "{\"eax\":\"0x%08lx\",\"ebx\":\"0x%08lx\",\"ecx\":\"0x%08lx\",\"edx\":\"0x%08lx\",\"esi\":\"0x%08lx\",\"edi\":\"0x%08lx\",\"ebp\":\"0x%08lx\",\"esp\":\"0x%08lx\",\"eip\":\"0x%08lx\",\"eflags\":\"0x%08lx\"}",
                (unsigned long)context.Eax, (unsigned long)context.Ebx, (unsigned long)context.Ecx,
                (unsigned long)context.Edx, (unsigned long)context.Esi, (unsigned long)context.Edi,
                (unsigned long)context.Ebp, (unsigned long)context.Esp, (unsigned long)context.Eip,
                (unsigned long)context.EFlags);
            fputs(",\"stack\":", summary);
            read_bytes(summary, process->handle, context.Esp, 512);
            fputs(",\"instruction_bytes\":", summary);
            read_bytes(summary, process->handle, context.Eip, 64);
            fputs(",\"ebx_memory\":", summary);
            read_bytes(summary, process->handle, context.Ebx, 64);
        } else fputs("null", summary);
        fputs(",\"dump_path\":", summary);
        json_string(summary, dump_path);
        fprintf(summary, ",\"dump_type\":%u,\"full_memory_dump_ok\":%s,\"dump_error\":%lu}\n",
            (unsigned)dump_type, dump_ok ? "true" : "false", (unsigned long)dump_error);
        BOOL summary_ok = !ferror(summary);
        if (fclose(summary) != 0) summary_ok = FALSE;
        if (!summary_ok) {
            error_record("write_crash_summary", ERROR_WRITE_FAULT);
            capture_failed = TRUE;
        }
    }
    header(journal, "crash_capture");
    fprintf(journal, ",\"pid\":%lu,\"tid\":%lu,\"summary_path\":", (unsigned long)event->dwProcessId,
        (unsigned long)event->dwThreadId);
    json_string(journal, summary_path);
    fputs(",\"dump_path\":", journal);
    json_string(journal, dump_path);
    fprintf(journal, ",\"first_chance\":%s,\"full_memory_dump_ok\":%s}\n",
        event->u.Exception.dwFirstChance ? "true" : "false", dump_ok ? "true" : "false");
    fflush(journal);
    wprintf(L"Exception captured: %ls\n", summary_path);
}

static void detach_all(void) {
    for (unsigned i = 0; i < PROCESS_CAP; ++i) {
        Process *process = &processes[i];
        if (!process->pid) continue;
        BOOL detached = DebugActiveProcessStop(process->pid);
        DWORD error = detached ? 0 : GetLastError();
        header(journal, "detach");
        fprintf(journal, ",\"pid\":%lu,\"ok\":%s,\"error\":%lu}\n", (unsigned long)process->pid,
            detached ? "true" : "false", (unsigned long)error);
        CloseHandle(process->handle);
        ZeroMemory(process, sizeof(*process));
    }
}

int wmain(int argc, WCHAR **argv) {
    const WCHAR *exe_arg = NULL, *directory_arg = NULL, *output_arg = NULL;
    for (int i = 1; i < argc; ++i) {
        if (i + 1 < argc && !wcscmp(argv[i], L"--exe") && !exe_arg) exe_arg = argv[++i];
        else if (i + 1 < argc && !wcscmp(argv[i], L"--working-directory") && !directory_arg)
            directory_arg = argv[++i];
        else if (i + 1 < argc && !wcscmp(argv[i], L"--output") && !output_arg) output_arg = argv[++i];
        else {
            fwprintf(stderr, L"Unknown, duplicate, or incomplete argument: %ls\n", argv[i]);
            return 2;
        }
    }
    if (!exe_arg || !directory_arg || !output_arg) {
        fputs("Usage: capture_crash.exe --exe PATH --working-directory DIR --output FRESH_DIR\n", stderr);
        return 2;
    }
    WCHAR executable[PATH_CAP], directory[PATH_CAP], output[PATH_CAP], path[PATH_CAP];
    if (!full_path(exe_arg, executable) || !full_path(directory_arg, directory) || !full_path(output_arg, output)) {
        error_record("resolve_paths", GetLastError());
        return 2;
    }
    DWORD binary_type;
    if (!GetBinaryTypeW(executable, &binary_type) || binary_type != SCS_32BIT_BINARY) {
        fputs("The target must be an existing 32-bit Windows executable.\n", stderr);
        return 2;
    }
    DWORD attributes = GetFileAttributesW(directory);
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        fputs("The working directory does not exist.\n", stderr);
        return 2;
    }
    if (!CreateDirectoryW(output, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        error_record("create_output_directory", GetLastError());
        return 2;
    }
    if (!join_path(path, output, L"events.jsonl") || !(journal = new_file(path))) {
        error_record("create_new_event_log", GetLastError());
        return 2;
    }
    // Buffer complete records, then flush at event boundaries. The previous
    // unbuffered stream turned every fprintf fragment into separate I/O.
    setvbuf(journal, journal_buffer, _IOFBF, sizeof(journal_buffer));
    setvbuf(stdout, NULL, _IONBF, 0);
    UINT system_length = GetSystemDirectoryW(path, PATH_CAP);
    if (!system_length || system_length + 13 >= PATH_CAP) {
        error_record("system_directory", ERROR_FILENAME_EXCED_RANGE);
        fclose(journal);
        return 2;
    }
    wcscat(path, L"\\dbghelp.dll");
    HMODULE dbghelp = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    DumpWriter write_dump = dbghelp ? (DumpWriter)GetProcAddress(dbghelp, "MiniDumpWriteDump") : NULL;
    if (!write_dump) {
        error_record("load_windows_dbghelp", GetLastError());
        if (dbghelp) FreeLibrary(dbghelp);
        fclose(journal);
        return 2;
    }
    WCHAR command[PATH_CAP + 3];
    swprintf(command, PATH_CAP + 3, L"\"%ls\"", executable);
    STARTUPINFOW startup = {0};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child = {0};
    SetConsoleCtrlHandler(console_control, TRUE);
    /* window-lint: allow gui-child ES.EXE (Win32 GUI program); the helper itself is the console of START_ES2_DIAGNOSE.cmd, visible on purpose */
    if (!CreateProcessW(executable, command, NULL, NULL, FALSE, DEBUG_PROCESS | CREATE_NEW_PROCESS_GROUP,
        NULL, directory, &startup, &child)) {
        error_record("create_debugged_process", GetLastError());
        FreeLibrary(dbghelp);
        fclose(journal);
        return 2;
    }
    // Windows requires a debugging connection before this call. Do it before
    // processing/continuing the first event, while the target is still stopped.
    if (!DebugSetProcessKillOnExit(FALSE)) {
        error_record("disable_debugger_kill_on_exit", GetLastError());
        // Never exit with an attached target while the default kill flag is set.
        // A failed policy change is exceptional; wait until safe detach succeeds.
        while (!DebugActiveProcessStop(child.dwProcessId)) {
            if (WaitForSingleObject(child.hProcess, 250) == WAIT_OBJECT_0) break;
            if (DebugSetProcessKillOnExit(FALSE)) break;
        }
        CloseHandle(child.hThread);
        CloseHandle(child.hProcess);
        FreeLibrary(dbghelp);
        fclose(journal);
        return 2;
    }
    header(journal, "session_start");
    fprintf(journal, ",\"root_pid\":%lu,\"helper_pid\":%lu,\"helper_version\":2,\"debugger_kill_on_exit\":false,\"executable\":",
        (unsigned long)child.dwProcessId, (unsigned long)GetCurrentProcessId());
    json_string(journal, executable);
    fputs(",\"working_directory\":", journal);
    json_string(journal, directory);
    fputs("}\n", journal);
    wprintf(L"Watching game process tree. Local diagnostics: %ls\n", output);
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);

    unsigned active = 0;
    unsigned exception_events = 0;
    int result = 0;
    for (;;) {
        if (InterlockedCompareExchange(&stop_requested, 0, 0)) { result = 130; break; }
        DEBUG_EVENT event = {0};
        if (!WaitForDebugEvent(&event, 250)) {
            DWORD error = GetLastError();
            if (error == ERROR_SEM_TIMEOUT) continue;
            error_record("wait_for_debug_event", error);
            result = 2;
            break;
        }
        DWORD continuation = DBG_CONTINUE;
        Process *process = find_process(event.dwProcessId);
        BOOL finished = FALSE, abort_loop = FALSE;
        switch (event.dwDebugEventCode) {
        case CREATE_PROCESS_DEBUG_EVENT:
            process = add_process(&event);
            if (event.u.CreateProcessInfo.hFile) CloseHandle(event.u.CreateProcessInfo.hFile);
            if (!process) {
                error_record("track_child_process", GetLastError());
                // The global no-kill setting also covers this untracked child.
                result = 2;
                abort_loop = TRUE;
                break;
            }
            ++active;
            header(journal, "process_start");
            fprintf(journal, ",\"pid\":%lu,\"image_base\":\"0x%08lx\",\"image\":",
                (unsigned long)process->pid, (unsigned long)(uintptr_t)event.u.CreateProcessInfo.lpBaseOfImage);
            json_string(journal, process->image);
            fputs("}\n", journal);
            break;
        case LOAD_DLL_DEBUG_EVENT:
            if (event.u.LoadDll.hFile) CloseHandle(event.u.LoadDll.hFile);
            break;
        case EXCEPTION_DEBUG_EVENT: {
            DWORD code = event.u.Exception.ExceptionRecord.ExceptionCode;
            BOOL first = event.u.Exception.dwFirstChance != 0;
            continuation = DBG_EXCEPTION_NOT_HANDLED;
            if (process && first && code == EXCEPTION_BREAKPOINT && process->initial_breakpoint_pending) {
                process->initial_breakpoint_pending = FALSE;
                continuation = DBG_CONTINUE;
                header(journal, "initial_breakpoint");
                fprintf(journal, ",\"pid\":%lu}\n", (unsigned long)event.dwProcessId);
                break;
            }
            ++exception_events;
            BOOL av = first && code == EXCEPTION_ACCESS_VIOLATION;
            unsigned repeats = 1;
            if (av && process) {
                ULONGLONG now = GetTickCount64();
                uintptr_t address = (uintptr_t)event.u.Exception.ExceptionRecord.ExceptionAddress;
                if (process->last_av_tid == event.dwThreadId && process->last_av_address == address &&
                    now - process->last_av_time <= 1000) ++process->rapid_av_count;
                else process->rapid_av_count = 1;
                process->last_av_tid = event.dwThreadId;
                process->last_av_address = address;
                process->last_av_time = now;
                repeats = process->rapid_av_count;
            }
            // First occurrence, powers of two, and final limit only. A broken
            // exception handler must not produce an unbounded flood of records.
            if (repeats == 1 || (repeats & (repeats - 1)) == 0 || repeats >= RAPID_AV_LIMIT) {
                header(journal, "exception");
                fprintf(journal, ",\"pid\":%lu,\"tid\":%lu,\"code\":\"0x%08lx\",\"address\":\"0x%08lx\",\"first_chance\":%s,\"rapid_repeat_count\":%u}\n",
                    (unsigned long)event.dwProcessId, (unsigned long)event.dwThreadId, (unsigned long)code,
                    (unsigned long)(uintptr_t)event.u.Exception.ExceptionRecord.ExceptionAddress,
                    first ? "true" : "false", repeats);
            }
            uintptr_t exception_address = (uintptr_t)event.u.Exception.ExceptionRecord.ExceptionAddress;
            BOOL uncaptured_storm = process && repeats >= RAPID_AV_LIMIT &&
                (process->captured_av_tid != event.dwThreadId || process->captured_av_address != exception_address);
            if (process && (!first || (av && (!process->first_av_captured || uncaptured_storm)))) {
                if (av) {
                    process->first_av_captured = TRUE;
                    process->captured_av_tid = event.dwThreadId;
                    process->captured_av_address = exception_address;
                }
                if (crash_sequence < CAPTURE_LIMIT) capture_crash(process, &event, output, write_dump);
            }
            if (repeats >= RAPID_AV_LIMIT || exception_events >= EXCEPTION_EVENT_LIMIT || crash_sequence >= CAPTURE_LIMIT) {
                header(journal, "diagnostic_limit");
                fprintf(journal, ",\"reason\":\"%s\",\"pid\":%lu,\"rapid_repeat_count\":%u,\"exception_events\":%u,\"captures\":%u,\"action\":\"detach_without_terminating\"}\n",
                    repeats >= RAPID_AV_LIMIT ? "repeated_access_violation" :
                    exception_events >= EXCEPTION_EVENT_LIMIT ? "exception_event_limit" : "capture_limit",
                    (unsigned long)event.dwProcessId, repeats, exception_events, crash_sequence);
                fputs("Repeated exceptions or diagnostic limit reached. Detaching without terminating the game.\n", stderr);
                result = capture_failed ? 2 : 1;
                abort_loop = TRUE;
            }
            break;
        }
        case EXIT_PROCESS_DEBUG_EVENT:
            header(journal, "process_exit");
            fprintf(journal, ",\"pid\":%lu,\"exit_code\":%lu}\n", (unsigned long)event.dwProcessId,
                (unsigned long)event.u.ExitProcess.dwExitCode);
            if (process) {
                CloseHandle(process->handle);
                ZeroMemory(process, sizeof(*process));
                if (active) --active;
            }
            finished = active == 0;
            break;
        default:
            break;
        }
        if (!ContinueDebugEvent(event.dwProcessId, event.dwThreadId, continuation)) {
            error_record("continue_debug_event", GetLastError());
            result = 2;
            break;
        }
        if (fflush(journal) != 0 || ferror(journal)) {
            fputs("Diagnostic event log write failed; detaching.\n", stderr);
            result = 2;
            break;
        }
        if (finished || abort_loop) break;
    }
    detach_all();
    if (!result) result = capture_failed ? 2 : crash_sequence ? 1 : 0;
    header(journal, "session_end");
    fprintf(journal, ",\"captures\":%u,\"exception_events\":%u,\"exit_code\":%d}\n", crash_sequence, exception_events, result);
    if (fclose(journal) != 0) result = 2;
    FreeLibrary(dbghelp);
    return result;
}
