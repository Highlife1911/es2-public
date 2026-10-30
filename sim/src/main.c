/* es2sim.exe, the own DBSIM process (own_process.md).
 * `es2sim --headless` claims the guest range before the CRT runs, builds the guest
 * image from the own tables, sets up the heap, virtual time, the dispatch (services
 * and the registry's adapters) and the stage-1 trap on the simulation thread; it
 * runs the c0w32 entry and the 65 init records (es2/sim/init.h), CW3220's call
 * of WinMain and the start-up up to the mission load (es2/sim/startup.h) over the
 * file services (es2/sim/files.h); when WinMain returns (the -eggplant refusal), the
 * 26 exit records follow and the process exits with WinMain's result, the dword
 * 0x4d27b8; the fatal report exits with 1 without them (launcher.md, "DBSIM's
 * start-up as own code"). Then the DIB section's frame-buffer objects headless, the
 * mission load through the dispatch, the main loop with its tick records, the
 * input journal of [input] as the message source, [run] frame_limit, the loop tail,
 * WinMain's pump until WM_QUIT and CW3220's exit (the atexit functions, the exit
 * records, the stream exit hooks) (es2/sim/loop.h).
 *
 *   es2sim --headless [--config <ini>] [--tick-log <path>]
 *          [--arguments "<DBSIM arguments>"] [--frames <path>]
 *
 * --frames writes the headless frame capture (one record per present in the frame
 * capture format, es2/sim/present.h); its closing stats line is written at the process's
 * normal end (atexit), not on ExitProcess paths.
 *
 * DBSIM's command line: --arguments, else [game] command_line of the
 * configuration, else "-eggplant -X2 -R0 -G" (the oracle runs' fixed
 * arguments, OracleSpeed.psm1 Get-DefaultGameArguments) followed by [game]
 * arguments (the extra arguments of a replay suite).
 *
 * CW3220's _time (the -D demo pick) reads the file services' clock: [run]
 * time=<seconds since 1970> fixes it in every run; without it a headless run
 * and a windowed replay use ES2_SIM_FILES_TIME_DEFAULT, a windowed run the
 * wall clock.
 *
 * No DBSIM.EXE is read. The guest image comes from the own tables (es2/sim/image.h,
 * es2/sim/image_data.h); the game files come from the working directory (the
 * installation); ES2_DATA_ROOT is not read.
 *
 * The windowed build (//sim:es2sim_window, ES2SIM_WINDOW defined; in the web build
 * //sim:es2sim_window_wasm) runs the same code; without --headless it opens the window
 * after the DIB section and takes the messages, the input answers and the pacing of
 * virtual time from it (es2/sim/platform_window.h over es2/sim/platform_sdl.h, in the
 * web build es2/sim/platform_web.h); with --headless it is the headless run. The
 * headless build //sim:es2sim compiles none of it. Windowed, the audio device is
 * the SOS mixer over the backend's sink and MCI goes through the backend's
 * (es2/sim/audio_window.h: SDL3 and winmm, or the web platform's audio ring and
 * cdaudio); --headless keeps the silent device. ES2SIM_PERF=1 (next to the tick log) or
 * =<file> writes the wall time and the trap and dispatch counts of every frame
 * (es2/sim/perf.h); off by default, and no replay result depends on it.
 * --image-access <path> arms the image-access measurement after the image's
 * protections (es2/sim/image_access.h: CODE no-access outside the table pages, the
 * file-backed pages guarded) and writes its map (VA ranges and counts, no bytes) to
 * <path> at the end of the run; off by default, and no replay result depends on it.
 * Windowed, [input] mode=replay replays the journal:
 * the journal is the message source in front of the posted queue, the window's events
 * are dropped, virtual time is unpaced, the mixer runs on the audio device's wall time.
 * The window summary counts the SOS notices (0xa000) posted, lost and retrieved;
 * [input] message_trace=1 or ES2SIM_MESSAGE_TRACE writes the per-frame message
 * trace (both builds; off by default). main_loop.md, "the windowed notices measured,
 * and the windowed replay". */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/platform/os.h"
#include "es2/sim/host.h"
#include "es2/sim/init.h"
#include "es2/sim/files.h"
#include "es2/sim/startup.h"
#include "es2/sim/loop.h"
#include "es2/sim/input.h"
#include "es2/sim/load.h"
#include "es2/sim/audio.h"
#include "es2/sim/present.h"
#include "es2/sim/perf.h"
#include "es2/sim/trap.h"
#include "es2/sim/machine_sim.h"
#include "es2/sim/guest_memory.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include "es2/sim/image_access.h"
#include "es2/sim/host_fault.h"
#include "es2/sim/os_process.h"
#include "sim/oracle/snapshot.h"
#ifdef ES2SIM_WINDOW
#ifdef __EMSCRIPTEN__
#include "es2/sim/platform_web.h"
#include "es2/sim/audio_web.h"
#else
#include "es2/sim/platform_sdl.h"
#include "es2/sim/audio_sdl.h"
#include "es2/sim/joystick_sdl.h"
#endif
#include "es2/sim/cursor.h"
#endif

/* The guest range the entry claimed (es2/sim/os_process.h). */
static uint8_t *reserved;

static es2_sim sim;
/* The main loop, its journal and where the run stops. */
static es2_sim_loop loop;
static es2_journal journal;
static int journal_replay, have_frame_limit, have_clock_time;
static uint32_t frame_limit, clock_time;
static struct {
    const char *config, *tick_log, *arguments, *frames, *dump_frames, *dump_dir, *environment;
    int headless;
} options;
static uint16_t config_w[ES2_OS_MAX_PATH], log_w[ES2_OS_MAX_PATH];

/* The frame capture's closing line at the normal end. */
static void close_frames(void) { es2_sim_present_capture_close(es2_sim_present_bound()); }

/* The frame-time recorder, on only with ES2SIM_PERF (es2/sim/perf.h); it reads
 * the clock and the counters and writes its own file. */
static es2_sim_perf perf;
/* The message instrument (main_loop.md, "the windowed notices measured, and the
 * windowed replay"). Windowed it always counts the SOS notices (0xa000) for the
 * window summary; the trace file is written only with [input]
 * message_trace=1 or ES2SIM_MESSAGE_TRACE (both builds). Headless without the
 * switch it is not attached and nothing of it runs. */
static es2_sim_message_trace message_trace;
static int message_trace_attached;
static es2_os_handle message_trace_file = ES2_OS_INVALID_HANDLE;
static char message_trace_path[ES2_OS_MAX_PATH];
static int trace_write(void *user, const char *bytes, unsigned length) {
    uint32_t written = 0;
    return !es2_os_write_file((es2_os_handle)user, bytes, length, &written) || written != length;
}
static uint32_t trace_now(void *user) { (void)user; return es2_os_tick_count(); }
static void close_message_trace(void) {
    if (!message_trace_attached) return;
    es2_sim_message_trace_close(&message_trace);
    if (message_trace_file != ES2_OS_INVALID_HANDLE) {
        if (message_trace.failed) fprintf(stderr, "es2sim: the message trace %s could not be written\n", message_trace_path);
        es2_os_close_handle(message_trace_file);
        message_trace_file = ES2_OS_INVALID_HANDLE;
    }
}
static int finishing;
static void finish_run(const char *reason, int exit_code) {
    /* The web build's canary and heap ceiling at exit (a breach ends here
     * through guest_fault, which finishes the run itself). */
    if (!finishing) {
        finishing = 1;
        es2_sim_guest_check_frame();
    }
    close_message_trace();
    /* The image-access map (only with --image-access). */
    if (es2_sim_image_access_write(reason)) fputs("es2sim: cannot write the image-access map\n", stderr);
    if (perf.enabled) {
        es2_sim_perf_end(&perf, reason, es2_sim_trap_calls(), es2_sim_perf_dispatch_calls(&sim.dispatch), &sim.dispatch);
        es2_sim_perf_close(&perf);
    }
    es2_sim_finish(&sim, reason, exit_code);
}

static int usage(void) {
#ifdef ES2SIM_WINDOW
    fputs("usage: es2sim [--headless] [--config <ini>] [--tick-log <path>] [--environment <file>] "
          "[--arguments \"<DBSIM arguments>\"] [--frames <path>] [--dump-frames <list> --dump-dir <dir>] [--image-access <path>]\n", stderr);
    return ES2_SIM_EXIT_USAGE;
#endif
    fputs("usage: es2sim --headless [--config <ini>] [--tick-log <path>] [--environment <file>] "
          "[--arguments \"<DBSIM arguments>\"] [--frames <path>] [--dump-frames <list> --dump-dir <dir>] [--image-access <path>]\n", stderr);
    return ES2_SIM_EXIT_USAGE;
}

