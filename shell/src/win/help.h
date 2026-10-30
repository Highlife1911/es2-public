#ifndef ES2_SHELL_WIN_HELP_H
#define ES2_SHELL_WIN_HELP_H

/* The online manual of the own shell (ONLINE MANUAL, the bay's online
 * image).
 *
 * VSHELL's help routine 0x431e79 reads the first byte of data\language.cfg
 * (0x408d69), joins the CD root with english\es2guide.hlp, french\... or
 * german\... (E, F, G; 0x431eac..0x431ee6, 0x40d9e1) and hands the path to
 * WinHelpA (0x407953, command 3). Current Windows has no WinHelp viewer by
 * default, so the own shell opens the same file through the program that is
 * registered for .hlp files (a platform deviation, vshell_ui.md section 21),
 * and says where the manual is when that is not possible. The policy is
 * open (default), text or hide. A language.cfg that is
 * missing or holds another byte than E, F, G is VSHELL's severity-4
 * assertion (the message box text and exit code 1): es2_help_assertion gives
 * its text, the host reports it and ends the process.
 *
 * es2_help_show does the decisions over three injected operations, so tests
 * never start a viewer; es2_help_os_* are the platform's operations. */
#include <stddef.h>

typedef enum es2_help_policy {
    ES2_HELP_POLICY_OPEN = 0,  /* open the file through its registered handler (default) */
    ES2_HELP_POLICY_TEXT,      /* only say where the file is */
    ES2_HELP_POLICY_HIDE       /* the presenter does not offer the entry */
} es2_help_policy;

/* menu.ini online_manual, in the style of cd_check: NULL, "" or "open"
 * -> OPEN, "text" -> TEXT, "hide" -> HIDE (lower case, exactly), otherwise
 * -1. */
int es2_help_policy_parse(const char *value);

/* The path below the CD root for a language byte: 'E', 'F', 'G' (exactly
 * those, as VSHELL compares); NULL for any other byte. */
const char *es2_help_relative(int letter);

/* CD root + relative path (0x40d9e1 is a plain concatenation; VSHELL's root
 * ends in a backslash, a root without one gets it; an empty root gives the
 * relative path). 1 on success, 0 for an unknown letter or a path that does
 * not fit. */
int es2_help_path(char *out, size_t capacity, const char *cd_root, int letter);

typedef enum es2_help_outcome {
    ES2_HELP_HIDDEN = 0,    /* policy hide: nothing done */
    ES2_HELP_OPENED,        /* the handler was started for path */
    ES2_HELP_TEXT_ONLY,     /* policy text: path says where it is, the file exists */
    ES2_HELP_NO_FILE,       /* data\language.cfg does not open: assertion 0x408d90 (VSHELL ends, exit 1) */
    ES2_HELP_BAD_LANGUAGE,  /* the byte is not E, F or G: assertion 0x431efc (VSHELL ends, exit 1) */
    ES2_HELP_TOO_LONG,      /* the CD root is too long for a path (empty path) */
    ES2_HELP_MISSING,       /* the file path does not exist (VSHELL's WinHelp would fail) */
    ES2_HELP_NO_VIEWER      /* no program is registered for the file (or it did not start) */
} es2_help_outcome;

typedef struct es2_help_ops {
    void *ctx;
    /* The first byte of data\language.cfg, or -1 when the file does not open
     * (an empty file gives 0: VSHELL reads an unset byte there). */
    int (*language)(void *ctx);
    /* 1 when the file exists. */
    int (*exists)(void *ctx, const char *path);
    /* 1 when the registered handler was started for the file. */
    int (*open)(void *ctx, const char *path);
} es2_help_ops;

#define ES2_HELP_PATH 260
typedef struct es2_help_result {
    es2_help_outcome outcome;
    int letter;                 /* the language byte read, -1 when none */
    char path[ES2_HELP_PATH];   /* empty for HIDDEN, NO_FILE, BAD_LANGUAGE and TOO_LONG */
} es2_help_result;

void es2_help_show(const es2_help_ops *ops, es2_help_policy policy, const char *cd_root, es2_help_result *out);

/* The text VSHELL's assertion report shows for NO_FILE and BAD_LANGUAGE
 * (0x44e6a8: Assert::err: Line N in "file", then ": text"); 0 and an empty
 * string for the other outcomes. The report is a MessageBoxA with the
 * caption "Error" (0x44e847) and the same line appended to assert.log
 * (0x44e764; the first one of a process after a line of 78 dashes), then
 * the process ends with exit code 1 (0x44e5b0). */
int es2_help_assertion(es2_help_outcome outcome, char *out, size_t capacity);
/* The bytes written before the first assertion line of a process: 78
 * dashes and CR LF (0x47a59f, 0x44e7aa). */
#define ES2_HELP_LOG_RULE 80

/* The platform's operations, ctx unused: on Windows (help_win.c)
 * GetFileAttributes and ShellExecuteEx with the verb "open" and no error
 * dialogs; in the web build (shell/src/web/help_web.c) the file's
 * attributes and no viewer (open returns 0). */
int es2_help_os_exists(void *ctx, const char *path);
int es2_help_os_open(void *ctx, const char *path);

#endif
