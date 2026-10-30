#ifndef ES2_SHELL_MENU_H
#define ES2_SHELL_MENU_H

/* Portable parts of the own text-mode menu es2menu.exe. The
 * console screens and the Win32 glue are in menu.c; contract and
 * acceptance in docs/reverse-engineering/launcher.md, section "Own menu".
 * Nothing
 * here embeds an original string: labels come from the installation's
 * ESTEXT.BIN, slot names from its SAV/GAMEFILE.STR. */
#include "es2/formats/common.h"

/* Command line: es2menu --menu DIR [--input FILE] <VSHELL argv>, where the
 * launcher appends the VSHELL argv (dummy -eggplant -X<state> [-s] [-a] [-@]
 * [-<language letter>]); --no-movies (own) is -a. Returns 0 and fills *out,
 * or -1 with a message. */
typedef struct es2m_args {
    const char *menu_dir;  /* menu directory (menu.ini, campaigns) */
    const char *input;     /* scripted input file, or NULL for stdin */
    int state;             /* the -X value */
    char language[4];      /* ENG (no letter, or E), GER (-G), FRE (-F) */
    int no_movies;         /* -a (VSHELL's movie flag 0x482359 := 0) or --no-movies */
} es2m_args;
int es2m_parse_args(int argc, char **argv, es2m_args *out, char *message, size_t capacity);

/* Entry of a localized BIN catalogue (u32 count, u32, then count u16
 * offsets from the text base 8 + 2 * count, NUL-terminated strings):
 * es2m_bin_raw gives the text up to its NUL, as VSHELL's lookup 0x4087f0
 * returns it (the slot table appends ESTEXT 0x21 this way);
 * es2m_bin_string also drops trailing CR/LF, for printing. The slot table
 * itself (SAV/GAMEFILE.STR) is es2/shell/slots.h. */
es2_status es2m_bin_raw(es2_span catalogue, size_t index, es2_span *text, es2_error *error);
es2_status es2m_bin_string(es2_span catalogue, size_t index, es2_span *text, es2_error *error);

/* What the menu shows of a SAV. */
typedef struct es2m_save_info {
    char name[32];          /* player name */
    uint16_t rank;          /* player rank word (the skill level of a new career) */
    int16_t group, position;/* CAREER selection */
    int16_t final_word;     /* 2 while the campaign runs (0x4826f2) */
    uint32_t salvage;       /* salvage total (tally, 0x482bd8) */
    size_t image_size;      /* writer image length (reader_end) */
} es2m_save_info;
es2_status es2m_save_info_read(es2_span sav, es2m_save_info *out, es2_error *error);

#endif
