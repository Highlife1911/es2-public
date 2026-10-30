#ifndef ES2_SHELL_MOVIES_H
#define ES2_SHELL_MOVIES_H

/* VSHELL's movie queue, the selection rules and the player's control
 * flow (vshell_ui.md section 19). Portable C17: no Win32, no file I/O. The
 * MCI playback itself is shell/src/win/mci_player.c; the player's screen
 * effects (palettes, panels, music) are calls the caller carries out.
 *
 * The state is what VSHELL keeps in globals: the movie flag 0x482359, the
 * ring of ten 28-byte records at 0x485748 with its write index 0x470fac and
 * play index 0x470fae, the once-flags 0x477946..0x477948 and the player's
 * flags 0x470fb0, 0x471120, 0x471124. */
#include <stddef.h>
#include <stdint.h>

/* ---- The 86 names of the pointer table 0x470fb4 ------------------------ */
enum {
    ES2_SHELL_MOVIE_NAMES = 86,
    ES2_SHELL_MOVIE_INTRO_1 = 0x44,   /* avi\intr_pt1.avi (language directory) */
    ES2_SHELL_MOVIE_INTRO_2 = 0x45,   /* avi\intr_pt2.avi */
    ES2_SHELL_MOVIE_GROUP = 0x46,     /* avi\c1.avi .. c5.avi: 0x46 + group - 1 */
    ES2_SHELL_MOVIE_THEATRE = 0x4b,   /* alph_th, delt_th, omic_th, brav_th, luna: 0x4b + group - 1 */
    ES2_SHELL_MOVIE_VICTORY = 0x53,
    ES2_SHELL_MOVIE_CREDITS = 0x54,
    ES2_SHELL_MOVIE_DROPSHIP = 0x55
};
/* The table entry ("avi\pt1.avi" ...) as VSHELL stores it; NULL outside
 * 0..85 (VSHELL reads past the table there: refused). */
const char *es2_shell_movie_name(int index);
/* The CD-relative path the player opens: the table entry, with the third
 * character of the two intro names patched by the first player call
 * (0x41e9ac..0x41e9d1) to 'i' (language 0, English), 'f' (1) or 'g' (2):
 * avi\, avf\ or avg\intr_pt1.avi. Language as the word 0x48235e (other
 * values: 'i'). Returns 0, or -1 for an index outside 0..85 or a too small
 * buffer. The player prefixes the CD root (0x40d9e1). */
int es2_shell_movie_path(int index, int language, char *out, size_t capacity);

/* ---- Queue ---------------------------------------------------------------- */
/* The record's rectangle as the four dwords +0x02..+0x11; the player hands
 * them to MoveWindow as x, y, width, height of the movie window in the main
 * window's client area (0x41e5fa). */
typedef struct es2_shell_movie_rect {
    int32_t x, y, width, height;
} es2_shell_movie_rect;
/* The three rectangles of the enqueue sites (after the sites' min/max
 * normalisation): intro, credits, victory and dropship 0x20,0x3c,0x240,0x168;
 * group, briefing and debrief movies 0x15,0x56,0xef,0xb3 (0x444882); the
 * theatre movie 0x122,0x43,0x127,0xe2 (0x4448e6). */
extern const es2_shell_movie_rect es2_shell_movie_rect_full, es2_shell_movie_rect_panel,
    es2_shell_movie_rect_theatre;

typedef struct es2_shell_movie_record {
    int16_t index;              /* +0x00, -1 = empty */
    es2_shell_movie_rect rect;  /* +0x02 */
    int16_t palette;            /* +0x12, -1 = none */
    uint32_t sequence;          /* +0x14, nonzero: mission teardown and theatre after the movie */
    uint32_t callback;          /* +0x18, called after the movie when nonzero (every site passes 0) */
} es2_shell_movie_record;

enum { ES2_SHELL_MOVIE_RING = 10, ES2_SHELL_MOVIE_RECORD_SIZE = 28 };

typedef struct es2_shell_movies {
    uint8_t enabled;            /* 0x482359: 1 by default (0x4079d5), 0 with -a */
    int16_t write, play;        /* 0x470fac, 0x470fae */
    es2_shell_movie_record ring[ES2_SHELL_MOVIE_RING]; /* 0x485748 */
    es2_shell_movie_rect empty; /* 0x47110c: the rectangle of an empty record (zero) */
    uint8_t group_shown;        /* 0x477946 */
    uint8_t briefing_shown;     /* 0x477947 */
    uint8_t debrief_shown;      /* 0x477948 */
    uint32_t active;            /* 0x470fb0: a movie sequence runs */
    int *gate;                  /* own: the toolkit's copy of 0x470fb0 (es2_ui.gate, the buttons'
                                 * test 0x409d8a/0x40a2c4), written with `active`; NULL = none */
    uint32_t intro;             /* 0x471124: the current record is an intro movie */
    uint32_t intro_skipped;     /* 0x471120: the intro was skipped (never cleared) */
} es2_shell_movies;

