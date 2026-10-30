#ifndef ES2_HOOK_H
#define ES2_HOOK_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include "es2/hook/descriptor.h"

/* Windows x86: legacy API is cdecl/EAX. The opt-in extension below adds
 * integer Borland/stdcall calls and pair/x87 results. No varargs or unwinding.
 * Install/uninstall and all hooked calls must belong to one simulation thread.
 * Descriptors, callback user data, log and runtime storage outlive installation.
 */
#define ES2_HOOK_MAX_PROLOGUE 32u
#define ES2_HOOK_MAX_SIGNATURE 64u
#define ES2_HOOK_MAX_STATE_BYTES 16384u
/* Replaced calls nest up to ES2_HOOK_MAX_DEPTH levels (a few hundred bytes
 * of host stack each). A compare level starts only below
 * ES2_HOOK_MAX_COMPARE_DEPTH; deeper calls run the original (skipped). The
 * legacy compare dispatcher keeps its 16 KiB span buffer on the stack; the
 * extended one uses one static 64 KiB buffer set per compare level. */
#define ES2_HOOK_MAX_DEPTH 32u
#define ES2_HOOK_MAX_COMPARE_DEPTH 8u
#define ES2_HOOK_MAX_NAME 63u
/* Snapshot regions (extended compare only): see es2_hook_snapshot_set. */
#define ES2_HOOK_MAX_SNAPSHOTS 8u
#define ES2_HOOK_MAX_SNAPSHOT_BYTES 0x01000000u /* all regions of one hook */
#define ES2_HOOK_MAX_SNAPSHOT_LEVELS 4u
#define ES2_HOOK_MAX_GUARDS 8u
#define ES2_HOOK_SNAPSHOT_CHUNK 256u

typedef enum es2_hook_error {
    ES2_HOOK_ERROR_NONE = 0,
    ES2_HOOK_ERROR_ARGUMENT,
    ES2_HOOK_ERROR_IMAGE,
    ES2_HOOK_ERROR_SIGNATURE,
    ES2_HOOK_ERROR_INSTRUCTION,
    ES2_HOOK_ERROR_MEMORY,
    ES2_HOOK_ERROR_PROTECT,
    ES2_HOOK_ERROR_FLUSH,
    ES2_HOOK_ERROR_ROLLBACK,
    ES2_HOOK_ERROR_BUSY,
    ES2_HOOK_ERROR_THREAD,
    ES2_HOOK_ERROR_TABLE_FULL
} es2_hook_error;

typedef struct es2_hook_log {
    HANDLE file;
    uint64_t attempted;
    uint64_t written;
    uint64_t failed_writes;
    uint32_t limit;
    es2_hook_frame frame;
    void *user;
} es2_hook_log;

/* Injected functions affect entry-site patching only, not code allocation.
 * NULL members select Win32 APIs. Test callbacks must preserve normal Win32
 * semantics (a FALSE return must not have changed the memory protection).
 */
typedef struct es2_hook_patch_api {
    BOOL (WINAPI *protect)(LPVOID, SIZE_T, DWORD, PDWORD);
    BOOL (WINAPI *flush)(HANDLE, LPCVOID, SIZE_T);
} es2_hook_patch_api;

typedef struct es2_hook_options {
    BYTE *image;
    uintptr_t expected_base;
    size_t expected_image_size;
    es2_hook_mode mode;
    es2_hook_log *log;                /* optional, borrowed */
    es2_hook_patch_api patch_api;
} es2_hook_options;

typedef struct es2_hook_result {
    uint32_t eax, edx;
    BYTE fpu[108]; /* x86 FNSAVE: 28-byte environment + eight exact 80-bit slots */
} es2_hook_result;
typedef struct es2_hook_call {
    void *target;
    es2_hook_abi abi;
    unsigned arg_count;
    const uint32_t *args;
    const BYTE *fpu_before;
    es2_hook_result *result;
    const uint32_t *entry_registers; /* optional EAX/EDX/ECX for stack ABIs */
} es2_hook_call;

/* Binds a snapshot set to an extension (NULL removes the binding).
 * It is not a member of es2_hook_extension, whose adapters use positional
 * initializers. Bind before installing (an adapter does it from a load-time
 * constructor); install copies the binding into the runtime. FALSE for an
 * invalid argument or a full table of ES2_HOOK_MAX_SNAPSHOT_BINDINGS. */
