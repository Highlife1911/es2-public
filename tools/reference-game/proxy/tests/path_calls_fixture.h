/* Synthetic callees verify argument forwarding, pair returns and attribution.
 * A virtual slot deliberately resolves to a direct callee's address too. */
static uint32_t call_args[5],call_count;
static const uint64_t call_result=UINT64_C(0x87654321fedcba98);
static uint64_t __cdecl call_one(uint32_t a) { call_args[0]=a; ++call_count; return call_result; }
static uint64_t __cdecl call_height(uint32_t a,uint32_t b) {
    call_args[0]=a; memcpy(call_args+1,(const void *)(uintptr_t)b,12); ++call_count; return call_result;
}
static uint64_t __cdecl call_clip(uint32_t a,uint32_t b,uint32_t c,uint32_t d) {
    call_args[0]=a; call_args[1]=b; call_args[2]=c; call_args[3]=d; ++call_count; return call_result;
}
static void own_jump(BYTE *image,uint32_t rva,const void *target) {
    BYTE *p=image+rva; p[0]=0xe9;
    math_native_write32(NULL,(uint32_t)(uintptr_t)(p+1),(uint32_t)(uintptr_t)target-(uint32_t)(uintptr_t)(p+5));
}
static BYTE scene[4096],scene_before[4096],scene_after[4096];
static uint32_t vtable_a[32],vtable_b[32],part_vtable[9];
static uint32_t scene_calls[16];
static unsigned scene_mode;
static uint32_t va(const void *p) { return (uint32_t)(uintptr_t)p; }
static uint64_t __cdecl radius_a(uint32_t a) { (void)a; ++scene_calls[0]; return 100; }
static uint64_t __cdecl radius_b(uint32_t a) { (void)a; ++scene_calls[1]; return 200; }
static uint64_t __cdecl reach_b(uint32_t a) { (void)a; ++scene_calls[2]; return 100; }
static uint64_t __cdecl touch_b(uint32_t a) { (void)a; ++scene_calls[3]; return call_result; }
static uint64_t __cdecl hit(uint32_t a,uint32_t b,uint32_t c,uint32_t d,uint32_t e) {
    (void)a;(void)b;(void)c;(void)d;(void)e; ++scene_calls[4]; return call_result;
}
static uint64_t __cdecl damage(uint32_t a,uint32_t b,uint32_t c,uint32_t d) {
    (void)a;(void)b;(void)c;(void)d; ++scene_calls[5]; return call_result;
}
static uint64_t __cdecl part(uint32_t a,uint32_t b) { (void)a;(void)b; ++scene_calls[6]; return 0; }
static uint64_t __cdecl buildings(uint32_t a,uint32_t b,uint32_t c) {
    (void)a;(void)b;(void)c; ++scene_calls[7]; return 0;
}
static uint64_t __cdecl face(uint32_t a,uint32_t b) { (void)a;(void)b; ++scene_calls[8]; return va(scene+0xe00); }
static uint64_t __cdecl bounded(uint32_t a,uint32_t b) { (void)a;(void)b; ++scene_calls[9]; return 1; }
static uint64_t __cdecl effect(uint32_t a) { (void)a; ++scene_calls[10]; return call_result; }
static uint64_t __cdecl sound(uint32_t a) { (void)a; ++scene_calls[11]; return call_result; }
static void reset_counters(void) {
    for(unsigned i=0;i<GROUND_UPDATE_COUNTERS;++i) ground_update_counters[i].count=0;
    for(unsigned i=0;i<GROUND_COLLIDE_COUNTERS;++i) ground_collide_counters[i].count=0;
}
static int calls_case(void) {
    BYTE *image=VirtualAlloc(NULL,RNG_HOOK_IMAGE_SIZE,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
    CHECK(image);
    uint64_t (*one[])(void *,uint32_t)={math_snapshot_game,math_ground_animate_game,math_collide_game,
        math_rollback_game,math_random_game,math_sound_game};
    const unsigned indices[]={0,1,3,4,5,6,8};
    for(unsigned i=0;i<7;++i) own_jump(image,ground_update_counters[indices[i]].key,(void *)call_one);
    own_jump(image,0x6e180,(void *)call_height); own_jump(image,0x792ac,(void *)call_clip);
    FlushInstructionCache(GetCurrentProcess(),image,RNG_HOOK_IMAGE_SIZE);
    for(unsigned i=0;i<6;++i) { CHECK(one[i](image,0x98765432)==call_result); CHECK(call_args[0]==0x98765432); }
    CHECK(math_effects_game(image,0x98765432,0x11223344,0x55667788)==call_result);CHECK(call_args[0]==0x98765432);
    CHECK(math_height_game(image,7,8,9,10)==call_result);
    CHECK(call_args[0]==7 && call_args[1]==8 && call_args[2]==9 && call_args[3]==10);
    CHECK(math_clip_game(image,11,12,13,14)==call_result);
    CHECK(call_args[0]==11 && call_args[1]==12 && call_args[2]==13 && call_args[3]==14 && call_count==9);
    for(unsigned i=0;i<GROUND_UPDATE_COUNTERS;++i) CHECK(ground_update_counters[i].count==1);
    for(unsigned i=0;i<GROUND_COLLIDE_COUNTERS;++i) CHECK(!ground_collide_counters[i].count);

    own_jump(image,0x04ae4,(void *)buildings); own_jump(image,0x6e498,(void *)face);
    own_jump(image,0x92f24,(void *)bounded); own_jump(image,0x34010,(void *)effect); own_jump(image,0x62830,(void *)sound);
    FlushInstructionCache(GetCurrentProcess(),image,RNG_HOOK_IMAGE_SIZE);
    vtable_a[0x5c/4]=va(radius_a); vtable_b[0x5c/4]=va(radius_b); vtable_b[0x7c/4]=va(reach_b);
    vtable_b[0x68/4]=va(touch_b); vtable_a[0x70/4]=vtable_b[0x70/4]=va(hit);
    vtable_a[0x74/4]=va(damage); part_vtable[0x20/4]=va(part);
    /* 0 idle, 1 flagged contact (two classes' +5c), 2 impact/hit, 3 slope
     * accumulation, 4 slide damage, 5 uphill early return (no accumulation),
     * 6 direct sound address as virtual +68 (must only count the slot). */
    for(scene_mode=0;scene_mode<7;++scene_mode) {
        memset(scene,0,sizeof(scene)); memset(scene_calls,0,sizeof(scene_calls)); reset_counters();
        es2_collide_globals g=math_collide_globals(image);
        g.terrain=va(scene+0xf00); g.objects=va(scene+0xf04); g.object_count=va(scene+0xf08);
        g.drift=va(scene+0xf10); g.drift_enabled=va(scene+0xf18); g.last_position=va(scene+0xf20);
        g.damage_level=va(scene+0xf30); g.damage_table=va(scene+0xf32); g.default_part=va(scene+0xf40);
        uint32_t a=va(scene),b=va(scene+0x300),ca=va(scene+0x600),cb=va(scene+0x700);
        math_native_write32(NULL,a,va(vtable_a)); math_native_write32(NULL,b,va(vtable_b));
        math_native_write32(NULL,a+0x1f2,ca); math_native_write32(NULL,b+0x1f2,cb);
        math_native_write32(NULL,b+0x45,va(scene+0x900));
        math_native_write32(NULL,g.objects,va(scene+0x800));
        math_native_write32(NULL,va(scene+0x800),b);
        if(scene_mode==1 || scene_mode==2 || scene_mode==6) math_native_write16(NULL,g.object_count,1);
        math_native_write16(NULL,b+0x1a8,scene_mode==1 || scene_mode==6);
        math_native_write16(NULL,a+0x32,1); math_native_write16(NULL,b+0x32,1);
        math_native_write16(NULL,ca+0x4e,1024); math_native_write16(NULL,a+0x28e,1024);
        math_native_write32(NULL,a+0x34,va(scene+0xa00)); math_native_write32(NULL,b+0x34,va(scene+0xa00));
        math_native_write32(NULL,va(scene+0xa04),va(scene+0xb00)); math_native_write32(NULL,va(scene+0xb00),va(part_vtable));
        scene[0xa3]=(BYTE)(scene_mode>=3 && scene_mode<=5);
        math_native_write16(NULL,va(scene+0xe04),scene_mode==3 || scene_mode==5?100:2000);
        math_native_write16(NULL,va(scene+0xe00),1024); math_native_write16(NULL,va(scene+0xe02),1024);
        math_native_write32(NULL,g.drift_enabled,scene_mode==4);
        math_native_write32(NULL,g.drift,scene_mode==4?1000:0);
        if(scene_mode==5) math_native_write32(NULL,a+0x2e,1);
        math_native_write16(NULL,g.damage_table,1024);
        vtable_b[0x68/4]=scene_mode==6?va(image+0x62830):va(touch_b);
        memcpy(scene_before,scene,sizeof(scene));
        g.observe=NULL;
        uint64_t baseline=es2_ground_collide_registers(&math_native_memory,a,&g,math_guest_call,NULL);
        memcpy(scene_after,scene,sizeof(scene));
        uint32_t expected_calls[16]; memcpy(expected_calls,scene_calls,sizeof(scene_calls));
        for(unsigned i=0;i<GROUND_COLLIDE_COUNTERS;++i) CHECK(!ground_collide_counters[i].count);
        memcpy(scene,scene_before,sizeof(scene)); memset(scene_calls,0,sizeof(scene_calls));
        g.observe=ground_collide_observe;
        CHECK(es2_ground_collide_registers(&math_native_memory,a,&g,math_guest_call,NULL)==baseline);
        CHECK(!memcmp(scene,scene_after,sizeof(scene)) && !memcmp(scene_calls,expected_calls,sizeof(scene_calls)));
        CHECK(ground_collide_counters[6].count==scene_calls[0]+scene_calls[1]);
        CHECK(ground_collide_counters[10].count==scene_calls[2]);
        CHECK(ground_collide_counters[7].count==scene_calls[3]+(scene_mode==6));
        CHECK(ground_collide_counters[8].count==scene_calls[4]);
        CHECK(ground_collide_counters[9].count==scene_calls[5]);
        CHECK(ground_collide_counters[5].count==scene_calls[6]);
        for(unsigned i=0;i<5;++i) CHECK(ground_collide_counters[i].count==scene_calls[7+i]-(i==4 && scene_mode==6));
        CHECK(ground_collide_counters[11].count==(scene_mode==1 || scene_mode==2 || scene_mode==6));
        CHECK(ground_collide_counters[12].count==(scene_mode==3));
        CHECK(ground_collide_counters[13].count==(scene_mode==4));
        if(scene_mode==2) CHECK(scene_calls[4]==2 && scene_calls[6]==2);
        if(scene_mode==4) CHECK(scene_calls[5]==6 && scene_calls[9]==6);
        for(unsigned i=0;i<GROUND_UPDATE_COUNTERS;++i) CHECK(!ground_update_counters[i].count);
    }
    VirtualFree(image,0,MEM_RELEASE);
    puts("PASS: nine ground callbacks, five direct callees, six class-independent slots; observer/null results, writes and calls identical; step 3/6/7 paths");
    return 0;
}