/* Process start: the static initialiser 0x41f11e (every record empty with
 * the zero rectangle, palette -1), indices and flags 0; `enabled` as the
 * movie flag (1, or 0 for VSHELL's -a). */
void es2_shell_movies_init(es2_shell_movies *m, int enabled);
/* 0x41e88f: 1 when a record (empty ones included, index -1) holds the low
 * word of `index`. */
int es2_shell_movie_queued(const es2_shell_movies *m, int32_t index);
/* 0x41e8c0(index, rect, palette, callback, sequence): nothing when the
 * movie flag is 0 or the low word is already queued (so -1 is never
 * queued while a record is empty); otherwise the record at the write
 * index is overwritten (no full-ring check) and the index advances
 * modulo 10. Returns 1 when stored. */
int es2_shell_movie_enqueue(es2_shell_movies *m, int32_t index, es2_shell_movie_rect rect, int32_t palette,
    uint32_t callback, uint32_t sequence);
/* The ring as the 280 bytes at 0x485748 (little-endian). */
void es2_shell_movies_image(const es2_shell_movies *m, uint8_t out[ES2_SHELL_MOVIE_RING * ES2_SHELL_MOVIE_RECORD_SIZE]);

/* ---- Selection rules -------------------------------------------------------- */
/* Start-up 0x40188a: entry mode (-X value, 0x482362) 3 and 4 run the results
 * path and 6 the main menu; every other value queues the two intro movies
 * (0x401955, 0x401983) and plays them with 0x41e98c(0). Returns 1 when the
 * intro is due (whether or not the flag let them into the queue). */
int es2_shell_movies_intro(es2_shell_movies *m, int16_t entry_mode);
/* CREDITS 0x431c7b: credits.avi (0x54), then 0x41e98c(0). */
void es2_shell_movies_credits(es2_shell_movies *m);
/* Results path, end of the campaign (0x40f554): victory.avi (0x53) and
 * credits.avi (0x54), then 0x41e98c(1). */
void es2_shell_movies_victory(es2_shell_movies *m);
/* The page MISSION (0x43ae49) opens: 0 while the group movies were not shown
 * (0x477946 = 0) and the CAREER position word 0x46fc5a is 0, else 1. */
int es2_shell_movies_mission_page(const es2_shell_movies *m, int16_t position);
/* Mission screen 0x444873(page) with the CAREER group word 0x46fc58:
 * page 0 while 0x477946 = 0: c<group>.avi (0x46 + group - 1, panel
 * rectangle, palette 3, or 4 when group - 1 >= 4) and the theatre movie
 * (0x4b + group - 1, theatre rectangle, palette -1, sequence 1), then
 * 0x477946 := 1; page 1 while 0x477947 = 0: the briefing word (0x48419c,
 * list-4 record +0x8e) with palette group - 1 + 5, then 0x477947 := 1;
 * page 4 while 0x477948 = 0: the debrief word (0x48419e, debrief record
 * +0x3e) with palette group - 1 + 10, then 0x477948 := 1. The flag is set
 * also when the queue refused the movie. Other pages queue nothing. Index
 * and palette arithmetic is 16-bit (the sites use the low word). */
void es2_shell_movies_mission(es2_shell_movies *m, int page, int16_t group, int16_t briefing_word,
    int16_t debrief_word);
/* 0x44472f, the theatre step the player runs after a sequence record:
 * group 5 (group - 1 = 4) queues dropship.avi (0x55, full rectangle) and
 * returns 1 (VSHELL then calls 0x43a430(2), music fade-out 0x42f7f0 and
 * focus lost 0x42f6a8); other groups return 0 (the theatre picture of the
 * table 0x478068 is shown, palette 0xf + group - 1). */
int es2_shell_movies_theatre(es2_shell_movies *m, int16_t group);
/* Once-flag writers: a new career (ACCEPT 0x43c7a5 clears 0x477946, the new
 * game 0x40e8a5 clears 0x477947/0x477948); a load 0x40eaaa (0x40ec06,
 * 0x40ec0d: 0x477947/0x477948; the results path's slot-10 load and
 * CONTINUE); RESTORE 0x438393 also clears 0x477946 (0x4383c6). */
