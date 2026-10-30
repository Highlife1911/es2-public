#ifndef ES2_HOOK_H
#define ES2_HOOK_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stddef.h>
#include <stdint.h>

/* Windows x86: legacy API is cdecl/EAX. The opt-in extension below adds
 * integer Borland/stdcall calls and pair/x87 results. No varargs or unwinding.
 * Install/uninstall and all hooked calls must belong to one simulation thread.
 * Descriptors, callback user data, log and runtime storage outlive installation.
 */
#define ES2_HOOK_MAX_ARGS 6u
#define ES2_HOOK_MAX_SPANS 4u
#define ES2_HOOK_MAX_PROLOGUE 32u
#define ES2_HOOK_MAX_SIGNATURE 64u
#define ES2_HOOK_MAX_STATE_BYTES 16384u
#define ES2_HOOK_MAX_POINTER_HOPS 3u
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

typedef enum es2_hook_mode {
    ES2_HOOK_ORIGINAL = 0,
    ES2_HOOK_REPLACED = 1,
    ES2_HOOK_COMPARE = 2
} es2_hook_mode;

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

typedef struct es2_hook_span {
    unsigned arg_index;
    size_t offset;
    size_t size;
} es2_hook_span;

typedef uint32_t (__cdecl *es2_hook_replacement)(const uint32_t args[ES2_HOOK_MAX_ARGS], void *user);

typedef struct es2_hook_comparison {
    uint32_t original_result;
    uint32_t replacement_result;
    unsigned span_count;
    const BYTE *original[ES2_HOOK_MAX_SPANS];
    const BYTE *replacement[ES2_HOOK_MAX_SPANS];
    size_t sizes[ES2_HOOK_MAX_SPANS];
    uint32_t original_edx, replacement_edx;
} es2_hook_comparison;

/* Optional comparator returns TRUE for equality; NULL compares EAX and bytes.
 * The callback must not mutate either view or perform game-visible work.
 */
typedef BOOL (__cdecl *es2_hook_compare)(const es2_hook_comparison *values, void *user);
typedef BOOL (__cdecl *es2_hook_frame)(void *user, uint32_t *frame);

typedef struct es2_hook_descriptor {
    const char *name;                 /* 1..63 ASCII letters/digits/underscores */
    uint32_t rva;
    const BYTE *expected;
    size_t expected_size;             /* exact signature, >= relocated size */
    size_t relocate_size;             /* 5..32 bytes, complete instructions */
    const BYTE *instruction_lengths;  /* one decoded length per instruction */
    size_t instruction_count;
    unsigned arg_count;
    es2_hook_replacement replacement;
    es2_hook_compare compare;
    void *user;
    const es2_hook_span *spans;
    unsigned span_count;
} es2_hook_descriptor;

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

/* Opt-in contracts; existing descriptors/options/install calls stay unchanged.
 * BORLAND uses EAX,EDX,ECX and remaining left-to-right stack slots, callee cleanup.
 * STDCall uses only stack slots, callee cleanup. No hidden/aggregate arguments. */
typedef enum es2_hook_abi { ES2_ABI_CDECL, ES2_ABI_STDCALL, ES2_ABI_BORLAND } es2_hook_abi;
typedef enum es2_hook_result_kind { ES2_RESULT_EAX, ES2_RESULT_PAIR, ES2_RESULT_ST0 } es2_hook_result_kind;
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

typedef enum es2_hook_region_source {
    ES2_REGION_RVA, ES2_REGION_ARG_INDIRECT, ES2_REGION_RVA_INDIRECT
} es2_hook_region_source;
typedef struct es2_hook_region {
    es2_hook_region_source source;
    unsigned arg_index;
    uint32_t rva;
    size_t pointer_offset;
    size_t offset;
    size_t size;
    BOOL read_only;
    /* Zero retains the legacy one-hop indirect contract. For two/three hops,
     * dereference pointer_offset first, then these offsets. Multi-hop regions
     * must be read-only; every traversed slot is checked for stability. */
    unsigned pointer_hops;
    size_t next_pointer_offsets[ES2_HOOK_MAX_POINTER_HOPS - 1];
} es2_hook_region;
/* Snapshot regions: large state (an object pool's block, a fixed range
 * of the image) that compare mode isolates IN PLACE, with copies outside
 * the stack: the pre-state S0 is copied, the original runs on real state,
 * the regions' post-state S1 is kept and S0 restored, the replacement runs
 * on real addresses (its native reads and its game calls through a guest
 * bridge see S0), its post-state is compared with S1 chunk by chunk and S1
 * restored. A fixed region is image + rva, size bytes. With resolve, the
 * callback computes base and size per call (size 0: absent this call; it
 * must not change state); size is then the largest accepted size. Regions
 * must not overlap each other, spans or extension regions. guard_rvas name
 * hooked functions whose entry during the original phase makes the call
 * not comparable (effects outside the regions: messages, clocks, sound):
 * the replacement phase is skipped. Every guard site must hold an installed
 * hook of this framework at compare time, otherwise the call is skipped.
 * levels buffer sets allow that many simultaneous compares of the hook
 * (re-entry through its replacement phase); every samples one call in N.
 * Skips of all kinds also count in es2_hook.skipped. */
typedef BOOL (__cdecl *es2_hook_snapshot_resolve)(void *user, BYTE *image, const uint32_t *args,
                                                  BYTE **base, size_t *size);
typedef struct es2_hook_snapshot {
    uint32_t rva;
    size_t size;
    es2_hook_snapshot_resolve resolve;
    void *user;
} es2_hook_snapshot;
typedef struct es2_hook_snapshot_set {
    const es2_hook_snapshot *regions;
    unsigned region_count;
    const uint32_t *guard_rvas;
    unsigned guard_count;
    unsigned levels;                  /* 0 means 1 */
    unsigned every;                   /* 0 means 1 */
} es2_hook_snapshot_set;
typedef uint64_t (__cdecl *es2_hook_replacement64)(const uint32_t *, void *);
/* Exact scratch-register contracts may depend on entry EAX/EDX/ECX. */
typedef uint64_t (__cdecl *es2_hook_replacement_context)(const uint32_t *, void *, const uint32_t registers[3]);
typedef long double (__cdecl *es2_hook_replacement80)(const uint32_t *, void *);
typedef struct es2_hook_extension {
    es2_hook_abi abi;
    es2_hook_result_kind result_kind;
    es2_hook_replacement64 replacement64;
    es2_hook_replacement80 replacement80;
    const es2_hook_region *regions;
    unsigned region_count;
    es2_hook_replacement_context replacement_context;
    /* TRUE when the function changes state outside its spans/regions (for
     * example by calling unreplaced game code), so running it twice in
     * compare mode would corrupt the game; installing in compare is refused. */
    BOOL compare_unsupported;
} es2_hook_extension;
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
