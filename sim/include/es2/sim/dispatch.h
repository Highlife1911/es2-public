/* The dispatch of the own process (own_process.md section 4): every call to
 * an original address (a VA in CODE, an import slot) is resolved here, never
 * by running original code. An entry is
 *   hook      the registry's adapter (its replacement, with the extension's
 *             ABI and result kind; the adapters' direct path),
 *   service   host code with the original's calling contract,
 *   entry     a C function over the machine (es2/hook/machine.h): the
 *             registers, the stack at m->esp,
 * and every other VA is refused: the refusal callback names it and the run
 * stops (es2sim.exe exits with ES2_SIM_EXIT_REFUSED).
 *
 * Calls arrive two ways: own C code calls es2_sim_call; es2_machine_call
 * runs on the sim backend (es2/sim/machine_sim.h). A call site that still
 * jumps to a VA in non-executable CODE faults and the trap (es2/sim/trap.h)
 * refuses it as an unconverted call.
 * Arguments follow //hook's conventions: CDECL and STDCALL take the stack
 * slots in order; BORLAND takes EAX, EDX, ECX and then the stack slots as
 * es2_hook_dispatch_fast orders them; callee cleanup as the hook bridge. */
#ifndef ES2_SIM_DISPATCH_H
#define ES2_SIM_DISPATCH_H
#include <stdint.h>
#include "es2/hook/descriptor.h"
#include "es2/hook/machine.h"

/* Exit code of a run stopped by a refusal (distinct from every exit code the
 * original process can return: 0, 1, 3, 4, 6; launcher.md, "DBSIM's process
 * shell, start-up and exit"). */
#define ES2_SIM_EXIT_REFUSED 70

typedef enum es2_sim_kind { ES2_SIM_REFUSED = 0, ES2_SIM_HOOK, ES2_SIM_SERVICE, ES2_SIM_ENTRY } es2_sim_kind;

typedef struct es2_sim_service_call {
    uint32_t va, return_address;
    uint32_t stack;            /* guest address of the first argument slot (the caller's ESP + 4 at the call) */
    const uint32_t *slots;     /* host view of the same slots */
    uint32_t eax, edx, ecx;    /* in: the entry registers; out: the registers the original leaves */
    uint32_t ebp;              /* the caller's EBP (machine calls only; 0 from es2_sim_call) */
    int refused;             /* a service that cannot serve this call sets it, with `reason` */
    const char *reason;
} es2_sim_service_call;
typedef void (*es2_sim_service_fn)(es2_sim_service_call *call, void *user);
/* An entry: m->esp points at the return address, the argument slots follow.
 * It leaves the registers it returns in *m; ESP is the caller's again
 * afterwards whatever it sets. */
typedef void (*es2_sim_entry_fn)(es2_machine *m, void *user);

typedef struct es2_sim_entry {
    uint32_t va;
    es2_sim_kind kind;
    const char *name;
    const es2_hook_descriptor *descriptor;   /* hook */
    const es2_hook_extension *extension;     /* hook; NULL: legacy cdecl replacement with an EAX result */
    es2_sim_service_fn service;              /* service */
    void *service_user;                      /* service, entry */
    uint16_t pop_bytes;                      /* service, entry: stack bytes the callee removes */
    uint8_t stack_slots;                     /* service, entry: argument slots it reads at least */
    uint64_t calls;                          /* diagnostic, like the proxy's call_<va> counters */
    es2_machine_shim shim;                   /* hook: its entry shim as a C entry (es2/hook/machine.h), or NULL */
    uint64_t traps;                          /* diagnostic: the calls of `calls` that came through the trap */
    es2_sim_entry_fn entry;                  /* entry */
} es2_sim_entry;

typedef struct es2_sim_refusal {
    uint32_t va;
    const char *name;          /* the entry's name, the import name, or NULL */
    const char *reason;        /* "unmapped", "import slot", a service's reason, ... */
    uint32_t return_address;   /* the caller's return address (0 for a C call) */
    uint32_t frame;            /* the caller's ESP at the call (the slot holding the return address) */
} es2_sim_refusal;
/* Called once per refused call. es2sim.exe logs and exits; a test may
 * return (es2_sim_call then returns 1); after an unconverted call (the trap,
 * es2/sim/trap.h) the process ends when it returns. */