void es2_shell_movies_new_career(es2_shell_movies *m);
void es2_shell_movies_load(es2_shell_movies *m, int restore);

/* ---- Player 0x41e98c ----------------------------------------------------------- */
/* What the player does, in its order, as VSHELL function or block
 * addresses; the caller carries them out (its screens, palettes, music,
 * MCI). `arg` is the call's argument (an object global for the panel calls). */
enum {
    ES2_SHELL_MOVIE_FADE_OUT = 0x42f7f0,      /* music fade-out */
    ES2_SHELL_MOVIE_FOCUS_LOST = 0x42f6a8,    /* sound focus lost (audio.md) */
    ES2_SHELL_MOVIE_PALETTE = 0x407b62,       /* load palette arg */
    ES2_SHELL_MOVIE_PALETTE_APPLY = 0x407c1b,
    ES2_SHELL_MOVIE_BAY_BUTTONS = 0x41eb83,   /* bay buttons 0x48d530..0x48d548 +0x45 := 0, 0x48d54c := 1, redraw */
    ES2_SHELL_MOVIE_SHOW = 0x41f93a,          /* show object arg */
    ES2_SHELL_MOVIE_HIDE = 0x41fabd,          /* hide object arg */
    ES2_SHELL_MOVIE_DRAIN = 0x402266,         /* draw what is pending (only with focus) */
    ES2_SHELL_MOVIE_REPAINT = 0x41f3ed,       /* repaint object arg */
    ES2_SHELL_MOVIE_TEXT_PANEL = 0x445343,    /* text panel of page arg */
    ES2_SHELL_MOVIE_BUSY = 0x408d2f,          /* busy cursor arg (1 on, 0 off) */
    ES2_SHELL_MOVIE_OPEN_DEVICE = 0x41e51b,   /* "open avivideo", arg: build the palette; returns 1 when open */
    ES2_SHELL_MOVIE_PLAY = 0x41e640,          /* play the current record; returns ES2_SHELL_MOVIE_ENDED/SKIPPED, 0 = open failed */
    ES2_SHELL_MOVIE_INTRO_ERROR = 0x41ed88,   /* intro failed: leave full screen, prefs +6 := 0, error.str 5, sound off */
    ES2_SHELL_MOVIE_CD_PROMPT = 0x41edda,     /* CD prompt [0x48d1e8] until Retry (returns 1) or quit (0) */
    ES2_SHELL_MOVIE_CLOSE_DEVICE = 0x41e5bb,  /* "close avivideo" */
    ES2_SHELL_MOVIE_CLEAR = 0x4077ee,         /* clear the frame */
    ES2_SHELL_MOVIE_PUMP = 0x406328,          /* Windows message pump */
    ES2_SHELL_MOVIE_BAY_SELECT = 0x43a430,    /* 0x43a430(arg) */
    ES2_SHELL_MOVIE_TEARDOWN = 0x445095,      /* hide the mission panels, free terradef */
    ES2_SHELL_MOVIE_MISSION_BUTTON = 0x41ef7a,/* 0x48d54c +0x45 := 0, redraw; screen word 0x4758b8 := -1 */
    ES2_SHELL_MOVIE_FOCUS_GAINED = 0x42f5d3,
    ES2_SHELL_MOVIE_FADE_IN = 0x42f894,
    ES2_SHELL_MOVIE_THEATRE_STEP = 0x44472f,  /* the caller runs es2_shell_movies_theatre */
    ES2_SHELL_MOVIE_WAIT = 0x4022c5,          /* busy wait arg seconds, no pumping */
    ES2_SHELL_MOVIE_THEATRE_HIDE = 0x44483e,  /* hide the theatre panel, free its picture */
    ES2_SHELL_MOVIE_CALLBACK = 0x41f01c       /* the record's +0x18 (arg) */
};
/* Player state the caller answers. */
enum {
    ES2_SHELL_MOVIE_Q_BAY_BAR = 1,   /* the bay bar [0x48d528] is visible (+0x11 bit 1 clear) */
    ES2_SHELL_MOVIE_Q_PANEL,         /* the theatre panel [0x48d8c0] is visible */
    ES2_SHELL_MOVIE_Q_FULLSCREEN,    /* full-screen preference 0x46d878 */
    ES2_SHELL_MOVIE_Q_PAGE,          /* mission-screen page word 0x481108 */
    ES2_SHELL_MOVIE_Q_FOCUS,         /* focus flag 0x46c288 */
    ES2_SHELL_MOVIE_Q_QUIT           /* quit flag 0x46c268 */
};
enum { ES2_SHELL_MOVIE_FAILED = 0, ES2_SHELL_MOVIE_ENDED = 1, ES2_SHELL_MOVIE_SKIPPED = 2 };
/* Objects named in REPAINT/SHOW/HIDE arguments (VSHELL globals). */
enum {
    ES2_SHELL_MOVIE_OBJ_ROOT = 0x481180, ES2_SHELL_MOVIE_OBJ_BAY = 0x48d520,
    ES2_SHELL_MOVIE_OBJ_TREES = 0x48d8e0, ES2_SHELL_MOVIE_OBJ_MAP = 0x48d8b4,
    ES2_SHELL_MOVIE_OBJ_TITLE = 0x48d8b8, ES2_SHELL_MOVIE_OBJ_PAGE = 0x48d8bc
};
typedef struct es2_shell_movie_hooks {
    void *context;
    /* An action; returns the value the player uses (OPEN_DEVICE, PLAY,
     * CD_PROMPT), 0 otherwise. For PLAY the record is `record`. */
    int32_t (*call)(void *context, uint32_t action, int32_t arg, const es2_shell_movie_record *record);
    int32_t (*query)(void *context, int what);
} es2_shell_movie_hooks;
/* 0x41e98c(build_palette): plays the queued records in order until an empty
 * one, then the final focus-gained/fade-in unless the last sequence record
 * ran the theatre step. Returns 1 when the quit flag ended it (intro
 * failure, CD prompt quit), else 0. Every record played or skipped is
 * emptied (rectangle `empty`, palette -1) and the play index advances. */
