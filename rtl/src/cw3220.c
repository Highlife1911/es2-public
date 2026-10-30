/* The own cw3220.dll (docs/reference-game/rtl_imports.md, section "the own
 * cw3220.dll").
 *
 * A replacement for CW3220.DLL, the Borland C++ runtime DLL of the game,
 * deployed in a clone's game directory (setup/configure-runtime.ps1) in the
 * place of the original, which stays next to it as CW3220-ORIGINAL.DLL.
 *
 * Own mode (DBSIM.EXE and every process that is not ES.EXE or VSHELL.EXE):
 * the 53 names DBSIM imports run own code, built on the core models that
 * were compared with the original in vitro:
 *   - 20 memory, string and formatting functions: core/src/rtl.c;
 *   - calloc, realloc, free: core/src/rtl_heap.c as the allocator over own
 *     VirtualAlloc reservations (CW3220's layout and registers); the DLL's own
 *     allocations (stream buffers, find nodes) come from the same heap, as
 *     in the original;
 *   - 21 file, console and clock functions: core/src/rtl_io.c on the DLL's
 *     own tables (handle table, FILE array, errno, ...), started like
 *     CW3220's init records at DLL attach (es2_io_startup, es2_io_tzset);
 *   - __startup, _atexit, __exit, _abort, __ErrorExit: core/src/rtl_start.c
 *     on the real stack, with the stream clean-up hooks
 *     that exit runs (es2_io_exit_buffers/_files) as own code;
 *   - __ExceptionHandler and @_CatchCleanup$qv (never reached in a normal
 *     run: DBSIM throws nothing) and the data names _errno and __streams
 *     (DBSIM references both only as link artefacts).
 * Calls from DBSIM's main thread run their C part on a private stack: below
 * the caller's ESP only the leaf functions' pushed registers are written
 * (and, for the file functions, the frames of the Win32 calls 32 bytes below
 * it). Calls from another thread run on that thread's stack.
 *
 * Delegate mode (ES.EXE, VSHELL.EXE, which import further names and run
 * their own Borland start-up): DllMain loads the original from
 * CW3220-ORIGINAL.DLL and binds the executable's cw3220 imports to it, so
 * these programs run exactly the original runtime.
 *
 * Not reproduced (rtl_imports.md "the own cw3220.dll"): floating-point
 * formatting (no DBSIM format has one), the frame clean-up of
 * __ExceptionHandler during an unwind (only after a crash), CW3220's
 * iostream and console-buffer start-up and the environment copy (nothing
 * DBSIM imports reads them). */
#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

#include "es2/core/rtl.h"
#include "es2/core/rtl_heap.h"
#include "es2/core/rtl_io.h"
#include "es2/core/rtl_start.h"
#include "es2/rtl/own_runtime.h"
#include "cw3220_exports.h"

/* ------------------------------------------------------------------------
 * The exports (cw3220_exports.h). */
#define OWN_NO_DATA(name, id)
enum {
#define OWN_INDEX(name, id, kind, model) OWN_K_##id,
    OWN_EXPORTS(OWN_INDEX, OWN_NO_DATA)
#undef OWN_INDEX
    OWN_CODE_COUNT
};
enum { OWN_PURE, OWN_HEAP, OWN_IO, OWN_ATEXIT, OWN_EXIT, OWN_ABORT, OWN_ERROREXIT, OWN_STARTUP, OWN_SEH, OWN_CATCH, OWN_EXTRA };
static const struct { const char *name; uint8_t kind, model; } own_exports[OWN_CODE_COUNT] = {
#define OWN_ROW(name, id, kind, model) [OWN_K_##id] = {name, OWN_##kind, (uint8_t)(model)},
    OWN_EXPORTS(OWN_ROW, OWN_NO_DATA)
#undef OWN_ROW
};

/* The registers of a private-stack call (read by the asm below). */
typedef struct own_call_state {
    uint32_t index;                                   /*  0 */
    uint32_t eax, ecx, edx, ebx, esi, edi, ebp, esp;  /*  4: entry registers, ESP -> return address */
    uint32_t out_eax, out_ecx, out_edx;               /* 36 */
} own_call_state;
_Static_assert(offsetof(own_call_state, eax) == 4 && offsetof(own_call_state, esp) == 32 &&
               offsetof(own_call_state, out_eax) == 36 && offsetof(own_call_state, out_edx) == 44, "adapter offsets");

__attribute__((used)) static own_call_state own_call;
__attribute__((used)) static volatile uint8_t own_busy;
__attribute__((used)) static uint32_t own_owner_lo, own_owner_hi;     /* the owner thread's stack */
__attribute__((used)) static uint32_t own_private_lo, own_private_top;
__attribute__((used)) static void *own_route[OWN_CODE_COUNT];
static int own_on_private;                                            /* the C part runs on the private stack */
static HMODULE own_module;
static uint32_t own_mode = ES2_OWN_MODE_OWN;
static es2_own_counters own_counters;

/* Code exports: one jump through own_route each. */
#define OWN_STUB(name, id, kind, model) \
    __attribute__((naked, used)) void own_x_##id(void) { __asm__ volatile("jmp *%0" : : "m"(own_route[OWN_K_##id])); }
OWN_EXPORTS(OWN_STUB, OWN_NO_DATA)
#undef OWN_STUB

/* Private-stack adapters of the pure, heap, file and atexit exports: on the
 * owner thread with the stack free, the entry registers go to own_call and
 * the C part runs on the private stack; otherwise (another thread, or a
 * nested call) on the caller's stack. Only the arithmetic flags change on
 * the way in. */
#define OWN_ADAPTER(entry, k) \
    __attribute__((naked, used)) static void entry(void) { \
        __asm__ volatile("cmpl %[lo], %%esp\n\t" "jb 1f\n\t" \
                         "cmpl %[hi], %%esp\n\t" "jae 1f\n\t" \
                         "cmpb $0, %[busy]\n\t" "jne 1f\n\t" \
                         "movl %%eax, %[eax]\n\t" "movl %[index], %%eax\n\t" "jmp _own_common\n" \
                         "1:\n\t" "pushl %[index]\n\t" "jmp _own_direct\n" \
                         : : [lo] "m"(own_owner_lo), [hi] "m"(own_owner_hi), [busy] "m"(own_busy), \
                             [eax] "m"(own_call.eax), [index] "i"(k)); }
/* Names outside DBSIM's 53: the original's export, loaded on first use. */
#define OWN_LAZY(entry, k) \
    __attribute__((naked, used)) static void entry(void) { \
        __asm__ volatile("pushl %[index]\n\t" "jmp _own_lazy_common\n" : : [index] "i"(k)); }
#define OWN_GEN_PURE(entry, lazy, k) OWN_ADAPTER(entry, k)
#define OWN_GEN_HEAP(entry, lazy, k) OWN_ADAPTER(entry, k)
#define OWN_GEN_IO(entry, lazy, k) OWN_ADAPTER(entry, k)
#define OWN_GEN_ATEXIT(entry, lazy, k) OWN_ADAPTER(entry, k)
#define OWN_GEN_EXIT(entry, lazy, k)
#define OWN_GEN_ABORT(entry, lazy, k)
#define OWN_GEN_ERROREXIT(entry, lazy, k)
#define OWN_GEN_STARTUP(entry, lazy, k)
#define OWN_GEN_SEH(entry, lazy, k)
#define OWN_GEN_CATCH(entry, lazy, k)
#define OWN_GEN_EXTRA(entry, lazy, k) OWN_LAZY(lazy, k)
#define OWN_GEN(name, id, kind, model) OWN_GEN_##kind(own_entry_##id, own_lazy_##id, OWN_K_##id)
OWN_EXPORTS(OWN_GEN, OWN_NO_DATA)
#undef OWN_GEN

