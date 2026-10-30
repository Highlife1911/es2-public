/* The own process's heap (own_process.md section 3): the model of the CW3220
 * allocator (core/src/rtl_heap.c) as the allocator itself, in the replacing
 * mode: own reservations with the original's VirtualAlloc/VirtualFree calls,
 * user data zero-filled and moved in guest memory, CW3220's block layout (the
 * header word s|1 at p-4, 4-byte alignment, NULL for size 0). The address
 * sequence does not matter (36/36 suites); for repeatable runs the reservations
 * are asked for at a preferred base first (logged by the host), the system's
 * choice is the fallback. Registers the original leaves are reproduced as
 * rtl_imports.md, "the heap, replaced" documents them.
 *
 * The reservations at the preferred base never adjoin (a gap of
 * ES2_SIM_HEAP_RESERVATION_GAP each). The original's second reservation does
 * not follow its first (c1_01-free-camera: 0x02630000 + 4 MiB, then
 * 0x04440000), so __heapadd opens a second region and the growing block
 * starts at its base + 8; an adjoining one would extend the first region and
 * join the growth with the free block below its end. The gap is a multiple
 * of 64K, so the low 16 bits stay the offset in the reservation. */
#ifndef ES2_SIM_HEAP_H
#define ES2_SIM_HEAP_H
#include <stdint.h>
#include "es2/core/guest.h"
#include "es2/core/rtl.h"
#include "es2/core/rtl_heap.h"

#define ES2_SIM_HEAP_RESERVATION_GAP 0x00010000u

/* The OS calls of the heap (VirtualAlloc/VirtualFree with the original's
 * arguments; the raw results). es2_sim_heap_os_system gives the real ones
 * (es2/platform/os.h). */
typedef struct es2_sim_heap_os {
    void *context;
    uint32_t (*virtual_alloc)(void *context, uint32_t address, uint32_t size, uint32_t type, uint32_t protect);
    uint32_t (*virtual_free)(void *context, uint32_t address, uint32_t size, uint32_t type);
} es2_sim_heap_os;
void es2_sim_heap_os_system(es2_sim_heap_os *os);
/* CW3220's platform word as its start-up computes it (2: kernel32 exports
 * "Borland32"; 1: GetVersion bit 31; else 0). */
uint32_t es2_sim_heap_platform(void);

/* The heap trace's hook (own_process.md, "the heap trace"): an observer of every operation,
 * called after it with the function, its arguments, the result (0 for
 * free), the caller's return address (0: not known) and whether CW3220 asked
 * for its own block (the file service's stream buffers and find nodes)
 * rather than DBSIM through an import. */
typedef enum es2_sim_heap_function {
    ES2_SIM_HEAP_MALLOC, ES2_SIM_HEAP_CALLOC, ES2_SIM_HEAP_FREE, ES2_SIM_HEAP_REALLOC
} es2_sim_heap_function;
typedef void (*es2_sim_heap_observer)(void *user, es2_sim_heap_function function, int internal, uint32_t caller,
                                      uint32_t a0, uint32_t a1, uint32_t result);

typedef struct es2_sim_heap {
    es2_heap heap;
    es2_heap_env env;
    const es2_guest_memory *memory;
    es2_sim_heap_os os;
    uint32_t preferred_next, preferred_end;   /* reservation cursor at the preferred base; 0: none */
    uint64_t os_calls, os_failed, preferred_misses;
    es2_sim_heap_observer observe;           /* NULL: none */
    void *observe_user;
    uint32_t caller;                         /* the import call's return address, for the observer (the services set it) */
} es2_sim_heap;

/* `sentinel`: the guest address of the list head block's link word (16
 * writable bytes from sentinel - 8). */
void es2_sim_heap_init(es2_sim_heap *h, const es2_guest_memory *memory, const es2_sim_heap_os *os,
                       uint32_t sentinel, uint32_t platform, uint32_t preferred_base, uint32_t preferred_size);