#ifdef ES2SIM_WINDOW
static void window_statistics(void);
#endif
static void unhandled(void *x) {
    es2_sim_os_exception r;
    es2_sim_os_exception_decode(x, &r);
    /* CODE holds only the own CODE runs and is no-access outside their pages; a
     * data access there is named, not a silent zero. */
    if (r.code == 0xc0000005u /* access violation */ && r.parameters >= 2 && r.information[0] != 8u &&
        r.information[1] >= sim.info.code_lo && r.information[1] < sim.info.code_hi)
        fprintf(stderr, "FAULT: %s of CODE 0x%08lx at %p: no own data there (CODE is no-access outside the own "
                "CODE runs, own_process.md \"The initialised DATA and the CODE bytes as own runs\")\n",
                r.information[0] == 1u ? "write" : "read", (unsigned long)r.information[1], r.address);
    fprintf(stderr, "FAULT: exception 0x%08lx at %p\n", (unsigned long)r.code, r.address);
    fflush(stderr);
#ifdef ES2SIM_WINDOW
    window_statistics();
#endif
    finish_run("fault", ES2_SIM_EXIT_FAULT);
    es2_os_exit(ES2_SIM_EXIT_FAULT);
}

static void process_fault(void *user, uint32_t code, uint32_t site);
/* A fault of own code under the guest's frame records on FS:0 (es2/sim/host_fault.h).
 * A guest entry is one of the sim backend's, the innermost named
 * (es2_sim_guest_entry_va). A read or write of the NULL page inside a guest
 * entry is the original's own fault: the accessor gives the guest's
 * address, which DBSIM's process has unmapped as well, so it ends with the
 * original's code, the entry named.
 * An integer divide fault inside a guest entry is the original's as well: the
 * core divides the operands the original divides where its contract leaves the
 * fault to the original (a zero divisor at a type load, the shield balance or
 * the HUD speed; own_process.md "The original's fault paths"). */
static void host_fault(void *x) {
    es2_sim_os_exception r;
    es2_sim_os_exception_decode(x, &r);
    const uint32_t entry = es2_sim_guest_entry_va(0);
    unsigned address;
    if (entry && es2_sim_host_fault_null_page(r.record, &address)) {
        fprintf(stderr, "FAULT: %s of 0x%08lx (the NULL page) in the entry 0x%08lx, host %p\n",
                r.information[0] == 1u ? "write" : "read", (unsigned long)address, (unsigned long)entry, r.address);
        process_fault(NULL, r.code, entry);
    }
    if (entry && es2_sim_host_fault_divide(r.record)) {
        fprintf(stderr, "FAULT: integer divide fault in the entry 0x%08lx, host %p\n", (unsigned long)entry,
                r.address);
        process_fault(NULL, r.code, entry);
    }
    unhandled(x);
}
static void own_fault_exit(void) {
    fflush(stderr);
#ifdef ES2SIM_WINDOW
    window_statistics();
#endif
    finish_run("fault", ES2_SIM_EXIT_FAULT);
    es2_os_exit(ES2_SIM_EXIT_FAULT);
}
/* The adapters' IDIVs (es2_machine_idiv) end as host_fault's divide branch,
 * an own stack overflow (es2_machine_stack_overflow) as unhandled's line. */
static void machine_fault(uint32_t code, void *user) {
    (void)user;
    const uint32_t entry = es2_sim_guest_entry_va(0);
    if (code == 0xc00000fdu) {
        fprintf(stderr, "FAULT: exception 0x%08lx in es2_machine_stack_overflow\n", (unsigned long)code);
        own_fault_exit();
    }
    if (entry) {
        fprintf(stderr, "FAULT: integer divide fault in the entry 0x%08lx, es2_machine_idiv\n", (unsigned long)entry);
        process_fault(NULL, code, entry);
    }
    fprintf(stderr, "FAULT: exception 0x%08lx in es2_machine_idiv\n", (unsigned long)code);
    own_fault_exit();
}
/* A hit of the checked guest accessor (es2/sim/guest_memory.h): the NULL
 * zone inside a guest entry as host_fault's NULL page; everything else is an
 * own fault. */
static void guest_fault(const es2_sim_guest_fault *f, void *user) {
    (void)user;
    const uint32_t entry = es2_sim_guest_entry_va(0);
    const char *access = f->write ? "write" : "read";
    if (entry && f->kind == ES2_SIM_GUEST_NULL_ZONE) {
        fprintf(stderr, "FAULT: %s of 0x%08lx (the NULL page) in the entry 0x%08lx, guest accessor\n", access,
                (unsigned long)f->va, (unsigned long)entry);
        process_fault(NULL, 0xc0000005u, entry);
    }
    if (f->kind == ES2_SIM_GUEST_CANARY)
        fprintf(stderr, "FAULT: the NULL zone holds a nonzero byte at 0x%08lx\n", (unsigned long)f->va);
    else if (f->kind == ES2_SIM_GUEST_HOST_HEAP)
        fprintf(stderr, "FAULT: the host heap reaches 0x%08lx, above 0x%08lx\n", (unsigned long)f->va,
                (unsigned long)ES2_SIM_GUEST_HOST_HEAP_CEILING);
    else
        fprintf(stderr, "FAULT: %s of %lu bytes at 0x%08lx: %s (guest accessor)\n", access, (unsigned long)f->length,
                (unsigned long)f->va, es2_sim_guest_class_name(f->kind));
    own_fault_exit();
}
/* A wasm RuntimeError of the web build (web_build.md section 6): the worker
 * resets the stack pointer and calls this export with the trap's kind
 * (ES2_MACHINE_SIM_TRAP_*). A divide trap inside a guest entry is the
 * original's divide fault, everything else an own fault. */
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void es2_wasm_trap_exit(unsigned kind);
void es2_wasm_trap_exit(unsigned kind) {
    const uint32_t entry = es2_sim_guest_entry_va(0);
    const uint32_t code = es2_machine_sim_trap_exit_code(kind, entry);
    if (code != ES2_SIM_EXIT_FAULT) {
        fprintf(stderr, "FAULT: integer divide fault in the entry 0x%08lx, wasm trap\n", (unsigned long)entry);
        process_fault(NULL, code, entry);
    }
    fprintf(stderr, "FAULT: wasm trap of kind %u, entry 0x%08lx\n", kind, (unsigned long)entry);
    own_fault_exit();
}
/* A read of a file the web VFS holds without bytes, a CD movie (web_build.md
 * section 8.1): the worker calls this export after the import threw
 * "media-bytes". The run ends as a refusal of the innermost guest entry with
 * the reason "media bytes". */
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void es2_wasm_media_exit(void);
void es2_wasm_media_exit(void) {
    es2_sim_refusal refusal = {es2_sim_guest_entry_va(0), NULL, "media bytes", 0, 0};
    ++sim.dispatch.refusals;
    if (sim.dispatch.refuse) sim.dispatch.refuse(&refusal, sim.dispatch.refuse_user);
    else fprintf(stderr, "es2sim: a read of a media file without bytes\n");
    fflush(stderr);
    finish_run("refused", ES2_SIM_EXIT_REFUSED);
    es2_os_exit(ES2_SIM_EXIT_REFUSED);
}

/* A refused call during the start-up or the exit records: the refusal is
 * already logged by the host; the run ends with ES2_SIM_EXIT_REFUSED. */
static uint32_t refused(const es2_sim_init *init, const char *phase) {
    fprintf(stderr, "es2sim: %s refused at 0x%08lx\n", phase, (unsigned long)init->current);
    finish_run("refused", ES2_SIM_EXIT_REFUSED);
    es2_sim_trap_remove();
    return ES2_SIM_EXIT_REFUSED;
}

/* The VxD open of init record 64 as a platform event in the tick log
 * (oracle_regression.py ignores events it does not know). */
static void log_platform(const es2_sim_platform_calls *platform) {
    if (!sim.log) return;
    char line[256], name[sizeof(platform->create_file_name) * 2];
    unsigned n = 0;
    for (const char *p = platform->create_file_name; *p && n + 2 < sizeof(name); ++p) {
        if (*p == '\\' || *p == '"') name[n++] = '\\';
        name[n++] = *p;
    }
    name[n] = 0;
    int length = snprintf(line, sizeof(line), "{\"event\":\"platform\",\"call\":\"CreateFileA\",\"name\":\"%s\",\"result\":\"0x%08lx\"}\r\n",
                          name, (unsigned long)platform->create_file_result);
    uint32_t written;
    if (length > 0 && length < (int)sizeof(line)) es2_os_write_file(sim.log, line, (uint32_t)length, &written);
}

