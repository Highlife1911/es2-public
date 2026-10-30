/* The own shell's host (host.h), shared by es2menu.exe and
 * es2shell.exe; text goes to the presenter's `say`. */
#include "host.h"
#include "es2/formats/cfg.h"
#include "es2/formats/sav.h"
#include "es2/formats/str.h"
#include "es2/shell/prepare.h"
#include "es2/shell/save.h"
#include "es2/shell/vshell_store.h"
#include <windows.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

enum { GAME_NONE = ES2_FLOW_GAME_NONE, GAME_CAMPAIGN = ES2_FLOW_GAME_CAMPAIGN, GAME_SLOT = ES2_FLOW_GAME_SLOT };
enum { EXIT_DBSIM = ES2_FLOW_EXIT_DBSIM };

/* ---- files ---------------------------------------------------------------- */
uint8_t *es2_host_read(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    uint8_t *data = NULL;
    long n;
    *size = 0;
    if (!f) return NULL;
    if (!fseek(f, 0, SEEK_END) && (n = ftell(f)) >= 0 && !fseek(f, 0, SEEK_SET) &&
        (data = malloc((size_t)n + 1)) != NULL) {
        if (fread(data, 1, (size_t)n, f) != (size_t)n) { free(data); data = NULL; }
        else { data[n] = 0; *size = (size_t)n; }
    }
    fclose(f);
    return data;
}
int es2_host_write(const char *path, const uint8_t *data, size_t size) {
    FILE *f = fopen(path, "wb");
    int ok = f && fwrite(data, 1, size, f) == size;
    if (f && fclose(f)) ok = 0;
    return ok;
}
static void join(char *out, size_t capacity, const char *a, const char *b) {
    snprintf(out, capacity, "%s\\%s", a, b);
}

/* ---- output ----------------------------------------------------------------- */
void es2_host_say(es2_host *h, const char *format, ...) {
    char text[2048];
    va_list args;
    int n;
    va_start(args, format);
    n = vsnprintf(text, sizeof text, format, args);
    va_end(args);
    if (n < 0) return;
    if ((size_t)n >= sizeof text) n = (int)sizeof text - 1;
    if (h->ui.say) h->ui.say(h->ui.ctx, text, (size_t)n);
}
void es2_host_engine_error(es2_host *h) {
    es2_host_say(h, "! %s\n", h->io.error[0] ? h->io.error : "the engine failed");
}

/* ---- labels: ESTEXT.BIN by string ID (vshell_ui.md) ------------------------- */
void es2_host_labels_open(es2_host *h) {
    size_t size;
    char path[64];
    es2_vfs_copy copy;
    es2_error e;
    h->lang_vol = es2_host_read("VOL\\LANG0.VOL", &size);
    if (!h->lang_vol || es2_vol_open((es2_span){h->lang_vol, size}, &h->vol, &e) != ES2_OK) return;
    h->source.kind = ES2_VFS_VOL;
    h->source.data.vol = &h->vol;
    snprintf(path, sizeof path, "%s/ESTEXT.BIN", h->args.language);
    if (es2_vfs_open(&h->source, 1, &h->vfs, &e) == ES2_OK && es2_vfs_find(&h->vfs, path, &copy, &e) == ES2_OK) {
        h->estext = copy.payload;
        if (es2m_bin_raw(h->estext, 0x21, &h->empty_text, &e) != ES2_OK) h->empty_text = (es2_span){0};
    }
    /* The WEAPONS screen's catalogue (0x43fba6: wpn_desc.bin). */
    snprintf(path, sizeof path, "%s/WPN_DESC.BIN", h->args.language);
    if (h->estext.data && es2_vfs_find(&h->vfs, path, &copy, &e) == ES2_OK) h->wpn_desc = copy.payload;
}

