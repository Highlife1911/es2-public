/* The 20 pure CW3220 imports, in vitro against the original DLL.
 *
 * CW3220.DLL is read from ES2_DATA_ROOT (CD root or extracted/cd), size and
 * SHA-256 checked on the read buffer, and mapped privately at an address
 * the system chooses (DBSIM occupies its preferred base 0x400000): sections
 * copied, HIGHLOW relocations applied, every import pointed to a named trap,
 * CODE read/execute. No entry point, TLS callback or import of the DLL runs;
 * the functions under test call none (a trap would fail the case).
 *
 * Direct cases: per function generated arguments, arena contents and entry
 * EAX/ECX/EDX; the export runs on a private test stack, then the arena is
 * restored and core/src/rtl.c runs on the same addresses. EAX/ECX/EDX, every
 * arena byte and the argument slots (sprintf's cursor) must be equal.
 * Installed cases: the proxy's IAT shim (proxy/rtl_imports.h) on the mapped
 * DBSIM with the CW3220 slots set to the mapped exports, every own import in
 * mode replaced, then in mode compare; each call goes through DBSIM's thunk
 * and must equal the direct original call, including the stack below the
 * return address for the leaf functions (and for compare mode everything),
 * with no compare mismatch. Selection: --rtl (all) or rtl_<function>. */
#define ES2_RTL_NO_PROCESS_ENTRY
#include "tools/reference-game/proxy/rtl_imports.h"
static void rtl_arm(void) {}

#define RTL_CW_FILE_SIZE 176128u
#define RTL_CW_SHA256 "6e90e1cad6d61bfaecc115286ba52b170bc05ef7f0b4cb1742a1fb28fbd3f505"
#define RTL_CW_IMAGE_SIZE 0x2e000u
static BYTE *rtl_cw;
static char rtl_cw_imports[128][64];
static unsigned rtl_cw_import_count;
static void __cdecl rtl_cw_trap(unsigned id) {
    fprintf(stderr, "TRAP: %s reached CW3220's import %s\n", current_function, id < rtl_cw_import_count ? rtl_cw_imports[id] : "invalid");
    longjmp(trap_return, 1);
}
static int rtl_map_cw3220(const char *root) {
    char candidates[2][MAX_PATH], path[MAX_PATH];
    if (snprintf(candidates[0], MAX_PATH, "%s/CW3220.DLL", root) >= MAX_PATH ||
        snprintf(candidates[1], MAX_PATH, "%s/extracted/cd/CW3220.DLL", root) >= MAX_PATH) return fail("root too long");
    unsigned found = 0;
    for (unsigned i = 0; i < 2; ++i) if (GetFileAttributesA(candidates[i]) != INVALID_FILE_ATTRIBUTES) { strcpy(path, candidates[i]); ++found; }
    if (found != 1) return fail("ES2_DATA_ROOT must contain exactly one CW3220.DLL in the two supported locations");
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return fail("open CW3220.DLL");
    LARGE_INTEGER size;
    BYTE *bytes = malloc(RTL_CW_FILE_SIZE);
    DWORD got = 0;
    BOOL read = GetFileSizeEx(file, &size) && size.QuadPart == RTL_CW_FILE_SIZE && bytes &&
                ReadFile(file, bytes, RTL_CW_FILE_SIZE, &got, NULL) && got == RTL_CW_FILE_SIZE;
    CloseHandle(file);
    if (!read) { free(bytes); return fail("CW3220.DLL size/read"); }
    HCRYPTPROV provider = 0; HCRYPTHASH hash = 0; BYTE digest[32] = {0}; DWORD length = 32;
    int ok = CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) &&
             CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) && CryptHashData(hash, bytes, RTL_CW_FILE_SIZE, 0) &&
             CryptGetHashParam(hash, HP_HASHVAL, digest, &length, 0);
    char hex[65]; static const char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < 32; ++i) { hex[2 * i] = digits[digest[i] >> 4]; hex[2 * i + 1] = digits[digest[i] & 15]; }
    hex[64] = 0;
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    if (!ok || strcmp(hex, RTL_CW_SHA256)) { free(bytes); return fail("CW3220.DLL SHA-256 gate"); }
    uint32_t pe = get32(bytes + 0x3c);
    const BYTE *nt = bytes + pe, *opt = nt + 24;
    uint32_t link_base = get32(opt + 28), image_size = get32(opt + 56), headers = get32(opt + 60);
    if (image_size != RTL_CW_IMAGE_SIZE || headers > RTL_CW_FILE_SIZE) { free(bytes); return fail("CW3220 image size"); }
    BYTE *image = VirtualAlloc(NULL, image_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!image) { free(bytes); return fail("CW3220 mapping"); }
    memcpy(image, bytes, headers);
    const BYTE *section = opt + get16(nt + 20);
    for (unsigned i = 0; i < get16(nt + 6); ++i, section += 40) {
        uint32_t va = get32(section + 12), count = get32(section + 16), offset = get32(section + 20);
        if (!extent(va, count, image_size) || !extent(offset, count, RTL_CW_FILE_SIZE)) { free(bytes); return fail("CW3220 section bounds"); }
        if (count) memcpy(image + va, bytes + offset, count);
    }
    free(bytes);
    /* Base relocations: HIGHLOW only (type 0 = padding). */
    uint32_t delta = (uint32_t)(uintptr_t)image - link_base;
    uint32_t reloc = get32(opt + 96 + 5 * 8), reloc_size = get32(opt + 96 + 5 * 8 + 4), applied = 0;
    if (!extent(reloc, reloc_size, image_size)) return fail("CW3220 relocation directory");
    for (uint32_t at = reloc; at + 8 <= reloc + reloc_size;) {
        uint32_t page = get32(image + at), block = get32(image + at + 4);
        if (block < 8 || at + block > reloc + reloc_size) return fail("CW3220 relocation block");
        for (uint32_t k = 8; k + 2 <= block; k += 2) {
            uint16_t entry = get16(image + at + k);
            if ((entry >> 12) == 0) continue;
            if ((entry >> 12) != 3 || !extent(page + (entry & 0xfffu), 4, image_size)) return fail("CW3220 relocation entry");
            BYTE *target = image + page + (entry & 0xfffu);
            put32(target, get32(target) + delta);
            ++applied;
        }
        at += block;
    }
    /* Imports: named traps. */
    BYTE *stubs = VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!stubs) return fail("CW3220 trap allocation");
    uint32_t imports_rva = get32(opt + 96 + 8);
    for (uint32_t d = imports_rva; get32(image + d + 12); d += 20) {
        uint32_t lookup = get32(image + d), iat = get32(image + d + 16);
        if (!lookup) lookup = iat;
        for (unsigned slot = 0; get32(image + lookup + 4 * slot); ++slot) {
            if (rtl_cw_import_count >= 128) return fail("CW3220 import count");
            uint32_t entry = get32(image + lookup + 4 * slot);
            unsigned id = rtl_cw_import_count++;
            snprintf(rtl_cw_imports[id], sizeof(rtl_cw_imports[id]), "%s!%s", (const char *)image + get32(image + d + 12),
                     (entry & 0x80000000u) ? "#ordinal" : (const char *)image + entry + 2);
            BYTE *s = stubs + 16 * id;
            s[0] = 0x68; put32(s + 1, id); s[5] = 0xe8;
            put32(s + 6, (uint32_t)(uintptr_t)rtl_cw_trap - (uint32_t)(uintptr_t)(s + 10)); s[10] = 0xcc;
            put32(image + iat + 4 * slot, (uint32_t)(uintptr_t)s);
        }
    }
    DWORD previous;
    if (!VirtualProtect(stubs, 4096, PAGE_EXECUTE_READ, &previous)) return fail("CW3220 trap protection");
    section = image + pe + 24 + get16(image + pe + 20);
    for (unsigned i = 0; i < get16(image + pe + 6); ++i, section += 40)
        if (get32(section + 36) & 0x20000000u)
            if (!VirtualProtect(image + get32(section + 12), get32(section + 8), PAGE_EXECUTE_READ, &previous)) return fail("CW3220 CODE protection");
    FlushInstructionCache(GetCurrentProcess(), NULL, 0);
    rtl_cw = image;
    printf("Mapped SHA-256 verified CW3220.DLL at 0x%08x (link base 0x%08x, %u relocations, %u imports trapped); no entry point called\n",
           (unsigned)(uintptr_t)image, (unsigned)link_base, (unsigned)applied, rtl_cw_import_count);
    return 0;
}
static uint32_t rtl_cw_export(const char *name) {
    uint32_t pe = get32(rtl_cw + 0x3c), dir = get32(rtl_cw + pe + 24 + 96);
    uint32_t count = get32(rtl_cw + dir + 24), functions = get32(rtl_cw + dir + 28), names = get32(rtl_cw + dir + 32), ordinals = get32(rtl_cw + dir + 36);
    for (uint32_t i = 0; i < count; ++i)
        if (!strcmp((const char *)rtl_cw + get32(rtl_cw + names + 4 * i), name))
            return (uint32_t)(uintptr_t)rtl_cw + get32(rtl_cw + functions + 4 * get16(rtl_cw + ordinals + 2 * i));
    return 0;
}

