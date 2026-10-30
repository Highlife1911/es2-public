#ifndef ES2_FORMATS_STR_H
#define ES2_FORMATS_STR_H
#include "es2/formats/common.h"

/* Packed string files ("STR"): error.str and its localized copies, mission.str,
 * gamefile.str, campaign.str, the alert files (gnl/ctl/obj/prf_alrt.str),
 * maplabel.str, demolist.str and the language-dependent system, command,
 * pilot, pilots, sounds and strings0 files. Layout and evidence: docs/formats/str.md.
 *
 *   u32 body size B, then B bytes of groups; a group is
 *   i16 N, then N strings; a string is
 *   i16 L, L bytes (the last one is the NUL), u8 E, E bytes of auxiliary data.
 *
 * The number of groups is not stored; groups run until the body is used up.
 * All views borrow caller-owned, immutable input. No allocation or I/O.
 * Outputs are unchanged on error. */

enum {
    /* E is a plain tag byte: the reader does not ask for the auxiliary
     * pointer (the game's string iterator 0x453918 with a null third
     * argument, ES.EXE's error.str loader) or reads the file with its own
     * stream code (gamefile.str, whose second group stores a slot flag in E).
     * The byte is kept in es2_str_string.tag and no E bytes are skipped.
     * Without the flag E is the length of the auxiliary data that follows. */
    ES2_STR_TAG_BYTE = 1u
};

typedef struct es2_str_string {
    es2_span text; /* the L - 1 bytes before the NUL; the NUL follows in the input */
    es2_span aux;  /* E bytes of auxiliary data (always empty with ES2_STR_TAG_BYTE) */
    uint8_t tag;   /* the E byte as stored */
} es2_str_string;

typedef struct es2_str {
    es2_span input;
    es2_span body;      /* input[4 .. 4 + B) */
    es2_span trailing;  /* input after the body (stale bytes; the game ignores them) */
    size_t group_count; /* groups parsed */
    size_t string_count;
    size_t groups_end;  /* body offset after the last parsed group; with max_groups the
                         * body may extend beyond it (gamefile.str keeps a stale tail
                         * inside its size field) */
    unsigned flags;
} es2_str;

/* Validates the header and the framing of the groups.
 * max_groups == 0: every group until the body is used up exactly; a partial
 *   group at the end is ES2_ERR_TRUNCATED. A body of 0 bytes has no group.
 * max_groups > 0: exactly that many groups are parsed (ES2_ERR_TRUNCATED if
 *   the body ends sooner) and further body bytes are ignored. This is how the
 *   game's readers see error.str: a caller asks for the groups it needs.
 * Errors: ES2_ERR_TRUNCATED (header or framing), ES2_ERR_LENGTH (B larger than
 * the file), ES2_ERR_COUNT (negative N), ES2_ERR_STRING (L < 1, a string whose
 * last byte is not NUL, E above 127 when auxiliary data is read). */
es2_status es2_str_parse(es2_span input, size_t max_groups, unsigned flags,
                         es2_str *out, es2_error *error);

typedef struct es2_str_cursor {
    const es2_str *str;
    size_t offset;    /* body offset of the next group header or string */
    size_t group;     /* groups entered so far */
    size_t remaining; /* strings left in the current group */
} es2_str_cursor;

es2_status es2_str_cursor_init(const es2_str *str, es2_str_cursor *cursor, es2_error *error);
/* Enters the next group and returns its string count; ES2_ERR_NOT_FOUND after
 * the last parsed group or while strings of the current group are unread. */
es2_status es2_str_next_group(es2_str_cursor *cursor, size_t *count, es2_error *error);
/* Next string of the current group; ES2_ERR_NOT_FOUND at the end of the group. */
es2_status es2_str_next_string(es2_str_cursor *cursor, es2_str_string *out, es2_error *error);

/* Random access (walks from the start). group_size: strings in group `group`. */
es2_status es2_str_group_size(const es2_str *str, size_t group, size_t *count, es2_error *error);
es2_status es2_str_string_at(const es2_str *str, size_t group, size_t index,
                             es2_str_string *out, es2_error *error);

typedef struct es2_str_group_in {
    const es2_str_string *strings; /* text without NUL (no NUL inside), optional aux */
    size_t count;
} es2_str_group_in;

/* Serializes groups the way the original writers do (VSHELL 0x418014 for
 * mission.str: strlen + 1 including the NUL, one zero byte E after each
 * string). Without flags E is the size of the string's aux data (0 when
 * empty) and tag is ignored; with ES2_STR_TAG_BYTE E is `tag` and aux must
 * be empty (gamefile.str writer 0x40e503). Limits: N <= 32767, text size +
 * 1 <= 32767, aux <= 127, whole body <= 0x7fffffff. The u32 body size is the
 * number of bytes written after it (the MISSION.STR writer's rule).
 * NULL output with zero capacity only sets *required. A short output gives
 * ES2_ERR_CAPACITY with *required set; the output is then not modified. */
es2_status es2_str_write(const es2_str_group_in *groups, size_t group_count, unsigned flags,
                         uint8_t *output, size_t capacity, size_t *required,
                         es2_error *error);

#endif
