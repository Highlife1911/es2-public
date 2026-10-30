/* 0x47967c track_target, 0x4796d8 track_rate (both with the route search
 * 0x4793d4), 0x402604 actor_animate and 0x418f40 ground_animate.
 * Tracks live in synthetic resources: clips of both classes (image vtables,
 * so the original reached/next/previous methods run in the mapping) and one
 * own vtable for core's fallback; frame records with transition counts and
 * indices into a transition table [R+0x1e]. actor_animate runs with 0x40250c
 * replaced by step_stub, ground_animate with 0x402604 replaced by
 * animate_stub; core gets the same stubs. Entry EAX/EDX/ECX are random. */
static uint32_t actor_stub_calls,actor_stub_actor,actor_stub_manager,actor_stub_step;
static uint64_t __cdecl actor_step_stub(uint32_t actor,uint32_t manager,uint32_t step) {
    ++actor_stub_calls; actor_stub_actor=actor; actor_stub_manager=manager; actor_stub_step=(uint16_t)step;
    put16((BYTE *)(uintptr_t)actor+0x40,(uint16_t)step);
    return ((uint64_t)(actor^0x5a5a0000u)<<32)|(manager+(uint16_t)step);
}
static uint64_t __cdecl actor_animate_stub(uint32_t actor,uint32_t step) {
    ++actor_stub_calls; actor_stub_actor=actor; actor_stub_step=step;
    put32((BYTE *)(uintptr_t)actor+0x40,step);
    return ((uint64_t)(step*3u)<<32)|(actor+1u);
}
static uint64_t actor_step_callback(void *c,uint32_t actor,uint32_t manager,uint32_t step) { (void)c; return actor_step_stub(actor,manager,step); }
static uint64_t actor_animate_callback(void *c,uint32_t actor,uint32_t step) { (void)c; return actor_animate_stub(actor,step); }
static uint64_t actor_rate_callback(void *c,uint32_t track,uint32_t rate) {
    (void)c; return ((uint64_t (__cdecl *)(uint32_t,uint32_t))(void *)(mapped+0x796d8))(track,rate);
}

static int actor_patch(uint32_t rva,void *target,BYTE saved[5],BOOL install) {
    BYTE *site=mapped+rva; DWORD previous;
    if(!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&previous)) return fail("actor stub protection");
    if(install) { memcpy(saved,site,5); site[0]=0xe9; put32(site+1,(uint32_t)(uintptr_t)target-(uint32_t)(uintptr_t)(site+5)); }
    else memcpy(site,saved,5);
    if(!VirtualProtect(site,5,previous,&previous)) return fail("actor stub restore protection");
    FlushInstructionCache(GetCurrentProcess(),site,5);
    return 0;
}

static uint64_t actor_call(unsigned index,const uint32_t *a,const uint32_t registers[3]) {
    es2_hook_result r; es2_hook_call call={mapped+function_hook_descriptor(index)->rva,ES2_ABI_CDECL,
        function_hook_descriptor(index)->arg_count,a,initial_fpu,&r,registers};
    es2_hook_machine_call(&call);
    return ((uint64_t)r.edx<<32)|r.eax;
}

