#ifndef ES2_ADAPTER_ENTRY_SHIMS_X86_H
#define ES2_ADAPTER_ENTRY_SHIMS_X86_H
/* The proxy's capture of the entry shims (entry_shims.h): the framework's
 * jump at a hook's site goes to the shim's stub, which keeps EAX in a static
 * and enters es2_shim_x86_capture with the shim in EAX. The capture writes
 * nothing on the original's stack: it stores the eight registers in
 * es2_shim_x86_m, switches ESP to a slab of the shims' own stack (one per
 * nesting level; the replacement a shim runs may reach another shim) and
 * calls the shim's C entry there, so that the residue below the entry ESP is
 * still the caller's when the entry reads it. Afterwards it loads the
 * registers the entry left and either jumps to the framework's entry with
 * the entry ESP (the shim continues into the replacement) or returns to the
 * caller (the shim ran it). The flags are clobbered. Main thread only, like
 * the original shims' statics. */
#include <string.h>

enum { ES2_SHIM_X86_SLAB = 16384, ES2_SHIM_X86_LEVELS = 64 };
__attribute__((used, aligned(16))) static uint8_t es2_shim_x86_stack[ES2_SHIM_X86_LEVELS * ES2_SHIM_X86_SLAB];
__attribute__((used)) static uint8_t *es2_shim_x86_sp = es2_shim_x86_stack + sizeof(es2_shim_x86_stack);
__attribute__((used)) static uint32_t es2_shim_x86_eax, es2_shim_x86_target;
__attribute__((used)) static es2_shim *es2_shim_x86_s;
__attribute__((used)) static es2_machine es2_shim_x86_m;

__attribute__((used)) static int __cdecl es2_shim_x86_run(es2_shim *s) {
    es2_machine m = es2_shim_x86_m;
    int ran = s->entry(&m, s->bridge);
    if (ran) {
        es2_shim_x86_target = shim_r32(m.esp);
        m.esp += 4u + s->pop;
    } else {
        es2_shim_x86_target = s->bridge;
    }
    es2_shim_x86_m = m;
    return ran;
}

__asm__(
    ".text\n\t.p2align 4\n"
    "_es2_shim_x86_capture:\n\t"
    "movl %eax,_es2_shim_x86_s\n\tmovl _es2_shim_x86_eax,%eax\n\tmovl %eax,_es2_shim_x86_m\n\t"
    "movl %ecx,_es2_shim_x86_m+4\n\tmovl %edx,_es2_shim_x86_m+8\n\tmovl %ebx,_es2_shim_x86_m+12\n\t"
    "movl %esp,_es2_shim_x86_m+16\n\tmovl %ebp,_es2_shim_x86_m+20\n\tmovl %esi,_es2_shim_x86_m+24\n\t"
    "movl %edi,_es2_shim_x86_m+28\n\t"
    "movl _es2_shim_x86_sp,%esp\n\tcmpl $_es2_shim_x86_stack+16384,%esp\n\tjb 9f\n\t"
    "subl $16384,_es2_shim_x86_sp\n\tpushl _es2_shim_x86_s\n\tcall _es2_shim_x86_run\n\t"
    "addl $16384,_es2_shim_x86_sp\n\t"
    "movl _es2_shim_x86_m+4,%ecx\n\tmovl _es2_shim_x86_m+8,%edx\n\tmovl _es2_shim_x86_m+12,%ebx\n\t"
    "movl _es2_shim_x86_m+20,%ebp\n\tmovl _es2_shim_x86_m+24,%esi\n\tmovl _es2_shim_x86_m+28,%edi\n\t"
    "movl _es2_shim_x86_m+16,%esp\n\tmovl _es2_shim_x86_m,%eax\n\tjmp *_es2_shim_x86_target\n"
    "9:\n\tud2\n\t");
_Static_assert(ES2_SHIM_X86_SLAB == 16384, "es2_shim_x86_capture's slab size");

#define ES2_SHIM_X86_STUB(shim) \
    __attribute__((naked, used)) static void shim##_x86(void) { \
        __asm__ volatile("movl %eax,_es2_shim_x86_eax\n\tmovl $_" #shim ",%eax\n\tjmp _es2_shim_x86_capture\n\t"); }
ES2_ENTRY_SHIMS(ES2_SHIM_X86_STUB)
#undef ES2_SHIM_X86_STUB
static void (*const es2_shim_x86_stubs[])(void) = {
#define ES2_SHIM_X86_ROW(shim) shim##_x86,
    ES2_ENTRY_SHIMS(ES2_SHIM_X86_ROW)
#undef ES2_SHIM_X86_ROW
};
_Static_assert(sizeof(es2_shim_x86_stubs) / sizeof(es2_shim_x86_stubs[0]) == ADAPTER_SHIM_COUNT, "one stub per shim");

/* Called only after the framework has validated hash/signature and installed
 * its own jump: the jump goes to the shim's stub, its old target becomes the
 * shim's bridge. h->patch stays consistent, so rollback and uninstall still
 * validate the exact patch. */
static BOOL es2_shim_x86_redirect(es2_hook *h, es2_shim *s, void (*stub)(void)) {
    DWORD old, ignored;
    size_t n = h->descriptor->relocate_size;
    if (memcmp(h->site, h->patch, n)) return FALSE;
    if (!VirtualProtect(h->site, n, PAGE_EXECUTE_READWRITE, &old)) return FALSE;
    s->bridge = (uint32_t)(uintptr_t)(h->site + 5) + shim_r32((uint32_t)(uintptr_t)(h->patch + 1));
    shim_w32((uint32_t)(uintptr_t)(h->patch + 1), (uint32_t)(uintptr_t)stub - (uint32_t)(uintptr_t)(h->site + 5));
    memcpy(h->site, h->patch, n);
    BOOL flushed = FlushInstructionCache(GetCurrentProcess(), h->site, n);
    BOOL protected = VirtualProtect(h->site, n, old, &ignored);
    return flushed && protected;
}

/* Called by weapons_install_hook: a hook with a shim of entry_shims.h gets
 * its stub; the vfs and assets groups record their hooks' runtimes. */
static BOOL weapons_capture_entry(es2_hook *h) {
    if (!h->installed) return TRUE;
    for (unsigned i = 0; i < ADAPTER_SHIM_COUNT; ++i)
        if (adapter_shims[i]->rva == h->descriptor->rva) return es2_shim_x86_redirect(h, adapter_shims[i], es2_shim_x86_stubs[i]);
    vfs_record_hook(h);
    assets_record_hook(h);
    return TRUE;
}
#endif
