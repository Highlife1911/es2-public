/* The own shell's input tables and constants against the reference
 * VER95/VSHELL.EXE (SHA-256 1534aa8f...a16e) under ES2_DATA_ROOT: the key
 * table 0x46d4bc, the hot-key list 0x46e588, the command bytes 0x46e5a9 for
 * every word below 0x100, the character tables 0x46e6a9/0x46e6fd, and the
 * code bytes the own shell's contract relies on. Nothing of the file is kept or
 * printed except counts. Skipped without ES2_DATA_ROOT. */
#include "es2/shell/gui/events.h"

#include "es2/formats/sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; fprintf(stderr, "line %d: %s\n", __LINE__, #x); } } while (0)

static uint8_t *image;
static size_t image_size;

static uint32_t u32(size_t at) {
    return at + 4 <= image_size ? (uint32_t)image[at] | (uint32_t)image[at + 1] << 8 | (uint32_t)image[at + 2] << 16 |
                                      (uint32_t)image[at + 3] << 24
                                : 0;
}
static uint16_t u16(size_t at) { return at + 2 <= image_size ? (uint16_t)(image[at] | image[at + 1] << 8) : 0; }

/* File offset of a virtual address (image base 0x400000), or 0 when unmapped. */
static size_t offset_of(uint32_t va) {
    uint32_t pe = u32(0x3c);
    unsigned sections = u16(pe + 6), optional = u16(pe + 20);
    for (unsigned i = 0; i < sections; i++) {
        size_t s = pe + 24 + optional + 40u * i;
        uint32_t vaddr = u32(s + 12) + 0x400000u, raw_size = u32(s + 16), raw = u32(s + 20);
        if (va >= vaddr && va < vaddr + raw_size) return raw + (va - vaddr);
    }
    return 0;
}
static const uint8_t *at(uint32_t va, size_t n) {
    size_t o = offset_of(va);
    return o && o + n <= image_size ? image + o : NULL;
}

