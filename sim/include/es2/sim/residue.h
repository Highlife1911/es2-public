/* Stack and register residue as modelled values (own_process.md section 5).
 * Inside DBSIM some callees read stack bytes or registers they did not
 * write. The own process has no original frames, so each such read gets a
 * modelled value: the table lists the reader, where it reads, the width, the
 * rule and the evidence that the golden accepts the value. Rule `zero` is
 * what the adapter's direct path supplies (its zeroed scratch frame); a rule
 * other than zero needs the writer's contract from the bytes. A replay
 * difference bisected to a residue read gets a row. The host's own stack
 * bytes are never a residue source. */
#ifndef ES2_SIM_RESIDUE_H
#define ES2_SIM_RESIDUE_H
#include <stdint.h>

typedef enum es2_sim_residue_rule { ES2_RESIDUE_ZERO, ES2_RESIDUE_WRITER, ES2_RESIDUE_CONSTANT } es2_sim_residue_rule;
typedef enum es2_sim_residue_place {
    ES2_RESIDUE_FRAME,       /* offset from the reader's EBP (negative: below) */
    ES2_RESIDUE_REGISTER,    /* the register named in `where` */
    ES2_RESIDUE_OBJECT       /* bytes of an object or argument the caller supplies */
} es2_sim_residue_place;

typedef struct es2_sim_residue {
    uint32_t reader;            /* VA of the reading function */
    const char *name;
    es2_sim_residue_place place;
    int32_t offset;             /* frame offset or object offset; 0 when not known */
    unsigned width;             /* bytes; 0 when not known */
    es2_sim_residue_rule rule;
    uint32_t value;             /* for ES2_RESIDUE_CONSTANT */
    const char *where;          /* what is read, in words */
    const char *evidence;       /* why the golden accepts the value, or "open" */
} es2_sim_residue;

unsigned es2_sim_residue_count(void);
const es2_sim_residue *es2_sim_residue_row(unsigned i);
/* The first row of a reader, or NULL. */
const es2_sim_residue *es2_sim_residue_find(uint32_t reader);

#endif
