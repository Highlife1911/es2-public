/* 40250c leaf order on a concrete manager path: no active tracks, a dirty
 * root pose, optionally a dirty child whose parent has a full rotation.
 * Manager traversal/flag writes and memset are own orchestration, not calls
 * into the original loader/CRT. Each math call uses the original argument
 * flow. This does not claim to cover active clip/virtual dispatch paths. */
static void chain_leaf(int core,unsigned index,void *a,void *b,void *c) {
    uint32_t args[ES2_HOOK_MAX_ARGS]={(uint32_t)(uintptr_t)a,(uint32_t)(uintptr_t)b,(uint32_t)(uintptr_t)c};
    if(core) function_hook_descriptor(index)->replacement(args,mapped);else (void)original(index,args);
}
static void chain_identity(BYTE *matrix) {
    memset(matrix,0,32);put16(matrix+16,16384);put16(matrix+6,16384);put16(matrix,16384);
}
static void chain_execute(int core,BYTE *actor,BYTE *manager,BYTE *poses,BYTE *matrices,BYTE *flags,int child) {
    BYTE identity[32],temporary[32],position[12],angles[6];chain_identity(identity);
    /* 478b7c sees M+1a == NULL: no 479194 call. 478d38 descends through
     * 478aac, its 4789d0 sort, and 478b00 each call track_count and execute
     * zero loop bodies. */
    chain_leaf(core,ES2_HOOK_INDEX_track_count,manager,NULL,NULL);
    chain_leaf(core,ES2_HOOK_INDEX_track_count,manager,NULL,NULL);
    chain_leaf(core,ES2_HOOK_INDEX_track_count,manager,NULL,NULL);
    /* 478c64 root relation (-1,0): 478c1c. Its optional child (0,1)
     * descends into 478bc4, whose leaves are Euler then composition. */
    chain_leaf(core,ES2_HOOK_INDEX_pose_matrix,poses,matrices,NULL);flags[0]|=2;
    if(child) {
        memset(temporary,0,32);
        put32(temporary+28,(uint32_t)(int32_t)(int16_t)get16(poses+22));
        put32(temporary+20,(uint32_t)(int32_t)(int16_t)get16(poses+18));
        put32(temporary+24,(uint32_t)(int32_t)(int16_t)get16(poses+20));
        chain_leaf(core,ES2_HOOK_INDEX_euler_matrix,poses+12,temporary,NULL);
        chain_leaf(core,ES2_HOOK_INDEX_matrix_compose,temporary,matrices,matrices+32);flags[1]|=2;
    }
    memset(flags,0,child?2:1);
    if(!get16(actor+0x32)) {
        chain_leaf(core,ES2_HOOK_INDEX_euler_matrix,actor+12,actor+18,NULL);put16(actor+0x32,1);
    }
    chain_leaf(core,ES2_HOOK_INDEX_transform_point,matrices+20,actor+18,position);
    put32(actor+0x2e,get32(position+8));put32(actor+0x26,get32(position));put32(actor+0x2a,get32(position+4));
    chain_leaf(core,ES2_HOOK_INDEX_matrix_angles,matrices,angles,NULL);
    put16(angles+4,get16(angles+4)+get16(actor+16));
    put16(angles,get16(angles)+get16(actor+12));put16(angles+2,get16(angles+2)+get16(actor+14));
    put32(actor+12,get32(angles));put16(actor+16,get16(angles+4));put16(actor+0x32,0);
    chain_identity(matrices);
}
static int run_chain(void) {
    current_function="40250c-leaf-chain";
    BYTE arena[256],before[256],after[256];
    for(unsigned n=0;n<4096;++n) {
        random_bytes(arena,sizeof(arena));
        BYTE *actor=arena,*manager=arena+64,*poses=arena+136,*matrices=arena+160,*flags=arena+224;
        memset(manager+26,0,40);flags[0]=flags[1]=1;
        int child=(n&2)!=0;
        if(child) {put16(poses,(get16(poses)|1));put16(poses+12,(get16(poses+12)|1));}
        else if(n%8<6) {put16(poses,0);if(n%8<4)put16(poses+2,0);if(n%8<2)put16(poses+4,0);}
        for(unsigned j=6;j<12;j+=2)put16(poses+j,(uint16_t)((int)(random32()%4001)-2000));
        put16(actor+0x32,n&1);
        if(n&1)chain_leaf(0,ES2_HOOK_INDEX_euler_matrix,actor+12,actor+18,NULL);
        memcpy(before,arena,sizeof(arena));chain_execute(0,actor,manager,poses,matrices,flags,child);
        memcpy(after,arena,sizeof(arena));memcpy(arena,before,sizeof(arena));chain_execute(1,actor,manager,poses,matrices,flags,child);
        if(memcmp(arena,after,sizeof(arena))) {fprintf(stderr,"FAIL: 40250c leaf chain case %u\n",n);return 1;}
    }
    puts("PASS: 40250c leaf chain 4096 states; zero-track root/child paths, both cache states, full actor/matrix/flags/guards");return 0;
}
