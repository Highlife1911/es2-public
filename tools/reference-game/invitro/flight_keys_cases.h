/* flight_keys group: flight_key 45a324, the flight-key callback of the hot-key
 * table (input.md, "flight-key callback 45a324"). The direct cases run the
 * original (cdecl, one slot) and the core on the same state: the key arrays
 * 4d2394/4d23a3/4d23b2 and the Alt word 4d23d6 (window 4d2380..4d23ff), the
 * demo and quit words 4d2530/4d2532, the byte 4d2571, the capture flag 49eaa5
 * (window 49eaa0..49eaa7), the list 49eb0b (window 49eb00..49eb3f) and the flag
 * 49ef38. The windows are random; the demo word is clear in seven cases of
 * eight, the capture flag set in seven of eight (any nonzero byte), 4d2571 and
 * 49ef38 zero or not; the list is the shipped one in three cases of four, else
 * up to a quarter of its bytes are replaced by Enter, Space, Num-/Num+, other
 * 7-bit codes or any byte (so the Enter test and the folded indices 16..29 are
 * reached). The code is mostly a list byte (7-bit, with or without 80h), else
 * one of the special keys or any byte, with random Shift/Ctrl/Alt bits and in
 * one case of four a random high word. Compared: EDX:EAX and all windows. Then
 * installed 31/a7 poison probes and a real-caller pass through the keyboard
 * handler 477bec with the real callbacks on their shipped lists
 * (keyboard_cases.h's kr_build: GetKeyState and the focus helper as its logging
 * stubs), key_message original or replaced, flight_key original or replaced,
 * against a run without hooks: key_message's EAX/EDX, the ring, entries, device
 * and callback state. */
enum { FK_WINDOWS=6, FK_REAL=1024 };
static const uint32_t fk_rva[FK_WINDOWS]={0xd2380,0xd2530,0xd2571,0x9eaa0,0x9eb00,0x9ef38};
static const unsigned fk_len[FK_WINDOWS]={0x80,4,1,8,0x40,4};
#define FK_BYTES (0x80+4+1+8+0x40+4)
#define FK_VA(va) (mapped+(uint32_t)(va)-0x400000u)
static BYTE fk_shipped[30];
static unsigned fk_demo,fk_off,fk_found,fk_up,fk_folded,fk_moved;
static void fk_windows(BYTE *out,int restore){
    for(unsigned i=0,at=0;i<FK_WINDOWS;at+=fk_len[i++]){
        if(restore)memcpy(mapped+fk_rva[i],out+at,fk_len[i]);else memcpy(out+at,mapped+fk_rva[i],fk_len[i]);
    }
}
static BYTE fk_state_byte(void){unsigned k=random32()%8;return k<4?0:k<7?1:(BYTE)random32();}
static void fk_build(unsigned n,uint32_t a[6],uint32_t r[3]){
    (void)n;memset(a,0,24);for(unsigned i=0;i<3;++i)r[i]=random32();
    for(unsigned i=0;i<FK_WINDOWS;++i)random_bytes(mapped+fk_rva[i],fk_len[i]);
    BYTE *list=FK_VA(0x49eb0b);memcpy(list,fk_shipped,30);
    if(random32()%4==0){
        ++fk_moved;static const BYTE special[]={0x1c,0x39,0x4a,0x4e};
        for(unsigned j=0;j<30;++j){
            if(random32()%4)continue;
            unsigned k=random32()%4;
            list[j]=k==0?special[random32()%4]:k==1?(BYTE)(random32()&0x7f):k==2?fk_shipped[random32()%15]:(BYTE)random32();
        }
    }
    for(unsigned j=0;j<0x2d;++j)FK_VA(0x4d2394)[j]=fk_state_byte();
    put16(FK_VA(0x4d2530),(uint16_t)(random32()%8?0:random32()|1));
    *FK_VA(0x49eaa5)=(BYTE)(random32()%8?random32()%255+1:0);
    *FK_VA(0x4d2571)=(BYTE)(random32()%2?0:random32()%2?1:random32());
    put32(FK_VA(0x49ef38),random32()%2?0:random32()%2?1:random32());
    uint32_t code,k=random32()%10;
    if(k<6)code=list[random32()%30]&0x7fu;
    else if(k<8){static const BYTE special[]={0x1c,0x39,0x4a,0x4e};code=special[random32()%4];}
    else code=random32()&0x7fu;
    if(random32()%2)code|=0x80u;
    code|=random32()%2?0:(random32()&0xe00u);
    if(random32()%4==0)code|=random32()&0xfffff100u;
    a[0]=code;
    /* The paths the original takes, counted before it runs. */
    if(get16(FK_VA(0x4d2530)))++fk_demo;
    else if(!*FK_VA(0x49eaa5))++fk_off;
    else{
        unsigned i=0;while(i<30&&list[i]!=(code&0x7fu))++i;
        if(i>15&&i<30)++fk_folded;
        if(i<15||(i>15&&i<30)){++fk_found;if(code&0x80u)++fk_up;}
    }
}
static int fk_report(const char *what,unsigned n,const uint64_t pair[2],const BYTE *want,const BYTE *got){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1]);
    for(unsigned j=0;j<FK_BYTES;++j)if(want[j]!=got[j]){fprintf(stderr," window byte %u %02x/%02x\n",j,want[j],got[j]);break;}
    return 1;
}
static int fk_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[FK_BYTES],after[FK_BYTES],actual[FK_BYTES];
    fk_windows(before,0);
    uint64_t pair[2];pair[0]=weapons_original(index,a,r);fk_windows(after,0);
    fk_windows(before,1);pair[1]=x->replacement_context(a,d->user,r);fk_windows(actual,0);
    if(pair[0]!=pair[1]||memcmp(after,actual,FK_BYTES))return fk_report("direct",n,pair,after,actual);
    return 0;
}
static int fk_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;fk_build(n,a,r);
        BYTE before[FK_BYTES],after[FK_BYTES],actual[FK_BYTES],expected_fpu[108],actual_fpu[108];
        fk_windows(before,0);
        sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);fk_windows(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("flight_key poison install");
        fk_windows(before,1);
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);es2_hook_fpu_save(actual_fpu);fk_windows(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||memcmp(after,actual,FK_BYTES)||
           memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=fk_report(fast?"installed fast":"installed preserving",n,pair,after,actual);
        if(!es2_hook_uninstall(&h))return fail("flight_key poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, EDX:EAX and windows\n",d->name,total);
    return status;
}
/* Through the only caller: 477bec with the real callbacks 477a7c,
 * 45a324 and 45a498 on their shipped lists (kr_build), the reference
 * without hooks against flight_key original (mode bit 0 clear) or
 * replaced, key_message original (bit 1 clear) or replaced. */
