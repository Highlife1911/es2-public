/* IAT-slot shim for the CW3220.DLL imports of DBSIM.EXE.
 * Design and evidence: docs/reference-game/rtl_imports.md, section
 * "the IAT-slot shim".
 *
 * DBSIM reaches every CW3220 import through one `jmp dword [slot]` thunk per
 * IAT slot (0x496754 + 6*i -> slot 0x6cc2d4 + 4*i). The shim replaces selected
 * slots, in memory only, with per-import forwarding thunks of this file. In
 * mode `original` a thunk counts the call
 * by caller category and tail-jumps to the CW3220 export with every register,
 * the flags' DF, the FPU state and the stack exactly as the caller left them:
 * it borrows EAX through the return-address slot (`xchg`) and writes nothing
 * below ESP. The thunks are reentrant (they call nothing) and count only on
 * the thread whose stack was recorded at install time (the main thread);
 * other threads are forwarded and counted in one shared counter.
 * Further modes (sections of rtl_imports.md):
 * - `replaced` and `compare` for the 20 imports with own code
 *   (core/src/rtl.c; "the pure imports as own code"): the thunk then jumps
 *   to an adapter of this file; other threads always reach the export.
 * - `shadow` for calloc, realloc and free ("the heap, shadow stage",
 *   rtl_heap_shadow.h): the original runs, a model of the CW3220 heap
 *   (core/src/rtl_heap.c) predicts every result and allocator word.
 * - `shadow` and `replaced` for the file and console imports ("file and
 *   console imports", rtl_io_shadow.h; core/src/rtl_io.c; the console stubs
 *   only `replaced`).
 * - `replaced` for calloc, realloc and free ("the heap, replaced",
 *   rtl_heap_own.h): an own instance of the heap model over own
 *   VirtualAlloc regions is the allocator of DBSIM's blocks.
 * - `replaced` for the lifecycle group __startup, _atexit, __exit, _abort
 *   and __ErrorExit, all five ("the lifecycle group as own code",
 *   rtl_life.h): the arming runs the own start-up (core/src/rtl_start.c)
 *   instead of CW3220's.
 * The own runtime ("the own cw3220.dll"):
 * with the own cw3220.dll (rtl/) in place of CW3220.DLL the thunks forward to
 * its exports as to the original's; the modes that work on CW3220.DLL's
 * internals (heap shadow, the I/O modes, the lifecycle group) are refused as
 * `own_runtime`, and the exit record names the runtime (rtl_runtime_check).
 *
 * Timing: the shim must see DBSIM's static initializers (init records run
 * inside __startup, before WinMain and long before the proxy's first
 * DirectDraw/MCI call). DllMain therefore only reads the environment
 * (ES2_RTL_CONFIG names the INI) and redirects the single `__startup` slot
 * to rtl_startup_entry. DBSIM's entry 0x401000 jumps through that slot after
 * every DllMain has run; rtl_startup_entry switches to a private stack,
 * restores the slot, reads the configuration, hashes DBSIM.EXE (own SHA-256:
 * no DLL is loaded this early), verifies the image and every CW3220 slot,
 * installs, switches back and jumps to the real __startup with the entry
 * registers, flags, FPU state, LastError and stack unchanged.
 *
 * Without ES2_RTL_CONFIG nothing at all is changed. Never installed for:
 * __ExceptionHandler (OS unwinding) and the data imports _errno and
 * __streams. __startup (the arming slot), __exit and _atexit (patched and
 * called by live coverage and the speed runtime) only as the lifecycle group
 * (rtl_life.h); __startup's slot is never patched for it.
 */
#ifndef ES2_RTL_IMPORTS_H
#define ES2_RTL_IMPORTS_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "hook/hook.h"
#include "es2/core/rtl.h"
#include "es2/rtl/own_runtime.h"
#include "sha256.h"

#define RTL_IMAGE_SIZE 0x002d8000u
#define RTL_IAT_RVA 0x002cc2d4u
#define RTL_THUNK_RVA 0x00096754u
#define RTL_IMPORT_COUNT 53u
#define RTL_STARTUP_INDEX 22u
#define RTL_FILE_SIZE 724512u
#define RTL_PRIVATE_STACK 262144u

/* Import table of cw3220.DLL in IAT order (slot i, thunk i). `key` names the
 * INI key and the tick fields (rtl_<key>_calls); `shim` 0 = never redirected
 * (__startup, _atexit and __exit only as the lifecycle group in mode
 * replaced, rtl_life.h). */
typedef struct rtl_import { const char *symbol; const char *key; BYTE shim; } rtl_import;
static const rtl_import rtl_imports[RTL_IMPORT_COUNT] = {
    {"_strupr","strupr",1},{"_atexit","atexit",0},{"_strncpy","strncpy",1},{"_strlen","strlen",1},
    {"__ErrorExit","ErrorExit",1},{"_rewind","rewind",1},{"_strncmp","strncmp",1},{"_write","write",1},
    {"_strstr","strstr",1},{"_strlwr","strlwr",1},{"_strnicmp","strnicmp",1},{"_sprintf","sprintf",1},
    {"__exit","exit",0},{"_strchr","strchr",1},{"_free","free",1},{"_errno","errno",0},
    {"_fscanf","fscanf",1},{"_memcmp","memcmp",1},{"_strncat","strncat",1},{"_open","open",1},
    {"_strrchr","strrchr",1},{"_printf","printf",1},{"__startup","startup",0},{"_ftell","ftell",1},
    {"@_CatchCleanup$qv","CatchCleanup",1},{"_strcat","strcat",1},{"__ExceptionHandler","ExceptionHandler",0},{"__fputc","fputc",1},
    {"_time","time",1},{"_fopen","fopen",1},{"_read","read",1},{"_memcpy","memcpy",1},
    {"_memmove","memmove",1},{"_memset","memset",1},{"_itoa","itoa",1},{"_lseek","lseek",1},
    {"_fseek","fseek",1},{"_fwrite","fwrite",1},{"_fclose","fclose",1},{"_vsprintf","vsprintf",1},
    {"_fputs","fputs",1},{"_fread","fread",1},{"_flushall","flushall",1},{"_filelength","filelength",1},
    {"_findfirst","findfirst",1},{"_findnext","findnext",1},{"_stricmp","stricmp",1},{"_calloc","calloc",1},
    {"_close","close",1},{"_realloc","realloc",1},{"_atol","atol",1},{"__streams","streams",0},
    {"_abort","abort",1},
};
enum {
    RTL_I_strlen = 3, RTL_I_sprintf = 11, RTL_I_strchr = 13, RTL_I_fscanf = 16, RTL_I_strcat = 25,
    RTL_I_fopen = 29, RTL_I_memcpy = 31, RTL_I_memmove = 32, RTL_I_memset = 33, RTL_I_itoa = 34,
    RTL_I_fclose = 38, RTL_I_stricmp = 46,
    RTL_I_free = 14, RTL_I_calloc = 47, RTL_I_realloc = 49,
    RTL_I_atexit = 1, RTL_I_ErrorExit = 4, RTL_I_exit = 12, RTL_I_abort = 52
};

/* The 85 call sites of simulation code (rtl_imports.md section 3: 20 in
 * registered hooks, 6 other frame-time, 59 mission load and static init) as
 * return-address RVAs (site + 5; all are E8 rel32 calls to the import's
 * thunk, checked at install and against the reference file by the fixture). */
typedef struct rtl_site { uint32_t ret_rva; BYTE import; } rtl_site;
static const rtl_site rtl_sim_sites[] = {
    {0x0225du,RTL_I_memset},{0x02350u,RTL_I_memset},{0x02528u,RTL_I_memset},{0x025ebu,RTL_I_memset},{0x027bcu,RTL_I_memcpy},
    {0x027d9u,RTL_I_memcpy},{0x027f1u,RTL_I_memcpy},{0x02997u,RTL_I_memcpy},{0x029b4u,RTL_I_memcpy},{0x029ccu,RTL_I_memcpy},
    {0x03007u,RTL_I_memcpy},{0x0356eu,RTL_I_memset},{0x07c38u,RTL_I_strcat},{0x07c49u,RTL_I_itoa},{0x07c7eu,RTL_I_itoa},
    {0x08547u,RTL_I_memset},{0x0aa70u,RTL_I_memset},{0x0b52fu,RTL_I_memset},{0x0d06au,RTL_I_memcpy},{0x0d092u,RTL_I_memcpy},
    {0x0d317u,RTL_I_memset},{0x0d32bu,RTL_I_memset},{0x0d33fu,RTL_I_memset},{0x0dde4u,RTL_I_memset},{0x0e00bu,RTL_I_memset},
    {0x0e01bu,RTL_I_memset},{0x0e031u,RTL_I_memset},{0x0e041u,RTL_I_memset},{0x0e053u,RTL_I_memset},{0x0efc2u,RTL_I_memset},
    {0x0fb53u,RTL_I_memset},{0x0fc03u,RTL_I_memset},{0x10858u,RTL_I_memset},{0x11a84u,RTL_I_memset},{0x11a94u,RTL_I_memset},
    {0x13e91u,RTL_I_memset},{0x13ea7u,RTL_I_memset},{0x1600cu,RTL_I_memset},{0x1fdd4u,RTL_I_memset},{0x200c3u,RTL_I_memset},
    {0x21745u,RTL_I_memset},{0x22db2u,RTL_I_memset},{0x23029u,RTL_I_memset},{0x23b97u,RTL_I_memset},{0x2432fu,RTL_I_memset},
    {0x24340u,RTL_I_memset},{0x24356u,RTL_I_memset},{0x24367u,RTL_I_memset},{0x24378u,RTL_I_memset},{0x24389u,RTL_I_memset},
    {0x2439cu,RTL_I_memset},{0x243b0u,RTL_I_memset},{0x243c4u,RTL_I_memset},{0x243d8u,RTL_I_memset},{0x2462au,RTL_I_strlen},
    {0x26362u,RTL_I_memcpy},{0x26377u,RTL_I_memcpy},{0x27936u,RTL_I_itoa},{0x28299u,RTL_I_memset},{0x5cb2au,RTL_I_memset},
    {0x60d0au,RTL_I_memset},{0x616f0u,RTL_I_strcat},{0x6a084u,RTL_I_memset},{0x6b290u,RTL_I_strcat},{0x6b39bu,RTL_I_itoa},
    {0x6b3a8u,RTL_I_strcat},{0x6b3c3u,RTL_I_strcat},{0x6b4e4u,RTL_I_memset},{0x6c81cu,RTL_I_strchr},{0x6c832u,RTL_I_stricmp},
    {0x6c8e5u,RTL_I_fopen},{0x6c91bu,RTL_I_fscanf},{0x6ca18u,RTL_I_fscanf},{0x6cb54u,RTL_I_fclose},{0x71b15u,RTL_I_memset},
    {0x71c5bu,RTL_I_sprintf},{0x78862u,RTL_I_memset},{0x78938u,RTL_I_memset},{0x78d2cu,RTL_I_memset},{0x78e73u,RTL_I_memset},
    {0x90bc2u,RTL_I_memcpy},{0x90f36u,RTL_I_memset},{0x92856u,RTL_I_memset},{0x92c76u,RTL_I_strlen},{0x92ea4u,RTL_I_memmove},
};
#define RTL_SIM_SITE_COUNT (sizeof(rtl_sim_sites) / sizeof(rtl_sim_sites[0]))

