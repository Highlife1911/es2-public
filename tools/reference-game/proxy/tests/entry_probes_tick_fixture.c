/* Synthetic ticks and COM objects only. No original code or game is loaded. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static WCHAR config_path[MAX_PATH];
static void log_event(const char *s) { (void)s; }
#include "../oracle_perf.h"
#include "../rng_hooks.h"
#include "../live_coverage.h"
#include "../entry_probes.h"
#include "hook/tests/counter_candidate.h"
#include "../ddraw_observer.h"
static char sink[16*1024*1024], expected[16*1024*1024];
static char counter_expected[16*1024*1024];
static DWORD sink_size, calls, short_write;
static BOOL fail_write;
static BOOL WINAPI fixed_qpc(LARGE_INTEGER *n) { n->QuadPart=123456789; return TRUE; }
static BOOL WINAPI capture_write(HANDLE f,const void *data,DWORD n,DWORD *done,OVERLAPPED *o) {
    (void)f;(void)o;++calls;
    if (fail_write) { *done=0;return FALSE; }
    if (short_write && n>short_write) n=short_write;
    if (n>=sizeof(sink)-sink_size) { *done=0;return FALSE; }
    memcpy(sink+sink_size,data,n);sink_size+=n;sink[sink_size]=0;*done=n;return TRUE;
}
#define QueryPerformanceCounter fixed_qpc
#define WriteFile capture_write
#include "../tick_log.h"
#undef QueryPerformanceCounter
#undef WriteFile
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); return 1; } } while(0)
static BYTE *image, *context;
static BYTE watched[600];
static void reset_ticks(void) {
    sink_size=0;sink[0]=0;calls=0;tick_log_used=0;tick_log_sequence=0;tick_io_failed=FALSE;
    tick_log_state=1;tick_log_limit=1000000;tick_log_file=(HANDLE)1;
    memset(watched,0,sizeof(watched));
    for (unsigned i=0;i<mem_watch_count;++i) { mem_watches[i].previous_length=0;mem_watches[i].previous_unreadable=FALSE; }
}
static void record(unsigned frame) {
    memcpy(context+0xbf,&frame,4);watched[frame%sizeof(watched)]++;
    es2_tick_record(context);
}
static int tick_fixture(void) {
    image=VirtualAlloc(NULL,TICK_LOG_IMAGE_SIZE,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);CHECK(image);
    context=image+TICK_LOG_CTX_RVA;tick_log_image=image;
    for (unsigned i=0;i<TICK_LOG_RNG_SIZE;++i) image[TICK_LOG_RNG_A_RVA+i]=(BYTE)i;
    char spec[128];snprintf(spec,sizeof(spec),"bank 0x%lx 600",(unsigned long)(uintptr_t)watched);CHECK(mem_watch_add(spec));
    rng_hooks_configured=TRUE;
    for (unsigned i=0;i<FUNCTION_HOOK_COUNT;++i) rng_hooks[i].options.mode=ES2_HOOK_REPLACED;
    reset_ticks();for(unsigned i=0;i<48;++i)record(i);
    DWORD length=sink_size,immediate_calls=calls;memcpy(expected,sink,length);
    CHECK(immediate_calls==48 && length>131072 && !tick_io_failed);   /* the sink holds all 48 records */
    WCHAR root[MAX_PATH]; CHECK(GetEnvironmentVariableW(L"TEST_TMPDIR",root,MAX_PATH));
    swprintf(config_path,MAX_PATH,L"%ls\\fast-off.ini",root);
    CHECK(WritePrivateProfileStringW(L"hooks",L"fast_path",L"0",config_path));
    CHECK(es2_hook_configure_fast_path(config_path));
    CHECK(probe_configuration(config_path)==0 && !ep_active && !ep_handler && !live_extra_tick);
    reset_ticks();for(unsigned i=0;i<48;++i)record(i);
    CHECK(length==sink_size && !memcmp(expected,sink,length));
    es2_hook_set_fast_path(TRUE);DeleteFileW(config_path);
    printf("PASS absent probes + fast_path=0: %lu identical raw tick/watch bytes\n",(unsigned long)length);
    DWORD counter_length=0;
    for(unsigned candidate=0;candidate<2;++candidate) {
        reset_ticks();
        for(unsigned i=0;i<FUNCTION_HOOK_COUNT;++i)rng_hooks[i].calls=UINT64_C(0x12345678fffffff0)-i;
        for(unsigned frame=0;frame<48;++frame) {
            for(unsigned i=0;i<FUNCTION_HOOK_COUNT;++i)for(unsigned j=0;j<=i%7;++j) {
                if(candidate)counter_candidate(&rng_hooks[i].calls);else ++rng_hooks[i].calls;
            }
            record(frame);
        }
        if(!candidate){counter_length=sink_size;memcpy(counter_expected,sink,sink_size);}
        else CHECK(counter_length==sink_size&&!memcmp(counter_expected,sink,sink_size));
    }
    printf("PASS B1 carry candidate: %lu identical raw all-hook tick/watch bytes\n",(unsigned long)counter_length);
    for(unsigned i=0;i<FUNCTION_HOOK_COUNT;++i)rng_hooks[i].calls=0;
    InitializeCriticalSection(&perf_lock);perf_lock_ready=TRUE;perf_thread=GetCurrentThreadId();
    QueryPerformanceFrequency(&perf_frequency);perf_enabled=TRUE;perf_last=perf_now();
    reset_ticks();for(unsigned i=0;i<48;++i)record(i);
    CHECK(length==sink_size && !memcmp(expected,sink,length));
    CHECK(perf_counts[PERF_FORMAT] && perf_counts[PERF_WATCH] && perf_counts[PERF_WRITE]);
    /* Manual buffering proves capacity, target, explicit flush, partial writes
     * and errors without depending on a wall-clock scheduler. */
    perf_enabled=FALSE;tick_io_buffered=TRUE;tick_io_target=48;
    reset_ticks();for(unsigned i=0;i<48;++i)record(i);
    CHECK(length==sink_size && !memcmp(expected,sink,length) && !tick_log_used);
    CHECK(calls<immediate_calls);
    printf("48 all-hook ticks + watches: %lu bytes; writes %lu -> %lu\n",(unsigned long)length,(unsigned long)immediate_calls,(unsigned long)calls);
    tick_io_target=0;reset_ticks();for(unsigned i=0;i<48;++i)record(i);
    CHECK(tick_log_used);tick_log_flush();CHECK(length==sink_size && !memcmp(expected,sink,length));
    short_write=31;reset_ticks();for(unsigned i=0;i<48;++i)record(i);tick_log_flush();
    CHECK(length==sink_size && !memcmp(expected,sink,length));short_write=0;
    reset_ticks();record(0);DWORD pending=tick_log_used;fail_write=TRUE;tick_log_flush();
    CHECK(tick_io_failed && tick_log_used==pending && !sink_size);fail_write=FALSE;tick_log_flush();CHECK(sink_size==pending);
    tick_io_buffered=FALSE;reset_ticks();tick_log_limit=2;record(0);record(1);record(2);
    length=sink_size;memcpy(expected,sink,length);
    tick_io_buffered=TRUE;reset_ticks();tick_log_limit=2;record(0);record(1);record(2);
    CHECK(sink_size==length && !memcmp(expected,sink,length) && !tick_log_used && tick_log_state==3);
    /* A real own worker drains a stalled logger within the bounded interval,
     * and acknowledgement follows all pending tick/watch bytes. */
    reset_ticks();tick_io_buffered=FALSE;CHECK(tick_io_start(0,25));
    /* Poll instead of one fixed 100 ms sleep: under parallel builds the
     * 25 ms worker was sometimes not scheduled in time. */
    record(0);
    {int drained=0;for(int waited=0;!drained && waited<5000;waited+=25){Sleep(25);tick_io_enter();drained=!tick_log_used && sink_size;tick_io_leave();}
     CHECK(drained);}
    record(1);CHECK(SetEvent(tick_io_request));CHECK(WaitForSingleObject(tick_io_ack,3000)==WAIT_OBJECT_0);
    tick_io_enter();CHECK(!tick_log_used);tick_io_leave();
    record(2);tick_log_shutdown();CHECK(!tick_log_used);
    CHECK(WaitForSingleObject(tick_io_worker,3000)==WAIT_OBJECT_0);
    CHECK(strstr(sink,"\"sequence\":3,")!=NULL);
    return 0;
}
static int compact_fixture(void) {
    image=VirtualAlloc(NULL,TICK_LOG_IMAGE_SIZE,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);CHECK(image);
    context=image+TICK_LOG_CTX_RVA;tick_log_image=image;
    reset_ticks();record(0);DWORD ordinary=sink_size;memcpy(expected,sink,ordinary);
    tick_log_compact_rng=TRUE;tick_log_rng_saved=FALSE;
    reset_ticks();record(0);
    CHECK(sink_size==ordinary+8 && !strncmp(sink,"{\"event\":\"tick_compact\"",23));
    CHECK(!memcmp(sink+22,expected+14,ordinary-14)); /* same bytes after event */
    reset_ticks();record(0);
    CHECK(strstr(sink,"\"rng_a_same\":true") && strstr(sink,"\"rng_b_same\":true"));
    CHECK(!strstr(sink,"rng_a_hex") && sink_size<ordinary);
    image[TICK_LOG_RNG_A_RVA]=0xa5;
    reset_ticks();record(0);
    CHECK(strstr(sink,"\"rng_a_hex\":\"a5") && strstr(sink,"\"rng_b_same\":true"));
    tick_log_compact_rng=FALSE;
    reset_ticks();record(0);
    CHECK(strstr(sink,"\"event\":\"tick\"") && strstr(sink,"\"rng_b_hex\":") && !strstr(sink,"_same"));
    puts("PASS compact RNG: full initial/change states, unchanged references, ordinary default");
    return 0;
}
static unsigned flips,locks,content;
static HRESULT flip_status=DD_OK, lost_status=DD_OK;
static uintptr_t WINAPI fake_flip(void *s,uintptr_t target,uintptr_t flags) {
    (void)s;if(target || flags!=DDFLIP_WAIT)return DDERR_INVALIDPARAMS;
    ++flips;if(SUCCEEDED(flip_status))content^=1;SetLastError(123);return (uintptr_t)flip_status;
}
static uintptr_t WINAPI fake_caps(void *s,uintptr_t c) { (void)s;((DDSCAPS *)c)->dwCaps=DDSCAPS_PRIMARYSURFACE|DDSCAPS_FLIP;return DD_OK; }
static uintptr_t WINAPI fake_lost(void *s) { (void)s;return (uintptr_t)lost_status; }
static uintptr_t WINAPI fake_status(void *s,uintptr_t f) { (void)s;(void)f;return (uintptr_t)lost_status; }
static uintptr_t WINAPI fake_lock(void *s,uintptr_t r,uintptr_t d,uintptr_t f,uintptr_t e) {
    (void)s;(void)r;(void)f;(void)e;++locks;*(unsigned *)d=content;return DD_OK;
}
static uintptr_t WINAPI fake_release(void *s) { (void)s;return 0; }
static uintptr_t WINAPI fake_blt(void *s,uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d,uintptr_t e) {
    (void)s;return a+2*b+3*c+4*d+5*e;
}
static int draw_fixture(void) {
    void *table[36]={0};table[11]=fake_flip;table[14]=fake_caps;table[18]=fake_status;
    table[24]=fake_lost;table[25]=fake_lock;table[2]=fake_release;table[5]=fake_blt;
    void **object=table;CHECK(dd_track(&object,1));CHECK(object!=table);
    uintptr_t a[]={0,DDFLIP_WAIT};
    CHECK(dd_m11(&object,a[0],a[1])==DD_OK && flips==1 && content==1 && GetLastError()==123);
    CHECK(dd_m5(&object,1,2,3,4,5)==55); /* arguments and stdcall cleanup */
    dd_defer_flip=TRUE;
    CHECK(dd_m11(&object,0,DDFLIP_WAIT)==DD_OK && flips==1 && dd_pending);
    unsigned readback=99;
    CHECK(dd_m25(&object,0,(uintptr_t)&readback,0,0)==DD_OK);
    CHECK(flips==2 && locks==1 && readback==0 && !dd_pending);
    /* Refuse suppression on loss/busy and forward the actual HRESULT. */
    lost_status=flip_status=DDERR_SURFACELOST;
    CHECK((HRESULT)dd_m11(&object,0,DDFLIP_WAIT)==DDERR_SURFACELOST && flips==3 && !dd_pending);
    lost_status=flip_status=DD_OK;
    CHECK(dd_m11(&object,0,DDFLIP_WAIT)==DD_OK && dd_pending);
    CHECK(dd_m2(&object)==0 && flips==4 && !dd_find(&object));
    /* A loss occurring AFTER a deferred success is explicitly invalid evidence. */
    object=table;CHECK(dd_track(&object,1));CHECK(dd_m11(&object,0,DDFLIP_WAIT)==DD_OK);
    flip_status=DDERR_SURFACELOST;dd_drain();CHECK(perf_error==3);
    puts("DirectDraw forwarding, deferred Flip, readback drain and error paths pass (synthetic only)");
    return 0;
}
static int report_fixture(void) {
    WCHAR root[MAX_PATH],ini[MAX_PATH],report[MAX_PATH];
    CHECK(GetEnvironmentVariableW(L"TEST_TMPDIR",root,MAX_PATH));
    swprintf(ini,MAX_PATH,L"%ls\\perf.ini",root);swprintf(report,MAX_PATH,L"%ls\\perf.json",root);
    CHECK(perf_configure(ini) && !perf_enabled);
    CHECK(WritePrivateProfileStringW(L"perf",L"enabled",L"1",ini));
    CHECK(WritePrivateProfileStringW(L"perf",L"report",report,ini));
    CHECK(perf_configure(ini));
    perf_tick(0);unsigned a=perf_begin(PERF_INPUT),b=perf_begin(PERF_HOOK);perf_end(b);perf_end(a);perf_tick(1);
    CHECK(perf_counts[PERF_INPUT]==1 && perf_counts[PERF_HOOK]==1 && perf_depth==0);
    CHECK(perf_report("exit",TRUE));
    HANDLE f=CreateFileW(report,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);CHECK(f!=INVALID_HANDLE_VALUE);
    char text[4096]={0};DWORD got;CHECK(ReadFile(f,text,sizeof(text)-1,&got,NULL));CloseHandle(f);
    CHECK(strstr(text,"\"complete\":true") && strstr(text,"\"reason\":\"exit\"") && strstr(text,"frame_other"));
    CHECK(!perf_configure(ini)); /* cannot replace an existing report */
    return 0;
}
static BYTE pixels[32],screen[32];
static BOOL driver_locked;
static unsigned driver_locks,driver_unlocks;
static uintptr_t WINAPI primary_caps(void *s,uintptr_t c) {(void)s;((DDSCAPS *)c)->dwCaps=DDSCAPS_PRIMARYSURFACE;return DD_OK;}
static uintptr_t WINAPI primary_lock(void *s,uintptr_t rect,uintptr_t desc,uintptr_t flags,uintptr_t event) {
    (void)s;(void)rect;(void)flags;(void)event;
    if(driver_locked)return DDERR_SURFACEBUSY;
    if(FAILED(lost_status))return (uintptr_t)lost_status;
    DDSURFACEDESC *d=(void *)desc;memset(d,0,sizeof(*d));d->dwSize=sizeof(*d);d->dwWidth=8;d->dwHeight=4;d->lPitch=8;d->lpSurface=pixels;
    driver_locked=TRUE;++driver_locks;return DD_OK;
}
static uintptr_t WINAPI primary_unlock(void *s,uintptr_t memory) {
    (void)s;(void)memory;
    if(!driver_locked)return DDERR_NOTLOCKED;
    driver_locked=FALSE;++driver_unlocks;memcpy(screen,pixels,sizeof(screen));return (uintptr_t)lost_status;
}
static uintptr_t WINAPI primary_dc(void *s,uintptr_t out) {
    (void)s;if(driver_locked)return DDERR_SURFACEBUSY;*(unsigned *)out=1;return DD_OK;
}
static int presentation_fixture(void) {
    void *table[36]={0};table[14]=primary_caps;table[24]=fake_lost;table[25]=primary_lock;
    table[32]=primary_unlock;table[17]=primary_dc;table[2]=fake_release;
    void **object=table;CHECK(dd_track(&object,1));DDSURFACEDESC d={0};d.dwSize=sizeof(d);
    dd_present_every=4;unsigned readback=0;
    for(unsigned frame=0;frame<8;++frame) {
        d.dwSize=sizeof(d);CHECK(dd_m25(&object,0,(uintptr_t)&d,DDLOCK_WAIT,0)==DD_OK);
        CHECK(d.lpSurface==pixels && d.dwWidth==8 && d.lPitch==8);
        if(frame)CHECK(((BYTE *)d.lpSurface)[0]==frame-1); /* state survives skipped presents */
        CHECK((HRESULT)dd_m25(&object,0,(uintptr_t)&d,DDLOCK_WAIT,0)==DDERR_SURFACEBUSY);
        memset(d.lpSurface,(int)frame,sizeof(pixels));CHECK(dd_m32(&object,0)==DD_OK);
        if(frame%4==3)CHECK(screen[0]==frame && !driver_locked);
        else CHECK(driver_locked);
    }
    CHECK(driver_locks==2 && driver_unlocks==2 && perf_dd_deferred==6);
    d.dwSize=sizeof(d);CHECK(dd_m25(&object,0,(uintptr_t)&d,DDLOCK_WAIT,0)==DD_OK);CHECK(dd_m32(&object,0)==DD_OK);
    CHECK(driver_locked);CHECK(dd_m17(&object,(uintptr_t)&readback)==DD_OK && readback==1 && !driver_locked);
    /* Surface loss never returns cached success, and readback uses real state. */
    d.dwSize=sizeof(d);CHECK(dd_m25(&object,0,(uintptr_t)&d,DDLOCK_WAIT,0)==DD_OK);CHECK(dd_m32(&object,0)==DD_OK);
    lost_status=DDERR_SURFACELOST;CHECK((HRESULT)dd_m25(&object,0,(uintptr_t)&d,DDLOCK_WAIT,0)==DDERR_SURFACELOST);
    CHECK(perf_error==3);lost_status=DD_OK;
    puts("PASS primary Lock/Unlock cache: 8 frames, 2 real pairs, exact memory/description, busy/lost/DC paths");
    return 0;
}
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    if(!strcmp(argv[1],"ticks"))return tick_fixture();
    if(!strcmp(argv[1],"draw"))return draw_fixture();
    if(!strcmp(argv[1],"report"))return report_fixture();
    if(!strcmp(argv[1],"presentation"))return presentation_fixture();
    if(!strcmp(argv[1],"compact"))return compact_fixture();
    return 2;
}
