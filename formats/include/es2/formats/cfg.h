#ifndef ES2_FORMATS_CFG_H
#define ES2_FORMATS_CFG_H
#include "es2/formats/common.h"

/* Configuration files of the game: data\prefs.cfg (binary), data\keyjoy.cfg
 * and data\sound.cfg (INI files read with GetPrivateProfileStringA).
 * Layouts, readers and writers: docs/formats/cfg.md. All views borrow
 * caller-owned input; no allocation or I/O. Outputs are unchanged on error. */

/* ---- prefs.cfg ------------------------------------------------------------
 * A flat block of 54 bytes shared by VSHELL (block at 0x48259c) and DBSIM
 * (block at 0x4d1f38). Both programs load it with the same rule: the block is
 * zeroed, then up to 54 bytes of the file are read; a missing or short file
 * is not an error. Only the offsets below are named; all other bytes are
 * preserved and unnamed (docs/formats/cfg.md has the evidence per byte). */
#define ES2_PREFS_SIZE 54u

enum es2_prefs_index {
    ES2_PREFS_AUDIO_A = 0x00,   /* two-state audio switches; DBSIM callbacks 0x459cb4 / 0x459c88 */
    ES2_PREFS_AUDIO_B = 0x01,
    ES2_PREFS_VOICE = 0x02,     /* three-state; DBSIM callback 0x459ce0: voice enabled when nonzero */
    ES2_PREFS_RESOLUTION = 0x04,/* DBSIM 0x45e510: 1 = 320x240 (video mode 0), any other value = 640x480 (mode 3) */
    ES2_PREFS_DISPLAY = 0x06,   /* window (0) / full screen (1); DBSIM rewrites it at exit */
    ES2_PREFS_LEVER = 0x0e,     /* DBSIM 0x45feb8 / 0x460870 / 0x4483a0: compared with 1 */
    ES2_PREFS_CONTROLS_A = 0x0d,/* 12 control bytes, used when DBSIM's selector (s) is 13 */
    ES2_PREFS_CONTROLS_B = 0x19,/* 12 control bytes, used when s is 25 */
    ES2_PREFS_PRACTICE_AMMO = 0x25,
    ES2_PREFS_PRACTICE_DAMAGE = 0x26,
    ES2_PREFS_PRACTICE_DIFFICULTY = 0x27,
    ES2_PREFS_PRACTICE_HERC = 0x28,
    ES2_PREFS_PRACTICE_TIME = 0x29,
    ES2_PREFS_SHELL_MODE = 0x2a,  /* 0 single missions, 1 campaign */
    ES2_PREFS_REPAIR = 0x2c,      /* 0 auto all, 1 manual (own Herc), 2 manual all */
    ES2_PREFS_BUILD = 0x2d,       /* 0 auto, 1 manual */
    ES2_PREFS_INSTANT_ROTATION = 0x2e /* instant action rotation, modulo 3 */
};

typedef struct es2_prefs { uint8_t byte[ES2_PREFS_SIZE]; } es2_prefs;

/* The start-up load of both programs. `file` is the whole file or an empty
 * span (no file); bytes beyond 54 are ignored. Always ES2_OK for valid args. */
es2_status es2_prefs_load(es2_span file, es2_prefs *out, es2_error *error);
/* The 54 bytes written by the whole-block writers (VSHELL 0x40dd0a, DBSIM
 * write of the block) into a truncating stream. */
void es2_prefs_serialize(const es2_prefs *prefs, uint8_t out[ES2_PREFS_SIZE]);
/* Read-modify-write of single bytes (VSHELL 0x40dfa3, DBSIM 0x459b94): the
 * file image is read into a 54-byte buffer, the listed indices are replaced
 * by the current block and all 54 bytes are written. DBSIM's copy leaves
 * bytes beyond a short file's end uninitialised; here they are zero.
 * VSHELL's 0x40dfa3 starts from a copy of the current block instead (the
 * own menu uses es2_shell_prefs_store, vshell_ui.md section 11).
 * Indices >= 54 give ES2_ERR_OFFSET. */
es2_status es2_prefs_overlay(es2_span file, const es2_prefs *current,
                             const uint8_t *indices, size_t count,
                             uint8_t out[ES2_PREFS_SIZE], es2_error *error);

/* The early video read of DBSIM (0x45e510): 7 bytes of the file, byte 4 and
 * byte 6. A file shorter than 7 bytes is ES2_ERR_TRUNCATED (the caller keeps
 * its default, as the original does when the 7-byte read fails). */
typedef struct es2_prefs_video {
    uint8_t resolution; /* file byte 4 */
    uint8_t display;    /* file byte 6 */
} es2_prefs_video;
es2_status es2_prefs_video_read(es2_span file, es2_prefs_video *out, es2_error *error);
/* Video mode DBSIM derives from byte 4: 0 (320x240) when it is 1, else 3 (640x480). */
int es2_prefs_video_mode(uint8_t resolution_byte);

