/* Own input slots, exit imports and dispatcher. No original executable loads. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
static WCHAR config_path[MAX_PATH];
static HMODULE self_module;
static void log_event(const char *s) { (void)s; }
#include "../oracle_perf.h"
#include "../virtual_time.h"
#include "../input_journal.h"
#include "../rng_hooks.h"
#include "../live_coverage.h"
#include "../tick_log.h"
#include "../oracle_perf_runtime.h"
static unsigned dispatched;
void __cdecl __real_es2_hook_dispatch_extended(void *frame) { ++dispatched; ++*(unsigned *)frame; }
static void (__cdecl *registered)(void);
static int exit_status,registration_error;
static void __cdecl fake_exit(int code) { exit_status=code; }
static int __cdecl fake_atexit(void (__cdecl *callback)(void)) { registered=callback;return registration_error; }
static SHORT WINAPI fake_key(int key) { SetLastError(719);return (SHORT)(key+100); }
#define CHECK(c) do {if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);return 1;}}while(0)
int main(void) {
    unsigned frame=0;__wrap_es2_hook_dispatch_extended(&frame);CHECK(frame==1&&dispatched==1&&!perf_enabled);
    WCHAR root[MAX_PATH],report[MAX_PATH];CHECK(GetEnvironmentVariableW(L"TEST_TMPDIR",root,MAX_PATH));
    swprintf(config_path,MAX_PATH,L"%ls\\runtime.ini",root);swprintf(report,MAX_PATH,L"%ls\\runtime.json",root);
    CHECK(WritePrivateProfileStringW(L"perf",L"enabled",L"1",config_path));
    CHECK(WritePrivateProfileStringW(L"perf",L"report",report,config_path));CHECK(perf_configure(config_path));
    perf_tick(0);perf_dispatch_target=perf_dispatch_measured;
    __wrap_es2_hook_dispatch_extended(&frame);CHECK(frame==2&&dispatched==2&&perf_counts[PERF_HOOK]==1);
    ij_image=VirtualAlloc(NULL,IJ_IMAGE_SIZE,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);CHECK(ij_image);
    for(unsigned i=0;i<5;++i) *(DWORD *)(ij_image+ij_slots[i].iat_rva)=(DWORD)(uintptr_t)ij_slots[i].hook;
    ij_iat_state=1;ij_real_key=fake_key;CHECK(perf_input_slots());
    IjKeyState key=(void *)(uintptr_t)*(DWORD *)(ij_image+ij_slots[1].iat_rva);
    CHECK(key(10)==110 && GetLastError()==719 && perf_counts[PERF_INPUT]==1);
    CHECK(!perf_input_slots()); /* refuses foreign/already changed pointers */
    DWORD *slots=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);CHECK(slots);
    slots[0]=(DWORD)(uintptr_t)fake_exit;slots[1]=(DWORD)(uintptr_t)fake_atexit;
    registration_error=1;CHECK(!speed_install_exit_slots(slots,slots+1) && slots[0]==(DWORD)(uintptr_t)fake_exit);
    registration_error=0;CHECK(speed_install_exit_slots(slots,slots+1));CHECK(registered==speed_on_exit);
    ((void (__cdecl *)(int))(uintptr_t)slots[0])(73);CHECK(exit_status==73 && perf_complete);
    CHECK(perf_report("runner_flush",FALSE));
    HANDLE f=CreateFileW(report,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);CHECK(f!=INVALID_HANDLE_VALUE);
    char data[4096]={0};DWORD got;CHECK(ReadFile(f,data,sizeof(data)-1,&got,NULL));CloseHandle(f);
    CHECK(strstr(data,"\"reason\":\"exit\"") && strstr(data,"\"complete\":true"));
    registered(); /* repeated exit is idempotent */
    /* Coverage owns its slot; speed chains only the verified previous target. */
    live_enabled=TRUE;live_complete=TRUE;live_previous_exit=fake_exit;
    slots[0]=123;CHECK(speed_install_exit_slots(NULL,slots+1));CHECK(slots[0]==123&&live_previous_exit==speed_exit);
    live_previous_exit(91);CHECK(exit_status==91);
    puts("PASS dispatcher, input results/LastError, runtime exit, coverage coexistence, final-report preservation");
    return 0;
}
