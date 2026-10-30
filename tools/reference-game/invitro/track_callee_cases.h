/* track_advance's subtree as standalone hooks: track_sort 4789d0,
 * track_step 479720, track_list 478aac, node_matrices 478c64. Managers come
 * from advance_build (advance_cases.h): node hierarchy, flags, poses and
 * matrices, resources whose clips use the image's ANSequence and
 * ANCyclicSequence vtables or (one in ten) an own vtable. The own frame
 * steps here return EDX:EAX that depend on the full argument slot, so the
 * high word of every virtual argument is checked; their calls are counted
 * and hashed in order. Byte 19 of matrices written by the padding wrappers
 * (T+0x41, node matrices) is stack residue in the original (track.h) and is
 * compared separately. track_step also gets forward steps through zero
 * frame durations (0x479906), which a whole advance never survives. */
#define TC_SORT_COVERAGE 16384u
#define TC_STEP_COVERAGE 32768u
static uint32_t tc_calls,tc_hash;
static void tc_note(uint32_t method,uint32_t clip,uint32_t frame) {
    ++tc_calls;
    const uint32_t words[3]={method,clip,frame};
    for(unsigned i=0;i<3;++i) tc_hash=(tc_hash^words[i])*0x01000193u;
}
static uint64_t __cdecl tc_next(uint32_t clip,uint32_t frame) {
    uint16_t n=get16((const BYTE *)(uintptr_t)clip+0x0a); tc_note(0x20,clip,frame);
    uint32_t low=(uint16_t)(((uint16_t)frame+2u)%n);
    return ((uint64_t)(frame*0x9e3779b9u^clip)<<32)|((frame^0x5a5a0000u)&0xffff0000u)|low;
}
static uint64_t __cdecl tc_previous(uint32_t clip,uint32_t frame) {
    uint16_t n=get16((const BYTE *)(uintptr_t)clip+0x0a); tc_note(0x24,clip,frame);
    return ((uint64_t)(frame+0x12345u)<<32)|(~frame&0xffff0000u)|(uint16_t)(((uint16_t)frame+n-1u)%n);
}
static void tc_reset(void) { tc_calls=0;tc_hash=0x811c9dc5u; }
static uint32_t tc_signed(uint16_t v) { return (v&0x8000u)?v|0xffff0000u:v; }

typedef struct tc_case {
    BYTE *manager,*track;
    uint32_t a[ES2_HOOK_MAX_ARGS],r[3];
    size_t masked[64]; unsigned mask_count;
} tc_case;

/* Zero some frame durations of the clips a forward step can reach (T+8 and
 * the transition target T+0x0e), only when the step goes forward from a
 * nonnegative time: backward through a zero duration the original never
 * ends. */
static void tc_zero_durations(BYTE *t,uint32_t step) {
    int32_t product=(int32_t)tc_signed((uint16_t)step)*(int32_t)tc_signed(get16(t+0x4e));
    uint16_t delta=(uint16_t)((uint32_t)product>>8|((product<0)?0xff000000u:0u));
    uint16_t time=(uint16_t)(get16(t+0x1c)+delta);
    if(!delta || (time&0x8000u)) return;
    const BYTE *clips=(const BYTE *)(uintptr_t)get32((const BYTE *)(uintptr_t)get32(t)+6);
    static const unsigned fields[2]={8,0x0e};
    for(unsigned f=0;f<2;++f) {
        const BYTE *clip=(const BYTE *)(uintptr_t)get32(clips+4*tc_signed(get16(t+fields[f])));
        BYTE *durations=(BYTE *)(uintptr_t)get32(clip+0x0c);
        for(unsigned i=0;i<get16(clip+0x0a);++i) if(random32()%3==0) put16(durations+6*i,0);
    }
}

static uint32_t tc_step_slot(void) {
    uint32_t slot=random32(); unsigned size=random32()%256;
    if(size<176) slot=(slot&0xffff0000u)|(uint16_t)((int32_t)(random32()%129)-64);
    else if(size<255) slot=(slot&0xffff0000u)|(uint16_t)((int32_t)(random32()%1025)-512);
    return slot;
}

