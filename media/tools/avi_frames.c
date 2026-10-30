/* avi_frames: decodes an AVI through Video for Windows with the system Indeo codec bound as
 * the own shell binds it (media/src/iv32_bind.c), and writes what the Windows build shows.
 *
 *   avi_frames <input.avi> <output prefix> [--max-frames <n>]
 *
 * <prefix>.rgb   the first video stream's frames, raw RGB24 (R, G, B per pixel), top-down,
 *                rows without padding, one frame after the other
 * <prefix>.wav   the first audio stream (PCM only), when the file has one
 * <prefix>.json  geometry, frame rate (dwRate/dwScale reduced), counts, source format
 *
 * AVIStreamGetFrame decompresses with ICDecompress into a 24-bit DIB. A codec that cannot
 * do that is asked for its own format; an 8-bit DIB is expanded through its colour table.
 * The program is a build tool: it is not linked into the game. */
#include "es2/media/iv32_bind.h"

#include <vfw.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DIMENSION 4096u
#define FORMAT_CAPACITY 2048u

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }

static int fail(const char *what) {
    fprintf(stderr, "avi_frames: %s\n", what);
    return 1;
}

static uint32_t gcd(uint32_t a, uint32_t b) {
    while (b) { uint32_t t = a % b; a = b; b = t; }
    return a;
}

/* A compression below 256 is a BI_ constant (BI_RLE8 is 1), not a code. */
static void fourcc_text(uint32_t code, char out[8]) {
    if (code < 256) {
        snprintf(out, 8, "BI_%u", (unsigned)code);
        return;
    }
    for (int i = 0; i < 4; i++) {
        uint8_t c = (uint8_t)(code >> (8 * i));
        out[i] = c >= 0x20 && c < 0x7f && c != '"' && c != '\\' ? (char)c : '?';
    }
    out[4] = 0;
}

typedef struct {
    uint32_t width, height;    /* from the stream's BITMAPINFOHEADER */
    uint32_t source_compression, source_bits;
} video_format;

/* The stream's format chunk is a BITMAPINFOHEADER: biSize 0, biWidth 4, biHeight 8,
 * biBitCount 14, biCompression 16. */
static int read_video_format(PAVISTREAM stream, LONG start, video_format *out) {
    uint8_t raw[FORMAT_CAPACITY];
    LONG size = sizeof raw;
    if (AVIStreamReadFormat(stream, start, raw, &size) != 0 || size < 40 || (size_t)size > sizeof raw) return 0;
    uint32_t header = rd32(raw);
    int32_t height = (int32_t)rd32(raw + 8);
    if (header < 40 || header > (uint32_t)size) return 0;
    out->width = rd32(raw + 4);
    out->height = height < 0 ? (uint32_t)(-(int64_t)height) : (uint32_t)height;
    out->source_bits = rd16(raw + 14);
    out->source_compression = rd32(raw + 16);
    return out->width >= 1 && out->width <= MAX_DIMENSION && out->height >= 1 && out->height <= MAX_DIMENSION;
}

/* One decoded DIB into top-down RGB24 rows. Only BI_RGB 24-bit and 8-bit are accepted. */
static int dib_to_rgb(const BITMAPINFOHEADER *dib, const video_format *f, uint8_t *rgb) {
    const uint8_t *base = (const uint8_t *)dib;
    uint32_t w = f->width, h = f->height;
    uint32_t bits = dib->biBitCount;
    int32_t dib_height = dib->biHeight;
    if (dib->biSize < 40 || dib->biCompression != BI_RGB || dib->biPlanes != 1) return 0;
    if (dib->biWidth != (LONG)w || (uint32_t)(dib_height < 0 ? -dib_height : dib_height) != h) return 0;
    if (bits != 24 && bits != 8) return 0;
    uint32_t colors = 0;
    if (bits == 8) {
        colors = dib->biClrUsed ? dib->biClrUsed : 256;
        if (colors > 256) return 0;
    }
    const uint8_t *table = base + dib->biSize;
    const uint8_t *pixels = table + 4 * colors;
    uint32_t stride = ((w * bits + 31) / 32) * 4;
    for (uint32_t y = 0; y < h; y++) {
        uint32_t source_row = dib_height > 0 ? h - 1 - y : y;
        const uint8_t *in = pixels + (size_t)source_row * stride;
        uint8_t *out = rgb + (size_t)y * w * 3;
        for (uint32_t x = 0; x < w; x++) {
            if (bits == 24) {
                out[3 * x] = in[3 * x + 2];
                out[3 * x + 1] = in[3 * x + 1];
                out[3 * x + 2] = in[3 * x];
            } else {
                if (in[x] >= colors) return 0;
                const uint8_t *entry = table + 4 * in[x];   /* B, G, R, reserved */
                out[3 * x] = entry[2];
                out[3 * x + 1] = entry[1];
                out[3 * x + 2] = entry[0];
            }
        }
    }
    return 1;
}