/* A platform event of the start-up (a message box, the fatal report) on stderr
 * and as a tick-log line. */
static void log_event(void *user, const char *kind, const char *text) {
    (void)user;
    fprintf(stderr, "es2sim: %s: %s\n", kind, text);
    if (!sim.log) return;
    char line[700], escaped[512];
    unsigned n = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p && n + 8 < sizeof(escaped); ++p) {
        if (*p == '\\' || *p == '"') { escaped[n++] = '\\'; escaped[n++] = (char)*p; }
        else if (*p < 0x20 || *p >= 0x7f) n += (unsigned)snprintf(escaped + n, sizeof(escaped) - n, "\\u%04x", *p);
        else escaped[n++] = (char)*p;
    }
    escaped[n] = 0;
    int length = snprintf(line, sizeof(line), "{\"event\":\"platform\",\"call\":\"%s\",\"text\":\"%s\"}\r\n", kind, escaped);
    uint32_t written;
    if (length > 0 && length < (int)sizeof(line)) es2_os_write_file(sim.log, line, (uint32_t)length, &written);
}

/* DBSIM's command line (program name first) into `out`. */
static int command_line(char *out, unsigned capacity) {
    char extra[1024] = "", whole[1024] = "";
    const char *arguments = options.arguments;
    if (!arguments && options.config) {
        uint16_t w[1024];
        if (es2_os_profile_string("game", "command_line", "", w, 1024, config_w) &&
            es2_os_ansi_from_utf16(w, -1, whole, sizeof(whole)) && whole[0])
            arguments = whole;
        else if (es2_os_profile_string("game", "arguments", "", w, 1024, config_w))
            es2_os_ansi_from_utf16(w, -1, extra, sizeof(extra));
    }
    int n = arguments ? snprintf(out, capacity, "\"DBSIM.EXE\" %s", arguments)
                      : snprintf(out, capacity, "\"DBSIM.EXE\" -eggplant -X2 -R0 -G%s%s", extra[0] ? " " : "", extra);
    return n < 0 || (unsigned)n >= capacity;
}

/* ---- the loop's platform and the start-up's own steps */
static unsigned vk_scan(unsigned vk) { return es2_sim_os_vk_scan(vk); }
/* TranslateMessage's characters with an all-up key state (the injected messages
 * of a replay never change the thread's key state). ToAscii keeps the thread's
 * dead-key state as TranslateMessage does: -1 a dead key, 2 the pending dead
 * character and this key's (main_loop.md, "the shell leftovers as own rows"). */
static uint32_t translate_key(void *user, uint32_t vk, uint32_t scan) {
    (void)user;
    uint16_t out = 0;
    int n = es2_sim_os_to_ascii(vk, scan, &out);
    if (n < 0) return ES2_SIM_TRANSLATE_DEAD | (out & 0xffu);
    if (n == 1) return ES2_SIM_TRANSLATE_ONE | (out & 0xffu);
    if (n == 2) return ES2_SIM_TRANSLATE_TWO | (out & 0xffffu);
    return 0u;
}
static int delete_file(void *user, const char *name) { (void)user; return es2_os_delete_file(name) ? 1 : -1; }
static int on_tick(es2_sim_loop *l, void *user) {
    (void)user;
    /* The host's own reads (tick record, watches) are not counted; the resume
     * starts a new epoch of the measurement. */
    es2_sim_image_access_pause();
    uint32_t frame = l->memory->read32(l->memory->context, ES2_SIM_VA_FRAME_COUNTER);
    es2_sim_guest_check_frame();
    if (perf.enabled) es2_sim_perf_frame(&perf, frame, es2_sim_trap_calls(), es2_sim_perf_dispatch_calls(&sim.dispatch));
    es2_sim_tick(&sim, journal_replay ? &journal : NULL);
    es2_sim_image_access_resume();
    return have_frame_limit && frame + 1u >= frame_limit;
}
#ifdef ES2SIM_WINDOW
/* The window layer and the frame metrics the start-up's window rectangle
 * 0x4d36cc was built from. */
static es2_sim_window window_layer;
/* The windowed audio (the SOS mixer over the backend's sink, MCI through the
 * backend's). */
static es2_sim_audio_window window_audio;
#ifndef __EMSCRIPTEN__
/* The joystick over SDL3 when ES2SIM_JOYSTICK=sdl (winmm otherwise). */
static es2_sim_joystick_sdl joystick_sdl;
#endif
static uint32_t window_frame_cx, window_frame_cy, window_caption;
static int window_statistics_due;
/* The windowed replay ([input] mode=replay with a journal; main_loop.md, "the
 * windowed notices measured, and the windowed replay"). The journal stays the
 * message source in front of the posted queue as in the headless pump; the
 * window's own events are drained and dropped (no input, focus, size, close or
 * paint message reaches the game), the input answers stay the journal's, the
 * cursor rows answer as headless, and virtual time is not paced or moved by the
 * wall clock (the goldens' pace 0). The window presents, and the mixer runs on
 * the audio device's wall time and posts its notices. */
static int window_replay;
static unsigned window_replay_dropped;
static es2_sim_loop_window window_replay_ops;
static int window_replay_peek(void *user, es2_sim_loop *l, uint32_t *message, uint32_t *wparam, uint32_t *lparam) {
    (void)l; (void)message; (void)wparam; (void)lparam;
    window_replay_dropped += es2_sim_window_drain(user);
    return 0;
}
/* The window and audio lines (with the counts of the window's own paths),
 * at the start-up's end and on a refusal or fault that ends the process. */
static void window_statistics(void) {
    if (!window_statistics_due) return;
    window_statistics_due = 0;
    printf("es2sim: window: %u events, %u messages queued, %u sent, %u paints, %u presents, %u sleeps, %u paces\n",
           window_layer.events, window_layer.queued, window_layer.sent, window_layer.paints, window_layer.presents,
           window_layer.sleeps, window_layer.paces);
    printf("es2sim: window: %u key-state resyncs, %u AltGr Ctrl keys, %u keyboard-menu commands, %u cursor warps, "
           "%u scaled and %u empty presents\n", window_layer.resyncs, window_layer.altgr_ctrls, window_layer.menu_keys,
           window_layer.cursor_warps, window_layer.presents_scaled, window_layer.presents_empty);
    printf("es2sim: audio: %llu mixed buffers, %llu bytes to the sink, %llu MCI commands (%llu sent), %llu notifies (%llu delivered), "
           "%llu pumps\n", (unsigned long long)window_audio.mixer.mixed, (unsigned long long)window_audio.sink_bytes,
           (unsigned long long)window_audio.mci.calls, (unsigned long long)window_audio.mci.sent,
           (unsigned long long)window_audio.notifies, (unsigned long long)window_audio.delivered,
           (unsigned long long)window_audio.pumps);
    /* The SOS voice notices (main_loop.md, "the windowed notices measured, and the windowed replay"). */
    printf("es2sim: notices: %llu posted (%llu lost), %llu retrieved: %llu in a frame's first pump pass, %llu in modal "
           "passes (%llu modal passes in %llu frames); at most %lu in frame %lu; %u still queued\n",
           (unsigned long long)window_audio.mixer.posts, (unsigned long long)window_audio.mixer.posts_lost,
           (unsigned long long)message_trace.retrieved, (unsigned long long)message_trace.retrieved_first,
           (unsigned long long)message_trace.retrieved_modal, (unsigned long long)message_trace.modal_passes,
           (unsigned long long)message_trace.frames_modal, (unsigned long)message_trace.most_in_frame,
           (unsigned long)message_trace.most_frame, message_trace.post_count);
    {
        const es2_sim_sos_mixer *x = &window_audio.mixer;
        printf("es2sim: sos: %llu init, %llu uninit, %llu active-window, %llu starts, %llu stops, %llu stop-all, "
               "%llu user services, %llu releases, %llu acquires; at the end: window 0x%08lx, %s, %s, %lu voices (%lu listed), "
               "%lu written, %lu done, %lu outstanding (%lu bytes queued), block %lu, sink error %lu\n",
               (unsigned long long)x->calls[ES2_SOS_INIT_DRIVER], (unsigned long long)x->calls[ES2_SOS_UNINIT_DRIVER],
               (unsigned long long)x->calls[ES2_SOS_SET_ACTIVE_WINDOW], (unsigned long long)x->calls[ES2_SOS_START_SAMPLE],
               (unsigned long long)x->calls[ES2_SOS_STOP_SAMPLE], (unsigned long long)x->calls[ES2_SOS_STOP_ALL],
               (unsigned long long)x->calls[ES2_SOS_USER_SERVICE], (unsigned long long)x->calls[ES2_SOS_RELEASE_DRIVER],
               (unsigned long long)x->calls[ES2_SOS_ACQUIRE_DRIVER], (unsigned long)x->window,
               x->initialised ? "initialised" : "not initialised", x->opened ? "open" : "closed", (unsigned long)x->count,
               (unsigned long)x->list_n, (unsigned long)x->written, (unsigned long)x->done, (unsigned long)x->outstanding,
               (unsigned long)(x->sink.queued ? x->sink.queued(x->sink.context) : 0u), (unsigned long)x->block,
               (unsigned long)x->sink_error);
    }
    if (window_replay) printf("es2sim: replay: %u window events dropped (the journal is the message source)\n", window_replay_dropped);
    fflush(stdout);
}
/* An unconverted call ends the process in the trap (ExitProcess(70) after
 * the host's callback, dispatch_trap.c), so the start-up's end never prints the
 * statistics: they come after the host's log. A C call's refusal (return
 * address 0) returns to its caller and reaches the start-up's end. */