/* ---- menu.ini ------------------------------------------------------------- */
static int ini_value(es2_span ini, const char *key, char *out, size_t capacity) {
    es2_error e;
    int found = 0;
    return es2_cfg_ini_get(ini, "Menu", key, out, capacity, &found, &e) == ES2_OK && found;
}
int es2_host_config(es2_host *h) {
    char path[MAX_PATH];
    size_t size;
    uint8_t *data;
    int ok;
    join(path, sizeof path, h->args.menu_dir, "menu.ini");
    if (!GetFullPathNameA(h->args.menu_dir, sizeof h->dir, h->dir, NULL)) return 0;
    data = es2_host_read(path, &size);
    if (!data) return 0;
    es2_span ini = {data, size};
    ok = ini_value(ini, "game_dir", h->game, sizeof h->game);
    if (!ini_value(ini, "seed", h->seed, sizeof h->seed)) h->seed[0] = 0;
    /* The CD (tables, archives), the hangar policy and the evidence
     * log. */
    snprintf(h->io.game, sizeof h->io.game, "%s", h->game);
    if (!ini_value(ini, "cd_root", h->io.cd_root, sizeof h->io.cd_root)) h->io.cd_root[0] = 0;
    if (!ini_value(ini, "core", h->io.core, sizeof h->io.core)) h->io.core[0] = 0;
    char hangar[32];
    h->io.lowest_free = !ini_value(ini, "hangar", hangar, sizeof hangar) || strcmp(hangar, "stop");
    if (!ini_value(ini, "chain_dir", h->io.log_dir, sizeof h->io.log_dir)) h->io.log_dir[0] = 0;
    char cd_check[32];   /* original (default) or skip */
    h->cd_policy = es2_shell_cd_policy_parse(ini_value(ini, "cd_check", cd_check, sizeof cd_check) ? cd_check : NULL);
    if (h->cd_policy < 0) ok = 0;
    char primary[16];   /* absent (native-r111 refused) or allowed */
    h->allow_primary = 0;
    if (ini_value(ini, "primary", primary, sizeof primary)) {
        if (!strcmp(primary, "allowed")) h->allow_primary = 1;
        else ok = 0;
    }
    const char *env = getenv("ES2_MENU_CHAIN_DIR");
    if (env) snprintf(h->io.log_dir, sizeof h->io.log_dir, "%s", env);   /* empty: no log */
    if ((env = getenv("ES2_MENU_SEED"))) snprintf(h->seed, sizeof h->seed, "%s", env);
    /* The ONLINE MANUAL policy: open (default), text, hide */
    char online_manual[32];
    h->help_policy = (es2_help_policy)es2_help_policy_parse(
        ini_value(ini, "online_manual", online_manual, sizeof online_manual) ? online_manual : NULL);
    if ((int)h->help_policy < 0) ok = 0;
    free(data);
    return ok;
}
static int same_directory(const char *a, const char *b) {
    char x[MAX_PATH], y[MAX_PATH];
    size_t n;
    if (!GetFullPathNameA(a, sizeof x, x, NULL) || !GetFullPathNameA(b, sizeof y, y, NULL)) return 0;
    for (n = strlen(x); n > 3 && x[n - 1] == '\\'; ) x[--n] = 0;
    for (n = strlen(y); n > 3 && y[n - 1] == '\\'; ) y[--n] = 0;
    return !_stricmp(x, y);
}
static int primary_installation(const char *path) {
    char lower[MAX_PATH];
    size_t i;
    for (i = 0; path[i] && i + 1 < sizeof lower; i++) lower[i] = (char)tolower((unsigned char)path[i]);
    lower[i] = 0;
    for (char *p = strstr(lower, "native-r111"); p; p = strstr(p + 1, "native-r111"))
        if ((p == lower || p[-1] == '\\' || p[-1] == '/') && (!p[11] || p[11] == '\\' || p[11] == '/')) return 1;
    return 0;
}
int es2_host_check_directory(es2_host *h, const char *cwd) {
    if (!h->allow_primary && (primary_installation(cwd) || primary_installation(h->game))) {
        es2_host_say(h, "! native-r111 is the primary installation; use a copy\n");
        return 0;
    }
    if (!same_directory(cwd, h->game)) {
        es2_host_say(h, "! started in %s, menu.ini names %s\n", cwd, h->game);
        return 0;
    }
    return 1;
}

/* ---- prefs.cfg: VSHELL's two blocks (es2_shell_prefs_session) -------- */
/* Host of the flow: a main-menu entry stores the mode word (0x40ec56): byte
 * +0x2a of the current block, written through the read-modify-write of
 * single bytes (0x40dfa3) over the file as it is now (the flow has set the
 * mode word before). */
static int host_store_mode(void *ctx, uint8_t mode) {
    es2_host *h = ctx;
    uint8_t *file, out[ES2_SHELL_PREFS_SIZE];
    size_t size;
    es2_error e;
    int ok;
    file = es2_host_read("DATA\\PREFS.CFG", &size);
    es2_span span = {file, size};
    ok = es2_shell_prefs_store(&h->flow.prefs, file ? &span : NULL, ES2_SHELL_PREFS_MODE, mode, out, &e) == ES2_OK &&
         es2_host_write("DATA\\PREFS.CFG", out, sizeof out);
    free(file);
    if (!ok) es2_host_say(h, "! DATA\\PREFS.CFG could not be written\n");
    return ok;
}
/* Host: Preferences ACCEPT writes all 54 bytes (0x40dd0a). */
static int host_write_prefs(void *ctx, const uint8_t block[ES2_SHELL_PREFS_SIZE]) {
    es2_host *h = ctx;
    if (es2_host_write("DATA\\PREFS.CFG", block, ES2_SHELL_PREFS_SIZE)) return 1;
    es2_host_say(h, "! DATA\\PREFS.CFG could not be written\n");
    return 0;
}

/* The help routine's last steps (0x431ea2..0x431f30), after the flow has
 * stored byte 6 and written prefs.cfg: language.cfg is read from the game
 * directory (0x408d69, "data\language.cfg" relative to the process), the
 * manual's path starts at the CD root (menu.ini cd_root; VSHELL's is
 * drive.cfg's) and the manual is opened through its registered handler (the
 * platform deviation of vshell_ui.md section 21) or only named. Nothing is
 * opened with the policy text, which the tests use. A language.cfg that does
 * not open or names no language is VSHELL's severity-4 assertion: the report
 * (a message box there, the presenter's here, and assert.log) and the end of
 * the process with exit code 1, without the loop end's save. */
