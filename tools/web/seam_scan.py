#!/usr/bin/env python3
"""Seam scanner: lists the x86 dependencies of C and assembly sources per site.

The categories are those of docs/web_build.md section 4.9 (the static check):
inline asm, naked functions, %fs and ESP reads, `call *` to guest VAs and casts
of guest VAs to function pointers, raw guest->host casts and Win32 calls,
`wchar_t`, `long double`, bitfields, `#pragma pack` and `%I64` formats, fault
trampolines (a handler that moves a faulting thread's EIP or ESP), plus the
float->int conversion sites of the shell and the launcher (informational).
Every hit carries the replacement that web_build.md names for it.

  seam_scan.py PATH... [--sources LIST]... [--root DIR] [--allowlist FILE]
               [--forbid] [--json OUT] [--summary] [--quiet]
               [--include-tests] [--float-int-scope PREFIX]...

PATH is a source file or a directory (scanned recursively for .c .h .S .s .inc;
directories named tests or testdata are skipped unless --include-tests). LIST is
a text file with one path per line ('#' starts a comment), so a Bazel rule can
hand over the exact source list of a link. Reported paths are relative to ROOT
(default: the directory with MODULE.bazel above this script, else the current
directory), with '/' separators.

The allowlist (tools/web/seam_allowlist.txt) names the x86-only sources. Its
lines are `pattern [kind,kind...]  # reason`; the last matching line wins and a
leading '!' removes a path from an earlier pattern. With --forbid the exit code
is 1 when a hit of a forbidden kind lies outside the allowlist; informational
kinds never fail the run.

Stdlib only (Python 3.12). Detection is textual: comments are blanked, string
literals are kept only where assembly is expected, and the sub-kinds of an asm
site (fs, esp, call-indirect, stack-probe, idiv, x87) are read from the asm
strings. The float->int detection knows only casts to integer types whose
operand mentions a float/double name of the same file, a float literal or a
libm call; implicit conversions are not seen.
"""

import argparse
import fnmatch
import json
import os
import re
import sys

SOURCE_EXTS = (".c", ".h", ".S", ".s", ".inc")
ASM_EXTS = (".S", ".s")
SKIP_DIRS = ("tests", "testdata")

# Kinds that never fail --forbid.
INFORMATIONAL = frozenset({"float-int", "win32-include", "crt-io", "packed-attr"})

# kind -> replacement named in docs/web_build.md (the section in brackets).
REPLACEMENT = {
    "inline-asm": "C over es2_machine; x86-only parts in the x86 backend or a proxy-only *_x86.h [4.2, 4.4]",
    "asm-source": "x86 backend (hook/machine_x86.S) or the sim backend in C [4.2]",
    "asm-label": "no x86 symbol renaming in portable code [4.9]",
    "naked": "C entry taking es2_machine *; the naked capture part moves to a proxy-only *_x86.h [4.4]",
    "fs-write": "es2_machine_fs0 (a variable in the sim backend, the real %fs:0 in the x86 backend) [4.5]",
    "fs-read": "es2_machine_fs0 [4.5]",
    "fs-stack": "the guest stack's base and limit of the machine (floor compares with the limit, 71 beyond) [4.6]",
    "fs-other": "no TEB access in portable code; the machine owns the thread state [4.5]",
    "esp": "m->esp of the modelled guest stack [4.2, 4.3]",
    "call-indirect": "es2_machine_call(machine, va, abi, args, count) [4.2]",
    "guest-fn-cast": "es2_machine_call(machine, va, abi, args, count) [4.2]",
    "machine-call": "es2_machine_call [4.2]",
    "bridge-seam": "removed; every call goes through the sim backend [4.8]",
    "stack-probe": "empty in the sim backend: no guard pages in the modelled stack or in wasm [4.6]",
    "shim-floor": "compare with the guest stack's limit; passing it ends with 71 [4.6]",
    "idiv": "explicit check before the divide, through the divide primitive in C [6]",
    "trampoline": "no fault redirection: every call reaches its entry through the sim backend; the trap ends with 70 [4.8]",
    "x87": "dropped in the sim backend (fninit/fldcw, ST0 result); the x86 backend keeps fnsave/frstor [4.7]",
    "guest-host-cast": "es2_guest_span(va, length) of the checked accessor [5]",
    "win32-call": "//platform:os [7]",
    "win32-call-guard": "Windows guard: stays in the Windows-only sources; wasm RuntimeError path instead [4.8, 6]",
    "win32-call-platform": "the web platform replaces the Windows platform layer [9, 10, 12]",
    "win32-include": "no <windows.h> in portable sources; //platform:os [7]",
    "crt-io": "//platform:os for files, clocks and the environment [7]",
    "wchar_t": "uint16_t for UTF-16 text (wchar_t is 4 bytes in Emscripten) [5]",
    "long-double": "no long double in the portable link; the ST0 result is dropped [4.7, 5]",
    "bitfield": "plain integer fields with explicit shifts and masks (MS and Itanium layouts differ) [5]",
    "pragma-pack": "a _Static_assert on every offset of the packed struct [5]",
    "packed-attr": "a _Static_assert on every offset of the packed struct [5]",
    "i64-format": "portable format macros (<inttypes.h>) instead of the MinGW-specific %I64 [5]",
    "float-int": "show that the operand stays in range (float->int out of range differs between x86 and wasm) [5]",
}

KINDS = (
    "inline-asm", "asm-source", "asm-label", "naked", "fs-write", "fs-read", "fs-stack", "fs-other", "esp",
    "call-indirect", "guest-fn-cast", "machine-call", "bridge-seam", "stack-probe", "shim-floor", "idiv", "x87", "trampoline",
    "guest-host-cast", "win32-call", "win32-include", "crt-io", "wchar_t", "long-double", "bitfield",
    "pragma-pack", "packed-attr", "i64-format", "float-int",
)

# --- Win32 -------------------------------------------------------------------------------------------------------