/* The window layer's sleeps (the limiter's pace, a pause's Sleep(100)) as the
 * frame's wait. */
static void (*window_delay)(void *user, uint32_t ms);
static void perf_delay(void *user, uint32_t ms) {
    es2_sim_perf_wait_begin(&perf);
    window_delay(user, ms);
    es2_sim_perf_wait_end(&perf);
}
static es2_sim_refuse_fn window_host_refuse;
static void *window_host_refuse_user;
static void window_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    if (window_host_refuse) window_host_refuse(r, window_host_refuse_user);
    if (r->return_address) window_statistics();
}
/* The process platform of the windowed run (own_process.md, "the error paths'
 * MessageBoxA, PostQuitMessage, __exit and _abort windowed"). MessageBoxA is
 * Win32's with the guest's ANSI bytes, owner, style and answer (SDL's message
 * box takes UTF-8 and has no MB_* modality), in the web build the page's
 * modal dialog; ES2SIM_MESSAGE_BOX=none (the run
 * test's switch, no desktop) prints the box and answers IDOK. __exit and _abort
 * print the statistics and the host's log, then ExitProcess(status): no atexit
 * functions (the frame capture's close_frames neither), no exit records, no
 * stream flush of the guest's streams, as CW3220's __exit. */
static char window_module[ES2_OS_MAX_PATH];
static uint32_t window_message_box(void *user, uint32_t owner, const char *text, const char *caption, uint32_t style) {
    (void)user;
    const char *mode = es2_os_getenv("ES2SIM_MESSAGE_BOX");
    uint32_t answer = ES2_SIM_IDOK;
    fflush(stdout);
    if (!mode || strcmp(mode, "none"))
#ifdef __EMSCRIPTEN__
        answer = es2_sim_window_web_message_box(text, caption, style);
#else
        answer = es2_sim_os_message_box(owner, text, caption, style);
#endif
    printf("es2sim: MessageBoxA(0x%lx, \"%s\", \"%s\", 0x%lx) = %lu%s\n", (unsigned long)owner, text, caption,
           (unsigned long)style, (unsigned long)answer, mode && !strcmp(mode, "none") ? " (not shown)" : "");
    fflush(stdout);
    return answer;
}
#endif
/* The process ends of the error paths, windowed and headless.
 * __exit and _abort print the statistics (windowed) and finish the host's
 * log, then ExitProcess(status); an unhandled fault of the original
 * (process_fault) prints a FAULT line and ends with the exception code, as
 * the OS ends the original's process after UnhandledExceptionFilter (the
 * OS's crash report and dialog are not reproduced). No atexit functions (the
 * frame capture's close neither), no exit records, no flush of the guest's
 * streams, as CW3220's __exit. */
static void process_exit(void *user, uint32_t status) {
    (void)user;
#ifdef ES2SIM_WINDOW
    if (!options.headless) window_statistics();
#endif
    printf("es2sim: %s; ExitProcess(%lu) without exit records\n", status == ES2_SIM_ABORT_STATUS ? "_abort" : "__exit",
           (unsigned long)status);
    fflush(stdout);
    finish_run(status == ES2_SIM_ABORT_STATUS ? "abort" : "fatal_error", (int)status);
    fflush(stdout);
    fflush(stderr);
    es2_os_exit(status);
}
static void process_fault(void *user, uint32_t code, uint32_t site) {
    (void)user;
    fprintf(stderr, "FAULT: guest exception 0x%08lx at 0x%08lx (the original's unhandled fault); ExitProcess(0x%08lx) "
            "without exit records\n", (unsigned long)code, (unsigned long)site, (unsigned long)code);
    fflush(stderr);
#ifdef ES2SIM_WINDOW
    if (!options.headless) window_statistics();
#endif
    finish_run("guest_fault", (int)code);
    fflush(stdout);
    fflush(stderr);
    es2_os_exit(code);
}
#ifdef ES2SIM_WINDOW
static es2_sim_process_platform window_process = {window_message_box, process_exit, window_module, NULL, process_fault};
#endif
/* D: headless, MessageBoxA keeps the headless answer (no box, IDOK,
 * counted); the process ends are the original's. */
static es2_sim_process_platform headless_process = {NULL, process_exit, "DBSIM.EXE", NULL, process_fault};
static uint32_t own_dib(void *user) {
    (void)user;
#ifdef ES2SIM_WINDOW
    if (!options.headless) {
        if (es2_sim_dib_section(&loop)) return loop.refused_va ? loop.refused_va : ES2_SIM_VA_DIB_SECTION;
        /* CreateWindowExA's window, after the DIB section as the original
         * shows it (ShowWindow, UpdateWindow): the client area is the game
         * size at the rectangle's client origin. */
        const es2_guest_memory *m = &sim.memory;
        int32_t x = (int32_t)m->read32(m->context, 0x004d36ccu), y = (int32_t)m->read32(m->context, 0x004d36d0u);
        if (es2_sim_window_open(&window_layer, (int32_t)m->read32(m->context, 0x004d253eu), (int32_t)m->read32(m->context, 0x004d2542u),
                                x + (int32_t)window_frame_cx, y + (int32_t)window_frame_cy + (int32_t)window_caption)) {
            fprintf(stderr, "es2sim: the window could not be opened: %s\n", window_layer.error);
            return ES2_SIM_VA_DIB_SECTION;
        }
        printf("es2sim: window %ldx%ld at %ld,%ld (renderer %s)\n", (long)window_layer.client_w, (long)window_layer.client_h,
               (long)window_layer.origin_x, (long)window_layer.origin_y, window_layer.open ? "on" : "off");
        if (window_layer.icon_error) printf("es2sim: no window icon: %s\n", window_layer.icon_error);
        fflush(stdout);
        return 0;
    }
#endif
    return es2_sim_dib_section(&loop) ? (loop.refused_va ? loop.refused_va : ES2_SIM_VA_DIB_SECTION) : 0u;
}
static int own_after_load(void *user, uint32_t ebp, uint32_t *refused_va) {
    (void)user;
    switch (es2_sim_main_loop(&loop, ebp)) {
    case ES2_SIM_LOOP_RETURNED: return 0;
    case ES2_SIM_LOOP_STOPPED: return 1;
    default: *refused_va = loop.refused_va; return 2;
    }
}
static uint32_t own_quit_pump(void *user) {
    (void)user;
    es2_sim_post_quit(&loop);
    for (uint32_t quit = 0; !quit;)
        if (es2_sim_pump(&loop, &quit)) return loop.refused_va ? loop.refused_va : ES2_SIM_VA_PUMP;
    return 0;
}
static int own_exit_records(void *user) { return es2_sim_run_exit_records(user); }
/* GetPrivateProfileStringA's file, as the game names it (the install root's
 * path from 0x45ee00). */
static int read_profile_file(void *user, const char *name, uint8_t *buffer, uint32_t capacity, uint32_t *size) {
    (void)user;
    es2_os_handle f = es2_os_create_file(name, ES2_OS_GENERIC_READ, ES2_OS_FILE_SHARE_READ, 0, ES2_OS_OPEN_EXISTING, 0);
    if (f == ES2_OS_INVALID_HANDLE) return 1;
    uint32_t got = 0;
    int ok = es2_os_read_file(f, buffer, capacity, &got);
    es2_os_close_handle(f);
    if (!ok) return 1;
    *size = got;
    return 0;
}
/* ---- the after-load snapshot ([snapshot] after_load=1, path, extra,
 * max_bytes_mib as the proxy reads them; mission_load.md section 13) at the
 * first call of the logic frame, written by //sim/oracle:snapshot: the
 * fixed regions, the own heap's regions (flag 128: the heap model has
 * CW3220's block layout, its attach blocks included, the environment
 * copy among them), the memory map of this
 * process, flag 32 (every hook is in place before the load). */
