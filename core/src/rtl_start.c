/* The CW3220 runtime lifecycle (start-up, exit paths, atexit, abort); contract
 * in docs/reference-game/rtl_imports.md ("the runtime lifecycle"). Addresses in
 * comments are CW3220.DLL VAs at its link base 0x400000.
 *
 * Section "the lifecycle group as own code": the registers and frame words
 * of the original's code at
 * every guest and Win32 call, for an env that runs the lifecycle on a real
 * stack (proxy/rtl_life.h). Return addresses are CW3220 RVAs (VA - 0x400000);
 * painted words that hold one are code_base + RVA. */
#include "es2/core/rtl_start.h"

#include <string.h>

void es2_rtl_runtime_init(es2_rtl_runtime *rt, uint32_t dummy_hook, uint32_t abort_message)
{
    memset(rt, 0, sizeof(*rt));
    rt->exitbuf = rt->exitfopen = rt->exitopen = dummy_hook;
    rt->abort_message = abort_message;
}

/* One stack word of the original's frames (only with env->write32). */
static void paint(const es2_rtl_start_env *env, uint32_t address, uint32_t value)
{
    if (env->write32) env->write32(env->context, address, value);
}

/* Calls a guest function with up to four arguments at `esp` and the
 * registers `cpu`, which then hold the callee's (EAX = its result); returns
 * the number of argument bytes it removed. */
static uint32_t invoke(const es2_rtl_start_env *env, es2_rtl_cpu *cpu, es2_rtl_call_kind kind, uint32_t record, uint32_t return_rva,
                       uint32_t target, uint32_t esp, uint32_t arg_count, const uint32_t *args, uint32_t *result)
{
    es2_rtl_guest_call call;
    memset(&call, 0, sizeof(call));
    call.kind = kind;
    call.target = target;
    call.esp = esp;
    call.arg_count = arg_count;
    for (uint32_t i = 0; i < arg_count && i < 4u; ++i) {
        call.args[i] = args[i];
        paint(env, esp + 4u + 4u * i, args[i]);
    }
    call.record = record;
    call.return_rva = return_rva;
    call.regs = *cpu;
    call.out = *cpu;
    env->call(env->context, &call);
    *cpu = call.out;
    cpu->eax = call.result;
    if (result) *result = call.result;
    return call.popped;
}

/* Announces the next Win32 call (env->site) with the registers `cpu`; the
 * env may replace site->cpu with the registers the API returns. */
static void announce(const es2_rtl_start_env *env, es2_rtl_site *site, es2_rtl_api api, uint32_t esp, uint32_t return_rva,
                     uint32_t guest, const es2_rtl_cpu *cpu)
{
    site->api = api;
    site->esp = esp;
    site->return_rva = return_rva;
    site->guest = guest;
    site->cpu = *cpu;
    if (env->site) env->site(env->context, site);
}
static uint32_t returned(es2_rtl_cpu *cpu, const es2_rtl_site *site, uint32_t eax)
{
    *cpu = site->cpu;
    cpu->eax = eax;
    return eax;
}

/* 0x41a634: one pass over the module list. The threshold starts at 0xffff
 * for init records and 0 for exit records; a pending record replaces the
 * current choice when its priority is below the threshold (init) or at
 * least the threshold (exit), and its priority becomes the threshold. The
 * scan of a table stops at its end or at a record whose function is 0.
 * `chosen_record` gets the record (the original returns its function in EAX
 * and the record in ECX and EDX). */
