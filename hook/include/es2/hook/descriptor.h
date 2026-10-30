/* The hook descriptors (docs/web_build.md section 4.2): what an adapter
 * declares about one function of the original, shared by both backends. The
 * x86 framework (hook/hook.h, //hook) installs them by patching the image's
 * entries; the sim dispatch (//sim:dispatch) runs them as dispatch entries.
 * Portable C17: no Windows header; the x87 result kind of the x86 framework
 * is in descriptor_x86.h. */
#ifndef ES2_HOOK_DESCRIPTOR_H
#define ES2_HOOK_DESCRIPTOR_H
#include <stddef.h>
#include <stdint.h>

#if defined(__i386__)
#define ES2_HOOK_CDECL __attribute__((cdecl))
#else
#define ES2_HOOK_CDECL
#endif

#define ES2_HOOK_MAX_ARGS 6u
#define ES2_HOOK_MAX_SPANS 4u
#define ES2_HOOK_MAX_POINTER_HOPS 3u

typedef enum es2_hook_mode {
    ES2_HOOK_ORIGINAL = 0,
    ES2_HOOK_REPLACED = 1,
    ES2_HOOK_COMPARE = 2
} es2_hook_mode;

/* The x86 framework's runtime of one installed hook (hook/hook.h). */
typedef struct es2_hook es2_hook;

typedef struct es2_hook_span {
    unsigned arg_index;
    size_t offset;
    size_t size;
} es2_hook_span;

typedef uint32_t (ES2_HOOK_CDECL *es2_hook_replacement)(const uint32_t args[ES2_HOOK_MAX_ARGS], void *user);

typedef struct es2_hook_comparison {
    uint32_t original_result;
    uint32_t replacement_result;
    unsigned span_count;
    const uint8_t *original[ES2_HOOK_MAX_SPANS];
    const uint8_t *replacement[ES2_HOOK_MAX_SPANS];
    size_t sizes[ES2_HOOK_MAX_SPANS];
    uint32_t original_edx, replacement_edx;
} es2_hook_comparison;

/* Optional comparator returns nonzero for equality; NULL compares EAX and bytes.
 * The callback must not mutate either view or perform game-visible work.
 */
typedef int (ES2_HOOK_CDECL *es2_hook_compare)(const es2_hook_comparison *values, void *user);
typedef int (ES2_HOOK_CDECL *es2_hook_frame)(void *user, uint32_t *frame);

typedef struct es2_hook_descriptor {
    const char *name;                 /* 1..63 ASCII letters/digits/underscores */
    uint32_t rva;
    const uint8_t *expected;
    size_t expected_size;             /* exact signature, >= relocated size */
    size_t relocate_size;             /* 5..32 bytes, complete instructions */
    const uint8_t *instruction_lengths; /* one decoded length per instruction */
    size_t instruction_count;
    unsigned arg_count;
    es2_hook_replacement replacement;
    es2_hook_compare compare;
    void *user;
    const es2_hook_span *spans;
    unsigned span_count;
} es2_hook_descriptor;

/* Opt-in contracts; existing descriptors/options/install calls stay unchanged.
 * BORLAND uses EAX,EDX,ECX and remaining left-to-right stack slots, callee cleanup.
 * STDCall uses only stack slots, callee cleanup. No hidden/aggregate arguments.
 * The x86 framework adds the x87 kind (descriptor_x86.h). */
typedef enum es2_hook_abi { ES2_ABI_CDECL, ES2_ABI_STDCALL, ES2_ABI_BORLAND } es2_hook_abi;
typedef enum es2_hook_result_kind { ES2_RESULT_EAX, ES2_RESULT_PAIR } es2_hook_result_kind;

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
    int read_only;
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
typedef int (ES2_HOOK_CDECL *es2_hook_snapshot_resolve)(void *user, uint8_t *image, const uint32_t *args,
                                                        uint8_t **base, size_t *size);
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
typedef uint64_t (ES2_HOOK_CDECL *es2_hook_replacement64)(const uint32_t *, void *);
/* Exact scratch-register contracts may depend on entry EAX/EDX/ECX. */
typedef uint64_t (ES2_HOOK_CDECL *es2_hook_replacement_context)(const uint32_t *, void *, const uint32_t registers[3]);
#if defined(__i386__)
#include "es2/hook/descriptor_x86.h"
#else
/* No x87 result outside the x86 framework: the member keeps the layout. */
typedef void (*es2_hook_replacement80)(void);
#endif
typedef struct es2_hook_extension {
    es2_hook_abi abi;
    es2_hook_result_kind result_kind;
    es2_hook_replacement64 replacement64;
    es2_hook_replacement80 replacement80;
    const es2_hook_region *regions;
    unsigned region_count;
    es2_hook_replacement_context replacement_context;
    /* Nonzero when the function changes state outside its spans/regions (for
     * example by calling unreplaced game code), so running it twice in
     * compare mode would corrupt the game; installing in compare is refused. */
    int compare_unsupported;
} es2_hook_extension;

#endif