GUARD_API = {
    "AddVectoredExceptionHandler", "RemoveVectoredExceptionHandler", "SetUnhandledExceptionFilter",
    "UnhandledExceptionFilter", "RaiseException", "SetErrorMode", "GetErrorMode", "GetProcessDEPPolicy",
    "SetProcessDEPPolicy", "VirtualProtect", "VirtualQuery", "FlushInstructionCache",
    "GetModuleHandleExW", "GetCurrentProcess", "GetCurrentThreadId", "AcquireSRWLockExclusive",
    "ReleaseSRWLockExclusive",
}
PLATFORM_API = {
    "AdjustWindowRectEx", "CreateIconFromResourceEx", "CreatePalette", "CreateWindowExA", "CreateWindowExW",
    "DefWindowProcA", "DefWindowProcW", "DeleteObject", "DestroyIcon", "DestroyWindow", "DispatchMessageA",
    "DispatchMessageW", "GetBitmapBits", "GetCapture", "GetClientRect", "GetCursorPos", "GetDC", "GetDIBits",
    "GetIconInfo", "GetKeyboardLayout", "GetKeyboardState", "GetKeyState", "GetObjectA", "GetQueueStatus",
    "GetStockObject", "GetSystemMetrics", "GetWindowLongPtrA", "IsChild", "LoadCursorA", "LoadImageA",
    "LookupIconIdFromDirectoryEx", "MapVirtualKeyA", "MapVirtualKeyExW", "MapVirtualKeyW", "MessageBoxA",
    "MessageBoxW", "MoveWindow", "MsgWaitForMultipleObjects", "PeekMessageA", "PeekMessageW", "PostMessageA",
    "PostQuitMessage", "PostThreadMessageA", "RegisterClassA", "RegisterClassW", "ReleaseCapture", "ReleaseDC",
    "SendMessageA", "SetCapture", "SetCursorPos", "SetWindowLongPtrA", "ShellExecuteExA", "ToAscii",
    "ToUnicodeEx", "TranslateMessage", "WaitMessage", "ShowCursor", "SetCursor", "ShowWindow", "SetFocus",
    "GetForegroundWindow", "SetForegroundWindow", "UpdateWindow", "InvalidateRect", "BeginPaint", "EndPaint",
    "GetMessageA", "GetMessageW", "mciSendCommandA", "mciSendStringA", "mciGetErrorStringA",
}
OS_API = {
    "CloseHandle", "CompareFileTime", "CopyFileA", "CreateDirectoryA", "CreateDirectoryW", "CreateEventW",
    "CreateFileA", "CreateFileMappingA", "CreateFileW", "CreatePipe", "CreateProcessA", "CreateThread",
    "DeleteCriticalSection", "DeleteFileA", "DeleteFileW", "EnterCriticalSection", "ExitProcess",
    "FileTimeToDosDateTime", "FileTimeToLocalFileTime", "FileTimeToSystemTime", "FindClose", "FindFirstFileA",
    "FindNextFileA", "FindResourceA", "FlushFileBuffers", "FreeEnvironmentStringsA", "FreeLibrary",
    "GetCommandLineA", "GetCurrentDirectoryA", "GetCurrentProcessId", "GetEnvironmentStrings",
    "GetEnvironmentStringsA", "GetEnvironmentVariableA", "GetEnvironmentVariableW", "GetExitCodeProcess",
    "GetExitCodeThread", "GetFileAttributesA", "GetFileAttributesW", "GetFileSize", "GetFileSizeEx",
    "GetFileType", "GetFullPathNameA", "GetFullPathNameW", "GetLastError", "GetLocalTime", "GetModuleFileNameA",
    "GetModuleFileNameW", "GetModuleHandleA", "GetModuleHandleW", "GetPrivateProfileIntW",
    "GetPrivateProfileStringA", "GetPrivateProfileStringW", "GetProcAddress", "GetProcessHeap",
    "GetStartupInfoA", "GetStdHandle", "GetSystemInfo", "GetSystemTime", "GetSystemTimeAsFileTime",
    "GetTempPathA", "GetTempPathW", "GetTickCount", "GetTickCount64", "GetTimeZoneInformation", "GetVersion",
    "GlobalAlloc", "GlobalFree", "GlobalHandle", "GlobalLock", "GlobalMemoryStatus", "GlobalUnlock",
    "HeapAlloc", "HeapFree", "HeapReAlloc", "InitOnceBeginInitialize", "InitOnceExecuteOnce",
    "InitializeCriticalSection", "LeaveCriticalSection", "LoadLibraryA", "LoadLibraryExA", "LoadLibraryExW",
    "LoadLibraryW", "LoadResource", "LockResource", "MapViewOfFile", "MoveFileExA", "MoveFileExW",
    "MultiByteToWideChar", "QueryPerformanceCounter", "QueryPerformanceFrequency", "ReadFile",
    "SetCurrentDirectoryA", "SetEnvironmentVariableA", "SetEnvironmentVariableW", "SetEvent", "SetFilePointer",
    "SetFilePointerEx", "SetHandleInformation", "SetLastError", "SizeofResource", "Sleep", "SwitchToFiber",
    "SystemTimeToFileTime", "TerminateProcess", "UnmapViewOfFile", "WaitForMultipleObjects",
    "WaitForSingleObject", "WideCharToMultiByte", "WritePrivateProfileStringW", "WriteFile", "YieldProcessor",
    "MemoryBarrier", "ConvertThreadToFiber", "CreateFiber", "DeleteFiber", "timeGetTime", "timeBeginPeriod",
    "timeEndPeriod", "OutputDebugStringA", "GetCurrentThread", "ExitThread", "SuspendThread", "ResumeThread",
    "TlsAlloc", "TlsGetValue", "TlsSetValue", "FormatMessageA", "lstrcpyA", "lstrlenA", "wsprintfA",
}
WIN32_FAMILY = re.compile(
    r"Interlocked\w+|Virtual(?:Alloc|Free|Protect|Query|Lock|Unlock)\w*|Crypt\w+|Reg(?:Open|Close|Query|Set|Create|Delete|Enum)\w*|"
    r"Create(?:Mutex|Semaphore|Event|File|Thread|Process)\w*|mci[A-Z]\w+|Heap(?:Create|Destroy|Size|Alloc|Free|ReAlloc)")

