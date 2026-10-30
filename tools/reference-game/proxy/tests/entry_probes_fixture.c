/* Own executable buffer, contexts and reports. Original data is read-only in
 * reference mode; none of its instructions is executed. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static WCHAR config_path[MAX_PATH];
static void log_event(const char *s) {(void)s;}
#include "../oracle_perf.h"
#include "../rng_hooks.h"
#include "../live_coverage.h"
#include "../entry_probes.h"
#define CHECK(c) do {if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);exit(1);}}while(0)
static BYTE *own_code(void) {
    /* push ebp; mov ebp,esp; mov eax,[ebp+8]; test eax,eax;
     * je pop; mov eax,7; pop ebp; ret */
    const BYTE bytes[]={0x55,0x89,0xe5,0x8b,0x45,0x08,0x85,0xc0,0x74,0x05,0xb8,7,0,0,0,0x5d,0xc3};
    BYTE *p=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);CHECK(p);
    memcpy(p,bytes,sizeof(bytes));CHECK(FlushInstructionCache(GetCurrentProcess(),p,sizeof(bytes)));return p;
}
static void site_init(ep_site *s,BYTE *code,unsigned receiver) {
    memset(s,0,sizeof(*s));s->patch.address=(uintptr_t)code;s->patch.length=1;s->patch.saved[0]=0x55;s->receiver=receiver;
}
static DWORD __cdecl call(BYTE *code,DWORD *object) {
    SetLastError(0x3579);DWORD n=((DWORD (__cdecl *)(DWORD *))code)(object);CHECK(GetLastError()==0x3579);return n;
}
typedef struct probe_work { BYTE *code; DWORD *object; } probe_work;
static DWORD WINAPI probe_worker(void *arg) {
    probe_work *w=arg;for(unsigned i=0;i<500;++i)CHECK(call(w->code,w->object)==7);return 0;
}
static void engine(void) {
    /* EP_RECEIVERS+8 receivers: every slot fills, eight overflow. */
    enum {OBJECTS=EP_RECEIVERS+8};
    BYTE *code=own_code();DWORD objects[OBJECTS];for(unsigned i=0;i<OBJECTS;++i)objects[i]=0x70000000+4*i;
    ep_site s[2];site_init(s,code,EP_ESP4);
    CHECK(ep_install(s,1,code,4096,NULL,0,LC_COUNT,3));
    for(unsigned i=0;i<10000;++i)CHECK(call(code,objects)==7);
    CHECK(s->count==10000&&s->receivers[0].count==10000&&!s->overflow&&!s->patch.restored);
    for(unsigned i=1;i<OBJECTS;++i)CHECK(call(code,objects+i)==7);
    CHECK(call(code,NULL)==0);CHECK(s->count==10000+OBJECTS&&s->overflow==9&&s->receivers[EP_RECEIVERS-1].vtable==(LONG)objects[EP_RECEIVERS-1]);
    CHECK(ep_uninstall()&&*code==0x55&&!ep_handler);
    site_init(s,code,EP_ESP4);CHECK(ep_install(s,1,code,4096,NULL,0,LC_SATURATE,3));
    for(unsigned i=0;i<10;++i)CHECK(call(code,objects)==7);
    CHECK(s->count==3&&s->patch.restored&&*code==0x55&&s->receivers[0].count==3);
    /* Already fetched INT3 after saturation: emulate, do not double count. */
    CONTEXT context={0},expected;DWORD stack[8]={0};context.Esp=(DWORD)(uintptr_t)(stack+4);context.Ebp=0x87654321;context.EFlags=0x202;
    EXCEPTION_RECORD record={0};record.ExceptionCode=EXCEPTION_BREAKPOINT;record.ExceptionAddress=code;EXCEPTION_POINTERS e={&record,&context};
    expected=context;expected.Esp-=4;expected.Eip=(DWORD)(uintptr_t)code+1;
    CHECK(ep_exception(&e)==EXCEPTION_CONTINUE_EXECUTION&&!memcmp(&context,&expected,sizeof(context))&&stack[3]==0x87654321&&s->count==3);
    CHECK(ep_uninstall());
    site_init(s,code,EP_ESP4);CHECK(ep_install(s,1,code,4096,NULL,0,LC_COUNT,1));
    probe_work work={code,objects};HANDLE workers[3];
    for(unsigned i=0;i<3;++i){workers[i]=CreateThread(NULL,0,probe_worker,&work,0,NULL);CHECK(workers[i]);}
    CHECK(WaitForMultipleObjects(3,workers,TRUE,10000)==WAIT_OBJECT_0);
    for(unsigned i=0;i<3;++i)CloseHandle(workers[i]);
    CHECK(s->count==1500&&s->receivers[0].count==1500&&ep_other_thread);CHECK(ep_uninstall());
    /* All GPR receiver selectors, including ECX and the entry stack pointer. */
    for(unsigned receiver=EP_EAX;receiver<=EP_EDI;++receiver) {
        site_init(s,code,receiver);CHECK(ep_install(s,1,code,4096,NULL,0,LC_COUNT,1));
        memset(&context,0,sizeof(context));context.Esp=(DWORD)(uintptr_t)(stack+4);stack[4]=objects[0];
        DWORD pointer=(DWORD)(uintptr_t)objects;
        context.Eax=context.Ecx=context.Edx=context.Ebx=context.Ebp=context.Esi=context.Edi=pointer;
        CHECK(ep_exception(&e)==EXCEPTION_CONTINUE_EXECUTION&&s->count==1&&s->receivers[0].count==1&&s->receivers[0].vtable==(LONG)objects[0]);
        CHECK(ep_uninstall());
    }
    /* The return-address key is the dword at [esp] itself. */
    site_init(s,code,EP_RETADDR);CHECK(ep_install(s,1,code,4096,NULL,0,LC_COUNT,1));
    for(unsigned i=0;i<3;++i) {
        memset(&context,0,sizeof(context));context.Esp=(DWORD)(uintptr_t)(stack+4);stack[4]=i<2?0x0040123au:0x00405678u;
        CHECK(ep_exception(&e)==EXCEPTION_CONTINUE_EXECUTION);
    }
    CHECK(s->count==3&&!s->overflow&&s->receivers[0].vtable==0x0040123a&&s->receivers[0].count==2&&
          s->receivers[1].vtable==0x00405678&&s->receivers[1].count==1);
    CHECK(ep_uninstall());
    site_init(s,code,EP_RETADDR+1);CHECK(!ep_install(s,1,code,4096,NULL,0,LC_COUNT,1)&&*code==0x55);
    /* Refusals are transactional. */
    site_init(s,code,0);s[1]=s[0];CHECK(!ep_install(s,2,code,4096,NULL,0,LC_COUNT,1)&&*code==0x55);
    lc_range exclusion={(uintptr_t)code,5};CHECK(!ep_install(s,1,code,4096,&exclusion,1,LC_COUNT,1));
    s[0].patch.address++;CHECK(!ep_install(s,1,code,4096,NULL,0,LC_COUNT,1));site_init(s,code,0);
    exclusion=(lc_range){(uintptr_t)code,1};CHECK(!ep_install(s,1,code,4096,&exclusion,1,LC_COUNT,1));
    /* Every supported push opcode has exactly the same context effects. */
    for(unsigned op=0x50;op<=0x57;++op)if(op!=0x54) {
        *code=(BYTE)op;site_init(s,code,0);s->patch.saved[0]=(BYTE)op;
        CHECK(ep_install(s,1,code,4096,NULL,0,LC_COUNT,1));
        memset(&context,0xa5,sizeof(context));context.Esp=(DWORD)(uintptr_t)(stack+4);expected=context;
        DWORD value=ep_register(&context,op-0x50);expected.Esp-=4;expected.Eip=(DWORD)(uintptr_t)code+1;
        CHECK(ep_exception(&e)==EXCEPTION_CONTINUE_EXECUTION&&!memcmp(&context,&expected,sizeof(context))&&stack[3]==value);
        CHECK(ep_uninstall());
    }
    *code=0x55;site_init(s,code,0);
    for(unsigned order=0;order<2;++order) {
        lc_site branch={.address=(uintptr_t)code+8,.length=2,.condition=LC_JE,.target=(uintptr_t)code+15};
        if(order)CHECK(ep_install(s,1,code,4096,NULL,0,LC_COUNT,1));
        CHECK(lc_install(&branch,1,code,4096,NULL,0,LC_COUNT));
        if(!order)CHECK(ep_install(s,1,code,4096,NULL,0,LC_COUNT,1));
        for(unsigned i=0;i<100;++i){CHECK(call(code,objects)==7);CHECK(call(code,NULL)==0);}
        CHECK(s->count==200&&branch.fall==100&&branch.taken==100);
        CHECK(ep_uninstall()&&lc_uninstall());
    }
    VirtualFree(code,0,MEM_RELEASE);puts("PASS probes: exact hits; N=3 restoration; all receivers, ret key, all slots, overflow; stale INT3 context; refusals; both VEH orders");
}
static void (__cdecl *exit_callback)(void);
static int exit_value;
static int __cdecl fake_atexit(void (__cdecl *f)(void)){exit_callback=f;return 0;}
static void __cdecl fake_exit(int n){exit_value=n;}
static void runtime(void) {
    WCHAR temp[MAX_PATH];CHECK(GetTempPathW(MAX_PATH,temp));
    swprintf(config_path,MAX_PATH,L"%lsprobe-%lu.ini",temp,(unsigned long)GetCurrentProcessId());
    CHECK(probe_configuration(config_path)==0&&!ep_active&&!ep_handler&&!live_exit_slot&&!live_extra_tick);
    CHECK(es2_hook_configure_fast_path(config_path));
    CHECK(WritePrivateProfileStringW(L"hooks",L"fast_path",L"0",config_path));CHECK(es2_hook_configure_fast_path(config_path));
    CHECK(WritePrivateProfileStringW(L"hooks",L"fast_path",L"false",config_path));CHECK(!es2_hook_configure_fast_path(config_path));
    WCHAR list[128];swprintf(list,128,L"%08lx:rcv=ecx",(unsigned long)probe_instructions[0].va);CHECK(probe_parse_entries(list));
    swprintf(list,128,L"%08lx:rcv=ret",(unsigned long)probe_instructions[0].va);CHECK(probe_parse_entries(list)&&probe_sites[0].receiver==EP_RETADDR);
    swprintf(list,128,L"%08lx:rcv=rets",(unsigned long)probe_instructions[0].va);CHECK(!probe_parse_entries(list));
    swprintf(list,128,L"%08lx,%08lx",(unsigned long)probe_instructions[0].va,(unsigned long)probe_instructions[0].va);CHECK(!probe_parse_entries(list));
    wcscpy(list,L"00400001");CHECK(!probe_parse_entries(list));wcscpy(list,L"fffffffff");CHECK(!probe_parse_entries(list));
    /* A start of the second table parses with its kind; push rows keep EP_PUSH. */
    const probe_entry_kind *k=&probe_entry_kinds[0];
    swprintf(list,128,L"%08lx:rcv=esp4,%08lx",(unsigned long)k->va,(unsigned long)probe_instructions[1].va);
    CHECK(probe_parse_entries(list)&&ep_count==2&&probe_sites[0].kind==k->kind&&probe_sites[0].kind!=EP_PUSH&&!probe_sites[0].patch.saved[0]&&
          probe_sites[0].receiver==EP_ESP4&&probe_sites[1].kind==EP_PUSH&&probe_sites[1].patch.saved[0]==probe_instructions[1].opcode);
    CHECK(probe_kind_of(probe_instructions[0].va)==EP_KINDS&&probe_kind_of(0x00400000u)==EP_KINDS);
    swprintf(probe_path,MAX_PATH,L"%lsprobe-%lu.json",temp,(unsigned long)GetCurrentProcessId());
    swprintf(probe_temp,MAX_PATH,L"%ls.tmp",probe_path);
    swprintf(probe_load_path,MAX_PATH,L"%lsprobe-load-%lu.json",temp,(unsigned long)GetCurrentProcessId());
    swprintf(probe_load_temp,MAX_PATH,L"%ls.tmp",probe_load_path);
    BYTE *code=own_code();site_init(probe_sites,code,EP_ESP4);CHECK(ep_install(probe_sites,1,code,4096,NULL,0,LC_COUNT,1));
    probe_enabled=TRUE;probe_install_thread=GetCurrentThreadId();probe_frame_limit=2;
    live_extra_shutdown=probe_shutdown;live_extra_tick=probe_tick;live_extra_active=probe_active;
    DWORD slots[2]={(DWORD)(uintptr_t)fake_exit,(DWORD)(uintptr_t)fake_atexit};CHECK(live_install_exit_slots(slots,slots+1));
    DWORD object=0x76543210;CHECK(call(code,&object)==7);live_coverage_tick(1);
    CHECK(probe_complete&&!ep_active&&*code==0x55);
    live_coverage_shutdown("exit");CHECK(!live_exit_slot&&slots[0]==(DWORD)(uintptr_t)fake_exit);
    FILE *f=_wfopen(probe_path,L"rb");CHECK(f);char text[4096];size_t n=fread(text,1,sizeof(text)-1,f);fclose(f);text[n]=0;
    CHECK(strstr(text,"\"complete\":true")&&strstr(text,"\"count\":1")&&strstr(text,"0x76543210"));
    /* The first tick's report holds the counts before the first frame. */
    f=_wfopen(probe_load_path,L"rb");CHECK(f);n=fread(text,1,sizeof(text)-1,f);fclose(f);text[n]=0;
    CHECK(strstr(text,"\"complete\":false")&&strstr(text,"\"reason\":\"first_tick\"")&&strstr(text,"\"has_tick\":true")&&
          strstr(text,"\"last_frame\":1")&&strstr(text,"\"count\":1")&&strstr(text,"0x76543210"));
    DeleteFileW(probe_load_path);*probe_load_path=0;
    /* Probe-only immediate exit, through coverage's same registration/slot. */
    probe_complete=FALSE;probe_frame_limit=0;site_init(probe_sites,code,0);CHECK(ep_install(probe_sites,1,code,4096,NULL,0,LC_COUNT,1));
    CHECK(live_install_exit_slots(slots,slots+1));live_coverage_exit(73);CHECK(exit_value==73&&probe_complete&&!live_exit_slot);
    exit_callback();CHECK(!ep_active);DeleteFileW(probe_path);DeleteFileW(probe_temp);DeleteFileW(config_path);VirtualFree(code,0,MEM_RELEASE);
    puts("PASS probes runtime: absent section, strict config, shared tick/atexit/immediate exit, complete JSON, first-tick report");
}
static void reference(void) {
    const char *root=getenv("ES2_DATA_ROOT");if(!root||!*root){puts("SKIP: ES2_DATA_ROOT unset");return;}
    char path[4096];snprintf(path,sizeof(path),"%s/VER95/DBSIM.EXE",root);FILE *f=fopen(path,"rb");CHECK(f);
    CHECK(!fseek(f,0,SEEK_END));long size=ftell(f);CHECK(size==724512);rewind(f);BYTE *bytes=malloc(size);CHECK(bytes&&fread(bytes,1,size,f)==(size_t)size);fclose(f);
    HCRYPTPROV provider;HCRYPTHASH hash;CHECK(CryptAcquireContextW(&provider,NULL,NULL,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)&&CryptCreateHash(provider,CALG_SHA_256,0,0,&hash)&&CryptHashData(hash,bytes,size,0));
    BYTE digest[32];DWORD length=32;char hex[65];CHECK(CryptGetHashParam(hash,HP_HASHVAL,digest,&length,0));for(unsigned i=0;i<32;++i)sprintf(hex+2*i,"%02x",digest[i]);CHECK(!strcmp(hex,LC_MODULE_HASH));CryptDestroyHash(hash);CryptReleaseContext(provider,0);
    DWORD pe=lc_u32(bytes+0x3c);CHECK(pe<(DWORD)size-256);unsigned sections=bytes[pe+6]|bytes[pe+7]<<8,optional=bytes[pe+20]|bytes[pe+21]<<8;DWORD table=pe+24+optional;
    CHECK(table<(DWORD)size&&sections<=((DWORD)size-table)/40);
    unsigned matched=0;
    for(unsigned i=0;i<sizeof(probe_instructions)/sizeof(probe_instructions[0]);++i) {
        uint32_t rva=probe_instructions[i].va-0x400000;BOOL found=FALSE;
        for(unsigned j=0;j<sections;++j) {const BYTE *h=bytes+table+40*j;DWORD start=lc_u32(h+12),n=lc_u32(h+16),offset=lc_u32(h+20);
            CHECK(offset<=(DWORD)size&&n<=(DWORD)size-offset);
            if(rva>=start&&rva-start<n) {CHECK(lc_u32(h+36)&0x20000000);CHECK(bytes[offset+rva-start]==probe_instructions[i].opcode);found=TRUE;break;}}
        CHECK(found&&probe_opcode(probe_instructions[i].va)==probe_instructions[i].opcode);++matched;
    }
    /* Every row of the second table is a start of its kind in CODE, sorted and not a push row. */
    unsigned kinds=0;
    for(unsigned i=0;i<sizeof(probe_entry_kinds)/sizeof(probe_entry_kinds[0]);++i) {
        const probe_entry_kind *k=&probe_entry_kinds[i];uint32_t rva=k->va-0x400000;BOOL found=FALSE;
        CHECK(k->kind<EP_KINDS&&(!i||probe_entry_kinds[i-1].va<k->va)&&!probe_opcode(k->va)&&probe_kind_of(k->va)==k->kind);
        for(unsigned j=0;j<sections;++j) {const BYTE *h=bytes+table+40*j;DWORD start=lc_u32(h+12),n=lc_u32(h+16),offset=lc_u32(h+20);
            if(rva>=start&&rva-start<n) {CHECK(lc_u32(h+36)&0x20000000);CHECK(rva-start+ep_kind_length[k->kind]<=n);
                CHECK(ep_kind_size(k->kind,bytes+offset+rva-start)==ep_kind_length[k->kind]);found=TRUE;break;}}
        CHECK(found);++kinds;
    }
    CHECK(kinds>=300);
    printf("PASS reference: %u hash-bound push-register signatures, %u emulated-kind starts; image read only, no game code executed\n",matched,kinds);free(bytes);
}
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    if(!strcmp(argv[1],"engine"))engine();else if(!strcmp(argv[1],"runtime"))runtime();else if(!strcmp(argv[1],"reference"))reference();else return 2;return 0;
}
