#include "es2/core/divide.h"
#include <stdio.h>
#define CHECK(c) do { ++checks; if(!(c)) { fprintf(stderr,"divide line %d: %s\n",__LINE__,#c); return 1; } } while(0)
static unsigned checks;
static uint64_t sext(uint32_t x) { return (uint64_t)(int64_t)(int32_t)x; }
/* The expected code, quotient and remainder of one pair. */
static int is(uint64_t n,uint32_t d,int is_signed,uint32_t code,uint32_t q,uint32_t r) {
    uint32_t gq=1,gr=1;
    return es2_divide(n,d,is_signed,&gq,&gr)==code && gq==q && gr==r;
}
int main(void) {
    /* A zero divisor whatever the dividend. */
    CHECK(is(5,0,1,ES2_DIVIDE_BY_ZERO,0,0)); CHECK(is(5,0,0,ES2_DIVIDE_BY_ZERO,0,0));
    CHECK(is(sext(0x80000000u),0,1,ES2_DIVIDE_BY_ZERO,0,0)); CHECK(is(UINT64_MAX,0,0,ES2_DIVIDE_BY_ZERO,0,0));
    /* IDIV truncates toward zero; the remainder has the dividend's sign. */
    CHECK(is(7,2,1,0,3,1)); CHECK(is(sext(0xfffffff9u),2,1,0,0xfffffffdu,0xffffffffu));
    CHECK(is(7,0xfffffffeu,1,0,0xfffffffdu,1)); CHECK(is(sext(0xfffffff9u),0xfffffffeu,1,0,3,0xffffffffu));
    /* INT_MIN / -1 and INT_MIN % -1 fault (wasm gives 0 for the remainder; x86 faults). */
    CHECK(is(sext(0x80000000u),0xffffffffu,1,ES2_DIVIDE_OVERFLOW,0,0));
    CHECK(is(sext(0x80000000u),1,1,0,0x80000000u,0));
    CHECK(is(0x80000000u,0xffffffffu,1,0,0x80000000u,0));           /* +2^31 / -1 = -2^31 fits */
    CHECK(is(0x80000000u,1,1,ES2_DIVIDE_OVERFLOW,0,0));             /* +2^31 does not */
    CHECK(is(sext(0x7fffffffu),0xffffffffu,1,0,0x80000001u,0));
    CHECK(is(0xffffffff00000000ull,2,1,0,0x80000000u,0));           /* -2^32 / 2 = -2^31 fits */
    CHECK(is(0xfffffffefffffffeull,2,1,ES2_DIVIDE_OVERFLOW,0,0));   /* -2^31 - 1 */
    CHECK(is(0x00000000fffffffeull,2,1,0,0x7fffffffu,0)); CHECK(is(0x0000000100000000ull,2,1,ES2_DIVIDE_OVERFLOW,0,0));
    CHECK(is(0x8000000000000000ull,0xffffffffu,1,ES2_DIVIDE_OVERFLOW,0,0));
    CHECK(is(0x8000000000000000ull,1,1,ES2_DIVIDE_OVERFLOW,0,0));
    CHECK(is((uint64_t)1<<40,2,1,ES2_DIVIDE_OVERFLOW,0,0));
    CHECK(is(sext(0x80000000u),0x80000000u,1,0,1,0)); CHECK(is(sext(0x80000001u),0x80000000u,1,0,0,0x80000001u));
    /* DIV: the quotient fits while the high dword is below the divisor. */
    CHECK(is(0xfffffffeffffffffull,0xffffffffu,0,0,0xffffffffu,0xfffffffeu));
    CHECK(is(0xffffffff00000000ull,0xffffffffu,0,ES2_DIVIDE_OVERFLOW,0,0));
    CHECK(is((uint64_t)59u<<32,60,0,0,0xfbbbbbbbu,44)); CHECK(is((uint64_t)60u<<32,60,0,ES2_DIVIDE_OVERFLOW,0,0));
    CHECK(is(0xffffffffu,1,0,0,0xffffffffu,0)); CHECK(is((uint64_t)1<<32,1,0,ES2_DIVIDE_OVERFLOW,0,0));
    CHECK(is(sext(0x80000000u),0xffffffffu,0,ES2_DIVIDE_OVERFLOW,0,0)); CHECK(is(0x80000000u,0xffffffffu,0,0,0,0x80000000u));
    /* Either output may be NULL; es2_divide_or_raise without a fault. */
    CHECK(es2_divide(100,7,1,NULL,NULL)==0);
    uint32_t r=0; CHECK(es2_divide_or_raise(100,7,1,&r)==14 && r==2);
    CHECK(es2_divide_or_raise(sext(0xffffff9cu),7,1,NULL)==0xfffffff2u);
    es2_divide_raise(0); es2_divide_raise(0xc0000005u);   /* no divide code: returns */
    printf("PASS: divide %u checks\n",checks); return 0;
}
