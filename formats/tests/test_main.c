#include "test_support.h"
#include <stdio.h>
static size_t checks;
static size_t failures;
int test_check(int passed, const char *expression, const char *file, int line)
{
    ++checks;
    if (!passed) { ++failures; fprintf(stderr, "%s:%d: %s\n", file, line, expression); }
    return passed;
}
int test_common(void);
int test_vol(void);
int test_images(void);
int test_msn(void);
int test_dts(void);
int test_dat(void);
int test_vfs(void);
int test_collision(void);
int main(void)
{
    int failed = 0;
    failed |= test_common(); failed |= test_vol(); failed |= test_images();
    failed |= test_msn(); failed |= test_dts();
    failed |= test_dat(); failed |= test_vfs();
    failed |= test_collision();
    printf("synthetic: %zu checks, %zu failures\n", checks, failures);
    return failed != 0 || failures != 0;
}