static uint32_t select_record(const es2_rtl_runtime *rt, const es2_rtl_start_env *env, uint32_t mode, uint32_t *chosen_record)
{
    void *c = env->context;
    uint32_t threshold = mode ? 0u : 0xffffu, chosen = 0;
    /* The list holds at most the one table __startup stores (0x423f24). */
    for (uint32_t i = 0; i < rt->module_count && i < 1u; ++i) {
        uint32_t table = rt->module_table;
        uint32_t record = env->read32(c, table + (mode ? ES2_RTL_TABLE_EXIT_START : ES2_RTL_TABLE_INIT_START));
        uint32_t end = env->read32(c, table + (mode ? ES2_RTL_TABLE_EXIT_END : ES2_RTL_TABLE_INIT_END));
        for (; record < end && env->read32(c, record + 2u) != 0u; record += ES2_RTL_RECORD_SIZE) {
            if (env->read8(c, record) != 0u) continue;
            uint32_t priority = env->read8(c, record + 1u);
            uint32_t at_least = priority >= (threshold & 0xffffu) ? 1u : 0u;
            if (mode == at_least) {
                threshold = priority;
                chosen = record;
            }
        }
    }
    *chosen_record = chosen;
    if (!chosen) return 0;
    env->write8(c, chosen, 1u);
    return env->read32(c, chosen + 2u);
}
uint32_t es2_rtl_select_record(const es2_rtl_runtime *rt, const es2_rtl_start_env *env, uint32_t mode)
{
    uint32_t record;
    return select_record(rt, env, mode, &record);
}
/* The registers the selector leaves when it found nothing: EAX 0, ECX the
 * mode, EDX the starting threshold. */
static void selected_nothing(es2_rtl_cpu *cpu, uint32_t mode)
{
    cpu->eax = 0;
    cpu->ecx = mode;
    cpu->edx = mode ? 0u : 0xffffu;
}

/* 0x41a7bb..0x41a800: skip blanks, the program name (quoted: up to the
 * closing quote, a tab or the end; else up to a blank, a tab or the end),
 * a closing quote, then blanks and tabs. `quoted`: the name began with a
 * quote (DL = '"' rather than ' ' afterwards). */
static uint32_t command_tail(const es2_rtl_start_env *env, uint32_t p, int *quoted)
{
    void *c = env->context;
    uint8_t byte;
    while ((byte = env->read8(c, p)) == ' ' || byte == '\t') ++p;
    uint8_t stop = ' ';
    if (env->read8(c, p) == '"') {
        stop = '"';
        ++p;
    }
    *quoted = stop == '"';
    while ((byte = env->read8(c, p)) != 0u && byte != stop && byte != '\t') ++p;
    if (env->read8(c, p) == '"') ++p;
    while ((byte = env->read8(c, p)) == ' ' || byte == '\t') ++p;
    return p;
}
uint32_t es2_rtl_command_tail(const es2_rtl_start_env *env, uint32_t p)
{
    int quoted;
    return command_tail(env, p, &quoted);
}

uint32_t es2_rtl_atexit(es2_rtl_runtime *rt, uint32_t function)
{
    if (rt->atexit_count == ES2_RTL_ATEXIT_LIMIT) return 1u;
    rt->atexit_table[rt->atexit_count % ES2_RTL_ATEXIT_LIMIT] = function;
    ++rt->atexit_count;
    return 0;
}

/* A stream hook at call time: CW3220's variable when it lives in guest
 * memory (the stream code installs the hooks lazily). */
static uint32_t stream_hook(const es2_rtl_runtime *rt, const es2_rtl_start_env *env, unsigned which)
{
    if (rt->stream_hooks) return env->read32(env->context, rt->stream_hooks + 4u * which);
    return which == 0 ? rt->exitbuf : which == 1 ? rt->exitfopen : rt->exitopen;
}

