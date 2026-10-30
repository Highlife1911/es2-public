/* Plays the shell sounds once through SDL3 for a listening check. Not used
 * by tests.
 *
 *   es2sound [SHLSOUND.VOL] [--music 1|2] [--seconds N]
 *
 * Default archive: %ES2_DATA_ROOT%\VOL\SHLSOUND.VOL. Sequence: music
 * (shell1 or shell2, default 1) gains the focus at volume 0 and fades in as
 * VSHELL does, plays N seconds (default 15), then gm_69, bptlt2 and
 * lswitch2 one after another, then the music fades out (to 1) and the focus
 * is lost (everything stops). */
#define SDL_MAIN_HANDLED
#include "es2/shell/gui/sound.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void wait_ms(es2_shell_audio *a, uint32_t ms) {
    uint64_t end = SDL_GetTicks() + ms;
    while (SDL_GetTicks() < end) { es2_shell_audio_service(a); SDL_Delay(5); }
}

static void ramp(es2_shell_sounds *s, es2_shell_audio *a, const uint8_t *prefs, int up) {
    es2_shell_sound_ramp r;
    if (!es2_shell_sounds_ramp_begin(s, prefs, up, (uint32_t)SDL_GetTicks(), &r)) return;
    while (es2_shell_sounds_ramp_tick(s, &r, (uint32_t)SDL_GetTicks())) { es2_shell_audio_service(a); SDL_Delay(1); }
}

int main(int argc, char **argv) {
    const char *vol_path = NULL;
    char default_path[1024];
    int music = 1, seconds = 15;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--music") && i + 1 < argc) music = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = atoi(argv[++i]);
        else vol_path = argv[i];
    }
    if (!vol_path) {
        const char *root = getenv("ES2_DATA_ROOT");
        if (!root) { fprintf(stderr, "es2sound: give SHLSOUND.VOL or set ES2_DATA_ROOT\n"); return 2; }
        snprintf(default_path, sizeof default_path, "%s\\VOL\\SHLSOUND.VOL", root);
        vol_path = default_path;
    }
    FILE *f = fopen(vol_path, "rb");
    if (!f) { fprintf(stderr, "es2sound: cannot open %s\n", vol_path); return 2; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *bytes = size > 0 ? malloc((size_t)size) : NULL;
    if (!bytes || fread(bytes, 1, (size_t)size, f) != (size_t)size) { fclose(f); return 2; }
    fclose(f);
    es2_vol vol;
    es2_vfs vfs;
    if (es2_vol_open((es2_span){bytes, (size_t)size}, &vol, NULL) != ES2_OK) { fprintf(stderr, "es2sound: bad VOL\n"); return 2; }
    es2_vfs_source src = {.kind = ES2_VFS_VOL, .data.vol = &vol};
    if (es2_vfs_open(&src, 1, &vfs, NULL) != ES2_OK) return 2;

    es2_shell_audio_device dev;
    if (!es2_shell_audio_sdl_device(&dev)) { fprintf(stderr, "es2sound: no audio device: %s\n", SDL_GetError()); return 3; }
    es2_shell_prefs_session prefs;
    uint8_t out[ES2_SHELL_PREFS_SIZE];
    es2_shell_prefs_session_open(NULL, &prefs);
    prefs.current[ES2_SHELL_PREFS_MUSIC] = prefs.current[ES2_SHELL_PREFS_EFFECTS] = 1;
    prefs.current[ES2_SHELL_PREFS_MUSIC_CHOICE] = music == 1;
    es2_shell_sounds s;
    es2_shell_sounds_startup(&s, NULL, 1, &prefs, out); /* out is not written anywhere */
    es2_shell_audio a;
    int loaded = es2_shell_audio_open(&a, &dev, &vfs, &s);
    printf("es2sound: %d of 4 samples loaded, music %s\n", loaded, s.music);
    s.player = es2_shell_audio_player(&a);

    es2_shell_sounds_focus_gain(&s);
    printf("music: fade-in\n");
    ramp(&s, &a, prefs.current, 1);
    wait_ms(&a, (uint32_t)(seconds > 0 ? seconds : 0) * 1000u);
    static const char *names[] = {"gm_69 (button)", "bptlt2 (bay bar)", "lswitch2 (main-menu background)"};
    static const int order[] = {ES2_SHELL_SOUND_GM_69, ES2_SHELL_SOUND_BPTLT2, ES2_SHELL_SOUND_LSWITCH2};
    for (int i = 0; i < 3; i++) {
        printf("effect: %s\n", names[i]);
        es2_shell_sounds_effect(&s, prefs.current, order[i]);
        wait_ms(&a, i == 2 ? 4500 : 1500);
    }
    printf("music: fade-out (ends at volume 1), then focus loss\n");
    ramp(&s, &a, prefs.current, 0);
    wait_ms(&a, 1000);
    es2_shell_sounds_focus_lose(&s);
    es2_shell_audio_close(&a);
    es2_shell_audio_sdl_shutdown();
    free(bytes);
    return 0;
}