int es2_shell_movies_play(es2_shell_movies *m, int build_palette, const es2_shell_movie_hooks *hooks);

/* ---- MCI strings of 0x41e51b, 0x41e640, 0x41e57c, 0x41e5bb ------------------ */
/* The command texts (wsprintfA formats at 0x470ebd .. 0x470f89); each
 * returns the length, or -1 when it does not fit. */
int es2_shell_mci_open(char *out, size_t capacity, const char *path, long parent);   /* "open %s alias mov style child parent %d" */
int es2_shell_mci_palette(char *out, size_t capacity, long palette);                 /* "setvideo mov palette handle to %d" */
extern const char es2_shell_mci_open_device[], es2_shell_mci_window_handle[], es2_shell_mci_play[],
    es2_shell_mci_close[], es2_shell_mci_close_device[];

/* ---- the start-up CD check and the CD prompt (vshell_ui.md section 20)
 * Policy (menu.ini `cd_check`): ORIGINAL is VSHELL's behaviour and the
 * default; SKIP is for installations whose videos live elsewhere: no
 * start-up check, and a movie that does not open is skipped without the
 * prompt and without setting the quit flag. */
enum { ES2_SHELL_CD_ORIGINAL = 0, ES2_SHELL_CD_SKIP = 1 };
enum { ES2_SHELL_CD_ERROR_MESSAGE = 5 };   /* error.str message of the check */
/* NULL, "" or "original" -> ORIGINAL, "skip" -> SKIP, otherwise -1. */
int es2_shell_cd_policy_parse(const char *value);
/* The probe opens `path` and returns nonzero when it opened (VSHELL keeps
 * the stream open until the main loop ends, 0x40224e; that is the
 * caller's business). */
typedef int (*es2_shell_cd_probe)(void *context, const char *path);
/* Original: 0x401cac..0x401d28. Builds cd_root (with its trailing
 * backslash) + "avi\pt1.avi" into `path` (may be NULL) and probes it.
 * Returns 0 when the start-up goes on (opened, or policy SKIP: no probe),
 * ES2_SHELL_CD_ERROR_MESSAGE when VSHELL shows that message and quits with
 * exit code 0, -1 for a too small buffer or no probe. */
int es2_shell_cd_check(int policy, const char *cd_root, es2_shell_cd_probe probe, void *context,
    char *path, size_t capacity);
/* The box answers 1 for Continue (0x4324fa: hide [0x48d1e8], done flag
 * [0x473bcc] := 1) and 0 for Quit (0x432569: quit flag [0x46c268] := 1). */
typedef int (*es2_shell_cd_box)(void *context);
/* The decision at the hook ES2_SHELL_MOVIE_CD_PROMPT (0x41edda): returns 1
 * to retry the open, 0 to stop; sets *quit to 1 after Quit. Policy SKIP
 * returns 0 without asking and leaves *quit alone, so the player skips the
 * movie. No box (ORIGINAL): treated as Quit. */
int es2_shell_cd_prompt(int policy, es2_shell_cd_box box, void *context, int *quit);

#endif
