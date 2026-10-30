/* Shell sound logic (es2/shell/sounds.h) and the sample player
 * (es2/shell/gui/sound.h) over a fake device: no audio device is opened.
 * With ES2_DATA_ROOT the samples of VOL\SHLSOUND.VOL are checked against
 * audio.md, "Files and playback format" (played bytes and formats); SKIP
 * without it. */
#include "es2/shell/gui/sound.h"
#include "es2/shell/sounds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; fprintf(stderr, "line %d: %s\n", __LINE__, #x); } } while (0)

/* Recording player. */
typedef struct rec { char log[2048]; } rec;
static void add(rec *r, const char *s) { if (strlen(r->log) + strlen(s) + 2 < sizeof r->log) { strcat(r->log, s); strcat(r->log, ";"); } }
static void r_play(void *c, int s, int v, int l) { char b[32]; snprintf(b, sizeof b, "play %d %d %d", s, v, l); add(c, b); }
static void r_vol(void *c, int s, int v) { char b[32]; snprintf(b, sizeof b, "vol %d %d", s, v); add(c, b); }
static void r_stop(void *c) { add(c, "stop"); }

static void test_startup(void) {
    rec r = {{0}};
    es2_shell_sound_player p = {&r, r_play, r_vol, r_stop};
    es2_shell_sounds s;
    es2_shell_prefs_session prefs;
    uint8_t file[ES2_SHELL_PREFS_SIZE], out[ES2_SHELL_PREFS_SIZE], expect[ES2_SHELL_PREFS_SIZE];
    for (unsigned i = 0; i < ES2_SHELL_PREFS_SIZE; i++) file[i] = (uint8_t)(i * 7 + 3);
    es2_span span = {file, sizeof file};

    /* Sound flag 0 (-s/-v/-?): nothing written, no flip. */
    es2_shell_prefs_session_open(&span, &prefs);
    memset(out, 0xee, sizeof out);
    CHECK(es2_shell_sounds_startup(&s, &p, 0, &prefs, out) == 0);
    CHECK(!s.active && !s.music && out[0] == 0xee && prefs.current[5] == file[5]);
    es2_shell_sounds_focus_gain(&s);
    CHECK(r.log[0] == 0);

    /* Byte 5 = 0x26 (!= 0) -> shell1, written byte 5 = 0x27, rest the file. */
    CHECK(es2_shell_sounds_startup(&s, &p, 1, &prefs, out) == 1);
    CHECK(s.music && strcmp(s.music, "hmi\\shell1.wav") == 0);
    memcpy(expect, file, sizeof expect);
    expect[5] ^= 1;
    CHECK(memcmp(out, expect, sizeof out) == 0);
    CHECK(memcmp(prefs.committed, expect, sizeof out) == 0);
    CHECK(s.music_volume == 0 && !s.focus);

    /* Consecutive runs alternate when byte 5 is 0/1. */
    file[5] = 1;
    for (int run = 0; run < 4; run++) {
        es2_shell_prefs_session_open(&span, &prefs);
        CHECK(es2_shell_sounds_startup(&s, &p, 1, &prefs, out) == 1);
        CHECK(strcmp(s.music, file[5] ? "hmi\\shell1.wav" : "hmi\\shell2.wav") == 0);
        CHECK(out[5] == (file[5] ^ 1));
        memcpy(file, out, sizeof file);
    }
    /* No file: zero block, shell2, byte 5 written as 1. */
    es2_shell_prefs_session_open(NULL, &prefs);
    CHECK(es2_shell_sounds_startup(&s, &p, 1, &prefs, out) == 1);
    CHECK(strcmp(s.music, "hmi\\shell2.wav") == 0 && out[5] == 1 && out[0] == 0 && out[53] == 0);
    CHECK(strcmp(es2_shell_sound_path(&s, ES2_SHELL_SOUND_MUSIC), "hmi\\shell2.wav") == 0);
    CHECK(es2_shell_sound_path(&s, 4) == NULL && es2_shell_sound_path(&s, -1) == NULL);
}

