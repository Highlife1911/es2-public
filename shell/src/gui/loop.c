/* Command line, window-procedure branches and main loop of the own
 * shell. Contract: vshell_process.md sections 3, 7, 8 and "the own shell
 * process frame". */
#include "es2/shell/gui/loop.h"

#include "es2/core/rtl.h"

#include <ctype.h>
#include <string.h>

/* Original: VSHELL.EXE 0x40796c(0) defaults, parser 0x4015d8, 0x401bc1..0x401bce */
void es2_gui_parse_args(int argc, char *const *argv, es2_gui_args *out) {
    memset(out, 0, sizeof *out);
    out->movies = 1;
    out->language = -1;
    out->keyboard = 1;
    out->sound = 1;
    out->l_word = 1;
    long exit_code = 0;
    for (int i = 1; i < argc; i++) {
        const char *t = argv[i];
        if (!t || (t[0] != '-' && t[0] != '/') || !t[1]) continue;
        char c = t[1];
        if (c == 'X') { /* 0x401768: upper case only, (short) CW3220 _atol */
            exit_code = (short)es2_rtl_atol(t + 2);
            continue;
        }
        switch (tolower((unsigned char)c)) {
        case 'a': out->movies = 0; break;
        case '@': out->cheat = 1; break;
        case 'f': out->language = 1; break;
        case 'g': out->language = 2; break;
        case 'e':
            if (!strcmp(t, "-eggplant")) out->eggplant = 1;
            else out->language = 3;
            break;
        case 'm': out->m_flag = 1; break;
        case 'k': out->keyboard = 0; break;
        case 'r': break; /* entry mode 3, overwritten at 0x401bc6 */
        case 's': out->sound = 0; break;
        case 'l': out->l_word = 0; break;
        case 'v': out->version = 1; out->sound = 0; break;
        case '?': out->usage = 1; out->sound = 0; break;
        default: break;
        }
    }
    out->entry_mode = (int)exit_code;
}

void es2_gui_shell_init(es2_gui_shell *shell, const es2_gui_args *args, const es2_gui_platform *platform,
                        const es2_gui_hooks *hooks) {
    memset(shell, 0, sizeof *shell);
    es2_gui_frame_init(&shell->frame);
    es2_gui_input_init(&shell->input);
    if (args) shell->input.keyboard = args->keyboard;
    /* 0x401bd4..0x401bed: entry mode 3 or 4 sets the gate before the window's first paint */
    if (args && (args->entry_mode == 3 || args->entry_mode == 4)) shell->frame.realize_gate = 1;
    shell->focused = 1; /* DATA initial value of 0x46c288 */
    shell->window_ready = 1;
    if (platform) shell->platform = *platform;
    if (hooks) shell->hooks = *hooks;
}

static void present(es2_gui_shell *shell, es2_gui_present_kind kind, const es2_gui_rect *rects, int count) {
    shell->presents++;
    if (shell->platform.present) shell->platform.present(shell->platform.context, shell, kind, rects, count);
}

void es2_gui_shell_present_full(es2_gui_shell *shell) {
    es2_gui_screen_present_full(&shell->screen, &shell->frame);
    present(shell, ES2_GUI_PRESENT_FULL, NULL, 0);
}

void es2_gui_shell_clear(es2_gui_shell *shell) {
    es2_gui_clear(&shell->screen, &shell->frame);
    present(shell, ES2_GUI_PRESENT_CLEAR, NULL, 0);
}

/* Original: VSHELL.EXE 0x408d2f */
void es2_gui_set_busy(es2_gui_shell *shell, int busy) {
    shell->busy = busy != 0;
    if (shell->platform.cursor) shell->platform.cursor(shell->platform.context, shell->busy);
}

/* Original: VSHELL.EXE 0x405455 (WM_SETFOCUS), 0x405443 (WM_KILLFOCUS) */
void es2_gui_on_focus(es2_gui_shell *shell, int gained) {
    if (gained) {
        if (shell->window_ready && shell->input.movie_open) es2_gui_shell_present_full(shell);
        shell->focused = 1;
        if (!shell->input.movie_open && shell->hooks.focus) shell->hooks.focus(shell->hooks.context, shell, 1);
    } else {
        shell->focused = 0;
        if (shell->hooks.focus) shell->hooks.focus(shell->hooks.context, shell, 0);
    }
}

/* Original: VSHELL.EXE 0x4057ab: a re-present of the existing frame, never a redraw */
void es2_gui_on_paint(es2_gui_shell *shell) {
    if (shell->frame.realize_gate && !shell->input.movie_open) es2_gui_shell_present_full(shell);
}

