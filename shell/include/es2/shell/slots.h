#ifndef ES2_SHELL_SLOTS_H
#define ES2_SHELL_SLOTS_H

#include "es2/formats/common.h"

/* VSHELL's save slots (vshell_ui.md section 9.1, "Slot table"). The
 * slot table is SAV/GAMEFILE.STR as VSHELL holds it in memory (0x4826f4,
 * twelve records of 0x5e bytes: file name 13, display text 80, flag 1).
 * Slots 0..9 are the user slots, 10 the campaign slot (GAME_R.SAV), 11 the
 * training slot (GAME_T.SAV). No file I/O; output buffers follow the formats
 * convention (NULL output or zero capacity queries the size, errors leave
 * output untouched). */
#define ES2_SHELL_SLOT_COUNT 12
#define ES2_SHELL_SLOT_FILE 13
#define ES2_SHELL_SLOT_TEXT 80
/* Own limit of the typed slot name (VSHELL's limit is the row's pixel width;
 * see vshell_ui.md section 4.3). Only the fallback without a font:
 * es2_shell_slot_row_typed applies VSHELL's width rule. */
#define ES2_SHELL_SLOT_NAME_MAX 24

typedef struct es2_shell_slot {
    char file[ES2_SHELL_SLOT_FILE];   /* NUL-terminated */
    char text[ES2_SHELL_SLOT_TEXT];   /* NUL-terminated */
    uint8_t flag;                     /* 1: the slot holds a game */
} es2_shell_slot;
typedef struct es2_shell_slot_table {
    es2_shell_slot slot[ES2_SHELL_SLOT_COUNT];
} es2_shell_slot_table;

/* The start-up reader 0x40e380 over a fresh (zero) table: per group a count
 * word and count entries (length word, that many raw bytes into the record,
 * one tag byte; group 0 into the file names, its tags ignored; group 1 into
 * the texts, its tags are the flags). A text whose fifth byte is zero gets
 * `empty` appended (ESTEXT.BIN entry 0x21 of the running language, e.g. the
 * German "LEER"); empty may be an empty span only if no text needs it.
 * Refused (the original would overrun its records): a count above 12, a
 * length above the record, a file name without NUL in its 13 bytes, a text
 * that does not fit 80 bytes with its NUL, truncated input. A negative count
 * reads nothing, as the original loop does. */
es2_status es2_shell_slots_load(es2_span file, es2_span empty,
    es2_shell_slot_table *out, es2_error *error);

/* The writer 0x40e503: size dword, then both groups with exactly twelve
 * entries (length = strlen + 1, the string with its NUL, tag 0 for file
 * names and the flag for texts), written from offset 0 over `previous`
 * without truncation; the size dword is the resulting file length - 4. */
es2_status es2_shell_slots_write(const es2_shell_slot_table *table,
    es2_span previous, uint8_t *output, size_t capacity, size_t *required,
    es2_error *error);

/* The slot index the save and load functions really use (0x40e962,
 * 0x40eabe): 10 becomes 11 when the shell mode word 0x4826f0 is 0. */
int es2_shell_slot_effective(int slot, int16_t mode);

/* 0x40e6cd without its write: for slots 0..9 the text becomes `text`, then
 * the slot's flag becomes 1; slots 10 and 11 ignore `text` (the save on exit
 * passes NULL). Refused: NULL text for slots 0..9 (the original's strcpy
 * faults) and a text that does not fit the record. */
es2_status es2_shell_slots_mark(es2_shell_slot_table *table, int slot,
    const char *text, es2_error *error);

/* The text a SAVE in the Saved games screen passes for slot 0..9: the row
 * "%2d. %s" (slot + 1, typed name), 0x4381cd. The typed name may hold the
 * characters of the row's edit set (0x475869: '^', digits, letters, space)
 * up to ES2_SHELL_SLOT_NAME_MAX of them. */
es2_status es2_shell_slot_row(int slot, const char *typed,
    char out[ES2_SHELL_SLOT_TEXT], es2_error *error);

/* GAMEFILE.STR after the save 0x40e933(slot, text) with a loaded game:
 * slot table loaded from `file` (as above), slot made effective for `mode`,
 * marked and written over `file`. *effective and file_name (the SAV file of
 * that slot, without directory) are set whenever the call succeeds, also
 * for a size query. A slot without a file name is refused. */
es2_status es2_shell_slot_gamefile(es2_span file, es2_span empty, int slot,
    const char *text, int16_t mode, int *effective,
    char file_name[ES2_SHELL_SLOT_FILE], uint8_t *output, size_t capacity,
    size_t *required, es2_error *error);

