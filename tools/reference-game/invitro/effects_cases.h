/* Effects contract isolation: every callee logs its full argument slots and
 * scripted EDX:EAX, plus pointed-to values. Only the local transform output
 * address is normalized. All stubs are own code; original bytes remain only
 * in the private, hash-checked image. */
typedef struct effects_record { uint32_t id,args[8]; uint64_t result; } effects_record;
static effects_record effects_log[96];
static unsigned effects_logged,effects_next;
static uint32_t effects_script[128],effects_actor,effects_entries[3],effects_active_record;
static uint32_t effects_vtable[9];
static uint32_t effects_take(void) { return effects_script[effects_next++%128]; }
static uint64_t effects_result(effects_record *r) {
    uint32_t low=effects_take();r->result=((uint64_t)effects_take()<<32)|low;return r->result;
}
static effects_record *effects_note(unsigned id,const uint32_t *args,unsigned count) {
    if(effects_logged>=96) { fail("effects log overflow");ExitProcess(1); }
    effects_record *r=&effects_log[effects_logged++];memset(r,0,sizeof(*r));r->id=id;
    for(unsigned i=0;i<count;++i)r->args[i]=args[i];return r;
}
static uint64_t __cdecl effects_lookup(uint32_t list,uint32_t selector) {
    uint32_t args[2]={list,selector};effects_record *r=effects_note(5,args,2);
    uint64_t result=effects_result(r);unsigned pick=effects_take()%4;
    uint32_t entry=pick==3?0:effects_entries[pick];
    BYTE *actor=(BYTE *)(uintptr_t)effects_actor,*manager=(BYTE *)(uintptr_t)get32(actor+0x34);
    int32_t part=entry?(int16_t)get16((BYTE *)(uintptr_t)entry+4):-1;
    effects_active_record=part<0?(uint32_t)(uintptr_t)(mapped+0x2c56a8):get32(manager+0x16)+32u*(uint32_t)part;
    r->result=(result&UINT64_C(0xffffffff00000000))|entry;return r->result;
}
static uint64_t __cdecl effects_euler(uint32_t angles,uint32_t matrix) {
    uint32_t args[2]={angles,matrix};effects_record *r=effects_note(0,args,2);
    for(unsigned i=0;i<3;++i)r->args[2+i]=get16((BYTE *)(uintptr_t)angles+2*i);
    for(unsigned i=0;i<20;i+=4)put32((BYTE *)(uintptr_t)matrix+i,effects_take());
    return effects_result(r);
}
static uint64_t __cdecl effects_transform(uint32_t point,uint32_t matrix,uint32_t out) {
    uint32_t args[3]={point,matrix,0};effects_record *r=effects_note(1,args,3);
    for(unsigned i=0;i<3;++i) {
        r->args[3+i]=get32((BYTE *)(uintptr_t)point+4*i);
        put32((BYTE *)(uintptr_t)out+4*i,r->args[3+i]+get32((BYTE *)(uintptr_t)matrix+20+4*i));
    }
    r->args[6]=get16((BYTE *)(uintptr_t)matrix+32); /* cache valid */
    return effects_result(r);
}
static uint64_t __cdecl effects_distance(uint32_t x,uint32_t y,uint32_t z) {
    uint32_t args[3]={x,y,z};effects_record *r=effects_note(2,args,3);
    /* Exercise the real metric's wrapped domain, but arbitrary scratch EDX. */
    r->result=((uint64_t)effects_take()<<32)|es2_distance3(x,y,z);return r->result;
}
static uint64_t __cdecl effects_sound(uint32_t id,uint32_t point) {
    uint32_t args[2]={id,point};effects_record *r=effects_note(3,args,2);
    for(unsigned i=0;i<3;++i)r->args[2+i]=get32((BYTE *)(uintptr_t)point+4*i);
    BYTE *actor=(BYTE *)(uintptr_t)effects_actor;
    r->args[5]=actor[0x2b4];r->args[6]=actor[0xa3];
    if(effects_take()%2)actor[0xa3]^=1; /* proves the post-sound reread */
    return effects_result(r);
}
static uint64_t __cdecl effects_feedback(void) {
    effects_record *r=effects_note(4,NULL,0);
    r->args[0]=((BYTE *)(uintptr_t)effects_actor)[0x2b4];return effects_result(r);
}
static const struct {uint32_t rva;void *stub;} effects_stubs[]={
    {0x7ebb8,(void *)effects_euler},{0x8043c,(void *)effects_transform},{0x7de72,(void *)effects_distance},
    {0x628e0,(void *)effects_sound},{0x34144,(void *)effects_feedback}};