__asm__(
    ".text\n.p2align 4\n"
    /* Owner thread: EAX = the index, the entry EAX in own_call.eax. */
    "_own_common:\n"
    "    movl %eax, _own_call\n"
    "    movl %ecx, _own_call+8\n    movl %edx, _own_call+12\n    movl %ebx, _own_call+16\n"
    "    movl %esi, _own_call+20\n    movl %edi, _own_call+24\n    movl %ebp, _own_call+28\n    movl %esp, _own_call+32\n"
    "    movb $1, _own_busy\n"
    "    movl _own_private_top, %esp\n"
    "    cld\n"
    "    call _own_private_c\n"
    "    movl _own_call+32, %esp\n"
    "    movb $0, _own_busy\n"
    "    movl _own_call+36, %eax\n    movl _own_call+40, %ecx\n    movl _own_call+44, %edx\n"
    "    ret\n"
    /* Another thread or a nested call: [esp] = the index, the frame on the
     * caller's stack. */
    ".p2align 4\n"
    "_own_direct:\n"
    "    pushl %edx\n    pushl %ecx\n    pushl %eax\n"
    "    pushl %esp\n"
    "    cld\n"
    "    call _own_direct_c\n"
    "    addl $4, %esp\n"
    "    popl %eax\n    popl %ecx\n    popl %edx\n"
    "    addl $4, %esp\n"
    "    ret\n"
    /* A name outside DBSIM's 53: [esp] = the index. */
    ".p2align 4\n"
    "_own_lazy_common:\n"
    "    pushfl\n    pushal\n"
    "    pushl 36(%esp)\n"
    "    cld\n"
    "    call _own_lazy_c\n"
    "    addl $4, %esp\n"
    "    popal\n    popfl\n"
    "    addl $4, %esp\n"
    "    jmp *_own_lazy_target\n"
    /* uint32_t own_guest_call(target, count, args, stack): a stdcall Win32
     * function with `count` arguments, on `stack` (0: here). EBX keeps the
     * C stack across the call. */
    ".p2align 4\n"
    "_own_guest_call:\n"
    "    pushl %ebp\n    movl %esp, %ebp\n    pushl %ebx\n    pushl %esi\n    pushl %edi\n"
    "    movl 8(%ebp), %eax\n    movl 12(%ebp), %ecx\n    movl 16(%ebp), %esi\n    movl 20(%ebp), %edx\n"
    "    movl %esp, %ebx\n"
    "    testl %edx, %edx\n    jz 1f\n    movl %edx, %esp\n"
    "1:  leal (%esi,%ecx,4), %esi\n"
    "2:  testl %ecx, %ecx\n    jz 3f\n    subl $4, %esi\n    pushl (%esi)\n    decl %ecx\n    jmp 2b\n"
    "3:  call *%eax\n"
    "    movl %ebx, %esp\n"
    "    popl %edi\n    popl %esi\n    popl %ebx\n    popl %ebp\n"
    "    ret\n"
    /* The stream hooks' resting value (CW3220's 4191a0). */
    ".p2align 4\n"
    "_own_dummy_hook:\n"
    "    ret\n");
extern uint32_t __cdecl own_guest_call(uint32_t target, uint32_t count, const uint32_t *args, uint32_t stack) __asm__("_own_guest_call");
extern void own_dummy_hook(void) __asm__("_own_dummy_hook");
/* Read by the asm only (hence not static: no set-but-unused warning). */
__attribute__((used)) uint32_t own_lazy_target;

/* ------------------------------------------------------------------------
 * Memory. */
static uint8_t own_read8(void *c, uint32_t a) { (void)c; return *(const volatile uint8_t *)(uintptr_t)a; }
static void own_write8(void *c, uint32_t a, uint8_t v) { (void)c; *(volatile uint8_t *)(uintptr_t)a = v; }
static uint32_t own_read32(void *c, uint32_t a) { (void)c; return *(const volatile uint32_t *)(uintptr_t)a; }
static void own_write32(void *c, uint32_t a, uint32_t v) { (void)c; *(volatile uint32_t *)(uintptr_t)a = v; }
static const es2_rtl_memory own_memory = {NULL, own_read8, own_write8};
#define OWN_U32(a) (*(volatile uint32_t *)(uintptr_t)(a))

/* ------------------------------------------------------------------------
 * Win32 calls of the models: on the owner thread's private stack they run
 * on the game's stack 32 bytes below the caller's ESP; elsewhere
 * where the caller is. The targets are read from the DLL's import slots at
 * each call (the in-vitro harness points them at an emulated OS). */
static uint32_t own_win32(uint32_t target, uint32_t count, const uint32_t *args)
{
    return own_guest_call(target, count, args, own_on_private ? own_call.esp - 0x20u : 0u);
}
#define OWN_API(f) ((uint32_t)(uintptr_t)(f))

/* ------------------------------------------------------------------------
 * The heap (the only heap of the process's runtime). */
static uint32_t own_head[4];   /* the head block: words -8, -4 (0), links +0, +4 */
static es2_heap own_heap;
static uint32_t own_heap_alloc(void *c, uint32_t address, uint32_t size, uint32_t type, uint32_t protect)
{
    (void)c;
    return (uint32_t)(uintptr_t)VirtualAlloc((LPVOID)(uintptr_t)address, size, type, protect);
}
static uint32_t own_heap_release(void *c, uint32_t address, uint32_t size, uint32_t type)
{
    (void)c;
    return (uint32_t)VirtualFree((LPVOID)(uintptr_t)address, size, type);
}
static void own_heap_fill(void *c, uint32_t address, uint32_t count) { (void)c; memset((void *)(uintptr_t)address, 0, count); }
static void own_heap_move(void *c, uint32_t dest, uint32_t source, uint32_t count)
{
    (void)c;
    memmove((void *)(uintptr_t)dest, (const void *)(uintptr_t)source, count);
}
static const es2_heap_env own_heap_env = {NULL, own_read32, own_write32, own_heap_fill, own_heap_move, own_heap_alloc, own_heap_release};

/* CW3220's platform word (its init record 418fbc): 2 when kernel32 exports
 * "Borland32", else 1 when GetVersion has bit 31 set, else 0. */
static uint32_t own_platform(void)
{
    HMODULE kernel = GetModuleHandleA("kernel32.dll");
    if (kernel && GetProcAddress(kernel, "Borland32")) return 2u;
    return (GetVersion() & 0x80000000u) ? 1u : 0u;
}

/* calloc, realloc, free with CW3220's registers (rtl_imports.md "the heap,
 * replaced"):
 * calloc EAX the block, ECX 0 and EDX count*n after the zero fill (NULL: ECX
 * count*n, EDX unchanged); free per path of 417160; realloc EAX only. */