/* ------------------------------------------------------------------------
 * Calls on a private test stack: the arguments at `esp`, the given entry
 * registers; afterwards every register, ESP and EFLAGS. */
typedef struct rtl_vitro_frame {
    uint32_t esp, target, eax, ecx, edx, ebx, esi, edi, ebp;
    uint32_t out_eax, out_ecx, out_edx, out_ebx, out_esi, out_edi, out_ebp, out_esp, out_flags;
} rtl_vitro_frame;
_Static_assert(offsetof(rtl_vitro_frame, out_eax) == 36 && offsetof(rtl_vitro_frame, out_flags) == 68, "frame offsets");
uint32_t rtl_vitro_current, rtl_vitro_host_esp, rtl_vitro_target, rtl_vitro_tmp;
__asm__(
    ".text\n"
    "_rtl_vitro_call:\n"
    "    pushl %ebp\n    pushl %ebx\n    pushl %esi\n    pushl %edi\n"
    "    movl 20(%esp), %eax\n"
    "    movl %eax, _rtl_vitro_current\n"
    "    movl %esp, _rtl_vitro_host_esp\n"
    "    movl 4(%eax), %ecx\n    movl %ecx, _rtl_vitro_target\n"
    "    movl 12(%eax), %ecx\n    movl 16(%eax), %edx\n    movl 20(%eax), %ebx\n"
    "    movl 24(%eax), %esi\n    movl 28(%eax), %edi\n    movl 32(%eax), %ebp\n"
    "    movl 0(%eax), %esp\n"
    "    movl 8(%eax), %eax\n"
    "    call *_rtl_vitro_target\n"
    "    movl %eax, _rtl_vitro_tmp\n"
    "    movl _rtl_vitro_current, %eax\n"
    "    movl %ecx, 40(%eax)\n    movl %edx, 44(%eax)\n    movl %ebx, 48(%eax)\n    movl %esi, 52(%eax)\n"
    "    movl %edi, 56(%eax)\n    movl %ebp, 60(%eax)\n    movl %esp, 64(%eax)\n"
    "    movl _rtl_vitro_host_esp, %esp\n"
    "    pushfl\n    popl %ecx\n    movl %ecx, 68(%eax)\n"
    "    movl _rtl_vitro_tmp, %ecx\n    movl %ecx, 36(%eax)\n"
    "    cld\n"
    "    popl %edi\n    popl %esi\n    popl %ebx\n    popl %ebp\n    ret\n");
