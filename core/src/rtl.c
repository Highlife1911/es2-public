/* Own code for the 20 pure CW3220.DLL imports of DBSIM.EXE.
 * Contracts: docs/reference-game/rtl_imports.md, section "the pure imports as
 * own code". Each function follows the CW3220 instruction sequence where it
 * matters for the result: the order of reads and writes (overlapping copies),
 * the bytes read (nothing beyond what the original reads), and the final
 * EAX/ECX/EDX. */
#include "es2/core/rtl.h"

#include <stddef.h>

typedef const es2_rtl_memory *mem;

static uint8_t rd(mem m, uint32_t address) { return m->read8(m->context, address); }
static void wr(mem m, uint32_t address, uint8_t value) { m->write8(m->context, address, value); }
static uint32_t rd32(mem m, uint32_t address)
{
    return (uint32_t)rd(m, address) | (uint32_t)rd(m, address + 1u) << 8 |
           (uint32_t)rd(m, address + 2u) << 16 | (uint32_t)rd(m, address + 3u) << 24;
}
static void wr32(mem m, uint32_t address, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) wr(m, address + i, (uint8_t)(value >> (8 * i)));
}
static uint32_t arg(mem m, uint32_t args, unsigned index) { return rd32(m, args + 4u * index); }
static int32_t sbyte(uint8_t value) { return value < 0x80u ? (int32_t)value : (int32_t)value - 256; }

/* `repne scasb`: compares AL with [EDI], EDI += 1, ECX -= 1 while ECX != 0
 * and the bytes differ. Returns 1 when it stopped on an equal byte. */
static int scan(mem m, uint32_t *edi, uint32_t *ecx, uint8_t al)
{
    while (*ecx) {
        uint8_t byte = rd(m, *edi);
        *edi += 1u;
        *ecx -= 1u;
        if (byte == al) return 1;
    }
    return 0;
}
/* strlen through `repne scasb` with ECX = -1: the count including the NUL. */
static uint32_t scan_length(mem m, uint32_t s)
{
    uint32_t edi = s, ecx = UINT32_MAX;
    scan(m, &edi, &ecx, 0);
    return ~ecx;
}
/* `rep movsd` then `rep movsb`, forward: each dword is read whole before it
 * is written (this decides the result of an overlapping copy). */
static void copy_forward(mem m, uint32_t *edi, uint32_t *esi, uint32_t count)
{
    for (uint32_t k = count >> 2; k; --k) {
        uint8_t t[4];
        for (unsigned i = 0; i < 4; ++i) t[i] = rd(m, *esi + i);
        for (unsigned i = 0; i < 4; ++i) wr(m, *edi + i, t[i]);
        *esi += 4u;
        *edi += 4u;
    }
    for (uint32_t k = count & 3u; k; --k) {
        wr(m, *edi, rd(m, *esi));
        *esi += 1u;
        *edi += 1u;
    }
}

/* CW3220 ctype classes as far as these functions use them: flag 8 (lower
 * case) only for 'a'..'z', flag 4 (upper case) only for 'A'..'Z'; bytes
 * 0x80..0xff carry no flag. toupper/tolower take a sign-extended byte, so
 * the byte 0xff arrives as -1 (EOF) and comes back as -1. */
int32_t es2_rtl_toupper(uint8_t byte)
{
    if (byte == 0xffu) return -1;
    return byte >= 'a' && byte <= 'z' ? (int32_t)byte - 0x20 : (int32_t)byte;
}
int32_t es2_rtl_tolower(uint8_t byte)
{
    if (byte == 0xffu) return -1;
    return byte >= 'A' && byte <= 'Z' ? (int32_t)byte + 0x20 : (int32_t)byte;
}
const char *es2_rtl_leaf_residue(es2_rtl_function function)
{
    switch (function) {
    case ES2_RTL_MEMSET: case ES2_RTL_STRLEN: case ES2_RTL_STRCHR: return "pd";
    case ES2_RTL_MEMCPY: case ES2_RTL_MEMMOVE: case ES2_RTL_STRCAT: case ES2_RTL_STRNCAT: case ES2_RTL_STRNCMP: return "psd";
    case ES2_RTL_MEMCMP: return "pb";
    default: return 0;
    }
}

