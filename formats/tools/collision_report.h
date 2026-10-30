#ifndef ES2FMT_COLLISION_REPORT_H
#define ES2FMT_COLLISION_REPORT_H
#include "es2/formats/vol.h"
/* Statistics-only client. Returns unexpected failures/reference mismatches. */
size_t es2fmt_collision_report(const es2_vol *const *archives,
                              const char *const *names, size_t count);
#endif