/* Per import, read by the thunks at fixed offsets (asm below). */
typedef struct rtl_counter { uint64_t calls, mismatches, hook, sim, dbsim, other; } rtl_counter;
_Static_assert(sizeof(rtl_counter) == 48 && offsetof(rtl_counter, hook) == 16 && offsetof(rtl_counter, sim) == 24 &&
               offsetof(rtl_counter, dbsim) == 32 && offsetof(rtl_counter, other) == 40, "thunk offsets");
typedef enum rtl_state_kind { RTL_OFF = 0, RTL_PENDING, RTL_INSTALLED, RTL_REFUSED } rtl_state_kind;

__attribute__((used)) static rtl_counter rtl_counters[RTL_IMPORT_COUNT];
__attribute__((used)) static uint32_t rtl_targets[RTL_IMPORT_COUNT];
__attribute__((used)) static uint32_t rtl_stack_lo, rtl_stack_hi, rtl_hook_lo, rtl_hook_hi, rtl_image_lo, rtl_image_size;
__attribute__((used)) static uint64_t rtl_foreign_calls;
/* Per import, compare-mode calls that were not compared (the own code
 * declined the call, the overlay overflowed, a nested call while another
 * adapter ran) and replaced-mode calls forwarded to the original (a format
 * outside the own subset, a nested call). Reported as rtl_<key>_skipped for
 * imports whose mode is not `original`. */
__attribute__((used)) static uint64_t rtl_skipped[RTL_IMPORT_COUNT];
/* One bit per byte of the image's first RTL_SIM_LIMIT bytes (headers and
 * CODE, which ends below RVA 0x97000): set at the return address of a
 * simulation site. 76 KiB; the thunk hard-codes the limit. */
#define RTL_SIM_LIMIT 0x00098000u
_Static_assert(RTL_SIM_LIMIT == 0x98000u, "rtl thunk hard-codes the bitmap limit");
__attribute__((used)) static uint32_t rtl_sim_bits[RTL_SIM_LIMIT / 32u];
static volatile LONG rtl_state;
static char rtl_reason[64];
static BYTE *rtl_installed_image;
static BYTE rtl_installed[RTL_IMPORT_COUNT];
__attribute__((used)) static uint32_t rtl_original[RTL_IMPORT_COUNT];
static es2_hook_mode rtl_modes[RTL_IMPORT_COUNT];
typedef BOOL (WINAPI *rtl_protect_fn)(LPVOID, SIZE_T, DWORD, PDWORD);
static rtl_protect_fn rtl_protect = VirtualProtect;

/* Forwarding thunks. Entry: [esp] = caller's return address, arguments above.
 * Owner thread = ESP inside the recorded stack reservation. Categories:
 * hook = return address inside the proxy module (a replaced hook's core
 * calling the import through the guest bridge), sim = a simulation call site
 * (bitmap), dbsim = other DBSIM image address, other = anything else.
 * Flags are clobbered (the callee's contract does not include them). */
__asm__(
    ".macro RTL_THUNK idx\n"
    ".text\n.p2align 4\n"
    "_rtl_thunk_\\idx:\n"
    "    cmpl _rtl_stack_lo, %esp\n    jb 9f\n"
    "    cmpl _rtl_stack_hi, %esp\n    jae 9f\n"
    "    xchgl %eax, (%esp)\n"
    "    addl $1, _rtl_counters+48*\\idx\n    adcl $0, _rtl_counters+48*\\idx+4\n"
    "    cmpl _rtl_hook_lo, %eax\n    jb 1f\n"
    "    cmpl _rtl_hook_hi, %eax\n    jae 1f\n"
    "    addl $1, _rtl_counters+48*\\idx+16\n    adcl $0, _rtl_counters+48*\\idx+20\n    jmp 4f\n"
    "1:  subl _rtl_image_lo, %eax\n"
    "    cmpl $0x98000, %eax\n    jae 6f\n"
    "    btl %eax, _rtl_sim_bits\n    jnc 2f\n"
    "    addl $1, _rtl_counters+48*\\idx+24\n    adcl $0, _rtl_counters+48*\\idx+28\n    jmp 5f\n"
    "6:  cmpl _rtl_image_size, %eax\n    jae 3f\n"
    "2:  addl $1, _rtl_counters+48*\\idx+32\n    adcl $0, _rtl_counters+48*\\idx+36\n    jmp 5f\n"
    "3:  addl $1, _rtl_counters+48*\\idx+40\n    adcl $0, _rtl_counters+48*\\idx+44\n"
    "5:  addl _rtl_image_lo, %eax\n"
    "4:  xchgl %eax, (%esp)\n"
    "    jmp *_rtl_targets+4*\\idx\n"
    "9:  lock addl $1, _rtl_foreign_calls\n    lock adcl $0, _rtl_foreign_calls+4\n"
    "    jmp *_rtl_original+4*\\idx\n"
    ".endm\n"
#define RTL_T(i) "RTL_THUNK " #i "\n"
    RTL_T(0) RTL_T(1) RTL_T(2) RTL_T(3) RTL_T(4) RTL_T(5) RTL_T(6) RTL_T(7) RTL_T(8) RTL_T(9)
    RTL_T(10) RTL_T(11) RTL_T(12) RTL_T(13) RTL_T(14) RTL_T(15) RTL_T(16) RTL_T(17) RTL_T(18) RTL_T(19)
    RTL_T(20) RTL_T(21) RTL_T(22) RTL_T(23) RTL_T(24) RTL_T(25) RTL_T(26) RTL_T(27) RTL_T(28) RTL_T(29)
    RTL_T(30) RTL_T(31) RTL_T(32) RTL_T(33) RTL_T(34) RTL_T(35) RTL_T(36) RTL_T(37) RTL_T(38) RTL_T(39)
    RTL_T(40) RTL_T(41) RTL_T(42) RTL_T(43) RTL_T(44) RTL_T(45) RTL_T(46) RTL_T(47) RTL_T(48) RTL_T(49)
    RTL_T(50) RTL_T(51) RTL_T(52)
#undef RTL_T
    ".purgem RTL_THUNK\n"
    ".section .rdata,\"dr\"\n.p2align 2\n"
    "_rtl_thunk_table:\n"
#define RTL_T(i) ".long _rtl_thunk_" #i "\n"
    RTL_T(0) RTL_T(1) RTL_T(2) RTL_T(3) RTL_T(4) RTL_T(5) RTL_T(6) RTL_T(7) RTL_T(8) RTL_T(9)
    RTL_T(10) RTL_T(11) RTL_T(12) RTL_T(13) RTL_T(14) RTL_T(15) RTL_T(16) RTL_T(17) RTL_T(18) RTL_T(19)
    RTL_T(20) RTL_T(21) RTL_T(22) RTL_T(23) RTL_T(24) RTL_T(25) RTL_T(26) RTL_T(27) RTL_T(28) RTL_T(29)
    RTL_T(30) RTL_T(31) RTL_T(32) RTL_T(33) RTL_T(34) RTL_T(35) RTL_T(36) RTL_T(37) RTL_T(38) RTL_T(39)
    RTL_T(40) RTL_T(41) RTL_T(42) RTL_T(43) RTL_T(44) RTL_T(45) RTL_T(46) RTL_T(47) RTL_T(48) RTL_T(49)
    RTL_T(50) RTL_T(51) RTL_T(52)
#undef RTL_T
    ".text\n");
extern const uint32_t rtl_thunk_table[RTL_IMPORT_COUNT] __asm__("_rtl_thunk_table");

static uint32_t rtl_u32(const BYTE *p) { uint32_t v; memcpy(&v, p, 4); return v; }

static BOOL rtl_refuse(const char *what, const char *key) {
    if (key) snprintf(rtl_reason, sizeof(rtl_reason), "%s_%s", what, key);
    else snprintf(rtl_reason, sizeof(rtl_reason), "%s", what);
    return FALSE;
}

/* ------------------------------------------------------------------------
 * Modes `replaced` and `compare` for the 20 imports with own code
 * (core/src/rtl.c; rtl_imports.md section "the pure imports as own code").
 *
 * The thunk above counts the call and jumps to rtl_targets[i]: the export
 * (original), rtl_replaced_<i> or rtl_compare_<i>. Both adapters save the
 * entry registers in `rtl_call`, run their C part on the private stack
 * (nothing of it lands on the game's stack) and keep EBX/ESI/EDI/EBP, DF
 * clear and the x87/SSE state untouched (the C parts are integer code).
 *
 * replaced: the own function runs on the game's memory; the adapter returns
 *   its EAX/ECX/EDX and, for the leaf functions whose CW3220 frame is only
 *   register pushes, writes what that frame leaves below the return address
 *   (rtl_residue). sprintf/vsprintf outside the own subset go to the export.
 * compare: the own function runs first, on the real pre-state, with every
 *   write going to a private overlay; then the export runs on the real state
 *   (its return address temporarily points to rtl_compare_post, the slot is
 *   restored afterwards), and the overlay bytes, EAX/ECX/EDX, guard bytes
 *   around the written range and the leaf residue are compared with what the
 *   export did. The caller always gets the export's results.
 * A call that arrives while an adapter runs (only possible if an export
 * called back into DBSIM's thunks) goes to the export and counts as skipped. */
__attribute__((used)) static uint32_t rtl_private_base, rtl_private_top;
typedef struct rtl_call_state {
    uint32_t index;                                   /*  0 */
    uint32_t eax, ecx, edx, ebx, esi, edi, ebp, esp;  /*  4: entry registers, ESP -> return address */
    uint32_t out_eax, out_ecx, out_edx;               /* 36 */
    uint32_t target;                                  /* 48 */
    uint32_t saved_return;                            /* 52 */
    uint32_t post_esp;                                /* 56 */
} rtl_call_state;
_Static_assert(offsetof(rtl_call_state, esp) == 32 && offsetof(rtl_call_state, out_eax) == 36 &&
               offsetof(rtl_call_state, target) == 48 && offsetof(rtl_call_state, saved_return) == 52 &&
               offsetof(rtl_call_state, post_esp) == 56, "adapter offsets");