/* atol's whitespace test indexes the ctype table with the sign-extended
 * byte: 0x09..0x0d and 0x20 have the space flag; a byte >= 0x80 lands in
 * the 256-byte identity table before ctype[-1], whose entry is the byte + 1,
 * so every even byte 0x80..0xfe tests as space (0xff hits ctype[-1] = 0). */
static int atol_space(uint8_t byte)
{
    if (byte < 0x80u) return (byte >= 0x09u && byte <= 0x0du) || byte == 0x20u;
    return byte != 0xffu && !(byte & 1u);
}

static void rtl_memset(mem m, uint32_t args, es2_rtl_regs *r)
{
    uint32_t d = arg(m, args, 0), n = arg(m, args, 2);
    uint8_t fill = (uint8_t)arg(m, args, 1);
    for (uint32_t i = 0; i < n; ++i) wr(m, d + i, fill);   /* dwords then bytes: ascending */
    r->eax = d; r->ecx = 0; r->edx = n;
}

static void rtl_memcpy(mem m, uint32_t args, es2_rtl_regs *r)
{
    uint32_t d = arg(m, args, 0), s = arg(m, args, 1), n = arg(m, args, 2);
    uint32_t edi = d, esi = s;
    copy_forward(m, &edi, &esi, n);
    r->eax = d; r->ecx = 0; r->edx = n;
}

static void rtl_memmove(mem m, uint32_t args, es2_rtl_regs *r)
{
    uint32_t d = arg(m, args, 0), s = arg(m, args, 1), n = arg(m, args, 2);
    r->ecx = 0;
    if (d < s) {
        uint32_t edi = d, esi = s;
        copy_forward(m, &edi, &esi, n);
    } else if (d == s) {
        r->ecx = n;                       /* nothing copied, ECX keeps the count */
    } else {
        /* Backward: the n mod 4 trailing bytes first, then the dwords. */
        uint32_t esi = s + n - 1u, edi = d + n - 1u;
        for (uint32_t k = n & 3u; k; --k) {
            wr(m, edi, rd(m, esi));
            esi -= 1u;
            edi -= 1u;
        }
        esi -= 3u;
        edi -= 3u;
        for (uint32_t k = n >> 2; k; --k) {
            uint8_t t[4];
            for (unsigned i = 0; i < 4; ++i) t[i] = rd(m, esi + i);
            for (unsigned i = 0; i < 4; ++i) wr(m, edi + i, t[i]);
            esi -= 4u;
            edi -= 4u;
        }
    }
    r->eax = d; r->edx = n;
}

static void rtl_memcmp(mem m, uint32_t args, es2_rtl_regs *r)
{
    uint32_t a = arg(m, args, 0), b = arg(m, args, 1), ecx = arg(m, args, 2);
    if (!ecx) { r->eax = 0; r->ecx = 0; r->edx = b; return; }
    for (;;) {
        uint32_t before = ecx;
        ecx -= 1u;
        if (!before) {                    /* all n bytes equal: the last pair again */
            r->eax = (uint32_t)rd(m, a - 1u) - rd(m, b - 1u);
            r->edx = rd(m, b - 1u);
            break;
        }
        if (rd(m, a) != rd(m, b)) {
            r->eax = (uint32_t)rd(m, a) - rd(m, b);
            r->edx = rd(m, b);
            break;
        }
        a += 1u;
        b += 1u;
    }
    r->ecx = ecx;
}

static void rtl_strlen(mem m, uint32_t args, es2_rtl_regs *r)
{
    uint32_t count = scan_length(m, arg(m, args, 0));
    r->eax = count - 1u; r->ecx = count;          /* EDX unchanged */
}

static void rtl_strcat(mem m, uint32_t args, es2_rtl_regs *r)
{
    uint32_t d = arg(m, args, 0), s = arg(m, args, 1);
    uint32_t edi = d + scan_length(m, d) - 1u;     /* the destination's NUL */
    uint32_t count = scan_length(m, s), esi = s;   /* source length + 1 */
    copy_forward(m, &edi, &esi, count);
    r->eax = d; r->ecx = 0; r->edx = count;
}