/* 0x4191a4 with `frame` = ESP at its entry and `cpu` the registers there. */
static int exit_common(es2_rtl_runtime *rt, const es2_rtl_start_env *env, es2_rtl_cpu *cpu, uint32_t frame, uint32_t quick,
                       uint32_t keep_running, uint32_t status)
{
    /* push ebp; mov ebp, esp; push ebx; mov ebx, [ebp+8] */
    paint(env, frame - 0x4u, cpu->ebp);
    paint(env, frame - 0x8u, cpu->ebx);
    cpu->ebp = frame - 0x4u;
    cpu->ebx = quick;
    if (!quick) {
        /* atexit functions, last registered first; the count is re-read
         * after every call (a function may register another). Each runs
         * with EAX = the new count and EDX = the function (4191b1..4191c3). */
        while (rt->atexit_count) {
            --rt->atexit_count;
            uint32_t function = rt->atexit_table[rt->atexit_count % ES2_RTL_ATEXIT_LIMIT];
            cpu->eax = rt->atexit_count;
            cpu->edx = function;
            invoke(env, cpu, ES2_RTL_CALL_ATEXIT, 0, 0x191c5u, function, frame - 0xcu, 0, NULL, NULL);
            if (rt->exited) return ES2_RTL_EXITED;
        }
        /* 0x41a848 (push ebx; push esi; push edi): the exit records of the
         * module list, each with EAX = EBX = its function and ECX = EDX =
         * the record; CW3220's second list (0x423f04) is never filled and
         * the DLL list (0x41a630) is always empty. */
        paint(env, frame - 0xcu, rt->code_base + 0x191d3u);
        paint(env, frame - 0x10u, cpu->ebx);
        paint(env, frame - 0x14u, cpu->esi);
        paint(env, frame - 0x18u, cpu->edi);
        const es2_rtl_cpu saved = *cpu;
        for (uint32_t f, record; (f = select_record(rt, env, 1u, &record)) != 0u;) {
            cpu->eax = cpu->ebx = f;
            cpu->ecx = cpu->edx = record;
            invoke(env, cpu, ES2_RTL_CALL_EXIT_RECORD, record, 0x1a84fu, f, frame - 0x1cu, 0, NULL, NULL);
            if (rt->exited) return ES2_RTL_EXITED;
        }
        /* Both lists empty, 0x41a630 returns 0; pop edi; pop esi; pop ebx. */
        selected_nothing(cpu, 1u);
        cpu->ebx = saved.ebx;
        cpu->esi = saved.esi;
        cpu->edi = saved.edi;
        invoke(env, cpu, ES2_RTL_CALL_EXITBUF, 0, 0x191d9u, stream_hook(rt, env, 0), frame - 0xcu, 0, NULL, NULL);
        if (rt->exited) return ES2_RTL_EXITED;
    }
    if (keep_running) return ES2_RTL_RETURNED;
    if (!quick) {
        invoke(env, cpu, ES2_RTL_CALL_EXITFOPEN, 0, 0x191e9u, stream_hook(rt, env, 1), frame - 0xcu, 0, NULL, NULL);
        if (rt->exited) return ES2_RTL_EXITED;
        invoke(env, cpu, ES2_RTL_CALL_EXITOPEN, 0, 0x191efu, stream_hook(rt, env, 2), frame - 0xcu, 0, NULL, NULL);
        if (rt->exited) return ES2_RTL_EXITED;
    }
    /* mov ecx, [ebp+10h]; push ecx; call 41a8c0: push ebp; mov ebp, esp;
     * mov eax, [ebp+8]; push eax; call ExitProcess (returning to 41a8cc). */
    paint(env, frame - 0xcu, status);
    paint(env, frame - 0x10u, rt->code_base + 0x191f8u);
    paint(env, frame - 0x14u, cpu->ebp);
    paint(env, frame - 0x18u, status);
    cpu->ecx = status;
    cpu->ebp = frame - 0x14u;
    cpu->eax = status;
    es2_rtl_site site;
    announce(env, &site, ES2_RTL_API_EXIT_PROCESS, frame - 0x1cu, 0x1a8ccu, 0, cpu);
    env->exit_process(env->context, status, frame - 0x1cu);
    rt->exited = 1;
    return ES2_RTL_EXITED;
}

/* exit and _exit push ebp and three arguments; _cexit and _c_exit only the
 * arguments. Each call of 0x4191a4 returns into its export. */