static int write_audio(PAVIFILE file, const char *wav_path, char *audio_json, size_t capacity) {
    PAVISTREAM stream = NULL;
    audio_json[0] = 0;
    if (AVIFileGetStream(file, &stream, streamtypeAUDIO, 0) != AVIERR_OK || !stream) return 0;
    int result = 0;
    uint8_t format[FORMAT_CAPACITY];
    LONG start = AVIStreamStart(stream), length = AVIStreamLength(stream), size = sizeof format;
    FILE *out = NULL;
    uint8_t *chunk = NULL;
    if (start < 0 || length < 0 || AVIStreamReadFormat(stream, start, format, &size) != 0 || size < 16 || (size_t)size > sizeof format) {
        result = fail("cannot read the audio format");
        goto done;
    }
    uint16_t tag = rd16(format), channels = rd16(format + 2), align = rd16(format + 12), bits = rd16(format + 14);
    uint32_t rate = rd32(format + 4);
    if (tag != WAVE_FORMAT_PCM || !channels || !rate || !align || (bits != 8 && bits != 16) || align != channels * (bits / 8)) {
        result = fail("the audio stream is not PCM of 8 or 16 bits");
        goto done;
    }
    uint64_t total = (uint64_t)length * align;
    if (total > 0x7fffffffu - 44u) {
        result = fail("the audio stream is too long for a WAV file");
        goto done;
    }
    out = fopen(wav_path, "wb");
    if (!out) {
        result = fail("cannot create the WAV file");
        goto done;
    }
    uint8_t header[44];
    memcpy(header, "RIFF", 4);
    wr32(header + 4, (uint32_t)(36 + total));
    memcpy(header + 8, "WAVEfmt ", 8);
    wr32(header + 16, 16);
    wr16(header + 20, 1);
    wr16(header + 22, channels);
    wr32(header + 24, rate);
    wr32(header + 28, rate * align);
    wr16(header + 32, align);
    wr16(header + 34, bits);
    memcpy(header + 36, "data", 4);
    wr32(header + 40, (uint32_t)total);
    if (fwrite(header, 1, sizeof header, out) != sizeof header) {
        result = fail("cannot write the WAV file");
        goto done;
    }
    const LONG chunk_samples = 8192;
    chunk = malloc((size_t)chunk_samples * align);
    if (!chunk) {
        result = fail("out of memory");
        goto done;
    }
    uint64_t written = 0;
    for (LONG position = 0; position < length;) {
        LONG want = length - position < chunk_samples ? length - position : chunk_samples;
        LONG bytes = 0, samples = 0;
        if (AVIStreamRead(stream, start + position, want, chunk, want * align, &bytes, &samples) != 0 || samples <= 0 ||
            samples > want || (uint32_t)bytes != (uint32_t)samples * align) {
            result = fail("cannot read the audio samples");
            goto done;
        }
        if (fwrite(chunk, 1, (size_t)bytes, out) != (size_t)bytes) {
            result = fail("cannot write the WAV file");
            goto done;
        }
        written += (uint64_t)bytes;
        position += samples;
    }
    if (written != total) {
        result = fail("short audio");
        goto done;
    }
    snprintf(audio_json, capacity,
             ",\n  \"audio\": {\"format_tag\": %u, \"channels\": %u, \"sample_rate\": %lu, \"bits\": %u, \"samples\": %ld, \"bytes\": %llu}",
             tag, channels, (unsigned long)rate, bits, (long)length, (unsigned long long)total);
done:
    free(chunk);
    if (out && fclose(out) != 0 && !result) result = fail("cannot write the WAV file");
    AVIStreamRelease(stream);
    return result;
}

