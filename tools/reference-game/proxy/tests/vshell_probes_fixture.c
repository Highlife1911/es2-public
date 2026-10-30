/* vshell_probes.h on own code, own INI files, own import slots and the
 * generated VSHELL inventory. The reference mode reads VER95\VSHELL.EXE from
 * ES2_DATA_ROOT (file bytes, and the file mapped as an image resource); none
 * of its instructions is executed. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static WCHAR config_path[MAX_PATH];
static void log_event(const char *s) {(void)s;}
#include "../mission_map_guard.h"
#include "../vshell_probes.h"
#define CHECK(c) do {if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);exit(1);}}while(0)

/* 600 own functions (more than EP_MAX_ENTRIES): push ebp; pop ebp; ret; nop. */
#define OWN_FUNCTIONS 600u
static BYTE *own_code(void) {
    BYTE *p=VirtualAlloc(NULL,4*OWN_FUNCTIONS,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);CHECK(p);
    for(unsigned i=0;i<OWN_FUNCTIONS;++i){p[4*i]=0x55;p[4*i+1]=0x5d;p[4*i+2]=0xc3;p[4*i+3]=0x90;}
    CHECK(FlushInstructionCache(GetCurrentProcess(),p,4*OWN_FUNCTIONS));return p;
}
static void own_sites(ep_site *sites,BYTE *code) {
    for(unsigned i=0;i<OWN_FUNCTIONS;++i) {ep_site *s=&sites[i];memset(s,0,sizeof(*s));
        s->patch.address=(uintptr_t)code+4*i;s->patch.length=1;s->patch.saved[0]=0x55;s->kind=EP_PUSH;}
}
static void call_all(BYTE *code,unsigned times) {
    for(unsigned t=0;t<times;++t)for(unsigned i=0;i<OWN_FUNCTIONS;i+=1+(i%3)) {
        SetLastError(0x2468);((void (__cdecl *)(void))(code+4*i))();CHECK(GetLastError()==0x2468);}
}
static unsigned expected_count(unsigned i,unsigned times) {
    unsigned hit=0;for(unsigned j=0;j<OWN_FUNCTIONS;j+=1+(j%3))if(j==i)hit=1;return hit*times;
}
static void temp_file(WCHAR *out,const WCHAR *suffix) {
    WCHAR temp[MAX_PATH];CHECK(GetTempPathW(MAX_PATH,temp));
    swprintf(out,MAX_PATH,L"%lsvshell-probes-%lu%ls",temp,(unsigned long)GetCurrentProcessId(),suffix);
}
static char *read_text(const WCHAR *path) {
    FILE *f=_wfopen(path,L"rb");CHECK(f);static char text[1<<16];size_t n=fread(text,1,sizeof(text)-1,f);fclose(f);text[n]=0;return text;
}

static int exit_value=-1,atexit_calls;
static void (__cdecl *registered)(void);
static int __cdecl fake_atexit(void (__cdecl *f)(void)){registered=f;++atexit_calls;return 0;}
static void __cdecl fake_exit(int n){exit_value=n;}
static int __cdecl fake_startup(int a,int b){return a*10+b+1000*(int)vsp_arm_calls;}