static int help_language(void *ctx) {
    size_t size;
    uint8_t *file = es2_host_read("DATA\\LANGUAGE.CFG", &size);
    int letter = !file ? -1 : size ? file[0] : 0;   /* an empty file: VSHELL reads an unset byte */
    (void)ctx;
    free(file);
    return letter;
}
static void assert_report(es2_host *h, const char *text) {
    FILE *log = fopen("assert.log", "ab");   /* 0x44e764: append */
    es2_host_say(h, "%s\n", text);
    if (log) {
        if (!h->assert_rule) {
            char rule[ES2_HELP_LOG_RULE + 1];
            memset(rule, '-', ES2_HELP_LOG_RULE - 2);
            rule[ES2_HELP_LOG_RULE - 2] = '\r';
            rule[ES2_HELP_LOG_RULE - 1] = '\n';
            fwrite(rule, 1, ES2_HELP_LOG_RULE, log);
        }
        fprintf(log, "%s\r\n", text);
        fclose(log);
    }
    h->assert_rule = 1;
    h->assert_exit = 1;
    if (h->ui.assertion) h->ui.assertion(h->ui.ctx, text);
    h->flow.quit = 1;   /* 0x44e5b0: exit(1) */
}
static void host_online_manual(void *ctx) {
    es2_host *h = ctx;
    static const es2_help_ops ops = {NULL, help_language, es2_help_win_exists, es2_help_win_open};
    es2_help_result r;
    es2_help_show(&ops, h->help_policy, h->io.cd_root, &r);
    switch (r.outcome) {
    case ES2_HELP_HIDDEN: break;
    case ES2_HELP_OPENED: es2_host_say(h, "The online manual was opened: %s\n", r.path); break;
    case ES2_HELP_TEXT_ONLY:
        es2_host_say(h, "The online manual is the Windows help file %s (not opened: menu.ini online_manual is text)\n", r.path);
        break;
    case ES2_HELP_NO_FILE: case ES2_HELP_BAD_LANGUAGE: {
        char text[160];
        es2_help_assertion(r.outcome, text, sizeof text);
        assert_report(h, text);
        break;
    }
    case ES2_HELP_TOO_LONG: es2_host_say(h, "! The online manual is not available: menu.ini cd_root is too long\n"); break;
    case ES2_HELP_MISSING: es2_host_say(h, "! The online manual was not found: %s\n", r.path); break;
    case ES2_HELP_NO_VIEWER:
        es2_host_say(h, "! The online manual is the Windows help file %s; no program on this computer opens .hlp files\n", r.path);
        break;
    }
}

/* ---- save slots (vshell_ui.md 4.3, es2/shell/slots.h) ---------------------- */
/* The slot table as VSHELL's start-up reader 0x40e380 leaves it. */
int es2_host_slot_table(es2_host *h, es2_shell_slot_table *t) {
    size_t size;
    uint8_t *file = es2_host_read("SAV\\GAMEFILE.STR", &size);
    es2_error e;
    int ok = file && es2_shell_slots_load((es2_span){file, size}, h->empty_text, t, &e) == ES2_OK;
    if (!ok) es2_host_say(h, "! SAV\\GAMEFILE.STR is missing or not a slot table\n");
    free(file);
    return ok;
}
static int copy_file(const char *from, const char *to) {
    size_t n;
    uint8_t *data = es2_host_read(from, &n);
    int ok = data && es2_host_write(to, data, n);   /* the copy 0x40da8d truncates (open 0x8301) */
    free(data);
    return ok;
}

/* The save 0x40e933(slot, text): nothing without a loaded game; slot 10 is
 * 11 in mode 0; GAMEFILE.STR rewritten (slot text for 0..9, flag) without
 * truncation; the SAV of the loaded game written over the slot's file
 * without truncation; DATA's three launch files copied to the slot's
 * companions. The loaded game's SAV is the engine's memory state. */
static int save_game(es2_host *h, int slot, const char *text) {
    char path[MAX_PATH], name[ES2_SHELL_SLOT_FILE], companion[32];
    size_t gf_size = 0, sav_size = 0, old_size = 0, need = 0;
    uint8_t *gamefile = NULL, *sav = NULL, *old = NULL, *out_gf = NULL, *out_sav = NULL;
    const char *data_name;
    es2_sav s;
    es2_error e;
    int effective = -1, ok = 0;
    if (h->flow.game == GAME_NONE || !h->io.engine.memory.data) return 0;
    gamefile = es2_host_read("SAV\\GAMEFILE.STR", &gf_size);
    /* The memory state of the engine */
    if ((sav = malloc(h->io.engine.memory.size ? h->io.engine.memory.size : 1)) != NULL) {
        memcpy(sav, h->io.engine.memory.data, h->io.engine.memory.size);
        sav_size = h->io.engine.memory.size;
    }
    if (!gamefile || !sav || es2_sav_parse((es2_span){sav, sav_size}, &s, &e) != ES2_OK ||
        es2_shell_slot_gamefile((es2_span){gamefile, gf_size}, h->empty_text, slot, text, h->flow.mode, &effective, name,
                                NULL, 0, &need, &e) != ES2_OK ||
        !(out_gf = malloc(need)) ||
        es2_shell_slot_gamefile((es2_span){gamefile, gf_size}, h->empty_text, slot, text, h->flow.mode, &effective, name,
                                out_gf, need, &need, &e) != ES2_OK ||
        !es2_host_write("SAV\\GAMEFILE.STR", out_gf, need)) goto done;
    snprintf(path, sizeof path, "SAV\\%s", name);
    old = es2_host_read(path, &old_size);
    if (es2_sav_overlay((es2_span){sav, s.reader_end}, (es2_span){old, old ? old_size : 0}, NULL, 0, &need, &e) != ES2_OK ||
        !(out_sav = malloc(need)) ||
        es2_sav_overlay((es2_span){sav, s.reader_end}, (es2_span){old, old ? old_size : 0}, out_sav, need, &need, &e) != ES2_OK ||
        !es2_host_write(path, out_sav, need)) goto done;
    ok = 1;
    for (int i = 0; ok && i < 3; i++) {
        char from[64], to[64];
        ok = es2_shell_slot_companion(effective, i, companion, sizeof companion, &data_name, &e) == ES2_OK;
        snprintf(from, sizeof from, "DATA\\%s", ok ? data_name : "");
        snprintf(to, sizeof to, "SAV\\%s", companion);
        ok = ok && copy_file(from, to);
    }
done:
    if (ok) es2_host_say(h, "[saved slot %d: SAV\\%s]\n", effective, name);
    else es2_host_say(h, "! slot %d was not saved\n", effective < 0 ? slot : effective);
    free(gamefile); free(sav); free(old); free(out_gf); free(out_sav);
    return ok;
}