WIN32_NAMES = GUARD_API | PLATFORM_API | OS_API
_NAME_ALT = "|".join(sorted((re.escape(n) for n in WIN32_NAMES), key=len, reverse=True))
WIN32_CALL = re.compile(r"(?<![\w.>])(" + _NAME_ALT + r"|" + WIN32_FAMILY.pattern + r")\s*\(")
WIN32_INCLUDE = re.compile(
    r"#\s*include\s*<\s*(windows|winsock2?|winnt|mmsystem|mmreg|shellapi|commctrl|ddraw|dsound|dinput|"
    r"io|direct|process|conio|tchar|wchar)\.h\s*>")

CRT_IO = re.compile(
    r"(?<![\w.>])(fopen|_wfopen|fopen_s|freopen|fread|fwrite|fclose|fflush|fseek|_fseeki64|ftell|_ftelli64|"
    r"remove|rename|_unlink|unlink|_mkdir|_wmkdir|mkdir|rmdir|getcwd|_getcwd|_chdir|chdir|_open|_wopen|"
    r"_read|_write|_close|_lseek|_lseeki64|_chsize|_stat|_wstat|_stati64|stat|getenv|_wgetenv|_putenv|time|clock|"
    r"gettimeofday|localtime|gmtime|mktime|opendir|readdir|system|_spawnl|_spawnv|_wspawnv|_wsystem)\s*\(")

# --- Other patterns -----------------------------------------------------------------------------------------------

ASM_KEYWORD = re.compile(r"\b(__asm__|__asm|asm)\b")
ASM_QUALIFIERS = re.compile(r"\s*(?:volatile|__volatile__|__volatile|inline|__inline__|goto)\b")
NAKED = re.compile(r"\bnaked\b")
RAW_CAST = re.compile(
    r"\(\s*(?:const\s+|volatile\s+)*(?:(?:struct|union|enum)\s+)?[A-Za-z_]\w*(?:\s+[A-Za-z_]\w*)*\s*\*+\s*\)"
    r"\s*\(\s*uintptr_t\s*\)")
FN_TYPEDEF = re.compile(r"\btypedef\b[^;{}]*?\(\s*(?:__\w+\s+)?\*\s*([A-Za-z_]\w*)\s*\)\s*\(")
INLINE_FN_CAST = re.compile(r"\(\s*[A-Za-z_][\w\s\*]*\(\s*(?:__\w+\s*)?\*\s*\)\s*\([^()]*\)\s*\)")
GUEST_OPERAND = re.compile(r"uintptr_t|_TARGET\b|BRIDGE|\bimage\b|\bva\b|\brva\b|\bvoid\s*\*")
GUEST_TARGET_MACRO = re.compile(r"\b(MATH_GAME_TARGET|MATH_GAME1)\b")
BRIDGE_SEAM = re.compile(r"\b(ES2_SIM_BRIDGE_ARG8|ES2_SIM_BRIDGE|es2_sim_bridge_target)\b")
MACHINE_CALL = re.compile(r"\b(es2_hook_machine_call)\b")
SHIM_FLOOR = re.compile(r"\bES2_SHIM_FLOOR\b")
# A fault handler that continues the thread elsewhere: a CONTEXT's EIP or ESP written.
CONTEXT_REDIRECT = re.compile(r"(?:->|\.)\s*(Eip|Esp)\s*(?:[-+]?=)(?!=)")
ST0 = re.compile(r"\b(ES2_RESULT_ST0|es2_hook_fpu_save|es2_hook_fpu_restore)\b")
INTRINSIC_FS = re.compile(r"\b(__readfsdword|__readfsbyte|__readfsword|__writefsdword)\b")
INTRINSIC_TEB = re.compile(r"\bNtCurrentTeb\b")
INTRINSIC_ESP = re.compile(r"\b(__builtin_frame_address|__builtin_return_address|_AddressOfReturnAddress)\b")
WCHAR = re.compile(r"\b(wchar_t|WCHAR|LPWSTR|LPCWSTR|PWSTR|PCWSTR|TCHAR)\b")
WIDE_LITERAL = re.compile(r"(?<![\w\"'])L(?=[\"'])")
LONG_DOUBLE = re.compile(r"\blong\s+double\b|\bLDBL_\w+\b")
PRAGMA_PACK = re.compile(r"^[ \t]*#[ \t]*pragma[ \t]+pack\b", re.M)
PACKED_ATTR = re.compile(r"\b(?:__attribute__\s*\(\([^;{}]*?\bpacked\b|__packed\b)")
I64_FORMAT = re.compile(r"%[-+ #0]*[0-9*]*(?:\.[0-9*]+)?I(?:64|32)")
STRUCT_HEAD = re.compile(
    r"\b(struct|union)\b\s*(\w*)\s*(?:__attribute__\s*\(\((?:[^()]|\([^()]*\))*\)\)\s*)*$")
BITFIELD_CHUNK = re.compile(
    r"(?<![?:])\b[A-Za-z_]\w*\s*:\s*(?:0[xX][0-9a-fA-F]+|\d+)[uUlL]*\s*(?:,|$)|^[^?:(){}\[\]]*\b(?:int|char|short|long|unsigned|signed|u?int\d+_t|BYTE|WORD|DWORD|bool|_Bool)\b\s*:\s*(?:0[xX][0-9a-fA-F]+|\d+)")

