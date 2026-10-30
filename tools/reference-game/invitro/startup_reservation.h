/* Claims 0x00400000..0x006d7fff for the DBSIM mapping before the CRT runs.
 * Windows places the 16 MiB initial stack at 0x00400000 (on this host, every
 * observed launch); a smaller PE stack lets locale.nls take that address
 * instead. Releasing the initial stack and reserving afterwards races with
 * other threads that get default-size stacks (up to 3 of 4 Bazel shards
 * lost the race under load). So, when the range lies in the bottom of
 * the calling thread's own stack reservation, below every committed stack
 * page, it is used in place: the range is committed later by load_image, and
 * the page above it is committed PAGE_NOACCESS as a barrier, so a stack that
 * ever grew that deep faults instead of overwriting the mapping. The calling
 * thread must stay alive (it only runs CRT startup and joins the worker). A
 * free range is reserved normally. Anything else is refused. */
#define STARTUP_IMAGE_BASE 0x400000u
#ifndef STARTUP_IMAGE_SIZE
#define STARTUP_IMAGE_SIZE 0x2d8000u
#endif
#define STARTUP_BARRIER_SIZE 0x1000u
static BYTE *claim_image_range(void *stack_allocation,DWORD *error,int *in_stack) {
    *in_stack=0;
    BYTE *image=VirtualAlloc((void *)(uintptr_t)STARTUP_IMAGE_BASE,STARTUP_IMAGE_SIZE,MEM_RESERVE,PAGE_NOACCESS);
    if(image) { *error=ERROR_SUCCESS; return image; }
    *error=GetLastError();
    MEMORY_BASIC_INFORMATION region={0};
    if(!VirtualQuery((void *)(uintptr_t)STARTUP_IMAGE_BASE,&region,sizeof(region))) return NULL;
    if(region.AllocationBase!=stack_allocation || region.BaseAddress!=(void *)(uintptr_t)STARTUP_IMAGE_BASE ||
       region.State!=MEM_RESERVE || region.Type!=MEM_PRIVATE ||
       region.RegionSize<STARTUP_IMAGE_SIZE+STARTUP_BARRIER_SIZE) return NULL;
    void *barrier=(void *)(uintptr_t)(STARTUP_IMAGE_BASE+STARTUP_IMAGE_SIZE);
    if(VirtualAlloc(barrier,STARTUP_BARRIER_SIZE,MEM_COMMIT,PAGE_NOACCESS)!=barrier) { *error=GetLastError(); return NULL; }
    *in_stack=1; *error=ERROR_SUCCESS;
    return (BYTE *)(uintptr_t)STARTUP_IMAGE_BASE;
}
