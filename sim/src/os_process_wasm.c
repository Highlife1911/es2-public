/* es2sim's process in the web build (es2/sim/os_process.h,
 * docs/web_build.md section 5): one thread, no claim, the keyboard and the
 * message box from the JS host (the "es2sim" imports, os_process_wasm.js). */
#include <stdio.h>
#include <emscripten/stack.h>
#include "es2/platform/os.h"
#include "es2/sim/host.h"
#include "es2/sim/os_process.h"

#define IMPORT(name) __attribute__((import_module("es2sim"), import_name(#name)))
IMPORT(key_scan) uint32_t host_key_scan(uint32_t vk);
IMPORT(key_translate) uint32_t host_key_translate(uint32_t vk, uint32_t scan);

/* The linker's symbols of the layout. */
extern unsigned char __global_base[];
extern unsigned char __heap_base[];

#define LAYOUT_FLOOR 0x00800000u
#define IMAGE_BASE 0x00400000u

/* Emscripten places nothing below GLOBAL_BASE, so the guest range is the
 * linear memory's own; the check is that nothing of the static data, the C
 * stack or the host heap lies below LAYOUT_FLOOR. */
uint8_t *es2_sim_os_reserved(uint32_t *error) {
    const struct { const char *name; uintptr_t at; } layout[] = {
        {"__global_base", (uintptr_t)__global_base},
        {"emscripten_stack_get_end()", emscripten_stack_get_end()},
        {"emscripten_stack_get_base()", emscripten_stack_get_base()},
        {"__heap_base", (uintptr_t)__heap_base},
    };
    for (unsigned i = 0; i < sizeof(layout) / sizeof(layout[0]); ++i)
        if (layout[i].at < LAYOUT_FLOOR) {
            fprintf(stderr, "FAULT: the layout puts %s at 0x%08lx, below 0x%08lx\n", layout[i].name,
                    (unsigned long)layout[i].at, (unsigned long)LAYOUT_FLOOR);
            fflush(stderr);
            es2_os_exit(ES2_SIM_EXIT_FAULT);
        }
    *error = 0;
    return (uint8_t *)(uintptr_t)IMAGE_BASE;
}

/* No exception reaches C: a RuntimeError ends in es2_wasm_trap_exit. */
void es2_sim_os_on_unhandled(void (*report)(void *exception_pointers)) { (void)report; }
void es2_sim_os_exception_decode(const void *exception_pointers, es2_sim_os_exception *out) {
    (void)exception_pointers;
    *out = (es2_sim_os_exception){0};
}

/* The simulation runs on the calling thread and its C stack (16 MiB,
 * -sSTACK_SIZE). */
int es2_sim_os_run_thread(uint32_t (*body)(void), uint32_t stack_reserve, uint32_t *result) {
    (void)stack_reserve;
    *result = body();
    return 1;
}

/* The c0w32 entry keeps two words below StackBase and points 0x6cbee4 at
 * the upper one for the whole run. On Windows they lie at the top of the
 * host thread's stack, outside the guest table; here they lie in static
 * data above 0x800000, outside the table as well. The top of the C stack
 * would be overwritten by C frames and the guest stack's top holds the FS:0
 * sentinel, so neither serves. */
static _Alignas(16) uint8_t stack_top[16];
uint32_t es2_sim_os_stack_base(void) { return (uint32_t)(uintptr_t)(stack_top + sizeof(stack_top)); }

/* The JS host's keyboard (web/keys.mjs): MapVirtualKeyA's scan code, and
 * ToAscii's answer packed as main.c's ES2_SIM_TRANSLATE_* values. */
#define TRANSLATE_ONE 0x01000000u
#define TRANSLATE_TWO 0x02000000u
#define TRANSLATE_DEAD 0x80000000u
unsigned es2_sim_os_vk_scan(unsigned vk) { return host_key_scan(vk); }
int es2_sim_os_to_ascii(uint32_t vk, uint32_t scan, uint16_t *out) {
    const uint32_t r = host_key_translate(vk, scan);
    *out = (uint16_t)(r & 0xffffu);
    if (r & TRANSLATE_DEAD) {
        *out = (uint16_t)(r & 0xffu);
        return -1;
    }
    if (r & TRANSLATE_TWO) return 2;
    if (r & TRANSLATE_ONE) {
        *out = (uint16_t)(r & 0xffu);
        return 1;
    }
    *out = 0;
    return 0;
}

/* No desktop here; main.c prints the box and its answer. */
uint32_t es2_sim_os_message_box(uint32_t owner, const char *text, const char *caption, uint32_t style) {
    (void)owner;
    (void)text;
    (void)caption;
    (void)style;
    return 1u; /* IDOK */
}
