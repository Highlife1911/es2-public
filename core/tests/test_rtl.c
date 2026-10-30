/* Unit test of core/src/rtl.c on a synthetic arena. Expected values are the
 * documented CW3220 contracts (docs/reference-game/rtl_imports.md, "the pure
 * imports as own code"); the in-vitro harness compares the same code with the
 * original DLL. */
#include "es2/core/rtl.h"

#include <stdio.h>
#include <string.h>

static unsigned failures, checks;
static void check(int passed, const char *expression, int line)
{
    ++checks;
    if (!passed) {
        if (failures < 30) fprintf(stderr, "rtl:%d: %s\n", line, expression);
        ++failures;
    }
}
#define CHECK(expression) check((expression) ? 1 : 0, #expression, __LINE__)

#define BASE 0x00100000u
#define SIZE 0x10000u
static uint8_t arena[SIZE];
static unsigned outside;
static uint8_t read8(void *context, uint32_t address)
{
    (void)context;
    if (address - BASE >= SIZE) { ++outside; return 0; }
    return arena[address - BASE];
}
static void write8(void *context, uint32_t address, uint8_t value)
{
    (void)context;
    if (address - BASE >= SIZE) { ++outside; return; }
    arena[address - BASE] = value;
}
static const es2_rtl_memory memory = {NULL, read8, write8};

/* Argument slots at BASE + 0x100 (the return address would be at 0xfc). */
#define ARGS (BASE + 0x100u)
#define A(offset) (BASE + (offset))
static void put32(uint32_t address, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) arena[address - BASE + i] = (uint8_t)(value >> (8 * i));
}
static uint32_t get32(uint32_t address)
{
    uint32_t v = 0;
    for (unsigned i = 0; i < 4; ++i) v |= (uint32_t)arena[address - BASE + i] << (8 * i);
    return v;
}
static void text(uint32_t address, const char *s) { memcpy(arena + (address - BASE), s, strlen(s) + 1); }
static const char *at(uint32_t address) { return (const char *)arena + (address - BASE); }
static es2_rtl_regs call(es2_rtl_function f, uint32_t a0, uint32_t a1, uint32_t a2, int *status)
{
    put32(ARGS, a0); put32(ARGS + 4, a1); put32(ARGS + 8, a2);
    es2_rtl_regs r = {0xaaaaaaaau, 0xccccccccu, 0xddddddddu};
    int s = es2_rtl_call(f, &memory, ARGS, &r);
    if (status) *status = s;
    return r;
}

static void memory_functions(void)
{
    memset(arena, 0x5a, SIZE);
    es2_rtl_regs r = call(ES2_RTL_MEMSET, A(0x1000), 0x1ab, 7, NULL);
    CHECK(r.eax == A(0x1000) && r.ecx == 0 && r.edx == 7);
    CHECK(!memcmp(at(A(0x1000)), "\xab\xab\xab\xab\xab\xab\xab\x5a", 8) && arena[0xfff] == 0x5a);
    /* memcpy is a forward dword copy: overlap by one byte smears dwords. */
    text(A(0x2000), "ABCDEFGHIJ");
    r = call(ES2_RTL_MEMCPY, A(0x2001), A(0x2000), 9, NULL);
    CHECK(r.eax == A(0x2001) && r.ecx == 0 && r.edx == 9 && !memcmp(at(A(0x2000)), "AABCDDFGHH", 10));
    text(A(0x2000), "ABCDEFGHIJ");
    r = call(ES2_RTL_MEMMOVE, A(0x2001), A(0x2000), 9, NULL);
    CHECK(r.eax == A(0x2001) && r.ecx == 0 && r.edx == 9 && !memcmp(at(A(0x2000)), "AABCDEFGHI", 10));
    text(A(0x2000), "ABCDEFGHIJ");
    r = call(ES2_RTL_MEMMOVE, A(0x2000), A(0x2003), 7, NULL);
    CHECK(r.ecx == 0 && !memcmp(at(A(0x2000)), "DEFGHIJHIJ", 10));
    r = call(ES2_RTL_MEMMOVE, A(0x2000), A(0x2000), 7, NULL);
    CHECK(r.eax == A(0x2000) && r.ecx == 7 && r.edx == 7);
    text(A(0x3000), "abcX"); text(A(0x3100), "abcd");
    r = call(ES2_RTL_MEMCMP, A(0x3000), A(0x3100), 4, NULL);
    CHECK(r.eax == (uint32_t)('X' - 'd') && r.ecx == 0 && r.edx == 'd');
    r = call(ES2_RTL_MEMCMP, A(0x3000), A(0x3100), 3, NULL);
    CHECK(r.eax == 0 && r.ecx == UINT32_MAX && r.edx == 'c');
    r = call(ES2_RTL_MEMCMP, A(0x3000), A(0x3100), 0, NULL);
    CHECK(r.eax == 0 && r.ecx == 0 && r.edx == A(0x3100));
    arena[0x3000] = 0x90;
    r = call(ES2_RTL_MEMCMP, A(0x3000), A(0x3100), 4, NULL);
    CHECK(r.eax == 0x90u - 'a' && r.ecx == 3 && r.edx == 'a');
}