static void own_heap_call(unsigned which, uint32_t args, es2_rtl_regs *r)
{
    DWORD error = GetLastError();
    if (which == 0) {
        uint32_t count = OWN_U32(args), n = OWN_U32(args + 4u), total = count * n;
        r->eax = es2_heap_calloc(&own_heap, &own_heap_env, count, n);
        if (r->eax) { r->ecx = 0; r->edx = total; }
        else r->ecx = total;
    } else if (which == 1) {
        r->eax = es2_heap_realloc(&own_heap, &own_heap_env, OWN_U32(args), OWN_U32(args + 4u));
    } else {
        uint32_t p = OWN_U32(args);
        r->eax = p;
        if (p) {
            uint32_t header = OWN_U32(p - 4u);
            if (!(header & 1u)) r->edx = header;
            else {
                uint32_t below = OWN_U32(p - 8u);
                int joined = !(below & 1u);
                uint32_t start = joined ? p - below : p;
                es2_heap_free(&own_heap, &own_heap_env, p);
                uint32_t size = OWN_U32(start - 4u);
                r->eax = start;
                if (joined) { r->ecx = start + size; r->edx = size; }
                else { r->ecx = OWN_U32(start + 4u); r->edx = own_heap.sentinel; }
            }
        }
    }
    SetLastError(error);
}

/* ------------------------------------------------------------------------
 * The I/O tables (CW3220's variables, here in the DLL; es2_io_layout). */
static uint32_t own_nfile, own_doserrno, own_sys_nerr, own_fmode_pointer, own_fmode_word, own_umask;
static uint32_t own_openfd[ES2_IO_NFILE], own_handles[ES2_IO_NFILE];
static uint32_t own_exit_hooks[3];             /* exitbuf, exitfopen, exitopen (4222bc..4222c4) */
static uint32_t own_find_list, own_time_zone, own_summer_time;
static uint8_t own_getc_byte;
static uint8_t own_error_map[ES2_IO_ERROR_MAP_SIZE], own_scan_class[ES2_IO_SCAN_CLASSES];
/* The data exports (cw3220.def): _errno, __streams, __ctype. __ctype is
 * CW3220's 257-byte class table (4218b4: EOF, then the bytes 0..255) with
 * the tolower table (4217b4) right below it, as in the original: code that
 * indexes it with a signed char reads tolower bytes for the high characters
 * (es2_io_ctype models that read). */
int32_t own_data_errno;
uint8_t own_data_streams[ES2_IO_NFILE * 0x18u];
__asm__(".data\n.p2align 4\n_own_tolower_table:\n.space 256\n.globl _own_data_ctype\n_own_data_ctype:\n.space 257\n.text\n");
extern uint8_t own_tolower_table[256] __asm__("_own_tolower_table");
extern uint8_t own_data_ctype[257] __asm__("_own_data_ctype");
static es2_io_layout own_io;

static uint32_t own_io_malloc(void *c, uint32_t n, uint32_t hint)
{
    (void)c; (void)hint;
    return es2_heap_malloc(&own_heap, &own_heap_env, n);
}
static void own_io_free(void *c, uint32_t p) { (void)c; es2_heap_free(&own_heap, &own_heap_env, p); }

/* One Win32 call of the I/O model, with the original's arguments (the
 * pointers of SECURITY_ATTRIBUTES, byte counts and output structures are
 * the DLL's statics: the game's stack is never a buffer here). */
static void own_io_os(void *context, es2_io_os *call)
{
    (void)context;
    static uint32_t sa[3], number, filetime[2], local[2];
    static WIN32_FIND_DATAA found;
    static SYSTEMTIME now;
    static STARTUPINFOA startup;
    static TIME_ZONE_INFORMATION zone;
    uint32_t args[7] = {0}, *a = call->arg, target = 0, n = 1;
    switch (call->function) {
    case ES2_IO_OS_CREATE_FILE:
        sa[0] = 12; sa[1] = 0; sa[2] = a[3];
        args[0] = a[0]; args[1] = a[1]; args[2] = a[2]; args[3] = (uint32_t)(uintptr_t)sa; args[4] = a[4]; args[5] = a[5]; args[6] = 0;
        target = OWN_API(CreateFileA); n = 7;
        break;
    case ES2_IO_OS_CLOSE_HANDLE: args[0] = a[0]; target = OWN_API(CloseHandle); break;
    case ES2_IO_OS_READ_FILE:
        args[0] = a[0]; args[1] = a[1]; args[2] = a[2]; args[3] = (uint32_t)(uintptr_t)&number; args[4] = 0;
        target = OWN_API(ReadFile); n = 5;
        break;
    case ES2_IO_OS_WRITE_FILE:
        args[0] = a[0]; args[1] = a[1] ? a[1] : (uint32_t)(uintptr_t)call->data; args[2] = a[2];
        args[3] = (uint32_t)(uintptr_t)&number; args[4] = 0;
        target = OWN_API(WriteFile); n = 5;
        break;
    case ES2_IO_OS_SET_FILE_POINTER: args[0] = a[0]; args[1] = a[1]; args[2] = 0; args[3] = a[2]; target = OWN_API(SetFilePointer); n = 4; break;
    case ES2_IO_OS_GET_FILE_SIZE: args[0] = a[0]; args[1] = 0; target = OWN_API(GetFileSize); n = 2; break;
    case ES2_IO_OS_GET_FILE_TYPE: args[0] = a[0]; target = OWN_API(GetFileType); break;
    case ES2_IO_OS_GET_FILE_ATTRIBUTES: args[0] = a[0]; target = OWN_API(GetFileAttributesA); break;
    case ES2_IO_OS_GET_LAST_ERROR: target = OWN_API(GetLastError); n = 0; break;
    case ES2_IO_OS_FIND_FIRST: args[0] = a[0]; args[1] = (uint32_t)(uintptr_t)&found; target = OWN_API(FindFirstFileA); n = 2; break;
    case ES2_IO_OS_FIND_NEXT: args[0] = a[0]; args[1] = (uint32_t)(uintptr_t)&found; target = OWN_API(FindNextFileA); n = 2; break;
    case ES2_IO_OS_FIND_CLOSE: args[0] = a[0]; target = OWN_API(FindClose); break;
    case ES2_IO_OS_FILE_TIME_TO_LOCAL:
        filetime[0] = a[0]; filetime[1] = a[1];
        args[0] = (uint32_t)(uintptr_t)filetime; args[1] = (uint32_t)(uintptr_t)local;
        target = OWN_API(FileTimeToLocalFileTime); n = 2;
        break;
    case ES2_IO_OS_FILE_TIME_TO_DOS:
        filetime[0] = a[0]; filetime[1] = a[1];
        args[0] = (uint32_t)(uintptr_t)filetime; args[1] = a[2]; args[2] = a[3];
        target = OWN_API(FileTimeToDosDateTime); n = 3;
        break;
    case ES2_IO_OS_GET_LOCAL_TIME: args[0] = (uint32_t)(uintptr_t)&now; target = OWN_API(GetLocalTime); break;
    case ES2_IO_OS_SET_HANDLE_COUNT: args[0] = a[0]; target = OWN_API(SetHandleCount); break;
    case ES2_IO_OS_GET_STARTUP_INFO: args[0] = (uint32_t)(uintptr_t)&startup; target = OWN_API(GetStartupInfoA); break;
    case ES2_IO_OS_GET_STD_HANDLE: args[0] = a[0]; target = OWN_API(GetStdHandle); break;
    case ES2_IO_OS_GET_TIME_ZONE: args[0] = (uint32_t)(uintptr_t)&zone; target = OWN_API(GetTimeZoneInformation); break;
    default: call->result = 0; return;
    }
    number = 0;
    call->result = own_win32(target, n, args);
    switch (call->function) {
    case ES2_IO_OS_READ_FILE: case ES2_IO_OS_WRITE_FILE: call->count = number; break;
    case ES2_IO_OS_FIND_FIRST: case ES2_IO_OS_FIND_NEXT: memcpy(call->out, &found, ES2_IO_FIND_DATA); break;
    case ES2_IO_OS_FILE_TIME_TO_LOCAL: memcpy(call->out, local, 8); break;
    case ES2_IO_OS_GET_LOCAL_TIME: memcpy(call->out, &now, 16); break;
    case ES2_IO_OS_GET_STARTUP_INFO: memcpy(call->out, &startup, sizeof(startup)); break;
    case ES2_IO_OS_GET_TIME_ZONE: memcpy(call->out, &zone, sizeof(zone)); break;
    default: break;
    }
}
static const es2_io_env own_io_env = {NULL, own_read8, own_write8, own_io_os, own_io_malloc, own_io_free};

