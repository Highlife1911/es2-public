/* The file and console imports of DBSIM.EXE from CW3220.DLL. Read
 * from the llvm-objdump listing of the reference CW3220.DLL (link base
 * 0x400000; addresses below are at that base). Contract and evidence:
 * docs/reference-game/rtl_imports.md, section "file and console imports".
 *
 * Handle layer: _open 409d84, _close 409a74, _read 408108 (binary reads
 * 409970, CR removal 40809c), _write 4082f0 (binary writes 4099d4, CRLF
 * expansion 408238), _lseek 409d0c, _filelength 408b2c, eof 408014, handle
 * allocation 408ebc, error setters 409158/4091bc/40919c.
 * FILE layer: _fopen 405a44 (free FILE 405a18, open 405970, mode 4058a4),
 * setvbuf 406e88/405384, _fclose 40555c, fflush 4055f0, flush of every
 * stream 405810, _flushall 407d68, _fread 405cec/405bbc, fgetc 40615c, fill
 * 4060e4, terminal flush 4060b0, __fgetc 406148, ungetc 4074cc, _fwrite
 * 405ed4/405aa8, fputc 40622c, _fseek 405dd4, buffered count 405d78, _ftell
 * 405e38, _rewind 406610, _fscanf 405d54 with the scanner 40668c and its
 * number reader 406c24.
 * Find: _findfirst 4057d0, _findnext 4057f4, node lookup 4086ec,
 * 408740, 408784. Clock: _time 41eb64, seconds 41e50c, summer time 41e3f8.
 * Console: _printf 40655c, __fputc 407da0, _fputs 405b7c (stubs). */
#include "es2/core/rtl_io.h"
#include "es2/core/rtl.h"

#include <string.h>

/* ------------------------------------------------------------------------
 * Layout (RVAs of the reference DLL). */
void es2_io_layout_cw3220(es2_io_layout *layout, uint32_t base)
{
    layout->nfile = base + 0x1fce0u;
    layout->openfd = base + 0x1fce4u;
    layout->handles = base + 0x234e8u;
    layout->streams = base + 0x1f830u;
    layout->errno_word = base + 0x22248u;
    layout->doserrno = base + 0x235d0u;
    layout->error_count = base + 0x1fe8cu;
    layout->error_map = base + 0x20358u;
    layout->fmode_pointer = base + 0x1fdb0u;
    layout->umask_word = base + 0x20354u;
    layout->exit_buffers = base + 0x222bcu;
    layout->exit_buffers_value = base + 0x7ca4u;
    layout->exit_files = base + 0x222c0u;
    layout->exit_files_value = base + 0x7c7cu;
    layout->getc_byte = base + 0x235b0u;
    layout->find_list = base + 0x235ccu;
    layout->scan_class = base + 0x20244u;
    layout->time_zone = base + 0x23070u;
    layout->summer_time = base + 0x23074u;
    layout->stdout_file = base + 0x1f848u;
}

/* ------------------------------------------------------------------------
 * Context and memory. */
typedef struct io {
    const es2_io_env *env;
    const es2_io_layout *l;
} io;

static uint8_t r8(const io *c, uint32_t a) { return c->env->read8(c->env->context, a); }
static uint32_t r16(const io *c, uint32_t a) { return (uint32_t)r8(c, a) | (uint32_t)r8(c, a + 1u) << 8; }
static uint32_t r32(const io *c, uint32_t a) { return r16(c, a) | r16(c, a + 2u) << 16; }
static void w8(const io *c, uint32_t a, uint32_t v) { c->env->write8(c->env->context, a, (uint8_t)v); }
static void w16(const io *c, uint32_t a, uint32_t v) { w8(c, a, v); w8(c, a + 1u, v >> 8); }
static void w32(const io *c, uint32_t a, uint32_t v) { w16(c, a, v); w16(c, a + 2u, v >> 16); }
static int32_t s32(uint32_t v) { return v < 0x80000000u ? (int32_t)v : (int32_t)(v - 0x80000000u) - 0x7fffffff - 1; }
static int32_t s8(uint32_t v) { v &= 0xffu; return v < 0x80u ? (int32_t)v : (int32_t)v - 0x100; }

static void os_call(const io *c, es2_io_os *call) { call->result = 0; call->count = 0; c->env->os(c->env->context, call); }
static uint32_t os1(const io *c, uint32_t function, uint32_t a0)
{
    es2_io_os call;
    memset(&call, 0, sizeof(call));
    call.function = function; call.arg[0] = a0;
    os_call(c, &call);
    return call.result;
}

/* CW3220 memchr (4029c8): bytes in order up to the first match; n = 0: NULL. */
static uint32_t memchr_g(const io *c, uint32_t p, uint8_t value, uint32_t n)
{
    for (; n; --n, ++p) if (r8(c, p) == value) return p;
    return 0;
}
/* CW3220 memcpy: a forward copy (every overlap here has dest <= source). */
static void memcpy_g(const io *c, uint32_t dest, uint32_t source, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i) w8(c, dest + i, r8(c, source + i));
}

/* ------------------------------------------------------------------------
 * errno (409158, 4091bc, 40919c). */
static uint32_t io_error(const io *c, int32_t code)
{
    uint32_t e;
    if (code >= 0) {
        if (code > 0x12a) code = 1;
        w32(c, c->l->doserrno, (uint32_t)code);
        e = (uint32_t)s8(r8(c, c->l->error_map + (uint32_t)code));
    } else {
        e = (uint32_t)-code;
        if (s32(e) >= s32(r32(c, c->l->error_count))) {
            w32(c, c->l->doserrno, 1u);
            e = (uint32_t)s8(r8(c, c->l->error_map + 1u));
        } else {
            w32(c, c->l->doserrno, 0xffffffffu);
        }
    }
    w32(c, c->l->errno_word, e);
    return 0xffffffffu;
}
static uint32_t nt_error(const io *c)
{
    uint32_t e = os1(c, ES2_IO_OS_GET_LAST_ERROR, 0) & 0xffffu;
    return io_error(c, (int32_t)e);
}
static uint32_t nt_error_code(const io *c)
{
    uint32_t e = os1(c, ES2_IO_OS_GET_LAST_ERROR, 0) & 0xffffu;
    io_error(c, (int32_t)e);
    return e;
}

/* ------------------------------------------------------------------------
 * Handle layer. */
static uint32_t nfile(const io *c) { return r32(c, c->l->nfile); }
static uint32_t fd_flags_address(const io *c, uint32_t fd) { return c->l->openfd + 4u * fd; }
static uint32_t fd_handle(const io *c, uint32_t fd) { return r32(c, c->l->handles + 4u * fd); }

/* open (409d84). `mode` is read only with O_CREAT (0x100). */
static uint32_t io_open(const io *c, uint32_t path, uint32_t flags, uint32_t mode_slot, int mode_known, uint32_t mode_value)
{
    if (!(flags & 0xc000u)) flags |= r32(c, r32(c, c->l->fmode_pointer)) & 0xc000u;
    if (!(flags & 0x8000u)) flags |= 0x4000u;
    uint32_t disposition, x = flags & 0x700u;
    if (x > 0x500u) disposition = x == 0x600u ? 5u : x == 0x700u ? 1u : 3u;
    else if (x == 0x500u) disposition = 1u;
    else if (x == 0x100u) disposition = 4u;
    else if (x == 0x200u) disposition = 5u;
    else if (x == 0x300u) disposition = 2u;
    else disposition = 3u;
    uint32_t attributes;
    if (flags & 0x100u) {
        uint32_t mode = mode_known ? mode_value : r32(c, mode_slot);
        attributes = ((mode & r32(c, c->l->umask_word)) & 0x80u) ? 0x80u : 1u;
    } else {
        attributes = os1(c, ES2_IO_OS_GET_FILE_ATTRIBUTES, path);
        if (attributes == 0xffffffffu) attributes = 0;
    }
    uint32_t access;
    switch (flags & 3u) {
    case 0: access = 0x80000000u; break;
    case 1: access = 0x40000000u; break;
    case 2: access = 0xc0000000u; break;
    default: return io_error(c, 1);
    }
    uint32_t share;
    switch (flags & 0x70u) {
    case 0x10u: share = 0; break;
    case 0x20u: share = 1; break;
    case 0x30u: share = 2; break;
    default: share = 3; break;
    }
    es2_io_os call;
    memset(&call, 0, sizeof(call));
    call.function = ES2_IO_OS_CREATE_FILE;
    call.arg[0] = path; call.arg[1] = access; call.arg[2] = share;
    call.arg[3] = (flags & 0x80u) ? 0u : 1u; call.arg[4] = disposition; call.arg[5] = attributes;
    os_call(c, &call);
    uint32_t handle = call.result;
    if (handle == 0xffffffffu) {
        uint32_t e = os1(c, ES2_IO_OS_GET_LAST_ERROR, 0) & 0xffffu;
        if (e == 0x6eu) e = (flags & 0x100u) ? 0x50u : 2u;
        return io_error(c, (int32_t)e);
    }
    if (os1(c, ES2_IO_OS_GET_FILE_TYPE, handle) == 2u) flags |= 0x2000u;
    if (!(attributes & 1u)) flags |= 0x100u;
    flags &= 0xfffff8ffu;
    /* Handle allocation (408ebc): the first free entry. */
    uint32_t n = nfile(c), fd = 0;
    while (s32(fd) < s32(n) && r32(c, fd_flags_address(c, fd)) != 0) ++fd;
    if (fd == n) {
        io_error(c, 4);
        os1(c, ES2_IO_OS_CLOSE_HANDLE, handle);
        return 0xffffffffu;
    }
    w32(c, fd_flags_address(c, fd), flags);
    w32(c, c->l->handles + 4u * fd, handle);
    return fd;
}

static uint32_t io_close(const io *c, uint32_t fd)
{
    if (fd >= nfile(c)) return io_error(c, 6);
    if (os1(c, ES2_IO_OS_CLOSE_HANDLE, fd_handle(c, fd)) == 1u) {
        w32(c, fd_flags_address(c, fd), 0);
        w32(c, c->l->handles + 4u * fd, 0xffffffffu);
        return 0;
    }
    return nt_error(c);
}

/* Binary read (409970). */
static uint32_t rtl_read(const io *c, uint32_t fd, uint32_t buffer, uint32_t n, int need_bytes)
{
    if (fd >= nfile(c)) return io_error(c, 6);
    es2_io_os call;
    memset(&call, 0, sizeof(call));
    call.function = ES2_IO_OS_READ_FILE;
    call.arg[0] = fd_handle(c, fd); call.arg[1] = buffer; call.arg[2] = n; call.arg[3] = (uint32_t)need_bytes;
    os_call(c, &call);
    return call.result == 1u ? call.count : nt_error(c);
}

/* Removes every CR (40809c); returns the remaining length. */
static uint32_t remove_cr(const io *c, uint32_t buffer, uint32_t n)
{
    uint32_t end = buffer + n, dest = buffer, source = buffer;
    for (;;) {
        uint32_t q = memchr_g(c, source, '\r', end - source);
        if (!q) q = end;
        uint32_t k = q - source;
        if (dest != source) memcpy_g(c, dest, source, k);
        if (q >= end - 1u) return dest - buffer + k;
        source = q + 1u;
        dest += k;
    }
}

