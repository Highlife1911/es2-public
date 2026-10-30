/* Snapshot regions of the extended compare mode, on a synthetic PE image
 * and machine code. */
#include "hook/hook.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr,"snapshot line %d: %s\n",__LINE__,#c); return 1; } } while (0)
#define IMAGE_SIZE 0x40000u
#define REGION_RVA 0x10000u
#define REGION_SIZE 0x20000u
#define A1 (image+REGION_RVA)
#define A2 (image+REGION_RVA+0x8010u)
#define HEADER (image+0x3000u)
static unsigned checks, kind, resolves, moved;
static BYTE *image, *pool;
static uintptr_t deepest;
static const BYTE signature[] = {0x90,0x90,0x90,0x90,0x90};
static const BYTE lengths[] = {1,1,1,1,1};
static void put32(BYTE *p, uint32_t x) { for (unsigned i=0;i<4;++i) p[i]=(BYTE)(x>>(8*i)); }
static uint32_t word(const BYTE *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static void add32(BYTE *p, uint32_t x) { put32(p,word(p)+x); }
typedef uint64_t (__cdecl *call1)(uint32_t);

/* F(n): [A1] += n, [A2] += n, EAX = [A1], EDX = 0. kind 1 adds one more to
 * [A2] in the replacement (a state difference 8010h bytes into the region),
 * kind 2 sets EDX. */
static uint64_t __cdecl f_replacement(const uint32_t *a, void *u) {
    (void)u; add32(A1,a[0]); add32(A2,a[0]+(kind==1)); return word(A1)|(kind==2?(uint64_t)1<<32:0);
}
/* Stack probe: reads the post-state the original left (nothing declared). */
static uint64_t __cdecl probe_replacement(const uint32_t *a, void *u) {
    (void)a; (void)u; volatile BYTE probe = 0; uintptr_t here=(uintptr_t)&probe; if (!deepest || here<deepest) deepest=here;
    return word(A1);
}
/* H(n) = F(n) through F's entry (and F's hook when installed). */
static uint64_t __cdecl h_replacement(const uint32_t *a, void *u) { (void)u; return ((call1)(image+0x1000))(a[0]); }
/* G(n): the pool block P = [HEADER]; if P, [P+100h] += n and EAX = it. */
static uint64_t __cdecl g_replacement(const uint32_t *a, void *u) {
    (void)u; BYTE *p=(BYTE *)(uintptr_t)word(HEADER); if (!p) return 0;
    add32(p+0x100,a[0]+(kind==1)); return word(p+0x100);
}
/* R(n): n >= 0 calls R(n | 80000000h) through its own entry first. */
static uint64_t __cdecl r_replacement(const uint32_t *a, void *u) {
    (void)u; if (!(a[0]&0x80000000u)) (void)((call1)(image+0x1300))(a[0]|0x80000000u);
    add32(A1,a[0]); return word(A1);
}
static uint32_t __cdecl legacy(const uint32_t *a, void *u) { (void)a; (void)u; return 0; }
static BOOL __cdecl pool_resolve(void *user, BYTE *base_image, const uint32_t *args, BYTE **base, size_t *size) {
    (void)user; (void)args; ++resolves;
    BYTE *p=(BYTE *)(uintptr_t)word(base_image+0x3000);
    if (moved && resolves%2==0 && p) p+=16;
    *base=p; *size=p?word(base_image+0x3004):0; return TRUE;
}

static es2_hook_descriptor desc(unsigned rva, es2_hook_replacement fn) {
    es2_hook_descriptor d={"snapshot",rva,signature,5,5,lengths,5,1,fn,NULL,NULL,NULL,0}; return d;
}
static es2_hook_options options(es2_hook_mode mode) {
    es2_hook_options o={0}; o.image=image; o.expected_base=(uintptr_t)image;
    o.expected_image_size=IMAGE_SIZE; o.mode=mode; return o;
}
/* Fills a pair-result extension and binds (or, with NULL, unbinds) its set. */
static BOOL make(es2_hook_extension *e, es2_hook_replacement64 fn, const es2_hook_snapshot_set *s) {
    memset(e,0,sizeof(*e)); e->result_kind=ES2_RESULT_PAIR; e->replacement64=fn; return es2_hook_bind_snapshot(e,s);
}
static void code(unsigned rva, const BYTE *tail, size_t n) {
    memcpy(image+rva,signature,5); memcpy(image+rva+5,tail,n);
    FlushInstructionCache(GetCurrentProcess(),image+rva,n+5);
}
static es2_hook_result invoke(unsigned rva, uint32_t n) {
    BYTE initial[108]; unsigned short cw=0x1332; uint32_t args[ES2_HOOK_MAX_ARGS]={n};
    __asm__ volatile("fninit; fldcw %0"::"m"(cw)); es2_hook_fpu_save(initial);
    es2_hook_result r; es2_hook_call c={image+rva,ES2_ABI_CDECL,1,args,initial,&r,NULL};
    es2_hook_machine_call(&c); __asm__ volatile("fninit"); return r;
}
static void reset_state(void) { memset(A1,0,REGION_SIZE); }

int main(void) {
    image=VirtualAlloc(NULL,IMAGE_SIZE,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE); CHECK(image);
    pool=VirtualAlloc(NULL,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE); CHECK(pool);
    put32(image,0x5a4d); put32(image+0x3c,0x80); put32(image+0x80,0x4550);
    image[0x84]=0x4c; image[0x85]=1; image[0x98]=0x0b; image[0x99]=1;
    put32(image+0xb4,(uint32_t)(uintptr_t)image); put32(image+0xd0,IMAGE_SIZE);
    BYTE f[]={0x8b,0x44,0x24,4, 0x01,0x05,0,0,0,0, 0x01,0x05,0,0,0,0, 0xa1,0,0,0,0, 0x33,0xd2, 0xc3};
    put32(f+6,(uint32_t)(uintptr_t)A1); put32(f+12,(uint32_t)(uintptr_t)A2); put32(f+17,(uint32_t)(uintptr_t)A1);
    code(0x1000,f,sizeof(f));
    BYTE h[]={0xff,0x74,0x24,4, 0xe8,0,0,0,0, 0x83,0xc4,4, 0xc3};
    put32(h+5,0x1000-(0x1100+5+9)); code(0x1100,h,sizeof(h));
    BYTE g[]={0x8b,0x0d,0,0,0,0, 0x33,0xd2, 0x33,0xc0, 0x85,0xc9, 0x74,0x10,
              0x8b,0x44,0x24,4, 0x01,0x81,0,1,0,0, 0x8b,0x81,0,1,0,0, 0xc3};
    put32(g+2,(uint32_t)(uintptr_t)HEADER); code(0x1200,g,sizeof(g));
    BYTE r[]={0x8b,0x44,0x24,4, 0x85,0xc0, 0x78,0x12, 0x0d,0,0,0,0x80, 0x50, 0xe8,0,0,0,0, 0x83,0xc4,4,
              0x8b,0x44,0x24,4, 0x01,0x05,0,0,0,0, 0xa1,0,0,0,0, 0x33,0xd2, 0xc3};
    put32(r+15,0x1300-(0x1300+5+19)); put32(r+28,(uint32_t)(uintptr_t)A1); put32(r+33,(uint32_t)(uintptr_t)A1);
    code(0x1300,r,sizeof(r));

    /* 1. A fixed 128 KiB region (eight times the span budget): the original
     * runs once on real state, the replacement in place on the restored
     * pre-state; equal calls compare clean and leave one application. */
    const es2_hook_snapshot fixed={REGION_RVA,REGION_SIZE,NULL,NULL};
    es2_hook_snapshot_set set={&fixed,1,NULL,0,0,0};
    es2_hook_descriptor fd=desc(0x1000,legacy); es2_hook_extension fe; make(&fe,f_replacement,&set);
    es2_hook_options co=options(ES2_HOOK_COMPARE);
    es2_hook fh={0}; reset_state(); kind=0;
    CHECK(es2_hook_install_ex(&fh,&fd,&co,&fe)); CHECK(fh.snapshot_memory && fh.snapshot_level_bytes>=2*REGION_SIZE);
    for (uint32_t i=1;i<=50;++i) {
        es2_hook_result x=invoke(0x1000,i); CHECK(x.eax==i*(i+1)/2 && x.edx==0);
        CHECK(word(A1)==i*(i+1)/2 && word(A2)==i*(i+1)/2);
    }
    CHECK(fh.calls==50 && fh.snapshot_compared==50 && !fh.mismatches && !fh.skipped && !fh.bypassed);
    /* 2. Deliberate differences: a state byte 8010h into the region (logged
     * as span 0 = no spans, no extension regions, snapshot region 0), and
     * EDX. The caller keeps the original's pair and state. */
    WCHAR log_path[MAX_PATH], dir[MAX_PATH]; CHECK(GetTempPathW(MAX_PATH,dir));
    swprintf(log_path,MAX_PATH,L"%lssnapshot-%lu-%lu.jsonl",dir,(unsigned long)GetCurrentProcessId(),(unsigned long)GetTickCount());
    es2_hook_log log={0}; CHECK(es2_hook_log_open(&log,log_path,8,NULL,NULL)); fh.options.log=&log;
    for (kind=1;kind<=2;++kind) {
        uint32_t before=word(A1); es2_hook_result x=invoke(0x1000,7);
        CHECK(x.eax==before+7 && x.edx==0 && word(A1)==before+7 && word(A2)==before+7);
    }
    CHECK(fh.mismatches==2 && fh.snapshot_compared==52 && !fh.skipped);
    es2_hook_log_close(&log); fh.options.log=NULL;
    { HANDLE file=CreateFileW(log_path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL); CHECK(file!=INVALID_HANDLE_VALUE);
      char text[2048]={0}; DWORD got=0; CHECK(ReadFile(file,text,sizeof(text)-1,&got,NULL)); CloseHandle(file); DeleteFileW(log_path);
      char *second=strstr(text,"\r\n"); CHECK(second);
      CHECK(strstr(text,"\"first_diff_span\":0,\"first_diff_offset\":32784") && strstr(text,"\"first_diff_span\":0,\"first_diff_offset\":32784")<second);
      CHECK(strstr(second,"\"first_diff_span\":null")); }
    CHECK(es2_hook_uninstall(&fh)); CHECK(!fh.snapshot_memory && !fh.code);

    /* 3. Sampling: every third eligible call is compared, the others run the
     * original only and count as skipped. */
    set.every=3; memset(&fh,0,sizeof(fh)); reset_state(); kind=1;
    CHECK(es2_hook_install_ex(&fh,&fd,&co,&fe));
    for (uint32_t i=1;i<=9;++i) CHECK(invoke(0x1000,1).eax==i && word(A2)==i);
    CHECK(fh.snapshot_compared==3 && fh.snapshot_unsampled==6 && fh.skipped==6 && fh.mismatches==3);
    CHECK(es2_hook_uninstall(&fh)); set.every=0; kind=0;

    /* 4. Nesting: H's original calls F through F's site; both compare with
     * snapshots of the same region. H's original phase bypasses F, its
     * replacement phase compares F one level deeper (its own buffers). */
    es2_hook hh={0}; es2_hook_descriptor hd=desc(0x1100,legacy); es2_hook_extension he; make(&he,h_replacement,&set);
    memset(&fh,0,sizeof(fh)); reset_state();
    CHECK(es2_hook_install_ex(&fh,&fd,&co,&fe)); CHECK(es2_hook_install_ex(&hh,&hd,&co,&he));
    for (uint32_t i=1;i<=20;++i) CHECK(invoke(0x1100,2).eax==2*i && word(A1)==2*i && word(A2)==2*i);
    CHECK(hh.snapshot_compared==20 && !hh.mismatches && !hh.skipped);
    CHECK(fh.calls==40 && fh.bypassed==20 && fh.snapshot_compared==20 && !fh.mismatches && !fh.skipped);
    kind=1; CHECK(invoke(0x1100,2).eax==42 && word(A2)==42);
    CHECK(fh.mismatches==1 && !hh.mismatches); kind=0;
    /* 5. Guards: F is H's guard. F installed (compare) and entered in H's
     * original phase: H skips its replacement phase (guarded). */
    CHECK(es2_hook_uninstall(&hh)); const uint32_t guard_f[]={0x1000}, guard_g[]={0x1200};
    es2_hook_snapshot_set guarded={&fixed,1,guard_f,1,0,0}; make(&he,h_replacement,&guarded); memset(&hh,0,sizeof(hh));
    CHECK(es2_hook_install_ex(&hh,&hd,&co,&he)); uint64_t f_calls=fh.calls;
    for (uint32_t i=1;i<=5;++i) CHECK(invoke(0x1100,1).eax==42+i && word(A1)==42+i && word(A2)==42+i);
    CHECK(hh.skipped==5 && hh.snapshot_guarded==5 && !hh.snapshot_compared && !hh.mismatches);
    CHECK(fh.calls==f_calls+5 && fh.bypassed==26);
    CHECK(es2_hook_uninstall(&hh)); CHECK(es2_hook_uninstall(&fh));
    /* A guard site without an installed hook refuses every compare. */
    memset(&hh,0,sizeof(hh)); CHECK(es2_hook_install_ex(&hh,&hd,&co,&he));
    CHECK(invoke(0x1100,1).eax==48 && word(A2)==48); CHECK(hh.snapshot_refused==1 && hh.skipped==1);
    CHECK(es2_hook_uninstall(&hh));
    /* An installed guard (replaced mode) that the call never enters. */
    es2_hook gh={0}; es2_hook_descriptor gd=desc(0x1200,legacy); es2_hook_extension ge; make(&ge,g_replacement,NULL);
    es2_hook_options ro=options(ES2_HOOK_REPLACED); CHECK(es2_hook_install_ex(&gh,&gd,&ro,&ge));
    guarded.guard_rvas=guard_g; memset(&hh,0,sizeof(hh)); CHECK(es2_hook_install_ex(&hh,&hd,&co,&he));
    CHECK(invoke(0x1100,1).eax==49); CHECK(hh.snapshot_compared==1 && !hh.skipped && !hh.mismatches);
    CHECK(es2_hook_uninstall(&hh)); CHECK(es2_hook_uninstall(&gh));

    /* 6. Exhaustion: R re-enters itself through its replacement phase. One
     * level: the inner compare is skipped (exhausted), not a crash; two
     * levels: both compare. */
    es2_hook rh={0}; es2_hook_descriptor rd=desc(0x1300,legacy); es2_hook_extension re; make(&re,r_replacement,&set);
    for (unsigned levels=1;levels<=2;++levels) {
        set.levels=levels; memset(&rh,0,sizeof(rh)); reset_state();
        CHECK(es2_hook_install_ex(&rh,&rd,&co,&re)); CHECK(rh.snapshot_level_bytes*levels<=REGION_SIZE*2*levels+0x1000);
        for (uint32_t i=1;i<=10;++i) { CHECK(invoke(0x1300,3).eax==i*(3u+0x80000003u)); CHECK(word(A1)==i*(3u+0x80000003u)); }
        CHECK(rh.calls==30 && rh.bypassed==10 && !rh.mismatches);
        CHECK(levels==1 ? rh.snapshot_compared==10 && rh.snapshot_exhausted==10 && rh.skipped==10
                        : rh.snapshot_compared==20 && !rh.snapshot_exhausted && !rh.skipped);
        CHECK(!rh.snapshot_active); CHECK(es2_hook_uninstall(&rh));
    }
    set.levels=0;

    /* 7. A resolved region: the block of a pool header (pointer and size in
     * the image). Absent (null pointer), too large and moved by the original
     * are handled without touching the block. */
    const es2_hook_snapshot pooled={0,0x8000,pool_resolve,NULL};
    es2_hook_snapshot_set pool_set={&pooled,1,NULL,0,0,0};
    es2_hook gc={0}; make(&ge,g_replacement,&pool_set); put32(HEADER,(uint32_t)(uintptr_t)pool); put32(HEADER+4,0x400);
    CHECK(es2_hook_install_ex(&gc,&gd,&co,&ge)); put32(pool+0x100,5);
    kind=0; CHECK(invoke(0x1200,3).eax==8 && word(pool+0x100)==8); CHECK(gc.snapshot_compared==1 && !gc.mismatches);
    kind=1; CHECK(invoke(0x1200,3).eax==11 && word(pool+0x100)==11); CHECK(gc.mismatches==1); kind=0;
    put32(HEADER,0); CHECK(invoke(0x1200,3).eax==0); CHECK(gc.snapshot_compared==3 && gc.mismatches==1);
    put32(HEADER,(uint32_t)(uintptr_t)pool); put32(HEADER+4,0x8001);
    CHECK(invoke(0x1200,1).eax==12 && word(pool+0x100)==12); CHECK(gc.snapshot_refused==1 && gc.skipped==1);
    put32(HEADER+4,0x400); moved=1; resolves=0;
    CHECK(invoke(0x1200,1).eax==13 && word(pool+0x100)==13); CHECK(gc.snapshot_refused==2 && gc.skipped==2);
    moved=0; CHECK(es2_hook_uninstall(&gc));
    /* Overlapping snapshot regions are refused per call. */
    es2_hook_snapshot two[]={{REGION_RVA,0x1000,NULL,NULL},{REGION_RVA+0x800,0x1000,NULL,NULL}};
    es2_hook_snapshot_set overlap={two,2,NULL,0,0,0}; make(&fe,f_replacement,&overlap); memset(&fh,0,sizeof(fh));
    CHECK(es2_hook_install_ex(&fh,&fd,&co,&fe)); reset_state();
    CHECK(invoke(0x1000,4).eax==4 && word(A2)==4); CHECK(fh.snapshot_refused==1 && fh.skipped==1);
    CHECK(es2_hook_uninstall(&fh));

    /* 8. Bindings: a null extension and a full table are refused; an
     * unbinding frees its slot. Then refused declarations, and no buffers
     * outside compare mode. */
    CHECK(!es2_hook_bind_snapshot(NULL,&set));
    static es2_hook_extension many[ES2_HOOK_MAX_SNAPSHOT_BINDINGS+1]; unsigned bound=0;
    for (unsigned i=0;i<=ES2_HOOK_MAX_SNAPSHOT_BINDINGS;++i) bound+=es2_hook_bind_snapshot(many+i,&set)?1u:0u;
    CHECK(bound>0 && bound<ES2_HOOK_MAX_SNAPSHOT_BINDINGS && !es2_hook_bind_snapshot(many+ES2_HOOK_MAX_SNAPSHOT_BINDINGS,&set));
    for (unsigned i=0;i<=ES2_HOOK_MAX_SNAPSHOT_BINDINGS;++i) CHECK(es2_hook_bind_snapshot(many+i,NULL));
    CHECK(es2_hook_bind_snapshot(many+ES2_HOOK_MAX_SNAPSHOT_BINDINGS,&set) && es2_hook_bind_snapshot(many+ES2_HOOK_MAX_SNAPSHOT_BINDINGS,NULL));
    es2_hook_snapshot bad=fixed; es2_hook_snapshot_set bad_set={&bad,1,NULL,0,0,0}; make(&fe,f_replacement,&bad_set);
    bad.size=0; CHECK(!es2_hook_install_ex(&fh,&fd,&co,&fe) && fh.error==ES2_HOOK_ERROR_ARGUMENT);
    bad.size=IMAGE_SIZE; CHECK(!es2_hook_install_ex(&fh,&fd,&co,&fe));
    bad.size=ES2_HOOK_MAX_SNAPSHOT_BYTES+1; bad.resolve=pool_resolve; CHECK(!es2_hook_install_ex(&fh,&fd,&co,&fe));
    bad=fixed; bad_set.levels=ES2_HOOK_MAX_SNAPSHOT_LEVELS+1; CHECK(!es2_hook_install_ex(&fh,&fd,&co,&fe));
    bad_set.levels=0; bad_set.region_count=0; CHECK(!es2_hook_install_ex(&fh,&fd,&co,&fe));
    bad_set.region_count=1; bad_set.guard_count=1; CHECK(!es2_hook_install_ex(&fh,&fd,&co,&fe));
    uint32_t far_guard=IMAGE_SIZE-4; bad_set.guard_rvas=&far_guard; CHECK(!es2_hook_install_ex(&fh,&fd,&co,&fe));
    CHECK(!memcmp(image+0x1000,signature,5) && !fh.snapshot_memory);
    bad_set.guard_count=0; memset(&fh,0,sizeof(fh)); CHECK(es2_hook_install_ex(&fh,&fd,&ro,&fe)); CHECK(!fh.snapshot_memory);
    reset_state(); CHECK(invoke(0x1000,6).eax==6 && word(A2)==6 && !fh.snapshot_compared); CHECK(es2_hook_uninstall(&fh));

    /* 9. The extended compare buffers are off the stack: the replacement of
     * a compare call without snapshot runs within 16 KiB of the caller. */
    make(&fe,probe_replacement,NULL); memset(&fh,0,sizeof(fh)); CHECK(es2_hook_install_ex(&fh,&fd,&co,&fe));
    volatile BYTE marker=0; uintptr_t top=(uintptr_t)&marker; deepest=0; reset_state();
    CHECK(invoke(0x1000,1).eax==1 && word(A1)==1); CHECK(fh.calls==1 && !fh.mismatches && !fh.skipped);
    CHECK(deepest && top-deepest<0x4000); CHECK(es2_hook_uninstall(&fh));
    /* 10. Cost (printed, not checked): a compared call with the 128 KiB
     * region against a replaced call of the same function. */
    double per_call[2]={0,0}; LARGE_INTEGER frequency,start,stop; QueryPerformanceFrequency(&frequency);
    for (unsigned mode=0;mode<2;++mode) {
        make(&fe,f_replacement,&set); memset(&fh,0,sizeof(fh)); es2_hook_options to=options(mode?ES2_HOOK_COMPARE:ES2_HOOK_REPLACED);
        CHECK(es2_hook_install_ex(&fh,&fd,&to,&fe)); reset_state(); kind=0;
        QueryPerformanceCounter(&start); for (unsigned i=0;i<2000;++i) (void)invoke(0x1000,1); QueryPerformanceCounter(&stop);
        per_call[mode]=(double)(stop.QuadPart-start.QuadPart)*1e6/(double)frequency.QuadPart/2000.0;
        CHECK(word(A2)==2000 && !fh.mismatches); CHECK(es2_hook_uninstall(&fh));
    }
    printf("COST: replaced %.2f us, compared with a %u-byte snapshot region %.2f us per call\n",per_call[0],REGION_SIZE,per_call[1]);

    /* 11. The table of installed hooks: ES2_HOOK_MAX_INSTALLED replaced
     * installs (one site each, 8 bytes apart above the region) fill it. The
     * next replaced or compare install fails with TABLE_FULL before it
     * allocates or patches anything; an uninstall frees an entry for it. */
    static es2_hook table[ES2_HOOK_MAX_INSTALLED+1]; static es2_hook_descriptor table_d[ES2_HOOK_MAX_INSTALLED+1];
    for (unsigned i=0;i<=ES2_HOOK_MAX_INSTALLED;++i) { memcpy(image+0x30000u+8u*i,signature,5); table_d[i]=desc(0x30000u+8u*i,legacy); }
    unsigned filled=0;
    for (unsigned i=0;i<ES2_HOOK_MAX_INSTALLED;++i) filled+=es2_hook_install(table+i,table_d+i,&ro)?1u:0u;
    CHECK(filled==ES2_HOOK_MAX_INSTALLED);
    es2_hook *last=table+ES2_HOOK_MAX_INSTALLED; const BYTE *last_site=image+0x30000u+8u*ES2_HOOK_MAX_INSTALLED;
    CHECK(!es2_hook_install(last,table_d+ES2_HOOK_MAX_INSTALLED,&ro) && last->error==ES2_HOOK_ERROR_TABLE_FULL);
    CHECK(!last->installed && !last->code && !memcmp(last_site,signature,5));
    make(&fe,f_replacement,&set); memset(&fh,0,sizeof(fh));
    CHECK(!es2_hook_install_ex(&fh,&fd,&co,&fe) && fh.error==ES2_HOOK_ERROR_TABLE_FULL);
    CHECK(!fh.installed && !fh.code && !fh.snapshot_memory && !memcmp(image+0x1000,signature,5));
    CHECK(es2_hook_uninstall(table+7)); CHECK(!memcmp(image+0x30000u+8u*7u,signature,5));
    CHECK(es2_hook_install(last,table_d+ES2_HOOK_MAX_INSTALLED,&ro) && last->installed==1 && last_site[0]==0xe9);
    for (unsigned i=0;i<=ES2_HOOK_MAX_INSTALLED;++i) if (i!=7) CHECK(es2_hook_uninstall(table+i));
    for (unsigned i=0;i<=ES2_HOOK_MAX_INSTALLED;++i) CHECK(!memcmp(image+0x30000u+8u*i,signature,5) && !table[i].code);
    memset(&fh,0,sizeof(fh)); CHECK(es2_hook_install_ex(&fh,&fd,&co,&fe)); CHECK(es2_hook_uninstall(&fh));
    printf("PASS: snapshot compare %u checks; deepest compare replacement %lu bytes below the caller\n",
        checks,(unsigned long)(top-deepest));
    VirtualFree(pool,0,MEM_RELEASE); VirtualFree(image,0,MEM_RELEASE); return 0;
}
