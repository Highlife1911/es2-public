/* Track/pose remainder: track_route 4793d4, track_poses 479ab0, pose_list
 * 478b00 (callees in core; managers and tracks from the
 * track_advance/track_target generators in the 12,288-byte advance arena, own
 * clip class with full-slot results traced as in track_callee_cases.h), and
 * track_seek 479344, turret_aim 403d5c (every game callee through the guest
 * bridge; direct cases with logging stubs that record arguments and entry
 * registers and change state between calls, then installed 31/a7 poison probes,
 * then real callees with the nested hooks in rotating modes). The adapters'
 * replacements are compared, so the descriptors' replacement_context and the
 * callee counters are exercised too. */
#define TQ_COVERAGE 16384u
#define TQ_IS_CORE(index) ((index)==ES2_HOOK_INDEX_track_route||(index)==ES2_HOOK_INDEX_track_poses||(index)==ES2_HOOK_INDEX_pose_list)

/* ---- track_route, track_poses, pose_list ---------------------------------- */
typedef struct tq_case { BYTE *track,*manager; unsigned clips; uint32_t a[ES2_HOOK_MAX_ARGS],r[3]; } tq_case;
static void tq_core_build(unsigned index,unsigned n,tq_case *c) {
    memset(c->a,0,sizeof(c->a)); for(unsigned i=0;i<3;++i) c->r[i]=random32();
    c->track=c->manager=NULL; c->clips=0;
    if(index==ES2_HOOK_INDEX_track_route) {
        random_bytes(advance_arena,sizeof(advance_arena)); advance_used=0;
        c->track=actor_build_track(&c->clips); c->a[0]=advance_va(c->track);
        /* Rate sign (direction) both ways: one in four exactly 0 or -1. */
        if(n%8==0) put16(c->track+0x4e,(uint16_t)(n%16?0:0xffff));
        return;
    }
    size_t masked[64]; unsigned count;
    c->manager=advance_build(index==ES2_HOOK_INDEX_track_poses?1+n%10:n,masked,&count);
    BYTE *m=c->manager; unsigned tracks=0; while(tracks<10 && get32(m+0x1a+4*tracks)) ++tracks;
    c->a[0]=advance_va(m);
    if(index==ES2_HOOK_INDEX_track_poses) {
        c->track=(BYTE *)(uintptr_t)get32(m+0x1a+4*(random32()%tracks));
        c->a[0]=advance_va(c->track); c->a[1]=get32(m+0x12); c->a[2]=get32(m+0x0e);
        /* A negative bone count runs no node (signed loop test). */
        if(n%32==1) {
            BYTE *t=c->track,*r=(BYTE *)(uintptr_t)get32(t);
            BYTE *clip=(BYTE *)(uintptr_t)get32((BYTE *)(uintptr_t)get32(r+6)+4*tc_signed(get16(t+4)));
            put16(clip+0x10,(uint16_t)(0x8000u|random32()));
        }
    }
}
static int tq_core_check(const char *what,unsigned n,const tq_case *c,uint64_t expected,uint64_t actual,
                         uint32_t expected_calls,uint32_t expected_hash) {
    if(expected==actual && expected_calls==tc_calls && expected_hash==tc_hash && !memcmp(advance_arena,advance_after,sizeof(advance_arena))) return 0;
    unsigned diff=0; while(diff<sizeof(advance_arena) && advance_arena[diff]==advance_after[diff]) ++diff;
    fprintf(stderr,"FAIL: %s case %u (%s) original=%016llx core=%016llx arena_offset=%u original_byte=%u core_byte=%u stub calls %u/%u hash %08x/%08x args=%08x,%08x,%08x\n",
        current_function,n,what,(unsigned long long)expected,(unsigned long long)actual,diff,
        diff<sizeof(advance_arena)?advance_after[diff]:0,diff<sizeof(advance_arena)?advance_arena[diff]:0,
        expected_calls,tc_calls,expected_hash,tc_hash,c->a[0],c->a[1],c->a[2]);
    return 1;
}
static int tq_core_direct(unsigned index,unsigned n,const tq_case *c) {
    const es2_hook_descriptor *d=function_hook_descriptor(index); const es2_hook_extension *x=function_hook_extension(index);
    memcpy(advance_before,advance_arena,sizeof(advance_arena));
    tc_reset(); uint64_t expected=actor_call(index,c->a,c->r);
    uint32_t calls=tc_calls,hash=tc_hash; tc_stub_total+=calls;
    memcpy(advance_after,advance_arena,sizeof(advance_arena));
    memcpy(advance_arena,advance_before,sizeof(advance_arena));
    tc_reset(); uint64_t actual=x->replacement_context(c->a,d->user,c->r);
    /* Installed in compare mode (adapter integration pass), the hooked call
     * runs the own class's methods once per phase; compare pair and arena. */
    if(rng_hooks[index].installed && rng_hooks[index].options.mode==ES2_HOOK_COMPARE) { calls=tc_calls; hash=tc_hash; }
    return tq_core_check("direct",n,c,expected,actual,calls,hash);
}
static int tq_core_probes(unsigned index,unsigned states) {
    const es2_hook_descriptor *d=function_hook_descriptor(index); BYTE saved_fpu[108]; es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi"); int status=0; unsigned total=0;
    for(unsigned fast=0;fast<2 && !status;++fast) for(unsigned poison=0;poison<2 && !status;++poison) for(unsigned n=0;n<states && !status;++n) {
        tq_case c; uint32_t pattern=poison?0xa7a7a7a7u:0x31313131u,pair[2]; BYTE expected_fpu[108],actual_fpu[108];
        tq_core_build(index,n,&c);
        memcpy(advance_before,advance_arena,sizeof(advance_arena));
        tc_reset(); sg_poison_call(mapped+d->rva,c.a,pattern,pair); es2_hook_fpu_save(expected_fpu);
        uint64_t expected=(uint64_t)pair[1]<<32|pair[0]; uint32_t calls=tc_calls,hash=tc_hash;
        memcpy(advance_after,advance_arena,sizeof(advance_arena));
        es2_hook h={0}; es2_hook_options o={0}; o.image=mapped; o.expected_base=0x400000; o.expected_image_size=0x2d8000; o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast); if(!weapons_install_hook(&h,d,&o,function_hook_extension(index))) return fail("track_pose poison install");
        memcpy(advance_arena,advance_before,sizeof(advance_arena));
        tc_reset(); sg_poison_call(mapped+d->rva,c.a,pattern,pair); es2_hook_fpu_save(actual_fpu);
        uint64_t actual=(uint64_t)pair[1]<<32|pair[0];
        if(tq_core_check(fast?"installed fast":"installed preserving",n,&c,expected,actual,calls,hash)) status=1;
        else if(memcmp(expected_fpu,actual_fpu,12) || memcmp(expected_fpu+28,actual_fpu+28,80) || h.calls!=1)
            status=fail("track_pose installed x87 state or call count");
        if(!es2_hook_uninstall(&h)) return fail("track_pose poison uninstall");
        ++total;
    }
    es2_hook_set_fast_path(TRUE); __asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status) printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/arena/stub trace\n",d->name,total);
    return status;
}
/* track_poses and pose_list: the hook under test original (modes 0-2) or
 * replaced (3-5) with its callee hooks in rotating modes. track_route: its
 * caller 47967c (track_target's original body) with track_route in
 * original/replaced/compare mode against an unhooked run. */
