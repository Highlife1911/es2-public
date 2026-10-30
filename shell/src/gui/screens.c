/* The layout table, the generic screen builder and the screen
 * registration table (docs/reverse-engineering/vshell_widgets.md, section
 * 4 and "widget toolkit"). */
#define ES2_UI_LAYOUT_DATA
#include "es2/shell/gui/layout.h"
#include "es2/shell/gui/boxes.h"
#include "es2/shell/gui/main_menu.h"
#include "es2/shell/gui/registration.h"
#include "es2/shell/gui/practice_screen.h"
#include "es2/shell/gui/saved_games.h"
#include "es2/shell/gui/preferences.h"
#include "es2/shell/gui/service_bay.h"
#include "es2/shell/gui/weapons.h"
#include "es2/shell/gui/repair.h"
#include "es2/shell/gui/build_armory.h"
#include "es2/shell/gui/crew.h"
#include "es2/shell/gui/mission.h"

#include <stddef.h>
#include <string.h>

_Static_assert(ES2_UI_LAYOUT_CONTROL_COUNT <= ES2_UI_LAYOUT_SLOTS, "layout_ids too small for the table");

/* ---- registration table ---------------------------------------------------
 * One line per screen, appended at the end of the list, in the form
 *     {ES2_UI_SCREEN_MAIN_MENU, es2_screen_main_menu_build},
 * with the build function declared in the screen's own header above it. */
const es2_ui_screen_entry es2_ui_screens[] = {
    {ES2_UI_SCREEN_DEBUG_DIALOG, es2_screen_debug_dialog_build},
    {ES2_UI_SCREEN_CD_PROMPT, es2_screen_cd_prompt_build},
    {ES2_UI_SCREEN_QUIT_BOX, es2_screen_quit_box_build},
    {ES2_UI_SCREEN_END_OF_GAME_BOX, es2_screen_end_of_game_box_build},
    {ES2_UI_SCREEN_LAUNCH_WARNING_BOX, es2_screen_launch_warning_box_build},
    {ES2_UI_SCREEN_REPLAY_BOX, es2_screen_replay_box_build},
    {ES2_UI_SCREEN_MAIN_MENU, es2_screen_main_menu_build},
    {ES2_UI_SCREEN_REGISTRATION, es2_screen_registration_build},
    {ES2_UI_SCREEN_PRACTICE, es2_screen_practice_build},
    {ES2_UI_SCREEN_SAVED_GAMES, es2_screen_saved_games_build},
    {ES2_UI_SCREEN_PREFERENCES, es2_screen_preferences_build},
    {ES2_UI_SCREEN_SERVICE_BAY, es2_screen_service_bay_build},
    {ES2_UI_SCREEN_SQUAD_PANEL, es2_screen_squad_panel_build},
    {ES2_UI_SCREEN_WEAPONS, es2_screen_weapons_build},
    {ES2_UI_SCREEN_REPAIR, es2_screen_repair_build},
    {ES2_UI_SCREEN_HERC_SCRAP_BOX, es2_screen_herc_scrap_box_build},
    {ES2_UI_SCREEN_BUILD, es2_screen_build_build},
    {ES2_UI_SCREEN_ARMORY, es2_screen_armory_build},
    {ES2_UI_SCREEN_WEAPON_SCRAP_BOX, es2_screen_weapon_scrap_box_build},
    {ES2_UI_SCREEN_CREW, es2_screen_crew_build},
    {ES2_UI_SCREEN_MISSION, es2_screen_mission_build},
    {-1, NULL}  /* end */
};

const es2_ui_screen_entry *es2_ui_screen_find(int screen) {
    for (const es2_ui_screen_entry *e = es2_ui_screens; e->build; e++)
        if (e->screen == screen) return e;
    return NULL;
}

/* ---- the table ------------------------------------------------------------- */

