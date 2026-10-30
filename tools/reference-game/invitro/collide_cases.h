/* 0x418f74 ground_collide. A scene of up to six objects around vehicle A,
 * each with a stub vtable (+0x5c radius, +0x68 touch, +0x70 hit, +0x74 part
 * damage, +0x7c reach), part lists with a stub +0x20 lookup and 32-byte part
 * records; the game callees 0x404ae4, 0x46e498, 0x492f24, 0x434010 and
 * 0x462830 are replaced by stubs in the private mapping. The arithmetic
 * helpers run as original code in the mapping and as core. Stubs log their
 * arguments (buffer contents, not stack addresses) and return script values;
 * hit and obstacle stubs also change the buffers they get. Both runs must give
 * the same call log, arena, globals and EDX:EAX. */
static uint32_t collide_script[128],collide_log[256],collide_actor;
static unsigned collide_next,collide_logged;
static BYTE *collide_entries[3],*collide_faces[2];
static uint32_t collide_take(void) { return collide_script[collide_next++%128]; }
static void collide_note(uint32_t value) { if(collide_logged<256) collide_log[collide_logged++]=value; }
static uint64_t collide_pair(void) { uint32_t eax=collide_take(); return ((uint64_t)collide_take()<<32)|eax; }
static void collide_note_point(uint32_t point) { const BYTE *p=(const BYTE *)(uintptr_t)point; for(unsigned i=0;i<3;++i) collide_note(get32(p+4*i)); }
/* A word for the radius and reach methods: small, sometimes 0 or negative. */
static uint64_t collide_size(void) {
    uint64_t r=collide_pair(); uint32_t pick=collide_take()%8;
    uint16_t size=pick==0?0:pick==1?(uint16_t)(0u-collide_take()%0x400):(uint16_t)(collide_take()%0x900);
    return (r&~(uint64_t)0xffffu)|size;
}
static uint64_t __cdecl collide_radius(uint32_t o) { collide_note(1); collide_note(o); return collide_size(); }
static uint64_t __cdecl collide_reach(uint32_t o) { collide_note(2); collide_note(o); return collide_size(); }
static uint64_t __cdecl collide_touch(uint32_t o) { collide_note(3); collide_note(o); return collide_pair(); }
static uint64_t __cdecl collide_hit(uint32_t o,uint32_t impact,uint32_t point,uint32_t k,uint32_t other) {
    collide_note(4); collide_note(o); collide_note(impact); collide_note_point(point); collide_note(k); collide_note(other);
    if(collide_take()%2) put32((BYTE *)(uintptr_t)point+8,collide_take());
    return collide_pair();
}
static uint64_t __cdecl collide_damage(uint32_t o,uint32_t part,uint32_t value,uint32_t zero) {
    collide_note(5); collide_note(o); collide_note(part); collide_note(value); collide_note(zero); return collide_pair();
}
static uint64_t __cdecl collide_part(uint32_t list,uint32_t word) {
    collide_note(6); collide_note(list); collide_note(word);
    uint64_t r=collide_pair(); unsigned pick=collide_take()%4;
    return (r&~(uint64_t)0xffffffffu)|(pick<3?advance_va(collide_entries[pick]):0u);
}
static uint64_t __cdecl collide_buildings(uint32_t point,uint32_t radius,uint32_t zero) {
    collide_note(7); collide_note_point(point); collide_note(radius); collide_note(zero);
    if(collide_take()%4==0) put32((BYTE *)(uintptr_t)point+8,collide_take());
    uint64_t r=collide_pair(); if(collide_take()%4) r&=~(uint64_t)0xffffu;
    return r;
}
/* No face (a null pointer) only where the original does not dereference it. */
static uint64_t __cdecl collide_face(uint32_t terrain,uint32_t point) {
    collide_note(8); collide_note(terrain); collide_note_point(point);
    uint64_t r=collide_pair(); unsigned pick=collide_take()%3;
    if(pick==2 && ((BYTE *)(uintptr_t)collide_actor)[0xa3]) pick=0;
    return (r&~(uint64_t)0xffffffffu)|(pick<2?advance_va(collide_faces[pick]):0u);
}
static uint64_t __cdecl collide_bounded(uint32_t g,uint32_t bound) { collide_note(9); collide_note(g); collide_note(bound); return collide_pair(); }
static uint64_t __cdecl collide_effect(uint32_t x) { collide_note(10); collide_note(x); return collide_pair(); }
static uint64_t __cdecl collide_sound(uint32_t id) { collide_note(11); collide_note(id); return collide_pair(); }