static void tc_build(unsigned index,unsigned n,tc_case *c) {
    memset(c->a,0,sizeof(c->a)); for(unsigned i=0;i<3;++i) c->r[i]=random32();
    size_t all[64]; unsigned count;
    c->manager=advance_build(index==ES2_HOOK_INDEX_track_step?1+n%10:n,all,&count);
    BYTE *m=c->manager; unsigned tracks=0; while(tracks<10 && get32(m+0x1a+4*tracks)) ++tracks;
    unsigned nodes=get16(m+0x0c); BYTE *matrices=(BYTE *)(uintptr_t)get32(m+0x16);
    c->a[0]=advance_va(m); c->mask_count=0; c->track=NULL;
    if(index==ES2_HOOK_INDEX_track_step) {
        c->track=(BYTE *)(uintptr_t)get32(m+0x1a+4*(random32()%tracks));
        c->a[0]=advance_va(c->track); c->a[1]=tc_step_slot();
        if(random32()%4==0) tc_zero_durations(c->track,c->a[1]);
        c->masked[c->mask_count++]=(size_t)(c->track-advance_arena)+0x41;
    } else if(index==ES2_HOOK_INDEX_track_list) {
        c->a[1]=tc_step_slot();
        for(unsigned i=0;i<tracks;++i) c->masked[c->mask_count++]=(size_t)((BYTE *)(uintptr_t)get32(m+0x1a+4*i)-advance_arena)+0x41;
        c->masked[c->mask_count++]=(size_t)(matrices-advance_arena)+19;
    } else if(index==ES2_HOOK_INDEX_node_matrices) {
        for(unsigned i=0;i<nodes;++i) c->masked[c->mask_count++]=(size_t)(matrices-advance_arena)+32*i+19;
    }
}

static uint64_t tc_core(unsigned index,const tc_case *c,uint8_t padding) {
    static const es2_clip_classes classes={ADVANCE_SEQUENCE,ADVANCE_CYCLIC,advance_other,NULL};
    const void *quarter=MATH_QUARTER(function_hook_descriptor(index)->user);
    if(index==ES2_HOOK_INDEX_track_sort) return es2_track_sort_registers(&harness_memory,c->a[0]);
    if(index==ES2_HOOK_INDEX_track_step) return es2_track_step_registers(&harness_memory,&classes,c->a[0],c->a[1],c->r[1],quarter,padding);
    if(index==ES2_HOOK_INDEX_track_list) return es2_track_list_registers(&harness_memory,c->a[0],c->a[1],&classes,quarter,padding);
    return es2_node_matrices_registers(&harness_memory,c->a[0],quarter,padding);
}

/* Compares the arena (masked bytes counted, then aligned) and the stub
 * trace; `after`/`expected_*` hold the original's run. */
static uint64_t tc_residue,tc_stub_total;
static int tc_check(const char *what,unsigned n,const tc_case *c,uint64_t expected,uint64_t actual,
                    uint32_t expected_calls,uint32_t expected_hash) {
    for(unsigned i=0;i<c->mask_count;++i) {
        if(advance_arena[c->masked[i]]!=advance_after[c->masked[i]]) ++tc_residue;
        advance_arena[c->masked[i]]=advance_after[c->masked[i]];
    }
    if(expected==actual && expected_calls==tc_calls && expected_hash==tc_hash && !memcmp(advance_arena,advance_after,sizeof(advance_arena))) return 0;
    unsigned diff=0; while(diff<sizeof(advance_arena) && advance_arena[diff]==advance_after[diff]) ++diff;
    fprintf(stderr,"FAIL: %s case %u (%s) original=%016llx core=%016llx arena_offset=%u (manager at %u) original_byte=%u core_byte=%u stub calls %u/%u hash %08x/%08x args=%08x,%08x regs=%08x,%08x\n",
        current_function,n,what,(unsigned long long)expected,(unsigned long long)actual,diff,(unsigned)(c->manager-advance_arena),
        diff<sizeof(advance_arena)?advance_after[diff]:0,diff<sizeof(advance_arena)?advance_arena[diff]:0,
        expected_calls,tc_calls,expected_hash,tc_hash,c->a[0],c->a[1],c->r[0],c->r[1]);
    return 1;
}

static int tc_direct(unsigned index,unsigned n,const tc_case *c) {
    memcpy(advance_before,advance_arena,sizeof(advance_arena));
    tc_reset(); uint64_t expected=actor_call(index,c->a,c->r);
    uint32_t calls=tc_calls,hash=tc_hash; tc_stub_total+=calls;
    memcpy(advance_after,advance_arena,sizeof(advance_arena));
    memcpy(advance_arena,advance_before,sizeof(advance_arena));
    tc_reset(); uint64_t actual=tc_core(index,c,0);
    return tc_check("direct",n,c,expected,actual,calls,hash);
}

/* Original on a 31/a7 stack (EAX = EDX = ECX = pattern), then the installed
 * replacement on the same state, fast and preserving dispatch. */
