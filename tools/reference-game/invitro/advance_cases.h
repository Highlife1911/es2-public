/* 0x478d38 track advance. Each case builds a synthetic manager: node
 * hierarchy, flags, poses and matrices, one or two ANAnimList-like resources
 * with clips and up to ten tracks, all inside one arena of random bytes.
 * Clips use the image's own ANSequence (0x4a684c) and ANCyclicSequence
 * (0x4a6810) vtables, so the original frame-step methods run in the private
 * mapping; one clip in ten uses an own vtable to exercise core's fallback to
 * the original virtual call. Indices stay inside their arrays and durations
 * are nonzero (both fault in the original). The original runs on a poisoned
 * stack; byte 19 of every written matrix (T+0x41, node matrices) can hold
 * stack residue (padding contract, track.h) and is compared separately. */
#define ADVANCE_SEQUENCE 0x4a684cu
#define ADVANCE_CYCLIC 0x4a6810u
#define ADVANCE_COVERAGE_CASES 8192u
static uint32_t __cdecl advance_other_next(uint32_t clip,uint32_t frame) {
    uint16_t n=get16((const BYTE *)(uintptr_t)clip+0x0a);
    return 0xabcd0000u|(uint16_t)(((uint16_t)frame+2u)%n);
}
static uint32_t __cdecl advance_other_previous(uint32_t clip,uint32_t frame) {
    uint16_t n=get16((const BYTE *)(uintptr_t)clip+0x0a);
    return 0x12340000u|(uint16_t)(((uint16_t)frame+n-1u)%n);
}
/* Own reached(frame, target, time) for slots 0x28/0x2c with explicit EDX. */
static uint64_t __cdecl advance_other_reached(uint32_t clip,uint32_t a,uint32_t b,uint32_t c) {
    (void)clip; return ((uint64_t)(a^b)<<32)|((a+b+c)>>3&1u);
}
static uint32_t advance_other_vtable[16];
static uint64_t advance_other_calls;
static uint64_t advance_other(void *c,uint32_t clip,uint32_t slot,const uint32_t *args) {
    (void)c; ++advance_other_calls;
    uint32_t vtable=get32((const BYTE *)(uintptr_t)clip),method=get32((const BYTE *)(uintptr_t)(vtable+slot));
    if(slot<0x28u) return ((uint64_t (__cdecl *)(uint32_t,uint32_t))(uintptr_t)method)(clip,args[0]);
    return ((uint64_t (__cdecl *)(uint32_t,uint32_t,uint32_t,uint32_t))(uintptr_t)method)(clip,args[0],args[1],args[2]);
}

static BYTE advance_arena[12288],advance_before[12288],advance_after[12288];
static size_t advance_used;
static BYTE *advance_alloc(size_t size) {
    advance_used+=random32()%4;
    BYTE *p=advance_arena+advance_used; advance_used+=size;
    return p;
}
static uint32_t advance_va(const BYTE *p) { return (uint32_t)(uintptr_t)p; }
static uint16_t advance_pick(unsigned n) { return (uint16_t)(random32()%n); }

typedef struct advance_resource { BYTE *object; unsigned clips,poses,bones; uint16_t frames[4]; } advance_resource;

static void advance_build_resource(advance_resource *r,unsigned nodes) {
    r->clips=1+random32()%4; r->poses=1+random32()%8; r->bones=random32()%5;
    r->object=advance_alloc(0x16);
    BYTE *clips=advance_alloc(4*r->clips),*poses=advance_alloc(12*r->poses);
    put32(r->object+6,advance_va(clips)); put32(r->object+0x12,advance_va(poses));
    BYTE *map=advance_alloc(2*r->bones+2);
    for(unsigned j=0;j<r->bones;++j) put16(map+2*j,advance_pick(nodes));
    for(unsigned i=0;i<r->clips;++i) {
        BYTE *clip=advance_alloc(0x1a);
        unsigned kind=random32()%20,frames=1+random32()%5;
        put32(clip,kind<9?ADVANCE_SEQUENCE:kind<18?ADVANCE_CYCLIC:advance_va((const BYTE *)advance_other_vtable));
        put16(clip+6,(uint16_t)(random32()%5-2));
        if(random32()%8==0) put16(clip+8,0); else if(!get16(clip+8)) put16(clip+8,1);
        put16(clip+0x0a,(uint16_t)frames); r->frames[i]=(uint16_t)frames;
        BYTE *durations=advance_alloc(6*frames);
        for(unsigned f=0;f<frames;++f) put16(durations+6*f,(uint16_t)(1+random32()%48));
        put32(clip+0x0c,advance_va(durations));
        put16(clip+0x10,(uint16_t)r->bones); put32(clip+0x12,advance_va(map));
        unsigned entries=frames*(r->bones?r->bones:1);
        BYTE *indices=advance_alloc(2*entries);
        for(unsigned e=0;e<entries;++e) put16(indices+2*e,advance_pick(r->poses));
        put32(clip+0x16,advance_va(indices));
        put32(clips+4*i,advance_va(clip));
    }
}