/* The stream hooks that exit runs once installed (CW3220's 407ca4 and
 * 407c7c), called by the lifecycle on the game's stack. */
static void __cdecl own_exitbuf(void) { es2_io_exit_buffers(&own_io_env, &own_io); }
static void __cdecl own_exitfopen(void) { es2_io_exit_files(&own_io_env, &own_io); }

static void own_io_layout_init(void)
{
    own_io.nfile = (uint32_t)(uintptr_t)&own_nfile;
    own_io.openfd = (uint32_t)(uintptr_t)own_openfd;
    own_io.handles = (uint32_t)(uintptr_t)own_handles;
    own_io.streams = (uint32_t)(uintptr_t)own_data_streams;
    own_io.errno_word = (uint32_t)(uintptr_t)&own_data_errno;
    own_io.doserrno = (uint32_t)(uintptr_t)&own_doserrno;
    own_io.error_count = (uint32_t)(uintptr_t)&own_sys_nerr;
    own_io.error_map = (uint32_t)(uintptr_t)own_error_map;
    own_io.fmode_pointer = (uint32_t)(uintptr_t)&own_fmode_pointer;
    own_io.umask_word = (uint32_t)(uintptr_t)&own_umask;
    own_io.exit_buffers = (uint32_t)(uintptr_t)&own_exit_hooks[0];
    own_io.exit_buffers_value = (uint32_t)(uintptr_t)own_exitbuf;
    own_io.exit_files = (uint32_t)(uintptr_t)&own_exit_hooks[1];
    own_io.exit_files_value = (uint32_t)(uintptr_t)own_exitfopen;
    own_io.getc_byte = (uint32_t)(uintptr_t)&own_getc_byte;
    own_io.find_list = (uint32_t)(uintptr_t)&own_find_list;
    own_io.scan_class = (uint32_t)(uintptr_t)own_scan_class;
    own_io.time_zone = (uint32_t)(uintptr_t)&own_time_zone;
    own_io.summer_time = (uint32_t)(uintptr_t)&own_summer_time;
    own_io.stdout_file = (uint32_t)(uintptr_t)(own_data_streams + 0x18u);
}

/* A call the I/O model leaves to the original (cases DBSIM never makes):
 * the result an own runtime gives instead. */
static uint32_t own_io_declined(unsigned f, uint32_t args)
{
    ++own_counters.declined;
    switch (f) {
    case ES2_IO_FSCANF: case ES2_IO_FREAD: case ES2_IO_PRINTF: return 0;   /* nothing converted, read or written */
    case ES2_IO_TIME:
        if (OWN_U32(args)) OWN_U32(OWN_U32(args)) = 0xffffffffu;
        return 0xffffffffu;
    default: return 0xffffffffu;                                          /* fclose of a tmpfile() stream */
    }
}

/* ------------------------------------------------------------------------
 * The lifecycle (its env on the real stack, rtl_imports.md "the lifecycle
 * group as own code"). */
#define OWN_LIFE_STACK 0x10000u
#define OWN_PRIVATE_STACK 0x40000u
static const char own_abort_text[] = "Abnormal program termination";

typedef struct own_life_frame {
    uint32_t target;        /*  0 */
    uint32_t esp;           /*  4: ESP at the target's entry (its return-address slot) */
    es2_rtl_cpu in;         /*  8 */
    es2_rtl_cpu out;        /* 36 */
    uint32_t out_esp;       /* 64 */
    uint32_t c_esp;         /* 68 */
    uint32_t outer;         /* 72 */
    uint32_t jump;          /* 76: enter by jmp ([esp] holds a return address already) */
} own_life_frame;
_Static_assert(offsetof(own_life_frame, in) == 8 && offsetof(own_life_frame, out) == 36 && offsetof(own_life_frame, out_esp) == 64 &&
               offsetof(own_life_frame, c_esp) == 68 && offsetof(own_life_frame, outer) == 72 && offsetof(own_life_frame, jump) == 76 &&
               offsetof(es2_rtl_cpu, ebp) == 24, "lifecycle frame offsets");

__attribute__((used)) static uint32_t own_life_target, own_life_scratch, own_life_caller_esp;
__attribute__((used)) uint32_t own_life_top, own_life_current;   /* read by the asm only */
__attribute__((used)) static es2_rtl_cpu own_life_caller;
static uint32_t own_life_lo;
static es2_rtl_runtime own_life;
static es2_rtl_site *own_life_site;
/* The original's return address of the guest or Win32 call in progress, as
 * a CW3220 RVA (the descriptor shows it to the in-vitro comparison). */
static volatile uint32_t own_life_return_rva;

__asm__(
    ".text\n.p2align 4\n"
    "_own_life_call:\n"
    "    pushl %ebp\n    pushl %ebx\n    pushl %esi\n    pushl %edi\n"
    "    movl 20(%esp), %eax\n"
    "    movl %esp, 68(%eax)\n"
    "    movl _own_life_current, %ecx\n    movl %ecx, 72(%eax)\n    movl %eax, _own_life_current\n"
    "    movl 0(%eax), %ecx\n    movl %ecx, _own_life_target\n"
    "    movl 4(%eax), %esp\n"
    "    cmpl $0, 76(%eax)\n    jne 1f\n"
    "    addl $4, %esp\n"
    "    movl 32(%eax), %ebp\n    movl 28(%eax), %edi\n    movl 24(%eax), %esi\n    movl 20(%eax), %ebx\n"
    "    movl 16(%eax), %edx\n    movl 12(%eax), %ecx\n    movl 8(%eax), %eax\n"
    "    cld\n"
    "    call *_own_life_target\n"
    "_own_life_return:\n"
    "    movl %eax, _own_life_scratch\n"
    "    movl _own_life_current, %eax\n"
    "    movl %ecx, 40(%eax)\n    movl %edx, 44(%eax)\n    movl %ebx, 48(%eax)\n    movl %esi, 52(%eax)\n"
    "    movl %edi, 56(%eax)\n    movl %ebp, 60(%eax)\n    movl %esp, 64(%eax)\n"
    "    movl _own_life_scratch, %ecx\n    movl %ecx, 36(%eax)\n"
    "    movl 72(%eax), %ecx\n    movl %ecx, _own_life_current\n"
    "    movl 68(%eax), %esp\n"
    "    popl %edi\n    popl %esi\n    popl %ebx\n    popl %ebp\n"
    "    cld\n    ret\n"
    "1:  movl 32(%eax), %ebp\n    movl 28(%eax), %edi\n    movl 24(%eax), %esi\n    movl 20(%eax), %ebx\n"
    "    movl 16(%eax), %edx\n    movl 12(%eax), %ecx\n    movl 8(%eax), %eax\n"
    "    cld\n"
    "    jmp *_own_life_target\n"
    /* __startup: ESP = E, DBSIM's entry registers. */
    ".p2align 4\n"
    "_own_life_startup_entry:\n"
    "    movl %eax, _own_life_caller\n    movl %ecx, _own_life_caller+4\n    movl %edx, _own_life_caller+8\n"
    "    movl %ebx, _own_life_caller+12\n    movl %esi, _own_life_caller+16\n    movl %edi, _own_life_caller+20\n"
    "    movl %ebp, _own_life_caller+24\n    movl %esp, _own_life_caller_esp\n"
    "    movl _own_life_top, %esp\n    cld\n"
    "    call _own_life_startup_c\n"
    "    ud2\n"
    /* __exit, _abort, __ErrorExit: [esp] = the caller's return address. */
    ".p2align 4\n"
    "_own_life_exit_entry:\n    movl %eax, _own_life_caller\n    movl $1, %eax\n    jmp _own_life_adapter_common\n"
    ".p2align 4\n"
    "_own_life_abort_entry:\n    movl %eax, _own_life_caller\n    movl $2, %eax\n    jmp _own_life_adapter_common\n"
    ".p2align 4\n"
    "_own_life_error_entry:\n    movl %eax, _own_life_caller\n    movl $3, %eax\n    jmp _own_life_adapter_common\n"
    ".p2align 4\n"
    "_own_life_adapter_common:\n"
    "    movl %ecx, _own_life_caller+4\n    movl %edx, _own_life_caller+8\n    movl %ebx, _own_life_caller+12\n"
    "    movl %esi, _own_life_caller+16\n    movl %edi, _own_life_caller+20\n    movl %ebp, _own_life_caller+24\n"
    "    movl %esp, _own_life_caller_esp\n"
    "    movl _own_life_current, %ecx\n    testl %ecx, %ecx\n    jz 1f\n"
    "    movl 68(%ecx), %esp\n    subl $64, %esp\n    andl $-16, %esp\n    jmp 2f\n"
    "1:  movl _own_life_top, %esp\n"
    "2:  cld\n    subl $12, %esp\n    pushl %eax\n"
    "    call _own_life_adapter_c\n"
    "    ud2\n");
