/* CW3220's file imports over the file model (es2/sim/files.h); the service rows
 * are in sim/dispatch/services.h. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include "es2/sim/files.h"
#include "es2/sim/loop.h"
#include "es2/sim/memory.h"
#include "es2/sim/services.h"

/* Offsets inside the runtime region (own layout; the model reaches every
 * variable through es2_io_layout, so only the sizes matter). */
enum {
    R_NFILE = 0x000, R_OPENFD = 0x010, R_HANDLES = 0x0e0, R_STREAMS = 0x1b0, R_ERRNO = 0x660, R_DOSERRNO = 0x664,
    R_NERR = 0x668, R_FMODE_POINTER = 0x66c, R_FMODE = 0x670, R_UMASK = 0x674, R_EXIT_BUFFERS = 0x678,
    R_EXIT_FILES = 0x67c, R_GETC = 0x680, R_FIND_LIST = 0x684, R_TIME_ZONE = 0x688, R_SUMMER = 0x68c,
    R_ERROR_MAP = 0x690, R_SCAN_CLASS = 0x7c0, R_HOOKS = 0x840, R_SCRATCH = 0x900
};

static es2_sim_files *bound;
void es2_sim_files_bind(es2_sim_files *f) { bound = f; }
es2_sim_files *es2_sim_files_bound(void) { return bound; }

/* ---- the model's environment */
static uint8_t env_read8(void *c, uint32_t a) { return es2_sim_read8(((es2_sim_files *)c)->memory, a); }
static void env_write8(void *c, uint32_t a, uint8_t v) { es2_sim_write8(((es2_sim_files *)c)->memory, a, v); }
static void env_os(void *c, es2_io_os *call) {
    es2_sim_files *f = c;
    ++f->os_calls;
    f->os(f->os_context, f->memory, call);
}
static uint32_t env_malloc(void *c, uint32_t n, uint32_t hint) {
    (void)hint;
    return es2_sim_heap_runtime_malloc(((es2_sim_files *)c)->heap, n);
}
static void env_free(void *c, uint32_t p) { es2_sim_heap_runtime_free(((es2_sim_files *)c)->heap, p); }

/* ---- headless Win32: the file API on the working directory. Buffers the
 * model names by guest address are reached through the guest view. */
static char *host_text(const es2_guest_memory *m, uint32_t a) { return a ? m->host(m->context, a) : NULL; }
void es2_sim_files_os_headless(void *context, const es2_guest_memory *m, es2_io_os *call) {
    const es2_sim_files *f = context;
    const uint32_t *a = call->arg;
    DWORD number = 0;
    call->result = 0;
    call->count = 0;
    switch (call->function) {
    case ES2_IO_OS_CREATE_FILE: {
        SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, (BOOL)a[3]};
        call->result = (uint32_t)(uintptr_t)CreateFileA(host_text(m, a[0]), a[1], a[2], &sa, a[4], a[5], NULL);
        break;
    }
    case ES2_IO_OS_CLOSE_HANDLE: call->result = (uint32_t)CloseHandle((HANDLE)(uintptr_t)a[0]); break;
    case ES2_IO_OS_READ_FILE: {
        void *buffer = a[2] ? m->host(m->context, a[1]) : NULL;
        if (a[2] && !buffer) { SetLastError(ERROR_INVALID_PARAMETER); break; }
        call->result = (uint32_t)ReadFile((HANDLE)(uintptr_t)a[0], buffer, a[2], &number, NULL);
        call->count = number;
        break;
    }
    case ES2_IO_OS_WRITE_FILE: {
        const void *buffer = a[1] ? m->host(m->context, a[1]) : call->data;
        if (a[2] && !buffer) { SetLastError(ERROR_INVALID_PARAMETER); break; }
        call->result = (uint32_t)WriteFile((HANDLE)(uintptr_t)a[0], buffer, a[2], &number, NULL);
        call->count = number;
        break;
    }
    case ES2_IO_OS_SET_FILE_POINTER: call->result = SetFilePointer((HANDLE)(uintptr_t)a[0], (LONG)a[1], NULL, a[2]); break;
    case ES2_IO_OS_GET_FILE_SIZE: call->result = GetFileSize((HANDLE)(uintptr_t)a[0], NULL); break;
    case ES2_IO_OS_GET_FILE_TYPE: call->result = a[0] ? GetFileType((HANDLE)(uintptr_t)a[0]) : FILE_TYPE_UNKNOWN; break;
    case ES2_IO_OS_GET_FILE_ATTRIBUTES: call->result = GetFileAttributesA(host_text(m, a[0])); break;
    case ES2_IO_OS_GET_LAST_ERROR: call->result = GetLastError(); break;
    case ES2_IO_OS_FIND_FIRST: {
        WIN32_FIND_DATAA found;
        memset(&found, 0, sizeof(found));
        call->result = (uint32_t)(uintptr_t)FindFirstFileA(host_text(m, a[0]), &found);
        memcpy(call->out, &found, ES2_IO_FIND_DATA);
        break;
    }
    case ES2_IO_OS_FIND_NEXT: {
        WIN32_FIND_DATAA found;
        memset(&found, 0, sizeof(found));
        call->result = (uint32_t)FindNextFileA((HANDLE)(uintptr_t)a[0], &found);
        memcpy(call->out, &found, ES2_IO_FIND_DATA);
        break;
    }
    case ES2_IO_OS_FIND_CLOSE: call->result = (uint32_t)FindClose((HANDLE)(uintptr_t)a[0]); break;
    case ES2_IO_OS_FILE_TIME_TO_LOCAL: {
        FILETIME in = {a[0], a[1]}, out = {0, 0};
        call->result = (uint32_t)FileTimeToLocalFileTime(&in, &out);
        memcpy(call->out, &out, 8);
        break;
    }
    case ES2_IO_OS_FILE_TIME_TO_DOS: {
        FILETIME in = {a[0], a[1]};
        WORD date = 0, time = 0;
        call->result = (uint32_t)FileTimeToDosDateTime(&in, &date, &time);
        es2_sim_write8(m, a[2], (uint8_t)date); es2_sim_write8(m, a[2] + 1u, (uint8_t)(date >> 8));
        es2_sim_write8(m, a[3], (uint8_t)time); es2_sim_write8(m, a[3] + 1u, (uint8_t)(time >> 8));
        break;
    }
    /* Headless: a GUI process without a console or inherited handles. */
    case ES2_IO_OS_SET_HANDLE_COUNT: call->result = a[0]; break;
    case ES2_IO_OS_GET_STARTUP_INFO: memset(call->out, 0, 0x44); call->out[0] = 0x44; break;
    case ES2_IO_OS_GET_STD_HANDLE: call->result = 0; break;
    /* The clock without a wall clock: GetLocalTime answers the files'
     * SYSTEMTIME, GetTimeZoneInformation TIME_ZONE_ID_UNKNOWN with every bias
     * 0 (UTC), so _time returns the seconds es2_sim_files_set_time chose. */
    case ES2_IO_OS_GET_LOCAL_TIME:
        if (f) memcpy(call->out, f->local_time, 16);
        break;
    case ES2_IO_OS_GET_TIME_ZONE: memset(call->out, 0, 172); break;
    default: break;
    }
}

