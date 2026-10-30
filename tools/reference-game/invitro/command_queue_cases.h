/* command_queue group: command_queue 45a498, the command-key queue callback of
 * the hot-key table (input.md, "command-key queue callback 45a498"). The direct
 * cases run the original (cdecl, one slot, entry EAX/EDX/ECX random) and the
 * core on the same state: the capture byte and index (window 49ea80..49eabf:
 * 49eaa0 count, 49eaa4 queue flag) and the data around the queue (window
 * 4d2000..4d27ff: the queue 4d20c4, the control record 4d22c6 behind its 257
 * words, and the globals up to stream A). The windows are random; the flag byte
 * is zero in one case of eight. The count is a small index (0..0x120,
 * 0x100..0x102 over-represented: the record behind the queue), a larger one
 * still inside the window (up to 0x3bf), the same with bit 31 set (the 32-bit
 * scale-2 address wraps to the same word), or an index that makes the store
 * alias the count, the flag or their neighbours (4d20c4 + 2 * index within
 * 49ea80..49eabf: the increment must reread the count). The code is any dword,
 * in three cases of four a key code (7-bit scan code, 80h, Alt/Ctrl/Shift
 * bits). Compared: EDX:EAX and both windows. Then installed 31/a7 poison probes
 * and a real-caller pass through the keyboard handler 477bec with the real
 * callbacks on their shipped lists (keyboard_cases.h's kr_build: GetKeyState
 * and the focus helper as its logging stubs), key_message original or replaced,
 * command_queue original or replaced, against a run without hooks:
 * key_message's EAX/EDX, the ring, entries, device and callback state. */
enum { CQU_WINDOWS=2, CQU_REAL=1024 };
static const uint32_t cqu_rva[CQU_WINDOWS]={0x9ea80,0xd2000};
static const unsigned cqu_len[CQU_WINDOWS]={0x40,0x800};
#define CQU_BYTES (0x40+0x800)
#define CQU_VA(va) (mapped+(uint32_t)(va)-0x400000u)
static unsigned cqu_off,cqu_small,cqu_large,cqu_wrapped,cqu_alias,cqu_past;
static void cqu_windows(BYTE *out,int restore){
    for(unsigned i=0,at=0;i<CQU_WINDOWS;at+=cqu_len[i++]){
        if(restore)memcpy(mapped+cqu_rva[i],out+at,cqu_len[i]);else memcpy(out+at,mapped+cqu_rva[i],cqu_len[i]);
    }
}
static void cqu_build(unsigned n,uint32_t a[6],uint32_t r[3]){
    (void)n;memset(a,0,24);for(unsigned i=0;i<3;++i)r[i]=random32();
    for(unsigned i=0;i<CQU_WINDOWS;++i)random_bytes(mapped+cqu_rva[i],cqu_len[i]);
    *CQU_VA(0x49eaa4)=(BYTE)(random32()%8?random32()%255+1:0);
    /* The index that puts the store at 4d20c4 + 2 * index = 49eaa0 + 2 * k. */
    const uint32_t alias=(uint32_t)(((int32_t)(0x49eaa0-0x4d20c4))/2);
    uint32_t index,k=random32()%20;
    switch(random32()%10){
    case 0:case 1:case 2:case 3:index=random32()%0x121u;break;
    case 4:index=0xfeu+random32()%6u;break;
    case 5:index=0x121u+random32()%0x29fu;break;
    case 6:index=(random32()%0x121u)|0x80000000u;break;
    case 7:index=alias+k-4u;break;
    case 8:index=(alias+k-4u)|0x80000000u;break;
    default:index=alias+random32()%0x20u-0x10u;
    }
    put32(CQU_VA(0x49eaa0),index);
    uint32_t code;
    if(random32()%4){code=random32()&0x7fu;if(random32()%2)code|=0x80u;code|=random32()%2?0:(random32()&0xe00u);}
    else code=random32();
    a[0]=code;
    /* The paths the original takes, counted before it runs. */
    if(!*CQU_VA(0x49eaa4))++cqu_off;
    else{
        const uint32_t at=0x4d20c4u+2u*index;
        if(index<0x121u)++cqu_small;
        else if(at>=0x4d2000u&&at<0x4d2800u)++cqu_large;
        if(index&0x80000000u&&at>=0x4d2000u&&at<0x4d2800u)++cqu_wrapped;
        if(at>=0x49ea80u&&at<0x49eac0u)++cqu_alias;
        if(index>=0x101u&&at>=0x4d2000u&&at<0x4d2800u)++cqu_past;
    }
}
static int cqu_report(const char *what,unsigned n,const uint64_t pair[2],const BYTE *want,const BYTE *got){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1]);
    for(unsigned j=0;j<CQU_BYTES;++j)if(want[j]!=got[j]){fprintf(stderr," window byte %u %02x/%02x\n",j,want[j],got[j]);break;}
    return 1;
}
static int cqu_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[CQU_BYTES],after[CQU_BYTES],actual[CQU_BYTES];
    cqu_windows(before,0);
    uint64_t pair[2];pair[0]=weapons_original(index,a,r);cqu_windows(after,0);
    cqu_windows(before,1);pair[1]=x->replacement_context(a,d->user,r);cqu_windows(actual,0);
    if(pair[0]!=pair[1]||memcmp(after,actual,CQU_BYTES))return cqu_report("direct",n,pair,after,actual);
    return 0;
}
static int cqu_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;cqu_build(n,a,r);
        BYTE before[CQU_BYTES],after[CQU_BYTES],actual[CQU_BYTES],expected_fpu[108],actual_fpu[108];
        cqu_windows(before,0);
        sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);cqu_windows(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("command_queue poison install");
        cqu_windows(before,1);
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);es2_hook_fpu_save(actual_fpu);cqu_windows(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||memcmp(after,actual,CQU_BYTES)||
           memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=cqu_report(fast?"installed fast":"installed preserving",n,pair,after,actual);
        if(!es2_hook_uninstall(&h))return fail("command_queue poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, EDX:EAX and windows\n",d->name,total);
    return status;
}
/* Through the only caller: 477bec with the real callbacks 477a7c,
 * 45a498 and 45a498 on their shipped lists (kr_build), the reference
 * without hooks against command_queue original (mode bit 0 clear) or
 * replaced, key_message original (bit 1 clear) or replaced. */
