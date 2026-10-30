/* The save checks of tools.wasm (docs/web_build.md section 13, step 3): one uploaded file's
 * name and bytes against the formats/ parsers. web/saves.mjs runs it per file and decides
 * what is written; this file only classifies and parses. */
#include <stddef.h>
#include <stdint.h>

#include "es2/formats/player_mec.h"
#include "es2/formats/sav.h"
#include "es2/formats/script.h"
#include "es2/formats/str.h"

enum {
    ES2T_NOT_ACCEPTED = 0,
    ES2T_GAME_SAV = 1,  /* GAME_<n>.SAV */
    ES2T_SCRIPT = 2,    /* script<n>.dat */
    ES2T_MISSION = 3,   /* missn<n>.str */
    ES2T_PLAYER = 4,    /* player<n>.mec */
    ES2T_GAMEFILE = 5,  /* GAMEFILE.STR */
};

/* The result of es2t_check besides an es2_status (ES2_OK and the parsers' errors). */
enum { ES2T_NAME = -1 };

/* VSHELL's slot table (0x40e380 into 0x482751 + 0x5e * i): 12 slots, a file name field of
 * 13 bytes and a text field of 80 bytes, each with its NUL (es2/shell/slots.h). */
#define SLOT_COUNT 12u
#define SLOT_FILE 13u
#define SLOT_TEXT 80u

static int upper(int c) { return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c; }

/* Matches `prefix` and `suffix` (upper case) case-insensitively around a middle part of one
 * or more decimal digits, or, with `letters`, one of those letters alone. */
static int matches(const char *name, const char *prefix, const char *suffix, const char *letters)
{
    size_t i = 0, n = 0, digits = 0, s = 0;
    while (name[n]) n++;
    for (; prefix[i]; i++)
        if (i >= n || upper((unsigned char)name[i]) != prefix[i]) return 0;
    while (suffix[s]) s++;
    if (n < i + s + 1) return 0;
    for (size_t k = 0; k < s; k++)
        if (upper((unsigned char)name[n - s + k]) != suffix[k]) return 0;
    for (size_t k = i; k < n - s; k++) {
        if (name[k] < '0' || name[k] > '9') {
            if (!letters || n - s - i != 1) return 0;
            for (const char *l = letters; *l; l++)
                if (upper((unsigned char)name[k]) == *l) return 1;
            return 0;
        }
        digits++;
    }
    return digits > 0;
}

/* The kind of a file name without directory (ES2T_*). GAMEFILE.STR names the slots 10 and 11
 * GAME_R.SAV and GAME_T.SAV (docs/formats/mission.md), so GAME_ also takes R and T. */
int es2t_kind(const char *name)
{
    if (!name) return ES2T_NOT_ACCEPTED;
    if (matches(name, "GAME_", ".SAV", "RT")) return ES2T_GAME_SAV;
    if (matches(name, "SCRIPT", ".DAT", NULL)) return ES2T_SCRIPT;
    if (matches(name, "MISSN", ".STR", NULL)) return ES2T_MISSION;
    if (matches(name, "PLAYER", ".MEC", NULL)) return ES2T_PLAYER;
    {
        static const char gamefile[] = "GAMEFILE.STR";
        size_t i = 0;
        for (; gamefile[i]; i++)
            if (upper((unsigned char)name[i]) != gamefile[i]) return ES2T_NOT_ACCEPTED;
        if (!name[i]) return ES2T_GAMEFILE;
    }
    return ES2T_NOT_ACCEPTED;
}

/* The slot table as VSHELL reads it: two groups of at most 12 strings with the E byte as a
 * tag (the second group's slot flag), the size field not consulted. */
static es2_status check_gamefile(es2_span input, es2_error *e)
{
    es2_str str;
    es2_status s = es2_str_parse(input, 2, ES2_STR_TAG_BYTE, &str, e);
    if (s != ES2_OK) return s;
    for (size_t group = 0; group < 2; group++) {
        size_t count;
        if ((s = es2_str_group_size(&str, group, &count, e)) != ES2_OK) return s;
        if (count > SLOT_COUNT) return es2_fail(e, ES2_ERR_COUNT, 0);
        for (size_t i = 0; i < count; i++) {
            es2_str_string string;
            if ((s = es2_str_string_at(&str, group, i, &string, e)) != ES2_OK) return s;
            if (string.text.size + 1 > (group ? SLOT_TEXT : SLOT_FILE))
                return es2_fail(e, ES2_ERR_LENGTH, (size_t)(string.text.data - input.data) - 2);
        }
    }
    return ES2_OK;
}

/* Checks one file. Returns ES2_OK, ES2T_NAME for a name that is not a save file, or the
 * parser's es2_status; *offset is the file offset the parser names (0 without one). */
int es2t_check(const char *name, const uint8_t *data, size_t size, uint32_t *offset)
{
    es2_span input = {data, size};
    es2_error e = {ES2_OK, 0, NULL};
    es2_status s;
    if (offset) *offset = 0;
    if (!data && size) return ES2_ERR_ARGUMENT;
    switch (es2t_kind(name)) {
    case ES2T_GAME_SAV: {
        es2_sav sav;
        s = es2_sav_parse(input, &sav, &e);
        break;
    }
    case ES2T_SCRIPT: {
        /* A stale tail after the last list passes, as for GAME_<n>.SAV. */
        es2_script script;
        s = es2_script_parse(input, &script, &e);
        break;
    }
    case ES2T_MISSION: {
        /* The readers of data\mission.str ask for one group without the aux pointer. */
        es2_str str;
        s = es2_str_parse(input, 1, ES2_STR_TAG_BYTE, &str, &e);
        break;
    }
    case ES2T_PLAYER: {
        es2_mec mec;
        s = es2_mec_parse(input, &mec, &e);
        break;
    }
    case ES2T_GAMEFILE:
        s = check_gamefile(input, &e);
        break;
    default:
        return ES2T_NAME;
    }
    if (s != ES2_OK && offset) *offset = (uint32_t)e.offset;
    return (int)s;
}
