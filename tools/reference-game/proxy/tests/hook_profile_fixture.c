/* Deterministic clock: prove exclusive per-hook attribution and bounded output. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static LONGLONG clock_value;
static BOOL WINAPI clock_read(LARGE_INTEGER *value) {value->QuadPart=clock_value;return TRUE;}
#define QueryPerformanceCounter clock_read
#include "../oracle_perf.h"
#undef QueryPerformanceCounter
#define CHECK(c) do {if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);return 1;}}while(0)
static perf_hook_entry *find(const void *identity) {
    for(unsigned i=0;i<PERF_HOOK_SLOTS;++i)if(perf_hooks[i].identity==identity)return &perf_hooks[i];return NULL;
}
int main(void) {
    static int identity[PERF_HOOK_SLOTS+1];static char names[PERF_HOOK_SLOTS+1][16];
    CHECK(perf_hook_begin(identity,"absent")==UINT_MAX&&!find(identity));perf_hook_end(UINT_MAX);
    InitializeCriticalSection(&perf_lock);perf_lock_ready=TRUE;perf_enabled=TRUE;perf_started=TRUE;
    perf_thread=GetCurrentThreadId();perf_frequency.QuadPart=10000000;perf_hook_names=TRUE;
    clock_value=10;unsigned a=perf_hook_begin(identity,"outer");
    clock_value=30;unsigned b=perf_hook_begin(identity+1,"inner");
    clock_value=50;unsigned input=perf_begin(PERF_INPUT);
    clock_value=60;perf_end(input);
    clock_value=80;perf_hook_end(b);
    clock_value=90;perf_hook_end(a);
    CHECK(find(identity)->qpc==30&&find(identity+1)->qpc==40);
    CHECK(find(identity)->count==1&&find(identity+1)->count==1);
    CHECK(perf_totals[PERF_HOOK]==70&&perf_totals[PERF_INPUT]==10&&perf_totals[PERF_OTHER]==10&&!perf_depth&&perf_current_hook==UINT_MAX);
    perf_started=FALSE;perf_tick(0);CHECK(!find(identity)->count&&!find(identity)->qpc&&!perf_totals[PERF_HOOK]);
    memset(perf_hooks,0,sizeof(perf_hooks));
    for(unsigned i=0;i<=PERF_HOOK_SLOTS;++i) {
        snprintf(names[i],sizeof(names[i]),"hook_%03u",i);
        unsigned token=perf_hook_begin(identity+i,names[i]);clock_value+=i+1;perf_hook_end(token);
    }
    CHECK(perf_hook_overflow==1&&perf_counts[PERF_HOOK]==PERF_HOOK_SLOTS+1);
    WCHAR root[MAX_PATH];CHECK(GetEnvironmentVariableW(L"TEST_TMPDIR",root,MAX_PATH));
    swprintf(perf_path,MAX_PATH,L"%ls\\hook-profile.json",root);swprintf(perf_temp,MAX_PATH,L"%ls.tmp",perf_path);
    CHECK(perf_report("exit",TRUE));
    FILE *f=_wfopen(perf_path,L"rb");CHECK(f);char text[16384];size_t size=fread(text,1,sizeof(text)-1,f);fclose(f);text[size]=0;
    CHECK(strstr(text,"\"hook_overflow\":1")&&strstr(text,"\"hooks_top20\":["));
    CHECK(strstr(text,"hook_255")<strstr(text,"hook_254")&&!strstr(text,"hook_235"));
    unsigned count=0;for(char *p=text;(p=strstr(p,"\"name\""));p+=6)++count;CHECK(count==20);
    CHECK(!strcmp(text+size-3,"]}\n"));
    DeleteFileW(perf_path);DeleteFileW(perf_temp);DeleteCriticalSection(&perf_lock);
    puts("PASS hook profile: disabled, nested exclusive 30/40 QPC, input exclusion, startup reset, 256-slot overflow, top 20 sorted");return 0;
}
