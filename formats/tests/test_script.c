#include "es2/formats/script.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failed;
#define CHECK(x) do { ++checks; if (!(x)) { ++failed; fprintf(stderr, "line %d: %s\n", __LINE__, #x); } } while (0)

#ifdef ES2_SCRIPT_REAL_TEST
#include "es2/formats/sha256.h"
#include <dirent.h>
#include <sys/stat.h>

static uint8_t *load(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    long n;
    uint8_t *p;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)) { fclose(f); return NULL; }
    p = malloc(n ? (size_t)n : 1);
    if (p && fread(p, 1, (size_t)n, f) != (size_t)n) { free(p); p = NULL; }
    if (fclose(f)) { free(p); p = NULL; }
    *size = (size_t)n;
    return p;
}
static int script_name(const char *name)
{
    size_t n = strlen(name), i;
    if (n < 11 || strncmp(name, "script", 6) || strcmp(name + n - 4, ".dat")) return 0;
    for (i = 6; i < n - 4; ++i) if (name[i] < '0' || name[i] > '9') return 0;
    return 1;
}
/* The two prepared examples docs/formats/mission.md lists counts for. */
static const struct { const char *sha256; size_t counts[ES2_SCRIPT_LIST_COUNT], routes; } known[] = {
    {"61efab9bedce8e1a45b484f6fd5157ba0e5d94629ad5881e7ac0720d24a135bb",
     {84, 1, 9, 8, 8, 0, 10, 4, 50, 14, 38, 1, 2}, 41},
    {"dd28822315105ec3054aa54609c13cada1aaef5871f475163b98ee439641cff9",
     {29, 1, 7, 1, 4, 0, 8, 3, 9, 10, 11, 1, 2}, 36},
};
static char seen[64][65];
static size_t files, distinct, matched, tails;
static void check_file(const char *path)
{
    size_t size = 0, i, j;
    uint8_t *data = load(path, &size);
    es2_script s = {0};
    es2_error e = {0};
    char hex[65];
    CHECK(data != NULL);
    if (!data) return;
    ++files;
    es2f_sha256_of(data, size, hex);
    if (es2_script_parse((es2_span){data, size}, &s, &e) != ES2_OK) {
        fprintf(stderr, "%s: %s at byte %zu\n", path, es2_status_string(e.code), e.offset);
        CHECK(0);
    } else {
        CHECK(s.lists[ES2_SCRIPT_STRINGS].records.data + s.lists[ES2_SCRIPT_STRINGS].records.size == data + s.end);
        if (s.trailing.size) ++tails;
        for (i = 0; i < sizeof known / sizeof known[0]; ++i) {
            if (strcmp(hex, known[i].sha256)) continue;
            for (j = 0; j < ES2_SCRIPT_LIST_COUNT; ++j) CHECK(s.lists[j].count == known[i].counts[j]);
            CHECK(s.route_references == known[i].routes && s.end == size && !s.trailing.size);
            ++matched;
        }
    }
    for (i = 0; i < distinct; ++i) if (!strcmp(seen[i], hex)) break;
    if (i == distinct && distinct < sizeof seen / sizeof seen[0]) {
        memcpy(seen[distinct++], hex, 65);
        printf("%s: %zu bytes, sha256 %.12s, counts", path, size, hex);
        for (j = 0; j < ES2_SCRIPT_LIST_COUNT; ++j) printf("%s%zu", j ? "," : " ", s.lists[j].count);
        printf(", %zu route references, end %zu, %zu trailing bytes\n", s.route_references, s.end, s.trailing.size);
    }
    free(data);
}
/* Every installation's SAV\script<n>.dat beside the CD (build/installs, two levels above
 * ES2_DATA_ROOT; ES2_SCRIPT_INSTALLS overrides). DATA\ is not read: a live regression
 * replaces a clone's DATA\ while it runs, and its script.dat is one of SAV's. */
