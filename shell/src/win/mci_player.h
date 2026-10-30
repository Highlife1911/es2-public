#ifndef ES2_SHELL_WIN_MCI_PLAYER_H
#define ES2_SHELL_WIN_MCI_PLAYER_H

/* The own MCI movie player, VSHELL's 0x41e51b (open the avivideo
 * device), 0x41e640 (open, place and play one movie, wait), 0x41e57c and
 * 0x41e5bb (close) with the same MCI command strings, in the caller's main
 * window. Before the first "open avivideo" the Indeo 3.2 decoder is bound in
 * this process (//media:iv32_bind). It carries out the player
 * actions of es2_shell_movies_play that touch MCI or the message queue
 * (es2_mci_player_call); everything else is the caller's.
 * Contract and deviations: docs/reverse-engineering/vshell_ui.md section 19. */
#include "es2/shell/movies.h"

#include <windows.h>

/* A rectangle in the parent window's client pixels. */
typedef struct es2_mci_rect { int x, y, width, height; } es2_mci_rect;

typedef struct es2_mci_player {
    HWND parent;           /* the main window ([0x481f38]): movie parent and MCI notify window */
    const char *cd_root;   /* CD root with its trailing backslash ([0x4823b9]) */
    int language;          /* 0 English, 1 French, 2 German (0x48235e) */
    /* Entries 10..245 of the screen palette as PALETTEENTRY bytes (r, g, b,
     * flags), for the palette 0x41e48c builds when the player runs with
     * build_palette = 1; NULL: none is built (handle 0, as before VSHELL
     * built one). */
    const BYTE (*palette)[4];
    /* Where the caller shows the 640 x 480 frame in the parent's
     * client area (client pixels), asked before every movie; NULL or a
     * return of 0: (0, 0, 640, 480), VSHELL's unscaled window. Platform
     * deviation for a scaled presenter (vshell_ui.md section 19.5). */
    int (*frame)(void *context, es2_mci_rect *out);
    /* CD prompt (hook point 0x41edda): 1 = Retry, 0 = quit. NULL:
     * no prompt, the sequence ends as after Quit. */
    int (*cd_prompt)(void *context, const es2_shell_movie_record *record);
    /* Intro movie that does not open (0x41ed88: error.str message 5, then
     * quit). NULL: nothing shown. */
    void (*intro_error)(void *context, const es2_shell_movie_record *record);
    void *context;
    /* State */
    int quit;              /* WM_QUIT seen while the player pumped */
    int movie_open;        /* [0x470eb0] */
    int iv32_status;       /* es2_iv32_status of the binding, 0 before the first open */
    HPALETTE palette_handle;   /* [0x485744] */
    char error[256];       /* last MCI error text */
} es2_mci_player;

/* Carries out OPEN_DEVICE, PLAY, CLOSE_DEVICE, CD_PROMPT, INTRO_ERROR and
 * PUMP; returns what es2_shell_movies_play expects, 0 for other actions.
 * Never called by a test: it opens windows and plays sound. */
int32_t es2_mci_player_call(es2_mci_player *player, uint32_t action, int32_t arg,
    const es2_shell_movie_record *record);

/* The full path of a record's movie: CD root + the table path with the
 * drive letter of the table entry dropped (0x40d9e1). Returns 0, or -1 for
 * an index outside the table or a too small buffer. Portable; tested. */
int es2_mci_player_path(const char *cd_root, int index, int language, char *out, size_t capacity);

/* The MoveWindow rectangle of a movie rectangle (frame coordinates,
 * 0x41e5fa) when the 640 x 480 frame is shown at `frame` (NULL or an empty
 * frame: VSHELL's (0, 0, 640, 480), the rectangle unchanged). Both edges are
 * mapped (x0 = frame.x + floor(x * frame.width / 640), x1 likewise for
 * x + width), so an integer scale k gives x * k + frame.x and width * k.
 * Portable; tested. */
es2_mci_rect es2_mci_player_place(const es2_mci_rect *frame, const es2_shell_movie_rect *rect);

#endif
