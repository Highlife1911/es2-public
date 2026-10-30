#ifndef ES2_CORE_RTL_HEAP_H
#define ES2_CORE_RTL_HEAP_H
#include <stdint.h>

/* A model of the CW3220.DLL heap (malloc, free, calloc, realloc,
 * _expand and the region growth through VirtualAlloc/VirtualFree).
 * Contract: docs/reference-game/rtl_imports.md, section "the heap, shadow
 * stage".
 *
 * The model keeps the allocator's own state (rover, region table, committed
 * and reserved end, reservation granularity) in `es2_heap` and reaches the
 * allocator's words in memory (block headers and footers, free-list links,
 * region markers, the list head block) only through `es2_heap_env`, with
 * 32-bit addresses. Every Win32 call CW3220 makes for the heap goes through
 * the env's virtual_alloc/virtual_free with the original's arguments, in the
 * original's order. User data is not modelled unless the env supplies the
 * optional fill/move operations (calloc's zero fill, realloc's copy); the
 * allocator never reads a word it did not write itself.
 *
 * Block layout (p = the pointer malloc returns, s = block size, a multiple of
 * 4, at least 16): header word at p-4 and footer word at p+s-8, both s|1
 * while the block is in use and s while it is free; a free block holds its
 * list links at p (previous) and p+4 (next). A region starts with the word 1
 * at its base (first block at base+8) and ends with the word 1 at
 * base+size-4. The free list is circular through a head block of size 0
 * (CW3220's data 0x423944: words at -8 and -4 are 0). */

#define ES2_HEAP_REGION_LIMIT 64u
#define ES2_HEAP_PAGE 0x1000u
/* Win32 constants of the calls the allocator makes. */
#define ES2_HEAP_MEM_COMMIT 0x1000u
#define ES2_HEAP_MEM_RESERVE 0x2000u
#define ES2_HEAP_MEM_DECOMMIT 0x4000u
#define ES2_HEAP_MEM_RELEASE 0x8000u
#define ES2_HEAP_PAGE_NOACCESS 0x01u
#define ES2_HEAP_PAGE_READWRITE 0x04u

typedef struct es2_heap {
    uint32_t sentinel;                               /* list head block (0x423944) */
    uint32_t rover;                                  /* next-fit start (0x423950) */
    uint32_t region_count;                           /* (0x423938) */
    uint32_t region_base[ES2_HEAP_REGION_LIMIT];     /* (0x423738 + 8 i) */
    uint32_t region_size[ES2_HEAP_REGION_LIMIT];     /* (0x42373c + 8 i) */
    uint32_t committed_end;                          /* end of the committed part of the last reservation (0x421f80) */
    uint32_t reserved_end;                           /* end of the last reservation (0x421f84) */
    uint32_t granularity;                            /* reservation rounding, 0 until the first reservation (0x422098) */
    uint32_t platform;                               /* 1 or 2: 1 MiB granularity, else 4 MiB (0x423a60) */
} es2_heap;

typedef struct es2_heap_env {
    void *context;
    uint32_t (*read32)(void *context, uint32_t address);
    void (*write32)(void *context, uint32_t address, uint32_t value);
    /* Optional (NULL: not modelled): calloc's memset(p, 0, count) and
     * realloc's memmove(dest, source, count). */
    void (*fill)(void *context, uint32_t address, uint32_t count);
    void (*move)(void *context, uint32_t dest, uint32_t source, uint32_t count);
    /* VirtualAlloc(address, size, type, protect) and VirtualFree(address,
     * size, type) with the original's arguments; the raw return values
     * (the allocator tests VirtualAlloc for nonzero, VirtualFree for 1). */
    uint32_t (*virtual_alloc)(void *context, uint32_t address, uint32_t size, uint32_t type, uint32_t protect);
    uint32_t (*virtual_free)(void *context, uint32_t address, uint32_t size, uint32_t type);
} es2_heap_env;

/* The block size for a request of n bytes: 16 below 8, else n rounded up to
 * a multiple of 4 plus 8 (32-bit wrap). A request is refused (NULL) when n
 * is 0 or the size is below 16 as a signed number. */
uint32_t es2_heap_block_size(uint32_t n);

