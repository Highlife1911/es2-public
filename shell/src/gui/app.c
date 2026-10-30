/* The graphical shell's presenter and host (gui/app.h).
 *
 * Contract: docs/reverse-engineering/launcher.md, section "Graphical
 * shell". This file only binds the screens and libraries:
 * resources by the screens' host callbacks, the flow's file layer by the
 * shared host (host.c, the text menu's), and the reaction to the flow's
 * state after every event (the screen or box the flow names is shown when
 * no screen handler showed it already). */
#include "gui/app.h"
#include "es2/shell/bytes.h"
#include "gui/app_sound.h"

#include "es2/formats/dbm.h"
#include "es2/formats/dpl.h"
#include "es2/formats/sav.h"
#include "es2/shell/draw.h"
#include "es2/shell/gui/boxes.h"
#include "es2/shell/gui/build_armory.h"
#include "es2/shell/gui/crew.h"
#include "es2/shell/gui/headless.h"   /* the present record's frame hashes */
#include "es2/shell/gui/layout.h"
#include "es2/shell/gui/main_menu.h"
#include "es2/shell/gui/mission.h"
#include "es2/shell/gui/mission_map.h"
#include "es2/shell/gui/practice_screen.h"
#include "es2/shell/gui/preferences.h"
#include "es2/shell/gui/registration.h"
#include "es2/shell/gui/repair.h"
#include "es2/shell/gui/saved_games.h"
#include "es2/shell/gui/service_bay.h"
#include "es2/shell/gui/squad_pictures.h"
#include "es2/shell/gui/weapons.h"
#include "es2/shell/campaign.h"
#include "es2/shell/editors.h"
#include "es2/shell/texts.h"
#include "es2/shell/vshell_store.h"
#include "win/mci_player.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define BITMAPS 1024   /* room for the Herc pictures' DBA frames */
#define TEXTS 96

typedef struct cached_bitmap {
    char file[40];
    int frame;            /* -1: a DBM file */
    es2_bitmap bitmap;
    uint8_t *pixels;      /* decoded pixels (NULL: borrowed from the VOL) */
} cached_bitmap;

struct es2_app {
    es2_host *h;
    es2_gui_shell *shell;
    es2_app_platform platform;
    es2_ui ui;
    /* resources: the installation's VOLs (ZONES, SHELL0, SHELL1, LANG0) */
    uint8_t *vol_bytes[4];
    es2_vol vols[4];
    es2_vfs_source sources[4];
    size_t source_order[4];             /* VSHELL's archive order */
    es2_vfs vfs;
    int vfs_ready;
    es2_dfn font2, black;
    int fonts;
    cached_bitmap bitmaps[BITMAPS];
    int bitmap_count;
    uint8_t records[ES2_PALETTE_COUNT][256][4];
    int palette_state[ES2_PALETTE_COUNT];    /* 0 not tried, 1 loaded, -1 missing */
    es2_span estext, wpn_desc, campaign_str;
    char texts[TEXTS][512];
    unsigned text_next;
    char *campaign_text, *report_text[5];
    /* screens */
    es2_boxes boxes;
    es2_main_menu menu;
    es2_screen_registration registration;
    es2_practice_screen practice;
    es2_saved_games saved;
    es2_preferences prefs;
    es2_service_bay bay;
    es2_weapons_screen weapons;
    es2_mission_screen mission;
    es2_mission_map map;                /* the map object [0x46f3a4] */
    es2_mission_map_globals map_g;      /* its process globals */
    es2_bitmap map_icons[ES2_MAP_ICONS];
    int map_built, map_globals;
    int bay_select_seen;                /* the flow's bay_select at the last sync */
    es2_repair_screen repair;
    es2_build_armory build;
    es2_build_armory_game build_game;
    es2_crew crew;
    es2_squad_pictures pictures;
    uint8_t editor_tables[ES2_SHELL_EDITOR_TABLES_SIZE];
    es2_span weapons_bin, wpn_info;
    int built;
    es2_shell_slot_table slots;
    es2_shell_stats stats[ES2_SHELL_STATS_SLOTS];
    es2_shell_practice_launch instant;       /* INSTANT ACTION's launch between its two stages */
    /* presenter state */
    es2_shell_flow_screen shown;             /* the flow screen on display */
    es2_shell_flow_host flow_host;           /* the host's callbacks the app wraps */
    es2_host_ui process_ui;                  /* the process's say and assertion (main.c), forwarded */
    es2_shell_debrief_texts debrief;         /* the last results path's statistics (0x40f904) */
    int have_debrief;
    /* sounds and movies */
    es2_app_sound *sound;
    /* The samples' archive VOL\SHLSOUND.VOL (audio.md, "Files and playback
     * format") */
    uint8_t *sound_vol_bytes;
    es2_vol sound_vol;
    es2_vfs_source sound_source;
    es2_vfs sound_vfs;
    es2_mci_player player;
    char cd_root[ES2_HOST_PATH];
    int playing;
    /* The -@ DEBUG dialog of a practice launch waits (debug_hold), and
     * the practice screen's dbsim is taken back once (debug_undo); the
     * memory state before the pass for WEAPONS' bay steps */
    int debug_hold, debug_undo;
    uint8_t *bay_before;
    size_t bay_before_size, bay_before_capacity;
    unsigned mounts_seen, stores_seen;
    /* The wall-time record of the presents (ES2_SHELL_PRESENT_LOG; off by default) */
    es2_os_stream *present_log;
    es2_gui_platform present_inner;          /* the platform the record forwards to */
    uint64_t present_frequency;
    unsigned present_seq, presents_since, blits_since;
    char present_index[65], present_table[65];
};

static void logf_(es2_app *a, const char *format, ...) {
    va_list args;
    if (!a->platform.log) return;
    va_start(args, format);
    es2_os_vfprintf(a->platform.log, format, args);
    va_end(args);
    es2_os_fflush(a->platform.log);
}

/* ---- resources ----------------------------------------------------------------------------- */
/* VSHELL registers vol\*.vol in _findfirst order with its mask 0x100
 * and the archives' priority (0x401aa4 -> 0x40356f, mask 0x100; file_io.md
 * VSHELL call sites, vol.md): SHELL1.VOL (priority 10) wins over
 * SHELL0.VOL (5).
 * The two differ only in the four fonts (dfn\font2, black, font, map; SHELL1's
 * have more glyphs); the plain source order took SHELL0's. The four archives
 * are given in the directory's name order. This lookup does not fold '/':
 * VOL member names take backslashes (vshell_ui.md section 38). */
static int open_vfs(es2_app *a) {
    static const char *const names[4] = {"LANG0.VOL", "SHELL0.VOL", "SHELL1.VOL", "ZONES.VOL"};
    es2_error e;
    if (a->vfs_ready) return a->vfs_ready > 0;
    a->vfs_ready = -1;
    for (int i = 0; i < 4; i++) {
        char path[ES2_HOST_PATH + 32];
        size_t size = 0;
        /* the installation's archive, else the CD's (menu.ini cd_root) */
        snprintf(path, sizeof path, "VOL\\%s", names[i]);
        a->vol_bytes[i] = es2_host_read(path, &size);
        if (!a->vol_bytes[i] && a->h->io.cd_root[0]) {
            snprintf(path, sizeof path, "%s\\VOL\\%s", a->h->io.cd_root, names[i]);
            a->vol_bytes[i] = es2_host_read(path, &size);
        }
        if (!a->vol_bytes[i] || es2_vol_open((es2_span){a->vol_bytes[i], size}, &a->vols[i], &e) != ES2_OK) {
            es2_host_say(a->h, "! %s could not be opened\n", names[i]);
            return 0;
        }
        a->sources[i].kind = ES2_VFS_VOL;
        a->sources[i].data.vol = &a->vols[i];
    }
    if (es2_vfs_open_dbsim_order(a->sources, 4, 0x100, a->source_order, 4, &a->vfs, &e) != ES2_OK) return 0;
    a->vfs_ready = 1;
    return 1;
}

static es2_span find(es2_app *a, const char *name) {
    es2_vfs_copy c;
    if (!open_vfs(a) || es2_vfs_find(&a->vfs, name, &c, NULL) != ES2_OK) return (es2_span){NULL, 0};
    return c.payload;
}

/* The resource globals of the start-up loader 0x40796c (vshell_widgets.md
 * section 5): 0x46ddfc and 0x46de00 dfn\font2, 0x46de04 dfn\black. */
static const es2_dfn *font(void *ctx, uint32_t global) {
    es2_app *a = ctx;
    if (!a->fonts) {
        es2_span f2 = find(a, "dfn\\font2.dfn"), bl = find(a, "dfn\\black.dfn");
        a->fonts = -1;
        if (f2.data && es2_dfn_parse(f2, &a->font2, NULL) == ES2_OK && bl.data && es2_dfn_parse(bl, &a->black, NULL) == ES2_OK)
            a->fonts = 1;
    }
    if (a->fonts < 0) return NULL;
    if (global == 0x46ddfc || global == 0x46de00) return &a->font2;
    if (global == 0x46de04) return &a->black;
    return NULL;
}

/* _stricmp equality in the C locale (the shell never sets one): 'A'..'Z'
 * fold to 'a'..'z'. */
static int same_name(const char *a, const char *b) {
    for (;; a++, b++) {
        int x = (unsigned char)*a, y = (unsigned char)*b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
        if (!x) return 1;
    }
}
/* A DBA member (frame >= 0) or a DBM file (frame -1), decoded once. */
static const es2_bitmap *bitmap(es2_app *a, const char *file, int frame) {
    es2_span bytes;
    es2_dbm dbm;
    cached_bitmap *c;
    if (!file) return NULL;
    for (int i = 0; i < a->bitmap_count; i++)
        if (a->bitmaps[i].frame == frame && same_name(a->bitmaps[i].file, file)) return &a->bitmaps[i].bitmap;
    if (a->bitmap_count >= BITMAPS || strlen(file) >= sizeof a->bitmaps[0].file) return NULL;
    bytes = find(a, file);
    if (!bytes.data) return NULL;
    if (frame < 0) {
        if (es2_dbm_parse(bytes, &dbm, NULL) != ES2_OK) return NULL;
    } else {
        es2_dba dba;
        if (es2_dba_parse(bytes, &dba, NULL) != ES2_OK || es2_dba_image_at(&dba, (size_t)frame, &dbm, NULL) != ES2_OK)
            return NULL;
    }
    c = &a->bitmaps[a->bitmap_count];
    memset(c, 0, sizeof *c);
    size_t size = (size_t)dbm.width * dbm.height;
    c->pixels = malloc(size ? size : 1);
    if (!c->pixels || es2_draw_bitmap_from_dbm(&dbm, c->pixels, size ? size : 1, &c->bitmap) != ES2_DRAW_DONE) {
        free(c->pixels);
        return NULL;
    }
    snprintf(c->file, sizeof c->file, "%s", file);
    c->frame = frame;
    a->bitmap_count++;
    return &c->bitmap;
}

/* The palette table 0x46de14 (draw.h): 0x407b62 select, 0x407c1b apply. */
static void palette(es2_ui *ui, uint16_t word) {
    es2_app *a = ui->context;
    if (word >= ES2_PALETTE_COUNT) return;
    if (!a->palette_state[word]) {
        es2_dpl dpl;
        es2_span f = es2_palette_file(word) ? find(a, es2_palette_file(word)) : (es2_span){NULL, 0};
        a->palette_state[word] = -1;
        if (f.data && es2_dpl_parse(f, &dpl, NULL) == ES2_OK) {
            memset(a->records[word], 0, sizeof a->records[word]);
            for (size_t i = 0; i < 256 && i < dpl.colour_count; i++) {
                es2_dpl_colour col;
                if (es2_dpl_colour_at(&dpl, i, &col, NULL) != ES2_OK) break;
                a->records[word][i][0] = col.red;
                a->records[word][i][1] = col.green;
                a->records[word][i][2] = col.blue;
                a->records[word][i][3] = col.flags;
            }
            a->palette_state[word] = 1;
        }
    }
    if (a->palette_state[word] > 0) es2_gui_palette_select(&ui->shell->frame, (const uint8_t(*)[4])a->records[word]);
    es2_gui_palette_apply(&ui->shell->frame);
}