static void test_events(void) {
    rec r = {{0}};
    es2_shell_sound_player p = {&r, r_play, r_vol, r_stop};
    es2_shell_sounds s;
    es2_shell_prefs_session prefs;
    uint8_t out[ES2_SHELL_PREFS_SIZE];
    es2_shell_prefs_session_open(NULL, &prefs);
    prefs.current[0] = prefs.current[1] = 1;
    es2_shell_sounds_startup(&s, &p, 1, &prefs, out);
    const uint8_t *pr = prefs.current;

    /* No focus yet: effects refused. */
    CHECK(es2_shell_sounds_effect(&s, pr, ES2_SHELL_SOUND_GM_69) == 0);
    es2_shell_sounds_focus_gain(&s);
    CHECK(strcmp(r.log, "play 3 0 1;") == 0);
    es2_shell_sounds_focus_gain(&s); /* already focused: nothing */
    CHECK(strcmp(r.log, "play 3 0 1;") == 0);
    r.log[0] = 0;

    es2_shell_button_press b = {ES2_SHELL_BUTTON_TEXT, 2, 1, 0, 0, 0};
    CHECK(es2_shell_sounds_button(&s, pr, &b) == 1);
    b.press = 4; CHECK(es2_shell_sounds_button(&s, pr, &b) == 1);
    b.press = 1; CHECK(es2_shell_sounds_button(&s, pr, &b) == 0);
    b.press = 2; b.extra = 1; CHECK(es2_shell_sounds_button(&s, pr, &b) == 0);
    b.extra = 0; b.enabled = 0; CHECK(es2_shell_sounds_button(&s, pr, &b) == 0);
    b = (es2_shell_button_press){ES2_SHELL_BUTTON_IMAGE, 2, 1, 7, 0, 0};
    CHECK(es2_shell_sounds_button(&s, pr, &b) == 1); /* +0x25 not tested */
    b.press = 4; CHECK(es2_shell_sounds_button(&s, pr, &b) == 0);
    b.press = 2; b.movie_open = 1; CHECK(es2_shell_sounds_button(&s, pr, &b) == 0);
    b.movie_open = 0; b.movie_sequence = 1; CHECK(es2_shell_sounds_button(&s, pr, &b) == 0);
    b = (es2_shell_button_press){ES2_SHELL_BUTTON_IMAGE_SUB, 2, 1, 7, 1, 1};
    CHECK(es2_shell_sounds_button(&s, pr, &b) == 1);
    b.press = 4; CHECK(es2_shell_sounds_button(&s, pr, &b) == 0);
    b.press = 2; b.button_class = 9; CHECK(es2_shell_sounds_button(&s, pr, &b) == 0);
    CHECK(strcmp(r.log, "play 0 100 0;play 0 100 0;play 0 100 0;play 0 100 0;") == 0);
    r.log[0] = 0;

    CHECK(es2_shell_sounds_bay_change(&s, pr, 3, 3) == 0);
    CHECK(es2_shell_sounds_bay_change(&s, pr, 0, 2) == 1);
    CHECK(strcmp(r.log, "play 1 100 0;") == 0);
    r.log[0] = 0;

    /* Effects off (byte 1 = 0): background step consumes the once-flag silently. */
    prefs.current[1] = 0;
    CHECK(es2_shell_sounds_button(&s, pr, &(es2_shell_button_press){0, 2, 1, 0, 0, 0}) == 0);
    CHECK(es2_shell_sounds_background_step(&s, pr) == 0);
    prefs.current[1] = 1;
    CHECK(es2_shell_sounds_background_step(&s, pr) == 0);
    CHECK(r.log[0] == 0);
    s.lswitch_done = 0;
    CHECK(es2_shell_sounds_background_step(&s, pr) == 1);
    CHECK(es2_shell_sounds_background_step(&s, pr) == 0);
    CHECK(strcmp(r.log, "play 2 100 0;") == 0);
    CHECK(es2_shell_sounds_effect(&s, pr, ES2_SHELL_SOUND_MUSIC) == 0);
    r.log[0] = 0;

    /* Focus loss stops everything; effects refused; gain restarts the music. */
    es2_shell_sounds_focus_lose(&s);
    es2_shell_sounds_focus_lose(&s);
    CHECK(es2_shell_sounds_bay_change(&s, pr, 0, 1) == 0);
    s.music_volume = 57;
    es2_shell_sounds_focus_gain(&s);
    CHECK(strcmp(r.log, "stop;play 3 57 1;") == 0);
}