extern void __cdecl own_life_call(own_life_frame *frame) __asm__("_own_life_call");
extern void own_life_return(void) __asm__("_own_life_return");
extern void own_life_startup_entry(void) __asm__("_own_life_startup_entry");
extern void own_life_exit_entry(void) __asm__("_own_life_exit_entry");
extern void own_life_abort_entry(void) __asm__("_own_life_abort_entry");
extern void own_life_error_entry(void) __asm__("_own_life_error_entry");

static void own_life_env_call(void *c, es2_rtl_guest_call *k)
{
    (void)c;
    switch (k->kind) {
    case ES2_RTL_CALL_INIT_RECORD: ++own_counters.init_records; break;
    case ES2_RTL_CALL_WINMAIN: case ES2_RTL_CALL_MAIN: ++own_counters.winmain; break;
    case ES2_RTL_CALL_ATEXIT: ++own_counters.atexit_run; break;
    case ES2_RTL_CALL_EXIT_RECORD: ++own_counters.exit_records; break;
    case ES2_RTL_CALL_SIGNAL: ++own_counters.signals; break;
    default: ++own_counters.stream_hooks; break;
    }
    own_life_frame f;
    memset(&f, 0, sizeof(f));
    f.target = k->target;
    f.esp = k->esp;
    f.in = k->regs;
    own_life_return_rva = k->return_rva;
    own_life_call(&f);
    k->out = f.out;
    k->result = f.out.eax;
    k->popped = f.out_esp - (k->esp + 4u);
    if (k->kind == ES2_RTL_CALL_WINMAIN || k->kind == ES2_RTL_CALL_MAIN) own_counters.exit_status = (int64_t)k->result;
}
static void own_life_env_fpu(void *c, uint16_t w)
{
    (void)c;
    __asm__ volatile("finit\n\tfldcw %0" ::"m"(w));   /* __fpreset: wait; fninit; then _control87 */
}
/* The handler of __startup's frame record (CW3220's 405160): for every code
 * but the C++ exception 0EEFFACEh, which nothing raises here, the stack
 * overflow message is switched off by a constant and both user handlers are
 * CW3220's default (return 1); its answer is UnhandledExceptionFilter's. */
static EXCEPTION_DISPOSITION __cdecl own_startup_handler(EXCEPTION_RECORD *record, void *frame, CONTEXT *context, void *dispatcher)
{
    (void)frame; (void)dispatcher;
    EXCEPTION_POINTERS pointers = {record, context};
    return (EXCEPTION_DISPOSITION)UnhandledExceptionFilter(&pointers);
}
static void own_life_env_frame(void *c, uint32_t record, uint32_t handler_rva)
{
    (void)c; (void)handler_rva;
    uint32_t head;
    __asm__ volatile("movl %%fs:0, %0" : "=r"(head));
    OWN_U32(record) = head;
    OWN_U32(record + 4u) = (uint32_t)(uintptr_t)own_startup_handler;
    __asm__ volatile("movl %0, %%fs:0" ::"r"(record) : "memory");
    /* What __startup has stored by now and the file functions read: the
     * module's _fmode word (open and fopen take their default mode from it). */
    own_fmode_pointer = own_life.fmode;
}
static void own_life_env_site(void *c, es2_rtl_site *site) { (void)c; own_life_site = site; }
/* The announced Win32 call (its arguments on the stack already), at the
 * original's ESP. EnumThreadWindows gets the DLL's callback, not CW3220's. */
static BOOL CALLBACK own_window_found(HWND window, LPARAM flag)
{
    (void)window;
    *(volatile uint32_t *)(uintptr_t)flag = 1;
    return FALSE;
}
static uint32_t own_life_api_call(void)
{
    es2_rtl_site *s = own_life_site;
    own_life_frame f;
    memset(&f, 0, sizeof(f));
    switch (s->api) {
    case ES2_RTL_API_ENVIRONMENT_STRINGS: f.target = OWN_API(GetEnvironmentStrings); break;
    case ES2_RTL_API_COMMAND_LINE: f.target = OWN_API(GetCommandLineA); break;
    case ES2_RTL_API_MODULE_HANDLE: f.target = OWN_API(GetModuleHandleA); break;
    case ES2_RTL_API_MODULE_FILE_NAME: f.target = OWN_API(GetModuleFileNameA); break;
    case ES2_RTL_API_VERSION: f.target = OWN_API(GetVersion); break;
    case ES2_RTL_API_THREAD_ID: f.target = OWN_API(GetCurrentThreadId); break;
    case ES2_RTL_API_ENUM_THREAD_WINDOWS:
        f.target = OWN_API(EnumThreadWindows);
        OWN_U32(s->esp + 8u) = (uint32_t)(uintptr_t)own_window_found;
        break;
    case ES2_RTL_API_MESSAGE_BOX: f.target = OWN_API(MessageBoxA); break;
    default: f.target = OWN_API(ExitProcess); break;
    }
    f.esp = s->esp;
    f.in = s->cpu;
    own_life_return_rva = s->return_rva;
    if (s->api == ES2_RTL_API_EXIT_PROCESS) {
        OWN_U32(s->esp) = own_life.code_base + s->return_rva;
        f.jump = 1;
    }
    own_life_call(&f);
    s->cpu = f.out;
    return f.out.eax;
}
static uint32_t own_life_env_environment(void *c) { (void)c; return own_life_api_call(); }
static uint32_t own_life_env_command(void *c) { (void)c; return own_life_api_call(); }
static uint32_t own_life_env_handle(void *c, uint32_t name) { (void)c; (void)name; return own_life_api_call(); }
static uint32_t own_life_env_file(void *c, char *buffer, uint32_t size)
{
    (void)c;
    uint32_t guest = own_life_site->guest, r = own_life_api_call();
    memcpy(buffer, (const void *)(uintptr_t)guest, size);   /* the model's buffer holds size + 1 zeroed bytes */
    return r;
}
static uint32_t own_life_env_version(void *c) { (void)c; return own_life_api_call(); }
static uint32_t own_life_env_thread(void *c) { (void)c; return own_life_api_call(); }
static int own_life_env_window(void *c, uint32_t thread)
{
    (void)c; (void)thread;
    uint32_t flag = own_life_site->guest;
    own_life_api_call();
    return OWN_U32(flag) != 0;
}
static void own_life_env_box(void *c, uint32_t text, const char *title, uint32_t style, uint32_t esp)
{
    (void)c; (void)text; (void)title; (void)style; (void)esp;
    own_life_api_call();
}
static void own_life_env_exit(void *c, uint32_t status, uint32_t esp)
{
    (void)c; (void)esp;
    own_counters.exit_status = (int64_t)status;
    own_life_api_call();
}
static const es2_rtl_start_env own_life_env = {NULL, own_read8, own_write8, own_read32, own_life_env_call, own_life_env_fpu,
                                               own_life_env_frame, own_life_env_environment, own_life_env_command, own_life_env_handle,
                                               own_life_env_file, own_life_env_version, own_life_env_thread, own_life_env_window,
                                               own_life_env_box, own_life_env_exit, own_write32, own_life_env_site};