#define EFFECTS_STUBS (sizeof(effects_stubs)/sizeof(effects_stubs[0]))
static void effects_reset_log(void) { effects_next=effects_logged=0;memset(effects_log,0,sizeof(effects_log)); }
static BYTE *effects_build(unsigned n) {
    random_bytes(advance_arena,sizeof(advance_arena));advance_used=0;
    for(unsigned i=0;i<128;++i)effects_script[i]=random32();
    BYTE *a=advance_alloc(0x300),*c=advance_alloc(0x220),*t=advance_alloc(0x52),*m=advance_alloc(0x1a);
    BYTE *list=advance_alloc(4),*slots=advance_alloc(24),*records=advance_alloc(3*32),*reference=advance_alloc(16);
    effects_actor=advance_va(a);effects_vtable[8]=(uint32_t)(uintptr_t)effects_lookup;
    put32(a+0x1f2,advance_va(c));put32(a+0x22c,advance_va(t));put32(a+0x34,advance_va(m));put32(a+0x238,advance_va(slots));
    put32(m+4,advance_va(list));put32(list,(uint32_t)(uintptr_t)effects_vtable);put32(m+0x16,advance_va(records));
    for(unsigned i=0;i<3;++i) { BYTE *e=advance_alloc(6);effects_entries[i]=advance_va(e);put16(e+4,(uint16_t)(i==2?0xffff:i)); }
    unsigned count=n<1024?1u:random32()%7;
    if(n<1024 && n%64==0)put32(a+0x22c,0);
    if(n%71==0)count=0;
    put16(a+0x23c,(uint16_t)(n%73==0?0xffff:count));put16(a+0x32,(uint16_t)(n%2));
    a[0xa3]=(BYTE)((n/3)%2);put16(a+0x2a0,(uint16_t)(n%3==0?0:n%3==1?1:0xffff));
    static const unsigned config_ids[]={0x46,0xe,0x7c,0x12,0x14,0x10};
    for(unsigned i=0;i<6;++i)put16(c+config_ids[i],(uint16_t)(10+i));
    uint16_t id=(uint16_t)(10+(n/2)%7);put16(t+0x16,n%5==0?0xffff:id);put16(t+4,id);
    for(unsigned i=0;i<6;++i) {
        BYTE *attached=advance_alloc(0x38);put32(slots+4*i,n>=1024 && random32()%5==0?0:advance_va(attached));
        c[0x72+i]=(BYTE)(n<1024?(n/14)%4:random32()%4);
        c[0x77+i]=(BYTE)random32();a[0x2b4+i]=(BYTE)(random32()%2);
    }
    /* Config arrays overlap at C+77; preserve the actual shared byte. */
    static const uint32_t coordinates[]={0,1,0xffffffffu,0x80000000u,0x7fffffffu,32767,0xffff8000u};
    uint32_t coordinate=coordinates[(n/7)%7];
    for(unsigned i=0;i<3;++i)put32(records+32*i+0x18,coordinate);
    random_bytes(mapped+0x2c56a8,32);put32(mapped+0x2c56c0,coordinate);
    for(unsigned i=0;i<3;++i) {
        int32_t center=(int16_t)(uint16_t)coordinate;
        put16(c+0xac+2*i,(uint16_t)(center+(int32_t)(random32()%3)-1));
        put16(c+0x98+2*i,(uint16_t)(center+(int32_t)(random32()%3)-1));
    }
    put32(mapped+0xd24ea,advance_va(reference));
    for(unsigned i=0;i<3;++i)put32(reference+4+4*i,get32(a+0x26+4*i));
    static const uint32_t distances[]={14999,15000,15001,0x80000000u,0xffffffffu};
    put32(reference+4,get32(a+0x26)-distances[n%5]);
    return a;
}