static void string_functions(void)
{
    memset(arena, 0x5a, SIZE);
    text(A(0x1000), "hello");
    es2_rtl_regs r = call(ES2_RTL_STRLEN, A(0x1000), 0, 0, NULL);
    CHECK(r.eax == 5 && r.ecx == 6 && r.edx == 0xddddddddu);
    text(A(0x1100), "ab"); text(A(0x1200), "cde");
    r = call(ES2_RTL_STRCAT, A(0x1100), A(0x1200), 0, NULL);
    CHECK(r.eax == A(0x1100) && r.ecx == 0 && r.edx == 4 && !strcmp(at(A(0x1100)), "abcde") && arena[0x1106] == 0x5a);
    text(A(0x1100), "ab");
    r = call(ES2_RTL_STRNCAT, A(0x1100), A(0x1200), 2, NULL);
    CHECK(r.eax == A(0x1100) && r.ecx == 0 && r.edx == 2 && !strcmp(at(A(0x1100)), "abcd") && arena[0x1105] == 0 && arena[0x1106] == 0x5a);
    memset(arena + 0x1300, 0x5a, 16);
    r = call(ES2_RTL_STRNCPY, A(0x1300), A(0x1200), 6, NULL);
    CHECK(r.eax == A(0x1300) && r.ecx == 0 && r.edx == 3 && !memcmp(at(A(0x1300)), "cde\0\0\0\x5a", 7));
    memset(arena + 0x1300, 0x5a, 16);
    r = call(ES2_RTL_STRNCPY, A(0x1300), A(0x1200), 2, NULL);
    CHECK(r.edx == 2 && !memcmp(at(A(0x1300)), "cd\x5a", 3));
    text(A(0x1400), "a.dba.x");
    r = call(ES2_RTL_STRCHR, A(0x1400), '.', 0, NULL);
    CHECK(r.eax == A(0x1401) && r.ecx == 6 && r.edx == A(0x1400));
    r = call(ES2_RTL_STRCHR, A(0x1400), 0x100 + 'q', 0, NULL);
    CHECK(r.eax == 0 && r.ecx == 0);
    r = call(ES2_RTL_STRCHR, A(0x1400), 0, 0, NULL);
    CHECK(r.eax == A(0x1407) && r.ecx == 0);
    r = call(ES2_RTL_STRRCHR, A(0x1400), 0x300 + '.', 0, NULL);
    CHECK(r.eax == A(0x1405) && r.ecx == A(0x1400) && r.edx == 0x300u + '.');
    r = call(ES2_RTL_STRRCHR, A(0x1400), 'q', 0, NULL);
    CHECK(r.eax == 0);
    text(A(0x1500), "xxabcab"); text(A(0x1600), "cab");
    r = call(ES2_RTL_STRSTR, A(0x1500), A(0x1600), 0, NULL);
    CHECK(r.eax == A(0x1504) && r.edx == 3);
    text(A(0x1600), "");
    r = call(ES2_RTL_STRSTR, A(0x1500), A(0x1600), 0, NULL);
    CHECK(r.eax == A(0x1500) && r.ecx == A(0x1600) && r.edx == 0xddddddddu);
    text(A(0x1600), "abd");
    r = call(ES2_RTL_STRSTR, A(0x1500), A(0x1600), 0, NULL);
    CHECK(r.eax == 0);
    text(A(0x1700), "abc"); text(A(0x1800), "abd");
    r = call(ES2_RTL_STRNCMP, A(0x1700), A(0x1800), 10, NULL);
    CHECK(r.eax == (uint32_t)('c' - 'd') && r.ecx == 1 && r.edx == 'd');
    r = call(ES2_RTL_STRNCMP, A(0x1700), A(0x1800), 2, NULL);
    CHECK(r.eax == 0 && r.ecx == 0 && r.edx == 'b');
    r = call(ES2_RTL_STRNCMP, A(0x1700), A(0x1800), 0, NULL);
    CHECK(r.eax == 0 && r.ecx == 0 && r.edx == 0xddddddddu);
}