int es2_rtl_exit(es2_rtl_runtime *rt, const es2_rtl_start_env *env, uint32_t esp, uint32_t status)
{
    es2_rtl_cpu cpu = rt->entry;
    paint(env, esp - 0x4u, cpu.ebp);
    paint(env, esp - 0x8u, status);
    paint(env, esp - 0xcu, 0);
    paint(env, esp - 0x10u, 0);
    paint(env, esp - 0x14u, rt->code_base + 0x1920cu);
    cpu.ebp = esp - 0x4u;
    cpu.eax = status;
    return exit_common(rt, env, &cpu, esp - 0x14u, 0, 0, status);
}
int es2_rtl_quick_exit(es2_rtl_runtime *rt, const es2_rtl_start_env *env, uint32_t esp, uint32_t status)
{
    es2_rtl_cpu cpu = rt->entry;
    paint(env, esp - 0x4u, cpu.ebp);
    paint(env, esp - 0x8u, status);
    paint(env, esp - 0xcu, 0);
    paint(env, esp - 0x10u, 1u);
    paint(env, esp - 0x14u, rt->code_base + 0x19224u);
    cpu.ebp = esp - 0x4u;
    cpu.eax = status;
    return exit_common(rt, env, &cpu, esp - 0x14u, 1u, 0, status);
}
int es2_rtl_cexit(es2_rtl_runtime *rt, const es2_rtl_start_env *env, uint32_t esp)
{
    es2_rtl_cpu cpu = rt->entry;
    paint(env, esp - 0x4u, 0);
    paint(env, esp - 0x8u, 1u);
    paint(env, esp - 0xcu, 0);
    paint(env, esp - 0x10u, rt->code_base + 0x19237u);
    return exit_common(rt, env, &cpu, esp - 0x10u, 0, 1u, 0);
}
int es2_rtl_c_exit(es2_rtl_runtime *rt, const es2_rtl_start_env *env, uint32_t esp)
{
    es2_rtl_cpu cpu = rt->entry;
    paint(env, esp - 0x4u, 0);
    paint(env, esp - 0x8u, 1u);
    paint(env, esp - 0xcu, 1u);
    paint(env, esp - 0x10u, rt->code_base + 0x19247u);
    return exit_common(rt, env, &cpu, esp - 0x10u, 1u, 1u, 0);
}

/* 0x418ee4 (the GUI branch: CW3220's byte 0x41f069 is constant 1) with
 * `cpu` the registers at its entry; afterwards EAX/ECX/EDX are
 * MessageBoxA's. The name buffer is ebp-54h = esp-58h; 0x418e9c keeps the
 * window flag at esp-68h. */
static void error_message(const es2_rtl_start_env *env, uint32_t code_base, uint32_t esp, uint32_t text, es2_rtl_cpu *cpu)
{
    void *c = env->context;
    char name[ES2_RTL_FILE_NAME_SIZE + 1u];
    memset(name, 0, sizeof(name));
    const uint32_t buffer = esp - 0x58u, flag = esp - 0x68u;
    es2_rtl_cpu inner = *cpu;
    es2_rtl_site site;
    /* push ebp; mov ebp, esp; add esp, -54h; push ebx; push esi; mov esi, [ebp+8];
     * push 50h; lea eax, [ebp-54h]; push eax; push 0; call GetModuleFileNameA */
    inner.ebp = esp - 0x4u;
    inner.esi = text;
    inner.eax = buffer;
    paint(env, esp - 0x64u, ES2_RTL_FILE_NAME_SIZE);
    paint(env, esp - 0x68u, buffer);
    paint(env, esp - 0x6cu, 0);
    announce(env, &site, ES2_RTL_API_MODULE_FILE_NAME, esp - 0x70u, 0x18f05u, buffer, &inner);
    returned(&inner, &site, env->module_file_name(c, name, ES2_RTL_FILE_NAME_SIZE));
    const char *title = strrchr(name, '\\');
    if (!title) title = strrchr(name, ':');
    title = title ? title + 1 : name;
    const uint32_t title_address = buffer + (uint32_t)(title - name);
    inner.ebx = title_address;
    /* 0x418e9c (call at 418f33): push ecx; [esp] = 0; call GetVersion. */
    paint(env, esp - 0x64u, code_base + 0x18f38u);
    paint(env, flag, 0);
    inner.eax = 0;
    announce(env, &site, ES2_RTL_API_VERSION, esp - 0x6cu, 0x18ea7u, 0, &inner);
    uint32_t version = returned(&inner, &site, env->version(c));
    /* 0x418e9c: task-modal, except on a Win32s/9x version word (bit 31)
     * when the thread has no window: system-modal. */
    uint32_t modality = 0x2000u, window = 0;
    if (version & 0x80000000u) {
        /* mov edx, esp; push edx; push offset callback; call GetCurrentThreadId */
        inner.edx = flag;
        paint(env, esp - 0x6cu, flag);
        paint(env, esp - 0x70u, code_base + ES2_RTL_ENUM_PROC_RVA);
        announce(env, &site, ES2_RTL_API_THREAD_ID, esp - 0x74u, 0x18ecau, 0, &inner);
        uint32_t thread = returned(&inner, &site, env->thread_id(c));
        /* push eax; call EnumThreadWindows: the callback sets the flag. */
        paint(env, esp - 0x74u, thread);
        announce(env, &site, ES2_RTL_API_ENUM_THREAD_WINDOWS, esp - 0x78u, 0x18ed0u, flag, &inner);
        if (!env->thread_has_window(c, thread)) modality = 0x1000u;
        window = modality == 0x2000u;
        inner = site.cpu;
    }
    /* mov eax, 2000h; (add eax, -1000h); pop ecx (the flag); ret; or eax,
     * 10010h; push eax; push ebx; push esi; push 0; call MessageBoxA */
    inner.ecx = window;
    inner.eax = modality | 0x10010u;
    paint(env, esp - 0x64u, modality | 0x10010u);
    paint(env, esp - 0x68u, title_address);
    paint(env, esp - 0x6cu, text);
    paint(env, esp - 0x70u, 0);
    announce(env, &site, ES2_RTL_API_MESSAGE_BOX, esp - 0x74u, 0x18f47u, title_address, &inner);
    env->message_box(c, text, title, modality | 0x10010u, esp - 0x74u);
    /* pop esi; pop ebx; mov esp, ebp; pop ebp: EAX/ECX/EDX as MessageBoxA left them. */
    cpu->eax = site.cpu.eax;
    cpu->ecx = site.cpu.ecx;
    cpu->edx = site.cpu.edx;
}
void es2_rtl_error_message(const es2_rtl_start_env *env, uint32_t esp, uint32_t text)
{
    es2_rtl_cpu cpu;
    memset(&cpu, 0, sizeof(cpu));
    error_message(env, 0, esp, text, &cpu);
}