static uint64_t __cdecl effects_update_collision(uint32_t a) {
    ground_note(4);ground_note(a);return ground_pair()&UINT64_C(0xffffffffffff0000);
}
/* Exercise actual entry detours in every original/replaced combination.
 * The update's other callees are isolated exactly as in ground_cases. */
static int effects_mode_cases(void) {
    BYTE saved[GROUND_STUBS-1][5],initial[2][24],before[2][24],after[2][24];
    for(unsigned i=0;i<GROUND_STUBS-1;++i) {
        void *stub=i==3?(void *)effects_update_collision:ground_stubs[i].stub;
        if(actor_patch(ground_stubs[i].rva,stub,saved[i],TRUE))return 1;
    }
    for(unsigned i=0;i<2;++i)memcpy(initial[i],mapped+ground_globals[i][0],ground_globals[i][1]);
    const unsigned update=ES2_HOOK_INDEX_ground_update,effects=ES2_HOOK_INDEX_ground_effects;
    int status=0;effects_record expected[96];uint32_t expected_ground[128];
    for(unsigned n=0;n<256 && !status;++n) {
        current_case=n;current_case_valid=1;
        BYTE *actor=effects_build(n+1024);
        if(n%4==0)put32(actor+0x22c,0); /* no-track return propagates entry registers */
        for(unsigned i=0;i<64;++i)ground_script[i]=random32();
        for(unsigned i=0;i<2;++i)memcpy(before[i],mapped+ground_globals[i][0],ground_globals[i][1]);
        uint32_t args[6]={advance_va(actor)},regs[3]={random32(),random32(),random32()};
        memcpy(advance_before,advance_arena,sizeof(advance_arena));effects_reset_log();ground_next=ground_logged=0;
        uint64_t expected_result=actor_call(update,args,regs);
        memcpy(expected,effects_log,sizeof(expected));unsigned expected_count=effects_logged;
        memcpy(expected_ground,ground_log,sizeof(expected_ground));unsigned expected_ground_count=ground_logged;
        memcpy(advance_after,advance_arena,sizeof(advance_arena));
        for(unsigned i=0;i<2;++i)memcpy(after[i],mapped+ground_globals[i][0],ground_globals[i][1]);
        for(unsigned mode=0;mode<4 && !status;++mode) {
            es2_hook hooks[2]={0};unsigned indices[2]={update,effects};
            es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
            for(unsigned i=0;i<2;++i) {
                o.mode=(mode&(1u<<i))?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
                if(!es2_hook_install_ex(&hooks[i],function_hook_descriptor(indices[i]),&o,function_hook_extension(indices[i])))return fail("effects mode install");
            }
            memcpy(advance_arena,advance_before,sizeof(advance_arena));
            for(unsigned i=0;i<2;++i)memcpy(mapped+ground_globals[i][0],before[i],ground_globals[i][1]);
            effects_reset_log();ground_next=ground_logged=0;
            uint64_t got=actor_call(update,args,regs);
            if(got!=expected_result || memcmp(advance_arena,advance_after,sizeof(advance_arena)) ||
               effects_logged!=expected_count || memcmp(expected,effects_log,effects_logged*sizeof(*expected)) ||
               ground_logged!=expected_ground_count || memcmp(expected_ground,ground_log,ground_logged*sizeof(uint32_t))) {
                fprintf(stderr,"FAIL: ground_effects mode case %u combination=%u result=%016llx expected=%016llx\n",n,mode,(unsigned long long)got,(unsigned long long)expected_result);status=1;
            }
            for(unsigned i=0;i<2;++i) {
                if(memcmp(mapped+ground_globals[i][0],after[i],ground_globals[i][1]))status=fail("effects mode global state");
                if((mode&(1u<<i)) && hooks[i].calls!=1)status=fail("effects mode entry count");
                if(!es2_hook_uninstall(&hooks[i]))return fail("effects mode uninstall");
            }
        }
    }
    current_case_valid=0;
    for(unsigned i=0;i<2;++i)memcpy(mapped+ground_globals[i][0],initial[i],ground_globals[i][1]);
    for(unsigned i=0;i<GROUND_STUBS-1;++i)if(actor_patch(ground_stubs[i].rva,NULL,saved[i],FALSE))return 1;
    if(!status)puts("PASS: ground_update/ground_effects 256 cases x 4 mode combinations; exact registers, graph, globals and both callee logs");
    return status;
}

