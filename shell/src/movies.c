/* Movie queue, selection rules and player control flow, written from
 * the contract in docs/reverse-engineering/vshell_ui.md section 19; see
 * es2/shell/movies.h. */
#include "es2/shell/movies.h"

#include <stdio.h>
#include <string.h>

/* Original: VSHELL.EXE pointer table 0x470fb4 (86 entries, names stored back
 * to back at 0x471136..0x471568). File names of the CD's AVI directory. */
static const char *const names[ES2_SHELL_MOVIE_NAMES] = {
    "avi\\pt1.avi", "avi\\pt2.avi", "avi\\pt3.avi", "avi\\pt4.avi", "avi\\pt5.avi", "avi\\pt6.avi",
    "avi\\rc1.avi", "avi\\rc2.avi", "avi\\rc3.avi", "avi\\rc4.avi", "avi\\rc5.avi",
    "avi\\as1.avi", "avi\\as2.avi", "avi\\as3.avi", "avi\\as4.avi", "avi\\as5.avi", "avi\\as6.avi", "avi\\as7.avi",
    "avi\\es1.avi", "avi\\es2.avi", "avi\\es3.avi", "avi\\es4.avi",
    "avi\\rs1.avi", "avi\\rs2.avi", "avi\\rs3.avi", "avi\\rs4.avi",
    "avi\\sc1.avi", "avi\\sc2.avi", "avi\\sc3.avi", "avi\\sc4.avi", "avi\\sc5.avi",
    "avi\\rd1.avi", "avi\\rd2.avi", "avi\\rd3.avi", "avi\\rd4.avi",
    "avi\\sp1.avi",
    "avi\\gd1.avi", "avi\\gd2.avi", "avi\\gd3.avi", "avi\\gd4.avi",
    "avi\\ex1.avi", "avi\\ex2.avi", "avi\\ex3.avi", "avi\\ex4.avi",
    "avi\\rf1.avi", "avi\\rf2.avi", "avi\\rf3.avi", "avi\\rf4.avi",
    "avi\\co1.avi", "avi\\co2.avi", "avi\\co3.avi", "avi\\co4.avi",
    "avi\\fl1.avi", "avi\\fl2.avi", "avi\\fl3.avi", "avi\\fl4.avi",
    "avi\\sk1.avi", "avi\\sk2.avi", "avi\\sk3.avi", "avi\\sk4.avi",
    "avi\\sv1.avi", "avi\\sv2.avi", "avi\\sv3.avi", "avi\\sv4.avi",
    "avi\\hc1.avi", "avi\\hc2.avi", "avi\\hc3.avi", "avi\\hc4.avi",
    "avi\\intr_pt1.avi", "avi\\intr_pt2.avi",
    "avi\\c1.avi", "avi\\c2.avi", "avi\\c3.avi", "avi\\c4.avi", "avi\\c5.avi",
    "avi\\alph_th.avi", "avi\\delt_th.avi", "avi\\omic_th.avi", "avi\\brav_th.avi", "avi\\luna.avi",
    "avi\\transm3.avi", "avi\\end1a.avi", "avi\\death.avi", "avi\\victory.avi", "avi\\credits.avi",
    "avi\\dropship.avi"};

const es2_shell_movie_rect es2_shell_movie_rect_full = {0x20, 0x3c, 0x240, 0x168};
const es2_shell_movie_rect es2_shell_movie_rect_panel = {0x15, 0x56, 0xef, 0xb3};
const es2_shell_movie_rect es2_shell_movie_rect_theatre = {0x122, 0x43, 0x127, 0xe2};

const char es2_shell_mci_open_device[] = "open avivideo";
const char es2_shell_mci_window_handle[] = "status mov window handle";
const char es2_shell_mci_play[] = "play mov window notify";
const char es2_shell_mci_close[] = "close mov";
const char es2_shell_mci_close_device[] = "close avivideo";

const char *es2_shell_movie_name(int index) {
    return index >= 0 && index < ES2_SHELL_MOVIE_NAMES ? names[index] : NULL;
}