static uint32_t io_read(const io *c, uint32_t fd, uint32_t buffer, uint32_t n, int need_bytes)
{
    if (fd >= nfile(c)) return io_error(c, -6);
    if (n + 1u < 2u) return 0;
    uint32_t flags_at = fd_flags_address(c, fd);
    if (!(r32(c, flags_at) & 0x4000u)) return rtl_read(c, fd, buffer, n, need_bytes);
    if (r32(c, flags_at) & 0x200u) return 0;
    uint32_t total = 0, p = buffer;
    while (total < n) {
        uint32_t rest = n - total;
        uint32_t got = rtl_read(c, fd, p, rest, 1);
        if (got == 0xffffffffu) return 0xffffffffu;
        if (got == 0) return total;
        uint32_t z = memchr_g(c, p, 0x1a, got);
        if (z) {
            w32(c, flags_at, r32(c, flags_at) | 0x200u);
            got = z - p;
            if (got == 0) return total;
        }
        if (r8(c, p + got - 1u) == '\r' && rtl_read(c, fd, p + got - 1u, 1, 1) + 1u == 0) return 0xffffffffu;
        uint32_t k = remove_cr(c, p, got);
        total += k;
        if (got < rest && (r32(c, flags_at) & 0x2000u)) return total;
        if (z || k) return total;
        p += k;
    }
    return total;
}

/* A write source: guest memory or a host buffer (fputc's character, the
 * CRLF expansion buffer). */
typedef struct source { uint32_t guest; const uint8_t *host; } source;
static uint8_t source_byte(const io *c, const source *s, uint32_t i) { return s->host ? s->host[i] : r8(c, s->guest + i); }

static uint32_t rtl_write(const io *c, uint32_t fd, const source *s, uint32_t n)
{
    if (fd >= nfile(c)) return io_error(c, 6);
    es2_io_os call;
    memset(&call, 0, sizeof(call));
    call.function = ES2_IO_OS_WRITE_FILE;
    call.arg[0] = fd_handle(c, fd); call.arg[1] = s->host ? 0u : s->guest; call.arg[2] = n;
    call.data = s->host;
    os_call(c, &call);
    return call.result == 1u ? call.count : nt_error(c);
}

static uint32_t io_lseek(const io *c, uint32_t fd, uint32_t offset, uint32_t whence)
{
    if (fd >= nfile(c)) return io_error(c, 6);
    if (whence > 2u) return io_error(c, 1);
    uint32_t at = fd_flags_address(c, fd);
    w32(c, at, r32(c, at) & ~0x200u);
    es2_io_os call;
    memset(&call, 0, sizeof(call));
    call.function = ES2_IO_OS_SET_FILE_POINTER;
    call.arg[0] = fd_handle(c, fd); call.arg[1] = offset; call.arg[2] = whence;
    os_call(c, &call);
    if (call.result == 0xffffffffu) nt_error(c);
    return call.result;
}

static uint32_t io_write(const io *c, uint32_t fd, const source *s, uint32_t n)
{
    if (fd >= nfile(c)) return io_error(c, -6);
    if (n + 1u < 2u) return 0;
    uint32_t flags_at = fd_flags_address(c, fd);
    if (r32(c, flags_at) & 0x800u) io_lseek(c, fd, 0, 2);
    if (!(r32(c, flags_at) & 0x4000u)) return rtl_write(c, fd, s, n);
    /* Text mode (4082f0 with 408238): up to 0x80 expanded bytes per write. */
    uint32_t offset = 0, left = n;
    while (left) {
        uint8_t out[0x80];
        uint32_t produced = 0, consumed = 0, src = offset, end = offset + left;
        for (;;) {
            uint32_t k = end - src, newline = 0, found = 0;
            for (uint32_t i = 0; i < k; ++i) if (source_byte(c, s, src + i) == '\n') { newline = i; found = 1; break; }
            if (found) k = newline;
            if (k > 0x80u - produced) {
                k = 0x80u - produced;
                for (uint32_t i = 0; i < k; ++i) out[produced + i] = source_byte(c, s, src + i);
                consumed += k; produced += k;
                break;
            }
            for (uint32_t i = 0; i < k; ++i) out[produced + i] = source_byte(c, s, src + i);
            produced += k;
            if (!found || s32(0x80u - produced) < 2) { consumed += k; break; }
            out[produced++] = '\r';
            out[produced++] = '\n';
            src += k + 1u;
            consumed += k + 1u;
        }
        source expanded = {0, out};
        uint32_t w = rtl_write(c, fd, &expanded, produced);
        if (produced != w) return w == 0xffffffffu ? 0xffffffffu : offset + w;
        offset += consumed;
        left -= consumed;
    }
    return n;
}

static uint32_t io_filelength(const io *c, uint32_t fd)
{
    if (fd >= nfile(c)) return io_error(c, 6);
    uint32_t size = os1(c, ES2_IO_OS_GET_FILE_SIZE, fd_handle(c, fd));
    if (size == 0xffffffffu) size = nt_error(c);
    return size;
}

/* eof (408014). */
static uint32_t io_eof(const io *c, uint32_t fd)
{
    if (fd >= nfile(c)) return io_error(c, -6);
    uint32_t flags = r32(c, fd_flags_address(c, fd));
    if (flags & 0x200u) return 1;
    if (flags & 0x2000u) return 0;
    uint32_t at = io_lseek(c, fd, 0, 1);
    if (at == 0xffffffffu) return 0xffffffffu;
    uint32_t end = io_lseek(c, fd, 0, 2);
    if (end == 0xffffffffu) return 0xffffffffu;
    if (io_lseek(c, fd, at, 0) == 0xffffffffu) return 0xffffffffu;
    return s32(end) <= s32(at) ? 1u : 0u;
}

/* ------------------------------------------------------------------------
 * FILE layer. FILE: +0 curp, +4 buffer, +8 level, +0xc bsize, +0x10 istemp
 * (word), +0x12 flags (word), +0x14 token (word = low half of the FILE's
 * address), +0x16 fd (signed byte), +0x17 hold byte. */
#define F_CURP 0x0u
#define F_BUFFER 0x4u
#define F_LEVEL 0x8u
#define F_BSIZE 0xcu
#define F_ISTEMP 0x10u
#define F_FLAGS 0x12u
#define F_TOKEN 0x14u
#define F_FD 0x16u
#define F_HOLD 0x17u

static uint32_t f_fd(const io *c, uint32_t fp) { return (uint32_t)s8(r8(c, fp + F_FD)); }
static uint32_t f_flags(const io *c, uint32_t fp) { return r16(c, fp + F_FLAGS); }
static void f_set_flags(const io *c, uint32_t fp, uint32_t v) { w16(c, fp + F_FLAGS, v); }
static int token_ok(const io *c, uint32_t fp) { return r16(c, fp + F_TOKEN) == (fp & 0xffffu); }

static uint32_t f_flush(const io *c, uint32_t fp);
static uint32_t f_seek(const io *c, uint32_t fp, uint32_t offset, uint32_t whence);

/* Every stream with pending output (405810). */
static uint32_t flush_out(const io *c)
{
    uint32_t count = 0, fp = c->l->streams;
    for (uint32_t i = nfile(c); i; --i, fp += 0x18u) {
        uint32_t level = r32(c, fp + F_LEVEL);
        if (s32(level) >= 0) continue;
        uint32_t n = r32(c, fp + F_BSIZE) + level + 1u;
        w32(c, fp + F_LEVEL, level - n);
        uint32_t buffer = r32(c, fp + F_BUFFER);
        w32(c, fp + F_CURP, buffer);
        source s = {buffer, 0};
        if (io_write(c, f_fd(c, fp), &s, n) != n && !(f_flags(c, fp) & 0x200u)) f_set_flags(c, fp, f_flags(c, fp) | 0x10u);
        ++count;
    }
    return count;
}

/* fflush (4055f0). */
static uint32_t f_flush(const io *c, uint32_t fp)
{
    if (!fp) { flush_out(c); return 0; }
    if (!token_ok(c, fp)) return 0xffffffffu;
    uint32_t level = r32(c, fp + F_LEVEL);
    if (s32(level) >= 0) {
        if ((f_flags(c, fp) & 8u) || r32(c, fp + F_CURP) == fp + F_HOLD) {
            w32(c, fp + F_LEVEL, 0);
            if (r32(c, fp + F_CURP) == fp + F_HOLD) w32(c, fp + F_CURP, r32(c, fp + F_BUFFER));
        }
        return 0;
    }
    uint32_t n = r32(c, fp + F_BSIZE) + level + 1u;
    w32(c, fp + F_LEVEL, level - n);
    uint32_t buffer = r32(c, fp + F_BUFFER);
    w32(c, fp + F_CURP, buffer);
    source s = {buffer, 0};
    if (io_write(c, f_fd(c, fp), &s, n) != n && !(f_flags(c, fp) & 0x200u)) {
        f_set_flags(c, fp, f_flags(c, fp) | 0x10u);
        return 0xffffffffu;
    }
    return 0;
}

/* Terminal streams' output before a terminal read (4060b0): 50 entries. */
static void flush_terminals(const io *c)
{
    uint32_t fp = c->l->streams;
    for (unsigned i = 0; i < 50u; ++i, fp += 0x18u)
        if ((f_flags(c, fp) & 0x300u) == 0x300u) f_flush(c, fp);
}

/* Buffer refill (4060e4). */
static uint32_t f_fill(const io *c, uint32_t fp)
{
    if (f_flags(c, fp) & 0x200u) flush_terminals(c);
    uint32_t buffer = r32(c, fp + F_BUFFER);
    w32(c, fp + F_CURP, buffer);
    uint32_t got = io_read(c, f_fd(c, fp), buffer, r32(c, fp + F_BSIZE), 1);
    w32(c, fp + F_LEVEL, got);
    if (s32(got) > 0) { f_set_flags(c, fp, f_flags(c, fp) & ~0x20u); return 0; }
    if (got == 0) f_set_flags(c, fp, (f_flags(c, fp) & 0xfe7fu) | 0x20u);
    else { w32(c, fp + F_LEVEL, 0); f_set_flags(c, fp, f_flags(c, fp) | 0x10u); }
    return 0xffffffffu;
}

static uint32_t take_byte(const io *c, uint32_t fp)
{
    w32(c, fp + F_LEVEL, r32(c, fp + F_LEVEL) - 1u);
    uint32_t p = r32(c, fp + F_CURP);
    w32(c, fp + F_CURP, p + 1u);
    return r8(c, p);
}

