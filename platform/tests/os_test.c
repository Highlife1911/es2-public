/* The --environment block: what es2_os_environment_load takes and refuses,
 * the copy es2_os_environment returns and the lookup of es2_os_getenv; the
 * modification time es2_os_file_mtime reports. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "es2/platform/os.h"

static int failures;
#define CHECK(x) do { if (!(x)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

static char dir[MAX_PATH];

static const char *write_block(const char *name, const char *bytes, size_t n) {
    static char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(bytes, 1, n, f) != n || fclose(f)) { printf("FAIL: cannot write %s\n", path); exit(1); }
    return path;
}

int main(void) {
    const char *tmp = getenv("TEST_TMPDIR");
    if (!tmp) tmp = ".";
    snprintf(dir, sizeof dir, "%s", tmp);

    /* Without a block: the process's environment. */
    SetEnvironmentVariableA("ES2_OS_TEST_VARIABLE", "process");
    _putenv("ES2_OS_TEST_VARIABLE=process");
    CHECK(es2_os_getenv("ES2_OS_TEST_VARIABLE") && !strcmp(es2_os_getenv("ES2_OS_TEST_VARIABLE"), "process"));
    char *host = es2_os_environment(), *raw = GetEnvironmentStringsA();
    size_t host_n = es2_os_environment_block_size(host, 1u << 20), raw_n = 0;
    while (raw[raw_n]) raw_n += strlen(raw + raw_n) + 1;
    CHECK(host_n == raw_n + 1 && !memcmp(host, raw, raw_n + 1));
    FreeEnvironmentStringsA(raw);
    es2_os_environment_free(host);

    /* Refused: missing file, empty file, no final NUL, a missing empty string. */
    CHECK(es2_os_environment_load("Z:\\no\\such\\file") == 1);
    CHECK(es2_os_environment_load(write_block("empty.env", "", 0)) == 1);
    CHECK(es2_os_environment_load(write_block("open.env", "A=1\0B=2", 7)) == 1);
    CHECK(es2_os_environment_load(write_block("short.env", "A=1\0B=2\0", 8)) == 1);
    CHECK(es2_os_environment_load(write_block("tail.env", "A=1\0\0X", 6)) == 1);
    CHECK(es2_os_getenv("ES2_OS_TEST_VARIABLE") && !strcmp(es2_os_getenv("ES2_OS_TEST_VARIABLE"), "process"));

    /* Taken: the block replaces the process's environment. */
    static const char block[] = "=C:=C:\\ES2\0Path=C:\\Windows\0es2sim_perf=1\0TZ=\0Z000=xx\0";
    CHECK(es2_os_environment_load(write_block("good.env", block, sizeof block)) == 0);
    char *copy = es2_os_environment();
    CHECK(copy && es2_os_environment_block_size(copy, sizeof block) == sizeof block && !memcmp(copy, block, sizeof block));
    es2_os_environment_free(copy);
    CHECK(es2_os_getenv("ES2_OS_TEST_VARIABLE") == NULL);
    CHECK(es2_os_getenv("PATH") && !strcmp(es2_os_getenv("PATH"), "C:\\Windows"));
    CHECK(es2_os_getenv("ES2SIM_PERF") && !strcmp(es2_os_getenv("ES2SIM_PERF"), "1"));
    CHECK(es2_os_getenv("TZ") && !strcmp(es2_os_getenv("TZ"), ""));
    CHECK(es2_os_getenv("C:") == NULL);
    CHECK(es2_os_getenv("Z00") == NULL);
    CHECK(es2_os_getenv("") == NULL);

    /* The empty block (one NUL) is a block. */
    CHECK(es2_os_environment_load(write_block("none.env", "\0", 1)) == 0);
    CHECK(es2_os_getenv("PATH") == NULL);

    /* es2_os_file_mtime is _stat64's st_mtime, which the CRT derives through
     * local time: on a DST change day it can differ from GetFileTime by an
     * hour, so the reference is _stat64 itself. */
    {
        const char *path = write_block("mtime.bin", "x", 1);
        int64_t seconds = 0;
        struct _stat64 st;
        CHECK(!_stat64(path, &st));
        CHECK(es2_os_file_mtime(path, &seconds) && seconds == (int64_t)st.st_mtime);
        CHECK(!es2_os_file_mtime("Z:\\no\\such\\file", &seconds));
    }

    printf("%s\n", failures ? "FAIL" : "PASS");
    return failures != 0;
}
