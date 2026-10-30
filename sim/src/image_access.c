/* The image-access measurement (es2/sim/image_access.h, own_process.md, "what
 * es2sim really reads from the mapped image"). A vectored handler of its own
 * sees the read and write faults the armed protections cause; it never takes an
 * execute fault (those stay the stage-1 trap's, dispatch_trap.c). */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/platform/os.h"
#include "es2/sim/image_access.h"

#define IA_PAGE 0x1000u
#define IA_MAX_PAGES 96u
#define IA_MAX_RUNS 16u
#define IA_MAX_AREAS 12u
#define IA_MAX_CODE_READS 1024u
#define IA_MAX_THREADS 8u
#define IA_MAX_PENDING 4u
#define IA_TRAP_FLAG 0x100u
/* Sampling: a guarded page is re-armed after an access at most this often per
 * epoch; an epoch ends at every resume (the host's tick) and after IA_EPOCH_MS
 * of wall time (the load phase has no ticks). Byte coverage is therefore a
 * sampled lower bound; page coverage is not sampled (the first access of an
 * epoch always faults). With 4 ms epochs acceptance-60s did not finish in seven
 * minutes; 50 ms costs about 1.6 times (own_process.md, "what es2sim really
 * reads from the mapped image"). */
#define IA_BUDGET 32u
#define IA_EPOCH_MS 50u

typedef struct ia_page {
    uint32_t va;
    DWORD base;                 /* the protection the host set */
    unsigned area;
    unsigned epoch_faults;
    unsigned pending;           /* re-arm waits for a single step */
    uint64_t read_faults, write_faults, exec_faults, rearms;
    uint8_t read_bits[IA_PAGE / 8u], write_bits[IA_PAGE / 8u];
} ia_page;

typedef struct ia_area {
    char name[12];
    uint32_t lo, hi, file_hi;   /* watched [lo, hi), bytes from the file below file_hi */
    int code;
} ia_area;

/* A CODE run made no-access while armed; `base` is the protection it had at
 * arming and gets back on pause and disarm (the host's own CODE protection,
 * no-access outside the table pages; before, read-only). */
typedef struct ia_run { uint32_t lo, hi; DWORD base; } ia_run;

typedef struct ia_thread {
    DWORD tid;
    unsigned count;
    uint32_t pages[IA_MAX_PENDING];
    uint8_t code[IA_MAX_PENDING];   /* 1: a CODE page to make no-access again */
} ia_thread;

static struct {
    char path[MAX_PATH];
    int configured, armed, written;
    unsigned paused;
    PVOID handler;
    SRWLOCK lock;
    ia_page pages[IA_MAX_PAGES];
    unsigned page_count;
    ia_area areas[IA_MAX_AREAS];
    unsigned area_count;
    ia_run runs[IA_MAX_RUNS];         /* CODE no-access runs */
    unsigned run_count;
    uint32_t code_lo, code_hi;
    ia_thread threads[IA_MAX_THREADS];
    es2_sim_code_read code_reads[IA_MAX_CODE_READS];
    unsigned code_read_count;
    uint64_t code_read_overflow, code_read_faults, guard_faults, single_steps, undecoded, epochs, foreign_steps;
    uint64_t epoch_start, frequency;
    uintptr_t main_lo, main_hi;
} ia = {.lock = SRWLOCK_INIT};

/* ------------------------------------------------------------------ decoder */

/* Operand size codes: 'b' 1, 'w' 2, 'v' 2 or 4 (operand-size prefix), 'd'
 * 4, 'q' 8, 'x' 16, 'p' 6 (far pointer). */
static unsigned size_of(char code, int opsize16) {
    switch (code) {
    case 'b': return 1;
    case 'w': return 2;
    case 'v': return opsize16 ? 2 : 4;
    case 'd': return 4;
    case 'p': return 6;
    case 'q': return 8;
    case 'x': return 16;
    default: return 0;
    }
}

