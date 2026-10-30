/* The streams and file functions of es2/platform/os.h with the bytes they
 * must produce. On Windows they are the C runtime's FILE and the Win32 calls,
 * so the expectations are the CRT's behaviour; the wasm build runs the same
 * checks over os_stream.c and the Node host (platform/src/os_node.js).
 * Argument: a scratch directory (default: TEST_TMPDIR). */
#include "es2/platform/os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static char dir[512];

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            failures++;                                                 \
        }                                                               \
    } while (0)

static const char *path_of(const char *name) {
    static char path[600];
    snprintf(path, sizeof path, "%s\\%s", dir, name);
    return path;
}

/* The file's bytes through the handle functions; the length, or -1. */
static long slurp(const char *name, unsigned char *out, size_t capacity) {
    es2_os_handle h = es2_os_create_file(path_of(name), ES2_OS_GENERIC_READ, ES2_OS_FILE_SHARE_READ, 0,
                                         ES2_OS_OPEN_EXISTING, ES2_OS_FILE_ATTRIBUTE_NORMAL);
    uint32_t done = 0;
    if (h == ES2_OS_INVALID_HANDLE) return -1;
    int ok = es2_os_read_file(h, out, (uint32_t)capacity, &done);
    es2_os_close_handle(h);
    return ok ? (long)done : -1;
}

static void put_bytes(const char *name, const void *bytes, size_t n) {
    es2_os_stream *s = es2_os_fopen(path_of(name), "wb");
    CHECK(s != NULL);
    if (!s) return;
    CHECK(es2_os_fwrite(bytes, 1, n, s) == n);
    CHECK(es2_os_fclose(s) == 0);
}

static void text_write(void) {
    unsigned char got[64];
    es2_os_stream *s = es2_os_fopen(path_of("text.txt"), "w");
    CHECK(s != NULL);
    if (!s) return;
    CHECK(es2_os_fputs("a\nb", s) == 0);
    CHECK(es2_os_fputc('\n', s) == '\n');
    CHECK(es2_os_ftell(s) == 6);
    CHECK(es2_os_fprintf(s, "%d\n", 42) == 3);
    CHECK(es2_os_fclose(s) == 0);
    CHECK(slurp("text.txt", got, sizeof got) == 10);
    CHECK(memcmp(got, "a\r\nb\r\n42\r\n", 10) == 0);
}

static void text_read(void) {
    static const char bytes[] = "a\r\nb\rc\n\r\n\x1azz";
    int c[8];
    put_bytes("read.txt", bytes, sizeof bytes - 1);
    es2_os_stream *s = es2_os_fopen(path_of("read.txt"), "r");
    CHECK(s != NULL);
    if (!s) return;
    for (int i = 0; i < 8; i++) c[i] = es2_os_fgetc(s);
    CHECK(c[0] == 'a' && c[1] == '\n' && c[2] == 'b' && c[3] == '\r' && c[4] == 'c');
    CHECK(c[5] == '\n' && c[6] == '\n' && c[7] == EOF);
    CHECK(es2_os_fseek(s, 0, SEEK_SET) == 0);
    char line[16] = {0};
    CHECK(es2_os_fread(line, 1, 4, s) == 4);
    CHECK(memcmp(line, "a\nb\r", 4) == 0);
    CHECK(es2_os_fclose(s) == 0);
}

static void binary(void) {
    unsigned char bytes[10000], got[10000];
    for (size_t i = 0; i < sizeof bytes; i++) bytes[i] = (unsigned char)(i * 7 + (i >> 8));
    put_bytes("bin.dat", bytes, sizeof bytes);
    es2_os_stream *s = es2_os_fopen(path_of("bin.dat"), "rb");
    CHECK(s != NULL);
    if (!s) return;
    CHECK(es2_os_fread(got, 1, 5000, s) == 5000);
    CHECK(es2_os_ftell(s) == 5000);
    CHECK(es2_os_fseek(s, -100, SEEK_CUR) == 0);
    CHECK(es2_os_ftell(s) == 4900);
    CHECK(es2_os_fgetc(s) == bytes[4900]);
    CHECK(es2_os_fseek(s, -3, SEEK_END) == 0);
    CHECK(es2_os_fread(got, 1, 10, s) == 3);
    CHECK(memcmp(got, bytes + sizeof bytes - 3, 3) == 0);
    CHECK(es2_os_fgetc(s) == EOF);
    CHECK(es2_os_fseek(s, 0, SEEK_SET) == 0);
    CHECK(es2_os_fread(got, 4, 2500, s) == 2500);
    CHECK(memcmp(got, bytes, sizeof bytes) == 0);
    CHECK(es2_os_fclose(s) == 0);
    CHECK(es2_os_fopen(path_of("missing.dat"), "rb") == NULL);
}

static void append(void) {
    unsigned char got[64];
    put_bytes("app.txt", "xy", 2);
    es2_os_stream *s = es2_os_fopen(path_of("app.txt"), "ab");
    CHECK(s != NULL);
    if (!s) return;
    CHECK(es2_os_fwrite("1", 1, 1, s) == 1);
    CHECK(es2_os_ftell(s) == 3);
    CHECK(es2_os_fseek(s, 0, SEEK_SET) == 0);
    CHECK(es2_os_fwrite("2", 1, 1, s) == 1);
    CHECK(es2_os_fclose(s) == 0);
    s = es2_os_fopen(path_of("app.txt"), "a");
    CHECK(s != NULL);
    if (!s) return;
    CHECK(es2_os_fputs("z\n", s) == 0);
    CHECK(es2_os_fclose(s) == 0);
    CHECK(slurp("app.txt", got, sizeof got) == 7);
    CHECK(memcmp(got, "xy12z\r\n", 7) == 0);
}

