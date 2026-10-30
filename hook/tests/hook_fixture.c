/* Executable fixtures consist solely of our synthetic PE and own x86 stubs. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "hook/hook.h"

enum { IMAGE_BYTES = 0x4000, SITE_RVA = 0x1000, INNER_RVA = 0x1100 };
static const BYTE prologue[] = {0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08};
static const BYTE lengths[] = {1, 2, 3};
/* cdecl(state, increment): add increment to state[0], return state[0]. */
static const BYTE stub[] = {
    0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08,
    0x8b, 0x4d, 0x0c, 0x01, 0x08, 0x8b, 0x00, 0x5d, 0xc3
};

typedef struct state_data { uint32_t value, guard; } state_data;
typedef uint32_t (__cdecl *stub_function)(state_data *, uint32_t);
typedef struct candidate_data {
    unsigned calls, kind;
    es2_hook *during_call;
    unsigned busy_refused;
} candidate_data;

static unsigned checks;
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, "FAIL line %d: %s (Win32 %lu)\n", __LINE__, #c, (unsigned long)GetLastError()); return 1; } } while (0)

static unsigned protect_calls, flush_calls, fail_protect_at, fail_flush_at;
static BOOL persistent_flush_failure;

static BOOL WINAPI injected_protect(LPVOID address, SIZE_T bytes, DWORD access, PDWORD previous)
{
    ++protect_calls;
    if (protect_calls == fail_protect_at) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    return VirtualProtect(address, bytes, access, previous);
}

static BOOL WINAPI injected_flush(HANDLE process, LPCVOID address, SIZE_T bytes)
{
    ++flush_calls;
    if (flush_calls == fail_flush_at || persistent_flush_failure) {
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    return FlushInstructionCache(process, address, bytes);
}

static uint32_t __cdecl candidate(const uint32_t args[ES2_HOOK_MAX_ARGS], void *opaque)
{
    candidate_data *data = opaque;
    state_data *state = (state_data *)(uintptr_t)args[0];
    ++data->calls;
    if (data->during_call && !es2_hook_uninstall(data->during_call) &&
        data->during_call->error == ES2_HOOK_ERROR_BUSY) ++data->busy_refused;
    state->value += args[1] + (data->kind == 1 ? 1u : 0u);
    if (data->kind == 3) state->guard ^= 1u;
    return state->value + (data->kind == 2 ? 1u : 0u);
}

static BOOL __cdecl frame_number(void *opaque, uint32_t *frame)
{
    *frame = *(const uint32_t *)opaque;
    return TRUE;
}

static void put32(BYTE *p, uint32_t word)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (BYTE)(word >> (8 * i));
}

static BYTE *make_image(BOOL nested)
{
    BYTE *image = VirtualAlloc(NULL, IMAGE_BYTES, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!image) return NULL;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)image;
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(image + 0x80);
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 0x80;
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    nt->FileHeader.SizeOfOptionalHeader = sizeof(nt->OptionalHeader);
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    nt->OptionalHeader.ImageBase = (DWORD)(uintptr_t)image;
    nt->OptionalHeader.SizeOfImage = IMAGE_BYTES;
    memcpy(image + SITE_RVA, stub, sizeof(stub));
    memcpy(image + INNER_RVA, stub, sizeof(stub));
    if (nested) {
        /* Forward both cdecl arguments to the second own function. */
        const BYTE outer_tail[] = {
            0xff, 0x75, 0x0c, 0xff, 0x75, 0x08,
            0xe8, 0, 0, 0, 0, 0x83, 0xc4, 0x08, 0x5d, 0xc3
        };
        memcpy(image + SITE_RVA + sizeof(prologue), outer_tail, sizeof(outer_tail));
        BYTE *call = image + SITE_RVA + sizeof(prologue) + 6;
        put32(call + 1, (uint32_t)(uintptr_t)(image + INNER_RVA) - (uint32_t)(uintptr_t)(call + 5));
    }
    DWORD previous;
    if (!VirtualProtect(image + 0x1000, 0x2000, PAGE_EXECUTE_READ, &previous) ||
        !FlushInstructionCache(GetCurrentProcess(), image, IMAGE_BYTES)) {
        VirtualFree(image, 0, MEM_RELEASE);
        return NULL;
    }
    return image;
}

static DWORD page_access(const void *site)
{
    MEMORY_BASIC_INFORMATION region;
    return VirtualQuery(site, &region, sizeof(region)) ? region.Protect : 0;
}

static es2_hook_descriptor descriptor(candidate_data *candidate_state, const es2_hook_span *span)
{
    es2_hook_descriptor d = {0};
    d.name = "own_add";
    d.rva = SITE_RVA;
    d.expected = prologue;
    d.expected_size = sizeof(prologue);
    d.relocate_size = sizeof(prologue);
    d.instruction_lengths = lengths;
    d.instruction_count = sizeof(lengths);
    d.arg_count = 2;
    d.replacement = candidate;
    d.user = candidate_state;
    d.spans = span;
    d.span_count = 1;
    return d;
}

