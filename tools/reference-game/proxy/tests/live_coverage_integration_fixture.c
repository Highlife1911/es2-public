#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static WCHAR config_path[MAX_PATH];
static void log_event(const char *s) {(void)s;}
#include "../rng_hooks.h"
#include "../live_coverage.h"
enum { BRANCH_SITE_LINES = 0
#define BRANCH(N,A,L,C,T) +1
#define PROVED(N,A,E,R)
#include "tools/reference-game/invitro/branch_sites.h"
#undef PROVED
#undef BRANCH
};
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
static void (__cdecl *registered_exit)(void);
static int exit_code,register_failure;
static void __cdecl fake_exit(int code) {exit_code=code;}
static int __cdecl fake_atexit(void (__cdecl *callback)(void)) {registered_exit=callback;return register_failure;}
static int config_report(void) {
    WCHAR temporary[MAX_PATH];CHECK(GetTempPathW(MAX_PATH,temporary));
    swprintf(config_path,MAX_PATH,L"%lscoverage-config-%lu.ini",temporary,(unsigned long)GetCurrentProcessId());
    DeleteFileW(config_path);CHECK(live_configuration(config_path)==0 && !lc_active && !lc_handler);
    CHECK(WritePrivateProfileStringW(L"coverage",L"enabled",L"garbage",config_path));CHECK(live_configuration(config_path)==-1);
    CHECK(WritePrivateProfileStringW(L"coverage",L"enabled",L"1",config_path));CHECK(live_configuration(config_path)==-1);
    WCHAR report[MAX_PATH];swprintf(report,MAX_PATH,L"%lscoverage-report-%lu.json",temporary,(unsigned long)GetCurrentProcessId());DeleteFileW(report);
    CHECK(WritePrivateProfileStringW(L"coverage",L"report",report,config_path));
    CHECK(WritePrivateProfileStringW(L"coverage",L"hooks",L"rng_next,track_count",config_path));CHECK(live_configuration(config_path)==1 && live_selected[0] && live_selected[ES2_HOOK_INDEX_track_count] && !live_selected[1]);
    CHECK(WritePrivateProfileStringW(L"coverage",L"hooks",L"rng_next,rng_next",config_path));CHECK(live_configuration(config_path)==-1);
    CHECK(WritePrivateProfileStringW(L"coverage",L"hooks",L"unknown",config_path));CHECK(live_configuration(config_path)==-1);
    CHECK(WritePrivateProfileStringW(L"coverage",L"hooks",L"all,",config_path));CHECK(live_configuration(config_path)==-1);
    CHECK(WritePrivateProfileStringW(L"coverage",L"hooks",L"ground_effects",config_path));
    CHECK(live_configuration(config_path)==1 && live_selected[ES2_HOOK_INDEX_ground_effects] && !live_selected[ES2_HOOK_INDEX_ground_update]);
    unsigned effects_sites=0;for(unsigned i=0;i<LIVE_SITE_COUNT;++i)effects_sites+=live_all_sites[i].owner==ES2_HOOK_INDEX_ground_effects;
    /* The live table holds every BRANCH line of branch_sites.h once (counted
     * from the same file, no hand-kept total), and no site twice. */
    CHECK(effects_sites==30 && LIVE_SITE_COUNT==BRANCH_SITE_LINES);
    for(unsigned i=0;i<LIVE_SITE_COUNT;++i)for(unsigned j=i+1;j<LIVE_SITE_COUNT;++j)CHECK(live_all_sites[i].address!=live_all_sites[j].address);
    CHECK(WritePrivateProfileStringW(L"coverage",L"hooks",L"all",config_path));CHECK(live_configuration(config_path)==1);
    CHECK(live_selected[ES2_HOOK_INDEX_ground_effects]);
    CHECK(WritePrivateProfileStringW(L"coverage",L"mode",L"invalid",config_path));CHECK(live_configuration(config_path)==-1);
    CHECK(WritePrivateProfileStringW(L"coverage",L"mode",L"count",config_path));CHECK(live_configuration(config_path)==1);
    CHECK(WritePrivateProfileStringW(L"coverage",L"frame_limit",L"4294967296",config_path));CHECK(live_configuration(config_path)==-1);
    CHECK(WritePrivateProfileStringW(L"coverage",L"frame_limit",L"2",config_path));CHECK(live_configuration(config_path)==1);
    CHECK(WritePrivateProfileStringW(L"coverage",L"first_hit_ticks",L"2",config_path));CHECK(live_configuration(config_path)==-1);
    CHECK(WritePrivateProfileStringW(L"coverage",L"first_hit_ticks",L"1",config_path));CHECK(live_configuration(config_path)==1 && lc_track_ticks);
    BYTE *code=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);CHECK(code);code[0]=0x74;code[1]=2;code[4]=0xc3;
    memset(live_sites,0,sizeof(live_sites));live_sites[0].address=(uintptr_t)code;live_sites[0].target=(uintptr_t)code+4;live_sites[0].length=2;
    CHECK(lc_install(live_sites,1,code,4096,NULL,0,LC_COUNT));live_enabled=TRUE;live_install_thread=GetCurrentThreadId();
    live_sites[0].fall=UINT64_C(4294967297);live_sites[0].taken=123;
    live_sites[0].first_fall_tick=-1;live_sites[0].first_taken_tick=0;
    swprintf(live_temp_path,MAX_PATH,L"%ls.tmp",report);DeleteFileW(live_temp_path);
    CHECK(live_report());live_coverage_tick(1);CHECK(live_complete && !lc_active && code[0]==0x74);
    FILE *f=_wfopen(report,L"rb");CHECK(f);static char text[65536];size_t n=fread(text,1,sizeof(text)-1,f);fclose(f);text[n]=0;
    CHECK(strstr(text,"\"fall\":4294967297") && strstr(text,"\"complete\":true") && strstr(text,"\"reason\":\"frame_limit\"") && !strstr(text,"saved"));
    CHECK(strstr(text,"\"first_hit_ticks\":true") && strstr(text,"\"first_fall_tick\":-1") && strstr(text,"\"first_taken_tick\":0") && lc_last_tick==1);
    f=_wfopen(live_temp_path,L"wb");CHECK(f);fputs("occupied",f);fclose(f);CHECK(!live_report());
    f=_wfopen(report,L"rb");CHECK(f);static char after[65536];size_t m=fread(after,1,sizeof(after),f);fclose(f);CHECK(m==n && !memcmp(text,after,n));
    DeleteFileW(live_temp_path);
    DWORD *slots=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);CHECK(slots);
    slots[0]=(DWORD)(uintptr_t)fake_exit;slots[1]=(DWORD)(uintptr_t)fake_atexit;
    register_failure=1;CHECK(!live_install_exit_slots(slots,slots+1) && slots[0]==(DWORD)(uintptr_t)fake_exit);
    register_failure=0;CHECK(live_install_exit_slots(slots,slots+1) && registered_exit==live_coverage_on_exit);
    slots[0]=123;CHECK(!live_restore_exit() && slots[0]==123);slots[0]=(DWORD)(uintptr_t)live_coverage_exit;
    live_complete=FALSE;CHECK(lc_install(live_sites,1,code,4096,NULL,0,LC_COUNT));live_coverage_exit(73);
    CHECK(exit_code==73 && live_complete && !strcmp(live_reason,"exit") && slots[0]==(DWORD)(uintptr_t)fake_exit && !live_exit_slot);
    live_complete=FALSE;CHECK(lc_install(live_sites,1,code,4096,NULL,0,LC_COUNT));registered_exit();CHECK(live_complete && !lc_active);
    live_complete=FALSE;CHECK(lc_install(live_sites,1,code,4096,NULL,0,LC_COUNT));lc_other_thread=99;
    live_coverage_shutdown("exit");CHECK(!live_complete && lc_active && lc_error==6);lc_other_thread=0;CHECK(lc_uninstall());
    live_enabled=FALSE;VirtualFree(slots,0,MEM_RELEASE);
    DeleteFileW(report);DeleteFileW(live_temp_path);DeleteFileW(config_path);VirtualFree(code,0,MEM_RELEASE);
    puts("PASS: absent/invalid/selected coverage configuration, uint64 report, final frame freeze, atomic failure keeps previous report");return 0;
}
static int write_text(const WCHAR *path,const char *text) {
    FILE *f=_wfopen(path,L"wb");if(!f)return 0;fputs(text,f);fclose(f);return 1;
}
static int edge_config(void) {
    /* EDGE lines: sorted, unique, each one a BRANCH site; every BRANCH site has one
     * (a stale table would only cost speed, but the committed one is current). */
    for(unsigned i=0;i+1<LIVE_EDGE_COUNT;++i)CHECK(live_edges[i].va<live_edges[i+1].va);
    for(unsigned i=0;i<LIVE_EDGE_COUNT;++i){unsigned j=0;while(j<LIVE_SITE_COUNT && live_all_sites[j].address!=live_edges[i].va)++j;CHECK(j<LIVE_SITE_COUNT);}
    for(unsigned i=0;i<LIVE_SITE_COUNT;++i)CHECK(live_edge_find(live_all_sites[i].address));
    WCHAR temporary[MAX_PATH],known[MAX_PATH],report[MAX_PATH];CHECK(GetTempPathW(MAX_PATH,temporary));
    swprintf(config_path,MAX_PATH,L"%lscoverage-edge-%lu.ini",temporary,(unsigned long)GetCurrentProcessId());DeleteFileW(config_path);
    swprintf(known,MAX_PATH,L"%lscoverage-known-%lu.txt",temporary,(unsigned long)GetCurrentProcessId());
    swprintf(report,MAX_PATH,L"%lscoverage-edge-report-%lu.json",temporary,(unsigned long)GetCurrentProcessId());DeleteFileW(report);
    CHECK(WritePrivateProfileStringW(L"coverage",L"enabled",L"1",config_path)&&WritePrivateProfileStringW(L"coverage",L"report",report,config_path)&&WritePrivateProfileStringW(L"coverage",L"hooks",L"rng_next",config_path));
    CHECK(WritePrivateProfileStringW(L"coverage",L"mode",L"edge",config_path));CHECK(live_configuration(config_path)==1 && lc_mode==LC_EDGE && !live_known_outcomes);
    char text[256];uint32_t first=(uint32_t)live_all_sites[0].address,second=(uint32_t)live_all_sites[1].address;
    snprintf(text,sizeof(text),"# baseline\r\n0x%08x 1 0\r\n\r\n0x%08x 1 1\n",first,second);CHECK(write_text(known,text));
    CHECK(WritePrivateProfileStringW(L"coverage",L"known",known,config_path));
    CHECK(live_configuration(config_path)==1 && live_known[0]==1 && live_known[1]==3 && live_known[2]==0 && live_known_outcomes==3);
    const char *bad[]={"0x00401000 1 0\n","0x%08x 1 0\n0x%08x 0 1\n","0x%08x 2 0\n","0x%08x 1\n","0x%08x 1 0 x\n"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {snprintf(text,sizeof(text),bad[i],first,first);CHECK(write_text(known,text));CHECK(live_configuration(config_path)==-1 && !live_known_outcomes);}
    snprintf(text,sizeof(text),"0x%08x 1 0\n",first);CHECK(write_text(known,text));
    CHECK(WritePrivateProfileStringW(L"coverage",L"mode",L"saturate",config_path));CHECK(live_configuration(config_path)==-1); /* known needs edge mode */
    CHECK(WritePrivateProfileStringW(L"coverage",L"known",L"C:\\missing\\known.txt",config_path)&&WritePrivateProfileStringW(L"coverage",L"mode",L"edge",config_path));CHECK(live_configuration(config_path)==-1);
    /* Report: mode, armed roles, baseline size and per-site known outcomes. */
    BYTE *code=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);CHECK(code);code[0]=0x74;code[1]=2;code[2]=0xc3;code[4]=0xc3;
    memset(live_sites,0,sizeof(live_sites));live_sites[0].address=(uintptr_t)code;live_sites[0].target=(uintptr_t)code+4;live_sites[0].length=2;live_sites[0].known[0]=1;live_sites[0].exclusive[1]=1;
    live_known_outcomes=1;CHECK(lc_install(live_sites,1,code,4096,NULL,0,LC_EDGE) && live_sites[0].roles==LC_ROLE_TAKEN);
    live_enabled=TRUE;live_complete=FALSE;live_install_thread=GetCurrentThreadId();live_selected[0]=TRUE;
    wcscpy(live_report_path,report);swprintf(live_temp_path,MAX_PATH,L"%ls.tmp",report);DeleteFileW(live_temp_path);
    CHECK(live_report());live_coverage_shutdown("exit");CHECK(live_complete && !lc_active && code[4]==0xc3);
    FILE *f=_wfopen(report,L"rb");CHECK(f);static char json[65536];size_t n=fread(json,1,sizeof(json)-1,f);fclose(f);json[n]=0;
    CHECK(strstr(json,"\"mode\":\"edge\"") && strstr(json,"\"edge_roles\":{\"jcc\":0,\"redirect\":0,\"fall\":0,\"taken\":1},\"known_outcomes\":1,\"hooks\":["));
    CHECK(strstr(json,"\"known_fall\":true,\"known_taken\":false") && strstr(json,"\"restored\":true") && strstr(json,"\"complete\":true"));
    live_enabled=FALSE;live_known_outcomes=0;memset(live_known,0,sizeof(live_known));
    DeleteFileW(report);DeleteFileW(known);DeleteFileW(config_path);VirtualFree(code,0,MEM_RELEASE);
    printf("PASS: %u EDGE lines sorted, one per BRANCH site; edge mode, known file (valid, unknown, duplicate, range, short, trailing, missing, wrong mode); edge report fields\n",(unsigned)LIVE_EDGE_COUNT);
    return 0;
}
static int reference(void) {
    const char *root=getenv("ES2_DATA_ROOT");if(!root||!*root){puts("SKIP: ES2_DATA_ROOT unset");return 0;}
    char path[4096];snprintf(path,sizeof(path),"%s/VER95/DBSIM.EXE",root);FILE *f=fopen(path,"rb");
    if(!f){snprintf(path,sizeof(path),"%s/extracted/cd/VER95/DBSIM.EXE",root);f=fopen(path,"rb");}CHECK(f);
    CHECK(!fseek(f,0,SEEK_END));long size=ftell(f);CHECK(size==724512);rewind(f);BYTE *bytes=malloc((size_t)size);CHECK(bytes&&fread(bytes,1,(size_t)size,f)==(size_t)size);fclose(f);
    HCRYPTPROV provider=0;HCRYPTHASH hash=0;CHECK(CryptAcquireContextW(&provider,NULL,NULL,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)&&CryptCreateHash(provider,CALG_SHA_256,0,0,&hash)&&CryptHashData(hash,bytes,(DWORD)size,0));
    BYTE digest[32];DWORD length=32;CHECK(CryptGetHashParam(hash,HP_HASHVAL,digest,&length,0));char hex[65];for(unsigned i=0;i<32;++i)sprintf(hex+2*i,"%02x",digest[i]);CHECK(!strcmp(hex,LC_MODULE_HASH));CryptDestroyHash(hash);CryptReleaseContext(provider,0);
    CHECK(bytes[0]=='M'&&bytes[1]=='Z');uint32_t pe=lc_u32(bytes+0x3c);CHECK(pe<(uint32_t)size-256 && !memcmp(bytes+pe,"PE\0\0",4));
    unsigned sections=bytes[pe+6]|((unsigned)bytes[pe+7]<<8),optional=bytes[pe+20]|((unsigned)bytes[pe+21]<<8);uint32_t table=pe+24+optional;CHECK(table<(uint32_t)size && sections<=((uint32_t)size-table)/40);
    BYTE *image=VirtualAlloc(NULL,RNG_HOOK_IMAGE_SIZE,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);CHECK(image);
    DWORD headers=lc_u32(bytes+pe+84);CHECK(headers<=(DWORD)size && headers<=RNG_HOOK_IMAGE_SIZE);memcpy(image,bytes,headers);
    for(unsigned i=0;i<sections;++i){BYTE *h=bytes+table+40*i;uint32_t rva=lc_u32(h+12),n=lc_u32(h+16),offset=lc_u32(h+20);CHECK(rva<=RNG_HOOK_IMAGE_SIZE&&n<=RNG_HOOK_IMAGE_SIZE-rva&&offset<=(uint32_t)size&&n<=(uint32_t)size-offset);memcpy(image+rva,bytes+offset,n);}
    lc_range ranges[FUNCTION_HOOK_COUNT];for(unsigned i=0;i<FUNCTION_HOOK_COUNT;++i){const es2_hook_descriptor *d=function_hook_descriptor(i);ranges[i]=(lc_range){(uintptr_t)image+d->rva,d->relocate_size};}
    for(unsigned i=0;i<LIVE_SITE_COUNT;++i){live_sites[i]=live_all_sites[i];live_sites[i].address=(uintptr_t)image+live_all_sites[i].address-0x400000;live_sites[i].target=(uintptr_t)image+live_all_sites[i].target-0x400000;}
    CHECK(lc_validate(live_sites,LIVE_SITE_COUNT,image,RNG_HOOK_IMAGE_SIZE,ranges,FUNCTION_HOOK_COUNT));
    CHECK(live_import_slot(image,RNG_HOOK_IMAGE_SIZE,"__exit") && live_import_slot(image,RNG_HOOK_IMAGE_SIZE,"_atexit"));
    CHECK(!live_import_slot(image,100,"__exit") && !live_import_slot(image,RNG_HOOK_IMAGE_SIZE,"missing"));
    CHECK(!live_import(image,RNG_HOOK_IMAGE_SIZE,"__exit")); /* original runtime is deliberately not loaded */
    printf("PASS: %u original sites decode after same-buffer SHA-256 gate; no overlap/prologue overlap; no original bytes written\n",(unsigned)LIVE_SITE_COUNT);
    /* Edge mode on the private copy: the generated table installs, every
     * redirect byte faults, and uninstall gives back every byte of the copy. */
    BYTE *before=malloc(RNG_HOOK_IMAGE_SIZE);CHECK(before);memcpy(before,image,RNG_HOOK_IMAGE_SIZE);
    unsigned table_fall=0,table_traps=0,table_taken=0,wide=0;
    /* The generator's PROLOGUE_WINDOW (16) covers every relocated prologue. */
    for(unsigned i=0;i<FUNCTION_HOOK_COUNT;++i)CHECK(function_hook_descriptor(i)->relocate_size<=16);
    for(unsigned i=0;i<LIVE_SITE_COUNT;++i){lc_site *s=&live_sites[i];const live_edge *e=live_edge_find(live_all_sites[i].address);CHECK(e);
        s->exclusive[0]=e->fall;s->exclusive[1]=e->taken;s->trap=e->trap?(uintptr_t)image+e->trap-0x400000:0;s->known[0]=s->known[1]=0;
        table_fall+=s->exclusive[0];table_taken+=s->exclusive[1];table_traps+=!!s->trap;wide+=s->length==6;
        if(s->trap)CHECK(lc_is_trap_byte(*(BYTE *)s->trap) && s->trap>=s->address+2-128 && s->trap<=s->address+2+127);}
    CHECK(lc_install(live_sites,LIVE_SITE_COUNT,image,RNG_HOOK_IMAGE_SIZE,ranges,FUNCTION_HOOK_COUNT,LC_EDGE));
    /* Every redirect byte of the table survives the install checks. */
    unsigned rel8_redirects=lc_edge_armed[1]-wide;
    CHECK(lc_edge_armed[1]>=wide && rel8_redirects==table_traps && lc_edge_armed[2]<=table_fall);
    CHECK(lc_uninstall() && !memcmp(before,image,RNG_HOOK_IMAGE_SIZE));
    printf("PASS: edge mode installs on the copy: jcc %u, redirect %u (rel8 %u of %u bytes, rel32 %u), fall %u of %u, taken %u of %u exclusive; copy restored\n",
        lc_edge_armed[0],lc_edge_armed[1],rel8_redirects,table_traps,wide,lc_edge_armed[2],table_fall,lc_edge_armed[3],table_taken);
    free(before);VirtualFree(image,0,MEM_RELEASE);free(bytes);return 0;
}
int main(int argc,char **argv){return argc>1 && !strcmp(argv[1],"reference")?reference():config_report() || edge_config();}
