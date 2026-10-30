/* wasm_unpack <bundle> <name> <file>...: writes the named entries of a link's bundle (wasm_bundle.h) to the given
   paths; each <name> is followed by its output path. An entry that is not asked for is ignored, a missing one is an
   error. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wasm_bundle.h"

int main(int argc, char **argv) {
    FILE *in;
    char magic[WASM_BUNDLE_MAGIC_LENGTH];
    int wanted = (argc - 2) / 2, i, *found;
    uint32_t length, size;
    int status;
    if (argc < 4 || (argc - 2) % 2) {
        fputs("usage: wasm_unpack <bundle> (<name> <output>)...\n", stderr);
        return 2;
    }
    in = fopen(argv[1], "rb");
    if (!in || fread(magic, 1, sizeof magic, in) != sizeof magic || memcmp(magic, WASM_BUNDLE_MAGIC, sizeof magic)) {
        fprintf(stderr, "wasm_unpack: not a wasm bundle: %s\n", argv[1]);
        return 2;
    }
    found = (int *)calloc((size_t)wanted, sizeof *found);
    if (!found) return 2;
    while ((status = wasm_bundle_read_u32(in, &length)) > 0) {
        char *name = (char *)malloc((size_t)length + 1);
        unsigned char *data;
        if (!name || length > 4096 || fread(name, 1, length, in) != length) goto corrupt;
        name[length] = 0;
        if (wasm_bundle_read_u32(in, &size) != 1) goto corrupt;
        data = (unsigned char *)malloc((size_t)size + 1);
        if (!data || fread(data, 1, size, in) != size) goto corrupt;
        for (i = 0; i < wanted; i++) {
            if (strcmp(argv[2 + 2 * i], name) == 0) {
                FILE *out = fopen(argv[3 + 2 * i], "wb");
                if (!out || fwrite(data, 1, size, out) != size || fclose(out)) {
                    fprintf(stderr, "wasm_unpack: cannot write %s\n", argv[3 + 2 * i]);
                    return 2;
                }
                found[i] = 1;
            }
        }
        free(data);
        free(name);
    }
    if (status < 0) goto corrupt;
    fclose(in);
    for (i = 0; i < wanted; i++) {
        if (!found[i]) {
            fprintf(stderr, "wasm_unpack: %s has no entry %s\n", argv[1], argv[2 + 2 * i]);
            return 1;
        }
    }
    free(found);
    return 0;
corrupt:
    fprintf(stderr, "wasm_unpack: truncated bundle: %s\n", argv[1]);
    return 2;
}
