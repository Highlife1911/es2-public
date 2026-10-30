#ifndef ES2_SHELL_TESTS_VSHELL_FILE_H
#define ES2_SHELL_TESTS_VSHELL_FILE_H

/* The tests' own path to a VSHELL.EXE file image. The own processes
 * read VSHELL's bytes from the span store only (es2/shell/vshell_store.h);
 * the tests that compare the store, the table readers or a screen with the
 * reference file (and the unit tests on synthetic PE images) read the file
 * through this bounded section walk. */
#include "es2/shell/practice.h"

/* A span at virtual address `va` of a PE image held in memory: ES2_ERR_TRUNCATED
 * for a short image or a section past its end, ES2_ERR_MAGIC without the PE
 * signature, ES2_ERR_NOT_FOUND for a VA no section's raw data holds whole. */
es2_status es2_test_vshell_file_span(es2_span image, uint32_t va, size_t size, es2_span *out, es2_error *error);
/* Installs es2_test_vshell_file_span as es2_shell_exe_span's file reader
 * (es2_shell_set_file_reader). */
void es2_test_vshell_file_install(void);

#endif