static int cqu_real(unsigned index){
    const es2_hook_descriptor *d=function_hook_descriptor(index),*k=function_hook_descriptor(ES2_HOOK_INDEX_key_message);
    int status=0;unsigned total=0;uint64_t entered=0,nested=0;
    BYTE saved_focus[5];uint32_t saved_iat;
    if(kb_patch(saved_focus,&saved_iat,1))return 1;
    for(unsigned mode=0;mode<4&&!status;++mode)for(unsigned n=0;n<CQU_REAL&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;kr_build(a,r);
        static BYTE before[KB_GLOBALS],after[KB_GLOBALS],actual[KB_GLOBALS];BYTE wb[KR_BYTES],wa[KR_BYTES],wx[KR_BYTES];
        memcpy(kb_before,kb_arena,KB_SIZE);kb_globals(before,0);kr_windows(wb,0);
        kb_reset();sg_poison_call(mapped+k->rva,a,pattern,expected_pair);
        memcpy(kb_after,kb_arena,KB_SIZE);kb_globals(after,0);kr_windows(wa,0);
        es2_hook h={0},hk={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        o.mode=mode&1?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("command_queue real install");
        if(mode&2){o.mode=ES2_HOOK_REPLACED;if(!weapons_install_hook(&hk,k,&o,function_hook_extension(ES2_HOOK_INDEX_key_message)))return fail("command_queue real key_message install");}
        memcpy(kb_arena,kb_before,KB_SIZE);kb_globals(before,1);kr_windows(wb,1);kb_reset();
        sg_poison_call(mapped+k->rva,a,pattern,actual_pair);kb_globals(actual,0);kr_windows(wx,0);
        if(memcmp(expected_pair,actual_pair,8)||memcmp(kb_after,kb_arena,KB_SIZE)||kb_globals_differ(after,actual)||memcmp(wa,wx,KR_BYTES)||
           (mode&2&&hk.calls!=1)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            fprintf(stderr,"real 477bec mode=%u command_queue calls %llu key_message calls %llu\n",mode,(unsigned long long)h.calls,(unsigned long long)hk.calls);
            for(unsigned j=0;j<KR_BYTES;++j)if(wa[j]!=wx[j]){fprintf(stderr," callback state byte %u %02x/%02x\n",j,wa[j],wx[j]);break;}
            kb_calls=0;status=kb_report("command_queue real",n,pair,0,NULL);
        }
        if(mode&1){entered+=h.calls;if(mode&2)nested+=h.calls;}
        if(mode&2&&!es2_hook_uninstall(&hk))return fail("command_queue real key_message uninstall");
        if(!es2_hook_uninstall(&h))return fail("command_queue real uninstall");
        kb_globals(before,1);kr_windows(wb,1);++total;
    }
    if(kb_patch(saved_focus,&saved_iat,0))return 1;
    if(!status&&(!entered||!nested)){fprintf(stderr,"FAIL: %s real pass entered %llu, nested %llu\n",d->name,(unsigned long long)entered,(unsigned long long)nested);status=1;}
    if(!status)printf("PASS: %s %u real-caller cases under 477bec (%llu replaced entries, %llu under the replaced key_message), "
        "both hooks original/replaced, EAX/EDX, ring/entries/device, callback state\n",d->name,total,(unsigned long long)entered,(unsigned long long)nested);
    return status;
}
static int command_queue_cases(unsigned index,unsigned limit){
    BYTE saved[CQU_BYTES],saved_kb[KB_GLOBALS],saved_kr[KR_BYTES];
    cqu_windows(saved,0);kb_globals(saved_kb,0);kr_windows(saved_kr,0);
    memcpy(kb_shipped,mapped+KB_TABLE,KB_TABLE_LEN);
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_COMMAND_QUEUE_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    cqu_off=cqu_small=cqu_large=cqu_wrapped=cqu_alias=cqu_past=0;
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];cqu_build(n,a,r);status=cqu_compare(index,n,a,r);}
    const unsigned off=cqu_off,small=cqu_small,large=cqu_large,wrapped=cqu_wrapped,alias=cqu_alias,past=cqu_past;
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    cqu_windows(saved,1);
    if(!limit&&!status)status=cqu_installed_probes(index,64);
    cqu_windows(saved,1);
    if(!limit&&!status)status=cqu_real(index);
    cqu_windows(saved,1);kb_globals(saved_kb,1);kr_windows(saved_kr,1);
    if(!status)printf("PASS: %-20s %u cases (%u with the queue flag clear, %u indices 0..120h, %u beyond it inside 4d2000..4d27ff, %u of them past the queue, %u wrapped by bit 31, %u stores on 49ea80..49eabf); EDX:EAX and windows\n",
        function_hook_descriptor(index)->name,cases,off,small,large,past,wrapped,alias);
    return status;
}
