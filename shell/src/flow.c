/* The shell flow (es2/shell/flow.h). No file I/O and no output: the
 * effects that touch files go through the host's callbacks.
 *
 * Contract: docs/reverse-engineering/vshell_ui.md section 18; addresses are
 * VSHELL.EXE's (vshell_ui.md sections 1 to 4, 9 to 11; vshell_state.md). */
#include "es2/shell/flow.h"
#include <string.h>

void es2_shell_flow_init(es2_shell_flow *flow, const es2_shell_flow_host *host, const es2_span *prefs_file) {
    memset(flow, 0, sizeof *flow);
    if (host) flow->host = *host;
    /* game init 0x40e736: prefs blocks 0x40dc44, mode word := byte +0x2a
     * (0x40e751); loaded flag 0; the final word starts at 0 */
    es2_shell_prefs_session_open(prefs_file, &flow->prefs);
    flow->mode = flow->prefs.current[ES2_SHELL_PREFS_MODE];
    flow->final_word = 0;
    flow->game = ES2_FLOW_GAME_NONE;
    /* image values: 0x4758b8 = -1, 0x474fde = 10, 0x474fdc = 0, 0x476248 = 0,
     * 0x48d424 = 0 (BSS) */
    flow->screen = ES2_FLOW_MAIN_MENU;
    flow->bay_word = ES2_FLOW_WORD_BAY;
    flow->slot = ES2_FLOW_SLOT_CAMPAIGN;
    flow->caller = ES2_FLOW_CALLER_MENU;
    flow->exit_code = ES2_FLOW_EXIT_QUIT;   /* the reset 0x401bce */
}

/* ---- helpers ------------------------------------------------------------- */
static int store_mode(es2_shell_flow *flow, uint8_t mode) {
    flow->mode = mode;   /* the mode word is set before the file is written */
    return flow->host.store_mode ? flow->host.store_mode(flow->host.ctx, mode) : 1;
}
static int save(es2_shell_flow *flow, int slot, const char *text) {
    if (flow->game == ES2_FLOW_GAME_NONE) return 0;   /* 0x40e947: nothing without a loaded game */
    return flow->host.save ? flow->host.save(flow->host.ctx, slot, text) : 0;
}
/* The load 0x40eaaa: on success the game is loaded with the slot's final word. */
static int load(es2_shell_flow *flow, int slot) {
    int16_t final_word = flow->final_word;
    int r = flow->host.load ? flow->host.load(flow->host.ctx, slot, &final_word) : 0;
    if (r != 0) flow->bay_select = 0;   /* hangar clear 0x410aaf (vshell_state.md) */
    if (r > 0) {
        flow->game = ES2_FLOW_GAME_SLOT;
        flow->final_word = final_word;
    }
    return r;
}
/* The bay show 0x43b7f2(8) + 0x43b758: screen word -1. */
static void show_bay(es2_shell_flow *flow) {
    flow->screen = ES2_FLOW_SERVICE_BAY;
    flow->bay_word = ES2_FLOW_WORD_BAY;
}
static void show_box(es2_shell_flow *flow, es2_shell_flow_box box, int value) {
    flow->box = box;
    flow->box_value = value;
}
static es2_shell_flow_result refuse(es2_shell_flow *flow, int why) {
    flow->refusal = why;
    return ES2_FLOW_REFUSED;
}
/* A loaded slot: final word 2 is the campaign in the bay (the host prepares
 * it); any other value shows the End of game box over the main menu. */
static es2_shell_flow_result campaign_from_slot(es2_shell_flow *flow) {
    if (flow->final_word != 2) {
        show_box(flow, ES2_FLOW_BOX_END_OF_GAME, flow->final_word);
        flow->screen = ES2_FLOW_MAIN_MENU;
        return ES2_FLOW_DONE;
    }
    if (flow->host.campaign && !flow->host.campaign(flow->host.ctx)) {
        flow->screen = ES2_FLOW_MAIN_MENU;
        return ES2_FLOW_FAILED;
    }
    flow->game = ES2_FLOW_GAME_CAMPAIGN;
    show_bay(flow);
    return ES2_FLOW_DONE;
}
/* Leaving the current bay screen 0x43a537 (jump table 0x43a54a): WEAPONS,
 * REPAIR and BUILD end in 0x412a37 with prefs +0x2d of the current block
 * (0x4825c9); ARMORY, CREW and MISSION only hide panels (no state). */
