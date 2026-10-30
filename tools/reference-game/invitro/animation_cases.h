/* 0x478b7c manager_reset and 0x40250c animation_step. The track advance
 * 0x478d38 is not replaced: for these cases the private mapping jumps from
 * its entry to advance_stub, which stores a prepared animation matrix in
 * [M+0x16] and records its arguments; core gets the same stub as callback.
 * Direct cases poison the stack (es2_probe_call) so that the padding byte
 * T+0x41 written by the nested track_scaled_inverse is checked exactly. */
static BYTE advance_matrix[32];
static uint32_t advance_calls,advance_manager,advance_step;
static void __cdecl advance_stub(uint32_t manager,uint32_t step) {
    ++advance_calls;advance_manager=manager;advance_step=step;
    memcpy((void *)(uintptr_t)get32((const BYTE *)(uintptr_t)manager+0x16),advance_matrix,32);
}
static void advance_callback(void *context,uint32_t manager,uint32_t step) { (void)context;advance_stub(manager,step); }
static uint32_t harness_read32(void *c,uint32_t x) { (void)c; return get32((const BYTE *)(uintptr_t)x); }
static uint16_t harness_read16(void *c,uint32_t x) { (void)c; return get16((const BYTE *)(uintptr_t)x); }
static void harness_write32(void *c,uint32_t x,uint32_t v) { (void)c; put32((BYTE *)(uintptr_t)x,v); }
static void harness_write16(void *c,uint32_t x,uint16_t v) { (void)c; put16((BYTE *)(uintptr_t)x,v); }
static void *harness_host(void *c,uint32_t x) { (void)c; return (void *)(uintptr_t)x; }
static const es2_guest_memory harness_memory={NULL,harness_read32,harness_read16,harness_write32,harness_write16,harness_host};

static int advance_patch(BOOL install) {
    static BYTE saved[5];
    BYTE *site=mapped+0x78d38; DWORD previous;
    if(!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&previous)) return fail("advance stub protection");
    if(install) {
        memcpy(saved,site,5);
        site[0]=0xe9;put32(site+1,(uint32_t)(uintptr_t)advance_stub-(uint32_t)(uintptr_t)(site+5));
    } else memcpy(site,saved,5);
    if(!VirtualProtect(site,5,previous,&previous)) return fail("advance stub restore protection");
    FlushInstructionCache(GetCurrentProcess(),site,5);
    return 0;
}