/* fgetc (40615c). */
static uint32_t f_getc(const io *c, uint32_t fp)
{
    if (!fp) return 0xffffffffu;
    uint32_t level = r32(c, fp + F_LEVEL);
    if (s32(level) > 0) return take_byte(c, fp);
    uint32_t flags = f_flags(c, fp);
    if (s32(level) < 0 || (flags & 0x110u) || !(flags & 1u)) { f_set_flags(c, fp, flags | 0x10u); return 0xffffffffu; }
    f_set_flags(c, fp, flags | 0x80u);
    if (r32(c, fp + F_BSIZE)) {
        if (f_fill(c, fp)) return 0xffffffffu;
        return take_byte(c, fp);
    }
    if (f_flags(c, fp) & 0x200u) flush_terminals(c);
    uint32_t got = io_read(c, f_fd(c, fp), c->l->getc_byte, 1, 1);
    if (got == 0) {
        if (io_eof(c, f_fd(c, fp)) == 1u) f_set_flags(c, fp, (f_flags(c, fp) & 0xfe7fu) | 0x20u);
        else f_set_flags(c, fp, f_flags(c, fp) | 0x10u);
        return 0xffffffffu;
    }
    f_set_flags(c, fp, f_flags(c, fp) & ~0x20u);
    return r8(c, c->l->getc_byte);
}

/* ungetc (4074cc). */
static uint32_t f_ungetc(const io *c, uint32_t ch, uint32_t fp)
{
    if (ch == 0xffffffffu || s32(r32(c, fp + F_LEVEL)) < 0) return 0xffffffffu;
    f_set_flags(c, fp, f_flags(c, fp) & ~0x20u);
    uint32_t level = r32(c, fp + F_LEVEL) + 1u;
    w32(c, fp + F_LEVEL, level);
    if (s32(level - 1u) <= 0) w32(c, fp + F_CURP, fp + F_HOLD);
    else w32(c, fp + F_CURP, r32(c, fp + F_CURP) - 1u);
    w8(c, r32(c, fp + F_CURP), ch);
    return ch;
}

/* fputc (40622c). */
static uint32_t f_putc(const io *c, uint32_t ch_in, uint32_t fp)
{
    uint8_t ch = (uint8_t)ch_in;
    uint32_t level = r32(c, fp + F_LEVEL);
    if (s32(level) < -1) {
        w32(c, fp + F_LEVEL, level + 1u);
        uint32_t p = r32(c, fp + F_CURP);
        w32(c, fp + F_CURP, p + 1u);
        w8(c, p, ch);
        if ((f_flags(c, fp) & 8u) && (ch == '\n' || ch == '\r') && f_flush(c, fp)) return 0xffffffffu;
        return ch;
    }
    uint32_t flags = f_flags(c, fp);
    if ((flags & 0x90u) || !(flags & 2u)) { f_set_flags(c, fp, flags | 0x10u); return 0xffffffffu; }
    f_set_flags(c, fp, flags | 0x100u);
    if (r32(c, fp + F_BSIZE)) {
        if (r32(c, fp + F_LEVEL) && f_flush(c, fp)) return 0xffffffffu;
        w32(c, fp + F_LEVEL, 0u - r32(c, fp + F_BSIZE));
        uint32_t p = r32(c, fp + F_CURP);
        w32(c, fp + F_CURP, p + 1u);
        w8(c, p, ch);
        if ((f_flags(c, fp) & 8u) && (ch == '\n' || ch == '\r') && f_flush(c, fp)) return 0xffffffffu;
        return ch;
    }
    source s = {0, &ch};
    if (io_write(c, f_fd(c, fp), &s, 1) != 1u && !(f_flags(c, fp) & 0x200u)) {
        f_set_flags(c, fp, f_flags(c, fp) | 0x10u);
        return 0xffffffffu;
    }
    return ch;
}

/* setvbuf's core (405384). */
static uint32_t set_buffer(const io *c, uint32_t fp, uint32_t buffer, uint32_t mode, uint32_t size)
{
    if (f_flags(c, fp) & 4u) c->env->free(c->env->context, r32(c, fp + F_BUFFER));
    f_set_flags(c, fp, f_flags(c, fp) & 0xfff3u);
    w32(c, fp + F_BSIZE, 0);
    w32(c, fp + F_BUFFER, fp + F_HOLD);
    w32(c, fp + F_CURP, fp + F_HOLD);
    if (mode != 2u && size != 0) {
        w32(c, c->l->exit_buffers, c->l->exit_buffers_value);
        if (!buffer) {
            buffer = c->env->malloc(c->env->context, size, fp + F_BUFFER);
            if (!buffer) return 0xffffffffu;
            f_set_flags(c, fp, f_flags(c, fp) | 4u);
        }
        w32(c, fp + F_CURP, buffer);
        w32(c, fp + F_BUFFER, buffer);
        w32(c, fp + F_BSIZE, size);
        if (mode == 1u) f_set_flags(c, fp, f_flags(c, fp) | 8u);
    }
    return 0;
}
static uint32_t f_setvbuf(const io *c, uint32_t fp, uint32_t buffer, uint32_t mode, uint32_t size)
{
    if (!token_ok(c, fp) || s32(mode) > 2 || size > 0x7fffffffu) return 0xffffffffu;
    if (r32(c, fp + F_LEVEL)) f_seek(c, fp, 0, 1);
    return set_buffer(c, fp, buffer, mode, size);
}

/* fclose (40555c); the caller has declined a tmpfile() stream. */
static uint32_t f_close(const io *c, uint32_t fp)
{
    if (!fp || !token_ok(c, fp)) return 0xffffffffu;
    if (r32(c, fp + F_BSIZE)) {
        if (s32(r32(c, fp + F_LEVEL)) < 0 && f_flush(c, fp)) return 0xffffffffu;
        if (f_flags(c, fp) & 4u) c->env->free(c->env->context, r32(c, fp + F_BUFFER));
    }
    uint32_t result = io_close(c, f_fd(c, fp));
    f_set_flags(c, fp, 0);
    w32(c, fp + F_BSIZE, 0);
    w32(c, fp + F_LEVEL, 0);
    w8(c, fp + F_FD, 0xffu);
    return result;
}

/* fopen's mode parser (4058a4); 0 for an invalid mode. */
static uint32_t parse_mode(const io *c, uint32_t mode, uint32_t *oflag, uint32_t *pmode)
{
    uint32_t of, pm = 0, f;
    uint8_t ch = r8(c, mode++);
    if (ch == 'r') { of = 0; f = 1; }
    else if (ch == 'w') { of = 0x301u; pm = 0x80u; f = 2; }
    else if (ch == 'a') { of = 0x901u; pm = 0x80u; f = 2; }
    else return 0;
    ch = r8(c, mode++);
    if (ch == '+' || (r8(c, mode) == '+' && (ch == 't' || ch == 'b'))) {
        if (ch == '+') ch = r8(c, mode);
        of = (of & ~1u) | 2u;
        pm = 0x180u;
        f = 3;
    }
    if (ch == 't') of |= 0x4000u;
    else if (ch == 'b') { of |= 0x8000u; f |= 0x40u; }
    else {
        if (ch != '+' && ch != 0) return 0;
        of |= r32(c, r32(c, c->l->fmode_pointer)) & 0xc000u;
        if (of & 0x8000u) f |= 0x40u;
    }
    w32(c, c->l->exit_files, c->l->exit_files_value);
    *oflag = of;
    *pmode = pm;
    return f;
}

static uint32_t f_open(const io *c, uint32_t path, uint32_t mode)
{
    /* A free FILE (405a18): fd < 0; the entry one past the table is tested too. */
    uint32_t fp = c->l->streams, end = c->l->streams + (nfile(c) << 3) * 3u;
    for (;;) {
        if (s8(r8(c, fp + F_FD)) < 0) break;
        if (fp >= end) return 0;
        fp += 0x18u;
    }
    uint32_t oflag = 0, pmode = 0, f = parse_mode(c, mode, &oflag, &pmode);
    f_set_flags(c, fp, f);
    if (!(f & 0xffffu)) goto fail;
    if (s8(r8(c, fp + F_FD)) < 0) {
        uint32_t fd = io_open(c, path, oflag, 0, 1, pmode);
        w8(c, fp + F_FD, fd);
        if (s8(fd) < 0) goto fail;
    }
    if (r32(c, fd_flags_address(c, f_fd(c, fp))) & 0x2000u) f_set_flags(c, fp, f_flags(c, fp) | 0x200u);
    if (f_setvbuf(c, fp, 0, (f_flags(c, fp) & 0x200u) ? 1u : 0u, 0x200u)) {
        f_close(c, fp);
        return 0;
    }
    w16(c, fp + F_ISTEMP, 0);
    return fp;
fail:
    w8(c, fp + F_FD, 0xffu);
    f_set_flags(c, fp, 0);
    return 0;
}

/* Pending bytes of a stream in file terms (405d78). */
static uint32_t buffered_count(const io *c, uint32_t fp)
{
    uint32_t level = r32(c, fp + F_LEVEL);
    uint32_t count = s32(level) < 0 ? r32(c, fp + F_BSIZE) + level + 1u : level;
    if (f_flags(c, fp) & 0x40u) return count;
    uint32_t p = r32(c, fp + F_CURP), k = count;
    if (s32(r32(c, fp + F_LEVEL)) < 0) {
        while (k--) { --p; if (r8(c, p) == '\n') ++count; }
    } else {
        while (k--) { if (r8(c, p) == '\n') ++count; ++p; }
    }
    return count;
}

static uint32_t f_seek(const io *c, uint32_t fp, uint32_t offset, uint32_t whence)
{
    if (f_flush(c, fp)) return 0xffffffffu;
    if (whence == 1u && s32(r32(c, fp + F_LEVEL)) > 0) offset -= buffered_count(c, fp);
    f_set_flags(c, fp, f_flags(c, fp) & 0xfe5fu);
    w32(c, fp + F_LEVEL, 0);
    w32(c, fp + F_CURP, r32(c, fp + F_BUFFER));
    return io_lseek(c, f_fd(c, fp), offset, whence) == 0xffffffffu ? 0xffffffffu : 0u;
}

static uint32_t f_tell(const io *c, uint32_t fp)
{
    uint32_t fd = f_fd(c, fp);
    uint32_t at = io_lseek(c, fd, 0, 1);
    if (at == 0xffffffffu) return at;
    if (s32(r32(c, fp + F_LEVEL)) >= 0) return at - buffered_count(c, fp);
    if (r32(c, fd_flags_address(c, fd)) & 0x800u) {
        uint32_t saved = at;
        at = io_lseek(c, fd, 0, 2);
        if (at == 0xffffffffu) return at;
        if (io_lseek(c, fd, saved, 0) == 0xffffffffu) return 0xffffffffu;
    }
    return at + buffered_count(c, fp);
}