static void leave(es2_shell_flow *flow) {
    switch (flow->bay_word) {
    case ES2_FLOW_WORD_WEAPONS: case ES2_FLOW_WORD_REPAIR: case ES2_FLOW_WORD_BUILD:
        if (flow->host.rebalance) flow->host.rebalance(flow->host.ctx, flow->prefs.current[ES2_SHELL_PREFS_BUILD]);
        break;
    default:
        break;
    }
}
static int bay_word_of(es2_shell_flow_handler event) {
    switch (event) {
    case ES2_FLOW_BAY_SAVE: return ES2_FLOW_WORD_SAVE;
    case ES2_FLOW_BAY_WEAPONS: return ES2_FLOW_WORD_WEAPONS;
    case ES2_FLOW_BAY_REPAIR: return ES2_FLOW_WORD_REPAIR;
    case ES2_FLOW_BAY_BUILD: return ES2_FLOW_WORD_BUILD;
    case ES2_FLOW_BAY_ARMORY: return ES2_FLOW_WORD_ARMORY;
    case ES2_FLOW_BAY_CREW: return ES2_FLOW_WORD_CREW;
    case ES2_FLOW_BAY_MISSION: return ES2_FLOW_WORD_MISSION;
    default: return ES2_FLOW_WORD_NONE;
    }
}

/* ---- entry dispatch and results path ------------------------------------ */
es2_shell_flow_entry es2_shell_flow_entry_of(int state) {
    if (state == 3 || state == 4) return ES2_FLOW_ENTRY_RESULTS;   /* 0x4018a1 */
    if (state == 6) return ES2_FLOW_ENTRY_MENU;                    /* 0x4018c6 */
    return ES2_FLOW_ENTRY_INTRO;                                   /* 0x4018fd */
}

/* -X3/-X4: load slot 10 (0x40eaaa(10, 0), 11 in mode 0), then the results
 * path 0x40f09f and what follows it (vshell_ui.md sections 4.9, 11.2). */
static void results_path(es2_shell_flow *flow) {
    es2_shell_flow_results r;
    flow->screen = ES2_FLOW_MAIN_MENU;
    if (load(flow, ES2_FLOW_SLOT_CAMPAIGN) <= 0) {
        flow->outcome = ES2_FLOW_OUTCOME_NO_SLOT;
        return;
    }
    flow->game = ES2_FLOW_GAME_CAMPAIGN;
    memset(&r, 0, sizeof r);
    r.final_word = flow->final_word;
    if (!flow->host.results || !flow->host.results(flow->host.ctx, &r)) {
        flow->outcome = ES2_FLOW_OUTCOME_REJECTED;
        return;
    }
    flow->final_word = r.final_word;
    if (r.ending == ES2_SHELL_RESULTS_PLAYER_LOST || (r.ending == ES2_SHELL_RESULTS_ADVANCED && r.advance == 0)) {
        /* 0x40f480 (final word 3) and 0x40f603 (final word 0): the Replay box */
        show_box(flow, ES2_FLOW_BOX_REPLAY, r.ending == ES2_SHELL_RESULTS_PLAYER_LOST ? 3 : 0);
        flow->outcome = ES2_FLOW_OUTCOME_REPLAY;
    } else if (r.ending == ES2_SHELL_RESULTS_ADVANCED && r.advance == 2) {
        /* 0x40f62d: page word := 4 (the DEBUG dialog's Use Default
         * opens the mission screen on it, 0x43aee7(0x481108, 1)); then Use
         * Default: the next mission at once, the bay */
        flow->page = 4;
        if (flow->debug_cheat) {
            /* With -@ 0x413305 shows the dialog without the posted
             * click (0x41331d -> 0x4133d8); the answer prepares */
            flow->debug = ES2_FLOW_DEBUG_NEXT;
            flow->outcome = ES2_FLOW_OUTCOME_NEXT;
        } else if (flow->host.prepare_next && flow->host.prepare_next(flow->host.ctx)) {
            flow->outcome = ES2_FLOW_OUTCOME_NEXT;
            show_bay(flow);
        } else
            flow->outcome = ES2_FLOW_OUTCOME_NEXT_FAILED;
    } else if (r.ending == ES2_SHELL_RESULTS_ADVANCED && r.advance == 1) {
        /* 0x40f554: final word 1, slot 10 saved, the victory movies, main menu */
        save(flow, ES2_FLOW_SLOT_CAMPAIGN, NULL);
        show_box(flow, ES2_FLOW_BOX_END_OF_GAME, 1);
        flow->outcome = ES2_FLOW_OUTCOME_WON;
    } else if (r.ending == ES2_SHELL_RESULTS_OTHER_MODE) {
        flow->outcome = ES2_FLOW_OUTCOME_OTHER_MODE;
    } else {
        flow->outcome = ES2_FLOW_OUTCOME_STOPPED;
        flow->outcome_advance = r.advance;
    }
}

