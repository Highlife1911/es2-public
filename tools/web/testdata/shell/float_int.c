/* Float to integer conversions, listed under shell/ and launcher/ as informational hits. */
#include <math.h>
int scale(float f) { return (int)(f * 2.0f); }                     /* EXPECT: float-int */
int to_pixel(double d) { return (int)d; }                          /* EXPECT: float-int */
int rounded(int v) { return (int)floor(v / 3.0); }                 /* EXPECT: float-int */
unsigned from_literal(void) { return (unsigned)1.5; }              /* EXPECT: float-int */
int widen(short s) { return (int)s; }
unsigned mask(unsigned u) { return (unsigned)(u & 0xff); }
int dialog(int n) { return (int)dialog_count(n); }
float to_float(int v) { return (float)v; }