/* An entry of a BIN catalogue up to its NUL (0x4087f0), copied into a ring. */
static const char *catalogue(es2_app *a, es2_span cat, uint32_t id) {
    es2_span t;
    char *out;
    if (!cat.data || es2m_bin_raw(cat, id, &t, NULL) != ES2_OK) return NULL;
    out = a->texts[a->text_next++ % TEXTS];
    size_t n = t.size < sizeof a->texts[0] - 1 ? t.size : sizeof a->texts[0] - 1;
    memcpy(out, t.data, n);
    out[n] = 0;
    return out;
}
static const char *text(void *ctx, uint32_t id) { es2_app *a = ctx; return catalogue(a, a->estext, id); }
static const char *text_int(void *ctx, int id) { return text(ctx, (uint32_t)id); }
static const char *desc(void *ctx, uint32_t index) { es2_app *a = ctx; return catalogue(a, a->wpn_desc, index); }
/* A NUL-terminated literal of VSHELL.EXE by address. The reads go to
 * the span store (ES2_VSHELL_STORE); no file gates them. */
static const char *literal(void *ctx, uint32_t address) {
    es2_app *a = ctx;
    es2_span s;
    char *out;
    for (uint32_t n = 1; n < sizeof a->texts[0]; n++) {
        if (es2_shell_exe_span(ES2_VSHELL_STORE, address, n, &s, NULL) != ES2_OK) return "";
        if (!s.data[n - 1]) {
            es2_shell_span_log("literal", address, n);   /* the span store's measurement switch */
            out = a->texts[a->text_next++ % TEXTS];
            memcpy(out, s.data, n);
            return out;
        }
    }
    return "";
}

/* ---- sounds (through gui/app_sound.h) ------------------------------------------------ */
/* VSHELL reads its four samples from VOL\SHLSOUND.VOL through the VOL
 * manager (audio.md, "Files and playback format"); the resources' VFS above holds only
 * ZONES, SHELL0, SHELL1 and LANG0, where no hmi\ member exists. The
 * installation's archive, else the CD's (as open_vfs); missing: silent. */
static const es2_vfs *open_sound_vfs(es2_app *a) {
    char path[ES2_HOST_PATH + 32];
    size_t size = 0;
    es2_error e;
    a->sound_vol_bytes = es2_host_read("VOL\\SHLSOUND.VOL", &size);
    if (!a->sound_vol_bytes && a->h->io.cd_root[0]) {
        snprintf(path, sizeof path, "%s\\VOL\\SHLSOUND.VOL", a->h->io.cd_root);
        a->sound_vol_bytes = es2_host_read(path, &size);
    }
    if (!a->sound_vol_bytes || es2_vol_open((es2_span){a->sound_vol_bytes, size}, &a->sound_vol, &e) != ES2_OK) {
        es2_host_say(a->h, "! SHLSOUND.VOL could not be opened\n");
        return NULL;
    }
    a->sound_source.kind = ES2_VFS_VOL;
    a->sound_source.data.vol = &a->sound_vol;
    return es2_vfs_open(&a->sound_source, 1, &a->sound_vfs, &e) == ES2_OK ? &a->sound_vfs : NULL;
}
static void sound_log(void *ctx, const char *line) { logf_(ctx, "%s", line); }
static void effect(es2_app *a, int which) { es2_app_sound_effect(a->sound, a->h->flow.prefs.current, which); }
static void click_sound(es2_ui *ui) { effect(ui->context, ES2_APP_SOUND_BUTTON); }   /* 0x42f547 */
/* The fades run the shell's pump on every pass (VSHELL's 0x406328 at
 * 0x42f8e9/0x42f86f): messages go to the window procedure's path (clicks and
 * keys queued for after the fade, paints, focus, close); the session's end
 * (WM_QUIT) ends the fade and the shell. */
static int ramp_pump(void *ctx) {
    es2_gui_shell *sh = ((es2_app *)ctx)->shell;
    if (!sh || !sh->platform.pump) return 0;
    if (!sh->platform.pump(sh->platform.context, sh)) return 0;
    sh->quit = 1;
    return 1;
}
static void ramp(es2_app *a, int up, const uint8_t prefs[ES2_SHELL_PREFS_SIZE]) {
    if (!a->platform.headless)
        es2_app_sound_ramp(a->sound, up, prefs, a->platform.clock, a->platform.clock_context, ramp_pump, a);
}
static void audio_call(es2_app *a, uint32_t address) {
    switch (address) {
    case ES2_MAIN_MENU_SOUND_SWITCH: es2_app_sound_background(a->sound, a->h->flow.prefs.current); break;
    case ES2_MAIN_MENU_SOUND_FADE_IN: ramp(a, 1, a->h->flow.prefs.current); break;
    case ES2_MISSION_MUSIC_FADE_OUT: ramp(a, 0, a->h->flow.prefs.current); break;
    case ES2_MISSION_FOCUS_LOST: es2_app_sound_focus(a->sound, 0); break;
    case ES2_SERVICE_BAY_SOUND: effect(a, ES2_APP_SOUND_BAY); break;
    default: break;
    }
}
static void main_sound(void *ctx, uint32_t address) { audio_call(ctx, address); }
static void mission_audio(void *ctx, uint32_t address) { audio_call(ctx, address); }
static void prefs_music(void *ctx, uint32_t address, const uint8_t prefs[ES2_SHELL_PREFS_SIZE]) {
    ramp(ctx, address == ES2_PREFERENCES_MUSIC_IN, prefs);
}

/* ---- the game state the screens read ------------------------------------------------------- */
static void career(es2_app *a, int16_t *group, int16_t *position) {
    uint8_t selection[ES2_SHELL_BRIEFING_SIZE];
    *group = 0;
    *position = 0;
    if (a->h->io.engine.memory.data)
        es2_shell_briefing_selection((es2_span){a->h->io.engine.memory.data, a->h->io.engine.memory.size}, selection,
                                     group, position, NULL);
}
static void read_slots(es2_app *a) {
    es2_shell_slot_table t;
    size_t size;
    uint8_t *file = es2_host_read("SAV\\GAMEFILE.STR", &size);
    if (file && es2_shell_slots_load((es2_span){file, size}, a->h->empty_text, &t, NULL) == ES2_OK) {
        a->slots = t;
        for (int k = 0; k < ES2_SHELL_STATS_SLOTS && k < ES2_SHELL_SLOT_COUNT; k++) {
            char path[64];
            uint8_t *sav;
            size_t n;
            if (!t.slot[k].flag || !t.slot[k].file[0]) continue;
            snprintf(path, sizeof path, "SAV\\%s", t.slot[k].file);
            if ((sav = es2_host_read(path, &n))) es2_shell_stats_read((es2_span){sav, n}, &a->stats[k], NULL);
            free(sav);
        }
    }
    free(file);
    a->menu.continue_flag = a->slots.slot[ES2_FLOW_SLOT_CAMPAIGN].flag != 0;   /* 0x482afd */
}

/* ---- the presenter: the screen the flow names -------------------------------------------- */
static void show_screen(es2_app *a, es2_shell_flow_screen screen) {
    es2_shell_flow *f = &a->h->flow;
    a->shown = screen;
    switch (screen) {
    case ES2_FLOW_MAIN_MENU: read_slots(a); es2_main_menu_show(&a->menu); break;          /* 0x431718 */
    case ES2_FLOW_REGISTRATION: es2_screen_registration_show(&a->registration); break;    /* 0x43c29a */
    case ES2_FLOW_SAVED_GAMES: read_slots(a); es2_saved_games_show(&a->saved); break;      /* 0x43a19c */
    case ES2_FLOW_PREFERENCES: es2_preferences_show(&a->prefs); break;                    /* 0x436d45 */
    case ES2_FLOW_PRACTICE: es2_practice_screen_show(&a->practice); break;                /* 0x44c322 */
    case ES2_FLOW_SERVICE_BAY: es2_service_bay_show(&a->bay); break;                      /* 0x43b7f2(8) + 0x43b758 */
    }
    (void)f;
}

/* After every flow event (the screens' `changed`) and every pass: the quit
 * flag, the boxes, and the screen the flow names when no handler showed it. */
/* The bay selection 0x482bc9 is the flow's bay_select (the text menu
 * reads it too). The flow resets it (load, new game, game_created); the
 * squad panel and the editors change the bay session's copy. */
static void bay_select_sync(es2_app *a) {
    es2_shell_flow *f = &a->h->flow;
    if (f->bay_select != a->bay_select_seen) a->bay.session.bay = (int16_t)f->bay_select;   /* the flow's reset */
    else f->bay_select = a->bay.session.bay;
    a->bay_select_seen = f->bay_select;
}
static void debug_undo(es2_app *a);
static void bay_snapshot(es2_app *a);
static void sync(es2_app *a) {
    es2_shell_flow *f = &a->h->flow;
    if (!a->built) return;
    debug_undo(a);
    es2_host_debug_ask(a->h);   /* the mode-1 DEBUG dialog of -@ */
    bay_snapshot(a);
    bay_select_sync(a);
    if (f->quit) {
        a->shell->quit = 1;
        return;
    }
    es2_boxes_sync(&a->boxes);
    if (f->screen != a->shown) show_screen(a, f->screen);
}
static void changed(void *ctx, uint32_t handler, es2_shell_flow_result result) {
    es2_app *a = ctx;
    if (a->built) bay_select_sync(a);
    debug_undo(a);
    if (a->built) es2_host_debug_ask(a->h);
    logf_(a, "event 0x%06x result %d screen %d word %d box %d\n", (unsigned)handler, (int)result, (int)a->h->flow.screen,
          a->h->flow.bay_word, (int)a->h->flow.box);
    if (a->h->flow.quit) a->shell->quit = 1;
}

/* ---- the mission map ----------------------------------------------------------------- */
/* A game file: the game directory's DATA\<name>, else the CD's. */
static uint8_t *data_file(es2_app *a, const char *name, size_t *size) {
    char path[ES2_HOST_PATH + 32];
    uint8_t *data;
    snprintf(path, sizeof path, "DATA\\%s", name);
    if ((data = es2_host_read(path, size))) return data;
    if (!a->h->io.cd_root[0]) return NULL;
    snprintf(path, sizeof path, "%s\\DATA\\%s", a->h->io.cd_root, name);
    return es2_host_read(path, size);
}
/* 0x40e75f after a load and a mission preparation (its callers 0x40ec14,
 * 0x44dba3, 0x44dca9): the old map object deleted, the new one built from
 * DATA's script.dat, player.mec and mission.str, data\mforms.dat and
 * data\mat0.dat, the zone's dat\zone<N>.dat and dba\zone<N>.dba (VOL),
 * dba\mis_icon.dba and the executable's cosine and arc-tangent tables. */
static void map_build(es2_app *a) {
    es2_mission_map_inputs in;
    size_t sizes[5] = {0};
    uint8_t *files[5] = {NULL};
    static const char *const names[5] = {"script.dat", "player.mec", "MISSION.STR", "MFORMS.DAT", "MAT0.DAT"};
    es2_span cosine, atan, zdat, zdba;
    es2_dba grid;
    es2_dbm cell;
    char name[64];
    if (!a->built) return;
    if (a->map_built) es2_mission_map_free(&a->map);
    memset(&a->map, 0, sizeof a->map);
    a->map_built = 0;
    if (!a->map_globals) {   /* one set per process; the generator 0x48110a from the table 0x47f850 */
        es2_span table;
        if (es2_shell_exe_span(ES2_VSHELL_STORE, 0x47f850u, 112, &table, NULL) != ES2_OK ||
            es2_shell_rng_init(table, 0, &a->map_g.material_rng, NULL) != ES2_OK)
            return;
        for (int i = 0; i < ES2_MAP_ICONS; i++) {
            const es2_bitmap *b = bitmap(a, "dba\\mis_icon.dba", i);
            if (b) a->map_icons[i] = *b;
        }
        a->map_globals = 1;
    }
    for (int i = 0; i < 5; i++) files[i] = i < 3 ? es2_host_read(i == 0 ? "DATA\\script.dat" : i == 1 ? "DATA\\player.mec" : "DATA\\MISSION.STR", &sizes[i])
                                                 : data_file(a, names[i], &sizes[i]);
    memset(&in, 0, sizeof in);
    if (!files[0] || !files[1] || !files[3] || !files[4] || sizes[4] < 12 ||
        es2_shell_exe_span(ES2_VSHELL_STORE, ES2_MAP_COSINE_OFFSET, ES2_MAP_COSINE_SIZE, &cosine, NULL) != ES2_OK ||
        es2_shell_exe_span(ES2_VSHELL_STORE, ES2_MAP_ATAN_OFFSET, ES2_MAP_ATAN_SIZE, &atan, NULL) != ES2_OK)
        goto done;
    in.script = (es2_span){files[0], sizes[0]};
    snprintf(name, sizeof name, "dat\\zone%d.dat", es2_mission_map_zone(in.script));
    zdat = find(a, name);
    snprintf(name, sizeof name, "dba\\zone%d.dba", es2_mission_map_zone(in.script));
    zdba = find(a, name);
    if (zdat.size < 16 || !zdba.data || es2_dba_parse(zdba, &grid, NULL) != ES2_OK ||
        es2_dba_image_at(&grid, 0, &cell, NULL) != ES2_OK)
        goto done;
    in.player_mec = (es2_span){files[1], sizes[1]};
    in.mforms = (es2_span){files[3], sizes[3]};
    in.cell_shift = (int32_t)(zdat.data[8] | zdat.data[9] << 8 | zdat.data[10] << 16 | (uint32_t)zdat.data[11] << 24);
    in.grid_width = cell.width;
    in.grid_height = cell.height;
    in.grid_bytes = cell.pixels;
    in.material_count = (uint32_t)(files[4][0] | files[4][1] << 8 | files[4][2] << 16 | (uint32_t)files[4][3] << 24);
    in.material_level = (int32_t)(files[4][8] | files[4][9] << 8 | files[4][10] << 16 | (uint32_t)files[4][11] << 24);
    in.mission_lines = files[2] && sizes[2] >= 6 && (files[2][0] | files[2][1] << 8) >= 2
                           ? (int16_t)(files[2][4] | files[2][5] << 8) : 0;
    in.icons = a->map_icons;
    in.icon_count = ES2_MAP_ICONS;
    in.cosine = cosine;
    in.atan = atan;
    a->map_built = es2_mission_map_build(&a->map, &in, &a->map_g) == 0;
done:
    logf_(a, "map %s\n", a->map_built ? "built" : "not built");
    for (int i = 0; i < 5; i++) free(files[i]);
}
/* 0x406264: the rectangle [0x481920] the map build 0x427e75 binds,
 * (0, 0)-(639, 399), presented at once (the capture's dst during the
 * fly-over). */
