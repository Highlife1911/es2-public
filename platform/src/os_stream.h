/* Buffered streams over the handle functions of es2/platform/os.h, with the
 * behaviour of the Windows C runtime's FILE that the callers see: the modes
 * r, w, a and + with b, t and N, CreateFileA with read and write sharing,
 * append writes at the end of the file, text mode (the default without b)
 * writing LF as CR LF and reading CR LF as LF up to a Ctrl-Z, and ftell over
 * the buffered bytes (reading in text mode: the raw position, where the CRT
 * estimates it from the LFs; no caller reads in text mode). os_wasm.c serves es2_os_fopen and its family with
 * them; os_stream_test compares them with the C runtime on Windows. */
#ifndef ES2_PLATFORM_OS_STREAM_H
#define ES2_PLATFORM_OS_STREAM_H

#include <stdarg.h>
#include <stddef.h>

typedef struct es2_os_hstream es2_os_hstream;

es2_os_hstream *es2_os_hstream_open(const char *path, const char *mode);
int es2_os_hstream_close(es2_os_hstream *s);
size_t es2_os_hstream_read(void *buffer, size_t size, size_t count, es2_os_hstream *s);
size_t es2_os_hstream_write(const void *buffer, size_t size, size_t count, es2_os_hstream *s);
int es2_os_hstream_seek(es2_os_hstream *s, long offset, int origin);
long es2_os_hstream_tell(es2_os_hstream *s);
int es2_os_hstream_getc(es2_os_hstream *s);
int es2_os_hstream_putc(int c, es2_os_hstream *s);
int es2_os_hstream_puts(const char *text, es2_os_hstream *s);
int es2_os_hstream_flush(es2_os_hstream *s);
int es2_os_hstream_error(es2_os_hstream *s);
int es2_os_hstream_vprintf(es2_os_hstream *s, const char *format, va_list args);

#endif