static void rtl_strncat(mem m, uint32_t args, es2_rtl_regs *r)
{
    uint32_t d = arg(m, args, 0), s = arg(m, args, 1);
    uint32_t edi = d + scan_length(m, d) - 1u;
    uint32_t count = scan_length(m, s) - 1u, esi = s;
    uint32_t n = arg(m, args, 2);
    if (count > n) count = n;
    copy_forward(m, &edi, &esi, count);
    wr(m, edi, 0);
    r->eax = d; r->ecx = 0; r->edx = count;
}

static void rtl_strncpy(mem m, uint32_t args, es2_rtl_regs *r)
{
    uint32_t d = arg(m, args, 0), s = arg(m, args, 1);
    uint32_t length = scan_length(m, s) - 1u;
    uint32_t n = arg(m, args, 2);
    uint32_t edi = d, esi = s;
    if (length > n) {
        copy_forward(m, &edi, &esi, n);
        r->edx = n;
    } else {
        copy_forward(m, &edi, &esi, length);
        uint32_t rest = arg(m, args, 2) - length;
        for (uint32_t i = 0; i < rest; ++i) wr(m, d + length + i, 0);
        r->edx = rest;
    }
    r->eax = d; r->ecx = 0;
}

static void rtl_strchr(mem m, uint32_t args, es2_rtl_regs *r)
{
    uint32_t s = arg(m, args, 0);
    uint32_t ecx = scan_length(m, s), edi = s;
    uint8_t c = rd(m, args + 4u);                  /* `mov al, [ebp+0xc]` */
    r->eax = scan(m, &edi, &ecx, c) ? edi - 1u : 0u;
    r->ecx = ecx; r->edx = s;
}

static void rtl_strrchr(mem m, uint32_t args, es2_rtl_regs *r)
{
    uint32_t s = arg(m, args, 0);
    uint32_t c = arg(m, args, 1);
    uint32_t p = s + scan_length(m, s) - 1u;       /* the NUL */
    for (;;) {
        if ((uint8_t)c == rd(m, p)) break;
        if (p == s) { p = 0; break; }
        p -= 1u;
    }
    r->eax = p; r->ecx = s; r->edx = c;
}

static void rtl_strstr(mem m, uint32_t args, es2_rtl_regs *r)
{
    uint32_t s1 = arg(m, args, 0);
    int32_t l1 = (int32_t)(scan_length(m, s1) - 1u);
    uint32_t s2 = arg(m, args, 1);
    int32_t l2 = (int32_t)(scan_length(m, s2) - 1u);
    uint32_t ecx = s2;                             /* `pop ecx` after strlen(s2); EDX untouched */
    if (!l2) { r->eax = s1; r->ecx = ecx; return; }
    if (!l1) { r->eax = 0; r->ecx = ecx; return; }
    int32_t i = 0;
    for (;;) {
        uint32_t edx = s1 + (uint32_t)i;
        while (l1 > i) {
            ecx = (ecx & 0xffffff00u) | rd(m, edx);
            if ((uint8_t)ecx == rd(m, s2)) break;
            i += 1;
            edx += 1u;
        }
        if (l1 == i) { r->eax = 0; r->ecx = ecx; r->edx = edx; return; }
        int32_t j = 0, start = i;
        uint32_t p2 = s2;
        ecx = s1;
        uint32_t esi = s1 + (uint32_t)i;
        while (l1 > i && j < l2) {
            ecx = (ecx & 0xffffff00u) | rd(m, esi);
            if ((uint8_t)ecx != rd(m, p2)) break;
            i += 1; esi += 1u; j += 1; p2 += 1u;
        }
        if (j == l2) { r->eax = s1 + (uint32_t)start; r->ecx = ecx; r->edx = (uint32_t)j; return; }
        if (l1 == i) { r->eax = 0; r->ecx = ecx; r->edx = (uint32_t)j; return; }
        i = start + 1;
    }
}