static void map_present(es2_app *a) {
    es2_gui_rect r = {0, 0, ES2_GUI_WIDTH - 1, 399};
    es2_gui_screen_present_rect(&a->shell->screen, &a->shell->frame, r);
    if (a->shell->platform.present) a->shell->platform.present(a->shell->platform.context, a->shell, ES2_GUI_PRESENT_DIRTY, &r, 1);
}
static uint32_t map_tick(es2_app *a) {   /* [0x471dc4]: 16 ms units */
    return (a->platform.clock ? a->platform.clock(a->platform.clock_context) : 0) / 16u;
}
/* (vshell_ui.md 42.6): the first paint's busy waits, which the step
 * only counted (map->wait_ticks), spent in wall time before the step's
 * present, as VSHELL waits inside its paint: no present and no pump
 * meanwhile, so the screen keeps what was presented before the step. The
 * headless clock runs only in the pump, so it is not waited on: the
 * virtual-clock sequence stays as it was. The present record notes the
 * wait either way. */
static void map_wait(es2_app *a, uint32_t now) {
    uint32_t target = now + a->map.wait_ticks, reads = 0;
    uint64_t start = es2_os_counter(), end;
    if (!a->platform.headless && a->platform.clock)
        reads = es2_mission_map_wait(target, a->platform.clock, a->platform.clock_context);
    end = es2_os_counter();
    if (a->present_log) {
        es2_os_fprintf(a->present_log,
                "{\"kind\":\"wait\",\"seq\":%u,\"step\":0,\"qpc\":%lld,\"qpc_end\":%lld,\"tick\":%u,\"ticks\":%u,"
                "\"wall\":%d,\"reads\":%u}\n",
                ++a->present_seq, (long long)start, (long long)end, (unsigned)now,
                (unsigned)a->map.wait_ticks, !a->platform.headless && a->platform.clock, (unsigned)reads);
        es2_os_fflush(a->present_log);
    }
}
/* Main-loop step 6 (0x4019c6) while the mission screen asks for it
 * ([0x46c269]): the palette request [0x46c26a] applied, the key ring
 * flushed, [0x46c26c] := 1, [0x46c270] := 0; then the fly-over step, a
 * present and the pump until the step returns 0; Esc or Space in the ring or
 * a click ([0x46c270]) skip (0x425a43); [0x46c26c] := 0. */
static void map_animation(es2_app *a) {
    es2_gui_shell *sh = a->shell;
    es2_gui_input *in = &sh->input;
    unsigned esc = es2_gui_key_index(0x1b), space = es2_gui_key_index(0x20);
    if (!a->mission.map_pending) return;
    a->mission.map_pending = 0;
    if (a->mission.map_palette >= 0 && a->ui.palette) a->ui.palette(&a->ui, (uint16_t)a->mission.map_palette);
    in->ring_read = in->ring_write;
    in->map_animation = 1;
    in->map_skip = 0;
    for (int guard = 0; a->map_built && guard < 100000; guard++) {
        uint32_t now = map_tick(a);
        int more = es2_mission_map_step(&a->map, &a->map_g, now, sh->frame.pixels);
        if (a->map.wait_ticks) map_wait(a, now);
        map_present(a);
        for (; in->ring_read != in->ring_write; in->ring_read = (in->ring_read + 1) % ES2_GUI_KEY_RING) {
            unsigned word = in->ring[in->ring_read];
            if (!(word & ES2_GUI_KEY_UP) && ((word & 0x7f) == esc || (word & 0x7f) == space)) in->map_skip = 1;
        }
        if (in->map_skip) {
            es2_mission_map_skip(&a->map);
            in->map_skip = 0;
        }
        if (!more || (sh->platform.pump && sh->platform.pump(sh->platform.context, sh)) || sh->quit) break;
    }
    in->map_animation = 0;
}
/* The six arrows 0x445577 .. 0x445982: 1 to 4 steps by the button's repeat
 * counter, each painted and presented (only with a map object). */
static void mission_map_arrow(void *ctx, uint32_t handler) {
    static const uint32_t handlers[6] = {0x445577u, 0x445646u, 0x445715u, 0x4457e4u, 0x4458b3u, 0x445982u};
    static const es2_mission_map_arrow arrows[6] = {ES2_MAP_NORTH, ES2_MAP_SOUTH, ES2_MAP_WEST, ES2_MAP_EAST,
                                                    ES2_MAP_ZOOM_IN, ES2_MAP_ZOOM_OUT};
    es2_app *a = ctx;
    for (int k = 0; k < 6; k++) {
        if (handlers[k] != handler) continue;
        es2_ui_widget *w = es2_ui_get(&a->ui, a->mission.ids[ES2_MISSION_ARROW + 2 * k]);
        int n = es2_mission_map_arrow_steps(w ? w->repeat_count : 0);
        for (int i = 0; a->map_built && i < n; i++) {
            es2_mission_map_arrow_step(&a->map, &a->map_g, arrows[k], a->shell->frame.pixels);
            map_present(a);
        }
    }
}

/* The flow's load and new game clear the hangar selection 0x410aaf
 * (vshell_state.md); a load, the campaign's preparation and the next
 * preparation build the map object (0x40e75f). */
static int wrap_load(void *ctx, int slot, int16_t *final_word) {
    es2_app *a = ctx;
    int r = a->flow_host.load(a->flow_host.ctx, slot, final_word);
    if (r > 0) {
        es2_service_bay_game_loaded(&a->bay);
        map_build(a);
    }
    return r;
}
static int wrap_new_game(void *ctx, const char *name, uint16_t rank, int16_t *final_word) {
    es2_app *a = ctx;
    int r = a->flow_host.new_game(a->flow_host.ctx, name, rank, final_word);
    if (r != 0) es2_service_bay_game_loaded(&a->bay);
    if (r > 0 && !a->h->flow.debug_cheat) map_build(a);   /* the Use Default preparation 0x44dba3 (-@: after the answer) */
    return r;
}
static int wrap_store_mode(void *ctx, uint8_t m) { es2_app *a = ctx; return a->flow_host.store_mode(a->flow_host.ctx, m); }
static int wrap_write_prefs(void *ctx, const uint8_t b[ES2_SHELL_PREFS_SIZE]) { es2_app *a = ctx; return a->flow_host.write_prefs(a->flow_host.ctx, b); }
static int wrap_save(void *ctx, int s, const char *t) { es2_app *a = ctx; return a->flow_host.save(a->flow_host.ctx, s, t); }
static int wrap_campaign(void *ctx) {
    es2_app *a = ctx;
    int r = a->flow_host.campaign(a->flow_host.ctx);
    if (r) map_build(a);   /* a slot without its copies was prepared */
    return r;
}
static int wrap_launch(void *ctx, int *w) { es2_app *a = ctx; return a->flow_host.launch(a->flow_host.ctx, w); }
static int wrap_rebalance(void *ctx, uint8_t b) { es2_app *a = ctx; return a->flow_host.rebalance(a->flow_host.ctx, b); }
static int wrap_results(void *ctx, es2_shell_flow_results *r) { es2_app *a = ctx; return a->flow_host.results(a->flow_host.ctx, r); }
static int wrap_prepare_next(void *ctx) {
    es2_app *a = ctx;
    int r = a->flow_host.prepare_next(a->flow_host.ctx);
    if (r) map_build(a);   /* Use Default 0x44dba3 */
    return r;
}
/* The DEBUG dialog's answer on a mode-1 path (0x44dba3 / 0x44dca9: the map) */
static int wrap_debug_prepare(void *ctx, const char *mission) {
    es2_app *a = ctx;
    int r = a->flow_host.debug_prepare && a->flow_host.debug_prepare(a->flow_host.ctx, mission);
    if (r) map_build(a);
    return r;
}
static void wrap_replay(void *ctx) { es2_app *a = ctx; if (a->flow_host.replay) a->flow_host.replay(a->flow_host.ctx); }
static void wrap_manual(void *ctx) {
    es2_app *a = ctx;
    es2_app_sound_focus(a->sound, 0);   /* 0x42f8ff: the sound focus is given up */
    if (a->flow_host.online_manual) a->flow_host.online_manual(a->flow_host.ctx);
}
/* The display word 0x481f4c. */
static int full_screen(void *ctx) { es2_app *a = ctx; return a->shell->fullscreen; }

/* ---- the movie player ---------------------------------------------------------------- */
static es2_ui_id object(es2_app *a, int32_t address) {
    switch (address) {
    case ES2_SHELL_MOVIE_OBJ_ROOT: return a->ui.root;
    case ES2_SHELL_MOVIE_OBJ_BAY: return a->bay.bay[ES2_BAY_FRAME];
    case ES2_SHELL_MOVIE_OBJ_TREES: return a->mission.ids[ES2_MISSION_BUTTON_ROW];
    case ES2_SHELL_MOVIE_OBJ_MAP: return a->mission.ids[ES2_MISSION_MAP_FRAME];
    case ES2_SHELL_MOVIE_OBJ_TITLE: return a->mission.ids[ES2_MISSION_TITLE];
    case ES2_SHELL_MOVIE_OBJ_PAGE: return a->mission.ids[ES2_MISSION_TEXT_FRAME];
    default: return 0;
    }
}
static void pressed(es2_app *a, int control, int value) {
    es2_ui_widget *w = es2_ui_get(&a->ui, a->bay.bay[control]);
    if (!w) return;
    w->pressed = value;
    es2_ui_draw(&a->ui, a->bay.bay[control]);
}
static int cd_box(void *context) { return es2_boxes_cd_prompt(&((es2_app *)context)->boxes); }
/* The frame's rectangle in the window's client pixels, by the
 * presenter's rule (es2_gui_layout_compute over the client size, which is
 * the SDL renderer's output size): the movie window is scaled and centred
 * with the picture (VSHELL switched the display to 640 x 480 instead). */
static int movie_frame(void *context, es2_mci_rect *out) {
    es2_app *a = context;
    int width = 0, height = 0;
    es2_gui_layout l;
    if (!a->player.parent || !a->platform.client_size ||
        !a->platform.client_size(a->platform.window_context, &width, &height) ||
        !es2_gui_layout_compute(width, height, &l))
        return 0;
    out->x = l.x;
    out->y = l.y;
    out->width = l.width;
    out->height = l.height;
    return 1;
}
/* The shell's pump (VSHELL's 0x406328 into its window procedure, so
 * the focus word 0x46c288 and the input path see every message); a quit
 * ends the player as WM_QUIT does. */
static void movie_pump(es2_app *a) {
    es2_gui_shell *sh = a->shell;
    if (sh->platform.pump && sh->platform.pump(sh->platform.context, sh)) a->player.quit = 1;
    /* the player's wait for the focus (0x41ee6c) polls; own: at most 10 ms
     * idle per pass instead of a busy spin */
    if (!sh->focused && !a->player.quit && a->platform.idle) a->platform.idle(a->platform.window_context, 10);
}
/* VSHELL's loop 0x41e79f..0x41e7f9 empties the key ring before it
 * plays (0x40586d: read index := write index) and pops one key per pass
 * (0x404d0d), so no key pressed during a movie survives it; its window
 * procedure does not post button messages while 0x470eb0 is set
 * (0x40512c, 0x405191, 0x4051b8, 0x4051e1). The own player dispatches the
 * messages to SDL, which queues them as events: they go through the input
 * path here while the movie flag is still set (button messages dropped,
 * keys into the ring), then the ring is emptied. Without this the click
 * that skipped CREDITS reached the main menu after the movie and started
 * it again. */
