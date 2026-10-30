#ifndef ES2_CORE_RTL_IO_H
#define ES2_CORE_RTL_IO_H
#include <stdint.h>

/* The file and console imports of DBSIM.EXE from CW3220.DLL as own
 * code (contract: docs/reference-game/rtl_imports.md, section "file and console
 * imports").
 *
 * The model keeps no state of its own. Every variable it reads or writes is
 * the original's, reached through `es2_io_env` at the guest addresses of
 * `es2_io_layout`: the handle table (_openfd flags and OS handles), the FILE
 * array (_streams), errno/_doserrno, the find-state list of findfirst, the
 * exit hooks that fopen and setvbuf install, the byte of unbuffered fgetc.
 * Buffers and find nodes come from CW3220's own heap (env malloc/free). So
 * the model and the original can work on the same tables call by call, in
 * any mix. Win32 calls go through `env->os` with the original's arguments in
 * the original's order; constant tables (the DOS error map, the scanner's
 * character classes) are read from the loaded DLL through the layout.
 *
 * Every function returns ES2_IO_DONE with the original's EAX, or
 * ES2_IO_UNSUPPORTED before anything observable was done (the caller then
 * runs the original): fclose of a tmpfile() stream, an fscanf format with a
 * floating-point conversion or an uninitialised number base, an unbuffered
 * fread of 0 bytes (the original reads an uninitialised local), a printf
 * format outside the modelled formatter subset, time() with an impossible
 * SYSTEMTIME. ECX/EDX are not modelled (the original leaves kernel32
 * residue there; no DBSIM caller reads them). */

/* Guest addresses (relocated) of CW3220's I/O variables and tables. */
typedef struct es2_io_layout {
    uint32_t nfile;            /* __nfile (50) */
    uint32_t openfd;           /* _openfd[] flags, one dword per handle */
    uint32_t handles;          /* OS handles, one dword per handle */
    uint32_t streams;          /* _streams[] (FILE, 0x18 bytes each) */
    uint32_t errno_word;       /* errno */
    uint32_t doserrno;         /* _doserrno */
    uint32_t error_count;      /* _sys_nerr */
    uint32_t error_map;        /* DOS error -> errno bytes (0x12b entries) */
    uint32_t fmode_pointer;    /* pointer to the module's _fmode word */
    uint32_t umask_word;       /* permission mask of open's O_CREAT path */
    uint32_t exit_buffers;     /* hook word set by setvbuf ... */
    uint32_t exit_buffers_value;   /* ... to this function (flush stdin/out/err) */
    uint32_t exit_files;       /* hook word set by fopen's mode parser ... */
    uint32_t exit_files_value;     /* ... to this function (close every FILE) */
    uint32_t getc_byte;        /* the byte unbuffered fgetc reads into */
    uint32_t find_list;        /* head of findfirst's node list */
    uint32_t scan_class;       /* scanner character classes (0x80 bytes) */
    uint32_t time_zone;        /* _timezone */
    uint32_t summer_time;      /* _daylight */
    uint32_t stdout_file;      /* &_streams[1] */
} es2_io_layout;

/* The layout of the reference CW3220.DLL loaded at `base`. */
void es2_io_layout_cw3220(es2_io_layout *layout, uint32_t base);

