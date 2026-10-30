#include <stdint.h>
#include <stdio.h>

/* Keep the complete witness on stderr even when stdout is truncated. */
static int coverage_missing(const char *name,uint32_t address,uint64_t fall,uint64_t taken) {
    if(fall && taken) return 0;
    fprintf(stderr,"FAIL: uncovered original branch function=%s VA=%08x missing=%s%s%s fall=%llu taken=%llu\n",
        name,address,fall?"":"fallthrough",(!fall&&!taken)?",":"",taken?"":"taken",
        (unsigned long long)fall,(unsigned long long)taken);
    fflush(stderr);
    return 1;
}
