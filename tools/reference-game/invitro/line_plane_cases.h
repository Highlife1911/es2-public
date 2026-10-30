/* Line/plane intersection: line_plane 47e610. A pure leaf: the original runs
 * with its helpers 4953c4, 47eaa1/47df84 and 47eabe/47de2d as original code,
 * against the core on generated planes and segments. The plane constant is
 * chosen from a target num, so num lands on every side of 0 and den (0, den,
 * den +- 1, inside, outside, the 32-bit extremes); den is forced to zero by
 * orthogonal or wrapping products. out may overlap the segment or the plane.
 * Then installed 31/a7 poison probes, and a real-caller pass: contact 470798 on
 * generated terrain (terrain_leaf_cases.h generator) with line_plane nested in
 * rotating modes and contact itself original or replaced. */
enum { LP_SIZE=0x200, LP_PLANE=0x40, LP_SEG=0x80, LP_OUT=0xc0 };
#define LP_BASE ((uint32_t)(uintptr_t)lp_arena)
static BYTE lp_arena[LP_SIZE],lp_before[LP_SIZE],lp_after[LP_SIZE];
static unsigned lp_hits;
static int lp_stack(uint32_t value){
    /* Same fixed stack window as terrain_leaf_cases.h. */
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static uint32_t lp_value(unsigned n){
    static const uint32_t edges[]={0,1,0xffffffffu,0x80000000u,0x7fffffffu,0x10000,0xffff0000u,0x4000,0xffffc000u,2,0xfffffffeu};
    switch(n<512?n%4:random32()%6){
    case 0:return edges[random32()%11];
    case 1:return random32()%0x8001u-0x4000u;   /* normal words and small coordinates */
    case 2:return random32()%0x2000001u-0x1000000u;
    default:return random32();
    }
}
/* Category (n % 8 in the boundary prefix): 0 num = 0, 1 num = den,
 * 2 num = den -+ 1 (just outside or inside), 3 num inside, 4 num = -1 or
 * the other sign, 5 den forced to 0, 6 fully random, 7 num = den + 1. */
static void lp_build(unsigned n,uint32_t a[6],uint32_t r[3]){
    uint32_t b=LP_BASE;random_bytes(lp_arena,LP_SIZE);memset(a,0,24);for(unsigned i=0;i<3;++i)r[i]=random32();
    uint32_t nv[3],s[3],d[3];
    for(unsigned k=0;k<3;++k){nv[k]=lp_value(n/8);s[k]=lp_value(n/8);d[k]=lp_value(n/8);}
    unsigned kind=n<4096?n%8:random32()%8;
    if(kind==5){
        switch(random32()%3){
        case 0:nv[2]=0;d[0]=nv[1];d[1]=0u-nv[0];break;            /* orthogonal in XY */
        case 1:nv[0]=0x10000u;d[0]=0x10000u;nv[1]=nv[2]=0;break;   /* 2^32 wraps to 0 */
        default:d[0]=d[1]=d[2]=0;break;                            /* degenerate segment */
        }
    }
    uint32_t den=nv[0]*d[0]+nv[1]*d[1]+nv[2]*d[2],num;
    switch(kind){
    case 0:num=0;break;
    case 1:num=den;break;
    case 2:num=(int32_t)den>0?den-1u+2u*(random32()%2):den+1u-2u*(random32()%2);break;
    case 3:num=den?(uint32_t)((int64_t)(int32_t)den*(int64_t)(random32()%1000)/1000):random32();break;
    case 4:num=random32()%2?0xffffffffu:(int32_t)den>0?0u-1u-random32()%64:1u+random32()%64;break;
    case 7:num=den+((int32_t)den>0?1u:0xffffffffu);break;
    default:num=random32();break;
    }
    /* c = -num - n.S (mod 2^32). */
    uint32_t c=0u-num-(nv[0]*s[0]+nv[1]*s[1]+nv[2]*s[2]);
    for(unsigned k=0;k<3;++k){put32(lp_arena+LP_PLANE+4*k,nv[k]);put32(lp_arena+LP_SEG+4*k,s[k]);put32(lp_arena+LP_SEG+12+4*k,s[k]+d[k]);}
    put32(lp_arena+LP_PLANE+12,c);
    a[0]=b+LP_PLANE;a[1]=b+LP_SEG;a[2]=b+LP_OUT;
    /* Overlaps: out on the segment (rereads see the new words) or the plane. */
    unsigned overlap=random32()%16;
    if(overlap<3)a[2]=b+LP_SEG+4u*(1u+random32()%5);
    else if(overlap==3)a[2]=b+LP_SEG+1u+random32()%20;
    else if(overlap==4)a[2]=b+LP_PLANE+4u*(random32()%4);
    else if(overlap==5)a[2]=b+LP_OUT+1u+random32()%3;
}
static int lp_same_pair(uint64_t want,uint64_t got){
    if(want==got)return 1;
    /* A hit returns the frame address EBP-28h in EDX (dead at both callers). */
    return (uint32_t)want==(uint32_t)got&&(uint8_t)want==1&&lp_stack((uint32_t)(want>>32))&&lp_stack((uint32_t)(got>>32));
}
static int lp_report(const char *what,unsigned n,const uint64_t pair[2]){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1]);
    for(unsigned j=0;j<LP_SIZE;++j)if(lp_after[j]!=lp_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,lp_after[j],lp_arena[j]);break;}
    return 1;
}
static int lp_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    memcpy(lp_before,lp_arena,LP_SIZE);
    uint64_t pair[2];pair[0]=weapons_original(index,a,r);memcpy(lp_after,lp_arena,LP_SIZE);
    memcpy(lp_arena,lp_before,LP_SIZE);pair[1]=x->replacement_context(a,d->user,r);
    lp_hits+=(uint8_t)pair[0]!=0;
    if(!lp_same_pair(pair[0],pair[1])||memcmp(lp_after,lp_arena,LP_SIZE))return lp_report("direct",n,pair);
    return 0;
}
static int lp_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;lp_build(n,a,r);
        BYTE expected_fpu[108],actual_fpu[108];
        memcpy(lp_before,lp_arena,LP_SIZE);sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        memcpy(lp_after,lp_arena,LP_SIZE);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("line_plane poison install");
        memcpy(lp_arena,lp_before,LP_SIZE);sg_poison_call(mapped+d->rva,a,pattern,actual_pair);es2_hook_fpu_save(actual_fpu);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(!lp_same_pair(pair[0],pair[1])||memcmp(lp_after,lp_arena,LP_SIZE)||
           memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=lp_report(fast?"installed fast":"installed preserving",n,pair);
        if(!es2_hook_uninstall(&h))return fail("line_plane poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/arena\n",d->name,total);
    return status;
}
/* contact 470798 over generated terrain (up to 8 x 8 cells, flags 0..2) with
 * line_plane nested (original, replaced or compare) and contact
 * original or replaced. EAX, the arena and the globals 4a0c4c/6b505c/4a0be0
 * are compared (contact's EDX after a second-plane hit is line_plane's frame
 * address, dead in 46e980). */
