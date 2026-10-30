#ifndef ES2_SHELL_GUI_SQUAD_PICTURES_H
#define ES2_SHELL_GUI_SQUAD_PICTURES_H

/* The squad panel's Herc pictures (the grid layers of the eight bay
 * panels [0x48d59c + 4k] and of REPAIR's panels [0x48d1f8 + 4k]), the Herc
 * diagram's condition colours and WEAPONS' slot marker, on the widget toolkit.
 *
 *   0x43cfa5(word)  the panel's load: 2, 4, 6 0x41547f (body, outline and
 *                   weapon layers of gam\arm_<herc>.dat); 3 0x4146cd (the
 *                   repair layers of gam\rpr_<herc>.dat, then 0x414cbe); 2 and
 *                   3 also 0x43c830 (hot spots, see below)
 *   0x43cfea(word)  its unload: 2, 4, 6 0x415a15; 3 0x414bd9 (every layer off)
 *   0x414cbe        the diagram: recolour targets by condition band
 *   0x415bff(slot)  the slot's weapon layer and the marker layer 12
 *   0x43df86        WEAPONS' bay change: layer 12 of the old bay's panel off
 *
 * The hot spots of gam\arm_hots.dat / gam\rpr_hots.dat (0x43c830) are ESRects
 * VSHELL constructs hidden and never shows (the window constructor sets the
 * hidden bit, 0x41f239; nothing shows them before 0x43ce7a deletes them), so
 * they never draw and never take a click: the files are read and the
 * rectangles kept (es2_squad_hot_spots), no window is built.
 *
 * Contract: docs/reverse-engineering/vshell_ui.md section 36; file formats:
 * docs/formats/herc_pictures.md. Parsers check every bound; no file I/O (the
 * host hands the files' bytes). */
#include <stdint.h>

#include "es2/formats/common.h"
#include "es2/shell/gui/widgets.h"

typedef struct es2_service_bay es2_service_bay;

#define ES2_HERC_TYPES 9
#define ES2_HERC_ITEMS 33          /* item IDs 0..32 index the slot lists */
#define ES2_HERC_RECORDS 400       /* per type and file (the CD's largest: 143) */
#define ES2_HERC_LAYERS 30         /* an ESGrid's layer count (0x40bdbb) */
#define ES2_SQUAD_HOT_SPOTS 32     /* per type and file (the CD's largest: 10) */

/* One picture record (0x1a bytes in memory). File records are 22 bytes in
 * gam\arm_<herc>.dat (word layer, dwords x, y, x2, y2, word frame, word
 * mirror) and 14 bytes in gam\rpr_<herc>.dat (no x2, y2). */
typedef struct es2_herc_picture {
    int16_t layer;    /* +0x00: grid layer; the key of a slot list (slot + 2, slot + 6) */
    int32_t x, y;     /* +0x02, +0x06 (slot lists of arm files: +1 each at load, 0x415335) */
    int32_t x2, y2;   /* +0x0a, +0x0e: the slot marker's point (arm files; slot lists +1 each) */
    int16_t frame;    /* +0x12: DBA frame (-1 in an arm slot list: no picture) */
    int32_t mirror;   /* +0x16: a word read into the dword's low half */
} es2_herc_picture;

/* One slot list: the records of an item ID (0 = the empty slot). */
typedef struct es2_herc_list {
    int16_t count;    /* 0x484702 / 0x484e30 + 0xc6 t: word [id] */
    uint16_t first;   /* index into records */
} es2_herc_list;

typedef struct es2_herc_type_pictures {
    /* gam\arm_<herc>.dat (0x41508f) */
    es2_herc_picture body[2];             /* 0x484444 / 0x48452e + 0x1a t: body and outline */
    es2_herc_list arm[ES2_HERC_ITEMS];    /* by item ID; a later list of an ID replaces the earlier */
    /* gam\rpr_<herc>.dat (0x414340) */
    int16_t layer_count;                  /* 0x484df8 */
    uint16_t layer_first;
    es2_herc_picture diagram;             /* 0x484618 + 0x1a t: the internal picture */
    es2_herc_list rpr[ES2_HERC_ITEMS];
    es2_herc_picture records[ES2_HERC_RECORDS];
    uint16_t used;
    int arm_loaded, rpr_loaded;
} es2_herc_type_pictures;

/* gam\arm_hots.dat / gam\rpr_hots.dat (0x43c830): word 9 (asserted), then per
 * type a word (read, not used), a word count (0x48d65c[i]) and count
 * rectangles of four dwords (0x4763c4[i]). */
typedef struct es2_squad_hots {
    int16_t count[ES2_HERC_TYPES];
    int32_t rect[ES2_HERC_TYPES][ES2_SQUAD_HOT_SPOTS][4];
    int loaded;
} es2_squad_hots;