/* The load 0x40eaaa(slot, 1) up to the point where a game is loaded: slot 10
 * is 11 in mode 0; a slot without flag loads nothing; the slot's three
 * companions are copied back to DATA (0x4131e3); the loaded game is the
 * state the original readers restore (es2_shell_loaded_save: item lists
 * reversed), the engine's memory state. *info describes the slot; the
 * final word in memory is the slot's. Returns 1, 0 on an error and -1 when
 * the slot holds no game (VSHELL loads nothing and keeps its memory). */
static int load_slot(es2_host *h, int slot, es2m_save_info *info) {
    es2_shell_slot_table t;
    char path[MAX_PATH], companion[32], to[64], from[64];
    const char *data_name;
    size_t size;
    uint8_t *sav;
    es2_error e;
    int k = es2_shell_slot_effective(slot, h->flow.mode), ok;
    if (!es2_host_slot_table(h, &t)) return 0;
    if (!t.slot[k].flag || !t.slot[k].file[0]) return -1;
    snprintf(path, sizeof path, "SAV\\%s", t.slot[k].file);
    sav = es2_host_read(path, &size);
    ok = sav && es2m_save_info_read((es2_span){sav, size}, info, &e) == ES2_OK &&
         es2_shell_engine_load(&h->io.engine, (es2_span){sav, size}, &e) == ES2_OK;
    free(sav);
    if (!ok) {
        es2_host_say(h, "! %s is not a SAV\n", path);
        return 0;
    }
    h->companions = 1;
    for (int i = 0; i < 3; i++)
        if (es2_shell_slot_companion(k, i, companion, sizeof companion, &data_name, &e) == ES2_OK) {
            snprintf(from, sizeof from, "SAV\\%s", companion);
            snprintf(to, sizeof to, "DATA\\%s", data_name);
            if (!copy_file(from, to)) h->companions = 0;
        }
    snprintf(h->slot_file, sizeof h->slot_file, "%s", path);
    return 1;
}

/* ---- the flow's host (es2/shell/flow.h): the file layer of its effects ---- */
static int host_save(void *ctx, int slot, const char *text) {
    return save_game(ctx, slot, text);
}
/* The flow marks the game loaded with the slot's final word. */
static int host_load(void *ctx, int slot, int16_t *final_word) {
    es2m_save_info info;
    int r = load_slot(ctx, slot, &info);
    if (r > 0) *final_word = info.final_word;
    if (r > 0) es2_shell_movies_load(&((es2_host *)ctx)->movies, 0);   /* 0x40ec06, 0x40ec0d */
    return r;
}
/* Registration ACCEPT: 0x40e8a5 from the process RNG (the skill becomes the
 * rank word), then the Use Default preparation into DATA. */
static int host_new_game(void *ctx, const char *name, uint16_t rank, int16_t *final_word) {
    es2_host *h = ctx;
    /* With -@ the preparation waits for the DEBUG dialog (host_debug_prepare) */
    if (eio_new_game(&h->io, name, rank, NULL, h->flow.debug_cheat)) {
        es2_shell_movies_new_career(&h->movies);   /* ACCEPT 0x43c7a5, 0x40e8b1, 0x40e8b8 */
        return 1;
    }
    es2_host_engine_error(h);
    if (!h->io.engine.loaded) return 0;
    *final_word = h->io.engine.final_word;
    return -1;
}
/* A loaded slot with final word 2 is the campaign in the service bay (as
 * VSHELL, with its launch-file copies; a slot without them gets its
 * CAREER mission prepared, the own menu's deviation). */
static int host_campaign(void *ctx) {
    es2_host *h = ctx;
    size_t size;
    uint8_t *file = es2_host_read(h->slot_file, &size);
    int ok = file && eio_loaded(&h->io, h->slot_file, file, size, h->companions);
    free(file);
    if (!ok) es2_host_engine_error(h);
    return ok;
}
/* Rock & Roll: the launch checks on the engine's memory state, then
 * 0x40f68c writes player.mec and mission.var. ES2_ERR_OFFSET (an active
 * squad pilot without a Herc, where the original reads outside its hangar
 * array) is a failure without a box: never launched. */
static int host_launch(void *ctx, int *warning) {
    es2_host *h = ctx;
    if (eio_launch(&h->io, warning)) return 1;
    if (*warning == ES2_SHELL_LAUNCH_OK) es2_host_engine_error(h);
    return 0;
}
/* Leaving WEAPONS (REPAIR, BUILD): 0x412a37 with the weapons-building byte
 * of the prefs block in memory (0x4825c9, +0x2d). */