extern void __cdecl rtl_vitro_call(rtl_vitro_frame *frame) __asm__("_rtl_vitro_call");

#define RTL_STACK_DWORDS 16384u
#define RTL_ARGS 16u
#define RTL_BELOW 4096u
static uint32_t rtl_stack[RTL_STACK_DWORDS] __attribute__((aligned(16)));
#define RTL_ARGS_ADDRESS ((uint32_t)(uintptr_t)(rtl_stack + RTL_STACK_DWORDS - 64u))
#define RTL_ARENA 0x10000u
static BYTE *rtl_arena, *rtl_pre, *rtl_post;
static BYTE rtl_below_post[RTL_BELOW], rtl_below_pattern[RTL_BELOW];

typedef struct rtl_case {
    uint32_t args[RTL_ARGS];
    uint32_t eax, ecx, edx;
    uint32_t dest, dest_room;      /* sprintf: for the empty-result check */
    int has_float;                 /* a floating-point conversion: the export would need imports */
} rtl_case;
typedef struct rtl_result {
    rtl_vitro_frame frame;
    uint32_t args[RTL_ARGS];
} rtl_result;

static uint32_t rtl_at(uint32_t offset) { return (uint32_t)(uintptr_t)rtl_arena + offset; }
static uint32_t rtl_below(unsigned n) { return random32() % n; }
static uint32_t rtl_pick(uint32_t size) { return rtl_below(RTL_ARENA - size); }
/* A string of `length` non-NUL bytes at `offset` from the alphabet: 0 any
 * byte, 1 "ab" letters, 2 mixed case and high bytes, 3 digits/space/sign. */
static void rtl_string(uint32_t offset, uint32_t length, unsigned alphabet) {
    static const char letters[] = "abcAB";
    static const BYTE mixed[] = {'a', 'A', 'z', 'Z', 'm', 'M', '@', '[', '`', '{', 0x80, 0xc4, 0xe4, 0xff, 0x7f, '0'};
    static const BYTE numeric[] = {'0', '1', '9', '5', ' ', '\t', '+', '-', 0x80, 0x81, 0xfe, 0xff, 'x', '7', '3', '2'};
    for (uint32_t i = 0; i < length; ++i) {
        BYTE b;
        if (alphabet == 1) b = (BYTE)letters[random32() % 5];
        else if (alphabet == 2) b = mixed[random32() % 16];
        else if (alphabet == 3) b = numeric[random32() % 16];
        else do b = (BYTE)random32(); while (!b);
        rtl_arena[offset + i] = b;
    }
    rtl_arena[offset + length] = 0;
}
static uint32_t rtl_length(unsigned n) {
    static const uint32_t edges[] = {0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33, 79, 80, 81, 255, 256};
    if (n < 40) return edges[n % 20];
    uint32_t r = random32() % 16;
    return r < 10 ? random32() % 24 : r < 15 ? random32() % 300 : random32() % 4000;
}
/* A string placed at a random offset, or so that its NUL is the arena's
 * last byte (the next page is inaccessible). */
static uint32_t rtl_place_string(uint32_t length, unsigned alphabet, unsigned n) {
    uint32_t offset = (n % 7 == 3) ? RTL_ARENA - 1u - length : rtl_pick(length + 1);
    rtl_string(offset, length, alphabet);
    return offset;
}
static uint32_t rtl_int(unsigned n) {
    static const uint32_t edges[] = {0, 1, 0xffffffffu, 0x80000000u, 0x7fffffffu, 9, 10, 11, 35, 36, 37, 100, 0xffff, 0x10000,
                                     0x8000, 0x7fff, 0xffff8000u, 12345, 0xfffffffeu, 2};
    if (n < 60) return edges[n % 20];
    uint32_t r = random32() % 4;
    return r == 0 ? random32() : r == 1 ? random32() % 1000 : r == 2 ? (uint32_t)-(int32_t)(random32() % 1000) : edges[random32() % 20];
}

/* A random format of the formatter's grammar: literal text, %%, flags,
 * widths and precisions (digits or '*'), size letters, conversions, and
 * sometimes an invalid or unsupported specification. The argument slots are
 * filled to match: small integers for '*', the string `text` (or NULL) for
 * %s, the dword `count_at` for %n, an integer edge or random value for the
 * rest. A floating-point conversion (which reads two or three slots) or %n
 * ends the generated format. Returns the number of slots. */