static void test_ramp(void) {
    rec r = {{0}};
    es2_shell_sound_player p = {&r, r_play, r_vol, r_stop};
    es2_shell_sounds s;
    es2_shell_prefs_session prefs;
    es2_shell_sound_ramp ramp;
    uint8_t out[ES2_SHELL_PREFS_SIZE];
    es2_shell_prefs_session_open(NULL, &prefs);
    es2_shell_sounds_startup(&s, &p, 1, &prefs, out);
    /* Byte 0 = 0: no ramp. */
    CHECK(es2_shell_sounds_ramp_begin(&s, prefs.current, 1, 0, &ramp) == 0);
    prefs.current[0] = 1;
    CHECK(es2_shell_sounds_ramp_begin(&s, prefs.current, 1, 1000, &ramp) == 1);
    CHECK(es2_shell_sounds_ramp_tick(&s, &ramp, 1010) == 1 && s.music_volume == 0); /* not more than 10 ms */
    CHECK(es2_shell_sounds_ramp_tick(&s, &ramp, 1011) == 1 && s.music_volume == 1);
    uint32_t t = 1011;
    int calls = 0;
    while (es2_shell_sounds_ramp_tick(&s, &ramp, t += 16)) calls++;
    CHECK(s.music_volume == 100 && calls == 98);
    CHECK(strstr(r.log, "vol 3 1;vol 3 2;") == r.log && strstr(r.log, "vol 3 100;"));
    CHECK(es2_shell_sounds_ramp_tick(&s, &ramp, t += 16) == 0 && s.music_volume == 100);
    /* Fade-out ends at 1. */
    CHECK(es2_shell_sounds_ramp_begin(&s, prefs.current, 0, t, &ramp) == 1);
    while (es2_shell_sounds_ramp_tick(&s, &ramp, t += 11)) {}
    CHECK(s.music_volume == 1);
    /* Tick counter wrap. */
    s.music_volume = 50;
    CHECK(es2_shell_sounds_ramp_begin(&s, prefs.current, 1, 0xfffffffau, &ramp) == 1);
    es2_shell_sounds_ramp_tick(&s, &ramp, 5);
    CHECK(s.music_volume == 51);
    /* Not active: no ramp. */
    es2_shell_sounds_startup(&s, &p, 0, &prefs, out);
    CHECK(es2_shell_sounds_ramp_begin(&s, prefs.current, 1, 0, &ramp) == 0);
}

/* Fake audio device. */
typedef struct fstream { uint32_t rate; int ch, bits; size_t queued; float gain; int clears, puts; } fstream;
typedef struct fdev { fstream s[8]; int n, closed; } fdev;
static void *f_open(void *c, uint32_t rate, int ch, int bits) {
    fdev *d = c;
    if (d->n >= 8) return NULL;
    d->s[d->n] = (fstream){rate, ch, bits, 0, -1.0f, 0, 0};
    return &d->s[d->n++];
}
static void f_close(void *c, void *s) { (void)s; ((fdev *)c)->closed++; }
static void f_clear(void *c, void *s) { (void)c; ((fstream *)s)->queued = 0; ((fstream *)s)->clears++; }
static void f_put(void *c, void *s, const uint8_t *d, size_t n) { (void)c; (void)d; ((fstream *)s)->queued += n; ((fstream *)s)->puts++; }
static size_t f_queued(void *c, void *s) { (void)c; return ((fstream *)s)->queued; }
static void f_gain(void *c, void *s, float g) { (void)c; ((fstream *)s)->gain = g; }

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static size_t make_wav(uint8_t *b, uint32_t rate, size_t data) {
    memset(b, 0, 0x2c + data);
    memcpy(b, "RIFF", 4);
    put32(b + 4, (uint32_t)(0x24 + data));
    memcpy(b + 8, "WAVEfmt ", 8);
    put32(b + 0x10, 16);
    b[0x14] = 1; b[0x16] = 1;
    put32(b + 0x18, rate);
    put32(b + 0x1c, rate);
    b[0x20] = 1; b[0x22] = 8;
    memcpy(b + 0x24, "data", 4);
    put32(b + 0x28, (uint32_t)data);
    for (size_t i = 0; i < data; i++) b[0x2c + i] = (uint8_t)(0x80 + i);
    return 0x2c + data;
}