/* What a panel load needs of a bay beyond es2_squad_bay's labels (the host's
 * view of the hangar record [0x482ba7 + 4k]). */
typedef struct es2_squad_record {
    int record;              /* a record is present */
    int16_t vehicle;         /* 0x411164: +0 (-1 without a record) */
    int16_t progress;        /* +0x4a */
    int16_t capacity;        /* +0x4c */
    int16_t item[10];        /* the item ID of slot pointer +0x50 + 4s, 0 for a null pointer */
    int16_t words[33];       /* +0x08: the condition words */
} es2_squad_record;

typedef struct es2_squad_pictures_host {
    void *ctx;
    /* A file's bytes (gam\arm_*.dat, gam\rpr_*.dat, gam\*_hots.dat); an empty span: absent. */
    es2_span (*file)(void *ctx, const char *name);
    /* Frame `frame` of a DBA (dba\mt_3qtr.dba, dba\*_bod/_wep/_out.dba, dba\rpr_*.dba, dba\*_int.dba). */
    const es2_bitmap *(*frame)(void *ctx, const char *file, int frame);
    /* The record of bay k (0 = no record). */
    int (*record)(void *ctx, int k, es2_squad_record *out);
    /* The REPAIR load's data error (0x414a3c: a mounted item without a slot
     * list record in gam\rpr_<herc>.dat; VSHELL's severity-4 assertion at
     * src\hercdisp.cpp line 233 ends the process). The text is VSHELL's
     * message ("Data Error: Herc: %s   Socket: %d \tWeapon: %s"). May be NULL. */
    void (*data_error)(void *ctx, const char *text);
    /* An ESTEXT entry (0x6e + vehicle, 0x7e + ID for the data error's text). */
    const char *(*text)(void *ctx, uint32_t id);
} es2_squad_pictures_host;

typedef struct es2_squad_pictures {
    es2_squad_pictures_host host;
    es2_herc_type_pictures type[ES2_HERC_TYPES];
    es2_squad_hots arm_hots, rpr_hots;
    /* executable tables (the host reads them from VSHELL.EXE): the part
     * groups 0x46f9e4 (24 words) and the internal diagram's recolour sources
     * 0x46ffc0 (9 words) */
    int16_t groups[24];
    int16_t internal_from[9];
    int loaded;              /* es2_squad_pictures_load ran */
    unsigned refused;        /* reads VSHELL makes outside its tables, skipped here */
    unsigned data_errors;    /* 0x414a3c's assertion reached */
} es2_squad_pictures;

/* ---- parsers (0 = parsed, -1 = refused: truncated, a count below 0, an item
 * ID outside 0..32, a list longer than the pools) ------------------------- */
int es2_herc_arm_parse(es2_span file, es2_herc_type_pictures *t);
int es2_herc_rpr_parse(es2_span file, es2_herc_type_pictures *t);
int es2_squad_hots_parse(es2_span file, es2_squad_hots *h);
/* 0x4142f0(table, id, key): the first record of item `id`'s list whose layer
 * word is `key`; rpr = 0 the arm lists, 1 the rpr lists. NULL if none. */
const es2_herc_picture *es2_herc_lookup(const es2_herc_type_pictures *t, int rpr, int16_t id, int16_t key);

/* Reads the 9 + 9 + 2 files through the host (VSHELL's start-up readers
 * 0x41508f, 0x414340 and, at each load, 0x43c830). Returns the number of
 * files that did not parse. */
int es2_squad_pictures_load(es2_squad_pictures *p);

/* ---- on the squad panel (b->pictures must be set) ------------------------- */
/* 0x43cfa5 (load = 1) / 0x43cfea (0), the picture parts. */
void es2_squad_pictures_panel(es2_service_bay *b, int word, int load);
/* 0x414cbe: the selected bay's diagram colours by REPAIR's tab
 * (b->session.tab), then the panel's draw. */
void es2_squad_pictures_diagram(es2_service_bay *b);
/* 0x415bff(slot), then 0x41f3ed of the selected bay's panel (WEAPONS
 * 0x43e2b6 and 0x43e32a redraw it after the call). */
void es2_squad_pictures_marker(es2_service_bay *b, int16_t slot);
/* 0x43dfc8: 0x40bdf4(panel of the selection, 12) in WEAPONS' bay change. */
void es2_squad_pictures_marker_off(es2_service_bay *b);
/* The hot spots 0x43c830 would construct for bay k on screen `word` (2, 3):
 * rectangles relative to the bay's panel and VSHELL handler addresses.
 * Returns the count (at most max); they are never shown (see above). */
typedef struct es2_squad_hot_spot {
    int32_t rect[4];
    uint32_t handler;
} es2_squad_hot_spot;
int es2_squad_hot_spots(const es2_service_bay *b, int word, int k, es2_squad_hot_spot *out, int max);

#endif
