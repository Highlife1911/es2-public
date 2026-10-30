/* es2movie.exe, a playback check of the own movie player. Opens a window like
 * VSHELL's (640 x 480 client, "EarthSiege II"), runs one selection rule of
 * es2/shell/movies.h into the queue and plays it with the player 0x41e98c's
 * control flow (es2_shell_movies_play) and the own MCI player
 * (shell/src/win/mci_player.c, IV32 bound in this process). Every player action
 * is printed. No test starts it.
 *
 *   es2movie --cd-root DIR [--language E|F|G] EVENT
 *   EVENT: intro | credits | victory | mission:GROUP | briefing:GROUP:WORD |
 *          debrief:GROUP:WORD | movie:INDEX
 * mission:5 plays c5, luna and the dropship (theatre step). Esc, Space or a
 * mouse button skip a movie; an intro skip skips the second intro movie. */
#include "es2/shell/movies.h"
#include "mci_player.h"

#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static es2_shell_movies movies;
static es2_mci_player player;
static int16_t group = 1;

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_CLOSE) { PostQuitMessage(0); return 0; }
    if (message == WM_DESTROY) return 0;
    return DefWindowProcA(window, message, w, l);
}

static const char *action_name(uint32_t a) {
    switch (a) {
    case ES2_SHELL_MOVIE_FADE_OUT: return "music fade-out";
    case ES2_SHELL_MOVIE_FOCUS_LOST: return "sound focus lost";
    case ES2_SHELL_MOVIE_PALETTE: return "palette";
    case ES2_SHELL_MOVIE_PALETTE_APPLY: return "palette apply";
    case ES2_SHELL_MOVIE_BAY_BUTTONS: return "bay buttons";
    case ES2_SHELL_MOVIE_SHOW: return "show";
    case ES2_SHELL_MOVIE_HIDE: return "hide";
    case ES2_SHELL_MOVIE_DRAIN: return "drain";
    case ES2_SHELL_MOVIE_REPAINT: return "repaint";
    case ES2_SHELL_MOVIE_TEXT_PANEL: return "text panel";
    case ES2_SHELL_MOVIE_BUSY: return "busy cursor";
    case ES2_SHELL_MOVIE_OPEN_DEVICE: return "open avivideo";
    case ES2_SHELL_MOVIE_PLAY: return "play";
    case ES2_SHELL_MOVIE_INTRO_ERROR: return "intro error";
    case ES2_SHELL_MOVIE_CD_PROMPT: return "CD prompt";
    case ES2_SHELL_MOVIE_CLOSE_DEVICE: return "close avivideo";
    case ES2_SHELL_MOVIE_CLEAR: return "clear";
    case ES2_SHELL_MOVIE_PUMP: return "pump";
    case ES2_SHELL_MOVIE_BAY_SELECT: return "bay select";
    case ES2_SHELL_MOVIE_TEARDOWN: return "mission teardown";
    case ES2_SHELL_MOVIE_MISSION_BUTTON: return "mission button";
    case ES2_SHELL_MOVIE_FOCUS_GAINED: return "sound focus gained";
    case ES2_SHELL_MOVIE_FADE_IN: return "music fade-in";
    case ES2_SHELL_MOVIE_THEATRE_STEP: return "theatre step";
    case ES2_SHELL_MOVIE_WAIT: return "wait";
    case ES2_SHELL_MOVIE_THEATRE_HIDE: return "theatre hide";
    case ES2_SHELL_MOVIE_CALLBACK: return "callback";
    default: return "?";
    }
}

static int32_t call(void *context, uint32_t action, int32_t arg, const es2_shell_movie_record *record) {
    (void)context;
    int32_t result = 0;
    if (action == ES2_SHELL_MOVIE_THEATRE_STEP) {
        if (es2_shell_movies_theatre(&movies, group)) printf("  (dropship queued)\n");
    } else
        result = es2_mci_player_call(&player, action, arg, record);
    if (action == ES2_SHELL_MOVIE_PUMP) return result;
    printf("0x%06x %-20s %d", (unsigned)action, action_name(action), (int)arg);
    if (record) {
        char path[MAX_PATH];
        if (es2_mci_player_path(player.cd_root, record->index, player.language, path, sizeof path) == 0)
            printf("  %s", path);
    }
    if (action == ES2_SHELL_MOVIE_PLAY) printf("  -> %s", result == ES2_SHELL_MOVIE_SKIPPED ? "skipped" :
                                                         result == ES2_SHELL_MOVIE_ENDED ? "ended" : "not opened");
    if (action == ES2_SHELL_MOVIE_OPEN_DEVICE) printf("  -> %s (IV32 status %d)", result ? "open" : "failed", player.iv32_status);
    printf("\n");
    fflush(stdout);
    return result;
}