void __cdecl own_life_startup_c(void) __asm__("_own_life_startup_c") __attribute__((used));
void __cdecl own_life_startup_c(void)
{
    uint32_t esp = own_life_caller_esp;
    own_life.entry = own_life_caller;
    es2_rtl_startup(&own_life, &own_life_env, esp, OWN_U32(esp + 4u));
    __builtin_trap();   /* only if ExitProcess returned */
}
void __cdecl own_life_adapter_c(uint32_t which) __asm__("_own_life_adapter_c") __attribute__((used));
void __cdecl own_life_adapter_c(uint32_t which)
{
    uint32_t esp = own_life_caller_esp, argument = OWN_U32(esp + 4u);
    own_life.entry = own_life_caller;
    if (which == 1u) es2_rtl_quick_exit(&own_life, &own_life_env, esp, argument);
    else if (which == 2u) es2_rtl_abort(&own_life, &own_life_env, esp);
    else es2_rtl_error_exit(&own_life, &own_life_env, esp, argument);
    __builtin_trap();
}

/* _atexit (CW3220's 419070): EAX 0, ECX the function, EDX the old count,
 * or for a full table EAX 1 and ECX/EDX unchanged. */
static void own_atexit_call(uint32_t args, es2_rtl_regs *r)
{
    uint32_t function = OWN_U32(args), count = own_life.atexit_count;
    if (es2_rtl_atexit(&own_life, function)) {
        ++own_counters.atexit_refused;
        r->eax = 1u;
    } else {
        ++own_counters.atexit_registered;
        r->eax = 0; r->ecx = function; r->edx = count;
    }
}

/* ------------------------------------------------------------------------
 * The exception exports. */

/* __ExceptionHandler (CW3220's 4042f0), the handler of every frame record
 * DBSIM's 49630c sets up. DBSIM's frame tables hold destructor entries
 * (kind 5) and one catch (kind 3), no __except entries (kinds 1 and 2), so
 * outside an unwind the original acts only for the C++ exception 0EEFFACEh,
 * which nothing in DBSIM's normal run raises; for any other exception the
 * search goes on (rtl_imports.md "the own cw3220.dll"). The original's catch
 * handling and its destructor calls during an unwind are not reproduced. */
static EXCEPTION_DISPOSITION __cdecl own_exception_handler(EXCEPTION_RECORD *record, void *frame, CONTEXT *context, void *dispatcher)
{
    (void)record; (void)frame; (void)context; (void)dispatcher;
    return ExceptionContinueSearch;
}
/* @_CatchCleanup$qv (CW3220's 403e91), called only by DBSIM's terminate()
 * after its catch-all: it takes the current exception off the context block
 * at StackBase-8. Without a thrown exception the list is empty and nothing is
 * done (the original would read through the empty list). */
static void __cdecl own_catch_cleanup(void)
{
    uint32_t base;
    __asm__ volatile("movl %%fs:4, %0" : "=r"(base));
    uint32_t context = OWN_U32(base - 8u);
    if (!context || !OWN_U32(context)) return;
    ++own_counters.declined;
    ((void (*)(void))own_route[OWN_K_abort])();
}

/* ------------------------------------------------------------------------
 * The C parts of the adapters. */
static void own_run(unsigned k, uint32_t esp, es2_rtl_regs *r, int private_stack)
{
    uint32_t args = esp + 4u;
    unsigned model = own_exports[k].model;
    switch (own_exports[k].kind) {
    case OWN_PURE:
        if (es2_rtl_call((es2_rtl_function)model, &own_memory, args, r) != ES2_RTL_DONE) {
            /* sprintf/vsprintf outside the formatter (a floating-point,
             * %p or %n conversion, or an empty result): dest[0] = 0, which
             * the original writes first; the count 0, ECX the formatter's
             * buffer record, EDX 0 (the original leaves formatter residue). */
            ++own_counters.declined;
            *(volatile uint8_t *)(uintptr_t)OWN_U32(args) = 0;
            r->eax = 0; r->ecx = args - 220u; r->edx = 0;
        }
        if (private_stack) {
            const char *residue = es2_rtl_leaf_residue((es2_rtl_function)model);
            for (unsigned i = 0; residue && residue[i]; ++i) {
                char reg = residue[i];
                OWN_U32(esp - 4u * (i + 1u)) = reg == 'p' ? own_call.ebp : reg == 'b' ? own_call.ebx : reg == 's' ? own_call.esi : own_call.edi;
            }
        }
        break;
    case OWN_HEAP: own_heap_call(model, args, r); break;
    case OWN_IO: {
        uint32_t eax = 0;
        if (model == ES2_IO_FPUTS && !private_stack && OWN_U32(args) && *(volatile const uint8_t *)(uintptr_t)OWN_U32(args)) {
            /* CW3220's fputs (405b7c, via 405aa8 and _fputc 40622c)
             * writes its FILE for every non-empty string. DBSIM's assert
             * notifier (479d18) passes __streams' thunk+0x30, inside its
             * read-only CODE: the write at 406287 (or word [fp+0x12], 0x10)
             * is an access violation that goes to __startup's frame record.
             * The stub reads and writes back that word unchanged: the same
             * fault there, no change to a writable FILE. On the private
             * stack's path own_entry_fputs_probe has made that write. */
            volatile uint16_t *flags = (volatile uint16_t *)(uintptr_t)(OWN_U32(args + 4u) + 0x12u);
            *flags = *flags;
        }
        if (es2_io_call((es2_io_function)model, &own_io_env, &own_io, args, &eax) != ES2_IO_DONE) eax = own_io_declined(model, args);
        r->eax = eax; r->ecx = 0; r->edx = 0;
        break;
    }
    case OWN_ATEXIT:
        own_atexit_call(args, r);
        if (private_stack) OWN_U32(esp - 4u) = own_call.ebp;   /* its pushed EBP */
        break;
    default: break;
    }
}
int __cdecl own_private_c(void) __asm__("_own_private_c") __attribute__((used));
int __cdecl own_private_c(void)
{
    es2_rtl_regs r = {own_call.eax, own_call.ecx, own_call.edx};
    ++own_counters.calls;
    own_on_private = 1;
    own_run(own_call.index, own_call.esp, &r, 1);
    own_on_private = 0;
    own_call.out_eax = r.eax; own_call.out_ecx = r.ecx; own_call.out_edx = r.edx;
    return 0;
}
typedef struct own_frame { uint32_t eax, ecx, edx, index, ret; } own_frame;
void __cdecl own_direct_c(own_frame *f) __asm__("_own_direct_c") __attribute__((used));
void __cdecl own_direct_c(own_frame *f)
{
    uint32_t esp = (uint32_t)(uintptr_t)&f->ret;
    if (esp - own_owner_lo < own_owner_hi - own_owner_lo) ++own_counters.nested_calls;
    else ++own_counters.foreign_calls;
    es2_rtl_regs r = {f->eax, f->ecx, f->edx};
    int saved = own_on_private;
    own_on_private = 0;
    own_run(f->index, esp, &r, 0);
    own_on_private = saved;
    f->eax = r.eax; f->ecx = r.ecx; f->edx = r.edx;
}