int es2_shell_movie_path(int index, int language, char *out, size_t capacity) {
    const char *name = es2_shell_movie_name(index);
    size_t n;
    if (!name || !out) return -1;
    n = strlen(name);
    if (n >= capacity) return -1;
    memcpy(out, name, n + 1);
    /* Original: 0x41e9ac..0x41e9d1, al = 'i', [0x48235e] 1 -> 'f', 2 -> 'g',
     * byte +2 of the strings [0x4710c4] (intr_pt1) and [0x4710c8] (intr_pt2) */
    if (index == ES2_SHELL_MOVIE_INTRO_1 || index == ES2_SHELL_MOVIE_INTRO_2)
        out[2] = language == 1 ? 'f' : language == 2 ? 'g' : 'i';
    return 0;
}

/* ---- Queue ---------------------------------------------------------------- */
static void empty_record(es2_shell_movie_record *r, es2_shell_movie_rect empty) {
    /* Original: 0x41e848 (constructor) and 0x41f02c..0x41f0aa (after a movie) */
    r->index = -1;
    r->rect = empty;
    r->palette = -1;
    r->sequence = 0;
    r->callback = 0;
}

void es2_shell_movies_init(es2_shell_movies *m, int enabled) {
    memset(m, 0, sizeof *m);
    /* Original: 0x41f11e, the rectangle 0x47110c from (0,0)-(0,0); 0x41e848 per record */
    for (int i = 0; i < ES2_SHELL_MOVIE_RING; i++) empty_record(&m->ring[i], m->empty);
    m->enabled = enabled ? 1 : 0;
}

int es2_shell_movie_queued(const es2_shell_movies *m, int32_t index) {
    /* Original: 0x41e88f, 16-bit compare with every record +0 */
    for (int i = 0; i < ES2_SHELL_MOVIE_RING; i++)
        if (m->ring[i].index == (int16_t)(uint16_t)index) return 1;
    return 0;
}

static int16_t ring_next(int16_t i) {
    /* inc word, movsx, cdq, idiv 10, remainder */
    int32_t v = (int16_t)(uint16_t)(i + 1);
    return (int16_t)(v % ES2_SHELL_MOVIE_RING);
}

