/* Opt-in, integer-only QPC accounting. Totals are exclusive (nested work is
 * charged to the innermost phase). This measures wall time, not CPU time. */
#ifndef ES2_ORACLE_PERF_H
#define ES2_ORACLE_PERF_H
#include <windows.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>
enum { PERF_FORMAT, PERF_WRITE, PERF_WATCH, PERF_INPUT, PERF_HOOK, PERF_DDRAW,
       PERF_OTHER, PERF_PHASES };
static const char *const perf_names[] = {"tick_format", "tick_write", "watch_reads",
    "input_journal", "hook_dispatch", "directdraw", "frame_other"};
static BOOL perf_enabled, perf_complete, perf_started;
static DWORD perf_thread, perf_frame, perf_frame_limit, perf_error;
static uint64_t perf_totals[PERF_PHASES], perf_counts[PERF_PHASES];
static uint64_t perf_worker_write_qpc, perf_worker_write_count;
static uint64_t perf_write_calls, perf_write_bytes;
static DWORD perf_dd_deferred, perf_dd_drained, perf_dd_unobserved;
static DWORD perf_presentation_every;
static BOOL perf_defer_flip;
static const char *perf_module_sha256 = "synthetic";
static LARGE_INTEGER perf_frequency;
static LONGLONG perf_last;
static unsigned perf_phase = PERF_OTHER, perf_depth;
static WCHAR perf_path[MAX_PATH], perf_temp[MAX_PATH];
static CRITICAL_SECTION perf_lock;
static BOOL perf_lock_ready;
static void (*perf_before_complete)(void);
/* Main-thread accounting, protected by the existing profiler lock. Names
 * are borrowed from stable hook descriptors; no allocation per call. */
#define PERF_HOOK_SLOTS 256u
typedef struct perf_hook_entry {
    const void *identity;
    const char *name;
    uint64_t qpc, count;
} perf_hook_entry;
static perf_hook_entry perf_hooks[PERF_HOOK_SLOTS];
static unsigned perf_current_hook=UINT_MAX;
static uint64_t perf_hook_overflow;
static BOOL perf_hook_names;

static LONGLONG perf_now(void) { LARGE_INTEGER n; QueryPerformanceCounter(&n); return n.QuadPart; }
static void perf_charge(LONGLONG now) {
    if (perf_started && now >= perf_last) {
        uint64_t elapsed=(uint64_t)(now-perf_last);
        perf_totals[perf_phase] += elapsed;
        if(perf_phase==PERF_HOOK && perf_current_hook<PERF_HOOK_SLOTS)perf_hooks[perf_current_hook].qpc+=elapsed;
    }
    perf_last = now;
}
/* UINT_MAX token means no instrumentation. Foreign threads are not merged
 * into a main-thread frame: the report becomes invalid instead. */