const es2_ui_layout_screen *es2_ui_layout_screen_of(int screen) {
    return screen >= 0 && screen < ES2_UI_LAYOUT_SCREEN_COUNT ? &layout_screens[screen] : NULL;
}
const es2_ui_layout_control *es2_ui_layout_control_of(int index) {
    return index >= 0 && index < ES2_UI_LAYOUT_CONTROL_COUNT ? &layout_controls[index] : NULL;
}
const es2_ui_layout_field *es2_ui_layout_field_of(int index) {
    return index >= 0 && index < ES2_UI_LAYOUT_FIELD_COUNT ? &layout_fields[index] : NULL;
}
const es2_ui_layout_method *es2_ui_layout_method_of(int index) {
    return index >= 0 && index < ES2_UI_LAYOUT_METHOD_COUNT ? &layout_methods[index] : NULL;
}
const es2_ui_layout_show_hide *es2_ui_layout_show_hide_of(int index) {
    return index >= 0 && index < ES2_UI_LAYOUT_SHOW_HIDE_COUNT ? &layout_show_hide[index] : NULL;
}
const char *es2_ui_layout_file(int index) {
    return index >= 0 && index < ES2_UI_LAYOUT_FILE_COUNT ? layout_files[index] : NULL;
}

/* ---- the builder --------------------------------------------------------- */

typedef struct build {
    es2_ui *ui;
    const es2_ui_resolver *r;
} build;

static const es2_dfn *font_of(const build *b, const es2_ui_layout_ref *ref) {
    return ref->kind == ES2_UI_REF_GLOBAL && b->r && b->r->font ? b->r->font(b->r->context, (uint32_t)ref->a) : NULL;
}
static const es2_bitmap *bitmap_of(const build *b, const es2_ui_layout_ref *ref) {
    return ref->kind != ES2_UI_REF_NONE && ref->kind != ES2_UI_REF_NUMBER && b->r && b->r->bitmap
               ? b->r->bitmap(b->r->context, ref)
               : NULL;
}
static const char *text_of(const build *b, const es2_ui_layout_ref *ref) {
    return ref->kind != ES2_UI_REF_NONE && ref->kind != ES2_UI_REF_NUMBER && b->r && b->r->text
               ? b->r->text(b->r->context, ref)
               : NULL;
}
static es2_ui_handler handler_of(const build *b, uint32_t address) {
    return address && b->r && b->r->handler ? b->r->handler(b->r->context, address) : NULL;
}

/* The ESDialog character sets by VSHELL address (gui/text.c's copies). */
static const char *edit_set(uint32_t address) {
    switch (address) {
    case 0x46ee48: return es2_shell_edit_set_capitals;
    case 0x475869:
    case 0x47a4e1: return es2_shell_edit_set_slots;
    case 0x4761c3:
    case 0x476205: return es2_shell_edit_set_names;
    default: return NULL;
    }
}

/* A field the screen constructor wrote after construction (layout table
 * `fields`), by VSHELL object offset and class. Returns 0, or -1 when the
 * class has no such field in the toolkit or the value is not a number. */