typedef void (*es2_sim_refuse_fn)(const es2_sim_refusal *refusal, void *user);
/* Called once per model error: the model cannot do what the original does
 * (a push or a shim's gap below the guest stack's limit). es2sim.exe logs and
 * exits with 71 (ES2_SIM_EXIT_FAULT); a test may return (the call then
 * returns 1). Set it after es2_sim_dispatch_init; NULL only counts. */
typedef es2_sim_refuse_fn es2_sim_model_error_fn;

typedef struct es2_sim_dispatch {
    es2_sim_entry *entries;
    unsigned count, capacity;
    int sealed;
    es2_sim_refuse_fn refuse;
    void *refuse_user;
    /* Names for refusals of import slots (not entries): slot VA and name. */
    const uint32_t *import_slots;
    const char *const *import_names;
    unsigned import_count;
    uint64_t refusals;
    es2_sim_model_error_fn model_error;
    void *model_error_user;
    uint64_t model_errors;
} es2_sim_dispatch;

void es2_sim_dispatch_init(es2_sim_dispatch *d, es2_sim_entry *storage, unsigned capacity,
                           es2_sim_refuse_fn refuse, void *refuse_user);
/* 0 on success; 1 when the table is full or sealed. */
int es2_sim_dispatch_add_hook(es2_sim_dispatch *d, uint32_t va, const char *name,
                              const es2_hook_descriptor *descriptor, const es2_hook_extension *extension);
int es2_sim_dispatch_add_service(es2_sim_dispatch *d, uint32_t va, const char *name, es2_sim_service_fn service,
                                 void *user, uint16_t pop_bytes, uint8_t stack_slots);
int es2_sim_dispatch_add_entry(es2_sim_dispatch *d, uint32_t va, const char *name, es2_sim_entry_fn entry,
                               void *user, uint16_t pop_bytes, uint8_t stack_slots);
/* Sorts by VA and checks: no VA twice (a service may not shadow a hook), at
 * most ES2_HOOK_MAX_ARGS arguments, a valid ABI/result combination per
 * extension (the checks of es2_hook_install_ex). 0 or 1 and *error naming
 * the first bad entry in `bad`. */
int es2_sim_dispatch_seal(es2_sim_dispatch *d, const char **error, uint32_t *bad);
const es2_sim_entry *es2_sim_dispatch_find(const es2_sim_dispatch *d, uint32_t va);
/* Name for a refusal message: an entry's or an import slot's name, else NULL. */
const char *es2_sim_dispatch_name(const es2_sim_dispatch *d, uint32_t va);

/* The arguments of a hook as its replacement receives them, from the stack
 * slots and the entry registers {EAX, EDX, ECX}; the stack bytes the callee
 * removes. */
void es2_sim_hook_args(const es2_hook_descriptor *descriptor, const es2_hook_extension *extension,
                       const uint32_t *slots, const uint32_t regs[3], uint32_t args[ES2_HOOK_MAX_ARGS]);
unsigned es2_sim_hook_pop_bytes(const es2_hook_descriptor *descriptor, const es2_hook_extension *extension);

typedef struct es2_sim_result {
    uint32_t eax, edx, ecx;
    unsigned pop_bytes;
} es2_sim_result;
/* A call from own C code: `slots` are the stack argument slots as the caller
 * pushes them (guest-addressable: in es2sim.exe a host array is a guest
 * address), `count` of them readable; `regs` = {EAX, EDX, ECX} at the call.
 * 0 and *out, or 1 when refused (the refusal callback ran). A wrapper over
 * the sim backend (es2_machine_sim_enter) with ESP at slots - 4; that slot
 * is neither read nor written, and the entry sees EBX, EBP, ESI, EDI as 0
 * and the return address 0. */
int es2_sim_call(es2_sim_dispatch *d, uint32_t va, const uint32_t *slots, unsigned count, const uint32_t regs[3],
                 es2_sim_result *out);

#endif