# Asm sub-kinds, read from the asm strings.
ASM_FS = re.compile(r"%+fs:\s*(\[|\()?\s*(0[xX][0-9a-fA-F]+|\d+|[A-Za-z_]\w*)?")
ASM_FS_WRITE = re.compile(r"(?:,\s*|\bpop[lwq]?\s+)%+fs:")
ASM_ESP = re.compile(r"%+esp\b")
ASM_CALL_INDIRECT = re.compile(r"\b(?:call|jmp)[lwq]?\s+\*")
ASM_PROBE = re.compile(r"\bor[lbw]\s+\$0\s*,\s*\(\s*%+esp\s*\)")
ASM_IDIV = re.compile(r"\bi?div[lwb]?\s")
ASM_X87 = re.compile(
    r"\b(f(?:n?init|ld\w*|n?st\w*|nsave|save|rstor|add\w*|sub\w*|mul\w*|div\w*|xch|chs|abs|sqrt|com\w*|ucom\w*|"
    r"ist\w*|ild\w*|wait|nclex|clex|ldcw))\b")

FLOAT_DECL = re.compile(r"\b(?:float|double)\b([^;(){}]*)")
FLOAT_NAMES_IN_DECL = re.compile(r"(?:^|,)\s*\**\s*([A-Za-z_]\w*)")
FLOAT_LITERAL = re.compile(r"(?<![\w.])(?:\d+\.\d*|\.\d+)(?:[eE][+-]?\d+)?[fFlL]?(?![\w.])|(?<![\w.])\d+[eE][+-]?\d+[fFlL]?")
FLOAT_FUNC = re.compile(r"\b(?:__builtin_)?(?:floor|ceil|round|trunc|sqrt|pow|sin|cos|tan|atan2?|fabs|fmod|exp|log|log2|log10)f?\s*\(")
INT_CAST = re.compile(
    r"\(\s*(?:const\s+)?(?:(?:unsigned|signed)(?:\s+(?:long\s+long|long|short|char|int))?|long\s+long|long|short|char|int|"
    r"u?int(?:8|16|32|64)_t|size_t|ptrdiff_t|intptr_t|uintptr_t|DWORD|WORD|BYTE|UINT|LONG|ULONG|"
    r"Sint(?:8|16|32|64)|Uint(?:8|16|32|64))\s*\)")
IDENT = re.compile(r"[A-Za-z_]\w*")

DEFAULT_FLOAT_INT_SCOPE = ("shell/", "launcher/")


# --- Masking --------------------------------------------------------------------------------------------------------

def mask(text, asm_source=False):
    """Returns (code, nostr, strs) of equal length to `text`, newlines preserved.

    code:  comments blanked, string and character literals kept;
    nostr: comments blanked, literal contents blanked (quotes kept);
    strs:  only the contents of string literals, everything else blanked.
    """
    n = len(text)
    code = list(text)
    nostr = list(text)
    strs = [" "] * n
    for i, ch in enumerate(text):
        if ch == "\n":
            strs[i] = "\n"
    i = 0
    at_line_start = True
    while i < n:
        ch = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if ch == "\n":
            at_line_start = True
            i += 1
            continue
        if ch == "/" and nxt == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
            for k in range(i, j):
                code[k] = nostr[k] = " "
            i = j
            continue
        if ch == "/" and nxt == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if text[k] != "\n":
                    code[k] = nostr[k] = " "
            i = j
            at_line_start = False
            continue
        if asm_source and ch == "#" and at_line_start:
            m = re.match(r"[ \t]*#[ \t]*(include|define|undef|if|ifdef|ifndef|else|elif|endif|pragma|error)\b",
                         text[i:i + 40])
            if not m:
                j = text.find("\n", i)
                j = n if j < 0 else j
                for k in range(i, j):
                    code[k] = nostr[k] = " "
                i = j
                continue
        if ch in "\"'":
            q = ch
            j = i + 1
            while j < n and text[j] != q and text[j] != "\n":
                j += 2 if text[j] == "\\" and j + 1 < n else 1
            j = min(j, n)
            for k in range(i + 1, j):
                if text[k] != "\n":
                    nostr[k] = " "
                    if q == '"':
                        strs[k] = text[k]
            i = j + 1
            at_line_start = False
            continue
        if not ch.isspace():
            at_line_start = False
        i += 1
    # Literal backslash-n and backslash-t between asm instructions must not glue words together.
    return "".join(code), "".join(nostr), re.sub(r"\\[ntr]", "  ", "".join(strs))


def line_starts(text):
    starts = [0]
    for m in re.finditer("\n", text):
        starts.append(m.end())
    return starts


def lineno(starts, pos):
    lo, hi = 0, len(starts) - 1
    while lo < hi:
        mid = (lo + hi + 1) // 2
        if starts[mid] <= pos:
            lo = mid
        else:
            hi = mid - 1
    return lo + 1


def match_paren(s, i, open_ch="(", close_ch=")"):
    """Index of the bracket closing the one at s[i], or -1."""
    depth = 0
    for j in range(i, len(s)):
        c = s[j]
        if c == open_ch:
            depth += 1
        elif c == close_ch:
            depth -= 1
            if depth == 0:
                return j
    return -1


def unary_end(s, i):
    """End of the unary expression that starts at s[i] (after a cast)."""
    n = len(s)
    while i < n and s[i].isspace():
        i += 1
    while i < n and s[i] in "-+~!*&":
        i += 1
        while i < n and s[i].isspace():
            i += 1
    if i >= n:
        return i
    if s[i] == "(":
        j = match_paren(s, i)
        i = n if j < 0 else j + 1
    else:
        m = re.compile(r"[A-Za-z_0-9.]+").match(s, i)
        i = m.end() if m else i + 1
    while i < n:
        if s[i] == "(":
            j = match_paren(s, i)
            i = n if j < 0 else j + 1
        elif s[i] == "[":
            j = match_paren(s, i, "[", "]")
            i = n if j < 0 else j + 1
        elif s.startswith("->", i):
            m = IDENT.match(s, i + 2)
            i = m.end() if m else i + 2
        elif s[i] == "." and i + 1 < n and (s[i + 1].isalpha() or s[i + 1] == "_"):
            m = IDENT.match(s, i + 1)
            i = m.end() if m else i + 1
        else:
            break
    return i


