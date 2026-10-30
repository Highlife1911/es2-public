/* The shell's sample playback over an audio device interface.
 * Contract: docs/reverse-engineering/audio.md, "Files and playback format"
 * and "the own shell's sounds". */
#include "es2/shell/gui/sound.h"
#include "es2/shell/bytes.h"

#include <string.h>

static uint32_t le32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

int es2_shell_wav_parse(es2_span file, es2_shell_wav *out) {
    if (!out || (!file.data && file.size)) return 0;
    es2_shell_wav w;
    if (file.size >= 4 && memcmp(file.data, "RIFF", 4) == 0) {
        /* Original: VSHELL.EXE 0x42eaf7, length 0x42ebd4..0x42ebd7 */
        if (file.size < 0x2c) return 0;
        uint32_t declared = le32(file.data + 0x28);
        size_t length = declared > 0x2c ? declared - 0x2c : 0;
        if (length > file.size - 0x2c) return 0;
        w.rate = le32(file.data + 0x18);
        w.channels = es2_le16(file.data + 0x16);
        w.bits = es2_le16(file.data + 0x22);
        w.samples = (es2_span){file.data + 0x2c, length};
        if (w.rate == 0 || (w.channels != 1 && w.channels != 2) || (w.bits != 8 && w.bits != 16)) return 0;
    } else {
        /* 0x42ec13..0x42ec2b: raw 8-bit mono 11,025 Hz. */
        w.rate = 11025;
        w.channels = 1;
        w.bits = 8;
        w.samples = file;
    }
    *out = w;
    return 1;
}

float es2_shell_audio_gain(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    /* 0x42e237..0x42e269: v * 100 * 32767 / 10000, unsigned, truncated. */
    uint32_t driver = (uint32_t)volume * 100u * 32767u / 10000u;
    return (float)driver / 32767.0f;
}

int es2_shell_audio_open(es2_shell_audio *a, const es2_shell_audio_device *device, const es2_vfs *vfs,
                         const es2_shell_sounds *sounds) {
    if (!a) return 0;
    memset(a, 0, sizeof *a);
    if (!device) return 0;
    a->device = *device;
    int n = 0;
    for (int i = 0; i < ES2_SHELL_SOUND_COUNT; i++) {
        const char *path = es2_shell_sound_path(sounds, i);
        es2_vfs_copy copy;
        if (!path || !vfs || es2_vfs_find(vfs, path, &copy, NULL) != ES2_OK) continue;
        if (!es2_shell_wav_parse(copy.payload, &a->wav[i])) continue;
        if (a->device.open_stream)
            a->stream[i] = a->device.open_stream(a->device.ctx, a->wav[i].rate, a->wav[i].channels, a->wav[i].bits);
        if (!a->stream[i]) continue;
        a->loaded[i] = 1;
        n++;
    }
    return n;
}

void es2_shell_audio_close(es2_shell_audio *a) {
    if (!a) return;
    for (int i = 0; i < ES2_SHELL_SOUND_COUNT; i++)
        if (a->stream[i] && a->device.close_stream) a->device.close_stream(a->device.ctx, a->stream[i]);
    memset(a, 0, sizeof *a);
}

static void put_sample(es2_shell_audio *a, int i) {
    if (a->device.put && a->wav[i].samples.size)
        a->device.put(a->device.ctx, a->stream[i], a->wav[i].samples.data, a->wav[i].samples.size);
}

static void play(void *ctx, int sound, int volume, int loop) {
    es2_shell_audio *a = ctx;
    if (!a || sound < 0 || sound >= ES2_SHELL_SOUND_COUNT || !a->loaded[sound]) return;
    /* A start while the sample still sounds restarts it (audio.md, "the VSHELL shell audio contract", "Open":
     * SOS's choice between restart and second voice is open). */
    if (a->device.clear) a->device.clear(a->device.ctx, a->stream[sound]);
    a->volume[sound] = volume;
    a->looping[sound] = loop;
    a->playing[sound] = 1;
    if (a->device.set_gain) a->device.set_gain(a->device.ctx, a->stream[sound], es2_shell_audio_gain(volume));
    put_sample(a, sound);
}

static void set_volume(void *ctx, int sound, int volume) {
    es2_shell_audio *a = ctx;
    if (!a || sound < 0 || sound >= ES2_SHELL_SOUND_COUNT) return;
    a->volume[sound] = volume;
    if (a->loaded[sound] && a->device.set_gain)
        a->device.set_gain(a->device.ctx, a->stream[sound], es2_shell_audio_gain(volume));
}

static void stop_all(void *ctx) {
    es2_shell_audio *a = ctx;
    if (!a) return;
    for (int i = 0; i < ES2_SHELL_SOUND_COUNT; i++) {
        if (a->loaded[i] && a->device.clear) a->device.clear(a->device.ctx, a->stream[i]);
        a->playing[i] = a->looping[i] = 0;
    }
}

es2_shell_sound_player es2_shell_audio_player(es2_shell_audio *a) {
    return (es2_shell_sound_player){a, play, set_volume, stop_all};
}

void es2_shell_audio_service(es2_shell_audio *a) {
    if (!a) return;
    for (int i = 0; i < ES2_SHELL_SOUND_COUNT; i++) {
        if (!a->loaded[i] || !a->playing[i]) continue;
        size_t q = a->device.queued ? a->device.queued(a->device.ctx, a->stream[i]) : 0;
        if (a->looping[i]) {
            /* Endless loop (count 0 -> driver -1): append the next pass early. */
            if (q < a->wav[i].samples.size) put_sample(a, i);
        } else if (q == 0) {
            a->playing[i] = 0;
        }
    }
}