int main(int argc, char **argv) {
    const char *input = NULL, *prefix = NULL;
    long max_frames = -1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--max-frames") && i + 1 < argc) {
            char *end;
            max_frames = strtol(argv[++i], &end, 10);
            if (*end || max_frames < 1) return fail("bad --max-frames");
        } else if (!input) input = argv[i];
        else if (!prefix) prefix = argv[i];
        else return fail("too many arguments");
    }
    if (!input || !prefix) {
        fprintf(stderr, "usage: avi_frames <input.avi> <output prefix> [--max-frames <n>]\n");
        return 2;
    }
    if (strlen(prefix) > 500) return fail("prefix too long");

    es2_iv32_drivers drivers;
    const char *binding = "unavailable";
    if (es2_iv32_system_drivers(&drivers)) {
        switch (es2_iv32_bind(NULL, &drivers)) {
        case ES2_IV32_BOUND: binding = "bound"; break;
        case ES2_IV32_EXISTING: binding = "existing"; break;
        default: binding = "failed"; break;
        }
    }

    int result = 1;
    PAVIFILE file = NULL;
    PAVISTREAM video = NULL;
    PGETFRAME getter = NULL;
    uint8_t *rgb = NULL;
    FILE *rgb_out = NULL, *json_out = NULL;
    char path[560], audio_json[300];
    AVIFileInit();
    if (AVIFileOpenA(&file, input, OF_READ | OF_SHARE_DENY_WRITE, NULL) != AVIERR_OK) {
        result = fail("cannot open the AVI");
        goto done;
    }
    if (AVIFileGetStream(file, &video, streamtypeVIDEO, 0) != AVIERR_OK || !video) {
        result = fail("no video stream");
        goto done;
    }
    AVISTREAMINFOA info;
    if (AVIStreamInfoA(video, &info, sizeof info) != AVIERR_OK || !info.dwScale || !info.dwRate) {
        result = fail("bad video stream header");
        goto done;
    }
    LONG start = AVIStreamStart(video), length = AVIStreamLength(video);
    video_format format;
    if (start < 0 || length < 1 || !read_video_format(video, start, &format)) {
        result = fail("bad video format");
        goto done;
    }
    uint32_t frames = (uint32_t)length;
    if (max_frames > 0 && (uint32_t)max_frames < frames) frames = (uint32_t)max_frames;

    BITMAPINFOHEADER wanted;
    memset(&wanted, 0, sizeof wanted);
    wanted.biSize = sizeof wanted;
    wanted.biWidth = (LONG)format.width;
    wanted.biHeight = (LONG)format.height;
    wanted.biPlanes = 1;
    wanted.biBitCount = 24;
    wanted.biCompression = BI_RGB;
    getter = AVIStreamGetFrameOpen(video, &wanted);
    if (!getter) getter = AVIStreamGetFrameOpen(video, NULL);
    if (!getter) {
        char name[8];
        fourcc_text(format.source_compression, name);
        fprintf(stderr, "avi_frames: no decompressor for '%s' (%u bits), IV32 binding %s\n", name, format.source_bits, binding);
        result = 1;
        goto done;
    }

    size_t frame_bytes = (size_t)format.width * format.height * 3;
    rgb = malloc(frame_bytes);
    snprintf(path, sizeof path, "%s.rgb", prefix);
    rgb_out = fopen(path, "wb");
    if (!rgb || !rgb_out) {
        result = fail("cannot create the output");
        goto done;
    }
    for (uint32_t n = 0; n < frames; n++) {
        const BITMAPINFOHEADER *dib = AVIStreamGetFrame(getter, start + (LONG)n);
        if (!dib || !dib_to_rgb(dib, &format, rgb)) {
            fprintf(stderr, "avi_frames: frame %u did not decode to a 24-bit or 8-bit DIB of the stream's size\n", n);
            result = 1;
            goto done;
        }
        if (fwrite(rgb, 1, frame_bytes, rgb_out) != frame_bytes) {
            result = fail("cannot write the frames");
            goto done;
        }
    }
    if (fclose(rgb_out) != 0) {
        rgb_out = NULL;
        result = fail("cannot write the frames");
        goto done;
    }
    rgb_out = NULL;

    snprintf(path, sizeof path, "%s.wav", prefix);
    if (write_audio(file, path, audio_json, sizeof audio_json)) goto done;

    uint32_t divisor = gcd(info.dwRate, info.dwScale);
    char name[8];
    fourcc_text(format.source_compression, name);
    snprintf(path, sizeof path, "%s.json", prefix);
    json_out = fopen(path, "wb");
    if (!json_out) {
        result = fail("cannot create the JSON file");
        goto done;
    }
    fprintf(json_out,
            "{\n  \"width\": %u,\n  \"height\": %u,\n  \"frames\": %u,\n  \"stream_frames\": %ld,\n"
            "  \"rate_num\": %lu,\n  \"rate_den\": %lu,\n  \"pixel_format\": \"rgb24\",\n"
            "  \"source_compression\": \"%s\",\n  \"source_bits\": %u,\n  \"iv32_binding\": \"%s\"%s\n}\n",
            format.width, format.height, frames, (long)length, (unsigned long)(info.dwRate / divisor),
            (unsigned long)(info.dwScale / divisor), name, format.source_bits, binding, audio_json);
    if (fclose(json_out) != 0) {
        json_out = NULL;
        result = fail("cannot write the JSON file");
        goto done;
    }
    json_out = NULL;
    result = 0;
done:
    if (rgb_out) fclose(rgb_out);
    if (json_out) fclose(json_out);
    free(rgb);
    if (getter) AVIStreamGetFrameClose(getter);
    if (video) AVIStreamRelease(video);
    if (file) AVIFileRelease(file);
    AVIFileExit();
    return result;
}