static void movie_input_begin(es2_app *a) {
    es2_gui_input *in = &a->shell->input;
    in->ring_read = in->ring_write;   /* 0x40586d */
    in->movie_skip = 0;               /* 0x41e774 */
}
static void movie_input_end(es2_app *a) {
    es2_gui_input *in = &a->shell->input;
    movie_pump(a);
    in->ring_read = in->ring_write;   /* the passes' 0x404d0d */
    in->movie_skip = 0;
}
static int32_t movie_call(void *context, uint32_t action, int32_t arg, const es2_shell_movie_record *r) {
    es2_app *a = context;
    switch (action) {
    case ES2_SHELL_MOVIE_OPEN_DEVICE:
        if (a->platform.headless) return 1;
        a->shell->input.movie_open = 1;
        return es2_mci_player_call(&a->player, action, arg, r);
    case ES2_SHELL_MOVIE_PLAY:
        if (a->platform.headless) {
            char path[64];
            int language = !strcmp(a->h->args.language, "FRE") ? 1 : !strcmp(a->h->args.language, "GER") ? 2 : 0;
            if (r && es2_shell_movie_path(r->index, language, path, sizeof path) == 0) logf_(a, "movie %s\n", path);
            return ES2_SHELL_MOVIE_ENDED;
        }
        {
            int32_t result;
            movie_input_begin(a);
            result = es2_mci_player_call(&a->player, action, arg, r);
            movie_input_end(a);
            return result;
        }
    case ES2_SHELL_MOVIE_CLOSE_DEVICE:
        a->shell->input.movie_open = 0;
        return a->platform.headless ? 0 : es2_mci_player_call(&a->player, action, arg, r);
    case ES2_SHELL_MOVIE_PUMP:
        if (!a->platform.headless) movie_pump(a);
        return 0;
    case ES2_SHELL_MOVIE_INTRO_ERROR:
        return a->platform.headless ? 0 : es2_mci_player_call(&a->player, action, arg, r);
    case ES2_SHELL_MOVIE_CD_PROMPT: {
        int quit = 0, retry = es2_shell_cd_prompt(a->h->cd_policy, cd_box, a, &quit);
        if (quit) a->h->flow.quit = 1;
        return retry;
    }
    case ES2_SHELL_MOVIE_FADE_OUT: ramp(a, 0, a->h->flow.prefs.current); return 0;
    case ES2_SHELL_MOVIE_FADE_IN: ramp(a, 1, a->h->flow.prefs.current); return 0;
    case ES2_SHELL_MOVIE_FOCUS_LOST: es2_app_sound_focus(a->sound, 0); return 0;
    case ES2_SHELL_MOVIE_FOCUS_GAINED: es2_app_sound_focus(a->sound, 1); return 0;
    case ES2_SHELL_MOVIE_PALETTE: if (a->ui.palette) a->ui.palette(&a->ui, (uint16_t)arg); return 0;
    case ES2_SHELL_MOVIE_PALETTE_APPLY: es2_gui_palette_apply(&a->shell->frame); return 0;
    case ES2_SHELL_MOVIE_BAY_BUTTONS:
        for (int k = ES2_BAY_MAIN_MENU; k < ES2_BAY_MISSION; k += 2) pressed(a, k, 0);
        pressed(a, ES2_BAY_MISSION, 1);
        return 0;
    case ES2_SHELL_MOVIE_MISSION_BUTTON:
        pressed(a, ES2_BAY_MISSION, 0);
        a->bay.session.screen = ES2_SHELL_SCREEN_BAY;
        a->h->flow.bay_word = ES2_FLOW_WORD_BAY;
        return 0;
    case ES2_SHELL_MOVIE_SHOW: es2_ui_show(&a->ui, object(a, arg)); return 0;
    case ES2_SHELL_MOVIE_HIDE: es2_ui_hide(&a->ui, object(a, arg)); return 0;
    case ES2_SHELL_MOVIE_REPAINT: es2_ui_redraw_tree(&a->ui, object(a, arg)); return 0;
    case ES2_SHELL_MOVIE_DRAIN: es2_boxes_drain_helper(a->shell); return 0;
    case ES2_SHELL_MOVIE_TEXT_PANEL: es2_mission_screen_panel(&a->mission, arg); return 0;
    case ES2_SHELL_MOVIE_BUSY: es2_gui_set_busy(a->shell, arg); return 0;
    case ES2_SHELL_MOVIE_CLEAR: es2_gui_shell_clear(a->shell); return 0;
    case ES2_SHELL_MOVIE_BAY_SELECT: if (a->ui.palette) a->ui.palette(&a->ui, (uint16_t)arg); return 0;
    case ES2_SHELL_MOVIE_TEARDOWN: es2_mission_screen_leave(&a->mission); return 0;
    case ES2_SHELL_MOVIE_THEATRE_STEP: return es2_mission_screen_theatre(&a->mission) > 0;
    case ES2_SHELL_MOVIE_THEATRE_HIDE: es2_mission_screen_theatre_hide(&a->mission); return 0;
    case ES2_SHELL_MOVIE_WAIT:
        if (a->platform.headless) return 0;
        if (a->platform.sleep) a->platform.sleep(a->platform.window_context, (uint32_t)(arg > 0 ? arg : 0) * 1000u);
        else es2_os_sleep((uint32_t)(arg > 0 ? arg : 0) * 1000u);
        return 0;
    default: return 0;
    }
}
static int32_t movie_query(void *context, int what) {
    es2_app *a = context;
    es2_ui_widget *w;
    switch (what) {
    case ES2_SHELL_MOVIE_Q_BAY_BAR:
        w = es2_ui_get(&a->ui, a->bay.bay[ES2_BAY_BAR]);
        return w && !(w->flags & ES2_UI_HIDDEN);
    case ES2_SHELL_MOVIE_Q_PANEL:
        w = es2_ui_get(&a->ui, a->mission.ids[ES2_MISSION_THEATRE]);
        return w && !(w->flags & ES2_UI_HIDDEN);
    case ES2_SHELL_MOVIE_Q_FULLSCREEN: return a->shell->fullscreen;
    case ES2_SHELL_MOVIE_Q_PAGE: return a->h->flow.page;
    case ES2_SHELL_MOVIE_Q_FOCUS: return a->shell->focused;
    case ES2_SHELL_MOVIE_Q_QUIT: return a->h->flow.quit || a->shell->quit || a->player.quit;
    default: return 0;
    }
}
/* 0x41e98c(build_palette) over the queue; the movie group of the theatre
 * step is the CAREER group (the mission screen reads it through its host). */
static void play(es2_app *a, int build_palette) {
    es2_shell_movie_hooks hooks = {a, movie_call, movie_query};
    if (a->playing) return;
    a->playing = 1;
    if (es2_shell_movies_play(&a->h->movies, build_palette, &hooks)) a->h->flow.quit = 1;
    a->playing = 0;
    if (a->h->flow.quit) a->shell->quit = 1;
}
static void menu_play(void *ctx, es2_shell_movies *movies, int build_palette) { (void)movies; play(ctx, build_palette); }

/* ---- the main menu's host ---------------------------------------------------------- */
static const es2_bitmap *menu_bitmap(void *ctx, const char *file) { return bitmap(ctx, file, -1); }
static void notice(void *ctx, int message, int caption) {
    es2_app *a = ctx;
    /* 0x401a81(6, 2, 0x30): error.str's message and caption as the
     * start-up reader keeps them (an unfilled slot is an empty text) */
    char line[256], title[96];
    es2_host_error_text(a->h, 0, message, line, sizeof line);
    es2_host_error_text(a->h, 1, caption, title, sizeof title);
    logf_(a, "notice error.str %d caption %d: \"%s\" / \"%s\"\n", message, caption, line, title);
    if (!a->platform.headless && a->platform.message_box)
        a->platform.message_box(a->platform.window_context, line, title, 0x30);   /* MB_ICONEXCLAMATION */
}
static int store_mode0(es2_app *a) {
    return a->h->flow.host.store_mode ? a->h->flow.host.store_mode(a->h->flow.host.ctx, 0) : 1;   /* 0x40ec56(0) */
}
static int entry(void *ctx, uint32_t handler, int stage) {
    es2_app *a = ctx;
    es2_host *h = a->h;
    if (handler == ES2_FLOW_PRACTICE_MISSIONS) {
        /* 0x431f3a: the show 0x44c322 (entry 0), then the mode store */
        if (!es2_host_practice_open(h)) return 0;
        a->shown = ES2_FLOW_PRACTICE;
        es2_practice_screen_show(&a->practice);
        return store_mode0(a);
    }
    if (handler != ES2_FLOW_INSTANT_ACTION) return 1;
    if (stage == 0) {
        /* 0x431935: the preset flag, the mode store and 0x44c58b's writes */
        uint8_t mode_out[ES2_SHELL_PREFS_SIZE], out[ES2_SHELL_PREFS_SIZE];
        size_t size;
        uint8_t *file;
        es2_status status;
        if (!es2_host_practice_open(h)) return 0;
        file = es2_host_read("DATA\\PREFS.CFG", &size);
        es2_span span = {file, size};
        status = es2_shell_instant_action(&h->practice.state, &h->flow.prefs, h->practice.tables, file ? &span : NULL,
                                          mode_out, out, &a->instant, NULL);
        free(file);
        if (status != ES2_OK) {
            es2_host_say(h, "! INSTANT ACTION refused: prefs.cfg +0x2e is %u (0..2)\n", h->flow.prefs.current[ES2_SHELL_PREFS_INSTANT]);
            return 0;
        }
        return es2_host_practice_write_prefs(h, mode_out) && es2_host_practice_write_prefs(h, out);
    }
    /* stage 1: new game in mode 0 and the launch (the flow's game_created inside) */
    if (es2_host_practice_launch(h, &a->instant, 1)) es2_shell_flow_dbsim(&h->flow);
    if (h->flow.quit) a->shell->quit = 1;
    return 1;
}
static void menu_changed(void *ctx, uint32_t handler, es2_shell_flow_result result) { changed(ctx, handler, result); }

/* ---- Saved games ------------------------------------------------------------------- */
static void saved_refresh(void *ctx, int slot) { (void)slot; read_slots(ctx); }   /* 0x430ec4 */
static void saved_changed(void *ctx, uint32_t handler, es2_shell_flow_result result) {
    read_slots(ctx);
    changed(ctx, handler, result);
}

/* ---- Preferences -------------------------------------------------------------------- */
static const es2_bitmap *member_bitmap(void *ctx, const char *file, int member) { return bitmap(ctx, file, member); }

/* ---- practice ------------------------------------------------------------------------ */
static int practice_launch(void *ctx, const es2_shell_practice_launch *launch) {
    es2_app *a = ctx;
    int code = es2_host_practice_launch(a->h, launch, 0);
    if (code == ES2_HOST_PRACTICE_PENDING) {
        /* The -@ DEBUG dialog is up; the practice screen's launch
         * effect (exit 2, quit) is taken back until a button answers */
        a->debug_undo = 1;
        return 1;
    }
    return code == ES2_FLOW_EXIT_DBSIM;
}

/* ---- The DEBUG dialog of -@ (0x413305 after the practice launch's new game) ---------- */
static int ui_debug_choice(void *ctx, char *typed, size_t capacity) {
    es2_app *a = ctx;
    (void)capacity;
    if (!a->built || !a->boxes.debug[0]) return 1;
    logf_(a, "debug dialog default %s\n", typed);
    a->debug_hold = 1;
    es2_boxes_debug_show(&a->boxes, typed);   /* with -@ no posted click */
    return ES2_HOST_DEBUG_PENDING;
}
static void results_next(es2_app *a);
static void debug_undo(es2_app *a) {
    es2_shell_flow *f = &a->h->flow;
    if (!a->debug_undo || !f->quit || f->exit_code != ES2_FLOW_EXIT_DBSIM) return;
    a->debug_undo = 0;
    f->quit = 0;
    f->exit_code = 0;
}
/* Use Default 0x44dbea / ACCEPT 0x44dc4d after the dialog hid itself: the
 * preparation, exit 2 and quit; a refused name or a failed preparation goes
 * back to the main menu (own, as the practice screen's failed launch). */
