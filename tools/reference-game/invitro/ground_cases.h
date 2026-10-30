/* 0x41a360 ground_update with all nine callees replaced by stubs in the
 * private mapping (snapshot, ground animation, terrain height, collision,
 * rollback, RNG, sound, clip, effects). The stubs log every call with its
 * arguments, change the actor from a per-case script (position, on-ground
 * byte A+0xa3, which 41a360 re-reads) and return script values (collision
 * yes/no in AX). Core reaches the same stubs through the proxy callbacks,
 * including their counters. Both runs must give
 * the same call log, arena, globals (0x4a0be0, 0x4a9cd0..0x4a9ce7) and
 * EDX:EAX for random entry registers. */
static uint32_t ground_script[64],ground_log[128];
static unsigned ground_next,ground_logged;
static uint32_t ground_take(void) { return ground_script[ground_next++%64]; }
static void ground_note(uint32_t value) { if(ground_logged<128) ground_log[ground_logged++]=value; }
static uint64_t ground_pair(void) { uint32_t eax=ground_take(); return ((uint64_t)ground_take()<<32)|eax; }
static void ground_place(uint32_t actor) {
    BYTE *a=(BYTE *)(uintptr_t)actor;
    put32(a+0x26,ground_take()); put32(a+0x2a,ground_take()); put32(a+0x2e,ground_take());
}
static uint64_t __cdecl ground_snapshot(uint32_t a) { ground_note(1); ground_note(a); put32((BYTE *)(uintptr_t)a+0x40,ground_take()); return ground_pair(); }
static uint64_t __cdecl ground_animate(uint32_t a) {
    ground_note(2); ground_note(a); ground_place(a);
    if(ground_take()%4==0) ((BYTE *)(uintptr_t)a)[0xa3]^=1;
    return ground_pair();
}
static uint64_t __cdecl ground_height(uint32_t terrain,uint32_t point) {
    const BYTE *p=(const BYTE *)(uintptr_t)point;
    ground_note(3); ground_note(terrain); ground_note(get32(p)); ground_note(get32(p+4)); ground_note(get32(p+8));
    return ground_pair();
}
static uint64_t __cdecl ground_collide(uint32_t a) {
    ground_note(4); ground_note(a);
    uint64_t r=ground_pair(); if(ground_take()%2) r&=~(uint64_t)0xffffu; return r;
}
static uint64_t __cdecl ground_rollback(uint32_t a) { ground_note(5); ground_note(a); ground_place(a); return ground_pair(); }
static uint64_t __cdecl ground_random(uint32_t g) { ground_note(6); ground_note(g); return ground_pair(); }
static uint64_t __cdecl ground_sound(uint32_t id) { ground_note(7); ground_note(id); return ground_pair(); }
static uint64_t __cdecl ground_clip(uint32_t t,uint32_t clip,uint32_t frame,uint32_t phase) {
    ground_note(8); ground_note(t); ground_note((uint16_t)clip); ground_note(frame); ground_note(phase); return ground_pair();
}
static uint64_t __cdecl ground_effects(uint32_t a) { ground_note(9); ground_note(a); return ground_pair(); }

static const struct { uint32_t rva; void *stub; } ground_stubs[]={
    {0x02628,(void *)ground_snapshot},{0x18f40,(void *)ground_animate},{0x6e180,(void *)ground_height},
    {0x18f74,(void *)ground_collide},{0x027fc,(void *)ground_rollback},{0x92ee0,(void *)ground_random},
    {0x62830,(void *)ground_sound},{0x792ac,(void *)ground_clip},{0x195c8,(void *)ground_effects}};
#define GROUND_STUBS (sizeof(ground_stubs)/sizeof(ground_stubs[0]))
static const uint32_t ground_globals[][2]={{0xa0be0,4},{0xa9cd0,24}};

