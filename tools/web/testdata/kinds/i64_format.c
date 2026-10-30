/* MinGW-specific %I64 formats */
#include <stdio.h>
void show(unsigned long long v) {
    printf("%I64u\n", v);                                          /* EXPECT: i64-format */
    printf("%08I64x %d\n", v, 1);                                  /* EXPECT: i64-format */
    printf("%llu\n", v);
    printf("100%%\n");
    printf("%d items\n", 1);
}
/* "%I64u" in a comment */
