/* Test driver of condition_real_test. Reads one SAV per stdin line
 * (hex) and prints, per bay k = 0..7, whether the hangar holds a record and
 * es2_shell_record_condition of it (100 without one), as
 * "OK f0..f7 v0..v7", or "REJECT <status>" when the SAV does not parse. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "es2/formats/sav.h"
#include "es2/shell/campaign.h"

static int hex(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }

int main(void) {
    static char line[1 << 20];
    static uint8_t bytes[1 << 19];
    while (fgets(line, sizeof line, stdin)) {
        size_t n = strcspn(line, "\r\n"), size = 0;
        es2_sav sav;
        es2_status st;
        int flag[8] = {0};
        int16_t value[8];
        for (size_t i = 0; i + 1 < n && size < sizeof bytes; i += 2) {
            int a = hex(line[i]), b = hex(line[i + 1]);
            if (a < 0 || b < 0) break;
            bytes[size++] = (uint8_t)(a << 4 | b);
        }
        if ((st = es2_sav_parse((es2_span){bytes, size}, &sav, NULL)) != ES2_OK) {
            printf("REJECT %d\n", (int)st);
            continue;
        }
        for (int k = 0; k < 8; k++) value[k] = 100;
        for (size_t i = 0; i < sav.loadout_count; i++) {
            es2_sav_loadout l;
            if (es2_sav_loadout_at(&sav, i, &l, NULL) != ES2_OK || l.index < 0 || l.index > 7) continue;
            flag[l.index] = 1;
            if ((st = es2_shell_record_condition(l.fixed, &value[l.index], NULL)) != ES2_OK) value[l.index] = -32768;
        }
        printf("OK");
        for (int k = 0; k < 8; k++) printf(" %d", flag[k]);
        for (int k = 0; k < 8; k++) printf(" %d", (int)value[k]);
        printf("\n");
        fflush(stdout);
    }
    return 0;
}