static void debug_answer(es2_app *a, const char *text) {
    es2_shell_flow *f = &a->h->flow;
    int code;
    if (!a->debug_hold) return;   /* without -@ the flow prepared already */
    a->debug_hold = 0;
    a->debug_undo = 0;
    if (!a->h->debug.pending) {
        /* A mode-1 path (flow.debug): the preparation and the bay
         * (the results path's: with the mission screen's page 4) */
        int next = f->debug == ES2_FLOW_DEBUG_NEXT;
        es2_shell_flow_result r = es2_host_debug_answer(a->h, text);
        if (r == ES2_FLOW_DONE && next) results_next(a);
        logf_(a, "debug %s result %d screen %d\n", text ? text : "(default)", (int)r, (int)f->screen);
        sync(a);
        return;
    }
    code = es2_host_practice_debug_answer(a->h, text);
    logf_(a, "debug %s exit %d\n", text ? text : "(default)", code);
    if (code == ES2_FLOW_EXIT_DBSIM) es2_shell_flow_dbsim(f);
    else (void)es2_practice_screen_press(&a->practice, ES2_FLOW_PRACTICE_MAIN_MENU, 1);
    if (f->quit) a->shell->quit = 1;
}
static void box_debug_default(void *ctx) { debug_answer(ctx, NULL); }
static void box_debug_accept(void *ctx, const char *text) { debug_answer(ctx, text); }

/* ---- the service bay and the squad panel -------------------------------------------- */
static void build_in(es2_app *a);
static void build_out(es2_app *a);
static int16_t open_from(es2_app *a);
static void bay_career(void *ctx, int16_t *group, int16_t *position, int *shown) {
    es2_app *a = ctx;
    career(a, group, position);
    *shown = a->h->movies.group_shown;
}
/* The squad panel's view of a bay from the memory state: the hangar record
 * (vehicle +0, progress +0x4a, built = 100), the Herc's name (ESTEXT 0x6e +
 * vehicle, as the text menu labels it), the pilot of the bay (the engine's
 * view: the player, else the first selected pilot) with its skill +0x25. The
 * condition is 0x4121f8 (es2_shell_record_condition). */
static int bay_view(void *ctx, int k, es2_squad_bay *out) {
    es2_app *a = ctx;
    es2_shell_engine *g = &a->h->io.engine;
    es2_shell_bay_view v;
    es2_sav sav;
    if (!g->memory.data || !g->tables || es2_shell_engine_bay_view(g, -1, -1, &v, NULL) != ES2_OK ||
        es2_sav_parse((es2_span){g->memory.data, g->memory.size}, &sav, NULL) != ES2_OK)
        return 0;
    for (size_t i = 0; i < sav.loadout_count; i++) {
        es2_sav_loadout l;
        if (es2_sav_loadout_at(&sav, i, &l, NULL) != ES2_OK || l.index != k) continue;
        out->record = 1;
        out->vehicle = (int16_t)es2_le16(l.fixed.data);
        out->progress = (int16_t)es2_le16(l.fixed.data + 70);
        out->finished = out->progress == 100;
        if (es2_shell_record_condition(l.fixed, &out->condition, NULL) != ES2_OK) out->condition = 100;
        out->herc = text(a, 0x6eu + (uint32_t)(uint16_t)out->vehicle);
        break;
    }
    if (v.pilot[k] >= 0) {
        es2_sav_pilot p;
        int ok = v.pilot[k] == 36 ? (p = sav.player, 1) : es2_sav_pilot_at(&sav, (size_t)v.pilot[k], &p, NULL) == ES2_OK;
        char *name = a->texts[a->text_next++ % TEXTS];
        snprintf(name, sizeof a->texts[0], "%s", v.name[k]);
        out->pilot = name;
        out->skill = ok && p.suffix.size > 4 ? (int16_t)es2_le16(p.suffix.data + 3) : 0;
    }
    return out->record || out->pilot;
}
static void bay_screen(void *ctx, int word, int show) {
    es2_app *a = ctx;
    es2_shell_flow *f = &a->h->flow;
    if (show) {
        switch (word) {
        case 0: show_screen(a, ES2_FLOW_MAIN_MENU); break;
        case 1: show_screen(a, ES2_FLOW_SAVED_GAMES); break;
        case 2:
            if (!eio_open(&a->h->io)) es2_host_engine_error(a->h);
            es2_weapons_screen_show(&a->weapons, a->bay.session.screen);
            break;
        case 3:
            if (!eio_open(&a->h->io)) es2_host_engine_error(a->h);
            es2_repair_screen_show(&a->repair, a->bay.session.screen);
            break;
        case 4: case 5: {
            int16_t previous = open_from(a);
            (void)previous;
            if (!eio_open(&a->h->io)) es2_host_engine_error(a->h);
            build_in(a);
            if (word == 4) es2_build_show(&a->build);
            else es2_armory_show(&a->build);
            build_out(a);
            break;
        }
        case 6: es2_crew_show(&a->crew); break;
        case 7: es2_mission_screen_show(&a->mission, f->page); break;
        default: break;
        }
    } else {
        switch (word) {
        case 2: es2_weapons_screen_hide(&a->weapons); break;
        case 3: es2_repair_screen_hide(&a->repair); break;
        case 4: es2_build_hide(&a->build); break;
        case 5: es2_armory_hide(&a->build); break;
        case 6: es2_crew_leave(&a->crew); break;
        case 7: es2_mission_screen_leave(&a->mission); break;
        default: break;
        }
    }
}
static void bay_selected(void *ctx, int word, int16_t k, int assign) {
    es2_app *a = ctx;
    (void)assign;
    switch (word) {
    case 2:                                                      /* 0x43df86 */
        es2_squad_pictures_marker_off(&a->bay);                  /* 0x43dfc8 */
        es2_weapons_screen_bay_changed(&a->weapons);
        break;
    case 3: es2_repair_screen_selected(&a->repair); break;       /* 0x434549, 0x43436f, 0x43433f, 0x433f86 */
    case 4:                                                      /* 0x447064 */
        build_in(a);
        es2_build_select_bay(&a->build, k);
        build_out(a);
        break;
    case 6: es2_crew_selected(&a->crew, k); break;               /* 0x4426e5 */
    default: break;
    }
}
static es2_ui_id repair_grid(void *ctx, int16_t k) { return es2_repair_screen_grid(&((es2_app *)ctx)->repair, k); }

/* ---- the Herc pictures -------------------------------------------------------------- */
/* The record of bay k from the memory state: vehicle +0, progress (fixed
 * byte 70), capacity (74), the 33 condition words (4..69) and the item ID of
 * each slot (the loadout's slot entries; a slot without one is a null
 * pointer, ID 0). */
static int pictures_record(void *ctx, int k, es2_squad_record *out) {
    es2_app *a = ctx;
    es2_shell_engine *g = &a->h->io.engine;
    es2_sav sav;
    if (!g->memory.data || es2_sav_parse((es2_span){g->memory.data, g->memory.size}, &sav, NULL) != ES2_OK) return 0;
    for (size_t i = 0; i < sav.loadout_count; i++) {
        es2_sav_loadout l;
        if (es2_sav_loadout_at(&sav, i, &l, NULL) != ES2_OK || l.index != k) continue;
        out->record = 1;
        out->vehicle = (int16_t)es2_le16(l.fixed.data);
        out->progress = (int16_t)es2_le16(l.fixed.data + 70);
        out->capacity = (int16_t)es2_le16(l.fixed.data + 74);
        for (int w = 0; w < 33; w++) out->words[w] = (int16_t)es2_le16(l.fixed.data + 4 + 2 * w);
        for (size_t e = 0; e < l.count; e++) {
            int16_t slot = (int16_t)es2_le16(l.items.data + 12 * e);
            if (slot >= 0 && slot < 10) out->item[slot] = (int16_t)es2_le16(l.items.data + 12 * e + 2);
        }
        return 1;
    }
    return 0;
}
static es2_span pictures_file(void *ctx, const char *name) { return find(ctx, name); }
static const es2_bitmap *pictures_frame(void *ctx, const char *file, int n) { return n < 0 ? NULL : bitmap(ctx, file, n); }
static const char *pictures_text(void *ctx, uint32_t id) { return text(ctx, id); }
/* 0x414a3c's assertion (src\hercdisp.cpp line 233): logged; the process's
 * exit 1 is not reproduced here (open point, vshell_ui.md section 36). */
static void pictures_error(void *ctx, const char *message) {
    es2_app *a = ctx;
    logf_(a, "assertion (not ended): Assert::err: Line 233 in \"src\\hercdisp.cpp\": %s\n", message);
}
/* 0x43cfa5 / 0x43cfea; the part groups (the results tables' first 24 words,
 * 0x46f9e4) are taken from the engine's tables, read by the time a screen
 * of the bar opens. */
static void bay_panel(void *ctx, int word, int load) {
    es2_app *a = ctx;
    for (int i = 0; i < 24; i++) a->pictures.groups[i] = (int16_t)es2_le16(a->h->io.tables.results + 2 * i);
    es2_squad_pictures_panel(&a->bay, word, load);
}
static void pictures_init(es2_app *a) {
    es2_squad_pictures *p = &a->pictures;
    es2_span s;
    p->host = (es2_squad_pictures_host){a, pictures_file, pictures_frame, pictures_record, pictures_error, pictures_text};
    if (es2_shell_exe_span(ES2_VSHELL_STORE, 0x46ffc0, 18, &s, NULL) == ES2_OK)
        for (int i = 0; i < 9; i++) p->internal_from[i] = (int16_t)es2_le16(s.data + 2 * i);
    int failed = es2_squad_pictures_load(p);
    if (failed) logf_(a, "herc pictures: %d files not read\n", failed);
    a->bay.pictures = p;
    a->bay.host.panel = bay_panel;
}
static void weapons_slot_shown(void *ctx, int16_t slot) { es2_squad_pictures_marker(&((es2_app *)ctx)->bay, slot); }

/* ---- WEAPONS ----------------------------------------------------------------------- */
static es2_span game_file(void *ctx, const char *name) { return find(ctx, name); }
static void weapons_select(void *ctx, int16_t k, int previous) {
    es2_app *a = ctx;
    int16_t screen = a->bay.session.screen;
    a->bay.session.screen = (int16_t)previous;
    es2_squad_panel_select(&a->bay, k, 0);
    a->bay.session.screen = screen;
    if (a->bay.session.bay != k) a->bay.session.bay = k;   /* the selection word (0x482bc9) */
}

/* WEAPONS' mounts and guidance stores as the text menu's `bay` steps
 * of the evidence log (engine_io's eio_bay, which chain_real_test's verify
 * replays through the original). The screen applies its operation to the
 * memory state itself; the operation is read back from the screen
 * (bay, slot, the slot's new item or the mode), the state before the pass is
 * put back and eio_bay applies and records it; the result must be the
 * screen's state byte for byte, else the screen's state stays and the step
 * is reported as not recorded. */
static void bay_snapshot(es2_app *a) {
    es2_shell_engine *g = &a->h->io.engine;
    a->mounts_seen = a->weapons.mounts;
    a->stores_seen = a->weapons.stores;
    a->bay_before_size = 0;
    if (!g->memory.data || !g->memory.size) return;
    if (a->bay_before_capacity < g->memory.size) {
        uint8_t *p = realloc(a->bay_before, g->memory.size);
        if (!p) return;
        a->bay_before = p;
        a->bay_before_capacity = g->memory.size;
    }
    memcpy(a->bay_before, g->memory.data, g->memory.size);
    a->bay_before_size = g->memory.size;
}
static void bay_step(es2_app *a) {
    es2_shell_engine *g = &a->h->io.engine;
    es2_shell_bay_view v;
    es2_shell_bay_op op;
    uint8_t *after;
    size_t n = g->memory.size;
    int16_t bay = a->bay.session.bay, slot = a->weapons.slot;
    memset(&op, 0, sizeof op);
    op.bay = bay;
    op.slot = slot;
    if (a->weapons.mounts != a->mounts_seen) {
        if (bay < 0 || bay > 7 || slot < 0 || slot > 9 || es2_shell_engine_bay_view(g, bay, slot, &v, NULL) != ES2_OK) goto fail;
        op.kind = ES2_SHELL_BAY_MOUNT;
        op.value = v.item[bay][slot];
    } else if (a->weapons.stores != a->stores_seen) {
        op.kind = ES2_SHELL_BAY_GUIDANCE;
        op.value = (int16_t)a->weapons.guidance_previous;
    } else
        return;
    /* the operation may change the SAV's size: the states are put back
     * through the engine's own set */
    if (!a->bay_before_size || !n || !(after = malloc(n))) goto fail;
    memcpy(after, g->memory.data, n);
    if (es2_shell_engine_set(g, (es2_span){a->bay_before, a->bay_before_size}, NULL) != ES2_OK ||
        !eio_bay(&a->h->io, &op) || g->memory.size != n || memcmp(g->memory.data, after, n)) {
        (void)es2_shell_engine_set(g, (es2_span){after, n}, NULL);
        free(after);
        goto fail;
    }
    free(after);
    logf_(a, "bay step %d %d %d %d applied %d\n", (int)op.kind, (int)op.bay, (int)op.slot, (int)op.value, op.applied);
    bay_snapshot(a);
    return;
fail:
    logf_(a, "! bay step (%d, %d, %d, %d) not recorded\n", (int)op.kind, (int)op.bay, (int)op.slot, (int)op.value);
    bay_snapshot(a);
}
static void weapons_changed(void *ctx, uint32_t handler) {
    es2_app *a = ctx;
    logf_(a, "weapons 0x%06x mounts %u stores %u\n", (unsigned)handler, a->weapons.mounts, a->weapons.stores);
    bay_step(a);
}