int main(void)
{
    const char *root = getenv("ES2_DATA_ROOT"), *installs = getenv("ES2_SCRIPT_INSTALLS");
    char base[4096];
    DIR *top;
    struct dirent *entry;
    if (!root || !*root) { puts("SKIP: ES2_DATA_ROOT unset"); return 0; }
    if (!installs || !*installs) {
        if (snprintf(base, sizeof base, "%s/../../installs", root) >= (int)sizeof base) return 1;
        installs = base;
    }
    top = opendir(installs);
    if (!top) { printf("SKIP: no installations at %s\n", installs); return 0; }
    while ((entry = readdir(top)) != NULL) {
        char dir[4096];
        DIR *sav;
        struct dirent *f;
        if (entry->d_name[0] == '.') continue;
        if (snprintf(dir, sizeof dir, "%s/%s/SAV", installs, entry->d_name) >= (int)sizeof dir) continue;
        if (!(sav = opendir(dir))) continue;
        while ((f = readdir(sav)) != NULL) {
            char path[4096];
            if (!script_name(f->d_name)) continue;
            if (snprintf(path, sizeof path, "%s/%s", dir, f->d_name) >= (int)sizeof path) continue;
            check_file(path);
        }
        closedir(sav);
    }
    closedir(top);
    CHECK(files > 0);
    printf("script real: %zu files, %zu distinct, %zu of the documented examples, %zu with a stale tail; "
           "%u checks, %u failures\n", files, distinct, matched, tails, checks, failed);
    return failed ? 1 : 0;
}
#else
static void w(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
/* A synthetic script: two coordinates, one angle, routes {0, 1} and {}, one record in each
 * fixed-stride list except timers, flyers, orders and objectives, two string indices. */
static size_t build(uint8_t *b, size_t *offsets)
{
    static const unsigned counts[ES2_SCRIPT_LIST_COUNT] = {2, 1, 2, 1, 1, 0, 1, 0, 1, 0, 1, 0, 2};
    size_t p = 20, i;
    for (i = 0; i < 20; ++i) b[i] = (uint8_t)(i * 13 + 1);
    w(b + 2, 7000);
    for (i = 0; i < ES2_SCRIPT_LIST_COUNT; ++i) {
        offsets[i] = p;
        w(b + p, counts[i]); p += 2;
        if (i == ES2_SCRIPT_ROUTES) {
            w(b + p, 2); w(b + p + 2, 0); w(b + p + 4, 1); w(b + p + 6, 0); p += 8;
        } else {
            size_t n = es2_script_stride((es2_script_list)i) * counts[i], k;
            for (k = 0; k < n; ++k) b[p + k] = (uint8_t)(k * 7 + i);
            p += n;
        }
    }
    return p;
}
int main(void)
{
    uint8_t bytes[600], mutated[601];
    size_t offsets[ES2_SCRIPT_LIST_COUNT], n, i, at;
    es2_script s, v;
    es2_span refs;
    es2_error e;
    n = build(bytes, offsets);
    CHECK(n == 20 + 26 + 24 + 2 + 8 + 6 + 74 + 134 + 52 + 156 + 4);
    CHECK(es2_script_parse((es2_span){bytes, n}, &s, &e) == ES2_OK);
    CHECK(s.header[1] == 7000 && s.route_references == 2 && s.end == n && s.trailing.size == 0);
    CHECK(s.lists[ES2_SCRIPT_COORDINATES].count == 2 && s.lists[ES2_SCRIPT_COORDINATES].records.size == 24);
    CHECK(s.lists[ES2_SCRIPT_ROUTES].count == 2 && s.lists[ES2_SCRIPT_ROUTES].records.size == 8);
    CHECK(s.lists[ES2_SCRIPT_TIMERS].count == 0 && s.lists[ES2_SCRIPT_TIMERS].records.size == 0);
    CHECK(s.lists[ES2_SCRIPT_GROUPS].records.size == 156 && s.lists[ES2_SCRIPT_STRINGS].count == 2);
    for (i = 0; i < ES2_SCRIPT_LIST_COUNT; ++i) CHECK(s.lists[i].offset == offsets[i]);
    CHECK(es2_script_stride(ES2_SCRIPT_TRIGGERS) == 74 && es2_script_stride(ES2_SCRIPT_ROUTES) == 0);
    CHECK(es2_script_stride(ES2_SCRIPT_LIST_COUNT) == 0);
    CHECK(es2_script_route_at(&s, 0, &refs, &at, &e) == ES2_OK && refs.size == 4 && at == offsets[2] + 2);
    CHECK(es2_script_route_at(&s, 1, &refs, &at, &e) == ES2_OK && refs.size == 0 && at == offsets[2] + 8);
    CHECK(es2_script_route_at(&s, 2, &refs, &at, &e) == ES2_ERR_OFFSET);
    CHECK(es2_script_parse((es2_span){bytes, n}, NULL, &e) == ES2_ERR_ARGUMENT);
    /* Every shorter file ends inside a header word, a count or a record. */
    for (i = 0; i < n; ++i) {
        v = s;
        CHECK(es2_script_parse((es2_span){bytes, i}, &v, &e) == ES2_ERR_TRUNCATED);
        CHECK(v.input.data == s.input.data && v.route_references == s.route_references);
    }
    /* A stale tail after the last list. */
    memcpy(mutated, bytes, n); mutated[n] = 0x5a;
    CHECK(es2_script_parse((es2_span){mutated, n + 1}, &v, &e) == ES2_OK);
    CHECK(v.end == n && v.trailing.size == 1 && v.trailing.data == mutated + n && v.route_references == 2);
    for (i = 0; i < ES2_SCRIPT_LIST_COUNT; ++i) {
        memcpy(mutated, bytes, n); w(mutated + offsets[i], 0xffff);
        CHECK(es2_script_parse((es2_span){mutated, n}, &v, &e) == ES2_ERR_COUNT && e.offset == offsets[i]);
    }
    memcpy(mutated, bytes, n); w(mutated + offsets[2] + 2, 0x8000);
    CHECK(es2_script_parse((es2_span){mutated, n}, &v, &e) == ES2_ERR_COUNT && e.offset == offsets[2] + 2);
    /* Route references: the coordinate count is the bound, negative indices are refused. */
    memcpy(mutated, bytes, n); w(mutated + offsets[2] + 6, 2);
    CHECK(es2_script_parse((es2_span){mutated, n}, &v, &e) == ES2_ERR_OFFSET && e.offset == offsets[2] + 6);
    memcpy(mutated, bytes, n); w(mutated + offsets[2] + 4, 0xffff);
    CHECK(es2_script_parse((es2_span){mutated, n}, &v, &e) == ES2_ERR_OFFSET && e.offset == offsets[2] + 4);
    /* One coordinate less: reference 1 is out of range, and 12 bytes move to the angles. */
    memcpy(mutated, bytes, n); w(mutated + offsets[0], 1);
    CHECK(es2_script_parse((es2_span){mutated, n}, &v, &e) != ES2_OK);
    /* A count that is too large runs past the end; one too small leaves a tail. */
    memcpy(mutated, bytes, n); w(mutated + offsets[ES2_SCRIPT_GROUPS], 2);
    CHECK(es2_script_parse((es2_span){mutated, n}, &v, &e) == ES2_ERR_TRUNCATED);
    memcpy(mutated, bytes, n); w(mutated + offsets[ES2_SCRIPT_STRINGS], 1);
    CHECK(es2_script_parse((es2_span){mutated, n}, &v, &e) == ES2_OK && v.trailing.size == 2);
    /* The smallest script: the header and 13 zero counts. */
    memset(mutated, 0, 46);
    CHECK(es2_script_parse((es2_span){mutated, 46}, &v, &e) == ES2_OK && v.route_references == 0);
    CHECK(es2_script_parse((es2_span){mutated, 19}, &v, &e) == ES2_ERR_TRUNCATED);
    printf("script: %u checks, %u failures\n", checks, failed);
    return failed ? 1 : 0;
}
#endif