static unsigned x87_size(uint8_t op, unsigned reg) {
    static const uint8_t d9[8] = {4, 0, 4, 4, 28, 2, 28, 2};
    static const uint8_t db[8] = {4, 4, 4, 4, 0, 10, 0, 10};
    static const uint8_t dd[8] = {8, 8, 8, 8, 108, 0, 108, 2};
    static const uint8_t df[8] = {2, 2, 2, 2, 10, 8, 10, 8};
    switch (op) {
    case 0xd8: case 0xda: return 4;
    case 0xdc: return 8;
    case 0xde: return 2;
    case 0xd9: return d9[reg];
    case 0xdb: return db[reg];
    case 0xdd: return dd[reg];
    default: return df[reg];
    }
}

static char two_byte_code(uint8_t op, int p66, int pf2, int pf3) {
    if (op >= 0x40 && op <= 0x4f) return 'v';          /* cmovcc */
    if (op >= 0x90 && op <= 0x9f) return 'b';          /* setcc */
    switch (op) {
    case 0x10: case 0x11: return pf3 ? 'd' : pf2 ? 'q' : 'x';
    case 0x12: case 0x13: case 0x16: case 0x17: return 'q';
    case 0x14: case 0x15: case 0x28: case 0x29: case 0x2b: case 0xe7: return 'x';
    case 0x2a: return pf2 || pf3 ? 'd' : 'q';
    case 0x2c: case 0x2d: return pf3 ? 'd' : 'q';
    case 0x2e: case 0x2f: return p66 ? 'q' : 'd';
    case 0x6e: return p66 ? 'd' : 'd';
    case 0x7e: return pf3 ? 'q' : 'd';
    case 0x6f: case 0x7f: return p66 || pf3 ? 'x' : 'q';
    case 0xd6: return 'q';
    case 0xa3: case 0xab: case 0xb3: case 0xbb: case 0xba: case 0xa4: case 0xa5: case 0xac: case 0xad:
    case 0xaf: case 0xb1: case 0xc1: case 0xbc: case 0xbd: return 'v';
    case 0xb0: case 0xc0: case 0xb6: case 0xbe: return 'b';
    case 0xb7: case 0xbf: return 'w';
    case 0xc7: return 'q';
    default: break;
    }
    if (op >= 0x51 && op <= 0x5f) return pf3 ? 'd' : pf2 ? 'q' : 'x';
    if ((op >= 0x60 && op <= 0x6d) || (op >= 0xd0 && op <= 0xfe) || op == 0x70 || (op >= 0x74 && op <= 0x76))
        return p66 ? 'x' : 'q';
    return 0;
}

unsigned es2_sim_access_operand_size(const uint8_t *ip, size_t avail, int *string) {
    int p66 = 0, pf2 = 0, pf3 = 0;
    size_t i = 0;
    if (string) *string = 0;
    for (; i < avail && i < 14; ++i) {
        uint8_t b = ip[i];
        if (b == 0x66) p66 = 1;
        else if (b == 0xf2) pf2 = 1;
        else if (b == 0xf3) pf3 = 1;
        else if (b == 0x67 || b == 0xf0 || b == 0x2e || b == 0x36 || b == 0x3e || b == 0x26 || b == 0x64 || b == 0x65) continue;
        else break;
    }
    if (i >= avail) return 0;
    uint8_t op = ip[i++];
    char code = 0;
    int modrm = 1;
    if (op == 0x0f) {
        if (i >= avail) return 0;
        op = ip[i++];
        code = two_byte_code(op, p66, pf2, pf3);
        /* An SSE size prefix is not an operand-size prefix. */
        if (code == 'x' || code == 'q') p66 = 0;
    } else if (op < 0x40 && (op & 7u) < 4u) {
        code = op & 1u ? 'v' : 'b';
    } else if (op >= 0xa0 && op <= 0xa3) {
        code = op & 1u ? 'v' : 'b';
        modrm = 0;
    } else if ((op >= 0xa4 && op <= 0xa7) || (op >= 0xaa && op <= 0xaf)) {
        if (string) *string = 1;
        code = op & 1u ? 'v' : 'b';
        modrm = 0;
    } else if (op == 0xd7) {
        code = 'b';                                    /* xlat */
        modrm = 0;
    } else if (op >= 0xd8 && op <= 0xdf) {
        if (i >= avail || (ip[i] >> 6) == 3u) return 0;
        return x87_size(op, (ip[i] >> 3) & 7u);
    } else if (op == 0xff) {
        if (i >= avail) return 0;
        static const char ff[8] = {'v', 'v', 'd', 'p', 'd', 'p', 'v', 0};
        code = ff[(ip[i] >> 3) & 7u];
    } else {
        switch (op) {
        case 0x69: case 0x6b: case 0x81: case 0x83: case 0x85: case 0x87: case 0x89: case 0x8b: case 0x8f:
        case 0xc1: case 0xc7: case 0xd1: case 0xd3: case 0xf7: code = 'v'; break;
        case 0x80: case 0x82: case 0x84: case 0x86: case 0x88: case 0x8a: case 0xc0: case 0xc6: case 0xd0: case 0xd2:
        case 0xf6: case 0xfe: code = 'b'; break;
        case 0x8c: case 0x8e: code = 'w'; break;
        default: return 0;
        }
    }
    if (!code) return 0;
    if (modrm && (i >= avail || (ip[i] >> 6) == 3u)) return 0;
    return size_of(code, p66);
}