static es2_hook_options options(BYTE *image, es2_hook_mode mode)
{
    es2_hook_options o = {0};
    o.image = image;
    o.expected_base = (uintptr_t)image;
    o.expected_image_size = IMAGE_BYTES;
    o.mode = mode;
    return o;
}

/* Check callee-saved registers, stack cleanup, EBP and EAX across the bridge.
 * Only register operands cross this asm block; the compiler's frame is saved. */
static BOOL invoke_abi(stub_function function, state_data *state, uint32_t amount, uint32_t expected)
{
    uint32_t function_word = (uint32_t)(uintptr_t)function;
    uint32_t state_word = (uint32_t)(uintptr_t)state;
    __asm__ volatile (
        "push %%ebx\n\tpush %%esi\n\tpush %%edi\n\tpush %%ebp\n\t"
        "mov %%esp, %%esi\n\tmov $0x13572468, %%ebx\n\t"
        "mov $0x24681357, %%edi\n\tmov $0x31415926, %%ebp\n\t"
        "push %%ecx\n\tpush %%edx\n\tcall *%%eax\n\tadd $8, %%esp\n\t"
        "xor %%edx, %%edx\n\tcmp %%esi, %%esp\n\tjne 1f\n\t"
        "cmp $0x13572468, %%ebx\n\tjne 1f\n\t"
        "cmp $0x24681357, %%edi\n\tjne 1f\n\t"
        "cmp $0x31415926, %%ebp\n\tjne 1f\n\tmov $1, %%edx\n"
        "1:\n\tpop %%ebp\n\tpop %%edi\n\tpop %%esi\n\tpop %%ebx"
        : "+a"(function_word), "+d"(state_word), "+c"(amount) : : "memory", "cc");
    return state_word == 1 && function_word == expected;
}

static int case_valid(BOOL test_busy)
{
    BYTE *image = make_image(FALSE);
    CHECK(image != NULL);
    candidate_data data = {0};
    const es2_hook_span span = {0, 0, sizeof(state_data)};
    es2_hook_descriptor d = descriptor(&data, &span);
    es2_hook_options o = options(image, ES2_HOOK_REPLACED);
    es2_hook hook = {0};
    state_data state = {10, 0xaabbccdd};
    CHECK(invoke_abi((stub_function)(image + SITE_RVA), &state, 3, 13));
    CHECK(es2_hook_install(&hook, &d, &o));
    CHECK(hook.installed == 1 && hook.code && hook.trampoline);
    CHECK(image[SITE_RVA] == 0xe9 && image[SITE_RVA + 5] == 0x90);
    CHECK(!memcmp(hook.trampoline, prologue, sizeof(prologue)));
    CHECK(page_access(image + SITE_RVA) == PAGE_EXECUTE_READ);
    CHECK(page_access(hook.code) == PAGE_EXECUTE_READ);
    if (test_busy) data.during_call = &hook;
    CHECK(invoke_abi((stub_function)(image + SITE_RVA), &state, 4, 17));
    CHECK(data.calls == 1 && hook.calls == 1 && hook.mismatches == 0);
    if (test_busy) {
        CHECK(data.busy_refused == 1 && hook.error == ES2_HOOK_ERROR_BUSY && hook.installed == 1);
        CHECK(!memcmp(image + SITE_RVA, hook.patch, sizeof(prologue)));
    }
    CHECK(invoke_abi((stub_function)hook.trampoline, &state, 5, 22));
    CHECK(data.calls == 1 && hook.calls == 1 && state.guard == 0xaabbccdd);
    CHECK(!es2_hook_install(&hook, &d, &o) && hook.error == ES2_HOOK_ERROR_BUSY);
    CHECK(es2_hook_uninstall(&hook));
    CHECK(!hook.code && !hook.trampoline && !hook.installed);
    CHECK(!memcmp(image + SITE_RVA, stub, sizeof(stub)));
    CHECK(page_access(image + SITE_RVA) == PAGE_EXECUTE_READ);
    CHECK(invoke_abi((stub_function)(image + SITE_RVA), &state, 6, 28));
    CHECK(data.calls == 1 && hook.calls == 1);
    CHECK(es2_hook_uninstall(&hook));
    CHECK(VirtualFree(image, 0, MEM_RELEASE));
    return 0;
}

static int case_original(void)
{
    BYTE *image = make_image(FALSE);
    CHECK(image != NULL);
    candidate_data data = {.kind = 1};
    const es2_hook_span span = {0, 0, sizeof(state_data)};
    es2_hook_descriptor d = descriptor(&data, &span);
    es2_hook_options o = options(image, ES2_HOOK_ORIGINAL);
    es2_hook hook = {0};
    CHECK(es2_hook_install(&hook, &d, &o));
    CHECK(!hook.installed && !hook.code && !hook.trampoline);
    CHECK(!memcmp(image + SITE_RVA, stub, sizeof(stub)));
    state_data state = {7, 123};
    CHECK(invoke_abi((stub_function)(image + SITE_RVA), &state, 2, 9));
    CHECK(!data.calls && !hook.calls && state.guard == 123);
    CHECK(page_access(image + SITE_RVA) == PAGE_EXECUTE_READ);
    CHECK(es2_hook_uninstall(&hook));
    /* Disabled mode does not even read a malformed/unmapped image. */
    memset(&hook, 0, sizeof(hook));
    o.image = NULL; o.expected_base = 0; o.expected_image_size = 0;
    CHECK(es2_hook_install(&hook, &d, &o));
    CHECK(!hook.code && !hook.installed);
    CHECK(VirtualFree(image, 0, MEM_RELEASE));
    return 0;
}

