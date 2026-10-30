#ifndef ES2_LIVE_COVERAGE_H
#define ES2_LIVE_COVERAGE_H
#include "live_coverage_engine.h"
#include <stdio.h>
#include <wchar.h>
#define LC_MODULE_HASH "8561aa47e5bb1bf1f837044b08e5d807e8de492784331708a7ddeabf6d44c4ad"
#define je LC_JE
#define jne LC_JNE
#define jl LC_JL
#define jle LC_JLE
#define jg LC_JG
#define jge LC_JGE
#define jb LC_JB
#define ja LC_JA
#define jns LC_JNS
#define jae LC_JAE
#define jbe LC_JBE
#define js LC_JS
#define jp LC_JP
#define jnp LC_JNP
#define jo LC_JO
#define jno LC_JNO
#define jz LC_JE
#define jnz LC_JNE
#define jc LC_JB
#define jnc LC_JAE
static lc_site live_all_sites[]={
#define BRANCH(N,A,L,C,T) {.owner=ES2_HOOK_INDEX_##N,.address=A,.length=L,.condition=C,.target=T},
#define PROVED(N,A,E,R)
#include "tools/reference-game/invitro/branch_sites.h"
#undef PROVED
#undef BRANCH
};
#undef je
#undef jne
#undef jl
#undef jle
#undef jg
#undef jge
#undef jb
#undef ja
#undef jns
#undef jae
#undef jbe
#undef js
#undef jp
#undef jnp
#undef jo
#undef jno
#undef jz
#undef jnz
#undef jc
#undef jnc
#define LIVE_SITE_COUNT (sizeof(live_all_sites)/sizeof(live_all_sites[0]))
/* Edge mode: EDGE lines sorted by address (scripts/branch_edges.py). A site
 * without a line (a stale table after new BRANCH lines) gets no edge help. */
typedef struct live_edge { uint32_t va; BYTE fall,taken; uint32_t trap; } live_edge;
static const live_edge live_edges[]={
#define EDGE(A,F,T,R) {A,F,T,R},
#include "tools/reference-game/proxy/live_coverage_edges.h"
#undef EDGE
};
#define LIVE_EDGE_COUNT (sizeof(live_edges)/sizeof(live_edges[0]))
static const live_edge *live_edge_find(uintptr_t va) {
    unsigned lo=0,hi=LIVE_EDGE_COUNT;
    while(lo<hi) {unsigned mid=lo+(hi-lo)/2;if(live_edges[mid].va<va)lo=mid+1;else hi=mid;}
    return lo<LIVE_EDGE_COUNT && live_edges[lo].va==va?&live_edges[lo]:NULL;
}
static lc_site live_sites[LIVE_SITE_COUNT];
/* Outcomes a baseline already knows ([coverage] known=<file>, edge mode only):
 * bit 0 fall, bit 1 taken, indexed like live_all_sites. */
static BYTE live_known[LIVE_SITE_COUNT];
static unsigned live_known_outcomes;
static BOOL live_selected[FUNCTION_HOOK_COUNT],live_enabled,live_complete;
static es2_hook_mode live_hook_modes[FUNCTION_HOOK_COUNT];
static WCHAR live_report_path[MAX_PATH],live_temp_path[MAX_PATH];
static DWORD live_period=32,live_frame_limit,live_tick_thread,live_install_thread,live_last_frame;
static BOOL live_has_tick;
static const char *live_reason="snapshot";
static void (__cdecl *live_previous_exit)(int);
static DWORD *live_exit_slot;
static DWORD live_exit_original;
static DWORD live_exit_protection;
/* Additional instrumentation shares this single verified exit import. */
static void (*live_extra_shutdown)(const char *reason);
static void (*live_extra_tick)(DWORD frame);
static BOOL (*live_extra_active)(void);
static void (*live_extra_exit_error)(void);
static BOOL live_restore_exit(void);
static void live_coverage_shutdown(const char *reason);
static void __cdecl live_coverage_on_exit(void) { live_coverage_shutdown("exit"); }
static void __cdecl live_coverage_exit(int code) { live_coverage_shutdown("exit"); live_previous_exit(code); }
static BOOL live_get(const WCHAR *ini,const WCHAR *key,const WCHAR *fallback,WCHAR *out,DWORD capacity) {
    DWORD n=GetPrivateProfileStringW(L"coverage",key,fallback,out,capacity,ini); return n<capacity-1;
}
static BOOL live_number(const WCHAR *ini,const WCHAR *key,const WCHAR *fallback,DWORD *value) {
    WCHAR text[24];if(!live_get(ini,key,fallback,text,24) || !*text)return FALSE;
    uint32_t v=0;for(unsigned i=0;text[i];++i) {if(text[i]<L'0'||text[i]>L'9'||v>(UINT32_MAX-(text[i]-L'0'))/10u)return FALSE;v=v*10u+(text[i]-L'0');}*value=v;return TRUE;
}
/* Known outcomes: one line per site, "0x<va> <fall 0|1> <taken 0|1>"; every
 * address must be a BRANCH site, each at most once. */
