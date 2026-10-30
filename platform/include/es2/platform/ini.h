/* An INI reader with the rules of GetPrivateProfileStringW/IntW for the keys
 * the code reads (platform/src/ini.c; the comparison with the API is
 * platform/tests/ini_test.c). It serves the builds without the Win32 API. */
#ifndef ES2_PLATFORM_INI_H
#define ES2_PLATFORM_INI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The largest file the reader takes; a larger one counts as missing. */
#define ES2_INI_FILE_MAX (16u * 1024u * 1024u)

/* The text of a file's bytes as UTF-16: after a UTF-16LE byte order mark
 * the rest, otherwise the bytes as ANSI (Windows-1252; a UTF-8 byte order
 * mark is three ANSI characters, as for the API). out needs count units;
 * returns the units written. */
size_t es2_ini_decode(const uint8_t *bytes, size_t count, uint16_t *out);

/* GetPrivateProfileStringW on the decoded text: section and key name an
 * entry, compared without case; fallback (NULL is "") stands for a missing
 * entry. Writes at most capacity units including the terminating 0 and
 * returns the units before it. text NULL is a missing file. */
uint32_t es2_ini_string(const uint16_t *text, size_t count, const uint16_t *section, const uint16_t *key,
                        const uint16_t *fallback, uint16_t *out, uint32_t capacity);
/* GetPrivateProfileIntW on the decoded text: the value's first 255 units,
 * the fallback when there are none. */
uint32_t es2_ini_int(const uint16_t *text, size_t count, const uint16_t *section, const uint16_t *key,
                     int32_t fallback);
/* RtlUnicodeStringToInteger with base 0 on a 0-terminated string: blanks,
 * one sign, the prefixes 0x, 0o and 0b (lower case only), digits up to the
 * first that does not fit the base, modulo 2^32. */
uint32_t es2_ini_parse_int(const uint16_t *text);

/* The same on a file (es2/platform/os.h): GetPrivateProfileStringW/IntW
 * and GetPrivateProfileStringA, whose text and result are ANSI. A file that
 * cannot be read gives the fallback. */
uint32_t es2_ini_file_string(const uint16_t *file, const uint16_t *section, const uint16_t *key,
                             const uint16_t *fallback, uint16_t *out, uint32_t capacity);
uint32_t es2_ini_file_int(const uint16_t *file, const uint16_t *section, const uint16_t *key, int32_t fallback);
uint32_t es2_ini_file_string_ansi(const char *file, const char *section, const char *key, const char *fallback,
                                  char *out, uint32_t capacity);

#ifdef __cplusplus
}
#endif

#endif
