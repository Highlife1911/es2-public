/* Shell sound logic. Contract: docs/reverse-engineering/audio.md,
 * "the VSHELL shell audio contract" and "the own shell's sounds". */
#include "es2/shell/sounds.h"

#include <string.h>

const char *es2_shell_sound_path(const es2_shell_sounds *s, int sound) {
    switch (sound) {
    case ES2_SHELL_SOUND_GM_69: return "hmi\\gm_69.wav";
    case ES2_SHELL_SOUND_BPTLT2: return "hmi\\bptlt2.wav";
    case ES2_SHELL_SOUND_LSWITCH2: return "hmi\\lswitch2.wav";
    case ES2_SHELL_SOUND_MUSIC: return s ? s->music : NULL;
    default: return NULL;
    }
}

int es2_shell_sounds_startup(es2_shell_sounds *s, const es2_shell_sound_player *player, int sound_flag,
                             es2_shell_prefs_session *prefs, uint8_t out[ES2_SHELL_PREFS_SIZE]) {
    if (!s) return 0;
    memset(s, 0, sizeof *s);
    if (player) s->player = *player;
    /* Original: VSHELL.EXE 0x42f2f4 step 1: no sound flag, no object. */
    if (!sound_flag || !prefs) return 0;
    s->active = 1;
    /* 0x42f40f..0x42f44f: byte 5 != 0 -> shell1, = 0 -> shell2. */
    s->music = prefs->current[ES2_SHELL_PREFS_MUSIC_CHOICE] ? "hmi\\shell1.wav" : "hmi\\shell2.wav";
    /* 0x40de35(5, 0): byte 5 xor 1; 0x40dd6e(1) commit; 0x40dd0a write. */
    prefs->current[ES2_SHELL_PREFS_MUSIC_CHOICE] ^= 1;
    es2_shell_prefs_accept(prefs, out);
    s->music_volume = 0;
    return 1;
}

void es2_shell_sounds_focus_gain(es2_shell_sounds *s) {
    /* Original: VSHELL.EXE 0x42f5d3 */
    if (!s || !s->active || s->focus) return;
    s->focus = 1;
    if (s->player.play) s->player.play(s->player.ctx, ES2_SHELL_SOUND_MUSIC, s->music_volume, 1);
}

void es2_shell_sounds_focus_lose(es2_shell_sounds *s) {
    /* Original: VSHELL.EXE 0x42f6a8 */
    if (!s || !s->active || !s->focus) return;
    if (s->player.stop_all) s->player.stop_all(s->player.ctx);
    s->focus = 0;
}

int es2_shell_sounds_effect(es2_shell_sounds *s, const uint8_t prefs[ES2_SHELL_PREFS_SIZE], int sound) {
    /* Original: VSHELL.EXE 0x42f501 / 0x42f547 / 0x42f58d */
    if (!s || !prefs || sound < 0 || sound >= ES2_SHELL_SOUND_MUSIC) return 0;
    if (!s->active || !prefs[ES2_SHELL_PREFS_EFFECTS] || !s->focus) return 0;
    if (s->player.play) s->player.play(s->player.ctx, sound, 100, 0);
    return 1;
}

int es2_shell_sounds_button(es2_shell_sounds *s, const uint8_t prefs[ES2_SHELL_PREFS_SIZE],
                            const es2_shell_button_press *p) {
    if (!s || !p || !p->enabled || !s->active) return 0; /* sound flag 0x482356 */
    switch (p->button_class) {
    case ES2_SHELL_BUTTON_TEXT: /* 0x40a0ca..0x40a116 */
        if ((p->press != 2 && p->press != 4) || p->extra != 0) return 0;
        break;
    case ES2_SHELL_BUTTON_IMAGE: /* 0x40a4e9..0x40a53d */
        if (p->press != 2 || p->movie_open || p->movie_sequence) return 0;
        break;
    case ES2_SHELL_BUTTON_IMAGE_SUB: /* 0x40a776..0x40a796 */
        if (p->press != 2) return 0;
        break;
    default: return 0;
    }
    return es2_shell_sounds_effect(s, prefs, ES2_SHELL_SOUND_GM_69);
}

int es2_shell_sounds_bay_change(es2_shell_sounds *s, const uint8_t prefs[ES2_SHELL_PREFS_SIZE],
                                int old_screen, int new_screen) {
    if (old_screen == new_screen) return 0;
    return es2_shell_sounds_effect(s, prefs, ES2_SHELL_SOUND_BPTLT2);
}

int es2_shell_sounds_background_step(es2_shell_sounds *s, const uint8_t prefs[ES2_SHELL_PREFS_SIZE]) {
    /* Original: VSHELL.EXE 0x431849..0x43185e */
    if (!s || s->lswitch_done) return 0;
    s->lswitch_done = 1;
    return es2_shell_sounds_effect(s, prefs, ES2_SHELL_SOUND_LSWITCH2);
}

int es2_shell_sounds_ramp_begin(const es2_shell_sounds *s, const uint8_t prefs[ES2_SHELL_PREFS_SIZE], int up,
                                uint32_t now, es2_shell_sound_ramp *ramp) {
    if (!s || !prefs || !ramp || !s->active || !prefs[ES2_SHELL_PREFS_MUSIC]) return 0;
    ramp->up = up != 0;
    ramp->last = now;
    return 1;
}

int es2_shell_sounds_ramp_tick(es2_shell_sounds *s, es2_shell_sound_ramp *ramp, uint32_t now) {
    /* Original: VSHELL.EXE 0x42f894 (in, while < 100) and 0x42f7f0 (out, while > 1). */
    if (!s || !ramp) return 0;
    if (ramp->up ? s->music_volume >= 100 : s->music_volume <= 1) return 0;
    if ((uint32_t)(now - ramp->last) > 10u) {
        ramp->last = now;
        s->music_volume += ramp->up ? 1 : -1;
        if (s->player.set_volume) s->player.set_volume(s->player.ctx, ES2_SHELL_SOUND_MUSIC, s->music_volume);
    }
    return ramp->up ? s->music_volume < 100 : s->music_volume > 1;
}
