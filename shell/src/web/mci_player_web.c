/* The own movie player's web host (mci_player.h; docs/web_build.md section
 * 11): the page plays the movie in a video element at the placed
 * rectangle, and the wait loop of 0x41e640 runs over the input ring. As in
 * mci_player.c every entry is dispatched to the window (the shell's pump
 * then drops the buttons and empties the key ring, app.c's movie_pump);
 * the loop ends on the page's end notification, a left or right button
 * down, or Esc or Space down. */
#include "mci_player.h"
#include "web/sound_web.h"
#include "web/web_imports.h"
#include "web/window_web.h"

#define MCI_NOTIFY_SUCCESSFUL 1
#define VK_ESCAPE 0x1b
#define VK_SPACE 0x20

static void pump(es2_mci_player *p) {
    if (es2_web_window_take_all()) p->quit = 1;
}

static int32_t play(es2_mci_player *p, const es2_shell_movie_record *r) {
    char path[600];
    int done = 0, skipped = 0;
    if (!r || es2_mci_player_path(p->cd_root, r->index, p->language, path, sizeof path) != 0)
        return ES2_SHELL_MOVIE_FAILED;
    es2_mci_rect frame = {0, 0, 0, 0}, at;
    if (!p->frame || !p->frame(p->context, &frame)) frame.width = frame.height = 0;
    at = es2_mci_player_place(&frame, &r->rect);
    if (!es2_web_movie_play(path, at.x, at.y, at.width, at.height)) return ES2_SHELL_MOVIE_FAILED;
    p->movie_open = 1;   /* 0x41e786 [0x470eb0] */
    while (!done) {
        es2_web_input e;
        if (es2_web_input_take(&e, 1) != 1) {
            es2_web_sound_wait(100);
            continue;
        }
        if (e.type == ES2_WEB_INPUT_QUIT) { p->quit = 1; break; }
        if (e.type == ES2_WEB_INPUT_MCI && e.a == MCI_NOTIFY_SUCCESSFUL) done = 1;   /* 0x4057f2 */
        /* 0x405145, 0x4050e0: VSHELL holds the capture, so every click
         * reaches its main window */
        if (e.type == ES2_WEB_INPUT_BUTTON && e.d && (e.c == 0 || e.c == 1)) done = skipped = 1;
        /* 0x41e7b8..0x41e7e2: key ring codes 1 (Esc) and 0x39 (Space), key down */
        if (e.type == ES2_WEB_INPUT_KEY && !(e.c & ES2_WEB_KEY_UP) && (e.b == VK_ESCAPE || e.b == VK_SPACE))
            done = skipped = 1;
        es2_web_window_dispatch(&e);
    }
    es2_web_movie_stop();   /* 0x41e57c */
    p->movie_open = 0;      /* 0x41e80e */
    return skipped ? ES2_SHELL_MOVIE_SKIPPED : ES2_SHELL_MOVIE_ENDED;
}

int32_t es2_mci_player_call(es2_mci_player *p, uint32_t action, int32_t arg, const es2_shell_movie_record *r) {
    if (!p) return 0;
    (void)arg;
    switch (action) {
    case ES2_SHELL_MOVIE_OPEN_DEVICE: return 1;   /* no device and no Indeo binding: the page decodes */
    case ES2_SHELL_MOVIE_PLAY: return play(p, r);
    case ES2_SHELL_MOVIE_CLOSE_DEVICE: return 0;
    case ES2_SHELL_MOVIE_PUMP: pump(p); return 0;
    case ES2_SHELL_MOVIE_CD_PROMPT:
        if (p->cd_prompt) return p->cd_prompt(p->context, r);
        p->quit = 1;
        return 0;
    case ES2_SHELL_MOVIE_INTRO_ERROR:
        if (p->intro_error) p->intro_error(p->context, r);
        p->quit = 1;
        return 0;
    default: return 0;
    }
}