# --- Allowlist ------------------------------------------------------------------------------------------------------

def glob_to_regex(pattern):
    out = []
    i = 0
    while i < len(pattern):
        c = pattern[i]
        if c == "*":
            if pattern.startswith("**/", i):
                out.append("(?:.*/)?")
                i += 3
                continue
            if pattern.startswith("**", i):
                out.append(".*")
                i += 2
                continue
            out.append("[^/]*")
        elif c == "?":
            out.append("[^/]")
        else:
            out.append(re.escape(c))
        i += 1
    return re.compile("".join(out) + r"\Z")


class Allowlist:
    def __init__(self, path=None):
        self.rules = []   # (regex, kinds or None, negate, reason, pattern)
        self.path = path
        if path:
            with open(path, encoding="utf-8") as f:
                for number, raw in enumerate(f, 1):
                    line = raw.rstrip("\n")
                    reason = ""
                    if "#" in line:
                        line, reason = line.split("#", 1)
                        reason = reason.strip()
                    line = line.strip()
                    if not line:
                        continue
                    parts = line.split()
                    negate = parts[0].startswith("!")
                    pattern = parts[0][1:] if negate else parts[0]
                    kinds = None
                    if len(parts) > 1:
                        kinds = frozenset(k for k in parts[1].split(",") if k)
                        unknown = kinds - set(KINDS) - {"guard", "platform", "os"}
                        if unknown:
                            raise SystemExit("%s:%d: unknown kind %s" % (path, number, ",".join(sorted(unknown))))
                    if len(parts) > 2:
                        raise SystemExit("%s:%d: expected 'pattern [kinds]  # reason'" % (path, number))
                    self.rules.append((glob_to_regex(pattern), kinds, negate, reason, pattern))

    def lookup(self, rel, kind, group=None):
        """Reason string when (path, kind) is allowed, else None."""
        verdict = None
        for regex, kinds, negate, reason, pattern in self.rules:
            if not regex.match(rel):
                continue
            if kinds is not None and kind not in kinds and group not in kinds:
                continue
            verdict = None if negate else (reason or pattern)
        return verdict


# --- Scanner ----------------------------------------------------------------------------------------------------------

class Hit:
    __slots__ = ("file", "line", "kind", "detail", "symbol", "text", "replacement", "in_naked", "group")

    def __init__(self, file, line, kind, detail, symbol, text, in_naked=False, group=None):
        self.file = file
        self.line = line
        self.kind = kind
        self.detail = detail
        self.symbol = symbol
        self.text = text
        self.in_naked = in_naked
        self.group = group
        rep_key = kind
        if kind == "win32-call" and group in ("guard", "platform"):
            rep_key = "win32-call-" + group
        self.replacement = REPLACEMENT[rep_key]

    def as_dict(self):
        return {"file": self.file, "line": self.line, "kind": self.kind, "detail": self.detail,
                "symbol": self.symbol, "in_naked": self.in_naked, "group": self.group,
                "text": self.text, "replacement": self.replacement}


FUNC_HEADER = re.compile(
    r"^(?:__attribute__\(\(.*?\)\)\s*)*(?:(?:static|inline|extern|__inline__)\s+)*[A-Za-z_][\w\s\*]*?\b([A-Za-z_]\w*)\s*\(")
DEFINE = re.compile(r"^\s*#\s*define\s+([A-Za-z_]\w*)")


def enclosing_symbol(lines, index):
    """Best-effort name of the function or macro that holds lines[index]."""
    cont = True
    for j in range(index, max(-1, index - 120), -1):
        line = lines[j]
        m = DEFINE.match(line)
        if m and cont:
            return m.group(1)
        if j < index and not lines[j].rstrip().endswith("\\"):
            cont = False
        if line[:1] not in (" ", "\t", "#", "}", "") and not line.rstrip().endswith(";"):
            fm = FUNC_HEADER.match(line)
            if fm and fm.group(1) not in ("if", "while", "for", "switch", "return", "sizeof"):
                return fm.group(1)
    return ""