static int case_refusal(const WCHAR *mode)
{
    BYTE *image = make_image(FALSE);
    CHECK(image != NULL);
    candidate_data data = {0};
    const es2_hook_span span = {0, 0, sizeof(state_data)};
    es2_hook_descriptor d = descriptor(&data, &span);
    es2_hook_options o = options(image, ES2_HOOK_REPLACED);
    es2_hook hook = {0};
    es2_hook_error expected = ES2_HOOK_ERROR_IMAGE;
    BYTE wrong[sizeof(prologue)];
    memcpy(wrong, prologue, sizeof(wrong));
    if (!wcscmp(mode, L"image-base")) o.expected_base += 0x10000;
    else if (!wcscmp(mode, L"image-size")) o.expected_image_size -= 0x1000;
    else if (!wcscmp(mode, L"signature")) { wrong[5] ^= 4; d.expected = wrong; expected = ES2_HOOK_ERROR_SIGNATURE; }
    else if (!wcscmp(mode, L"relative")) {
        static const BYTE branch[] = {0xe8, 0, 0, 0, 0, 0x90};
        static const BYTE branch_lengths[] = {5, 1};
        d.expected = branch; d.instruction_lengths = branch_lengths; d.instruction_count = 2;
        expected = ES2_HOOK_ERROR_INSTRUCTION;
    } else return 1;
    CHECK(!es2_hook_install(&hook, &d, &o));
    CHECK(hook.error == expected && !hook.installed && !hook.code);
    CHECK(!memcmp(image + SITE_RVA, stub, sizeof(stub)));
    CHECK(page_access(image + SITE_RVA) == PAGE_EXECUTE_READ);
    CHECK(!data.calls && !hook.calls);
    CHECK(VirtualFree(image, 0, MEM_RELEASE));
    return 0;
}

static uint32_t __cdecl ret_replacement(const uint32_t args[ES2_HOOK_MAX_ARGS], void *opaque)
{
    (void)args;
    ++*(unsigned *)opaque;
    return 0x1234u;
}

/* A whole-body hook: the function is exactly the relocated five bytes
 * `push ebp; mov ebp,esp; pop ebp; ret` (DBSIM's empty slot methods), with
 * int3 bytes behind it. The original returns EAX unchanged, the replaced
 * path answers 0x1234, the trampoline is the original again. */
static int case_ret_body(void)
{
    static const BYTE body[] = {0x55,0x8b,0xec,0x5d,0xc3};
    static const BYTE body_lengths[] = {1,2,1,1};
    BYTE *image = make_image(FALSE);
    CHECK(image != NULL);
    DWORD previous;
    CHECK(VirtualProtect(image + SITE_RVA, 0x1000, PAGE_READWRITE, &previous));
    memcpy(image + SITE_RVA, body, sizeof(body));
    memset(image + SITE_RVA + sizeof(body), 0xcc, sizeof(stub) - sizeof(body));
    CHECK(VirtualProtect(image + SITE_RVA, 0x1000, PAGE_EXECUTE_READ, &previous));
    unsigned calls = 0;
    es2_hook_descriptor d = {0};
    d.name = "own_ret_body";
    d.rva = SITE_RVA;
    d.expected = body;
    d.expected_size = sizeof(body);
    d.relocate_size = sizeof(body);
    d.instruction_lengths = body_lengths;
    d.instruction_count = sizeof(body_lengths);
    d.arg_count = 0;
    d.replacement = ret_replacement;
    d.user = &calls;
    es2_hook_options o = options(image, ES2_HOOK_REPLACED);
    es2_hook hook = {0};
    state_data state = {0, 0};
    stub_function site = (stub_function)(image + SITE_RVA);
    CHECK(invoke_abi(site, &state, 0, (uint32_t)(uintptr_t)site));
    CHECK(es2_hook_install(&hook, &d, &o));
    CHECK(hook.installed == 1 && image[SITE_RVA] == 0xe9 && !memcmp(hook.trampoline, body, sizeof(body)));
    CHECK(invoke_abi(site, &state, 0, 0x1234u) && calls == 1 && hook.calls == 1);
    CHECK(invoke_abi((stub_function)hook.trampoline, &state, 0, (uint32_t)(uintptr_t)hook.trampoline));
    CHECK(calls == 1);
    CHECK(es2_hook_uninstall(&hook));
    CHECK(!memcmp(image + SITE_RVA, body, sizeof(body)));
    CHECK(invoke_abi(site, &state, 0, (uint32_t)(uintptr_t)site) && calls == 1);
    CHECK(VirtualFree(image, 0, MEM_RELEASE));
    return 0;
}