static void update(void) {
    unsigned char got[64];
    put_bytes("upd.dat", "abcdef", 6);
    es2_os_stream *s = es2_os_fopen(path_of("upd.dat"), "r+b");
    CHECK(s != NULL);
    if (!s) return;
    CHECK(es2_os_fgetc(s) == 'a');
    CHECK(es2_os_fseek(s, 0, SEEK_CUR) == 0);
    CHECK(es2_os_fputc('B', s) == 'B');
    CHECK(es2_os_fseek(s, 0, SEEK_CUR) == 0);
    CHECK(es2_os_fgetc(s) == 'c');
    CHECK(es2_os_fseek(s, 0, SEEK_END) == 0);
    CHECK(es2_os_fwrite("g", 1, 1, s) == 1);
    CHECK(es2_os_fclose(s) == 0);
    CHECK(slurp("upd.dat", got, sizeof got) == 7);
    CHECK(memcmp(got, "aBcdefg", 7) == 0);
}

static void long_print(void) {
    char text[2001];
    unsigned char got[2100];
    memset(text, 'q', 2000);
    text[2000] = 0;
    es2_os_stream *s = es2_os_fopen(path_of("long.txt"), "wb");
    CHECK(s != NULL);
    if (!s) return;
    CHECK(es2_os_fprintf(s, "<%s>", text) == 2002);
    CHECK(es2_os_fclose(s) == 0);
    CHECK(slurp("long.txt", got, sizeof got) == 2002);
    CHECK(got[0] == '<' && got[1] == 'q' && got[2000] == 'q' && got[2001] == '>');
}

static void files(void) {
    int64_t seconds = 0;
    uint8_t find[ES2_OS_FIND_DATA_SIZE];
    char pattern[600];
    put_bytes("Find1.dat", "1", 1);
    CHECK(es2_os_file_mtime(path_of("Find1.dat"), &seconds) && seconds > 1000000000);
    CHECK(!es2_os_file_mtime(path_of("absent.dat"), &seconds));
    CHECK(es2_os_file_attributes(path_of("absent.dat")) == ES2_OS_INVALID_FILE_ATTRIBUTES);
    CHECK(es2_os_last_error() == ES2_OS_ERROR_FILE_NOT_FOUND);
    CHECK((es2_os_file_attributes(dir) & ES2_OS_FILE_ATTRIBUTE_DIRECTORY) != 0);
    snprintf(pattern, sizeof pattern, "%s\\find?.DAT", dir);
    es2_os_handle h = es2_os_find_first(pattern, find);
    CHECK(h != ES2_OS_INVALID_HANDLE);
    if (h != ES2_OS_INVALID_HANDLE) {
        CHECK(strcmp((const char *)find + 44, "Find1.dat") == 0);
        CHECK(find[32] == 1 && find[33] == 0);   /* nFileSizeLow */
        CHECK(!es2_os_find_next(h, find));
        CHECK(es2_os_find_close(h));
    }
    CHECK(es2_os_remove(path_of("Find1.dat")) == 0);
    CHECK(es2_os_remove(path_of("Find1.dat")) != 0);
    CHECK(es2_os_find_first(pattern, find) == ES2_OS_INVALID_HANDLE);
}

/* The whole-file view: the file's bytes; an empty or missing file has none. */
static void file_view(void) {
    unsigned char bytes[10000];
    for (size_t i = 0; i < sizeof bytes; i++) bytes[i] = (unsigned char)(i * 7u + 3u);
    put_bytes("view.bin", bytes, sizeof bytes);
    put_bytes("view0.bin", "", 0);
    es2_os_file_view v;
    CHECK(es2_os_map_file(path_of("view.bin"), &v));
    CHECK(v.bytes && v.size == sizeof bytes && !memcmp(v.bytes, bytes, sizeof bytes));
    /* Shared for reading while the view holds it. */
    unsigned char again[10000];
    CHECK(slurp("view.bin", again, sizeof again) == (long)sizeof again);
    es2_os_unmap_file(&v);
    CHECK(!v.bytes && v.size == 0);
    es2_os_unmap_file(&v);
    CHECK(!es2_os_map_file(path_of("view0.bin"), &v) && !v.bytes && v.size == 0);
    CHECK(!es2_os_map_file(path_of("no-such-view.bin"), &v) && !v.bytes && v.size == 0);
}

#ifdef __EMSCRIPTEN__
/* The command line a wasm main sets from argv. */
static void command_line(void) {
    char *const argv[] = {"es2shell", "--menu", "D:\\a b\\", "", "-@", "x\"y", "a\\\\\"b", "c\\d"};
    CHECK(!strcmp(es2_os_command_line(), ""));
    CHECK(es2_os_set_command_line(8, argv));
    CHECK(!strcmp(es2_os_command_line(),
                  "es2shell --menu \"D:\\a b\\\\\" \"\" -@ \"x\\\"y\" \"a\\\\\\\\\\\"b\" c\\d"));
}
#endif

int main(int argc, char **argv) {
    const char *base = argc > 1 ? argv[1] : getenv("TEST_TMPDIR");
    if (!base || !*base) {
        printf("usage: os_stream_test DIRECTORY\n");
        return 2;
    }
    snprintf(dir, sizeof dir, "%s\\os_stream", base);
    for (char *p = dir; *p; p++)
        if (*p == '/') *p = '\\';
    es2_os_create_directory(dir);
    text_write();
    text_read();
    binary();
    append();
    update();
    long_print();
    files();
    file_view();
#ifdef __EMSCRIPTEN__
    command_line();
#endif
    printf("%s (%d failures)\n", failures ? "FAIL" : "ok", failures);
    return failures != 0;
}