/* A track over a resource with clips, frame records and transitions. */
static BYTE *actor_build_track(unsigned *clip_count) {
    unsigned clips=1+random32()%4,transitions=1+random32()%12; uint16_t frames[4];
    BYTE *resource=advance_alloc(0x22),*list=advance_alloc(4*clips),*table=advance_alloc(8*transitions);
    put32(resource+6,advance_va(list)); put32(resource+0x1e,advance_va(table));
    for(unsigned i=0;i<transitions;++i) {
        put16(table+8*i+2,(uint16_t)(random32()%(clips+1))); put16(table+8*i+4,(uint16_t)(random32()%6));
        if(random32()%8==0) put16(table+8*i+2,0xffffu);
    }
    for(unsigned i=0;i<clips;++i) {
        BYTE *clip=advance_alloc(0x1a); unsigned kind=random32()%20,count=1+random32()%5;
        put32(clip,kind<9?ADVANCE_SEQUENCE:kind<18?ADVANCE_CYCLIC:advance_va((const BYTE *)advance_other_vtable));
        put16(clip+0x0a,(uint16_t)count); frames[i]=(uint16_t)count;
        BYTE *records=advance_alloc(6*count);
        for(unsigned f=0;f<count;++f) {
            unsigned first=random32()%transitions,n=random32()%(transitions-first+1);
            if(random32()%3==0) n=0;
            put16(records+6*f+2,(uint16_t)n); put16(records+6*f+4,(uint16_t)first);
        }
        put32(clip+0x0c,advance_va(records)); put32(list+4*i,advance_va(clip));
    }
    BYTE *t=advance_alloc(0x52);
    *clip_count=clips;
    put32(t,advance_va(resource));
    uint16_t clip=advance_pick(clips);
    put16(t+4,clip); put16(t+6,advance_pick(frames[clip])); put16(t+0x0a,advance_pick(frames[clip]));
    uint16_t target=random32()%10==0?0xffffu:advance_pick(clips);
    put16(t+0x16,target);
    put16(t+0x18,random32()%4==0||target==0xffffu?0xffffu:advance_pick(frames[target]));
    if(random32()%4==0) { put16(t+0x16,clip); put16(t+0x18,get16(t+6)); }
    if(random32()%3==0) put16(t+0x1c,0);
    return t;
}