/* ------------------------------------------------------------------ pages */

static ia_page *find_page(uint32_t va) {
    unsigned lo = 0, hi = ia.page_count;
    uint32_t page = va & ~(IA_PAGE - 1u);
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2u;
        if (ia.pages[mid].va < page) lo = mid + 1u; else hi = mid;
    }
    return lo < ia.page_count && ia.pages[lo].va == page ? &ia.pages[lo] : NULL;
}

static int in_code_runs(uint32_t va) {
    for (unsigned i = 0; i < ia.run_count; ++i)
        if (va >= ia.runs[i].lo && va < ia.runs[i].hi) return 1;
    return 0;
}

static int protect(uint32_t va, uint32_t size, DWORD protection) {
    uint32_t previous;
    return es2_os_virtual_protect(va, size, protection, &previous);
}

static void arm_page(ia_page *p) {
    if (protect(p->va, IA_PAGE, p->base | PAGE_GUARD)) ++p->rearms;
}

static void arm_all(void) {
    for (unsigned i = 0; i < ia.page_count; ++i) {
        ia.pages[i].epoch_faults = 0;              /* a new epoch: the budget again */
        if (!ia.pages[i].pending) arm_page(&ia.pages[i]);
    }
    for (unsigned i = 0; i < ia.run_count; ++i) protect(ia.runs[i].lo, ia.runs[i].hi - ia.runs[i].lo, PAGE_NOACCESS);
    ++ia.epochs;
    ia.epoch_start = es2_os_counter();
}

static void restore_all(void) {
    for (unsigned i = 0; i < ia.page_count; ++i) protect(ia.pages[i].va, IA_PAGE, ia.pages[i].base);
    for (unsigned i = 0; i < ia.run_count; ++i) protect(ia.runs[i].lo, ia.runs[i].hi - ia.runs[i].lo, ia.runs[i].base);
}

static void epoch_check(void) {
    uint64_t now = es2_os_counter();
    if ((int64_t)(now - ia.epoch_start) * 1000 >= (int64_t)IA_EPOCH_MS * (int64_t)ia.frequency) arm_all();
}

static void mark(uint8_t *bits, uint32_t page_va, uint32_t va, unsigned size) {
    uint32_t end = va + (size ? size : 1u);
    if (end > page_va + IA_PAGE || end < va) end = page_va + IA_PAGE;
    for (uint32_t a = va; a < end; ++a) bits[(a - page_va) >> 3] |= (uint8_t)(1u << ((a - page_va) & 7u));
}

static ia_thread *thread_slot(int create) {
    uint32_t tid = es2_os_thread_id();
    for (unsigned i = 0; i < IA_MAX_THREADS; ++i)
        if (ia.threads[i].tid == tid) return &ia.threads[i];
    if (!create) return NULL;
    for (unsigned i = 0; i < IA_MAX_THREADS; ++i)
        if (!ia.threads[i].tid) { ia.threads[i].tid = tid; ia.threads[i].count = 0; return &ia.threads[i]; }
    return NULL;
}

static unsigned instruction_size(const CONTEXT *c) {
    int string = 0;
    return es2_sim_access_operand_size((const uint8_t *)(uintptr_t)c->Eip, 15, &string);
}

