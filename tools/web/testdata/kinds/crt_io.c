/* CRT file and clock calls are informational: listed, never forbidden. */
#include <stdio.h>
#include <time.h>
void *open_file(void) { return fopen("x", "rb"); }                 /* EXPECT: crt-io */
long now(void) { return time(0); }                                 /* EXPECT: crt-io */
void *wrapped(void) { return my_fopen("x"); }
int time_value;