static void rtl_strncmp(mem m, uint32_t args, es2_rtl_regs *r)
{
    uint32_t a = arg(m, args, 0), b = arg(m, args, 1), n = arg(m, args, 2);
    if (!n) { r->eax = 0; r->ecx = 0; return; }   /* `jecxz`: EDX unchanged */
    uint32_t edi = b, ecx = n;
    scan(m, &edi, &ecx, 0);
    ecx = n - ecx;                                  /* bytes of b up to and including a NUL, at most n */
    uint32_t esi = a;
    edi = b;
    while (ecx) {                                   /* `repe cmpsb` */
        uint8_t x = rd(m, esi), y = rd(m, edi);
        esi += 1u; edi += 1u; ecx -= 1u;
        if (x != y) break;
    }
    uint8_t x = rd(m, esi - 1u), y = rd(m, edi - 1u);
    r->eax = (uint32_t)x - y; r->ecx = ecx; r->edx = y;
}

static void rtl_stricmp(mem m, uint32_t args, es2_rtl_regs *r)
{
    uint32_t a = arg(m, args, 0), b = arg(m, args, 1);
    uint32_t slot = r->ecx;                         /* `push ecx`: the local byte is its top byte */
    uint8_t upper_a;
    for (;;) {
        upper_a = (uint8_t)es2_rtl_toupper(rd(m, a));
        uint8_t upper_b = (uint8_t)es2_rtl_toupper(rd(m, b));
        slot = (slot & 0x00ffffffu) | (uint32_t)upper_b << 24;
        if (upper_a != upper_b || !upper_a) break;
        a += 1u; b += 1u;
    }
    int32_t last_b = sbyte((uint8_t)(slot >> 24));
    r->eax = (uint32_t)(sbyte(upper_a) - last_b);
    r->ecx = slot; r->edx = (uint32_t)last_b;
}

static void rtl_strnicmp(mem m, uint32_t args, es2_rtl_regs *r)
{
    uint32_t a = arg(m, args, 0), b = arg(m, args, 1), left = arg(m, args, 2);
    uint32_t ua, ub;
    for (;;) {
        ua = (uint32_t)es2_rtl_toupper(rd(m, a));
        ub = (uint32_t)es2_rtl_toupper(rd(m, b));
        if (ua != ub || !rd(m, a) || !left) break;
        left -= 1u; a += 1u; b += 1u;
    }
    if (!left) { r->eax = 0; r->ecx = ua; r->edx = ub; return; }
    uint8_t xa = rd(m, a);
    ua = (uint32_t)es2_rtl_toupper(xa);
    uint8_t xb = rd(m, b);
    ub = (uint32_t)es2_rtl_toupper(xb);
    r->eax = ua - ub; r->ecx = (uint32_t)sbyte(xb); r->edx = ub;
}

static void rtl_case(mem m, uint32_t args, es2_rtl_regs *r, int upper)
{
    uint32_t s = arg(m, args, 0);
    for (uint32_t p = s;; ++p) {
        uint8_t byte = (uint8_t)(upper ? es2_rtl_toupper(rd(m, p)) : es2_rtl_tolower(rd(m, p)));
        wr(m, p, byte);
        if (!byte) break;
    }
    r->eax = s; r->ecx = 0; r->edx = 0;             /* the last call is toupper(0)/tolower(0) */
}

static void rtl_itoa(mem m, uint32_t args, es2_rtl_regs *r)
{
    uint32_t value = arg(m, args, 0), buf = arg(m, args, 1), radix = arg(m, args, 2);
    if ((int32_t)radix < 2 || (int32_t)radix > 36) {
        wr(m, buf, 0);
        r->eax = buf; r->ecx = buf; r->edx = value;
        return;
    }
    uint32_t p = buf;
    if ((int32_t)value < 0 && radix == 10u) {       /* signed only in radix 10 */
        wr(m, p++, '-');
        value = 0u - value;
    }
    uint8_t digits[32];
    unsigned count = 0;
    do {
        digits[count++] = (uint8_t)(value % radix);
        value /= radix;
    } while (value);
    while (count) {
        uint8_t d = digits[--count];
        wr(m, p++, (uint8_t)(d < 10u ? '0' + d : 'a' + d - 10u));
    }
    wr(m, p, 0);
    /* ECX = EDX = the conversion helper's digit buffer at entry ESP - 68. */
    r->eax = buf; r->ecx = r->edx = args - 72u;
}