__attribute__((used)) static rtl_call_state rtl_call;
__attribute__((used)) static volatile BYTE rtl_busy;
/* es2_rtl_function + 1 per import, 0 = no own code (set at install). */
static signed char rtl_own[RTL_IMPORT_COUNT];
static int rtl_own_function(unsigned i) {
    for (int f = 0; f < ES2_RTL_FUNCTION_COUNT; ++f)
        if (!strcmp(rtl_imports[i].symbol, es2_rtl_name((es2_rtl_function)f))) return f;
    return -1;
}
/* What a leaf function's frame leaves below its return address: the
 * registers it pushes, in push order ('p' EBP, 'b' EBX, 's' ESI, 'd' EDI;
 * es2_rtl_leaf_residue, shared with the own cw3220.dll). The other
 * own functions call further CW3220 code; their stack residue is not
 * reproduced (documented in rtl_imports.md). */
#define rtl_residue(f) es2_rtl_leaf_residue((es2_rtl_function)(f))
static uint32_t rtl_residue_value(char reg) {
    return reg == 'p' ? rtl_call.ebp : reg == 'b' ? rtl_call.ebx : reg == 's' ? rtl_call.esi : rtl_call.edi;
}

static uint8_t rtl_direct_read(void *context, uint32_t address) { (void)context; return *(const volatile uint8_t *)(uintptr_t)address; }
static void rtl_direct_write(void *context, uint32_t address, uint8_t value) { (void)context; *(volatile uint8_t *)(uintptr_t)address = value; }
static const es2_rtl_memory rtl_direct_memory = {NULL, rtl_direct_read, rtl_direct_write};

/* Compare overlay: written bytes by address (open addressing, generation
 * stamps instead of clearing). One allocation when compare is requested. */
#define RTL_OVERLAY_BITS 19u
#define RTL_OVERLAY_SLOTS (1u << RTL_OVERLAY_BITS)
#define RTL_OVERLAY_LIMIT (RTL_OVERLAY_SLOTS / 2u)
typedef struct rtl_overlay {
    uint32_t *keys, *generation, *written;
    uint8_t *values;
    uint32_t current, count;
    BOOL overflow;
} rtl_overlay;
static rtl_overlay rtl_shadow;
static BOOL rtl_overlay_allocate(void) {
    if (rtl_shadow.keys) return TRUE;
    size_t bytes = (size_t)RTL_OVERLAY_SLOTS * 9u + (size_t)RTL_OVERLAY_LIMIT * 4u;
    BYTE *block = VirtualAlloc(NULL, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!block) return FALSE;
    rtl_shadow.keys = (uint32_t *)block;
    rtl_shadow.generation = rtl_shadow.keys + RTL_OVERLAY_SLOTS;
    rtl_shadow.written = rtl_shadow.generation + RTL_OVERLAY_SLOTS;
    rtl_shadow.values = (uint8_t *)(rtl_shadow.written + RTL_OVERLAY_LIMIT);
    rtl_shadow.current = 1;
    return TRUE;
}
static void rtl_overlay_reset(void) {
    if (++rtl_shadow.current == 0) {
        memset(rtl_shadow.generation, 0, (size_t)RTL_OVERLAY_SLOTS * 4u);
        rtl_shadow.current = 1;
    }
    rtl_shadow.count = 0;
    rtl_shadow.overflow = FALSE;
}
static uint32_t rtl_overlay_find(uint32_t address, BOOL *found) {
    uint32_t h = (address * 2654435761u) >> (32u - RTL_OVERLAY_BITS);
    while (rtl_shadow.generation[h] == rtl_shadow.current) {
        if (rtl_shadow.keys[h] == address) { *found = TRUE; return h; }
        h = (h + 1u) & (RTL_OVERLAY_SLOTS - 1u);
    }
    *found = FALSE;
    return h;
}
static uint8_t rtl_overlay_read(void *context, uint32_t address) {
    (void)context;
    BOOL found;
    uint32_t h = rtl_overlay_find(address, &found);
    return found ? rtl_shadow.values[h] : *(const volatile uint8_t *)(uintptr_t)address;
}
static void rtl_overlay_write(void *context, uint32_t address, uint8_t value) {
    (void)context;
    BOOL found;
    uint32_t h = rtl_overlay_find(address, &found);
    if (!found) {
        if (rtl_shadow.count >= RTL_OVERLAY_LIMIT) { rtl_shadow.overflow = TRUE; return; }
        rtl_shadow.generation[h] = rtl_shadow.current;
        rtl_shadow.keys[h] = address;
        rtl_shadow.written[rtl_shadow.count++] = address;
    }
    rtl_shadow.values[h] = value;
}
static const es2_rtl_memory rtl_overlay_memory = {NULL, rtl_overlay_read, rtl_overlay_write};

/* First compare mismatch of the run (exit record field rtl_first_mismatch). */
static char rtl_mismatch_detail[192];
static es2_rtl_regs rtl_own_regs;
static BOOL rtl_compare_skip;
/* Guard bytes: up to 8 below the lowest and 8 above the highest written
 * address, inside the same 4 KiB page (readable because the export reads or
 * writes that page), never in the stack below the arguments. */
static uint32_t rtl_guard_address[16];
static uint8_t rtl_guard_value[16];
static unsigned rtl_guard_count;
static BOOL rtl_guard_allowed(uint32_t address, uint32_t page, uint32_t args) {
    if ((address & ~0xfffu) != page) return FALSE;
    if (address >= rtl_stack_lo && address < args) return FALSE;
    BOOL found;
    rtl_overlay_find(address, &found);
    return !found;
}
static void rtl_guards_take(uint32_t args) {
    rtl_guard_count = 0;
    if (!rtl_shadow.count) return;
    uint32_t lo = rtl_shadow.written[0], hi = lo;
    for (uint32_t k = 1; k < rtl_shadow.count; ++k) {
        uint32_t a = rtl_shadow.written[k];
        if (a < lo) lo = a;
        if (a > hi) hi = a;
    }
    for (uint32_t d = 1; d <= 8; ++d) {
        if (lo >= d && rtl_guard_allowed(lo - d, lo & ~0xfffu, args)) rtl_guard_address[rtl_guard_count++] = lo - d;
        if (hi + d > hi && rtl_guard_allowed(hi + d, hi & ~0xfffu, args)) rtl_guard_address[rtl_guard_count++] = hi + d;
    }
    for (unsigned k = 0; k < rtl_guard_count; ++k) rtl_guard_value[k] = *(const volatile uint8_t *)(uintptr_t)rtl_guard_address[k];
}

extern void rtl_compare_post(void) __asm__("_rtl_compare_post");
/* rtl_heap_own.h: calloc/realloc/free replaced by the own heap. */
static BYTE rtl_heap_replaced[RTL_IMPORT_COUNT];
static int rtl_heap_own_call(unsigned import);
/* rtl_life.h: the slot imports of the lifecycle group; _atexit is a
 * leaf through this adapter. */
static BYTE rtl_life_replaced[RTL_IMPORT_COUNT];
static int rtl_life_atexit_call(void);
int __cdecl rtl_replaced_c(void) __asm__("_rtl_replaced_c") __attribute__((used));
int __cdecl rtl_replaced_c(void) {
    unsigned i = rtl_call.index;
    if (rtl_heap_replaced[i]) return rtl_heap_own_call(i);
    if (rtl_life_replaced[i]) return rtl_life_atexit_call();
    int f = rtl_own[i] - 1;
    es2_rtl_regs r = {rtl_call.eax, rtl_call.ecx, rtl_call.edx};
    if (f < 0 || es2_rtl_call((es2_rtl_function)f, &rtl_direct_memory, rtl_call.esp + 4u, &r) != ES2_RTL_DONE) {
        ++rtl_skipped[i];
        return 1;
    }
    rtl_call.out_eax = r.eax; rtl_call.out_ecx = r.ecx; rtl_call.out_edx = r.edx;
    const char *residue = rtl_residue(f);
    for (unsigned k = 0; residue && residue[k]; ++k)
        *(volatile uint32_t *)(uintptr_t)(rtl_call.esp - 4u * (k + 1u)) = rtl_residue_value(residue[k]);
    return 0;
}
void __cdecl rtl_compare_before_c(void) __asm__("_rtl_compare_before_c") __attribute__((used));
void __cdecl rtl_compare_before_c(void) {
    unsigned i = rtl_call.index;
    int f = rtl_own[i] - 1;
    rtl_overlay_reset();
    rtl_own_regs.eax = rtl_call.eax; rtl_own_regs.ecx = rtl_call.ecx; rtl_own_regs.edx = rtl_call.edx;
    rtl_compare_skip = f < 0 ||
        es2_rtl_call((es2_rtl_function)f, &rtl_overlay_memory, rtl_call.esp + 4u, &rtl_own_regs) != ES2_RTL_DONE ||
        rtl_shadow.overflow;
    if (!rtl_compare_skip) rtl_guards_take(rtl_call.esp + 4u);
    volatile uint32_t *slot = (volatile uint32_t *)(uintptr_t)rtl_call.esp;
    rtl_call.saved_return = *slot;
    *slot = (uint32_t)(uintptr_t)rtl_compare_post;
}
void __cdecl rtl_compare_after_c(void) __asm__("_rtl_compare_after_c") __attribute__((used));
void __cdecl rtl_compare_after_c(void) {
    unsigned i = rtl_call.index;
    uint32_t e = rtl_call.esp;
    *(volatile uint32_t *)(uintptr_t)e = rtl_call.saved_return;   /* as the export's `ret` leaves it */
    if (rtl_compare_skip) { ++rtl_skipped[i]; return; }
    const char *what = NULL;
    uint32_t where = 0;
    if (rtl_own_regs.eax != rtl_call.out_eax || rtl_own_regs.ecx != rtl_call.out_ecx || rtl_own_regs.edx != rtl_call.out_edx)
        what = "registers";
    for (uint32_t k = 0; !what && k < rtl_shadow.count; ++k) {
        uint32_t a = rtl_shadow.written[k];
        BOOL found;
        uint32_t h = rtl_overlay_find(a, &found);
        if (rtl_shadow.values[h] != *(const volatile uint8_t *)(uintptr_t)a) { what = "memory"; where = a; }
    }
    for (unsigned k = 0; !what && k < rtl_guard_count; ++k)
        if (rtl_guard_value[k] != *(const volatile uint8_t *)(uintptr_t)rtl_guard_address[k]) { what = "guard"; where = rtl_guard_address[k]; }
    const char *residue = rtl_residue(rtl_own[i] - 1);
    for (unsigned k = 0; !what && residue && residue[k]; ++k) {
        uint32_t a = e - 4u * (k + 1u);
        if (*(const volatile uint32_t *)(uintptr_t)a != rtl_residue_value(residue[k])) { what = "residue"; where = a; }
    }
    if (!what) return;
    ++rtl_counters[i].mismatches;
    if (!rtl_mismatch_detail[0]) {
        const volatile uint32_t *a = (const volatile uint32_t *)(uintptr_t)(e + 4u);
        snprintf(rtl_mismatch_detail, sizeof(rtl_mismatch_detail),
                 "%s:%s@%08x:args=%08x,%08x,%08x:own=%08x,%08x,%08x:orig=%08x,%08x,%08x",
                 rtl_imports[i].key, what, (unsigned)where, (unsigned)a[0], (unsigned)a[1], (unsigned)a[2],
                 (unsigned)rtl_own_regs.eax, (unsigned)rtl_own_regs.ecx, (unsigned)rtl_own_regs.edx,
                 (unsigned)rtl_call.out_eax, (unsigned)rtl_call.out_ecx, (unsigned)rtl_call.out_edx);
    }
}