/* Real feedback early return: 429820 loads [4cf99c] without changing EDX;
 * 434144 returns immediately for object+14 == 4. This cannot be established
 * with an ordinary C stub that always fabricates both result registers. */
static int effects_feedback_probe(void) {
    BYTE *a=effects_build(1025),*c=(BYTE *)(uintptr_t)get32(a+0x1f2),*t=(BYTE *)(uintptr_t)get32(a+0x22c);
    BYTE *m=(BYTE *)(uintptr_t)get32(a+0x34),*records=(BYTE *)(uintptr_t)get32(m+0x16);
    BYTE *reference=(BYTE *)(uintptr_t)get32(mapped+0xd24ea),*feedback=advance_alloc(0x18),*attached=advance_alloc(0x38);
    uint32_t old=get32(mapped+0xcf99c);put32(mapped+0xcf99c,advance_va(feedback));put32(feedback+0x14,4);
    put16(a+0x23c,1);put32((BYTE *)(uintptr_t)get32(a+0x238),advance_va(attached));
    put16(t+0x16,11);c[0x72]=0;a[0xa3]=1;put16(a+0x2a0,1);put16(c+0xac,0xffff);put16(c+0x98,1);
    for(unsigned i=0;i<3;++i)put32(records+32*i+0x18,0);put32(mapped+0x2c56c0,0);
    put32(reference+4,get32(a+0x26)-15000);put32(reference+8,get32(a+0x2a));put32(reference+12,get32(a+0x2e));
    uint32_t args[6]={advance_va(a)},regs[3]={random32(),random32(),random32()};
    memcpy(advance_before,advance_arena,sizeof(advance_arena));effects_reset_log();
    uint64_t got=actor_call(ES2_HOOK_INDEX_ground_effects,args,regs);
    memcpy(advance_after,advance_arena,sizeof(advance_arena));memcpy(advance_arena,advance_before,sizeof(advance_arena));
    effects_reset_log();BYTE point[12];es2_effects_context g=math_effects_context(mapped,(uint32_t)(uintptr_t)point);
    uint64_t want=es2_ground_effects_registers(&harness_memory,args[0],&g,regs[0],regs[1]);
    uint64_t expected=((uint64_t)(advance_va(a)+0x92u)<<32)|1u;
    put32(mapped+0xcf99c,old);
    if(got!=expected || want!=expected || memcmp(advance_arena,advance_after,sizeof(advance_arena))) {
        fprintf(stderr,"FAIL: ground_effects case 1001024 real feedback EDX: original=%016llx core=%016llx expected=%016llx\n",(unsigned long long)got,(unsigned long long)want,(unsigned long long)expected);return 1;
    }
    puts("PASS: ground_effects real Euler/transform/distance and 434144 early return preserve incoming EDX");return 0;
}