static void rtl_atol(mem m, uint32_t args, es2_rtl_regs *r)
{
    uint32_t edx = arg(m, args, 0), ecx = 0;
    uint8_t al;
    do { al = rd(m, edx); edx += 1u; } while (atol_space(al));
    int negative = 0;
    if (al == '+' || al == '-') {
        negative = al == '-';
        al = rd(m, edx);
        edx += 1u;
    }
    while (al >= '0' && al <= '9') {                /* signed compare: bytes >= 0x80 stop */
        ecx = ecx * 10u + al - 0x30u;
        al = rd(m, edx);
        edx += 1u;
    }
    r->eax = negative ? 0u - ecx : ecx; r->ecx = ecx; r->edx = edx;
}

int32_t es2_rtl_atol(const char *text)
{
    const uint8_t *p = (const uint8_t *)text;
    while (atol_space(*p)) p += 1;
    int negative = *p == '-';
    if (*p == '+' || *p == '-') p += 1;
    uint32_t value = 0;
    for (; *p >= '0' && *p <= '9'; p += 1) value = value * 10u + *p - 0x30u;
    return (int32_t)(negative ? 0u - value : value);
}

/* ------------------------------------------------------------------------
 * sprintf / vsprintf: CW3220's formatter (a state machine over a class
 * table) writing through an 80-byte buffer whose flushes copy into the
 * destination and advance the cursor kept in sprintf's first argument slot. */
typedef struct printer {
    mem m;
    uint32_t slot;
    uint32_t fill, total;
    uint8_t buffer[0x50];
} printer;

static void flush(printer *p)
{
    if (!p->fill) return;
    uint32_t to = rd32(p->m, p->slot);
    for (uint32_t i = 0; i < p->fill; ++i) wr(p->m, to + i, p->buffer[i]);
    wr32(p->m, p->slot, rd32(p->m, p->slot) + p->fill);
    wr(p->m, rd32(p->m, p->slot), 0);
    p->fill = 0;
}
static void put(printer *p, uint8_t byte)
{
    if (p->fill >= 0x50u) flush(p);
    p->buffer[p->fill++] = byte;
    p->total += 1u;
}

/* Character classes of the formatter for 0x20..0x7f (bytes below 0x20 and
 * >= 0x80 end a specification as invalid). */
enum {
    C_SIGN = 0, C_ALT = 1, C_STAR = 2, C_LEFT = 3, C_DOT = 4, C_DIGIT = 5, C_LONG = 6, C_LDOUBLE = 7,
    C_SHORT = 8, C_ZERO = 9, C_DEC = 10, C_OCT = 11, C_UNS = 12, C_HEX = 13, C_PTR = 14, C_FLOAT = 15,
    C_CHAR = 16, C_STR = 17, C_COUNT = 18, C_BAD = 20, C_NEAR = 22, C_FAR = 23
};
static unsigned char_class(uint8_t c)
{
    switch (c) {
    case ' ': case '+': return C_SIGN;
    case '#': return C_ALT;
    case '*': return C_STAR;
    case '-': return C_LEFT;
    case '.': return C_DOT;
    case '0': return C_ZERO;
    case 'l': return C_LONG;
    case 'L': return C_LDOUBLE;
    case 'h': case 'H': return C_SHORT;
    case 'd': case 'i': return C_DEC;
    case 'o': return C_OCT;
    case 'u': return C_UNS;
    case 'x': case 'X': return C_HEX;
    case 'p': return C_PTR;
    case 'e': case 'E': case 'f': case 'g': case 'G': return C_FLOAT;
    case 'c': return C_CHAR;
    case 's': return C_STR;
    case 'n': return C_COUNT;
    case 'N': return C_NEAR;
    case 'F': return C_FAR;
    default: return c >= '1' && c <= '9' ? C_DIGIT : C_BAD;
    }
}