static uintptr_t module_of(uintptr_t address, char *name, unsigned capacity) {
    HMODULE module = NULL;
    name[0] = 0;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)address, &module) || !module)
        return 0;
    char full[ES2_OS_MAX_PATH];
    uint32_t got = es2_os_module_file_name((uintptr_t)module, full, sizeof full);
    if (got && got < sizeof full) {
        const char *slash = strrchr(full, '\\');
        snprintf(name, capacity, "%s", slash ? slash + 1 : full);
    }
    return (uintptr_t)module;
}

/* The first dword on the stack that is a return address into the main
 * module (the byte before it ends a CALL). */
static uint32_t caller_of(const CONTEXT *c) {
    const uint32_t *sp = (const uint32_t *)(uintptr_t)c->Esp;
    es2_os_region m;
    if (!es2_os_virtual_query((uintptr_t)sp, &m) || m.state != ES2_OS_MEM_COMMIT) return 0;
    uintptr_t region_end = m.base + m.size;
    for (unsigned i = 0; i < 256u && (uintptr_t)(sp + i + 1) <= region_end; ++i) {
        uintptr_t v = sp[i];
        if (v < ia.main_lo + 8u || v >= ia.main_hi) continue;
        const uint8_t *b = (const uint8_t *)v;
        if (b[-5] == 0xe8 || (b[-2] == 0xff && ((b[-1] >> 3) & 7u) == 2u) || (b[-3] == 0xff && ((b[-2] >> 3) & 7u) == 2u) ||
            (b[-6] == 0xff && ((b[-5] >> 3) & 7u) == 2u) || (b[-7] == 0xff && ((b[-6] >> 3) & 7u) == 2u))
            return (uint32_t)(v - ia.main_lo);
    }
    return 0;
}

static void record_code_read(const CONTEXT *c, uint32_t va) {
    ++ia.code_read_faults;
    unsigned size = instruction_size(c);
    if (!size) ++ia.undecoded;
    char name[32];
    uintptr_t base = module_of(c->Eip, name, sizeof(name));
    uint32_t offset = (uint32_t)(c->Eip - base);
    uint32_t caller = caller_of(c);
    for (unsigned i = 0; i < ia.code_read_count; ++i) {
        es2_sim_code_read *r = &ia.code_reads[i];
        if (r->va == va && r->size == size && r->eip_offset == offset && r->caller_offset == caller && !strcmp(r->module, name)) {
            ++r->count;
            return;
        }
    }
    if (ia.code_read_count == IA_MAX_CODE_READS) { ++ia.code_read_overflow; return; }
    es2_sim_code_read *r = &ia.code_reads[ia.code_read_count++];
    r->va = va; r->size = size; r->eip_offset = offset; r->caller_offset = caller; r->count = 1;
    snprintf(r->module, sizeof(r->module), "%s", name);
}

static int pend(ia_thread *t, uint32_t page, uint8_t code) {
    for (unsigned i = 0; i < t->count; ++i)
        if (t->pages[i] == page) return 1;
    if (t->count == IA_MAX_PENDING) return 0;
    t->pages[t->count] = page;
    t->code[t->count] = code;
    ++t->count;
    return 1;
}