/* ------------------------------------------------------------------------
 * Delegation to the original (ES.EXE, VSHELL.EXE; in own mode the names
 * outside DBSIM's 53). */
static HMODULE own_original;
static INIT_ONCE own_original_once = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK own_load_original(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    (void)once; (void)parameter; (void)context;
    WCHAR path[MAX_PATH];
    DWORD n = GetModuleFileNameW(own_module, path, MAX_PATH);
    WCHAR *slash = n && n < MAX_PATH ? wcsrchr(path, L'\\') : NULL;
    const WCHAR name[] = L"" ES2_OWN_ORIGINAL_FILE;
    if (!slash || (size_t)(slash + 1 - path) + sizeof(name) / sizeof(name[0]) > MAX_PATH) return FALSE;
    memcpy(slash + 1, name, sizeof(name));
    own_original = LoadLibraryW(path);
    return own_original != NULL;
}
static void own_fatal(const char *text)
{
    MessageBoxA(NULL, text, "cw3220.dll", MB_OK | MB_ICONERROR | MB_TASKMODAL);
    ExitProcess(255);
}
void __cdecl own_lazy_c(uint32_t k) __asm__("_own_lazy_c") __attribute__((used));
void __cdecl own_lazy_c(uint32_t k)
{
    DWORD error = GetLastError();
    ++own_counters.delegated;
    FARPROC f = NULL;
    if (k < OWN_CODE_COUNT && InitOnceExecuteOnce(&own_original_once, own_load_original, NULL, NULL))
        f = GetProcAddress(own_original, own_exports[k].name);
    if (!f) own_fatal("The original runtime CW3220-ORIGINAL.DLL next to the own cw3220.dll is missing or incomplete.");
    own_route[k] = (void *)(uintptr_t)f;
    own_lazy_target = (uint32_t)(uintptr_t)f;
    SetLastError(error);
}

/* DllMain in ES.EXE or VSHELL.EXE: load the original and bind the
 * executable's cw3220 imports to it, so that its own Borland start-up,
 * heap and streams are the original's (data names included). */
static BOOL own_bind_executable(void)
{
    BYTE *image = (BYTE *)GetModuleHandleW(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)image;
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(image + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir->VirtualAddress) return TRUE;
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(image + dir->VirtualAddress); d->Name; ++d) {
        if (_stricmp((const char *)image + d->Name, "cw3220.dll")) continue;
        uint32_t *names = (uint32_t *)(image + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        uint32_t *slots = (uint32_t *)(image + d->FirstThunk);
        for (unsigned i = 0; names[i]; ++i) {
            FARPROC f = (names[i] & 0x80000000u) ? GetProcAddress(own_original, (LPCSTR)(uintptr_t)(names[i] & 0xffffu))
                                                 : GetProcAddress(own_original, (const char *)image + names[i] + 2);
            DWORD old, ignored;
            if (!f || !VirtualProtect(&slots[i], 4, PAGE_READWRITE, &old)) return FALSE;
            slots[i] = (uint32_t)(uintptr_t)f;
            VirtualProtect(&slots[i], 4, old, &ignored);
        }
    }
    return TRUE;
}
static BOOL own_delegate_attach(void)
{
    own_mode = ES2_OWN_MODE_DELEGATE;
    if (!InitOnceExecuteOnce(&own_original_once, own_load_original, NULL, NULL)) return FALSE;
    for (unsigned k = 0; k < OWN_CODE_COUNT; ++k) {
        FARPROC f = GetProcAddress(own_original, own_exports[k].name);
        if (!f) return FALSE;
        own_route[k] = (void *)(uintptr_t)f;
    }
    return own_bind_executable();
}

/* ------------------------------------------------------------------------
 * The routes of own mode. */
#define OWN_TARGET_PURE(entry, lazy) (void *)entry
#define OWN_TARGET_HEAP(entry, lazy) (void *)entry
#define OWN_TARGET_IO(entry, lazy) (void *)entry
#define OWN_TARGET_ATEXIT(entry, lazy) (void *)entry
#define OWN_TARGET_EXIT(entry, lazy) (void *)own_life_exit_entry
#define OWN_TARGET_ABORT(entry, lazy) (void *)own_life_abort_entry
#define OWN_TARGET_ERROREXIT(entry, lazy) (void *)own_life_error_entry
#define OWN_TARGET_STARTUP(entry, lazy) (void *)own_life_startup_entry
#define OWN_TARGET_SEH(entry, lazy) (void *)own_exception_handler
#define OWN_TARGET_CATCH(entry, lazy) (void *)own_catch_cleanup
#define OWN_TARGET_EXTRA(entry, lazy) (void *)lazy

/* _fputs from the owner thread: CW3220's fputs makes its FILE write
 * (406287: or word [fp+0x12], 0x10) on the caller's stack. The same
 * read-modify-write, value unchanged, runs here before the switch to the
 * private stack, so DBSIM's read-only FILE faults with ESP on the thread's
 * stack as in the original. From the private stack the dispatch reaches the
 * same records, but the unwind after an EXCEPTION_EXECUTE_HANDLER answer
 * fails the OS's stack check (STATUS_BAD_STACK or
 * STATUS_INVALID_UNWIND_TARGET instead of the fault code as exit code).
 * NULL and "" touch no FILE (405b84, 405b93). */
__attribute__((naked, used)) static void own_entry_fputs_probe(void)
{
    __asm__ volatile("cmpl %[lo], %%esp\n\t" "jb 1f\n\t"
                     "cmpl %[hi], %%esp\n\t" "jae 1f\n\t"
                     "cmpb $0, %[busy]\n\t" "jne 1f\n\t"
                     "movl %%eax, %[eax]\n\t"
                     "movl 4(%%esp), %%eax\n\t" "testl %%eax, %%eax\n\t" "jz 2f\n\t"
                     "cmpb $0, (%%eax)\n\t" "je 2f\n\t"
                     "movl 8(%%esp), %%eax\n\t" "orw $0, 0x12(%%eax)\n"
                     "2:\n\t" "movl %[index], %%eax\n\t" "jmp _own_common\n"
                     "1:\n\t" "pushl %[index]\n\t" "jmp _own_direct\n"
                     : : [lo] "m"(own_owner_lo), [hi] "m"(own_owner_hi), [busy] "m"(own_busy),
                         [eax] "m"(own_call.eax), [index] "i"(OWN_K_fputs));
}