/* fread's byte mover (405bbc): returns the bytes not delivered. */
static uint32_t get_n(const io *c, uint32_t p, uint32_t left, uint32_t fp)
{
    if (!r32(c, fp + F_BSIZE)) {
        uint32_t ch = 0;
        while (left) {
            uint32_t level = r32(c, fp + F_LEVEL) - 1u;
            w32(c, fp + F_LEVEL, level);
            if (s32(level) < 0) {
                w32(c, fp + F_LEVEL, r32(c, fp + F_LEVEL) + 1u);   /* __fgetc 406148 */
                ch = f_getc(c, fp);
            } else {
                uint32_t q = r32(c, fp + F_CURP);
                w32(c, fp + F_CURP, q + 1u);
                ch = r8(c, q);
            }
            if (ch == 0xffffffffu) break;
            w8(c, p++, ch);
            --left;
        }
        if (ch == 0xffffffffu) f_set_flags(c, fp, f_flags(c, fp) | 0x20u);
        return left;
    }
    for (;;) {
        if (!left) return left;
        for (;;) {
            uint32_t level = r32(c, fp + F_LEVEL);
            if (level) {
                uint32_t k = level < left ? level : left;
                memcpy_g(c, p, r32(c, fp + F_CURP), k);
                w32(c, fp + F_CURP, r32(c, fp + F_CURP) + k);
                w32(c, fp + F_LEVEL, r32(c, fp + F_LEVEL) - k);
                if (left == k) return 0;
                p += k;
                left -= k;
            }
            uint32_t bsize = r32(c, fp + F_BSIZE);
            if (left >= bsize) {
                uint32_t chunk = left - left % bsize;
                uint32_t got = io_read(c, f_fd(c, fp), p, chunk, 0);
                if (got == left) return 0;
                if (got == 0xffffffffu) { f_set_flags(c, fp, f_flags(c, fp) | 0x10u); return left; }
                p += got;
                left -= got;
                if (chunk != got) {
                    if (!(f_flags(c, fp) & 0x40u) && got != 0) break;   /* back to the top */
                    f_set_flags(c, fp, f_flags(c, fp) | 0x20u);
                    return left;
                }
            }
            uint32_t ch = f_getc(c, fp);
            if (ch == 0xffffffffu) { f_set_flags(c, fp, f_flags(c, fp) | 0x20u); return left; }
            w8(c, p++, ch);
            --left;
            if (!left) return left;
        }
    }
}

/* fwrite's byte mover (405aa8): bytes written or 0. */
static uint32_t put_n(const io *c, uint32_t buffer, uint32_t n, uint32_t fp)
{
    if (f_flags(c, fp) & 8u) {
        for (uint32_t i = 0; i < n; ++i)
            if (f_putc(c, (uint32_t)s8(r8(c, buffer + i)), fp) == 0xffffffffu) return 0;
        return n;
    }
    uint32_t bsize = r32(c, fp + F_BSIZE);
    if (bsize && bsize >= n) {
        uint32_t level = r32(c, fp + F_LEVEL);
        if (s32(level + n) >= 0) {
            if (level == 0) w32(c, fp + F_LEVEL, 0xffffffffu - bsize);
            else if (f_flush(c, fp)) return 0;
        }
        memcpy_g(c, r32(c, fp + F_CURP), buffer, n);
        w32(c, fp + F_LEVEL, r32(c, fp + F_LEVEL) + n);
        w32(c, fp + F_CURP, r32(c, fp + F_CURP) + n);
        return n;
    }
    if (bsize && r32(c, fp + F_LEVEL) && f_flush(c, fp)) return 0;
    source s = {buffer, 0};
    uint32_t w = io_write(c, f_fd(c, fp), &s, n);
    return n > w ? 0u : n;
}

/* ------------------------------------------------------------------------
 * fscanf (40668c, 406c24). get = fgetc, unget = ungetc on the FILE. */
static int is_space_scan(const io *c, uint32_t ch) { return ch < 0x80u && r8(c, c->l->scan_class + ch) == 1u; }
static uint32_t scan_class_of(const io *c, uint8_t ch) { return (ch & 0x80u) ? 2u : (uint32_t)s8(r8(c, c->l->scan_class + ch)); }

/* The number reader (406c24). */
static uint32_t scan_number(const io *c, uint32_t fp, uint32_t base, uint32_t width, uint32_t *nread, uint32_t *status_out)
{
    unsigned state = 0;
    int negative = 0;
    uint32_t count = 0, status = 1, value = 0, digits = 0, ch;
    int32_t w = s32(width);
    for (;;) {
        ++count;
        ch = f_getc(c, fp);
        if (state == 0) {
            if (s32(ch) >= 0 && s32(ch) < 0x80 && (ch == ' ' || (ch >= 9u && ch <= 13u))) continue;
            state = 1;
        }
        if (--w < 0) break;
        if (state == 1) {
            state = 2;
            if (ch == '+' || ch == '-') { negative = ch == '-'; continue; }
        }
        if (state == 2) {
            state = 3;
            if (ch == '0') {
                digits = 1;
                if (base == 0 || base == 16u) {
                    ++count;
                    ch = f_getc(c, fp);
                    if (--w < 0) break;
                    if (ch == 'x' || ch == 'X') { base = 16; continue; }
                    if (base == 0) base = 8;
                }
            }
            if (base == 0) base = 10;
            else if (s32(base) < 1 || s32(base) > 36) break;
        }
        /* state 3: digits */
        uint32_t d;
        if (s32(ch) >= '0' && s32(ch) <= '9') d = ch - '0';
        else if (s32(ch) >= 'a' && s32(ch) <= 'z') d = ch - 0x57u;
        else if (s32(ch) >= 'A' && s32(ch) <= 'Z') d = ch - 0x37u;
        else break;
        if (s32(base) <= s32(d)) break;
        ++digits;
        uint32_t old = value;
        value = value * base + d;
        if ((value - d) / base == old) continue;
        status = 2;
        value = negative ? 0x80000000u : 0x7fffffffu;
        break;
    }
    if (status != 2) {
        f_ungetc(c, ch, fp);
        --count;
        if (negative) value = 0u - value;
    }
    if (!digits) status = ch == 0xffffffffu ? 0xffffffffu : 0u;
    *status_out = status;
    *nread += count;
    return value;
}

/* A dry pass over the format: 0 when the scanner would reach a float
 * conversion or a number read with an uninitialised base. */
static int scan_supported(const io *c, uint32_t fmt)
{
    for (;;) {
        uint8_t ch = r8(c, fmt++);
        if (!ch) return 1;
        if (ch != '%') continue;
        ch = r8(c, fmt++);
        if (ch == '%') continue;
        for (;;) {
            uint32_t k = scan_class_of(c, ch);
            if (k == 0 || k == 1 || k == 3 || k == 10) return 0;
            if (k == 2) return 1;              /* the scanner stops here */
            if (k == 4 || k == 5 || k == 11 || k == 12 || k == 13 || k == 20 || k == 21) { ch = r8(c, fmt++); continue; }
            if (k == 16) {
                /* Scan set: the parse below, without effects. */
                uint8_t b = r8(c, fmt++);
                if (b == '^') b = r8(c, fmt++);
                for (;;) {
                    uint8_t first = b;
                    if (!b) return 1;
                    b = r8(c, fmt++);
                    if (b == ']') break;
                    if (b == '-' && s8(first) < s8(r8(c, fmt)) && r8(c, fmt) != ']') b = r8(c, fmt++);
                }
            }
            break;
        }
    }
}

int es2_io_scan_supported(const es2_io_env *env, const es2_io_layout *layout, uint32_t format)
{
    io context = {env, layout};
    return scan_supported(&context, format);
}

static uint32_t f_scan(const io *c, uint32_t fp, uint32_t fmt, uint32_t ap)
{
    uint32_t count = 0, nread = 0, base = 0;
    uint8_t spec = 0;
    for (;;) {
        uint8_t fc = r8(c, fmt++);
        spec = fc;
        if (!fc) return count;
        if (fc == '%') {
            spec = r8(c, fmt++);
            if (spec != '%') goto conversion;
        }
        /* A literal byte or white space. */
        ++nread;
        uint32_t ch = f_getc(c, fp);
        if ((ch & 0xffu) == 0xffu) goto input_end;
        if (!(spec & 0x80u) && scan_class_of(c, spec) == 1u) {
            while (is_space_scan(c, ch & 0xffu) && !(ch & 0x80u)) {
                ++nread;
                ch = f_getc(c, fp);
                if ((ch & 0xffu) == 0xffu) goto input_end;
            }
            f_ungetc(c, (uint32_t)s8(ch), fp);
            --nread;
            continue;
        }
        if ((ch & 0xffu) == spec) continue;
        f_ungetc(c, (uint32_t)s8(ch), fp);
        return count;
    conversion:;
        uint8_t flags = 0x20;
        int32_t width = -1;
        for (;;) {
            uint32_t k = scan_class_of(c, spec);
            if (k == 4) { flags |= 1u; spec = r8(c, fmt++); continue; }
            if (k == 5) { width = width < 0 ? (int32_t)spec - '0' : (int32_t)((uint32_t)width * 10u + spec - '0'); spec = r8(c, fmt++); continue; }
            if (k == 11) { flags |= 8u; spec = r8(c, fmt++); continue; }
            if (k == 12) { flags |= 2u; spec = r8(c, fmt++); continue; }
            if (k == 13) { flags |= 4u; spec = r8(c, fmt++); continue; }
            if (k == 20) { flags &= (uint8_t)~0x20u; spec = r8(c, fmt++); continue; }
            if (k == 21) { flags |= 0x20u; spec = r8(c, fmt++); continue; }
            if (k == 2) return count;
            uint32_t value = 0, status = 0;
            if (k == 7 || k == 8 || k == 9 || k == 14 || k == 18 || k == 17) {
                if (k == 17) value = nread;
                else {
                    base = k == 7 || k == 8 ? 10u : k == 9 ? 0u : k == 14 ? 8u : 16u;
                    value = scan_number(c, fp, base, (uint32_t)width & 0x7fffu, &nread, &status);
                    if (s32(status) < 0) goto input_end;
                    if (status == 0) return count;
                }
                if (spec >= 'A' && spec <= 'Z' && spec != 'X') flags |= 4u;
                if (flags & 1u) break;
                ap += 4u;
                uint32_t dest = r32(c, ap - 4u);
                if (flags & 4u) w32(c, dest, value);
                else if (flags & 2u) w16(c, dest, value);
                else w32(c, dest, value);
                if (spec != 'n') ++count;
                break;
            }
            if (k == 19) {   /* p: hex, at most 8 digits */
                value = scan_number(c, fp, 16, 8, &nread, &status);
                if (s32(status) <= 0) return count;
                if (flags & 1u) break;
                ap += 4u;
                w32(c, r32(c, ap - 4u), value);
                ++count;
                break;
            }
            if (k == 15) {   /* s */
                uint32_t ch;
                do {
                    ++nread;
                    ch = f_getc(c, fp);
                    if ((ch & 0xffu) == 0xffu) goto input_end;
                } while (!(ch & 0x80u) && is_space_scan(c, ch & 0xffu));
                uint32_t dest = 0;
                if (!(flags & 1u)) { ap += 4u; dest = r32(c, ap - 4u); ++count; }
                if (width == -1) width = 0x7fff;
                for (;;) {
                    if (!(flags & 1u)) w8(c, dest++, ch);
                    ++nread;
                    ch = f_getc(c, fp);
                    uint8_t b = (uint8_t)ch;
                    if (b == 0xffu || b == 0 || (!(b & 0x80u) && is_space_scan(c, b))) break;
                    if (--width <= 0) break;
                }
                f_ungetc(c, (uint32_t)s8(ch), fp);
                --nread;
                if (!(flags & 1u)) w8(c, dest, 0);
                break;
            }
            if (k == 6) {    /* c */
                uint32_t dest = 0;
                if (!(flags & 1u)) { ap += 4u; dest = r32(c, ap - 4u); }
                if (width < 0) width = 1;
                while (width) {
                    ++nread;
                    uint32_t ch = f_getc(c, fp);
                    if ((ch & 0xffu) == 0xffu) goto input_end;
                    if (!(flags & 1u)) w8(c, dest++, ch);
                    if (--width <= 0) break;
                }
                if (flags & 1u) break;
                ++count;
                break;
            }
            /* k == 16: scan set. */
            uint8_t set[32];
            memset(set, 0, sizeof(set));
            int negate = 0;
            uint8_t b = r8(c, fmt++);
            if (b == '^') { negate = 1; b = r8(c, fmt++); }
            for (;;) {
                spec = b;
                if (!b) return count;
                set[(uint32_t)(s8(b) >> 3) & 0x1fu] |= (uint8_t)(1u << (b & 7u));
                b = r8(c, fmt++);
                if (b == ']') break;
                if (b == '-' && s8(spec) < s8(r8(c, fmt)) && r8(c, fmt) != ']') {
                    b = r8(c, fmt++);
                    for (;;) {
                        ++spec;
                        if (!(s8(b) > s8(spec))) break;
                        set[(uint32_t)(s8(spec) >> 3) & 0x1fu] |= (uint8_t)(1u << (spec & 7u));
                    }
                }
            }
            if (width == -1) width = 0x7fff;
            uint32_t dest = 0, matched = 0;
            if (!(flags & 1u)) { ap += 4u; dest = r32(c, ap - 4u); }
            uint8_t last = b;
            for (;;) {
                if (--width < 0) break;
                ++nread;
                uint32_t ch = f_getc(c, fp);
                last = (uint8_t)ch;
                if (last == 0xffu) break;
                int in_set = (set[(uint32_t)(s8(last) >> 3) & 0x1fu] >> (last & 7u)) & 1u;
                if (in_set == negate) break;
                ++matched;
                if (!(flags & 1u)) w8(c, dest++, last);
            }
            if (width >= 0) { f_ungetc(c, (uint32_t)s8(last), fp); --nread; }
            if (matched && !(flags & 1u)) { w8(c, dest, 0); ++count; }
            if (last == 0xffu) goto input_end;
            break;
        }
        continue;
    }
input_end:
    f_ungetc(c, 0xffffffffu, fp);
    return count ? count : 0xffffffffu;
}