static int host_rebalance(void *ctx, uint8_t build) {
    es2_host *h = ctx;
    es2_shell_bay_op op = {ES2_SHELL_BAY_REBALANCE, 0, 0, build, 0, 0};
    if (eio_bay(&h->io, &op)) return 1;
    es2_host_engine_error(h);
    return 0;
}
static int host_prepare_next(void *ctx) {
    return eio_prepare_next(&((es2_host *)ctx)->io);
}
/* The DEBUG dialog's answer on a mode-1 path: Use Default (typed
 * NULL) or ACCEPT with the field's text (msn\<stem>.msn after the '^'
 * replacement, es2_shell_debug_mission). */
static int host_debug_prepare(void *ctx, const char *typed) {
    es2_host *h = ctx;
    char stem[32];
    es2_error e;
    int ok;
    if (typed && es2_shell_debug_mission(typed, stem, sizeof stem, &e) != ES2_OK) {
        es2_host_say(h, "! no mission file name: letters, digits, '_' and one '^'\n");
        return 0;
    }
    ok = typed ? eio_prepare_mission(&h->io, stem) : eio_prepare_next(&h->io);
    if (!ok) es2_host_engine_error(h);
    return ok;
}
static void host_replay(void *ctx) {
    eio_replay(&((es2_host *)ctx)->io);   /* the evidence log only */
}
/* -X3/-X4 (vshell_ui.md section 6), host side of the flow's results
 * path after its slot-10 load (11 in mode 0, with its copies): the results
 * path 0x40f09f over DATA\mission.var and DATA\results.dat with the options
 * of the start-up prefs block; the presenter shows the debrief. */
static int host_results(void *ctx, es2_shell_flow_results *r) {
    es2_host *h = ctx;
    es2_shell_results_output out;
    char played[40];
    uint8_t *slot, *results_dat = NULL;
    size_t slot_size, results_size = 0;
    int ok;
    h->io.engine.mode = h->flow.mode;
    eio_mission_name(&h->io, played, sizeof played);
    slot = es2_host_read(h->slot_file, &slot_size);
    memset(&out, 0, sizeof out);
    ok = slot && eio_results(&h->io, slot, slot_size, h->flow.prefs.current[ES2_SHELL_PREFS_REPAIR],
                             h->flow.prefs.current[ES2_SHELL_PREFS_BUILD], &out, &results_dat, &results_size);
    free(slot);
    if (!ok) {
        es2_host_say(h, "! results rejected: %s\n", h->io.error);
        es2_shell_results_free(&out);
        return 0;
    }
    if (h->ui.results) h->ui.results(h->ui.ctx, played, &out, (es2_span){results_dat, results_size});
    h->debrief_selected = out.debrief;   /* the debrief word 0x48419e for page 4 */
    h->debrief_word = out.selected.aux;
    h->debrief_group = out.group;
    r->ending = out.ending;
    r->advance = out.advance;
    r->final_word = out.final_word;
    es2_shell_results_free(&out);
    free(results_dat);
    return 1;
}

static int practice_cheat(void);
void es2_host_start(es2_host *h, int (*full_screen)(void *ctx)) {
    /* game init 0x40e736: the prefs blocks (0x40dc44), mode word := byte
     * +0x2a (0x40e751); the final word in memory starts at 0 (the flow) */
    es2_shell_flow_host host = {
        h, host_store_mode, host_write_prefs, host_save, host_load, host_new_game, host_campaign,
        host_launch, host_rebalance, host_results, host_prepare_next, host_replay, host_online_manual,
        full_screen, host_debug_prepare};
    size_t size;
    uint8_t *file = es2_host_read("DATA\\PREFS.CFG", &size);
    es2_span span = {file, size};
    es2_shell_flow_init(&h->flow, &host, file ? &span : NULL);
    h->flow.host.ctx = h;
    h->flow.debug_cheat = practice_cheat();   /* 0x482368 */
    free(file);
    es2_shell_movies_init(&h->movies, !h->args.no_movies);   /* movie flag 0x482359 (-a: 0) */
    /* The process start of the engine; the RNG is reset with
     * GetTickCount() & 127 draws (0x407b42), or ES2_MENU_SEED / menu.ini's
     * seed, recorded in the evidence log */
    snprintf(h->io.language, sizeof h->io.language, "%s", h->args.language);
    h->io.state = h->args.state;
    eio_start(&h->io, (uint16_t)(h->seed[0] ? strtoul(h->seed, NULL, 10) & 0x7fffu : GetTickCount() & 127u), h->flow.mode);
}

void es2_host_close(es2_host *h) {
    if (h->cd_stream) fclose(h->cd_stream);
    h->cd_stream = NULL;
    eio_close(&h->io);
    free(h->lang_vol);
    h->lang_vol = NULL;
    free(h->debug.game.data);
    h->debug.game = (es2_shell_buffer){0};
    h->debug.pending = 0;
}