static unsigned perf_begin(unsigned phase) {
    if (!perf_enabled || perf_complete) return UINT_MAX;
    if (GetCurrentThreadId() != perf_thread) { InterlockedExchange((LONG *)&perf_error, 1); return UINT_MAX; }
    EnterCriticalSection(&perf_lock);
    perf_charge(perf_now());
    unsigned previous = perf_phase;
    perf_phase = phase; ++perf_depth; ++perf_counts[phase];
    LeaveCriticalSection(&perf_lock);
    return previous;
}
static void perf_end(unsigned previous) {
    if (previous == UINT_MAX) return;
    EnterCriticalSection(&perf_lock);
    perf_charge(perf_now()); perf_phase = previous; --perf_depth;
    LeaveCriticalSection(&perf_lock);
}
static unsigned perf_hook_begin(const void *identity,const char *name) {
    if(!perf_enabled||perf_complete)return UINT_MAX;
    if(GetCurrentThreadId()!=perf_thread){InterlockedExchange((LONG *)&perf_error,1);return UINT_MAX;}
    EnterCriticalSection(&perf_lock);perf_charge(perf_now());
    unsigned token=perf_phase|((perf_current_hook+1)<<3);
    perf_phase=PERF_HOOK;++perf_depth;++perf_counts[PERF_HOOK];
    unsigned start=(unsigned)(((uintptr_t)identity>>4)^((uintptr_t)identity>>16))&(PERF_HOOK_SLOTS-1);
    perf_current_hook=UINT_MAX;
    for(unsigned i=0;i<PERF_HOOK_SLOTS;++i) {
        unsigned slot=(start+i)&(PERF_HOOK_SLOTS-1);perf_hook_entry *h=&perf_hooks[slot];
        if(!h->identity || h->identity==identity) {h->identity=identity;h->name=name;++h->count;perf_current_hook=slot;break;}
    }
    if(perf_current_hook==UINT_MAX)++perf_hook_overflow;
    LeaveCriticalSection(&perf_lock);return token;
}
static void perf_hook_end(unsigned token) {
    if(token==UINT_MAX)return;
    EnterCriticalSection(&perf_lock);perf_charge(perf_now());
    perf_phase=token&7;perf_current_hook=(token>>3)-1;--perf_depth;LeaveCriticalSection(&perf_lock);
}
static BOOL perf_hook_report(HANDLE f) {
    unsigned top[20],count=0;
    for(unsigned i=0;i<PERF_HOOK_SLOTS;++i)if(perf_hooks[i].identity&&perf_hooks[i].count) {
        unsigned at=0;while(at<count && (perf_hooks[top[at]].qpc>perf_hooks[i].qpc ||
            (perf_hooks[top[at]].qpc==perf_hooks[i].qpc && strcmp(perf_hooks[top[at]].name,perf_hooks[i].name)<0)))++at;
        if(at>=20)continue;if(count<20)++count;
        for(unsigned j=count-1;j>at;--j)top[j]=top[j-1];top[at]=i;
    }
    char text[256];DWORD written;int n=snprintf(text,sizeof(text),",\"hook_names\":%s,\"hook_overflow\":%llu,\"hooks_top20\":[",perf_hook_names?"true":"false",(unsigned long long)perf_hook_overflow);
    if(!WriteFile(f,text,n,&written,NULL)||written!=(DWORD)n)return FALSE;
    for(unsigned i=0;i<count;++i) {
        perf_hook_entry *h=&perf_hooks[top[i]];
        n=snprintf(text,sizeof(text),"%s{\"name\":\"%s\",\"qpc\":%llu,\"count\":%llu}",i?",":"",h->name,(unsigned long long)h->qpc,(unsigned long long)h->count);
        if(n<0||n>=(int)sizeof(text)||!WriteFile(f,text,n,&written,NULL)||written!=(DWORD)n)return FALSE;
    }
    return WriteFile(f,"]}\n",3,&written,NULL)&&written==3;
}
static BOOL perf_report(const char *reason, BOOL complete) {
    if (!perf_enabled) return TRUE;
    EnterCriticalSection(&perf_lock);
    if (perf_complete) { LeaveCriticalSection(&perf_lock); return TRUE; }
    /* Reporting on the flush worker must not charge its clock to an active
     * main-thread phase. Final reports are made at a quiescent boundary. */
    if (GetCurrentThreadId() == perf_thread && !perf_complete) perf_charge(perf_now());
    if (complete) perf_complete = TRUE;
    HANDLE f = CreateFileW(perf_temp, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    BOOL ok = f != INVALID_HANDLE_VALUE;
    char text[4096];
    int n = snprintf(text, sizeof(text), "{\"schema\":1,\"module_sha256\":\"%s\",\"pid\":%lu,\"thread\":%lu,\"qpc_frequency\":%lld,\"error\":%lu,\"complete\":%s,\"reason\":\"%s\",\"last_frame\":%lu,\"frame_limit\":%lu,\"exclusive\":true,\"phases\":{",
        perf_module_sha256, (unsigned long)GetCurrentProcessId(), (unsigned long)perf_thread, (long long)perf_frequency.QuadPart,
        (unsigned long)perf_error, perf_complete ? "true" : "false", reason,
        (unsigned long)perf_frame, (unsigned long)perf_frame_limit);
    for (unsigned i = 0; i < PERF_PHASES; ++i)
        n += snprintf(text+n, sizeof(text)-(size_t)n, "%s\"%s\":{\"qpc\":%llu,\"count\":%llu}",
            i ? "," : "", perf_names[i], (unsigned long long)perf_totals[i], (unsigned long long)perf_counts[i]);
    n += snprintf(text+n, sizeof(text)-(size_t)n,
        "},\"background_write\":{\"qpc\":%llu,\"count\":%llu},\"write_calls\":%llu,\"write_bytes\":%llu,\"presentation\":{\"every_n\":%lu,\"defer_flip\":%s,\"deferred\":%llu,\"drained\":%llu,\"unobserved_interfaces\":%llu}}\n",
        (unsigned long long)perf_worker_write_qpc, (unsigned long long)perf_worker_write_count,
        (unsigned long long)perf_write_calls, (unsigned long long)perf_write_bytes,
        (unsigned long)perf_presentation_every, perf_defer_flip?"true":"false",
        (unsigned long long)perf_dd_deferred, (unsigned long long)perf_dd_drained, (unsigned long long)perf_dd_unobserved);
    DWORD written = 0;
    /* Keep the existing presentation object; replace only the final outer
     * brace/newline with the bounded per-hook array. Stream to avoid a new
     * fixed-size assumption for twenty maximum-length names/counters. */
    n-=2;
    if (ok) { ok = WriteFile(f, text, (DWORD)n, &written, NULL) && written == (DWORD)n && perf_hook_report(f); CloseHandle(f); }
    if (ok) ok = MoveFileExW(perf_temp, perf_path, MOVEFILE_REPLACE_EXISTING);
    if (!ok) { DeleteFileW(perf_temp); perf_error = 2; }
    LeaveCriticalSection(&perf_lock);
    return ok;
}
static void perf_tick(DWORD frame) {
    if (!perf_enabled || perf_complete) return;
    EnterCriticalSection(&perf_lock);
    perf_charge(perf_now()); perf_frame = frame;
    if (!perf_started) { /* Exclude startup, including the first tick's instrumentation. */
        memset(perf_totals, 0, sizeof(perf_totals)); memset(perf_counts, 0, sizeof(perf_counts));
        for(unsigned i=0;i<PERF_HOOK_SLOTS;++i)perf_hooks[i].qpc=perf_hooks[i].count=0;
        perf_hook_overflow=0;
        perf_worker_write_qpc=perf_worker_write_count=perf_write_calls=perf_write_bytes=0;
        perf_dd_deferred=perf_dd_drained=perf_dd_unobserved=0;
        perf_started = TRUE;
    } else ++perf_counts[PERF_OTHER];
    LeaveCriticalSection(&perf_lock);
    if (perf_frame_limit && frame >= perf_frame_limit-1) {
        if (perf_before_complete) perf_before_complete();
        perf_report("frame_limit", TRUE);
    }
}
static BOOL perf_configure(const WCHAR *ini) {
    if (!GetPrivateProfileIntW(L"perf", L"enabled", 0, ini)) return TRUE;
    DWORD n = GetPrivateProfileStringW(L"perf", L"report", L"", perf_path, MAX_PATH, ini);
    if (!n || n > MAX_PATH-32) return FALSE;
    perf_frame_limit = GetPrivateProfileIntW(L"perf", L"frame_limit", 0, ini);
    swprintf(perf_temp, MAX_PATH, L"%ls.tmp.%lu", perf_path, (unsigned long)GetCurrentProcessId());
    if (GetFileAttributesW(perf_temp) != INVALID_FILE_ATTRIBUTES) return FALSE;
    HANDLE reserve = CreateFileW(perf_path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (reserve == INVALID_HANDLE_VALUE) return FALSE;
    CloseHandle(reserve);
    if (!perf_lock_ready) { InitializeCriticalSection(&perf_lock); perf_lock_ready = TRUE; }
    QueryPerformanceFrequency(&perf_frequency); perf_thread = GetCurrentThreadId();
    perf_last = perf_now(); perf_enabled = TRUE;
    return perf_report("startup", FALSE);
}
#endif