static int snapshot_wanted, snapshot_done;
static uint16_t snapshot_path[ES2_OS_MAX_PATH];
static es2_snapshot_region snapshot_extra[16];
static unsigned snapshot_extra_count;
static uint64_t snapshot_max_bytes = 256u << 20;
static int region_readable(void *user, uint32_t va, uint32_t n) {
    (void)user;
    uintptr_t cursor = va, end = (uintptr_t)va + n;
    if (!va || end < cursor) return 0;
    while (cursor < end) {
        es2_os_region m;
        if (!es2_os_virtual_query(cursor, &m) || m.state != ES2_OS_MEM_COMMIT ||
            (m.protect & (ES2_OS_PAGE_GUARD | ES2_OS_PAGE_NOACCESS)))
            return 0;
        uintptr_t next = m.base + m.size;
        if (next <= cursor) return 0;
        cursor = next;
    }
    return 1;
}
static void region_read(void *user, uint32_t va, void *dst, uint32_t n) {
    (void)user;
    if (n) memcpy(dst, es2_guest_memory_span(&sim.memory, va, n), n);
}
static void read_snapshot_config(void) {
    if (!options.config) return;
    snapshot_wanted = es2_os_profile_int("snapshot", "after_load", 0, config_w) == 1;
    if (!snapshot_wanted) return;
    uint16_t extra_w[512];
    char extra[512];
    uint32_t n = es2_os_profile_string("snapshot", "path", "", snapshot_path, ES2_OS_MAX_PATH, config_w);
    es2_os_profile_string("snapshot", "extra", "", extra_w, 512, config_w);
    snapshot_max_bytes = (uint64_t)(uint32_t)es2_os_profile_int("snapshot", "max_bytes_mib", 256, config_w) << 20;
    if (!n || !es2_os_ansi_from_utf16(extra_w, -1, extra, sizeof(extra)) ||
        es2_snapshot_parse_extra(extra, snapshot_extra, 16, &snapshot_extra_count)) {
        fputs("es2sim: [snapshot] path or extra unusable: no snapshot\n", stderr);
        snapshot_wanted = 0;
    }
}
static int snapshot_step(es2_sim_loop *l, void *user) {
    (void)user;
    if (!snapshot_wanted || snapshot_done) return 0;
    snapshot_done = 1;
    static es2_snapshot_map_entry map[65536];
    static es2_snapshot_module modules[512];
    static char names[512][ES2_OS_MAX_PATH];
    unsigned map_count = 0, module_count = 0;
    int truncated = 0;
    uintptr_t lowest, highest;
    es2_os_address_range(&lowest, &highest);
    for (uintptr_t cursor = lowest; cursor < highest;) {
        es2_os_region m;
        if (!es2_os_virtual_query(cursor, &m)) { truncated = 1; break; }
        uintptr_t next = m.base + m.size;
        if (next <= cursor) break;
        if (m.state == ES2_OS_MEM_COMMIT) {
            if (map_count == 65536u) truncated = 1;
            else map[map_count++] = (es2_snapshot_map_entry){(uint32_t)m.base, (uint32_t)m.size,
                                                             (uint32_t)m.allocation_base, m.protect, m.type};
            if (m.type == ES2_OS_MEM_IMAGE) {
                uint32_t base = (uint32_t)m.allocation_base;
                unsigned k = 0;
                while (k < module_count && modules[k].base != base) ++k;
                if (k == module_count) {
                    if (module_count == 512u) truncated = 1;
                    else {
                        modules[module_count].base = base;
                        modules[module_count].size = 0;
                        if (region_readable(NULL, base, 0x40)) {
                            uint32_t pe;
                            memcpy(&pe, es2_guest_memory_span(&sim.memory, base + 0x3cu, 4u), 4);
                            const uint8_t *nt = pe < 0x1000u - 0x58u && region_readable(NULL, base + pe, 0x58)
                                                    ? es2_guest_memory_span(&sim.memory, base + pe, 0x58u)
                                                    : NULL;
                            if (nt && !memcmp(nt, "PE\0\0", 4)) memcpy(&modules[module_count].size, nt + 24 + 56, 4);
                        }
                        char full[ES2_OS_MAX_PATH];
                        uint32_t got = es2_os_module_file_name(base, full, ES2_OS_MAX_PATH);
                        const char *slash = got && got < ES2_OS_MAX_PATH ? strrchr(full, '\\') : NULL;
                        snprintf(names[module_count], ES2_OS_MAX_PATH, "%s", got && got < ES2_OS_MAX_PATH ? (slash ? slash + 1 : full) : "");
                        /* The guest image is mapped, not loaded: name it as the original's module. */
                        if (base == 0x00400000u && !names[module_count][0]) snprintf(names[module_count], ES2_OS_MAX_PATH, "DBSIM.EXE");
                        modules[module_count].name = names[module_count];
                        ++module_count;
                    }
                }
            }
        }
        cursor = next;
    }
    es2_snapshot_source source = {NULL, region_readable, region_read};
    es2_snapshot_input in = {l->memory->read32(l->memory->context, ES2_SIM_VA_FRAME_COUNTER), 0x00400000u, ES2_SNAPSHOT_IMAGE_SIZE,
                             ES2_SNAPSHOT_FLAG_ARMED_EARLY | ES2_SNAPSHOT_FLAG_OWN_HEAP, snapshot_extra, snapshot_extra_count,
                             l->memory->read32(l->memory->context, ES2_SNAPSHOT_ARENA_THRESHOLD) != 0, 1, sim.heap.heap.region_count,
                             sim.heap.heap.region_base, sim.heap.heap.region_size, snapshot_max_bytes, map, map_count, modules,
                             module_count, truncated};
    uint8_t *file = NULL;
    size_t length = 0;
    int ok = !es2_snapshot_build(&source, &in, &file, &length);
    if (ok) {
        es2_os_handle f = es2_os_create_file_utf16(snapshot_path, ES2_OS_GENERIC_WRITE, ES2_OS_FILE_SHARE_READ, 0,
                                                   ES2_OS_CREATE_NEW, ES2_OS_FILE_ATTRIBUTE_NORMAL);
        uint32_t written = 0;
        ok = f != ES2_OS_INVALID_HANDLE && es2_os_write_file(f, file, (uint32_t)length, &written) && written == length;
        if (f != ES2_OS_INVALID_HANDLE && !es2_os_close_handle(f)) ok = 0;
    }
    free(file);
    log_event(NULL, "snapshot", ok ? "after_load_snapshot_written" : "after_load_snapshot_failed");
    return ok ? 0 : 1;
}
/* A frame's logic starts (the recorder), then the snapshot step. */
static int before_logic_step(es2_sim_loop *l, void *user) {
    if (perf.enabled) es2_sim_perf_work(&perf, es2_sim_trap_calls(), es2_sim_perf_dispatch_calls(&sim.dispatch));
    if (!snapshot_wanted || snapshot_done) return 0;
    es2_sim_image_access_pause();     /* the snapshot reads with the image's own protections */
    int result = snapshot_step(l, user);
    es2_sim_image_access_resume();
    return result;
}
/* `s` equals the ASCII `word` with A-Z folded (_wcsicmp in the C locale). */
static int same_word(const uint16_t *s, const char *word) {
    for (;; ++s, ++word) {
        uint16_t a = *s >= 'A' && *s <= 'Z' ? (uint16_t)(*s + 32) : *s;
        char b = *word >= 'A' && *word <= 'Z' ? (char)(*word + 32) : *word;
        if (a != (unsigned char)b) return 0;
        if (!a) return 1;
    }
}
/* [run] time as wcstoul(text, &end, 10) read it: leading white space (ASCII),
 * a sign, decimal digits up to the end, an overflow as 0xffffffff, a negative
 * value negated; refused when no digit, a character after them, a '-' first
 * or a value above 0x7fffffff. */
