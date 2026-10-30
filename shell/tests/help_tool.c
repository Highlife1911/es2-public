/* Test driver of //shell:help_real_test: our ONLINE MANUAL event (the
 * flow's help routine and the manual's path) over stdin, for the comparison
 * with VSHELL's 0x431e79 in the mapped original. Nothing is opened: the
 * Windows operations are replaced by recording ones.
 *
 *   line 1            the prefs file (hex, "-" for none), opened as at start-up
 *   control N         a Preferences control (es2_shell_prefs_control)
 *   accept / cancel   Preferences ACCEPT (the file is replaced) / CANCEL
 *   manual LETTER ROOT [bay]
 *                     the ONLINE MANUAL event (or the bay's online image)
 *                     with language.cfg = LETTER (one character) and the CD
 *                     root ROOT ("-" for none)
 *
 * After each operation "STATE current committed" (hex); after manual
 * "MANUAL outcome path" ("-" when no path). At the end "FILE bytes" or
 * "FILE -". No file I/O beyond stdin/stdout. */
#include "es2/shell/flow.h"
#include "win/help.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char line[1 << 17];
static uint8_t file[4096];
static es2_span disk;
static int letter = -1;
static const char *root = "";
static es2_help_result last;

static void hex(const char *key, const uint8_t *p, size_t n) {
    printf("%s ", key);
    for (size_t i = 0; i < n; i++) printf("%02x", p[i]);
}
static size_t unhex(const char *text, uint8_t *out, size_t capacity) {
    size_t n = 0;
    unsigned v;
    while (text[0] && text[1] && text[0] != '\n' && text[0] != '\r' && n < capacity && sscanf(text, "%2x", &v) == 1) {
        out[n++] = (uint8_t)v;
        text += 2;
    }
    return n;
}

static int h_write(void *ctx, const uint8_t block[ES2_SHELL_PREFS_SIZE]) {
    (void)ctx;
    memcpy(file, block, ES2_SHELL_PREFS_SIZE);
    disk = (es2_span){file, ES2_SHELL_PREFS_SIZE};
    return 1;
}
static int o_language(void *ctx) { (void)ctx; return letter; }
static int o_exists(void *ctx, const char *path) { (void)ctx; (void)path; return 1; }
static int o_open(void *ctx, const char *path) { (void)ctx; (void)path; return 1; }
static void h_manual(void *ctx) {
    static const es2_help_ops ops = {NULL, o_language, o_exists, o_open};
    (void)ctx;
    es2_help_show(&ops, ES2_HELP_POLICY_OPEN, root, &last);
}

int main(void) {
    static es2_shell_flow flow;
    es2_shell_flow_host host;
    if (!fgets(line, sizeof line, stdin)) return 1;
    if (line[0] == '-') disk = (es2_span){NULL, 0};
    else disk = (es2_span){file, unhex(line, file, sizeof file)};
    memset(&host, 0, sizeof host);
    host.write_prefs = h_write;
    host.online_manual = h_manual;
    es2_shell_flow_init(&flow, &host, disk.data ? &disk : NULL);
    while (fgets(line, sizeof line, stdin)) {
        int a, fields, bay = 0, manual = 0;
        char word[16], third[16], root_text[256];
        uint8_t out[ES2_SHELL_PREFS_SIZE];
        es2_error e;
        es2_shell_flow_result r = ES2_FLOW_DONE;
        if (sscanf(line, "control %d", &a) == 1) {
            if (es2_shell_prefs_control(&flow.prefs, a, &e) != ES2_OK) { printf("ERROR control\n"); continue; }
        } else if (!strncmp(line, "accept", 6)) {
            es2_shell_prefs_accept(&flow.prefs, out);
            h_write(NULL, out);
        } else if (!strncmp(line, "cancel", 6)) {
            es2_shell_prefs_cancel(&flow.prefs);
        } else if ((fields = sscanf(line, "manual %15s %255s %15s", word, root_text, third)) >= 2) {
            manual = 1;
            letter = word[0] == '-' && !word[1] ? -1 : (unsigned char)word[0];
            root = root_text[0] == '-' && !root_text[1] ? "" : root_text;
            bay = fields == 3 && !strcmp(third, "bay");
            memset(&last, 0, sizeof last);
            flow.screen = bay ? ES2_FLOW_SERVICE_BAY : ES2_FLOW_MAIN_MENU;
            r = es2_shell_flow_event(&flow, bay ? ES2_FLOW_BAY_ONLINE : ES2_FLOW_ONLINE_MANUAL, NULL);
        } else {
            printf("ERROR unknown operation\n");
            return 1;
        }
        hex("STATE", flow.prefs.current, ES2_SHELL_PREFS_SIZE);
        hex("", flow.prefs.committed, ES2_SHELL_PREFS_SIZE);
        printf("\n");
        if (manual) printf("MANUAL %d %d %s\n", (int)r, (int)last.outcome, last.path[0] ? last.path : "-");
    }
    if (disk.data) { hex("FILE", disk.data, disk.size); printf("\n"); }
    else printf("FILE -\n");
    return 0;
}