/* Win32 functions CW3220 calls for these imports. */
typedef enum es2_io_os_function {
    ES2_IO_OS_CREATE_FILE,          /* (path, access, share, inherit, disposition, attributes): HANDLE; SA {12, NULL, inherit}, no template */
    ES2_IO_OS_CLOSE_HANDLE,         /* (handle): BOOL */
    ES2_IO_OS_READ_FILE,            /* (handle, buffer, count, need_bytes): BOOL; os.count = bytes read */
    ES2_IO_OS_WRITE_FILE,           /* (handle, buffer or 0, count): BOOL; os.data when buffer is 0; os.count = written */
    ES2_IO_OS_SET_FILE_POINTER,     /* (handle, distance, method): DWORD (high part NULL) */
    ES2_IO_OS_GET_FILE_SIZE,        /* (handle): DWORD (high part NULL) */
    ES2_IO_OS_GET_FILE_TYPE,        /* (handle): DWORD */
    ES2_IO_OS_GET_FILE_ATTRIBUTES,  /* (path): DWORD */
    ES2_IO_OS_GET_LAST_ERROR,       /* (): DWORD */
    ES2_IO_OS_FIND_FIRST,           /* (path): HANDLE; os.out = WIN32_FIND_DATAA (320 bytes) */
    ES2_IO_OS_FIND_NEXT,            /* (handle): BOOL; os.out = WIN32_FIND_DATAA */
    ES2_IO_OS_FIND_CLOSE,           /* (handle): BOOL */
    ES2_IO_OS_FILE_TIME_TO_LOCAL,   /* (low, high): BOOL; os.out = FILETIME (8 bytes) */
    ES2_IO_OS_FILE_TIME_TO_DOS,     /* (low, high, date address, time address): BOOL; the OS writes the two words */
    ES2_IO_OS_GET_LOCAL_TIME,       /* (): os.out = SYSTEMTIME (16 bytes) */
    ES2_IO_OS_FUNCTION_COUNT
} es2_io_os_function;

#define ES2_IO_FIND_DATA 320u
typedef struct es2_io_os {
    uint32_t function;
    uint32_t arg[6];
    const uint8_t *data;      /* WRITE_FILE from host memory (arg[1] == 0) */
    uint32_t result;          /* EAX of the call */
    uint32_t count;           /* READ_FILE/WRITE_FILE: *lpNumberOfBytes... */
    uint8_t out[ES2_IO_FIND_DATA];
} es2_io_os;

typedef struct es2_io_env {
    void *context;
    uint8_t (*read8)(void *context, uint32_t address);
    void (*write8)(void *context, uint32_t address, uint8_t value);
    /* One Win32 call; fills result, count and out. READ_FILE stores the
     * bytes at the guest buffer (arg[3] says whether the model reads them
     * back; a shadow replay needs the recorded bytes only then). */
    void (*os)(void *context, es2_io_os *call);
    /* CW3220's _malloc(n) / _free(p). `hint` is the guest address where the
     * original stores the result next (a replay reads it from there). */
    uint32_t (*malloc)(void *context, uint32_t n, uint32_t hint);
    void (*free)(void *context, uint32_t p);
} es2_io_env;

typedef enum es2_io_function {
    ES2_IO_OPEN,
    ES2_IO_CLOSE,
    ES2_IO_READ,
    ES2_IO_WRITE,
    ES2_IO_LSEEK,
    ES2_IO_FILELENGTH,
    ES2_IO_FOPEN,
    ES2_IO_FCLOSE,
    ES2_IO_FREAD,
    ES2_IO_FWRITE,
    ES2_IO_FSEEK,
    ES2_IO_FTELL,
    ES2_IO_REWIND,
    ES2_IO_FSCANF,
    ES2_IO_FLUSHALL,
    ES2_IO_FINDFIRST,
    ES2_IO_FINDNEXT,
    ES2_IO_TIME,
    /* Console stubs: no output, no FILE state; only the return value. */
    ES2_IO_PRINTF,
    ES2_IO_FPUTC,     /* __fputc (the putc macro's overflow call) */
    ES2_IO_FPUTS,
    ES2_IO_FUNCTION_COUNT
} es2_io_function;

enum { ES2_IO_DONE = 0, ES2_IO_UNSUPPORTED = 1 };

/* 1 when fscanf's format at `format` is inside the model (no float
 * conversion, no number read with the scanner's uninitialised base: '%'
 * followed by NUL, white space or '%' after flags). */
int es2_io_scan_supported(const es2_io_env *env, const es2_io_layout *layout, uint32_t format);

/* CW3220 export name ("_open", "__fputc", ...). */
const char *es2_io_name(es2_io_function function);
/* 1 for the console stubs (printf, __fputc, fputs). */
int es2_io_is_stub(es2_io_function function);

/* Calls `function` with its cdecl argument slots at guest address `args`
 * (the caller's ESP + 4), read through the env. */
int es2_io_call(es2_io_function function, const es2_io_env *env, const es2_io_layout *layout, uint32_t args, uint32_t *eax);