static int actor_cases(unsigned index,unsigned limit) {
    const es2_hook_descriptor *d=function_hook_descriptor(index); current_function=d->name;
    const char *override=getenv("ES2_INVITRO_ACTOR_CASES");
    unsigned cases=case_budget(limit,1000000);if(!limit&&override&&*override)cases=(unsigned)strtoul(override,NULL,10);
    const es2_clip_classes classes={ADVANCE_SEQUENCE,ADVANCE_CYCLIC,advance_other,NULL};
    advance_other_vtable[8]=(uint32_t)(uintptr_t)advance_other_next;
    advance_other_vtable[9]=(uint32_t)(uintptr_t)advance_other_previous;
    advance_other_vtable[10]=advance_other_vtable[11]=(uint32_t)(uintptr_t)advance_other_reached;
    BOOL step_stubbed=index==ES2_HOOK_INDEX_actor_animate,animate_stubbed=index==ES2_HOOK_INDEX_ground_animate;
    BYTE saved[5];
    if(step_stubbed && actor_patch(0x0250c,(void *)actor_step_stub,saved,TRUE)) return 1;
    if(animate_stubbed && actor_patch(0x02604,(void *)actor_animate_stub,saved,TRUE)) return 1;
    uint64_t stopped=0,routes=0,calls=0;
    if(!limit) coverage_enable(1);
    int status=0;
    for(unsigned n=0;n<cases && !status;++n) {
        if(!limit && n==65536) coverage_enable(0);
        random_bytes(advance_arena,sizeof(advance_arena)); advance_used=0;
        unsigned clip_count;
        BYTE *t=actor_build_track(&clip_count),*actor=advance_alloc(0x2c0);
        uint32_t a[ES2_HOOK_MAX_ARGS]={0},registers[3]={random32(),random32(),random32()};
        uint16_t scale=(uint16_t)random32();
        if(index==ES2_HOOK_INDEX_track_target) {
            a[0]=advance_va(t); a[1]=random32(); a[2]=random32(); a[3]=random32();
            /* Low words: the generated target, or a fresh clip (or 0xffff) and frame. */
            if(n%2) { a[1]=(a[1]&0xffff0000u)|get16(t+0x16); a[2]=(a[2]&0xffff0000u)|get16(t+0x18); }
            else {
                a[1]=(a[1]&0xffff0000u)|(random32()%6==0?0xffffu:advance_pick(clip_count));
                a[2]=(a[2]&0xffff0000u)|(random32()%4==0?0xffffu:advance_pick(6));
            }
        } else if(index==ES2_HOOK_INDEX_track_rate) {
            a[0]=advance_va(t); a[1]=random32();
            if(random32()%4==0) a[1]=(a[1]&0xffff0000u)|(uint16_t)((int32_t)get16(t+0x4e)^0x8000);
        } else {
            put32(actor+0x34,random32()); put32(actor+0x22c,advance_va(t)); put16(actor+0x2a0,(uint16_t)random32());
            if(random32()%3==0) actor[0x39]=0; else if(!actor[0x39]) actor[0x39]=1;
            a[0]=advance_va(actor); a[1]=random32();
        }
        if(advance_used>sizeof(advance_arena)) { status=fail("actor arena too small"); break; }
        put16(mapped+0xd3b64,scale);
        memcpy(advance_before,advance_arena,sizeof(advance_arena));
        actor_stub_calls=0;
        uint64_t got=actor_call(index,a,registers);
        uint32_t original_calls=actor_stub_calls,original_actor=actor_stub_actor,original_step=actor_stub_step;
        memcpy(advance_after,advance_arena,sizeof(advance_arena));
        memcpy(advance_arena,advance_before,sizeof(advance_arena));
        actor_stub_calls=0;
        uint64_t want;
        if(index==ES2_HOOK_INDEX_track_target) want=es2_track_target_registers(&harness_memory,&classes,a[0],a[1],a[2],a[3],registers[1]);
        else if(index==ES2_HOOK_INDEX_track_rate) want=es2_track_rate_registers(&harness_memory,&classes,a[0],a[1]);
        else if(step_stubbed) want=es2_actor_animate_registers(&harness_memory,a[0],a[1],actor_step_callback,NULL);
        else want=es2_ground_animate_registers(&harness_memory,a[0],scale,registers[0],actor_rate_callback,actor_animate_callback,NULL);
        BOOL calls_equal=original_calls==actor_stub_calls && original_actor==actor_stub_actor &&
            (uint16_t)original_step==(uint16_t)actor_stub_step;
        if(got!=want || memcmp(advance_arena,advance_after,sizeof(advance_arena)) || !calls_equal) {
            unsigned diff=0; while(diff<sizeof(advance_arena) && advance_arena[diff]==advance_after[diff]) ++diff;
            fprintf(stderr,"FAIL: %s case %u EAX original=%08x core=%08x EDX original=%08x core=%08x arena_offset=%u track at %u original_byte=%u core_byte=%u calls=%u/%u args=%08x,%08x,%08x,%08x regs=%08x,%08x\n",
                d->name,n,(uint32_t)got,(uint32_t)want,(uint32_t)(got>>32),(uint32_t)(want>>32),diff,(unsigned)(t-advance_arena),
                diff<sizeof(advance_arena)?advance_after[diff]:0,diff<sizeof(advance_arena)?advance_arena[diff]:0,
                original_calls,actor_stub_calls,a[0],a[1],a[2],a[3],registers[0],registers[1]);
            status=1;
        }
        stopped+=(get16(t+0x50)&1)!=0; routes+=get16(t+0x0c)!=0xffffu; calls+=original_calls;
    }
    if(!limit) coverage_enable(0);
    if(step_stubbed && actor_patch(0x0250c,NULL,saved,FALSE)) return 1;
    if(animate_stubbed && actor_patch(0x02604,NULL,saved,FALSE)) return 1;
    if(status) return 1;
    printf("PASS: %-20s %u cases; exact EDX:EAX, %u arena bytes and stub calls; stopped=%llu transitions found=%llu stub calls=%llu\n",
        d->name,cases,(unsigned)sizeof(advance_arena),(unsigned long long)stopped,(unsigned long long)routes,(unsigned long long)calls);
    fflush(stdout); return 0;
}