static int tc_installed_probes(unsigned index,unsigned states) {
    const es2_hook_descriptor *d=function_hook_descriptor(index); BYTE saved_fpu[108]; es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi"); int status=0; unsigned total=0;
    for(unsigned fast=0;fast<2 && !status;++fast) for(unsigned poison=0;poison<2 && !status;++poison) for(unsigned n=0;n<states && !status;++n) {
        tc_case c; uint32_t pattern=poison?0xa7a7a7a7u:0x31313131u,pair[2]; BYTE expected_fpu[108],actual_fpu[108];
        tc_build(index,n,&c); c.r[0]=c.r[1]=c.r[2]=pattern;
        memcpy(advance_before,advance_arena,sizeof(advance_arena));
        tc_reset(); sg_poison_call(mapped+d->rva,c.a,pattern,pair); es2_hook_fpu_save(expected_fpu);
        uint64_t expected=(uint64_t)pair[1]<<32|pair[0]; uint32_t calls=tc_calls,hash=tc_hash;
        memcpy(advance_after,advance_arena,sizeof(advance_arena));
        es2_hook h={0}; es2_hook_options o={0}; o.image=mapped; o.expected_base=0x400000; o.expected_image_size=0x2d8000; o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast); if(!weapons_install_hook(&h,d,&o,function_hook_extension(index))) return fail("track callee poison install");
        memcpy(advance_arena,advance_before,sizeof(advance_arena));
        tc_reset(); sg_poison_call(mapped+d->rva,c.a,pattern,pair); es2_hook_fpu_save(actual_fpu);
        uint64_t actual=(uint64_t)pair[1]<<32|pair[0];
        if(tc_check(fast?"installed fast":"installed preserving",n,&c,expected,actual,calls,hash)) status=1;
        else if(memcmp(expected_fpu,actual_fpu,12) || memcmp(expected_fpu+28,actual_fpu+28,80) || h.calls!=1)
            status=fail("track callee installed x87 state or call count");
        if(!es2_hook_uninstall(&h)) return fail("track callee poison uninstall");
        ++total;
    }
    es2_hook_set_fast_path(TRUE); __asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status) printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/arena/stub trace\n",d->name,total);
    return status;
}

/* node_matrices on a 31/a7 stack against the core with the pattern byte as
 * its padding, byte 19 of every node matrix compared: before the first root
 * call the child calls find the pattern there, after it the high byte of the
 * root matrix pointer that 0x478c1c pushed (animation.c). */
static int tc_residue_probe(unsigned index,unsigned states) {
    const es2_hook_descriptor *d=function_hook_descriptor(index); int status=0; unsigned total=0;
    unsigned long long from_pattern=0,from_root=0;
    for(unsigned poison=0;poison<2 && !status;++poison) for(unsigned n=0;n<states && !status;++n) {
        tc_case c; uint32_t pattern=poison?0xa7a7a7a7u:0x31313131u,pair[2];
        tc_build(index,n,&c); c.r[0]=c.r[1]=c.r[2]=pattern; c.mask_count=0;
        memcpy(advance_before,advance_arena,sizeof(advance_arena));
        tc_reset(); sg_poison_call(mapped+d->rva,c.a,pattern,pair);
        uint64_t expected=(uint64_t)pair[1]<<32|pair[0]; uint32_t calls=tc_calls,hash=tc_hash;
        memcpy(advance_after,advance_arena,sizeof(advance_arena));
        size_t matrices=(size_t)((BYTE *)(uintptr_t)get32(c.manager+0x16)-advance_arena);
        for(unsigned i=0;i<get16(c.manager+0x0c);++i) {
            BYTE b=advance_after[matrices+32*i+19];
            if(b==advance_before[matrices+32*i+19]) continue;
            if(b==(BYTE)pattern) ++from_pattern; else if(b==(BYTE)(advance_va(advance_arena+matrices)>>24)) ++from_root;
        }
        memcpy(advance_arena,advance_before,sizeof(advance_arena));
        tc_reset(); uint64_t actual=tc_core(index,&c,(uint8_t)pattern);
        if(tc_check("residue",n,&c,expected,actual,calls,hash)) status=1;
        ++total;
    }
    if(!status) printf("PASS: %s %u residue cases on a 31/a7 stack, byte 19 of the node matrices exact (%llu from the pattern, %llu the root pointer's high byte %02x)\n",
        d->name,total,from_pattern,from_root,(unsigned)(advance_va(advance_arena)>>24));
    if(!status && !from_root) status=fail("node_matrices residue probe reached no root pointer byte");
    return status;
}

/* The original with its callee hooks installed in rotating modes, then the
 * hook under test original (modes 0-2) or replaced (3-5) on the same state. */
