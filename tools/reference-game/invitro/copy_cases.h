/* 0x402628 actor_snapshot and 0x4027fc actor_rollback: actor, manager, up to
 * ten track slots with random empty slots, node arrays of n = 0..40 nodes
 * (more than the 30 the block holds, so the copies run into the following
 * globals as in the original). The global window after 0x4a8cd0 is filled
 * with random bytes per case and compared with the arena; both runs use the
 * own CW3220 memcpy emulation. The window is restored afterwards. */
#define COPY_BLOCK 0xa8cd0u
#define COPY_WINDOW 0x1000u
static BYTE copy_window_initial[COPY_WINDOW],copy_window_before[COPY_WINDOW],copy_window_after[COPY_WINDOW];

static int copy_cases(unsigned index,unsigned limit) {
    const es2_hook_descriptor *d=function_hook_descriptor(index); current_function=d->name;
    const char *override=getenv("ES2_INVITRO_COPY_CASES");
    unsigned cases=case_budget(limit,1000000);if(!limit&&override&&*override)cases=(unsigned)strtoul(override,NULL,10);
    BOOL save=index==ES2_HOOK_INDEX_actor_snapshot;
    BYTE *window=mapped+COPY_BLOCK;
    memcpy(copy_window_initial,window,COPY_WINDOW);
    uint64_t cached=0,tracks=0;
    if(!limit) coverage_enable(1);
    int status=0;
    for(unsigned n=0;n<cases && !status;++n) {
        if(!limit && n==65536) coverage_enable(0);
        random_bytes(advance_arena,sizeof(advance_arena)); advance_used=0;
        random_bytes(window,COPY_WINDOW);
        unsigned nodes=random32()%8==0?random32()%41:random32()%31;
        BYTE *actor=advance_alloc(0x40),*manager=advance_alloc(0x42);
        put32(actor+0x34,advance_va(manager));
        if(random32()%2) put16(actor+0x32,0); else if(!get16(actor+0x32)) put16(actor+0x32,1);
        put16(manager+0x0c,(uint16_t)nodes);
        put32(manager+0x0e,advance_va(advance_alloc(nodes)));
        put32(manager+0x12,advance_va(advance_alloc(12*nodes)));
        put32(manager+0x16,advance_va(advance_alloc(32*nodes)));
        for(unsigned i=0;i<10;++i) {
            if(random32()%3==0) { put32(manager+0x1a+4*i,0); continue; }
            put32(manager+0x1a+4*i,advance_va(advance_alloc(0x52))); ++tracks;
        }
        if(advance_used>sizeof(advance_arena)) { status=fail("copy arena too small"); break; }
        cached+=get16(actor+0x32)!=0;
        uint32_t a[ES2_HOOK_MAX_ARGS]={0},registers[3]={random32(),random32(),random32()};
        a[0]=advance_va(actor);
        memcpy(advance_before,advance_arena,sizeof(advance_arena)); memcpy(copy_window_before,window,COPY_WINDOW);
        uint64_t got=actor_call(index,a,registers);
        memcpy(advance_after,advance_arena,sizeof(advance_arena)); memcpy(copy_window_after,window,COPY_WINDOW);
        memcpy(advance_arena,advance_before,sizeof(advance_arena)); memcpy(window,copy_window_before,COPY_WINDOW);
        uint64_t want=save ? es2_actor_snapshot_registers(&harness_memory,a[0],(uint32_t)(uintptr_t)window,MATH_QUARTER(d->user)) :
                             es2_actor_rollback_registers(&harness_memory,a[0],(uint32_t)(uintptr_t)window,MATH_QUARTER(d->user));
        if(got!=want || memcmp(advance_arena,advance_after,sizeof(advance_arena)) || memcmp(window,copy_window_after,COPY_WINDOW)) {
            unsigned arena_diff=0,window_diff=0;
            while(arena_diff<sizeof(advance_arena) && advance_arena[arena_diff]==advance_after[arena_diff]) ++arena_diff;
            while(window_diff<COPY_WINDOW && window[window_diff]==copy_window_after[window_diff]) ++window_diff;
            fprintf(stderr,"FAIL: %s case %u EAX original=%08x core=%08x EDX original=%08x core=%08x arena_offset=%u window_offset=0x%x nodes=%u\n",
                d->name,n,(uint32_t)got,(uint32_t)want,(uint32_t)(got>>32),(uint32_t)(want>>32),arena_diff,window_diff,nodes);
            status=1;
        }
    }
    if(!limit) coverage_enable(0);
    memcpy(window,copy_window_initial,COPY_WINDOW);
    if(status) return 1;
    printf("PASS: %-20s %u cases; exact EDX:EAX, %u arena bytes and the 0x%x-byte global window; cached rotation=%llu tracks=%llu\n",
        d->name,cases,(unsigned)sizeof(advance_arena),COPY_WINDOW,(unsigned long long)cached,(unsigned long long)tracks);
    fflush(stdout); return 0;
}