static int parse_seconds(const uint16_t *t, uint32_t *out) {
    unsigned i = 0;
    while (t[i] == ' ' || (t[i] >= 9u && t[i] <= 13u)) ++i;
    const int negative = t[i] == '-';
    if (t[i] == '-' || t[i] == '+') ++i;
    uint64_t v = 0;
    unsigned digits = 0;
    for (; t[i] >= '0' && t[i] <= '9'; ++i, ++digits)
        if (v <= 0xffffffffu) v = v * 10u + (uint64_t)(t[i] - '0');
    if (!digits || t[i] || t[0] == '-') return 0;
    const uint32_t seconds = v > 0xffffffffu ? 0xffffffffu : negative ? 0u - (uint32_t)v : (uint32_t)v;
    if (seconds > 0x7fffffffu) return 0;
    *out = seconds;
    return 1;
}
/* [input] mode=replay with a journal, and [run] frame_limit. */
static void read_run_config(void) {
    if (!options.config) return;
    uint16_t mode[16], path[ES2_OS_MAX_PATH];
    es2_os_profile_string("input", "mode", "off", mode, 16, config_w);
    es2_os_profile_string("input", "journal", "", path, ES2_OS_MAX_PATH, config_w);
    if (same_word(mode, "replay") && path[0]) {
        es2_os_handle f = es2_os_create_file_utf16(path, ES2_OS_GENERIC_READ, ES2_OS_FILE_SHARE_READ, 0, ES2_OS_OPEN_EXISTING, 0);
        uint32_t size = f != ES2_OS_INVALID_HANDLE ? es2_os_file_size(f) : ES2_OS_INVALID_FILE_SIZE, got = 0;
        char *text = size != ES2_OS_INVALID_FILE_SIZE ? malloc((size_t)size + 1u) : NULL;
        if (text && es2_os_read_file(f, text, size, &got) && got == size && !es2_journal_load_text(&journal, text, size, vk_scan))
            journal_replay = 1;
        else
            fputs("es2sim: [input] journal unreadable: the run has no journal\n", stderr);
        free(text);
        if (f != ES2_OS_INVALID_HANDLE) es2_os_close_handle(f);
    }
    int limit = (int)es2_os_profile_int("run", "frame_limit", -1, config_w);
    if (limit > 0) { have_frame_limit = 1; frame_limit = (uint32_t)limit; }
    uint16_t value[32];
    if (es2_os_profile_string("run", "time", "", value, 32, config_w)) {
        if (parse_seconds(value, &clock_time)) have_clock_time = 1;
        else fputs("es2sim: [run] time is not 0..2147483647: the default clock\n", stderr);
    }
}
/* _time's clock (own_process.md, "_time in es2sim"): the wall clock in a
 * windowed run that is not a replay and has no [run] time, else the
 * headless one at [run] time or ES2_SIM_FILES_TIME_DEFAULT. */
static void select_clock(es2_sim_files *files) {
    if (have_clock_time) es2_sim_files_set_time(files, clock_time);
#ifdef ES2SIM_WINDOW
    if (!options.headless && !journal_replay && !have_clock_time) {
        files->os = es2_sim_files_os_wall_clock;
        char *environment = es2_os_environment();
        es2_io_tzset(&files->env, &files->layout, (uint32_t)(uintptr_t)environment);
        es2_os_environment_free(environment);
        puts("es2sim: _time clock: the wall clock (GetLocalTime, the time zone)");
        return;
    }
#endif
    printf("es2sim: _time clock: %lu seconds since 1970%s\n", (unsigned long)(have_clock_time ? clock_time : ES2_SIM_FILES_TIME_DEFAULT),
           have_clock_time ? " ([run] time)" : " (the default)");
}
/* The message trace's file: [input] message_trace=1 (the proxy's key) or
 * ES2SIM_MESSAGE_TRACE=1 put it beside the tick log as <tick
 * log>.messages.ndjson; any other nonempty value of the variable but 0 is the
 * path itself. Created new: an existing file, or no tick log for the default
 * name, refuses the trace (one stderr line) and the run goes on without it. 1
 * when a trace file is open. */
static int open_message_trace(void) {
    const char *env = es2_os_getenv("ES2SIM_MESSAGE_TRACE");
    int wanted = options.config && es2_os_profile_int("input", "message_trace", 0, config_w) == 1;
    const char *path = NULL;
    if (env && *env && strcmp(env, "0")) { wanted = 1; if (strcmp(env, "1")) path = env; }
    if (!wanted) return 0;
    if (!path) {
        if (!options.tick_log ||
            snprintf(message_trace_path, sizeof(message_trace_path), "%s.messages.ndjson", options.tick_log) >= (int)sizeof(message_trace_path)) {
            fputs("es2sim: message trace refused: no tick log to put it beside\n", stderr);
            return 0;
        }
    } else if (snprintf(message_trace_path, sizeof(message_trace_path), "%s", path) >= (int)sizeof(message_trace_path)) {
        fputs("es2sim: message trace refused: the path is too long\n", stderr);
        return 0;
    }
    message_trace_file = es2_os_create_file(message_trace_path, ES2_OS_GENERIC_WRITE, ES2_OS_FILE_SHARE_READ, 0,
                                            ES2_OS_CREATE_NEW, ES2_OS_FILE_ATTRIBUTE_NORMAL);
    if (message_trace_file == ES2_OS_INVALID_HANDLE) {
        fprintf(stderr, "es2sim: message trace refused: %s cannot be created new (Win32 %lu)\n", message_trace_path,
                (unsigned long)es2_os_last_error());
        return 0;
    }
    printf("es2sim: message trace to %s\n", message_trace_path);
    return 1;
}

