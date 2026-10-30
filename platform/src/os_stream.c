/* Streams over the handle functions (os_stream.h). */
#include "os_stream.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "es2/platform/os.h"

/* the C runtime's buffer size (BUFSIZ of msvcrt) */
#define STREAM_BUFFER 4096
enum { IDLE, READING, WRITING };
enum { FILE_BEGIN_ = 0, FILE_CURRENT_ = 1, FILE_END_ = 2 };

struct es2_os_hstream {
    es2_os_handle handle;
    int can_read, can_write, append, text;
    int state, eof, error;
    size_t pos, len;   /* READING: buf[pos, len) not yet taken; WRITING: buf[0, len) pending */
    uint8_t buf[STREAM_BUFFER];
};

es2_os_hstream *es2_os_hstream_open(const char *path, const char *mode) {
    uint32_t access, disposition;
    int plus = 0, text = 1;
    if (!path || !mode) return NULL;
    switch (mode[0]) {
    case 'r': access = ES2_OS_GENERIC_READ; disposition = ES2_OS_OPEN_EXISTING; break;
    case 'w': access = ES2_OS_GENERIC_WRITE; disposition = ES2_OS_CREATE_ALWAYS; break;
    case 'a': access = ES2_OS_GENERIC_WRITE; disposition = ES2_OS_OPEN_ALWAYS; break;
    default: return NULL;
    }
    for (const char *m = mode + 1; *m; m++) {
        if (*m == '+') plus = 1;
        else if (*m == 'b') text = 0;
        else if (*m == 't') text = 1;
        /* N (not inherited), c, n, S, R, T, D: no effect on the bytes */
    }
    if (plus) access = ES2_OS_GENERIC_READ | ES2_OS_GENERIC_WRITE;
    es2_os_hstream *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->handle = es2_os_create_file(path, access, ES2_OS_FILE_SHARE_READ | ES2_OS_FILE_SHARE_WRITE, 0, disposition,
                                   ES2_OS_FILE_ATTRIBUTE_NORMAL);
    if (s->handle == ES2_OS_INVALID_HANDLE) {
        free(s);
        return NULL;
    }
    s->can_read = (access & ES2_OS_GENERIC_READ) != 0;
    s->can_write = (access & ES2_OS_GENERIC_WRITE) != 0;
    s->append = mode[0] == 'a';
    s->text = text;
    return s;
}

static int flush_write(es2_os_hstream *s) {
    size_t at = 0;
    if (s->state != WRITING) return 0;
    if (s->len && s->append && es2_os_set_file_pointer(s->handle, 0, FILE_END_) == 0xffffffffu) {
        s->error = 1;
        s->len = 0;
        return -1;
    }
    while (at < s->len) {
        uint32_t done = 0;
        if (!es2_os_write_file(s->handle, s->buf + at, (uint32_t)(s->len - at), &done) || !done) {
            s->error = 1;
            s->len = 0;
            return -1;
        }
        at += done;
    }
    s->len = 0;
    return 0;
}

/* Leaves the read state: the handle back at the first byte not yet taken. */
static void drop_read(es2_os_hstream *s) {
    if (s->state == READING && s->pos < s->len)
        es2_os_set_file_pointer(s->handle, -(int32_t)(s->len - s->pos), FILE_CURRENT_);
    s->pos = s->len = 0;
}

static int to_state(es2_os_hstream *s, int state) {
    if (s->state == state) return 0;
    if (state == READING && !s->can_read) { s->error = 1; return -1; }
    if (state == WRITING && !s->can_write) { s->error = 1; return -1; }
    if (s->state == WRITING && flush_write(s)) return -1;
    if (s->state == READING) drop_read(s);
    s->state = state;
    return 0;
}

/* The next raw byte, -1 at the end of the file. */
static int raw_peek(es2_os_hstream *s) {
    if (s->pos == s->len) {
        uint32_t done = 0;
        s->pos = s->len = 0;
        if (!es2_os_read_file(s->handle, s->buf, STREAM_BUFFER, &done)) {
            s->error = 1;
            return -1;
        }
        s->len = done;
        if (!done) return -1;
    }
    return s->buf[s->pos];
}

int es2_os_hstream_getc(es2_os_hstream *s) {
    int c;
    if (!s || s->eof || to_state(s, READING)) return EOF;
    c = raw_peek(s);
    if (c < 0) { s->eof = 1; return EOF; }
    if (s->text && c == 0x1a) { s->eof = 1; return EOF; }   /* Ctrl-Z ends a text file and stays */
    s->pos++;
    if (s->text && c == '\r' && raw_peek(s) == '\n') {
        s->pos++;
        return '\n';
    }
    return c;
}

