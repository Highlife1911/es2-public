#ifndef ES2_SHELL_TEXTS_H
#define ES2_SHELL_TEXTS_H

#include "es2/shell/campaign.h"

/* The texts of VSHELL's mission screen 0x442bc4 (vshell_ui.md section
 * 16): page 0 (the "<name> <sector>" title and the <lang>\campaign.str text
 * of 0x40fd2d), page 1 (briefing, objectives and intelligence of 0x4135bb
 * from data\mission.str), page 4 (the ten statistics values of 0x40f904) and
 * the line/page split of the text panels (ESREPORT 0x40d686). The caller
 * passes every file and ESTEXT entry as bytes; no file I/O, no RNG (none of
 * these functions draws in VSHELL either). Outputs are unchanged on failure;
 * buffers are released with the matching _free function. Inputs on which
 * VSHELL faults or asserts are refused. */

/* ---- Page 0 ------------------------------------------------------------- */
/* ESTEXT IDs of the title sprintf("%s %s", ESTEXT(0x77 + group - 1),
 * ESTEXT(0x7c)) at 0x444ae4..0x444b17 (16-bit sum, sign-extended). */
enum { ES2_SHELL_SECTOR_FIRST = 0x77, ES2_SHELL_SECTOR_WORD = 0x7c };
int es2_shell_sector_name_id(int16_t group);
/* The title: name and word up to their first NUL, joined by one space.
 * Refused: a title longer than 100 bytes (the label's setText 0x40b8bb
 * asserts) or not fitting `capacity` (including its NUL). */
es2_status es2_shell_sector_title(es2_span name, es2_span word, char *out, size_t capacity,
    es2_error *error);
/* The page-0 text: string group - 1 (CAREER group word 0x46fc58 minus one)
 * of <lang>\campaign.str read the way 0x40fd2d does (u32, i16 N, per string
 * i16 L, L bytes, E, E bytes skipped), up to the string's first NUL (*text
 * borrows the input). Refused: an index outside 0..N-1 (0x40fd2d returns a
 * null pointer and setText 0x40d231 faults in strlen) and malformed files. */
es2_status es2_shell_campaign_text(es2_span campaign_str, int16_t group, es2_span *text,
    es2_error *error);

/* ---- Page 1 ------------------------------------------------------------- */
/* The 148-byte briefing selection (0x4840cc..0x48419f, the SAV's selection
 * block after its two CAREER words; es2_shell_output.briefing): i16 count +
 * 10 objective references, i16 count + 30 briefing references, i16 count +
 * 30 intelligence references, the briefing movie word. */
#define ES2_SHELL_BRIEFING_SIZE 148u
enum { ES2_SHELL_TEXT_BRIEFING, ES2_SHELL_TEXT_OBJECTIVES, ES2_SHELL_TEXT_INTELLIGENCE, ES2_SHELL_TEXT_COUNT };
typedef struct es2_shell_briefing_texts {
    /* [0x46fc5c], [0x46fc60], [0x46fc64]: bytes without the final NUL */
    es2_shell_buffer text[ES2_SHELL_TEXT_COUNT];
} es2_shell_briefing_texts;
/* 0x4135bb: each text is the concatenation, in reference order, of the
 * strings (up to their first NUL) of every reference other than -1 into
 * the single group of `mission_str` (read without auxiliary data, as the
 * group iterator 0x4286e2 with a null aux pointer does; a stale tail after
 * the group is ignored). The stored counts are not used. Refused: a group
 * count outside 0..127 (the array is sized from its low byte, 0x42863a, and
 * a mismatch asserts at 0x413679), a reference other than -1 outside the
 * group, malformed files. `out` must be zero. */
es2_status es2_shell_briefing_texts_compose(es2_span mission_str, const uint8_t selection[ES2_SHELL_BRIEFING_SIZE],
    es2_shell_briefing_texts *out, es2_error *error);
void es2_shell_briefing_texts_free(es2_shell_briefing_texts *out);
/* The selection and the CAREER words of a SAV (what a load restores). */
es2_status es2_shell_briefing_selection(es2_span save, uint8_t selection[ES2_SHELL_BRIEFING_SIZE],
    int16_t *group, int16_t *position, es2_error *error);

/* ---- Page 4 ------------------------------------------------------------- */
/* The words 0x40f904 receives and reads (vshell_ui.md 14.2). Columns are in
 * widget order: [0] pilot word +0x2d, [1] +0x31, [2] +0x2f. */