/* ------------------------------------------------------------------------
 * findfirst / findnext (4086ec, 408740, 408784). ffblk: +0 node, +4 size,
 * +8 attributes, +0xc time (word), +0xe date (word), +0x10 name. Node (0x14
 * bytes, CW3220 heap): +0 ffblk, +4 find handle, +8 attribute mask, +0xc
 * path pointer, +0x10 next. */
static uint32_t find_next(const io *c, uint32_t ffblk)
{
    uint32_t node = r32(c, ffblk);
    es2_io_os call;
    for (;;) {
        memset(&call, 0, sizeof(call));
        uint32_t ok;
        if (r32(c, node + 4u) == 0xffffffffu) {
            call.function = ES2_IO_OS_FIND_FIRST;
            call.arg[0] = r32(c, node + 0xcu);
            os_call(c, &call);
            w32(c, node + 4u, call.result);
            ok = call.result != 0xffffffffu;
        } else {
            call.function = ES2_IO_OS_FIND_NEXT;
            call.arg[0] = r32(c, node + 4u);
            os_call(c, &call);
            ok = call.result != 0;
        }
        if (!ok) return nt_error_code(c);
        uint32_t attributes = (uint32_t)call.out[0] | (uint32_t)call.out[1] << 8 | (uint32_t)call.out[2] << 16 | (uint32_t)call.out[3] << 24;
        if ((r8(c, node + 8u) & 8u) && (attributes & 8u)) break;
        if (!(attributes & 0x16u)) break;
        if ((r32(c, node + 8u) & attributes) & 0x16u) break;
    }
    w32(c, ffblk + 8u, (uint32_t)call.out[0] | (uint32_t)call.out[1] << 8 | (uint32_t)call.out[2] << 16 | (uint32_t)call.out[3] << 24);
    es2_io_os local;
    memset(&local, 0, sizeof(local));
    local.function = ES2_IO_OS_FILE_TIME_TO_LOCAL;
    /* ftLastWriteTime at +0x14 of WIN32_FIND_DATAA. */
    local.arg[0] = (uint32_t)call.out[0x14] | (uint32_t)call.out[0x15] << 8 | (uint32_t)call.out[0x16] << 16 | (uint32_t)call.out[0x17] << 24;
    local.arg[1] = (uint32_t)call.out[0x18] | (uint32_t)call.out[0x19] << 8 | (uint32_t)call.out[0x1a] << 16 | (uint32_t)call.out[0x1b] << 24;
    os_call(c, &local);
    es2_io_os dos;
    memset(&dos, 0, sizeof(dos));
    dos.function = ES2_IO_OS_FILE_TIME_TO_DOS;
    dos.arg[0] = (uint32_t)local.out[0] | (uint32_t)local.out[1] << 8 | (uint32_t)local.out[2] << 16 | (uint32_t)local.out[3] << 24;
    dos.arg[1] = (uint32_t)local.out[4] | (uint32_t)local.out[5] << 8 | (uint32_t)local.out[6] << 16 | (uint32_t)local.out[7] << 24;
    dos.arg[2] = ffblk + 0xeu;
    dos.arg[3] = ffblk + 0xcu;
    os_call(c, &dos);
    /* nFileSizeLow at +0x20, cFileName at +0x2c. */
    w32(c, ffblk + 4u, (uint32_t)call.out[0x20] | (uint32_t)call.out[0x21] << 8 | (uint32_t)call.out[0x22] << 16 | (uint32_t)call.out[0x23] << 24);
    for (uint32_t i = 0; 0x2cu + i < ES2_IO_FIND_DATA; ++i) {
        w8(c, ffblk + 0x10u + i, call.out[0x2c + i]);
        if (!call.out[0x2c + i]) break;
    }
    return 0;
}
static uint32_t find_first(const io *c, uint32_t path, uint32_t attributes, uint32_t ffblk)
{
    uint32_t node = r32(c, c->l->find_list);
    while (node && r32(c, node) != ffblk) node = r32(c, node + 0x10u);
    if (node) os1(c, ES2_IO_OS_FIND_CLOSE, r32(c, node + 4u));
    else {
        node = c->env->malloc(c->env->context, 0x14u, ffblk);
        if (node) {
            w32(c, node, ffblk);
            w32(c, node + 0x10u, r32(c, c->l->find_list));
            w32(c, c->l->find_list, node);
        }
    }
    w32(c, ffblk, node);
    if (!node) { io_error(c, 8); return 8; }
    w32(c, node + 4u, 0xffffffffu);
    w32(c, node + 8u, attributes);
    w32(c, node + 0xcu, path);
    return find_next(c, ffblk);
}

/* ------------------------------------------------------------------------
 * time (41eb64, 41e50c, 41e3f8). */
static const int32_t month_days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
static const uint32_t month_start[13] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365};

/* Summer time (41e3f8): hour, day of the month (0-based) or of the year
 * when month is 0, month 1..12, years since 1970. */
static int is_summer(uint32_t hour, uint32_t day, uint32_t month, uint32_t year)
{
    uint32_t yday = day;
    if (month == 0) {
        uint32_t t = day;
        if (day >= 59u && ((year + 70u) & 3u) == 0) --t;
        month = 0;
        while (month_start[month] <= t) ++month;
    } else {
        yday = day + month_start[month - 1u];
        if (month > 2u && ((year + 70u) & 3u) == 0) ++yday;
    }
    if (month < 4u || month > 10u) return 0;
    if (month > 4u && month < 10u) return 1;
    uint32_t limit = (year > 16u && month == 4u) ? month_start[month - 1u] + 7u : month_start[month];
    if ((year + 70u) & 3u) --limit;
    uint32_t total = ((year + 1u) >> 2) + limit + 365u * year + 4u;
    limit -= total % 7u;
    if (month == 4u) {
        if (limit < yday) return 1;
        if (limit > yday) return 0;
        return hour >= 2u;
    }
    if (limit > yday) return 1;
    if (limit < yday) return 0;
    return hour <= 1u;
}

static uint32_t total_seconds(const io *c, int32_t year, int32_t month, int32_t day, int32_t hour, int32_t minute, int32_t second)
{
    if (year < 70 || year > 138) return 0xffffffffu;
    minute += second / 60; second %= 60;
    hour += minute / 60; minute %= 60;
    day += hour / 24; hour %= 24;
    year += month / 12; month %= 12;
    int32_t index = month;
    while (day >= month_days[index]) {
        if ((year & 3) == 0 && month == 1) {
            if (day <= 28) break;
            day -= 29; ++month; ++index;
        } else {
            day -= month_days[index]; ++month; ++index;
        }
        if (month >= 12) { month -= 12; index -= 12; ++year; }
    }
    int32_t y = year - 70;
    int32_t leaps = (y + 2) / 4;
    if (((y + 70) & 3) == 0 && month < 2) --leaps;
    uint32_t days = (uint32_t)leaps + 365u * (uint32_t)y + month_start[month] + (uint32_t)day;
    uint32_t total = days * 86400u + (uint32_t)hour * 3600u + (uint32_t)minute * 60u + (uint32_t)second + r32(c, c->l->time_zone);
    if (r32(c, c->l->summer_time) && is_summer((uint32_t)hour, (uint32_t)day, (uint32_t)month + 1u, (uint32_t)y)) total -= 3600u;
    return s32(total) > 0 ? total : 0xffffffffu;
}

/* ------------------------------------------------------------------------
 * printf's count: the modelled formatter over a scratch window. */
#define SCRATCH_BASE 0xfff00000u
#define SCRATCH_SLOTS 16u
#define SCRATCH_SIZE 0x10000u
typedef struct scratch { const io *c; uint8_t bytes[SCRATCH_SLOTS + SCRATCH_SIZE]; int overflow; } scratch;
static uint8_t scratch_read(void *context, uint32_t a)
{
    scratch *s = context;
    if (a - SCRATCH_BASE < sizeof(s->bytes)) return s->bytes[a - SCRATCH_BASE];
    return r8(s->c, a);
}
static void scratch_write(void *context, uint32_t a, uint8_t v)
{
    scratch *s = context;
    if (a - SCRATCH_BASE < sizeof(s->bytes)) s->bytes[a - SCRATCH_BASE] = v;
    else s->overflow = 1;
}

/* ------------------------------------------------------------------------
 * Entry. */
static const char *const io_names[ES2_IO_FUNCTION_COUNT] = {
    "_open", "_close", "_read", "_write", "_lseek", "_filelength", "_fopen", "_fclose", "_fread", "_fwrite", "_fseek",
    "_ftell", "_rewind", "_fscanf", "_flushall", "_findfirst", "_findnext", "_time", "_printf", "__fputc", "_fputs",
};
const char *es2_io_name(es2_io_function function) { return (unsigned)function < ES2_IO_FUNCTION_COUNT ? io_names[function] : "?"; }
int es2_io_is_stub(es2_io_function function) { return function == ES2_IO_PRINTF || function == ES2_IO_FPUTC || function == ES2_IO_FPUTS; }