static uint64_t collide_call(void *c,uint32_t function,const uint32_t *x,unsigned count) {
    (void)c; void *f=(void *)(uintptr_t)function;
    switch(count) {
    case 1: return ((uint64_t (__cdecl *)(uint32_t))f)(x[0]);
    case 2: return ((uint64_t (__cdecl *)(uint32_t,uint32_t))f)(x[0],x[1]);
    case 3: return ((uint64_t (__cdecl *)(uint32_t,uint32_t,uint32_t))f)(x[0],x[1],x[2]);
    case 4: return ((uint64_t (__cdecl *)(uint32_t,uint32_t,uint32_t,uint32_t))f)(x[0],x[1],x[2],x[3]);
    default: return ((uint64_t (__cdecl *)(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t))f)(x[0],x[1],x[2],x[3],x[4]);
    }
}

static const struct { uint32_t rva; void *stub; } collide_stubs[]={
    {0x04ae4,(void *)collide_buildings},{0x6e498,(void *)collide_face},{0x92f24,(void *)collide_bounded},
    {0x34010,(void *)collide_effect},{0x62830,(void *)collide_sound}};
#define COLLIDE_STUBS (sizeof(collide_stubs)/sizeof(collide_stubs[0]))
/* Terrain pointer, object list and count, drift/last position/flag, damage
 * level, default part record dword +0x1c. */
static const uint32_t collide_globals[][2]={{0xa0be0,4},{0xa9af8,8},{0xa9cd0,24},{0xa9e5c,2},{0x2c56c4,4}};
#define COLLIDE_GLOBALS (sizeof(collide_globals)/sizeof(collide_globals[0]))
static uint32_t collide_vtable[0x20],collide_list_vtable[0x10];

/* Vehicle or scene object near `center`: vtable, flag block, config, parts. */
static BYTE *collide_object(const BYTE *center,BYTE *records) {
    BYTE *o=advance_alloc(0x2c0),*config=advance_alloc(0x60),*flags=advance_alloc(0x18);
    BYTE *parts=advance_alloc(0x1a),*list=advance_alloc(8);
    put32(o,(uint32_t)(uintptr_t)collide_vtable); put32(o+0x45,advance_va(flags));
    if(random32()%4) put32(flags+0x14,0);
    put32(o+0x1f2,advance_va(config)); put32(o+0x34,advance_va(parts));
    put32(parts+4,advance_va(list)); put32(list,(uint32_t)(uintptr_t)collide_list_vtable);
    put32(parts+0x16,advance_va(records));
    put16(o+0x1a8,(uint16_t)(random32()%5<3?random32()%3:random32()));
    put16(o+0x32,(uint16_t)(random32()%2));
    if(random32()%2) put16(config+0x4e,(uint16_t)(random32()%0x800));
    if(random32()%2) put16(o+0x28e,(uint16_t)(random32()%0x1000-0x800));
    if(center) for(unsigned i=0;i<3;++i) put32(o+0x26+4*i,get32(center+0x26+4*i)+random32()%0x1000-0x800);
    return o;
}