/* ------------------------------------------------------------------------
 * rtl_imports.md, section "the own cw3220.dll": what an own cw3220.dll (rtl/)
 * needs besides the 21 imports, so that its tables can live in the DLL
 * instead of CW3220: CW3220's static values of the variables, the start-up
 * of the handle table and of the FILE array (two of CW3220's init records),
 * the time zone (another init record) and the two stream clean-up hooks that
 * exit runs. Their Win32 calls use these further codes in es2_io_os.function
 * (none of the 21 imports makes them). */
enum {
    ES2_IO_OS_SET_HANDLE_COUNT = ES2_IO_OS_FUNCTION_COUNT, /* (count): UINT */
    ES2_IO_OS_GET_STARTUP_INFO,      /* (): os.out = STARTUPINFOA (0x44 bytes: cbReserved2 at +0x32, lpReserved2 at +0x34) */
    ES2_IO_OS_GET_STD_HANDLE,        /* (which: 0xfffffff6, 0xfffffff5, 0xfffffff4): HANDLE */
    ES2_IO_OS_GET_TIME_ZONE,         /* (): DWORD; os.out = TIME_ZONE_INFORMATION (172 bytes) */
    ES2_IO_OS_ALL_COUNT
};

/* CW3220's ctype flags of a byte as its macros read them, ctype[(signed
 * char)byte + 1]: 1 space, 2 digit, 4 upper, 8 lower, 0x10 hex digit, 0x20
 * control, 0x40 punctuation, 0x80 blank; a byte 0x80..0xfe reads the
 * tolower table below ctype (its entry is the byte + 1), 0xff the EOF
 * entry (0). */
uint32_t es2_io_ctype(uint8_t byte);
/* CW3220's constant tables that the I/O code reads through the layout: the
 * errno byte of a DOS error code 0..0x12a and the scanner class of a byte
 * 0..0x7f. es2_io_static_state writes them. */
uint8_t es2_io_error_map(uint32_t code);
uint8_t es2_io_scan_class(uint8_t byte);
#define ES2_IO_ERROR_MAP_SIZE 0x12bu
#define ES2_IO_SCAN_CLASSES 0x80u
#define ES2_IO_SYS_NERR 49u          /* _sys_nerr: the error messages CW3220 has */
#define ES2_IO_NFILE 50u             /* __nfile before the start-up */
#define ES2_IO_FMODE_DEFAULT 0x4000u /* CW3220's own _fmode word (text) */

/* CW3220's values of the layout's variables before its init records run
 * (its DATA section and zeroed BSS): __nfile 50, handle flags 0x6000,
 * 0x6001, 0x6001 for 0..2, the three standard FILE entries (flags 0x209,
 * 0x20a, 0x202, fd 0, 1, 2), every other FILE and handle 0, errno and
 * _doserrno 0, _sys_nerr, the two tables, the _fmode pointer naming
 * `fmode_word` (which gets 0x4000), the umask 0xffffffff, both exit hooks
 * `dummy_hook` (CW3220's bare ret), the time zone 18000 with summer time 1
 * (overwritten by es2_io_tzset). */
void es2_io_static_state(const es2_io_env *env, const es2_io_layout *layout, uint32_t fmode_word, uint32_t dummy_hook);
/* The start-up of the handle table (409014: SetHandleCount(__nfile) may
 * lower __nfile; GetStartupInfoA's handle block of a parent, used when
 * cbReserved2 = 4 + 5 * its count, else GetStdHandle for 0..2 and their
 * device flag from GetFileType) and of the FILE array (407248: tokens,
 * fd -1 for 3..__nfile-1, the standard input's and output's terminal flag
 * and buffer through setvbuf's core, which installs the exitbuf hook). */
void es2_io_startup(const es2_io_env *env, const es2_io_layout *layout);
/* _tzset (41e268): TZ from `environment` (an environment block as
 * GetEnvironmentStrings returns it: NUL-terminated strings ending with an
 * empty one; 0 for none) when it has the form "AAA[+-]h..[DDD]", else
 * GetTimeZoneInformation. Writes the time zone and summer-time words. */
void es2_io_tzset(const es2_io_env *env, const es2_io_layout *layout, uint32_t environment);
/* The stream hooks that exit calls once fopen or setvbuf installed them:
 * exitbuf (407ca4) flushes the three standard streams that are open,
 * exitfopen (407c7c) closes every open FILE of 0..__nfile-1. */