static void advance_build_track(BYTE *t,const advance_resource *r) {
    put32(t,advance_va(r->object));
    uint16_t clip=advance_pick(r->clips),next=advance_pick(r->clips),target=advance_pick(r->clips),stop=advance_pick(r->clips+1);
    put16(t+4,clip); put16(t+6,advance_pick(r->frames[clip]));
    put16(t+8,next); put16(t+0x0a,advance_pick(r->frames[next]));
    put16(t+0x0c,random32()%2?0xffffu:advance_pick(r->frames[clip]));
    put16(t+0x0e,target); put16(t+0x10,advance_pick(r->frames[target]));
    put16(t+0x12,(uint16_t)(1+random32()%48)); put16(t+0x14,advance_pick(r->poses));
    put16(t+0x16,stop); put16(t+0x18,random32()%3==0||stop>=r->clips?0xffffu:advance_pick(r->frames[stop]));
    unsigned kind=random32()%20;
    int32_t duration=1+(int32_t)(random32()%48);
    if(kind==0) duration=-duration;
    put16(t+0x1e,(uint16_t)duration);
    int32_t magnitude=duration<0?-duration:duration;
    put16(t+0x1c,(uint16_t)(kind<2?(int32_t)(random32()%129)-64:(int32_t)(random32()%(uint32_t)magnitude)));
    put16(t+0x4e,(uint16_t)(random32()%8==0?0:(int32_t)(random32()%513)-256));
}

/* Returns the manager; masked lists the residue byte offsets. */
static BYTE *advance_build(unsigned n,size_t masked[64],unsigned *mask_count) {
    random_bytes(advance_arena,sizeof(advance_arena)); advance_used=0; *mask_count=0;
    unsigned nodes=1+random32()%12,tracks=n%11,edges=random32()%(nodes+3);
    BYTE *m=advance_alloc(0x42),*shape=advance_alloc(0x2e),*hierarchy=advance_alloc(0x10),*entries=advance_alloc(4*edges+4);
    put32(m+4,advance_va(shape)); put32(shape+0x2a,advance_va(hierarchy));
    put16(hierarchy+0x0a,(uint16_t)edges); put32(hierarchy+0x0c,advance_va(entries));
    for(unsigned e=0;e<edges;++e) { put16(entries+4*e,random32()%4==0?0xffffu:advance_pick(nodes)); put16(entries+4*e+2,advance_pick(nodes)); }
    BYTE *flags=advance_alloc(nodes),*poses=advance_alloc(12*nodes),*matrices=advance_alloc(32*nodes);
    put16(m+0x0c,(uint16_t)nodes); put32(m+0x0e,advance_va(flags)); put32(m+0x12,advance_va(poses)); put32(m+0x16,advance_va(matrices));
    for(unsigned i=0;i<nodes;++i) masked[(*mask_count)++]=(size_t)(matrices-advance_arena)+32*i+19;
    advance_resource resources[2]; unsigned count=1+random32()%2;
    for(unsigned i=0;i<count;++i) advance_build_resource(&resources[i],nodes);
    for(unsigned i=0;i<tracks;++i) {
        BYTE *t=advance_alloc(0x52);
        advance_build_track(t,&resources[random32()%count]);
        put32(m+0x1a+4*i,advance_va(t));
        masked[(*mask_count)++]=(size_t)(t-advance_arena)+0x41;
    }
    if(tracks<10) put32(m+0x1a+4*tracks,0);
    return m;
}