int es2_shell_movie_enqueue(es2_shell_movies *m, int32_t index, es2_shell_movie_rect rect, int32_t palette,
                            uint32_t callback, uint32_t sequence) {
    es2_shell_movie_record *r;
    /* Original: 0x41e8c7 movie flag; 0x41e8d5 already queued */
    if (!m->enabled || es2_shell_movie_queued(m, index)) return 0;
    /* 0x41e8f1..0x41e965: the record at the write index (signed word), no
     * full-ring check */
    if (m->write < 0 || m->write >= ES2_SHELL_MOVIE_RING) return 0;   /* outside the ring: unreachable */
    r = &m->ring[m->write];
    r->index = (int16_t)(uint16_t)index;
    r->rect = rect;
    r->palette = (int16_t)(uint16_t)palette;
    r->sequence = sequence;       /* [ebp+0x24] -> +0x14 */
    r->callback = callback;       /* [ebp+0x20] -> +0x18 */
    m->write = ring_next(m->write);
    return 1;
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

void es2_shell_movies_image(const es2_shell_movies *m, uint8_t out[ES2_SHELL_MOVIE_RING * ES2_SHELL_MOVIE_RECORD_SIZE]) {
    for (int i = 0; i < ES2_SHELL_MOVIE_RING; i++) {
        const es2_shell_movie_record *r = &m->ring[i];
        uint8_t *p = out + i * ES2_SHELL_MOVIE_RECORD_SIZE;
        p[0] = (uint8_t)r->index; p[1] = (uint8_t)((uint16_t)r->index >> 8);
        put32(p + 2, (uint32_t)r->rect.x);
        put32(p + 6, (uint32_t)r->rect.y);
        put32(p + 10, (uint32_t)r->rect.width);
        put32(p + 14, (uint32_t)r->rect.height);
        p[18] = (uint8_t)r->palette; p[19] = (uint8_t)((uint16_t)r->palette >> 8);
        put32(p + 20, r->sequence);
        put32(p + 24, r->callback);
    }
}

/* ---- Selection rules -------------------------------------------------------- */
int es2_shell_movies_intro(es2_shell_movies *m, int16_t entry_mode) {
    /* Original: 0x40188a..0x40189f: 3, 4 results path; 6 main menu; else 0x4018fd */
    if (entry_mode == 3 || entry_mode == 4 || entry_mode == 6) return 0;
    es2_shell_movie_enqueue(m, ES2_SHELL_MOVIE_INTRO_1, es2_shell_movie_rect_full, -1, 0, 0);
    es2_shell_movie_enqueue(m, ES2_SHELL_MOVIE_INTRO_2, es2_shell_movie_rect_full, -1, 0, 0);
    return 1;
}

void es2_shell_movies_credits(es2_shell_movies *m) {
    /* Original: 0x431d18 */
    es2_shell_movie_enqueue(m, ES2_SHELL_MOVIE_CREDITS, es2_shell_movie_rect_full, -1, 0, 0);
}

void es2_shell_movies_victory(es2_shell_movies *m) {
    /* Original: 0x40f58d, 0x40f5bb */
    es2_shell_movie_enqueue(m, ES2_SHELL_MOVIE_VICTORY, es2_shell_movie_rect_full, -1, 0, 0);
    es2_shell_movie_enqueue(m, ES2_SHELL_MOVIE_CREDITS, es2_shell_movie_rect_full, -1, 0, 0);
}

int es2_shell_movies_mission_page(const es2_shell_movies *m, int16_t position) {
    /* Original: 0x43ae49..0x43ae70 */
    return !m->group_shown && position == 0 ? 0 : 1;
}

void es2_shell_movies_mission(es2_shell_movies *m, int page, int16_t group, int16_t briefing_word,
                              int16_t debrief_word) {
    int16_t si = (int16_t)(uint16_t)(group - 1);   /* 0x444a12: si = [0x46fc58] - 1 */
    if (page == 0) {
        /* 0x444a69..0x444add */
        if (m->group_shown) return;
        es2_shell_movie_enqueue(m, (uint16_t)(si + ES2_SHELL_MOVIE_GROUP), es2_shell_movie_rect_panel,
                                si >= 4 ? 4 : 3, 0, 0);
        es2_shell_movie_enqueue(m, (uint16_t)(si + ES2_SHELL_MOVIE_THEATRE), es2_shell_movie_rect_theatre, -1, 0, 1);
        m->group_shown = 1;
    } else if (page == 1) {
        /* 0x444bb2 palette word si + 5 ([0x46c26a]); 0x444cd6..0x444d18 */
        if (m->briefing_shown) return;
        es2_shell_movie_enqueue(m, briefing_word, es2_shell_movie_rect_panel, (uint16_t)(si + 5), 0, 0);
        m->briefing_shown = 1;
    } else if (page == 4) {
        /* 0x444d40 palette word si + 10; 0x444dc8..0x444e0a */
        if (m->debrief_shown) return;
        es2_shell_movie_enqueue(m, debrief_word, es2_shell_movie_rect_panel, (uint16_t)(si + 10), 0, 0);
        m->debrief_shown = 1;
    }
}

int es2_shell_movies_theatre(es2_shell_movies *m, int16_t group) {
    /* Original: 0x44473f bx = [0x46fc58] - 1; 0x44474f cmp bx, 4 */
    if ((int16_t)(uint16_t)(group - 1) != 4) return 0;
    es2_shell_movie_enqueue(m, ES2_SHELL_MOVIE_DROPSHIP, es2_shell_movie_rect_full, -1, 0, 0);
    return 1;
}

void es2_shell_movies_new_career(es2_shell_movies *m) {
    m->group_shown = 0;      /* 0x43c7a5 */
    m->briefing_shown = 0;   /* 0x40e8b1 */
    m->debrief_shown = 0;    /* 0x40e8b8 */
}

void es2_shell_movies_load(es2_shell_movies *m, int restore) {
    if (restore) m->group_shown = 0;   /* 0x4383c6 */
    m->briefing_shown = 0;             /* 0x40ec06 */
    m->debrief_shown = 0;              /* 0x40ec0d */
}

/* ---- Player ---------------------------------------------------------------------- */
/* 0x470fb0 and the toolkit's view of it */
static void set_active(es2_shell_movies *m, uint32_t v) {
    m->active = v;
    if (m->gate) *m->gate = (int)v;
}

static int32_t act(const es2_shell_movie_hooks *h, uint32_t action, int32_t arg, const es2_shell_movie_record *r) {
    return h->call ? h->call(h->context, action, arg, r) : 0;
}
static int32_t ask(const es2_shell_movie_hooks *h, int what) {
    return h->query ? h->query(h->context, what) : 0;
}

int es2_shell_movies_play(es2_shell_movies *m, int build_palette, const es2_shell_movie_hooks *h) {
    int theatre_ran = 0;   /* esi */
    /* Original: 0x41e995 movie flag; 0x41e9de empty record at the play index */
    if (!m->enabled) return 0;
    if (m->play < 0 || m->play >= ES2_SHELL_MOVIE_RING) return 0;   /* unreachable */
    if (m->ring[m->play].index == -1) {
        set_active(m, 0);                            /* 0x41e9fc */
        return 0;
    }
    set_active(m, 1);                                /* 0x41ea06 */
    act(h, ES2_SHELL_MOVIE_FADE_OUT, 0, NULL);       /* 0x41ea21 */
    act(h, ES2_SHELL_MOVIE_FOCUS_LOST, 0, NULL);     /* 0x41ea37 */
    while (m->ring[m->play].index != -1) {           /* 0x41f0eb..0x41f105 */
        es2_shell_movie_record *r = &m->ring[m->play];
        int credits, opened = 0;
        if (r->index == ES2_SHELL_MOVIE_DROPSHIP) theatre_ran = 0;    /* 0x41ea6a */
        m->intro = 0;                                                  /* 0x41ea74 ([0x47111c] := 0 too) */
        if (r->index == ES2_SHELL_MOVIE_INTRO_1 || r->index == ES2_SHELL_MOVIE_INTRO_2) m->intro = 1;
        credits = r->index == ES2_SHELL_MOVIE_CREDITS;                 /* edi */
        /* 0x41eaf7..0x41eb3d */
        if (r->palette != -1 && !m->intro && !credits) {
            act(h, ES2_SHELL_MOVIE_PALETTE, r->palette, NULL);
            act(h, ES2_SHELL_MOVIE_PALETTE_APPLY, 0, NULL);
        }
        /* 0x41eb53..0x41ecc8: the bay bar is visible (not for the dropship) */
        if (r->index != ES2_SHELL_MOVIE_DROPSHIP && ask(h, ES2_SHELL_MOVIE_Q_BAY_BAR)) {
            int page = (int16_t)ask(h, ES2_SHELL_MOVIE_Q_PAGE);
            act(h, ES2_SHELL_MOVIE_BAY_BUTTONS, 0, NULL);
            if (page == 1) act(h, ES2_SHELL_MOVIE_SHOW, ES2_SHELL_MOVIE_OBJ_TREES, NULL);
            act(h, ES2_SHELL_MOVIE_SHOW, ES2_SHELL_MOVIE_OBJ_MAP, NULL);
            act(h, ES2_SHELL_MOVIE_SHOW, ES2_SHELL_MOVIE_OBJ_TITLE, NULL);
            act(h, ES2_SHELL_MOVIE_SHOW, ES2_SHELL_MOVIE_OBJ_PAGE, NULL);
            act(h, ES2_SHELL_MOVIE_DRAIN, 0, NULL);
            page = (int16_t)ask(h, ES2_SHELL_MOVIE_Q_PAGE);
            if (page == 1) act(h, ES2_SHELL_MOVIE_REPAINT, ES2_SHELL_MOVIE_OBJ_TREES, NULL);
            act(h, ES2_SHELL_MOVIE_REPAINT, ES2_SHELL_MOVIE_OBJ_MAP, NULL);
            act(h, ES2_SHELL_MOVIE_REPAINT, ES2_SHELL_MOVIE_OBJ_TITLE, NULL);
            act(h, ES2_SHELL_MOVIE_TEXT_PANEL, (int16_t)ask(h, ES2_SHELL_MOVIE_Q_PAGE), NULL);
            act(h, ES2_SHELL_MOVIE_DRAIN, 0, NULL);
        }
        act(h, ES2_SHELL_MOVIE_BUSY, 1, NULL);                         /* 0x41ece0 */
        /* 0x41ecf6..0x41ed2e: a skipped intro opens nothing; a device that
         * does not open skips the movie */
        if (!(m->intro && m->intro_skipped) && act(h, ES2_SHELL_MOVIE_OPEN_DEVICE, build_palette ? 1 : 0, NULL)) {
            /* 0x41ee1a..0x41ee2b: until the movie opened or the quit flag */
            while (!opened && !ask(h, ES2_SHELL_MOVIE_Q_QUIT)) {
                int32_t result = act(h, ES2_SHELL_MOVIE_PLAY, 0, r);
                if (result != ES2_SHELL_MOVIE_FAILED) {
                    opened = 1;
                    /* 0x4050e0/0x405145, 0x41e7d8: a skip during an intro movie */
                    if (result == ES2_SHELL_MOVIE_SKIPPED && m->intro) m->intro_skipped = 1;
                } else if (m->intro) {
                    /* 0x41ed88..0x41edc2: error.str 5, sound off, quit flag */
                    act(h, ES2_SHELL_MOVIE_INTRO_ERROR, 0, r);
                    return 1;
                } else {
                    /* 0x41edda..0x41ee10: the CD prompt until Retry or quit */
                    int retry;
                    set_active(m, 0);
                    retry = act(h, ES2_SHELL_MOVIE_CD_PROMPT, 0, r);
                    set_active(m, 1);   /* 0x41ee10, after Retry and after quit */
                    if (!retry) break;
                }
            }
            if (ask(h, ES2_SHELL_MOVIE_Q_QUIT) || !opened) return 1;  /* 0x41ee31 */
            act(h, ES2_SHELL_MOVIE_CLOSE_DEVICE, 0, NULL);              /* 0x41ee3f */
            if (ask(h, ES2_SHELL_MOVIE_Q_FULLSCREEN) && (m->intro || credits))
                act(h, ES2_SHELL_MOVIE_CLEAR, 0, NULL);                 /* 0x41ee5b */
        }
        act(h, ES2_SHELL_MOVIE_BUSY, 0, NULL);                         /* 0x41ee62 */
        act(h, ES2_SHELL_MOVIE_PUMP, 0, NULL);                         /* 0x41ee67 */
        while (!ask(h, ES2_SHELL_MOVIE_Q_FOCUS)) act(h, ES2_SHELL_MOVIE_PUMP, 0, NULL);
        /* 0x41ee9b..0x41ef37 */
        if (ask(h, ES2_SHELL_MOVIE_Q_BAY_BAR)) {
            if ((int16_t)ask(h, ES2_SHELL_MOVIE_Q_PAGE) == 1) act(h, ES2_SHELL_MOVIE_REPAINT, ES2_SHELL_MOVIE_OBJ_TREES, NULL);
            act(h, ES2_SHELL_MOVIE_REPAINT, ES2_SHELL_MOVIE_OBJ_MAP, NULL);
            act(h, ES2_SHELL_MOVIE_REPAINT, ES2_SHELL_MOVIE_OBJ_TITLE, NULL);
            act(h, ES2_SHELL_MOVIE_TEXT_PANEL, (int16_t)ask(h, ES2_SHELL_MOVIE_Q_PAGE), NULL);
            if (r->index == ES2_SHELL_MOVIE_DROPSHIP) {
                act(h, ES2_SHELL_MOVIE_BAY_SELECT, 1, NULL);
                act(h, ES2_SHELL_MOVIE_REPAINT, ES2_SHELL_MOVIE_OBJ_ROOT, NULL);
            }
        } else if (!ask(h, ES2_SHELL_MOVIE_Q_FULLSCREEN)) {
            act(h, ES2_SHELL_MOVIE_HIDE, ES2_SHELL_MOVIE_OBJ_ROOT, NULL);
            act(h, ES2_SHELL_MOVIE_SHOW, ES2_SHELL_MOVIE_OBJ_ROOT, NULL);
        }
        if (r->index == ES2_SHELL_MOVIE_CREDITS) act(h, ES2_SHELL_MOVIE_PALETTE, 1, NULL);   /* 0x41ef54 */
        if (r->sequence) {                                              /* 0x41ef6a..0x41efaa */
            act(h, ES2_SHELL_MOVIE_TEARDOWN, 0, NULL);
            act(h, ES2_SHELL_MOVIE_MISSION_BUTTON, 0, NULL);
            act(h, ES2_SHELL_MOVIE_FOCUS_GAINED, 0, NULL);
            act(h, ES2_SHELL_MOVIE_FADE_IN, 0, NULL);
            theatre_ran = 1;
            act(h, ES2_SHELL_MOVIE_THEATRE_STEP, 0, NULL);
        }
        if (!m->intro && !credits) act(h, ES2_SHELL_MOVIE_DRAIN, 0, NULL);   /* 0x41efaf..0x41efbc */
        if (ask(h, ES2_SHELL_MOVIE_Q_PANEL)) {                          /* 0x41efc1..0x41efe8 */
            act(h, ES2_SHELL_MOVIE_WAIT, 2, NULL);
            act(h, ES2_SHELL_MOVIE_THEATRE_HIDE, 0, NULL);
            act(h, ES2_SHELL_MOVIE_REPAINT, ES2_SHELL_MOVIE_OBJ_BAY, NULL);
            act(h, ES2_SHELL_MOVIE_PALETTE, 1, NULL);
        }
        /* the theatre step may have queued the dropship: r stays the record
         * at the play index (the write index differs unless the ring is full) */
        r = &m->ring[m->play];
        if (r->callback) act(h, ES2_SHELL_MOVIE_CALLBACK, (int32_t)r->callback, NULL);   /* 0x41eff5..0x41f01c */
        empty_record(r, m->empty);                                      /* 0x41f01e..0x41f0aa */
        m->play = ring_next(m->play);                                   /* 0x41f0b1..0x41f0c7 */
    }
    if (!theatre_ran) {                                                 /* 0x41f10b..0x41f114 */
        act(h, ES2_SHELL_MOVIE_FOCUS_GAINED, 0, NULL);
        act(h, ES2_SHELL_MOVIE_FADE_IN, 0, NULL);
    }
    return 0;
}

/* ---- MCI strings ------------------------------------------------------------ */
int es2_shell_mci_open(char *out, size_t capacity, const char *path, long parent) {
    int n;
    if (!out || !path) return -1;
    n = snprintf(out, capacity, "open %s alias mov style child parent %ld", path, parent);   /* 0x470ef9 */
    return n < 0 || (size_t)n >= capacity ? -1 : n;
}

int es2_shell_mci_palette(char *out, size_t capacity, long palette) {
    int n;
    if (!out) return -1;
    n = snprintf(out, capacity, "setvideo mov palette handle to %ld", palette);   /* 0x470f3d */
    return n < 0 || (size_t)n >= capacity ? -1 : n;
}

/* ---- Start-up CD check and CD prompt (vshell_ui.md section 20) ---- */
int es2_shell_cd_policy_parse(const char *value) {
    if (!value || !value[0] || !strcmp(value, "original")) return ES2_SHELL_CD_ORIGINAL;
    if (!strcmp(value, "skip")) return ES2_SHELL_CD_SKIP;
    return -1;
}

/* Original: VSHELL.EXE 0x401cac..0x401d28 */
int es2_shell_cd_check(int policy, const char *cd_root, es2_shell_cd_probe probe, void *context,
    char *path, size_t capacity) {
    char local[512];
    int n;
    if (policy == ES2_SHELL_CD_SKIP) return 0;
    if (!probe) return -1;
    if (!path) { path = local; capacity = sizeof local; }
    n = snprintf(path, capacity, "%s%s", cd_root ? cd_root : "", names[0]);   /* avi\pt1.avi */
    if (n < 0 || (size_t)n >= capacity) return -1;
    return probe(context, path) ? 0 : ES2_SHELL_CD_ERROR_MESSAGE;
}

/* Original: VSHELL.EXE 0x4322a8 (box), Continue 0x4324fa, Quit 0x432569 */
int es2_shell_cd_prompt(int policy, es2_shell_cd_box box, void *context, int *quit) {
    if (policy == ES2_SHELL_CD_SKIP) return 0;
    if (box && box(context)) return 1;
    if (quit) *quit = 1;
    return 0;
}