/* The text an output step reads: the local digit buffer, a guest string or
 * the formatter's own "(null)". */
typedef struct source {
    const uint8_t *host;
    uint32_t guest;
} source;
static uint8_t src_at(mem m, const source *s, int32_t i)
{
    return s->host ? s->host[i] : rd(m, s->guest + (uint32_t)i);
}
static int32_t src_length(mem m, const source *s)
{
    if (s->host) { int32_t n = 0; while (s->host[n]) ++n; return n; }
    return (int32_t)(scan_length(m, s->guest) - 1u);
}

/* Integer to text as CW3220's conversion helper: '-' only when `is_signed`,
 * digits from 'a'/'A' above 9, then a NUL. */
static void to_text(uint8_t *out, uint32_t value, uint32_t radix, int is_signed, uint8_t hex_base)
{
    if ((int32_t)value < 0 && is_signed) { *out++ = '-'; value = 0u - value; }
    uint8_t digits[32];
    unsigned count = 0;
    do { digits[count++] = (uint8_t)(value % radix); value /= radix; } while (value);
    while (count) {
        uint8_t d = digits[--count];
        *out++ = (uint8_t)(d < 10u ? '0' + d : hex_base + d - 10u);
    }
    *out = 0;
}

/* Runs the formatter over `format` with the argument pointer `ap`. With
 * `p` NULL nothing is written and no argument is read: the pass only
 * decides whether a conversion outside the own subset (floating point, %p,
 * %n) is reached. Returns ES2_RTL_UNSUPPORTED for such a format. */