static int case_instructions(void)
{
    static const BYTE split[] = {1, 1, 4};
    static const BYTE partial[] = {1, 2, 2};
    static const BYTE immediate[] = {0x55, 0xb8, 0xe8, 0xe9, 0xeb, 0x70};
    static const BYTE immediate_lengths[] = {1, 5};
    static const BYTE displacement[] = {0x8b, 0x85, 0xe8, 0xe9, 0xeb, 0x70};
    static const BYTE displacement_lengths[] = {6};
    static const BYTE short_branch[] = {0xeb, 0x00, 0x90, 0x90, 0x90};
    static const BYTE short_lengths[] = {2, 1, 1, 1};
    static const BYTE indirect[] = {0xff, 0xd0, 0x90, 0x90, 0x90};
    static const BYTE enter[] = {0xc8,0,0,0,0x8b,0x45,0x08};
    static const BYTE enter_lengths[] = {4,3};
    static const BYTE enter_operands[] = {0xc8,0xe8,0xe9,0x00,0x90};
    static const BYTE enter_operand_lengths[] = {4,1};
    static const BYTE enter_truncated[] = {0x90,0x90,0xc8,0,0};
    static const BYTE truncated_lengths[] = {1,1,3};
    /* 16-bit MOV r/m (0x66 prefix): accepted only for 89/8b without immediate. */
    static const BYTE word_move[] = {0x55,0x8b,0xec,0x53,0x66,0x8b,0x45,0x0c};
    static const BYTE word_move_lengths[] = {1,2,1,4};
    static const BYTE word_immediate[] = {0x66,0xc7,0x45,0x08,0x01,0x00};
    static const BYTE word_immediate_lengths[] = {6};
    static const BYTE word_prefix_only[] = {0x90,0x90,0x90,0x90,0x66};
    static const BYTE prefix_only_lengths[] = {1,1,1,1,1};
    /* MOV EAX,[moffs32] between PUSH EBP and MOV EBP,ESP (DBSIM 0x4011a0). */
    static const BYTE absolute_load[] = {0x55,0xa1,0x48,0x80,0x4a,0x00,0x8b,0xec};
    static const BYTE absolute_load_lengths[] = {1,5,2};
    static const BYTE absolute_truncated[] = {0x90,0x90,0xa1,0x48,0x80};
    static const BYTE absolute_store[] = {0xa3,0x48,0x80,0x4a,0x00};
    static const BYTE absolute_one[] = {5};
    /* MOV AX,[moffs32] after the frame setup (DBSIM 0x413180): 66 A1 is six
     * bytes; a truncated offset and 66 A3 (store) are refused. */
    static const BYTE word_absolute[] = {0x55,0x8b,0xec,0x66,0xa1,0x54,0x9e,0x4a,0x00};
    static const BYTE word_absolute_lengths[] = {1,2,6};
    static const BYTE word_absolute_truncated[] = {0x90,0x90,0x66,0xa1,0x54};
    static const BYTE word_absolute_store[] = {0x55,0x8b,0xec,0x66,0xa3,0x54,0x9e,0x4a,0x00};
    /* MOV BX,1 after PUSH EBX (DBSIM 0x45fe6c): 66 BB imm16 is four bytes;
     * a cut immediate and the 32-bit reading (six bytes) are refused. */
    static const BYTE word_register_immediate[] = {0x55,0x8b,0xec,0x53,0x66,0xbb,0x01,0x00};
    static const BYTE word_register_immediate_lengths[] = {1,2,1,4};
    static const BYTE word_register_truncated[] = {0x90,0x90,0x66,0xbb,0x01};
    static const BYTE word_register_wide[] = {0x66,0xbb,0x01,0x00,0x00,0x00};
    static const BYTE word_register_wide_lengths[] = {6};
    /* CMP word [moffs32],0 (DBSIM 0x4240f4): 66 83 /7 ib is eight bytes;
     * ADD (/0) and SUB (/5) the same; OR (/1), a cut immediate and 66 81
     * (imm16) are refused. */
    static const BYTE word_compare[] = {0x66,0x83,0x3d,0x52,0x9e,0x4a,0x00,0x00};
    static const BYTE word_compare_lengths[] = {8};
    static const BYTE word_add[] = {0x66,0x83,0x45,0x08,0x01,0x55,0x8b,0xec};
    static const BYTE word_add_lengths[] = {5,1,2};
    static const BYTE word_sub[] = {0x66,0x83,0x6d,0x08,0x01,0x90};
    static const BYTE word_sub_lengths[] = {5,1};
    static const BYTE word_or[] = {0x66,0x83,0x4d,0x08,0x01,0x90};
    static const BYTE word_compare_truncated[] = {0x90,0x66,0x83,0x3d,0x52,0x9e,0x4a,0x00};
    static const BYTE word_compare_truncated_lengths[] = {1,7};
    static const BYTE word_compare_wide[] = {0x66,0x81,0x3d,0x52,0x9e,0x4a,0x00,0x00,0x00};
    static const BYTE word_compare_wide_lengths[] = {9};
    CHECK(es2_hook_prologue_valid(word_compare,sizeof(word_compare),word_compare_lengths,1));
    CHECK(es2_hook_prologue_valid(word_add,sizeof(word_add),word_add_lengths,3));
    CHECK(es2_hook_prologue_valid(word_sub,sizeof(word_sub),word_sub_lengths,2));
    CHECK(!es2_hook_prologue_valid(word_or,sizeof(word_or),word_sub_lengths,2));
    /* OR r/m32,imm8 (83 /1, DBSIM 0x411a44 `or eax,-1`) is accepted; 81 /1
     * (imm32) and the 16-bit form (word_or above) are not. A near RET (C3)
     * is accepted as the very last relocated byte only (the empty slot
     * methods `push ebp; mov ebp,esp; pop ebp; ret` of DBSIM, five bytes);
     * inside the range and as RET imm16 (C2) it is refused. */
    static const BYTE dword_or[] = {0x55,0x8b,0xec,0x83,0xc8,0xff,0x5d};
    static const BYTE dword_or_lengths[] = {1,2,3,1};
    static const BYTE dword_or_wide[] = {0x81,0xc8,0xff,0xff,0xff,0xff,0x90};
    static const BYTE dword_or_wide_lengths[] = {6,1};
    static const BYTE ret_body[] = {0x55,0x8b,0xec,0x5d,0xc3};
    static const BYTE ret_body_lengths[] = {1,2,1,1};
    static const BYTE ret_body_wrong[] = {1,2,2};
    static const BYTE ret_inside[] = {0x55,0xc3,0x90,0x90,0x90};
    static const BYTE ret_inside_lengths[] = {1,1,1,1,1};
    static const BYTE ret_imm[] = {0x55,0x8b,0xec,0x5d,0xc2,0x04,0x00};
    static const BYTE ret_imm_lengths[] = {1,2,1,3};
    CHECK(es2_hook_prologue_valid(dword_or,sizeof(dword_or),dword_or_lengths,4));
    CHECK(!es2_hook_prologue_valid(dword_or_wide,sizeof(dword_or_wide),dword_or_wide_lengths,2));
    CHECK(es2_hook_prologue_valid(ret_body,sizeof(ret_body),ret_body_lengths,4));
    CHECK(!es2_hook_prologue_valid(ret_body,sizeof(ret_body),ret_body_wrong,3));
    CHECK(!es2_hook_prologue_valid(ret_inside,sizeof(ret_inside),ret_inside_lengths,5));
    CHECK(!es2_hook_prologue_valid(ret_imm,sizeof(ret_imm),ret_imm_lengths,4));
    CHECK(!es2_hook_prologue_valid(word_compare_truncated,sizeof(word_compare_truncated),word_compare_truncated_lengths,2));
    CHECK(!es2_hook_prologue_valid(word_compare_wide,sizeof(word_compare_wide),word_compare_wide_lengths,1));
    /* CMP byte ptr [abs],0 after the frame setup (DBSIM 0x45a498): 80 3D is
     * seven bytes; other 80 groups (here ADD) and a cut operand are refused. */
    static const BYTE byte_compare[] = {0x55,0x8b,0xec,0x80,0x3d,0xa4,0xea,0x49,0x00,0x00};
    static const BYTE byte_compare_lengths[] = {1,2,7};
    static const BYTE byte_add[] = {0x55,0x8b,0xec,0x80,0x05,0xa4,0xea,0x49,0x00,0x00};
    static const BYTE byte_cut[] = {0x90,0x90,0x90,0x80,0x3d,0xa4,0xea};
    static const BYTE byte_cut_lengths[] = {1,1,1,4};
    CHECK(es2_hook_prologue_valid(byte_compare,sizeof(byte_compare),byte_compare_lengths,3));
    CHECK(!es2_hook_prologue_valid(byte_add,sizeof(byte_add),byte_compare_lengths,3));
    CHECK(!es2_hook_prologue_valid(byte_cut,sizeof(byte_cut),byte_cut_lengths,4));
    CHECK(es2_hook_prologue_valid(word_register_immediate,sizeof(word_register_immediate),word_register_immediate_lengths,4));
    CHECK(!es2_hook_prologue_valid(word_register_truncated,sizeof(word_register_truncated),truncated_lengths,3));
    CHECK(!es2_hook_prologue_valid(word_register_wide,sizeof(word_register_wide),word_register_wide_lengths,1));
    CHECK(es2_hook_prologue_valid(word_absolute,sizeof(word_absolute),word_absolute_lengths,3));
    CHECK(!es2_hook_prologue_valid(word_absolute_truncated,sizeof(word_absolute_truncated),truncated_lengths,3));
    CHECK(!es2_hook_prologue_valid(word_absolute_store,sizeof(word_absolute_store),word_absolute_lengths,3));
    CHECK(es2_hook_prologue_valid(absolute_load,6,absolute_load_lengths,2));
    CHECK(es2_hook_prologue_valid(absolute_load,sizeof(absolute_load),absolute_load_lengths,3));
    CHECK(!es2_hook_prologue_valid(absolute_truncated,sizeof(absolute_truncated),truncated_lengths,3));
    CHECK(!es2_hook_prologue_valid(absolute_store,sizeof(absolute_store),absolute_one,1));
    CHECK(es2_hook_prologue_valid(word_move,sizeof(word_move),word_move_lengths,4));
    CHECK(!es2_hook_prologue_valid(word_immediate,sizeof(word_immediate),word_immediate_lengths,1));
    CHECK(!es2_hook_prologue_valid(word_prefix_only,sizeof(word_prefix_only),prefix_only_lengths,5));
    CHECK(es2_hook_prologue_valid(enter,sizeof(enter),enter_lengths,2));
    CHECK(es2_hook_prologue_valid(enter_operands,sizeof(enter_operands),enter_operand_lengths,2));
    CHECK(!es2_hook_prologue_valid(enter_truncated,sizeof(enter_truncated),truncated_lengths,3));
    CHECK(!es2_hook_prologue_valid(enter,sizeof(enter),truncated_lengths,3));
    CHECK(es2_hook_prologue_valid(prologue, sizeof(prologue), lengths, sizeof(lengths)));
    CHECK(!es2_hook_prologue_valid(prologue, sizeof(prologue), split, sizeof(split)));
    CHECK(!es2_hook_prologue_valid(prologue, 5, partial, sizeof(partial)));
    CHECK(es2_hook_prologue_valid(immediate, sizeof(immediate), immediate_lengths, sizeof(immediate_lengths)));
    CHECK(es2_hook_prologue_valid(displacement, sizeof(displacement), displacement_lengths, sizeof(displacement_lengths)));
    CHECK(!es2_hook_prologue_valid(short_branch, sizeof(short_branch), short_lengths, sizeof(short_lengths)));
    CHECK(!es2_hook_prologue_valid(indirect, sizeof(indirect), short_lengths, sizeof(short_lengths)));
    CHECK(!es2_hook_prologue_valid(NULL, 6, lengths, sizeof(lengths)));
    CHECK(!es2_hook_prologue_valid(prologue, sizeof(prologue), NULL, sizeof(lengths)));
    CHECK(!es2_hook_prologue_valid(prologue, sizeof(prologue), lengths, 0));
    return case_ret_body();
}

