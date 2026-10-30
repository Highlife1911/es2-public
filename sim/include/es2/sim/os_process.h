#ifndef ES2_SIM_OS_PROCESS_H
#define ES2_SIM_OS_PROCESS_H
/* es2sim's process on its host OS, beside the OS layer (es2/platform/os.h):
 * what main.c needs that is the process itself rather than a service of the
 * game. os_process_win32.c implements it on Windows, os_process_wasm.c in the
 * web build (docs/web_build.md section 5). */
#include <stdint.h>

/* The guest range 0x400000..0x6d7fff claimed at the entry before the CRT ran
 * (startup_reservation.h): its base, or NULL with the error in *error. */
uint8_t *es2_sim_os_reserved(uint32_t *error);

/* The last-chance fault report: the system's error boxes off and `report`
 * called with the EXCEPTION_POINTERS of an exception no handler took;
 * `report` ends the process. */
void es2_sim_os_on_unhandled(void (*report)(void *exception_pointers));

/* What the fault reports print of an exception's EXCEPTION_RECORD: its code,
 * NumberParameters, the first two ExceptionInformation words, the address,
 * and the record itself for es2/sim/host_fault.h. */
typedef struct es2_sim_os_exception {
    uint32_t code;
    unsigned parameters;
    uintptr_t information[2];
    const void *address;
    const void *record;
} es2_sim_os_exception;
void es2_sim_os_exception_decode(const void *exception_pointers, es2_sim_os_exception *out);

/* Runs `body` on a thread with a stack reservation of `stack_reserve` bytes
 * and joins it: 1 with its result in *result, 0 when it could not run. */
int es2_sim_os_run_thread(uint32_t (*body)(void), uint32_t stack_reserve, uint32_t *result);

/* The StackBase the c0w32 entry keeps its words under: on Windows the
 * calling thread's TIB value, in the web build the end of a static block
 * outside the guest table (docs/web_build.md section 5). */
uint32_t es2_sim_os_stack_base(void);

/* MapVirtualKeyA(vk, MAPVK_VK_TO_VSC). */
unsigned es2_sim_os_vk_scan(unsigned vk);
/* ToAscii(vk, scan, all keys up, out, 0): its result, the characters in *out.
 * The thread's dead-key state carries over between calls. */
int es2_sim_os_to_ascii(uint32_t vk, uint32_t scan, uint16_t *out);

/* MessageBoxA with the guest's owner, ANSI text and caption, style; its
 * answer. */
uint32_t es2_sim_os_message_box(uint32_t owner, const char *text, const char *caption, uint32_t style);

#endif
