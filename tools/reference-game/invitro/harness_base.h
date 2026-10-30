/* The part every in-vitro harness shares: the start-up reservation of the
 * original's image range, the private hash-gated PE mapping with named import
 * traps, the case helpers and the fault handler. Included first by main.c (the
 * hook-group harness, ES2_INVITRO_HOOKS defined) and sim_main.c (one binary per
 * sim suite); the hook registry, the hook sharding and the branch-site coverage
 * exist only in the hook harness.
 *
 * Execute only hash-verified leaves in a private PE image. No loader entry,
 * original DLL, TLS callback, CRT startup, or imported target is executed. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <wincrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include "hook/hook.h"
#ifdef ES2_INVITRO_HOOKS
#include "es2/core/rng.h"
#include "es2/core/geometry.h"
#include "es2/core/timing.h"
#include "es2/core/movement.h"
static WCHAR config_path[MAX_PATH];
static void log_event(const char *s) { (void)s; }
#include "tools/reference-game/proxy/rng_hooks.h"
#endif

static BYTE *mapped,*reserved,initial_fpu[108];
static DWORD reservation_error;
static int reservation_in_stack;
typedef struct stack_layout { void *base,*limit,*allocation; SIZE_T uncommitted; DWORD state,type; } stack_layout;
static stack_layout main_stack;
static stack_layout current_stack(void) {
    const NT_TIB *tib=(const NT_TIB *)NtCurrentTeb();
    MEMORY_BASIC_INFORMATION info={0},bottom={0};
    VirtualQuery(&info,&info,sizeof(info));
    VirtualQuery(info.AllocationBase,&bottom,sizeof(bottom));
    stack_layout result={tib->StackBase,tib->StackLimit,info.AllocationBase,bottom.RegionSize,bottom.State,bottom.Type}; return result;
}
static void print_stack(const char *name,stack_layout s) {
    printf("STACK %s base=%p limit=%p allocation=%p reserve=%lu uncommitted=%lu state=%lx type=%lx\n",name,s.base,s.limit,s.allocation,
        (unsigned long)((uintptr_t)s.base-(uintptr_t)s.allocation),(unsigned long)s.uncommitted,(unsigned long)s.state,(unsigned long)s.type);
}
extern void __cdecl mainCRTStartup(void);
#include "startup_reservation.h"
void __cdecl es2_invitro_start(void) {
    /* Claim the original's preferred range before the CRT or any other thread
     * can allocate there (see startup_reservation.h). This thread then runs
     * the CRT and main, which only creates and joins the 16 MiB test worker;
     * it must not exit while the mapping lives in its stack reservation. No
     * original bytes are read before the hash gate. */
    main_stack=current_stack();
    reserved=claim_image_range(main_stack.allocation,&reservation_error,&reservation_in_stack);
    mainCRTStartup();
    ExitProcess(120);
}
static char imports[1024][192];
static unsigned import_count;
static void *first_import_trap;
/* Only the own CW3220 memset/memcpy emulations execute (probe_call.S). */
extern void __cdecl es2_rt_memset(void);
extern void __cdecl es2_rt_memcpy(void);
static jmp_buf trap_return;
static jmp_buf divide_return;
static volatile int expecting_divide;
static volatile uintptr_t divide_site;
static const char *current_function;
static unsigned current_case;
static int current_case_valid;
static uint32_t random_state=0x48c06e21u;
/* Case budget per hook. The full tier keeps every generator's own count
 * (about one million random cases after its boundary prefix). The quick tier
 * (--quick) caps the random budget at ES2_INVITRO_QUICK_CASES; every
 * coverage window (at most 65,536 cases) and boundary prefix lies inside it,
 * so the branch-site requirement is unchanged. A window is an upper bound:
 * coverage.h disarms each site at its second outcome (early exit), so the
 * case counts stay but the INT3 cost ends early. Installed-adapter runs pass
 * their own limit and are never capped. */
