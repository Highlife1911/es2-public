/* DBSIM's audio as own code (audio.md, "audio in the own process"): the SOS
 * driver load and initialisation 0x473708 (with 0x4958fd and 0x473eac) and its
 * shutdown, the sound table init 0x46254c, the SFX manager below the sfx hooks
 * (open 0x463a14, close 0x463c50, cache 0x463d4c, play 0x464038, stop 0x464118,
 * done 0x4641e8, volume 0x464618, rate 0x46466c, pan 0x4646dc, loops, priority,
 * eviction 0x464390, stop all 0x4648e0) with the SOS glue 0x473828..0x474b2e,
 * the voice start 0x462940/0x462b9c with the LRU channel 0x462b30 and the lip
 * file 0x463374, the per-frame lip pass 0x4632c4, the window handler 0x462398,
 * CD audio 0x46313c/0x464858.. over MCI, pause and resume, the prefs sound
 * groups and the sound close 0x462734.
 *
 * SOS9503.DLL is not loaded. Every SOS entry point DBSIM calls through its
 * stubs 0x495a43..0x496300 goes to `device` instead (es2_sim_sos_call names
 * the stub), every mciSendCommandA to `device.mci`. Headless the device is
 * silent (es2_sim_audio_silent): it accepts every call, answers the
 * detection as a wave device with every PCM format, hands out sample handles
 * and reports a sample done once its playing time has passed on the virtual
 * clock; MCI answers "no CD audio device". A windowed host gives its own
 * device (SDL3) behind the same seam.
 *
 * The original's GlobalAlloc blocks (sample memory) come from `platform`:
 * headless host memory (guest-addressable in es2sim.exe), in tests an arena.
 * Guest state is read and written only through the bound main loop's guest
 * view; every call of other DBSIM code goes through the dispatch. */
#ifndef ES2_SIM_AUDIO_H
#define ES2_SIM_AUDIO_H
#include <stdint.h>
#include "es2/sim/loop.h"

/* The SOS entry points DBSIM reaches (stub VA in the comment). The stubs
 * pass words zero-extended; `args` are the stub's arguments in order. */
typedef enum es2_sim_sos_call {
    ES2_SOS_DETECT_INIT = 0,      /* 0x495ba3 (device, 0) */
    ES2_SOS_DETECT_FIND_FIRST,    /* 0x495bc5 (capabilities 0x6b5590) */
    ES2_SOS_DETECT_UNINIT,        /* 0x495bb9 () */
    ES2_SOS_INIT_SYSTEM,          /* 0x495a43 (0, 0) */
    ES2_SOS_TIMER_INIT,           /* 0x495a8c (0) */
    ES2_SOS_INIT_DRIVER,          /* 0x495a65 (capabilities, &handle word 0x6b566e) */
    ES2_SOS_SET_ACTIVE_WINDOW,    /* 0x495b7c (driver, window) */
    ES2_SOS_STOP_ALL,             /* 0x495ead (driver) */
    ES2_SOS_UNINIT_DRIVER,        /* 0x495a79 (driver) */
    ES2_SOS_TIMER_UNINIT,         /* 0x495a9f () */
    ES2_SOS_UNINIT_SYSTEM,        /* 0x495a59 () */
    ES2_SOS_START_SAMPLE,         /* 0x495e7c (driver, sample structure, &sample handle word) */
    ES2_SOS_STOP_SAMPLE,          /* 0x495e95 (driver, sample handle) */
    ES2_SOS_SAMPLE_DONE,          /* 0x495e64 (driver, sample handle): nonzero = done */
    ES2_SOS_SET_VOLUME,           /* 0x495d1f (driver, sample handle, volume dword) */
    ES2_SOS_SET_PAN,              /* 0x495d9f (driver, sample handle, pan) */
    ES2_SOS_SET_RATE,             /* 0x495dd4 (driver, sample handle, rate) */
    ES2_SOS_USER_SERVICE,         /* 0x495b2b (driver) */
    ES2_SOS_ACQUIRE_DRIVER,       /* 0x495be7 (driver) */
    ES2_SOS_RELEASE_DRIVER,       /* 0x495bfa (driver, 0x1000) */
    ES2_SOS_CALL_COUNT
} es2_sim_sos_call;