static int run_format(mem m, printer *p, uint32_t format, uint32_t ap)
{
    static const uint8_t null_text[] = "(null)";
    uint32_t f = format;
    for (;;) {
        uint8_t c = rd(m, f++);
        if (!c) break;
        if (c != '%') { if (p) put(p, c); continue; }
        c = rd(m, f);
        if (c == '%') { f += 1u; if (p) put(p, c); continue; }
        uint32_t start = f - 1u;
        uint8_t sign = 0, hex_base = 'a';
        int32_t zeros = 0, precision = -1, width = -1;
        uint32_t flags = 0x20u, state = 0;
        source text = {NULL, 0};
        uint8_t local[48];                         /* local[0]: room for a sign prefix */
        int32_t length = 0;
        int invalid = 0, done = 0;
        while (!done && !invalid) {
            c = rd(m, f++);
            if (c < 0x20u || c >= 0x80u) { invalid = 1; break; }
            unsigned k = char_class(c);
            if (k == C_ZERO && (int32_t)state > 0) k = C_DIGIT;   /* a digit of the width or precision */
            switch (k) {
            case C_SIGN:
                if ((int32_t)state > 0) { invalid = 1; break; }
                if (sign != '+') sign = c;
                break;
            case C_ALT:
                if ((int32_t)state > 0) { invalid = 1; break; }
                flags |= 1u;
                break;
            case C_LEFT:
                if ((int32_t)state > 0) { invalid = 1; break; }
                flags |= 2u;
                break;
            case C_ZERO:
                if (!(flags & 2u)) { flags |= 8u; state = 1; }
                break;
            case C_DIGIT: {
                int32_t d = (int32_t)c - '0';
                if ((int32_t)state <= 2) {
                    state = 2;
                    width = width == -1 ? d : width * 10 + d;
                } else if (state == 4) {
                    precision = precision * 10 + d;
                } else invalid = 1;
                break;
            }
            case C_STAR: {
                uint32_t value = p ? rd32(m, ap) : 0u;
                ap += 4u;
                if ((int32_t)state < 2) {
                    if ((int32_t)value < 0) { width = (int32_t)(0u - value); flags |= 2u; }
                    else width = (int32_t)value;
                    state = 3;
                } else if (state == 4) {
                    precision = (int32_t)value;
                    state = 5;
                } else invalid = 1;
                break;
            }
            case C_DOT:
                if ((int32_t)state >= 4) { invalid = 1; break; }
                state = 4;
                precision += 1;
                break;
            case C_LONG: flags |= 0x10u; state = 5; break;
            case C_LDOUBLE: flags = (flags | 0x100u) & ~0x10u; state = 5; break;
            case C_SHORT: flags = (flags | 0x200u) & ~0x10u; state = 5; break;
            case C_NEAR: flags &= ~0x20u; state = 5; break;
            case C_FAR: flags |= 0x20u; state = 5; break;
            case C_PTR: case C_FLOAT: case C_COUNT:
                return ES2_RTL_UNSUPPORTED;
            case C_CHAR:
                if (p) {
                    local[0] = rd(m, ap);                   /* one byte of the slot */
                    local[1] = 0;
                    text.host = local;
                    length = 1;
                }
                ap += 4u;
                done = 1;
                break;
            case C_STR:
                if (p) {
                    uint32_t s = rd32(m, ap);
                    if (s) text.guest = s; else text.host = null_text;
                    length = src_length(m, &text);
                    if (length > precision && precision >= 0) length = precision;
                }
                ap += 4u;
                done = 1;
                break;
            case C_DEC: case C_OCT: case C_UNS: case C_HEX: {
                uint32_t radix = k == C_OCT ? 8u : k == C_HEX ? 16u : 10u;
                int is_signed = k == C_DEC;
                if (k == C_HEX) hex_base = (uint8_t)(c - 0x17);   /* 'a' or 'A' */
                if (!is_signed) sign = 0;
                done = 1;
                if (!p) { ap += 4u; break; }
                uint32_t value;
                if (flags & 0x10u) value = rd32(m, ap);
                else if (flags & 0x200u) {
                    uint32_t w = (uint32_t)rd(m, ap) | (uint32_t)rd(m, ap + 1u) << 8;
                    value = is_signed ? (uint32_t)(int32_t)(int16_t)w : w;
                } else value = rd32(m, ap);
                ap += 4u;
                uint8_t *b = local + 1;
                text.host = b;
                if (value) { flags |= 4u; to_text(b, value, radix, is_signed, hex_base); }
                else if (precision != 0) to_text(b, value, radix, is_signed, hex_base);
                else b[0] = 0;
                if (precision >= 0) {
                    int32_t digits = src_length(m, &text);
                    length = digits;
                    if (b[0] == '-') digits -= 1;
                    else if (sign) { length += 1; text.host = b - 1; b[-1] = sign; }
                    if (precision > digits) zeros = precision - digits;
                } else {
                    if ((flags & 8u) && width > 0) {
                        int32_t n = src_length(m, &text);
                        if (b[0] == '-') n -= 1;
                        if (width > n) zeros = width - n;
                    }
                    uint8_t first = b[0];
                    if (first == '-' || sign) {
                        if (first != '-') { text.host = b - 1; b[-1] = sign; }
                        if (zeros > 0 && precision < 0) zeros -= 1;
                    }
                    length = src_length(m, &text);
                }
                break;
            }
            default:
                invalid = 1;
                break;
            }
        }
        if (invalid) {
            /* The rest of the format, from the '%', is copied as it is and
             * ends the formatting. */
            if (p) {
                for (uint32_t q = start;; ++q) {
                    uint8_t byte = rd(m, q);
                    if (!byte) break;
                    put(p, byte);
                }
            }
            break;
        }
        if (!p) continue;
        if ((flags & 5u) == 5u) {
            if (c == 'o') { if (zeros <= 0) zeros = 1; }
            else if (c == 'x' || c == 'X') {
                flags |= 0x40u;
                width -= 2;
                zeros -= 2;
                if (zeros < 0) zeros = 0;
            }
        }
        length += zeros;
        if (!(flags & 2u)) while (width > length) { put(p, ' '); width -= 1; }
        if (flags & 0x40u) { put(p, '0'); put(p, c); }
        int32_t at = 0;
        if (zeros > 0) {
            length -= zeros;
            width -= zeros;
            uint8_t first = src_at(m, &text, 0);
            if (first == '-' || first == ' ' || first == '+') {
                put(p, first);
                at = 1;
                length -= 1;
                width -= 1;
            }
            while (zeros-- != 0) put(p, '0');
        }
        if (length != 0) {
            width -= length;
            while (length-- != 0) put(p, src_at(m, &text, at++));
        }
        while (width-- > 0) put(p, ' ');
    }
    return ES2_RTL_DONE;
}