static int32_t query(void *context, int what) {
    (void)context;
    switch (what) {
    case ES2_SHELL_MOVIE_Q_FOCUS: return 1;
    case ES2_SHELL_MOVIE_Q_QUIT: return player.quit;
    default: return 0;   /* no bay bar, no theatre panel, windowed, page 0 */
    }
}

int main(int argc, char **argv) {
    const char *root = NULL, *event = NULL;
    char cd_root[MAX_PATH];
    int language = 0, build = 1, a, b;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--cd-root") && i + 1 < argc) root = argv[++i];
        else if (!strcmp(argv[i], "--language") && i + 1 < argc) {
            const char *l = argv[++i];
            language = (l[0] == 'F' || l[0] == 'f') ? 1 : (l[0] == 'G' || l[0] == 'g') ? 2 : 0;
        } else if (!event) event = argv[i];
        else event = NULL, i = argc;
    }
    if (!root || !event) {
        fprintf(stderr, "usage: es2movie --cd-root DIR [--language E|F|G] intro|credits|victory|mission:G|"
                        "briefing:G:W|debrief:G:W|movie:N\n");
        return 2;
    }
    snprintf(cd_root, sizeof cd_root, "%s%s", root, root[0] && root[strlen(root) - 1] != '\\' ? "\\" : "");
    es2_shell_movies_init(&movies, 1);
    if (!strcmp(event, "intro")) { es2_shell_movies_intro(&movies, 0); build = 0; }
    else if (!strcmp(event, "credits")) { es2_shell_movies_credits(&movies); build = 0; }
    else if (!strcmp(event, "victory")) es2_shell_movies_victory(&movies);
    else if (sscanf(event, "mission:%d", &a) == 1 && !strchr(event + 8, ':')) {
        group = (int16_t)a;
        es2_shell_movies_mission(&movies, 0, group, -1, -1);
    } else if (sscanf(event, "briefing:%d:%d", &a, &b) == 2) {
        group = (int16_t)a;
        es2_shell_movies_mission(&movies, 1, group, (int16_t)b, -1);
    } else if (sscanf(event, "debrief:%d:%d", &a, &b) == 2) {
        group = (int16_t)a;
        es2_shell_movies_mission(&movies, 4, group, -1, (int16_t)b);
    } else if (sscanf(event, "movie:%i", &a) == 1 && es2_shell_movie_name(a)) {
        es2_shell_movie_enqueue(&movies, a, es2_shell_movie_rect_full, -1, 0, 0);
    } else {
        fprintf(stderr, "es2movie: unknown event %s\n", event);
        return 2;
    }
    WNDCLASSA wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "ES2MovieCheck";
    RegisterClassA(&wc);
    RECT r = {0, 0, 640, 480};
    DWORD style = 0xce0000;   /* VSHELL's AdjustWindowRectEx style (vshell_process.md section 7) */
    AdjustWindowRectEx(&r, style, FALSE, 0);
    HWND window = CreateWindowExA(0, wc.lpszClassName, "EarthSiege II", style | WS_VISIBLE, CW_USEDEFAULT,
                                  CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
    if (!window) { fprintf(stderr, "es2movie: no window\n"); return 1; }
    player.parent = window;
    player.cd_root = cd_root;
    player.language = language;
    es2_shell_movie_hooks hooks = {NULL, call, query};
    int quit = es2_shell_movies_play(&movies, build, &hooks);
    printf("[player returned %d, quit %d]\n", quit, player.quit);
    DestroyWindow(window);
    return quit ? 1 : 0;
}
