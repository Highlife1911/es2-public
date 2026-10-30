/* The image-access measurement of es2sim (own_process.md, "what es2sim really
 * reads from the mapped image").
 *
 * Off by default. With `--image-access <file>` the host, after the image's
 * protections (es2_sim_map), arms it:
 *
 *  - CODE becomes no-access except the pages that hold the known CODE read
 *  sites (the seven tables of dbsim_gap.md, "What `es2sim` takes from `DBSIM.EXE`"); a read fault on a
 *    no-access CODE page is recorded (VA, operand size when the faulting
 *    instruction decodes, the faulting instruction's module and offset and
 *    the first return address of the own module on the stack) and served:
 *    the page is made readable for that one instruction (trap flag), then
 *    no-access again. Execute faults are not touched: they go on to the
 *    stage-1 trap (dispatch_trap.c) as without the switch. The table pages
 *    keep their protection plus a guard (sampled like DATA below).
 *  - The file-backed pages outside CODE (headers, the initialised DATA,
 *    .idata, .edata, .reloc, .rsrc) get a guard: the first access of a page
 *    is recorded (read or write, and the bytes of the access when the
 *    instruction decodes), and the guard is re-armed after that instruction
 *    up to a budget per page and epoch (32 per tick, or per 50 ms); so
 *    pages are exact, bytes a sample.
 *
 * Nothing of the image's bytes is written: the map file holds VA ranges and
 * counts only. The host's own reads (the tick record, the watches, the
 * snapshot) run between es2_sim_image_access_pause and _resume and are not
 * counted. No replay result depends on the switch: every fault is served
 * and the guest sees the same bytes. Windows x86 only. */
#ifndef ES2_SIM_IMAGE_ACCESS_H
#define ES2_SIM_IMAGE_ACCESS_H
#include <stddef.h>
#include <stdint.h>
#include "es2/sim/image.h"

/* The CODE read sites own code reads by constant VA (dbsim_gap.md, "What
 * `es2sim` takes from `DBSIM.EXE`": six byte tables and the dword at 0x4967ae);
 * their pages stay readable. */
#define ES2_SIM_IMAGE_TABLE_SITES 7u
extern const uint32_t es2_sim_image_table_sites[ES2_SIM_IMAGE_TABLE_SITES];

/* Where the map goes; NULL or "" leaves the measurement off (the default).
 * Call before es2_sim_map. 0, or 1 when the path is too long. */
int es2_sim_image_access_configure(const char *path);
int es2_sim_image_access_configured(void);

/* Arms the measurement over the placed and protected image `info` (mapped
 * identity at info->image_base): a no-op returning 0 when not configured.
 * `sites`: the CODE VAs whose pages stay readable (guarded). 0, or 1 and
 * *error. */
int es2_sim_image_access_arm(const es2_sim_image_info *info, const uint32_t *sites, unsigned site_count, const char **error);
int es2_sim_image_access_armed(void);

/* The host's own reads: pause restores the image's protections (no guard,
 * CODE readable), resume re-arms everything and starts a new epoch. Nested
 * calls count. No-ops when not armed. */
void es2_sim_image_access_pause(void);
void es2_sim_image_access_resume(void);

/* Writes the map (JSON) to the configured path once; later calls do
 * nothing. Pauses first. 0, or 1 when the file cannot be written. */
int es2_sim_image_access_write(const char *reason);

/* Restores the protections, removes the handler and forgets the counts
 * (tests). */
void es2_sim_image_access_disarm(void);

/* ---- what was recorded (tests, the writer) */
typedef struct es2_sim_code_read {
    uint32_t va, size;          /* size 0: the instruction did not decode */
    uint32_t eip_offset;        /* the faulting instruction, offset in its module */
    uint32_t caller_offset;     /* first return address into the main module on the stack, offset (0: none) */
    char module[32];            /* the faulting instruction's module (file name) */
    uint64_t count;
} es2_sim_code_read;

typedef struct es2_sim_access_page_stats {
    uint32_t va;
    uint64_t read_faults, write_faults, exec_faults, rearms;
    unsigned read_bytes, written_bytes;   /* distinct bytes seen (sampled) */
} es2_sim_access_page_stats;

/* The page holding `va` among the guarded pages: 0 and *out, else 1. */
int es2_sim_image_access_page(uint32_t va, es2_sim_access_page_stats *out);
/* Whether the byte at `va` was seen read (1) / written (2) / both (3). */
unsigned es2_sim_image_access_byte(uint32_t va);
/* The distinct CODE reads recorded so far. */
unsigned es2_sim_image_access_code_reads(const es2_sim_code_read **reads);

/* The memory operand size of the instruction at `ip` (up to `avail` bytes):
 * 1, 2, 4, 6, 8, 10, 16, 28 or 108; 0 when it has no memory operand or does
 * not decode. *string is set for the string instructions (one element per
 * fault). */
unsigned es2_sim_access_operand_size(const uint8_t *ip, size_t avail, int *string);

#endif