static BOOL live_read_known(const WCHAR *path) {
    memset(live_known,0,sizeof(live_known));live_known_outcomes=0;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE)return FALSE;
    DWORD size=GetFileSize(file,NULL),read=0;
    char *text=size!=INVALID_FILE_SIZE && size<(1u<<20)?HeapAlloc(GetProcessHeap(),0,size+1):NULL;
    BOOL ok=text && ReadFile(file,text,size,&read,NULL) && read==size;
    CloseHandle(file);if(!ok) {if(text)HeapFree(GetProcessHeap(),0,text);return FALSE;}
    text[size]=0;
    static BOOL seen[LIVE_SITE_COUNT];memset(seen,0,sizeof(seen));
    for(char *line=text;ok && *line;) {
        char *end=strchr(line,'\n');if(end)*end=0;
        size_t n=strlen(line);if(n && line[n-1]=='\r')line[--n]=0;
        if(n && line[0]!='#') {
            unsigned long va=0;unsigned fall=2,taken=2;char tail;unsigned i=0;
            ok=sscanf(line,"0x%lx %u %u %c",&va,&fall,&taken,&tail)==3 && fall<=1 && taken<=1;
            while(ok && i<LIVE_SITE_COUNT && live_all_sites[i].address!=(uintptr_t)va)++i;
            ok=ok && i<LIVE_SITE_COUNT && !seen[i];
            if(ok) {seen[i]=TRUE;live_known[i]=(BYTE)(fall|taken<<1);live_known_outcomes+=fall+taken;}
        }
        if(!end)break;line=end+1;
    }
    HeapFree(GetProcessHeap(),0,text);
    if(!ok) {memset(live_known,0,sizeof(live_known));live_known_outcomes=0;}
    return ok;
}
/* 0 disabled, 1 enabled, -1 malformed; absent section has no side effects. */
static int live_configuration(const WCHAR *ini) {
    WCHAR enabled[8],mode[24],hooks[8192],known[MAX_PATH];DWORD first_ticks;
    if(!live_get(ini,L"enabled",L"0",enabled,8))return -1;
    if(!wcscmp(enabled,L"0"))return 0;
    if(wcscmp(enabled,L"1") || !live_get(ini,L"mode",L"saturate",mode,24))return -1;
    if(!wcscmp(mode,L"saturate"))lc_mode=LC_SATURATE;else if(!wcscmp(mode,L"count"))lc_mode=LC_COUNT;
    else if(!wcscmp(mode,L"edge"))lc_mode=LC_EDGE;else return -1;
    if(!live_get(ini,L"known",L"",known,MAX_PATH))return -1;
    memset(live_known,0,sizeof(live_known));live_known_outcomes=0;
    if(*known && (lc_mode!=LC_EDGE || !live_read_known(known)))return -1;
    if(!live_get(ini,L"hooks",L"all",hooks,8192)||!*hooks||
       !live_get(ini,L"report",L"",live_report_path,MAX_PATH)||!*live_report_path||
       !live_number(ini,L"report_every_ticks",L"32",&live_period)||
       !live_number(ini,L"frame_limit",L"0",&live_frame_limit)||
       !live_number(ini,L"first_hit_ticks",L"0",&first_ticks)||first_ticks>1)return -1;
    lc_track_ticks=first_ticks!=0;
    memset(live_selected,0,sizeof(live_selected));
    if(!wcscmp(hooks,L"all")) {for(unsigned i=0;i<FUNCTION_HOOK_COUNT;++i)live_selected[i]=TRUE;return 1;}
    WCHAR *p=hooks;
    while(*p) {WCHAR *end=wcschr(p,L',');if(end)*end=0;while(*p==L' '||*p==L'\t')++p;
        WCHAR *last=p+wcslen(p);while(last>p && (last[-1]==L' '||last[-1]==L'\t'))*--last=0;
        BOOL found=FALSE;for(unsigned i=0;i<FUNCTION_HOOK_COUNT;++i) {const char *name=function_hook_descriptor(i)->name;unsigned j=0;while(name[j]&&p[j]==(WCHAR)name[j])++j;
            if(!name[j]&&!p[j]) {if(live_selected[i])return -1;live_selected[i]=TRUE;found=TRUE;break;}}
        if(!found)return -1;if(!end)break;p=end+1;if(!*p)return -1;
    }
    return 1;
}
static BOOL live_write(HANDLE file,const char *text,int length) {DWORD written=0;return length>=0 && WriteFile(file,text,(DWORD)length,&written,NULL)&&written==(DWORD)length;}
static BOOL live_report(void) {
    if(!live_enabled)return TRUE;
    HANDLE file=CreateFileW(live_temp_path,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE) {lc_error=7;return FALSE;}
    char line[1024];BOOL ok=TRUE,edge=lc_mode==LC_EDGE;
    int n=snprintf(line,sizeof(line),"{\"schema\":1,\"module_sha256\":\"%s\",\"mode\":\"%s\",\"pid\":%lu,\"install_thread\":%lu,\"first_thread\":%lu,\"other_thread\":%lu,\"tick_thread\":%lu,\"error\":%ld,\"complete\":%s,\"reason\":\"%s\",\"has_tick\":%s,\"last_frame\":%lu,\"frame_limit\":%lu,\"first_hit_ticks\":%s,",
        LC_MODULE_HASH,edge?"edge":lc_mode==LC_COUNT?"count":"saturate",(unsigned long)GetCurrentProcessId(),(unsigned long)live_install_thread,
        (unsigned long)lc_first_thread,(unsigned long)lc_other_thread,(unsigned long)live_tick_thread,(long)lc_error,
        live_complete?"true":"false",live_reason,live_has_tick?"true":"false",(unsigned long)live_last_frame,(unsigned long)live_frame_limit,lc_track_ticks?"true":"false");
    ok=live_write(file,line,n);
    /* Edge mode: the roles armed at install and the baseline size. */
    if(ok && edge) {n=snprintf(line,sizeof(line),"\"edge_roles\":{\"jcc\":%u,\"redirect\":%u,\"fall\":%u,\"taken\":%u},\"known_outcomes\":%u,",
        lc_edge_armed[0],lc_edge_armed[1],lc_edge_armed[2],lc_edge_armed[3],live_known_outcomes);ok=live_write(file,line,n);}
    ok=ok&&live_write(file,"\"hooks\":[",9);BOOL comma=FALSE;
    for(unsigned i=0;ok&&i<FUNCTION_HOOK_COUNT;++i)if(live_selected[i]) {n=snprintf(line,sizeof(line),"%s{\"name\":\"%s\",\"mode\":%u}",comma?",":"",function_hook_descriptor(i)->name,live_hook_modes[i]);ok=live_write(file,line,n);comma=TRUE;}
    ok=ok&&live_write(file,"],\"sites\":[",11);
    for(unsigned i=0;ok&&i<lc_count;++i) {lc_site *s=&live_sites[i];n=snprintf(line,sizeof(line),"%s{\"hook\":\"%s\",\"va\":\"0x%08lx\",\"condition\":\"%s\",\"fall\":%llu,\"taken\":%llu,\"restored\":%s",i?",":"",function_hook_descriptor(s->owner)->name,(unsigned long)s->address,lc_conditions[s->condition],(unsigned long long)lc_read64(&s->fall),(unsigned long long)lc_read64(&s->taken),s->restored?"true":"false");ok=live_write(file,line,n);
        if(edge) {n=snprintf(line,sizeof(line),",\"known_fall\":%s,\"known_taken\":%s",s->known[0]?"true":"false",s->known[1]?"true":"false");ok=ok&&live_write(file,line,n);}
        if(lc_track_ticks) {char fall[32]="null",taken[32]="null";LONG64 f=lc_read64(&s->first_fall_tick),t=lc_read64(&s->first_taken_tick);
            if(f!=INT64_MIN)snprintf(fall,sizeof(fall),"%lld",(long long)f);if(t!=INT64_MIN)snprintf(taken,sizeof(taken),"%lld",(long long)t);
            n=snprintf(line,sizeof(line),",\"first_fall_tick\":%s,\"first_taken_tick\":%s",fall,taken);ok=ok&&live_write(file,line,n);}
        ok=ok&&live_write(file,"}",1);
    }
    ok=ok&&live_write(file,"]}\n",3);if(ok)ok=FlushFileBuffers(file);if(!CloseHandle(file))ok=FALSE;
    if(ok)ok=MoveFileExW(live_temp_path,live_report_path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    if(!ok) {DeleteFileW(live_temp_path);lc_error=7;}return ok;
}
static void live_coverage_shutdown(const char *reason) {
    if(live_extra_shutdown && strcmp(reason,"frame_limit"))live_extra_shutdown(reason);
    if(!live_enabled||live_complete) {
        DWORD error=GetLastError();
        if((!live_extra_active || !live_extra_active()) && !live_restore_exit() && live_extra_exit_error)live_extra_exit_error();
        SetLastError(error);
        return;
    }
    __attribute__((aligned(16))) BYTE fpu[512];__asm__ volatile("fxsave %0":"=m"(fpu));
    DWORD last_error=GetLastError();
    BOOL owned=!lc_other_thread && (!lc_first_thread || (DWORD)lc_first_thread==GetCurrentThreadId());
    if(!owned)lc_error=6;
    /* A detected foreign executor invalidates quiescence. Keep the VEH alive
     * rather than removing it underneath that thread. */
    BOOL restored=owned&&lc_uninstall();
    if((!live_extra_active || !live_extra_active()) && !live_restore_exit()) {lc_error=9;restored=FALSE;}
    live_complete=restored&&!lc_error;live_reason=reason;live_report();SetLastError(last_error);
    __asm__ volatile("fxrstor %0"::"m"(fpu));
}
static void live_coverage_tick(DWORD frame) {
    if(live_extra_tick)live_extra_tick(frame);
    if(live_extra_active && !live_extra_active() && (!live_enabled||live_complete)) {
        DWORD error=GetLastError();if(!live_restore_exit() && live_extra_exit_error)live_extra_exit_error();SetLastError(error);
    }
    if(!live_enabled||live_complete)return;
    __attribute__((aligned(16))) BYTE fpu[512];__asm__ volatile("fxsave %0":"=m"(fpu));
    DWORD last_error=GetLastError();live_tick_thread=GetCurrentThreadId();live_last_frame=frame;live_has_tick=TRUE;
    if(lc_track_ticks)InterlockedExchange64(&lc_last_tick,frame);
    if(live_frame_limit && frame>=live_frame_limit-1)live_coverage_shutdown("frame_limit");
    else if(live_period && (frame%live_period)==0)live_report();
    SetLastError(last_error);__asm__ volatile("fxrstor %0"::"m"(fpu));
}
/* Resolve a bounded, named import only when its loaded pointer still equals
 * the DLL export. The unchanged executable hash was checked by the caller. */
static DWORD *live_import_slot(BYTE *image,size_t size,const char *symbol) {
    if(size<136||!lc_readable((uintptr_t)image,64)||image[0]!='M'||image[1]!='Z')return NULL;
    DWORD pe=lc_u32(image+0x3c);
    if(pe>size-136||!lc_readable((uintptr_t)image+pe,136)||memcmp(image+pe,"PE\0\0",4)||image[pe+24]!=0x0b||image[pe+25]!=1||lc_u32(image+pe+116)<2)return NULL;
    DWORD rva=lc_u32(image+pe+128),extent=lc_u32(image+pe+132);
    if(rva>=size||extent>size-rva||!lc_readable((uintptr_t)image+rva,extent))return NULL;
    for(unsigned i=0;i<extent/20;++i) {
        BYTE *descriptor=image+rva+20*i;DWORD name=lc_u32(descriptor+12);if(!name)break;
        if(name>size-11 || !lc_readable((uintptr_t)image+name,11))return NULL;
        if(_strnicmp((char *)image+name,"cw3220.DLL",11))continue;
        DWORD names=lc_u32(descriptor),slots=lc_u32(descriptor+16);
        if(!names||names>=size||slots>=size)return NULL;
        for(unsigned j=0;j<(size-names)/4 && j<(size-slots)/4;++j) {
            if(!lc_readable((uintptr_t)image+names+4*j,4)||!lc_readable((uintptr_t)image+slots+4*j,4))return NULL;
            DWORD rva=lc_u32(image+names+4*j);if(!rva)break;if(rva&0x80000000u)continue;
            size_t n=strlen(symbol)+1;if(n+2>size||rva>size-n-2||!lc_readable((uintptr_t)image+rva,n+2))return NULL;
            if(memcmp((char *)image+rva+2,symbol,n))continue;
            return (DWORD *)(image+slots+4*j);
        }
    }
    return NULL;
}
static DWORD *live_import(BYTE *image,size_t size,const char *symbol) {
    DWORD *slot=live_import_slot(image,size,symbol);
#ifdef ES2_RTL_IMPORTS_H
    /* With the lifecycle group the CW3220 import shim (installed at
     * DBSIM's entry, before this) owns _atexit and __exit; its thunk stands
     * for the import, so registrations reach the own atexit table and the
     * __exit chain ends in the own quick exit (rtl_imports.md "the
     * lifecycle group as own code"). */
    uint32_t shim=rtl_import_slot_value(symbol);
    if(slot&&shim&&*slot==shim)return slot;
#endif
    FARPROC expected=GetProcAddress(GetModuleHandleW(L"cw3220.DLL"),symbol);
    return slot&&expected&&*slot==(DWORD)(uintptr_t)expected?slot:NULL;
}
static BOOL live_restore_exit(void) {
    if(!live_exit_slot)return TRUE;
    DWORD current=*live_exit_slot;
    if(current!=live_exit_original && current!=(DWORD)(uintptr_t)live_coverage_exit)return FALSE;
    DWORD old,ignored;if(!VirtualProtect(live_exit_slot,4,PAGE_READWRITE,&old))return FALSE;
    InterlockedCompareExchange((LONG *)live_exit_slot,(LONG)live_exit_original,(LONG)(uintptr_t)live_coverage_exit);
    BOOL ok=*live_exit_slot==live_exit_original;
    if(!VirtualProtect(live_exit_slot,4,live_exit_protection,&ignored))return FALSE;
    if(ok)live_exit_slot=NULL;return ok;
}
static BOOL live_install_exit_slots(DWORD *exit_slot,DWORD *atexit_slot) {
    if(!exit_slot||!atexit_slot)return FALSE;
    /* Borland's imported C atexit covers normal return/exit; the _exit import
     * intercept covers immediate exits that skip registered callbacks. */
    int (__cdecl *register_exit)(void (__cdecl *)(void))=(void *)(uintptr_t)*atexit_slot;
    if(register_exit(live_coverage_on_exit))return FALSE;
    DWORD old,ignored;if(!VirtualProtect(exit_slot,4,PAGE_READWRITE,&old))return FALSE;
    live_previous_exit=(void *)(uintptr_t)*exit_slot;live_exit_slot=exit_slot;live_exit_original=*exit_slot;live_exit_protection=old;
    InterlockedExchange((LONG *)exit_slot,(LONG)(uintptr_t)live_coverage_exit);
    if(!VirtualProtect(exit_slot,4,old,&ignored)) {InterlockedExchange((LONG *)exit_slot,(LONG)live_exit_original);VirtualProtect(exit_slot,4,old,&ignored);live_exit_slot=NULL;return FALSE;}
    return TRUE;
}
static BOOL live_install_exit(BYTE *image,size_t size) {
    if(live_exit_slot)return TRUE;
    return live_install_exit_slots(live_import(image,size,"__exit"),live_import(image,size,"_atexit"));
}
static BOOL install_live_coverage(void) {
    WCHAR exe[MAX_PATH],ini[MAX_PATH];DWORD length=GetModuleFileNameW(NULL,exe,MAX_PATH);
    WCHAR *name=length&&length<MAX_PATH?wcsrchr(exe,L'\\'):NULL;
    if(!name||_wcsicmp(name+1,L"DBSIM.EXE"))return TRUE;
    DWORD env=GetEnvironmentVariableW(L"ES2_LIVE_COVERAGE_CONFIG",ini,MAX_PATH);
    if(env>=MAX_PATH)return FALSE;if(!env)wcscpy(ini,config_path);
    int configured=live_configuration(ini);if(!configured)return TRUE;if(configured<0){log_event("live_coverage_config_refused");return FALSE;}
    BYTE *image=(BYTE *)GetModuleHandleW(NULL);
    if(!rng_hook_reference_file(exe)||!es2_hook_image_valid(image,0x400000,RNG_HOOK_IMAGE_SIZE)){log_event("live_coverage_hash_refused");return FALSE;}
    lc_range ranges[FUNCTION_HOOK_COUNT];unsigned count=0;
    for(unsigned i=0;i<FUNCTION_HOOK_COUNT;++i) {const es2_hook_descriptor *d=function_hook_descriptor(i);ranges[i]=(lc_range){(uintptr_t)image+d->rva,d->relocate_size};if(!es2_hook_read_mode(config_path,d->name,&live_hook_modes[i]))return FALSE;}
    for(unsigned i=0;i<LIVE_SITE_COUNT;++i)if(live_selected[live_all_sites[i].owner]) {
        lc_site *s=&live_sites[count++];*s=live_all_sites[i];
        if(lc_mode==LC_EDGE) {
            const live_edge *e=live_edge_find(s->address);
            if(e) {s->exclusive[0]=e->fall;s->exclusive[1]=e->taken;s->trap=e->trap;}
            s->known[0]=live_known[i]&1;s->known[1]=(live_known[i]>>1)&1;
        }
    }
    if(!lc_validate(live_sites,count,image,RNG_HOOK_IMAGE_SIZE,ranges,FUNCTION_HOOK_COUNT)){log_event("live_coverage_sites_refused");return FALSE;}
    if(wcslen(live_report_path)>MAX_PATH-32)return FALSE;
    swprintf(live_temp_path,MAX_PATH,L"%ls.tmp.%lu",live_report_path,(unsigned long)GetCurrentProcessId());
    if(GetFileAttributesW(live_temp_path)!=INVALID_FILE_ATTRIBUTES)return FALSE;
    HANDLE reserve=CreateFileW(live_report_path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if(reserve==INVALID_HANDLE_VALUE)return FALSE;CloseHandle(reserve);
    if(!live_install_exit(image,RNG_HOOK_IMAGE_SIZE)) {log_event("live_coverage_exit_refused");return FALSE;}
    live_install_thread=GetCurrentThreadId();live_enabled=TRUE;
    if(!lc_install(live_sites,count,image,RNG_HOOK_IMAGE_SIZE,ranges,FUNCTION_HOOK_COUNT,lc_mode)) {lc_error=1;live_restore_exit();live_reason="install_failed";live_report();return FALSE;}
    live_report();log_event("live_coverage_installed");return TRUE;
}
#endif
