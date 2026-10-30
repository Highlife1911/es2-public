/* Own synthetic probes: nothing from DBSIM is embedded here. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../live_coverage_engine.h"
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL line %d: %s (error %ld)\n",__LINE__,#x,(long)lc_error); return 1; } } while(0)
void *lc_probe_target;
uint32_t lc_probe_flags,lc_probe_result[9];
__attribute__((aligned(16))) BYTE lc_probe_fpu[512];
extern void lc_probe(void);
static LONG later_hits;
static LONG CALLBACK later_handler(EXCEPTION_POINTERS *e) {
    if(e->ExceptionRecord->ExceptionCode==0xe1234567) { ++later_hits; return EXCEPTION_CONTINUE_EXECUTION; }
    return EXCEPTION_CONTINUE_SEARCH;
}
static int execution(void) {
    lc_track_ticks=TRUE;
    BYTE *code=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE); CHECK(code);
    PVOID later=AddVectoredExceptionHandler(0,later_handler); CHECK(later);
    for(unsigned mode=0;mode<2;++mode) for(unsigned c=0;c<LC_CONDITIONS;++c) for(unsigned wide=0;wide<2;++wide) {
        memset(code,0x90,64); unsigned length=wide?6:2;
        code[0]=wide?0x0f:(BYTE)(0x70+lc_opcode[c]);
        if(wide) {code[1]=(BYTE)(0x80+lc_opcode[c]); code[2]=6; code[3]=code[4]=code[5]=0;} else code[1]=6;
        code[length]=0xb8; memset(code+length+1,0x11,4); code[length+5]=0xc3;
        code[length+6]=0xb8; memset(code+length+7,0x22,4); code[length+11]=0xc3;
        lc_site site={0};site.address=(uintptr_t)code;site.length=length;site.condition=c;site.target=site.address+length+6;
        uint32_t expected[64][9]; BYTE fpu[64][512]; lc_probe_target=code;
        for(unsigned f=0;f<64;++f) { lc_probe_flags=0x202|((f&1)?1:0)|((f&2)?0x40:0)|((f&4)?0x80:0)|((f&8)?0x800:0)|((f&16)?0x10:0)|((f&32)?4:0); lc_probe(); memcpy(expected[f],lc_probe_result,sizeof(lc_probe_result)); memcpy(fpu[f],lc_probe_fpu,512); }
        CHECK(lc_install(&site,1,code,64,NULL,0,mode));
        unsigned outcomes[2]={0};
        for(unsigned f=0;f<64;++f) { lc_probe_flags=0x202|((f&1)?1:0)|((f&2)?0x40:0)|((f&4)?0x80:0)|((f&8)?0x800:0)|((f&16)?0x10:0)|((f&32)?4:0); ++outcomes[expected[f][7]==0x22222222]; SetLastError(0x1234); lc_probe(); CHECK(GetLastError()==0x1234); CHECK(!memcmp(expected[f],lc_probe_result,sizeof(lc_probe_result))); CHECK(!memcmp(fpu[f],lc_probe_fpu,512)); }
        CHECK(site.fall && site.taken);
        if(mode==LC_COUNT) CHECK(site.fall==outcomes[0] && site.taken==outcomes[1] && !site.restored);
        else CHECK(site.restored && code[0]==site.saved[0]);
        DWORD ignored=0; RaiseException(0xe1234567,0,0,NULL); (void)ignored; CHECK(later_hits>0);
        CHECK(lc_uninstall()); CHECK(code[0]==site.saved[0]);
    }
    lc_track_ticks=FALSE;RemoveVectoredExceptionHandler(later);VirtualFree(code,0,MEM_RELEASE);
    puts("PASS: 16 predicates (jbe, js, jp, jnp, jo, jno included) x rel8/rel32 x count/saturate; 64 independent CF/ZF/SF/OF/AF/PF states, all GPR/ESP/EFLAGS/FXSAVE equal; later VEH coexists");return 0;
}

static unsigned flush_calls;
static BOOL WINAPI fail_once_flush(HANDLE process,LPCVOID address,SIZE_T size) {
    if(++flush_calls==1)return FALSE;return FlushInstructionCache(process,address,size);
}
static int refusal(void) {
    BYTE *code=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);CHECK(code);
    BYTE own[]={0x74,2,0x90,0x90,0xc3};memcpy(code,own,sizeof(own));
    lc_site sites[2]={{0}};sites[0].address=(uintptr_t)code;sites[0].length=2;sites[0].condition=LC_JE;sites[0].target=(uintptr_t)code+4;
    sites[1]=sites[0];sites[1].address++; /* overlapping and not a valid Jcc */
    CHECK(!lc_install(sites,2,code,4096,NULL,0,LC_COUNT) && !memcmp(code,own,sizeof(own)) && !lc_handler);
    sites[1]=sites[0];CHECK(!lc_install(sites,2,code,4096,NULL,0,LC_COUNT));
    lc_range range={(uintptr_t)code,5};CHECK(!lc_install(sites,1,code,4096,&range,1,LC_COUNT));
    ++sites[0].target;CHECK(!lc_install(sites,1,code,4096,NULL,0,LC_COUNT));--sites[0].target;
    sites[0].condition=LC_JNE;CHECK(!lc_install(sites,1,code,4096,NULL,0,LC_COUNT));sites[0].condition=LC_JE;
    /* The opcode must match the condition: JE bytes are neither JBE (jbe 76) nor out of range. */
    sites[0].condition=LC_JBE;CHECK(!lc_install(sites,1,code,4096,NULL,0,LC_COUNT));sites[0].condition=LC_CONDITIONS;CHECK(!lc_install(sites,1,code,4096,NULL,0,LC_COUNT));sites[0].condition=LC_JE;
    {BYTE jbe_bytes[]={0x76,2,0x90,0x90,0xc3};BYTE *at=code+64;memcpy(at,jbe_bytes,sizeof(jbe_bytes));lc_site jbe={.address=(uintptr_t)at,.length=2,.condition=LC_JBE,.target=(uintptr_t)at+4};
     CHECK(lc_validate(&jbe,1,code,4096,NULL,0));jbe.condition=LC_JA;CHECK(!lc_validate(&jbe,1,code,4096,NULL,0));memset(at,0x90,sizeof(jbe_bytes));}
    sites[0].length=6;CHECK(!lc_install(sites,1,code,4096,NULL,0,LC_COUNT));sites[0].length=2;
    lc_flush=fail_once_flush;flush_calls=0;CHECK(!lc_install(sites,1,code,4096,NULL,0,LC_COUNT));CHECK(!lc_handler && !memcmp(code,own,sizeof(own)));lc_flush=FlushInstructionCache;
    CHECK(lc_install(sites,1,code,4096,NULL,0,LC_COUNT));code[1]=3;CHECK(!lc_uninstall() && code[0]==0xcc && lc_handler);code[1]=2;
    code[0]=0x90;CHECK(!lc_uninstall() && code[0]==0x90);code[0]=0xcc;CHECK(lc_uninstall() && !memcmp(code,own,sizeof(own)));
    EXCEPTION_RECORD record={0};CONTEXT context={0},before;EXCEPTION_POINTERS fault={&record,&context};
    lc_track_ticks=TRUE;
    CHECK(lc_install(sites,1,code,4096,NULL,0,LC_COUNT));
    memset(&context,0xa5,sizeof(context));before=context;record.ExceptionCode=EXCEPTION_ACCESS_VIOLATION;record.ExceptionAddress=code;
    CHECK(lc_exception(&fault)==EXCEPTION_CONTINUE_SEARCH && !memcmp(&context,&before,sizeof(context)));
    record.ExceptionCode=EXCEPTION_BREAKPOINT;record.ExceptionAddress=code+15;CHECK(lc_exception(&fault)==EXCEPTION_CONTINUE_SEARCH);
    record.ExceptionAddress=code;before.Eip=(DWORD)code+4;context.EFlags=before.EFlags=0x246;
    CHECK(lc_exception(&fault)==EXCEPTION_CONTINUE_EXECUTION && !memcmp(&context,&before,sizeof(context)));
    CHECK(sites[0].first_taken_tick==-1 && sites[0].first_fall_tick==INT64_MIN);InterlockedExchange64(&lc_last_tick,7);
    lc_mode=LC_SATURATE;context.EFlags=before.EFlags=0x202;before.Eip=(DWORD)code+2;
    lc_patch_lock=1;CHECK(lc_exception(&fault)==EXCEPTION_CONTINUE_EXECUTION && !sites[0].restored);lc_patch_lock=0;
    CHECK(lc_exception(&fault)==EXCEPTION_CONTINUE_EXECUTION && sites[0].restored);
    CHECK(lc_exception(&fault)==EXCEPTION_CONTINUE_EXECUTION && !memcmp(&context,&before,sizeof(context))); /* already-fetched INT3 */
    CHECK(sites[0].first_taken_tick==-1 && sites[0].first_fall_tick==7);
    CHECK(lc_uninstall());lc_track_ticks=FALSE;VirtualFree(code,0,MEM_RELEASE);puts("PASS: transactional mismatch/overlap/prologue refusal, failed flush rollback, verified uninstall, first-hit ticks and unrelated exceptions");return 0;
}
/* Edge mode. Layout per site: Jcc at code, fall path "mov eax,0x11111111; ret"
 * at code+length, taken path "mov eax,0x22222222; ret" at code+length+6, a
 * fault byte at code+40. Both destinations are exclusive by construction. */