/* Companion copy names of a slot save (0x413095; load 0x4131e3 copies them
 * back): index 0 script%d.dat <- DATA\script.dat, 1 missn%d.str <-
 * DATA\mission.str, 2 player%d.mec <- DATA\player.mec. */
es2_status es2_shell_slot_companion(int slot, int index, char *slot_name,
    size_t slot_capacity, const char **data_name, es2_error *error);

/* The slot statistics of the Saved games screen (VSHELL stats.cpp;
 * vshell_ui.md section 13). VSHELL keeps one 0x43-byte record per slot
 * 0..10 at 0x48cebc + 0x43*slot (the name at +4 is the row's 0x48cec0); the
 * records start zero and a slot without its flag is never read. Layout:
 * +0 word, +2 word, +4 name (up to 30 bytes, NUL only if shorter), +0x22
 * word, +0x24 byte, +0x25 skill, +0x27, +0x29 rank, +0x2b, +0x2d/+0x33 Herc
 * kills current/total, +0x2f/+0x35 Flyer, +0x31/+0x37 Base, +0x39 (words),
 * +0x3b salvage dword, +0x3f sector word, +0x41 mission word. */
#define ES2_SHELL_STATS_SLOTS 11
#define ES2_SHELL_STATS_SIZE 0x43
typedef struct es2_shell_stats {
    uint8_t raw[ES2_SHELL_STATS_SIZE];
} es2_shell_stats;

/* One slot of the start-up read 0x4309f0 over `record` (the original writes
 * into the existing record): the slot's SAV is walked as the reader seeks
 * (33 blocks of 1 + 2 + 10n bytes, 22 bytes, the CAREER position words
 * sector and mission into +0x3f/+0x41, 148 bytes, 36 roster pilots of
 * 31 + n bytes, 16 bytes, the player pilot through 0x410520 into +0..+0x3a,
 * the hangar records of 80 + 12m bytes, 18 bytes, the salvage dword into
 * +0x3b). Refused, record untouched: a seek or read outside the file, a
 * negative name length or one that would write past +0x3e (a name up to
 * 59 bytes is written raw and overwritten by the later fields, as the
 * original does). A negative hangar count reads no records (0x430d3c). */
es2_status es2_shell_stats_read(es2_span sav, es2_shell_stats *record, es2_error *error);

/* The refresh 0x430ec4(slot) after a save or a load: from the memory state
 * (`memory`, laid out as a record: the player pilot 0x482b60.., salvage
 * 0x482bd8 at +0x3b, CAREER 0x46fc58/0x46fc5a at +0x3f/+0x41) it copies the
 * name with strcpy and the words +0x25, +0x29, +0x2d..+0x37, the salvage and
 * both CAREER words; +0, +2, +0x22, +0x24, +0x27, +0x2b and +0x39 keep their
 * values. Refused: a memory name without NUL in its 30 bytes. */
es2_status es2_shell_stats_refresh(es2_shell_stats *record, const es2_shell_stats *memory, es2_error *error);

/* What the statistics panel 0x4377bc shows for a record: the name (up to the
 * first NUL of the 30 bytes), ESTEXT IDs of skill (0x35 + skill), rank
 * (0x39 + rank) and sector (0x76 + sector), the six kill words (Herc,
 * Flyer, Base; current then total), salvage / 1000 (signed, truncated)
 * with ESTEXT 0x2f, and mission + 1. */
typedef struct es2_shell_stats_view {
    char name[31];
    int skill_text, rank_text, sector_text, salvage_text;
    int kills[6];
    int32_t salvage;
    int mission;
} es2_shell_stats_view;
void es2_shell_stats_show(const es2_shell_stats *record, es2_shell_stats_view *view);

/* The naming row under VSHELL's edit-field rule instead of the
 * ES2_SHELL_SLOT_NAME_MAX fallback above (vshell_text.md, "Edit field").
 * The row's edit field (r - l = 325 for slots 0 and 9, else 327) starts as
 * "%2d. " (0x4381cd with an empty name, then +0xb7 := 4) and takes each typed
 * byte through 0x40c382 with the set 0x475869 and the glyph widths of
 * `font` (FONT2): every byte must be taken. Refused: a byte outside the set
 * (ES2_ERR_STRING), the length or width rule (ES2_ERR_LENGTH; offset = the
 * typed index), and a row of 80 bytes or more, which the pixel rule can
 * allow but the 80-byte slot record cannot hold (ES2_ERR_CAPACITY; the
 * original's strcpy 0x40e6ea would run past the record). */
struct es2_dfn;
es2_status es2_shell_slot_row_typed(int slot, const char *typed, const struct es2_dfn *font,
    char out[ES2_SHELL_SLOT_TEXT], es2_error *error);

#endif