/* Adapters. Entry from the thunk: [esp] = return address, every register as
 * the caller left it (only the arithmetic flags differ). */
__asm__(
    ".macro RTL_ADAPTER idx\n"
    ".text\n.p2align 4\n"
    "_rtl_replaced_\\idx:\n"
    "    cmpb $0, _rtl_busy\n    jne 1f\n"
    "    movl %eax, _rtl_call+4\n    movl $\\idx, %eax\n    jmp _rtl_replaced_common\n"
    "1:  addl $1, _rtl_skipped+8*\\idx\n    adcl $0, _rtl_skipped+8*\\idx+4\n"
    "    jmp *_rtl_original+4*\\idx\n"
    ".p2align 4\n"
    "_rtl_compare_\\idx:\n"
    "    cmpb $0, _rtl_busy\n    jne 2f\n"
    "    movl %eax, _rtl_call+4\n    movl $\\idx, %eax\n    jmp _rtl_compare_common\n"
    "2:  addl $1, _rtl_skipped+8*\\idx\n    adcl $0, _rtl_skipped+8*\\idx+4\n"
    "    jmp *_rtl_original+4*\\idx\n"
    ".endm\n"
#define RTL_T(i) "RTL_ADAPTER " #i "\n"
    RTL_T(0) RTL_T(1) RTL_T(2) RTL_T(3) RTL_T(4) RTL_T(5) RTL_T(6) RTL_T(7) RTL_T(8) RTL_T(9)
    RTL_T(10) RTL_T(11) RTL_T(12) RTL_T(13) RTL_T(14) RTL_T(15) RTL_T(16) RTL_T(17) RTL_T(18) RTL_T(19)
    RTL_T(20) RTL_T(21) RTL_T(22) RTL_T(23) RTL_T(24) RTL_T(25) RTL_T(26) RTL_T(27) RTL_T(28) RTL_T(29)
    RTL_T(30) RTL_T(31) RTL_T(32) RTL_T(33) RTL_T(34) RTL_T(35) RTL_T(36) RTL_T(37) RTL_T(38) RTL_T(39)
    RTL_T(40) RTL_T(41) RTL_T(42) RTL_T(43) RTL_T(44) RTL_T(45) RTL_T(46) RTL_T(47) RTL_T(48) RTL_T(49)
    RTL_T(50) RTL_T(51) RTL_T(52)
#undef RTL_T
    ".purgem RTL_ADAPTER\n"
    /* Save the entry registers (EAX is already in rtl_call+4, the index in
     * EAX), switch to the private stack and run the C part. */
    ".macro RTL_SAVE\n"
    "    movl %eax, _rtl_call\n"
    "    movl %ecx, _rtl_call+8\n    movl %edx, _rtl_call+12\n    movl %ebx, _rtl_call+16\n"
    "    movl %esi, _rtl_call+20\n    movl %edi, _rtl_call+24\n    movl %ebp, _rtl_call+28\n    movl %esp, _rtl_call+32\n"
    "    movb $1, _rtl_busy\n"
    "    movl _rtl_private_top, %esp\n    cld\n"
    ".endm\n"
    "_rtl_replaced_common:\n"
    "    RTL_SAVE\n"
    "    call _rtl_replaced_c\n"
    "    movl _rtl_call+32, %esp\n"
    "    movb $0, _rtl_busy\n"
    "    testl %eax, %eax\n    jnz 3f\n"
    "    movl _rtl_call+36, %eax\n    movl _rtl_call+40, %ecx\n    movl _rtl_call+44, %edx\n"
    "    ret\n"
    "3:  movl _rtl_call, %eax\n    movl _rtl_original(,%eax,4), %eax\n    movl %eax, _rtl_call+48\n"
    "    movl _rtl_call+4, %eax\n    movl _rtl_call+8, %ecx\n    movl _rtl_call+12, %edx\n"
    "    jmp *_rtl_call+48\n"
    ".p2align 4\n"
    "_rtl_compare_common:\n"
    "    RTL_SAVE\n"
    "    call _rtl_compare_before_c\n"
    "    movl _rtl_call+32, %esp\n"
    "    movl _rtl_call, %eax\n    movl _rtl_original(,%eax,4), %eax\n    movl %eax, _rtl_call+48\n"
    "    movl _rtl_call+4, %eax\n    movl _rtl_call+8, %ecx\n    movl _rtl_call+12, %edx\n"
    "    jmp *_rtl_call+48\n"
    /* The export returns here with ESP = entry ESP + 4. */
    ".p2align 4\n"
    "_rtl_compare_post:\n"
    "    movl %eax, _rtl_call+36\n    movl %ecx, _rtl_call+40\n    movl %edx, _rtl_call+44\n"
    "    movl %esp, _rtl_call+56\n"
    "    movl _rtl_private_top, %esp\n    cld\n"
    "    call _rtl_compare_after_c\n"
    "    movl _rtl_call+56, %esp\n"
    "    movb $0, _rtl_busy\n"
    "    movl _rtl_call+36, %eax\n    movl _rtl_call+40, %ecx\n    movl _rtl_call+44, %edx\n"
    "    jmp *_rtl_call+52\n"
    ".purgem RTL_SAVE\n"
    ".section .rdata,\"dr\"\n.p2align 2\n"
    "_rtl_replaced_table:\n"
#define RTL_T(i) ".long _rtl_replaced_" #i "\n"
    RTL_T(0) RTL_T(1) RTL_T(2) RTL_T(3) RTL_T(4) RTL_T(5) RTL_T(6) RTL_T(7) RTL_T(8) RTL_T(9)
    RTL_T(10) RTL_T(11) RTL_T(12) RTL_T(13) RTL_T(14) RTL_T(15) RTL_T(16) RTL_T(17) RTL_T(18) RTL_T(19)
    RTL_T(20) RTL_T(21) RTL_T(22) RTL_T(23) RTL_T(24) RTL_T(25) RTL_T(26) RTL_T(27) RTL_T(28) RTL_T(29)
    RTL_T(30) RTL_T(31) RTL_T(32) RTL_T(33) RTL_T(34) RTL_T(35) RTL_T(36) RTL_T(37) RTL_T(38) RTL_T(39)
    RTL_T(40) RTL_T(41) RTL_T(42) RTL_T(43) RTL_T(44) RTL_T(45) RTL_T(46) RTL_T(47) RTL_T(48) RTL_T(49)
    RTL_T(50) RTL_T(51) RTL_T(52)
#undef RTL_T
    "_rtl_compare_table:\n"
#define RTL_T(i) ".long _rtl_compare_" #i "\n"
    RTL_T(0) RTL_T(1) RTL_T(2) RTL_T(3) RTL_T(4) RTL_T(5) RTL_T(6) RTL_T(7) RTL_T(8) RTL_T(9)
    RTL_T(10) RTL_T(11) RTL_T(12) RTL_T(13) RTL_T(14) RTL_T(15) RTL_T(16) RTL_T(17) RTL_T(18) RTL_T(19)
    RTL_T(20) RTL_T(21) RTL_T(22) RTL_T(23) RTL_T(24) RTL_T(25) RTL_T(26) RTL_T(27) RTL_T(28) RTL_T(29)
    RTL_T(30) RTL_T(31) RTL_T(32) RTL_T(33) RTL_T(34) RTL_T(35) RTL_T(36) RTL_T(37) RTL_T(38) RTL_T(39)
    RTL_T(40) RTL_T(41) RTL_T(42) RTL_T(43) RTL_T(44) RTL_T(45) RTL_T(46) RTL_T(47) RTL_T(48) RTL_T(49)
    RTL_T(50) RTL_T(51) RTL_T(52)
#undef RTL_T
    ".text\n");
extern const uint32_t rtl_replaced_table[RTL_IMPORT_COUNT] __asm__("_rtl_replaced_table");
extern const uint32_t rtl_compare_table[RTL_IMPORT_COUNT] __asm__("_rtl_compare_table");
static BOOL rtl_private_stack_allocate(void) {
    if (rtl_private_base) return TRUE;
    BYTE *stack = VirtualAlloc(NULL, RTL_PRIVATE_STACK, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!stack) return FALSE;
    rtl_private_base = (uint32_t)(uintptr_t)stack;
    rtl_private_top = rtl_private_base + RTL_PRIVATE_STACK;
    return TRUE;
}

/* ------------------------------------------------------------------------
 * SHA-256: the proxy's shared sha256.h (own implementation, no CryptoAPI: the
 * reference check runs at DBSIM's entry point, where loading the provider
 * would add DLL loads before the game's static initializers). */
/* A file on disk with the given size and full SHA-256. */
static BOOL rtl_file_matches(const WCHAR *path, DWORD size, const char *sha256) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    static BYTE buffer[16384];
    sha256_ctx s; sha256_init(&s);
    DWORD got = 0, total = 0; BOOL ok = TRUE;
    while ((ok = ReadFile(file, buffer, sizeof(buffer), &got, NULL)) && got) { total += got; sha256_update(&s, buffer, got); }
    CloseHandle(file);
    char hex[65]; sha256_hex(&s, hex);
    return ok && total == size && !strcmp(hex, sha256);
}
/* The unchanged executable on disk: size and full SHA-256 (hooks.md). */
static BOOL rtl_reference_file(const WCHAR *path) {
    return rtl_file_matches(path, RTL_FILE_SIZE, "8561aa47e5bb1bf1f837044b08e5d807e8de492784331708a7ddeabf6d44c4ad");
}

/* ------------------------------------------------------------------------
 * Layout: import names, thunk bytes and simulation call sites, all relative
 * to `map` (a loaded image, or the reference file mapped by the fixture);
 * absolute addresses use `link_base`. */
