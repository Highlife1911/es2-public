/* Real-data count test: every DBA and DBM member of the CD's
 * VOL/SHELL0.VOL (VSHELL's resources) parsed strictly and every image
 * decoded with es2_dbm_decode; counts per compression kind, flags, depth and
 * embedded colours, exact consumption of each record's stored bytes, and an
 * FNV-1a 64 digest over all decoded pixels. Prints counts and digests only,
 * never pixels. Skipped when ES2_DATA_ROOT is unset or empty. */
#include "es2/formats/dbm.h"
#include "es2/formats/vol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

static uint8_t *read_file(const char *root, const char *name, size_t *length)
{
    char path[4096];
    FILE *file;
    long size;
    uint8_t *bytes;
    int count = snprintf(path, sizeof path, "%s/VOL/%s", root, name);
    if (count < 0 || (size_t)count >= sizeof path) return NULL;
    file = fopen(path, "rb");
    if (file == NULL) return NULL;
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    bytes = malloc((size_t)size + 1);
    if (bytes == NULL || fread(bytes, 1, (size_t)size, file) != (size_t)size) {
        free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *length = (size_t)size;
    return bytes;
}

static int has_suffix(es2_span name, const char *suffix)
{
    size_t n = strlen(suffix), i;
    if (name.size < n) return 0;
    for (i = 0; i < n; ++i) {
        uint8_t c = name.data[name.size - n + i];
        if (c >= 'a' && c <= 'z') c = (uint8_t)(c - 32);
        if (c != (uint8_t)suffix[i]) return 0;
    }
    return 1;
}

static uint64_t digest = UINT64_C(0xcbf29ce484222325);
static size_t kinds[256], flag_values[256], depths[256], coloured, images, pixels, consumed_ok;
static uint8_t scratch[640 * 480];

static void image(const es2_dbm *dbm)
{
    size_t written = 0, consumed = 0, i;
    ++images;
    ++kinds[dbm->compression];
    ++flag_values[dbm->flags];
    ++depths[dbm->bits_per_pixel];
    coloured += dbm->colour_count != 0;
    CHECK((size_t)dbm->width * dbm->height <= sizeof scratch);
    if (es2_dbm_decode(dbm, scratch, sizeof scratch, &written, &consumed, NULL) != ES2_OK) {
        CHECK(0);
        return;
    }
    CHECK(written == (size_t)dbm->width * dbm->height);
    consumed_ok += consumed == dbm->pixels.size;
    pixels += written;
    for (i = 0; i < written; ++i) {
        digest ^= scratch[i];
        digest *= UINT64_C(0x100000001b3);
    }
}

int main(void)
{
    const char *root = getenv("ES2_DATA_ROOT");
    size_t length = 0, m, dba_members = 0, dbm_members = 0, trailers = 0, i;
    uint8_t *bytes;
    es2_vol vol;
    if (root == NULL || *root == 0) { puts("SKIP real dbm: ES2_DATA_ROOT not set"); return 0; }
    bytes = read_file(root, "SHELL0.VOL", &length);
    CHECK(bytes != NULL);
    if (bytes == NULL || es2_vol_open((es2_span){bytes, length}, &vol, NULL) != ES2_OK) {
        CHECK(0);
        free(bytes);
        return 1;
    }
    for (m = 0; m < vol.member_count; ++m) {
        es2_vol_member member;
        if (es2_vol_member_at(&vol, m, &member, NULL) != ES2_OK) { CHECK(0); continue; }
        if (has_suffix(member.name, ".DBA")) {
            es2_dba dba;
            uint32_t k;
            ++dba_members;
            if (es2_dba_parse_prefix(member.payload, &dba, NULL) != ES2_OK) { CHECK(0); continue; }
            trailers += dba.trailing.size != 0;
            for (k = 0; k < dba.count; ++k) {
                es2_dbm dbm;
                if (es2_dba_image_at(&dba, k, &dbm, NULL) != ES2_OK) { CHECK(0); continue; }
                image(&dbm);
            }
        } else if (has_suffix(member.name, ".DBM")) {
            es2_dbm dbm;
            ++dbm_members;
            if (es2_dbm_parse(member.payload, &dbm, NULL) != ES2_OK) { CHECK(0); continue; }
            image(&dbm);
        }
    }
    /* dbm.md's audit: 83 DBA members with 1,275 images and 10 DBMs, all
     * raw 8-bit, flags 0, no embedded colours, no trailer in SHELL0. */
    CHECK(dba_members == 83 && dbm_members == 10 && images == 1285);
    CHECK(kinds[0] == images && flag_values[0] == images && depths[8] == images);
    CHECK(coloured == 0 && trailers == 0 && consumed_ok == images);
    printf("SHELL0.VOL: %zu DBA and %zu DBM members, %zu images, %zu pixel bytes, "
           "%zu consumed exactly, %zu with embedded colours, %zu DBA trailers\n",
           dba_members, dbm_members, images, pixels, consumed_ok, coloured, trailers);
    for (i = 0; i < 256; ++i)
        if (kinds[i]) printf("compression kind %zu: %zu images\n", i, kinds[i]);
    for (i = 0; i < 256; ++i)
        if (flag_values[i]) printf("flags 0x%02zx: %zu images\n", i, flag_values[i]);
    printf("decoded pixels FNV-1a 64: %016llx\n", (unsigned long long)digest);
    free(bytes);
    printf("dbm real data: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
