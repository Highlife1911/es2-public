#ifndef ES2_ENTRY_PROBES_H
#define ES2_ENTRY_PROBES_H
#include "entry_probe_engine.h"
#include "probe_entry_inventory.h"
#include <wchar.h>
#include <stdio.h>

static ep_site probe_sites[EP_MAX_ENTRIES];
static BOOL probe_enabled, probe_complete, probe_has_tick;
static DWORD probe_frame, probe_frame_limit, probe_install_thread, probe_tick_thread;
static WCHAR probe_path[MAX_PATH], probe_temp[MAX_PATH], probe_load_path[MAX_PATH], probe_load_temp[MAX_PATH];
static const char *probe_reason="startup";
static const WCHAR *const probe_register_names[]={L"",L"esp4",L"eax",L"ecx",L"edx",L"ebx",L"esp",L"ebp",L"esi",L"edi",L"ret"};

static BYTE probe_opcode(uint32_t va) {
    size_t low=0,high=sizeof(probe_instructions)/sizeof(probe_instructions[0]);
    while(low<high) {size_t mid=low+(high-low)/2;uint32_t value=probe_instructions[mid].va;
        if(value==va)return probe_instructions[mid].opcode;if(value<va)low=mid+1;else high=mid;}
    return 0;
}
/* Kind of a known function start of the second table, EP_KINDS if none. */
static unsigned probe_kind_of(uint32_t va) {
    size_t low=0,high=sizeof(probe_entry_kinds)/sizeof(probe_entry_kinds[0]);
    while(low<high) {size_t mid=low+(high-low)/2;uint32_t value=probe_entry_kinds[mid].va;
        if(value==va)return probe_entry_kinds[mid].kind;if(value<va)low=mid+1;else high=mid;}
    return EP_KINDS;
}
static BOOL probe_get(const WCHAR *ini,const WCHAR *key,const WCHAR *fallback,WCHAR *out,DWORD size) {
    return GetPrivateProfileStringW(L"probes",key,fallback,out,size,ini)<size-1;
}
static BOOL probe_decimal(const WCHAR *ini,const WCHAR *key,const WCHAR *fallback,DWORD *out) {
    WCHAR text[24];if(!probe_get(ini,key,fallback,text,24)||!*text)return FALSE;
    uint32_t n=0;for(unsigned i=0;text[i];++i) {unsigned d=text[i]-L'0';if(d>9||n>(UINT32_MAX-d)/10)return FALSE;n=n*10+d;}*out=n;return TRUE;
}
static BOOL probe_parse_entries(WCHAR *text) {
    ep_count=0;memset(probe_sites,0,sizeof(probe_sites));
    WCHAR *p=text;
    while(*p) {
        if(ep_count==EP_MAX_ENTRIES)return FALSE;
        while(*p==L' '||*p==L'\t')++p;
        if(p[0]==L'0'&&(p[1]==L'x'||p[1]==L'X'))p+=2;
        uint32_t va=0;unsigned digits=0;
        for(;;) {unsigned d=*p>=L'0'&&*p<=L'9'?*p-L'0':*p>=L'a'&&*p<=L'f'?*p-L'a'+10:*p>=L'A'&&*p<=L'F'?*p-L'A'+10:16;
            if(d==16)break;if(++digits>8)return FALSE;va=va*16+d;++p;}
        BYTE opcode=digits?probe_opcode(va):0;unsigned kind=opcode?EP_PUSH:digits?probe_kind_of(va):EP_KINDS;
        if(kind==EP_KINDS)return FALSE;
        unsigned rcv=EP_NONE;
        if(!wcsncmp(p,L":rcv=",5)) {
            p+=5;WCHAR *end=p;while(*end && *end!=L',' && *end!=L' '&&*end!=L'\t')++end;
            for(unsigned i=1;i<=EP_RETADDR;++i)if(wcslen(probe_register_names[i])==(size_t)(end-p)&&!wcsncmp(p,probe_register_names[i],end-p))rcv=i;
            if(!rcv)return FALSE;p=end;
        }
        while(*p==L' '||*p==L'\t')++p;
        if(*p && *p!=L',')return FALSE;
        for(unsigned i=0;i<ep_count;++i)if(probe_sites[i].patch.address==va)return FALSE;
        ep_site *s=&probe_sites[ep_count++];s->patch.address=va;s->patch.length=1;s->patch.saved[0]=opcode;s->receiver=rcv;s->kind=kind;
        if(!*p)break;if(!*++p)return FALSE;
    }
    return ep_count!=0;
}
static int probe_configuration(const WCHAR *ini) {
    WCHAR enabled[8],mode[24],entries[8192];DWORD limit;
    if(!probe_get(ini,L"enabled",L"0",enabled,8))return -1;
    if(!wcscmp(enabled,L"0"))return 0;
    if(wcscmp(enabled,L"1")||!probe_get(ini,L"mode",L"count",mode,24))return -1;
    if(!wcscmp(mode,L"count"))ep_mode=LC_COUNT;else if(!wcscmp(mode,L"saturate"))ep_mode=LC_SATURATE;else return -1;
    if(!probe_decimal(ini,L"saturate_after",L"1",&limit)||!limit||
       !probe_decimal(ini,L"frame_limit",L"0",&probe_frame_limit)||
       !probe_get(ini,L"entries",L"",entries,8192)||!probe_parse_entries(entries)||
       !probe_get(ini,L"report",L"",probe_path,MAX_PATH)||!*probe_path||wcslen(probe_path)>MAX_PATH-32||
       !probe_get(ini,L"load_report",L"",probe_load_path,MAX_PATH)||wcslen(probe_load_path)>MAX_PATH-32)return -1;
    ep_limit=limit;return 1;
}
/* The report is written to path through temp (both reserved at install). */
static BOOL probe_write(const WCHAR *path,const WCHAR *temp) {
    HANDLE f=CreateFileW(temp,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if(f==INVALID_HANDLE_VALUE){ep_error=7;return FALSE;}
    char text[1024];int n=snprintf(text,sizeof(text),"{\"schema\":1,\"module_sha256\":\"%s\",\"mode\":\"%s\",\"saturate_after\":%llu,\"pid\":%lu,\"install_thread\":%lu,\"first_thread\":%lu,\"other_thread\":%lu,\"tick_thread\":%lu,\"error\":%ld,\"complete\":%s,\"reason\":\"%s\",\"has_tick\":%s,\"last_frame\":%lu,\"frame_limit\":%lu,\"entries\":[",
        LC_MODULE_HASH,ep_mode==LC_COUNT?"count":"saturate",(unsigned long long)ep_limit,(unsigned long)GetCurrentProcessId(),
        (unsigned long)probe_install_thread,(unsigned long)ep_first_thread,(unsigned long)ep_other_thread,(unsigned long)probe_tick_thread,(long)ep_error,
        probe_complete?"true":"false",probe_reason,probe_has_tick?"true":"false",(unsigned long)probe_frame,(unsigned long)probe_frame_limit);
    BOOL ok=live_write(f,text,n);
    for(unsigned i=0;ok&&i<ep_count;++i) {
        ep_site *s=&probe_sites[i];LONG64 count=lc_read64(&s->count);
        n=snprintf(text,sizeof(text),"%s{\"va\":\"0x%08lx\",\"receiver\":\"%ls\",\"count\":%llu,\"saturated\":%s,\"overflow\":%llu,\"receivers\":[",i?",":"",(unsigned long)s->patch.address,
            probe_register_names[s->receiver],(unsigned long long)count,ep_mode==LC_SATURATE&&count>=ep_limit?"true":"false",(unsigned long long)lc_read64(&s->overflow));
        ok=live_write(f,text,n);BOOL comma=FALSE;
        for(unsigned j=0;ok&&j<EP_RECEIVERS;++j)if(s->receivers[j].vtable) {
            n=snprintf(text,sizeof(text),"%s{\"vtable\":\"0x%08lx\",\"count\":%llu}",comma?",":"",(unsigned long)s->receivers[j].vtable,(unsigned long long)lc_read64(&s->receivers[j].count));
            ok=live_write(f,text,n);comma=TRUE;
        }
        ok=ok&&live_write(f,"]}",2);
    }
    ok=ok&&live_write(f,"]}\n",3);if(ok)ok=FlushFileBuffers(f);if(!CloseHandle(f))ok=FALSE;
    if(ok)ok=MoveFileExW(temp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    if(!ok){DeleteFileW(temp);ep_error=7;}return ok;
}
static BOOL probe_report(void) {return !probe_enabled||probe_write(probe_path,probe_temp);}
static BOOL probe_active(void) {return probe_enabled&&!probe_complete;}
static void probe_exit_error(void) {
    ep_error=9;probe_complete=FALSE;probe_reason="exit_restore_failed";probe_report();
}
static void probe_shutdown(const char *reason) {
    if(!probe_active())return;
    __attribute__((aligned(16))) BYTE fpu[512];__asm__ volatile("fxsave %0":"=m"(fpu));DWORD error=GetLastError();
    BOOL owned=!ep_other_thread&&(!ep_first_thread||(DWORD)ep_first_thread==GetCurrentThreadId());
    if(!owned)ep_error=6;
    BOOL restored=owned&&ep_uninstall();probe_complete=restored&&!ep_error;probe_reason=reason;probe_report();
    SetLastError(error);__asm__ volatile("fxrstor %0"::"m"(fpu));
}
static void probe_tick(DWORD frame) {
    if(!probe_active())return;
    BOOL first=!probe_has_tick;
    probe_frame=frame;probe_has_tick=TRUE;probe_tick_thread=GetCurrentThreadId();
    /* The counts at the first tick, i.e. what ran between arming and
     * the first frame (the mission load under early arming). */
    if(first&&*probe_load_path) {
        __attribute__((aligned(16))) BYTE fpu[512];__asm__ volatile("fxsave %0":"=m"(fpu));DWORD error=GetLastError();
        probe_reason="first_tick";probe_write(probe_load_path,probe_load_temp);probe_reason="running";
        SetLastError(error);__asm__ volatile("fxrstor %0"::"m"(fpu));
    }
    if(probe_frame_limit&&frame>=probe_frame_limit-1)probe_shutdown("frame_limit");
}
static BOOL install_entry_probes(void) {
    WCHAR exe[MAX_PATH],ini[MAX_PATH];DWORD n=GetModuleFileNameW(NULL,exe,MAX_PATH);
    WCHAR *name=n&&n<MAX_PATH?wcsrchr(exe,L'\\'):NULL;
    if(!name||_wcsicmp(name+1,L"DBSIM.EXE"))return TRUE;
    n=GetEnvironmentVariableW(L"ES2_LIVE_COVERAGE_CONFIG",ini,MAX_PATH);if(n>=MAX_PATH)return FALSE;if(!n)wcscpy(ini,config_path);
    int configured=probe_configuration(ini);if(!configured)return TRUE;if(configured<0){log_event("entry_probes_config_refused");return FALSE;}
    BYTE *image=(BYTE *)GetModuleHandleW(NULL);
    if(!rng_hook_reference_file(exe)||!es2_hook_image_valid(image,0x400000,RNG_HOOK_IMAGE_SIZE)){log_event("entry_probes_hash_refused");return FALSE;}
    lc_range ranges[FUNCTION_HOOK_COUNT+LIVE_SITE_COUNT];unsigned nranges=0;
    /* Hooks are installed all together as soon as one is not original (and
     * then patch their prologue even in original mode), or not at all. Only
     * a patched prologue excludes probes; an uninstalled one is original code. */
    for(unsigned i=0;i<FUNCTION_HOOK_COUNT;++i)if(rng_hooks[i].installed) {const es2_hook_descriptor *d=function_hook_descriptor(i);ranges[nranges++]=(lc_range){(uintptr_t)image+d->rva,d->relocate_size};}
    for(unsigned i=0;i<LIVE_SITE_COUNT;++i)ranges[nranges++]=(lc_range){live_all_sites[i].address,live_all_sites[i].length};
    if(!ep_validate(probe_sites,ep_count,image,RNG_HOOK_IMAGE_SIZE,ranges,nranges)){log_event("entry_probes_sites_refused");return FALSE;}
    swprintf(probe_temp,MAX_PATH,L"%ls.tmp.%lu",probe_path,(unsigned long)GetCurrentProcessId());
    swprintf(probe_load_temp,MAX_PATH,L"%ls.tmp.%lu",probe_load_path,(unsigned long)GetCurrentProcessId());
    for(unsigned i=0;i<(*probe_load_path?2u:1u);++i) {
        if(GetFileAttributesW(i?probe_load_temp:probe_temp)!=INVALID_FILE_ATTRIBUTES)return FALSE;
        HANDLE reserve=CreateFileW(i?probe_load_path:probe_path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
        if(reserve==INVALID_HANDLE_VALUE)return FALSE;CloseHandle(reserve);
    }
    if(!live_install_exit(image,RNG_HOOK_IMAGE_SIZE)){log_event("entry_probes_exit_refused");return FALSE;}
    live_extra_shutdown=probe_shutdown;live_extra_tick=probe_tick;live_extra_active=probe_active;
    live_extra_exit_error=probe_exit_error;
    probe_enabled=TRUE;probe_install_thread=GetCurrentThreadId();
    if(!ep_install(probe_sites,ep_count,image,RNG_HOOK_IMAGE_SIZE,ranges,nranges,ep_mode,ep_limit)) {ep_error=1;probe_reason="install_failed";probe_report();return FALSE;}
    probe_report();log_event("entry_probes_installed");return TRUE;
}
#endif