static BOOL rtl_verify_layout(const BYTE *map, size_t size, uint32_t link_base) {
    if (size != RTL_IMAGE_SIZE) return rtl_refuse("image_size", NULL);
    uint32_t pe = rtl_u32(map + 0x3c);
    if (pe < 64 || pe > 4096 - 248) return rtl_refuse("pe_header", NULL);
    uint32_t dir = rtl_u32(map + pe + 128), extent = rtl_u32(map + pe + 132);
    if (dir >= size || extent > size - dir) return rtl_refuse("import_directory", NULL);
    const BYTE *names = NULL;
    for (uint32_t d = 0; d + 20 <= extent; d += 20) {
        const BYTE *descriptor = map + dir + d;
        uint32_t name = rtl_u32(descriptor + 12);
        if (!name) break;
        if (name > size - 11 || _strnicmp((const char *)map + name, "cw3220.DLL", 11)) continue;
        uint32_t lookup = rtl_u32(descriptor), slots = rtl_u32(descriptor + 16);
        if (slots != RTL_IAT_RVA) return rtl_refuse("iat_rva", NULL);
        if (!lookup || lookup > size - 4 * (RTL_IMPORT_COUNT + 1)) return rtl_refuse("import_lookup", NULL);
        names = map + lookup;
        break;
    }
    if (!names) return rtl_refuse("import_descriptor", NULL);
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) {
        uint32_t entry = rtl_u32(names + 4 * i);
        size_t length = strlen(rtl_imports[i].symbol) + 1;
        if (!entry || (entry & 0x80000000u) || entry > size - length - 2 || memcmp(map + entry + 2, rtl_imports[i].symbol, length))
            return rtl_refuse("import_name", rtl_imports[i].key);
    }
    if (rtl_u32(names + 4 * RTL_IMPORT_COUNT)) return rtl_refuse("import_count", NULL);
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) {
        const BYTE *thunk = map + RTL_THUNK_RVA + 6 * i;
        if (thunk[0] != 0xff || thunk[1] != 0x25 || rtl_u32(thunk + 2) != link_base + RTL_IAT_RVA + 4 * i)
            return rtl_refuse("thunk", rtl_imports[i].key);
    }
    for (unsigned i = 0; i < RTL_SIM_SITE_COUNT; ++i) {
        uint32_t r = rtl_sim_sites[i].ret_rva;
        if (r < 5 || r >= RTL_SIM_LIMIT || r >= size || map[r - 5] != 0xe8 ||
            r + rtl_u32(map + r - 4) != RTL_THUNK_RVA + 6u * rtl_sim_sites[i].import) {
            char site[16]; snprintf(site, sizeof(site), "%05x", (unsigned)(r - 5));
            return rtl_refuse("sim_site", site);
        }
    }
    return TRUE;
}

/* ------------------------------------------------------------------------
 * Install / uninstall. */
typedef FARPROC (*rtl_resolver)(const char *symbol, void *context);
typedef struct rtl_options {
    BYTE *image;
    uintptr_t expected_base;
    size_t image_size;
    rtl_resolver resolve;
    void *resolve_context;
    uintptr_t stack_lo, stack_hi;   /* owner thread's stack reservation */
    uintptr_t hook_lo, hook_hi;     /* proxy module (replaced hooks' code) */
    BOOL selected[RTL_IMPORT_COUNT];
    es2_hook_mode modes[RTL_IMPORT_COUNT];
    BYTE shadow[RTL_IMPORT_COUNT];  /* mode shadow (calloc, realloc, free; all three or none) */
    BYTE *cw_image;                 /* the loaded CW3220.DLL (shadow only) */
    WCHAR heap_trace[MAX_PATH];     /* the heap trace's file (shadow only); empty: none */
} rtl_options;

static FARPROC rtl_resolve_module(const char *symbol, void *context) {
    return context ? GetProcAddress((HMODULE)context, symbol) : NULL;
}
static BOOL rtl_module_range(const void *address, uintptr_t *lo, uintptr_t *hi) {
    HMODULE module = NULL;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)address, &module) || !module) return FALSE;
    const BYTE *base = (const BYTE *)module;
    const IMAGE_NT_HEADERS32 *nt = (const IMAGE_NT_HEADERS32 *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
    *lo = (uintptr_t)base; *hi = (uintptr_t)base + nt->OptionalHeader.SizeOfImage;
    return TRUE;
}
/* The stack reservation that contains `address`: [allocation base, stack base). */
static uintptr_t rtl_stack_floor(const void *address) {
    MEMORY_BASIC_INFORMATION region;
    return VirtualQuery(address, &region, sizeof(region)) ? (uintptr_t)region.AllocationBase : 0;
}

/* Swap one slot from `from` to `to` (compare-exchange). */
static BOOL rtl_swap(DWORD *slot, uint32_t from, uint32_t to) {
    DWORD old, ignored;
    if (!rtl_protect(slot, 4, PAGE_READWRITE, &old)) return FALSE;
    BOOL ok = (uint32_t)InterlockedCompareExchange((volatile LONG *)slot, (LONG)to, (LONG)from) == from;
    if (!rtl_protect(slot, 4, old, &ignored)) {
        if (ok) InterlockedCompareExchange((volatile LONG *)slot, (LONG)from, (LONG)to);
        return FALSE;
    }
    return ok;
}
#include "rtl_heap_shadow.h"
#include "rtl_heap_own.h"
#include "rtl_io_shadow.h"
#include "rtl_life.h"
/* The arming's jump target after the arming step (CW3220's __startup,
 * or the own start-up of rtl_life.h when the lifecycle group installed). */
__attribute__((used)) static uint32_t rtl_startup_target;
/* Restores every installed slot that still holds its thunk. TRUE when all
 * slots are original again; a slot changed by someone else is left alone.
 * Also the CW3220 entries and slots of the heap shadow. */
__attribute__((unused)) static BOOL rtl_uninstall(void) {
    if (!rtl_installed_image) return TRUE;
    BOOL all = TRUE;
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) {
        if (!rtl_installed[i]) continue;
        DWORD *slot = (DWORD *)(rtl_installed_image + RTL_IAT_RVA + 4 * i);
        if (rtl_swap(slot, rtl_thunk_table[i], rtl_original[i])) rtl_installed[i] = 0;
        else all = FALSE;
    }
    rtl_heap_unpatch();
    if (!rtl_heap_unpatched()) all = FALSE;
    rtl_io_unpatch();
    if (!rtl_io_unpatched()) all = FALSE;
    if (all) {
        if (rtl_heap_trace_on && rtl_heap_trace_flush()) CloseHandle(rtl_heap_trace_file);
        rtl_heap_trace_file = INVALID_HANDLE_VALUE;
        rtl_heap_trace_on = FALSE;
        rtl_installed_image = NULL;
        rtl_heap_active = FALSE;
        rtl_heap_cw = NULL;
        memset(rtl_shadowed, 0, sizeof(rtl_shadowed));
        rtl_heap_own_active = FALSE;
        memset(rtl_heap_replaced, 0, sizeof(rtl_heap_replaced));
        rtl_io_active = FALSE;
        rtl_io_cw = NULL;
        memset(rtl_io_shadowed, 0, sizeof(rtl_io_shadowed));
        memset(rtl_io_replaced, 0, sizeof(rtl_io_replaced));
        rtl_life_active = FALSE;
        memset(rtl_life_replaced, 0, sizeof(rtl_life_replaced));
        InterlockedExchange(&rtl_state, RTL_OFF);
    }
    return all;
}
/* What a slot holds while the shim owns it (live_coverage.h accepts
 * it for _atexit and __exit in place of the export); 0 when not installed. */
__attribute__((unused)) static uint32_t rtl_import_slot_value(const char *symbol) {
    if (!rtl_installed_image || rtl_state != RTL_INSTALLED) return 0;
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i)
        if (rtl_installed[i] && !strcmp(rtl_imports[i].symbol, symbol)) return rtl_thunk_table[i];
    return 0;
}

/* The runtime behind the cw3220 imports, checked by the arming: the
 * own cw3220.dll (rtl/; it exports es2_own_runtime) or CW3220.DLL. The modes
 * that work on CW3220.DLL's internals (`internal`: the heap shadow, the I/O
 * modes, the lifecycle group) need the reference file and are refused as
 * own_runtime with the own DLL; the forwarding modes and the own code of
 * rtl.c and rtl_heap_own.h work with either. */
static BOOL rtl_runtime_known;
static const es2_own_runtime_info *rtl_own_runtime;
static BOOL rtl_runtime_check(HMODULE cw, BOOL internal) {
    es2_own_runtime_fn describe = cw ? (es2_own_runtime_fn)(void *)GetProcAddress(cw, ES2_OWN_RUNTIME_EXPORT) : NULL;
    const es2_own_runtime_info *info = describe ? describe() : NULL;
    rtl_own_runtime = info && info->version == ES2_OWN_RUNTIME_VERSION ? info : NULL;
    rtl_runtime_known = TRUE;
    if (!internal) return TRUE;
    if (describe) return rtl_refuse("own_runtime", NULL);
    WCHAR path[MAX_PATH];
    DWORD length = cw ? GetModuleFileNameW(cw, path, MAX_PATH) : 0;
    if (!length || length >= MAX_PATH || !rtl_file_matches(path, RTL_HEAP_CW_FILE_SIZE, RTL_HEAP_CW_SHA256)) return rtl_refuse("cw_hash", NULL);
    return TRUE;
}
/* Exit-record fields of the runtime: its kind and, for the own DLL, whether
 * the original was loaded into the process (CW3220-ORIGINAL.DLL, only for a
 * name outside DBSIM's 53) and the DLL's counters. -1: does not fit. */
static int rtl_runtime_fields(char *out, size_t capacity) {
    if (!rtl_runtime_known) return 0;
    int n;
    if (!rtl_own_runtime) n = snprintf(out, capacity, ",\"rtl_runtime\":\"cw3220\"");
    else {
        const es2_own_counters *k = rtl_own_runtime->counters;
        n = snprintf(out, capacity,
                     ",\"rtl_runtime\":\"own\",\"rtl_runtime_original_loaded\":%d,\"rtl_own_calls\":%llu,\"rtl_own_foreign_calls\":%llu,"
                     "\"rtl_own_nested_calls\":%llu,\"rtl_own_declined\":%llu,\"rtl_own_delegated\":%llu,\"rtl_own_exit_status\":%lld",
                     GetModuleHandleW(L"" ES2_OWN_ORIGINAL_FILE) != NULL, (unsigned long long)k->calls, (unsigned long long)k->foreign_calls,
                     (unsigned long long)k->nested_calls, (unsigned long long)k->declined, (unsigned long long)k->delegated,
                     (long long)k->exit_status);
    }
    return n > 0 && (size_t)n < capacity ? n : -1;
}