/* ---- REPAIR, BUILD, ARMORY, CREW ------------------------------------------ */
/* The editors work on the engine's memory state (the same SAV form as
 * the service bay, editors.h). REPAIR and CREW change it in place;
 * BUILD/ARMORY keep the SAV in their game struct, which mirrors the
 * engine's buffer: taken before every call the host makes, given back
 * after every handler (`changed`). */
static void build_in(es2_app *a) { a->build_game.save = a->h->io.engine.memory; }
static void build_out(es2_app *a) { a->h->io.engine.memory = a->build_game.save; }
static void editors_squad(void *ctx, uint32_t routine, int16_t x, int16_t y) {
    es2_app *a = ctx;
    switch (routine) {
    case ES2_REPAIR_SQUAD_GRID_HIDE:
        if (x >= 0 && x < 8) es2_ui_hide(&a->ui, a->bay.squad[ES2_SQUAD_GRID + x]);
        break;
    case ES2_REPAIR_SQUAD_GRID_SHOW:
        if (x >= 0 && x < 8) es2_ui_show(&a->ui, a->bay.squad[ES2_SQUAD_GRID + x]);
        break;
    case ES2_REPAIR_SQUAD_SELECT: es2_squad_panel_select(&a->bay, y, 0); break;
    case ES2_REPAIR_SQUAD_LABELS: es2_squad_panel_labels(&a->bay, x); break;
    case ES2_REPAIR_SQUAD_CONDITION: es2_squad_panel_hercs(&a->bay); break;
    case ES2_REPAIR_SQUAD_REFRESH:
        es2_squad_panel_hercs(&a->bay);
        es2_squad_panel_pilots(&a->bay);
        es2_squad_panel_labels(&a->bay, a->bay.session.bay);
        break;
    case ES2_REPAIR_SQUAD_REDRAW: es2_ui_redraw_tree(&a->ui, a->bay.squad[ES2_SQUAD_CONDITION]); break;
    case ES2_REPAIR_BUILD_REFRESH:
        build_in(a);
        es2_build_refresh(&a->build);
        build_out(a);
        break;
    case ES2_REPAIR_SQUAD_DIAGRAM: es2_squad_pictures_diagram(&a->bay); break;   /* 0x414cbe */
    default: break;
    }
}
static void editor_changed(void *ctx, uint32_t handler, es2_shell_flow_result result) {
    es2_app *a = ctx;
    logf_(a, "editor 0x%06x result %d\n", (unsigned)handler, (int)result);
}
static const char *weapon_name(void *ctx, int id) { es2_app *a = ctx; return catalogue(a, a->weapons_bin, (uint32_t)id); }
static const char *weapon_info(void *ctx, int index) { es2_app *a = ctx; return catalogue(a, a->wpn_info, (uint32_t)index); }
static void herc_scrap(void *ctx) { es2_herc_scrap_box_show(&((es2_app *)ctx)->repair); }
static void build_squad(void *ctx) {
    es2_app *a = ctx;
    /* The Build replaced the SAV (es2_shell_editors frees the old
     * buffer); the squad panel reads the engine's memory state, so give the
     * new state back before 0x43e0d7 runs (it read the freed old buffer:
     * the new Herc appeared only after the next interaction) */
    build_out(a);
    es2_squad_panel_hercs(&a->bay);    /* 0x43e0d7 */
    es2_squad_panel_pilots(&a->bay);   /* 0x43e167 */
    es2_squad_panel_labels(&a->bay, a->bay.session.bay);   /* 0x43da0c */
}
static void build_changed(void *ctx, uint32_t handler, int applied) {
    es2_app *a = ctx;
    build_out(a);
    logf_(a, "editor 0x%06x applied %d\n", (unsigned)handler, applied);
}
/* CREW's view of the team from the memory state (the editors' records). */
static void crew_pilot(es2_app *a, const es2_sav *sav, int index, es2_crew_pilot *p) {
    es2_sav_pilot r;
    char *name = a->texts[a->text_next++ % TEXTS];
    memset(p, 0, sizeof *p);
    p->bay = -1;
    if (index == 36) r = sav->player;
    else if (es2_sav_pilot_at(sav, (size_t)index, &r, NULL) != ES2_OK) return;
    size_t n = r.name.size < sizeof a->texts[0] - 1 ? r.name.size : sizeof a->texts[0] - 1;
    memcpy(name, r.name.data, n);
    name[n] = 0;
    p->name = name;
    p->portrait = (int16_t)es2_le16(r.prefix.data);
    if (r.suffix.size >= 7) {
        p->bay = (int16_t)es2_le16(r.suffix.data);
        p->skill = (int16_t)es2_le16(r.suffix.data + 3);
        p->order = (int16_t)es2_le16(r.suffix.data + 5);
    }
}
static int crew_team(void *ctx, es2_crew_team *t) {
    es2_app *a = ctx;
    es2_shell_engine *g = &a->h->io.engine;
    es2_sav sav;
    if (!g->memory.data || es2_sav_parse((es2_span){g->memory.data, g->memory.size}, &sav, NULL) != ES2_OK) return 0;
    t->capacity = (int16_t)es2_le16(sav.team.data);
    crew_pilot(a, &sav, 36, &t->player);
    for (int k = 0; k < 3; k++)
        crew_pilot(a, &sav, 12 * k + (int16_t)es2_le16(sav.roster_selection.data + 2 * k), &t->group[k]);
    for (int k = 0; k < 8; k++) t->vehicle[k] = -1;
    for (size_t i = 0; i < sav.loadout_count; i++) {
        es2_sav_loadout l;
        if (es2_sav_loadout_at(&sav, i, &l, NULL) == ES2_OK && l.index >= 0 && l.index < 8)
            t->vehicle[l.index] = (int16_t)es2_le16(l.fixed.data);
    }
    return 1;
}
static es2_status crew_edit(void *ctx, es2_shell_editor_session *session, es2_shell_editor_op *op) {
    es2_app *a = ctx;
    es2_shell_engine *g = &a->h->io.engine;
    es2_shell_buffer out = {NULL, 0};
    es2_status st;
    if (!g->memory.data || !g->tables) return ES2_ERR_ARGUMENT;
    st = es2_shell_editors(&a->h->io.vfs, (es2_span){g->tables->results, ES2_SHELL_RESULTS_TABLES_SIZE},
                           (es2_span){a->editor_tables, sizeof a->editor_tables}, (es2_span){g->memory.data, g->memory.size},
                           session, op, 1, &out, NULL, NULL);
    if (st == ES2_OK) {
        free(g->memory.data);
        g->memory = out;
    } else
        free(out.data);
    return st;
}
static void crew_changed(void *ctx, uint32_t handler) {
    logf_(ctx, "crew 0x%06x\n", (unsigned)handler);
}
/* The bar's screen show for an editor whose library OPEN would run the
 * leave 0x43a537 again: the flow's rebalance already ran for WEAPONS,
 * REPAIR and BUILD (vshell_ui.md section 31's double-leave note), so the
 * library opens from the bay screen -1, as REPAIR's show does for the same
 * case. */
static int16_t open_from(es2_app *a) {
    int16_t previous = a->bay.session.screen;
    if (previous == 2 || previous == 3 || previous == 4) a->bay.session.screen = ES2_SHELL_SCREEN_BAY;
    return previous;
}

/* ---- the mission screen ------------------------------------------------------------ */
static int16_t mission_group(void *ctx) {
    int16_t group, position;
    career(ctx, &group, &position);
    return group;
}
static char *copy_text(const uint8_t *data, size_t size) {
    char *t = malloc(size + 1);
    if (!t) return NULL;
    memcpy(t, data, size);
    t[size] = 0;
    return t;
}
static const char *mission_campaign(void *ctx, int16_t index) {
    es2_app *a = ctx;
    es2_span t;
    free(a->campaign_text);
    a->campaign_text = NULL;
    if (!a->campaign_str.data || es2_shell_campaign_text(a->campaign_str, (int16_t)(index + 1), &t, NULL) != ES2_OK) return NULL;
    return a->campaign_text = copy_text(t.data, t.size);
}
/* The page-1 texts 0x4135bb from DATA\MISSION.STR and the selection of the
 * memory state; the debrief text of the last results path (panel 4). */
static const char *mission_report(void *ctx, int panel) {
    es2_app *a = ctx;
    uint8_t selection[ES2_SHELL_BRIEFING_SIZE], *strings;
    size_t size;
    int16_t group;
    es2_shell_briefing_texts t;
    if (panel < 1 || panel > 4) return NULL;
    if (panel == 4) return a->report_text[4];
    if (!a->h->io.engine.memory.data ||
        es2_shell_briefing_selection((es2_span){a->h->io.engine.memory.data, a->h->io.engine.memory.size}, selection,
                                     &group, NULL, NULL) != ES2_OK)
        return NULL;
    strings = es2_host_read("DATA\\MISSION.STR", &size);
    memset(&t, 0, sizeof t);
    if (strings && es2_shell_briefing_texts_compose((es2_span){strings, size}, selection, &t, NULL) == ES2_OK) {
        static const int which[4] = {0, ES2_SHELL_TEXT_BRIEFING, ES2_SHELL_TEXT_OBJECTIVES, ES2_SHELL_TEXT_INTELLIGENCE};
        free(a->report_text[panel]);
        a->report_text[panel] = copy_text(t.text[which[panel]].data, t.text[which[panel]].size);
    }
    es2_shell_briefing_texts_free(&t);
    free(strings);
    return a->report_text[panel];
}
static void mission_words(void *ctx, int16_t *briefing, int16_t *debrief) {
    es2_app *a = ctx;
    uint8_t selection[ES2_SHELL_BRIEFING_SIZE];
    int16_t group;
    *briefing = -1;
    *debrief = a->h->debrief_selected ? a->h->debrief_word : -1;
    if (a->h->io.engine.memory.data &&
        es2_shell_briefing_selection((es2_span){a->h->io.engine.memory.data, a->h->io.engine.memory.size}, selection,
                                     &group, NULL, NULL) == ES2_OK)
        *briefing = (int16_t)es2_le16(selection + 146);   /* 0x48419c */
}

/* ---- the boxes ---------------------------------------------------------------------- */
static const es2_bitmap *box_bitmap(void *ctx, uint32_t global) {
    return global == 0x46de0c ? bitmap(ctx, "dbm\\bay2a_84.dbm", -1) : NULL;
}
/* Replay No's end (0x44d26e): the main menu 0x431718 whatever is on display;
 * after -X3/-X4 `shown` already names the main menu (es2_app_enter), so the
 * screen is shown again rather than left to sync. */
static void box_screen(void *ctx, es2_shell_flow *flow) {
    es2_app *a = ctx;
    if (flow->screen == ES2_FLOW_MAIN_MENU) a->shown = (es2_shell_flow_screen)-1;
    sync(a);
}
static void prefs_refresh(void *ctx) { es2_preferences_display_refresh(&((es2_app *)ctx)->prefs); }

/* ---- the results path's screen part (host.c's ui.results) ---------------------------------- */
static void results(void *ctx, const char *played, const es2_shell_results_output *out, es2_span results_dat) {
    es2_app *a = ctx;
    es2_shell_debrief_words w;
    es2_span outcome = {0}, unit = {0};
    logf_(a, "results %s salvage %lu\n", played, (unsigned long)out->total);
    free(a->report_text[4]);
    a->report_text[4] = out->text.size ? copy_text(out->text.data, out->text.size) : NULL;
    a->have_debrief = 0;
    if (results_dat.data && out->save.data &&
        es2_shell_debrief_words_after(results_dat, (es2_span){out->save.data, out->save.size}, &w, NULL) == ES2_OK) {
        if (a->estext.data) {
            es2m_bin_raw(a->estext, (size_t)(uint16_t)es2_shell_debrief_outcome_id(w.outcome), &outcome, NULL);
            es2m_bin_raw(a->estext, ES2_SHELL_DEBRIEF_UNIT, &unit, NULL);
        }
        a->have_debrief = es2_shell_debrief_texts_compose(&w, outcome, unit, &a->debrief, NULL) == ES2_OK;
    }
}

