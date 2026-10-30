/* Test driver of the slot statistics (es2/shell/slots.h) for
 * stats_real_test. Standard input, per case:
 *   CASE
 *   eleven lines: the SAV of slot 0..10 in hex, or "-" (flag 0)
 *   OPS op op ...   stats | memory:K | refresh:J | dump
 * stats reads every flagged slot over the zero records (0x4309f0);
 * memory:K makes the memory state the record read from slot K's SAV over a
 * zero record (the game the original readers restored from that SAV);
 * refresh:J refreshes record J from that memory state (0x430ec4(J)); dump
 * prints the eleven records. Output per case: one line per dump, "D <hex>", then
 * "END", or "ERR <status> <op>" for the first refused step. */
#include "es2/shell/slots.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *line(void) {
    static char *buffer;
    static size_t capacity;
    size_t n = 0;
    int c;
    while ((c = getchar()) != EOF && c != '\n') {
        if (n + 2 > capacity) {
            capacity = capacity ? 2 * capacity : 1 << 16;
            buffer = realloc(buffer, capacity);
            if (!buffer) exit(2);
        }
        if (c != '\r') buffer[n++] = (char)c;
    }
    if (c == EOF && !n) return NULL;
    if (!buffer) { buffer = malloc(1); capacity = 1; }
    buffer[n] = 0;
    return buffer;
}
static uint8_t *unhex(const char *s, size_t *n) {
    size_t length = strlen(s) / 2;
    uint8_t *out = malloc(length ? length : 1);
    if (!out) exit(2);
    for (size_t i = 0; i < length; i++) {
        unsigned v;
        if (sscanf(s + 2 * i, "%2x", &v) != 1) exit(2);
        out[i] = (uint8_t)v;
    }
    *n = length;
    return out;
}

int main(void) {
    char *l;
    while ((l = line()) != NULL) {
        uint8_t *sav[ES2_SHELL_STATS_SLOTS] = {0};
        size_t size[ES2_SHELL_STATS_SLOTS] = {0};
        es2_shell_stats records[ES2_SHELL_STATS_SLOTS], memory;
        es2_error e;
        es2_status s = ES2_OK;
        char *op;
        if (strcmp(l, "CASE")) return 2;
        for (int k = 0; k < ES2_SHELL_STATS_SLOTS; k++) {
            if (!(l = line())) return 2;
            if (strcmp(l, "-")) sav[k] = unhex(l, &size[k]);
        }
        if (!(l = line()) || strncmp(l, "OPS", 3)) return 2;
        memset(records, 0, sizeof records);
        memset(&memory, 0, sizeof memory);
        for (op = strtok(l + 3, " "); op && s == ES2_OK; op = strtok(NULL, " ")) {
            unsigned k;
            if (!strcmp(op, "stats")) {
                for (int i = 0; i < ES2_SHELL_STATS_SLOTS && s == ES2_OK; i++)
                    if (sav[i]) s = es2_shell_stats_read((es2_span){sav[i], size[i]}, &records[i], &e);
            } else if (sscanf(op, "memory:%u", &k) == 1 && k < ES2_SHELL_STATS_SLOTS && sav[k]) {
                memset(&memory, 0, sizeof memory);
                s = es2_shell_stats_read((es2_span){sav[k], size[k]}, &memory, &e);
            } else if (sscanf(op, "refresh:%u", &k) == 1 && k < ES2_SHELL_STATS_SLOTS) {
                s = es2_shell_stats_refresh(&records[k], &memory, &e);
            } else if (!strcmp(op, "dump")) {
                printf("D ");
                for (int i = 0; i < ES2_SHELL_STATS_SLOTS; i++)
                    for (int j = 0; j < ES2_SHELL_STATS_SIZE; j++) printf("%02x", records[i].raw[j]);
                printf("\n");
            } else
                return 2;
            if (s != ES2_OK) printf("ERR %d %s\n", (int)s, op);
        }
        printf("END\n");
        fflush(stdout);
        for (int k = 0; k < ES2_SHELL_STATS_SLOTS; k++) free(sav[k]);
    }
    return 0;
}