static unsigned rtl_format(char *out, size_t room, uint32_t *values, unsigned max_values, uint32_t text, uint32_t count_at, unsigned n, int *has_float) {
    static const char conversions[] = "diouxXcs";
    static const char literal[] = "Ab -:%,x";
    size_t at = 0;
    unsigned specs = 1 + random32() % 6, count = 0;
    for (unsigned s = 0; s < specs && at + 40 < room && count + 3 <= max_values; ++s) {
        unsigned plain = random32() % 4;
        for (unsigned k = 0; k < plain; ++k) { char c = literal[random32() % 8]; if (c == '%') { out[at++] = '%'; out[at++] = '%'; } else out[at++] = c; }
        if (random32() % 16 == 0) { for (unsigned k = 0; k < 90 && at + 40 < room; ++k) out[at++] = (char)('a' + k % 26); }
        out[at++] = '%';
        size_t spec_start = at;
        unsigned flags = random32() % 4;
        for (unsigned k = 0; k < flags; ++k) out[at++] = "-+ #0"[random32() % 5];
        unsigned width = random32() % 4;
        if (width == 1) at += (size_t)sprintf(out + at, "%u", random32() % 30);
        else if (width == 2) { out[at++] = '*'; values[count++] = (uint32_t)((int32_t)(random32() % 61) - 30); }
        unsigned precision = random32() % 4;
        if (precision == 1) at += (size_t)sprintf(out + at, ".%u", random32() % 25);
        else if (precision == 2) { out[at++] = '.'; out[at++] = '*'; values[count++] = (uint32_t)((int32_t)(random32() % 41) - 10); }
        else if (precision == 3) out[at++] = '.';
        unsigned size = random32() % 8;
        if (size == 1) out[at++] = 'l';
        else if (size == 2) out[at++] = 'h';
        else if (size == 3) out[at++] = "LNFH"[random32() % 4];
        unsigned kind = random32() % 64;
        char conversion = kind == 0 ? "fegpnE"[random32() % 6] : kind == 1 ? "%y.5!"[random32() % 5] :
                          kind == 2 ? (char)(0x80 + random32() % 0x7f) : kind == 3 ? '\t' : conversions[random32() % 8];
        out[at++] = conversion;
        if (!strchr("diouxXcspnfegE", conversion) || !conversion) {
            /* "%%" is a literal percent; anything else ends the formatting
             * (the rest of the format is copied): no slot either way. */
            if (conversion == '%' && at == spec_start + 1) continue;
            break;
        }
        uint32_t value = conversion == 's' ? (random32() % 8 ? text : 0u) : conversion == 'n' ? count_at : rtl_int(n + count);
        values[count++] = value;
        if (conversion == 'f' || conversion == 'e' || conversion == 'g' || conversion == 'E') { *has_float = 1; break; }
        if (conversion == 'n') break;
    }
    out[at] = 0;
    return count;
}

/* Two buffers that must not overlap: one in the lower half of the arena,
 * one in the upper half (or ending at the arena's last byte). */
static uint32_t rtl_low(uint32_t size) { return rtl_below(RTL_ARENA / 2u - size); }
static uint32_t rtl_high(uint32_t size, unsigned n) {
    return n % 7 == 3 ? RTL_ARENA - size : RTL_ARENA / 2u + rtl_below(RTL_ARENA / 2u - size);
}