static BOOL rtl_install(const rtl_options *o) {
    if (rtl_installed_image) return rtl_refuse("busy", NULL);
    BOOL adapters = FALSE, compare = FALSE, io = FALSE, io_shadow = FALSE;
    unsigned shadowed = 0, heap_replaced = 0, life_replaced = 0;
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) {
        /* __startup, _atexit and __exit only as the lifecycle group. */
        if (o->selected[i] && !rtl_imports[i].shim && (rtl_life_index(i) < 0 || o->modes[i] != ES2_HOOK_REPLACED || o->shadow[i]))
            return rtl_refuse("not_shimmable", rtl_imports[i].key);
        int io_function = rtl_io_function_of(i);
        if (o->shadow[i]) {
            if (io_function >= 0) {
                /* Shadow for the file imports, not the console stubs. */
                if (es2_io_is_stub((es2_io_function)io_function) || o->modes[i] != ES2_HOOK_ORIGINAL)
                    return rtl_refuse("mode_reserved", rtl_imports[i].key);
                io = io || o->selected[i];
                io_shadow = io_shadow || o->selected[i];
                continue;
            }
            /* Shadow only for the three heap imports, all together. */
            if (rtl_heap_function_of(i) < 0 || o->modes[i] != ES2_HOOK_ORIGINAL) return rtl_refuse("mode_reserved", rtl_imports[i].key);
            shadowed += o->selected[i] ? 1u : 0u;
            continue;
        }
        if (o->modes[i] == ES2_HOOK_ORIGINAL) continue;
        if (rtl_life_index(i) >= 0) {
            /* Replaced for the lifecycle group, all five together; no compare. */
            if (o->modes[i] != ES2_HOOK_REPLACED) return rtl_refuse("mode_reserved", rtl_imports[i].key);
            life_replaced += o->selected[i] ? 1u : 0u;
            continue;
        }
        if (io_function >= 0) {
            /* Replaced for every import of the set; no compare (I/O cannot run twice). */
            if (o->modes[i] != ES2_HOOK_REPLACED) return rtl_refuse("mode_reserved", rtl_imports[i].key);
            io = io || o->selected[i];
            continue;
        }
        if (rtl_heap_function_of(i) >= 0) {
            /* Replaced for the three heap imports, all together; no compare. */
            if (o->modes[i] != ES2_HOOK_REPLACED) return rtl_refuse("mode_reserved", rtl_imports[i].key);
            heap_replaced += o->selected[i] ? 1u : 0u;
            continue;
        }
        /* compare/replaced only for the imports with own code. */
        if ((o->modes[i] != ES2_HOOK_REPLACED && o->modes[i] != ES2_HOOK_COMPARE) || rtl_own_function(i) < 0)
            return rtl_refuse("mode_reserved", rtl_imports[i].key);
        adapters = adapters || o->selected[i];
        compare = compare || (o->selected[i] && o->modes[i] == ES2_HOOK_COMPARE);
    }
    if (o->image_size != RTL_IMAGE_SIZE || o->expected_base > UINT32_MAX - RTL_IMAGE_SIZE ||
        !es2_hook_image_valid(o->image, o->expected_base, o->image_size)) return rtl_refuse("image", NULL);
    if (!rtl_verify_layout(o->image, o->image_size, (uint32_t)o->expected_base)) return FALSE;
    if (!o->resolve || !o->stack_lo || o->stack_hi <= o->stack_lo || o->hook_hi <= o->hook_lo) return rtl_refuse("options", NULL);
    uint32_t slots[RTL_IMPORT_COUNT];
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) {
        FARPROC expected = o->resolve(rtl_imports[i].symbol, o->resolve_context);
        slots[i] = rtl_u32(o->image + RTL_IAT_RVA + 4 * i);
        if (!expected || slots[i] != (uint32_t)(uintptr_t)expected) return rtl_refuse("slot", rtl_imports[i].key);
    }
    BOOL heap = shadowed != 0, heap_own = heap_replaced != 0;
    if (heap && (shadowed != 3u || !o->shadow[RTL_I_free] || !o->shadow[RTL_I_calloc] || !o->shadow[RTL_I_realloc]))
        return rtl_refuse("shadow_set", NULL);
    if (heap_own && (heap_replaced != 3u || !o->selected[RTL_I_free] || !o->selected[RTL_I_calloc] || !o->selected[RTL_I_realloc]))
        return rtl_refuse("heap_set", NULL);
    BOOL life = life_replaced != 0;
    if (life && life_replaced != RTL_LIFE_GROUP) return rtl_refuse("lifecycle_set", NULL);
    if ((adapters || heap || heap_own || life) && !rtl_private_stack_allocate()) return rtl_refuse("private_stack", NULL);
    if (compare && !rtl_overlay_allocate()) return rtl_refuse("overlay", NULL);
    if (heap && !rtl_heap_prepare(o->cw_image, o->resolve, o->resolve_context)) return FALSE;
    if (o->heap_trace[0] && !heap) return rtl_refuse("heap_trace_needs_shadow", NULL);
    if (o->heap_trace[0] && !rtl_heap_trace_open(o->heap_trace)) return FALSE;
    if (heap_own && !rtl_heap_own_prepare()) return FALSE;
    if (io && !rtl_io_prepare(o->cw_image, o->resolve, o->resolve_context, io_shadow)) return FALSE;
    if (life && !rtl_life_prepare(o->cw_image, o->resolve, o->resolve_context)) return FALSE;
    /* Publish everything a thunk reads before the first slot points to it. */
    memset(rtl_counters, 0, sizeof(rtl_counters));
    memset(rtl_skipped, 0, sizeof(rtl_skipped));
    memset(rtl_sim_bits, 0, sizeof(rtl_sim_bits));
    rtl_mismatch_detail[0] = 0;
    rtl_busy = 0;
    for (unsigned i = 0; i < RTL_SIM_SITE_COUNT; ++i)
        rtl_sim_bits[rtl_sim_sites[i].ret_rva / 32u] |= 1u << (rtl_sim_sites[i].ret_rva % 32u);
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) {
        rtl_own[i] = (signed char)(rtl_own_function(i) + 1);
        rtl_original[i] = slots[i];
        rtl_modes[i] = o->selected[i] ? o->modes[i] : ES2_HOOK_ORIGINAL;
        rtl_io_of[i] = (signed char)rtl_io_function_of(i);
        rtl_io_shadowed[i] = (BYTE)(io && o->selected[i] && o->shadow[i] && rtl_io_of[i] >= 0);
        rtl_io_replaced[i] = (BYTE)(io && o->selected[i] && rtl_io_of[i] >= 0 && o->modes[i] == ES2_HOOK_REPLACED);
        rtl_shadowed[i] = (BYTE)(heap && o->selected[i] && o->shadow[i] && rtl_io_of[i] < 0);
        rtl_heap_replaced[i] = (BYTE)(heap_own && o->selected[i] && rtl_heap_function_of(i) >= 0);
        rtl_life_replaced[i] = (BYTE)(life && o->selected[i] && rtl_life_index(i) >= 0 && i != RTL_STARTUP_INDEX);
        rtl_targets[i] = rtl_shadowed[i] ? rtl_heap_adapter(i) :
                         (rtl_io_shadowed[i] || rtl_io_replaced[i]) ? rtl_io_adapter_table[i] :
                         rtl_life_replaced[i] ? rtl_life_adapter(i) :
                         rtl_modes[i] == ES2_HOOK_REPLACED ? rtl_replaced_table[i] :
                         rtl_modes[i] == ES2_HOOK_COMPARE ? rtl_compare_table[i] : slots[i];
        rtl_installed[i] = 0;
    }
    rtl_foreign_calls = 0;
    rtl_stack_lo = (uint32_t)o->stack_lo; rtl_stack_hi = (uint32_t)o->stack_hi;
    rtl_hook_lo = (uint32_t)o->hook_lo; rtl_hook_hi = (uint32_t)o->hook_hi;
    rtl_image_lo = (uint32_t)(uintptr_t)o->image; rtl_image_size = (uint32_t)o->image_size;
    rtl_installed_image = o->image;
    rtl_heap_own_active = heap_own;
    rtl_life_active = life;
    MemoryBarrier();
    /* CW3220's recorder slots and entry jumps before any DBSIM slot
     * points to a shadow adapter. */
    if (heap && !rtl_heap_patch()) {
        char reason[sizeof(rtl_reason)]; memcpy(reason, rtl_reason, sizeof(reason));
        if (!rtl_uninstall()) snprintf(rtl_reason, sizeof(rtl_reason), "rollback_failed");
        else memcpy(rtl_reason, reason, sizeof(reason));
        return FALSE;
    }
    /* The recorder into CW3220's KERNEL32 slots before any DBSIM slot
     * points to a shadow adapter. */
    rtl_io_active = io;
    if (io_shadow && !rtl_io_patch()) {
        char reason[sizeof(rtl_reason)]; memcpy(reason, rtl_reason, sizeof(reason));
        if (!rtl_uninstall()) snprintf(rtl_reason, sizeof(rtl_reason), "rollback_failed");
        else memcpy(rtl_reason, reason, sizeof(reason));
        return FALSE;
    }
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) {
        if (!o->selected[i] || i == RTL_STARTUP_INDEX) continue;   /* The arming owns __startup's slot */
        DWORD *slot = (DWORD *)(o->image + RTL_IAT_RVA + 4 * i);
        if (!rtl_swap(slot, slots[i], rtl_thunk_table[i])) {
            rtl_refuse("patch", rtl_imports[i].key);
            char reason[sizeof(rtl_reason)]; memcpy(reason, rtl_reason, sizeof(reason));
            if (!rtl_uninstall()) snprintf(rtl_reason, sizeof(rtl_reason), "rollback_failed");
            else memcpy(rtl_reason, reason, sizeof(reason));
            return FALSE;
        }
        rtl_installed[i] = 1;
    }
    InterlockedExchange(&rtl_state, RTL_INSTALLED);
    return TRUE;
}

/* ------------------------------------------------------------------------
 * Configuration ([rtl] of the INI named by ES2_RTL_CONFIG):
 *   enabled=0|1          (absent or 0: nothing is installed)
 *   imports=all|<keys>   (comma list of shimmable keys, default all)
 *   <key>=original|compare|replaced|shadow  (missing = original; compare
 *                        and replaced only for the 20 imports with own code,
 *                        shadow only for calloc, realloc and free, all
 *                        three selected; replaced and shadow for
 *                        the file imports, replaced for printf, fputc and
 *                        fputs; replaced for calloc, realloc and free,
 *                        all three selected; replaced for startup,
 *                        atexit, exit, abort and ErrorExit, all five selected
 *                        (`all` includes them then); otherwise
 *                        mode_reserved_<key>, shadow_set, heap_set,
 *                        lifecycle_set or, for startup/atexit/exit outside
 *                        the group, not_shimmable_<key>)
 *   heap_trace=<path>    (the heap trace of rtl_heap_shadow.h, a new
 *                        file; only with the heap shadow, otherwise
 *                        heap_trace_needs_shadow)
 * Returns 1 enabled, 0 disabled, -1 refused (rtl_reason says why). */
