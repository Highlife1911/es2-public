#ifndef ES2_SHELL_GUI_SOUND_H
#define ES2_SHELL_GUI_SOUND_H
/* Playback of the shell's four samples (audio.md, "the VSHELL shell audio
 * contract", "Files and playback format", and "the own shell's sounds").
 * One voice (stream) per sample in the file's own format;
 * the device is an interface so tests run without audio hardware.
 * es2_shell_audio_sdl_device (sound_sdl.c) is the SDL3 implementation. */
#include "es2/formats/vfs.h"
#include "es2/shell/sounds.h"

#include <stddef.h>
#include <stdint.h>

typedef struct es2_shell_wav {
    uint32_t rate;
    uint16_t channels, bits;
    es2_span samples; /* borrowed from the file bytes */
} es2_shell_wav;
/* Loader 0x42eaf7: "RIFF" -> rate at 0x18, channels at 0x16, bits at 0x22,
 * samples from 0x2c, length = dword at 0x28 minus 0x2c (so the last 44 bytes
 * are not played); a negative length is 0. Anything else: raw 8-bit mono
 * 11,025 Hz over the whole file (audio.md: length of that path not read; whole
 * file is our assumption). Returns 0 and leaves *out when the RIFF header is
 * shorter than 0x2c bytes or the samples run past the file. */
int es2_shell_wav_parse(es2_span file, es2_shell_wav *out);

/* Audio device interface: one stream per voice. */
typedef struct es2_shell_audio_device {
    void *ctx;
    void *(*open_stream)(void *ctx, uint32_t rate, int channels, int bits);
    void (*close_stream)(void *ctx, void *stream);
    void (*clear)(void *ctx, void *stream);
    void (*put)(void *ctx, void *stream, const uint8_t *data, size_t size);
    size_t (*queued)(void *ctx, void *stream);
    void (*set_gain)(void *ctx, void *stream, float gain);
} es2_shell_audio_device;

typedef struct es2_shell_audio {
    es2_shell_audio_device device;
    es2_shell_wav wav[ES2_SHELL_SOUND_COUNT];
    int loaded[ES2_SHELL_SOUND_COUNT];
    void *stream[ES2_SHELL_SOUND_COUNT];
    int volume[ES2_SHELL_SOUND_COUNT];
    int playing[ES2_SHELL_SOUND_COUNT];
    int looping[ES2_SHELL_SOUND_COUNT];
} es2_shell_audio;

/* Finds the four samples in `vfs` (es2_shell_sound_path; a missing or bad
 * file leaves that sample silent, as VSHELL's failed play) and opens one
 * stream each. The VFS bytes must stay alive. Returns the number loaded. */
int es2_shell_audio_open(es2_shell_audio *a, const es2_shell_audio_device *device, const es2_vfs *vfs,
                         const es2_shell_sounds *sounds);
void es2_shell_audio_close(es2_shell_audio *a);
/* The player callbacks for es2_shell_sounds (ctx = a). */
es2_shell_sound_player es2_shell_audio_player(es2_shell_audio *a);
/* Keep looping voices fed; call every frame or tick. */
void es2_shell_audio_service(es2_shell_audio *a);
/* Gain of volume 0..100: driver value v*100*32767/10000 over 32767 (linear). */
float es2_shell_audio_gain(int volume);

/* SDL3 device (sound_sdl.c): opens the default playback device. Returns 0
 * when no device opens (the shell then stays silent, audio.md, "What an own
 * shell must reproduce"). */
int es2_shell_audio_sdl_device(es2_shell_audio_device *out);
void es2_shell_audio_sdl_shutdown(void);
#endif