int main(void) {
    const char *root = getenv("ES2_DATA_ROOT");
    if (!root || !*root) {
        printf("SKIP: ES2_DATA_ROOT not set\n");
        return 0;
    }
    char path[1024];
    snprintf(path, sizeof path, "%s/VER95/VSHELL.EXE", root);
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("SKIP: %s not found\n", path);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    image = size > 0 ? malloc((size_t)size) : NULL;
    image_size = image ? fread(image, 1, (size_t)size, f) : 0;
    fclose(f);
    char hex[65];
    es2f_sha256_of(image, image_size, hex);
    if (strcmp(hex, "1534aa8f732bdecd826484d893ad58b8a5e6ebd4d25ab3158a2469b8f69ba16e")) {
        printf("FAIL: VSHELL.EXE is not the reference file\n");
        return 1;
    }

    /* key table: 84 dwords */
    const uint8_t *vk = at(0x46d4bc, 4 * ES2_GUI_KEY_COUNT);
    CHECK(vk != NULL);
    for (unsigned i = 0; vk && i < ES2_GUI_KEY_COUNT; i++) {
        uint32_t v = (uint32_t)vk[4 * i] | (uint32_t)vk[4 * i + 1] << 8 | (uint32_t)vk[4 * i + 2] << 16 |
                     (uint32_t)vk[4 * i + 3] << 24;
        CHECK(v == es2_gui_key_vk(i));
    }
    /* hot-key list: zero-terminated bytes; our hot keys are exactly those (down and up forms) */
    const uint8_t *hot = at(0x46e588, 64);
    unsigned listed = 0;
    for (unsigned i = 0; hot && hot[i]; i++) {
        listed++;
        unsigned word = hot[i];
        CHECK(es2_gui_key_vk(word & 0x7f) != 0 || (word & 0x7f) == 12); /* minus (12) has no key */
    }
    CHECK(listed == 32);
    /* command bytes for every word below 0x100 */
    const uint8_t *commands = at(0x46e5a9, 256);
    for (unsigned w = 0; commands && w < 256; w++) CHECK(commands[w] == es2_gui_hot_command(w));
    /* The bytes past the table, read by hot keys with Shift, Ctrl or Alt (0x409587, 0x409605).
     * The 15 hot keys with a virtual key (the minus key has none), down and up, the seven modifier
     * sets; Alt+Esc, Ctrl+Esc, Alt+Tab and Alt+Enter are system words that never reach the hook. */
    {
        static es2_gui_input in;
        static const uint8_t hot_index[15] = {15, 14, 82, 83, 73, 81, 71, 79, 1, 28, 72, 80, 76, 75, 77};
        static const unsigned sets[7] = {0x800, 0x400, 0x200, 0xc00, 0xa00, 0x600, 0xe00};
        const uint8_t *bytes = at(ES2_GUI_HOT_BYTES_BASE, ES2_GUI_HOT_BYTES_SIZE);
        unsigned reached = 0, none = 0, zero = 0, other = 0, actions = 0;
        CHECK(bytes != NULL);
        es2_gui_input_init(&in);
        CHECK(bytes && es2_gui_input_set_hot_bytes(&in, bytes, ES2_GUI_HOT_BYTES_SIZE));
        for (unsigned k = 0; bytes && k < 15; k++)
            for (unsigned up = 0; up < 2; up++)
                for (unsigned m = 0; m < 7; m++) {
                    unsigned base = hot_index[k] | sets[m]; /* before the up flag: the system compare */
                    unsigned word = base | (up ? ES2_GUI_KEY_UP : 0);
                    if (base == 0x201 || base == 0x401 || base == 0x20f || base == 0x21c) continue;
                    reached++;
                    CHECK(es2_gui_input_hot_command(&in, word) == bytes[word]);
                    if (bytes[word] == 0xff) none++;
                    else if (bytes[word] == 0) zero++;
                    else other++;
                    /* the codes the WinConsumer acts on, or an edit field deletes on: 1, 4 to 7, 0x0a, 0x11, 0x1d */
                    if (bytes[word] == 1 || (bytes[word] >= 4 && bytes[word] <= 7) || bytes[word] == 0x0a ||
                        bytes[word] == 0x11 || bytes[word] == 0x1d) {
                        actions++;
                        CHECK(((word == 0x801 || word == 0xcc9) && bytes[word] == 5) || (word == 0xec7 && bytes[word] == 4));
                    }
                }
        CHECK(reached == 202 && none == 10 && zero == 79 && other == 113 && actions == 3);
        /* the consumer 0x409a2e: the case index per code (0x409a5e) and the jump table (0x409a7c) */
        const uint8_t *cases = at(0x409a5e, 30), *jumps = at(0x409a7c, 44);
        static const struct { uint8_t code, index; } mapped[] = {{0, 10}, {1, 9}, {4, 8}, {5, 7}, {6, 6}, {7, 5},
                                                                 {0x0a, 4}, {0x11, 3}, {0x13, 2}, {0x1d, 1}};
        static const uint32_t targets[11] = {0x409c7a, 0x409b4c, 0x409abe, 0x409c60, 0x409aea, 0x409c35,
                                             0x409bdc, 0x409c0a, 0x409bae, 0x409ad4, 0x409aa8};
        CHECK(cases && jumps);
        for (unsigned c = 0; cases && c < 30; c++) {
            unsigned want = 0;
            for (unsigned i = 0; i < sizeof mapped / sizeof mapped[0]; i++)
                if (mapped[i].code == c) want = mapped[i].index;
            CHECK(cases[c] == want);
        }
        for (unsigned i = 0; jumps && i < 11; i++)
            CHECK((uint32_t)jumps[4 * i] + ((uint32_t)jumps[4 * i + 1] << 8) + ((uint32_t)jumps[4 * i + 2] << 16) +
                      ((uint32_t)jumps[4 * i + 3] << 24) == targets[i]);
        /* the log-only cases (code 0, 1, 0x13 and the default) start with a trace call: push str, push 0x16, push 0x28 */
        static const uint32_t log_only[4] = {0x409aa8, 0x409ad4, 0x409abe, 0x409c7a};
        for (unsigned i = 0; i < 4; i++) {
            const uint8_t *c = at(log_only[i], 9);
            CHECK(c && c[0] == 0x68 && c[5] == 0x6a && c[6] == 0x16 && c[7] == 0x6a && c[8] == 0x28);
        }
        /* the hook compares the low byte of the word only (and bl, 0xff at 0x4053ab) */
        static const uint8_t low[] = {0x80, 0xe3, 0xff};
        const uint8_t *lo = at(0x4053ab, sizeof low);
        CHECK(lo && !memcmp(lo, low, sizeof low));
    }
    /* character tables (84 bytes each) */
    const uint8_t *plain = at(0x46e6a9, ES2_GUI_KEY_COUNT), *shift = at(0x46e6fd, ES2_GUI_KEY_COUNT);
    for (unsigned i = 0; plain && shift && i < ES2_GUI_KEY_COUNT; i++) {
        CHECK(plain[i] == es2_gui_key_character(i));
        CHECK(shift[i] == es2_gui_key_character(i | ES2_GUI_KEY_SHIFT));
    }
    /* code facts: display constructor sets the mouse speed to 0x100 (0x41fdd6, 0x41fddd) */
    static const uint8_t speed_x[] = {0xc7, 0x43, 0x09, 0x00, 0x01, 0x00, 0x00};
    static const uint8_t speed_y[] = {0xc7, 0x43, 0x0d, 0x00, 0x01, 0x00, 0x00};
    const uint8_t *sx = at(0x41fdd6, sizeof speed_x), *sy = at(0x41fddd, sizeof speed_y);
    CHECK(sx && !memcmp(sx, speed_x, sizeof speed_x));
    CHECK(sy && !memcmp(sy, speed_y, sizeof speed_y));
    /* 0x4689c1: posting stores the display ([0x47f940]) and calls its slot +0x14 (get pointer) */
    static const uint8_t post_pos[] = {0xa1, 0x40, 0xf9, 0x47, 0x00, 0x89, 0x43, 0x18};
    const uint8_t *pp = at(0x4689c1, sizeof post_pos);
    CHECK(pp && !memcmp(pp, post_pos, sizeof post_pos));
    /* dirty queue limit compare 0x41ffeb: cmp eax, 0x64 */
    static const uint8_t limit[] = {0x83, 0xf8, 0x64};
    const uint8_t *lim = at(0x41ffeb, sizeof limit);
    CHECK(lim && !memcmp(lim, limit, sizeof limit));
    free(image);
    printf("%u checks, %u failures\n", checks, failures);
    return failures != 0;
}