static int collide_cases(unsigned index,unsigned limit) {
    const es2_hook_descriptor *d=function_hook_descriptor(index); current_function=d->name;
    const char *override=getenv("ES2_INVITRO_COLLIDE_CASES");
    unsigned cases=case_budget(limit,1000000);if(!limit&&override&&*override)cases=(unsigned)strtoul(override,NULL,10);
    BYTE saved[COLLIDE_STUBS][5],initial[COLLIDE_GLOBALS][24],before[COLLIDE_GLOBALS][24],after[COLLIDE_GLOBALS][24];
    uint32_t original_log[256]; unsigned original_logged;
    collide_vtable[0x5c/4]=(uint32_t)(uintptr_t)collide_radius; collide_vtable[0x68/4]=(uint32_t)(uintptr_t)collide_touch;
    collide_vtable[0x70/4]=(uint32_t)(uintptr_t)collide_hit; collide_vtable[0x74/4]=(uint32_t)(uintptr_t)collide_damage;
    collide_vtable[0x7c/4]=(uint32_t)(uintptr_t)collide_reach; collide_list_vtable[0x20/4]=(uint32_t)(uintptr_t)collide_part;
    for(unsigned i=0;i<COLLIDE_STUBS;++i) if(actor_patch(collide_stubs[i].rva,collide_stubs[i].stub,saved[i],TRUE)) return 1;
    for(unsigned i=0;i<COLLIDE_GLOBALS;++i) memcpy(initial[i],mapped+collide_globals[i][0],collide_globals[i][1]);
    const uint32_t base=(uint32_t)(uintptr_t)mapped;
    const es2_collide_globals globals={base+0xa0be0u,base+0xa9af8u,base+0xa9afeu,base+0xa9cd0u,base+0xa9ce4u,base+0xa9cd8u,
        base+0xa9e5cu,base+0x9a058u,base+0x2c56a8u,base+0xd2599u,base+0x92f24u,base+0x04ae4u,base+0x6e498u,
        base+0x34010u,base+0x62830u,MATH_QUARTER(d->user),ground_collide_observe,mapped};
    uint64_t contacts=0,damages=0,slides=0,obstacles=0;
    if(!limit) coverage_enable(1);
    int status=0;
    for(unsigned n=0;n<cases && !status;++n) {
        if(!limit && n==65536) coverage_enable(0);
        random_bytes(advance_arena,sizeof(advance_arena)); advance_used=0;
        for(unsigned i=0;i<128;++i) collide_script[i]=random32();
        for(unsigned i=0;i<COLLIDE_GLOBALS;++i) random_bytes(mapped+collide_globals[i][0],collide_globals[i][1]);
        BYTE *records=advance_alloc(4*32);
        for(unsigned i=0;i<3;++i) { collide_entries[i]=advance_alloc(6); put16(collide_entries[i]+4,(uint16_t)(random32()%5-1)); }
        for(unsigned i=0;i<2;++i) {
            collide_faces[i]=advance_alloc(6);
            if(random32()%2) put16(collide_faces[i]+4,(uint16_t)(0x5aa+random32()%5-2-(random32()%2?0xb54:0)));
        }
        BYTE *actor=collide_object(NULL,records); collide_actor=advance_va(actor);
        actor[0xa3]=(BYTE)(random32()%2);
        unsigned objects=random32()%7; BYTE *list=advance_alloc(4*(objects+1));
        for(unsigned i=0;i<objects;++i) {
            unsigned kind=random32()%8;
            put32(list+4*i,kind==0?advance_va(actor):kind==1?0:advance_va(collide_object(actor,records)));
        }
        put32(list+4*objects,0);
        put32(mapped+0xa9af8,advance_va(list));
        put16(mapped+0xa9afe,(uint16_t)(random32()%8==0?random32():random32()%(objects+2)));
        put16(mapped+0xa9e5c,(uint16_t)(random32()%4));
        if(random32()%2) put32(mapped+0xa9ce4,0);
        if(random32()%2) { put32(mapped+0xa9cd0,random32()%0x200-0x100); put32(mapped+0xa9cd4,random32()%0x200-0x100); }
        if(random32()%2) put32(mapped+0xa9ce0,get32(actor+0x2e)+random32()%3-1);
        uint32_t a[ES2_HOOK_MAX_ARGS]={0},registers[3]={random32(),random32(),random32()};
        a[0]=advance_va(actor);
        memcpy(advance_before,advance_arena,sizeof(advance_arena));
        for(unsigned i=0;i<COLLIDE_GLOBALS;++i) memcpy(before[i],mapped+collide_globals[i][0],collide_globals[i][1]);
        collide_next=0; collide_logged=0;
        uint64_t got=actor_call(index,a,registers);
        memcpy(original_log,collide_log,sizeof(collide_log)); original_logged=collide_logged;
        memcpy(advance_after,advance_arena,sizeof(advance_arena));
        for(unsigned i=0;i<COLLIDE_GLOBALS;++i) { memcpy(after[i],mapped+collide_globals[i][0],collide_globals[i][1]); memcpy(mapped+collide_globals[i][0],before[i],collide_globals[i][1]); }
        memcpy(advance_arena,advance_before,sizeof(advance_arena));
        collide_next=0; collide_logged=0;
        uint64_t counter_before[GROUND_COLLIDE_COUNTERS],observed[11]={0};
        for(unsigned i=0;i<GROUND_COLLIDE_COUNTERS;++i) counter_before[i]=ground_collide_counters[i].count;
        uint64_t want=es2_ground_collide_registers(&harness_memory,a[0],&globals,collide_call,NULL);
        BOOL globals_equal=TRUE;
        for(unsigned i=0;i<COLLIDE_GLOBALS;++i) globals_equal&=!memcmp(after[i],mapped+collide_globals[i][0],collide_globals[i][1]);
        if(got!=want || memcmp(advance_arena,advance_after,sizeof(advance_arena)) || !globals_equal ||
           original_logged!=collide_logged || memcmp(original_log,collide_log,sizeof(uint32_t)*collide_logged)) {
            unsigned diff=0; while(diff<sizeof(advance_arena) && advance_arena[diff]==advance_after[diff]) ++diff;
            unsigned log_diff=0; while(log_diff<collide_logged && log_diff<original_logged && original_log[log_diff]==collide_log[log_diff]) ++log_diff;
            fprintf(stderr,"FAIL: %s case %u EAX original=%08x core=%08x EDX original=%08x core=%08x arena_offset=%u globals_equal=%d log %u/%u differs at %u\n",
                d->name,n,(uint32_t)got,(uint32_t)want,(uint32_t)(got>>32),(uint32_t)(want>>32),diff,globals_equal,original_logged,collide_logged,log_diff);
            status=1;
        }
        for(unsigned i=0;i<original_logged;) {
            uint32_t id=original_log[i]; contacts+=id==4; damages+=id==5; slides+=id==11; obstacles+=id==7;
            static const unsigned sizes[12]={1,2,2,2,8,5,3,6,5,3,2,2};
            static const unsigned counters[11]={6,10,7,8,9,5,0,1,2,3,4};
            if(id>=1 && id<=11) ++observed[counters[id-1]];
            i+=id<12?sizes[id]:1;
        }
        for(unsigned i=0;i<11;++i) if(ground_collide_counters[i].count-counter_before[i]!=observed[i]) {
            fprintf(stderr,"FAIL: %s case %u counter %s differs from original callee log\n",d->name,n,ground_collide_counters[i].name);
            status=1;
        }
    }
    if(!limit) coverage_enable(0);
    for(unsigned i=0;i<COLLIDE_GLOBALS;++i) memcpy(mapped+collide_globals[i][0],initial[i],collide_globals[i][1]);
    for(unsigned i=0;i<COLLIDE_STUBS;++i) if(actor_patch(collide_stubs[i].rva,NULL,saved[i],FALSE)) return 1;
    if(status) return 1;
    printf("PASS: %-20s %u cases; exact EDX:EAX, arena, globals, callee log and counter deltas; hits=%llu part damage=%llu slide sounds=%llu obstacle tests=%llu\n",
        d->name,cases,(unsigned long long)contacts,(unsigned long long)damages,(unsigned long long)slides,(unsigned long long)obstacles);
    fflush(stdout); return 0;
}