typedef struct es2_sim_audio_device {
    void *context;
    /* One SOS call; returns its EAX. The registers EDX and ECX after a driver
     * call are not modelled: the own code keeps the values they had at the
     * stub's call (audio.md, "audio in the own process", open point). */
    uint32_t (*sos)(void *context, const es2_guest_memory *m, es2_sim_sos_call call, const uint32_t *args, unsigned count);
    /* mciSendCommandA(device, message, flags, parameter block); returns its EAX. */
    uint32_t (*mci)(void *context, const es2_guest_memory *m, uint32_t device, uint32_t message, uint32_t flags, uint32_t params);
} es2_sim_audio_device;

/* The silent device: every call accepted, nothing audible. */
#define ES2_SIM_SILENT_SAMPLES 32u
typedef struct es2_sim_audio_silent {
    const es2_vt *vt;                          /* the virtual clock (peeked, never read: no clock call) */
    uint32_t formats;                          /* the WAVEOUTCAPS dwFormats it reports (0xfff: every PCM format) */
    uint32_t mci_open_error;                   /* MCI_OPEN's answer (306: no CD audio device) */
    struct { int used; uint32_t start_ms, length_ms; } sample[ES2_SIM_SILENT_SAMPLES];
    uint64_t calls[ES2_SOS_CALL_COUNT], mci_calls;
} es2_sim_audio_silent;
void es2_sim_audio_silent_init(es2_sim_audio_silent *s, const es2_vt *vt);
void es2_sim_audio_silent_device(es2_sim_audio_silent *s, es2_sim_audio_device *device);

typedef struct es2_sim_audio_platform {
    /* GlobalAlloc(flags, size) / GlobalFree(handle) (GMEM_FIXED: handle =
     * pointer, GlobalLock returns it). Headless: host memory. */
    uint32_t (*global_alloc)(void *user, uint32_t flags, uint32_t size);
    uint32_t (*global_free)(void *user, uint32_t handle);
    void *user;
    /* GetVersion's answer (headless: a Windows NT value, bit 31 clear). */
    uint32_t version;
    /* Recorded, not acted on. */
    uint32_t last_error;
    unsigned set_last_error, global_allocs, global_frees, error_reports;
    char last_report[160];                     /* the text 0x4649b8 formats into 0x4d2ef0 */
} es2_sim_audio_platform;

typedef struct es2_sim_audio {
    es2_sim_loop *loop;
    es2_sim_audio_device device;
    es2_sim_audio_platform platform;
    uint32_t refused_va;
} es2_sim_audio;

/* Zeroes the record, sets the headless platform (host GlobalAlloc, the NT
 * version) and the given device. */
void es2_sim_audio_headless(es2_sim_audio *audio, es2_sim_loop *loop, const es2_sim_audio_device *device);
/* The instance the service rows use (one per process). */
void es2_sim_audio_bind(es2_sim_audio *audio);

/* The test seam (in vitro): an internal function of the bound instance by
 * its original VA (0x463a14 open, 0x463c50 close, 0x463d4c cache, 0x4641e8
 * done, 0x4647b8 priority, 0x46474c loops, 0x464390 evict, 0x4648e0 stop
 * all, 0x4737a4 driver shutdown, 0x462b30 the LRU channel); EAX in *out.
 * 0, or 1 when refused or the VA is not one of them. */
int es2_sim_audio_internal(uint32_t va, const uint32_t *args, unsigned count, const uint32_t regs[3], es2_sim_result *out);

/* ---- the SOS software mixer and the mixing device (audio.md, "The
 * mixing device"). SOS9503.DLL's digital driver as own code behind the same
 * seam: the voices DBSIM starts are mixed as the DLL's mixer 0x406b90 mixes
 * them (volume, pan, rate, loops, the routines of table 0x40730c, the output
 * conversion of table 0x40924c) into buffers of the format the caps record asks
 * for; a sink plays them (the SDL3 sink in audio_sdl.h, a fake one in the
 * tests). Detection and MCI stay the silent device's answers. Headless es2sim
 * keeps the silent device. */
