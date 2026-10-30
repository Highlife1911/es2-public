/* hud_helpers group: panel_visible 431410, the corner test of a rectangle
 * against the cockpit canvas view (hud.md, "HUD-region helpers"). The direct
 * cases run the original and the core on one arena: the cockpit object H
 * (panel-off byte H+20Fh, canvas pointer H+4), the canvas C (clip rect
 * C+210h..+21Fh, origin C+220h/+224h) and the argument rect, plus the globals
 * 49b088 (cockpit pointer), 4d253e..4d2545 (view size) and 4d2556..4d255d
 * (scroll offset), which are set per case, compared and restored. Rect corners
 * are placed at the view's edges (-1/0/+1), inside, or anywhere, from an
 * independent model of the view; sizes and offsets include the signed wrap
 * (7FFFFFFFh, 80000000h), so every signed test goes both ways. The panel-off
 * byte is set in one case in eight; one rect in sixteen lies over H or C. The
 * full EDX:EAX pair is compared (the panel-off path returns the entry EDX).
 * Then installed 31/a7 poison probes and a real-caller pass under the
 * two-instruction wrapper 444b84, which returns 431410's EDX:EAX to its own
 * caller. */
enum { HH_SIZE=0x800, HH_H=0x40, HH_C=0x300, HH_RECT=0x600, HH_GLOBALS=4+8+8, HH_COCKPIT=0x9b088, HH_SIZE_WORDS=0xd253e,
    HH_SCROLL=0xd2556, HH_WRAPPER=0x44b84 };
#define HH_BASE ((uint32_t)(uintptr_t)hh_arena)
static BYTE hh_arena[HH_SIZE],hh_before[HH_SIZE],hh_after[HH_SIZE];
static unsigned hh_visible,hh_off,hh_second;
static void hh_globals(BYTE *out,int restore){
    BYTE *p[3]={mapped+HH_COCKPIT,mapped+HH_SIZE_WORDS,mapped+HH_SCROLL};
    const unsigned len[3]={4,8,8};
    for(unsigned i=0,at=0;i<3;at+=len[i++]){if(restore)memcpy(p[i],out+at,len[i]);else memcpy(out+at,p[i],len[i]);}
}
static int32_t hh_s(uint32_t x){return (int32_t)x;}
/* Coordinates: small, screen-like, large, edges. */
static uint32_t hh_coordinate(void){
    static const uint32_t edges[]={0,1,0xffffffffu,0x7fffffffu,0x80000000u,0x7ffffffeu,0x80000001u,0xffff8000u};
    switch(random32()%8){
    case 0:return edges[random32()%8];
    case 1:return random32();
    case 2:return random32()%0x100000u-0x80000u;
    default:return random32()%1024u-128u;
    }
}
static uint32_t hh_extent(uint32_t screen){
    static const uint32_t edges[]={0,1,0x7fffffffu,0x80000000u,0xffffffffu,0x7fffff00u};
    switch(random32()%8){
    case 0:return edges[random32()%6];
    case 1:return random32();
    default:return random32()%screen;
    }
}
/* A target coordinate relative to the view range lo..hi (signed). */
static uint32_t hh_target(uint32_t lo,uint32_t hi){
    switch(random32()%10){
    case 0:return lo-1u;
    case 1:return lo;
    case 2:return lo+1u;
    case 3:return hi-1u;
    case 4:return hi;
    case 5:return hi+1u;
    case 6:{uint32_t w=hi-lo;return hh_s(hi)>hh_s(lo)&&w?lo+random32()%w:lo;}
    case 7:return hh_coordinate();
    default:return random32()%2?lo+random32()%64u:hi-random32()%64u;
    }
}
/* Fills one state; a[0] = the rect, r = entry EAX/EDX/ECX. */
static void hh_build(unsigned n,uint32_t a[6],uint32_t r[3],int real){
    (void)n;uint32_t b=HH_BASE;BYTE *h=hh_arena+HH_H,*c=hh_arena+HH_C,*rect=hh_arena+HH_RECT;
    random_bytes(hh_arena,HH_SIZE);memset(a,0,24);for(unsigned i=0;i<3;++i)r[i]=random32();
    put32(mapped+HH_COCKPIT,b+HH_H);
    h[0x20f]=(BYTE)(random32()%8?0:1+random32()%255);
    if(h[0x20f])++hh_off;
    put32(h+4,b+HH_C);
    for(unsigned i=0;i<4;++i)put32(c+0x210+4*i,random32()%4?hh_coordinate():random32());
    put32(c+0x220,hh_coordinate());put32(c+0x224,hh_coordinate());
    put32(mapped+HH_SIZE_WORDS,hh_extent(640));put32(mapped+HH_SIZE_WORDS+4,hh_extent(480));
    put32(mapped+HH_SCROLL,random32()%4?hh_coordinate():0);put32(mapped+HH_SCROLL+4,random32()%4?hh_coordinate():0);
    /* The view and the rect from an independent model. */
    uint32_t vx1=get32(c+0x210)+get32(c+0x220),vy1=get32(c+0x214)+get32(c+0x224);
    uint32_t vx2=vx1+get32(mapped+HH_SIZE_WORDS),vy2=vy1+get32(mapped+HH_SIZE_WORDS+4);
    uint32_t dx=get32(mapped+HH_SCROLL),dy=get32(mapped+HH_SCROLL+4);
    put32(rect,hh_target(vx1,vx2)+dx);put32(rect+4,hh_target(vy1,vy2)+dy);
    put32(rect+8,hh_target(vx1,vx2)+dx);put32(rect+12,hh_target(vy1,vy2)+dy);
    a[0]=b+HH_RECT;
    if(!real&&random32()%16==0)a[0]=b+HH_H+random32()%(HH_RECT-HH_H);
}
static int hh_report(const char *what,unsigned n,const uint64_t pair[2],const BYTE *want,const BYTE *got){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1]);
    for(unsigned j=0;j<HH_SIZE;++j)if(hh_after[j]!=hh_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,hh_after[j],hh_arena[j]);break;}
    for(unsigned j=0;j<HH_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);break;}
    return 1;
}
static int hh_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[HH_GLOBALS],after[HH_GLOBALS],actual[HH_GLOBALS];
    memcpy(hh_before,hh_arena,HH_SIZE);hh_globals(before,0);
    uint64_t pair[2];pair[0]=weapons_original(index,a,r);
    memcpy(hh_after,hh_arena,HH_SIZE);hh_globals(after,0);
    memcpy(hh_arena,hh_before,HH_SIZE);hh_globals(before,1);
    pair[1]=x->replacement_context(a,d->user,r);hh_globals(actual,0);
    if((uint32_t)pair[0])++hh_visible;
    if((uint32_t)(pair[0]>>32)!=r[1]&&(uint32_t)(pair[0]>>32)!=get32(mapped+HH_SCROLL))++hh_second;
    if(pair[0]!=pair[1]||memcmp(hh_after,hh_arena,HH_SIZE)||memcmp(after,actual,HH_GLOBALS))
        return hh_report("direct",n,pair,after,actual);
    return 0;
}
static int hh_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;hh_build(n*37u+5u,a,r,0);
        BYTE before[HH_GLOBALS],after[HH_GLOBALS],actual[HH_GLOBALS],expected_fpu[108],actual_fpu[108];
        memcpy(hh_before,hh_arena,HH_SIZE);hh_globals(before,0);
        sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        memcpy(hh_after,hh_arena,HH_SIZE);hh_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("panel_visible poison install");
        memcpy(hh_arena,hh_before,HH_SIZE);hh_globals(before,1);
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);es2_hook_fpu_save(actual_fpu);hh_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||memcmp(hh_after,hh_arena,HH_SIZE)||memcmp(after,actual,HH_GLOBALS)||
           memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=hh_report(fast?"installed fast":"installed preserving",n,pair,after,actual);
        if(!es2_hook_uninstall(&h))return fail("panel_visible poison uninstall");hh_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/arena/globals\n",d->name,total);
    return status;
}
/* 444b84: push the argument, call 431410, pop ecx, test al,al, return; its
 * EDX:EAX are 431410's, so the caller pass compares the pair through a real
 * call site with the hook original or replaced. */