/* Use Default 0x44db5c / ACCEPT 0x44dc4d in mode 1 (0x44db97,
 * 0x44dc9c: mode word 1): the preparation 0x41cd61, the map, the briefing,
 * the slot-10 statistics, the bay, the mission screen on the page word
 * (0x43aee7(0x481108, 1)); no launch. */
es2_shell_flow_result es2_shell_flow_debug_answer(es2_shell_flow *flow, const char *mission) {
    es2_shell_flow_debug where = flow->debug;
    int ok;
    if (where == ES2_FLOW_DEBUG_NONE) return ES2_FLOW_IGNORED;
    flow->debug = ES2_FLOW_DEBUG_NONE;
    ok = flow->host.debug_prepare ? flow->host.debug_prepare(flow->host.ctx, mission)
         : !mission && flow->host.prepare_next && flow->host.prepare_next(flow->host.ctx);
    if (!ok) {
        flow->screen = ES2_FLOW_MAIN_MENU;
        if (where == ES2_FLOW_DEBUG_NEXT) flow->outcome = ES2_FLOW_OUTCOME_NEXT_FAILED;
        return ES2_FLOW_FAILED;
    }
    show_bay(flow);
    return ES2_FLOW_DONE;
}

es2_shell_flow_entry es2_shell_flow_start(es2_shell_flow *flow, int state) {
    es2_shell_flow_entry entry = es2_shell_flow_entry_of(state);
    flow->outcome = ES2_FLOW_OUTCOME_NONE;
    if (entry == ES2_FLOW_ENTRY_RESULTS) results_path(flow);
    else flow->screen = ES2_FLOW_MAIN_MENU;
    return entry;
}

int es2_shell_flow_finish(es2_shell_flow *flow) {
    /* loop end 0x402197: 0x40e933(10, 0), only with a loaded game */
    if (flow->game != ES2_FLOW_GAME_NONE) save(flow, ES2_FLOW_SLOT_CAMPAIGN, NULL);
    return flow->exit_code;
}

/* ---- controls ------------------------------------------------------------ */
int es2_shell_flow_enabled(const es2_shell_flow *flow, es2_shell_flow_handler event, int flag) {
    switch (event) {
    case ES2_FLOW_SLOT_SAVE:      /* 0x43807b: not naming, slot below 10, a game loaded */
        return !flow->naming && flow->slot >= 0 && flow->slot < ES2_FLOW_USER_SLOTS && flow->game != ES2_FLOW_GAME_NONE;
    case ES2_FLOW_SLOT_RESTORE:   /* 0x438126: slot below 10 with its flag; SAVE disables it while naming (0x4382bd) */
        return !flow->naming && flow->slot >= 0 && flow->slot < ES2_FLOW_USER_SLOTS && flag;
    case ES2_FLOW_SLOT_EXIT:      /* disabled by SAVE (0x438302), enabled by ACCEPT and CANCEL */
        return !flow->naming;
    case ES2_FLOW_SLOT_ACCEPT: case ES2_FLOW_SLOT_CANCEL:
        return flow->naming;      /* enabled by 0x437e62 only, disabled by the show 0x43a1eb, ACCEPT and CANCEL */
    case ES2_FLOW_BAY_REPAIR: case ES2_FLOW_BAY_BUILD: case ES2_FLOW_BAY_ARMORY:
        return flow->mode != 0;   /* 0x43b758: mode 0 enables WEAPONS and CREW only of the five editor
                                   * buttons; it does not touch MAIN MENU, SAVE and MISSION */
    default:
        return 1;
    }
}