static void case_functions(void)
{
    memset(arena, 0x5a, SIZE);
    CHECK(es2_rtl_toupper('a') == 'A' && es2_rtl_toupper('A') == 'A' && es2_rtl_toupper(0xe4) == 0xe4 && es2_rtl_toupper(0xff) == -1);
    CHECK(es2_rtl_tolower('Z') == 'z' && es2_rtl_tolower('{') == '{' && es2_rtl_tolower(0xc4) == 0xc4 && es2_rtl_tolower(0xff) == -1);
    text(A(0x1000), "Grid.DBA"); text(A(0x1100), "grid.dba");
    es2_rtl_regs r = call(ES2_RTL_STRICMP, A(0x1000), A(0x1100), 0, NULL);
    CHECK(r.eax == 0 && r.ecx == 0x00ccccccu && r.edx == 0);
    text(A(0x1000), "\x80"); text(A(0x1100), "a");
    r = call(ES2_RTL_STRICMP, A(0x1000), A(0x1100), 0, NULL);
    CHECK(r.eax == (uint32_t)(-128 - 65) && r.ecx == (0x00ccccccu | 0x41000000u) && r.edx == 0x41);
    r = call(ES2_RTL_STRNICMP, A(0x1000), A(0x1100), 5, NULL);
    CHECK(r.eax == 0x80u - 0x41u && r.ecx == 'a' && r.edx == 'A');
    text(A(0x1000), "abX"); text(A(0x1100), "ABy");
    r = call(ES2_RTL_STRNICMP, A(0x1000), A(0x1100), 2, NULL);
    CHECK(r.eax == 0 && r.ecx == 'X' && r.edx == 'Y');
    r = call(ES2_RTL_STRNICMP, A(0x1000), A(0x1100), 0, NULL);
    CHECK(r.eax == 0 && r.ecx == 'A' && r.edx == 'A');
    text(A(0x1200), "aZ\xe4\xff!");
    r = call(ES2_RTL_STRUPR, A(0x1200), 0, 0, NULL);
    CHECK(r.eax == A(0x1200) && r.ecx == 0 && r.edx == 0 && !strcmp(at(A(0x1200)), "AZ\xe4\xff!"));
    r = call(ES2_RTL_STRLWR, A(0x1200), 0, 0, NULL);
    CHECK(!strcmp(at(A(0x1200)), "az\xe4\xff!"));
}

static void conversions(void)
{
    memset(arena, 0x5a, SIZE);
    es2_rtl_regs r = call(ES2_RTL_ITOA, (uint32_t)-5, A(0x1000), 10, NULL);
    CHECK(r.eax == A(0x1000) && r.ecx == ARGS - 72u && r.edx == ARGS - 72u && !strcmp(at(A(0x1000)), "-5"));
    call(ES2_RTL_ITOA, (uint32_t)-5, A(0x1000), 16, NULL);
    CHECK(!strcmp(at(A(0x1000)), "fffffffb"));
    call(ES2_RTL_ITOA, 0x80000000u, A(0x1000), 10, NULL);
    CHECK(!strcmp(at(A(0x1000)), "-2147483648"));
    call(ES2_RTL_ITOA, 35, A(0x1000), 36, NULL);
    CHECK(!strcmp(at(A(0x1000)), "z"));
    call(ES2_RTL_ITOA, 0, A(0x1000), 2, NULL);
    CHECK(!strcmp(at(A(0x1000)), "0"));
    r = call(ES2_RTL_ITOA, 77, A(0x1000), 37, NULL);
    CHECK(r.eax == A(0x1000) && r.ecx == A(0x1000) && r.edx == 77 && arena[0x1000] == 0 && arena[0x1002] == '1');
    text(A(0x1100), "  -123x");
    r = call(ES2_RTL_ATOL, A(0x1100), 0, 0, NULL);
    CHECK(r.eax == (uint32_t)-123 && r.ecx == 123 && r.edx == A(0x1107));
    text(A(0x1100), "\x80\x82 +7");
    r = call(ES2_RTL_ATOL, A(0x1100), 0, 0, NULL);
    CHECK(r.eax == 7);
    text(A(0x1100), "\x81" "7");
    r = call(ES2_RTL_ATOL, A(0x1100), 0, 0, NULL);
    CHECK(r.eax == 0 && r.edx == A(0x1101));
    text(A(0x1100), "4294967297");
    r = call(ES2_RTL_ATOL, A(0x1100), 0, 0, NULL);
    CHECK(r.eax == 1);
    CHECK(es2_rtl_atol("  -123x") == -123 && es2_rtl_atol("\x80\x82 +7") == 7 && es2_rtl_atol("\x81" "7") == 0);
    CHECK(es2_rtl_atol("4294967297") == 1 && es2_rtl_atol("-2147483648") == INT32_MIN && es2_rtl_atol("\xff" "5") == 0);
}