void es2_sim_files_os_wall_clock(void *context, const es2_guest_memory *m, es2_io_os *call) {
    if (call->function == ES2_IO_OS_GET_LOCAL_TIME) {
        SYSTEMTIME now;
        GetLocalTime(&now);
        memcpy(call->out, &now, 16);
        call->result = 0;
        call->count = 0;
    } else if (call->function == ES2_IO_OS_GET_TIME_ZONE) {
        TIME_ZONE_INFORMATION zone;
        memset(&zone, 0, sizeof(zone));
        call->result = GetTimeZoneInformation(&zone);
        call->count = 0;
        memcpy(call->out, &zone, sizeof(zone));
    } else {
        es2_sim_files_os_headless(context, m, call);
    }
}

/* Seconds since 1970 as a UTC SYSTEMTIME (the proleptic Gregorian
 * calendar's days from the epoch; 1970-01-01 was a Thursday). */
void es2_sim_files_set_time(es2_sim_files *f, uint32_t seconds) {
    uint32_t days = seconds / 86400u, rest = seconds % 86400u;
    uint32_t z = days + 719468u, era = z / 146097u, doe = z - era * 146097u;
    uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    uint32_t doy = doe - (365u * yoe + yoe / 4u - yoe / 100u), mp = (5u * doy + 2u) / 153u;
    uint32_t day = doy - (153u * mp + 2u) / 5u + 1u, month = mp < 10u ? mp + 3u : mp - 9u;
    uint32_t year = yoe + era * 400u + (month <= 2u ? 1u : 0u);
    const uint32_t field[8] = {year, month, (days + 4u) % 7u, day, rest / 3600u, rest / 60u % 60u, rest % 60u, 0};
    for (unsigned i = 0; i < 8u; ++i) {
        f->local_time[2u * i] = (uint8_t)field[i];
        f->local_time[2u * i + 1u] = (uint8_t)(field[i] >> 8);
    }
}

int es2_sim_files_init(es2_sim_files *f, const es2_guest_memory *memory, uint32_t region, es2_sim_heap *heap) {
    memset(f, 0, sizeof(*f));
    f->memory = memory;
    f->heap = heap;
    f->region = region;
    f->scratch = region + R_SCRATCH;
    if (!memory->host(memory->context, region) || !memory->host(memory->context, region + ES2_SIM_FILES_REGION_SIZE - 1u))
        return 1;
    for (uint32_t i = 0; i < ES2_SIM_FILES_REGION_SIZE; i += 4u) memory->write32(memory->context, region + i, 0);
    es2_io_layout *l = &f->layout;
    l->nfile = region + R_NFILE;
    l->openfd = region + R_OPENFD;
    l->handles = region + R_HANDLES;
    l->streams = region + R_STREAMS;
    l->errno_word = region + R_ERRNO;
    l->doserrno = region + R_DOSERRNO;
    l->error_count = region + R_NERR;
    l->error_map = region + R_ERROR_MAP;
    l->fmode_pointer = region + R_FMODE_POINTER;
    l->umask_word = region + R_UMASK;
    l->exit_buffers = region + R_EXIT_BUFFERS;
    l->exit_files = region + R_EXIT_FILES;
    l->getc_byte = region + R_GETC;
    l->find_list = region + R_FIND_LIST;
    l->scan_class = region + R_SCAN_CLASS;
    l->time_zone = region + R_TIME_ZONE;
    l->summer_time = region + R_SUMMER;
    l->stdout_file = l->streams + 0x18u;
    /* The hook values are markers in the region, never executed: the exit
     * path that calls them (exitbuf, exitfopen) is loop.c's. */
    f->dummy_hook = region + R_HOOKS;
    l->exit_buffers_value = region + R_HOOKS + 4u;
    l->exit_files_value = region + R_HOOKS + 8u;
    f->fmode_word = region + R_FMODE;
    f->env = (es2_io_env){f, env_read8, env_write8, env_os, env_malloc, env_free};
    f->os = es2_sim_files_os_headless;
    f->os_context = f;
    es2_sim_files_set_time(f, ES2_SIM_FILES_TIME_DEFAULT);
    es2_io_static_state(&f->env, l, f->fmode_word, f->dummy_hook);
    es2_io_startup(&f->env, l);
    es2_io_tzset(&f->env, l, 0);
    return 0;
}

int es2_sim_files_call(es2_sim_files *f, es2_io_function function, const uint32_t *slots, unsigned count, uint32_t *eax) {
    if (count > ES2_SIM_FILES_SCRATCH_SLOTS) count = ES2_SIM_FILES_SCRATCH_SLOTS;
    for (unsigned i = 0; i < ES2_SIM_FILES_SCRATCH_SLOTS; ++i)
        f->memory->write32(f->memory->context, f->scratch + 4u * i, i < count ? slots[i] : 0u);
    if (function == ES2_IO_FSCANF && !es2_io_scan_supported(&f->env, &f->layout, slots[1])) return 1;
    uint32_t result = 0;
    if (es2_io_call(function, &f->env, &f->layout, f->scratch, &result) != ES2_IO_DONE) return 1;
    ++f->calls[function];
    *eax = result;
    return 0;
}

/* ---- the service rows: EAX as the model returns it; ECX and EDX are left
 * as at the call (rtl_io.h: CW3220 leaves kernel32 residue there and no DBSIM
 * caller reads them). The slots a call can have: the fixed arguments, or for
 * _fscanf the format's pointers (a trapped call reads the caller's stack,
 * es2_sim_call the caller's array, which must be that long). */
static void file_service(es2_sim_service_call *call, es2_io_function function, unsigned slots) {
    es2_sim_files *f = bound;
    if (!f) { call->refused = 1; call->reason = "file services not set up"; return; }
    uint32_t eax = 0;
    if (es2_sim_files_call(f, function, call->slots, slots, &eax)) {
        call->refused = 1;
        call->reason = "file call outside the H6-4 model (fscanf float or base, unbuffered fread of 0, ...)";
        return;
    }
    call->eax = eax;
}
#define FILE_SERVICE(lower, upper, slots) \
    void es2_sim_service_file_##lower(es2_sim_service_call *call, void *user) { (void)user; file_service(call, ES2_IO_##upper, slots); }
FILE_SERVICE(fopen, FOPEN, 2)
FILE_SERVICE(fclose, FCLOSE, 1)
FILE_SERVICE(fread, FREAD, 4)
FILE_SERVICE(fwrite, FWRITE, 4)
FILE_SERVICE(fseek, FSEEK, 3)
FILE_SERVICE(ftell, FTELL, 1)
FILE_SERVICE(rewind, REWIND, 1)
FILE_SERVICE(fscanf, FSCANF, ES2_SIM_FILES_SCRATCH_SLOTS)
FILE_SERVICE(open, OPEN, 3)
FILE_SERVICE(close, CLOSE, 1)
FILE_SERVICE(read, READ, 3)
FILE_SERVICE(write, WRITE, 3)
FILE_SERVICE(lseek, LSEEK, 3)
FILE_SERVICE(filelength, FILELENGTH, 1)
FILE_SERVICE(findfirst, FINDFIRST, 3)
FILE_SERVICE(findnext, FINDNEXT, 1)
FILE_SERVICE(flushall, FLUSHALL, 0)
/* _time(&t), the thunk 0x4967fc (jmp [0x6cc344]); its one caller is the -D
 * demo pick 0x45ceb8 at 0x45cf91, which reads the stored t, not EAX. The
 * model's value from the files' OS clock (es2_sim_files_os_headless or
 * es2_sim_files_os_wall_clock). */