static int ground_cases(unsigned index,unsigned limit) {
    const es2_hook_descriptor *d=function_hook_descriptor(index); current_function=d->name;
    const char *override=getenv("ES2_INVITRO_GROUND_CASES");
    unsigned cases=case_budget(limit,1000000);if(!limit&&override&&*override)cases=(unsigned)strtoul(override,NULL,10);
    BYTE saved[GROUND_STUBS][5],initial[2][24],before[2][24],after[2][24];
    uint32_t original_log[128]; unsigned original_logged;
    for(unsigned i=0;i<GROUND_STUBS;++i) if(actor_patch(ground_stubs[i].rva,ground_stubs[i].stub,saved[i],TRUE)) return 1;
    for(unsigned i=0;i<2;++i) memcpy(initial[i],mapped+ground_globals[i][0],ground_globals[i][1]);
    const uint32_t base=(uint32_t)(uintptr_t)mapped;
    const es2_ground_globals globals={base+0xa0be0u,base+0xa9cd0u,base+0xa9ce4u,base+0xa9cd8u,base+0xd2599u};
    const es2_ground_calls calls={math_snapshot_game,math_ground_animate_game,math_height_game,math_collide_game,math_rollback_game,
        math_random_game,math_sound_game,math_clip_game,math_effects_game,mapped};
    uint64_t collisions=0,clips=0;
    if(!limit) coverage_enable(1);
    int status=0;
    for(unsigned n=0;n<cases && !status;++n) {
        if(!limit && n==65536) coverage_enable(0);
        random_bytes(advance_arena,sizeof(advance_arena)); advance_used=0;
        for(unsigned i=0;i<64;++i) ground_script[i]=random32();
        for(unsigned i=0;i<2;++i) {
            random_bytes(mapped+ground_globals[i][0],ground_globals[i][1]);
            if(i==1 && random32()%2) put32(mapped+0xa9ce4,0);
        }
        BYTE *actor=advance_alloc(0x2c0),*config=advance_alloc(0x80),*track=advance_alloc(0x52);
        put32(actor+0x1f2,advance_va(config)); put32(actor+0x22c,advance_va(track));
        actor[0xa3]=(BYTE)(random32()%2);
        if(random32()%4==0) put16(config+6,(uint16_t)(random32()%200)), put16(config+0x6e,(uint16_t)(-(int32_t)(random32()%200)));
        uint32_t a[ES2_HOOK_MAX_ARGS]={0},registers[3]={random32(),random32(),random32()};
        a[0]=advance_va(actor);
        memcpy(advance_before,advance_arena,sizeof(advance_arena));
        for(unsigned i=0;i<2;++i) memcpy(before[i],mapped+ground_globals[i][0],ground_globals[i][1]);
        ground_next=0; ground_logged=0;
        uint64_t got=actor_call(index,a,registers);
        memcpy(original_log,ground_log,sizeof(ground_log)); original_logged=ground_logged;
        memcpy(advance_after,advance_arena,sizeof(advance_arena));
        for(unsigned i=0;i<2;++i) { memcpy(after[i],mapped+ground_globals[i][0],ground_globals[i][1]); memcpy(mapped+ground_globals[i][0],before[i],ground_globals[i][1]); }
        memcpy(advance_arena,advance_before,sizeof(advance_arena));
        ground_next=0; ground_logged=0;
        uint64_t counter_before[GROUND_UPDATE_COUNTERS],observed[GROUND_UPDATE_COUNTERS]={0};
        for(unsigned i=0;i<GROUND_UPDATE_COUNTERS;++i) counter_before[i]=ground_update_counters[i].count;
        uint64_t want=es2_ground_update_registers(&harness_memory,a[0],&globals,&calls);
        BOOL globals_equal=TRUE;
        for(unsigned i=0;i<2;++i) globals_equal&=!memcmp(after[i],mapped+ground_globals[i][0],ground_globals[i][1]);
        if(got!=want || memcmp(advance_arena,advance_after,sizeof(advance_arena)) || !globals_equal ||
           original_logged!=ground_logged || memcmp(original_log,ground_log,sizeof(uint32_t)*ground_logged)) {
            unsigned diff=0; while(diff<sizeof(advance_arena) && advance_arena[diff]==advance_after[diff]) ++diff;
            unsigned log_diff=0; while(log_diff<ground_logged && log_diff<original_logged && original_log[log_diff]==ground_log[log_diff]) ++log_diff;
            fprintf(stderr,"FAIL: %s case %u EAX original=%08x core=%08x EDX original=%08x core=%08x arena_offset=%u globals_equal=%d log %u/%u differs at %u\n",
                d->name,n,(uint32_t)got,(uint32_t)want,(uint32_t)(got>>32),(uint32_t)(want>>32),diff,globals_equal,original_logged,ground_logged,log_diff);
            status=1;
        }
        for(unsigned i=0;i<original_logged;) {
            uint32_t id=original_log[i]; collisions+=id==5; clips+=id==8;
            if(id>=1 && id<=GROUND_UPDATE_COUNTERS) ++observed[id-1];
            i+=id==3?5:id==8?5:2;
        }
        for(unsigned i=0;i<GROUND_UPDATE_COUNTERS;++i) if(ground_update_counters[i].count-counter_before[i]!=observed[i]) {
            fprintf(stderr,"FAIL: %s case %u counter %s differs from original callee log\n",d->name,n,ground_update_counters[i].name);
            status=1;
        }
    }
    if(!limit) coverage_enable(0);
    for(unsigned i=0;i<2;++i) memcpy(mapped+ground_globals[i][0],initial[i],ground_globals[i][1]);
    for(unsigned i=0;i<GROUND_STUBS;++i) if(actor_patch(ground_stubs[i].rva,NULL,saved[i],FALSE)) return 1;
    if(status) return 1;
    printf("PASS: %-20s %u cases; exact EDX:EAX, arena, globals, callee log and counter deltas; rollbacks=%llu clip switches=%llu\n",
        d->name,cases,(unsigned long long)collisions,(unsigned long long)clips);
    fflush(stdout); return 0;
}