static uint32_t simulation_thread(void) {
    char path[ES2_OS_MAX_PATH];
    /* ES2SIM_PERF switches the recorder on (off by default). */
    if (es2_sim_perf_path(es2_os_getenv("ES2SIM_PERF"), options.tick_log, path, sizeof(path))) {
#ifdef ES2SIM_WINDOW
        const char *perf_mode = options.headless ? "headless" : "window";
#else
        const char *perf_mode = "headless";
#endif
        if (es2_sim_perf_open(&perf, perf_mode, path)) fprintf(stderr, "es2sim: cannot write the frame times %s\n", path);
        else printf("es2sim: frame times (%s) to %s\n", perf_mode, path);
    }
    int code = es2_sim_map(&sim, reserved);
    if (code) { fprintf(stderr, "es2sim: image refused: %s\n", sim.error); return (uint32_t)code; }
    printf("es2sim: built the guest image from the own tables at 0x%08lx (no DBSIM.EXE read; CODE 0x%08lx..0x%08lx "
           "not executable, no-access outside the own CODE runs' pages, %u import slots trapped; relocations not applied)\n",
           (unsigned long)sim.info.image_base, (unsigned long)sim.info.code_lo, (unsigned long)sim.info.code_hi,
           sim.import_count);
    code = es2_sim_setup(&sim, options.config ? config_w : NULL, options.tick_log ? log_w : NULL);
    if (code) { fprintf(stderr, "es2sim: set-up failed: %s\n", sim.error); return (uint32_t)code; }
    printf("es2sim: dispatch %u entries (registry hooks %u, services %u), heap base preference 0x%08lx, "
           "virtual time step %lu ms, DEP flags 0x%lx%s\n", sim.dispatch.count, es2_sim_adapter_count(),
           es2_sim_service_count(), (unsigned long)ES2_SIM_HEAP_PREFERRED_BASE, (unsigned long)sim.vt.step,
           sim.dep_flags, sim.dep_permanent ? " (permanent)" : "");
    /* CW3220's attach before DBSIM's entry: the environment copy, the file
     * services (CW3220's I/O variables in a region of their own, its stream
     * buffers on the game's heap), the C++ static objects; then the heap trace. */
    static es2_sim_files files;
    uint8_t *region = (uint8_t *)es2_os_virtual_alloc(0, ES2_SIM_FILES_REGION_SIZE, ES2_OS_MEM_COMMIT | ES2_OS_MEM_RESERVE,
                                                      ES2_OS_PAGE_READWRITE);
    code = region ? es2_sim_runtime_attach(&sim, &files, (uint32_t)(uintptr_t)region, options.config ? config_w : NULL)
                  : ES2_SIM_EXIT_SETUP;
    if (code) {
        fprintf(stderr, "es2sim: runtime attach failed: %s\n", region ? sim.error : "runtime region");
        finish_run("setup", ES2_SIM_EXIT_SETUP);
        es2_sim_trap_remove();
        return ES2_SIM_EXIT_SETUP;
    }
    /* The c0w32 entry (StackBase = this thread's, as on the original's main
     * thread), then the init records in selector order. */
    es2_sim_init init = {&sim.memory, &sim.dispatch, es2_sim_os_stack_base(), 0, 0, 0};
    es2_sim_platform_calls_reset();
    if (es2_sim_c0w32_entry(&init) || es2_sim_run_init_records(&init)) return refused(&init, "start-up");
    const es2_sim_platform_calls *platform = es2_sim_platform_calls_get();
    printf("es2sim: c0w32 entry and %u init records run; stage registry %d entries; CreateFileA(\"%s\") = 0x%08lx "
           "(headless platform)\n", init.init_run,
           (int)(int16_t)sim.memory.read16(sim.memory.context, ES2_SIM_VA_STAGE_COUNT), platform->create_file_name,
           (unsigned long)platform->create_file_result);
    log_platform(platform);
    /* The start-up on the file services set up above. */
    static es2_sim_startup start;
    static char line[1100];
    uint8_t *frame = (uint8_t *)es2_os_virtual_alloc(0, 0x4000, ES2_OS_MEM_COMMIT | ES2_OS_MEM_RESERVE, ES2_OS_PAGE_READWRITE);
    uint8_t *text = (uint8_t *)es2_os_virtual_alloc(0, sizeof(line), ES2_OS_MEM_COMMIT | ES2_OS_MEM_RESERVE, ES2_OS_PAGE_READWRITE);
    if (!frame || !text || command_line(line, sizeof(line))) {
        fputs("es2sim: start-up set-up failed (frame, command line)\n", stderr);
        finish_run("setup", ES2_SIM_EXIT_SETUP);
        es2_sim_trap_remove();
        return ES2_SIM_EXIT_SETUP;
    }
    es2_sim_files_bind(&files);
    /* The loop: its call area, the DIB bits region (the largest DIB, 1280 x
     * 960), the journal, the tick callback. */
    es2_journal_init(&journal);
    read_run_config();
    read_snapshot_config();
    select_clock(&files);
#ifdef ES2SIM_WINDOW
    /* Windowed, [input] replay keeps the journal as the message source. */
    window_replay = !options.headless && journal_replay;
#endif
    es2_sim_loop_defaults(&loop);
    uint8_t *loop_stack = (uint8_t *)es2_os_virtual_alloc(0, 0x10000, ES2_OS_MEM_COMMIT | ES2_OS_MEM_RESERVE, ES2_OS_PAGE_READWRITE);
    uint8_t *bits = (uint8_t *)es2_os_virtual_alloc(0, 1280u * 960u, ES2_OS_MEM_COMMIT | ES2_OS_MEM_RESERVE, ES2_OS_PAGE_READWRITE);
    if (!loop_stack || !bits) {
        fputs("es2sim: loop set-up failed (call area, DIB region)\n", stderr);
        finish_run("setup", ES2_SIM_EXIT_SETUP);
        es2_sim_trap_remove();
        return ES2_SIM_EXIT_SETUP;
    }
    /* The modelled guest stack at its fixed place (es2/sim/machine_sim.h),
     * after the host regions above, so that a fallback cannot move them. */
    uint8_t *guest_stack = (uint8_t *)es2_os_virtual_alloc(ES2_MACHINE_SIM_STACK_LIMIT, ES2_MACHINE_SIM_STACK_SIZE,
                                                           ES2_OS_MEM_COMMIT | ES2_OS_MEM_RESERVE, ES2_OS_PAGE_READWRITE);
    if (!guest_stack)
        guest_stack = (uint8_t *)es2_os_virtual_alloc(0, ES2_MACHINE_SIM_STACK_SIZE, ES2_OS_MEM_COMMIT | ES2_OS_MEM_RESERVE,
                                                      ES2_OS_PAGE_READWRITE);
    if (!guest_stack) {
        fputs("es2sim: guest stack set-up failed\n", stderr);
        finish_run("setup", ES2_SIM_EXIT_SETUP);
        es2_sim_trap_remove();
        return ES2_SIM_EXIT_SETUP;
    }
    es2_machine_sim_bind(&sim.dispatch, (uint32_t)(uintptr_t)guest_stack,
                         (uint32_t)(uintptr_t)guest_stack + ES2_MACHINE_SIM_STACK_SIZE);
    loop.memory = &sim.memory;
    loop.dispatch = &sim.dispatch;
    loop.vt = &sim.vt;
    loop.journal = journal_replay ? &journal : NULL;
    loop.stack_lo = (uint32_t)(uintptr_t)loop_stack;
    loop.stack_hi = (uint32_t)(uintptr_t)loop_stack + 0x10000u;
    loop.platform.bits = (uint32_t)(uintptr_t)bits;
    loop.platform.bits_size = 1280u * 960u;
    loop.platform.translate = translate_key;
    loop.tick = on_tick;
    loop.before_logic = before_logic_step;
    loop.delete_file = delete_file;
    es2_sim_loop_bind(&loop);
    /* The message instrument: with the trace switch, and windowed always (the
     * notice counts of the window summary). */
    {
        int traced = open_message_trace();
        int windowed = 0;
#ifdef ES2SIM_WINDOW
        windowed = !options.headless;
#endif
        if (traced || windowed) {
            es2_sim_message_trace_attach(&message_trace, &loop, traced ? trace_write : NULL, traced ? (void *)message_trace_file : NULL);
            message_trace.now = trace_now;
            message_trace_attached = 1;
        }
    }
    /* The input layer over the loop, headless from the journal; the client area
     * is the game size. */
    static es2_sim_input input;
    es2_sim_input_headless(&input, &loop, journal_replay ? &journal : NULL, 0, 0);
    input.read_file = read_profile_file;
    es2_sim_input_bind(&input);
    /* The mission load's guest callees over the loop (headless display answers). */
    static es2_sim_load load;
    es2_sim_load_headless(&load, &loop);
    es2_sim_load_bind(&load);
    /* Audio over the loop with the silent device (no SOS9503.DLL, no CD audio;
     * the samples' playing time on the virtual clock). */
    static es2_sim_audio_silent silent;
    static es2_sim_audio audio;
    static es2_sim_audio_device device;
    es2_sim_audio_silent_init(&silent, loop.vt);
    es2_sim_audio_silent_device(&silent, &device);
#ifdef ES2SIM_WINDOW
    /* Windowed, the mixing device and the CD through winmm; with --headless the
     * silent device as above. */
    if (!options.headless) {
        if (es2_sim_audio_window_init(&window_audio, loop.vt, NULL, NULL, NULL, 1))
            fprintf(stderr, "es2sim: %s; CD music without MM_MCINOTIFY\n", window_audio.error);
        es2_sim_audio_window_device(&window_audio, &device);
    }
#endif
    es2_sim_audio_headless(&audio, &loop, &device);
    es2_sim_audio_bind(&audio);
    /* The presentation frame headless; --frames: the frame capture. */
    static es2_sim_present present;
    es2_sim_present_headless(&present, &loop);
    es2_sim_present_bind(&present);
    if (options.frames) {
        if (es2_sim_present_capture_open(&present, options.frames)) {
            fprintf(stderr, "es2sim: cannot write the frame capture %s\n", options.frames);
            return ES2_SIM_EXIT_SETUP;
        }
        atexit(close_frames);
    }
    /* --dump-frames <list> --dump-dir <dir>: ES2F dumps of those frames (decoded game frames: ignored scratch only). */
    if (options.dump_frames && es2_sim_present_dump_open(&present, options.dump_dir, options.dump_frames)) {
        fprintf(stderr, "es2sim: unusable --dump-frames %s or --dump-dir %s\n", options.dump_frames, options.dump_dir);
        return ES2_SIM_EXIT_SETUP;
    }
    memcpy(text, line, strlen(line) + 1);
    memset(&start, 0, sizeof(start));
    es2_sim_startup_defaults(&start);
#ifdef ES2SIM_WINDOW
    if (!options.headless) {
        /* The window layer over the loop, the input and the present; the
         * desktop and frame metrics from the system. */
        const es2_sim_input_platform journal_input = input.platform;
        es2_sim_window_init(&window_layer, &loop, &input, &present);
#ifdef __EMSCRIPTEN__
        /* MM_MCINOTIFY arrives in the input ring (es2/sim/platform_web.h). */
        es2_sim_window_web_mci(&window_layer, es2_sim_audio_web_notify, &window_audio);
#endif
        if (perf.enabled) { window_delay = window_layer.delay_ms; window_layer.delay_ms = perf_delay; }
        if (window_replay) {
            /* The replay's seams (window_replay above): the journal's input
             * answers, the headless cursor rows, the window's SetCursor and
             * Sleep, no clock, no pace, the loop's own DefWindowProcA. */
            input.platform = journal_input;
            es2_sim_cursor_bind(NULL);
            window_replay_ops = window_layer.ops;
            window_replay_ops.peek = window_replay_peek;
            window_replay_ops.clock = NULL;
            window_replay_ops.pace = NULL;
            window_replay_ops.def_window_proc = NULL;
            loop.window = &window_replay_ops;
            printf("es2sim: windowed replay of the input journal (the window's events are dropped, virtual time unpaced)\n");
        }
        es2_sim_audio_window_attach(&window_audio, &loop);
        /* The SOS voice notices (0xa000) into the pump's queue, through
         * the message instrument's count. */
        window_audio.mixer.post = es2_sim_message_trace_post;
        window_audio.mixer.post_user = &message_trace;
        /* The statistics before a refusal's ExitProcess. */
        window_host_refuse = sim.dispatch.refuse;
        window_host_refuse_user = sim.dispatch.refuse_user;
        sim.dispatch.refuse = window_refuse;
        window_statistics_due = 1;
        /* The boxes and the process ends of the error paths;
         * GetModuleFileNameA(NULL)'s file name is the original's, DBSIM.EXE (a
         * constant; no file path is read). */
        snprintf(window_module, sizeof(window_module), "%s", "DBSIM.EXE");
        loop.process = &window_process;
        start.process = &window_process;
        window_layer.cursor_wait = start.platform.cursor_wait;
        /* The class icon, LoadIconA(hInstance, 0x4a084d) at 0x465111, the name
         * read where the original reads it. */
        {
            const uint32_t name_rva = 0x004a084du - sim.info.image_base;
            if (name_rva < sim.info.image_size && memchr(sim.image + name_rva, 0, sim.info.image_size - name_rva))
                window_layer.icon_name = (const char *)sim.image + name_rva;
        }
        if (es2_sim_window_video_init(&window_layer)) {
            fprintf(stderr, "es2sim: %s\n", window_layer.error);
            finish_run("setup", ES2_SIM_EXIT_SETUP);
            es2_sim_trap_remove();
            return ES2_SIM_EXIT_SETUP;
        }
#ifndef __EMSCRIPTEN__
        if (es2_sim_joystick_select(&window_layer.joystick, &joystick_sdl, es2_os_getenv("ES2SIM_JOYSTICK"))) {
            fprintf(stderr, "es2sim: %s\n", joystick_sdl.error);
            es2_sim_window_close(&window_layer);
            finish_run("setup", ES2_SIM_EXIT_SETUP);
            es2_sim_trap_remove();
            return ES2_SIM_EXIT_SETUP;
        }
#endif
        es2_sim_window_metrics(&start.platform.screen_cx, &start.platform.screen_cy, &start.platform.frame_cx,
                               &start.platform.frame_cy, &start.platform.caption);
        window_frame_cx = start.platform.frame_cx;
        window_frame_cy = start.platform.frame_cy;
        window_caption = start.platform.caption;
        printf("es2sim: windowed (" ES2_SIM_WINDOW_PLATFORM "): screen %lux%lu, frame %lu/%lu, caption %lu\n", (unsigned long)start.platform.screen_cx,
               (unsigned long)start.platform.screen_cy, (unsigned long)start.platform.frame_cx,
               (unsigned long)start.platform.frame_cy, (unsigned long)start.platform.caption);
    }
#endif
    if (!loop.process) {
        loop.process = &headless_process;
        start.process = &headless_process;
    }
    start.memory = &sim.memory;
    start.dispatch = &sim.dispatch;
    /* 0x1000 bytes above the frame area's top stay free: a guard for the
     * unbounded %s reads of drive.cfg, whose lengths the start-up checks
     * after the reads. */
    start.frame_lo = (uint32_t)(uintptr_t)frame;
    start.frame_hi = (uint32_t)(uintptr_t)frame + 0x3000u;
    start.command_line = (uint32_t)(uintptr_t)text;
    start.event = log_event;
    start.defer_dib = 0;
    start.dib_section = own_dib;
    start.after_load = own_after_load;
    start.quit_pump = own_quit_pump;
    printf("es2sim: command line %s\n", line);
    fflush(stdout);
    es2_sim_startup_outcome outcome = es2_sim_startup_run(&start);
#ifdef ES2SIM_WINDOW
    if (!options.headless) {
        window_statistics();
        es2_sim_audio_window_close(&window_audio);
        es2_sim_window_close(&window_layer);
#ifndef __EMSCRIPTEN__
        es2_sim_joystick_sdl_close(&joystick_sdl);
#endif
    }
#endif
    printf("es2sim: start-up: argc %u, %u archives registered, outcome %d; %llu loop passes, %lu ticks, %u peeks\n",
           (unsigned)start.argc, start.archives, (int)outcome, (unsigned long long)loop.passes, sim.ticks,
           loop.platform.peeks);
    if (outcome == ES2_SIM_STARTUP_REFUSED) {
        init.current = start.refused_va;
        return refused(&init, "start-up");
    }
    if (outcome == ES2_SIM_STARTUP_STOPPED) {
        /* [run] frame_limit: the run ends after that tick. */
        printf("es2sim: stopped at frame_limit %lu after %lu ticks\n", (unsigned long)frame_limit, sim.ticks);
        fflush(stdout);
        finish_run("frame_limit", 0);
        es2_sim_trap_remove();
        return 0;
    }
    if (outcome == ES2_SIM_STARTUP_EXIT) {
        /* __exit(1): no atexit functions, no exit records, no stream flush.
         * The record's process_exit ends the process first; this
         * stays for a record whose process_exit returns. */
        printf("es2sim: fatal report; exit code %lu without exit records\n", (unsigned long)start.exit_code);
        fflush(stdout);
        finish_run("fatal_error", (int)start.exit_code);
        es2_sim_trap_remove();
        return (uint32_t)start.exit_code;
    }
    /* WinMain returned: CW3220's exit runs the atexit functions, the exit
     * records and the stream exit hooks; the process exits with WinMain's
     * result. */
    if (es2_sim_exit(&loop, own_exit_records, &init)) {
        if (loop.refused_va) init.current = loop.refused_va;
        return refused(&init, "exit");
    }
    int exit_code = (int)start.exit_code;
    printf("es2sim: WinMain returned; %u exit records run; exit code %d\n", init.exit_run, exit_code);
    fflush(stdout);
    finish_run("winmain_returned", exit_code);
    es2_sim_trap_remove();
    return (uint32_t)exit_code;
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (!strcmp(a, "--headless")) options.headless = 1;
        else if (!strcmp(a, "--config") && i + 1 < argc) options.config = argv[++i];
        else if (!strcmp(a, "--tick-log") && i + 1 < argc) options.tick_log = argv[++i];
        else if (!strcmp(a, "--arguments") && i + 1 < argc) options.arguments = argv[++i];
        else if (!strcmp(a, "--frames") && i + 1 < argc) options.frames = argv[++i];
        else if (!strcmp(a, "--dump-frames") && i + 1 < argc) options.dump_frames = argv[++i];
        else if (!strcmp(a, "--dump-dir") && i + 1 < argc) options.dump_dir = argv[++i];
        else if (!strcmp(a, "--environment") && i + 1 < argc) options.environment = argv[++i];
        else if (!strcmp(a, "--image-access") && i + 1 < argc) { if (es2_sim_image_access_configure(argv[++i])) return usage(); }
        else return usage();
    }