static int case_rollback(const WCHAR *mode)
{
    BYTE *image = make_image(FALSE);
    CHECK(image != NULL);
    candidate_data data = {0};
    const es2_hook_span span = {0, 0, sizeof(state_data)};
    es2_hook_descriptor d = descriptor(&data, &span);
    es2_hook_options o = options(image, ES2_HOOK_REPLACED);
    o.patch_api.protect = injected_protect; o.patch_api.flush = injected_flush;
    es2_hook hook = {0};
    es2_hook_error expected;
    if (!wcscmp(mode, L"protect-failure")) { fail_protect_at = 1; expected = ES2_HOOK_ERROR_PROTECT; }
    else if (!wcscmp(mode, L"flush-failure")) { fail_flush_at = 1; expected = ES2_HOOK_ERROR_FLUSH; }
    else if (!wcscmp(mode, L"restore-failure")) { fail_protect_at = 2; expected = ES2_HOOK_ERROR_PROTECT; }
    else { persistent_flush_failure = TRUE; expected = ES2_HOOK_ERROR_ROLLBACK; }
    CHECK(!es2_hook_install(&hook, &d, &o));
    CHECK(hook.error == expected);
    CHECK(!memcmp(image + SITE_RVA, stub, sizeof(stub)));
    CHECK(page_access(image + SITE_RVA) == PAGE_EXECUTE_READ);
    if (expected == ES2_HOOK_ERROR_ROLLBACK) {
        CHECK(hook.installed == 2 && hook.code && hook.trampoline);
        persistent_flush_failure = FALSE;
        CHECK(es2_hook_uninstall(&hook));
    }
    CHECK(!hook.code && !hook.trampoline && !hook.installed);
    CHECK(!data.calls && !hook.calls);
    state_data state = {2, 6};
    CHECK(invoke_abi((stub_function)(image + SITE_RVA), &state, 3, 5));
    CHECK(state.guard == 6);
    CHECK(VirtualFree(image, 0, MEM_RELEASE));
    return 0;
}