static int tq_core_real(unsigned index) {
    static const unsigned poses_nested[]={ES2_HOOK_INDEX_pose_interpolate};
    static const unsigned list_nested[]={ES2_HOOK_INDEX_track_count,ES2_HOOK_INDEX_track_poses,ES2_HOOK_INDEX_pose_interpolate};
    static const unsigned route_nested[]={ES2_HOOK_INDEX_track_route};
    int route=index==ES2_HOOK_INDEX_track_route;
    const unsigned *nested=route?route_nested:index==ES2_HOOK_INDEX_track_poses?poses_nested:list_nested;
    unsigned count=route?1:index==ES2_HOOK_INDEX_track_poses?1:3;
    const es2_hook_descriptor *d=function_hook_descriptor(index); int status=0; unsigned total=0; uint64_t entered=0;
    BYTE *target=route?mapped+0x7967c:mapped+d->rva;
    for(unsigned mode=0;mode<(route?3u:6u) && !status;++mode) for(unsigned n=0;n<512 && !status;++n) {
        tq_case c; uint32_t pattern=n&1?0xa7a7a7a7u:0x31313131u,pair[2];
        tq_core_build(index,n,&c);
        if(route) {
            /* track_target(T, clip, frame, flags): the generated target or a fresh one. */
            c.a[1]=random32(); c.a[2]=random32(); c.a[3]=random32();
            if(n%2) { c.a[1]=(c.a[1]&0xffff0000u)|get16(c.track+0x16); c.a[2]=(c.a[2]&0xffff0000u)|get16(c.track+0x18); }
            else { c.a[1]=(c.a[1]&0xffff0000u)|(random32()%6==0?0xffffu:advance_pick(c.clips)); c.a[2]=(c.a[2]&0xffff0000u)|(random32()%4==0?0xffffu:random32()%6); }
        }
        es2_hook hk[4]; memset(hk,0,sizeof(hk)); es2_hook_options o={0}; o.image=mapped; o.expected_base=0x400000; o.expected_image_size=0x2d8000;
        if(!route) for(unsigned j=0;j<count;++j) {
            o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);
            if(e && e->compare_unsupported && o.mode==ES2_HOOK_COMPARE) o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e)) return fail("track_pose nested install");
        }
        memcpy(advance_before,advance_arena,sizeof(advance_arena));
        tc_reset(); sg_poison_call(target,c.a,pattern,pair);
        uint64_t expected=(uint64_t)pair[1]<<32|pair[0]; uint32_t calls=tc_calls,hash=tc_hash;
        memcpy(advance_after,advance_arena,sizeof(advance_arena));
        if(route) { o.mode=(es2_hook_mode)mode; if(!weapons_install_hook(hk,d,&o,function_hook_extension(index))) return fail("track_route mode install"); }
        else { o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+count,d,&o,function_hook_extension(index))) return fail("track_pose caller install"); }
        memcpy(advance_arena,advance_before,sizeof(advance_arena));
        tc_reset(); sg_poison_call(target,c.a,pattern,pair);
        uint64_t actual=(uint64_t)pair[1]<<32|pair[0];
        /* A compared route runs the own class's methods once per phase. */
        if(route && mode==ES2_HOOK_COMPARE && hk[0].calls) { tc_calls=calls; tc_hash=hash; }
        if(tq_core_check(route?"real caller":mode<3?"real original":"real replaced",n,&c,expected,actual,calls,hash)) { fprintf(stderr,"real mode=%u\n",mode); status=1; }
        for(unsigned j=0;j<4;++j) {
            if(j<count) entered+=hk[j].calls;
            if(hk[j].mismatches) status=fail("track_pose nested compare mismatch");
            if(hk[j].descriptor && !es2_hook_uninstall(hk+j)) return fail("track_pose nested uninstall");
        }
        ++total;
    }
    if(!status && !entered) { fprintf(stderr,"FAIL: %s real callee hooks never entered\n",d->name); status=1; }
    if(!status) printf("PASS: %s %u real-%s cases, hooks in rotating modes (%llu nested entries), 31/a7 stack, full pair/arena/stub trace\n",
        d->name,total,route?"caller":"callee",(unsigned long long)entered);
    return status;
}
static int tq_core_cases(unsigned index,unsigned limit) {
    const es2_hook_descriptor *d=function_hook_descriptor(index);
    advance_other_vtable[8]=(uint32_t)(uintptr_t)tc_next;
    advance_other_vtable[9]=(uint32_t)(uintptr_t)tc_previous;
    advance_other_vtable[10]=advance_other_vtable[11]=(uint32_t)(uintptr_t)advance_other_reached;
    const char *quick=getenv("ES2_INVITRO_TRACK_POSE_CASES");
    unsigned cases=case_budget(limit,1001024u);if(!limit&&quick&&*quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0; tc_stub_total=0;
    if(!limit) coverage_enable(1);
    for(unsigned n=0;n<cases && !status;++n) {
        current_case=n; current_case_valid=1;
        if(!limit && n==TQ_COVERAGE) coverage_enable(0);
        tq_case c; tq_core_build(index,n,&c);
        if(advance_used>sizeof(advance_arena)) { status=fail("track_pose arena too small"); break; }
        status=tq_core_direct(index,n,&c);
    }
    if(!limit) coverage_enable(0);
    current_case_valid=0;
    if(!limit && !status) status=tq_core_probes(index,64);
    if(!limit && !status) status=tq_core_real(index);
    if(status) return 1;
    printf("PASS: %-20s %u cases; exact EDX:EAX, %u arena bytes, %llu ordered own frame-step calls\n",
        d->name,cases,(unsigned)sizeof(advance_arena),(unsigned long long)tc_stub_total);
    fflush(stdout); return 0;
}

/* ---- track_seek, turret_aim ------------------------------------------------ */
enum { TQ_SIZE=0x2000, TQ_MAX=16, TQ_IDS=5, TQ_TABLE=0x973e4, TQ_GLOBALS=16 };
typedef struct tq_record { uint32_t id,args[4],regs[3],eax,edx; } tq_record;
/* 4792ac (below track_seek); 47e0a0, 467adc, 479344, 402604 (below turret_aim). */
static const uint32_t tq_addresses[TQ_IDS]={0x4792ac,0x47e0a0,0x467adc,0x479344,0x402604};
static const unsigned tq_counts[TQ_IDS]={4,2,3,3,2};
static BYTE tq_arena[TQ_SIZE],tq_before[TQ_SIZE],tq_after[TQ_SIZE];
static size_t tq_used;
static tq_record tq_log[TQ_MAX];
static unsigned tq_calls;
static uint32_t tq_seed,tq_stream,tq_building;
__attribute__((used)) static uint32_t tq_ecx;
static uint32_t tq_next(void) {
    uint32_t z=tq_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *tq_host(uint32_t a) { return (BYTE *)(uintptr_t)a; }
static uint32_t tq_va(const BYTE *p) { return (uint32_t)(uintptr_t)p; }
static BYTE *tq_alloc(size_t size) { tq_used+=random32()%4; BYTE *p=tq_arena+tq_used; tq_used+=size; return p; }
static void tq_reset(void) { tq_calls=0; tq_stream=tq_seed; }
__attribute__((used,noinline)) static uint64_t __cdecl tq_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a) {
    if(tq_calls==TQ_MAX) { fail("track_pose synthetic call overflow"); ExitProcess(1); }
    tq_record *v=tq_log+tq_calls++; memset(v,0,sizeof(*v)); v->id=id; memcpy(v->args,a,4*tq_counts[id]); memcpy(v->regs,regs,12);
    uint32_t eax=tq_next(),edx=tq_next(),pick=tq_next(); tq_ecx=tq_next();
    BYTE *b=tq_host(tq_building),*table=mapped+TQ_TABLE;
    switch(id) {
    case 1: /* multiply_q8: small products; may change the table words read next. */
        if(pick%2) eax=(uint32_t)((int32_t)(tq_next()%1025u)-512);
        if(pick%5==0) put16(table+2*(pick>>8&7),(uint16_t)tq_next());
        break;
    case 2: /* approach_word writes the rate word; may change the angle and limits. */
        put16(tq_host(a[0]),(uint16_t)(pick%3?tq_next()%512u-256u:tq_next()));
        if(pick%7==0) put16(b+0x20d+2*(pick>>8&1),(uint16_t)tq_next());
        if(pick%5==0) put16(table+8+2*(pick>>12&3),(uint16_t)tq_next());
        break;
    case 3: /* track_seek: may change the other axis, its track pointer, the tables. */
        if(pick%4==0) put16(b+0x20d+2*(pick>>8&1),(uint16_t)tq_next());
        if(pick%6==0) put32(b+0x1f9+4*(pick>>12&1),tq_next());
        if(pick%5==0) put16(table+2*(pick>>16&7),(uint16_t)tq_next());
        break;
    }
    v->eax=eax; v->edx=edx; return (uint64_t)edx<<32|eax;
}
/* Pushes the entry EAX/EDX/ECX for the record before the slot pointer. */
#define TQ_STUB(n) __attribute__((naked)) static void tq_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _tq_stub_impl\n\taddl $24,%esp\n\tmovl _tq_ecx,%ecx\n\tret\n\t");}
TQ_STUB(0) TQ_STUB(1) TQ_STUB(2) TQ_STUB(3) TQ_STUB(4)
#undef TQ_STUB
static void *tq_stubs[TQ_IDS]={tq_stub_0,tq_stub_1,tq_stub_2,tq_stub_3,tq_stub_4};
static void tq_globals(BYTE *out,int restore) { if(restore) memcpy(mapped+TQ_TABLE,out,TQ_GLOBALS); else memcpy(out,mapped+TQ_TABLE,TQ_GLOBALS); }

/* A track over one clip (image ANSequence or ANCyclicSequence vtable) with
 * 0-6 frames and two 7fffh sentinel records, so every frame walk ends
 * (player_movement.md: the walk is unbounded in the original). [R+6] is
 * biased so that any signed clip word selects the one clip pointer. The
 * clip's poses and indices let the real track_clip/clip_pose run. */
static uint32_t tq_seek_track(uint32_t *slot,int fixed) {
    BYTE *t=tq_alloc(0x52),*r=tq_alloc(0x16),*list=tq_alloc(4),*clip=tq_alloc(0x1a);
    unsigned count=random32()%7,bones=random32()%3,poses=1+random32()%6,entries=(count+2)*(bones?bones:1);
    BYTE *records=tq_alloc(6*(count+2)),*indices=tq_alloc(2*entries),*pose=tq_alloc(12*poses);
    for(unsigned f=0;f<count;++f) { unsigned k=random32()%10; put16(records+6*f,(uint16_t)(k==0?0:k==1?random32()%0x2000:1+random32()%48)); }
    put16(records+6*count,0x7fff); put16(records+6*(count+1),0x7fff);
    for(unsigned e=0;e<entries;++e) put16(indices+2*e,(uint16_t)(random32()%poses));
    put32(clip,random32()%2?ADVANCE_SEQUENCE:ADVANCE_CYCLIC); put16(clip+8,(uint16_t)(random32()%4?1:0));
    put16(clip+0x0a,(uint16_t)count); put32(clip+0x0c,tq_va(records)); put16(clip+0x10,(uint16_t)bones); put32(clip+0x16,tq_va(indices));
    static const uint16_t words[]={0,1,0x7fff,0x8000,0xffff};
    uint16_t word=fixed>=0?(uint16_t)fixed:random32()%4?(uint16_t)random32():words[random32()%5];
    put32(r+6,tq_va(list)-4u*tc_signed(word)); put32(list,tq_va(clip)); put32(r+0x12,tq_va(pose)); put32(t,tq_va(r));
    *slot=(random32()&0xffff0000u)|word; return tq_va(t);
}
static void tq_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real) {
    tq_seed=random32(); random_bytes(tq_arena,TQ_SIZE); tq_used=0x40; memset(a,0,24);
    for(unsigned i=0;i<3;++i) r[i]=random32();
    if(index==ES2_HOOK_INDEX_track_seek) {
        uint32_t slot; a[0]=tq_seek_track(&slot,-1); a[1]=slot;
        static const uint16_t edges[]={0,0x3fff,0x1770,0x2000,1,0x3ffe};
        unsigned kind=random32()%8; uint16_t f;
        if(kind<4) f=(uint16_t)(random32()%0x4000); else if(kind==4) f=edges[random32()%6];
        else if(kind==5) f=(uint16_t)(0x8000u|random32()); else f=(uint16_t)random32();
        if(real) f=(uint16_t)(random32()%0x4000);
        a[2]=(random32()&0xffff0000u)|f;
        return;
    }
    BYTE *b=tq_alloc(0x220),*p=tq_alloc(8),*table=mapped+TQ_TABLE;
    tq_building=tq_va(b);
    static const uint16_t words[]={0,1,7,8,0xfff8,0xfff9,0x7ff,0x800,0x801,0xf7ff,0xf800,0xf801,0x7fff,0x8000,0xffff,0x808};
    put16(p,n<256?words[n%16]:random32()%2?(uint16_t)random32():(uint16_t)(random32()%8193u-4096u));
    put16(p+4,n<256?words[n/16%16]:random32()%2?(uint16_t)random32():(uint16_t)(random32()%8193u-4096u));
    for(unsigned i=0;i<2;++i) {
        /* k, step, lo, hi: usually ordered limits, sometimes anything. */
        put16(table+2*i,(uint16_t)(random32()%3?random32()%1025u:random32()));
        put16(table+4+2*i,(uint16_t)(random32()%3?1+random32()%64:random32()));
        uint16_t lo=(uint16_t)random32(),hi=(uint16_t)random32();
        if(random32()%4 && (int16_t)lo>(int16_t)hi) { uint16_t x=lo; lo=hi; hi=x; }
        put16(table+8+2*i,lo); put16(table+12+2*i,hi);
        if(random32()%3==0) put16(b+0x20d+2*i,(uint16_t)(random32()%2?lo:hi)+(uint16_t)(random32()%5-2));
        if(random32()%2) put16(b+0x209+2*i,(uint16_t)(random32()%513u-256u));
    }
    if(real) {
        /* 403d5c passes the axis index as the clip word. */
        uint32_t slot; put32(b+0x1f9,tq_seek_track(&slot,0)); put32(b+0x1fd,tq_seek_track(&slot,1)); b[0x39]=0;
    }
    a[0]=tq_va(b); a[1]=tq_va(p);
}
static int tq_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const tq_record *expected) {
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,tq_calls);
    for(unsigned j=0;j<calls&&j<tq_calls&&expected;++j) if(memcmp(expected+j,tq_log+j,sizeof(*expected))) {
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(tq_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k) if(e[k]!=f[k]) fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<TQ_SIZE;++j) if(tq_after[j]!=tq_arena[j]) { fprintf(stderr," arena +%x %02x/%02x\n",j,tq_after[j],tq_arena[j]); break; }
    return 1;
}
static unsigned tq_expected_hits(const callee_counter *c) {
    unsigned hits=0; for(unsigned j=0;j<tq_calls;++j) if(tq_addresses[tq_log[j].id]==c->key) ++hits;
    return hits;
}
static int tq_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r) {
    const es2_hook_descriptor *d=function_hook_descriptor(index); const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[TQ_GLOBALS],after[TQ_GLOBALS],actual[TQ_GLOBALS]; tq_record expected[TQ_MAX];
    memcpy(tq_before,tq_arena,TQ_SIZE); tq_globals(before,0);
    tq_reset(); uint64_t pair[2]; pair[0]=weapons_original(index,a,r); unsigned calls=tq_calls; memcpy(expected,tq_log,calls*sizeof(*expected));
    memcpy(tq_after,tq_arena,TQ_SIZE); tq_globals(after,0);
    memcpy(tq_arena,tq_before,TQ_SIZE); tq_globals(before,1); tq_reset();
    unsigned count; callee_counter *c=weapons_graph_counters(index,&count); uint64_t counts[4];
    for(unsigned i=0;i<count;++i) counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r); tq_globals(actual,0);
    for(unsigned i=0;i<count;++i) if(c[i].count-counts[i]!=tq_expected_hits(c+i)) return fail("track_pose callee counter delta");
    if(pair[0]!=pair[1]||calls!=tq_calls||memcmp(expected,tq_log,calls*sizeof(*expected))||memcmp(tq_after,tq_arena,TQ_SIZE)||memcmp(after,actual,TQ_GLOBALS)) {
        for(unsigned j=0;j<TQ_GLOBALS;++j) if(after[j]!=actual[j]) fprintf(stderr," table byte %u %02x/%02x\n",j,after[j],actual[j]);
        return tq_report("direct",n,pair,calls,expected);
    }
    return 0;
}
static int tq_patch(BYTE saved[TQ_IDS][5],int active[TQ_IDS],unsigned which,int on) {
    for(unsigned i=0;i<TQ_IDS;++i) {
        if(on) { active[i]=0; if(!(which>>i&1)) continue; if(actor_patch(tq_addresses[i]-0x400000u,tq_stubs[i],saved[i],TRUE)) return 1; active[i]=1; }
        else if(active[i] && actor_patch(tq_addresses[i]-0x400000u,NULL,saved[i],FALSE)) return 1;
    }
    return 0;
}
static int tq_probes(unsigned index,unsigned states) {
    const es2_hook_descriptor *d=function_hook_descriptor(index); BYTE saved_fpu[108]; es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi"); int status=0; unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast) for(unsigned poison=0;poison<2&&!status;++poison) for(unsigned n=0;n<states&&!status;++n) {
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u; tq_build(index,n,a,r,0);
        BYTE before[TQ_GLOBALS],after[TQ_GLOBALS],actual[TQ_GLOBALS],expected_fpu[108],actual_fpu[108]; tq_record expected[TQ_MAX];
        memcpy(tq_before,tq_arena,TQ_SIZE); tq_globals(before,0);
        tq_reset(); sg_poison_call(mapped+d->rva,a,pattern,expected_pair); es2_hook_fpu_save(expected_fpu);
        unsigned calls=tq_calls; memcpy(expected,tq_log,calls*sizeof(*expected)); memcpy(tq_after,tq_arena,TQ_SIZE); tq_globals(after,0);
        es2_hook h={0}; es2_hook_options o={0}; o.image=mapped; o.expected_base=0x400000; o.expected_image_size=0x2d8000; o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast); if(!weapons_install_hook(&h,d,&o,function_hook_extension(index))) return fail("track_pose poison install");
        memcpy(tq_arena,tq_before,TQ_SIZE); tq_globals(before,1); tq_reset(); sg_poison_call(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu); tq_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=tq_calls||memcmp(expected,tq_log,calls*sizeof(*expected))||memcmp(tq_after,tq_arena,TQ_SIZE)||
           memcmp(after,actual,TQ_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=tq_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h)) return fail("track_pose poison uninstall"); ++total;
    }
    es2_hook_set_fast_path(TRUE); __asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status) printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state\n",d->name,total);
    return status;
}
/* Real callees: track_clip and clip_pose below track_seek; multiply_q8,
 * approach_word, track_seek, actor_animate (byte B+39 clear, so no
 * animation step), track_clip and clip_pose below turret_aim. The hook
 * under test runs original (modes 0-2) or replaced (3-5). */