void es2_sim_service_file_time(es2_sim_service_call *call, void *user) {
    (void)user;
    es2_sim_files *f = bound;
    if (!f) { call->refused = 1; call->reason = "file services not set up"; return; }
    uint32_t eax = 0;
    if (es2_sim_files_call(f, ES2_IO_TIME, call->slots, 1, &eax)) {
        call->refused = 1;
        call->reason = "_time: the OS clock answered no SYSTEMTIME (month or day 0)";
        return;
    }
    call->eax = eax;
}

/* ---- 0x45ee00, the install-root path helper (es2/sim/files.h). */
void es2_sim_path_root(const es2_guest_memory *m, uint32_t p, uint32_t regs[3]) {
    const uint32_t buffer = 0x004d285cu, root = 0x004d27bcu;
    uint32_t n = 0, i = 0;
    uint8_t b;
    do { b = es2_sim_read8(m, root + i); es2_sim_write8(m, buffer + i, b); ++i; } while (b);
    n = i - 1u;
    uint32_t source = p + (es2_sim_read8(m, p + 1u) == ':' ? 2u : 0u), length = 0;
    do { b = es2_sim_read8(m, source + length); es2_sim_write8(m, buffer + n + length, b); ++length; } while (b);
    regs[0] = buffer;
    regs[1] = length;      /* EDX: the source length + 1 */
    regs[2] = 0;           /* ECX */
}
void es2_sim_service_path_root(es2_sim_service_call *call, void *user) {
    es2_sim_services *s = user;
    uint32_t regs[3];
    es2_sim_path_root(s->memory, call->slots[0], regs);
    call->eax = regs[0]; call->edx = regs[1]; call->ecx = regs[2];
}

/* ======================================================================
 * The stream classes' leaf methods (own_process.md, "files, streams, math and
 * runtime leftovers"; rows in sim/dispatch/services/runtime_leaves.h): the base class
 * (vtable 0x4a16e8), the read-from-handle class (0x4a1738) and the buffered
 * class (0x4a1ad8), plus the two detach thunks of the write and memory classes.
 * The methods of the three classes the startup uses are also written inline in
 * volumes.c; these rows are the general vtable versions for original code and
 * the bridge.
 *
 * Every row sets EAX, EDX and ECX as the original does (listing of the
 * address named at each row). The original pops a pushed argument into ECX
 * after a call (`push x; call f; pop ecx`): ECX is then x, taken as the
 * callee not writing its own argument slot. A callee through the loop
 * (es2_sim_loop_call) runs with zero entry registers and gives its exit
 * registers; where the original leaves ECX and EDX as they were at the call
 * of a CW3220 import (rows above), a row that calls it here sees the
 * callee's result instead. Nothing reads them in the original. */
typedef struct sregs { uint32_t eax, edx, ecx; } sregs;
#define VT_BUF 0x004a1ad8u
#define VT_READ 0x004a1738u
#define VA_STREAM_COUNTER 0x006cbee4u     /* pointer to the construction counter */
#define VA_THUNK_PROBE 0x004967aeu         /* the code bytes of an import thunk */

static uint32_t frd(const es2_sim_services *s, uint32_t a) { return s->memory->read32(s->memory->context, a); }
static void fwr(const es2_sim_services *s, uint32_t a, uint32_t v) { s->memory->write32(s->memory->context, a, v); }
static uint32_t frd16(const es2_sim_services *s, uint32_t a) { return s->memory->read16(s->memory->context, a); }
static uint32_t fsx16(uint32_t w) { return (uint32_t)(int32_t)(int16_t)(uint16_t)w; }
static uint32_t lo_al(uint32_t reg, uint32_t al) { return (reg & 0xffffff00u) | (al & 0xffu); }

static int fcall(es2_sim_loop *l, uint32_t va, const uint32_t *slots, unsigned count, sregs *r) {
    es2_sim_result x;
    if (es2_sim_loop_call(l, va, slots, count, &x)) return 1;
    r->eax = x.eax; r->edx = x.edx; r->ecx = x.ecx;
    return 0;
}
/* call [[obj] + off] */
static int fvcall(const es2_sim_services *s, es2_sim_loop *l, uint32_t obj, uint32_t off, const uint32_t *slots, unsigned count, sregs *r) {
    return fcall(l, frd(s, frd(s, obj) + off), slots, count, r);
}
static int fserve(es2_sim_service_call *sc, es2_sim_loop **l) {
    *l = es2_sim_loop_bound();
    if (!*l) { sc->refused = 1; sc->reason = "no main loop bound"; return 1; }
    return 0;
}
static void ffailed(es2_sim_service_call *sc) { sc->refused = 1; sc->reason = "a call inside the service was refused"; }
static sregs fentry(const es2_sim_service_call *sc) { sregs r = {sc->eax, sc->edx, sc->ecx}; return r; }
static void fput(es2_sim_service_call *sc, const sregs *r) { sc->eax = r->eax; sc->edx = r->edx; sc->ecx = r->ecx; }

/* ---- the base class (vtable 0x4a16e8) */
/* 0x47a874(obj): the status, [obj+4], through 0x4930ab (inlined: EAX =
 * [arg+4]; its caller pops the argument into ECX). Listing 0x47a874..0x47a884. */
void es2_sim_service_stream_status_47a874(es2_sim_service_call *sc, void *user) {
    es2_sim_services *s = user;
    sc->eax = frd(s, sc->slots[0] + 4u);
    sc->ecx = sc->slots[0];
}
/* 0x47a8d4(obj, x) and 0x47a8f4(obj): 0. 0x47a8fc, 0x47a904, 0x47a90c and
 * 0x47a914: AL = 1, the rest of EAX as it was (listing 0x47a8d4..0x47a91a). */
void es2_sim_service_stream_zero_47a8d4(es2_sim_service_call *sc, void *user) { (void)user; sc->eax = 0; }
void es2_sim_service_stream_true_47a8fc(es2_sim_service_call *sc, void *user) { (void)user; sc->eax = lo_al(sc->eax, 1); }

/* ---- the detach thunks of the write (0x47b5cc) and memory (0x47b9cc)
 * classes, identical: with an inner stream ([obj+8] != 0) call slot 4 of the
 * object's own table (ECX = obj), then [obj+8] = 0 and [obj+4] = 6 and EDX =
 * 0; EAX is the callee's. Without one nothing changes (listing 0x47b5cc..
 * 0x47b5ee, 0x47b9cc..0x47b9ee). */
