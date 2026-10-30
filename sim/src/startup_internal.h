/* Helpers shared by startup.c and volumes.c (not installed). */
#ifndef ES2_SIM_STARTUP_INTERNAL_H
#define ES2_SIM_STARTUP_INTERNAL_H
#include <stdint.h>
#include "es2/sim/startup.h"
#include "es2/sim/memory.h"
#include "es2/sim/files.h"

static inline uint32_t su_r32(const es2_sim_startup *s, uint32_t a) { return s->memory->read32(s->memory->context, a); }
static inline uint16_t su_r16(const es2_sim_startup *s, uint32_t a) { return s->memory->read16(s->memory->context, a); }
static inline uint8_t su_r8(const es2_sim_startup *s, uint32_t a) { return es2_sim_read8(s->memory, a); }
static inline void su_w32(const es2_sim_startup *s, uint32_t a, uint32_t v) { s->memory->write32(s->memory->context, a, v); }
static inline void su_w16(const es2_sim_startup *s, uint32_t a, uint16_t v) { s->memory->write16(s->memory->context, a, v); }
static inline void su_w8(const es2_sim_startup *s, uint32_t a, uint8_t v) { es2_sim_write8(s->memory, a, v); }

/* strlen / strcpy / strcat / strcmp on guest memory (the original's inline
 * rep movs/scas sequences and the pure CW3220 string imports; no side
 * effect beyond the bytes). */
static inline uint32_t su_strlen(const es2_sim_startup *s, uint32_t p) {
    uint32_t n = 0;
    while (su_r8(s, p + n)) ++n;
    return n;
}
static inline void su_strcpy(const es2_sim_startup *s, uint32_t d, uint32_t p) {
    uint32_t i = 0;
    uint8_t b;
    do { b = su_r8(s, p + i); su_w8(s, d + i, b); ++i; } while (b);
}
static inline void su_strcat(const es2_sim_startup *s, uint32_t d, uint32_t p) { su_strcpy(s, d + su_strlen(s, d), p); }
/* The inline compare loop of the original (bytes, signed only as equal or not). */
static inline int su_streq(const es2_sim_startup *s, uint32_t a, uint32_t b) {
    for (uint32_t i = 0;; ++i) {
        uint8_t x = su_r8(s, a + i), y = su_r8(s, b + i);
        if (x != y) return 0;
        if (!x) return 1;
    }
}

/* The construction counter [[0x6cbee4]] += n. */
static inline void su_counter(const es2_sim_startup *s, int32_t n) {
    uint32_t p = su_r32(s, 0x006cbee4u);
    su_w32(s, p, su_r32(s, p) + (uint32_t)n);
}

/* A call of other DBSIM code through the dispatch (cdecl/stdcall slots,
 * EAX/EDX/ECX zero at entry). 0, or 1 when refused (outcome set). */
int su_call(es2_sim_startup *s, uint32_t va, const uint32_t *slots, unsigned count, uint32_t *eax);
/* A refusal of the start-up's own (no dispatch entry is involved): logged
 * through the dispatch's refusal callback. Always returns 1. */
int su_refuse(es2_sim_startup *s, uint32_t va, const char *name, const char *reason);
/* The original reads, writes or calls through NULL at va: with a process
 * record that has process_fault the process ends with
 * ES2_SIM_ACCESS_VIOLATION (a recorder returns: outcome
 * ES2_SIM_STARTUP_EXIT, exit_code the code), otherwise su_refuse. Always
 * returns 1. */
int su_null_refuse(es2_sim_startup *s, uint32_t va, const char *name, const char *reason);

/* The frame area: a stack of guest blocks (zero-filled on allocation). */
uint32_t su_frame_push(es2_sim_startup *s, uint32_t bytes);
void su_frame_pop(es2_sim_startup *s, uint32_t to);

/* Thunks and callees (DBSIM.EXE). */
/* Argument slots passed to _fscanf and _sprintf (variadic; the services
 * copy that many). */
#define SU_SCAN_SLOTS 16u

#define VA_NEW 0x0047749cu
#define VA_NEW_ARRAY 0x004774f0u
#define VA_DELETE 0x00477548u
#define VA_DELETE_ARRAY 0x0047758cu
#define VA_GAME_CALLOC 0x0047747cu
#define VA_FOPEN 0x00496802u
#define VA_FCLOSE 0x00496838u
#define VA_FREAD 0x0049684au
#define VA_FWRITE 0x00496832u
#define VA_FSCANF 0x004967b4u
#define VA_OPEN 0x004967c6u
#define VA_CLOSE 0x00496874u
#define VA_READ 0x00496808u
#define VA_WRITE 0x0049677eu
#define VA_LSEEK 0x00496826u
#define VA_FINDFIRST 0x0049685cu
#define VA_FINDNEXT 0x00496862u
#define VA_ATOL 0x00496880u
#define VA_SPRINTF 0x00496796u
#define VA_MODULE_HANDLE 0x00496898u

#endif