#define ES2_INVITRO_QUICK_CASES 100000u
static int quick_tier;
static unsigned case_budget(unsigned limit,unsigned full) {
    if(limit) return limit;
    return quick_tier && full>ES2_INVITRO_QUICK_CASES ? ES2_INVITRO_QUICK_CASES : full;
}
static uint32_t random32(void) { uint32_t x=random_state; x^=x<<13; x^=x>>17; x^=x<<5; return random_state=x; }
static uint16_t get16(const BYTE *p) { return (uint16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8)); }
static uint32_t get32(const BYTE *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static void put32(BYTE *p,uint32_t x) { for(unsigned i=0;i<4;++i) p[i]=(BYTE)(x>>(i*8)); }
static void put16(BYTE *p,uint16_t x) { p[0]=(BYTE)x; p[1]=(BYTE)(x>>8); }
static void random_bytes(BYTE *p,size_t n) { for(size_t i=0;i<n;++i) p[i]=(BYTE)random32(); }
/* Generated data words kept out of the running thread's 16 MiB stack
 * reservation (bit 31 flipped), for normalisers that classify stack values
 * against a per-side window: such a word would be classified on one side
 * only, depending on ASLR. The stream's consumption does not change. */
static uint32_t stack_clean(uint32_t v) {
    const uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase; return top-v-1u<0x1000000u?v^0x80000000u:v;
}
/* every dword at any byte offset; ascending, so a flip of byte i+3 is seen by the later offsets */
static void stack_clean_bytes(BYTE *p,size_t n) { for(size_t i=0;i+4<=n;++i) if(stack_clean(get32(p+i))!=get32(p+i)) p[i+3]^=0x80u; }
/* Regression guard: a classified value in the reservation's uncommitted part
 * [AllocationBase, StackLimit) is no live stack address, so it is a data
 * word that escaped stack_clean (about 1 in 256 raw words lands there). */
static int stack_uncommitted(uint32_t v) {
    static uint32_t top,low; const NT_TIB *tib=(const NT_TIB *)NtCurrentTeb();
    if(top!=(uint32_t)(uintptr_t)tib->StackBase) { MEMORY_BASIC_INFORMATION m={0}; VirtualQuery(&m,&m,sizeof(m));
        top=(uint32_t)(uintptr_t)tib->StackBase; low=(uint32_t)(uintptr_t)m.AllocationBase; }
    return v-low<(uint32_t)(uintptr_t)tib->StackLimit-low;
}
static void __cdecl import_trap(unsigned id) {
    fprintf(stderr,"TRAP: %s reached import %s\n",current_function,id<import_count?imports[id]:"invalid");
    longjmp(trap_return,1);
}
static int fail(const char *s) {
    DWORD error=GetLastError();
    if(current_case_valid) fprintf(stderr,"FAIL: %s case %u: %s (Win32 %lu)\n",current_function,current_case,s,(unsigned long)error);
    else fprintf(stderr,"FAIL: %s (Win32 %lu)\n",s,(unsigned long)error);
    return 1;
}
#ifdef ES2_INVITRO_HOOKS
#include "sharding.h"
#include "coverage.h"
#else
#include "suite_sharding.h"
#endif
static LONG CALLBACK fault_handler(EXCEPTION_POINTERS *fault) {
#ifdef ES2_INVITRO_HOOKS
    if(coverage_fault(fault)==EXCEPTION_CONTINUE_EXECUTION) return EXCEPTION_CONTINUE_EXECUTION;
#endif
    if(expecting_divide && (fault->ExceptionRecord->ExceptionCode==EXCEPTION_INT_DIVIDE_BY_ZERO ||
                           fault->ExceptionRecord->ExceptionCode==EXCEPTION_INT_OVERFLOW)) {
        divide_site=(uintptr_t)fault->ExceptionRecord->ExceptionAddress;
        expecting_divide=0;longjmp(divide_return,1);
    }
    fprintf(stderr,"FAIL: %s raised exception 0x%08lx at %p\n",current_function?current_function:"loader",
        (unsigned long)fault->ExceptionRecord->ExceptionCode,fault->ExceptionRecord->ExceptionAddress);
    if(current_case_valid) fprintf(stderr,"  during case %u\n",current_case);
    ExitProcess(1);
}
static int extent(uint32_t p,uint32_t n,uint32_t size) { return p<=size && n<=size-p; }

#define IN_VITRO_FILE_SIZE 724512
#define IN_VITRO_IMAGE_SIZE 0x2d8000
#define IN_VITRO_SHA256 "8561aa47e5bb1bf1f837044b08e5d807e8de492784331708a7ddeabf6d44c4ad"
#define IN_VITRO_EXE "DBSIM.EXE"
#define IN_VITRO_LABEL "DBSIM"
#define IN_VITRO_FPU_INIT() do { unsigned short cw=0x1332; __asm__ volatile("fninit; fldcw %0"::"m"(cw)); es2_hook_fpu_save(initial_fpu); } while(0)
#include "mapped_image.h"
/* Run the harness body on a 16 MiB worker thread once the image range is
 * claimed (the start thread keeps the mapping inside its stack reservation
 * alive and only joins). */
static int run_harness_worker(LPTHREAD_START_ROUTINE body) {
    const char *root=getenv("ES2_DATA_ROOT");
    if(!root||!*root) { puts("SKIP: ES2_DATA_ROOT is not set; no original data read"); return 0; }
    if(!reserved) {
        print_stack("main",main_stack);
        MEMORY_BASIC_INFORMATION region={0}; WCHAR mapping[MAX_PATH]={0};
        VirtualQuery((void *)(uintptr_t)0x400000,&region,sizeof(region));
        GetMappedFileNameW(GetCurrentProcess(),(void *)(uintptr_t)0x400000,mapping,MAX_PATH);
        fprintf(stderr,"base collision before worker: allocation=%p region=%p size=%lu state=%lx type=%lx mapping=%ls\n",
            region.AllocationBase,region.BaseAddress,(unsigned long)region.RegionSize,
            (unsigned long)region.State,(unsigned long)region.Type,mapping);
        SetLastError(reservation_error); return fail("preferred image base unavailable");
    }
    HANDLE worker=CreateThread(NULL,0x1000000,body,NULL,STACK_SIZE_PARAM_IS_A_RESERVATION,NULL);
    if(!worker) return fail("create harness worker");
    DWORD result=1;
    BOOL joined=WaitForSingleObject(worker,INFINITE)==WAIT_OBJECT_0 && GetExitCodeThread(worker,&result);
    DWORD error=GetLastError(); CloseHandle(worker);
    if(!joined) { SetLastError(error); return fail("join harness worker"); }
    return (int)result;
}