static uintptr_t fault_at,fault_resume;static DWORD fault_code;
static LONG CALLBACK fault_handler(EXCEPTION_POINTERS *e) {
    if((uintptr_t)e->ExceptionRecord->ExceptionAddress!=fault_at) return EXCEPTION_CONTINUE_SEARCH;
    fault_code=e->ExceptionRecord->ExceptionCode;e->ContextRecord->Eip=(DWORD)fault_resume;return EXCEPTION_CONTINUE_EXECUTION;
}
static void edge_code(BYTE *code,unsigned c,unsigned wide) {
    memset(code,0x90,64);unsigned length=wide?6:2;
    code[0]=wide?0x0f:(BYTE)(0x70+lc_opcode[c]);
    if(wide) {code[1]=(BYTE)(0x80+lc_opcode[c]);code[2]=6;code[3]=code[4]=code[5]=0;} else code[1]=6;
    code[length]=0xb8;memset(code+length+1,0x11,4);code[length+5]=0xc3;
    code[length+6]=0xb8;memset(code+length+7,0x22,4);code[length+11]=0xc3;
    code[40]=0xf4;
}
static int edge(void) {
    BYTE *code=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);CHECK(code);
    /* Every byte the generator may pick faults at its own address before executing. */
    PVOID probe=AddVectoredExceptionHandler(1,fault_handler);CHECK(probe);
    for(unsigned i=0;i<sizeof(lc_trap_bytes);++i) {
        memset(code,0x90,32);code[0]=lc_trap_bytes[i];code[1]=0x60;code[16]=0xc3;fault_at=(uintptr_t)code;fault_resume=(uintptr_t)code+16;fault_code=0;
        ((void (*)(void))code)();
        CHECK(fault_code==(lc_trap_bytes[i]==0xcc?EXCEPTION_BREAKPOINT:EXCEPTION_PRIV_INSTRUCTION));
    }
    RemoveVectoredExceptionHandler(probe);
    lc_track_ticks=TRUE;unsigned exceptions[6]={0};
    /* variant: 0 both destinations, 1 rel8 redirect + fall, 2 no edge help (jcc),
     * 3 fall known, 4 both known, 5 a redirect byte that does not fault (rel8 falls back to jcc). */
    for(unsigned variant=0;variant<6;++variant) for(unsigned c=0;c<LC_CONDITIONS;++c) for(unsigned wide=0;wide<2;++wide) {
        edge_code(code,c,wide);unsigned length=wide?6:2;BYTE original[64];memcpy(original,code,64);
        lc_site site={0};site.address=(uintptr_t)code;site.length=length;site.condition=c;site.target=site.address+length+6;
        site.exclusive[0]=site.exclusive[1]=variant!=2;
        if(variant==1) {site.exclusive[1]=0;site.trap=(uintptr_t)code+40;}
        if(variant==3) site.known[0]=1;
        if(variant==4) site.known[0]=site.known[1]=1;
        if(variant==5) {site.exclusive[1]=0;site.trap=(uintptr_t)code+41;code[41]=0x90;memcpy(original,code,64);}
        uint32_t expected[64][9];BYTE fpu[64][512];lc_probe_target=code;
        for(unsigned f=0;f<64;++f) {lc_probe_flags=0x202|((f&1)?1:0)|((f&2)?0x40:0)|((f&4)?0x80:0)|((f&8)?0x800:0)|((f&16)?0x10:0)|((f&32)?4:0);lc_probe();memcpy(expected[f],lc_probe_result,sizeof(lc_probe_result));memcpy(fpu[f],lc_probe_fpu,512);}
        CHECK(lc_install(&site,1,code,64,NULL,0,LC_EDGE));
        LONG roles=site.roles;
        if(variant==0) CHECK(roles==(wide?LC_ROLE_REDIRECT|LC_ROLE_FALL:LC_ROLE_TAKEN|LC_ROLE_FALL));
        if(variant==1) CHECK(roles==(LC_ROLE_REDIRECT|LC_ROLE_FALL) && code[1]==(BYTE)(wide?code[1]:40-2));
        if(variant==2) CHECK(roles==(wide?LC_ROLE_REDIRECT|LC_ROLE_JCC:LC_ROLE_JCC));
        if(variant==3) CHECK(roles==(wide?LC_ROLE_REDIRECT:LC_ROLE_TAKEN));
        if(variant==4) CHECK(roles==0 && !memcmp(code,original,64));
        if(variant==5) CHECK(roles==(wide?LC_ROLE_REDIRECT|LC_ROLE_FALL:LC_ROLE_JCC|LC_ROLE_FALL));
        unsigned outcomes[2]={0};
        for(unsigned f=0;f<64;++f) {lc_probe_flags=0x202|((f&1)?1:0)|((f&2)?0x40:0)|((f&4)?0x80:0)|((f&8)?0x800:0)|((f&16)?0x10:0)|((f&32)?4:0);++outcomes[expected[f][7]==0x22222222];
            SetLastError(0x1234);lc_probe();CHECK(GetLastError()==0x1234);CHECK(!memcmp(expected[f],lc_probe_result,sizeof(lc_probe_result)));CHECK(!memcmp(fpu[f],lc_probe_fpu,512));}
        CHECK(outcomes[0] && outcomes[1]);
        LONG64 fall=site.fall,taken=site.taken;exceptions[variant]+=(unsigned)(fall+taken);
        if(variant==0 || variant==1 || variant==5) CHECK((fall==1 || (roles&LC_ROLE_JCC)) && taken==1 && (wide || variant!=0 || fall==1));
        if(variant==2) CHECK(fall>=1 && taken>=1);
        if(variant==3) CHECK(fall==0 && taken==1);
        if(variant==4) CHECK(fall==0 && taken==0);
        /* Every outcome seen or known: every role gone, every byte back before uninstall. */
        CHECK(site.roles==0 && !memcmp(code,original,64));
        CHECK(lc_uninstall() && site.restored && !lc_active && !memcmp(code,original,64));
    }
    /* A destination that is another site's Jcc shares one point: the fall of A
     * lands on B; B keeps its jcc role (no edge help), A's fall is one-shot. */
    {   memset(code,0x90,64);
        BYTE layout[]={0x74,0x08, 0x75,0x02, 0x31,0xc0, 0xc3, 0x90,0x90,0x90, 0xb8,0x22,0x22,0x22,0x22,0xc3};
        memcpy(code,layout,sizeof(layout));BYTE original[64];memcpy(original,code,64);
        lc_site two[2]={{0}};
        two[0].address=(uintptr_t)code;two[0].length=2;two[0].condition=LC_JE;two[0].target=(uintptr_t)code+10;two[0].exclusive[0]=1;two[0].exclusive[1]=1;
        two[1].address=(uintptr_t)code+2;two[1].length=2;two[1].condition=LC_JNE;two[1].target=(uintptr_t)code+6;
        CHECK(lc_install(two,2,code,64,NULL,0,LC_EDGE) && lc_point_count==2 && lc_point_find((uintptr_t)code+2)->jcc==1 && lc_point_find((uintptr_t)code+2)->dest==0);
        for(unsigned f=0;f<4;++f) {lc_probe_target=code;lc_probe_flags=0x202|((f&1)?0x40:0);lc_probe();}
        /* B is reached only with ZF clear, so its JNE never falls through: its jcc role stays. */
        CHECK(two[0].fall==1 && two[0].taken==1 && two[0].roles==0 && two[1].taken==2 && two[1].fall==0 && two[1].roles==LC_ROLE_JCC);
        CHECK(lc_uninstall() && !memcmp(code,original,64));
        /* Destination inside another site's bytes and a duplicate trap: refused roles fall back. */
        two[0].exclusive[0]=1;two[0].target=(uintptr_t)code+10;two[1].address=(uintptr_t)code+1;two[1].length=2;
        CHECK(!lc_install(two,2,code,64,NULL,0,LC_EDGE)); /* overlapping sites stay refused by lc_validate */
        lc_site dup[2]={{0}};BYTE pair[]={0x74,0x04,0x74,0x02,0xc3,0x90,0xc3};memcpy(code,pair,sizeof(pair));code[20]=0xcc;
        dup[0].address=(uintptr_t)code;dup[0].length=2;dup[0].condition=LC_JE;dup[0].target=(uintptr_t)code+6;dup[0].trap=(uintptr_t)code+20;
        dup[1]=dup[0];dup[1].address=(uintptr_t)code+2;dup[1].target=(uintptr_t)code+6;
        CHECK(lc_install(dup,2,code,64,NULL,0,LC_EDGE) && dup[0].roles==LC_ROLE_JCC && dup[1].roles==LC_ROLE_JCC);
        CHECK(lc_uninstall() && code[0]==0x74 && code[2]==0x74);
    }
    /* Handler: a released destination (INT3 fetched before the release) re-executes;
     * an unrelated breakpoint and an unrelated fault stay with later handlers. */
    {   edge_code(code,LC_JE,0);lc_site site={0};site.address=(uintptr_t)code;site.length=2;site.condition=LC_JE;site.target=(uintptr_t)code+8;site.exclusive[0]=site.exclusive[1]=1;
        CHECK(lc_install(&site,1,code,64,NULL,0,LC_EDGE));
        EXCEPTION_RECORD record={0};CONTEXT context={0};EXCEPTION_POINTERS fault={&record,&context};
        record.ExceptionCode=EXCEPTION_BREAKPOINT;record.ExceptionAddress=code+2;context.Eip=0;
        CHECK(lc_edge_exception(&fault)==EXCEPTION_CONTINUE_EXECUTION && context.Eip==(DWORD)code+2 && site.fall==1 && code[2]==0xb8);
        CHECK(lc_edge_exception(&fault)==EXCEPTION_CONTINUE_EXECUTION && context.Eip==(DWORD)code+2 && site.fall==1);
        record.ExceptionAddress=code+30;CHECK(lc_edge_exception(&fault)==EXCEPTION_CONTINUE_SEARCH);
        record.ExceptionCode=EXCEPTION_ACCESS_VIOLATION;record.ExceptionAddress=code+8;CHECK(lc_edge_exception(&fault)==EXCEPTION_CONTINUE_SEARCH);
        CHECK(lc_uninstall() && code[8]==0xb8);
    }
    lc_track_ticks=FALSE;VirtualFree(code,0,MEM_RELEASE);
    printf("PASS: edge mode, 16 predicates x rel8/rel32 x 6 role variants; %u fault bytes; exceptions per variant %u/%u/%u/%u/%u/%u; shared point, duplicate trap, released point\n",
        (unsigned)sizeof(lc_trap_bytes),exceptions[0],exceptions[1],exceptions[2],exceptions[3],exceptions[4],exceptions[5]);return 0;
}
static DWORD WINAPI worker(void *code) {
    void (*call)(void)=(void (*)(void))code;for(unsigned i=0;i<2000;++i)call();return 0;
}
static int threads(void) {
    BYTE *code=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);CHECK(code);
    BYTE own[]={0x74,2,0xc3,0x90,0xc3};memcpy(code,own,sizeof(own));
    lc_site site={.address=(uintptr_t)code,.length=2,.condition=LC_JE,.target=(uintptr_t)code+4};
    CHECK(lc_install(&site,1,code,4096,NULL,0,LC_COUNT));worker(code);
    HANDLE a=CreateThread(NULL,0,worker,code,0,NULL),b=CreateThread(NULL,0,worker,code,0,NULL);CHECK(a&&b);
    CHECK(WaitForSingleObject(a,10000)==WAIT_OBJECT_0 && WaitForSingleObject(b,10000)==WAIT_OBJECT_0);CloseHandle(a);CloseHandle(b);
    CHECK(site.fall+site.taken==6000 && lc_first_thread==(LONG)GetCurrentThreadId() && lc_other_thread);
    CHECK(lc_uninstall());VirtualFree(code,0,MEM_RELEASE);puts("PASS: three executors, exact atomic totals and foreign-thread detection");return 0;
}
static void bench_call(void *target,unsigned value) {
    __asm__ volatile("testl %1,%1; call *%0"::"r"(target),"r"(value):"eax","ecx","edx","cc","memory");
}
static int benchmark(void) {
    enum {SITES=210,N=100000};
    BYTE *code=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);CHECK(code);
    lc_site sites[SITES];memset(sites,0,sizeof(sites));
    for(unsigned i=0;i<SITES;++i) {
        BYTE *p=code+16*i;p[0]=0x74;p[1]=2;p[2]=p[4]=0xc3;
        sites[i].address=(uintptr_t)p;sites[i].length=2;sites[i].condition=LC_JE;sites[i].target=(uintptr_t)p+4;
    }
    LARGE_INTEGER frequency,start,end;CHECK(QueryPerformanceFrequency(&frequency));
    for(unsigned sample=0;sample<3;++sample)for(unsigned kind=0;kind<4;++kind) {
        unsigned index=kind==0?0:SITES-1,mode=kind<2?LC_COUNT:LC_SATURATE;
        QueryPerformanceCounter(&start);for(unsigned i=0;i<N;++i)bench_call((void *)sites[index].address,0);QueryPerformanceCounter(&end);
        double base=(double)(end.QuadPart-start.QuadPart)/frequency.QuadPart;
        CHECK(lc_install(sites,SITES,code,4096,NULL,0,mode));
        QueryPerformanceCounter(&start);
        for(unsigned i=0;i<N;++i)bench_call((void *)sites[index].address,kind==3 && i==0?1:0);
        QueryPerformanceCounter(&end);double seconds=(double)(end.QuadPart-start.QuadPart)/frequency.QuadPart;
        LONG64 exceptions=lc_read64(&sites[index].fall)+lc_read64(&sites[index].taken);
        CHECK(exceptions==(kind==3?2:N));CHECK(lc_uninstall());
        printf("COST sample=%u scenario=%s sites=%u calls=%u exceptions=%lld baseline_s=%.9f instrumented_s=%.9f exceptions_per_s=%.0f added_us_per_call=%.6f\n",
            sample,kind==0?"count_first":kind==1?"count_last":kind==2?"saturate_one_way":"saturate_both",SITES,N,(long long)exceptions,base,seconds,exceptions/seconds,(seconds-base)*1e6/N);
    }
    /* Edge mode on the same one-way site: one exception in total. */
    for(unsigned i=0;i<SITES;++i) {sites[i].exclusive[0]=sites[i].exclusive[1]=1;}
    for(unsigned sample=0;sample<3;++sample) {
        QueryPerformanceCounter(&start);for(unsigned i=0;i<N;++i)bench_call((void *)sites[SITES-1].address,0);QueryPerformanceCounter(&end);
        double base=(double)(end.QuadPart-start.QuadPart)/frequency.QuadPart;
        CHECK(lc_install(sites,SITES,code,4096,NULL,0,LC_EDGE));
        QueryPerformanceCounter(&start);for(unsigned i=0;i<N;++i)bench_call((void *)sites[SITES-1].address,0);QueryPerformanceCounter(&end);
        double seconds=(double)(end.QuadPart-start.QuadPart)/frequency.QuadPart;
        LONG64 exceptions=lc_read64(&sites[SITES-1].fall)+lc_read64(&sites[SITES-1].taken);
        CHECK(exceptions==1);CHECK(lc_uninstall());
        printf("COST sample=%u scenario=edge_one_way sites=%u calls=%u exceptions=%lld baseline_s=%.9f instrumented_s=%.9f added_us_per_call=%.6f\n",
            sample,SITES,N,(long long)exceptions,base,seconds,(seconds-base)*1e6/N);
    }
    VirtualFree(code,0,MEM_RELEASE);return 0;
}
int main(int argc,char **argv) { return argc>1 && !strcmp(argv[1],"benchmark")?benchmark():execution() || refusal() || threads() || edge(); }