/* One generated case of function f, case number n; fills the arena. */
static void rtl_generate(es2_rtl_function f, unsigned n, rtl_case *c) {
    memset(c, 0, sizeof(*c));
    for (uint32_t i = 0; i < RTL_ARENA; i += 4) put32(rtl_arena + i, random32());
    c->eax = random32(); c->ecx = random32(); c->edx = random32();
    for (unsigned i = 0; i < RTL_ARGS; ++i) c->args[i] = random32();
    uint32_t a, b, len, la, lb;
    switch (f) {
    case ES2_RTL_MEMSET:
        len = rtl_length(n);
        c->args[0] = rtl_at(n % 5 == 1 ? RTL_ARENA - len : rtl_pick(len)); c->args[1] = rtl_int(n); c->args[2] = len;
        break;
    case ES2_RTL_MEMCPY: case ES2_RTL_MEMMOVE: {
        len = rtl_length(n);
        a = rtl_pick(len + 16);
        b = (n % 3 == 0) ? rtl_pick(len) : a + 8u + (uint32_t)((int32_t)(random32() % 17) - 8);
        if (n % 3 == 1) b = a + 8u;                                    /* the same address */
        c->args[0] = rtl_at(a + 8u); c->args[1] = rtl_at(b); c->args[2] = len;
        break;
    }
    case ES2_RTL_MEMCMP:
        len = rtl_length(n);
        a = rtl_low(len + 3); b = rtl_high(len + 3, n);
        memmove(rtl_arena + b, rtl_arena + a, len + 3);
        if (len && random32() % 4) rtl_arena[b + random32() % len] ^= (BYTE)(1u << (random32() % 8));
        c->args[0] = rtl_at(a); c->args[1] = rtl_at(b);
        c->args[2] = n % 11 == 5 ? len + random32() % 3 : n % 13 == 7 ? 0 : len;
        break;
    case ES2_RTL_STRLEN: case ES2_RTL_STRUPR: case ES2_RTL_STRLWR:
        c->args[0] = rtl_at(rtl_place_string(rtl_length(n), n % 2 ? 2 : 0, n));
        break;
    case ES2_RTL_STRCAT: case ES2_RTL_STRNCAT:
        la = rtl_length(n) % 300; lb = rtl_length(n + 7) % 300;
        a = rtl_low(2 * la + lb + 8);
        rtl_string(a, la, 0);
        if (n % 9 == 4) b = a + random32() % (la + 1);                 /* the source inside the destination */
        else { b = rtl_high(lb + 1, n); rtl_string(b, lb, 0); }
        c->args[0] = rtl_at(a); c->args[1] = rtl_at(b);
        c->args[2] = f == ES2_RTL_STRCAT ? random32() : n % 4 == 0 ? 0xffffffffu : rtl_below(lb + 4);
        break;
    case ES2_RTL_STRNCPY:
        lb = rtl_length(n) % 300;
        len = n % 5 == 0 ? lb : rtl_below(lb + 40);
        if (n % 8 == 2) { b = rtl_low(lb + len + 2); a = b + 1; rtl_string(b, lb, 0); }   /* overlapping */
        else { b = rtl_high(lb + 1, n); rtl_string(b, lb, 0); a = rtl_low(len + 1); }
        c->args[0] = rtl_at(a); c->args[1] = rtl_at(b); c->args[2] = len;
        break;
    case ES2_RTL_STRCHR: case ES2_RTL_STRRCHR: {
        a = rtl_place_string(rtl_length(n) % 300, 1, n);
        static const uint32_t chars[] = {'a', 'b', 'c', 'A', 0, 0x161, 0xffffff62u, 'z', 0x100, 0xff};
        c->args[0] = rtl_at(a); c->args[1] = n % 3 ? chars[random32() % 10] : random32();
        break;
    }
    case ES2_RTL_STRSTR:
        la = random32() % 24; lb = random32() % 5;
        if (n < 25) { la = n / 5; lb = n % 5; }
        a = rtl_low(la + 1); rtl_string(a, la, 1);
        b = rtl_high(lb + 1, n); rtl_string(b, lb, 1);
        if (lb && la >= lb && random32() % 2) memcpy(rtl_arena + b, rtl_arena + a + random32() % (la - lb + 1), lb);
        c->args[0] = rtl_at(a); c->args[1] = rtl_at(b);
        break;
    case ES2_RTL_STRNCMP: case ES2_RTL_STRICMP: case ES2_RTL_STRNICMP:
        la = rtl_length(n) % 64;
        lb = random32() % 3 ? la : random32() % 64;
        a = rtl_low(la + 1); rtl_string(a, la, f == ES2_RTL_STRNCMP ? 1 : 2);
        b = rtl_high(lb + 1, n); rtl_string(b, lb, f == ES2_RTL_STRNCMP ? 1 : 2);
        for (uint32_t i = 0; i < la && i < lb; ++i) if (random32() % 8) {
            BYTE x = rtl_arena[a + i];
            if (f != ES2_RTL_STRNCMP && random32() % 2) x = (BYTE)((x >= 'a' && x <= 'z') ? x - 32 : (x >= 'A' && x <= 'Z') ? x + 32 : x);
            rtl_arena[b + i] = x;
        }
        c->args[0] = rtl_at(a); c->args[1] = rtl_at(b);
        c->args[2] = n % 6 == 0 ? 0 : n % 6 == 1 ? 0xffffffffu : rtl_below(la + 4);
        break;
    case ES2_RTL_ITOA: {
        static const uint32_t radices[] = {10, 16, 8, 2, 36, 1, 0, 37, 0xffffffffu, 3, 7, 35, 10, 16, 0x80000002u};
        a = n % 5 == 1 ? RTL_ARENA - 33u : rtl_pick(40);
        c->args[0] = rtl_int(n); c->args[1] = rtl_at(a);
        c->args[2] = n < 300 ? radices[(n / 20) % 15] : random32() % 3 ? radices[random32() % 15] : random32() % 40;
        break;
    }
    case ES2_RTL_ATOL:
        len = random32() % 24;
        if (n % 5 == 0) len = 10 + random32() % 20;
        a = rtl_place_string(len, 3, n);
        if (n % 5 == 0) for (uint32_t i = 0; i < len; ++i) rtl_arena[a + i] = (BYTE)('0' + random32() % 10);
        if (n < 256 && len) rtl_arena[a] = (BYTE)(n ? n : 1);         /* every leading byte */
        c->args[0] = rtl_at(a);
        break;
    case ES2_RTL_SPRINTF: case ES2_RTL_VSPRINTF: {
        /* Layout: format 0x1000.., the %s string 0x2000.. (or at the arena
         * end), the destination 0x6000.. with 8 KiB of room, vsprintf's
         * slots 0xa000.., the %n target 0xb000... */
        char format[700];
        uint32_t values[12];
        uint32_t sl = random32() % 40;
        uint32_t sa = n % 7 == 3 ? RTL_ARENA - 1u - sl : 0x2000u + rtl_below(0x1000);
        rtl_string(sa, sl, 0);
        for (unsigned i = 0; i < 12; ++i) values[i] = random32();      /* the second half of a double */
        unsigned slots_used = rtl_format(format, sizeof(format), values, 12, rtl_at(sa), rtl_at(0xb000u + rtl_below(0x100)), n, &c->has_float);
        if (getenv("ES2_RTL_TRACE")) { fprintf(stderr, "gen %u slots %u:", n, slots_used); for (unsigned i = 0; i < 12; ++i) fprintf(stderr, " %08x", values[i]); fprintf(stderr, "\n"); }
        if (n % 50 == 1) { strcpy(format, ""); c->has_float = 0; }
        if (n % 50 == 2) { strcpy(format, "%s"); values[0] = n % 100 == 2 ? 0u : rtl_at(sa); c->has_float = 0; }
        if (n % 50 == 3) {
            c->has_float = 0;
            strcpy(format, "%5.0d|%.0x|%-+5d|%#o|%#5o|%#.3x|%#08X|%08.3d|% 05d|%+.0d|%c%c");
            for (unsigned i = 0; i < 12; ++i) values[i] = i % 3 == 0 ? 0u : rtl_int(n + i);
        }
        uint32_t fa = 0x1000u + rtl_below(0x800);
        memcpy(rtl_arena + fa, format, strlen(format) + 1);
        c->dest = 0x6000u + rtl_below(0x100); c->dest_room = 8192;
        c->args[0] = rtl_at(c->dest); c->args[1] = rtl_at(fa);
        if (f == ES2_RTL_SPRINTF) for (unsigned i = 0; i < 12; ++i) c->args[2 + i] = values[i];
        else {
            uint32_t ap = 0xa000u + rtl_below(0x100);
            for (unsigned i = 0; i < 12; ++i) put32(rtl_arena + ap + 4 * i, values[i]);
            c->args[2] = rtl_at(ap);
        }
        break;
    }
    default:
        break;
    }
}