static void own_routes_init(void)
{
#define OWN_ROUTE(name, id, kind, model) own_route[OWN_K_##id] = OWN_TARGET_##kind(own_entry_##id, own_lazy_##id);
    OWN_EXPORTS(OWN_ROUTE, OWN_NO_DATA)
    own_route[OWN_K_fputs] = (void *)own_entry_fputs_probe;
#undef OWN_ROUTE
}

/* ------------------------------------------------------------------------
 * Attach (own mode): what CW3220's DLL start-up and init records set up
 * that the 53 functions read, in their order (heap 4170fc, handles 409014,
 * streams 407248, time zone 41e268), and the lifecycle's state. */
static es2_own_test_setup own_setup;
static void own_init_state(void)
{
    own_io_layout_init();
    memset(&own_counters, 0, sizeof(own_counters));
    own_counters.exit_status = -1;
    for (unsigned b = 0; b < 256u; ++b) own_tolower_table[b] = (uint8_t)(b >= 'A' && b <= 'Z' ? b + 0x20u : b);
    own_data_ctype[0] = 0;
    for (unsigned b = 0; b < 256u; ++b) own_data_ctype[b + 1u] = b < 0x80u ? (uint8_t)es2_io_ctype((uint8_t)b) : 0u;
    es2_io_static_state(&own_io_env, &own_io, (uint32_t)(uintptr_t)&own_fmode_word, (uint32_t)(uintptr_t)own_dummy_hook);
    own_exit_hooks[2] = (uint32_t)(uintptr_t)own_dummy_hook;
    uint32_t sentinel = own_setup.sentinel ? own_setup.sentinel : (uint32_t)(uintptr_t)&own_head[2];
    es2_heap_init(&own_heap, &own_heap_env, sentinel, own_setup.platform >= 0 ? (uint32_t)own_setup.platform : own_platform());
    if (!(own_setup.flags & ES2_OWN_SETUP_NO_IO_START)) {
        es2_io_startup(&own_io_env, &own_io);
        es2_io_tzset(&own_io_env, &own_io, own_setup.environment ? own_setup.environment : (uint32_t)(uintptr_t)GetEnvironmentStrings());
    }
    es2_rtl_runtime_init(&own_life, (uint32_t)(uintptr_t)own_dummy_hook, (uint32_t)(uintptr_t)own_abort_text);
    own_life.code_base = own_setup.code_base ? own_setup.code_base : (uint32_t)(uintptr_t)own_module;
    own_life.stream_hooks = (uint32_t)(uintptr_t)own_exit_hooks;
    own_life_current = 0;
    own_life_site = NULL;
    own_life_return_rva = 0;
    if (own_setup.stack_hi) { own_owner_lo = own_setup.stack_lo; own_owner_hi = own_setup.stack_hi; }
    else {
        MEMORY_BASIC_INFORMATION region;
        uint32_t here = (uint32_t)(uintptr_t)&region, base;
        __asm__ volatile("movl %%fs:4, %0" : "=r"(base));
        own_owner_lo = VirtualQuery(&region, &region, sizeof(region)) ? (uint32_t)(uintptr_t)region.AllocationBase : here;
        own_owner_hi = base;
    }
    own_busy = 0;
    own_on_private = 0;
}
static BOOL own_attach(void)
{
    BYTE *stacks = VirtualAlloc(NULL, OWN_PRIVATE_STACK + OWN_LIFE_STACK, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!stacks) return FALSE;
    own_private_lo = (uint32_t)(uintptr_t)stacks;
    own_private_top = own_private_lo + OWN_PRIVATE_STACK;
    own_life_lo = own_private_top;
    own_life_top = own_life_lo + OWN_LIFE_STACK - 64u;
    own_routes_init();
    memset(&own_setup, 0, sizeof(own_setup));
    own_setup.platform = -1;
    own_init_state();
    return TRUE;
}
/* At detach: __startup's frame record off the chain (CW3220's exit record
 * 405260 with 4052c1). */
static void own_detach(void)
{
    uint32_t record = own_life.frame_record, head;
    if (!record) return;
    __asm__ volatile("movl %%fs:0, %0" : "=r"(head));
    if (head == record) {
        uint32_t next = OWN_U32(record);
        __asm__ volatile("movl %0, %%fs:0" ::"r"(next) : "memory");
        return;
    }
    for (uint32_t r = head; r != 0xffffffffu && r; r = OWN_U32(r))
        if (OWN_U32(r) == record) { OWN_U32(r) = OWN_U32(record); return; }
}

static int __cdecl own_reinitialize(const es2_own_test_setup *setup)
{
    if (own_mode != ES2_OWN_MODE_OWN || own_busy) return 1;
    if (setup) own_setup = *setup;
    else { memset(&own_setup, 0, sizeof(own_setup)); own_setup.platform = -1; }
    own_routes_init();
    own_init_state();
    return 0;
}

static es2_own_runtime_info own_info;
__attribute__((used)) const es2_own_runtime_info *__cdecl es2_own_runtime(void)
{
    own_info.version = ES2_OWN_RUNTIME_VERSION;
    own_info.mode = own_mode;
    own_info.image_base = (uint32_t)(uintptr_t)own_module;
    if (own_module) {
        const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)own_module;
        own_info.image_size = ((const IMAGE_NT_HEADERS32 *)((const BYTE *)own_module + dos->e_lfanew))->OptionalHeader.SizeOfImage;
    }
    own_info.io = &own_io;
    own_info.heap = &own_heap;
    own_info.lifecycle = &own_life;
    own_info.counters = &own_counters;
    own_info.private_stack_lo = own_private_lo;
    own_info.private_stack_hi = own_private_top;
    own_info.life_stack_lo = own_life_lo;
    own_info.life_stack_hi = own_life_lo + OWN_LIFE_STACK;
    own_info.exit_hooks = (uint32_t)(uintptr_t)own_exit_hooks;
    own_info.dummy_hook = (uint32_t)(uintptr_t)own_dummy_hook;
    own_info.reinitialize = own_reinitialize;
    own_info.ctype_block = (uint32_t)(uintptr_t)own_tolower_table;
    own_info.life_return = (uint32_t)(uintptr_t)own_life_return;
    own_info.life_return_rva = &own_life_return_rva;
    own_info.startup_handler = (uint32_t)(uintptr_t)own_startup_handler;
    own_info.window_callback = (uint32_t)(uintptr_t)own_window_found;
    own_info.abort_text = (uint32_t)(uintptr_t)own_abort_text;
    return &own_info;
}

/* ES.EXE and VSHELL.EXE run their own Borland start-up over the original. */
static int own_process_delegates(void)
{
    WCHAR exe[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);
    const WCHAR *name = n && n < MAX_PATH ? wcsrchr(exe, L'\\') : NULL;
    name = name ? name + 1 : exe;
    return n && n < MAX_PATH && (!_wcsicmp(name, L"ES.EXE") || !_wcsicmp(name, L"VSHELL.EXE"));
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        own_module = instance;
        if (own_process_delegates()) return own_delegate_attach();
        return own_attach();
    }
    if (reason == DLL_PROCESS_DETACH && own_mode == ES2_OWN_MODE_OWN) own_detach();
    return TRUE;
}