int es2_io_call(es2_io_function function, const es2_io_env *env, const es2_io_layout *layout, uint32_t args, uint32_t *eax)
{
    io context = {env, layout};
    const io *c = &context;
    uint32_t a0 = r32(c, args), a1, a2, a3;
    switch (function) {
    case ES2_IO_OPEN:
        a1 = r32(c, args + 4u);
        *eax = io_open(c, a0, a1, args + 8u, 0, 0);
        return ES2_IO_DONE;
    case ES2_IO_CLOSE: *eax = io_close(c, a0); return ES2_IO_DONE;
    case ES2_IO_READ: *eax = io_read(c, a0, r32(c, args + 4u), r32(c, args + 8u), 0); return ES2_IO_DONE;
    case ES2_IO_WRITE: {
        source s = {r32(c, args + 4u), 0};
        *eax = io_write(c, a0, &s, r32(c, args + 8u));
        return ES2_IO_DONE;
    }
    case ES2_IO_LSEEK: *eax = io_lseek(c, a0, r32(c, args + 4u), r32(c, args + 8u)); return ES2_IO_DONE;
    case ES2_IO_FILELENGTH: *eax = io_filelength(c, a0); return ES2_IO_DONE;
    case ES2_IO_FOPEN: *eax = f_open(c, a0, r32(c, args + 4u)); return ES2_IO_DONE;
    case ES2_IO_FCLOSE:
        if (a0 && token_ok(c, a0) && r16(c, a0 + F_ISTEMP)) return ES2_IO_UNSUPPORTED;
        *eax = f_close(c, a0);
        return ES2_IO_DONE;
    case ES2_IO_FREAD:
        a1 = r32(c, args + 4u); a2 = r32(c, args + 8u); a3 = r32(c, args + 12u);
        if (!a1) { *eax = 0; return ES2_IO_DONE; }
        if (!r32(c, a3 + F_BSIZE) && a1 * a2 == 0) return ES2_IO_UNSUPPORTED;
        *eax = (a1 * a2 - get_n(c, a0, a1 * a2, a3)) / a1;
        return ES2_IO_DONE;
    case ES2_IO_FWRITE:
        a1 = r32(c, args + 4u); a2 = r32(c, args + 8u); a3 = r32(c, args + 12u);
        if (!a1) { *eax = a2; return ES2_IO_DONE; }
        *eax = put_n(c, a0, a1 * a2, a3) / a1;
        return ES2_IO_DONE;
    case ES2_IO_FSEEK: *eax = f_seek(c, a0, r32(c, args + 4u), r32(c, args + 8u)); return ES2_IO_DONE;
    case ES2_IO_FTELL: *eax = f_tell(c, a0); return ES2_IO_DONE;
    case ES2_IO_REWIND:
        *eax = f_seek(c, a0, 0, 0);
        if (*eax == 0) f_set_flags(c, a0, f_flags(c, a0) & ~0x10u);
        return ES2_IO_DONE;
    case ES2_IO_FSCANF:
        a1 = r32(c, args + 4u);
        if (!scan_supported(c, a1)) return ES2_IO_UNSUPPORTED;
        *eax = f_scan(c, a0, a1, args + 8u);
        return ES2_IO_DONE;
    case ES2_IO_FLUSHALL: {
        uint32_t count = 0, fp = layout->streams;
        for (uint32_t i = nfile(c); i; --i, fp += 0x18u)
            if ((f_flags(c, fp) & 3u) && r32(c, fp + F_LEVEL)) { f_flush(c, fp); ++count; }
        *eax = count;
        return ES2_IO_DONE;
    }
    case ES2_IO_FINDFIRST:
        *eax = find_first(c, a0, r32(c, args + 8u), r32(c, args + 4u)) ? 0xffffffffu : 0u;
        return ES2_IO_DONE;
    case ES2_IO_FINDNEXT: *eax = find_next(c, a0) ? 0xffffffffu : 0u; return ES2_IO_DONE;
    case ES2_IO_TIME: {
        es2_io_os call;
        memset(&call, 0, sizeof(call));
        call.function = ES2_IO_OS_GET_LOCAL_TIME;
        os_call(c, &call);
        uint32_t year = (uint32_t)call.out[0] | (uint32_t)call.out[1] << 8, month = (uint32_t)call.out[2] | (uint32_t)call.out[3] << 8;
        uint32_t day = (uint32_t)call.out[6] | (uint32_t)call.out[7] << 8, hour = (uint32_t)call.out[8] | (uint32_t)call.out[9] << 8;
        uint32_t minute = (uint32_t)call.out[10] | (uint32_t)call.out[11] << 8, second = (uint32_t)call.out[12] | (uint32_t)call.out[13] << 8;
        if (!month || !day) return ES2_IO_UNSUPPORTED;   /* not a SYSTEMTIME Windows returns */
        uint32_t value = total_seconds(c, (int32_t)year - 1900, (int32_t)month - 1, (int32_t)day - 1, (int32_t)hour, (int32_t)minute, (int32_t)second);
        if (a0) w32(c, a0, value);
        *eax = value;
        return ES2_IO_DONE;
    }
    case ES2_IO_PRINTF: {
        static scratch s;
        s.c = c; s.overflow = 0;
        /* vsprintf(dest, format, &first vararg): its slots and dest in the scratch window. */
        uint32_t v[3] = {SCRATCH_BASE + SCRATCH_SLOTS, a0, args + 4u};
        for (unsigned i = 0; i < 12u; ++i) s.bytes[i] = (uint8_t)(v[i / 4u] >> (8u * (i % 4u)));
        es2_rtl_memory memory = {&s, scratch_read, scratch_write};
        es2_rtl_regs regs = {0, 0, 0};
        if (es2_rtl_call(ES2_RTL_VSPRINTF, &memory, SCRATCH_BASE, &regs) != ES2_RTL_DONE || s.overflow) return ES2_IO_UNSUPPORTED;
        *eax = regs.eax;
        return ES2_IO_DONE;
    }
    case ES2_IO_FPUTC: *eax = a0 & 0xffu; return ES2_IO_DONE;
    case ES2_IO_FPUTS: {
        if (!a0) { *eax = 0; return ES2_IO_DONE; }
        uint32_t n = 0;
        while (r8(c, a0 + n)) ++n;
        *eax = n ? r8(c, a0 + n - 1u) : 0u;
        return ES2_IO_DONE;
    }
    default: break;
    }
    return ES2_IO_UNSUPPORTED;
}

/* ------------------------------------------------------------------------
 * Shadow replay. */
void es2_io_replay_init(es2_io_replay *r, uint32_t *keys, uint32_t *stamp, uint8_t *values, uint32_t *written,
                        uint32_t slot_bits, uint32_t limit, uint8_t *snapshot, uint32_t snapshot_capacity)
{
    memset(r, 0, sizeof(*r));
    r->keys = keys; r->stamp = stamp; r->values = values; r->written = written;
    r->slot_bits = slot_bits; r->limit = limit;
    r->snapshot = snapshot; r->snapshot_capacity = snapshot_capacity;
    memset(stamp, 0, sizeof(uint32_t) << slot_bits);
    r->generation = 1;
}
void es2_io_replay_reset(es2_io_replay *r)
{
    if (++r->generation == 0) { memset(r->stamp, 0, sizeof(uint32_t) << r->slot_bits); r->generation = 1; }
    r->count = 0;
    r->region_count = 0;
    r->snapshot_used = 0;
    r->what = 0;
    r->where = r->model_value = r->real_value = 0;
    r->skip = 0;
    r->next = 0;
}
int es2_io_replay_track(es2_io_replay *r, uint32_t start, uint32_t size, uint8_t (*read8)(void *, uint32_t), void *context)
{
    if (!size) return 1;
    for (uint32_t i = 0; i < r->region_count; ++i)
        if (start - r->region_start[i] < r->region_size[i] && size <= r->region_size[i] - (start - r->region_start[i])) return 1;
    if (r->region_count >= 64u || size > r->snapshot_capacity - r->snapshot_used) return 0;
    r->region_start[r->region_count] = start;
    r->region_size[r->region_count] = size;
    r->region_offset[r->region_count] = r->snapshot_used;
    r->region_freed[r->region_count] = 0;
    for (uint32_t i = 0; i < size; ++i) r->snapshot[r->snapshot_used + i] = read8(context, start + i);
    r->snapshot_used += size;
    ++r->region_count;
    return 1;
}
int es2_io_replay_track_input(es2_io_replay *r, uint32_t start, uint32_t size, uint8_t (*read8)(void *, uint32_t), void *context)
{
    uint32_t before = r->region_count;
    if (!es2_io_replay_track(r, start, size, read8, context)) return 0;
    if (r->region_count > before) r->region_freed[before] = 1;
    return 1;
}
static uint32_t live32(uint8_t (*read8)(void *, uint32_t), void *context, uint32_t a)
{
    return (uint32_t)read8(context, a) | (uint32_t)read8(context, a + 1u) << 8 | (uint32_t)read8(context, a + 2u) << 16 |
           (uint32_t)read8(context, a + 3u) << 24;
}
int es2_io_replay_track_call(es2_io_replay *r, const es2_io_layout *l, uint32_t fp, uint32_t ffblk,
                             uint8_t (*read8)(void *, uint32_t), void *context)
{
    int ok = es2_io_replay_track(r, l->nfile, 4, read8, context) &&
             es2_io_replay_track(r, l->openfd, 50u * 4u, read8, context) &&
             es2_io_replay_track(r, l->handles, 50u * 4u, read8, context) &&
             es2_io_replay_track(r, l->streams, 50u * 0x18u, read8, context) &&
             es2_io_replay_track(r, l->errno_word, 4, read8, context) &&
             es2_io_replay_track(r, l->doserrno, 4, read8, context) &&
             es2_io_replay_track(r, l->exit_buffers, 8, read8, context) &&
             es2_io_replay_track(r, l->getc_byte, 1, read8, context) &&
             es2_io_replay_track(r, l->find_list, 4, read8, context);
    uint32_t node = live32(read8, context, l->find_list);
    for (unsigned k = 0; ok && node && k < 16u; ++k) {
        ok = es2_io_replay_track(r, node, 0x14u, read8, context);
        node = live32(read8, context, node + 0x10u);
    }
    if (ok && fp) {
        if (fp - l->streams >= 50u * 0x18u) ok = es2_io_replay_track(r, fp, 0x18u, read8, context);
        uint32_t buffer = live32(read8, context, fp + F_BUFFER), size = live32(read8, context, fp + F_BSIZE);
        if (ok && size && size <= 0x10000u) ok = es2_io_replay_track(r, buffer, size, read8, context);
    }
    if (ok && ffblk) ok = es2_io_replay_track(r, ffblk, 0x110u, read8, context);
    return ok;
}