static void rtl_frame_setup(rtl_vitro_frame *fr, uint32_t target, const rtl_case *c) {
    memset(fr, 0, sizeof(*fr));
    memcpy((void *)(uintptr_t)RTL_ARGS_ADDRESS, c->args, sizeof(c->args));
    memcpy((BYTE *)(uintptr_t)(RTL_ARGS_ADDRESS - RTL_BELOW), rtl_below_pattern, RTL_BELOW);
    fr->esp = RTL_ARGS_ADDRESS; fr->target = target;
    fr->eax = c->eax; fr->ecx = c->ecx; fr->edx = c->edx;
    fr->ebx = 0xb0b0b0b0u; fr->esi = 0x51515151u; fr->edi = 0xd1d1d1d1u; fr->ebp = 0xe0e0e0e0u;
}
/* The export (or a DBSIM thunk) on the case from the saved pre-state:
 * registers, then the arena, argument slots and stack below in place. */
static int rtl_run_target(uint32_t target, const rtl_case *c, rtl_result *out) {
    memcpy(rtl_arena, rtl_pre, RTL_ARENA);
    rtl_frame_setup(&out->frame, target, c);
    rtl_vitro_call(&out->frame);
    memcpy(out->args, (const void *)(uintptr_t)RTL_ARGS_ADDRESS, sizeof(out->args));
    memcpy(rtl_below_post, (const void *)(uintptr_t)(RTL_ARGS_ADDRESS - RTL_BELOW), RTL_BELOW);
    const rtl_vitro_frame *fr = &out->frame;
    if (fr->out_ebx != 0xb0b0b0b0u || fr->out_esi != 0x51515151u || fr->out_edi != 0xd1d1d1d1u || fr->out_ebp != 0xe0e0e0e0u ||
        fr->out_esp != RTL_ARGS_ADDRESS || (fr->out_flags & 0x400u)) return fail("callee-saved registers, ESP or DF");
    return 0;
}
static uint8_t rtl_identity_read(void *context, uint32_t address) { (void)context; return *(const BYTE *)(uintptr_t)address; }
static void rtl_identity_write(void *context, uint32_t address, uint8_t value) { (void)context; *(BYTE *)(uintptr_t)address = value; }
static const es2_rtl_memory rtl_identity = {NULL, rtl_identity_read, rtl_identity_write};
/* A format the own code may decline: it contains a conversion letter
 * outside the subset (floating point, p, n). */
static int rtl_format_outside(uint32_t offset) {
    for (const BYTE *p = rtl_pre + offset; *p; ++p)
        if (*p == 'p' || *p == 'n') return 1;
    return 0;
}

typedef struct rtl_tally { unsigned direct, declined, installed[3]; } rtl_tally;
static rtl_tally rtl_tallies[ES2_RTL_FUNCTION_COUNT];

/* Direct case: export vs own code from the same state. A format with a
 * floating-point conversion is not run on the export when the own code
 * declines it (its float formatting calls CW3220 imports, which are traps
 * here); the own code must then have written nothing. */
static int rtl_direct(es2_rtl_function f, uint32_t export_address, const rtl_case *c) {
    rtl_result want;
    memcpy(rtl_pre, rtl_arena, RTL_ARENA);
    if (c->has_float) {
        uint32_t *slots = (uint32_t *)(uintptr_t)RTL_ARGS_ADDRESS;
        memcpy(slots, c->args, sizeof(c->args));
        es2_rtl_regs r = {c->eax, c->ecx, c->edx};
        int status = es2_rtl_call(f, &rtl_identity, RTL_ARGS_ADDRESS, &r);
        BOOL untouched = !memcmp(rtl_arena, rtl_pre, RTL_ARENA) && !memcmp(slots, c->args, sizeof(c->args));
        memcpy(rtl_arena, rtl_pre, RTL_ARENA);
        if (status == ES2_RTL_UNSUPPORTED && untouched) { ++rtl_tallies[f].declined; return 0; }
        /* Formatting ended before the conversion (an invalid specification): an ordinary case. */
    }
    if (rtl_run_target(export_address, c, &want)) return 1;
    memcpy(rtl_post, rtl_arena, RTL_ARENA);
    memcpy(rtl_arena, rtl_pre, RTL_ARENA);
    uint32_t *slots = (uint32_t *)(uintptr_t)RTL_ARGS_ADDRESS;
    memcpy(slots, c->args, sizeof(c->args));
    es2_rtl_regs r = {c->eax, c->ecx, c->edx};
    int status = es2_rtl_call(f, &rtl_identity, RTL_ARGS_ADDRESS, &r);
    if (status == ES2_RTL_UNSUPPORTED) {
        if ((f != ES2_RTL_SPRINTF && f != ES2_RTL_VSPRINTF) || (want.frame.out_eax != 0 && !rtl_format_outside(c->args[1] - (uint32_t)(uintptr_t)rtl_arena)))
            return fail("own code declined a supported call");
        /* Nothing written but dest[0] = 0 (which the export writes first). */
        if (rtl_arena[c->dest] != 0 && rtl_arena[c->dest] != rtl_pre[c->dest]) return fail("declined call: dest[0]");
        rtl_arena[c->dest] = rtl_pre[c->dest];
        if (memcmp(rtl_arena, rtl_pre, RTL_ARENA) || memcmp(slots, c->args, sizeof(c->args))) return fail("declined call wrote memory");
        ++rtl_tallies[f].declined;
        return 0;
    }
    if (r.eax != want.frame.out_eax || r.ecx != want.frame.out_ecx || r.edx != want.frame.out_edx) {
        fprintf(stderr, "FAIL: %s case %u: EAX/ECX/EDX original %08x %08x %08x own %08x %08x %08x args %08x %08x %08x\n",
                current_function, current_case, want.frame.out_eax, want.frame.out_ecx, want.frame.out_edx, r.eax, r.ecx, r.edx,
                c->args[0], c->args[1], c->args[2]);
        return 1;
    }
    for (uint32_t i = 0; i < RTL_ARENA; ++i) if (rtl_arena[i] != rtl_post[i]) {
        fprintf(stderr, "FAIL: %s case %u: arena byte +0x%x original %02x own %02x (before %02x)\n",
                current_function, current_case, (unsigned)i, rtl_post[i], rtl_arena[i], rtl_pre[i]);
        return 1;
    }
    if (memcmp(slots, want.args, sizeof(want.args))) return fail("argument slots differ");
    ++rtl_tallies[f].direct;
    return 0;
}