static void formatting(void)
{
    memset(arena, 0x5a, SIZE);
    text(A(0x2000), "%d|%5d|%-5d|%05d|%+d|% d|%.3d|%x|%#x|%#o|%c|%s|%.2s|%ld|%hd|%X|%#05x|%u|%s|%5%|tail");
    text(A(0x2100), "hi"); text(A(0x2104), "hello");
    static const uint32_t values[] = {42, 42, 42, (uint32_t)-42, 42, 42, 7, 255, 255, 8, 'Q', 0, 0,
                                      (uint32_t)-1, 0x12345678u, 0xbeef, 255, 0xffffffffu, 0};
    uint32_t ap = ARGS + 8;
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) put32(ap + 4 * i, values[i]);
    put32(ap + 4 * 11, A(0x2100)); put32(ap + 4 * 12, A(0x2104));
    put32(ARGS, A(0x3000)); put32(ARGS + 4, A(0x2000));
    es2_rtl_regs r = {1, 2, 3};
    int status = es2_rtl_call(ES2_RTL_SPRINTF, &memory, ARGS, &r);
    const char *want = "42|   42|42   |-0042|+42| 42|007|ff|0xff|010|Q|hi|he|-1|22136|BEEF|0x0ff|4294967295|(null)|%5%|tail";
    CHECK(status == ES2_RTL_DONE && !strcmp(at(A(0x3000)), want));
    if (strcmp(at(A(0x3000)), want)) fprintf(stderr, "got  %s\nwant %s\n", at(A(0x3000)), want);
    CHECK(r.eax == strlen(want) && r.ecx == ARGS - 220u && r.edx == 0 && get32(ARGS) == A(0x3000) + strlen(want));
    CHECK(arena[0x3000 + strlen(want) + 1] == 0x5a);
    /* vsprintf: the argument pointer is the third slot. */
    text(A(0x2200), "[%*d|%-*s|%.*d]");
    put32(A(0x2300), (uint32_t)-4); put32(A(0x2304), 9); put32(A(0x2308), 3); put32(A(0x230c), A(0x2100));
    put32(A(0x2310), 4); put32(A(0x2314), 5);
    put32(ARGS, A(0x3100)); put32(ARGS + 4, A(0x2200)); put32(ARGS + 8, A(0x2300));
    status = es2_rtl_call(ES2_RTL_VSPRINTF, &memory, ARGS, &r);
    CHECK(status == ES2_RTL_DONE && !strcmp(at(A(0x3100)), "[9   |hi |0005]") && r.eax == 15 && get32(ARGS) == A(0x3100) + 15);
    /* More than one 80-byte buffer: two flushes, one cursor. */
    char longer[200];
    memset(longer, 'y', 180); longer[180] = 0;
    text(A(0x2400), longer);
    put32(ARGS, A(0x4000)); put32(ARGS + 4, A(0x2400));
    status = es2_rtl_call(ES2_RTL_SPRINTF, &memory, ARGS, &r);
    CHECK(status == ES2_RTL_DONE && r.eax == 180 && !strcmp(at(A(0x4000)), longer) && get32(ARGS) == A(0x4000) + 180);
    /* Floating point, %p, %n: nothing done; an empty result: only dest[0] = 0. */
    static const char *const unsupported[] = {"%f", "x%5.2e", "%p", "%n", "%g"};
    for (unsigned i = 0; i < 5; ++i) {
        text(A(0x2500), unsupported[i]);
        arena[0x5000] = 0x77;
        put32(ARGS, A(0x5000)); put32(ARGS + 4, A(0x2500));
        CHECK(es2_rtl_call(ES2_RTL_SPRINTF, &memory, ARGS, &r) == ES2_RTL_UNSUPPORTED && arena[0x5000] == 0x77);
    }
    text(A(0x2500), "");
    put32(ARGS, A(0x5000)); put32(ARGS + 4, A(0x2500));
    CHECK(es2_rtl_call(ES2_RTL_SPRINTF, &memory, ARGS, &r) == ES2_RTL_UNSUPPORTED && arena[0x5000] == 0);
    /* An invalid specification ends the formatting with the rest verbatim. */
    text(A(0x2500), "a%5.5.5d%d");
    arena[0x5000] = 0x77;
    CHECK(es2_rtl_call(ES2_RTL_SPRINTF, &memory, ARGS, &r) == ES2_RTL_DONE && !strcmp(at(A(0x5000)), "a%5.5.5d%d"));
}

int main(void)
{
    memory_functions();
    string_functions();
    case_functions();
    conversions();
    formatting();
    CHECK(outside == 0);
    CHECK(!strcmp(es2_rtl_name(ES2_RTL_VSPRINTF), "_vsprintf") && !strcmp(es2_rtl_name(ES2_RTL_MEMSET), "_memset"));
    if (failures) { fprintf(stderr, "rtl: %u of %u checks failed\n", failures, checks); return 1; }
    printf("rtl: %u checks passed\n", checks);
    return 0;
}
