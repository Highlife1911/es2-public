// Deliberately faults only in this own-code test fixture, never the game.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

static LONG CALLBACK handled_exception(EXCEPTION_POINTERS *exception) {
    if (exception->ExceptionRecord->ExceptionCode == 0xe0421001)
        return EXCEPTION_CONTINUE_EXECUTION;
    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG CALLBACK repeated_fault(EXCEPTION_POINTERS *exception) {
    static unsigned repeats;
    if (exception->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
        return EXCEPTION_CONTINUE_SEARCH;
    if (!IsDebuggerPresent()) {
        HANDLE marker = CreateFileW(L"fixture-storm-detached.txt", GENERIC_WRITE, FILE_SHARE_READ,
            NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (marker == INVALID_HANDLE_VALUE) ExitProcess(10);
        CloseHandle(marker);
        ExitProcess(0);
    }
    if (++repeats > 2000) ExitProcess(11);
    // Deliberately return to the same faulting instruction, reproducing the
    // game's observed first-chance exception loop without touching the game.
    return EXCEPTION_CONTINUE_EXECUTION;
}

int wmain(int argc, WCHAR **argv) {
    (void)argv;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc > 1) {
        Sleep(150);
        volatile DWORD * volatile target = (DWORD *)(uintptr_t)1;
        *target = 0x12345678;
        return 99;
    }
    WCHAR mode[32];
    if (!GetEnvironmentVariableW(L"ES2_CAPTURE_FIXTURE_MODE", mode, 32)) return 3;
    if (!wcscmp(mode, L"storm")) {
        if (!AddVectoredExceptionHandler(1, repeated_fault)) return 12;
        // A handled earlier AV at another address must not hide the later
        // repeating fault's context from the bounded diagnostic capture.
        RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, NULL);
        volatile DWORD * volatile target = (DWORD *)(uintptr_t)1;
        *target = 0x12345678;
        return 13;
    }
    if (!wcscmp(mode, L"handled")) {
        PVOID handler = AddVectoredExceptionHandler(1, handled_exception);
        if (!handler) return 4;
        RaiseException(0xe0421001, 0, 0, NULL);
        RemoveVectoredExceptionHandler(handler);
        return 0;
    }
    if (!wcscmp(mode, L"detach")) {
        Sleep(2000);
        HANDLE marker = CreateFileW(L"fixture-survived.txt", GENERIC_WRITE, FILE_SHARE_READ,
            NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (marker == INVALID_HANDLE_VALUE) return 5;
        CloseHandle(marker);
        return 0;
    }
    if (wcscmp(mode, L"tree-crash")) return 6;
    WCHAR executable[MAX_PATH], command[MAX_PATH + 32];
    if (!GetModuleFileNameW(NULL, executable, MAX_PATH)) return 7;
    swprintf(command, MAX_PATH + 32, L"\"%ls\" --crash-child", executable);
    STARTUPINFOW startup = {0};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child = {0};
    /* window-lint: allow inherits console: the child is this fixture, started under a Bazel test or a hidden script and sharing its console */
    if (!CreateProcessW(executable, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &child)) return 8;
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    // The helper must keep following this child even after the root exits.
    return 0;
}
