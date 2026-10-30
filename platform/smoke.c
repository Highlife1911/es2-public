#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

enum { VIEW_WIDTH = 800, VIEW_HEIGHT = 450, TEST_FRAMES = 8 };

typedef struct smoke_state {
    SDL_Window *window;
    SDL_Renderer *renderer;
    unsigned int frames;
    bool testing;
    bool ready;
    bool failure_reported;
} smoke_state;

static smoke_state state;

static SDL_AppResult fail(smoke_state *app, const char *stage)
{
    fprintf(stderr, "ES2_SMOKE_FAIL stage=%s error=%s\n", stage, SDL_GetError());
    fflush(stderr);
    app->failure_reported = true;
    return SDL_APP_FAILURE;
}

static bool fill(SDL_Renderer *renderer, float x, float y, float width,
                 float height, Uint8 red, Uint8 green, Uint8 blue)
{
    const SDL_FRect rectangle = { x, y, width, height };
    return SDL_SetRenderDrawColor(renderer, red, green, blue, 255) &&
           SDL_RenderFillRect(renderer, &rectangle);
}

static bool draw(smoke_state *app)
{
    SDL_Renderer *renderer = app->renderer;
    const float marker_x = 64.0f + (float)(app->frames % TEST_FRAMES) * 84.0f;

    return SDL_SetRenderDrawColor(renderer, 14, 29, 41, 255) &&
           SDL_RenderClear(renderer) &&
           fill(renderer, 40.0f, 38.0f, 720.0f, 374.0f, 25, 47, 62) &&
           fill(renderer, 64.0f, 142.0f, 208.0f, 144.0f, 74, 204, 182) &&
           fill(renderer, 296.0f, 142.0f, 208.0f, 144.0f, 240, 183, 78) &&
           fill(renderer, 528.0f, 142.0f, 208.0f, 144.0f, 103, 157, 235) &&
           fill(renderer, marker_x, 318.0f, 84.0f, 8.0f, 235, 242, 246) &&
           SDL_SetRenderScale(renderer, 2.0f, 2.0f) &&
           SDL_SetRenderDrawColor(renderer, 235, 242, 246, 255) &&
           SDL_RenderDebugText(renderer, 32.0f, 34.0f, "ES2 / SDL3 BUILD SPIKE") &&
           SDL_SetRenderScale(renderer, 1.0f, 1.0f) &&
           SDL_RenderDebugText(renderer, 64.0f, 108.0f,
                               "One C17 source. Native SDL3 window.") &&
           SDL_RenderDebugText(renderer, 64.0f, 360.0f,
                               "Original geometry. No game assets.") &&
           SDL_RenderDebugText(renderer, 64.0f, 384.0f,
                               app->testing ? "TEST: eight checked frames" :
                                              "Close the window to exit.") &&
           SDL_RenderPresent(renderer);
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    int index;
    *appstate = &state;
    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--smoke-test") == 0) {
            state.testing = true;
        } else {
            SDL_SetError("unsupported argument: %s", argv[index]);
            return fail(&state, "arguments");
        }
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) return fail(&state, "video-init");
    if (!SDL_CreateWindowAndRenderer("ES2 - SDL3 build spike", VIEW_WIDTH,
                                    VIEW_HEIGHT, SDL_WINDOW_RESIZABLE,
                                    &state.window, &state.renderer)) {
        return fail(&state, "window-renderer");
    }
    if (!SDL_SetRenderLogicalPresentation(state.renderer, VIEW_WIDTH, VIEW_HEIGHT,
                                         SDL_LOGICAL_PRESENTATION_LETTERBOX)) {
        return fail(&state, "logical-presentation");
    }
    /* Pacing is optional; failed rendering and initialization are not. */
    if (!SDL_SetRenderVSync(state.renderer, 1)) {
        fprintf(stderr, "ES2_SMOKE_INFO vsync-unavailable error=%s\n", SDL_GetError());
    }
    state.ready = true;
    printf("ES2_SMOKE_READY width=%d height=%d headers=%d runtime=%d\n",
           VIEW_WIDTH, VIEW_HEIGHT, SDL_VERSION, SDL_GetVersion());
    fflush(stdout);
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    smoke_state *app = appstate;
    if (!draw(app)) return fail(app, "render");
    ++app->frames;
    if (app->testing && app->frames == TEST_FRAMES) return SDL_APP_SUCCESS;
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    smoke_state *app = appstate;
    if (event->type != SDL_EVENT_QUIT) return SDL_APP_CONTINUE;
    if (app->testing && app->frames < TEST_FRAMES) {
        SDL_SetError("window closed before all test frames completed");
        return fail(app, "early-quit");
    }
    return SDL_APP_SUCCESS;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    smoke_state *app = appstate;
    if (app == NULL) return;
    if (app->renderer != NULL) SDL_DestroyRenderer(app->renderer);
    if (app->window != NULL) SDL_DestroyWindow(app->window);
    app->renderer = NULL;
    app->window = NULL;
    if (result == SDL_APP_SUCCESS && app->ready && app->testing &&
        app->frames == TEST_FRAMES && !app->failure_reported) {
        printf("ES2_SMOKE_PASS frames=%u\n", app->frames);
        fflush(stdout);
    } else if (result == SDL_APP_SUCCESS && app->ready && !app->testing) {
        printf("ES2_SMOKE_STOP frames=%u\n", app->frames);
        fflush(stdout);
    } else if (!app->failure_reported) {
        fprintf(stderr, "ES2_SMOKE_FAIL stage=shutdown frames=%u\n", app->frames);
        fflush(stderr);
    }
}