static BOOL rtl_key_equal(const WCHAR *text, size_t length, const char *key) {
    if (strlen(key) != length) return FALSE;
    for (size_t i = 0; i < length; ++i) {
        WCHAR a = text[i], b = (WCHAR)(BYTE)key[i];
        if (a >= L'A' && a <= L'Z') a = (WCHAR)(a + 32);
        if (b >= L'A' && b <= L'Z') b = (WCHAR)(b + 32);
        if (a != b) return FALSE;
    }
    return TRUE;
}
static int rtl_configure(const WCHAR *ini, rtl_options *o) {
    WCHAR text[2048];
    DWORD n = GetPrivateProfileStringW(L"rtl", L"enabled", L"0", text, 8, ini);
    if (n >= 7) return rtl_refuse("config_enabled", NULL) - 1;
    if (!wcscmp(text, L"0")) return 0;
    if (wcscmp(text, L"1")) return rtl_refuse("config_enabled", NULL) - 1;
    memset(o->selected, 0, sizeof(o->selected));
    n = GetPrivateProfileStringW(L"rtl", L"imports", L"all", text, 2048, ini);
    if (!n || n >= 2047) return rtl_refuse("config_imports", NULL) - 1;
    BOOL all = !_wcsicmp(text, L"all");
    if (all) {
        /* startup, atexit and exit join below when the lifecycle group is requested. */
        for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) o->selected[i] = rtl_imports[i].shim;
    } else {
        WCHAR *p = text;
        for (;;) {
            WCHAR *end = wcschr(p, L',');
            if (end) *end = 0;
            while (*p == L' ' || *p == L'\t') ++p;
            size_t length = wcslen(p);
            while (length && (p[length - 1] == L' ' || p[length - 1] == L'\t')) --length;
            unsigned found = RTL_IMPORT_COUNT;
            for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) if (rtl_key_equal(p, length, rtl_imports[i].key)) found = i;
            if (found == RTL_IMPORT_COUNT || !length) return rtl_refuse("config_unknown_import", NULL) - 1;
            /* startup, atexit and exit are checked against their mode below. */
            if (!rtl_imports[found].shim && rtl_life_index(found) < 0) return rtl_refuse("not_shimmable", rtl_imports[found].key) - 1;
            if (o->selected[found]) return rtl_refuse("config_duplicate", rtl_imports[found].key) - 1;
            o->selected[found] = TRUE;
            if (!end) break;
            p = end + 1;
        }
    }
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) {
        WCHAR key[24], value[16];
        size_t length = strlen(rtl_imports[i].key);
        for (size_t c = 0; c <= length; ++c) key[c] = (WCHAR)(BYTE)rtl_imports[i].key[c];
        n = GetPrivateProfileStringW(L"rtl", key, L"", value, 16, ini);
        o->modes[i] = ES2_HOOK_ORIGINAL;
        o->shadow[i] = 0;
        if (!n || !_wcsicmp(value, L"original")) continue;
        int io_function = rtl_io_function_of(i);
        if (n < 15 && !_wcsicmp(value, L"shadow")) {
            /* the heap imports and the file imports. */
            if (rtl_heap_function_of(i) < 0 && (io_function < 0 || es2_io_is_stub((es2_io_function)io_function)))
                return rtl_refuse("mode_reserved", rtl_imports[i].key) - 1;
            o->shadow[i] = 1;
            continue;
        }
        if (n < 15 && io_function >= 0 && !_wcsicmp(value, L"replaced")) {
            /* core/src/rtl_io.c. */
            o->modes[i] = ES2_HOOK_REPLACED;
            continue;
        }
        if (n < 15 && rtl_heap_function_of(i) >= 0 && !_wcsicmp(value, L"replaced")) {
            /* The own heap (rtl_heap_own.h). */
            o->modes[i] = ES2_HOOK_REPLACED;
            continue;
        }
        if (n < 15 && rtl_life_index(i) >= 0 && !_wcsicmp(value, L"replaced")) {
            /* The lifecycle group (rtl_life.h). */
            o->modes[i] = ES2_HOOK_REPLACED;
            continue;
        }
        if (n < 15 && (!_wcsicmp(value, L"compare") || !_wcsicmp(value, L"replaced"))) {
            /* only the imports with own code (core/src/rtl.c). */
            if (rtl_own_function(i) < 0) return rtl_refuse("mode_reserved", rtl_imports[i].key) - 1;
            o->modes[i] = !_wcsicmp(value, L"compare") ? ES2_HOOK_COMPARE : ES2_HOOK_REPLACED;
            continue;
        }
        return rtl_refuse("mode_malformed", rtl_imports[i].key) - 1;
    }
    unsigned shadow = 0, chosen = 0, replaced = 0, replaced_chosen = 0;
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) {
        if (rtl_heap_function_of(i) < 0) continue;   /* the heap set */
        shadow += o->shadow[i]; chosen += o->shadow[i] && o->selected[i];
        replaced += o->modes[i] == ES2_HOOK_REPLACED; replaced_chosen += o->modes[i] == ES2_HOOK_REPLACED && o->selected[i];
    }
    if (shadow && (shadow != 3u || chosen != 3u)) return rtl_refuse("shadow_set", NULL) - 1;
    if (replaced && (replaced != 3u || replaced_chosen != 3u)) return rtl_refuse("heap_set", NULL) - 1;
    /* The lifecycle group, all five replaced and selected (`all`
     * selects __startup, _atexit and __exit then); those three exist only in
     * the group. */
    unsigned life = 0, life_chosen = 0;
    for (unsigned k = 0; k < RTL_LIFE_GROUP; ++k) life += o->modes[rtl_life_group[k].import] == ES2_HOOK_REPLACED;
    for (unsigned k = 0; life && all && k < RTL_LIFE_GROUP; ++k) o->selected[rtl_life_group[k].import] = TRUE;
    for (unsigned k = 0; k < RTL_LIFE_GROUP; ++k)
        life_chosen += o->modes[rtl_life_group[k].import] == ES2_HOOK_REPLACED && o->selected[rtl_life_group[k].import];
    if (life && (life != RTL_LIFE_GROUP || life_chosen != RTL_LIFE_GROUP)) return rtl_refuse("lifecycle_set", NULL) - 1;
    /* heap_trace=<path>, only with the heap shadow (rtl_heap_shadow.h). */
    n = GetPrivateProfileStringW(L"rtl", L"heap_trace", L"", o->heap_trace, MAX_PATH, ini);
    if (n >= MAX_PATH - 1) return rtl_refuse("config_heap_trace", NULL) - 1;
    if (n && !shadow) return rtl_refuse("heap_trace_needs_shadow", NULL) - 1;
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i)
        if (o->selected[i] && !rtl_imports[i].shim && o->modes[i] != ES2_HOOK_REPLACED) return rtl_refuse("not_shimmable", rtl_imports[i].key) - 1;
    return 1;
}

/* ------------------------------------------------------------------------
 * Tick fields. Each tick: rtl_<key>_calls per installed import and
 * rtl_foreign_calls; `full` (the exit_counters record) adds _mismatches,
 * _hook, _sim, _dbsim and _other, for an import not in mode original
 * _skipped, and after rtl_foreign_calls the first compare mismatch
 * (rtl_first_mismatch) if there was one. With the heap shadow:
 * _shadow_mismatches per shadowed import (full), rtl_heap_shadow_mismatches
 * after rtl_foreign_calls in every tick, and the heap counters at the end
 * (full, rtl_heap_fields). With the own heap: rtl_heap_foreign_frees
 * in every tick, the own heap's fields at the end (full). With the
 * lifecycle group: rtl_life_init_records and rtl_life_winmain in every tick,
 * all rtl_life_* fields at the end (full). A refused request reports rtl_refused
 * (`not_armed` while the __startup slot was never taken).
 * Returns 0 when nothing is configured or the fields do not fit. */
static int rtl_tick_fields(char *out, size_t capacity, BOOL full) {
    LONG state = rtl_state;
    if (state == RTL_REFUSED || state == RTL_PENDING) {
        /* Pending at a tick: DBSIM's entry never went through the slot. */
        int n = snprintf(out, capacity, ",\"rtl_refused\":\"%s\"", state == RTL_PENDING ? "not_armed" : rtl_reason);
        if (n <= 0 || (size_t)n >= capacity) return 0;
        int runtime = full ? rtl_runtime_fields(out + n, capacity - (size_t)n) : 0;
        return runtime < 0 ? 0 : n + runtime;
    }
    if (state != RTL_INSTALLED) return 0;
    int n = 0;
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) {
        if (!rtl_installed[i]) continue;
        const char *k = rtl_imports[i].key;
        const rtl_counter *c = &rtl_counters[i];
        int added = full ?
            snprintf(out + n, capacity - (size_t)n,
                     ",\"rtl_%s_calls\":%llu,\"rtl_%s_mismatches\":%llu,\"rtl_%s_hook\":%llu,\"rtl_%s_sim\":%llu,\"rtl_%s_dbsim\":%llu,\"rtl_%s_other\":%llu",
                     k, (unsigned long long)c->calls, k, (unsigned long long)c->mismatches, k, (unsigned long long)c->hook,
                     k, (unsigned long long)c->sim, k, (unsigned long long)c->dbsim, k, (unsigned long long)c->other) :
            snprintf(out + n, capacity - (size_t)n, ",\"rtl_%s_calls\":%llu", k, (unsigned long long)c->calls);
        if (added <= 0 || (size_t)added >= capacity - (size_t)n) return 0;
        n += added;
        if (full && rtl_modes[i] != ES2_HOOK_ORIGINAL) {
            added = snprintf(out + n, capacity - (size_t)n, ",\"rtl_%s_skipped\":%llu", k, (unsigned long long)rtl_skipped[i]);
            if (added <= 0 || (size_t)added >= capacity - (size_t)n) return 0;
            n += added;
        }
        if (full && rtl_shadowed[i]) {
            added = snprintf(out + n, capacity - (size_t)n, ",\"rtl_%s_shadow_mismatches\":%llu", k,
                             (unsigned long long)rtl_heap_import_mismatches[i]);
            if (added <= 0 || (size_t)added >= capacity - (size_t)n) return 0;
            n += added;
        }
        if (full && rtl_io_shadowed[i]) {
            added = snprintf(out + n, capacity - (size_t)n, ",\"rtl_%s_shadow_mismatches\":%llu,\"rtl_%s_shadow_skipped\":%llu", k,
                             (unsigned long long)rtl_io_mismatches[i], k, (unsigned long long)rtl_io_shadow_skipped[i]);
            if (added <= 0 || (size_t)added >= capacity - (size_t)n) return 0;
            n += added;
        }
    }
    int added = snprintf(out + n, capacity - (size_t)n, ",\"rtl_foreign_calls\":%llu", (unsigned long long)rtl_foreign_calls);
    if (added <= 0 || (size_t)added >= capacity - (size_t)n) return 0;
    n += added;
    if (rtl_heap_active) {
        /* Every tick carries the heap shadow's total mismatch count
         * (suites stopped by the runner have no exit record). */
        added = snprintf(out + n, capacity - (size_t)n, ",\"rtl_heap_shadow_mismatches\":%llu", (unsigned long long)rtl_heap_mismatch_total());
        if (added <= 0 || (size_t)added >= capacity - (size_t)n) return 0;
        n += added;
    }
    if (rtl_heap_own_active) {
        /* Every tick carries the frees/reallocs sent to CW3220. */
        added = snprintf(out + n, capacity - (size_t)n, ",\"rtl_heap_foreign_frees\":%llu", (unsigned long long)rtl_heap_foreign_frees);
        if (added <= 0 || (size_t)added >= capacity - (size_t)n) return 0;
        n += added;
    }
    if (rtl_io_active && rtl_io_any_shadow()) {
        /* Every tick carries the I/O shadow's total mismatch count. */
        added = snprintf(out + n, capacity - (size_t)n, ",\"rtl_io_shadow_mismatches\":%llu", (unsigned long long)rtl_io_mismatch_total());
        if (added <= 0 || (size_t)added >= capacity - (size_t)n) return 0;
        n += added;
    }
    if (rtl_life_active && !full) {
        /* Every tick carries the init records and WinMain entries of the own start-up. */
        added = rtl_life_fields(out + n, capacity - (size_t)n, FALSE);
        if (added <= 0) return 0;
        n += added;
    }
    if (full && rtl_mismatch_detail[0]) {
        /* key:what@address:args=..:own=eax,ecx,edx:orig=eax,ecx,edx (no quotes or backslashes) */
        added = snprintf(out + n, capacity - (size_t)n, ",\"rtl_first_mismatch\":\"%s\"", rtl_mismatch_detail);
        if (added <= 0 || (size_t)added >= capacity - (size_t)n) return 0;
        n += added;
    }
    if (full && rtl_heap_active) {
        /* The heap shadow's counters (rtl_heap_shadow.h). */
        added = rtl_heap_fields(out + n, capacity - (size_t)n);
        if (added <= 0) return 0;
        n += added;
    }
    if (full && rtl_heap_own_active) {
        /* The own heap's regions, blocks and OS calls (rtl_heap_own.h). */
        added = rtl_heap_own_fields(out + n, capacity - (size_t)n);
        if (added <= 0) return 0;
        n += added;
    }
    if (full && rtl_io_active && rtl_io_any_shadow()) {
        /* The recorded Win32 calls and the first I/O shadow mismatch. */
        added = rtl_io_fields(out + n, capacity - (size_t)n);
        if (added <= 0) return 0;
        n += added;
    }
    if (full && rtl_life_active) {
        /* The lifecycle's counters (rtl_life.h). */
        added = rtl_life_fields(out + n, capacity - (size_t)n, TRUE);
        if (added <= 0) return 0;
        n += added;
    }
    if (full) {
        /* The runtime (rtl_runtime_fields). */
        added = rtl_runtime_fields(out + n, capacity - (size_t)n);
        if (added < 0) return 0;
        n += added;
    }
    return n;
}
static BOOL rtl_reporting(void) { return rtl_state != RTL_OFF; }


