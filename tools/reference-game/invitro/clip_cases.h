/* 4792ac: signed indices are biased into small synthetic arrays, including
 * -32768/-1/32767. Both callees record full slots/results and track state
 * at entry. V20 may change the resource and indices before duration lookup. */
typedef struct clip_record { uint32_t id,args[3];BYTE track[0x52];uint64_t result; } clip_record;
static clip_record clip_log[2];
static unsigned clip_logged;
static uint32_t clip_track,clip_resource,clip_list,clip_object,clip_frames;
static uint16_t clip_change_clip,clip_change_frame;
static int clip_mutate;
static uint64_t clip_next_result,clip_pose_result;
static uint32_t clip_signed(uint32_t v) { return (v&0x8000u)?v|0xffff0000u:v&0xffffu; }
static clip_record *clip_record_call(uint32_t id,uint32_t a,uint32_t b,uint32_t c,uint64_t result) {
    if(clip_logged==2) {fail("track_clip extra call");exit(1);}
    clip_record *r=&clip_log[clip_logged++];r->id=id;r->args[0]=a;r->args[1]=b;r->args[2]=c;r->result=result;
    memcpy(r->track,(void *)(uintptr_t)clip_track,sizeof(r->track));return r;
}
static uint64_t __cdecl clip_next_stub(uint32_t object,uint32_t frame) {
    (void)clip_record_call(1,object,frame,0,clip_next_result);
    if(clip_mutate) {
        BYTE *t=(BYTE *)(uintptr_t)clip_track;
        put32(t,clip_resource);put16(t+4,clip_change_clip);put16(t+6,clip_change_frame);
        put32((BYTE *)(uintptr_t)(clip_resource+6),clip_list-4u*clip_signed(clip_change_clip));
        put32((BYTE *)(uintptr_t)(clip_object+0xc),clip_frames-6u*clip_signed(clip_change_frame));
    }
    return clip_next_result;
}
static uint64_t __cdecl clip_pose_stub(uint32_t track,uint32_t clip,uint32_t frame) {
    (void)clip_record_call(2,track,clip,frame,clip_pose_result);
    put32((BYTE *)(uintptr_t)track+0x22,clip^frame^0xbadcafeu);
    return clip_pose_result;
}
static int clip_cases(unsigned index,unsigned limit) {
    const es2_hook_descriptor *d=function_hook_descriptor(index);current_function=d->name;
    unsigned cases=case_budget(limit,1000025);BYTE saved[5];int status=0;
    if(actor_patch(0x78ef4,(void *)clip_pose_stub,saved,TRUE))return 1;
    for(unsigned n=0;n<cases && !status;++n) {
        current_case=n;current_case_valid=1;
        random_bytes(advance_arena,sizeof(advance_arena));advance_used=16;
        BYTE *t=advance_alloc(0x60),*r=advance_alloc(0x24),*r2=advance_alloc(0x24);
        BYTE *list=advance_alloc(4),*object=advance_alloc(0x20),*v=advance_alloc(0x30),*frames=advance_alloc(6);
        uint32_t a[6]={advance_va(t),random32(),random32(),random32()},regs[3]={random32(),random32(),random32()};
        static const uint16_t edges[]={0,1,0x7fff,0x8000,0xffff};
        if(n<25) {a[1]=(a[1]&0xffff0000u)|edges[n%5];a[2]=(a[2]&0xffff0000u)|edges[n/5];}
        clip_track=a[0];clip_resource=advance_va(r2);clip_list=advance_va(list);clip_object=advance_va(object);clip_frames=advance_va(frames);
        clip_change_clip=(uint16_t)random32();clip_change_frame=(uint16_t)random32();clip_mutate=(int)(n&1);
        clip_next_result=((uint64_t)random32()<<32)|random32();clip_pose_result=((uint64_t)random32()<<32)|random32();
        put32(t,advance_va(r));put32(r+6,advance_va(list)-4u*clip_signed(a[1]&0xffffu));
        put32(list,advance_va(object));put32(object,advance_va(v));put32(v+0x20,(uint32_t)(uintptr_t)clip_next_stub);
        put32(object+0xc,advance_va(frames)-6u*clip_signed(a[2]&0xffffu));
        memcpy(advance_before,advance_arena,sizeof(advance_arena));memset(clip_log,0,sizeof(clip_log));clip_logged=0;
        /* Exercise ground_update's bridge in either installed hook mode. */
        if(!(n&1))regs[0]=a[0];
        uint64_t got=(n&1)?actor_call(index,a,regs):math_clip_game(mapped,a[0],a[1],a[2],a[3]);
        clip_record expected[2];memcpy(expected,clip_log,sizeof(expected));
        unsigned count=clip_logged;memcpy(advance_after,advance_arena,sizeof(advance_arena));
        memcpy(advance_arena,advance_before,sizeof(advance_arena));memset(clip_log,0,sizeof(clip_log));clip_logged=0;
        uint64_t want=es2_track_clip_registers(&harness_memory,a[0],a[1],a[2],a[3],regs[0],(uint32_t)(uintptr_t)mapped+0x78ef4u,math_guest_call,NULL);
        if(got!=want || count!=2 || clip_logged!=2 || memcmp(expected,clip_log,sizeof(expected)) || memcmp(advance_arena,advance_after,sizeof(advance_arena))) {
            fprintf(stderr,"FAIL: track_clip case %u original=%016llx core=%016llx ordered calls=%u/%u\n",n,(unsigned long long)got,(unsigned long long)want,count,clip_logged);status=1;
        }
    }
    current_case_valid=0;if(actor_patch(0x78ef4,NULL,saved,FALSE))return 1;
    if(status)return 1;
    printf("PASS: %-20s %u cases; exact EDX:EAX, arena, ordered callee arguments/results and entry states; branchless\n",d->name,cases);fflush(stdout);return 0;
}