static int set_field(const build *b, es2_ui_id id, const es2_ui_layout_field *f) {
    es2_ui_widget *w = es2_ui_get(b->ui, id);
    const es2_ui_layout_ref *v = &f->value;
    int32_t n = v->a;
    int rect_family;
    if (!w) return -1;
    if (f->offset == 0x3d) { /* handler */
        if (v->kind != ES2_UI_REF_CODE && v->kind != ES2_UI_REF_NUMBER) return -1;
        w->original_handler = (uint32_t)v->a;
        w->handler = handler_of(b, (uint32_t)v->a);
        return 0;
    }
    if (f->offset == 0x5d && w->cls == ES2_UI_BITMAP) { /* the bitmap */
        w->bitmap = bitmap_of(b, v);
        return 0;
    }
    if (v->kind != ES2_UI_REF_NUMBER) return -1; /* run-time formula: the screen's */
    rect_family = w->cls == ES2_UI_RECT || w->cls == ES2_UI_REGION_FILL || w->cls == ES2_UI_TITLE ||
                  w->cls == ES2_UI_ALERT || w->cls == ES2_UI_BITMAP || w->cls == ES2_UI_GRID ||
                  w->cls == ES2_UI_ARM || w->cls == ES2_UI_QUAD || w->cls == ES2_UI_BUTTON_FONT;
    switch (f->offset) {
    case 0x49: w->enabled = n; return 0;
    case 0x4d: if (!rect_family) return -1; w->colour = n; return 0;
    case 0x51: if (!rect_family) return -1; w->frame = n; return 0;
    case 0x55:
        if (w->cls == ES2_UI_BITMAP) w->ox = n;
        else if (w->cls == ES2_UI_TITLE || w->cls == ES2_UI_ALERT || w->cls == ES2_UI_REGION_FILL ||
                 w->cls == ES2_UI_ARM) w->f55 = n;
        else return -1;
        return 0;
    case 0x59:
        if (w->cls == ES2_UI_BITMAP) w->oy = n;
        else if (w->cls == ES2_UI_TITLE || w->cls == ES2_UI_ALERT || w->cls == ES2_UI_ARM) w->f59 = n;
        else return -1;
        return 0;
    case 0x5d:
        if (w->cls == ES2_UI_BUTTON_BITMAP || w->cls == ES2_UI_RADIO) w->latch = n;
        else if (w->cls == ES2_UI_TITLE || w->cls == ES2_UI_ALERT || w->cls == ES2_UI_ARM) w->f5d = n;
        else return -1;
        return 0;
    case 0x61:
        if (w->cls == ES2_UI_BUTTON_BITMAP || w->cls == ES2_UI_RADIO) w->repeat = n;
        else if (w->cls == ES2_UI_TITLE || w->cls == ES2_UI_ALERT) w->f61 = n;
        else return -1;
        return 0;
    case 0x65: if (w->cls != ES2_UI_TITLE && w->cls != ES2_UI_ALERT) return -1; w->f65 = n; return 0;
    case 0x6d: if (w->cls != ES2_UI_TITLE && w->cls != ES2_UI_ALERT) return -1; w->f6d = n; return 0;
    case 0x71: if (w->cls != ES2_UI_TITLE && w->cls != ES2_UI_ALERT) return -1; w->f71 = n; return 0;
    case 0x6e5: if (w->cls != ES2_UI_GRID) return -1; w->grid_lines = (uint8_t)n; return 0;
    case 0x6e6: if (w->cls != ES2_UI_GRID) return -1; w->grid_colour = (int16_t)n; return 0;
    case 0x9f: if (w->cls != ES2_UI_DIALOG) return -1; w->edit.allowed = edit_set((uint32_t)n); return 0;
    case 0xb3: if (w->cls != ES2_UI_DIALOG) return -1; w->edit.blink = n; return 0;
    case 0xb7: if (w->cls != ES2_UI_DIALOG) return -1; w->edit.prefix = n; return 0;
    case 0xbb: if (w->cls != ES2_UI_DIALOG) return -1; w->edit.colour = (uint8_t)n; return 0;
    case 0xbf: if (w->cls != ES2_UI_DIALOG) return -1; w->editable = n; return 0;
    case 0xb5: if (w->cls != ES2_UI_MESSAGE) return -1; w->text_colour = n; return 0;
    case 0xc1: if (w->cls != ES2_UI_MESSAGE) return -1; w->opaque = n; return 0;
    case 0xc5: if (w->cls != ES2_UI_MESSAGE) return -1; w->background = n; return 0;
    default: return -1;
    }
}

static int resolved(const es2_ui_layout_control *c) {
    for (int i = 0; i < 4; i++)
        if (c->rect_kind[i] == ES2_UI_EDGE_FORMULA || c->rect_kind[i] == ES2_UI_EDGE_FILE) return 0;
    return 1;
}