void es2_io_exit_buffers(const es2_io_env *env, const es2_io_layout *layout);
void es2_io_exit_files(const es2_io_env *env, const es2_io_layout *layout);

/* ------------------------------------------------------------------------
 * Shadow replay: the original ran first with its Win32 calls recorded;
 * the model then runs on the state before the call (a snapshot of the
 * tracked regions, the model's writes in an overlay, everything else read
 * from live memory) and takes every OS result from the record. */
typedef struct es2_io_record {
    uint32_t function;        /* es2_io_os_function */
    uint32_t arg[7];          /* the raw stack arguments */
    uint32_t result;
    uint32_t count;           /* READ/WRITE: *lpNumberOfBytes after the call */
    uint32_t data, size;      /* bytes in the pool: READ output, WRITE input, SA (CREATE),
                               * find data, FILETIME in+out, the two DOS words, SYSTEMTIME */
} es2_io_record;

typedef struct es2_io_replay {
    /* Live memory (after the original ran). */
    void *live_context;
    uint8_t (*live_read8)(void *context, uint32_t address);
    /* Overlay of the model's writes (open addressing, generation stamps). */
    uint32_t *keys, *stamp, *written;
    uint8_t *values;
    uint32_t slot_bits, limit, count, generation;
    /* Snapshot of tracked regions before the original ran. */
    uint32_t region_start[64], region_size[64], region_offset[64];
    uint8_t region_freed[64];   /* not compared: a buffer the call freed (CW3220's free writes list links
                                 * into it) or an input region (es2_io_replay_track_input) */
    uint32_t region_count;
    uint8_t *snapshot;
    uint32_t snapshot_used, snapshot_capacity;
    /* The original's Win32 calls. */
    const es2_io_record *records;
    uint32_t record_count, next;
    const uint8_t *pool;
    uint32_t pool_size;
    /* Result: the first problem, or none. */
    const char *what;         /* NULL, or a mismatch kind */
    uint32_t where, model_value, real_value;
    int skip;                 /* the replay cannot decide (overlay or record capacity) */
} es2_io_replay;

/* Overlay storage: keys/stamp 2^slot_bits words each, values 2^slot_bits
 * bytes, written `limit` words (limit <= 2^slot_bits / 2). */
void es2_io_replay_init(es2_io_replay *replay, uint32_t *keys, uint32_t *stamp, uint8_t *values, uint32_t *written,
                        uint32_t slot_bits, uint32_t limit, uint8_t *snapshot, uint32_t snapshot_capacity);
/* Before the original runs: forget the last call, then add the regions to
 * snapshot (read through `read8` now). Returns 0 when they do not fit. */
void es2_io_replay_reset(es2_io_replay *replay);
int es2_io_replay_track(es2_io_replay *replay, uint32_t start, uint32_t size, uint8_t (*read8)(void *, uint32_t), void *context);
/* The same for bytes the model only reads and the original may change
 * without a trace for its caller (its own argument slots, which CW3220
 * functions reuse as locals): not compared afterwards. */
int es2_io_replay_track_input(es2_io_replay *replay, uint32_t start, uint32_t size, uint8_t (*read8)(void *, uint32_t), void *context);
/* The regions of every call: the tables of `layout`, the find nodes, the
 * FILE argument's buffer (`fp`, 0 if none), the find block (`ffblk`, 0 if
 * none). Returns 0 when they do not fit. */
int es2_io_replay_track_call(es2_io_replay *replay, const es2_io_layout *layout, uint32_t fp, uint32_t ffblk,
                             uint8_t (*read8)(void *, uint32_t), void *context);
/* After the original ran: the env for the model run. */
void es2_io_replay_env(es2_io_env *env, es2_io_replay *replay, const es2_io_record *records, uint32_t record_count,
                       const uint8_t *pool, uint32_t pool_size);
/* After the model ran: compare EAX, the record (every call consumed), each
 * overlay byte with live memory and each untouched snapshot byte with live
 * memory. Returns NULL or the kind of the first difference (also in
 * replay->what); replay->skip says the call could not be judged. */
const char *es2_io_replay_finish(es2_io_replay *replay, uint32_t model_eax, uint32_t real_eax);

#endif