void es2_sim_service_stream_detach_47b5cc(es2_sim_service_call *sc, void *user) {
    es2_sim_services *s = user;
    es2_sim_loop *l;
    uint32_t obj = sc->slots[0];
    if (!frd(s, obj + 8u)) return;
    if (fserve(sc, &l)) return;
    sregs r = fentry(sc);
    if (fvcall(s, l, obj, 0x10u, &obj, 1, &r)) { ffailed(sc); return; }
    fwr(s, obj + 8u, 0);
    fwr(s, obj + 4u, 6u);
    r.edx = 0;
    r.ecx = obj;
    fput(sc, &r);
}

/* ---- the buffered class (vtable 0x4a1ad8). Object: +4 status, +8 the
 * inner stream (0: detached), +0xc the buffer size, +0x10 the stream
 * position of the buffer's first byte, +0x14 the buffer, +0x18 the byte
 * "owns the buffer", +0x19 the count of valid bytes, +0x1d the cursor
 * (unaligned dwords). */
/* 0x47c95c(B): [B+4] = the inner's status (0x4930ab inlined), or 6 without
 * an inner; EAX = 0; ECX = the inner when there is one (listing 0x47c95c..
 * 0x47c983). */
static void b_status(const es2_sim_services *s, uint32_t b, sregs *r) {
    uint32_t inner = frd(s, b + 8u);
    if (inner) {
        fwr(s, b + 4u, frd(s, inner + 4u));
        r->ecx = inner;
    } else fwr(s, b + 4u, 6u);
    r->eax = 0;
}
void es2_sim_service_buf_status_47c95c(es2_sim_service_call *sc, void *user) {
    sregs r = fentry(sc);
    b_status(user, sc->slots[0], &r);
    fput(sc, &r);
}

/* 0x47c984(inner, n, buf): inner's slot 6 (read) with the three arguments;
 * the exit is the callee's (listing 0x47c984..0x47c99b). */
static int b_inner_read(const es2_sim_services *s, es2_sim_loop *l, const uint32_t *args, sregs *r) {
    uint32_t slots[3] = {args[0], args[1], args[2]};
    return fvcall(s, l, args[0], 0x18u, slots, 3, r);
}
void es2_sim_service_buf_read_inner_47c984(es2_sim_service_call *sc, void *user) {
    es2_sim_loop *l;
    if (fserve(sc, &l)) return;
    sregs r = fentry(sc);
    if (b_inner_read(user, l, sc->slots, &r)) { ffailed(sc); return; }
    fput(sc, &r);
}

/* 0x47c8c4(B, pos): fill the buffer from stream position pos. Seek the
 * inner (slot 2); on a failed seek take the inner's status (0x47c95c) and
 * return 0. Else read [B+0xc] bytes into [B+0x14] (0x47c984): a read that
 * returned true sets the valid count to the buffer size; a failed one takes
 * the inner's status, and when that is 2 (end of data) the valid count is
 * tell - pos (slot 1): non-zero is a short read (still success), zero sets
 * the status to 2 with nothing valid. Any other status is copied by 0x47c95c.
 * Success: [B+0x10] = pos, [B+0x1d] = 0, AL = 1 (listing 0x47c8c4..0x47c958).
 * Registers as listed in the comments. */
static int b_refill(const es2_sim_services *s, es2_sim_loop *l, uint32_t b, uint32_t pos, sregs *r) {
    uint32_t inner = frd(s, b + 8u);
    uint32_t seek_args[2] = {inner, pos};
    sregs seek;
    if (fvcall(s, l, inner, 8u, seek_args, 2, &seek)) return 1;
    if ((seek.eax & 0xffu) == 0) {
        sregs t = seek;
        b_status(s, b, &t);                 /* push ebx; call 0x47c95c; pop ecx */
        r->eax = 0; r->ecx = b; r->edx = seek.edx;
        return 0;
    }
    uint32_t valid = frd(s, b + 0xcu);
    uint32_t read_args[3] = {frd(s, b + 8u), frd(s, b + 0xcu), frd(s, b + 0x14u)};
    sregs rd;
    if (b_inner_read(s, l, read_args, &rd)) return 1;
    if ((rd.eax & 0xffu) != 0) {
        fwr(s, b + 0x19u, valid); fwr(s, b + 0x10u, pos); fwr(s, b + 0x1du, 0);
        r->eax = lo_al(rd.eax, 1); r->edx = 0; r->ecx = rd.ecx;
        return 0;
    }
    inner = frd(s, b + 8u);
    uint32_t status = frd(s, inner + 4u);   /* 0x4930ab inlined; pop ecx: ECX = the inner */
    if (status != 2u) {
        sregs t = {0, rd.edx, inner};
        b_status(s, b, &t);                 /* push ebx; call 0x47c95c; pop ecx */
        r->eax = 0; r->ecx = b; r->edx = rd.edx;
        return 0;
    }
    uint32_t tell_args[1] = {frd(s, b + 8u)};
    sregs tell;
    if (fvcall(s, l, tell_args[0], 4u, tell_args, 1, &tell)) return 1;
    valid = tell.eax - pos;
    if (valid != 0) {
        fwr(s, b + 0x19u, valid); fwr(s, b + 0x10u, pos); fwr(s, b + 0x1du, 0);
        r->eax = lo_al(tell.eax, 1); r->edx = 0; r->ecx = tell_args[0];
        return 0;
    }
    fwr(s, b + 4u, 2u); fwr(s, b + 0x10u, pos); fwr(s, b + 0x19u, 0); fwr(s, b + 0x1du, 0);
    r->eax = 0; r->edx = 0; r->ecx = tell_args[0];
    return 0;
}
void es2_sim_service_buf_refill_47c8c4(es2_sim_service_call *sc, void *user) {
    es2_sim_loop *l;
    if (fserve(sc, &l)) return;
    sregs r = fentry(sc);
    if (b_refill(user, l, sc->slots[0], sc->slots[1], &r)) { ffailed(sc); return; }
    fput(sc, &r);
}

/* 0x47c7d8(B, pos): seek within the buffer. Status 2 is cleared to 0 first
 * (EDX = 0); any other non-zero status gives 0; a position inside the valid
 * bytes just moves the cursor (EAX = B with AL = 1, EDX = the offset);
 * otherwise refill at pos (listing 0x47c7d8..0x47c816). ECX = pos unless the
 * refill sets it. */
void es2_sim_service_buf_seek_47c7d8(es2_sim_service_call *sc, void *user) {
    es2_sim_services *s = user;
    es2_sim_loop *l;
    uint32_t b = sc->slots[0], pos = sc->slots[1];
    sregs r = fentry(sc);
    r.ecx = pos;
    if (frd(s, b + 4u) == 2u) { r.edx = 0; fwr(s, b + 4u, 0); }
    if (frd(s, b + 4u) != 0) { r.eax = 0; fput(sc, &r); return; }
    int32_t off = (int32_t)(pos - frd(s, b + 0x10u));
    r.edx = (uint32_t)off;
    if (off < 0 || off > (int32_t)frd(s, b + 0x19u)) {
        if (fserve(sc, &l)) return;
        if (b_refill(s, l, b, pos, &r)) { ffailed(sc); return; }
        fput(sc, &r);
        return;
    }
    fwr(s, b + 0x1du, (uint32_t)off);
    r.eax = lo_al(b, 1);
    fput(sc, &r);
}