/* ------------------------------------------------------------------------
 * Arming at DBSIM's entry point. DBSIM's entry 0x401000 ends with
 * `push 0; jmp 0x4967d8` (the __startup thunk), after every DllMain. While
 * armed, that slot points to rtl_startup_entry, which runs the arming step
 * on a private stack (TEB stack bounds swapped for its duration, x87/SSE
 * state, flags and all general registers saved there) and then jumps to the
 * real __startup with ESP and every register as the entry code left them
 * (to the own start-up of rtl_life.h when the lifecycle group
 * installed). Nothing is written to the game's stack. The private stack is
 * allocated only when the shim is requested (no static space in the proxy). */
__attribute__((used)) static BYTE rtl_fpu_state[512] __attribute__((aligned(16)));
__attribute__((used)) static uint32_t rtl_entry_esp, rtl_entry_stack_base, rtl_startup_original;
static DWORD *rtl_startup_slot;
static rtl_resolver rtl_startup_resolve;
static void *rtl_startup_context;
static WCHAR rtl_config_path[MAX_PATH];

__asm__(
    ".text\n.p2align 4\n"
    "_rtl_startup_entry:\n"
    "    movl %esp, _rtl_entry_esp\n"
    "    movl _rtl_private_top, %esp\n"
    "    pushfl\n"
    "    pushal\n"
    "    pushl %fs:4\n"
    "    pushl %fs:8\n"
    "    movl %fs:4, %eax\n"
    "    movl %eax, _rtl_entry_stack_base\n"
    "    movl _rtl_private_top, %eax\n"
    "    movl %eax, %fs:4\n"
    "    movl _rtl_private_base, %eax\n"
    "    movl %eax, %fs:8\n"
    "    fxsave _rtl_fpu_state\n"
    "    cld\n"
    "    call _rtl_arm_at_entry\n"
    "    fxrstor _rtl_fpu_state\n"
    "    popl %fs:8\n"
    "    popl %fs:4\n"
    "    popal\n"
    "    popfl\n"
    "    movl _rtl_entry_esp, %esp\n"
    "    jmp *_rtl_startup_target\n");
extern void rtl_startup_entry(void) __asm__("_rtl_startup_entry");

/* The arming step, run on the private stack: defined below for the proxy;
 * a fixture defines ES2_RTL_NO_PROCESS_ENTRY and its own rtl_arm. */
static void rtl_arm(void);

void __cdecl rtl_arm_at_entry(void) __asm__("_rtl_arm_at_entry") __attribute__((used));
void __cdecl rtl_arm_at_entry(void) {
    DWORD error = GetLastError();
    BOOL restored = rtl_startup_slot &&
        rtl_swap(rtl_startup_slot, (uint32_t)(uintptr_t)rtl_startup_entry, rtl_startup_original);
    if (restored) {
        rtl_startup_slot = NULL;
        rtl_arm();
    } else {
        rtl_refuse("startup_restore", NULL);
        InterlockedExchange(&rtl_state, RTL_REFUSED);
    }
    SetLastError(error);
}

/* DllMain-safe part (memory only): verifies the image header, the __startup
 * thunk and its slot (= the CW3220 export) and redirects that one slot. */
static BOOL rtl_install_startup_image(BYTE *image, uintptr_t expected_base, size_t size, rtl_resolver resolve, void *context) {
    if (rtl_startup_slot || rtl_state != RTL_OFF) return rtl_refuse("busy", NULL);
    if (size != RTL_IMAGE_SIZE || expected_base > UINT32_MAX - RTL_IMAGE_SIZE || !es2_hook_image_valid(image, expected_base, size))
        return rtl_refuse("image", NULL);
    const BYTE *thunk = image + RTL_THUNK_RVA + 6 * RTL_STARTUP_INDEX;
    DWORD *slot = (DWORD *)(image + RTL_IAT_RVA + 4 * RTL_STARTUP_INDEX);
    if (thunk[0] != 0xff || thunk[1] != 0x25 || rtl_u32(thunk + 2) != (uint32_t)((uintptr_t)slot))
        return rtl_refuse("thunk", "startup");
    FARPROC expected = resolve ? resolve("__startup", context) : NULL;
    uint32_t original = *slot;
    if (!expected || original != (uint32_t)(uintptr_t)expected) return rtl_refuse("slot", "startup");
    if (!rtl_private_stack_allocate()) return rtl_refuse("private_stack", NULL);
    rtl_startup_resolve = resolve; rtl_startup_context = context;
    rtl_startup_original = rtl_startup_target = original;
    MemoryBarrier();
    if (!rtl_swap(slot, original, (uint32_t)(uintptr_t)rtl_startup_entry)) return rtl_refuse("patch", "startup");
    rtl_startup_slot = slot;
    InterlockedExchange(&rtl_state, RTL_PENDING);
    return TRUE;
}

#ifndef ES2_RTL_NO_PROCESS_ENTRY
/* Production arming step (on the private stack, outside the loader lock). */
static void rtl_arm(void) {
    if (!config_path[0]) {
        DWORD n = GetModuleFileNameW(self_module, config_path, MAX_PATH);
        WCHAR *slash = n && n < MAX_PATH ? wcsrchr(config_path, L'\\') : NULL;
        if (!slash || slash - config_path + 1 + wcslen(L"es2-oracle.ini") >= MAX_PATH) config_path[0] = 0;
        else wcscpy(slash + 1, L"es2-oracle.ini");
    }
    rtl_options o;
    memset(&o, 0, sizeof(o));
    int configured = rtl_configure(rtl_config_path, &o);
    if (!configured) { InterlockedExchange(&rtl_state, RTL_OFF); log_event("rtl_imports_disabled"); return; }
    WCHAR exe[MAX_PATH];
    DWORD length = GetModuleFileNameW(NULL, exe, MAX_PATH);
    BOOL ok = configured > 0;
    if (ok && (!length || length >= MAX_PATH || !rtl_reference_file(exe))) ok = rtl_refuse("hash", NULL);
    if (ok) {
        o.image = (BYTE *)GetModuleHandleW(NULL);
        o.expected_base = 0x00400000u;
        o.image_size = RTL_IMAGE_SIZE;
        o.resolve = rtl_startup_resolve;
        o.resolve_context = rtl_startup_context;
        o.stack_lo = rtl_stack_floor((const void *)(uintptr_t)rtl_entry_esp);
        o.stack_hi = rtl_entry_stack_base;
        if (!rtl_module_range((const void *)rtl_install, &o.hook_lo, &o.hook_hi)) ok = rtl_refuse("proxy_range", NULL);
        /* The shadow patches the loaded CW3220.DLL (entry jumps, two
         * import slots): only the reference file. The I/O modes work on
         * its tables and import slots too, the lifecycle group on its
         * variables, code and import slots. Refused as own_runtime with the
         * own cw3220.dll. */
        o.cw_image = (BYTE *)rtl_startup_context;
        BOOL heap = FALSE;
        for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i)   /* the I/O modes and the lifecycle group too */
            heap = heap || o.shadow[i] || (o.modes[i] != ES2_HOOK_ORIGINAL && (rtl_io_function_of(i) >= 0 || rtl_life_index(i) >= 0));
        if (ok) ok = rtl_runtime_check((HMODULE)rtl_startup_context, heap);
    }
    if (ok) ok = rtl_install(&o);
    /* The arming continues in the own start-up instead of CW3220's. */
    if (ok && rtl_life_active) rtl_startup_target = (uint32_t)(uintptr_t)rtl_life_startup_entry;
    InterlockedExchange(&rtl_state, ok ? RTL_INSTALLED : RTL_REFUSED);
    log_event(ok ? "rtl_imports_installed" : "rtl_imports_refused");
}

/* DllMain: only with ES2_RTL_CONFIG set and only in DBSIM.EXE. A refusal is
 * reported later as the tick field rtl_refused (no file I/O here). */
static void install_rtl_startup(void) {
    DWORD n = GetEnvironmentVariableW(L"ES2_RTL_CONFIG", rtl_config_path, MAX_PATH);
    if (!n) return;
    WCHAR exe[MAX_PATH];
    DWORD length = GetModuleFileNameW(NULL, exe, MAX_PATH);
    WCHAR *name = length && length < MAX_PATH ? wcsrchr(exe, L'\\') : NULL;
    if (!name || _wcsicmp(name + 1, L"DBSIM.EXE")) return;
    BOOL ok = n < MAX_PATH ? TRUE : rtl_refuse("config_path", NULL);
    if (ok) ok = rtl_install_startup_image((BYTE *)GetModuleHandleW(NULL), 0x00400000u, RTL_IMAGE_SIZE,
                                           rtl_resolve_module, GetModuleHandleW(L"cw3220.DLL"));
    if (!ok) InterlockedExchange(&rtl_state, RTL_REFUSED);
}
#endif
#endif
