/* Test driver of //shell:launch_real_test: our launch checks and
 * preferences blocks over stdin, for the comparison with the mapped VSHELL.
 *
 *   launch_prefs_tool check   one SAV per line (hex); per line
 *                    "CHECK bay functional armed squad warning" or "ERROR text"
 *   launch_prefs_tool prefs   line 1: the prefs file (hex, "-" for none), opened as
 *                    at start-up; then one operation per line: "control N"
 *                    (es2_shell_prefs_* control), "accept", "cancel",
 *                    "store INDEX VALUE" (read-modify-write of the file as it
 *                    is now); accept and store replace the file with their
 *                    output. After each operation "STATE current committed",
 *                    at the end "FILE bytes" or "FILE -" (all hex)
 *
 * No file I/O beyond stdin/stdout. */
#include "es2/shell/campaign.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char line[1 << 17];

static size_t unhex(const char *text, uint8_t *out, size_t capacity) {
    size_t n = 0;
    unsigned v;
    while (text[0] && text[1] && text[0] != '\n' && text[0] != '\r' && n < capacity && sscanf(text, "%2x", &v) == 1) {
        out[n++] = (uint8_t)v;
        text += 2;
    }
    return n;
}
static void hex(const char *key, const uint8_t *p, size_t n) {
    printf("%s ", key);
    for (size_t i = 0; i < n; i++) printf("%02x", p[i]);
}
static int check(void) {
    static uint8_t sav[1 << 16];
    while (fgets(line, sizeof line, stdin)) {
        size_t n = unhex(line, sav, sizeof sav);
        es2_shell_launch_check_result r;
        es2_error e;
        es2_status status = es2_shell_launch_check((es2_span){sav, n}, &r, &e);
        if (status != ES2_OK) printf("ERROR %s\n", es2_status_string(status));
        else printf("CHECK %d %d %d %d %d\n", r.player_bay, r.functional, r.armed, r.squad_armed, r.warning);
    }
    return 0;
}
static es2_span file_arg(const char *text, uint8_t *buffer, size_t capacity) {
    if (text[0] == '-') return (es2_span){NULL, 0};
    return (es2_span){buffer, unhex(text, buffer, capacity)};
}
static int prefs(void) {
    static uint8_t file[4096];
    es2_shell_prefs_session session;
    es2_error e;
    if (!fgets(line, sizeof line, stdin)) return 1;
    es2_span disk = file_arg(line, file, sizeof file);
    es2_shell_prefs_session_open(&disk, &session);
    while (fgets(line, sizeof line, stdin)) {
        int a, b;
        uint8_t out[ES2_SHELL_PREFS_SIZE];
        es2_status status = ES2_OK;
        if (sscanf(line, "control %d", &a) == 1) status = es2_shell_prefs_control(&session, a, &e);
        else if (!strncmp(line, "accept", 6)) {
            es2_shell_prefs_accept(&session, file);
            disk = (es2_span){file, ES2_SHELL_PREFS_SIZE};
        } else if (!strncmp(line, "cancel", 6)) es2_shell_prefs_cancel(&session);
        else if (sscanf(line, "store %d %d", &a, &b) == 2 && a >= 0 && b >= 0 && b < 256) {
            status = es2_shell_prefs_store(&session, &disk, (size_t)a, (uint8_t)b, out, &e);
            if (status == ES2_OK) {
                memcpy(file, out, sizeof out);
                disk = (es2_span){file, ES2_SHELL_PREFS_SIZE};
            }
        } else {
            printf("ERROR unknown operation\n");
            return 1;
        }
        if (status != ES2_OK) { printf("ERROR %s\n", es2_status_string(status)); continue; }
        hex("STATE", session.current, ES2_SHELL_PREFS_SIZE);
        hex("", session.committed, ES2_SHELL_PREFS_SIZE);
        printf("\n");
    }
    if (disk.data) { hex("FILE", disk.data, disk.size); printf("\n"); }
    else printf("FILE -\n");
    return 0;
}
int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "check")) return check();
    if (argc == 2 && !strcmp(argv[1], "prefs")) return prefs();
    fprintf(stderr, "usage: launch_prefs_tool check|prefs < input\n");
    return 2;
}