class FileScan:
    def __init__(self, rel, text, fn_types, float_int):
        self.rel = rel
        self.text = text
        self.asm_source = rel.endswith(ASM_EXTS)
        self.code, self.nostr, self.strs = mask(text, self.asm_source)
        self.starts = line_starts(text)
        self.lines = text.split("\n")
        self.fn_types = fn_types
        self.float_int = float_int
        self.hits = []
        self.seen = set()
        self.naked_ranges = []

    def add(self, pos, kind, detail="", group=None, line=None):
        number = line if line is not None else lineno(self.starts, pos)
        key = (number, kind, detail)
        if key in self.seen:
            return
        self.seen.add(key)
        raw = self.lines[number - 1] if number - 1 < len(self.lines) else ""
        in_naked = any(a <= pos < b for a, b in self.naked_ranges)
        self.hits.append(Hit(self.rel, number, kind, detail, enclosing_symbol(self.lines, number - 1),
                             raw.strip()[:160], in_naked, group))

    # asm sub-kinds over a view that holds only asm string contents
    def scan_asm_text(self, view, base):
        for m in ASM_FS.finditer(view):
            tail = view[max(0, m.start() - 12):m.start() + 1]
            write = bool(ASM_FS_WRITE.search(view[max(0, m.start() - 12):m.end()]))
            off = m.group(2)
            offset = None
            if off is not None and re.fullmatch(r"0[xX][0-9a-fA-F]+|\d+", off):
                offset = int(off, 0)
            if offset == 0:
                kind = "fs-write" if write else "fs-read"
            elif offset in (4, 8):
                kind = "fs-stack"
            else:
                kind = "fs-other"
            self.add(base + m.start(), kind, "fs:%s%s" % (off if off is not None else "?", " (write)" if write else ""))
        for m in ASM_ESP.finditer(view):
            self.add(base + m.start(), "esp", "%esp in asm")
        for m in ASM_CALL_INDIRECT.finditer(view):
            tail = re.match(r"\s*[^\s,;\"]*", view[m.end():]).group(0).strip()
            self.add(base + m.start(), "call-indirect", ("%s *%s" % (m.group(0).split()[0], tail)).strip())
        for m in ASM_PROBE.finditer(view):
            self.add(base + m.start(), "stack-probe", "orl $0,(%esp)")
        for m in ASM_IDIV.finditer(view):
            self.add(base + m.start(), "idiv", m.group(0).strip())
        for m in ASM_X87.finditer(view):
            self.add(base + m.start(), "x87", m.group(1))

    def scan_asm_statements(self):
        for m in ASM_KEYWORD.finditer(self.nostr):
            i = m.end()
            q = ASM_QUALIFIERS.match(self.nostr, i)
            qualified = False
            while q:
                qualified = True
                i = q.end()
                q = ASM_QUALIFIERS.match(self.nostr, i)
            while i < len(self.nostr) and self.nostr[i].isspace():
                i += 1
            if i >= len(self.nostr) or self.nostr[i] != "(":
                continue
            end = match_paren(self.nostr, i)
            if end < 0:
                continue
            content = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', self.code[i:end + 1]))
            before = self.nostr[:m.start()].rstrip()[-1:]
            if (not qualified and re.fullmatch(r"\s*[A-Za-z_]\w*\s*", content)
                    and (before == ")" or before == "]" or before.isalnum() or before == "_")):
                self.add(m.start(), "asm-label", content.strip())
                continue
            if m.group(1) == "asm" and not qualified and not content:
                continue
            detail = "extended" if re.search(r'"\s*:', self.code[i:end + 1]) else "basic"
            self.add(m.start(), "inline-asm", detail)
            self.scan_asm_text(self.strs[i:end + 1], i)

    def scan_string_macros(self):
        """#define bodies that consist only of string literals (asm fragments such as ES2_SHIM_FLOOR)."""
        for m in re.finditer(r"^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)(\([^)]*\))?((?:[^\n]*\\\n)*[^\n]*)", self.nostr, re.M):
            body = m.group(3)
            if re.fullmatch(r'[\s\\"]*', body) and '"' in body:
                start = m.start(3)
                self.scan_asm_text(self.strs[start:m.end()], start)

    def scan_naked(self):
        for m in NAKED.finditer(self.nostr):
            head = self.nostr[max(0, m.start() - 200):m.start()]
            head = head[max(head.rfind(c) for c in ";{}") + 1:]
            if "__attribute__" not in head and "__declspec" not in head:
                continue
            # function name and body range
            j = m.end()
            depth = 0
            name = ""
            k = j
            while k < len(self.nostr):
                c = self.nostr[k]
                if c == ";" and depth == 0:
                    break
                if c == "{":
                    close = match_paren(self.nostr, k, "{", "}")
                    if close > 0:
                        self.naked_ranges.append((k, close))
                    break
                k += 1
            fm = re.compile(r"\)\s*\)\s*(?:[\w\s\*]*?)\b([A-Za-z_]\w*)\s*\(").search(self.nostr, j, k if k > j else j + 200)
            if fm:
                name = fm.group(1)
            self.add(m.start(), "naked", name)

    def scan_c_patterns(self):
        code, nostr = self.code, self.nostr
        for m in RAW_CAST.finditer(nostr):
            operand = re.sub(r"\s+", " ", nostr[m.end():unary_end(nostr, m.end())]).strip()
            self.add(m.start(), "guest-host-cast", "%s %s" % (re.sub(r"\s+", " ", m.group(0)), operand[:40]))
        for m in BRIDGE_SEAM.finditer(nostr):
            self.add(m.start(), "bridge-seam", m.group(1))
        for m in MACHINE_CALL.finditer(nostr):
            self.add(m.start(), "machine-call", m.group(1))
        for m in SHIM_FLOOR.finditer(nostr):
            self.add(m.start(), "shim-floor", "ES2_SHIM_FLOOR")
        for m in ST0.finditer(nostr):
            self.add(m.start(), "x87", m.group(1))
        for m in CONTEXT_REDIRECT.finditer(nostr):
            head = nostr[:m.start()]
            statement = head[max(head.rfind(c) for c in ";{}\n") + 1:].lstrip()
            if statement.startswith("*"):
                continue  # a store through the register's value, not the register
            self.add(m.start(), "trampoline", m.group(1) + " written")
        for m in INTRINSIC_FS.finditer(nostr):
            self.add(m.start(), "fs-other", m.group(1))
        for m in INTRINSIC_TEB.finditer(nostr):
            self.add(m.start(), "fs-stack", "NtCurrentTeb")
        for m in INTRINSIC_ESP.finditer(nostr):
            self.add(m.start(), "esp", m.group(1))
        for m in WCHAR.finditer(nostr):
            self.add(m.start(), "wchar_t", m.group(1))
        for m in WIDE_LITERAL.finditer(code):
            if nostr[m.start()] == "L":
                self.add(m.start(), "wchar_t", "L literal")
        for m in LONG_DOUBLE.finditer(nostr):
            self.add(m.start(), "long-double", m.group(0))
        for m in PRAGMA_PACK.finditer(nostr):
            self.add(m.start(), "pragma-pack", "#pragma pack")
        for m in PACKED_ATTR.finditer(nostr):
            self.add(m.start(), "packed-attr", "packed attribute")
        for m in I64_FORMAT.finditer(self.strs):
            self.add(m.start(), "i64-format", m.group(0))
        for m in WIN32_INCLUDE.finditer(nostr.replace('"', ' ')) if False else WIN32_INCLUDE.finditer(code):
            self.add(m.start(), "win32-include", m.group(0))
        for m in WIN32_CALL.finditer(nostr):
            name = m.group(1)
            if name in GUARD_API:
                group = "guard"
            elif name in PLATFORM_API or name.startswith("mci"):
                group = "platform"
            else:
                group = "os"
            if re.search(r"^[ \t]*#[ \t]*define[ \t]+" + re.escape(name) + r"\b", self.line_text(m.start())):
                continue
            self.add(m.start(), "win32-call", name, group=group)
        for m in CRT_IO.finditer(nostr):
            self.add(m.start(), "crt-io", m.group(1))
        self.scan_function_pointer_casts()
        self.scan_bitfields()
        if self.float_int:
            self.scan_float_int()

    def line_text(self, pos):
        n = lineno(self.starts, pos)
        return self.lines[n - 1]

    def scan_function_pointer_casts(self):
        nostr = self.nostr
        candidates = [(m.start(), m.end(), m.group(0)) for m in INLINE_FN_CAST.finditer(nostr)]
        if self.fn_types:
            for m in re.finditer(r"\(\s*(" + "|".join(sorted(map(re.escape, self.fn_types), key=len, reverse=True))
                                 + r")\s*\)", nostr):
                candidates.append((m.start(), m.end(), m.group(1)))
        for start, end, cast in candidates:
            stop = unary_end(nostr, end)
            operand = self.code[end:stop]
            called = bool(re.match(r"\s*\)\s*\(", nostr[stop:stop + 12])) or bool(re.match(r"\s*\(", nostr[stop:stop + 4]))
            if GUEST_OPERAND.search(operand) or (called and re.fullmatch(r"\s*[A-Za-z_]\w*\s*", operand)):
                self.add(start, "guest-fn-cast", "cast to " + re.sub(r"\s+", " ", cast).strip())
        # A guest target macro on a line without a recognised cast (the cast sits elsewhere).
        cast_lines = {h.line for h in self.hits if h.kind == "guest-fn-cast"}
        for m in GUEST_TARGET_MACRO.finditer(nostr):
            if lineno(self.starts, m.start()) in cast_lines or re.match(r"\s*#\s*define", self.line_text(m.start())):
                continue
            self.add(m.start(), "guest-fn-cast", m.group(1))

    def scan_bitfields(self):
        nostr = self.nostr
        stack = []
        bodies = []
        for i, c in enumerate(nostr):
            if c == "{":
                head = nostr[max(0, i - 300):i]
                m = STRUCT_HEAD.search(head)
                stack.append((i, m.group(2) if m else None, bool(m)))
            elif c == "}":
                if stack:
                    start, tag, is_struct = stack.pop()
                    if is_struct:
                        bodies.append((start, i, tag))
        for start, end, tag in bodies:
            interior = list(nostr[start + 1:end])
            depth = 0
            for k, c in enumerate(interior):
                if c == "{":
                    depth += 1
                elif c == "}":
                    depth -= 1
                    interior[k] = " "
                    continue
                if depth > 0 and c != "\n":
                    interior[k] = " "
            interior = "".join(interior)
            offset = 0
            for chunk in interior.split(";"):
                if BITFIELD_CHUNK.search(chunk) and "(" not in chunk and "?" not in chunk and not chunk.lstrip().startswith(("case", "default")):
                    m = re.search(r":\s*(?:0[xX][0-9a-fA-F]+|\d+)", chunk)
                    pos = start + 1 + offset + (m.start() if m else 0)
                    self.add(pos, "bitfield", "%s %s" % ("struct", tag or "(anonymous)"))
                offset += len(chunk) + 1

    def scan_float_int(self):
        nostr = self.nostr
        names = set()
        for m in FLOAT_DECL.finditer(nostr):
            decl = m.group(1)
            names.update(FLOAT_NAMES_IN_DECL.findall(decl))
        for m in re.finditer(r"\b(?:float|double)\b[\s\*]+([A-Za-z_]\w*)", nostr):
            names.add(m.group(1))
        names -= {"const", "static", "unsigned", "signed", "int", "void"}
        for m in INT_CAST.finditer(nostr):
            stop = unary_end(nostr, m.end())
            operand = nostr[m.end():stop]
            if not operand.strip():
                continue
            reason = None
            if re.search(r"\(\s*(?:float|double)\s*\)", operand):
                reason = "float cast"
            elif FLOAT_FUNC.search(operand):
                reason = "libm call"
            elif FLOAT_LITERAL.search(operand):
                reason = "float literal"
            else:
                for ident in IDENT.findall(operand):
                    if ident in names:
                        reason = "float variable " + ident
                        break
            if reason:
                self.add(m.start(), "float-int", "%s %s: %s" % (re.sub(r"\s+", "", m.group(0)), operand.strip()[:40], reason))

    def run(self):
        if self.asm_source:
            self.add(0, "asm-source", "assembly source", line=1)
            self.scan_asm_text(self.strs if False else self.code, 0)
            return self.hits
        self.scan_naked()
        self.scan_asm_statements()
        self.scan_string_macros()
        self.scan_c_patterns()
        return self.hits