void es2_shell_flow_select_slot(es2_shell_flow *flow, int slot) {
    flow->slot = slot;   /* 0x437fef */
}

es2_shell_flow_result es2_shell_flow_prefs_control(es2_shell_flow *flow, int control) {
    es2_error e;
    if (flow->screen != ES2_FLOW_PREFERENCES || flow->box != ES2_FLOW_BOX_NONE) return ES2_FLOW_IGNORED;
    if (control == ES2_SHELL_PREFS_FULL_SCREEN) {
        /* 0x437608 shows the Alert box only while windowed ([0x481f4c] = 0,
         * 0x437618..0x437628); only its ACCEPT 0x437678 stores */
        if (flow->host.full_screen && flow->host.full_screen(flow->host.ctx)) return ES2_FLOW_IGNORED;
        show_box(flow, ES2_FLOW_BOX_ALERT, 0);
        return ES2_FLOW_DONE;
    }
    return es2_shell_prefs_control(&flow->prefs, control, &e) == ES2_OK ? ES2_FLOW_DONE : ES2_FLOW_IGNORED;
}

void es2_shell_flow_game_created(es2_shell_flow *flow) {
    /* 0x40e8a5 in mode 0: final word 2 (0x40e8a8), loaded (0x40e92a) */
    flow->game = ES2_FLOW_GAME_SLOT;
    flow->final_word = 2;
    flow->bay_select = 0;   /* default hangar 0x410b11 (vshell_state.md) */
}

void es2_shell_flow_dbsim(es2_shell_flow *flow) {
    flow->exit_code = ES2_FLOW_EXIT_DBSIM;
    flow->quit = 1;
}

void es2_shell_flow_box_close(es2_shell_flow *flow) {
    show_box(flow, ES2_FLOW_BOX_NONE, 0);
}