/* Original: VSHELL.EXE 0x4050a3 */
void es2_gui_on_close(es2_gui_shell *shell) {
    if (shell->input.interactive) shell->quit = 1;
}

void es2_gui_on_mouse(es2_gui_shell *shell, es2_gui_mouse_message msg, int32_t x, int32_t y, int mk_left, int mk_right) {
    es2_gui_mouse_message_in(&shell->input, msg, x, y, mk_left, mk_right);
}

/* Original: VSHELL.EXE 0x405263..0x405396 (the actions); 0x407636 toggle, 0x4077df leave */
void es2_gui_on_key(es2_gui_shell *shell, unsigned vk, int up, int shift, int alt, int ctrl) {
    es2_gui_system_action action = es2_gui_key_message_in(&shell->input, vk, up, shift, alt, ctrl);
    switch (action) {
    case ES2_GUI_SYS_LEAVE_FULLSCREEN:
        if (shell->fullscreen) {
            shell->fullscreen = 0;
            if (shell->platform.fullscreen) shell->platform.fullscreen(shell->platform.context, 0);
        }
        break;
    case ES2_GUI_SYS_TOGGLE_FULLSCREEN:
        shell->fullscreen = !shell->fullscreen;
        if (shell->platform.fullscreen) shell->platform.fullscreen(shell->platform.context, shell->fullscreen);
        break;
    default: break;
    }
    if (action != ES2_GUI_SYS_NONE && shell->hooks.system) shell->hooks.system(shell->hooks.context, shell, action);
}

/* Original: VSHELL.EXE main loop 0x402018..0x402191 entered at its pump 0x402181 */
int es2_gui_step(es2_gui_shell *shell) {
    if (shell->platform.pump && shell->platform.pump(shell->platform.context, shell)) return 0;
    if (shell->quit) return 0;
    shell->iterations++;
    if (!shell->focused) return 1; /* 0x402018: idle while unfocused */
    es2_gui_poll(&shell->input);   /* 0x41e0bf */
    if (shell->quit) return 0;
    if (shell->hooks.timer) shell->hooks.timer(shell->hooks.context, shell); /* 0x46848c: 0x46a508, */
    es2_gui_drain(&shell->input, shell->hooks.handle, shell->hooks.context); /* then 0x469ee4 */
    if (shell->quit) return 0;
    es2_gui_palette_apply(&shell->frame); /* 0x407c1b */
    if (shell->quit) return 0;
    int recoloured = es2_gui_recolour(&shell->screen, &shell->frame);
    if (shell->frame.dirty_count) { /* 0x41fe49 */
        es2_gui_rect rects[ES2_GUI_DIRTY_MAX];
        int n = shell->frame.dirty_count;
        memcpy(rects, shell->frame.dirty, (size_t)n * sizeof rects[0]);
        es2_gui_flush(&shell->screen, &shell->frame);
        present(shell, ES2_GUI_PRESENT_DIRTY, rects, n);
    } else if (recoloured) {
        present(shell, ES2_GUI_PRESENT_FULL, NULL, 0);
    }
    if (shell->quit) return 0;
    if (shell->hooks.movie) shell->hooks.movie(shell->hooks.context, shell);
    if (shell->quit) return 0;
    if (shell->hooks.map) shell->hooks.map(shell->hooks.context, shell);
    return !shell->quit;
}

void es2_gui_test_pattern(es2_gui_shell *shell) {
    uint8_t records[256][4];
    for (int i = 0; i < 256; i++) { /* own colours: 16 hues x 16 brightness steps, 6-bit DPL scale */
        int hue = i >> 4, level = (i & 15) * 4 + 3;
        records[i][0] = (uint8_t)(hue & 1 ? level : level / 4);
        records[i][1] = (uint8_t)(hue & 2 ? level : level / 4);
        records[i][2] = (uint8_t)(hue & 4 ? level : level / 4);
        records[i][3] = 1;
    }
    for (int y = 0; y < ES2_GUI_HEIGHT; y++)
        for (int x = 0; x < ES2_GUI_WIDTH; x++)
            shell->frame.pixels[y * ES2_GUI_WIDTH + x] = (uint8_t)(((y / 15) % 16) * 16 + (x / 20) % 16);
    shell->frame.realize_gate = 1;
    es2_gui_palette_select(&shell->frame, (const uint8_t(*)[4])records);
    es2_gui_rect all = {0, 0, ES2_GUI_WIDTH - 1, ES2_GUI_HEIGHT - 1};
    es2_gui_invalidate_window(&shell->frame, all);
}