/* ---- PRACTICE MISSIONS, INSTANT ACTION (vshell_ui.md section 15) -------
 * es2/shell/practice.h over the process's prefs blocks (h->flow.prefs). A launch is
 * VSHELL's new game in mode 0 plus the DEBUG dialog's posted Use Default:
 * computed here in C from VSHELL's tables in the span store (*
 * es2/shell/vshell_store.h) and the CD (cd_root of menu.ini: VOL\ZONES, SHELL0,
 * LANG0), the four DATA files written as VSHELL leaves them (script.dat and
 * MISSION.STR without truncation), the state kept as the loaded game so that
 * the exit save writes slot 11 (mode 0) with its three companions. With -@
 * (cheat byte 0x482368) the dialog waits: Use Default or a typed mission file
 * (ACCEPT 0x44dc4d); INSTANT ACTION then exits with the new game's mission.var
 * and player.mec only. */
/* The tables and the CD's archives, once per process. */
int es2_host_practice_open(es2_host *h) {
    static const char *names[3] = {"ZONES.VOL", "SHELL0.VOL", "LANG0.VOL"};
    static const uint32_t defaults[3][2] = {{0x47f850, 112}, {0x46f72c, 16}, {0x46f868, 36}};
    es2_host_practice *p = &h->practice;
    char root[MAX_PATH], path[MAX_PATH];
    size_t size, at = 0;
    uint8_t *ini;
    es2_span span, exe;
    es2_error e;
    if (p->ready) return 1;
    if (p->failed) return 0;
    p->failed = 1;
    es2_shell_practice_open(&p->state);
    join(path, sizeof path, h->dir, "menu.ini");
    ini = es2_host_read(path, &size);
    if (!ini || !ini_value((es2_span){ini, size}, "cd_root", root, sizeof root)) {
        free(ini);
        es2_host_say(h, "! menu.ini names no cd_root\n");
        return 0;
    }
    free(ini);
    exe = ES2_VSHELL_STORE;   /* the tables from the span store only; VER95\VSHELL.EXE is not opened */
    for (int i = 0; i < 3; i++) {
        if (es2_shell_exe_span(exe, defaults[i][0], defaults[i][1], &span, &e) != ES2_OK) return 0;
        memcpy(p->defaults + at, span.data, span.size);
        at += span.size;
    }
    if (es2_shell_exe_span(exe, 0x46f9a8, 60, &span, &e) != ES2_OK) return 0;
    memcpy(p->items, span.data, 60);
    for (int i = 0; i < 9; i++) {
        es2_span pointer, text;
        if (es2_shell_exe_span(exe, 0x47083c + 4u * (uint32_t)i, 4, &pointer, &e) != ES2_OK ||
            es2_shell_exe_span(exe, (uint32_t)(pointer.data[0] | pointer.data[1] << 8 | pointer.data[2] << 16 |
                                               (uint32_t)pointer.data[3] << 24), 64, &text, &e) != ES2_OK ||
            !memchr(text.data, 0, 64)) return 0;
        memcpy(p->paths + 64 * i, text.data, strlen((const char *)text.data));
    }
    if (es2_shell_practice_tables(exe, p->tables, &e) != ES2_OK) return 0;
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof path, "%s\\VOL\\%s", root, names[i]);
        p->vols[i] = es2_host_read(path, &size);
        if (!p->vols[i] || es2_vol_open((es2_span){p->vols[i], size}, &p->vol[i], &e) != ES2_OK) {
            es2_host_say(h, "! %s could not be opened\n", path);
            return 0;
        }
        p->sources[i].kind = ES2_VFS_VOL;
        p->sources[i].data.vol = &p->vol[i];
    }
    if (es2_vfs_open(p->sources, 3, &p->vfs, &e) != ES2_OK) return 0;
    p->ready = 1;
    p->failed = 0;
    return 1;
}
/* The cheat byte 0x482368: -@ in the VSHELL argv (vshell_process.md). */
static int practice_cheat(void) {
    const char *line = GetCommandLineA();
    for (const char *p = strstr(line, "-@"); p; p = strstr(p + 1, "-@"))
        if ((p == line || p[-1] == ' ') && (!p[2] || p[2] == ' ')) return 1;
    return 0;
}
int es2_host_practice_write_prefs(es2_host *h, const uint8_t out[ES2_SHELL_PREFS_SIZE]) {
    int ok = es2_host_write("DATA\\PREFS.CFG", out, ES2_SHELL_PREFS_SIZE);
    if (!ok) es2_host_say(h, "! DATA\\PREFS.CFG could not be written\n");
    return ok;
}
/* A DATA file as VSHELL writes it: replaced, or (script.dat, MISSION.STR)
 * rewritten from offset 0 without truncation. */
static int practice_write_data(const char *name, const uint8_t *data, size_t size, int overlay) {
    char path[MAX_PATH];
    size_t old_size = 0;
    uint8_t *old = NULL, *out;
    int ok;
    snprintf(path, sizeof path, "DATA\\%s", name);
    if (overlay) old = es2_host_read(path, &old_size);
    if (!old || old_size <= size) { free(old); return es2_host_write(path, data, size); }
    out = malloc(old_size);
    if (!out) { free(old); return 0; }
    memcpy(out, old, old_size);
    memcpy(out, data, size);
    ok = es2_host_write(path, out, old_size);
    free(out);
    free(old);
    return ok;
}
/* The SAV of the game the process holds (0x40e8a5 keeps parts of it): the
 * engine's memory state. */