/* ---- handlers ------------------------------------------------------------- */
static es2_shell_flow_result online_manual(es2_shell_flow *flow);   /* below */
static es2_shell_flow_result main_menu(es2_shell_flow *flow, es2_shell_flow_handler event) {
    int r;
    switch (event) {
    case ES2_FLOW_INSTANT_ACTION:      /* 0x431935: mode 0 (the practice writes are the host's) */
        flow->mode = 0;
        return ES2_FLOW_DONE;
    case ES2_FLOW_START_NEW_GAME:      /* 0x431a08: mode 1, Registration */
        if (!store_mode(flow, 1)) return ES2_FLOW_FAILED;
        flow->screen = ES2_FLOW_REGISTRATION;
        return ES2_FLOW_DONE;
    case ES2_FLOW_CONTINUE_GAME:       /* 0x431a73: mode 1, load slot 10, selected slot 10 */
        if (!store_mode(flow, 1)) return ES2_FLOW_FAILED;
        flow->slot = ES2_FLOW_SLOT_CAMPAIGN;
        r = load(flow, ES2_FLOW_SLOT_CAMPAIGN);
        if (r > 0) return campaign_from_slot(flow);
        if (r == 0) return ES2_FLOW_FAILED;
        /* slot 10 without flag: the final word in memory decides (0 when no
         * game was created or loaded: "The war is lost."); the loaded flag
         * stays as it was */
        if (flow->final_word != 2) {
            show_box(flow, ES2_FLOW_BOX_END_OF_GAME, flow->final_word);
            return ES2_FLOW_DONE;
        }
        if (flow->game == ES2_FLOW_GAME_CAMPAIGN) show_bay(flow);
        return ES2_FLOW_DONE;
    case ES2_FLOW_SAVE_RESTORE:        /* 0x431b27: mode 1, caller mark 0 */
        if (!store_mode(flow, 1)) return ES2_FLOW_FAILED;
        flow->caller = ES2_FLOW_CALLER_MENU;
        flow->screen = ES2_FLOW_SAVED_GAMES;
        return ES2_FLOW_DONE;
    case ES2_FLOW_PREFERENCES_BUTTON:  /* 0x431b9b */
        flow->screen = ES2_FLOW_PREFERENCES;
        return ES2_FLOW_DONE;
    case ES2_FLOW_PRACTICE_MISSIONS:   /* 0x431f3a: mode 0 (the show's writes are the host's) */
        flow->mode = 0;
        flow->screen = ES2_FLOW_PRACTICE;
        return ES2_FLOW_DONE;
    case ES2_FLOW_VIEW_DEMO:           /* 0x431bfe: exit 5, quit flag */
        flow->exit_code = ES2_FLOW_EXIT_DEMO;
        flow->quit = 1;
        return ES2_FLOW_DONE;
    case ES2_FLOW_QUIT:                /* 0x431db6: quit flag */
        flow->exit_code = ES2_FLOW_EXIT_QUIT;
        flow->quit = 1;
        return ES2_FLOW_DONE;
    case ES2_FLOW_ONLINE_MANUAL:       /* 0x431e1b -> 0x431e79; the screen stays */
        return online_manual(flow);
    default:
        return ES2_FLOW_IGNORED;
    }
}

/* The help routine 0x431e79 (ONLINE MANUAL 0x431e1b, the bay's online image
 * 0x43a5bd): preference byte 6 := 0 (0x40ddfe(6, 0, 1), all callbacks null),
 * committed block := current (0x40dd6e(0)), the 54 bytes written (0x40dd0a),
 * then sound focus lost (0x42f8ff), language.cfg and WinHelpA (the host). The
 * rest of VSHELL's routine runs whatever the write did. */
static es2_shell_flow_result online_manual(es2_shell_flow *flow) {
    uint8_t out[ES2_SHELL_PREFS_SIZE];
    es2_error e;
    int wrote = 1;
    es2_shell_prefs_control(&flow->prefs, ES2_SHELL_PREFS_WINDOW, &e);
    es2_shell_prefs_accept(&flow->prefs, out);
    if (flow->host.write_prefs && !flow->host.write_prefs(flow->host.ctx, out)) wrote = 0;
    if (flow->host.online_manual) flow->host.online_manual(flow->host.ctx);
    return wrote ? ES2_FLOW_DONE : ES2_FLOW_FAILED;
}

static es2_shell_flow_result registration(es2_shell_flow *flow, es2_shell_flow_handler event, const char *name) {
    int16_t final_word = flow->final_word;
    int r;
    switch (event) {
    case ES2_FLOW_SKILL:               /* 0x43c6ad: (value + 1) mod 4 */
        flow->skill = (flow->skill + 1) & 3;
        return ES2_FLOW_DONE;
    case ES2_FLOW_REGISTRATION_CANCEL: /* 0x43c728: hide, main menu */
        flow->screen = ES2_FLOW_MAIN_MENU;
        return ES2_FLOW_DONE;
    case ES2_FLOW_REGISTRATION_ACCEPT: /* 0x43c78b: 0x40e8a5(name, skill), Use Default, bay */
        flow->screen = ES2_FLOW_MAIN_MENU;
        r = flow->host.new_game ? flow->host.new_game(flow->host.ctx, name ? name : "", (uint16_t)flow->skill, &final_word) : 0;
        if (r != 0) flow->bay_select = 0;   /* default hangar 0x410b11/0x410c58 (vshell_state.md) */
        if (r < 0) {
            flow->game = ES2_FLOW_GAME_SLOT;
            flow->final_word = final_word;
        }
        if (r <= 0) return ES2_FLOW_FAILED;
        flow->game = ES2_FLOW_GAME_CAMPAIGN;
        flow->final_word = 2;   /* 0x40e8a8 */
        /* 0x43c7c6..0x43c7dc: page word := 0 when the CAREER position word
         * 0x46fc5a is 0, else 1; the mode-1 new game sets the position to
         * (1, 0) (0x413053), so ACCEPT always stores 0 */
        flow->page = 0;
        if (flow->debug_cheat) {
            /* 0x41308e's dialog waits (-@); host.new_game created the
             * game only, the answer prepares and shows the bay */
            flow->debug = ES2_FLOW_DEBUG_CAREER;
            return ES2_FLOW_DONE;
        }
        show_bay(flow);
        return ES2_FLOW_DONE;
    default:
        return ES2_FLOW_IGNORED;
    }
}

