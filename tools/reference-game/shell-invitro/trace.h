/* Optional instruction-address evidence. No original bytes are exported. */
static BYTE trace_seen[0x6b000];
static uint32_t trace_resume, trace_previous;
static BYTE trace_resume_byte;
static int trace_enabled;
/* ES2_SHELL_TRACE=exec keeps only the EXEC set (no per-call EDGE lines). */
static int trace_edges = 1;
static int original_code(uint32_t a) { return a >= 0x401000 && a < 0x46bb00; }
static void trace_patch(uint32_t a, BYTE value) {
  DWORD old;
  if (!VirtualProtect(P(a), 1, PAGE_EXECUTE_READWRITE, &old))
    die("trace protection");
  *P(a) = value;
  VirtualProtect(P(a), 1, old, &old);
  FlushInstructionCache(GetCurrentProcess(), P(a), 1);
}
static void trace_arm(uint32_t a) {
  trace_resume = a;
  trace_resume_byte = *P(a);
  trace_patch(a, 0xcc);
}
static void trace_before(uint32_t a) {
  if (trace_enabled)
    trace_arm(a);
}
static int trace_fault(EXCEPTION_POINTERS *e) {
  if (!trace_enabled)
    return 0;
  CONTEXT *c = e->ContextRecord;
  DWORD kind = e->ExceptionRecord->ExceptionCode;
  if (kind == EXCEPTION_BREAKPOINT &&
      (uintptr_t)e->ExceptionRecord->ExceptionAddress == trace_resume) {
    trace_patch(trace_resume, trace_resume_byte);
    c->Eip = trace_resume;
    trace_resume = 0;
  } else if (kind != EXCEPTION_SINGLE_STEP)
    return 0;
  uint32_t a = c->Eip;
  if (trace_previous) {
    BYTE *p = P(trace_previous);
    if (trace_edges && (*p == 0xe8 || (*p == 0xff && ((p[1] >> 3) & 7) == 2)))
      fprintf(stderr, "EDGE %08x %08x\n", trace_previous, a);
  }
  trace_previous = 0;
  if (original_code(a)) {
    trace_seen[a - 0x401000] = 1;
    trace_previous = a;
    c->EFlags |= 0x100;
  } else {
    c->EFlags &= ~0x100u;
    uint32_t back = get32(P(c->Esp));
    if (original_code(back))
      trace_arm(back);
  }
  return 1;
}
static void trace_finish(void) {
  if (!trace_enabled)
    return;
  for (uint32_t n = 0; n < sizeof(trace_seen); n++)
    if (trace_seen[n])
      fprintf(stderr, "EXEC %08x\n", n + 0x401000);
  trace_enabled = 0;
}