/* 0x47c82c(B, n, dst): copy n bytes from the stream. Non-zero status: 0.
 * Otherwise while fewer than n bytes are buffered, copy what is there (the
 * memcpy import 0x49680e), refill at the end of the buffer (a false refill
 * gives 0) and go on; the rest is copied and the cursor advances, AL = 1.
 * ECX leaves as the running destination: the entry ECX is pushed as a
 * local slot and popped back as [ebp-4] at 0x47c8aa; EDX is the last
 * call's (listing 0x47c82c..0x47c8ac). A refill that succeeds without
 * progress (a zero buffer size) loops for ever, as the original's
 * 0x47c853..0x47c88c, which has no other exit (D). */
void es2_sim_service_buf_read_47c82c(es2_sim_service_call *sc, void *user) {
    es2_sim_services *s = user;
    es2_sim_loop *l;
    uint32_t b = sc->slots[0], n = sc->slots[1], dst = sc->slots[2];
    sregs r = fentry(sc);
    if (frd(s, b + 4u) != 0) { sc->eax = 0; sc->ecx = dst; return; }
    if (fserve(sc, &l)) return;
    int32_t avail = (int32_t)(frd(s, b + 0x19u) - frd(s, b + 0x1du));
    while (avail < (int32_t)n) {
        uint32_t copy_args[3] = {dst, frd(s, b + 0x14u) + frd(s, b + 0x1du), (uint32_t)avail};
        sregs copy;
        if (fcall(l, 0x0049680eu, copy_args, 3, &copy)) { ffailed(sc); return; }
        sregs refill = fentry(sc);
        if (b_refill(s, l, b, frd(s, b + 0x10u) + frd(s, b + 0x19u), &refill)) { ffailed(sc); return; }
        r.edx = refill.edx;
        if ((refill.eax & 0xffu) == 0) { sc->eax = 0; sc->edx = r.edx; sc->ecx = dst; return; }
        n -= (uint32_t)avail;
        dst += (uint32_t)avail;
        avail = (int32_t)(frd(s, b + 0x19u) - frd(s, b + 0x1du));
    }
    uint32_t copy_args[3] = {dst, frd(s, b + 0x14u) + frd(s, b + 0x1du), n};
    sregs copy;
    if (fcall(l, 0x0049680eu, copy_args, 3, &copy)) { ffailed(sc); return; }
    fwr(s, b + 0x1du, frd(s, b + 0x1du) + n);
    sc->eax = lo_al(copy.eax, 1);
    sc->edx = copy.edx;
    sc->ecx = dst;
}

/* 0x47c78c(obj, x): the status mapping (0xd for x = 0, 1 for x = 2 or 3,
 * else 0); 0x47c7bc(B): the stream position, [B+0x10] + [B+0x1d], or 0 for
 * a detached stream (status 6; EDX is written only otherwise); 0x47c818(B):
 * status != 6; 0x47c8b0(B): status = 4, 0 (listing 0x47c78c..0x47c8c0). */
void es2_sim_service_buf_map_47c78c(es2_sim_service_call *sc, void *user) {
    (void)user;
    uint32_t x = sc->slots[1];
    sc->eax = x == 0 ? 0xdu : (x == 2u || x == 3u) ? 1u : 0u;
}
void es2_sim_service_buf_tell_47c7bc(es2_sim_service_call *sc, void *user) {
    es2_sim_services *s = user;
    uint32_t b = sc->slots[0];
    if (frd(s, b + 4u) == 6u) { sc->eax = 0; return; }
    sc->edx = frd(s, b + 0x10u) + frd(s, b + 0x1du);
    sc->eax = sc->edx;
}
void es2_sim_service_buf_live_47c818(es2_sim_service_call *sc, void *user) {
    es2_sim_services *s = user;
    sc->eax = frd(s, sc->slots[0] + 4u) != 6u;
}
void es2_sim_service_buf_fail_47c8b0(es2_sim_service_call *sc, void *user) {
    es2_sim_services *s = user;
    fwr(s, sc->slots[0] + 4u, 4u);
    sc->eax = 0;
}

/* 0x47c754(B): detach the inner stream. Without one nothing happens (the
 * registers stay). Otherwise slot 4 of B's own table (ECX = B), then seek the
 * inner (slot 2) to [B+0x10] + [B+0x1d]; [B+8] = 0, [B+4] = 6, EDX = 0, EAX
 * and ECX the seek's (listing 0x47c754..0x47c789). */
static int b_detach(const es2_sim_services *s, es2_sim_loop *l, uint32_t b, sregs *r) {
    if (!frd(s, b + 8u)) return 0;
    sregs t = *r;
    if (fvcall(s, l, b, 0x10u, &b, 1, &t)) return 1;
    uint32_t seek_args[2] = {frd(s, b + 8u), frd(s, b + 0x10u) + frd(s, b + 0x1du)};
    if (fvcall(s, l, seek_args[0], 8u, seek_args, 2, &t)) return 1;
    fwr(s, b + 8u, 0);
    fwr(s, b + 4u, 6u);
    t.edx = 0;
    *r = t;
    return 0;
}
void es2_sim_service_buf_detach_47c754(es2_sim_service_call *sc, void *user) {
    es2_sim_services *s = user;
    es2_sim_loop *l;
    if (!frd(s, sc->slots[0] + 8u)) return;
    if (fserve(sc, &l)) return;
    sregs r = fentry(sc);
    if (b_detach(s, l, sc->slots[0], &r)) { ffailed(sc); return; }
    fput(sc, &r);
}

/* 0x47c684(B, flags), the destructor: the construction counter is
 * decremented even for a null B (EAX = the counter's pointer then). Else the
 * table is reset, the stream detached, an owned buffer freed (0x4967a8), the
 * base destructor 0x47a8a4 run with flags 0 and, when flags bit 0 is set, B
 * deleted (0x477548; ECX = B). Without bit 0 ECX is the base destructor's
 * (listing 0x47c684..0x47c6cd). */
void es2_sim_service_buf_dtor_47c684(es2_sim_service_call *sc, void *user) {
    es2_sim_services *s = user;
    es2_sim_loop *l;
    uint32_t b = sc->slots[0], flags = sc->slots[1];
    if (fserve(sc, &l)) return;
    uint32_t counter = frd(s, VA_STREAM_COUNTER);
    fwr(s, counter, frd(s, counter) - 1u);
    sc->eax = counter;
    if (!b) return;
    fwr(s, b, VT_BUF);
    sregs r = fentry(sc);
    if (b_detach(s, l, b, &r)) { ffailed(sc); return; }
    if (es2_sim_read8(s->memory, b + 0x18u)) {
        uint32_t pointer = frd(s, b + 0x14u);
        if (fcall(l, 0x004967a8u, &pointer, 1, &r)) { ffailed(sc); return; }
    }
    uint32_t base_args[2] = {b, 0};
    if (fcall(l, 0x0047a8a4u, base_args, 2, &r)) { ffailed(sc); return; }
    if (flags & 1u) {
        if (fcall(l, 0x00477548u, &b, 1, &r)) { ffailed(sc); return; }
        r.ecx = b;
    }
    fput(sc, &r);
}

