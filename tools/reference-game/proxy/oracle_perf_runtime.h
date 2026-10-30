/* Integration without changes to hook or input-journal implementations. */
#ifndef ES2_ORACLE_PERF_RUNTIME_H
#define ES2_ORACLE_PERF_RUNTIME_H
void __cdecl __real_es2_hook_dispatch_extended(void *frame);
static void (__cdecl *perf_dispatch_target)(void *) __attribute__((used)) = __real_es2_hook_dispatch_extended;
/* With perf absent this is a tail jump: even the dispatcher's stack layout
 * remains unchanged. Instrumentation is selected only after configuration. */
__attribute__((naked)) void __cdecl __wrap_es2_hook_dispatch_extended(void *frame) {
    __asm__ volatile("jmp *_perf_dispatch_target");
}
static void __cdecl perf_dispatch_measured(void *frame) {
    DWORD error = GetLastError(); unsigned phase;
    if(perf_hook_names) {es2_hook *hook=es2_hook_frame_owner(frame);phase=perf_hook_begin(hook,hook->descriptor->name);}
    else phase=perf_begin(PERF_HOOK);
    SetLastError(error);
    __real_es2_hook_dispatch_extended(frame);
    error = GetLastError(); if(perf_hook_names)perf_hook_end(phase);else perf_end(phase); SetLastError(error);
}
static unsigned __cdecl perf_fast_begin(es2_hook *hook) {
    DWORD error = GetLastError();
    unsigned token = perf_hook_names?perf_hook_begin(hook,hook->descriptor->name):perf_begin(PERF_HOOK); SetLastError(error); return token;
}
static void __cdecl perf_fast_end(unsigned token) {
    DWORD error = GetLastError(); if(perf_hook_names)perf_hook_end(token);else perf_end(token); SetLastError(error);
}
#define PERF_INPUT_CALL(TYPE, NAME, PARAMETERS, ARGS) \
static TYPE WINAPI perf_##NAME PARAMETERS { \
    if (!perf_enabled) return NAME ARGS; \
    DWORD error = GetLastError(); unsigned phase = perf_begin(PERF_INPUT); SetLastError(error); \
    TYPE result = NAME ARGS; error = GetLastError(); perf_end(phase); SetLastError(error); return result; \
}
PERF_INPUT_CALL(BOOL, ij_peek_message, (LPMSG m, HWND w, UINT a, UINT b, UINT f), (m,w,a,b,f))
PERF_INPUT_CALL(SHORT, ij_get_key_state, (int k), (k))
PERF_INPUT_CALL(MMRESULT, ij_joy_get_pos, (UINT i, LPJOYINFOEX p), (i,p))
PERF_INPUT_CALL(MMRESULT, ij_joy_get_caps, (UINT_PTR i, LPJOYCAPSA p, UINT n), (i,p,n))
PERF_INPUT_CALL(BOOL, ij_get_cursor_pos, (LPPOINT p), (p))
#undef PERF_INPUT_CALL
static BOOL perf_input_slots(void) {
    void *wrappers[]={perf_ij_peek_message,perf_ij_get_key_state,perf_ij_joy_get_pos,perf_ij_joy_get_caps,perf_ij_get_cursor_pos};
    if (ij_iat_state!=1) return FALSE;
    for (unsigned i=0;i<5;++i) {
        DWORD *slot=(DWORD *)(ij_image+ij_slots[i].iat_rva),old,ignored;
        if (*slot!=(DWORD)(uintptr_t)ij_slots[i].hook || !VirtualProtect(slot,4,PAGE_READWRITE,&old)) return FALSE;
        InterlockedExchange((LONG *)slot,(LONG)(uintptr_t)wrappers[i]);
        if (!VirtualProtect(slot,4,old,&ignored)) return FALSE;
    }
    return TRUE;
}
static void (__cdecl *speed_previous_exit)(int);
static DWORD *speed_exit_slot;
static void speed_on_exit(void) {
#ifdef ES2_DDRAW_OBSERVER_H
    dd_finish();
#endif
    live_coverage_shutdown("exit");
    tick_log_shutdown();
    if (!perf_complete) perf_report("exit", TRUE);
}
static void __cdecl speed_exit(int code) { speed_on_exit(); speed_previous_exit(code); }
static BOOL speed_install_exit_slots(DWORD *exit_slot, DWORD *registration) {
    if (!registration) return FALSE;
    int (__cdecl *register_exit)(void (__cdecl *)(void)) = (void *)(uintptr_t)*registration;
    if (register_exit(speed_on_exit)) return FALSE;
    if (live_enabled || live_exit_slot) {
        /* Keep coverage's verified slot/restoration unchanged. Its immediate
         * exit forwards here; the atexit callback handles ordinary exit. */
        speed_previous_exit = live_previous_exit; live_previous_exit = speed_exit;
        return TRUE;
    }
    speed_exit_slot = exit_slot;
    if (!speed_exit_slot) return FALSE;
    DWORD old, ignored;
    if (!VirtualProtect(speed_exit_slot, 4, PAGE_READWRITE, &old)) return FALSE;
    speed_previous_exit = (void *)(uintptr_t)*speed_exit_slot;
    InterlockedExchange((LONG *)speed_exit_slot, (LONG)(uintptr_t)speed_exit);
    return VirtualProtect(speed_exit_slot, 4, old, &ignored);
}
static BOOL speed_install_exit(BYTE *image) {
    return speed_install_exit_slots(live_import(image,RNG_HOOK_IMAGE_SIZE,"__exit"),
                                   live_import(image,RNG_HOOK_IMAGE_SIZE,"_atexit"));
}
#endif