#ifndef ES2SIM_WINDOW
    if (!options.headless) return usage();
#endif
    if (!options.dump_frames != !options.dump_dir) return usage();
    if (options.config && !es2_os_utf16_from_ansi(options.config, -1, config_w, ES2_OS_MAX_PATH)) return usage();
    if (options.tick_log && !es2_os_utf16_from_ansi(options.tick_log, -1, log_w, ES2_OS_MAX_PATH)) return usage();
    /* The environment block the run reads (es2_os_environment_load) in place
     * of the process's, before anything reads a variable. */
    if (options.environment && es2_os_environment_load(options.environment)) {
        fprintf(stderr, "es2sim: --environment %s is not an environment block\n", options.environment);
        return usage();
    }
    uint32_t reservation_error = 0;
    reserved = es2_sim_os_reserved(&reservation_error);
    if (!reserved) {
        es2_os_set_last_error(reservation_error);
        fprintf(stderr, "es2sim: the range 0x00400000..0x006d7fff could not be claimed (Win32 %lu)\n",
                (unsigned long)reservation_error);
        return ES2_SIM_EXIT_CLAIM;
    }
    es2_sim_os_on_unhandled(unhandled);
    if (es2_sim_host_fault_install(host_fault)) return ES2_SIM_EXIT_SETUP;
    es2_machine_sim_on_fault(machine_fault, NULL);
    es2_sim_guest_on_fault(guest_fault, NULL);
    uint32_t result = ES2_SIM_EXIT_SETUP;
    return es2_sim_os_run_thread(simulation_thread, 0x1000000u, &result) ? (int)result : ES2_SIM_EXIT_SETUP;
}