static es2_shell_flow_result saved_games(es2_shell_flow *flow, es2_shell_flow_handler event, const char *text) {
    switch (event) {
    case ES2_FLOW_SLOT_SAVE:           /* 0x438263: naming mode 1 */
        if (!es2_shell_flow_enabled(flow, event, 0))
            return flow->game == ES2_FLOW_GAME_NONE ? refuse(flow, ES2_FLOW_NO_GAME) : ES2_FLOW_IGNORED;
        flow->naming = 1;
        return ES2_FLOW_DONE;
    case ES2_FLOW_SLOT_ACCEPT:         /* 0x43868a: 0x40e933(slot, row text), naming 0 */
        if (!es2_shell_flow_enabled(flow, event, 0)) return ES2_FLOW_IGNORED;
        flow->naming = 0;
        return save(flow, flow->slot, text) ? ES2_FLOW_DONE : ES2_FLOW_FAILED;
    case ES2_FLOW_SLOT_CANCEL:         /* 0x4384aa: the row texts back, naming 0, then 0x437fef(10) */
        if (!es2_shell_flow_enabled(flow, event, 0)) return ES2_FLOW_IGNORED;
        flow->naming = 0;
        flow->slot = ES2_FLOW_SLOT_CAMPAIGN;
        return ES2_FLOW_DONE;
    case ES2_FLOW_SLOT_RESTORE:        /* 0x438393: load(slot, 1), save as slot 10, hide, bay */
        if (flow->naming || flow->slot < 0 || flow->slot >= ES2_FLOW_USER_SLOTS) return ES2_FLOW_IGNORED;
        if (load(flow, flow->slot) <= 0) return refuse(flow, ES2_FLOW_NO_SLOT_GAME);
        save(flow, ES2_FLOW_SLOT_CAMPAIGN, NULL);
        flow->slot = ES2_FLOW_SLOT_CAMPAIGN;   /* the hide 0x43a3f6 selects 10 */
        /* 0x4383cd..0x4383d5: the bay show for every final word (no test of
         * 0x4826f2); a slot with final word 2 becomes the campaign the own
         * shell prepares, any other stays a loaded game without one */
        if (flow->final_word == 2) return campaign_from_slot(flow);
        show_bay(flow);
        return ES2_FLOW_DONE;
    case ES2_FLOW_SLOT_EXIT:           /* 0x438424: hide (selects 10); caller 0 main menu, 8 bay show, other nothing */
        if (!es2_shell_flow_enabled(flow, event, 0)) return ES2_FLOW_IGNORED;
        flow->slot = ES2_FLOW_SLOT_CAMPAIGN;
        if (flow->caller == ES2_FLOW_CALLER_MENU) flow->screen = ES2_FLOW_MAIN_MENU;
        else if (flow->caller == ES2_FLOW_CALLER_BAY) show_bay(flow);
        return ES2_FLOW_DONE;
    default:
        return ES2_FLOW_IGNORED;
    }
}

