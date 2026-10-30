/* The parts of es2/platform/os.h shared by both builds: the environment
 * block of --environment FILE. */
#include <stdlib.h>
#include <string.h>
#include "es2/platform/os.h"
#include "os_internal.h"

/* The loaded block and its size including the final NUL. */
static char *loaded;
static size_t loaded_size;

size_t es2_os_environment_block_size(const char *block, size_t size) {
    size_t i = 0;
    if (!block) return 0;
    for (;;) {
        if (i >= size) return 0;
        if (!block[i]) return i + 1;
        while (i < size && block[i]) ++i;
        if (i >= size) return 0;
        ++i;
    }
}

int es2_os_environment_load(const char *path) {
    es2_os_handle h = es2_os_create_file(path, ES2_OS_GENERIC_READ, ES2_OS_FILE_SHARE_READ, 0, ES2_OS_OPEN_EXISTING, 0);
    if (h == ES2_OS_INVALID_HANDLE) return 1;
    uint32_t size = es2_os_file_size(h), got = 0;
    char *block = size != ES2_OS_INVALID_FILE_SIZE && size && size <= (1u << 20) ? malloc(size) : NULL;
    int ok = block && es2_os_read_file(h, block, size, &got) && got == size;
    es2_os_close_handle(h);
    /* The whole file is the block: strings, then the empty one at the end. */
    if (!ok || es2_os_environment_block_size(block, size) != size) {
        free(block);
        return 1;
    }
    free(loaded);
    loaded = block;
    loaded_size = size;
    return 0;
}

char *es2_os_environment(void) {
    if (!loaded) return es2_os_host_environment();
    char *copy = malloc(loaded_size);
    if (copy) memcpy(copy, loaded, loaded_size);
    return copy;
}

void es2_os_environment_free(char *block) { free(block); }

static int name_is(const char *entry, const char *name, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        char a = entry[i], b = name[i];
        if (a >= 'a' && a <= 'z') a = (char)(a - 32);
        if (b >= 'a' && b <= 'z') b = (char)(b - 32);
        if (a != b) return 0;
    }
    return entry[n] == '=';
}

const char *es2_os_getenv(const char *name) {
    if (!loaded) return es2_os_host_getenv(name);
    size_t n = strlen(name);
    if (!n) return NULL;
    for (const char *p = loaded; *p; p += strlen(p) + 1)
        if (name_is(p, name, n)) return p + n + 1;
    return NULL;
}
