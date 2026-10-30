#include <stdlib.h>
#include <string.h>
#include "coverage_diagnostics.h"

int main(void) {
    const char *directory=getenv("TEST_TMPDIR"); char path[4096],text[2048];
    if(!directory) directory=".";
    if(snprintf(path,sizeof(path),"%s/coverage-errors.txt",directory)>=(int)sizeof(path)) return 1;
    if(!freopen(path,"w+",stderr)) return 2;
    if(coverage_missing("complete",0x1000,1,1)) return 3;
    if(!coverage_missing("track_forward",0x1234,0,7) ||
       !coverage_missing("track_inverse",0x5678,9,0) ||
       !coverage_missing("both_missing",0x9abc,0,0)) return 4;
    rewind(stderr); size_t length=fread(text,1,sizeof(text)-1,stderr); text[length]=0;
    const char *expected=
        "FAIL: uncovered original branch function=track_forward VA=00001234 missing=fallthrough fall=0 taken=7\n"
        "FAIL: uncovered original branch function=track_inverse VA=00005678 missing=taken fall=9 taken=0\n"
        "FAIL: uncovered original branch function=both_missing VA=00009abc missing=fallthrough,taken fall=0 taken=0\n";
    if(strcmp(text,expected)) { puts("FAIL: coverage stderr witness"); return 5; }
    puts("PASS: complete stderr witnesses for fallthrough, taken and both missing; covered site is silent");
    return 0;
}