int es2_rtl_error_exit(es2_rtl_runtime *rt, const es2_rtl_start_env *env, uint32_t esp, uint32_t message)
{
    es2_rtl_cpu cpu = rt->entry;
    /* 418fa4: push ebp; mov ebp, esp; mov eax, [ebp+8]; push eax; call __ErrorMessage */
    paint(env, esp - 0x4u, cpu.ebp);
    paint(env, esp - 0x8u, message);
    paint(env, esp - 0xcu, rt->code_base + 0x18fb0u);
    cpu.ebp = esp - 0x4u;
    cpu.eax = message;
    error_message(env, rt->code_base, esp - 0xcu, message, &cpu);
    /* pop ecx; push 1; call __exit */
    cpu.ecx = message;
    paint(env, esp - 0x8u, ES2_RTL_ERROR_STATUS);
    paint(env, esp - 0xcu, rt->code_base + 0x18fb8u);
    rt->entry = cpu;
    return es2_rtl_quick_exit(rt, env, esp - 0xcu, ES2_RTL_ERROR_STATUS);
}

/* 0x419060: raise(SIGABRT) (0x41a2f0), then the message and _exit(3)
 * (0x41904c). With the default handler raise itself takes that last step
 * from deeper in the stack. */
int es2_rtl_abort(es2_rtl_runtime *rt, const es2_rtl_start_env *env, uint32_t esp)
{
    void *c = env->context;
    es2_rtl_cpu cpu = rt->entry;
    const es2_rtl_cpu entry = cpu;
    /* push 16h; call _raise: push ebp; mov ebp, esp; push ebx; push esi; mov
     * esi, [ebp+8]; push esi; call 41a0d8 (EAX = the index 9, EDX = its
     * entry of the signal table); pop ecx; mov ebx, eax */
    paint(env, esp - 0x4u, ES2_RTL_SIGABRT);
    paint(env, esp - 0x8u, rt->code_base + 0x19067u);
    paint(env, esp - 0xcu, cpu.ebp);
    paint(env, esp - 0x10u, cpu.ebx);
    paint(env, esp - 0x14u, cpu.esi);
    paint(env, esp - 0x18u, ES2_RTL_SIGABRT);
    cpu.ebp = esp - 0xcu;
    cpu.esi = cpu.ecx = ES2_RTL_SIGABRT;
    cpu.ebx = ES2_RTL_SIGABRT_INDEX;
    cpu.edx = rt->code_base + ES2_RTL_SIGNAL_TABLE_RVA + 4u * ES2_RTL_SIGABRT_INDEX;
    uint32_t handler = rt->sigabrt_slot ? env->read32(c, rt->sigabrt_slot) : rt->sigabrt;
    cpu.eax = handler;
    if (handler == 0u) {
        /* The default: esi counts down to 0; call 41904c: push offset
         * message; call __ErrorMessage; pop ecx; push 3; call __exit. */
        cpu.esi = 0;
        paint(env, esp - 0x18u, rt->code_base + 0x1a334u);
        paint(env, esp - 0x1cu, rt->abort_message);
        paint(env, esp - 0x20u, rt->code_base + 0x19056u);
        error_message(env, rt->code_base, esp - 0x20u, rt->abort_message, &cpu);
        cpu.ecx = rt->abort_message;
        paint(env, esp - 0x1cu, ES2_RTL_ABORT_STATUS);
        paint(env, esp - 0x20u, rt->code_base + 0x1905eu);
        rt->entry = cpu;
        return es2_rtl_quick_exit(rt, env, esp - 0x20u, ES2_RTL_ABORT_STATUS);
    }
    if (handler != 1u) {
        /* xor edx, edx; mov [entry], edx; xor edx, edx; mov dl, [extra] */
        rt->sigabrt = 0;
        if (rt->sigabrt_slot && env->write32) env->write32(c, rt->sigabrt_slot, rt->sigabrt);
        uint8_t extra = rt->sigabrt_extra_slot ? env->read8(c, rt->sigabrt_extra_slot) : rt->sigabrt_extra;
        cpu.edx = extra;
        uint32_t args[2] = {ES2_RTL_SIGABRT, extra};
        if (extra) invoke(env, &cpu, ES2_RTL_CALL_SIGNAL, 0, 0x1a359u, handler, esp - 0x20u, 2u, args, NULL);
        else invoke(env, &cpu, ES2_RTL_CALL_SIGNAL, 0, 0x1a361u, handler, esp - 0x1cu, 1u, args, NULL);
        if (rt->exited) return ES2_RTL_EXITED;
    }
    /* raise: (pop ecx or add esp, 8;) xor eax, eax; pop esi; pop ebx; pop
     * ebp. abort: pop ecx; call 41904c: push offset message; call
     * __ErrorMessage; pop ecx; push 3; call __exit. */
    cpu.eax = 0;
    cpu.ebx = entry.ebx;
    cpu.esi = entry.esi;
    cpu.ebp = entry.ebp;
    cpu.ecx = ES2_RTL_SIGABRT;
    paint(env, esp - 0x4u, rt->code_base + 0x1906du);
    paint(env, esp - 0x8u, rt->abort_message);
    paint(env, esp - 0xcu, rt->code_base + 0x19056u);
    error_message(env, rt->code_base, esp - 0xcu, rt->abort_message, &cpu);
    cpu.ecx = rt->abort_message;
    paint(env, esp - 0x8u, ES2_RTL_ABORT_STATUS);
    paint(env, esp - 0xcu, rt->code_base + 0x1905eu);
    rt->entry = cpu;
    return es2_rtl_quick_exit(rt, env, esp - 0xcu, ES2_RTL_ABORT_STATUS);
}

