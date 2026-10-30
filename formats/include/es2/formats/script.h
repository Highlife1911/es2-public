#ifndef ES2_FORMATS_SCRIPT_H
#define ES2_FORMATS_SCRIPT_H
#include "es2/formats/common.h"

/* The prepared script (DATA\script.dat, SAV\script<n>.dat): docs/formats/mission.md
 * "Prepared script exact framing". Ten header words, then 13 lists, each a signed 16-bit
 * count and its records, in the order DBSIM's pass 1 reads them (0x4246dd..0x424f58).
 * Views borrow immutable input; no allocation or I/O; outputs are unchanged on error.
 * Counts must be nonnegative and every route reference a coordinate index below the
 * coordinate count. Bytes after the last list are a stale tail, not an error: VSHELL
 * rewrites data\script.dat without truncation (0x41b278) and copies it whole to
 * sav\script%d.dat, and DBSIM's readers stop after the last list; an exact file has an
 * empty `trailing`. Header words and record contents are not restricted. */
typedef enum es2_script_list {
    ES2_SCRIPT_COORDINATES, ES2_SCRIPT_ANGLES, ES2_SCRIPT_ROUTES, ES2_SCRIPT_REGIONS,
    ES2_SCRIPT_TRIGGERS, ES2_SCRIPT_TIMERS, ES2_SCRIPT_MECHS, ES2_SCRIPT_FLYERS,
    ES2_SCRIPT_OBJECTS, ES2_SCRIPT_ORDERS, ES2_SCRIPT_GROUPS, ES2_SCRIPT_OBJECTIVES,
    ES2_SCRIPT_STRINGS,
    ES2_SCRIPT_LIST_COUNT
} es2_script_list;
typedef struct es2_script_part {
    size_t offset;     /* of the count word */
    size_t count;
    es2_span records;  /* the bytes after the count word */
} es2_script_part;
typedef struct es2_script {
    es2_span input;
    int16_t header[10];
    es2_script_part lists[ES2_SCRIPT_LIST_COUNT];
    size_t route_references;  /* over all routes */
    size_t end;               /* where the last list ends */
    es2_span trailing;        /* input after `end` */
} es2_script;

/* Bytes per record; 0 for the routes (a count word K, then K coordinate indices). */
size_t es2_script_stride(es2_script_list list);
es2_status es2_script_parse(es2_span input, es2_script *out, es2_error *error);
/* Route `index`'s coordinate indices (2 * K bytes) and the offset of its count word. */
es2_status es2_script_route_at(const es2_script *script, size_t index,
                               es2_span *references, size_t *offset, es2_error *error);
#endif