static uint8_t *practice_carry(es2_host *h, size_t *size) {
    uint8_t *copy;
    *size = 0;
    if (h->flow.game == GAME_NONE || !h->io.engine.memory.data || !(copy = malloc(h->io.engine.memory.size))) return NULL;
    memcpy(copy, h->io.engine.memory.data, h->io.engine.memory.size);
    *size = h->io.engine.memory.size;
    return copy;
}
/* The posted (or clicked) Use Default 0x44db5c, or the DEBUG dialog's ACCEPT
 * 0x44dc4d with `mission`: the preparation of the new game and the four
 * launch files. Returns EXIT_DBSIM or 0. */
static int practice_finish(es2_host *h, const es2_shell_practice_launch *launch, es2_span game, const char *mission) {
    es2_host_practice *p = &h->practice;
    es2_shell_save_tables tables = {{p->defaults, 164}, {p->items, 60}};
    es2_shell_rng rng = h->io.engine.rng;
    int16_t selectors[30];
    es2_shell_buffer save = {0};
    es2_shell_output files = {0};
    es2_error e;
    int code = 0;
    memcpy(selectors, h->io.engine.selectors, sizeof selectors);
    if (es2_shell_practice_prepare(&p->vfs, h->args.language, &tables,
                                   (es2_span){p->paths, sizeof p->paths}, launch, mission,
                                   game, &rng, selectors, &files, &save, &e) != ES2_OK) {
        es2_host_say(h, "! the preparation failed (%s)\n", es2_status_string(e.code));
        goto done;
    }
    h->io.engine.rng = rng;
    memcpy(h->io.engine.selectors, selectors, sizeof selectors);
    if (practice_write_data("script.dat", files.script.data, files.script.size, 1) &&
        practice_write_data("MISSION.STR", files.strings.data, files.strings.size, 1) &&
        practice_write_data("mission.var", files.variables.data, files.variables.size, 0) &&
        practice_write_data("player.mec", files.player.data, files.player.size, 0) &&
        es2_shell_engine_set(&h->io.engine, (es2_span){save.data, save.size}, &e) == ES2_OK) code = EXIT_DBSIM;
    else es2_host_say(h, "! the launch files could not be written\n");
done:
    if (code) es2_host_say(h, "[DBSIM: practice position %d]\n", launch->position);
    free(save.data);
    es2_shell_output_free(&files);
    return code;
}

/* New game, then (unless -@ keeps the dialog waiting) Use Default or ACCEPT;
 * returns EXIT_DBSIM, or 0 when the launch failed. The RNG and the
 * selectors are the process's (VSHELL continues its stream), and the
 * state is the engine's memory state, which the exit saves as slot 11. */
int es2_host_practice_launch(es2_host *h, const es2_shell_practice_launch *launch, int instant) {
    es2_host_practice *p = &h->practice;
    es2_shell_save_tables tables = {{p->defaults, 164}, {p->items, 60}};
    es2_shell_rng rng;
    es2_shell_buffer game = {0};
    char stem[32], typed[64];
    const char *mission = NULL;
    size_t carry_size;
    uint8_t *carry_bytes = practice_carry(h, &carry_size);
    es2_span carry = {carry_bytes, carry_size};
    es2_error e;
    int code = 0, choice = 1;
    if (!eio_open(&h->io)) { es2_host_engine_error(h); goto done; }
    rng = h->io.engine.rng;
    if (es2_shell_practice_new_game(&p->vfs, h->args.language, &tables, launch, carry_bytes ? &carry : NULL,
                                    &rng, &game, &e) != ES2_OK ||
        es2_shell_engine_set(&h->io.engine, (es2_span){game.data, game.size}, &e) != ES2_OK) {
        es2_host_say(h, "! the new game failed (%s)\n", es2_status_string(e.code));
        goto done;
    }
    h->io.engine.rng = rng;
    es2_shell_flow_game_created(&h->flow);   /* 0x40e92a: the game is loaded from here on, final word 2 */
    h->slot_file[0] = 0;
    if (practice_cheat()) {
        if (instant) {
            /* no posted click: the handler's own 0x40f68c, exit 2 */
            es2_shell_buffer mec = {0}, var = {0};
            if (es2_shell_practice_new_game_files((es2_span){game.data, game.size}, &mec, &var, &e) == ES2_OK &&
                practice_write_data("mission.var", var.data, var.size, 0) &&
                practice_write_data("player.mec", mec.data, mec.size, 0)) code = EXIT_DBSIM;
            else es2_host_say(h, "! the launch files of the new game failed (%s)\n", es2_status_string(e.code));
            free(mec.data);
            free(var.data);
            goto done;
        }
        /* Typed starts as the dialog's default name (0x413305: the
         * CAREER entry (0, position), [0x48dcec]) */
        int16_t group_id;
        char name[40];
        typed[0] = 0;
        if (es2_shell_campaign_mission(&p->vfs, 0, launch->position, h->args.language, name, sizeof name, &group_id,
                                       &e) == ES2_OK)
            snprintf(typed, sizeof typed, "%s", name);
        choice = h->ui.debug_choice ? h->ui.debug_choice(h->ui.ctx, typed, sizeof typed) : 1;
        if (choice == 0) goto done;   /* end of input: the dialog stays, VSHELL quits */
        if (choice == ES2_HOST_DEBUG_PENDING) {
            /* the presenter shows the dialog; its button answers through
             * es2_host_practice_debug_answer */
            free(h->debug.game.data);
            h->debug.game = game;
            game = (es2_shell_buffer){0};
            h->debug.launch = *launch;
            h->debug.pending = 1;
            code = ES2_HOST_PRACTICE_PENDING;
            goto done;
        }
        if (choice == 2) {
            if (es2_shell_debug_mission(typed, stem, sizeof stem, &e) != ES2_OK) {
                es2_host_say(h, "! no mission file name: letters, digits, '_' and one '^'\n");
                goto done;
            }
            mission = stem;
        }
    }
    code = practice_finish(h, launch, (es2_span){game.data, game.size}, mission);
done:
    free(carry_bytes);
    free(game.data);
    return code;
}

