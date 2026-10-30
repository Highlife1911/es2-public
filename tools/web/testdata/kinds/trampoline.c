/* Fault trampolines: a handler that continues the faulting thread elsewhere. */
extern void resume(void);
long handler(EXCEPTION_POINTERS *x) {
    CONTEXT *c = x->ContextRecord;
    c->Eip = (DWORD)(uintptr_t)resume;                             /* EXPECT: trampoline */
    x->ContextRecord->Esp -= 4;                                    /* EXPECT: trampoline */
    if (c->Eip == 0 || c->Esp != 0) return 0;
    DWORD eip = c->Eip, esp = c->Esp;
    *(DWORD *)(uintptr_t)c->Esp = eip + esp;                       /* EXPECT: guest-host-cast */
    return -1;
}
/* c->Eip = resume; in a comment */
const char *text = "c->Eip = resume";