#define ES2_HOOK_MAX_SNAPSHOT_BINDINGS 32u
BOOL es2_hook_bind_snapshot(const es2_hook_extension *extension, const es2_hook_snapshot_set *set);

typedef struct es2_hook {
    const es2_hook_descriptor *descriptor;
    es2_hook_options options;
    BYTE *site;
    BYTE *code;
    void *trampoline;
    BYTE original[ES2_HOOK_MAX_PROLOGUE];
    BYTE patch[ES2_HOOK_MAX_PROLOGUE];
    DWORD owner_thread;
    DWORD original_protection;
    volatile LONG installed;
    unsigned in_flight;
    es2_hook_error error;
    uint64_t calls;
    uint64_t mismatches;
    uint64_t skipped;
    uint64_t bypassed;
    const es2_hook_extension *extension;
    /* Snapshot compare: level buffers (allocated at a compare install
     * with a snapshot set, freed by uninstall), levels in use, the sampling
     * counter and the reasons of skipped snapshot compares. */
    const es2_hook_snapshot_set *snapshot;
    BYTE *snapshot_memory;
    size_t snapshot_level_bytes;
    unsigned snapshot_active;
    uint64_t snapshot_seen;
    uint64_t snapshot_compared, snapshot_unsampled, snapshot_exhausted,
             snapshot_guarded, snapshot_refused;
} es2_hook;
/* Snapshot guards find their runtime by site in a framework table of
 * patched hooks (entered by a successful install, removed by a successful
 * uninstall). An install that finds the table full patches nothing and
 * fails with ES2_HOOK_ERROR_TABLE_FULL. */
#define ES2_HOOK_MAX_INSTALLED 1024u

/* Zero-initialize runtime before first use. ORIGINAL succeeds without touching
 * the image or allocating code. On ordinary failure the entry bytes are
 * restored and code is freed. ERROR_ROLLBACK retains code/context: keep this
 * runtime alive and retry uninstall; never unload its containing module.
 */
BOOL es2_hook_install(es2_hook *hook, const es2_hook_descriptor *descriptor, const es2_hook_options *options);
BOOL es2_hook_install_ex(es2_hook *hook, const es2_hook_descriptor *descriptor,
                         const es2_hook_options *options, const es2_hook_extension *extension);
/* Low-level own ABI harness. Caller checks target/args, count <= MAX_ARGS and
 * supplies a valid 108-byte FNSAVE state. Captures EDX:EAX and complete x87
 * state, restores that captured state on return. Does not install a hook. */
void __cdecl es2_hook_machine_call(const es2_hook_call *call);
void __cdecl es2_hook_fpu_save(BYTE state[108]);
BOOL es2_hook_uninstall(es2_hook *hook);
/* Set before installing hooks, at a quiescent point. Default TRUE. Only
 * extended REPLACED entries use this path; descriptors remain unchanged.
 * Observer is optional integer-only instrumentation and must preserve x87
 * and LastError. Its returned token pairs nested begin/end observations. */
typedef unsigned (__cdecl *es2_hook_fast_begin)(es2_hook *hook);
typedef void (__cdecl *es2_hook_fast_end)(unsigned token);
void es2_hook_set_fast_path(BOOL enabled);
void es2_hook_set_fast_observer(es2_hook_fast_begin begin, es2_hook_fast_end end);
/* Instrumentation-only accessor for the preserving assembly entry frame. */
es2_hook *es2_hook_frame_owner(const void *frame);
BOOL es2_hook_configure_fast_path(const WCHAR *ini_path);
BOOL es2_hook_image_valid(const BYTE *image, uintptr_t expected_base, size_t expected_size);
BOOL es2_hook_prologue_valid(const BYTE *bytes, size_t size, const BYTE *lengths, size_t count);

/* Missing key means ORIGINAL; malformed/truncated values return FALSE.
 * Log creation is CREATE_NEW. limit=0 selects the default cap of 32 records.
 * Zero-initialize the log before first use; reopening an open log is refused.
 * [hooks] mismatch_log/mismatch_limit are read by the proxy integration.
 */
BOOL es2_hook_read_mode(const WCHAR *ini_path, const char *name, es2_hook_mode *mode);
BOOL es2_hook_log_open(es2_hook_log *log, const WCHAR *path, uint32_t limit, es2_hook_frame frame, void *user);
void es2_hook_log_close(es2_hook_log *log);

#endif