/* The heap's start state (CW3220's init record 0x4170fc): no region, the
 * head block's words written, rover = head. */
void es2_heap_init(es2_heap *heap, const es2_heap_env *env, uint32_t sentinel, uint32_t platform);

uint32_t es2_heap_malloc(es2_heap *heap, const es2_heap_env *env, uint32_t n);          /* 0x41729c */
void es2_heap_free(es2_heap *heap, const es2_heap_env *env, uint32_t p);                /* 0x417160 */
uint32_t es2_heap_calloc(es2_heap *heap, const es2_heap_env *env, uint32_t count, uint32_t n); /* 0x416ffc */
uint32_t es2_heap_realloc(es2_heap *heap, const es2_heap_env *env, uint32_t p, uint32_t n);   /* 0x417b8c */
uint32_t es2_heap_expand(es2_heap *heap, const es2_heap_env *env, uint32_t p, uint32_t n);    /* 0x417a70 */

/* Every allocator word of a heap state, read through `read32`: the four
 * words of the head block, then per region the base marker, per block its
 * header, footer and (free blocks) both links, and the end marker. `visit`
 * may be NULL. Returns the number of blocks, or -1 when the words do not
 * form a valid heap (a size below 16 or not a multiple of 4, a block past its
 * region, a footer different from its header, a region without its
 * markers). */
typedef void (*es2_heap_visit)(void *context, uint32_t address, uint32_t value);
int32_t es2_heap_walk(const es2_heap *heap, uint32_t (*read32)(void *context, uint32_t address), void *read_context,
                      es2_heap_visit visit, void *visit_context);

/* Takes over a running heap: `state` holds the allocator's variables as read
 * from the original, `read32` reads its memory; every word of the walk is
 * written to `env`. Returns the number of blocks or -1 (invalid heap). */
int32_t es2_heap_attach(es2_heap *heap, const es2_heap *state, const es2_heap_env *env,
                        uint32_t (*read32)(void *context, uint32_t address), void *read_context);

/* ------------------------------------------------------------------------
 * Word store and shadow environment: the model's memory as a hash table of
 * the words it wrote (caller-provided storage, capacity a power of two), and
 * an env whose virtual_alloc/virtual_free replay the calls the original made
 * in the same operation (recorded by the caller), checking each argument. */
typedef struct es2_heap_words {
    uint32_t *keys, *values;
    uint32_t bits, count, limit;
    int overflow;        /* more than limit words or an unusable address: the model is no longer exact */
    uint32_t missing;    /* reads of a word the model never wrote */
} es2_heap_words;
void es2_heap_words_init(es2_heap_words *words, uint32_t *keys, uint32_t *values, uint32_t bits);
void es2_heap_words_clear(es2_heap_words *words);
uint32_t es2_heap_words_read(es2_heap_words *words, uint32_t address);
void es2_heap_words_write(es2_heap_words *words, uint32_t address, uint32_t value);
/* 1 and the value when the model wrote the word. */
int es2_heap_words_find(const es2_heap_words *words, uint32_t address, uint32_t *value);

enum { ES2_HEAP_OS_ALLOC = 1, ES2_HEAP_OS_FREE = 2 };
typedef struct es2_heap_os_call {
    uint32_t kind, address, size, type, protect, result;   /* protect 0 for VirtualFree */
} es2_heap_os_call;

typedef struct es2_heap_shadow {
    es2_heap_words words;
    uint32_t *journal;                 /* addresses written in this operation (may repeat) */
    uint32_t journal_capacity, journal_count;
    int journal_overflow;
    const es2_heap_os_call *calls;     /* the original's calls in this operation */
    uint32_t call_count, call_next;
    int os_mismatch;                   /* a call differed, was missing or was not made */
    uint32_t os_mismatch_index;
} es2_heap_shadow;
void es2_heap_shadow_env(es2_heap_env *env, es2_heap_shadow *shadow);
void es2_heap_shadow_begin(es2_heap_shadow *shadow, const es2_heap_os_call *calls, uint32_t count);
/* After the model's operation: 0 when every recorded call was replayed with
 * equal arguments and none is left over. */
int es2_heap_shadow_finish(es2_heap_shadow *shadow);

#endif
