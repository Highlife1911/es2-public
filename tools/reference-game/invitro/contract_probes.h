extern void __cdecl es2_probe_call(void *,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t *);
static int probe_divide(uint32_t va,unsigned count,const uint32_t *args,int expected) {
    es2_hook_result result;es2_hook_call call={(void *)(uintptr_t)va,ES2_ABI_CDECL,count,args,initial_fpu,&result,NULL};
    int faulted=0;divide_site=0;
    if(setjmp(divide_return))faulted=1;
    else {expecting_divide=1;es2_hook_machine_call(&call);expecting_divide=0;}
    if(faulted!=expected)return fail("division-domain probe disagrees");
    printf("DOMAIN VA=%08x fault=%d site=%08x args=%08x,%08x\n",va,faulted,(unsigned)divide_site,args[0],args[1]);return 0;
}
static int run_contract_probes(void) {
    current_function="contract-probes";
    uint32_t args[ES2_HOOK_MAX_ARGS]={0};
    /* Exact signed IDIV limits, one value either side, zero denominator. */
    static const uint32_t scalars[][6]={
        {0,0,0,0,1,1},{0x7fffffff,0,1,0,1,0},{0x80000000,0,1,0,1,0},
        {0x7fffffff,0,1,0,2,1},{0x80000000,0,1,0,0xffffffff,1}};
    for(unsigned i=0;i<sizeof(scalars)/sizeof(scalars[0]);++i)if(probe_divide(0x47df48,5,scalars[i],(int)scalars[i][5]))return 1;
    static const uint32_t ratios[][3]={{0x80000000,0,0},{0x80000000,1,1},{1,0x80000000,1},{0,0x80000000,1},{0x80000000,0x80000000,1},{0x7fffffff,0x7fffffff,0}};
    for(unsigned i=0;i<sizeof(ratios)/sizeof(ratios[0]);++i) {
        args[0]=ratios[i][0];args[1]=ratios[i][1];if(probe_divide(0x47d32c,2,args,(int)ratios[i][2]))return 1;
    }
    BYTE track[96]={0},out[32]={0},pose[12]={0},parent[32]={0};uint32_t result[2];
    chain_identity(parent);chain_identity(track+0x2e);put16(pose,1234);
    es2_probe_call((void *)0x478bc4,(uint32_t)(uintptr_t)pose,(uint32_t)(uintptr_t)parent,(uint32_t)(uintptr_t)out,0x31313131,result);
    BYTE first[32];memcpy(first,out,32);
    es2_probe_call((void *)0x478bc4,(uint32_t)(uintptr_t)pose,(uint32_t)(uintptr_t)parent,(uint32_t)(uintptr_t)out,0xa7a7a7a7,result);
    if(first[19]!=0x31||out[19]!=0xa7)return fail("pose-parent padding probe");
    first[19]=out[19];if(memcmp(first,out,32))return fail("pose-parent poison escaped padding");
    puts("PASS: 478bc4 same inputs, stack poison 31/a7 changes only matrix byte 19 (padding contract)");
    static const uint32_t wrappers[]={0x478f6c,0x478ff4,0x4790b4,0x479194};
    for(unsigned w=0;w<4;++w) {
        BYTE saved[96],first_track[96],first_out[32];
        memset(track,0,sizeof(track));chain_identity(track+0x2e);chain_identity(out);
        put16(track+0x20,1);put16(track+0x28,1234);put16(track+0x1c,1);put16(track+0x1e,1);
        memcpy(saved,track,sizeof(track));
        es2_probe_call((void *)(uintptr_t)wrappers[w],(uint32_t)(uintptr_t)track,(uint32_t)(uintptr_t)out,0,0x31313131,result);
        memcpy(first_track,track,sizeof(track));memcpy(first_out,out,sizeof(out));
        memcpy(track,saved,sizeof(track));chain_identity(out);
        es2_probe_call((void *)(uintptr_t)wrappers[w],(uint32_t)(uintptr_t)track,(uint32_t)(uintptr_t)out,0,0xa7a7a7a7,result);
        BYTE *a=w==2?first_out:first_track+0x2e,*b=w==2?out:track+0x2e;
        if(a[19]!=0x31||b[19]!=0xa7)return fail("track wrapper padding probe");
        a[19]=b[19];if(memcmp(first_track,track,sizeof(track))||memcmp(first_out,out,sizeof(out)))return fail("track poison escaped padding");
        printf("PASS: %08x same inputs, stack poison 31/a7 changes only matrix byte 19\n",wrappers[w]);
    }
    args[0]=(uint32_t)(uintptr_t)track;args[1]=(uint32_t)(uintptr_t)out;
    put16(track+0x1e,0);
    put16(track+0x20,0);if(probe_divide(0x4790b4,2,args,0)||probe_divide(0x479194,2,args,0))return 1;
    put16(track+0x20,1);if(probe_divide(0x4790b4,2,args,1)||probe_divide(0x479194,2,args,1))return 1;
    put16(track+0x1e,0xffff);put16(track+0x1c,0x8000);
    for(unsigned i=0x22;i<0x2e;i+=2)put16(track+i,0x8000);
    if(probe_divide(0x4790b4,2,args,0)||probe_divide(0x479194,2,args,0))return 1;
    puts("PASS: division boundaries and disabled-track bypass; no division fault emulated by core");return 0;
}