static int effects_cases(unsigned index,unsigned limit) {
    const es2_hook_descriptor *d=function_hook_descriptor(index);current_function=d->name;
    const char *override=getenv("ES2_INVITRO_EFFECTS_CASES");
    unsigned cases=case_budget(limit,1001024);if(!limit&&override&&*override)cases=(unsigned)strtoul(override,NULL,10);
    BYTE saved[EFFECTS_STUBS][5],initial_default[32];uint32_t initial_reference=get32(mapped+0xd24ea);
    memcpy(initial_default,mapped+0x2c56a8,32);
    for(unsigned i=0;i<EFFECTS_STUBS;++i)if(actor_patch(effects_stubs[i].rva,effects_stubs[i].stub,saved[i],TRUE))return 1;
    effects_record expected[96];uint64_t totals[GROUND_EFFECTS_COUNTERS]={0};int status=0;
    if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases && !status;++n) {
        current_case=n;current_case_valid=1;
        if(!limit && n==65536)coverage_enable(0);
        BYTE *actor=effects_build(n),default_before[32];memcpy(default_before,mapped+0x2c56a8,32);
        uint32_t reference_before=get32(mapped+0xd24ea);
        uint32_t args[6]={advance_va(actor)},regs[3]={random32(),random32(),random32()};
        memcpy(advance_before,advance_arena,sizeof(advance_arena));effects_reset_log();
        uint64_t got=actor_call(index,args,regs);
        if(memcmp(default_before,mapped+0x2c56a8,32) || get32(mapped+0xd24ea)!=reference_before)status=fail("effects original wrote read-only globals");
        unsigned expected_count=effects_logged;memcpy(expected,effects_log,sizeof(expected));
        memcpy(advance_after,advance_arena,sizeof(advance_arena));memcpy(advance_arena,advance_before,sizeof(advance_arena));
        effects_reset_log();BYTE point[12];es2_effects_context g=math_effects_context(mapped,(uint32_t)(uintptr_t)point);
        uint64_t previous[GROUND_EFFECTS_COUNTERS],observed[GROUND_EFFECTS_COUNTERS]={0};
        for(unsigned i=0;i<GROUND_EFFECTS_COUNTERS;++i)previous[i]=ground_effects_counters[i].count;
        uint64_t want=es2_ground_effects_registers(&harness_memory,args[0],&g,regs[0],regs[1]);
        if(memcmp(default_before,mapped+0x2c56a8,32) || get32(mapped+0xd24ea)!=reference_before)status=fail("effects core wrote read-only globals");
        if(got!=want || memcmp(advance_arena,advance_after,sizeof(advance_arena)) || expected_count!=effects_logged || memcmp(expected,effects_log,effects_logged*sizeof(*expected))) {
            unsigned diff=0;while(diff<sizeof(advance_arena) && advance_arena[diff]==advance_after[diff])++diff;
            unsigned ld=0;while(ld<expected_count && ld<effects_logged && !memcmp(expected+ld,effects_log+ld,sizeof(*expected)))++ld;
            fprintf(stderr,"FAIL: %s case %u EAX original=%08x core=%08x EDX original=%08x core=%08x arena_offset=%u calls=%u/%u first_call_diff=%u\n",d->name,n,(uint32_t)got,(uint32_t)want,(uint32_t)(got>>32),(uint32_t)(want>>32),diff,expected_count,effects_logged,ld);status=1;
        }
        for(unsigned i=0;i<expected_count;++i)++observed[expected[i].id];
        for(unsigned i=0;i<GROUND_EFFECTS_COUNTERS;++i) {
            if(ground_effects_counters[i].count-previous[i]!=observed[i])status=fail("effects callee counter delta");
            totals[i]+=observed[i];
        }
    }
    current_case_valid=0;if(!limit)coverage_enable(0);
    if(!limit && !status)status=effects_mode_cases();
    for(unsigned i=0;i<EFFECTS_STUBS;++i)if(actor_patch(effects_stubs[i].rva,NULL,saved[i],FALSE))return 1;
    if(!limit && !status)status=effects_feedback_probe();
    memcpy(mapped+0x2c56a8,initial_default,32);put32(mapped+0xd24ea,initial_reference);
    if(status)return 1;
    printf("PASS: %-20s %u cases; exact EDX:EAX, arena, ordered callee arguments/results and counter deltas; lookup=%llu transform=%llu sound=%llu feedback=%llu\n",d->name,cases,(unsigned long long)totals[5],(unsigned long long)totals[1],(unsigned long long)totals[3],(unsigned long long)totals[4]);
    fflush(stdout);return 0;
}
