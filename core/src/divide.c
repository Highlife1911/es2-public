#include "es2/core/divide.h"

uint32_t es2_divide(uint64_t dividend, uint32_t divisor, int is_signed, uint32_t *quotient, uint32_t *remainder) {
    uint32_t code = 0, q = 0, r = 0;
    if (divisor == 0) code = ES2_DIVIDE_BY_ZERO;
    else if (is_signed) {
        const int64_t n = dividend >> 63 ? -(int64_t)~dividend - 1 : (int64_t)dividend,
                      d = divisor >> 31 ? (int64_t)divisor - INT64_C(4294967296) : (int64_t)divisor;
        /* -2^63 / -1 is outside int64 as well; any other -1 leaves no remainder. */
        if (d == -1 && n == INT64_MIN) code = ES2_DIVIDE_OVERFLOW;
        else {
            const int64_t sq = d == -1 ? -n : n / d, sr = d == -1 ? 0 : n % d;
            if (sq < INT32_MIN || sq > INT32_MAX) code = ES2_DIVIDE_OVERFLOW;
            else { q = (uint32_t)sq; r = (uint32_t)sr; }
        }
    } else {
        const uint64_t uq = dividend / divisor;
        if (uq > UINT32_MAX) code = ES2_DIVIDE_OVERFLOW;
        else { q = (uint32_t)uq; r = (uint32_t)(dividend % divisor); }
    }
    if (quotient) *quotient = q;
    if (remainder) *remainder = r;
    return code;
}

void es2_divide_raise(uint32_t code) {
    volatile int32_t n = INT32_MIN, d;
    if (code == ES2_DIVIDE_BY_ZERO) d = 0;
    else if (code == ES2_DIVIDE_OVERFLOW) d = -1;
    else return;
    n = n / d;
}

uint32_t es2_divide_or_raise(uint64_t dividend, uint32_t divisor, int is_signed, uint32_t *remainder) {
    uint32_t q;
    const uint32_t code = es2_divide(dividend, divisor, is_signed, &q, remainder);
    if (code) es2_divide_raise(code);
    return q;
}