static int fk_real(unsigned index){
    const es2_hook_descriptor *d=function_hook_descriptor(index),*k=function_hook_descriptor(ES2_HOOK_INDEX_key_message);
    int status=0;unsigned total=0;uint64_t entered=0,nested=0;
    BYTE saved_focus[5];uint32_t saved_iat;
    if(kb_patch(saved_focus,&saved_iat,1))return 1;
    for(unsigned mode=0;mode<4&&!status;++mode)for(unsigned n=0;n<FK_REAL&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;kr_build(a,r);
        static BYTE before[KB_GLOBALS],after[KB_GLOBALS],actual[KB_GLOBALS];BYTE wb[KR_BYTES],wa[KR_BYTES],wx[KR_BYTES];
        memcpy(kb_before,kb_arena,KB_SIZE);kb_globals(before,0);kr_windows(wb,0);
        kb_reset();sg_poison_call(mapped+k->rva,a,pattern,expected_pair);
        memcpy(kb_after,kb_arena,KB_SIZE);kb_globals(after,0);kr_windows(wa,0);
        es2_hook h={0},hk={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        o.mode=mode&1?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("flight_key real install");
        if(mode&2){o.mode=ES2_HOOK_REPLACED;if(!weapons_install_hook(&hk,k,&o,function_hook_extension(ES2_HOOK_INDEX_key_message)))return fail("flight_key real key_message install");}
        memcpy(kb_arena,kb_before,KB_SIZE);kb_globals(before,1);kr_windows(wb,1);kb_reset();
        sg_poison_call(mapped+k->rva,a,pattern,actual_pair);kb_globals(actual,0);kr_windows(wx,0);
        if(memcmp(expected_pair,actual_pair,8)||memcmp(kb_after,kb_arena,KB_SIZE)||kb_globals_differ(after,actual)||memcmp(wa,wx,KR_BYTES)||
           (mode&2&&hk.calls!=1)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            fprintf(stderr,"real 477bec mode=%u flight_key calls %llu key_message calls %llu\n",mode,(unsigned long long)h.calls,(unsigned long long)hk.calls);
            for(unsigned j=0;j<KR_BYTES;++j)if(wa[j]!=wx[j]){fprintf(stderr," callback state byte %u %02x/%02x\n",j,wa[j],wx[j]);break;}
            kb_calls=0;status=kb_report("flight_key real",n,pair,0,NULL);
        }
        if(mode&1){entered+=h.calls;if(mode&2)nested+=h.calls;}
        if(mode&2&&!es2_hook_uninstall(&hk))return fail("flight_key real key_message uninstall");
        if(!es2_hook_uninstall(&h))return fail("flight_key real uninstall");
        kb_globals(before,1);kr_windows(wb,1);++total;
    }
    if(kb_patch(saved_focus,&saved_iat,0))return 1;
    if(!status&&(!entered||!nested)){fprintf(stderr,"FAIL: %s real pass entered %llu, nested %llu\n",d->name,(unsigned long long)entered,(unsigned long long)nested);status=1;}
    if(!status)printf("PASS: %s %u real-caller cases under 477bec (%llu replaced entries, %llu under the replaced key_message), "
        "both hooks original/replaced, EAX/EDX, ring/entries/device, callback state\n",d->name,total,(unsigned long long)entered,(unsigned long long)nested);
    return status;
}
static int flight_keys_cases(unsigned index,unsigned limit){
    BYTE saved[FK_BYTES],saved_kb[KB_GLOBALS],saved_kr[KR_BYTES];
    fk_windows(saved,0);kb_globals(saved_kb,0);kr_windows(saved_kr,0);
    memcpy(fk_shipped,FK_VA(0x49eb0b),30);memcpy(kb_shipped,mapped+KB_TABLE,KB_TABLE_LEN);
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_FLIGHT_KEYS_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    fk_demo=fk_off=fk_found=fk_up=fk_folded=fk_moved=0;
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];fk_build(n,a,r);status=fk_compare(index,n,a,r);}
    const unsigned demo=fk_demo,off=fk_off,found=fk_found,up=fk_up,folded=fk_folded,moved=fk_moved;
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    fk_windows(saved,1);
    if(!limit&&!status)status=fk_installed_probes(index,64);
    fk_windows(saved,1);
    if(!limit&&!status)status=fk_real(index);
    fk_windows(saved,1);kb_globals(saved_kb,1);kr_windows(saved_kr,1);
    if(!status)printf("PASS: %-20s %u cases (%u demo stops, %u with capture off, %u keys found, %u of them key-up, %u folded, %u modified lists); EDX:EAX and windows\n",
        function_hook_descriptor(index)->name,cases,demo,off,found,up,folded,moved);
    return status;
}