static int lp_real(unsigned index){
    const es2_hook_descriptor *d=function_hook_descriptor(ES2_HOOK_INDEX_contact);int status=0;unsigned total=0,ones=0;uint64_t entered=0;
    uint64_t compared=0;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<768&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;tl_build(ES2_HOOK_INDEX_contact,n,a,r,1);
        BYTE before[TL_GLOBALS],after[TL_GLOBALS],actual[TL_GLOBALS];
        memcpy(tl_before,tl_arena,TL_SIZE);tl_globals(before,0);
        sg_poison_call(mapped+d->rva,a,pattern,expected_pair);memcpy(tl_after,tl_arena,TL_SIZE);tl_globals(after,0);
        es2_hook nest={0},caller={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        o.mode=(es2_hook_mode)(mode%3);
        if(!weapons_install_hook(&nest,function_hook_descriptor(index),&o,function_hook_extension(index)))return fail("line_plane nested install");
        o.mode=mode/3?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(&caller,d,&o,function_hook_extension(ES2_HOOK_INDEX_contact)))return fail("line_plane contact install");
        memcpy(tl_arena,tl_before,TL_SIZE);tl_globals(before,1);
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);tl_globals(actual,0);
        if(expected_pair[0]!=actual_pair[0]||memcmp(tl_after,tl_arena,TL_SIZE)||memcmp(after,actual,TL_GLOBALS)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            for(unsigned j=0;j<TL_GLOBALS;++j)if(after[j]!=actual[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,after[j],actual[j]);break;}
            fprintf(stderr,"real mode=%u\n",mode);memcpy(lp_after,lp_arena,LP_SIZE);status=lp_report("real",n,pair);
        }
        ones+=expected_pair[0]!=0;if(mode%3==ES2_HOOK_REPLACED)entered+=nest.calls;
        if(mode%3==ES2_HOOK_COMPARE){compared+=nest.calls-nest.skipped;if(nest.mismatches||nest.bypassed)status=fail("line_plane nested compare mismatch");}
        if(!es2_hook_uninstall(&caller)||!es2_hook_uninstall(&nest))return fail("line_plane real uninstall");
        tl_globals(before,1);++total;
    }
    if(!status&&(!entered||!ones||!compared)){fprintf(stderr,"FAIL: line_plane real pass entered %llu, compared %llu, contacts %u\n",
        (unsigned long long)entered,(unsigned long long)compared,ones);status=1;}
    if(!status)printf("PASS: %s %u real-caller cases under contact (%u contacts, %llu replaced entries, %llu compared without mismatch), 31/a7 stack, EAX/arena/globals\n",
        function_hook_descriptor(index)->name,total,ones,(unsigned long long)entered,(unsigned long long)compared);
    return status;
}
/* Compare cases (compare_probes.h). out is kept off the plane and the segment
 * (the live caller passes the global 6b505c); a hit's EDX is a frame address
 * that moves with the stack depth of the original. */
static void lp_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    (void)index;lp_build(n,a,r);a[2]=LP_BASE+LP_OUT+random32()%4u;
}
static void lp_compare_state(BYTE *s,int restore){if(restore)memcpy(lp_arena,s,LP_SIZE);else memcpy(s,lp_arena,LP_SIZE);}
static int lp_compare_same(unsigned index,uint64_t want,uint64_t got){(void)index;return lp_same_pair(want,got);}
static int lp_compare_run(unsigned index,unsigned limit){
    compare_probe_same=lp_compare_same;int status=compare_probe_run(index,limit,lp_compare_build,lp_compare_state,LP_SIZE);
    compare_probe_same=NULL;return status;
}
static int line_plane_cases(unsigned index,unsigned limit){
    if(limit&&compare_probe_installed(index))return lp_compare_run(index,limit);
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_LINE_PLANE_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;lp_hits=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];lp_build(n,a,r);status=lp_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    if(!limit&&!status)status=lp_installed_probes(index,64);
    if(!limit&&!status)status=lp_real(index);
    if(!limit&&!status)status=lp_compare_run(index,0);
    if(!status)printf("PASS: %-20s %u cases (%u hits); full return pair (hit EDX: stack address), arena incl. overlapping out\n",
        function_hook_descriptor(index)->name,cases,lp_hits);
    return status;
}
