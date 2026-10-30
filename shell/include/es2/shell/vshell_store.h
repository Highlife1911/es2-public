#ifndef ES2_SHELL_VSHELL_STORE_H
#define ES2_SHELL_VSHELL_STORE_H

#include "es2/formats/common.h"

/* The bytes es2shell and es2menu take from VSHELL.EXE
 * as own data. The runs are generated from the reference VSHELL.EXE by
 * tools/reference-game/scripts/vshell_store.py into shell/data/vshell_store_data.c
 * (never edited by hand); each run keeps its original virtual address (image
 * base 0x00400000) and names the reader it serves. //shell:vshell_store_real_test
 * compares every run and the icon with the file (vshell_presentation.md,
 * "the VSHELL span store"). */
typedef struct es2_vshell_run {
    uint32_t va;
    uint32_t size;
    const uint8_t *bytes;
    const char *readers;   /* provenance: the reader(s) the run serves */
} es2_vshell_run;
/* Sorted by VA, disjoint and not adjacent. */
extern const es2_vshell_run es2_vshell_runs[];
extern const size_t es2_vshell_run_count;

/* VSHELL's class icon "ES2ICON" (0x40687c: LoadIconA(hInstance, "es2icon")):
 * its RT_GROUP_ICON resource and the RT_ICON images it names, each with the
 * VA of its resource data. */
typedef struct es2_vshell_icon_image {
    uint16_t id;
    uint32_t va;
    uint32_t size;
    const uint8_t *bytes;
} es2_vshell_icon_image;
extern const uint32_t es2_vshell_icon_group_va;
extern const uint32_t es2_vshell_icon_group_size;
extern const uint8_t es2_vshell_icon_group[];
extern const es2_vshell_icon_image es2_vshell_icon_images[];
extern const size_t es2_vshell_icon_image_count;
/* Provenance line of the generated file (source file and SHA-256). */
extern const char es2_vshell_store_source[];

/* The image handle that selects the store in es2_shell_exe_span (practice.h):
 * es2_shell_exe_span(ES2_VSHELL_STORE, va, size, ...) answers from the runs. */
extern const uint8_t es2_vshell_store_handle[1];
#define ES2_VSHELL_STORE ((es2_span){es2_vshell_store_handle, 0})

/* The bytes [va, va + size) when one run holds all of them; a read outside the
 * store (also one that straddles a run's end) is ES2_ERR_NOT_FOUND with the
 * VA as offset, never zero bytes. size 0 is ES2_ERR_ARGUMENT. */
es2_status es2_vshell_store_span(uint32_t va, size_t size, es2_span *out, es2_error *error);

/* The measurement switch (off by default): with the environment
 * variable ES2_SHELL_SPAN_LOG naming an existing directory, every successful
 * read through es2_shell_exe_span and the layout literal reader appends one
 * line "<reader> 0x<va> <size>" to <dir>/spans-<pid>.log. Without it nothing
 * is written and nothing changes. */
void es2_shell_span_log(const char *reader, uint32_t va, size_t size);

#endif