/* ---- INI files -----------------------------------------------------------
 * Subset of GetPrivateProfileStringA: sections "[name]", "key = value"
 * lines, ';' comment lines, ASCII case-insensitive names, first match wins,
 * value trimmed of blanks and of one pair of matching quotes, result cut to
 * capacity - 1 characters. Line ends: LF or CR LF. Not claimed: behavior for
 * names the game never asks for. found is set to 1 when the key exists
 * (even with an empty value), else the output is the empty string. */
es2_status es2_cfg_ini_get(es2_span file, const char *section, const char *key,
                           char *out, size_t capacity, int *found, es2_error *error);

/* ---- sound.cfg ------------------------------------------------------------
 * Section [Sound]; keys Driver, Buffers, Rate, Width. Both executables read
 * the file at start-up with a 40-byte value buffer and defaults of "" (the
 * file is optional). */
typedef enum es2_cfg_reader {
    ES2_CFG_READER_DBSIM = 0,  /* sound.cfg reader 0x46379c */
    ES2_CFG_READER_VSHELL = 1  /* 0x42c591: reads Driver but always selects MME */
} es2_cfg_reader;
enum { ES2_SOUND_DRIVER_MME = 1, ES2_SOUND_DRIVER_DIRECTSOUND = 2 };
typedef struct es2_cfg_sound {
    uint16_t driver;    /* ES2_SOUND_DRIVER_*; DirectSound only if Driver equals it (DBSIM) */
    uint16_t buffers;   /* Buffers when 1..64, else 5 */
    uint16_t rate_code; /* 0x10 when Rate is 11, else 0x20 (22 is the shipped value) */
    uint16_t width_code;/* 4 when Width equals "Mono", else 8 */
} es2_cfg_sound;
es2_status es2_cfg_sound_parse(es2_span file, es2_cfg_reader reader,
                               es2_cfg_sound *out, es2_error *error);

/* ---- keyjoy.cfg -----------------------------------------------------------
 * Section [Keyjoy]; keys Tilt, Backturn, Missile, Rudder. DBSIM only
 * (0x45b7a8). Each flag is 1 when the value equals "Reverse" (ASCII
 * case-insensitive), else 0. DBSIM applies them as sign inversions of the
 * turret tilt, the back-turn direction, the missile steering and the rudder
 * axis. */
typedef struct es2_cfg_keyjoy {
    uint8_t tilt, backturn, missile, rudder;
} es2_cfg_keyjoy;
es2_status es2_cfg_keyjoy_parse(es2_span file, es2_cfg_keyjoy *out, es2_error *error);

/* ---- tape files (DBSIM -r / -p / -D) -------------------------------------
 * docs/formats/cfg.md, "Tape files". A header of ES2_TAPE_MEMBERS records,
 * one per embedded launch file in es2_tape_member_name order: a u32 length
 * and that many bytes (DBSIM copies them in pieces of at most
 * ES2_TAPE_PIECE bytes, 45cca4 / 45cd5c; the pieces do not show in the
 * file). Then one entry per control_record call (45a810): the first entry
 * starts with ES2_TAPE_PREFIX bytes of the joystick block; a 16-byte record
 * (key word, four axis words, the time-scale word, a dword of button bits);
 * u32 N and u32 M; N 14-byte mouse events; M key words. */
#define ES2_TAPE_MEMBERS 7u
#define ES2_TAPE_PIECE 500u
#define ES2_TAPE_PREFIX 8u
#define ES2_TAPE_RECORD 16u
#define ES2_TAPE_EVENT 14u
/* The member's file name (script.dat, player.mec, mission.var, prefs.cfg,
 * restore.dat, object.str, keyjoy.cfg); NULL for an index out of range.
 * Recording reads data\<name>; playback writes data\<name>, except the
 * members for which es2_tape_member_in_tapes is 1 (prefs.cfg, keyjoy.cfg),
 * which go to tapes\<name>. */
const char *es2_tape_member_name(unsigned index);
int es2_tape_member_in_tapes(unsigned index);
typedef struct es2_tape_header {
    es2_span member[ES2_TAPE_MEMBERS]; /* views into the file */
    size_t size;                       /* header bytes = offset of the first entry */
} es2_tape_header;
es2_status es2_tape_header_parse(es2_span file, es2_tape_header *out, es2_error *error);
typedef struct es2_tape_entry {
    es2_span prefix;                   /* ES2_TAPE_PREFIX bytes in the first entry, else empty */
    uint16_t key, axis[4], time_scale;
    uint32_t bits;                     /* 0..7 the first button set, 8..15 the second, 16 the mouse button */
    uint32_t event_count, key_count;
    es2_span events;                   /* event_count * ES2_TAPE_EVENT bytes */
    es2_span keys;                     /* key_count key words */
    size_t size;                       /* bytes of this entry */
} es2_tape_entry;
/* The entry at offset; first selects the entry with the prefix (the one at
 * the header's end). Counts beyond the remaining bytes give
 * ES2_ERR_TRUNCATED. */
es2_status es2_tape_entry_parse(es2_span file, size_t offset, int first,
                                es2_tape_entry *out, es2_error *error);

#endif