static LONG CALLBACK access_handler(EXCEPTION_POINTERS *x) {
    const EXCEPTION_RECORD *r = x->ExceptionRecord;
    CONTEXT *c = x->ContextRecord;
    DWORD code = r->ExceptionCode;
    if (code == EXCEPTION_SINGLE_STEP) {
        AcquireSRWLockExclusive(&ia.lock);
        ia_thread *t = ia.armed ? thread_slot(0) : NULL;
        if (!t || !t->count) {
            ++ia.foreign_steps;
            ReleaseSRWLockExclusive(&ia.lock);
            return EXCEPTION_CONTINUE_SEARCH;
        }
        ++ia.single_steps;
        for (unsigned i = 0; i < t->count; ++i) {
            if (t->code[i]) {
                if (!ia.paused) protect(t->pages[i], IA_PAGE, PAGE_NOACCESS);
                continue;
            }
            ia_page *p = find_page(t->pages[i]);
            if (!p) continue;
            p->pending = 0;
            if (!ia.paused) arm_page(p);
        }
        t->count = 0;
        c->EFlags &= ~IA_TRAP_FLAG;
        if (!ia.paused) epoch_check();
        ReleaseSRWLockExclusive(&ia.lock);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if ((code != EXCEPTION_ACCESS_VIOLATION && code != STATUS_GUARD_PAGE_VIOLATION) || r->NumberParameters < 2)
        return EXCEPTION_CONTINUE_SEARCH;
    uint32_t kind = (uint32_t)r->ExceptionInformation[0], va = (uint32_t)r->ExceptionInformation[1];
    int execute = kind == 8u || va == (uint32_t)c->Eip;
    AcquireSRWLockExclusive(&ia.lock);
    if (!ia.armed) { ReleaseSRWLockExclusive(&ia.lock); return EXCEPTION_CONTINUE_SEARCH; }
    LONG verdict = EXCEPTION_CONTINUE_SEARCH;
    if (code == STATUS_GUARD_PAGE_VIOLATION) {
        ia_page *p = find_page(va);
        if (p) {
            /* The guard is gone; the instruction runs again and succeeds (or,
             * fetching, faults on to the trap). */
            ++ia.guard_faults;
            verdict = EXCEPTION_CONTINUE_EXECUTION;
            if (execute) ++p->exec_faults;
            else {
                unsigned size = instruction_size(c);
                if (!size) ++ia.undecoded;
                if (kind == 1u) { ++p->write_faults; mark(p->write_bits, p->va, va, size); }
                else { ++p->read_faults; mark(p->read_bits, p->va, va, size); }
                ia_thread *t;
                if (++p->epoch_faults < IA_BUDGET && !ia.paused && (t = thread_slot(1)) && pend(t, p->va, 0)) {
                    p->pending = 1;
                    c->EFlags |= IA_TRAP_FLAG;
                }
            }
            epoch_check();
        }
    } else if (!execute && kind == 0u && va >= ia.code_lo && va < ia.code_hi && in_code_runs(va)) {
        /* A read of a no-access CODE page: recorded, then served for this one
         * instruction. */
        ia_thread *t = thread_slot(1);
        uint32_t page = va & ~(IA_PAGE - 1u);
        if (t && pend(t, page, 1) && protect(page, IA_PAGE, PAGE_READONLY)) {
            record_code_read(c, va);
            c->EFlags |= IA_TRAP_FLAG;
            verdict = EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    ReleaseSRWLockExclusive(&ia.lock);
    return verdict;
}

/* ------------------------------------------------------------------ set-up */

int es2_sim_image_access_configure(const char *path) {
    ia.configured = 0;
    ia.path[0] = 0;
    if (!path || !*path) return 0;
    if (strlen(path) >= sizeof(ia.path)) return 1;
    strcpy(ia.path, path);
    ia.configured = 1;
    return 0;
}

int es2_sim_image_access_configured(void) { return ia.configured; }
int es2_sim_image_access_armed(void) { return ia.armed; }

static uint32_t up(uint32_t x) { return (x + IA_PAGE - 1u) & ~(IA_PAGE - 1u); }

static int add_area(const char *name, uint32_t lo, uint32_t file_hi, int code) {
    if (ia.area_count == IA_MAX_AREAS) return 1;
    ia_area *a = &ia.areas[ia.area_count++];
    snprintf(a->name, sizeof(a->name), "%s", name);
    a->lo = lo; a->file_hi = file_hi; a->hi = up(file_hi); a->code = code;
    return 0;
}

static int add_page(uint32_t va, unsigned area) {
    if (ia.page_count == IA_MAX_PAGES) return 1;
    if (ia.page_count && ia.pages[ia.page_count - 1u].va >= va) return 1;
    es2_os_region m;
    if (!es2_os_virtual_query(va, &m) || m.state != ES2_OS_MEM_COMMIT ||
        (m.protect & (ES2_OS_PAGE_GUARD | ES2_OS_PAGE_NOACCESS)))
        return 1;
    ia_page *p = &ia.pages[ia.page_count++];
    memset(p, 0, sizeof(*p));
    p->va = va;
    p->base = m.protect;
    p->area = area;
    return 0;
}

static int is_site_page(uint32_t page, const uint32_t *sites, unsigned count) {
    for (unsigned i = 0; i < count; ++i)
        if ((sites[i] & ~(IA_PAGE - 1u)) == page) return 1;
    return 0;
}

static void reset(void) {
    ia.page_count = ia.area_count = ia.run_count = ia.code_read_count = 0;
    ia.code_read_overflow = ia.code_read_faults = ia.guard_faults = ia.single_steps = ia.undecoded = ia.epochs = 0;
    ia.foreign_steps = 0;
    ia.paused = 0;
    ia.written = 0;
    memset(ia.threads, 0, sizeof(ia.threads));
}

static int arm_fail(const char *text, const char **error) {
    if (error) *error = text;
    reset();
    return 1;
}

int es2_sim_image_access_arm(const es2_sim_image_info *info, const uint32_t *sites, unsigned site_count, const char **error) {
    if (!ia.configured) return 0;
    if (ia.armed || !info || (site_count && !sites)) return arm_fail("image access: armed twice or no image", error);
    reset();
    uint32_t base = info->image_base;
    /* Sections in VA order (the parse keeps the file's order, which is). */
    if (add_area("headers", base, base + info->headers_size, 0)) return arm_fail("image access: areas", error);
    for (uint32_t va = base; va < up(base + info->headers_size); va += IA_PAGE)
        if (add_page(va, ia.area_count - 1u)) return arm_fail("image access: header page", error);
    for (unsigned s = 0; s < info->section_count; ++s) {
        const es2_sim_section *sec = &info->sections[s];
        uint32_t lo = base + sec->rva;
        if (sec->flags & ES2_SIM_SCN_EXECUTE) {
            uint32_t span = up(sec->virtual_size > sec->raw_size ? sec->virtual_size : sec->raw_size);
            if (sec->rva + span > info->image_size) span = info->image_size - sec->rva;
            if (add_area(sec->name, lo, lo + span, 1)) return arm_fail("image access: areas", error);
            ia.areas[ia.area_count - 1u].file_hi = lo + sec->raw_size;
            if (ia.code_hi) return arm_fail("image access: more than one CODE section", error);
            ia.code_lo = lo;
            ia.code_hi = lo + span;
            for (uint32_t page = lo; page < lo + span; page += IA_PAGE) {
                if (is_site_page(page, sites, site_count)) {
                    if (add_page(page, ia.area_count - 1u)) return arm_fail("image access: table page", error);
                } else {
                    es2_os_region m;
                    if (!es2_os_virtual_query(page, &m) || m.state != ES2_OS_MEM_COMMIT ||
                        (m.protect & ES2_OS_PAGE_GUARD))
                        return arm_fail("image access: CODE page", error);
                    if (ia.run_count && ia.runs[ia.run_count - 1u].hi == page && ia.runs[ia.run_count - 1u].base == m.protect) {
                        ia.runs[ia.run_count - 1u].hi = page + IA_PAGE;
                    } else {
                        if (ia.run_count == IA_MAX_RUNS) return arm_fail("image access: CODE runs", error);
                        ia.runs[ia.run_count++] = (ia_run){page, page + IA_PAGE, m.protect};
                    }
                }
            }
            continue;
        }
        if (!sec->raw_size) continue;                 /* nothing from the file */
        if (add_area(sec->name, lo, lo + sec->raw_size, 0)) return arm_fail("image access: areas", error);
        for (uint32_t page = lo; page < up(lo + sec->raw_size); page += IA_PAGE)
            if (add_page(page, ia.area_count - 1u)) return arm_fail("image access: section page", error);
    }
    for (unsigned i = 0; i < site_count; ++i)
        if (sites[i] < ia.code_lo || sites[i] >= ia.code_hi) return arm_fail("image access: a site outside CODE", error);
    HMODULE self = GetModuleHandleW(NULL);
    const uint8_t *image = (const uint8_t *)self;
    uint32_t pe;
    memcpy(&pe, image + 0x3c, 4);
    uint32_t size_of_image;
    memcpy(&size_of_image, image + pe + 24u + 56u, 4);
    ia.main_lo = (uintptr_t)self;
    ia.main_hi = (uintptr_t)self + size_of_image;
    ia.frequency = es2_os_counter_frequency();
    ia.handler = AddVectoredExceptionHandler(1, access_handler);
    if (!ia.handler) return arm_fail("image access: handler", error);
    ia.armed = 1;
    arm_all();
    return 0;
}

void es2_sim_image_access_pause(void) {
    if (!ia.armed) return;
    AcquireSRWLockExclusive(&ia.lock);
    if (ia.paused++ == 0u) restore_all();
    ReleaseSRWLockExclusive(&ia.lock);
}

void es2_sim_image_access_resume(void) {
    if (!ia.armed) return;
    AcquireSRWLockExclusive(&ia.lock);
    if (ia.paused && --ia.paused == 0u) arm_all();
    ReleaseSRWLockExclusive(&ia.lock);
}

void es2_sim_image_access_disarm(void) {
    if (!ia.armed) return;
    AcquireSRWLockExclusive(&ia.lock);
    restore_all();
    ia.armed = 0;
    ReleaseSRWLockExclusive(&ia.lock);
    RemoveVectoredExceptionHandler(ia.handler);
    ia.handler = NULL;
    ia.code_lo = ia.code_hi = 0;
    reset();
}

/* ------------------------------------------------------------------ queries */

static unsigned popcount_bits(const uint8_t *bits, unsigned from, unsigned to) {
    unsigned n = 0;
    for (unsigned a = from; a < to; ++a) n += (bits[a >> 3] >> (a & 7u)) & 1u;
    return n;
}

int es2_sim_image_access_page(uint32_t va, es2_sim_access_page_stats *out) {
    const ia_page *p = find_page(va);
    if (!p || !out) return 1;
    out->va = p->va;
    out->read_faults = p->read_faults; out->write_faults = p->write_faults;
    out->exec_faults = p->exec_faults; out->rearms = p->rearms;
    out->read_bytes = popcount_bits(p->read_bits, 0, IA_PAGE);
    out->written_bytes = popcount_bits(p->write_bits, 0, IA_PAGE);
    return 0;
}

unsigned es2_sim_image_access_byte(uint32_t va) {
    const ia_page *p = find_page(va);
    if (!p) return 0;
    unsigned o = va - p->va;
    return ((p->read_bits[o >> 3] >> (o & 7u)) & 1u) | (((p->write_bits[o >> 3] >> (o & 7u)) & 1u) << 1);
}

unsigned es2_sim_image_access_code_reads(const es2_sim_code_read **reads) {
    if (reads) *reads = ia.code_reads;
    return ia.code_read_count;
}

/* ------------------------------------------------------------------ the map */

static void put(es2_os_stream *f, int *first, const char *text) { es2_os_fputs(*first ? "" : ",", f); es2_os_fputs(text, f); *first = 0; }

static void put_ranges(es2_os_stream *f, const uint8_t *bits, uint32_t page_va) {
    int first = 1;
    es2_os_fputc('[', f);
    for (unsigned a = 0; a < IA_PAGE;) {
        if (!((bits[a >> 3] >> (a & 7u)) & 1u)) { ++a; continue; }
        unsigned b = a;
        while (b < IA_PAGE && ((bits[b >> 3] >> (b & 7u)) & 1u)) ++b;
        char text[40];
        snprintf(text, sizeof(text), "[\"0x%08lx\",\"0x%08lx\"]", (unsigned long)(page_va + a), (unsigned long)(page_va + b));
        put(f, &first, text);
        a = b;
    }
    es2_os_fputc(']', f);
}

int es2_sim_image_access_write(const char *reason) {
    if (!ia.armed || ia.written) return 0;
    es2_sim_image_access_pause();
    ia.written = 1;
    es2_os_stream *f = es2_os_fopen(ia.path, "wb");
    if (!f) return 1;
    es2_os_fprintf(f, "{\"format\":\"es2sim-image-access/1\",\"reason\":\"%s\",\"method\":{\"code\":\"no-access, read faults served by a single step\","
               "\"guarded\":\"page guard, re-armed after a single step\",\"budget_per_page_and_epoch\":%u,\"epoch_ms\":%u},",
            reason ? reason : "", IA_BUDGET, IA_EPOCH_MS);
    es2_os_fprintf(f, "\"counts\":{\"code_read_faults\":%llu,\"code_read_sites\":%u,\"code_read_overflow\":%llu,\"guard_faults\":%llu,"
               "\"single_steps\":%llu,\"undecoded\":%llu,\"epochs\":%llu,\"foreign_single_steps\":%llu},",
            (unsigned long long)ia.code_read_faults, ia.code_read_count, (unsigned long long)ia.code_read_overflow,
            (unsigned long long)ia.guard_faults, (unsigned long long)ia.single_steps, (unsigned long long)ia.undecoded,
            (unsigned long long)ia.epochs, (unsigned long long)ia.foreign_steps);
    es2_os_fputs("\"code_no_access\":[", f);
    for (unsigned i = 0; i < ia.run_count; ++i)
        es2_os_fprintf(f, "%s[\"0x%08lx\",\"0x%08lx\"]", i ? "," : "", (unsigned long)ia.runs[i].lo, (unsigned long)ia.runs[i].hi);
    es2_os_fputs("],\"code_reads\":[", f);
    for (unsigned i = 0; i < ia.code_read_count; ++i) {
        const es2_sim_code_read *r = &ia.code_reads[i];
        es2_os_fprintf(f, "%s{\"va\":\"0x%08lx\",\"size\":%lu,\"module\":\"%s\",\"eip_offset\":\"0x%lx\",\"caller_offset\":\"0x%lx\",\"count\":%llu}",
                i ? "," : "", (unsigned long)r->va, (unsigned long)r->size, r->module, (unsigned long)r->eip_offset,
                (unsigned long)r->caller_offset, (unsigned long long)r->count);
    }
    es2_os_fputs("],\"areas\":[", f);
    for (unsigned a = 0; a < ia.area_count; ++a) {
        const ia_area *area = &ia.areas[a];
        unsigned watched = 0, read = 0, written = 0, executed = 0, read_file = 0, read_beyond = 0, written_file = 0, written_beyond = 0;
        for (unsigned i = 0; i < ia.page_count; ++i) {
            const ia_page *p = &ia.pages[i];
            if (p->area != a) continue;
            ++watched;
            read += p->read_faults != 0;
            written += p->write_faults != 0;
            executed += p->exec_faults != 0;
            unsigned split = area->file_hi <= p->va ? 0u : area->file_hi - p->va >= IA_PAGE ? IA_PAGE : area->file_hi - p->va;
            read_file += popcount_bits(p->read_bits, 0, split);
            read_beyond += popcount_bits(p->read_bits, split, IA_PAGE);
            written_file += popcount_bits(p->write_bits, 0, split);
            written_beyond += popcount_bits(p->write_bits, split, IA_PAGE);
        }
        es2_os_fprintf(f, "%s{\"name\":\"%s\",\"lo\":\"0x%08lx\",\"hi\":\"0x%08lx\",\"file_hi\":\"0x%08lx\",\"code\":%d,"
                   "\"guarded_pages\":%u,\"pages_read\":%u,\"pages_written\":%u,\"pages_executed\":%u,"
                   "\"bytes_read\":%u,\"bytes_written\":%u,\"bytes_read_beyond_file\":%u,\"bytes_written_beyond_file\":%u}",
                a ? "," : "", area->name, (unsigned long)area->lo, (unsigned long)area->hi, (unsigned long)area->file_hi, area->code,
                watched, read, written, executed, read_file, written_file, read_beyond, written_beyond);
    }
    es2_os_fputs("],\"pages\":[", f);
    for (unsigned i = 0; i < ia.page_count; ++i) {
        const ia_page *p = &ia.pages[i];
        es2_os_fprintf(f, "%s{\"va\":\"0x%08lx\",\"area\":\"%s\",\"read_faults\":%llu,\"write_faults\":%llu,\"exec_faults\":%llu,"
                   "\"rearms\":%llu,\"bytes_read\":%u,\"bytes_written\":%u,\"read\":",
                i ? "," : "", (unsigned long)p->va, ia.areas[p->area].name, (unsigned long long)p->read_faults,
                (unsigned long long)p->write_faults, (unsigned long long)p->exec_faults, (unsigned long long)p->rearms,
                popcount_bits(p->read_bits, 0, IA_PAGE), popcount_bits(p->write_bits, 0, IA_PAGE));
        put_ranges(f, p->read_bits, p->va);
        es2_os_fputs(",\"written\":", f);
        put_ranges(f, p->write_bits, p->va);
        es2_os_fputc('}', f);
    }
    es2_os_fputs("]}\n", f);
    int bad = es2_os_ferror(f);
    if (es2_os_fclose(f)) bad = 1;
    return bad ? 1 : 0;
}