/* Installed case: the same call through DBSIM's thunk with the shim in
 * `mode`, against the direct original call from the same state. */
static int rtl_installed_case(es2_rtl_function f, unsigned import, uint32_t export_address, const rtl_case *c, es2_hook_mode mode) {
    rtl_result want, got;
    memcpy(rtl_pre, rtl_arena, RTL_ARENA);
    if (rtl_run_target(export_address, c, &want)) return 1;
    memcpy(rtl_post, rtl_arena, RTL_ARENA);
    static BYTE below_want[RTL_BELOW];
    memcpy(below_want, rtl_below_post, RTL_BELOW);
    if (rtl_run_target((uint32_t)(uintptr_t)mapped + RTL_THUNK_RVA + 6u * import, c, &got)) return 1;
    if (got.frame.out_eax != want.frame.out_eax || got.frame.out_ecx != want.frame.out_ecx || got.frame.out_edx != want.frame.out_edx) {
        fprintf(stderr, "FAIL: %s case %u: installed mode %u EAX/ECX/EDX original %08x %08x %08x got %08x %08x %08x\n", current_function,
                current_case, (unsigned)mode, want.frame.out_eax, want.frame.out_ecx, want.frame.out_edx, got.frame.out_eax, got.frame.out_ecx, got.frame.out_edx);
        return 1;
    }
    if (memcmp(rtl_arena, rtl_post, RTL_ARENA)) return fail("installed: arena differs");
    if (memcmp(got.args, want.args, sizeof(want.args))) return fail("installed: argument slots differ");
    /* Below the arguments: the return-address slot always; for the leaf
     * functions (and in compare mode, where the export itself runs) all of it. */
    size_t compare = (mode == ES2_HOOK_COMPARE || rtl_residue(f)) ? RTL_BELOW : 4u;
    if (memcmp(rtl_below_post + RTL_BELOW - compare, below_want + RTL_BELOW - compare, compare)) return fail("installed: stack below the arguments differs");
    ++rtl_tallies[f].installed[mode];
    return 0;
}

static FARPROC rtl_cw_resolve(const char *symbol, void *context) { (void)context; return (FARPROC)(uintptr_t)rtl_cw_export(symbol); }
static unsigned rtl_import_of(es2_rtl_function f) {
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) if (!strcmp(rtl_imports[i].symbol, es2_rtl_name(f))) return i;
    return RTL_IMPORT_COUNT;
}
static int rtl_install_mode(es2_hook_mode mode) {
    rtl_options o;
    memset(&o, 0, sizeof(o));
    o.image = mapped; o.expected_base = 0x00400000u; o.image_size = RTL_IMAGE_SIZE;
    o.resolve = rtl_cw_resolve;
    o.stack_lo = (uintptr_t)rtl_stack; o.stack_hi = (uintptr_t)(rtl_stack + RTL_STACK_DWORDS);
    if (!rtl_module_range((const void *)rtl_install_mode, &o.hook_lo, &o.hook_hi)) return fail("harness module range");
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) {
        o.selected[i] = rtl_imports[i].shim;
        o.modes[i] = rtl_own_function(i) >= 0 ? mode : ES2_HOOK_ORIGINAL;
    }
    if (!rtl_install(&o)) { fprintf(stderr, "FAIL: rtl shim refused: %s\n", rtl_reason); return 1; }
    return 0;
}

/* Every function (and, in the installed phase, every mode) draws from its own stream, so that Bazel
 * shards can split the functions (shard f % shard_count) and a sharded run executes exactly the cases of
 * an unsharded one. A function's cases do not depend on the functions that ran before it. */
static void rtl_reseed(unsigned stream) {
    random_state = 0x48c06e21u + 0x9e3779b9u * (stream + 1u);
    if (!random_state) random_state = 1u;
}
static int rtl_function_in_shard(int f) { return (unsigned)f % shard_count == shard_index; }
/* Selection: --rtl (every function; with Bazel shards the functions f % shard_count == shard_index) or
 * rtl_<function> (mutation runs). */