static void engine(void) {
    BYTE *code=own_code();
    static ep_site sites[OWN_FUNCTIONS];own_sites(sites,code);
    /* More than one ep_validate chunk; count mode, exact counts. */
    CHECK(OWN_FUNCTIONS>EP_MAX_ENTRIES);
    CHECK(vsp_install_sites(sites,OWN_FUNCTIONS,code,4*OWN_FUNCTIONS,NULL,0,LC_COUNT,1));
    for(unsigned i=0;i<OWN_FUNCTIONS;++i)CHECK(code[4*i]==0xcc);
    call_all(code,7);
    for(unsigned i=0;i<OWN_FUNCTIONS;++i)CHECK(sites[i].count==expected_count(i,7));
    CHECK(ep_uninstall()&&!ep_handler);for(unsigned i=0;i<OWN_FUNCTIONS;++i)CHECK(code[4*i]==0x55);
    /* Saturate after 2: every entered site restored, the rest still armed. */
    own_sites(sites,code);CHECK(vsp_install_sites(sites,OWN_FUNCTIONS,code,4*OWN_FUNCTIONS,NULL,0,LC_SATURATE,2));
    call_all(code,5);
    for(unsigned i=0;i<OWN_FUNCTIONS;++i) {unsigned e=expected_count(i,5);
        CHECK(sites[i].count==(e?2u:0u)&&(code[4*i]==(e?0x55:0xcc))&&sites[i].patch.restored==(e?1:0));}
    CHECK(ep_uninstall());
    /* Refusals leave every byte original: an overlap across the chunk border,
     * an excluded range, an unsorted list, a wrong opcode, a busy engine. */
    own_sites(sites,code);sites[EP_MAX_ENTRIES].patch.address=sites[EP_MAX_ENTRIES-1].patch.address;
    CHECK(!vsp_install_sites(sites,OWN_FUNCTIONS,code,4*OWN_FUNCTIONS,NULL,0,LC_COUNT,1));
    own_sites(sites,code);lc_range ex={(uintptr_t)code+4*550+2,4};
    CHECK(!vsp_install_sites(sites,OWN_FUNCTIONS,code,4*OWN_FUNCTIONS,&ex,1,LC_COUNT,1));
    own_sites(sites,code);ep_site t=sites[100];sites[100]=sites[101];sites[101]=t;
    CHECK(!vsp_install_sites(sites,OWN_FUNCTIONS,code,4*OWN_FUNCTIONS,NULL,0,LC_COUNT,1));
    own_sites(sites,code);sites[599].patch.saved[0]=0x53;
    CHECK(!vsp_install_sites(sites,OWN_FUNCTIONS,code,4*OWN_FUNCTIONS,NULL,0,LC_COUNT,1));
    for(unsigned i=0;i<OWN_FUNCTIONS;++i)CHECK(code[4*i]==0x55);CHECK(!ep_active&&!ep_handler);
    own_sites(sites,code);CHECK(vsp_install_sites(sites,10,code,4*OWN_FUNCTIONS,NULL,0,LC_COUNT,1));
    CHECK(!vsp_install_sites(sites+10,10,code,4*OWN_FUNCTIONS,NULL,0,LC_COUNT,1));CHECK(ep_uninstall());

    /* Exit paths: atexit registration, __exit slot taken and given back,
     * the report written once with the counts. */
    own_sites(sites,code);vsp_sites=sites;vsp_count=OWN_FUNCTIONS;vsp_mode=LC_COUNT;vsp_limit=1;
    temp_file(vsp_path,L".json");swprintf(vsp_temp,MAX_PATH,L"%ls.tmp",vsp_path);
    DWORD slots[2]={(DWORD)(uintptr_t)fake_exit,(DWORD)(uintptr_t)fake_atexit};
    CHECK(vsp_install_exit(slots,slots+1)&&atexit_calls==1&&registered==vsp_on_exit&&slots[0]==(DWORD)(uintptr_t)vsp_exit);
    CHECK(vsp_install_sites(sites,OWN_FUNCTIONS,code,4*OWN_FUNCTIONS,NULL,0,LC_COUNT,1));
    vsp_install_thread=GetCurrentThreadId();vsp_state=VSP_ARMED;
    call_all(code,3);
    vsp_exit(73);
    CHECK(exit_value==73&&vsp_state==VSP_DONE&&vsp_complete&&!ep_active&&slots[0]==(DWORD)(uintptr_t)fake_exit&&!vsp_exit_slot);
    for(unsigned i=0;i<OWN_FUNCTIONS;++i)CHECK(code[4*i]==0x55);
    char *text=read_text(vsp_path);
    CHECK(strstr(text,"\"module_sha256\":\"" VSP_MODULE_HASH "\"")&&strstr(text,"\"complete\":true")&&strstr(text,"\"reason\":\"immediate_exit\"")&&strstr(text,"\"sites\":600"));
    char row[96];snprintf(row,sizeof(row),"{\"va\":\"0x%08lx\",\"kind\":0,\"count\":3,",(unsigned long)(uintptr_t)code);CHECK(strstr(text,row));
    snprintf(row,sizeof(row),"{\"va\":\"0x%08lx\",\"kind\":0,\"count\":0,",(unsigned long)(uintptr_t)(code+8));CHECK(strstr(text,row));
    registered();CHECK(vsp_state==VSP_DONE);   /* the atexit call after an immediate end: no second report */
    DeleteFileW(vsp_path);
    /* Normal end through the atexit callback. */
    own_sites(sites,code);slots[0]=(DWORD)(uintptr_t)fake_exit;CHECK(vsp_install_exit(slots,slots+1));
    CHECK(vsp_install_sites(sites,OWN_FUNCTIONS,code,4*OWN_FUNCTIONS,NULL,0,LC_COUNT,1));vsp_state=VSP_ARMED;
    call_all(code,1);registered();
    CHECK(vsp_state==VSP_DONE&&slots[0]==(DWORD)(uintptr_t)fake_exit&&!ep_active);
    text=read_text(vsp_path);CHECK(strstr(text,"\"reason\":\"exit\"")&&strstr(text,"\"complete\":true"));
    DeleteFileW(vsp_path);
    puts("PASS vshell probes engine: 600 sites over two validation chunks, exact counts, saturation, transactional refusals, __exit and atexit reports");

    /* The startup thunk: arm runs once, the startup slot gets its original
     * value back, the call reaches the original with its arguments and return. */
    DWORD startup=(DWORD)(uintptr_t)vsp_startup_entry;
    vsp_state=VSP_OFF;vsp_startup_slot=&startup;vsp_startup_original=(DWORD)(uintptr_t)fake_startup;vsp_arm_calls=0;
    int r=((int (__cdecl *)(int,int))(uintptr_t)vsp_startup_entry)(3,4);
    CHECK(r==1034&&vsp_arm_calls==1&&startup==(DWORD)(uintptr_t)fake_startup&&!vsp_startup_slot&&vsp_state==VSP_OFF);
    VirtualFree(code,0,MEM_RELEASE);
    puts("PASS vshell probes startup thunk: registers, arguments and return kept, slot restored");
}

