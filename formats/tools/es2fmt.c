/* Statistics-only, opt-in corpus client. Library modules themselves do no I/O. */
#include "es2/formats/vol.h"
#include "es2/formats/dpl.h"
#include "es2/formats/dbm.h"
#include "es2/formats/msn.h"
#include "es2/formats/dts.h"
#include "es2/formats/dat.h"
#include "es2/formats/vfs.h"
#include "collision_report.h"
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct extension_count { char name[14]; size_t count; } extension_count;
typedef struct format_count { const char *name; size_t ok, failed; } format_count;
typedef struct archive_expectation { const char *path; size_t members; } archive_expectation;
static const archive_expectation reference_archives[] = {
    {"VER31/SIMVOICE.VOL",213}, {"VER31/SIMVOICF.VOL",213}, {"VER31/SIMVOICG.VOL",213},
    {"VOL/LANG0.VOL",21}, {"VOL/SHELL0.VOL",170}, {"VOL/SHELL1.VOL",4},
    {"VOL/SHLSOUND.VOL",5}, {"VOL/SIMALERT.VOL",23}, {"VOL/SIMLANG.VOL",26},
    {"VOL/SIMPATCH.VOL",10}, {"VOL/SIMSOUND.VOL",641}, {"VOL/SIMVOICE.VOL",213},
    {"VOL/SIMVOICF.VOL",213}, {"VOL/SIMVOICG.VOL",213},
    {"VOL/SIMVOL0.VOL",1300}, {"VOL/ZONES.VOL",324}
};
enum { FORMAT_MSN, FORMAT_DTS, FORMAT_DPL, FORMAT_DBM, FORMAT_DBA, FORMAT_HBA, FORMAT_COUNT };
typedef struct retained_archive {
    es2_vol archive;
    uint8_t *bytes;
    char *path;
    struct retained_archive *next;
} retained_archive;
typedef struct retained_loose {
    uint8_t *bytes;
    size_t size;
    char *path;
    struct retained_loose *next;
} retained_loose;
typedef struct dat_count {
    uint64_t files, ok, failed, bytes, records, secondary, children, lists, groups;
} dat_count;
typedef struct corpus {
    size_t archives, members, skipped, io_errors, malformed_archives, unexpected, known;
    uint64_t archive_bytes, payload_bytes, images, pixels, colours, ramps, roots, objects;
    size_t max_depth;
    uint64_t tags[ES2_DTS_TAG_COUNT];
    size_t seen_archives[16];
    extension_count extensions[256];
    size_t extension_count;
    format_count formats[FORMAT_COUNT];
    retained_archive *archive_memory;
    retained_loose *loose_memory;
    size_t retained_archives, loose_files;
    uint64_t loose_bytes, dat_inputs, dat_archive_inputs, dat_loose_inputs, dat_bytes;
    uint64_t dat_support[4], dat_suffix_files, dat_suffix_bytes, dat_slot_exceptions;
    uint64_t dat_weapon_a, dat_weapon_b;
    dat_count dat[ES2_DAT_KIND_COUNT];
} corpus;