static es2_ui_id construct(const build *b, const es2_ui_layout_control *c, es2_ui_id parent) {
    es2_ui *ui = b->ui;
    es2_gui_rect r = {c->rect[0], c->rect[1], c->rect[2], c->rect[3]};
    es2_ui_handler h = handler_of(b, c->handler);
    const es2_dfn *font = font_of(b, &c->ref[ES2_UI_ARG_FONT]);
    const char *text = text_of(b, &c->text);
    switch (c->cls) {
    case ES2_UI_ESWINDOW: return es2_ui_window(ui, parent, r, h);
    case ES2_UI_RECT: return es2_ui_rect(ui, parent, r, h, c->num[ES2_UI_ARG_COLOUR]);
    case ES2_UI_BUTTON_FONT: return es2_ui_button_font(ui, parent, r, font, h, text, c->num[ES2_UI_ARG_COLOUR], c->num[ES2_UI_ARG_ARG8]);
    case ES2_UI_BUTTON_BITMAP:
        return es2_ui_button_bitmap(ui, parent, r, font, bitmap_of(b, &c->ref[ES2_UI_ARG_BITMAP_NORMAL]), bitmap_of(b, &c->ref[ES2_UI_ARG_BITMAP_ACTIVE]),
                                    bitmap_of(b, &c->ref[ES2_UI_ARG_BITMAP_EXTRA]), h, text);
    case ES2_UI_RADIO:
        return es2_ui_radio(ui, parent, r, font, bitmap_of(b, &c->ref[ES2_UI_ARG_BITMAP_NORMAL]), bitmap_of(b, &c->ref[ES2_UI_ARG_BITMAP_ACTIVE]),
                            bitmap_of(b, &c->ref[ES2_UI_ARG_BITMAP_EXTRA]), h, text);
    case ES2_UI_REGION_FILL: return es2_ui_region_fill(ui, parent, r, h, c->num[ES2_UI_ARG_FILL], c->num[ES2_UI_ARG_COLOUR]);
    case ES2_UI_TITLE: return es2_ui_title(ui, parent, r, font, h, c->num[ES2_UI_ARG_COLOUR], text, c->num[ES2_UI_ARG_TITLE_HEIGHT]);
    case ES2_UI_ALERT: return es2_ui_alert(ui, parent, r, font, h, c->num[ES2_UI_ARG_COLOUR], text);
    case ES2_UI_MESSAGE: return es2_ui_message(ui, parent, r, font, h, text, c->num[ES2_UI_ARG_ALIGN], c->num[ES2_UI_ARG_COLOUR], c->num[ES2_UI_ARG_OPAQUE], c->num[ES2_UI_ARG_STYLE]);
    case ES2_UI_BITMAP: return es2_ui_bitmap(ui, parent, r, bitmap_of(b, &c->ref[ES2_UI_ARG_BITMAP]), h, c->num[ES2_UI_ARG_COLOUR]);
    case ES2_UI_GRID: return es2_ui_grid(ui, parent, r, h, c->num[ES2_UI_ARG_COLOUR]);
    case ES2_UI_DIALOG: return es2_ui_dialog(ui, parent, r, font, h);
    case ES2_UI_ARM: return es2_ui_arm(ui, parent, r, h, c->num[ES2_UI_ARG_COLOUR]);
    case ES2_UI_ANIM2: return es2_ui_anim2(ui, parent, r, h);
    case ES2_UI_PAL: return es2_ui_pal(ui, parent, r, h);
    case ES2_UI_QUAD: {
        for (int m = 0; m < c->method_count; m++) {
            const es2_ui_layout_method *k = &layout_methods[c->method_first + m];
            if (k->kind != ES2_UI_METHOD_QUAD_COLUMNS) continue;
            const char *t[4];
            int32_t x[3] = {k->num[4], k->num[5], k->num[6]}, a[4] = {k->num[0], k->num[1], k->num[2], k->num[3]};
            for (int i = 0; i < 4; i++) t[i] = text_of(b, &k->ref[1 + i]);
            return es2_ui_quad(ui, parent, r, h, c->num[ES2_UI_ARG_COLOUR], font_of(b, &k->ref[0]), t, x, a);
        }
        return es2_ui_quad(ui, parent, r, h, c->num[ES2_UI_ARG_COLOUR], NULL, NULL, NULL, NULL);
    }
    default: return 0;
    }
}

