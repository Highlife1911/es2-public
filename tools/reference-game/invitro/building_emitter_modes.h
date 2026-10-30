/* Included after the building graph helpers and before building_emit_cases.
 * Matrix/short-vector callees run from the read-only source EXE's in-memory
 * mapping. No original code or tables are embedded in this fixture. */
static void be_modes_matrix(BYTE *matrix,unsigned mode,unsigned n) {
    memset(matrix,0,32);
    put16(matrix,16384);put16(matrix+8,16384);put16(matrix+16,16384);
    matrix[18]=(BYTE)mode;matrix[19]=(BYTE)(0x6du+n);
    put32(matrix+20,100u+n);put32(matrix+24,0u-200u-n);put32(matrix+28,50u+3u*n);
}
static void be_modes_build(unsigned n,uint32_t a[6],uint32_t r[3]) {
    be_build(n+1,a,r);uint32_t b=a[0];
    be_flags=(2u<<14)|((n%3==1)?1u<<12:0); /* Resource index zero; alternate barrel. */
    r2_arena[0x99]=0;put32(r2_arena+0x1a4,b+0x400);be_pick=b+0x400;
    be_distance=20000;be_speed=0;put16(r2_arena+0xd0a,0);
    put16(r2_arena+0x824,0);put16(r2_arena+0x826,5);put16(r2_arena+0x82e,n%2?2:0);
    put16(r2_arena+0xb00,0);put16(r2_arena+0xb40,2);
    put16(r2_arena+0x1f7,1);put16(r2_arena+0x21e,10000);
    put16(r2_arena+0x219,n%2?1000:0);put16(r2_arena+0x21b,n%2?1:0);
    put16(r2_arena+0x212,0);put16(r2_arena+0xd20,0);put16(r2_arena+0xd24,0);
    put16(r2_arena+0x32,0);
    put16(r2_arena+0x0c,(uint16_t)(n*127));put16(r2_arena+0x0e,(uint16_t)(n*251));
    put16(r2_arena+0x10,(uint16_t)(n*509));
    put32(r2_arena+0x26,100);put32(r2_arena+0x2a,200);put32(r2_arena+0x2e,300);
    be_modes_matrix(r2_arena+0xc00,n%3==2?3:n%3,n);
    put32(r2_arena+0xd60,300);put32(r2_arena+0xd64,400);put32(r2_arena+0xd68,0);
}
static int building_emit_modes(BYTE saved[15][5]) {
    static const unsigned restored[]={2,9,10,11,12};
    static const unsigned nested[]={ES2_HOOK_INDEX_timer_word_step,
        ES2_HOOK_INDEX_euler_matrix,ES2_HOOK_INDEX_matrix_compose,
        ES2_HOOK_INDEX_matrix_angles,ES2_HOOK_INDEX_time_scale,
        ES2_HOOK_INDEX_transform_point};
    uint16_t old_scale=get16(mapped+0xd3b64);uint64_t entered[6]={0};
    int status=0;unsigned total=0,emissions=0;
    for(unsigned j=0;j<sizeof(restored)/sizeof(restored[0]);++j) {
        unsigned i=restored[j];
        if(actor_patch(be_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned mode=0;mode<6&&!status;++mode)
    for(unsigned n=0;n<32&&!status;++n) {
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2];
        uint32_t poison=n&1?0xa7a7a7a7u:0x31313131u;
        BYTE expected_fpu[108],actual_fpu[108];
        es2_hook hooks[7]={{0}};es2_hook_options options={0};
        options.image=mapped;options.expected_base=0x400000;options.expected_image_size=0x2d8000;
        be_modes_build(n,a,r);es2_hook_set_fast_path(fast);
        for(unsigned j=0;j<6;++j) {
            options.mode=(es2_hook_mode)((mode+j)%3);
            if(!weapons_install_hook(hooks+j,function_hook_descriptor(nested[j]),&options,
                                    function_hook_extension(nested[j]))) {
                status=fail("building nested mode install");break;
            }
        }
        if(!status) {
            /* These two setup callees feed the scale and target position.
             * 48043c is NOT a nested call of building 404100: the body uses
             * the distinct original short-vector transform 4801d4. */
            uint32_t scale_args[2]={125,32+n*13u};es2_weapon_registers scale_regs={0};
            weapons_machine_call((uint32_t)(uintptr_t)(mapped+0x67894),scale_args,2,&scale_regs);
            put16(mapped+0xd3b64,(uint16_t)scale_regs.eax);
            uint32_t point_args[3]={a[0]+0xd60,a[0]+0xc00,a[0]+0xd80};
            es2_weapon_registers point_regs={0};
            weapons_machine_call((uint32_t)(uintptr_t)(mapped+0x8043c),point_args,3,&point_regs);
            memcpy(r2_arena+0x426,r2_arena+0xd80,12);
            memcpy(r2_before,r2_arena,sizeof(r2_arena));r2_calls=0;r2_stream=r2_seed;be_random_calls=0;
            es2_probe_call(mapped+0x4100,a[0],0,0,poison,expected_pair);
            es2_hook_fpu_save(expected_fpu);
            unsigned calls=r2_calls;r2_record expected[40];
            memcpy(expected,r2_log,calls*sizeof(*expected));memcpy(r2_after,r2_arena,sizeof(r2_arena));
            for(unsigned j=0;j<calls;++j)if(expected[j].id==13||expected[j].id==14)++emissions;
            options.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hooks+6,function_hook_descriptor(ES2_HOOK_INDEX_turret),
                                    &options,function_hook_extension(ES2_HOOK_INDEX_turret)))
                status=fail("building caller mode install");
            if(!status) {
                memcpy(r2_arena,r2_before,sizeof(r2_arena));r2_calls=0;r2_stream=r2_seed;be_random_calls=0;
                es2_probe_call(mapped+0x4100,a[0],0,0,poison,actual_pair);
                es2_hook_fpu_save(actual_fpu);
                if(memcmp(expected_pair,actual_pair,8)||calls!=r2_calls
                   ||memcmp(expected,r2_log,calls*sizeof(*expected))
                   ||memcmp(r2_after,r2_arena,sizeof(r2_arena))
                   ||get16(expected_fpu)!=get16(actual_fpu)
                   ||get16(expected_fpu+4)!=get16(actual_fpu+4)
                   ||get16(expected_fpu+8)!=get16(actual_fpu+8)) {
                    fprintf(stderr,"FAIL: building mixed fast=%u mode=%u case=%u pair=%08x:%08x/%08x:%08x calls=%u/%u\n",
                            fast,mode,n,expected_pair[1],expected_pair[0],actual_pair[1],actual_pair[0],calls,r2_calls);
                    for(unsigned j=0;j<calls&&j<r2_calls;++j)if(memcmp(expected+j,r2_log+j,sizeof(*expected))) {
                        uint32_t *e=(uint32_t *)(expected+j),*f=(uint32_t *)(r2_log+j);
                        for(unsigned k=0;k<sizeof(*expected)/4;++k) { if(e[k]!=f[k])
                            fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]); }
                        break;
                    }
                    for(unsigned j=0;j<sizeof(r2_arena);++j)if(r2_after[j]!=r2_arena[j]) {
                        fprintf(stderr," arena +%x %02x/%02x\n",j,r2_after[j],r2_arena[j]);break;
                    }
                    status=1;
                }
            }
            ++total;
        }
        for(unsigned j=7;j>0;--j) {
            unsigned i=j-1;if(i<6)entered[i]+=hooks[i].calls;
            if(hooks[i].mismatches)status=fail("building nested compare mismatch");
            if(hooks[i].descriptor&&!es2_hook_uninstall(hooks+i))return fail("building mode uninstall");
        }
    }
    es2_hook_set_fast_path(TRUE);put16(mapped+0xd3b64,old_scale);
    for(unsigned j=0;j<6;++j)if(!entered[j])status=fail("building expected mixed callee never entered");
    if(!emissions)status=fail("building mixed constructors never entered");
    if(!status)printf("PASS: turret %u mixed cases, %u emissions; real timer/Euler/compose/angles/short-transform, timing/long-transform setup, 31/a7 stack, both dispatch paths\n",total,emissions);
    return status;
}
