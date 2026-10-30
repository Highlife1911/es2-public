/* 0x46e180 terrain height on random grids: up to 16 x 16 cells of 16 bytes
 * (random height and flag bytes), log2 cell sizes 0..31 (mostly 0..12),
 * shift counts sometimes with bits above the low five, points inside, at the
 * edges (corner cells outside the array) and outside the grid, random scale
 * and offset. The function only reads, so the whole arena must stay equal;
 * EDX:EAX must match for random entry registers. */
static int terrain_cases(unsigned index,unsigned limit) {
    const es2_hook_descriptor *d=function_hook_descriptor(index); current_function=d->name;
    const char *override=getenv("ES2_INVITRO_TERRAIN_CASES");
    unsigned cases=case_budget(limit,1000000);if(!limit&&override&&*override)cases=(unsigned)strtoul(override,NULL,10);
    uint64_t zero=0;
    if(!limit) coverage_enable(1);
    int status=0;
    for(unsigned n=0;n<cases && !status;++n) {
        if(!limit && n==65536) coverage_enable(0);
        random_bytes(advance_arena,sizeof(advance_arena)); advance_used=0;
        uint32_t columns=n%10?1+random32()%4:0,rows=n%10==5?0:1+random32()%4,shift=random32()%4==0?random32()%32:random32()%13;
        /* One case in 64: 30 column bits and 2 row bits, so (row << 30) can
         * reach the sign bit (negative cell index, 46e224); every result is 0
         * before any cell is read except cell 0. */
        BOOL wide=n%64==63; if(wide) { columns=30; rows=2; shift=0; }
        BYTE *g=advance_alloc(0x11c),*cells=advance_alloc(wide?16u:16u<<(columns+rows)),*p=advance_alloc(8);
        uint32_t high=n%8==0?random32()&0xffffffe0u:0;
        put32(g+0xec,advance_va(cells));
        put32(g+0x100,columns|high); put32(g+0x104,rows|(n%16==0?random32()&0xffffffe0u:0));
        put32(g+0x108,shift|(n%4==0?random32()&0xffffffe0u:0));
        if(n%3==0) { put32(g+0x110,random32()%4096); put32(g+0x118,random32()%64); }
        /* Coordinates around the grid: one cell outside on each side. */
        for(unsigned axis=0;axis<2;++axis) {
            uint32_t count=1u<<(axis?rows:columns),value=random32();
            if(wide) { value=random32()%4; put32(p+4*axis,value); continue; }
            if(shift<24 && random32()%8) {
                uint32_t cell=(uint32_t)(random32()%(count+2))-1u;
                value=(cell<<shift)+(shift?random32()%(1u<<shift):0);
            }
            put32(p+4*axis,value);
        }
        uint32_t a[ES2_HOOK_MAX_ARGS]={0},registers[3]={random32(),random32(),random32()};
        a[0]=advance_va(g); a[1]=advance_va(p);
        memcpy(advance_before,advance_arena,sizeof(advance_arena));
        uint64_t got=actor_call(index,a,registers);
        uint64_t want=es2_terrain_height_registers(&harness_memory,a[0],a[1]);
        if(got!=want || memcmp(advance_arena,advance_before,sizeof(advance_arena))) {
            fprintf(stderr,"FAIL: %s case %u EAX original=%08x core=%08x EDX original=%08x core=%08x columns=%08x rows=%08x shift=%08x point=%08x,%08x\n",
                d->name,n,(uint32_t)got,(uint32_t)want,(uint32_t)(got>>32),(uint32_t)(want>>32),
                get32(g+0x100),get32(g+0x104),get32(g+0x108),get32(p),get32(p+4));
            status=1;
        }
        zero+=(uint32_t)got==0;
    }
    if(!limit) coverage_enable(0);
    if(status) return 1;
    printf("PASS: %-20s %u cases; exact EDX:EAX, read-only arena; zero heights=%llu (outside or edge)\n",
        d->name,cases,(unsigned long long)zero);
    fflush(stdout); return 0;
}
