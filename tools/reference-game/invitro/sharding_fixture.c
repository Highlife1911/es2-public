#include <stdio.h>
#include <string.h>
#define FUNCTION_HOOK_COUNT 51
#include "sharding.h"

int main(void) {
    unsigned count,index,seen[FUNCTION_HOOK_COUNT]={0};
    if(parse_sharding(NULL,NULL,&count,&index) || count!=1 || index!=0) return 1;
    const char *bad[][2]={{NULL,"0"},{"4",NULL},{"","0"},{"4",""},{"0","0"},
        {"52","0"},{"4","4"},{"4","-1"},{"-1","0"},{"4x","0"},{"4","x"},
        {"9999999999999999999999999999999","0"}};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i)
        if(!parse_sharding(bad[i][0],bad[i][1],&count,&index)) return 2;
    for(unsigned i=0;i<4;++i) {
        char number[2]={(char)('0'+i),0};
        if(parse_sharding("4",number,&shard_count,&shard_index) || shard_count!=4 || shard_index!=i) return 3;
        for(unsigned function=0;function<FUNCTION_HOOK_COUNT;++function)
            if(function_in_shard(function)) ++seen[function];
    }
    for(unsigned i=0;i<FUNCTION_HOOK_COUNT;++i) if(seen[i]!=1) return 4;
    /* Group selection: 51 functions in three groups (a: index%3==0, 17
     * members; b: index%3==1, 17; c: 17). Two shards of group b own each
     * member exactly once, no member of a or c, and an unknown group is empty. */
    const char *names[FUNCTION_HOOK_COUNT];
    for(unsigned i=0;i<FUNCTION_HOOK_COUNT;++i) names[i]=i%3==0?"a":i%3==1?"b":"c";
    if(select_group(names,"b")!=17 || select_group(names,"none")!=0) return 5;
    memset(seen,0,sizeof(seen)); select_group(names,"b");
    for(unsigned i=0;i<2;++i) {
        char number[2]={(char)('0'+i),0};
        if(parse_sharding("2",number,&shard_count,&shard_index)) return 6;
        for(unsigned function=0;function<FUNCTION_HOOK_COUNT;++function) if(function_in_shard(function)) ++seen[function];
    }
    for(unsigned i=0;i<FUNCTION_HOOK_COUNT;++i) if(seen[i]!=(i%3==1)) return 7;
    if(shard_ranks[1]!=0 || shard_ranks[4]!=1 || shard_ranks[49]!=16 || shard_ranks[0]!=0) return 8;
    puts("PASS: default/invalid shard configuration, exactly one owner for all 51 functions, group ranks and selection");
    return 0;
}