static int tc_real(unsigned index) {
    static const unsigned sort_nested[]={ES2_HOOK_INDEX_track_count};
    static const unsigned step_nested[]={ES2_HOOK_INDEX_clip_pose,ES2_HOOK_INDEX_track_forward,ES2_HOOK_INDEX_track_inverse};
    static const unsigned list_nested[]={ES2_HOOK_INDEX_track_count,ES2_HOOK_INDEX_track_step,ES2_HOOK_INDEX_track_sort,
        ES2_HOOK_INDEX_track_scaled,ES2_HOOK_INDEX_clip_pose,ES2_HOOK_INDEX_track_forward,ES2_HOOK_INDEX_track_inverse};
    static const unsigned node_nested[]={ES2_HOOK_INDEX_pose_parent,ES2_HOOK_INDEX_pose_matrix};
    const unsigned *nested=index==ES2_HOOK_INDEX_track_sort?sort_nested:index==ES2_HOOK_INDEX_track_step?step_nested:
        index==ES2_HOOK_INDEX_track_list?list_nested:node_nested;
    unsigned count=index==ES2_HOOK_INDEX_track_sort?1:index==ES2_HOOK_INDEX_track_step?3:index==ES2_HOOK_INDEX_track_list?7:2;
    const es2_hook_descriptor *d=function_hook_descriptor(index); int status=0; unsigned total=0; uint64_t entered=0;
    for(unsigned mode=0;mode<6 && !status;++mode) for(unsigned n=0;n<512 && !status;++n) {
        tc_case c; uint32_t pattern=n&1?0xa7a7a7a7u:0x31313131u,pair[2];
        tc_build(index,n,&c); c.r[0]=c.r[1]=c.r[2]=pattern;
        es2_hook hk[8]; memset(hk,0,sizeof(hk)); es2_hook_options o={0}; o.image=mapped; o.expected_base=0x400000; o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j) {
            o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);
            if(e && e->compare_unsupported && o.mode==ES2_HOOK_COMPARE) o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e)) return fail("track callee nested install");
        }
        memcpy(advance_before,advance_arena,sizeof(advance_arena));
        tc_reset(); sg_poison_call(mapped+d->rva,c.a,pattern,pair);
        uint64_t expected=(uint64_t)pair[1]<<32|pair[0]; uint32_t calls=tc_calls,hash=tc_hash;
        memcpy(advance_after,advance_arena,sizeof(advance_arena));
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+count,d,&o,function_hook_extension(index))) return fail("track callee caller install");
        memcpy(advance_arena,advance_before,sizeof(advance_arena));
        tc_reset(); sg_poison_call(mapped+d->rva,c.a,pattern,pair);
        uint64_t actual=(uint64_t)pair[1]<<32|pair[0];
        if(tc_check(mode<3?"real original":"real replaced",n,&c,expected,actual,calls,hash)) { fprintf(stderr,"real mode=%u\n",mode); status=1; }
        for(unsigned j=0;j<=count;++j) {
            if(j<count) entered+=hk[j].calls;
            if(hk[j].mismatches) status=fail("track callee nested compare mismatch");
            if(hk[j].descriptor && !es2_hook_uninstall(hk+j)) return fail("track callee nested uninstall");
        }
        ++total;
    }
    if(!status) printf("PASS: %s %u real-callee cases, nested hooks in rotating modes (%llu nested entries), 31/a7 stack, full pair/arena/stub trace\n",
        d->name,total,(unsigned long long)entered);
    return status;
}

static int track_callee_cases(unsigned index,unsigned limit) {
    const es2_hook_descriptor *d=function_hook_descriptor(index); current_function=d->name;
    advance_other_vtable[8]=(uint32_t)(uintptr_t)tc_next;
    advance_other_vtable[9]=(uint32_t)(uintptr_t)tc_previous;
    advance_other_vtable[10]=advance_other_vtable[11]=(uint32_t)(uintptr_t)advance_other_reached;
    const char *quick=getenv("ES2_INVITRO_TRACK_CALLEE_CASES");
    unsigned cases=case_budget(limit,1001024u);if(!limit&&quick&&*quick)cases=(unsigned)strtoul(quick,NULL,10);
    unsigned coverage=index==ES2_HOOK_INDEX_track_step?TC_STEP_COVERAGE:TC_SORT_COVERAGE;
    int status=0; tc_residue=tc_stub_total=0;
    if(!limit) coverage_enable(1);
    for(unsigned n=0;n<cases && !status;++n) {
        current_case=n; current_case_valid=1;
        if(!limit && n==coverage) coverage_enable(0);
        tc_case c; tc_build(index,n,&c);
        if(advance_used>sizeof(advance_arena)) { status=fail("track callee arena too small"); break; }
        status=tc_direct(index,n,&c);
    }
    if(!limit) coverage_enable(0);
    current_case_valid=0;
    if(!limit && !status && index==ES2_HOOK_INDEX_node_matrices) status=tc_residue_probe(index,4096);
    if(!limit && !status) status=tc_installed_probes(index,64);
    if(!limit && !status) status=tc_real(index);
    if(status) return 1;
    printf("PASS: %-20s %u cases; exact EDX:EAX, %u arena bytes except byte 19 of written matrices (%llu differed), %llu ordered own frame-step calls\n",
        d->name,cases,(unsigned)sizeof(advance_arena),(unsigned long long)tc_residue,(unsigned long long)tc_stub_total);
    fflush(stdout); return 0;
}