/* ---- the read-from-handle class (vtable 0x4a1738). Object: +4 status, +8
 * the handle (a word, 0xffff: none), +0xa the byte "opened here". The
 * imports are lseek 0x496826, _read 0x496808 and _close 0x496874. */
#define VA_LSEEK 0x00496826u
#define VA_READ 0x00496808u
#define VA_CLOSE 0x00496874u
/* 0x47af60(obj): the error status: 1 when the dword at 0x4967ae (the first
 * code bytes of an import thunk, never 0) is non-zero, else 3; EAX = 0
 * (listing 0x47af60..0x47af82). */
static void r_error(const es2_sim_services *s, uint32_t obj) {
    fwr(s, obj + 4u, frd(s, VA_THUNK_PROBE) != 0 ? 1u : 3u);
}
void es2_sim_service_read_error_47af60(es2_sim_service_call *sc, void *user) {
    r_error(user, sc->slots[0]);
    sc->eax = 0;
}
/* 0x47af58: AL = 1. 0x47af84(obj): status = 4, 0. 0x47adb4(obj, x): 2 for
 * x = 0, 1 for x = 2 or 3, else 0 (listing 0x47adb4..0x47ade0, 0x47af58,
 * 0x47af84). */
void es2_sim_service_read_true_47af58(es2_sim_service_call *sc, void *user) { (void)user; sc->eax = lo_al(sc->eax, 1); }
void es2_sim_service_read_fail_47af84(es2_sim_service_call *sc, void *user) {
    es2_sim_services *s = user;
    fwr(s, sc->slots[0] + 4u, 4u);
    sc->eax = 0;
}
void es2_sim_service_read_map_47adb4(es2_sim_service_call *sc, void *user) {
    (void)user;
    uint32_t x = sc->slots[1];
    sc->eax = x == 0 ? 2u : (x == 2u || x == 3u) ? 1u : 0u;
}
/* 0x47aeb0(obj): lseek(handle, 0, 1) as the position; the exit is the
 * import's (listing 0x47aeb0..0x47aec8). */
void es2_sim_service_read_tell_47aeb0(es2_sim_service_call *sc, void *user) {
    es2_sim_services *s = user;
    es2_sim_loop *l;
    if (fserve(sc, &l)) return;
    uint32_t args[3] = {fsx16(frd16(s, sc->slots[0] + 8u)), 0, 1};
    sregs r = fentry(sc);
    if (fcall(l, VA_LSEEK, args, 3, &r)) { ffailed(sc); return; }
    fput(sc, &r);
}
/* 0x47aecc(obj, pos): status 2 is cleared; any non-zero status gives 0;
 * else lseek(handle, pos, 0): -1 sets the error status (0x47af60, ECX =
 * obj, EAX = 0), anything else AL = 1 over EAX = result + 1 (listing
 * 0x47aecc..0x47af0d). */
void es2_sim_service_read_seek_47aecc(es2_sim_service_call *sc, void *user) {
    es2_sim_services *s = user;
    es2_sim_loop *l;
    uint32_t obj = sc->slots[0];
    if (frd(s, obj + 4u) == 2u) { fwr(s, obj + 4u, 0); sc->eax = 0; }
    if (frd(s, obj + 4u) != 0) { sc->eax = 0; return; }
    if (fserve(sc, &l)) return;
    uint32_t args[3] = {fsx16(frd16(s, obj + 8u)), sc->slots[1], 0};
    sregs r = fentry(sc);
    if (fcall(l, VA_LSEEK, args, 3, &r)) { ffailed(sc); return; }
    if (r.eax + 1u == 0) {
        r_error(s, obj);
        r.eax = 0; r.ecx = obj;
    } else r.eax = lo_al(r.eax + 1u, 1);
    fput(sc, &r);
}
/* 0x47ae28(obj), close: an object that opened its handle (byte +0xa) and
 * still has one (not 0xffff) closes it; a failed _close (-1) sets status 3
 * and gives 0 (ECX = the handle as pushed). Otherwise the handle becomes
 * 0xffff, the status 5, AL = 1 (listing 0x47ae28..0x47ae67). */
static int r_close(const es2_sim_services *s, es2_sim_loop *l, uint32_t obj, sregs *r) {
    if (es2_sim_read8(s->memory, obj + 0xau) != 0 && frd16(s, obj + 8u) != 0xffffu) {
        uint32_t handle = fsx16(frd16(s, obj + 8u));
        if (fcall(l, VA_CLOSE, &handle, 1, r)) return 1;
        r->ecx = handle;
        r->eax += 1u;
        if (r->eax == 0) { fwr(s, obj + 4u, 3u); return 0; }
    }
    s->memory->write16(s->memory->context, obj + 8u, 0xffffu);
    fwr(s, obj + 4u, 5u);
    r->eax = lo_al(r->eax, 1);
    return 0;
}
void es2_sim_service_read_close_47ae28(es2_sim_service_call *sc, void *user) {
    es2_sim_loop *l;
    if (fserve(sc, &l)) return;
    sregs r = fentry(sc);
    if (r_close(user, l, sc->slots[0], &r)) { ffailed(sc); return; }
    fput(sc, &r);
}

/* ======================================================================
 * The help file's name and the demo pick (own_process.md, "SHELL, RTL, FILE and
 * INP gap roots as rows and verdicts"; rows in sim/dispatch/services/gap_roots_shell.h).
 * Guest locals live in the loop's call area (l->stack_hi grows down),
 * zero-filled. */
#define VA_HELP_LANG_NAME 0x0049f400u    /* the language file's name */
#define VA_HELP_LANG_MODE 0x0049f412u
#define VA_HELP_NAME 0x004d2958u          /* the help path buffer */
#define VA_HELP_TAIL_A 0x0049f433u
#define VA_HELP_TAIL_B 0x0049f435u
#define VA_CD_PATH 0x0045ee60u            /* hud_widgets.c's row hud_cd_path */
#define VA_STRCAT 0x004967eau
#define VA_FOPEN_H 0x00496802u
#define VA_FREAD_H 0x0049684au
#define VA_FCLOSE_H 0x00496838u
#define VA_DEMO_HOLDER 0x004d26e0u        /* the demo list's string holder */
#define VA_DEMO_NAMES 0x004d26f4u         /* 20 name pointers */
#define VA_DEMO_COUNT 0x004d26f0u         /* word */
#define VA_DEMO_INDEX 0x0049f034u         /* word: the last pick */
#define VA_DEMO_BUILT 0x0049f032u         /* byte: the holder is constructed */
#define VA_DEMO_READ 0x0049f033u          /* byte: the list was read and is not empty */
#define VA_DEMO_LIST 0x0049f212u          /* the demo list's name */
#define VA_HOLDER_CTOR 0x00453824u
#define VA_READ_STREAM_CTOR 0x0047ad40u
#define VA_READ_STREAM_DTOR 0x0047ad78u
#define VA_NEW_ARRAY_H 0x004774f0u
#define VA_STRINGS_GROUP 0x004539a0u
#define VA_TIME 0x004967fcu