static es2_shell_flow_result preferences(es2_shell_flow *flow, es2_shell_flow_handler event) {
    uint8_t out[ES2_SHELL_PREFS_SIZE];
    es2_error e;
    if (flow->box == ES2_FLOW_BOX_ALERT) {
        if (event != ES2_FLOW_ALERT_ACCEPT) return ES2_FLOW_IGNORED;
        flow->box = ES2_FLOW_BOX_NONE;
        es2_shell_prefs_control(&flow->prefs, ES2_SHELL_PREFS_FULL_SCREEN, &e);   /* 0x437678: byte 6 := 1 */
        return ES2_FLOW_DONE;
    }
    switch (event) {
    case ES2_FLOW_PREFS_CANCEL:        /* 0x437220: the current block back to the committed one */
        es2_shell_prefs_cancel(&flow->prefs);
        flow->screen = ES2_FLOW_MAIN_MENU;
        return ES2_FLOW_DONE;
    case ES2_FLOW_PREFS_ACCEPT:        /* 0x4372e1: commit, write all 54 bytes */
        es2_shell_prefs_accept(&flow->prefs, out);
        flow->screen = ES2_FLOW_MAIN_MENU;
        return !flow->host.write_prefs || flow->host.write_prefs(flow->host.ctx, out) ? ES2_FLOW_DONE : ES2_FLOW_FAILED;
    default:
        return ES2_FLOW_IGNORED;
    }
}

static es2_shell_flow_result service_bay(es2_shell_flow *flow, es2_shell_flow_handler event) {
    int word;
    if (!es2_shell_flow_enabled(flow, event, 0)) return ES2_FLOW_IGNORED;
    if (event == ES2_FLOW_BAY_MAIN_MENU) {
        /* 0x43a66c (skipped while the word is 0): leave, hide the bay, word
         * 0, save slot 10 (0x43a705), main menu */
        if (flow->bay_word == ES2_FLOW_WORD_NONE) return ES2_FLOW_IGNORED;
        leave(flow);
        flow->bay_word = ES2_FLOW_WORD_NONE;
        save(flow, ES2_FLOW_SLOT_CAMPAIGN, NULL);
        flow->screen = ES2_FLOW_MAIN_MENU;
        return ES2_FLOW_DONE;
    }
    word = bay_word_of(event);
    if (word == ES2_FLOW_WORD_NONE) return ES2_FLOW_IGNORED;
    /* every bar button compares the word with its own and does nothing
     * when equal (WEAPONS 0x43a8ce, REPAIR 0x43a9ce, MISSION 0x43aded,
     * SAVE 0x43a7d4); otherwise one leave and the new word */
    if (flow->bay_word == word) return ES2_FLOW_IGNORED;
    leave(flow);
    flow->bay_word = word;
    if (word == ES2_FLOW_WORD_SAVE) {
        /* 0x43a768: Saved games with caller mark 8, no mode store */
        flow->caller = ES2_FLOW_CALLER_BAY;
        flow->screen = ES2_FLOW_SAVED_GAMES;
    }
    return ES2_FLOW_DONE;
}

/* Rock & Roll 0x445b99: the launch checks; a failed rule shows the warning
 * box 0x44d66c (0x44d90c(n)); otherwise 0x40f68c and exit 2. */
static es2_shell_flow_result rock_and_roll(es2_shell_flow *flow) {
    int warning = ES2_SHELL_LAUNCH_OK;
    if (flow->game != ES2_FLOW_GAME_CAMPAIGN) return refuse(flow, ES2_FLOW_NO_CAMPAIGN);
    if (flow->host.launch && flow->host.launch(flow->host.ctx, &warning)) {
        flow->exit_code = ES2_FLOW_EXIT_DBSIM;
        flow->quit = 1;
        return ES2_FLOW_DONE;
    }
    if (warning == ES2_SHELL_LAUNCH_OK) return ES2_FLOW_FAILED;
    show_box(flow, ES2_FLOW_BOX_WARNING, warning);
    return ES2_FLOW_DONE;
}