#define ES2_SIM_SOS_VOICES 32u
#define ES2_SIM_SOS_RING 64u           /* the driver's buffer ring (+882h, 64 entries) */
#define ES2_SIM_SOS_BLOCK_MAX 0x10000u /* the block is a word (+64h) */
#define ES2_SIM_SOS_DIV_SITE 0x00406cf0u /* SOS9503.DLL's resample DIV at its link address */

/* A voice: the fields of the DLL's 0xFC-byte copy of the sample structure
 * that the mixer and the calls use (DLL offsets in the comments). */
typedef struct es2_sim_sos_voice {
    uint32_t data;      /* +0   first sample byte (guest address) */
    uint32_t pos;       /* +8   current position */
    uint32_t length;    /* +10h bytes */
    uint32_t flags;     /* +24h 8000h used, 4000h ended or stopped, 800h noticed, 2000h removed */
    uint32_t volume;    /* +30h left word | right word << 16 */
    uint32_t loops;     /* +34h repeats left (FFFFFFFFh endless) */
    uint32_t rate;      /* +38h Hz */
    uint32_t bits;      /* +3Ch 8 or 16 */
    uint32_t channels;  /* +40h 1 or 2 */
    uint32_t format;    /* +44h 8000h unsigned 8-bit data */
    uint32_t pan;       /* +48h 8000h centre */
    uint32_t samples;   /* +5Ch the sample counter */
    uint32_t done_at;   /* +70h the written-buffer count when the end was noticed (FFFFFFFFh none) */
} es2_sim_sos_voice;

/* Where the mixed buffers go (waveOutOpen/Write/Reset/Close). */
typedef struct es2_sim_sos_sink {
    void *context;
    /* Opens the output: rate Hz, channels 1/2, bits 8 (unsigned bytes) or
     * 16 (signed little-endian words). 0 = open, else an MMRESULT-like code. */
    uint32_t (*open)(void *context, uint32_t rate, uint32_t channels, uint32_t bits);
    /* Queues one buffer; 0 = queued. */
    uint32_t (*write)(void *context, const uint8_t *data, uint32_t bytes);
    /* Bytes queued and not yet played. */
    uint32_t (*queued)(void *context);
    /* Drops everything queued (waveOutReset). */
    void (*reset)(void *context);
    void (*close)(void *context);
    /* The ring runs dry (audio.md, "the ring runs dry as waveOut's does"): the input has
     * ended for now, so every byte queued becomes playable and `queued` can
     * fall to 0 (SDL_FlushAudioStream; a resampling SDL stream otherwise holds
     * the last frames back). The mixer calls it after a buffer mixed with no
     * voice listed, the last write before the ring drains. NULL: nothing to do
     * (waveOut, the tests' sinks). */
    void (*flush)(void *context);
} es2_sim_sos_sink;

typedef struct es2_sim_sos_mixer {
    es2_sim_audio_silent silent;             /* detection and MCI answers */
    es2_sim_sos_sink sink;
    int initialised, opened;                 /* driver word +0 bit 1000h; the sink open (released: closed) */
    uint32_t rate, channels, bits, out_flags; /* driver +4, +8, +0Ch, +10h (8000h: unsigned 8-bit output) */
    uint32_t block, buffers;                 /* +64h bytes per buffer, +62h buffer count */
    /* caps +70h, copied to +A4Ch (0x4180b8) by the init: bit 0 the service
     * thread (WOM_DONE resumes it, 0x403055, and the user service answers 0),
     * bit 1 instead the user service called from WOM_DONE; neither: only the
     * user service refills. DBSIM writes 1. */
    uint32_t service;
    es2_sim_sos_voice voice[ES2_SIM_SOS_VOICES];
    uint8_t list[ES2_SIM_SOS_VOICES];        /* the voice list from +30h, in link order */
    uint32_t list_n;
    uint32_t count;                          /* +4Ch: voices visited by the last mix, + started since */
    uint32_t written, done, outstanding;     /* +992h, +996h, +87Eh */
    uint32_t sink_error;                     /* the last open/write failure (0 none) */
    uint64_t flushes;                        /* sink flushes (buffers written with no voice listed) */
    /* The resample DIV faulted and the process ended (only a test's recorder
     * returns): nothing more is mixed or written. */
    int faulted;
    /* The voice notices (main_loop.md, "the SOS voice notices in es2sim's
     * pump"). `window` is driver 0's entry of the window table 0x41809e
     * (sosDIGISetActiveWindow 0x404050); while it is nonzero, the default end
     * and remove callbacks (0x404140, 0x4041ae) post 0xa000 with wParam 0xa003
     * and lParam handle << 16 | 4000h (end) or 2000h (removal) through `post`
     * (PostMessageA; nonzero: not queued, counted in `posts_lost`). `is_window`
     * is IsWindow (0x4040ed; NULL: every handle is a window). Both NULL by
     * default: no notice is delivered. */
    uint32_t window;
    int (*is_window)(void *user, uint32_t window);
    int (*post)(void *user, uint32_t window, uint32_t message, uint32_t wparam, uint32_t lparam);
    void *post_user;
    uint64_t posts, posts_lost;
    uint64_t calls[ES2_SOS_CALL_COUNT], mixed;
    int32_t acc[ES2_SIM_SOS_BLOCK_MAX];      /* the 32-bit accumulators (+40h) */
    uint8_t out[ES2_SIM_SOS_BLOCK_MAX];      /* the last mixed buffer */
} es2_sim_sos_mixer;