/* ---- the main loop's hooks ------------------------------------------------------------------ */
static void on_event(void *context, const es2_gui_event *ev) { es2_ui_on_event(&((es2_app *)context)->ui, ev); }
static void on_timer(void *context, es2_gui_shell *shell) {
    es2_app *a = context;
    es2_ui_on_timer(&a->ui, shell);
    es2_app_sound_service(a->sound);
}
/* After the dirty flush (main-loop step 5 has run: 0x46c280), the flow's
 * screen and box, then the movie player 0x41e98c(1) over the queue. */
static void on_movie(void *context, es2_gui_shell *shell) {
    es2_app *a = context;
    (void)shell;
    a->bay.loop_started = 1;
    sync(a);
    /* 0x4020b2: every pass; an empty ring clears the sequence flag 0x470fb0
     * (0x41e9fc), which stays 1 after a run until then, so the clicks queued
     * during its fades and drains reach no button */
    if (!a->shell->quit) play(a, 1);
}
/* Main-loop step 6 (the map, 0x4019c6) and step 10 (the Quit box repaint). */
static void on_map(void *context, es2_gui_shell *shell) {
    map_animation(context);
    es2_boxes_pass(&((es2_app *)context)->boxes, shell);
}
/* Alt+F4: the Quit box; leaving or toggling full screen: prefs byte 6 stored
 * and written (0x4077df, 0x407636); the display itself is the shell's. */
static void on_system(void *context, es2_gui_shell *shell, es2_gui_system_action action) {
    es2_app *a = context;
    if (action == ES2_GUI_SYS_QUIT_BOX) {
        es2_boxes_system(&a->boxes, shell, action);
        return;
    }
    if (action == ES2_GUI_SYS_LEAVE_FULLSCREEN || action == ES2_GUI_SYS_TOGGLE_FULLSCREEN) {
        uint8_t out[ES2_SHELL_PREFS_SIZE];
        size_t size;
        uint8_t *file = es2_host_read("DATA\\PREFS.CFG", &size);
        es2_span span = {file, size};
        if (es2_shell_prefs_store(&a->h->flow.prefs, file ? &span : NULL, 6, (uint8_t)(shell->fullscreen != 0), out, NULL) ==
            ES2_OK)
            es2_host_write("DATA\\PREFS.CFG", out, sizeof out);
        free(file);
    }
}
static void on_focus(void *context, es2_gui_shell *shell, int gained) {
    es2_app *a = context;
    (void)shell;
    es2_app_sound_focus(a->sound, gained);
}

/* ---- create, build, enter ------------------------------------------------------------------- */
static uint32_t ui_clock(void *context) {
    es2_app *a = context;
    return a->platform.clock ? a->platform.clock(a->platform.clock_context) : 0;
}

static void ui_say(void *ctx, const char *data, size_t size) {
    es2_app *a = ctx;
    if (a->process_ui.say) a->process_ui.say(a->process_ui.ctx, data, size);
}
static void ui_assertion(void *ctx, const char *text) {
    es2_app *a = ctx;
    if (a->process_ui.assertion) a->process_ui.assertion(a->process_ui.ctx, text);
    a->shell->quit = 1;
}

/* The wall-time record of the presents, off by default. With
 * ES2_SHELL_PRESENT_LOG set to a file path the shell's platform is wrapped:
 * every present is stamped with QueryPerformanceCounter on entry, before it
 * is forwarded and before the frame is hashed, and a record is written when
 * the frame's index or colour-table hash changed (the capture tour's every=0
 * rule), in the shape of its record version 2 (vshell_ui.md 42.5), so that
 * vshell_present_times.py reads it as it reads VSHELL's capture.jsonl; the
 * host's busy waits add a "wait" record (map_wait). Without the variable the
 * platform is untouched. */
static int log_pump(void *ctx, es2_gui_shell *shell) {
    es2_app *a = ctx;
    return a->present_inner.pump ? a->present_inner.pump(a->present_inner.context, shell) : 0;
}
static void log_present(void *ctx, es2_gui_shell *shell, es2_gui_present_kind kind, const es2_gui_rect *rects,
                        int count) {
    es2_app *a = ctx;
    uint64_t at = es2_os_counter();
    char index[65], table[65];
    if (a->present_inner.present) a->present_inner.present(a->present_inner.context, shell, kind, rects, count);
    a->presents_since++;
    es2_gui_hash_frame(&shell->frame, index);
    es2_gui_hash_table(&shell->frame, table);
    /* blits: one per rectangle, as VSHELL's BitBlt at 0x406e52 (the flush
     * 0x41fe49 presents rect by rect); the change counts at the first one */
    if (!strcmp(index, a->present_index) && !strcmp(table, a->present_table)) {
        a->blits_since += count > 0 && rects ? (unsigned)count : 1u;
        return;
    }
    unsigned blits = a->blits_since + 1;
    a->blits_since = count > 1 && rects ? (unsigned)count - 1 : 0;
    memcpy(a->present_index, index, sizeof index);
    memcpy(a->present_table, table, sizeof table);
    es2_gui_rect r = count > 0 && rects ? rects[0] : (es2_gui_rect){0, 0, ES2_GUI_WIDTH, ES2_GUI_HEIGHT};
    es2_os_fprintf(a->present_log,
            "{\"kind\":\"%s\",\"seq\":%u,\"step\":0,\"qpc\":%lld,\"present_kind\":%d,\"dst\":[%d,%d,%d,%d],"
            "\"rects\":%d,\"presents\":%u,\"blits\":%u,\"index_sha256\":\"%s\",\"palette_sha256\":\"%s\"}\n",
            kind == ES2_GUI_PRESENT_CLEAR ? "clear" : "present", ++a->present_seq, (long long)at, (int)kind,
            (int)r.left, (int)r.top, (int)r.right, (int)r.bottom, count, a->presents_since, blits, index, table);
    es2_os_fflush(a->present_log);
    a->presents_since = 0;
}
static void log_cursor(void *ctx, int busy) {
    es2_app *a = ctx;
    if (a->present_inner.cursor) a->present_inner.cursor(a->present_inner.context, busy);
}
static void log_fullscreen(void *ctx, int on) {
    es2_app *a = ctx;
    if (a->present_inner.fullscreen) a->present_inner.fullscreen(a->present_inner.context, on);
}
static void present_log_open(es2_app *a) {
    const char *path = es2_os_getenv("ES2_SHELL_PRESENT_LOG");
    uint64_t at;
    if (!path || !*path || !(a->present_log = es2_os_fopen(path, "wb"))) return;
    a->present_frequency = es2_os_counter_frequency();
    at = es2_os_counter();
    es2_os_fprintf(a->present_log,
            "{\"kind\":\"start\",\"seq\":0,\"step\":0,\"qpc\":%lld,\"schema\":2,\"source\":\"es2shell\",\"every\":0,"
            "\"headless\":%d,\"qpc_frequency\":%lld}\n",
            (long long)at, a->platform.headless, (long long)a->present_frequency);
    es2_os_fflush(a->present_log);
    a->present_inner = a->shell->platform;
    a->shell->platform = (es2_gui_platform){a, log_pump, log_present, log_cursor, log_fullscreen};
}

es2_app *es2_app_create(es2_host *host, es2_gui_shell *shell, const es2_app_platform *platform) {
    es2_app *a = calloc(1, sizeof *a);
    if (!a) return NULL;
    a->h = host;
    a->shell = shell;
    a->platform = *platform;
    a->shown = (es2_shell_flow_screen)-1;
    if (!(a->sound = es2_app_sound_create())) {
        free(a);
        return NULL;
    }
    /* the flow's host callbacks wrapped (load and new game clear the bay selection) */
    a->flow_host = host->flow.host;
    host->flow.host = (es2_shell_flow_host){a, wrap_store_mode, wrap_write_prefs, wrap_save, wrap_load, wrap_new_game,
                                            wrap_campaign, wrap_launch, wrap_rebalance, wrap_results, wrap_prepare_next,
                                            wrap_replay, wrap_manual, full_screen, wrap_debug_prepare};
    /* the host's presenter callbacks: the results path's screen part here,
     * the process's say and assertion forwarded */
    a->process_ui = host->ui;
    host->ui = (es2_host_ui){a, ui_say, results, ui_debug_choice, ui_assertion};
    present_log_open(a);
    return a;
}

void es2_app_destroy(es2_app *a) {
    if (!a) return;
    if (a->present_log) {   /* the shell's own platform back */
        a->shell->platform = a->present_inner;
        es2_os_fclose(a->present_log);
    }
    es2_mission_screen_release(&a->mission);
    if (a->map_built) es2_mission_map_free(&a->map);
    es2_ui_release(&a->ui);
    es2_app_sound_destroy(a->sound);
    free(a->sound_vol_bytes);
    for (int i = 0; i < a->bitmap_count; i++) free(a->bitmaps[i].pixels);
    for (int i = 0; i < 4; i++) free(a->vol_bytes[i]);
    for (int i = 0; i < 5; i++) free(a->report_text[i]);
    free(a->campaign_text);
    free(a->bay_before);
    free(a);
}