static int read_log(const WCHAR *path, char *text, DWORD capacity)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(file != INVALID_HANDLE_VALUE);
    DWORD size = GetFileSize(file, NULL), got = 0;
    CHECK(size != INVALID_FILE_SIZE && size < capacity);
    CHECK(ReadFile(file, text, size, &got, NULL) && got == size);
    text[got] = 0;
    CHECK(CloseHandle(file));
    return 0;
}

static unsigned occurrences(const char *text, const char *needle)
{
    unsigned count = 0;
    while ((text = strstr(text, needle)) != NULL) { ++count; text += strlen(needle); }
    return count;
}

static int case_calls(const WCHAR *mode, const WCHAR *log_path)
{
    BOOL replaced = !wcscmp(mode, L"replaced");
    BOOL equal = !wcscmp(mode, L"compare-equal");
    BOOL return_only = !wcscmp(mode, L"compare-return");
    BOOL state_offset = !wcscmp(mode, L"state-offset");
    BYTE *image = make_image(FALSE);
    CHECK(image != NULL);
    candidate_data data = {.kind = equal ? 0u : return_only ? 2u : state_offset ? 3u : 1u};
    es2_hook_span span = {0, state_offset ? 4u : 0u, state_offset ? 4u : sizeof(state_data)};
    es2_hook_descriptor d = descriptor(&data, &span);
    es2_hook_options o = options(image, replaced ? ES2_HOOK_REPLACED : ES2_HOOK_COMPARE);
    es2_hook hook = {0};
    es2_hook_log log = {0};
    uint32_t frame = 70;
    CHECK(es2_hook_log_open(&log, log_path, 2, state_offset ? NULL : frame_number, &frame));
    CHECK(log.limit == 2);
    o.log = &log;
    CHECK(es2_hook_install(&hook, &d, &o));
    state_data actual = {10, 0xaabbccdd}, baseline = actual;
    for (unsigned i = 0; i < 5; ++i) {
        baseline.value += 3;
        uint32_t expected = replaced ? baseline.value + i + 1 : baseline.value;
        CHECK(invoke_abi((stub_function)(image + SITE_RVA), &actual, 3, expected));
        if (!replaced) CHECK(!memcmp(&actual, &baseline, sizeof(actual)));
        else CHECK(actual.value == expected && actual.guard == baseline.guard);
        ++frame;
    }
    CHECK(hook.calls == 5 && data.calls == 5 && hook.skipped == 0 && hook.bypassed == 0);
    CHECK(hook.mismatches == (replaced || equal ? 0u : 5u));
    CHECK(log.attempted == (replaced || equal ? 0u : 2u));
    CHECK(log.written == (replaced || equal ? 0u : 2u) && !log.failed_writes);
    CHECK(es2_hook_uninstall(&hook));
    es2_hook_log_close(&log);
    char text[4096];
    CHECK(read_log(log_path, text, sizeof(text)) == 0);
    if (replaced || equal) CHECK(text[0] == 0);
    else {
        CHECK(occurrences(text, "\"event\":\"hook_mismatch\"") == 2);
        CHECK(occurrences(text, "\"hook\":\"own_add\"") == 2);
        CHECK(strstr(text, "\"call\":1,") && strstr(text, "\"call\":2,") && !strstr(text, "\"call\":3,"));
        CHECK(strstr(text, "\"original\":13,") && strstr(text, "\"original\":16,"));
        CHECK(strstr(text, state_offset ? "\"replacement\":13," : "\"replacement\":14,"));
        CHECK(occurrences(text, return_only ? "\"first_diff_span\":null,\"first_diff_offset\":null" :
                                            "\"first_diff_span\":0,\"first_diff_offset\":0") == 2);
        if (state_offset) CHECK(occurrences(text, "\"frame\":null,") == 2);
        else CHECK(strstr(text, "\"frame\":70,") && strstr(text, "\"frame\":71,"));
        char argument[80];
        snprintf(argument, sizeof(argument), "\"args\":[%lu,3]", (unsigned long)(uintptr_t)&actual);
        CHECK(occurrences(text, argument) == 2);
    }
    CHECK(!memcmp(image + SITE_RVA, stub, sizeof(stub)));
    CHECK(VirtualFree(image, 0, MEM_RELEASE));
    return 0;
}