static int hh_real(unsigned index){
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered=0;
    for(unsigned mode=0;mode<2&&!status;++mode)for(unsigned n=0;n<1536&&!status;++n){
        uint32_t a[6],r[3],w[6]={0},expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;
        hh_build(n*61u+mode,a,r,1);w[0]=a[0];
        BYTE before[HH_GLOBALS],after[HH_GLOBALS],actual[HH_GLOBALS];
        memcpy(hh_before,hh_arena,HH_SIZE);hh_globals(before,0);
        sg_poison_call(mapped+HH_WRAPPER,w,pattern,expected_pair);
        memcpy(hh_after,hh_arena,HH_SIZE);hh_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        o.mode=mode?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("panel_visible real install");
        memcpy(hh_arena,hh_before,HH_SIZE);hh_globals(before,1);
        sg_poison_call(mapped+HH_WRAPPER,w,pattern,actual_pair);hh_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||memcmp(hh_after,hh_arena,HH_SIZE)||memcmp(after,actual,HH_GLOBALS)||(mode&&h.calls!=1)){
            fprintf(stderr,"real 444b84 mode=%u calls %llu\n",mode,(unsigned long long)h.calls);status=hh_report("real",n,pair,after,actual);}
        if(mode)entered+=h.calls;
        if(!es2_hook_uninstall(&h))return fail("panel_visible real uninstall");hh_globals(before,1);++total;
    }
    if(!status&&!entered){fprintf(stderr,"FAIL: %s real pass never entered\n",d->name);status=1;}
    if(!status)printf("PASS: %s %u real-caller cases under 444b84 (%llu replaced entries), original/replaced, 31/a7 stack, pair/arena/globals\n",
        d->name,total,(unsigned long long)entered);
    return status;
}
static int hud_helpers_cases(unsigned index,unsigned limit){
    BYTE saved_globals[HH_GLOBALS];hh_globals(saved_globals,0);
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_HUD_HELPERS_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    hh_visible=hh_off=hh_second=0;
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];hh_build(n,a,r,0);status=hh_compare(index,n,a,r);}
    const unsigned visible=hh_visible,off=hh_off,second=hh_second;
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    hh_globals(saved_globals,1);
    if(!limit&&!status)status=hh_installed_probes(index,64);
    hh_globals(saved_globals,1);
    if(!limit&&!status)status=hh_real(index);
    hh_globals(saved_globals,1);
    if(!status)printf("PASS: %-20s %u cases (%u visible, %u panel off, %u with EDX from the far corner); full return pair, arena and globals\n",
        function_hook_descriptor(index)->name,cases,visible,off,second);
    return status;
}