int es2_app_build(es2_app *a) {
    es2_host *h = a->h;
    es2_shell_flow *f = &h->flow;
    es2_gui_hooks hooks;
    if (!open_vfs(a)) return -1;
    {
        char name[32];
        /* Backslashes (VSHELL's archive lookup does not fold '/') */
        snprintf(name, sizeof name, "%s\\ESTEXT.BIN", h->args.language);
        a->estext = find(a, name);
        snprintf(name, sizeof name, "%s\\WPN_DESC.BIN", h->args.language);
        a->wpn_desc = find(a, name);
        snprintf(name, sizeof name, "%s\\CAMPAIGN.STR", h->args.language);
        a->campaign_str = find(a, name);
    }
    if (!font(a, 0x46de00)) { es2_host_say(h, "! dfn\\font2.dfn or dfn\\black.dfn could not be read\n"); return -1; }
    /* the practice tables and the CD's archives (es2_host_practice);
     * the executable's tables (literals, the practice tables, the band
     * tables) come from the span store; VER95\VSHELL.EXE is not opened */
    (void)es2_host_practice_open(h);
    { /* the bytes behind the keyboard's command table, read by hot keys with modifiers */
        es2_span s;
        if (es2_shell_exe_span(ES2_VSHELL_STORE, ES2_GUI_HOT_BYTES_BASE, ES2_GUI_HOT_BYTES_SIZE, &s,
                               NULL) == ES2_OK)
            (void)es2_gui_input_set_hot_bytes(&a->shell->input, s.data, s.size);
    }
    if (!eio_open(&h->io)) es2_host_engine_error(h);   /* the engine's tables (WEAPONS rows, launch) */
    snprintf(a->cd_root, sizeof a->cd_root, "%s%s", h->io.cd_root,
             h->io.cd_root[0] && h->io.cd_root[strlen(h->io.cd_root) - 1] != '\\' ? "\\" : "");

    if (es2_ui_init(&a->ui, a->shell)) return -1;
    a->ui.context = a;
    a->ui.palette = palette;
    a->ui.click_sound = click_sound;
    a->ui.sound = a->platform.sound;   /* [0x482356] */
    es2_ui_set_clock(&a->ui, ui_clock, a);

    /* the boxes 0x401812..0x40187b */
    es2_boxes_init(&a->boxes, &a->ui, f,
                   &(es2_boxes_host){a, font, box_bitmap, text_int, literal, box_screen, box_debug_default, box_debug_accept,
                                     prefs_refresh},
                   a->platform.cheat);
    /* the screens in the layout table's construction order */
    a->menu.flow = f;
    a->menu.movies = &h->movies;
    a->menu.host = (es2_main_menu_host){a, font, menu_bitmap, text, main_sound, notice, entry, menu_play, menu_changed};
    a->practice.flow = f;
    a->practice.practice = &h->practice.state;
    a->practice.tables = h->practice.tables;
    a->practice.host = (es2_practice_screen_host){a, font, menu_bitmap, text, literal, practice_launch, menu_changed};
    a->saved.flow = f;
    a->saved.movies = &h->movies;
    a->saved.table = &a->slots;
    a->saved.stats = a->stats;
    a->saved.host = (es2_saved_games_host){a, font, menu_bitmap, text, saved_refresh, saved_changed};
    es2_preferences_init(&a->prefs, f, NULL, &a->boxes,
                         &(es2_preferences_host){a, font, member_bitmap, text, prefs_music, menu_changed});
    a->bay.flow = f;
    a->bay.host = (es2_service_bay_host){a, font, member_bitmap, text, main_sound, bay_career, bay_screen, bay_view, NULL,
                                         bay_selected, repair_grid, menu_changed};
    {
        es2_span s;
        if (es2_shell_exe_span(ES2_VSHELL_STORE, 0x47665c, 10, &s, NULL) == ES2_OK)
            for (int i = 0; i < 5; i++) a->bay.band_limit[i] = (int16_t)es2_le16(s.data + 2 * i);
        if (es2_shell_exe_span(ES2_VSHELL_STORE, 0x476666, 12, &s, NULL) == ES2_OK)
            for (int i = 0; i < 6; i++) a->bay.band_colour[i] = (int16_t)es2_le16(s.data + 2 * i);
    }
    pictures_init(a);   /* the Herc pictures (the panel hook, the files) */
    a->registration.flow = f;
    a->registration.font_button = font(a, 0x46ddfc);
    a->registration.font_label = font(a, 0x46de00);
    a->registration.background = bitmap(a, "dbm\\bay2a_84.dbm", -1);
    a->registration.estext = text_int;
    a->registration.estext_context = a;
    a->weapons.engine = &h->io.engine;
    a->weapons.bay = &a->bay.session.bay;
    a->weapons.host = (es2_weapons_screen_host){a, font, member_bitmap, game_file, text, desc, literal, weapons_select,
                                                weapons_slot_shown, weapons_changed};
    /* The editors over the engine's memory state; the editor
     * tables from the executable (editors.h: 0x47665c 5 words, 0x46f868 9,
     * 0x46f87a 9, 0x476a4c 26) */
    {
        static const uint32_t parts[4][2] = {{0x47665c, 10}, {0x46f868, 18}, {0x46f87a, 18}, {0x476a4c, 52}};
        size_t at = 0;
        for (int i = 0; i < 4; i++) {
            es2_span part;
            if (es2_shell_exe_span(ES2_VSHELL_STORE, parts[i][0], parts[i][1], &part, NULL) != ES2_OK) break;
            memcpy(a->editor_tables + at, part.data, part.size);
            at += part.size;
        }
    }
    {
        char name[32];
        snprintf(name, sizeof name, "%s\\WEAPONS.BIN", h->args.language);
        a->weapons_bin = find(a, name);
        snprintf(name, sizeof name, "%s\\WPN_INFO.BIN", h->args.language);
        a->wpn_info = find(a, name);
    }
    es2_span results_tables = {h->io.tables.results, ES2_SHELL_RESULTS_TABLES_SIZE};
    es2_span editor_tables = {a->editor_tables, sizeof a->editor_tables};
    a->repair.flow = f;
    a->repair.session = &a->bay.session;
    a->repair.vfs = &h->io.vfs;
    a->repair.tables = results_tables;
    a->repair.editor_tables = editor_tables;
    a->repair.save = &h->io.engine.memory;
    a->repair.host = (es2_repair_screen_host){a, font, text, literal, editors_squad, editor_changed};
    a->build_game.vfs = &h->io.vfs;
    a->build_game.tables = results_tables;
    a->build_game.editor_tables = editor_tables;
    a->build_game.session = &a->bay.session;
    es2_build_armory_init(&a->build, f, &a->build_game,
                          &(es2_build_armory_host){a, font, member_bitmap, text, weapon_name, weapon_info, game_file,
                                                   herc_scrap, build_squad, build_changed});
    a->crew.bay = &a->bay;
    a->crew.host = (es2_crew_host){a, font, member_bitmap, text, crew_team, crew_edit, crew_changed};
    a->mission.flow = f;
    a->mission.movies = &h->movies;
    a->mission.bay = &a->bay;
    a->mission.host = (es2_mission_screen_host){a, font, member_bitmap, text, mission_group, mission_campaign,
                                                mission_report, mission_words, mission_map_arrow, mission_audio,
                                                menu_changed};
    /* every screen in the layout table's construction order (layout_table.h's
     * enum; the dead pilot-name screen 0x43b8f0 is not built) */
    for (int screen = 0; screen < ES2_UI_LAYOUT_SCREEN_COUNT; screen++) {
        int rc = 0;
        switch (screen) {
        case ES2_UI_SCREEN_MAIN_MENU: rc = es2_screen_main_menu_build(&a->ui, &a->menu); break;
        case ES2_UI_SCREEN_PRACTICE: rc = es2_screen_practice_build(&a->ui, &a->practice); break;
        case ES2_UI_SCREEN_SAVED_GAMES: rc = es2_screen_saved_games_build(&a->ui, &a->saved); break;
        case ES2_UI_SCREEN_PREFERENCES: rc = es2_screen_preferences_build(&a->ui, &a->prefs); break;
        case ES2_UI_SCREEN_DEBUG_DIALOG: rc = es2_screen_debug_dialog_build(&a->ui, &a->boxes); break;
        case ES2_UI_SCREEN_CD_PROMPT: rc = es2_screen_cd_prompt_build(&a->ui, &a->boxes); break;
        case ES2_UI_SCREEN_SERVICE_BAY: rc = es2_screen_service_bay_build(&a->ui, &a->bay); break;
        case ES2_UI_SCREEN_SQUAD_PANEL: rc = es2_screen_squad_panel_build(&a->ui, &a->bay); break;
        case ES2_UI_SCREEN_REPAIR: rc = es2_screen_repair_build(&a->ui, &a->repair); break;
        case ES2_UI_SCREEN_QUIT_BOX: rc = es2_screen_quit_box_build(&a->ui, &a->boxes); break;
        case ES2_UI_SCREEN_END_OF_GAME_BOX: rc = es2_screen_end_of_game_box_build(&a->ui, &a->boxes); break;
        case ES2_UI_SCREEN_LAUNCH_WARNING_BOX: rc = es2_screen_launch_warning_box_build(&a->ui, &a->boxes); break;
        case ES2_UI_SCREEN_REGISTRATION: rc = es2_screen_registration_build(&a->ui, &a->registration); break;
        case ES2_UI_SCREEN_REPLAY_BOX: rc = es2_screen_replay_box_build(&a->ui, &a->boxes); break;
        case ES2_UI_SCREEN_WEAPONS: rc = es2_screen_weapons_build(&a->ui, &a->weapons); break;
        case ES2_UI_SCREEN_BUILD: rc = es2_screen_build_build(&a->ui, &a->build); break;
        case ES2_UI_SCREEN_ARMORY: rc = es2_screen_armory_build(&a->ui, &a->build); break;
        case ES2_UI_SCREEN_CREW: rc = es2_screen_crew_build(&a->ui, &a->crew); break;
        case ES2_UI_SCREEN_MISSION: rc = es2_screen_mission_build(&a->ui, &a->mission); break;
        case ES2_UI_SCREEN_HERC_SCRAP_BOX: rc = es2_screen_herc_scrap_box_build(&a->ui, &a->repair); break;
        case ES2_UI_SCREEN_WEAPON_SCRAP_BOX: rc = es2_screen_weapon_scrap_box_build(&a->ui, &a->build); break;
        default: break;   /* ES2_UI_SCREEN_PILOT_NAME */
        }
        if (rc) {
            es2_host_say(h, "! screen %d could not be built\n", screen);
            return -1;
        }
    }
    es2_boxes_bind_preferences(&a->boxes);
    es2_ui_show(&a->ui, a->ui.root);
    memset(&hooks, 0, sizeof hooks);
    hooks.context = a;
    hooks.handle = on_event;
    hooks.timer = on_timer;
    hooks.movie = on_movie;
    hooks.map = on_map;
    hooks.system = on_system;
    hooks.focus = on_focus;
    a->shell->hooks = hooks;
    /* the movie player in VSHELL's main window */
    a->player.parent = a->platform.native_window;
    a->player.cd_root = a->cd_root;
    a->player.language = !strcmp(h->args.language, "FRE") ? 1 : !strcmp(h->args.language, "GER") ? 2 : 0;
    a->player.frame = movie_frame;
    a->player.context = a;
    h->movies.gate = &a->ui.gate;   /* one 0x470fb0 for the player and the buttons */
    read_slots(a);
    a->bay.session.bay = (int16_t)f->bay_select;
    a->bay_select_seen = f->bay_select;
    a->built = 1;
    return 0;
}

void es2_app_enter(es2_app *a, int state) {
    es2_host *h = a->h;
    /* the sounds' start-up 0x42f2f4 (-s, -v, -? clear the sound flag): the
     * music choice and prefs byte 5 flipped and written; then the samples on
     * the SDL device (none headless) and the sound focus */
    {
        uint8_t out[ES2_SHELL_PREFS_SIZE];
        es2_app_sound_set_log(a->sound, sound_log, a);
        if (es2_app_sound_startup(a->sound, a->platform.sound, a->platform.audio_device,
                                  a->platform.sound ? open_sound_vfs(a) : NULL, &h->flow.prefs, out))
            es2_host_write("DATA\\PREFS.CFG", out, sizeof out);
        if (a->shell->focused) es2_app_sound_focus(a->sound, 1);
    }
    a->shell->input.interactive = 1;   /* 0x46c28c once the first screen is up */
    logf_(a, "enter -X%d\n", state);
    if (es2_main_menu_enter(&a->menu, state) == ES2_FLOW_ENTRY_RESULTS) {
        es2_shell_flow *f = &h->flow;
        /* With -@ the DEBUG dialog waits (flow.debug); its answer opens it */
        if (f->outcome == ES2_FLOW_OUTCOME_NEXT && !f->quit && f->debug == ES2_FLOW_DEBUG_NONE) results_next(a);
        ramp(a, 1, f->prefs.current);   /* 0x4018bc: the fade-in after the results path 0x40f09f */
    }
    if (h->flow.screen == ES2_FLOW_MAIN_MENU && a->shown != ES2_FLOW_SERVICE_BAY) a->shown = ES2_FLOW_MAIN_MENU;
    sync(a);
}

/* The DEBUG dialog's Use Default ran the next preparation (the flow's
 * prepare_next, or with -@ its answer): the bay and the mission screen's
 * page 4 (0x43aee7(page, 1)), then the statistics 0x40f904. */
static void results_next(es2_app *a) {
    a->shown = ES2_FLOW_SERVICE_BAY;
    es2_service_bay_show(&a->bay);
    es2_mission_screen_open(&a->mission, a->h->flow.page, 1);
    if (a->have_debrief) es2_mission_screen_statistics(&a->mission, &a->debrief);
}

int es2_app_finish(es2_app *a) {
    es2_host *h = a->h;
    if (!h->flow.quit) h->flow.quit = 1;   /* the window was closed: the loop end */
    return h->assert_exit ? 1 : es2_shell_flow_finish(&h->flow);
}

/* ---- the headless script's view ------------------------------------------------------------- */
es2_ui *es2_app_ui(es2_app *a) { return &a->ui; }

static const char *const screen_names[ES2_UI_LAYOUT_SCREEN_COUNT] = {
    "main_menu", "practice", "saved_games", "preferences", "debug_dialog", "cd_prompt", "service_bay", "squad_panel",
    "repair", "quit_box", "end_of_game_box", "launch_warning_box", "pilot_name", "registration", "replay_box", "weapons",
    "build", "armory", "crew", "mission", "herc_scrap_box", "weapon_scrap_box"};

es2_ui_id es2_app_control(es2_app *a, const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot) return 0;
    for (int s = 0; s < ES2_UI_LAYOUT_SCREEN_COUNT; s++) {
        size_t n = strlen(screen_names[s]);
        if ((size_t)(dot - name) != n || strncmp(name, screen_names[s], n)) continue;
        const es2_ui_layout_screen *sc = es2_ui_layout_screen_of(s);
        int k = atoi(dot + 1);
        if (!sc || k < 0 || k >= sc->count) return 0;
        /* the screens keep their own id arrays; the toolkit records the
         * table's windows by control index */
        return a->ui.layout_ids[sc->first + k];
    }
    return 0;
}

void es2_app_describe(es2_app *a, char *out, size_t capacity) {
    es2_shell_flow *f = &a->h->flow;
    snprintf(out, capacity, "screen=%d shown=%d word=%d box=%d mode=%d game=%d final=%d page=%d quit=%d exit=%d bay=%d",
             (int)f->screen, (int)a->shown, f->bay_word, (int)f->box, f->mode, (int)f->game, f->final_word, f->page,
             f->quit, f->exit_code, a->bay.session.bay);
}