def find_root(start):
    here = os.path.abspath(start)
    while True:
        if os.path.exists(os.path.join(here, "MODULE.bazel")):
            return here
        parent = os.path.dirname(here)
        if parent == here:
            return None
        here = parent


def collect_files(paths, include_tests):
    files = []
    for path in paths:
        if os.path.isdir(path):
            for dirpath, dirnames, filenames in os.walk(path):
                dirnames[:] = sorted(d for d in dirnames if include_tests or d not in SKIP_DIRS)
                for name in sorted(filenames):
                    if name.endswith(SOURCE_EXTS):
                        files.append(os.path.join(dirpath, name))
        elif os.path.isfile(path):
            files.append(path)
        else:
            raise SystemExit("seam_scan: no such file or directory: %s" % path)
    seen = set()
    unique = []
    for f in files:
        key = os.path.normcase(os.path.abspath(f))
        if key not in seen:
            seen.add(key)
            unique.append(f)
    return unique


def relative(path, root):
    absolute = os.path.abspath(path)
    try:
        rel = os.path.relpath(absolute, root)
    except ValueError:
        return absolute.replace("\\", "/")
    if rel.startswith(".."):
        return absolute.replace("\\", "/")
    return rel.replace("\\", "/")


def read_text(path):
    with open(path, "rb") as f:
        data = f.read()
    return data.decode("utf-8", errors="replace").replace("\r\n", "\n")