static void configuration(void) {
    WCHAR ini[MAX_PATH],report[MAX_PATH];temp_file(ini,L".ini");temp_file(report,L"-report.json");
    vsp_sites=malloc(VSP_STARTS*sizeof(ep_site));CHECK(vsp_sites);
    CHECK(VSP_STARTS>EP_MAX_ENTRIES&&sizeof(vshell_probe_entry_kinds)/sizeof(vshell_probe_entry_kinds[0])<VSP_STARTS);
    /* Absent section and enabled=0: off. */
    CHECK(vsp_configuration(ini)==0);
    CHECK(WritePrivateProfileStringW(L"vshell_probes",L"enabled",L"0",ini)&&vsp_configuration(ini)==0);
    CHECK(WritePrivateProfileStringW(L"vshell_probes",L"enabled",L"yes",ini)&&vsp_configuration(ini)==-1);
    CHECK(WritePrivateProfileStringW(L"vshell_probes",L"enabled",L"1",ini)&&vsp_configuration(ini)==-1);   /* no report */
    CHECK(WritePrivateProfileStringW(L"vshell_probes",L"report",report,ini));
    CHECK(vsp_configuration(ini)==1&&vsp_count==VSP_STARTS&&vsp_mode==LC_COUNT&&vsp_limit==1);
    for(unsigned i=0;i<vsp_count;++i) {const ep_site *s=&vsp_sites[i];
        CHECK(s->patch.address==vshell_probe_function_starts[i]&&(!i||vshell_probe_function_starts[i-1]<s->patch.address));
        CHECK(s->kind==EP_PUSH?s->patch.saved[0]==vsp_opcode(s->patch.address)&&s->patch.saved[0]:vsp_kind_of(s->patch.address)==s->kind&&s->kind<EP_KINDS);}
    CHECK(WritePrivateProfileStringW(L"vshell_probes",L"mode",L"saturate",ini)&&WritePrivateProfileStringW(L"vshell_probes",L"saturate_after",L"4",ini));
    CHECK(vsp_configuration(ini)==1&&vsp_mode==LC_SATURATE&&vsp_limit==4);
    CHECK(WritePrivateProfileStringW(L"vshell_probes",L"saturate_after",L"0",ini)&&vsp_configuration(ini)==-1);
    CHECK(WritePrivateProfileStringW(L"vshell_probes",L"saturate_after",L"1",ini));
    CHECK(WritePrivateProfileStringW(L"vshell_probes",L"mode",L"edge",ini)&&vsp_configuration(ini)==-1);
    CHECK(WritePrivateProfileStringW(L"vshell_probes",L"mode",L"count",ini));
    /* An explicit list: a kind row and a push row, sorted; any push row of the
     * first table is accepted; unknown, repeated and empty entries refused. */
    WCHAR list[160];uint32_t kind_va=vshell_probe_entry_kinds[3].va,push_va=0,plain=0;
    for(unsigned i=0;i<VSP_STARTS&&!push_va;++i)if(vsp_opcode(vshell_probe_function_starts[i]))push_va=vshell_probe_function_starts[i];
    for(unsigned i=0;i<sizeof(vshell_probe_instructions)/sizeof(vshell_probe_instructions[0])&&!plain;++i) {
        uint32_t va=vshell_probe_instructions[i].va;BOOL start=FALSE;
        for(unsigned j=0;j<VSP_STARTS;++j)if(vshell_probe_function_starts[j]==va)start=TRUE;if(!start)plain=va;}
    CHECK(plain);
    swprintf(list,160,L"0x%08lx, %08lX,%lx",(unsigned long)kind_va,(unsigned long)push_va,(unsigned long)plain);
    CHECK(WritePrivateProfileStringW(L"vshell_probes",L"entries",list,ini)&&vsp_configuration(ini)==1&&vsp_count==3);
    CHECK(push_va&&vsp_sites[0].patch.address<vsp_sites[1].patch.address&&vsp_sites[1].patch.address<vsp_sites[2].patch.address);
    for(unsigned i=0;i<3;++i) {const ep_site *s=&vsp_sites[i];
        CHECK(s->patch.address==kind_va?s->kind==vshell_probe_entry_kinds[3].kind:(s->patch.address==push_va||s->patch.address==plain)&&s->kind==EP_PUSH);}
    const WCHAR *bad[]={L"00400001",L"fffffffff",L"",L"0x401074,",L",0x401074",L"0x401074 x"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i)CHECK(WritePrivateProfileStringW(L"vshell_probes",L"entries",bad[i],ini)&&vsp_configuration(ini)==-1);
    swprintf(list,160,L"%lx,%lx",(unsigned long)push_va,(unsigned long)push_va);
    CHECK(WritePrivateProfileStringW(L"vshell_probes",L"entries",list,ini)&&vsp_configuration(ini)==-1);
    /* Exclusions (the mission-map guard's ranges): "all" drops, a list refuses. */
    vsp_excluded[0]=(lc_range){push_va,1};vsp_nexcluded=1;
    CHECK(WritePrivateProfileStringW(L"vshell_probes",L"entries",L"all",ini)&&vsp_configuration(ini)==1&&vsp_count==VSP_STARTS-1&&vsp_sites[0].patch.address!=push_va);
    swprintf(list,160,L"%lx",(unsigned long)push_va);
    CHECK(WritePrivateProfileStringW(L"vshell_probes",L"entries",list,ini)&&vsp_configuration(ini)==-1);
    /* The guard's own ranges when [compat] mission_map_lifetime=1. */
    wcscpy(vsp_guard_ini,ini);vsp_guard_exclusions();CHECK(vsp_nexcluded==0);
    CHECK(WritePrivateProfileStringW(L"compat",L"mission_map_lifetime",L"1",ini));vsp_guard_exclusions();
    CHECK(vsp_nexcluded==5&&vsp_excluded[1].start==0x0040bc48u&&vsp_excluded[4].start==0x00445095u);
    CHECK(WritePrivateProfileStringW(L"vshell_probes",L"entries",L"all",ini)&&vsp_configuration(ini)==1);
    unsigned dropped=VSP_STARTS-vsp_count;
    for(unsigned i=0;i<vsp_count;++i)CHECK(!vsp_excluded_site((uint32_t)vsp_sites[i].patch.address,vsp_sites[i].kind));
    vsp_nexcluded=0;vsp_guard_ini[0]=0;
    /* {pid} in the report path. */
    WCHAR expanded[MAX_PATH];wcscpy(expanded,L"C:\\x\\probes-{pid}.json");CHECK(vsp_expand_pid(expanded,4321)&&!wcscmp(expanded,L"C:\\x\\probes-4321.json"));
    wcscpy(expanded,L"C:\\x\\probes.json");CHECK(vsp_expand_pid(expanded,4321)&&!wcscmp(expanded,L"C:\\x\\probes.json"));
    wcscpy(expanded,L"{pid}-{pid}");CHECK(!vsp_expand_pid(expanded,7));
    /* Attach in a process that is not VSHELL.EXE: nothing changes. */
    install_vshell_probes(NULL);CHECK(vsp_state==VSP_OFF&&!vsp_startup_slot);
    DeleteFileW(ini);free(vsp_sites);vsp_sites=NULL;
    printf("PASS vshell probes configuration: %u starts by default, strict keys, lists, exclusions (guard drops %u), attach outside VSHELL inert\n",(unsigned)VSP_STARTS,dropped);
}