static void test_wav(void) {
    uint8_t b[0x2c + 300];
    es2_shell_wav w;
    size_t n = make_wav(b, 22050, 300);
    CHECK(es2_shell_wav_parse((es2_span){b, n}, &w) == 1);
    CHECK(w.rate == 22050 && w.channels == 1 && w.bits == 8 && w.samples.data == b + 0x2c && w.samples.size == 256);
    put32(b + 0x28, 0x10); /* below 0x2c: length 0 */
    CHECK(es2_shell_wav_parse((es2_span){b, n}, &w) == 1 && w.samples.size == 0);
    put32(b + 0x28, 300 + 0x2c + 1); /* runs past the file */
    CHECK(es2_shell_wav_parse((es2_span){b, n}, &w) == 0);
    put32(b + 0x28, 300); b[0x22] = 12;
    CHECK(es2_shell_wav_parse((es2_span){b, n}, &w) == 0);
    CHECK(es2_shell_wav_parse((es2_span){b, 0x2b}, &w) == 0);
    b[0] = 'X';
    CHECK(es2_shell_wav_parse((es2_span){b, n}, &w) == 1 && w.rate == 11025 && w.bits == 8 && w.samples.size == n);
    CHECK(es2_shell_audio_gain(100) == 1.0f && es2_shell_audio_gain(0) == 0.0f);
    CHECK(es2_shell_audio_gain(1) == 327.0f / 32767.0f);
    CHECK(es2_shell_audio_gain(50) == 16383.0f / 32767.0f);
}

static void test_player(void) {
    static uint8_t files[5][0x2c + 2000];
    size_t sizes[5];
    sizes[0] = make_wav(files[0], 22050, 100);
    sizes[1] = make_wav(files[1], 22050, 200);
    sizes[2] = make_wav(files[2], 22050, 300);
    sizes[3] = make_wav(files[3], 11025, 2000);
    sizes[4] = make_wav(files[4], 11025, 1500);
    es2_vfs_file loose[5] = {
        {"HMI\\GM_69.WAV", {files[0], sizes[0]}}, {"HMI\\BPTLT2.WAV", {files[1], sizes[1]}},
        {"HMI\\LSWITCH2.WAV", {files[2], sizes[2]}}, {"HMI\\SHELL1.WAV", {files[3], sizes[3]}},
        {"HMI\\SHELL2.WAV", {files[4], sizes[4]}}};
    es2_vfs_source src = {.kind = ES2_VFS_LOOSE, .data.loose = {loose, 5}};
    es2_vfs vfs;
    CHECK(es2_vfs_open(&src, 1, &vfs, NULL) == ES2_OK);

    fdev d = {0};
    es2_shell_audio_device dev = {&d, f_open, f_close, f_clear, f_put, f_queued, f_gain};
    es2_shell_audio a;
    es2_shell_sounds s;
    es2_shell_prefs_session prefs;
    uint8_t out[ES2_SHELL_PREFS_SIZE];
    es2_shell_prefs_session_open(NULL, &prefs); /* byte 5 = 0 -> shell2 */
    prefs.current[0] = prefs.current[1] = 1;
    es2_shell_sounds_startup(&s, NULL, 1, &prefs, out);
    CHECK(es2_shell_audio_open(&a, &dev, &vfs, &s) == 4);
    CHECK(d.n == 4 && d.s[3].rate == 11025 && d.s[0].rate == 22050 && d.s[3].bits == 8 && d.s[3].ch == 1);
    s.player = es2_shell_audio_player(&a);

    es2_shell_sounds_focus_gain(&s);
    CHECK(d.s[3].queued == 1500 - 0x2c && d.s[3].gain == 0.0f);
    es2_shell_sounds_effect(&s, prefs.current, ES2_SHELL_SOUND_GM_69);
    CHECK(d.s[0].queued == 100 - 0x2c && d.s[0].gain == 1.0f);
    es2_shell_sounds_effect(&s, prefs.current, ES2_SHELL_SOUND_GM_69); /* restart */
    CHECK(d.s[0].queued == 100 - 0x2c && d.s[0].clears == 2);
    /* Service: the loop is fed while less than one pass is queued. */
    es2_shell_audio_service(&a);
    CHECK(d.s[3].puts == 1);
    d.s[3].queued = 10;
    es2_shell_audio_service(&a);
    CHECK(d.s[3].puts == 2 && d.s[3].queued == 10 + 1500 - 0x2c);
    d.s[0].queued = 0;
    es2_shell_audio_service(&a);
    CHECK(!a.playing[0] && d.s[0].puts == 2);
    /* Ramp gains. */
    es2_shell_sound_ramp ramp;
    es2_shell_sounds_ramp_begin(&s, prefs.current, 1, 0, &ramp);
    es2_shell_sounds_ramp_tick(&s, &ramp, 11);
    CHECK(d.s[3].gain == 327.0f / 32767.0f);
    /* Focus loss clears every stream and stops the loop feed. */
    es2_shell_sounds_focus_lose(&s);
    CHECK(d.s[3].queued == 0 && d.s[0].queued == 0 && !a.playing[3]);
    es2_shell_audio_service(&a);
    CHECK(d.s[3].queued == 0);
    es2_shell_sounds_focus_gain(&s);
    CHECK(d.s[3].queued == 1500 - 0x2c && a.playing[3] && a.looping[3]);
    es2_shell_audio_close(&a);
    CHECK(d.closed == 4);

    /* Missing samples stay silent. */
    es2_vfs_source none = {.kind = ES2_VFS_LOOSE, .data.loose = {loose, 1}};
    CHECK(es2_vfs_open(&none, 1, &vfs, NULL) == ES2_OK);
    fdev d2 = {0};
    dev.ctx = &d2;
    CHECK(es2_shell_audio_open(&a, &dev, &vfs, &s) == 1);
    s.player = es2_shell_audio_player(&a);
    CHECK(es2_shell_sounds_bay_change(&s, prefs.current, 0, 1) == 1 && d2.s[0].queued == 0);
    es2_shell_audio_close(&a);
}

