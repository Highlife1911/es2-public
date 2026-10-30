#include <stdlib.h>
#include <string.h>
#include <errno.h>

static unsigned shard_count=1,shard_index;
/* Group selection: shard_group_names[function] names each function's hook
 * group; with a selected group only its members are owned, ranked in
 * registry order so that Bazel shards split the group. Without a selection
 * the rank is the registry index (ownership=registry-index-modulo). */
static const char *const *shard_group_names;
static const char *shard_group;
static unsigned shard_ranks[FUNCTION_HOOK_COUNT];
static int parse_sharding(const char *total,const char *index,unsigned *count,unsigned *which) {
    *count=1; *which=0;
    if(!total && !index) return 0;
    if(!total || !index || !*total || !*index) return 1;
    char *end; errno=0; unsigned long n=strtoul(total,&end,10);
    if(errno || *end || n<1 || n>FUNCTION_HOOK_COUNT) return 1;
    errno=0; unsigned long i=strtoul(index,&end,10);
    if(errno || *end || i>=n) return 1;
    *count=(unsigned)n; *which=(unsigned)i; return 0;
}
static int function_in_shard(unsigned function) {
    if(shard_group && strcmp(shard_group_names[function],shard_group)) return 0;
    return (shard_group?shard_ranks[function]:function)%shard_count==shard_index;
}
/* Select one group; returns its member count (0: unknown group). */
static unsigned select_group(const char *const *names,const char *group) {
    unsigned rank=0; shard_group_names=names; shard_group=group;
    for(unsigned i=0;i<FUNCTION_HOOK_COUNT;++i) shard_ranks[i]=strcmp(names[i],group)?0:rank++;
    return rank;
}
static int configure_sharding(void) {
    if(parse_sharding(getenv("TEST_TOTAL_SHARDS"),getenv("TEST_SHARD_INDEX"),&shard_count,&shard_index)) {
        fputs("FAIL: invalid Bazel shard count/index\n",stderr); return 1;
    }
    const char *status=getenv("TEST_SHARD_STATUS_FILE");
    if(status && *status) {
        FILE *file=fopen(status,"wb");
        if(!file) { fputs("FAIL: cannot acknowledge Bazel sharding\n",stderr); return 1; }
        if(fclose(file)) return 1;
    }
    return 0;
}