static int advance_cases(unsigned index,unsigned limit) {
    const es2_hook_descriptor *d=function_hook_descriptor(index); current_function=d->name;
    const char *override=getenv("ES2_INVITRO_ADVANCE_CASES");
    unsigned cases=case_budget(limit,1000000);if(!limit&&override&&*override)cases=(unsigned)strtoul(override,NULL,10);
    const es2_clip_classes classes={ADVANCE_SEQUENCE,ADVANCE_CYCLIC,advance_other,NULL};
    advance_other_vtable[8]=(uint32_t)(uintptr_t)advance_other_next;
    advance_other_vtable[9]=(uint32_t)(uintptr_t)advance_other_previous;
    advance_other_vtable[10]=advance_other_vtable[11]=(uint32_t)(uintptr_t)advance_other_reached;
    uint64_t residue=0,tracks=0,other=0;
    size_t masked[64]; unsigned mask_count;
    if(!limit) coverage_enable(1);
    int status=0;
    for(unsigned n=0;n<cases && !status;++n) {
        /* Each instrumented case raises a breakpoint per executed branch. */
        if(!limit && n==ADVANCE_COVERAGE_CASES) coverage_enable(0);
        BYTE *m=advance_build(n,masked,&mask_count);
        if(advance_used>sizeof(advance_arena)) { status=fail("advance arena too small"); break; }
        tracks+=n%11;
        uint32_t a[ES2_HOOK_MAX_ARGS]={0}; a[0]=advance_va(m); a[1]=random32();
        /* Mostly a few frames per track; one case in 256 takes the full word. */
        unsigned size=random32()%256;
        if(size<176) a[1]=(a[1]&0xffff0000u)|(uint16_t)((int32_t)(random32()%129)-64);
        else if(size<255) a[1]=(a[1]&0xffff0000u)|(uint16_t)((int32_t)(random32()%1025)-512);
        memcpy(advance_before,advance_arena,sizeof(advance_arena));
        uint32_t got[2]; uint8_t padding;
        if(!limit) {
            padding=(n&1)?0x31:0xa7;
            es2_probe_call(mapped+d->rva,a[0],a[1],0,padding*0x01010101u,got);
        } else {
            got[0]=original(index,a); got[1]=original_edx; padding=0;
        }
        memcpy(advance_after,advance_arena,sizeof(advance_arena));
        memcpy(advance_arena,advance_before,sizeof(advance_arena));
        uint64_t calls=advance_other_calls;
        uint64_t want=es2_track_advance_registers(&harness_memory,a[0],a[1],&classes,MATH_QUARTER(d->user),padding);
        other+=advance_other_calls-calls;
        for(unsigned i=0;i<mask_count;++i) {
            if(advance_arena[masked[i]]!=advance_after[masked[i]]) ++residue;
            advance_arena[masked[i]]=advance_after[masked[i]];
        }
        if(got[0]!=(uint32_t)want || got[1]!=(uint32_t)(want>>32) || memcmp(advance_arena,advance_after,sizeof(advance_arena))) {
            unsigned diff=0; while(diff<sizeof(advance_arena) && advance_arena[diff]==advance_after[diff]) ++diff;
            fprintf(stderr,"FAIL: %s case %u EAX original=%08x core=%08x EDX original=%08x core=%08x arena_offset=%u (manager at %u) original_byte=%u core_byte=%u step=%08x\n",
                d->name,n,got[0],(uint32_t)want,got[1],(uint32_t)(want>>32),diff,(unsigned)(m-advance_arena),
                diff<sizeof(advance_arena)?advance_after[diff]:0,diff<sizeof(advance_arena)?advance_arena[diff]:0,a[1]);
            status=1;
        }
    }
    if(!limit) coverage_enable(0);
    if(status) return 1;
    printf("PASS: %-20s %u cases; exact EDX:EAX and %u arena bytes except byte 19 of written matrices%s; tracks=%llu fallback virtual calls=%llu residue bytes differing=%llu\n",
        d->name,cases,(unsigned)sizeof(advance_arena),limit?"":" (stack poison 31/a7)",(unsigned long long)tracks,(unsigned long long)other,(unsigned long long)residue);
    fflush(stdout); return 0;
}