static void reference(void) {
    const char *root=getenv("ES2_DATA_ROOT");if(!root||!*root){puts("SKIP: ES2_DATA_ROOT unset");return;}
    char path[4096];snprintf(path,sizeof(path),"%s/VER95/VSHELL.EXE",root);WCHAR wide[4096];
    CHECK(MultiByteToWideChar(CP_UTF8,0,path,-1,wide,4096));CHECK(vsp_reference_file(wide));
    FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long size=ftell(f);CHECK(size==(long)VSP_FILE_SIZE);rewind(f);
    BYTE *bytes=malloc(size);CHECK(bytes&&fread(bytes,1,size,f)==(size_t)size);fclose(f);
    DWORD pe=lc_u32(bytes+0x3c);CHECK(pe<(DWORD)size-256);CHECK(lc_u32(bytes+pe+24+56)==VSP_IMAGE_SIZE&&lc_u32(bytes+pe+24+28)==VSP_BASE);
    unsigned sections=bytes[pe+6]|bytes[pe+7]<<8,optional=bytes[pe+20]|bytes[pe+21]<<8;DWORD table=pe+24+optional;
    CHECK(table<(DWORD)size&&sections<=((DWORD)size-table)/40);
    #define CODE_AT(va,len,out) do{uint32_t rva_=(va)-VSP_BASE;out=NULL;for(unsigned j_=0;j_<sections;++j_){const BYTE *h_=bytes+table+40*j_;\
        DWORD s_=lc_u32(h_+12),n_=lc_u32(h_+16),o_=lc_u32(h_+20);CHECK(o_<=(DWORD)size&&n_<=(DWORD)size-o_);\
        if(rva_>=s_&&rva_-s_<n_){CHECK(lc_u32(h_+36)&0x20000000);CHECK(rva_-s_+(len)<=n_);out=bytes+o_+rva_-s_;break;}}CHECK(out);}while(0)
    unsigned pushes=sizeof(vshell_probe_instructions)/sizeof(vshell_probe_instructions[0]);
    for(unsigned i=0;i<pushes;++i) {const BYTE *c;CODE_AT(vshell_probe_instructions[i].va,1,c);
        CHECK(*c==vshell_probe_instructions[i].opcode&&(!i||vshell_probe_instructions[i-1].va<vshell_probe_instructions[i].va));}
    unsigned kinds=sizeof(vshell_probe_entry_kinds)/sizeof(vshell_probe_entry_kinds[0]);
    for(unsigned i=0;i<kinds;++i) {const vshell_probe_entry_kind *k=&vshell_probe_entry_kinds[i];const BYTE *c;
        CHECK(k->kind<EP_KINDS&&(!i||vshell_probe_entry_kinds[i-1].va<k->va)&&!vsp_opcode(k->va));
        CODE_AT(k->va,ep_kind_length[k->kind],c);CHECK(ep_kind_size(k->kind,c)==ep_kind_length[k->kind]);}
    unsigned push_starts=0;
    for(unsigned i=0;i<VSP_STARTS;++i) {uint32_t va=vshell_probe_function_starts[i];
        CHECK(!i||vshell_probe_function_starts[i-1]<va);CHECK(vsp_opcode(va)||vsp_kind_of(va)!=EP_KINDS);push_starts+=vsp_opcode(va)!=0;}
    CHECK(VSP_STARTS==2139&&kinds==300&&push_starts==1839&&pushes==14188);
    /* Every row of the kind table is a start (the table holds only starts). */
    for(unsigned i=0,j=0;i<kinds;++i){while(j<VSP_STARTS&&vshell_probe_function_starts[j]<vshell_probe_entry_kinds[i].va)++j;CHECK(j<VSP_STARTS&&vshell_probe_function_starts[j]==vshell_probe_entry_kinds[i].va);}
    /* The import slots the arming uses, in the file mapped as an image resource. */
    HMODULE h=LoadLibraryExW(wide,NULL,LOAD_LIBRARY_AS_IMAGE_RESOURCE);CHECK(h);BYTE *image=(BYTE *)((uintptr_t)h&~(uintptr_t)3);
    DWORD *startup=vsp_import_slot(image,VSP_IMAGE_SIZE,"__startup"),*quit=vsp_import_slot(image,VSP_IMAGE_SIZE,"__exit"),*at=vsp_import_slot(image,VSP_IMAGE_SIZE,"_atexit");
    CHECK(startup&&quit&&at&&!vsp_import_slot(image,VSP_IMAGE_SIZE,"_no_such_import"));
    CHECK((BYTE *)startup-image==0x1de2ec&&(BYTE *)quit-image==0x1de2dc&&(BYTE *)at-image==0x1de2e0);
    /* The entry ends in jmp 0x46b9be; that thunk is jmp [0x5de2ec] (__startup). */
    const BYTE *c;CODE_AT(0x401069,5,c);CHECK(c[0]==0xe9&&0x40106eu + lc_u32(c+1)==0x46b9beu);
    CODE_AT(0x46b9be,6,c);CHECK(c[0]==0xff&&c[1]==0x25&&lc_u32(c+2)==0x5de2ec);
    /* The mission-map guard's signatures are the file's bytes (its exclusions name real code). */
    for(unsigned i=0;i<sizeof(map_guard_signatures)/sizeof(map_guard_signatures[0]);++i){
        CODE_AT(VSP_BASE+map_guard_signatures[i].rva,map_guard_signatures[i].size,c);CHECK(!memcmp(c,map_guard_signatures[i].bytes,map_guard_signatures[i].size));}
    FreeLibrary(h);free(bytes);
    printf("PASS reference: %u push rows, %u kind rows, %u probed starts (%u push) match VSHELL.EXE; __startup/__exit/_atexit slots 0x5de2ec/0x5de2dc/0x5de2e0; no game code executed\n",
           pushes,kinds,(unsigned)VSP_STARTS,push_starts);
}
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    if(!strcmp(argv[1],"engine"))engine();else if(!strcmp(argv[1],"configuration"))configuration();
    else if(!strcmp(argv[1],"reference"))reference();else return 2;
    return 0;
}