static unsigned char lower_ascii(unsigned char c)
{ return c >= 'A' && c <= 'Z' ? (unsigned char)(c + ('a' - 'A')) : c; }
static int equal_ascii(const char *a, const char *b)
{
    while (*a && *b) {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x == '\\') x = '/';
        if (y == '\\') y = '/';
        if (lower_ascii(x) != lower_ascii(y)) return 0;
    }
    return *a == *b;
}
static int span_equal(es2_span a, const char *b)
{
    size_t i, n = strlen(b);
    if (a.size != n) return 0;
    for (i = 0; i < n; ++i) if (lower_ascii(a.data[i]) != lower_ascii((unsigned char)b[i])) return 0;
    return 1;
}
static int has_vol_extension(const char *name)
{
    size_t n = strlen(name);
    return n >= 4 && equal_ascii(name + n - 4, ".vol");
}
static char *join_path(const char *a, const char *b)
{
    size_t na = strlen(a), nb = strlen(b);
    char *path;
    if (nb > SIZE_MAX - 2 || na > SIZE_MAX - nb - 2) return NULL;
    path = (char *)malloc(na + nb + 2);
    if (path != NULL) {
        memcpy(path, a, na);
        if (na != 0) path[na++] = '/';
        memcpy(path + na, b, nb + 1);
    }
    return path;
}
static char *copy_string(const char *text)
{
    size_t length = strlen(text);
    char *copy;
    if (length == SIZE_MAX) return NULL;
    copy = (char *)malloc(length + 1);
    if (copy != NULL) memcpy(copy, text, length + 1);
    return copy;
}
static char *member_path(const es2_vol_member *member)
{
    size_t a = member->directory.size, b = member->name.size;
    char *path;
    if (b == SIZE_MAX || a > SIZE_MAX - b - 1) return NULL;
    path = (char *)malloc(a + b + 1);
    if (path != NULL) {
        memcpy(path, member->directory.data, a);
        memcpy(path + a, member->name.data, b);
        path[a + b] = '\0';
    }
    return path;
}
static int load_file(const char *path, uint8_t **bytes, size_t *size)
{
    FILE *file = fopen(path, "rb");
    long length;
    uint8_t *buffer;
    size_t read_count;
    if (file == NULL) return 0;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0) {
        (void)fclose(file); return 0;
    }
    if ((uintmax_t)length > SIZE_MAX) { (void)fclose(file); return 0; }
    buffer = (uint8_t *)malloc(length == 0 ? 1 : (size_t)length);
    if (buffer == NULL) { (void)fclose(file); return 0; }
    read_count = fread(buffer, 1, (size_t)length, file);
    if (fclose(file) != 0 || read_count != (size_t)length) { free(buffer); return 0; }
    *bytes = buffer; *size = (size_t)length;
    return 1;
}
static void extension_of(es2_span name, char out[14])
{
    size_t start = name.size, i, n = 0;
    for (i = 0; i < name.size; ++i) if (name.data[i] == '.') start = i + 1;
    for (i = start; i < name.size && n < 13; ++i) {
        unsigned char c = lower_ascii(name.data[i]);
        out[n++] = c >= 'a' && c <= 'z' ? (char)(c - ('a' - 'A')) : (char)c;
    }
    out[n] = '\0';
}
static void count_extension(corpus *c, const char *name)
{
    size_t i;
    for (i = 0; i < c->extension_count; ++i) {
        if (strcmp(c->extensions[i].name, name) == 0) { ++c->extensions[i].count; return; }
    }
    if (i == sizeof c->extensions / sizeof c->extensions[0]) { ++c->unexpected; return; }
    (void)snprintf(c->extensions[i].name, sizeof c->extensions[i].name, "%s", name);
    c->extensions[i].count = 1; ++c->extension_count;
}
static const char *dat_support_name(es2_dat_support support)
{
    switch (support) {
    case ES2_DAT_READER_FRAMING: return "confirmed-reader-framing";
    case ES2_DAT_DATA_FIT_ONLY: return "candidate-data-fit-only";
    case ES2_DAT_EXTERNAL_COUNT: return "external-count-framing";
    case ES2_DAT_PREFIX_WITH_SUFFIX: return "confirmed-prefix-opaque-suffix";
    }
    return "unknown";
}
static void inspect_dat(corpus *c, const char *source, const char *path,
                        es2_span bytes, const es2_vol *archive)
{
    es2_dat_kind kind = es2_dat_classify(path);
    es2_dat table;
    es2_error error;
    es2_status status = ES2_OK;
    size_t external_count = 0, i;
    dat_count *counts = &c->dat[kind];
    ++c->dat_inputs; c->dat_bytes += bytes.size;
    if (archive == NULL) ++c->dat_loose_inputs; else ++c->dat_archive_inputs;
    ++counts->files; counts->bytes += bytes.size;
    if (kind == ES2_DAT_FIRE) {
        /* Explicit CD framing assumption; the shape-array dependency is unknown. */
        external_count = 4;
        printf("DAT-conditional source=%s path=%s caller_external_count=4 original_shape_dependency=unverified\n", source, path);
    } else if (kind == ES2_DAT_BASECOL) {
        es2_vol_member bases;
        es2_dat dependency;
        if (archive == NULL) status = es2_fail(&error, ES2_ERR_ARGUMENT, 0);
        else status = es2_vol_find(archive, "dat/BASES.DAT", &bases, &error);
        if (status == ES2_OK)
            status = es2_dat_parse(ES2_DAT_BASES, bases.payload, 0, &dependency, &error);
        if (status == ES2_OK) {
            external_count = dependency.count;
            printf("DAT-dependency source=%s path=%s external_count=%zu from=dat/BASES.DAT\n", source, path, external_count);
        }
    }
    if (status == ES2_OK) status = es2_dat_parse(kind, bytes, external_count, &table, &error);
    if (status != ES2_OK) {
        ++counts->failed; ++c->unexpected;
        printf("DAT-failed source=%s path=%s structure=%s error=%s offset=%zu\n",
               source, path, es2_dat_kind_name(kind), es2_status_string(status), error.offset);
        return;
    }
    ++counts->ok; counts->records += table.count; counts->secondary += table.secondary_count;
    counts->children += table.child_count; counts->lists += table.byte_list_count;
    counts->groups += table.nested_group_count;
    if ((unsigned)table.support < 4) ++c->dat_support[table.support];
    if (table.support != ES2_DAT_READER_FRAMING)
        printf("DAT-scope source=%s path=%s support=%s\n", source, path, dat_support_name(table.support));
    if (table.suffix.size != 0) {
        ++c->dat_suffix_files; c->dat_suffix_bytes += table.suffix.size;
        printf("DAT-opaque-suffix source=%s path=%s active_records=%zu prefix_bytes=%zu suffix_bytes=%zu\n",
               source, path, table.count, bytes.size - table.suffix.size, table.suffix.size);
    }
    if (kind == ES2_DAT_SIM_WEAPONS) {
        for (i = 0; i < table.count; ++i) {
            es2_dat_sim_weapon weapon;
            status = es2_dat_sim_weapon_at(&table, i, &weapon, &error);
            if (status != ES2_OK) { ++c->unexpected; break; }
            c->dat_weapon_a += weapon.count_a; c->dat_weapon_b += weapon.count_b;
        }
    }
    if (kind == ES2_DAT_LOADOUT || kind == ES2_DAT_TRN_HERC) {
        for (i = 0; i < table.count; ++i) {
            es2_dat_loadout loadout;
            size_t j;
            int sparse = 0;
            status = es2_dat_loadout_at(&table, i, &loadout, &error);
            if (status != ES2_OK) { ++c->unexpected; break; }
            for (j = 0; j < loadout.slot_count; ++j) {
                es2_dat_loadout_slot slot;
                status = es2_dat_loadout_slot_at(&loadout, j, &slot, &error);
                if (status != ES2_OK) { ++c->unexpected; break; }
                if (slot.slot_id >= 0 && (size_t)slot.slot_id >= loadout.slot_count) sparse = 1;
            }
            if (sparse) ++c->dat_slot_exceptions;
        }
    }
    if (kind == ES2_DAT_ARM_WEAP)
        printf("DAT-consumer-exception source=%s path=%s second_list_records=%zu second_list_bytes=%zu consumed_by_full_parser=yes\n",
               source, path, table.secondary_count, table.secondary.size);
}
static void inspect_member(corpus *c, const char *archive_name,
                           const es2_vol *archive, const es2_vol_member *m)
{
    char ext[14];
    es2_error error = {ES2_OK,0,"ok"};
    es2_status status = ES2_OK;
    int f = -1, expected = 0;
    extension_of(m->name, ext); count_extension(c, ext);
    c->payload_bytes += m->length;
    if (strcmp(ext, "MSN") == 0) {
        es2_msn mission;
        f = FORMAT_MSN; status = es2_msn_parse(m->payload, &mission, &error);
        expected = equal_ascii(archive_name, "VOL/ZONES.VOL") && span_equal(m->directory, "MSN\\")
            && span_equal(m->name, "DEMO2.MSN") && m->length == 8184
            && status == ES2_ERR_TRUNCATED && error.offset == 8168;
    } else if (strcmp(ext, "DTS") == 0) {
        es2_dts_stats stats;
        size_t i;
        f = FORMAT_DTS; status = es2_dts_walk(m->payload, 64, NULL, NULL, &stats, &error);
        if (status == ES2_OK) {
            c->roots += stats.roots; c->objects += stats.objects;
            if (stats.max_depth > c->max_depth) c->max_depth = stats.max_depth;
            for (i = 0; i < ES2_DTS_TAG_COUNT; ++i) c->tags[i] += stats.counts[i];
        }
    } else if (strcmp(ext, "DPL") == 0) {
        es2_dpl palette;
        f = FORMAT_DPL; status = es2_dpl_parse(m->payload, &palette, &error);
        if (status == ES2_OK) { c->colours += palette.colour_count; c->ramps += palette.ramp_count; }
    } else if (strcmp(ext, "DBM") == 0) {
        es2_dbm bitmap;
        f = FORMAT_DBM; status = es2_dbm_parse(m->payload, &bitmap, &error);
        if (status == ES2_OK) {
            ++c->images; c->pixels += (uint64_t)bitmap.width * bitmap.height;
        }
    } else if (strcmp(ext, "DBA") == 0 || strcmp(ext, "HBA") == 0) {
        es2_dba array;
        f = strcmp(ext, "DBA") == 0 ? FORMAT_DBA : FORMAT_HBA;
        status = es2_dba_parse(m->payload, &array, &error);
        if (status == ES2_OK) { c->images += array.count; c->pixels += array.total_pixel_bytes; }
        else if (status == ES2_ERR_LENGTH && equal_ascii(archive_name, "VOL/SIMVOL0.VOL")
                 && span_equal(m->directory, "dba\\") && span_equal(m->name, "CORNERS.DBA")
                 && m->length == 2330) {
            es2_error prefix_error;
            if (es2_dba_parse_prefix(m->payload, &array, &prefix_error) == ES2_OK
                && array.count == 24 && array.serialized_size == 1724 && array.trailing.size == 606) {
                expected = 1; c->images += array.count; c->pixels += array.total_pixel_bytes;
                printf("known-trailer archive=%s member=CORNERS.DBA declared_images=24 bytes=606\n", archive_name);
            }
        }
    }
    if (strcmp(ext, "DAT") == 0) {
        char *path = member_path(m);
        if (path == NULL) ++c->io_errors;
        else { inspect_dat(c, archive_name, path, m->payload, archive); free(path); }
    }
    if (f >= 0) {
        if (status == ES2_OK) ++c->formats[f].ok;
        else {
            ++c->formats[f].failed;
            if (expected) ++c->known; else ++c->unexpected;
            printf("parse-failed archive=%s member=%.*s%.*s format=%s error=%s offset=%zu documented=%s\n",
                archive_name, (int)m->directory.size, (const char *)m->directory.data,
                (int)m->name.size, (const char *)m->name.data, c->formats[f].name,
                es2_status_string(status), error.offset, expected ? "yes" : "no");
        }
    }
}
static void inspect_archive(corpus *c, const char *path, const char *relative)
{
    uint8_t *bytes;
    size_t size, i;
    es2_vol archive;
    es2_error error;
    es2_status status;
    if (!load_file(path, &bytes, &size)) {
        fprintf(stderr, "read-failed path=%s\n", path); ++c->io_errors; return;
    }
    if (size < 4 || memcmp(bytes, "VOLN", 4) != 0) { ++c->skipped; free(bytes); return; }
    status = es2_vol_open((es2_span){bytes,size}, &archive, &error);
    if (status != ES2_OK) {
        fprintf(stderr, "archive-failed path=%s error=%s offset=%zu\n", relative, error.message, error.offset);
        ++c->malformed_archives; free(bytes); return;
    }
    ++c->archives; c->members += archive.member_count; c->archive_bytes += size;
    printf("archive=%s members=%zu directories=%zu bytes=%zu\n", relative, archive.member_count, archive.directory_count, size);
    for (i = 0; i < sizeof reference_archives / sizeof reference_archives[0]; ++i) {
        if (equal_ascii(relative, reference_archives[i].path)) {
            ++c->seen_archives[i];
            if (archive.member_count != reference_archives[i].members) {
                printf("mismatch archive=%s expected_members=%zu actual=%zu\n", relative, reference_archives[i].members, archive.member_count);
                ++c->unexpected;
            }
            break;
        }
    }
    if (i == sizeof reference_archives / sizeof reference_archives[0]) ++c->unexpected;
    for (i = 0; i < archive.member_count; ++i) {
        es2_vol_member member;
        status = es2_vol_member_at(&archive, i, &member, &error);
        if (status != ES2_OK) { ++c->unexpected; break; }
        inspect_member(c, relative, &archive, &member);
    }
    {
        retained_archive *keep = (retained_archive *)malloc(sizeof *keep);
        char *name = copy_string(relative);
        if (keep == NULL || name == NULL) {
            free(keep); free(name); free(bytes); ++c->io_errors; return;
        }
        keep->archive = archive; keep->bytes = bytes; keep->path = name;
        keep->next = c->archive_memory; c->archive_memory = keep;
        ++c->retained_archives;
    }
}
static int data_path(const char *path)
{
    return strlen(path) > 5 && lower_ascii((unsigned char)path[0]) == 'd'
        && lower_ascii((unsigned char)path[1]) == 'a'
        && lower_ascii((unsigned char)path[2]) == 't'
        && lower_ascii((unsigned char)path[3]) == 'a'
        && (path[4] == '/' || path[4] == '\\');
}
static void inspect_loose(corpus *c, const char *path, const char *relative)
{
    uint8_t *bytes;
    size_t size, length = strlen(relative);
    retained_loose *keep;
    char *name;
    if (!load_file(path, &bytes, &size)) {
        fprintf(stderr, "loose-read-failed path=%s\n", path); ++c->io_errors; return;
    }
    keep = (retained_loose *)malloc(sizeof *keep);
    name = copy_string(relative);
    if (keep == NULL || name == NULL) {
        free(keep); free(name); free(bytes); ++c->io_errors; return;
    }
    keep->bytes = bytes; keep->size = size; keep->path = name;
    keep->next = c->loose_memory; c->loose_memory = keep;
    ++c->loose_files; c->loose_bytes += size;
    if (length >= 4 && equal_ascii(relative + length - 4, ".dat"))
        inspect_dat(c, "loose-CD", relative, (es2_span){bytes,size}, NULL);
}
static void walk_directory(corpus *c, const char *root, const char *relative, size_t depth)
{
    char *path = join_path(root, relative);
    DIR *directory;
    struct dirent *entry;
    if (path == NULL) { ++c->io_errors; return; }
    if (depth > 64) { fprintf(stderr, "directory-depth-limit path=%s\n", path); ++c->io_errors; free(path); return; }
    directory = opendir(path);
    if (directory == NULL) { fprintf(stderr, "directory-failed path=%s\n", path); ++c->io_errors; free(path); return; }
    for (;;) {
        char *child, *absolute;
        struct stat info;
        errno = 0; entry = readdir(directory);
        if (entry == NULL) { if (errno != 0) ++c->io_errors; break; }
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        child = join_path(relative, entry->d_name);
        absolute = child == NULL ? NULL : join_path(root, child);
        if (absolute == NULL) { free(child); ++c->io_errors; continue; }
        if (stat(absolute, &info) != 0) { fprintf(stderr, "stat-failed path=%s\n", absolute); ++c->io_errors; }
        else if (S_ISDIR(info.st_mode)) walk_directory(c, root, child, depth + 1);
        else if (S_ISREG(info.st_mode)) {
            if (has_vol_extension(entry->d_name)) inspect_archive(c, absolute, child);
            else if (data_path(child)) inspect_loose(c, absolute, child);
        }
        free(absolute); free(child);
    }
    if (closedir(directory) != 0) ++c->io_errors;
    free(path);
}
static int extension_compare(const void *a, const void *b)
{ return strcmp(((const extension_count *)a)->name, ((const extension_count *)b)->name); }
static size_t expect_count(const char *label, uint64_t actual, uint64_t expected)
{
    if (actual == expected) return 0;
    printf("mismatch %s expected=%" PRIu64 " actual=%" PRIu64 "\n", label, expected, actual);
    return 1;
}
static int path_compare(const char *a, const char *b)
{
    const char *original_a = a, *original_b = b;
    while (*a != '\0' && *b != '\0') {
        unsigned char x = lower_ascii((unsigned char)*a++), y = lower_ascii((unsigned char)*b++);
        if (x == '\\') x = '/';
        if (y == '\\') y = '/';
        if (x != y) return x < y ? -1 : 1;
    }
    if (*a != *b) return *a == '\0' ? -1 : 1;
    return strcmp(original_a, original_b);
}
static int archive_compare(const void *a, const void *b)
{
    return path_compare((*(retained_archive *const *)a)->path,
                        (*(retained_archive *const *)b)->path);
}
static int loose_compare(const void *a, const void *b)
{
    return path_compare(((const es2_vfs_file *)a)->path, ((const es2_vfs_file *)b)->path);
}
static int vol_directory_path(const char *path)
{
    return strlen(path) > 4 && lower_ascii((unsigned char)path[0]) == 'v'
        && lower_ascii((unsigned char)path[1]) == 'o'
        && lower_ascii((unsigned char)path[2]) == 'l'
        && (path[3] == '/' || path[3] == '\\');
}
static void print_vfs_path(es2_vfs_path path)
{
    size_t part, i;
    es2_span spans[2] = {path.prefix, path.name};
    for (part = 0; part < 2; ++part) {
        for (i = 0; i < spans[part].size; ++i) {
            unsigned char ch = lower_ascii(spans[part].data[i]);
            (void)putchar(ch == '\\' ? '/' : ch);
        }
    }
}
static size_t report_vfs_view(corpus *c, const char *label,
                              const es2_vfs_source *sources, const char *const *names,
                              size_t source_count, int baseline)
{
    es2_vfs vfs;
    es2_vfs_stats stats;
    es2_vfs_entry *entries = NULL;
    es2_vfs_copy *copies = NULL;
    es2_error error;
    es2_status status = es2_vfs_open(sources, source_count, &vfs, &error);
    size_t i, mismatches = 0;
    if (status != ES2_OK) {
        printf("VFS-failed view=%s stage=open error=%s offset=%zu\n", label, es2_status_string(status), error.offset);
        ++c->unexpected; return 0;
    }
    if (vfs.file_count > SIZE_MAX / sizeof *entries || vfs.file_count > SIZE_MAX / sizeof *copies) {
        ++c->io_errors; return 0;
    }
    if (vfs.file_count != 0) {
        entries = (es2_vfs_entry *)malloc(vfs.file_count * sizeof *entries);
        copies = (es2_vfs_copy *)malloc(vfs.file_count * sizeof *copies);
        if (entries == NULL || copies == NULL) {
            free(entries); free(copies); ++c->io_errors; return 0;
        }
    }
    status = es2_vfs_enumerate(&vfs, entries, vfs.file_count, copies, vfs.file_count, &stats, &error);
    if (status != ES2_OK) {
        printf("VFS-failed view=%s stage=enumerate error=%s offset=%zu\n", label, es2_status_string(status), error.offset);
        ++c->unexpected; free(entries); free(copies); return 0;
    }
    printf("VFS view=%s sources=%zu copies=%zu unique_paths=%zu duplicate_paths=%zu identical_duplicate_paths=%zu differing_duplicate_paths=%zu\n",
           label, source_count, stats.copy_count, stats.unique_count, stats.duplicate_count,
           stats.identical_duplicate_count, stats.duplicate_count - stats.identical_duplicate_count);
    for (i = 0; i < stats.unique_count; ++i) {
        const es2_vfs_entry *entry = &entries[i];
        /* Shared archive duplicates were all printed in the baseline view. */
        if (entry->copy_count > 1 && (baseline || copies[entry->first_copy].source_index == 0)) {
            printf("VFS-duplicate view=%s path=", label); print_vfs_path(entry->path);
            printf(" copies=%zu identical=%s winner=%s\n", entry->copy_count,
                   entry->all_identical ? "yes" : "no", names[copies[entry->first_copy].source_index]);
        }
    }
    if (baseline) {
        mismatches += expect_count("VFS VOL sources", source_count, 13);
        mismatches += expect_count("VFS VOL members", stats.copy_count, 3163);
        mismatches += expect_count("VFS documented duplicate paths", stats.duplicate_count, 567);
        mismatches += expect_count("VFS identical duplicate paths", stats.identical_duplicate_count, 557);
        mismatches += expect_count("VFS differing duplicate paths", stats.duplicate_count - stats.identical_duplicate_count, 10);
    }
    free(entries); free(copies);
    return mismatches;
}
static size_t report_vfs(corpus *c)
{
    retained_archive **archives;
    retained_archive *archive;
    retained_loose *loose;
    es2_vfs_source *sources;
    const char **names;
    es2_vfs_file *files;
    size_t i = 0, count = 0, loose_count = 0, mismatches = 0;
    size_t source_capacity;
    if (c->retained_archives == SIZE_MAX) { ++c->io_errors; return 0; }
    source_capacity = c->retained_archives + 1;
    if (source_capacity > SIZE_MAX / sizeof *sources || source_capacity > SIZE_MAX / sizeof *names
        || source_capacity > SIZE_MAX / sizeof *archives || c->loose_files > SIZE_MAX / sizeof *files) {
        ++c->io_errors; return 0;
    }
    archives = (retained_archive **)malloc(source_capacity * sizeof *archives);
    sources = (es2_vfs_source *)calloc(source_capacity, sizeof *sources);
    names = (const char **)calloc(source_capacity, sizeof *names);
    files = c->loose_files == 0 ? NULL : (es2_vfs_file *)malloc(c->loose_files * sizeof *files);
    if (archives == NULL || sources == NULL || names == NULL || (c->loose_files != 0 && files == NULL)) {
        free(archives); free(sources); free(names); free(files); ++c->io_errors; return 0;
    }
    for (archive = c->archive_memory; archive != NULL; archive = archive->next)
        if (vol_directory_path(archive->path)) archives[count++] = archive;
    qsort(archives, count, sizeof *archives, archive_compare);
    for (i = 0; i < count; ++i) {
        sources[i].kind = ES2_VFS_VOL; sources[i].data.vol = &archives[i]->archive;
        names[i] = archives[i]->path;
    }
    puts("VFS-policy baseline=VOL-only order=ASCII-path-sorted first-source-wins original-DBSIM=priority-descending header-priority=not-used-in-this-view");
    mismatches += report_vfs_view(c, "VOL-only", sources, names, count, 1);
    for (loose = c->loose_memory; loose != NULL; loose = loose->next) {
        files[loose_count].path = loose->path;
        files[loose_count].data = (es2_span){loose->bytes,loose->size};
        ++loose_count;
    }
    if (loose_count != 0) qsort(files, loose_count, sizeof *files, loose_compare);
    for (i = count; i != 0; --i) { sources[i] = sources[i - 1]; names[i] = names[i - 1]; }
    sources[0].kind = ES2_VFS_LOOSE; sources[0].data.loose.files = files;
    sources[0].data.loose.count = loose_count; names[0] = "loose-DATA";
    puts("VFS-policy combined=loose-source-first-then-sorted-VOL original-DBSIM-loose-first=static-confirmed");
    mismatches += report_vfs_view(c, "physical-DATA-paths", sources, names, count + 1, 0);
    for (i = 0; i < loose_count; ++i) {
        if (equal_ascii(files[i].path, "DATA/MAT0.DAT")) files[i].path = "dat/MAT0.DAT";
        else if (equal_ascii(files[i].path, "DATA/MFORMS.DAT")) files[i].path = "dat/MFORMS.DAT";
    }
    if (loose_count != 0) qsort(files, loose_count, sizeof *files, loose_compare);
    puts("VFS-policy aliases=DATA/MAT0.DAT->dat/MAT0.DAT,DATA/MFORMS.DAT->dat/MFORMS.DAT explicit-caller-mapping=yes original-mapping=unverified");
    mismatches += report_vfs_view(c, "two-explicit-dat-aliases", sources, names, count + 1, 0);
    /* Compare the two documented loose copies directly with their archive copies. */
    for (i = 0; i < loose_count; ++i) {
        size_t a;
        if (!equal_ascii(files[i].path, "dat/MAT0.DAT") && !equal_ascii(files[i].path, "dat/MFORMS.DAT")) continue;
        for (a = 0; a < count; ++a) {
            if (equal_ascii(archives[a]->path, "VOL/SIMVOL0.VOL")) {
                es2_vol_member member;
                es2_error error;
                es2_status status = es2_vol_find(&archives[a]->archive, files[i].path, &member, &error);
                int identical = status == ES2_OK && member.payload.size == files[i].data.size
                    && (files[i].data.size == 0 || memcmp(member.payload.data, files[i].data.data, files[i].data.size) == 0);
                printf("loose-archive-copy path=%s identical=%s\n", files[i].path, identical ? "yes" : "no");
                mismatches += expect_count(files[i].path, (uint64_t)identical, 1);
                break;
            }
        }
    }
    free(archives); free(sources); free(names); free(files);
    return mismatches;
}
static void release_corpus(corpus *c)
{
    while (c->archive_memory != NULL) {
        retained_archive *next = c->archive_memory->next;
        free(c->archive_memory->bytes); free(c->archive_memory->path); free(c->archive_memory);
        c->archive_memory = next;
    }
    while (c->loose_memory != NULL) {
        retained_loose *next = c->loose_memory->next;
        free(c->loose_memory->bytes); free(c->loose_memory->path); free(c->loose_memory);
        c->loose_memory = next;
    }
}
static size_t report_dat(corpus *c)
{
    /* File counts and byte totals are the complete inventory in dat.md. */
    static const uint64_t files[ES2_DAT_KIND_COUNT] = {
        [ES2_DAT_SIM_WEAPONS]=1, [ES2_DAT_VEHICLE]=21, [ES2_DAT_SKIMMER]=1,
        [ES2_DAT_DEBRIS]=21, [ES2_DAT_PROJ]=1, [ES2_DAT_ROCKETS]=1,
        [ES2_DAT_BULLETS]=1, [ES2_DAT_BEAM]=1, [ES2_DAT_LC_WPNS]=1,
        [ES2_DAT_HERC_INF]=1, [ES2_DAT_LOADOUT]=9, [ES2_DAT_HERCS]=1,
        [ES2_DAT_TRN_HERC]=1, [ES2_DAT_SHELL_WEAPONS]=1, [ES2_DAT_DAMAGE]=1,
        [ES2_DAT_ARM]=9, [ES2_DAT_RPR]=9, [ES2_DAT_ARM_WEAP]=1,
        [ES2_DAT_HOTSPOTS]=2, [ES2_DAT_MFORMS]=2, [ES2_DAT_FFORMS]=1,
        [ES2_DAT_BFORMS]=1, [ES2_DAT_BASES]=1, [ES2_DAT_BASECOL]=1,
        [ES2_DAT_EXPLOS]=2, [ES2_DAT_COLORS]=1, [ES2_DAT_FIRE]=1,
        [ES2_DAT_SHADES]=1, [ES2_DAT_MAT0]=2, [ES2_DAT_ZONE]=39,
        [ES2_DAT_CAREER]=1, [ES2_DAT_THEATER]=1, [ES2_DAT_MAP_INFO]=1,
        [ES2_DAT_MAP_LOCS]=1, [ES2_DAT_TEX]=1, [ES2_DAT_STYLES]=1
    };
    static const uint64_t bytes[ES2_DAT_KIND_COUNT] = {
        [ES2_DAT_SIM_WEAPONS]=3790, [ES2_DAT_VEHICLE]=4536, [ES2_DAT_SKIMMER]=46,
        [ES2_DAT_DEBRIS]=3336, [ES2_DAT_PROJ]=974, [ES2_DAT_ROCKETS]=72,
        [ES2_DAT_BULLETS]=170, [ES2_DAT_BEAM]=62, [ES2_DAT_LC_WPNS]=68,
        [ES2_DAT_HERC_INF]=146, [ES2_DAT_LOADOUT]=432, [ES2_DAT_HERCS]=148,
        [ES2_DAT_TRN_HERC]=432, [ES2_DAT_SHELL_WEAPONS]=707, [ES2_DAT_DAMAGE]=102,
        [ES2_DAT_ARM]=18158, [ES2_DAT_RPR]=12240, [ES2_DAT_ARM_WEAP]=364,
        [ES2_DAT_HOTSPOTS]=1900, [ES2_DAT_MFORMS]=284, [ES2_DAT_FFORMS]=92,
        [ES2_DAT_BFORMS]=3186, [ES2_DAT_BASES]=6422, [ES2_DAT_BASECOL]=4938,
        [ES2_DAT_EXPLOS]=1920, [ES2_DAT_COLORS]=54, [ES2_DAT_FIRE]=8,
        [ES2_DAT_SHADES]=56, [ES2_DAT_MAT0]=488, [ES2_DAT_ZONE]=624,
        [ES2_DAT_CAREER]=148, [ES2_DAT_THEATER]=58, [ES2_DAT_MAP_INFO]=148,
        [ES2_DAT_MAP_LOCS]=1108, [ES2_DAT_TEX]=64, [ES2_DAT_STYLES]=258
    };
    /* Zero denotes a family whose primary aggregate is not stated in the doc. */
    static const uint64_t records[ES2_DAT_KIND_COUNT] = {
        [ES2_DAT_SIM_WEAPONS]=33, [ES2_DAT_VEHICLE]=21, [ES2_DAT_SKIMMER]=1,
        [ES2_DAT_DEBRIS]=148, [ES2_DAT_PROJ]=27, [ES2_DAT_ROCKETS]=5,
        [ES2_DAT_BULLETS]=12, [ES2_DAT_BEAM]=10, [ES2_DAT_LC_WPNS]=3,
        [ES2_DAT_HERC_INF]=9, [ES2_DAT_LOADOUT]=9, [ES2_DAT_HERCS]=5,
        [ES2_DAT_TRN_HERC]=9, [ES2_DAT_SHELL_WEAPONS]=33, [ES2_DAT_DAMAGE]=1,
        [ES2_DAT_ARM_WEAP]=26, [ES2_DAT_HOTSPOTS]=18, [ES2_DAT_MFORMS]=10,
        [ES2_DAT_FFORMS]=5, [ES2_DAT_BFORMS]=17, [ES2_DAT_BASES]=65,
        [ES2_DAT_BASECOL]=65, [ES2_DAT_EXPLOS]=38, [ES2_DAT_COLORS]=1,
        [ES2_DAT_FIRE]=4, [ES2_DAT_SHADES]=5, [ES2_DAT_MAT0]=26,
        [ES2_DAT_ZONE]=39, [ES2_DAT_CAREER]=6, [ES2_DAT_THEATER]=4,
        [ES2_DAT_MAP_INFO]=5, [ES2_DAT_MAP_LOCS]=5, [ES2_DAT_TEX]=15,
        [ES2_DAT_STYLES]=4
    };
    size_t i, mismatches = 0;
    uint64_t failed = 0;
    for (i = 0; i < ES2_DAT_KIND_COUNT; ++i) {
        char label[128];
        const dat_count *count = &c->dat[i];
        if (i == ES2_DAT_UNKNOWN && count->files == 0) continue;
        printf("DAT structure=%s inputs=%" PRIu64 " ok=%" PRIu64 " failed=%" PRIu64
               " bytes=%" PRIu64 " primary_records=%" PRIu64 " secondary_records=%" PRIu64
               " child_records=%" PRIu64 " nested_lists=%" PRIu64 " nested_groups=%" PRIu64 "\n",
               es2_dat_kind_name((es2_dat_kind)i), count->files, count->ok, count->failed,
               count->bytes, count->records, count->secondary, count->children, count->lists, count->groups);
        (void)snprintf(label, sizeof label, "DAT %s inputs", es2_dat_kind_name((es2_dat_kind)i));
        mismatches += expect_count(label, count->files, files[i]);
        (void)snprintf(label, sizeof label, "DAT %s bytes", es2_dat_kind_name((es2_dat_kind)i));
        mismatches += expect_count(label, count->bytes, bytes[i]);
        if (records[i] != 0) {
            (void)snprintf(label, sizeof label, "DAT %s primary records", es2_dat_kind_name((es2_dat_kind)i));
            mismatches += expect_count(label, count->records, records[i]);
        }
        failed += count->failed;
    }
    printf("DAT total_inputs=%" PRIu64 " archive_inputs=%" PRIu64 " loose_inputs=%" PRIu64
           " bytes=%" PRIu64 " failed=%" PRIu64 "\n",
           c->dat_inputs, c->dat_archive_inputs, c->dat_loose_inputs, c->dat_bytes, failed);
    printf("DAT support reader_framing=%" PRIu64 " candidate_data_fit=%" PRIu64
           " external_count=%" PRIu64 " prefix_with_suffix=%" PRIu64
           " BASECOL_external_count=derived-from-BASES FIRE_external_count=caller-assumed-four\n",
           c->dat_support[0], c->dat_support[1], c->dat_support[2], c->dat_support[3]);
    printf("DAT exceptions opaque_suffix_files=%" PRIu64 " opaque_suffix_bytes=%" PRIu64
           " sparse_loadout_records=%" PRIu64 "\n",
           c->dat_suffix_files, c->dat_suffix_bytes, c->dat_slot_exceptions);
    printf("loose-DATA files=%zu bytes=%" PRIu64 "\n", c->loose_files, c->loose_bytes);
    mismatches += expect_count("DAT inputs", c->dat_inputs, 142);
    mismatches += expect_count("DAT archive inputs", c->dat_archive_inputs, 140);
    mismatches += expect_count("DAT loose inputs", c->dat_loose_inputs, 2);
    mismatches += expect_count("DAT bytes", c->dat_bytes, 67539);
    mismatches += expect_count("DAT failures", failed, 0);
    mismatches += expect_count("DAT reader-backed framing including BASECOL", c->dat_support[0] + c->dat[ES2_DAT_BASECOL].ok, 133);
    mismatches += expect_count("DAT candidate/data-only framing", c->dat_support[1], 6);
    mismatches += expect_count("DAT external count consumers", c->dat_support[2], 2);
    mismatches += expect_count("DAT prefix with suffix", c->dat_support[3], 2);
    mismatches += expect_count("DAT MAT0 suffix files", c->dat_suffix_files, 2);
    mismatches += expect_count("DAT MAT0 suffix bytes", c->dat_suffix_bytes, 272);
    mismatches += expect_count("DAT sparse loadout exceptions", c->dat_slot_exceptions, 4);
    mismatches += expect_count("DAT simulator WEAPONS nested A", c->dat_weapon_a, 30);
    mismatches += expect_count("DAT simulator WEAPONS nested B", c->dat_weapon_b, 211);
    mismatches += expect_count("DAT debris children", c->dat[ES2_DAT_DEBRIS].children, 193);
    mismatches += expect_count("DAT shell WEAPONS secondary", c->dat[ES2_DAT_SHELL_WEAPONS].secondary, 39);
    mismatches += expect_count("DAT DAMAGE coefficients", c->dat[ES2_DAT_DAMAGE].children, 33);
    mismatches += expect_count("DAT ARM_WEAP second list", c->dat[ES2_DAT_ARM_WEAP].secondary, 4);
    mismatches += expect_count("DAT hotspot rectangles", c->dat[ES2_DAT_HOTSPOTS].children, 114);
    mismatches += expect_count("DAT BFORMS elements", c->dat[ES2_DAT_BFORMS].children, 145);
    mismatches += expect_count("DAT BFORMS byte lists", c->dat[ES2_DAT_BFORMS].lists, 112);
    mismatches += expect_count("DAT BASES children", c->dat[ES2_DAT_BASES].children, 110);
    mismatches += expect_count("DAT BASECOL elements", c->dat[ES2_DAT_BASECOL].children, 543);
    mismatches += expect_count("DAT BASECOL shapes", c->dat[ES2_DAT_BASECOL].lists, 80);
    mismatches += expect_count("DAT BASECOL groups", c->dat[ES2_DAT_BASECOL].groups, 36);
    mismatches += expect_count("DAT EXPLOS parameters", c->dat[ES2_DAT_EXPLOS].secondary, 44);
    mismatches += expect_count("DAT SHADES pairs", c->dat[ES2_DAT_SHADES].children, 22);
    mismatches += expect_count("DAT CAREER indices", c->dat[ES2_DAT_CAREER].children, 61);
    mismatches += expect_count("loose DATA files", c->loose_files, 7);
    mismatches += expect_count("loose DATA bytes", c->loose_bytes, 1775);
    return mismatches;
}
static int report(corpus *c)
{
    static const size_t expected_ok[] = {61,55,65,10,252,51};
    static const size_t expected_failed[] = {1,0,0,0,1,0};
    /* Counts from the class table in docs/formats/dts.md, in public tag order. */
    static const uint64_t expected_tags[ES2_DTS_TAG_COUNT] = {
        1947,1517,31782,0,400,327,765,0,2158,92,0,3806,0,0,0,624,8379,433,147,152,152,285
    };
    size_t i, mismatches = 0;
    qsort(c->extensions, c->extension_count, sizeof c->extensions[0], extension_compare);
    for (i = 0; i < c->extension_count; ++i)
        printf("extension=%s members=%zu\n", c->extensions[i].name[0] ? c->extensions[i].name : "(none)", c->extensions[i].count);
    for (i = 0; i < FORMAT_COUNT; ++i) {
        printf("format=%s ok=%zu failed=%zu\n", c->formats[i].name, c->formats[i].ok, c->formats[i].failed);
        mismatches += expect_count(c->formats[i].name, c->formats[i].ok, expected_ok[i]);
        mismatches += expect_count("format_failure_count", c->formats[i].failed, expected_failed[i]);
    }
    printf("DTS roots=%" PRIu64 " objects=%" PRIu64 " max_depth=%zu\n", c->roots, c->objects, c->max_depth);
    for (i = 0; i < ES2_DTS_TAG_COUNT; ++i) {
        printf("DTS tag=%08" PRIx32 " class=%s objects=%" PRIu64 "\n", es2_dts_tag_at(i), es2_dts_tag_name(es2_dts_tag_at(i)), c->tags[i]);
        mismatches += expect_count(es2_dts_tag_name(es2_dts_tag_at(i)), c->tags[i], expected_tags[i]);
    }
    for (i = 0; i < sizeof reference_archives / sizeof reference_archives[0]; ++i)
        mismatches += expect_count(reference_archives[i].path, c->seen_archives[i], 1);
    mismatches += expect_count("VOLN archives", c->archives, 16);
    mismatches += expect_count("members", c->members, 3802);
    mismatches += expect_count("other VOL files", c->skipped, 8);
    mismatches += expect_count("archive bytes", c->archive_bytes, 83746191);
    mismatches += expect_count("payload bytes", c->payload_bytes, 83638861);
    mismatches += expect_count("DTS roots", c->roots, 479);
    mismatches += expect_count("DTS objects", c->objects, 52966);
    mismatches += expect_count("DTS depth", c->max_depth, 5);
    mismatches += expect_count("palette colours", c->colours, 16640);
    mismatches += expect_count("palette ramps", c->ramps, 9472);
    printf("images declared=%" PRIu64 " pixel_bytes=%" PRIu64 "\n", c->images, c->pixels);
    mismatches += expect_count("declared images", c->images, 3453);
    mismatches += expect_count("declared pixel bytes", c->pixels, 25699737);
    mismatches += expect_count("documented parse failures", c->known, 2);
    mismatches += report_dat(c);
    mismatches += report_vfs(c);
    {
        const es2_vol **archives;
        const char **names;
        retained_archive *a;
        size_t n = 0;
        if (c->retained_archives > SIZE_MAX / sizeof *archives ||
            c->retained_archives > SIZE_MAX / sizeof *names) {
            ++c->io_errors;
        } else {
            archives = (const es2_vol **)malloc(c->retained_archives * sizeof *archives);
            names = (const char **)malloc(c->retained_archives * sizeof *names);
            if (c->retained_archives != 0 && (archives == NULL || names == NULL)) ++c->io_errors;
            else {
                for (a = c->archive_memory; a != NULL; a = a->next) {
                    archives[n] = &a->archive; names[n] = a->path; ++n;
                }
                mismatches += es2fmt_collision_report(archives, names, n);
            }
            free(archives); free(names);
        }
    }
    printf("total archives=%zu members=%zu other_vol_files=%zu documented_failures=%zu unexpected=%zu io_errors=%zu malformed_archives=%zu mismatches=%zu\n",
        c->archives,c->members,c->skipped,c->known,c->unexpected,c->io_errors,c->malformed_archives,mismatches);
    return mismatches != 0 || c->unexpected != 0 || c->io_errors != 0 || c->malformed_archives != 0;
}
int main(int argc, char **argv)
{
    const char *root = getenv("ES2_DATA_ROOT");
    corpus c = {0};
    size_t i;
    int result;
    static const char *const names[] = {"MSN","DTS","DPL","DBM","DBA","HBA"};
    if (argc > 1) {
        if (strcmp(argv[1], "--help") == 0 && argc == 2) {
            puts("es2fmt [--real [CD_ROOT]]\nNo arguments: use ES2_DATA_ROOT, or skip when unset.\n--real: opt in, using ES2_DATA_ROOT or build\\extracted\\cd.\nStatistics only; exits nonzero for unexpected failures or reference-count mismatches."); return 0;
        }
        if (strcmp(argv[1], "--real") != 0 || argc > 3) { fprintf(stderr,"usage: es2fmt [--real [CD_ROOT]]\n"); return 2; }
        if (argc == 3) root = argv[2];
        else if (root == NULL || root[0] == '\0') root = "build\\extracted\\cd";
    } else if (root == NULL || root[0] == '\0') {
        puts("SKIP real data: set ES2_DATA_ROOT or pass --real explicitly."); return 0;
    }
    if (root == NULL || root[0] == '\0') { fprintf(stderr,"empty data root\n"); return 2; }
    for (i = 0; i < FORMAT_COUNT; ++i) c.formats[i].name = names[i];
    walk_directory(&c, root, "", 0);
    result = report(&c);
    release_corpus(&c);
    return result;
}