typedef struct es2_shell_debrief_words {
    int16_t outcome;     /* 0x482bcd */
    uint32_t tally;      /* 0x482bcf, the results.dat salvage dword */
    int16_t recovered;   /* pair count P + items added by 0x412ce2 (16-bit) */
    int16_t lost;        /* 0x482bd3 */
    int16_t you[3], squad[3];
} es2_shell_debrief_words;
/* Player and squad columns from the memory state a SAV describes (the
 * results path's state at 0x40f524 equals its SAV: nothing between them
 * writes these words): You = the player's words; Squad = those plus, for
 * orders 1 .. team word 0 - 1, the selected pilot of the first group whose
 * order word +0x27 equals the order (0x4108fa) when its byte +0x24 is
 * nonzero (16-bit wrapping sums). Refused: a SAV that does not parse or a
 * selected-pilot index above 11. */
es2_status es2_shell_debrief_words_from_save(es2_span save, int16_t outcome, uint32_t tally, int16_t lost,
    int16_t recovered, es2_shell_debrief_words *out, es2_error *error);
/* The same for a finished results path (mode 1, past step 8): outcome,
 * tally and P from results.dat, lost and added from the output, the columns
 * from its SAV. Refused: results.dat shorter than 8 bytes or an output that
 * did not reach the advancement. */
es2_status es2_shell_debrief_words_from_results(es2_span results, const es2_shell_results_output *done,
    es2_shell_debrief_words *out, es2_error *error);
/* The same from results.dat and the SAV the results path wrote (what the own
 * menu holds after the -X3/-X4 step): lost is bank word 9 (step 6 stores the
 * lost count there; the later steps clear only variables 50..63) and the
 * items 0x412ce2 added are the sum of the positive bank words 21..49 (one
 * item per unit; those variables are not cleared), 16-bit wrapping. Valid
 * for a path that reached the advancement in mode 1, as 0x40f904 is. */
es2_status es2_shell_debrief_words_after(es2_span results, es2_span save_after, es2_shell_debrief_words *out,
    es2_error *error);
/* ESTEXT ID of the outcome text: (int16)(outcome + 0x146). */
int es2_shell_debrief_outcome_id(int16_t outcome);
enum { ES2_SHELL_DEBRIEF_UNIT = 0x149, ES2_SHELL_DEBRIEF_VALUES = 10 };
typedef struct es2_shell_debrief_texts {
    /* In 0x40f904's call order: outcome (widget 0x48d91c), salvage 0x48d924,
     * weapons recovered 0x48d92c, You/Squad column 1 (0x48d94c, 0x48d958),
     * column 2 (0x48d950, 0x48d95c), column 3 (0x48d954, 0x48d960), lost
     * 0x48d964. */
    char text[ES2_SHELL_DEBRIEF_VALUES][101];
    uint32_t widget[ES2_SHELL_DEBRIEF_VALUES];
} es2_shell_debrief_texts;
/* The ten setText texts: outcome_text (ESTEXT of the outcome ID) as is,
 * "%d %s" of tally / 1000 (unsigned) and unit_text (ESTEXT 0x149), "%d" of
 * the signed words. Texts are taken up to their first NUL. Refused: the
 * salvage text above 39 bytes (it would overrun 0x40f904's 40-byte buffer
 * into the column words) or an outcome text above 100 bytes (setText
 * asserts). */
es2_status es2_shell_debrief_texts_compose(const es2_shell_debrief_words *words, es2_span outcome_text,
    es2_span unit_text, es2_shell_debrief_texts *out, es2_error *error);

/* ---- Text panels: ESREPORT line breaking 0x40d686 and pages ----------- */
typedef struct es2_shell_report {
    /* The panel's copy after the breaker: line breaks and break spaces are
     * NULs, other bytes 0x01..0x1f are spaces; line i is the NUL-terminated
     * string at text.data + line[i] (lines may overlap, as in VSHELL). */
    es2_shell_buffer text;
    size_t *line;
    size_t lines;
    int16_t pages;   /* lines / rows + ((lines + 1) != rows), 0x40d2d0 */
} es2_shell_report;
/* `text` up to its first NUL; widths[b] = getWidth of byte b in the panel
 * font (0x4010fd); width = r - l of the panel; rows = (b - t) / font height.
 * Refused: width or rows not positive, the breaker's assertion at 0x40d798
 * (the line after a break is not narrower than width), allocation failure.
 * `out` must be zero. */
es2_status es2_shell_report_layout(es2_span text, const uint8_t widths[256], int16_t width, int16_t rows,
    es2_shell_report *out, es2_error *error);
void es2_shell_report_free(es2_shell_report *report);

#endif
