/* Portable decision logic of our ES.EXE replacement. No Win32 calls.
 * Contract: docs/reverse-engineering/launcher.md. */
#ifndef ES2_LAUNCH_LOGIC_H
#define ES2_LAUNCH_LOGIC_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ES2L_R_BUFFER 20  /* size of the original -r buffer, including its dash */
#define ES2L_MAX_ARGS 16
#define ES2L_ARG_TEXT 24

typedef struct es2l_options {
    bool s, a, l, sprunknown, force_sim, r_given;
    bool r_overflow;               /* the original would overrun its buffer */
    char r_buffer[ES2L_R_BUFFER];  /* "-" followed by every -r token without its dash */
} es2l_options;

typedef enum es2l_program { ES2L_QUIT, ES2L_SHELL, ES2L_SIM } es2l_program;

typedef struct es2l_launch {
    es2l_program program;
    const char *name;              /* "vshell" or "dbsim" */
    unsigned argc;
    char args[ES2L_MAX_ARGS][ES2L_ARG_TEXT];
} es2l_launch;

typedef struct es2l_strings {
    const char *group0[6];
    const char *group1[3];
} es2l_strings;

/* Parses the WinMain command line exactly like ES.EXE. */
void es2l_parse(const char *command_line, es2l_options *out);

/* Language byte from data\language.cfg (0 when absent): 'E' means none. */
char es2l_language(int first_byte_or_eof);

/* Memory decision. Returns -1 when the start may proceed, otherwise the
 * index of the group-0 message to show. */
int es2l_memory_check(uint32_t total_phys, uint32_t total_pagefile,
                      uint32_t avail_pagefile, uint32_t allocated);

/* Next child for state (the previous exit code). sim_counter is the -R value
 * and is advanced for every DBSIM start. */
es2l_launch es2l_next(int32_t state, const es2l_options *options, char language,
                      int32_t *sim_counter);

/* Joins args with single spaces (Borland _spawnv). Returns false if the
 * result does not fit. */
bool es2l_command_line(const es2l_launch *launch, char *out, size_t capacity);

/* error.str: u32 body size, body = packed STR groups. Pointers point into
 * data. Returns false for any framing problem. */
bool es2l_parse_strings(const uint8_t *data, size_t size, es2l_strings *out);

/* Own addition (environment ES2_LAUNCH_SHELL): a shell replacement
 * started instead of VSHELL. Its command line is the configured prefix, one
 * space and the VSHELL argv (es2l_command_line). Returns false if the
 * prefix is empty or the result does not fit. */
bool es2l_replacement_command(const char *prefix, const es2l_launch *launch, char *out, size_t capacity);

/* State after a shell replacement exits: 2 starts DBSIM, 5 (VIEW DEMO)
 * starts DBSIM with -D as ES.EXE does for VSHELL's 5; every other code
 * (0, a crash, -1 for a failed start) ends the loop. Unlike VSHELL's 1,
 * which ES.EXE answers with a fresh VSHELL, nothing restarts the
 * replacement, so a failing replacement cannot loop. */
int32_t es2l_after_replacement(int32_t exit_code);

/* Own addition (environment ES2_LAUNCH_SHELL_CONSOLE): a nonempty value
 * other than "0" starts the replacement in a console window of its own (an
 * interactive text menu, shell/src/menu.c) instead of without a window. */
bool es2l_replacement_console(const char *value);

/* Own addition (environment ES2_LAUNCH_SIM or es2launch.ini [sim]
 * command): the own DBSIM process (es2sim) started instead of DBSIM. Its
 * command line is the configured prefix followed by
 * ` --arguments "<DBSIM argv without args[0]>"`. Returns false (out empty) if
 * the prefix is empty, there is no argument, an argument contains a quote or
 * the result does not fit. Its exit code goes to es2l_next unchanged: 0..6 as
 * DBSIM's, es2sim's own codes 64..71 end the loop like any code above 6. */
bool es2l_sim_command(const char *prefix, const es2l_launch *launch, char *out, size_t capacity);

/* The sim replacement's prefix: a nonempty environment value, else a
 * nonempty INI value, else NULL (DBSIM.EXE is started). */
const char *es2l_sim_prefix(const char *environment, const char *ini);

#endif