/* 0x41a6e0. */
int es2_rtl_startup(es2_rtl_runtime *rt, const es2_rtl_start_env *env, uint32_t esp, uint32_t table)
{
    void *c = env->context;
    es2_rtl_cpu cpu = rt->entry;
    es2_rtl_site site;
    /* push ebp; mov ebp, esp; add esp, -0ch; push ebx; push esi; push edi;
     * mov esi, [ebp+8]. The local at E-10h is never written. */
    paint(env, esp - 0x4u, cpu.ebp);
    paint(env, esp - 0x14u, cpu.ebx);
    paint(env, esp - 0x18u, cpu.esi);
    paint(env, esp - 0x1cu, cpu.edi);
    cpu.ebp = esp - 0x4u;
    cpu.esi = table;
    rt->gui = env->read32(c, table + ES2_RTL_TABLE_FLAGS) & 1u;
    /* __fpreset: fninit, then _control87(default, 0x1fff) over the word
     * fninit left. */
    env->fpu_reset(c, (uint16_t)((ES2_RTL_FPU_DEFAULT & ES2_RTL_FPU_MASK) | (ES2_RTL_FPU_FNINIT & ~ES2_RTL_FPU_MASK)));
    rt->matherrl = env->read32(c, table + ES2_RTL_TABLE_MATHERRL);
    rt->matherr = env->read32(c, table + ES2_RTL_TABLE_MATHERR);
    rt->fmode = env->read32(c, table + ES2_RTL_TABLE_FMODE);
    rt->module_count = 1u;
    rt->module_table = table;
    /* The SEH record in __startup's frame (ebp - 8). */
    rt->frame_record = esp - 0xcu;
    env->register_frame(c, rt->frame_record, ES2_RTL_FRAME_HANDLER_RVA);
    /* 405274 leaves EAX = ECX = the record and EDX = 0. */
    cpu.eax = cpu.ecx = rt->frame_record;
    cpu.edx = 0;
    announce(env, &site, ES2_RTL_API_ENVIRONMENT_STRINGS, esp - 0x20u, 0x1a739u, 0, &cpu);
    rt->environment = returned(&cpu, &site, env->environment_strings(c));
    announce(env, &site, ES2_RTL_API_COMMAND_LINE, esp - 0x20u, 0x1a743u, 0, &cpu);
    rt->command_line = returned(&cpu, &site, env->command_line(c));
    /* 4192a0 (ret); 41a62c (xor eax, eax); mov edi, eax. */
    cpu.eax = cpu.edi = 0;
    for (uint32_t f, record; (f = select_record(rt, env, 0, &record)) != 0u;) {
        cpu.eax = cpu.ebx = f;
        cpu.ecx = cpu.edx = record;
        invoke(env, &cpu, ES2_RTL_CALL_INIT_RECORD, record, 0x1a7a0u, f, esp - 0x20u, 0, NULL, NULL);
        if (rt->exited) return ES2_RTL_EXITED;
    }
    selected_nothing(&cpu, 0);
    cpu.ebx = 0;   /* mov ebx, eax */
    uint32_t result, popped, exit_return;
    if (env->read8(c, table + ES2_RTL_TABLE_FLAGS) & 1u) {
        /* The scan leaves EBX = lpCmdLine, AL = its first byte, DL = the
         * name's end character; push 0ah; push ebx; push 0; push 0; call
         * GetModuleHandleA; push eax; call [esi+18h]. */
        int quoted;
        uint32_t tail = command_tail(env, rt->command_line, &quoted);
        cpu.ebx = tail;
        cpu.eax = env->read8(c, tail);
        cpu.edx = (cpu.edx & ~0xffu) | (quoted ? 0x22u : 0x20u);
        paint(env, esp - 0x20u, ES2_RTL_SHOW_DEFAULT);
        paint(env, esp - 0x24u, tail);
        paint(env, esp - 0x28u, 0);
        paint(env, esp - 0x2cu, 0);
        announce(env, &site, ES2_RTL_API_MODULE_HANDLE, esp - 0x30u, 0x1a80eu, 0, &cpu);
        uint32_t instance = returned(&cpu, &site, env->module_handle(c, 0));
        uint32_t args[4] = {instance, 0, tail, ES2_RTL_SHOW_DEFAULT};
        popped = invoke(env, &cpu, ES2_RTL_CALL_WINMAIN, 0, 0x1a812u, env->read32(c, table + ES2_RTL_TABLE_MAIN), esp - 0x30u, 4u, args, &result);
        exit_return = 0x1a81bu;
    } else {
        /* mov edx, [envp]; push edx; mov ecx, [argv]; push ecx; mov eax, [argc]; push eax; call [esi+18h] */
        cpu.edx = rt->envp;
        cpu.ecx = rt->argv;
        cpu.eax = rt->argc;
        uint32_t args[3] = {rt->argc, rt->argv, rt->envp};
        popped = invoke(env, &cpu, ES2_RTL_CALL_MAIN, 0, 0x1a835u, env->read32(c, table + ES2_RTL_TABLE_MAIN), esp - 0x2cu, 3u, args, &result);
        exit_return = 0x1a83eu;
    }
    if (rt->exited) return ES2_RTL_EXITED;
    /* The caller cleans the arguments again after a stdcall WinMain, so exit
     * runs higher on the stack by the bytes the callee removed; push eax;
     * call exit. */
    paint(env, esp - 0x24u + popped + 4u, result);
    paint(env, esp - 0x24u + popped, rt->code_base + exit_return);
    rt->entry = cpu;
    return es2_rtl_exit(rt, env, esp - 0x24u + popped, result);
}
