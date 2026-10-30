/* The SDL3 audio device behind es2_shell_audio_device. SDL mixes the
 * bound streams and converts each sample's own format to the device's. */
#include "es2/shell/gui/sound.h"

#include <SDL3/SDL.h>

static SDL_AudioDeviceID g_device;

static void *open_stream(void *ctx, uint32_t rate, int channels, int bits) {
    (void)ctx;
    SDL_AudioSpec spec = {bits == 16 ? SDL_AUDIO_S16LE : SDL_AUDIO_U8, channels, (int)rate};
    SDL_AudioStream *s = SDL_CreateAudioStream(&spec, NULL);
    if (!s) return NULL;
    if (!SDL_BindAudioStream(g_device, s)) { SDL_DestroyAudioStream(s); return NULL; }
    return s;
}
static void close_stream(void *ctx, void *s) { (void)ctx; SDL_DestroyAudioStream(s); }
static void clear(void *ctx, void *s) { (void)ctx; SDL_ClearAudioStream(s); }
static void put(void *ctx, void *s, const uint8_t *d, size_t n) {
    (void)ctx;
    while (n) {
        int chunk = n > 0x40000000u ? 0x40000000 : (int)n;
        SDL_PutAudioStreamData(s, d, chunk);
        d += chunk;
        n -= (size_t)chunk;
    }
}
static size_t queued(void *ctx, void *s) {
    (void)ctx;
    int q = SDL_GetAudioStreamQueued(s);
    return q > 0 ? (size_t)q : 0;
}
static void set_gain(void *ctx, void *s, float g) { (void)ctx; SDL_SetAudioStreamGain(s, g); }

int es2_shell_audio_sdl_device(es2_shell_audio_device *out) {
    if (!out) return 0;
    if (!g_device) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return 0;
        g_device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
        if (!g_device) { SDL_QuitSubSystem(SDL_INIT_AUDIO); return 0; }
    }
    *out = (es2_shell_audio_device){NULL, open_stream, close_stream, clear, put, queued, set_gain};
    return 1;
}

void es2_shell_audio_sdl_shutdown(void) {
    if (!g_device) return;
    SDL_CloseAudioDevice(g_device);
    g_device = 0;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}