static uint32_t overlay_slot(const es2_io_replay *r, uint32_t a, int *found)
{
    uint32_t mask = (1u << r->slot_bits) - 1u, h = (a * 2654435761u) >> (32u - r->slot_bits);
    while (r->stamp[h] == r->generation) {
        if (r->keys[h] == a) { *found = 1; return h; }
        h = (h + 1u) & mask;
    }
    *found = 0;
    return h;
}
static int snapshot_byte(const es2_io_replay *r, uint32_t a, uint8_t *v)
{
    for (uint32_t i = 0; i < r->region_count; ++i)
        if (a - r->region_start[i] < r->region_size[i]) { *v = r->snapshot[r->region_offset[i] + a - r->region_start[i]]; return 1; }
    return 0;
}
static uint8_t replay_read8(void *context, uint32_t a)
{
    es2_io_replay *r = context;
    int found;
    uint32_t h = overlay_slot(r, a, &found);
    if (found) return r->values[h];
    uint8_t v;
    if (snapshot_byte(r, a, &v)) return v;
    return r->live_read8(r->live_context, a);
}
static void replay_write8(void *context, uint32_t a, uint8_t v)
{
    es2_io_replay *r = context;
    int found;
    uint32_t h = overlay_slot(r, a, &found);
    if (!found) {
        if (r->count >= r->limit) { r->skip = 1; return; }
        r->stamp[h] = r->generation;
        r->keys[h] = a;
        r->written[r->count++] = a;
    }
    r->values[h] = v;
}
static void replay_problem(es2_io_replay *r, const char *what, uint32_t where, uint32_t model, uint32_t real)
{
    if (r->what) return;
    r->what = what; r->where = where; r->model_value = model; r->real_value = real;
}
static uint32_t pool32(const es2_io_replay *r, uint32_t offset)
{
    if (offset + 4u > r->pool_size) return 0;
    const uint8_t *p = r->pool + offset;
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void replay_os(void *context, es2_io_os *call)
{
    es2_io_replay *r = context;
    if (r->next >= r->record_count) {
        replay_problem(r, "os_missing", r->next, call->function, 0);
        call->result = call->function == ES2_IO_OS_CREATE_FILE || call->function == ES2_IO_OS_FIND_FIRST ? 0xffffffffu : 0u;
        return;
    }
    const es2_io_record *k = &r->records[r->next++];
    const uint32_t *a = call->arg;
    int same = k->function == call->function;
    if (same) {
        switch (call->function) {
        case ES2_IO_OS_CREATE_FILE:
            same = k->arg[0] == a[0] && k->arg[1] == a[1] && k->arg[2] == a[2] && k->arg[4] == a[4] && k->arg[5] == a[5] &&
                   k->arg[6] == 0 && k->size >= 12u && pool32(r, k->data) == 12u && pool32(r, k->data + 4u) == 0 &&
                   pool32(r, k->data + 8u) == a[3];
            break;
        case ES2_IO_OS_READ_FILE: same = k->arg[0] == a[0] && k->arg[1] == a[1] && k->arg[2] == a[2] && k->arg[4] == 0; break;
        case ES2_IO_OS_WRITE_FILE:
            same = k->arg[0] == a[0] && k->arg[2] == a[2] && k->arg[4] == 0 && (!a[1] || k->arg[1] == a[1]);
            if (same && k->size < a[2]) { r->skip = 1; break; }
            for (uint32_t i = 0; same && i < a[2]; ++i) {
                uint8_t mine = call->data ? call->data[i] : replay_read8(r, a[1] + i);
                if (mine != r->pool[k->data + i]) same = 0;
            }
            break;
        case ES2_IO_OS_SET_FILE_POINTER: same = k->arg[0] == a[0] && k->arg[1] == a[1] && k->arg[2] == 0 && k->arg[3] == a[2]; break;
        case ES2_IO_OS_GET_FILE_SIZE: same = k->arg[0] == a[0] && k->arg[1] == 0; break;
        case ES2_IO_OS_CLOSE_HANDLE: case ES2_IO_OS_GET_FILE_TYPE: case ES2_IO_OS_GET_FILE_ATTRIBUTES:
        case ES2_IO_OS_FIND_FIRST: case ES2_IO_OS_FIND_NEXT: case ES2_IO_OS_FIND_CLOSE:
            same = k->arg[0] == a[0];
            break;
        case ES2_IO_OS_FILE_TIME_TO_LOCAL: same = k->size >= 16u && pool32(r, k->data) == a[0] && pool32(r, k->data + 4u) == a[1]; break;
        case ES2_IO_OS_FILE_TIME_TO_DOS:
            same = k->size >= 12u && pool32(r, k->data) == a[0] && pool32(r, k->data + 4u) == a[1] && k->arg[1] == a[2] && k->arg[2] == a[3];
            break;
        default: break;
        }
    }
    if (!same) replay_problem(r, "os", r->next - 1u, call->function, k->function);
    call->result = k->result;
    call->count = k->count;
    switch (k->function) {
    case ES2_IO_OS_READ_FILE:
        if (call->function == ES2_IO_OS_READ_FILE && a[3] && k->result == 1u) {
            if (k->size < k->count) { r->skip = 1; break; }
            for (uint32_t i = 0; i < k->count; ++i) replay_write8(r, a[1] + i, r->pool[k->data + i]);
        }
        break;
    case ES2_IO_OS_FIND_FIRST: case ES2_IO_OS_FIND_NEXT:
        if (k->size >= ES2_IO_FIND_DATA) memcpy(call->out, r->pool + k->data, ES2_IO_FIND_DATA);
        else r->skip = 1;
        break;
    case ES2_IO_OS_FILE_TIME_TO_LOCAL:
        if (k->size >= 16u) memcpy(call->out, r->pool + k->data + 8u, 8);
        break;
    case ES2_IO_OS_FILE_TIME_TO_DOS:
        if (k->size >= 12u && call->function == ES2_IO_OS_FILE_TIME_TO_DOS) {
            replay_write8(r, a[2], r->pool[k->data + 8u]); replay_write8(r, a[2] + 1u, r->pool[k->data + 9u]);
            replay_write8(r, a[3], r->pool[k->data + 10u]); replay_write8(r, a[3] + 1u, r->pool[k->data + 11u]);
        }
        break;
    case ES2_IO_OS_GET_LOCAL_TIME:
        if (k->size >= 16u) memcpy(call->out, r->pool + k->data, 16);
        else r->skip = 1;
        break;
    default: break;
    }
}
static uint32_t replay_malloc(void *context, uint32_t n, uint32_t hint)
{
    es2_io_replay *r = context;
    uint32_t p = live32(r->live_read8, r->live_context, hint);
    /* The original's block: its CW3220 header word is the block size with the in-use bit. */
    uint32_t size = n < 8u ? 16u : ((n + 3u) & ~3u) + 8u;
    if (p && live32(r->live_read8, r->live_context, p - 4u) != (size | 1u))
        replay_problem(r, "malloc", hint, size | 1u, live32(r->live_read8, r->live_context, p - 4u));
    return p;
}
static void replay_free(void *context, uint32_t p)
{
    es2_io_replay *r = context;
    for (uint32_t i = 0; i < r->region_count; ++i) if (r->region_start[i] == p) r->region_freed[i] = 1;
}

void es2_io_replay_env(es2_io_env *env, es2_io_replay *r, const es2_io_record *records, uint32_t record_count,
                       const uint8_t *pool, uint32_t pool_size)
{
    r->records = records; r->record_count = record_count; r->next = 0;
    r->pool = pool; r->pool_size = pool_size;
    env->context = r;
    env->read8 = replay_read8;
    env->write8 = replay_write8;
    env->os = replay_os;
    env->malloc = replay_malloc;
    env->free = replay_free;
}

const char *es2_io_replay_finish(es2_io_replay *r, uint32_t model_eax, uint32_t real_eax)
{
    if (r->skip) return 0;
    if (r->what) return r->what;
    if (model_eax != real_eax) { replay_problem(r, "result", 0, model_eax, real_eax); return r->what; }
    if (r->next != r->record_count) { replay_problem(r, "os_extra", r->next, r->next, r->record_count); return r->what; }
    for (uint32_t i = 0; i < r->count; ++i) {
        uint32_t a = r->written[i];
        int found;
        uint32_t h = overlay_slot(r, a, &found);
        uint8_t real = r->live_read8(r->live_context, a);
        if (r->values[h] != real) { replay_problem(r, "memory", a, r->values[h], real); return r->what; }
    }
    for (uint32_t i = 0; i < r->region_count; ++i)
        for (uint32_t k = 0; !r->region_freed[i] && k < r->region_size[i]; ++k) {
            uint32_t a = r->region_start[i] + k;
            int found;
            overlay_slot(r, a, &found);
            if (found) continue;
            uint8_t before = r->snapshot[r->region_offset[i] + k], real = r->live_read8(r->live_context, a);
            if (before != real) { replay_problem(r, "state", a, before, real); return r->what; }
        }
    return 0;
}

/* ------------------------------------------------------------------------
 * The state an own runtime keeps in its own data (rtl/), with
 * CW3220's start-up, time zone and exit hooks. */

/* ctype[(signed char)b + 1] of CW3220 (table 4218b4, the tolower table
 * 4217b4 below it). */
uint32_t es2_io_ctype(uint8_t b)
{
    if (b >= 0x80u) return b == 0xffu ? 0u : (uint32_t)(uint8_t)(b + 1u);
    if (b == ' ') return 0x81u;
    if (b >= 0x09u && b <= 0x0du) return 0x21u;
    if (b < 0x20u || b == 0x7fu) return 0x20u;
    if (b >= '0' && b <= '9') return 0x12u;
    if (b >= 'A' && b <= 'Z') return b <= 'F' ? 0x14u : 0x04u;
    if (b >= 'a' && b <= 'z') return b <= 'f' ? 0x18u : 0x08u;
    return 0x40u;
}

/* The DOS error map (420358): errno for a Win32/DOS error code. Codes not
 * named below (and up to 0x12a) give EINVAL-like 19. */
uint8_t es2_io_error_map(uint32_t code)
{
    switch (code) {
    case 0x00: return 0;
    case 0x02: case 0x03: case 0x12: case 0x35: case 0x3f: case 0x40: case 0x43: case 0x6e: case 0x7b: return 2;
    case 0x04: case 0x71: return 4;
    case 0x05: case 0x0c: case 0x0f: case 0x10: case 0x20: case 0x21: case 0x41: case 0x91: case 0x9e: case 0xa7: return 5;
    case 0x06: case 0x72: return 6;
    case 0x07: case 0x08: case 0x09: return 8;
    case 0x0e: case 0x49: case 0x4a: case 0x4b: case 0x4c: case 0x4d: case 0x4e: case 0x4f: case 0x51: case 0x73: return 14;
    case 0x32: case 0x37: case 0x42: return 15;
    case 0x11: return 17;
    case 0x0a: return 20;
    case 0x0b: return 21;
    case 0x6b: return 22;
    case 0x23: case 0x24: case 0x44: case 0x54: return 23;
    case 0x80: case 0x81: case 0xb8: return 24;
    case 0x3d: return 27;
    case 0x3e: case 0x52: case 0x70: return 28;
    case 0x84: return 29;
    case 0x13: return 30;
    case 0x6d: case 0xe6: case 0xe9: return 32;
    case 0x34: case 0x50: case 0x55: case 0xb7: return 35;
    case 0x56: return 37;
    case 0x7f: return 38;
    case 0x16: case 0x17: case 0x18: case 0x19: case 0x1a: case 0x1d: case 0x1e: case 0x1f: case 0x39: case 0x3a: case 0x3b:
    case 0x45: case 0x53: case 0x58: return 40;
    case 0x14: case 0x1b: case 0x22: return 41;
    case 0x38: case 0x46: case 0x48: case 0x59: case 0x64: case 0x65: case 0x6c: case 0x8e: case 0xe7: return 42;
    case 0x15: case 0x1c: case 0x33: case 0x36: case 0x63: return 44;
    case 0xce: return 48;
    default: return code >= 0x25u && code <= 0x31u ? 14u : 19u;
    }
}

/* The scanner's character classes (420244), as f_scan reads them. */
uint8_t es2_io_scan_class(uint8_t b)
{
    switch (b) {
    case 0x00: return 0;
    case 0x09: case 0x0a: case 0x0b: case 0x0c: case 0x0d: case ' ': return 1;
    case '%': return 3;
    case '*': return 4;
    case 'c': return 6;
    case 'D': case 'd': return 7;
    case 'U': case 'u': return 8;
    case 'I': case 'i': return 9;
    case 'E': case 'G': case 'e': case 'f': case 'g': return 10;
    case 'L': return 11;
    case 'H': case 'h': return 12;
    case 'l': return 13;
    case 'O': case 'o': return 14;
    case 's': return 15;
    case '[': case ']': return 16;
    case 'n': return 17;
    case 'X': case 'x': return 18;
    case 'p': return 19;
    case 'N': return 20;
    case 'F': return 21;
    default: return b >= '0' && b <= '9' ? 5u : 2u;
    }
}

void es2_io_static_state(const es2_io_env *env, const es2_io_layout *layout, uint32_t fmode_word, uint32_t dummy_hook)
{
    io context = {env, layout};
    const io *c = &context;
    static const uint32_t std_flags[3] = {0x209u, 0x20au, 0x202u};
    w32(c, layout->nfile, ES2_IO_NFILE);
    for (uint32_t fd = 0; fd < ES2_IO_NFILE; ++fd) {
        w32(c, fd_flags_address(c, fd), fd < 3u ? 0x6000u + (fd ? 1u : 0u) : 0u);
        w32(c, layout->handles + 4u * fd, 0);
    }
    for (uint32_t i = 0; i < ES2_IO_NFILE; ++i) {
        uint32_t fp = layout->streams + 0x18u * i;
        for (uint32_t k = 0; k < 0x18u; ++k) w8(c, fp + k, 0);
        if (i < 3u) { f_set_flags(c, fp, std_flags[i]); w8(c, fp + F_FD, i); }
    }
    w32(c, layout->errno_word, 0);
    w32(c, layout->doserrno, 0);
    w32(c, layout->error_count, ES2_IO_SYS_NERR);
    for (uint32_t k = 0; k < ES2_IO_ERROR_MAP_SIZE; ++k) w8(c, layout->error_map + k, es2_io_error_map(k));
    w32(c, fmode_word, ES2_IO_FMODE_DEFAULT);
    w32(c, layout->fmode_pointer, fmode_word);
    w32(c, layout->umask_word, 0xffffffffu);
    w32(c, layout->exit_buffers, dummy_hook);
    w32(c, layout->exit_files, dummy_hook);
    w8(c, layout->getc_byte, 0);
    w32(c, layout->find_list, 0);
    for (uint32_t k = 0; k < ES2_IO_SCAN_CLASSES; ++k) w8(c, layout->scan_class + k, es2_io_scan_class((uint8_t)k));
    w32(c, layout->time_zone, 18000u);
    w32(c, layout->summer_time, 1u);
}

/* CW3220 memmove (402a0c) for the start-up's copy of inherited handles. */
static void memmove_g(const io *c, uint32_t dest, uint32_t source, uint32_t n)
{
    if (dest < source) { memcpy_g(c, dest, source, n); return; }
    if (dest == source) return;
    while (n) { --n; w8(c, dest + n, r8(c, source + n)); }
}
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* isatty (409cc4). */
static int is_terminal(const io *c, uint32_t fd)
{
    if (fd >= nfile(c)) return 0;
    return os1(c, ES2_IO_OS_GET_FILE_TYPE, fd_handle(c, fd)) == 2u;
}

void es2_io_startup(const es2_io_env *env, const es2_io_layout *layout)
{
    io context = {env, layout};
    const io *c = &context;
    /* 409014. */
    uint32_t count = os1(c, ES2_IO_OS_SET_HANDLE_COUNT, nfile(c));
    if (count < nfile(c)) w32(c, layout->nfile, count);
    es2_io_os info;
    memset(&info, 0, sizeof(info));
    info.function = ES2_IO_OS_GET_STARTUP_INFO;
    os_call(c, &info);
    uint32_t size = (uint32_t)info.out[0x32] | (uint32_t)info.out[0x33] << 8, block = le32(info.out + 0x34);
    int inherited = 0;
    if (size) {
        uint32_t k = r32(c, block), p = block + 4u;
        if (k * 5u + 4u == size) {
            inherited = 1;
            uint32_t i = 0;
            for (; s32(i) < s32(k); ++i) {
                uint8_t b = r8(c, p++);
                uint32_t flags = (b & 0x20u ? 0x800u : 0u) | (b & 0x40u ? 0x2000u : 0u) | (b & 0x80u ? 0x4000u : 0x8000u);
                uint32_t at = fd_flags_address(c, i);
                w32(c, at, (r32(c, at) & 3u) | flags);
            }
            for (i = k; i < nfile(c); ++i) w32(c, fd_flags_address(c, i), 0);
            memmove_g(c, layout->handles, p, 4u * k);
        }
    }
    if (!inherited) {
        static const uint32_t which[3] = {0xfffffff6u, 0xfffffff5u, 0xfffffff4u};
        for (uint32_t fd = 0; fd < 3u; ++fd) w32(c, layout->handles + 4u * fd, os1(c, ES2_IO_OS_GET_STD_HANDLE, which[fd]));
        for (uint32_t fd = 0; fd <= 2u; ++fd) {
            uint32_t at = fd_flags_address(c, fd);
            if (is_terminal(c, fd)) w32(c, at, r32(c, at) | 0x2000u);
            else w32(c, at, r32(c, at) & 0xffffdfffu);
        }
    }
    /* 407248. */
    uint32_t streams = layout->streams;
    for (uint32_t i = 0; i < 3u; ++i) w16(c, streams + 0x18u * i + F_TOKEN, streams + 0x18u * i);
    for (uint32_t i = 3; i < nfile(c); ++i) {
        uint32_t fp = streams + 0x18u * i;
        w8(c, fp + F_FD, 0xffu);
        w16(c, fp + F_TOKEN, fp);
    }
    for (uint32_t i = 0; i < 2u; ++i) {
        uint32_t fp = streams + 0x18u * i;
        if (!(r32(c, fd_flags_address(c, f_fd(c, fp))) & 0x2000u)) f_set_flags(c, fp, f_flags(c, fp) & 0xfdffu);
        /* stdin line buffered on a terminal, stdout unbuffered there; else fully buffered. */
        uint32_t mode = (f_flags(c, fp) & 0x200u) ? (i ? 2u : 1u) : 0u;
        set_buffer(c, fp, 0, mode, 0x200u);
    }
}

/* atol (4148c8) on guest memory: the value only. */
static uint32_t atol_value(const io *c, uint32_t p)
{
    uint8_t b;
    do b = r8(c, p++); while (es2_io_ctype(b) & 1u);
    int negative = 0;
    if (b == '+' || b == '-') { negative = b == '-'; b = r8(c, p++); }
    uint32_t value = 0;
    while (b >= '0' && b <= '9') { value = value * 10u + b - 0x30u; b = r8(c, p++); }
    return negative ? 0u - value : value;
}
static uint32_t length_g(const io *c, uint32_t p) { uint32_t n = 0; while (r8(c, p + n)) ++n; return n; }
static int alpha_at(const io *c, uint32_t p) { return (es2_io_ctype(r8(c, p)) & 0xcu) != 0; }
static int digit_at(const io *c, uint32_t p) { return (es2_io_ctype(r8(c, p)) & 2u) != 0; }

/* getenv (41924c) over the copy of the block 41a050 makes: the first entry
 * that starts with the name followed by '='. */
static uint32_t getenv_g(const io *c, uint32_t block, const char *name)
{
    uint32_t n = (uint32_t)strlen(name);
    for (uint32_t entry = block; r8(c, entry); entry += length_g(c, entry) + 1u) {
        uint32_t k = 0;
        while (k < n && r8(c, entry + k) == (uint8_t)name[k]) ++k;
        if (k == n && r8(c, entry + n) == '=') return entry + n + 1u;
    }
    return 0;
}

void es2_io_tzset(const es2_io_env *env, const es2_io_layout *layout, uint32_t environment)
{
    io context = {env, layout};
    const io *c = &context;
    uint32_t tz = environment ? getenv_g(c, environment, "TZ") : 0u;
    if (tz && length_g(c, tz) >= 4u && alpha_at(c, tz) && alpha_at(c, tz + 1u) && alpha_at(c, tz + 2u)) {
        uint8_t sign = r8(c, tz + 3u);
        if ((sign == '-' || sign == '+' || digit_at(c, tz + 3u)) && (digit_at(c, tz + 3u) || digit_at(c, tz + 4u))) {
            w32(c, layout->time_zone, atol_value(c, tz + 3u) * 3600u);
            w32(c, layout->summer_time, 0);
            /* A further three letters name the summer time. */
            for (uint32_t p = tz + 3u; r8(c, p); ++p) {
                if (!alpha_at(c, p)) continue;
                if (length_g(c, p) >= 3u && alpha_at(c, p + 1u) && alpha_at(c, p + 2u)) w32(c, layout->summer_time, 1u);
                break;
            }
            return;
        }
    }
    es2_io_os call;
    memset(&call, 0, sizeof(call));
    call.function = ES2_IO_OS_GET_TIME_ZONE;
    os_call(c, &call);
    w32(c, layout->summer_time, call.result == 2u ? 1u : 0u);
    w32(c, layout->time_zone, (le32(call.out) + le32(call.out + 0x54)) * 60u);
}

void es2_io_exit_buffers(const es2_io_env *env, const es2_io_layout *layout)
{
    io context = {env, layout};
    const io *c = &context;
    for (uint32_t i = 0; i < 3u; ++i) {
        uint32_t fp = layout->streams + 0x18u * i;
        if (r8(c, fp + F_FLAGS) & 3u) f_flush(c, fp);
    }
}

void es2_io_exit_files(const es2_io_env *env, const es2_io_layout *layout)
{
    io context = {env, layout};
    const io *c = &context;
    for (uint32_t i = 0; i < nfile(c); ++i) {
        uint32_t fp = layout->streams + 0x18u * i;
        if (r8(c, fp + F_FLAGS) & 3u) f_close(c, fp);
    }
}
