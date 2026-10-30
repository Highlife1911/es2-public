/* The portable parts of the own MCI player: the movie path of
 * 0x40d9e1 and the hooks without a window. Opens no window, sends no MCI
 * command, plays nothing. */
#include "mci_player.h"

#include <stdio.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static int prompts, errors;
static int prompt(void *context, const es2_shell_movie_record *r) { (void)context; (void)r; prompts++; return 1; }
static void intro_error(void *context, const es2_shell_movie_record *r) { (void)context; (void)r; errors++; }

int main(void) {
    char path[64];
    es2_mci_player p;
    es2_shell_movie_record record = {0x46, {0, 0, 0, 0}, -1, 0, 0};
    CHECK(es2_mci_player_path("D:\\", 0x46, 0, path, sizeof path) == 0 && !strcmp(path, "D:\\avi\\c1.avi"));
    CHECK(es2_mci_player_path("D:\\", 0x44, 2, path, sizeof path) == 0 && !strcmp(path, "D:\\avg\\intr_pt1.avi"));
    CHECK(es2_mci_player_path("", 0x55, 0, path, sizeof path) == 0 && !strcmp(path, "avi\\dropship.avi"));
    CHECK(es2_mci_player_path("D:\\", 86, 0, path, sizeof path) == -1);
    CHECK(es2_mci_player_path(NULL, 0, 0, path, sizeof path) == -1);
    CHECK(es2_mci_player_path("D:\\", 0, 0, path, 14) == -1);   /* "D:\avi\pt1.avi" needs 15 */
    CHECK(es2_mci_player_path("D:\\", 0, 0, path, 15) == 0);
    memset(&p, 0, sizeof p);
    /* without a CD prompt hook: the sequence ends as after Quit */
    CHECK(es2_mci_player_call(&p, ES2_SHELL_MOVIE_CD_PROMPT, 0, &record) == 0 && p.quit == 1);
    memset(&p, 0, sizeof p);
    p.cd_prompt = prompt;
    CHECK(es2_mci_player_call(&p, ES2_SHELL_MOVIE_CD_PROMPT, 0, &record) == 1 && prompts == 1 && !p.quit);
    p.intro_error = intro_error;
    CHECK(es2_mci_player_call(&p, ES2_SHELL_MOVIE_INTRO_ERROR, 0, &record) == 0 && errors == 1 && p.quit);
    /* screen actions are the caller's */
    CHECK(es2_mci_player_call(&p, ES2_SHELL_MOVIE_PALETTE, 3, NULL) == 0);
    CHECK(es2_mci_player_call(NULL, ES2_SHELL_MOVIE_PLAY, 0, &record) == 0);
    /* a record outside the table never reaches MCI */
    record.index = 200;
    CHECK(es2_mci_player_call(&p, ES2_SHELL_MOVIE_PLAY, 0, &record) == ES2_SHELL_MOVIE_FAILED);
    CHECK(es2_mci_player_call(&p, ES2_SHELL_MOVIE_PLAY, 0, NULL) == ES2_SHELL_MOVIE_FAILED);
    /* The movie window follows the presenter's frame rectangle */
    {
        es2_mci_rect at, frame;
        const es2_shell_movie_rect full = es2_shell_movie_rect_full, panel = es2_shell_movie_rect_panel,
                                   theatre = es2_shell_movie_rect_theatre;
#define RECT_IS(r, X, Y, W, H) ((r).x == (X) && (r).y == (Y) && (r).width == (W) && (r).height == (H))
        /* VSHELL's window: no frame, an empty frame, the frame at (0, 0, 640, 480) */
        at = es2_mci_player_place(NULL, &full);
        CHECK(RECT_IS(at, 0x20, 0x3c, 0x240, 0x168));
        frame = (es2_mci_rect){0, 0, 0, 0};
        at = es2_mci_player_place(&frame, &panel);
        CHECK(RECT_IS(at, 0x15, 0x56, 0xef, 0xb3));
        frame = (es2_mci_rect){0, 0, 640, 480};
        at = es2_mci_player_place(&frame, &theatre);
        CHECK(RECT_IS(at, 0x122, 0x43, 0x127, 0xe2));
        /* full screen 1920 x 1080: scale 2, the frame at (320, 60) */
        frame = (es2_mci_rect){320, 60, 1280, 960};
        at = es2_mci_player_place(&frame, &full);
        CHECK(RECT_IS(at, 320 + 0x40, 60 + 0x78, 0x480, 0x2d0));
        at = es2_mci_player_place(&frame, &panel);
        CHECK(RECT_IS(at, 320 + 0x2a, 60 + 0xac, 0x1de, 0x166));
        /* 2560 x 1440: scale 3, the frame at (320, 0) */
        frame = (es2_mci_rect){320, 0, 1920, 1440};
        at = es2_mci_player_place(&frame, &theatre);
        CHECK(RECT_IS(at, 320 + 3 * 0x122, 3 * 0x43, 3 * 0x127, 3 * 0xe2));
        /* a window smaller than the frame (400 x 300 at (0, 0)): the edges
         * are scaled, so the panel keeps its right and bottom edges */
        frame = (es2_mci_rect){0, 0, 400, 300};
        at = es2_mci_player_place(&frame, &full);
        CHECK(RECT_IS(at, 20, 37, 360, 225));   /* 32*5/8, 60*5/8, (32+576)*5/8 - 20, (60+360)*5/8 - 37 */
        at = es2_mci_player_place(&frame, &panel);
        CHECK(RECT_IS(at, 13, 53, 149, 112));   /* 21*5/8=13, 86*5/8=53, 260*5/8=162, 265*5/8=165 */
        /* a frame that is not a multiple of 640 x 480 (odd sizes round the edges down) */
        frame = (es2_mci_rect){7, 3, 641, 481};
        at = es2_mci_player_place(&frame, &full);
        CHECK(at.x == 7 + 32 && at.y == 3 + 60 && at.x + at.width == 7 + 608 * 641 / 640 &&
              at.y + at.height == 3 + 420 * 481 / 480);
        /* negative frame origin (a window narrower than the picture's offset) */
        frame = (es2_mci_rect){-10, -5, 640, 480};
        at = es2_mci_player_place(&frame, &full);
        CHECK(RECT_IS(at, 0x20 - 10, 0x3c - 5, 0x240, 0x168));
        at = es2_mci_player_place(&frame, NULL);
        CHECK(RECT_IS(at, 0, 0, 0, 0));
#undef RECT_IS
    }
    printf("%u checks, %u failures\n", checks, failures);
    return failures != 0;
}