static int case_nested(void)
{
    BYTE *image = make_image(TRUE);
    CHECK(image != NULL);
    candidate_data outer_data = {0}, inner_data = {.kind = 1};
    const es2_hook_span span = {0, 0, sizeof(state_data)};
    es2_hook_descriptor outer = descriptor(&outer_data, &span), inner = descriptor(&inner_data, &span);
    outer.name = "outer"; inner.name = "inner"; inner.rva = INNER_RVA;
    es2_hook_options outer_options = options(image, ES2_HOOK_COMPARE), inner_options = options(image, ES2_HOOK_REPLACED);
    es2_hook outer_hook = {0}, inner_hook = {0};
    CHECK(es2_hook_install(&inner_hook, &inner, &inner_options));
    CHECK(es2_hook_install(&outer_hook, &outer, &outer_options));
    state_data state = {30, 0xaabbccdd};
    for (unsigned i = 0; i < 4; ++i) CHECK(invoke_abi((stub_function)(image + SITE_RVA), &state, 3, 33 + i * 3));
    CHECK(state.value == 42 && state.guard == 0xaabbccdd);
    CHECK(outer_hook.calls == 4 && outer_hook.mismatches == 0 && outer_data.calls == 4);
    CHECK(inner_hook.calls == 4 && inner_hook.bypassed == 4 && inner_data.calls == 0);
    CHECK(invoke_abi((stub_function)(image + INNER_RVA), &state, 3, 46));
    CHECK(inner_data.calls == 1 && inner_hook.calls == 5 && inner_hook.bypassed == 4);
    CHECK(es2_hook_uninstall(&outer_hook));
    CHECK(es2_hook_uninstall(&inner_hook));
    CHECK(page_access(image + SITE_RVA) == PAGE_EXECUTE_READ);
    CHECK(VirtualFree(image, 0, MEM_RELEASE));
    return 0;
}