static uint32_t h25_push(es2_sim_loop *l, uint32_t bytes) {
    if (l->stack_hi - l->stack_lo < bytes + 0x400u) return 0;
    l->stack_hi -= bytes;
    for (uint32_t i = 0; i < bytes; i += 4u) l->memory->write32(l->memory->context, l->stack_hi + i, 0);
    return l->stack_hi;
}
static void h25_strcpy(const es2_sim_services *s, uint32_t to, uint32_t from) {
    uint8_t b;
    uint32_t i = 0;
    do { b = es2_sim_read8(s->memory, from + i); es2_sim_write8(s->memory, to + i, b); ++i; } while (b);
}

/* 0x45effc: the language of the help file. f = _fopen(root + 0x49f400,
 * 0x49f412); with a file one byte is read into the low byte of a local
 * dword (the entry ECX pushed, its low byte zeroed) and the file closed.
 * 'F', 'G' and 'S' give 0x49f415, 0x49f41c and 0x49f423, anything else
 * (also no file: the byte stays 0) 0x49f42b. EDX is the local dword (pop
 * edx), ECX f after a close (pop ecx), else _fopen's ECX (the pop ecx at
 * 0x45f011 is overwritten by the call). Listing 0x45effc..0x45f06c. */
static int help_language(es2_sim_services *s, es2_sim_loop *l, sregs *r) {
    uint32_t path[3];
    es2_sim_path_root(s->memory, VA_HELP_LANG_NAME, path);
    uint32_t top = l->stack_hi, local = h25_push(l, 4u);
    if (!local) return es2_sim_loop_refuse(l, 0x0045effcu, "help_language", "the own code's call area is exhausted");
    fwr(s, local, r->ecx & 0xffffff00u);
    uint32_t o[2] = {path[0], VA_HELP_LANG_MODE};
    if (fcall(l, VA_FOPEN_H, o, 2, r)) { l->stack_hi = top; return 1; }
    uint32_t f = r->eax;
    if (f) {
        uint32_t rd[4] = {local, 1u, 1u, f};
        if (fcall(l, VA_FREAD_H, rd, 4, r)) { l->stack_hi = top; return 1; }
        uint32_t c[1] = {f};
        if (fcall(l, VA_FCLOSE_H, c, 1, r)) { l->stack_hi = top; return 1; }
        r->ecx = f;
    }
    uint32_t word = frd(s, local);
    l->stack_hi = top;
    switch (word & 0xffu) {
    case 'F': r->eax = 0x0049f415u; break;
    case 'G': r->eax = 0x0049f41cu; break;
    case 'S': r->eax = 0x0049f423u; break;
    default: r->eax = 0x0049f42bu; break;
    }
    r->edx = word;
    return 0;
}
void es2_sim_service_help_language_45effc(es2_sim_service_call *sc, void *user) {
    es2_sim_loop *l;
    if (fserve(sc, &l)) return;
    sregs r = fentry(sc);
    if (help_language(user, l, &r)) { ffailed(sc); return; }
    fput(sc, &r);
}
/* 0x45f070: the help file's path. 0x4d2958 = the language (0x45effc,
 * copied inline) + 0x49f433 + 0x49f435 (_strcat), then the CD path
 * 0x45ee60(0x4d2958): EAX and EDX its, ECX 0x4d2958 (pop ecx). Listing
 * 0x45f070..0x45f0cc. */
void es2_sim_service_help_file_name_45f070(es2_sim_service_call *sc, void *user) {
    es2_sim_services *s = user;
    es2_sim_loop *l;
    if (fserve(sc, &l)) return;
    sregs r = fentry(sc);
    if (help_language(s, l, &r)) { ffailed(sc); return; }
    h25_strcpy(s, VA_HELP_NAME, r.eax);
    uint32_t a[2] = {VA_HELP_NAME, VA_HELP_TAIL_A}, b[2] = {VA_HELP_NAME, VA_HELP_TAIL_B}, c[1] = {VA_HELP_NAME};
    if (fcall(l, VA_STRCAT, a, 2, &r) || fcall(l, VA_STRCAT, b, 2, &r) || fcall(l, VA_CD_PATH, c, 1, &r)) { ffailed(sc); return; }
    r.ecx = VA_HELP_NAME;
    fput(sc, &r);
}

/* 0x45ceb8: the demo pick of -D (listing 0x45ceb8..0x45cfc5). The frame
 * record 0x49630c and the frame's state words are the original's exception
 * frame, not modelled. Once (byte 0x49f032): the holder 0x4d26e0 is built
 * (0x453824, counter + 1) and the counter taken back (- 1, a static
 * object). Until a list was read (byte 0x49f033): a read-from-handle
 * stream S (12 bytes at ebp-0xc) is opened on root + 0x49f212 (0x47ad40);
 * with status [S+4] 0 the holder's bytes are read as 0x4538d0 reads them
 * (inline: [holder+8] by the stream's slot +0x18, new[] of it into
 * [holder], the bytes, [holder+4] = [holder]), the first group goes to
 * 0x4d26f4 (0x4539a0, its n 0x14 not read) and its count (AX) to the word
 * 0x4d26f0; a positive count sets the byte; S is destroyed (0x47ad78(S,
 * 2)) either way. Without a list EAX is 0. Otherwise the index is 0 for a
 * count of 1, else _time(&t) and t % count (idiv: the remainder has the
 * dividend's sign); the word 0x49f034 = the index, EAX and ECX the name
 * pointer [0x4d26f4 + 4 * index (sign-extended word)], EDX the index
 * sign-extended. A count of 0 or less with the byte set (the code never
 * leaves it so) would divide by zero or pick outside the list: refused.
 * EDX of the empty answer is the frame record's residue (not reproduced:
 * the last callee's). */