static es2_shell_flow_result boxes(es2_shell_flow *flow, es2_shell_flow_handler event) {
    switch (flow->box) {
    case ES2_FLOW_BOX_END_OF_GAME:     /* OKAY 0x44d60b only hides the box */
        if (event != ES2_FLOW_END_OKAY) return ES2_FLOW_IGNORED;
        show_box(flow, ES2_FLOW_BOX_NONE, 0);
        return ES2_FLOW_DONE;
    case ES2_FLOW_BOX_WARNING:         /* OKAY 0x44da94 hides it */
        if (event != ES2_FLOW_WARNING_OKAY) return ES2_FLOW_IGNORED;
        show_box(flow, ES2_FLOW_BOX_NONE, 0);
        return ES2_FLOW_DONE;
    case ES2_FLOW_BOX_REPLAY:
        if (event == ES2_FLOW_REPLAY_NO) {
            /* 0x44d24d: save slot 10 (the state after the results path), main menu */
            show_box(flow, ES2_FLOW_BOX_NONE, 0);
            save(flow, ES2_FLOW_SLOT_CAMPAIGN, NULL);
            flow->screen = ES2_FLOW_MAIN_MENU;
            return ES2_FLOW_DONE;
        }
        if (event != ES2_FLOW_REPLAY_YES) return ES2_FLOW_IGNORED;
        /* 0x44d1d4: load slot 10 without the briefing texts (0x40eaaa(10,
         * 0): the state and the launch-file copies; DATA\mission.var stays
         * DBSIM's), exit 2 (the exit saves slot 10 again) */
        show_box(flow, ES2_FLOW_BOX_NONE, 0);
        flow->screen = ES2_FLOW_MAIN_MENU;
        if (load(flow, ES2_FLOW_SLOT_CAMPAIGN) <= 0) return refuse(flow, ES2_FLOW_NO_SLOT_GAME);
        flow->game = ES2_FLOW_GAME_CAMPAIGN;
        if (flow->host.replay) flow->host.replay(flow->host.ctx);
        flow->exit_code = ES2_FLOW_EXIT_DBSIM;
        flow->quit = 1;
        return ES2_FLOW_DONE;
    default:
        return ES2_FLOW_IGNORED;
    }
}

es2_shell_flow_result es2_shell_flow_event(es2_shell_flow *flow, es2_shell_flow_handler event, const char *text) {
    flow->refusal = 0;
    if (flow->quit) return ES2_FLOW_IGNORED;
    /* the Quit box (window close, Escape) is reachable on every screen */
    if (event == ES2_FLOW_QUIT_ACCEPT) {
        flow->exit_code = ES2_FLOW_EXIT_QUIT;
        flow->quit = 1;
        return ES2_FLOW_DONE;
    }
    if (flow->box != ES2_FLOW_BOX_NONE)
        return flow->box == ES2_FLOW_BOX_ALERT ? preferences(flow, event) : boxes(flow, event);
    switch (flow->screen) {
    case ES2_FLOW_MAIN_MENU:
        /* the text menu's shortcut: its Rock & Roll from the main menu */
        if (event == ES2_FLOW_ROCK_AND_ROLL) return rock_and_roll(flow);
        return main_menu(flow, event);
    case ES2_FLOW_REGISTRATION:
        return registration(flow, event, text);
    case ES2_FLOW_SAVED_GAMES:
        return saved_games(flow, event, text);
    case ES2_FLOW_PREFERENCES:
        return preferences(flow, event);
    case ES2_FLOW_PRACTICE:
        if (event != ES2_FLOW_PRACTICE_MAIN_MENU) return ES2_FLOW_IGNORED;
        flow->screen = ES2_FLOW_MAIN_MENU;   /* 0x44c96a: no revert of the controls */
        return ES2_FLOW_DONE;
    case ES2_FLOW_SERVICE_BAY:
        /* 0x43a5bd: the online image runs the same routine once the main loop
         * has started (0x46c280, set at every iteration: always here) */
        if (event == ES2_FLOW_BAY_ONLINE) return online_manual(flow);
        if (event == ES2_FLOW_ROCK_AND_ROLL)
            return flow->bay_word == ES2_FLOW_WORD_MISSION ? rock_and_roll(flow) : ES2_FLOW_IGNORED;
        return service_bay(flow, event);
    }
    return ES2_FLOW_IGNORED;
}