int es2_host_practice_debug_answer(es2_host *h, const char *typed) {
    char stem[32];
    es2_error e;
    int code = 0;
    if (!h->debug.pending) return 0;
    h->debug.pending = 0;
    if (typed && es2_shell_debug_mission(typed, stem, sizeof stem, &e) != ES2_OK)
        es2_host_say(h, "! no mission file name: letters, digits, '_' and one '^'\n");
    else
        code = practice_finish(h, &h->debug.launch, (es2_span){h->debug.game.data, h->debug.game.size},
                               typed ? stem : NULL);
    free(h->debug.game.data);
    h->debug.game = (es2_shell_buffer){0};
    return code;
}

int es2_host_debug_ask(es2_host *h) {
    char typed[64];
    int choice;
    if (h->flow.debug == ES2_FLOW_DEBUG_NONE || h->debug.asked) return 1;
    /* 0x413305: the default label [0x48dcec] is the CAREER entry at the
     * memory state's position (empty when there is none) */
    eio_mission_name(&h->io, typed, sizeof typed);
    if (!strcmp(typed, "-")) typed[0] = 0;
    choice = h->ui.debug_choice ? h->ui.debug_choice(h->ui.ctx, typed, sizeof typed) : 1;
    if (choice == ES2_HOST_DEBUG_PENDING) {
        h->debug.asked = 1;
        return ES2_HOST_DEBUG_PENDING;
    }
    if (choice == 0) {   /* end of input: the dialog stays, the process ends */
        h->flow.quit = 1;
        return 0;
    }
    es2_host_debug_answer(h, choice == 2 ? typed : NULL);
    return 1;
}
es2_shell_flow_result es2_host_debug_answer(es2_host *h, const char *typed) {
    h->debug.asked = 0;
    return es2_shell_flow_debug_answer(&h->flow, typed);
}

/* ---- error.str ----------------------------------------------------------
 * VSHELL reads error.str once at start-up through the VOL-aware stream
 * (0x401b49..0x401b88: the game directory's file first, then the CD's),
 * group 0 into 12 message pointers and group 1 into 3 caption pointers;
 * 0x401a81(msg, caption, flags) is MessageBoxA(NULL, group0[msg],
 * group1[caption], flags). A slot the file does not fill is NULL (the English
 * file has six messages, so message 6 is an empty box text). */
int es2_host_error_text(es2_host *h, int group, int index, char *out, size_t capacity) {
    size_t size = 0;
    char path[ES2_HOST_PATH + 16];
    uint8_t *file = es2_host_read("error.str", &size);
    es2_str str;
    es2_str_string s;
    es2_error e;
    int found = 0;
    if (capacity) out[0] = 0;
    if (!file && h->io.cd_root[0]) {
        snprintf(path, sizeof path, "%s\\ERROR.STR", h->io.cd_root);
        file = es2_host_read(path, &size);
    }
    if (file && es2_str_parse((es2_span){file, size}, 2, ES2_STR_TAG_BYTE, &str, &e) == ES2_OK &&
        index >= 0 && index < (group == 0 ? 12 : 3) &&
        es2_str_string_at(&str, (size_t)group, (size_t)index, &s, &e) == ES2_OK && capacity) {
        size_t n = s.text.size < capacity - 1 ? s.text.size : capacity - 1;
        memcpy(out, s.text.data, n);
        out[n] = 0;
        found = 1;
    }
    free(file);
    return found;
}

const char *es2_host_language_dir(int language) {
    /* 0x408bb5: 0 eng\, 1 fre\, 2 ger\, any other value no prefix */
    return language == 0 ? "ENG" : language == 1 ? "FRE" : language == 2 ? "GER" : "";
}

/* ---- Start-up CD check (vshell_ui.md section 20) ---------------------
 * VSHELL opens <CD>\avi\pt1.avi before the game init (0x401cac); when it
 * does not open it shows error.str message 5 and quits with exit code 0.
 * The host does the same with menu.ini's cd_root; without a cd_root it has
 * no CD to check and goes on (the menus without CD data keep working), and
 * cd_check = skip turns the check off. */
static int cd_probe(void *context, const char *path) {
    es2_host *h = context;
    h->cd_stream = fopen(path, "rb");
    return h->cd_stream != NULL;
}
int es2_host_cd_check(es2_host *h) {
    char root[MAX_PATH], path[MAX_PATH];
    size_t n = strlen(h->io.cd_root);
    if (!n) return 1;
    snprintf(root, sizeof root, "%s%s", h->io.cd_root, h->io.cd_root[n - 1] == '\\' ? "" : "\\");
    if (es2_shell_cd_check(h->cd_policy, root, cd_probe, h, path, sizeof path) == 0) return 1;
    es2_host_say(h, "! error.str message %d: the CD check found no %s\n", ES2_SHELL_CD_ERROR_MESSAGE, path);
    return 0;
}
