/* Bazel test sharding for the sim-suite binaries. A suite splits its parts
 * (independent case batches after its own set-up) over the shards: shard i runs
 * the parts p with p % count == i; every shard repeats the set-up. An unsharded
 * run owns every part. */
#include <errno.h>

static unsigned shard_count=1,shard_index;
static int suite_part_in_shard(unsigned part) { return part%shard_count==shard_index; }
static int configure_sharding(void) {
    const char *total=getenv("TEST_TOTAL_SHARDS"),*index=getenv("TEST_SHARD_INDEX");
    if(total || index) {
        char *end; unsigned long n,i;
        if(!total || !index || !*total || !*index) goto invalid;
        errno=0; n=strtoul(total,&end,10); if(errno || *end || n<1 || n>64) goto invalid;
        errno=0; i=strtoul(index,&end,10); if(errno || *end || i>=n) goto invalid;
        shard_count=(unsigned)n; shard_index=(unsigned)i;
    }
    const char *status=getenv("TEST_SHARD_STATUS_FILE");
    if(status && *status) {
        FILE *file=fopen(status,"wb");
        if(!file || fclose(file)) { fputs("FAIL: cannot acknowledge Bazel sharding\n",stderr); return 1; }
    }
    return 0;
invalid:
    fputs("FAIL: invalid Bazel shard count/index\n",stderr); return 1;
}