/* Is p inside one of the heap's regions? */
int es2_sim_heap_owns(const es2_sim_heap *h, uint32_t p);

/* CW3220's calloc/free/realloc with their registers (regs in: the entry
 * registers; out: what the original leaves). 0, or 1 for a pointer the heap
 * does not own (free/realloc; the call is refused). */
uint32_t es2_sim_heap_calloc(es2_sim_heap *h, uint32_t count, uint32_t n, es2_rtl_regs *regs);
int es2_sim_heap_free(es2_sim_heap *h, uint32_t p, es2_rtl_regs *regs);
int es2_sim_heap_realloc(es2_sim_heap *h, uint32_t p, uint32_t n, es2_rtl_regs *regs);

/* CW3220's own _malloc(n) and _free(p) (0x41729c, 0x417160) as its runtime
 * calls them on the same heap (the stream buffers of setvbuf 0x405384 and
 * fclose 0x40555c, the find nodes): no registers, a new block not
 * zero-filled. runtime_free: 1 for a pointer the heap does not own (nothing
 * is freed). */
uint32_t es2_sim_heap_runtime_malloc(es2_sim_heap *h, uint32_t n);
int es2_sim_heap_runtime_free(es2_sim_heap *h, uint32_t p);

/* CW3220's own blocks from its DLL attach, before DBSIM's entry, as its
 * init records (table 0x423084, by priority) allocate them on this heap
 * (own_process.md, "the heap trace"; the original's heap walk at DBSIM's
 * entry has exactly these blocks):
 * - priority 3, 0x41a050, es2_sim_heap_copy_environment: _malloc((count +
 *   1) * 4) for the pointer array, then _malloc(strlen + 1) for each string
 *   of the environment block (GetEnvironmentStringsA), in its order, none
 *   skipped, the string copied and its pointer stored; the array ends with
 *   a NULL word. A failed _malloc ends the copy (the array's: no copy).
 *   NULL is an empty block.
 * - priority 4 and 5, 0x409014 and 0x407248: the handles and the stdin and
 *   stdout buffers, es2_sim_files_init.
 * - priorities 16, 31 and 32, es2_sim_heap_static_objects: 0x409f98's three
 *   filebufs (handles 0, 1, 2): new(0x3c), then the constructor 0x40a67c's
 *   new[](0x204), each; 0x418444's new[](0x100); 0x417dc8's xalloc message
 *   "Out of memory": the string's TStringRef new(0x12), its characters
 *   _malloc(64) (TStringRef::round_capacity(13) = 63, plus 1), xmsg's string
 *   object new(4) (the copy shares the TStringRef). new and new[] are
 *   _malloc(n) (0x417d94, 0x417e64). The objects' contents (CW3220's vtables
 *   and fields) are not modelled: only CW3220's iostream and string code
 *   reads them, and DBSIM imports none of it.
 * The other records allocate nothing. */
void es2_sim_heap_copy_environment(es2_sim_heap *h, const char *block);
void es2_sim_heap_static_objects(es2_sim_heap *h);

/* The environment block the guest sees, from es2sim's own (strings separated
 * by NULs, a second NUL at the end; malloc'ed, the caller frees it; NULL for
 * NULL or no memory). es2launch removes __COMPAT_LAYER from es2sim's
 * environment (a layer would quantise es2sim's true-colour output) and hands
 * its value on as ES2SIM_COMPAT_LAYER, because DBSIM runs with it and its
 * copy (rtl_imports.md, "The environment copy") has that string. Without an
 * ES2SIM_COMPAT_LAYER entry the block is returned as it is. With one, it is
 * dropped (DBSIM has no such variable); a non-empty value also replaces any
 * __COMPAT_LAYER entry by __COMPAT_LAYER=<value> before the first entry whose
 * name sorts after it, in the order Windows keeps an environment block (names
 * compared upper-cased, byte by byte, a prefix first; a leading '=' belongs
 * to the name, so the "=C:" entries stay in front). */
char *es2_sim_guest_environment(const char *block);

#endif