/* The windows an owner's constructor created, in the table's order. */
static es2_ui_id owned(const es2_ui *ui, es2_ui_id owner, int nth) {
    const es2_ui_widget *w = es2_ui_get((es2_ui *)ui, owner);
    if (!w) return 0;
    if (w->cls == ES2_UI_QUAD) return nth < 4 ? w->columns[nth] : 0;
    return nth == 0 ? w->label : 0;
}

int es2_ui_layout_build(es2_ui *ui, int screen, const es2_ui_resolver *resolver, es2_ui_id *ids, int capacity) {
    const es2_ui_layout_screen *s = es2_ui_layout_screen_of(screen);
    build b;
    int built = 0, owned_seen[ES2_UI_LAYOUT_CONTROL_COUNT];
    if (!ui || !s) return -1;
    b.ui = ui;
    b.r = resolver;
    memset(owned_seen, 0, sizeof owned_seen);
    for (int k = 0; k < s->count; k++) {
        int index = s->first + k;
        const es2_ui_layout_control *c = &layout_controls[index];
        es2_ui_id id = 0, parent;
        if (c->owner >= 0) {
            id = owned(ui, ui->layout_ids[c->owner], owned_seen[c->owner]++);
        } else if (c->per_record || !resolved(c)) {
            id = 0; /* the screen builds record lists and run-time rectangles itself */
        } else {
            parent = c->parent == ES2_UI_LAYOUT_ROOT      ? ui->root
                     : c->parent == ES2_UI_LAYOUT_DISPLAY ? ui->display
                                                          : ui->layout_ids[c->parent];
            if (parent) id = construct(&b, c, parent);
            if (id) es2_ui_get(ui, id)->original_handler = c->handler;
        }
        ui->layout_ids[index] = id;
        if (ids && k < capacity) ids[k] = id;
        if (!id) continue;
        if (c->owner < 0) built++;
        for (int m = 0; m < c->method_count; m++) {
            const es2_ui_layout_method *k2 = &layout_methods[c->method_first + m];
            if (k2->kind == ES2_UI_METHOD_QUAD_SET_COLOUR) es2_ui_quad_set_colour(ui, id, k2->num[0], k2->num[1]);
            else if (k2->kind == ES2_UI_METHOD_ANIM_ADD_FRAME) (void)es2_ui_anim_add_frame(ui, id, bitmap_of(&b, &k2->ref[0]));
        }
    }
    /* Fields and the constructor's own show/hide calls, after the windows
     * (the flags they leave equal the table's after_construction). */
    for (int k = 0; k < s->count; k++) {
        const es2_ui_layout_control *c = &layout_controls[s->first + k];
        es2_ui_id id = ui->layout_ids[s->first + k];
        if (!id) continue;
        for (int f = 0; f < c->field_count; f++) (void)set_field(&b, id, &layout_fields[c->field_first + f]);
    }
    /* The state after the screen constructor (the layout table's
     * after_construction): it includes the constructor's show/hide calls and
     * also the modal shows 0x40b7e9 inside the CD prompt (0x4323c9) and
     * Preferences (0x435739) constructors, which the table's screen_show_hide
     * does not list. With the root hidden (es2_ui_init) every control is hidden
     * or pending, so the flags are set without events. */
    for (int k = 0; k < s->count; k++) {
        const es2_ui_layout_control *c = &layout_controls[s->first + k];
        es2_ui_widget *w = es2_ui_get(ui, ui->layout_ids[s->first + k]);
        if (!w || c->after == 2) continue;
        w->flags = (w->flags & ~(ES2_UI_HIDDEN | ES2_UI_PENDING)) | ES2_UI_HIDDEN | (c->after ? ES2_UI_PENDING : 0u);
    }
    return built;
}