static int rtl_print(mem m, uint32_t args, es2_rtl_regs *r, int variadic)
{
    uint32_t d = arg(m, args, 0), fmt = arg(m, args, 1);
    uint32_t ap = variadic ? args + 8u : arg(m, args, 2);
    if (run_format(m, NULL, fmt, ap) != ES2_RTL_DONE) return ES2_RTL_UNSUPPORTED;
    wr(m, d, 0);
    printer p;
    p.m = m; p.slot = args; p.fill = 0; p.total = 0;
    /* The format and arguments are read again, after dest[0] = 0, as the
     * original reads them. */
    run_format(m, &p, arg(m, args, 1), variadic ? args + 8u : arg(m, args, 2));
    flush(&p);
    if (!p.total) return ES2_RTL_UNSUPPORTED;      /* EDX would be formatter residue */
    /* ECX: the formatter's buffer record at entry ESP - 216; EDX: the final
     * flush clears it. */
    r->eax = p.total; r->ecx = args - 220u; r->edx = 0;
    return ES2_RTL_DONE;
}

int es2_rtl_call(es2_rtl_function function, const es2_rtl_memory *memory, uint32_t args, es2_rtl_regs *regs)
{
    switch (function) {
    case ES2_RTL_MEMSET: rtl_memset(memory, args, regs); break;
    case ES2_RTL_MEMCPY: rtl_memcpy(memory, args, regs); break;
    case ES2_RTL_MEMMOVE: rtl_memmove(memory, args, regs); break;
    case ES2_RTL_MEMCMP: rtl_memcmp(memory, args, regs); break;
    case ES2_RTL_STRLEN: rtl_strlen(memory, args, regs); break;
    case ES2_RTL_STRCAT: rtl_strcat(memory, args, regs); break;
    case ES2_RTL_STRNCAT: rtl_strncat(memory, args, regs); break;
    case ES2_RTL_STRNCPY: rtl_strncpy(memory, args, regs); break;
    case ES2_RTL_STRCHR: rtl_strchr(memory, args, regs); break;
    case ES2_RTL_STRRCHR: rtl_strrchr(memory, args, regs); break;
    case ES2_RTL_STRSTR: rtl_strstr(memory, args, regs); break;
    case ES2_RTL_STRNCMP: rtl_strncmp(memory, args, regs); break;
    case ES2_RTL_STRICMP: rtl_stricmp(memory, args, regs); break;
    case ES2_RTL_STRNICMP: rtl_strnicmp(memory, args, regs); break;
    case ES2_RTL_STRUPR: rtl_case(memory, args, regs, 1); break;
    case ES2_RTL_STRLWR: rtl_case(memory, args, regs, 0); break;
    case ES2_RTL_ITOA: rtl_itoa(memory, args, regs); break;
    case ES2_RTL_ATOL: rtl_atol(memory, args, regs); break;
    case ES2_RTL_SPRINTF: return rtl_print(memory, args, regs, 1);
    case ES2_RTL_VSPRINTF: return rtl_print(memory, args, regs, 0);
    default: return ES2_RTL_UNSUPPORTED;
    }
    return ES2_RTL_DONE;
}

const char *es2_rtl_name(es2_rtl_function function)
{
    static const char *const names[ES2_RTL_FUNCTION_COUNT] = {
        "_memset", "_memcpy", "_memmove", "_memcmp", "_strlen", "_strcat", "_strncat", "_strncpy",
        "_strchr", "_strrchr", "_strstr", "_strncmp", "_stricmp", "_strnicmp", "_strupr", "_strlwr",
        "_itoa", "_atol", "_sprintf", "_vsprintf",
    };
    return (unsigned)function < ES2_RTL_FUNCTION_COUNT ? names[function] : "";
}
