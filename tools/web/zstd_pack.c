/* zstd_pack: the site builder's compressor (docs/web_build.md sections 8.2 and 17).
 *
 *   zstd_pack -c <level> <jobs>      compress every job of the list
 *   zstd_pack -d <input> <output>    decompress one frame (for the tests)
 *
 * A job line is "<offset>\t<length>\t<input>\t<output>\n": that byte range of the input
 * becomes one zstd frame in the output. The frames are deterministic: a fixed level, one
 * thread, the content size in the header, no checksum and no dictionary ID. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zstd.h"

static unsigned char *read_range(const char *path, unsigned long long offset, size_t length)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    unsigned char *data = malloc(length ? length : 1);
    int ok = data && offset <= 0x7fffffffu && fseek(f, (long)offset, SEEK_SET) == 0 &&
             fread(data, 1, length, f) == length;
    fclose(f);
    if (!ok) {
        free(data);
        return NULL;
    }
    return data;
}

static int write_file(const char *path, const void *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int ok = fwrite(data, 1, size, f) == size;
    return fclose(f) == 0 && ok;
}

static int compress_jobs(int level, const char *jobs_path)
{
    FILE *jobs = fopen(jobs_path, "rb");
    if (!jobs) {
        fprintf(stderr, "zstd_pack: cannot open %s\n", jobs_path);
        return 2;
    }
    ZSTD_CCtx *cctx = ZSTD_createCCtx();
    if (!cctx || ZSTD_isError(ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, level)) ||
        ZSTD_isError(ZSTD_CCtx_setParameter(cctx, ZSTD_c_contentSizeFlag, 1)) ||
        ZSTD_isError(ZSTD_CCtx_setParameter(cctx, ZSTD_c_checksumFlag, 0)) ||
        ZSTD_isError(ZSTD_CCtx_setParameter(cctx, ZSTD_c_dictIDFlag, 0))) {
        fprintf(stderr, "zstd_pack: bad level %d\n", level);
        fclose(jobs);
        ZSTD_freeCCtx(cctx);
        return 2;
    }
    static char line[4 * 1024];
    int status = 0;
    unsigned long count = 0;
    while (fgets(line, sizeof line, jobs)) {
        size_t n = strlen(line);
        if (n && line[n - 1] == '\n') line[--n] = 0;
        if (n && line[n - 1] == '\r') line[--n] = 0;
        if (!n) continue;
        char *fields[4];
        char *p = line;
        int k = 0;
        for (; k < 4 && p; k++) {
            fields[k] = p;
            p = strchr(p, '\t');
            if (p) *p++ = 0;
        }
        if (k != 4 || p) {
            fprintf(stderr, "zstd_pack: bad job line %lu\n", count + 1);
            status = 2;
            break;
        }
        char *end;
        unsigned long long offset = strtoull(fields[0], &end, 10);
        unsigned long long length = *end ? 0 : strtoull(fields[1], &end, 10);
        if (*end || length > 0x7fffffffu) {
            fprintf(stderr, "zstd_pack: bad range in job line %lu\n", count + 1);
            status = 2;
            break;
        }
        unsigned char *src = read_range(fields[2], offset, (size_t)length);
        if (!src) {
            fprintf(stderr, "zstd_pack: cannot read %s at %llu+%llu\n", fields[2], offset, length);
            status = 1;
            break;
        }
        size_t bound = ZSTD_compressBound((size_t)length);
        unsigned char *dst = malloc(bound);
        size_t size = dst ? ZSTD_compress2(cctx, dst, bound, src, (size_t)length) : 0;
        if (!dst || ZSTD_isError(size) || !write_file(fields[3], dst, size)) {
            fprintf(stderr, "zstd_pack: cannot compress %s into %s\n", fields[2], fields[3]);
            status = 1;
        }
        free(dst);
        free(src);
        if (status) break;
        count++;
    }
    fclose(jobs);
    ZSTD_freeCCtx(cctx);
    if (!status) printf("zstd_pack: %lu parts at level %d\n", count, level);
    return status;
}

static int decompress_file(const char *input, const char *output)
{
    FILE *f = fopen(input, "rb");
    if (!f || fseek(f, 0, SEEK_END) != 0) {
        if (f) fclose(f);
        fprintf(stderr, "zstd_pack: cannot open %s\n", input);
        return 2;
    }
    long size = ftell(f);
    fclose(f);
    unsigned char *src = size >= 0 ? read_range(input, 0, (size_t)size) : NULL;
    unsigned long long content = src ? ZSTD_getFrameContentSize(src, (size_t)size) : ZSTD_CONTENTSIZE_ERROR;
    if (content == ZSTD_CONTENTSIZE_ERROR || content == ZSTD_CONTENTSIZE_UNKNOWN || content > 0x7fffffffu) {
        fprintf(stderr, "zstd_pack: %s is no frame with a content size\n", input);
        free(src);
        return 1;
    }
    unsigned char *dst = malloc(content ? (size_t)content : 1);
    size_t got = dst ? ZSTD_decompress(dst, (size_t)content, src, (size_t)size) : 0;
    int ok = dst && !ZSTD_isError(got) && got == content && write_file(output, dst, (size_t)content);
    free(dst);
    free(src);
    if (!ok) fprintf(stderr, "zstd_pack: cannot decompress %s\n", input);
    return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc == 4 && strcmp(argv[1], "-c") == 0) {
        char *end;
        long level = strtol(argv[2], &end, 10);
        if (*end || level < 1 || level > ZSTD_maxCLevel()) {
            fprintf(stderr, "zstd_pack: bad level %s\n", argv[2]);
            return 2;
        }
        return compress_jobs((int)level, argv[3]);
    }
    if (argc == 4 && strcmp(argv[1], "-d") == 0) return decompress_file(argv[2], argv[3]);
    fprintf(stderr, "usage: zstd_pack -c <level> <jobs> | zstd_pack -d <input> <output>\n");
    return 2;
}