static int animation_cases(unsigned index,unsigned limit) {
    const es2_hook_descriptor *d=function_hook_descriptor(index); current_function=d->name;
    BOOL step_hook=index==ES2_HOOK_INDEX_animation_step;
    unsigned cases=case_budget(limit,1000000);
    uint64_t modes[4]={0},aliased=0,residue=0;
    BYTE arena[512],before[512],after[512];
    const es2_math_tables tables={MATH_QUARTER(d->user),(const BYTE *)d->user+0xa05bc,(const BYTE *)d->user+0xa1e3c,(const BYTE *)d->user+0xa1c34};
    if(step_hook && advance_patch(TRUE)) return 1;
    if(!limit) coverage_enable(1);
    int status=0;
    for(unsigned n=0;n<cases && !status;++n) {
        if(!limit && n==65536) coverage_enable(0);
        random_bytes(arena,sizeof(arena));
        BYTE *actor=arena+16,*manager=arena+96,*matrix=arena+128,*track=arena+176,*input=arena+272;
        unsigned has_track=n%4!=0,enabled=(n/4)%3!=0,cached=(n/2)%2,alias=!limit && !step_hook && n%7==6;
        put32(manager+0x16,(uint32_t)(uintptr_t)matrix);
        put32(manager+0x1a,has_track?(uint32_t)(uintptr_t)track:0);
        put16(track+0x20,enabled?(uint16_t)(random32()|1):0);
        if(!get16(track+0x1e)) put16(track+0x1e,1);
        track[0x2e + 18]=(BYTE)padding_class((n/12)%4);
        input[18]=(BYTE)padding_class((n/48)%4);
        if(alias) input=matrix+4*(int)((n/7)%5)-8;
        put16(actor+0x32,cached?(uint16_t)(random32()|1):0);
        random_bytes(advance_matrix,32);advance_matrix[18]=(BYTE)padding_class((n/24)%4);
        uint32_t a[ES2_HOOK_MAX_ARGS]={0};
        if(step_hook) { a[0]=(uint32_t)(uintptr_t)actor;a[1]=(uint32_t)(uintptr_t)manager;a[2]=random32(); }
        else { a[0]=(uint32_t)(uintptr_t)manager;a[1]=(uint32_t)(uintptr_t)input; }
        modes[has_track?(enabled?2:1):0]++;if(step_hook && cached)modes[3]++;
        aliased+=alias;
        memcpy(before,arena,sizeof(arena));
        uint32_t got[2],edx; uint8_t padding;
        advance_calls=0;
        if(!limit) {
            padding=(n&1)?0x31:0xa7; edx=padding*0x01010101u;
            es2_probe_call(mapped+d->rva,a[0],a[1],a[2],edx,got);
        } else {
            got[0]=original(index,a);got[1]=original_edx;padding=0;edx=(uint32_t)(uintptr_t)a;
        }
        uint32_t original_calls=advance_calls,original_manager=advance_manager,original_step=advance_step;
        memcpy(after,arena,sizeof(arena));memcpy(arena,before,sizeof(arena));
        advance_calls=0;
        uint64_t want=step_hook ?
            es2_animation_step_registers(&harness_memory,a[0],a[1],a[2],&tables,advance_callback,NULL,padding) :
            es2_manager_reset_registers(&harness_memory,a[0],(void *)(uintptr_t)a[1],tables.quarter,padding,edx);
        size_t masked=(size_t)(track-arena)+0x41;
        /* Branch coverage (first 65536 direct cases) stops at INT3 in
         * 0x478b7c; the exception dispatch runs on this stack below ESP and
         * overwrites the poisoned slot that 0x479194's local later occupies. */
        BOOL clobbered=!limit && n<65536;
        if(!limit && !clobbered && has_track && after[masked]==padding && before[masked]!=padding) ++residue;
        if(limit || clobbered) arena[masked]=after[masked];
        BOOL calls_equal=!step_hook || (original_calls==1 && advance_calls==1 && original_manager==advance_manager &&
            (uint16_t)original_step==(uint16_t)advance_step && (uint16_t)advance_step==(uint16_t)a[2]);
        if(got[0]!=(uint32_t)want || got[1]!=(uint32_t)(want>>32) || memcmp(arena,after,sizeof(arena)) || !calls_equal) {
            unsigned diff=0;while(diff<sizeof(arena)&&arena[diff]==after[diff])++diff;
            fprintf(stderr,"FAIL: %s case %u EAX original=%08x core=%08x EDX original=%08x core=%08x state_offset=%u original_byte=%u core_byte=%u calls=%u/%u padding=%02x\n",
                d->name,n,got[0],(uint32_t)want,got[1],(uint32_t)(want>>32),diff,diff<512?after[diff]:0,diff<512?arena[diff]:0,original_calls,advance_calls,padding);
            status=1;
        }
    }
    if(!limit) coverage_enable(0);
    if(step_hook && advance_patch(FALSE)) return 1;
    if(status) return 1;
    printf("PASS: %-20s %u cases; exact EDX:EAX/state/512 guard bytes%s; aliases=%llu; residue T+0x41 from padding=%llu; no track=%llu disabled=%llu enabled=%llu",
        d->name,cases,limit?" except T+0x41":" with stack padding 31/a7 (T+0x41 masked in the 65536 coverage cases)",(unsigned long long)aliased,(unsigned long long)residue,
        (unsigned long long)modes[0],(unsigned long long)modes[1],(unsigned long long)modes[2]);
    if(step_hook) printf(" cached rotation=%llu; track advance stubbed",(unsigned long long)modes[3]);
    puts("");fflush(stdout);return 0;
}