def read_list(path):
    paths = []
    with open(path, encoding="utf-8") as f:
        for raw in f:
            line = raw.split("#", 1)[0].strip()
            if line:
                paths.append(line)
    return paths


def scan(paths, root, allowlist, include_tests=False, float_int_scope=DEFAULT_FLOAT_INT_SCOPE):
    files = collect_files(paths, include_tests)
    texts = []
    fn_types = set()
    for path in files:
        text = read_text(path)
        rel = relative(path, root)
        texts.append((rel, text))
        if not rel.endswith(ASM_EXTS):
            _, nostr, _ = mask(text)
            fn_types.update(FN_TYPEDEF.findall(nostr))
    hits = []
    line_total = 0
    for rel, text in texts:
        line_total += text.count("\n") + (0 if text.endswith("\n") else 1)
        float_int = any(rel.startswith(prefix) for prefix in float_int_scope)
        for hit in FileScan(rel, text, fn_types, float_int).run():
            reason = allowlist.lookup(rel, hit.kind, hit.group)
            d = hit.as_dict()
            d["allowed"] = reason
            d["informational"] = hit.kind in INFORMATIONAL
            hits.append(d)
    hits.sort(key=lambda h: (h["file"], h["line"], h["kind"], h["detail"]))
    return {"files": [rel for rel, _ in texts], "lines": line_total, "hits": hits}


def violations(result):
    return [h for h in result["hits"] if not h["informational"] and not h["allowed"]]


def summary_text(result, only_violations=False):
    hits = violations(result) if only_violations else result["hits"]
    out = []
    counts = {}
    for h in hits:
        row = counts.setdefault(h["kind"], [0, 0])
        row[0] += 1
        if h["allowed"]:
            row[1] += 1
    out.append("%d files, %d lines, %d hits" % (len(result["files"]), result["lines"], len(hits)))
    for kind in KINDS:
        if kind in counts:
            total, allowed = counts[kind]
            out.append("  %-16s %6d  (allowlisted %d)%s" % (kind, total, allowed,
                                                         "  informational" if kind in INFORMATIONAL else ""))
    return "\n".join(out)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("paths", nargs="*", help="source files or directories")
    parser.add_argument("--sources", action="append", default=[], metavar="LIST",
                        help="text file with one source path per line (repeatable)")
    parser.add_argument("--root", help="paths are reported relative to this directory")
    parser.add_argument("--allowlist", help="allowlist file (default: seam_allowlist.txt next to this script)")
    parser.add_argument("--no-allowlist", action="store_true", help="treat every hit as outside the allowlist")
    parser.add_argument("--forbid", action="store_true", help="exit 1 on hits of forbidden kinds outside the allowlist")
    parser.add_argument("--json", metavar="OUT", help="write the machine-readable result ('-' for stdout)")
    parser.add_argument("--summary", action="store_true", help="print only the counts per kind")
    parser.add_argument("--quiet", action="store_true", help="print no hit lines")
    parser.add_argument("--include-tests", action="store_true", help="also scan directories named tests/testdata")
    parser.add_argument("--float-int-scope", action="append", default=None, metavar="PREFIX",
                        help="path prefix whose float->int conversions are listed (default: shell/ launcher/)")
    args = parser.parse_args(argv)

    paths = list(args.paths)
    for listing in args.sources:
        paths.extend(read_list(listing))
    if not paths:
        parser.error("no sources given")
    root = os.path.abspath(args.root) if args.root else (find_root(os.path.dirname(os.path.abspath(__file__))) or os.getcwd())
    allow_path = None if args.no_allowlist else (args.allowlist or os.path.join(os.path.dirname(os.path.abspath(__file__)), "seam_allowlist.txt"))
    if allow_path and not os.path.exists(allow_path):
        if args.allowlist:
            parser.error("no such allowlist: %s" % allow_path)
        allow_path = None
    allowlist = Allowlist(allow_path)
    scope = tuple(args.float_int_scope) if args.float_int_scope is not None else DEFAULT_FLOAT_INT_SCOPE

    result = scan(paths, root, allowlist, args.include_tests, scope)
    result["root"] = root.replace("\\", "/")
    result["allowlist"] = allow_path.replace("\\", "/") if allow_path else None
    result["forbidden_kinds"] = [k for k in KINDS if k not in INFORMATIONAL]
    bad = violations(result)

    if args.json:
        data = json.dumps(result, indent=1, sort_keys=False) + "\n"
        if args.json == "-":
            sys.stdout.write(data)
        else:
            with open(args.json, "wb") as f:
                f.write(data.encode("utf-8"))
    if args.json != "-":
        shown = bad if args.forbid else result["hits"]
        if not args.summary and not args.quiet:
            for h in shown:
                tag = " [allowlisted: %s]" % h["allowed"] if h["allowed"] else ""
                detail = ": " + h["detail"] if h["detail"] else ""
                print("%s:%d: %s%s%s\n    %s\n    -> %s" % (h["file"], h["line"], h["kind"], detail, tag,
                                                             h["text"], h["replacement"]))
        print(summary_text(result, args.forbid))
        if args.forbid:
            if bad:
                print("seam_scan: %d forbidden hit(s) outside the allowlist" % len(bad))
            else:
                print("seam_scan: no forbidden hit outside the allowlist")
    return 1 if (args.forbid and bad) else 0


if __name__ == "__main__":
    sys.exit(main())