size_t es2_os_hstream_read(void *buffer, size_t size, size_t count, es2_os_hstream *s) {
    uint8_t *out = buffer;
    size_t total, n = 0;
    if (!s || !size || !count || count > SIZE_MAX / size) return 0;
    total = size * count;
    if (!s->text) {
        if (s->eof || to_state(s, READING)) return 0;
        while (n < total) {
            if (raw_peek(s) < 0) { s->eof = 1; break; }
            size_t take = s->len - s->pos < total - n ? s->len - s->pos : total - n;
            memcpy(out + n, s->buf + s->pos, take);
            s->pos += take;
            n += take;
        }
    } else {
        int c;
        while (n < total && (c = es2_os_hstream_getc(s)) != EOF) out[n++] = (uint8_t)c;
    }
    return n / size;
}

static int put_raw(es2_os_hstream *s, uint8_t c) {
    if (s->len == STREAM_BUFFER && flush_write(s)) return -1;
    s->buf[s->len++] = c;
    return 0;
}

size_t es2_os_hstream_write(const void *buffer, size_t size, size_t count, es2_os_hstream *s) {
    const uint8_t *in = buffer;
    size_t total;
    if (!s || !size || !count || count > SIZE_MAX / size || to_state(s, WRITING)) return 0;
    total = size * count;
    for (size_t i = 0; i < total; i++) {
        if (s->text && in[i] == '\n' && put_raw(s, '\r')) return i / size;
        if (put_raw(s, in[i])) return i / size;
    }
    return count;
}

int es2_os_hstream_putc(int c, es2_os_hstream *s) {
    uint8_t byte = (uint8_t)c;
    return es2_os_hstream_write(&byte, 1, 1, s) == 1 ? byte : EOF;
}

int es2_os_hstream_puts(const char *text, es2_os_hstream *s) {
    size_t n = strlen(text);
    if (!n) return s ? 0 : EOF;
    return es2_os_hstream_write(text, 1, n, s) == n ? 0 : EOF;
}

int es2_os_hstream_vprintf(es2_os_hstream *s, const char *format, va_list args) {
    char small[512], *text = small;
    va_list again;
    va_copy(again, args);
    int n = vsnprintf(small, sizeof small, format, args);
    if (n < 0) { va_end(again); return -1; }
    if ((size_t)n >= sizeof small) {
        text = malloc((size_t)n + 1);
        if (!text) { va_end(again); return -1; }
        vsnprintf(text, (size_t)n + 1, format, again);
    }
    va_end(again);
    size_t written = n ? es2_os_hstream_write(text, 1, (size_t)n, s) : 0;
    if (text != small) free(text);
    return written == (size_t)n ? n : -1;
}

int es2_os_hstream_flush(es2_os_hstream *s) {
    if (!s) return EOF;
    return s->state == WRITING && flush_write(s) ? EOF : 0;
}

int es2_os_hstream_error(es2_os_hstream *s) { return s ? s->error : 1; }

long es2_os_hstream_tell(es2_os_hstream *s) {
    uint32_t at;
    if (!s) return -1;
    if (s->state == WRITING && s->append && s->len) {
        at = es2_os_file_size(s->handle);
        return at == ES2_OS_INVALID_FILE_SIZE ? -1 : (long)(at + s->len);
    }
    at = es2_os_set_file_pointer(s->handle, 0, FILE_CURRENT_);
    if (at == 0xffffffffu) return -1;
    if (s->state == READING) return (long)(at - (s->len - s->pos));
    if (s->state == WRITING) return (long)(at + s->len);
    return (long)at;
}

int es2_os_hstream_seek(es2_os_hstream *s, long offset, int origin) {
    long base;
    if (!s || origin < SEEK_SET || origin > SEEK_END) return -1;
    if (origin == SEEK_CUR) {
        if ((base = es2_os_hstream_tell(s)) < 0) return -1;
        offset += base;
        origin = SEEK_SET;
    }
    if (s->state == WRITING && flush_write(s)) return -1;
    if (s->state == READING) drop_read(s);
    s->state = IDLE;
    if (origin == SEEK_SET && offset < 0) return -1;
    if (es2_os_set_file_pointer(s->handle, (int32_t)offset, origin == SEEK_SET ? FILE_BEGIN_ : FILE_END_) ==
        0xffffffffu)
        return -1;
    s->eof = 0;
    return 0;
}

int es2_os_hstream_close(es2_os_hstream *s) {
    int result;
    if (!s) return EOF;
    result = es2_os_hstream_flush(s);
    if (!es2_os_close_handle(s->handle)) result = EOF;
    free(s);
    return result;
}
