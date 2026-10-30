/* Checks, under Node, the guest-memory layout the wasm link options give (docs/web_build.md section 5):
   static data from 0x00800000, a 16 MiB C stack after it, the heap after the stack, and a heap that grows. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DATA_BASE 0x00800000u
#define STACK_SIZE 0x01000000u

static int data_marker = 1;
static int failures;

static void check(int ok, const char *what, uintptr_t value) {
    if (!ok) {
        printf("FAIL %s (0x%08lx)\n", what, (unsigned long)value);
        failures++;
    }
}

int main(int argc, char **argv) {
    volatile char deep[8u << 20]; /* half of the stack */
    char *heap = (char *)malloc(16);
    char *big = (char *)malloc(256u << 20); /* more than INITIAL_MEMORY: the heap must grow */
    uintptr_t data = (uintptr_t)&data_marker, stack = (uintptr_t)&deep[0], first = (uintptr_t)heap;

    memset((void *)deep, 0x5a, sizeof deep);
    check(data >= DATA_BASE && data < DATA_BASE + 0x100000u, "static data starts at 0x00800000", data);
    check(stack >= data && stack < data + 0x100000u + STACK_SIZE, "stack lies between the static data and the heap", stack);
    check(first >= data + STACK_SIZE, "heap starts after the 16 MiB stack", first);
    check(big != NULL, "heap grows beyond the initial memory", 0);
    if (big) { big[0] = 1; big[(256u << 20) - 1] = 1; }
    check(deep[(8u << 20) - 1] == 0x5a, "8 MiB of stack are usable", 0);
    if (argc > 1 && strcmp(argv[1], "--fail") == 0) failures++; /* checks that a failing program fails the test */
    printf("layout: data 0x%08lx, stack 0x%08lx, heap 0x%08lx, %d failures\n", (unsigned long)data, (unsigned long)stack, (unsigned long)first, failures);
    free(big);
    free(heap);
    return failures != 0;
}
