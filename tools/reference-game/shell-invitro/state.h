#ifndef ES2_SHELL_STATE_H
#define ES2_SHELL_STATE_H
#include "es2/formats/common.h"
typedef struct shell_save {
  size_t selection_offset, variable_offset, consumed, trailer_size;
  unsigned group, mission;
} shell_save;
/* Validate the bounded subset safe for the original loaders. Opaque final
 * bytes are reported, never interpreted. No original state is a fixture. */
es2_status shell_save_check(es2_span input, shell_save *out, es2_error *error);
#endif