/* `vt` as for the silent device; `sink` is copied. */
void es2_sim_sos_mixer_init(es2_sim_sos_mixer *x, const es2_vt *vt, const es2_sim_sos_sink *sink);
void es2_sim_sos_mixer_device(es2_sim_sos_mixer *x, es2_sim_audio_device *device);
/* One buffer as 0x406b90 mixes it, into x->out (x->block bytes). */
void es2_sim_sos_mix(es2_sim_sos_mixer *x, const es2_guest_memory *m);
/* The buffers the sink has played since the last call count as returned
 * (WOM_DONE 0x402fa1 for each: the removals, then the service thread's
 * refill 0x403809). Every device call pumps first; a host may also call it. */
void es2_sim_sos_pump(es2_sim_sos_mixer *x, const es2_guest_memory *m);

/* ---- mciSendCommandA through the system (audio.md, "sound and CD music
 * in the window"). The own CD code (0x473c00..0x473ea8) sends its commands with
 * guest parameter blocks; this copies each block into a host block of the same
 * layout (on x86 every field of the MCI_*_PARMS DBSIM uses is 32 bits), reads
 * the strings MCI_OPEN names, puts the host notify window in place of the
 * callback when MCI_NOTIFY is set and the guest gave one, sends, and writes the
 * fields the system changed back into the guest block. The commands DBSIM
 * sends: MCI_OPEN (803h), MCI_CLOSE (804h), MCI_PLAY (806h), MCI_STOP (808h),
 * MCI_SET (80Dh), MCI_STATUS (814h); any other with a parameter block answers
 * MCIERR_UNSUPPORTED_FUNCTION (274) unsent. */
#define ES2_SIM_MCI_FIELDS 5u
#define ES2_SIM_MCI_STRING 128u
typedef struct es2_sim_mci_host {
    /* The system's mciSendCommandA over a host block (NULL when the guest
     * passed none); its MCIERROR. */
    uint32_t (*send)(void *user, uint32_t device, uint32_t message, uint32_t flags, void *params);
    void *user;
    uintptr_t notify_window;                 /* the host HWND MM_MCINOTIFY goes to (0: the guest's callback is passed as is) */
    uint32_t guest_window;                   /* the guest's callback of the last MCI_NOTIFY command */
    uintptr_t block[ES2_SIM_MCI_FIELDS];     /* the last host block as sent and returned */
    char strings[3][ES2_SIM_MCI_STRING];     /* device type, element, alias */
    uint32_t last_message, last_flags, last_error;
    uint64_t calls, sent, unsupported, notify_requests;
} es2_sim_mci_host;
/* `send` NULL: every command answers 306 as the silent device's MCI_OPEN. */
void es2_sim_mci_host_init(es2_sim_mci_host *h, uint32_t (*send)(void *user, uint32_t device, uint32_t message, uint32_t flags, void *params),
                           void *user, uintptr_t notify_window);
uint32_t es2_sim_mci_send(es2_sim_mci_host *h, const es2_guest_memory *m, uint32_t device, uint32_t message, uint32_t flags,
                          uint32_t params);

#endif