void es2_sim_service_demo_pick_45ceb8(es2_sim_service_call *sc, void *user) {
    es2_sim_services *s = user;
    es2_sim_loop *l;
    if (fserve(sc, &l)) return;
    sregs r = fentry(sc);
    if (!es2_sim_read8(s->memory, VA_DEMO_BUILT)) {
        uint32_t h[1] = {VA_DEMO_HOLDER};
        if (fcall(l, VA_HOLDER_CTOR, h, 1, &r)) { ffailed(sc); return; }
        uint32_t counter = frd(s, VA_STREAM_COUNTER);
        fwr(s, counter, frd(s, counter) - 1u);
        es2_sim_write8(s->memory, VA_DEMO_BUILT, (uint8_t)(es2_sim_read8(s->memory, VA_DEMO_BUILT) + 1u));
    }
    if (!es2_sim_read8(s->memory, VA_DEMO_READ)) {
        uint32_t path[3];
        es2_sim_path_root(s->memory, VA_DEMO_LIST, path);
        uint32_t top = l->stack_hi, stream = h25_push(l, 0x10u);
        if (!stream) { es2_sim_loop_refuse(l, 0x0045ceb8u, "demo_pick", "the own code's call area is exhausted"); ffailed(sc); return; }
        uint32_t o[2] = {stream, path[0]};
        int refused = fcall(l, VA_READ_STREAM_CTOR, o, 2, &r);
        if (!refused && frd(s, stream + 4u) == 0) {
            uint32_t r1[3] = {stream, 4u, VA_DEMO_HOLDER + 8u};
            refused = fcall(l, frd(s, frd(s, stream) + 0x18u), r1, 3, &r);
            if (!refused) {
                uint32_t n[1] = {frd(s, VA_DEMO_HOLDER + 8u)};
                refused = fcall(l, VA_NEW_ARRAY_H, n, 1, &r);
            }
            if (!refused) {
                fwr(s, VA_DEMO_HOLDER, r.eax);
                uint32_t r2[3] = {stream, frd(s, VA_DEMO_HOLDER + 8u), r.eax};
                refused = fcall(l, frd(s, frd(s, stream) + 0x18u), r2, 3, &r);
            }
            if (!refused) {
                fwr(s, VA_DEMO_HOLDER + 4u, frd(s, VA_DEMO_HOLDER));
                uint32_t g[4] = {VA_DEMO_HOLDER, VA_DEMO_NAMES, 0x14u, 0};
                refused = fcall(l, VA_STRINGS_GROUP, g, 4, &r);
            }
            if (!refused) {
                s->memory->write16(s->memory->context, VA_DEMO_COUNT, (uint16_t)r.eax);
                if ((int16_t)(uint16_t)r.eax > 0) es2_sim_write8(s->memory, VA_DEMO_READ, 1);
            }
        }
        if (!refused) {
            uint32_t d[2] = {stream, 2u};
            refused = fcall(l, VA_READ_STREAM_DTOR, d, 2, &r);
        }
        l->stack_hi = top;
        if (refused) { ffailed(sc); return; }
    }
    if (!es2_sim_read8(s->memory, VA_DEMO_READ)) { sc->eax = 0; sc->edx = r.edx; sc->ecx = r.ecx; return; }
    int32_t count = (int16_t)frd16(s, VA_DEMO_COUNT), index = 0;
    if (count <= 0) { sc->refused = 1; sc->reason = "a demo count of 0 or less with the list marked read (the original divides by it)"; return; }
    if (count != 1) {
        uint32_t top = l->stack_hi, t = h25_push(l, 4u);
        if (!t) { es2_sim_loop_refuse(l, 0x0045ceb8u, "demo_pick", "the own code's call area is exhausted"); ffailed(sc); return; }
        uint32_t a[1] = {t};
        int refused = fcall(l, VA_TIME, a, 1, &r);
        int32_t now = (int32_t)frd(s, t);
        l->stack_hi = top;
        if (refused) { ffailed(sc); return; }
        index = now % count;
    }
    s->memory->write16(s->memory->context, VA_DEMO_INDEX, (uint16_t)index);
    uint32_t name = frd(s, VA_DEMO_NAMES + 4u * (uint32_t)(int32_t)(int16_t)(uint16_t)index);
    sc->eax = name; sc->ecx = name; sc->edx = (uint32_t)(int32_t)(int16_t)(uint16_t)index;
}

/* ======================================================================
 * The leaf slots of the volume stream (vtable 0x4a0e10), the write stream
 * (0x4a1760) and the memory stream (0x4a1788), and the class lookup
 * 0x47a4a0 (file_io.md, "the stream slots and the class lookup as rows";
 * rows in sim/dispatch/services/file_streams.h). The slots whose bodies are
 * those of the read-from-handle class reuse its services above. */
/* 0x473510(S, k), slot +0xc of 0x4a0e10 (listing 0x473510..0x473575): the
 * subtraction chain on EDX = k leaves EDX = -1 for k = 0, 0 for k = 1 and 4,
 * k - 4 otherwise. With a handle (word +8 not 0xffff): 2, 0, 1, 1, 0 for
 * k = 0..4; without: the dword +0x1e for k = 0 and ([S+0x1e] != 2) in EAX
 * and EDX for k = 4. Any other k gives 0. ECX stays. */
void es2_sim_service_vol_map_473510(es2_sim_service_call *sc, void *user) {
    es2_sim_services *s = user;
    uint32_t obj = sc->slots[0], k = sc->slots[1];
    int none = frd16(s, obj + 8u) == 0xffffu;
    sc->edx = k == 0 ? 0xffffffffu : (k == 1u || k == 4u) ? 0u : k - 4u;
    if (k == 0) sc->eax = none ? frd(s, obj + 0x1eu) : 2u;
    else if (k == 2u || k == 3u) sc->eax = 1u;
    else if (k == 4u && none) sc->eax = sc->edx = frd(s, obj + 0x1eu) != 2u;
    else sc->eax = 0;
}
/* 0x47b0d8(S, k), slot +0xc of 0x4a1760 (listing 0x47b0d8..0x47b113, the
 * jump table 0x47b0ea): 3, 1, 0, 1, 0 for k = 0..4, else 0. */
void es2_sim_service_write_map_47b0d8(es2_sim_service_call *sc, void *user) {
    (void)user;
    static const uint8_t map[5] = {3, 1, 0, 1, 0};
    uint32_t k = sc->slots[1];
    sc->eax = k <= 4u ? map[k] : 0u;
}
/* 0x47b3d4(S, k), slot +0xc of 0x4a1788 (listing 0x47b3d4..0x47b3fd): 4
 * for k = 0, 1 for k = 1..3, else 0. */
void es2_sim_service_memory_map_47b3d4(es2_sim_service_call *sc, void *user) {
    (void)user;
    uint32_t k = sc->slots[1];
    sc->eax = k == 0 ? 4u : k <= 3u ? 1u : 0u;
}
/* 0x47a4a0(tag) (listing 0x47a4a0..0x47a4e3): the [0x4a1690] registry pairs
 * (table, count) at 0x6c5834 hold 12-byte entries whose first dword is the
 * tag. Found: EAX = ECX = EDX = the entry. Not found: EAX 0, ECX the pair
 * address past the last, EDX the last table's end (as at the call without
 * a pair). */
#define VA_CLASS_PAIRS 0x006c5834u
#define VA_CLASS_PAIR_COUNT 0x004a1690u
void es2_sim_service_class_lookup_47a4a0(es2_sim_service_call *sc, void *user) {
    es2_sim_services *s = user;
    uint32_t tag = sc->slots[0], pair = VA_CLASS_PAIRS, entry = sc->edx;
    for (uint32_t i = 0; i < frd(s, VA_CLASS_PAIR_COUNT); ++i, pair += 8u) {
        entry = frd(s, pair);
        for (uint32_t k = 0; k < frd(s, pair + 4u); ++k, entry += 12u)
            if (frd(s, entry) == tag) { sc->eax = sc->ecx = sc->edx = entry; return; }
    }
    sc->eax = 0; sc->ecx = pair; sc->edx = entry;
}