static int tq_real(unsigned index) {
    static const unsigned seek_nested[]={ES2_HOOK_INDEX_track_clip,ES2_HOOK_INDEX_clip_pose};
    static const unsigned aim_nested[]={ES2_HOOK_INDEX_multiply_q8,ES2_HOOK_INDEX_approach_word,ES2_HOOK_INDEX_track_seek,
        ES2_HOOK_INDEX_actor_animate,ES2_HOOK_INDEX_track_clip,ES2_HOOK_INDEX_clip_pose};
    int seek=index==ES2_HOOK_INDEX_track_seek;
    const unsigned *nested=seek?seek_nested:aim_nested; unsigned count=seek?2:6;
    const es2_hook_descriptor *d=function_hook_descriptor(index); int status=0; unsigned total=0; uint64_t entered[6]={0};
    for(unsigned mode=0;mode<6&&!status;++mode) for(unsigned n=0;n<512&&!status;++n) {
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u; tq_build(index,n,a,r,1);
        BYTE before[TQ_GLOBALS],after[TQ_GLOBALS],actual[TQ_GLOBALS];
        memcpy(tq_before,tq_arena,TQ_SIZE); tq_globals(before,0);
        es2_hook hk[7]; memset(hk,0,sizeof(hk)); es2_hook_options o={0}; o.image=mapped; o.expected_base=0x400000; o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j) { o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]); if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE) o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e)) return fail("track_pose nested install"); }
        sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        memcpy(tq_after,tq_arena,TQ_SIZE); tq_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+count,d,&o,function_hook_extension(index))) return fail("track_pose caller install");
        memcpy(tq_arena,tq_before,TQ_SIZE); tq_globals(before,1);
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair); tq_globals(actual,0);
        if(memcmp(expected_pair,actual_pair,8)||memcmp(tq_after,tq_arena,TQ_SIZE)||memcmp(after,actual,TQ_GLOBALS)) {
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            fprintf(stderr,"real mode=%u\n",mode); tq_calls=0; status=tq_report("real",n,pair,0,NULL);
        }
        for(unsigned j=0;j<=count;++j) { if(j<count) entered[j]+=hk[j].calls; if(hk[j].mismatches) status=fail("track_pose nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j)) return fail("track_pose nested uninstall"); }
        tq_globals(before,1); ++total;
    }
    for(unsigned j=0;j<count&&!status;++j) if(!entered[j]) { fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name); status=1; }
    if(!status) printf("PASS: %s %u real-callee cases, %u nested hooks in rotating modes, 31/a7 stack, full pair, arena and tables\n",d->name,total,count);
    return status;
}
static int tq_graph_cases(unsigned index,unsigned limit) {
    unsigned which=index==ES2_HOOK_INDEX_track_seek?0x1u:0x1eu;
    BYTE saved[TQ_IDS][5],old_globals[TQ_GLOBALS]; int active[TQ_IDS];
    tq_globals(old_globals,0); if(tq_patch(saved,active,which,1)) return 1;
    const char *quick=getenv("ES2_INVITRO_TRACK_POSE_CASES");
    unsigned cases=case_budget(limit,1001024u);if(!limit&&quick&&*quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0; if(!limit) coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n) { current_case=n; current_case_valid=1; if(!limit&&n==TQ_COVERAGE) coverage_enable(0);
        uint32_t a[6],r[3]; tq_build(index,n,a,r,0); status=tq_compare(index,n,a,r); tq_globals(old_globals,1); }
    if(!limit) coverage_enable(0);
    if(!limit&&!status) status=tq_probes(index,64);
    if(tq_patch(saved,active,which,0)) return 1;
    current_case_valid=0;
    if(!limit&&!status) status=tq_real(index);
    tq_globals(old_globals,1);
    if(!status) printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, arena/tables, callee counters\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
static int track_pose_cases(unsigned index,unsigned limit) {
    current_function=function_hook_descriptor(index)->name;
    return TQ_IS_CORE(index)?tq_core_cases(index,limit):tq_graph_cases(index,limit);
}