static int rtl_import_cases(const char *selection) {
    const char *root = getenv("ES2_DATA_ROOT");
    current_function = "rtl";
    if (rtl_map_cw3220(root)) return 1;
    BYTE *arena = VirtualAlloc(NULL, RTL_ARENA + 4096u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    DWORD previous;
    if (!arena || !VirtualProtect(arena + RTL_ARENA, 4096, PAGE_NOACCESS, &previous)) return fail("arena");
    rtl_arena = arena; rtl_pre = malloc(RTL_ARENA); rtl_post = malloc(RTL_ARENA);
    if (!rtl_pre || !rtl_post) return fail("arena copies");
    for (unsigned i = 0; i < RTL_BELOW; ++i) rtl_below_pattern[i] = (BYTE)random32();
    static char names[ES2_RTL_FUNCTION_COUNT][24];
    int chosen = -1;
    for (int f = 0; f < ES2_RTL_FUNCTION_COUNT; ++f) {
        snprintf(names[f], sizeof(names[f]), "rtl%s", es2_rtl_name((es2_rtl_function)f));   /* rtl_memset */
        if (!strcmp(selection, names[f])) chosen = f;
    }
    if (strcmp(selection, "--rtl") && chosen < 0) return fail("unknown rtl selection");
    if (shard_count > ES2_RTL_FUNCTION_COUNT) return fail("more Bazel shards than rtl functions");
    uint32_t exports[ES2_RTL_FUNCTION_COUNT];
    for (int f = 0; f < ES2_RTL_FUNCTION_COUNT; ++f) {
        exports[f] = rtl_cw_export(es2_rtl_name((es2_rtl_function)f));
        if (!exports[f] || rtl_import_of((es2_rtl_function)f) == RTL_IMPORT_COUNT) return fail("export or import missing");
    }
    unsigned direct = quick_tier ? 20000u : 200000u, installed = quick_tier ? 2000u : 20000u;
    /* Direct cases. */
    for (int f = 0; f < ES2_RTL_FUNCTION_COUNT; ++f) {
        if (chosen >= 0 ? f != chosen : !rtl_function_in_shard(f)) continue;
        current_function = names[f];
        rtl_reseed((unsigned)f);
        for (unsigned n = 0; n < direct; ++n) {
            rtl_case c;
            current_case = n; current_case_valid = 1;
            rtl_generate((es2_rtl_function)f, n, &c);
            if (getenv("ES2_RTL_TRACE") && (f == ES2_RTL_SPRINTF || f == ES2_RTL_VSPRINTF)) {
                fprintf(stderr, "case %u: format \"%s\" args", n, (const char *)(uintptr_t)c.args[1]);
                for (unsigned i = 0; i < RTL_ARGS; ++i) fprintf(stderr, " %08x", c.args[i]);
                fprintf(stderr, " arena %08x\n", (unsigned)(uintptr_t)rtl_arena);
            }
            if (rtl_direct((es2_rtl_function)f, exports[f], &c)) return 1;
        }
        current_case_valid = 0;
    }
    /* Installed cases: the CW3220 slots of the mapped DBSIM point to the
     * mapped exports while the shim is installed. */
    uint32_t saved[RTL_IMPORT_COUNT];
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) {
        uint32_t *slot = (uint32_t *)(mapped + RTL_IAT_RVA + 4 * i);
        saved[i] = *slot;
        *slot = rtl_cw_export(rtl_imports[i].symbol);
        if (!*slot) return fail("CW3220 export of an import missing");
    }
    for (es2_hook_mode mode = ES2_HOOK_REPLACED; mode <= ES2_HOOK_COMPARE; mode = (es2_hook_mode)(mode + 1)) {
        current_function = "rtl";
        if (rtl_install_mode(mode)) return 1;
        for (int f = 0; f < ES2_RTL_FUNCTION_COUNT; ++f) {
            if (chosen >= 0 ? f != chosen : !rtl_function_in_shard(f)) continue;
            current_function = names[f];
            rtl_reseed(64u + 32u * (unsigned)mode + (unsigned)f);
            unsigned import = rtl_import_of((es2_rtl_function)f);
            for (unsigned n = 0; n < installed; ++n) {
                rtl_case c;
                current_case = n; current_case_valid = 1;
                do rtl_generate((es2_rtl_function)f, n, &c); while (c.has_float);   /* the export would trap */
                if (rtl_installed_case((es2_rtl_function)f, import, exports[f], &c, mode)) return 1;
            }
            current_case_valid = 0;
            const rtl_counter *k = &rtl_counters[import];
            printf("COUNTERS %-14s mode=%s calls=%llu hook=%llu mismatches=%llu skipped=%llu\n", names[f],
                   mode == ES2_HOOK_COMPARE ? "compare" : "replaced", (unsigned long long)k->calls, (unsigned long long)k->hook,
                   (unsigned long long)k->mismatches, (unsigned long long)rtl_skipped[import]);
            if (k->calls != installed || k->hook != installed) return fail("installed: thunk counters");
            if (k->mismatches) { fprintf(stderr, "first mismatch: %s\n", rtl_mismatch_detail); return fail("installed: compare mismatch"); }
        }
        if (rtl_foreign_calls) return fail("installed: foreign calls");
        if (!rtl_uninstall()) return fail("rtl shim uninstall");
    }
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) *(uint32_t *)(mapped + RTL_IAT_RVA + 4 * i) = saved[i];
    for (int f = 0; f < ES2_RTL_FUNCTION_COUNT; ++f) {
        if (chosen >= 0 ? f != chosen : !rtl_function_in_shard(f)) continue;
        const rtl_tally *t = &rtl_tallies[f];
        printf("PASS: %-14s %u direct cases (%u declined, original count 0 or a format outside the subset); "
               "installed %u replaced, %u compare: EAX/ECX/EDX, arena, argument slots%s equal\n",
               names[f], t->direct + t->declined, t->declined, t->installed[ES2_HOOK_REPLACED], t->installed[ES2_HOOK_COMPARE],
               rtl_residue(f) ? ", stack below" : ", return slot");
    }
    puts("PASS: rtl in-vitro run against the mapped CW3220.DLL; no entry point or import of it executed; no game process started");
    return 0;
}
