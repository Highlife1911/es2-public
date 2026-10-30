#ifndef ES2_CORE_RTL_H
#define ES2_CORE_RTL_H
#include <stdint.h>

/* The 20 "pure" CW3220.DLL imports of DBSIM.EXE as own code
 * (contracts: docs/reference-game/rtl_imports.md, section "the pure imports as
 * own code").
 *
 * Every function is described by what the CW3220 code does with memory and
 * with EAX/ECX/EDX, including the cases outside the C standard (overlapping
 * copies, case folding of bytes >= 0x80, radix and sign handling of itoa,
 * atol's overflow and whitespace set, the formatter's quirks). Memory is
 * reached only through `es2_rtl_memory` with 32-bit guest addresses
 * (identity inside the game process, a private overlay or arena in tests),
 * and every function reads and writes exactly the bytes the original reads
 * and writes, in the same order where an overlap could make it matter. */

typedef struct es2_rtl_memory {
    void *context;
    uint8_t (*read8)(void *context, uint32_t address);
    void (*write8)(void *context, uint32_t address, uint8_t value);
} es2_rtl_memory;

/* On entry: the caller's EAX/ECX/EDX (some functions leave ECX or EDX as
 * they were). On return: the values CW3220 returns in them. */
typedef struct es2_rtl_regs {
    uint32_t eax, ecx, edx;
} es2_rtl_regs;

typedef enum es2_rtl_function {
    ES2_RTL_MEMSET,
    ES2_RTL_MEMCPY,
    ES2_RTL_MEMMOVE,
    ES2_RTL_MEMCMP,
    ES2_RTL_STRLEN,
    ES2_RTL_STRCAT,
    ES2_RTL_STRNCAT,
    ES2_RTL_STRNCPY,
    ES2_RTL_STRCHR,
    ES2_RTL_STRRCHR,
    ES2_RTL_STRSTR,
    ES2_RTL_STRNCMP,
    ES2_RTL_STRICMP,
    ES2_RTL_STRNICMP,
    ES2_RTL_STRUPR,
    ES2_RTL_STRLWR,
    ES2_RTL_ITOA,
    ES2_RTL_ATOL,
    ES2_RTL_SPRINTF,
    ES2_RTL_VSPRINTF,
    ES2_RTL_FUNCTION_COUNT
} es2_rtl_function;

enum {
    ES2_RTL_DONE = 0,
    /* Nothing observable was done that the original would not do first
     * (at most sprintf's leading dest[0] = 0): the caller runs the original.
     * Only sprintf/vsprintf return it: a format with a floating-point, %p or
     * %n conversion, or an empty result (whose EDX the original leaves from
     * the formatter's internals). */
    ES2_RTL_UNSUPPORTED = 1
};

/* CW3220 export name ("_memset", ...) of a function. */
const char *es2_rtl_name(es2_rtl_function function);

/* Calls `function` with its cdecl argument slots at guest address `args`
 * (the caller's ESP at the call + 4, i.e. just above the return address).
 * Arguments are read from there; sprintf and vsprintf also write their
 * output cursor back into the first slot, as CW3220 does. Results whose
 * original value is a stack address (sprintf's ECX, itoa's ECX/EDX) are
 * derived from `args`. Returns ES2_RTL_DONE or ES2_RTL_UNSUPPORTED. */
int es2_rtl_call(es2_rtl_function function, const es2_rtl_memory *memory, uint32_t args, es2_rtl_regs *regs);

/* The CW3220 case mappings used by stricmp/strnicmp/strupr/strlwr: the int
 * results of toupper/tolower for a sign-extended byte argument (-1 for the
 * byte 0xff, which CW3220 treats as EOF). */
int32_t es2_rtl_toupper(uint8_t byte);
int32_t es2_rtl_tolower(uint8_t byte);

/* CW3220's atol on a host string: the EAX of ES2_RTL_ATOL (its whitespace
 * set and the 32-bit wrap on overflow; rtl_imports.md). VSHELL imports the
 * same function. */
int32_t es2_rtl_atol(const char *text);

/* What the frame of a leaf function (one that calls no further CW3220
 * code) leaves below its return address: the registers it pushes, in push
 * order ('p' EBP, 'b' EBX, 's' ESI, 'd' EDI); NULL for the functions that
 * call further code (their residue is not modelled). */
const char *es2_rtl_leaf_residue(es2_rtl_function function);

#endif