static void test_real(void) {
    const char *root = getenv("ES2_DATA_ROOT");
    if (!root || !*root) { printf("SKIP real: ES2_DATA_ROOT not set\n"); return; }
    char path[1024];
    snprintf(path, sizeof path, "%s/VOL/SHLSOUND.VOL", root);
    FILE *f = fopen(path, "rb");
    if (!f) { printf("SKIP real: %s missing\n", path); return; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *bytes = malloc((size_t)size);
    CHECK(bytes && fread(bytes, 1, (size_t)size, f) == (size_t)size);
    fclose(f);
    es2_vol vol;
    es2_vfs vfs;
    CHECK(es2_vol_open((es2_span){bytes, (size_t)size}, &vol, NULL) == ES2_OK);
    es2_vfs_source src = {.kind = ES2_VFS_VOL, .data.vol = &vol};
    CHECK(es2_vfs_open(&src, 1, &vfs, NULL) == ES2_OK);
    static const struct { const char *path; uint32_t rate; size_t played; } want[] = {
        {"hmi\\gm_69.wav", 22050, 1372}, {"hmi\\bptlt2.wav", 22050, 4804}, {"hmi\\lswitch2.wav", 22050, 84832},
        {"hmi\\shell1.wav", 11025, 1003060}, {"hmi\\shell2.wav", 11025, 1029977}};
    for (int i = 0; i < 5; i++) {
        es2_vfs_copy c;
        es2_shell_wav w;
        CHECK(es2_vfs_find(&vfs, want[i].path, &c, NULL) == ES2_OK);
        CHECK(es2_shell_wav_parse(c.payload, &w) == 1);
        CHECK(w.rate == want[i].rate && w.bits == 8 && w.channels == 1 && w.samples.size == want[i].played);
    }
    free(bytes);
    printf("real: 5 samples checked\n");
}

int main(void) {
    test_startup();
    test_events();
    test_ramp();
    test_wav();
    test_player();
    test_real();
    printf("%u checks, %u failures\n", checks, failures);
    return failures != 0;
}