static int case_ini(const WCHAR *path)
{
    es2_hook_mode mode = ES2_HOOK_COMPARE;
    CHECK(es2_hook_read_mode(path, "rng_next", &mode) && mode == ES2_HOOK_ORIGINAL);
    const WCHAR *values[] = {L"original", L"replaced", L"compare", L"COMPARE"};
    const es2_hook_mode expected[] = {ES2_HOOK_ORIGINAL, ES2_HOOK_REPLACED, ES2_HOOK_COMPARE, ES2_HOOK_COMPARE};
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        CHECK(WritePrivateProfileStringW(L"hooks", L"rng_next", values[i], path));
        CHECK(es2_hook_read_mode(path, "rng_next", &mode) && mode == expected[i]);
    }
    const WCHAR *bad[] = {L"", L"enabled", L"compare_extra", L"compare1234567890123456789012345"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        CHECK(WritePrivateProfileStringW(L"hooks", L"rng_next", bad[i], path));
        CHECK(!es2_hook_read_mode(path, "rng_next", &mode) && mode == ES2_HOOK_ORIGINAL);
    }
    CHECK(es2_hook_read_mode(path, "rng_bounded", &mode) && mode == ES2_HOOK_ORIGINAL);
    CHECK(!es2_hook_read_mode(path, "bad=name", &mode));
    CHECK(!es2_hook_read_mode(path, "", &mode));
    CHECK(!es2_hook_read_mode(NULL, "rng_next", &mode));
    CHECK(!es2_hook_read_mode(path, "rng_next", NULL));
    return 0;
}

static int case_log_existing(const WCHAR *path)
{
    es2_hook_log first = {0}, second = {0};
    CHECK(es2_hook_log_open(&first, path, 0, NULL, NULL));
    CHECK(first.limit == 32);
    es2_hook_log_close(&first);
    CHECK(!es2_hook_log_open(&second, path, 2, NULL, NULL));
    char text[32];
    CHECK(read_log(path, text, sizeof(text)) == 0 && text[0] == 0);
    es2_hook_log_close(&second);
    CHECK(!es2_hook_log_open(NULL, path, 1, NULL, NULL));
    return 0;
}

static int output_paths(int argc, WCHAR **argv, WCHAR directory[MAX_PATH], WCHAR log[MAX_PATH], WCHAR ini[MAX_PATH])
{
    WCHAR parent[MAX_PATH];
    DWORD length;
    if (argc == 3) length = GetFullPathNameW(argv[2], MAX_PATH, parent, NULL);
    else {
        length = GetEnvironmentVariableW(L"TEST_TMPDIR", parent, MAX_PATH);
        if (!length) length = GetTempPathW(MAX_PATH, parent);
    }
    CHECK(length && length < MAX_PATH - 64);
    DWORD attributes = GetFileAttributesW(parent);
    CHECK(attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY));
    int n = swprintf(directory, MAX_PATH, L"%ls\\hook-%lu-%llu", parent,
                     (unsigned long)GetCurrentProcessId(), (unsigned long long)GetTickCount64());
    CHECK(n > 0 && n < MAX_PATH - 24);
    CHECK(CreateDirectoryW(directory, NULL));
    CHECK(swprintf(log, MAX_PATH, L"%ls\\mismatches.jsonl", directory) > 0);
    CHECK(swprintf(ini, MAX_PATH, L"%ls\\config.ini", directory) > 0);
    return 0;
}

int wmain(int argc, WCHAR **argv)
{
    CHECK(argc == 2 || argc == 3);
    CHECK(sizeof(void *) == 4);
    WCHAR directory[MAX_PATH], log[MAX_PATH], ini[MAX_PATH];
    CHECK(output_paths(argc, argv, directory, log, ini) == 0);
    const WCHAR *mode = argv[1];
    int result;
    if (!wcscmp(mode, L"valid")) result = case_valid(FALSE);
    else if (!wcscmp(mode, L"uninstall-busy")) result = case_valid(TRUE);
    else if (!wcscmp(mode, L"original")) result = case_original();
    else if (!wcscmp(mode, L"image-base") || !wcscmp(mode, L"image-size") || !wcscmp(mode, L"signature") || !wcscmp(mode, L"relative")) result = case_refusal(mode);
    else if (!wcscmp(mode, L"instructions")) result = case_instructions();
    else if (!wcscmp(mode, L"protect-failure") || !wcscmp(mode, L"flush-failure") || !wcscmp(mode, L"restore-failure") || !wcscmp(mode, L"rollback-failure")) result = case_rollback(mode);
    else if (!wcscmp(mode, L"replaced") || !wcscmp(mode, L"compare-equal") || !wcscmp(mode, L"compare-wrong") || !wcscmp(mode, L"compare-return") || !wcscmp(mode, L"state-offset")) result = case_calls(mode, log);
    else if (!wcscmp(mode, L"nested")) result = case_nested();
    else if (!wcscmp(mode, L"ini")) result = case_ini(ini);
    else if (!wcscmp(mode, L"log-existing")) result = case_log_existing(log);
    else { fwprintf(stderr, L"Unknown fixture case: %ls\n", mode); return 2; }
    if (!result) wprintf(L"PASS: hook %ls (%u checks), own synthetic code only; artifacts %ls\n", mode, checks, directory);
    return result;
}
